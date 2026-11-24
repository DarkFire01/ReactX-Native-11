/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Walks the DirectWrite calls the Qt 6 font database and font engine make, on both runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d2d1.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

typedef HRESULT (WINAPI *DqCreateFactory)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);

/* One runtime's transcript of the walk: the font database part, then the font engine part */
struct DqLog
{
    std::vector<std::string> Lines[2];
    UINT32 Section = 0;

    VOID
    Add(
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...)
    {
        CHAR Text[512];
        va_list Arguments;

        va_start(Arguments, Format);
        vsprintf_s(Text, Format, Arguments);
        va_end(Arguments);
        Lines[Section].push_back(Text);
    }
};

/* Counts what Qt's path sink would build */
class DqSink final : public IDWriteGeometrySink
{
public:
    UINT32 Figures = 0;
    UINT32 Segments = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        *ppvObject = (riid == __uuidof(IUnknown) || riid == __uuidof(ID2D1SimplifiedGeometrySink)) ? this : nullptr;
        return *ppvObject != nullptr ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    void STDMETHODCALLTYPE SetFillMode(D2D1_FILL_MODE) override {}
    void STDMETHODCALLTYPE SetSegmentFlags(D2D1_PATH_SEGMENT) override {}
    void STDMETHODCALLTYPE BeginFigure(D2D1_POINT_2F, D2D1_FIGURE_BEGIN) override { Figures++; }
    void STDMETHODCALLTYPE AddLines(const D2D1_POINT_2F*, UINT32 pointsCount) override { Segments += pointsCount; }
    void STDMETHODCALLTYPE AddBeziers(const D2D1_BEZIER_SEGMENT*, UINT32 beziersCount) override { Segments += beziersCount; }
    void STDMETHODCALLTYPE EndFigure(D2D1_FIGURE_END) override {}
    HRESULT STDMETHODCALLTYPE Close() override { return S_OK; }
};

static
std::string
DqString(
    _In_opt_ IDWriteLocalizedStrings* Names,
    _In_z_ PCWSTR Locale)
{
    UINT32 Index = 0;
    BOOL Exists = FALSE;
    UINT32 Length = 0;
    std::wstring Wide;
    CHAR Narrow[256] = {};

    if (Names == nullptr || FAILED(Names->FindLocaleName(Locale, &Index, &Exists)) || !Exists)
        return "";
    if (FAILED(Names->GetStringLength(Index, &Length)))
        return "";
    Wide.resize(Length + 1);
    if (FAILED(Names->GetString(Index, &Wide[0], Length + 1)))
        return "";
    WideCharToMultiByte(CP_UTF8, 0, Wide.c_str(), -1, Narrow, sizeof(Narrow), nullptr, nullptr);
    return Narrow;
}

