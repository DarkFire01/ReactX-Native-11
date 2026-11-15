/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Glyph runs and the text renderer DrawTextLayout uses
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include <cmath>

/* Alpha correction ratios for gamma encoded targets, by gamma index 1.0 to 2.2 in tenths */
static const FLOAT RxGammaRatios[13][4] =
{
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.00415f, -0.020175f, 0.055675f, -0.018775f },
    { 0.00875f, -0.044f, 0.108125f, -0.03425f },
    { 0.013575f, -0.070525f, 0.15755f, -0.0469f },
    { 0.018475f, -0.099075f, 0.204175f, -0.057175f },
    { 0.023325f, -0.129025f, 0.24815f, -0.0654f },
    { 0.028025f, -0.159875f, 0.2897f, -0.071925f },
    { 0.0325f, -0.191225f, 0.328975f, -0.077f },
    { 0.036725f, -0.222775f, 0.3661f, -0.08085f },
    { 0.040675f, -0.25425f, 0.401275f, -0.083675f },
    { 0.044325f, -0.2855f, 0.434625f, -0.08565f },
    { 0.0477f, -0.3163f, 0.46625f, -0.0869f },
    { 0.050775f, -0.3466f, 0.496275f, -0.087525f },
};

/* Rounds half away from zero below zero only toward zero on exact halves, as the glyph path does */
static
INT32
RxRoundGlyph(
    _In_ double Value)
{
    double Whole = std::trunc(Value);
    double Rest = Value - Whole;

    if (Value >= 0.0)
        return static_cast<INT32>(Rest >= 0.5 ? Whole + 1.0 : Whole);
    return static_cast<INT32>(Rest < -0.5 ? Whole - 1.0 : Whole);
}

static
FLOAT
RxClamp(
    _In_ FLOAT Value)
{
    return Value < 0.0f ? 0.0f : (Value > 1.0f ? 1.0f : Value);
}

/* Coverage to premultiplied BGRA for one solid color, gamma and contrast applied */
static
VOID
RxBuildTextTable(
    _In_ const D2D1_COLOR_F& Color,
    _In_ FLOAT Opacity,
    _In_ FLOAT Gamma,
    _In_ FLOAT Contrast,
    _Out_writes_(256) UINT32* Table)
{
    FLOAT Red = RxClamp(Color.r);
    FLOAT Green = RxClamp(Color.g);
    FLOAT Blue = RxClamp(Color.b);
    FLOAT Alpha = RxClamp(Color.a) * RxClamp(Opacity);
    INT32 Index = static_cast<INT32>(std::lround(Gamma * 10.0f));
    const FLOAT* Ratios;
    INT32 Luminance = RxRoundGlyph(255.0 * (0.25 * Red + 0.5 * Green + 0.25 * Blue));

    Index = std::min(std::max(Index, 10), 22) - 10;
    Ratios = RxGammaRatios[Index];

    /* Light text on dark gets less contrast, none at all once it is bright enough */
    {
        FLOAT Light = 0.3f * Red + 0.59f * Green + 0.11f * Blue;

        if (Light >= 0.75f)
            Contrast = 0.0f;
        else if (Light > 0.5f)
            Contrast *= (0.75f - Light) * 4.0f;
    }

    for (UINT32 Coverage = 0; Coverage < 256; Coverage++)
    {
        INT32 Enhanced;
        double X;
        double T;
        INT32 Base;
        INT32 Delta;
        INT32 Corrected;

        if (Coverage == 0)
            Enhanced = 0;
        else if (Coverage == 255)
            Enhanced = 255;
        else
        {
            double Value = Coverage / 255.0;

            Enhanced = static_cast<INT32>(255.0 * ((Contrast + 1.0) * Value) / (Contrast * Value + 1.0) + 0.5);
        }

        X = Enhanced / 255.0;
        T = X * (1.0 - X);
        Base = static_cast<UINT8>(RxRoundGlyph(255.0 * (X + T * (Ratios[1] * X + Ratios[3]) * 1024.0 / 255.0)));
        Delta = static_cast<INT8>(RxRoundGlyph(255.0 * T * (Ratios[0] * X + Ratios[2]) * 1028.0 / 255.0));
        Corrected = Base + ((Delta * Luminance) >> 8);
        Corrected = std::min(std::max(Corrected, 0), 255);

        {
            UINT32 B = static_cast<UINT32>(std::min(255, RxRoundGlyph(Corrected * Blue * Alpha)));
            UINT32 G = static_cast<UINT32>(std::min(255, RxRoundGlyph(Corrected * Green * Alpha)));
            UINT32 R = static_cast<UINT32>(std::min(255, RxRoundGlyph(Corrected * Red * Alpha)));
            UINT32 A = static_cast<UINT32>(std::min(255, RxRoundGlyph(Corrected * Alpha)));

            Table[Coverage] = (A << 24) | (R << 16) | (G << 8) | B;
        }
    }
}

