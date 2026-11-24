/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares the smaller factory entry points against the system
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textcheck.hpp"

#include <cstdio>

static
std::vector<BYTE>
TcReadFont(
    _In_z_ PCWSTR FileName)
{
    WCHAR Path[MAX_PATH];
    std::vector<BYTE> Bytes;
    FILE* File;
    long Size;

    GetWindowsDirectoryW(Path, MAX_PATH);
    wcscat_s(Path, L"\\Fonts\\");
    wcscat_s(Path, FileName);
    if (_wfopen_s(&File, Path, L"rb") != 0)
        return Bytes;
    fseek(File, 0, SEEK_END);
    Size = ftell(File);
    fseek(File, 0, SEEK_SET);
    Bytes.resize(Size > 0 ? Size : 0);
    if (!Bytes.empty())
        fread(Bytes.data(), 1, Bytes.size(), File);
    fclose(File);
    return Bytes;
}

/* Hands out a fixed list of local files */
class TcFileEnumerator final : public IDWriteFontFileEnumerator
{
public:
    TcFileEnumerator(
        _In_ IDWriteFactory* Owner) : Factory(Owner), Next(0) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileEnumerator))
        {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&Count); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG Left = InterlockedDecrement(&Count);

        if (Left == 0)
            delete this;
        return Left;
    }

    HRESULT STDMETHODCALLTYPE MoveNext(BOOL* hasCurrentFile) override
    {
        static const PCWSTR Names[] = { L"arial.ttf", L"arialbd.ttf", L"cambria.ttc", L"tahoma.ttf" };

        Current.Reset();
        *hasCurrentFile = FALSE;
        if (Next >= _countof(Names))
            return S_OK;

        WCHAR Path[MAX_PATH];
        GetWindowsDirectoryW(Path, MAX_PATH);
        wcscat_s(Path, L"\\Fonts\\");
        wcscat_s(Path, Names[Next++]);
        HRESULT hr = Factory->CreateFontFileReference(Path, nullptr, &Current);
        *hasCurrentFile = SUCCEEDED(hr);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentFontFile(IDWriteFontFile** fontFile) override
    {
        *fontFile = Current.Get();
        if (*fontFile != nullptr)
            (*fontFile)->AddRef();
        return *fontFile != nullptr ? S_OK : E_FAIL;
    }

private:
    LONG Count = 1;
    IDWriteFactory* Factory;
    UINT32 Next;
    ComPtr<IDWriteFontFile> Current;
};

class TcCollectionLoader final : public IDWriteFontCollectionLoader
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontCollectionLoader))
        {
            *ppvObject = this;
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE CreateEnumeratorFromKey(IDWriteFactory* factory, const void*, UINT32,
                                                      IDWriteFontFileEnumerator** fontFileEnumerator) override
    {
        *fontFileEnumerator = new TcFileEnumerator(factory);
        return S_OK;
    }
};

static
std::wstring
TcDescribeCollection(
    _In_ HRESULT Result,
    _In_opt_ IDWriteFontCollection* Collection)
{
    WCHAR Line[64];
    std::wstring Text;

    swprintf_s(Line, L"hr %08lX", Result);
    Text = Line;
    if (FAILED(Result) || Collection == nullptr)
        return Text;

    for (UINT32 Index = 0; Index < Collection->GetFontFamilyCount(); Index++)
    {
        ComPtr<IDWriteFontFamily> Family;
        ComPtr<IDWriteLocalizedStrings> Names;

        Collection->GetFontFamily(Index, &Family);
        Family->GetFamilyNames(&Names);
        swprintf_s(Line, L" | %u fonts ", Family->GetFontCount());
        Text += Line + TcName(Names.Get());
    }
    return Text;
}

