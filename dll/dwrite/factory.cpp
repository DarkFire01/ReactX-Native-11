/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectWrite factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"
#include "fallback.hpp"

HRESULT
RxWriteFactory::Create(
    _In_ DWRITE_FACTORY_TYPE Type,
    _Outptr_ RxWriteFactory** Factory)
{
    *Factory = new (std::nothrow) RxWriteFactory(Type);
    return *Factory != nullptr ? S_OK : E_OUTOFMEMORY;
}

RxWriteFactory::RxWriteFactory(
    _In_ DWRITE_FACTORY_TYPE FactoryType) : RefCount(1), Type(FactoryType), System{}, Interop(nullptr), Fallback(nullptr)
{
    InitializeSRWLock(&Lock);
}

RxWriteFactory::~RxWriteFactory()
{
    for (RxFontCollection* Collection : System)
    {
        if (Collection != nullptr)
            Collection->Release();
    }
    delete Interop;
    if (Fallback != nullptr)
        Fallback->Release();
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetGdiInterop(
    _COM_Outptr_ IDWriteGdiInterop** gdiInterop)
{
    if (gdiInterop == nullptr)
        return E_INVALIDARG;
    *gdiInterop = nullptr;

    AcquireSRWLockExclusive(&Lock);
    if (Interop == nullptr)
        Interop = new (std::nothrow) RxGdiInterop(this);
    ReleaseSRWLockExclusive(&Lock);
    if (Interop == nullptr)
        return E_OUTOFMEMORY;

    Interop->AddRef();
    *gdiInterop = Interop;
    return S_OK;
}

/* Font collections */

HRESULT
RxWriteFactory::SystemCollectionOf(
    _In_ DWRITE_FONT_FAMILY_MODEL Model,
    _Outptr_ RxFontCollection** Collection)
{
    HRESULT hr = S_OK;

    *Collection = nullptr;
    AcquireSRWLockExclusive(&Lock);
    if (System[Model] == nullptr)
    {
        RxFontCollection* Other = System[Model == DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC ?
                                         DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE : DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC];
        RxFontSetData* Data;

        if (Other != nullptr)
        {
            hr = RxFontCollection::Create(Other->Data(), Model, &System[Model]);
        }
        else
        {
            hr = RxFontSetData::CreateSystem(&Data);
            if (SUCCEEDED(hr))
            {
                hr = RxFontCollection::Create(Data, Model, &System[Model]);
                Data->Release();
            }
        }
    }
    if (SUCCEEDED(hr))
    {
        System[Model]->AddRef();
        *Collection = System[Model];
    }
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetSystemFontCollection(
    _COM_Outptr_ IDWriteFontCollection** fontCollection,
    BOOL checkForUpdates)
{
    RxFontCollection* Collection;
    HRESULT hr;

    if (fontCollection == nullptr)
        return E_INVALIDARG;
    *fontCollection = nullptr;

    hr = SystemCollection(&Collection);
    if (FAILED(hr))
        return hr;
    *fontCollection = Collection;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetSystemFontCollection(
    BOOL includeDownloadableFonts,
    _COM_Outptr_ IDWriteFontCollection1** fontCollection,
    BOOL checkForUpdates)
{
    return GetSystemFontCollection(includeDownloadableFonts, DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE,
                                   reinterpret_cast<IDWriteFontCollection3**>(fontCollection));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetSystemFontCollection(
    BOOL includeDownloadableFonts,
    DWRITE_FONT_FAMILY_MODEL fontFamilyModel,
    _COM_Outptr_ IDWriteFontCollection2** fontCollection)
{
    return GetSystemFontCollection(includeDownloadableFonts, fontFamilyModel,
                                   reinterpret_cast<IDWriteFontCollection3**>(fontCollection));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::GetSystemFontCollection(
    BOOL includeDownloadableFonts,
    DWRITE_FONT_FAMILY_MODEL fontFamilyModel,
    _COM_Outptr_ IDWriteFontCollection3** fontCollection)
{
    RxFontCollection* Collection;
    HRESULT hr;

    if (fontCollection == nullptr)
        return E_INVALIDARG;
    *fontCollection = nullptr;
    if (static_cast<UINT32>(fontFamilyModel) > DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE)
        return E_INVALIDARG;

    /* Downloadable fonts are never offered, so both answers are the local set */
    hr = SystemCollectionOf(fontFamilyModel, &Collection);
    if (FAILED(hr))
        return hr;
    *fontCollection = Collection;
    return S_OK;
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

    hr = RxFontFace::Create(fontFiles[0], faceIndex, fontFaceSimulationFlags, nullptr, 0, &Face);
    if (FAILED(hr))
        return hr;

    *fontFace = Face;
    return S_OK;
}

/* Rendering parameters */

static
HRESULT
RxMakeParams(
    _In_ const RxParamValues& Values,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Params)
{
    RxRenderingParams* Result;
    HRESULT hr;

    hr = RxRenderingParams::Create(Values, &Result);
    if (FAILED(hr))
        return hr;
    hr = Result->QueryInterface(Iid, Params);
    Result->Release();
    return hr;
}

static
HRESULT
RxCustomParams(
    _In_ FLOAT Gamma,
    _In_ FLOAT Contrast,
    _In_ FLOAT GrayscaleContrast,
    _In_ FLOAT Level,
    _In_ DWRITE_PIXEL_GEOMETRY Geometry,
    _In_ UINT32 Mode,
    _In_ BOOLEAN Modern,
    _In_ DWRITE_GRID_FIT_MODE GridFit,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Params)
{
    RxParamValues Values = {};
    UINT32 LastMode = Modern ? DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED : DWRITE_RENDERING_MODE_OUTLINE;

    if (Params == nullptr)
        return E_INVALIDARG;
    *Params = nullptr;

    if (!(Gamma > 0.0f && Gamma <= 256.0f) || !(Contrast >= 0.0f) || !(GrayscaleContrast >= 0.0f) || !(Level >= 0.0f))
        return E_INVALIDARG;
    if (static_cast<UINT32>(Geometry) > DWRITE_PIXEL_GEOMETRY_BGR || Mode > LastMode ||
        static_cast<UINT32>(GridFit) > DWRITE_GRID_FIT_MODE_ENABLED)
        return E_INVALIDARG;

    Values.Gamma = Gamma;
    Values.EnhancedContrast = Contrast;
    Values.GrayscaleEnhancedContrast = GrayscaleContrast;
    Values.ClearTypeLevel = Level;
    Values.PixelGeometry = Geometry;
    Values.RenderingMode = static_cast<DWRITE_RENDERING_MODE1>(Mode);
    Values.GridFitMode = GridFit;
    Values.IsModern = Modern;
    Values.CompatibleGamma = RxCompatibleGamma();
    Values.CompatibleSmoothing = RxCompatibleSmoothing();
    return RxMakeParams(Values, Iid, Params);
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateRenderingParams(
    _COM_Outptr_ IDWriteRenderingParams** renderingParams)
{
    RxParamValues Values;

    if (renderingParams == nullptr)
        return E_INVALIDARG;
    *renderingParams = nullptr;

    RxRenderingParams::SystemValues(nullptr, &Values);
    return RxMakeParams(Values, __uuidof(IDWriteRenderingParams), reinterpret_cast<void**>(renderingParams));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateMonitorRenderingParams(
    HMONITOR monitor,
    _COM_Outptr_ IDWriteRenderingParams** renderingParams)
{
    MONITORINFOEXW Info = {};
    RxParamValues Values;

    if (renderingParams == nullptr)
        return E_INVALIDARG;
    *renderingParams = nullptr;

    Info.cbSize = sizeof(Info);
    if (!GetMonitorInfoW(monitor, &Info))
        return E_INVALIDARG;

    RxRenderingParams::SystemValues(Info.szDevice, &Values);
    return RxMakeParams(Values, __uuidof(IDWriteRenderingParams), reinterpret_cast<void**>(renderingParams));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomRenderingParams(
    FLOAT gamma,
    FLOAT enhancedContrast,
    FLOAT clearTypeLevel,
    DWRITE_PIXEL_GEOMETRY pixelGeometry,
    DWRITE_RENDERING_MODE renderingMode,
    _COM_Outptr_ IDWriteRenderingParams** renderingParams)
{
    return RxCustomParams(gamma, enhancedContrast, 1.0f, clearTypeLevel, pixelGeometry, renderingMode, FALSE,
                          DWRITE_GRID_FIT_MODE_DEFAULT, __uuidof(IDWriteRenderingParams),
                          reinterpret_cast<void**>(renderingParams));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomRenderingParams(
    FLOAT gamma,
    FLOAT enhancedContrast,
    FLOAT enhancedContrastGrayscale,
    FLOAT clearTypeLevel,
    DWRITE_PIXEL_GEOMETRY pixelGeometry,
    DWRITE_RENDERING_MODE renderingMode,
    _COM_Outptr_ IDWriteRenderingParams1** renderingParams)
{
    return RxCustomParams(gamma, enhancedContrast, enhancedContrastGrayscale, clearTypeLevel, pixelGeometry,
                          renderingMode, FALSE, DWRITE_GRID_FIT_MODE_DEFAULT, __uuidof(IDWriteRenderingParams1),
                          reinterpret_cast<void**>(renderingParams));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomRenderingParams(
    FLOAT gamma,
    FLOAT enhancedContrast,
    FLOAT grayscaleEnhancedContrast,
    FLOAT clearTypeLevel,
    DWRITE_PIXEL_GEOMETRY pixelGeometry,
    DWRITE_RENDERING_MODE renderingMode,
    DWRITE_GRID_FIT_MODE gridFitMode,
    _COM_Outptr_ IDWriteRenderingParams2** renderingParams)
{
    return RxCustomParams(gamma, enhancedContrast, grayscaleEnhancedContrast, clearTypeLevel, pixelGeometry,
                          renderingMode, FALSE, gridFitMode, __uuidof(IDWriteRenderingParams2),
                          reinterpret_cast<void**>(renderingParams));
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateCustomRenderingParams(
    FLOAT gamma,
    FLOAT enhancedContrast,
    FLOAT grayscaleEnhancedContrast,
    FLOAT clearTypeLevel,
    DWRITE_PIXEL_GEOMETRY pixelGeometry,
    DWRITE_RENDERING_MODE1 renderingMode,
    DWRITE_GRID_FIT_MODE gridFitMode,
    _COM_Outptr_ IDWriteRenderingParams3** renderingParams)
{
    return RxCustomParams(gamma, enhancedContrast, grayscaleEnhancedContrast, clearTypeLevel, pixelGeometry,
                          renderingMode, TRUE, gridFitMode, __uuidof(IDWriteRenderingParams3),
                          reinterpret_cast<void**>(renderingParams));
}

/* Glyph run analysis */

static
HRESULT
RxAnalyze(
    _In_ DWRITE_GLYPH_RUN const* GlyphRun,
    _In_ FLOAT PixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* Transform,
    _In_ UINT32 Mode,
    _In_ UINT32 LastMode,
    _In_ DWRITE_MEASURING_MODE Measuring,
    _In_ DWRITE_GRID_FIT_MODE GridFit,
    _In_ DWRITE_TEXT_ANTIALIAS_MODE Antialias,
    _In_ BOOLEAN Modern,
    _In_ FLOAT BaselineX,
    _In_ FLOAT BaselineY,
    _COM_Outptr_ IDWriteGlyphRunAnalysis** Analysis)
{
    RxGlyphRunAnalysis* Result;
    HRESULT hr;

    if (Analysis == nullptr)
        return E_INVALIDARG;
    *Analysis = nullptr;

    if (GlyphRun == nullptr || GlyphRun->fontFace == nullptr || !(GlyphRun->fontEmSize >= 0.0f) ||
        (GlyphRun->glyphCount != 0 && GlyphRun->glyphIndices == nullptr))
        return E_INVALIDARG;
    if (!(PixelsPerDip > 0.0f) || Mode > LastMode || static_cast<UINT32>(Measuring) > DWRITE_MEASURING_MODE_GDI_NATURAL)
        return E_INVALIDARG;
    if (static_cast<UINT32>(GridFit) > DWRITE_GRID_FIT_MODE_ENABLED ||
        static_cast<UINT32>(Antialias) > DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        return E_INVALIDARG;

    hr = RxGlyphRunAnalysis::Create(GlyphRun, PixelsPerDip, Transform, static_cast<DWRITE_RENDERING_MODE1>(Mode),
                                    Measuring, GridFit, Antialias, Modern, BaselineX, BaselineY, &Result);
    if (FAILED(hr))
        return hr;

    *Analysis = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateGlyphRunAnalysis(
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    DWRITE_RENDERING_MODE renderingMode,
    DWRITE_MEASURING_MODE measuringMode,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _COM_Outptr_ IDWriteGlyphRunAnalysis** glyphRunAnalysis)
{
    return RxAnalyze(glyphRun, pixelsPerDip, transform, renderingMode, DWRITE_RENDERING_MODE_OUTLINE, measuringMode,
                     DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE, FALSE, baselineOriginX,
                     baselineOriginY, glyphRunAnalysis);
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateGlyphRunAnalysis(
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    _In_opt_ DWRITE_MATRIX const* transform,
    DWRITE_RENDERING_MODE renderingMode,
    DWRITE_MEASURING_MODE measuringMode,
    DWRITE_GRID_FIT_MODE gridFitMode,
    DWRITE_TEXT_ANTIALIAS_MODE antialiasMode,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _COM_Outptr_ IDWriteGlyphRunAnalysis** glyphRunAnalysis)
{
    return RxAnalyze(glyphRun, 1.0f, transform, renderingMode, DWRITE_RENDERING_MODE_OUTLINE, measuringMode, gridFitMode,
                     antialiasMode, FALSE, baselineOriginX, baselineOriginY, glyphRunAnalysis);
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateGlyphRunAnalysis(
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    _In_opt_ DWRITE_MATRIX const* transform,
    DWRITE_RENDERING_MODE1 renderingMode,
    DWRITE_MEASURING_MODE measuringMode,
    DWRITE_GRID_FIT_MODE gridFitMode,
    DWRITE_TEXT_ANTIALIAS_MODE antialiasMode,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _COM_Outptr_ IDWriteGlyphRunAnalysis** glyphRunAnalysis)
{
    return RxAnalyze(glyphRun, 1.0f, transform, renderingMode, DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED,
                     measuringMode, gridFitMode, antialiasMode, TRUE, baselineOriginX, baselineOriginY,
                     glyphRunAnalysis);
}

/* Text */

/* The OpenType features a layout range asks for */
class RxTypography final : public IDWriteTypography
{
public:
    RxTypography() : RefCount(1) {}

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    AddFontFeature(
        DWRITE_FONT_FEATURE fontFeature) override
    {
        Features.push_back(fontFeature);
        return S_OK;
    }

    UINT32
    STDMETHODCALLTYPE
    GetFontFeatureCount(VOID) override
    {
        return static_cast<UINT32>(Features.size());
    }

    HRESULT
    STDMETHODCALLTYPE
    GetFontFeature(
        UINT32 fontFeatureIndex,
        _Out_ DWRITE_FONT_FEATURE* fontFeature) override
    {
        if (fontFeatureIndex >= Features.size())
            return E_INVALIDARG;
        *fontFeature = Features[fontFeatureIndex];
        return S_OK;
    }

private:
    LONG RefCount;
    std::vector<DWRITE_FONT_FEATURE> Features;
};

HRESULT
STDMETHODCALLTYPE
RxTypography::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTypography))
    {
        *ppvObject = static_cast<IDWriteTypography*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxTypography)

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateTypography(
    _COM_Outptr_ IDWriteTypography** typography)
{
    if (typography == nullptr)
        return E_INVALIDARG;
    *typography = new (std::nothrow) RxTypography();
    return *typography != nullptr ? S_OK : E_OUTOFMEMORY;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateTextFormat(
    _In_z_ WCHAR const* fontFamilyName,
    _In_opt_ IDWriteFontCollection* fontCollection,
    DWRITE_FONT_WEIGHT fontWeight,
    DWRITE_FONT_STYLE fontStyle,
    DWRITE_FONT_STRETCH fontStretch,
    FLOAT fontSize,
    _In_z_ WCHAR const* localeName,
    _COM_Outptr_ IDWriteTextFormat** textFormat)
{
    RxTextProperties Properties = {};
    RxTextFormat* Format;
    HRESULT hr;

    if (textFormat == nullptr)
        return E_INVALIDARG;
    *textFormat = nullptr;

    if (fontFamilyName == nullptr || localeName == nullptr || !RxValidFont(fontWeight, fontStyle, fontStretch) ||
        !(fontSize > 0.0f))
        return E_INVALIDARG;

    if (fontCollection != nullptr)
    {
        Properties.Collection = RxComPtr<IDWriteFontCollection>(fontCollection);
    }
    else
    {
        RxFontCollection* Installed;

        hr = SystemCollection(&Installed);
        if (FAILED(hr))
            return hr;
        Properties.Collection.Attach(Installed);
    }

    Properties.Family = fontFamilyName;
    Properties.Weight = fontWeight;
    Properties.Style = fontStyle;
    Properties.Stretch = fontStretch;
    Properties.Size = fontSize;
    Properties.Locale = localeName;
    Properties.IncrementalTabStop = 4.0f * fontSize;
    Properties.LastLineWrapping = TRUE;

    hr = RxTextFormat::Create(Properties, &Format);
    if (FAILED(hr))
        return hr;
    *textFormat = Format;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateTextLayout(
    _In_reads_(stringLength) WCHAR const* string,
    UINT32 stringLength,
    _In_ IDWriteTextFormat* textFormat,
    FLOAT maxWidth,
    FLOAT maxHeight,
    _COM_Outptr_ IDWriteTextLayout** textLayout)
{
    RxTextLayout* Layout;
    HRESULT hr;

    if (textLayout == nullptr)
        return E_INVALIDARG;
    *textLayout = nullptr;
    if (string == nullptr || textFormat == nullptr || !(maxWidth >= 0.0f) || !(maxHeight >= 0.0f))
        return E_INVALIDARG;

    hr = RxTextLayout::Create(this, string, stringLength, textFormat, maxWidth, maxHeight,
                              DWRITE_MEASURING_MODE_NATURAL, 1.0f, nullptr, &Layout);
    if (FAILED(hr))
        return hr;
    *textLayout = Layout;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateGdiCompatibleTextLayout(
    _In_reads_(stringLength) WCHAR const* string,
    UINT32 stringLength,
    _In_ IDWriteTextFormat* textFormat,
    FLOAT layoutWidth,
    FLOAT layoutHeight,
    FLOAT pixelsPerDip,
    _In_opt_ DWRITE_MATRIX const* transform,
    BOOL useGdiNatural,
    _COM_Outptr_ IDWriteTextLayout** textLayout)
{
    RxTextLayout* Layout;
    HRESULT hr;

    if (textLayout == nullptr)
        return E_INVALIDARG;
    *textLayout = nullptr;
    if (string == nullptr || textFormat == nullptr || !(layoutWidth >= 0.0f) || !(layoutHeight >= 0.0f) ||
        !(pixelsPerDip > 0.0f))
        return E_INVALIDARG;

    hr = RxTextLayout::Create(this, string, stringLength, textFormat, layoutWidth, layoutHeight,
                              useGdiNatural ? DWRITE_MEASURING_MODE_GDI_NATURAL : DWRITE_MEASURING_MODE_GDI_CLASSIC,
                              pixelsPerDip, transform, &Layout);
    if (FAILED(hr))
        return hr;
    *textLayout = Layout;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateEllipsisTrimmingSign(
    _In_ IDWriteTextFormat* textFormat,
    _COM_Outptr_ IDWriteInlineObject** trimmingSign)
{
    if (trimmingSign == nullptr)
        return E_INVALIDARG;
    return RxEllipsisSign::Create(this, textFormat, trimmingSign);
}

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateTextFormat(
    _In_z_ WCHAR const* fontFamilyName,
    _In_opt_ IDWriteFontCollection* fontCollection,
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    FLOAT fontSize,
    _In_z_ WCHAR const* localeName,
    _COM_Outptr_ IDWriteTextFormat3** textFormat)
{
    RX_UNIMPLEMENTED();
    if (textFormat != nullptr)
        *textFormat = nullptr;
    return E_NOTIMPL;
}
