/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device lifetime, the driver device behind it, and the simple ID3D12Device methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "pipeline.hpp"

/* Direct3D 12 keeps one device per adapter; a second D3D12CreateDevice hands back the first */
struct RxDeviceSlot
{
    LUID Luid;
    RxDevice12* Device;
};

static SRWLOCK RxDeviceListLock = SRWLOCK_INIT;
static RxDeviceSlot RxDevices[16];

static thread_local HRESULT RxDriverError12 = S_OK;

static
BOOLEAN
RxSameLuid(
    _In_ const LUID& First,
    _In_ const LUID& Second)
{
    return First.LowPart == Second.LowPart && First.HighPart == Second.HighPart;
}

/* A reference only while the count has not already reached zero */
static
BOOLEAN
RxTryAddRef(
    _Inout_ volatile LONG* Count)
{
    LONG Current = *Count;

    while (Current != 0)
    {
        LONG Seen = InterlockedCompareExchange(Count, Current + 1, Current);

        if (Seen == Current)
            return TRUE;
        Current = Seen;
    }

    return FALSE;
}

HRESULT
RxDevice12::CheckLevel(
    _In_ const LUID& AdapterLuid,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel)
{
    RxAdapter12 Adapter;
    HRESULT hr;

    hr = Adapter.Open(AdapterLuid);
    if (FAILED(hr))
        return hr;

    return Adapter.GetMaxFeatureLevel() >= MinimumFeatureLevel ? S_OK : E_INVALIDARG;
}

HRESULT
RxDevice12::Get(
    _In_ const LUID& AdapterLuid,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ UINT Sharing,
    _Outptr_ RxDevice12** Device)
{
    RxAdapter12* Adapter;
    RxDevice12* Created;
    BOOLEAN Stored = FALSE;
    UINT Index;
    HRESULT hr;

    *Device = nullptr;

    AcquireSRWLockExclusive(&RxDeviceListLock);

    for (Index = 0; Index < _countof(RxDevices); Index++)
    {
        RxDevice12* Existing = RxDevices[Index].Device;

        if (Existing == nullptr || !RxSameLuid(RxDevices[Index].Luid, AdapterLuid))
            continue;

        /* A live singleton is either handed back or left in place beside the new device */
        if (Existing->RefCount != 0)
            Stored = TRUE;
        if ((Sharing & RxDeviceReuseSingleton) == 0 || !RxTryAddRef(&Existing->RefCount))
            continue;

        ReleaseSRWLockExclusive(&RxDeviceListLock);
        if (Existing->Adapter->GetMaxFeatureLevel() < MinimumFeatureLevel)
        {
            Existing->Release();
            return E_INVALIDARG;
        }

        *Device = Existing;
        return S_OK;
    }

    Adapter = new (std::nothrow) RxAdapter12();
    Created = new (std::nothrow) RxDevice12();
    if (Adapter == nullptr || Created == nullptr)
    {
        ReleaseSRWLockExclusive(&RxDeviceListLock);
        delete Adapter;
        delete Created;
        return E_OUTOFMEMORY;
    }

    hr = Adapter->Open(AdapterLuid);
    if (SUCCEEDED(hr) && Adapter->GetMaxFeatureLevel() < MinimumFeatureLevel)
        hr = E_INVALIDARG;
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&RxDeviceListLock);
        delete Adapter;
        delete Created;
        return hr;
    }

    hr = Created->Initialize(Adapter);
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&RxDeviceListLock);
        RX_TRACE(L"device initialization failed 0x%08lX", hr);
        delete Created;
        return hr;
    }

    for (Index = 0; Index < _countof(RxDevices) && !Stored && (Sharing & RxDeviceStoreSingleton) != 0; Index++)
    {
        if (RxDevices[Index].Device == nullptr)
        {
            RxDevices[Index].Luid = AdapterLuid;
            RxDevices[Index].Device = Created;
            break;
        }
    }

    ReleaseSRWLockExclusive(&RxDeviceListLock);
    *Device = Created;
    return S_OK;
}

