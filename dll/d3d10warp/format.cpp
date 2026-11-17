/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXGI format layouts, capabilities and color encoding
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "format.hpp"

#define RX_FORMAT_COUNT 192

typedef struct _RX_WARP_FORMAT_CAPS
{
    BOOLEAN Valid;
    UINT Support;
    BOOLEAN Displayable;
    BOOLEAN Multisample;
} RX_WARP_FORMAT_CAPS;

static const RX_WARP_FORMAT_CAPS RxWarpFormatCaps[RX_FORMAT_COUNT] =
{
#include "formatcaps.inl"
};

static RX_WARP_FORMAT_INFO RxWarpFormats[RX_FORMAT_COUNT];

static
VOID
RxWarpDefine(
    _In_ UINT Format,
    _In_ UINT8 Kind,
    _In_ UINT8 Bytes,
    _In_ UINT8 Count = 0,
    _In_ RX_WARP_CHANNEL First = {},
    _In_ RX_WARP_CHANNEL Second = {},
    _In_ RX_WARP_CHANNEL Third = {},
    _In_ RX_WARP_CHANNEL Fourth = {})
{
    RX_WARP_FORMAT_INFO *Info = &RxWarpFormats[Format];

    Info->Kind = Kind;
    Info->Bytes = Bytes;
    Info->ChannelCount = Count;
    Info->Channels[0] = First;
    Info->Channels[1] = Second;
    Info->Channels[2] = Third;
    Info->Channels[3] = Fourth;
}

/* The same channel type on every component, in R, G, B, A order */
static
VOID
RxWarpDefineRgba(
    _In_ UINT Format,
    _In_ UINT8 Count,
    _In_ UINT8 Bits,
    _In_ UINT8 Type)
{
    RxWarpDefine(Format, RxKindPlain, (UINT8)(Count * Bits / 8), Count,
                 { Bits, Type, 0 }, { Bits, Type, 1 }, { Bits, Type, 2 }, { Bits, Type, 3 });
}

/* A family of formats that share a layout: TYPELESS first, then the typed members */
static
VOID
RxWarpDefineFamily(
    _In_ UINT First,
    _In_ UINT8 Count,
    _In_ UINT8 Bits,
    _In_reads_(Members) const UINT8 *Types,
    _In_ UINT Members)
{
    for (UINT Index = 0; Index < Members; Index++)
        RxWarpDefineRgba(First + Index, Count, Bits, Types[Index]);
}

