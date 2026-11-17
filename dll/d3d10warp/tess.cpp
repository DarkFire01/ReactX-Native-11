/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fixed function tessellator, bit exact with the reference's 16.16 fixed point layout
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "tess.hpp"

static constexpr UINT RxTessOne = 0x10000;
static constexpr UINT RxTessHalf = 0x8000;
static constexpr UINT RxTessThird = 0x5555;
static constexpr UINT RxTessTwoThirds = 0xAAAA;

enum
{
    RxTessPlain = 0,
    RxTessShifted = 1,
    RxTessMirrored = 2
};

enum
{
    RxTessInsideOut = 0,        /* every diagonal leans from the inner row forward to the outer row */
    RxTessInsideOutButMiddle = 1,
    RxTessMirroredHalves = 2
};

/*
 * Where split number i lands on a half edge when points are added in ruler order.
 * A ring transition advances a row whenever the entry is below that row's half point count.
 */
static const UINT8 RxTessSplitOrder[33] =
{
    0, 32, 16, 8, 17, 4, 18, 9, 19, 2, 20, 10, 21, 5, 22, 11, 23,
    1, 24, 12, 25, 6, 26, 13, 27, 3, 28, 14, 29, 7, 30, 15, 31
};

static
FLOAT
RxTessFlush(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    if ((Bits & 0x7F800000) == 0)
        Bits &= 0x80000000;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

/* Clamps with denormals flushed; NaN becomes the low bound */
static
FLOAT
RxTessClamp(
    _In_ FLOAT Low,
    _In_ FLOAT High,
    _In_ FLOAT Value)
{
    Value = RxTessFlush(Value);
    if (Value != Value)
        return Low;
    if (Value < Low)
        Value = Low;
    if (Value > High)
        Value = High;
    return Value;
}

static
BOOLEAN
RxTessIsOdd(
    _In_ FLOAT Value)
{
    return ((INT)Value & 1) != 0;
}

/* Unsigned 16.16, rounding to nearest even; negatives and NaN give zero */
static
UINT
RxTessToFixed(
    _In_ FLOAT Value)
{
    double Scaled;
    UINT Whole;
    double Rest;

    if (!(Value > 0.0f))
        return 0;
    if (Value >= 32768.0f)
        return UINT_MAX;
    Scaled = (double)Value * 65536.0;
    Whole = (UINT)Scaled;
    Rest = Scaled - (double)Whole;
    if (Rest > 0.5 || (Rest == 0.5 && (Whole & 1)))
        Whole++;
    return Whole;
}

static
UINT
RxTessCeil(
    _In_ UINT Value)
{
    return (Value & 0xFFFF) ? (Value & 0x7FFF0000) + RxTessOne : Value;
}

static
UINT
RxTessStep(
    _In_ UINT Segments)
{
    return Segments ? (RxTessOne + Segments / 2) / Segments : UINT_MAX;
}

static
INT
RxTessDropTopBit(
    _In_ INT Value)
{
    ULONG Top;

    if (Value <= 0 || !_BitScanReverse(&Top, (ULONG)Value))
        return 0;
    return Value & ~(1 << Top);
}

static
VOID
RxTessMakeScale(
    _In_ UINT Factor,
    _In_ BOOLEAN Odd,
    _Out_ RX_WARP_EDGE_SCALE *Scale)
{
    UINT Half = (Factor + 1) >> 1;
    UINT Adjusted = Half;
    UINT Floor;
    UINT Ceil;

    if (Odd || Adjusted == RxTessHalf)
        Adjusted += RxTessHalf;
    Floor = Adjusted & 0xFFFF0000;
    Ceil = RxTessCeil(Adjusted);

    Scale->Odd = Odd;
    Scale->Fraction = Adjusted - Floor;
    Scale->HalfPoints = (INT)(Ceil >> 16);
    if (Ceil == Floor)
        Scale->Split = Scale->HalfPoints + 1;
    else if (Odd)
        Scale->Split = (Floor == RxTessOne) ? 0 : (RxTessDropTopBit((INT)(Floor >> 16) - 1) << 1) + 1;
    else
        Scale->Split = (RxTessDropTopBit((INT)(Floor >> 16)) << 1) + 1;
    Scale->FloorStep = RxTessStep(((Floor >> 15) & 0xFFFE) - (Odd ? 1 : 0));
    Scale->CeilStep = RxTessStep(((Ceil >> 15) & 0xFFFF) - (Odd ? 1 : 0));
    if (Odd)
        Scale->Points = (INT)((RxTessCeil(Half + RxTessHalf) >> 15) & 0xFFFF);
    else
        Scale->Points = (INT)((RxTessCeil(Half) >> 15) & 0xFFFF) + 1;
}

/* Position of point Index along an edge, 0 to 1 in 16.16 */
static
UINT
RxTessPlace(
    _In_ const RX_WARP_EDGE_SCALE *Scale,
    _In_ INT Index)
{
    BOOLEAN Mirror = FALSE;
    UINT OnFloor;
    UINT Location;

    if (Index >= Scale->HalfPoints)
    {
        Index = 2 * Scale->HalfPoints - Index - (Scale->Odd ? 1 : 0);
        if (Index == Scale->HalfPoints)
            return RxTessHalf;
        Mirror = TRUE;
    }
    OnFloor = (UINT)((Index > Scale->Split) ? Index - 1 : Index);
    Location = (OnFloor * Scale->FloorStep * (RxTessOne - Scale->Fraction) +
                (UINT)Index * Scale->CeilStep * Scale->Fraction + RxTessHalf) >> 16;
    return Mirror ? RxTessOne - Location : Location;
}

VOID
RxWarpTessellator::AddPoint(
    _In_ UINT U,
    _In_ UINT V)
{
    RX_WARP_DOMAIN_POINT Point;

    Point.U = (FLOAT)U * (1.0f / 65536.0f);
    Point.V = (FLOAT)V * (1.0f / 65536.0f);
    Points.push_back(Point);
}

INT
RxWarpTessellator::Remap(
    _In_ INT Index)
{
    if (RemapMode == RxTessShifted)
    {
        if (Index >= OutsideStart)
            return (Index == OutsideWrap) ? OutsideFirst : Index + OutsideShift;
        return (Index == InsideWrap) ? InsideFirst : Index + InsideShift;
    }
    if (RemapMode == RxTessMirrored)
    {
        if (Index == MirrorCorner)
            return MirrorCornerValue;
        if (Index >= MirrorStart)
            return MirrorEnd - Index;
    }
    return Index;
}

/* Takes a clockwise triangle; stores it in the declared winding */
VOID
RxWarpTessellator::AddTriangle(
    _In_ INT First,
    _In_ INT Second,
    _In_ INT Third)
{
    BOOLEAN Clockwise = (Output == D3D11_SB_TESSELLATOR_OUTPUT_TRIANGLE_CW);

    Indices.push_back((UINT)Remap(First));
    Indices.push_back((UINT)Remap(Clockwise ? Second : Third));
    Indices.push_back((UINT)Remap(Clockwise ? Third : Second));
}

/* Each stitch owns a fixed number of index slots */
VOID
RxWarpTessellator::Close(
    _In_ SIZE_T Count)
{
    Indices.resize(Count, 0);
}

/* Joins two rows of points with the same spacing */
VOID
RxWarpTessellator::Strip(
    _In_ BOOLEAN Ends,
    _In_ UINT Diagonals,
    _In_ INT InsideCount,
    _In_ INT Inside,
    _In_ INT Outside)
{
    INT Step;

    if (Ends)
    {
        AddTriangle(Outside, Outside + 1, Inside);
        Outside++;
    }

    switch (Diagonals)
    {
        case RxTessInsideOut:
            for (Step = 0; Step < InsideCount - 1; Step++, Inside++, Outside++)
            {
                AddTriangle(Inside, Outside, Outside + 1);
                AddTriangle(Inside, Outside + 1, Inside + 1);
            }
            break;
        case RxTessInsideOutButMiddle:
            for (Step = 0; Step < InsideCount / 2 - 1; Step++, Inside++, Outside++)
            {
                AddTriangle(Outside, Outside + 1, Inside);
                AddTriangle(Inside, Outside + 1, Inside + 1);
            }
            AddTriangle(Outside, Inside + 1, Inside);
            AddTriangle(Outside, Outside + 1, Inside + 1);
            Inside++;
            Outside++;
            for (Step += 2; Step < InsideCount; Step++, Inside++, Outside++)
            {
                AddTriangle(Outside, Outside + 1, Inside);
                AddTriangle(Inside, Outside + 1, Inside + 1);
            }
            break;
        case RxTessMirroredHalves:
            for (Step = 0; Step < InsideCount / 2; Step++, Inside++, Outside++)
            {
                AddTriangle(Outside, Inside + 1, Inside);
                AddTriangle(Outside, Outside + 1, Inside + 1);
            }
            for (; Step < InsideCount - 1; Step++, Inside++, Outside++)
            {
                AddTriangle(Inside, Outside, Outside + 1);
                AddTriangle(Inside, Outside + 1, Inside + 1);
            }
            break;
    }

    if (Ends)
        AddTriangle(Outside, Outside + 1, Inside);
}

/* Joins the outer edge to the first inner ring when their spacing differs */
VOID
RxWarpTessellator::Transition(
    _In_ INT Inside,
    _In_ const RX_WARP_EDGE_SCALE *InsideScale,
    _In_ INT Outside,
    _In_ const RX_WARP_EDGE_SCALE *OutsideScale)
{
    INT InsideHalf = InsideScale->HalfPoints - (InsideScale->Odd ? 1 : 0);
    INT OutsideHalf = OutsideScale->HalfPoints - (OutsideScale->Odd ? 1 : 0);
    INT Order;

    if (OutsideHalf > 0)
    {
        AddTriangle(Outside, Outside + 1, Inside);
        Outside++;
    }
    for (Order = 1; Order <= 32; Order++)
    {
        if (RxTessSplitOrder[Order] < InsideHalf)
        {
            AddTriangle(Inside, Outside, Inside + 1);
            Inside++;
        }
        if (RxTessSplitOrder[Order] < OutsideHalf)
        {
            AddTriangle(Outside, Outside + 1, Inside);
            Outside++;
        }
    }

    if (InsideScale->Odd && OutsideScale->Odd)
    {
        AddTriangle(Inside, Outside, Inside + 1);
        AddTriangle(Inside + 1, Outside, Outside + 1);
        Inside++;
        Outside++;
    }
    else if (InsideScale->Odd != OutsideScale->Odd)
    {
        if (!InsideScale->Odd)
        {
            AddTriangle(Inside, Outside, Outside + 1);
            Outside++;
        }
        else
        {
            AddTriangle(Inside, Outside, Inside + 1);
            Inside++;
        }
    }

    for (Order = 32; Order >= 1; Order--)
    {
        if (RxTessSplitOrder[Order] < OutsideHalf)
        {
            AddTriangle(Outside, Outside + 1, Inside);
            Outside++;
        }
        if (RxTessSplitOrder[Order] < InsideHalf)
        {
            AddTriangle(Inside, Outside, Inside + 1);
            Inside++;
        }
    }
    if (OutsideHalf > 0)
        AddTriangle(Outside, Outside + 1, Inside);
}

VOID
RxWarpTessellator::Quad(
    _In_reads_(6) const FLOAT *Factors)
{
    BOOLEAN Integer = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_INTEGER ||
                       Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_POW2);
    BOOLEAN FractionalOdd = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD);
    FLOAT Low = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN) ? 2.0f : 1.0f;
    FLOAT High = FractionalOdd ? 63.0f : 64.0f;
    FLOAT Edge[4];
    FLOAT Inside[2];
    UINT EdgeFixed[4];
    UINT InsideFixed[2];
    BOOLEAN EdgeOdd[4];
    BOOLEAN InsideOdd[2];
    RX_WARP_EDGE_SCALE EdgeScale[4];
    RX_WARP_EDGE_SCALE InsideScale[2];
    INT InsidePoints[2];
    INT Smaller;
    BOOLEAN AllOnes = TRUE;

    for (UINT Side = 0; Side < 4; Side++)
    {
        if (!(Factors[Side] > 0.0f))
            return;
    }

    for (UINT Side = 0; Side < 4; Side++)
    {
        Edge[Side] = RxTessClamp(Low, High, Factors[Side]);
        if (Integer)
            Edge[Side] = ceilf(RxTessFlush(Edge[Side]));
    }

    /* Fractional odd keeps an inner ring once anything rises above one */
    if (FractionalOdd)
    {
        for (UINT Slot = 0; Slot < 6; Slot++)
        {
            FLOAT Value = RxTessFlush((Slot < 4) ? Edge[Slot] : Factors[Slot]);

            if (Value > 1.0f + 1.0f / 131072.0f)
            {
                Low = 1.0f + 1.0f / 65536.0f;
                break;
            }
        }
    }
    for (UINT Axis = 0; Axis < 2; Axis++)
    {
        Inside[Axis] = RxTessClamp(Low, High, Factors[4 + Axis]);
        if (Integer)
            Inside[Axis] = ceilf(RxTessFlush(Inside[Axis]));
    }

    for (UINT Side = 0; Side < 4; Side++)
    {
        EdgeOdd[Side] = Integer ? RxTessIsOdd(Edge[Side]) : FractionalOdd;
        EdgeFixed[Side] = RxTessToFixed(Edge[Side]);
        AllOnes = AllOnes && (EdgeFixed[Side] == RxTessOne);
    }
    for (UINT Axis = 0; Axis < 2; Axis++)
    {
        if (Integer)
            InsideOdd[Axis] = RxTessIsOdd(Inside[Axis]) && RxTessFlush(Inside[Axis]) != 1.0f;
        else
            InsideOdd[Axis] = FractionalOdd;
        InsideFixed[Axis] = RxTessToFixed(Inside[Axis]);
        AllOnes = AllOnes && (InsideFixed[Axis] == RxTessOne);
    }

    if ((Integer || FractionalOdd) && AllOnes)
    {
        AddPoint(0, 0);
        AddPoint(RxTessOne, 0);
        AddPoint(RxTessOne, RxTessOne);
        AddPoint(0, RxTessOne);
        if (Output == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
        {
            for (UINT Index = 0; Index < 4; Index++)
                Indices.push_back(Index);
        }
        else if (Output != D3D11_SB_TESSELLATOR_OUTPUT_LINE)
        {
            AddTriangle(0, 1, 3);
            AddTriangle(1, 2, 3);
        }
        return;
    }

    for (UINT Side = 0; Side < 4; Side++)
        RxTessMakeScale(EdgeFixed[Side], EdgeOdd[Side], &EdgeScale[Side]);
    for (UINT Axis = 0; Axis < 2; Axis++)
    {
        RxTessMakeScale(InsideFixed[Axis], InsideOdd[Axis], &InsideScale[Axis]);
        InsidePoints[Axis] = InsideScale[Axis].Points;
        if (InsidePoints[Axis] < (InsideOdd[Axis] ? 4 : 3))
            InsidePoints[Axis] = InsideOdd[Axis] ? 4 : 3;
    }
    Smaller = (InsidePoints[0] < InsidePoints[1]) ? InsidePoints[0] : InsidePoints[1];

    /* Outer edges, clockwise from the U == 0 edge; each edge leaves its last point to the next */
    for (UINT Side = 0; Side < 4; Side++)
    {
        INT Last = EdgeScale[Side].Points - 1;

        for (INT Point = 0; Point < Last; Point++)
        {
            BOOLEAN Forward = (Side == 1 || Side == 2);
            UINT Along = RxTessPlace(&EdgeScale[Side], Forward ? Point : Last - Point);

            if (Side & 1)
                AddPoint(Along, (Side == 3) ? RxTessOne : 0);
            else
                AddPoint((Side == 2) ? RxTessOne : 0, Along);
        }
    }

    /* Inner rings */
    for (INT Ring = 1; Ring < (Smaller >> 1); Ring++)
    {
        INT Ends[2] = { InsidePoints[0] - 1 - Ring, InsidePoints[1] - 1 - Ring };

        for (UINT Side = 0; Side < 4; Side++)
        {
            UINT Across = Side & 1;
            UINT Along = Across ^ 1;
            UINT Fixed = RxTessPlace(&InsideScale[Across], (Side < 2) ? Ring : Ends[Across]);

            for (INT Point = Ring; Point < Ends[Along]; Point++)
            {
                BOOLEAN Forward = (Side == 1 || Side == 2);
                UINT Moving = RxTessPlace(&InsideScale[Along], Forward ? Point : Ends[Along] - (Point - Ring));

                if (Along)
                    AddPoint(Fixed, Moving);
                else
                    AddPoint(Moving, Fixed);
            }
        }
    }

    /* Even inside factors leave a row of points down the middle */
    if (InsidePoints[0] > InsidePoints[1])
    {
        if (!InsideOdd[1])
        {
            for (INT Point = Smaller >> 1; Point <= InsidePoints[0] - 1 - (Smaller >> 1); Point++)
                AddPoint(RxTessPlace(&InsideScale[0], Point), RxTessHalf);
        }
    }
    else if (!InsideOdd[0])
    {
        for (INT Point = InsidePoints[1] - 1 - (Smaller >> 1); Point >= (Smaller >> 1); Point--)
            AddPoint(RxTessHalf, RxTessPlace(&InsideScale[1], Point));
    }

    if (Output == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
    {
        for (UINT Index = 0; Index < (UINT)Points.size(); Index++)
            Indices.push_back(Index);
        return;
    }
    if (Output == D3D11_SB_TESSELLATOR_OUTPUT_LINE)
        return;

    {
        INT Rows[2] = { (InsidePoints[0] + 1) >> 1, (InsidePoints[1] + 1) >> 1 };
        INT Rings = (Rows[0] < Rows[1]) ? Rows[0] : Rows[1];
        INT Flat[2] = { InsideOdd[1] ? -1 : Rows[1] - 1, InsideOdd[0] ? -1 : Rows[0] - 1 };
        const RX_WARP_EDGE_SCALE *OuterScale[4] = { &EdgeScale[0], &EdgeScale[1], &EdgeScale[2], &EdgeScale[3] };
        INT OuterPoints[4] = { EdgeScale[0].Points, EdgeScale[1].Points, EdgeScale[2].Points, EdgeScale[3].Points };
        INT InnerBase = 0;
        INT OuterBase = 0;

        for (UINT Side = 0; Side < 4; Side++)
            InnerBase += OuterPoints[Side];
        InnerBase -= 4;

        for (INT Ring = 1; Ring < Rings; Ring++)
        {
            INT InnerPoints[2] = { InsidePoints[0] - 2 * Ring, InsidePoints[1] - 2 * Ring };
            INT RingInner = InnerBase;
            INT RingOuter = OuterBase;

            for (UINT Side = 0; Side < 4; Side++)
            {
                UINT Axis = (Side + 1) & 1;
                INT Count = InnerPoints[Axis];
                SIZE_T Slots = Indices.size() + (SIZE_T)(Count + OuterPoints[Side] - 2) * 3;
                INT InnerAt = InnerBase;
                INT OuterAt = OuterBase;
                BOOLEAN FlatSide = (Ring == Flat[Axis]);

                if (Side == 3 && FlatSide)
                {
                    RemapMode = RxTessMirrored;
                    MirrorStart = InnerBase + 1;
                    MirrorCorner = OuterBase + OuterPoints[Side] - 1;
                    MirrorCornerValue = RingOuter;
                    MirrorEnd = (MirrorStart << 1) - 1;
                    InnerAt = MirrorStart;
                }
                else if (Side == 3)
                {
                    RemapMode = RxTessShifted;
                    InsideShift = InnerBase;
                    InsideWrap = Count - 1;
                    InsideFirst = RingInner;
                    OutsideStart = Count;
                    OutsideShift = OuterBase - Count;
                    OutsideWrap = Count + OuterPoints[Side] - 1;
                    OutsideFirst = RingOuter;
                    InnerAt = 0;
                    OuterAt = OutsideStart;
                }
                else if (Side == 2 && FlatSide)
                {
                    RemapMode = RxTessMirrored;
                    MirrorStart = InnerBase;
                    MirrorCorner = -1;
                    MirrorCornerValue = -1;
                    MirrorEnd = MirrorStart << 1;
                }

                if (Ring == 1)
                    Transition(InnerAt, &InsideScale[Axis], OuterAt, OuterScale[Side]);
                else
                    Strip(TRUE, RxTessMirroredHalves, Count, InnerAt, OuterAt);
                RemapMode = RxTessPlain;
                Close(Slots);

                OuterBase += OuterPoints[Side] - 1;
                if (Side == 2 && FlatSide)
                    InnerBase -= Count - 1;
                else
                    InnerBase += Count - 1;
                OuterPoints[Side] = Count;
            }
            if (Ring == 1)
            {
                for (UINT Side = 0; Side < 4; Side++)
                    OuterScale[Side] = &InsideScale[Side & 1];
            }
        }

        /* The middle: a strip of quads when the shorter inside factor is odd */
        if (InsidePoints[0] > InsidePoints[1] && InsideOdd[1])
        {
            INT Quads = (((InsidePoints[0] >> 1) - (InsidePoints[1] >> 1)) << 1) + (InsideOdd[0] ? 1 : 2);
            SIZE_T Slots = Indices.size() + (SIZE_T)Quads * 6;

            RemapMode = RxTessMirrored;
            MirrorStart = OuterBase + Quads + 2;
            MirrorCorner = MirrorStart;
            MirrorCornerValue = OuterBase;
            MirrorEnd = MirrorStart + MirrorStart + Quads;
            Strip(FALSE, RxTessInsideOut, Quads + 1, MirrorStart, OuterBase + 1);
            RemapMode = RxTessPlain;
            Close(Slots);
        }
        else if (InsidePoints[1] >= InsidePoints[0] && InsideOdd[0])
        {
            INT Quads = (((InsidePoints[1] >> 1) - (InsidePoints[0] >> 1)) << 1) + (InsideOdd[1] ? 1 : 2);
            SIZE_T Slots = Indices.size() + (SIZE_T)Quads * 6;

            RemapMode = RxTessMirrored;
            MirrorStart = OuterBase + Quads + 1;
            MirrorCorner = -1;
            MirrorEnd = MirrorStart + MirrorStart + Quads;
            Strip(FALSE, InsideOdd[1] ? RxTessInsideOutButMiddle : RxTessInsideOut, Quads + 1, MirrorStart, OuterBase);
            RemapMode = RxTessPlain;
            Close(Slots);
        }
    }
}

VOID
RxWarpTessellator::Triangle(
    _In_reads_(4) const FLOAT *Factors)
{
    BOOLEAN Integer = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_INTEGER ||
                       Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_POW2);
    BOOLEAN FractionalOdd = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD);
    FLOAT Low = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN) ? 2.0f : 1.0f;
    FLOAT High = FractionalOdd ? 63.0f : 64.0f;
    FLOAT Edge[3];
    FLOAT Inside;
    UINT EdgeFixed[3];
    UINT InsideFixed;
    BOOLEAN EdgeOdd[3];
    BOOLEAN InsideOdd;
    RX_WARP_EDGE_SCALE EdgeScale[3];
    RX_WARP_EDGE_SCALE InsideScale;
    INT InsidePoints;
    BOOLEAN AllOnes = TRUE;

    for (UINT Side = 0; Side < 3; Side++)
    {
        if (!(Factors[Side] > 0.0f))
            return;
    }

    for (UINT Side = 0; Side < 3; Side++)
    {
        Edge[Side] = RxTessClamp(Low, High, Factors[Side]);
        if (Integer)
            Edge[Side] = ceilf(RxTessFlush(Edge[Side]));
    }
    if (FractionalOdd)
    {
        for (UINT Side = 0; Side < 3; Side++)
        {
            if (RxTessFlush(Edge[Side]) > 1.0f + 1.0f / 131072.0f)
            {
                Low = 1.0f + 1.0f / 65536.0f;
                break;
            }
        }
    }
    Inside = RxTessClamp(Low, High, Factors[3]);
    if (Integer)
        Inside = ceilf(RxTessFlush(Inside));

    for (UINT Side = 0; Side < 3; Side++)
    {
        EdgeOdd[Side] = Integer ? RxTessIsOdd(Edge[Side]) : FractionalOdd;
        EdgeFixed[Side] = RxTessToFixed(Edge[Side]);
        AllOnes = AllOnes && (EdgeFixed[Side] == RxTessOne);
    }
    InsideOdd = Integer ? (RxTessIsOdd(Inside) && RxTessFlush(Inside) != 1.0f) : FractionalOdd;
    InsideFixed = RxTessToFixed(Inside);
    AllOnes = AllOnes && (InsideFixed == RxTessOne);

    if ((Integer || FractionalOdd) && AllOnes)
    {
        AddPoint(0, RxTessOne);
        AddPoint(0, 0);
        AddPoint(RxTessOne, 0);
        if (Output == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
        {
            for (UINT Index = 0; Index < 3; Index++)
                Indices.push_back(Index);
        }
        else if (Output != D3D11_SB_TESSELLATOR_OUTPUT_LINE)
        {
            AddTriangle(0, 1, 2);
        }
        return;
    }

    for (UINT Side = 0; Side < 3; Side++)
        RxTessMakeScale(EdgeFixed[Side], EdgeOdd[Side], &EdgeScale[Side]);
    RxTessMakeScale(InsideFixed, InsideOdd, &InsideScale);
    InsidePoints = InsideScale.Points;
    if (InsidePoints < (InsideOdd ? 4 : 3))
        InsidePoints = InsideOdd ? 4 : 3;

    /* Outer edges, clockwise from the U == 0 edge */
    for (UINT Side = 0; Side < 3; Side++)
    {
        INT Last = EdgeScale[Side].Points - 1;

        for (INT Point = 0; Point < Last; Point++)
        {
            UINT Along = RxTessPlace(&EdgeScale[Side], (Side & 1) ? Point : Last - Point);

            if (Side == 0)
                AddPoint(0, Along);
            else
                AddPoint(Along, (Side == 2) ? RxTessOne - Along : 0);
        }
    }

    /* Inner rings spiral inward; points move in by two thirds of the edge parameter */
    for (INT Ring = 1; Ring < (InsidePoints >> 1); Ring++)
    {
        INT End = InsidePoints - 1 - Ring;

        for (UINT Side = 0; Side < 3; Side++)
        {
            UINT Depth = (RxTessPlace(&InsideScale, Ring) * RxTessTwoThirds + RxTessHalf) >> 16;
            UINT Pull = (Depth + 1) >> 1;

            for (INT Point = Ring; Point < End; Point++)
            {
                UINT Along = RxTessPlace(&InsideScale, (Side & 1) ? Point : End - (Point - Ring));

                if (Side == 0)
                    AddPoint(Depth, Along - Pull);
                else if (Side == 1)
                    AddPoint(Along - Pull, Depth);
                else
                    AddPoint(Along - Pull, RxTessOne - (Along - Pull) - Depth);
            }
        }
    }
    if (!InsideOdd)
        AddPoint(RxTessThird, RxTessThird);

    if (Output == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
    {
        for (UINT Index = 0; Index < (UINT)Points.size(); Index++)
            Indices.push_back(Index);
        return;
    }
    if (Output == D3D11_SB_TESSELLATOR_OUTPUT_LINE)
        return;

    {
        INT Rings = (InsidePoints + 1) >> 1;
        const RX_WARP_EDGE_SCALE *OuterScale[3] = { &EdgeScale[0], &EdgeScale[1], &EdgeScale[2] };
        INT OuterPoints[3] = { EdgeScale[0].Points, EdgeScale[1].Points, EdgeScale[2].Points };
        INT InnerBase = OuterPoints[0] + OuterPoints[1] + OuterPoints[2] - 3;
        INT OuterBase = 0;

        for (INT Ring = 1; Ring < Rings; Ring++)
        {
            INT Count = InsidePoints - 2 * Ring;
            INT RingInner = InnerBase;
            INT RingOuter = OuterBase;

            for (UINT Side = 0; Side < 3; Side++)
            {
                SIZE_T Slots = Indices.size() + (SIZE_T)(Count + OuterPoints[Side] - 2) * 3;
                INT InnerAt = InnerBase;
                INT OuterAt = OuterBase;

                if (Side == 2)
                {
                    RemapMode = RxTessShifted;
                    InsideShift = InnerBase;
                    InsideWrap = Count - 1;
                    InsideFirst = RingInner;
                    OutsideStart = Count;
                    OutsideShift = OuterBase - Count;
                    OutsideWrap = Count + OuterPoints[Side] - 1;
                    OutsideFirst = RingOuter;
                    InnerAt = 0;
                    OuterAt = OutsideStart;
                }
                if (Ring == 1)
                    Transition(InnerAt, &InsideScale, OuterAt, OuterScale[Side]);
                else
                    Strip(TRUE, RxTessMirroredHalves, Count, InnerAt, OuterAt);
                RemapMode = RxTessPlain;
                Close(Slots);

                OuterBase += OuterPoints[Side] - 1;
                InnerBase += Count - 1;
                OuterPoints[Side] = Count;
            }
            if (Ring == 1)
            {
                for (UINT Side = 0; Side < 3; Side++)
                    OuterScale[Side] = &InsideScale;
            }
        }
        if (InsideOdd)
            AddTriangle(OuterBase, OuterBase + 1, OuterBase + 2);
    }
}

VOID
RxWarpTessellator::IsoLine(
    _In_reads_(2) const FLOAT *Factors)
{
    BOOLEAN Integer = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_INTEGER ||
                       Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_POW2);
    BOOLEAN FractionalOdd = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_ODD);
    FLOAT Low = (Partitioning == D3D11_SB_TESSELLATOR_PARTITIONING_FRACTIONAL_EVEN) ? 2.0f : 1.0f;
    FLOAT High = FractionalOdd ? 63.0f : 64.0f;
    FLOAT Detail;
    FLOAT Density;
    RX_WARP_EDGE_SCALE DetailScale;
    RX_WARP_EDGE_SCALE DensityScale;
    INT Lines;

    if (!(Factors[0] > 0.0f) || !(Factors[1] > 0.0f))
        return;

    Detail = RxTessClamp(Low, High, Factors[0]);
    Density = RxTessClamp(1.0f, 64.0f, Factors[1]);
    if (Integer)
        Detail = ceilf(RxTessFlush(Detail));
    RxTessMakeScale(RxTessToFixed(Detail), Integer ? RxTessIsOdd(Detail) : FractionalOdd, &DetailScale);

    /* Density always partitions as integers */
    Density = ceilf(RxTessFlush(Density));
    RxTessMakeScale(RxTessToFixed(Density), RxTessIsOdd(Density), &DensityScale);
    Lines = DensityScale.Points - 1;

    for (INT Line = 0; Line < Lines; Line++)
    {
        for (INT Point = 0; Point < DetailScale.Points; Point++)
            AddPoint(RxTessPlace(&DetailScale, Point), RxTessPlace(&DensityScale, Line));
    }

    for (INT Line = 0; Line < Lines; Line++)
    {
        for (INT Point = 0; Point < DetailScale.Points; Point++)
        {
            UINT Index = (UINT)(Line * DetailScale.Points + Point);

            if (Output == D3D11_SB_TESSELLATOR_OUTPUT_POINT)
            {
                Indices.push_back(Index);
            }
            else if (Point > 0)
            {
                Indices.push_back(Index - 1);
                Indices.push_back(Index);
            }
        }
    }
}

VOID
RxWarpTessellator::Tessellate(
    _In_ UINT Domain,
    _In_ UINT Spacing,
    _In_ UINT Primitive,
    _In_reads_(6) const FLOAT *Factors)
{
    Partitioning = Spacing;
    Output = Primitive;
    RemapMode = RxTessPlain;
    Points.clear();
    Indices.clear();

    switch (Domain)
    {
        case D3D11_SB_TESSELLATOR_DOMAIN_QUAD:
            Quad(Factors);
            break;
        case D3D11_SB_TESSELLATOR_DOMAIN_TRI:
            Triangle(Factors);
            break;
        case D3D11_SB_TESSELLATOR_DOMAIN_ISOLINE:
            IsoLine(Factors);
            break;
        default:
            break;
    }
}
