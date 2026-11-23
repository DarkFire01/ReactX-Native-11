/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline states and the shader and fixed function objects they are built from
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "pipeline.hpp"
#include "core.hpp"
#include "internalshaders.h"

static
constexpr UINT
RxFourCcOf(
    _In_ char A,
    _In_ char B,
    _In_ char C,
    _In_ char D)
{
    return (UINT)(BYTE)A | ((UINT)(BYTE)B << 8) | ((UINT)(BYTE)C << 16) | ((UINT)(BYTE)D << 24);
}

/* Gathering */

VOID
RxGatherGraphicsDesc(
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC& From,
    _Out_ RxPipelineDesc* Desc)
{
    const D3D12_RASTERIZER_DESC& Raster = From.RasterizerState;
    const D3D12_DEPTH_STENCIL_DESC& Depth = From.DepthStencilState;

    ZeroMemory(Desc, sizeof(*Desc));
    Desc->RootSignature = From.pRootSignature;
    Desc->Shaders[RxStageVertex] = From.VS;
    Desc->Shaders[RxStagePixel] = From.PS;
    Desc->Shaders[RxStageDomain] = From.DS;
    Desc->Shaders[RxStageHull] = From.HS;
    Desc->Shaders[RxStageGeometry] = From.GS;
    Desc->StreamOutput = From.StreamOutput;
    Desc->Blend = From.BlendState;
    Desc->SampleMask = From.SampleMask;

    Desc->Rasterizer.FillMode = Raster.FillMode;
    Desc->Rasterizer.CullMode = Raster.CullMode;
    Desc->Rasterizer.FrontCounterClockwise = Raster.FrontCounterClockwise;
    Desc->Rasterizer.DepthBias = (FLOAT)Raster.DepthBias;
    Desc->Rasterizer.DepthBiasClamp = Raster.DepthBiasClamp;
    Desc->Rasterizer.SlopeScaledDepthBias = Raster.SlopeScaledDepthBias;
    Desc->Rasterizer.DepthClipEnable = Raster.DepthClipEnable;
    if (Raster.MultisampleEnable)
        Desc->Rasterizer.LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE;
    else if (Raster.AntialiasedLineEnable)
        Desc->Rasterizer.LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED;
    else
        Desc->Rasterizer.LineRasterizationMode = D3D12_LINE_RASTERIZATION_MODE_ALIASED;
    Desc->Rasterizer.ForcedSampleCount = Raster.ForcedSampleCount;
    Desc->Rasterizer.ConservativeRaster = Raster.ConservativeRaster;

    /* The older form has one pair of stencil masks for both faces */
    Desc->DepthStencil.DepthEnable = Depth.DepthEnable;
    Desc->DepthStencil.DepthWriteMask = Depth.DepthWriteMask;
    Desc->DepthStencil.DepthFunc = Depth.DepthFunc;
    Desc->DepthStencil.StencilEnable = Depth.StencilEnable;
    Desc->DepthStencil.FrontFace.StencilFailOp = Depth.FrontFace.StencilFailOp;
    Desc->DepthStencil.FrontFace.StencilDepthFailOp = Depth.FrontFace.StencilDepthFailOp;
    Desc->DepthStencil.FrontFace.StencilPassOp = Depth.FrontFace.StencilPassOp;
    Desc->DepthStencil.FrontFace.StencilFunc = Depth.FrontFace.StencilFunc;
    Desc->DepthStencil.FrontFace.StencilReadMask = Depth.StencilReadMask;
    Desc->DepthStencil.FrontFace.StencilWriteMask = Depth.StencilWriteMask;
    Desc->DepthStencil.BackFace.StencilFailOp = Depth.BackFace.StencilFailOp;
    Desc->DepthStencil.BackFace.StencilDepthFailOp = Depth.BackFace.StencilDepthFailOp;
    Desc->DepthStencil.BackFace.StencilPassOp = Depth.BackFace.StencilPassOp;
    Desc->DepthStencil.BackFace.StencilFunc = Depth.BackFace.StencilFunc;
    Desc->DepthStencil.BackFace.StencilReadMask = Depth.StencilReadMask;
    Desc->DepthStencil.BackFace.StencilWriteMask = Depth.StencilWriteMask;

    Desc->InputLayout = From.InputLayout;
    Desc->StripCut = From.IBStripCutValue;
    Desc->Topology = From.PrimitiveTopologyType;
    Desc->Targets.NumRenderTargets = From.NumRenderTargets;
    CopyMemory(Desc->Targets.RTFormats, From.RTVFormats, sizeof(Desc->Targets.RTFormats));
    Desc->DepthFormat = From.DSVFormat;
    Desc->Samples = From.SampleDesc;
    Desc->NodeMask = From.NodeMask;
    Desc->Cached = From.CachedPSO;
    Desc->Flags = From.Flags;
}

VOID
RxGatherComputeDesc(
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC& From,
    _Out_ RxPipelineDesc* Desc)
{
    ZeroMemory(Desc, sizeof(*Desc));
    Desc->RootSignature = From.pRootSignature;
    Desc->Shaders[RxStageCompute] = From.CS;
    Desc->NodeMask = From.NodeMask;
    Desc->Cached = From.CachedPSO;
    Desc->Flags = From.Flags;
    Desc->IsCompute = TRUE;
}

/* Shared parts */

HRESULT
RxPipelinePart::Acquire(
    _In_ RxDevice12& Device,
    _In_ Kind PartKind,
    _In_reads_bytes_(KeySize) const BYTE* Key,
    _In_ UINT KeySize,
    _In_ HRESULT (*Build)(RxPipelinePart* Part, const VOID* Context),
    _In_opt_ const VOID* Context,
    _Outptr_ RxPipelinePart** Part)
{
    RxPipelinePart* Created;
    HRESULT hr;

    *Part = nullptr;

    AcquireSRWLockExclusive(&Device.PartCacheLock);
    for (RxPipelinePart* Each = Device.PartCache; Each != nullptr; Each = Each->NextCached)
    {
        if (Each->Matches(PartKind, Key, KeySize))
        {
            Each->AddUse();
            *Part = Each;
            break;
        }
    }
    ReleaseSRWLockExclusive(&Device.PartCacheLock);
    if (*Part != nullptr)
        return S_OK;

    Created = new (std::nothrow) RxPipelinePart(Device, PartKind);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    Created->Key = new (std::nothrow) BYTE[KeySize ? KeySize : 1];
    if (Created->Key == nullptr)
    {
        delete Created;
        return E_OUTOFMEMORY;
    }
    CopyMemory(Created->Key, Key, KeySize);
    Created->KeySize = KeySize;

    hr = Build(Created, Context);
    if (FAILED(hr))
    {
        delete Created;
        return hr;
    }

    AcquireSRWLockExclusive(&Device.PartCacheLock);
    Created->NextCached = Device.PartCache;
    Device.PartCache = Created;
    Created->IsCached = TRUE;
    ReleaseSRWLockExclusive(&Device.PartCacheLock);

    *Part = Created;
    return S_OK;
}

