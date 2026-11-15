/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectWrite factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "fontface.hpp"

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

private:
    explicit RxWriteFactory(
        _In_ DWRITE_FACTORY_TYPE FactoryType);

    BOOLEAN
    IsFileLoaderRegistered(
        _In_ IDWriteFontFileLoader* Loader);

    LONG RefCount;
    DWRITE_FACTORY_TYPE Type;
    SRWLOCK Lock;
    std::vector<RxComPtr<IDWriteFontFileLoader>> FileLoaders;
    std::vector<RxComPtr<IDWriteFontCollectionLoader>> CollectionLoaders;
};
