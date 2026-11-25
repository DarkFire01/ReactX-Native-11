/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Heaps over memory the application already has: its own pages or a file mapping
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "sharedrecord12.hpp"

#include <winternl.h>

/* Existing heaps are made by the kernel over the memory, then opened like a shared heap */
constexpr D3D12_HEAP_FLAGS RxExistingHeapFlags = (D3D12_HEAP_FLAGS)(D3D12_HEAP_FLAG_SHARED |
                                                                    D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER |
                                                                    D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS);

/* Existing memory has to fit a 32 bit size */
constexpr UINT64 RxExistingHeapLimit = 0x100000000ull;

/* Page protections the GPU cannot write through, or that cannot be cached coherently */
constexpr DWORD RxUnusableProtection = PAGE_NOACCESS | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_NOCACHE;

constexpr UINT64 RxPageSize = 0x1000;
constexpr UINT64 RxLargeAlignment = 0x400000;

/* Section information as NtQuerySection's basic class returns it */
struct RxSectionBasics
{
    PVOID BaseAddress;
    ULONG AllocationAttributes;
    LARGE_INTEGER MaximumSize;
};

typedef NTSTATUS (NTAPI* RxQuerySectionRoutine)(HANDLE, ULONG, PVOID, SIZE_T, PSIZE_T);

/* The desc every existing heap starts from: custom, CPU cached unless the memory is write combined */
static
VOID
RxExistingHeapDesc(
    _In_ UINT64 Size,
    _In_ BOOLEAN WriteCombined,
    _In_ UINT64 Alignment,
    _Out_ D3D12_HEAP_DESC* Desc)
{
    ZeroMemory(Desc, sizeof(*Desc));
    Desc->SizeInBytes = Size;
    Desc->Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    Desc->Properties.CPUPageProperty = WriteCombined ? D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE
                                                     : D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    Desc->Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    Desc->Properties.CreationNodeMask = 1;
    Desc->Properties.VisibleNodeMask = 1;
    Desc->Alignment = Alignment;
    Desc->Flags = RxExistingHeapFlags;
}

static
BOOLEAN
RxIsUsableRegion(
    _In_ const MEMORY_BASIC_INFORMATION& Region)
{
    return (Region.State & (MEM_COMMIT | MEM_RESERVE | MEM_FREE)) == MEM_COMMIT && Region.Protect != 0 &&
           (Region.Protect & RxUnusableProtection) == 0;
}

/*
 * Memory from VirtualAlloc: without a size the whole allocation starting at the address, else
 * the pages from the address on. Every page has to be committed alike and writable.
 */
static
HRESULT
RxCheckExistingAddress(
    _In_ RxDevice12& Device,
    _In_opt_ const VOID* Address,
    _In_ UINT64 Size,
    _Out_ D3D12_HEAP_DESC* Desc)
{
    D3D12_FEATURE_DATA_EXISTING_HEAPS Existing = {};
    MEMORY_BASIC_INFORMATION First;
    MEMORY_BASIC_INFORMATION Region;
    ULONG_PTR Start = (ULONG_PTR)Address;
    ULONG_PTR Next;
    UINT64 Total;
    UINT64 Alignment;
    BOOLEAN WholeAllocation = Size == 0;

    if (Address == nullptr || VirtualQuery(Address, &First, sizeof(First)) == 0)
        return E_INVALIDARG;

    if (WholeAllocation)
    {
        if (First.AllocationBase != Address)
            return E_INVALIDARG;
    }
    else
    {
        if (First.BaseAddress != Address)
            return E_INVALIDARG;
        Size = (Size + RxPageSize - 1) & ~(RxPageSize - 1);
    }

    Region = First;
    Total = First.RegionSize;
    for (;;)
    {
        if (!RxIsUsableRegion(Region))
            return E_INVALIDARG;

        Next = (ULONG_PTR)Region.BaseAddress + Region.RegionSize;
        if (!WholeAllocation && Next >= Start + Size)
            break;

        /* The whole allocation ends where the next region belongs to another one */
        if (VirtualQuery((const VOID*)Next, &Region, sizeof(Region)) == 0 ||
            Region.AllocationBase != First.AllocationBase)
        {
            if (WholeAllocation)
                break;
            return E_INVALIDARG;
        }
        if (Region.Protect != First.Protect || Region.State != First.State)
            return E_INVALIDARG;
        Total += Region.RegionSize;
    }

    if (!WholeAllocation)
        Total = Size;
    if (Total >= RxExistingHeapLimit)
        return E_INVALIDARG;

    if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_EXISTING_HEAPS, &Existing, sizeof(Existing))) ||
        !Existing.Supported)
    {
        return E_INVALIDARG;
    }

    /* The heap is as aligned as the address is, up to 4MB */
    if (Start % RxLargeAlignment == 0)
        Alignment = RxLargeAlignment;
    else if (Start % D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT == 0)
        Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    else
        Alignment = RxPageSize;
    RxExistingHeapDesc(Total, (First.Protect & PAGE_WRITECOMBINE) != 0, Alignment, Desc);

    /* A whole allocation made with write watch can be tracked as one */
    if (WholeAllocation)
    {
        PVOID Written[1];
        ULONG_PTR Count = 1;
        ULONG Granularity;

        if (GetWriteWatch(0, First.AllocationBase, RxPageSize, Written, &Count, &Granularity) == 0)
            Desc->Flags |= D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH;
    }

    return S_OK;
}