VOID
RxWarpInitFormats(VOID)
{
    static const UINT8 FloatUintSint[] = { RxChannelTypeless, RxChannelFloat, RxChannelUint, RxChannelSint };
    static const UINT8 Half[] = { RxChannelTypeless, RxChannelFloat, RxChannelUnorm, RxChannelUint, RxChannelSnorm, RxChannelSint };
    static const UINT8 Byte4[] = { RxChannelTypeless, RxChannelUnorm, RxChannelSrgb, RxChannelUint, RxChannelSnorm, RxChannelSint };
    static const UINT8 Byte2[] = { RxChannelTypeless, RxChannelUnorm, RxChannelUint, RxChannelSnorm, RxChannelSint };
    static const UINT8 Short1[] = { RxChannelFloat, RxChannelUnorm, RxChannelUnorm, RxChannelUint, RxChannelSnorm, RxChannelSint };
    UINT Index;

    RxWarpDefine(DXGI_FORMAT_UNKNOWN, RxKindBuffer, 1);
    RxWarpDefineFamily(DXGI_FORMAT_R32G32B32A32_TYPELESS, 4, 32, FloatUintSint, 4);
    RxWarpDefineFamily(DXGI_FORMAT_R32G32B32_TYPELESS, 3, 32, FloatUintSint, 4);
    RxWarpDefineFamily(DXGI_FORMAT_R16G16B16A16_TYPELESS, 4, 16, Half, 6);
    RxWarpDefineFamily(DXGI_FORMAT_R32G32_TYPELESS, 2, 32, FloatUintSint, 4);

    /* 64 bit depth: float depth, eight bit stencil, 24 unused bits */
    RxWarpDefine(DXGI_FORMAT_R32G8X24_TYPELESS, RxKindPlain, 8, 3,
                 { 32, RxChannelTypeless, 0 }, { 8, RxChannelTypeless, 1 }, { 24, RxChannelUnused, 0 });
    RxWarpDefine(DXGI_FORMAT_D32_FLOAT_S8X24_UINT, RxKindPlain, 8, 3,
                 { 32, RxChannelFloat, 4 }, { 8, RxChannelUint, 5 }, { 24, RxChannelUnused, 0 });
    RxWarpDefine(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, RxKindPlain, 8, 3,
                 { 32, RxChannelFloat, 0 }, { 8, RxChannelUnused, 0 }, { 24, RxChannelUnused, 0 });
    RxWarpDefine(DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, RxKindPlain, 8, 3,
                 { 32, RxChannelUnused, 0 }, { 8, RxChannelUint, 1 }, { 24, RxChannelUnused, 0 });

    for (Index = DXGI_FORMAT_R10G10B10A2_TYPELESS; Index <= DXGI_FORMAT_R10G10B10A2_UINT; Index++)
    {
        UINT8 Type = (Index == DXGI_FORMAT_R10G10B10A2_TYPELESS) ? RxChannelTypeless :
                     (Index == DXGI_FORMAT_R10G10B10A2_UNORM) ? RxChannelUnorm : RxChannelUint;

        RxWarpDefine(Index, RxKindPlain, 4, 4, { 10, Type, 0 }, { 10, Type, 1 }, { 10, Type, 2 }, { 2, Type, 3 });
    }
    RxWarpDefine(DXGI_FORMAT_R11G11B10_FLOAT, RxKindR11G11B10, 4);

    RxWarpDefineFamily(DXGI_FORMAT_R8G8B8A8_TYPELESS, 4, 8, Byte4, 6);
    for (Index = 0; Index < 6; Index++)
        RxWarpDefineRgba(DXGI_FORMAT_R16G16_TYPELESS + Index, 2, 16, Half[Index]);

    RxWarpDefine(DXGI_FORMAT_R32_TYPELESS, RxKindPlain, 4, 1, { 32, RxChannelTypeless, 0 });
    RxWarpDefine(DXGI_FORMAT_D32_FLOAT, RxKindPlain, 4, 1, { 32, RxChannelFloat, 4 });
    RxWarpDefine(DXGI_FORMAT_R32_FLOAT, RxKindPlain, 4, 1, { 32, RxChannelFloat, 0 });
    RxWarpDefine(DXGI_FORMAT_R32_UINT, RxKindPlain, 4, 1, { 32, RxChannelUint, 0 });
    RxWarpDefine(DXGI_FORMAT_R32_SINT, RxKindPlain, 4, 1, { 32, RxChannelSint, 0 });

    RxWarpDefine(DXGI_FORMAT_R24G8_TYPELESS, RxKindPlain, 4, 2, { 24, RxChannelTypeless, 0 }, { 8, RxChannelTypeless, 1 });
    RxWarpDefine(DXGI_FORMAT_D24_UNORM_S8_UINT, RxKindPlain, 4, 2, { 24, RxChannelUnorm, 4 }, { 8, RxChannelUint, 5 });
    RxWarpDefine(DXGI_FORMAT_R24_UNORM_X8_TYPELESS, RxKindPlain, 4, 2, { 24, RxChannelUnorm, 0 }, { 8, RxChannelUnused, 0 });
    RxWarpDefine(DXGI_FORMAT_X24_TYPELESS_G8_UINT, RxKindPlain, 4, 2, { 24, RxChannelUnused, 0 }, { 8, RxChannelUint, 1 });

    for (Index = 0; Index < 5; Index++)
        RxWarpDefineRgba(DXGI_FORMAT_R8G8_TYPELESS + Index, 2, 8, Byte2[Index]);

    RxWarpDefine(DXGI_FORMAT_R16_TYPELESS, RxKindPlain, 2, 1, { 16, RxChannelTypeless, 0 });
    for (Index = 0; Index < 6; Index++)
        RxWarpDefine(DXGI_FORMAT_R16_FLOAT + Index, RxKindPlain, 2, 1, { 16, Short1[Index], (UINT8)(Index == 1 ? 4 : 0) });

    RxWarpDefine(DXGI_FORMAT_R8_TYPELESS, RxKindPlain, 1, 1, { 8, RxChannelTypeless, 0 });
    RxWarpDefine(DXGI_FORMAT_R8_UNORM, RxKindPlain, 1, 1, { 8, RxChannelUnorm, 0 });
    RxWarpDefine(DXGI_FORMAT_R8_UINT, RxKindPlain, 1, 1, { 8, RxChannelUint, 0 });
    RxWarpDefine(DXGI_FORMAT_R8_SNORM, RxKindPlain, 1, 1, { 8, RxChannelSnorm, 0 });
    RxWarpDefine(DXGI_FORMAT_R8_SINT, RxKindPlain, 1, 1, { 8, RxChannelSint, 0 });
    RxWarpDefine(DXGI_FORMAT_A8_UNORM, RxKindPlain, 1, 1, { 8, RxChannelUnorm, 3 });
    RxWarpDefine(DXGI_FORMAT_R1_UNORM, RxKindPlanar, 1);
    RxWarpDefine(DXGI_FORMAT_R9G9B9E5_SHAREDEXP, RxKindR9G9B9E5, 4);
    RxWarpDefine(DXGI_FORMAT_R8G8_B8G8_UNORM, RxKindPacked422, 4);
    RxWarpDefine(DXGI_FORMAT_G8R8_G8B8_UNORM, RxKindPacked422, 4);

    for (Index = DXGI_FORMAT_BC1_TYPELESS; Index <= DXGI_FORMAT_BC5_SNORM; Index++)
    {
        BOOLEAN Small = (Index <= DXGI_FORMAT_BC1_UNORM_SRGB) || (Index >= DXGI_FORMAT_BC4_TYPELESS && Index <= DXGI_FORMAT_BC4_SNORM);

        RxWarpDefine(Index, RxKindBlock, Small ? 8 : 16);
    }
    for (Index = DXGI_FORMAT_BC6H_TYPELESS; Index <= DXGI_FORMAT_BC7_UNORM_SRGB; Index++)
        RxWarpDefine(Index, RxKindBlock, 16);

    RxWarpDefine(DXGI_FORMAT_B5G6R5_UNORM, RxKindPlain, 2, 3,
                 { 5, RxChannelUnorm, 2 }, { 6, RxChannelUnorm, 1 }, { 5, RxChannelUnorm, 0 });
    RxWarpDefine(DXGI_FORMAT_B5G5R5A1_UNORM, RxKindPlain, 2, 4,
                 { 5, RxChannelUnorm, 2 }, { 5, RxChannelUnorm, 1 }, { 5, RxChannelUnorm, 0 }, { 1, RxChannelUnorm, 3 });
    RxWarpDefine(DXGI_FORMAT_B4G4R4A4_UNORM, RxKindPlain, 2, 4,
                 { 4, RxChannelUnorm, 2 }, { 4, RxChannelUnorm, 1 }, { 4, RxChannelUnorm, 0 }, { 4, RxChannelUnorm, 3 });

    /* BGRA families: B, G, R and then alpha or an unused byte */
    {
        static const struct { UINT Format; UINT8 Type; BOOLEAN Alpha; } Bgra[] =
        {
            { DXGI_FORMAT_B8G8R8A8_UNORM, RxChannelUnorm, TRUE },
            { DXGI_FORMAT_B8G8R8X8_UNORM, RxChannelUnorm, FALSE },
            { DXGI_FORMAT_B8G8R8A8_TYPELESS, RxChannelTypeless, TRUE },
            { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, RxChannelSrgb, TRUE },
            { DXGI_FORMAT_B8G8R8X8_TYPELESS, RxChannelTypeless, FALSE },
            { DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, RxChannelSrgb, FALSE },
        };

        for (const auto &Entry : Bgra)
        {
            RxWarpDefine(Entry.Format, RxKindPlain, 4, 4, { 8, Entry.Type, 2 }, { 8, Entry.Type, 1 }, { 8, Entry.Type, 0 },
                         Entry.Alpha ? RX_WARP_CHANNEL{ 8, Entry.Type == RxChannelSrgb ? (UINT8)RxChannelUnorm : Entry.Type, 3 } :
                                       RX_WARP_CHANNEL{ 8, RxChannelUnused, 0 });
        }
    }
    RxWarpDefine(DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, RxKindPlanar, 4);

    RxWarpDefine(DXGI_FORMAT_AYUV, RxKindPlain, 4, 4,
                 { 8, RxChannelUnorm, 2 }, { 8, RxChannelUnorm, 1 }, { 8, RxChannelUnorm, 0 }, { 8, RxChannelUnorm, 3 });
    RxWarpDefine(DXGI_FORMAT_Y410, RxKindPlain, 4, 4,
                 { 10, RxChannelUnorm, 0 }, { 10, RxChannelUnorm, 1 }, { 10, RxChannelUnorm, 2 }, { 2, RxChannelUnorm, 3 });
    RxWarpDefine(DXGI_FORMAT_Y416, RxKindPlain, 8, 4,
                 { 16, RxChannelUnorm, 0 }, { 16, RxChannelUnorm, 1 }, { 16, RxChannelUnorm, 2 }, { 16, RxChannelUnorm, 3 });
    for (Index = DXGI_FORMAT_NV12; Index <= DXGI_FORMAT_420_OPAQUE; Index++)
        RxWarpDefine(Index, RxKindPlanar, (Index == DXGI_FORMAT_NV12 || Index == DXGI_FORMAT_420_OPAQUE) ? 1 : 2);
    RxWarpDefine(DXGI_FORMAT_YUY2, RxKindPacked422, 4);
    RxWarpDefine(DXGI_FORMAT_Y210, RxKindPacked422, 8);
    RxWarpDefine(DXGI_FORMAT_Y216, RxKindPacked422, 8);
    RxWarpDefine(DXGI_FORMAT_NV11, RxKindPlanar, 1);
    RxWarpDefine(DXGI_FORMAT_AI44, RxKindPlain, 1, 1, { 8, RxChannelUnorm, 0 });
    RxWarpDefine(DXGI_FORMAT_IA44, RxKindPlain, 1, 1, { 8, RxChannelUnorm, 0 });
    RxWarpDefine(DXGI_FORMAT_P8, RxKindPlain, 1, 1, { 8, RxChannelUint, 0 });
    RxWarpDefine(DXGI_FORMAT_A8P8, RxKindPlain, 2, 2, { 8, RxChannelUint, 0 }, { 8, RxChannelUnorm, 3 });
    RxWarpDefine(DXGI_FORMAT_P208, RxKindPlanar, 1);
    RxWarpDefine(DXGI_FORMAT_V208, RxKindPlanar, 1);
    RxWarpDefine(DXGI_FORMAT_V408, RxKindPlanar, 1);
    RxWarpDefine(191, RxKindPlain, 2, 4,
                 { 4, RxChannelUnorm, 3 }, { 4, RxChannelUnorm, 2 }, { 4, RxChannelUnorm, 1 }, { 4, RxChannelUnorm, 0 });
}

