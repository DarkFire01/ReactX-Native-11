/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device object and the driver side objects it creates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "format.hpp"
#include "shader.hpp"

class RxWarpAdapter;

/* One mip level of one array slice, inside the resource's memory */
typedef struct _RX_WARP_SUBRESOURCE
{
    SIZE_T Offset;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT RowPitch;
    UINT DepthPitch;
    SIZE_T Size;
} RX_WARP_SUBRESOURCE;

class RxWarpResource
{
public:
    ~RxWarpResource();

    HRESULT
    Initialize(
        _In_ const D3D11DDIARG_CREATERESOURCE *Args);

    RX_WARP_SUBRESOURCE *
    Subresource(
        _In_ UINT Index)
    {
        return (Index < SubresourceCount) ? &Subresources[Index] : nullptr;
    }

    BYTE *
    Data(
        _In_ UINT Index)
    {
        return Memory + Subresources[Index].Offset;
    }

    D3D10DDIRESOURCE_TYPE Dimension = D3D10DDIRESOURCE_BUFFER;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    UINT Usage = 0;
    UINT BindFlags = 0;
    UINT MapFlags = 0;
    UINT MiscFlags = 0;
    UINT MipLevels = 1;
    UINT ArraySize = 1;
    UINT SampleCount = 1;
    UINT ByteStride = 0;
    BYTE *Memory = nullptr;
    SIZE_T MemorySize = 0;
    UINT SubresourceCount = 0;
    RX_WARP_SUBRESOURCE *Subresources = nullptr;
    UINT StreamFilled = 0;      /* bytes stream output has written, for DrawAuto */
};

/* Render target, depth stencil, shader resource and unordered access views share one shape */
class RxWarpView
{
public:
    RxWarpResource *Resource = nullptr;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    D3D10DDIRESOURCE_TYPE Dimension = D3D10DDIRESOURCE_BUFFER;
    UINT MipSlice = 0;
    UINT MipCount = 1;
    UINT FirstSlice = 0;
    UINT SliceCount = 1;
    UINT FirstElement = 0;
    UINT ElementCount = 0;
    UINT Flags = 0;
    UINT Counter = 0;           /* append and consume, and counter buffers */
};

class RxWarpBlendState
{
public:
    D3D11_1_DDI_BLEND_DESC Desc = {};
};

class RxWarpDepthStencilState
{
public:
    D3D10_DDI_DEPTH_STENCIL_DESC Desc = {};
};

class RxWarpRasterizerState
{
public:
    D3DWDDM2_0DDI_RASTERIZER_DESC Desc = {};
};

class RxWarpSampler
{
public:
    RX_WARP_SAMPLER State = {};
};

class RxWarpElementLayout
{
public:
    std::vector<D3D10DDIARG_INPUT_ELEMENT_DESC> Elements;
};

typedef struct _RX_WARP_BUFFER_BINDING
{
    RxWarpResource *Resource;
    UINT Stride;
    UINT Offset;
} RX_WARP_BUFFER_BINDING;

typedef struct _RX_WARP_CONSTANT_BINDING
{
    RxWarpResource *Resource;
    UINT First;             /* in sixteen byte constants */
    UINT Count;
} RX_WARP_CONSTANT_BINDING;

typedef enum _RX_WARP_STAGE
{
    RxWarpStageVertex,
    RxWarpStagePixel,
    RxWarpStageGeometry,
    RxWarpStageHull,
    RxWarpStageDomain,
    RxWarpStageCompute,
    RxWarpStageCount
} RX_WARP_STAGE;

typedef struct _RX_WARP_STAGE_STATE
{
    RxWarpShader *Shader;
    RX_WARP_CONSTANT_BINDING ConstantBuffers[RxWarpConstantBufferSlots];
    RxWarpView *Views[128];
    RxWarpSampler *Samplers[16];
    RxWarpView *Uavs[64];
} RX_WARP_STAGE_STATE;

/* Everything bound to the device that draws read */
typedef struct _RX_WARP_PIPELINE
{
    RxWarpElementLayout *Layout;
    RX_WARP_BUFFER_BINDING VertexBuffers[32];
    RxWarpResource *IndexBuffer;
    DXGI_FORMAT IndexFormat;
    UINT IndexOffset;
    D3D10_DDI_PRIMITIVE_TOPOLOGY Topology;
    RX_WARP_STAGE_STATE Stages[RxWarpStageCount];
    RxWarpBlendState *Blend;
    FLOAT BlendFactor[4];
    UINT SampleMask;
    RxWarpDepthStencilState *DepthStencil;
    UINT StencilRef;
    RxWarpRasterizerState *Rasterizer;
    D3D10_DDI_VIEWPORT Viewports[16];
    UINT ViewportCount;
    D3D10_DDI_RECT Scissors[16];
    UINT ScissorCount;
    RxWarpView *RenderTargets[8];
    UINT RenderTargetCount;
    RxWarpView *DepthStencilView;
    RxWarpResource *StreamTargets[4];
} RX_WARP_PIPELINE;

