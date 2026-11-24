/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GDI interop and the bitmap render target
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "glyphrun.hpp"
#include "collection.hpp"

class RxWriteFactory;

/* One per factory; its references are the factory's */
class RxGdiInterop final : public IDWriteGdiInterop1
{
public:
    explicit RxGdiInterop(
        _In_ RxWriteFactory* Owner);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFromLOGFONT(
        _In_ LOGFONTW const* logFont,
        _COM_Outptr_ IDWriteFont** font) override;

    HRESULT
    STDMETHODCALLTYPE
    ConvertFontToLOGFONT(
        _In_ IDWriteFont* font,
        _Out_ LOGFONTW* logFont,
        _Out_ BOOL* isSystemFont) override;

    HRESULT
    STDMETHODCALLTYPE
    ConvertFontFaceToLOGFONT(
        _In_ IDWriteFontFace* font,
        _Out_ LOGFONTW* logFont) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFaceFromHdc(
        HDC hdc,
        _COM_Outptr_ IDWriteFontFace** fontFace) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateBitmapRenderTarget(
        _In_opt_ HDC hdc,
        UINT32 width,
        UINT32 height,
        _COM_Outptr_ IDWriteBitmapRenderTarget** renderTarget) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFromLOGFONT(
        _In_ LOGFONTW const* logFont,
        _In_opt_ IDWriteFontCollection* fontCollection,
        _COM_Outptr_ IDWriteFont** font) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFontSignature(
        _In_ IDWriteFontFace* fontFace,
        _Out_ FONTSIGNATURE* fontSignature) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFontSignature(
        _In_ IDWriteFont* font,
        _Out_ FONTSIGNATURE* fontSignature) override;

    HRESULT
    STDMETHODCALLTYPE
    GetMatchingFontsByLOGFONT(
        _In_ LOGFONT const* logFont,
        _In_ IDWriteFontSet* fontSet,
        _COM_Outptr_ IDWriteFontSet** filteredSet) override;

private:
    /* The name in the system locale, else en-us, else the first */
    const std::wstring&
    PickName(
        _In_ const RxNameList& Names) const;

    VOID
    FillLogFont(
        _In_ const RxFaceDescription& Description,
        _In_ DWRITE_FONT_SIMULATIONS Simulations,
        _In_ BOOLEAN IsSymbol,
        _Out_ LOGFONTW* LogFont) const;

    RxWriteFactory* Factory;
    std::wstring Locale;
};

class RxBitmapTarget final : public IDWriteBitmapRenderTarget1
{
public:
    static
    HRESULT
    Create(
        _In_opt_ HDC Reference,
        _In_ UINT32 Width,
        _In_ UINT32 Height,
        _Outptr_ RxBitmapTarget** Target);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    DrawGlyphRun(
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode,
        _In_ DWRITE_GLYPH_RUN const* glyphRun,
        IDWriteRenderingParams* renderingParams,
        COLORREF textColor,
        _Out_opt_ RECT* blackBoxRect) override;

    HDC
    STDMETHODCALLTYPE
    GetMemoryDC(VOID) override;

    FLOAT
    STDMETHODCALLTYPE
    GetPixelsPerDip(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPixelsPerDip(
        FLOAT pixelsPerDip) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCurrentTransform(
        _Out_ DWRITE_MATRIX* transform) override;

    HRESULT
    STDMETHODCALLTYPE
    SetCurrentTransform(
        _In_opt_ DWRITE_MATRIX const* transform) override;

    HRESULT
    STDMETHODCALLTYPE
    GetSize(
        _Out_ SIZE* size) override;

    HRESULT
    STDMETHODCALLTYPE
    Resize(
        UINT32 width,
        UINT32 height) override;

    DWRITE_TEXT_ANTIALIAS_MODE
    STDMETHODCALLTYPE
    GetTextAntialiasMode(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetTextAntialiasMode(
        DWRITE_TEXT_ANTIALIAS_MODE antialiasMode) override;

private:
    RxBitmapTarget() : RefCount(1), Dc(nullptr), Bitmap(nullptr), Original(nullptr), Bits(nullptr), Width(0),
                       Height(0), PixelsPerDip(1.0f), Transform{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
                       HasTransform(FALSE), Antialias(DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE) {}
    ~RxBitmapTarget();

    /* A zeroed top down 32 bpp section; no section and no bits for an empty size */
    HRESULT
    MakeSection(
        _In_ UINT32 NewWidth,
        _In_ UINT32 NewHeight,
        _Out_ HBITMAP* Section,
        _Out_ UINT32** SectionBits);

    LONG RefCount;
    HDC Dc;
    HBITMAP Bitmap;
    HGDIOBJ Original;
    UINT32* Bits;
    UINT32 Width;
    UINT32 Height;
    FLOAT PixelsPerDip;
    DWRITE_MATRIX Transform;
    BOOLEAN HasTransform;
    DWRITE_TEXT_ANTIALIAS_MODE Antialias;
};