static
VOID
TcCompareContainers(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    ComPtr<IDWriteFactory5> Want5;
    ComPtr<IDWriteFactory5> Got5;
    std::vector<BYTE> Arial = TcReadFont(L"arial.ttf");
    BYTE Woff[48] = { 'w', 'O', 'F', 'F' };
    BYTE Woff2[48] = { 'w', 'O', 'F', '2' };

    if (FAILED(Want.Factory.As(&Want5)) || FAILED(Got.Factory.As(&Got5)))
        return;

    const struct
    {
        const void* Data;
        UINT32 Size;
        PCSTR Tag;
    } Cases[] =
    {
        { Arial.data(), static_cast<UINT32>(Arial.size()), "ttf" }, { Woff, 48, "woff" }, { Woff, 44, "woff 44" },
        { Woff, 43, "woff 43" }, { Woff2, 48, "woff2" }, { Woff2, 47, "woff2 47" }, { Woff, 3, "short" },
        { nullptr, 0, "empty" },
    };

    for (const auto& Case : Cases)
    {
        DWRITE_CONTAINER_TYPE A = Want5->AnalyzeContainerType(Case.Data, Case.Size);
        DWRITE_CONTAINER_TYPE B = Got5->AnalyzeContainerType(Case.Data, Case.Size);

        TcCheck(A == B, "AnalyzeContainerType %s want %d got %d", Case.Tag, A, B);
    }
}

static
VOID
TcCompareNumberSubstitution(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    for (UINT32 Method = 0; Method <= DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL + 1; Method++)
    {
        for (PCWSTR Locale : { L"en-us", L"ar-eg", static_cast<PCWSTR>(nullptr) })
        {
            ComPtr<IDWriteNumberSubstitution> A;
            ComPtr<IDWriteNumberSubstitution> B;
            HRESULT Ha = Want.Factory->CreateNumberSubstitution(static_cast<DWRITE_NUMBER_SUBSTITUTION_METHOD>(Method),
                                                                Locale, FALSE, &A);
            HRESULT Hb = Got.Factory->CreateNumberSubstitution(static_cast<DWRITE_NUMBER_SUBSTITUTION_METHOD>(Method),
                                                               Locale, FALSE, &B);

            TcCheck(Ha == Hb && (A != nullptr) == (B != nullptr), "CreateNumberSubstitution %u %ls want %08lX got %08lX",
                    Method, Locale != nullptr ? Locale : L"(null)", Ha, Hb);
        }
    }
}

static
HRESULT
TcColorRun(
    _In_ TcSide& Side,
    _In_z_ PCWSTR FileName,
    _In_ UINT32 CodePoint)
{
    ComPtr<IDWriteFactory2> Factory2;
    ComPtr<IDWriteFontFile> File;
    ComPtr<IDWriteFontFace> Face;
    ComPtr<IDWriteColorGlyphRunEnumerator> Layers;
    WCHAR Path[MAX_PATH];
    DWRITE_GLYPH_RUN Run = {};
    UINT16 Glyph = 0;
    FLOAT Advance = 20.0f;
    HRESULT hr;

    if (FAILED(Side.Factory.As(&Factory2)))
        return E_NOINTERFACE;
    GetWindowsDirectoryW(Path, MAX_PATH);
    wcscat_s(Path, L"\\Fonts\\");
    wcscat_s(Path, FileName);
    hr = Side.Factory->CreateFontFileReference(Path, nullptr, &File);
    if (FAILED(hr))
        return hr;
    IDWriteFontFile* Files[] = { File.Get() };
    hr = Side.Factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, Files, 0, DWRITE_FONT_SIMULATIONS_NONE, &Face);
    if (FAILED(hr))
        return hr;
    Face->GetGlyphIndices(&CodePoint, 1, &Glyph);

    Run.fontFace = Face.Get();
    Run.fontEmSize = 20.0f;
    Run.glyphCount = 1;
    Run.glyphIndices = &Glyph;
    Run.glyphAdvances = &Advance;
    hr = Factory2->TranslateColorGlyphRun(0.0f, 0.0f, &Run, nullptr, DWRITE_MEASURING_MODE_NATURAL, nullptr, 0, &Layers);
    if (SUCCEEDED(hr) && !Layers)
        return E_POINTER;
    return hr;
}

static
VOID
TcCompareColorRuns(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    static const struct
    {
        PCWSTR File;
        UINT32 CodePoint;
    } Cases[] = { { L"arial.ttf", L'A' }, { L"seguiemj.ttf", 0x1F600 }, { L"seguiemj.ttf", L'A' } };

    for (const auto& Case : Cases)
    {
        HRESULT A = TcColorRun(Want, Case.File, Case.CodePoint);
        HRESULT B = TcColorRun(Got, Case.File, Case.CodePoint);

        TcCheck(A == B, "TranslateColorGlyphRun %ls U+%04X want %08lX got %08lX", Case.File, Case.CodePoint, A, B);
    }
}

