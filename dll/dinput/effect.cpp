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
    _Out_ RX_DI_EFFECT_ENTRY* Entry)
{
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Index);
    ZeroMemory(Entry, sizeof(*Entry));
    return FALSE;
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

HRESULT
DiEnumEffectsInFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_opt_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ LPVOID Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return DIERR_UNSUPPORTED;
}

HRESULT
DiWriteEffectToFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_ DWORD Count,
    _In_reads_opt_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Flags);
    return DIERR_UNSUPPORTED;
}
