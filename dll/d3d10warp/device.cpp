/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation, the per version function tables and device queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device.hpp"
#include <type_traits>

/* Placeholder for entry points not written yet: does nothing and returns zero */
template <typename Function>
struct RxWarpStub;

template <typename Result, typename... Arguments>
struct RxWarpStub<Result (APIENTRY *)(Arguments...)>
{
    static
    Result
    APIENTRY
    Call(
        Arguments...)
    {
        if constexpr (std::is_void_v<Result>)
            return;
        else
            return Result{};
    }
};

/* Every CalcPrivate*Size: the runtime's object memory only holds our pointer */
template <typename Function>
struct RxWarpHandleSizeOf;

template <typename Result, typename... Arguments>
struct RxWarpHandleSizeOf<Result (APIENTRY *)(Arguments...)>
{
    static
    Result
    APIENTRY
    Call(
        Arguments...)
    {
        return (Result)RxWarpHandleSize;
    }
};

static
RxWarpDevice *
RxWarpGetDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarpDevice>(Device);
}

VOID
RxWarpDevice::SetError(
    _In_ HRESULT Error)
{
    if (CoreCallbacks != nullptr && CoreCallbacks->pfnSetErrorCb != nullptr)
        CoreCallbacks->pfnSetErrorCb(RuntimeCoreLayer, Error);
}

static
VOID
APIENTRY
RxWarpDestroyDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    RxWarpLock Lock;

    delete RxWarpGetDevice(Device);
    *static_cast<RxWarpDevice **>(Device.pDrvPrivate) = nullptr;
}

/* Work runs as it is recorded, so there is never anything queued */
static
BOOL
APIENTRY
RxWarpFlush(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT Flags)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Flags);
    return TRUE;
}

static
BOOL
APIENTRY
RxWarpFlush2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT ContextType,
    _In_ UINT Flags)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(ContextType);
    UNREFERENCED_PARAMETER(Flags);
    return TRUE;
}

static
VOID
APIENTRY
RxWarpCheckFormatSupport(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _Out_ UINT *Support)
{
    RxWarpLock Lock;

    *Support = RxWarpFormatSupport(Format, RxWarpGetDevice(Device)->Interface);
}

static
VOID
APIENTRY
RxWarpCheckMultisampleQualityLevels(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ UINT Flags,
    _Out_ UINT *Levels)
{
    UNREFERENCED_PARAMETER(Device);
    *Levels = RxWarpMultisampleQuality(Format, SampleCount, Flags);
}

static
VOID
APIENTRY
RxWarpCheckCounterInfo(
    _In_ D3D10DDI_HDEVICE Device,
    _Out_ D3D10DDI_COUNTER_INFO *Info)
{
    UNREFERENCED_PARAMETER(Device);
    Info->LastDeviceDependentCounter = (D3D10DDI_QUERY)0;
    Info->NumSimultaneousCounters = 0;
    Info->NumDetectableParallelUnits = 1;
}

