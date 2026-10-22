/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     WDDM 2 kernel callbacks every ReactX runtime hands to its user mode driver
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"
#include "kmcallbacks.hpp"

#include <new>

/*
 * A WDDM 2.x kernel driver puts its UMD on these even when the runtime negotiated
 * a WDDM 1.x interface, so they are wired up whenever gdi32 exports the thunks.
 */

#define RX_REQUIRE_THUNK(Name) \
    do \
    { \
        if (RxGetKmtThunks()->Name == nullptr) \
            return E_NOTIMPL; \
    } while (0)

_Success_(return != nullptr)
D3DKMT_HANDLE*
RxTranslateResources(
    _In_ RxKernelDevice& Device,
    _In_reads_(Count) const HANDLE* Resources,
    _In_ UINT Count)
{
    D3DKMT_HANDLE* Handles;
    UINT Index;

    Handles = new (std::nothrow) D3DKMT_HANDLE[Count];
    if (Handles == nullptr)
        return nullptr;

    for (Index = 0; Index < Count; Index++)
        Handles[Index] = Device.KernelResourceOf(Resources[Index]);

    return Handles;
}

static
D3DKMT_HANDLE
RxKmContext(
    _In_opt_ HANDLE Context)
{
    return (D3DKMT_HANDLE)(ULONG_PTR)Context;
}

HRESULT
APIENTRY
RxCreatePagingQueueCb(
    _In_ HANDLE hDevice,
    _Out_ D3DDDICB_CREATEPAGINGQUEUE* Args)
{
    D3DKMT_CREATEPAGINGQUEUE Create = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(CreatePagingQueue);

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.Priority = Args->Priority;
    Create.PhysicalAdapterIndex = Args->PhysicalAdapterIndex;

    Status = RxGetKmtThunks()->CreatePagingQueue(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreatePagingQueue failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Args->hPagingQueue = Create.hPagingQueue;
    Args->hSyncObject = Create.hSyncObject;
    Args->FenceValueCPUVirtualAddress = Create.FenceValueCPUVirtualAddress;
    return S_OK;
}

HRESULT
APIENTRY
RxDestroyPagingQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDI_DESTROYPAGINGQUEUE* Args)
{
    D3DDDI_DESTROYPAGINGQUEUE Destroy = *Args;

    RX_REQUIRE_THUNK(DestroyPagingQueue);
    return RxHresultFromStatus(RxGetKmtThunks()->DestroyPagingQueue(&Destroy));
}

HRESULT
APIENTRY
RxMakeResidentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_MAKERESIDENT* Args)
{
    NTSTATUS Status;

    RX_REQUIRE_THUNK(MakeResident);

    /* E_PENDING tells the UMD to wait on the paging fence written into the arguments */
    Status = RxGetKmtThunks()->MakeResident(Args);
    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxEvictCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_EVICT* Args)
{
    D3DKMT_EVICT Evict = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(Evict);

    Evict.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Evict.NumAllocations = Args->NumAllocations;
    Evict.AllocationList = Args->AllocationList;
    Evict.Flags = Args->Flags;

    Status = RxGetKmtThunks()->Evict(&Evict);
    Args->NumBytesToTrim = Evict.NumBytesToTrim;
    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxReserveGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_RESERVEGPUVIRTUALADDRESS* Args)
{
    RX_REQUIRE_THUNK(ReserveGpuVirtualAddress);

    /* The UMD leaves the adapter for the runtime to fill in */
    Args->hAdapter = RxKernelDeviceFromHandle(hDevice).KernelAdapter();
    return RxHresultFromStatus(RxGetKmtThunks()->ReserveGpuVirtualAddress(Args));
}

HRESULT
APIENTRY
RxMapGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_MAPGPUVIRTUALADDRESS* Args)
{
    HRESULT hr;

    RX_REQUIRE_THUNK(MapGpuVirtualAddress);

    /* A lost device shows up on the next submit, so the mapping itself reports success */
    hr = RxHresultFromStatus(RxGetKmtThunks()->MapGpuVirtualAddress(Args));
    return (hr == D3DDDIERR_DEVICEREMOVED) ? S_OK : hr;
}

