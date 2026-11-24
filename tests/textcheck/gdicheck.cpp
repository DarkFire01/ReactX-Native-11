/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares IDWriteGdiInterop and the bitmap render target against the system
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textcheck.hpp"

#include <algorithm>
#include <cstdio>

static
std::wstring
TcDescribeLogFont(
    _In_ HRESULT Result,
    _In_ const LOGFONTW& LogFont)
{
    WCHAR Line[160];

    if (FAILED(Result))
    {
        swprintf_s(Line, L"hr 0x%08lX", Result);
        return Line;
    }
    swprintf_s(Line, L"[%.32s] h%ld w%ld wt%ld it%u ul%u so%u cs%u op%u cp%u q%u pf%u", LogFont.lfFaceName,
               LogFont.lfHeight, LogFont.lfWidth, LogFont.lfWeight, LogFont.lfItalic, LogFont.lfUnderline,
               LogFont.lfStrikeOut, LogFont.lfCharSet, LogFont.lfOutPrecision, LogFont.lfClipPrecision,
               LogFont.lfQuality, LogFont.lfPitchAndFamily);
    return Line;
}

static
ComPtr<IDWriteGdiInterop>
TcInterop(
    _In_ TcSide& Side)
{
    ComPtr<IDWriteGdiInterop> Interop;
    HRESULT hr = Side.Factory->GetGdiInterop(&Interop);

    TcCheck(SUCCEEDED(hr) && Interop, "%ls GetGdiInterop 0x%08lX", Side.Name, hr);
    return Interop;
}

/* Everything a LOGFONT lookup gives back: the font, its LOGFONT, signature and face LOGFONT */
static
std::wstring
TcDescribeLookup(
    _In_ IDWriteGdiInterop* Interop,
    _In_ const LOGFONTW& Request)
{
    ComPtr<IDWriteFont> Font;
    ComPtr<IDWriteFontFamily> Family;
    ComPtr<IDWriteLocalizedStrings> Names;
    ComPtr<IDWriteFontFace> Face;
    ComPtr<IDWriteGdiInterop1> Interop1;
    LOGFONTW Back = {};
    LOGFONTW FaceBack = {};
    FONTSIGNATURE Signature = {};
    BOOL IsSystem = 2;
    WCHAR Line[160];
    std::wstring Text;
    HRESULT hr;

    hr = Interop->CreateFontFromLOGFONT(&Request, &Font);
    if (FAILED(hr))
    {
        swprintf_s(Line, L"hr 0x%08lX font %s", hr, Font ? L"set" : L"null");
        return Line;
    }

    Font->GetFontFamily(&Family);
    if (Family)
        Family->GetFamilyNames(&Names);
    Text = TcName(Names.Get()) + L" / " + TcDescribeFont(Font.Get());

    hr = Interop->ConvertFontToLOGFONT(Font.Get(), &Back, &IsSystem);
    swprintf_s(Line, L" | font %ls sys%d", TcDescribeLogFont(hr, Back).c_str(), IsSystem);
    Text += Line;

    if (SUCCEEDED(Interop->QueryInterface(IID_PPV_ARGS(&Interop1))))
    {
        hr = Interop1->GetFontSignature(Font.Get(), &Signature);
        swprintf_s(Line, L" | sig 0x%08lX %08lX %08lX %08lX %08lX %08lX %08lX", hr, Signature.fsUsb[0],
                   Signature.fsUsb[1], Signature.fsUsb[2], Signature.fsUsb[3], Signature.fsCsb[0], Signature.fsCsb[1]);
        Text += Line;
    }

    if (SUCCEEDED(Font->CreateFontFace(&Face)))
    {
        hr = Interop->ConvertFontFaceToLOGFONT(Face.Get(), &FaceBack);
        swprintf_s(Line, L" | face type %d ", Face->GetType());
        Text += Line + TcDescribeLogFont(hr, FaceBack);
        if (Interop1)
        {
            memset(&Signature, 0xCC, sizeof(Signature));
            hr = Interop1->GetFontSignature(Face.Get(), &Signature);
            swprintf_s(Line, L" | facesig 0x%08lX %08lX %08lX", hr, Signature.fsUsb[0], Signature.fsCsb[0]);
            Text += Line;
        }
    }
    return Text;
}

