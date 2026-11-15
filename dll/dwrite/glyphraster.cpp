/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Turns glyphs of one face into sample masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "glyphraster.hpp"

#include <cmath>

/* Shear of simulated oblique faces, 22272 / 65536 */
constexpr double RxObliqueShear = 0.33984375;

RxGlyphRasterizer::RxGlyphRasterizer(
    _In_ RxFaceTables* Tables) :
    Face(Tables), Hinter(nullptr), HinterFailed(FALSE), HaveSize(FALSE), HintedSize{}, HintUsable(FALSE)
{
    InitializeSRWLock(&Lock);
    Face->AddRef();
}

RxGlyphRasterizer::~RxGlyphRasterizer()
{
    delete Hinter;
    Face->Release();
}

static
BOOLEAN
RxIsAxisAligned(
    _In_ const RxRasterSize& Size)
{
    return Size.M12 == 0.0 && Size.M21 == 0.0 && Size.M11 > 0.0 && Size.M22 > 0.0;
}

/* ClearType state the interpreter reports for a rendering */
static
UINT32
RxClearTypeFlags(
    _In_ const RxRasterSize& Size)
{
    UINT32 Flags;

    if (Size.OversampleX == 1)
        return 0;

    if (!(Size.Flags & (RxRasterGdiClassic | RxRasterGdiNatural)))
        Flags = Size.OversampleY > 1 ? 0x71 : 0x51;
    else if (Size.Flags & RxRasterBitmaps)
        Flags = 0;
    else if (Size.Flags & RxRasterGdiClassic)
        Flags = 0x03;
    else
        Flags = 0x01;

    if ((Size.Flags & RxRasterGray) && (Flags & RxClearTypeEnabled))
        Flags |= RxClearTypeGray;
    return Flags;
}

HRESULT
RxGlyphRasterizer::PrepareHinter(
    _In_ const RxRasterSize& Size)
{
    const RxFaceHeader& Header = Face->Header();
    RxHintRequest Request = {};
    HRESULT hr;

    if (HaveSize && HintedSize == Size)
        return S_OK;

    HaveSize = FALSE;
    HintUsable = FALSE;

    if (Hinter == nullptr && !HinterFailed)
    {
        Hinter = new (std::nothrow) RxTrueTypeHinter();
        if (Hinter == nullptr)
            return E_OUTOFMEMORY;
        hr = Hinter->Initialize(Face);
        if (FAILED(hr))
        {
            delete Hinter;
            Hinter = nullptr;
            HinterFailed = TRUE;
        }
    }
    if (Hinter == nullptr)
        return S_OK;

    /* 26.6 pixels per font unit, as 16.16 */
    Request.ScaleX = static_cast<INT32>(std::lround(Size.M11 * 64.0 * 65536.0 / Header.UnitsPerEm));
    Request.ScaleY = static_cast<INT32>(std::lround(Size.M22 * 64.0 * 65536.0 / Header.UnitsPerEm));
    Request.PpemX = static_cast<USHORT>(std::floor(Size.M11 + 0.5));
    Request.PpemY = static_cast<USHORT>(std::floor(Size.M22 + 0.5));
    Request.PointSize = static_cast<RxF26Dot6>(Request.PpemY) << 6;
    Request.Stretched = Size.M11 != Size.M22;
    Request.Oversampled = Size.OversampleX * Size.OversampleY > 1;
    Request.ClearTypeFlags = RxClearTypeFlags(Size);

    hr = Hinter->SetSize(Request);
    if (FAILED(hr))
        return hr;

    HintedSize = Size;
    HaveSize = TRUE;
    HintUsable = Hinter->IsUsable();
    return S_OK;
}

static
RxF26Dot6
RxTo26Dot6(
    _In_ double Value)
{
    return static_cast<RxF26Dot6>(std::floor(Value * 64.0 + 0.5));
}

