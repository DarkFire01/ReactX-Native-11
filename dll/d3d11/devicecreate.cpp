/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11Device methods that create pipeline objects, and the device defaults
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"
#include "states.hpp"
#include "textures.hpp"
#include "views.hpp"
#include "resourcecheck.hpp"

/*
 * Hands a new object to the caller. A null output pointer means the caller only
 * wanted the arguments checked, which the API reports as S_FALSE. The object is
 * taken by reference: argument order is unspecified, so it is read only once the
 * create call that fills it has run.
 */
template <typename Object, typename Interface>
static
HRESULT
RxHandOut(
    _In_ HRESULT Result,
    _In_opt_ Object*& Created,
    _Out_opt_ Interface** Output,
    _In_z_ PCSTR Caller = __builtin_FUNCTION())
{
    if (FAILED(Result))
    {
        RX_TRACE(L"%S failed 0x%08lX", Caller, Result);
        return Result;
    }

    if (Output == nullptr)
    {
        Created->Release();
        return S_FALSE;
    }

    *Output = Created;
    return S_OK;
}

template <typename Interface>
static
VOID
RxClearOutput(
    _Out_opt_ Interface** Output)
{
    if (Output != nullptr)
        *Output = nullptr;
}

RxDevice::DefaultStates::DefaultStates() = default;
RxDevice::DefaultStates::~DefaultStates() = default;

