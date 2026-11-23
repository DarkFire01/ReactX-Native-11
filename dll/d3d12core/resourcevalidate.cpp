/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The rules resource descriptions, heap properties and initial states must meet
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatlayout12.hpp"

#include <d3d12video.h>

constexpr UINT RxKnownResourceFlags = 0xFFF;
constexpr UINT RxKnownHeapFlags = 0x3FED;
constexpr UINT RxLutFlag3D = 0x200;
constexpr UINT RxLutFlag1D = 0x800;
constexpr UINT RxGuidLayout = 4;
constexpr UINT64 RxLargeAlignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT64 RxSmallAlignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT64 RxMsaaAlignment = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT RxTargetOrDepth = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
constexpr UINT RxVideoReference = D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
                                  D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY;
constexpr UINT RxDenyKinds = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES |
                             D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

HRESULT
RxDevice12::QueryValidationCaps()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS Base = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 Options2 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS4 Options4 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 Options6 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 Options7 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS8 Options8 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS14 Options14 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS16 Options16 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS17 Options17 = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS20 Options20 = {};
    RxValidationCaps12& Caps = ValidationCaps;

    /* A query the device can not answer leaves its facts at zero */
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Base, sizeof(Base));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &Options2, sizeof(Options2));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS4, &Options4, sizeof(Options4));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &Options6, sizeof(Options6));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &Options7, sizeof(Options7));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS8, &Options8, sizeof(Options8));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS14, &Options14, sizeof(Options14));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &Options16, sizeof(Options16));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS17, &Options17, sizeof(Options17));
    CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS20, &Options20, sizeof(Options20));

    Caps.TiledTier = Base.TiledResourcesTier;
    Caps.ResourceHeapTier = Base.ResourceHeapTier;
    Caps.StandardSwizzle = Base.StandardSwizzle64KBSupported != FALSE;
    Caps.CrossAdapterRowMajor = Base.CrossAdapterRowMajorTextureSupported != FALSE;
    Caps.SamplePositionsTier = Options2.ProgrammableSamplePositionsTier;
    Caps.Msaa64KbAligned = Options4.MSAA64KBAlignedTextureSupported != FALSE;
    Caps.SrvOnlyTiledTier3 = Options5.SRVOnlyTiledResourceTier3 != FALSE;
    Caps.RaytracingTier = Options5.RaytracingTier;
    Caps.ShadingRateTier = Options6.VariableShadingRateTier;
    Caps.SamplerFeedbackTier = Options7.SamplerFeedbackTier;
    Caps.UnalignedBlocks = Options8.UnalignedBlockTexturesSupported != FALSE;
    Caps.WriteableMsaa = Options14.WriteableMSAATexturesSupported != FALSE;
    Caps.GpuUploadHeaps = Options16.GPUUploadHeapSupported != FALSE;
    Caps.ManualWriteTracking = Options17.ManualWriteTrackingResourceSupported != FALSE;
    Caps.ComputeWriteWatch = Options20.ComputeOnlyWriteWatchSupported != FALSE;
    return S_OK;
}

static
BOOLEAN
RxIsCoreLevel(
    _In_ D3D_FEATURE_LEVEL Level)
{
    return Level == D3D_FEATURE_LEVEL_1_0_CORE || Level == D3D_FEATURE_LEVEL_1_0_GENERIC;
}

