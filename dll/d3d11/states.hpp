/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Blend, depth stencil, rasterizer and sampler state objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "devicechild.hpp"

/*
 * Each state kind supplies its API desc, the DDI desc and handle types, the driver
 * entries, and Normalize/ToDdi. The normalized desc is what GetDesc returns.
 */
struct RxBlendTraits
{
    using Interface = ID3D11BlendState1;
    using MiddleInterface = ID3D11BlendState1;
    using BaseInterface = ID3D11BlendState;
    using ApiDesc = D3D11_BLEND_DESC1;
    using BaseDesc = D3D11_BLEND_DESC;
    using DdiDesc = D3D11_1_DDI_BLEND_DESC;
    using Handle = D3D10DDI_HBLENDSTATE;
    using RuntimeHandle = D3D10DDI_HRTBLENDSTATE;
    static constexpr GUID PrivateId = { 0x0b8e45d6, 0x37a1, 0x4f6e, { 0x91, 0xcd, 0x5e, 0x02, 0x7b, 0xa4, 0x18, 0x6c } };
    static constexpr auto CalcSize = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCalcPrivateBlendStateSize;
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateBlendState;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyBlendState;

    static
    ApiDesc
    Normalize(
        _In_ const ApiDesc& Desc);

    static
    DdiDesc
    ToDdi(
        _In_ const ApiDesc& Desc);

    /* Between the original desc and the one newer interfaces use */
    static
    ApiDesc
    Promote(
        _In_ const BaseDesc& Desc);

    static
    BaseDesc
    Demote(
        _In_ const ApiDesc& Desc);
};

struct RxDepthStencilTraits
{
    using Interface = ID3D11DepthStencilState;
    using MiddleInterface = ID3D11DepthStencilState;
    using BaseInterface = ID3D11DepthStencilState;
    using ApiDesc = D3D11_DEPTH_STENCIL_DESC;
    using BaseDesc = D3D11_DEPTH_STENCIL_DESC;
    using DdiDesc = D3D10_DDI_DEPTH_STENCIL_DESC;
    using Handle = D3D10DDI_HDEPTHSTENCILSTATE;
    using RuntimeHandle = D3D10DDI_HRTDEPTHSTENCILSTATE;
    static constexpr GUID PrivateId = { 0x7c2f19a0, 0xe56b, 0x4d83, { 0xa4, 0x3e, 0x18, 0xb9, 0x6d, 0x50, 0xc2, 0x77 } };
    static constexpr auto CalcSize = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCalcPrivateDepthStencilStateSize;
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateDepthStencilState;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyDepthStencilState;

    static
    ApiDesc
    Normalize(
        _In_ const ApiDesc& Desc);

    static
    DdiDesc
    ToDdi(
        _In_ const ApiDesc& Desc);

    /* Between the original desc and the one newer interfaces use */
    static
    ApiDesc
    Promote(
        _In_ const BaseDesc& Desc);

    static
    BaseDesc
    Demote(
        _In_ const ApiDesc& Desc);
};

/* The WDDM 2.0 rasterizer desc: the 1.1 one plus a conservative mode, which older drivers never read */
struct RxRasterizerDdiDesc : D3D11_1_DDI_RASTERIZER_DESC
{
    D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_MODE ConservativeRasterizationMode;
};
static_assert(sizeof(RxRasterizerDdiDesc) == sizeof(D3DWDDM2_0DDI_RASTERIZER_DESC));

struct RxRasterizerTraits
{
    using Interface = ID3D11RasterizerState2;
    using MiddleInterface = ID3D11RasterizerState1;
    using BaseInterface = ID3D11RasterizerState;
    using ApiDesc = D3D11_RASTERIZER_DESC2;
    using BaseDesc = D3D11_RASTERIZER_DESC;
    using DdiDesc = RxRasterizerDdiDesc;
    using Handle = D3D10DDI_HRASTERIZERSTATE;
    using RuntimeHandle = D3D10DDI_HRTRASTERIZERSTATE;
    static constexpr GUID PrivateId = { 0x94d1c63b, 0x08f2, 0x4a7d, { 0xbe, 0x55, 0x3c, 0x6e, 0x81, 0x2a, 0xf9, 0x04 } };
    static constexpr auto CalcSize = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCalcPrivateRasterizerStateSize;
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateRasterizerState;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyRasterizerState;

    static
    ApiDesc
    Normalize(
        _In_ const ApiDesc& Desc);

    static
    DdiDesc
    ToDdi(
        _In_ const ApiDesc& Desc);

    /* Between the original desc and the one newer interfaces use */
    static
    ApiDesc
    Promote(
        _In_ const BaseDesc& Desc);

