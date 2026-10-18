/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Argument checks for texture creation, done before the driver sees anything
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "resourcecheck.hpp"
#include "formats.hpp"
#include "views.hpp"

/* Misc flags that exist for buffers, or that this runtime does not offer: tiled resources */
#define RX_MISC_NEVER_ON_TEXTURES (D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS | \
                                   D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_TILE_POOL | \
                                   D3D11_RESOURCE_MISC_TILED)

/* Binds a texture can never have */
#define RX_BIND_BUFFER_ONLY (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_CONSTANT_BUFFER | \
                             D3D11_BIND_STREAM_OUTPUT)

/* Binds that write from the pipeline, which immutable and dynamic resources can not have */
#define RX_BIND_GPU_WRITES (D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | \
                            D3D11_BIND_UNORDERED_ACCESS)

/* Any of these makes a format readable by shaders */
#define RX_SUPPORT_SHADER_READ (D3D11_FORMAT_SUPPORT_SHADER_LOAD | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE | \
                                D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_COMPARISON | \
                                D3D11_FORMAT_SUPPORT_SHADER_SAMPLE_MONO_TEXT | D3D11_FORMAT_SUPPORT_SHADER_GATHER)

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE1D_DESC& Desc)
{
    return { D3D11_RESOURCE_DIMENSION_TEXTURE1D, Desc.Width, 1, 1, Desc.MipLevels, Desc.ArraySize, Desc.Format,
             { 1, 0 }, Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, Desc.MiscFlags };
}

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE2D_DESC& Desc)
{
    return { D3D11_RESOURCE_DIMENSION_TEXTURE2D, Desc.Width, Desc.Height, 1, Desc.MipLevels, Desc.ArraySize,
             Desc.Format, Desc.SampleDesc, Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, Desc.MiscFlags };
}

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE3D_DESC& Desc)
{
    return { D3D11_RESOURCE_DIMENSION_TEXTURE3D, Desc.Width, Desc.Height, Desc.Depth, Desc.MipLevels, 1,
             Desc.Format, { 1, 0 }, Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, Desc.MiscFlags };
}

/* Number of levels in a full chain down to one texel */
static
UINT
RxFullChainLength(
    _In_ UINT Largest)
{
    UINT Levels = 0;

    while (Largest != 0)
    {
        Largest >>= 1;
        Levels++;
    }

    return Levels;
}

/*
 * Formats a typed view could reinterpret this one as, the format itself included.
 * Typeless formats reach their whole family; a few typed ones are allowed to as well.
 */
static
UINT
RxCastFamily(
    _In_ DXGI_FORMAT Format,
    _Out_writes_(8) DXGI_FORMAT* Family)
{
    static const struct
    {
        DXGI_FORMAT First;
        DXGI_FORMAT Last;
    } Ranges[] =
    {
        { DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_SINT },
        { DXGI_FORMAT_R32G32B32_TYPELESS, DXGI_FORMAT_R32G32B32_SINT },
        { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_SINT },
        { DXGI_FORMAT_R32G32_TYPELESS, DXGI_FORMAT_R32G32_SINT },
        { DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT },
        { DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UINT },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_SINT },
        { DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_SINT },
        { DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_SINT },
        { DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT },
        { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8G8_SINT },
        { DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_SINT },
        { DXGI_FORMAT_R8_TYPELESS, DXGI_FORMAT_R8_SINT },
        { DXGI_FORMAT_BC1_TYPELESS, DXGI_FORMAT_BC1_UNORM_SRGB },
        { DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM_SRGB },
        { DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM_SRGB },
        { DXGI_FORMAT_BC4_TYPELESS, DXGI_FORMAT_BC4_SNORM },
        { DXGI_FORMAT_BC5_TYPELESS, DXGI_FORMAT_BC5_SNORM },
        { DXGI_FORMAT_BC6H_TYPELESS, DXGI_FORMAT_BC6H_SF16 },
        { DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM_SRGB },
    };
    UINT Count = 0;
    UINT Value;

    /* The BGRA families are not contiguous */
    if (Format == DXGI_FORMAT_B8G8R8A8_TYPELESS || Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
        Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
    {
        Family[0] = DXGI_FORMAT_B8G8R8A8_TYPELESS;
        Family[1] = DXGI_FORMAT_B8G8R8A8_UNORM;
        Family[2] = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        return 3;
    }

    if (Format == DXGI_FORMAT_B8G8R8X8_TYPELESS)
    {
        Family[0] = DXGI_FORMAT_B8G8R8X8_TYPELESS;
        Family[1] = DXGI_FORMAT_B8G8R8X8_UNORM;
        Family[2] = DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
        return 3;
    }

    /* Typed formats stand alone, except the color formats that may still be viewed as their siblings */
    if (!RxIsTypelessFormat(Format) && Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB && Format != DXGI_FORMAT_R10G10B10A2_UNORM)
    {
        Family[0] = Format;
        return 1;
    }

    for (const auto& Range : Ranges)
    {
        if (Format < Range.First || Format > Range.Last)
            continue;

        for (Value = Range.First; Value <= (UINT)Range.Last && Count < 8; Value++)
            Family[Count++] = (DXGI_FORMAT)Value;
        return Count;
    }

    Family[0] = Format;
    return 1;
}

