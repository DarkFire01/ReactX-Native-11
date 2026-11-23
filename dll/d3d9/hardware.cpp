/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The kernel device, the user mode driver device and the resources a device asks the driver for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Runtime data the kernel keeps with each resource, read back by runtimes that open it */
struct D9ResourceRecord
{
    UINT Flags;
    UINT Format;
    UINT Pool;
    UINT Reserved[2];
    UINT SurfaceCount;
    UINT MipLevels;
};

static_assert(sizeof(D9ResourceRecord) == 28);

/* Direct3D 9 creates its driver device at the DirectX 9 interface */
static constexpr UINT D9DeviceInterface = 9;

/* Driver interfaces from these on must offer the newer entry points the runtime checks for */
static constexpr UINT D9NewEntryPointsVersion = 0x4002;
static constexpr UINT D9MarkerEntryPointsVersion = 0x4001;
static constexpr UINT D9AlwaysCreateResource2Version = 0x4000;
static constexpr UINT D9SyncTokenVersion = 0x6000;
static constexpr UINT D9FlipBatchingUpgradeVersion = 0x6001;

RxKernelDevice&
RxKernelDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<D9Device*>(Handle);
}

D3DKMT_HANDLE
D9Device::KernelDevice() const
{
    return DeviceHandle;
}

D3DKMT_HANDLE
D9Device::KernelAdapter() const
{
    return Adapter->KernelAdapter;
}

D3DKMT_CLIENTHINT
D9Device::KernelClientHint() const
{
    return D3DKMT_CLIENTHINT_DX9;
}

D3DKMT_HANDLE
D9Device::KernelResourceOf(
    _In_opt_ HANDLE RuntimeResource)
{
    if (RuntimeResource == NULL)
        return 0;
    return static_cast<D9Resource*>(RuntimeResource)->KernelResource;
}

VOID
D9Device::RetireKernelResource(
    _In_ HANDLE RuntimeResource)
{
    static_cast<D9Resource*>(RuntimeResource)->KernelResource = 0;
}

VOID
D9Device::NoteKernelContext(
    _In_ D3DKMT_HANDLE Context)
{
    if (KernelContextCount == D9MaxKernelContexts)
    {
        D9_TRACE_ONCE(L"the driver made more than %u contexts; later ones keep the default priority",
                      D9MaxKernelContexts);
        return;
    }
    KernelContexts[KernelContextCount] = Context;
    KernelContextCount += 1;
}

VOID
D9Device::ForgetKernelContext(
    _In_ D3DKMT_HANDLE Context)
{
    for (UINT Index = 0; Index < KernelContextCount; Index++)
    {
        if (KernelContexts[Index] != Context)
            continue;

        MoveMemory(&KernelContexts[Index], &KernelContexts[Index + 1],
                   (KernelContextCount - Index - 1) * sizeof(KernelContexts[0]));
        KernelContextCount -= 1;
        return;
    }
}

HRESULT
D9Device::AllocateCallback(
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    D9Resource* Resource = static_cast<D9Resource*>(Args->hResource);
    D3DKMT_CREATEALLOCATION Create = {};
    D9ResourceRecord Record = {};
    NTSTATUS Status;

    Create.hDevice = DeviceHandle;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.NumAllocations = Args->NumAllocations;
    Create.pAllocationInfo = Args->pAllocationInfo;

    if (Resource != nullptr)
    {
        Record.Flags = Resource->Flags.Value;
        Record.Format = Resource->Format;
        Record.Pool = Resource->Pool;
        Record.SurfaceCount = 1;
        Record.MipLevels = 1;
        Create.hResource = Resource->KernelResource;
        Create.pPrivateRuntimeData = &Record;
        Create.PrivateRuntimeDataSize = sizeof(Record);
        if (Resource->KernelResource == 0)
            Create.Flags.CreateResource = TRUE;
    }

    Status = RxGetKmtThunks()->CreateAllocation(&Create);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTCreateAllocation failed 0x%08lX", Status);
        return D9HresultFromStatus(Status);
    }

    if (Resource != nullptr)
    {
        if (Resource->KernelResource == 0)
            Resource->KernelResource = Create.hResource;
        Args->hKMResource = Resource->KernelResource;
    }
    return S_OK;
}