static
BOOLEAN
RxIsBlockCompressedFormat(
    _In_ DXGI_FORMAT Format)
{
    return (Format >= DXGI_FORMAT_BC1_TYPELESS && Format <= DXGI_FORMAT_BC5_SNORM) ||
           (Format >= DXGI_FORMAT_BC6H_TYPELESS && Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

static
BOOLEAN
RxIsFeedbackFormat(
    _In_ DXGI_FORMAT Format)
{
    return Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE ||
           Format == DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
}

/* Mip levels a chain down to 1x1 needs */
static
UINT
RxFullMipCount(
    _In_ UINT64 Largest)
{
    UINT Count = 0;

    while (Largest != 0)
    {
        Largest >>= 1;
        Count += 1;
    }

    return Count;
}

static
UINT
RxLayoutDepth(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    return Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? Desc.DepthOrArraySize : 1;
}

static
HRESULT
RxQueryFormat(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format,
    _Out_ D3D12_FEATURE_DATA_FORMAT_SUPPORT* Support)
{
    ZeroMemory(Support, sizeof(*Support));
    Support->Format = Format;
    return Device.CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, Support, sizeof(*Support));
}

/* Whether the format, or any format of its cast family, has a FORMAT_SUPPORT1 bit */
static
BOOLEAN
RxFamilySupports(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Support1)
{
    DXGI_FORMAT Family = RxFormatFamily(Format);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;

    if (Family == DXGI_FORMAT_UNKNOWN)
        return SUCCEEDED(RxQueryFormat(Device, Format, &Support)) && (Support.Support1 & Support1) != 0;

    for (UINT Index = 1; Index <= DXGI_FORMAT_A4B4G4R4_UNORM; Index++)
    {
        if (RxFormatFamily((DXGI_FORMAT)Index) != Family)
            continue;
        if (SUCCEEDED(RxQueryFormat(Device, (DXGI_FORMAT)Index, &Support)) && (Support.Support1 & Support1) != 0)
            return TRUE;
    }

    return FALSE;
}

struct RxTileExtent
{
    UINT Width;
    UINT Height;
    UINT Depth;
};

/* Texels one 4KB tile covers; Scale grows each axis for the 64KB tile */
static
RxTileExtent
RxTileShape(
    _In_ DXGI_FORMAT Format,
    _In_ D3D12_RESOURCE_DIMENSION Dimension,
    _In_ UINT Samples,
    _In_ UINT Scale)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);
    UINT Bits = Layout != nullptr ? Layout->BitsPerUnit : 0;
    RxTileExtent Tile = { 1, 1, 1 };

    if (Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        if (RxIsBlockCompressedFormat(Format))
        {
            Tile.Width = Layout->WidthAlign * 16 * (Bits == 64 ? 2 : 1);
            Tile.Height = Layout->HeightAlign * 16;
        }
        else
        {
            if (Bits <= 8)
                Tile = { 64, 64, 1 };
            else if (Bits <= 16)
                Tile = { 64, 32, 1 };
            else if (Bits <= 32)
                Tile = { 32, 32, 1 };
            else if (Bits <= 64)
                Tile = { 32, 16, 1 };
            else
                Tile = { 16, 16, 1 };

            /* Each doubling of the sample count halves the width first, then the height */
            for (UINT Step = 1; Step < Samples && Step < 16; Step *= 2)
            {
                if ((Step & 0x5) != 0)
                    Tile.Width /= 2;
                else
                    Tile.Height /= 2;
            }
        }
        Tile.Width *= Scale;
        Tile.Height *= Scale;
        return Tile;
    }

    if (Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        if (RxIsBlockCompressedFormat(Format))
            return { Layout->WidthAlign * 8u, Layout->HeightAlign * 8u, Bits == 64 ? 8u : 4u };
        if (Bits <= 8)
            return { 16, 16, 16 };
        if (Bits <= 16)
            return { 16, 16, 8 };
        if (Bits <= 32)
            return { 16, 8, 8 };
        if (Bits <= 64)
            return { 8, 8, 8 };
        return { 8, 8, 4 };
    }

    Tile.Width = (Bits != 0 ? 32768 / Bits : 4096) * Scale * Scale;
    return Tile;
}

static
UINT64
RxTileCount(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const RxTileExtent& Tile)
{
    UINT64 Across = (Desc.Width + Tile.Width - 1) / Tile.Width;
    UINT64 Down = (Desc.Height + Tile.Height - 1) / Tile.Height;
    UINT64 Deep = (RxLayoutDepth(Desc) + Tile.Depth - 1) / Tile.Depth;

    return Across * Down * Deep;
}

/* A sampler feedback mip region side: a power of two from 4 up to half the texture */
static
BOOLEAN
RxMipRegionFits(
    _In_ UINT Region,
    _In_ UINT64 Size)
{
    return Region >= 4 && (Region & (Region - 1)) == 0 && Region <= Size / 2;
}

