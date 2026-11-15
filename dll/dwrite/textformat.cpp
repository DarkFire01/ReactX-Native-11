/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Text formats and the paragraph properties they share with layouts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "textformat.hpp"

static constexpr GUID RxTextFormatIid = { 0x61b0d7c4, 0x2f3e, 0x4a91, { 0xb8, 0x07, 0x5e, 0x4c, 0x93, 0xa2, 0x1d, 0x30 } };

HRESULT
RxCopyName(
    _In_ const std::wstring& Text,
    _Out_writes_z_(Size) WCHAR* Buffer,
    _In_ UINT32 Size)
{
    if (Buffer == nullptr || Size == 0)
        return E_NOT_SUFFICIENT_BUFFER;
    if (Text.size() + 1 > Size)
    {
        Buffer[0] = UNICODE_NULL;
        return E_NOT_SUFFICIENT_BUFFER;
    }
    memcpy(Buffer, Text.c_str(), (Text.size() + 1) * sizeof(WCHAR));
    return S_OK;
}

BOOLEAN
RxValidFont(
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch)
{
    return Weight >= 1 && Weight <= 999 && static_cast<UINT32>(Style) <= DWRITE_FONT_STYLE_ITALIC &&
           Stretch >= DWRITE_FONT_STRETCH_ULTRA_CONDENSED && Stretch <= DWRITE_FONT_STRETCH_ULTRA_EXPANDED;
}

/* RxTextProperties */

