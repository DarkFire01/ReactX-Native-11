/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fixed function tessellator: domain points and their primitives for one patch
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <vector>

typedef struct _RX_WARP_DOMAIN_POINT
{
    FLOAT U;
    FLOAT V;
} RX_WARP_DOMAIN_POINT;

/* How one tess factor spreads points along an edge, in 16.16 fixed point */
typedef struct _RX_WARP_EDGE_SCALE
{
    BOOLEAN Odd;
    UINT FloorStep;             /* one segment of the factor rounded down */
    UINT CeilStep;              /* one segment of the factor rounded up */
    UINT Fraction;              /* blend between the two */
    INT HalfPoints;             /* points on half the edge */
    INT Split;                  /* points after this one sit one place earlier on the floor factor */
    INT Points;                 /* points on the whole edge */
} RX_WARP_EDGE_SCALE;

class RxWarpTessellator
{
public:
    /*
     * Domain, partitioning and output primitive take the tokenized program values.
     * Factors: quad Ueq0, Veq0, Ueq1, Veq1, inside U, inside V; tri Ueq0, Veq0, Weq0, inside;
     * isoline detail, density.
     */
    VOID
    Tessellate(
        _In_ UINT Domain,
        _In_ UINT Spacing,
        _In_ UINT Primitive,
        _In_reads_(6) const FLOAT *Factors);

    std::vector<RX_WARP_DOMAIN_POINT> Points;
    std::vector<UINT> Indices;

private:
    VOID
    Quad(
        _In_reads_(6) const FLOAT *Factors);

    VOID
    Triangle(
        _In_reads_(4) const FLOAT *Factors);

    VOID
    IsoLine(
        _In_reads_(2) const FLOAT *Factors);

    VOID
    AddPoint(
        _In_ UINT U,
        _In_ UINT V);

    VOID
    AddTriangle(
        _In_ INT First,
        _In_ INT Second,
        _In_ INT Third);

    INT
    Remap(
        _In_ INT Index);

    VOID
    Strip(
        _In_ BOOLEAN Ends,
        _In_ UINT Diagonals,
        _In_ INT InsideCount,
        _In_ INT Inside,
        _In_ INT Outside);

    VOID
    Transition(
        _In_ INT Inside,
        _In_ const RX_WARP_EDGE_SCALE *InsideScale,
        _In_ INT Outside,
        _In_ const RX_WARP_EDGE_SCALE *OutsideScale);

    VOID
    Close(
        _In_ SIZE_T Count);

    UINT Partitioning = 0;
    UINT Output = 0;

    /* Index fixups while the last side of a ring wraps around to its first point */
    UINT RemapMode = 0;
    INT InsideShift = 0;
    INT InsideWrap = 0;
    INT InsideFirst = 0;
    INT OutsideStart = 0;
    INT OutsideShift = 0;
    INT OutsideWrap = 0;
    INT OutsideFirst = 0;
    INT MirrorStart = 0;
    INT MirrorEnd = 0;
    INT MirrorCorner = 0;
    INT MirrorCornerValue = 0;
};
