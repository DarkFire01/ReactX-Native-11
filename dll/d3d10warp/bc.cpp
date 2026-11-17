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

#include "bptctables.inl"

/* Reads a block's bits from the lowest one up */
typedef struct _RX_WARP_BIT_READER
{
    const BYTE *Block;
    UINT Position;
} RX_WARP_BIT_READER;

static
UINT
RxWarpTakeBits(
    _Inout_ RX_WARP_BIT_READER *Reader,
    _In_ UINT Count)
{
    UINT Value = 0;

    for (UINT Bit = 0; Bit < Count && Reader->Position < 128; Bit++, Reader->Position++)
        Value |= (UINT)((Reader->Block[Reader->Position >> 3] >> (Reader->Position & 7)) & 1) << Bit;
    return Value;
}

static const UINT8 RxBptcWeights2[4] = { 0, 21, 43, 64 };
static const UINT8 RxBptcWeights3[8] = { 0, 9, 18, 27, 37, 46, 55, 64 };
static const UINT8 RxBptcWeights4[16] = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };

static
UINT
RxBptcWeight(
    _In_ UINT Bits,
    _In_ UINT Index)
{
    if (Bits == 2)
        return RxBptcWeights2[Index & 3];
    if (Bits == 3)
        return RxBptcWeights3[Index & 7];
    return RxBptcWeights4[Index & 15];
}

/* Whether a texel holds the shortened index of its subset */
static
BOOLEAN
RxBptcIsAnchor(
    _In_ UINT Subsets,
    _In_ UINT Shape,
    _In_ UINT Texel)
{
    for (UINT Subset = 0; Subset < Subsets; Subset++)
    {
        if (RxBptcAnchor[Subsets - 1][Shape][Subset] == Texel)
            return TRUE;
    }
    return FALSE;
}

/* BC7 modes: subsets, shape bits, rotation bits, selector bits, color bits, alpha bits, p bits, index bits, second index bits */
static const UINT8 RxBc7Modes[8][9] =
{
    { 3, 4, 0, 0, 4, 0, 6, 3, 0 },
    { 2, 6, 0, 0, 6, 0, 2, 3, 0 },
    { 3, 6, 0, 0, 5, 0, 0, 2, 0 },
    { 2, 6, 0, 0, 7, 0, 4, 2, 0 },
    { 1, 0, 2, 1, 5, 6, 0, 2, 3 },
    { 1, 0, 2, 0, 7, 8, 0, 2, 2 },
    { 1, 0, 0, 0, 7, 7, 2, 4, 0 },
    { 2, 6, 0, 0, 5, 5, 4, 2, 0 },
};

