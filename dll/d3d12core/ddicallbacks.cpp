/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runtime callbacks handed to the Direct3D 12 driver at device creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/*
 * Kernel callbacks receive the runtime device as hDevice. Contexts come back to the
 * driver as their kernel handles, so the shared callbacks pass them through unchanged.
 */

RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<RxDevice12*>(Handle);
}

static
RxDevice12&
RxDeviceOf(
    _In_ HANDLE Handle)
{
    return *static_cast<RxDevice12*>(Handle);
}

static
HRESULT
APIENTRY
RxEscapeCb12(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDICB_ESCAPE* Args)
{
    D3DKMT_ESCAPE Escape = {};

    /* The first argument is the runtime adapter; the device inside the arguments is optional */
    if (Args->hDevice != NULL)
    {
        Escape.hAdapter = RxDeviceOf(Args->hDevice).KernelAdapter();
        Escape.hDevice = RxDeviceOf(Args->hDevice).KernelDevice();
    }
    else
    {
        Escape.hAdapter = static_cast<RxAdapter12*>(hAdapter)->GetKmtHandle();
    }

    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = Args->Flags;
    Escape.pPrivateDriverData = Args->pPrivateDriverData;
    Escape.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Escape.hContext = (D3DKMT_HANDLE)(ULONG_PTR)Args->hContext;

    return RxHresultFromStatus(RxGetKmtThunks()->Escape(&Escape));
}

