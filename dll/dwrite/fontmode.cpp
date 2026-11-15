/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Recommended rendering and grid fit modes of a font face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "glyphrun.hpp"
#include "fontface.hpp"

#include <cmath>

/* gasp behavior bits */
constexpr UINT32 RxGaspDoGray = 0x0002;
constexpr UINT32 RxGaspSymmetricSmoothing = 0x0008;

/* Pixel aligned axes, sideways glyphs, one aligned axis */
constexpr UINT32 RxAxisAligned = 0x1;
constexpr UINT32 RxAxisSideways = 0x2;
constexpr UINT32 RxAxisOneAligned = 0x4;

static
double
RxRoundHalfUp(
    _In_ double Value)
{
    return std::floor(Value + 0.5);
}

static
BOOLEAN
RxNearZero(
    _In_ double Value)
{
    return std::fabs(Value) < 1.0 / 65536.0;
}

static
BOOLEAN
RxCompatibleGrayscaleSize(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem)
{
    UINT32 Behavior = RxGaspBehavior(Face, std::min<UINT32>(Ppem, 65535));

    if (Behavior != MAXUINT32)
        return (Behavior & RxGaspDoGray) != 0;
    if (Face->Header().HasOs2 && Face->Header().WeightClass >= 600)
        return TRUE;
    return !(Ppem >= 9 && Ppem <= 17);
}

