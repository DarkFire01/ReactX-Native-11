/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback effects and effect files
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

HRESULT
DiReadAttributes(
    _In_ HKEY Key,
    _Out_ LPDIFFDEVICEATTRIBUTES Attributes)
{
    return DiReadValue(Key, L"Attributes", Attributes, sizeof(*Attributes));
}

BOOLEAN
DiReadEffectEntry(
    _In_ HKEY Effects,
    _In_ DWORD Index,
    _Inout_ RX_DI_EFFECT_ENTRY* Entry)
{
    WCHAR Name[39];
    BOOLEAN Usable = FALSE;
    HKEY Key;
    LONG Error;

    /* Each effect is a subkey named by its GUID */
    if (RegEnumKeyW(Effects, Index, Name, _countof(Name)) != ERROR_SUCCESS || !DiParseGuid(Name, &Entry->Guid))
        return FALSE;
    if (RegOpenKeyExW(Effects, Name, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    /* A name too long for the record is cut and the effect is left out */
    Error = DiReadString(Key, nullptr, Entry->Name, _countof(Entry->Name));
    if (Error == ERROR_MORE_DATA)
    {
        Entry->Name[_countof(Entry->Name) - 1] = L'\0';
    }
    else
    {
        if (Error != ERROR_SUCCESS)
            Entry->Name[0] = L'\0';

        /* Only effects with a direction system the driver understands are offered */
        if ((Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND) &&
            SUCCEEDED(DiReadValue(Key, L"Attributes", Entry, FIELD_OFFSET(RX_DI_EFFECT_ENTRY, Guid))) &&
            (Entry->Coordinates & (DIEFF_CARTESIAN | DIEFF_POLAR | DIEFF_SPHERICAL)) != 0)
        {
            Usable = TRUE;
        }
    }

    RegCloseKey(Key);
    return Usable;
}

HRESULT
DiCreateEffect(
    _In_ RxDiDevice* Device,
    _In_ REFGUID Guid,
    _In_opt_ LPCDIEFFECT Params,
    _Outptr_ LPDIRECTINPUTEFFECT* Effect,
    _In_opt_ IUnknown* OuterUnknown)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Guid);
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(OuterUnknown);
    *Effect = nullptr;
    return DIERR_UNSUPPORTED;
}

LPDIRECTINPUTEFFECT
DiHoldEffect(
    _In_ PVOID Effect)
{
    return static_cast<LPDIRECTINPUTEFFECT>(Effect);
}

VOID
DiUnholdEffect(
    _In_ LPDIRECTINPUTEFFECT Effect)
{
    UNREFERENCED_PARAMETER(Effect);
}

VOID
DiReleaseCreatedEffects(
    _In_reads_opt_(Count) PVOID* Effects,
    _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Count);
}