static
VOID
TcCompareLookups(
    _In_ TcSide& Want,
    _In_ TcSide& Got,
    _In_ IDWriteGdiInterop* WantInterop,
    _In_ IDWriteGdiInterop* GotInterop)
{
    ComPtr<IDWriteFontCollection> Collection;
    std::vector<std::wstring> Names;
    static const LONG Weights[] = { 0, 100, 300, 400, 500, 600, 700, 800, 900 };
    static const PCWSTR Extra[] =
    {
        L"Segoe UI Semibold", L"Segoe UI Light", L"Segoe UI Black", L"Arial Black", L"Arial Narrow", L"Arial Bold",
        L"Arial Bold Italic", L"Segoe UI Variable", L"Segoe UI Variable Text", L"Cambria Math", L"MS Shell Dlg",
        L"MS Shell Dlg 2", L"System", L"Courier", L"No Such Font", L"", L"arial", L"SEGOE UI", L"Yu Gothic UI",
        L"Microsoft YaHei UI", L"Malgun Gothic", L"Symbol", L"Wingdings", L"Marlett", L"Segoe UI Emoji",
    };
    static const BYTE Charsets[] = { ANSI_CHARSET, SYMBOL_CHARSET, SHIFTJIS_CHARSET, GREEK_CHARSET, RUSSIAN_CHARSET };
    ULONG Mismatches = 0;
    ULONG Cases = 0;
    std::wstring Differing;

    printf("gdi interop lookups\n");
    if (FAILED(Want.Factory->GetSystemFontCollection(&Collection)))
        return;
    for (UINT32 Index = 0; Index < Collection->GetFontFamilyCount(); Index++)
    {
        ComPtr<IDWriteFontFamily> Family;
        ComPtr<IDWriteLocalizedStrings> FamilyNames;

        Collection->GetFontFamily(Index, &Family);
        Family->GetFamilyNames(&FamilyNames);
        Names.push_back(TcName(FamilyNames.Get()));
    }
    for (PCWSTR Name : Extra)
        Names.push_back(Name);

    for (const auto& Name : Names)
    {
        for (LONG Weight : Weights)
        {
            for (BYTE Italic = 0; Italic < 2; Italic++)
            {
                LOGFONTW Request = {};
                std::wstring Expected;
                std::wstring Actual;

                Request.lfHeight = -16;
                Request.lfWeight = Weight;
                Request.lfItalic = Italic;
                Request.lfCharSet = DEFAULT_CHARSET;
                wcsncpy_s(Request.lfFaceName, Name.c_str(), _TRUNCATE);

                Expected = TcDescribeLookup(WantInterop, Request);
                Actual = TcDescribeLookup(GotInterop, Request);
                Cases++;
                if (Expected == Actual)
                    continue;
                if (Differing.find(L"[" + Name + L"]") == std::wstring::npos)
                    Differing += L" [" + Name + L"]";
                if (Mismatches++ < 40)
                    printf("  LOGFONT %ls %ld %u\n    want %ls\n    got  %ls\n", Name.c_str(), Weight, Italic,
                           Expected.c_str(), Actual.c_str());
            }
        }
    }

    for (PCWSTR Name : { L"Arial", L"Segoe UI", L"Symbol", L"MS Gothic", L"Times New Roman" })
    {
        for (BYTE Charset : Charsets)
        {
            LOGFONTW Request = {};
            std::wstring Expected;
            std::wstring Actual;

            Request.lfWeight = FW_BOLD;
            Request.lfCharSet = Charset;
            wcscpy_s(Request.lfFaceName, Name);
            Expected = TcDescribeLookup(WantInterop, Request);
            Actual = TcDescribeLookup(GotInterop, Request);
            Cases++;
            if (Expected != Actual && Mismatches++ < 40)
                printf("  LOGFONT %ls charset %u\n    want %ls\n    got  %ls\n", Name, Charset, Expected.c_str(),
                       Actual.c_str());
        }
    }
    if (!Differing.empty())
        printf("  names that differ:%ls\n", Differing.c_str());
    TcCheck(Mismatches == 0, "LOGFONT lookups, %lu of %lu differ", Mismatches, Cases);

    /* Argument checks */
    {
        ComPtr<IDWriteFont> Font;
        LOGFONTW Back;
        BOOL IsSystem;
        HRESULT WantHr = WantInterop->CreateFontFromLOGFONT(nullptr, &Font);
        HRESULT GotHr = GotInterop->CreateFontFromLOGFONT(nullptr, &Font);

        TcCheck(WantHr == GotHr, "CreateFontFromLOGFONT(NULL) want 0x%08lX got 0x%08lX", WantHr, GotHr);
        WantHr = WantInterop->ConvertFontToLOGFONT(nullptr, &Back, &IsSystem);
        GotHr = GotInterop->ConvertFontToLOGFONT(nullptr, &Back, &IsSystem);
        TcCheck(WantHr == GotHr, "ConvertFontToLOGFONT(NULL) want 0x%08lX got 0x%08lX", WantHr, GotHr);
        WantHr = WantInterop->ConvertFontFaceToLOGFONT(nullptr, &Back);
        GotHr = GotInterop->ConvertFontFaceToLOGFONT(nullptr, &Back);
        TcCheck(WantHr == GotHr, "ConvertFontFaceToLOGFONT(NULL) want 0x%08lX got 0x%08lX", WantHr, GotHr);
    }
}

