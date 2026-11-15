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
            UINT32 Behavior = RxGaspBehavior(Face->Tables(), std::min<UINT32>(std::max<UINT32>(Ppem, 1), 65535));

            GridFit = (Behavior == MAXUINT32 || (Behavior & 0x1)) ? DWRITE_GRID_FIT_MODE_ENABLED :
                                                                   DWRITE_GRID_FIT_MODE_DISABLED;
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