HRESULT
D9Device::DeallocateCallback(
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    D9Resource* Resource = static_cast<D9Resource*>(Args->hResource);
    D3DKMT_DESTROYALLOCATION Destroy = {};
    NTSTATUS Status;

    Destroy.hDevice = DeviceHandle;
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
        return S_OK;
    }
    return D9HresultFromStatus(Status);
}

static
HRESULT
APIENTRY
D9AllocateCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    return static_cast<D9Device*>(hDevice)->AllocateCallback(Args);
}

static
HRESULT
APIENTRY
D9DeallocateCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    return static_cast<D9Device*>(hDevice)->DeallocateCallback(Args);
}

static
HRESULT
APIENTRY
D9PresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args)
{
    return static_cast<D9Device*>(hDevice)->PresentCallback(Args);
}

static
HRESULT
APIENTRY
D9SubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    return static_cast<D9Device*>(hDevice)->SubmitPresentCallback(Args);
}

static
HRESULT
APIENTRY
D9SetDisplayModeCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SETDISPLAYMODE* Args)
{
    return static_cast<D9Device*>(hDevice)->SetDisplayModeCallback(Args);
}

/* The runtime hands out no synchronization tokens yet, so every token a driver names is unknown */
static
HRESULT
APIENTRY
D9AcquireResourceCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SYNCTOKEN* Args)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Args);
    return E_INVALIDARG;
}

static
HRESULT
APIENTRY
D9ReleaseResourceCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_SYNCTOKEN* Args)
{
    UNREFERENCED_PARAMETER(hDevice);
    UNREFERENCED_PARAMETER(Args);
    return E_INVALIDARG;
}

static
HRESULT
APIENTRY
D9EscapeCb(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDICB_ESCAPE* Args)
{
    D9Device* Device = static_cast<D9Device*>(Args->hDevice);
    D3DKMT_ESCAPE Escape = {};

    Escape.hAdapter = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(hAdapter));
    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = Args->Flags;
    Escape.pPrivateDriverData = Args->pPrivateDriverData;
    Escape.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    if (Device != nullptr)
    {
        Escape.hDevice = Device->KernelDevice();
        if (Device->DriverInterfaceVersion() > 1)
            Escape.hContext = static_cast<D3DKMT_HANDLE>(reinterpret_cast<ULONG_PTR>(Args->hContext));
    }
    return D9HresultFromStatus(RxGetKmtThunks()->Escape(&Escape));
}

