/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runtime callbacks handed to the UMD at device creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "calltrap.hpp"
#include "ddicallbacks.hpp"
#include "device.hpp"

#include <intrin.h>

/*
 * The UMD gets kernel context handles back as its HANDLE hContext values, so they
 * pass through without a lookup. Device and resource handles are runtime objects.
 */

static
RxDevice&
RxDeviceFromHandle(
    _In_ HANDLE Handle)
{
    return *static_cast<RxDevice*>(Handle);
}

static
D3DKMT_HANDLE
RxKmContext(
    _In_opt_ HANDLE Context)
{
    return (D3DKMT_HANDLE)(ULONG_PTR)Context;
}

static
HRESULT
APIENTRY
RxAllocateCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_ALLOCATE* Args)
{
    RxDevice& Device = RxDeviceFromHandle(hDevice);
    const KmtThunks* Kmt = RxGetKmtThunks();
    RxRuntimeResource* Resource = static_cast<RxRuntimeResource*>(Args->hResource);
    D3DKMT_CREATEALLOCATION Create = {};
    NTSTATUS Status;

    Create.hDevice = Device.GetKmDevice();
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.NumAllocations = Args->NumAllocations;
    Create.pAllocationInfo = Args->pAllocationInfo;
    Create.hPrivateRuntimeResourceHandle = Args->hResource;

    /* Allocations for a runtime resource hang off one kernel resource */
    if (Resource != nullptr)
    {
        if (Resource->KmResource == 0)
            Create.Flags.CreateResource = TRUE;
        else
            Create.hResource = Resource->KmResource;
    }

    /* Shared textures carry their record; flip model buffers also say what they are */
    if (Resource != nullptr && Resource->SharedRecord != nullptr && Create.Flags.CreateResource)
    {
        Create.Flags.CreateShared = TRUE;
        if (Resource->ShareKind == RxShareKind::SwapChain)
        {
            Create.Flags.NonSecure = TRUE;
            Create.Flags.RestrictSharedAccess = TRUE;
            Create.Flags.SwapChainBackBuffer = TRUE;
        }
        if (Resource->ShareKind != RxShareKind::GlobalHandle)
            Create.Flags.NtSecuritySharing = TRUE;
        Create.pPrivateRuntimeData = Resource->SharedRecord;
        Create.PrivateRuntimeDataSize = Resource->SharedRecordSize;
    }

    if (Kmt->CreateAllocation2 != nullptr)
        Status = Kmt->CreateAllocation2(&Create);
    else
        Status = Kmt->CreateAllocation(&Create);

    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateAllocation failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    if (Resource != nullptr && Create.Flags.CreateResource)
    {
        Resource->KmResource = Create.hResource;
        Resource->GlobalShare = Create.hGlobalShare;
    }

    Args->hKMResource = Create.hResource;
    return S_OK;
}

static
HRESULT
APIENTRY
RxDeallocateCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_DEALLOCATE* Args)
{
    RxDevice& Device = RxDeviceFromHandle(hDevice);
    RxRuntimeResource* Resource = static_cast<RxRuntimeResource*>(Args->hResource);
    D3DKMT_DESTROYALLOCATION Destroy = {};
    NTSTATUS Status;

    Destroy.hDevice = Device.GetKmDevice();
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

    Status = RxGetKmtThunks()->DestroyAllocation(&Destroy);
    if (NT_SUCCESS(Status) && Resource != nullptr)
        Device.ReleaseRuntimeResource(Resource);

    return RxHresultFromStatus(Status);
}

static
HRESULT
APIENTRY
RxEscapeCb(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDICB_ESCAPE* Args)
{
    RxDevice* Device = static_cast<RxDevice*>(Args->hDevice);
    D3DKMT_ESCAPE Escape = {};

    /* The first argument is the runtime adapter; the device inside the arguments is optional */
    if (Device != nullptr)
    {
        Escape.hAdapter = Device->GetAdapter().GetKmtHandle();
        Escape.hDevice = Device->GetKmDevice();
    }
    else
    {
        Escape.hAdapter = static_cast<UmdAdapter*>(hAdapter)->GetKmtHandle();
    }

    Escape.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
    Escape.Flags = Args->Flags;
    Escape.pPrivateDriverData = Args->pPrivateDriverData;
    Escape.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Escape.hContext = RxKmContext(Args->hContext);

    return RxHresultFromStatus(RxGetKmtThunks()->Escape(&Escape));
}

