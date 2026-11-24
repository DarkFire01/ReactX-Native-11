/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectWrite factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "glyphrun.hpp"
#include "collection.hpp"
#include "textlayout.hpp"
#include "gdiinterop.hpp"

class RxFontFallback;

class RxWriteFactory final : public IDWriteFactory7
{
public:
    static
    HRESULT
    Create(
        _In_ DWRITE_FACTORY_TYPE Type,
        _Outptr_ RxWriteFactory** Factory);

    RX_DECLARE_IUNKNOWN

#include "factory.inl"

    BOOLEAN
    IsShared(VOID) const
    {
        return Type == DWRITE_FACTORY_TYPE_SHARED;
    }

    /* Built on first use and kept for the factory's life; the caller gets a reference */
    HRESULT
    SystemCollection(
        _Outptr_ RxFontCollection** Collection)
    {
        return SystemCollectionOf(DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE, Collection);
    }

    /* The installed fonts grouped by one family model; both share one parse */
    HRESULT
    SystemCollectionOf(
        _In_ DWRITE_FONT_FAMILY_MODEL Model,
        _Outptr_ RxFontCollection** Collection);

private:
    explicit RxWriteFactory(
        _In_ DWRITE_FACTORY_TYPE FactoryType);
    ~RxWriteFactory();

    BOOLEAN
    IsFileLoaderRegistered(
        _In_ IDWriteFontFileLoader* Loader);

    LONG RefCount;
    DWRITE_FACTORY_TYPE Type;
    SRWLOCK Lock;
    std::vector<RxComPtr<IDWriteFontFileLoader>> FileLoaders;
    std::vector<RxComPtr<IDWriteFontCollectionLoader>> CollectionLoaders;
    RxFontCollection* System[2];
    RxGdiInterop* Interop;
    /* Mappings and font cache every system fallback shares, built on first use */
    RxFontFallback* Fallback;
};