VOID
RxPipelinePart::ReleaseUse()
{
    RxDevice12& Owner = Device;

    AcquireSRWLockExclusive(&Owner.PartCacheLock);
    Uses -= 1;
    if (Uses != 0)
    {
        ReleaseSRWLockExclusive(&Owner.PartCacheLock);
        return;
    }

    for (RxPipelinePart** Link = &Owner.PartCache; *Link != nullptr; Link = &(*Link)->NextCached)
    {
        if (*Link == this)
        {
            *Link = NextCached;
            break;
        }
    }
    ReleaseSRWLockExclusive(&Owner.PartCacheLock);
    delete this;
}

RxPipelinePart::~RxPipelinePart()
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();

    if (IsCreated)
    {
        switch (PartKind)
        {
            case Kind::Shader:
            {
                D3D12DDI_HSHADER Shader = { Handle };

                Ddi.pfnDestroyShader(Device.Handle(), Shader);
                break;
            }
            case Kind::Blend:
            {
                D3D12DDI_HBLENDSTATE Blend = { Handle };

                Ddi.pfnDestroyBlendState(Device.Handle(), Blend);
                break;
            }
            case Kind::DepthStencil:
            {
                D3D12DDI_HDEPTHSTENCILSTATE Depth = { Handle };

                Ddi.pfnDestroyDepthStencilState(Device.Handle(), Depth);
                break;
            }
            case Kind::Rasterizer:
            {
                D3D12DDI_HRASTERIZERSTATE Raster = { Handle };

                Ddi.pfnDestroyRasterizerState(Device.Handle(), Raster);
                break;
            }
        }
    }

    delete[] Key;
    delete[] Code;
    delete[] Entries;
    if (Converted != nullptr)
        CoTaskMemFree(Converted);
}

/* Shaders */

/* The 24 byte element of ISGN, OSGN and PCSG; OSG5 puts the stream in front; the 1 forms add precision */
enum class RxSignatureForm
{
    Plain,
    Streams,
    Precise,
};

static
D3D10_SB_NAME
RxSystemValueName(
    _In_ UINT Name)
{
    /* The token names stop below the pixel shader outputs, which have none */
    return Name < 64 ? (D3D10_SB_NAME)Name : D3D10_SB_NAME_UNDEFINED;
}

static
UINT
RxReadWord(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ UINT Size,
    _In_ UINT Offset)
{
    UINT Value = 0;

    if (Offset <= Size && Size - Offset >= sizeof(Value))
        CopyMemory(&Value, Data + Offset, sizeof(Value));
    return Value;
}

static
UINT
RxSignatureCount(
    _In_reads_bytes_opt_(Size) const BYTE* Part,
    _In_ UINT Size)
{
    return Part != nullptr ? RxReadWord(Part, Size, 0) : 0;
}

static
HRESULT
RxReadSignature(
    _In_reads_bytes_opt_(Size) const BYTE* Part,
    _In_ UINT Size,
    _In_ RxSignatureForm Form,
    _Out_writes_(RxSignatureCount(Part, Size)) D3D12DDIARG_SIGNATURE_ENTRY_0012* Entries)
{
    const UINT Stride = Form == RxSignatureForm::Plain ? 24 : (Form == RxSignatureForm::Streams ? 28 : 32);
    const UINT Lead = Form == RxSignatureForm::Plain ? 0 : 4;
    UINT Count = RxSignatureCount(Part, Size);
    UINT First = RxReadWord(Part, Size, 4);

    if (Count == 0)
        return S_OK;
    if (First > Size || (Size - First) / Stride < Count)
        return E_INVALIDARG;

    for (UINT Index = 0; Index < Count; Index++)
    {
        UINT Element = First + Index * Stride;
        UINT Masks = RxReadWord(Part, Size, Element + Lead + 20);

        Entries[Index].SystemValue = RxSystemValueName(RxReadWord(Part, Size, Element + Lead + 8));
        Entries[Index].Register = RxReadWord(Part, Size, Element + Lead + 16);
        Entries[Index].Mask = (BYTE)(Masks & 0xF);
        Entries[Index].Stream = Form == RxSignatureForm::Plain ? 0 : (BYTE)RxReadWord(Part, Size, Element);
        Entries[Index].RegisterComponentType =
            (D3D10_SB_REGISTER_COMPONENT_TYPE)RxReadWord(Part, Size, Element + Lead + 12);
        Entries[Index].MinPrecision = Form == RxSignatureForm::Precise
                                          ? (D3D11_SB_OPERAND_MIN_PRECISION)RxReadWord(Part, Size, Element + 28)
                                          : D3D11_SB_OPERAND_MIN_PRECISION_DEFAULT;
    }
    return S_OK;
}

/* The first of the named parts a container holds, with the form its elements take */
static
VOID
RxFindSignature(
    _In_ const D3D12_SHADER_BYTECODE& Code,
    _In_ BOOLEAN Output,
    _Out_ const BYTE** Part,
    _Out_ UINT* Size,
    _Out_ RxSignatureForm* Form)
{
    const BYTE* Data = static_cast<const BYTE*>(Code.pShaderBytecode);

    *Form = RxSignatureForm::Precise;
    if (RxFindContainerPart(Data, Code.BytecodeLength, Output ? RxFourCcOf('O', 'S', 'G', '1') : RxFourCcOf('I', 'S', 'G', '1'),
                            Part, Size))
    {
        return;
    }

    *Form = RxSignatureForm::Streams;
    if (Output && RxFindContainerPart(Data, Code.BytecodeLength, RxFourCcOf('O', 'S', 'G', '5'), Part, Size))
        return;

    *Form = RxSignatureForm::Plain;
    if (!RxFindContainerPart(Data, Code.BytecodeLength, Output ? RxFourCcOf('O', 'S', 'G', 'N') : RxFourCcOf('I', 'S', 'G', 'N'),
                             Part, Size))
    {
        *Part = nullptr;
        *Size = 0;
    }
}

/* dxilconv's converter interface, as the DirectXShaderCompiler headers publish it */
struct __declspec(uuid("5F956ED5-78D1-4B15-8247-F7187614A041")) __declspec(novtable) RxDxbcConverter : public IUnknown
{
    virtual
    HRESULT
    STDMETHODCALLTYPE
    Convert(
        _In_reads_bytes_(DxbcSize) LPCVOID Dxbc,
        _In_ UINT32 DxbcSize,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Outptr_result_bytebuffer_maybenull_(*DxilSize) LPVOID* Dxil,
        _Out_ UINT32* DxilSize,
        _Outptr_result_maybenull_z_ LPWSTR* Diagnostics) = 0;
};

static const CLSID RxDxbcConverterClsid = {0x4900391e, 0xb752, 0x4edd, {0xa8, 0x85, 0x6f, 0xb7, 0x6e, 0x25, 0xad, 0xdb}};

