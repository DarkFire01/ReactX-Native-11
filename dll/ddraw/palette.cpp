/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectDrawPalette objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

extern const VOID* const DdPaletteVtbl[];

/* Palette record flags; the size flags follow the old internal numbering */
enum : ULONG
{
    DdPalette256 = 0x00000001,
    DdPalette16 = 0x00000002,
    DdPaletteHelMade = 0x00000180,
    DdPaletteAllow256 = 0x00000200,
    DdPalette4 = 0x00000400,
    DdPalette2 = 0x00000800,
    DdPaletteIndexEntries = 0x00001000,
    DdPaletteAlpha = 0x00002000,
};

static constexpr DWORD DdPaletteCreateMask = 0x000007FF;
static constexpr DWORD DdPalettePrimaryFlags = DDPCAPS_PRIMARYSURFACE | DDPCAPS_PRIMARYSURFACELEFT | DDPCAPS_VSYNC;

static
DWORD
DdPaletteEntryCount(
    _In_ ULONG Flags)
{
    if ((Flags & DdPalette2) != 0)
        return 2;
    if ((Flags & DdPalette4) != 0)
        return 4;
    if ((Flags & DdPalette16) != 0)
        return 16;
    return 256;
}

static
DWORD
DdPaletteEntrySize(
    _In_ ULONG Flags)
{
    return (Flags & DdPaletteIndexEntries) != 0 ? 1 : sizeof(PALETTEENTRY);
}

/* The emulation layer keeps the first and last entries of a full 256 color palette black and white */
static
VOID
DdPinSystemEntries(
    _Inout_ DdPalette* Palette)
{
    if ((Palette->Flags & DdPalette256) == 0 || (Palette->Flags & DdPaletteAllow256) != 0)
        return;

    Palette->Entries[0] = 0;
    Palette->Entries[1] = 0;
    Palette->Entries[2] = 0;
    Palette->Entries[1020] = 0xFF;
    Palette->Entries[1021] = 0xFF;
    Palette->Entries[1022] = 0xFF;
}

static
HRESULT
STDMETHODCALLTYPE
DdCreatePaletteObject(
    _In_ DdInterface* This,
    _In_ DWORD Flags,
    _In_ LPPALETTEENTRY Initial,
    _Out_ LPDIRECTDRAWPALETTE* Result,
    _In_opt_ IUnknown* Outer)
{
    DdLocal* Local = This->Local;
    DdDriver* Driver = Local->Driver;
    DdPalette* Palette;
    ULONG Record;
    DWORD Sizes;
    DWORD Bytes;

    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;

    DdLockScope Lock;

    if (Result == nullptr)
        return E_INVALIDARG;
    *Result = nullptr;

    if ((Flags & ~DdPaletteCreateMask) != 0)
        return E_INVALIDARG;
    if ((Local->Flags & DDRAWILCL_SETCOOPCALLED) == 0)
        return DDERR_NOCOOPERATIVELEVELSET;
    if ((Flags & DdPalettePrimaryFlags) != 0)
        return E_INVALIDARG;

    Sizes = ((Flags & DDPCAPS_1BIT) != 0) + ((Flags & DDPCAPS_2BIT) != 0) + ((Flags & DDPCAPS_4BIT) != 0) +
            ((Flags & DDPCAPS_8BIT) != 0);
    if (Sizes != 1 || ((Flags & DDPCAPS_ALPHA) != 0 && (Flags & DDPCAPS_8BITENTRIES) != 0))
        return E_INVALIDARG;
    if ((Flags & DDPCAPS_8BIT) != 0 ? (Flags & DDPCAPS_8BITENTRIES) != 0 : (Flags & DDPCAPS_ALLOW256) != 0)
        return E_INVALIDARG;

    if ((Flags & DDPCAPS_1BIT) != 0)
        Record = DdPalette2;
    else if ((Flags & DDPCAPS_2BIT) != 0)
        Record = DdPalette4;
    else if ((Flags & DDPCAPS_4BIT) != 0)
        Record = DdPalette16;
    else
        Record = DdPalette256;

    if ((Flags & DDPCAPS_ALLOW256) != 0)
        Record |= DdPaletteAllow256;
    if ((Flags & DDPCAPS_8BITENTRIES) != 0)
        Record |= DdPaletteIndexEntries;
    if ((Flags & DDPCAPS_ALPHA) != 0)
        Record |= DdPaletteAlpha;

    /* Palettes need either the driver or the emulation layer to take them */
    if ((Driver->HalCaps.ddsCaps.dwCaps & DDSCAPS_PALETTE) == 0 && (Driver->HelCaps.ddsCaps.dwCaps & DDSCAPS_PALETTE) == 0)
        return E_NOTIMPL;

    Palette = new (std::nothrow) DdPalette;
    if (Palette == nullptr)
        return E_OUTOFMEMORY;

    ZeroMemory(Palette, sizeof(*Palette));
    Bytes = DdPaletteEntryCount(Record) * DdPaletteEntrySize(Record);
    Palette->Entries = static_cast<BYTE*>(LocalAlloc(LPTR, Bytes));
    if (Palette->Entries == nullptr)
    {
        delete Palette;
        return E_OUTOFMEMORY;
    }

    memcpy(Palette->Entries, Initial, Bytes);
    Palette->Vtbl = DdPaletteVtbl;
    Palette->Flags = Record | DdPaletteHelMade;
    Palette->RefCount = 1;
    Palette->Driver = Driver;
    Palette->Creator = Local;
    Palette->ProcessId = GetCurrentProcessId();
    DdPinSystemEntries(Palette);

    Palette->Next = Driver->Palettes;
    Driver->Palettes = Palette;

    if (!DdIsOldDraw(This))
    {
        DdAddRefInterface(This);
        Palette->Owner = This;
    }

    *Result = reinterpret_cast<LPDIRECTDRAWPALETTE>(Palette);
    return S_OK;
}

