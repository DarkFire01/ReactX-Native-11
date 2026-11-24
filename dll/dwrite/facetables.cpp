/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Parsed tables of one font face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "facetables.hpp"

/* Composite glyph component flags */
constexpr USHORT RxComponentWordArgs = 0x0001;
constexpr USHORT RxComponentXyValues = 0x0002;
constexpr USHORT RxComponentScale = 0x0008;
constexpr USHORT RxComponentMore = 0x0020;
constexpr USHORT RxComponentXyScale = 0x0040;
constexpr USHORT RxComponentTwoByTwo = 0x0080;
constexpr USHORT RxComponentInstructions = 0x0100;
constexpr USHORT RxComponentScaledOffset = 0x0800;

/* Simple glyph point flags */
constexpr BYTE RxPointOnCurve = 0x01;
constexpr BYTE RxPointShortX = 0x02;
constexpr BYTE RxPointShortY = 0x04;
constexpr BYTE RxPointRepeat = 0x08;
constexpr BYTE RxPointSameX = 0x10;
constexpr BYTE RxPointSameY = 0x20;

/* Nesting beyond this is treated as a broken font */
constexpr ULONG RxMaxComponentDepth = 16;

HRESULT
RxFaceTables::Create(
    _In_ IDWriteFontFile* File,
    _In_ UINT32 FaceIndex,
    _Outptr_ RxFaceTables** Tables)
{
    RxFaceTables* Result;
    HRESULT hr;

    *Tables = nullptr;

    Result = new (std::nothrow) RxFaceTables();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->FontFile = RxComPtr<IDWriteFontFile>(File);
    Result->FaceIndex = FaceIndex;

    hr = Result->Data.Load(File);
    if (SUCCEEDED(hr))
        hr = Result->Parse();
    if (FAILED(hr))
    {
        Result->Release();
        return hr;
    }

    *Tables = Result;
    return S_OK;
}