HRESULT
APIENTRY
RxFreeGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_FREEGPUVIRTUALADDRESS* Args)
{
    D3DKMT_FREEGPUVIRTUALADDRESS Free = {};

    RX_REQUIRE_THUNK(FreeGpuVirtualAddress);

    Free.hAdapter = RxKernelDeviceFromHandle(hDevice).KernelAdapter();
    Free.BaseAddress = Args->BaseAddress;
    Free.Size = Args->Size;

    return RxHresultFromStatus(RxGetKmtThunks()->FreeGpuVirtualAddress(&Free));
}

HRESULT
APIENTRY
RxUpdateGpuVirtualAddressCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UPDATEGPUVIRTUALADDRESS* Args)
{
    D3DKMT_UPDATEGPUVIRTUALADDRESS Update = {};

    RX_REQUIRE_THUNK(UpdateGpuVirtualAddress);

    Update.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Update.hContext = RxKmContext(Args->hContext);
    Update.hFenceObject = Args->hFenceObject;
    Update.NumOperations = Args->NumOperations;
    Update.Operations = Args->Operations;
    Update.FenceValue = Args->FenceValue;
    Update.Flags.Value = Args->Flags.Value;

    return RxHresultFromStatus(RxGetKmtThunks()->UpdateGpuVirtualAddress(&Update));
}

HRESULT
APIENTRY
RxCreateContextVirtualCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args)
{
    D3DKMT_CREATECONTEXTVIRTUAL Create = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(CreateContextVirtual);

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags = Args->Flags;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = D3DKMT_CLIENTHINT_DX11;

    Status = RxGetKmtThunks()->CreateContextVirtual(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContextVirtual(node %u) failed 0x%08lX", Args->NodeOrdinal, Status);
        return RxHresultFromStatus(Status);
    }

    RX_TRACE(L"virtual context 0x%X on node %u", Create.hContext, Args->NodeOrdinal);
    RxKernelDeviceFromHandle(hDevice).NoteKernelContext(Create.hContext);
    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    return S_OK;
}

HRESULT
APIENTRY
RxSubmitCommandCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITCOMMAND* Args)
{
    D3DKMT_SUBMITCOMMAND Submit = {};
    UINT Index;

    RX_REQUIRE_THUNK(SubmitCommand);

    Submit.Commands = Args->Commands;
    Submit.CommandLength = Args->CommandLength;
    Submit.Flags.NullRendering = Args->Flags.NullRendering;
    Submit.pPrivateDriverData = Args->pPrivateDriverData;
    Submit.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    /* There is no separate context field: the first broadcast target is the context */
    Submit.BroadcastContextCount = Args->BroadcastContextCount;
    for (Index = 0; Index < Args->BroadcastContextCount && Index < D3DDDI_MAX_BROADCAST_CONTEXT; Index++)
        Submit.BroadcastContext[Index] = RxKmContext(Args->BroadcastContext[Index]);

    Submit.NumPrimaries = Args->NumPrimaries;
    memcpy(Submit.WrittenPrimaries, Args->WrittenPrimaries, sizeof(Submit.WrittenPrimaries));
    Submit.NumHistoryBuffers = Args->NumHistoryBuffers;
    Submit.HistoryBufferArray = Args->HistoryBufferArray;

    return RxHresultFromStatus(RxGetKmtThunks()->SubmitCommand(&Submit));
}

HRESULT
APIENTRY
RxLock2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_LOCK2* Args)
{
    D3DKMT_LOCK2 Lock = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(Lock2);

    Lock.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Lock.hAllocation = Args->hAllocation;
    Lock.Flags = Args->Flags;

    Status = RxGetKmtThunks()->Lock2(&Lock);
    if (NT_SUCCESS(Status))
        Args->pData = Lock.pData;

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxUnlock2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UNLOCK2* Args)
{
    D3DKMT_UNLOCK2 Unlock = {};

    RX_REQUIRE_THUNK(Unlock2);

    Unlock.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Unlock.hAllocation = Args->hAllocation;

    return RxHresultFromStatus(RxGetKmtThunks()->Unlock2(&Unlock));
}