static
std::wstring
TcDescribeMemoryFace(
    _In_ TcSide& Side,
    _In_ const std::vector<BYTE>& Bytes,
    _In_ BOOLEAN Register,
    _In_ BOOLEAN WithOwner)
{
    ComPtr<IDWriteFactory5> Factory5;
    ComPtr<IDWriteInMemoryFontFileLoader> Loader;
    ComPtr<IDWriteFontFile> File;
    ComPtr<IDWriteFontFace> Face;
    WCHAR Line[160];
    HRESULT hr;

    if (FAILED(Side.Factory.As(&Factory5)))
        return L"no factory5";
    hr = Factory5->CreateInMemoryFontFileLoader(&Loader);
    if (FAILED(hr))
    {
        swprintf_s(Line, L"loader hr %08lX", hr);
        return Line;
    }
    if (Register)
        Side.Factory->RegisterFontFileLoader(Loader.Get());

    hr = Loader->CreateInMemoryFontFileReference(Side.Factory.Get(), Bytes.data(), static_cast<UINT32>(Bytes.size()),
                                                 WithOwner ? Loader.Get() : nullptr, &File);
    if (SUCCEEDED(hr))
    {
        IDWriteFontFile* Files[] = { File.Get() };

        hr = Side.Factory->CreateFontFace(DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, Files, 0, DWRITE_FONT_SIMULATIONS_NONE, &Face);
    }
    swprintf_s(Line, L"hr %08lX files %u file %s glyphs %u", hr, Loader->GetFileCount(), File ? L"set" : L"null",
               Face ? Face->GetGlyphCount() : 0);
    if (Register)
        Side.Factory->UnregisterFontFileLoader(Loader.Get());
    return Line;
}

VOID
TcCompareFactoryExtras(
    _In_ TcSide& Want,
    _In_ TcSide& Got)
{
    printf("factory entry points\n");
    TcCompareContainers(Want, Got);
    TcCompareNumberSubstitution(Want, Got);
    TcCompareColorRuns(Want, Got);

    /* EUDC collection */
    {
        ComPtr<IDWriteFactory1> Want1;
        ComPtr<IDWriteFactory1> Got1;
        ComPtr<IDWriteFontCollection> A;
        ComPtr<IDWriteFontCollection> B;
        std::wstring Expected;
        std::wstring Actual;

        Want.Factory.As(&Want1);
        Got.Factory.As(&Got1);
        Expected = TcDescribeCollection(Want1->GetEudcFontCollection(&A, FALSE), A.Get());
        Actual = TcDescribeCollection(Got1->GetEudcFontCollection(&B, FALSE), B.Get());

        TcCheck(Expected == Actual, "GetEudcFontCollection want [%ls] got [%ls]", Expected.c_str(), Actual.c_str());
    }

    /* Custom collections, before and after registering the loader */
    {
        TcCollectionLoader Loader;

        for (BOOLEAN Register = FALSE; Register <= TRUE; Register++)
        {
            ComPtr<IDWriteFontCollection> A;
            ComPtr<IDWriteFontCollection> B;
            std::wstring Expected;
            std::wstring Actual;

            if (Register)
            {
                Want.Factory->RegisterFontCollectionLoader(&Loader);
                Got.Factory->RegisterFontCollectionLoader(&Loader);
            }
            Expected = TcDescribeCollection(Want.Factory->CreateCustomFontCollection(&Loader, "k", 1, &A), A.Get());
            Actual = TcDescribeCollection(Got.Factory->CreateCustomFontCollection(&Loader, "k", 1, &B), B.Get());
            TcCheck(Expected == Actual, "CreateCustomFontCollection registered %d\n    want [%ls]\n    got  [%ls]",
                    Register, Expected.c_str(), Actual.c_str());
        }
        Want.Factory->UnregisterFontCollectionLoader(&Loader);
        Got.Factory->UnregisterFontCollectionLoader(&Loader);
    }

    /* In memory files */
    {
        std::vector<BYTE> Arial = TcReadFont(L"arial.ttf");

        for (UINT32 Variant = 0; Variant < 3; Variant++)
        {
            BOOLEAN Register = Variant != 0;
            BOOLEAN Owner = Variant == 2;
            std::wstring Expected = TcDescribeMemoryFace(Want, Arial, Register, Owner);
            std::wstring Actual = TcDescribeMemoryFace(Got, Arial, Register, Owner);

            TcCheck(Expected == Actual, "in memory file registered %d owner %d want [%ls] got [%ls]", Register, Owner,
                    Expected.c_str(), Actual.c_str());
        }
    }
}
