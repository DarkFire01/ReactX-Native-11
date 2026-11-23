/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12Heap: application heaps backed by one driver buffer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Heap flags that only steer creation and are not kept in the heap's desc */
constexpr UINT RxCreationOnlyHeapFlags = D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT | D3D12_HEAP_FLAG_CREATE_NOT_ZEROED;

HRESULT
RxHeap::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_DESC& Desc,
    _Outptr_ RxHeap** Heap)
{
    RxHeap* Created;
    HRESULT hr;

    *Heap = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxHeap(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxHeap::Initialize(
    _In_ const D3D12_HEAP_DESC& HeapDesc)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12_RESOURCE_DESC1 Backing = {};
    D3D12DDIARG_CREATERESOURCE_0111 DdiResource;
    D3D12DDIARG_CREATEHEAP_0001 DdiHeap;
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Existing = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES Sizes;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    D3D12DDI_HRTRESOURCE RuntimeHandle;
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 NoSession = {};
    HRESULT hr;

    /* The driver sees a heap as one buffer as large as the heap */
    Backing.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Backing.Alignment = HeapDesc.Alignment != 0 ? HeapDesc.Alignment : D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    Backing.Width = HeapDesc.SizeInBytes;
    Backing.Height = 1;
    Backing.DepthOrArraySize = 1;
    Backing.MipLevels = 1;
    Backing.SampleDesc.Count = 1;
    Backing.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Desc = HeapDesc;
    Desc.Flags = (D3D12_HEAP_FLAGS)(HeapDesc.Flags & ~RxCreationOnlyHeapFlags);
    if (Desc.Properties.CreationNodeMask == 0)
        Desc.Properties.CreationNodeMask = 1;
    if (Desc.Properties.VisibleNodeMask == 0)
        Desc.Properties.VisibleNodeMask = 1;

    RxToDdiResource(Backing, DdiResource);
    Info = RxAllocationInfo(Device, Backing, DdiResource, FALSE, Desc.Flags, &DdiResource.Layout);
    if (Info.SizeInBytes >= 0xFFFFFFFFFFC00000ull)
        return E_OUTOFMEMORY;
    if (Desc.Alignment == 0)
        Desc.Alignment = Info.Alignment;

    RxToDdiHeap(Device, Desc.Properties, Desc.Flags, Info.SizeInBytes, Info.Alignment, FALSE, DdiHeap);

    /* Sized twice as for committed resources */
    Sizes = Ddi.pfnCalcPrivateHeapAndResourceSizes(Device.Handle(), &DdiHeap, &DdiResource, NoSession);
    Sizes = Ddi.pfnCalcPrivateHeapAndResourceSizes(Device.Handle(), &DdiHeap, &DdiResource, NoSession);
    DriverHeap.pDrvPrivate = HeapMemory.Allocate(Sizes.Heap);
    DriverResource.pDrvPrivate = ResourceMemory.Allocate(Sizes.Resource);
    if (DriverHeap.pDrvPrivate == nullptr || DriverResource.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    RuntimeHandle.handle = &Runtime;
    hr = Ddi.pfnCreateHeapAndResource(Device.Handle(), &DdiHeap, DriverHeap, RuntimeHandle, &DdiResource, nullptr,
                                      NoSession, DriverResource);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateHeapAndResource failed for a heap 0x%08lX", hr);
        return hr;
    }
    IsDriverHeapCreated = TRUE;

    Ddi.pfnCheckExistingResourceAllocationInfo(Device.Handle(), DriverResource, &Existing);
    GpuAddress = Ddi.pfnCheckResourceVirtualAddress(Device.Handle(), DriverResource);

    if ((HeapDesc.Flags & D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT) != 0)
        return S_OK;
    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_HEAP);
}

RxHeap::~RxHeap()
{
    if (MapCount != 0)
        Device.Ddi().pfnUnmapHeap(Device.Handle(), DriverHeap);

    if (IsDriverHeapCreated)
        Device.Ddi().pfnDestroyHeapAndResource(Device.Handle(), DriverHeap, DriverResource);
}

HRESULT
STDMETHODCALLTYPE
RxHeap::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Heap) || riid == __uuidof(ID3D12Heap1) ||
        riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12Heap1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

D3D12_HEAP_DESC
STDMETHODCALLTYPE
RxHeap::GetDesc(VOID)
{
    return Desc;
}

HRESULT
STDMETHODCALLTYPE
RxHeap::GetProtectedResourceSession(
    REFIID riid,
    _COM_Outptr_opt_ void** ppProtectedSession)
{
    if (ppProtectedSession != nullptr)
        *ppProtectedSession = nullptr;
    return DXGI_ERROR_NOT_FOUND;
}

HRESULT
RxHeap::MapMemory(
    _Outptr_ BYTE** Base)
{
    HRESULT hr = S_OK;

    AcquireSRWLockExclusive(&MapLock);
    if (MapCount == 0)
    {
        hr = Device.Ddi().pfnMapHeap(Device.Handle(), DriverHeap, reinterpret_cast<void**>(&MappedBase));
        if (FAILED(hr))
            MappedBase = nullptr;
    }
    if (SUCCEEDED(hr))
        MapCount += 1;
    *Base = MappedBase;
    ReleaseSRWLockExclusive(&MapLock);
    return hr;
}

VOID
RxHeap::UnmapMemory()
{
    AcquireSRWLockExclusive(&MapLock);
    if (MapCount != 0)
    {
        MapCount -= 1;
        if (MapCount == 0)
        {
            Device.Ddi().pfnUnmapHeap(Device.Handle(), DriverHeap);
            MappedBase = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&MapLock);
}

/* Device entry points */

static
HRESULT
RxCreateHeapCommon(
    _In_ RxDevice12& Device,
    _In_opt_ const D3D12_HEAP_DESC* pDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvHeap)
{
    RxHeap* Heap;
    HRESULT hr;

    if (ppvHeap != nullptr)
        *ppvHeap = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* The heap is made on one node that exists; the single node is node 0 */
    if (pDesc->Properties.CreationNodeMask > 1)
        return E_INVALIDARG;

    hr = RxValidateHeapDesc(Device, *pDesc, 0);
    if (FAILED(hr))
        return hr;
    if (ppvHeap == nullptr)
        return S_FALSE;

    /* Visibility on nodes that do not exist only fails once the heap is really made */
    if (pDesc->Properties.VisibleNodeMask > 1)
        return E_INVALIDARG;

    /* Sharing and protected sessions need kernel support this runtime does not have yet */
    if (pProtectedSession != nullptr || (pDesc->Flags & D3D12_HEAP_FLAG_SHARED) != 0)
    {
        RX_TRACE(L"heap flags 0x%X or protected session not supported", pDesc->Flags);
        return E_NOTIMPL;
    }

    hr = RxHeap::Create(Device, *pDesc, &Heap);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Heap, riid, ppvHeap);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateHeap(
    _In_ const D3D12_HEAP_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvHeap)
{
    return RxCreateHeapCommon(*this, pDesc, nullptr, riid, ppvHeap);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateHeap1(
    _In_ const D3D12_HEAP_DESC* pDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvHeap)
{
    return RxCreateHeapCommon(*this, pDesc, pProtectedSession, riid, ppvHeap);
}
