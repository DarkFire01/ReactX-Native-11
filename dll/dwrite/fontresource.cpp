/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font resources: one face of a file with its variation axes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "fontresource.hpp"
#include "fontface.hpp"
#include "factory.hpp"

HRESULT
STDMETHODCALLTYPE
RxWriteFactory::CreateFontResource(
    _In_ IDWriteFontFile* fontFile,
    UINT32 faceIndex,
    _COM_Outptr_ IDWriteFontResource** fontResource)
{
    *fontResource = nullptr;
    if (fontFile == nullptr)
        return E_INVALIDARG;
    return RxFontResource::Create(fontFile, faceIndex, fontResource);
}

/* What the system reports for a buffer longer than the axis list */
constexpr HRESULT RxAxisBufferTooLong = static_cast<HRESULT>(0x88985007);

HRESULT
RxFontResource::Create(
    _In_ IDWriteFontFile* File,
    _In_ UINT32 FaceIndex,
    _COM_Outptr_ IDWriteFontResource** Resource)
{
    RxFontResource* Result;
    RxFaceTables* Tables;
    HRESULT hr;

    *Resource = nullptr;
    hr = RxFaceTables::Create(File, FaceIndex, &Tables);
    if (FAILED(hr))
        return hr;

    Result = new (std::nothrow) RxFontResource();
    if (Result == nullptr)
    {
        Tables->Release();
        return E_OUTOFMEMORY;
    }
    Result->File = RxComPtr<IDWriteFontFile>(File);
    Result->FaceIndex = FaceIndex;
    RxDescribeAxes(Tables, Result->Axes);
    Tables->Release();
    *Resource = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontResource))
    {
        *ppvObject = static_cast<IDWriteFontResource*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontResource)

HRESULT
STDMETHODCALLTYPE
RxFontResource::GetFontFile(
    _COM_Outptr_ IDWriteFontFile** fontFile)
{
    *fontFile = File.Copy();
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxFontResource::GetFontFaceIndex(VOID)
{
    return FaceIndex;
}

UINT32
STDMETHODCALLTYPE
RxFontResource::GetFontAxisCount(VOID)
{
    return static_cast<UINT32>(Axes.size());
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::GetDefaultFontAxisValues(
    _Out_writes_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE* fontAxisValues,
    UINT32 fontAxisValueCount)
{
    memset(fontAxisValues, 0, fontAxisValueCount * sizeof(*fontAxisValues));
    if (fontAxisValueCount < Axes.size())
        return E_NOT_SUFFICIENT_BUFFER;

    for (SIZE_T Index = 0; Index < Axes.size(); Index++)
    {
        fontAxisValues[Index].axisTag = Axes[Index].Tag;
        fontAxisValues[Index].value = RxFixedToFloat(Axes[Index].Default);
    }
    return fontAxisValueCount > Axes.size() ? RxAxisBufferTooLong : S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::GetFontAxisRanges(
    _Out_writes_(fontAxisRangeCount) DWRITE_FONT_AXIS_RANGE* fontAxisRanges,
    UINT32 fontAxisRangeCount)
{
    memset(fontAxisRanges, 0, fontAxisRangeCount * sizeof(*fontAxisRanges));
    if (fontAxisRangeCount < Axes.size())
        return E_NOT_SUFFICIENT_BUFFER;

    for (SIZE_T Index = 0; Index < Axes.size(); Index++)
    {
        fontAxisRanges[Index].axisTag = Axes[Index].Tag;
        fontAxisRanges[Index].minValue = RxFixedToFloat(Axes[Index].Minimum);
        fontAxisRanges[Index].maxValue = RxFixedToFloat(Axes[Index].Maximum);
    }
    return fontAxisRangeCount > Axes.size() ? RxAxisBufferTooLong : S_OK;
}

DWRITE_FONT_AXIS_ATTRIBUTES
STDMETHODCALLTYPE
RxFontResource::GetFontAxisAttributes(
    UINT32 axisIndex)
{
    if (axisIndex >= Axes.size())
        return DWRITE_FONT_AXIS_ATTRIBUTES_NONE;
    return static_cast<DWRITE_FONT_AXIS_ATTRIBUTES>(Axes[axisIndex].Attributes);
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::GetAxisNames(
    UINT32 axisIndex,
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    *names = nullptr;
    if (axisIndex >= Axes.size())
        return E_INVALIDARG;
    return RxLocalizedStrings::Create(Axes[axisIndex].Names, names);
}

UINT32
STDMETHODCALLTYPE
RxFontResource::GetAxisValueNameCount(
    UINT32 axisIndex)
{
    if (axisIndex >= Axes.size())
        return 0;
    return static_cast<UINT32>(Axes[axisIndex].Values.size());
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::GetAxisValueNames(
    UINT32 axisIndex,
    UINT32 axisValueIndex,
    _Out_ DWRITE_FONT_AXIS_RANGE* fontAxisRange,
    _COM_Outptr_ IDWriteLocalizedStrings** names)
{
    const RxAxisValueName* Value;

    *names = nullptr;
    memset(fontAxisRange, 0, sizeof(*fontAxisRange));
    if (axisIndex >= Axes.size() || axisValueIndex >= Axes[axisIndex].Values.size())
        return E_INVALIDARG;

    Value = &Axes[axisIndex].Values[axisValueIndex];
    fontAxisRange->axisTag = Axes[axisIndex].Tag;
    fontAxisRange->minValue = RxFixedToFloat(Value->Minimum);
    fontAxisRange->maxValue = RxFixedToFloat(Value->Maximum);
    return RxLocalizedStrings::Create(Value->Names, names);
}

BOOL
STDMETHODCALLTYPE
RxFontResource::HasVariations(VOID)
{
    for (const auto& Axis : Axes)
    {
        if (Axis.Attributes & DWRITE_FONT_AXIS_ATTRIBUTES_VARIABLE)
            return TRUE;
    }
    return FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::CreateFontFace(
    DWRITE_FONT_SIMULATIONS fontSimulations,
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    _COM_Outptr_ IDWriteFontFace5** fontFace)
{
    std::vector<RxAxisValue> Values;
    RxFontFace* Face;
    HRESULT hr;

    *fontFace = nullptr;
    if ((fontSimulations & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE)) != 0 ||
        fontAxisValueCount >= 0x10000 || (fontAxisValueCount != 0 && fontAxisValues == nullptr))
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < fontAxisValueCount; Index++)
        Values.push_back({ fontAxisValues[Index].axisTag, RxFloatToFixed(fontAxisValues[Index].value) });

    hr = RxFontFace::Create(File.Get(), FaceIndex, fontSimulations, Values.data(), fontAxisValueCount, &Face);
    if (FAILED(hr))
        return hr;
    *fontFace = Face;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontResource::CreateFontFaceReference(
    DWRITE_FONT_SIMULATIONS fontSimulations,
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    _COM_Outptr_ IDWriteFontFaceReference1** fontFaceReference)
{
    *fontFaceReference = nullptr;
    if ((fontSimulations & ~(DWRITE_FONT_SIMULATIONS_BOLD | DWRITE_FONT_SIMULATIONS_OBLIQUE)) != 0 ||
        fontAxisValueCount >= 0x10000 || (fontAxisValueCount != 0 && fontAxisValues == nullptr))
        return E_INVALIDARG;

    /* Font face references are not implemented yet */
    RX_UNIMPLEMENTED();
    return E_NOTIMPL;
}
