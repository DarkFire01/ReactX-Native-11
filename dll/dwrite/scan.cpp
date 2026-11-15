/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Scan converter for glyph outlines
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "scan.hpp"

#include <cmath>

namespace
{

struct RxPointF
{
    double X;
    double Y;
};

/* A line or a quadratic Bezier, y monotonic, in sample units with y growing downward */
struct RxEdge
{
    RxPointF Start;
    RxPointF Control;
    RxPointF End;
    BOOLEAN IsCurve;
    /* +1 when the edge runs downward */
    INT32 Winding;
    double Top;
    double Bottom;
};

struct RxCrossing
{
    double Position;
    INT32 Winding;
};

class RxEdgeList
{
public:
    std::vector<RxEdge> Edges;

    VOID
    AddLine(
        _In_ const RxPointF& A,
        _In_ const RxPointF& B)
    {
        RxEdge Edge;

        if (A.Y == B.Y)
            return;
        Edge.Start = A;
        Edge.Control = A;
        Edge.End = B;
        Edge.IsCurve = FALSE;
        Edge.Winding = B.Y > A.Y ? 1 : -1;
        Edge.Top = std::min(A.Y, B.Y);
        Edge.Bottom = std::max(A.Y, B.Y);
        Edges.push_back(Edge);
    }