static
std::wstring
TcDescribeFace(
    _In_ HRESULT Result,
    _In_opt_ IDWriteFontFace* Face)
{
    ComPtr<IDWriteFontFile> File;
    ComPtr<IDWriteFontFileLoader> Loader;
    ComPtr<IDWriteLocalFontFileLoader> Local;
    UINT32 Files = 1;
    WCHAR Path[MAX_PATH] = L"?";
    WCHAR Line[400];

    if (FAILED(Result) || Face == nullptr)
    {
        swprintf_s(Line, L"hr 0x%08lX", Result);
        return Line;
    }

    if (SUCCEEDED(Face->GetFiles(&Files, &File)) && SUCCEEDED(File->GetLoader(&Loader)) &&
        SUCCEEDED(Loader.As(&Local)))
    {
        const void* Key;
        UINT32 KeySize;

        File->GetReferenceKey(&Key, &KeySize);
        Local->GetFilePathFromKey(Key, KeySize, Path, MAX_PATH);
        _wcslwr_s(Path);
    }

    swprintf_s(Line, L"type %d index %u sim %d glyphs %u symbol %d file %s", Face->GetType(), Face->GetIndex(),
               Face->GetSimulations(), Face->GetGlyphCount(), Face->IsSymbolFont(), Path);
    return Line;
}

