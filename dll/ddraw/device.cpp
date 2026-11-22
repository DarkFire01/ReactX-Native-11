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
static constexpr UINT DdCreateResource2Version = 0x3002;
static constexpr UINT DdAlwaysCreateResource2Version = 0x4000;
static constexpr UINT DdMarkErrorVersion = 0x5002;

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

/* Overlays have no DirectDraw path to the kernel yet */
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

    DdReleaseWindowSurface(Hardware);
    Hardware->DeviceFunctions.pfnDestroyDevice(Hardware->DriverDevice);
    Hardware->DriverDevice = NULL;
    ZeroMemory(&Hardware->DeviceFunctions, sizeof(Hardware->DeviceFunctions));
}

/* Marks the device as lost once; drivers from WDDM 1.3 on are told through the kernel */
VOID
DdMarkDeviceLost(
    _Inout_ DdHardware* Hardware)
{
    static PFND3DKMT_MARKDEVICEASERROR MarkAsError = reinterpret_cast<PFND3DKMT_MARKDEVICEASERROR>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTMarkDeviceAsError"));
    D3DKMT_MARKDEVICEASERROR Mark = {};

    if (Hardware->Lost)
        return;

    Hardware->Lost = TRUE;
    DdState.DeviceLost = TRUE;
    DdTrace(L"the display device was lost");
    if (Hardware->DriverVersion >= DdMarkErrorVersion && MarkAsError != nullptr)
    {
        Mark.hDevice = Hardware->DeviceHandle;
        Mark.Reason = D3DKMT_DEVICE_ERROR_REASON_GENERIC;
        MarkAsError(&Mark);
    }
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

static
D3DDDIFORMAT
DdFormatFromMasks(
    _In_ const DDPIXELFORMAT* Format)
{
    DWORD Red = Format->dwRBitMask;
    DWORD Green = Format->dwGBitMask;
    DWORD Blue = Format->dwBBitMask;
    DWORD Alpha = (Format->dwFlags & DDPF_ALPHAPIXELS) != 0 ? Format->dwRGBAlphaBitMask : 0;

    switch (Format->dwRGBBitCount)
    {
    case 32:
        if (Red == 0xFF0000 && Green == 0xFF00 && Blue == 0xFF)
            return Alpha == 0xFF000000 ? D3DDDIFMT_A8R8G8B8 : D3DDDIFMT_X8R8G8B8;
        if (Red == 0xFF && Green == 0xFF00 && Blue == 0xFF0000)
            return Alpha == 0xFF000000 ? D3DDDIFMT_A8B8G8R8 : D3DDDIFMT_X8B8G8R8;
        if (Red == 0x3FF00000 && Green == 0xFFC00 && Blue == 0x3FF)
            return D3DDDIFMT_A2R10G10B10;
        if (Red == 0x3FF && Green == 0xFFC00 && Blue == 0x3FF00000)
            return D3DDDIFMT_A2B10G10R10;
        if (Red == 0xFFFF && Green == 0xFFFF0000 && Blue == 0)
            return D3DDDIFMT_G16R16;
        break;
    case 24:
        if (Red == 0xFF0000 && Green == 0xFF00 && Blue == 0xFF)
            return D3DDDIFMT_R8G8B8;
        break;
    case 16:
        if (Red == 0xF800 && Green == 0x7E0 && Blue == 0x1F)
            return D3DDDIFMT_R5G6B5;
        if (Red == 0x7C00 && Green == 0x3E0 && Blue == 0x1F)
            return Alpha == 0x8000 ? D3DDDIFMT_A1R5G5B5 : D3DDDIFMT_X1R5G5B5;
        if (Red == 0xF00 && Green == 0xF0 && Blue == 0xF)
            return Alpha == 0xF000 ? D3DDDIFMT_A4R4G4B4 : D3DDDIFMT_X4R4G4B4;
        if (Red == 0xE0 && Green == 0x1C && Blue == 0x3 && Alpha == 0xFF00)
            return D3DDDIFMT_A8R3G3B2;
        break;
    case 8:
        if (Red == 0xE0 && Green == 0x1C && Blue == 0x3)
            return D3DDDIFMT_R3G3B2;
        break;
    }
    return D3DDDIFMT_UNKNOWN;
}

/* The Direct3D format for a DirectDraw pixel format; unknown when the driver has no such format */
/* Z buffers with stencil, told apart by where the depth and stencil bits sit */
static
D3DDDIFORMAT
DdStencilFormat(
    _In_ const DDPIXELFORMAT* Format)
{
    DWORD Mask = Format->dwZBitMask;
    DWORD Stencil = Format->dwStencilBitMask;
    DWORD Bits = Format->dwStencilBitDepth;

    if (Format->dwZBufferBitDepth == 32)
    {
        if (Mask == 0xFFFFFF00 && Stencil == 0xFF && Bits == 8)
            return D3DDDIFMT_D24S8;
        if (Mask == 0x00FFFFFF && Stencil == 0xFF000000 && Bits == 8)
            return D3DDDIFMT_S8D24;
    }
    else if (Format->dwZBufferBitDepth == 16 && Bits == 1)
    {
        if (Mask == 0xFFFE && Stencil == 0x1)
            return D3DDDIFMT_D15S1;
        if (Mask == 0x7FFF && Stencil == 0x8000)
            return D3DDDIFMT_S1D15;
    }
    return D3DDDIFMT_UNKNOWN;
}

/* Z buffers by depth and the bits the depth occupies */
static
D3DDDIFORMAT
DdDepthFormat(
    _In_ const DDPIXELFORMAT* Format)
{
    DWORD Mask = Format->dwZBitMask;

    if ((Format->dwFlags & DDPF_STENCILBUFFER) != 0)
        return DdStencilFormat(Format);

    switch (Format->dwZBufferBitDepth)
    {
    case 15:
    case 16:
        if (Mask == 0xFFFF || Mask == 0)
            return D3DDDIFMT_D16;
        if (Mask == 0xFFFE)
            return D3DDDIFMT_D15S1;
        if (Mask == 0x7FFF)
            return D3DDDIFMT_S1D15;
        return D3DDDIFMT_UNKNOWN;
    case 24:
    case 32:
        if (Mask == 0xFFFFFFFF)
            return D3DDDIFMT_D32;
        if (Mask == 0x00FFFFFF)
            return D3DDDIFMT_X8D24;
        if (Mask == 0xFFFFFF00)
            return D3DDDIFMT_D24X8;
        return D3DDDIFMT_UNKNOWN;
    default:
        return D3DDDIFMT_UNKNOWN;
    }
}

D3DDDIFORMAT
DdDriverFormat(
    _In_ const DDPIXELFORMAT* Format)
{
    if ((Format->dwFlags & DDPF_FOURCC) != 0)
        return static_cast<D3DDDIFORMAT>(Format->dwFourCC);
    if ((Format->dwFlags & DDPF_PALETTEINDEXED8) != 0)
        return D3DDDIFMT_P8;
    if ((Format->dwFlags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4)) != 0)
        return D3DDDIFMT_UNKNOWN;
    if ((Format->dwFlags & DDPF_RGB) != 0)
        return DdFormatFromMasks(Format);
    if ((Format->dwFlags & DDPF_ALPHA) != 0 && Format->dwAlphaBitDepth == 8)
        return D3DDDIFMT_A8;
    if ((Format->dwFlags & DDPF_ZBUFFER) != 0)
        return DdDepthFormat(Format);
    return D3DDDIFMT_UNKNOWN;
}

