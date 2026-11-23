/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 shaders, fixed function states, root signatures, pipeline states, query heaps and command signatures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"
#include "dxil.hpp"

static constexpr UINT RxWarp12ContainerMagic = 0x43425844;     /* "DXBC" */
static constexpr UINT RxWarp12ChunkShdr = 0x52444853;
static constexpr UINT RxWarp12ChunkShex = 0x58454853;

/* The tokenized program inside a shader blob; a bare program comes back as it is */
static
const UINT *
RxWarp12Program(
    _In_ const UINT *Code)
{
    UINT Chunks;

    if (Code == nullptr || Code[0] != RxWarp12ContainerMagic)
        return Code;
    Chunks = Code[7];
    for (UINT Index = 0; Index < Chunks; Index++)
    {
        const UINT *Chunk = reinterpret_cast<const UINT *>(reinterpret_cast<const BYTE *>(Code) + Code[8 + Index]);

        if (Chunk[0] == RxWarp12ChunkShdr || Chunk[0] == RxWarp12ChunkShex)
            return Chunk + 2;
    }
    return nullptr;
}

static
RxWarpShader *
RxWarp12NewShader(
    _In_ const UINT *Code,
    _In_ D3D12DDI_HSHADER Shader)
{
    RxWarpShader *Object = new (std::nothrow) RxWarpShader();
    BOOLEAN Dxil = RxWarpIsDxil(Code);
    UINT *Lowered = Dxil ? RxWarpLowerDxil(Code) : nullptr;
    const UINT *Program = Dxil ? Lowered : RxWarp12Program(Code);

    RxWarpBind(Shader, Object);
    if (Object == nullptr)
    {
        RxWarpFreeLowered(Lowered);
        RX_WARP_TRACE("D3D12 shader allocation failed\n");
        return nullptr;
    }
    if (Program == nullptr || FAILED(Object->Initialize(Program)))
        RX_WARP_TRACE("D3D12 shader decode failed\n");
    RxWarpFreeLowered(Lowered);
    return Object;
}

static
VOID
APIENTRY
RxWarp12CreateShader(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_SHADER_0026 *Args,
    _In_ D3D12DDI_HSHADER Shader)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    RxWarp12NewShader(Args->pShaderCode, Shader);
}

static
VOID
APIENTRY
RxWarp12CreateGeometryShaderWithStreamOutput(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_GEOMETRY_SHADER_WITH_STREAM_OUTPUT_0026 *Args,
    _In_ D3D12DDI_HSHADER Shader)
{
    RxWarpLock Lock;
    RxWarpShader *Object;

    UNREFERENCED_PARAMETER(Device);
    if (Args->CreateShader.pShaderCode != nullptr)
    {
        Object = RxWarp12NewShader(Args->CreateShader.pShaderCode, Shader);
    }
    else
    {
        Object = new (std::nothrow) RxWarpShader();
        RxWarpBind(Shader, Object);
        if (Object != nullptr)
            Object->PassThrough = TRUE;
    }
    if (Object == nullptr)
        return;
    for (UINT Index = 0; Index < Args->NumEntries; Index++)
    {
        D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY Entry;

        Entry.Stream = Args->pOutputStreamDecl[Index].Stream;
        Entry.OutputSlot = Args->pOutputStreamDecl[Index].OutputSlot;
        Entry.RegisterIndex = Args->pOutputStreamDecl[Index].RegisterIndex;
        Entry.RegisterMask = Args->pOutputStreamDecl[Index].RegisterMask;
        Object->StreamDecl.push_back(Entry);
    }
    for (UINT Index = 0; Index < Args->NumStrides && Index < 4; Index++)
        Object->StreamStrides[Index] = Args->BufferStridesInBytes[Index];
    Object->RasterizedStream = Args->RasterizedStream;
}

static
VOID
APIENTRY
RxWarp12DestroyShader(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HSHADER Shader)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarpShader>(Shader);
}

