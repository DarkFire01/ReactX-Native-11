/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Format support answers built from the format rules and the driver's bits
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "formatsupport12.hpp"

constexpr UINT64 RxRowKnown = 1ull << 60;
constexpr UINT64 RxRowConfirm = 1ull << 61;
constexpr UINT64 RxRowTileable = 1ull << 62;
constexpr UINT64 RxRowVideo = 1ull << 63;

/* Driver bits from pfnCheckFormatSupport */
constexpr UINT RxDrvSample = 0x1;
constexpr UINT RxDrvRenderTarget = 0x2;
constexpr UINT RxDrvBlend = 0x4;
constexpr UINT RxDrvMultisampleTarget = 0x8;
constexpr UINT RxDrvMultisampleLoad = 0x10;
constexpr UINT RxDrvDecoderOutput = 0x20;
constexpr UINT RxDrvProcessorOutput = 0x40;
constexpr UINT RxDrvProcessorInput = 0x80;
constexpr UINT RxDrvVertexBuffer = 0x100;
constexpr UINT RxDrvTypedUav = 0x200;
constexpr UINT RxDrvBuffer = 0x400;
constexpr UINT RxDrvCapture = 0x800;
constexpr UINT RxDrvEncoder = 0x1000;
constexpr UINT RxDrvGather = 0x4000;
constexpr UINT RxDrvOverlay = 0x8000;
constexpr UINT RxDrvTypedLoad = 0x20000;
constexpr UINT RxDrvDisplayable = 0x200000;
constexpr UINT RxDrvUnsupported = 0x80000000;

/* FORMAT_SUPPORT1 bit with no public name */
constexpr UINT RxSupportCapture = 0x80000000;

static
const UINT64*
RxRulesFor(
    _In_ D3D_FEATURE_LEVEL Level)
{
    switch (Level)
    {
        case D3D_FEATURE_LEVEL_11_0:
            return RxFormatRules11_0;
        case D3D_FEATURE_LEVEL_11_1:
        case D3D_FEATURE_LEVEL_12_0:
        case D3D_FEATURE_LEVEL_12_1:
        case D3D_FEATURE_LEVEL_12_2:
            return RxFormatRules11_1;
        default:
            return nullptr;
    }
}

static
UINT64
RxRowOf(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format)
{
    const UINT64* Rules = RxRulesFor(Level);

    if (Rules == nullptr || (UINT)Format >= RxFormatRuleCount12)
        return 0;

    return Rules[Format];
}

static
RxFormatRule12
RxRuleIn(
    _In_ UINT64 Row,
    _In_ RxFormatCap12 Cap)
{
    return (RxFormatRule12)((Row >> (Cap * 2)) & 3);
}

static
BOOLEAN
RxFormatIn(
    _In_ DXGI_FORMAT Format,
    _In_reads_(Count) const UINT8* List,
    _In_ UINT Count)
{
    UINT Index;

    for (Index = 0; Index < Count; Index++)
    {
        if (List[Index] == (UINT)Format)
            return TRUE;
    }

    return FALSE;
}

RxFormatRule12
RxTypedLoadRule12(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format)
{
    /* Loads every level 12 device does on top of the 32 bit single channel ones */
    static const UINT8 Level12Loads[] = { 2, 3, 4, 10, 12, 14, 28, 30, 32, 54, 57, 59, 61, 62, 64 };
    static const UINT8 DriverLoads[] =
    {
        2, 3, 4, 10, 11, 12, 13, 14, 16, 17, 18, 24, 25, 26, 28, 30, 31, 32, 34, 35, 36, 37, 38,
        49, 50, 51, 52, 54, 56, 57, 58, 59, 61, 62, 63, 64, 65, 67, 85, 86, 87, 115, 191
    };

    if (Level < D3D_FEATURE_LEVEL_11_0)
        return RxRuleNone;
    if (Format == DXGI_FORMAT_R32_FLOAT || Format == DXGI_FORMAT_R32_UINT || Format == DXGI_FORMAT_R32_SINT)
        return RxRuleRequired;
    if (Level >= D3D_FEATURE_LEVEL_12_0 && RxFormatIn(Format, Level12Loads, _countof(Level12Loads)))
        return RxRuleRequired;
    if (RxRuleIn(RxRowOf(Level, Format), RxCapTypedUav) == RxRuleNone)
        return RxRuleNone;

    return RxFormatIn(Format, DriverLoads, _countof(DriverLoads)) ? RxRuleDriver : RxRuleNone;
}

/* Compare and exchange on typed UAVs: promised for the 32 bit integers, never offered otherwise */
static
RxFormatRule12
RxAtomicRule(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format)
{
    if (Level < D3D_FEATURE_LEVEL_11_0)
        return RxRuleNone;
    if (Format == DXGI_FORMAT_R32_UINT || Format == DXGI_FORMAT_R32_SINT)
        return RxRuleRequired;

    return RxRuleDriver;
}

