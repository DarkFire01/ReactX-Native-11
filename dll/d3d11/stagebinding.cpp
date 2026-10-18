/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Per stage resource binding methods, all routed to the shared stage code
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shaders.hpp"


VOID
STDMETHODCALLTYPE
RxImmediateContext::VSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Vertex, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Vertex, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Vertex, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    SetConstantBuffers(RxStage::Hull, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    SetShaderResources(RxStage::Hull, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    SetSamplers(RxStage::Hull, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Hull, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Hull, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Hull, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    SetConstantBuffers(RxStage::Domain, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    SetShaderResources(RxStage::Domain, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    SetSamplers(RxStage::Domain, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Domain, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Domain, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Domain, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    SetConstantBuffers(RxStage::Geometry, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    SetShaderResources(RxStage::Geometry, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    SetSamplers(RxStage::Geometry, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Geometry, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Geometry, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Geometry, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Pixel, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Pixel, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Pixel, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _In_reads_opt_(NumBuffers) ID3D11Buffer* const* ppConstantBuffers)
{
    SetConstantBuffers(RxStage::Compute, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _In_reads_opt_(NumViews) ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    SetShaderResources(RxStage::Compute, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _In_reads_opt_(NumSamplers) ID3D11SamplerState* const* ppSamplers)
{
    SetSamplers(RxStage::Compute, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetConstantBuffers(
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT - StartSlot) UINT NumBuffers,
    _Out_writes_opt_(NumBuffers) ID3D11Buffer** ppConstantBuffers)
{
    GetConstantBuffers(RxStage::Compute, StartSlot, NumBuffers, ppConstantBuffers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetShaderResources(
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot) UINT NumViews,
    _Out_writes_opt_(NumViews) ID3D11ShaderResourceView** ppShaderResourceViews)
{
    GetShaderResources(RxStage::Compute, StartSlot, NumViews, ppShaderResourceViews);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetSamplers(
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - 1) UINT StartSlot,
    _In_range_(0, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT - StartSlot) UINT NumSamplers,
    _Out_writes_opt_(NumSamplers) ID3D11SamplerState** ppSamplers)
{
    GetSamplers(RxStage::Compute, StartSlot, NumSamplers, ppSamplers);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::VSGetShader(
    _Outptr_result_maybenull_ ID3D11VertexShader** ppVertexShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Vertex, __uuidof(ID3D11VertexShader), reinterpret_cast<VOID**>(ppVertexShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSGetShader(
    _Outptr_result_maybenull_ ID3D11HullShader** ppHullShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Hull, __uuidof(ID3D11HullShader), reinterpret_cast<VOID**>(ppHullShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSGetShader(
    _Outptr_result_maybenull_ ID3D11DomainShader** ppDomainShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Domain, __uuidof(ID3D11DomainShader), reinterpret_cast<VOID**>(ppDomainShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSGetShader(
    _Outptr_result_maybenull_ ID3D11GeometryShader** ppGeometryShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Geometry, __uuidof(ID3D11GeometryShader), reinterpret_cast<VOID**>(ppGeometryShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::PSGetShader(
    _Outptr_result_maybenull_ ID3D11PixelShader** ppPixelShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Pixel, __uuidof(ID3D11PixelShader), reinterpret_cast<VOID**>(ppPixelShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSGetShader(
    _Outptr_result_maybenull_ ID3D11ComputeShader** ppComputeShader,
    _Out_writes_opt_(*pNumClassInstances) ID3D11ClassInstance** ppClassInstances,
    _Inout_opt_ UINT* pNumClassInstances)
{
    GetShader(RxStage::Compute, __uuidof(ID3D11ComputeShader), reinterpret_cast<VOID**>(ppComputeShader), pNumClassInstances);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::HSSetShader(
    _In_opt_ ID3D11HullShader* pHullShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxHullShader* Shader = RxUnwrap<RxHullShader>(pHullShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Hull, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::DSSetShader(
    _In_opt_ ID3D11DomainShader* pDomainShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxDomainShader* Shader = RxUnwrap<RxDomainShader>(pDomainShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Domain, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GSSetShader(
    _In_opt_ ID3D11GeometryShader* pShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxGeometryShader* Shader = RxUnwrap<RxGeometryShader>(pShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Geometry, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CSSetShader(
    _In_opt_ ID3D11ComputeShader* pComputeShader,
    _In_reads_opt_(NumClassInstances) ID3D11ClassInstance* const* ppClassInstances,
    _In_ UINT NumClassInstances)
{
    RxComputeShader* Shader = RxUnwrap<RxComputeShader>(pComputeShader);

    UNREFERENCED_PARAMETER(ppClassInstances);
    UNREFERENCED_PARAMETER(NumClassInstances);

    SetShader(RxStage::Compute, Shader, Shader, Shader ? Shader->GetHandle() : D3D10DDI_HSHADER{});
}
