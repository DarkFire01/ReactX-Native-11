/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Names and style values of faces described by a STAT table
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "names.hpp"

/* Name IDs */
constexpr USHORT RxNameFamily = 1;
constexpr USHORT RxNamePostscript = 6;
constexpr USHORT RxNameTypographicFamily = 16;
constexpr USHORT RxNameVariationsPrefix = 25;

/* A design axis once STAT and fvar are merged */
struct RxStyleAxis
{
    DWRITE_FONT_AXIS_TAG Tag;
    INT32 Default;
    INT32 Minimum;
    INT32 Maximum;
    USHORT Ordering;
    UINT32 Source;
};

/* One axis a STAT value applies to, as an axis index and a range around a nominal value */
struct RxStyleCondition
{
    UINT32 Axis;
    INT32 Value;
    INT32 Minimum;
    INT32 Maximum;
};

struct RxStyleRecord
{
    BOOLEAN Elidable;
    BOOLEAN Hidden;
    INT32 SortKey;
    UINT64 Distance;
    std::vector<RxStyleCondition> Conditions;
    RxNameList Names;
};

struct RxStyleTable
{
    std::vector<RxStyleAxis> Axes;
    std::vector<RxStyleRecord> Records;
    RxNameList ElidedName;
};

/* Axis values a name part is tested against */
struct RxStyleFilter
{
    DWRITE_FONT_AXIS_TAG Tag;
    INT32 Minimum;
    INT32 Maximum;
};

/* Axes that make up a WWS face name */
static const RxStyleFilter RxWwsAxes[] =
{
    { DWRITE_FONT_AXIS_TAG_WEIGHT, RxFixedMinimum, RxFixedMaximum },
    { DWRITE_FONT_AXIS_TAG_WIDTH, RxFixedMinimum, RxFixedMaximum },
    { DWRITE_FONT_AXIS_TAG_SLANT, RxFixedMinimum, 0 },
    { DWRITE_FONT_AXIS_TAG_ITALIC, 0, 0 },
    { DWRITE_FONT_AXIS_TAG_ITALIC, RxFixedOne, RxFixedOne },
};

/* Values that a Win32 subfamily can express: regular, bold, upright and italic */
static const RxStyleFilter RxWin32Axes[] =
{
    { DWRITE_FONT_AXIS_TAG_WEIGHT, 375 * RxFixedOne, 425 * RxFixedOne },
    { DWRITE_FONT_AXIS_TAG_WEIGHT, 675 * RxFixedOne, 725 * RxFixedOne },
    { DWRITE_FONT_AXIS_TAG_ITALIC, 0, 0 },
    { DWRITE_FONT_AXIS_TAG_ITALIC, RxFixedOne, RxFixedOne },
};

/* Lower wdth bound of stretch 2 through 9 */
static const INT32 RxWidthSteps[] =
{
    0x00384000, 0x0044C000, 0x00514000, 0x005DC000, 0x006A4000, 0x0076C000, 0x00890000, 0x00AF0000
};

static
BOOLEAN
RxReadStyleRecords(
    _In_ const RxFaceTables* Face,
    _In_ const RxSpan& Stat,
    _Inout_ RxStyleTable& Table)
{
    UINT32 DesignAxes = Stat.U16(6);
    UINT32 Count = Stat.U16(12);
    UINT32 Offsets = Stat.U32(14);

    if (!Stat.Has(Offsets, 2 * Count))
        return FALSE;

    for (UINT32 Index = 0; Index < Count; Index++)
    {
        UINT32 Start = Offsets + Stat.U16(Offsets + 2 * Index);
        USHORT Format;
        RxStyleRecord Record;

        if (!Stat.Has(Start, 2))
            return FALSE;
        Format = Stat.U16(Start);
        if (Format < 1 || Format > 4)
            continue;
        if (!Stat.Has(Start, 8))
            return FALSE;

        Record.Elidable = (Stat.U16(Start + 4) & 2) != 0;
        Record.Hidden = FALSE;
        Record.Distance = 0;

        if (Format == 4)
        {
            UINT32 Values = Stat.U16(Start + 2);

            if (!Stat.Has(Start + 8, 6 * Values))
                return FALSE;
            for (UINT32 Value = 0; Value < Values; Value++)
            {
                UINT32 Entry = Start + 8 + 6 * Value;
                INT32 Nominal = Stat.S32(Entry + 2);

                Record.Conditions.push_back({ Stat.U16(Entry), Nominal, Nominal, Nominal });
            }
            if (Record.Conditions.empty())
                continue;
        }
        else if (Format == 2)
        {
            if (!Stat.Has(Start, 20))
                return FALSE;
            Record.Conditions.push_back({ Stat.U16(Start + 2), Stat.S32(Start + 8), Stat.S32(Start + 12),
                                          Stat.S32(Start + 16) });
        }
        else
        {
            INT32 Nominal;

            if (!Stat.Has(Start, 12))
                return FALSE;
            Nominal = Stat.S32(Start + 8);
            Record.Conditions.push_back({ Stat.U16(Start + 2), Nominal, Nominal, Nominal });
        }

        for (const auto& Condition : Record.Conditions)
        {
            if (Condition.Axis >= DesignAxes)
                return FALSE;
        }

        Record.SortKey = Record.Conditions.size() == 1 ? static_cast<INT32>(Record.Conditions[0].Axis) : RxFixedMinimum;
        RxReadNames(Face, Stat.U16(Start + 6), Record.Names);
        Table.Records.push_back(std::move(Record));
    }
    return TRUE;
}