/* Whether the driver lists the surface's format for what the caps ask of it */
BOOLEAN
DdDriverCanCreate(
    _In_ DdHardware* Hardware,
    _In_ const DDSCAPS2* Caps,
    _In_ const DDPIXELFORMAT* Format)
{
    D3DDDIFORMAT Wanted = DdDriverFormat(Format);
    DWORD Needs = 0;
    UINT Index;

    if ((Caps->dwCaps2 & DDSCAPS2_CUBEMAP) != 0)
        Needs = FORMATOP_CUBETEXTURE;
    else if ((Caps->dwCaps & DDSCAPS_TEXTURE) != 0)
        Needs = FORMATOP_TEXTURE;

    if ((Caps->dwCaps & DDSCAPS_ZBUFFER) != 0)
        Needs |= FORMATOP_ZSTENCIL;
    else if ((Caps->dwCaps & DDSCAPS_3DDEVICE) != 0)
        Needs |= FORMATOP_OFFSCREEN_RENDERTARGET;
    else if ((Caps->dwCaps & DDSCAPS_OVERLAY) != 0)
        Needs |= FORMATOP_OVERLAY;

    if ((Caps->dwCaps & DDSCAPS_OFFSCREENPLAIN) != 0 && Needs == 0)
        Needs = FORMATOP_OFFSCREENPLAIN;

    if (!DdHardwareQueryFormats(Hardware))
        return FALSE;
    for (Index = 0; Index < Hardware->FormatCount; Index++)
    {
        if (Hardware->Formats[Index].Format == Wanted && (Needs & ~Hardware->Formats[Index].Operations) == 0)
            return TRUE;
    }
    return FALSE;
}