static
HRESULT
APIENTRY
RxSetDisplayModeCb12(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SETDISPLAYMODE* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
RxNoOverlayCb12(
    _In_ HANDLE hDevice,
    _In_ PVOID Args)
{
    return E_NOTIMPL;
}

/* Offering through the old callback does nothing here, as on Windows; drivers use the 12 one */
static
HRESULT
APIENTRY
RxIgnoreOfferCb12(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_OFFERALLOCATIONS* Args)
{
    return S_OK;
}

static
HRESULT
APIENTRY
RxSubmitHistorySequenceCb12(
    _In_ HANDLE hDevice,
    _In_ PVOID Args)
{
    return S_OK;
}

static
D3DDDI_DEVICECALLBACKS
RxBuildKernelCallbacks(VOID)
{
    D3DDDI_DEVICECALLBACKS Table = {};

    Table.pfnSetPriorityCb = RxSetPriorityCb;
    Table.pfnQueryResidencyCb = RxQueryResidencyCb;
    Table.pfnSetDisplayModeCb = RxSetDisplayModeCb12;
    Table.pfnRenderCb = RxRenderCb;
    Table.pfnLockCb = RxLockCb;
    Table.pfnUnlockCb = RxUnlockCb;
    Table.pfnEscapeCb = RxEscapeCb12;
    Table.pfnCreateOverlayCb = reinterpret_cast<PFND3DDDI_CREATEOVERLAYCB>(RxNoOverlayCb12);
    Table.pfnUpdateOverlayCb = reinterpret_cast<PFND3DDDI_UPDATEOVERLAYCB>(RxNoOverlayCb12);
    Table.pfnFlipOverlayCb = reinterpret_cast<PFND3DDDI_FLIPOVERLAYCB>(RxNoOverlayCb12);
    Table.pfnDestroyOverlayCb = reinterpret_cast<PFND3DDDI_DESTROYOVERLAYCB>(RxNoOverlayCb12);
    Table.pfnCreateContextCb = RxCreateContextCb;
    Table.pfnDestroyContextCb = RxDestroyContextCb;
    Table.pfnCreateSynchronizationObjectCb = RxCreateSynchronizationObjectCb;
    Table.pfnDestroySynchronizationObjectCb = RxDestroySynchronizationObjectCb;
    Table.pfnWaitForSynchronizationObjectCb = RxWaitForSynchronizationObjectCb;
    Table.pfnSignalSynchronizationObjectCb = RxSignalSynchronizationObjectCb;
    Table.pfnSetDisplayPrivateDriverFormatCb = RxSetDisplayPrivateDriverFormatCb;
    Table.pfnOfferAllocationsCb = RxIgnoreOfferCb12;
    Table.pfnReclaimAllocationsCb = RxReclaimAllocationsCb;
    Table.pfnCreateSynchronizationObject2Cb = RxCreateSynchronizationObject2Cb;
    Table.pfnWaitForSynchronizationObject2Cb = RxWaitForSynchronizationObject2Cb;
    Table.pfnSignalSynchronizationObject2Cb = RxSignalSynchronizationObject2Cb;
    Table.pfnLogUMDMarkerCb = RxLogUmdMarkerCb;
    Table.pfnMakeResidentCb = RxMakeResidentCb;
    Table.pfnEvictCb = RxEvictCb;
    Table.pfnWaitForSynchronizationObjectFromCpuCb = RxWaitForSynchronizationObjectFromCpuCb;
    Table.pfnSignalSynchronizationObjectFromCpuCb = RxSignalSynchronizationObjectFromCpuCb;
    Table.pfnWaitForSynchronizationObjectFromGpuCb = RxWaitForSynchronizationObjectFromGpuCb;
    Table.pfnSignalSynchronizationObjectFromGpuCb = RxSignalSynchronizationObjectFromGpuCb;
    Table.pfnCreatePagingQueueCb = RxCreatePagingQueueCb;
    Table.pfnDestroyPagingQueueCb = RxDestroyPagingQueueCb;
    Table.pfnLock2Cb = RxLock2Cb;
    Table.pfnUnlock2Cb = RxUnlock2Cb;
    Table.pfnInvalidateCacheCb = RxInvalidateCacheCb;
    Table.pfnReserveGpuVirtualAddressCb = RxReserveGpuVirtualAddressCb;
    Table.pfnMapGpuVirtualAddressCb = RxMapGpuVirtualAddressCb;
    Table.pfnFreeGpuVirtualAddressCb = RxFreeGpuVirtualAddressCb;
    Table.pfnUpdateGpuVirtualAddressCb = RxUpdateGpuVirtualAddressCb;
    Table.pfnCreateContextVirtualCb = RxCreateContextVirtualCb;
    Table.pfnSubmitCommandCb = RxSubmitCommandCb;
    Table.pfnSignalSynchronizationObjectFromGpu2Cb = RxSignalSynchronizationObjectFromGpu2Cb;
    Table.pfnUpdateAllocationPropertyCb = RxUpdateAllocationPropertyCb;
    Table.pfnCreateHwQueueCb = RxCreateHwQueueCb;
    Table.pfnDestroyHwQueueCb = RxDestroyHwQueueCb;
    Table.pfnSubmitCommandToHwQueueCb = RxSubmitCommandToHwQueueCb;
    Table.pfnSubmitWaitForSyncObjectsToHwQueueCb = RxSubmitWaitForSyncObjectsToHwQueueCb;
    Table.pfnSubmitSignalSyncObjectsToHwQueueCb = RxSubmitSignalSyncObjectsToHwQueueCb;
    Table.pfnSubmitHistorySequenceCb = reinterpret_cast<PFND3DDDI_SUBMITHISTORYSEQUENCECB>(RxSubmitHistorySequenceCb12);
    return Table;
}

/* Allocation, DDI build 22 and later: the driver passes its runtime resource handle */

static
HRESULT
APIENTRY
RxAllocateCb0022(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _Inout_ D3D12DDICB_ALLOCATE_0022* Args)
{
    RxDevice12& Device = RxDeviceOf(hRTDevice.handle);
    const KmtThunks* Kmt = RxGetKmtThunks();
    RxRuntimeResource12* Resource = static_cast<RxRuntimeResource12*>(Args->hResource);
    D3DKMT_CREATEALLOCATION Create = {};
    NTSTATUS Status;

    static_assert(sizeof(D3D12DDI_ALLOCATION_INFO_0022) == sizeof(D3DDDI_ALLOCATIONINFO2));

    if (Kmt->CreateAllocation2 == nullptr)
        return E_NOTIMPL;

    Create.hDevice = Device.KernelDevice();
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.NumAllocations = Args->NumAllocations;
    Create.pAllocationInfo2 = reinterpret_cast<D3DDDI_ALLOCATIONINFO2*>(Args->pAllocationInfo);
    Create.hPrivateRuntimeResourceHandle = Args->hResource;

    /* Allocations for a runtime resource hang off one kernel resource */
    if (Resource != nullptr)
    {
        if (Resource->KmResource == 0)
            Create.Flags.CreateResource = TRUE;
        else
            Create.hResource = Resource->KmResource;
    }

    Status = Kmt->CreateAllocation2(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateAllocation2 failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    if (Resource != nullptr && Create.Flags.CreateResource)
        Resource->KmResource = Create.hResource;

    Args->hKMResource = Create.hResource;
    return S_OK;
}

static
HRESULT
APIENTRY
RxDeallocateCb0022(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ const D3D12DDICB_DEALLOCATE_0022* Args)
{
    RxDevice12& Device = RxDeviceOf(hRTDevice.handle);
    RxRuntimeResource12* Resource = static_cast<RxRuntimeResource12*>(Args->hResource);
    D3DKMT_DESTROYALLOCATION2 Destroy = {};
    NTSTATUS Status;

    if (RxGetKmtThunks()->DestroyAllocation2 == nullptr)
        return E_NOTIMPL;

    Destroy.hDevice = Device.KernelDevice();
    if (Resource != nullptr)
    {
        if (Resource->KmResource == 0)
            return S_OK;
        Destroy.hResource = Resource->KmResource;
    }
    else
    {
        Destroy.phAllocationList = Args->HandleList;
        Destroy.AllocationCount = Args->NumAllocations;
    }

    Destroy.Flags.AssumeNotInUse = (Args->Flags & D3D12DDI_DEALLOCATE_FLAGS_0022_ASSUME_NOT_IN_USE) != 0;
    Destroy.Flags.SynchronousDestroy = (Args->Flags & D3D12DDI_DEALLOCATE_FLAGS_0022_SYNCHRONOUS_DESTROY) != 0;

    Status = RxGetKmtThunks()->DestroyAllocation2(&Destroy);
    if (NT_SUCCESS(Status) && Resource != nullptr)
        Resource->KmResource = 0;

    return RxHresultFromStatus(Status);
}

/* Core layer callbacks */

static
VOID
APIENTRY
RxSetErrorCb12(
    _In_ D3D10DDI_HRTDEVICE hRTDevice,
    _In_ HRESULT Error)
{
    RxDevice12::NoteDriverError(Error);

    if (Error == DXGI_ERROR_DEVICE_REMOVED || Error == DXGI_ERROR_DEVICE_RESET || Error == DXGI_ERROR_DEVICE_HUNG ||
        Error == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
    {
        RxDeviceOf(hRTDevice.handle).MarkRemoved(Error);
    }
}

/* The runtime's own paging queue stands behind every residency call the driver makes */
static
HRESULT
APIENTRY
RxMakeResidentCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTPAGINGQUEUE hRTPagingQueue,
    _Inout_ D3DDDI_MAKERESIDENT* Args)
{
    Args->hPagingQueue = static_cast<RxPagingQueue12*>(hRTPagingQueue.handle)->Handle;
    return RxMakeResidentCb(hRTDevice.handle, Args);
}

static
HRESULT
APIENTRY
RxEvictCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ const D3DDDICB_EVICT* Args)
{
    D3DDDICB_EVICT Copy = *Args;

    return RxEvictCb(hRTDevice.handle, &Copy);
}

static
HRESULT
APIENTRY
RxReclaimAllocations2Cb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTPAGINGQUEUE hRTPagingQueue,
    _Inout_ D3D12DDICB_RECLAIMALLOCATIONS2* Args)
{
    D3DKMT_RECLAIMALLOCATIONS2 Reclaim = {};
    NTSTATUS Status;

    if (RxGetKmtThunks()->ReclaimAllocations2 == nullptr)
        return E_NOTIMPL;

    Reclaim.hPagingQueue = static_cast<RxPagingQueue12*>(hRTPagingQueue.handle)->Handle;
    Reclaim.NumAllocations = Args->NumAllocations;
    Reclaim.HandleList = Args->HandleList;
    Reclaim.pDiscarded = Args->pDiscarded;

    Status = RxGetKmtThunks()->ReclaimAllocations2(&Reclaim);
    Args->PagingFenceValue = Reclaim.PagingFenceValue;
    return RxHresultFromStatus(Status);
}