HRESULT
APIENTRY
RxInvalidateCacheCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_INVALIDATECACHE* Args)
{
    D3DKMT_INVALIDATECACHE Invalidate = {};

    RX_REQUIRE_THUNK(InvalidateCache);

    Invalidate.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Invalidate.hAllocation = Args->hAllocation;
    Invalidate.Offset = Args->Offset;
    Invalidate.Length = Args->Length;

    return RxHresultFromStatus(RxGetKmtThunks()->InvalidateCache(&Invalidate));
}

HRESULT
APIENTRY
RxDeallocate2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE2* Args)
{
    RxKernelDevice& Device = RxKernelDeviceFromHandle(hDevice);
    D3DKMT_DESTROYALLOCATION2 Destroy = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(DestroyAllocation2);

    Destroy.hDevice = Device.KernelDevice();
    Destroy.Flags = Args->Flags;
    if (Args->hResource != NULL)
    {
        Destroy.hResource = Device.KernelResourceOf(Args->hResource);
        if (Destroy.hResource == 0)
            return S_OK;
    }
    else
    {
        Destroy.phAllocationList = Args->HandleList;
        Destroy.AllocationCount = Args->NumAllocations;
    }

    Status = RxGetKmtThunks()->DestroyAllocation2(&Destroy);
    if (NT_SUCCESS(Status) && Args->hResource != NULL)
        Device.RetireKernelResource(Args->hResource);

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxReclaimAllocations2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RECLAIMALLOCATIONS2* Args)
{
    D3DKMT_RECLAIMALLOCATIONS2 Reclaim = {};
    D3DKMT_HANDLE* Resources = nullptr;
    NTSTATUS Status;

    RX_REQUIRE_THUNK(ReclaimAllocations2);

    if (Args->pResources != nullptr)
    {
        Resources = RxTranslateResources(RxKernelDeviceFromHandle(hDevice), Args->pResources, Args->NumAllocations);
        if (Resources == nullptr)
            return E_OUTOFMEMORY;
    }

    Reclaim.hPagingQueue = Args->PagingQueue;
    Reclaim.NumAllocations = Args->NumAllocations;
    Reclaim.pResources = Resources;
    Reclaim.HandleList = Args->HandleList;
    Reclaim.pDiscarded = Args->pDiscarded;

    Status = RxGetKmtThunks()->ReclaimAllocations2(&Reclaim);
    Args->PagingFenceValue = Reclaim.PagingFenceValue;
    delete[] Resources;

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxReclaimAllocations3Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RECLAIMALLOCATIONS3* Args)
{
    D3DKMT_RECLAIMALLOCATIONS2 Reclaim = {};
    D3DKMT_HANDLE* Resources = nullptr;
    NTSTATUS Status;

    RX_REQUIRE_THUNK(ReclaimAllocations2);

    if (Args->pResources != nullptr)
    {
        Resources = RxTranslateResources(RxKernelDeviceFromHandle(hDevice), Args->pResources, Args->NumAllocations);
        if (Resources == nullptr)
            return E_OUTOFMEMORY;
    }

    Reclaim.hPagingQueue = Args->PagingQueue;
    Reclaim.NumAllocations = Args->NumAllocations;
    Reclaim.pResources = Resources;
    Reclaim.HandleList = Args->HandleList;
    Reclaim.pResults = Args->pResults;

    Status = RxGetKmtThunks()->ReclaimAllocations2(&Reclaim);
    Args->PagingFenceValue = Reclaim.PagingFenceValue;
    delete[] Resources;

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxUpdateAllocationPropertyCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_UPDATEALLOCPROPERTY* Args)
{
    NTSTATUS Status;

    RX_REQUIRE_THUNK(UpdateAllocationProperty);

    Status = RxGetKmtThunks()->UpdateAllocationProperty(Args);
    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxGetResourcePresentPrivateDriverDataCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDI_GETRESOURCEPRESENTPRIVATEDRIVERDATA* Args)
{
    RX_REQUIRE_THUNK(GetResourcePresentPrivateDriverData);
    return RxHresultFromStatus(RxGetKmtThunks()->GetResourcePresentPrivateDriverData(Args));
}