/* Overlays, queued blits and native fences have no Direct3D 9 path to the kernel yet */
static
HRESULT
APIENTRY
D9CreateOverlayCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_CREATEOVERLAY* Args)
{
    D9_TRACE_ONCE(L"the driver asked for an overlay, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9UpdateOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_UPDATEOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9FlipOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_FLIPOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9DestroyOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DESTROYOVERLAY* Args)
{
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9OfferAllocations2Cb(
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
D9SubmitPresentBltToHwQueueCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SUBMITPRESENTBLTTOHWQUEUE* Args)
{
    D9_TRACE_ONCE(L"the driver asked to queue a present blit, which is not implemented yet");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
D9CreateNativeFenceCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DKMT_CREATENATIVEFENCE* Args)
{
    D9_TRACE_ONCE(L"the driver asked for a native fence, which is not implemented yet");
    return E_NOTIMPL;
}

static
VOID
D9FillDeviceCallbacks(
    _Out_ D3DDDI_DEVICECALLBACKS* Callbacks)
{
    ZeroMemory(Callbacks, sizeof(*Callbacks));
    Callbacks->pfnAllocateCb = D9AllocateCb;
    Callbacks->pfnDeallocateCb = D9DeallocateCb;
    Callbacks->pfnSetPriorityCb = RxSetPriorityCb;
    Callbacks->pfnQueryResidencyCb = RxQueryResidencyCb;
    Callbacks->pfnSetDisplayModeCb = D9SetDisplayModeCb;
    Callbacks->pfnPresentCb = D9PresentCb;
    Callbacks->pfnRenderCb = RxRenderCb;
    Callbacks->pfnLockCb = RxLockCb;
    Callbacks->pfnUnlockCb = RxUnlockCb;
    Callbacks->pfnEscapeCb = D9EscapeCb;
    Callbacks->pfnCreateOverlayCb = D9CreateOverlayCb;
    Callbacks->pfnUpdateOverlayCb = D9UpdateOverlayCb;
    Callbacks->pfnFlipOverlayCb = D9FlipOverlayCb;
    Callbacks->pfnDestroyOverlayCb = D9DestroyOverlayCb;
    Callbacks->pfnCreateContextCb = RxCreateContextCb;
    Callbacks->pfnDestroyContextCb = RxDestroyContextCb;
    Callbacks->pfnCreateSynchronizationObjectCb = RxCreateSynchronizationObjectCb;
    Callbacks->pfnDestroySynchronizationObjectCb = RxDestroySynchronizationObjectCb;
    Callbacks->pfnWaitForSynchronizationObjectCb = RxWaitForSynchronizationObjectCb;
    Callbacks->pfnSignalSynchronizationObjectCb = RxSignalSynchronizationObjectCb;
    Callbacks->pfnSetAsyncCallbacksCb = RxSetAsyncCallbacksCb;
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
    Callbacks->pfnOfferAllocations2Cb = D9OfferAllocations2Cb;
    Callbacks->pfnReclaimAllocations3Cb = RxReclaimAllocations3Cb;
    Callbacks->pfnAcquireResourceCb = D9AcquireResourceCb;
    Callbacks->pfnReleaseResourceCb = D9ReleaseResourceCb;
    Callbacks->pfnCreateHwQueueCb = RxCreateHwQueueCb;
    Callbacks->pfnDestroyHwQueueCb = RxDestroyHwQueueCb;
    Callbacks->pfnSubmitCommandToHwQueueCb = RxSubmitCommandToHwQueueCb;
    Callbacks->pfnSubmitWaitForSyncObjectsToHwQueueCb = RxSubmitWaitForSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitSignalSyncObjectsToHwQueueCb = RxSubmitSignalSyncObjectsToHwQueueCb;
    Callbacks->pfnSubmitPresentBltToHwQueueCb = D9SubmitPresentBltToHwQueueCb;
    Callbacks->pfnSubmitPresentToHwQueueCb = D9SubmitPresentToHwQueueCb;
    Callbacks->pfnCreateNativeFenceCb = D9CreateNativeFenceCb;
}

/* Threads may call the driver unless vertices go through the runtime or a fullscreen device has no focus */
BOOLEAN
D9DriverMayThread(
    _In_ DWORD BehaviorFlags,
    _In_opt_ HWND FocusWindow,
    _In_ BOOLEAN Windowed)
{
    if ((BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING |
                          D3DCREATE_DISABLE_PSGP_THREADING)) != 0)
    {
        return FALSE;
    }
    return FocusWindow != NULL || Windowed;
}

HRESULT
D9Device::CreateKernelDevice(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEDEVICE Create = {};
    NTSTATUS Status;

    if (Kmt == nullptr || Kmt->CreateDevice == nullptr)
        return D3DERR_NOTAVAILABLE;

    /* Only the plain object's devices run in the kernel's legacy mode */
    Create.hAdapter = Adapter->KernelAdapter;
    Create.Flags.LegacyMode = !IsEx;
    Create.Flags.RequestVSync = (BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) != 0;
    Status = Kmt->CreateDevice(&Create);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTCreateDevice failed 0x%08lX", Status);
        return D9HresultFromStatus(Status);
    }

    DeviceHandle = Create.hDevice;
    return S_OK;
}

/* Each device opens the driver's adapter again for itself, looking the driver up first as the system does */
HRESULT
D9Device::OpenDriverAdapter(VOID)
{
    D3DKMT_UMDFILENAMEINFO Name;
    HRESULT hr;

    if (!D9QueryUmdName(Adapter->KernelAdapter, KMTUMDVERSION_DX9, &Name))
    {
        D9Trace(L"%S lost its Direct3D 9 user mode driver", Adapter->DeviceName);
        return E_FAIL;
    }

    AdapterCallbacks = Adapter->Callbacks;
    hr = D9CallOpenAdapter(Adapter, &AdapterCallbacks, &AdapterFunctions, &DriverAdapter, &RuntimeVersion,
                           &DriverVersion);
    if (FAILED(hr))
        D9Trace(L"OpenAdapter for a new device failed 0x%08lX", hr);
    return hr;
}

/* The kernel facts and capabilities are read again through the device's own driver adapter */
HRESULT
D9Device::ReadDriverAnswers(VOID)
{
    D9Adapter* Answers = D9CopyAdapter(Adapter);
    BOOLEAN Read;

    if (Answers == nullptr)
        return E_OUTOFMEMORY;

    Answers->Functions = AdapterFunctions;
    Answers->DriverAdapter = DriverAdapter;
    Read = D9ReadDriverAnswers(Answers, IsEx);
    D9FreeDriverAnswers(Answers);
    delete Answers;
    return Read ? S_OK : D3DERR_NOTAVAILABLE;
}

/* Interfaces from WDDM 1.2 on drop the old blit, create, flush and present entry points for newer ones */
static
BOOLEAN
D9DriverEntryPointsValid(
    _In_ const D3DDDI_DEVICEFUNCS* Funcs,
    _In_ UINT DriverVersion,
    _In_ BOOLEAN UsesFlush1)
{
    if (DriverVersion < D9NewEntryPointsVersion)
        return TRUE;

    if (Funcs->pfnVolBlt != nullptr || Funcs->pfnVolBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnBufBlt != nullptr || Funcs->pfnBufBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnTexBlt != nullptr || Funcs->pfnTexBlt1 == nullptr)
        return FALSE;
    if (Funcs->pfnCreateResource != nullptr || Funcs->pfnCreateResource2 == nullptr)
        return FALSE;

    if (UsesFlush1)
    {
        if (Funcs->pfnFlush != nullptr || Funcs->pfnFlush1 == nullptr)
            return FALSE;
        if (Funcs->pfnPresent != nullptr || Funcs->pfnPresent1 == nullptr)
            return FALSE;
    }
    return TRUE;
}

HRESULT
D9Device::CreateDriverDevice(
    _In_ BOOLEAN Windowed)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    const D3DDDI_DEVICEFUNCS& Funcs = DeviceFunctions;
    D3DDDIARG_CREATEDEVICE Args = {};
    D3DKMT_CREATECONTEXT Context = {};
    BOOLEAN MayThread;
    NTSTATUS Status;
    HRESULT hr;

    /* Drivers from before the WDDM 1.1 interface get their first command buffer from the runtime */
    if (DriverVersion <= 1)
    {
        Context.hDevice = DeviceHandle;
        Context.ClientHint = D3DKMT_CLIENTHINT_DX9;
        Status = Kmt->CreateContext(&Context);
        if (!NT_SUCCESS(Status))
        {
            D9Trace(L"D3DKMTCreateContext failed 0x%08lX", Status);
            return D9HresultFromStatus(Status);
        }
        ContextHandle = Context.hContext;
        Args.pCommandBuffer = Context.pCommandBuffer;
        Args.CommandBufferSize = Context.CommandBufferSize;
        Args.pAllocationList = Context.pAllocationList;
        Args.AllocationListSize = Context.AllocationListSize;
        Args.pPatchLocationList = Context.pPatchLocationList;
        Args.PatchLocationListSize = Context.PatchLocationListSize;
    }

    D9FillDeviceCallbacks(&DeviceCallbacks);
    MayThread = D9DriverMayThread(BehaviorFlags, FocusWindow, Windowed);
    Args.hDevice = this;
    Args.Interface = D9DeviceInterface;
    Args.Version = RuntimeVersion;
    Args.pCallbacks = &DeviceCallbacks;
    Args.pDeviceFuncs = &DeviceFunctions;
    Args.Flags.AllowMultithreading = MayThread;
    Args.Flags.AllowFlipBatching = MayThread && !IsEx;

    hr = AdapterFunctions.pfnCreateDevice(DriverAdapter, &Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD CreateDevice failed 0x%08lX", hr);
        return hr == D3DERR_DEVICEREMOVED ? E_FAIL : hr;
    }
    DriverDevice = Args.hDevice;

    UsesFlush1 = DriverVersion > D9AlwaysCreateResource2Version && Funcs.pfnFlush1 != nullptr &&
                 Funcs.pfnCheckCounterInfo != nullptr && Funcs.pfnCheckCounter != nullptr &&
                 Funcs.pfnUpdateSubresourceUP != nullptr && Funcs.pfnPresent1 != nullptr &&
                 (DriverVersion <= D9MarkerEntryPointsVersion ||
                  (Funcs.pfnSetMarker != nullptr && Funcs.pfnSetMarkerMode != nullptr));

    if (DriverVersion > D9SyncTokenVersion &&
        (Funcs.pfnAcquireResource == nullptr || Funcs.pfnReleaseResource == nullptr))
    {
        D9Trace(L"the driver lacks the WDDM 2.1 resource synchronization entry points");
        return E_FAIL;
    }
    if (!D9DriverEntryPointsValid(&Funcs, DriverVersion, UsesFlush1))
    {
        D9Trace(L"the driver offers entry points that do not fit interface 0x%X", DriverVersion);
        return E_FAIL;
    }
    return S_OK;
}

