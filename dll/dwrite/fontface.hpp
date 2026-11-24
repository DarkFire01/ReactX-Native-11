/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font face objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "facetables.hpp"
#include "variations.hpp"
#include "gpos.hpp"

class RxGlyphRasterizer;

class RxFontFace final : public IDWriteFontFace5
{
public:
    static
    HRESULT
    Create(
        _In_ IDWriteFontFile* File,
        _In_ UINT32 FaceIndex,
        _In_ DWRITE_FONT_SIMULATIONS Simulations,
        _In_reads_opt_(ValueCount) const RxAxisValue* Values,
        _In_ UINT32 ValueCount,
        _Outptr_ RxFontFace** Face);

    RX_DECLARE_IUNKNOWN

#include "fontface.inl"

    RxFaceTables*
    Tables(VOID) const
    {
        return Data;
    }

    /* Created on first use; NULL when out of memory */
    RxGlyphRasterizer*
    Rasterizer(VOID);

    /* Axis values the face is instanced at; empty without fvar */
    const std::vector<RxAxisValue>&
    Coordinates(VOID) const
    {
        return AxisValues;
    }

    /* Latin kern feature pairs, read on first use */
    const RxKerning&
    Kerning(VOID);

    /* Our own faces carry their tables; anything else is not one of ours */
    static
    RxFontFace*
    FromInterface(
        _In_opt_ IUnknown* Face);

private:
    RxFontFace() : RefCount(1), Data(nullptr), Simulations(DWRITE_FONT_SIMULATIONS_NONE), Metrics{},
                   GlyphRasterizer(nullptr), RasterizerOnce(INIT_ONCE_STATIC_INIT) {}
    ~RxFontFace();

    VOID
    ComputeMetrics(VOID);

    LONG RefCount;
    RxFaceTables* Data;
    DWRITE_FONT_SIMULATIONS Simulations;
    std::vector<RxAxisValue> AxisValues;
    /* What GetFontAxisValues reports: design axes, then the standard ones */
    std::vector<RxAxisValue> ReportedValues;
    DWRITE_FONT_METRICS1 Metrics;
    RxGlyphRasterizer* GlyphRasterizer;
    INIT_ONCE RasterizerOnce;
    RxKerning Pairs;
    INIT_ONCE PairsOnce = INIT_ONCE_STATIC_INIT;
};

/* Glyph box in font units from the glyf header; FALSE for a glyph without outline */
BOOLEAN
RxGlyphBox(
    _In_ const RxFaceTables* Tables,
    _In_ USHORT Glyph,
    _Out_ SHORT* XMin,
    _Out_ SHORT* YMin,
    _Out_ SHORT* XMax,
    _Out_ SHORT* YMax);