HRESULT
RxGlyphRasterizer::Outline(
    _In_ USHORT Glyph,
    _In_ const RxRasterSize& Size,
    _Inout_ std::vector<RxHintPoint>& Points,
    _Inout_ std::vector<USHORT>& ContourEnds,
    _Out_ RxDropoutControl* Dropout)
{
    const RxFaceHeader& Header = Face->Header();
    BOOLEAN Oblique = (Size.Flags & RxRasterOblique) != 0;
    double Shear = Oblique ? RxObliqueShear : 0.0;
    HRESULT hr;

    Points.clear();
    ContourEnds.clear();
    Dropout->Enabled = FALSE;
    Dropout->Mode = 2;

    /* CFF outlines are not read yet */
    if (Face->Sfnt().IsCff())
        return S_OK;

    if ((Size.Flags & RxRasterHint) && RxIsAxisAligned(Size))
    {
        hr = PrepareHinter(Size);
        if (FAILED(hr))
            return hr;

        if (HintUsable)
        {
            hr = Hinter->HintGlyph(Glyph, Points, ContourEnds);
            if (SUCCEEDED(hr) && Points.size() >= 4)
            {
                Points.resize(Points.size() - 4);

                /* Subpixel rendering keeps the outline's own x; hints only place it vertically */
                if (Size.OversampleX > 1)
                {
                    RxGlyphOutline Source;

                    if (SUCCEEDED(Face->LoadOutline(Glyph, Source)) && Source.Points.size() == Points.size())
                    {
                        for (SIZE_T Index = 0; Index < Points.size(); Index++)
                            Points[Index].X = RxTo26Dot6(Size.M11 * Source.Points[Index].X / Header.UnitsPerEm);
                    }
                }
                *Dropout = Hinter->Dropout();

                /* Oblique applies to the hinted outline */
                if (Oblique)
                {
                    for (auto& Point : Points)
                    {
                        if (Size.Flags & RxRasterObliqueSideways)
                            Point.Y -= static_cast<RxF26Dot6>(std::lround(Point.X * Shear));
                        else
                            Point.X += static_cast<RxF26Dot6>(std::lround(Point.Y * Shear));
                    }
                }
                return S_OK;
            }
            Points.clear();
            ContourEnds.clear();
        }
    }

    {
        RxGlyphOutline Source;
        double Scale = 1.0 / Header.UnitsPerEm;
        double M11 = Size.M11;
        double M12 = Size.M12;
        double M21 = Size.M21;
        double M22 = Size.M22;

        hr = Face->LoadOutline(Glyph, Source);
        if (FAILED(hr))
            return hr;

        /* Shear in font space before the device transform */
        if (Oblique)
        {
            if (Size.Flags & RxRasterObliqueSideways)
            {
                M11 -= Shear * M21;
                M12 -= Shear * M22;
            }
            else
            {
                M21 += Shear * M11;
                M22 += Shear * M12;
            }
        }

        for (const auto& Point : Source.Points)
        {
            double X = (M11 * Point.X + M21 * Point.Y) * Scale;
            double Y = (M12 * Point.X + M22 * Point.Y) * Scale;

            Points.push_back({ RxTo26Dot6(X), RxTo26Dot6(Y), Point.OnCurve });
        }
        ContourEnds = Source.ContourEnds;
    }
    return S_OK;
}

HRESULT
RxGlyphRasterizer::Rasterize(
    _In_ USHORT Glyph,
    _In_ const RxRasterSize& Size,
    _Out_ RxGlyphImage* Image)
{
    std::vector<RxHintPoint> Points;
    std::vector<USHORT> ContourEnds;
    RxDropoutControl Dropout;
    HRESULT hr;

    Image->Mask = RxSampleMask{};

    hr = Outline(Glyph, Size, Points, ContourEnds, &Dropout);
    if (FAILED(hr) || Points.empty())
        return hr;

    /* Oversampling scales the outline onto the sample grid */
    for (auto& Point : Points)
    {
        Point.X *= static_cast<INT32>(Size.OversampleX);
        Point.Y *= static_cast<INT32>(Size.OversampleY);
    }

    /* Antialiased types turn the dropout rules off */
    if (Size.OversampleX * Size.OversampleY > 1 && (Dropout.Mode & 1))
        Dropout.Mode &= ~1;

    return RxScanConvert(Points.data(), static_cast<UINT32>(Points.size()), ContourEnds.data(),
                         static_cast<UINT32>(ContourEnds.size()), Dropout, &Image->Mask);
}