HRESULT
DdNewPalette(
    _In_ DdInterface* This,
    _In_ DWORD Flags,
    _In_ LPPALETTEENTRY Initial,
    _Out_ LPDIRECTDRAWPALETTE* Result,
    _In_opt_ IUnknown* Outer)
{
    return DdCreatePaletteObject(This, Flags, Initial, Result, Outer);
}

static
DdPalette*
DdPaletteFromCom(
    _In_ VOID* Com)
{
    return static_cast<DdPalette*>(Com);
}

static
HRESULT
STDMETHODCALLTYPE
DdPaletteQueryInterface(
    _In_ VOID* Com,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object)
{
    DdPalette* Palette = DdPaletteFromCom(Com);

    DdLockScope Lock;

    if (Iid == nullptr || Object == nullptr)
        return E_INVALIDARG;
    if (!IsEqualGUID(*Iid, IID_IUnknown) && !IsEqualGUID(*Iid, IID_IDirectDrawPalette))
        return E_FAIL;

    Palette->RefCount += 1;
    *Object = Palette;
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
DdPaletteAddRef(
    _In_ VOID* Com)
{
    DdPalette* Palette = DdPaletteFromCom(Com);

    DdLockScope Lock;

    Palette->RefCount += 1;
    return Palette->RefCount;
}

ULONG
DdReleasePalette(
    _In_ DdPalette* Palette)
{
    DdPalette** Link;
    DdInterface* Owner;
    ULONG Remaining;

    Palette->RefCount -= 1;
    Remaining = Palette->RefCount;
    if (Remaining != 0)
        return Remaining;

    for (Link = &Palette->Driver->Palettes; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Palette)
        {
            *Link = Palette->Next;
            break;
        }
    }

    Owner = Palette->Owner;
    LocalFree(Palette->Entries);
    Palette->Vtbl = nullptr;
    delete Palette;

    if (Owner != nullptr)
        DdReleaseInterface(Owner);
    return 0;
}

/* Palettes a DirectDraw object made die with it */
VOID
DdReleaseLocalPalettes(
    _In_ DdLocal* Local)
{
    DdPalette* Palette;
    DdPalette* Next;

    for (Palette = Local->Driver->Palettes; Palette != nullptr; Palette = Next)
    {
        Next = Palette->Next;
        if (Palette->Creator == Local && Palette->ProcessId == Local->ProcessId)
        {
            Palette->RefCount = 1;
            DdReleasePalette(Palette);
        }
    }
}