HRESULT
APIENTRY
RxWaitForSynchronizationObjectFromCpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMCPU* Args)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    HRESULT hr;

    RX_REQUIRE_THUNK(WaitForSynchronizationObjectFromCpu);

    Wait.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Wait.ObjectCount = Args->ObjectCount;
    Wait.ObjectHandleArray = Args->ObjectHandleArray;
    Wait.FenceValueArray = Args->FenceValueArray;
    Wait.hAsyncEvent = Args->hAsyncEvent;
    Wait.Flags = Args->Flags;

    hr = RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait));
    return (hr == D3DDDIERR_DEVICEREMOVED) ? S_OK : hr;
}

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromCpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMCPU* Args)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal = {};

    RX_REQUIRE_THUNK(SignalSynchronizationObjectFromCpu);

    Signal.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Signal.ObjectCount = Args->ObjectCount;
    Signal.ObjectHandleArray = Args->ObjectHandleArray;
    Signal.FenceValueArray = Args->FenceValueArray;

    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObjectFromCpu(&Signal));
}

HRESULT
APIENTRY
RxWaitForSynchronizationObjectFromGpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECTFROMGPU* Args)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU Wait = {};

    RX_REQUIRE_THUNK(WaitForSynchronizationObjectFromGpu);

    Wait.hContext = RxKmContext(Args->hContext);
    Wait.ObjectCount = Args->ObjectCount;
    Wait.ObjectHandleArray = Args->ObjectHandleArray;
    static_assert(sizeof(Wait.Reserved) == sizeof(Args->Reserved));
    memcpy(Wait.Reserved, Args->Reserved, sizeof(Wait.Reserved));

    return RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObjectFromGpu(&Wait));
}

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromGpuCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU* Args)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU Signal = {};

    RX_REQUIRE_THUNK(SignalSynchronizationObjectFromGpu);

    Signal.hContext = RxKmContext(Args->hContext);
    Signal.ObjectCount = Args->ObjectCount;
    Signal.ObjectHandleArray = Args->ObjectHandleArray;
    static_assert(sizeof(Signal.Reserved) == sizeof(Args->Reserved));
    memcpy(Signal.Reserved, Args->Reserved, sizeof(Signal.Reserved));

    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObjectFromGpu(&Signal));
}

HRESULT
APIENTRY
RxSignalSynchronizationObjectFromGpu2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2* Args)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal = {};
    D3DKMT_HANDLE Contexts[D3DDDI_MAX_BROADCAST_CONTEXT];
    ULONG Index;

    RX_REQUIRE_THUNK(SignalSynchronizationObjectFromGpu2);

    if (Args->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
        return E_INVALIDARG;

    for (Index = 0; Index < Args->BroadcastContextCount; Index++)
        Contexts[Index] = RxKmContext(Args->BroadcastContextArray[Index]);

    Signal.ObjectCount = Args->ObjectCount;
    Signal.ObjectHandleArray = Args->ObjectHandleArray;
    Signal.Flags = Args->Flags;
    Signal.BroadcastContextCount = Args->BroadcastContextCount;
    Signal.BroadcastContextArray = Contexts;
    static_assert(sizeof(Signal.Reserved) == sizeof(Args->Reserved));
    memcpy(Signal.Reserved, Args->Reserved, sizeof(Signal.Reserved));

    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObjectFromGpu2(&Signal));
}

/* Hardware scheduling: queue and hardware context handles pass through like contexts */

static
D3DKMT_HANDLE
RxKmQueue(
    _In_opt_ HANDLE Queue)
{
    return (D3DKMT_HANDLE)(ULONG_PTR)Queue;
}

HRESULT
APIENTRY
RxCreateHwContextCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEHWCONTEXT* Args)
{
    D3DKMT_CREATEHWCONTEXT Create = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(CreateHwContext);

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags = Args->Flags;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.pPrivateDriverData = Args->pPrivateDriverData;

    Status = RxGetKmtThunks()->CreateHwContext(&Create);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Args->hHwContext = (HANDLE)(ULONG_PTR)Create.hHwContext;
    return S_OK;
}

HRESULT
APIENTRY
RxDestroyHwContextCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYHWCONTEXT* Args)
{
    D3DKMT_DESTROYHWCONTEXT Destroy;

    RX_REQUIRE_THUNK(DestroyHwContext);

    Destroy.hHwContext = RxKmContext(Args->hHwContext);
    return RxHresultFromStatus(RxGetKmtThunks()->DestroyHwContext(&Destroy));
}

