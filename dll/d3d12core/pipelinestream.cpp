/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline states described by a subobject stream
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "pipeline.hpp"

/* Subobject types the stream reader knows: everything below the serialized root signature */
constexpr UINT RxStreamTypeCount = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2 + 1;

/* Bytes one subobject of a type takes, its value padded out to pointer alignment; 0 for an unknown type */
static
SIZE_T
RxSubobjectStride(
    _In_ UINT Type)
{
    switch (Type)
    {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
            return 16;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
            return 24;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
            return 40;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
            return 336;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
            return 8;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
            return 48;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
            return 56;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
            return 64;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
            return 40;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
            return 16;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
            return 32;
        default:
            return 0;
    }
}

/* Repeats are found by the first depth stencil and rasterizer type; DEPTH_STENCIL1 and RASTERIZER1 count as those */
static
UINT
RxSubobjectSlot(
    _In_ UINT Type)
{
    switch (Type)
    {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
            return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
            return D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER;
        default:
            return Type;
    }
}

/* The value of a subobject sits after its type, at the value's own alignment */
template <typename Value>
static
const Value&
RxSubobjectValue(
    _In_ const BYTE* Subobject)
{
    constexpr SIZE_T Offset = alignof(Value) > sizeof(UINT) ? alignof(Value) : sizeof(UINT);

    return *reinterpret_cast<const Value*>(Subobject + Offset);
}

static
VOID
RxWidenRasterizer(
    _In_ const D3D12_RASTERIZER_DESC1& From,
    _Out_ D3D12_RASTERIZER_DESC2* To)
{
    To->FillMode = From.FillMode;
    To->CullMode = From.CullMode;
    To->FrontCounterClockwise = From.FrontCounterClockwise;
    To->DepthBias = From.DepthBias;
    To->DepthBiasClamp = From.DepthBiasClamp;
    To->SlopeScaledDepthBias = From.SlopeScaledDepthBias;
    To->DepthClipEnable = From.DepthClipEnable;
    if (From.MultisampleEnable)
        To->LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE;
    else if (From.AntialiasedLineEnable)
        To->LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
    else
        To->LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_ALIASED;
    To->ForcedSampleCount = From.ForcedSampleCount;
    To->ConservativeRaster = From.ConservativeRaster;
}

static
VOID
RxWidenDepthStencil(
    _In_ const D3D12_DEPTH_STENCIL_DESC1& From,
    _Out_ D3D12_DEPTH_STENCIL_DESC2* To)
{
    const D3D12_DEPTH_STENCILOP_DESC* Faces[] = { &From.FrontFace, &From.BackFace };
    D3D12_DEPTH_STENCILOP_DESC1* Wide[] = { &To->FrontFace, &To->BackFace };

    To->DepthEnable = From.DepthEnable;
    To->DepthWriteMask = From.DepthWriteMask;
    To->DepthFunc = From.DepthFunc;
    To->StencilEnable = From.StencilEnable;
    To->DepthBoundsTestEnable = From.DepthBoundsTestEnable;

    /* The older forms keep one pair of masks for both faces */
    for (UINT Face = 0; Face < 2; Face++)
    {
        Wide[Face]->StencilFailOp = Faces[Face]->StencilFailOp;
        Wide[Face]->StencilDepthFailOp = Faces[Face]->StencilDepthFailOp;
        Wide[Face]->StencilPassOp = Faces[Face]->StencilPassOp;
        Wide[Face]->StencilFunc = Faces[Face]->StencilFunc;
        Wide[Face]->StencilReadMask = From.StencilReadMask;
        Wide[Face]->StencilWriteMask = From.StencilWriteMask;
    }
}