static
ULONG
STDMETHODCALLTYPE
DdPaletteRelease(
    _In_ VOID* Com)
{
    DdPalette* Palette = DdPaletteFromCom(Com);

    DdLockScope Lock;

    if (Palette == nullptr || Palette->Vtbl != DdPaletteVtbl)
        return 0;
    return DdReleasePalette(Palette);
}

static
HRESULT
STDMETHODCALLTYPE
DdPaletteGetCaps(
    _In_ VOID* Com,
    _Out_ DWORD* Caps)
{
    DdPalette* Palette = DdPaletteFromCom(Com);
    DWORD Size;
    DWORD Value;

    DdLockScope Lock;

    if (Caps == nullptr)
        return E_INVALIDARG;

    if ((Palette->Flags & DdPalette2) != 0)
        Size = DDPCAPS_1BIT;
    else if ((Palette->Flags & DdPalette4) != 0)
        Size = DDPCAPS_2BIT;
    else if ((Palette->Flags & DdPalette16) != 0)
        Size = DDPCAPS_4BIT;
    else
        Size = DDPCAPS_8BIT;

    Value = Size;
    if (Palette->OnPrimary)
        Value |= DDPCAPS_PRIMARYSURFACE;
    if ((Palette->Flags & DdPaletteAllow256) != 0)
        Value |= DDPCAPS_ALLOW256;
    if ((Palette->Flags & DdPaletteIndexEntries) != 0)
        Value |= DDPCAPS_8BITENTRIES;

    *Caps = Value;
    return S_OK;
}

static
BOOLEAN
DdPaletteRangeValid(
    _In_ const DdPalette* Palette,
    _In_ DWORD Flags,
    _In_ DWORD Start,
    _In_ DWORD Count)
{
    DWORD Total = DdPaletteEntryCount(Palette->Flags);

    return Flags == 0 && Count != 0 && Count <= Total && Start < Total && Start + Count <= Total;
}

static
HRESULT
STDMETHODCALLTYPE
DdPaletteGetEntries(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_ DWORD Start,
    _In_ DWORD Count,
    _Out_writes_(Count) LPPALETTEENTRY Entries)
{
    DdPalette* Palette = DdPaletteFromCom(Com);
    DWORD Size;

    DdLockScope Lock;

    if (!DdPaletteRangeValid(Palette, Flags, Start, Count) || Entries == nullptr)
        return E_INVALIDARG;

    Size = DdPaletteEntrySize(Palette->Flags);
    memcpy(Entries, Palette->Entries + Start * Size, Count * Size);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdPaletteInitialize(
    _In_ VOID* Com,
    _In_opt_ LPDIRECTDRAW Draw,
    _In_ DWORD Flags,
    _In_opt_ LPPALETTEENTRY Entries)
{
    return DDERR_ALREADYINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdPaletteSetEntries(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_ DWORD Start,
    _In_ DWORD Count,
    _In_reads_(Count) LPPALETTEENTRY Entries)
{
    DdPalette* Palette = DdPaletteFromCom(Com);
    DWORD Size;

    DdLockScope Lock;

    if (!DdPaletteRangeValid(Palette, Flags, Start, Count) || Entries == nullptr)
        return E_INVALIDARG;

    Size = DdPaletteEntrySize(Palette->Flags);
    memcpy(Palette->Entries + Start * Size, Entries, Count * Size);

    /* A palette on the primary is realized when it is presented */
    if (Start == 0 || Start + Count == 256)
        DdPinSystemEntries(Palette);
    return S_OK;
}

#define DD_PALETTE_SLOT(Function) reinterpret_cast<const VOID*>(&Function)

const VOID* const DdPaletteVtbl[] =
{
    DD_PALETTE_SLOT(DdPaletteQueryInterface), DD_PALETTE_SLOT(DdPaletteAddRef), DD_PALETTE_SLOT(DdPaletteRelease),
    DD_PALETTE_SLOT(DdPaletteGetCaps), DD_PALETTE_SLOT(DdPaletteGetEntries), DD_PALETTE_SLOT(DdPaletteInitialize),
    DD_PALETTE_SLOT(DdPaletteSetEntries),
};