/* The fixed function states keep the Direct3D 11 shape the executor reads */
static
VOID
APIENTRY
RxWarp12CreateBlendState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDI_BLEND_DESC_0010 *Desc,
    _In_ D3D12DDI_HBLENDSTATE State)
{
    RxWarpLock Lock;
    RxWarpBlendState *Object = new (std::nothrow) RxWarpBlendState();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(State, Object);
    if (Object == nullptr)
        return;
    Object->Desc.AlphaToCoverageEnable = Desc->AlphaToCoverageEnable;
    Object->Desc.IndependentBlendEnable = Desc->IndependentBlendEnable;
    for (UINT Index = 0; Index < 8; Index++)
    {
        const D3D12DDI_RENDER_TARGET_BLEND_DESC &From = Desc->RenderTarget[Desc->IndependentBlendEnable ? Index : 0];
        D3D11_1_DDI_RENDER_TARGET_BLEND_DESC &To = Object->Desc.RenderTarget[Index];

        To.BlendEnable = From.BlendEnable;
        To.LogicOpEnable = From.LogicOpEnable;
        To.SrcBlend = (D3D10_DDI_BLEND)From.SrcBlend;
        To.DestBlend = (D3D10_DDI_BLEND)From.DestBlend;
        To.BlendOp = (D3D10_DDI_BLEND_OP)From.BlendOp;
        To.SrcBlendAlpha = (D3D10_DDI_BLEND)From.SrcBlendAlpha;
        To.DestBlendAlpha = (D3D10_DDI_BLEND)From.DestBlendAlpha;
        To.BlendOpAlpha = (D3D10_DDI_BLEND_OP)From.BlendOpAlpha;
        To.LogicOp = (D3D11_1_DDI_LOGIC_OP)From.LogicOp;
        To.RenderTargetWriteMask = From.RenderTargetWriteMask;
    }
}

static
VOID
APIENTRY
RxWarp12DestroyBlendState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HBLENDSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarpBlendState>(State);
}

static
D3D10_DDI_DEPTH_STENCILOP_DESC
RxWarp12StencilFace(
    _In_ const D3D12DDI_DEPTH_STENCILOP_DESC &Face)
{
    D3D10_DDI_DEPTH_STENCILOP_DESC Result;

    Result.StencilFailOp = (D3D10_DDI_STENCIL_OP)Face.StencilFailOp;
    Result.StencilDepthFailOp = (D3D10_DDI_STENCIL_OP)Face.StencilDepthFailOp;
    Result.StencilPassOp = (D3D10_DDI_STENCIL_OP)Face.StencilPassOp;
    Result.StencilFunc = (D3D10_DDI_COMPARISON_FUNC)Face.StencilFunc;
    return Result;
}

/* Both faces share one pair of masks in the executor; the front face's are taken */
static
VOID
APIENTRY
RxWarp12CreateDepthStencilState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDI_DEPTH_STENCIL_DESC_0095 *Desc,
    _In_ D3D12DDI_HDEPTHSTENCILSTATE State)
{
    RxWarpLock Lock;
    RxWarpDepthStencilState *Object = new (std::nothrow) RxWarpDepthStencilState();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(State, Object);
    if (Object == nullptr)
        return;
    Object->Desc.DepthEnable = Desc->DepthEnable;
    Object->Desc.DepthWriteMask = (D3D10_DDI_DEPTH_WRITE_MASK)Desc->DepthWriteMask;
    Object->Desc.DepthFunc = (D3D10_DDI_COMPARISON_FUNC)Desc->DepthFunc;
    Object->Desc.StencilEnable = Desc->StencilEnable;
    Object->Desc.FrontEnable = Desc->FrontEnable;
    Object->Desc.BackEnable = Desc->BackEnable;
    Object->Desc.StencilReadMask = Desc->FrontFaceStencilReadMask;
    Object->Desc.StencilWriteMask = Desc->FrontFaceStencilWriteMask;
    Object->Desc.FrontFace = RxWarp12StencilFace(Desc->FrontFace);
    Object->Desc.BackFace = RxWarp12StencilFace(Desc->BackFace);
}

