/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Factory entry points for color runs, containers, number substitution and custom collections
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"

#include <memory>

/* Color glyph runs */

static
HRESULT
RxColorRun(
    _In_opt_ const DWRITE_GLYPH_RUN* GlyphRun,
    _In_ UINT32 Formats,
    _Outptr_ IUnknown** Layers)
{
    RxComPtr<IDWriteFontFace2> Face;

    *Layers = nullptr;
    if (GlyphRun == nullptr || (Formats & ~0x1FFu) != 0)
        return E_INVALIDARG;
    if (GlyphRun->fontFace == nullptr ||
        FAILED(GlyphRun->fontFace->QueryInterface(__uuidof(IDWriteFontFace2), reinterpret_cast<void**>(Face.Put()))))
        return DWRITE_E_NOCOLOR;
    if (!Face->IsColorFont())
        return DWRITE_E_NOCOLOR;

    /* Color layers are not built yet, so callers draw the run in one color */
    RX_TRACE(L"TranslateColorGlyphRun: color layers of a %u glyph run are not built", GlyphRun->glyphCount);
    return DWRITE_E_NOCOLOR;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::TranslateColorGlyphRun(
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    _In_opt_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    DWRITE_MEASURING_MODE measuringMode,
    _In_opt_ DWRITE_MATRIX const* worldToDeviceTransform,
    UINT32 colorPaletteIndex,
    _COM_Outptr_ IDWriteColorGlyphRunEnumerator** colorLayers)
{
    if (colorLayers == nullptr)
        return E_INVALIDARG;
    return RxColorRun(glyphRun, DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE | DWRITE_GLYPH_IMAGE_FORMATS_COLR,
                      reinterpret_cast<IUnknown**>(colorLayers));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::TranslateColorGlyphRun(
    D2D1_POINT_2F baselineOrigin,
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    _In_opt_ DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
    DWRITE_GLYPH_IMAGE_FORMATS desiredGlyphImageFormats,
    DWRITE_MEASURING_MODE measuringMode,
    _In_opt_ DWRITE_MATRIX const* worldAndDpiTransform,
    UINT32 colorPaletteIndex,
    _COM_Outptr_ IDWriteColorGlyphRunEnumerator1** colorLayers)
{
    if (colorLayers == nullptr)
        return E_INVALIDARG;
    return RxColorRun(glyphRun, desiredGlyphImageFormats, reinterpret_cast<IUnknown**>(colorLayers));
}

/* Containers */

DWRITE_CONTAINER_TYPE
STDMETHODCALLTYPE
RxWriteFactory::AnalyzeContainerType(
    _In_reads_bytes_(fileDataSize) void const* fileData,
    UINT32 fileDataSize)
{
    RxSpan Data(fileData, fileDataSize);

    /* Only the signature decides; the headers are read when the file is unpacked */
    if (Data.U32(0) == RxTag('w', 'O', 'F', 'F'))
        return DWRITE_CONTAINER_TYPE_WOFF;
    if (Data.U32(0) == RxTag('w', 'O', 'F', '2'))
        return DWRITE_CONTAINER_TYPE_WOFF2;
    return DWRITE_CONTAINER_TYPE_UNKNOWN;
}

/* Number substitution */

/* Holds the settings it was made with; the interface has no methods of its own */
class RxNumberSubstitution final : public IDWriteNumberSubstitution
{
public:
    RxNumberSubstitution(
        _In_ DWRITE_NUMBER_SUBSTITUTION_METHOD SubstitutionMethod,
        _In_opt_z_ PCWSTR LocaleName,
        _In_ BOOLEAN IgnoreOverride) : RefCount(1), Method(SubstitutionMethod),
                                       Locale(LocaleName != nullptr ? LocaleName : L""), IgnoreUserOverride(IgnoreOverride) {}

    RX_DECLARE_IUNKNOWN

private:
    LONG RefCount;
    DWRITE_NUMBER_SUBSTITUTION_METHOD Method;
    std::wstring Locale;
    BOOLEAN IgnoreUserOverride;
};

HRESULT
STDMETHODCALLTYPE
RxNumberSubstitution::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteNumberSubstitution))
    {
        *ppvObject = static_cast<IDWriteNumberSubstitution*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxNumberSubstitution)

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateNumberSubstitution(
    _In_ DWRITE_NUMBER_SUBSTITUTION_METHOD substitutionMethod,
    _In_z_ WCHAR const* localeName,
    _In_ BOOL ignoreUserOverride,
    _COM_Outptr_ IDWriteNumberSubstitution** numberSubstitution)
{
    if (numberSubstitution == nullptr)
        return E_INVALIDARG;
    *numberSubstitution = nullptr;

    /* Only the method that never substitutes may leave the locale out */
    if (static_cast<UINT32>(substitutionMethod) > DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL ||
        (localeName == nullptr && substitutionMethod != DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE))
        return E_INVALIDARG;

    *numberSubstitution = new (std::nothrow) RxNumberSubstitution(substitutionMethod, localeName,
                                                                  ignoreUserOverride != FALSE);
    return *numberSubstitution != nullptr ? S_OK : E_OUTOFMEMORY;
}

