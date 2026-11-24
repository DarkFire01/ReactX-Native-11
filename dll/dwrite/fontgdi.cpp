/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GDI compatible metrics, glyph outlines and face names of a font face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "fontface.hpp"
#include "names.hpp"

#include <d2d1.h>

#include <cmath>

static
double
RxRound(
    _In_ double Value)
{
    return std::floor(Value + 0.5);
}

/* Ascent and descent in pixels from the first VDMX ratio GDI would pick, for one ppem */
static
BOOLEAN
RxVdmxLookup(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem,
    _Out_ INT32* YMax,
    _Out_ INT32* YMin)
{
    RxSpan Vdmx = Face->Sfnt().Table(RxTag('V', 'D', 'M', 'X'));
    USHORT Ratios = Vdmx.U16(4);

    *YMax = 0;
    *YMin = 0;
    for (USHORT Ratio = 0; Ratio < Ratios; Ratio++)
    {
        UINT32 Record = 6 + Ratio * 4;
        USHORT Offset;
        USHORT Count;

        if (Vdmx.U8(Record) != 1 || Vdmx.U8(Record + 1) >= 2)
            continue;

        Offset = Vdmx.U16(6 + Ratios * 4 + Ratio * 2);
        Count = Vdmx.U16(Offset);
        for (USHORT Entry = 0; Entry < Count; Entry++)
        {
            UINT32 At = Offset + 4 + Entry * 6;

            if (Vdmx.U16(At) == Ppem)
            {
                *YMax = Vdmx.S16(At + 2);
                *YMin = Vdmx.S16(At + 4);
                return TRUE;
            }
        }
        return FALSE;
    }
    return FALSE;
}

/* Pixel width of a glyph from the hdmx device record for ppem */
static
BOOLEAN
RxHdmxWidth(
    _In_ const RxFaceTables* Face,
    _In_ UINT32 Ppem,
    _In_ USHORT Glyph,
    _Out_ UINT32* Width)
{
    RxSpan Hdmx = Face->Sfnt().Table(RxTag('h', 'd', 'm', 'x'));
    SHORT Records = static_cast<SHORT>(Hdmx.U16(2));
    UINT32 RecordSize = Hdmx.U32(4);

    *Width = 0;
    for (SHORT Record = 0; Record < Records; Record++)
    {
        UINT32 At = 8 + static_cast<UINT32>(Record) * RecordSize;

        if (!Hdmx.Has(At, 2 + static_cast<UINT32>(Glyph) + 1))
            return FALSE;
        if (Hdmx.U8(At) == Ppem)
        {
            *Width = Hdmx.U8(At + 2 + Glyph);
            return TRUE;
        }
    }
    return FALSE;
}