VOID
RxDrawGlyphRun(
    _In_ RxD2DContext* Context,
    _In_ RxRenderer& Renderer,
    _In_ IDWriteFactory2* Write,
    _In_ const D2D1_MATRIX_3X2_F& World,
    _In_ FLOAT DpiX,
    _In_ FLOAT DpiY,
    _In_ D2D1_POINT_2F Baseline,
    _In_ const DWRITE_GLYPH_RUN* Run,
    _In_ const RxPaint& Paint,
    _In_ BOOLEAN IsSolid,
    _In_ const D2D1_COLOR_F& Color,
    _In_ FLOAT Opacity,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_ D2D1_TEXT_ANTIALIAS_MODE TextAntialias,
    _In_ IDWriteRenderingParams* Params)
{
    RxComPtr<IDWriteFontFace3> Face3;
    RxComPtr<IDWriteRenderingParams1> Params1;
    RxComPtr<IDWriteGlyphRunAnalysis> Analysis;
    DWRITE_RENDERING_MODE1 Mode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
    DWRITE_GRID_FIT_MODE GridFit = DWRITE_GRID_FIT_MODE_DEFAULT;
    BOOLEAN Aliased = TextAntialias == D2D1_TEXT_ANTIALIAS_MODE_ALIASED;
    DWRITE_MATRIX Device;
    DWRITE_MATRIX Placement;
    FLOAT Contrast = 1.0f;
    RECT Bounds;
    std::vector<BYTE> Coverage;
    std::vector<UINT32> Pixels;
    UINT32 Table[256];
    HRESULT hr;

    if (Write == nullptr || Run == nullptr || Run->glyphCount == 0 || Run->fontFace == nullptr)
        return;

    /* Glyphs are rasterized in device space; DWrite gets the world transform and the dpi */
    Placement = { World._11, World._12, World._21, World._22, 0.0f, 0.0f };
    Device = { World._11 * DpiX / 96.0f, World._12 * DpiY / 96.0f, World._21 * DpiX / 96.0f, World._22 * DpiY / 96.0f,
               World._31 * DpiX / 96.0f, World._32 * DpiY / 96.0f };

    if (SUCCEEDED(Run->fontFace->QueryInterface(__uuidof(IDWriteFontFace3), Face3.PutVoid())))
    {
        hr = Face3->GetRecommendedRenderingMode(Run->fontEmSize, DpiX, DpiY, &Placement, Run->isSideways,
                                                Aliased ? DWRITE_OUTLINE_THRESHOLD_ALIASED :
                                                          DWRITE_OUTLINE_THRESHOLD_ANTIALIASED,
                                                Measuring, Params, &Mode, &GridFit);
        if (FAILED(hr))
            Mode = DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;
    }

    if (Aliased && Mode != DWRITE_RENDERING_MODE1_OUTLINE)
        Mode = DWRITE_RENDERING_MODE1_ALIASED;
    /* Outlines are not filled as geometry here; large text goes through coverage like the rest */
    if (Mode == DWRITE_RENDERING_MODE1_OUTLINE || Mode == DWRITE_RENDERING_MODE1_DEFAULT)
        Mode = Aliased ? DWRITE_RENDERING_MODE1_ALIASED : DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC;

    {
        RxComPtr<IDWriteFactory3> Write3;

        if (SUCCEEDED(Write->QueryInterface(__uuidof(IDWriteFactory3), Write3.PutVoid())))
            hr = Write3->CreateGlyphRunAnalysis(Run, &Device, Mode, Measuring, GridFit,
                                                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, Baseline.x, Baseline.y,
                                                Analysis.Put());
        else
            hr = Write->CreateGlyphRunAnalysis(Run, &Device,
                                               Mode == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED ?
                                                   DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC :
                                                   static_cast<DWRITE_RENDERING_MODE>(Mode),
                                               Measuring, GridFit, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, Baseline.x,
                                               Baseline.y, Analysis.Put());
    }
    if (FAILED(hr))
        return;

    if (FAILED(Analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_ALIASED_1x1, &Bounds)) || IsRectEmpty(&Bounds))
        return;

    Coverage.resize(static_cast<SIZE_T>(Bounds.right - Bounds.left) * (Bounds.bottom - Bounds.top));
    if (FAILED(Analysis->CreateAlphaTexture(DWRITE_TEXTURE_ALIASED_1x1, &Bounds, Coverage.data(),
                                            static_cast<UINT32>(Coverage.size()))))
        return;

    if (SUCCEEDED(Params->QueryInterface(__uuidof(IDWriteRenderingParams1), Params1.PutVoid())))
        Contrast = Params1->GetGrayscaleEnhancedContrast();

    /* Brushes other than solid colors take the coverage as plain alpha in white */
    if (IsSolid)
        RxBuildTextTable(Color, Opacity, Params->GetGamma(), Contrast, Table);
    else
        RxBuildTextTable({ 1.0f, 1.0f, 1.0f, 1.0f }, 1.0f, 1.0f, Contrast, Table);

    Pixels.resize(Coverage.size());
    for (SIZE_T Index = 0; Index < Coverage.size(); Index++)
        Pixels[Index] = Table[Coverage[Index]];

    {
        UINT32 Width = Bounds.right - Bounds.left;
        UINT32 Height = Bounds.bottom - Bounds.top;
        UINT32 TextureWidth;
        UINT32 TextureHeight;
        ID3D11ShaderResourceView* View = Renderer.Upload(Pixels.data(), Width, Height, &TextureWidth, &TextureHeight);
        RxPaint Glyphs = {};
        FLOAT L = static_cast<FLOAT>(Bounds.left);
        FLOAT T = static_cast<FLOAT>(Bounds.top);
        FLOAT R = static_cast<FLOAT>(Bounds.right);
        FLOAT B = static_cast<FLOAT>(Bounds.bottom);
        RxVertex Quad[6] = { { L, T, 1.0f }, { R, T, 1.0f }, { R, B, 1.0f }, { L, T, 1.0f }, { R, B, 1.0f }, { L, B, 1.0f } };

        if (View == nullptr)
            return;

        Glyphs.Texture = View;
        Glyphs.Sampler = RxSamplerPointClamp;
        Glyphs.Opacity = 1.0f;
        Glyphs.Uv[0] = 1.0f / TextureWidth;
        Glyphs.Uv[1] = 0.0f;
        Glyphs.Uv[2] = -L / TextureWidth;
        Glyphs.Uv[3] = 0.0f;
        Glyphs.Uv[4] = 1.0f / TextureHeight;
        Glyphs.Uv[5] = -T / TextureHeight;

        if (!IsSolid)
            RX_UNIMPLEMENTED();
        Renderer.Draw(Quad, _countof(Quad), Glyphs, FALSE);
    }
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Paint);
}

