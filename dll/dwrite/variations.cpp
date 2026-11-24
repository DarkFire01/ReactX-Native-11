/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font variation axes and named instances
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "variations.hpp"

/* fvar header fields */
constexpr UINT32 RxFvarHeaderSize = 16;
constexpr UINT32 RxFvarAxisRecordSize = 20;

BOOLEAN
RxReadVariationAxes(
    _In_ const RxSfnt& Sfnt,
    _Out_ std::vector<RxVariationAxis>& Axes)
{
    RxSpan Fvar = Sfnt.Table(RxTag('f', 'v', 'a', 'r'));
    UINT32 Offset;
    USHORT Count;
    USHORT Size;

    Axes.clear();
    if (Fvar.Length() < RxFvarHeaderSize)
        return FALSE;
    if (Fvar.U16(0) != 1)
        return TRUE;

    Offset = Fvar.U16(4);
    Count = Fvar.U16(8);
    Size = Fvar.U16(10);
    if (Size < RxFvarAxisRecordSize)
        return FALSE;

    for (USHORT Index = 0; Index < Count; Index++)
    {
        UINT32 Record = Offset + static_cast<UINT32>(Index) * Size;
        RxVariationAxis Axis;

        if (!Fvar.Has(Record, RxFvarAxisRecordSize))
        {
            Axes.clear();
            return FALSE;
        }
        Axis.Tag = static_cast<DWRITE_FONT_AXIS_TAG>(RxDWriteTag(Fvar.U32(Record)));
        Axis.Minimum = Fvar.S32(Record + 4);
        Axis.Default = Fvar.S32(Record + 8);
        Axis.Maximum = Fvar.S32(Record + 12);
        Axis.NameId = Fvar.U16(Record + 18);
        Axis.Attributes = static_cast<USHORT>((Axis.Maximum > Axis.Minimum ? DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE : 0) |
                                              ((Fvar.U16(Record + 16) & 1) ? DWRITE_FONT_AXIS_ATTRIBUTES_HIDDEN : 0));
        Axes.push_back(Axis);
    }
    return TRUE;
}

USHORT
RxNamedInstanceCount(
    _In_ const RxSfnt& Sfnt)
{
    RxSpan Fvar = Sfnt.Table(RxTag('f', 'v', 'a', 'r'));
    USHORT Count;

    if (Fvar.IsEmpty() || !Sfnt.HasTable(RxTag('S', 'T', 'A', 'T')) || Fvar.Length() < RxFvarHeaderSize)
        return 1;
    Count = Fvar.U16(12);
    return Count != 0 ? Count : 1;
}

BOOLEAN
RxNamedInstanceValues(
    _In_ const RxSfnt& Sfnt,
    _In_ USHORT Instance,
    _Out_ std::vector<RxAxisValue>& Values)
{
    RxSpan Fvar = Sfnt.Table(RxTag('f', 'v', 'a', 'r'));
    UINT32 Record;
    USHORT Count;
    USHORT Size;

    Values.clear();
    if (Fvar.IsEmpty() || Fvar.Length() < RxFvarHeaderSize)
        return FALSE;

    Count = Fvar.U16(8);
    Size = Fvar.U16(10);
    if (Size < RxFvarAxisRecordSize)
        return FALSE;

    Record = Fvar.U16(4);
    for (USHORT Index = 0; Index < Count; Index++, Record += Size)
    {
        if (!Fvar.Has(Record, RxFvarAxisRecordSize))
        {
            Values.clear();
            return FALSE;
        }
        Values.push_back({ static_cast<DWRITE_FONT_AXIS_TAG>(RxDWriteTag(Fvar.U32(Record))), Fvar.S32(Record + 8) });
    }

    /* Instance records are located as if the axes start right after the header and use the minimum record size */
    if (Instance < Fvar.U16(12) && Sfnt.HasTable(RxTag('S', 'T', 'A', 'T')))
    {
        UINT32 Coordinates = static_cast<UINT32>(Fvar.U16(14)) * Instance + RxFvarAxisRecordSize * Count +
                             RxFvarHeaderSize + 4;

        if (!Fvar.Has(Coordinates, 4 * static_cast<UINT32>(Count)))
        {
            Values.clear();
            return FALSE;
        }
        for (USHORT Index = 0; Index < Count; Index++)
            Values[Index].Value = Fvar.S32(Coordinates + 4 * Index);
    }
    return TRUE;
}

INT32
RxFixedSubtract(
    _In_ INT32 Left,
    _In_ INT32 Right)
{
    INT64 Difference;

    if (Left == RxFixedMinimum || Left == RxFixedMaximum)
        return Left;
    if (Right == RxFixedMaximum)
        return RxFixedMinimum;
    if (Right == RxFixedMinimum)
        return RxFixedMaximum;

    Difference = static_cast<INT64>(Left) - Right;
    if (Difference < RxFixedMinimum)
        return RxFixedMinimum;
    if (Difference > RxFixedMaximum)
        return RxFixedMaximum;
    return static_cast<INT32>(Difference);
}