VOID
D9Device::DestroyHardware(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_DESTROYCONTEXT Context = {};
    D3DKMT_DESTROYDEVICE Device = {};

    if (DriverDevice != NULL)
    {
        ReleaseWindowSurface();
        DeviceFunctions.pfnDestroyDevice(DriverDevice);
        DriverDevice = NULL;
    }
    if (DriverAdapter != NULL && AdapterFunctions.pfnCloseAdapter != nullptr)
    {
        AdapterFunctions.pfnCloseAdapter(DriverAdapter);
        DriverAdapter = NULL;
    }
    if (ContextHandle != 0)
    {
        Context.hContext = ContextHandle;
        Kmt->DestroyContext(&Context);
        ContextHandle = 0;
    }
    if (DeviceHandle != 0)
    {
        Device.hDevice = DeviceHandle;
        Kmt->DestroyDevice(&Device);
        DeviceHandle = 0;
    }
}

/* Driver flags for a surface: back buffers and render targets are render targets, depth formats are Z buffers */
static
VOID
D9SurfaceFlags(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _Out_ D3DDDI_RESOURCEFLAGS* Flags)
{
    Flags->Value = 0;
    if ((Desc->Usage & D3DUSAGE_RENDERTARGET) != 0)
        Flags->RenderTarget = TRUE;
    if ((Desc->Usage & D3DUSAGE_DEPTHSTENCIL) != 0)
        Flags->ZBuffer = TRUE;
    if (!Lockable)
        Flags->NotLockable = TRUE;
    if (Discard)
        Flags->DiscardRenderTarget = TRUE;
}

