/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Text layout
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "textformat.hpp"
#include "linebreak.hpp"

class RxWriteFactory;

/* Formatting of one range of a layout's text */
struct RxFormatRun
{
    UINT32 Start;
    UINT32 Length;
    RxComPtr<IDWriteFontCollection> Collection;
    std::wstring Family;
    DWRITE_FONT_WEIGHT Weight;
    DWRITE_FONT_STYLE Style;
    DWRITE_FONT_STRETCH Stretch;
    FLOAT Size;
    std::wstring Locale;
    BOOL Underline;
    BOOL Strikethrough;
    RxComPtr<IUnknown> Effect;
    RxComPtr<IDWriteInlineObject> Inline;
    RxComPtr<IDWriteTypography> Typography;
    BOOL PairKerning;
    FLOAT Leading;
    FLOAT Trailing;
    FLOAT MinimumAdvance;
};

/* A piece of one line with one face, one format run and one kind of content */
struct RxTextSpan
{
    UINT32 Start;
    UINT32 Length;
    UINT32 Run;
    UINT32 Face;
    FLOAT Size;
    UINT32 Line;
    /* Inline object of the run, the trimming sign, or NULL */
    IDWriteInlineObject* Inline;
    BOOLEAN IsSign;
    /* Removed by trimming */
    BOOLEAN Hidden;
    /* Newlines and controls: drawn with no glyphs */
    BOOLEAN Collapsed;
    FLOAT X;
    FLOAT Top;
    FLOAT Width;
    /* From the span top to the baseline */
    FLOAT Ascent;
    FLOAT Above;
    FLOAT Below;
    FLOAT LineAscent;
    FLOAT LineDescent;
};

struct RxTextLine
{
    UINT32 Start;
    UINT32 Length;
    UINT32 TrailingWhitespace;
    UINT32 Newline;
    FLOAT Width;
    FLOAT WidthWithWhitespace;
    FLOAT Height;
    FLOAT Baseline;
    FLOAT Top;
    FLOAT X;
    BOOL IsTrimmed;
    UINT32 FirstSpan;
    UINT32 SpanCount;
};

class RxTextLayout final : public IDWriteTextLayout2
{
public:
    static
    HRESULT
    Create(
        _In_ RxWriteFactory* Factory,
        _In_reads_(Length) PCWSTR Text,
        _In_ UINT32 Length,
        _In_ IDWriteTextFormat* Format,
        _In_ FLOAT MaxWidth,
        _In_ FLOAT MaxHeight,
        _In_ DWRITE_MEASURING_MODE Measuring,
        _In_ FLOAT PixelsPerDip,
        _In_opt_ const DWRITE_MATRIX* Transform,
        _Outptr_ RxTextLayout** Layout);

    RX_DECLARE_IUNKNOWN

#include "textlayout.inl"

    /* Forces stacked glyph orientation; used by the ellipsis sign */
    VOID
    SetStacked(VOID)
    {
        Props.GlyphOrientation = DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED;
        Valid = FALSE;
    }

private:
    RxTextLayout() : RefCount(1), Factory(nullptr), MaxWidth(0), MaxHeight(0),
                     Measuring(DWRITE_MEASURING_MODE_NATURAL), PixelsPerDip(1.0f), Transform{ 1, 0, 0, 1, 0, 0 },
                     Valid(FALSE), Metrics{} {}
    ~RxTextLayout();

    /* Formatting runs */
    UINT32 SplitRun(_In_ UINT32 Position);
    HRESULT CheckRange(_In_ const DWRITE_TEXT_RANGE& Range);
    const RxFormatRun& RunAt(_In_ UINT32 Position, _Out_opt_ DWRITE_TEXT_RANGE* Range) const;

    template <typename Apply>
    HRESULT
    ApplyRange(
        _In_ const DWRITE_TEXT_RANGE& Range,
        _In_ Apply Change);