static
VOID
TcCompareHdcFaces(
    _In_ IDWriteGdiInterop* WantInterop,
    _In_ IDWriteGdiInterop* GotInterop)
{
    static const struct
    {
        PCWSTR Name;
        LONG Weight;
        BYTE Italic;
    } Cases[] =
    {
        { L"Arial", FW_NORMAL, 0 }, { L"Arial", FW_BOLD, 0 }, { L"Arial", FW_NORMAL, 1 }, { L"Arial", FW_BOLD, 1 },
        { L"Segoe UI", FW_NORMAL, 0 }, { L"Segoe UI", FW_SEMIBOLD, 0 }, { L"Segoe UI Semibold", FW_NORMAL, 0 },
        { L"Tahoma", FW_NORMAL, 1 }, { L"Tahoma", FW_HEAVY, 0 }, { L"Cambria", FW_NORMAL, 0 },
        { L"Cambria Math", FW_NORMAL, 0 }, { L"Consolas", FW_BOLD, 1 }, { L"Times New Roman", FW_NORMAL, 0 },
        { L"MS Shell Dlg 2", FW_NORMAL, 0 }, { L"Verdana", FW_BLACK, 1 }, { L"No Such Font", FW_NORMAL, 0 },
        { L"Courier", FW_NORMAL, 0 }, { L"System", FW_NORMAL, 0 },
    };
    HDC Dc = CreateCompatibleDC(nullptr);
    ULONG Mismatches = 0;

    printf("gdi interop faces from a DC\n");
    for (const auto& Case : Cases)
    {
        LOGFONTW Request = {};
        HFONT Font;
        HGDIOBJ Old;
        ComPtr<IDWriteFontFace> WantFace;
        ComPtr<IDWriteFontFace> GotFace;
        HRESULT WantHr;
        HRESULT GotHr;
        LOGFONTW WantBack = {};
        LOGFONTW GotBack = {};
        std::wstring Expected;
        std::wstring Actual;

        Request.lfHeight = -20;
        Request.lfWeight = Case.Weight;
        Request.lfItalic = Case.Italic;
        Request.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(Request.lfFaceName, Case.Name);
        Font = CreateFontIndirectW(&Request);
        Old = SelectObject(Dc, Font);

        WantHr = WantInterop->CreateFontFaceFromHdc(Dc, &WantFace);
        GotHr = GotInterop->CreateFontFaceFromHdc(Dc, &GotFace);
        Expected = TcDescribeFace(WantHr, WantFace.Get());
        Actual = TcDescribeFace(GotHr, GotFace.Get());
        if (WantFace)
            Expected += L" | " + TcDescribeLogFont(WantInterop->ConvertFontFaceToLOGFONT(WantFace.Get(), &WantBack),
                                                   WantBack);
        if (GotFace)
            Actual += L" | " + TcDescribeLogFont(GotInterop->ConvertFontFaceToLOGFONT(GotFace.Get(), &GotBack),
                                                 GotBack);
        if (Expected != Actual)
        {
            Mismatches++;
            printf("  HDC %ls %ld %u\n    want %ls\n    got  %ls\n", Case.Name, Case.Weight, Case.Italic,
                   Expected.c_str(), Actual.c_str());
        }

        SelectObject(Dc, Old);
        DeleteObject(Font);
    }

    {
        ComPtr<IDWriteFontFace> Face;
        HRESULT WantHr = WantInterop->CreateFontFaceFromHdc(nullptr, &Face);
        HRESULT GotHr = GotInterop->CreateFontFaceFromHdc(nullptr, &Face);

        TcCheck(WantHr == GotHr, "CreateFontFaceFromHdc(NULL) want 0x%08lX got 0x%08lX", WantHr, GotHr);
    }
    DeleteDC(Dc);
    TcCheck(Mismatches == 0, "CreateFontFaceFromHdc, %lu cases differ", Mismatches);
}

/* The bits of the DIB selected into a target's memory DC */
static
const UINT32*
TcTargetBits(
    _In_ IDWriteBitmapRenderTarget* Target)
{
    DIBSECTION Section = {};
    HGDIOBJ Bitmap = GetCurrentObject(Target->GetMemoryDC(), OBJ_BITMAP);

    if (GetObjectW(Bitmap, sizeof(Section), &Section) != sizeof(Section))
        return nullptr;
    return static_cast<const UINT32*>(Section.dsBm.bmBits);
}

static
VOID
TcFillTarget(
    _In_ IDWriteBitmapRenderTarget* Target,
    _In_ UINT32 Color)
{
    SIZE Size = {};
    UINT32* Bits = const_cast<UINT32*>(TcTargetBits(Target));

    Target->GetSize(&Size);
    if (Bits == nullptr)
        return;
    std::fill(Bits, Bits + static_cast<SIZE_T>(Size.cx) * Size.cy, Color);
}

struct TcRunData
{
    ComPtr<IDWriteFontFace> Face;
    std::vector<UINT16> Glyphs;
    std::vector<FLOAT> Advances;
};