static
HRESULT
APIENTRY
RxOfferAllocationsCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ const D3D12DDICB_OFFERALLOCATIONS* Args)
{
    D3DKMT_OFFERALLOCATIONS Offer = {};

    if (Args->Priority < D3DDDI_OFFER_PRIORITY_LOW || Args->Priority > D3DDDI_OFFER_PRIORITY_AUTO)
        return E_INVALIDARG;

    Offer.hDevice = RxDeviceOf(hRTDevice.handle).KernelDevice();
    Offer.HandleList = Args->HandleList;
    Offer.NumAllocations = Args->NumAllocations;
    Offer.Priority = (D3DKMT_OFFER_PRIORITY)Args->Priority;
    Offer.Flags.AllowDecommit = TRUE;

    return RxHresultFromStatus(RxGetKmtThunks()->OfferAllocations(&Offer));
}

/* Driver work the runtime runs on its own threads */
struct RxBackgroundWork
{
    PFND3D12DDI_UMD_CALLBACK_METHOD Callback;
    VOID* Context;
};

/* Work items queued and not yet finished, so callers can wait for what is queued now */
static volatile LONG RxBackgroundPending = 0;

static
VOID
CALLBACK
RxRunBackgroundWork(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_ PVOID Context)
{
    RxBackgroundWork* Work = static_cast<RxBackgroundWork*>(Context);

    Work->Callback(Work->Context);
    delete Work;

    if (InterlockedDecrement(&RxBackgroundPending) == 0)
        WakeByAddressAll((PVOID)&RxBackgroundPending);
}

