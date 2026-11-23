/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Texture sampling, texel loads and resource queries for shaders
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "sample.hpp"

/* The part of a resource a view sees */
typedef struct _RX_WARP_VIEW_EXTENT
{
    RxWarpResource *Resource;
    UINT FirstMip;
    UINT MipCount;
    UINT FirstSlice;
    UINT SliceCount;
} RX_WARP_VIEW_EXTENT;

static
BOOLEAN
RxWarpViewExtent(
    _In_opt_ RxWarpView *View,
    _Out_ RX_WARP_VIEW_EXTENT *Extent)
{
    ZeroMemory(Extent, sizeof(*Extent));
    if (View == nullptr || View->Resource == nullptr)
        return FALSE;

    Extent->Resource = View->Resource;
    Extent->FirstMip = View->MipSlice;
    if (Extent->FirstMip >= View->Resource->MipLevels)
        return FALSE;
    Extent->MipCount = View->Resource->MipLevels - Extent->FirstMip;
    if (View->MipCount != 0 && View->MipCount < Extent->MipCount)
        Extent->MipCount = View->MipCount;
    Extent->FirstSlice = View->FirstSlice;
    Extent->SliceCount = View->SliceCount ? View->SliceCount : 1;
    return TRUE;
}

static
RX_WARP_SUBRESOURCE *
RxWarpExtentLevel(
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ UINT Mip,
    _In_ UINT Slice)
{
    RxWarpResource *Resource = Extent->Resource;
    UINT ArraySlice = (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? 0 : Extent->FirstSlice + Slice;

    return Resource->Subresource(ArraySlice * Resource->MipLevels + Extent->FirstMip + Mip);
}

/* Reads one texel as raw component bits; FALSE when the location is outside the level */
static
BOOLEAN
RxWarpReadTexel(
    _In_ RxWarpView *View,
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ UINT Mip,
    _In_ UINT Slice,
    _In_ INT X,
    _In_ INT Y,
    _In_ INT Z,
    _Out_writes_(4) UINT *Value,
    _In_ UINT Sample = 0)
{
    RX_WARP_SUBRESOURCE *Level = RxWarpExtentLevel(Extent, Mip, Slice);
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
    BYTE Element[16] = {};
    const BYTE *Source;

    if (Level == nullptr || Info == nullptr || X < 0 || Y < 0 || Z < 0 ||
        (UINT)X >= Level->Width || (UINT)Y >= Level->Height || (UINT)Z >= Level->Depth)
    {
        Value[0] = Value[1] = Value[2] = Value[3] = 0;
        return FALSE;
    }

    /* Block formats store a row of 4x4 blocks per row pitch */
    if (Info->Kind == RxKindBlock)
    {
        Source = Extent->Resource->Memory + Level->Offset + (SIZE_T)Z * Level->DepthPitch + (SIZE_T)(Y / 4) * Level->RowPitch +
                 (SIZE_T)(X / 4) * Info->Bytes;
        memcpy(Element, Source, Info->Bytes);
        return RxWarpDecodeBlockTexel(View->Format, Element, X, Y, Value);
    }

    /* Multisampled texels keep their samples together */
    if (Sample >= Extent->Resource->SampleCount)
    {
        Value[0] = Value[1] = Value[2] = Value[3] = 0;
        return FALSE;
    }
    Source = Extent->Resource->Memory + Level->Offset + (SIZE_T)Z * Level->DepthPitch + (SIZE_T)Y * Level->RowPitch +
             ((SIZE_T)X * Extent->Resource->SampleCount + Sample) * Info->Bytes;
    memcpy(Element, Source, (Info->Bytes <= 16) ? Info->Bytes : 16);
    return RxWarpDecodeTexel(View->Format, Element, Value);
}

/* Not a number or infinite: addressing treats these as zero */
static
BOOLEAN
RxWarpUnaddressable(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    return (Bits & 0x7F800000) == 0x7F800000;
}

static
FLOAT
RxWarpFraction(
    _In_ FLOAT Value)
{
    return (fabsf(Value) <= 8388600.0f) ? Value - floorf(Value) : 0.0f;
}

/*
 * Texture coordinates become texel positions with eight fraction bits, as the reference
 * computes them: the normalized coordinate is folded into range by the address mode first and
 * only then scaled by the size.
 */
static
VOID
RxWarpAddressPoint(
    _In_ FLOAT Coordinate,
    _In_ INT Size,
    _In_ UINT Mode,
    _In_ INT Offset,
    _Out_ INT *Index,
    _Out_ BOOLEAN *Border)
{
    FLOAT Scale = (FLOAT)Size * 256.0f;
    INT Fixed;
    INT Texel;

    *Border = FALSE;
    *Index = 0;
    if (RxWarpUnaddressable(Coordinate))
        return;

    switch (Mode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:
            Fixed = (INT)floorf(Scale * RxWarpFraction(Coordinate)) + Offset * 256;
            Texel = Fixed >> 8;
            Texel %= Size;
            *Index = (Texel < 0) ? Texel + Size : Texel;
            return;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:
        {
            INT Period = Size * 2;

            Fixed = (INT)floorf(((FLOAT)Period * 256.0f) * RxWarpFraction(Coordinate * 0.5f)) + Offset * 256;
            Texel = (Fixed >> 8) % Period;
            if (Texel < 0)
                Texel += Period;
            *Index = (Texel >= Size) ? Period - 1 - Texel : Texel;
            return;
        }
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:
        {
            FLOAT Clamped = (Coordinate > -1.0f) ? Coordinate : -1.0f;

            Clamped = (Clamped >= 2.0f) ? 2.0f : Clamped;
            Texel = (((INT)floorf(Scale * Clamped)) >> 8) + Offset;
            *Border = (Texel < 0 || Texel > Size - 1);
            *Index = *Border ? 0 : Texel;
            return;
        }
        case D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE:
        {
            FLOAT Folded = fabsf(Coordinate);

            Folded = (Folded >= 1.0f) ? 1.0f : Folded;
            Texel = (((INT)floorf(Scale * Folded)) >> 8) + Offset;
            *Index = (Texel < 0) ? 0 : ((Texel > Size - 1) ? Size - 1 : Texel);
            return;
        }
        default:
        {
            /* Offsets move the texel before it clamps; the coordinate is only bounded to keep the integer small */
            FLOAT Clamped = (Coordinate > -1.0f) ? Coordinate : -1.0f;

            Clamped = (Clamped >= 2.0f) ? 2.0f : Clamped;
            Texel = (((INT)floorf(Scale * Clamped)) >> 8) + Offset;
            *Index = (Texel < 0) ? 0 : ((Texel > Size - 1) ? Size - 1 : Texel);
            return;
        }
    }
}

/* The two texels a linear filter blends and the eight bit weight of the second */
typedef struct _RX_WARP_TAPS
{
    INT First;
    INT Second;
    UINT Weight;
    BOOLEAN FirstBorder;
    BOOLEAN SecondBorder;
} RX_WARP_TAPS;

static
VOID
RxWarpAddressLinear(
    _In_ FLOAT Coordinate,
    _In_ INT Size,
    _In_ UINT Mode,
    _In_ INT Offset,
    _Out_ RX_WARP_TAPS *Taps)
{
    FLOAT Scale = (FLOAT)Size * 256.0f;
    FLOAT Folded;
    INT Fixed;

    ZeroMemory(Taps, sizeof(*Taps));
    if (RxWarpUnaddressable(Coordinate))
        return;

    switch (Mode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:
            Folded = RxWarpFraction(Coordinate);
            break;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:
            Folded = RxWarpFraction(Coordinate * 0.5f);
            Scale = (FLOAT)Size * 512.0f;
            break;
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:
            Folded = (Coordinate > -1.0f) ? Coordinate : -1.0f;
            Folded = (Folded >= 2.0f) ? 2.0f : Folded;
            break;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE:
            Folded = fabsf(Coordinate);
            Folded = (Folded >= 1.0f) ? 1.0f : Folded;
            break;
        default:
            Folded = (Coordinate > -1.0f) ? Coordinate : -1.0f;
            Folded = (Folded >= 2.0f) ? 2.0f : Folded;
            break;
    }

    /* Texel space less half a texel, in 8.8 and floored */
    Fixed = (INT)floorf((Folded * (Scale * (1.0f / 256.0f)) - 0.5f) * 256.0f) + Offset * 256;
    Taps->First = Fixed >> 8;
    Taps->Second = Taps->First + 1;
    Taps->Weight = (UINT)Fixed & 0xFF;

    switch (Mode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:
            Taps->First = ((Taps->First % Size) + Size) % Size;
            Taps->Second = ((Taps->Second % Size) + Size) % Size;
            break;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:
        {
            INT Period = Size * 2;

            Taps->First = ((Taps->First % Period) + Period) % Period;
            Taps->Second = ((Taps->Second % Period) + Period) % Period;
            if (Taps->First >= Size)
                Taps->First = Period - 1 - Taps->First;
            if (Taps->Second >= Size)
                Taps->Second = Period - 1 - Taps->Second;
            break;
        }
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:
            Taps->FirstBorder = (Taps->First < 0 || Taps->First > Size - 1);
            Taps->SecondBorder = (Taps->Second < 0 || Taps->Second > Size - 1);
            break;
        default:
            Taps->First = (Taps->First < 0) ? 0 : ((Taps->First > Size - 1) ? Size - 1 : Taps->First);
            Taps->Second = (Taps->Second < 0) ? 0 : ((Taps->Second > Size - 1) ? Size - 1 : Taps->Second);
            break;
    }
}

static
FLOAT
RxWarpBitsFloat(
    _In_ UINT Bits)
{
    FLOAT Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
BOOLEAN
RxWarpCompare(
    _In_ UINT Function,
    _In_ FLOAT Reference,
    _In_ FLOAT Texel)
{
    switch (Function)
    {
        case D3D10_DDI_COMPARISON_NEVER:
            return FALSE;
        case D3D10_DDI_COMPARISON_LESS:
            return Reference < Texel;
        case D3D10_DDI_COMPARISON_EQUAL:
            return Reference == Texel;
        case D3D10_DDI_COMPARISON_LESS_EQUAL:
            return Reference <= Texel;
        case D3D10_DDI_COMPARISON_GREATER:
            return Reference > Texel;
        case D3D10_DDI_COMPARISON_NOT_EQUAL:
            return Reference != Texel;
        case D3D10_DDI_COMPARISON_GREATER_EQUAL:
            return Reference >= Texel;
        default:
            return TRUE;
    }
}

/* Shape of the texture as the shader addresses it */
typedef struct _RX_WARP_SHAPE
{
    UINT Axes;              /* 1, 2 or 3 coordinates */
    BOOLEAN Array;
    BOOLEAN Cube;
    UINT SliceComponent;
} RX_WARP_SHAPE;

static
RX_WARP_SHAPE
RxWarpShapeOf(
    _In_ UINT Dimension)
{
    RX_WARP_SHAPE Shape = { 2, FALSE, FALSE, 2 };

    switch (Dimension)
    {
        case D3D10_SB_RESOURCE_DIMENSION_BUFFER:
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE1D:
            Shape.Axes = 1;
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY:
            Shape.Axes = 1;
            Shape.Array = TRUE;
            Shape.SliceComponent = 1;
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY:
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY:
            Shape.Array = TRUE;
            Shape.SliceComponent = 2;
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D:
            Shape.Axes = 3;
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE:
            Shape.Cube = TRUE;
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBEARRAY:
            Shape.Cube = TRUE;
            Shape.Array = TRUE;
            Shape.SliceComponent = 3;
            break;
        default:
            break;
    }
    return Shape;
}

/* Cube direction to a face and its 0..1 coordinates */
static
UINT
RxWarpCubeFace(
    _In_reads_(3) const FLOAT *Direction,
    _Out_writes_(2) FLOAT *Coordinate)
{
    FLOAT X = Direction[0];
    FLOAT Y = Direction[1];
    FLOAT Z = Direction[2];
    FLOAT Ax = fabsf(X);
    FLOAT Ay = fabsf(Y);
    FLOAT Az = fabsf(Z);
    FLOAT S;
    FLOAT T;
    FLOAT Major;
    UINT Face;

    if (Ax >= Ay && Ax >= Az)
    {
        Face = (X >= 0.0f) ? 0 : 1;
        S = (X >= 0.0f) ? -Z : Z;
        T = -Y;
        Major = Ax;
    }
    else if (Ay >= Az)
    {
        Face = (Y >= 0.0f) ? 2 : 3;
        S = X;
        T = (Y >= 0.0f) ? Z : -Z;
        Major = Ay;
    }
    else
    {
        Face = (Z >= 0.0f) ? 4 : 5;
        S = (Z >= 0.0f) ? X : -X;
        T = -Y;
        Major = Az;
    }
    if (Major == 0.0f)
        Major = 1.0f;
    Coordinate[0] = (S / Major + 1.0f) * 0.5f;
    Coordinate[1] = (T / Major + 1.0f) * 0.5f;
    return Face;
}

/* Eight bit normalized formats, which the reference filters in fixed point */
static
BOOLEAN
RxWarpByteFormat(
    _In_ DXGI_FORMAT Format)
{
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Format);

    if (Info == nullptr || Info->Kind != RxKindPlain || Info->ChannelCount == 0)
        return FALSE;
    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        if (Info->Channels[Index].Bits != 8 ||
            (Info->Channels[Index].Type != RxChannelUnorm && Info->Channels[Index].Type != RxChannelUnused))
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* The border color as the texture format would store it */
static
VOID
RxWarpQuantizedBorder(
    _In_ DXGI_FORMAT Format,
    _In_reads_(4) const FLOAT *Color,
    _Out_writes_(4) FLOAT *Result)
{
    BYTE Element[16];
    UINT Raw[4];

    if (RxWarpFormatInfo(Format) == nullptr || RxWarpFormatIsInteger(Format) || !RxWarpEncodeColor(Format, Color, Element))
    {
        memcpy(Result, Color, 4 * sizeof(FLOAT));
        return;
    }
    RxWarpDecodeTexel(Format, Element, Raw);
    memcpy(Result, Raw, 4 * sizeof(FLOAT));
}

/* A texel or the quantized border color as eight bit channel values */
static
VOID
RxWarpFetchBytes(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ UINT Mip,
    _In_ UINT Slice,
    _In_reads_(3) const INT *At,
    _In_ BOOLEAN Border,
    _Out_writes_(4) INT *Bytes)
{
    FLOAT Color[4];
    UINT Raw[4];

    if (Border)
        RxWarpQuantizedBorder(Request->View->Format, Request->Sampler->Desc.BorderColor, Color);
    else
    {
        RxWarpReadTexel(Request->View, Extent, Mip, Slice, At[0], At[1], At[2], Raw);
        memcpy(Color, Raw, sizeof(Color));
    }
    for (UINT Component = 0; Component < 4; Component++)
        Bytes[Component] = (INT)nearbyintf(Color[Component] * 255.0f);
}

/*
 * The reference's eight bit filter: seven bit weights, eight bit values with seven fraction
 * bits, the second step floored, and the result scaled to float in two terms: 1/32768 and 1/(32768 * 255).
 */
static BOOLEAN RxWarpByteFilterValid;
static INT RxWarpByteFilterFixed[4];

BOOLEAN
RxWarpLastByteFilter(
    _Out_writes_(4) INT *Fixed)
{
    memcpy(Fixed, RxWarpByteFilterFixed, sizeof(RxWarpByteFilterFixed));
    return RxWarpByteFilterValid;
}

FLOAT
RxWarpByteResult(
    _In_ INT Fixed)
{
    FLOAT Value = (FLOAT)Fixed;
    UINT Bits = 0x34008081;
    FLOAT Correction;

    memcpy(&Correction, &Bits, sizeof(Correction));
    return Value * (1.0f / 32768.0f) + Value * Correction;
}

/* Reads one texel or the border color as four floats, compared when the sampler compares */
static
VOID
RxWarpFetch(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ UINT Mip,
    _In_ UINT Slice,
    _In_reads_(3) const INT *At,
    _In_ BOOLEAN Border,
    _Out_writes_(4) FLOAT *Texel)
{
    const D3D10_DDI_SAMPLER_DESC *Desc = &Request->Sampler->Desc;
    UINT Raw[4];

    if (Border)
    {
        RxWarpQuantizedBorder(Request->View->Format, Desc->BorderColor, Texel);
    }
    else
    {
        RxWarpReadTexel(Request->View, Extent, Mip, Slice, At[0], At[1], At[2], Raw);
        for (UINT Component = 0; Component < 4; Component++)
            Texel[Component] = RxWarpBitsFloat(Raw[Component]);
    }
    if (Request->Compare)
    {
        FLOAT Pass = RxWarpCompare(Desc->ComparisonFunc, Request->Reference, Texel[0]) ? 1.0f : 0.0f;

        Texel[0] = Texel[1] = Texel[2] = Texel[3] = Pass;
    }
}

/* One filtered lookup at a single mip level */
static
VOID
RxWarpFilterLevel(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ const RX_WARP_SHAPE *Shape,
    _In_ UINT Mip,
    _In_ UINT Slice,
    _In_reads_(3) const FLOAT *Coordinate,
    _In_ BOOLEAN Linear,
    _Out_writes_(4) FLOAT *Result)
{
    const D3D10_DDI_SAMPLER_DESC *Desc = &Request->Sampler->Desc;
    RX_WARP_SUBRESOURCE *Level = RxWarpExtentLevel(Extent, Mip, Slice);
    UINT Modes[3] = { (UINT)Desc->AddressU, (UINT)Desc->AddressV, (UINT)Desc->AddressW };
    INT Size[3];
    RX_WARP_TAPS Taps[3] = {};
    UINT Axes = Shape->Axes;

    Result[0] = Result[1] = Result[2] = Result[3] = 0.0f;
    if (Level == nullptr)
        return;

    Size[0] = (INT)Level->Width;
    Size[1] = (INT)Level->Height;
    Size[2] = (INT)Level->Depth;
    if (Shape->Cube)
        Modes[0] = Modes[1] = D3D10_DDI_TEXTURE_ADDRESS_CLAMP;

    if (!Linear && !Request->Gather)
    {
        INT At[3] = {};
        BOOLEAN Border = FALSE;

        for (UINT Axis = 0; Axis < Axes; Axis++)
        {
            BOOLEAN Outside;

            RxWarpAddressPoint(Coordinate[Axis], Size[Axis], Modes[Axis], Request->Offset[Axis], &At[Axis], &Outside);
            Border |= Outside;
        }
        RxWarpFetch(Request, Extent, Mip, Slice, At, Border, Result);
        return;
    }

    for (UINT Axis = 0; Axis < 3; Axis++)
    {
        if (Axis < Axes)
            RxWarpAddressLinear(Coordinate[Axis], Size[Axis], Modes[Axis], Request->Offset[Axis], &Taps[Axis]);
    }

    /* Gather: the four texels of the footprint, ordered (0,1), (1,1), (1,0), (0,0) */
    if (Request->Gather)
    {
        static const UINT Order[4][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };

        for (UINT Index = 0; Index < 4; Index++)
        {
            INT At[3] = { Order[Index][0] ? Taps[0].Second : Taps[0].First, Order[Index][1] ? Taps[1].Second : Taps[1].First, 0 };
            BOOLEAN Border = (Order[Index][0] ? Taps[0].SecondBorder : Taps[0].FirstBorder) ||
                             (Order[Index][1] ? Taps[1].SecondBorder : Taps[1].FirstBorder);
            FLOAT Texel[4];

            RxWarpFetch(Request, Extent, Mip, Slice, At, Border, Texel);
            Result[Index] = Texel[Request->GatherComponent & 3];
        }
        return;
    }

    /* Eight bit normalized textures filter in fixed point, horizontally first */
    if (Axes == 2 && !Request->Compare && RxWarpByteFormat(Request->View->Format))
    {
        INT Corner[2][2][4];
        INT Wx = (INT)Taps[0].Weight;
        INT Wy = (INT)Taps[1].Weight;

        for (UINT Ty = 0; Ty < 2; Ty++)
        {
            for (UINT Tx = 0; Tx < 2; Tx++)
            {
                INT At[3] = { Tx ? Taps[0].Second : Taps[0].First, Ty ? Taps[1].Second : Taps[1].First, 0 };
                BOOLEAN Border = (Tx ? Taps[0].SecondBorder : Taps[0].FirstBorder) || (Ty ? Taps[1].SecondBorder : Taps[1].FirstBorder);

                RxWarpFetchBytes(Request, Extent, Mip, Slice, At, Border, Corner[Ty][Tx]);
            }
        }
        for (UINT Component = 0; Component < 4; Component++)
        {
            INT Top = Corner[0][0][Component] * 256 + (Corner[0][1][Component] - Corner[0][0][Component]) * Wx;
            INT Bottom = Corner[1][0][Component] * 256 + (Corner[1][1][Component] - Corner[1][0][Component]) * Wx;

            RxWarpByteFilterFixed[Component] = (Top + (((Bottom - Top) * Wy) >> 8)) >> 1;
            Result[Component] = RxWarpByteResult(RxWarpByteFilterFixed[Component]);
        }
        RxWarpByteFilterValid = TRUE;
        return;
    }

    /* Everything else blends in float with the same eight bit weights */
    {
        FLOAT Corners[2][2][2][4];

        for (UINT Tz = 0; Tz < ((Axes > 2) ? 2u : 1u); Tz++)
        {
            for (UINT Ty = 0; Ty < ((Axes > 1) ? 2u : 1u); Ty++)
            {
                for (UINT Tx = 0; Tx < 2; Tx++)
                {
                    INT At[3] = { Tx ? Taps[0].Second : Taps[0].First, (Axes > 1) ? (Ty ? Taps[1].Second : Taps[1].First) : 0,
                                  (Axes > 2) ? (Tz ? Taps[2].Second : Taps[2].First) : 0 };
                    BOOLEAN Border = (Tx ? Taps[0].SecondBorder : Taps[0].FirstBorder) ||
                                     (Axes > 1 && (Ty ? Taps[1].SecondBorder : Taps[1].FirstBorder)) ||
                                     (Axes > 2 && (Tz ? Taps[2].SecondBorder : Taps[2].FirstBorder));

                    RxWarpFetch(Request, Extent, Mip, Slice, At, Border, Corners[Tz][Ty][Tx]);
                }
            }
        }

        for (UINT Component = 0; Component < 4; Component++)
        {
            FLOAT Wx = (FLOAT)Taps[0].Weight * (1.0f / 256.0f);
            FLOAT Wy = (FLOAT)Taps[1].Weight * (1.0f / 256.0f);
            FLOAT Wz = (FLOAT)Taps[2].Weight * (1.0f / 256.0f);
            FLOAT Plane[2];

            for (UINT Tz = 0; Tz < ((Axes > 2) ? 2u : 1u); Tz++)
            {
                FLOAT Top = Corners[Tz][0][0][Component] + (Corners[Tz][0][1][Component] - Corners[Tz][0][0][Component]) * Wx;

                if (Axes > 1)
                {
                    FLOAT Bottom = Corners[Tz][1][0][Component] + (Corners[Tz][1][1][Component] - Corners[Tz][1][0][Component]) * Wx;

                    Top = Top + (Bottom - Top) * Wy;
                }
                Plane[Tz] = Top;
            }
            Result[Component] = (Axes > 2) ? Plane[0] + (Plane[1] - Plane[0]) * Wz : Plane[0];
        }
    }
}

/* Level of detail before clamping to the view's mips, from screen space derivatives */
static
FLOAT
RxWarpRawLod(
    _In_ const RX_WARP_VIEW_EXTENT *Extent,
    _In_ const RX_WARP_SHAPE *Shape,
    _In_reads_(3) const FLOAT *Dx,
    _In_reads_(3) const FLOAT *Dy)
{
    RX_WARP_SUBRESOURCE *Level = RxWarpExtentLevel(Extent, 0, 0);
    FLOAT Size[3];
    FLOAT LengthX = 0.0f;
    FLOAT LengthY = 0.0f;
    UINT Axes = Shape->Cube ? 3 : Shape->Axes;
    FLOAT Rho;

    if (Level == nullptr)
        return 0.0f;
    Size[0] = (FLOAT)Level->Width;
    Size[1] = Shape->Cube ? (FLOAT)Level->Width : (FLOAT)Level->Height;
    Size[2] = Shape->Cube ? (FLOAT)Level->Width : (FLOAT)Level->Depth;

    for (UINT Axis = 0; Axis < Axes; Axis++)
    {
        FLOAT X = Dx[Axis] * Size[Axis];
        FLOAT Y = Dy[Axis] * Size[Axis];

        LengthX += X * X;
        LengthY += Y * Y;
    }
    Rho = sqrtf((LengthX > LengthY) ? LengthX : LengthY);
    if (Shape->Cube)
        Rho *= 0.5f;
    if (Rho <= 0.0f)
        return -INFINITY;
    return log2f(Rho);
}

static
FLOAT
RxWarpClampLod(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_ FLOAT Lod)
{
    const D3D10_DDI_SAMPLER_DESC *Desc = &Request->Sampler->Desc;

    if (Lod < Desc->MinLOD)
        Lod = Desc->MinLOD;
    if (Lod > Desc->MaxLOD)
        Lod = Desc->MaxLOD;
    return Lod;
}

VOID
RxWarpComputeLod(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(3) const FLOAT *Dx,
    _In_reads_(3) const FLOAT *Dy,
    _Out_writes_(4) FLOAT *Result)
{
    RX_WARP_VIEW_EXTENT Extent;
    RX_WARP_SHAPE Shape = RxWarpShapeOf(Request->Dimension);
    FLOAT Lod;
    FLOAT Clamped;

    Result[0] = Result[1] = Result[2] = Result[3] = 0.0f;
    if (Request->Sampler == nullptr || !RxWarpViewExtent(Request->View, &Extent))
        return;

    Lod = RxWarpRawLod(&Extent, &Shape, Dx, Dy) + Request->Sampler->Desc.MipLODBias;
    Clamped = RxWarpClampLod(Request, Lod);
    if (Clamped < 0.0f)
        Clamped = 0.0f;
    if (Clamped > (FLOAT)(Extent.MipCount - 1))
        Clamped = (FLOAT)(Extent.MipCount - 1);
    Result[0] = Clamped;
    Result[1] = Lod;
}

VOID
RxWarpSample(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(4) const FLOAT *Coordinate,
    _In_reads_opt_(3) const FLOAT *Dx,
    _In_reads_opt_(3) const FLOAT *Dy,
    _Out_writes_(4) FLOAT *Result)
{
    RxWarpByteFilterValid = FALSE;
    RX_WARP_VIEW_EXTENT Extent;
    RX_WARP_SHAPE Shape = RxWarpShapeOf(Request->Dimension);
    const D3D10_DDI_SAMPLER_DESC *Desc;
    FLOAT Lookup[3] = { Coordinate[0], Coordinate[1], Coordinate[2] };
    UINT Slice = 0;
    UINT Filter;
    BOOLEAN MinLinear;
    BOOLEAN MagLinear;
    BOOLEAN MipLinear;
    FLOAT Lod;

    Result[0] = Result[1] = Result[2] = 0.0f;
    Result[3] = 0.0f;
    if (Request->Sampler == nullptr || !RxWarpViewExtent(Request->View, &Extent))
        return;
    Desc = &Request->Sampler->Desc;

    if (Shape.Array)
    {
        FLOAT Layer = nearbyintf(Coordinate[Shape.SliceComponent]);
        UINT Layers = Shape.Cube ? Extent.SliceCount / 6 : Extent.SliceCount;

        if (!(Layer > 0.0f))
            Layer = 0.0f;
        Slice = (Layer >= (FLOAT)Layers) ? Layers - 1 : (UINT)Layer;
        if (Shape.Cube)
            Slice *= 6;
    }
    if (Shape.Cube)
    {
        Slice += RxWarpCubeFace(Coordinate, Lookup);
        Lookup[2] = 0.0f;
    }

    Filter = (UINT)Desc->Filter;
    MipLinear = (Filter & 0x1) != 0;
    MagLinear = (Filter & 0x4) != 0;
    MinLinear = (Filter & 0x10) != 0;

    if (Request->Mode == RxWarpSampleLevel)
        Lod = Request->Level + Desc->MipLODBias;
    else if (Dx != nullptr && Dy != nullptr)
        Lod = RxWarpRawLod(&Extent, &Shape, Dx, Dy) + Request->Bias + Desc->MipLODBias;
    else
        Lod = 0.0f;
    Lod = RxWarpClampLod(Request, Lod);

    /* Gathers always read the most detailed level with a bilinear footprint */
    if (Request->Gather)
    {
        RxWarpFilterLevel(Request, &Extent, &Shape, 0, Slice, Lookup, TRUE, Result);
        return;
    }

    if (!(Lod > 0.0f))
    {
        RxWarpFilterLevel(Request, &Extent, &Shape, 0, Slice, Lookup, MagLinear, Result);
        return;
    }
    if (Lod > (FLOAT)(Extent.MipCount - 1))
        Lod = (FLOAT)(Extent.MipCount - 1);

    if (!MipLinear)
    {
        UINT Mip = (UINT)floorf(Lod + 0.5f);

        if (Mip >= Extent.MipCount)
            Mip = Extent.MipCount - 1;
        RxWarpFilterLevel(Request, &Extent, &Shape, Mip, Slice, Lookup, MinLinear, Result);
        return;
    }
    else
    {
        UINT Lower = (UINT)floorf(Lod);
        UINT Upper = (Lower + 1 < Extent.MipCount) ? Lower + 1 : Lower;
        FLOAT Blend = Lod - (FLOAT)Lower;
        FLOAT First[4];
        FLOAT Second[4];

        INT FirstFixed[4];
        BOOLEAN FirstBytes;

        RxWarpFilterLevel(Request, &Extent, &Shape, Lower, Slice, Lookup, MinLinear, First);
        if (Blend == 0.0f || Upper == Lower)
        {
            memcpy(Result, First, sizeof(First));
            return;
        }
        FirstBytes = RxWarpByteFilterValid;
        memcpy(FirstFixed, RxWarpByteFilterFixed, sizeof(FirstFixed));
        RxWarpFilterLevel(Request, &Extent, &Shape, Upper, Slice, Lookup, MinLinear, Second);

        /* Eight bit levels blend in their fixed point form, by the level fraction rounded to eight bits */
        if (FirstBytes && RxWarpByteFilterValid)
        {
            INT Weight = (INT)nearbyintf(Blend * 256.0f);

            for (UINT Component = 0; Component < 4; Component++)
            {
                RxWarpByteFilterFixed[Component] = FirstFixed[Component] + (((RxWarpByteFilterFixed[Component] - FirstFixed[Component]) * Weight) >> 8);
                Result[Component] = RxWarpByteResult(RxWarpByteFilterFixed[Component]);
            }
            return;
        }
        RxWarpByteFilterValid = FALSE;
        for (UINT Component = 0; Component < 4; Component++)
            Result[Component] = First[Component] + (Second[Component] - First[Component]) * Blend;
    }
}

VOID
RxWarpLoad(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(4) const INT *Address,
    _Out_writes_(4) UINT *Result)
{
    RxWarpView *View = Request->View;
    RX_WARP_VIEW_EXTENT Extent;
    RX_WARP_SHAPE Shape = RxWarpShapeOf(Request->Dimension);
    INT At[3] = {};
    INT Slice = 0;
    INT Mip;

    Result[0] = Result[1] = Result[2] = Result[3] = 0;
    if (View == nullptr || View->Resource == nullptr)
        return;

    /* Buffers: one element of the view's format */
    if (View->Resource->Dimension == D3D10DDIRESOURCE_BUFFER)
    {
        const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
        BYTE Element[16] = {};
        SIZE_T Offset;

        if (Info == nullptr || Address[0] < 0 || (UINT)Address[0] >= View->ElementCount)
            return;
        Offset = ((SIZE_T)View->FirstElement + (UINT)Address[0]) * Info->Bytes;
        if (Offset + Info->Bytes > View->Resource->MemorySize)
            return;
        memcpy(Element, View->Resource->Memory + Offset, (Info->Bytes <= 16) ? Info->Bytes : 16);
        RxWarpDecodeTexel(View->Format, Element, Result);
        return;
    }

    if (!RxWarpViewExtent(View, &Extent))
        return;

    for (UINT Axis = 0; Axis < Shape.Axes; Axis++)
        At[Axis] = Address[Axis] + Request->Offset[Axis];
    if (Shape.Array)
        Slice = Address[Shape.SliceComponent];
    Mip = (Request->Dimension == D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMS ||
           Request->Dimension == D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DMSARRAY) ? 0 : Address[3];

    if (Mip < 0 || (UINT)Mip >= Extent.MipCount || Slice < 0 || (UINT)Slice >= Extent.SliceCount)
        return;
    RxWarpReadTexel(View, &Extent, (UINT)Mip, (UINT)Slice, At[0], At[1], At[2], Result, Request->Sample);
}

VOID
RxWarpSamplePosition(
    _In_ UINT Count,
    _In_ UINT Index,
    _Out_writes_(2) FLOAT *Position)
{
    static const INT8 Two[2][2] = { { 4, 4 }, { -4, -4 } };
    static const INT8 Four[4][2] = { { -2, -6 }, { 6, -2 }, { -6, 2 }, { 2, 6 } };
    static const INT8 Eight[8][2] = { { 1, -3 }, { -1, 3 }, { 5, 1 }, { -3, -5 }, { -5, 5 }, { -7, -1 }, { 3, 7 }, { 7, -7 } };
    static const INT8 Sixteen[16][2] =
    {
        { 1, 1 }, { -1, -3 }, { -3, 2 }, { 4, -1 }, { -5, -2 }, { 2, 5 }, { 5, 3 }, { 3, -5 },
        { -2, 6 }, { 0, -7 }, { -4, -6 }, { -6, 4 }, { -8, 0 }, { 7, -4 }, { 6, 7 }, { -7, -8 },
    };
    const INT8 *Entry = nullptr;

    Position[0] = Position[1] = 0.0f;
    if (Index >= Count)
        return;
    switch (Count)
    {
        case 2:
            Entry = Two[Index];
            break;
        case 4:
            Entry = Four[Index];
            break;
        case 8:
            Entry = Eight[Index];
            break;
        case 16:
            Entry = Sixteen[Index];
            break;
        default:
            return;
    }
    Position[0] = (FLOAT)(Entry[0] * 16) * (1.0f / 256.0f);
    Position[1] = (FLOAT)(Entry[1] * 16) * (1.0f / 256.0f);
}

VOID
RxWarpResourceInfo(
    _In_opt_ RxWarpView *View,
    _In_ UINT Mip,
    _In_ UINT ReturnType,
    _Out_writes_(4) UINT *Result)
{
    RX_WARP_VIEW_EXTENT Extent;
    RX_WARP_SUBRESOURCE *Level;
    UINT Values[4] = {};
    BOOLEAN Volume;

    Result[0] = Result[1] = Result[2] = Result[3] = 0;
    if (!RxWarpViewExtent(View, &Extent))
        return;

    Values[3] = Extent.MipCount;
    Volume = (Extent.Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D);
    if (Mip < Extent.MipCount && (Level = RxWarpExtentLevel(&Extent, Mip, 0)) != nullptr)
    {
        Values[0] = Level->Width;
        Values[1] = (Extent.Resource->Dimension == D3D10DDIRESOURCE_TEXTURE1D) ? Extent.SliceCount : Level->Height;
        Values[2] = Volume ? Level->Depth :
                    (Extent.Resource->Dimension == D3D10DDIRESOURCE_TEXTURECUBE) ? Extent.SliceCount / 6 : Extent.SliceCount;
        if (Extent.Resource->Dimension == D3D10DDIRESOURCE_TEXTURE1D && View->Dimension == D3D10DDIRESOURCE_TEXTURE1D &&
            Extent.Resource->ArraySize == 1)
        {
            Values[1] = 0;
        }
    }

    for (UINT Component = 0; Component < 4; Component++)
    {
        FLOAT Value = (FLOAT)Values[Component];

        if (ReturnType == D3D10_SB_RESINFO_INSTRUCTION_RETURN_UINT)
            Result[Component] = Values[Component];
        else
        {
            /* rcpfloat inverts the sizes, not the array size or mip count */
            if (ReturnType == D3D10_SB_RESINFO_INSTRUCTION_RETURN_RCPFLOAT && Component < 3 &&
                (Component < 2 || Volume) && Values[Component] != 0)
            {
                Value = 1.0f / Value;
            }
            memcpy(&Result[Component], &Value, sizeof(Value));
        }
    }
}
