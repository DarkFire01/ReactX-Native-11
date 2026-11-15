/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Text layout objects, their properties and formatting ranges
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"
#include "textlayout.hpp"

HRESULT
RxTextLayout::Create(
    _In_ RxWriteFactory* WriteFactory,
    _In_reads_(Length) PCWSTR String,
    _In_ UINT32 Length,
    _In_ IDWriteTextFormat* Format,
    _In_ FLOAT Width,
    _In_ FLOAT Height,
    _In_ DWRITE_MEASURING_MODE Mode,
    _In_ FLOAT Scale,
    _In_opt_ const DWRITE_MATRIX* Matrix,
    _Outptr_ RxTextLayout** Layout)
{
    RxTextLayout* Result;
    RxFormatRun Run = {};
    HRESULT hr;

    *Layout = nullptr;
    Result = new (std::nothrow) RxTextLayout();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->Factory = WriteFactory;
    WriteFactory->AddRef();
    Result->Text.assign(String, Length);
    RxTextFormat::ReadProperties(Format, &Result->Props);
    Result->MaxWidth = Width;
    Result->MaxHeight = Height;
    Result->Measuring = Mode;
    Result->PixelsPerDip = Scale;
    if (Matrix != nullptr)
        Result->Transform = *Matrix;

    if (!Result->Props.Collection.Get())
    {
        RxFontCollection* System;

        hr = WriteFactory->SystemCollection(&System);
        if (FAILED(hr))
        {
            Result->Release();
            return hr;
        }
        Result->Props.Collection.Attach(System);
    }

    /* One run covers everything, past the end of the text too */
    Run.Start = 0;
    Run.Length = MAXUINT32;
    Run.Collection = Result->Props.Collection;
    Run.Family = Result->Props.Family;
    Run.Weight = Result->Props.Weight;
    Run.Style = Result->Props.Style;
    Run.Stretch = Result->Props.Stretch;
    Run.Size = Result->Props.Size;
    Run.Locale = Result->Props.Locale;
    Result->Runs.push_back(Run);

    *Layout = Result;
    return S_OK;
}

RxTextLayout::~RxTextLayout()
{
    if (Factory != nullptr)
        Factory->Release();
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextFormat) || riid == __uuidof(IDWriteTextLayout) ||
        riid == __uuidof(IDWriteTextLayout1) || riid == __uuidof(IDWriteTextLayout2))
    {
        *ppvObject = static_cast<IDWriteTextLayout2*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxTextLayout)

/* Paragraph properties */

#define RX_LAYOUT_SETTER(Method, Type, Name) \
    HRESULT \
    STDMETHODCALLTYPE \
    RxTextLayout::Method( \
        Type Name) \
    { \
        HRESULT hr = Props.Method(Name); \
        \
        if (SUCCEEDED(hr)) \
            Valid = FALSE; \
        return hr; \
    }

RX_LAYOUT_SETTER(SetTextAlignment, DWRITE_TEXT_ALIGNMENT, textAlignment)
RX_LAYOUT_SETTER(SetParagraphAlignment, DWRITE_PARAGRAPH_ALIGNMENT, paragraphAlignment)
RX_LAYOUT_SETTER(SetWordWrapping, DWRITE_WORD_WRAPPING, wordWrapping)
RX_LAYOUT_SETTER(SetReadingDirection, DWRITE_READING_DIRECTION, readingDirection)
RX_LAYOUT_SETTER(SetFlowDirection, DWRITE_FLOW_DIRECTION, flowDirection)
RX_LAYOUT_SETTER(SetIncrementalTabStop, FLOAT, incrementalTabStop)
RX_LAYOUT_SETTER(SetVerticalGlyphOrientation, DWRITE_VERTICAL_GLYPH_ORIENTATION, glyphOrientation)
RX_LAYOUT_SETTER(SetOpticalAlignment, DWRITE_OPTICAL_ALIGNMENT, opticalAlignment)