HRESULT
RxFaceTables::Parse(VOID)
{
    RxSpan Head;
    RxSpan Hhea;
    RxSpan Vhea;
    RxSpan Maxp;
    RxSpan Os2;
    RxSpan Post;

    if (!Directory.Open(Data.Span(), FaceIndex))
        return DWRITE_E_FILEFORMAT;

    Head = Directory.Table(RxTag('h', 'e', 'a', 'd'));
    Hhea = Directory.Table(RxTag('h', 'h', 'e', 'a'));
    Maxp = Directory.Table(RxTag('m', 'a', 'x', 'p'));
    if (!Head.Has(0, 54) || !Hhea.Has(0, 36) || !Maxp.Has(0, 6))
        return DWRITE_E_FILEFORMAT;

    Fields.UnitsPerEm = Head.U16(18);
    Fields.XMin = Head.S16(36);
    Fields.YMin = Head.S16(38);
    Fields.XMax = Head.S16(40);
    Fields.YMax = Head.S16(42);
    Fields.MacStyle = Head.U16(44);
    Fields.IndexToLocFormat = Head.S16(50);
    if (Fields.UnitsPerEm < 16 || Fields.UnitsPerEm > 16384)
        return DWRITE_E_FILEFORMAT;

    Fields.HheaAscender = Hhea.S16(4);
    Fields.HheaDescender = Hhea.S16(6);
    Fields.HheaLineGap = Hhea.S16(8);
    Fields.CaretSlopeRise = Hhea.S16(18);
    Fields.CaretSlopeRun = Hhea.S16(20);
    Fields.CaretOffset = Hhea.S16(22);
    Fields.HMetricCount = Hhea.U16(34);

    Fields.GlyphCount = Maxp.U16(4);
    if (Maxp.U32(0) == 0x00010000 && Maxp.Has(0, 32))
    {
        Fields.MaxPoints = Maxp.U16(6);
        Fields.MaxContours = Maxp.U16(8);
        Fields.MaxComponentPoints = Maxp.U16(10);
        Fields.MaxComponentContours = Maxp.U16(12);
        Fields.MaxZones = Maxp.U16(14);
        Fields.MaxTwilightPoints = Maxp.U16(16);
        Fields.MaxStorage = Maxp.U16(18);
        Fields.MaxFunctionDefs = Maxp.U16(20);
        Fields.MaxInstructionDefs = Maxp.U16(22);
        Fields.MaxStackElements = Maxp.U16(24);
    }

    Vhea = Directory.Table(RxTag('v', 'h', 'e', 'a'));
    if (Vhea.Has(0, 36))
    {
        Fields.HasVhea = TRUE;
        Fields.VheaAscent = Vhea.S16(4);
        Fields.VheaDescent = Vhea.S16(6);
        Fields.VheaLineGap = Vhea.S16(8);
        Fields.VMetricCount = Vhea.U16(34);
    }

    Os2 = Directory.Table(RxTag('O', 'S', '/', '2'));
    if (Os2.Has(0, 78))
    {
        Fields.HasOs2 = TRUE;
        Fields.Os2Version = Os2.U16(0);
        Fields.XAvgCharWidth = Os2.S16(2);
        Fields.WeightClass = Os2.U16(4);
        Fields.WidthClass = Os2.U16(6);
        Fields.FsType = Os2.U16(8);
        Fields.SubscriptXSize = Os2.S16(10);
        Fields.SubscriptYSize = Os2.S16(12);
        Fields.SubscriptXOffset = Os2.S16(14);
        Fields.SubscriptYOffset = Os2.S16(16);
        Fields.SuperscriptXSize = Os2.S16(18);
        Fields.SuperscriptYSize = Os2.S16(20);
        Fields.SuperscriptXOffset = Os2.S16(22);
        Fields.SuperscriptYOffset = Os2.S16(24);
        Fields.StrikeoutSize = Os2.S16(26);
        Fields.StrikeoutPosition = Os2.S16(28);
        Fields.FamilyClass = Os2.S16(30);
        for (UINT32 Index = 0; Index < 10; Index++)
            Fields.Panose[Index] = Os2.U8(32 + Index);
        for (UINT32 Index = 0; Index < 4; Index++)
            Fields.UnicodeRange[Index] = Os2.U32(42 + Index * 4);
        Fields.VendorId = Os2.U32(58);
        Fields.FsSelection = Os2.U16(62);
        Fields.FirstCharIndex = Os2.U16(64);
        Fields.LastCharIndex = Os2.U16(66);
        Fields.TypoAscender = Os2.S16(68);
        Fields.TypoDescender = Os2.S16(70);
        Fields.TypoLineGap = Os2.S16(72);
        Fields.WinAscent = Os2.U16(74);
        Fields.WinDescent = Os2.U16(76);
        if (Fields.Os2Version >= 1 && Os2.Has(0, 86))
        {
            Fields.CodePageRange[0] = Os2.U32(78);
            Fields.CodePageRange[1] = Os2.U32(82);
        }
        if (Fields.Os2Version >= 2 && Os2.Has(0, 90))
        {
            Fields.XHeight = Os2.S16(86);
            Fields.CapHeight = Os2.S16(88);
        }
    }

    Post = Directory.Table(RxTag('p', 'o', 's', 't'));
    if (Post.Has(0, 16))
    {
        Fields.ItalicAngle = Post.S32(4);
        Fields.UnderlinePosition = Post.S16(8);
        Fields.UnderlineThickness = Post.S16(10);
        Fields.IsFixedPitch = Post.U32(12);
    }

    Hmtx = Directory.Table(RxTag('h', 'm', 't', 'x'));
    Vmtx = Directory.Table(RxTag('v', 'm', 't', 'x'));
    Loca = Directory.Table(RxTag('l', 'o', 'c', 'a'));
    Glyf = Directory.Table(RxTag('g', 'l', 'y', 'f'));
    Name = Directory.Table(RxTag('n', 'a', 'm', 'e'));

    Type = Directory.IsCff() ? DWRITE_FONT_FACE_TYPE_CFF : DWRITE_FONT_FACE_TYPE_TRUETYPE;
    if (Data.Span().U32(0) == RxSfntCollection)
        Type = DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION;

    SelectCmap();
    return S_OK;
}

/* cmap */

