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
        Record.SurfaceCount = Resource->SurfaceCount != 0 ? Resource->SurfaceCount : 1;
        Record.MipLevels = Resource->MipLevels != 0 ? Resource->MipLevels : 1;
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

    /* Only the plain object's devices run in the kernel's legacy mode, unless their swap effect was upgraded */
    Create.hAdapter = Adapter->KernelAdapter;
    Create.Flags.LegacyMode = !IsEx && !SwapEffectUpgrade;
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

/* Hands the driver one resource and keeps what it returns; textures name their levels and faces */
HRESULT
D9Device::CreateDriverResource(
    _Inout_ D9Resource* Resource,
    _In_ const D3DDDI_SURFACEINFO* Surface,
    _In_ D3DDDIMULTISAMPLE_TYPE MultisampleType,
    _In_ UINT MultisampleQuality)
{
    UINT Count = Resource->SurfaceCount != 0 ? Resource->SurfaceCount : 1;

    D3DDDIARG_CREATERESOURCE2 Create2 = {};
    D3DDDIARG_CREATERESOURCE Create = {};
    HRESULT hr;

    if (DeviceFunctions.pfnCreateResource2 != nullptr)
    {
        Create2.Format = Resource->Format;
        Create2.Pool = Resource->Pool;
        Create2.Fvf = Resource->Fvf;
        Create2.MultisampleType = MultisampleType;
        Create2.MultisampleQuality = MultisampleQuality;
        Create2.MipLevels = Resource->MipLevels;
        Create2.pSurfList = Surface;
        Create2.SurfCount = Count;
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
        Create.Fvf = Resource->Fvf;
        Create.MultisampleType = MultisampleType;
        Create.MultisampleQuality = MultisampleQuality;
        Create.MipLevels = Resource->MipLevels;
        Create.pSurfList = Surface;
        Create.SurfCount = Count;
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

/* A vertex or index buffer the application made, in video memory or in memory of the runtime's own */
HRESULT
D9Device::CreateBufferResource(
    _In_ D3DFORMAT Format,
    _In_ UINT Length,
    _In_ UINT Flags,
    _In_ DWORD Fvf,
    _In_ D3DDDI_POOL Placement,
    _In_opt_ VOID* Memory,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = static_cast<D3DDDIFORMAT>(Format);
    Resource->Pool = Placement;
    Resource->Flags.Value = Flags;
    Resource->Width = Length;
    Resource->Height = 1;
    Resource->Fvf = Fvf;

    Surface.Width = Length;
    Surface.Height = 1;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = Memory != nullptr ? Length : 0;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

/* System memory surfaces hand the driver the memory the runtime keeps their pixels in */
HRESULT
D9Device::CreateDriverSurface(
    _In_ const D3DSURFACE_DESC* Desc,
    _In_ BOOLEAN Lockable,
    _In_ BOOLEAN Discard,
    _In_ D3DDDI_POOL DriverPool,
    _In_opt_ VOID* Memory,
    _In_ UINT MemoryPitch,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = static_cast<D3DDDIFORMAT>(D9DriverDepthFormat(Adapter, Desc->Format));
    Resource->Pool = DriverPool;
    Resource->Width = Desc->Width;
    Resource->Height = Desc->Height;
    D9SurfaceFlags(Desc, Lockable, Discard, &Resource->Flags);

    Surface.Width = Desc->Width;
    Surface.Height = Desc->Height;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = MemoryPitch;
    return CreateDriverResource(Resource, &Surface, static_cast<D3DDDIMULTISAMPLE_TYPE>(Desc->MultiSampleType),
                                Desc->MultiSampleQuality);
}

VOID
D9Device::DestroyDriverSurface(
    _Inout_ D9Resource* Resource)
{
    if (Resource->DriverResource == NULL)
        return;

    DrainBatch();
    DeviceFunctions.pfnDestroyResource(DriverDevice, Resource->DriverResource);
    Resource->DriverResource = NULL;
}

/* Copies a call into the batch; FALSE leaves it to the caller to make */
static
BOOLEAN
D9RecordCall(
    _In_opt_ D9Batch* Batch,
    _In_ D9BatchCall Call,
    _In_reads_bytes_(Size) const VOID* Args,
    _In_ UINT Size)
{
    VOID* Slot;

    if (Batch == nullptr)
        return FALSE;
    Slot = Batch->Record(Call, Size);
    if (Slot == nullptr)
        return FALSE;
    CopyMemory(Slot, Args, Size);
    return TRUE;
}

/* The lock flags the driver sees for an application lock */
static
VOID
D9DriverLockFlags(
    _In_ const D9Resource* Resource,
    _In_ DWORD Flags,
    _Out_ D3DDDI_LOCKFLAGS* Driver)
{
    /* A read only lock never discards; system memory locks only tell the driver */
    Driver->Value = 0;
    Driver->ReadOnly = (Flags & D3DLOCK_READONLY) != 0;
    Driver->Discard = !Driver->ReadOnly && (Flags & D3DLOCK_DISCARD) != 0;
    Driver->DoNotWait = (Flags & D3DLOCK_DONOTWAIT) != 0;
    Driver->NoOverwrite = (Flags & D3DLOCK_NOOVERWRITE) != 0;
    Driver->NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
}

/* A batching device locks dynamic and system memory buffers asynchronously when the lock discards, does not
   overwrite or only tells the driver; a discard renames the buffer in the batch */
HRESULT
D9Device::LockAsyncBuffer(
    _In_ D9Resource* Resource,
    _In_ const D3DDDIARG_LOCK* Request,
    _In_ UINT LockCount,
    _Outptr_ VOID** Data)
{
    D3DDDIARG_LOCKASYNC Args = {};
    D3DDDIARG_RENAME Rename = {};
    HRESULT hr;

    if (!Request->Flags.NotifyOnly && LockCount == 0 && Request->Flags.Discard == Request->Flags.NoOverwrite)
        return E_FAIL;

    Args.hResource = Request->hResource;
    Args.Flags.NoOverwrite = Request->Flags.NoOverwrite;
    Args.Flags.Discard = Request->Flags.Discard && !Request->Flags.NotifyOnly;
    Args.Flags.NotifyOnly = Request->Flags.NotifyOnly;
    if (Request->Flags.RangeValid)
    {
        Args.Flags.RangeValid = TRUE;
        Args.Range = Request->Range;
    }

    /* Notifying the driver of a buffer the batch still uses waits for the batch */
    if (!Request->Flags.NotifyOnly || Request->Flags.NoOverwrite)
    {
        Args.Flags.NoExistingReferences = !Batch->IsReferenced(Request->hResource);
    }
    else
    {
        if (Batch->IsReferenced(Request->hResource))
            Batch->Drain();
        Args.Flags.NoExistingReferences = TRUE;
    }

    hr = DeviceFunctions.pfnLockAsync(DriverDevice, &Args);
    if (FAILED(hr))
        return hr;
    *Data = Args.pSurfData;

    if (Args.Flags.Discard && LockCount == 0)
    {
        Rename.hResource = Request->hResource;
        Rename.hCookie = Args.hCookie;
        if (!D9RecordCall(Batch, D9BatchCall::Rename, &Rename, sizeof(Rename)))
            DeviceFunctions.pfnRename(DriverDevice, &Rename);
        Batch->Reference(Request->hResource);
    }
    return S_OK;
}

HRESULT
D9Device::LockDriverBuffer(
    _In_ D9Resource* Resource,
    _In_ UINT Offset,
    _In_ UINT Size,
    _In_ BOOLEAN Ranged,
    _In_ DWORD Flags,
    _In_ UINT LockCount,
    _Inout_ BOOLEAN* Asynchronous,
    _Outptr_ VOID** Data)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (Ranged)
    {
        Args.Flags.RangeValid = TRUE;
        Args.Range.Offset = Offset;
        Args.Range.Size = Size;
    }

    if (*Asynchronous && Batch != nullptr)
    {
        hr = LockAsyncBuffer(Resource, &Args, LockCount, Data);
        if (SUCCEEDED(hr))
            return S_OK;
    }
    *Asynchronous = FALSE;

    DrainBatch();
    hr = DeviceFunctions.pfnLock(DriverDevice, &Args);
    if (FAILED(hr))
    {
        if (hr != D3DERR_WASSTILLDRAWING)
            D9Trace(L"UMD Lock of a buffer failed 0x%08lX", hr);
        return hr;
    }
    *Data = Args.pSurfData;
    return S_OK;
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
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (Rect != nullptr)
    {
        Args.Flags.AreaValid = TRUE;
        Args.Area = *Rect;
    }

    DrainBatch();
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
D9Device::UnlockDriverBuffer(
    _In_ D9Resource* Resource,
    _In_ BOOLEAN Asynchronous)
{
    D3DDDIARG_UNLOCKASYNC Unlock = {};

    if (!Asynchronous)
        return UnlockDriverSurface(Resource);

    Unlock.hResource = Resource->DriverResource;
    Unlock.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
    return DeviceFunctions.pfnUnlockAsync(DriverDevice, &Unlock);
}

HRESULT
D9Device::UnlockDriverSurface(
    _In_ D9Resource* Resource)
{
    D3DDDIARG_UNLOCK Unlock = {};

    Unlock.hResource = Resource->DriverResource;
    Unlock.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
    DrainBatch();
    return DeviceFunctions.pfnUnlock(DriverDevice, &Unlock);
}

HRESULT
D9Device::CreateTextureResource(
    _In_reads_(Count) const D3DDDI_SURFACEINFO* Surfaces,
    _In_ UINT Count,
    _In_ UINT MipLevels,
    _Inout_ D9Resource* Resource)
{
    Resource->SurfaceCount = Count;
    Resource->MipLevels = MipLevels;
    Resource->Width = Surfaces[0].Width;
    Resource->Height = Surfaces[0].Height;
    return CreateDriverResource(Resource, Surfaces, D3DDDIMULTISAMPLE_NONE, 0);
}

/* System memory locks only tell the driver, and always name the first subresource */
HRESULT
D9Device::LockDriverSubresource(
    _In_ D9Resource* Resource,
    _In_ UINT Index,
    _In_opt_ const D3DBOX* Box,
    _In_ BOOLEAN IsVolume,
    _In_ DWORD Flags,
    _Out_ D3DLOCKED_BOX* Locked)
{
    D3DDDIARG_LOCK Args = {};
    HRESULT hr;

    Args.hResource = Resource->DriverResource;
    D9DriverLockFlags(Resource, Flags, &Args.Flags);
    if (!Args.Flags.NotifyOnly)
    {
        Args.SubResourceIndex = Index;
        if (Box != nullptr && IsVolume)
        {
            Args.Flags.BoxValid = TRUE;
            Args.Box.Left = Box->Left;
            Args.Box.Top = Box->Top;
            Args.Box.Right = Box->Right;
            Args.Box.Bottom = Box->Bottom;
            Args.Box.Front = Box->Front;
            Args.Box.Back = Box->Back;
        }
        else if (Box != nullptr)
        {
            Args.Flags.AreaValid = TRUE;
            Args.Area.left = static_cast<LONG>(Box->Left);
            Args.Area.top = static_cast<LONG>(Box->Top);
            Args.Area.right = static_cast<LONG>(Box->Right);
            Args.Area.bottom = static_cast<LONG>(Box->Bottom);
        }
    }

    DrainBatch();
    hr = DeviceFunctions.pfnLock(DriverDevice, &Args);
    if (FAILED(hr))
    {
        if (hr != D3DERR_WASSTILLDRAWING)
            D9Trace(L"UMD Lock of a texture failed 0x%08lX", hr);
        return hr;
    }
    Locked->pBits = Args.pSurfData;
    Locked->RowPitch = static_cast<INT>(Args.Pitch);
    Locked->SlicePitch = static_cast<INT>(Args.SlicePitch);
    return S_OK;
}

HRESULT
D9Device::UnlockDriverSubresource(
    _In_ D9Resource* Resource,
    _In_ UINT Index)
{
    D3DDDIARG_UNLOCK Unlock = {};

    Unlock.hResource = Resource->DriverResource;
    Unlock.Flags.NotifyOnly = Resource->Pool == D3DDDIPOOL_SYSTEMMEM;
    if (!Unlock.Flags.NotifyOnly)
        Unlock.SubResourceIndex = Index;
    DrainBatch();
    return DeviceFunctions.pfnUnlock(DriverDevice, &Unlock);
}

/* Copies a region of one face's top level, which the driver repeats down the levels */
VOID
D9Device::DriverTexBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ UINT Face,
    _In_ const D3DBOX* Box)
{
    D3DDDIARG_TEXBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.CubeMapFace = Face;
    Blt.DstPoint.x = static_cast<LONG>(Box->Left);
    Blt.DstPoint.y = static_cast<LONG>(Box->Top);
    Blt.SrcRect.left = static_cast<LONG>(Box->Left);
    Blt.SrcRect.top = static_cast<LONG>(Box->Top);
    Blt.SrcRect.right = static_cast<LONG>(Box->Right);
    Blt.SrcRect.bottom = static_cast<LONG>(Box->Bottom);

    DrainBatch();
    if (DeviceFunctions.pfnTexBlt1 != nullptr)
    {
        hr = DeviceFunctions.pfnTexBlt1(DriverDevice, &Blt);
    }
    else
    {
        D3DDDIARG_TEXBLT Old = {};

        Old.hDstResource = Blt.hDstResource;
        Old.hSrcResource = Blt.hSrcResource;
        Old.CubeMapFace = Blt.CubeMapFace;
        Old.DstPoint = Blt.DstPoint;
        Old.SrcRect = Blt.SrcRect;
        hr = DeviceFunctions.pfnTexBlt(DriverDevice, &Old);
    }
    if (FAILED(hr))
        D9Trace(L"UMD TexBlt failed 0x%08lX", hr);
}

VOID
D9Device::DriverVolBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ const D3DBOX* Box)
{
    D3DDDIARG_VOLUMEBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.DstX = Box->Left;
    Blt.DstY = Box->Top;
    Blt.DstZ = Box->Front;
    Blt.SrcBox.Left = Box->Left;
    Blt.SrcBox.Top = Box->Top;
    Blt.SrcBox.Right = Box->Right;
    Blt.SrcBox.Bottom = Box->Bottom;
    Blt.SrcBox.Front = Box->Front;
    Blt.SrcBox.Back = Box->Back;

    DrainBatch();
    if (DeviceFunctions.pfnVolBlt1 != nullptr)
    {
        hr = DeviceFunctions.pfnVolBlt1(DriverDevice, &Blt);
    }
    else
    {
        D3DDDIARG_VOLUMEBLT Old = {};

        Old.hDstResource = Blt.hDstResource;
        Old.hSrcResource = Blt.hSrcResource;
        Old.DstX = Blt.DstX;
        Old.DstY = Blt.DstY;
        Old.DstZ = Blt.DstZ;
        Old.SrcBox = Blt.SrcBox;
        hr = DeviceFunctions.pfnVolBlt(DriverDevice, &Old);
    }
    if (FAILED(hr))
        D9Trace(L"UMD VolBlt failed 0x%08lX", hr);
}

/* Copies a byte range of a managed buffer's system memory copy into its video memory copy */
VOID
D9Device::DriverBufBlt(
    _In_ D9Resource* Destination,
    _In_ D9Resource* Source,
    _In_ UINT Offset,
    _In_ UINT Size)
{
    D3DDDIARG_BUFFERBLT1 Blt = {};
    HRESULT hr;

    Blt.hDstResource = Destination->DriverResource;
    Blt.hSrcResource = Source->DriverResource;
    Blt.Offset = Offset;
    Blt.SrcRange.Offset = Offset;
    Blt.SrcRange.Size = Size;

    DrainBatch();
    if (DeviceFunctions.pfnBufBlt1 != nullptr)
    {
        hr = DeviceFunctions.pfnBufBlt1(DriverDevice, &Blt);
    }
    else
    {
        D3DDDIARG_BUFFERBLT Old = {};

        Old.hDstResource = Blt.hDstResource;
        Old.hSrcResource = Blt.hSrcResource;
        Old.Offset = Blt.Offset;
        Old.SrcRange = Blt.SrcRange;
        hr = DeviceFunctions.pfnBufBlt(DriverDevice, &Old);
    }
    if (FAILED(hr))
        D9Trace(L"UMD BufBlt failed 0x%08lX", hr);
}

VOID
D9Device::DriverGenerateMips(
    _In_ D9Resource* Resource,
    _In_ D3DTEXTUREFILTERTYPE Filter)
{
    D3DDDIARG_GENERATEMIPSUBLEVELS Args = {};

    Args.hResource = Resource->DriverResource;
    Args.Filter = static_cast<D3DDDITEXTUREFILTERTYPE>(Filter);
    DrainBatch();
    DeviceFunctions.pfnGenerateMipSubLevels(DriverDevice, &Args);
}

/* The managed pool may be spelled the extended object's way */
static
BOOLEAN
D9IsManagedPool(
    _In_ D3DPOOL Pool)
{
    return Pool == D3DPOOL_MANAGED || Pool == static_cast<D3DPOOL>(6);
}

DWORD
D9Device::ChangePriority(
    _In_opt_ D9Resource* Resource,
    _In_ D3DPOOL Pool,
    _Inout_ DWORD* Stored,
    _In_ DWORD Priority)
{
    DWORD Previous = *Stored;

    if (D9IsManagedPool(Pool))
    {
        *Stored = Priority;
        return Previous;
    }
    if (!IsEx || Pool != D3DPOOL_DEFAULT)
        return 0;

    *Stored = Priority;
    if (Resource != nullptr && Resource->DriverResource != NULL)
        DriverSetPriority(Resource, Priority);
    return Previous;
}

DWORD
D9Device::ReadPriority(
    _In_ D3DPOOL Pool,
    _In_ DWORD Stored) const
{
    if (D9IsManagedPool(Pool) || (IsEx && Pool == D3DPOOL_DEFAULT))
        return Stored;
    return 0;
}

VOID
D9Device::DriverSetPriority(
    _In_ D9Resource* Resource,
    _In_ DWORD Priority)
{
    D3DDDIARG_SETPRIORITY Args = {};

    Args.hResource = Resource->DriverResource;
    Args.Priority = Priority;
    DrainBatch();
    DeviceFunctions.pfnSetPriority(DriverDevice, &Args);
}

/* A runtime owned system memory buffer the driver reads from */
HRESULT
D9Device::CreateSystemBuffer(
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Size,
    _In_ UINT Flags,
    _In_ VOID* Memory,
    _Out_ D9Resource* Resource)
{
    D3DDDI_SURFACEINFO Surface = {};

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_SYSTEMMEM;
    Resource->Flags.Value = Flags;
    Resource->Width = Size;
    Resource->Height = 1;

    Surface.Width = Size;
    Surface.Height = 1;
    Surface.pSysMem = Memory;
    Surface.SysMemPitch = Size;
    return CreateDriverResource(Resource, &Surface, D3DDDIMULTISAMPLE_NONE, 0);
}

/* Whether the machine has more than one processor core */
static
BOOLEAN
D9HasSeveralCores(VOID)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION* Entries;
    DWORD Size = 0;
    UINT Cores = 0;

    if (GetLogicalProcessorInformation(nullptr, &Size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;

    Entries = static_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION*>(HeapAlloc(GetProcessHeap(), 0, Size));
    if (Entries == nullptr)
        return FALSE;
    if (GetLogicalProcessorInformation(Entries, &Size))
    {
        for (DWORD Index = 0; Index < Size / sizeof(*Entries) && Cores < 2; Index++)
        {
            if (Entries[Index].Relationship == RelationProcessorCore)
                Cores += 1;
        }
    }
    HeapFree(GetProcessHeap(), 0, Entries);
    return Cores > 1;
}

/* Drivers that offer asynchronous locks on a hardware device leave threading to the runtime */
BOOLEAN
D9Device::WantsBatch(VOID) const
{
    static LONG SeveralCores = -1;

    if (DeviceType != D3DDEVTYPE_HAL || (BehaviorFlags & D3DCREATE_DISABLE_PSGP_THREADING) != 0)
        return FALSE;
    if (DeviceFunctions.pfnLockAsync == nullptr || DeviceFunctions.pfnUnlockAsync == nullptr ||
        DeviceFunctions.pfnRename == nullptr)
    {
        return FALSE;
    }
    if (SeveralCores < 0)
        InterlockedExchange(&SeveralCores, D9HasSeveralCores() ? 1 : 0);
    return SeveralCores == 1;
}

VOID
D9Device::StartBatch(VOID)
{
    HRESULT hr;

    if (Batch != nullptr || !WantsBatch())
        return;

    Batch = new (std::nothrow) D9Batch(this, &DeviceFunctions, DriverDevice);
    if (Batch == nullptr)
        return;
    hr = Batch->Start();
    if (FAILED(hr))
    {
        D9Trace(L"driver calls run on the calling thread; the batch could not start (0x%08lX)", hr);
        StopBatch();
    }
}

VOID
D9Device::StopBatch(VOID)
{
    D9Batch* Stopping = Batch;

    if (Stopping == nullptr)
        return;
    Batch = nullptr;
    Stopping->Stop();
    delete Stopping;
}

VOID
D9Device::DrainBatch(VOID)
{
    if (Batch != nullptr)
        Batch->Drain();
}

VOID
D9Device::DdiRenderState(
    _In_ const D3DDDIARG_RENDERSTATE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::RenderState, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetRenderState(DriverDevice, Args);
}

VOID
D9Device::DdiTextureStageState(
    _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::TextureStageState, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetTextureStageState(DriverDevice, Args);
}

VOID
D9Device::DdiPixelShaderConst(
    _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
    _In_reads_(Args->Count * 4) const FLOAT* Data)
{
    UINT DataSize = Args->Count * 4 * sizeof(*Data);
    BYTE* Slot = Batch != nullptr ? static_cast<BYTE*>(Batch->Record(D9BatchCall::PixelShaderConst,
                                                                      sizeof(*Args) + DataSize))
                                  : nullptr;

    if (Slot == nullptr)
    {
        DeviceFunctions.pfnSetPixelShaderConst(DriverDevice, Args, Data);
        return;
    }
    CopyMemory(Slot, Args, sizeof(*Args));
    CopyMemory(Slot + sizeof(*Args), Data, DataSize);
}

VOID
D9Device::DdiViewport(
    _In_ const D3DDDIARG_VIEWPORTINFO* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::Viewport, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetViewport(DriverDevice, Args);
}

VOID
D9Device::DdiZRange(
    _In_ const D3DDDIARG_ZRANGE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::ZRange, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetZRange(DriverDevice, Args);
}

VOID
D9Device::DdiScissorRect(
    _In_ const RECT* Rect)
{
    if (!D9RecordCall(Batch, D9BatchCall::ScissorRect, Rect, sizeof(*Rect)))
        DeviceFunctions.pfnSetScissorRect(DriverDevice, Rect);
}

VOID
D9Device::DdiClipPlane(
    _In_ const D3DDDIARG_SETCLIPPLANE* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::ClipPlane, Args, sizeof(*Args)))
        DeviceFunctions.pfnSetClipPlane(DriverDevice, Args);
}