static
D3DDDI_POOL
DdDriverPool(
    _In_ DWORD Caps)
{
    if ((Caps & DDSCAPS_SYSTEMMEMORY) != 0)
        return D3DDDIPOOL_SYSTEMMEM;
    if ((Caps & DDSCAPS_LOCALVIDMEM) != 0)
        return D3DDDIPOOL_LOCALVIDMEM;
    if ((Caps & DDSCAPS_NONLOCALVIDMEM) != 0)
        return D3DDDIPOOL_NONLOCALVIDMEM;
    return D3DDDIPOOL_VIDEOMEMORY;
}

static
D3DDDI_RESOURCEFLAGS
DdDriverFlags(
    _In_ const DDSCAPS2* Caps)
{
    D3DDDI_RESOURCEFLAGS Flags = {};

    if ((Caps->dwCaps & DDSCAPS_ZBUFFER) != 0)
        Flags.ZBuffer = TRUE;
    else if ((Caps->dwCaps & DDSCAPS_3DDEVICE) != 0)
        Flags.RenderTarget = TRUE;
    if ((Caps->dwCaps2 & DDSCAPS2_HINTDYNAMIC) != 0)
        Flags.Dynamic = TRUE;
    if ((Caps->dwCaps2 & DDSCAPS2_HINTSTATIC) != 0)
        Flags.HintStatic = TRUE;
    if ((Caps->dwCaps & DDSCAPS_WRITEONLY) != 0)
        Flags.WriteOnly = TRUE;
    if ((Caps->dwCaps & DDSCAPS_OVERLAY) != 0)
        Flags.Overlay = TRUE;

    if ((Caps->dwCaps2 & DDSCAPS2_CUBEMAP) != 0)
    {
        Flags.Texture = TRUE;
        Flags.CubeMap = TRUE;
    }
    else if ((Caps->dwCaps & DDSCAPS_TEXTURE) != 0)
    {
        Flags.Texture = TRUE;
    }
    return Flags;
}

/* Bytes the runtime charges against the adapter's memory: whole rows rounded to a DWORD */
static
ULONGLONG
DdChargeFor(
    _In_ const DdSurface* Surface)
{
    ULONGLONG Row = ((static_cast<ULONGLONG>(Surface->Width) * Surface->Format.dwRGBBitCount + 7) / 8 + 3) & ~3ull;

    return Row * Surface->Height;
}

/* Asks the driver for a resource of one level; the newer entry point when the interface has it */
static
HRESULT
DdDriverCreateResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdResource* Resource,
    _In_ const D3DDDI_SURFACEINFO* Level)
{
    const D3DDDI_DEVICEFUNCS& Funcs = Hardware->DeviceFunctions;
    D3DDDIARG_CREATERESOURCE2 Create = {};
    HRESULT hr;

    Create.Format = Resource->Format;
    Create.Pool = Resource->Pool;
    Create.pSurfList = Level;
    Create.SurfCount = Resource->SurfaceCount;
    Create.MipLevels = Resource->MipLevels;
    Create.hResource = Resource;
    Create.Flags = Resource->Flags;
    Create.Rotation = D3DDDI_ROTATION_IDENTITY;

    if (Hardware->DriverVersion > DdAlwaysCreateResource2Version ||
        (Funcs.pfnCreateResource2 != nullptr && Hardware->DriverVersion >= DdCreateResource2Version))
    {
        hr = Funcs.pfnCreateResource2(Hardware->DriverDevice, &Create);
    }
    else
    {
        hr = Funcs.pfnCreateResource(Hardware->DriverDevice, reinterpret_cast<D3DDDIARG_CREATERESOURCE*>(&Create));
    }

    if (SUCCEEDED(hr))
        Resource->DriverResource = Create.hResource;
    return hr;
}