const RX_WARP_FORMAT_INFO *
RxWarpFormatInfo(
    _In_ DXGI_FORMAT Format)
{
    if ((UINT)Format >= RX_FORMAT_COUNT || RxWarpFormats[Format].Bytes == 0)
        return nullptr;
    return &RxWarpFormats[Format];
}

UINT
RxWarpFormatBlockWidth(
    _In_ DXGI_FORMAT Format)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);

    if (Info == nullptr)
        return 1;
    if (Info->Kind == RxKindBlock)
        return 4;
    if (Info->Kind == RxKindPacked422)
        return 2;
    return 1;
}

UINT
RxWarpFormatRowBytes(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    UINT Block = RxWarpFormatBlockWidth(Format);

    if (Info == nullptr)
        return Width;
    return (Width + Block - 1) / Block * Info->Bytes;
}

UINT
RxWarpFormatRows(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Height)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);

    if (Info != nullptr && Info->Kind == RxKindBlock)
        return (Height + 3) / 4;
    return Height;
}

UINT
RxWarpFormatSupport(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Interface)
{
    const RX_WARP_FORMAT_CAPS *Caps;
    UINT Support;

    if ((UINT)Format >= RX_FORMAT_COUNT || !RxWarpFormatCaps[Format].Valid)
        return D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED;

    Caps = &RxWarpFormatCaps[Format];
    Support = Caps->Support;
    if (Caps->Displayable)
        Support |= D3DWDDM3_0DDI_FORMAT_SUPPORT_DISPLAYABLE;

    /* Without overlay planes this format keeps scanout only */
    if (Format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP)
        Support = (Support & ~D3DWDDM3_0DDI_FORMAT_SUPPORT_DISPLAYABLE) | D3D11_1DDI_FORMAT_SUPPORT_DISPLAY;

    if (Format == DXGI_FORMAT_NV12 && Interface >= 0xB0022)
        Support |= D3D11_1DDI_FORMAT_SUPPORT_CAPTURE | D3D11_1DDI_FORMAT_SUPPORT_VIDEO_ENCODER;
    return Support;
}

