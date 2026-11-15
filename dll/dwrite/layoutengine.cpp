/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Text layout passes, drawing and hit testing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"
#include "textlayout.hpp"

#include <cmath>

/* Characters that take no space and draw nothing */
static
BOOLEAN
RxIsControl(
    _In_ UINT32 Cp)
{
    return (Cp < 0x20 && Cp != 0x09) || Cp == 0x7F || Cp == 0x85 || Cp == 0x2028 || Cp == 0x2029 ||
           (Cp >= 0x200B && Cp <= 0x200F) || (Cp >= 0x202A && Cp <= 0x202E) || (Cp >= 0x2060 && Cp <= 0x206F) ||
           Cp == 0xFEFF;
}

static
BOOLEAN
RxIsNewlineClass(
    _In_ RxBreakClass Class)
{
    return Class == RxLbLF || Class == RxLbBK || Class == RxLbCR || Class == RxLbNL;
}

/* Families tried, in order, when a run's own font lacks a character */
static
const PCWSTR*
RxFallbackFamilies(
    _In_ UINT32 Cp)
{
    static const PCWSTR Latin[] = { L"Segoe UI", L"Segoe UI Symbol", L"Ebrima", L"Calibri", L"Tahoma", L"Arial",
                                    L"Microsoft Sans Serif", L"Lucida Sans Unicode", nullptr };
    static const PCWSTR Symbols[] = { L"Segoe UI Symbol", L"Segoe UI", L"Cambria Math", L"Segoe UI Emoji",
                                      L"Yu Gothic UI", L"Meiryo UI", L"Microsoft JhengHei UI", L"Microsoft YaHei UI",
                                      L"Malgun Gothic", L"Tahoma", L"Arial", L"Lucida Sans Unicode", nullptr };
    static const PCWSTR Cjk[] = { L"Yu Gothic UI", L"Microsoft YaHei UI", L"Microsoft JhengHei UI", L"Malgun Gothic",
                                  L"SimSun", L"MS Gothic", L"MingLiU", L"Segoe UI Symbol", nullptr };
    static const PCWSTR Hangul[] = { L"Malgun Gothic", L"Gulim", L"Segoe UI Symbol", nullptr };
    static const PCWSTR Japanese[] = { L"Yu Gothic UI", L"Meiryo UI", L"MS Gothic", L"Microsoft YaHei UI", nullptr };
    static const PCWSTR Emoji[] = { L"Segoe UI Emoji", L"Segoe UI Symbol", L"Segoe UI", nullptr };
    static const PCWSTR Indic[] = { L"Nirmala UI", L"Mangal", L"Segoe UI", nullptr };
    static const PCWSTR Thai[] = { L"Leelawadee UI", L"Tahoma", L"Segoe UI", nullptr };
    static const PCWSTR African[] = { L"Ebrima", L"Nyala", L"Segoe UI", nullptr };
    static const PCWSTR Private[] = { L"Segoe MDL2 Assets", L"Segoe Fluent Icons", L"Segoe UI Symbol", nullptr };

    if (Cp >= 0xE000 && Cp <= 0xF8FF)
        return Private;
    if ((Cp >= 0x1F000 && Cp <= 0x1FAFF) || (Cp >= 0x2600 && Cp <= 0x27BF))
        return Emoji;
    if (Cp >= 0x3040 && Cp <= 0x30FF)
        return Japanese;
    if ((Cp >= 0x1100 && Cp <= 0x11FF) || (Cp >= 0x3130 && Cp <= 0x318F) || (Cp >= 0xAC00 && Cp <= 0xD7AF))
        return Hangul;
    if ((Cp >= 0x2E80 && Cp <= 0x2FFF) || (Cp >= 0x3000 && Cp <= 0x303F) || (Cp >= 0x3100 && Cp <= 0x9FFF) ||
        (Cp >= 0xF900 && Cp <= 0xFAFF) || (Cp >= 0xFF00 && Cp <= 0xFFEF) || Cp >= 0x20000)
        return Cjk;
    if (Cp >= 0x0900 && Cp <= 0x0DFF)
        return Indic;
    if (Cp >= 0x0E00 && Cp <= 0x0EFF)
        return Thai;
    if ((Cp >= 0x1200 && Cp <= 0x139F) || (Cp >= 0x07C0 && Cp <= 0x07FF) || (Cp >= 0x2D30 && Cp <= 0x2D7F))
        return African;
    if (Cp >= 0x2000 && Cp <= 0x2BFF)
        return Symbols;
    return Latin;
}

/* Fonts */

UINT32
RxTextLayout::AddFace(
    _In_ IDWriteFontFace* Face)
{
    for (UINT32 Index = 0; Index < Faces.size(); Index++)
    {
        if (Faces[Index].Get() == Face)
            return Index;
    }
    Faces.push_back(RxComPtr<IDWriteFontFace>(Face));
    return static_cast<UINT32>(Faces.size() - 1);
}

/* The face a collection gives for a family; an unknown family takes Segoe UI or the first family */
static
HRESULT
RxMatchFace(
    _In_ IDWriteFontCollection* Collection,
    _In_z_ PCWSTR Family,
    _In_ BOOLEAN AllowSubstitute,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _COM_Outptr_ IDWriteFontFace** Face)
{
    RxComPtr<IDWriteFontFamily> FontFamily;
    RxComPtr<IDWriteFont> Font;
    UINT32 Index = 0;
    BOOL Exists = FALSE;
    HRESULT hr;

    *Face = nullptr;
    Collection->FindFamilyName(Family, &Index, &Exists);
    if (!Exists)
    {
        if (!AllowSubstitute)
            return DWRITE_E_NOFONT;
        Collection->FindFamilyName(L"Segoe UI", &Index, &Exists);
        if (!Exists)
            Index = 0;
    }
    if (Index >= Collection->GetFontFamilyCount())
        return DWRITE_E_NOFONT;

    hr = Collection->GetFontFamily(Index, FontFamily.Put());
    if (SUCCEEDED(hr))
        hr = FontFamily->GetFirstMatchingFont(Weight, Stretch, Style, Font.Put());
    if (SUCCEEDED(hr))
        hr = Font->CreateFontFace(Face);
    return hr;
}

HRESULT
RxTextLayout::ResolveFaces(VOID)
{
    struct Resolved
    {
        IDWriteFontCollection* Collection;
        std::wstring Family;
        DWRITE_FONT_WEIGHT Weight;
        DWRITE_FONT_STYLE Style;
        DWRITE_FONT_STRETCH Stretch;
        UINT32 Face;
    };
    std::vector<Resolved> Seen;

    Faces.clear();
    RunFaces.clear();

    for (const auto& Run : Runs)
    {
        UINT32 Slot = MAXUINT32;

        for (const auto& Entry : Seen)
        {
            if (Entry.Collection == Run.Collection.Get() && Entry.Family == Run.Family && Entry.Weight == Run.Weight &&
                Entry.Style == Run.Style && Entry.Stretch == Run.Stretch)
            {
                Slot = Entry.Face;
                break;
            }
        }

        if (Slot == MAXUINT32)
        {
            RxComPtr<IDWriteFontFace> Face;
            HRESULT hr = RxMatchFace(Run.Collection.Get(), Run.Family.c_str(), TRUE, Run.Weight, Run.Style,
                                     Run.Stretch, Face.Put());

            if (FAILED(hr))
                return hr;
            Slot = AddFace(Face.Get());
            Seen.push_back({ Run.Collection.Get(), Run.Family, Run.Weight, Run.Style, Run.Stretch, Slot });
        }
        RunFaces.push_back(Slot);
    }
    return S_OK;
}

