/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Blend, depth stencil, rasterizer and sampler state objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "states.hpp"

#include <cfloat>

/* Defaults */

D3D11_BLEND_DESC
RxDefaultBlendDesc(VOID)
{
    D3D11_BLEND_DESC Desc = {};
    UINT Target;

    for (Target = 0; Target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        Desc.RenderTarget[Target].SrcBlend = D3D11_BLEND_ONE;
        Desc.RenderTarget[Target].DestBlend = D3D11_BLEND_ZERO;
        Desc.RenderTarget[Target].BlendOp = D3D11_BLEND_OP_ADD;
        Desc.RenderTarget[Target].SrcBlendAlpha = D3D11_BLEND_ONE;
        Desc.RenderTarget[Target].DestBlendAlpha = D3D11_BLEND_ZERO;
        Desc.RenderTarget[Target].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        Desc.RenderTarget[Target].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }

    return Desc;
}

D3D11_DEPTH_STENCIL_DESC
RxDefaultDepthStencilDesc(VOID)
{
    const D3D11_DEPTH_STENCILOP_DESC Keep = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                              D3D11_COMPARISON_ALWAYS };
    D3D11_DEPTH_STENCIL_DESC Desc = {};

    Desc.DepthEnable = TRUE;
    Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    Desc.DepthFunc = D3D11_COMPARISON_LESS;
    Desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    Desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    Desc.FrontFace = Keep;
    Desc.BackFace = Keep;
    return Desc;
}

D3D11_RASTERIZER_DESC
RxDefaultRasterizerDesc(VOID)
{
    D3D11_RASTERIZER_DESC Desc = {};

    Desc.FillMode = D3D11_FILL_SOLID;
    Desc.CullMode = D3D11_CULL_BACK;
    Desc.DepthClipEnable = TRUE;
    return Desc;
}

D3D11_SAMPLER_DESC
RxDefaultSamplerDesc(VOID)
{
    D3D11_SAMPLER_DESC Desc = {};

    Desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    Desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    Desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    Desc.MaxAnisotropy = 1;
    Desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    Desc.BorderColor[0] = 1.0f;
    Desc.BorderColor[1] = 1.0f;
    Desc.BorderColor[2] = 1.0f;
    Desc.BorderColor[3] = 1.0f;
    Desc.MinLOD = -FLT_MAX;
    Desc.MaxLOD = FLT_MAX;
    return Desc;
}

/* Blend */

D3D11_BLEND_DESC1
RxBlendTraits::Promote(
    _In_ const D3D11_BLEND_DESC& Desc)
{
    D3D11_BLEND_DESC1 Wide = {};
    UINT Target;

    Wide.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable;
    Wide.IndependentBlendEnable = Desc.IndependentBlendEnable;
    for (Target = 0; Target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC& Source = Desc.RenderTarget[Target];
        D3D11_RENDER_TARGET_BLEND_DESC1& Out = Wide.RenderTarget[Target];

        Out.BlendEnable = Source.BlendEnable;
        Out.SrcBlend = Source.SrcBlend;
        Out.DestBlend = Source.DestBlend;
        Out.BlendOp = Source.BlendOp;
        Out.SrcBlendAlpha = Source.SrcBlendAlpha;
        Out.DestBlendAlpha = Source.DestBlendAlpha;
        Out.BlendOpAlpha = Source.BlendOpAlpha;
        Out.LogicOp = D3D11_LOGIC_OP_NOOP;
        Out.RenderTargetWriteMask = Source.RenderTargetWriteMask;
    }

    return Wide;
}

D3D11_BLEND_DESC
RxBlendTraits::Demote(
    _In_ const D3D11_BLEND_DESC1& Desc)
{
    D3D11_BLEND_DESC Narrow = {};
    UINT Target;

    Narrow.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable;
    Narrow.IndependentBlendEnable = Desc.IndependentBlendEnable;
    for (Target = 0; Target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC1& Source = Desc.RenderTarget[Target];
        D3D11_RENDER_TARGET_BLEND_DESC& Out = Narrow.RenderTarget[Target];

        Out.BlendEnable = Source.BlendEnable;
        Out.SrcBlend = Source.SrcBlend;
        Out.DestBlend = Source.DestBlend;
        Out.BlendOp = Source.BlendOp;
        Out.SrcBlendAlpha = Source.SrcBlendAlpha;
        Out.DestBlendAlpha = Source.DestBlendAlpha;
        Out.BlendOpAlpha = Source.BlendOpAlpha;
        Out.RenderTargetWriteMask = Source.RenderTargetWriteMask;
    }

    return Narrow;
}