HRESULT
APIENTRY
RxCreateHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEHWQUEUE* Args)
{
    D3DKMT_CREATEHWQUEUE Create = {};
    NTSTATUS Status;

    RX_REQUIRE_THUNK(CreateHwQueue);

    Create.hHwContext = RxKmContext(Args->hHwContext);
    Create.Flags = Args->Flags;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.pPrivateDriverData = Args->pPrivateDriverData;

    Status = RxGetKmtThunks()->CreateHwQueue(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateHwQueue failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    RX_TRACE(L"hardware queue 0x%X on context 0x%X", Create.hHwQueue, Create.hHwContext);
    RxKernelDeviceFromHandle(hDevice).NoteKernelQueue(Create.hHwContext, Create.hHwQueue);

    Args->hHwQueue = (HANDLE)(ULONG_PTR)Create.hHwQueue;
    Args->hHwQueueProgressFence = Create.hHwQueueProgressFence;
    Args->HwQueueProgressFenceCPUVirtualAddress = Create.HwQueueProgressFenceCPUVirtualAddress;
    Args->HwQueueProgressFenceGPUVirtualAddress = Create.HwQueueProgressFenceGPUVirtualAddress;
    return S_OK;
}

HRESULT
APIENTRY
RxDestroyHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYHWQUEUE* Args)
{
    D3DKMT_DESTROYHWQUEUE Destroy;
    NTSTATUS Status;

    RX_REQUIRE_THUNK(DestroyHwQueue);

    Destroy.hHwQueue = RxKmQueue(Args->hHwQueue);
    Status = RxGetKmtThunks()->DestroyHwQueue(&Destroy);
    if (NT_SUCCESS(Status))
        RxKernelDeviceFromHandle(hDevice).ForgetKernelQueue(Destroy.hHwQueue);

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxSubmitCommandToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITCOMMANDTOHWQUEUE* Args)
{
    D3DKMT_SUBMITCOMMANDTOHWQUEUE Submit = {};

    RX_REQUIRE_THUNK(SubmitCommandToHwQueue);

    Submit.hHwQueue = RxKmQueue(Args->hHwQueue);
    Submit.HwQueueProgressFenceId = Args->HwQueueProgressFenceId;
    Submit.CommandBuffer = Args->Commands;
    Submit.CommandLength = Args->CommandLength;
    Submit.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Submit.pPrivateDriverData = Args->pPrivateDriverData;
    Submit.NumPrimaries = Args->NumPrimaries;
    Submit.WrittenPrimaries = Args->WrittenPrimaries;

    return RxHresultFromStatus(RxGetKmtThunks()->SubmitCommandToHwQueue(&Submit));
}

HRESULT
APIENTRY
RxSubmitWaitForSyncObjectsToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE* Args)
{
    D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE Wait = {};

    RX_REQUIRE_THUNK(SubmitWaitForSyncObjectsToHwQueue);

    Wait.hHwQueue = RxKmQueue(Args->hHwQueue);
    Wait.ObjectCount = Args->ObjectCount;
    Wait.ObjectHandleArray = Args->ObjectHandleArray;
    Wait.FenceValueArray = Args->FenceValueArray;

    return RxHresultFromStatus(RxGetKmtThunks()->SubmitWaitForSyncObjectsToHwQueue(&Wait));
}

HRESULT
APIENTRY
RxSubmitSignalSyncObjectsToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE* Args)
{
    D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE Signal = {};
    D3DKMT_HANDLE* Queues;
    ULONG Index;
    NTSTATUS Status;

    RX_REQUIRE_THUNK(SubmitSignalSyncObjectsToHwQueue);

    Queues = new (std::nothrow) D3DKMT_HANDLE[Args->BroadcastHwQueueCount ? Args->BroadcastHwQueueCount : 1];
    if (Queues == nullptr)
        return E_OUTOFMEMORY;

    for (Index = 0; Index < Args->BroadcastHwQueueCount; Index++)
        Queues[Index] = RxKmQueue(Args->BroadcastHwQueueArray[Index]);

    Signal.Flags = Args->Flags;
    Signal.BroadcastHwQueueCount = Args->BroadcastHwQueueCount;
    Signal.BroadcastHwQueueArray = Queues;
    Signal.ObjectCount = Args->ObjectCount;
    Signal.ObjectHandleArray = Args->ObjectHandleArray;
    Signal.FenceValueArray = Args->FenceValueArray;

    Status = RxGetKmtThunks()->SubmitSignalSyncObjectsToHwQueue(&Signal);
    delete[] Queues;
    return RxHresultFromStatus(Status);
}

