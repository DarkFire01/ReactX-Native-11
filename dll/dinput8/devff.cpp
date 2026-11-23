/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback on the generic device: the effect driver, the effects it offers and the ones made
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <stdlib.h>

/* EnumEffects types outside these are rejected */
#define DI_EFFECT_TYPE_INVALID      0xFBFFFF00

/* SendForceFeedbackCommand takes exactly one of the low six bits */
#define DI_FF_COMMAND_MASK          0x0000003F

/* Effect kinds a driver entry adds to the device capabilities */
#define DI_FF_CAPS_FROM_EFFECTS     0x0000FE00
#define DI_FF_CAPS_KEPT             0x0000FF00

/* The device side of an effect */

HRESULT
DiDevice::CheckExclusive(VOID)
{
    if ((Cooperation & DISCL_EXCLUSIVE) == 0)
        return DIERR_NOTEXCLUSIVEACQUIRED;
    if (Flags & DI_DEV_ACQUIRED)
        return S_OK;
    return LostResult == DIERR_NOTACQUIRED ? DIERR_NOTEXCLUSIVEACQUIRED : LostResult;
}

BOOLEAN
DiDevice::FindEffect(
    _In_ REFGUID Guid,
    _Out_ RX_DI_EFFECT_ENTRY* Entry)
{
    BOOLEAN Found = FALSE;

    EnterCriticalSection(&Lock);
    for (ULONG Index = 0; Index < EffectCount && !Found; Index++)
    {
        if (IsEqualGUID(Guid, EffectList[Index].Guid))
        {
            *Entry = EffectList[Index];
            Found = TRUE;
        }
    }
    LeaveCriticalSection(&Lock);
    return Found;
}

HRESULT
DiDevice::ListEffect(
    _In_ PVOID Effect)
{
    PVOID* Grown = static_cast<PVOID*>(realloc(CreatedEffects, (CreatedEffectCount + 1) * sizeof(*Grown)));

    if (Grown == nullptr)
        return E_OUTOFMEMORY;
    CreatedEffects = Grown;
    CreatedEffects[CreatedEffectCount] = Effect;
    CreatedEffectCount += 1;
    return S_OK;
}

BOOLEAN
DiDevice::UnlistEffect(
    _In_ PVOID Effect)
{
    for (ULONG Index = 0; Index < CreatedEffectCount; Index++)
    {
        if (CreatedEffects[Index] != Effect)
            continue;
        CreatedEffectCount -= 1;
        CreatedEffects[Index] = CreatedEffects[CreatedEffectCount];
        return TRUE;
    }
    return FALSE;
}

/* Object ids or offsets in the application's format, checked against the wanted kind of object */
HRESULT
DiDevice::ConvertObjects(
    _In_ ULONG Count,
    _Inout_updates_(Count) PDWORD Values,
    _In_ DWORD How)
{
    const DI_FORMAT* Native;
    BOOLEAN FromOffset = (How & (DI_CONVERT_FROM_ID | DI_CONVERT_FROM_OFFSET)) == DI_CONVERT_FROM_OFFSET;
    BOOLEAN ToOffset = (How & (DI_CONVERT_TO_ID | DI_CONVERT_TO_OFFSET)) == DI_CONVERT_TO_OFFSET;
    HRESULT Result = S_OK;

    EnterCriticalSection(&Lock);
    if ((How & (DI_CONVERT_FROM_OFFSET | DI_CONVERT_TO_OFFSET)) != 0 && (AppObjects == nullptr || ObjectByOffset == nullptr))
    {
        LeaveCriticalSection(&Lock);
        return E_INVALIDARG;
    }

    Native = Backend->Format();
    for (ULONG Slot = 0; Slot < Count && SUCCEEDED(Result); Slot++)
    {
        LONG Object = -1;
        DWORD Type;

        if (FromOffset)
        {
            if (Values[Slot] < AppDataSize)
                Object = ObjectByOffset[Values[Slot]];
        }
        else
        {
            for (DWORD Remaining = Native->ObjectCount; Remaining > 0 && Object < 0; Remaining -= 1)
            {
                if (((Native->Objects[Remaining - 1].Type ^ Values[Slot]) & 0x00FFFFFF) == 0)
                    Object = static_cast<LONG>(Remaining - 1);
            }
        }

        if (Object < 0)
        {
            Result = E_INVALIDARG;
            break;
        }
        Type = Native->Objects[Object].Type;
        if ((Type & How & 0xFF) == 0 || (Type & How & 0xFF000000) != (How & 0xFF000000))
        {
            Result = E_INVALIDARG;
            break;
        }
        Values[Slot] = ToOffset ? static_cast<DWORD>(AppObjects[Object].Offset) : Type;
    }
    LeaveCriticalSection(&Lock);
    return Result;
}

