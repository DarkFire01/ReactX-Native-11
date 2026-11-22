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
    RxWarpDevice *This = RxWarpGetDevice(Device);

    if (This != nullptr && This->ContextAllocation != 0)
    {
        D3DDDICB_UNLOCK Unlock = {};
        D3DDDICB_DEALLOCATE Deallocate = {};

        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &This->ContextAllocation;
        This->KernelCallbacks->pfnUnlockCb(This->RuntimeDevice.handle, &Unlock);
        Deallocate.NumAllocations = 1;
        Deallocate.HandleList = &This->ContextAllocation;
        This->KernelCallbacks->pfnDeallocateCb(This->RuntimeDevice.handle, &Deallocate);
    }
    if (This != nullptr && This->KernelContext != nullptr && This->KernelCallbacks->pfnDestroyContextCb != nullptr)
    {
        D3DDDICB_DESTROYCONTEXT Destroy = {};

        Destroy.hContext = This->KernelContext;
        This->KernelCallbacks->pfnDestroyContextCb(This->RuntimeDevice.handle, &Destroy);
    }
    if (This != nullptr && This->ContextPage != nullptr)
        VirtualFree(This->ContextPage, 0, MEM_RELEASE);
    delete This;
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
    D3DKMT_HANDLE Allocation;
    BOOLEAN Locked;

    if (Args->Resources < 2)
        return S_OK;

    /* Each buffer takes the next one's storage and allocation; the first's go to the last */
    First = RxWarpDxgiResource(Args->pResources[0]);
    Memory = First->Memory;
    Allocation = First->Allocation;
    Locked = First->Locked;
    for (UINT Index = 0; Index + 1 < Args->Resources; Index++)
    {
        RxWarpResource *This = RxWarpDxgiResource(Args->pResources[Index]);
        RxWarpResource *Next = RxWarpDxgiResource(Args->pResources[Index + 1]);

        This->Memory = Next->Memory;
        This->Allocation = Next->Allocation;
        This->Locked = Next->Locked;
    }
    RxWarpDxgiResource(Args->pResources[Args->Resources - 1])->Memory = Memory;
    RxWarpDxgiResource(Args->pResources[Args->Resources - 1])->Allocation = Allocation;
    RxWarpDxgiResource(Args->pResources[Args->Resources - 1])->Locked = Locked;
    return S_OK;
}

/* Whether every channel of a format is floating point */
static
BOOLEAN
RxWarpFormatIsFloat(
    _In_ DXGI_FORMAT Format)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);

    return Info != nullptr && Info->Kind == RxKindPlain && Info->ChannelCount != 0 && Info->Channels[0].Type == RxChannelFloat;
}

/* Present and format conversion copies: the source scales onto the destination rectangle */
static
HRESULT
APIENTRY
RxWarpBlt(
    _In_ DXGI_DDI_ARG_BLT *Args)
{
    RxWarpLock Lock;
    RxWarpResource *Source = RxWarpDxgiResource(Args->hSrcResource);
    RxWarpResource *Target = RxWarpDxgiResource(Args->hDstResource);
    RX_WARP_SUBRESOURCE *From;
    RX_WARP_SUBRESOURCE *To;
    const RX_WARP_FORMAT_INFO *FromInfo;
    const RX_WARP_FORMAT_INFO *ToInfo;
    BOOLEAN Encode;
    UINT Width;
    UINT Height;

    if (Source == nullptr || Target == nullptr)
        return E_INVALIDARG;
    From = Source->Subresource(Args->SrcSubresource);
    To = Target->Subresource(Args->DstSubresource);
    FromInfo = RxWarpFormatInfo(Source->Format);
    ToInfo = RxWarpFormatInfo(Target->Format);
    if (From == nullptr || To == nullptr || FromInfo == nullptr || ToInfo == nullptr)
        return E_INVALIDARG;
    if (Args->DstLeft > Args->DstRight || Args->DstTop > Args->DstBottom || Args->DstRight > To->Width || Args->DstBottom > To->Height)
        return E_INVALIDARG;

    /* Float sources are linear light and go out sRGB encoded to normalized targets */
    Encode = RxWarpFormatIsFloat(Source->Format) && !RxWarpFormatIsFloat(Target->Format);
    Width = Args->DstRight - Args->DstLeft;
    Height = Args->DstBottom - Args->DstTop;

    for (UINT Y = 0; Y < Height; Y++)
    {
        UINT Sy = (UINT)(((UINT64)Y * From->Height + From->Height / 2) / Height);
        BYTE *Row = Target->Data(Args->DstSubresource) + (SIZE_T)(Args->DstTop + Y) * To->RowPitch;

        Sy = (Sy < From->Height) ? Sy : From->Height - 1;
        for (UINT X = 0; X < Width; X++)
        {
            UINT Sx = (UINT)(((UINT64)X * From->Width + From->Width / 2) / Width);
            const BYTE *Texel;
            UINT Raw[4];
            FLOAT Color[4];
            BYTE Element[16];

            Sx = (Sx < From->Width) ? Sx : From->Width - 1;
            Texel = Source->Data(Args->SrcSubresource) + (SIZE_T)Sy * From->RowPitch + (SIZE_T)Sx * FromInfo->Bytes;
            if (!RxWarpDecodeTexel(Source->Format, Texel, Raw))
                return E_INVALIDARG;
            memcpy(Color, Raw, sizeof(Color));
            if (Encode)
            {
                for (UINT Component = 0; Component < 3; Component++)
                    Color[Component] = (FLOAT)RxWarpLinearToSrgbByte(Color[Component]) * (1.0f / 255.0f);
            }
            if (!RxWarpEncodeColor(Target->Format, Color, Element))
                return E_INVALIDARG;
            memcpy(Row + (SIZE_T)(Args->DstLeft + X) * ToInfo->Bytes, Element, ToInfo->Bytes);
        }
    }
    return S_OK;
}