/* Custom collections */

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomFontCollection(
    _In_ IDWriteFontCollectionLoader* collectionLoader,
    _In_reads_bytes_(collectionKeySize) void const* collectionKey,
    UINT32 collectionKeySize,
    _COM_Outptr_ IDWriteFontCollection** fontCollection)
{
    RxComPtr<IDWriteFontFileEnumerator> Enumerator;
    std::vector<RxComPtr<IDWriteFontFile>> Held;
    std::vector<IDWriteFontFile*> Files;
    BOOLEAN Registered = FALSE;
    RxFontSetData* Data;
    RxFontCollection* Collection;
    BOOL HasFile;
    HRESULT hr;

    if (fontCollection == nullptr)
        return E_INVALIDARG;
    *fontCollection = nullptr;
    if (collectionLoader == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockShared(&Lock);
    for (const auto& Entry : CollectionLoaders)
    {
        if (Entry.Get() == collectionLoader)
            Registered = TRUE;
    }
    ReleaseSRWLockShared(&Lock);
    if (!Registered)
        return E_INVALIDARG;

    hr = collectionLoader->CreateEnumeratorFromKey(this, collectionKey, collectionKeySize, Enumerator.Put());
    if (FAILED(hr))
        return hr;
    if (!Enumerator.Get())
        return E_FAIL;

    for (;;)
    {
        RxComPtr<IDWriteFontFile> File;

        hr = Enumerator->MoveNext(&HasFile);
        if (FAILED(hr))
            return hr;
        if (!HasFile)
            break;
        hr = Enumerator->GetCurrentFontFile(File.Put());
        if (FAILED(hr))
            return hr;
        Files.push_back(File.Get());
        Held.push_back(std::move(File));
    }

    hr = RxFontSetData::CreateFromFiles(Files.data(), static_cast<UINT32>(Files.size()), &Data);
    if (FAILED(hr))
        return hr;
    hr = RxFontCollection::Create(Data, &Collection);
    Data->Release();
    if (FAILED(hr))
        return hr;
    *fontCollection = Collection;
    return S_OK;
}

/* Fonts of the end user defined character set, as the registry lists them for the ANSI code page */
HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetEudcFontCollection(
    _COM_Outptr_ IDWriteFontCollection** fontCollection,
    BOOL checkForUpdates)
{
    std::vector<RxComPtr<IDWriteFontFile>> Held;
    std::vector<IDWriteFontFile*> Files;
    WCHAR KeyName[32];
    HKEY Key;
    RxFontSetData* Data;
    RxFontCollection* Collection;
    HRESULT hr;

    if (fontCollection == nullptr)
        return E_INVALIDARG;
    *fontCollection = nullptr;

    swprintf_s(KeyName, L"EUDC\\%u", GetACP());
    if (RegOpenKeyExW(HKEY_CURRENT_USER, KeyName, 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        for (DWORD Index = 0;; Index++)
        {
            WCHAR Name[256];
            WCHAR Path[MAX_PATH];
            WCHAR Full[MAX_PATH];
            DWORD NameSize = _countof(Name);
            DWORD PathSize = sizeof(Path) - sizeof(WCHAR);
            DWORD ValueType;
            RxComPtr<IDWriteFontFile> File;

            if (RegEnumValueW(Key, Index, Name, &NameSize, nullptr, &ValueType, reinterpret_cast<BYTE*>(Path),
                              &PathSize) != ERROR_SUCCESS)
                break;
            if ((ValueType != REG_SZ && ValueType != REG_EXPAND_SZ) || PathSize < sizeof(WCHAR))
                continue;
            Path[PathSize / sizeof(WCHAR)] = UNICODE_NULL;

            /* Bare names live in the fonts folder */
            if (wcschr(Path, L'\\') == nullptr)
                swprintf_s(Full, L"%s%s", RxFontsDirectory(), Path);
            else if (ExpandEnvironmentStringsW(Path, Full, MAX_PATH) == 0)
                continue;
            if (GetFileAttributesW(Full) == INVALID_FILE_ATTRIBUTES)
                continue;
            if (FAILED(CreateFontFileReference(Full, nullptr, File.Put())))
                continue;
            Files.push_back(File.Get());
            Held.push_back(std::move(File));
        }
        RegCloseKey(Key);
    }

    hr = RxFontSetData::CreateFromFiles(Files.data(), static_cast<UINT32>(Files.size()), &Data);
    if (FAILED(hr))
        return hr;
    hr = RxFontCollection::Create(Data, &Collection);
    Data->Release();
    if (FAILED(hr))
        return hr;
    *fontCollection = Collection;
    return S_OK;
}

/* In memory font files */

namespace
{

/* One file the loader holds: its own copy, or the caller's bytes kept alive by the owner */
struct RxMemoryFile
{
    std::vector<BYTE> Copy;
    RxComPtr<IUnknown> Owner;
    const BYTE* Bytes;
    UINT32 Size;
};

class RxMemoryStream final : public IDWriteFontFileStream
{
public:
    RxMemoryStream(
        _In_ IUnknown* Keeper,
        _In_reads_bytes_(ByteCount) const BYTE* Data,
        _In_ UINT32 ByteCount) : RefCount(1), Holder(Keeper), Bytes(Data), Size(ByteCount) {}

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    ReadFileFragment(
        _Outptr_result_bytebuffer_(fragmentSize) void const** fragmentStart,
        UINT64 fileOffset,
        UINT64 fragmentSize,
        _Out_ void** fragmentContext) override
    {
        *fragmentStart = nullptr;
        *fragmentContext = nullptr;
        if (fileOffset > Size || fragmentSize > Size - fileOffset)
            return E_FAIL;
        *fragmentStart = Bytes + fileOffset;
        return S_OK;
    }

    void
    STDMETHODCALLTYPE
    ReleaseFileFragment(
        void* fragmentContext) override
    {
        UNREFERENCED_PARAMETER(fragmentContext);
    }

    HRESULT
    STDMETHODCALLTYPE
    GetFileSize(
        _Out_ UINT64* fileSize) override
    {
        *fileSize = Size;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetLastWriteTime(
        _Out_ UINT64* lastWriteTime) override
    {
        *lastWriteTime = 0;
        return E_NOTIMPL;
    }

private:
    LONG RefCount;
    /* The loader, which owns the bytes */
    RxComPtr<IUnknown> Holder;
    const BYTE* Bytes;
    UINT32 Size;
};

HRESULT
STDMETHODCALLTYPE
RxMemoryStream::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileStream))
    {
        *ppvObject = static_cast<IDWriteFontFileStream*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxMemoryStream)

class RxMemoryLoader final : public IDWriteInMemoryFontFileLoader
{
public:
    RxMemoryLoader() : RefCount(1)
    {
        InitializeSRWLock(&Lock);
    }

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateStreamFromKey(
        _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        _COM_Outptr_ IDWriteFontFileStream** fontFileStream) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateInMemoryFontFileReference(
        IDWriteFactory* factory,
        _In_reads_bytes_(fontDataSize) void const* fontData,
        UINT32 fontDataSize,
        _In_opt_ IUnknown* ownerObject,
        _COM_Outptr_ IDWriteFontFile** fontFile) override;

    UINT32
    STDMETHODCALLTYPE
    GetFileCount(VOID) override;

private:
    LONG RefCount;
    SRWLOCK Lock;
    /* Files never move once added, so streams can point into them */
    std::vector<std::unique_ptr<RxMemoryFile>> Files;
};

HRESULT
STDMETHODCALLTYPE
RxMemoryLoader::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileLoader) ||
        riid == __uuidof(IDWriteInMemoryFontFileLoader))
    {
        *ppvObject = static_cast<IDWriteInMemoryFontFileLoader*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxMemoryLoader)

