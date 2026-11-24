/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font variation axes and named instances
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "sfnt.hpp"

#include <math.h>

/* 16.16 fixed point limits, which also stand for the infinities */
constexpr INT32 RxFixedOne = 0x10000;
constexpr INT32 RxFixedMinimum = static_cast<INT32>(0x80000000);
constexpr INT32 RxFixedMaximum = 0x7FFFFFFF;

/* An axis value in 16.16 fixed point */
struct RxAxisValue
{
    DWRITE_FONT_AXIS_TAG Tag;
    INT32 Value;
};

/* One fvar axis */
struct RxVariationAxis
{
    DWRITE_FONT_AXIS_TAG Tag;
    INT32 Minimum;
    INT32 Default;
    INT32 Maximum;
    USHORT NameId;
    /* DWRITE_FONT_AXIS_ATTRIBUTES */
    USHORT Attributes;
};

/* fvar axes in file order; FALSE when fvar is missing or damaged */
BOOLEAN
RxReadVariationAxes(
    _In_ const RxSfnt& Sfnt,
    _Out_ std::vector<RxVariationAxis>& Axes);

/* Entries a font set makes of a face: one per named instance when fvar and STAT are both present */
USHORT
RxNamedInstanceCount(
    _In_ const RxSfnt& Sfnt);

/* Defaults of every fvar axis, replaced by the coordinates of a named instance when Instance names one */
BOOLEAN
RxNamedInstanceValues(
    _In_ const RxSfnt& Sfnt,
    _In_ USHORT Instance,
    _Out_ std::vector<RxAxisValue>& Values);

/* Nearest 16.16 value, saturated */
INT32
RxFloatToFixed(
    _In_ FLOAT Value);

/* The limits read back as the infinities they stand for */
inline
FLOAT
RxFixedToFloat(
    _In_ INT32 Value)
{
    if (Value == RxFixedMaximum)
        return INFINITY;
    if (Value == RxFixedMinimum)
        return -INFINITY;
    return static_cast<FLOAT>(Value) / RxFixedOne;
}

/* Fixed point subtraction that keeps the infinities and saturates */
INT32
RxFixedSubtract(
    _In_ INT32 Left,
    _In_ INT32 Right);
