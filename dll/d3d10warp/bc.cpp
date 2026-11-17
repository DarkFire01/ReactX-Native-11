/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Block compressed texel decoding
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "format.hpp"

/* 565 endpoint to packed 8 bit RGBA (red in the low byte), bits replicated down */
static
UINT
RxWarpExpand565(
    _In_ UINT Color)
{
    UINT Red = (Color >> 11) & 0x1F;
    UINT Green = (Color >> 5) & 0x3F;
    UINT Blue = Color & 0x1F;

    Red = (Red << 3) | (Red >> 2);
    Green = (Green << 2) | (Green >> 4);
    Blue = (Blue << 3) | (Blue >> 2);
    return Red | (Green << 8) | (Blue << 16);
}

/*
 * The color a third of the way from From to To. Red and blue are worked as one packed
 * word, so a borrow from red reaches blue just as it does in the reference.
 */
static
UINT
RxWarpThird(
    _In_ UINT From,
    _In_ UINT To)
{
    UINT FromRb = From & 0x00FF00FF;
    UINT ToRb = To & 0x00FF00FF;
    UINT Rb = (FromRb + ((85u * (ToRb - FromRb)) >> 8)) & 0x00FF00FF;
    UINT FromGreen = (From >> 8) & 0xFF;
    UINT ToGreen = (To >> 8) & 0xFF;
    UINT Green = (((FromGreen << 8) + 85u * (ToGreen - FromGreen)) >> 8) & 0xFF;

    return Rb | (Green << 8);
}

/* The midpoint used by three color blocks, again with red and blue packed together */
static
UINT
RxWarpHalfway(
    _In_ UINT From,
    _In_ UINT To)
{
    UINT FromRb = From & 0x00FF00FF;
    UINT Rb = (FromRb + (((To & 0x00FF00FF) - FromRb) >> 1)) & 0x00FF00FF;
    UINT Green = ((((From >> 8) & 0xFF) + ((To >> 8) & 0xFF)) >> 1) & 0xFF;

    return Rb | (Green << 8);
}

/* Color of one texel of a BC1 style color block; Punch allows the three color mode */
static
UINT
RxWarpColorBlock(
    _In_reads_bytes_(8) const BYTE *Block,
    _In_ UINT Texel,
    _In_ BOOLEAN Punch)
{
    UINT First = Block[0] | (Block[1] << 8);
    UINT Second = Block[2] | (Block[3] << 8);
    UINT Indices = Block[4] | (Block[5] << 8) | (Block[6] << 16) | ((UINT)Block[7] << 24);
    UINT Palette[4];

    Palette[0] = RxWarpExpand565(First) | 0xFF000000;
    Palette[1] = RxWarpExpand565(Second) | 0xFF000000;
    if (!Punch || First > Second)
    {
        Palette[2] = RxWarpThird(Palette[0], Palette[1]) | 0xFF000000;
        Palette[3] = RxWarpThird(Palette[1], Palette[0]) | 0xFF000000;
    }
    else
    {
        Palette[2] = RxWarpHalfway(Palette[0], Palette[1]) | 0xFF000000;
        Palette[3] = 0;
    }
    return Palette[(Indices >> (Texel * 2)) & 3];
}

/* BC3 alpha: eight bit endpoints stepped in 8.24 fixed point, with half a unit added once */
static
UINT
RxWarpAlphaBlock(
    _In_reads_bytes_(8) const BYTE *Block,
    _In_ UINT Texel)
{
    UINT First = (UINT)Block[0] << 24;
    UINT Second = (UINT)Block[1] << 24;
    UINT64 Bits = 0;
    UINT Index;
    UINT Value;

    for (UINT Byte = 0; Byte < 6; Byte++)
        Bits |= (UINT64)Block[2 + Byte] << (Byte * 8);
    Index = (UINT)(Bits >> (Texel * 3)) & 7;

    if (Index == 0)
        return Block[0];
    if (Index == 1)
        return Block[1];
    if (First <= Second)
    {
        UINT Step = (Second - First) / 5;

        if (Index == 6)
            return 0;
        if (Index == 7)
            return 0xFF;
        Value = First + 0x800000 + (Index - 1) * Step;
    }
    else
    {
        UINT Step = (First - Second) / 7;

        Value = First + 0x800000 - (Index - 1) * Step;
    }
    return Value >> 24;
}