/* One BC7 texel as packed RGBA8; reserved modes read as transparent black */
static
UINT
RxWarpBc7Texel(
    _In_reads_bytes_(16) const BYTE *Block,
    _In_ UINT Texel)
{
    RX_WARP_BIT_READER Reader = { Block, 0 };
    const UINT8 *Mode;
    UINT ModeNumber = 0;
    UINT Endpoints;
    UINT Shape;
    UINT Rotation;
    UINT Selector;
    UINT Ends[6][4] = {};
    UINT Pbits[6] = {};
    UINT Subset;
    UINT ColorIndex = 0;
    UINT AlphaIndex = 0;
    UINT Pixel[4];

    while (ModeNumber < 8 && RxWarpTakeBits(&Reader, 1) == 0)
        ModeNumber++;
    if (ModeNumber >= 8)
        return 0;
    Mode = RxBc7Modes[ModeNumber];
    Endpoints = Mode[0] * 2;

    Shape = RxWarpTakeBits(&Reader, Mode[1]);
    Rotation = RxWarpTakeBits(&Reader, Mode[2]);
    Selector = RxWarpTakeBits(&Reader, Mode[3]);

    /* Every endpoint's red, then every green, blue and alpha */
    for (UINT Channel = 0; Channel < 4; Channel++)
    {
        UINT Bits = (Channel < 3) ? Mode[4] : Mode[5];

        for (UINT End = 0; End < Endpoints; End++)
            Ends[End][Channel] = Bits ? RxWarpTakeBits(&Reader, Bits) : 255;
    }
    for (UINT Bit = 0; Bit < Mode[6]; Bit++)
        Pbits[Bit] = RxWarpTakeBits(&Reader, 1);

    /* Append the p bit, then widen to eight bits by repeating the top bits */
    for (UINT End = 0; End < Endpoints; End++)
    {
        for (UINT Channel = 0; Channel < 4; Channel++)
        {
            UINT Bits = (Channel < 3) ? Mode[4] : Mode[5];
            UINT Value = Ends[End][Channel];

            if (Bits == 0)
                continue;
            if (Mode[6] != 0)
            {
                Value = ((Value << 1) & 0xFF) | Pbits[End * Mode[6] / Endpoints];
                Bits++;
            }
            Value = (Value << (8 - Bits)) & 0xFF;
            Ends[End][Channel] = Value | (Value >> Bits);
        }
    }

    for (UINT Index = 0; Index < 16; Index++)
    {
        UINT Bits = Mode[7] - (RxBptcIsAnchor(Mode[0], Shape, Index) ? 1 : 0);
        UINT Value = RxWarpTakeBits(&Reader, Bits);

        if (Index == Texel)
            ColorIndex = Value;
    }
    if (Mode[8] != 0)
    {
        for (UINT Index = 0; Index < 16; Index++)
        {
            UINT Value = RxWarpTakeBits(&Reader, Mode[8] - ((Index == 0) ? 1 : 0));

            if (Index == Texel)
                AlphaIndex = Value;
        }
    }

    Subset = RxBptcShape[Mode[0] - 1][Shape][Texel];
    for (UINT Channel = 0; Channel < 4; Channel++)
    {
        BOOLEAN UseSecond = (Mode[8] != 0) && ((Channel == 3) != (Selector != 0));
        UINT Bits = UseSecond ? Mode[8] : Mode[7];
        UINT Weight = RxBptcWeight(Bits, UseSecond ? AlphaIndex : ColorIndex);
        UINT First = Ends[Subset * 2][Channel];
        UINT Second = Ends[Subset * 2 + 1][Channel];

        Pixel[Channel] = (First * (64 - Weight) + Second * Weight + 32) >> 6;
    }
    if (Rotation != 0)
    {
        UINT Swap = Pixel[3];

        Pixel[3] = Pixel[Rotation - 1];
        Pixel[Rotation - 1] = Swap;
    }
    return Pixel[0] | (Pixel[1] << 8) | (Pixel[2] << 16) | (Pixel[3] << 24);
}

static
INT
RxBc6hSignExtend(
    _In_ INT Value,
    _In_ UINT Bits)
{
    if (Bits == 0 || Bits >= 32)
        return Value;
    return (Value & (1 << (Bits - 1))) ? (Value | ~((1 << Bits) - 1)) : Value;
}

/* Endpoint value to the 16 bit interpolation range */
static
INT
RxBc6hExpand(
    _In_ INT Value,
    _In_ UINT Bits,
    _In_ BOOLEAN Signed)
{
    if (Signed)
    {
        BOOLEAN Negative = (Value < 0);
        INT Magnitude = Negative ? -Value : Value;
        INT Result;

        if (Bits >= 16)
            return Value;
        if (Magnitude == 0)
            Result = 0;
        else if (Magnitude >= (1 << (Bits - 1)) - 1)
            Result = 0x7FFF;
        else
            Result = ((Magnitude << 15) + 0x4000) >> (Bits - 1);
        return Negative ? -Result : Result;
    }
    if (Bits >= 15)
        return Value;
    if (Value == 0)
        return 0;
    if (Value == (1 << Bits) - 1)
        return 0xFFFF;
    return ((Value << 16) + 0x8000) >> Bits;
}