static
VOID
CALLBACK
RxSignalWhenIdle(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_ PVOID Context)
{
    LONG Pending = RxBackgroundPending;

    while (Pending != 0)
    {
        WaitOnAddress(&RxBackgroundPending, &Pending, sizeof(Pending), INFINITE);
        Pending = RxBackgroundPending;
    }
    SetEvent(static_cast<HANDLE>(Context));
}

VOID
RxSignalAfterBackgroundWork12(
    _In_ HANDLE Event)
{
    if (RxBackgroundPending == 0 || !TrySubmitThreadpoolCallback(RxSignalWhenIdle, Event, nullptr))
        SetEvent(Event);
}

static
HRESULT
APIENTRY
RxQueueBackgroundWorkCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ PFND3D12DDI_UMD_CALLBACK_METHOD pfnCallback,
    _In_opt_ PFND3D12DDI_UMD_CALLBACK_METHOD pfnCancel,
    _In_opt_ void* pContext)
{
    RxBackgroundWork* Work = new (std::nothrow) RxBackgroundWork{ pfnCallback, pContext };

    if (Work == nullptr)
        return E_OUTOFMEMORY;

    InterlockedIncrement(&RxBackgroundPending);
    if (!TrySubmitThreadpoolCallback(RxRunBackgroundWork, Work, nullptr))
    {
        InterlockedDecrement(&RxBackgroundPending);
        delete Work;
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

/* Native fences are never asked for, so the driver has none to make */
static
HRESULT
APIENTRY
RxCreateNativeFenceCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTFENCE hRTFence,
    _Inout_ D3DKMT_CREATENATIVEFENCE* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
RxOpenNativeFenceCb12(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTFENCE hRTFence,
    _Inout_ D3DKMT_OPENNATIVEFENCEFROMNTHANDLE* Args)
{
    return E_NOTIMPL;
}

static
D3D12DDI_CORELAYER_DEVICECALLBACKS_0116
RxBuildCoreLayerCallbacks(VOID)
{
    D3D12DDI_CORELAYER_DEVICECALLBACKS_0116 Table = {};

    Table.pfnSetErrorCb = RxSetErrorCb12;
    Table.pfnSetCommandListErrorCb = RxCommandList::SetCommandListErrorCb;
    Table.pfnSetCommandListDDITableCb = RxCommandList::SetCommandListDdiTableCb;
    Table.pfnCreateContextCb = RxCommandQueue::CreateContextCb;
    Table.pfnCreateContextVirtualCb = RxCommandQueue::CreateContextVirtualCb;
    Table.pfnDestroyContextCb = RxCommandQueue::DestroyContextCb;
    Table.pfnMakeResidentCb = RxMakeResidentCb12;
    Table.pfnEvictCb = RxEvictCb12;
    Table.pfnReclaimAllocations2Cb = RxReclaimAllocations2Cb12;
    Table.pfnOfferAllocationsCb = RxOfferAllocationsCb12;
    Table.pfnAllocateCb = RxAllocateCb0022;
    Table.pfnDeallocateCb = RxDeallocateCb0022;
    Table.pfnCreateSchedulingGroupContextCb = RxSchedulingGroup::CreateContextCb;
    Table.pfnCreateSchedulingGroupContextVirtualCb = RxSchedulingGroup::CreateContextVirtualCb;
    Table.pfnCreateHwQueueCb = RxCommandQueue::CreateHwQueueCb;
    Table.pfnQueueBackgroundProcessingWorkCb = RxQueueBackgroundWorkCb12;
    Table.pfnCreateNativeFenceCb = RxCreateNativeFenceCb12;
    Table.pfnOpenNativeFenceCb = RxOpenNativeFenceCb12;
    return Table;
}

const D3DDDI_DEVICECALLBACKS*
RxGetKernelCallbacks12(VOID)
{
    static const D3DDDI_DEVICECALLBACKS Table = RxBuildKernelCallbacks();

    return &Table;
}

const D3D12DDI_CORELAYER_DEVICECALLBACKS_0116*
RxGetCoreLayerCallbacks12(VOID)
{
    static const D3D12DDI_CORELAYER_DEVICECALLBACKS_0116 Table = RxBuildCoreLayerCallbacks();

    return &Table;
}