/* Lets go of every effect still listed once the application released the device */
VOID
DiDevice::OnFinalRelease(VOID)
{
    PVOID* Effects;
    ULONG Count;

    if (Flags & DI_DEV_ACQUIRED)
        InternalUnacquire();

    EnterCriticalSection(&Lock);
    Effects = CreatedEffects;
    Count = CreatedEffectCount;
    CreatedEffects = nullptr;
    CreatedEffectCount = 0;
    LeaveCriticalSection(&Lock);

    DiReleaseCreatedEffects(Effects, Count);
    free(Effects);
}

/* The driver and what it offers */

HRESULT
DiDevice::LoadEffects(VOID)
{
    HKEY Key = nullptr;
    HKEY Effects = nullptr;
    DWORD Listed = 0;
    HRESULT Result;

    Result = Backend->GetFFConfigKey(KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &Key);
    if (Result == S_FALSE)
    {
        /* A PID device only describes its effects once its driver ran */
        Result = CreateEffectDriver();
        if (FAILED(Result))
            return Result;
        Result = Backend->GetFFConfigKey(KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &Key);
        if (Result == S_FALSE)
            return E_FAIL;
    }
    if (FAILED(Result))
        return Result;

    if (DiReadAttributes(Key, &FFAttributes) != S_OK)
        ZeroMemory(&FFAttributes, sizeof(FFAttributes));

    if (RegOpenKeyExW(Key, L"Effects", 0, KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &Effects) != ERROR_SUCCESS)
    {
        RegCloseKey(Key);
        return E_NOTIMPL;
    }

    if (RegQueryInfoKeyW(Effects, nullptr, nullptr, nullptr, &Listed, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr) != ERROR_SUCCESS)
    {
        Result = E_FAIL;
    }
    else
    {
        free(EffectList);
        EffectList = Listed != 0 ? static_cast<RX_DI_EFFECT_ENTRY*>(calloc(Listed, sizeof(*EffectList))) : nullptr;
        if (Listed != 0 && EffectList == nullptr)
        {
            Result = E_OUTOFMEMORY;
        }
        else
        {
            /* Entries without a direction system the driver understands are left out */
            EffectCount = 0;
            for (DWORD Index = 0; Index < Listed; Index++)
            {
                if (!DiReadEffectEntry(Effects, Index, &EffectList[EffectCount]))
                    continue;
                ForceFeedbackCaps |= EffectList[EffectCount].EffectType & DI_FF_CAPS_FROM_EFFECTS;
                EffectCount += 1;
            }
            ForceFeedbackCaps &= DI_FF_CAPS_KEPT;
            if (EffectCount != 0)
                ForceFeedbackCaps |= DIDC_FORCEFEEDBACK;
            Result = S_OK;
        }
    }
    RegCloseKey(Effects);
    RegCloseKey(Key);
    return Result;
}

HRESULT
DiDevice::CreateEffectDriver(VOID)
{
    HRESULT Result = S_OK;

    EnterCriticalSection(&Lock);
    if (EffectDriver == nullptr)
    {
        Result = Backend != nullptr ? Backend->CreateEffectDriver(&EffectDriver) : DIERR_NOTINITIALIZED;
        if (SUCCEEDED(Result) && (Flags & DI_DEV_ACQUIRED))
        {
            AcquireForceFeedback();
            Result = S_OK;
        }
    }
    LeaveCriticalSection(&Lock);
    return Result;
}

HRESULT
DiDevice::AcquireForceFeedback(VOID)
{
    HRESULT Result;

    if (EffectDriver == nullptr || (Cooperation & DISCL_EXCLUSIVE) == 0)
        return S_OK;
    Result = EffectDriver->SetForceFeedbackState(&DriverId, DI_FF_ACQUIRE);
    if (FAILED(Result))
        return Result;
    RefreshGain();
    if (AutoCenter == 0)
        EffectDriver->SetForceFeedbackState(&DriverId, DISFFC_STOPALL);
    return S_OK;
}