/* Scans a primary out; the device has to own the source first */
static
HRESULT
APIENTRY
RxSetDisplayModeCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_SETDISPLAYMODE* Args)
{
    RxDevice& Device = RxDeviceFromHandle(hDevice);
    D3DKMT_SETDISPLAYMODE Mode = {};
    NTSTATUS Status;

    if (RxGetKmtThunks()->SetDisplayMode == nullptr)
        return E_NOTIMPL;

    Mode.hDevice = Device.GetKmDevice();
    Mode.hPrimaryAllocation = Args->hPrimaryAllocation;
    Mode.ScanLineOrdering = D3DDDI_VSSLO_PROGRESSIVE;
    Mode.DisplayOrientation = D3DDDI_ROTATION_IDENTITY;
    Status = RxGetKmtThunks()->SetDisplayMode(&Mode);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTSetDisplayMode failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Args->PrivateDriverFormatAttribute = Mode.PrivateDriverFormatAttribute;
    return S_OK;
}

static
HRESULT
APIENTRY
RxPresentCb(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDICB_PRESENT* Args)
{
    RX_TRACE(L"PresentCb not implemented");
    return E_NOTIMPL;
}

static
HRESULT
APIENTRY
RxPresentMultiPlaneOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const D3DDDICB_PRESENTMULTIPLANEOVERLAY* Args)
{
    return E_NOTIMPL;
}

static const D3DDDI_DEVICECALLBACKS ImplementedKernelCallbacks =
{
    .pfnAllocateCb = RxAllocateCb,
    .pfnDeallocateCb = RxDeallocateCb,
    .pfnSetPriorityCb = RxSetPriorityCb,
    .pfnQueryResidencyCb = RxQueryResidencyCb,
    .pfnSetDisplayModeCb = RxSetDisplayModeCb,
    .pfnPresentCb = RxPresentCb,
    .pfnRenderCb = RxRenderCb,
    .pfnLockCb = RxLockCb,
    .pfnUnlockCb = RxUnlockCb,
    .pfnEscapeCb = RxEscapeCb,
    .pfnCreateContextCb = RxCreateContextCb,
    .pfnDestroyContextCb = RxDestroyContextCb,
    .pfnCreateSynchronizationObjectCb = RxCreateSynchronizationObjectCb,
    .pfnDestroySynchronizationObjectCb = RxDestroySynchronizationObjectCb,
    .pfnWaitForSynchronizationObjectCb = RxWaitForSynchronizationObjectCb,
    .pfnSignalSynchronizationObjectCb = RxSignalSynchronizationObjectCb,
    .pfnSetAsyncCallbacksCb = RxSetAsyncCallbacksCb,
    .pfnSetDisplayPrivateDriverFormatCb = RxSetDisplayPrivateDriverFormatCb,
    .pfnOfferAllocationsCb = RxOfferAllocationsCb,
    .pfnReclaimAllocationsCb = RxReclaimAllocationsCb,
    .pfnCreateSynchronizationObject2Cb = RxCreateSynchronizationObject2Cb,
    .pfnWaitForSynchronizationObject2Cb = RxWaitForSynchronizationObject2Cb,
    .pfnSignalSynchronizationObject2Cb = RxSignalSynchronizationObject2Cb,
    .pfnPresentMultiPlaneOverlayCb = RxPresentMultiPlaneOverlayCb,
    .pfnLogUMDMarkerCb = RxLogUmdMarkerCb,
    .pfnMakeResidentCb = RxMakeResidentCb,
    .pfnEvictCb = RxEvictCb,
    .pfnWaitForSynchronizationObjectFromCpuCb = RxWaitForSynchronizationObjectFromCpuCb,
    .pfnSignalSynchronizationObjectFromCpuCb = RxSignalSynchronizationObjectFromCpuCb,
    .pfnWaitForSynchronizationObjectFromGpuCb = RxWaitForSynchronizationObjectFromGpuCb,
    .pfnSignalSynchronizationObjectFromGpuCb = RxSignalSynchronizationObjectFromGpuCb,
    .pfnCreatePagingQueueCb = RxCreatePagingQueueCb,
    .pfnDestroyPagingQueueCb = RxDestroyPagingQueueCb,
    .pfnLock2Cb = RxLock2Cb,
    .pfnUnlock2Cb = RxUnlock2Cb,
    .pfnInvalidateCacheCb = RxInvalidateCacheCb,
    .pfnReserveGpuVirtualAddressCb = RxReserveGpuVirtualAddressCb,
    .pfnMapGpuVirtualAddressCb = RxMapGpuVirtualAddressCb,
    .pfnFreeGpuVirtualAddressCb = RxFreeGpuVirtualAddressCb,
    .pfnUpdateGpuVirtualAddressCb = RxUpdateGpuVirtualAddressCb,
    .pfnCreateContextVirtualCb = RxCreateContextVirtualCb,
    .pfnSubmitCommandCb = RxSubmitCommandCb,
    .pfnDeallocate2Cb = RxDeallocate2Cb,
    .pfnSignalSynchronizationObjectFromGpu2Cb = RxSignalSynchronizationObjectFromGpu2Cb,
    .pfnReclaimAllocations2Cb = RxReclaimAllocations2Cb,
    .pfnGetResourcePresentPrivateDriverDataCb = RxGetResourcePresentPrivateDriverDataCb,
    .pfnUpdateAllocationPropertyCb = RxUpdateAllocationPropertyCb,
    .pfnReclaimAllocations3Cb = RxReclaimAllocations3Cb,
    .pfnCreateHwContextCb = RxCreateHwContextCb,
    .pfnDestroyHwContextCb = RxDestroyHwContextCb,
    .pfnCreateHwQueueCb = RxCreateHwQueueCb,
    .pfnDestroyHwQueueCb = RxDestroyHwQueueCb,
    .pfnSubmitCommandToHwQueueCb = RxSubmitCommandToHwQueueCb,
    .pfnSubmitWaitForSyncObjectsToHwQueueCb = RxSubmitWaitForSyncObjectsToHwQueueCb,
    .pfnSubmitSignalSyncObjectsToHwQueueCb = RxSubmitSignalSyncObjectsToHwQueueCb,
};

