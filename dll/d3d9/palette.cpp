/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Texture palettes: the numbered palette table and the current palette
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* The palette table grows by this many numbers at a time */
static constexpr UINT D9PaletteGrowth = 8;

/* Makes room for a palette number; existing palettes keep their slots */
HRESULT
D9Device::ReservePalette(
    _In_ UINT Number)
{
    D9Palette** Grown;
    UINT Capacity;

    if (Number < PaletteCapacity)
        return S_OK;
    if (Number > UINT_MAX - D9PaletteGrowth)
        return E_OUTOFMEMORY;

    Capacity = (Number / D9PaletteGrowth + 1) * D9PaletteGrowth;
    Grown = new (std::nothrow) D9Palette*[Capacity]();
    if (Grown == nullptr)
        return E_OUTOFMEMORY;
    for (UINT Index = 0; Index < PaletteCapacity; Index++)
        Grown[Index] = Palettes[Index];
    delete[] Palettes;
    Palettes = Grown;
    PaletteCapacity = Capacity;
    return S_OK;
}

VOID
D9Device::FreePalettes(VOID)
{
    for (UINT Index = 0; Index < PaletteCapacity; Index++)
        delete Palettes[Index];
    delete[] Palettes;
    Palettes = nullptr;
    PaletteCapacity = 0;
}

/* Entries other than fully opaque need a device that blends with palette alpha */
STDMETHODIMP
D9Device::SetPaletteEntries(
    _In_ UINT PaletteNumber,
    _In_opt_ const PALETTEENTRY* Entries)
{
    D9_DEVICE_LOCK();
    BOOLEAN Translucent = FALSE;
    HRESULT hr;

    if (NotReset || Entries == nullptr)
        return D3DERR_INVALIDCALL;

    for (UINT Index = 0; Index < _countof(Palettes[0]->Entries); Index++)
    {
        if (Entries[Index].peFlags != 0xFF)
            Translucent = TRUE;
    }
    if (Translucent && (Caps.TextureCaps & D3DPTEXTURECAPS_ALPHAPALETTE) == 0)
    {
        D9_TRACE_ONCE(L"SetPaletteEntries: palette alpha needs D3DPTEXTURECAPS_ALPHAPALETTE");
        return D3DERR_INVALIDCALL;
    }

    hr = ReservePalette(PaletteNumber);
    if (FAILED(hr))
        return hr;
    if (Palettes[PaletteNumber] == nullptr)
    {
        Palettes[PaletteNumber] = new (std::nothrow) D9Palette;
        if (Palettes[PaletteNumber] == nullptr)
            return E_OUTOFMEMORY;
    }
    CopyMemory(Palettes[PaletteNumber]->Entries, Entries, sizeof(Palettes[PaletteNumber]->Entries));
    return S_OK;
}

STDMETHODIMP
D9Device::GetPaletteEntries(
    _In_ UINT PaletteNumber,
    _Out_ PALETTEENTRY* Entries)
{
    D9_DEVICE_LOCK();

    if (NotReset || Entries == nullptr)
        return D3DERR_INVALIDCALL;
    if (PaletteNumber >= PaletteCapacity || Palettes[PaletteNumber] == nullptr)
    {
        D9_TRACE_ONCE(L"GetPaletteEntries: palette %u was never set", PaletteNumber);
        return D3DERR_INVALIDCALL;
    }

    CopyMemory(Entries, Palettes[PaletteNumber]->Entries, sizeof(Palettes[PaletteNumber]->Entries));
    return S_OK;
}

/* Any number is taken, set or not; a state block being recorded keeps it instead */
STDMETHODIMP
D9Device::SetCurrentTexturePalette(
    _In_ UINT PaletteNumber)
{
    D9_DEVICE_LOCK();

    if (NotReset)
        return D3DERR_INVALIDCALL;

    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::TexturePalette, 0, 0, &PaletteNumber, sizeof(PaletteNumber));
    CurrentPalette = PaletteNumber;
    return S_OK;
}

STDMETHODIMP
D9Device::GetCurrentTexturePalette(
    _Out_ UINT* PaletteNumber)
{
    D9_DEVICE_LOCK();

    if (NotReset || PaletteNumber == nullptr)
        return D3DERR_INVALIDCALL;

    *PaletteNumber = CurrentPalette;
    return S_OK;
}
