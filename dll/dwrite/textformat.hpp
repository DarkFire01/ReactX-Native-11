/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Text formats and the paragraph properties they share with layouts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "collection.hpp"

/* Paragraph and default font properties a format holds and a layout copies */
struct RxTextProperties
{
    DWRITE_TEXT_ALIGNMENT TextAlignment;
    DWRITE_PARAGRAPH_ALIGNMENT ParagraphAlignment;
    DWRITE_WORD_WRAPPING WordWrapping;
    DWRITE_READING_DIRECTION ReadingDirection;
    DWRITE_FLOW_DIRECTION FlowDirection;
    FLOAT IncrementalTabStop;
    DWRITE_TRIMMING Trimming;
    RxComPtr<IDWriteInlineObject> TrimmingSign;
    DWRITE_LINE_SPACING LineSpacing;
    DWRITE_VERTICAL_GLYPH_ORIENTATION GlyphOrientation;
    BOOL LastLineWrapping;
    DWRITE_OPTICAL_ALIGNMENT OpticalAlignment;
    RxComPtr<IDWriteFontFallback> FontFallback;

    RxComPtr<IDWriteFontCollection> Collection;
    std::wstring Family;
    DWRITE_FONT_WEIGHT Weight;
    DWRITE_FONT_STYLE Style;
    DWRITE_FONT_STRETCH Stretch;
    FLOAT Size;
    std::wstring Locale;

    /* Shared setters; each returns E_INVALIDARG and leaves the value alone when it is out of range */
    HRESULT SetTextAlignment(_In_ DWRITE_TEXT_ALIGNMENT Value);
    HRESULT SetParagraphAlignment(_In_ DWRITE_PARAGRAPH_ALIGNMENT Value);
    HRESULT SetWordWrapping(_In_ DWRITE_WORD_WRAPPING Value);
    HRESULT SetReadingDirection(_In_ DWRITE_READING_DIRECTION Value);
    HRESULT SetFlowDirection(_In_ DWRITE_FLOW_DIRECTION Value);
    HRESULT SetIncrementalTabStop(_In_ FLOAT Value);
    HRESULT SetTrimming(_In_ const DWRITE_TRIMMING* Value, _In_opt_ IDWriteInlineObject* Sign);
    HRESULT GetTrimming(_Out_ DWRITE_TRIMMING* Value, _COM_Outptr_ IDWriteInlineObject** Sign);
    HRESULT SetLineSpacing(_In_ DWRITE_LINE_SPACING_METHOD Method, _In_ FLOAT Height, _In_ FLOAT Baseline);
    HRESULT GetLineSpacing(_Out_ DWRITE_LINE_SPACING_METHOD* Method, _Out_ FLOAT* Height, _Out_ FLOAT* Baseline);
    HRESULT SetVerticalGlyphOrientation(_In_ DWRITE_VERTICAL_GLYPH_ORIENTATION Value);
    HRESULT SetOpticalAlignment(_In_ DWRITE_OPTICAL_ALIGNMENT Value);
};

/* Copies a string out the way DirectWrite getters do */
HRESULT
RxCopyName(
    _In_ const std::wstring& Text,
    _Out_writes_z_(Size) WCHAR* Buffer,
    _In_ UINT32 Size);

BOOLEAN
RxValidFont(
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STYLE Style,
    _In_ DWRITE_FONT_STRETCH Stretch);

class RxTextFormat final : public IDWriteTextFormat1
{
public:
    static
    HRESULT
    Create(
        _In_ const RxTextProperties& Properties,
        _Outptr_ RxTextFormat** Format);

    /* Reads any format, ours or not, into properties */
    static
    VOID
    ReadProperties(
        _In_ IDWriteTextFormat* Format,
        _Out_ RxTextProperties* Properties);

    RX_DECLARE_IUNKNOWN

#include "textformat.inl"

    const RxTextProperties&
    Properties(VOID) const
    {
        return Fields;
    }

private:
    RxTextFormat() : RefCount(1) {}

    LONG RefCount;
    RxTextProperties Fields;
};