VOID
RxFaceTables::SelectCmap(VOID)
{
    /* Best first: full Unicode, then BMP Unicode, then symbol */
    static const struct
    {
        USHORT Platform;
        USHORT Encoding;
        BOOLEAN Symbol;
    } Preference[] =
    {
        { 3, 10, FALSE },
        { 0, 6, FALSE },
        { 0, 4, FALSE },
        { 3, 1, FALSE },
        { 0, 3, FALSE },
        { 0, 2, FALSE },
        { 0, 1, FALSE },
        { 0, 0, FALSE },
        { 3, 0, TRUE },
    };
    RxSpan Table = Directory.Table(RxTag('c', 'm', 'a', 'p'));
    USHORT Count = Table.U16(2);

    for (const auto& Wanted : Preference)
    {
        for (USHORT Index = 0; Index < Count; Index++)
        {
            UINT32 Record = 4 + Index * 8;
            RxSpan Subtable;
            USHORT Format;

            if (Table.U16(Record) != Wanted.Platform || Table.U16(Record + 2) != Wanted.Encoding)
                continue;

            Subtable = Table.Sub(Table.U32(Record + 4));
            Format = Subtable.U16(0);
            /* Format 14 holds variation sequences only */
            if (Format != 0 && Format != 4 && Format != 6 && Format != 10 && Format != 12 && Format != 13)
                continue;

            Cmap = Subtable;
            CmapFormat = Format;
            CmapIsSymbol = Wanted.Symbol;
            return;
        }
    }
}

static
USHORT
RxLookupFormat4(
    _In_ const RxSpan& Table,
    _In_ UINT32 CodePoint)
{
    UINT32 SegmentCount = Table.U16(6) / 2;
    UINT32 EndCodes = 14;
    UINT32 StartCodes = EndCodes + SegmentCount * 2 + 2;
    UINT32 Deltas = StartCodes + SegmentCount * 2;
    UINT32 RangeOffsets = Deltas + SegmentCount * 2;
    UINT32 Low = 0;
    UINT32 High = SegmentCount;

    if (CodePoint > 0xFFFF)
        return 0;

    while (Low < High)
    {
        UINT32 Middle = (Low + High) / 2;
        USHORT End = Table.U16(EndCodes + Middle * 2);

        if (End < CodePoint)
        {
            Low = Middle + 1;
            continue;
        }

        USHORT Start = Table.U16(StartCodes + Middle * 2);
        if (Start > CodePoint)
        {
            High = Middle;
            continue;
        }

        USHORT Delta = Table.U16(Deltas + Middle * 2);
        UINT32 RangeAt = RangeOffsets + Middle * 2;
        USHORT RangeOffset = Table.U16(RangeAt);
        USHORT Glyph;

        if (RangeOffset == 0)
            return static_cast<USHORT>(CodePoint + Delta);

        Glyph = Table.U16(RangeAt + RangeOffset + (CodePoint - Start) * 2);
        return Glyph == 0 ? 0 : static_cast<USHORT>(Glyph + Delta);
    }
    return 0;
}

static
USHORT
RxLookupGroups(
    _In_ const RxSpan& Table,
    _In_ UINT32 CodePoint,
    _In_ BOOLEAN ManyToOne)
{
    UINT32 Count = Table.U32(12);
    UINT32 Low = 0;
    UINT32 High = Count;

    while (Low < High)
    {
        UINT32 Middle = (Low + High) / 2;
        UINT32 Group = 16 + Middle * 12;
        UINT32 Start = Table.U32(Group);
        UINT32 End = Table.U32(Group + 4);

        if (CodePoint < Start)
        {
            High = Middle;
        }
        else if (CodePoint > End)
        {
            Low = Middle + 1;
        }
        else
        {
            UINT32 Glyph = Table.U32(Group + 8);

            if (!ManyToOne)
                Glyph += CodePoint - Start;
            return Glyph > 0xFFFF ? 0 : static_cast<USHORT>(Glyph);
        }
    }
    return 0;
}