/* A pagefile or file section the process can map for both reading and writing */
static
HRESULT
RxCheckExistingMapping(
    _In_ RxDevice12& Device,
    _In_opt_ HANDLE Mapping,
    _Out_ D3D12_HEAP_DESC* Desc)
{
    static RxQuerySectionRoutine QuerySection =
        reinterpret_cast<RxQuerySectionRoutine>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySection"));
    D3D12_FEATURE_DATA_EXISTING_HEAPS Existing = {};
    RxSectionBasics Section = {};
    SIZE_T Probe;
    VOID* View;

    if (Mapping == nullptr || QuerySection == nullptr)
        return E_INVALIDARG;
    if (!NT_SUCCESS(QuerySection(Mapping, 0, &Section, sizeof(Section), nullptr)))
        return E_INVALIDARG;

    /* Reserved sections commit on access, and uncached ones cannot be snooped */
    if ((Section.AllocationAttributes & (SEC_RESERVE | SEC_NOCACHE)) != 0)
        return E_INVALIDARG;

    /* Mapping its first page proves the handle grants full access */
    Probe = (Section.AllocationAttributes & SEC_64K_PAGES) != 0 ? 0x10000 : RxPageSize;
    if ((UINT64)Section.MaximumSize.QuadPart < Probe)
        Probe = (SIZE_T)Section.MaximumSize.QuadPart;
    View = MapViewOfFile(Mapping, FILE_MAP_ALL_ACCESS, 0, 0, Probe);
    if (View == nullptr)
        return E_INVALIDARG;
    UnmapViewOfFile(View);

    if ((UINT64)Section.MaximumSize.QuadPart >= RxExistingHeapLimit)
        return E_INVALIDARG;

    if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_EXISTING_HEAPS, &Existing, sizeof(Existing))) ||
        !Existing.Supported)
    {
        return E_INVALIDARG;
    }

    RxExistingHeapDesc(Section.MaximumSize.QuadPart, (Section.AllocationAttributes & SEC_WRITECOMBINE) != 0,
                       D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT, Desc);
    return S_OK;
}

