/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Glyph run analysis and alpha textures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "glyphrun.hpp"

#include <cmath>

static
double
RxRoundHalfUp(
    _In_ double Value)
{
    return std::floor(Value + 0.5);
}

static
INT32
RxFloorDiv(
    _In_ INT32 Value,
    _In_ INT32 Divisor)
{
    INT32 Quotient = Value / Divisor;

    if ((Value % Divisor) != 0 && ((Value < 0) != (Divisor < 0)))
        Quotient--;
    return Quotient;
}

static
INT32
RxCeilDiv(
    _In_ INT32 Value,
    _In_ INT32 Divisor)
{
    return -RxFloorDiv(-Value, Divisor);
}

/* Modes */

const RxModeTraits&
RxGetModeTraits(
    _In_ DWRITE_RENDERING_MODE1 Mode,
    _In_ DWRITE_TEXT_ANTIALIAS_MODE Antialias)
{
    /*                 valid  X  Y  CT     gray   gdi    snap   gasp   classic bitmaps reach */
    static const RxModeTraits ClearType[8] =
    {
        { FALSE },
        { TRUE,  1, 1, FALSE, FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,  0 },
        { TRUE,  6, 1, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  TRUE,  2 },
        { TRUE,  6, 1, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  FALSE, TRUE,  2 },
        { TRUE,  6, 1, TRUE,  FALSE, FALSE, FALSE, TRUE,  FALSE, TRUE,  2 },
        { TRUE,  6, 5, TRUE,  FALSE, FALSE, FALSE, TRUE,  FALSE, FALSE, 2 },
        { FALSE },
        { TRUE,  6, 5, TRUE,  FALSE, FALSE, FALSE, TRUE,  FALSE, FALSE, 2 },
    };
    static const RxModeTraits Grayscale[8] =
    {
        { FALSE },
        { TRUE,  1, 1, FALSE, FALSE, FALSE, TRUE,  TRUE,  TRUE,  TRUE,  0 },
        { TRUE,  8, 1, FALSE, TRUE,  FALSE, TRUE,  TRUE,  TRUE,  TRUE,  0 },
        { TRUE,  8, 1, FALSE, TRUE,  FALSE, TRUE,  TRUE,  FALSE, TRUE,  0 },
        { TRUE,  8, 1, FALSE, TRUE,  FALSE, FALSE, TRUE,  FALSE, TRUE,  0 },
        { TRUE,  4, 4, FALSE, TRUE,  FALSE, FALSE, TRUE,  FALSE, FALSE, 0 },
        { FALSE },
        { TRUE,  2, 2, FALSE, TRUE,  FALSE, FALSE, TRUE,  FALSE, FALSE, 0 },
    };
    static const RxModeTraits Invalid = { FALSE };

    if (static_cast<UINT32>(Mode) >= 8)
        return Invalid;
    if (Antialias == DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE)
        return ClearType[Mode];
    if (Antialias == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        return Grayscale[Mode];
    return Invalid;
}

UINT32
RxGaspBehavior(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem)
{
    RxSpan Gasp = Face->Sfnt().Table(RxTag('g', 'a', 's', 'p'));
    USHORT Count = Gasp.U16(2);

    if (Gasp.IsEmpty() || Count == 0)
        return MAXUINT32;

    for (USHORT Index = 0; Index < Count; Index++)
    {
        if (Ppem <= Gasp.U16(4 + Index * 4))
            return Gasp.U16(6 + Index * 4);
    }
    /* Past the last range the last behavior holds */
    return Gasp.U16(4 + (Count - 1) * 4 + 2);
}

BOOLEAN
RxGaspGridFits(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem)
{
    RxSpan Gasp = Face->Sfnt().Table(RxTag('g', 'a', 's', 'p'));
    UINT32 Behavior;

    /* Version 0 tables predate the symmetric bit, and grid fitting is then always on */
    if (Gasp.IsEmpty() || Gasp.U16(0) == 0)
        return TRUE;
    Behavior = RxGaspBehavior(Face, Ppem);
    return Behavior == MAXUINT32 || (Behavior & 0x4) != 0;
}

/* Blend parameters */

static
FLOAT
RxLightOnDarkFactor(
    _In_ FLOAT Red,
    _In_ FLOAT Green,
    _In_ FLOAT Blue)
{
    FLOAT Luminance = 0.3f * Red + 0.59f * Green + 0.11f * Blue;

    if (Luminance >= 0.75f)
        return 0.0f;
    if (Luminance > 0.5f)
        return (0.75f - Luminance) * 4.0f;
    return 1.0f;
}

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
    _Out_ FLOAT* ClearTypeLevel)
{
    FLOAT Factor = RxLightOnDarkFactor(Red, Green, Blue);

    if (Traits.Grayscale)
    {
        *Gamma = Params.Gamma;
        *Contrast = Params.GrayscaleEnhancedContrast * Factor;
        *ClearTypeLevel = 0.0f;
    }
    else if (!Traits.GdiBlend)
    {
        *Gamma = Params.Gamma;
        *Contrast = Params.EnhancedContrast * Factor;
        *ClearTypeLevel = IsGrayscale ? 0.0f : Params.ClearTypeLevel;
    }
    else
    {
        *Gamma = Params.CompatibleGamma;
        *Contrast = 0.0f;
        *ClearTypeLevel = IsGrayscale ? 0.0f : (Params.CompatibleSmoothing == 2 ? 0.0f : 1.0f);
    }
}