UINT32
RxTextLayout::FaceFor(
    _In_ const RxFormatRun& Run,
    _In_ UINT32 CodePoint)
{
    UINT32 RunIndex = static_cast<UINT32>(&Run - Runs.data());
    UINT32 Own = RunFaces[RunIndex];
    RxComPtr<IDWriteFontCollection> System;
    UINT16 Glyph = 0;

    Faces[Own]->GetGlyphIndices(&CodePoint, 1, &Glyph);
    if (Glyph != 0 || RxIsControl(CodePoint) || CodePoint == 0x09)
        return Own;

    /* Faces already in use first, then the fallback families */
    for (UINT32 Index = 0; Index < Faces.size(); Index++)
    {
        if (Index == Own)
            continue;
        Faces[Index]->GetGlyphIndices(&CodePoint, 1, &Glyph);
        if (Glyph != 0)
        {
            RxComPtr<IDWriteFontFace3> Face3;

            /* Only reuse a face that matches the run's style closely enough */
            if (SUCCEEDED(Faces[Index]->QueryInterface(__uuidof(IDWriteFontFace3), Face3.PutVoid())) &&
                Face3->GetWeight() == Run.Weight && Face3->GetStyle() == Run.Style)
                return Index;
        }
    }

    {
        RxFontCollection* Collection;

        if (FAILED(Factory->SystemCollection(&Collection)))
            return Own;
        System.Attach(Collection);
    }

    for (const PCWSTR* Family = RxFallbackFamilies(CodePoint); *Family != nullptr; Family++)
    {
        RxComPtr<IDWriteFontFace> Face;

        if (FAILED(RxMatchFace(System.Get(), *Family, FALSE, Run.Weight, Run.Style, Run.Stretch, Face.Put())))
            continue;
        Face->GetGlyphIndices(&CodePoint, 1, &Glyph);
        if (Glyph != 0)
            return AddFace(Face.Get());
    }
    return Own;
}

/* Glyphs */

static
UINT32
RxCodePointAt(
    _In_ const std::wstring& Text,
    _In_ UINT32 Index,
    _Out_ UINT32* Units)
{
    UINT32 Cp = Text[Index];

    *Units = 1;
    if (Cp >= 0xD800 && Cp <= 0xDBFF && Index + 1 < Text.size() && Text[Index + 1] >= 0xDC00 && Text[Index + 1] <= 0xDFFF)
    {
        *Units = 2;
        return 0x10000 + ((Cp - 0xD800) << 10) + (Text[Index + 1] - 0xDC00);
    }
    return Cp;
}

/* Natural or GDI compatible advance in DIPs for a design advance */
static
FLOAT
RxAdvance(
    _In_ INT32 Design,
    _In_ FLOAT Size,
    _In_ UINT32 UnitsPerEm,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_ FLOAT PixelsPerDip)
{
    double Value = static_cast<double>(Design) * Size / UnitsPerEm;

    if (Measuring != DWRITE_MEASURING_MODE_NATURAL)
    {
        double Pixels = Value * PixelsPerDip;

        if (std::fabs(Pixels) < 8388600.0)
            Pixels = std::floor(Pixels + 0.5);
        Value = Pixels / PixelsPerDip;
    }
    return static_cast<FLOAT>(Value);
}

VOID
RxTextLayout::BuildGlyphs(VOID)
{
    UINT32 Length = static_cast<UINT32>(Text.size());
    UINT32 Index = 0;
    UINT32 RunIndex = 0;

    CharFace.assign(Length, 0);
    CharRun.assign(Length, 0);
    ClusterStart.assign(Length, 0);
    ClusterMap.assign(Length, 0);
    Glyphs.clear();
    Advances.clear();
    Offsets.clear();

    while (Index < Length)
    {
        const RxFormatRun* Run;
        UINT32 Cp;
        UINT32 Units;
        UINT32 Face;
        UINT32 Glyph = static_cast<UINT32>(Glyphs.size());

        while (RunIndex + 1 < Runs.size() && Index >= Runs[RunIndex + 1].Start)
            RunIndex++;
        Run = &Runs[RunIndex];

        /* An inline object takes its whole run as one cluster */
        if (Run->Inline.Get())
        {
            DWRITE_INLINE_OBJECT_METRICS Object = {};
            UINT32 End = static_cast<UINT32>(std::min<UINT64>(Length, static_cast<UINT64>(Run->Start) + Run->Length));

            Run->Inline->GetMetrics(&Object);
            Glyphs.push_back(0);
            Advances.push_back(Object.width);
            Offsets.push_back({ 0.0f, 0.0f });
            ClusterStart[Index] = 1;
            for (UINT32 Char = Index; Char < End; Char++)
            {
                ClusterMap[Char] = Glyph;
                CharRun[Char] = RunIndex;
                CharFace[Char] = RunFaces[RunIndex];
            }
            Index = End;
            continue;
        }

        Cp = RxCodePointAt(Text, Index, &Units);
        Face = FaceFor(*Run, Cp);

        {
            IDWriteFontFace* FontFace = Faces[Face].Get();
            DWRITE_FONT_METRICS FontMetrics;
            DWRITE_GLYPH_METRICS GlyphMetrics = {};
            UINT32 Lookup = Cp == 0x09 ? 0x20 : Cp;
            UINT16 Id = 0;
            FLOAT Advance = 0.0f;

            FontFace->GetMetrics(&FontMetrics);
            FontFace->GetGlyphIndices(&Lookup, 1, &Id);
            if (!RxIsControl(Cp))
            {
                FontFace->GetDesignGlyphMetrics(&Id, 1, &GlyphMetrics, FALSE);
                Advance = RxAdvance(static_cast<INT32>(GlyphMetrics.advanceWidth), Run->Size, FontMetrics.designUnitsPerEm,
                                    Measuring, PixelsPerDip);
                if (Run->Leading != 0.0f || Run->Trailing != 0.0f)
                {
                    FLOAT Spaced = Advance + Run->Leading + Run->Trailing;

                    if (Run->Leading < 0.0f || Run->Trailing < 0.0f)
                        Spaced = std::max(Spaced, Run->MinimumAdvance);
                    Advance = Spaced;
                }
            }

            Glyphs.push_back(Id);
            Advances.push_back(Advance);
            Offsets.push_back({ Run->Leading, 0.0f });
        }

        ClusterStart[Index] = 1;
        for (UINT32 Unit = 0; Unit < Units; Unit++)
        {
            ClusterMap[Index + Unit] = Glyph;
            CharFace[Index + Unit] = Face;
            CharRun[Index + Unit] = RunIndex;
        }
        Index += Units;
    }

    ApplyKerning();
}

/* Pair kerning over each stretch of glyphs that share a face and size */
VOID
RxTextLayout::ApplyKerning(VOID)
{
    UINT32 Length = static_cast<UINT32>(Text.size());
    UINT32 Start = 0;

    while (Start < Length)
    {
        UINT32 End = Start;
        UINT32 Face = CharFace[Start];
        FLOAT Size = Runs[CharRun[Start]].Size;
        BOOLEAN Plain = !Runs[CharRun[Start]].Inline.Get() && !RxIsControl(Text[Start]) && Text[Start] != L'\t';

        while (End < Length && CharFace[End] == Face && Runs[CharRun[End]].Size == Size &&
               (!Runs[CharRun[End]].Inline.Get() && !RxIsControl(Text[End]) && Text[End] != L'\t') == Plain)
            End++;

        if (Plain)
        {
            RxFontFace* Ours = RxFontFace::FromInterface(Faces[Face].Get());
            UINT32 First = GlyphFor(Start);
            UINT32 Last = GlyphFor(End);

            if (Ours != nullptr && Last - First > 1 && !Ours->Kerning().IsEmpty())
            {
                std::vector<INT32> Adjust(Last - First, 0);
                DWRITE_FONT_METRICS Font;

                Ours->Kerning().Apply(&Glyphs[First], Last - First, Adjust.data());
                Ours->GetMetrics(&Font);
                for (UINT32 Glyph = First; Glyph < Last; Glyph++)
                {
                    if (Adjust[Glyph - First] != 0)
                        Advances[Glyph] += RxAdvance(Adjust[Glyph - First], Size, Font.designUnitsPerEm, Measuring,
                                                     PixelsPerDip);
                }
            }
        }
        Start = End;
    }
}