HRESULT
RxDevice12::Initialize(
    _In_ RxAdapter12* OpenedAdapter)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    const D3D12DDI_ADAPTERFUNCS_0109& Functions = OpenedAdapter->GetFunctions();
    D3DKMT_CREATEDEVICE CreateKm = {};
    D3D12DDIARG_CALCPRIVATEDEVICESIZE Calc = {};
    D3D12DDIARG_CREATEDEVICE_0109 Create = {};
    D3DKMT_CREATEPAGINGQUEUE Paging = {};
    SIZE_T Size;
    NTSTATUS Status;
    HRESULT hr;

    Adapter = OpenedAdapter;
    DdiBuild = (UINT)(Adapter->GetVersion() >> 16) & 0xFFFF;
    Level = Adapter->GetMaxFeatureLevel();

    CreateKm.hAdapter = Adapter->GetKmtHandle();
    CreateKm.Flags.RequestVSync = TRUE;
    Status = Kmt->CreateDevice(&CreateKm);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateDevice failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }
    KmDevice = CreateKm.hDevice;

    Calc.Interface = (UINT)(Adapter->GetVersion() >> 32);
    Calc.Version = (UINT)Adapter->GetVersion();
    Size = Functions.pfnCalcPrivateDeviceSize(Adapter->GetDriverHandle(), &Calc);
    DriverDevice.pDrvPrivate = DriverMemory.Allocate(Size);
    if (DriverDevice.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    Create.hRTDevice.handle = this;
    Create.Interface = Calc.Interface;
    Create.Version = Calc.Version;
    Create.pKTCallbacks = RxGetKernelCallbacks12();
    Create.hDrvDevice = DriverDevice;
    Create.p12UMCallbacks_0116 = RxGetCoreLayerCallbacks12();

    hr = Functions.pfnCreateDevice(Adapter->GetDriverHandle(), &Create);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateDevice failed 0x%08lX", hr);
        return hr == DXGI_ERROR_DEVICE_REMOVED ? DXGI_ERROR_DRIVER_INTERNAL_ERROR : hr;
    }
    IsDriverDeviceCreated = TRUE;

    hr = QueryDeviceCaps();
    if (FAILED(hr))
        return hr;

    hr = FillTables();
    if (FAILED(hr))
        return hr;

    hr = QueryLateCaps();
    if (FAILED(hr))
        return hr;

    /* The paging queue every residency change of this device goes through */
    Paging.hDevice = KmDevice;
    Paging.Priority = D3DDDI_PAGINGQUEUE_PRIORITY_NORMAL;
    Paging.PhysicalAdapterIndex = 0;
    Status = Kmt->CreatePagingQueue(&Paging);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreatePagingQueue failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }
    PagingQueue.Handle = Paging.hPagingQueue;
    PagingQueue.SyncObject = Paging.hSyncObject;
    PagingQueue.FenceCpuVa = Paging.FenceValueCPUVirtualAddress;

    hr = CreateDefaults();
    if (FAILED(hr))
        return hr;

    hr = QueryVirtualAddressBits();
    if (FAILED(hr))
        return hr;

    hr = QueryValidationCaps();
    if (FAILED(hr))
        return hr;

    hr = CreateInternalObjects();
    if (FAILED(hr))
        return hr;

    RX_TRACE(L"Direct3D 12 device on DDI 0x%016llX, level 0x%X", Adapter->GetVersion(), Level);
    return S_OK;
}

RxDevice12::~RxDevice12()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DDDI_DESTROYPAGINGQUEUE DestroyPaging;
    D3DKMT_DESTROYDEVICE DestroyKm;

    if (CommandPool.pDrvPrivate != nullptr)
        CoreFunctions.pfnDestroyCommandPool(DriverDevice, CommandPool);

    if (DefaultGraphics != nullptr)
        DefaultGraphics->Release();
    if (DefaultCompute != nullptr)
        DefaultCompute->Release();
    if (DefaultRoot != nullptr)
        DefaultRoot->Release();

    if (IsDriverDeviceCreated)
        Adapter->GetFunctions().pfnDestroyDevice(DriverDevice);

    for (RxSessionCaps12& Caps : Sessions)
        delete[] Caps.Types;

    if (PagingQueue.Handle != 0)
    {
        DestroyPaging.hPagingQueue = PagingQueue.Handle;
        Kmt->DestroyPagingQueue(&DestroyPaging);
    }

    if (KmDevice != 0)
    {
        DestroyKm.hDevice = KmDevice;
        Kmt->DestroyDevice(&DestroyKm);
    }

    delete Adapter;
}