/* The face Qt's font engine gets for one requested font */
static
VOID
DqEngine(
    _In_ IDWriteFactory* Factory,
    _In_ IDWriteGdiInterop* Interop,
    _In_ HDC Dc,
    _In_z_ PCWSTR Family,
    _In_ LONG Weight,
    _In_ BYTE Italic,
    _In_ LONG PixelSize,
    _Inout_ DqLog& Log)
{
    LOGFONTW Request = {};
    HFONT Font;
    HGDIOBJ Old;
    ComPtr<IDWriteFontFace> Face;
    ComPtr<IDWriteFontFace2> Face2;
    ComPtr<IDWriteFontFace3> Face3;
    ComPtr<IDWriteFactory2> Factory2;
    ComPtr<IDWriteRenderingParams> Params;
    DWRITE_FONT_METRICS Metrics = {};
    HRESULT hr;

    Request.lfHeight = -PixelSize;
    Request.lfWeight = Weight;
    Request.lfItalic = Italic;
    Request.lfCharSet = DEFAULT_CHARSET;
    Request.lfOutPrecision = OUT_DEFAULT_PRECIS;
    Request.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(Request.lfFaceName, Family);
    Log.Add("engine %ls %ld %u %ld", Family, Weight, Italic, PixelSize);

    /* QWindowsFontDatabase::createEngine */
    Font = CreateFontIndirectW(&Request);
    Old = SelectObject(Dc, Font);
    hr = Interop->CreateFontFaceFromHdc(Dc, &Face);
    SelectObject(Dc, Old);
    DeleteObject(Font);
    Log.Add("  CreateFontFaceFromHdc %08lX %s", hr, Face ? "face" : "null");
    if (FAILED(hr) || !Face)
        return;

    if (SUCCEEDED(Face.As(&Face2)))
        Log.Add("  IsColorFont %d palette entries %u simulations %d", Face2->IsColorFont(), Face2->GetPaletteEntryCount(),
                Face->GetSimulations());
    else
        Log.Add("  no IDWriteFontFace2");

    /* QWindowsFontEngineDirectWrite constructor and collectMetrics */
    hr = Factory->CreateRenderingParams(&Params);
    Log.Add("  CreateRenderingParams %08lX geometry %d", hr, Params ? Params->GetPixelGeometry() : -1);
    Face->GetMetrics(&Metrics);
    Log.Add("  metrics upem %u asc %u desc %u gap %d cap %u x %u ul %d %u", Metrics.designUnitsPerEm, Metrics.ascent,
            Metrics.descent, Metrics.lineGap, Metrics.capHeight, Metrics.xHeight, Metrics.underlinePosition,
            Metrics.underlineThickness);
    {
        ComPtr<IDWriteFontFile> File;
        ComPtr<IDWriteFontFileLoader> Loader;
        ComPtr<IDWriteLocalFontFileLoader> Local;
        UINT32 Files = 1;
        const void* Key = nullptr;
        UINT32 KeySize = 0;
        UINT32 Length = 0;
        WCHAR Path[MAX_PATH] = L"";

        hr = Face->GetFiles(&Files, &File);
        if (SUCCEEDED(hr))
            hr = File->GetLoader(&Loader);
        if (SUCCEEDED(hr))
            hr = Loader.As(&Local);
        if (SUCCEEDED(hr))
            hr = File->GetReferenceKey(&Key, &KeySize);
        if (SUCCEEDED(hr))
            hr = Local->GetFilePathLengthFromKey(Key, KeySize, &Length);
        if (SUCCEEDED(hr))
            hr = Local->GetFilePathFromKey(Key, KeySize, Path, MAX_PATH);
        _wcslwr_s(Path);
        Log.Add("  file %08lX length %u %ls", hr, Length, Path);
    }
    {
        const void* Table = nullptr;
        UINT32 Size = 0;
        void* Context = nullptr;
        BOOL Exists = FALSE;

        hr = Face->TryGetFontTable(DWRITE_MAKE_OPENTYPE_TAG('h', 'h', 'e', 'a'), &Table, &Size, &Context, &Exists);
        Log.Add("  hhea %08lX exists %d size %u", hr, Exists, Size);
        if (SUCCEEDED(hr) && Exists)
            Face->ReleaseFontTable(Context);
    }

    /* stringToCMap and recalcAdvances */
    {
        static const UINT32 Text[] = { 'Q', 't', ' ', 'O', 'B', 'S', 0xE9, 0x4E2D, 0x1F600 };
        UINT16 Glyphs[_countof(Text)] = {};
        DWRITE_GLYPH_METRICS Design[_countof(Text)] = {};
        DWRITE_GLYPH_METRICS Gdi[_countof(Text)] = {};
        std::string Line;

        hr = Face->GetGlyphIndices(Text, _countof(Text), Glyphs);
        Line = "  glyphs";
        for (UINT16 Glyph : Glyphs)
            Line += " " + std::to_string(Glyph);
        Log.Add("%s hr %08lX", Line.c_str(), hr);

        hr = Face->GetDesignGlyphMetrics(Glyphs, _countof(Glyphs), Design, FALSE);
        Line = "  design";
        for (const auto& Glyph : Design)
            Line += " " + std::to_string(Glyph.advanceWidth);
        Log.Add("%s hr %08lX", Line.c_str(), hr);

        hr = Face->GetGdiCompatibleGlyphMetrics(static_cast<FLOAT>(PixelSize), 1.0f, nullptr, FALSE, Glyphs, _countof(Glyphs),
                                                Gdi, FALSE);
        Line = "  gdi classic";
        for (const auto& Glyph : Gdi)
            Line += " " + std::to_string(Glyph.advanceWidth);
        Log.Add("%s hr %08lX", Line.c_str(), hr);

        /* addGlyphsToPath and getUnscaledGlyph */
        {
            DqSink Sink;
            FLOAT Advances[_countof(Glyphs)] = {};
            DWRITE_GLYPH_OFFSET Offsets[_countof(Glyphs)] = {};

            hr = Face->GetGlyphRunOutline(static_cast<FLOAT>(PixelSize), Glyphs, Advances, Offsets, _countof(Glyphs), FALSE,
                                          FALSE, &Sink);
            Log.Add("  outline %08lX figures %u segments %u", hr, Sink.Figures, Sink.Segments);
        }

        /* imageForGlyph, through IDWriteFactory2 as Qt prefers */
        if (SUCCEEDED(Factory->QueryInterface(IID_PPV_ARGS(&Factory2))))
        {
            for (UINT32 Index = 0; Index < 3; Index++)
            {
                DWRITE_GLYPH_RUN Run = {};
                FLOAT Advance = 0.0f;
                DWRITE_GLYPH_OFFSET Offset = {};
                DWRITE_MATRIX Transform = { 1.0f, 0.0f, 0.0f, 1.0f, 0.25f, 0.0f };
                ComPtr<IDWriteGlyphRunAnalysis> Analysis;
                ComPtr<IDWriteColorGlyphRunEnumerator> Layers;
                DWRITE_RENDERING_MODE Mode = PixelSize > 16 ? DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC :
                                                              DWRITE_RENDERING_MODE_GDI_CLASSIC;
                DWRITE_MEASURING_MODE Measuring = PixelSize > 16 ? DWRITE_MEASURING_MODE_NATURAL :
                                                                   DWRITE_MEASURING_MODE_GDI_CLASSIC;
                RECT Bounds = {};

                Run.fontFace = Face.Get();
                Run.fontEmSize = static_cast<FLOAT>(PixelSize);
                Run.glyphCount = 1;
                Run.glyphIndices = &Glyphs[Index];
                Run.glyphAdvances = &Advance;
                Run.glyphOffsets = &Offset;
                hr = Factory2->CreateGlyphRunAnalysis(&Run, &Transform, Mode, Measuring, DWRITE_GRID_FIT_MODE_DEFAULT,
                                                      DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE, 0.0f, 0.0f, &Analysis);
                if (SUCCEEDED(hr))
                    Analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &Bounds);
                Log.Add("  glyph %u analysis %08lX bounds %ld %ld %ld %ld", Glyphs[Index], hr, Bounds.left, Bounds.top,
                        Bounds.right, Bounds.bottom);
                if (SUCCEEDED(hr) && !IsRectEmpty(&Bounds))
                {
                    std::vector<BYTE> Alpha(static_cast<SIZE_T>(Bounds.right - Bounds.left) * (Bounds.bottom - Bounds.top) * 3);
                    ULONG Sum = 0;

                    hr = Analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &Bounds, Alpha.data(),
                                                      static_cast<UINT32>(Alpha.size()));
                    for (BYTE Value : Alpha)
                        Sum += Value;
                    Log.Add("    alpha %08lX coverage %lu", hr, Sum);
                }

                hr = Factory2->TranslateColorGlyphRun(0.0f, 0.0f, &Run, nullptr, Measuring, nullptr, 0, &Layers);
                Log.Add("    color %08lX %s", hr, Layers ? "layers" : "null");
            }
        }
        else
        {
            Log.Add("  no IDWriteFactory2");
        }
    }

    /* initFontInfo and createHFONT */
    if (SUCCEEDED(Face.As(&Face3)))
    {
        ComPtr<IDWriteLocalizedStrings> Names;

        hr = Face3->GetFaceNames(&Names);
        Log.Add("  face names %08lX [%s]", hr, DqString(Names.Get(), L"en-us").c_str());
    }
    {
        LOGFONTW Back = {};

        hr = Interop->ConvertFontFaceToLOGFONT(Face.Get(), &Back);
        Log.Add("  ConvertFontFaceToLOGFONT %08lX [%ls] %ld %u %u", hr, Back.lfFaceName, Back.lfWeight, Back.lfItalic,
                Back.lfCharSet);
    }
}