static
VOID
APIENTRY
RxSetErrorCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ HRESULT Error)
{
    HMODULE Caller = NULL;
    WCHAR Name[MAX_PATH] = L"?";

    /* Busy answers to queries and maps are routine, not worth a line each */
    if (Error == DXGI_DDI_ERR_WASSTILLDRAWING)
    {
        static_cast<RxDevice*>(CoreLayer.handle)->SetDriverError(Error);
        return;
    }

    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(_ReturnAddress()), &Caller);
    if (Caller != NULL)
        GetModuleFileNameW(Caller, Name, _countof(Name));
    RX_TRACE(L"SetErrorCb 0x%08lX from %s+0x%IX", Error, wcsrchr(Name, L'\\') ? wcsrchr(Name, L'\\') + 1 : Name,
             (ULONG_PTR)_ReturnAddress() - (ULONG_PTR)Caller);
    static_cast<RxDevice*>(CoreLayer.handle)->SetDriverError(Error);
}

/*
 * The UMD calls these when it wants pipeline state sent again, for example after it
 * used the pipeline for its own work. The answer is whatever is bound right now.
 */
static
RxImmediateContext&
RxContextOf(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    return static_cast<RxDevice*>(CoreLayer.handle)->GetContext();
}

template <RxStage Stage>
static
VOID
APIENTRY
RxStateShaderCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendShader(Stage);
}

template <RxStage Stage>
static
VOID
APIENTRY
RxStateConstantBuffersCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ UINT Base,
    _In_ UINT Count)
{
    RxContextOf(CoreLayer).SendConstantBuffers(Stage, Base, Count);
}

template <RxStage Stage>
static
VOID
APIENTRY
RxStateShaderResourcesCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ UINT Base,
    _In_ UINT Count)
{
    RxContextOf(CoreLayer).SendShaderResources(Stage, Base, Count);
}

template <RxStage Stage>
static
VOID
APIENTRY
RxStateSamplersCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ UINT Base,
    _In_ UINT Count)
{
    RxContextOf(CoreLayer).SendSamplers(Stage, Base, Count);
}

static
VOID
APIENTRY
RxStateInputLayoutCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendInputLayout();
}

static
VOID
APIENTRY
RxStateVertexBuffersCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ UINT Base,
    _In_ UINT Count)
{
    RxContextOf(CoreLayer).SendVertexBuffers(Base, Count);
}