/* Shared by every GetRecommendedRenderingMode form */
static
HRESULT
RxRecommend(
    _In_ const RxFaceTables* Face,
    _In_ FLOAT EmSize,
    _In_ FLOAT DpiX,
    _In_ FLOAT DpiY,
    _In_opt_ const DWRITE_MATRIX* Transform,
    _In_ BOOL IsSideways,
    _In_ DWRITE_OUTLINE_THRESHOLD Threshold,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_opt_ IDWriteRenderingParams* Params,
    _In_ BOOLEAN AllowDownsampled,
    _Out_ DWRITE_RENDERING_MODE1* Mode,
    _Out_opt_ DWRITE_GRID_FIT_MODE* GridFit)
{
    RxParamValues Values;
    UINT32 Axis;
    double Scale;
    double Ppem;
    UINT32 Pixels;
    DWRITE_RENDERING_MODE1 Result;
    BOOLEAN HighDpi = std::min(DpiX, DpiY) * 100.0f >= 17280.0f;

    *Mode = DWRITE_RENDERING_MODE1_DEFAULT;
    if (GridFit != nullptr)
        *GridFit = DWRITE_GRID_FIT_MODE_DEFAULT;

    if (static_cast<UINT32>(Threshold) > DWRITE_OUTLINE_THRESHOLD_ALIASED ||
        static_cast<UINT32>(Measuring) > DWRITE_MEASURING_MODE_GDI_NATURAL)
        return E_INVALIDARG;

    RxRenderingParams::ReadValues(Params, &Values);
    if (Params == nullptr)
        Values.IsModern = TRUE;

    /* How the glyph axes sit on the pixel grid, and the scale along them */
    if (Transform == nullptr && DpiX == DpiY)
    {
        Scale = DpiX / 96.0;
        Axis = RxAxisAligned | (IsSideways ? RxAxisSideways : 0);
    }
    else
    {
        DWRITE_MATRIX Identity = { 1, 0, 0, 1, 0, 0 };
        const DWRITE_MATRIX& M = Transform != nullptr ? *Transform : Identity;
        double A = M.m11 * DpiX / 96.0;
        double B = M.m12 * DpiY / 96.0;
        double C = M.m21 * DpiX / 96.0;
        double D = M.m22 * DpiY / 96.0;

        if (RxNearZero(B) && RxNearZero(C))
        {
            Axis = RxAxisAligned | (IsSideways ? RxAxisSideways : 0);
            Scale = std::max(std::fabs(A), std::fabs(D));
        }
        else if (RxNearZero(A) && RxNearZero(D))
        {
            Axis = RxAxisAligned | (IsSideways ? 0 : RxAxisSideways);
            Scale = std::max(std::fabs(B), std::fabs(C));
        }
        else
        {
            double Sum = A * A + B * B + C * C + D * D;
            double Cross = 2.0 * (A * C + B * D);
            double Spread = C * C + D * D - A * A - B * B;

            Axis = (RxNearZero(A) || RxNearZero(B) || RxNearZero(C) || RxNearZero(D)) ? RxAxisOneAligned : 0;
            Scale = std::sqrt((Sum + std::sqrt(Cross * Cross + Spread * Spread)) / 2.0);
        }
    }

    Ppem = EmSize * Scale;
    Pixels = static_cast<UINT32>(std::max(0.0, RxRoundHalfUp(Ppem)));

    if (Values.RenderingMode == DWRITE_RENDERING_MODE1_DEFAULT)
    {
        if ((Threshold == DWRITE_OUTLINE_THRESHOLD_ANTIALIASED && Ppem >= 100.0) ||
            (Threshold == DWRITE_OUTLINE_THRESHOLD_ALIASED && Ppem >= 350.0))
        {
            Result = DWRITE_RENDERING_MODE1_OUTLINE;
        }
        else if (Measuring == DWRITE_MEASURING_MODE_GDI_CLASSIC)
        {
            UINT32 Smoothing = Values.CompatibleSmoothing != 0 ? Values.CompatibleSmoothing : RxCompatibleSmoothing();

            if (Smoothing == 1)
                Result = DWRITE_RENDERING_MODE1_ALIASED;
            else if (Smoothing == 2)
                Result = RxCompatibleGrayscaleSize(Face, Pixels) ? DWRITE_RENDERING_MODE1_GDI_CLASSIC :
                                                                    DWRITE_RENDERING_MODE1_ALIASED;
            else
                Result = DWRITE_RENDERING_MODE1_GDI_CLASSIC;
        }
        else if (Measuring == DWRITE_MEASURING_MODE_GDI_NATURAL)
        {
            Result = DWRITE_RENDERING_MODE1_GDI_NATURAL;
        }
        else if (!(Axis & RxAxisAligned))
        {
            Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
        }
        else if (Ppem <= 0.0)
        {
            Result = DWRITE_RENDERING_MODE1_NATURAL;
        }
        else
        {
            UINT32 Behavior = RxGaspBehavior(Face, std::min<UINT32>(Pixels, 65535));

            /* A version 0 gasp table says nothing about symmetric smoothing */
            if (Face->Sfnt().Table(RxTag('g', 'a', 's', 'p')).U16(0) == 0)
                Behavior = MAXUINT32;

            if (Behavior != MAXUINT32)
            {
                Result = (Behavior & RxGaspSymmetricSmoothing) ? DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC :
                                                                 DWRITE_RENDERING_MODE1_NATURAL;
            }
            else
            {
                const RxSfnt& Sfnt = Face->Sfnt();
                BOOLEAN Hinted = Sfnt.HasTable(RxTag('f', 'p', 'g', 'm')) || Sfnt.HasTable(RxTag('p', 'r', 'e', 'p'));

                if (!Sfnt.IsCff() && Hinted)
                    Result = Pixels <= 20 ? DWRITE_RENDERING_MODE1_NATURAL : DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
                else
                    Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
            }

            if ((Axis & RxAxisSideways) && Result == DWRITE_RENDERING_MODE1_NATURAL)
                Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
        }

        if (Result == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC && HighDpi && AllowDownsampled)
            Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED;
    }
    else
    {
        Result = Values.RenderingMode;
        if (!Values.IsModern)
        {
            if (Result == DWRITE_RENDERING_MODE1_NATURAL && !(Axis & RxAxisAligned))
                Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
            if (Result == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC && HighDpi && AllowDownsampled)
                Result = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED;
        }
    }

    *Mode = Result;

    if (GridFit != nullptr)
    {
        if (Values.GridFitMode != DWRITE_GRID_FIT_MODE_DEFAULT)
        {
            *GridFit = Values.GridFitMode;
        }
        else
        {
            const RxModeTraits& Traits = RxGetModeTraits(Result, DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE);

            if (!(Axis & (RxAxisAligned | RxAxisOneAligned)))
            {
                *GridFit = DWRITE_GRID_FIT_MODE_DISABLED;
            }
            else if (Traits.Valid && Traits.SnapToPixels)
            {
                *GridFit = DWRITE_GRID_FIT_MODE_ENABLED;
            }
            else if (!Traits.Valid || !Traits.GridFitAllowed)
            {
                *GridFit = DWRITE_GRID_FIT_MODE_DISABLED;
            }
            else
            {
                *GridFit = RxGaspGridFits(Face, std::min<UINT32>(std::max<UINT32>(Pixels, 1), 65535)) ?
                               DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED;
            }
        }
    }
    return S_OK;
}

