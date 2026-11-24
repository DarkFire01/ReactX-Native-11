/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader resource, unordered access and depth stencil views and their checks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatlayout12.hpp"
#include "formatsupport12.hpp"

/* What a view is made for, as the planar format rules name it */
enum RxViewUsage
{
    RxUsageShaderResource = 0x08,
    RxUsageRenderTarget = 0x20,
    RxUsageUnorderedAccess = 0x80,
};

/* The facts of a resource a view is checked against, or the stand in for a view without one */
struct RxViewTarget
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Width;
    UINT ArraySize;
    UINT MipLevels;
    DXGI_FORMAT Format;
    UINT SampleCount;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;
};

/* Format facts */

static
BOOLEAN
RxViewFormatKnown(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutHidden) == 0;
}

static
BOOLEAN
RxFormatFullyTyped(
    _In_ DXGI_FORMAT Format)
{
    return Format != DXGI_FORMAT_UNKNOWN && RxViewFormatKnown(Format) && !RxFormatIsTypeless(Format);
}

static
BOOLEAN
RxFormatFloat(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R32G32B32_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R16G16_FLOAT:
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_R16_FLOAT:
        case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
            return TRUE;
        default:
            return FALSE;
    }
}

static
BOOLEAN
RxFormatPlanarOrVideo(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & (RxLayoutPlanar | RxLayoutYuv)) != 0;
}

static
BOOLEAN
RxFormatVideo(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutYuv) != 0;
}

static
UINT
RxFormatBits(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr ? Layout->BitsPerUnit : 0;
}

/* Drivers of the DDI builds this runtime speaks all report the relaxed view casting of the RS3 format set */
static constexpr BOOLEAN RxRelaxedViewCasting = TRUE;

/*
 * Whether a view may read the resource's format as the view's. A fully typed resource casts within its family
 * as long as float and non float readings stay apart; a typeless one needs a typed member of its family.
 * An unknown view format takes the resource's own.
 */
static
BOOLEAN
RxViewFormatFits(
    _In_ DXGI_FORMAT ResourceFormat,
    _Inout_ DXGI_FORMAT* ViewFormat)
{
    const DXGI_FORMAT View = *ViewFormat;

    if (RxFormatFullyTyped(ResourceFormat))
    {
        if (View == DXGI_FORMAT_UNKNOWN || View == ResourceFormat)
        {
            *ViewFormat = ResourceFormat;
            return TRUE;
        }
        if (!RxRelaxedViewCasting || RxFormatFamily(ResourceFormat) != RxFormatFamily(View) ||
            RxFormatIsTypeless(View))
        {
            return FALSE;
        }
        return !RxFormatFullyTyped(View) || RxFormatFloat(View) == RxFormatFloat(ResourceFormat);
    }

    if (View == DXGI_FORMAT_UNKNOWN)
        return FALSE;
    return RxFormatFamily(View) == ResourceFormat && !RxFormatIsTypeless(View);
}

/* One view format of a planar or video family: the usages it serves and the planes it may name */
struct RxPlaneView
{
    UINT8 Family;
    UINT8 View;
    UINT8 Usages;
    UINT8 Planes;
};

#define RX_ANY_PLANE 0xFF
#define RX_PLANES_0_TO_2 0xFE

static const RxPlaneView RxPlaneViews[] =
{
    { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 0x28, RX_ANY_PLANE },
    { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, 0x28, 0 },
    { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 0x28, 1 },
    { DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, 0x28, RX_ANY_PLANE },
    { DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, 0x28, 0 },
    { DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT, 0x28, 1 },
    { DXGI_FORMAT_AYUV, DXGI_FORMAT_R8G8B8A8_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_AYUV, DXGI_FORMAT_R8G8B8A8_UINT, 0xA8, 0 },
    { DXGI_FORMAT_AYUV, DXGI_FORMAT_R32_UINT, 0x80, 0 },
    { DXGI_FORMAT_Y410, DXGI_FORMAT_R10G10B10A2_UNORM, 0x88, 0 },
    { DXGI_FORMAT_Y410, DXGI_FORMAT_R10G10B10A2_UINT, 0x88, 0 },
    { DXGI_FORMAT_Y410, DXGI_FORMAT_R32_UINT, 0x80, 0 },
    { DXGI_FORMAT_Y416, DXGI_FORMAT_R16G16B16A16_UNORM, 0x88, 0 },
    { DXGI_FORMAT_Y416, DXGI_FORMAT_R16G16B16A16_UINT, 0x88, 0 },
    { DXGI_FORMAT_Y210, DXGI_FORMAT_R16G16B16A16_UNORM, 0x88, 0 },
    { DXGI_FORMAT_Y210, DXGI_FORMAT_R16G16B16A16_UINT, 0x88, 0 },
    { DXGI_FORMAT_Y216, DXGI_FORMAT_R16G16B16A16_UNORM, 0x88, 0 },
    { DXGI_FORMAT_Y216, DXGI_FORMAT_R16G16B16A16_UINT, 0x88, 0 },
    { DXGI_FORMAT_YUY2, DXGI_FORMAT_R8G8B8A8_UNORM, 0x88, 0 },
    { DXGI_FORMAT_YUY2, DXGI_FORMAT_R8G8B8A8_UINT, 0x88, 0 },
    { DXGI_FORMAT_YUY2, DXGI_FORMAT_R8G8_B8G8_UNORM, 0x88, 0 },
    { DXGI_FORMAT_YUY2, DXGI_FORMAT_R32_UINT, 0x80, 0 },
    { DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_NV12, DXGI_FORMAT_R8_UINT, 0xA8, 0 },
    { DXGI_FORMAT_NV12, DXGI_FORMAT_R8G8_UNORM, 0xA8, 1 },
    { DXGI_FORMAT_NV12, DXGI_FORMAT_R8G8_UINT, 0xA8, 1 },
    { DXGI_FORMAT_NV11, DXGI_FORMAT_R8_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_NV11, DXGI_FORMAT_R8_UINT, 0xA8, 0 },
    { DXGI_FORMAT_NV11, DXGI_FORMAT_R8G8_UNORM, 0xA8, 1 },
    { DXGI_FORMAT_NV11, DXGI_FORMAT_R8G8_UINT, 0xA8, 1 },
    { DXGI_FORMAT_P208, DXGI_FORMAT_R8_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_P208, DXGI_FORMAT_R8_UINT, 0xA8, 0 },
    { DXGI_FORMAT_P208, DXGI_FORMAT_R8G8_UNORM, 0xA8, 1 },
    { DXGI_FORMAT_P208, DXGI_FORMAT_R8G8_UINT, 0xA8, 1 },
    { DXGI_FORMAT_P010, DXGI_FORMAT_R16_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_P010, DXGI_FORMAT_R16_UINT, 0xA8, 0 },
    { DXGI_FORMAT_P010, DXGI_FORMAT_R16G16_UNORM, 0xA8, 1 },
    { DXGI_FORMAT_P010, DXGI_FORMAT_R16G16_UINT, 0xA8, 1 },
    { DXGI_FORMAT_P010, DXGI_FORMAT_R32_UINT, 0x80, 1 },
    { DXGI_FORMAT_P016, DXGI_FORMAT_R16_UNORM, 0xA8, 0 },
    { DXGI_FORMAT_P016, DXGI_FORMAT_R16_UINT, 0xA8, 0 },
    { DXGI_FORMAT_P016, DXGI_FORMAT_R16G16_UNORM, 0xA8, 1 },
    { DXGI_FORMAT_P016, DXGI_FORMAT_R16G16_UINT, 0xA8, 1 },
    { DXGI_FORMAT_P016, DXGI_FORMAT_R32_UINT, 0x80, 1 },
    { DXGI_FORMAT_V208, DXGI_FORMAT_R8_UNORM, 0xA8, RX_PLANES_0_TO_2 },
    { DXGI_FORMAT_V208, DXGI_FORMAT_R8_UINT, 0xA8, RX_PLANES_0_TO_2 },
    { DXGI_FORMAT_V408, DXGI_FORMAT_R8_UNORM, 0xA8, RX_PLANES_0_TO_2 },
    { DXGI_FORMAT_V408, DXGI_FORMAT_R8_UINT, 0xA8, RX_PLANES_0_TO_2 },
};