static
VOID
APIENTRY
RxStateIndexBufferCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendIndexBuffer();
}

static
VOID
APIENTRY
RxStateTopologyCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendTopology();
}

static
VOID
APIENTRY
RxStateRenderTargetsCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendRenderTargets(0);
}

static
VOID
APIENTRY
RxStateBlendCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendBlendState();
}

static
VOID
APIENTRY
RxStateDepthStencilCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendDepthStencilState();
}

static
VOID
APIENTRY
RxStateRasterizerCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendRasterizerState();
}

static
VOID
APIENTRY
RxStateViewportsCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendViewports(0);
}

static
VOID
APIENTRY
RxStateScissorRectsCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendScissorRects(0);
}

static
VOID
APIENTRY
RxStateStreamTargetsCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    RxContextOf(CoreLayer).SendStreamTargets(0);
}

static
VOID
APIENTRY
RxStateComputeUavsCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer,
    _In_ UINT Base,
    _In_ UINT Count)
{
    RxContextOf(CoreLayer).SendComputeUavs(Base, Count);
}

/* State the runtime keeps nothing of, so there is nothing to send again */
static
VOID
APIENTRY
RxStateNothingCb(
    _In_ D3D10DDI_HRTCORELAYER CoreLayer)
{
    UNREFERENCED_PARAMETER(CoreLayer);
}

/* WDDM 2.0 drivers make their contexts through the core layer; the kernel side is the same */
static
HRESULT
APIENTRY
RxCreateContextCb2_0(
    _In_ D3D10DDI_HRTCORELAYER hDevice,
    _Inout_ D3DWDDM2_0DDICB_CREATECONTEXT* Args)
{
    D3DDDICB_CREATECONTEXT Create = {};
    HRESULT hr;

    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags = Args->Flags;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    hr = RxCreateContextCb(hDevice.handle, &Create);
    if (FAILED(hr))
        return hr;

    Args->hContext = Create.hContext;
    Args->pCommandBuffer = Create.pCommandBuffer;
    Args->CommandBufferSize = Create.CommandBufferSize;
    Args->pAllocationList = Create.pAllocationList;
    Args->AllocationListSize = Create.AllocationListSize;
    Args->pPatchLocationList = Create.pPatchLocationList;
    Args->PatchLocationListSize = Create.PatchLocationListSize;
    return S_OK;
}

static
HRESULT
APIENTRY
RxCreateContextVirtualCb2_0(
    _In_ D3D10DDI_HRTCORELAYER hDevice,
    _Inout_ D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL* Args)
{
    D3DDDICB_CREATECONTEXTVIRTUAL Create = {};
    HRESULT hr;

    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags = Args->Flags;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;

    hr = RxCreateContextVirtualCb(hDevice.handle, &Create);
    if (SUCCEEDED(hr))
        Args->hContext = Create.hContext;
    return hr;
}

