/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font resources: one face of a file with its variation axes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "names.hpp"

class RxFontResource final : public IDWriteFontResource
{
public:
    static
    HRESULT
    Create(
        _In_ IDWriteFontFile* File,
        _In_ UINT32 FaceIndex,
        _COM_Outptr_ IDWriteFontResource** Resource);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetFontFile(
        _COM_Outptr_ IDWriteFontFile** fontFile) override;

    UINT32
    STDMETHODCALLTYPE
    GetFontFaceIndex(VOID) override;

    UINT32
    STDMETHODCALLTYPE
    GetFontAxisCount(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDefaultFontAxisValues(
        _Out_writes_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE* fontAxisValues,
        UINT32 fontAxisValueCount) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFontAxisRanges(
        _Out_writes_(fontAxisRangeCount) DWRITE_FONT_AXIS_RANGE* fontAxisRanges,
        UINT32 fontAxisRangeCount) override;

    DWRITE_FONT_AXIS_ATTRIBUTES
    STDMETHODCALLTYPE
    GetFontAxisAttributes(
        UINT32 axisIndex) override;

    HRESULT
    STDMETHODCALLTYPE
    GetAxisNames(
        UINT32 axisIndex,
        _COM_Outptr_ IDWriteLocalizedStrings** names) override;

    UINT32
    STDMETHODCALLTYPE
    GetAxisValueNameCount(
        UINT32 axisIndex) override;

    HRESULT
    STDMETHODCALLTYPE
    GetAxisValueNames(
        UINT32 axisIndex,
        UINT32 axisValueIndex,
        _Out_ DWRITE_FONT_AXIS_RANGE* fontAxisRange,
        _COM_Outptr_ IDWriteLocalizedStrings** names) override;

    BOOL
    STDMETHODCALLTYPE
    HasVariations(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFace(
        DWRITE_FONT_SIMULATIONS fontSimulations,
        _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
        UINT32 fontAxisValueCount,
        _COM_Outptr_ IDWriteFontFace5** fontFace) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFontFaceReference(
        DWRITE_FONT_SIMULATIONS fontSimulations,
        _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
        UINT32 fontAxisValueCount,
        _COM_Outptr_ IDWriteFontFaceReference1** fontFaceReference) override;

private:
    RxFontResource() : RefCount(1), FaceIndex(0) {}

    LONG RefCount;
    RxComPtr<IDWriteFontFile> File;
    UINT32 FaceIndex;
    std::vector<RxFontAxisInfo> Axes;
};