/* DXGI: no display here yet, so presents and mode changes succeed without doing anything */
static
HRESULT
APIENTRY
RxWarpGetGammaCaps(
    _In_ DXGI_DDI_ARG_GET_GAMMA_CONTROL_CAPS *Args)
{
    ZeroMemory(Args->pGammaCapabilities, sizeof(*Args->pGammaCapabilities));
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarpQueryResourceResidency(
    _In_ DXGI_DDI_ARG_QUERYRESOURCERESIDENCY *Args)
{
    for (UINT Index = 0; Index < Args->Resources; Index++)
        Args->pStatus[Index] = DXGI_DDI_RESIDENCY_FULLY_RESIDENT;
    return S_OK;
}

/* DXGI hands resources over as the driver private pointer itself */
static
RxWarpResource *
RxWarpDxgiResource(
    _In_ DXGI_DDI_HRESOURCE Handle)
{
    return *reinterpret_cast<RxWarpResource **>(Handle);
}

static
HRESULT
APIENTRY
RxWarpRotateResourceIdentities(
    _In_ DXGI_DDI_ARG_ROTATE_RESOURCE_IDENTITIES *Args)
{
    RxWarpLock Lock;
    RxWarpResource *First;
    BYTE *Memory;

    if (Args->Resources < 2)
        return S_OK;

    /* Each buffer takes the next one's storage; the first's goes to the last */
    First = RxWarpDxgiResource(Args->pResources[0]);
    Memory = First->Memory;
    for (UINT Index = 0; Index + 1 < Args->Resources; Index++)
        RxWarpDxgiResource(Args->pResources[Index])->Memory = RxWarpDxgiResource(Args->pResources[Index + 1])->Memory;
    RxWarpDxgiResource(Args->pResources[Args->Resources - 1])->Memory = Memory;
    return S_OK;
}

HRESULT
RxWarpDevice::Create(
    _In_ RxWarpAdapter *Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE *Args)
{
    static D3DWDDM2_6DDI_DEVICEFUNCS Funcs;
    static DXGI1_6_1_DDI_BASE_FUNCTIONS Dxgi;
    static LONG Filled;
    RxWarpDevice *Device;
    SIZE_T Bytes;

    if (InterlockedCompareExchange(&Filled, 1, 0) == 0)
    {
#define RX_DEVICE_SLOT(Field) Funcs.Field = &RxWarpStub<decltype(Funcs.Field)>::Call;
#define RX_DEVICE_SIZE_SLOT(Field) Funcs.Field = &RxWarpHandleSizeOf<decltype(Funcs.Field)>::Call;
#define RX_DXGI_SLOT(Field) Dxgi.Field = &RxWarpStub<decltype(Dxgi.Field)>::Call;
#include "slots.inl"
#undef RX_DEVICE_SLOT
#undef RX_DEVICE_SIZE_SLOT
#undef RX_DXGI_SLOT

        Funcs.pfnDestroyDevice = RxWarpDestroyDevice;
        Funcs.pfnFlush = RxWarpFlush2;
        Funcs.pfnCheckFormatSupport = RxWarpCheckFormatSupport;
        Funcs.pfnCheckMultisampleQualityLevels = RxWarpCheckMultisampleQualityLevels;
        Funcs.pfnCheckCounterInfo = RxWarpCheckCounterInfo;

        Funcs.pfnCreateResource = RxWarpDdi::CreateResource;
        Funcs.pfnOpenResource = RxWarpDdi::OpenResource;
        Funcs.pfnDestroyResource = RxWarpDdi::DestroyResource;
        Funcs.pfnResourceMap = RxWarpDdi::ResourceMap;
        Funcs.pfnStagingResourceMap = RxWarpDdi::ResourceMap;
        Funcs.pfnDynamicIABufferMapNoOverwrite = RxWarpDdi::ResourceMap;
        Funcs.pfnDynamicIABufferMapDiscard = RxWarpDdi::ResourceMap;
        Funcs.pfnDynamicConstantBufferMapDiscard = RxWarpDdi::ResourceMap;
        Funcs.pfnDynamicConstantBufferMapNoOverwrite = RxWarpDdi::ResourceMap;
        Funcs.pfnDynamicResourceMapDiscard = RxWarpDdi::ResourceMap;
        Funcs.pfnResourceUnmap = RxWarpDdi::ResourceUnmap;
        Funcs.pfnStagingResourceUnmap = RxWarpDdi::ResourceUnmap;
        Funcs.pfnDynamicIABufferUnmap = RxWarpDdi::ResourceUnmap;
        Funcs.pfnDynamicConstantBufferUnmap = RxWarpDdi::ResourceUnmap;
        Funcs.pfnDynamicResourceUnmap = RxWarpDdi::ResourceUnmap;
        Funcs.pfnResourceIsStagingBusy = RxWarpDdi::ResourceIsStagingBusy;
        Funcs.pfnResourceCopy = RxWarpDdi::ResourceCopy;
        Funcs.pfnResourceConvert = RxWarpDdi::ResourceCopy;
        Funcs.pfnResourceCopyRegion = RxWarpDdi::ResourceCopyRegion;
        Funcs.pfnResourceConvertRegion = RxWarpDdi::ResourceCopyRegion;
        Funcs.pfnResourceUpdateSubresourceUP = RxWarpDdi::ResourceUpdateSubresourceUP;
        Funcs.pfnDefaultConstantBufferUpdateSubresourceUP = RxWarpDdi::ResourceUpdateSubresourceUP;

        Funcs.pfnCreateRenderTargetView = RxWarpDdi::CreateRenderTargetView2;
        Funcs.pfnDestroyRenderTargetView = RxWarpDdi::DestroyRenderTargetView;
        Funcs.pfnCreateDepthStencilView = RxWarpDdi::CreateDepthStencilView;
        Funcs.pfnDestroyDepthStencilView = RxWarpDdi::DestroyDepthStencilView;
        Funcs.pfnCreateShaderResourceView = RxWarpDdi::CreateShaderResourceView2;
        Funcs.pfnDestroyShaderResourceView = RxWarpDdi::DestroyShaderResourceView;
        Funcs.pfnCreateUnorderedAccessView = RxWarpDdi::CreateUnorderedAccessView2;
        Funcs.pfnDestroyUnorderedAccessView = RxWarpDdi::DestroyUnorderedAccessView;

        Funcs.pfnClearRenderTargetView = RxWarpDdi::ClearRenderTargetView;
        Funcs.pfnClearDepthStencilView = RxWarpDdi::ClearDepthStencilView;
        Funcs.pfnClearUnorderedAccessViewUint = RxWarpDdi::ClearUnorderedAccessViewUint;
        Funcs.pfnClearUnorderedAccessViewFloat = RxWarpDdi::ClearUnorderedAccessViewFloat;
        Funcs.pfnClearView = RxWarpDdi::ClearView;

        Funcs.pfnCreateBlendState = RxWarpDdi::CreateBlendState;
        Funcs.pfnDestroyBlendState = RxWarpDdi::DestroyBlendState;
        Funcs.pfnCreateDepthStencilState = RxWarpDdi::CreateDepthStencilState;
        Funcs.pfnDestroyDepthStencilState = RxWarpDdi::DestroyDepthStencilState;
        Funcs.pfnCreateRasterizerState = RxWarpDdi::CreateRasterizerState2;
        Funcs.pfnDestroyRasterizerState = RxWarpDdi::DestroyRasterizerState;
        Funcs.pfnCreateSampler = RxWarpDdi::CreateSampler;
        Funcs.pfnDestroySampler = RxWarpDdi::DestroySampler;
        Funcs.pfnCreateElementLayout = RxWarpDdi::CreateElementLayout;
        Funcs.pfnDestroyElementLayout = RxWarpDdi::DestroyElementLayout;
        Funcs.pfnCreateVertexShader = RxWarpDdi::CreateVertexShader;
        Funcs.pfnCreatePixelShader = RxWarpDdi::CreatePixelShader;
        Funcs.pfnCreateGeometryShader = RxWarpDdi::CreateGeometryShader;
        Funcs.pfnCreateGeometryShaderWithStreamOutput = RxWarpDdi::CreateGeometryShaderWithStreamOutput;
        Funcs.pfnGsSetShader = RxWarpDdi::GsSetShader;
        Funcs.pfnGsSetConstantBuffers = RxWarpDdi::GsSetConstantBuffers;
        Funcs.pfnGsSetShaderResources = RxWarpDdi::GsSetShaderResources;
        Funcs.pfnGsSetSamplers = RxWarpDdi::GsSetSamplers;
        Funcs.pfnCreateHullShader = RxWarpDdi::CreateHullShader;
        Funcs.pfnHsSetShader = RxWarpDdi::HsSetShader;
        Funcs.pfnHsSetConstantBuffers = RxWarpDdi::HsSetConstantBuffers;
        Funcs.pfnHsSetShaderResources = RxWarpDdi::HsSetShaderResources;
        Funcs.pfnHsSetSamplers = RxWarpDdi::HsSetSamplers;
        Funcs.pfnCreateDomainShader = RxWarpDdi::CreateDomainShader;
        Funcs.pfnDsSetShader = RxWarpDdi::DsSetShader;
        Funcs.pfnDsSetConstantBuffers = RxWarpDdi::DsSetConstantBuffers;
        Funcs.pfnDsSetShaderResources = RxWarpDdi::DsSetShaderResources;
        Funcs.pfnDsSetSamplers = RxWarpDdi::DsSetSamplers;
        Funcs.pfnSoSetTargets = RxWarpDdi::SoSetTargets;
        Funcs.pfnDrawAuto = RxWarpDdi::DrawAuto;
        Funcs.pfnDestroyShader = RxWarpDdi::DestroyShader;

        Funcs.pfnIaSetInputLayout = RxWarpDdi::IaSetInputLayout;
        Funcs.pfnIaSetVertexBuffers = RxWarpDdi::IaSetVertexBuffers;
        Funcs.pfnIaSetIndexBuffer = RxWarpDdi::IaSetIndexBuffer;
        Funcs.pfnIaSetTopology = RxWarpDdi::IaSetTopology;
        Funcs.pfnVsSetShader = RxWarpDdi::VsSetShader;
        Funcs.pfnPsSetShader = RxWarpDdi::PsSetShader;
        Funcs.pfnVsSetConstantBuffers = RxWarpDdi::VsSetConstantBuffers;
        Funcs.pfnPsSetConstantBuffers = RxWarpDdi::PsSetConstantBuffers;
        Funcs.pfnVsSetShaderResources = RxWarpDdi::VsSetShaderResources;
        Funcs.pfnPsSetShaderResources = RxWarpDdi::PsSetShaderResources;
        Funcs.pfnVsSetSamplers = RxWarpDdi::VsSetSamplers;
        Funcs.pfnPsSetSamplers = RxWarpDdi::PsSetSamplers;
        Funcs.pfnSetBlendState = RxWarpDdi::SetBlendState;
        Funcs.pfnSetDepthStencilState = RxWarpDdi::SetDepthStencilState;
        Funcs.pfnSetRasterizerState = RxWarpDdi::SetRasterizerState;
        Funcs.pfnSetViewports = RxWarpDdi::SetViewports;
        Funcs.pfnSetScissorRects = RxWarpDdi::SetScissorRects;
        Funcs.pfnSetRenderTargets = RxWarpDdi::SetRenderTargets;

        Funcs.pfnCreateQuery = RxWarpDdi::CreateQuery2;
        Funcs.pfnDestroyQuery = RxWarpDdi::DestroyQuery;
        Funcs.pfnQueryBegin = RxWarpDdi::QueryBegin;
        Funcs.pfnQueryEnd = RxWarpDdi::QueryEnd;
        Funcs.pfnQueryGetData = RxWarpDdi::QueryGetData;
        Funcs.pfnSetPredication = RxWarpDdi::SetPredication;

        Funcs.pfnGenMips = RxWarpDdi::GenMips;

        Funcs.pfnCreateComputeShader = RxWarpDdi::CreateComputeShader;
        Funcs.pfnCsSetShader = RxWarpDdi::CsSetShader;
        Funcs.pfnCsSetShaderResources = RxWarpDdi::CsSetShaderResources;
        Funcs.pfnCsSetSamplers = RxWarpDdi::CsSetSamplers;
        Funcs.pfnCsSetConstantBuffers = RxWarpDdi::CsSetConstantBuffers;
        Funcs.pfnCsSetUnorderedAccessViews = RxWarpDdi::CsSetUnorderedAccessViews;
        Funcs.pfnDispatch = RxWarpDdi::Dispatch;
        Funcs.pfnDispatchIndirect = RxWarpDdi::DispatchIndirect;
        Funcs.pfnCopyStructureCount = RxWarpDdi::CopyStructureCount;
        Funcs.pfnDrawInstancedIndirect = RxWarpDdi::DrawInstancedIndirect;
        Funcs.pfnDrawIndexedInstancedIndirect = RxWarpDdi::DrawIndexedInstancedIndirect;

        Funcs.pfnDraw = RxWarpDdi::Draw;
        Funcs.pfnDrawIndexed = RxWarpDdi::DrawIndexed;
        Funcs.pfnDrawInstanced = RxWarpDdi::DrawInstanced;
        Funcs.pfnDrawIndexedInstanced = RxWarpDdi::DrawIndexedInstanced;

        /* Left empty, as the reference does: the runtime emulates deferred contexts */
        Funcs.pfnCommandListExecute = nullptr;
        Funcs.pfnCheckDeferredContextHandleSizes = nullptr;
        Funcs.pfnCalcDeferredContextHandleSize = nullptr;
        Funcs.pfnCalcPrivateDeferredContextSize = nullptr;
        Funcs.pfnCreateDeferredContext = nullptr;
        Funcs.pfnAbandonCommandList = nullptr;
        Funcs.pfnCalcPrivateCommandListSize = nullptr;
        Funcs.pfnCreateCommandList = nullptr;
        Funcs.pfnDestroyCommandList = nullptr;
        Funcs.pfnRecycleCommandList = nullptr;
        Funcs.pfnRecycleCreateCommandList = nullptr;
        Funcs.pfnRecycleCreateDeferredContext = nullptr;
        Funcs.pfnRecycleDestroyCommandList = nullptr;
        Funcs.pfnSetHardwareProtection = nullptr;
        Funcs.pfnRetrieveShaderComment = nullptr;
        Funcs.pfnCalcPrivateShaderCacheSessionSize = nullptr;
        Funcs.pfnCreateShaderCacheSession = nullptr;
        Funcs.pfnDestroyShaderCacheSession = nullptr;
        Funcs.pfnSetShaderCacheSession = nullptr;

        Dxgi.pfnPresent = nullptr;
        Dxgi.pfnGetGammaCaps = RxWarpGetGammaCaps;
        Dxgi.pfnQueryResourceResidency = RxWarpQueryResourceResidency;
        Dxgi.pfnRotateResourceIdentities = RxWarpRotateResourceIdentities;
        Dxgi.pfnGetMultiplaneOverlayCaps = nullptr;
        Dxgi.pfnGetMultiplaneOverlayGroupCaps = nullptr;
        Dxgi.pfnReserved1 = nullptr;
        Dxgi.pfnPresentMultiplaneOverlay = nullptr;
        Dxgi.pfnReserved2 = nullptr;
        Dxgi.pfnCheckPresentDurationSupport = nullptr;
        Dxgi.pfnCheckMultiplaneOverlayColorSpaceSupport = nullptr;
        Dxgi.pfnPresentMultiplaneOverlay1 = nullptr;
        Dxgi.pfnReclaimResources1 = nullptr;
    }

    /* Each interface gets its table's prefix of the newest one */
    switch (Args->Interface)
    {
        case D3DWDDM1_3_DDI_INTERFACE_VERSION:
            Bytes = sizeof(D3DWDDM1_3DDI_DEVICEFUNCS);
            break;
        case D3DWDDM2_0_DDI_INTERFACE_VERSION:
            Bytes = sizeof(D3DWDDM2_0DDI_DEVICEFUNCS);
            break;
        case D3DWDDM2_1_DDI_INTERFACE_VERSION:
            Bytes = sizeof(D3DWDDM2_1DDI_DEVICEFUNCS);
            break;
        default:
            if (Args->Interface >= 0xB0023 && Args->Interface <= 0xB0026)
                Bytes = sizeof(D3DWDDM2_2DDI_DEVICEFUNCS);
            else if (Args->Interface >= 0xB0027 && Args->Interface <= 0xB002D)
                Bytes = sizeof(D3DWDDM2_6DDI_DEVICEFUNCS);
            else
                return E_INVALIDARG;
            break;
    }

    Device = new (std::nothrow) RxWarpDevice();
    if (Device == nullptr)
        return E_OUTOFMEMORY;

    Device->Adapter = Adapter;
    Device->RuntimeDevice = Args->hRTDevice;
    Device->RuntimeCoreLayer = Args->hRTCoreLayer;
    Device->KernelCallbacks = Args->pKTCallbacks;
    Device->CoreCallbacks = Args->p11UMCallbacks;
    Device->Interface = Args->Interface;
    Device->Version = Args->Version;
    RxWarpBind(Args->hDrvDevice, Device);
    RxWarpLastDevice = Device;

    memcpy(Args->pWDDM2_6DeviceFuncs, &Funcs, Bytes);

    /* WDDM 1.3 takes the older argument structs for these */
    if (Args->Interface == D3DWDDM1_3_DDI_INTERFACE_VERSION)
    {
        Args->pWDDM1_3DeviceFuncs->pfnFlush = RxWarpFlush;
        Args->pWDDM1_3DeviceFuncs->pfnCreateRenderTargetView = RxWarpDdi::CreateRenderTargetView;
        Args->pWDDM1_3DeviceFuncs->pfnCreateShaderResourceView = RxWarpDdi::CreateShaderResourceView;
        Args->pWDDM1_3DeviceFuncs->pfnCreateUnorderedAccessView = RxWarpDdi::CreateUnorderedAccessView;
        Args->pWDDM1_3DeviceFuncs->pfnCreateRasterizerState = RxWarpDdi::CreateRasterizerState;
        Args->pWDDM1_3DeviceFuncs->pfnCreateQuery = RxWarpDdi::CreateQuery;
    }

    /* DXGI: 1.3 for WDDM 1.3, 1.4 for WDDM 2.0, 1.6.1 after that */
    if (Args->DXGIBaseDDI.pDXGIDDIBaseFunctions6_1 != nullptr)
    {
        if (Args->Interface == D3DWDDM1_3_DDI_INTERFACE_VERSION)
            memcpy(Args->DXGIBaseDDI.pDXGIDDIBaseFunctions4, &Dxgi, sizeof(DXGI1_3_DDI_BASE_FUNCTIONS));
        else if (Args->Interface == D3DWDDM2_0_DDI_INTERFACE_VERSION)
            memcpy(Args->DXGIBaseDDI.pDXGIDDIBaseFunctions5, &Dxgi, sizeof(DXGI1_4_DDI_BASE_FUNCTIONS));
        else
            memcpy(Args->DXGIBaseDDI.pDXGIDDIBaseFunctions6_1, &Dxgi, sizeof(Dxgi));
    }
    return S_OK;
}
