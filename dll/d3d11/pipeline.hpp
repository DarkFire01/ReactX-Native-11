/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The immediate context's copy of everything bound to the pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

#include "bound.hpp"

#include <array>

class DdiResource;
class RxChildLifetime;
class RxBuffer;
class RxInputLayout;
class RxQuery;
template <typename Traits>
class RxView;
struct RxRenderTargetTraits;
struct RxShaderResourceTraits;
struct RxDepthStencilViewTraits;
struct RxUnorderedAccessTraits;
using RxRenderTargetView = RxView<RxRenderTargetTraits>;
using RxShaderResourceView = RxView<RxShaderResourceTraits>;
using RxDepthStencilView = RxView<RxDepthStencilViewTraits>;
using RxUnorderedAccessView = RxView<RxUnorderedAccessTraits>;
template <typename Traits>
class RxStateObject;
struct RxBlendTraits;
struct RxDepthStencilTraits;
struct RxRasterizerTraits;
struct RxSamplerTraits;

/* Shader stages in the order the DDI tables list their entries */
enum class RxStage : UINT
{
    Vertex,
    Hull,
    Domain,
    Geometry,
    Pixel,
    Compute,
    Count
};

constexpr UINT RxStageCount = (UINT)RxStage::Count;

/* Which part of the pipeline an output binding belongs to */
enum class RxOutput
{
    Merger,
    Compute,
    Stream
};

/* Feature level 11.1 slot count; lower levels use the first eight */
constexpr UINT RxUavSlotCount = D3D11_1_UAV_SLOT_COUNT;

/* Hidden counter and stream offset values that mean leave it where it is */
inline constexpr std::array<UINT, RxUavSlotCount> RxKeepCounters = []
{
    std::array<UINT, RxUavSlotCount> Values = {};

    Values.fill(UINT_MAX);
    return Values;
}();

/*
 * Bound objects are held by internal reference next to the driver handles last sent.
 * A NULL object may still have a handle: that is a device default standing in.
 */
struct RxStageState
{
    RxBound<RxChildLifetime> Shader;
    ID3D11DeviceChild* ShaderObject = nullptr;
    D3D10DDI_HSHADER ShaderHandle = {};

    std::array<RxBound<RxBuffer>, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> ConstantBuffers;
    std::array<D3D10DDI_HRESOURCE, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> ConstantBufferHandles = {};
    UINT ConstantBufferCount = 0;

    /* Ranges in 16 byte constants set through the *SetConstantBuffers1 calls; a count of 0 means the whole buffer */
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> FirstConstants = {};
    std::array<UINT, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT> ConstantCounts = {};

    std::array<RxBound<RxShaderResourceView>, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ShaderResources;
    std::array<D3D10DDI_HSHADERRESOURCEVIEW, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> ShaderResourceHandles = {};
    UINT ShaderResourceCount = 0;

    std::array<RxBound<RxStateObject<RxSamplerTraits>>, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> Samplers;
    std::array<D3D10DDI_HSAMPLER, D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT> SamplerHandles = {};
    UINT SamplerCount = 0;
};

struct RxPipelineState
{
    /* Defined where every bound type is complete */
    RxPipelineState();
    ~RxPipelineState();
    RxPipelineState& operator=(const RxPipelineState&) = default;

    /* Input assembler */
    RxBound<RxInputLayout> InputLayout;
    D3D10DDI_HELEMENTLAYOUT InputLayoutHandle = {};
    D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    std::array<RxBound<RxBuffer>, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> VertexBuffers;
    std::array<D3D10DDI_HRESOURCE, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> VertexBufferHandles = {};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> Strides = {};
    std::array<UINT, D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT> Offsets = {};
    UINT VertexBufferCount = 0;
    RxBound<RxBuffer> IndexBuffer;
    D3D10DDI_HRESOURCE IndexBufferHandle = {};
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_UNKNOWN;
    UINT IndexOffset = 0;

    std::array<RxStageState, RxStageCount> Stages;

    /* Rasterizer */
    RxBound<RxStateObject<RxRasterizerTraits>> Rasterizer;
    D3D10DDI_HRASTERIZERSTATE RasterizerHandle = {};
    std::array<D3D10_DDI_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> Viewports = {};
    UINT ViewportCount = 0;
    std::array<D3D10_DDI_RECT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> Scissors = {};
    UINT ScissorCount = 0;

    /* Output merger */
    RxBound<RxStateObject<RxBlendTraits>> Blend;
    D3D10DDI_HBLENDSTATE BlendHandle = {};
    std::array<FLOAT, 4> BlendFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
    UINT SampleMask = 0xFFFFFFFF;
    RxBound<RxStateObject<RxDepthStencilTraits>> DepthStencil;
    D3D10DDI_HDEPTHSTENCILSTATE DepthStencilHandle = {};
    UINT StencilRef = 0;
    std::array<RxBound<RxRenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargets;
    std::array<D3D10DDI_HRENDERTARGETVIEW, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> RenderTargetHandles = {};
    UINT RenderTargetCount = 0;
    RxBound<RxDepthStencilView> DepthStencilView;
    D3D10DDI_HDEPTHSTENCILVIEW DepthStencilViewHandle = {};

    /* Output merger UAVs share slot numbers with the targets and start at OutputUavStart */
    std::array<RxBound<RxUnorderedAccessView>, RxUavSlotCount> OutputUavs;
    std::array<D3D11DDI_HUNORDEREDACCESSVIEW, RxUavSlotCount> OutputUavHandles = {};
    UINT OutputUavStart = 0;
    UINT OutputUavEnd = 0;

    /* Compute UAVs */
    std::array<RxBound<RxUnorderedAccessView>, RxUavSlotCount> ComputeUavs;
    std::array<D3D11DDI_HUNORDEREDACCESSVIEW, RxUavSlotCount> ComputeUavHandles = {};
    UINT ComputeUavCount = 0;

    /* Stream output */
    std::array<RxBound<RxBuffer>, D3D11_SO_BUFFER_SLOT_COUNT> StreamTargets;
    std::array<D3D10DDI_HRESOURCE, D3D11_SO_BUFFER_SLOT_COUNT> StreamTargetHandles = {};
    UINT StreamTargetCount = 0;

    /* Predication */
    RxBound<RxQuery> Predicate;
    BOOL PredicateValue = FALSE;
};
