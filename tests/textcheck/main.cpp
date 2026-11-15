/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares a DirectWrite DLL against the system one
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI *TcCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

struct TcSide
{
    PCWSTR Name;
    HMODULE Module;
    ComPtr<IDWriteFactory> Factory;
};

struct TcTotals
{
    ULONG Checks;
    ULONG Failures;
};

static TcTotals TcResult;
static WCHAR TcDumpDirectory[MAX_PATH];

static const PCWSTR TcFonts[] =
{
    L"segoeui.ttf", L"segoeuib.ttf", L"arial.ttf", L"tahoma.ttf", L"times.ttf", L"consola.ttf",
    L"cour.ttf", L"verdana.ttf", L"calibri.ttf", L"cambria.ttc", L"seguisym.ttf", L"segmdl2.ttf",
};

static const PCWSTR TcSample = L"Hamburgefonstiv 0123 Wg";

static
VOID
TcCheck(
    _In_ BOOLEAN Condition,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    va_list Arguments;

    TcResult.Checks++;
    if (Condition)
        return;

    TcResult.Failures++;
    va_start(Arguments, Format);
    printf("  FAIL ");
    vprintf(Format, Arguments);
    printf("\n");
    va_end(Arguments);
}

static
BOOLEAN
TcLoad(
    _Inout_ TcSide& Side,
    _In_z_ PCWSTR Path)
{
    TcCreateFactory Create;
    HRESULT hr;

    Side.Module = LoadLibraryExW(Path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Side.Module == nullptr)
    {
        printf("cannot load %ls (%lu)\n", Path, GetLastError());
        return FALSE;
    }

    Create = reinterpret_cast<TcCreateFactory>(GetProcAddress(Side.Module, "DWriteCreateFactory"));
    if (Create == nullptr)
    {
        printf("%ls has no DWriteCreateFactory\n", Path);
        return FALSE;
    }

    hr = Create(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(Side.Factory.GetAddressOf()));
    if (FAILED(hr))
    {
        printf("%ls: DWriteCreateFactory failed 0x%08lX\n", Side.Name, hr);
        return FALSE;
    }
    return TRUE;
}

static
HRESULT
TcOpenFace(
    _In_ TcSide& Side,
    _In_z_ PCWSTR FileName,
    _Outptr_ IDWriteFontFace** Face)
{
    WCHAR Path[MAX_PATH];
    ComPtr<IDWriteFontFile> File;
    BOOL Supported;
    DWRITE_FONT_FILE_TYPE FileType;
    DWRITE_FONT_FACE_TYPE FaceType;
    UINT32 Faces;
    HRESULT hr;

    *Face = nullptr;
    GetWindowsDirectoryW(Path, MAX_PATH);
    wcscat_s(Path, L"\\Fonts\\");
    wcscat_s(Path, FileName);

    hr = Side.Factory->CreateFontFileReference(Path, nullptr, &File);
    if (FAILED(hr))
        return hr;
    hr = File->Analyze(&Supported, &FileType, &FaceType, &Faces);
    if (FAILED(hr))
        return hr;
    if (!Supported)
        return DWRITE_E_FILEFORMAT;

    IDWriteFontFile* Files[] = { File.Get() };
    return Side.Factory->CreateFontFace(FaceType, 1, Files, 0, DWRITE_FONT_SIMULATIONS_NONE, Face);
}

static
VOID
TcCompareFace(
    _In_ IDWriteFontFace* Expected,
    _In_ IDWriteFontFace* Actual,
    _In_z_ PCWSTR Name)
{
    ComPtr<IDWriteFontFace1> Expected1;
    ComPtr<IDWriteFontFace1> Actual1;
    DWRITE_FONT_METRICS1 Want = {};
    DWRITE_FONT_METRICS1 Got = {};
    std::vector<UINT32> Codes;
    std::vector<UINT16> WantGlyphs;
    std::vector<UINT16> GotGlyphs;
    std::vector<DWRITE_GLYPH_METRICS> WantMetrics;
    std::vector<DWRITE_GLYPH_METRICS> GotMetrics;
    ULONG Mismatches = 0;

    Expected->QueryInterface(IID_PPV_ARGS(&Expected1));
    Actual->QueryInterface(IID_PPV_ARGS(&Actual1));
    if (Expected1 && Actual1)
    {
        Expected1->GetMetrics(&Want);
        Actual1->GetMetrics(&Got);
    }
    else
    {
        Expected->GetMetrics(reinterpret_cast<DWRITE_FONT_METRICS*>(&Want));
        Actual->GetMetrics(reinterpret_cast<DWRITE_FONT_METRICS*>(&Got));
    }

#define TC_FIELD(Field) TcCheck(Want.Field == Got.Field, "%ls metrics." #Field " want %d got %d", Name, \
                                static_cast<INT>(Want.Field), static_cast<INT>(Got.Field))
    TC_FIELD(designUnitsPerEm);
    TC_FIELD(ascent);
    TC_FIELD(descent);
    TC_FIELD(lineGap);
    TC_FIELD(capHeight);
    TC_FIELD(xHeight);
    TC_FIELD(underlinePosition);
    TC_FIELD(underlineThickness);
    TC_FIELD(strikethroughPosition);
    TC_FIELD(strikethroughThickness);
    if (Expected1 && Actual1)
    {
        TC_FIELD(glyphBoxLeft);
        TC_FIELD(glyphBoxTop);
        TC_FIELD(glyphBoxRight);
        TC_FIELD(glyphBoxBottom);
        TC_FIELD(subscriptPositionX);
        TC_FIELD(subscriptPositionY);
        TC_FIELD(subscriptSizeX);
        TC_FIELD(subscriptSizeY);
        TC_FIELD(superscriptPositionX);
        TC_FIELD(superscriptPositionY);
        TC_FIELD(superscriptSizeX);
        TC_FIELD(superscriptSizeY);
        TC_FIELD(hasTypographicMetrics);
    }