static
BOOLEAN
RxMayBeDisplayable(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format)
{
    if (Level < D3D_FEATURE_LEVEL_11_0)
        return FALSE;

    switch (Format)
    {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_YUY2:
            return TRUE;
        default:
            return FALSE;
    }
}

static
BOOLEAN
RxCastsWhenTyped(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
            return TRUE;
        default:
            return FALSE;
    }
}

BOOLEAN
RxFormatKnown12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format)
{
    return (RxRowOf(Device.Level, Format) & RxRowKnown) != 0;
}

BOOLEAN
RxFormatTileable12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount)
{
    UINT64 Row = RxRowOf(Device.Level, Format);

    if (Device.Level < D3D_FEATURE_LEVEL_11_0 || (Row & RxRowTileable) == 0)
        return FALSE;

    /* Four samples need a format that is not left open for multisampled targets */
    if (SampleCount != 1 && (SampleCount != 4 || RxRuleIn(Row, RxCapMultisampleTarget) == RxRuleOpen))
        return FALSE;

    return RxRuleIn(Row, RxCapTexture2D) != RxRuleOpen;
}

/* Tiled 2D textures are promised from level 12, left to the device's tier below it */
static
RxFormatRule12
RxTiledRule(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format)
{
    if (!RxFormatTileable12(Device, Format, 1))
        return RxRuleNone;

    return Device.Level >= D3D_FEATURE_LEVEL_12_0 ? RxRuleRequired : RxRuleDriver;
}

/* Whether any answer for the format comes from the driver */
static
BOOLEAN
RxAsksDriver(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT64 Row)
{
    static const RxFormatCap12 DriverCaps[] =
    {
        RxCapVertexBuffer, RxCapSample, RxCapRenderTarget, RxCapBlend, RxCapMultisampleTarget,
        RxCapMultisampleLoad, RxCapTypedUav, RxCapDecoderOutput, RxCapProcessorInput, RxCapProcessorOutput,
        RxCapEncoder, RxCapCapture, RxCapGather, RxCapBuffer, RxCapScanOut, RxCapStreamOutput
    };
    UINT Index;

    for (Index = 0; Index < _countof(DriverCaps); Index++)
    {
        if (RxRuleIn(Row, DriverCaps[Index]) == RxRuleDriver)
            return TRUE;
    }

    if (RxTypedLoadRule12(Device.Level, Format) == RxRuleDriver || (Row & RxRowVideo) != 0)
        return TRUE;

    /* Only formats that can never scan out and never be displayable skip the driver */
    return RxRuleIn(Row, RxCapScanOut) != RxRuleNone || RxMayBeDisplayable(Device.Level, Format);
}

static
UINT
RxPromised1(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT64 Row)
{
    static const struct
    {
        RxFormatCap12 Cap;
        UINT Bit;
    } Map[] =
    {
        { RxCapBuffer, D3D12_FORMAT_SUPPORT1_BUFFER },
        { RxCapVertexBuffer, D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER },
        { RxCapIndexBuffer, D3D12_FORMAT_SUPPORT1_IA_INDEX_BUFFER },
        { RxCapStreamOutput, D3D12_FORMAT_SUPPORT1_SO_BUFFER },
        { RxCapTexture1D, D3D12_FORMAT_SUPPORT1_TEXTURE1D },
        { RxCapTexture2D, D3D12_FORMAT_SUPPORT1_TEXTURE2D },
        { RxCapTexture3D, D3D12_FORMAT_SUPPORT1_TEXTURE3D },
        { RxCapTextureCube, D3D12_FORMAT_SUPPORT1_TEXTURECUBE },
        { RxCapShaderLoad, D3D12_FORMAT_SUPPORT1_SHADER_LOAD },
        { RxCapSample, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE },
        { RxCapSampleCompare, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_COMPARISON },
        { RxCapSampleMono, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE_MONO_TEXT },
        { RxCapGather, D3D12_FORMAT_SUPPORT1_SHADER_GATHER },
        { RxCapGatherCompare, D3D12_FORMAT_SUPPORT1_SHADER_GATHER_COMPARISON },
        { RxCapMips, D3D12_FORMAT_SUPPORT1_MIP },
        { RxCapRenderTarget, D3D12_FORMAT_SUPPORT1_RENDER_TARGET },
        { RxCapTypedUav, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW },
        { RxCapBlend, D3D12_FORMAT_SUPPORT1_BLENDABLE },
        { RxCapDepthStencil, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL },
        { RxCapMultisampleTarget, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET },
        { RxCapMultisampleLoad, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD },
        { RxCapMultisampleResolve, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE },
        { RxCapScanOut, D3D12_FORMAT_SUPPORT1_DISPLAY },
        { RxCapCastInLayout, D3D12_FORMAT_SUPPORT1_CAST_WITHIN_BIT_LAYOUT },
    };
    static const struct
    {
        RxFormatCap12 Cap;
        UINT Bit;
    } VideoMap[] =
    {
        { RxCapProcessorInput, D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT },
        { RxCapProcessorOutput, D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT },
        { RxCapDecoderOutput, D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT },
        { RxCapEncoder, D3D12_FORMAT_SUPPORT1_VIDEO_ENCODER },
        { RxCapCapture, RxSupportCapture },
    };
    UINT Support = 0;
    UINT Index;

    for (Index = 0; Index < _countof(Map); Index++)
    {
        if (RxRuleIn(Row, Map[Index].Cap) == RxRuleRequired)
            Support |= Map[Index].Bit;
    }

    if (RxCastsWhenTyped(Format))
        Support |= D3D12_FORMAT_SUPPORT1_BACK_BUFFER_CAST;

    if (Device.Video)
    {
        for (Index = 0; Index < _countof(VideoMap); Index++)
        {
            if (RxRuleIn(Row, VideoMap[Index].Cap) == RxRuleRequired)
                Support |= VideoMap[Index].Bit;
        }
    }

    return Support;
}