    VOID
    AddCurve(
        _In_ const RxPointF& A,
        _In_ const RxPointF& C,
        _In_ const RxPointF& B)
    {
        double Denominator = A.Y - 2.0 * C.Y + B.Y;

        /* Split at the vertical turning point so each piece is monotonic */
        if (Denominator != 0.0)
        {
            double T = (A.Y - C.Y) / Denominator;

            if (T > 0.0 && T < 1.0)
            {
                RxPointF Left = { A.X + (C.X - A.X) * T, A.Y + (C.Y - A.Y) * T };
                RxPointF Right = { C.X + (B.X - C.X) * T, C.Y + (B.Y - C.Y) * T };
                RxPointF Middle = { Left.X + (Right.X - Left.X) * T, Left.Y + (Right.Y - Left.Y) * T };

                AddMonotonicCurve(A, Left, Middle);
                AddMonotonicCurve(Middle, Right, B);
                return;
            }
        }
        AddMonotonicCurve(A, C, B);
    }

private:
    VOID
    AddMonotonicCurve(
        _In_ const RxPointF& A,
        _In_ const RxPointF& C,
        _In_ const RxPointF& B)
    {
        RxEdge Edge;

        if (A.Y == B.Y)
            return;
        Edge.Start = A;
        Edge.Control = C;
        Edge.End = B;
        Edge.IsCurve = TRUE;
        Edge.Winding = B.Y > A.Y ? 1 : -1;
        Edge.Top = std::min(A.Y, B.Y);
        Edge.Bottom = std::max(A.Y, B.Y);
        Edges.push_back(Edge);
    }
};

/* Where an edge crosses the horizontal line Y, which lies inside its span */
double
RxEdgeCrossing(
    _In_ const RxEdge& Edge,
    _In_ double Y)
{
    double T;

    if (!Edge.IsCurve)
    {
        T = (Y - Edge.Start.Y) / (Edge.End.Y - Edge.Start.Y);
        return Edge.Start.X + (Edge.End.X - Edge.Start.X) * T;
    }

    double A = Edge.Start.Y - 2.0 * Edge.Control.Y + Edge.End.Y;
    double B = 2.0 * (Edge.Control.Y - Edge.Start.Y);
    double C = Edge.Start.Y - Y;

    if (std::fabs(A) < 1e-12)
    {
        T = -C / B;
    }
    else
    {
        double Discriminant = std::max(0.0, B * B - 4.0 * A * C);
        double Root = std::sqrt(Discriminant);
        double T1 = (-B + Root) / (2.0 * A);
        double T2 = (-B - Root) / (2.0 * A);

        T = (T1 >= -1e-9 && T1 <= 1.0 + 1e-9) ? T1 : T2;
    }

    T = std::min(1.0, std::max(0.0, T));
    return (1.0 - T) * (1.0 - T) * Edge.Start.X + 2.0 * T * (1.0 - T) * Edge.Control.X + T * T * Edge.End.X;
}

/* Turns TrueType quadratic contours into edges, y flipped to grow downward */
VOID
RxBuildEdges(
    _In_reads_(PointCount) const RxHintPoint* Points,
    _In_ UINT32 PointCount,
    _In_reads_(ContourCount) const USHORT* ContourEnds,
    _In_ UINT32 ContourCount,
    _In_ BOOLEAN Transpose,
    _Inout_ RxEdgeList& List)
{
    UINT32 Start = 0;

    for (UINT32 Contour = 0; Contour < ContourCount; Contour++)
    {
        UINT32 End = ContourEnds[Contour];
        UINT32 Count;
        std::vector<RxPointF> Coordinates;
        std::vector<BYTE> OnCurve;
        RxPointF First;
        RxPointF Current;
        UINT32 Begin = 0;

        if (End >= PointCount || End < Start)
            break;
        Count = End - Start + 1;

        for (UINT32 Index = Start; Index <= End; Index++)
        {
            RxPointF Point = { Points[Index].X / 64.0, -Points[Index].Y / 64.0 };

            if (Transpose)
                std::swap(Point.X, Point.Y);
            Coordinates.push_back(Point);
            OnCurve.push_back(Points[Index].OnCurve);
        }
        Start = End + 1;

        if (Count < 2)
            continue;

        /* Start from an on curve point, or the midpoint of two off curve points */
        while (Begin < Count && !OnCurve[Begin])
            Begin++;
        if (Begin == Count)
        {
            First = { (Coordinates[0].X + Coordinates[1].X) / 2.0, (Coordinates[0].Y + Coordinates[1].Y) / 2.0 };
            Begin = 1;
        }
        else
        {
            First = Coordinates[Begin];
        }

        Current = First;
        {
            BOOLEAN HaveControl = FALSE;
            RxPointF Control = {};

            for (UINT32 Step = 1; Step <= Count; Step++)
            {
                UINT32 Index = (Begin + Step) % Count;
                RxPointF Point = Coordinates[Index];

                /* The last step returns to where the contour started */
                if (Step == Count)
                    Point = First;

                if (Step == Count || OnCurve[Index])
                {
                    if (HaveControl)
                        List.AddCurve(Current, Control, Point);
                    else
                        List.AddLine(Current, Point);
                    Current = Point;
                    HaveControl = FALSE;
                    continue;
                }

                if (HaveControl)
                {
                    RxPointF Middle = { (Control.X + Point.X) / 2.0, (Control.Y + Point.Y) / 2.0 };

                    List.AddCurve(Current, Control, Middle);
                    Current = Middle;
                }
                Control = Point;
                HaveControl = TRUE;
            }
        }
    }
}

/* All crossings of sample row centers, by row; rows are absolute sample indices */
VOID
RxCollectCrossings(
    _In_ const RxEdgeList& List,
    _In_ INT32 FirstRow,
    _In_ INT32 RowCount,
    _Inout_ std::vector<std::vector<RxCrossing>>& Rows)
{
    Rows.assign(RowCount, std::vector<RxCrossing>());

    for (const auto& Edge : List.Edges)
    {
        /* Rows whose center lies in [Top, Bottom) */
        INT32 Row = static_cast<INT32>(std::ceil(Edge.Top - 0.5));
        INT32 Last = static_cast<INT32>(std::ceil(Edge.Bottom - 0.5)) - 1;

        for (; Row <= Last; Row++)
        {
            double Center = Row + 0.5;

            if (Row < FirstRow || Row >= FirstRow + RowCount)
                continue;
            Rows[Row - FirstRow].push_back({ RxEdgeCrossing(Edge, Center), Edge.Winding });
        }
    }

    for (auto& Crossings : Rows)
    {
        std::sort(Crossings.begin(), Crossings.end(),
                  [](const RxCrossing& A, const RxCrossing& B) { return A.Position < B.Position; });
    }
}

VOID
RxSetSample(
    _Inout_ RxSampleMask* Mask,
    _In_ INT32 X,
    _In_ INT32 Y)
{
    if (X < Mask->Left || X >= Mask->Right || Y < Mask->Top || Y >= Mask->Bottom)
        return;
    UINT32 Column = static_cast<UINT32>(X - Mask->Left);
    Mask->Bits[(Y - Mask->Top) * Mask->Stride + Column / 8] |= static_cast<BYTE>(0x80 >> (Column % 8));
}

/*
 * Fills spans along one direction. Gaps narrower than a sample are dropouts; when Dropout asks
 * for it a sample is turned on for them, the one nearest the gap's middle for the smart modes.
 */
VOID
RxFillDirection(
    _In_ const std::vector<std::vector<RxCrossing>>& Rows,
    _In_ INT32 FirstRow,
    _In_ BOOLEAN Transposed,
    _In_ BOOLEAN FillSpans,
    _In_ const RxDropoutControl& Dropout,
    _Inout_ RxSampleMask* Mask)
{
    for (SIZE_T RowIndex = 0; RowIndex < Rows.size(); RowIndex++)
    {
        const auto& Crossings = Rows[RowIndex];
        INT32 Row = FirstRow + static_cast<INT32>(RowIndex);
        INT32 Winding = 0;

        for (SIZE_T Index = 0; Index + 1 < Crossings.size(); Index++)
        {
            double From;
            double To;
            INT32 First;
            INT32 Last;

            Winding += Crossings[Index].Winding;
            if (Winding == 0)
                continue;

            From = Crossings[Index].Position;
            To = Crossings[Index + 1].Position;

            /* Sample centers on or inside the span */
            First = static_cast<INT32>(std::ceil(From - 0.5));
            Last = static_cast<INT32>(std::floor(To - 0.5));

            if (First <= Last)
            {
                if (!FillSpans)
                    continue;
                for (INT32 Column = First; Column <= Last; Column++)
                {
                    if (Transposed)
                        RxSetSample(Mask, Row, Column);
                    else
                        RxSetSample(Mask, Column, Row);
                }
                continue;
            }

            if (!Dropout.Enabled || Dropout.Mode == 2 || Dropout.Mode == 3)
                continue;

            {
                INT32 Column;

                /* Rule 3 takes the left or lower sample; rules 5 and 6 the one nearest the middle */
                if (Dropout.Mode == 4 || Dropout.Mode == 5)
                    Column = static_cast<INT32>(std::floor((From + To) / 2.0));
                else
                    Column = Last;

                if (Transposed)
                {
                    if (!Mask->Sample(Row, Column) && !Mask->Sample(Row, Column + 1))
                        RxSetSample(Mask, Row, Column);
                }
                else if (!Mask->Sample(Column, Row) && !Mask->Sample(Column + 1, Row))
                {
                    RxSetSample(Mask, Column, Row);
                }
            }
        }
    }
}

}

