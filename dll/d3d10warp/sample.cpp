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
    _Out_writes_(4) UINT *Value)
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

    Source = Extent->Resource->Memory + Level->Offset + (SIZE_T)Z * Level->DepthPitch + (SIZE_T)Y * Level->RowPitch +
             (SIZE_T)X * Info->Bytes;
    memcpy(Element, Source, (Info->Bytes <= 16) ? Info->Bytes : 16);
    return RxWarpDecodeTexel(View->Format, Element, Value);
}

static
INT
RxWarpAddress(
    _In_ INT Coordinate,
    _In_ INT Size,
    _In_ UINT Mode,
    _Out_ BOOLEAN *Border)
{
    INT Period;

    *Border = FALSE;
    switch (Mode)
    {
        case D3D10_DDI_TEXTURE_ADDRESS_WRAP:
            Coordinate %= Size;
            return (Coordinate < 0) ? Coordinate + Size : Coordinate;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRROR:
            Period = Size * 2;
            Coordinate %= Period;
            if (Coordinate < 0)
                Coordinate += Period;
            return (Coordinate < Size) ? Coordinate : Period - 1 - Coordinate;
        case D3D10_DDI_TEXTURE_ADDRESS_BORDER:
            if (Coordinate < 0 || Coordinate >= Size)
                *Border = TRUE;
            return Coordinate;
        case D3D10_DDI_TEXTURE_ADDRESS_MIRRORONCE:
            if (Coordinate < 0)
                Coordinate = -1 - Coordinate;
            return (Coordinate >= Size) ? Size - 1 : Coordinate;
        default:
            if (Coordinate < 0)
                return 0;
            return (Coordinate >= Size) ? Size - 1 : Coordinate;
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
    INT Base[3] = {};
    FLOAT Weight[3] = {};
    UINT Taps = Linear ? 2 : 1;
    FLOAT Gathered[4] = {};

    Result[0] = Result[1] = Result[2] = Result[3] = 0.0f;
    if (Level == nullptr)
        return;

    Size[0] = (INT)Level->Width;
    Size[1] = (INT)Level->Height;
    Size[2] = (INT)Level->Depth;
    if (Shape->Cube)
        Modes[0] = Modes[1] = D3D10_DDI_TEXTURE_ADDRESS_CLAMP;

    for (UINT Axis = 0; Axis < Shape->Axes; Axis++)
    {
        FLOAT Scaled = Coordinate[Axis] * (FLOAT)Size[Axis];

        if (Linear)
        {
            Scaled -= 0.5f;
            Base[Axis] = (INT)floorf(Scaled);
            Weight[Axis] = Scaled - floorf(Scaled);
        }
        else
        {
            Base[Axis] = (INT)floorf(Scaled);
        }
        Base[Axis] += Request->Offset[Axis];
    }

    for (UINT Tz = 0; Tz < ((Shape->Axes > 2) ? Taps : 1); Tz++)
    {
        for (UINT Ty = 0; Ty < ((Shape->Axes > 1) ? Taps : 1); Ty++)
        {
            for (UINT Tx = 0; Tx < Taps; Tx++)
            {
                INT At[3] = { Base[0] + (INT)Tx, Base[1] + (INT)Ty, Base[2] + (INT)Tz };
                BOOLEAN Border = FALSE;
                FLOAT Texel[4];
                FLOAT Factor = 1.0f;
                UINT Raw[4];

                for (UINT Axis = 0; Axis < 3; Axis++)
                {
                    BOOLEAN Outside;

                    if (Axis >= Shape->Axes)
                    {
                        At[Axis] = 0;
                        continue;
                    }
                    At[Axis] = RxWarpAddress(At[Axis], Size[Axis], Modes[Axis], &Outside);
                    Border |= Outside;
                }

                if (Border)
                {
                    memcpy(Texel, Desc->BorderColor, sizeof(Texel));
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

                /* Gather order: (0,1), (1,1), (1,0), (0,0) */
                if (Request->Gather)
                {
                    static const UINT Slot[2][2] = { { 3, 2 }, { 0, 1 } };

                    Gathered[Slot[Ty][Tx]] = Texel[Request->GatherComponent & 3];
                    continue;
                }

                if (Linear)
                {
                    Factor = Tx ? Weight[0] : 1.0f - Weight[0];
                    if (Shape->Axes > 1)
                        Factor *= Ty ? Weight[1] : 1.0f - Weight[1];
                    if (Shape->Axes > 2)
                        Factor *= Tz ? Weight[2] : 1.0f - Weight[2];
                }
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Component] += Texel[Component] * Factor;
            }
        }
    }
    if (Request->Gather)
        memcpy(Result, Gathered, sizeof(Gathered));
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

        RxWarpFilterLevel(Request, &Extent, &Shape, Lower, Slice, Lookup, MinLinear, First);
        if (Blend == 0.0f || Upper == Lower)
        {
            memcpy(Result, First, sizeof(First));
            return;
        }
        RxWarpFilterLevel(Request, &Extent, &Shape, Upper, Slice, Lookup, MinLinear, Second);
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
    RxWarpReadTexel(View, &Extent, (UINT)Mip, (UINT)Slice, At[0], At[1], At[2], Result);
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