/* Analysis */

HRESULT
RxGlyphRunAnalysis::Create(
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
    _Outptr_ RxGlyphRunAnalysis** Analysis)
{
    RxFontFace* Face;
    RxGlyphRunAnalysis* Result;
    DWRITE_MATRIX Matrix = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    BOOLEAN HasMatrix = FALSE;
    double Scale = PixelsPerDip;
    double OriginX = BaselineX;
    double OriginY = BaselineY;
    double EmPixels;
    RxRasterSize Size = {};
    BOOLEAN Hint;
    std::vector<double> OriginsX;
    std::vector<double> OriginsY;
    const RxFaceHeader* Header;
    HRESULT hr;

    *Analysis = nullptr;

    Face = RxFontFace::FromInterface(Run->fontFace);
    if (Face == nullptr)
        return E_INVALIDARG;
    Header = &Face->Tables()->Header();

    /* A uniform scale and translation folds into the pixel scale and the origin */
    if (Transform != nullptr)
    {
        const double Tolerance = 1.0 / 65536.0;

        if (std::fabs(Transform->m12) < Tolerance && std::fabs(Transform->m21) < Tolerance &&
            std::fabs(Transform->m22 - Transform->m11) < Tolerance && Transform->m11 != 0.0f)
        {
            OriginX += Transform->dx / Transform->m11;
            OriginY += Transform->dy / Transform->m11;
            Scale *= Transform->m11;
        }
        else
        {
            Matrix = *Transform;
            HasMatrix = TRUE;
        }
    }

    Result = new (std::nothrow) RxGlyphRunAnalysis();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->Traits = RxGetModeTraits(Mode, Antialias);
    if (!Result->Traits.Valid)
    {
        Result->Release();
        return E_INVALIDARG;
    }
    Result->IsGrayscale = Antialias == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;

    /* Rotated natural text is drawn symmetric */
    if (HasMatrix && !Modern && Mode == DWRITE_RENDERING_MODE1_NATURAL &&
        (Matrix.m12 != 0.0f || Matrix.m21 != 0.0f))
    {
        Mode = (Scale * 96.0 * 100.0 >= 17280.0) ? DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED :
                                                   DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
        Result->Traits = RxGetModeTraits(Mode, Antialias);
    }

    const RxModeTraits& Traits = Result->Traits;

    /* The rasterizer scale, an integer ppem when the mode snaps */
    EmPixels = Run->fontEmSize * Scale;
    if (Traits.SnapToPixels)
        EmPixels = RxRoundHalfUp(EmPixels);
    else
        EmPixels = std::floor(EmPixels * 65536.0 + 0.5) / 65536.0;

    if (HasMatrix)
    {
        /* Device y grows down and font y up, so the off diagonal terms change sign */
        Size.M11 = EmPixels * Matrix.m11;
        Size.M12 = -EmPixels * Matrix.m12;
        Size.M21 = -EmPixels * Matrix.m21;
        Size.M22 = EmPixels * Matrix.m22;
    }
    else
    {
        Size.M11 = Size.M22 = EmPixels;
    }

    /* Grid fitting, unless the caller decided */
    if (GridFit == DWRITE_GRID_FIT_MODE_DEFAULT)
    {
        BOOLEAN Aligned = Size.M12 == 0.0 && Size.M21 == 0.0;
        UINT32 Ppem = static_cast<UINT32>(std::max(0.0, RxRoundHalfUp(std::fabs(Size.M22))));

        if (!Aligned)
        {
            GridFit = DWRITE_GRID_FIT_MODE_DISABLED;
        }
        else if (Traits.SnapToPixels)
        {
            GridFit = DWRITE_GRID_FIT_MODE_ENABLED;
        }
        else if (!Traits.GridFitAllowed)
        {
            GridFit = DWRITE_GRID_FIT_MODE_DISABLED;
        }
        else
        {
            GridFit = RxGaspGridFits(Face->Tables(), std::min<UINT32>(std::max<UINT32>(Ppem, 1), 65535)) ?
                          DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED;
        }
    }
    Hint = GridFit == DWRITE_GRID_FIT_MODE_ENABLED;

    Size.OversampleX = Traits.OversampleX;
    Size.OversampleY = Traits.OversampleY;
    Size.Flags = 0;
    if (Run->fontFace->GetSimulations() & DWRITE_FONT_SIMULATIONS_BOLD)
        Size.Flags |= RxRasterBold;
    if (Run->fontFace->GetSimulations() & DWRITE_FONT_SIMULATIONS_OBLIQUE)
        Size.Flags |= RxRasterOblique;
    if (Hint)
    {
        Size.Flags |= RxRasterHint;
        if (Traits.SnapToPixels)
            Size.Flags |= Traits.GdiClassicHinting ? RxRasterGdiClassic : RxRasterGdiNatural;
        /* No embedded bitmap support, so only single sample output asks for bitmaps */
        if (Traits.OversampleX == 1 && Traits.OversampleY == 1)
            Size.Flags |= RxRasterBitmaps;
    }
    if (Traits.Grayscale)
        Size.Flags |= RxRasterGray;

    /* Glyph origins in DIPs */
    {
        double PenX = OriginX;
        BOOLEAN RightToLeft = (Run->bidiLevel & 1) != 0;

        OriginsX.resize(Run->glyphCount);
        OriginsY.resize(Run->glyphCount);
        for (UINT32 Index = 0; Index < Run->glyphCount; Index++)
        {
            double Advance;

            if (Run->glyphAdvances != nullptr)
            {
                Advance = Run->glyphAdvances[Index];
            }
            else
            {
                DWRITE_GLYPH_METRICS Metrics;

                Run->fontFace->GetDesignGlyphMetrics(&Run->glyphIndices[Index], 1, &Metrics, Run->isSideways);
                Advance = (Run->isSideways ? Metrics.advanceHeight : Metrics.advanceWidth) * (double)Run->fontEmSize /
                          Header->UnitsPerEm;
                if (Measuring != DWRITE_MEASURING_MODE_NATURAL)
                    Advance = RxRoundHalfUp(Advance * Scale) / Scale;
            }

            if (RightToLeft)
                PenX -= Advance;

            OriginsX[Index] = PenX;
            OriginsY[Index] = OriginY;
            if (Run->glyphOffsets != nullptr)
            {
                OriginsX[Index] += RightToLeft ? -Run->glyphOffsets[Index].advanceOffset :
                                                 Run->glyphOffsets[Index].advanceOffset;
                OriginsY[Index] -= Run->glyphOffsets[Index].ascenderOffset;
            }

            if (!RightToLeft)
                PenX += Advance;
        }

        if (HasMatrix)
        {
            for (UINT32 Index = 0; Index < Run->glyphCount; Index++)
            {
                double X = OriginsX[Index];
                double Y = OriginsY[Index];

                OriginsX[Index] = X * Matrix.m11 + Y * Matrix.m21 + Matrix.dx;
                OriginsY[Index] = X * Matrix.m12 + Y * Matrix.m22 + Matrix.dy;
            }
        }
    }

    /* Images, placed on the sample grid */
    {
        RxGlyphRasterizer* Rasterizer = Face->Rasterizer();
        BOOLEAN Any = FALSE;

        if (Rasterizer == nullptr)
        {
            Result->Release();
            return E_OUTOFMEMORY;
        }

        for (UINT32 Index = 0; Index < Run->glyphCount; Index++)
        {
            Placed Entry;
            double X = OriginsX[Index];

            AcquireSRWLockExclusive(&Rasterizer->Lock);
            hr = Rasterizer->Rasterize(Run->glyphIndices[Index], Size, &Entry.Image);
            ReleaseSRWLockExclusive(&Rasterizer->Lock);
            if (FAILED(hr))
            {
                Result->Release();
                return hr;
            }
            if (Entry.Image.IsEmpty())
                continue;

            if (Traits.SnapToPixels && std::fabs(X * Scale) < 8388600.0)
                X = RxRoundHalfUp(X * Scale) / Scale;

            Entry.X = static_cast<INT32>(RxRoundHalfUp(X * Traits.OversampleX * Scale)) + Entry.Image.Mask.Left;
            Entry.Y = static_cast<INT32>(RxRoundHalfUp(OriginsY[Index] * Traits.OversampleY * Scale)) +
                      Entry.Image.Mask.Top;

            RECT Box = { Entry.X, Entry.Y, Entry.X + static_cast<LONG>(Entry.Image.Mask.Width()),
                         Entry.Y + static_cast<LONG>(Entry.Image.Mask.Height()) };
            if (!Any)
            {
                Result->Bounds = Box;
                Any = TRUE;
            }
            else
            {
                UnionRect(&Result->Bounds, &Result->Bounds, &Box);
            }
            Result->Glyphs.push_back(std::move(Entry));
        }

        if (!Any)
        {
            LONG X = static_cast<LONG>(Traits.OversampleX * RxRoundHalfUp(OriginX * Scale));
            LONG Y = static_cast<LONG>(Traits.OversampleY * RxRoundHalfUp(OriginY * Scale));

            Result->Bounds = { X, Y, X, Y };
        }
        else if (Traits.OversampleY > 1)
        {
            Result->Bounds.top = RxFloorDiv(Result->Bounds.top, Traits.OversampleY) * Traits.OversampleY;
            Result->Bounds.bottom = RxCeilDiv(Result->Bounds.bottom, Traits.OversampleY) * Traits.OversampleY;
        }
    }

    *Analysis = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGlyphRunAnalysis::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteGlyphRunAnalysis))
    {
        *ppvObject = static_cast<IDWriteGlyphRunAnalysis*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxGlyphRunAnalysis)