/* Combined support of everything in the family, so a typeless texture can be bound as any member could */
static
UINT
RxFamilySupport(
    _In_ RxDevice& Device,
    _In_ DXGI_FORMAT Format)
{
    DXGI_FORMAT Family[8];
    UINT Count = RxCastFamily(Format, Family);
    UINT Combined = 0;
    UINT Index;
    UINT Bits;

    for (Index = 0; Index < Count; Index++)
    {
        if (SUCCEEDED(Device.GetFormatSupport(Family[Index], &Bits)))
            Combined |= Bits;
    }

    return Combined;
}

/* Families whose typed UAVs may all go through R32 views */
static
BOOLEAN
RxIsR32Castable(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
            return TRUE;
        default:
            return FALSE;
    }
}

/* Width and height in texels of one compression block or packed pair; 1x1 for ordinary formats */
static
VOID
RxBlockShape(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Width,
    _Out_ UINT* Height)
{
    *Width = 1;
    *Height = 1;

    if (RxIsBlockCompressed(Format))
    {
        *Width = 4;
        *Height = 4;
        return;
    }

    switch (Format)
    {
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_420_OPAQUE:
            *Width = 2;
            *Height = 2;
            break;
        case DXGI_FORMAT_YUY2:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
        case DXGI_FORMAT_P208:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
            *Width = 2;
            break;
        case DXGI_FORMAT_NV11:
            *Width = 4;
            break;
        case DXGI_FORMAT_V208:
            *Height = 2;
            break;
        default:
            break;
    }
}

static
BOOLEAN
RxIsPlanarFormat(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_P208:
        case DXGI_FORMAT_V208:
        case DXGI_FORMAT_V408:
            return TRUE;
        default:
            return FALSE;
    }
}

/* Sample count and quality against what the driver reports for the format */
static
BOOLEAN
RxIsValidSampling(
    _In_ RxDevice& Device,
    _In_ const RxTextureRequest& Request)
{
    UINT Count = Request.SampleDesc.Count;
    UINT Quality = Request.SampleDesc.Quality;
    UINT Levels = 0;

    if (Count == 0)
        return FALSE;
    if (Count == 1 && Quality == 0)
        return TRUE;

    if (FAILED(Device.CheckMultisampleQualityLevels(Request.Format, Count, &Levels)))
        return FALSE;

    /* The two standard patterns only exist for power of two counts */
    if (Quality == D3D11_STANDARD_MULTISAMPLE_PATTERN || Quality == D3D11_CENTER_MULTISAMPLE_PATTERN)
        return Levels != 0 && (Count == 1 || Count == 2 || Count == 4 || Count == 8 || Count == 16);

    return Quality < Levels;
}

static
HRESULT
RxCheckTextureRequestWorker(
    _In_ RxDevice& Device,
    _In_ const RxTextureRequest& Request,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData);

HRESULT
RxCheckTextureRequest(
    _In_ RxDevice& Device,
    _In_ const RxTextureRequest& Request,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData)
{
    HRESULT hr = RxCheckTextureRequestWorker(Device, Request, InitialData);

    if (FAILED(hr))
    {
        RX_TRACE(L"texture refused: dimension %u %ux%ux%u mips %u array %u format %u samples %u usage %u bind %X cpu %X misc %X",
                 Request.Dimension, Request.Width, Request.Height, Request.Depth, Request.MipLevels, Request.ArraySize,
                 Request.Format, Request.SampleDesc.Count, Request.Usage, Request.BindFlags, Request.CPUAccessFlags,
                 Request.MiscFlags);
    }

    return hr;
}

