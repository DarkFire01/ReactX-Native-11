/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares a DirectWrite DLL against the system one
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textcheck.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

typedef HRESULT (WINAPI *TcCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

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
    TcCheck(Expected->GetType() == Actual->GetType(), "%ls GetType want %d got %d", Name, Expected->GetType(),
            Actual->GetType());

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

    /* Recommended rendering and grid fit modes */
    {
        ComPtr<IDWriteFontFace3> Want3;
        ComPtr<IDWriteFontFace3> Got3;
        static const FLOAT Sizes[] = { 6.0f, 8.0f, 9.0f, 11.0f, 13.0f, 16.0f, 20.0f, 21.0f, 24.0f, 48.0f, 99.0f, 100.0f, 400.0f };
        static const FLOAT Dpis[] = { 96.0f, 144.0f, 192.0f };

        Mismatches = 0;
        if (SUCCEEDED(Expected->QueryInterface(IID_PPV_ARGS(&Want3))) && SUCCEEDED(Actual->QueryInterface(IID_PPV_ARGS(&Got3))))
        {
            for (FLOAT Size : Sizes)
            {
                for (FLOAT Dpi : Dpis)
                {
                    for (UINT32 Measuring = 0; Measuring < 3; Measuring++)
                    {
                        for (UINT32 Threshold = 0; Threshold < 2; Threshold++)
                        {
                            DWRITE_RENDERING_MODE1 WantMode = DWRITE_RENDERING_MODE1_DEFAULT;
                            DWRITE_RENDERING_MODE1 GotMode = DWRITE_RENDERING_MODE1_DEFAULT;
                            DWRITE_GRID_FIT_MODE WantFit = DWRITE_GRID_FIT_MODE_DEFAULT;
                            DWRITE_GRID_FIT_MODE GotFit = DWRITE_GRID_FIT_MODE_DEFAULT;

                            Want3->GetRecommendedRenderingMode(Size, Dpi, Dpi, nullptr, FALSE,
                                                               (DWRITE_OUTLINE_THRESHOLD)Threshold,
                                                               (DWRITE_MEASURING_MODE)Measuring, nullptr, &WantMode, &WantFit);
                            Got3->GetRecommendedRenderingMode(Size, Dpi, Dpi, nullptr, FALSE,
                                                              (DWRITE_OUTLINE_THRESHOLD)Threshold,
                                                              (DWRITE_MEASURING_MODE)Measuring, nullptr, &GotMode, &GotFit);
                            if ((WantMode != GotMode || WantFit != GotFit) && Mismatches++ < 4)
                                printf("  mode size %.0f dpi %.0f measuring %u threshold %u want %d/%d got %d/%d\n", Size, Dpi,
                                       Measuring, Threshold, WantMode, WantFit, GotMode, GotFit);
                        }
                    }
                }
            }
        }
        TcCheck(Mismatches == 0, "%ls GetRecommendedRenderingMode, %lu mismatches", Name, Mismatches);
    }
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

std::wstring
TcName(
    _In_opt_ IDWriteLocalizedStrings* Names)
{
    UINT32 Index = 0;
    BOOL Exists = FALSE;
    UINT32 Length = 0;
    std::wstring Text;

    if (Names == nullptr || Names->GetCount() == 0)
        return L"";
    Names->FindLocaleName(L"en-us", &Index, &Exists);
    if (!Exists)
        Index = 0;
    Names->GetStringLength(Index, &Length);
    Text.resize(Length + 1);
    Names->GetString(Index, &Text[0], Length + 1);
    Text.resize(Length);
    return Text;
}

std::wstring
TcDescribeFont(
    _In_ IDWriteFont* Font)
{
    ComPtr<IDWriteLocalizedStrings> Faces;
    WCHAR Line[256];

    Font->GetFaceNames(&Faces);
    swprintf_s(Line, L"%s w%d s%d st%d sim%d", TcName(Faces.Get()).c_str(), Font->GetWeight(), Font->GetStretch(),
               Font->GetStyle(), Font->GetSimulations());
    return Line;
}

static
VOID
TcCompareCollections(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    ComPtr<IDWriteFontCollection> WantCollection;
    ComPtr<IDWriteFontCollection> GotCollection;
    ULONG Shown = 0;
    static const struct
    {
        DWRITE_FONT_WEIGHT Weight;
        DWRITE_FONT_STRETCH Stretch;
        DWRITE_FONT_STYLE Style;
    } Requests[] =
    {
        { DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL },
        { DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL },
        { DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL },
        { DWRITE_FONT_WEIGHT_LIGHT, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_ITALIC },
        { DWRITE_FONT_WEIGHT_BLACK, DWRITE_FONT_STRETCH_CONDENSED, DWRITE_FONT_STYLE_OBLIQUE },
    };

    printf("system collection\n");
    if (FAILED(Want.Factory->GetSystemFontCollection(&WantCollection)))
        return;
    if (FAILED(Got.Factory->GetSystemFontCollection(&GotCollection)))
    {
        TcCheck(FALSE, "GetSystemFontCollection failed");
        return;
    }

    TcCheck(WantCollection->GetFontFamilyCount() == GotCollection->GetFontFamilyCount(), "family count want %u got %u",
            WantCollection->GetFontFamilyCount(), GotCollection->GetFontFamilyCount());

    for (UINT32 Family = 0; Family < WantCollection->GetFontFamilyCount(); Family++)
    {
        ComPtr<IDWriteFontFamily> WantFamily;
        ComPtr<IDWriteFontFamily> GotFamily;
        ComPtr<IDWriteLocalizedStrings> WantNames;
        ComPtr<IDWriteLocalizedStrings> GotNames;
        std::wstring Name;
        UINT32 Index;
        BOOL Exists;

        WantCollection->GetFontFamily(Family, &WantFamily);
        WantFamily->GetFamilyNames(&WantNames);
        Name = TcName(WantNames.Get());

        GotCollection->FindFamilyName(Name.c_str(), &Index, &Exists);
        TcCheck(Exists != FALSE, "family %ls missing", Name.c_str());
        if (!Exists)
            continue;
        if (Index != Family && Shown++ < 5)
            printf("  family %ls at %u, want %u\n", Name.c_str(), Index, Family);

        GotCollection->GetFontFamily(Index, &GotFamily);
        TcCheck(WantFamily->GetFontCount() == GotFamily->GetFontCount(), "%ls font count want %u got %u",
                Name.c_str(), WantFamily->GetFontCount(), GotFamily->GetFontCount());

        for (UINT32 Font = 0; Font < std::min(WantFamily->GetFontCount(), GotFamily->GetFontCount()); Font++)
        {
            ComPtr<IDWriteFont> A;
            ComPtr<IDWriteFont> B;

            WantFamily->GetFont(Font, &A);
            GotFamily->GetFont(Font, &B);
            std::wstring Expected = TcDescribeFont(A.Get());
            std::wstring Actual = TcDescribeFont(B.Get());
            TcCheck(Expected == Actual, "%ls font %u want [%ls] got [%ls]", Name.c_str(), Font, Expected.c_str(),
                    Actual.c_str());
        }

        for (const auto& Request : Requests)
        {
            ComPtr<IDWriteFont> A;
            ComPtr<IDWriteFont> B;

            if (FAILED(WantFamily->GetFirstMatchingFont(Request.Weight, Request.Stretch, Request.Style, &A)))
                continue;
            if (FAILED(GotFamily->GetFirstMatchingFont(Request.Weight, Request.Stretch, Request.Style, &B)))
            {
                TcCheck(FALSE, "%ls GetFirstMatchingFont failed", Name.c_str());
                continue;
            }
            std::wstring Expected = TcDescribeFont(A.Get());
            std::wstring Actual = TcDescribeFont(B.Get());
            TcCheck(Expected == Actual, "%ls match %d/%d/%d want [%ls] got [%ls]", Name.c_str(), Request.Weight,
                    Request.Stretch, Request.Style, Expected.c_str(), Actual.c_str());
        }
    }
}

/* Records what a layout draws */
class TcRecorder final : public IDWriteTextRenderer
{
public:
    std::vector<std::wstring> Calls;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWritePixelSnapping) || riid == __uuidof(IDWriteTextRenderer))
        {
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override
    {
        *isDisabled = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override
    {
        *transform = { 1, 0, 0, 1, 0, 0 };
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override
    {
        *pixelsPerDip = 1.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE, const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION* description, IUnknown*) override
    {
        WCHAR Line[160];
        FLOAT Width = 0.0f;

        for (UINT32 Index = 0; Index < run->glyphCount; Index++)
            Width += run->glyphAdvances[Index];
        swprintf_s(Line, L"run %.2f,%.2f size %.1f glyphs %u width %.2f text %u+%u", x, y, run->fontEmSize,
                   run->glyphCount, Width, description->textPosition, description->stringLength);
        Calls.push_back(Line);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT x, FLOAT y, const DWRITE_UNDERLINE* underline, IUnknown*) override
    {
        WCHAR Line[160];

        swprintf_s(Line, L"underline %.2f,%.2f width %.2f thick %.2f offset %.2f", x, y, underline->width,
                   underline->thickness, underline->offset);
        Calls.push_back(Line);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT x, FLOAT y, const DWRITE_STRIKETHROUGH* strike, IUnknown*) override
    {
        WCHAR Line[160];

        swprintf_s(Line, L"strike %.2f,%.2f width %.2f", x, y, strike->width);
        Calls.push_back(Line);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void* context, FLOAT x, FLOAT y, IDWriteInlineObject* object, BOOL, BOOL,
                                               IUnknown*) override
    {
        WCHAR Line[160];

        swprintf_s(Line, L"inline %.2f,%.2f", x, y);
        Calls.push_back(Line);
        return object->Draw(context, this, x, y, FALSE, FALSE, nullptr);
    }
};

static
std::vector<std::wstring>
TcDescribeLayout(
    _In_ TcSide& Side,
    _In_z_ PCWSTR Text,
    _In_z_ PCWSTR Family,
    _In_ FLOAT Size,
    _In_ FLOAT Width,
    _In_ FLOAT Height,
    _In_ INT Variant)
{
    std::vector<std::wstring> Lines;
    ComPtr<IDWriteTextFormat> Format;
    ComPtr<IDWriteTextLayout> Layout;
    ComPtr<IDWriteInlineObject> Sign;
    DWRITE_TEXT_METRICS Metrics;
    DWRITE_LINE_METRICS LineMetrics[64];
    UINT32 Count = 0;
    TcRecorder Recorder;
    WCHAR Line[256];

    if (FAILED(Side.Factory->CreateTextFormat(Family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, Size, L"en-us", &Format)))
    {
        Lines.push_back(L"CreateTextFormat failed");
        return Lines;
    }

    if (Variant & 1)
        Format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (Variant & 2)
    {
        DWRITE_TRIMMING Trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };

        Side.Factory->CreateEllipsisTrimmingSign(Format.Get(), &Sign);
        Format->SetTrimming(&Trimming, Sign.Get());
    }
    if (Variant & 4)
        Format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    if (Variant & 8)
        Format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    if (FAILED(Side.Factory->CreateTextLayout(Text, (UINT32)wcslen(Text), Format.Get(), Width, Height, &Layout)))
    {
        Lines.push_back(L"CreateTextLayout failed");
        return Lines;
    }
    if (Variant & 16)
    {
        DWRITE_TEXT_RANGE Range = { 2, 5 };

        Layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, Range);
        Layout->SetUnderline(TRUE, Range);
    }

    Layout->GetMetrics(&Metrics);
    swprintf_s(Line, L"metrics left %.2f top %.2f width %.2f wws %.2f height %.2f lines %u", Metrics.left, Metrics.top,
               Metrics.width, Metrics.widthIncludingTrailingWhitespace, Metrics.height, Metrics.lineCount);
    Lines.push_back(Line);

    Layout->GetLineMetrics(LineMetrics, 64, &Count);
    for (UINT32 Index = 0; Index < Count && Index < 64; Index++)
    {
        swprintf_s(Line, L"line %u len %u trail %u nl %u height %.2f base %.2f trim %d", Index, LineMetrics[Index].length,
                   LineMetrics[Index].trailingWhitespaceLength, LineMetrics[Index].newlineLength,
                   LineMetrics[Index].height, LineMetrics[Index].baseline, LineMetrics[Index].isTrimmed);
        Lines.push_back(Line);
    }

    Layout->Draw(nullptr, &Recorder, 10.0f, 20.0f);
    Lines.insert(Lines.end(), Recorder.Calls.begin(), Recorder.Calls.end());
    return Lines;
}