#undef RX_LAYOUT_SETTER

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetTrimming(
    _In_ DWRITE_TRIMMING const* trimmingOptions,
    _In_opt_ IDWriteInlineObject* trimmingSign)
{
    HRESULT hr = Props.SetTrimming(trimmingOptions, trimmingSign);

    if (SUCCEEDED(hr))
        Valid = FALSE;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetLineSpacing(
    DWRITE_LINE_SPACING_METHOD lineSpacingMethod,
    FLOAT lineSpacing,
    FLOAT baseline)
{
    HRESULT hr = Props.SetLineSpacing(lineSpacingMethod, lineSpacing, baseline);

    if (SUCCEEDED(hr))
        Valid = FALSE;
    return hr;
}

DWRITE_TEXT_ALIGNMENT
STDMETHODCALLTYPE
RxTextLayout::GetTextAlignment(VOID)
{
    return Props.TextAlignment;
}

DWRITE_PARAGRAPH_ALIGNMENT
STDMETHODCALLTYPE
RxTextLayout::GetParagraphAlignment(VOID)
{
    return Props.ParagraphAlignment;
}

DWRITE_WORD_WRAPPING
STDMETHODCALLTYPE
RxTextLayout::GetWordWrapping(VOID)
{
    return Props.WordWrapping;
}

DWRITE_READING_DIRECTION
STDMETHODCALLTYPE
RxTextLayout::GetReadingDirection(VOID)
{
    return Props.ReadingDirection;
}

DWRITE_FLOW_DIRECTION
STDMETHODCALLTYPE
RxTextLayout::GetFlowDirection(VOID)
{
    return Props.FlowDirection;
}

FLOAT
STDMETHODCALLTYPE
RxTextLayout::GetIncrementalTabStop(VOID)
{
    return Props.IncrementalTabStop;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetTrimming(
    _Out_ DWRITE_TRIMMING* trimmingOptions,
    _COM_Outptr_ IDWriteInlineObject** trimmingSign)
{
    return Props.GetTrimming(trimmingOptions, trimmingSign);
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetLineSpacing(
    _Out_ DWRITE_LINE_SPACING_METHOD* lineSpacingMethod,
    _Out_ FLOAT* lineSpacing,
    _Out_ FLOAT* baseline)
{
    return Props.GetLineSpacing(lineSpacingMethod, lineSpacing, baseline);
}

/* The format level getters report the start of the text */

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontCollection(
    _COM_Outptr_ IDWriteFontCollection** fontCollection)
{
    *fontCollection = Runs[0].Collection.Copy();
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxTextLayout::GetFontFamilyNameLength(VOID)
{
    return static_cast<UINT32>(Runs[0].Family.size());
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontFamilyName(
    _Out_writes_z_(nameSize) WCHAR* fontFamilyName,
    UINT32 nameSize)
{
    return RxCopyName(Runs[0].Family, fontFamilyName, nameSize);
}

DWRITE_FONT_WEIGHT
STDMETHODCALLTYPE
RxTextLayout::GetFontWeight(VOID)
{
    return Runs[0].Weight;
}

DWRITE_FONT_STYLE
STDMETHODCALLTYPE
RxTextLayout::GetFontStyle(VOID)
{
    return Runs[0].Style;
}

DWRITE_FONT_STRETCH
STDMETHODCALLTYPE
RxTextLayout::GetFontStretch(VOID)
{
    return Runs[0].Stretch;
}

FLOAT
STDMETHODCALLTYPE
RxTextLayout::GetFontSize(VOID)
{
    return Runs[0].Size;
}

UINT32
STDMETHODCALLTYPE
RxTextLayout::GetLocaleNameLength(VOID)
{
    return static_cast<UINT32>(Runs[0].Locale.size());
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetLocaleName(
    _Out_writes_z_(nameSize) WCHAR* localeName,
    UINT32 nameSize)
{
    return RxCopyName(Runs[0].Locale, localeName, nameSize);
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetMaxWidth(
    FLOAT maxWidth)
{
    if (!(maxWidth >= 0.0f))
        return E_INVALIDARG;
    if (maxWidth != MaxWidth)
    {
        MaxWidth = maxWidth;
        Valid = FALSE;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetMaxHeight(
    FLOAT maxHeight)
{
    if (!(maxHeight >= 0.0f))
        return E_INVALIDARG;
    if (maxHeight != MaxHeight)
    {
        MaxHeight = maxHeight;
        Valid = FALSE;
    }
    return S_OK;
}

FLOAT
STDMETHODCALLTYPE
RxTextLayout::GetMaxWidth(VOID)
{
    return MaxWidth;
}

FLOAT
STDMETHODCALLTYPE
RxTextLayout::GetMaxHeight(VOID)
{
    return MaxHeight;
}

/* Formatting runs */

UINT32
RxTextLayout::SplitRun(
    _In_ UINT32 Position)
{
    for (UINT32 Index = 0; Index < Runs.size(); Index++)
    {
        RxFormatRun& Run = Runs[Index];
        UINT64 End = static_cast<UINT64>(Run.Start) + Run.Length;

        if (Position == Run.Start)
            return Index;
        if (Position > Run.Start && Position < End)
        {
            RxFormatRun Tail = Run;

            Tail.Start = Position;
            Tail.Length = static_cast<UINT32>(End - Position);
            Run.Length = Position - Run.Start;
            Runs.insert(Runs.begin() + Index + 1, Tail);
            return Index + 1;
        }
    }
    return static_cast<UINT32>(Runs.size());
}

HRESULT
RxTextLayout::CheckRange(
    _In_ const DWRITE_TEXT_RANGE& Range)
{
    if (static_cast<UINT64>(Range.startPosition) + Range.length > MAXUINT32)
        return E_INVALIDARG;
    return S_OK;
}

template <typename Apply>
HRESULT
RxTextLayout::ApplyRange(
    _In_ const DWRITE_TEXT_RANGE& Range,
    _In_ Apply Change)
{
    UINT32 First;
    UINT32 Last;
    HRESULT hr = CheckRange(Range);

    if (FAILED(hr))
        return hr;
    if (Range.length == 0)
        return S_OK;

    First = SplitRun(Range.startPosition);
    Last = SplitRun(Range.startPosition + Range.length);
    for (UINT32 Index = First; Index < Last && Index < Runs.size(); Index++)
        Change(Runs[Index]);

    /* Neighbors that ended up the same are not merged; lookups do not care */
    Valid = FALSE;
    return S_OK;
}

const RxFormatRun&
RxTextLayout::RunAt(
    _In_ UINT32 Position,
    _Out_opt_ DWRITE_TEXT_RANGE* Range) const
{
    for (const auto& Run : Runs)
    {
        if (Position >= Run.Start && static_cast<UINT64>(Position) < static_cast<UINT64>(Run.Start) + Run.Length)
        {
            if (Range != nullptr)
            {
                Range->startPosition = Run.Start;
                Range->length = Run.Length;
            }
            return Run;
        }
    }
    if (Range != nullptr)
    {
        Range->startPosition = Runs.back().Start;
        Range->length = Runs.back().Length;
    }
    return Runs.back();
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontCollection(
    _In_ IDWriteFontCollection* fontCollection,
    DWRITE_TEXT_RANGE textRange)
{
    RxComPtr<IDWriteFontCollection> Value(fontCollection);

    if (fontCollection == nullptr)
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Collection = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontFamilyName(
    _In_z_ WCHAR const* fontFamilyName,
    DWRITE_TEXT_RANGE textRange)
{
    std::wstring Value;

    if (fontFamilyName == nullptr)
        return E_INVALIDARG;
    Value = fontFamilyName;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Family = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontWeight(
    DWRITE_FONT_WEIGHT fontWeight,
    DWRITE_TEXT_RANGE textRange)
{
    if (fontWeight < 1 || fontWeight > 999)
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Weight = fontWeight; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontStyle(
    DWRITE_FONT_STYLE fontStyle,
    DWRITE_TEXT_RANGE textRange)
{
    if (static_cast<UINT32>(fontStyle) > DWRITE_FONT_STYLE_ITALIC)
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Style = fontStyle; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontStretch(
    DWRITE_FONT_STRETCH fontStretch,
    DWRITE_TEXT_RANGE textRange)
{
    if (fontStretch < DWRITE_FONT_STRETCH_ULTRA_CONDENSED || fontStretch > DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Stretch = fontStretch; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontSize(
    FLOAT fontSize,
    DWRITE_TEXT_RANGE textRange)
{
    if (!(fontSize > 0.0f))
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Size = fontSize; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetUnderline(
    BOOL hasUnderline,
    DWRITE_TEXT_RANGE textRange)
{
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Underline = hasUnderline != FALSE; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetStrikethrough(
    BOOL hasStrikethrough,
    DWRITE_TEXT_RANGE textRange)
{
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Strikethrough = hasStrikethrough != FALSE; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetDrawingEffect(
    IUnknown* drawingEffect,
    DWRITE_TEXT_RANGE textRange)
{
    RxComPtr<IUnknown> Value(drawingEffect);

    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Effect = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetInlineObject(
    _In_ IDWriteInlineObject* inlineObject,
    DWRITE_TEXT_RANGE textRange)
{
    RxComPtr<IDWriteInlineObject> Value(inlineObject);

    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Inline = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetTypography(
    _In_ IDWriteTypography* typography,
    DWRITE_TEXT_RANGE textRange)
{
    RxComPtr<IDWriteTypography> Value(typography);

    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Typography = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetLocaleName(
    _In_z_ WCHAR const* localeName,
    DWRITE_TEXT_RANGE textRange)
{
    std::wstring Value;

    if (localeName == nullptr)
        return E_INVALIDARG;
    Value = localeName;
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.Locale = Value; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetPairKerning(
    BOOL isPairKerningEnabled,
    DWRITE_TEXT_RANGE textRange)
{
    return ApplyRange(textRange, [&](RxFormatRun& Run) { Run.PairKerning = isPairKerningEnabled != FALSE; });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetCharacterSpacing(
    FLOAT leadingSpacing,
    FLOAT trailingSpacing,
    FLOAT minimumAdvanceWidth,
    DWRITE_TEXT_RANGE textRange)
{
    if (!(minimumAdvanceWidth >= 0.0f))
        return E_INVALIDARG;
    return ApplyRange(textRange, [&](RxFormatRun& Run)
    {
        Run.Leading = leadingSpacing;
        Run.Trailing = trailingSpacing;
        Run.MinimumAdvance = minimumAdvanceWidth;
    });
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontCollection(
    UINT32 currentPosition,
    _COM_Outptr_ IDWriteFontCollection** fontCollection,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *fontCollection = RunAt(currentPosition, textRange).Collection.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontFamilyNameLength(
    UINT32 currentPosition,
    _Out_ UINT32* nameLength,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *nameLength = static_cast<UINT32>(RunAt(currentPosition, textRange).Family.size());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontFamilyName(
    UINT32 currentPosition,
    _Out_writes_z_(nameSize) WCHAR* fontFamilyName,
    UINT32 nameSize,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    return RxCopyName(RunAt(currentPosition, textRange).Family, fontFamilyName, nameSize);
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontWeight(
    UINT32 currentPosition,
    _Out_ DWRITE_FONT_WEIGHT* fontWeight,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *fontWeight = RunAt(currentPosition, textRange).Weight;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontStyle(
    UINT32 currentPosition,
    _Out_ DWRITE_FONT_STYLE* fontStyle,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *fontStyle = RunAt(currentPosition, textRange).Style;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontStretch(
    UINT32 currentPosition,
    _Out_ DWRITE_FONT_STRETCH* fontStretch,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *fontStretch = RunAt(currentPosition, textRange).Stretch;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontSize(
    UINT32 currentPosition,
    _Out_ FLOAT* fontSize,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *fontSize = RunAt(currentPosition, textRange).Size;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetUnderline(
    UINT32 currentPosition,
    _Out_ BOOL* hasUnderline,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *hasUnderline = RunAt(currentPosition, textRange).Underline;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetStrikethrough(
    UINT32 currentPosition,
    _Out_ BOOL* hasStrikethrough,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *hasStrikethrough = RunAt(currentPosition, textRange).Strikethrough;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetDrawingEffect(
    UINT32 currentPosition,
    _COM_Outptr_ IUnknown** drawingEffect,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *drawingEffect = RunAt(currentPosition, textRange).Effect.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetInlineObject(
    UINT32 currentPosition,
    _COM_Outptr_ IDWriteInlineObject** inlineObject,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *inlineObject = RunAt(currentPosition, textRange).Inline.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetTypography(
    UINT32 currentPosition,
    _COM_Outptr_ IDWriteTypography** typography,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *typography = RunAt(currentPosition, textRange).Typography.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetLocaleNameLength(
    UINT32 currentPosition,
    _Out_ UINT32* nameLength,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *nameLength = static_cast<UINT32>(RunAt(currentPosition, textRange).Locale.size());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetLocaleName(
    UINT32 currentPosition,
    _Out_writes_z_(nameSize) WCHAR* localeName,
    UINT32 nameSize,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    return RxCopyName(RunAt(currentPosition, textRange).Locale, localeName, nameSize);
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetPairKerning(
    UINT32 currentPosition,
    _Out_ BOOL* isPairKerningEnabled,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    *isPairKerningEnabled = RunAt(currentPosition, textRange).PairKerning;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetCharacterSpacing(
    UINT32 currentPosition,
    _Out_ FLOAT* leadingSpacing,
    _Out_ FLOAT* trailingSpacing,
    _Out_ FLOAT* minimumAdvanceWidth,
    _Out_opt_ DWRITE_TEXT_RANGE* textRange)
{
    const RxFormatRun& Run = RunAt(currentPosition, textRange);

    *leadingSpacing = Run.Leading;
    *trailingSpacing = Run.Trailing;
    *minimumAdvanceWidth = Run.MinimumAdvance;
    return S_OK;
}

DWRITE_VERTICAL_GLYPH_ORIENTATION
STDMETHODCALLTYPE
RxTextLayout::GetVerticalGlyphOrientation(VOID)
{
    return Props.GlyphOrientation;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetLastLineWrapping(
    BOOL isLastLineWrappingEnabled)
{
    Props.LastLineWrapping = isLastLineWrappingEnabled != FALSE;
    Valid = FALSE;
    return S_OK;
}

BOOL
STDMETHODCALLTYPE
RxTextLayout::GetLastLineWrapping(VOID)
{
    return Props.LastLineWrapping;
}

DWRITE_OPTICAL_ALIGNMENT
STDMETHODCALLTYPE
RxTextLayout::GetOpticalAlignment(VOID)
{
    return Props.OpticalAlignment;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::SetFontFallback(
    IDWriteFontFallback* fontFallback)
{
    Props.FontFallback = RxComPtr<IDWriteFontFallback>(fontFallback);
    Valid = FALSE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextLayout::GetFontFallback(
    __out IDWriteFontFallback** fontFallback)
{
    *fontFallback = Props.FontFallback.Copy();
    return S_OK;
}