HRESULT
STDMETHODCALLTYPE
RxMemoryLoader::CreateStreamFromKey(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _COM_Outptr_ IDWriteFontFileStream** fontFileStream)
{
    UINT32 Index;
    const RxMemoryFile* File = nullptr;

    if (fontFileStream == nullptr)
        return E_INVALIDARG;
    *fontFileStream = nullptr;
    if (fontFileReferenceKey == nullptr || fontFileReferenceKeySize != sizeof(Index))
        return E_INVALIDARG;
    memcpy(&Index, fontFileReferenceKey, sizeof(Index));

    AcquireSRWLockShared(&Lock);
    if (Index < Files.size())
        File = Files[Index].get();
    ReleaseSRWLockShared(&Lock);
    if (File == nullptr)
        return E_INVALIDARG;

    *fontFileStream = new (std::nothrow) RxMemoryStream(static_cast<IDWriteInMemoryFontFileLoader*>(this), File->Bytes,
                                                        File->Size);
    return *fontFileStream != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT
STDMETHODCALLTYPE
RxMemoryLoader::CreateInMemoryFontFileReference(
    IDWriteFactory* factory,
    _In_reads_bytes_(fontDataSize) void const* fontData,
    UINT32 fontDataSize,
    _In_opt_ IUnknown* ownerObject,
    _COM_Outptr_ IDWriteFontFile** fontFile)
{
    std::unique_ptr<RxMemoryFile> File;
    UINT32 Index;

    if (fontFile == nullptr)
        return E_INVALIDARG;
    *fontFile = nullptr;
    if (factory == nullptr || (fontData == nullptr && fontDataSize != 0))
        return E_INVALIDARG;

    File.reset(new (std::nothrow) RxMemoryFile());
    if (!File)
        return E_OUTOFMEMORY;
    if (ownerObject != nullptr)
    {
        File->Owner = RxComPtr<IUnknown>(ownerObject);
        File->Bytes = static_cast<const BYTE*>(fontData);
    }
    else
    {
        File->Copy.assign(static_cast<const BYTE*>(fontData), static_cast<const BYTE*>(fontData) + fontDataSize);
        File->Bytes = File->Copy.data();
    }
    File->Size = fontDataSize;

    AcquireSRWLockExclusive(&Lock);
    Index = static_cast<UINT32>(Files.size());
    Files.push_back(std::move(File));
    ReleaseSRWLockExclusive(&Lock);

    return factory->CreateCustomFontFileReference(&Index, sizeof(Index), this, fontFile);
}

UINT32
STDMETHODCALLTYPE
RxMemoryLoader::GetFileCount(VOID)
{
    UINT32 Count;

    AcquireSRWLockShared(&Lock);
    Count = static_cast<UINT32>(Files.size());
    ReleaseSRWLockShared(&Lock);
    return Count;
}

} // namespace

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateInMemoryFontFileLoader(
    _COM_Outptr_ IDWriteInMemoryFontFileLoader** newLoader)
{
    if (newLoader == nullptr)
        return E_INVALIDARG;
    *newLoader = new (std::nothrow) RxMemoryLoader();
    return *newLoader != nullptr ? S_OK : E_OUTOFMEMORY;
}