/* Talks to the Basic Render kernel driver through its escape interface */
static
HRESULT
RxWarpEscape(
    _In_ RxWarpDevice *Device,
    _Inout_updates_bytes_(Size) VOID *Data,
    _In_ UINT Size,
    _In_opt_ HANDLE Context)
{
    D3DDDICB_ESCAPE Escape = {};

    if (Device->KernelCallbacks == nullptr || Device->KernelCallbacks->pfnEscapeCb == nullptr)
        return E_NOTIMPL;
    Escape.hDevice = Device->RuntimeDevice.handle;
    Escape.pPrivateDriverData = Data;
    Escape.PrivateDriverDataSize = Size;
    Escape.hContext = Context;
    return Device->KernelCallbacks->pfnEscapeCb(Device->Adapter->RuntimeAdapter.handle, &Escape);
}

/*
 * Presents are submitted to a kernel context on the engine node the kernel driver reports.
 * The driver first takes the protocol revision, then answers which node and engine to use.
 */
static
VOID
RxWarpCreateKernelContext(
    _In_ RxWarpDevice *Device)
{
    const D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS *Core;
    D3DWDDM2_0DDICB_CREATECONTEXTVIRTUAL Create = {};
    UINT Revision[2] = { 3, 2 };
    UINT Engine[4] = { 1, 0, 0, 0 };
    UINT64 Values[2] = { 0, 8 };
    UINT64 *Private[2] = { &Values[0], &Values[1] };

    if (Device->Interface < D3DWDDM2_0_DDI_INTERFACE_VERSION || Device->CoreCallbacks == nullptr)
        return;
    Core = reinterpret_cast<const D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS *>(Device->CoreCallbacks);
    if (Core->pfnCreateContextVirtualCb == nullptr)
        return;
    if (FAILED(RxWarpEscape(Device, Revision, sizeof(Revision), nullptr)) || FAILED(RxWarpEscape(Device, Engine, sizeof(Engine), nullptr)))
        return;

    Create.NodeOrdinal = Engine[2];
    Create.EngineAffinity = Engine[3];
    Create.ContextTypeFlags = 7;
    Create.pPrivateDriverData = Private;
    Create.PrivateDriverDataSize = sizeof(Private);
    if (FAILED(Core->pfnCreateContextVirtualCb(Device->RuntimeCoreLayer, &Create)))
        return;
    Device->KernelContext = Create.hContext;

    /*
     * The context gets a page of its own the kernel driver knows, which sizes a kernel buffer
     * made next; then the device is registered with the same page.
     */
    Device->ContextPage = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Device->ContextPage != nullptr)
    {
        UINT64 ContextData[6] = { 7, (UINT64)Device, (UINT64)Device->ContextPage, 4096, 0, 0 };
        UINT64 DeviceData[7] = { 0, (UINT64)Device, (UINT64)Device->ContextPage, 4096, 0, 0, 0 };
        /* The answer comes back in the last field */
        if (SUCCEEDED(RxWarpEscape(Device, ContextData, sizeof(ContextData), Create.hContext)) &&
            SUCCEEDED(RxWarpAllocateRaw(Device, (UINT)ContextData[5], &Device->ContextAllocation, &Device->ContextBuffer)))
        {
            RxWarpEscape(Device, DeviceData, sizeof(DeviceData), nullptr);
        }
    }
}