typedef HRESULT (WINAPI* RxDxcCreateInstanceProc)(REFCLSID Clsid, REFIID Riid, LPVOID* Object);

/* DXBC for a driver that takes DXIL, converted by dxilconv.dll the way the system core loads it */
static
HRESULT
RxConvertToDxil(
    _In_reads_bytes_(Size) const BYTE* Container,
    _In_ UINT Size,
    _Outptr_result_bytebuffer_(*DxilSize) BYTE** Dxil,
    _Out_ UINT* DxilSize)
{
    static RxDxcCreateInstanceProc CreateInstance;
    RxDxbcConverter* Converter = nullptr;
    LPVOID Output = nullptr;
    UINT32 OutputSize = 0;
    LPWSTR Diagnostics = nullptr;
    HRESULT hr;

    *Dxil = nullptr;
    *DxilSize = 0;

    if (CreateInstance == nullptr)
    {
        HMODULE Module = LoadLibraryW(L"dxilconv.dll");

        if (Module == NULL)
        {
            RX_TRACE(L"dxilconv.dll is not available");
            return E_FAIL;
        }
        CreateInstance = reinterpret_cast<RxDxcCreateInstanceProc>(GetProcAddress(Module, "DxcCreateInstance"));
        if (CreateInstance == nullptr)
            return E_FAIL;
    }

    hr = CreateInstance(RxDxbcConverterClsid, __uuidof(RxDxbcConverter), reinterpret_cast<LPVOID*>(&Converter));
    if (FAILED(hr))
        return hr;

    hr = Converter->Convert(Container, Size, nullptr, &Output, &OutputSize, &Diagnostics);
    Converter->Release();
    if (Diagnostics != nullptr)
        CoTaskMemFree(Diagnostics);
    if (FAILED(hr))
    {
        RX_TRACE(L"dxilconv failed 0x%08lX", hr);
        return hr;
    }

    *Dxil = static_cast<BYTE*>(Output);
    *DxilSize = OutputSize;
    return S_OK;
}

struct RxShaderBuild
{
    RxShaderStage Stage;
    const D3D12_SHADER_BYTECODE* Code;
    D3D12DDI_HROOTSIGNATURE Root;
};

static
HRESULT
RxBuildShader(
    _In_ RxPipelinePart* Part,
    _In_ const VOID* Context)
{
    const RxShaderBuild& Build = *static_cast<const RxShaderBuild*>(Context);
    RxDevice12& Device = Part->GetDevice();
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_SHADER_0026 Args = {};
    D3D12DDIARG_STAGE_IO_SIGNATURES Io = {};
    D3D12DDI_HSHADER Shader;
    const BYTE* Program;
    const BYTE* Inputs;
    const BYTE* Outputs;
    UINT ProgramSize;
    UINT InputSize;
    UINT OutputSize;
    RxSignatureForm InputForm;
    RxSignatureForm OutputForm;
    UINT InputCount;
    UINT OutputCount;
    SIZE_T DriverSize;
    HRESULT hr;

    /* The driver reads the code after creation returns, so the part keeps its own copy */
    Part->Code = new (std::nothrow) BYTE[Build.Code->BytecodeLength];
    if (Part->Code == nullptr)
        return E_OUTOFMEMORY;
    CopyMemory(Part->Code, Build.Code->pShaderBytecode, Build.Code->BytecodeLength);

    {
        D3D12_SHADER_BYTECODE Own = { Part->Code, Build.Code->BytecodeLength };

        if (!RxFindContainerPart(Part->Code, Own.BytecodeLength, RxFourCcOf('D', 'X', 'I', 'L'), &Program, &ProgramSize) &&
            !RxFindContainerPart(Part->Code, Own.BytecodeLength, RxFourCcOf('S', 'H', 'E', 'X'), &Program, &ProgramSize) &&
            !RxFindContainerPart(Part->Code, Own.BytecodeLength, RxFourCcOf('S', 'H', 'D', 'R'), &Program, &ProgramSize))
        {
            return E_INVALIDARG;
        }

        RxFindSignature(Own, FALSE, &Inputs, &InputSize, &InputForm);
        RxFindSignature(Own, TRUE, &Outputs, &OutputSize, &OutputForm);
    }

    /* Drivers of shader model 6 and up take DXIL only; the signatures and hash stay the original's */
    if (Device.GetHighestShaderModel() >= D3D_SHADER_MODEL_6_0 &&
        !RxFindContainerPart(Part->Code, (UINT)Build.Code->BytecodeLength, RxFourCcOf('D', 'X', 'I', 'L'), &Program,
                             &ProgramSize))
    {
        hr = RxConvertToDxil(Part->Code, (UINT)Build.Code->BytecodeLength, &Part->Converted, &Part->ConvertedSize);
        if (FAILED(hr))
            return hr;
        if (!RxFindContainerPart(Part->Converted, Part->ConvertedSize, RxFourCcOf('D', 'X', 'I', 'L'), &Program,
                                 &ProgramSize))
        {
            return E_FAIL;
        }
    }

    InputCount = RxSignatureCount(Inputs, InputSize);
    OutputCount = RxSignatureCount(Outputs, OutputSize);
    Part->Entries = new (std::nothrow) D3D12DDIARG_SIGNATURE_ENTRY_0012[InputCount + OutputCount + 1]();
    if (Part->Entries == nullptr)
        return E_OUTOFMEMORY;

    hr = RxReadSignature(Inputs, InputSize, InputForm, Part->Entries);
    if (SUCCEEDED(hr))
        hr = RxReadSignature(Outputs, OutputSize, OutputForm, Part->Entries + InputCount);
    if (FAILED(hr))
        return hr;

    Io.pInputSignature = Part->Entries;
    Io.NumInputSignatureEntries = InputCount;
    Io.pOutputSignature = Part->Entries + InputCount;
    Io.NumOutputSignatureEntries = OutputCount;

    Args.hRootSignature = Build.Root;
    Args.pShaderCode = reinterpret_cast<const UINT*>(Program);
    Args.IOSignatures.Standard = &Io;
    CopyMemory(&Args.ShaderCodeHash, Part->Code + 4, sizeof(Args.ShaderCodeHash));

    if (Build.Stage == RxStageHull || Build.Stage == RxStageDomain)
        DriverSize = Ddi.pfnCalcPrivateTessellationShaderSize(Device.Handle(), &Args);
    else
        DriverSize = Ddi.pfnCalcPrivateShaderSize(Device.Handle(), &Args);

    Shader.pDrvPrivate = Part->AllocateDriverMemory(DriverSize);
    if (Shader.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    switch (Build.Stage)
    {
        case RxStageVertex:
            Ddi.pfnCreateVertexShader(Device.Handle(), &Args, Shader);
            break;
        case RxStagePixel:
            Ddi.pfnCreatePixelShader(Device.Handle(), &Args, Shader);
            break;
        case RxStageGeometry:
            Ddi.pfnCreateGeometryShader(Device.Handle(), &Args, Shader);
            break;
        case RxStageHull:
            Ddi.pfnCreateHullShader(Device.Handle(), &Args, Shader);
            break;
        case RxStageDomain:
            Ddi.pfnCreateDomainShader(Device.Handle(), &Args, Shader);
            break;
        case RxStageCompute:
            Ddi.pfnCreateComputeShader(Device.Handle(), &Args, Shader);
            break;
        default:
            return E_NOTIMPL;
    }

    hr = RxDevice12::TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"driver shader of stage %u failed 0x%08lX", Build.Stage, hr);
        return hr;
    }

    Part->MarkCreated(Build.Stage);
    return S_OK;
}

