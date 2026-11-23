/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline states and the shader and fixed function objects they are built from
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "objects.hpp"

/* Pipeline stages in the order a desc is gathered */
enum RxShaderStage
{
    RxStageVertex,
    RxStagePixel,
    RxStageDomain,
    RxStageHull,
    RxStageGeometry,
    RxStageCompute,
    RxStageAmplification,
    RxStageMesh,
    RxStageCount,
};

/* What a pipeline state is made from, gathered from a graphics or compute desc or a stream */
struct RxPipelineDesc
{
    ID3D12RootSignature* RootSignature;
    D3D12_SHADER_BYTECODE Shaders[RxStageCount];
    D3D12_STREAM_OUTPUT_DESC StreamOutput;
    D3D12_BLEND_DESC Blend;
    UINT SampleMask;
    D3D12_RASTERIZER_DESC2 Rasterizer;
    D3D12_DEPTH_STENCIL_DESC2 DepthStencil;
    D3D12_INPUT_LAYOUT_DESC InputLayout;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE StripCut;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE Topology;
    D3D12_RT_FORMAT_ARRAY Targets;
    DXGI_FORMAT DepthFormat;
    DXGI_SAMPLE_DESC Samples;
    UINT NodeMask;
    D3D12_CACHED_PIPELINE_STATE Cached;
    D3D12_PIPELINE_STATE_FLAGS Flags;
    D3D12_VIEW_INSTANCING_DESC ViewInstancing;
    BOOLEAN IsCompute;
};

/* Gathering from the two fixed descs; the older rasterizer and depth stencil forms widen on the way */
VOID
RxGatherGraphicsDesc(
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC& From,
    _Out_ RxPipelineDesc* Desc);

VOID
RxGatherComputeDesc(
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC& From,
    _Out_ RxPipelineDesc* Desc);

/* A shader or fixed function state the driver holds; equal descs share one */
class RxPipelinePart
{
public:
    enum class Kind
    {
        Shader,
        Blend,
        DepthStencil,
        Rasterizer,
        ElementLayout,
    };

    /* The part for this key, found in the device's cache or made through Build */
    static
    HRESULT
    Acquire(
        _In_ RxDevice12& Device,
        _In_ Kind PartKind,
        _In_reads_bytes_(KeySize) const BYTE* Key,
        _In_ UINT KeySize,
        _In_ HRESULT (*Build)(RxPipelinePart* Part, const VOID* Context),
        _In_opt_ const VOID* Context,
        _Outptr_ RxPipelinePart** Part);

    VOID
    AddUse()
    {
        InterlockedIncrement(&Uses);
    }

    VOID
    ReleaseUse();

    VOID*
    GetDriverHandle() const
    {
        return Handle;
    }

    /* A layout no element of which the shader reads has memory but no driver object */
    BOOLEAN
    HasDriverObject() const
    {
        return IsCreated;
    }

    RxDevice12&
    GetDevice() const
    {
        return Device;
    }

    /* Driver memory for the part, sized by the matching CalcPrivate call */
    _Ret_maybenull_
    VOID*
    AllocateDriverMemory(
        _In_ SIZE_T Size)
    {
        Handle = Memory.Allocate(Size);
        return Handle;
    }

    VOID
    MarkCreated(
        _In_ RxShaderStage ShaderStage)
    {
        Stage = ShaderStage;
        IsCreated = TRUE;
    }

    /* Storage a shader part keeps for the driver: the code and its signatures */
    BYTE* Code = nullptr;
    D3D12DDIARG_SIGNATURE_ENTRY_0012* Entries = nullptr;

    /* The DXIL dxilconv made from DXBC code, from CoTaskMemAlloc */
    BYTE* Converted = nullptr;
    UINT ConvertedSize = 0;

    RxPipelinePart* NextCached = nullptr;

private:
    RxPipelinePart(
        _In_ RxDevice12& Owner,
        _In_ Kind PartKind)
        : Device(Owner),
          PartKind(PartKind)
    {
    }

    ~RxPipelinePart();

    BOOLEAN
    Matches(
        _In_ Kind OtherKind,
        _In_reads_bytes_(Size) const BYTE* OtherKey,
        _In_ UINT Size) const
    {
        return OtherKind == PartKind && Size == KeySize && memcmp(OtherKey, Key, Size) == 0;
    }

    RxDevice12& Device;
    Kind PartKind;
    RxShaderStage Stage = RxStageVertex;
    LONG Uses = 1;
    BYTE* Key = nullptr;
    UINT KeySize = 0;
    BOOLEAN IsCached = FALSE;
    BOOLEAN IsCreated = FALSE;
    RxDriverMemory12 Memory;
    VOID* Handle = nullptr;
};

class RxPipelineState final : public RxDeviceChild12<ID3D12PipelineState>
{
public:
    static constexpr GUID PrivateId = {0x5d27e9b4, 0x1c83, 0x4a6f, {0xb5, 0x0d, 0x93, 0x4e, 0x7a, 0x21, 0xc6, 0x58}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const RxPipelineDesc& Desc,
        _In_ BOOLEAN Internal,
        _Outptr_ RxPipelineState** State);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCachedBlob(
        _COM_Outptr_ ID3DBlob** ppBlob) override;

    D3D12DDI_HPIPELINESTATE
    GetDriverHandle() const
    {
        return DriverState;
    }

    /* What list entries take: a bypass header that leads the driver to its pipeline */
    D3D12DDI_API_HPIPELINESTATE
    Api()
    {
        D3D12DDI_API_HPIPELINESTATE Handle;

        Handle.pDrvPrivate = &Bypass;
        return Handle;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxPipelineState() override;

    HRESULT
    Initialize(
        _In_ const RxPipelineDesc& Desc);

    HRESULT
    AcquireShader(
        _In_ const RxPipelineDesc& Desc,
        _In_ RxShaderStage Stage);

    HRESULT
    AcquireElementLayout(
        _In_ const RxPipelineDesc& Desc);

    BOOLEAN IsCompute = FALSE;
    RxRootSignature* Root = nullptr;
    RxPipelinePart* Shaders[RxStageCount] = {};
    RxPipelinePart* BlendState = nullptr;
    RxPipelinePart* DepthStencilState = nullptr;
    RxPipelinePart* RasterizerState = nullptr;
    RxPipelinePart* ElementLayout = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HPIPELINESTATE DriverState = {};
    D3D12DDI_RUNTIME_BYPASS_HEADER Bypass = {};
    BOOLEAN IsDriverStateCreated = FALSE;
};