/* Running totals the queries sample, in the order of the pipeline statistics */
typedef enum _RX_WARP_COUNTER
{
    RxWarpCountIaVertices,
    RxWarpCountIaPrimitives,
    RxWarpCountVsInvocations,
    RxWarpCountGsInvocations,
    RxWarpCountGsPrimitives,
    RxWarpCountClipInvocations,
    RxWarpCountClipPrimitives,
    RxWarpCountPsInvocations,
    RxWarpCountHsInvocations,
    RxWarpCountDsInvocations,
    RxWarpCountCsInvocations,
    RxWarpCountSamplesPassed,
    RxWarpCountSoWritten,
    RxWarpCountSoNeeded,
    RxWarpCountTotal
} RX_WARP_COUNTER;

class RxWarpQuery
{
public:
    D3D10DDI_QUERY Type = D3D10DDI_QUERY_EVENT;
    UINT64 Start[RxWarpCountTotal] = {};
    UINT64 Stop[RxWarpCountTotal] = {};
    UINT64 Timestamp = 0;
    BOOLEAN Done = FALSE;
};

class RxWarpDevice
{
public:
    /* TRUE when predication says to skip rendering, clears and copies */
    BOOLEAN
    Predicated(VOID);

    static
    HRESULT
    Create(
        _In_ RxWarpAdapter *Adapter,
        _Inout_ D3D10DDIARG_CREATEDEVICE *Args);

    /* Reports an error to the runtime the way the DDI asks: through pfnSetErrorCb */
    VOID
    SetError(
        _In_ HRESULT Error);

    RxWarpAdapter *Adapter = nullptr;
    D3D10DDI_HRTDEVICE RuntimeDevice = {};
    D3D10DDI_HRTCORELAYER RuntimeCoreLayer = {};
    const D3DDDI_DEVICECALLBACKS *KernelCallbacks = nullptr;
    const D3D11DDI_CORELAYER_DEVICECALLBACKS *CoreCallbacks = nullptr;
    UINT Interface = 0;
    UINT Version = 0;
    RX_WARP_PIPELINE Pipeline = {};
    UINT64 Counters[RxWarpCountTotal] = {};
    RxWarpQuery *Predicate = nullptr;
    BOOL PredicateValue = FALSE;
};

/* What a stage's shader sees, gathered from its bound state */
VOID
RxWarpBindStage(
    _In_ const RX_WARP_STAGE_STATE *State,
    _Out_ RX_WARP_STAGE_BINDINGS *Bindings);

VOID
RxWarpBindUavs(
    _Inout_ RX_WARP_STAGE_STATE *State,
    _In_ UINT StartSlot,
    _In_ UINT Count,
    _In_reads_opt_(Count) const D3D11DDI_HUNORDEREDACCESSVIEW *Views,
    _In_reads_opt_(Count) const UINT *InitialCounts);