/* Kernel callbacks that are not WDDM 2 specific */

HRESULT
APIENTRY
RxSetPriorityCb(
    _In_ HANDLE hDevice,
    _In_ D3DDDICB_SETPRIORITY* Args)
{
    D3DKMT_SETALLOCATIONPRIORITY Priority = {};

    Priority.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Priority.hResource = RxKernelDeviceFromHandle(hDevice).KernelResourceOf(Args->hResource);
    if (Priority.hResource == 0)
    {
        Priority.phAllocationList = Args->HandleList;
        Priority.AllocationCount = Args->NumAllocations;
    }
    Priority.pPriorities = Args->pPriorities;

    return RxHresultFromStatus(RxGetKmtThunks()->SetAllocationPriority(&Priority));
}

HRESULT
APIENTRY
RxQueryResidencyCb(
    _In_ HANDLE hDevice,
    _Inout_ const D3DDDICB_QUERYRESIDENCY* Args)
{
    D3DKMT_QUERYALLOCATIONRESIDENCY Query = {};

    Query.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Query.hResource = RxKernelDeviceFromHandle(hDevice).KernelResourceOf(Args->hResource);
    if (Query.hResource == 0)
    {
        Query.phAllocationList = Args->HandleList;
        Query.AllocationCount = Args->NumAllocations;
    }
    Query.pResidencyStatus = reinterpret_cast<D3DKMT_ALLOCATIONRESIDENCYSTATUS*>(Args->pResidencyStatus);

    return RxHresultFromStatus(RxGetKmtThunks()->QueryAllocationResidency(&Query));
}

HRESULT
APIENTRY
RxRenderCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_RENDER* Args)
{
    D3DKMT_RENDER Render = {};
    UINT Flags;
    UINT Index;
    NTSTATUS Status;

    Render.hContext = RxKmContext(Args->hContext);
    Render.CommandOffset = Args->CommandOffset;
    Render.CommandLength = Args->CommandLength;
    Render.AllocationCount = Args->NumAllocations;
    Render.PatchLocationCount = Args->NumPatchLocations;
    Render.NewCommandBufferSize = Args->NewCommandBufferSize;
    Render.NewAllocationListSize = Args->NewAllocationListSize;
    Render.NewPatchLocationListSize = Args->NewPatchLocationListSize;
    Render.pPrivateDriverData = Args->pPrivateDriverData;
    Render.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    /* The resize and null rendering bits sit at the same positions in both flag words */
    Flags = Args->Flags.Value & 0xF;
    static_assert(sizeof(Render.Flags) == sizeof(Flags));
    memcpy(&Render.Flags, &Flags, sizeof(Flags));

    Render.BroadcastContextCount = Args->BroadcastContextCount;
    for (Index = 0; Index < Args->BroadcastContextCount && Index < D3DDDI_MAX_BROADCAST_CONTEXT; Index++)
        Render.BroadcastContext[Index] = RxKmContext(Args->BroadcastContext[Index]);

    Status = RxGetKmtThunks()->Render(&Render);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTRender failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Args->pNewCommandBuffer = Render.pNewCommandBuffer;
    Args->NewCommandBufferSize = Render.NewCommandBufferSize;
    Args->pNewAllocationList = Render.pNewAllocationList;
    Args->NewAllocationListSize = Render.NewAllocationListSize;
    Args->pNewPatchLocationList = Render.pNewPatchLocationList;
    Args->NewPatchLocationListSize = Render.NewPatchLocationListSize;
    Args->QueuedBufferCount = Render.QueuedBufferCount;
    Args->NewCommandBuffer = Render.NewCommandBuffer;
    return S_OK;
}

