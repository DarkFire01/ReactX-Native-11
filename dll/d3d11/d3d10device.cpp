/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D10Device1 methods, carried out on the Direct3D 11 device and immediate context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10convert.hpp"

#include <algorithm>

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxD3D10Device::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxD3D10Device::Release(VOID)
{
    return Device.Release();
}

RxImmediateContext&
RxD3D10Device::Context()
{
    return Device.GetContext();
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSSetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D10Buffer* const* ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    for (Index = 0; Index < NumBuffers; Index++)
        Buffers[Index] = ppConstantBuffers ? RxTo11<ID3D11Buffer>(ppConstantBuffers[Index]) : nullptr;
    Context().VSSetConstantBuffers(StartSlot, NumBuffers, Buffers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSSetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D10ShaderResourceView* const* ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    UINT Index;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    for (Index = 0; Index < NumViews; Index++)
        Views[Index] = ppShaderResourceViews ? RxTo11<ID3D11ShaderResourceView>(ppShaderResourceViews[Index]) : nullptr;
    Context().PSSetShaderResources(StartSlot, NumViews, Views);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSSetShader(
    _In_opt_ ID3D10PixelShader* pPixelShader)
{
    Context().PSSetShader(RxTo11<ID3D11PixelShader>(pPixelShader), nullptr, 0);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSSetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D10SamplerState* const* ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    UINT Index;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    for (Index = 0; Index < NumSamplers; Index++)
        Samplers[Index] = ppSamplers ? RxTo11<ID3D11SamplerState>(ppSamplers[Index]) : nullptr;
    Context().PSSetSamplers(StartSlot, NumSamplers, Samplers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSSetShader(
    _In_opt_ ID3D10VertexShader* pVertexShader)
{
    Context().VSSetShader(RxTo11<ID3D11VertexShader>(pVertexShader), nullptr, 0);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::DrawIndexed(
    _In_ UINT IndexCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation)
{
    Context().DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::Draw(
    _In_ UINT VertexCount,
    _In_ UINT StartVertexLocation)
{
    Context().Draw(VertexCount, StartVertexLocation);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSSetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D10Buffer* const* ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    for (Index = 0; Index < NumBuffers; Index++)
        Buffers[Index] = ppConstantBuffers ? RxTo11<ID3D11Buffer>(ppConstantBuffers[Index]) : nullptr;
    Context().PSSetConstantBuffers(StartSlot, NumBuffers, Buffers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IASetInputLayout(
    _In_opt_ ID3D10InputLayout* pInputLayout)
{
    Context().IASetInputLayout(RxTo11<ID3D11InputLayout>(pInputLayout));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IASetVertexBuffers(
    _In_range_(0, D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D10Buffer* const* ppVertexBuffers,
    _In_reads_opt_(NumBuffers) const UINT* pStrides,
    _In_reads_opt_(NumBuffers) const UINT* pOffsets)
{
    ID3D11Buffer* Buffers[D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    for (Index = 0; Index < NumBuffers; Index++)
        Buffers[Index] = ppVertexBuffers ? RxTo11<ID3D11Buffer>(ppVertexBuffers[Index]) : nullptr;
    Context().IASetVertexBuffers(StartSlot, NumBuffers, Buffers, pStrides, pOffsets);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IASetIndexBuffer(
    _In_opt_ ID3D10Buffer* pIndexBuffer,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Offset)
{
    Context().IASetIndexBuffer(RxTo11<ID3D11Buffer>(pIndexBuffer), Format, Offset);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::DrawIndexedInstanced(
    _In_ UINT IndexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndexLocation,
    _In_ INT BaseVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    Context().DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                                   StartInstanceLocation);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::DrawInstanced(
    _In_ UINT VertexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartVertexLocation,
    _In_ UINT StartInstanceLocation)
{
    Context().DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSSetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D10Buffer* const* ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT];
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    for (Index = 0; Index < NumBuffers; Index++)
        Buffers[Index] = ppConstantBuffers ? RxTo11<ID3D11Buffer>(ppConstantBuffers[Index]) : nullptr;
    Context().GSSetConstantBuffers(StartSlot, NumBuffers, Buffers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSSetShader(
    _In_opt_ ID3D10GeometryShader* pShader)
{
    Context().GSSetShader(RxTo11<ID3D11GeometryShader>(pShader), nullptr, 0);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IASetPrimitiveTopology(
    _In_ D3D10_PRIMITIVE_TOPOLOGY Topology)
{
    Context().IASetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY)Topology);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSSetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D10ShaderResourceView* const* ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    UINT Index;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    for (Index = 0; Index < NumViews; Index++)
        Views[Index] = ppShaderResourceViews ? RxTo11<ID3D11ShaderResourceView>(ppShaderResourceViews[Index]) : nullptr;
    Context().VSSetShaderResources(StartSlot, NumViews, Views);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSSetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D10SamplerState* const* ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    UINT Index;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    for (Index = 0; Index < NumSamplers; Index++)
        Samplers[Index] = ppSamplers ? RxTo11<ID3D11SamplerState>(ppSamplers[Index]) : nullptr;
    Context().VSSetSamplers(StartSlot, NumSamplers, Samplers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::SetPredication(
    _In_opt_ ID3D10Predicate* pPredicate,
    _In_ BOOL PredicateValue)
{
    Context().SetPredication(RxTo11<ID3D11Predicate>(pPredicate), PredicateValue);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSSetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D10ShaderResourceView* const* ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT];
    UINT Index;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    for (Index = 0; Index < NumViews; Index++)
        Views[Index] = ppShaderResourceViews ? RxTo11<ID3D11ShaderResourceView>(ppShaderResourceViews[Index]) : nullptr;
    Context().GSSetShaderResources(StartSlot, NumViews, Views);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSSetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D10SamplerState* const* ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    UINT Index;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    for (Index = 0; Index < NumSamplers; Index++)
        Samplers[Index] = ppSamplers ? RxTo11<ID3D11SamplerState>(ppSamplers[Index]) : nullptr;
    Context().GSSetSamplers(StartSlot, NumSamplers, Samplers);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMSetRenderTargets(
    _In_range_(0, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D10RenderTargetView* const* ppRenderTargetViews,
    _In_opt_ ID3D10DepthStencilView* pDepthStencilView)
{
    ID3D11RenderTargetView* Views[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT];
    UINT Index;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    for (Index = 0; Index < NumViews; Index++)
        Views[Index] = ppRenderTargetViews ? RxTo11<ID3D11RenderTargetView>(ppRenderTargetViews[Index]) : nullptr;
    Context().OMSetRenderTargets(NumViews, Views, RxTo11<ID3D11DepthStencilView>(pDepthStencilView));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMSetBlendState(
    _In_opt_ ID3D10BlendState* pBlendState,
    _In_ const FLOAT BlendFactor[ 4 ],
    _In_ UINT SampleMask)
{
    Context().OMSetBlendState(RxTo11<ID3D11BlendState>(pBlendState), BlendFactor, SampleMask);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMSetDepthStencilState(
    _In_opt_ ID3D10DepthStencilState* pDepthStencilState,
    _In_ UINT StencilRef)
{
    Context().OMSetDepthStencilState(RxTo11<ID3D11DepthStencilState>(pDepthStencilState), StencilRef);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::SOSetTargets(
    _In_range_(0, D3D10_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D10Buffer* const* ppSOTargets,
    _In_reads_opt_(NumBuffers) const UINT* pOffsets)
{
    ID3D11Buffer* Buffers[D3D10_SO_BUFFER_SLOT_COUNT];
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    for (Index = 0; Index < NumBuffers; Index++)
        Buffers[Index] = ppSOTargets ? RxTo11<ID3D11Buffer>(ppSOTargets[Index]) : nullptr;
    Context().SOSetTargets(NumBuffers, Buffers, pOffsets);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::DrawAuto(VOID)
{
    Context().DrawAuto();
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSSetState(
    _In_opt_ ID3D10RasterizerState* pRasterizerState)
{
    Context().RSSetState(RxTo11<ID3D11RasterizerState>(pRasterizerState));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSSetViewports(
    _In_range_(0, D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_reads_opt_(NumViewports) const D3D10_VIEWPORT* pViewports)
{
    D3D11_VIEWPORT Viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT Index;

    NumViewports = std::min<UINT>(NumViewports, _countof(Viewports));
    for (Index = 0; pViewports != nullptr && Index < NumViewports; Index++)
        Viewports[Index] = RxToD3D11Viewport(pViewports[Index]);
    Context().RSSetViewports(NumViewports, pViewports ? Viewports : nullptr);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSSetScissorRects(
    _In_range_(0, D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_reads_opt_(NumRects) const D3D10_RECT* pRects)
{
    Context().RSSetScissorRects(NumRects, pRects);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::CopySubresourceRegion(
    _In_ ID3D10Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ UINT DstX,
    _In_ UINT DstY,
    _In_ UINT DstZ,
    _In_ ID3D10Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_opt_ const D3D10_BOX* pSrcBox)
{
    Context().CopySubresourceRegion(RxTo11<ID3D11Resource>(pDstResource), DstSubresource, DstX, DstY, DstZ,
                                    RxTo11<ID3D11Resource>(pSrcResource), SrcSubresource,
                                    reinterpret_cast<const D3D11_BOX*>(pSrcBox));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::CopyResource(
    _In_ ID3D10Resource* pDstResource,
    _In_ ID3D10Resource* pSrcResource)
{
    Context().CopyResource(RxTo11<ID3D11Resource>(pDstResource), RxTo11<ID3D11Resource>(pSrcResource));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::UpdateSubresource(
    _In_ ID3D10Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_opt_ const D3D10_BOX* pDstBox,
    _In_ const void* pSrcData,
    _In_ UINT SrcRowPitch,
    _In_ UINT SrcDepthPitch)
{
    Context().UpdateSubresource(RxTo11<ID3D11Resource>(pDstResource), DstSubresource,
                                reinterpret_cast<const D3D11_BOX*>(pDstBox), pSrcData, SrcRowPitch, SrcDepthPitch);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::ClearRenderTargetView(
    _In_ ID3D10RenderTargetView* pRenderTargetView,
    _In_ const FLOAT ColorRGBA[ 4 ])
{
    Context().ClearRenderTargetView(RxTo11<ID3D11RenderTargetView>(pRenderTargetView), ColorRGBA);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::ClearDepthStencilView(
    _In_ ID3D10DepthStencilView* pDepthStencilView,
    _In_ UINT ClearFlags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil)
{
    Context().ClearDepthStencilView(RxTo11<ID3D11DepthStencilView>(pDepthStencilView), ClearFlags, Depth, Stencil);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GenerateMips(
    _In_ ID3D10ShaderResourceView* pShaderResourceView)
{
    Context().GenerateMips(RxTo11<ID3D11ShaderResourceView>(pShaderResourceView));
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::ResolveSubresource(
    _In_ ID3D10Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ ID3D10Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_ DXGI_FORMAT Format)
{
    Context().ResolveSubresource(RxTo11<ID3D11Resource>(pDstResource), DstSubresource,
                                 RxTo11<ID3D11Resource>(pSrcResource), SrcSubresource, Format);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSGetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D10Buffer** ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    UINT Index;

    if (ppConstantBuffers == nullptr)
        return;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    Context().VSGetConstantBuffers(StartSlot, NumBuffers, Buffers);
    for (Index = 0; Index < NumBuffers; Index++)
        ppConstantBuffers[Index] = RxTo10<ID3D10Buffer>(Buffers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSGetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D10ShaderResourceView** ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    UINT Index;

    if (ppShaderResourceViews == nullptr)
        return;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    Context().PSGetShaderResources(StartSlot, NumViews, Views);
    for (Index = 0; Index < NumViews; Index++)
        ppShaderResourceViews[Index] = RxTo10<ID3D10ShaderResourceView>(Views[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSGetShader(
    _Out_ ID3D10PixelShader** ppPixelShader)
{
    ID3D11PixelShader* Shader = nullptr;

    Context().PSGetShader(&Shader, nullptr, nullptr);
    *ppPixelShader = RxTo10<ID3D10PixelShader>(Shader);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSGetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D10SamplerState** ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    UINT Index;

    if (ppSamplers == nullptr)
        return;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    Context().PSGetSamplers(StartSlot, NumSamplers, Samplers);
    for (Index = 0; Index < NumSamplers; Index++)
        ppSamplers[Index] = RxTo10<ID3D10SamplerState>(Samplers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSGetShader(
    _Out_ ID3D10VertexShader** ppVertexShader)
{
    ID3D11VertexShader* Shader = nullptr;

    Context().VSGetShader(&Shader, nullptr, nullptr);
    *ppVertexShader = RxTo10<ID3D10VertexShader>(Shader);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::PSGetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D10Buffer** ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    UINT Index;

    if (ppConstantBuffers == nullptr)
        return;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    Context().PSGetConstantBuffers(StartSlot, NumBuffers, Buffers);
    for (Index = 0; Index < NumBuffers; Index++)
        ppConstantBuffers[Index] = RxTo10<ID3D10Buffer>(Buffers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IAGetInputLayout(
    _Out_ ID3D10InputLayout** ppInputLayout)
{
    ID3D11InputLayout* Layout = nullptr;

    Context().IAGetInputLayout(&Layout);
    *ppInputLayout = RxTo10<ID3D10InputLayout>(Layout);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IAGetVertexBuffers(
    _In_range_(0, D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D10Buffer** ppVertexBuffers,
    _Out_writes_opt_(NumBuffers) UINT* pStrides,
    _Out_writes_opt_(NumBuffers) UINT* pOffsets)
{
    ID3D11Buffer* Buffers[D3D10_1_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    UINT Index;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    Context().IAGetVertexBuffers(StartSlot, NumBuffers, Buffers, pStrides, pOffsets);
    for (Index = 0; Index < NumBuffers; Index++)
    {
        if (ppVertexBuffers != nullptr)
            ppVertexBuffers[Index] = RxTo10<ID3D10Buffer>(Buffers[Index]);
        else if (Buffers[Index] != nullptr)
            Buffers[Index]->Release();
    }
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IAGetIndexBuffer(
    _Out_opt_ ID3D10Buffer** pIndexBuffer,
    _Out_opt_ DXGI_FORMAT* Format,
    _Out_opt_ UINT* Offset)
{
    ID3D11Buffer* Buffer = nullptr;

    Context().IAGetIndexBuffer(&Buffer, Format, Offset);
    if (pIndexBuffer != nullptr)
        *pIndexBuffer = RxTo10<ID3D10Buffer>(Buffer);
    else if (Buffer != nullptr)
        Buffer->Release();
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSGetConstantBuffers(
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D10Buffer** ppConstantBuffers)
{
    ID3D11Buffer* Buffers[D3D10_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    UINT Index;

    if (ppConstantBuffers == nullptr)
        return;

    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    Context().GSGetConstantBuffers(StartSlot, NumBuffers, Buffers);
    for (Index = 0; Index < NumBuffers; Index++)
        ppConstantBuffers[Index] = RxTo10<ID3D10Buffer>(Buffers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSGetShader(
    _Out_ ID3D10GeometryShader** ppGeometryShader)
{
    ID3D11GeometryShader* Shader = nullptr;

    Context().GSGetShader(&Shader, nullptr, nullptr);
    *ppGeometryShader = RxTo10<ID3D10GeometryShader>(Shader);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::IAGetPrimitiveTopology(
    _Out_ D3D10_PRIMITIVE_TOPOLOGY* pTopology)
{
    D3D11_PRIMITIVE_TOPOLOGY Topology;

    Context().IAGetPrimitiveTopology(&Topology);
    *pTopology = (D3D10_PRIMITIVE_TOPOLOGY)Topology;
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSGetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D10ShaderResourceView** ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    UINT Index;

    if (ppShaderResourceViews == nullptr)
        return;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    Context().VSGetShaderResources(StartSlot, NumViews, Views);
    for (Index = 0; Index < NumViews; Index++)
        ppShaderResourceViews[Index] = RxTo10<ID3D10ShaderResourceView>(Views[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::VSGetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D10SamplerState** ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    UINT Index;

    if (ppSamplers == nullptr)
        return;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    Context().VSGetSamplers(StartSlot, NumSamplers, Samplers);
    for (Index = 0; Index < NumSamplers; Index++)
        ppSamplers[Index] = RxTo10<ID3D10SamplerState>(Samplers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GetPredication(
    _Out_opt_ ID3D10Predicate** ppPredicate,
    _Out_opt_ BOOL* pPredicateValue)
{
    ID3D11Predicate* Predicate = nullptr;

    Context().GetPredication(ppPredicate ? &Predicate : nullptr, pPredicateValue);
    if (ppPredicate != nullptr)
        *ppPredicate = RxTo10<ID3D10Predicate>(Predicate);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSGetShaderResources(
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D10ShaderResourceView** ppShaderResourceViews)
{
    ID3D11ShaderResourceView* Views[D3D10_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    UINT Index;

    if (ppShaderResourceViews == nullptr)
        return;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    Context().GSGetShaderResources(StartSlot, NumViews, Views);
    for (Index = 0; Index < NumViews; Index++)
        ppShaderResourceViews[Index] = RxTo10<ID3D10ShaderResourceView>(Views[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GSGetSamplers(
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D10SamplerState** ppSamplers)
{
    ID3D11SamplerState* Samplers[D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    UINT Index;

    if (ppSamplers == nullptr)
        return;

    NumSamplers = std::min<UINT>(NumSamplers, _countof(Samplers));
    Context().GSGetSamplers(StartSlot, NumSamplers, Samplers);
    for (Index = 0; Index < NumSamplers; Index++)
        ppSamplers[Index] = RxTo10<ID3D10SamplerState>(Samplers[Index]);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMGetRenderTargets(
    _In_range_(0, D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D10RenderTargetView** ppRenderTargetViews,
    _Out_opt_ ID3D10DepthStencilView** ppDepthStencilView)
{
    ID3D11RenderTargetView* Views[D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* Depth = nullptr;
    UINT Index;

    NumViews = std::min<UINT>(NumViews, _countof(Views));
    Context().OMGetRenderTargets(ppRenderTargetViews ? NumViews : 0, ppRenderTargetViews ? Views : nullptr,
                                 ppDepthStencilView ? &Depth : nullptr);
    for (Index = 0; ppRenderTargetViews != nullptr && Index < NumViews; Index++)
        ppRenderTargetViews[Index] = RxTo10<ID3D10RenderTargetView>(Views[Index]);
    if (ppDepthStencilView != nullptr)
        *ppDepthStencilView = RxTo10<ID3D10DepthStencilView>(Depth);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMGetBlendState(
    _Out_opt_ ID3D10BlendState** ppBlendState,
    _Out_opt_ FLOAT BlendFactor[ 4 ],
    _Out_opt_ UINT* pSampleMask)
{
    ID3D11BlendState* State = nullptr;

    Context().OMGetBlendState(ppBlendState ? &State : nullptr, BlendFactor, pSampleMask);
    if (ppBlendState != nullptr)
        *ppBlendState = RxTo10<ID3D10BlendState>(State);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::OMGetDepthStencilState(
    _Out_opt_ ID3D10DepthStencilState** ppDepthStencilState,
    _Out_opt_ UINT* pStencilRef)
{
    ID3D11DepthStencilState* State = nullptr;

    Context().OMGetDepthStencilState(ppDepthStencilState ? &State : nullptr, pStencilRef);
    if (ppDepthStencilState != nullptr)
        *ppDepthStencilState = RxTo10<ID3D10DepthStencilState>(State);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::SOGetTargets(
    _In_range_(0, D3D10_SO_BUFFER_SLOT_COUNT) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D10Buffer** ppSOTargets,
    _Out_writes_opt_(NumBuffers) UINT* pOffsets)
{
    ID3D11Buffer* Buffers[D3D10_SO_BUFFER_SLOT_COUNT] = {};
    UINT Index;

    /* Direct3D 11 no longer reports the offsets */
    NumBuffers = std::min<UINT>(NumBuffers, _countof(Buffers));
    Context().SOGetTargets(NumBuffers, Buffers);
    for (Index = 0; Index < NumBuffers; Index++)
    {
        if (ppSOTargets != nullptr)
            ppSOTargets[Index] = RxTo10<ID3D10Buffer>(Buffers[Index]);
        else if (Buffers[Index] != nullptr)
            Buffers[Index]->Release();
        if (pOffsets != nullptr)
            pOffsets[Index] = 0;
    }
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSGetState(
    _Out_ ID3D10RasterizerState** ppRasterizerState)
{
    ID3D11RasterizerState* State = nullptr;

    Context().RSGetState(&State);
    *ppRasterizerState = RxTo10<ID3D10RasterizerState>(State);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSGetViewports(
    _Inout_ UINT* NumViewports,
    _Out_writes_opt_(*NumViewports) D3D10_VIEWPORT* pViewports)
{
    D3D11_VIEWPORT Viewports[D3D10_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT Count = std::min<UINT>(*NumViewports, _countof(Viewports));
    UINT Index;

    if (pViewports == nullptr)
    {
        Context().RSGetViewports(NumViewports, nullptr);
        return;
    }

    Context().RSGetViewports(&Count, Viewports);
    for (Index = 0; Index < Count; Index++)
        pViewports[Index] = RxToD3D10Viewport(Viewports[Index]);
    *NumViewports = Count;
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::RSGetScissorRects(
    _Inout_ UINT* NumRects,
    _Out_writes_opt_(*NumRects) D3D10_RECT* pRects)
{
    Context().RSGetScissorRects(NumRects, pRects);
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::GetDeviceRemovedReason(VOID)
{
    return Device.GetDeviceRemovedReason();
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::SetExceptionMode(
    UINT RaiseFlags)
{
    return Device.SetExceptionMode(RaiseFlags);
}

UINT
STDMETHODCALLTYPE
RxD3D10Device::GetExceptionMode(VOID)
{
    return Device.GetExceptionMode();
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::GetPrivateData(
    _In_ REFGUID guid,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_opt_(*pDataSize) void* pData)
{
    return Device.GetPrivateData(guid, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::SetPrivateData(
    _In_ REFGUID guid,
    _In_ UINT DataSize,
    _In_reads_bytes_opt_(DataSize) const void* pData)
{
    return Device.SetPrivateData(guid, DataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::SetPrivateDataInterface(
    _In_ REFGUID guid,
    _In_opt_ const IUnknown* pData)
{
    return Device.SetPrivateDataInterface(guid, pData);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::ClearState(VOID)
{
    Context().ClearState();
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::Flush(VOID)
{
    Context().Flush();
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateBuffer(
    _In_ const D3D10_BUFFER_DESC* pDesc,
    _In_opt_ const D3D10_SUBRESOURCE_DATA* pInitialData,
    _Out_opt_ ID3D10Buffer** ppBuffer)
{
    D3D11_BUFFER_DESC Desc;
    ID3D11Buffer* Buffer = nullptr;
    HRESULT hr;

    if (ppBuffer != nullptr)
        *ppBuffer = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    Desc = RxToD3D11Desc(*pDesc);
    hr = Device.CreateBuffer(&Desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(pInitialData),
                             ppBuffer ? &Buffer : nullptr);
    if (SUCCEEDED(hr) && ppBuffer != nullptr)
        *ppBuffer = RxTo10<ID3D10Buffer>(Buffer);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateTexture1D(
    _In_ const D3D10_TEXTURE1D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels* pDesc->ArraySize)) const D3D10_SUBRESOURCE_DATA* pInitialData,
    _Out_ ID3D10Texture1D** ppTexture1D)
{
    D3D11_TEXTURE1D_DESC Desc;
    ID3D11Texture1D* Created = nullptr;
    HRESULT hr;

    if (ppTexture1D != nullptr)
        *ppTexture1D = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    Desc = RxToD3D11Desc(*pDesc);
    hr = Device.CreateTexture1D(&Desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(pInitialData), ppTexture1D ? &Created : nullptr);
    if (SUCCEEDED(hr) && ppTexture1D != nullptr)
        *ppTexture1D = RxTo10<ID3D10Texture1D>(Created);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateTexture2D(
    _In_ const D3D10_TEXTURE2D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels* pDesc->ArraySize)) const D3D10_SUBRESOURCE_DATA* pInitialData,
    _Out_ ID3D10Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC Desc;
    ID3D11Texture2D* Created = nullptr;
    HRESULT hr;

    if (ppTexture2D != nullptr)
        *ppTexture2D = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    Desc = RxToD3D11Desc(*pDesc);
    hr = Device.CreateTexture2D(&Desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(pInitialData), ppTexture2D ? &Created : nullptr);
    if (SUCCEEDED(hr) && ppTexture2D != nullptr)
        *ppTexture2D = RxTo10<ID3D10Texture2D>(Created);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateTexture3D(
    _In_ const D3D10_TEXTURE3D_DESC* pDesc,
    _In_reads_opt_(_Inexpressible_(pDesc->MipLevels)) const D3D10_SUBRESOURCE_DATA* pInitialData,
    _Out_ ID3D10Texture3D** ppTexture3D)
{
    D3D11_TEXTURE3D_DESC Desc;
    ID3D11Texture3D* Created = nullptr;
    HRESULT hr;

    if (ppTexture3D != nullptr)
        *ppTexture3D = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    Desc = RxToD3D11Desc(*pDesc);
    hr = Device.CreateTexture3D(&Desc, reinterpret_cast<const D3D11_SUBRESOURCE_DATA*>(pInitialData), ppTexture3D ? &Created : nullptr);
    if (SUCCEEDED(hr) && ppTexture3D != nullptr)
        *ppTexture3D = RxTo10<ID3D10Texture3D>(Created);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateShaderResourceView(
    _In_ ID3D10Resource* pResource,
    _In_opt_ const D3D10_SHADER_RESOURCE_VIEW_DESC* pDesc,
    _Out_opt_ ID3D10ShaderResourceView** ppSRView)
{
    D3D10_SHADER_RESOURCE_VIEW_DESC1 Desc1 = {};
    ID3D10ShaderResourceView1* View = nullptr;
    HRESULT hr;

    if (ppSRView != nullptr)
        *ppSRView = nullptr;

    /* The older desc is the newer one without cube arrays */
    if (pDesc != nullptr)
        memcpy(&Desc1, pDesc, sizeof(*pDesc));

    hr = CreateShaderResourceView1(pResource, pDesc ? &Desc1 : nullptr, ppSRView ? &View : nullptr);
    if (ppSRView != nullptr)
        *ppSRView = View;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateRenderTargetView(
    _In_ ID3D10Resource* pResource,
    _In_opt_ const D3D10_RENDER_TARGET_VIEW_DESC* pDesc,
    _Out_opt_ ID3D10RenderTargetView** ppRTView)
{
    D3D11_RENDER_TARGET_VIEW_DESC Desc;
    ID3D11RenderTargetView* View = nullptr;
    HRESULT hr;

    if (ppRTView != nullptr)
        *ppRTView = nullptr;
    if (pDesc != nullptr)
        Desc = RxToD3D11Desc(*pDesc);

    hr = Device.CreateRenderTargetView(RxTo11<ID3D11Resource>(pResource), pDesc ? &Desc : nullptr,
                                       ppRTView ? &View : nullptr);
    if (SUCCEEDED(hr) && ppRTView != nullptr)
        *ppRTView = RxTo10<ID3D10RenderTargetView>(View);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateDepthStencilView(
    _In_ ID3D10Resource* pResource,
    _In_opt_ const D3D10_DEPTH_STENCIL_VIEW_DESC* pDesc,
    _Out_opt_ ID3D10DepthStencilView** ppDepthStencilView)
{
    D3D11_DEPTH_STENCIL_VIEW_DESC Desc;
    ID3D11DepthStencilView* View = nullptr;
    HRESULT hr;

    if (ppDepthStencilView != nullptr)
        *ppDepthStencilView = nullptr;
    if (pDesc != nullptr)
        Desc = RxToD3D11Desc(*pDesc);

    hr = Device.CreateDepthStencilView(RxTo11<ID3D11Resource>(pResource), pDesc ? &Desc : nullptr,
                                       ppDepthStencilView ? &View : nullptr);
    if (SUCCEEDED(hr) && ppDepthStencilView != nullptr)
        *ppDepthStencilView = RxTo10<ID3D10DepthStencilView>(View);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateInputLayout(
    _In_reads_(NumElements) const D3D10_INPUT_ELEMENT_DESC* pInputElementDescs,
    _In_range_(0, D3D10_1_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT) UINT NumElements,
    _In_reads_(BytecodeLength) const void* pShaderBytecodeWithInputSignature,
    _In_ SIZE_T BytecodeLength,
    _Out_opt_ ID3D10InputLayout** ppInputLayout)
{
    ID3D11InputLayout* Layout = nullptr;
    HRESULT hr;

    if (ppInputLayout != nullptr)
        *ppInputLayout = nullptr;

    /* The element desc is the same in both */
    static_assert(sizeof(D3D10_INPUT_ELEMENT_DESC) == sizeof(D3D11_INPUT_ELEMENT_DESC));
    hr = Device.CreateInputLayout(reinterpret_cast<const D3D11_INPUT_ELEMENT_DESC*>(pInputElementDescs), NumElements,
                                  pShaderBytecodeWithInputSignature, BytecodeLength,
                                  ppInputLayout ? &Layout : nullptr);
    if (SUCCEEDED(hr) && ppInputLayout != nullptr)
        *ppInputLayout = RxTo10<ID3D10InputLayout>(Layout);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateVertexShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_opt_ ID3D10VertexShader** ppVertexShader)
{
    ID3D11VertexShader* Shader = nullptr;
    HRESULT hr;

    if (ppVertexShader != nullptr)
        *ppVertexShader = nullptr;

    hr = Device.CreateVertexShader(pShaderBytecode, BytecodeLength, nullptr, ppVertexShader ? &Shader : nullptr);
    if (SUCCEEDED(hr) && ppVertexShader != nullptr)
        *ppVertexShader = RxTo10<ID3D10VertexShader>(Shader);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateGeometryShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_opt_ ID3D10GeometryShader** ppGeometryShader)
{
    ID3D11GeometryShader* Shader = nullptr;
    HRESULT hr;

    if (ppGeometryShader != nullptr)
        *ppGeometryShader = nullptr;

    hr = Device.CreateGeometryShader(pShaderBytecode, BytecodeLength, nullptr, ppGeometryShader ? &Shader : nullptr);
    if (SUCCEEDED(hr) && ppGeometryShader != nullptr)
        *ppGeometryShader = RxTo10<ID3D10GeometryShader>(Shader);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateGeometryShaderWithStreamOutput(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _In_reads_opt_(NumEntries) const D3D10_SO_DECLARATION_ENTRY* pSODeclaration,
    _In_range_(0, D3D10_SO_SINGLE_BUFFER_COMPONENT_LIMIT) UINT NumEntries,
    _In_ UINT OutputStreamStride,
    _Out_opt_ ID3D10GeometryShader** ppGeometryShader)
{
    D3D11_SO_DECLARATION_ENTRY Entries[D3D10_SO_SINGLE_BUFFER_COMPONENT_LIMIT * D3D10_SO_BUFFER_SLOT_COUNT];
    ID3D11GeometryShader* Shader = nullptr;
    UINT Index;
    HRESULT hr;

    if (ppGeometryShader != nullptr)
        *ppGeometryShader = nullptr;
    if (NumEntries > _countof(Entries) || (NumEntries != 0 && pSODeclaration == nullptr))
        return E_INVALIDARG;

    /* Direct3D 10 streams out of stream 0 only, into one buffer with the stride given */
    for (Index = 0; Index < NumEntries; Index++)
    {
        Entries[Index].Stream = 0;
        Entries[Index].SemanticName = pSODeclaration[Index].SemanticName;
        Entries[Index].SemanticIndex = pSODeclaration[Index].SemanticIndex;
        Entries[Index].StartComponent = pSODeclaration[Index].StartComponent;
        Entries[Index].ComponentCount = pSODeclaration[Index].ComponentCount;
        Entries[Index].OutputSlot = pSODeclaration[Index].OutputSlot;
    }

    hr = Device.CreateGeometryShaderWithStreamOutput(pShaderBytecode, BytecodeLength, Entries, NumEntries,
                                                     OutputStreamStride ? &OutputStreamStride : nullptr,
                                                     OutputStreamStride ? 1 : 0, 0, nullptr,
                                                     ppGeometryShader ? &Shader : nullptr);
    if (SUCCEEDED(hr) && ppGeometryShader != nullptr)
        *ppGeometryShader = RxTo10<ID3D10GeometryShader>(Shader);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreatePixelShader(
    _In_reads_(BytecodeLength) const void* pShaderBytecode,
    _In_ SIZE_T BytecodeLength,
    _Out_opt_ ID3D10PixelShader** ppPixelShader)
{
    ID3D11PixelShader* Shader = nullptr;
    HRESULT hr;

    if (ppPixelShader != nullptr)
        *ppPixelShader = nullptr;

    hr = Device.CreatePixelShader(pShaderBytecode, BytecodeLength, nullptr, ppPixelShader ? &Shader : nullptr);
    if (SUCCEEDED(hr) && ppPixelShader != nullptr)
        *ppPixelShader = RxTo10<ID3D10PixelShader>(Shader);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateBlendState(
    _In_ const D3D10_BLEND_DESC* pBlendStateDesc,
    _Out_opt_ ID3D10BlendState** ppBlendState)
{
    D3D11_BLEND_DESC Desc;
    ID3D11BlendState* State = nullptr;
    HRESULT hr;

    if (ppBlendState != nullptr)
        *ppBlendState = nullptr;
    if (pBlendStateDesc == nullptr)
        return E_INVALIDARG;

    Desc = RxToD3D11Desc(*pBlendStateDesc);
    hr = Device.CreateBlendState(&Desc, ppBlendState ? &State : nullptr);
    if (SUCCEEDED(hr) && ppBlendState != nullptr)
        *ppBlendState = RxTo10<ID3D10BlendState>(State);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateDepthStencilState(
    _In_ const D3D10_DEPTH_STENCIL_DESC* pDepthStencilDesc,
    _Out_opt_ ID3D10DepthStencilState** ppDepthStencilState)
{
    ID3D11DepthStencilState* State = nullptr;
    HRESULT hr;

    if (ppDepthStencilState != nullptr)
        *ppDepthStencilState = nullptr;

    static_assert(sizeof(D3D10_DEPTH_STENCIL_DESC) == sizeof(D3D11_DEPTH_STENCIL_DESC));
    hr = Device.CreateDepthStencilState(reinterpret_cast<const D3D11_DEPTH_STENCIL_DESC*>(pDepthStencilDesc), ppDepthStencilState ? &State : nullptr);
    if (SUCCEEDED(hr) && ppDepthStencilState != nullptr)
        *ppDepthStencilState = RxTo10<ID3D10DepthStencilState>(State);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateRasterizerState(
    _In_ const D3D10_RASTERIZER_DESC* pRasterizerDesc,
    _Out_opt_ ID3D10RasterizerState** ppRasterizerState)
{
    ID3D11RasterizerState* State = nullptr;
    HRESULT hr;

    if (ppRasterizerState != nullptr)
        *ppRasterizerState = nullptr;

    static_assert(sizeof(D3D10_RASTERIZER_DESC) == sizeof(D3D11_RASTERIZER_DESC));
    hr = Device.CreateRasterizerState(reinterpret_cast<const D3D11_RASTERIZER_DESC*>(pRasterizerDesc), ppRasterizerState ? &State : nullptr);
    if (SUCCEEDED(hr) && ppRasterizerState != nullptr)
        *ppRasterizerState = RxTo10<ID3D10RasterizerState>(State);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateSamplerState(
    _In_ const D3D10_SAMPLER_DESC* pSamplerDesc,
    _Out_opt_ ID3D10SamplerState** ppSamplerState)
{
    ID3D11SamplerState* State = nullptr;
    HRESULT hr;

    if (ppSamplerState != nullptr)
        *ppSamplerState = nullptr;

    static_assert(sizeof(D3D10_SAMPLER_DESC) == sizeof(D3D11_SAMPLER_DESC));
    hr = Device.CreateSamplerState(reinterpret_cast<const D3D11_SAMPLER_DESC*>(pSamplerDesc), ppSamplerState ? &State : nullptr);
    if (SUCCEEDED(hr) && ppSamplerState != nullptr)
        *ppSamplerState = RxTo10<ID3D10SamplerState>(State);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateQuery(
    _In_ const D3D10_QUERY_DESC* pQueryDesc,
    _Out_opt_ ID3D10Query** ppQuery)
{
    D3D11_QUERY_DESC Desc;
    ID3D11Query* Query = nullptr;
    HRESULT hr;

    if (ppQuery != nullptr)
        *ppQuery = nullptr;
    if (pQueryDesc == nullptr)
        return E_INVALIDARG;

    Desc = { (D3D11_QUERY)pQueryDesc->Query, pQueryDesc->MiscFlags };
    hr = Device.CreateQuery(&Desc, ppQuery ? &Query : nullptr);
    if (SUCCEEDED(hr) && ppQuery != nullptr)
        *ppQuery = RxTo10<ID3D10Query>(Query);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreatePredicate(
    _In_ const D3D10_QUERY_DESC* pPredicateDesc,
    _Out_opt_ ID3D10Predicate** ppPredicate)
{
    D3D11_QUERY_DESC Desc;
    ID3D11Predicate* Predicate = nullptr;
    HRESULT hr;

    if (ppPredicate != nullptr)
        *ppPredicate = nullptr;
    if (pPredicateDesc == nullptr)
        return E_INVALIDARG;

    Desc = { (D3D11_QUERY)pPredicateDesc->Query, pPredicateDesc->MiscFlags };
    hr = Device.CreatePredicate(&Desc, ppPredicate ? &Predicate : nullptr);
    if (SUCCEEDED(hr) && ppPredicate != nullptr)
        *ppPredicate = RxTo10<ID3D10Predicate>(Predicate);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateCounter(
    _In_ const D3D10_COUNTER_DESC* pCounterDesc,
    _Out_opt_ ID3D10Counter** ppCounter)
{
    if (ppCounter != nullptr)
        *ppCounter = nullptr;

    /* No counters are offered, as on the Direct3D 11 side */
    return Device.CreateCounter(reinterpret_cast<const D3D11_COUNTER_DESC*>(pCounterDesc), nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CheckFormatSupport(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* pFormatSupport)
{
    return Device.CheckFormatSupport(Format, pFormatSupport);
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CheckMultisampleQualityLevels(
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _Out_ UINT* pNumQualityLevels)
{
    return Device.CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::CheckCounterInfo(
    _Out_ D3D10_COUNTER_INFO* pCounterInfo)
{
    D3D11_COUNTER_INFO Info = {};

    Device.CheckCounterInfo(&Info);
    pCounterInfo->LastDeviceDependentCounter = (D3D10_COUNTER)Info.LastDeviceDependentCounter;
    pCounterInfo->NumSimultaneousCounters = Info.NumSimultaneousCounters;
    pCounterInfo->NumDetectableParallelUnits = Info.NumDetectableParallelUnits;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CheckCounter(
    _In_ const D3D10_COUNTER_DESC* pDesc,
    _Out_ D3D10_COUNTER_TYPE* pType,
    _Out_ UINT* pActiveCounters,
    _Out_writes_opt_(*pNameLength) LPSTR szName,
    _Inout_opt_ UINT* pNameLength,
    _Out_writes_opt_(*pUnitsLength) LPSTR szUnits,
    _Inout_opt_ UINT* pUnitsLength,
    _Out_writes_opt_(*pDescriptionLength) LPSTR szDescription,
    _Inout_opt_ UINT* pDescriptionLength)
{
    return Device.CheckCounter(reinterpret_cast<const D3D11_COUNTER_DESC*>(pDesc), (D3D11_COUNTER_TYPE*)pType,
                               pActiveCounters, szName, pNameLength, szUnits, pUnitsLength, szDescription,
                               pDescriptionLength);
}

UINT
STDMETHODCALLTYPE
RxD3D10Device::GetCreationFlags(VOID)
{
    UINT Flags = Device.GetCreationFlags();
    UINT Result = Flags & (D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_DEBUG |
                           D3D11_CREATE_DEVICE_SWITCH_TO_REF | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                           D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY);

    /* The flags in Direct3D 10 terms; debuggable moved */
    if (Flags & D3D11_CREATE_DEVICE_DEBUGGABLE)
        Result |= D3D10_CREATE_DEVICE_DEBUGGABLE;
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::OpenSharedResource(
    _In_ HANDLE hResource,
    _In_ REFIID ReturnedInterface,
    _Out_opt_ void** ppResource)
{
    return Device.OpenSharedResource(hResource, ReturnedInterface, ppResource);
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::SetTextFilterSize(
    _In_ UINT Width,
    _In_ UINT Height)
{
    TextFilterWidth = Width;
    TextFilterHeight = Height;
}

VOID
STDMETHODCALLTYPE
RxD3D10Device::GetTextFilterSize(
    _Out_opt_ UINT* pWidth,
    _Out_opt_ UINT* pHeight)
{
    if (pWidth != nullptr)
        *pWidth = TextFilterWidth;
    if (pHeight != nullptr)
        *pHeight = TextFilterHeight;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateShaderResourceView1(
    _In_ ID3D10Resource* pResource,
    _In_opt_ const D3D10_SHADER_RESOURCE_VIEW_DESC1* pDesc,
    _Out_opt_ ID3D10ShaderResourceView1** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC Desc;
    ID3D11ShaderResourceView* View = nullptr;
    HRESULT hr;

    if (ppSRView != nullptr)
        *ppSRView = nullptr;
    if (pDesc != nullptr)
        Desc = RxToD3D11Desc(*pDesc);

    hr = Device.CreateShaderResourceView(RxTo11<ID3D11Resource>(pResource), pDesc ? &Desc : nullptr,
                                         ppSRView ? &View : nullptr);
    if (SUCCEEDED(hr) && ppSRView != nullptr)
        *ppSRView = RxTo10<ID3D10ShaderResourceView1>(View);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxD3D10Device::CreateBlendState1(
    _In_ const D3D10_BLEND_DESC1* pBlendStateDesc,
    _Out_opt_ ID3D10BlendState1** ppBlendState)
{
    ID3D11BlendState* State = nullptr;
    HRESULT hr;

    if (ppBlendState != nullptr)
        *ppBlendState = nullptr;
    if (pBlendStateDesc == nullptr)
        return E_INVALIDARG;

    /* The 10.1 desc is the Direct3D 11 one */
    hr = Device.CreateBlendState(reinterpret_cast<const D3D11_BLEND_DESC*>(pBlendStateDesc),
                                 ppBlendState ? &State : nullptr);
    if (SUCCEEDED(hr) && ppBlendState != nullptr)
        *ppBlendState = RxTo10<ID3D10BlendState1>(State);
    return hr;
}

D3D10_FEATURE_LEVEL1
STDMETHODCALLTYPE
RxD3D10Device::GetFeatureLevel(VOID)
{
    return (D3D10_FEATURE_LEVEL1)Device.GetFeatureLevel();
}