HRESULT
DdCreateSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    D3DDDI_SURFACEINFO Level = {};
    DdResource* Resource;
    ULONGLONG Charge;
    BOOLEAN MayUseShared;
    BOOLEAN Shared;
    HRESULT hr;

    Resource = new (std::nothrow) DdResource;
    if (Resource == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Resource, sizeof(*Resource));

    Resource->Format = DdDriverFormat(&Surface->Format);
    Resource->Pool = DdDriverPool(Surface->Caps.dwCaps);
    Resource->Flags = DdDriverFlags(&Surface->Caps);
    Resource->SurfaceCount = 1;
    Resource->MipLevels = (Surface->Caps.dwCaps & DDSCAPS_TEXTURE) != 0 ? 1 : 0;

    if (Resource->Format == D3DDDIFMT_UNKNOWN)
    {
        delete Resource;
        return E_NOTIMPL;
    }

    /* Local memory first; plain video memory may spill into the shared pool */
    Charge = DdChargeFor(Surface);
    MayUseShared = Resource->Pool == D3DDDIPOOL_NONLOCALVIDMEM ||
                   (Resource->Pool == D3DDDIPOOL_VIDEOMEMORY && !Resource->Flags.RenderTarget &&
                    !Resource->Flags.ZBuffer && !Resource->Flags.DecodeRenderTarget &&
                    !Resource->Flags.VideoProcessRenderTarget && !Resource->Flags.Overlay);
    if (Resource->Pool != D3DDDIPOOL_NONLOCALVIDMEM && Hardware->LocalMemoryFree > Charge)
    {
        Shared = FALSE;
    }
    else if (MayUseShared && Hardware->NonLocalMemoryFree > Charge)
    {
        Shared = TRUE;
    }
    else
    {
        delete Resource;
        return DDERR_OUTOFVIDEOMEMORY;
    }

    Level.Width = Surface->Width;
    Level.Height = Surface->Height;

    hr = DdDriverCreateResource(Hardware, Resource, &Level);
    if (FAILED(hr))
    {
        DdTrace(L"UMD CreateResource failed 0x%08lX for a %lux%lu surface of format %u", hr, Surface->Width,
                Surface->Height, Resource->Format);
        delete Resource;
        return hr;
    }

    if (Resource->DriverResource != NULL)
    {
        Resource->Charged = static_cast<UINT>(Charge);
        Resource->ChargedShared = Shared;
        if (Shared)
            Hardware->NonLocalMemoryFree -= Charge;
        else
            Hardware->LocalMemoryFree -= Charge;
    }

    Surface->Resource = Resource;
    Surface->Pitch = 0;
    return S_OK;
}

VOID
DdDestroySurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    DdResource* Resource = Surface->Resource;

    if (Resource == nullptr)
        return;

    while (Resource->LockCount != 0)
        DdUnlockSurfaceResource(Hardware, Surface);

    if (Resource->DriverResource != NULL)
        Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Resource->DriverResource);

    if (Resource->ChargedShared)
        Hardware->NonLocalMemoryFree += Resource->Charged;
    else
        Hardware->LocalMemoryFree += Resource->Charged;

    delete Resource;
    Surface->Resource = nullptr;
}

HRESULT
DdLockSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface,
    _In_opt_ const RECT* Rect,
    _In_ DWORD Flags,
    _Outptr_ BYTE** Bits)
{
    DdResource* Resource = Surface->Resource;
    D3DDDIARG_LOCK Lock = {};
    HRESULT hr;

    *Bits = nullptr;
    Lock.hResource = Resource->DriverResource;
    if (Rect != nullptr)
    {
        Lock.Area = *Rect;
        Lock.Flags.AreaValid = TRUE;
    }
    if ((Flags & DDLOCK_READONLY) != 0)
        Lock.Flags.ReadOnly = TRUE;
    else if ((Flags & DDLOCK_WRITEONLY) != 0)
        Lock.Flags.WriteOnly = TRUE;
    if ((Flags & DDLOCK_NOOVERWRITE) != 0)
        Lock.Flags.NoOverwrite = TRUE;
    if ((Flags & DDLOCK_DISCARDCONTENTS) != 0)
        Lock.Flags.Discard = TRUE;

    hr = Hardware->DeviceFunctions.pfnLock(Hardware->DriverDevice, &Lock);
    if (FAILED(hr))
    {
        DdTrace(L"UMD Lock failed 0x%08lX", hr);
        return hr;
    }

    Surface->Pitch = static_cast<LONG>(Lock.Pitch);
    Resource->LockCount += 1;
    *Bits = static_cast<BYTE*>(Lock.pSurfData);
    return S_OK;
}