VOID
DiDevice::RefreshGain(VOID)
{
    if (EffectDriver != nullptr)
        EffectDriver->SetGain(&DriverId, Gain);
}

/* IDirectInputDevice8 */

STDMETHODIMP
DiDevice::CreateEffect(
    _In_ REFGUID Effect,
    _In_opt_ LPCDIEFFECT Params,
    _Out_ LPDIRECTINPUTEFFECT* Created,
    _In_opt_ LPUNKNOWN Outer)
{
    HRESULT Result;

    if (Created == nullptr)
        return E_POINTER;
    Result = CreateEffectDriver();
    *Created = nullptr;
    if (FAILED(Result))
        return Result;
    return DiCreateEffect(this, Effect, Params, Created, Outer);
}

STDMETHODIMP
DiDevice::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    RX_DI_EFFECT_ENTRY* Snapshot = nullptr;
    ULONG Count;

    if (Callback == nullptr || (Type & DI_EFFECT_TYPE_INVALID) != 0)
        return E_INVALIDARG;

    /* The callback runs on a copy, so it may create effects on this device */
    EnterCriticalSection(&Lock);
    Count = EffectCount;
    if (Count != 0)
    {
        Snapshot = static_cast<RX_DI_EFFECT_ENTRY*>(malloc(Count * sizeof(*Snapshot)));
        if (Snapshot == nullptr)
        {
            LeaveCriticalSection(&Lock);
            return E_OUTOFMEMORY;
        }
        CopyMemory(Snapshot, EffectList, Count * sizeof(*Snapshot));
    }
    LeaveCriticalSection(&Lock);

    for (ULONG Index = 0; Index < Count; Index++)
    {
        const RX_DI_EFFECT_ENTRY* Entry = &Snapshot[Index];
        DIEFFECTINFOW Info;

        if (Type != 0 && Type != DIEFT_GETTYPE(Entry->EffectType))
            continue;
        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        Info.guid = Entry->Guid;
        Info.dwEffType = Entry->EffectType;
        Info.dwStaticParams = Entry->StaticParams;
        Info.dwDynamicParams = Entry->DynamicParams;
        CopyMemory(Info.tszName, Entry->Name, sizeof(Info.tszName));
        if (!Callback(&Info, Context))
            break;
    }
    free(Snapshot);
    return DI_OK;
}

static
VOID
DiEffectInfoToAnsi(
    _Inout_ LPDIEFFECTINFOA Ansi,
    _In_ LPCDIEFFECTINFOW Wide)
{
    Ansi->guid = Wide->guid;
    Ansi->dwEffType = Wide->dwEffType;
    Ansi->dwStaticParams = Wide->dwStaticParams;
    Ansi->dwDynamicParams = Wide->dwDynamicParams;
    WideCharToMultiByte(CP_ACP, 0, Wide->tszName, -1, Ansi->tszName, MAX_PATH, nullptr, nullptr);
}

struct DI_ANSI_EFFECTS
{
    LPDIENUMEFFECTSCALLBACKA Callback;
    VOID* Context;
};

static
BOOL
CALLBACK
DiAnsiEffectThunk(
    _In_ LPCDIEFFECTINFOW Wide,
    _In_ VOID* Context)
{
    DI_ANSI_EFFECTS* Enum = static_cast<DI_ANSI_EFFECTS*>(Context);
    DIEFFECTINFOA Ansi;

    ZeroMemory(&Ansi, sizeof(Ansi));
    Ansi.dwSize = sizeof(Ansi);
    DiEffectInfoToAnsi(&Ansi, Wide);
    return Enum->Callback(&Ansi, Enum->Context);
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    DI_ANSI_EFFECTS Enum;

    if (Callback == nullptr)
        return E_INVALIDARG;
    Enum.Callback = Callback;
    Enum.Context = Context;
    return Owner->EnumEffects(DiAnsiEffectThunk, &Enum, Type);
}