static
HRESULT
RxCheckTextureRequestWorker(
    _In_ RxDevice& Device,
    _In_ const RxTextureRequest& Request,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData)
{
    BOOLEAN IsCube = (Request.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
    BOOLEAN IsMultisampled = Request.SampleDesc.Count > 1;
    BOOLEAN IsLevel11 = Device.GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0;
    UINT Bind = Request.BindFlags;
    UINT Misc = Request.MiscFlags;
    UINT LargestSide = std::max({ Request.Width, Request.Height, Request.Depth });
    UINT SideLimit;
    UINT ArrayLimit;
    UINT DimensionBit;
    UINT BlockWidth;
    UINT BlockHeight;
    UINT Mips;
    UINT Support;
    UINT Family;
    UINT Index;

    /* Values outside every enum */
    if (Request.Usage > D3D11_USAGE_STAGING || (Bind & ~0xFFFu) != 0 ||
        (Request.CPUAccessFlags & ~(D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)) != 0 ||
        (Misc & (0x8u | 0x400u | 0x10000u)) != 0 || Misc >= 0x400000u)
    {
        return E_INVALIDARG;
    }

    /* Sizes */
    switch (Request.Dimension)
    {
        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
            SideLimit = IsLevel11 ? D3D11_REQ_TEXTURE1D_U_DIMENSION : D3D10_REQ_TEXTURE1D_U_DIMENSION;
            ArrayLimit = IsLevel11 ? D3D11_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION : D3D10_REQ_TEXTURE1D_ARRAY_AXIS_DIMENSION;
            DimensionBit = D3D11_FORMAT_SUPPORT_TEXTURE1D;
            break;
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            SideLimit = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
            ArrayLimit = 1;
            DimensionBit = D3D11_FORMAT_SUPPORT_TEXTURE3D;
            break;
        default:
            SideLimit = IsLevel11 ? D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION : D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
            ArrayLimit = IsLevel11 ? D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION : D3D10_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
            DimensionBit = IsCube ? D3D11_FORMAT_SUPPORT_TEXTURECUBE : D3D11_FORMAT_SUPPORT_TEXTURE2D;
            break;
    }

    if (Request.Width == 0 || Request.Height == 0 || Request.Depth == 0 || Request.ArraySize == 0 ||
        LargestSide > SideLimit || Request.ArraySize > ArrayLimit)
    {
        return E_INVALIDARG;
    }

    Mips = Request.MipLevels ? Request.MipLevels : RxFullChainLength(LargestSide);
    if (Mips > RxFullChainLength(LargestSide))
        return E_INVALIDARG;

    /* Cube maps are square, at least one cube of faces, and never multisampled or dynamic */
    if (IsCube)
    {
        if (Request.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || Request.Width != Request.Height ||
            Request.ArraySize < 6 || IsMultisampled || Request.Usage == D3D11_USAGE_DYNAMIC)
        {
            return E_INVALIDARG;
        }

        if (Device.GetFeatureLevel() < D3D_FEATURE_LEVEL_10_1 && Request.ArraySize != 6)
            return E_INVALIDARG;
    }

    /* Sampling */
    if (!RxIsValidSampling(Device, Request) || (IsMultisampled && Request.MipLevels != 1))
        return E_INVALIDARG;

    /* Misc flags that belong to buffers, or that need something this texture does not have */
    if ((Misc & RX_MISC_NEVER_ON_TEXTURES) != 0)
        return E_INVALIDARG;
    if ((Misc & D3D11_RESOURCE_MISC_RESOURCE_CLAMP) &&
        (!IsLevel11 || (Misc & (D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_SHARED |
                                D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)) || Request.CPUAccessFlags != 0))
    {
        return E_INVALIDARG;
    }
    if ((Misc & D3D11_RESOURCE_MISC_GDI_COMPATIBLE) &&
        (!(Bind & D3D11_BIND_RENDER_TARGET) || Request.Usage != D3D11_USAGE_DEFAULT || Mips != 1 ||
         IsMultisampled || (Misc & D3D11_RESOURCE_MISC_RESOURCE_CLAMP) ||
         (Request.Format != DXGI_FORMAT_B8G8R8A8_UNORM && Request.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS &&
          Request.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)))
    {
        return E_INVALIDARG;
    }
    if ((Misc & D3D11_RESOURCE_MISC_GENERATE_MIPS) &&
        (Bind & (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET)) !=
            (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET))
    {
        return E_INVALIDARG;
    }
    if ((Misc & D3D11_RESOURCE_MISC_SHARED) && (Misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
        return E_INVALIDARG;

    /* The format must exist here, and the exact format decides mips and video binds */
    if (FAILED(Device.GetFormatSupport(Request.Format, &Support)) || Request.Format == DXGI_FORMAT_UNKNOWN)
        return E_INVALIDARG;
    if (Mips != 1 && !(Support & D3D11_FORMAT_SUPPORT_MIP))
        return E_INVALIDARG;
    if (!(Support & DimensionBit))
        return E_INVALIDARG;
    if ((Bind & D3D11_BIND_DECODER) && !(Support & D3D11_FORMAT_SUPPORT_DECODER_OUTPUT))
        return E_INVALIDARG;
    if ((Bind & D3D11_BIND_VIDEO_ENCODER) && !(Support & D3D11_FORMAT_SUPPORT_VIDEO_ENCODER))
        return E_INVALIDARG;
    if (RxIsPlanarFormat(Request.Format) && Request.ArraySize > 2 && !(Bind & D3D11_BIND_DECODER))
        return E_INVALIDARG;

    /* Compressed and subsampled formats come in whole blocks */
    RxBlockShape(Request.Format, &BlockWidth, &BlockHeight);
    if ((Request.Width % BlockWidth) != 0 || (Request.Height % BlockHeight) != 0)
        return E_INVALIDARG;

    /* Binds, judged against everything the format could be viewed as */
    Family = RxFamilySupport(Device, Request.Format);
    if ((Bind & RX_BIND_BUFFER_ONLY) || (Bind & 0x100u))
        return E_INVALIDARG;
    if ((Bind & D3D11_BIND_RENDER_TARGET) && (Bind & D3D11_BIND_DEPTH_STENCIL))
        return E_INVALIDARG;
    if ((Bind & D3D11_BIND_SHADER_RESOURCE) && !(Family & RX_SUPPORT_SHADER_READ))
        return E_INVALIDARG;
    if ((Bind & D3D11_BIND_RENDER_TARGET) && !(Family & D3D11_FORMAT_SUPPORT_RENDER_TARGET) &&
        !(Support & (D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT | D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT)))
    {
        return E_INVALIDARG;
    }
    if ((Bind & D3D11_BIND_DEPTH_STENCIL) &&
        (!(Family & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) || Request.Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D))
    {
        return E_INVALIDARG;
    }
    if (Bind & D3D11_BIND_UNORDERED_ACCESS)
    {
        BOOLEAN ViaR32 = RxIsR32Castable(Request.Format);

        if (!IsLevel11 || (Bind & D3D11_BIND_DEPTH_STENCIL) || IsMultisampled ||
            (!ViaR32 && !(Family & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW)))
        {
            return E_INVALIDARG;
        }
    }

    /* Usage against binds and CPU access */
    switch (Request.Usage)
    {
        case D3D11_USAGE_DEFAULT:
            /* Mapping default textures needs a newer driver interface than this runtime speaks */
            if (Request.CPUAccessFlags != 0)
                return E_INVALIDARG;
            break;

        case D3D11_USAGE_IMMUTABLE:
            if (Request.CPUAccessFlags != 0 || (Bind & RX_BIND_GPU_WRITES) || Bind == 0 || InitialData == nullptr)
                return E_INVALIDARG;
            break;

        case D3D11_USAGE_DYNAMIC:
            if ((Bind & RX_BIND_GPU_WRITES) || Bind == 0 ||
                (Misc & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)))
            {
                return E_INVALIDARG;
            }

            /* Only 2D textures leave CPU access and mip count to the driver */
            if (Request.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D &&
                (Request.CPUAccessFlags != D3D11_CPU_ACCESS_WRITE || Mips != 1 || Request.ArraySize != 1))
            {
                return E_INVALIDARG;
            }
            break;

        case D3D11_USAGE_STAGING:
            if (Bind != 0)
                return E_INVALIDARG;
            if (Request.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D && Request.CPUAccessFlags == 0)
                return E_INVALIDARG;
            break;
    }

    /* Multisampled textures are only ever read, rendered or depth tested */
    if (IsMultisampled &&
        ((Bind & ~(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL)) != 0 || Bind == 0))
    {
        return E_INVALIDARG;
    }

    if (InitialData == nullptr)
        return S_OK;

    /* Initial data: every subresource needs memory, and rows that step forward */
    if (IsMultisampled || Request.Format == DXGI_FORMAT_420_OPAQUE)
        return E_INVALIDARG;

    for (Index = 0; Index < Mips * Request.ArraySize; Index++)
    {
        const D3D11_SUBRESOURCE_DATA& Data = InitialData[Index];

        if (Data.pSysMem == nullptr)
            return E_INVALIDARG;
        if (Request.Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D && Data.SysMemPitch == 0)
            return E_INVALIDARG;
        if (Request.Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE3D &&
            (Data.SysMemPitch == 0 || Data.SysMemSlicePitch == 0))
        {
            return E_INVALIDARG;
        }
    }

    return S_OK;
}