/* The legacy enum has no downsampled symmetric mode */
static
DWRITE_RENDERING_MODE
RxLegacyMode(
    _In_ DWRITE_RENDERING_MODE1 Mode)
{
    if (Mode == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED)
        return DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    return static_cast<DWRITE_RENDERING_MODE>(Mode);
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetRecommendedRenderingMode(
    FLOAT emSize,
    FLOAT pixelsPerDip,
    DWRITE_MEASURING_MODE measuringMode,
    IDWriteRenderingParams* renderingParams,
    _Out_ DWRITE_RENDERING_MODE* renderingMode)
{
    DWRITE_RENDERING_MODE1 Mode;
    HRESULT hr;

    hr = RxRecommend(Data, emSize * pixelsPerDip, 96.0f, 96.0f, nullptr, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED,
                     measuringMode, renderingParams, FALSE, &Mode, nullptr);
    *renderingMode = RxLegacyMode(Mode);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetRecommendedRenderingMode(
    FLOAT fontEmSize,
    FLOAT dpiX,
    FLOAT dpiY,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL isSideways,
    DWRITE_OUTLINE_THRESHOLD outlineThreshold,
    DWRITE_MEASURING_MODE measuringMode,
    _Out_ DWRITE_RENDERING_MODE* renderingMode)
{
    DWRITE_RENDERING_MODE1 Mode;
    HRESULT hr;

    hr = RxRecommend(Data, fontEmSize, dpiX, dpiY, transform, isSideways, outlineThreshold, measuringMode, nullptr, FALSE,
                     &Mode, nullptr);
    *renderingMode = RxLegacyMode(Mode);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetRecommendedRenderingMode(
    FLOAT fontEmSize,
    FLOAT dpiX,
    FLOAT dpiY,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL isSideways,
    DWRITE_OUTLINE_THRESHOLD outlineThreshold,
    DWRITE_MEASURING_MODE measuringMode,
    _In_opt_ IDWriteRenderingParams* renderingParams,
    _Out_ DWRITE_RENDERING_MODE* renderingMode,
    _Out_ DWRITE_GRID_FIT_MODE* gridFitMode)
{
    DWRITE_RENDERING_MODE1 Mode;
    HRESULT hr;

    hr = RxRecommend(Data, fontEmSize, dpiX, dpiY, transform, isSideways, outlineThreshold, measuringMode,
                     renderingParams, FALSE, &Mode, gridFitMode);
    *renderingMode = RxLegacyMode(Mode);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetRecommendedRenderingMode(
    FLOAT fontEmSize,
    FLOAT dpiX,
    FLOAT dpiY,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL isSideways,
    DWRITE_OUTLINE_THRESHOLD outlineThreshold,
    DWRITE_MEASURING_MODE measuringMode,
    _In_opt_ IDWriteRenderingParams* renderingParams,
    _Out_ DWRITE_RENDERING_MODE1* renderingMode,
    _Out_ DWRITE_GRID_FIT_MODE* gridFitMode)
{
    return RxRecommend(Data, fontEmSize, dpiX, dpiY, transform, isSideways, outlineThreshold, measuringMode,
                       renderingParams, TRUE, renderingMode, gridFitMode);
}