UINT32
RxTextLayout::ClusterEnd(
    _In_ UINT32 Position) const
{
    UINT32 End = Position + 1;

    while (End < Text.size() && !ClusterStart[End])
        End++;
    return End;
}

UINT32
RxTextLayout::GlyphFor(
    _In_ UINT32 Position) const
{
    if (Position >= Text.size())
        return static_cast<UINT32>(Glyphs.size());
    return ClusterMap[Position];
}

FLOAT
RxTextLayout::RangeWidth(
    _In_ UINT32 Start,
    _In_ UINT32 End) const
{
    FLOAT Width = 0.0f;
    UINT32 First = GlyphFor(Start);
    UINT32 Last = GlyphFor(End);

    for (UINT32 Glyph = First; Glyph < Last; Glyph++)
        Width += Advances[Glyph];
    return Width;
}

/* Line breaking */

VOID
RxTextLayout::BreakLines(VOID)
{
    UINT32 Length = static_cast<UINT32>(Text.size());
    BOOLEAN Wrap = Props.WordWrapping != DWRITE_WORD_WRAPPING_NO_WRAP;
    BOOLEAN Character = Props.WordWrapping == DWRITE_WORD_WRAPPING_CHARACTER;
    BOOLEAN Emergency = Props.WordWrapping == DWRITE_WORD_WRAPPING_EMERGENCY_BREAK || Character ||
                        (Props.WordWrapping == DWRITE_WORD_WRAPPING_WRAP &&
                         Props.Trimming.granularity == DWRITE_TRIMMING_GRANULARITY_NONE);
    BOOLEAN PixelMode = Measuring != DWRITE_MEASURING_MODE_NATURAL;
    FLOAT Extent = (Props.ReadingDirection & 2) ? MaxHeight : MaxWidth;
    FLOAT TabStop = Props.IncrementalTabStop;
    UINT32 LineStart = 0;

    auto Fits = [&](FLOAT Width)
    {
        if (!PixelMode)
            return Width <= Extent;
        return std::floor(Width * PixelsPerDip + 0.5f) <= std::floor(Extent * PixelsPerDip + 0.5f);
    };

    Lines.clear();

    while (LineStart < Length)
    {
        FLOAT Position = 0.0f;
        FLOAT Ink = 0.0f;
        UINT32 Break = LineStart;
        FLOAT BreakPosition = 0.0f;
        FLOAT BreakInk = 0.0f;
        BOOLEAN SeenBreak = FALSE;
        BOOLEAN Ended = FALSE;
        UINT32 Cluster = LineStart;
        RxTextLine Line = {};

        while (Cluster < Length && !Ended)
        {
            UINT32 End = ClusterEnd(Cluster);
            FLOAT Width = RangeWidth(Cluster, End);
            BOOLEAN Whitespace = (Breakpoints[Cluster] & RxBreakIsWhitespace) != 0;

            if (Whitespace)
            {
                if (Text[Cluster] == L'\t')
                {
                    if (TabStop > 0.0f)
                        Width = (std::floor(Position / TabStop) + 1.0f) * TabStop - Position;
                    Advances[ClusterMap[Cluster]] = Width;
                    Position += Width;
                    Ink = Position;
                }
                else
                {
                    Position += Width;
                }
            }
            else
            {
                FLOAT Candidate = Position + Width;

                if (Wrap && !Fits(Candidate))
                {
                    if (!SeenBreak && (!Emergency || Cluster == LineStart))
                    {
                        /* A line always holds its first cluster; whole words run on to their next break */
                    }
                    else
                    {
                        if (!SeenBreak)
                        {
                            Break = Cluster;
                            BreakPosition = Position;
                            BreakInk = Ink;
                        }
                        Ended = TRUE;
                        break;
                    }
                }
                Position = Candidate;
                Ink = Candidate;
            }

            if (Character || RxCanBreakAfter(Breakpoints[End - 1]))
            {
                Break = End;
                BreakPosition = Position;
                BreakInk = Ink;
                SeenBreak = TRUE;
                if (RxBreakAfter(Breakpoints[End - 1]) == DWRITE_BREAK_CONDITION_MUST_BREAK)
                    Ended = TRUE;
            }
            Cluster = End;
        }

        if (!Ended && Cluster >= Length)
        {
            Break = Length;
            BreakPosition = Position;
            BreakInk = Ink;
        }
        if (Break <= LineStart)
        {
            Break = ClusterEnd(LineStart);
            BreakPosition = RangeWidth(LineStart, Break);
            BreakInk = BreakPosition;
        }

        Line.Start = LineStart;
        Line.Length = Break - LineStart;

        /* Newline characters at the break, then the whitespace that hangs before them */
        {
            UINT32 Walk = Break;
            UINT32 ContentEnd;

            while (Walk > LineStart && RxIsNewlineClass(Classes[Walk - 1]))
                Walk--;
            Line.Newline = Break - Walk;
            ContentEnd = Walk;
            while (Walk > LineStart && (Breakpoints[Walk - 1] & RxBreakIsWhitespace) && Text[Walk - 1] != L'\t')
                Walk--;
            Line.TrailingWhitespace = Break - Walk;
            UNREFERENCED_PARAMETER(ContentEnd);
        }

        Line.Width = BreakInk;
        Line.WidthWithWhitespace = BreakPosition;
        Lines.push_back(Line);
        LineStart = Break;
    }

    /* Empty text, or text ending in a newline, has a last empty line */
    if (Length == 0 || RxBreakAfter(Breakpoints[Length - 1]) == DWRITE_BREAK_CONDITION_MUST_BREAK)
    {
        RxTextLine Line = {};

        Line.Start = Length;
        Lines.push_back(Line);
    }
}

/* Spans and heights */

VOID
RxTextLayout::MeasureSpan(
    _Inout_ RxTextSpan& Span)
{
    FLOAT Above;
    FLOAT Below;

    if (Span.Inline != nullptr)
    {
        DWRITE_INLINE_OBJECT_METRICS Object = {};

        if (FAILED(Span.Inline->GetMetrics(&Object)))
            Object = {};
        Above = Object.baseline;
        Below = Object.height - Object.baseline;
        if (!Span.IsSign)
            Span.Width = Object.width;
    }
    else
    {
        DWRITE_FONT_METRICS Font;
        FLOAT Gap;
        FLOAT Scale;

        Faces[Span.Face]->GetMetrics(&Font);
        Scale = Span.Size / Font.designUnitsPerEm;

        if (Props.LineSpacing.fontLineGapUsage == DWRITE_FONT_LINE_GAP_USAGE_DISABLED)
            Gap = 0.0f;
        else if (Props.LineSpacing.fontLineGapUsage == DWRITE_FONT_LINE_GAP_USAGE_ENABLED)
            Gap = Font.lineGap;
        else
            Gap = Measuring == DWRITE_MEASURING_MODE_NATURAL ? Font.lineGap : 0.0f;

        Above = (Font.ascent + Gap) * Scale;
        Below = Font.descent * Scale;
        if (Measuring != DWRITE_MEASURING_MODE_NATURAL)
        {
            Above = std::floor(Above * PixelsPerDip + 0.5f) / PixelsPerDip;
            Below = std::floor(Below * PixelsPerDip + 0.5f) / PixelsPerDip;
        }
    }

    Span.Above = Above;
    Span.Below = Below;
    Span.Ascent = Above;
    if (Props.LineSpacing.method == DWRITE_LINE_SPACING_METHOD_UNIFORM)
    {
        Span.LineAscent = Props.LineSpacing.baseline;
        Span.LineDescent = Props.LineSpacing.height - Props.LineSpacing.baseline;
    }
    else
    {
        Span.LineAscent = std::max(Above, 0.0f);
        Span.LineDescent = std::max(Below, 0.0f);
    }
}

