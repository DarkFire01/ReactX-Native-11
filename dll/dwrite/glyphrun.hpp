/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Glyph run analysis and alpha textures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "fontface.hpp"
#include "glyphraster.hpp"
#include "params.hpp"

/* What a rendering mode does, for one antialias mode */
struct RxModeTraits
{
    BOOLEAN Valid;
    BYTE OversampleX;
    BYTE OversampleY;
    /* Output is a three channel ClearType texture */
    BOOLEAN ClearTypeTexture;
    BOOLEAN Grayscale;
    /* Blends with the GDI compatible gamma and no enhanced contrast */
    BOOLEAN GdiBlend;
    /* Whole pixel origins, forced grid fitting and integer ppem */
    BOOLEAN SnapToPixels;
    /* Grid fitting follows the font's gasp table */
    BOOLEAN GridFitAllowed;
    /* Hints with GDI classic widths */
    BOOLEAN GdiClassicHinting;
    BOOLEAN EmbeddedBitmaps;
    /* Subpixels the ClearType filter reaches past each side */
    BYTE FilterReach;
};

/* Traits for a mode, or an invalid entry for DEFAULT, OUTLINE and out of range values */
const RxModeTraits&
RxGetModeTraits(
    _In_ DWRITE_RENDERING_MODE1 Mode,
    _In_ DWRITE_TEXT_ANTIALIAS_MODE Antialias);

/* gasp behavior bits for a ppem, or MAXUINT32 when the font has no gasp table */
UINT32
RxGaspBehavior(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem);

/* Whether the gasp table asks for grid fitting at a ppem */
BOOLEAN
RxGaspGridFits(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem);

class RxGlyphRunAnalysis final : public IDWriteGlyphRunAnalysis
{
public:
    static
    HRESULT
    Create(
        _In_ const DWRITE_GLYPH_RUN* Run,
        _In_ FLOAT PixelsPerDip,
        _In_opt_ const DWRITE_MATRIX* Transform,
        _In_ DWRITE_RENDERING_MODE1 Mode,
        _In_ DWRITE_MEASURING_MODE Measuring,
        _In_ DWRITE_GRID_FIT_MODE GridFit,
        _In_ DWRITE_TEXT_ANTIALIAS_MODE Antialias,
        _In_ BOOLEAN Modern,
        _In_ FLOAT BaselineX,
        _In_ FLOAT BaselineY,
        _Outptr_ RxGlyphRunAnalysis** Analysis);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetAlphaTextureBounds(
        DWRITE_TEXTURE_TYPE textureType,
        _Out_ RECT* textureBounds) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateAlphaTexture(
        DWRITE_TEXTURE_TYPE textureType,
        _In_ RECT const* textureBounds,
        _Out_writes_bytes_(bufferSize) BYTE* alphaValues,
        UINT32 bufferSize) override;

    HRESULT
    STDMETHODCALLTYPE
    GetAlphaBlendParams(
        _In_ IDWriteRenderingParams* renderingParams,
        _Out_ FLOAT* blendGamma,
        _Out_ FLOAT* blendEnhancedContrast,
        _Out_ FLOAT* blendClearTypeLevel) override;

private:
    struct Placed
    {
        RxGlyphImage Image;
        /* Sample position of the image's top left corner */
        INT32 X;
        INT32 Y;
    };

    RxGlyphRunAnalysis() : RefCount(1), Traits{}, IsGrayscale(FALSE), Bounds{} {}

    VOID
    TextureBounds(
        _Out_ RECT* Result) const;

    /* Whether the sample at absolute sample coordinates is set in any glyph */
    VOID
    MergeSamples(
        _In_ INT32 Left,
        _In_ INT32 Top,
        _In_ INT32 Width,
        _In_ INT32 Height,
        _Inout_ std::vector<BYTE>& Samples) const;

    LONG RefCount;
    RxModeTraits Traits;
    BOOLEAN IsGrayscale;
    std::vector<Placed> Glyphs;
    /* Union of the glyph images, in samples */
    RECT Bounds;
};

/* Blend values a client applies with the alpha texture of a mode */
VOID
RxAlphaBlendValues(
    _In_ const RxModeTraits& Traits,
    _In_ BOOLEAN IsGrayscale,
    _In_ const RxParamValues& Params,
    _In_ FLOAT Red,
    _In_ FLOAT Green,
    _In_ FLOAT Blue,
    _Out_ FLOAT* Gamma,
    _Out_ FLOAT* Contrast,
    _Out_ FLOAT* ClearTypeLevel);