static
USHORT
RxLookupCmap(
    _In_ const RxSpan& Table,
    _In_ USHORT Format,
    _In_ UINT32 CodePoint)
{
    switch (Format)
    {
        case 0:
            return CodePoint < 256 ? Table.U8(6 + CodePoint) : 0;

        case 4:
            return RxLookupFormat4(Table, CodePoint);

        case 6:
        {
            UINT32 First = Table.U16(6);
            UINT32 Count = Table.U16(8);

            if (CodePoint < First || CodePoint - First >= Count)
                return 0;
            return Table.U16(10 + (CodePoint - First) * 2);
        }

        case 10:
        {
            UINT32 First = Table.U32(12);
            UINT32 Count = Table.U32(16);

            if (CodePoint < First || CodePoint - First >= Count)
                return 0;
            return Table.U16(20 + (CodePoint - First) * 2);
        }

        case 12:
            return RxLookupGroups(Table, CodePoint, FALSE);

        case 13:
            return RxLookupGroups(Table, CodePoint, TRUE);
    }
    return 0;
}

USHORT
RxFaceTables::GlyphIndex(
    _In_ UINT32 CodePoint) const
{
    USHORT Glyph;

    if (Cmap.IsEmpty())
        return 0;

    Glyph = RxLookupCmap(Cmap, CmapFormat, CodePoint);

    /* Symbol fonts map their characters into the private use area at F000 */
    if (Glyph == 0 && CmapIsSymbol && CodePoint <= 0xFF)
        Glyph = RxLookupCmap(Cmap, CmapFormat, CodePoint + 0xF000);

    return Glyph < Fields.GlyphCount ? Glyph : 0;
}

static
VOID
RxAddRange(
    _Inout_ std::vector<DWRITE_UNICODE_RANGE>& Ranges,
    _In_ UINT32 First,
    _In_ UINT32 Last)
{
    if (!Ranges.empty() && Ranges.back().last + 1 == First)
        Ranges.back().last = Last;
    else
        Ranges.push_back({ First, Last });
}

VOID
RxFaceTables::UnicodeRanges(
    _Out_ std::vector<DWRITE_UNICODE_RANGE>& Ranges) const
{
    Ranges.clear();
    if (Cmap.IsEmpty())
        return;

    switch (CmapFormat)
    {
        case 0:
            RxAddRange(Ranges, 0, 255);
            break;

        case 4:
        {
            UINT32 SegmentCount = Cmap.U16(6) / 2;
            UINT32 StartCodes = 14 + SegmentCount * 2 + 2;

            /* The closing 0xFFFF segment is not reported */
            for (UINT32 Segment = 0; Segment < SegmentCount && Cmap.Has(StartCodes + Segment * 2, 2); Segment++)
            {
                USHORT End = Cmap.U16(14 + Segment * 2);

                if (End == 0xFFFF)
                    break;
                RxAddRange(Ranges, Cmap.U16(StartCodes + Segment * 2), End);
            }
            break;
        }

        case 6:
            if (Cmap.U16(8) != 0)
                RxAddRange(Ranges, Cmap.U16(6), Cmap.U16(6) + Cmap.U16(8) - 1);
            break;

        case 10:
            if (Cmap.U32(16) != 0)
                RxAddRange(Ranges, Cmap.U32(12), Cmap.U32(12) + Cmap.U32(16) - 1);
            break;

        case 12:
        case 13:
        {
            UINT32 Count = Cmap.U32(12);

            for (UINT32 Group = 0; Group < Count && Cmap.Has(16 + Group * 12, 12); Group++)
                RxAddRange(Ranges, Cmap.U32(16 + Group * 12), Cmap.U32(20 + Group * 12));
            break;
        }
    }
}

/* Metrics */

VOID
RxFaceTables::HorizontalMetrics(
    _In_ USHORT Glyph,
    _Out_ USHORT* Advance,
    _Out_ SHORT* LeftSideBearing) const
{
    UINT32 Count = Fields.HMetricCount;

    *Advance = 0;
    *LeftSideBearing = 0;
    if (Count == 0)
        return;

    if (Glyph < Count)
    {
        *Advance = Hmtx.U16(Glyph * 4);
        *LeftSideBearing = Hmtx.S16(Glyph * 4 + 2);
        return;
    }

    *Advance = Hmtx.U16((Count - 1) * 4);
    *LeftSideBearing = Hmtx.S16(Count * 4 + (Glyph - Count) * 2);
}