static
DqLog
DqWalk(
    _In_z_ PCWSTR Path)
{
    DqLog Log;
    HMODULE Module = LoadLibraryExW(Path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    DqCreateFactory Create;
    ComPtr<IDWriteFactory> Factory;
    ComPtr<IDWriteFactory6> Factory6;
    ComPtr<IDWriteGdiInterop> Interop;
    WCHAR DefaultLocale[LOCALE_NAME_MAX_LENGTH] = L"en-us";
    HDC Dc;
    HRESULT hr;

    if (Module == nullptr)
    {
        Log.Add("cannot load %ls (%lu)", Path, GetLastError());
        return Log;
    }
    Create = reinterpret_cast<DqCreateFactory>(GetProcAddress(Module, "DWriteCreateFactory"));
    hr = Create != nullptr ? Create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                    reinterpret_cast<IUnknown**>(Factory.GetAddressOf())) : E_NOINTERFACE;
    Log.Add("DWriteCreateFactory %08lX", hr);
    if (FAILED(hr))
        return Log;

    hr = Factory->GetGdiInterop(&Interop);
    Log.Add("GetGdiInterop %08lX %s", hr, Interop ? "interop" : "null");

    /* QWindowsDirectWriteFontDatabase::populateFontDatabase */
    GetUserDefaultLocaleName(DefaultLocale, LOCALE_NAME_MAX_LENGTH);
    hr = Factory.As(&Factory6);
    Log.Add("IDWriteFactory6 %08lX", hr);
    if (SUCCEEDED(hr))
    {
        ComPtr<IDWriteFontCollection2> Collection;

        hr = Factory6->GetSystemFontCollection(FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &Collection);
        Log.Add("GetSystemFontCollection typographic %08lX families %u", hr, Collection ? Collection->GetFontFamilyCount() : 0);
        for (UINT32 Family = 0; Collection && Family < Collection->GetFontFamilyCount() && Family < 12; Family++)
        {
            ComPtr<IDWriteFontFamily2> Entry;
            ComPtr<IDWriteLocalizedStrings> Names;
            ComPtr<IDWriteFontList> Fonts;

            if (FAILED(Collection->GetFontFamily(Family, &Entry)) || FAILED(Entry->GetFamilyNames(&Names)))
                continue;
            hr = Entry->GetMatchingFonts(DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                         &Fonts);
            Log.Add("  family %s: matching %08lX %u fonts", DqString(Names.Get(), L"en-us").c_str(), hr,
                    Fonts ? Fonts->GetFontCount() : 0);
            for (UINT32 Index = 0; Fonts && Index < Fonts->GetFontCount(); Index++)
            {
                ComPtr<IDWriteFont> Font;
                ComPtr<IDWriteFont1> Font1;
                ComPtr<IDWriteLocalizedStrings> Faces;

                if (FAILED(Fonts->GetFont(Index, &Font)) || FAILED(Font.As(&Font1)))
                {
                    Log.Add("    font %u has no IDWriteFont1", Index);
                    continue;
                }
                Font1->GetFaceNames(&Faces);
                Log.Add("    font %s w%d s%d st%d mono %d", DqString(Faces.Get(), L"en-us").c_str(), Font1->GetWeight(),
                        Font1->GetStretch(), Font1->GetStyle(), Font1->IsMonospacedFont());
            }
        }
    }
    Log.Section = 1;
    if (!Interop)
        return Log;

    /* Fonts an OBS window asks for at startup */
    Dc = CreateCompatibleDC(nullptr);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Segoe UI", FW_NORMAL, 0, 12, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Segoe UI", FW_BOLD, 0, 16, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Segoe UI", FW_NORMAL, 1, 24, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Open Sans", FW_NORMAL, 0, 13, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Consolas", FW_NORMAL, 0, 13, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"Segoe UI Emoji", FW_NORMAL, 0, 20, Log);
    DqEngine(Factory.Get(), Interop.Get(), Dc, L"MS Shell Dlg 2", FW_NORMAL, 0, 11, Log);
    DeleteDC(Dc);

    /* The QIcon::fromTheme path that crashed: a LOGFONT lookup and its face */
    {
        LOGFONTW Request = {};
        ComPtr<IDWriteFont> Font;
        ComPtr<IDWriteFontFace> Face;

        Request.lfWeight = FW_NORMAL;
        Request.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(Request.lfFaceName, L"Segoe UI");
        hr = Interop->CreateFontFromLOGFONT(&Request, &Font);
        if (SUCCEEDED(hr))
            hr = Font->CreateFontFace(&Face);
        Log.Add("CreateFontFromLOGFONT Segoe UI %08lX %s", hr, Face ? "face" : "null");
    }
    return Log;
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    WCHAR SystemPath[MAX_PATH];
    DqLog Want;
    DqLog Got;
    ULONG Different = 0;

    if (argc < 2)
    {
        printf("dwriteqt <dwrite.dll to test>\n");
        return 2;
    }

    GetSystemDirectoryW(SystemPath, MAX_PATH);
    wcscat_s(SystemPath, L"\\dwrite.dll");
    Want = DqWalk(SystemPath);
    Got = DqWalk(argv[1]);

    for (UINT32 Section = 0; Section < 2; Section++)
    {
        const auto& Expected = Want.Lines[Section];
        const auto& Actual = Got.Lines[Section];

        printf(Section == 0 ? "font database\n" : "font engine\n");
        for (SIZE_T Index = 0; Index < std::max(Expected.size(), Actual.size()); Index++)
        {
            PCSTR A = Index < Expected.size() ? Expected[Index].c_str() : "(none)";
            PCSTR B = Index < Actual.size() ? Actual[Index].c_str() : "(none)";

            if (strcmp(A, B) == 0)
            {
                printf("   %s\n", A);
                continue;
            }
            Different++;
            printf("!= %s\n   got %s\n", A, B);
        }
    }
    printf("%Iu steps, %lu differ\n", Want.Lines[0].size() + Want.Lines[1].size(), Different);
    return Different == 0 ? 0 : 1;
}