VOID
RxTextLayout::BuildSpans(VOID)
{
    Spans.clear();

    for (UINT32 LineIndex = 0; LineIndex < Lines.size(); LineIndex++)
    {
        RxTextLine& Line = Lines[LineIndex];
        UINT32 End = Line.Start + Line.Length;
        UINT32 TrailingStart = End - Line.TrailingWhitespace;
        UINT32 Position = Line.Start;

        Line.FirstSpan = static_cast<UINT32>(Spans.size());

        if (Line.Length == 0)
        {
            RxTextSpan Span = {};
            UINT32 Source = Line.Start > 0 ? Line.Start - 1 : 0;

            Span.Start = Line.Start;
            Span.Line = LineIndex;
            Span.Run = Text.empty() ? 0 : CharRun[Source];
            Span.Face = Text.empty() ? RunFaces[0] : CharFace[Source];
            Span.Size = Runs[Span.Run].Size;
            MeasureSpan(Span);
            Spans.push_back(Span);
        }

        while (Position < End)
        {
            RxTextSpan Span = {};
            UINT32 Next = ClusterEnd(Position);
            BOOLEAN IsTab = Text[Position] == L'\t';
            BOOLEAN Collapsed = RxIsControl(Text[Position]) || IsTab;
            BOOLEAN IsInline = Runs[CharRun[Position]].Inline.Get() != nullptr;

            /* Grow while nothing that splits spans changes; each tab stands alone */
            while (!IsTab && Next < End && Next != TrailingStart && CharRun[Next] == CharRun[Position] &&
                   CharFace[Next] == CharFace[Position] && RxIsControl(Text[Next]) == Collapsed &&
                   Text[Next] != L'\t' && !IsInline)
                Next = ClusterEnd(Next);

            Span.Start = Position;
            Span.Length = Next - Position;
            Span.Line = LineIndex;
            Span.Run = CharRun[Position];
            Span.Face = CharFace[Position];
            Span.Size = Runs[Span.Run].Size;
            Span.Collapsed = Collapsed;
            Span.Inline = IsInline ? Runs[Span.Run].Inline.Get() : nullptr;
            Span.Width = RangeWidth(Position, Next);
            MeasureSpan(Span);
            Spans.push_back(Span);
            Position = Next;
        }

        Line.SpanCount = static_cast<UINT32>(Spans.size()) - Line.FirstSpan;
    }
}

VOID
RxTextLayout::LineHeights(VOID)
{
    for (auto& Line : Lines)
    {
        FLOAT Ascent = -FLT_MAX;
        FLOAT Descent = -FLT_MAX;

        for (UINT32 Index = Line.FirstSpan; Index < Line.FirstSpan + Line.SpanCount; Index++)
        {
            Ascent = std::max(Ascent, Spans[Index].LineAscent);
            Descent = std::max(Descent, Spans[Index].LineDescent);
        }
        if (Ascent == -FLT_MAX)
            Ascent = 0.0f;
        if (Descent == -FLT_MAX)
            Descent = 0.0f;

        if (Props.LineSpacing.method == DWRITE_LINE_SPACING_METHOD_PROPORTIONAL)
        {
            FLOAT Base = Props.LineSpacing.baseline * Ascent;
            FLOAT Height = Props.LineSpacing.height * (Ascent + Descent);
            FLOAT Extra = Height - Base - Descent;

            Line.Height = Height;
            Line.Baseline = Base + Props.LineSpacing.leadingBefore * Extra;
        }
        else
        {
            Line.Height = Ascent + Descent;
            Line.Baseline = Ascent;
        }
    }
}

/* Trimming */

VOID
RxTextLayout::TrimLine(
    _In_ UINT32 LineIndex,
    _In_ FLOAT SignWidth)
{
    RxTextLine& Line = Lines[LineIndex];
    UINT32 Start = Line.Start;
    UINT32 End = Line.Start + Line.Length - Line.Newline;
    FLOAT Target = std::max(0.0f, MaxWidth - SignWidth);
    FLOAT Width = Line.WidthWithWhitespace;
    BOOLEAN WordMode = Props.Trimming.granularity == DWRITE_TRIMMING_GRANULARITY_WORD;
    BOOLEAN SeenBoundary = FALSE;
    UINT32 Keep = End;
    UINT32 Cut;
    UINT32 Resume;

    auto PreviousCluster = [&](UINT32 Position)
    {
        UINT32 Back = Position - 1;

        while (Back > Start && !ClusterStart[Back])
            Back--;
        return Back;
    };

    /* The text from the delimiter on is kept */
    if (Props.Trimming.delimiter != 0 && Props.Trimming.delimiterCount != 0)
    {
        UINT32 Found = 0;

        for (UINT32 Index = End; Index > Start; Index--)
        {
            if (Text[Index - 1] == static_cast<WCHAR>(Props.Trimming.delimiter))
            {
                Keep = Index - 1;
                if (++Found == Props.Trimming.delimiterCount)
                    break;
            }
        }
    }

    Cut = Keep;
    while (Cut > Start)
    {
        UINT32 Previous;

        if (Width > Target)
        {
            Previous = PreviousCluster(Cut);
            Width -= RangeWidth(Previous, Cut);
            Cut = Previous;
            continue;
        }

        if (WordMode && !SeenBoundary)
        {
            BOOLEAN Boundary = RxCanBreakAfter(Breakpoints[Cut - 1]) ||
                               (Cut < End && (Breakpoints[Cut] & RxBreakIsWhitespace));

            if (!Boundary)
            {
                Previous = PreviousCluster(Cut);
                Width -= RangeWidth(Previous, Cut);
                Cut = Previous;
                continue;
            }
            SeenBoundary = TRUE;
        }

        if (Cut < End && !(Breakpoints[Cut - 1] & RxBreakIsWhitespace))
            break;

        Previous = PreviousCluster(Cut);
        Width -= RangeWidth(Previous, Cut);
        Cut = Previous;
    }

    Resume = Keep;
    if (Width > Target && Keep < End)
    {
        while (Resume < End && Width > Target)
        {
            UINT32 Next = ClusterEnd(Resume);

            Width -= RangeWidth(Resume, Next);
            Resume = Next;
        }
    }

    /* Never trim a line down to nothing */
    if (Width <= 0.0f && Cut == Start && Start < End)
    {
        Cut = ClusterEnd(Start);
        Width = RangeWidth(Start, Cut);
    }

    if (Cut < Resume)
    {
        for (UINT32 Index = Cut; Index < Resume; Index++)
            Hidden[Index] = 1;
        SignAt[LineIndex] = Cut;
        Width += SignWidth;
    }

    Line.WidthWithWhitespace = Width;
    Line.Width = Width;
    Line.IsTrimmed = TRUE;
}