BOOLEAN
RxFaceTables::VerticalMetrics(
    _In_ USHORT Glyph,
    _Out_ USHORT* Advance,
    _Out_ SHORT* TopSideBearing) const
{
    UINT32 Count = Fields.VMetricCount;

    *Advance = 0;
    *TopSideBearing = 0;
    if (!Fields.HasVhea || Count == 0 || Vmtx.IsEmpty())
        return FALSE;

    if (Glyph < Count)
    {
        *Advance = Vmtx.U16(Glyph * 4);
        *TopSideBearing = Vmtx.S16(Glyph * 4 + 2);
        return TRUE;
    }

    *Advance = Vmtx.U16((Count - 1) * 4);
    *TopSideBearing = Vmtx.S16(Count * 4 + (Glyph - Count) * 2);
    return TRUE;
}

/* glyf */

RxSpan
RxFaceTables::GlyphData(
    _In_ USHORT Glyph) const
{
    UINT32 Start;
    UINT32 End;

    if (Glyph >= Fields.GlyphCount || Loca.IsEmpty() || Glyf.IsEmpty())
        return RxSpan();

    if (Fields.IndexToLocFormat == 0)
    {
        Start = Loca.U16(Glyph * 2) * 2;
        End = Loca.U16(Glyph * 2 + 2) * 2;
    }
    else
    {
        Start = Loca.U32(Glyph * 4);
        End = Loca.U32(Glyph * 4 + 4);
    }

    if (End <= Start || !Glyf.Has(Start, End - Start))
        return RxSpan();
    return Glyf.Sub(Start, End - Start);
}

/* Reads the point arrays of a simple glyph and appends them to the outline */
static
BOOLEAN
RxReadSimpleGlyph(
    _In_ const RxSpan& Glyph,
    _In_ SHORT ContourCount,
    _Inout_ RxGlyphOutline& Outline)
{
    UINT32 Offset = 10;
    UINT32 PointCount;
    UINT32 InstructionLength;
    SIZE_T FirstPoint = Outline.Points.size();
    std::vector<BYTE> Flags;
    INT32 Value;

    for (SHORT Contour = 0; Contour < ContourCount; Contour++)
    {
        USHORT End = Glyph.U16(Offset + Contour * 2);

        if (Contour != 0 && End < Outline.ContourEnds.back() - FirstPoint)
            return FALSE;
        Outline.ContourEnds.push_back(static_cast<USHORT>(FirstPoint + End));
    }

    PointCount = Glyph.U16(Offset + (ContourCount - 1) * 2) + 1u;
    Offset += ContourCount * 2;
    InstructionLength = Glyph.U16(Offset);
    Offset += 2;
    if (!Glyph.Has(Offset, InstructionLength))
        return FALSE;

    Outline.Instructions = Glyph.Sub(Offset, InstructionLength);
    Offset += InstructionLength;

    Flags.reserve(PointCount);
    while (Flags.size() < PointCount)
    {
        BYTE Flag;

        if (!Glyph.Has(Offset, 1))
            return FALSE;
        Flag = Glyph.U8(Offset++);
        Flags.push_back(Flag);
        if (Flag & RxPointRepeat)
        {
            BYTE Repeat = Glyph.U8(Offset++);

            while (Repeat-- != 0 && Flags.size() < PointCount)
                Flags.push_back(Flag);
        }
    }

    Outline.Points.resize(FirstPoint + PointCount);

    Value = 0;
    for (UINT32 Index = 0; Index < PointCount; Index++)
    {
        BYTE Flag = Flags[Index];

        if (Flag & RxPointShortX)
        {
            BYTE Delta = Glyph.U8(Offset++);
            Value += (Flag & RxPointSameX) ? Delta : -static_cast<INT32>(Delta);
        }
        else if (!(Flag & RxPointSameX))
        {
            Value += Glyph.S16(Offset);
            Offset += 2;
        }
        Outline.Points[FirstPoint + Index].X = Value;
        Outline.Points[FirstPoint + Index].OnCurve = Flag & RxPointOnCurve;
    }

    Value = 0;
    for (UINT32 Index = 0; Index < PointCount; Index++)
    {
        BYTE Flag = Flags[Index];

        if (Flag & RxPointShortY)
        {
            BYTE Delta = Glyph.U8(Offset++);
            Value += (Flag & RxPointSameY) ? Delta : -static_cast<INT32>(Delta);
        }
        else if (!(Flag & RxPointSameY))
        {
            Value += Glyph.S16(Offset);
            Offset += 2;
        }
        Outline.Points[FirstPoint + Index].Y = Value;
    }

    return Offset <= Glyph.Length();
}