/* BC4 palettes in sixteen bits: unsigned endpoints scaled by 257, signed ones to 32767 */
static
INT
RxWarpRedBlock(
    _In_reads_bytes_(8) const BYTE *Block,
    _In_ UINT Texel,
    _In_ BOOLEAN Signed)
{
    UINT64 Bits = 0;
    UINT Index;

    for (UINT Byte = 0; Byte < 6; Byte++)
        Bits |= (UINT64)Block[2 + Byte] << (Byte * 8);
    Index = (UINT)(Bits >> (Texel * 3)) & 7;

    if (!Signed)
    {
        INT A = Block[0];
        INT B = Block[1];
        static const UINT8 Eight[8][2] = { { 1, 0 }, { 0, 1 }, { 6, 1 }, { 5, 2 }, { 4, 3 }, { 3, 4 }, { 2, 5 }, { 1, 6 } };
        static const UINT8 Six[6][2] = { { 1, 0 }, { 0, 1 }, { 4, 1 }, { 3, 2 }, { 2, 3 }, { 1, 4 } };

        if (Index < 2)
            return 257 * (Index ? B : A);
        if (A > B)
            return (257 * (Eight[Index][0] * A + Eight[Index][1] * B)) / 7;
        if (Index == 6)
            return 0;
        if (Index == 7)
            return 65535;
        return (257 * (Six[Index][0] * A + Six[Index][1] * B)) / 5;
    }
    else
    {
        INT A = (INT8)Block[0];
        INT B = (INT8)Block[1];
        static const UINT8 Eight[8][2] = { { 1, 0 }, { 0, 1 }, { 6, 1 }, { 5, 2 }, { 4, 3 }, { 3, 4 }, { 2, 5 }, { 1, 6 } };
        static const UINT8 Six[6][2] = { { 1, 0 }, { 0, 1 }, { 4, 1 }, { 3, 2 }, { 2, 3 }, { 1, 4 } };

        A = (A == -128) ? -127 : A;
        B = (B == -128) ? -127 : B;
        if (Index < 2)
            return (32767 * (Index ? B : A)) / 127;
        if (A > B)
            return (32767 * (Eight[Index][0] * A + Eight[Index][1] * B)) / 889;
        if (Index == 6)
            return -32767;
        if (Index == 7)
            return 32767;
        return (32767 * (Six[Index][0] * A + Six[Index][1] * B)) / 635;
    }
}

static
UINT
RxWarpFloatBits(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

static
FLOAT
RxWarpSrgbExpand(
    _In_ UINT Byte)
{
    FLOAT Value = (FLOAT)Byte / 255.0f;

    if (Value <= 0.04045f)
        return Value / 12.92f;
    return powf((Value + 0.055f) / 1.055f, 2.4f);
}

BOOLEAN
RxWarpDecodeBlockTexel(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(16) const BYTE *Block,
    _In_ UINT X,
    _In_ UINT Y,
    _Out_writes_(4) UINT *Value)
{
    UINT Texel = (Y & 3) * 4 + (X & 3);
    UINT Color;
    BOOLEAN Srgb = FALSE;

    Value[0] = Value[1] = Value[2] = 0;
    Value[3] = RxWarpFloatBits(1.0f);

    switch (Format)
    {
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            Srgb = TRUE;
            __fallthrough;
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
            Color = RxWarpColorBlock(Block, Texel, TRUE);
            break;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            Srgb = TRUE;
            __fallthrough;
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        {
            UINT Alpha = (Block[Texel / 2] >> ((Texel & 1) * 4)) & 0xF;

            Color = (RxWarpColorBlock(Block + 8, Texel, FALSE) & 0xFFFFFF) | ((Alpha | (Alpha << 4)) << 24);
            break;
        }
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            Srgb = TRUE;
            __fallthrough;
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
            Color = (RxWarpColorBlock(Block + 8, Texel, FALSE) & 0xFFFFFF) | (RxWarpAlphaBlock(Block, Texel) << 24);
            break;
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
            Value[0] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block, Texel, FALSE) / 65535.0f);
            return TRUE;
        case DXGI_FORMAT_BC4_SNORM:
            Value[0] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block, Texel, TRUE) / 32767.0f);
            return TRUE;
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
            Value[0] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block, Texel, FALSE) / 65535.0f);
            Value[1] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block + 8, Texel, FALSE) / 65535.0f);
            return TRUE;
        case DXGI_FORMAT_BC5_SNORM:
            Value[0] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block, Texel, TRUE) / 32767.0f);
            Value[1] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block + 8, Texel, TRUE) / 32767.0f);
            return TRUE;
        default:
            return FALSE;
    }

    for (UINT Component = 0; Component < 4; Component++)
    {
        UINT Byte = (Color >> (Component * 8)) & 0xFF;

        Value[Component] = RxWarpFloatBits((Srgb && Component < 3) ? RxWarpSrgbExpand(Byte) : (FLOAT)Byte / 255.0f);
    }
    return TRUE;
}