/* One BC6H texel as three half floats; invalid blocks read as opaque black */
static
BOOLEAN
RxWarpBc6hTexel(
    _In_reads_bytes_(16) const BYTE *Block,
    _In_ UINT Texel,
    _In_ BOOLEAN Signed,
    _Out_writes_(3) UINT16 *Half)
{
    RX_WARP_BIT_READER Reader = { Block, 0 };
    const UINT8 *Mode;
    UINT ModeBits;
    INT Info;
    UINT Regions;
    UINT HeaderBits;
    UINT Shape = 0;
    INT Ends[4][3] = {};
    UINT ColorIndex = 0;
    UINT Region;

    Half[0] = Half[1] = Half[2] = 0;
    ModeBits = RxWarpTakeBits(&Reader, 2);
    if (ModeBits >= 2)
        ModeBits |= RxWarpTakeBits(&Reader, 3) << 2;
    Info = RxBc6hModeIndex[ModeBits];
    if (Info < 0)
        return FALSE;
    Mode = RxBc6hModes[Info];
    Regions = Mode[1] + 1;
    HeaderBits = (Regions > 1) ? 82 : 65;

    /* Header bits scatter into the shape and the endpoints by the mode's layout */
    while (Reader.Position < HeaderBits)
    {
        UINT At = Reader.Position;
        UINT Field = RxBc6hLayout[Info][At][0];
        UINT Bit = RxBc6hLayout[Info][At][1];

        if (!RxWarpTakeBits(&Reader, 1))
            continue;
        if (Field == 2)
            Shape |= 1u << Bit;
        else if (Field >= 3 && Field <= 14)
            Ends[(Field - 3) % 4][(Field - 3) / 4] |= 1 << Bit;
        else
            return FALSE;
    }

    /* Ends: region 0 first and second, region 1 first and second; precisions follow that order */
    for (UINT End = 0; End < Regions * 2; End++)
    {
        BOOLEAN Delta = (End != 0) && Mode[2];

        for (UINT Channel = 0; Channel < 3; Channel++)
        {
            if (Signed || Delta)
                Ends[End][Channel] = RxBc6hSignExtend(Ends[End][Channel], Mode[4 + End * 4 + Channel]);
        }
    }

    /* Transformed modes store the other ends as offsets from the first */
    if (Mode[2])
    {
        for (UINT End = 1; End < Regions * 2; End++)
        {
            for (UINT Channel = 0; Channel < 3; Channel++)
            {
                UINT Bits = Mode[4 + Channel];
                INT Value = (Ends[End][Channel] + Ends[0][Channel]) & ((1 << Bits) - 1);

                Ends[End][Channel] = Signed ? RxBc6hSignExtend(Value, Bits) : Value;
            }
        }
    }

    for (UINT Index = 0; Index < 16; Index++)
    {
        UINT Bits = Mode[3] - (RxBptcIsAnchor(Regions, Shape, Index) ? 1 : 0);
        UINT Value = RxWarpTakeBits(&Reader, Bits);

        if (Index == Texel)
            ColorIndex = Value;
    }

    Region = RxBptcShape[Regions - 1][Shape][Texel];
    for (UINT Channel = 0; Channel < 3; Channel++)
    {
        INT First = RxBc6hExpand(Ends[Region * 2][Channel], Mode[4 + Channel], Signed);
        INT Second = RxBc6hExpand(Ends[Region * 2 + 1][Channel], Mode[4 + Channel], Signed);
        INT Weight = (INT)RxBptcWeight(Mode[3], ColorIndex);
        INT Value = (First * (64 - Weight) + Second * Weight + 32) >> 6;

        /* Scale to the largest finite half, keeping the sign apart */
        if (Signed)
        {
            Value = (Value < 0) ? -(((-Value) * 31) >> 5) : (Value * 31) >> 5;
            Half[Channel] = (UINT16)((Value < 0) ? (0x8000 | -Value) : Value);
        }
        else
        {
            Half[Channel] = (UINT16)((Value * 31) >> 6);
        }
    }
    return TRUE;
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
            Value[0] = RxWarpFloatBits(RxWarpSnormToFloat(RxWarpRedBlock(Block, Texel, TRUE), 16));
            return TRUE;
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
            Value[0] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block, Texel, FALSE) / 65535.0f);
            Value[1] = RxWarpFloatBits((FLOAT)RxWarpRedBlock(Block + 8, Texel, FALSE) / 65535.0f);
            return TRUE;
        case DXGI_FORMAT_BC5_SNORM:
            Value[0] = RxWarpFloatBits(RxWarpSnormToFloat(RxWarpRedBlock(Block, Texel, TRUE), 16));
            Value[1] = RxWarpFloatBits(RxWarpSnormToFloat(RxWarpRedBlock(Block + 8, Texel, TRUE), 16));
            return TRUE;
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        {
            UINT16 Half[3];

            RxWarpBc6hTexel(Block, Texel, Format == DXGI_FORMAT_BC6H_SF16, Half);
            for (UINT Component = 0; Component < 3; Component++)
                Value[Component] = RxWarpFloatBits(RxWarpHalfToFloat(Half[Component]));
            return TRUE;
        }
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            Srgb = TRUE;
            __fallthrough;
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
            Color = RxWarpBc7Texel(Block, Texel);
            break;
        default:
            return FALSE;
    }

    for (UINT Component = 0; Component < 4; Component++)
    {
        UINT Byte = (Color >> (Component * 8)) & 0xFF;

        Value[Component] = RxWarpFloatBits((Srgb && Component < 3) ? RxWarpSrgbByteToLinear(Byte) : (FLOAT)Byte / 255.0f);
    }
    return TRUE;
}