/* The kernel allocation over the memory, shared once by NT handle so it opens like any shared heap */
static
HRESULT
RxShareExistingMemory(
    _In_ RxDevice12& Device,
    _In_opt_ HANDLE Mapping,
    _In_opt_ const VOID* Address,
    _In_ const D3D12_HEAP_DESC& Desc,
    _Out_ HANDLE* SharedHandle)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATESTANDARDALLOCATION Standard = {};
    D3DDDI_ALLOCATIONINFO2 Allocation = {};
    D3DKMT_CREATEALLOCATION Create = {};
    D3DKMT_DESTROYALLOCATION2 Destroy = {};
    OBJECT_ATTRIBUTES Attributes;
    RxSharedRecord12 Record;
    NTSTATUS Status;
    HRESULT hr;

    *SharedHandle = nullptr;
    if (Kmt->CreateAllocation2 == nullptr || Kmt->DestroyAllocation2 == nullptr || Kmt->ShareObjects == nullptr)
        return E_NOTIMPL;

    RxFillSharedRecord12(Desc, nullptr, Record);

    Standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    Standard.ExistingHeapData.Size = Desc.SizeInBytes;
    if (Address != nullptr)
        Allocation.pSystemMem = Address;
    else
        Allocation.hSection = Mapping;

    Create.hDevice = Device.KernelDevice();
    Create.pPrivateRuntimeData = &Record;
    Create.PrivateRuntimeDataSize = sizeof(Record);
    Create.pStandardAllocation = &Standard;
    Create.NumAllocations = 1;
    Create.pAllocationInfo2 = &Allocation;
    Create.Flags.CreateResource = TRUE;
    Create.Flags.CreateShared = TRUE;
    Create.Flags.NtSecuritySharing = TRUE;
    Create.Flags.CrossAdapter = TRUE;
    Create.Flags.StandardAllocation = TRUE;
    Create.Flags.ExistingSysMem = Address != nullptr;
    Create.Flags.ExistingSection = Address == nullptr;
    Status = Kmt->CreateAllocation2(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"existing memory allocation failed 0x%08lX", Status);
        return RxApiResult12(Device, Status, "D3DKMTCreateAllocation2");
    }

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr, nullptr);
    hr = RxApiResult12(Device, Kmt->ShareObjects(1, &Create.hResource, &Attributes, GENERIC_ALL, SharedHandle),
                       "D3DKMTShareObjects");

    /* The handle keeps the allocation alive; this device opens it again like any other */
    Destroy.hDevice = Device.KernelDevice();
    Destroy.hResource = Create.hResource;
    Kmt->DestroyAllocation2(&Destroy);
    return hr == E_HANDLE ? DXGI_ERROR_NAME_ALREADY_EXISTS : hr;
}

static
HRESULT
RxOpenExistingHeap(
    _In_ RxDevice12& Device,
    _In_opt_ HANDLE Mapping,
    _In_opt_ const VOID* Address,
    _In_ const D3D12_HEAP_DESC& Desc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvHeap)
{
    IUnknown* Object;
    HANDLE SharedHandle;
    HRESULT hr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    hr = RxShareExistingMemory(Device, Mapping, Address, Desc, &SharedHandle);
    if (FAILED(hr))
        return hr;

    hr = RxOpenSharedResource12(Device, SharedHandle, &Object);
    CloseHandle(SharedHandle);
    if (FAILED(hr))
        return hr;

    hr = Object->QueryInterface(riid, ppvHeap);
    Object->Release();
    return hr;
}

/* Without an output the call only says whether the memory would do */
static
HRESULT
RxOpenExistingAddress(
    _In_ RxDevice12& Device,
    _In_opt_ const VOID* Address,
    _In_ UINT64 Size,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvHeap)
{
    D3D12_HEAP_DESC Desc;
    HRESULT hr;

    if (ppvHeap != nullptr)
        *ppvHeap = nullptr;

    hr = RxCheckExistingAddress(Device, Address, Size, &Desc);
    if (FAILED(hr) || ppvHeap == nullptr)
        return FAILED(hr) ? hr : S_FALSE;

    return RxOpenExistingHeap(Device, nullptr, Address, Desc, riid, ppvHeap);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::OpenExistingHeapFromAddress(
    _In_ const void* pAddress,
    REFIID riid,
    _COM_Outptr_ void** ppvHeap)
{
    return RxOpenExistingAddress(*this, pAddress, 0, riid, ppvHeap);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::OpenExistingHeapFromAddress1(
    _In_ const void* pAddress,
    SIZE_T size,
    REFIID riid,
    _COM_Outptr_ void** ppvHeap)
{
    if (ppvHeap != nullptr)
        *ppvHeap = nullptr;
    if (size == 0)
        return E_INVALIDARG;

    return RxOpenExistingAddress(*this, pAddress, size, riid, ppvHeap);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::OpenExistingHeapFromFileMapping(
    _In_ HANDLE hFileMapping,
    REFIID riid,
    _COM_Outptr_ void** ppvHeap)
{
    D3D12_HEAP_DESC Desc;
    HRESULT hr;

    if (ppvHeap != nullptr)
        *ppvHeap = nullptr;

    hr = RxCheckExistingMapping(*this, hFileMapping, &Desc);
    if (FAILED(hr) || ppvHeap == nullptr)
        return FAILED(hr) ? hr : S_FALSE;

    return RxOpenExistingHeap(*this, hFileMapping, nullptr, Desc, riid, ppvHeap);
}