UINT
RxWarpMultisampleQuality(
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ UINT Flags)
{
    /* Tiled resources only multisample at four samples */
    if (Flags != 0 && SampleCount != 0 && SampleCount != 1 && SampleCount != 4)
        return 0;
    if (SampleCount != 2 && SampleCount != 4 && SampleCount != 8 && SampleCount != 16)
        return 0;
    if ((UINT)Format >= RX_FORMAT_COUNT || !RxWarpFormatCaps[Format].Multisample)
        return 0;
    return 1;
}

/* Float to normalized integer: clamp, scale and round half away from zero */
static
UINT
RxWarpToUnorm(
    _In_ FLOAT Value,
    _In_ UINT Bits)
{
    FLOAT Max = (FLOAT)((1ull << Bits) - 1);

    if (!(Value > 0.0f))
        return 0;
    if (Value >= 1.0f)
        return (UINT)Max;
    return (UINT)roundf(Value * Max);
}

static
UINT
RxWarpToSnorm(
    _In_ FLOAT Value,
    _In_ UINT Bits)
{
    FLOAT Max = (FLOAT)((1ull << (Bits - 1)) - 1);
    INT Result;

    if (Value != Value)
        return 0;
    if (Value >= 1.0f)
        Result = (INT)Max;
    else if (Value <= -1.0f)
        Result = -(INT)Max;
    else
        Result = (INT)(Value * Max + (Value >= 0.0f ? 0.5f : -0.5f));
    return (UINT)Result & (UINT)((1ull << Bits) - 1);
}