static
UINT
RxPromised2(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT64 Row)
{
    RxFormatRule12 LogicOp = RxRuleIn(Row, RxCapLogicOp);
    RxFormatRule12 Tiled = RxTiledRule(Device, Format);
    UINT Support = 0;

    if (Device.Level >= D3D_FEATURE_LEVEL_11_0)
    {
        if (Format == DXGI_FORMAT_R32_UINT || Format == DXGI_FORMAT_R32_SINT)
        {
            Support |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_BITWISE_OPS |
                       D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_SIGNED_MIN_OR_MAX |
                       D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_UNSIGNED_MIN_OR_MAX;
        }
        if (Format == DXGI_FORMAT_R32_FLOAT || Format == DXGI_FORMAT_R32_UINT || Format == DXGI_FORMAT_R32_SINT)
            Support |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE;
    }

    if (RxAtomicRule(Device.Level, Format) == RxRuleRequired)
        Support |= D3D12_FORMAT_SUPPORT2_UAV_ATOMIC_COMPARE_STORE_OR_COMPARE_EXCHANGE;
    if (RxTypedLoadRule12(Device.Level, Format) == RxRuleRequired)
        Support |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    if (RxRuleIn(Row, RxCapTypedUav) == RxRuleRequired)
        Support |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (LogicOp == RxRuleRequired || (LogicOp == RxRuleDriver && Device.LogicOp))
        Support |= D3D12_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP;
    if (Tiled == RxRuleRequired || (Tiled == RxRuleDriver && Device.Tiled))
        Support |= D3D12_FORMAT_SUPPORT2_TILED;
    if (Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE || Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE)
        Support |= D3D12_FORMAT_SUPPORT2_SAMPLER_FEEDBACK;

    return Support;
}