HRESULT
APIENTRY
RxLockCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_LOCK* Args)
{
    D3DKMT_LOCK Lock = {};
    NTSTATUS Status;

    Lock.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Lock.hAllocation = Args->hAllocation;
    Lock.PrivateDriverData = Args->PrivateDriverData;
    Lock.NumPages = Args->NumPages;
    Lock.pPages = Args->pPages;
    Lock.Flags = Args->Flags;

    Status = RxGetKmtThunks()->Lock(&Lock);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Args->hAllocation = Lock.hAllocation;
    Args->pData = Lock.pData;
    Args->GpuVirtualAddress = Lock.GpuVirtualAddress;
    return S_OK;
}

HRESULT
APIENTRY
RxUnlockCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UNLOCK* Args)
{
    D3DKMT_UNLOCK Unlock = {};

    Unlock.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Unlock.NumAllocations = Args->NumAllocations;
    Unlock.phAllocations = Args->phAllocations;

    return RxHresultFromStatus(RxGetKmtThunks()->Unlock(&Unlock));
}

HRESULT
APIENTRY
RxCreateContextCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATECONTEXT* Args)
{
    D3DKMT_CREATECONTEXT Create = {};
    NTSTATUS Status;

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags = Args->Flags;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = RxKernelDeviceFromHandle(hDevice).KernelClientHint();

    Status = RxGetKmtThunks()->CreateContext(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContext(node %u) failed 0x%08lX", Args->NodeOrdinal, Status);
        return RxHresultFromStatus(Status);
    }

    RX_TRACE(L"context 0x%X on node %u", Create.hContext, Args->NodeOrdinal);
    RxKernelDeviceFromHandle(hDevice).NoteKernelContext(Create.hContext);

    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    Args->pCommandBuffer = Create.pCommandBuffer;
    Args->CommandBufferSize = Create.CommandBufferSize;
    Args->pAllocationList = Create.pAllocationList;
    Args->AllocationListSize = Create.AllocationListSize;
    Args->pPatchLocationList = Create.pPatchLocationList;
    Args->PatchLocationListSize = Create.PatchLocationListSize;
    Args->CommandBuffer = Create.CommandBuffer;
    return S_OK;
}

HRESULT
APIENTRY
RxDestroyContextCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYCONTEXT* Args)
{
    D3DKMT_DESTROYCONTEXT Destroy;

    Destroy.hContext = RxKmContext(Args->hContext);
    RxKernelDeviceFromHandle(hDevice).ForgetKernelContext(Destroy.hContext);
    return RxHresultFromStatus(RxGetKmtThunks()->DestroyContext(&Destroy));
}

HRESULT
APIENTRY
RxCreateSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATESYNCHRONIZATIONOBJECT* Args)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT Create = {};
    NTSTATUS Status;

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.Info = Args->Info;

    Status = RxGetKmtThunks()->CreateSynchronizationObject(&Create);
    if (NT_SUCCESS(Status))
        Args->hSyncObject = Create.hSyncObject;

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxCreateSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATESYNCHRONIZATIONOBJECT2* Args)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    NTSTATUS Status;

    Create.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Create.Info = Args->Info;

    Status = RxGetKmtThunks()->CreateSynchronizationObject2(&Create);
    if (NT_SUCCESS(Status))
    {
        Args->Info = Create.Info;
        Args->hSyncObject = Create.hSyncObject;
    }

    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxDestroySynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYSYNCHRONIZATIONOBJECT* Args)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    Destroy.hSyncObject = Args->hSyncObject;
    return RxHresultFromStatus(RxGetKmtThunks()->DestroySynchronizationObject(&Destroy));
}

HRESULT
APIENTRY
RxWaitForSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT* Args)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT Wait = {};

    Wait.hContext = RxKmContext(Args->hContext);
    Wait.ObjectCount = Args->ObjectCount;
    memcpy(Wait.ObjectHandleArray, Args->ObjectHandleArray, sizeof(Wait.ObjectHandleArray));

    return RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObject(&Wait));
}