static
FLOAT
RxWarpLinearToSrgb(
    _In_ FLOAT Value)
{
    if (!(Value > 0.0f))
        return 0.0f;
    if (Value >= 1.0f)
        return 1.0f;
    if (Value <= 0.0031308f)
        return Value * 12.92f;
    return 1.055f * powf(Value, 1.0f / 2.4f) - 0.055f;
}

/* IEEE half from float, rounding to nearest even; NaN stays NaN, overflow becomes infinity */
static
UINT
RxWarpToHalf(
    _In_ FLOAT Value)
{
    UINT Bits;
    UINT Sign;
    INT Exponent;
    UINT Mantissa;
    UINT Result;

    memcpy(&Bits, &Value, sizeof(Bits));
    Sign = (Bits >> 16) & 0x8000;
    Exponent = (INT)((Bits >> 23) & 0xFF) - 127 + 15;
    Mantissa = Bits & 0x7FFFFF;

    if (((Bits >> 23) & 0xFF) == 0xFF)
        return Sign | 0x7C00 | (Mantissa ? 0x200 : 0);
    if (Exponent >= 31)
        return Sign | 0x7C00;
    if (Exponent <= 0)
    {
        if (Exponent < -10)
            return Sign;
        Mantissa |= 0x800000;
        UINT Shift = (UINT)(14 - Exponent);
        UINT Half = Mantissa >> Shift;
        UINT Rest = Mantissa & ((1u << Shift) - 1);
        UINT Midpoint = 1u << (Shift - 1);

        if (Rest > Midpoint || (Rest == Midpoint && (Half & 1)))
            Half++;
        return Sign | Half;
    }

    Result = ((UINT)Exponent << 10) | (Mantissa >> 13);
    if ((Mantissa & 0x1FFF) > 0x1000 || ((Mantissa & 0x1FFF) == 0x1000 && (Result & 1)))
        Result++;
    return Sign | Result;
}

/* Unsigned small float (11 or 10 bits: 5 bit exponent), negative values clamp to zero */
static
UINT
RxWarpToSmallFloat(
    _In_ FLOAT Value,
    _In_ UINT MantissaBits)
{
    UINT Half;

    if (Value != Value)
        return (0x1Fu << MantissaBits) | 1;
    if (!(Value > 0.0f))
        return 0;
    Half = RxWarpToHalf(Value);
    if ((Half & 0x7C00) == 0x7C00)
        return 0x1Fu << MantissaBits;
    return Half >> (10 - MantissaBits);
}