static const D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS CoreLayerCallbacks =
{
    .pfnSetErrorCb = RxSetErrorCb,
    .pfnStateVsConstBufCb = RxStateConstantBuffersCb<RxStage::Vertex>,
    .pfnStatePsSrvCb = RxStateShaderResourcesCb<RxStage::Pixel>,
    .pfnStatePsShaderCb = RxStateShaderCb<RxStage::Pixel>,
    .pfnStatePsSamplerCb = RxStateSamplersCb<RxStage::Pixel>,
    .pfnStateVsShaderCb = RxStateShaderCb<RxStage::Vertex>,
    .pfnStatePsConstBufCb = RxStateConstantBuffersCb<RxStage::Pixel>,
    .pfnStateIaInputLayoutCb = RxStateInputLayoutCb,
    .pfnStateIaVertexBufCb = RxStateVertexBuffersCb,
    .pfnStateIaIndexBufCb = RxStateIndexBufferCb,
    .pfnStateGsConstBufCb = RxStateConstantBuffersCb<RxStage::Geometry>,
    .pfnStateGsShaderCb = RxStateShaderCb<RxStage::Geometry>,
    .pfnStateIaPrimitiveTopologyCb = RxStateTopologyCb,
    .pfnStateVsSrvCb = RxStateShaderResourcesCb<RxStage::Vertex>,
    .pfnStateVsSamplerCb = RxStateSamplersCb<RxStage::Vertex>,
    .pfnStateGsSrvCb = RxStateShaderResourcesCb<RxStage::Geometry>,
    .pfnStateGsSamplerCb = RxStateSamplersCb<RxStage::Geometry>,
    .pfnStateOmRenderTargetsCb = RxStateRenderTargetsCb,
    .pfnStateOmBlendStateCb = RxStateBlendCb,
    .pfnStateOmDepthStateCb = RxStateDepthStencilCb,
    .pfnStateRsRastStateCb = RxStateRasterizerCb,
    .pfnStateSoTargetsCb = RxStateStreamTargetsCb,
    .pfnStateRsViewportsCb = RxStateViewportsCb,
    .pfnStateRsScissorCb = RxStateScissorRectsCb,
    .pfnDisableDeferredStagingResourceDestruction = RxStateNothingCb,
    .pfnStateTextFilterSizeCb = RxStateNothingCb,
    .pfnStateHsSrvCb = RxStateShaderResourcesCb<RxStage::Hull>,
    .pfnStateHsShaderCb = RxStateShaderCb<RxStage::Hull>,
    .pfnStateHsSamplerCb = RxStateSamplersCb<RxStage::Hull>,
    .pfnStateHsConstBufCb = RxStateConstantBuffersCb<RxStage::Hull>,
    .pfnStateDsSrvCb = RxStateShaderResourcesCb<RxStage::Domain>,
    .pfnStateDsShaderCb = RxStateShaderCb<RxStage::Domain>,
    .pfnStateDsSamplerCb = RxStateSamplersCb<RxStage::Domain>,
    .pfnStateDsConstBufCb = RxStateConstantBuffersCb<RxStage::Domain>,
    .pfnPerformAmortizedProcessingCb = RxStateNothingCb,
    .pfnStateCsSrvCb = RxStateShaderResourcesCb<RxStage::Compute>,
    .pfnStateCsUavCb = RxStateComputeUavsCb,
    .pfnStateCsShaderCb = RxStateShaderCb<RxStage::Compute>,
    .pfnStateCsSamplerCb = RxStateSamplersCb<RxStage::Compute>,
    .pfnStateCsConstBufCb = RxStateConstantBuffersCb<RxStage::Compute>,
    .pfnCreateContextCb = RxCreateContextCb2_0,
    .pfnCreateContextVirtualCb = RxCreateContextVirtualCb2_0,
};

static
HRESULT
APIENTRY
RxDxgiPresentCb(
    _In_ HANDLE hDevice,
    _In_ DXGIDDICB_PRESENT* Args)
{
    RxPresentContext* Context = static_cast<RxPresentContext*>(Args->pDXGIContext);
    D3DKMT_PRESENT Present;
    ULONG Index;

    UNREFERENCED_PARAMETER(hDevice);

    if (Context == nullptr || RxGetKmtThunks()->Present == nullptr)
        return E_INVALIDARG;
    if (Args->BroadcastContextCount > D3DDDI_MAX_BROADCAST_CONTEXT)
        return E_INVALIDARG;

    /*
     * The swapchain filled in everything but the allocations and the contexts.
     * Fields past BroadcastContext only exist for WDDM 2.0 UMDs and are left alone.
     */
    Present = Context->Template;
    Present.pSrcSubRects = &Context->SourceRect;
    Present.hContext = RxKmContext(Args->hContext);
    Present.hSource = Args->hSrcAllocation;
    Present.hDestination = Args->hDstAllocation;
    Present.BroadcastContextCount = Args->BroadcastContextCount;
    for (Index = 0; Index < Args->BroadcastContextCount; Index++)
        Present.BroadcastContext[Index] = RxKmContext(Args->BroadcastContext[Index]);

    Context->KernelStatus = RxGetKmtThunks()->Present(&Present);
    if (!NT_SUCCESS(Context->KernelStatus))
        RX_TRACE(L"D3DKMTPresent failed 0x%08lX (flags %08X source %u)", Context->KernelStatus, Present.Flags.Value,
                 Present.VidPnSourceId);
    return RxHresultFromStatus(Context->KernelStatus);
}