/* Fixed function states, keyed by the driver form so equal states share a driver object */

static
VOID
RxToDdiBlend(
    _In_ const D3D12_BLEND_DESC& Desc,
    _Out_ D3D12DDI_BLEND_DESC_0010* Ddi)
{
    ZeroMemory(Ddi, sizeof(*Ddi));
    Ddi->AlphaToCoverageEnable = Desc.AlphaToCoverageEnable != FALSE;
    Ddi->IndependentBlendEnable = Desc.IndependentBlendEnable != FALSE;

    for (UINT Index = 0; Index < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; Index++)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC& Source = Desc.RenderTarget[Desc.IndependentBlendEnable ? Index : 0];
        D3D12DDI_RENDER_TARGET_BLEND_DESC& Target = Ddi->RenderTarget[Index];

        Target.BlendEnable = Source.BlendEnable != FALSE;
        Target.LogicOpEnable = Source.LogicOpEnable != FALSE;
        Target.RenderTargetWriteMask = Source.RenderTargetWriteMask & 0xF;

        /* Factors only count while blending without a logic op; the op only with one */
        if (Source.BlendEnable && !Source.LogicOpEnable)
        {
            Target.SrcBlend = (D3D12DDI_BLEND)Source.SrcBlend;
            Target.DestBlend = (D3D12DDI_BLEND)Source.DestBlend;
            Target.BlendOp = (D3D12DDI_BLEND_OP)Source.BlendOp;
            Target.SrcBlendAlpha = (D3D12DDI_BLEND)Source.SrcBlendAlpha;
            Target.DestBlendAlpha = (D3D12DDI_BLEND)Source.DestBlendAlpha;
            Target.BlendOpAlpha = (D3D12DDI_BLEND_OP)Source.BlendOpAlpha;
        }
        else
        {
            Target.SrcBlend = D3D12DDI_BLEND_ONE;
            Target.DestBlend = D3D12DDI_BLEND_ZERO;
            Target.BlendOp = D3D12DDI_BLEND_OP_ADD;
            Target.SrcBlendAlpha = D3D12DDI_BLEND_ONE;
            Target.DestBlendAlpha = D3D12DDI_BLEND_ZERO;
            Target.BlendOpAlpha = D3D12DDI_BLEND_OP_ADD;
        }
        Target.LogicOp = Source.LogicOpEnable ? (D3D12DDI_LOGIC_OP)(Source.LogicOp & 0xF) : D3D12DDI_LOGIC_OP_NOOP;
    }
}

/* A face only counts as stencil tested when it does something besides keep on an always pass */
static
BOOLEAN
RxFaceTests(
    _In_ const D3D12_DEPTH_STENCILOP_DESC1& Face,
    _In_ BOOLEAN DepthEnable)
{
    D3D12_STENCIL_OP DepthFail = DepthEnable ? Face.StencilDepthFailOp : D3D12_STENCIL_OP_KEEP;

    return Face.StencilFunc != D3D12_COMPARISON_FUNC_ALWAYS || Face.StencilPassOp != D3D12_STENCIL_OP_KEEP ||
           DepthFail != D3D12_STENCIL_OP_KEEP;
}

static
VOID
RxToDdiFace(
    _In_ const D3D12_DEPTH_STENCILOP_DESC1& Face,
    _In_ BOOLEAN Tested,
    _In_ BOOLEAN DepthEnable,
    _Out_ D3D12DDI_DEPTH_STENCILOP_DESC* Ddi,
    _Out_ UINT8* ReadMask,
    _Out_ UINT8* WriteMask)
{
    if (!Tested)
    {
        Ddi->StencilFailOp = D3D12DDI_STENCIL_OP_KEEP;
        Ddi->StencilDepthFailOp = D3D12DDI_STENCIL_OP_KEEP;
        Ddi->StencilPassOp = D3D12DDI_STENCIL_OP_KEEP;
        Ddi->StencilFunc = D3D12DDI_COMPARISON_FUNC_ALWAYS;
        *ReadMask = 0;
        *WriteMask = 0;
        return;
    }

    Ddi->StencilFailOp = (D3D12DDI_STENCIL_OP)Face.StencilFailOp;
    Ddi->StencilDepthFailOp = DepthEnable ? (D3D12DDI_STENCIL_OP)Face.StencilDepthFailOp : D3D12DDI_STENCIL_OP_KEEP;
    Ddi->StencilPassOp = (D3D12DDI_STENCIL_OP)Face.StencilPassOp;
    Ddi->StencilFunc = (D3D12DDI_COMPARISON_FUNC)Face.StencilFunc;
    *ReadMask = Face.StencilReadMask;
    *WriteMask = Face.StencilWriteMask;
}

static
VOID
RxToDdiDepthStencil(
    _In_ const D3D12_DEPTH_STENCIL_DESC2& Desc,
    _Out_ D3D12DDI_DEPTH_STENCIL_DESC_0095* Ddi)
{
    BOOLEAN DepthEnable = Desc.DepthEnable != FALSE;
    BOOLEAN StencilEnable = Desc.StencilEnable != FALSE;

    ZeroMemory(Ddi, sizeof(*Ddi));
    Ddi->DepthEnable = DepthEnable;

    /* Without a depth test the write mask and function settle on their defaults */
    Ddi->DepthWriteMask = DepthEnable ? (D3D12DDI_DEPTH_WRITE_MASK)(Desc.DepthWriteMask != D3D12_DEPTH_WRITE_MASK_ZERO)
                                      : D3D12DDI_DEPTH_WRITE_MASK_ALL;
    Ddi->DepthFunc = DepthEnable ? (D3D12DDI_COMPARISON_FUNC)Desc.DepthFunc : D3D12DDI_COMPARISON_FUNC_LESS;
    Ddi->StencilEnable = StencilEnable;
    Ddi->FrontEnable = StencilEnable && RxFaceTests(Desc.FrontFace, DepthEnable);
    Ddi->BackEnable = StencilEnable && RxFaceTests(Desc.BackFace, DepthEnable);
    RxToDdiFace(Desc.FrontFace, Ddi->FrontEnable, DepthEnable, &Ddi->FrontFace, &Ddi->FrontFaceStencilReadMask,
                &Ddi->FrontFaceStencilWriteMask);
    RxToDdiFace(Desc.BackFace, Ddi->BackEnable, DepthEnable, &Ddi->BackFace, &Ddi->BackFaceStencilReadMask,
                &Ddi->BackFaceStencilWriteMask);
    Ddi->DepthBoundsTestEnable = Desc.DepthBoundsTestEnable != FALSE;
}