VOID
RxTextLayout::Trim(VOID)
{
    FLOAT SignWidth = 0.0f;
    FLOAT Total = 0.0f;
    BOOLEAN Cutting = FALSE;

    Hidden.assign(Text.size(), 0);
    SignAt.assign(Lines.size(), MAXUINT32);
    if (Props.Trimming.granularity == DWRITE_TRIMMING_GRANULARITY_NONE)
        return;

    if (Props.TrimmingSign.Get())
    {
        DWRITE_INLINE_OBJECT_METRICS Sign = {};

        if (SUCCEEDED(Props.TrimmingSign->GetMetrics(&Sign)))
            SignWidth = Sign.width;
    }

    for (UINT32 Index = 0; Index < Lines.size(); Index++)
    {
        RxTextLine& Line = Lines[Index];

        if (Cutting)
        {
            for (UINT32 Char = Line.Start; Char < Line.Start + Line.Length; Char++)
                Hidden[Char] = 1;
            Line.Height = 0.0f;
            Line.Baseline = 0.0f;
            Line.Width = 0.0f;
            Line.WidthWithWhitespace = 0.0f;
            Line.IsTrimmed = TRUE;
            continue;
        }

        Total += Line.Height;
        if (Index + 1 < Lines.size() && Total + Lines[Index + 1].Height > MaxHeight)
        {
            Cutting = TRUE;
            if (Props.Trimming.delimiter == 0 && Line.Newline == 0)
            {
                TrimLine(Index, SignWidth);
                continue;
            }
        }

        if (Line.Width > MaxWidth)
            TrimLine(Index, SignWidth);
    }
}

/* Alignment and metrics */

VOID
RxTextLayout::AlignLines(VOID)
{
    BOOLEAN RightToLeft = (Props.ReadingDirection & 1) != 0;
    FLOAT W = (Props.ReadingDirection & 2) ? MaxHeight : MaxWidth;
    FLOAT H = (Props.ReadingDirection & 2) ? MaxWidth : MaxHeight;
    FLOAT Anchor;
    FLOAT Total = 0.0f;
    FLOAT Offset;
    FLOAT Left;
    FLOAT Right;
    FLOAT Widest = 0.0f;
    INT Side;

    /* 0 near, 1 center, 2 far */
    switch (Props.TextAlignment)
    {
        case DWRITE_TEXT_ALIGNMENT_TRAILING:
            Side = RightToLeft ? 0 : 2;
            break;
        case DWRITE_TEXT_ALIGNMENT_CENTER:
            Side = 1;
            break;
        default:
            Side = RightToLeft ? 2 : 0;
            break;
    }
    Anchor = Side == 0 ? 0.0f : (Side == 1 ? W * 0.5f : W);

    for (const auto& Line : Lines)
        Total += Line.Height;

    switch (Props.ParagraphAlignment)
    {
        case DWRITE_PARAGRAPH_ALIGNMENT_FAR:
            Offset = H - Total;
            break;
        case DWRITE_PARAGRAPH_ALIGNMENT_CENTER:
            Offset = (H - Total) * 0.5f;
            break;
        default:
            Offset = 0.0f;
            break;
    }
    if (Measuring != DWRITE_MEASURING_MODE_NATURAL && std::fabs(Offset * PixelsPerDip) < 8388600.0f)
        Offset = std::floor(Offset * PixelsPerDip + 0.25f) / PixelsPerDip;

    Left = FLT_MAX;
    Right = Anchor;
    {
        FLOAT Top = Offset;

        for (auto& Line : Lines)
        {
            FLOAT X;
            FLOAT Pen;

            /* The near edge is minus the leading side bearing adjustment, which is zero */
            if (Side == 0)
                X = -0.0f;
            else if (Side == 1)
                X = (W - Line.Width) * 0.5f;
            else
                X = W - Line.Width;
            if (RightToLeft)
                X -= Line.WidthWithWhitespace - Line.Width;
            if (Measuring != DWRITE_MEASURING_MODE_NATURAL && std::fabs(X * PixelsPerDip) < 8388600.0f)
                X = std::floor(X * PixelsPerDip + 0.25f) / PixelsPerDip;

            Line.X = X;
            Line.Top = Top;
            Pen = X;
            for (UINT32 Index = Line.FirstSpan; Index < Line.FirstSpan + Line.SpanCount; Index++)
            {
                RxTextSpan& Span = Spans[Index];

                Span.X = Pen;
                Span.Top = Top + Line.Baseline - Span.Above;
                Pen += Span.Width;
            }

            Left = std::min(Left, X);
            Right = std::max(Right, X + Line.WidthWithWhitespace);
            Widest = std::max(Widest, Line.Width);
            Top += Line.Height;
        }
    }

    Metrics.left = std::min(Left, Anchor);
    Metrics.top = Offset;
    Metrics.width = Widest;
    Metrics.widthIncludingTrailingWhitespace = Right - Left;
    Metrics.height = Total;
    Metrics.layoutWidth = MaxWidth;
    Metrics.layoutHeight = MaxHeight;
    Metrics.maxBidiReorderingDepth = 1;
    Metrics.lineCount = static_cast<UINT32>(Lines.size());
    Metrics.heightIncludingTrailingWhitespace = Total;
}

HRESULT
RxTextLayout::Layout(VOID)
{
    HRESULT hr;

    if (Valid)
        return S_OK;

    if ((Props.ReadingDirection & 2) != (Props.FlowDirection & 2))
        return DWRITE_E_FLOWDIRECTIONCONFLICTS;

    hr = ResolveFaces();
    if (FAILED(hr))
        return hr;

    BuildGlyphs();

    Breakpoints.assign(Text.size(), 0);
    Classes.assign(Text.size(), RxLbXX);
    RxAnalyzeBreakpoints(Text.c_str(), static_cast<UINT32>(Text.size()), Breakpoints.data(), Classes.data());

    BreakLines();
    Hidden.assign(Text.size(), 0);
    SignAt.assign(Lines.size(), MAXUINT32);
    BuildSpans();
    LineHeights();

    /* Trimming hides text and places the sign; spans are rebuilt around what it hid */
    if (Props.Trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE)
    {
        Trim();
        RebuildTrimmedSpans();
    }

    AlignLines();
    Valid = TRUE;
    return S_OK;
}

/* Spans of trimmed lines: hidden text goes into hidden spans, the sign sits at the cut */
VOID
RxTextLayout::RebuildTrimmedSpans(VOID)
{
    std::vector<RxTextSpan> Rebuilt;

    for (UINT32 LineIndex = 0; LineIndex < Lines.size(); LineIndex++)
    {
        RxTextLine& Line = Lines[LineIndex];
        UINT32 First = static_cast<UINT32>(Rebuilt.size());

        for (UINT32 Index = Line.FirstSpan; Index < Line.FirstSpan + Line.SpanCount; Index++)
        {
            RxTextSpan Span = Spans[Index];
            UINT32 Position = Span.Start;
            UINT32 End = Span.Start + Span.Length;

            if (Span.Length == 0)
            {
                Rebuilt.push_back(Span);
                continue;
            }

            while (Position < End)
            {
                RxTextSpan Piece = Span;
                UINT32 Next = Position + 1;
                BOOLEAN IsHidden = Hidden[Position] != 0;

                if (SignAt[LineIndex] == Position && Props.TrimmingSign.Get())
                {
                    RxTextSpan Sign = Span;

                    Sign.Start = Position;
                    Sign.Length = 0;
                    Sign.Inline = Props.TrimmingSign.Get();
                    Sign.IsSign = TRUE;
                    Sign.Hidden = FALSE;
                    Sign.Collapsed = FALSE;
                    {
                        DWRITE_INLINE_OBJECT_METRICS Object = {};

                        Props.TrimmingSign->GetMetrics(&Object);
                        Sign.Width = Object.width;
                    }
                    MeasureSpan(Sign);
                    Rebuilt.push_back(Sign);
                }

                while (Next < End && (Hidden[Next] != 0) == IsHidden &&
                       !(SignAt[LineIndex] == Next && Props.TrimmingSign.Get()))
                    Next++;

                Piece.Start = Position;
                Piece.Length = Next - Position;
                Piece.Hidden = IsHidden;
                Piece.Width = IsHidden ? 0.0f : (Piece.Inline != nullptr ? Span.Width : RangeWidth(Position, Next));
                if (IsHidden && Line.Height == 0.0f)
                {
                    Piece.Above = Piece.Below = Piece.Ascent = 0.0f;
                }
                Rebuilt.push_back(Piece);
                Position = Next;
            }
        }

        Line.FirstSpan = First;
        Line.SpanCount = static_cast<UINT32>(Rebuilt.size()) - First;
    }

    Spans = std::move(Rebuilt);
}