HRESULT
DdUnlockSurfaceResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    DdResource* Resource = Surface->Resource;
    D3DDDIARG_UNLOCK Unlock = {};
    HRESULT hr;

    if (Resource->LockCount == 0)
        return S_OK;

    Unlock.hResource = Resource->DriverResource;
    hr = Hardware->DeviceFunctions.pfnUnlock(Hardware->DriverDevice, &Unlock);
    Resource->LockCount -= 1;
    if (FAILED(hr))
        DdTrace(L"UMD Unlock failed 0x%08lX", hr);
    return hr;
}

/* A short lived video memory resource the runtime copies through; nothing is charged for it */
HRESULT
DdCreateStagingResource(
    _In_ DdHardware* Hardware,
    _In_ D3DDDIFORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _Out_ DdResource* Resource)
{
    D3DDDI_SURFACEINFO Level = {};
    HRESULT hr;

    ZeroMemory(Resource, sizeof(*Resource));
    Resource->Format = Format;
    Resource->Pool = D3DDDIPOOL_VIDEOMEMORY;
    Resource->SurfaceCount = 1;
    Level.Width = Width;
    Level.Height = Height;

    hr = DdDriverCreateResource(Hardware, Resource, &Level);
    if (FAILED(hr))
    {
        DdTrace(L"UMD CreateResource failed 0x%08lX for a %ux%u staging copy", hr, Width, Height);
        Resource->DriverResource = NULL;
    }
    return hr;
}

/* A driver resource over a system memory surface's own pixels, made the first time the driver must touch them */
HRESULT
DdCreateSystemResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    D3DDDI_SURFACEINFO Level = {};
    DdResource* Resource;
    HRESULT hr;

    if (Surface->SystemResource != nullptr)
        return S_OK;
    if (!DdDriverCanCreate(Hardware, &Surface->Caps, &Surface->Format))
        return E_FAIL;

    Resource = new (std::nothrow) DdResource;
    if (Resource == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Resource, sizeof(*Resource));

    Resource->Format = DdDriverFormat(&Surface->Format);
    Resource->Pool = D3DDDIPOOL_SYSTEMMEM;
    Resource->Flags = DdDriverFlags(&Surface->Caps);
    Resource->SurfaceCount = 1;

    Level.Width = Surface->Width;
    Level.Height = Surface->Height;
    Level.pSysMem = Surface->Bits;
    Level.SysMemPitch = static_cast<UINT>(Surface->Pitch);

    hr = DdDriverCreateResource(Hardware, Resource, &Level);
    if (FAILED(hr) || Resource->DriverResource == NULL)
    {
        DdTrace(L"UMD CreateResource failed 0x%08lX over a %lux%lu system memory surface", hr, Surface->Width,
                Surface->Height);
        delete Resource;
        return E_FAIL;
    }

    Surface->SystemResource = Resource;
    return S_OK;
}

VOID
DdDestroySystemResource(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    DdResource* Resource = Surface->SystemResource;

    if (Resource == nullptr)
        return;

    DdWaitForDriver(Hardware, Surface);
    Hardware->DeviceFunctions.pfnDestroyResource(Hardware->DriverDevice, Resource->DriverResource);
    delete Resource;
    Surface->SystemResource = nullptr;
}

/* A driver lock waits for the copies queued on a system memory surface; nothing else does */
VOID
DdWaitForDriver(
    _In_ DdHardware* Hardware,
    _Inout_ DdSurface* Surface)
{
    D3DDDIARG_UNLOCK Unlock = {};
    D3DDDIARG_LOCK Lock = {};

    if (!Surface->DriverPending || Surface->SystemResource == nullptr)
        return;

    Surface->DriverPending = FALSE;
    Lock.hResource = Surface->SystemResource->DriverResource;
    if (FAILED(Hardware->DeviceFunctions.pfnLock(Hardware->DriverDevice, &Lock)))
        return;
    Unlock.hResource = Lock.hResource;
    Hardware->DeviceFunctions.pfnUnlock(Hardware->DriverDevice, &Unlock);
}