VOID
D9Device::DdiWInfo(
    _In_ const D3DDDIARG_WINFO* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::WInfo, Args, sizeof(*Args)))
        DeviceFunctions.pfnUpdateWInfo(DriverDevice, Args);
}

VOID
D9Device::DdiRenderTarget(
    _In_ const D3DDDIARG_SETRENDERTARGET* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::RenderTarget, Args, sizeof(*Args)))
    {
        DeviceFunctions.pfnSetRenderTarget(DriverDevice, Args);
        return;
    }
    Batch->NoteBound(Args->RenderTargetIndex, Args->hRenderTarget);
}

VOID
D9Device::DdiDepthStencil(
    _In_ const D3DDDIARG_SETDEPTHSTENCIL* Args)
{
    if (!D9RecordCall(Batch, D9BatchCall::DepthStencil, Args, sizeof(*Args)))
    {
        DeviceFunctions.pfnSetDepthStencil(DriverDevice, Args);
        return;
    }
    Batch->NoteBound(D9MaxRenderTargets, Args->hZBuffer);
}

/* A recorded clear keeps its rectangle count and then the rectangles after the arguments */
HRESULT
D9Device::DdiClear(
    _In_ const D3DDDIARG_CLEAR* Args,
    _In_ UINT Count,
    _In_reads_(Count) const RECT* Rects)
{
    UINT Size = sizeof(*Args) + 2 * sizeof(UINT) + Count * sizeof(*Rects);
    BYTE* Slot = Batch != nullptr ? static_cast<BYTE*>(Batch->Record(D9BatchCall::Clear, Size)) : nullptr;
    UINT* Kept;

    if (Slot == nullptr)
        return DeviceFunctions.pfnClear(DriverDevice, Args, Count, Rects);

    CopyMemory(Slot, Args, sizeof(*Args));
    Kept = reinterpret_cast<UINT*>(Slot + sizeof(*Args));
    Kept[0] = Count;
    Kept[1] = 0;
    CopyMemory(Kept + 2, Rects, Count * sizeof(*Rects));
    return S_OK;
}