static
VOID
RxToDdiRasterizer(
    _In_ const D3D12_RASTERIZER_DESC2& Desc,
    _Out_ D3D12DDI_RASTERIZER_DESC_0102* Ddi)
{
    ZeroMemory(Ddi, sizeof(*Ddi));
    Ddi->FillMode = (Desc.FillMode & 3) == D3D12_FILL_MODE_WIREFRAME ? D3D12DDI_FILL_MODE_WIREFRAME : D3D12DDI_FILL_MODE_SOLID;
    switch (Desc.CullMode & 3)
    {
        case D3D12_CULL_MODE_NONE:
            Ddi->CullMode = D3D12DDI_CULL_MODE_NONE;
            break;
        case D3D12_CULL_MODE_FRONT:
            Ddi->CullMode = D3D12DDI_CULL_MODE_FRONT;
            break;
        default:
            Ddi->CullMode = D3D12DDI_CULL_MODE_BACK;
            break;
    }
    Ddi->FrontCounterClockwise = Desc.FrontCounterClockwise != FALSE;
    Ddi->DepthBias = Desc.DepthBias;
    Ddi->DepthBiasClamp = Desc.DepthBiasClamp;
    Ddi->SlopeScaledDepthBias = Desc.SlopeScaledDepthBias;
    Ddi->DepthClipEnable = Desc.DepthClipEnable != FALSE;

    /* Direct3D 12 always scissors */
    Ddi->ScissorEnable = TRUE;
    Ddi->LineRasterizationMode = (D3D12DDI_LINE_RASTERIZATION_MODE)(Desc.LineRasterizationMode & 3);
    Ddi->ForcedSampleCount = Desc.ForcedSampleCount & 0x7F;
    Ddi->ConservativeRasterizationMode = (D3D12DDI_CONSERVATIVE_RASTERIZATION_MODE)(Desc.ConservativeRaster & 1);
}