/* F2Dot14 to a double */
static
double
RxF2Dot14(
    _In_ SHORT Value)
{
    return Value / 16384.0;
}

HRESULT
RxFaceTables::LoadOutlineAt(
    _In_ USHORT Glyph,
    _In_ ULONG Depth,
    _Inout_ RxGlyphOutline& Outline) const
{
    RxSpan Record = GlyphData(Glyph);
    SHORT ContourCount;
    UINT32 Offset;
    USHORT Flags;

    if (Record.IsEmpty())
        return S_OK;
    if (!Record.Has(0, 10))
        return DWRITE_E_FILEFORMAT;

    ContourCount = Record.S16(0);
    if (Depth == 0)
    {
        Outline.XMin = Record.S16(2);
        Outline.YMin = Record.S16(4);
        Outline.XMax = Record.S16(6);
        Outline.YMax = Record.S16(8);
    }

    if (ContourCount >= 0)
    {
        RxSpan Instructions;

        if (ContourCount == 0)
            return S_OK;
        if (Depth != 0)
            Instructions = Outline.Instructions;
        if (!RxReadSimpleGlyph(Record, ContourCount, Outline))
            return DWRITE_E_FILEFORMAT;
        /* Only the top level glyph's program is kept */
        if (Depth != 0)
            Outline.Instructions = Instructions;
        return S_OK;
    }

    if (Depth >= RxMaxComponentDepth)
        return DWRITE_E_FILEFORMAT;

    if (Depth == 0)
        Outline.IsComposite = TRUE;

    Offset = 10;
    do
    {
        RxGlyphOutline Component;
        USHORT ComponentGlyph;
        INT32 Arg1;
        INT32 Arg2;
        double Xx = 1.0;
        double Xy = 0.0;
        double Yx = 0.0;
        double Yy = 1.0;
        double Dx;
        double Dy;
        HRESULT hr;

        if (!Record.Has(Offset, 4))
            return DWRITE_E_FILEFORMAT;
        Flags = Record.U16(Offset);
        ComponentGlyph = Record.U16(Offset + 2);
        Offset += 4;

        if (Flags & RxComponentWordArgs)
        {
            Arg1 = (Flags & RxComponentXyValues) ? Record.S16(Offset) : Record.U16(Offset);
            Arg2 = (Flags & RxComponentXyValues) ? Record.S16(Offset + 2) : Record.U16(Offset + 2);
            Offset += 4;
        }
        else
        {
            Arg1 = (Flags & RxComponentXyValues) ? static_cast<CHAR>(Record.U8(Offset)) : Record.U8(Offset);
            Arg2 = (Flags & RxComponentXyValues) ? static_cast<CHAR>(Record.U8(Offset + 1)) : Record.U8(Offset + 1);
            Offset += 2;
        }

        if (Flags & RxComponentScale)
        {
            Xx = Yy = RxF2Dot14(Record.S16(Offset));
            Offset += 2;
        }
        else if (Flags & RxComponentXyScale)
        {
            Xx = RxF2Dot14(Record.S16(Offset));
            Yy = RxF2Dot14(Record.S16(Offset + 2));
            Offset += 4;
        }
        else if (Flags & RxComponentTwoByTwo)
        {
            Xx = RxF2Dot14(Record.S16(Offset));
            Xy = RxF2Dot14(Record.S16(Offset + 2));
            Yx = RxF2Dot14(Record.S16(Offset + 4));
            Yy = RxF2Dot14(Record.S16(Offset + 6));
            Offset += 8;
        }

        Component.Clear();
        hr = LoadOutlineAt(ComponentGlyph, Depth + 1, Component);
        if (FAILED(hr))
            return hr;

        for (auto& Point : Component.Points)
        {
            double X = Point.X * Xx + Point.Y * Yx;
            double Y = Point.X * Xy + Point.Y * Yy;

            Point.X = static_cast<INT32>(X >= 0 ? X + 0.5 : X - 0.5);
            Point.Y = static_cast<INT32>(Y >= 0 ? Y + 0.5 : Y - 0.5);
        }

        if (Flags & RxComponentXyValues)
        {
            Dx = Arg1;
            Dy = Arg2;
            /* Microsoft fonts leave offsets unscaled unless asked */
            if (Flags & RxComponentScaledOffset)
            {
                double X = Dx * Xx + Dy * Yx;
                double Y = Dx * Xy + Dy * Yy;

                Dx = X;
                Dy = Y;
            }
        }
        else
        {
            /* Arg1 is a point already placed, Arg2 a point of this component */
            if (static_cast<SIZE_T>(Arg1) >= Outline.Points.size() || static_cast<SIZE_T>(Arg2) >= Component.Points.size())
                return DWRITE_E_FILEFORMAT;
            Dx = Outline.Points[Arg1].X - Component.Points[Arg2].X;
            Dy = Outline.Points[Arg1].Y - Component.Points[Arg2].Y;
        }

        SIZE_T Base = Outline.Points.size();
        INT32 OffsetX = static_cast<INT32>(Dx >= 0 ? Dx + 0.5 : Dx - 0.5);
        INT32 OffsetY = static_cast<INT32>(Dy >= 0 ? Dy + 0.5 : Dy - 0.5);

        for (const auto& Point : Component.Points)
            Outline.Points.push_back({ Point.X + OffsetX, Point.Y + OffsetY, Point.OnCurve });
        for (USHORT End : Component.ContourEnds)
            Outline.ContourEnds.push_back(static_cast<USHORT>(Base + End));
    } while (Flags & RxComponentMore);

    if (Depth == 0 && (Flags & RxComponentInstructions))
    {
        USHORT Length = Record.U16(Offset);

        if (Record.Has(Offset + 2, Length))
            Outline.Instructions = Record.Sub(Offset + 2, Length);
    }
    else if (Depth == 0)
    {
        Outline.Instructions = RxSpan();
    }

    return S_OK;
}