/* A batch records the copy and remembers both resources; a driver failure fails the call */
HRESULT
D9Device::DriverBlt(
    _In_ const D3DDDIARG_BLT* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::Blt, Args, sizeof(*Args)))
    {
        Batch->Reference(Args->hSrcResource);
        Batch->Reference(Args->hDstResource);
        return S_OK;
    }
    hr = DeviceFunctions.pfnBlt(DriverDevice, Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD Blt failed 0x%08lX", hr);
        return E_FAIL;
    }
    return S_OK;
}

HRESULT
D9Device::DriverColorFill(
    _In_ const D3DDDIARG_COLORFILL* Args)
{
    HRESULT hr;

    if (D9RecordCall(Batch, D9BatchCall::ColorFill, Args, sizeof(*Args)))
    {
        Batch->Reference(Args->hResource);
        return S_OK;
    }
    hr = DeviceFunctions.pfnColorFill(DriverDevice, Args);
    if (FAILED(hr))
    {
        D9Trace(L"UMD ColorFill failed 0x%08lX", hr);
        return E_FAIL;
    }
    return S_OK;
}

/* Hands a single source present to the worker; FALSE leaves it to the caller */
BOOLEAN
D9Device::DdiPresent1(
    _In_ const D3DDDIARG_PRESENT1* Args)
{
    BYTE* Slot;

    if (Batch == nullptr || Args->SrcResources != 1)
        return FALSE;

    Slot = static_cast<BYTE*>(Batch->Record(D9BatchCall::Present1,
                                            sizeof(*Args) + sizeof(D3DDDIARG_PRESENTSURFACE)));
    if (Slot == nullptr)
        return FALSE;
    CopyMemory(Slot, Args, sizeof(*Args));
    CopyMemory(Slot + sizeof(*Args), Args->phSrcResources, sizeof(D3DDDIARG_PRESENTSURFACE));
    Batch->Submit();
    return TRUE;
}
