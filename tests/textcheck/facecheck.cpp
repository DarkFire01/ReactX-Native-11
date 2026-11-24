/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares GDI compatible metrics, glyph outlines and face names against the system
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textcheck.hpp"

#include <d2d1.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

/* Records an outline as text, coordinates to 1/1000 */
class TcOutlineSink final : public IDWriteGeometrySink
{
public:
    std::vector<std::wstring> Commands;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SimplifiedGeometrySink))
        {
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE fillMode) override
    {
        Add(L"fill %d", fillMode);
    }
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT vertexFlags) override
    {
        Add(L"flags %d", vertexFlags);
    }
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin) override
    {
        Add(L"begin %d %s", figureBegin, Point(startPoint).c_str());
    }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F* points, UINT32 pointsCount) override
    {
        for (UINT32 Index = 0; Index < pointsCount; Index++)
            Add(L"line %s", Point(points[Index]).c_str());
    }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT* beziers, UINT32 beziersCount) override
    {
        for (UINT32 Index = 0; Index < beziersCount; Index++)
            Add(L"bezier %s %s %s", Point(beziers[Index].point1).c_str(), Point(beziers[Index].point2).c_str(),
                Point(beziers[Index].point3).c_str());
    }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END figureEnd) override
    {
        Add(L"end %d", figureEnd);
    }
    HRESULT STDMETHODCALLTYPE Close() override
    {
        Add(L"close");
        return S_OK;
    }

private:
    static
    std::wstring
    Point(
        _In_ const D2D1_POINT_2F& Value)
    {
        WCHAR Text[64];

        swprintf_s(Text, L"%.3f,%.3f", Value.x + 0.0f, Value.y + 0.0f);
        return Text;
    }

    VOID
    Add(
        _In_z_ _Printf_format_string_ PCWSTR Format,
        ...)
    {
        WCHAR Text[256];
        va_list Arguments;

        va_start(Arguments, Format);
        vswprintf_s(Text, Format, Arguments);
        va_end(Arguments);
        Commands.push_back(Text);
    }
};

static
std::wstring
TcMetricsLine(
    _In_ HRESULT Result,
    _In_ const DWRITE_FONT_METRICS1& M)
{
    WCHAR Line[300];

    swprintf_s(Line, L"hr %08lX upem %u asc %u desc %u gap %d cap %u x %u ul %d %u st %d %u box %d %d %d %d sub %d %d %d %d "
               L"sup %d %d %d %d", Result, M.designUnitsPerEm, M.ascent, M.descent, M.lineGap, M.capHeight, M.xHeight,
               M.underlinePosition, M.underlineThickness, M.strikethroughPosition, M.strikethroughThickness,
               M.glyphBoxLeft, M.glyphBoxTop, M.glyphBoxRight, M.glyphBoxBottom, M.subscriptPositionX,
               M.subscriptPositionY, M.subscriptSizeX, M.subscriptSizeY, M.superscriptPositionX, M.superscriptPositionY,
               M.superscriptSizeX, M.superscriptSizeY);
    return Line;
}

