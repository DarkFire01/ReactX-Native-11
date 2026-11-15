/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Scan converter for glyph outlines
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "hinter.hpp"

/*
 * A one bit image of an outline, sampled at sample centers on its own grid.
 * The grid is the pixel grid scaled up by the oversampling factors of the rendering mode.
 */
struct RxSampleMask
{
    /* Bounds in samples, Right and Bottom exclusive; y grows downward */
    INT32 Left;
    INT32 Top;
    INT32 Right;
    INT32 Bottom;
    /* Bytes per row; bit 7 of a byte is its leftmost sample */
    UINT32 Stride;
    std::vector<BYTE> Bits;

    UINT32
    Width(VOID) const
    {
        return static_cast<UINT32>(Right - Left);
    }

    UINT32
    Height(VOID) const
    {
        return static_cast<UINT32>(Bottom - Top);
    }

    BOOLEAN
    Sample(
        _In_ INT32 X,
        _In_ INT32 Y) const
    {
        if (X < Left || X >= Right || Y < Top || Y >= Bottom)
            return FALSE;
        UINT32 Column = static_cast<UINT32>(X - Left);
        return (Bits[(Y - Top) * Stride + Column / 8] & (0x80 >> (Column % 8))) != 0;
    }
};

/*
 * Converts an outline in 26.6 sample units, y up, into a sample mask.
 * Samples are on when their center is inside the outline by the nonzero rule.
 */
HRESULT
RxScanConvert(
    _In_reads_(PointCount) const RxHintPoint* Points,
    _In_ UINT32 PointCount,
    _In_reads_(ContourCount) const USHORT* ContourEnds,
    _In_ UINT32 ContourCount,
    _In_ const RxDropoutControl& Dropout,
    _Out_ RxSampleMask* Mask);