static
VOID
TcCompareLayouts(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    static const struct
    {
        PCWSTR Text;
        PCWSTR Family;
        FLOAT Size;
        FLOAT Width;
        FLOAT Height;
        INT Variant;
    } Cases[] =
    {
        { L"Settings", L"Segoe UI", 12.0f, 200.0f, 50.0f, 0 },
        { L"The quick brown fox jumps over the lazy dog", L"Segoe UI", 13.0f, 120.0f, 400.0f, 0 },
        { L"Line one\r\nLine two\nthree", L"Tahoma", 11.0f, 300.0f, 300.0f, 0 },
        { L"Trimmed text that will not fit in the box", L"Segoe UI", 14.0f, 100.0f, 30.0f, 3 },
        { L"Centered", L"Segoe UI", 16.0f, 150.0f, 60.0f, 12 },
        { L"Bold underline range test", L"Segoe UI", 12.0f, 400.0f, 40.0f, 16 },
        { L"Tabs\tand\tspaces   ", L"Segoe UI", 12.0f, 400.0f, 40.0f, 0 },
        { L"", L"Segoe UI", 12.0f, 100.0f, 40.0f, 0 },
        { L"Unknown family falls back", L"No Such Font", 12.0f, 400.0f, 40.0f, 0 },
        { L"Symbols ←→ ☺ and CJK 中文", L"Segoe UI", 12.0f, 400.0f, 40.0f, 0 },
    };

    printf("text layout\n");
    for (const auto& Case : Cases)
    {
        std::vector<std::wstring> Expected = TcDescribeLayout(Want, Case.Text, Case.Family, Case.Size, Case.Width,
                                                              Case.Height, Case.Variant);
        std::vector<std::wstring> Actual = TcDescribeLayout(Got, Case.Text, Case.Family, Case.Size, Case.Width,
                                                            Case.Height, Case.Variant);
        BOOLEAN Same = Expected == Actual;

        TcCheck(Same, "layout \"%ls\"", Case.Text);
        if (!Same)
        {
            for (SIZE_T Index = 0; Index < std::max(Expected.size(), Actual.size()); Index++)
            {
                PCWSTR A = Index < Expected.size() ? Expected[Index].c_str() : L"(none)";
                PCWSTR B = Index < Actual.size() ? Actual[Index].c_str() : L"(none)";

                printf("    %s want %ls\n", wcscmp(A, B) == 0 ? "  " : "!=", A);
                if (wcscmp(A, B) != 0)
                    printf("         got  %ls\n", B);
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

    if (Only == nullptr || _wcsicmp(Only, L"collection") == 0)
        TcCompareCollections(System, ReactX);
    if (Only == nullptr || _wcsicmp(Only, L"layout") == 0)
        TcCompareLayouts(System, ReactX);
    if (Only == nullptr || _wcsicmp(Only, L"gdi") == 0)
        TcCompareGdiInterop(System, ReactX);
    if (Only != nullptr && (_wcsicmp(Only, L"layout") == 0 || _wcsicmp(Only, L"gdi") == 0))
        Only = L"";
    if (Only != nullptr && _wcsicmp(Only, L"collection") == 0)
        Only = L"";

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
        TcCompareFaceExtras(WantFace.Get(), GotFace.Get(), Font);
        TcCompareRaster(System, ReactX, WantFace.Get(), GotFace.Get(), Font);
    }

    printf("%lu checks, %lu failed\n", TcResult.Checks, TcResult.Failures);
    return TcResult.Failures == 0 ? 0 : 1;
}