D3D11_BLEND_DESC1
RxBlendTraits::Normalize(
    _In_ const D3D11_BLEND_DESC1& Source)
{
    D3D11_BLEND_DESC1 Desc = Source;
    UINT Target;

    /* Without independent blending every target follows the first */
    for (Target = 1; !Desc.IndependentBlendEnable && Target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
        Desc.RenderTarget[Target] = Desc.RenderTarget[0];

    /* A target that does not blend carries the pass through factors, whatever was asked */
    for (D3D11_RENDER_TARGET_BLEND_DESC1& Each : Desc.RenderTarget)
    {
        Each.BlendEnable = Each.BlendEnable ? TRUE : FALSE;
        Each.LogicOpEnable = Each.LogicOpEnable ? TRUE : FALSE;
        if (!Each.LogicOpEnable)
            Each.LogicOp = D3D11_LOGIC_OP_NOOP;
        if (!Each.BlendEnable)
        {
            Each.SrcBlend = D3D11_BLEND_ONE;
            Each.DestBlend = D3D11_BLEND_ZERO;
            Each.BlendOp = D3D11_BLEND_OP_ADD;
            Each.SrcBlendAlpha = D3D11_BLEND_ONE;
            Each.DestBlendAlpha = D3D11_BLEND_ZERO;
            Each.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
    }

    Desc.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable ? TRUE : FALSE;
    Desc.IndependentBlendEnable = Desc.IndependentBlendEnable ? TRUE : FALSE;
    return Desc;
}

D3D11_1_DDI_BLEND_DESC
RxBlendTraits::ToDdi(
    _In_ const D3D11_BLEND_DESC1& Desc)
{
    D3D11_1_DDI_BLEND_DESC Ddi = {};
    UINT Target;

    Ddi.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable;
    Ddi.IndependentBlendEnable = Desc.IndependentBlendEnable;
    for (Target = 0; Target < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC1& Source = Desc.RenderTarget[Target];
        D3D11_1_DDI_RENDER_TARGET_BLEND_DESC& Out = Ddi.RenderTarget[Target];

        Out.BlendEnable = Source.BlendEnable;
        Out.LogicOpEnable = Source.LogicOpEnable;
        Out.SrcBlend = (D3D10_DDI_BLEND)Source.SrcBlend;
        Out.DestBlend = (D3D10_DDI_BLEND)Source.DestBlend;
        Out.BlendOp = (D3D10_DDI_BLEND_OP)Source.BlendOp;
        Out.SrcBlendAlpha = (D3D10_DDI_BLEND)Source.SrcBlendAlpha;
        Out.DestBlendAlpha = (D3D10_DDI_BLEND)Source.DestBlendAlpha;
        Out.BlendOpAlpha = (D3D10_DDI_BLEND_OP)Source.BlendOpAlpha;
        Out.LogicOp = (D3D11_1_DDI_LOGIC_OP)Source.LogicOp;
        Out.RenderTargetWriteMask = Source.RenderTargetWriteMask;
    }

    return Ddi;
}

/* Depth stencil */

D3D11_DEPTH_STENCIL_DESC
RxDepthStencilTraits::Promote(
    _In_ const D3D11_DEPTH_STENCIL_DESC& Desc)
{
    return Desc;
}

D3D11_DEPTH_STENCIL_DESC
RxDepthStencilTraits::Demote(
    _In_ const D3D11_DEPTH_STENCIL_DESC& Desc)
{
    return Desc;
}

D3D11_DEPTH_STENCIL_DESC
RxDepthStencilTraits::Normalize(
    _In_ const D3D11_DEPTH_STENCIL_DESC& Source)
{
    const D3D11_DEPTH_STENCILOP_DESC Keep = { D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP,
                                              D3D11_COMPARISON_ALWAYS };
    D3D11_DEPTH_STENCIL_DESC Desc = Source;

    Desc.DepthEnable = Desc.DepthEnable ? TRUE : FALSE;
    Desc.StencilEnable = Desc.StencilEnable ? TRUE : FALSE;

    /* Fields a disabled test ignores are pinned, so equal behavior means equal descs */
    if (!Desc.DepthEnable)
    {
        Desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        Desc.DepthFunc = D3D11_COMPARISON_LESS;
    }

    if (!Desc.StencilEnable)
    {
        Desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
        Desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
        Desc.FrontFace = Keep;
        Desc.BackFace = Keep;
    }
    else if (!Desc.DepthEnable)
    {
        /* Nothing fails a depth test that is off */
        Desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        Desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    }

    return Desc;
}

/* A face whose stencil can never change anything is left disabled for the driver */
static
BOOL
RxIsStencilFaceActive(
    _In_ const D3D11_DEPTH_STENCILOP_DESC& Face)
{
    return Face.StencilFunc != D3D11_COMPARISON_ALWAYS || Face.StencilPassOp != D3D11_STENCIL_OP_KEEP ||
           Face.StencilDepthFailOp != D3D11_STENCIL_OP_KEEP;
}

static
D3D10_DDI_DEPTH_STENCILOP_DESC
RxToDdiStencilFace(
    _In_ const D3D11_DEPTH_STENCILOP_DESC& Face)
{
    D3D10_DDI_DEPTH_STENCILOP_DESC Ddi;

    Ddi.StencilFailOp = (D3D10_DDI_STENCIL_OP)Face.StencilFailOp;
    Ddi.StencilDepthFailOp = (D3D10_DDI_STENCIL_OP)Face.StencilDepthFailOp;
    Ddi.StencilPassOp = (D3D10_DDI_STENCIL_OP)Face.StencilPassOp;
    Ddi.StencilFunc = (D3D10_DDI_COMPARISON_FUNC)Face.StencilFunc;
    return Ddi;
}

D3D10_DDI_DEPTH_STENCIL_DESC
RxDepthStencilTraits::ToDdi(
    _In_ const D3D11_DEPTH_STENCIL_DESC& Desc)
{
    D3D10_DDI_DEPTH_STENCIL_DESC Ddi = {};

    Ddi.DepthEnable = Desc.DepthEnable;
    Ddi.DepthWriteMask = (D3D10_DDI_DEPTH_WRITE_MASK)Desc.DepthWriteMask;
    Ddi.DepthFunc = (D3D10_DDI_COMPARISON_FUNC)Desc.DepthFunc;
    Ddi.StencilEnable = Desc.StencilEnable;
    Ddi.FrontEnable = Desc.StencilEnable && RxIsStencilFaceActive(Desc.FrontFace);
    Ddi.BackEnable = Desc.StencilEnable && RxIsStencilFaceActive(Desc.BackFace);
    Ddi.StencilReadMask = Desc.StencilReadMask;
    Ddi.StencilWriteMask = Desc.StencilWriteMask;
    Ddi.FrontFace = RxToDdiStencilFace(Desc.FrontFace);
    Ddi.BackFace = RxToDdiStencilFace(Desc.BackFace);
    return Ddi;
}

/* Rasterizer */

D3D11_RASTERIZER_DESC1
RxRasterizerTraits::Promote(
    _In_ const D3D11_RASTERIZER_DESC& Desc)
{
    D3D11_RASTERIZER_DESC1 Wide = {};

    memcpy(&Wide, &Desc, sizeof(Desc));
    Wide.ForcedSampleCount = 0;
    return Wide;
}

D3D11_RASTERIZER_DESC
RxRasterizerTraits::Demote(
    _In_ const D3D11_RASTERIZER_DESC1& Desc)
{
    D3D11_RASTERIZER_DESC Narrow;

    /* The newer desc only appends ForcedSampleCount */
    static_assert(offsetof(D3D11_RASTERIZER_DESC1, ForcedSampleCount) == sizeof(D3D11_RASTERIZER_DESC));
    memcpy(&Narrow, &Desc, sizeof(Narrow));
    return Narrow;
}

D3D11_RASTERIZER_DESC1
RxRasterizerTraits::Normalize(
    _In_ const D3D11_RASTERIZER_DESC1& Source)
{
    D3D11_RASTERIZER_DESC1 Desc = Source;

    Desc.FrontCounterClockwise = Desc.FrontCounterClockwise ? TRUE : FALSE;
    Desc.DepthClipEnable = Desc.DepthClipEnable ? TRUE : FALSE;
    Desc.ScissorEnable = Desc.ScissorEnable ? TRUE : FALSE;
    Desc.MultisampleEnable = Desc.MultisampleEnable ? TRUE : FALSE;
    Desc.AntialiasedLineEnable = Desc.AntialiasedLineEnable ? TRUE : FALSE;
    return Desc;
}

D3D11_1_DDI_RASTERIZER_DESC
RxRasterizerTraits::ToDdi(
    _In_ const D3D11_RASTERIZER_DESC1& Desc)
{
    D3D11_1_DDI_RASTERIZER_DESC Ddi = {};

    Ddi.FillMode = (D3D10_DDI_FILL_MODE)Desc.FillMode;
    Ddi.CullMode = (D3D10_DDI_CULL_MODE)Desc.CullMode;
    Ddi.FrontCounterClockwise = Desc.FrontCounterClockwise;
    Ddi.DepthBias = Desc.DepthBias;
    Ddi.DepthBiasClamp = Desc.DepthBiasClamp;
    Ddi.SlopeScaledDepthBias = Desc.SlopeScaledDepthBias;
    Ddi.DepthClipEnable = Desc.DepthClipEnable;
    Ddi.ScissorEnable = Desc.ScissorEnable;
    Ddi.MultisampleEnable = Desc.MultisampleEnable;
    Ddi.AntialiasedLineEnable = Desc.AntialiasedLineEnable;
    Ddi.ForcedSampleCount = Desc.ForcedSampleCount;
    return Ddi;
}

/* Sampler */

D3D11_SAMPLER_DESC
RxSamplerTraits::Promote(
    _In_ const D3D11_SAMPLER_DESC& Desc)
{
    return Desc;
}

D3D11_SAMPLER_DESC
RxSamplerTraits::Demote(
    _In_ const D3D11_SAMPLER_DESC& Desc)
{
    return Desc;
}

static
BOOLEAN
RxIsAnisotropicFilter(
    _In_ D3D11_FILTER Filter)
{
    return D3D11_DECODE_IS_ANISOTROPIC_FILTER(Filter);
}

D3D11_SAMPLER_DESC
RxSamplerTraits::Normalize(
    _In_ const D3D11_SAMPLER_DESC& Source)
{
    D3D11_SAMPLER_DESC Desc = Source;

    /* Fields the filter and address modes never read are cleared */
    if (!RxIsAnisotropicFilter(Desc.Filter))
        Desc.MaxAnisotropy = 0;
    if (!D3D11_DECODE_IS_COMPARISON_FILTER(Desc.Filter))
        Desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    if (Desc.AddressU != D3D11_TEXTURE_ADDRESS_BORDER && Desc.AddressV != D3D11_TEXTURE_ADDRESS_BORDER &&
        Desc.AddressW != D3D11_TEXTURE_ADDRESS_BORDER)
    {
        Desc.BorderColor[0] = 0.0f;
        Desc.BorderColor[1] = 0.0f;
        Desc.BorderColor[2] = 0.0f;
        Desc.BorderColor[3] = 0.0f;
    }

    return Desc;
}

D3D10_DDI_SAMPLER_DESC
RxSamplerTraits::ToDdi(
    _In_ const D3D11_SAMPLER_DESC& Desc)
{
    D3D10_DDI_SAMPLER_DESC Ddi = {};

    Ddi.Filter = (D3D10_DDI_FILTER)Desc.Filter;
    Ddi.AddressU = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc.AddressU;
    Ddi.AddressV = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc.AddressV;
    Ddi.AddressW = (D3D10_DDI_TEXTURE_ADDRESS_MODE)Desc.AddressW;
    Ddi.MipLODBias = Desc.MipLODBias;
    Ddi.MaxAnisotropy = Desc.MaxAnisotropy;
    Ddi.ComparisonFunc = (D3D10_DDI_COMPARISON_FUNC)Desc.ComparisonFunc;
    memcpy(Ddi.BorderColor, Desc.BorderColor, sizeof(Ddi.BorderColor));
    Ddi.MinLOD = Desc.MinLOD;
    Ddi.MaxLOD = Desc.MaxLOD;
    return Ddi;
}

/* The shared object */

template <typename Traits>
RxStateObject<Traits>::~RxStateObject()
{
    if (IsCreated)
        (this->Device.GetDeviceFuncs().*Traits::Destroy)(this->Device.GetDriverDevice(), Handle);
}

template <typename Traits>
HRESULT
RxStateObject<Traits>::Create(
    _In_ RxDevice& Device,
    _In_ const ApiDesc& Desc,
    _Outptr_ RxStateObject** State)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    typename Traits::DdiDesc Ddi;
    typename Traits::RuntimeHandle RuntimeHandle;
    RxStateObject* NewState;
    HRESULT hr;

    *State = nullptr;

    NewState = new (std::nothrow) RxStateObject(Device);
    if (NewState == nullptr)
        return E_OUTOFMEMORY;

    NewState->Desc = Traits::Normalize(Desc);
    Ddi = Traits::ToDdi(NewState->Desc);

    NewState->Handle.pDrvPrivate = NewState->Memory.Allocate((Funcs.*Traits::CalcSize)(Device.GetDriverDevice(), &Ddi));
    if (NewState->Handle.pDrvPrivate == nullptr)
    {
        NewState->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<Interface*>(NewState);
    (Funcs.*Traits::Create)(Device.GetDriverDevice(), &Ddi, NewState->Handle, RuntimeHandle);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD state create failed 0x%08lX", hr);
        NewState->Release();
        return hr;
    }

    NewState->IsCreated = TRUE;
    *State = NewState;
    return S_OK;
}

template <typename Traits>
HRESULT
STDMETHODCALLTYPE
RxStateObject<Traits>::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(typename Traits::BaseInterface) || riid == __uuidof(Interface))
    {
        *ppvObject = static_cast<Interface*>(this);
        this->AddRef();
        return S_OK;
    }

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

template class RxStateObject<RxBlendTraits>;
template class RxStateObject<RxDepthStencilTraits>;
template class RxStateObject<RxRasterizerTraits>;
template class RxStateObject<RxSamplerTraits>;
