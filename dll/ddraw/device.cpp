/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel device, user mode driver device and the resources behind video memory surfaces
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Runtime data the kernel keeps with each resource, read back by runtimes that open it */
struct DdResourceRecord
{
    UINT Flags;
    UINT Format;
    UINT Pool;
    UINT Reserved[2];
    UINT SurfaceCount;
    UINT MipLevels;
};

static_assert(sizeof(DdResourceRecord) == 28);

/* Driver interfaces from this one on must use the newer entry points the runtime checks for */
static constexpr UINT DdNewEntryPointsVersion = 0x4002;
static constexpr UINT DdMarkerEntryPointsVersion = 0x4001;
static constexpr UINT DdAlwaysCreateResource2Version = 0x4000;

RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<DdHardware*>(Handle);
}

D3DKMT_HANDLE
DdHardware::KernelDevice() const
{
    return DeviceHandle;
}

D3DKMT_HANDLE
DdHardware::KernelAdapter() const
{
    return AdapterHandle;
}

D3DKMT_CLIENTHINT
DdHardware::KernelClientHint() const
{
    return D3DKMT_CLIENTHINT_DX7;
}

D3DKMT_HANDLE
DdHardware::KernelResourceOf(
    _In_opt_ HANDLE RuntimeResource)
{
    if (RuntimeResource == NULL)
        return 0;
    return static_cast<DdResource*>(RuntimeResource)->KernelResource;
}

VOID
DdHardware::RetireKernelResource(
    _In_ HANDLE RuntimeResource)
{
    static_cast<DdResource*>(RuntimeResource)->KernelResource = 0;
}

static
HRESULT
APIENTRY
DdAllocateCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hDevice);
    DdResource* Resource = static_cast<DdResource*>(Args->hResource);
    D3DKMT_CREATEALLOCATION Create = {};
    DdResourceRecord Record = {};
    NTSTATUS Status;
    UINT Index;

    Create.hDevice = Hardware->DeviceHandle;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.NumAllocations = Args->NumAllocations;
    Create.pAllocationInfo = Args->pAllocationInfo;

    if (Resource != nullptr)
    {
        Record.Flags = Resource->Flags.Value;
        Record.Format = Resource->Format;
        Record.Pool = Resource->Pool;
        Record.SurfaceCount = Resource->SurfaceCount;
        Record.MipLevels = Resource->MipLevels;
        Create.hResource = Resource->KernelResource;
        Create.pPrivateRuntimeData = &Record;
        Create.PrivateRuntimeDataSize = sizeof(Record);
        if (Resource->KernelResource == 0)
            Create.Flags.CreateResource = TRUE;
    }

    /* DirectDraw never makes stereo allocations */
    for (Index = 0; Index < Args->NumAllocations; Index++)
        Args->pAllocationInfo[Index].Flags.Stereo = FALSE;

    Status = RxGetKmtThunks()->CreateAllocation(&Create);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTCreateAllocation failed 0x%08lX", Status);
        return DdHresultFromStatus(Status);
    }

    if (Resource != nullptr)
    {
        if (Resource->KernelResource == 0)
            Resource->KernelResource = Create.hResource;
        Args->hKMResource = Resource->KernelResource;
    }
    return DdHresultFromStatus(Status);
}

static
HRESULT
APIENTRY
DdDeallocateCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hDevice);
    DdResource* Resource = static_cast<DdResource*>(Args->hResource);
    D3DKMT_DESTROYALLOCATION Destroy = {};
    NTSTATUS Status;

    Destroy.hDevice = Hardware->DeviceHandle;
    if (Resource != nullptr)
    {
        Destroy.hResource = Resource->KernelResource;
    }
    else
    {
        Destroy.phAllocationList = Args->HandleList;
        Destroy.AllocationCount = Args->NumAllocations;
    }

    Status = RxGetKmtThunks()->DestroyAllocation(&Destroy);
    if (NT_SUCCESS(Status) || Status == STATUS_DEVICE_REMOVED)
    {
        if (Resource != nullptr)
            Resource->KernelResource = 0;
        if (!NT_SUCCESS(Status))
            return S_OK;
    }
    return DdHresultFromStatus(Status);
}

