/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline views over buffers and textures of every dimension
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "textures.hpp"

/*
 * Each view kind supplies its API desc, DDI argument and handle types, the driver
 * entries and Resolve, which fills in defaults and builds the DDI arguments.
 */
struct RxRenderTargetTraits
{
    using Interface = ID3D11RenderTargetView;
    using ApiDesc = D3D11_RENDER_TARGET_VIEW_DESC;
    using DdiArgs = D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW;
    using Handle = D3D10DDI_HRENDERTARGETVIEW;
    using RuntimeHandle = D3D10DDI_HRTRENDERTARGETVIEW;
    static constexpr UINT RequiredBind = D3D11_BIND_RENDER_TARGET;
    static constexpr GUID PrivateId = { 0x5d8e3a17, 0xc46b, 0x4f92, { 0x8a, 0x0e, 0x73, 0x2b, 0xd9, 0x41, 0x6c, 0xf5 } };
    static constexpr auto CalcSize = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCalcPrivateRenderTargetViewSize;
    static constexpr auto Create = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCreateRenderTargetView;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyRenderTargetView;

    static
    HRESULT
    Resolve(
        _In_ const RxResourceInfo& Resource,
        _In_opt_ const ApiDesc* Requested,
        _Out_ ApiDesc* Resolved,
        _Out_ DdiArgs* Args);
};

struct RxShaderResourceTraits
{
    using Interface = ID3D11ShaderResourceView;
    using ApiDesc = D3D11_SHADER_RESOURCE_VIEW_DESC;
    using DdiArgs = D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW;
    using Handle = D3D10DDI_HSHADERRESOURCEVIEW;
    using RuntimeHandle = D3D10DDI_HRTSHADERRESOURCEVIEW;
    static constexpr UINT RequiredBind = D3D11_BIND_SHADER_RESOURCE;
    static constexpr GUID PrivateId = { 0x2e64b9d1, 0x8f30, 0x4c57, { 0xa6, 0x1d, 0xc4, 0x05, 0x7e, 0x92, 0x3b, 0x68 } };
    static constexpr auto CalcSize = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCalcPrivateShaderResourceViewSize;
    static constexpr auto Create = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCreateShaderResourceView;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyShaderResourceView;

    static
    HRESULT
    Resolve(
        _In_ const RxResourceInfo& Resource,
        _In_opt_ const ApiDesc* Requested,
        _Out_ ApiDesc* Resolved,
        _Out_ DdiArgs* Args);
};

struct RxDepthStencilViewTraits
{
    using Interface = ID3D11DepthStencilView;
    using ApiDesc = D3D11_DEPTH_STENCIL_VIEW_DESC;
    using DdiArgs = D3D11DDIARG_CREATEDEPTHSTENCILVIEW;
    using Handle = D3D10DDI_HDEPTHSTENCILVIEW;
    using RuntimeHandle = D3D10DDI_HRTDEPTHSTENCILVIEW;
    static constexpr UINT RequiredBind = D3D11_BIND_DEPTH_STENCIL;
    static constexpr GUID PrivateId = { 0x9a07d52e, 0x41fc, 0x4b38, { 0x97, 0xe0, 0x5d, 0x2c, 0xa8, 0x16, 0xf4, 0x71 } };
    static constexpr auto CalcSize = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCalcPrivateDepthStencilViewSize;
    static constexpr auto Create = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCreateDepthStencilView;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyDepthStencilView;

    static
    HRESULT
    Resolve(
        _In_ const RxResourceInfo& Resource,
        _In_opt_ const ApiDesc* Requested,
        _Out_ ApiDesc* Resolved,
        _Out_ DdiArgs* Args);
};

struct RxUnorderedAccessTraits
{
    using Interface = ID3D11UnorderedAccessView;
    using ApiDesc = D3D11_UNORDERED_ACCESS_VIEW_DESC;
    using DdiArgs = D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW;
    using Handle = D3D11DDI_HUNORDEREDACCESSVIEW;
    using RuntimeHandle = D3D11DDI_HRTUNORDEREDACCESSVIEW;
    static constexpr UINT RequiredBind = D3D11_BIND_UNORDERED_ACCESS;
    static constexpr GUID PrivateId = { 0x61c2f7d9, 0x3e8a, 0x4b05, { 0x9d, 0x46, 0xa8, 0x1c, 0x75, 0x2e, 0xb0, 0x93 } };
    static constexpr auto CalcSize = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCalcPrivateUnorderedAccessViewSize;
    static constexpr auto Create = &D3DWDDM2_0DDI_DEVICEFUNCS::pfnCreateUnorderedAccessView;
    static constexpr auto Destroy = &D3DWDDM1_3DDI_DEVICEFUNCS::pfnDestroyUnorderedAccessView;

    static
    HRESULT
    Resolve(
        _In_ const RxResourceInfo& Resource,
        _In_opt_ const ApiDesc* Requested,
        _Out_ ApiDesc* Resolved,
        _Out_ DdiArgs* Args);
};

/*
 * From WDDM 2.0 the view entries take arguments whose 2D member ends in a plane slice.
 * The fields before it sit where older drivers read them, so one set serves every driver.
 */
static_assert(offsetof(D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW, Tex2D) == offsetof(D3D11DDIARG_CREATESHADERRESOURCEVIEW, Tex2D));
static_assert(offsetof(D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW, Tex2D) == offsetof(D3D10DDIARG_CREATERENDERTARGETVIEW, Tex2D));
static_assert(offsetof(D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW, Tex2D) == offsetof(D3D11DDIARG_CREATEUNORDEREDACCESSVIEW, Tex2D));

template <typename Traits>
class RxView final : public RxDeviceChild<typename Traits::Interface>
{
public:
    using Interface = typename Traits::Interface;
    using ApiDesc = typename Traits::ApiDesc;
    static constexpr GUID PrivateId = Traits::PrivateId;

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ ID3D11Resource* Resource,
        _In_opt_ const ApiDesc* Desc,
        _Outptr_ RxView** View);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    VOID
    STDMETHODCALLTYPE
    GetResource(
        _Outptr_ ID3D11Resource** ppResource) override
    {
        Resource->AddRef();
        *ppResource = Resource;
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ ApiDesc* pDesc) override
    {
        *pDesc = Desc;
    }

    typename Traits::Handle
    GetHandle() const
    {
        return Handle;
    }

    DdiResource*
    GetBacking() const
    {
        return Backing;
    }

    /* Tells the driver about writes to the resource since this view last read it */
    VOID
    NoteRead();

private:
    explicit RxView(
        _In_ RxDevice& Device)
        : RxDeviceChild<Interface>(Device)
    {
    }

    ~RxView() override;

    ID3D11Resource* Resource = nullptr;
    DdiResource* Backing = nullptr;
    ApiDesc Desc = {};
    RxDriverMemory Memory;
    typename Traits::Handle Handle = {};
    BOOLEAN IsCreated = FALSE;
    UINT64 SeenWrites = 0;
};

using RxRenderTargetView = RxView<RxRenderTargetTraits>;
using RxShaderResourceView = RxView<RxShaderResourceTraits>;
using RxDepthStencilView = RxView<RxDepthStencilViewTraits>;
using RxUnorderedAccessView = RxView<RxUnorderedAccessTraits>;

/* TRUE for the formats a view cannot use until a typed format is named */
BOOLEAN
RxIsTypelessFormat(
    _In_ DXGI_FORMAT Format);