HRESULT
RxFaceTables::LoadOutline(
    _In_ USHORT Glyph,
    _Inout_ RxGlyphOutline& Outline) const
{
    Outline.Clear();
    return LoadOutlineAt(Glyph, 0, Outline);
}

/* name */

/* Mac Roman bytes above 0x7F */
static const WCHAR RxMacRomanHigh[128] =
{
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211, 0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7,
};

WCHAR
RxMacRomanChar(
    _In_ BYTE Byte)
{
    return Byte < 0x80 ? static_cast<WCHAR>(Byte) : RxMacRomanHigh[Byte - 0x80];
}

BOOLEAN
RxFaceTables::FindName(
    _In_ USHORT NameId,
    _In_ USHORT Platform,
    _In_ USHORT Language,
    _Inout_ std::vector<WCHAR>& Text) const
{
    USHORT Count = Name.U16(2);
    UINT32 Storage = Name.U16(4);

    for (USHORT Index = 0; Index < Count; Index++)
    {
        UINT32 Record = 6 + Index * 12;
        USHORT RecordPlatform = Name.U16(Record);
        USHORT Encoding = Name.U16(Record + 2);
        UINT32 Length;
        UINT32 Offset;

        if (Name.U16(Record + 6) != NameId || RecordPlatform != Platform)
            continue;
        if (Name.U16(Record + 4) != Language)
            continue;

        Length = Name.U16(Record + 8);
        Offset = Storage + Name.U16(Record + 10);
        if (!Name.Has(Offset, Length))
            continue;

        Text.clear();
        if (Platform == 3 || Platform == 0)
        {
            for (UINT32 Char = 0; Char + 1 < Length; Char += 2)
                Text.push_back(static_cast<WCHAR>(Name.U16(Offset + Char)));
        }
        else if (Platform == 1 && Encoding == 0)
        {
            for (UINT32 Char = 0; Char < Length; Char++)
            {
                BYTE Byte = Name.U8(Offset + Char);
                Text.push_back(Byte < 0x80 ? static_cast<WCHAR>(Byte) : RxMacRomanHigh[Byte - 0x80]);
            }
        }
        else
        {
            continue;
        }

        while (!Text.empty() && Text.back() == UNICODE_NULL)
            Text.pop_back();
        Text.push_back(UNICODE_NULL);
        return TRUE;
    }
    return FALSE;
}