static
VOID
RxWarpPutBits(
    _Inout_updates_bytes_(16) BYTE *Element,
    _In_ UINT Offset,
    _In_ UINT Bits,
    _In_ UINT64 Value)
{
    for (UINT Bit = 0; Bit < Bits; Bit++)
    {
        UINT Position = Offset + Bit;

        if ((Value >> Bit) & 1)
            Element[Position / 8] |= (BYTE)(1u << (Position % 8));
    }
}

static
FLOAT
RxWarpComponent(
    _In_reads_(4) const FLOAT *Color,
    _In_ UINT Source)
{
    if (Source < 4)
        return Color[Source];
    return Color[Source - 4];
}

BOOLEAN
RxWarpEncodeColor(
    _In_ DXGI_FORMAT Format,
    _In_reads_(4) const FLOAT *Color,
    _Out_writes_bytes_(16) BYTE *Element)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    UINT Offset = 0;

    memset(Element, 0, 16);
    if (Info == nullptr)
        return FALSE;

    if (Info->Kind == RxKindR11G11B10)
    {
        UINT Packed = RxWarpToSmallFloat(Color[0], 6) | (RxWarpToSmallFloat(Color[1], 6) << 11) |
                      (RxWarpToSmallFloat(Color[2], 5) << 22);

        memcpy(Element, &Packed, sizeof(Packed));
        return TRUE;
    }
    if (Info->Kind != RxKindPlain)
        return FALSE;

    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        const RX_WARP_CHANNEL &Channel = Info->Channels[Index];
        FLOAT Value = RxWarpComponent(Color, Channel.Source);
        UINT64 Bits = 0;
        UINT Raw;

        switch (Channel.Type)
        {
            case RxChannelUnorm:
                Bits = RxWarpToUnorm(Value, Channel.Bits);
                break;
            case RxChannelSrgb:
                /* Alpha is linear in sRGB formats */
                if (Channel.Source == 3)
                    Bits = RxWarpToUnorm(Value, Channel.Bits);
                else
                    Bits = RxWarpToUnorm(RxWarpLinearToSrgb(Value), Channel.Bits);
                break;
            case RxChannelSnorm:
                Bits = RxWarpToSnorm(Value, Channel.Bits);
                break;
            case RxChannelFloat:
                if (Channel.Bits == 32)
                {
                    memcpy(&Raw, &Value, sizeof(Raw));
                    Bits = Raw;
                }
                else
                {
                    Bits = RxWarpToHalf(Value);
                }
                break;
            case RxChannelUint:
            case RxChannelSint:
                /* Float clears of integer formats store the value converted to an integer */
                Bits = (UINT64)(INT64)Value & ((Channel.Bits == 32) ? 0xFFFFFFFFull : ((1ull << Channel.Bits) - 1));
                break;
            case RxChannelTypeless:
                memcpy(&Raw, &Value, sizeof(Raw));
                Bits = (Channel.Bits == 32) ? Raw : RxWarpToUnorm(Value, Channel.Bits);
                break;
            default:
                break;
        }
        RxWarpPutBits(Element, Offset, Channel.Bits, Bits);
        Offset += Channel.Bits;
    }
    return TRUE;
}

BOOLEAN
RxWarpEncodeUint(
    _In_ DXGI_FORMAT Format,
    _In_reads_(4) const UINT *Values,
    _Out_writes_bytes_(16) BYTE *Element)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    UINT Offset = 0;

    memset(Element, 0, 16);
    if (Info == nullptr || Info->Kind != RxKindPlain)
        return FALSE;

    /* Raw bits per channel, truncated to the channel width */
    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        const RX_WARP_CHANNEL &Channel = Info->Channels[Index];
        UINT Value = Values[Channel.Source < 4 ? Channel.Source : Channel.Source - 4];

        if (Channel.Type != RxChannelUnused)
            RxWarpPutBits(Element, Offset, Channel.Bits, Value);
        Offset += Channel.Bits;
    }
    return TRUE;
}

static
UINT64
RxWarpGetBits(
    _In_reads_bytes_(16) const BYTE *Element,
    _In_ UINT Offset,
    _In_ UINT Bits)
{
    UINT64 Value = 0;

    for (UINT Bit = 0; Bit < Bits; Bit++)
    {
        UINT Position = Offset + Bit;

        Value |= (UINT64)((Element[Position / 8] >> (Position % 8)) & 1) << Bit;
    }
    return Value;
}