/* Hands the driver one resource of a single subresource and keeps what it returns */
HRESULT
D9Device::CreateDriverResource(
    _Inout_ D9Resource* Resource,
    _In_ const D3DDDI_SURFACEINFO* Surface,
    _In_ D3DDDIMULTISAMPLE_TYPE MultisampleType,
    _In_ UINT MultisampleQuality)
{
    D3DDDIARG_CREATERESOURCE2 Create2 = {};
    D3DDDIARG_CREATERESOURCE Create = {};
    HRESULT hr;

    if (DeviceFunctions.pfnCreateResource2 != nullptr)
    {
        Create2.Format = Resource->Format;
        Create2.Pool = Resource->Pool;
        Create2.MultisampleType = MultisampleType;
        Create2.MultisampleQuality = MultisampleQuality;
        Create2.pSurfList = Surface;
        Create2.SurfCount = 1;
        Create2.VidPnSourceId = Adapter->SourceId;
        Create2.hResource = Resource;
        Create2.Flags = Resource->Flags;
        Create2.Rotation = D3DDDI_ROTATION_IDENTITY;
        hr = DeviceFunctions.pfnCreateResource2(DriverDevice, &Create2);
        Resource->DriverResource = Create2.hResource;
    }
    else
    {
        Create.Format = Resource->Format;
        Create.Pool = Resource->Pool;
        Create.MultisampleType = MultisampleType;
        Create.MultisampleQuality = MultisampleQuality;
        Create.pSurfList = Surface;
        Create.SurfCount = 1;
        Create.VidPnSourceId = Adapter->SourceId;
        Create.hResource = Resource;
        Create.Flags = Resource->Flags;
        Create.Rotation = D3DDDI_ROTATION_IDENTITY;
        hr = DeviceFunctions.pfnCreateResource(DriverDevice, &Create);
        Resource->DriverResource = Create.hResource;
    }

    if (FAILED(hr))
    {
        D9Trace(L"UMD CreateResource failed 0x%08lX for format %u", hr, Resource->Format);
        Resource->DriverResource = NULL;
    }
    return hr;
}