VOID
RxGlyphRunAnalysis::TextureBounds(
    _Out_ RECT* Result) const
{
    INT32 X = Traits.OversampleX;
    INT32 Y = Traits.OversampleY;
    RECT Box = Bounds;

    if (IsRectEmpty(&Box))
    {
        SetRectEmpty(Result);
        return;
    }

    if (Traits.ClearTypeTexture)
    {
        Box.left -= Traits.FilterReach;
        Box.right += Traits.FilterReach;
    }

    Result->left = RxFloorDiv(Box.left, X);
    Result->right = RxCeilDiv(Box.right, X);
    Result->top = Y > 1 ? RxFloorDiv(Box.top, Y) : Box.top;
    Result->bottom = Y > 1 ? RxCeilDiv(Box.bottom, Y) : Box.bottom;
}

HRESULT
STDMETHODCALLTYPE
RxGlyphRunAnalysis::GetAlphaTextureBounds(
    DWRITE_TEXTURE_TYPE textureType,
    _Out_ RECT* textureBounds)
{
    if (textureBounds == nullptr)
        return E_INVALIDARG;
    SetRectEmpty(textureBounds);

    if (textureType != DWRITE_TEXTURE_ALIASED_1x1 && textureType != DWRITE_TEXTURE_CLEARTYPE_3x1)
        return E_INVALIDARG;

    /* Each analysis only makes the texture type its mode renders */
    if ((textureType == DWRITE_TEXTURE_CLEARTYPE_3x1) != (Traits.ClearTypeTexture != FALSE))
        return S_OK;

    TextureBounds(textureBounds);
    return S_OK;
}

