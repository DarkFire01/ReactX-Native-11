/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Turns glyphs of one face into sample masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "scan.hpp"

/* Rasterizer flags, as the rendering mode asks for them */
constexpr UINT32 RxRasterHint = 0x01;
constexpr UINT32 RxRasterBold = 0x02;
constexpr UINT32 RxRasterOblique = 0x04;
constexpr UINT32 RxRasterObliqueSideways = 0x08;
constexpr UINT32 RxRasterGdiClassic = 0x10;
constexpr UINT32 RxRasterGdiNatural = 0x20;
constexpr UINT32 RxRasterBitmaps = 0x40;
constexpr UINT32 RxRasterGray = 0x80;

/* How one size of a face is rasterized */
struct RxRasterSize
{
    /* Font space to pixels: x' = M11 x + M21 y, y' = M12 x + M22 y, y up, in pixels per em */
    double M11;
    double M12;
    double M21;
    double M22;
    UINT32 OversampleX;
    UINT32 OversampleY;
    UINT32 Flags;

    BOOLEAN
    operator==(
        _In_ const RxRasterSize& Other) const
    {
        return M11 == Other.M11 && M12 == Other.M12 && M21 == Other.M21 && M22 == Other.M22 &&
               OversampleX == Other.OversampleX && OversampleY == Other.OversampleY && Flags == Other.Flags;
    }
};

/* A glyph in samples, its mask placed relative to the glyph origin, y down */
struct RxGlyphImage
{
    RxSampleMask Mask;

    BOOLEAN
    IsEmpty(VOID) const
    {
        return Mask.Bits.empty();
    }
};

class RxGlyphRasterizer
{
public:
    RxGlyphRasterizer(
        _In_ RxFaceTables* Face);
    ~RxGlyphRasterizer();

    /* Serialized by the caller */
    HRESULT
    Rasterize(
        _In_ USHORT Glyph,
        _In_ const RxRasterSize& Size,
        _Out_ RxGlyphImage* Image);

    /* Hinted or scaled outline in 26.6 pixels, y up, origin at zero */
    HRESULT
    Outline(
        _In_ USHORT Glyph,
        _In_ const RxRasterSize& Size,
        _Inout_ std::vector<RxHintPoint>& Points,
        _Inout_ std::vector<USHORT>& ContourEnds,
        _Out_ RxDropoutControl* Dropout);

    SRWLOCK Lock;

private:
    HRESULT
    PrepareHinter(
        _In_ const RxRasterSize& Size);

    RxFaceTables* Face;
    RxTrueTypeHinter* Hinter;
    BOOLEAN HinterFailed;
    BOOLEAN HaveSize;
    RxRasterSize HintedSize;
    BOOLEAN HintUsable;
};