static
VOID
APIENTRY
RxWarp12DestroyDepthStencilState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HDEPTHSTENCILSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarpDepthStencilState>(State);
}

/* Line modes stand for the two Direct3D 11 switches; quadrilateral lines are the multisampled ones */
static
VOID
APIENTRY
RxWarp12CreateRasterizerState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDI_RASTERIZER_DESC_0102 *Desc,
    _In_ D3D12DDI_HRASTERIZERSTATE State)
{
    RxWarpLock Lock;
    RxWarpRasterizerState *Object = new (std::nothrow) RxWarpRasterizerState();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(State, Object);
    if (Object == nullptr)
        return;
    Object->Desc.FillMode = (D3D10_DDI_FILL_MODE)Desc->FillMode;
    Object->Desc.CullMode = (D3D10_DDI_CULL_MODE)Desc->CullMode;
    Object->Desc.FrontCounterClockwise = Desc->FrontCounterClockwise;
    Object->Desc.DepthBias = (INT)Desc->DepthBias;
    Object->Desc.DepthBiasClamp = Desc->DepthBiasClamp;
    Object->Desc.SlopeScaledDepthBias = Desc->SlopeScaledDepthBias;
    Object->Desc.DepthClipEnable = Desc->DepthClipEnable;
    Object->Desc.ScissorEnable = TRUE;
    Object->Desc.MultisampleEnable = Desc->LineRasterizationMode == D3D12DDI_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE ||
                                     Desc->LineRasterizationMode == D3D12DDI_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW;
    Object->Desc.AntialiasedLineEnable = Desc->LineRasterizationMode == D3D12DDI_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
    Object->Desc.ForcedSampleCount = Desc->ForcedSampleCount;
    Object->Desc.ConservativeRasterizationMode = (D3DWDDM2_0DDI_CONSERVATIVE_RASTERIZATION_MODE)Desc->ConservativeRasterizationMode;
}

static
VOID
APIENTRY
RxWarp12DestroyRasterizerState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HRASTERIZERSTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarpRasterizerState>(State);
}

static
VOID
APIENTRY
RxWarp12CreateElementLayout(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATEELEMENTLAYOUT_0010 *Args,
    _In_ D3D12DDI_HELEMENTLAYOUT Layout)
{
    RxWarpLock Lock;
    RxWarpElementLayout *Object = new (std::nothrow) RxWarpElementLayout();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(Layout, Object);
    if (Object == nullptr)
        return;
    for (UINT Index = 0; Index < Args->NumElements; Index++)
    {
        const D3D12DDIARG_INPUT_ELEMENT_DESC &From = Args->pVertexElements[Index];
        D3D10DDIARG_INPUT_ELEMENT_DESC To;

        To.InputSlot = From.InputSlot;
        To.AlignedByteOffset = From.AlignedByteOffset;
        To.Format = From.Format;
        To.InputSlotClass = (D3D10_DDI_INPUT_CLASSIFICATION)From.InputSlotClass;
        To.InstanceDataStepRate = From.InstanceDataStepRate;
        To.InputRegister = From.InputRegister;
        Object->Elements.push_back(To);
    }
}

static
VOID
APIENTRY
RxWarp12DestroyElementLayout(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HELEMENTLAYOUT Layout)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarpElementLayout>(Layout);
}

