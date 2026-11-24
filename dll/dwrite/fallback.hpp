/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font fallback and the font fallback builder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "factory.hpp"

#include <string>

/* Families to try for some ranges, as AddMapping describes them */
struct RxFallbackMapping
{
    std::vector<DWRITE_UNICODE_RANGE> Ranges;
    std::vector<std::wstring> Families;
    /* NULL for the system collection */
    RxComPtr<IDWriteFontCollection> Collection;
    /* Empty when the mapping applies to every locale */
    std::vector<std::wstring> Locales;
    /* Empty when the mapping applies to every base family */
    std::wstring BaseFamily;
    FLOAT Scale;
};

/* Per system family: cmap segments, read the first time a scan reaches it, and the fonts matched in it */
class RxFallbackCache
{
public:
    static
    HRESULT
    Create(
        _In_ RxFontCollection* Collection,
        _Outptr_ RxFallbackCache** Cache);

    VOID
    AddRef(VOID)
    {
        InterlockedIncrement(&RefCount);
    }

    VOID
    Release(VOID)
    {
        if (InterlockedDecrement(&RefCount) == 0)
            delete this;
    }

    RxFontCollection*
    Collection(VOID) const
    {
        return System.Get();
    }

    UINT32
    FamilyCount(VOID) const
    {
        return static_cast<UINT32>(Families.size());
    }

    /* FALSE for a symbol family or one whose cmap has no segment holding CodePoint */
    BOOLEAN
    MayHave(
        _In_ UINT32 Family,
        _In_ UINT32 CodePoint);

    BOOLEAN
    AnyMayHave(
        _In_ UINT32 CodePoint);

    /* GetFirstMatchingFont of a family, kept with its face loaded so threads can share it */
    HRESULT
    MatchingFont(
        _In_ UINT32 Family,
        _In_ DWRITE_FONT_WEIGHT Weight,
        _In_ DWRITE_FONT_STYLE Style,
        _In_ DWRITE_FONT_STRETCH Stretch,
        _COM_Outptr_ IDWriteFont** Font);

private:
    struct FamilyRanges
    {
        BOOLEAN Loaded;
        BOOLEAN Usable;
        std::vector<DWRITE_UNICODE_RANGE> Ranges;
    };

    struct MatchedFont
    {
        UINT32 Family;
        DWRITE_FONT_WEIGHT Weight;
        DWRITE_FONT_STYLE Style;
        DWRITE_FONT_STRETCH Stretch;
        RxComPtr<IDWriteFont> Font;
    };

    RxFallbackCache() : RefCount(1)
    {
        InitializeSRWLock(&Lock);
    }

    VOID
    Load(
        _Inout_ FamilyRanges& Slot,
        _In_ UINT32 Family);

    LONG RefCount;
    SRWLOCK Lock;
    RxComPtr<RxFontCollection> System;
    std::vector<FamilyRanges> Families;
    std::vector<MatchedFont> Matched;
};

class RxFontFallback final : public IDWriteFontFallback1
{
public:
    /* The built in mappings over the system collection, followed by a scan of every family */
    static
    HRESULT
    CreateSystem(
        _In_ RxFontCollection* System,
        _Outptr_ RxFontFallback** Fallback);

    static
    HRESULT
    Create(
        _In_opt_ RxWriteFactory* Owner,
        _In_ RxFontCollection* System,
        _In_ const std::vector<RxFallbackMapping>& Mappings,
        _In_opt_ RxFallbackCache* Cache,
        _Outptr_ RxFontFallback** Fallback);

    static
    RxFontFallback*
    FromInterface(
        _In_opt_ IUnknown* Fallback);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    MapCharacters(
        IDWriteTextAnalysisSource* analysisSource,
        UINT32 textPosition,
        UINT32 textLength,
        _In_opt_ IDWriteFontCollection* baseFontCollection,
        _In_opt_z_ wchar_t const* baseFamilyName,
        DWRITE_FONT_WEIGHT baseWeight,
        DWRITE_FONT_STYLE baseStyle,
        DWRITE_FONT_STRETCH baseStretch,
        _Out_range_(0, textLength) UINT32* mappedLength,
        _COM_Outptr_result_maybenull_ IDWriteFont** mappedFont,
        _Out_ FLOAT* scale) override;

    HRESULT
    STDMETHODCALLTYPE
    MapCharacters(
        IDWriteTextAnalysisSource* analysisSource,
        UINT32 textPosition,
        UINT32 textLength,
        _In_opt_ IDWriteFontCollection* baseFontCollection,
        _In_opt_z_ WCHAR const* baseFamilyName,
        _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
        UINT32 fontAxisValueCount,
        _Deref_out_range_(0, textLength) UINT32* mappedLength,
        _Out_ FLOAT* scale,
        _COM_Outptr_ IDWriteFontFace5** mappedFontFace) override;

    const std::vector<RxFallbackMapping>&
    MappingList(VOID) const
    {
        return Mappings;
    }

    RxFontCollection*
    Collection(VOID) const
    {
        return System.Get();
    }

    RxFallbackCache*
    Cache(VOID) const
    {
        return Scan.Get();
    }

private:
    RxFontFallback() : RefCount(1) {}

    /* Mapping indices that apply to a locale and base family, locale specific ones first */
    VOID
    Applicable(
        _In_z_ PCWSTR Locale,
        _In_opt_z_ PCWSTR BaseFamily,
        _Out_ std::vector<UINT32>& Order) const;

    /* First applicable mapping holding CodePoint, or a key for none */
    INT32
    Classify(
        _In_ const std::vector<UINT32>& Order,
        _In_ UINT32 CodePoint) const;

    LONG RefCount;
    RxComPtr<RxWriteFactory> Factory;
    RxComPtr<RxFontCollection> System;
    std::vector<RxFallbackMapping> Mappings;
    RxComPtr<RxFallbackCache> Scan;
};

class RxFontFallbackBuilder final : public IDWriteFontFallbackBuilder
{
public:
    static
    HRESULT
    Create(
        _In_ RxWriteFactory* Owner,
        _Outptr_ RxFontFallbackBuilder** Builder);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    AddMapping(
        _In_reads_(rangesCount) DWRITE_UNICODE_RANGE const* ranges,
        UINT32 rangesCount,
        _In_reads_(targetFamilyNamesCount) WCHAR const** targetFamilyNames,
        UINT32 targetFamilyNamesCount,
        _In_opt_ IDWriteFontCollection* fontCollection,
        _In_opt_z_ WCHAR const* localeName,
        _In_opt_z_ WCHAR const* baseFamilyName,
        FLOAT scale) override;

    HRESULT
    STDMETHODCALLTYPE
    AddMappings(
        IDWriteFontFallback* fontFallback) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFallback(
        _COM_Outptr_ IDWriteFontFallback** fontFallback) override;

private:
    RxFontFallbackBuilder() : RefCount(1) {}

    LONG RefCount;
    RxComPtr<RxWriteFactory> Factory;
    std::vector<RxFallbackMapping> Mappings;
    RxComPtr<RxFallbackCache> Scan;
};