#undef TC_FIELD

    TcCheck(Expected->GetGlyphCount() == Actual->GetGlyphCount(), "%ls glyph count want %u got %u", Name,
            Expected->GetGlyphCount(), Actual->GetGlyphCount());
    TcCheck(Expected->IsSymbolFont() == Actual->IsSymbolFont(), "%ls IsSymbolFont", Name);

    for (UINT32 Code = 0x20; Code < 0x3000; Code++)
        Codes.push_back(Code);
    Codes.push_back(0x1F600);
    Codes.push_back(0xF020);

    WantGlyphs.resize(Codes.size());
    GotGlyphs.resize(Codes.size());
    Expected->GetGlyphIndices(Codes.data(), static_cast<UINT32>(Codes.size()), WantGlyphs.data());
    Actual->GetGlyphIndices(Codes.data(), static_cast<UINT32>(Codes.size()), GotGlyphs.data());
    for (SIZE_T Index = 0; Index < Codes.size(); Index++)
    {
        if (WantGlyphs[Index] != GotGlyphs[Index] && Mismatches++ < 4)
            printf("  glyph for U+%04X want %u got %u\n", Codes[Index], WantGlyphs[Index], GotGlyphs[Index]);
    }
    TcCheck(Mismatches == 0, "%ls GetGlyphIndices, %lu mismatches", Name, Mismatches);

    WantMetrics.resize(WantGlyphs.size());
    GotMetrics.resize(WantGlyphs.size());
    Expected->GetDesignGlyphMetrics(WantGlyphs.data(), static_cast<UINT32>(WantGlyphs.size()), WantMetrics.data(), FALSE);
    Actual->GetDesignGlyphMetrics(WantGlyphs.data(), static_cast<UINT32>(WantGlyphs.size()), GotMetrics.data(), FALSE);
    Mismatches = 0;
    for (SIZE_T Index = 0; Index < WantGlyphs.size(); Index++)
    {
        if (memcmp(&WantMetrics[Index], &GotMetrics[Index], sizeof(WantMetrics[Index])) != 0 && Mismatches++ < 4)
            printf("  design metrics of glyph %u: want %u %d %d %u %d %d, got %u %d %d %u %d %d\n", WantGlyphs[Index],
                   WantMetrics[Index].advanceWidth, WantMetrics[Index].leftSideBearing, WantMetrics[Index].rightSideBearing,
                   WantMetrics[Index].advanceHeight, WantMetrics[Index].topSideBearing, WantMetrics[Index].bottomSideBearing,
                   GotMetrics[Index].advanceWidth, GotMetrics[Index].leftSideBearing, GotMetrics[Index].rightSideBearing,
                   GotMetrics[Index].advanceHeight, GotMetrics[Index].topSideBearing, GotMetrics[Index].bottomSideBearing);
    }
    TcCheck(Mismatches == 0, "%ls GetDesignGlyphMetrics, %lu mismatches", Name, Mismatches);
}