static
HRESULT
TcMakeRun(
    _In_ IDWriteGdiInterop* Interop,
    _In_z_ PCWSTR Family,
    _In_ LONG Weight,
    _In_ FLOAT EmSize,
    _In_z_ PCWSTR Text,
    _Out_ TcRunData* Run)
{
    LOGFONTW Request = {};
    ComPtr<IDWriteFont> Font;
    std::vector<UINT32> Codes;
    std::vector<DWRITE_GLYPH_METRICS> Metrics;
    DWRITE_FONT_METRICS FontMetrics;
    HRESULT hr;

    Request.lfWeight = Weight;
    Request.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(Request.lfFaceName, Family);
    hr = Interop->CreateFontFromLOGFONT(&Request, &Font);
    if (FAILED(hr))
        return hr;
    hr = Font->CreateFontFace(&Run->Face);
    if (FAILED(hr))
        return hr;

    for (PCWSTR Char = Text; *Char != UNICODE_NULL; Char++)
        Codes.push_back(*Char);
    Run->Glyphs.resize(Codes.size());
    Run->Advances.resize(Codes.size());
    Metrics.resize(Codes.size());
    Run->Face->GetGlyphIndices(Codes.data(), static_cast<UINT32>(Codes.size()), Run->Glyphs.data());
    Run->Face->GetDesignGlyphMetrics(Run->Glyphs.data(), static_cast<UINT32>(Run->Glyphs.size()), Metrics.data(), FALSE);
    Run->Face->GetMetrics(&FontMetrics);
    for (SIZE_T Index = 0; Index < Codes.size(); Index++)
        Run->Advances[Index] = Metrics[Index].advanceWidth * EmSize / FontMetrics.designUnitsPerEm;
    return S_OK;
}