/* Design axes with ranges widened by every value naming them, then the fvar axes over them */
static
BOOLEAN
RxReadStyleAxes(
    _In_ const RxFaceTables* Face,
    _In_ const RxSpan& Stat,
    _Inout_ RxStyleTable& Table)
{
    UINT32 Size = Stat.U16(4);
    UINT32 Count = Stat.U16(6);
    UINT32 Offset = Stat.U32(8);
    std::vector<RxVariationAxis> Variations;

    for (UINT32 Index = 0; Index < Count; Index++)
    {
        UINT32 Record = Offset + Index * Size;

        if (!Stat.Has(Record, 8))
            return FALSE;
        Table.Axes.push_back({ static_cast<DWRITE_FONT_AXIS_TAG>(RxDWriteTag(Stat.U32(Record))), RxFixedMaximum,
                               RxFixedMaximum, RxFixedMinimum, Stat.U16(Record + 6), Index });
    }

    for (const auto& Record : Table.Records)
    {
        for (const auto& Condition : Record.Conditions)
        {
            RxStyleAxis& Axis = Table.Axes[Condition.Axis];
            INT64 Magnitude = Condition.Value < 0 ? -static_cast<INT64>(Condition.Value) : Condition.Value;
            INT64 Closest = Axis.Default < 0 ? -static_cast<INT64>(Axis.Default) : Axis.Default;

            Axis.Minimum = std::min(Condition.Value, std::min(Condition.Minimum, Axis.Minimum));
            Axis.Maximum = std::max(Condition.Value, std::max(Condition.Maximum, Axis.Maximum));
            /* The value nearest zero becomes the default, as for a font without fvar */
            if (std::min<INT64>(Magnitude, RxFixedMaximum) < std::min<INT64>(Closest, RxFixedMaximum))
                Axis.Default = Condition.Value;
        }
    }

    for (auto& Axis : Table.Axes)
    {
        if (Axis.Default == RxFixedMaximum)
        {
            Axis.Default = 0;
            Axis.Minimum = std::min(Axis.Minimum, 0);
            Axis.Maximum = std::max(Axis.Maximum, 0);
        }
    }

    RxReadVariationAxes(Face->Sfnt(), Variations);
    for (const auto& Variation : Variations)
    {
        RxStyleAxis* Target = nullptr;

        for (auto& Axis : Table.Axes)
        {
            if (Axis.Tag == Variation.Tag)
            {
                Target = &Axis;
                break;
            }
        }
        if (Target == nullptr)
        {
            Table.Axes.push_back({});
            Target = &Table.Axes.back();
        }
        Target->Tag = Variation.Tag;
        Target->Default = Variation.Default;
        Target->Minimum = Variation.Minimum;
        Target->Maximum = Variation.Maximum;
    }
    return TRUE;
}