HRESULT
RxDevice12::TakeDriverError()
{
    HRESULT Error = RxDriverError12;

    RxDriverError12 = S_OK;
    return Error;
}

VOID
RxDevice12::NoteDriverError(
    _In_ HRESULT Error)
{
    RX_TRACE(L"driver raised 0x%08lX", Error);
    RxDriverError12 = Error;
}

VOID
RxDevice12::MarkRemoved(
    _In_ HRESULT Reason)
{
    InterlockedCompareExchange(&RemovedReason, Reason, S_OK);
}

VOID
RxDevice12::RemoveOnBadView(VOID)
{
    RX_TRACE(L"invalid view description, removing the device");
    MarkRemoved(DXGI_ERROR_INVALID_CALL);
}

HRESULT
RxDevice12::MakePageableResident(
    _In_ VOID* DriverHandle,
    _In_ D3D12DDI_HANDLETYPE Type)
{
    D3D12DDI_HANDLE_AND_TYPE Object = { DriverHandle, Type };
    D3D12DDI_HRTPAGINGQUEUE Queue = { &PagingQueue };
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    D3D12DDIARG_MAKERESIDENT_0001 Args = {};
    UINT64 FenceValues[32] = {};
    HRESULT hr;

    if (CoreFunctions.pfnMakeResident == nullptr)
        return S_OK;

    Args.NumAdapters = 1;
    Args.pRTPagingQueue = &Queue;
    /* Pipeline states have no allocations of their own, so the list stays empty */
    Args.NumObjects = DriverHandle != nullptr ? 1 : 0;
    Args.pObjects = &Object;
    Args.Flags.CantTrimFurther = TRUE;
    Args.pPagingFenceValue = FenceValues;

    hr = CoreFunctions.pfnMakeResident(DriverDevice, &Args);
    if (hr != E_PENDING)
        return hr;

    /* Paging is still in flight; the object is usable once the paging fence passes */
    if ((Args.WaitMask & 1) == 0)
        return S_OK;

    Wait.hDevice = KmDevice;
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &PagingQueue.SyncObject;
    Wait.FenceValueArray = &FenceValues[0];
    return RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait));
}

D3D12DDI_COMMAND_QUEUE_FLAGS
RxQueueFlagsOf(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_DIRECT:
        case D3D12_COMMAND_LIST_TYPE_BUNDLE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_3D | D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
        case D3D12_COMMAND_LIST_TYPE_COMPUTE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE | D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
        case D3D12_COMMAND_LIST_TYPE_COPY:
            return D3D12DDI_COMMAND_QUEUE_FLAG_COPY;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE;
        default:
            return D3D12DDI_COMMAND_QUEUE_FLAG_NONE;
    }
}

/* IUnknown and ID3D12Object */