/*
 * The plane a view reads when its desc names none. Depth stencil families and the two plane video
 * families tell it from the view format.
 */
static
UINT
RxImpliedPlane(
    _In_ DXGI_FORMAT ResourceFormat,
    _In_ DXGI_FORMAT ViewFormat)
{
    switch (RxFormatFamily(ResourceFormat))
    {
        case DXGI_FORMAT_R32G8X24_TYPELESS:
            return ViewFormat != DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R24G8_TYPELESS:
            return ViewFormat != DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_P208:
            return ViewFormat == DXGI_FORMAT_R8G8_UNORM || ViewFormat == DXGI_FORMAT_R8G8_UINT;
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            return ViewFormat != DXGI_FORMAT_R16_UINT && (UINT)ViewFormat <= DXGI_FORMAT_R32_UINT;
        default:
            return 0;
    }
}

/* Whether a view format and plane fit a planar or video resource for one usage */
static
BOOLEAN
RxPlaneViewFits(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT ResourceFormat,
    _In_ DXGI_FORMAT ViewFormat,
    _In_ RxViewUsage Usage,
    _In_ UINT Plane)
{
    const DXGI_FORMAT Family = RxFormatFamily(ResourceFormat);
    const RxPlaneView* Match = nullptr;
    UINT Usages = 0;
    BOOLEAN PlaneFits = FALSE;
    UINT Index;

    for (Index = 0; Index < _countof(RxPlaneViews); Index++)
    {
        if (RxPlaneViews[Index].Family != Family)
            continue;

        Usages |= RxPlaneViews[Index].Usages;
        if (RxPlaneViews[Index].View == ViewFormat && (RxPlaneViews[Index].Usages & Usage) != 0)
            Match = &RxPlaneViews[Index];
    }

    if (Match != nullptr)
    {
        if (Match->Planes == RX_ANY_PLANE)
            PlaneFits = TRUE;
        else if (Match->Planes == RX_PLANES_0_TO_2)
            PlaneFits = Plane < 3;
        else
            PlaneFits = Plane == Match->Planes;
    }

    /* A typed depth stencil format only views as a member of its own family */
    if (Match != nullptr && (Family == DXGI_FORMAT_R32G8X24_TYPELESS || Family == DXGI_FORMAT_R24G8_TYPELESS) &&
        !RxRelaxedViewCasting && ResourceFormat != Family && ResourceFormat != ViewFormat)
    {
        Match = nullptr;
    }

    if (Level < D3D_FEATURE_LEVEL_10_0)
    {
        Usages = 0;
        Match = nullptr;
    }
    else if (Level < D3D_FEATURE_LEVEL_11_0)
    {
        if (Usage == RxUsageUnorderedAccess)
            Match = nullptr;
        Usages &= ~RxUsageUnorderedAccess;
    }

    return (Usages & Usage) != 0 && Match != nullptr && PlaneFits;
}

/* Mip and slice windows: a start inside the resource and a count that fits, or -1 for the rest */
static
BOOLEAN
RxWindowFits(
    _In_ UINT First,
    _In_ UINT Count,
    _In_ UINT Total)
{
    if (First >= Total || Count == 0)
        return FALSE;
    return Count == (UINT)-1 || (Count <= Total && Count + First <= Total);
}

/* Buffer elements: the first inside the buffer, a nonzero count, and the end within the byte width */
static
BOOLEAN
RxElementsFit(
    _In_ UINT64 Width,
    _In_ UINT64 First,
    _In_ UINT Count,
    _In_ UINT Bits)
{
    const UINT64 Limit = 8ull * ((0xFFFFFFFFu / Bits) & 0x1FFFFFFF);
    UINT64 End;

    if (First > Limit || Width <= ((UINT64)Bits * First) >> 3 || Count == 0)
        return FALSE;

    End = First + Count;
    if (End < Count || End > Limit)
        return FALSE;
    return End * Bits <= 8 * Width;
}

static
BOOLEAN
RxCapMissing(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format,
    _In_ RxFormatCap12 Cap)
{
    return RxFormatCapRule12(Level, Format, Cap) == RxRuleNone;
}

/* The driver's FORMAT_SUPPORT1 bits, asked the way the system runtime asks */
static
UINT
RxDriverFormatSupport(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format)
{
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = { Format };

    if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))))
        return 0;
    return Support.Support1;
}

/*
 * A format serves shader resource views when the level promises one of load, sample, compare, mono sample or
 * gather. When it only leaves them to the driver the driver is asked, but its answer never refuses the view.
 */
static
BOOLEAN
RxShaderReadable(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format)
{
    static const RxFormatCap12 Caps[] = { RxCapShaderLoad, RxCapSample, RxCapSampleCompare, RxCapSampleMono, RxCapGather };
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    UINT Driver = 0;

    if (Format == DXGI_FORMAT_UNKNOWN)
        return FALSE;

    for (RxFormatCap12 Cap : Caps)
    {
        RxFormatRule12 Rule = RxFormatCapRule12(Level, Format, Cap);

        if (Rule == RxRuleRequired)
            return TRUE;
        if (Rule == RxRuleDriver)
            Driver += 1;
    }

    if (Driver == 0)
        return FALSE;

    RxDriverFormatSupport(Device, Format);
    return TRUE;
}

static
VOID
RxDescribeTarget(
    _In_opt_ RxResource* Resource,
    _Out_ RxViewTarget* Target)
{
    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();

    Target->Dimension = Desc.Dimension;
    Target->Width = Desc.Width;
    Target->ArraySize = Desc.DepthOrArraySize;
    Target->MipLevels = Desc.MipLevels;
    Target->Format = Desc.Format;
    Target->SampleCount = Desc.SampleDesc.Count;
    Target->Layout = Desc.Layout;
    Target->Flags = Desc.Flags;
}

/* Shader resource views */

static
BOOLEAN
RxComponentMappingValid(
    _In_ UINT Mapping)
{
    UINT Index;

    if ((Mapping & ~0x1FFFu) != 0 || (Mapping & D3D12_SHADER_COMPONENT_MAPPING_ALWAYS_SET_BIT_AVOIDING_ZEROMEM_MISTAKES) == 0)
        return FALSE;

    for (Index = 0; Index < 4; Index++)
    {
        if (((Mapping >> (Index * 3)) & 7) > D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1)
            return FALSE;
    }
    return TRUE;
}

/* The resource kind each view dimension reads */
static
D3D12_RESOURCE_DIMENSION
RxSrvResourceKind(
    _In_ D3D12_SRV_DIMENSION Dimension)
{
    switch (Dimension)
    {
        case D3D12_SRV_DIMENSION_BUFFER:
        case D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE:
            return D3D12_RESOURCE_DIMENSION_BUFFER;
        case D3D12_SRV_DIMENSION_TEXTURE1D:
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        default:
            return D3D12_RESOURCE_DIMENSION_UNKNOWN;
    }
}