HRESULT
APIENTRY
RxWaitForSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_WAITFORSYNCHRONIZATIONOBJECT2* Args)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT2 Wait = {};

    Wait.hContext = RxKmContext(Args->hContext);
    Wait.ObjectCount = Args->ObjectCount;
    memcpy(Wait.ObjectHandleArray, Args->ObjectHandleArray, sizeof(Wait.ObjectHandleArray));
    Wait.Fence.FenceValue = Args->FenceValue;

    return RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObject2(&Wait));
}

HRESULT
APIENTRY
RxSignalSynchronizationObjectCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT* Args)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT Signal = {};

    Signal.hContext = RxKmContext(Args->hContext);
    Signal.ObjectCount = Args->ObjectCount;
    memcpy(Signal.ObjectHandleArray, Args->ObjectHandleArray, sizeof(Signal.ObjectHandleArray));
    Signal.Flags = Args->Flags;

    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObject(&Signal));
}

HRESULT
APIENTRY
RxSignalSynchronizationObject2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2* Args)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT2 Signal = {};
    UINT Index;

    Signal.hContext = RxKmContext(Args->hContext);
    Signal.ObjectCount = Args->ObjectCount;
    memcpy(Signal.ObjectHandleArray, Args->ObjectHandleArray, sizeof(Signal.ObjectHandleArray));
    Signal.Flags = Args->Flags;

    Signal.BroadcastContextCount = Args->BroadcastContextCount;
    for (Index = 0; Index < Args->BroadcastContextCount && Index < D3DDDI_MAX_BROADCAST_CONTEXT; Index++)
        Signal.BroadcastContext[Index] = RxKmContext(Args->BroadcastContext[Index]);

    /* Copies either the fence value or the CPU event, they share storage on both sides */
    Signal.Fence.FenceValue = Args->FenceValue;

    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObject2(&Signal));
}

HRESULT
APIENTRY
RxSetAsyncCallbacksCb(
    _In_ HANDLE hDevice,
    _In_ BOOL Enable)
{
    /* Every callback here is safe to call from driver worker threads */
    return S_OK;
}

HRESULT
APIENTRY
RxOfferAllocationsCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_OFFERALLOCATIONS* Args)
{
    D3DKMT_OFFERALLOCATIONS Offer = {};
    D3DKMT_HANDLE* Resources = nullptr;
    NTSTATUS Status;

    if (Args->pResources != nullptr)
    {
        Resources = RxTranslateResources(RxKernelDeviceFromHandle(hDevice), Args->pResources, Args->NumAllocations);
        if (Resources == nullptr)
            return E_OUTOFMEMORY;
    }

    Offer.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Offer.pResources = Resources;
    Offer.HandleList = Args->HandleList;
    Offer.NumAllocations = Args->NumAllocations;
    Offer.Priority = (D3DKMT_OFFER_PRIORITY)Args->Priority;

    Status = RxGetKmtThunks()->OfferAllocations(&Offer);
    delete[] Resources;
    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxReclaimAllocationsCb(
    _In_ HANDLE hDevice,
    _Inout_ const D3DDDICB_RECLAIMALLOCATIONS* Args)
{
    D3DKMT_RECLAIMALLOCATIONS Reclaim = {};
    D3DKMT_HANDLE* Resources = nullptr;
    NTSTATUS Status;

    if (Args->pResources != nullptr)
    {
        Resources = RxTranslateResources(RxKernelDeviceFromHandle(hDevice), Args->pResources, Args->NumAllocations);
        if (Resources == nullptr)
            return E_OUTOFMEMORY;
    }

    Reclaim.hDevice = RxKernelDeviceFromHandle(hDevice).KernelDevice();
    Reclaim.pResources = Resources;
    Reclaim.HandleList = Args->HandleList;
    Reclaim.pDiscarded = Args->pDiscarded;
    Reclaim.NumAllocations = Args->NumAllocations;

    Status = RxGetKmtThunks()->ReclaimAllocations(&Reclaim);
    delete[] Resources;
    return RxHresultFromStatus(Status);
}

HRESULT
APIENTRY
RxLogUmdMarkerCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_LOGUMDMARKER* Args)
{
    return S_OK;
}

HRESULT
APIENTRY
RxSetDisplayPrivateDriverFormatCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SETDISPLAYPRIVATEDRIVERFORMAT* Args)
{
    return E_NOTIMPL;
}