static
HRESULT
APIENTRY
DdEscapeCb(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDICB_ESCAPE* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hAdapter);
    DdHardware* Device = static_cast<DdHardware*>(Args->hDevice);
    D3DKMT_ESCAPE Escape = {};

    Escape.hAdapter = Hardware->AdapterHandle;
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = Args->Flags;
    Escape.pPrivateDriverData = Args->pPrivateDriverData;
    Escape.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    if (Device != nullptr)
    {
        Escape.hDevice = Device->DeviceHandle;
        if (Device->DriverVersion > 1)
            Escape.hContext = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Args->hContext));
    }

    return DdHresultFromStatus(RxGetKmtThunks()->Escape(&Escape));
}

/* Presenting and overlays have no DirectDraw path to the kernel yet */
static
HRESULT
APIENTRY
DdPresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args)
{
    DD_TRACE_ONCE(L"the driver asked to present, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdCreateOverlayCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEOVERLAY* Args)
{
    DD_TRACE_ONCE(L"the driver asked for an overlay, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdUpdateOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UPDATEOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdFlipOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_FLIPOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdDestroyOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdOfferAllocations2Cb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_OFFERALLOCATIONS2* Args)
{
    D3DDDICB_OFFERALLOCATIONS Offer = {};

    Offer.pResources = Args->pResources;
    Offer.HandleList = Args->HandleList;
    Offer.NumAllocations = Args->NumAllocations;
    Offer.Priority = Args->Priority;
    return RxOfferAllocationsCb(hDevice, &Offer);
}

static
HRESULT
APIENTRY
DdSubmitPresentBltToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTBLTTOHWQUEUE* Args)
{
    DD_TRACE_ONCE(L"the driver asked to queue a present, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdSubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    DD_TRACE_ONCE(L"the driver asked to queue a present, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
DdCreateNativeFenceCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DKMT_CREATENATIVEFENCE* Args)
{
    DD_TRACE_ONCE(L"the driver asked for a native fence, which is not implemented yet");
    return E_NOTIMPL;
}

static
VOID
DdFillDeviceCallbacks(
    _Out_ D3DDDI_DEVICECALLBACKS* Callbacks)
{
    ZeroMemory(Callbacks, sizeof(*Callbacks));
    Callbacks->pfnAllocateCb = DdAllocateCb;
    Callbacks->pfnDeallocateCb = DdDeallocateCb;
    Callbacks->pfnSetPriorityCb = RxSetPriorityCb;
    Callbacks->pfnQueryResidencyCb = RxQueryResidencyCb;
    Callbacks->pfnPresentCb = DdPresentCb;
    Callbacks->pfnRenderCb = RxRenderCb;
    Callbacks->pfnLockCb = RxLockCb;
    Callbacks->pfnUnlockCb = RxUnlockCb;
    Callbacks->pfnEscapeCb = DdEscapeCb;
    Callbacks->pfnCreateOverlayCb = DdCreateOverlayCb;
    Callbacks->pfnUpdateOverlayCb = DdUpdateOverlayCb;
    Callbacks->pfnFlipOverlayCb = DdFlipOverlayCb;
    Callbacks->pfnDestroyOverlayCb = DdDestroyOverlayCb;
    Callbacks->pfnCreateContextCb = RxCreateContextCb;
    Callbacks->pfnDestroyContextCb = RxDestroyContextCb;
    Callbacks->pfnCreateSynchronizationObjectCb = RxCreateSynchronizationObjectCb;
    Callbacks->pfnDestroySynchronizationObjectCb = RxDestroySynchronizationObjectCb;
    Callbacks->pfnWaitForSynchronizationObjectCb = RxWaitForSynchronizationObjectCb;
    Callbacks->pfnSignalSynchronizationObjectCb = RxSignalSynchronizationObjectCb;
    Callbacks->pfnSetDisplayPrivateDriverFormatCb = RxSetDisplayPrivateDriverFormatCb;
    Callbacks->pfnOfferAllocationsCb = RxOfferAllocationsCb;
    Callbacks->pfnReclaimAllocationsCb = RxReclaimAllocationsCb;
    Callbacks->pfnCreateSynchronizationObject2Cb = RxCreateSynchronizationObject2Cb;
    Callbacks->pfnWaitForSynchronizationObject2Cb = RxWaitForSynchronizationObject2Cb;
    Callbacks->pfnSignalSynchronizationObject2Cb = RxSignalSynchronizationObject2Cb;
    Callbacks->pfnMakeResidentCb = RxMakeResidentCb;
    Callbacks->pfnEvictCb = RxEvictCb;
    Callbacks->pfnWaitForSynchronizationObjectFromCpuCb = RxWaitForSynchronizationObjectFromCpuCb;
    Callbacks->pfnSignalSynchronizationObjectFromCpuCb = RxSignalSynchronizationObjectFromCpuCb;
    Callbacks->pfnWaitForSynchronizationObjectFromGpuCb = RxWaitForSynchronizationObjectFromGpuCb;
    Callbacks->pfnSignalSynchronizationObjectFromGpuCb = RxSignalSynchronizationObjectFromGpuCb;
    Callbacks->pfnCreatePagingQueueCb = RxCreatePagingQueueCb;
    Callbacks->pfnDestroyPagingQueueCb = RxDestroyPagingQueueCb;
    Callbacks->pfnLock2Cb = RxLock2Cb;
    Callbacks->pfnUnlock2Cb = RxUnlock2Cb;
    Callbacks->pfnInvalidateCacheCb = RxInvalidateCacheCb;
    Callbacks->pfnReserveGpuVirtualAddressCb = RxReserveGpuVirtualAddressCb;
    Callbacks->pfnMapGpuVirtualAddressCb = RxMapGpuVirtualAddressCb;
    Callbacks->pfnFreeGpuVirtualAddressCb = RxFreeGpuVirtualAddressCb;
    Callbacks->pfnUpdateGpuVirtualAddressCb = RxUpdateGpuVirtualAddressCb;
    Callbacks->pfnCreateContextVirtualCb = RxCreateContextVirtualCb;
    Callbacks->pfnSubmitCommandCb = RxSubmitCommandCb;
    Callbacks->pfnDeallocate2Cb = RxDeallocate2Cb;
    Callbacks->pfnSignalSynchronizationObjectFromGpu2Cb = RxSignalSynchronizationObjectFromGpu2Cb;
    Callbacks->pfnReclaimAllocations2Cb = RxReclaimAllocations2Cb;
    Callbacks->pfnGetResourcePresentPrivateDriverDataCb = RxGetResourcePresentPrivateDriverDataCb;
    Callbacks->pfnUpdateAllocationPropertyCb = RxUpdateAllocationPropertyCb;
    Callbacks->pfnOfferAllocations2Cb = DdOfferAllocations2Cb;
    Callbacks->pfnReclaimAllocations3Cb = RxReclaimAllocations3Cb;
    Callbacks->pfnCreateHwQueueCb = RxCreateHwQueueCb;
    Callbacks->pfnDestroyHwQueueCb = RxDestroyHwQueueCb;
    Callbacks->pfnSubmitCommandToHwQueueCb = RxSubmitCommandToHwQueueCb;
    Callbacks->pfnSubmitWaitForSyncObjectsToHwQueueCb = RxSubmitWaitForSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitSignalSyncObjectsToHwQueueCb = RxSubmitSignalSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitPresentBltToHwQueueCb = DdSubmitPresentBltToHwQueueCb;
    Callbacks->pfnSubmitPresentToHwQueueCb = DdSubmitPresentToHwQueueCb;
    Callbacks->pfnCreateNativeFenceCb = DdCreateNativeFenceCb;
}

BOOLEAN
DdCreateKernelDevice(
    _Inout_ DdHardware* Hardware)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEDEVICE Create = {};
    NTSTATUS Status;

    if (Kmt->CreateDevice == nullptr)
        return FALSE;

    Create.hAdapter = Hardware->AdapterHandle;
    Create.Flags.LegacyMode = TRUE;
    Status = Kmt->CreateDevice(&Create);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTCreateDevice failed 0x%08lX, running without hardware", Status);
        return FALSE;
    }

    Hardware->DeviceHandle = Create.hDevice;
    return TRUE;
}

/* Interfaces from WDDM 1.2 on drop the old blit, create, flush and present entry points for newer ones */
static
BOOLEAN
DdDriverEntryPointsValid(
    _In_ const DdHardware* Hardware)
{
    const D3DDDI_DEVICEFUNCS& Funcs = Hardware->DeviceFunctions;

    if (Hardware->DriverVersion < DdNewEntryPointsVersion)
        return TRUE;

    if (Funcs.pfnVolBlt != nullptr || Funcs.pfnVolBlt1 == nullptr)
        return FALSE;
    if (Funcs.pfnBufBlt != nullptr || Funcs.pfnBufBlt1 == nullptr)
        return FALSE;
    if (Funcs.pfnTexBlt != nullptr || Funcs.pfnTexBlt1 == nullptr)
        return FALSE;
    if (Funcs.pfnCreateResource != nullptr || Funcs.pfnCreateResource2 == nullptr)
        return FALSE;

    if (Hardware->UsesFlush1)
    {
        if (Funcs.pfnFlush != nullptr || Funcs.pfnFlush1 == nullptr)
            return FALSE;
        if (Funcs.pfnPresent != nullptr || Funcs.pfnPresent1 == nullptr)
            return FALSE;
    }
    return TRUE;
}

BOOLEAN
DdCreateDriverDevice(
    _Inout_ DdHardware* Hardware)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    const D3DDDI_DEVICEFUNCS& Funcs = Hardware->DeviceFunctions;
    D3DDDIARG_CREATEDEVICE Args = {};
    D3DKMT_CREATECONTEXT Context = {};
    NTSTATUS Status;
    HRESULT hr;

    /* Drivers from before the WDDM 1.1 interface get their first command buffer from the runtime */
    if (Hardware->DriverVersion <= 1)
    {
        Context.hDevice = Hardware->DeviceHandle;
        Context.ClientHint = D3DKMT_CLIENTHINT_DX7;
        Status = Kmt->CreateContext(&Context);
        if (!NT_SUCCESS(Status))
        {
            DdTrace(L"D3DKMTCreateContext failed 0x%08lX", Status);
            return FALSE;
        }
        Hardware->ContextHandle = Context.hContext;
        Args.pCommandBuffer = Context.pCommandBuffer;
        Args.CommandBufferSize = Context.CommandBufferSize;
        Args.pAllocationList = Context.pAllocationList;
        Args.AllocationListSize = Context.AllocationListSize;
        Args.pPatchLocationList = Context.pPatchLocationList;
        Args.PatchLocationListSize = Context.PatchLocationListSize;
    }

    DdFillDeviceCallbacks(&Hardware->DeviceCallbacks);
    Args.hDevice = Hardware;
    Args.Interface = 7;
    Args.Version = Hardware->RuntimeVersion;
    Args.pCallbacks = &Hardware->DeviceCallbacks;
    Args.pDeviceFuncs = &Hardware->DeviceFunctions;

    hr = Hardware->Functions.pfnCreateDevice(Hardware->DriverAdapter, &Args);
    if (FAILED(hr))
    {
        DdTrace(L"UMD CreateDevice failed 0x%08lX, running without hardware", hr);
        return FALSE;
    }
    Hardware->DriverDevice = Args.hDevice;

    Hardware->UsesFlush1 = Hardware->DriverVersion > DdAlwaysCreateResource2Version && Funcs.pfnFlush1 != nullptr &&
                           Funcs.pfnCheckCounterInfo != nullptr && Funcs.pfnCheckCounter != nullptr &&
                           Funcs.pfnUpdateSubresourceUP != nullptr && Funcs.pfnPresent1 != nullptr &&
                           (Hardware->DriverVersion <= DdMarkerEntryPointsVersion ||
                            (Funcs.pfnSetMarker != nullptr && Funcs.pfnSetMarkerMode != nullptr));

    if (!DdDriverEntryPointsValid(Hardware))
    {
        DdTrace(L"UMD %s offers entry points that do not fit interface 0x%X", Hardware->UmdName,
                Hardware->DriverVersion);
        return FALSE;
    }
    return TRUE;
}

VOID
DdDestroyDriverDevice(
    _Inout_ DdHardware* Hardware)
{
    if (Hardware->DriverDevice == NULL)
        return;

    Hardware->DeviceFunctions.pfnDestroyDevice(Hardware->DriverDevice);
    Hardware->DriverDevice = NULL;
    ZeroMemory(&Hardware->DeviceFunctions, sizeof(Hardware->DeviceFunctions));
}

VOID
DdDestroyKernelDevice(
    _Inout_ DdHardware* Hardware)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROYCONTEXT Context = {};
    D3DKMT_DESTROYDEVICE Device = {};

    if (Hardware->ContextHandle != 0)
    {
        Context.hContext = Hardware->ContextHandle;
        Kmt->DestroyContext(&Context);
        Hardware->ContextHandle = 0;
    }

    if (Hardware->DeviceHandle != 0)
    {
        Device.hDevice = Hardware->DeviceHandle;
        Kmt->DestroyDevice(&Device);
        Hardware->DeviceHandle = 0;
    }
}