/* Alignment of a texture with the driver chosen layout */
static
HRESULT
RxCheckOpaqueAlignment(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    RxTileExtent Tile;

    if (Desc.Alignment == 0)
        return S_OK;

    if (Desc.SampleDesc.Count > 1)
    {
        if (Desc.Alignment == RxMsaaAlignment)
            return S_OK;
        if (Desc.Alignment != RxLargeAlignment)
            return E_INVALIDARG;
        if (Device.GetValidationCaps().Msaa64KbAligned)
            return S_OK;
        Tile = RxTileShape(Desc.Format, Desc.Dimension, Desc.SampleDesc.Count, 4);
        return RxTileCount(Desc, Tile) > 64 ? E_INVALIDARG : S_OK;
    }

    if (Desc.Alignment == RxLargeAlignment)
        return S_OK;

    /* Targets, depth and video references keep the 64KB alignment */
    if ((Desc.Flags & (RxTargetOrDepth | RxVideoReference)) != 0 || Desc.Alignment != RxSmallAlignment)
        return E_INVALIDARG;
    if (RxFormatIsPlanar(Desc.Format))
        return E_INVALIDARG;

    Tile = RxTileShape(Desc.Format, Desc.Dimension, Desc.SampleDesc.Count, 1);
    return RxTileCount(Desc, Tile) > 16 ? E_INVALIDARG : S_OK;
}

static
HRESULT
RxCheckBufferDesc(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear)
{
    constexpr UINT TextureOnly = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                                 D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | RxVideoReference;

    if ((Desc.Alignment & ~RxLargeAlignment) != 0)
        return E_INVALIDARG;
    if (Desc.Height != 1 || Desc.DepthOrArraySize != 1 || Desc.MipLevels != 1 || Desc.Format != DXGI_FORMAT_UNKNOWN)
        return E_INVALIDARG;
    if (Desc.SampleDesc.Count != 1 || Desc.SampleDesc.Quality != 0 || Desc.Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR)
        return E_INVALIDARG;
    if ((Desc.Flags & TextureOnly) != 0 || Clear != nullptr)
        return E_INVALIDARG;

    return S_OK;
}

/* Multisampled textures: one mip, 2D, a quality the format has and a target to render into */
static
HRESULT
RxCheckSampling(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ UINT Mips)
{
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Levels = {};
    UINT Count = Desc.SampleDesc.Count;
    UINT Quality = Desc.SampleDesc.Quality;

    if (Mips != 1 || Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        return E_INVALIDARG;

    Levels.Format = Desc.Format;
    Levels.SampleCount = Count;
    if (Desc.Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
        Levels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE;
    Device.CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Levels, sizeof(Levels));

    if (Quality >= DXGI_CENTER_MULTISAMPLE_QUALITY_PATTERN)
    {
        BOOLEAN Standard = Count == 1 || Count == 2 || Count == 4 || Count == 8 || Count == 16;

        if (!Standard || Levels.NumQualityLevels == 0)
            return E_INVALIDARG;
    }
    else if (Quality >= Levels.NumQualityLevels)
    {
        return E_INVALIDARG;
    }

    if ((Desc.Flags & RxTargetOrDepth) == 0)
        return E_INVALIDARG;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0 && !Device.GetValidationCaps().WriteableMsaa)
        return E_INVALIDARG;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0)
        return E_INVALIDARG;

    return S_OK;
}

