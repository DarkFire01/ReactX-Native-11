/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font collections, families and fonts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "fontface.hpp"
#include "names.hpp"

/* One face of one file as a collection knows it */
struct RxFontEntry
{
    RxComPtr<IDWriteFontFile> File;
    UINT32 FaceIndex;
    DWRITE_FONT_FACE_TYPE FaceType;
    BOOLEAN IsSymbol;
    /* OS/2 Unicode and code page ranges, as GetFontSignature reports them */
    FONTSIGNATURE Signature;
    RxFaceDescription Description;
};

/* A face as a family lists it, possibly simulated */
struct RxFontItem
{
    UINT32 Entry;
    DWRITE_FONT_SIMULATIONS Simulations;
};

struct RxFamilyData
{
    RxNameList Names;
    std::vector<RxFontItem> Items;
};

/* The faces and families behind a collection; shared and never changed once built */
class RxFontSetData
{
public:
    /* The installed fonts, as the registry lists them */
    static
    HRESULT
    CreateSystem(
        _Outptr_ RxFontSetData** Data);

    /* Faces of the given files, in order */
    static
    HRESULT
    CreateFromFiles(
        _In_reads_(Count) IDWriteFontFile* const* Files,
        _In_ UINT32 Count,
        _Outptr_ RxFontSetData** Data);

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

    /* MAXUINT32 when no family of the model has the name */
    UINT32
    FindFamily(
        _In_z_ PCWSTR Name,
        _In_ DWRITE_FONT_FAMILY_MODEL Model) const;

    /* The family listing an entry; MAXUINT32 if none does */
    UINT32
    FamilyOfEntry(
        _In_ UINT32 Entry,
        _In_ DWRITE_FONT_FAMILY_MODEL Model) const;

    std::vector<RxFontEntry> Entries;
    /* Indexed by DWRITE_FONT_FAMILY_MODEL: typographic, then weight stretch style */
    std::vector<RxFamilyData> Families[2];
    /* Built from the installed fonts */
    BOOLEAN IsSystem;

private:
    RxFontSetData() : IsSystem(FALSE), RefCount(1) {}

    VOID
    AddFile(
        _In_ IDWriteFontFile* File);

    /* System set order: faces of one WWS family together, by weight, style and stretch */
    VOID
    GroupEntries(VOID);

    VOID
    BuildFamilies(
        _In_ DWRITE_FONT_FAMILY_MODEL Model);

    struct NameIndex
    {
        std::wstring Name;
        UINT32 Family;
    };

    LONG RefCount;
    std::vector<NameIndex> NameMap[2];
};

/* Weight and style a face shows with its simulations */
USHORT
RxEffectiveWeight(
    _In_ const RxFontEntry& Entry,
    _In_ DWRITE_FONT_SIMULATIONS Simulations);

BYTE
RxEffectiveStyle(
    _In_ const RxFontEntry& Entry,
    _In_ DWRITE_FONT_SIMULATIONS Simulations);

/* Index into Items of the best match, with the simulations it needs */
SIZE_T
RxFindMatchingItem(
    _In_ const RxFontSetData* Data,
    _In_ const std::vector<RxFontItem>& Items,
    _In_ DWRITE_FONT_WEIGHT Weight,
    _In_ DWRITE_FONT_STRETCH Stretch,
    _In_ DWRITE_FONT_STYLE Style,
    _Out_ DWRITE_FONT_SIMULATIONS* Simulations);

/* GDI weight, style, stretch and charset distance of an entry; Simulations gets what closes the gap */
UINT32
RxWin32Penalty(
    _In_ const RxFontEntry& Entry,
    _In_ UINT32 CharsetMask,
    _In_ LONG Weight,
    _In_ BOOLEAN Italic,
    _Out_ DWRITE_FONT_SIMULATIONS* Simulations);

/* Entries whose Win32 family name is Name, else those whose full name is */
VOID
RxWin32Candidates(
    _In_ const RxFontSetData* Data,
    _In_reads_(Length) PCWCH Name,
    _In_ SIZE_T Length,
    _Inout_ std::vector<UINT32>& Entries);

class RxFontCollection final : public IDWriteFontCollection3
{
public:
    static
    HRESULT
    Create(
        _In_ RxFontSetData* Data,
        _In_ DWRITE_FONT_FAMILY_MODEL Model,
        _Outptr_ RxFontCollection** Collection);

    RX_DECLARE_IUNKNOWN

#include "collection.inl"

    RxFontSetData*
    Data(VOID) const
    {
        return SetData;
    }

    DWRITE_FONT_FAMILY_MODEL
    Model(VOID) const
    {
        return FamilyModel;
    }

    /* The families of the model this collection groups by */
    const std::vector<RxFamilyData>&
    FamilyList(VOID) const
    {
        return SetData->Families[FamilyModel];
    }

    static
    RxFontCollection*
    FromInterface(
        _In_opt_ IUnknown* Collection);

private:
    RxFontCollection() : RefCount(1), SetData(nullptr), FamilyModel(DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE) {}
    ~RxFontCollection();

    LONG RefCount;
    RxFontSetData* SetData;
    DWRITE_FONT_FAMILY_MODEL FamilyModel;
};

class RxFontFamily final : public IDWriteFontFamily2, public IDWriteFontList2
{
public:
    static
    HRESULT
    Create(
        _In_ RxFontCollection* Collection,
        _In_ UINT32 Family,
        _In_ BOOLEAN AddSimulations,
        _Outptr_ RxFontFamily** Result);

    /* A matching font list over chosen items */
    static
    HRESULT
    CreateList(
        _In_ RxFontCollection* Collection,
        _In_ UINT32 Family,
        _In_ std::vector<RxFontItem>&& Items,
        _Outptr_ RxFontFamily** Result);

    RX_DECLARE_IUNKNOWN

#include "family.inl"

private:
    RxFontFamily() : RefCount(1), FamilyIndex(0) {}

    LONG RefCount;
    RxComPtr<RxFontCollection> Collection;
    UINT32 FamilyIndex;
    std::vector<RxFontItem> Items;
};

class RxFont final : public IDWriteFont3
{
public:
    static
    HRESULT
    Create(
        _In_ RxFontCollection* Collection,
        _In_ UINT32 Family,
        _In_ const RxFontItem& Item,
        _Outptr_ RxFont** Result);

    RX_DECLARE_IUNKNOWN

#include "font.inl"

    static
    RxFont*
    FromInterface(
        _In_opt_ IUnknown* Font);

    const RxFontEntry&
    Entry(VOID) const
    {
        return Collection->Data()->Entries[Item.Entry];
    }

    RxFontCollection*
    Owner(VOID) const
    {
        return Collection.Get();
    }

private:
    RxFont() : RefCount(1), FamilyIndex(0), Item{} {}

    HRESULT
    Face(
        _Outptr_ RxFontFace** Face);

    LONG RefCount;
    RxComPtr<RxFontCollection> Collection;
    UINT32 FamilyIndex;
    RxFontItem Item;
    RxComPtr<IDWriteFontFace> CachedFace;
};
