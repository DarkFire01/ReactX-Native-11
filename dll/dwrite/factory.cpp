/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectWrite factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"

HRESULT
RxWriteFactory::Create(
    _In_ DWRITE_FACTORY_TYPE Type,
    _Outptr_ RxWriteFactory** Factory)
{
    *Factory = new (std::nothrow) RxWriteFactory(Type);
    return *Factory != nullptr ? S_OK : E_OUTOFMEMORY;
}

RxWriteFactory::RxWriteFactory(
    _In_ DWRITE_FACTORY_TYPE FactoryType) : RefCount(1), Type(FactoryType)
{
    InitializeSRWLock(&Lock);
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFactory) || riid == __uuidof(IDWriteFactory1) ||
        riid == __uuidof(IDWriteFactory2) || riid == __uuidof(IDWriteFactory3) || riid == __uuidof(IDWriteFactory4) ||
        riid == __uuidof(IDWriteFactory5) || riid == __uuidof(IDWriteFactory6) || riid == __uuidof(IDWriteFactory7))
    {
        *ppvObject = static_cast<IDWriteFactory7*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxWriteFactory)

/* Font files */

BOOLEAN
RxWriteFactory::IsFileLoaderRegistered(
    _In_ IDWriteFontFileLoader* Loader)
{
    BOOLEAN Found = FALSE;

    if (Loader == RxLocalLoader::Instance())
        return TRUE;

    AcquireSRWLockShared(&Lock);
    for (const auto& Entry : FileLoaders)
    {
        if (Entry.Get() == Loader)
        {
            Found = TRUE;
            break;
        }
    }
    ReleaseSRWLockShared(&Lock);
    return Found;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateFontFileReference(
    _In_z_ WCHAR const* filePath,
    _In_opt_ FILETIME const* lastWriteTime,
    _COM_Outptr_ IDWriteFontFile** fontFile)
{
    std::vector<BYTE> Key;
    RxFontFile* File;
    HRESULT hr;

    if (fontFile == nullptr)
        return E_INVALIDARG;
    *fontFile = nullptr;
    if (filePath == nullptr)
        return E_INVALIDARG;

    hr = RxLocalLoader::MakeKey(filePath, lastWriteTime, Key);
    if (FAILED(hr))
        return hr;

    hr = RxFontFile::Create(RxLocalLoader::Instance(), Key.data(), static_cast<UINT32>(Key.size()), &File);
    if (FAILED(hr))
        return hr;

    *fontFile = File;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomFontFileReference(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _In_ IDWriteFontFileLoader* fontFileLoader,
    _COM_Outptr_ IDWriteFontFile** fontFile)
{
    RxFontFile* File;
    HRESULT hr;

    if (fontFile == nullptr)
        return E_INVALIDARG;
    *fontFile = nullptr;
    if (fontFileLoader == nullptr || (fontFileReferenceKey == nullptr && fontFileReferenceKeySize != 0))
        return E_INVALIDARG;

    if (!IsFileLoaderRegistered(fontFileLoader))
        return E_INVALIDARG;

    hr = RxFontFile::Create(fontFileLoader, fontFileReferenceKey, fontFileReferenceKeySize, &File);
    if (FAILED(hr))
        return hr;

    *fontFile = File;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::RegisterFontFileLoader(
    _In_ IDWriteFontFileLoader* fontFileLoader)
{
    HRESULT hr = S_OK;

    if (fontFileLoader == nullptr)
        return E_INVALIDARG;
    if (fontFileLoader == RxLocalLoader::Instance())
        return DWRITE_E_ALREADYREGISTERED;

    AcquireSRWLockExclusive(&Lock);
    for (const auto& Entry : FileLoaders)
    {
        if (Entry.Get() == fontFileLoader)
        {
            hr = DWRITE_E_ALREADYREGISTERED;
            break;
        }
    }
    if (SUCCEEDED(hr))
        FileLoaders.push_back(RxComPtr<IDWriteFontFileLoader>(fontFileLoader));
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::UnregisterFontFileLoader(
    _In_ IDWriteFontFileLoader* fontFileLoader)
{
    HRESULT hr = E_INVALIDARG;

    if (fontFileLoader == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);
    for (auto Entry = FileLoaders.begin(); Entry != FileLoaders.end(); ++Entry)
    {
        if (Entry->Get() == fontFileLoader)
        {
            FileLoaders.erase(Entry);
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::RegisterFontCollectionLoader(
    _In_ IDWriteFontCollectionLoader* fontCollectionLoader)
{
    HRESULT hr = S_OK;

    if (fontCollectionLoader == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);
    for (const auto& Entry : CollectionLoaders)
    {
        if (Entry.Get() == fontCollectionLoader)
        {
            hr = DWRITE_E_ALREADYREGISTERED;
            break;
        }
    }
    if (SUCCEEDED(hr))
        CollectionLoaders.push_back(RxComPtr<IDWriteFontCollectionLoader>(fontCollectionLoader));
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::UnregisterFontCollectionLoader(
    _In_ IDWriteFontCollectionLoader* fontCollectionLoader)
{
    HRESULT hr = E_INVALIDARG;

    if (fontCollectionLoader == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);
    for (auto Entry = CollectionLoaders.begin(); Entry != CollectionLoaders.end(); ++Entry)
    {
        if (Entry->Get() == fontCollectionLoader)
        {
            CollectionLoaders.erase(Entry);
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

/* Font faces */

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateFontFace(
    DWRITE_FONT_FACE_TYPE fontFaceType,
    UINT32 numberOfFiles,
    _In_reads_(numberOfFiles) IDWriteFontFile* const* fontFiles,
    UINT32 faceIndex,
    DWRITE_FONT_SIMULATIONS fontFaceSimulationFlags,
    _COM_Outptr_ IDWriteFontFace** fontFace)
{
    BOOL Supported;
    DWRITE_FONT_FILE_TYPE FileType;
    DWRITE_FONT_FACE_TYPE ActualType;
    UINT32 FaceCount;
    RxFontFace* Face;
    HRESULT hr;

    if (fontFace == nullptr)
        return E_INVALIDARG;
    *fontFace = nullptr;
    if (numberOfFiles != 1 || fontFiles == nullptr || fontFiles[0] == nullptr)
        return E_INVALIDARG;
    if (fontFaceSimulationFlags & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE))
        return E_INVALIDARG;

    hr = fontFiles[0]->Analyze(&Supported, &FileType, &ActualType, &FaceCount);
    if (FAILED(hr))
        return hr;
    if (!Supported)
        return DWRITE_E_FILEFORMAT;

    /* A collection may be opened as the type of its faces, and a face type as a collection of one */
    if (fontFaceType != ActualType && fontFaceType != DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION &&
        ActualType != DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION)
        return DWRITE_E_FILEFORMAT;
    if (faceIndex >= FaceCount)
        return E_INVALIDARG;

    hr = RxFontFace::Create(fontFiles[0], faceIndex, fontFaceSimulationFlags, &Face);
    if (FAILED(hr))
        return hr;

    *fontFace = Face;
    return S_OK;
}