/* Puts axes in STAT ordering and renumbers the values to match */
static
VOID
RxOrderStyleAxes(
    _Inout_ RxStyleTable& Table)
{
    std::vector<UINT32> Position(Table.Axes.size(), 0);

    std::stable_sort(Table.Axes.begin(), Table.Axes.end(),
                     [](const RxStyleAxis& A, const RxStyleAxis& B) { return A.Ordering < B.Ordering; });

    /* Axes only fvar knows claim source slot zero, like the original ones do */
    for (UINT32 Index = 0; Index < Table.Axes.size(); Index++)
    {
        if (Table.Axes[Index].Source < Position.size())
            Position[Table.Axes[Index].Source] = Index;
    }

    for (auto& Record : Table.Records)
    {
        for (auto& Condition : Record.Conditions)
            Condition.Axis = Position[Condition.Axis];
        if (Record.Conditions.size() == 1)
            Record.SortKey = static_cast<INT32>(Record.Conditions[0].Axis);
    }

    std::stable_sort(Table.Records.begin(), Table.Records.end(), [](const RxStyleRecord& A, const RxStyleRecord& B)
    {
        if (A.SortKey != B.SortKey)
            return A.SortKey < B.SortKey;
        return A.Conditions[0].Minimum < B.Conditions[0].Minimum;
    });
}

static
BOOLEAN
RxReadStyleTable(
    _In_ const RxFaceTables* Face,
    _Out_ RxStyleTable& Table)
{
    RxSpan Stat = Face->Sfnt().Table(RxTag('S', 'T', 'A', 'T'));

    Table.Axes.clear();
    Table.Records.clear();
    Table.ElidedName.clear();
    if (Stat.Length() < 18)
        return FALSE;

    if (!RxReadStyleRecords(Face, Stat, Table) || !RxReadStyleAxes(Face, Stat, Table))
    {
        Table.Axes.clear();
        Table.Records.clear();
        return FALSE;
    }
    RxOrderStyleAxes(Table);

    if (Stat.U16(2) >= 1 && Stat.Has(18, 2))
        RxReadNames(Face, Stat.U16(18), Table.ElidedName);
    return TRUE;
}

/* The face coordinates clamped to every design axis, defaults filled in, then the rest */
static
VOID
RxCanonicalValues(
    _In_ const RxStyleTable& Table,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _Out_ std::vector<RxAxisValue>& Canonical)
{
    Canonical.clear();
    for (const auto& Axis : Table.Axes)
    {
        INT32 Value = Axis.Default;

        for (const auto& Coordinate : Coordinates)
        {
            if (Coordinate.Tag == Axis.Tag)
            {
                Value = Coordinate.Value;
                break;
            }
        }
        Canonical.push_back({ Axis.Tag, std::max(std::min(Value, Axis.Maximum), Axis.Minimum) });
    }

    for (const auto& Coordinate : Coordinates)
    {
        BOOLEAN Known = FALSE;

        for (const auto& Axis : Table.Axes)
            Known |= Axis.Tag == Coordinate.Tag;
        if (!Known)
            Canonical.push_back(Coordinate);
    }
}

static
const RxAxisValue*
RxFindValue(
    _In_ const std::vector<RxAxisValue>& Values,
    _In_ DWRITE_FONT_AXIS_TAG Tag)
{
    for (const auto& Value : Values)
    {
        if (Value.Tag == Tag)
            return &Value;
    }
    return nullptr;
}

/* The nearest value of each axis, each axis named once, in axis order */
static
VOID
RxMatchStyleRecords(
    _In_ const RxStyleTable& Table,
    _In_ const std::vector<RxAxisValue>& Canonical,
    _Out_ std::vector<RxStyleRecord>& Matched)
{
    std::vector<BOOLEAN> Named(Table.Axes.size(), FALSE);

    Matched = Table.Records;
    for (auto& Record : Matched)
    {
        Record.Distance = 0;
        for (const auto& Condition : Record.Conditions)
        {
            const RxAxisValue* Found = RxFindValue(Canonical, Table.Axes[Condition.Axis].Tag);
            INT32 Wanted = Found != nullptr ? Found->Value : 0;
            INT32 Nearest = std::min(Condition.Maximum, Wanted);
            INT32 Difference;

            if (Nearest < Condition.Minimum)
                Nearest = Condition.Minimum;
            Difference = RxFixedSubtract(Nearest, Wanted);

            /* Squared distance; the low bit marks a value below the request */
            Record.Distance += 2 * static_cast<UINT64>((static_cast<INT64>(Difference) * Difference) >> 16) +
                               (Difference < 0 ? 1 : 0);
        }
    }

    std::stable_sort(Matched.begin(), Matched.end(),
                     [](const RxStyleRecord& A, const RxStyleRecord& B) { return A.Distance < B.Distance; });

    for (auto& Record : Matched)
    {
        BOOLEAN Taken = FALSE;

        for (const auto& Condition : Record.Conditions)
            Taken |= Named[Condition.Axis];
        if (Taken)
        {
            Record.Hidden = TRUE;
            continue;
        }
        for (const auto& Condition : Record.Conditions)
            Named[Condition.Axis] = TRUE;
    }

    Matched.erase(std::remove_if(Matched.begin(), Matched.end(), [](const RxStyleRecord& Record) { return Record.Hidden; }),
                  Matched.end());
    std::stable_sort(Matched.begin(), Matched.end(), [](const RxStyleRecord& A, const RxStyleRecord& B)
    {
        return A.Conditions[0].Axis < B.Conditions[0].Axis;
    });
}