/* The eight bit shadow a non BGRA8 back buffer is converted into before DXGI presents it */
static
RxWarpResource *
RxWarpPresentShadow(
    _In_ RxWarpDevice *Device,
    _In_ RxWarpResource *Source,
    _In_ UINT Subresource)
{
    RX_WARP_SUBRESOURCE *From = Source->Subresource(Subresource);
    D3D10DDI_MIPINFO Mip = {};
    D3D11DDIARG_CREATERESOURCE Create = {};
    RxWarpResource *Shadow = Source->Shadow;

    if (From == nullptr)
        return nullptr;
    if (Shadow != nullptr && Shadow->Subresources[0].Width == From->Width && Shadow->Subresources[0].Height == From->Height)
        return Shadow;
    if (Shadow != nullptr)
    {
        RxWarpReleaseForPresent(Device, Shadow);
        delete Shadow;
        Source->Shadow = nullptr;
    }

    Mip.TexelWidth = From->Width;
    Mip.TexelHeight = From->Height;
    Mip.TexelDepth = 1;
    Mip.PhysicalWidth = From->Width;
    Mip.PhysicalHeight = From->Height;
    Mip.PhysicalDepth = 1;
    Create.pMipInfoList = &Mip;
    Create.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    Create.Usage = D3D10_DDI_USAGE_STAGING;
    Create.BindFlags = Source->BindFlags & ~D3D10_DDI_BIND_PRESENT;
    Create.MiscFlags = Source->MiscFlags & ~D3D10_DDI_RESOURCE_MISC_SHARED;
    Create.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
    Create.SampleDesc.Count = 1;
    Create.MipLevels = 1;
    Create.ArraySize = 1;
    Shadow = new (std::nothrow) RxWarpResource();
    if (Shadow == nullptr)
        return nullptr;
    if (FAILED(Shadow->Initialize(&Create)) || FAILED(RxWarpAllocateForPresent(Device, Shadow, &Create, nullptr)) || Shadow->Allocation == 0)
    {
        RxWarpReleaseForPresent(Device, Shadow);
        delete Shadow;
        return nullptr;
    }
    Source->Shadow = Shadow;
    return Shadow;
}

/* Windowed presents hand DXGI an allocation in B8G8R8X8 or B8G8R8A8; other formats go through the shadow. Older tables take a prefix of this argument */
static
HRESULT
APIENTRY
RxWarpPresent1(
    _In_ DXGI1_6_1_DDI_ARG_PRESENT *Args)
{
    RxWarpLock Lock;
    RxWarpDevice *Device = RxWarpObject<RxWarpDevice>(*reinterpret_cast<D3D10DDI_HDEVICE *>(&Args->hDevice));
    HRESULT hr = S_OK;

    if (Device == nullptr || Device->DxgiCallbacks == nullptr || Device->DxgiCallbacks->pfnPresentCb == nullptr)
        return E_INVALIDARG;
    if (Args->hDstResource != 0)
        return E_NOTIMPL;

    for (UINT Index = 0; Index < Args->SurfacesToPresent && SUCCEEDED(hr); Index++)
    {
        RxWarpResource *Source = RxWarpDxgiResource(Args->phSurfacesToPresent[Index].hSurface);
        UINT Subresource = Args->phSurfacesToPresent[Index].SubResourceIndex;
        DXGIDDICB_PRESENT Present = {};

        if (Source == nullptr)
            return E_INVALIDARG;
        Present.hSrcAllocation = Source->Allocation;
        if ((Source->Format != DXGI_FORMAT_B8G8R8A8_UNORM && Source->Format != DXGI_FORMAT_B8G8R8X8_UNORM) ||
            Source->SampleCount > 1 || Subresource != 0)
        {
            RxWarpResource *Shadow = RxWarpPresentShadow(Device, Source, Subresource);
            RX_WARP_SUBRESOURCE *From = Source->Subresource(Subresource);
            DXGI_DDI_ARG_BLT Blt = {};

            if (Shadow == nullptr || From == nullptr)
                return E_OUTOFMEMORY;
            Blt.hDstResource = reinterpret_cast<DXGI_DDI_HRESOURCE>(&Shadow);
            Blt.hSrcResource = Args->phSurfacesToPresent[Index].hSurface;
            Blt.SrcSubresource = Subresource;
            Blt.DstRight = From->Width;
            Blt.DstBottom = From->Height;
            hr = RxWarpBlt(&Blt);
            Present.hSrcAllocation = Shadow->Allocation;
        }
        if (Present.hSrcAllocation == 0)
            return E_INVALIDARG;
        Present.pDXGIContext = Args->pDXGIContext;

        if (Device->KernelContext == nullptr)
            RxWarpCreateKernelContext(Device);
        Present.hContext = Device->KernelContext;
        if (SUCCEEDED(hr))
            hr = Device->DxgiCallbacks->pfnPresentCb(Device->RuntimeDevice.handle, &Present);
    }
    return hr;
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
        Funcs.pfnResourceResolveSubresource = RxWarpDdi::ResourceResolveSubresource;
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
        Dxgi.pfnBlt = RxWarpBlt;
        Dxgi.pfnPresent1 = RxWarpPresent1;
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
    Device->DxgiCallbacks = Args->DXGIBaseDDI.pDXGIBaseCallbacks;
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
