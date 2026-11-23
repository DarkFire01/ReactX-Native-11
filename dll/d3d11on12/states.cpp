/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Capability queries, fixed function state objects and samplers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device.hpp"

/* D3D12 format support bits and the DDI bits they turn into */
struct O12SupportBit
{
    UINT64 D3D12;
    UINT Ddi;
};

static const O12SupportBit O12SupportBits[] =
{
    { D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE },
    { D3D12_FORMAT_SUPPORT1_RENDER_TARGET, D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET },
    { D3D12_FORMAT_SUPPORT1_BLENDABLE, D3D10_DDI_FORMAT_SUPPORT_BLENDABLE },
    { D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET, D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET },
    { D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD, D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD },
    { D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT, D3D11_1DDI_FORMAT_SUPPORT_DECODER_OUTPUT },
    { D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT, D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT },
    { D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT, D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT },
    { D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER, D3D11_1DDI_FORMAT_SUPPORT_VERTEX_BUFFER },
    { D3D12_FORMAT_SUPPORT1_BUFFER, D3D11_1DDI_FORMAT_SUPPORT_BUFFER },
    { 0x80000000ull, D3D11_1DDI_FORMAT_SUPPORT_CAPTURE },
    { D3D12_FORMAT_SUPPORT1_SHADER_GATHER, D3D11_1DDI_FORMAT_SUPPORT_SHADER_GATHER },
    { UINT64(D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP) << 32, D3D11_1DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP },
    { UINT64(D3D12_FORMAT_SUPPORT2_TILED) << 32, D3DWDDM1_3DDI_FORMAT_SUPPORT_TILED },
    { UINT64(D3D12_FORMAT_SUPPORT2_MULTIPLANE_OVERLAY) << 32, D3D11_1DDI_FORMAT_SUPPORT_MULTIPLANE_OVERLAY },
    { UINT64(D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) << 32, D3D11_1DDI_FORMAT_SUPPORT_UAV_WRITES },
    { UINT64(D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) << 32, D3DWDDM2_0DDI_FORMAT_SUPPORT_UAV_READS },
};

/* Only the video formats keep the decoder output bit */
static
BOOLEAN
O12CanDecodeTo(
    _In_ DXGI_FORMAT Format)
{
    return Format >= DXGI_FORMAT_NV12 && Format <= DXGI_FORMAT_YUY2;
}

static
VOID
APIENTRY
O12CheckFormatSupport(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Support)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Query = {};
    UINT64 Bits;
    HRESULT hr;

    Query.Format = Format;
    hr = O12Device::From(Device)->GetD3D12()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Query, sizeof(Query));
    if (FAILED(hr))
    {
        *Support = D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;
        return;
    }

    if ((Query.Support1 & D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT) != 0 && !O12CanDecodeTo(Format))
        Query.Support1 &= ~D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT;

    Bits = UINT64(Query.Support1) | (UINT64(Query.Support2) << 32);
    for (const O12SupportBit& Each : O12SupportBits)
    {
        if ((Bits & Each.D3D12) != 0)
            *Support |= Each.Ddi;
    }
}

static
VOID
APIENTRY
O12CheckMultisampleQualityLevels(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ UINT Flags,
    _Out_ UINT* Levels)
{
    O12Device* Self = O12Device::From(Device);
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Query = {};

    if (Self->GetAdapter()->IsComputeOnly())
    {
        *Levels = 0;
        return;
    }

    Query.Format = Format;
    Query.SampleCount = SampleCount;
    Query.Flags = (Flags & D3DWDDM1_3DDI_CHECK_MULTISAMPLE_QUALITY_LEVELS_TILED_RESOURCE) ?
                  D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE : D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    if (FAILED(Self->GetD3D12()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Query, sizeof(Query))))
        Query.NumQualityLevels = 0;
    *Levels = Query.NumQualityLevels;
}

static
VOID
APIENTRY
O12CheckCounterInfo(
    _In_ D3D10DDI_HDEVICE Device,
    _Out_ D3D10DDI_COUNTER_INFO* Info)
{
    UNREFERENCED_PARAMETER(Device);

    Info->LastDeviceDependentCounter = static_cast<D3D10DDI_QUERY>(0);
    Info->NumSimultaneousCounters = 0;
    Info->NumDetectableParallelUnits = 1;
}

static
VOID
APIENTRY
O12CheckDeferredContextHandleSizes(
    _In_ D3D10DDI_HDEVICE Device,
    _Inout_ UINT* Count,
    _Out_writes_opt_(*Count) D3D11DDI_HANDLESIZE* Sizes)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Sizes);

    *Count = 0;
}

static
SIZE_T
APIENTRY
O12CalcDeferredContextHandleSize(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D11DDI_HANDLETYPE Type,
    _In_opt_ VOID* Object)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Type);
    UNREFERENCED_PARAMETER(Object);

    return 0;
}

/* State objects keep the runtime's description; pipeline state is built from them at draw time */
struct O12BlendState
{
    D3D11_1_DDI_BLEND_DESC Desc;
};