/* Hides the records whose first axis value passes the filter, or those whose value does not */
static
VOID
RxFilterRecords(
    _In_ const RxStyleTable& Table,
    _Inout_ std::vector<RxStyleRecord>& Matched,
    _In_reads_opt_(Count) const RxStyleFilter* Filter,
    _In_ SIZE_T Count,
    _In_ BOOLEAN HidePassing)
{
    for (auto& Record : Matched)
    {
        const RxStyleCondition& First = Record.Conditions[0];
        DWRITE_FONT_AXIS_TAG Tag = Table.Axes[First.Axis].Tag;
        BOOLEAN Passes = FALSE;

        for (SIZE_T Index = 0; Index < Count; Index++)
        {
            if (Filter[Index].Tag == Tag && First.Value >= Filter[Index].Minimum && First.Value <= Filter[Index].Maximum)
                Passes = TRUE;
        }
        Record.Hidden = Passes == HidePassing;
    }
}

/* Entry for a language, else en-us, else the first */
static
const std::wstring*
RxPickName(
    _In_ const RxNameList& Names,
    _In_ const std::wstring& Locale)
{
    const RxNameEntry* English = nullptr;

    if (Names.empty())
        return nullptr;
    for (const auto& Entry : Names)
    {
        if (Entry.Locale == Locale)
            return &Entry.Text;
        if (Entry.Locale == L"en-us")
            English = &Entry;
    }
    return English != nullptr ? &English->Text : &Names[0].Text;
}

/* Base name followed by the visible value names, in every language the values carry */
static
VOID
RxStyleNames(
    _In_ const RxStyleTable& Table,
    _In_ const std::vector<RxStyleRecord>& Matched,
    _In_ const RxNameList& Base,
    _Out_ RxNameList& Names)
{
    std::vector<std::wstring> Locales;

    Names.clear();
    for (const auto& Record : Matched)
    {
        if (Record.Hidden)
            continue;
        for (const auto& Entry : Record.Names)
        {
            if (std::find(Locales.begin(), Locales.end(), Entry.Locale) == Locales.end())
                Locales.push_back(Entry.Locale);
        }
    }
    if (Locales.empty())
        Locales.push_back(L"en-us");

    for (const auto& Locale : Locales)
    {
        const std::wstring* Part = RxPickName(Base, Locale);
        std::wstring Text = Part != nullptr ? *Part : std::wstring();

        for (const auto& Record : Matched)
        {
            if (Record.Hidden || Record.Elidable)
                continue;
            Part = RxPickName(Record.Names, Locale);
            if (Part == nullptr || Part->empty())
                continue;
            if (!Text.empty())
                Text += L" ";
            Text += *Part;
        }
        if (Text.empty())
        {
            Part = RxPickName(Table.ElidedName, Locale);
            if (Part != nullptr)
                Text = *Part;
        }
        Names.push_back({ Locale, Text });
    }

    std::sort(Names.begin(), Names.end(),
              [](const RxNameEntry& A, const RxNameEntry& B) { return A.Locale < B.Locale; });
}

/* First entry with characters PostScript names cannot hold dropped and spaces made hyphens */
static
VOID
RxPostscriptFromNames(
    _In_ const RxNameList& Source,
    _Out_ RxNameList& Names)
{
    std::wstring Text;

    Names.clear();
    if (Source.empty())
        return;

    for (WCHAR Char : Source[0].Text)
    {
        if (Char == L' ')
            Text.push_back(L'-');
        else if (Char > L' ' && Char < 0x7F && wcschr(L"[](){}<>/%", Char) == nullptr)
            Text.push_back(Char);
    }
    Names.push_back({ Source[0].Locale, Text });
}