/* A linear vertex or index buffer of the runtime's own */
HRESULT
D9Device::CreateDriverBuffer(
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Size,
    _In_ UINT Flags,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.Value = Flags;
    Resource->Width = Size;
    Resource->Height = 1;

    Surface.Width = Size;
    Surface.Height = 1;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

HRESULT
D9Device::CreateDriverSurface(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    /* Depth buffers may live in any video memory; render targets stay local */
    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = static_cast<D3DDDIFORMAT>(Desc->Format);
    Resource->Pool = (Desc->Usage & D3DUSAGE_DEPTHSTENCIL) != 0 ? D3DDDIPOOL_VIDEOMEMORY : D3DDDIPOOL_LOCALVIDMEM;
    Resource->Width = Desc->Width;
    Resource->Height = Desc->Height;
    D9SurfaceFlags(Desc, Lockable, Discard, &Resource->Flags);

    Surface.Width = Desc->Width;
    Surface.Height = Desc->Height;
    return CreateDriverResource(Resource, &Surface, static_cast<D3DDDIMULTISAMPLE_TYPE>(Desc->MultiSampleType),
                                Desc->MultiSampleQuality);
}

VOID
D9Device::DestroyDriverSurface(
    _Inout_ D9Resource* Resource)
{
    if (Resource->DriverResource == NULL)
        return;

    DeviceFunctions.pfnDestroyResource(DriverDevice, Resource->DriverResource);
    Resource->DriverResource = NULL;
}

HRESULT
D9Device::LockDriverSurface(
    _In_ D9Resource* Resource,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags,
    _Out_ D3DLOCKED_RECT* Locked)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    Args.Flags.ReadOnly = (Flags & D3DLOCK_READONLY) != 0;
    Args.Flags.Discard = (Flags & D3DLOCK_DISCARD) != 0;
    Args.Flags.DoNotWait = (Flags & D3DLOCK_DONOTWAIT) != 0;
    Args.Flags.NoOverwrite = (Flags & D3DLOCK_NOOVERWRITE) != 0;
    if (Rect != nullptr)
    {
        Args.Flags.AreaValid = TRUE;
        Args.Area = *Rect;
    }

    hr = DeviceFunctions.pfnLock(DriverDevice, &Args);
    if (FAILED(hr))
    {
        if (hr != D3DERR_WASSTILLDRAWING)
            D9Trace(L"UMD Lock failed 0x%08lX", hr);
        return hr;
    }

    Locked->pBits = Args.pSurfData;
    Locked->Pitch = static_cast<INT>(Args.Pitch);
    return S_OK;
}

HRESULT
D9Device::UnlockDriverSurface(
    _In_ D9Resource* Resource)
{
    D3DDDIARG_UNLOCK Unlock = {};

    Unlock.hResource = Resource->DriverResource;
    return DeviceFunctions.pfnUnlock(DriverDevice, &Unlock);
}