/* Writes an alpha texture as a PGM, three bytes per pixel becoming three columns */
static
VOID
TcDump(
    _In_z_ PCWSTR Tag,
    _In_ const RECT& Bounds,
    _In_ const std::vector<BYTE>& Pixels,
    _In_ UINT32 BytesPerPixel)
{
    WCHAR Path[MAX_PATH];
    FILE* File;
    UINT32 Width = (Bounds.right - Bounds.left) * BytesPerPixel;
    UINT32 Height = Bounds.bottom - Bounds.top;

    if (TcDumpDirectory[0] == UNICODE_NULL || Width == 0 || Height == 0)
        return;

    swprintf_s(Path, L"%s\\%s.pgm", TcDumpDirectory, Tag);
    if (_wfopen_s(&File, Path, L"wb") != 0)
        return;
    fprintf(File, "P5\n%u %u\n255\n", Width, Height);
    fwrite(Pixels.data(), 1, Pixels.size(), File);
    fclose(File);
}

static
HRESULT
TcRender(
    _In_ IDWriteFactory* Factory,
    _In_ IDWriteFontFace* Face,
    _In_ FLOAT EmSize,
    _In_ DWRITE_RENDERING_MODE Mode,
    _In_ DWRITE_TEXTURE_TYPE Texture,
    _Out_ RECT* Bounds,
    _Inout_ std::vector<BYTE>& Pixels)
{
    std::vector<UINT32> Codes;
    std::vector<UINT16> Glyphs;
    std::vector<FLOAT> Advances;
    std::vector<DWRITE_GLYPH_METRICS> Metrics;
    DWRITE_FONT_METRICS FontMetrics;
    DWRITE_GLYPH_RUN Run = {};
    ComPtr<IDWriteGlyphRunAnalysis> Analysis;
    HRESULT hr;

    for (PCWSTR Char = TcSample; *Char != UNICODE_NULL; Char++)
        Codes.push_back(*Char);
    Glyphs.resize(Codes.size());
    Advances.resize(Codes.size());
    Metrics.resize(Codes.size());
    Face->GetGlyphIndices(Codes.data(), static_cast<UINT32>(Codes.size()), Glyphs.data());
    Face->GetDesignGlyphMetrics(Glyphs.data(), static_cast<UINT32>(Glyphs.size()), Metrics.data(), FALSE);
    Face->GetMetrics(&FontMetrics);
    for (SIZE_T Index = 0; Index < Glyphs.size(); Index++)
        Advances[Index] = Metrics[Index].advanceWidth * EmSize / FontMetrics.designUnitsPerEm;

    Run.fontFace = Face;
    Run.fontEmSize = EmSize;
    Run.glyphCount = static_cast<UINT32>(Glyphs.size());
    Run.glyphIndices = Glyphs.data();
    Run.glyphAdvances = Advances.data();

    hr = Factory->CreateGlyphRunAnalysis(&Run, 1.0f, nullptr, Mode, DWRITE_MEASURING_MODE_NATURAL, 0.25f, 20.0f,
                                         &Analysis);
    if (FAILED(hr))
        return hr;

    hr = Analysis->GetAlphaTextureBounds(Texture, Bounds);
    if (FAILED(hr))
        return hr;

    Pixels.assign(static_cast<SIZE_T>(Bounds->right - Bounds->left) * (Bounds->bottom - Bounds->top) *
                  (Texture == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1), 0);
    if (Pixels.empty())
        return S_OK;
    return Analysis->CreateAlphaTexture(Texture, Bounds, Pixels.data(), static_cast<UINT32>(Pixels.size()));
}

