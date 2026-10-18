/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Numbered stand-ins for callback slots the runtime does not implement
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <array>
#include <utility>

/*
 * Every D3DDDI and DXGI runtime callback takes two pointer sized arguments and
 * returns an HRESULT, so one trap shape can sit in any of their slots on both
 * x64 and x86 (stdcall pops the same 8 bytes).
 */
using RxCallbackTrap = HRESULT (APIENTRY*)(PVOID, PVOID);

template <const PCWSTR* Name, SIZE_T Slot>
static
HRESULT
APIENTRY
RxTrapCallback(
    _In_opt_ PVOID Handle,
    _In_opt_ PVOID Arguments)
{
    RX_TRACE(L"UMD called unimplemented %s slot %Iu", *Name, Slot);
    return E_NOTIMPL;
}

template <const PCWSTR* Name, SIZE_T... Slots>
static
constexpr
std::array<RxCallbackTrap, sizeof...(Slots)>
RxMakeTraps(
    _In_ std::index_sequence<Slots...>)
{
    return { &RxTrapCallback<Name, Slots>... };
}

/* Fills each empty slot of a callback table with a trap that names its index */
template <const PCWSTR* Name, typename Callbacks>
static
VOID
RxFillMissingCallbacks(
    _Inout_ Callbacks& Table)
{
    constexpr SIZE_T Count = sizeof(Callbacks) / sizeof(RxCallbackTrap);
    static constexpr auto Traps = RxMakeTraps<Name>(std::make_index_sequence<Count>());
    RxCallbackTrap Slots[Count];
    SIZE_T Index;

    static_assert(sizeof(Callbacks) % sizeof(RxCallbackTrap) == 0);

    memcpy(Slots, &Table, sizeof(Slots));
    for (Index = 0; Index < Count; Index++)
    {
        if (Slots[Index] == nullptr)
            Slots[Index] = Traps[Index];
    }
    memcpy(&Table, Slots, sizeof(Slots));
}