struct O12DepthStencilState
{
    D3D10_DDI_DEPTH_STENCIL_DESC Desc;
};

struct O12RasterizerState
{
    D3DWDDM2_0DDI_RASTERIZER_DESC Desc;
};

struct O12Sampler
{
    O12Device* Device;
    D3D12_CPU_DESCRIPTOR_HANDLE Slot;
};

static
SIZE_T
APIENTRY
O12CalcBlendStateSize(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11_1_DDI_BLEND_DESC* Desc)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Desc);

    return sizeof(O12BlendState);
}

static
VOID
APIENTRY
O12CreateBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11_1_DDI_BLEND_DESC* Desc,
    _In_ D3D10DDI_HBLENDSTATE State,
    _In_ D3D10DDI_HRTBLENDSTATE Runtime)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Runtime);

    static_cast<O12BlendState*>(State.pDrvPrivate)->Desc = *Desc;
}

static
VOID
APIENTRY
O12DestroyBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HBLENDSTATE State)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(State);
}

static
SIZE_T
APIENTRY
O12CalcDepthStencilStateSize(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_DEPTH_STENCIL_DESC* Desc)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Desc);

    return sizeof(O12DepthStencilState);
}

static
VOID
APIENTRY
O12CreateDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_DEPTH_STENCIL_DESC* Desc,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State,
    _In_ D3D10DDI_HRTDEPTHSTENCILSTATE Runtime)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Runtime);

    static_cast<O12DepthStencilState*>(State.pDrvPrivate)->Desc = *Desc;
}

static
VOID
APIENTRY
O12DestroyDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(State);
}

static
SIZE_T
APIENTRY
O12CalcRasterizerStateSize(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDI_RASTERIZER_DESC* Desc)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Desc);

    return sizeof(O12RasterizerState);
}

static
VOID
APIENTRY
O12CreateRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDI_RASTERIZER_DESC* Desc,
    _In_ D3D10DDI_HRASTERIZERSTATE State,
    _In_ D3D10DDI_HRTRASTERIZERSTATE Runtime)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Runtime);

    static_cast<O12RasterizerState*>(State.pDrvPrivate)->Desc = *Desc;
}

static
VOID
APIENTRY
O12DestroyRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRASTERIZERSTATE State)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(State);
}

static
SIZE_T
APIENTRY
O12CalcSamplerSize(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_SAMPLER_DESC* Desc)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Desc);

    return sizeof(O12Sampler);
}

/* The DDI sampler description has the D3D12 layout, so it is handed over as is */
static
VOID
APIENTRY
O12CreateSampler(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10_DDI_SAMPLER_DESC* Desc,
    _In_ D3D10DDI_HSAMPLER Sampler,
    _In_ D3D10DDI_HRTSAMPLER Runtime)
{
    O12Device* Self = O12Device::From(Device);
    O12Sampler* Object = static_cast<O12Sampler*>(Sampler.pDrvPrivate);
    O12Context& Context = Self->GetContext();

    UNREFERENCED_PARAMETER(Runtime);
    static_assert(sizeof(D3D10_DDI_SAMPLER_DESC) == sizeof(D3D12_SAMPLER_DESC), "sampler layouts differ");

    Object->Device = Self;
    Object->Slot = {};
    if (Context.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_CORE)
        return;

    if (FAILED(Context.GetSamplerPool().Allocate(&Object->Slot)))
    {
        O12Trace(L"no sampler slot left");
        return;
    }
    Self->GetD3D12()->CreateSampler(reinterpret_cast<const D3D12_SAMPLER_DESC*>(Desc), Object->Slot);
}

static
VOID
APIENTRY
O12DestroySampler(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSAMPLER Sampler)
{
    O12Sampler* Object = static_cast<O12Sampler*>(Sampler.pDrvPrivate);

    if (Object->Slot.ptr != 0)
        O12Device::From(Device)->GetContext().GetSamplerPool().Free(Object->Slot);
}

static
VOID
APIENTRY
O12SetBlendState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HBLENDSTATE State,
    _In_ const FLOAT Factor[4],
    _In_ UINT SampleMask)
{
    O12Device* Self = O12Device::From(Device);
    O12PipelineInputs& Inputs = Self->GetInputs();

    if (Self->GetAdapter()->IsComputeOnly())
        return;

    Inputs.Blend = State.pDrvPrivate;
    Inputs.SampleMask = SampleMask;
    Inputs.Dirty |= O12DirtyPipeline;
    Self->GetContext().QueueBlendFactor(Factor);
}

static
VOID
APIENTRY
O12SetDepthStencilState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILSTATE State,
    _In_ UINT StencilRef)
{
    O12Device* Self = O12Device::From(Device);
    O12PipelineInputs& Inputs = Self->GetInputs();

    if (Self->GetAdapter()->IsComputeOnly())
        return;

    Inputs.DepthStencil = State.pDrvPrivate;
    Inputs.Dirty |= O12DirtyPipeline;
    Self->GetContext().QueueStencilRef(StencilRef);
}