static
VOID
TcCompareRaster(
    _In_ TcSide& Want,
    _In_ TcSide& Got,
    _In_ IDWriteFontFace* WantFace,
    _In_ IDWriteFontFace* GotFace,
    _In_z_ PCWSTR Name)
{
    static const struct
    {
        DWRITE_RENDERING_MODE Mode;
        DWRITE_TEXTURE_TYPE Texture;
        PCWSTR Tag;
    } Cases[] =
    {
        { DWRITE_RENDERING_MODE_ALIASED, DWRITE_TEXTURE_ALIASED_1x1, L"aliased" },
        { DWRITE_RENDERING_MODE_GDI_CLASSIC, DWRITE_TEXTURE_CLEARTYPE_3x1, L"gdiclassic" },
        { DWRITE_RENDERING_MODE_GDI_NATURAL, DWRITE_TEXTURE_CLEARTYPE_3x1, L"gdinatural" },
        { DWRITE_RENDERING_MODE_NATURAL, DWRITE_TEXTURE_CLEARTYPE_3x1, L"natural" },
        { DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, DWRITE_TEXTURE_CLEARTYPE_3x1, L"symmetric" },
    };
    static const FLOAT Sizes[] = { 9.0f, 11.0f, 12.0f, 13.333f, 16.0f, 20.0f, 32.0f };

    for (const auto& Case : Cases)
    {
        for (FLOAT Size : Sizes)
        {
            RECT WantBounds = {};
            RECT GotBounds = {};
            std::vector<BYTE> WantPixels;
            std::vector<BYTE> GotPixels;
            HRESULT WantResult = TcRender(Want.Factory.Get(), WantFace, Size, Case.Mode, Case.Texture, &WantBounds, WantPixels);
            HRESULT GotResult = TcRender(Got.Factory.Get(), GotFace, Size, Case.Mode, Case.Texture, &GotBounds, GotPixels);
            UINT32 Depth = Case.Texture == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
            ULONG Different = 0;
            ULONG Largest = 0;

            if (FAILED(WantResult))
                continue;
            TcCheck(SUCCEEDED(GotResult), "%ls %ls %.1f: render failed 0x%08lX", Name, Case.Tag, Size, GotResult);
            if (FAILED(GotResult))
                continue;

            TcCheck(EqualRect(&WantBounds, &GotBounds) != FALSE, "%ls %ls %.1f: bounds want %ld,%ld,%ld,%ld got %ld,%ld,%ld,%ld",
                    Name, Case.Tag, Size, WantBounds.left, WantBounds.top, WantBounds.right, WantBounds.bottom,
                    GotBounds.left, GotBounds.top, GotBounds.right, GotBounds.bottom);

            /* Compare over the expected bounds */
            for (LONG Y = WantBounds.top; Y < WantBounds.bottom; Y++)
            {
                for (LONG X = WantBounds.left; X < WantBounds.right; X++)
                {
                    for (UINT32 Channel = 0; Channel < Depth; Channel++)
                    {
                        BYTE A = WantPixels[((Y - WantBounds.top) * (WantBounds.right - WantBounds.left) +
                                             (X - WantBounds.left)) * Depth + Channel];
                        BYTE B = 0;

                        if (X >= GotBounds.left && X < GotBounds.right && Y >= GotBounds.top && Y < GotBounds.bottom)
                            B = GotPixels[((Y - GotBounds.top) * (GotBounds.right - GotBounds.left) +
                                           (X - GotBounds.left)) * Depth + Channel];
                        if (A != B)
                        {
                            Different++;
                            Largest = std::max(Largest, static_cast<ULONG>(abs(A - B)));
                        }
                    }
                }
            }

            TcCheck(Different == 0, "%ls %ls %.1f: %lu bytes differ, largest %lu", Name, Case.Tag, Size, Different,
                    Largest);

            if (Different != 0)
            {
                WCHAR Tag[128];

                swprintf_s(Tag, L"%s_%s_%.0f_want", Name, Case.Tag, Size * 10);
                TcDump(Tag, WantBounds, WantPixels, Depth);
                swprintf_s(Tag, L"%s_%s_%.0f_got", Name, Case.Tag, Size * 10);
                TcDump(Tag, GotBounds, GotPixels, Depth);
            }
        }
    }
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    TcSide System = { L"system" };
    TcSide ReactX = { L"reactx" };
    WCHAR SystemPath[MAX_PATH];
    PCWSTR Only = nullptr;

    if (argc < 2)
    {
        printf("textcheck <dwrite.dll to test> [--dump folder] [--font file]\n");
        return 2;
    }

    for (int Index = 2; Index + 1 < argc; Index += 2)
    {
        if (_wcsicmp(argv[Index], L"--dump") == 0)
        {
            wcscpy_s(TcDumpDirectory, argv[Index + 1]);
            CreateDirectoryW(TcDumpDirectory, nullptr);
        }
        else if (_wcsicmp(argv[Index], L"--font") == 0)
        {
            Only = argv[Index + 1];
        }
    }

    GetSystemDirectoryW(SystemPath, MAX_PATH);
    wcscat_s(SystemPath, L"\\dwrite.dll");
    if (!TcLoad(System, SystemPath) || !TcLoad(ReactX, argv[1]))
        return 1;

    for (PCWSTR Font : TcFonts)
    {
        ComPtr<IDWriteFontFace> WantFace;
        ComPtr<IDWriteFontFace> GotFace;
        HRESULT hr;

        if (Only != nullptr && _wcsicmp(Only, Font) != 0)
            continue;
        if (FAILED(TcOpenFace(System, Font, &WantFace)))
            continue;

        printf("%ls\n", Font);
        hr = TcOpenFace(ReactX, Font, &GotFace);
        TcCheck(SUCCEEDED(hr), "%ls: CreateFontFace failed 0x%08lX", Font, hr);
        if (FAILED(hr))
            continue;

        TcCompareFace(WantFace.Get(), GotFace.Get(), Font);
        TcCompareRaster(System, ReactX, WantFace.Get(), GotFace.Get(), Font);
    }

    printf("%lu checks, %lu failed\n", TcResult.Checks, TcResult.Failures);
    return TcResult.Failures == 0 ? 0 : 1;
}