/* Texture checks shared by every dimension: the format cast, then the mip and slice windows */
static
UINT
RxCheckTextureSrv(
    _In_ RxDevice12& Device,
    _In_ BOOLEAN HasResource,
    _In_ const RxViewTarget& Target,
    _In_ const D3D12_SHADER_RESOURCE_VIEW_DESC& Desc,
    _Inout_ DXGI_FORMAT* ViewFormat)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    UINT Problems = 0;

    if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && RxFormatPlanarOrVideo(Target.Format))
    {
        UINT Plane;

        if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
            Plane = Desc.Texture2D.PlaneSlice;
        else if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
            Plane = Desc.Texture2DArray.PlaneSlice;
        else
            Plane = RxImpliedPlane(Target.Format, *ViewFormat);

        if (!RxPlaneViewFits(Level, Target.Format, *ViewFormat, RxUsageShaderResource, Plane))
            Problems += 1;
    }
    else
    {
        UINT Plane = 0;

        if (!RxViewFormatFits(Target.Format, ViewFormat))
            Problems += 1;

        if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D)
            Plane = Desc.Texture2D.PlaneSlice;
        else if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY)
            Plane = Desc.Texture2DArray.PlaneSlice;
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && Plane != 0)
            Problems += 1;
    }

    switch (Target.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (RxCapMissing(Level, *ViewFormat, RxCapTexture1D))
                Problems += 1;
            if (!HasResource)
                break;
            if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE1DARRAY)
            {
                if (!RxWindowFits(Desc.Texture1DArray.MostDetailedMip, Desc.Texture1DArray.MipLevels, Target.MipLevels) ||
                    !RxWindowFits(Desc.Texture1DArray.FirstArraySlice, Desc.Texture1DArray.ArraySize, Target.ArraySize))
                {
                    Problems += 1;
                }
            }
            else if (!RxWindowFits(Desc.Texture1D.MostDetailedMip, Desc.Texture1D.MipLevels, Target.MipLevels))
            {
                Problems += 1;
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (RxCapMissing(Level, *ViewFormat, RxCapTexture3D))
                Problems += 1;
            if (HasResource && !RxWindowFits(Desc.Texture3D.MostDetailedMip, Desc.Texture3D.MipLevels, Target.MipLevels))
                Problems += 1;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        {
            BOOLEAN CubeCapChecked = FALSE;

            if (HasResource)
            {
                switch (Desc.ViewDimension)
                {
                    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
                        if (!RxWindowFits(Desc.Texture2DArray.MostDetailedMip, Desc.Texture2DArray.MipLevels,
                                          Target.MipLevels) ||
                            !RxWindowFits(Desc.Texture2DArray.FirstArraySlice, Desc.Texture2DArray.ArraySize,
                                          Target.ArraySize))
                        {
                            Problems += 1;
                        }
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
                        if (!RxWindowFits(Desc.Texture2DMSArray.FirstArraySlice, Desc.Texture2DMSArray.ArraySize,
                                          Target.ArraySize))
                        {
                            Problems += 1;
                        }
                        break;
                    case D3D12_SRV_DIMENSION_TEXTURE2D:
                    case D3D12_SRV_DIMENSION_TEXTURECUBE:
                        if (!RxWindowFits(Desc.Texture2D.MostDetailedMip, Desc.Texture2D.MipLevels, Target.MipLevels))
                            Problems += 1;
                        break;
                    default:
                        break;
                }
            }

            /* Cube arrays are checked with or without a resource */
            if (Desc.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY && HasResource)
            {
                const D3D12_TEXCUBE_ARRAY_SRV& Cube = Desc.TextureCubeArray;

                if (Level < D3D_FEATURE_LEVEL_10_1)
                {
                    Problems += 1;
                }
                else
                {
                    if (Target.ArraySize < 6)
                    {
                        Problems += 1;
                    }
                    else
                    {
                        if (Cube.First2DArrayFace > Target.ArraySize - 6)
                            Problems += 1;
                        else if (Cube.NumCubes == 0 ||
                                 (Cube.NumCubes != (UINT)-1 && (Target.ArraySize < 6 * Cube.NumCubes ||
                                                                Cube.First2DArrayFace + 6 * Cube.NumCubes > Target.ArraySize)))
                            Problems += 1;

                        if (!RxWindowFits(Cube.MostDetailedMip, Cube.MipLevels, Target.MipLevels))
                            Problems += 1;
                    }
                    if (RxCapMissing(Level, *ViewFormat, RxCapTextureCube))
                        Problems += 1;
                    CubeCapChecked = TRUE;
                }
            }

            if (Level >= D3D_FEATURE_LEVEL_10_1)
            {
                if (Desc.ViewDimension != D3D12_SRV_DIMENSION_TEXTURECUBE &&
                    Desc.ViewDimension != D3D12_SRV_DIMENSION_TEXTURECUBEARRAY &&
                    RxCapMissing(Level, *ViewFormat, RxCapTexture2D))
                {
                    Problems += 1;
                }
            }
            else if (RxCapMissing(Level, *ViewFormat, RxCapTexture2D) &&
                     (Level >= D3D_FEATURE_LEVEL_10_0 ||
                      (*ViewFormat != DXGI_FORMAT_R16_UNORM && *ViewFormat != DXGI_FORMAT_R24_UNORM_X8_TYPELESS) ||
                      (Target.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0))
            {
                Problems += 1;
            }
            UNREFERENCED_PARAMETER(CubeCapChecked);

            if (Target.SampleCount > 1)
            {
                if (Desc.ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DMS &&
                    Desc.ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY)
                {
                    Problems += 1;
                }
                if (Level < D3D_FEATURE_LEVEL_10_1 && (Target.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
                    Problems += 1;
            }
            break;
        }

        default:
            break;
    }

    return Problems;
}

/* Buffer views: typed, structured or raw, each with its own element size */
static
HRESULT
RxCheckBufferSrv(
    _In_ RxDevice12& Device,
    _In_opt_ RxResource* Resource,
    _In_ const RxViewTarget& Target,
    _In_ const D3D12_SHADER_RESOURCE_VIEW_DESC& Desc,
    _Inout_ UINT* Problems,
    _Out_ BOOLEAN* CheckReadable)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    const D3D12_BUFFER_SRV& Buffer = Desc.Buffer;
    BOOLEAN Typed = TRUE;
    UINT Bits;

    *CheckReadable = FALSE;

    if (Buffer.StructureByteStride != 0)
    {
        if (Desc.Format != DXGI_FORMAT_UNKNOWN)
            *Problems += 1;
        if (Buffer.Flags != D3D12_BUFFER_SRV_FLAG_NONE)
            *Problems += 1;
        if (Desc.Shader4ComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)
            *Problems += 1;
        if (Resource != nullptr && !RxElementsFit(Target.Width, Buffer.FirstElement, Buffer.NumElements,
                                                  8 * Buffer.StructureByteStride))
        {
            *Problems += 1;
        }
        return S_OK;
    }

    if ((Buffer.Flags & D3D12_BUFFER_SRV_FLAG_RAW) != 0)
    {
        if (Desc.Format != DXGI_FORMAT_R32_TYPELESS)
            return E_INVALIDARG;

        if (Level != D3D_FEATURE_LEVEL_1_0_CORE && Level != D3D_FEATURE_LEVEL_1_0_GENERIC)
        {
            if (Resource != nullptr && (Resource->GetPlacementOffset() & 0xF) != 0)
                *Problems += 1;
            if ((Buffer.FirstElement & 3) != 0)
                *Problems += 1;
        }
        if (Desc.Shader4ComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)
            *Problems += 1;
        Typed = FALSE;
    }
    else if (Buffer.Flags != D3D12_BUFFER_SRV_FLAG_NONE)
    {
        *Problems += 1;
    }
    else
    {
        RxFormatRule12 Rule = RxFormatCapRule12(Level, Desc.Format, RxCapBuffer);

        if (Level == D3D_FEATURE_LEVEL_1_0_CORE)
            *Problems += 1;
        if (Rule == RxRuleNone || Desc.Format == DXGI_FORMAT_UNKNOWN)
            return E_INVALIDARG;
        if (Rule == RxRuleDriver && (RxDriverFormatSupport(Device, Desc.Format) & D3D12_FORMAT_SUPPORT1_BUFFER) == 0)
            return E_INVALIDARG;
        if (RxFormatIsTypeless(Desc.Format))
            *Problems += 1;
    }

    Bits = RxFormatBits(Desc.Format);
    if (Resource != nullptr && Bits != 0 && !RxElementsFit(Target.Width, Buffer.FirstElement, Buffer.NumElements, Bits))
        *Problems += 1;

    if (Target.Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE &&
        RxFormatCapRule12(Level, Desc.Format, RxCapBuffer) == RxRuleNone)
    {
        *Problems += 1;
    }

    *CheckReadable = Typed;
    return S_OK;
}

/* The checks the system runtime makes before a shader resource view reaches the driver */
static
BOOLEAN
RxShaderResourceViewValid(
    _In_ RxDevice12& Device,
    _In_opt_ RxResource* Resource,
    _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    RxViewTarget Target = {};
    DXGI_FORMAT ViewFormat;
    BOOLEAN CheckReadable = TRUE;
    UINT Problems = 0;

    if (pDesc != nullptr && !RxViewFormatKnown(pDesc->Format))
        return FALSE;

    if (Resource != nullptr)
    {
        RxDescribeTarget(Resource, &Target);
    }
    else
    {
        if (pDesc == nullptr)
            return FALSE;

        /* Without a resource the view stands for a one mip, one slice resource of its own kind */
        Target.Dimension = RxSrvResourceKind(pDesc->ViewDimension);
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
            return FALSE;
        Target.Width = 1;
        Target.ArraySize = 1;
        Target.MipLevels = 1;
        Target.Format = pDesc->Format;
        Target.SampleCount = (pDesc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DMS ||
                              pDesc->ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY) ? 2 : 1;
    }

    if (pDesc != nullptr && RxSrvResourceKind(pDesc->ViewDimension) != Target.Dimension)
        return FALSE;

    if (pDesc != nullptr && !RxComponentMappingValid(pDesc->Shader4ComponentMapping))
        Problems += 1;

    if (Target.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (pDesc == nullptr)
            return FALSE;

        if (pDesc->ViewDimension == D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE)
        {
            if (Resource != nullptr || pDesc->Format != DXGI_FORMAT_UNKNOWN)
                return FALSE;
            if (Device.GetOptions().RaytracingTier == D3D12DDI_RAYTRACING_TIER_NOT_SUPPORTED)
                Problems += 1;
            if ((pDesc->RaytracingAccelerationStructure.Location & 0xFF) != 0)
                Problems += 1;
            if (pDesc->Shader4ComponentMapping != D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING)
                Problems += 1;
            CheckReadable = FALSE;
        }
        else if (FAILED(RxCheckBufferSrv(Device, Resource, Target, *pDesc, &Problems, &CheckReadable)))
        {
            return FALSE;
        }
        ViewFormat = pDesc->Format;
    }
    else if (pDesc == nullptr)
    {
        /* Defaults take the resource's format, which must be typed and not video */
        if (RxFormatIsTypeless(Target.Format))
            Problems += 1;
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && RxFormatVideo(Target.Format))
            Problems += 1;
        ViewFormat = Target.Format;
    }
    else
    {
        ViewFormat = pDesc->Format;
        Problems += RxCheckTextureSrv(Device, Resource != nullptr, Target, *pDesc, &ViewFormat);
    }

    if (CheckReadable && !RxShaderReadable(Device, ViewFormat) &&
        (Level >= D3D_FEATURE_LEVEL_10_0 ||
         (ViewFormat != DXGI_FORMAT_R16_UNORM && ViewFormat != DXGI_FORMAT_R24_UNORM_X8_TYPELESS) ||
         (Target.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0))
    {
        Problems += 1;
    }

    if ((Target.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) != 0)
        Problems += 1;

    return Problems == 0;
}

/* The view a NULL desc stands for */
static
VOID
RxDefaultSrvDesc(
    _In_opt_ RxResource* Resource,
    _Out_ D3D12_SHADER_RESOURCE_VIEW_DESC* View)
{
    ZeroMemory(View, sizeof(*View));
    View->Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (Resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();

    View->Format = Desc.Format;
    switch (Desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            View->Format = DXGI_FORMAT_UNKNOWN;
            View->ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            View->Buffer.NumElements = (UINT)Desc.Width;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE1DARRAY
                                                            : D3D12_SRV_DIMENSION_TEXTURE1D;
            View->Texture1DArray.MipLevels = Desc.MipLevels;
            View->Texture1DArray.ArraySize = Desc.DepthOrArraySize > 1 ? Desc.DepthOrArraySize : 0;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (Desc.SampleDesc.Count > 1)
            {
                View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY
                                                                : D3D12_SRV_DIMENSION_TEXTURE2DMS;
                if (Desc.DepthOrArraySize > 1)
                    View->Texture2DMSArray.ArraySize = Desc.DepthOrArraySize;
            }
            else if (Desc.DepthOrArraySize > 1)
            {
                View->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                View->Texture2DArray.MipLevels = Desc.MipLevels;
                View->Texture2DArray.ArraySize = Desc.DepthOrArraySize;
            }
            else
            {
                View->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                View->Texture2D.MipLevels = Desc.MipLevels;
            }
            break;
        default:
            View->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            View->Texture3D.MipLevels = Desc.MipLevels;
            break;
    }
}

/* Fills -1 counts and an unknown format from the resource */
static
VOID
RxResolveSrvDesc(
    _In_opt_ RxResource* Resource,
    _Inout_ D3D12_SHADER_RESOURCE_VIEW_DESC* View)
{
    if (Resource == nullptr || View->ViewDimension == D3D12_SRV_DIMENSION_BUFFER)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();
    const UINT Mips = Desc.MipLevels;
    const UINT Slices = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;

    switch (View->ViewDimension)
    {
        case D3D12_SRV_DIMENSION_TEXTURE1D:
        case D3D12_SRV_DIMENSION_TEXTURE2D:
        case D3D12_SRV_DIMENSION_TEXTURE3D:
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            /* These four keep MostDetailedMip and MipLevels at the same place */
            if (View->Texture2D.MipLevels == (UINT)-1)
                View->Texture2D.MipLevels = Mips - View->Texture2D.MostDetailedMip;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            if (View->Texture2DArray.ArraySize == (UINT)-1)
                View->Texture2DArray.ArraySize = Slices - View->Texture2DArray.FirstArraySlice;
            if (View->Texture2DArray.MipLevels == (UINT)-1)
                View->Texture2DArray.MipLevels = Mips - View->Texture2DArray.MostDetailedMip;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
            if (View->Texture2DMSArray.ArraySize == (UINT)-1)
                View->Texture2DMSArray.ArraySize = Slices - View->Texture2DMSArray.FirstArraySlice;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            if (View->TextureCubeArray.MipLevels == (UINT)-1)
                View->TextureCubeArray.MipLevels = Mips - View->TextureCubeArray.MostDetailedMip;
            if (View->TextureCubeArray.NumCubes == (UINT)-1)
                View->TextureCubeArray.NumCubes = (Slices - View->TextureCubeArray.First2DArrayFace) / 6;
            break;
        default:
            break;
    }

    if (View->Format == DXGI_FORMAT_UNKNOWN)
        View->Format = Desc.Format;
}

static
VOID
RxBuildDdiSrv(
    _In_opt_ RxResource* Resource,
    _In_ const D3D12_SHADER_RESOURCE_VIEW_DESC& View,
    _Out_ D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002* Args)
{
    ZeroMemory(Args, sizeof(*Args));
    Args->hDrvResource = Resource != nullptr ? Resource->GetDriverHandle() : D3D12DDI_HRESOURCE{};
    Args->Format = View.Format;
    Args->Shader4ComponentMapping = View.Shader4ComponentMapping;

    switch (View.ViewDimension)
    {
        case D3D12_SRV_DIMENSION_BUFFER:
            Args->ResourceDimension = D3D12DDI_RD_BUFFER;
            Args->Buffer.FirstElement = View.Buffer.FirstElement;
            Args->Buffer.NumElements = View.Buffer.NumElements;
            Args->Buffer.StructureByteStride = View.Buffer.StructureByteStride;
            Args->Buffer.Flags = (D3D12DDI_BUFFER_SRV_FLAGS)View.Buffer.Flags;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex1D.MostDetailedMip = View.Texture1D.MostDetailedMip;
            Args->Tex1D.MipLevels = View.Texture1D.MipLevels;
            Args->Tex1D.ArraySize = 1;
            Args->Tex1D.ResourceMinLODClamp = View.Texture1D.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex1D.MostDetailedMip = View.Texture1DArray.MostDetailedMip;
            Args->Tex1D.MipLevels = View.Texture1DArray.MipLevels;
            Args->Tex1D.FirstArraySlice = View.Texture1DArray.FirstArraySlice;
            Args->Tex1D.ArraySize = View.Texture1DArray.ArraySize;
            Args->Tex1D.ResourceMinLODClamp = View.Texture1DArray.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.MostDetailedMip = View.Texture2D.MostDetailedMip;
            Args->Tex2D.MipLevels = View.Texture2D.MipLevels;
            Args->Tex2D.ArraySize = 1;
            Args->Tex2D.PlaneSlice = View.Texture2D.PlaneSlice;
            Args->Tex2D.ResourceMinLODClamp = View.Texture2D.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.MostDetailedMip = View.Texture2DArray.MostDetailedMip;
            Args->Tex2D.MipLevels = View.Texture2DArray.MipLevels;
            Args->Tex2D.FirstArraySlice = View.Texture2DArray.FirstArraySlice;
            Args->Tex2D.ArraySize = View.Texture2DArray.ArraySize;
            Args->Tex2D.PlaneSlice = View.Texture2DArray.PlaneSlice;
            Args->Tex2D.ResourceMinLODClamp = View.Texture2DArray.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.MipLevels = 1;
            Args->Tex2D.ArraySize = 1;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.FirstArraySlice = View.Texture2DMSArray.FirstArraySlice;
            Args->Tex2D.MipLevels = 1;
            Args->Tex2D.ArraySize = View.Texture2DMSArray.ArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            Args->Tex3D.MostDetailedMip = View.Texture3D.MostDetailedMip;
            Args->Tex3D.MipLevels = View.Texture3D.MipLevels;
            Args->Tex3D.ResourceMinLODClamp = View.Texture3D.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURECUBE;
            Args->TexCube.MostDetailedMip = View.TextureCube.MostDetailedMip;
            Args->TexCube.MipLevels = View.TextureCube.MipLevels;
            Args->TexCube.NumCubes = 1;
            Args->TexCube.ResourceMinLODClamp = View.TextureCube.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURECUBE;
            Args->TexCube.MostDetailedMip = View.TextureCubeArray.MostDetailedMip;
            Args->TexCube.MipLevels = View.TextureCubeArray.MipLevels;
            Args->TexCube.First2DArrayFace = View.TextureCubeArray.First2DArrayFace;
            Args->TexCube.NumCubes = View.TextureCubeArray.NumCubes;
            Args->TexCube.ResourceMinLODClamp = View.TextureCubeArray.ResourceMinLODClamp;
            break;
        case D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE:
            Args->ResourceDimension = D3D12DDI_RD_RAYTRACING_ACCELERATION_STRUCTURE_0042;
            Args->RaytracingAccelerationStructure.Location = View.RaytracingAccelerationStructure.Location;
            break;
        default:
            break;
    }
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateShaderResourceView(
    _In_opt_ ID3D12Resource* pResource,
    _In_opt_ const D3D12_SHADER_RESOURCE_VIEW_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    RxResource* Resource = RxUnwrap12<RxResource>(pResource);
    D3D12_SHADER_RESOURCE_VIEW_DESC View;
    D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 Args;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;

    if (pDesc != nullptr)
    {
        View = *pDesc;
        RxResolveSrvDesc(Resource, &View);
    }
    else
    {
        RxDefaultSrvDesc(Resource, &View);
    }

    if (!RxShaderResourceViewValid(*this, Resource, pDesc))
    {
        RemoveOnBadView();
        return;
    }

    RxBuildDdiSrv(Resource, View, &Args);
    Destination.ptr = DestDescriptor.ptr;
    Ddi().pfnCreateShaderResourceView(DriverDevice, &Args, Destination);
}

/* Unordered access views */

/* Typeless 32 bit layouts a UAV may read as a single 32 bit channel */
static
BOOLEAN
RxCastsToR32Uav(
    _In_ DXGI_FORMAT ResourceFormat,
    _In_ DXGI_FORMAT ViewFormat)
{
    if (ViewFormat != DXGI_FORMAT_R32_FLOAT && ViewFormat != DXGI_FORMAT_R32_UINT && ViewFormat != DXGI_FORMAT_R32_SINT)
        return FALSE;

    switch (ResourceFormat)
    {
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            return TRUE;
        default:
            return FALSE;
    }
}

/* The cast rules of shader resource views, plus the 32 bit single channel reading */
static
BOOLEAN
RxUavFormatFits(
    _In_ DXGI_FORMAT ResourceFormat,
    _Inout_ DXGI_FORMAT* ViewFormat)
{
    const DXGI_FORMAT View = *ViewFormat;

    if (RxFormatFullyTyped(ResourceFormat))
    {
        if (View == DXGI_FORMAT_UNKNOWN)
        {
            *ViewFormat = ResourceFormat;
            return TRUE;
        }
        if (!RxRelaxedViewCasting)
            return View == ResourceFormat || RxCastsToR32Uav(ResourceFormat, View);
        if (!RxCastsToR32Uav(RxFormatFamily(ResourceFormat), View) &&
            (RxFormatFamily(ResourceFormat) != RxFormatFamily(View) || RxFormatIsTypeless(View)))
        {
            return FALSE;
        }
        return !RxFormatFullyTyped(View) || RxFormatFloat(View) == RxFormatFloat(ResourceFormat);
    }

    if (View == DXGI_FORMAT_UNKNOWN)
        return FALSE;
    if (RxFormatFamily(View) == ResourceFormat && !RxFormatIsTypeless(View))
        return TRUE;
    return RxCastsToR32Uav(ResourceFormat, View);
}

/* Typed UAV stores the level promises, or leaves to the driver and the driver confirms */
static
BOOLEAN
RxUavStorable(
    _In_ RxDevice12& Device,
    _In_ DXGI_FORMAT Format)
{
    RxFormatRule12 Rule;

    if (Format == DXGI_FORMAT_UNKNOWN)
        return FALSE;

    if (RxFormatIsTypeless(Format) && RxCastsToR32Uav(Format, DXGI_FORMAT_R32_UINT))
        return TRUE;

    Rule = RxFormatCapRule12(Device.GetFeatureLevel(), Format, RxCapTypedUav);
    if (Rule == RxRuleRequired)
        return TRUE;
    if (Rule != RxRuleDriver)
        return FALSE;
    return (RxDriverFormatSupport(Device, Format) & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0;
}

static
D3D12_RESOURCE_DIMENSION
RxUavResourceKind(
    _In_ D3D12_UAV_DIMENSION Dimension)
{
    switch (Dimension)
    {
        case D3D12_UAV_DIMENSION_BUFFER:
            return D3D12_RESOURCE_DIMENSION_BUFFER;
        case D3D12_UAV_DIMENSION_TEXTURE1D:
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case D3D12_UAV_DIMENSION_TEXTURE2D:
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
        case D3D12_UAV_DIMENSION_TEXTURE2DMS:
        case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        default:
            return D3D12_RESOURCE_DIMENSION_UNKNOWN;
    }
}

/* UAV buffer elements; the element limit here is taken over 64 bits */
static
BOOLEAN
RxUavElementsFit(
    _In_ UINT64 Width,
    _In_ UINT64 First,
    _In_ UINT Count,
    _In_ UINT64 Bits)
{
    const UINT64 Limit = 8 * (~0ull / Bits);
    UINT64 End;

    if (First > Limit || Width <= (Bits * First) >> 3 || Count == 0)
        return FALSE;

    End = First + Count;
    if (End < Count || End > Limit)
        return FALSE;
    return End * Bits <= 8 * Width;
}

/* Buffer UAVs; FALSE when the system runtime gives up on the view at once */
static
BOOLEAN
RxCheckBufferUav(
    _In_ RxDevice12& Device,
    _In_opt_ RxResource* Resource,
    _In_opt_ RxResource* Counter,
    _In_ const RxViewTarget& Target,
    _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    _Inout_ UINT* Problems,
    _Out_ BOOLEAN* CheckStorable)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    const BOOLEAN CoreLevel = Level == D3D_FEATURE_LEVEL_1_0_CORE || Level == D3D_FEATURE_LEVEL_1_0_GENERIC;
    UINT Stride = 0;
    UINT Bits;

    *CheckStorable = FALSE;

    if (pDesc != nullptr)
    {
        if (pDesc->Buffer.CounterOffsetInBytes != 0 && Counter == nullptr)
            *Problems += 1;
        if ((pDesc->Buffer.Flags & ~D3D12_BUFFER_UAV_FLAG_RAW) != 0)
            return FALSE;
        Stride = pDesc->Buffer.StructureByteStride;
    }

    if (Stride != 0)
    {
        if (pDesc->Format != DXGI_FORMAT_UNKNOWN)
            *Problems += 1;
        if ((pDesc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) != 0)
            *Problems += 1;
        if (Resource != nullptr && !RxUavElementsFit(Target.Width, pDesc->Buffer.FirstElement, pDesc->Buffer.NumElements,
                                                     8ull * (Stride & 0x1FFFFFFF)))
        {
            *Problems += 1;
        }
        return TRUE;
    }

    if (pDesc == nullptr || Counter != nullptr)
        return FALSE;

    if ((pDesc->Buffer.Flags & D3D12_BUFFER_UAV_FLAG_RAW) != 0)
    {
        if (pDesc->Format != DXGI_FORMAT_R32_TYPELESS)
            return FALSE;
        if (!CoreLevel)
        {
            if (Resource != nullptr && (Resource->GetPlacementOffset() & 0xF) != 0)
                *Problems += 1;
            if ((pDesc->Buffer.FirstElement & 3) != 0)
                *Problems += 1;
        }
    }
    else
    {
        RxFormatRule12 Rule = RxFormatCapRule12(Level, pDesc->Format, RxCapBuffer);

        if (CoreLevel)
            *Problems += 1;
        if (Rule == RxRuleNone || pDesc->Format == DXGI_FORMAT_UNKNOWN)
            return FALSE;
        if (Rule == RxRuleDriver &&
            (RxDriverFormatSupport(Device, pDesc->Format) & D3D12_FORMAT_SUPPORT1_BUFFER) == 0)
        {
            return FALSE;
        }
        if (RxFormatIsTypeless(pDesc->Format))
            *Problems += 1;
        *CheckStorable = TRUE;
    }

    Bits = RxFormatBits(pDesc->Format);
    if (Resource != nullptr && Bits != 0 &&
        !RxUavElementsFit(Target.Width, pDesc->Buffer.FirstElement, pDesc->Buffer.NumElements, Bits))
    {
        *Problems += 1;
    }
    return TRUE;
}

static
UINT
RxCheckTextureUav(
    _In_ RxDevice12& Device,
    _In_ BOOLEAN HasResource,
    _In_ const RxViewTarget& Target,
    _In_ const D3D12_UNORDERED_ACCESS_VIEW_DESC& Desc,
    _Inout_ DXGI_FORMAT* ViewFormat)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    UINT Problems = 0;

    switch (Target.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            if (!RxUavFormatFits(Target.Format, ViewFormat))
                Problems += 1;
            if (RxCapMissing(Level, *ViewFormat, RxCapTexture1D))
                Problems += 1;
            if (!HasResource)
                break;
            if (Desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE1DARRAY)
            {
                if (Desc.Texture1DArray.MipSlice >= Target.MipLevels ||
                    !RxWindowFits(Desc.Texture1DArray.FirstArraySlice, Desc.Texture1DArray.ArraySize, Target.ArraySize))
                {
                    Problems += 1;
                }
            }
            else if (Desc.Texture1D.MipSlice >= Target.MipLevels)
            {
                Problems += 1;
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (!RxUavFormatFits(Target.Format, ViewFormat))
                Problems += 1;
            if (RxCapMissing(Level, *ViewFormat, RxCapTexture3D))
                Problems += 1;
            if (HasResource)
            {
                UINT Depth = Desc.Texture3D.MipSlice < 32 ? Target.ArraySize >> Desc.Texture3D.MipSlice : 0;

                if (Depth <= 1)
                    Depth = 1;
                if (Desc.Texture3D.MipSlice >= Target.MipLevels ||
                    !RxWindowFits(Desc.Texture3D.FirstWSlice, Desc.Texture3D.WSize, Depth))
                {
                    Problems += 1;
                }
            }
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        {
            UINT Plane;

            if (Desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2D)
                Plane = Desc.Texture2D.PlaneSlice;
            else if (Desc.ViewDimension == D3D12_UAV_DIMENSION_TEXTURE2DARRAY)
                Plane = Desc.Texture2DArray.PlaneSlice;
            else
                Plane = RxImpliedPlane(Target.Format, *ViewFormat);

            if (RxFormatPlanarOrVideo(Target.Format))
            {
                if (!RxPlaneViewFits(Level, Target.Format, *ViewFormat, RxUsageUnorderedAccess, Plane))
                    Problems += 1;
            }
            else
            {
                if (!RxUavFormatFits(Target.Format, ViewFormat))
                    Problems += 1;
                if (Plane != 0)
                    Problems += 1;
            }

            if (RxCapMissing(Level, *ViewFormat, RxCapTexture2D))
                Problems += 1;
            if (!HasResource)
                break;

            switch (Desc.ViewDimension)
            {
                case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
                    if (Desc.Texture2DArray.MipSlice >= Target.MipLevels ||
                        !RxWindowFits(Desc.Texture2DArray.FirstArraySlice, Desc.Texture2DArray.ArraySize, Target.ArraySize))
                    {
                        Problems += 1;
                    }
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
                    if (!RxWindowFits(Desc.Texture2DMSArray.FirstArraySlice, Desc.Texture2DMSArray.ArraySize,
                                      Target.ArraySize))
                    {
                        Problems += 1;
                    }
                    break;
                case D3D12_UAV_DIMENSION_TEXTURE2D:
                    if (Desc.Texture2D.MipSlice >= Target.MipLevels)
                        Problems += 1;
                    break;
                default:
                    break;
            }

            if (Target.SampleCount > 1 && Desc.ViewDimension != D3D12_UAV_DIMENSION_TEXTURE2DMS &&
                Desc.ViewDimension != D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY)
            {
                Problems += 1;
            }
            break;
        }

        default:
            break;
    }

    return Problems;
}

static
BOOLEAN
RxUnorderedAccessViewValid(
    _In_ RxDevice12& Device,
    _In_opt_ RxResource* Resource,
    _In_opt_ RxResource* Counter,
    _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc)
{
    RxViewTarget Target = {};
    DXGI_FORMAT ViewFormat;
    BOOLEAN CheckStorable = TRUE;
    UINT Problems = 0;

    if (pDesc != nullptr && !RxViewFormatKnown(pDesc->Format))
        return FALSE;

    if (Resource != nullptr)
    {
        RxDescribeTarget(Resource, &Target);
    }
    else
    {
        if (pDesc == nullptr)
            return FALSE;

        Target.Dimension = RxUavResourceKind(pDesc->ViewDimension);
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
            return FALSE;
        Target.Width = 1;
        Target.ArraySize = 1;
        Target.MipLevels = 1;
        Target.Format = pDesc->Format;
        Target.SampleCount = 1;
        Target.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    if (pDesc != nullptr && RxUavResourceKind(pDesc->ViewDimension) != Target.Dimension)
        Problems += 1;

    if (Counter != nullptr)
    {
        const D3D12_RESOURCE_DESC1& CounterDesc = Counter->GetApiDesc();

        if (Target.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            Problems += 1;
        if (CounterDesc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            Problems += 1;
        if (Resource == nullptr)
            Problems += 1;
        if (pDesc == nullptr)
            return FALSE;
        if ((pDesc->Buffer.CounterOffsetInBytes & (D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT - 1)) != 0)
            Problems += 1;
        if (pDesc->Buffer.CounterOffsetInBytes + 4 < pDesc->Buffer.CounterOffsetInBytes ||
            pDesc->Buffer.CounterOffsetInBytes + 4 > CounterDesc.Width)
        {
            Problems += 1;
        }
    }

    if (Target.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (!RxCheckBufferUav(Device, Resource, Counter, Target, pDesc, &Problems, &CheckStorable))
            return FALSE;
        ViewFormat = pDesc != nullptr ? pDesc->Format : DXGI_FORMAT_UNKNOWN;
    }
    else if (pDesc == nullptr)
    {
        if (RxFormatIsTypeless(Target.Format))
            Problems += 1;
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && RxFormatVideo(Target.Format))
            Problems += 1;
        ViewFormat = Target.Format;
    }
    else
    {
        ViewFormat = pDesc->Format;
        Problems += RxCheckTextureUav(Device, Resource != nullptr, Target, *pDesc, &ViewFormat);
    }

    if (CheckStorable && !RxUavStorable(Device, ViewFormat))
        Problems += 1;

    if ((Target.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        Problems += 1;
    if (Counter != nullptr && (Counter->GetApiDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
        Problems += 1;

    return Problems == 0;
}

static
VOID
RxDefaultUavDesc(
    _In_opt_ RxResource* Resource,
    _Out_ D3D12_UNORDERED_ACCESS_VIEW_DESC* View)
{
    ZeroMemory(View, sizeof(*View));
    if (Resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();

    View->Format = Desc.Format;
    switch (Desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            View->Format = DXGI_FORMAT_UNKNOWN;
            View->ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            View->Buffer.NumElements = (UINT)Desc.Width;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY
                                                            : D3D12_UAV_DIMENSION_TEXTURE1D;
            if (Desc.DepthOrArraySize > 1)
                View->Texture1DArray.ArraySize = Desc.DepthOrArraySize;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (Desc.SampleDesc.Count > 1)
            {
                View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY
                                                                : D3D12_UAV_DIMENSION_TEXTURE2DMS;
                if (Desc.DepthOrArraySize > 1)
                    View->Texture2DMSArray.ArraySize = Desc.DepthOrArraySize;
            }
            else
            {
                View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY
                                                                : D3D12_UAV_DIMENSION_TEXTURE2D;
                if (Desc.DepthOrArraySize > 1)
                    View->Texture2DArray.ArraySize = Desc.DepthOrArraySize;
            }
            break;
        default:
            View->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            View->Texture3D.WSize = Desc.DepthOrArraySize;
            break;
    }
}

static
VOID
RxResolveUavDesc(
    _In_opt_ RxResource* Resource,
    _Inout_ D3D12_UNORDERED_ACCESS_VIEW_DESC* View)
{
    if (Resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();
    const UINT Slices = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;

    switch (View->ViewDimension)
    {
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            if (View->Texture2DArray.ArraySize == (UINT)-1)
                View->Texture2DArray.ArraySize = Slices - View->Texture2DArray.FirstArraySlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
            if (View->Texture2DMSArray.ArraySize == (UINT)-1)
                View->Texture2DMSArray.ArraySize = Slices - View->Texture2DMSArray.FirstArraySlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            if (View->Texture3D.WSize == (UINT)-1)
            {
                INT Depth = (INT)Desc.DepthOrArraySize >> View->Texture3D.MipSlice;

                if (Depth <= 1)
                    Depth = 1;
                View->Texture3D.WSize = (UINT)Depth - View->Texture3D.FirstWSlice;
            }
            break;
        default:
            break;
    }

    if (View->ViewDimension != D3D12_UAV_DIMENSION_BUFFER && View->Format == DXGI_FORMAT_UNKNOWN)
        View->Format = Desc.Format;
}

static
VOID
RxBuildDdiUav(
    _In_opt_ RxResource* Resource,
    _In_opt_ RxResource* Counter,
    _In_ const D3D12_UNORDERED_ACCESS_VIEW_DESC& View,
    _Out_ D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002* Args)
{
    ZeroMemory(Args, sizeof(*Args));
    Args->hDrvResource = Resource != nullptr ? Resource->GetDriverHandle() : D3D12DDI_HRESOURCE{};
    Args->Format = View.Format;

    switch (View.ViewDimension)
    {
        case D3D12_UAV_DIMENSION_BUFFER:
            Args->ResourceDimension = D3D12DDI_RD_BUFFER;
            Args->Buffer.hDrvCounterResource = Counter != nullptr ? Counter->GetDriverHandle() : D3D12DDI_HRESOURCE{};
            Args->Buffer.FirstElement = View.Buffer.FirstElement;
            Args->Buffer.NumElements = View.Buffer.NumElements;
            Args->Buffer.StructureByteStride = View.Buffer.StructureByteStride;
            Args->Buffer.CounterOffsetInBytes = View.Buffer.CounterOffsetInBytes;
            Args->Buffer.Flags = (D3D12DDI_BUFFER_UAV_FLAGS)View.Buffer.Flags;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE1D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex1D.MipSlice = View.Texture1D.MipSlice;
            Args->Tex1D.ArraySize = 1;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex1D.MipSlice = View.Texture1DArray.MipSlice;
            Args->Tex1D.FirstArraySlice = View.Texture1DArray.FirstArraySlice;
            Args->Tex1D.ArraySize = View.Texture1DArray.ArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.MipSlice = View.Texture2D.MipSlice;
            Args->Tex2D.ArraySize = 1;
            Args->Tex2D.PlaneSlice = View.Texture2D.PlaneSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.MipSlice = View.Texture2DArray.MipSlice;
            Args->Tex2D.FirstArraySlice = View.Texture2DArray.FirstArraySlice;
            Args->Tex2D.ArraySize = View.Texture2DArray.ArraySize;
            Args->Tex2D.PlaneSlice = View.Texture2DArray.PlaneSlice;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DMS:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.ArraySize = 1;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE2DMSARRAY:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args->Tex2D.FirstArraySlice = View.Texture2DMSArray.FirstArraySlice;
            Args->Tex2D.ArraySize = View.Texture2DMSArray.ArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D:
            Args->ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            Args->Tex3D.MipSlice = View.Texture3D.MipSlice;
            Args->Tex3D.FirstW = View.Texture3D.FirstWSlice;
            Args->Tex3D.WSize = View.Texture3D.WSize;
            break;
        default:
            break;
    }
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateUnorderedAccessView(
    _In_opt_ ID3D12Resource* pResource,
    _In_opt_ ID3D12Resource* pCounterResource,
    _In_opt_ const D3D12_UNORDERED_ACCESS_VIEW_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    RxResource* Resource = RxUnwrap12<RxResource>(pResource);
    RxResource* Counter = RxUnwrap12<RxResource>(pCounterResource);
    D3D12_UNORDERED_ACCESS_VIEW_DESC View;
    D3D12DDIARG_CREATE_UNORDERED_ACCESS_VIEW_0002 Args;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;

    if (!RxUnorderedAccessViewValid(*this, Resource, Counter, pDesc))
    {
        RemoveOnBadView();
        return;
    }

    if (pDesc != nullptr)
    {
        View = *pDesc;
        RxResolveUavDesc(Resource, &View);
    }
    else
    {
        RxDefaultUavDesc(Resource, &View);
    }

    RxBuildDdiUav(Resource, Counter, View, &Args);
    Destination.ptr = DestDescriptor.ptr;
    Ddi().pfnCreateUnorderedAccessView(DriverDevice, &Args, Destination);
}

/* Depth stencil views */

static
D3D12_RESOURCE_DIMENSION
RxDsvResourceKind(
    _In_ D3D12_DSV_DIMENSION Dimension)
{
    switch (Dimension)
    {
        case D3D12_DSV_DIMENSION_TEXTURE1D:
        case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        case D3D12_DSV_DIMENSION_TEXTURE2D:
        case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
        case D3D12_DSV_DIMENSION_TEXTURE2DMS:
        case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
            return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        default:
            return D3D12_RESOURCE_DIMENSION_UNKNOWN;
    }
}

static
UINT
RxCheckTextureDsv(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ BOOLEAN HasResource,
    _In_ const RxViewTarget& Target,
    _In_ const D3D12_DEPTH_STENCIL_VIEW_DESC& Desc,
    _Inout_ DXGI_FORMAT* ViewFormat)
{
    UINT Problems = 0;

    if (!RxViewFormatFits(Target.Format, ViewFormat))
        Problems += 1;

    if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
    {
        if (RxCapMissing(Level, *ViewFormat, RxCapTexture1D))
            Problems += 1;
        if (!HasResource)
            return Problems;

        if (Desc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE1DARRAY)
        {
            if (Desc.Texture1DArray.MipSlice >= Target.MipLevels ||
                !RxWindowFits(Desc.Texture1DArray.FirstArraySlice, Desc.Texture1DArray.ArraySize, Target.ArraySize))
            {
                Problems += 1;
            }
        }
        else if (Desc.Texture1D.MipSlice >= Target.MipLevels)
        {
            Problems += 1;
        }
        return Problems;
    }

    if (RxCapMissing(Level, *ViewFormat, RxCapTexture2D))
        Problems += 1;

    if (HasResource)
    {
        switch (Desc.ViewDimension)
        {
            case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
                if (Desc.Texture2DArray.MipSlice >= Target.MipLevels ||
                    !RxWindowFits(Desc.Texture2DArray.FirstArraySlice, Desc.Texture2DArray.ArraySize, Target.ArraySize))
                {
                    Problems += 1;
                }
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY:
                if (!RxWindowFits(Desc.Texture2DMSArray.FirstArraySlice, Desc.Texture2DMSArray.ArraySize,
                                  Target.ArraySize))
                {
                    Problems += 1;
                }
                break;
            case D3D12_DSV_DIMENSION_TEXTURE2D:
                if (Desc.Texture2D.MipSlice >= Target.MipLevels)
                    Problems += 1;
                break;
            default:
                break;
        }
    }

    if (Target.SampleCount > 1 && (Desc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D ||
                                   Desc.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY))
    {
        Problems += 1;
    }
    return Problems;
}

static
BOOLEAN
RxDepthStencilViewValid(
    _In_ RxDevice12& Device,
    _In_opt_ RxResource* Resource,
    _In_opt_ const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    RxViewTarget Target = {};
    DXGI_FORMAT ViewFormat;
    RxFormatRule12 Rule;
    UINT Problems = 0;

    if (Level < D3D_FEATURE_LEVEL_9_1)
        Problems += 1;

    if (pDesc != nullptr && !RxViewFormatKnown(pDesc->Format))
        return FALSE;

    if (Resource != nullptr)
    {
        RxDescribeTarget(Resource, &Target);
    }
    else
    {
        if (pDesc == nullptr)
            return FALSE;

        Target.Dimension = RxDsvResourceKind(pDesc->ViewDimension);
        if (Target.Dimension == D3D12_RESOURCE_DIMENSION_UNKNOWN)
            return FALSE;
        Target.ArraySize = 1;
        Target.MipLevels = 1;
        Target.Format = pDesc->Format;
        Target.SampleCount = (pDesc->ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMS ||
                              pDesc->ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY) ? 2 : 1;
        Target.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    ViewFormat = pDesc != nullptr ? pDesc->Format : DXGI_FORMAT_UNKNOWN;
    if (pDesc != nullptr && RxDsvResourceKind(pDesc->ViewDimension) != Target.Dimension)
        Problems += 1;

    if (Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D || Target.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        if (pDesc != nullptr)
        {
            Problems += RxCheckTextureDsv(Level, Resource != nullptr, Target, *pDesc, &ViewFormat);
        }
        else
        {
            if (RxFormatIsTypeless(Target.Format))
                Problems += 1;
            ViewFormat = Target.Format;
        }
    }
    else
    {
        Problems += 1;
    }

    Rule = RxFormatCapRule12(Level, ViewFormat, RxCapDepthStencil);
    if (ViewFormat == DXGI_FORMAT_UNKNOWN || (Rule != RxRuleRequired && Rule != RxRuleDriver))
        Problems += 1;

    if ((Target.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0)
        Problems += 1;

    if (pDesc != nullptr && pDesc->Flags != D3D12_DSV_FLAG_NONE)
    {
        const BOOLEAN Stencil = ViewFormat == DXGI_FORMAT_D24_UNORM_S8_UINT ||
                                ViewFormat == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        const BOOLEAN Depth = Stencil || ViewFormat == DXGI_FORMAT_D32_FLOAT || ViewFormat == DXGI_FORMAT_D16_UNORM;

        if ((pDesc->Flags & ~(D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL)) != 0)
            return FALSE;
        if (Level < D3D_FEATURE_LEVEL_11_0)
            return FALSE;
        if ((pDesc->Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) != 0 && !Depth)
            Problems += 1;
        if ((pDesc->Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) != 0 && !Stencil)
            Problems += 1;
    }

    return Problems == 0;
}

static
VOID
RxDefaultDsvDesc(
    _In_opt_ RxResource* Resource,
    _Out_ D3D12_DEPTH_STENCIL_VIEW_DESC* View)
{
    ZeroMemory(View, sizeof(*View));
    if (Resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();

    View->Format = Desc.Format;
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
    {
        View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY
                                                        : D3D12_DSV_DIMENSION_TEXTURE1D;
        if (Desc.DepthOrArraySize > 1)
            View->Texture1DArray.ArraySize = Desc.DepthOrArraySize;
    }
    else if (Desc.SampleDesc.Count > 1)
    {
        View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY
                                                        : D3D12_DSV_DIMENSION_TEXTURE2DMS;
        if (Desc.DepthOrArraySize > 1)
            View->Texture2DMSArray.ArraySize = Desc.DepthOrArraySize;
    }
    else
    {
        View->ViewDimension = Desc.DepthOrArraySize > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY
                                                        : D3D12_DSV_DIMENSION_TEXTURE2D;
        if (Desc.DepthOrArraySize > 1)
            View->Texture2DArray.ArraySize = Desc.DepthOrArraySize;
    }
}

static
VOID
RxResolveDsvDesc(
    _In_opt_ RxResource* Resource,
    _Inout_ D3D12_DEPTH_STENCIL_VIEW_DESC* View)
{
    if (Resource == nullptr)
        return;

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();
    const UINT Slices = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;

    if (View->ViewDimension == D3D12_DSV_DIMENSION_TEXTURE1DARRAY ||
        View->ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY)
    {
        if (View->Texture2DArray.ArraySize == (UINT)-1)
            View->Texture2DArray.ArraySize = Slices - View->Texture2DArray.FirstArraySlice;
    }
    else if (View->ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY)
    {
        if (View->Texture2DMSArray.ArraySize == (UINT)-1)
            View->Texture2DMSArray.ArraySize = Slices - View->Texture2DMSArray.FirstArraySlice;
    }

    if (View->Format == DXGI_FORMAT_UNKNOWN)
        View->Format = Desc.Format;
}

static
VOID
RxBuildDdiDsv(
    _In_opt_ RxResource* Resource,
    _In_ const D3D12_DEPTH_STENCIL_VIEW_DESC& View,
    _Out_ D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW* Args)
{
    ZeroMemory(Args, sizeof(*Args));
    Args->hDrvResource = Resource != nullptr ? Resource->GetDriverHandle() : D3D12DDI_HRESOURCE{};
    Args->Format = View.Format;
    Args->Flags = (D3D12DDI_CREATE_DEPTH_STENCIL_VIEW_FLAGS)View.Flags;
    Args->ResourceDimension = D3D12DDI_RD_TEXTURE2D;

    /* The one dimensional and plain two dimensional shapes share their layout */
    switch (View.ViewDimension)
    {
        case D3D12_DSV_DIMENSION_TEXTURE1D:
        case D3D12_DSV_DIMENSION_TEXTURE2D:
            if (View.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE1D)
                Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex2D.MipSlice = View.Texture2D.MipSlice;
            Args->Tex2D.ArraySize = 1;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
        case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
            if (View.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE1DARRAY)
                Args->ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args->Tex2D.MipSlice = View.Texture2DArray.MipSlice;
            Args->Tex2D.FirstArraySlice = View.Texture2DArray.FirstArraySlice;
            Args->Tex2D.ArraySize = View.Texture2DArray.ArraySize;
            break;
        case D3D12_DSV_DIMENSION_TEXTURE2DMS:
            Args->Tex2D.ArraySize = 1;
            break;
        default:
            Args->Tex2D.FirstArraySlice = View.Texture2DMSArray.FirstArraySlice;
            Args->Tex2D.ArraySize = View.Texture2DMSArray.ArraySize;
            break;
    }
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateDepthStencilView(
    _In_opt_ ID3D12Resource* pResource,
    _In_opt_ const D3D12_DEPTH_STENCIL_VIEW_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    RxResource* Resource = RxUnwrap12<RxResource>(pResource);
    D3D12_DEPTH_STENCIL_VIEW_DESC View;
    D3D12DDIARG_CREATE_DEPTH_STENCIL_VIEW Args;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;

    if (pDesc != nullptr)
    {
        View = *pDesc;
        RxResolveDsvDesc(Resource, &View);
    }
    else
    {
        RxDefaultDsvDesc(Resource, &View);
    }

    if (!RxDepthStencilViewValid(*this, Resource, pDesc))
    {
        RemoveOnBadView();
        return;
    }

    RxBuildDdiDsv(Resource, View, &Args);
    Destination.ptr = DestDescriptor.ptr;
    Ddi().pfnCreateDepthStencilView(DriverDevice, &Args, Destination);
}