/* DDI entry points, grouped by the file that implements them */
namespace RxWarpDdi
{
    /* resource.cpp */
    VOID APIENTRY CreateResource(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATERESOURCE *, D3D10DDI_HRESOURCE, D3D10DDI_HRTRESOURCE);
    VOID APIENTRY OpenResource(D3D10DDI_HDEVICE, const D3D10DDIARG_OPENRESOURCE *, D3D10DDI_HRESOURCE, D3D10DDI_HRTRESOURCE);
    VOID APIENTRY DestroyResource(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceMap(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3D10_DDI_MAP, UINT, D3D10DDI_MAPPED_SUBRESOURCE *);
    VOID APIENTRY ResourceUnmap(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT);
    BOOL APIENTRY ResourceIsStagingBusy(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceCopy(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceCopyRegion(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, UINT, UINT, UINT, D3D10DDI_HRESOURCE, UINT, const D3D10_DDI_BOX *, UINT);
    VOID APIENTRY ResourceResolveSubresource(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3D10DDI_HRESOURCE, UINT, DXGI_FORMAT);
    VOID APIENTRY ResourceUpdateSubresourceUP(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, const D3D10_DDI_BOX *, const VOID *, UINT, UINT, UINT);

    /* view.cpp */
    VOID APIENTRY CreateRenderTargetView(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERENDERTARGETVIEW *, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW);
    VOID APIENTRY CreateRenderTargetView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW);
    VOID APIENTRY DestroyRenderTargetView(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW);
    VOID APIENTRY CreateDepthStencilView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *, D3D10DDI_HDEPTHSTENCILVIEW, D3D10DDI_HRTDEPTHSTENCILVIEW);
    VOID APIENTRY DestroyDepthStencilView(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILVIEW);
    VOID APIENTRY CreateShaderResourceView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATESHADERRESOURCEVIEW *, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW);
    VOID APIENTRY CreateShaderResourceView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW);
    VOID APIENTRY DestroyShaderResourceView(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW);
    VOID APIENTRY CreateUnorderedAccessView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW);
    VOID APIENTRY CreateUnorderedAccessView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW);
    VOID APIENTRY DestroyUnorderedAccessView(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW);

    /* clear.cpp */
    VOID APIENTRY ClearRenderTargetView(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW, FLOAT[4]);
    VOID APIENTRY ClearDepthStencilView(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILVIEW, UINT, FLOAT, UINT8);
    VOID APIENTRY ClearUnorderedAccessViewUint(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const UINT[4]);
    VOID APIENTRY ClearUnorderedAccessViewFloat(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT[4]);
    VOID APIENTRY ClearView(D3D10DDI_HDEVICE, D3D11DDI_HANDLETYPE, VOID *, const FLOAT[4], const D3D10_DDI_RECT *, UINT);

    /* state.cpp */
    VOID APIENTRY CreateBlendState(D3D10DDI_HDEVICE, const D3D11_1_DDI_BLEND_DESC *, D3D10DDI_HBLENDSTATE, D3D10DDI_HRTBLENDSTATE);
    VOID APIENTRY DestroyBlendState(D3D10DDI_HDEVICE, D3D10DDI_HBLENDSTATE);
    VOID APIENTRY CreateDepthStencilState(D3D10DDI_HDEVICE, const D3D10_DDI_DEPTH_STENCIL_DESC *, D3D10DDI_HDEPTHSTENCILSTATE, D3D10DDI_HRTDEPTHSTENCILSTATE);
    VOID APIENTRY DestroyDepthStencilState(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILSTATE);
    VOID APIENTRY CreateRasterizerState(D3D10DDI_HDEVICE, const D3D11_1_DDI_RASTERIZER_DESC *, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE);
    VOID APIENTRY CreateRasterizerState2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDI_RASTERIZER_DESC *, D3D10DDI_HRASTERIZERSTATE, D3D10DDI_HRTRASTERIZERSTATE);
    VOID APIENTRY DestroyRasterizerState(D3D10DDI_HDEVICE, D3D10DDI_HRASTERIZERSTATE);
    VOID APIENTRY CreateSampler(D3D10DDI_HDEVICE, const D3D10_DDI_SAMPLER_DESC *, D3D10DDI_HSAMPLER, D3D10DDI_HRTSAMPLER);
    VOID APIENTRY DestroySampler(D3D10DDI_HDEVICE, D3D10DDI_HSAMPLER);
    VOID APIENTRY CreateElementLayout(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATEELEMENTLAYOUT *, D3D10DDI_HELEMENTLAYOUT, D3D10DDI_HRTELEMENTLAYOUT);
    VOID APIENTRY DestroyElementLayout(D3D10DDI_HDEVICE, D3D10DDI_HELEMENTLAYOUT);
    VOID APIENTRY CreateVertexShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_STAGE_IO_SIGNATURES *);
    VOID APIENTRY CreatePixelShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_STAGE_IO_SIGNATURES *);
    VOID APIENTRY CreateGeometryShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_STAGE_IO_SIGNATURES *);
    VOID APIENTRY CreateGeometryShaderWithStreamOutput(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_STAGE_IO_SIGNATURES *);
    VOID APIENTRY GsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY GsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY GsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY GsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY CreateHullShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES *);
    VOID APIENTRY HsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY HsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY HsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY HsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY CreateDomainShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER, const D3D11_1DDIARG_TESSELLATION_IO_SIGNATURES *);
    VOID APIENTRY DsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY DsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY DsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY DsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY SoSetTargets(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *);
    VOID APIENTRY DestroyShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);

    VOID APIENTRY IaSetInputLayout(D3D10DDI_HDEVICE, D3D10DDI_HELEMENTLAYOUT);
    VOID APIENTRY IaSetVertexBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY IaSetIndexBuffer(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, DXGI_FORMAT, UINT);
    VOID APIENTRY IaSetTopology(D3D10DDI_HDEVICE, D3D10_DDI_PRIMITIVE_TOPOLOGY);
    VOID APIENTRY VsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY PsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY VsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY PsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY VsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY PsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY VsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY PsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY SetBlendState(D3D10DDI_HDEVICE, D3D10DDI_HBLENDSTATE, const FLOAT[4], UINT);
    VOID APIENTRY SetDepthStencilState(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILSTATE, UINT);
    VOID APIENTRY SetRasterizerState(D3D10DDI_HDEVICE, D3D10DDI_HRASTERIZERSTATE);
    VOID APIENTRY SetViewports(D3D10DDI_HDEVICE, UINT, UINT, const D3D10_DDI_VIEWPORT *);
    VOID APIENTRY SetScissorRects(D3D10DDI_HDEVICE, UINT, UINT, const D3D10_DDI_RECT *);
    VOID APIENTRY SetRenderTargets(D3D10DDI_HDEVICE, const D3D10DDI_HRENDERTARGETVIEW *, UINT, UINT, D3D10DDI_HDEPTHSTENCILVIEW, const D3D11DDI_HUNORDEREDACCESSVIEW *, const UINT *, UINT, UINT, UINT, UINT);

    /* mips.cpp */
    VOID APIENTRY GenMips(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW);

    /* query.cpp */
    VOID APIENTRY CreateQuery(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATEQUERY *, D3D10DDI_HQUERY, D3D10DDI_HRTQUERY);
    VOID APIENTRY CreateQuery2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATEQUERY *, D3D10DDI_HQUERY, D3D10DDI_HRTQUERY);
    VOID APIENTRY DestroyQuery(D3D10DDI_HDEVICE, D3D10DDI_HQUERY);
    VOID APIENTRY QueryBegin(D3D10DDI_HDEVICE, D3D10DDI_HQUERY);
    VOID APIENTRY QueryEnd(D3D10DDI_HDEVICE, D3D10DDI_HQUERY);
    VOID APIENTRY QueryGetData(D3D10DDI_HDEVICE, D3D10DDI_HQUERY, VOID *, UINT, UINT);
    VOID APIENTRY SetPredication(D3D10DDI_HDEVICE, D3D10DDI_HQUERY, BOOL);

    /* compute.cpp */
    VOID APIENTRY CreateComputeShader(D3D10DDI_HDEVICE, const UINT *, D3D10DDI_HSHADER, D3D10DDI_HRTSHADER);
    VOID APIENTRY CsSetShader(D3D10DDI_HDEVICE, D3D10DDI_HSHADER);
    VOID APIENTRY CsSetShaderResources(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSHADERRESOURCEVIEW *);
    VOID APIENTRY CsSetSamplers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HSAMPLER *);
    VOID APIENTRY CsSetConstantBuffers(D3D10DDI_HDEVICE, UINT, UINT, const D3D10DDI_HRESOURCE *, const UINT *, const UINT *);
    VOID APIENTRY CsSetUnorderedAccessViews(D3D10DDI_HDEVICE, UINT, UINT, const D3D11DDI_HUNORDEREDACCESSVIEW *, const UINT *);
    VOID APIENTRY Dispatch(D3D10DDI_HDEVICE, UINT, UINT, UINT);
    VOID APIENTRY DispatchIndirect(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT);
    VOID APIENTRY CopyStructureCount(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3D11DDI_HUNORDEREDACCESSVIEW);

    /* draw.cpp */
    VOID APIENTRY DrawInstancedIndirect(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT);
    VOID APIENTRY DrawAuto(D3D10DDI_HDEVICE);
    VOID APIENTRY DrawIndexedInstancedIndirect(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT);
    VOID APIENTRY Draw(D3D10DDI_HDEVICE, UINT, UINT);
    VOID APIENTRY DrawIndexed(D3D10DDI_HDEVICE, UINT, UINT, INT);
    VOID APIENTRY DrawInstanced(D3D10DDI_HDEVICE, UINT, UINT, UINT, UINT);
    VOID APIENTRY DrawIndexedInstanced(D3D10DDI_HDEVICE, UINT, UINT, UINT, INT, UINT);
}