/* Bits the driver grants where the rules leave the choice to it */
static
VOID
RxAddDriverBits(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT64 Row,
    _In_ UINT Driver,
    _Inout_ UINT* Support1,
    _Inout_ UINT* Support2)
{
    static const struct
    {
        RxFormatCap12 Cap;
        UINT DriverBit;
        UINT Bit;
        BOOLEAN IsVideo;
    } Map[] =
    {
        { RxCapVertexBuffer, RxDrvVertexBuffer, D3D12_FORMAT_SUPPORT1_IA_VERTEX_BUFFER, FALSE },
        { RxCapSample, RxDrvSample, D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE, FALSE },
        { RxCapBlend, RxDrvBlend, D3D12_FORMAT_SUPPORT1_BLENDABLE, FALSE },
        { RxCapMultisampleTarget, RxDrvMultisampleTarget, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET, FALSE },
        { RxCapMultisampleLoad, RxDrvMultisampleLoad, D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD, FALSE },
        { RxCapTypedUav, RxDrvTypedUav, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW, FALSE },
        { RxCapDecoderOutput, RxDrvDecoderOutput, D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT, TRUE },
        { RxCapProcessorInput, RxDrvProcessorInput, D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_INPUT, TRUE },
        { RxCapProcessorOutput, RxDrvProcessorOutput, D3D12_FORMAT_SUPPORT1_VIDEO_PROCESSOR_OUTPUT, TRUE },
        { RxCapEncoder, RxDrvEncoder, D3D12_FORMAT_SUPPORT1_VIDEO_ENCODER, TRUE },
        { RxCapCapture, RxDrvCapture, RxSupportCapture, TRUE },
        { RxCapGather, RxDrvGather, D3D12_FORMAT_SUPPORT1_SHADER_GATHER, FALSE },
        { RxCapBuffer, RxDrvBuffer, D3D12_FORMAT_SUPPORT1_BUFFER, FALSE },
    };
    BOOLEAN StoreOk;
    UINT Index;

    for (Index = 0; Index < _countof(Map); Index++)
    {
        if (Map[Index].IsVideo && !Device.Video)
            continue;
        if (RxRuleIn(Row, Map[Index].Cap) == RxRuleDriver && (Driver & Map[Index].DriverBit) != 0)
            *Support1 |= Map[Index].Bit;
    }

    /* A driver that cannot render to the format takes blending away with it */
    if (RxRuleIn(Row, RxCapRenderTarget) == RxRuleDriver)
    {
        if ((Driver & RxDrvRenderTarget) != 0)
            *Support1 |= D3D12_FORMAT_SUPPORT1_RENDER_TARGET;
        else
            *Support1 &= ~D3D12_FORMAT_SUPPORT1_BLENDABLE;
    }

    if ((Driver & RxDrvOverlay) != 0)
        *Support2 |= D3D12_FORMAT_SUPPORT2_MULTIPLANE_OVERLAY;

    StoreOk = RxRuleIn(Row, RxCapTypedUav) == RxRuleDriver && (Driver & RxDrvTypedUav) != 0;
    if (StoreOk)
        *Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;

    /* Typed loads need the device's load tier and a store the format already has */
    if (RxTypedLoadRule12(Device.Level, Format) == RxRuleDriver && (Driver & RxDrvTypedLoad) != 0 &&
        (Device.TypedLoadExtra ||
         (Device.TypedLoadMinimal && Device.Level >= D3D_FEATURE_LEVEL_11_0 &&
          (Format == DXGI_FORMAT_R8G8_UINT || Format == DXGI_FORMAT_R8_UINT))) &&
        (RxRuleIn(Row, RxCapTypedUav) == RxRuleRequired || StoreOk))
    {
        *Support2 |= D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD;
    }

    if (Device.DisplayableDdi && RxMayBeDisplayable(Device.Level, Format) && (Driver & RxDrvDisplayable) != 0)
        *Support2 |= D3D12_FORMAT_SUPPORT2_DISPLAYABLE;

    if (Device.DisplayableDdi)
    {
        if (RxRuleIn(Row, RxCapScanOut) == RxRuleDriver && (Driver & RxDrvDisplayable) != 0)
            *Support1 |= D3D12_FORMAT_SUPPORT1_DISPLAY;
        return;
    }

    /* Older drivers get no buffer, target or UAV use of the shared exponent format */
    if (Format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP)
    {
        *Support1 &= ~(D3D12_FORMAT_SUPPORT1_DISPLAY | D3D12_FORMAT_SUPPORT1_BUFFER | D3D12_FORMAT_SUPPORT1_BLENDABLE |
                       D3D12_FORMAT_SUPPORT1_RENDER_TARGET | D3D12_FORMAT_SUPPORT1_MULTISAMPLE_LOAD |
                       D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET |
                       D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW);
        *Support2 &= ~(D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE |
                       D3D12_FORMAT_SUPPORT2_DISPLAYABLE);
    }
}

BOOLEAN
RxCheckFormatSupport12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ RxFormatQuery12 Query,
    _In_ VOID* QueryContext,
    _Out_ UINT* Support1,
    _Out_ UINT* Support2)
{
    UINT64 Row = RxRowOf(Device.Level, Format);

    *Support1 = 0;
    *Support2 = 0;

    if ((Row & RxRowKnown) == 0)
        return FALSE;
    if (!Device.SamplerFeedback &&
        (Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE || Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE))
    {
        return FALSE;
    }
    if ((Row & RxRowConfirm) != 0 && Query(QueryContext, Format) == RxDrvUnsupported)
        return FALSE;

    *Support1 = RxPromised1(Device, Format, Row);
    *Support2 = RxPromised2(Device, Format, Row);

    if (RxAsksDriver(Device, Format, Row))
        RxAddDriverBits(Device, Format, Row, Query(QueryContext, Format), Support1, Support2);

    /* Sampling NV12 implies loading it, and shared exponent buffers can be resolved */
    if (Format == DXGI_FORMAT_NV12 && (*Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0)
        *Support1 |= D3D12_FORMAT_SUPPORT1_SHADER_LOAD;
    if (Format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP && (*Support1 & D3D12_FORMAT_SUPPORT1_BUFFER) != 0)
        *Support1 |= D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE;

    return *Support1 != 0 || *Support2 != 0;
}