static
HRESULT
RxGdiFontMetrics(
    _In_ RxFontFace* Face,
    _In_ FLOAT EmSize,
    _In_ FLOAT PixelsPerDip,
    _In_opt_ const DWRITE_MATRIX* Transform,
    _Out_ DWRITE_FONT_METRICS1* Metrics)
{
    double Scale = 1.0;
    double PpemF;
    double UnitsPerPixel;
    INT32 Top = 0;
    INT32 Bottom = 0;
    BOOLEAN HaveDevice = FALSE;
    INT32 Ppem;
    UINT16* Words;

    memset(Metrics, 0, sizeof(*Metrics));
    if (Face->GetType() == DWRITE_FONT_FACE_TYPE_RAW_CFF)
        return DWRITE_E_UNSUPPORTEDOPERATION;
    if (!(EmSize > 0.0f) || !(PixelsPerDip > 0.0f))
        return E_INVALIDARG;

    Face->GetMetrics(Metrics);
    if (Transform != nullptr)
        Scale = std::sqrt(static_cast<double>(Transform->m21) * Transform->m21 +
                          static_cast<double>(Transform->m22) * Transform->m22);

    PpemF = static_cast<double>(EmSize) * PixelsPerDip * Scale;
    if (std::fabs(PpemF) < 1.5259e-5)
        return S_OK;
    UnitsPerPixel = Metrics->designUnitsPerEm / PpemF;
    if (std::fabs(UnitsPerPixel) < 1.5259e-5)
        return S_OK;
    Ppem = static_cast<INT32>(RxRound(PpemF));

    if (PpemF > 0.0 && PpemF < 255.0)
        HaveDevice = RxVdmxLookup(Face->Tables(), static_cast<UINT32>(Ppem), &Top, &Bottom);

    /* Words 1 to 21 snap to whole pixels; ascent, descent, cap height, x height and the thickness pair are unsigned */
    Words = reinterpret_cast<UINT16*>(Metrics);
    for (UINT32 Word = 1; Word <= 21; Word++)
    {
        BOOLEAN IsUnsigned = Word == 1 || Word == 2 || Word == 4 || Word == 5 || Word == 7 || Word == 9;
        double Value = IsUnsigned ? static_cast<double>(Words[Word]) : static_cast<double>(static_cast<INT16>(Words[Word]));
        double Pixels = Value / UnitsPerPixel;

        if (HaveDevice && Word == 1)
            Value = RxRound(Top * UnitsPerPixel);
        else if (HaveDevice && Word == 2)
            Value = RxRound(-Bottom * UnitsPerPixel);
        else
            Value = RxRound((std::fabs(Pixels) < 8388600.0 ? RxRound(Pixels) : Pixels) * UnitsPerPixel);
        Words[Word] = static_cast<UINT16>(static_cast<INT32>(Value));
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGdiCompatibleMetrics(
    FLOAT emSize,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    _Out_ DWRITE_FONT_METRICS* fontFaceMetrics)
{
    DWRITE_FONT_METRICS1 Full;
    HRESULT hr;

    if (fontFaceMetrics == nullptr)
        return E_INVALIDARG;
    hr = RxGdiFontMetrics(this, emSize, pixelsPerDip, transform, &Full);
    memcpy(fontFaceMetrics, &Full, sizeof(*fontFaceMetrics));
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGdiCompatibleMetrics(
    FLOAT emSize,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    _Out_ DWRITE_FONT_METRICS1* fontMetrics)
{
    if (fontMetrics == nullptr)
        return E_INVALIDARG;
    return RxGdiFontMetrics(this, emSize, pixelsPerDip, transform, fontMetrics);
}

/* Integer ppem of a GDI compatible request; zero when the transform calls for design metrics */
static
HRESULT
RxGdiPpem(
    _In_ FLOAT EmSize,
    _In_ FLOAT PixelsPerDip,
    _In_opt_ const DWRITE_MATRIX* Transform,
    _In_ BOOL IsSideways,
    _Out_ UINT32* Ppem)
{
    double Base = static_cast<double>(EmSize) * PixelsPerDip;
    double M11 = IsSideways ? 0.0 : Base;
    double M12 = IsSideways ? Base : 0.0;
    double M21 = IsSideways ? -Base : 0.0;
    double M22 = IsSideways ? 0.0 : Base;
    double Rounded;

    *Ppem = 0;
    if (Transform != nullptr)
    {
        double A = M11 * Transform->m11 + M12 * Transform->m21;
        double B = M11 * Transform->m12 + M12 * Transform->m22;
        double C = M21 * Transform->m11 + M22 * Transform->m21;
        double D = M21 * Transform->m12 + M22 * Transform->m22;

        M11 = A;
        M12 = B;
        M21 = C;
        M22 = D;
    }

    /* Arbitrary rotations are not hinted */
    if (M11 != 0.0 && M12 != 0.0 && M21 != 0.0 && M22 != 0.0)
        return S_OK;

    Rounded = RxRound(std::max(std::fabs(M11), std::fabs(M12)));
    if (Rounded < 0.0 || Rounded >= 32768.0)
        return E_INVALIDARG;
    *Ppem = static_cast<UINT32>(Rounded);
    return S_OK;
}

/* A pixel width back in design units, through the 16.16 em fraction */
static
INT32
RxPixelsToDesign(
    _In_ INT64 Pixels,
    _In_ UINT32 Ppem,
    _In_ UINT32 UnitsPerEm)
{
    INT64 Fraction = (Pixels * 65536 + Ppem / 2) / Ppem;

    return static_cast<INT32>((UnitsPerEm * Fraction + 0x8000) >> 16);
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGdiCompatibleGlyphMetrics(
    FLOAT emSize,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL useGdiNatural,
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    UINT32 glyphCount,
    _Out_writes_(glyphCount) DWRITE_GLYPH_METRICS* glyphMetrics,
    BOOL isSideways)
{
    UINT32 Ppem;
    UINT32 UnitsPerEm = Metrics.designUnitsPerEm;
    HRESULT hr;

    if (glyphMetrics != nullptr && glyphCount != 0)
        memset(glyphMetrics, 0, sizeof(*glyphMetrics) * glyphCount);
    if (GetType() == DWRITE_FONT_FACE_TYPE_RAW_CFF)
        return DWRITE_E_UNSUPPORTEDOPERATION;
    if (glyphIndices == nullptr || glyphMetrics == nullptr || !(emSize > 0.0f) || !(pixelsPerDip > 0.0f))
        return E_INVALIDARG;

    hr = RxGdiPpem(emSize, pixelsPerDip, transform, isSideways, &Ppem);
    if (FAILED(hr))
        return hr;

    hr = GetDesignGlyphMetrics(glyphIndices, glyphCount, glyphMetrics, isSideways);
    if (FAILED(hr) || Ppem == 0)
        return hr;

    /* Widths snap to the pixel grid, from hdmx where the font has a device record */
    for (UINT32 Index = 0; Index < glyphCount; Index++)
    {
        DWRITE_GLYPH_METRICS& Out = glyphMetrics[Index];
        UINT32 Width;
        INT64 Pixels;
        INT32 Advance;

        if (!useGdiNatural && RxHdmxWidth(Data, Ppem, glyphIndices[Index], &Width))
            Pixels = Width;
        else
            Pixels = static_cast<INT64>(RxRound(static_cast<double>(Out.advanceWidth) * Ppem / UnitsPerEm));

        Advance = RxPixelsToDesign(Pixels, Ppem, UnitsPerEm);
        Out.rightSideBearing += Advance - static_cast<INT32>(Out.advanceWidth);
        Out.advanceWidth = static_cast<UINT32>(std::max(0, Advance));
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGdiCompatibleGlyphAdvances(
    FLOAT emSize,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL useGdiNatural,
    BOOL isSideways,
    UINT32 glyphCount,
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    _Out_writes_(glyphCount) INT32* glyphAdvances)
{
    std::vector<DWRITE_GLYPH_METRICS> Glyphs;
    HRESULT hr;

    if (glyphAdvances != nullptr && glyphCount != 0)
        memset(glyphAdvances, 0, sizeof(*glyphAdvances) * glyphCount);
    if (glyphCount == 0)
        return S_OK;
    if (glyphAdvances == nullptr)
        return E_INVALIDARG;

    Glyphs.resize(glyphCount);
    hr = GetGdiCompatibleGlyphMetrics(emSize, pixelsPerDip, transform, useGdiNatural, glyphIndices, glyphCount,
                                      Glyphs.data(), isSideways);
    if (FAILED(hr))
        return hr;
    for (UINT32 Index = 0; Index < glyphCount; Index++)
        glyphAdvances[Index] = static_cast<INT32>(isSideways ? Glyphs[Index].advanceHeight : Glyphs[Index].advanceWidth);
    return S_OK;
}

/* Outlines */

namespace
{

class RxOutlineWriter
{
public:
    RxOutlineWriter(
        _In_ IDWriteGeometrySink* Target) : Sink(Target), EmSize(1.0f), UnitsPerEm(1.0f), OriginX(0.0f), OriginY(0.0f), Oblique(FALSE) {}

    /* Where the next glyph goes; its points stay in design units until they are written */
    VOID
    Place(
        _In_ FLOAT NewEmSize,
        _In_ FLOAT NewUnitsPerEm,
        _In_ FLOAT X,
        _In_ FLOAT Y,
        _In_ BOOLEAN IsOblique)
    {
        EmSize = NewEmSize;
        UnitsPerEm = NewUnitsPerEm;
        OriginX = X;
        OriginY = Y;
        Oblique = IsOblique;
    }

    VOID
    Contour(
        _In_reads_(Count) const D2D1_POINT_2F* Points,
        _In_reads_(Count) const BYTE* OnCurve,
        _In_ UINT32 Count);

private:
    D2D1_POINT_2F
    Map(
        _In_ const D2D1_POINT_2F& Design) const
    {
        /* Oblique simulation shears by 22272 / 65536 */
        FLOAT X = Oblique ? Design.x + Design.y * 0.33984375f : Design.x;

        return { OriginX + Scaled(X), OriginY - Scaled(Design.y) };
    }

    FLOAT
    Scaled(
        _In_ FLOAT Design) const
    {
        return Design * (EmSize / UnitsPerEm);
    }

    VOID
    Line(
        _In_ const D2D1_POINT_2F& To)
    {
        D2D1_POINT_2F Point = Map(To);

        Sink->AddLines(&Point, 1);
    }

    /* A quadratic segment as the cubic Direct2D draws */
    VOID
    Quadratic(
        _In_ const D2D1_POINT_2F& From,
        _In_ const D2D1_POINT_2F& Control,
        _In_ const D2D1_POINT_2F& To)
    {
        D2D1_BEZIER_SEGMENT Segment;

        Segment.point1 = Map({ (From.x + 2.0f * Control.x) / 3.0f, (From.y + 2.0f * Control.y) / 3.0f });
        Segment.point2 = Map({ (To.x + 2.0f * Control.x) / 3.0f, (To.y + 2.0f * Control.y) / 3.0f });
        Segment.point3 = Map(To);
        Sink->AddBeziers(&Segment, 1);
    }

    IDWriteGeometrySink* Sink;
    FLOAT EmSize;
    FLOAT UnitsPerEm;
    FLOAT OriginX;
    FLOAT OriginY;
    BOOLEAN Oblique;
};

D2D1_POINT_2F
RxMid(
    _In_ const D2D1_POINT_2F& A,
    _In_ const D2D1_POINT_2F& B)
{
    return { (A.x + B.x) * 0.5f, (A.y + B.y) * 0.5f };
}

VOID
RxOutlineWriter::Contour(
    _In_reads_(Count) const D2D1_POINT_2F* Points,
    _In_reads_(Count) const BYTE* OnCurve,
    _In_ UINT32 Count)
{
    D2D1_POINT_2F Start;
    D2D1_POINT_2F Current;
    D2D1_POINT_2F Control = {};
    BOOLEAN HasControl = FALSE;
    UINT32 First = 0;
    UINT32 Last = Count;

    /* A lone point draws nothing */
    if (Count < 2)
        return;

    /* Start on an on curve point, or between two off curve ones */
    if (OnCurve[0])
    {
        Start = Points[0];
        First = 1;
    }
    else if (OnCurve[Count - 1])
    {
        Start = Points[Count - 1];
        Last = Count - 1;
    }
    else
    {
        Start = RxMid(Points[Count - 1], Points[0]);
    }

    Sink->BeginFigure(Map(Start), D2D1_FIGURE_BEGIN_FILLED);
    Current = Start;
    for (UINT32 Index = First; Index < Last; Index++)
    {
        if (OnCurve[Index])
        {
            if (HasControl)
                Quadratic(Current, Control, Points[Index]);
            else
                Line(Points[Index]);
            Current = Points[Index];
            HasControl = FALSE;
            continue;
        }

        if (HasControl)
        {
            D2D1_POINT_2F Middle = RxMid(Control, Points[Index]);

            Quadratic(Current, Control, Middle);
            Current = Middle;
        }
        Control = Points[Index];
        HasControl = TRUE;
    }
    if (HasControl)
        Quadratic(Current, Control, Start);
    Sink->EndFigure(D2D1_FIGURE_END_CLOSED);
}

} // namespace

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGlyphRunOutline(
    FLOAT emSize,
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    _In_reads_opt_(glyphCount) FLOAT const* glyphAdvances,
    _In_reads_opt_(glyphCount) DWRITE_GLYPH_OFFSET const* glyphOffsets,
    UINT32 glyphCount,
    BOOL isSideways,
    BOOL isRightToLeft,
    _In_ IDWriteGeometrySink* geometrySink)
{
    RxOutlineWriter Writer(geometrySink);
    RxGlyphOutline Outline;
    std::vector<D2D1_POINT_2F> Points;
    std::vector<BYTE> OnCurve;
    FLOAT Scale;
    FLOAT PenX = 0.0f;
    HRESULT hr;

    if (geometrySink == nullptr || (glyphCount != 0 && glyphIndices == nullptr) || !(emSize > 0.0f))
        return E_INVALIDARG;

    Scale = emSize / Metrics.designUnitsPerEm;
    geometrySink->SetFillMode(D2D1_FILL_MODE_WINDING);

    for (UINT32 Index = 0; Index < glyphCount; Index++)
    {
        USHORT Glyph = glyphIndices[Index];
        DWRITE_GLYPH_METRICS Design;
        FLOAT Advance;
        FLOAT OriginX;
        FLOAT OriginY = 0.0f;
        UINT32 Begin = 0;

        hr = GetDesignGlyphMetrics(&Glyph, 1, &Design, FALSE);
        if (FAILED(hr))
            return hr;
        Advance = glyphAdvances != nullptr ? glyphAdvances[Index] : Design.advanceWidth * Scale;

        /* Right to left glyphs end at the pen by their own width; the given advance only moves the pen */
        OriginX = isRightToLeft ? PenX - Design.advanceWidth * Scale : PenX;
        if (glyphOffsets != nullptr)
        {
            OriginX += isRightToLeft ? -glyphOffsets[Index].advanceOffset : glyphOffsets[Index].advanceOffset;
            OriginY -= glyphOffsets[Index].ascenderOffset;
        }
        PenX += isRightToLeft ? -Advance : Advance;

        Outline.Clear();
        hr = Data->LoadOutline(Glyph, Outline);
        if (FAILED(hr))
            return hr;

        Points.resize(Outline.Points.size());
        OnCurve.resize(Outline.Points.size());
        Writer.Place(emSize, static_cast<FLOAT>(Metrics.designUnitsPerEm), OriginX, OriginY, (Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) != 0);
        for (SIZE_T Point = 0; Point < Outline.Points.size(); Point++)
        {
            Points[Point] = { static_cast<FLOAT>(Outline.Points[Point].X), static_cast<FLOAT>(Outline.Points[Point].Y) };
            OnCurve[Point] = Outline.Points[Point].OnCurve;
        }

        for (USHORT End : Outline.ContourEnds)
        {
            if (End < Begin || End >= Points.size())
                break;
            Writer.Contour(&Points[Begin], &OnCurve[Begin], End - Begin + 1);
            Begin = End + 1;
        }
    }
    UNREFERENCED_PARAMETER(isSideways);
    return S_OK;
}

/* Names */

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetFamilyNames(
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    RxFaceDescription Description;

    if (names == nullptr)
        return E_INVALIDARG;
    *names = nullptr;
    RxDescribeFace(Data, &Description);
    return RxLocalizedStrings::Create(Description.WwsFamily, names);
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetFaceNames(
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    RxFaceDescription Description;
    RxNameList Simulated;

    if (names == nullptr)
        return E_INVALIDARG;
    *names = nullptr;
    RxDescribeFace(Data, &Description);

    if (Simulations != DWRITE_FONT_SIMULATIONS_NONE && !Description.WwsFace.empty())
    {
        const RxNameEntry* Source = &Description.WwsFace[0];

        for (const auto& Entry : Description.WwsFace)
        {
            if (Entry.Locale == L"en-us")
                Source = &Entry;
        }
        Simulated.push_back({ Source->Locale, RxSimulatedFaceName(Source->Text, Simulations) });
        return RxLocalizedStrings::Create(Simulated, names);
    }
    return RxLocalizedStrings::Create(Description.WwsFace, names);
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetInformationalStrings(
    DWRITE_INFORMATIONAL_STRING_ID informationalStringID,
    _COM_Outptr_result_maybenull_ IDWriteLocalizedStrings** informationalStrings,
    _Out_ BOOL* exists)
{
    /* Name ID for each informational string, by enum value */
    static const SHORT NameIds[] = { -1, 0, 5, 7, 8, 9, 12, 10, 11, 13, 14, 1, 2, 16, 17, 19, 4, 6, 20, 21 };
    RxFaceDescription Description;
    RxNameList Names;
    HRESULT hr;

    if (informationalStrings == nullptr || exists == nullptr)
        return E_INVALIDARG;
    *informationalStrings = nullptr;
    *exists = FALSE;

    if (static_cast<UINT32>(informationalStringID) >= _countof(NameIds) || NameIds[informationalStringID] < 0)
        return S_OK;

    RxDescribeFace(Data, &Description);
    if (informationalStringID == DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES)
        Names = Description.Win32Family;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES)
        Names = Description.Win32Face;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_FULL_NAME)
        Names = Description.FullName;
    else if (informationalStringID == DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME)
        Names = Description.PostscriptName;
    else
        RxReadNames(Data, static_cast<USHORT>(NameIds[informationalStringID]), Names);

    if (Names.empty())
        return S_OK;

    hr = RxLocalizedStrings::Create(Names, informationalStrings);
    if (SUCCEEDED(hr))
        *exists = TRUE;
    return hr;
}