    /* Layout passes */
    HRESULT Layout(VOID);
    HRESULT ResolveFaces(VOID);
    UINT32 FaceFor(_In_ const RxFormatRun& Run, _In_ UINT32 CodePoint);
    UINT32 AddFace(_In_ IDWriteFontFace* Face);
    VOID BuildGlyphs(VOID);
    VOID ApplyKerning(VOID);
    VOID BreakLines(VOID);
    VOID BuildSpans(VOID);
    VOID MeasureSpan(_Inout_ RxTextSpan& Span);
    VOID LineHeights(VOID);
    VOID Trim(VOID);
    VOID TrimLine(_In_ UINT32 LineIndex, _In_ FLOAT SignWidth);
    VOID RebuildTrimmedSpans(VOID);
    VOID AlignLines(VOID);

    /* Helpers over the built data */
    UINT32 ClusterEnd(_In_ UINT32 Position) const;
    FLOAT RangeWidth(_In_ UINT32 Start, _In_ UINT32 End) const;
    UINT32 GlyphFor(_In_ UINT32 Position) const;

    LONG RefCount;
    RxWriteFactory* Factory;
    std::wstring Text;
    RxTextProperties Props;
    FLOAT MaxWidth;
    FLOAT MaxHeight;
    DWRITE_MEASURING_MODE Measuring;
    FLOAT PixelsPerDip;
    DWRITE_MATRIX Transform;
    std::vector<RxFormatRun> Runs;

    /* Built on demand */
    BOOLEAN Valid;
    std::vector<RxComPtr<IDWriteFontFace>> Faces;
    std::vector<UINT32> RunFaces;
    std::vector<UINT32> CharFace;
    std::vector<UINT32> CharRun;
    std::vector<BYTE> Breakpoints;
    std::vector<RxBreakClass> Classes;
    std::vector<BYTE> ClusterStart;
    std::vector<UINT32> ClusterMap;
    std::vector<UINT16> Glyphs;
    std::vector<FLOAT> Advances;
    std::vector<DWRITE_GLYPH_OFFSET> Offsets;
    std::vector<RxTextLine> Lines;
    std::vector<RxTextSpan> Spans;
    /* Per character: removed by trimming */
    std::vector<BYTE> Hidden;
    /* Per line: where the trimming sign goes, or MAXUINT32 */
    std::vector<UINT32> SignAt;
    DWRITE_TEXT_METRICS1 Metrics;
};

/* The ellipsis a format gets from CreateEllipsisTrimmingSign */
class RxEllipsisSign final : public IDWriteInlineObject
{
public:
    static
    HRESULT
    Create(
        _In_ RxWriteFactory* Factory,
        _In_ IDWriteTextFormat* Format,
        _COM_Outptr_ IDWriteInlineObject** Sign);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    Draw(
        _In_opt_ void* clientDrawingContext,
        _In_ IDWriteTextRenderer* renderer,
        FLOAT originX,
        FLOAT originY,
        BOOL isSideways,
        BOOL isRightToLeft,
        _In_opt_ IUnknown* clientDrawingEffect) override;

    HRESULT
    STDMETHODCALLTYPE
    GetMetrics(
        _Out_ DWRITE_INLINE_OBJECT_METRICS* metrics) override;

    HRESULT
    STDMETHODCALLTYPE
    GetOverhangMetrics(
        _Out_ DWRITE_OVERHANG_METRICS* overhangs) override;

    HRESULT
    STDMETHODCALLTYPE
    GetBreakConditions(
        _Out_ DWRITE_BREAK_CONDITION* breakConditionBefore,
        _Out_ DWRITE_BREAK_CONDITION* breakConditionAfter) override;

private:
    RxEllipsisSign() : RefCount(1), OffsetX(0), OffsetY(0), ObjectMetrics{} {}

    LONG RefCount;
    RxComPtr<IDWriteTextLayout> Inner;
    FLOAT OffsetX;
    FLOAT OffsetY;
    DWRITE_INLINE_OBJECT_METRICS ObjectMetrics;
};
