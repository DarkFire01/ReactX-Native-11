/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The newer collection, family and font interfaces
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "collection.hpp"

#include <cmath>

/* RxFontCollection */

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFamily(
    UINT32 index,
    _COM_Outptr_ IDWriteFontFamily1** fontFamily)
{
    return GetFontFamily(index, reinterpret_cast<IDWriteFontFamily**>(fontFamily));
}

HRESULT
STDMETHODCALLTYPE
RxFontCollection::GetFontFamily(
    UINT32 index,
    _COM_Outptr_ IDWriteFontFamily2** fontFamily)
{
    return GetFontFamily(index, reinterpret_cast<IDWriteFontFamily**>(fontFamily));
}

DWRITE_FONT_FAMILY_MODEL
STDMETHODCALLTYPE
RxFontCollection::GetFontFamilyModel(VOID)
{
    return FamilyModel;
}

/* RxFontFamily */

HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetFont(
    UINT32 listIndex,
    _COM_Outptr_ IDWriteFont3** font)
{
    return GetFont(listIndex, reinterpret_cast<IDWriteFont**>(font));
}

DWRITE_LOCALITY
STDMETHODCALLTYPE
RxFontFamily::GetFontLocality(
    UINT32 listIndex)
{
    UNREFERENCED_PARAMETER(listIndex);
    return DWRITE_LOCALITY_LOCAL;
}

/* RxFont */

VOID
STDMETHODCALLTYPE
RxFont::GetMetrics(
    _Out_ DWRITE_FONT_METRICS1* fontMetrics)
{
    RxFontFace* Result;

    memset(fontMetrics, 0, sizeof(*fontMetrics));
    if (SUCCEEDED(Face(&Result)))
        Result->GetMetrics(fontMetrics);
}

VOID
STDMETHODCALLTYPE
RxFont::GetPanose(
    _Out_ DWRITE_PANOSE* panose)
{
    RxFontFace* Result;

    memset(panose, 0, sizeof(*panose));
    if (SUCCEEDED(Face(&Result)))
        Result->GetPanose(panose);
}

BOOL
STDMETHODCALLTYPE
RxFont::IsMonospacedFont(VOID)
{
    RxFontFace* Result;

    return SUCCEEDED(Face(&Result)) ? Result->IsMonospacedFont() : FALSE;
}

BOOL
STDMETHODCALLTYPE
RxFont::IsColorFont(VOID)
{
    RxFontFace* Result;

    return SUCCEEDED(Face(&Result)) ? Result->IsColorFont() : FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxFont::CreateFontFace(
    _COM_Outptr_ IDWriteFontFace3** fontFace)
{
    return CreateFontFace(reinterpret_cast<IDWriteFontFace**>(fontFace));
}

BOOL
STDMETHODCALLTYPE
RxFont::Equals(
    IDWriteFont* font)
{
    RxFont* Other = RxFont::FromInterface(font);

    return Other != nullptr && Other->Collection->Data() == Collection->Data() && Other->Item.Entry == Item.Entry &&
           Other->Item.Simulations == Item.Simulations;
}

BOOL
STDMETHODCALLTYPE
RxFont::HasCharacter(
    UINT32 unicodeValue)
{
    BOOL Exists = FALSE;

    HasCharacter(unicodeValue, &Exists);
    return Exists;
}

DWRITE_LOCALITY
STDMETHODCALLTYPE
RxFont::GetLocality(VOID)
{
    return DWRITE_LOCALITY_LOCAL;
}

/* Axis requests reduced to weight, stretch and style; variable instances are not listed yet */
HRESULT
STDMETHODCALLTYPE
RxFontFamily::GetMatchingFonts(
    _In_reads_(fontAxisValueCount) DWRITE_FONT_AXIS_VALUE const* fontAxisValues,
    UINT32 fontAxisValueCount,
    _COM_Outptr_ IDWriteFontList2** matchingFonts)
{
    static const FLOAT Widths[] = { 50.0f, 62.5f, 75.0f, 87.5f, 100.0f, 112.5f, 125.0f, 150.0f, 200.0f };
    FLOAT Weight = 400.0f;
    FLOAT Width = 100.0f;
    FLOAT Italic = 0.0f;
    FLOAT Slant = 0.0f;
    UINT32 Stretch = DWRITE_FONT_STRETCH_NORMAL;
    DWRITE_FONT_STYLE Style = DWRITE_FONT_STYLE_NORMAL;
    IDWriteFontList* List;
    HRESULT hr;

    if (matchingFonts == nullptr)
        return E_INVALIDARG;
    *matchingFonts = nullptr;
    if (fontAxisValueCount >= 65536 || (fontAxisValueCount != 0 && fontAxisValues == nullptr))
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < fontAxisValueCount; Index++)
    {
        if (fontAxisValues[Index].axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT)
            Weight = fontAxisValues[Index].value;
        else if (fontAxisValues[Index].axisTag == DWRITE_FONT_AXIS_TAG_WIDTH)
            Width = fontAxisValues[Index].value;
        else if (fontAxisValues[Index].axisTag == DWRITE_FONT_AXIS_TAG_ITALIC)
            Italic = fontAxisValues[Index].value;
        else if (fontAxisValues[Index].axisTag == DWRITE_FONT_AXIS_TAG_SLANT)
            Slant = fontAxisValues[Index].value;
    }

    for (UINT32 Index = 0; Index < _countof(Widths); Index++)
    {
        if (std::fabs(Widths[Index] - Width) < std::fabs(Widths[Stretch - 1] - Width))
            Stretch = Index + 1;
    }
    if (Italic >= 0.5f)
        Style = DWRITE_FONT_STYLE_ITALIC;
    else if (Slant <= -10.0f)
        Style = DWRITE_FONT_STYLE_OBLIQUE;

    hr = GetMatchingFonts(static_cast<DWRITE_FONT_WEIGHT>(std::min(1000.0f, std::max(1.0f, Weight + 0.5f))),
                          static_cast<DWRITE_FONT_STRETCH>(Stretch), Style, &List);
    if (FAILED(hr))
        return hr;

    /* The list came back as the family interface; hand out its list face */
    *matchingFonts = static_cast<IDWriteFontList2*>(static_cast<RxFontFamily*>(static_cast<IDWriteFontFamily2*>(List)));
    return S_OK;
}