HRESULT
RxTextProperties::SetTextAlignment(
    _In_ DWRITE_TEXT_ALIGNMENT Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_TEXT_ALIGNMENT_JUSTIFIED)
        return E_INVALIDARG;
    TextAlignment = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetParagraphAlignment(
    _In_ DWRITE_PARAGRAPH_ALIGNMENT Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
        return E_INVALIDARG;
    ParagraphAlignment = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetWordWrapping(
    _In_ DWRITE_WORD_WRAPPING Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_WORD_WRAPPING_CHARACTER)
        return E_INVALIDARG;
    WordWrapping = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetReadingDirection(
    _In_ DWRITE_READING_DIRECTION Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_READING_DIRECTION_BOTTOM_TO_TOP)
        return E_INVALIDARG;
    ReadingDirection = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetFlowDirection(
    _In_ DWRITE_FLOW_DIRECTION Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT)
        return E_INVALIDARG;
    FlowDirection = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetIncrementalTabStop(
    _In_ FLOAT Value)
{
    if (!(Value > 0.0f))
        return E_INVALIDARG;
    IncrementalTabStop = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetTrimming(
    _In_ const DWRITE_TRIMMING* Value,
    _In_opt_ IDWriteInlineObject* Sign)
{
    if (Value == nullptr || static_cast<UINT32>(Value->granularity) > DWRITE_TRIMMING_GRANULARITY_WORD ||
        Value->delimiter >= 0x110000)
        return E_INVALIDARG;
    Trimming = *Value;
    TrimmingSign = RxComPtr<IDWriteInlineObject>(Sign);
    return S_OK;
}

HRESULT
RxTextProperties::GetTrimming(
    _Out_ DWRITE_TRIMMING* Value,
    _COM_Outptr_ IDWriteInlineObject** Sign)
{
    *Value = Trimming;
    *Sign = TrimmingSign.Copy();
    return S_OK;
}

HRESULT
RxTextProperties::SetLineSpacing(
    _In_ DWRITE_LINE_SPACING_METHOD Method,
    _In_ FLOAT Height,
    _In_ FLOAT Baseline)
{
    if (!(Height >= 0.0f) || static_cast<UINT32>(Method) > DWRITE_LINE_SPACING_METHOD_PROPORTIONAL)
        return E_INVALIDARG;
    LineSpacing.method = Method;
    LineSpacing.height = Height;
    LineSpacing.baseline = Baseline;
    LineSpacing.leadingBefore = 0.0f;
    LineSpacing.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_DEFAULT;
    return S_OK;
}

HRESULT
RxTextProperties::GetLineSpacing(
    _Out_ DWRITE_LINE_SPACING_METHOD* Method,
    _Out_ FLOAT* Height,
    _Out_ FLOAT* Baseline)
{
    *Method = LineSpacing.method;
    *Height = LineSpacing.height;
    *Baseline = LineSpacing.baseline;
    return S_OK;
}

HRESULT
RxTextProperties::SetVerticalGlyphOrientation(
    _In_ DWRITE_VERTICAL_GLYPH_ORIENTATION Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED)
        return E_INVALIDARG;
    GlyphOrientation = Value;
    return S_OK;
}

HRESULT
RxTextProperties::SetOpticalAlignment(
    _In_ DWRITE_OPTICAL_ALIGNMENT Value)
{
    if (static_cast<UINT32>(Value) > DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS)
        return E_INVALIDARG;
    OpticalAlignment = Value;
    return S_OK;
}

/* RxTextFormat */

HRESULT
RxTextFormat::Create(
    _In_ const RxTextProperties& Properties,
    _Outptr_ RxTextFormat** Format)
{
    *Format = new (std::nothrow) RxTextFormat();
    if (*Format == nullptr)
        return E_OUTOFMEMORY;
    (*Format)->Fields = Properties;
    return S_OK;
}

VOID
RxTextFormat::ReadProperties(
    _In_ IDWriteTextFormat* Format,
    _Out_ RxTextProperties* Properties)
{
    RxTextFormat* Ours = nullptr;
    RxComPtr<IDWriteTextFormat1> Format1;
    RxComPtr<IDWriteTextFormat3> Format3;
    std::vector<WCHAR> Name;

    if (SUCCEEDED(Format->QueryInterface(RxTextFormatIid, reinterpret_cast<void**>(&Ours))))
    {
        *Properties = Ours->Fields;
        Ours->Release();
        return;
    }

    Properties->TextAlignment = Format->GetTextAlignment();
    Properties->ParagraphAlignment = Format->GetParagraphAlignment();
    Properties->WordWrapping = Format->GetWordWrapping();
    Properties->ReadingDirection = Format->GetReadingDirection();
    Properties->FlowDirection = Format->GetFlowDirection();
    Properties->IncrementalTabStop = Format->GetIncrementalTabStop();
    Format->GetTrimming(&Properties->Trimming, Properties->TrimmingSign.Put());
    Properties->LineSpacing = {};
    Format->GetLineSpacing(&Properties->LineSpacing.method, &Properties->LineSpacing.height,
                           &Properties->LineSpacing.baseline);
    Properties->GlyphOrientation = DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT;
    Properties->LastLineWrapping = TRUE;
    Properties->OpticalAlignment = DWRITE_OPTICAL_ALIGNMENT_NONE;

    if (SUCCEEDED(Format->QueryInterface(__uuidof(IDWriteTextFormat1), Format1.PutVoid())))
    {
        Properties->GlyphOrientation = Format1->GetVerticalGlyphOrientation();
        Properties->LastLineWrapping = Format1->GetLastLineWrapping();
        Properties->OpticalAlignment = Format1->GetOpticalAlignment();
        Format1->GetFontFallback(Properties->FontFallback.Put());
    }
    if (SUCCEEDED(Format->QueryInterface(__uuidof(IDWriteTextFormat3), Format3.PutVoid())))
        Format3->GetLineSpacing(&Properties->LineSpacing);

    Format->GetFontCollection(Properties->Collection.Put());
    Name.resize(Format->GetFontFamilyNameLength() + 1);
    Format->GetFontFamilyName(Name.data(), static_cast<UINT32>(Name.size()));
    Properties->Family = Name.data();
    Properties->Weight = Format->GetFontWeight();
    Properties->Style = Format->GetFontStyle();
    Properties->Stretch = Format->GetFontStretch();
    Properties->Size = Format->GetFontSize();
    Name.resize(Format->GetLocaleNameLength() + 1);
    Format->GetLocaleName(Name.data(), static_cast<UINT32>(Name.size()));
    Properties->Locale = Name.data();
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxTextFormatIid || riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextFormat) ||
        riid == __uuidof(IDWriteTextFormat1))
    {
        *ppvObject = static_cast<IDWriteTextFormat1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxTextFormat)

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetTextAlignment(
    DWRITE_TEXT_ALIGNMENT textAlignment)
{
    return Fields.SetTextAlignment(textAlignment);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetParagraphAlignment(
    DWRITE_PARAGRAPH_ALIGNMENT paragraphAlignment)
{
    return Fields.SetParagraphAlignment(paragraphAlignment);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetWordWrapping(
    DWRITE_WORD_WRAPPING wordWrapping)
{
    return Fields.SetWordWrapping(wordWrapping);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetReadingDirection(
    DWRITE_READING_DIRECTION readingDirection)
{
    return Fields.SetReadingDirection(readingDirection);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetFlowDirection(
    DWRITE_FLOW_DIRECTION flowDirection)
{
    return Fields.SetFlowDirection(flowDirection);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetIncrementalTabStop(
    FLOAT incrementalTabStop)
{
    return Fields.SetIncrementalTabStop(incrementalTabStop);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetTrimming(
    _In_ DWRITE_TRIMMING const* trimmingOptions,
    _In_opt_ IDWriteInlineObject* trimmingSign)
{
    return Fields.SetTrimming(trimmingOptions, trimmingSign);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetLineSpacing(
    DWRITE_LINE_SPACING_METHOD lineSpacingMethod,
    FLOAT lineSpacing,
    FLOAT baseline)
{
    return Fields.SetLineSpacing(lineSpacingMethod, lineSpacing, baseline);
}

DWRITE_TEXT_ALIGNMENT
STDMETHODCALLTYPE
RxTextFormat::GetTextAlignment(VOID)
{
    return Fields.TextAlignment;
}

DWRITE_PARAGRAPH_ALIGNMENT
STDMETHODCALLTYPE
RxTextFormat::GetParagraphAlignment(VOID)
{
    return Fields.ParagraphAlignment;
}

DWRITE_WORD_WRAPPING
STDMETHODCALLTYPE
RxTextFormat::GetWordWrapping(VOID)
{
    return Fields.WordWrapping;
}

DWRITE_READING_DIRECTION
STDMETHODCALLTYPE
RxTextFormat::GetReadingDirection(VOID)
{
    return Fields.ReadingDirection;
}

DWRITE_FLOW_DIRECTION
STDMETHODCALLTYPE
RxTextFormat::GetFlowDirection(VOID)
{
    return Fields.FlowDirection;
}

FLOAT
STDMETHODCALLTYPE
RxTextFormat::GetIncrementalTabStop(VOID)
{
    return Fields.IncrementalTabStop;
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetTrimming(
    _Out_ DWRITE_TRIMMING* trimmingOptions,
    _COM_Outptr_ IDWriteInlineObject** trimmingSign)
{
    return Fields.GetTrimming(trimmingOptions, trimmingSign);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetLineSpacing(
    _Out_ DWRITE_LINE_SPACING_METHOD* lineSpacingMethod,
    _Out_ FLOAT* lineSpacing,
    _Out_ FLOAT* baseline)
{
    return Fields.GetLineSpacing(lineSpacingMethod, lineSpacing, baseline);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetFontCollection(
    _COM_Outptr_ IDWriteFontCollection** fontCollection)
{
    *fontCollection = Fields.Collection.Copy();
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxTextFormat::GetFontFamilyNameLength(VOID)
{
    return static_cast<UINT32>(Fields.Family.size());
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetFontFamilyName(
    _Out_writes_z_(nameSize) WCHAR* fontFamilyName,
    UINT32 nameSize)
{
    return RxCopyName(Fields.Family, fontFamilyName, nameSize);
}

DWRITE_FONT_WEIGHT
STDMETHODCALLTYPE
RxTextFormat::GetFontWeight(VOID)
{
    return Fields.Weight;
}

DWRITE_FONT_STYLE
STDMETHODCALLTYPE
RxTextFormat::GetFontStyle(VOID)
{
    return Fields.Style;
}

DWRITE_FONT_STRETCH
STDMETHODCALLTYPE
RxTextFormat::GetFontStretch(VOID)
{
    return Fields.Stretch;
}

FLOAT
STDMETHODCALLTYPE
RxTextFormat::GetFontSize(VOID)
{
    return Fields.Size;
}

UINT32
STDMETHODCALLTYPE
RxTextFormat::GetLocaleNameLength(VOID)
{
    return static_cast<UINT32>(Fields.Locale.size());
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetLocaleName(
    _Out_writes_z_(nameSize) WCHAR* localeName,
    UINT32 nameSize)
{
    return RxCopyName(Fields.Locale, localeName, nameSize);
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetVerticalGlyphOrientation(
    DWRITE_VERTICAL_GLYPH_ORIENTATION glyphOrientation)
{
    return Fields.SetVerticalGlyphOrientation(glyphOrientation);
}

DWRITE_VERTICAL_GLYPH_ORIENTATION
STDMETHODCALLTYPE
RxTextFormat::GetVerticalGlyphOrientation(VOID)
{
    return Fields.GlyphOrientation;
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetLastLineWrapping(
    BOOL isLastLineWrappingEnabled)
{
    Fields.LastLineWrapping = isLastLineWrappingEnabled != FALSE;
    return S_OK;
}

BOOL
STDMETHODCALLTYPE
RxTextFormat::GetLastLineWrapping(VOID)
{
    return Fields.LastLineWrapping;
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetOpticalAlignment(
    DWRITE_OPTICAL_ALIGNMENT opticalAlignment)
{
    return Fields.SetOpticalAlignment(opticalAlignment);
}

DWRITE_OPTICAL_ALIGNMENT
STDMETHODCALLTYPE
RxTextFormat::GetOpticalAlignment(VOID)
{
    return Fields.OpticalAlignment;
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::SetFontFallback(
    IDWriteFontFallback* fontFallback)
{
    Fields.FontFallback = RxComPtr<IDWriteFontFallback>(fontFallback);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextFormat::GetFontFallback(
    _COM_Outptr_ IDWriteFontFallback** fontFallback)
{
    *fontFallback = Fields.FontFallback.Copy();
    return S_OK;
}