VOID
RxGlyphRunAnalysis::MergeSamples(
    _In_ INT32 Left,
    _In_ INT32 Top,
    _In_ INT32 Width,
    _In_ INT32 Height,
    _Inout_ std::vector<BYTE>& Samples) const
{
    Samples.assign(static_cast<SIZE_T>(Width) * Height, 0);

    for (const auto& Glyph : Glyphs)
    {
        const RxSampleMask& Mask = Glyph.Image.Mask;

        for (UINT32 Row = 0; Row < Mask.Height(); Row++)
        {
            INT32 Y = Glyph.Y + static_cast<INT32>(Row) - Top;

            if (Y < 0 || Y >= Height)
                continue;
            for (UINT32 Column = 0; Column < Mask.Width(); Column++)
            {
                INT32 X = Glyph.X + static_cast<INT32>(Column) - Left;

                if (X < 0 || X >= Width)
                    continue;
                if (Mask.Bits[Row * Mask.Stride + Column / 8] & (0x80 >> (Column % 8)))
                    Samples[static_cast<SIZE_T>(Y) * Width + X] = 1;
            }
        }
    }
}

HRESULT
STDMETHODCALLTYPE
RxGlyphRunAnalysis::CreateAlphaTexture(
    DWRITE_TEXTURE_TYPE textureType,
    _In_ RECT const* textureBounds,
    _Out_writes_bytes_(bufferSize) BYTE* alphaValues,
    UINT32 bufferSize)
{
    INT32 X = Traits.OversampleX;
    INT32 Y = Traits.OversampleY;
    INT64 Width;
    INT64 Height;
    UINT64 Needed;
    std::vector<BYTE> Samples;

    if (textureBounds == nullptr || IsRectEmpty(textureBounds))
        return E_INVALIDARG;
    if (textureType != DWRITE_TEXTURE_ALIASED_1x1 && textureType != DWRITE_TEXTURE_CLEARTYPE_3x1)
        return E_INVALIDARG;

    Width = textureBounds->right - textureBounds->left;
    Height = textureBounds->bottom - textureBounds->top;
    Needed = static_cast<UINT64>(Width) * Height * (textureType == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1);
    if (Needed > bufferSize)
        return E_NOT_SUFFICIENT_BUFFER;

    memset(alphaValues, 0, bufferSize);

    if (textureType == DWRITE_TEXTURE_CLEARTYPE_3x1)
    {
        /* Subpixel columns the box filters read, plus the filter reach on both sides */
        INT32 Left = static_cast<INT32>(textureBounds->left) * 6 - 2;
        INT32 Top = static_cast<INT32>(textureBounds->top) * Y;
        INT32 SampleWidth = static_cast<INT32>(Width) * 6 + 4;
        INT32 SampleHeight = static_cast<INT32>(Height) * Y;
        static const INT32 RowWeights[5] = { 4, 9, 10, 9, 4 };

        if (X != 6)
            return DWRITE_E_UNSUPPORTEDOPERATION;

        MergeSamples(Left, Top, SampleWidth, SampleHeight, Samples);

        for (INT64 Row = 0; Row < Height; Row++)
        {
            for (INT64 Column = 0; Column < Width; Column++)
            {
                /* Red reads subpixels 0..5 of the window, green 2..7 and blue 4..9 */
                for (INT32 Channel = 0; Channel < 3; Channel++)
                {
                    INT32 First = static_cast<INT32>(Column) * 6 + Channel * 2;
                    INT32 Total = 0;

                    for (INT32 Sub = 0; Sub < Y; Sub++)
                    {
                        const BYTE* Line = &Samples[(static_cast<SIZE_T>(Row) * Y + Sub) * SampleWidth];
                        INT32 Count = 0;

                        for (INT32 Bit = 0; Bit < 6; Bit++)
                            Count += Line[First + Bit];
                        Total += (Y == 5) ? Count * RowWeights[Sub] : Count;
                    }

                    alphaValues[(Row * Width + Column) * 3 + Channel] = static_cast<BYTE>(
                        RxRoundHalfUp(Total * 255.0 / (Y == 5 ? 216.0 : 6.0)));
                }
            }
        }
        return S_OK;
    }

    /* Grayscale and aliased coverage */
    {
        INT32 Left = static_cast<INT32>(textureBounds->left) * X;
        INT32 Top = static_cast<INT32>(textureBounds->top) * Y;
        INT32 SampleWidth = static_cast<INT32>(Width) * X;
        INT32 SampleHeight = static_cast<INT32>(Height) * Y;
        INT32 SampleCount = X * Y;

        if (Traits.ClearTypeTexture)
            return S_OK;

        MergeSamples(Left, Top, SampleWidth, SampleHeight, Samples);

        for (INT64 Row = 0; Row < Height; Row++)
        {
            for (INT64 Column = 0; Column < Width; Column++)
            {
                INT32 Count = 0;
                BYTE Value;

                for (INT32 Sub = 0; Sub < Y; Sub++)
                {
                    const BYTE* Line = &Samples[(static_cast<SIZE_T>(Row) * Y + Sub) * SampleWidth];

                    for (INT32 Bit = 0; Bit < X; Bit++)
                        Count += Line[Column * X + Bit];
                }

                if (SampleCount == 1)
                    Value = Count != 0 ? 255 : 0;
                else if (SampleCount == 16)
                    Value = static_cast<BYTE>((255 * Count + 8) >> 4);
                else
                    Value = static_cast<BYTE>(RxRoundHalfUp(Count * 255.0 / SampleCount));
                alphaValues[Row * Width + Column] = Value;
            }
        }
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGlyphRunAnalysis::GetAlphaBlendParams(
    _In_ IDWriteRenderingParams* renderingParams,
    _Out_ FLOAT* blendGamma,
    _Out_ FLOAT* blendEnhancedContrast,
    _Out_ FLOAT* blendClearTypeLevel)
{
    RxParamValues Values;

    if (renderingParams == nullptr)
        return E_INVALIDARG;

    RxRenderingParams::ReadValues(renderingParams, &Values);
    RxAlphaBlendValues(Traits, IsGrayscale, Values, 0.0f, 0.0f, 0.0f, blendGamma, blendEnhancedContrast,
                       blendClearTypeLevel);
    return S_OK;
}

/* Drawing into a bitmap */

namespace
{

/* Linear and encoded values of one gamma, 0 and 255 kept exact */
struct RxGammaPair
{
    BYTE Linear[256];
    BYTE Encode[256];
};

VOID
RxBuildGamma(
    _In_ FLOAT Gamma,
    _Out_ RxGammaPair* Table)
{
    Table->Linear[0] = Table->Encode[0] = 0;
    Table->Linear[255] = Table->Encode[255] = 255;
    for (UINT32 Index = 1; Index < 255; Index++)
    {
        double X = Index * (1.0 / 255.0);

        Table->Linear[Index] = static_cast<BYTE>(std::floor(255.0 * std::pow(X, static_cast<double>(Gamma)) + 0.5));
        Table->Encode[Index] = static_cast<BYTE>(std::floor(255.0 * std::pow(X, 1.0 / Gamma) + 0.5));
    }
}

VOID
RxBuildContrast(
    _In_ FLOAT Contrast,
    _Out_writes_(256) USHORT* Table)
{
    Table[0] = 0;
    Table[255] = 256;
    for (UINT32 Index = 1; Index < 255; Index++)
    {
        FLOAT X = Index * 0.0039216f;

        Table[Index] = static_cast<USHORT>(std::floor(256.0f * (Contrast + 1.0f) * X / (Contrast * X + 1.0f) + 0.5f));
    }
}

/* Curve corrections per gamma step, for grayscale coverage */
const FLOAT RxGammaRatios[12][4] =
{
    { 0.0166f, -0.0807f, 0.2227f, -0.0751f },
    { 0.0350f, -0.1760f, 0.4325f, -0.1370f },
    { 0.0543f, -0.2821f, 0.6302f, -0.1876f },
    { 0.0739f, -0.3963f, 0.8167f, -0.2287f },
    { 0.0933f, -0.5161f, 0.9926f, -0.2616f },
    { 0.1121f, -0.6395f, 1.1588f, -0.2877f },
    { 0.1300f, -0.7649f, 1.3159f, -0.3080f },
    { 0.1469f, -0.8911f, 1.4644f, -0.3234f },
    { 0.1627f, -1.0170f, 1.6051f, -0.3347f },
    { 0.1773f, -1.1420f, 1.7385f, -0.3426f },
    { 0.1908f, -1.2652f, 1.8650f, -0.3476f },
    { 0.2031f, -1.3864f, 1.9851f, -0.3501f },
};

VOID
RxGrayscaleAlphas(
    _In_ FLOAT Contrast,
    _In_ FLOAT Gamma,
    _In_ COLORREF Color,
    _In_ UINT32 Levels,
    _Out_writes_(Levels) BYTE* Alphas)
{
    const FLOAT* Ratios = nullptr;
    FLOAT Step = 1.0f / Levels;
    FLOAT Lum = GetRValue(Color) * (1.0f / 1020.0f) + GetGValue(Color) * (1.0f / 510.0f) +
                GetBValue(Color) * (1.0f / 1020.0f);
    FLOAT Steps = (Gamma - 1.05f) * 10.0f;
    UINT32 Row = Steps > 0.0f ? static_cast<UINT32>(Steps) : 0;
    FLOAT X = 0.0f;

    if (Row != 0)
        Ratios = RxGammaRatios[std::min<UINT32>(Row, 11)];

    for (UINT32 Index = 0; Index + 1 < Levels; Index++)
    {
        FLOAT Value;

        X += Step;
        Value = X;
        if (Contrast != 0.0f)
            Value = (Contrast + 1.0f) * Value / (Contrast * Value + 1.0f);
        if (Ratios != nullptr)
            Value += ((Ratios[0] * Lum + Ratios[1]) * Value + (Ratios[2] * Lum + Ratios[3])) * Value * (1.0f - Value);
        Alphas[Index] = static_cast<BYTE>(std::min(255.0, RxRoundHalfUp(Value * 256.0)));
    }
}

} // namespace

VOID
RxGlyphRunAnalysis::DrawInto(
    _In_ const RxParamValues& Params,
    _In_ COLORREF Color,
    _Inout_updates_(Width * Height) UINT32* Bits,
    _In_ UINT32 Width,
    _In_ UINT32 Height,
    _Out_ RECT* Drawn) const
{
    INT32 X = Traits.OversampleX;
    INT32 Y = Traits.OversampleY;
    const BYTE Text[3] = { GetBValue(Color), GetGValue(Color), GetRValue(Color) };
    FLOAT Factor = RxLightOnDarkFactor(Text[2] / 255.0f, Text[1] / 255.0f, Text[0] / 255.0f);
    RECT Box;
    INT32 BoxWidth;
    INT32 BoxHeight;
    std::vector<BYTE> Samples;

    TextureBounds(&Box);
    *Drawn = Box;
    if (IsRectEmpty(&Box))
        return;
    BoxWidth = Box.right - Box.left;
    BoxHeight = Box.bottom - Box.top;

    if (Traits.ClearTypeTexture && X == 6)
    {
        static const INT32 RowWeights[5] = { 4, 9, 10, 9, 4 };
        RxGammaPair Gamma;
        USHORT Contrast[256];
        DWRITE_PIXEL_GEOMETRY Geometry = IsGrayscale ? DWRITE_PIXEL_GEOMETRY_FLAT : Params.PixelGeometry;
        FLOAT Level = Traits.GdiBlend ? (Params.CompatibleSmoothing == 2 ? 0.0f : 1.0f) : Params.ClearTypeLevel;
        INT32 Reach = 0;
        INT32 Starts[3];
        INT32 SampleWidth = BoxWidth * 6 + 4;

        /* Classic filters reach two subpixels each way, intermediate ones one, grayscale none */
        if (Geometry != DWRITE_PIXEL_GEOMETRY_FLAT && Level >= 0.25f)
            Reach = Level > 0.75f ? 2 : 1;
        for (INT32 Channel = 0; Channel < 3; Channel++)
            Starts[Channel] = Channel * Reach - Reach;
        if (Geometry == DWRITE_PIXEL_GEOMETRY_BGR)
            std::swap(Starts[0], Starts[2]);

        RxBuildGamma(Traits.GdiBlend ? Params.CompatibleGamma : Params.Gamma, &Gamma);
        RxBuildContrast(Traits.GdiBlend ? 0.0f : std::max(0.0f, Params.EnhancedContrast * Factor), Contrast);

        MergeSamples(static_cast<INT32>(Box.left) * 6 - 2, Box.top * Y, SampleWidth, BoxHeight * Y, Samples);

        for (INT32 Row = 0; Row < BoxHeight; Row++)
        {
            INT32 PixelY = Box.top + Row;

            if (PixelY < 0 || PixelY >= static_cast<INT32>(Height))
                continue;
            for (INT32 Column = 0; Column < BoxWidth; Column++)
            {
                INT32 PixelX = Box.left + Column;
                BYTE* Pixel;

                if (PixelX < 0 || PixelX >= static_cast<INT32>(Width))
                    continue;
                Pixel = reinterpret_cast<BYTE*>(&Bits[static_cast<SIZE_T>(PixelY) * Width + PixelX]);

                /* Byte 0 is blue, which reads the window of the third channel */
                for (INT32 Byte = 0; Byte < 3; Byte++)
                {
                    INT32 First = Column * 6 + 2 + Starts[2 - Byte];
                    INT32 Total = 0;
                    USHORT Weight;

                    for (INT32 Sub = 0; Sub < Y; Sub++)
                    {
                        const BYTE* Line = &Samples[(static_cast<SIZE_T>(Row) * Y + Sub) * SampleWidth];
                        INT32 Count = 0;

                        for (INT32 Bit = 0; Bit < 6; Bit++)
                            Count += Line[First + Bit];
                        Total += (Y == 5) ? Count * RowWeights[Sub] : Count;
                    }
                    if (Total == 0)
                        continue;

                    Weight = Contrast[static_cast<BYTE>(RxRoundHalfUp(Total * 255.0 / (Y == 5 ? 216.0 : 6.0)))];
                    Pixel[Byte] = Gamma.Encode[(Gamma.Linear[Text[Byte]] * Weight +
                                                Gamma.Linear[Pixel[Byte]] * (256 - Weight) + 128) >> 8];
                }
            }
        }
        return;
    }

    /* Aliased and grayscale coverage */
    {
        INT32 Levels = X * Y;
        INT32 SampleWidth = BoxWidth * X;
        BYTE Alphas[16] = {};

        if (Levels > 1)
            RxGrayscaleAlphas(Params.GrayscaleEnhancedContrast * Factor, Params.Gamma, Color,
                              static_cast<UINT32>(std::min(Levels, 16)), Alphas);

        MergeSamples(Box.left * X, Box.top * Y, SampleWidth, BoxHeight * Y, Samples);
        for (INT32 Row = 0; Row < BoxHeight; Row++)
        {
            INT32 PixelY = Box.top + Row;

            if (PixelY < 0 || PixelY >= static_cast<INT32>(Height))
                continue;
            for (INT32 Column = 0; Column < BoxWidth; Column++)
            {
                INT32 PixelX = Box.left + Column;
                INT32 Count = 0;
                BYTE* Pixel;

                if (PixelX < 0 || PixelX >= static_cast<INT32>(Width))
                    continue;
                for (INT32 Sub = 0; Sub < Y; Sub++)
                {
                    const BYTE* Line = &Samples[(static_cast<SIZE_T>(Row) * Y + Sub) * SampleWidth];

                    for (INT32 Bit = 0; Bit < X; Bit++)
                        Count += Line[Column * X + Bit];
                }
                if (Count == 0)
                    continue;

                Pixel = reinterpret_cast<BYTE*>(&Bits[static_cast<SIZE_T>(PixelY) * Width + PixelX]);
                for (INT32 Byte = 0; Byte < 3; Byte++)
                {
                    UINT32 Alpha;

                    if (Count >= Levels)
                    {
                        Pixel[Byte] = Text[Byte];
                        continue;
                    }
                    Alpha = Alphas[Count - 1];
                    Pixel[Byte] = static_cast<BYTE>(((Text[Byte] * Alpha + 127) >> 8) +
                                                    ((Pixel[Byte] * (256 - Alpha) + 127) >> 8));
                }
            }
        }
    }
}