static
HRESULT
RxBuildBlend(
    _In_ RxPipelinePart* Part,
    _In_ const VOID* Context)
{
    const D3D12DDI_BLEND_DESC_0010* Desc = static_cast<const D3D12DDI_BLEND_DESC_0010*>(Context);
    RxDevice12& Device = Part->GetDevice();
    D3D12DDI_HBLENDSTATE Blend;

    Blend.pDrvPrivate = Part->AllocateDriverMemory(Device.Ddi().pfnCalcPrivateBlendStateSize(Device.Handle(), Desc));
    if (Blend.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;
    Device.Ddi().pfnCreateBlendState(Device.Handle(), Desc, Blend);
    Part->MarkCreated(RxStageVertex);
    return RxDevice12::TakeDriverError();
}

static
HRESULT
RxBuildDepthStencil(
    _In_ RxPipelinePart* Part,
    _In_ const VOID* Context)
{
    const D3D12DDI_DEPTH_STENCIL_DESC_0095* Desc = static_cast<const D3D12DDI_DEPTH_STENCIL_DESC_0095*>(Context);
    RxDevice12& Device = Part->GetDevice();
    D3D12DDI_HDEPTHSTENCILSTATE Depth;

    Depth.pDrvPrivate =
        Part->AllocateDriverMemory(Device.Ddi().pfnCalcPrivateDepthStencilStateSize(Device.Handle(), Desc));
    if (Depth.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;
    Device.Ddi().pfnCreateDepthStencilState(Device.Handle(), Desc, Depth);
    Part->MarkCreated(RxStageVertex);
    return RxDevice12::TakeDriverError();
}

static
HRESULT
RxBuildRasterizer(
    _In_ RxPipelinePart* Part,
    _In_ const VOID* Context)
{
    const D3D12DDI_RASTERIZER_DESC_0102* Desc = static_cast<const D3D12DDI_RASTERIZER_DESC_0102*>(Context);
    RxDevice12& Device = Part->GetDevice();
    D3D12DDI_HRASTERIZERSTATE Raster;

    Raster.pDrvPrivate =
        Part->AllocateDriverMemory(Device.Ddi().pfnCalcPrivateRasterizerStateSize(Device.Handle(), Desc));
    if (Raster.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;
    Device.Ddi().pfnCreateRasterizerState(Device.Handle(), Desc, Raster);
    Part->MarkCreated(RxStageVertex);
    return RxDevice12::TakeDriverError();
}

/* State checks, run as each state is made, after the pipeline's shaders */

/* How a blend factor may be used: by color only, as dual source, or behind the alpha factor cap */
enum class RxFactorUse
{
    Invalid,
    Plain,
    ColorOnly,
    DualSource,
    AlphaFactor,
};

static
RxFactorUse
RxClassifyFactor(
    _In_ D3D12_BLEND Factor)
{
    switch (Factor)
    {
        case D3D12_BLEND_ZERO:
        case D3D12_BLEND_ONE:
        case D3D12_BLEND_SRC_ALPHA:
        case D3D12_BLEND_INV_SRC_ALPHA:
        case D3D12_BLEND_DEST_ALPHA:
        case D3D12_BLEND_INV_DEST_ALPHA:
        case D3D12_BLEND_SRC_ALPHA_SAT:
        case D3D12_BLEND_BLEND_FACTOR:
        case D3D12_BLEND_INV_BLEND_FACTOR:
            return RxFactorUse::Plain;
        case D3D12_BLEND_SRC_COLOR:
        case D3D12_BLEND_INV_SRC_COLOR:
        case D3D12_BLEND_DEST_COLOR:
        case D3D12_BLEND_INV_DEST_COLOR:
        case D3D12_BLEND_SRC1_COLOR:
        case D3D12_BLEND_INV_SRC1_COLOR:
            return RxFactorUse::ColorOnly;
        case D3D12_BLEND_SRC1_ALPHA:
        case D3D12_BLEND_INV_SRC1_ALPHA:
            return RxFactorUse::DualSource;
        case D3D12_BLEND_ALPHA_FACTOR:
        case D3D12_BLEND_INV_ALPHA_FACTOR:
            return RxFactorUse::AlphaFactor;
        default:
            return RxFactorUse::Invalid;
    }
}

static
BOOLEAN
RxIsSecondSourceColor(
    _In_ D3D12_BLEND Factor)
{
    return Factor == D3D12_BLEND_SRC1_COLOR || Factor == D3D12_BLEND_INV_SRC1_COLOR;
}

static
BOOLEAN
RxIsBlendOp(
    _In_ D3D12_BLEND_OP Op)
{
    return Op >= D3D12_BLEND_OP_ADD && Op <= D3D12_BLEND_OP_MAX;
}

static
BOOLEAN
RxIsMinMax(
    _In_ D3D12_BLEND_OP Op)
{
    return Op == D3D12_BLEND_OP_MIN || Op == D3D12_BLEND_OP_MAX;
}

static
HRESULT
RxCheckBlend(
    _In_ RxDevice12& Device,
    _In_ const D3D12_BLEND_DESC& Desc)
{
    const BOOLEAN SameBlendOnly = Device.GetFeatureLevel() <= D3D_FEATURE_LEVEL_10_0;
    const BOOLEAN LogicOps = Device.GetOptions().OutputMergerLogicOp != FALSE;
    const BOOLEAN AlphaFactors = Device.GetDdiBuild() >= 52;
    const D3D12_RENDER_TARGET_BLEND_DESC* FirstBlended = nullptr;
    BOOLEAN ColorDual = FALSE;
    BOOLEAN AlphaDual = FALSE;
    UINT Errors = 0;
    UINT Targets = Desc.IndependentBlendEnable ? D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT : 1;

    for (UINT Index = 0; Index < Targets; Index++)
    {
        const D3D12_RENDER_TARGET_BLEND_DESC& Target = Desc.RenderTarget[Index];

        if (Target.BlendEnable)
        {
            BOOLEAN ColorDualHere = FALSE;
            BOOLEAN AlphaDualHere = FALSE;

            if (FirstBlended == nullptr)
                FirstBlended = &Target;

            for (D3D12_BLEND Factor : { Target.SrcBlend, Target.DestBlend })
            {
                switch (RxClassifyFactor(Factor))
                {
                    case RxFactorUse::Plain:
                        break;
                    case RxFactorUse::ColorOnly:
                    case RxFactorUse::DualSource:
                        if (RxClassifyFactor(Factor) == RxFactorUse::DualSource || RxIsSecondSourceColor(Factor))
                        {
                            Errors += Index != 0;
                            ColorDualHere = TRUE;
                            ColorDual = TRUE;
                        }
                        break;
                    case RxFactorUse::AlphaFactor:
                        Errors += !AlphaFactors;
                        break;
                    default:
                        Errors++;
                        break;
                }
            }
            if (!RxIsBlendOp(Target.BlendOp))
                Errors++;
            else if (RxIsMinMax(Target.BlendOp) && ColorDualHere)
                Errors++;

            for (UINT Which = 0; Which < 2; Which++)
            {
                D3D12_BLEND Factor = Which == 0 ? Target.SrcBlendAlpha : Target.DestBlendAlpha;

                switch (RxClassifyFactor(Factor))
                {
                    case RxFactorUse::Plain:
                        break;
                    case RxFactorUse::DualSource:
                        Errors += Index != 0;
                        AlphaDualHere = TRUE;
                        if (Which == 0)
                            AlphaDual = TRUE;
                        break;
                    case RxFactorUse::AlphaFactor:
                        Errors += !AlphaFactors;
                        break;
                    default:
                        Errors++;
                        break;
                }
            }
            if (!RxIsBlendOp(Target.BlendOpAlpha))
                Errors++;
            else if (RxIsMinMax(Target.BlendOpAlpha) && AlphaDualHere)
                Errors++;

            /* Level 10.0 hardware blends every target alike */
            if (SameBlendOnly &&
                (Target.SrcBlend != FirstBlended->SrcBlend || Target.DestBlend != FirstBlended->DestBlend ||
                 Target.BlendOp != FirstBlended->BlendOp || Target.SrcBlendAlpha != FirstBlended->SrcBlendAlpha ||
                 Target.DestBlendAlpha != FirstBlended->DestBlendAlpha ||
                 Target.BlendOpAlpha != FirstBlended->BlendOpAlpha))
            {
                Errors++;
            }

            if (Index != 0 && (ColorDual || AlphaDual))
                Errors++;
        }

        if ((Target.RenderTargetWriteMask & 0xF0) != 0)
            Errors++;

        if (Target.LogicOpEnable)
        {
            Errors += !LogicOps;
            Errors += Desc.IndependentBlendEnable != FALSE;
            Errors += Target.BlendEnable != FALSE;
            Errors += (UINT)Target.LogicOp > D3D12_LOGIC_OP_OR_INVERTED;
        }
    }

    return Errors != 0 ? E_INVALIDARG : S_OK;
}

static
BOOLEAN
RxIsStencilOp(
    _In_ UINT Op)
{
    return Op >= D3D12_STENCIL_OP_KEEP && Op <= D3D12_STENCIL_OP_DECR;
}

static
BOOLEAN
RxIsComparison(
    _In_ UINT Function)
{
    return Function >= D3D12_COMPARISON_FUNC_NEVER && Function <= D3D12_COMPARISON_FUNC_ALWAYS;
}

static
HRESULT
RxCheckDepthStencil(
    _In_ RxDevice12& Device,
    _In_ const D3D12_DEPTH_STENCIL_DESC2& Desc)
{
    UINT Errors = 0;

    Errors += (UINT)Desc.DepthWriteMask > D3D12_DEPTH_WRITE_MASK_ALL;
    if (Desc.DepthEnable)
        Errors += !RxIsComparison(Desc.DepthFunc);

    if (Desc.StencilEnable)
    {
        for (const D3D12_DEPTH_STENCILOP_DESC1* Face : { &Desc.FrontFace, &Desc.BackFace })
        {
            Errors += !RxIsStencilOp(Face->StencilFailOp);
            Errors += !RxIsStencilOp(Face->StencilDepthFailOp);
            Errors += !RxIsStencilOp(Face->StencilPassOp);
            Errors += !RxIsComparison(Face->StencilFunc);
        }
    }

    if (Desc.DepthBoundsTestEnable && !Device.GetOptions().DepthBoundsTestSupported)
        Errors++;

    /* Drivers before build 55 take one pair of masks for both faces */
    if (Device.GetDdiBuild() < 55 && (Desc.FrontFace.StencilReadMask != Desc.BackFace.StencilReadMask ||
                                      Desc.FrontFace.StencilWriteMask != Desc.BackFace.StencilWriteMask))
    {
        Errors++;
    }

    return Errors != 0 ? E_INVALIDARG : S_OK;
}

static
HRESULT
RxCheckRasterizer(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RASTERIZER_DESC2& Desc)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    BOOLEAN Quadrilateral = FALSE;
    UINT Errors = 0;

    Errors += Desc.FillMode != D3D12_FILL_MODE_WIREFRAME && Desc.FillMode != D3D12_FILL_MODE_SOLID;
    Errors += (UINT)Desc.CullMode < D3D12_CULL_MODE_NONE || (UINT)Desc.CullMode > D3D12_CULL_MODE_BACK;

    switch (Desc.LineRasterizationMode)
    {
        case D3D12_LINE_RASTERIZATION_MODE_ALIASED:
        case D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED:
            break;
        case D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE:
        case D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_NARROW:
            Quadrilateral = TRUE;
            break;
        default:
            Errors++;
            break;
    }

    switch (Desc.ForcedSampleCount)
    {
        case 0:
            break;
        case 1:
            Errors += Level < D3D_FEATURE_LEVEL_11_1 && Quadrilateral;
            break;
        case 4:
        case 8:
            Errors += Level < D3D_FEATURE_LEVEL_11_0;
            break;
        case 16:
            Errors += Level < D3D_FEATURE_LEVEL_11_1;
            break;
        default:
            Errors++;
            break;
    }

    if (Desc.ConservativeRaster == D3D12_CONSERVATIVE_RASTERIZATION_MODE_ON)
    {
        Errors += Device.GetOptions().ConservativeRasterizationTier == 0;
        Errors += Desc.FillMode != D3D12_FILL_MODE_SOLID;
    }
    else if (Desc.ConservativeRaster != D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF)
    {
        Errors++;
    }

    return Errors != 0 ? E_INVALIDARG : S_OK;
}

/* Pipeline states */

HRESULT
RxPipelineState::Create(
    _In_ RxDevice12& Device,
    _In_ const RxPipelineDesc& Desc,
    _In_ BOOLEAN Internal,
    _Outptr_ RxPipelineState** State)
{
    RxPipelineState* Created;
    HRESULT hr;

    *State = nullptr;

    Created = new (std::nothrow) RxPipelineState(Device, Internal);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *State = Created;
    return S_OK;
}

HRESULT
RxPipelineState::AcquireShader(
    _In_ const RxPipelineDesc& Desc,
    _In_ RxShaderStage Stage)
{
    const D3D12_SHADER_BYTECODE& Code = Desc.Shaders[Stage];
    RxShaderBuild Build;
    BYTE* Key;
    UINT KeySize;
    HRESULT hr;

    if (Code.pShaderBytecode == nullptr || Code.BytecodeLength == 0)
        return S_OK;
    if (Code.BytecodeLength > UINT_MAX - 2 * sizeof(VOID*))
        return E_INVALIDARG;

    /* Equal code under the same root signature for the same stage is one driver shader */
    KeySize = (UINT)(sizeof(UINT) + sizeof(VOID*) + Code.BytecodeLength);
    Key = new (std::nothrow) BYTE[KeySize];
    if (Key == nullptr)
        return E_OUTOFMEMORY;
    {
        UINT StageValue = (UINT)Stage;
        VOID* RootHandle = Root->GetDriverHandle().pDrvPrivate;

        CopyMemory(Key, &StageValue, sizeof(StageValue));
        CopyMemory(Key + sizeof(StageValue), &RootHandle, sizeof(RootHandle));
        CopyMemory(Key + sizeof(StageValue) + sizeof(RootHandle), Code.pShaderBytecode, Code.BytecodeLength);
    }

    Build.Stage = Stage;
    Build.Code = &Code;
    Build.Root = Root->GetDriverHandle();
    hr = RxPipelinePart::Acquire(Device, RxPipelinePart::Kind::Shader, Key, KeySize, RxBuildShader, &Build,
                                 &Shaders[Stage]);
    delete[] Key;
    return hr;
}

HRESULT
RxPipelineState::Initialize(
    _In_ const RxPipelineDesc& Desc)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_PIPELINE_STATE_0099 Args = {};
    D3D12DDI_HRTPIPELINESTATE Runtime = { this };
    HRESULT hr;

    IsCompute = Desc.IsCompute;
    Root = RxUnwrap12<RxRootSignature>(Desc.RootSignature);
    if (Root == nullptr)
        return E_INVALIDARG;
    Root->AddUse();

    if (IsCompute)
    {
        hr = AcquireShader(Desc, RxStageCompute);
        if (FAILED(hr))
            return hr;
        Args.hComputeShader.pDrvPrivate = Shaders[RxStageCompute]->GetDriverHandle();
        Args.SampleMask = UINT_MAX;
    }
    else
    {
        static const RxShaderStage Order[] = { RxStageVertex, RxStagePixel, RxStageHull, RxStageDomain,
                                               RxStageGeometry };
        D3D12DDI_BLEND_DESC_0010 Blend;
        D3D12DDI_DEPTH_STENCIL_DESC_0095 Depth;
        D3D12DDI_RASTERIZER_DESC_0102 Raster;

        for (RxShaderStage Stage : Order)
        {
            hr = AcquireShader(Desc, Stage);
            if (FAILED(hr))
                return hr;
        }

        hr = RxCheckBlend(Device, Desc.Blend);
        if (FAILED(hr))
            return hr;
        RxToDdiBlend(Desc.Blend, &Blend);
        hr = RxPipelinePart::Acquire(Device, RxPipelinePart::Kind::Blend, reinterpret_cast<const BYTE*>(&Blend),
                                     sizeof(Blend), RxBuildBlend, &Blend, &BlendState);
        if (FAILED(hr))
            return hr;

        hr = RxCheckDepthStencil(Device, Desc.DepthStencil);
        if (FAILED(hr))
            return hr;
        RxToDdiDepthStencil(Desc.DepthStencil, &Depth);
        hr = RxPipelinePart::Acquire(Device, RxPipelinePart::Kind::DepthStencil, reinterpret_cast<const BYTE*>(&Depth),
                                     sizeof(Depth), RxBuildDepthStencil, &Depth, &DepthStencilState);
        if (FAILED(hr))
            return hr;

        hr = RxCheckRasterizer(Device, Desc.Rasterizer);
        if (FAILED(hr))
            return hr;
        RxToDdiRasterizer(Desc.Rasterizer, &Raster);
        hr = RxPipelinePart::Acquire(Device, RxPipelinePart::Kind::Rasterizer, reinterpret_cast<const BYTE*>(&Raster),
                                     sizeof(Raster), RxBuildRasterizer, &Raster, &RasterizerState);
        if (FAILED(hr))
            return hr;

        if (Shaders[RxStageVertex] != nullptr)
            Args.hVertexShader.pDrvPrivate = Shaders[RxStageVertex]->GetDriverHandle();
        if (Shaders[RxStagePixel] != nullptr)
            Args.hPixelShader.pDrvPrivate = Shaders[RxStagePixel]->GetDriverHandle();
        if (Shaders[RxStageDomain] != nullptr)
            Args.hDomainShader.pDrvPrivate = Shaders[RxStageDomain]->GetDriverHandle();
        if (Shaders[RxStageHull] != nullptr)
            Args.hHullShader.pDrvPrivate = Shaders[RxStageHull]->GetDriverHandle();
        if (Shaders[RxStageGeometry] != nullptr)
            Args.hGeometryShader.pDrvPrivate = Shaders[RxStageGeometry]->GetDriverHandle();
        Args.hBlendState.pDrvPrivate = BlendState->GetDriverHandle();
        Args.SampleMask = Desc.SampleMask;
        Args.hRasterizerState.pDrvPrivate = RasterizerState->GetDriverHandle();
        Args.hDepthStencilState.pDrvPrivate = DepthStencilState->GetDriverHandle();
        Args.IBStripCutValue = (D3D12DDI_INDEX_BUFFER_STRIP_CUT_VALUE)Desc.StripCut;
        Args.PrimitiveTopologyType = (D3D12DDI_PRIMITIVE_TOPOLOGY_TYPE)Desc.Topology;
        Args.NumRenderTargets = Desc.Targets.NumRenderTargets;
        CopyMemory(Args.RTVFormats, Desc.Targets.RTFormats, sizeof(Args.RTVFormats));
        Args.DSVFormat = Desc.DepthFormat;
        Args.SampleDesc = Desc.Samples;
    }

    Args.hRootSignature = Root->GetDriverHandle();
    if (Args.SampleDesc.Count == 0)
        Args.SampleDesc = { 1, 0 };
    Args.Flags = (D3D12DDI_PIPELINE_STATE_FLAGS)Desc.Flags;
    hr = Device.TranslateNodeMask(Desc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;

    DriverState.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivatePipelineStateSize(Device.Handle(), &Args));
    if (DriverState.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreatePipelineState(Device.Handle(), &Args, DriverState, Runtime);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver pipeline state failed 0x%08lX", hr);
        return hr;
    }
    IsDriverStateCreated = TRUE;

    return Device.MakePageableResident(nullptr, D3D12DDI_HT_PIPELINE_STATE);
}

RxPipelineState::~RxPipelineState()
{
    if (IsDriverStateCreated)
        Device.Ddi().pfnDestroyPipelineState(Device.Handle(), DriverState);

    /* The order Windows lets go of them in */
    if (DepthStencilState != nullptr)
        DepthStencilState->ReleaseUse();
    if (RasterizerState != nullptr)
        RasterizerState->ReleaseUse();
    if (BlendState != nullptr)
        BlendState->ReleaseUse();
    for (RxPipelinePart* Shader : Shaders)
    {
        if (Shader != nullptr)
            Shader->ReleaseUse();
    }
    if (Root != nullptr)
        Root->ReleaseUse();
}

HRESULT
STDMETHODCALLTYPE
RxPipelineState::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12PipelineState) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12PipelineState*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxPipelineState::GetCachedBlob(
    _COM_Outptr_ ID3DBlob** ppBlob)
{
    RX_UNIMPLEMENTED();
    if (ppBlob != nullptr)
        *ppBlob = nullptr;
    return E_NOTIMPL;
}