/* With hardware scheduling the UMD queues the present itself; queue 0 carries the main allocations */
static
HRESULT
APIENTRY
RxDxgiSubmitPresentToHwQueueCb(
    _In_ HANDLE hDevice,
    _In_ DXGIDDICB_SUBMITPRESENTTOHWQUEUE* Args)
{
    RxPresentContext* Context = static_cast<RxPresentContext*>(Args->pDXGIContext);
    D3DKMT_SUBMITPRESENTTOHWQUEUE Submit = {};
    D3DKMT_HANDLE Queues[D3DDDI_MAX_BROADCAST_CONTEXT + 1];
    D3DKMT_PRESENT& Present = Submit.PrivatePresentData;
    NTSTATUS Status;
    HRESULT hr;
    UINT Index;

    if (Context == nullptr || RxGetKmtThunks()->SubmitPresentToHwQueue == nullptr)
        return E_INVALIDARG;
    if (Args->BroadcastHwQueueCount == 0 || Args->BroadcastHwQueueCount > _countof(Queues))
        return (HRESULT)STATUS_INVALID_PARAMETER;

    for (Index = 0; Index < Args->BroadcastHwQueueCount; Index++)
        Queues[Index] = (D3DKMT_HANDLE)(ULONG_PTR)Args->hHwQueues[Index];

    Present = Context->Template;
    Present.pSrcSubRects = &Context->SourceRect;
    Present.hDevice = RxDeviceFromHandle(hDevice).GetKmDevice();
    Present.BroadcastContextCount = Args->BroadcastHwQueueCount - 1;
    Present.hSource = Args->BroadcastSrcAllocations[0];
    if (Args->BroadcastHwQueueCount > 1)
        Present.BroadcastSrcAllocation = &Args->BroadcastSrcAllocations[1];
    if (Args->BroadcastDstAllocations != nullptr)
    {
        Present.hDestination = Args->BroadcastDstAllocations[0];
        if (Args->BroadcastHwQueueCount > 1)
            Present.BroadcastDstAllocation = &Args->BroadcastDstAllocations[1];
    }
    Present.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Present.pPrivateDriverData = Args->pPrivateDriverData;

    /* Flips keep their orientation in the flip itself */
    if (Present.Flags.Flip || Present.Flags.RedirectedFlip)
        Present.Flags.Rotate = 0;

    Submit.hHwQueues = Queues;
    Status = RxGetKmtThunks()->SubmitPresentToHwQueue(&Submit);
    Context->KernelStatus = Status;

    /* An occluded window is not an error; the present simply showed nothing */
    hr = RxHresultFromStatus(Status);
    if (FAILED(hr))
        RX_TRACE(L"D3DKMTSubmitPresentToHwQueue failed 0x%08lX", Status);
    return hr;
}

static
HRESULT
APIENTRY
RxDxgiPresentMultiplaneOverlayCb(
    _In_ HANDLE hDevice,
    _In_ const DXGIDDICB_PRESENT_MULTIPLANE_OVERLAY* Args)
{
    return E_NOTIMPL;
}

static const DXGI_DDI_BASE_CALLBACKS ImplementedDxgiCallbacks =
{
    .pfnPresentCb = RxDxgiPresentCb,
    .pfnPresentMultiplaneOverlayCb = RxDxgiPresentMultiplaneOverlayCb,
    .pfnSubmitPresentToHwQueueCb = RxDxgiSubmitPresentToHwQueueCb,
};

static const PCWSTR KernelCallbacksName = L"D3DDDI_DEVICECALLBACKS";
static const PCWSTR DxgiCallbacksName = L"DXGI_DDI_BASE_CALLBACKS";

const D3DDDI_DEVICECALLBACKS*
RxGetKernelCallbacks(VOID)
{
    static const D3DDDI_DEVICECALLBACKS Callbacks = []()
    {
        D3DDDI_DEVICECALLBACKS Filled = ImplementedKernelCallbacks;

        RxFillMissingCallbacks<&KernelCallbacksName>(Filled);
        return Filled;
    }();

    return &Callbacks;
}

const D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS*
RxGetCoreLayerCallbacks(VOID)
{
    return &CoreLayerCallbacks;
}

DXGI_DDI_BASE_CALLBACKS*
RxGetDxgiCallbacks(VOID)
{
    /* The UMD records this pointer, and the DDI types it as writable */
    static DXGI_DDI_BASE_CALLBACKS Callbacks = []()
    {
        DXGI_DDI_BASE_CALLBACKS Filled = ImplementedDxgiCallbacks;

        RxFillMissingCallbacks<&DxgiCallbacksName>(Filled);
        return Filled;
    }();

    return &Callbacks;
}