static
VOID
TcCompareBitmapTargets(
    _In_ TcSide& Want,
    _In_ TcSide& Got,
    _In_ IDWriteGdiInterop* WantInterop,
    _In_ IDWriteGdiInterop* GotInterop)
{
    static const struct
    {
        FLOAT Gamma;
        FLOAT Contrast;
        FLOAT Level;
        DWRITE_PIXEL_GEOMETRY Geometry;
        DWRITE_RENDERING_MODE Mode;
        PCWSTR Tag;
    } Params[] =
    {
        { 0.0f, 0.0f, 0.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_DEFAULT, L"system" },
        { 1.8f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_NATURAL, L"natural" },
        { 2.2f, 1.0f, 1.0f, DWRITE_PIXEL_GEOMETRY_BGR, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, L"symmetricbgr" },
        { 1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, L"flat" },
        { 1.8f, 0.5f, 0.5f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_NATURAL, L"halflevel" },
        { 1.8f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_GDI_CLASSIC, L"gdiclassic" },
        { 1.8f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_GDI_NATURAL, L"gdinatural" },
        { 1.8f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_ALIASED, L"aliased" },
        { 1.8f, 0.5f, 1.0f, DWRITE_PIXEL_GEOMETRY_RGB, DWRITE_RENDERING_MODE_OUTLINE, L"outline" },
    };
    static const struct
    {
        UINT32 Background;
        COLORREF Text;
    } Colors[] =
    {
        { 0x00FFFFFF, RGB(0, 0, 0) }, { 0x00000000, RGB(255, 255, 255) }, { 0x00808080, RGB(200, 30, 60) },
    };
    static const FLOAT Sizes[] = { 9.0f, 12.0f, 16.0f, 24.0f };
    static const PCWSTR Text = L"Hamburgefonstiv Wg 0123";
    ComPtr<IDWriteBitmapRenderTarget> WantTarget;
    ComPtr<IDWriteBitmapRenderTarget> GotTarget;
    ComPtr<IDWriteBitmapRenderTarget1> WantTarget1;
    ComPtr<IDWriteBitmapRenderTarget1> GotTarget1;
    HDC Screen = GetDC(nullptr);
    HRESULT WantHr;
    HRESULT GotHr;
    ULONG Mismatches = 0;
    ULONG Cases = 0;

    printf("bitmap render target\n");
    WantHr = WantInterop->CreateBitmapRenderTarget(Screen, 320, 64, &WantTarget);
    GotHr = GotInterop->CreateBitmapRenderTarget(Screen, 320, 64, &GotTarget);
    ReleaseDC(nullptr, Screen);
    TcCheck(WantHr == GotHr, "CreateBitmapRenderTarget want 0x%08lX got 0x%08lX", WantHr, GotHr);
    if (FAILED(WantHr) || FAILED(GotHr))
        return;

    TcCheck(WantTarget->GetPixelsPerDip() == GotTarget->GetPixelsPerDip(), "GetPixelsPerDip want %.9g got %.9g",
            WantTarget->GetPixelsPerDip(), GotTarget->GetPixelsPerDip());
    TcCheck(SUCCEEDED(WantTarget.As(&WantTarget1)) == SUCCEEDED(GotTarget.As(&GotTarget1)),
            "IDWriteBitmapRenderTarget1 support differs");

    /* Size, transform and argument checks */
    {
        static const DWRITE_MATRIX Skew = { 1.0f, 0.0f, 0.3f, 1.0f, 2.0f, 1.0f };
        DWRITE_MATRIX A = {};
        DWRITE_MATRIX B = {};
        SIZE SizeA = {};
        SIZE SizeB = {};
        HRESULT Ha;
        HRESULT Hb;

        WantTarget->GetCurrentTransform(&A);
        GotTarget->GetCurrentTransform(&B);
        TcCheck(memcmp(&A, &B, sizeof(A)) == 0, "default transform differs");
        WantTarget->SetCurrentTransform(&Skew);
        GotTarget->SetCurrentTransform(&Skew);
        WantTarget->GetCurrentTransform(&A);
        GotTarget->GetCurrentTransform(&B);
        TcCheck(memcmp(&A, &B, sizeof(A)) == 0, "transform round trip differs");
        WantTarget->SetCurrentTransform(nullptr);
        GotTarget->SetCurrentTransform(nullptr);

        Ha = WantTarget->SetPixelsPerDip(0.0f);
        Hb = GotTarget->SetPixelsPerDip(0.0f);
        TcCheck(Ha == Hb, "SetPixelsPerDip(0) want 0x%08lX got 0x%08lX", Ha, Hb);

        for (const SIZE& Next : { SIZE{ 10, 0 }, SIZE{ 0, 0 }, SIZE{ 33, 17 }, SIZE{ 320, 64 } })
        {
            Ha = WantTarget->Resize(Next.cx, Next.cy);
            Hb = GotTarget->Resize(Next.cx, Next.cy);
            WantTarget->GetSize(&SizeA);
            GotTarget->GetSize(&SizeB);
            TcCheck(Ha == Hb && SizeA.cx == SizeB.cx && SizeA.cy == SizeB.cy,
                    "Resize %ld,%ld want 0x%08lX %ldx%ld got 0x%08lX %ldx%ld", Next.cx, Next.cy, Ha, SizeA.cx,
                    SizeA.cy, Hb, SizeB.cx, SizeB.cy);
        }
        if (WantTarget1 && GotTarget1)
        {
            Ha = WantTarget1->SetTextAntialiasMode(static_cast<DWRITE_TEXT_ANTIALIAS_MODE>(2));
            Hb = GotTarget1->SetTextAntialiasMode(static_cast<DWRITE_TEXT_ANTIALIAS_MODE>(2));
            TcCheck(Ha == Hb, "SetTextAntialiasMode(2) want 0x%08lX got 0x%08lX", Ha, Hb);
            TcCheck(WantTarget1->GetTextAntialiasMode() == GotTarget1->GetTextAntialiasMode(),
                    "default antialias mode differs");
        }
    }

    for (const auto& Param : Params)
    {
        ComPtr<IDWriteRenderingParams> WantParams;
        ComPtr<IDWriteRenderingParams> GotParams;

        if (Param.Gamma == 0.0f)
        {
            Want.Factory->CreateRenderingParams(&WantParams);
            Got.Factory->CreateRenderingParams(&GotParams);
        }
        else
        {
            Want.Factory->CreateCustomRenderingParams(Param.Gamma, Param.Contrast, Param.Level, Param.Geometry,
                                                      Param.Mode, &WantParams);
            Got.Factory->CreateCustomRenderingParams(Param.Gamma, Param.Contrast, Param.Level, Param.Geometry,
                                                     Param.Mode, &GotParams);
        }
        if (!WantParams || !GotParams)
        {
            TcCheck(FALSE, "%ls params not created", Param.Tag);
            continue;
        }

        for (UINT32 Antialias = 0; Antialias < 2; Antialias++)
        {
            if (Antialias != 0 && !(WantTarget1 && GotTarget1))
                continue;
            if (WantTarget1 && GotTarget1)
            {
                WantTarget1->SetTextAntialiasMode(static_cast<DWRITE_TEXT_ANTIALIAS_MODE>(Antialias));
                GotTarget1->SetTextAntialiasMode(static_cast<DWRITE_TEXT_ANTIALIAS_MODE>(Antialias));
            }

            for (FLOAT Size : Sizes)
            {
                for (const auto& Color : Colors)
                {
                    TcRunData WantRun;
                    TcRunData GotRun;
                    DWRITE_GLYPH_RUN Run = {};
                    RECT WantBox = { -1, -1, -1, -1 };
                    RECT GotBox = { -1, -1, -1, -1 };
                    const UINT32* WantBits;
                    const UINT32* GotBits;
                    ULONG Different = 0;
                    ULONG Largest = 0;

                    if (FAILED(TcMakeRun(WantInterop, L"Segoe UI", FW_NORMAL, Size, Text, &WantRun)) ||
                        FAILED(TcMakeRun(GotInterop, L"Segoe UI", FW_NORMAL, Size, Text, &GotRun)))
                    {
                        TcCheck(FALSE, "no Segoe UI run");
                        return;
                    }

                    TcFillTarget(WantTarget.Get(), Color.Background);
                    TcFillTarget(GotTarget.Get(), Color.Background);

                    Run.fontEmSize = Size;
                    Run.glyphCount = static_cast<UINT32>(WantRun.Glyphs.size());
                    Run.glyphIndices = WantRun.Glyphs.data();
                    Run.glyphAdvances = WantRun.Advances.data();
                    Run.fontFace = WantRun.Face.Get();
                    WantHr = WantTarget->DrawGlyphRun(4.25f, 40.0f, DWRITE_MEASURING_MODE_NATURAL, &Run,
                                                      WantParams.Get(), Color.Text, &WantBox);
                    Run.fontFace = GotRun.Face.Get();
                    GotHr = GotTarget->DrawGlyphRun(4.25f, 40.0f, DWRITE_MEASURING_MODE_NATURAL, &Run, GotParams.Get(),
                                                    Color.Text, &GotBox);

                    WantBits = TcTargetBits(WantTarget.Get());
                    GotBits = TcTargetBits(GotTarget.Get());
                    if (WantBits != nullptr && GotBits != nullptr)
                    {
                        for (SIZE_T Index = 0; Index < 320 * 64; Index++)
                        {
                            for (UINT32 Shift = 0; Shift < 24; Shift += 8)
                            {
                                INT A = (WantBits[Index] >> Shift) & 0xFF;
                                INT B = (GotBits[Index] >> Shift) & 0xFF;

                                if (A != B)
                                {
                                    Different++;
                                    Largest = std::max<ULONG>(Largest, abs(A - B));
                                }
                            }
                        }
                    }

                    Cases++;
                    if (WantHr != GotHr || !EqualRect(&WantBox, &GotBox) || Different != 0)
                    {
                        if (Mismatches++ < 30)
                            printf("  draw %ls aa%u %.0f text %06lX: hr 0x%08lX/0x%08lX box %ld,%ld,%ld,%ld / "
                                   "%ld,%ld,%ld,%ld, %lu bytes differ, largest %lu\n", Param.Tag, Antialias, Size,
                                   Color.Text, WantHr, GotHr, WantBox.left, WantBox.top, WantBox.right,
                                   WantBox.bottom, GotBox.left, GotBox.top, GotBox.right, GotBox.bottom, Different,
                                   Largest);
                    }
                }
            }
        }
    }
    TcCheck(Mismatches == 0, "bitmap render target draws, %lu of %lu differ", Mismatches, Cases);
}

VOID
TcCompareGdiInterop(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    ComPtr<IDWriteGdiInterop> WantInterop = TcInterop(Want);
    ComPtr<IDWriteGdiInterop> GotInterop = TcInterop(Got);

    if (!WantInterop || !GotInterop)
        return;

    TcCompareLookups(Want, Got, WantInterop.Get(), GotInterop.Get());
    TcCompareHdcFaces(WantInterop.Get(), GotInterop.Get());
    TcCompareBitmapTargets(Want, Got, WantInterop.Get(), GotInterop.Get());
}