static
VOID
APIENTRY
O12SetRasterizerState(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRASTERIZERSTATE State)
{
    O12Device* Self = O12Device::From(Device);
    O12PipelineInputs& Inputs = Self->GetInputs();

    if (Self->GetAdapter()->IsComputeOnly())
        return;

    Inputs.Rasterizer = State.pDrvPrivate;
    Inputs.Dirty |= O12DirtyPipeline;
}

/* Sampler bindings per stage; descriptors are copied into the shader visible heap at draw time */
template <O12Stage Stage>
static
VOID
APIENTRY
O12SetSamplers(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_(Count) const D3D10DDI_HSAMPLER* Samplers)
{
    O12PipelineInputs& Inputs = O12Device::From(Device)->GetInputs();

    for (UINT Index = 0; Index < Count && Start + Index < D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT; Index++)
        Inputs.Samplers[Stage][Start + Index] = Samplers[Index].pDrvPrivate;
    Inputs.Dirty |= O12DirtySamplers;
}

struct O12CacheSession
{
    O12Device* Device;
    D3DWDDM2_2DDI_HRTCACHESESSION Runtime;
};

static
SIZE_T
APIENTRY
O12CalcCacheSessionSize(
    _In_ D3D10DDI_HDEVICE Device)
{
    UNREFERENCED_PARAMETER(Device);

    return sizeof(O12CacheSession);
}

static
VOID
APIENTRY
O12CreateCacheSession(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3DWDDM2_2DDI_HCACHESESSION Session,
    _In_ D3DWDDM2_2DDI_HRTCACHESESSION Runtime)
{
    O12CacheSession* Object = static_cast<O12CacheSession*>(Session.pDrvPrivate);

    Object->Device = O12Device::From(Device);
    Object->Runtime = Runtime;
}

static
VOID
APIENTRY
O12DestroyCacheSession(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3DWDDM2_2DDI_HCACHESESSION Session)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Session);
}

static
VOID
APIENTRY
O12SetCacheSession(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3DWDDM2_2DDI_HCACHESESSION Session)
{
    O12Device::From(Device)->GetInputs().CacheSession = Session.pDrvPrivate;
}

VOID
O12FillStateFunctions(
    _Inout_ D3DWDDM2_6DDI_DEVICEFUNCS* Functions)
{
    Functions->pfnCheckFormatSupport = O12CheckFormatSupport;
    Functions->pfnCheckMultisampleQualityLevels = O12CheckMultisampleQualityLevels;
    Functions->pfnCheckCounterInfo = O12CheckCounterInfo;
    Functions->pfnCheckDeferredContextHandleSizes = O12CheckDeferredContextHandleSizes;
    Functions->pfnCalcDeferredContextHandleSize = O12CalcDeferredContextHandleSize;

    Functions->pfnCalcPrivateBlendStateSize = O12CalcBlendStateSize;
    Functions->pfnCreateBlendState = O12CreateBlendState;
    Functions->pfnDestroyBlendState = O12DestroyBlendState;
    Functions->pfnCalcPrivateDepthStencilStateSize = O12CalcDepthStencilStateSize;
    Functions->pfnCreateDepthStencilState = O12CreateDepthStencilState;
    Functions->pfnDestroyDepthStencilState = O12DestroyDepthStencilState;
    Functions->pfnCalcPrivateRasterizerStateSize = O12CalcRasterizerStateSize;
    Functions->pfnCreateRasterizerState = O12CreateRasterizerState;
    Functions->pfnDestroyRasterizerState = O12DestroyRasterizerState;
    Functions->pfnCalcPrivateSamplerSize = O12CalcSamplerSize;
    Functions->pfnCreateSampler = O12CreateSampler;
    Functions->pfnDestroySampler = O12DestroySampler;

    Functions->pfnSetBlendState = O12SetBlendState;
    Functions->pfnSetDepthStencilState = O12SetDepthStencilState;
    Functions->pfnSetRasterizerState = O12SetRasterizerState;
    Functions->pfnVsSetSamplers = O12SetSamplers<O12StageVertex>;
    Functions->pfnHsSetSamplers = O12SetSamplers<O12StageHull>;
    Functions->pfnDsSetSamplers = O12SetSamplers<O12StageDomain>;
    Functions->pfnGsSetSamplers = O12SetSamplers<O12StageGeometry>;
    Functions->pfnPsSetSamplers = O12SetSamplers<O12StagePixel>;
    Functions->pfnCsSetSamplers = O12SetSamplers<O12StageCompute>;

    Functions->pfnCalcPrivateShaderCacheSessionSize = O12CalcCacheSessionSize;
    Functions->pfnCreateShaderCacheSession = O12CreateCacheSession;
    Functions->pfnDestroyShaderCacheSession = O12DestroyCacheSession;
    Functions->pfnSetShaderCacheSession = O12SetCacheSession;
}