/* What a stream leaves out takes the default each state has in the D3D12 helpers */
static
VOID
RxStreamDefaults(
    _Out_ RxPipelineDesc* Desc)
{
    ZeroMemory(Desc, sizeof(*Desc));

    for (D3D12_RENDER_TARGET_BLEND_DESC& Target : Desc->Blend.RenderTarget)
    {
        Target.SrcBlend = D3D12_BLEND_ONE;
        Target.DestBlend = D3D12_BLEND_ZERO;
        Target.BlendOp = D3D12_BLEND_OP_ADD;
        Target.SrcBlendAlpha = D3D12_BLEND_ONE;
        Target.DestBlendAlpha = D3D12_BLEND_ZERO;
        Target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        Target.LogicOp = D3D12_LOGIC_OP_NOOP;
        Target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    Desc->SampleMask = UINT_MAX;

    Desc->Rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    Desc->Rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    Desc->Rasterizer.DepthClipEnable = TRUE;
    Desc->Rasterizer.LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_ALIASED;

    /* Depth stays off unless a depth format turns the default on */
    Desc->DepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    Desc->DepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    for (D3D12_DEPTH_STENCILOP_DESC1* Face : { &Desc->DepthStencil.FrontFace, &Desc->DepthStencil.BackFace })
    {
        Face->StencilFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilPassOp = D3D12_STENCIL_OP_KEEP;
        Face->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        Face->StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        Face->StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    }

    Desc->Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Desc->Samples.Count = 1;
}

/* Bytes of the runtime info in a shader's pipeline validation part; FALSE when the info is shorter */
static
BOOLEAN
RxReadRuntimeInfo(
    _In_ const D3D12_SHADER_BYTECODE& Code,
    _In_ UINT Offset,
    _In_ UINT Size,
    _Out_writes_bytes_(Size) VOID* Value)
{
    const BYTE* Part;
    UINT PartSize;
    UINT InfoSize;

    ZeroMemory(Value, Size);
    if (!RxFindContainerPart(static_cast<const BYTE*>(Code.pShaderBytecode), Code.BytecodeLength,
                             'P' | ('S' << 8) | ('V' << 16) | ('0' << 24), &Part, &PartSize) ||
        PartSize < sizeof(UINT))
    {
        return FALSE;
    }

    CopyMemory(&InfoSize, Part, sizeof(InfoSize));
    if (InfoSize < Offset + Size || PartSize - sizeof(UINT) < Offset + Size)
        return FALSE;

    CopyMemory(Value, Part + sizeof(UINT) + Offset, Size);
    return TRUE;
}

/* The topology a mesh shader declares, 27 bytes into the second runtime info revision */
static
D3D12_PRIMITIVE_TOPOLOGY_TYPE
RxMeshTopology(
    _In_ const D3D12_SHADER_BYTECODE& Mesh)
{
    BYTE Topology;

    if (!RxReadRuntimeInfo(Mesh, 27, sizeof(Topology), &Topology))
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    if (Topology == 1)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    if (Topology == 2)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
}

/* A stream read into one desc; a repeated or unknown subobject fails the whole stream */
static
HRESULT
RxReadPipelineStream(
    _In_ const D3D12_PIPELINE_STATE_STREAM_DESC& Stream,
    _Out_ RxPipelineDesc* Desc)
{
    const BYTE* Base = static_cast<const BYTE*>(Stream.pPipelineStateSubobjectStream);
    BOOLEAN Seen[RxStreamTypeCount] = {};
    BOOLEAN HasDepthStencil = FALSE;
    BOOLEAN HasTopology = FALSE;
    SIZE_T Offset = 0;
    SIZE_T Stride;

    RxStreamDefaults(Desc);

    if (Stream.SizeInBytes == 0 || Base == nullptr)
        return E_INVALIDARG;

    while (Offset < Stream.SizeInBytes)
    {
        const BYTE* Subobject = Base + Offset;
        UINT Type = *reinterpret_cast<const UINT*>(Subobject);

        if (Type >= RxStreamTypeCount)
            return E_INVALIDARG;
        if (Seen[RxSubobjectSlot(Type)])
            return E_INVALIDARG;
        Seen[Type] = TRUE;

        Stride = RxSubobjectStride(Type);
        if (Stride == 0)
            return E_INVALIDARG;

        switch (Type)
        {
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
                Desc->RootSignature = RxSubobjectValue<ID3D12RootSignature*>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
                Desc->Shaders[RxStageVertex] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
                Desc->Shaders[RxStagePixel] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
                Desc->Shaders[RxStageDomain] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
                Desc->Shaders[RxStageHull] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
                Desc->Shaders[RxStageGeometry] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
                Desc->Shaders[RxStageCompute] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
                Desc->Shaders[RxStageAmplification] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS:
                Desc->Shaders[RxStageMesh] = RxSubobjectValue<D3D12_SHADER_BYTECODE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
                Desc->StreamOutput = RxSubobjectValue<D3D12_STREAM_OUTPUT_DESC>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
                Desc->Blend = RxSubobjectValue<D3D12_BLEND_DESC>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
                Desc->SampleMask = RxSubobjectValue<UINT>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER:
            {
                const D3D12_RASTERIZER_DESC& Old = RxSubobjectValue<D3D12_RASTERIZER_DESC>(Subobject);
                D3D12_RASTERIZER_DESC1 Middle;

                Middle.FillMode = Old.FillMode;
                Middle.CullMode = Old.CullMode;
                Middle.FrontCounterClockwise = Old.FrontCounterClockwise;
                Middle.DepthBias = (FLOAT)Old.DepthBias;
                Middle.DepthBiasClamp = Old.DepthBiasClamp;
                Middle.SlopeScaledDepthBias = Old.SlopeScaledDepthBias;
                Middle.DepthClipEnable = Old.DepthClipEnable;
                Middle.MultisampleEnable = Old.MultisampleEnable;
                Middle.AntialiasedLineEnable = Old.AntialiasedLineEnable;
                Middle.ForcedSampleCount = Old.ForcedSampleCount;
                Middle.ConservativeRaster = Old.ConservativeRaster;
                RxWidenRasterizer(Middle, &Desc->Rasterizer);
                break;
            }
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1:
                RxWidenRasterizer(RxSubobjectValue<D3D12_RASTERIZER_DESC1>(Subobject), &Desc->Rasterizer);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
                Desc->Rasterizer = RxSubobjectValue<D3D12_RASTERIZER_DESC2>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
            {
                const D3D12_DEPTH_STENCIL_DESC& Old = RxSubobjectValue<D3D12_DEPTH_STENCIL_DESC>(Subobject);
                D3D12_DEPTH_STENCIL_DESC1 Middle;

                Middle.DepthEnable = Old.DepthEnable;
                Middle.DepthWriteMask = Old.DepthWriteMask;
                Middle.DepthFunc = Old.DepthFunc;
                Middle.StencilEnable = Old.StencilEnable;
                Middle.StencilReadMask = Old.StencilReadMask;
                Middle.StencilWriteMask = Old.StencilWriteMask;
                Middle.FrontFace = Old.FrontFace;
                Middle.BackFace = Old.BackFace;
                Middle.DepthBoundsTestEnable = FALSE;
                RxWidenDepthStencil(Middle, &Desc->DepthStencil);
                HasDepthStencil = TRUE;
                break;
            }
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
                RxWidenDepthStencil(RxSubobjectValue<D3D12_DEPTH_STENCIL_DESC1>(Subobject), &Desc->DepthStencil);
                HasDepthStencil = TRUE;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
                Desc->DepthStencil = RxSubobjectValue<D3D12_DEPTH_STENCIL_DESC2>(Subobject);
                HasDepthStencil = TRUE;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
                Desc->InputLayout = RxSubobjectValue<D3D12_INPUT_LAYOUT_DESC>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
                Desc->StripCut = RxSubobjectValue<D3D12_INDEX_BUFFER_STRIP_CUT_VALUE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
                Desc->Topology = RxSubobjectValue<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(Subobject);
                HasTopology = TRUE;
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
                Desc->Targets = RxSubobjectValue<D3D12_RT_FORMAT_ARRAY>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
                Desc->DepthFormat = RxSubobjectValue<DXGI_FORMAT>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
                Desc->Samples = RxSubobjectValue<DXGI_SAMPLE_DESC>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK:
                Desc->NodeMask = RxSubobjectValue<UINT>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO:
                Desc->Cached = RxSubobjectValue<D3D12_CACHED_PIPELINE_STATE>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
                Desc->Flags = RxSubobjectValue<D3D12_PIPELINE_STATE_FLAGS>(Subobject);
                break;
            case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
                Desc->ViewInstancing = RxSubobjectValue<D3D12_VIEW_INSTANCING_DESC>(Subobject);
                break;
        }

        Offset += Stride;
    }

    if (!HasDepthStencil && Desc->DepthFormat != DXGI_FORMAT_UNKNOWN)
        Desc->DepthStencil.DepthEnable = TRUE;

    /* A mesh pipeline without a topology takes the one its mesh shader declares */
    if (Seen[D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS] && !HasTopology)
    {
        Desc->Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
        if (Desc->Shaders[RxStageMesh].pShaderBytecode != nullptr)
            Desc->Topology = RxMeshTopology(Desc->Shaders[RxStageMesh]);
    }

    Desc->IsCompute = Desc->Shaders[RxStageCompute].pShaderBytecode != nullptr;
    return S_OK;
}

static
BOOLEAN
RxHasStage(
    _In_ const RxPipelineDesc& Desc,
    _In_ RxShaderStage Stage)
{
    return Desc.Shaders[Stage].pShaderBytecode != nullptr;
}

/* Which shaders a stream may combine, and what a stream as a whole must hold */
static
HRESULT
RxCheckPipelineStream(
    _In_ RxDevice12& Device,
    _In_ const RxPipelineDesc& Desc)
{
    BOOLEAN Vertex = RxHasStage(Desc, RxStageVertex);
    BOOLEAN Compute = RxHasStage(Desc, RxStageCompute);
    BOOLEAN Mesh = RxHasStage(Desc, RxStageMesh);
    BOOLEAN Dxil = FALSE;
    BOOLEAN Dxbc = FALSE;
    const BYTE* Part;
    UINT Size;
    UINT Index;

    if (!Compute && (Device.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_CORE ||
                     Device.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_GENERIC))
    {
        return E_INVALIDARG;
    }

    if (Vertex && Compute)
        return E_INVALIDARG;
    if (!Vertex && !Compute && !Mesh)
        return E_INVALIDARG;
    if (RxHasStage(Desc, RxStageAmplification) && !Mesh)
        return E_INVALIDARG;
    if (Mesh && (Vertex || Compute || RxHasStage(Desc, RxStageHull) || RxHasStage(Desc, RxStageDomain) ||
                 RxHasStage(Desc, RxStageGeometry)))
    {
        return E_INVALIDARG;
    }

    /* Formats past the last target must be left unknown */
    if (Desc.Targets.NumRenderTargets > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT)
        return E_INVALIDARG;
    for (Index = Desc.Targets.NumRenderTargets; Index < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; Index++)
    {
        if (Desc.Targets.RTFormats[Index] != DXGI_FORMAT_UNKNOWN)
            return E_INVALIDARG;
    }

    /* Mesh pipelines take neither vertex input nor stream output */
    if (Mesh && (Desc.InputLayout.NumElements != 0 || Desc.StreamOutput.NumEntries != 0))
        return E_INVALIDARG;

    /* What the amplification shader sends is what the mesh shader reads */
    if (Mesh && RxHasStage(Desc, RxStageAmplification))
    {
        UINT Sent;
        UINT Read;

        RxReadRuntimeInfo(Desc.Shaders[RxStageAmplification], 0, sizeof(Sent), &Sent);
        RxReadRuntimeInfo(Desc.Shaders[RxStageMesh], 8, sizeof(Read), &Read);
        if (Sent != Read)
            return E_INVALIDARG;
    }

    /* One pipeline is all DXIL or all DXBC */
    for (const D3D12_SHADER_BYTECODE& Code : Desc.Shaders)
    {
        if (Code.pShaderBytecode == nullptr)
            continue;
        if (RxFindContainerPart(static_cast<const BYTE*>(Code.pShaderBytecode), Code.BytecodeLength,
                                'D' | ('X' << 8) | ('I' << 16) | ('L' << 24), &Part, &Size))
        {
            Dxil = TRUE;
        }
        else
        {
            Dxbc = TRUE;
        }
    }
    if (Dxil && Dxbc)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreatePipelineState(
    const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDesc Desc;
    RxPipelineState* State;
    HRESULT hr;

    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxReadPipelineStream(*pDesc, &Desc);
    if (FAILED(hr))
        return hr;
    hr = RxCheckPipelineStream(*this, Desc);
    if (FAILED(hr))
        return hr;

    if (Desc.IsCompute)
    {
        RxPipelineDesc Compute = {};

        Compute.RootSignature = Desc.RootSignature;
        Compute.Shaders[RxStageCompute] = Desc.Shaders[RxStageCompute];
        Compute.NodeMask = Desc.NodeMask;
        Compute.Cached = Desc.Cached;
        Compute.Flags = Desc.Flags;
        Compute.IsCompute = TRUE;
        Desc = Compute;
    }

    if (ppPipelineState == nullptr)
        return S_FALSE;

    hr = RxPipelineState::Create(*this, Desc, FALSE, &State);
    if (FAILED(hr))
        return hr;
    return RxHandOut12(State, riid, ppPipelineState);
}