/* RxTextRenderer */

RxTextRenderer::RxTextRenderer(
    _In_ ID2D1DeviceContext* Target,
    _In_ ID2D1Brush* Brush,
    _In_ D2D1_DRAW_TEXT_OPTIONS DrawOptions,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ BOOLEAN PixelUnits) :
    Context(Target), DefaultBrush(Brush), Options(DrawOptions), DpiX(X), DpiY(Y), Pixels(PixelUnits)
{
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer))
    {
        *ppvObject = static_cast<IDWriteTextRenderer*>(this);
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Lives on the stack of one DrawTextLayout call */
ULONG
STDMETHODCALLTYPE
RxTextRenderer::AddRef(VOID)
{
    return 2;
}

ULONG
STDMETHODCALLTYPE
RxTextRenderer::Release(VOID)
{
    return 1;
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::IsPixelSnappingDisabled(
    _In_opt_ void* clientDrawingContext,
    _Out_ BOOL* isDisabled)
{
    *isDisabled = (Options & D2D1_DRAW_TEXT_OPTIONS_NO_SNAP) != 0;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::GetCurrentTransform(
    _In_opt_ void* clientDrawingContext,
    _Out_ DWRITE_MATRIX* transform)
{
    D2D1_MATRIX_3X2_F World;
    FLOAT Ratio = Pixels ? 1.0f : DpiX / DpiY;

    Context->GetTransform(&World);
    *transform = { World._11 * Ratio, World._12, World._21 * Ratio, World._22, World._31 * Ratio, World._32 };
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::GetPixelsPerDip(
    _In_opt_ void* clientDrawingContext,
    _Out_ FLOAT* pixelsPerDip)
{
    *pixelsPerDip = Pixels ? 1.0f : DpiY / 96.0f;
    return S_OK;
}

HRESULT
RxTextRenderer::BrushFor(
    _In_opt_ IUnknown* Effect,
    _Outptr_ ID2D1Brush** Brush)
{
    RxComPtr<ID2D1Resource> Resource;

    *Brush = DefaultBrush;
    if (Effect == nullptr)
        return S_OK;
    if (SUCCEEDED(Effect->QueryInterface(__uuidof(ID2D1Brush), reinterpret_cast<void**>(Brush))))
    {
        (*Brush)->Release();
        return S_OK;
    }
    if (SUCCEEDED(Effect->QueryInterface(__uuidof(ID2D1Resource), Resource.PutVoid())))
        return D2DERR_TEXT_EFFECT_IS_WRONG_TYPE;
    *Brush = DefaultBrush;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::DrawGlyphRun(
    _In_opt_ void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode,
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    _In_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    _In_opt_ IUnknown* clientDrawingEffect)
{
    ID2D1Brush* Brush;
    HRESULT hr;

    if (glyphRun == nullptr || glyphRunDescription == nullptr)
        return E_INVALIDARG;

    hr = BrushFor(clientDrawingEffect, &Brush);
    if (FAILED(hr))
        return hr;
    Context->DrawGlyphRun({ baselineOriginX, baselineOriginY }, glyphRun, glyphRunDescription, Brush, measuringMode);
    return S_OK;
}

HRESULT
RxTextRenderer::FillLine(
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Width,
    _In_ FLOAT Offset,
    _In_ FLOAT Thickness,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_opt_ IUnknown* Effect)
{
    D2D1_ANTIALIAS_MODE Saved = Context->GetAntialiasMode();
    ID2D1Brush* Brush;
    D2D1_RECT_F Rect;
    HRESULT hr;

    if (static_cast<UINT32>(Measuring) > DWRITE_MEASURING_MODE_GDI_NATURAL)
        return E_INVALIDARG;

    hr = BrushFor(Effect, &Brush);
    if (FAILED(hr))
        return hr;

    /* Non natural text draws its lines aliased, at least one device pixel thick */
    if (Measuring != DWRITE_MEASURING_MODE_NATURAL || Context->GetTextAntialiasMode() == D2D1_TEXT_ANTIALIAS_MODE_ALIASED)
    {
        D2D1_MATRIX_3X2_F World;
        FLOAT ScaleX;
        FLOAT ScaleY;
        double Determinant;
        double Minimum;

        Context->GetTransform(&World);
        ScaleX = Pixels ? 1.0f : DpiX / 96.0f;
        ScaleY = Pixels ? 1.0f : DpiY / 96.0f;
        Determinant = std::fabs(World._11 * ScaleX * World._22 * ScaleY - World._12 * ScaleY * World._21 * ScaleX);
        Minimum = Determinant > 0.0 ? std::sqrt(std::pow(World._11 * ScaleX, 2) + std::pow(World._12 * ScaleY, 2)) /
                                          Determinant : 0.0;
        Thickness = std::max(Thickness, static_cast<FLOAT>(Minimum));
        Context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    }
    else
    {
        Context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    Rect = { X, Y + Offset, X + Width, Y + Offset + Thickness };
    Context->FillRectangle(&Rect, Brush);
    Context->SetAntialiasMode(Saved);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::DrawUnderline(
    _In_opt_ void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _In_ DWRITE_UNDERLINE const* underline,
    _In_opt_ IUnknown* clientDrawingEffect)
{
    return FillLine(baselineOriginX, baselineOriginY, underline->width, underline->offset, underline->thickness,
                    underline->measuringMode, clientDrawingEffect);
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::DrawStrikethrough(
    _In_opt_ void* clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _In_ DWRITE_STRIKETHROUGH const* strikethrough,
    _In_opt_ IUnknown* clientDrawingEffect)
{
    return FillLine(baselineOriginX, baselineOriginY, strikethrough->width, strikethrough->offset,
                    strikethrough->thickness, strikethrough->measuringMode, clientDrawingEffect);
}

HRESULT
STDMETHODCALLTYPE
RxTextRenderer::DrawInlineObject(
    _In_opt_ void* clientDrawingContext,
    FLOAT originX,
    FLOAT originY,
    _In_ IDWriteInlineObject* inlineObject,
    BOOL isSideways,
    BOOL isRightToLeft,
    _In_opt_ IUnknown* clientDrawingEffect)
{
    ID2D1Brush* Saved = DefaultBrush;
    ID2D1Brush* Brush;
    HRESULT hr;

    if (inlineObject == nullptr)
        return E_INVALIDARG;

    hr = BrushFor(clientDrawingEffect, &Brush);
    if (FAILED(hr))
        return hr;

    DefaultBrush = Brush;
    hr = inlineObject->Draw(clientDrawingContext, this, originX, originY, isSideways, isRightToLeft, clientDrawingEffect);
    DefaultBrush = Saved;
    return hr;
}