HRESULT
RxDevice::BuildStockStates()
{
    RxBlendState* Blend;
    RxDepthStencilState* DepthStencil;
    RxRasterizerState* Rasterizer;
    RxSamplerState* Sampler;
    HRESULT hr;

    /*
     * Built through the same path the API uses. Only internal references are kept,
     * so the defaults do not keep their own device alive.
     */
    hr = RxBlendState::Create(*this, RxBlendTraits::Promote(RxDefaultBlendDesc()), &Blend);
    if (FAILED(hr))
        return hr;
    Defaults.Blend = RxBound<RxBlendState>(Blend);
    Blend->Release();

    hr = RxDepthStencilState::Create(*this, RxDefaultDepthStencilDesc(), &DepthStencil);
    if (FAILED(hr))
        return hr;
    Defaults.DepthStencil = RxBound<RxDepthStencilState>(DepthStencil);
    DepthStencil->Release();

    hr = RxRasterizerState::Create(*this, RxRasterizerTraits::Promote(RxDefaultRasterizerDesc()), &Rasterizer);
    if (FAILED(hr))
        return hr;
    Defaults.Rasterizer = RxBound<RxRasterizerState>(Rasterizer);
    Rasterizer->Release();

    hr = RxSamplerState::Create(*this, RxDefaultSamplerDesc(), &Sampler);
    if (FAILED(hr))
        return hr;
    Defaults.Sampler = RxBound<RxSamplerState>(Sampler);
    Sampler->Release();

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateBuffer(
    _In_ const D3D11_BUFFER_DESC* pDesc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* pInitialData,
    _COM_Outptr_opt_ ID3D11Buffer** ppBuffer)
{
    RxBuffer* Buffer = nullptr;

    RxClearOutput(ppBuffer);
    if (pDesc == nullptr || pDesc->ByteWidth == 0 ||
        (pDesc->Usage == D3D11_USAGE_IMMUTABLE && pInitialData == nullptr) ||
        ((pDesc->BindFlags & D3D11_BIND_CONSTANT_BUFFER) && (pDesc->ByteWidth % 16) != 0))
    {
        return E_INVALIDARG;
    }

    return RxHandOut(RxBuffer::Create(*this, *pDesc, pInitialData, &Buffer), Buffer, ppBuffer);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateVertexShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11VertexShader** ppVertexShader)
{
    RxVertexShader* Shader = nullptr;

    RxClearOutput(ppVertexShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxVertexShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppVertexShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreatePixelShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11PixelShader** ppPixelShader)
{
    RxPixelShader* Shader = nullptr;

    RxClearOutput(ppPixelShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxPixelShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppPixelShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateGeometryShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11GeometryShader** ppGeometryShader)
{
    RxGeometryShader* Shader = nullptr;

    RxClearOutput(ppGeometryShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxGeometryShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppGeometryShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateHullShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11HullShader** ppHullShader)
{
    RxHullShader* Shader = nullptr;

    RxClearOutput(ppHullShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxHullShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppHullShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateDomainShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11DomainShader** ppDomainShader)
{
    RxDomainShader* Shader = nullptr;

    RxClearOutput(ppDomainShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxDomainShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppDomainShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateComputeShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11ComputeShader** ppComputeShader)
{
    RxComputeShader* Shader = nullptr;

    RxClearOutput(ppComputeShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxComputeShader::Create(*this, pShaderBytecode, BytecodeLength, pClassLinkage, &Shader), Shader,
                     ppComputeShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateGeometryShaderWithStreamOutput(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_reads_opt_(NumEntries) const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
    _In_range_(0, D3D11_SO_STREAM_COUNT * D3D11_SO_OUTPUT_COMPONENT_COUNT) UINT NumEntries,
    _In_reads_opt_(NumStrides) const UINT* pBufferStrides,
    _In_range_(0, D3D11_SO_BUFFER_SLOT_COUNT) UINT NumStrides,
    _In_ UINT RasterizedStream,
    _In_opt_ ID3D11ClassLinkage* pClassLinkage,
    _COM_Outptr_opt_ ID3D11GeometryShader** ppGeometryShader)
{
    RxStreamOutputDesc StreamOutput = { pSODeclaration, NumEntries, pBufferStrides, NumStrides, RasterizedStream };
    RxGeometryShader* Shader = nullptr;

    RxClearOutput(ppGeometryShader);
    if (pShaderBytecode == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxGeometryShader::CreateWithStreamOutput(*this, pShaderBytecode, BytecodeLength, StreamOutput,
                                                              pClassLinkage, &Shader),
                     Shader, ppGeometryShader);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateInputLayout(
    _In_reads_(NumElements) const D3D11_INPUT_ELEMENT_DESC* pInputElementDescs,
    _In_range_(0, D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) UINT NumElements,
    _In_reads_(BytecodeLength) const void* pShaderBytecodeWithInputSignature,
    _In_ SIZE_T BytecodeLength,
    _COM_Outptr_opt_ ID3D11InputLayout** ppInputLayout)
{
    RxInputLayout* Layout = nullptr;

    RxClearOutput(ppInputLayout);
    if (pShaderBytecodeWithInputSignature == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxInputLayout::Create(*this, pInputElementDescs, NumElements, pShaderBytecodeWithInputSignature,
                                           BytecodeLength, &Layout),
                     Layout, ppInputLayout);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateShaderResourceView(
    _In_ ID3D11Resource* pResource,
    _In_opt_ const D3D11_SHADER_RESOURCE_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11ShaderResourceView** ppSRView)
{
    RxShaderResourceView* View = nullptr;

    RxClearOutput(ppSRView);
    if (pResource == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxShaderResourceView::Create(*this, pResource, pDesc, &View), View, ppSRView);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateDepthStencilView(
    _In_ ID3D11Resource* pResource,
    _In_opt_ const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11DepthStencilView** ppDepthStencilView)
{
    RxDepthStencilView* View = nullptr;

    RxClearOutput(ppDepthStencilView);
    if (pResource == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxDepthStencilView::Create(*this, pResource, pDesc, &View), View, ppDepthStencilView);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateBlendState(
    _In_ const D3D11_BLEND_DESC* pBlendStateDesc,
    _COM_Outptr_opt_ ID3D11BlendState** ppBlendState)
{
    RxBlendState* State = nullptr;

    RxClearOutput(ppBlendState);
    if (pBlendStateDesc == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxBlendState::Create(*this, RxBlendTraits::Promote(*pBlendStateDesc), &State), State, ppBlendState);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateDepthStencilState(
    _In_ const D3D11_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    _COM_Outptr_opt_ ID3D11DepthStencilState** ppDepthStencilState)
{
    RxDepthStencilState* State = nullptr;

    RxClearOutput(ppDepthStencilState);
    if (pDepthStencilDesc == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxDepthStencilState::Create(*this, *pDepthStencilDesc, &State), State, ppDepthStencilState);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateRasterizerState(
    _In_ const D3D11_RASTERIZER_DESC* pRasterizerDesc,
    _COM_Outptr_opt_ ID3D11RasterizerState** ppRasterizerState)
{
    RxRasterizerState* State = nullptr;

    RxClearOutput(ppRasterizerState);
    if (pRasterizerDesc == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxRasterizerState::Create(*this, RxRasterizerTraits::Promote(*pRasterizerDesc), &State), State, ppRasterizerState);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateSamplerState(
    _In_ const D3D11_SAMPLER_DESC* pSamplerDesc,
    _COM_Outptr_opt_ ID3D11SamplerState** ppSamplerState)
{
    RxSamplerState* State = nullptr;

    RxClearOutput(ppSamplerState);
    if (pSamplerDesc == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxSamplerState::Create(*this, *pSamplerDesc, &State), State, ppSamplerState);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateTexture1D(
    _In_ const D3D11_TEXTURE1D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels * pDesc->ArraySize)) const D3D11_SUBRESOURCE_DATA* pInitialData,
    _COM_Outptr_opt_ ID3D11Texture1D** ppTexture1D)
{
    RxTexture1D* Texture = nullptr;

    RxClearOutput(ppTexture1D);
    if (pDesc == nullptr || FAILED(RxCheckTextureRequest(*this, RxMakeTextureRequest(*pDesc), pInitialData)))
        return E_INVALIDARG;

    return RxHandOut(RxTexture1D::Create(*this, *pDesc, pInitialData, &Texture), Texture, ppTexture1D);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateTexture3D(
    _In_ const D3D11_TEXTURE3D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels)) const D3D11_SUBRESOURCE_DATA* pInitialData,
    _COM_Outptr_opt_ ID3D11Texture3D** ppTexture3D)
{
    RxTexture3D* Texture = nullptr;

    RxClearOutput(ppTexture3D);
    if (pDesc == nullptr || FAILED(RxCheckTextureRequest(*this, RxMakeTextureRequest(*pDesc), pInitialData)))
        return E_INVALIDARG;

    return RxHandOut(RxTexture3D::Create(*this, *pDesc, pInitialData, &Texture), Texture, ppTexture3D);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateUnorderedAccessView(
    _In_ ID3D11Resource* pResource,
    _In_opt_ const D3D11_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11UnorderedAccessView** ppUAView)
{
    RxUnorderedAccessView* View = nullptr;

    RxClearOutput(ppUAView);
    if (pResource == nullptr)
        return E_INVALIDARG;

    return RxHandOut(RxUnorderedAccessView::Create(*this, pResource, pDesc, &View), View, ppUAView);
}