HRESULT
STDMETHODCALLTYPE
RxDevice12::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    static const IID* const Faces[] =
    {
        &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12Device), &__uuidof(ID3D12Device1),
        &__uuidof(ID3D12Device2), &__uuidof(ID3D12Device3), &__uuidof(ID3D12Device4), &__uuidof(ID3D12Device5),
        &__uuidof(ID3D12Device6), &__uuidof(ID3D12Device7), &__uuidof(ID3D12Device8), &__uuidof(ID3D12Device9),
        &__uuidof(ID3D12Device10), &__uuidof(ID3D12Device11), &__uuidof(ID3D12Device12), &__uuidof(ID3D12Device13),
        &__uuidof(ID3D12Device14),
    };

    if (ppvObject == nullptr)
        return E_POINTER;

    for (const IID* Face : Faces)
    {
        if (riid == *Face)
        {
            *ppvObject = static_cast<ID3D12Device14*>(this);
            AddRef();
            return S_OK;
        }
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxDevice12::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDevice12::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);
    UINT Index;

    if (Count != 0)
        return Count;

    AcquireSRWLockExclusive(&RxDeviceListLock);
    for (Index = 0; Index < _countof(RxDevices); Index++)
    {
        if (RxDevices[Index].Device == this)
            RxDevices[Index].Device = nullptr;
    }
    ReleaseSRWLockExclusive(&RxDeviceListLock);

    delete this;
    return 0;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::GetPrivateData(
    _In_ REFGUID guid,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_opt_(*pDataSize) void* pData)
{
    return PrivateData.Get(guid, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetPrivateData(
    _In_ REFGUID guid,
    _In_ UINT DataSize,
    _In_reads_bytes_opt_(DataSize) const void* pData)
{
    return PrivateData.Set(guid, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetPrivateDataInterface(
    _In_ REFGUID guid,
    _In_opt_ const IUnknown* pData)
{
    return PrivateData.Set(guid, 0, nullptr, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetName(
    _In_z_ LPCWSTR Name)
{
    if (Name == nullptr)
        return PrivateData.Set(WKPDID_D3DDebugObjectNameW, 0, nullptr, nullptr);

    return PrivateData.Set(WKPDID_D3DDebugObjectNameW, (UINT)((wcslen(Name) + 1) * sizeof(WCHAR)), Name, nullptr);
}

/* Simple device queries */

UINT
STDMETHODCALLTYPE
RxDevice12::GetNodeCount(VOID)
{
    return 1;
}

LUID
STDMETHODCALLTYPE
RxDevice12::GetAdapterLuid(VOID)
{
    return Adapter->GetLuid();
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::GetDeviceRemovedReason(VOID)
{
    return RemovedReason;
}

UINT
STDMETHODCALLTYPE
RxDevice12::GetDescriptorHandleIncrementSize(
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapType)
{
    /* Render target views are runtime slots wrapping the driver's descriptor */
    if (DescriptorHeapType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
        return sizeof(RxRtvEntry);

    if ((UINT)DescriptorHeapType >= D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES)
        return 0;

    return CoreFunctions.pfnGetDescriptorSizeInBytes(DriverDevice, (D3D12DDI_DESCRIPTOR_HEAP_TYPE)DescriptorHeapType);
}

/* Object creation */

/* Global realtime queues need the right to raise base priority, held and enabled */
static
BOOLEAN
RxCanRaisePriority(VOID)
{
    HANDLE Token = NULL;
    PRIVILEGE_SET Wanted = {};
    BOOL Held = FALSE;
    BOOLEAN Result = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return FALSE;

    Wanted.PrivilegeCount = 1;
    Wanted.Control = PRIVILEGE_SET_ALL_NECESSARY;
    Wanted.Privilege[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueA(NULL, "SeIncreaseBasePriorityPrivilege", &Wanted.Privilege[0].Luid) &&
        PrivilegeCheck(Token, &Wanted, &Held))
    {
        Result = Held != FALSE;
    }

    CloseHandle(Token);
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandQueue(
    _In_ const D3D12_COMMAND_QUEUE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppCommandQueue)
{
    return CreateCommandQueue1(pDesc, GUID_NULL, riid, ppCommandQueue);
}

/* The creator ID keeps queues of different components out of each other's scheduling groups */
HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandQueue1(
    _In_ const D3D12_COMMAND_QUEUE_DESC* pDesc,
    REFIID CreatorID,
    REFIID riid,
    _COM_Outptr_ void** ppCommandQueue)
{
    RxCommandQueue* Queue;
    HRESULT hr;

    if (ppCommandQueue != nullptr)
        *ppCommandQueue = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* A valid request without an output is a caller error, not a check */
    hr = RxCommandQueue::Validate(*pDesc);
    if (FAILED(hr))
        return hr;
    if (!TakesListType(pDesc->Type))
        return E_INVALIDARG;

    if (pDesc->Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
    {
        if (!RxCanRaisePriority())
            return DXGI_ERROR_ACCESS_DENIED;
        if ((RxQueueFlagsOf(pDesc->Type) & ~RealtimeQueueFlags) != 0)
            return DXGI_ERROR_UNSUPPORTED;
    }

    if (ppCommandQueue == nullptr)
        return E_UNEXPECTED;

    hr = RxCommandQueue::Create(*this, *pDesc, CreatorID, &Queue);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Queue, riid, ppCommandQueue);
}

BOOLEAN
RxDevice12::TakesListType(
    _In_ D3D12_COMMAND_LIST_TYPE Type) const
{
    if (RxQueueFlagsOf(Type) == D3D12DDI_COMMAND_QUEUE_FLAG_NONE)
        return FALSE;
    if (Type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE || Type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
        Type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE)
    {
        return VideoVersion != 0;
    }
    return TRUE;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandAllocator(
    _In_ D3D12_COMMAND_LIST_TYPE type,
    REFIID riid,
    _COM_Outptr_ void** ppCommandAllocator)
{
    RxCommandAllocator* Allocator;
    HRESULT hr;

    if (ppCommandAllocator != nullptr)
        *ppCommandAllocator = nullptr;
    if (!TakesListType(type))
        return E_INVALIDARG;
    if (ppCommandAllocator == nullptr)
        return E_UNEXPECTED;

    hr = RxCommandAllocator::Create(*this, type, &Allocator);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Allocator, riid, ppCommandAllocator);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandList(
    _In_ UINT nodeMask,
    _In_ D3D12_COMMAND_LIST_TYPE type,
    _In_ ID3D12CommandAllocator* pCommandAllocator,
    _In_opt_ ID3D12PipelineState* pInitialState,
    REFIID riid,
    _COM_Outptr_ void** ppCommandList)
{
    RxCommandAllocator* Allocator = RxUnwrap12<RxCommandAllocator>(pCommandAllocator);
    RxCommandList* List;
    HRESULT hr;

    if (ppCommandList != nullptr)
        *ppCommandList = nullptr;
    if (Allocator == nullptr || nodeMask > 1 || !TakesListType(type) || Allocator->GetType() != type)
    {
        return E_INVALIDARG;
    }

    /* Video lists are their own objects, which no graphics list interface reaches */
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE || type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
        type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE)
    {
        return E_NOINTERFACE;
    }
    if (ppCommandList == nullptr)
        return E_UNEXPECTED;

    hr = RxCommandList::Create(*this, type, Allocator, pInitialState, &List);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(List, riid, ppCommandList);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandList1(
    _In_ UINT nodeMask,
    _In_ D3D12_COMMAND_LIST_TYPE type,
    _In_ D3D12_COMMAND_LIST_FLAGS flags,
    REFIID riid,
    _COM_Outptr_ void** ppCommandList)
{
    RxCommandList* List;
    HRESULT hr;

    if (ppCommandList != nullptr)
        *ppCommandList = nullptr;
    if (nodeMask > 1 || !TakesListType(type))
        return E_INVALIDARG;
    if (type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE || type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
        type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE)
    {
        return E_NOINTERFACE;
    }
    if (ppCommandList == nullptr)
        return E_UNEXPECTED;

    hr = RxCommandList::Create(*this, type, nullptr, nullptr, &List);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(List, riid, ppCommandList);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateFence(
    UINT64 InitialValue,
    D3D12_FENCE_FLAGS Flags,
    REFIID riid,
    _COM_Outptr_ void** ppFence)
{
    RxFence* Fence;
    HRESULT hr;

    if (ppFence != nullptr)
        *ppFence = nullptr;

    hr = RxFence::Create(*this, InitialValue, Flags, &Fence);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Fence, riid, ppFence);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateDescriptorHeap(
    _In_ const D3D12_DESCRIPTOR_HEAP_DESC* pDescriptorHeapDesc,
    REFIID riid,
    _COM_Outptr_ void** ppvHeap)
{
    RxDescriptorHeap* Heap;
    HRESULT hr;

    /* Without an output nothing is checked */
    if (ppvHeap == nullptr)
        return S_FALSE;

    *ppvHeap = nullptr;
    if (pDescriptorHeapDesc == nullptr)
        return E_INVALIDARG;

    hr = RxDescriptorHeap::Create(*this, *pDescriptorHeapDesc, &Heap);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Heap, riid, ppvHeap);
}