    static
    BaseDesc
    Demote(
        _In_ const ApiDesc& Desc);
};

struct RxSamplerTraits
{
    using Interface = ID3D11SamplerState;
    using MiddleInterface = ID3D11SamplerState;
    using BaseInterface = ID3D11SamplerState;
    using ApiDesc = D3D11_SAMPLER_DESC;
    using BaseDesc = D3D11_SAMPLER_DESC;
    using DdiDesc = D3D10_DDI_SAMPLER_DESC;
    using Handle = D3D10DDI_HSAMPLER;
    using RuntimeHandle = D3D10DDI_HRTSAMPLER;
    static constexpr GUID PrivateId = { 0xe1734b98, 0x5cd0, 0x4e19, { 0x86, 0x2a, 0x9f, 0x31, 0x07, 0xd8, 0x6b, 0xe3 } };
    static constexpr auto CalcSize = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCalcPrivateSamplerSize;
    static constexpr auto Create = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnCreateSampler;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroySampler;

    static
    ApiDesc
    Normalize(
        _In_ const ApiDesc& Desc);

    static
    DdiDesc
    ToDdi(
        _In_ const ApiDesc& Desc);

    /* Between the original desc and the one newer interfaces use */
    static
    ApiDesc
    Promote(
        _In_ const BaseDesc& Desc);

    static
    BaseDesc
    Demote(
        _In_ const ApiDesc& Desc);
};

/* Holds the desc; the kinds with a second desc version also answer GetDesc1 from it */
template <typename Traits>
class RxStateBase : public RxDeviceChild<typename Traits::Interface>
{
public:
    explicit RxStateBase(
        _In_ RxDevice& Device)
        : RxDeviceChild<typename Traits::Interface>(Device)
    {
    }

protected:
    typename Traits::ApiDesc Desc = {};
};

template <>
class RxStateBase<RxBlendTraits> : public RxDeviceChild<ID3D11BlendState1>
{
public:
    explicit RxStateBase(
        _In_ RxDevice& Device)
        : RxDeviceChild<ID3D11BlendState1>(Device)
    {
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc1(
        _Out_ D3D11_BLEND_DESC1* pDesc) override
    {
        *pDesc = Desc;
    }

protected:
    D3D11_BLEND_DESC1 Desc = {};
};

template <>
class RxStateBase<RxRasterizerTraits> : public RxDeviceChild<ID3D11RasterizerState2>
{
public:
    explicit RxStateBase(
        _In_ RxDevice& Device)
        : RxDeviceChild<ID3D11RasterizerState2>(Device)
    {
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc1(
        _Out_ D3D11_RASTERIZER_DESC1* pDesc) override
    {
        /* The newest desc only appends ConservativeRaster */
        static_assert(offsetof(D3D11_RASTERIZER_DESC2, ConservativeRaster) == sizeof(D3D11_RASTERIZER_DESC1));
        memcpy(pDesc, &Desc, sizeof(*pDesc));
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc2(
        _Out_ D3D11_RASTERIZER_DESC2* pDesc) override
    {
        *pDesc = Desc;
    }

protected:
    D3D11_RASTERIZER_DESC2 Desc = {};
};

template <typename Traits>
class RxStateObject final : public RxStateBase<Traits>
{
public:
    using Interface = typename Traits::Interface;
    using ApiDesc = typename Traits::ApiDesc;
    static constexpr GUID PrivateId = Traits::PrivateId;

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const ApiDesc& Desc,
        _Outptr_ RxStateObject** State);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ typename Traits::BaseDesc* pDesc) override
    {
        *pDesc = Traits::Demote(this->Desc);
    }

    typename Traits::Handle
    GetHandle() const
    {
        return Handle;
    }

private:
    explicit RxStateObject(
        _In_ RxDevice& Device)
        : RxStateBase<Traits>(Device)
    {
    }

    ~RxStateObject() override;

    RxDriverMemory Memory;
    typename Traits::Handle Handle = {};
    BOOLEAN IsCreated = FALSE;
};

using RxBlendState = RxStateObject<RxBlendTraits>;
using RxDepthStencilState = RxStateObject<RxDepthStencilTraits>;
using RxRasterizerState = RxStateObject<RxRasterizerTraits>;
using RxSamplerState = RxStateObject<RxSamplerTraits>;

/* The documented defaults the pipeline starts with and returns to on ClearState */
D3D11_BLEND_DESC
RxDefaultBlendDesc(VOID);

D3D11_DEPTH_STENCIL_DESC
RxDefaultDepthStencilDesc(VOID);

D3D11_RASTERIZER_DESC
RxDefaultRasterizerDesc(VOID);

D3D11_SAMPLER_DESC
RxDefaultSamplerDesc(VOID);
