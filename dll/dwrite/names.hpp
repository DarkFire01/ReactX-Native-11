/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Localized names and the weight, stretch and style of a face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "facetables.hpp"
#include "variations.hpp"

#include <string>

/* One name per language, sorted by lowercase language tag */
struct RxNameEntry
{
    std::wstring Locale;
    std::wstring Text;
};

typedef std::vector<RxNameEntry> RxNameList;

/* The en-us entry, else the first one */
const std::wstring&
RxIdentityName(
    _In_ const RxNameList& Names);

/* What a face contributes to a collection */
struct RxFaceDescription
{
    RxNameList WwsFamily;
    RxNameList WwsFace;
    RxNameList TypographicFamily;
    RxNameList TypographicFace;
    RxNameList Win32Family;
    RxNameList Win32Face;
    RxNameList FullName;
    RxNameList PostscriptName;
    USHORT Weight;
    USHORT Win32Weight;
    BYTE Stretch;
    BYTE Style;
    BYTE Win32Style;
    /* Names came from STAT, so the informational strings report them too */
    BOOLEAN FromStyleTable;
    /* The face has fvar axes */
    BOOLEAN HasVariations;
    /* Values on every axis the face reports, design axes first */
    std::vector<RxAxisValue> AxisValues;
};

/* Reads every record of a name ID, one per language */
VOID
RxReadNames(
    _In_ const RxFaceTables* Face,
    _In_ USHORT NameId,
    _Inout_ RxNameList& Names);

/* Naming and style values the way a font set stores them; Coordinates are the instance axis values */
VOID
RxDescribeFace(
    _In_ const RxFaceTables* Face,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _Out_ RxFaceDescription* Description);

/* The STAT path of RxDescribeFace */
VOID
RxDescribeStyledFace(
    _In_ const RxFaceTables* Face,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _Out_ RxFaceDescription* Description);

/* Axis values a face reports for instance Coordinates, standard axes valued from Weight, Stretch and Style */
VOID
RxFaceAxisValues(
    _In_ const RxFaceTables* Face,
    _In_ const std::vector<RxAxisValue>& Coordinates,
    _In_ USHORT Weight,
    _In_ BYTE Stretch,
    _In_ BYTE Style,
    _Out_ std::vector<RxAxisValue>& Values);

/* OS/2 and head values */
USHORT
RxFaceWeight(
    _In_ const RxFaceHeader& Header);

BYTE
RxFaceStyle(
    _In_ const RxFaceHeader& Header);

BYTE
RxFaceStretch(
    _In_ const RxFaceHeader& Header);

/* Values from wght, wdth, ital and slnt; the OS/2 values for an axis that is missing */
USHORT
RxAxisWeight(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values);

BYTE
RxAxisStretch(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values);

BYTE
RxAxisStyle(
    _In_ const RxFaceHeader& Header,
    _In_ const std::vector<RxAxisValue>& Values);

/* Face name of a face with simulations applied */
std::wstring
RxSimulatedFaceName(
    _In_ const std::wstring& Face,
    _In_ DWRITE_FONT_SIMULATIONS Simulations);

class RxLocalizedStrings final : public IDWriteLocalizedStrings
{
public:
    static
    HRESULT
    Create(
        _In_ const RxNameList& Names,
        _COM_Outptr_ IDWriteLocalizedStrings** Strings);

    RX_DECLARE_IUNKNOWN

    UINT32
    STDMETHODCALLTYPE
    GetCount(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    FindLocaleName(
        _In_z_ WCHAR const* localeName,
        _Out_ UINT32* index,
        _Out_ BOOL* exists) override;

    HRESULT
    STDMETHODCALLTYPE
    GetLocaleNameLength(
        UINT32 index,
        _Out_ UINT32* length) override;

    HRESULT
    STDMETHODCALLTYPE
    GetLocaleName(
        UINT32 index,
        _Out_writes_z_(size) WCHAR* localeName,
        UINT32 size) override;

    HRESULT
    STDMETHODCALLTYPE
    GetStringLength(
        UINT32 index,
        _Out_ UINT32* length) override;

    HRESULT
    STDMETHODCALLTYPE
    GetString(
        UINT32 index,
        _Out_writes_z_(size) WCHAR* stringBuffer,
        UINT32 size) override;

private:
    RxLocalizedStrings() : RefCount(1) {}

    LONG RefCount;
    RxNameList Entries;
};