static
FLOAT
RxWarpBitsToFloat(
    _In_ UINT Bits)
{
    FLOAT Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
UINT
RxWarpFloatToBits(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

/* Small float with a five bit exponent and the given mantissa width to float */
static
FLOAT
RxWarpFromSmallFloat(
    _In_ UINT Value,
    _In_ UINT MantissaBits,
    _In_ BOOLEAN Signed)
{
    UINT Exponent = (Value >> MantissaBits) & 0x1F;
    UINT Mantissa = Value & ((1u << MantissaBits) - 1);
    UINT Sign = Signed ? (Value >> 15) & 1 : 0;
    UINT Bits;

    if (Exponent == 0x1F)
        Bits = 0x7F800000 | (Mantissa << (23 - MantissaBits));
    else if (Exponent != 0)
        Bits = ((Exponent + 112) << 23) | (Mantissa << (23 - MantissaBits));
    else if (Mantissa != 0)
        return (Sign ? -1.0f : 1.0f) * ldexpf((FLOAT)Mantissa, -14 - (INT)MantissaBits);
    else
        Bits = 0;
    return RxWarpBitsToFloat(Bits | (Sign << 31));
}

FLOAT
RxWarpHalfToFloat(
    _In_ UINT Half)
{
    return RxWarpFromSmallFloat(Half & 0xFFFF, 10, TRUE);
}

static
FLOAT
RxWarpSrgbToLinear(
    _In_ FLOAT Value)
{
    double Wide = Value;

    /* Worked in double and rounded once, as a correctly rounded table would hold */
    if (Wide <= 0.04045)
        return (FLOAT)(Wide / 12.92);
    return (FLOAT)pow((Wide + 0.055) / 1.055, 2.4);
}

static
FLOAT
RxWarpBitsAsFloat(
    _In_ UINT Bits)
{
    FLOAT Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

/*
 * Normalized integers to float the way the reference does: a power of two scale plus a small
 * correction term, two multiplies and an add, instead of one divide.
 */
FLOAT
RxWarpUnormToFloat(
    _In_ UINT Value,
    _In_ UINT Bits)
{
    static const struct
    {
        UINT8 Bits;
        UINT Scale;
        UINT Correction;
    } Factors[] =
    {
        { 1, 0x3F000000, 0x3F000000 }, { 2, 0x3E800000, 0x3DAAAAAC }, { 4, 0x3D800000, 0x3B888890 },
        { 5, 0x3D000000, 0x3A842100 }, { 6, 0x3C800000, 0x39820840 }, { 8, 0x3B800000, 0x37808100 },
        { 10, 0x3A800000, 0x35802000 }, { 16, 0x37800000, 0x2F800000 }, { 24, 0x33800000, 0x27800001 },
    };
    FLOAT Number = (FLOAT)Value;

    for (const auto &Factor : Factors)
    {
        if (Factor.Bits == Bits)
            return Number * RxWarpBitsAsFloat(Factor.Scale) + Number * RxWarpBitsAsFloat(Factor.Correction);
    }
    return Number / (FLOAT)((1ull << Bits) - 1);
}

FLOAT
RxWarpSnormToFloat(
    _In_ INT Value,
    _In_ UINT Bits)
{
    FLOAT Result;

    if (Bits == 8)
        Result = (FLOAT)Value * RxWarpBitsAsFloat(0x3C010204);
    else if (Bits == 16)
        Result = (FLOAT)Value * RxWarpBitsAsFloat(0x38000100);
    else
        Result = (FLOAT)Value / (FLOAT)((1ull << (Bits - 1)) - 1);
    return (Result < -1.0f) ? -1.0f : Result;
}

static const UINT RxWarpSrgbTable[256] =
{
#include "srgbtable.inl"
};

FLOAT
RxWarpSrgbByteToLinear(
    _In_ UINT Byte)
{
    FLOAT Value;

    memcpy(&Value, &RxWarpSrgbTable[Byte & 0xFF], sizeof(Value));
    return Value;
}

BOOLEAN
RxWarpDecodeTexel(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(16) const BYTE *Element,
    _Out_writes_(4) UINT *Value)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);
    BOOLEAN Integer = FALSE;
    UINT Offset = 0;
    UINT Packed;

    Value[0] = Value[1] = Value[2] = 0;
    Value[3] = RxWarpFloatToBits(1.0f);
    if (Info == nullptr)
        return FALSE;

    if (Info->Kind == RxKindR11G11B10)
    {
        memcpy(&Packed, Element, sizeof(Packed));
        Value[0] = RxWarpFloatToBits(RxWarpFromSmallFloat(Packed & 0x7FF, 6, FALSE));
        Value[1] = RxWarpFloatToBits(RxWarpFromSmallFloat((Packed >> 11) & 0x7FF, 6, FALSE));
        Value[2] = RxWarpFloatToBits(RxWarpFromSmallFloat(Packed >> 22, 5, FALSE));
        return TRUE;
    }
    if (Info->Kind == RxKindR9G9B9E5)
    {
        memcpy(&Packed, Element, sizeof(Packed));
        FLOAT Scale = ldexpf(1.0f, (INT)(Packed >> 27) - 15 - 9);

        Value[0] = RxWarpFloatToBits((FLOAT)(Packed & 0x1FF) * Scale);
        Value[1] = RxWarpFloatToBits((FLOAT)((Packed >> 9) & 0x1FF) * Scale);
        Value[2] = RxWarpFloatToBits((FLOAT)((Packed >> 18) & 0x1FF) * Scale);
        return TRUE;
    }
    if (Info->Kind != RxKindPlain)
        return FALSE;

    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        if (Info->Channels[Index].Type == RxChannelUint || Info->Channels[Index].Type == RxChannelSint)
            Integer = TRUE;
    }
    if (Integer)
        Value[3] = 1;

    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        const RX_WARP_CHANNEL &Channel = Info->Channels[Index];
        UINT64 Bits = RxWarpGetBits(Element, Offset, Channel.Bits);
        UINT Slot = (Channel.Source < 4) ? Channel.Source : Channel.Source - 4;
        FLOAT Max;

        Offset += Channel.Bits;
        switch (Channel.Type)
        {
            case RxChannelUnorm:
                /* Sampling divides exactly; the two term form is only for conversions */
                Max = (FLOAT)((1ull << Channel.Bits) - 1);
                Value[Slot] = RxWarpFloatToBits((FLOAT)Bits / Max);
                break;
            case RxChannelSrgb:
                Max = (FLOAT)((1ull << Channel.Bits) - 1);
                /* Alpha stays linear */
                if (Channel.Source == 3)
                    Value[Slot] = RxWarpFloatToBits((FLOAT)Bits / Max);
                else if (Channel.Bits == 8)
                    Value[Slot] = RxWarpFloatToBits(RxWarpSrgbByteToLinear((UINT)Bits));
                else
                    Value[Slot] = RxWarpFloatToBits(RxWarpSrgbToLinear((FLOAT)Bits / Max));
                break;
            case RxChannelSnorm:
            {
                INT Signed = (INT)(Bits << (32 - Channel.Bits)) >> (32 - Channel.Bits);

                Value[Slot] = RxWarpFloatToBits(RxWarpSnormToFloat(Signed, Channel.Bits));
                break;
            }
            case RxChannelFloat:
                Value[Slot] = (Channel.Bits == 32) ? (UINT)Bits : RxWarpFloatToBits(RxWarpHalfToFloat((UINT)Bits));
                break;
            case RxChannelUint:
                Value[Slot] = (UINT)Bits;
                break;
            case RxChannelSint:
                Value[Slot] = (Channel.Bits == 32) ? (UINT)Bits : (UINT)((INT)(Bits << (32 - Channel.Bits)) >> (32 - Channel.Bits));
                break;
            case RxChannelTypeless:
                Value[Slot] = (UINT)Bits;
                break;
            default:
                break;
        }
    }
    return TRUE;
}

BOOLEAN
RxWarpFormatIsInteger(
    _In_ DXGI_FORMAT Format)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);

    if (Info == nullptr || Info->Kind != RxKindPlain)
        return FALSE;
    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        if (Info->Channels[Index].Type == RxChannelUint || Info->Channels[Index].Type == RxChannelSint)
            return TRUE;
    }
    return FALSE;
}

UINT
RxWarpFloatToHalf(
    _In_ FLOAT Value)
{
    return RxWarpToHalf(Value);
}