VOID
TcCompareFaceExtras(
    _In_ IDWriteFontFace* Want,
    _In_ IDWriteFontFace* Got,
    _In_z_ PCWSTR Name)
{
    ComPtr<IDWriteFontFace1> Want1;
    ComPtr<IDWriteFontFace1> Got1;
    ComPtr<IDWriteFontFace3> Want3;
    ComPtr<IDWriteFontFace3> Got3;
    static const FLOAT Sizes[] = { 8.0f, 9.0f, 11.0f, 12.0f, 13.0f, 16.0f, 20.0f, 32.0f, 72.0f };
    static const FLOAT Dips[] = { 1.0f, 1.25f, 2.0f };
    std::vector<UINT16> Glyphs;
    ULONG Mismatches = 0;
    ULONG Shown = 0;

    for (UINT16 Glyph = 0; Glyph < std::min<UINT32>(Want->GetGlyphCount(), 400); Glyph++)
        Glyphs.push_back(Glyph);

    if (FAILED(Want->QueryInterface(IID_PPV_ARGS(&Want1))) || FAILED(Got->QueryInterface(IID_PPV_ARGS(&Got1))))
    {
        TcCheck(FALSE, "%ls IDWriteFontFace1 missing", Name);
        return;
    }

    /* GDI compatible font metrics */
    for (FLOAT Size : Sizes)
    {
        for (FLOAT Dip : Dips)
        {
            DWRITE_FONT_METRICS1 A = {};
            DWRITE_FONT_METRICS1 B = {};
            std::wstring Expected = TcMetricsLine(Want1->GetGdiCompatibleMetrics(Size, Dip, nullptr, &A), A);
            std::wstring Actual = TcMetricsLine(Got1->GetGdiCompatibleMetrics(Size, Dip, nullptr, &B), B);

            if (Expected != Actual)
            {
                Mismatches++;
                if (Shown++ < 3)
                    printf("  gdi metrics %.0f x%.2f\n    want %ls\n    got  %ls\n", Size, Dip, Expected.c_str(),
                           Actual.c_str());
            }
        }
    }
    TcCheck(Mismatches == 0, "%ls GetGdiCompatibleMetrics, %lu sizes differ", Name, Mismatches);

    /* GDI compatible glyph metrics, classic and natural */
    for (BOOL Natural = FALSE; Natural <= TRUE; Natural++)
    {
        Mismatches = 0;
        Shown = 0;
        for (FLOAT Size : Sizes)
        {
            std::vector<DWRITE_GLYPH_METRICS> A(Glyphs.size());
            std::vector<DWRITE_GLYPH_METRICS> B(Glyphs.size());
            HRESULT Ha = Want->GetGdiCompatibleGlyphMetrics(Size, 1.0f, nullptr, Natural, Glyphs.data(),
                                                            static_cast<UINT32>(Glyphs.size()), A.data(), FALSE);
            HRESULT Hb = Got->GetGdiCompatibleGlyphMetrics(Size, 1.0f, nullptr, Natural, Glyphs.data(),
                                                           static_cast<UINT32>(Glyphs.size()), B.data(), FALSE);

            if (Ha != Hb)
            {
                Mismatches++;
                continue;
            }
            for (SIZE_T Index = 0; Index < Glyphs.size(); Index++)
            {
                const DWRITE_GLYPH_METRICS& X = A[Index];
                const DWRITE_GLYPH_METRICS& Y = B[Index];

                if (memcmp(&X, &Y, sizeof(X)) == 0)
                    continue;
                Mismatches++;
                if (Shown++ < 3)
                    printf("  gdi glyph %u size %.0f natural %d: want %u %d %d %u %d %d %d, got %u %d %d %u %d %d %d\n",
                           Glyphs[Index], Size, Natural, X.advanceWidth, X.leftSideBearing, X.rightSideBearing,
                           X.advanceHeight, X.topSideBearing, X.bottomSideBearing, X.verticalOriginY, Y.advanceWidth,
                           Y.leftSideBearing, Y.rightSideBearing, Y.advanceHeight, Y.topSideBearing,
                           Y.bottomSideBearing, Y.verticalOriginY);
            }
        }
        TcCheck(Mismatches == 0, "%ls GetGdiCompatibleGlyphMetrics natural %d, %lu glyphs differ", Name, Natural,
                Mismatches);
    }

    /* Outlines of a run with advances and offsets, left to right and right to left */
    for (BOOL RightToLeft = FALSE; RightToLeft <= TRUE; RightToLeft++)
    {
        UINT16 Run[] = { 36, 37, 68, 69, 70, 3 };
        FLOAT Advances[] = { 10.5f, 11.0f, 9.25f, 8.0f, 7.5f, 4.0f };
        DWRITE_GLYPH_OFFSET Offsets[] = { { 0, 0 }, { 1.5f, -2.0f }, { 0, 0 }, { 0, 3.0f }, { 0, 0 }, { 0, 0 } };
        TcOutlineSink A;
        TcOutlineSink B;
        HRESULT Ha;
        HRESULT Hb;

        for (UINT16& Glyph : Run)
            Glyph = static_cast<UINT16>(std::min<UINT32>(Glyph, Want->GetGlyphCount() - 1));
        Ha = Want->GetGlyphRunOutline(24.0f, Run, Advances, Offsets, 6, FALSE, RightToLeft, &A);
        Hb = Got->GetGlyphRunOutline(24.0f, Run, Advances, Offsets, 6, FALSE, RightToLeft, &B);
        Mismatches = 0;
        for (SIZE_T Index = 0; Index < std::max(A.Commands.size(), B.Commands.size()); Index++)
        {
            PCWSTR X = Index < A.Commands.size() ? A.Commands[Index].c_str() : L"(none)";
            PCWSTR Y = Index < B.Commands.size() ? B.Commands[Index].c_str() : L"(none)";

            if (wcscmp(X, Y) != 0 && Mismatches++ < 3)
                printf("  outline rtl %d command %Iu: want %ls got %ls\n", RightToLeft, Index, X, Y);
        }
        TcCheck(Ha == Hb && Mismatches == 0, "%ls GetGlyphRunOutline rtl %d: hr %08lX/%08lX, %Iu/%Iu commands, %lu differ",
                Name, RightToLeft, Ha, Hb, A.Commands.size(), B.Commands.size(), Mismatches);
    }

    /* Face names */
    if (SUCCEEDED(Want->QueryInterface(IID_PPV_ARGS(&Want3))) && SUCCEEDED(Got->QueryInterface(IID_PPV_ARGS(&Got3))))
    {
        ComPtr<IDWriteLocalizedStrings> A;
        ComPtr<IDWriteLocalizedStrings> B;

        Want3->GetFamilyNames(&A);
        Got3->GetFamilyNames(&B);
        TcCheck(TcName(A.Get()) == TcName(B.Get()), "%ls IDWriteFontFace3::GetFamilyNames want %ls got %ls", Name,
                TcName(A.Get()).c_str(), TcName(B.Get()).c_str());
        A.Reset();
        B.Reset();
        Want3->GetFaceNames(&A);
        Got3->GetFaceNames(&B);
        TcCheck(TcName(A.Get()) == TcName(B.Get()), "%ls IDWriteFontFace3::GetFaceNames want %ls got %ls", Name,
                TcName(A.Get()).c_str(), TcName(B.Get()).c_str());

        for (UINT32 Id = 0; Id <= DWRITE_INFORMATIONAL_STRING_WEIGHT_STRETCH_STYLE_FAMILY_NAME; Id++)
        {
            BOOL ExistsA = FALSE;
            BOOL ExistsB = FALSE;

            A.Reset();
            B.Reset();
            Want3->GetInformationalStrings(static_cast<DWRITE_INFORMATIONAL_STRING_ID>(Id), &A, &ExistsA);
            Got3->GetInformationalStrings(static_cast<DWRITE_INFORMATIONAL_STRING_ID>(Id), &B, &ExistsB);
            TcCheck(ExistsA == ExistsB && TcName(A.Get()) == TcName(B.Get()),
                    "%ls informational string %u want %d [%ls] got %d [%ls]", Name, Id, ExistsA, TcName(A.Get()).c_str(),
                    ExistsB, TcName(B.Get()).c_str());
        }
    }
}