/* Device entries */

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateGraphicsPipelineState(
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDesc Desc;
    RxPipelineState* State;
    HRESULT hr;

    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    if (pDesc == nullptr || Level < D3D_FEATURE_LEVEL_9_1 || pDesc->VS.pShaderBytecode == nullptr)
        return E_INVALIDARG;

    RxGatherGraphicsDesc(*pDesc, &Desc);
    if (ppPipelineState == nullptr)
        return S_FALSE;

    hr = RxPipelineState::Create(*this, Desc, FALSE, &State);
    if (FAILED(hr))
        return hr;
    return RxHandOut12(State, riid, ppPipelineState);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateComputePipelineState(
    _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_ void** ppPipelineState)
{
    RxPipelineDesc Desc;
    RxPipelineState* State;
    HRESULT hr;

    if (ppPipelineState != nullptr)
        *ppPipelineState = nullptr;
    if (pDesc == nullptr || pDesc->CS.pShaderBytecode == nullptr)
        return E_INVALIDARG;

    RxGatherComputeDesc(*pDesc, &Desc);
    if (ppPipelineState == nullptr)
        return S_FALSE;

    hr = RxPipelineState::Create(*this, Desc, FALSE, &State);
    if (FAILED(hr))
        return hr;
    return RxHandOut12(State, riid, ppPipelineState);
}

/* Defaults */

HRESULT
RxDevice12::CreateDefaults()
{
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC Empty = {};
    ID3DBlob* Blob = nullptr;
    const BYTE* Part;
    UINT PartSize;
    BOOLEAN UseDxil = HighestShaderModel >= D3D_SHADER_MODEL_6_0;
    RxPipelineDesc Desc;
    HRESULT hr;

    if (Level == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return S_OK;

    Empty.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
    hr = RxSerializeVersionedRootSignature(&Empty, &Blob, nullptr);
    if (FAILED(hr))
        return hr;

    if (!RxFindRootPart(static_cast<const BYTE*>(Blob->GetBufferPointer()), Blob->GetBufferSize(), &Part, &PartSize))
        hr = E_FAIL;
    else
        hr = RxRootSignature::Create(*this, GetAllNodesMask(), Part, PartSize, TRUE, &DefaultRoot);
    Blob->Release();
    if (FAILED(hr))
        return hr;
    RememberRootSignature(DefaultRoot);

    /* A vertex shader writing nothing but a position, with every fixed function state at its default */
    if (Level != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        ZeroMemory(&Desc, sizeof(Desc));
        Desc.RootSignature = DefaultRoot;
        if (UseDxil)
            Desc.Shaders[RxStageVertex] = { RxInternalVertexDxil, sizeof(RxInternalVertexDxil) };
        else
            Desc.Shaders[RxStageVertex] = { RxInternalVertexDxbc, sizeof(RxInternalVertexDxbc) };

        for (D3D12_RENDER_TARGET_BLEND_DESC& Target : Desc.Blend.RenderTarget)
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
        Desc.Rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
        Desc.Rasterizer.CullMode = D3D12_CULL_MODE_BACK;
        Desc.Rasterizer.DepthClipEnable = TRUE;
        Desc.DepthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        Desc.DepthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        for (D3D12_DEPTH_STENCILOP_DESC1* Face : { &Desc.DepthStencil.FrontFace, &Desc.DepthStencil.BackFace })
        {
            Face->StencilFailOp = D3D12_STENCIL_OP_KEEP;
            Face->StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
            Face->StencilPassOp = D3D12_STENCIL_OP_KEEP;
            Face->StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            Face->StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
            Face->StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        }
        Desc.Topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        Desc.NodeMask = GetAllNodesMask();

        hr = RxPipelineState::Create(*this, Desc, TRUE, &DefaultGraphics);
        if (FAILED(hr))
            return hr;
    }

    /* An empty compute shader of one thread */
    ZeroMemory(&Desc, sizeof(Desc));
    Desc.RootSignature = DefaultRoot;
    if (UseDxil)
        Desc.Shaders[RxStageCompute] = { RxInternalComputeDxil, sizeof(RxInternalComputeDxil) };
    else
        Desc.Shaders[RxStageCompute] = { RxInternalComputeDxbc, sizeof(RxInternalComputeDxbc) };
    Desc.NodeMask = GetAllNodesMask();
    Desc.IsCompute = TRUE;
    return RxPipelineState::Create(*this, Desc, TRUE, &DefaultCompute);
}