HRESULT
RxScanConvert(
    _In_reads_(PointCount) const RxHintPoint* Points,
    _In_ UINT32 PointCount,
    _In_reads_(ContourCount) const USHORT* ContourEnds,
    _In_ UINT32 ContourCount,
    _In_ const RxDropoutControl& Dropout,
    _Out_ RxSampleMask* Mask)
{
    RxEdgeList Rows;
    std::vector<std::vector<RxCrossing>> Crossings;
    double MinX = 0.0;
    double MaxX = 0.0;
    double MinY = 0.0;
    double MaxY = 0.0;
    BOOLEAN Any = FALSE;

    Mask->Left = Mask->Top = Mask->Right = Mask->Bottom = 0;
    Mask->Stride = 0;
    Mask->Bits.clear();

    RxBuildEdges(Points, PointCount, ContourEnds, ContourCount, FALSE, Rows);
    if (Rows.Edges.empty())
        return S_OK;

    for (const auto& Edge : Rows.Edges)
    {
        const RxPointF* Corners[3] = { &Edge.Start, &Edge.Control, &Edge.End };

        for (const RxPointF* Corner : Corners)
        {
            if (!Any)
            {
                MinX = MaxX = Corner->X;
                MinY = MaxY = Corner->Y;
                Any = TRUE;
                continue;
            }
            MinX = std::min(MinX, Corner->X);
            MaxX = std::max(MaxX, Corner->X);
            MinY = std::min(MinY, Corner->Y);
            MaxY = std::max(MaxY, Corner->Y);
        }
    }

    /* The image covers the outline's control box rounded to the sample grid */
    Mask->Left = static_cast<INT32>(std::floor(MinX + 0.5));
    Mask->Right = static_cast<INT32>(std::floor(MaxX + 0.5));
    Mask->Top = static_cast<INT32>(std::floor(MinY + 0.5));
    Mask->Bottom = static_cast<INT32>(std::floor(MaxY + 0.5));
    if (Mask->Right == Mask->Left)
        Mask->Right++;
    if (Mask->Bottom == Mask->Top)
        Mask->Bottom++;
    if (static_cast<INT64>(Mask->Right - Mask->Left) * (Mask->Bottom - Mask->Top) > 64 * 1024 * 1024)
        return E_OUTOFMEMORY;

    Mask->Stride = (Mask->Width() + 7) / 8;
    Mask->Bits.assign(static_cast<SIZE_T>(Mask->Stride) * Mask->Height(), 0);

    RxCollectCrossings(Rows, Mask->Top, Mask->Height(), Crossings);
    RxFillDirection(Crossings, Mask->Top, FALSE, TRUE, Dropout, Mask);

    /* Vertical scan lines only find dropouts */
    if (Dropout.Enabled && Dropout.Mode != 2 && Dropout.Mode != 3)
    {
        RxEdgeList Columns;

        RxBuildEdges(Points, PointCount, ContourEnds, ContourCount, TRUE, Columns);
        RxCollectCrossings(Columns, Mask->Left, Mask->Width(), Crossings);
        RxFillDirection(Crossings, Mask->Left, TRUE, FALSE, Dropout, Mask);
    }
    return S_OK;
}