STDMETHODIMP
DiDevice::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOW Info,
    _In_ REFGUID Effect)
{
    RX_DI_EFFECT_ENTRY Entry;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(*Info))
        return E_INVALIDARG;
    ZeroMemory(&Entry, sizeof(Entry));
    if (!FindEffect(Effect, &Entry))
        return DIERR_DEVICENOTREG;
    Info->guid = Entry.Guid;
    Info->dwEffType = Entry.EffectType;
    Info->dwStaticParams = Entry.StaticParams;
    Info->dwDynamicParams = Entry.DynamicParams;
    CopyMemory(Info->tszName, Entry.Name, sizeof(Info->tszName));
    return DI_OK;
}

STDMETHODIMP
DiDevice::AnsiFace::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOA Info,
    _In_ REFGUID Effect)
{
    DIEFFECTINFOW Wide;
    HRESULT Result;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(*Info))
        return E_INVALIDARG;
    ZeroMemory(&Wide, sizeof(Wide));
    Wide.dwSize = sizeof(Wide);
    Result = Owner->GetEffectInfo(&Wide, Effect);
    if (FAILED(Result))
        return Result;
    DiEffectInfoToAnsi(Info, &Wide);
    return DI_OK;
}

STDMETHODIMP
DiDevice::GetForceFeedbackState(
    _Out_ LPDWORD State)
{
    DIDEVICESTATE Status;
    HRESULT Result;

    if (State == nullptr)
        return E_POINTER;
    *State = 0;
    Result = CreateEffectDriver();
    if (FAILED(Result))
        return Result;

    EnterCriticalSection(&Lock);
    ZeroMemory(&Status, sizeof(Status));
    Status.dwSize = sizeof(Status);
    Result = EffectDriver->GetForceFeedbackState(&DriverId, &Status);
    if (SUCCEEDED(Result))
    {
        *State = Status.dwState;
        Result = DI_OK;
    }
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::SendForceFeedbackCommand(
    _In_ DWORD Command)
{
    HRESULT Result;

    if (Command == 0 || (Command & ~DI_FF_COMMAND_MASK) != 0 || (Command & (Command - 1)) != 0)
        return E_INVALIDARG;
    Result = CreateEffectDriver();
    if (FAILED(Result))
        return Result;

    EnterCriticalSection(&Lock);
    Result = EffectDriver->SetForceFeedbackState(&DriverId, Command);
    if (SUCCEEDED(Result) && (Command & DISFFC_RESET))
        RefreshGain();
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::EnumCreatedEffectObjects(
    _In_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD EnumFlags)
{
    LPDIRECTINPUTEFFECT* Held = nullptr;
    ULONG Count = 0;

    if (Callback == nullptr || EnumFlags != 0)
        return E_INVALIDARG;

    /* Each effect is held while the callback sees it, so releasing it there is safe */
    EnterCriticalSection(&Lock);
    if (CreatedEffectCount != 0)
    {
        Held = static_cast<LPDIRECTINPUTEFFECT*>(malloc(CreatedEffectCount * sizeof(*Held)));
        if (Held != nullptr)
        {
            Count = CreatedEffectCount;
            for (ULONG Index = 0; Index < Count; Index++)
                Held[Index] = DiHoldEffect(CreatedEffects[Index]);
        }
    }
    LeaveCriticalSection(&Lock);

    for (ULONG Index = 0; Index < Count; Index++)
    {
        if (!Callback(Held[Index], Context))
            break;
    }
    for (ULONG Index = 0; Index < Count; Index++)
        DiUnholdEffect(Held[Index]);
    free(Held);
    return DI_OK;
}

STDMETHODIMP
DiDevice::Escape(
    _Inout_ LPDIEFFESCAPE Request)
{
    HRESULT Result;

    if (Request == nullptr)
        return E_POINTER;
    if (Request->dwSize != sizeof(*Request))
        return E_INVALIDARG;
    if ((Request->lpvInBuffer == nullptr && Request->cbInBuffer != 0) ||
        (Request->lpvOutBuffer == nullptr && Request->cbOutBuffer != 0))
    {
        return E_POINTER;
    }
    Result = CreateEffectDriver();
    if (FAILED(Result))
        return Result;

    EnterCriticalSection(&Lock);
    Result = EffectDriver->DeviceEscape(&DriverId, Request);
    LeaveCriticalSection(&Lock);
    return Result;
}