/* Static border colors as the sampler reads them */
static
VOID
RxWarp12BorderColor(
    _In_ D3D12DDI_STATIC_BORDER_COLOR Color,
    _Out_writes_(4) FLOAT *Value)
{
    FLOAT Shade = (Color == D3D12DDI_STATIC_BORDER_COLOR_TRANSPARENT_BLACK || Color == D3D12DDI_STATIC_BORDER_COLOR_OPAQUE_BLACK ||
                   Color == D3D12DDI_STATIC_BORDER_COLOR_OPAQUE_BLACK_UINT) ? 0.0f : 1.0f;

    Value[0] = Shade;
    Value[1] = Shade;
    Value[2] = Shade;
    Value[3] = (Color == D3D12DDI_STATIC_BORDER_COLOR_TRANSPARENT_BLACK) ? 0.0f : 1.0f;
}

static
HRESULT
RxWarp12ReadRootSignature(
    _Inout_ RxWarp12RootSignature *Object,
    _In_ const D3D12DDI_ROOT_SIGNATURE_0100 *Desc)
{
    for (UINT Index = 0; Index < Desc->NumParameters; Index++)
    {
        const D3D12DDI_ROOT_PARAMETER_0013 &From = Desc->pRootParameters[Index];
        RX_WARP12_ROOT_PARAMETER To = {};

        To.Type = From.ParameterType;
        To.Visibility = From.ShaderVisibility;
        if (From.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
        {
            UINT Next = 0;

            for (UINT Range = 0; Range < From.DescriptorTable.NumDescriptorRanges; Range++)
            {
                const D3D12DDI_DESCRIPTOR_RANGE_0013 &Source = From.DescriptorTable.pDescriptorRanges[Range];
                RX_WARP12_ROOT_RANGE Item;

                Item.Kind = Source.RangeType;
                Item.Base = Source.BaseShaderRegister;
                Item.Count = Source.NumDescriptors;
                Item.Space = Source.RegisterSpace;
                Item.Offset = (Source.OffsetInDescriptorsFromTableStart == UINT_MAX) ? Next : Source.OffsetInDescriptorsFromTableStart;
                Next = (Item.Count == UINT_MAX) ? UINT_MAX : Item.Offset + Item.Count;
                To.Ranges.push_back(Item);
            }
        }
        else if (From.ParameterType == D3D12DDI_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        {
            To.Register = From.Constants.ShaderRegister;
            To.Space = From.Constants.RegisterSpace;
            To.Values = From.Constants.Num32BitValues;
        }
        else
        {
            To.Register = From.Descriptor.ShaderRegister;
            To.Space = From.Descriptor.RegisterSpace;
        }
        Object->Parameters.push_back(std::move(To));
    }
    for (UINT Index = 0; Index < Desc->NumStaticSamplers; Index++)
    {
        const D3D12DDI_STATIC_SAMPLER_0100 &From = Desc->pStaticSamplers[Index];
        RX_WARP12_STATIC_SAMPLER To = {};

        To.Sampler.Desc.Filter = (D3D10_DDI_FILTER)From.Filter;
        To.Sampler.Desc.AddressU = (D3D10_DDI_TEXTURE_ADDRESS_MODE)From.AddressU;
        To.Sampler.Desc.AddressV = (D3D10_DDI_TEXTURE_ADDRESS_MODE)From.AddressV;
        To.Sampler.Desc.AddressW = (D3D10_DDI_TEXTURE_ADDRESS_MODE)From.AddressW;
        To.Sampler.Desc.MipLODBias = From.MipLODBias;
        To.Sampler.Desc.MaxAnisotropy = From.MaxAnisotropy;
        To.Sampler.Desc.ComparisonFunc = (D3D10_DDI_COMPARISON_FUNC)From.ComparisonFunc;
        RxWarp12BorderColor(From.BorderColor, To.Sampler.Desc.BorderColor);
        To.Sampler.Desc.MinLOD = From.MinLOD;
        To.Sampler.Desc.MaxLOD = From.MaxLOD;
        To.Register = From.ShaderRegister;
        To.Space = From.RegisterSpace;
        To.Visibility = From.ShaderVisibility;
        Object->Samplers.push_back(To);
    }
    if (Object->Parameters.size() > RxWarp12RootSlots)
        return E_INVALIDARG;
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarp12CreateRootSignature(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0100 *Args,
    _In_ D3D12DDI_HROOTSIGNATURE Signature)
{
    RxWarpLock Lock;
    RxWarp12Device *This = RxWarpObject<RxWarp12Device>(Device);
    RxWarp12RootSignature *Object;
    HRESULT hr;

    Object = new (std::nothrow) RxWarp12RootSignature();
    RxWarpBind(Signature, Object);
    if (Object == nullptr)
    {
        RX_WARP_TRACE("D3D12 root signature allocation failed\n");
        This->Callbacks->pfnSetErrorCb(This->RuntimeDevice, E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    Object->Device = This;
    hr = RxWarp12ReadRootSignature(Object, Args->pRootSignature_1_2);
    if (FAILED(hr))
        RX_WARP_TRACE("D3D12 root signature not read: %08lx\n", hr);
    return hr;
}

static
VOID
APIENTRY
RxWarp12DestroyRootSignature(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HROOTSIGNATURE Signature)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12RootSignature>(Signature);
}

static
HRESULT
APIENTRY
RxWarp12CreatePipelineState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_PIPELINE_STATE_0099 *Args,
    _In_ D3D12DDI_HPIPELINESTATE State,
    _In_ D3D12DDI_HRTPIPELINESTATE RuntimeState)
{
    RxWarpLock Lock;
    RxWarp12PipelineState *Object;
    RxWarpBlendState *Blend = RxWarpObject<RxWarpBlendState>(Args->hBlendState);
    RxWarpDepthStencilState *DepthStencil = RxWarpObject<RxWarpDepthStencilState>(Args->hDepthStencilState);
    RxWarpRasterizerState *Rasterizer = RxWarpObject<RxWarpRasterizerState>(Args->hRasterizerState);
    RxWarpElementLayout *Layout = RxWarpObject<RxWarpElementLayout>(Args->hElementLayout);

    UNREFERENCED_PARAMETER(RuntimeState);
    Object = new (std::nothrow) RxWarp12PipelineState();
    RxWarpBind(State, Object);
    if (Object == nullptr)
    {
        RX_WARP_TRACE("D3D12 pipeline state allocation failed\n");
        return E_OUTOFMEMORY;
    }
    Object->Device = RxWarpObject<RxWarp12Device>(Device);
    Object->Shaders[RxWarpStageCompute] = RxWarpObject<RxWarpShader>(Args->hComputeShader);
    Object->Compute = Object->Shaders[RxWarpStageCompute] != nullptr;
    if (Object->Compute)
        return S_OK;

    Object->Shaders[RxWarpStageVertex] = RxWarpObject<RxWarpShader>(Args->hVertexShader);
    Object->Shaders[RxWarpStagePixel] = RxWarpObject<RxWarpShader>(Args->hPixelShader);
    Object->Shaders[RxWarpStageGeometry] = RxWarpObject<RxWarpShader>(Args->hGeometryShader);
    Object->Shaders[RxWarpStageHull] = RxWarpObject<RxWarpShader>(Args->hHullShader);
    Object->Shaders[RxWarpStageDomain] = RxWarpObject<RxWarpShader>(Args->hDomainShader);
    if (Blend != nullptr)
        Object->Blend = *Blend;
    if (DepthStencil != nullptr)
        Object->DepthStencil = *DepthStencil;
    if (Rasterizer != nullptr)
        Object->Rasterizer = *Rasterizer;
    if (Layout != nullptr)
        Object->Layout.Elements = Layout->Elements;

    /* Depth and stencil only run with a depth format to run against */
    if (Args->DSVFormat == DXGI_FORMAT_UNKNOWN)
    {
        Object->DepthStencil.Desc.DepthEnable = FALSE;
        Object->DepthStencil.Desc.StencilEnable = FALSE;
    }
    Object->SampleMask = Args->SampleMask;
    Object->StripCut = Args->IBStripCutValue;
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyPipelineState(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HPIPELINESTATE State)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12PipelineState>(State);
}

static
HRESULT
APIENTRY
RxWarp12CreateQueryHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_QUERY_HEAP_0001 *Args,
    _In_ D3D12DDI_HQUERYHEAP Heap)
{
    RxWarpLock Lock;
    RxWarp12QueryHeap *Object = new (std::nothrow) RxWarp12QueryHeap();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(Heap, Object);
    if (Object == nullptr)
        return E_OUTOFMEMORY;
    Object->Type = Args->Type;
    Object->Count = Args->Count;
    Object->Values.resize((SIZE_T)Args->Count * RxWarpCountTotal);
    Object->Results.resize((SIZE_T)Args->Count * RxWarpCountTotal);
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyQueryHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HQUERYHEAP Heap)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12QueryHeap>(Heap);
}

static
HRESULT
APIENTRY
RxWarp12CreateCommandSignature(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 *Args,
    _In_ D3D12DDI_HCOMMANDSIGNATURE Signature)
{
    RxWarpLock Lock;
    RxWarp12CommandSignature *Object = new (std::nothrow) RxWarp12CommandSignature();

    UNREFERENCED_PARAMETER(Device);
    RxWarpBind(Signature, Object);
    if (Object == nullptr)
        return E_OUTOFMEMORY;
    Object->Stride = Args->ByteStride;
    Object->RootSignature = RxWarpObject<RxWarp12RootSignature>(Args->hRootSignature);
    Object->Arguments.assign(Args->pArgumentDescs, Args->pArgumentDescs + Args->NumArgumentDescs);
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyCommandSignature(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDSIGNATURE Signature)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12CommandSignature>(Signature);
}

VOID
RxWarp12FillPipelineTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table)
{
    Table->pfnCreateVertexShader = RxWarp12CreateShader;
    Table->pfnCreatePixelShader = RxWarp12CreateShader;
    Table->pfnCreateGeometryShader = RxWarp12CreateShader;
    Table->pfnCreateComputeShader = RxWarp12CreateShader;
    Table->pfnCreateHullShader = RxWarp12CreateShader;
    Table->pfnCreateDomainShader = RxWarp12CreateShader;
    Table->pfnCreateGeometryShaderWithStreamOutput = RxWarp12CreateGeometryShaderWithStreamOutput;
    Table->pfnDestroyShader = RxWarp12DestroyShader;
    Table->pfnCreateBlendState = RxWarp12CreateBlendState;
    Table->pfnDestroyBlendState = RxWarp12DestroyBlendState;
    Table->pfnCreateDepthStencilState = RxWarp12CreateDepthStencilState;
    Table->pfnDestroyDepthStencilState = RxWarp12DestroyDepthStencilState;
    Table->pfnCreateRasterizerState = RxWarp12CreateRasterizerState;
    Table->pfnDestroyRasterizerState = RxWarp12DestroyRasterizerState;
    Table->pfnCreateElementLayout = RxWarp12CreateElementLayout;
    Table->pfnDestroyElementLayout = RxWarp12DestroyElementLayout;
    Table->pfnCreateRootSignature = RxWarp12CreateRootSignature;
    Table->pfnDestroyRootSignature = RxWarp12DestroyRootSignature;
    Table->pfnCreatePipelineState = RxWarp12CreatePipelineState;
    Table->pfnDestroyPipelineState = RxWarp12DestroyPipelineState;
    Table->pfnCreateQueryHeap = RxWarp12CreateQueryHeap;
    Table->pfnDestroyQueryHeap = RxWarp12DestroyQueryHeap;
    Table->pfnCreateCommandSignature = RxWarp12CreateCommandSignature;
    Table->pfnDestroyCommandSignature = RxWarp12DestroyCommandSignature;
}
