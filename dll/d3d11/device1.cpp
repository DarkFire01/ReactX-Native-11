/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11Device1 and ID3D11Device2 methods, and the multithread lock
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "queries.hpp"
#include "states.hpp"

/* Multithread protection */

template <typename Interface, typename Owner>
VOID
STDMETHODCALLTYPE
RxMultithread<Interface, Owner>::Enter(VOID)
{
    EnterCriticalSection(&Device.ThreadLock);
}

template <typename Interface, typename Owner>
VOID
STDMETHODCALLTYPE
RxMultithread<Interface, Owner>::Leave(VOID)
{
    LeaveCriticalSection(&Device.ThreadLock);
}

template <typename Interface, typename Owner>
BOOL
STDMETHODCALLTYPE
RxMultithread<Interface, Owner>::SetMultithreadProtected(
    _In_ BOOL bMTProtect)
{
    return InterlockedExchange(reinterpret_cast<LONG*>(&Device.IsThreadProtected), bMTProtect ? TRUE : FALSE);
}

template <typename Interface, typename Owner>
BOOL
STDMETHODCALLTYPE
RxMultithread<Interface, Owner>::GetMultithreadProtected(VOID)
{
    return Device.IsThreadProtected;
}

template class RxMultithread<ID3D10Multithread, RxDevice>;
template class RxMultithread<ID3D11Multithread, RxImmediateContext>;

/* ID3D11Device1 */

VOID
STDMETHODCALLTYPE
RxDevice::GetImmediateContext1(
    _Outptr_ ID3D11DeviceContext1** ppImmediateContext)
{
    Context.AddRef();
    *ppImmediateContext = &Context;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateBlendState1(
    _In_ const D3D11_BLEND_DESC1* pBlendStateDesc,
    _COM_Outptr_opt_ ID3D11BlendState1** ppBlendState)
{
    D3D11_FEATURE_DATA_D3D11_OPTIONS Options = {};
    RxBlendState* State;
    BOOLEAN UsesLogic = FALSE;
    HRESULT hr;

    if (ppBlendState != nullptr)
        *ppBlendState = nullptr;
    if (pBlendStateDesc == nullptr)
        return E_INVALIDARG;

    /* Logic ops replace blending, need driver support and apply to every target alike */
    for (const D3D11_RENDER_TARGET_BLEND_DESC1& Target : pBlendStateDesc->RenderTarget)
    {
        if (Target.LogicOpEnable && Target.BlendEnable)
            return E_INVALIDARG;
        UsesLogic |= Target.LogicOpEnable != FALSE;
    }

    if (UsesLogic)
    {
        CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &Options, sizeof(Options));
        if (!Options.OutputMergerLogicOp || pBlendStateDesc->IndependentBlendEnable)
            return E_INVALIDARG;
    }

    hr = RxBlendState::Create(*this, *pBlendStateDesc, &State);
    if (FAILED(hr))
        return hr;

    /* A null output pointer only validates the arguments */
    if (ppBlendState == nullptr)
    {
        State->Release();
        return S_FALSE;
    }

    *ppBlendState = State;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateRasterizerState1(
    _In_ const D3D11_RASTERIZER_DESC1* pRasterizerDesc,
    _COM_Outptr_opt_ ID3D11RasterizerState1** ppRasterizerState)
{
    RxRasterizerState* State;
    UINT Forced;
    HRESULT hr;

    if (ppRasterizerState != nullptr)
        *ppRasterizerState = nullptr;
    if (pRasterizerDesc == nullptr)
        return E_INVALIDARG;

    /* A forced count is a power of two up to 16 */
    Forced = pRasterizerDesc->ForcedSampleCount;
    if (Forced != 0 && (Forced > 16 || (Forced & (Forced - 1)) != 0))
        return E_INVALIDARG;

    hr = RxRasterizerState::Create(*this, *pRasterizerDesc, &State);
    if (FAILED(hr))
        return hr;

    if (ppRasterizerState == nullptr)
    {
        State->Release();
        return S_FALSE;
    }

    *ppRasterizerState = State;
    return S_OK;
}

/* ID3D11Device2 */

VOID
STDMETHODCALLTYPE
RxDevice::GetImmediateContext2(
    _Outptr_ ID3D11DeviceContext2** ppImmediateContext)
{
    Context.AddRef();
    *ppImmediateContext = &Context;
}

VOID
STDMETHODCALLTYPE
RxDevice::GetResourceTiling(
    _In_ ID3D11Resource* pTiledResource,
    _Out_opt_ UINT* pNumTilesForEntireResource,
    _Out_opt_ D3D11_PACKED_MIP_DESC* pPackedMipDesc,
    _Out_opt_ D3D11_TILE_SHAPE* pStandardTileShapeForNonPackedMips,
    _Inout_opt_ UINT* pNumSubresourceTilings,
    _In_ UINT FirstSubresourceTilingToGet,
    _Out_writes_(*pNumSubresourceTilings) D3D11_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips)
{
    UNREFERENCED_PARAMETER(pTiledResource);
    UNREFERENCED_PARAMETER(FirstSubresourceTilingToGet);
    UNREFERENCED_PARAMETER(pSubresourceTilingsForNonPackedMips);

    /* No resource here is tiled, which reads back as no tiles at all */
    if (pNumTilesForEntireResource != nullptr)
        *pNumTilesForEntireResource = 0;
    if (pPackedMipDesc != nullptr)
        *pPackedMipDesc = {};
    if (pStandardTileShapeForNonPackedMips != nullptr)
        *pStandardTileShapeForNonPackedMips = {};
    if (pNumSubresourceTilings != nullptr)
        *pNumSubresourceTilings = 0;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CheckMultisampleQualityLevels1(
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ UINT Flags,
    _Out_ UINT* pNumQualityLevels)
{
    if (pNumQualityLevels == nullptr)
        return E_INVALIDARG;

    *pNumQualityLevels = 0;
    if ((Flags & ~D3D11_CHECK_MULTISAMPLE_QUALITY_LEVELS_TILED_RESOURCE) != 0)
        return E_INVALIDARG;

    /* Tiled resources are not offered, so they have no quality levels */
    if (Flags & D3D11_CHECK_MULTISAMPLE_QUALITY_LEVELS_TILED_RESOURCE)
        return S_OK;

    return CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}