/* The rules each texture layout adds */
static
HRESULT
RxCheckTextureLayout(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    const RxValidationCaps12& Caps = Device.GetValidationCaps();
    const RxFormatLayout12* Layout = RxFormatLayout(Desc.Format);
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;

    switch ((INT)Desc.Layout)
    {
        case D3D12_TEXTURE_LAYOUT_UNKNOWN:
            return RxCheckOpaqueAlignment(Device, Desc);

        case D3D12_TEXTURE_LAYOUT_ROW_MAJOR:
            if ((Desc.Alignment & ~RxLargeAlignment) != 0 || Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
                return E_INVALIDARG;
            if (Desc.MipLevels != 1 || Desc.DepthOrArraySize != 1)
                return E_INVALIDARG;
            if (Desc.SampleDesc.Count != 1 || Desc.SampleDesc.Quality != 0)
                return E_INVALIDARG;
            if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
                return E_INVALIDARG;
            if (!Caps.CrossAdapterRowMajor &&
                (Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0)
            {
                return E_INVALIDARG;
            }
            return RxFormatIsYuv(Desc.Format) ? E_INVALIDARG : S_OK;

        case D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE:
            if (Desc.SampleDesc.Count > 1 || (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0 ||
                Layout->BitsPerUnit == 96 || Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            {
                return E_INVALIDARG;
            }
            return (Desc.Alignment & ~RxLargeAlignment) != 0 ? E_INVALIDARG : S_OK;

        case D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE:
            if ((Desc.Alignment & ~RxLargeAlignment) != 0 || Caps.TiledTier == 0)
                return E_INVALIDARG;
            if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
                return E_INVALIDARG;
            if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && Caps.TiledTier < D3D12_TILED_RESOURCES_TIER_3)
            {
                if (!Caps.SrvOnlyTiledTier3)
                    return E_INVALIDARG;
                if ((Desc.Flags & (RxTargetOrDepth | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0)
                    return E_INVALIDARG;
            }
            RxQueryFormat(Device, Desc.Format, &Support);
            return (Support.Support2 & D3D12_FORMAT_SUPPORT2_TILED) != 0 ? S_OK : E_INVALIDARG;

        default:
            return E_NOTIMPL;
    }
}

/* What the access flags need from the format, and which flags may go together */
static
HRESULT
RxCheckAccessFlags(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear)
{
    UINT Flags = Desc.Flags;
    ID3D12Device* VideoDevice = nullptr;

    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
    {
        if (Device.GetFeatureLevel() < D3D_FEATURE_LEVEL_11_0)
            return E_INVALIDARG;
        if (!RxIsFeedbackFormat(Desc.Format) &&
            !RxFamilySupports(Device, Desc.Format, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW))
        {
            return E_INVALIDARG;
        }
    }

    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
        !RxFamilySupports(Device, Desc.Format, D3D12_FORMAT_SUPPORT1_RENDER_TARGET))
    {
        return E_INVALIDARG;
    }

    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
    {
        constexpr UINT NotWithDepth = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                                      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

        if ((Flags & NotWithDepth) != 0)
            return E_INVALIDARG;
        if (!RxFamilySupports(Device, Desc.Format, D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
            return E_INVALIDARG;
    }
    else if ((Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0 && (Flags & RxVideoReference) == 0)
    {
        return E_INVALIDARG;
    }

    if ((Flags & RxVideoReference) == 0)
        return S_OK;

    /* Video references need a device that does video */
    if (FAILED(Device.QueryInterface(__uuidof(ID3D12VideoDevice), reinterpret_cast<void**>(&VideoDevice))))
        return E_INVALIDARG;
    VideoDevice->Release();

    if ((Flags & D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY) != 0)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;
        constexpr UINT Allowed = D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        RxQueryFormat(Device, Desc.Format, &Support);
        if ((Support.Support1 & D3D12_FORMAT_SUPPORT1_DECODER_OUTPUT) == 0 || (Flags & ~Allowed) != 0 ||
            (Flags & Allowed) != Allowed)
        {
            return E_INVALIDARG;
        }
    }
    else
    {
        constexpr UINT Allowed = D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

        if ((Flags & ~Allowed) != 0 || (Flags & Allowed) != Allowed)
            return E_INVALIDARG;
    }

    if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || Desc.Layout != D3D12_TEXTURE_LAYOUT_UNKNOWN ||
        Desc.MipLevels != 1 || Clear != nullptr)
    {
        return E_INVALIDARG;
    }

    return S_OK;
}

/* An optimized clear value must suit the resource it is given with */
static
HRESULT
RxCheckClearValue(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const D3D12_CLEAR_VALUE& Clear)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;
    BOOLEAN ExactFormat;

    if ((Desc.Flags & RxTargetOrDepth) == 0)
        return E_INVALIDARG;
    if (!RxFormatInHeader(Clear.Format) || FAILED(RxQueryFormat(Device, Clear.Format, &Support)))
        return E_INVALIDARG;
    if (RxFormatIsTypeless(Clear.Format))
        return E_INVALIDARG;

    /* A fully typed resource takes its own format; others take any of their family */
    ExactFormat = !RxFormatIsTypeless(Desc.Format) &&
                  !(Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
                    RxFormatCastsWhenTyped(Desc.Format, Device.GetFeatureLevel()));
    if (ExactFormat ? Clear.Format != Desc.Format : RxFormatFamily(Clear.Format) != RxFormatFamily(Desc.Format))
        return E_INVALIDARG;

    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 &&
        (Support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) == 0)
    {
        return E_INVALIDARG;
    }

    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
    {
        if ((Support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) == 0)
            return E_INVALIDARG;
        if (Clear.DepthStencil.Depth < 0.0f || Clear.DepthStencil.Depth > 1.0f)
            return E_INVALIDARG;
    }

    return S_OK;
}

static
HRESULT
RxCheckFeedbackRegion(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    const D3D12_MIP_REGION& Region = Desc.SamplerFeedbackMipRegion;

    if (!RxIsFeedbackFormat(Desc.Format))
        return (Region.Width | Region.Height | Region.Depth) != 0 ? E_INVALIDARG : S_OK;

    if (Desc.Width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION || !RxMipRegionFits(Region.Width, Desc.Width) ||
        !RxMipRegionFits(Region.Height, Desc.Height) || Region.Depth >= 2)
    {
        return E_INVALIDARG;
    }

    return S_OK;
}

HRESULT
RxValidateResourceDesc(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear)
{
    const RxValidationCaps12& Caps = Device.GetValidationCaps();
    D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    const RxFormatLayout12* Layout;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support;
    UINT DimensionSupport;
    UINT64 WidthLimit;
    UINT64 Largest;
    UINT HeightLimit;
    UINT DepthLimit;
    UINT Mips;
    HRESULT FormatResult;
    HRESULT hr;

    if ((INT)Desc.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D || !RxFormatInHeader(Desc.Format) ||
        (INT)Desc.Layout > (INT)RxGuidLayout || (Desc.Flags & ~RxKnownResourceFlags) != 0)
    {
        return E_INVALIDARG;
    }
    if (Desc.Width == 0 || Desc.Height == 0 || Desc.DepthOrArraySize == 0)
        return E_INVALIDARG;
    if (RxIsCoreLevel(Level) && Desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER &&
        Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        return E_INVALIDARG;
    }

    /* The device reports no texture layout GUIDs */
    if ((UINT)Desc.Layout == RxGuidLayout)
        return E_INVALIDARG;

    if ((Desc.Flags & RxLutFlag3D) != 0)
    {
        static const UINT LutSides[] = { 17, 33, 45, 65 };
        BOOLEAN Found = FALSE;

        if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D || Desc.Width != Desc.Height ||
            Desc.Height != Desc.DepthOrArraySize)
        {
            return E_INVALIDARG;
        }
        for (UINT Side : LutSides)
            Found = Found || Desc.Width == Side;
        if (!Found)
            return E_INVALIDARG;
    }
    if ((Desc.Flags & RxLutFlag1D) != 0 && Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        return E_INVALIDARG;

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return RxCheckBufferDesc(Desc, Clear);
    if (Desc.Dimension < D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        return E_INVALIDARG;

    FormatResult = RxQueryFormat(Device, Desc.Format, &Support);

    /* Size limits by dimension and level */
    WidthLimit = Level >= D3D_FEATURE_LEVEL_11_0 ? D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION : 8192;
    DepthLimit = Level >= D3D_FEATURE_LEVEL_11_0 ? D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION : 512;
    switch (Desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            HeightLimit = 1;
            DimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE1D;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            HeightLimit = (UINT)WidthLimit;
            DimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
            break;
        default:
            WidthLimit = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
            HeightLimit = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
            DepthLimit = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
            DimensionSupport = D3D12_FORMAT_SUPPORT1_TEXTURE3D;
            break;
    }
    if (Desc.Width > WidthLimit || Desc.Height > HeightLimit || Desc.DepthOrArraySize > DepthLimit)
        return E_INVALIDARG;

    /* A longer mip chain than the size allows is only warned about */
    Largest = Desc.Width;
    if (Desc.Height > Largest)
        Largest = Desc.Height;
    if (RxLayoutDepth(Desc) > Largest)
        Largest = RxLayoutDepth(Desc);
    Mips = Desc.MipLevels != 0 ? Desc.MipLevels : RxFullMipCount(Largest);

    if (RxIsFeedbackFormat(Desc.Format) && Caps.SamplerFeedbackTier == 0)
        return E_INVALIDARG;
    if (FAILED(FormatResult) || (Support.Support1 & DimensionSupport) == 0)
        return E_INVALIDARG;

    Layout = RxFormatLayout(Desc.Format);
    if (!RxIsBlockCompressedFormat(Desc.Format) || !Caps.UnalignedBlocks)
    {
        if (Desc.Width % Layout->WidthAlign != 0 || Desc.Height % Layout->HeightAlign != 0)
            return E_INVALIDARG;
    }

    if (Mips > 1 && (Support.Support1 & D3D12_FORMAT_SUPPORT1_MIP) == 0)
        return E_INVALIDARG;
    if (Desc.SampleDesc.Count == 0)
        return E_INVALIDARG;
    if (Desc.SampleDesc.Count != 1)
    {
        hr = RxCheckSampling(Device, Desc, Mips);
        if (FAILED(hr))
            return hr;
    }

    hr = RxCheckTextureLayout(Device, Desc);
    if (FAILED(hr))
        return hr;

    hr = RxCheckAccessFlags(Device, Desc, Clear);
    if (FAILED(hr))
        return hr;

    if (Clear != nullptr && (Desc.Flags & D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY) == 0)
    {
        hr = RxCheckClearValue(Device, Desc, *Clear);
        if (FAILED(hr))
            return hr;
    }

    return RxCheckFeedbackRegion(Desc);
}

HRESULT
RxValidateCommittedHeap(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_PROPERTIES& Properties,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ UINT NodeIndex)
{
    const RxValidationCaps12& Caps = Device.GetValidationCaps();
    UINT Flags = HeapFlags;
    UINT Creation = Properties.CreationNodeMask != 0 ? Properties.CreationNodeMask : 1;
    UINT Visible = Properties.VisibleNodeMask != 0 ? Properties.VisibleNodeMask : 1;
    BOOLEAN CpuVisible;

    if ((INT)Properties.Type <= 0 || (INT)Properties.Type > D3D12_HEAP_TYPE_GPU_UPLOAD)
        return E_INVALIDARG;
    if (Properties.Type == D3D12_HEAP_TYPE_GPU_UPLOAD && !Caps.GpuUploadHeaps)
        return E_INVALIDARG;
    if ((UINT)Properties.CPUPageProperty > D3D12_CPU_PAGE_PROPERTY_WRITE_BACK ||
        (UINT)Properties.MemoryPoolPreference > D3D12_MEMORY_POOL_L1)
    {
        return E_INVALIDARG;
    }
    if ((Flags & ~RxKnownHeapFlags) != 0)
        return E_INVALIDARG;

    if (RxIsCoreLevel(Device.GetFeatureLevel()))
    {
        constexpr UINT NotOnCore = D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_HARDWARE_PROTECTED |
                                   D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS;

        if ((Flags & NotOnCore) != 0 || Properties.Type == D3D12_HEAP_TYPE_CUSTOM ||
            Properties.Type == D3D12_HEAP_TYPE_GPU_UPLOAD)
        {
            return E_INVALIDARG;
        }
        if ((Flags & D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH) != 0 && !Caps.ComputeWriteWatch)
            return E_INVALIDARG;
        if ((Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) != 0 && (Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) == 0)
            return E_INVALIDARG;
    }

    if ((Flags & D3D12_HEAP_FLAG_TOOLS_USE_MANUAL_WRITE_TRACKING) != 0 && !Caps.ManualWriteTracking)
        return E_INVALIDARG;

    if (Properties.Type == D3D12_HEAP_TYPE_CUSTOM)
    {
        if (Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_UNKNOWN ||
            Properties.MemoryPoolPreference == D3D12_MEMORY_POOL_UNKNOWN)
        {
            return E_INVALIDARG;
        }
        if (Properties.MemoryPoolPreference == D3D12_MEMORY_POOL_L1)
        {
            if (Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_BACK || Device.IsNodeUma(NodeIndex))
                return E_INVALIDARG;
            if (Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE && !Caps.GpuUploadHeaps)
                return E_INVALIDARG;
        }
        CpuVisible = Properties.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
    }
    else
    {
        if (Properties.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_UNKNOWN ||
            Properties.MemoryPoolPreference != D3D12_MEMORY_POOL_UNKNOWN)
        {
            return E_INVALIDARG;
        }
        CpuVisible = Properties.Type != D3D12_HEAP_TYPE_DEFAULT;
    }

    /* Shared CPU heaps go through OpenExistingHeap; write watch needs CPU access */
    if (CpuVisible ? (Flags & D3D12_HEAP_FLAG_SHARED) != 0 : (Flags & D3D12_HEAP_FLAG_ALLOW_WRITE_WATCH) != 0)
        return E_INVALIDARG;

    /* A committed resource's heap gets the deny flags of its kind on its own */
    if ((Flags & RxDenyKinds) != 0)
        return E_INVALIDARG;
    if ((Creation & Visible) != Creation)
        return E_INVALIDARG;

    if ((Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) != 0)
    {
        if ((Flags & D3D12_HEAP_FLAG_SHARED) == 0 || CpuVisible && Properties.Type != D3D12_HEAP_TYPE_CUSTOM)
            return E_INVALIDARG;
        if (Properties.Type == D3D12_HEAP_TYPE_CUSTOM &&
            (Properties.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
             Properties.MemoryPoolPreference != D3D12_MEMORY_POOL_L0))
        {
            return E_INVALIDARG;
        }
        return S_OK;
    }

    if ((Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) != 0)
    {
        if (Properties.Type != D3D12_HEAP_TYPE_DEFAULT && Properties.Type != D3D12_HEAP_TYPE_CUSTOM)
            return E_INVALIDARG;
        if (Properties.Type == D3D12_HEAP_TYPE_CUSTOM &&
            (Properties.CPUPageProperty != D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE ||
             Properties.MemoryPoolPreference != (Device.IsNodeUma(NodeIndex) ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1)))
        {
            return E_INVALIDARG;
        }
        return S_OK;
    }

    /* Shader atomics against CPU visible memory only work on custom heaps */
    if ((Flags & D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS) != 0 && Properties.Type != D3D12_HEAP_TYPE_CUSTOM && CpuVisible)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT
RxValidateHeapPlacement(
    _In_ RxDevice12& Device,
    _In_ D3D12_HEAP_TYPE Type,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear)
{
    BOOLEAN CrossAdapter = (HeapFlags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) != 0;
    BOOLEAN CpuHeap = Type == D3D12_HEAP_TYPE_UPLOAD || Type == D3D12_HEAP_TYPE_READBACK;
    BOOLEAN PlanarOrYuv = FALSE;
    HRESULT hr;

    hr = RxValidateResourceDesc(Device, Desc, Clear);
    if (FAILED(hr))
        return hr;

    if (CrossAdapter)
    {
        if (Desc.Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR && Desc.Layout != D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE)
            return E_INVALIDARG;
        if (Desc.MipLevels != 1 || RxFormatIsYuv(Desc.Format))
            return E_INVALIDARG;
        PlanarOrYuv = RxFormatIsPlanar(Desc.Format);
        if (PlanarOrYuv)
            return E_INVALIDARG;
    }
    if (CrossAdapter != ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) != 0))
        return E_INVALIDARG;

    if (Desc.Layout == D3D12_TEXTURE_LAYOUT_64KB_STANDARD_SWIZZLE && !Device.GetValidationCaps().StandardSwizzle)
    {
        if (!CrossAdapter)
            return E_INVALIDARG;
        if ((Desc.Flags & (RxTargetOrDepth | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0)
            return E_INVALIDARG;
    }

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (CpuHeap &&
            (Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)) != 0)
        {
            return E_INVALIDARG;
        }
        if ((HeapFlags & D3D12_HEAP_FLAG_DENY_BUFFERS) != 0)
            return E_INVALIDARG;
    }
    else
    {
        if (CpuHeap)
            return E_INVALIDARG;
        if ((Desc.Flags & RxTargetOrDepth) != 0 ? (HeapFlags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) != 0
                                                : (HeapFlags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES) != 0)
        {
            return E_INVALIDARG;
        }
        if (Desc.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR &&
            (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || !CrossAdapter))
        {
            return E_INVALIDARG;
        }
    }

    if ((HeapFlags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) != 0)
    {
        BOOLEAN LayoutFits = Desc.Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN ||
                             (CrossAdapter && Desc.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR);

        if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || Desc.Alignment != 0 ||
            Desc.DepthOrArraySize == 0 || Desc.DepthOrArraySize > 2 || Desc.MipLevels != 1 ||
            Desc.SampleDesc.Count != 1 || Desc.SampleDesc.Quality != 0 || !LayoutFits ||
            (Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | RxVideoReference)) != 0)
        {
            return E_INVALIDARG;
        }
    }

    return S_OK;
}

/* The state bits a resource state may use on this device */
static
UINT
RxKnownStates(
    _In_ const RxValidationCaps12& Caps)
{
    UINT Known = 0xAF3FFF;

    if (Caps.ShadingRateTier != 0)
        Known |= D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    if (Caps.RaytracingTier != 0)
        Known |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
    return Known;
}

HRESULT
RxValidateInitialState(
    _In_ RxDevice12& Device,
    _In_ D3D12_RESOURCE_STATES InitialState,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ D3D12_HEAP_TYPE HeapType)
{
    constexpr UINT WriteStates = 0xCA151C;
    constexpr UINT ReadStates = 0x1252AE3;
    constexpr UINT BufferOnly = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_STREAM_OUT;
    constexpr UINT DepthStates = D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_DEPTH_READ;
    constexpr UINT ShaderStates = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const RxValidationCaps12& Caps = Device.GetValidationCaps();
    BOOLEAN Feedback = Caps.SamplerFeedbackTier != 0;
    BOOLEAN Buffer = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    UINT State = InitialState;
    UINT Flags = Desc.Flags;
    UINT Writes;
    UINT Needed = 0;
    UINT Barred = 0;
    BOOLEAN NeedsBuffer = FALSE;
    BOOLEAN NeedsTexture = FALSE;

    /* Buffers and simultaneous access textures outside CPU heaps start out common, whatever is asked */
    if (State != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE && HeapType != D3D12_HEAP_TYPE_UPLOAD &&
        HeapType != D3D12_HEAP_TYPE_READBACK && (Buffer || (Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0))
    {
        State = D3D12_RESOURCE_STATE_COMMON;
    }

    if ((State & ~RxKnownStates(Caps)) != 0)
        return E_INVALIDARG;

    /* Several bits only combine as reads */
    Writes = State & WriteStates;
    if ((State & (State - 1)) != 0 && ((Writes & (Writes - 1)) != 0 || (Writes != 0 && (State & ReadStates) != 0)))
        return E_INVALIDARG;

    if (State != D3D12_RESOURCE_STATE_GENERIC_READ)
    {
        if ((State & ShaderStates) != 0)
            Barred = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        if ((State & D3D12_RESOURCE_STATE_UNORDERED_ACCESS) != 0)
            Needed = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        NeedsBuffer = (State & BufferOnly) != 0;
        if (State == D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE)
        {
            NeedsBuffer = TRUE;
            Needed = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            Barred = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }
    }

    if (State != D3D12_RESOURCE_STATE_COMMON)
    {
        if (HeapType == D3D12_HEAP_TYPE_UPLOAD)
        {
            UINT Allowed = D3D12_RESOURCE_STATE_GENERIC_READ;

            if (Feedback)
                Allowed |= D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
            if ((State & ~Allowed) != 0)
                return E_INVALIDARG;
        }
        else if (HeapType == D3D12_HEAP_TYPE_READBACK)
        {
            if (State != D3D12_RESOURCE_STATE_COPY_DEST && !(Feedback && State == D3D12_RESOURCE_STATE_RESOLVE_DEST))
                return E_INVALIDARG;
        }
    }

    if ((State & D3D12_RESOURCE_STATE_RESOLVE_SOURCE) != 0)
        NeedsTexture = !Feedback;
    if ((State & DepthStates) != 0)
        NeedsTexture = TRUE;
    if ((State & D3D12_RESOURCE_STATE_RESOLVE_DEST) != 0)
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 Options2 = {};

        if (!Feedback)
            NeedsTexture = TRUE;
        Device.CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS2, &Options2, sizeof(Options2));
        if (Options2.ProgrammableSamplePositionsTier == 0 && Desc.SampleDesc.Count != 1)
            return E_INVALIDARG;
    }

    if ((State & D3D12_RESOURCE_STATE_RENDER_TARGET) != 0)
        Needed |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if ((State & DepthStates) != 0)
        Needed |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    if ((Flags & Needed) != Needed || (Flags & Barred) != 0)
        return E_INVALIDARG;

    if ((NeedsBuffer && !Buffer) || (NeedsTexture && Buffer))
        return E_INVALIDARG;

    /* Video references only sit in video and copy states */
    if ((Flags & D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY) != 0 && (State & ~0x30C00u) != 0)
        return E_INVALIDARG;
    if ((Flags & D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY) != 0 && (State & ~0xAC0C00u) != 0)
        return E_INVALIDARG;

    return S_OK;
}