/* Queries */

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetMetrics(
    _Out_ DWRITE_TEXT_METRICS* textMetrics)
{
    HRESULT hr = Layout();

    if (FAILED(hr))
        return hr;
    memcpy(textMetrics, &Metrics, sizeof(*textMetrics));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetMetrics(
    _Out_ DWRITE_TEXT_METRICS1* textMetrics)
{
    HRESULT hr = Layout();

    if (FAILED(hr))
        return hr;
    *textMetrics = Metrics;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetLineMetrics(
    _Out_writes_opt_(maxLineCount) DWRITE_LINE_METRICS* lineMetrics,
    UINT32 maxLineCount,
    _Out_ UINT32* actualLineCount)
{
    HRESULT hr;

    *actualLineCount = 0;
    if (lineMetrics == nullptr && maxLineCount != 0)
        return E_INVALIDARG;

    hr = Layout();
    if (FAILED(hr))
        return hr;

    *actualLineCount = static_cast<UINT32>(Lines.size());
    if (Lines.size() > maxLineCount)
        return E_NOT_SUFFICIENT_BUFFER;

    for (SIZE_T Index = 0; Index < Lines.size(); Index++)
    {
        const RxTextLine& Line = Lines[Index];

        lineMetrics[Index].length = Line.Length;
        lineMetrics[Index].trailingWhitespaceLength = Line.TrailingWhitespace;
        lineMetrics[Index].newlineLength = Line.Newline;
        lineMetrics[Index].height = Line.Height;
        lineMetrics[Index].baseline = Line.Baseline;
        lineMetrics[Index].isTrimmed = Line.IsTrimmed;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetOverhangMetrics(
    _Out_ DWRITE_OVERHANG_METRICS* overhangs)
{
    HRESULT hr = Layout();

    if (FAILED(hr))
        return hr;

    /* The layout box against the text box; glyph ink is not measured */
    overhangs->left = -Metrics.left;
    overhangs->top = -Metrics.top;
    overhangs->right = Metrics.left + Metrics.width - MaxWidth;
    overhangs->bottom = Metrics.top + Metrics.height - MaxHeight;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetClusterMetrics(
    _Out_writes_opt_(maxClusterCount) DWRITE_CLUSTER_METRICS* clusterMetrics,
    UINT32 maxClusterCount,
    _Out_ UINT32* actualClusterCount)
{
    UINT32 Count = 0;
    HRESULT hr;

    *actualClusterCount = 0;
    hr = Layout();
    if (FAILED(hr))
        return hr;

    for (UINT32 Position = 0; Position < Text.size(); Position = ClusterEnd(Position))
        Count++;
    *actualClusterCount = Count;
    if (Count > maxClusterCount)
        return E_NOT_SUFFICIENT_BUFFER;
    if (clusterMetrics == nullptr && Count != 0)
        return E_INVALIDARG;

    Count = 0;
    for (UINT32 Position = 0; Position < Text.size();)
    {
        UINT32 End = ClusterEnd(Position);
        DWRITE_CLUSTER_METRICS& Out = clusterMetrics[Count++];

        Out.width = RangeWidth(Position, End);
        Out.length = static_cast<UINT16>(End - Position);
        Out.canWrapLineAfter = RxCanBreakAfter(Breakpoints[End - 1]);
        Out.isWhitespace = (Breakpoints[Position] & RxBreakIsWhitespace) != 0;
        Out.isNewline = RxIsNewlineClass(Classes[Position]);
        Out.isSoftHyphen = (Breakpoints[Position] & RxBreakIsSoftHyphen) != 0;
        Out.isRightToLeft = FALSE;
        Out.padding = 0;
        Position = End;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::DetermineMinWidth(
    _Out_ FLOAT* minWidth)
{
    FLOAT Word = 0.0f;
    FLOAT Widest = 0.0f;
    HRESULT hr;

    *minWidth = 0.0f;
    hr = Layout();
    if (FAILED(hr))
        return hr;

    for (UINT32 Position = 0; Position < Text.size();)
    {
        UINT32 End = ClusterEnd(Position);

        if (!(Breakpoints[Position] & RxBreakIsWhitespace))
            Word += RangeWidth(Position, End);
        if (RxCanBreakAfter(Breakpoints[End - 1]))
        {
            Widest = std::max(Widest, Word);
            Word = 0.0f;
        }
        Position = End;
    }
    *minWidth = std::max(Widest, Word);
    return S_OK;
}

/* Drawing */

namespace
{

/* Snaps the baseline axis to device pixels as the renderer reports them */
struct RxSnapper
{
    FLOAT Scale;
    FLOAT Offset;

    FLOAT
    Snap(
        _In_ FLOAT Value) const
    {
        FLOAT Pixel;

        if (Scale == 0.0f)
            return Value;
        Pixel = Scale * Value + Offset;
        if (std::fabs(Pixel) < 8388600.0f)
            Pixel = std::floor(Pixel + 0.5f);
        return (Pixel - Offset) / Scale;
    }

    FLOAT
    SnapOffset(
        _In_ FLOAT Value) const
    {
        FLOAT Pixel;
        FLOAT Rounded;

        if (Scale == 0.0f)
            return Value;
        Pixel = Scale * Value;
        Rounded = std::fabs(Pixel) < 8388600.0f ? std::floor(Pixel + 0.5f) : Pixel;
        if (Rounded == 0.0f && Pixel != 0.0f)
            Rounded = Pixel > 0.0f ? 1.0f : -1.0f;
        return Rounded / Scale;
    }
};

}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::Draw(
    _In_opt_ void* clientDrawingContext,
    _In_ IDWriteTextRenderer* renderer,
    FLOAT originX,
    FLOAT originY)
{
    RxSnapper Snap = { 0.0f, 0.0f };
    BOOL SnappingOff = FALSE;
    std::vector<UINT16> ClusterValues;
    HRESULT hr;

    if (renderer == nullptr)
        return E_INVALIDARG;

    hr = Layout();
    if (FAILED(hr))
        return hr;

    if ((Props.ReadingDirection & 2) != 0)
        return DWRITE_E_TEXTRENDERERINCOMPATIBLE;

    hr = renderer->IsPixelSnappingDisabled(clientDrawingContext, &SnappingOff);
    if (FAILED(hr))
        return hr;
    if (!SnappingOff)
    {
        FLOAT Ppd = 0.0f;
        DWRITE_MATRIX Matrix = { 1, 0, 0, 1, 0, 0 };

        hr = renderer->GetPixelsPerDip(clientDrawingContext, &Ppd);
        if (FAILED(hr))
            return hr;
        hr = renderer->GetCurrentTransform(clientDrawingContext, &Matrix);
        if (FAILED(hr))
            return hr;

        if (Ppd > 0.0f)
        {
            if (Matrix.m12 == 0.0f && Matrix.m22 != 0.0f)
                Snap = { Matrix.m22 * Ppd, Matrix.dy };
            else if (Matrix.m11 == 0.0f && Matrix.m21 != 0.0f)
                Snap = { Matrix.m21 * Ppd, Matrix.dx };
        }
    }

    /* Glyph runs */
    for (const auto& Span : Spans)
    {
        DWRITE_GLYPH_RUN Run = {};
        DWRITE_GLYPH_RUN_DESCRIPTION Description = {};
        UINT32 First;
        UINT32 Last;

        if (Span.Inline != nullptr || Span.Length == 0 || Span.Hidden)
            continue;

        First = GlyphFor(Span.Start);
        Last = GlyphFor(Span.Start + Span.Length);

        ClusterValues.resize(Span.Length);
        for (UINT32 Index = 0; Index < Span.Length; Index++)
            ClusterValues[Index] = static_cast<UINT16>(ClusterMap[Span.Start + Index] - First);

        Run.fontFace = Faces[Span.Face].Get();
        Run.fontEmSize = Span.Size;
        Run.glyphCount = Span.Collapsed ? 0 : Last - First;
        Run.glyphIndices = Glyphs.data() + First;
        Run.glyphAdvances = Advances.data() + First;
        Run.glyphOffsets = Offsets.data() + First;
        Run.isSideways = FALSE;
        Run.bidiLevel = 0;

        Description.localeName = Runs[Span.Run].Locale.c_str();
        Description.string = Text.c_str() + Span.Start;
        Description.stringLength = Span.Length;
        Description.clusterMap = ClusterValues.data();
        Description.textPosition = Span.Start;

        hr = renderer->DrawGlyphRun(clientDrawingContext, originX + Span.X, Snap.Snap(originY + Span.Top + Span.Ascent),
                                    Measuring, &Run, &Description, Runs[Span.Run].Effect.Get());
        if (FAILED(hr))
            return hr;
    }

    /* Inline objects, the trimming sign among them */
    for (const auto& Span : Spans)
    {
        FLOAT X;
        FLOAT Y;

        if (Span.Inline == nullptr || Span.Hidden)
            continue;

        X = originX + Span.X;
        Y = Snap.Snap(originY + Span.Top + Span.Ascent);
        hr = renderer->DrawInlineObject(clientDrawingContext, X, Y - Span.Ascent, Span.Inline, FALSE, FALSE,
                                        Runs[Span.Run].Effect.Get());
        if (FAILED(hr))
            return hr;
    }

    /* Underlines, one per group of underlined spans on a line */
    for (const auto& Line : Lines)
    {
        UINT32 Index = Line.FirstSpan;
        UINT32 End = Line.FirstSpan + Line.SpanCount;

        while (Index < End)
        {
            UINT32 GroupEnd;
            FLOAT Total = 0.0f;
            FLOAT Offset = 0.0f;
            FLOAT Thickness = 0.0f;
            FLOAT RunHeight = 0.0f;
            FLOAT Baseline = -FLT_MAX;

            if (!Runs[Spans[Index].Run].Underline)
            {
                Index++;
                continue;
            }

            for (GroupEnd = Index; GroupEnd < End && Runs[Spans[GroupEnd].Run].Underline; GroupEnd++)
            {
                const RxTextSpan& Span = Spans[GroupEnd];
                DWRITE_FONT_METRICS Font;
                FLOAT Scale;

                Faces[Span.Face]->GetMetrics(&Font);
                Scale = Span.Size / Font.designUnitsPerEm;
                Total += Span.Width;
                Offset += -Font.underlinePosition * Scale * Span.Width;
                Thickness += Font.underlineThickness * Scale * Span.Width;
                RunHeight = std::max(RunHeight, Font.capHeight * Scale);
                Baseline = std::max(Baseline, Span.Top + Span.Ascent);
            }

            if (Total > 0.0f)
            {
                DWRITE_UNDERLINE Underline = {};

                Underline.width = Total;
                Underline.thickness = Thickness / Total;
                Underline.offset = Snap.SnapOffset(Offset / Total);
                Underline.runHeight = RunHeight;
                Underline.readingDirection = Props.ReadingDirection;
                Underline.flowDirection = Props.FlowDirection;
                Underline.localeName = Runs[Spans[Index].Run].Locale.c_str();
                Underline.measuringMode = Measuring;

                hr = renderer->DrawUnderline(clientDrawingContext, originX + Spans[Index].X,
                                             Snap.Snap(originY + Baseline), &Underline,
                                             Runs[Spans[Index].Run].Effect.Get());
                if (FAILED(hr))
                    return hr;
            }
            Index = GroupEnd;
        }
    }

    /* Strikethroughs, per span */
    for (const auto& Span : Spans)
    {
        DWRITE_STRIKETHROUGH Strike = {};
        DWRITE_FONT_METRICS Font;
        FLOAT Scale;

        if (!Runs[Span.Run].Strikethrough || Span.Width <= 0.0f || Span.Hidden)
            continue;

        Faces[Span.Face]->GetMetrics(&Font);
        Scale = Span.Size / Font.designUnitsPerEm;
        Strike.width = Span.Width;
        Strike.thickness = Font.strikethroughThickness * Scale;
        Strike.offset = -Font.strikethroughPosition * Scale;
        Strike.readingDirection = Props.ReadingDirection;
        Strike.flowDirection = Props.FlowDirection;
        Strike.measuringMode = Measuring;

        hr = renderer->DrawStrikethrough(clientDrawingContext, originX + Span.X, originY + Span.Top + Span.Ascent,
                                         &Strike, Runs[Span.Run].Effect.Get());
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Hit testing */

HRESULT
STDMETHODCALLTYPE
RxTextLayout::HitTestTextPosition(
    UINT32 textPosition,
    BOOL isTrailingHit,
    _Out_ FLOAT* pointX,
    _Out_ FLOAT* pointY,
    _Out_ DWRITE_HIT_TEST_METRICS* hitTestMetrics)
{
    UINT32 LineIndex = 0;
    UINT32 Cluster;
    UINT32 End;
    HRESULT hr;

    *pointX = 0.0f;
    *pointY = 0.0f;
    memset(hitTestMetrics, 0, sizeof(*hitTestMetrics));

    hr = Layout();
    if (FAILED(hr))
        return hr;

    if (textPosition > Text.size())
        textPosition = static_cast<UINT32>(Text.size());

    for (UINT32 Index = 0; Index < Lines.size(); Index++)
    {
        if (textPosition >= Lines[Index].Start)
            LineIndex = Index;
    }

    const RxTextLine& Line = Lines[LineIndex];
    Cluster = textPosition;
    while (Cluster > 0 && Cluster < Text.size() && !ClusterStart[Cluster])
        Cluster--;
    End = Cluster < Text.size() ? ClusterEnd(Cluster) : Cluster;

    hitTestMetrics->textPosition = Cluster;
    hitTestMetrics->length = End - Cluster;
    hitTestMetrics->left = Line.X + RangeWidth(Line.Start, Cluster);
    hitTestMetrics->top = Line.Top;
    hitTestMetrics->width = RangeWidth(Cluster, End);
    hitTestMetrics->height = Line.Height;
    hitTestMetrics->bidiLevel = 0;
    hitTestMetrics->isText = Cluster < Text.size() && !Runs[CharRun[Cluster]].Inline.Get();
    hitTestMetrics->isTrimmed = Cluster < Text.size() && Hidden[Cluster] != 0;

    *pointX = hitTestMetrics->left + (isTrailingHit ? hitTestMetrics->width : 0.0f);
    *pointY = Line.Top;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::HitTestPoint(
    FLOAT pointX,
    FLOAT pointY,
    _Out_ BOOL* isTrailingHit,
    _Out_ BOOL* isInside,
    _Out_ DWRITE_HIT_TEST_METRICS* hitTestMetrics)
{
    UINT32 LineIndex = 0;
    BOOLEAN InsideY = TRUE;
    FLOAT Pen;
    HRESULT hr;

    *isTrailingHit = FALSE;
    *isInside = FALSE;
    memset(hitTestMetrics, 0, sizeof(*hitTestMetrics));

    hr = Layout();
    if (FAILED(hr))
        return hr;

    if (Lines.empty())
        return S_OK;

    if (pointY < Lines[0].Top)
    {
        InsideY = FALSE;
    }
    else
    {
        LineIndex = static_cast<UINT32>(Lines.size() - 1);
        for (UINT32 Index = 0; Index < Lines.size(); Index++)
        {
            if (pointY < Lines[Index].Top + Lines[Index].Height)
            {
                LineIndex = Index;
                break;
            }
        }
        if (pointY >= Lines[LineIndex].Top + Lines[LineIndex].Height)
            InsideY = FALSE;
    }

    const RxTextLine& Line = Lines[LineIndex];
    UINT32 ContentEnd = Line.Start + Line.Length - Line.Newline;
    UINT32 Position = Line.Start;

    Pen = Line.X;
    if (pointX < Pen || Line.Length == 0)
    {
        FLOAT X;
        FLOAT Y;

        hr = HitTestTextPosition(Line.Start, FALSE, &X, &Y, hitTestMetrics);
        *isInside = FALSE;
        return hr;
    }

    while (Position < ContentEnd)
    {
        UINT32 End = ClusterEnd(Position);
        FLOAT Width = RangeWidth(Position, End);

        if (pointX < Pen + Width)
        {
            FLOAT X;
            FLOAT Y;

            hr = HitTestTextPosition(Position, FALSE, &X, &Y, hitTestMetrics);
            *isTrailingHit = pointX >= Pen + Width * 0.5f;
            *isInside = InsideY;
            return hr;
        }
        Pen += Width;
        Position = End;
    }

    /* Past the end of the line: the last cluster, trailing side */
    if (ContentEnd > Line.Start)
    {
        UINT32 Last = ContentEnd - 1;
        FLOAT X;
        FLOAT Y;

        while (Last > Line.Start && !ClusterStart[Last])
            Last--;
        hr = HitTestTextPosition(Last, FALSE, &X, &Y, hitTestMetrics);
        *isTrailingHit = TRUE;
    }
    *isInside = FALSE;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::HitTestTextRange(
    UINT32 textPosition,
    UINT32 textLength,
    FLOAT originX,
    FLOAT originY,
    _Out_writes_opt_(maxHitTestMetricsCount) DWRITE_HIT_TEST_METRICS* hitTestMetrics,
    UINT32 maxHitTestMetricsCount,
    _Out_ UINT32* actualHitTestMetricsCount)
{
    UINT32 Count = 0;
    UINT64 RangeEnd = static_cast<UINT64>(textPosition) + textLength;
    HRESULT hr;

    *actualHitTestMetricsCount = 0;
    hr = Layout();
    if (FAILED(hr))
        return hr;

    for (const auto& Line : Lines)
    {
        UINT32 Start = std::max(textPosition, Line.Start);
        UINT64 End = std::min<UINT64>(RangeEnd, static_cast<UINT64>(Line.Start) + Line.Length);

        if (Start >= End && !(textLength == 0 && textPosition >= Line.Start &&
                              textPosition <= Line.Start + Line.Length && Count == 0))
            continue;

        if (Count < maxHitTestMetricsCount && hitTestMetrics != nullptr)
        {
            DWRITE_HIT_TEST_METRICS& Out = hitTestMetrics[Count];

            Out.textPosition = Start;
            Out.length = static_cast<UINT32>(End > Start ? End - Start : 0);
            Out.left = originX + Line.X + RangeWidth(Line.Start, Start);
            Out.top = originY + Line.Top;
            Out.width = End > Start ? RangeWidth(Start, static_cast<UINT32>(End)) : 0.0f;
            Out.height = Line.Height;
            Out.bidiLevel = 0;
            Out.isText = TRUE;
            Out.isTrimmed = Line.IsTrimmed;
        }
        Count++;
    }

    *actualHitTestMetricsCount = Count;
    return Count > maxHitTestMetricsCount ? E_NOT_SUFFICIENT_BUFFER : S_OK;
}

/* RxEllipsisSign */

HRESULT
RxEllipsisSign::Create(
    _In_ RxWriteFactory* Factory,
    _In_ IDWriteTextFormat* Format,
    _COM_Outptr_ IDWriteInlineObject** Sign)
{
    RxEllipsisSign* Result;
    RxTextLayout* Layout;
    DWRITE_TEXT_METRICS1 Metrics;
    DWRITE_LINE_METRICS Line[2] = {};
    UINT32 Lines = 0;
    FLOAT Descent = 0.0f;
    WCHAR Ellipsis;
    HRESULT hr;

    *Sign = nullptr;
    if (Format == nullptr)
        return E_INVALIDARG;

    Ellipsis = (Format->GetReadingDirection() & 2) ? 0x22EE : 0x2026;
    hr = RxTextLayout::Create(Factory, &Ellipsis, 1, Format, 0.0f, 0.0f, DWRITE_MEASURING_MODE_NATURAL, 1.0f, nullptr,
                              &Layout);
    if (FAILED(hr))
        return hr;
    Layout->SetStacked();

    Result = new (std::nothrow) RxEllipsisSign();
    if (Result == nullptr)
    {
        Layout->Release();
        return E_OUTOFMEMORY;
    }
    Result->Inner.Attach(Layout);

    Layout->GetMetrics(&Metrics);
    if (SUCCEEDED(Layout->GetLineMetrics(Line, 2, &Lines)) && Lines != 0)
        Descent = Line[Lines - 1].height - Line[Lines - 1].baseline;

    if (Format->GetReadingDirection() & 2)
    {
        Result->OffsetX = Metrics.left + (Metrics.width - Descent);
        Result->OffsetY = Metrics.top;
        Result->ObjectMetrics.height = Metrics.heightIncludingTrailingWhitespace;
        Result->ObjectMetrics.baseline = Metrics.heightIncludingTrailingWhitespace;
        Result->ObjectMetrics.supportsSideways = TRUE;
    }
    else
    {
        Result->OffsetX = Metrics.left;
        Result->OffsetY = Metrics.top + (Metrics.height - Descent);
        Result->ObjectMetrics.width = Metrics.widthIncludingTrailingWhitespace;
    }

    *Sign = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxEllipsisSign::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteInlineObject))
    {
        *ppvObject = static_cast<IDWriteInlineObject*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxEllipsisSign)

HRESULT
STDMETHODCALLTYPE
RxEllipsisSign::Draw(
    _In_opt_ void* clientDrawingContext,
    _In_ IDWriteTextRenderer* renderer,
    FLOAT originX,
    FLOAT originY,
    BOOL isSideways,
    BOOL isRightToLeft,
    _In_opt_ IUnknown* clientDrawingEffect)
{
    return Inner->Draw(clientDrawingContext, renderer, originX - OffsetX, originY - OffsetY);
}

HRESULT
STDMETHODCALLTYPE
RxEllipsisSign::GetMetrics(
    _Out_ DWRITE_INLINE_OBJECT_METRICS* metrics)
{
    *metrics = ObjectMetrics;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxEllipsisSign::GetOverhangMetrics(
    _Out_ DWRITE_OVERHANG_METRICS* overhangs)
{
    return Inner->GetOverhangMetrics(overhangs);
}

HRESULT
STDMETHODCALLTYPE
RxEllipsisSign::GetBreakConditions(
    _Out_ DWRITE_BREAK_CONDITION* breakConditionBefore,
    _Out_ DWRITE_BREAK_CONDITION* breakConditionAfter)
{
    *breakConditionBefore = DWRITE_BREAK_CONDITION_NEUTRAL;
    *breakConditionAfter = DWRITE_BREAK_CONDITION_NEUTRAL;
    return S_OK;
}