USHORT
RxAxisWeight(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values)
{
    const RxAxisValue* Weight = RxFindValue(Values, DWRITE_FONT_AXIS_TAG_WEIGHT);
    INT32 Rounded;

    if (Weight == nullptr)
        return RxFaceWeight(Header);
    Rounded = static_cast<INT32>((static_cast<INT64>(Weight->Value) + RxFixedOne / 2) >> 16);
    return static_cast<USHORT>(std::min(std::max(Rounded, 1), 999));
}

BYTE
RxAxisStretch(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values)
{
    const RxAxisValue* Width = RxFindValue(Values, DWRITE_FONT_AXIS_TAG_WIDTH);
    BYTE Stretch = DWRITE_FONT_STRETCH_ULTRA_CONDENSED;

    if (Width == nullptr)
        return RxFaceStretch(Header);
    for (INT32 Step : RxWidthSteps)
    {
        if (Width->Value < Step)
            break;
        Stretch += 1;
    }
    return Stretch;
}

BYTE
RxAxisStyle(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values)
{
    const RxAxisValue* Italic = RxFindValue(Values, DWRITE_FONT_AXIS_TAG_ITALIC);
    const RxAxisValue* Slant;

    if (Italic != nullptr)
        return static_cast<BYTE>(Italic->Value >= RxFixedOne / 2 ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL);
    Slant = RxFindValue(Values, DWRITE_FONT_AXIS_TAG_SLANT);
    if (Slant != nullptr)
        return static_cast<BYTE>(Slant->Value <= -10 * RxFixedOne ? DWRITE_FONT_STYLE_OBLIQUE : DWRITE_FONT_STYLE_NORMAL);
    return RxFaceStyle(Header);
}

VOID
RxDescribeStyledFace(
    _In_ const RxFaceTables* Face,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _Out_ RxFaceDescription* Description)
{
    RxStyleTable Table;
    std::vector<RxAxisValue> Canonical;
    std::vector<RxStyleRecord> Matched;
    RxNameList Empty;
    RxNameList& Base = Description->TypographicFamily;

    RxReadNames(Face, RxNameTypographicFamily, Base);
    if (Base.empty())
        RxReadNames(Face, RxNameFamily, Base);

    RxReadStyleTable(Face, Table);
    RxCanonicalValues(Table, Coordinates, Canonical);
    RxMatchStyleRecords(Table, Canonical, Matched);

    Description->Weight = RxAxisWeight(Face->Header(), Canonical);
    Description->Win32Weight = Description->Weight;
    Description->Stretch = RxAxisStretch(Face->Header(), Canonical);
    Description->Style = RxAxisStyle(Face->Header(), Canonical);
    Description->Win32Style = Description->Style;
    Description->FromStyleTable = TRUE;

    RxFilterRecords(Table, Matched, RxWin32Axes, _countof(RxWin32Axes), TRUE);
    RxStyleNames(Table, Matched, Base, Description->Win32Family);
    RxFilterRecords(Table, Matched, RxWin32Axes, _countof(RxWin32Axes), FALSE);
    RxStyleNames(Table, Matched, Empty, Description->Win32Face);

    RxFilterRecords(Table, Matched, nullptr, 0, TRUE);
    RxStyleNames(Table, Matched, Empty, Description->TypographicFace);
    RxStyleNames(Table, Matched, Base, Description->FullName);

    RxFilterRecords(Table, Matched, RxWwsAxes, _countof(RxWwsAxes), TRUE);
    RxStyleNames(Table, Matched, Base, Description->WwsFamily);
    RxFilterRecords(Table, Matched, RxWwsAxes, _countof(RxWwsAxes), FALSE);
    RxStyleNames(Table, Matched, Empty, Description->WwsFace);

    if (!Coordinates.empty())
    {
        RxNameList Prefix;
        RxNameList Full;

        RxReadNames(Face, RxNameVariationsPrefix, Prefix);
        RxFilterRecords(Table, Matched, nullptr, 0, TRUE);
        RxStyleNames(Table, Matched, Prefix.empty() ? Base : Prefix, Full);
        RxPostscriptFromNames(Full, Description->PostscriptName);
    }
    else
    {
        RxReadNames(Face, RxNamePostscript, Description->PostscriptName);
    }
}
