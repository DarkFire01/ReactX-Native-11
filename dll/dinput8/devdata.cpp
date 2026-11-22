/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Data formats, cooperative levels, acquisition, device state and buffered data
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "shared.hpp"

#include <stdlib.h>

/* Internal property that turns device objects on as a data format selects them */
#define DI_PROP_ENABLE_OBJECT       0xFFFB

/* GetDeviceData reads what is left in the buffer even when the device is not acquired */
#define DI_GDD_RESIDUAL             0x00000002

#define DI_COOPERATION_VALID        (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_BACKGROUND | DISCL_NOWINKEY)

/* Devices acquired for a foreground window, dropped when that window loses activation */
static DiDevice** DiForegroundDevices;
static UINT DiForegroundCount;
static UINT DiForegroundCapacity;
static HHOOK DiActivationHook;
static BOOLEAN DiFocusLost;
static BOOLEAN DiMinimizing;

static
BOOLEAN
DiWindowIsOurs(
    _In_ HWND Window)
{
    DWORD Process = 0;

    GetWindowThreadProcessId(Window, &Process);
    return Process == GetCurrentProcessId();
}

static
VOID
DiDropForegroundHook(VOID)
{
    EnterCriticalSection(&DiLock);
    if (DiActivationHook != nullptr)
        UnhookWindowsHookEx(DiActivationHook);
    DiActivationHook = nullptr;
    LeaveCriticalSection(&DiLock);
}

static
LRESULT
CALLBACK
DiActivationProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    const CWPSTRUCT* Message = reinterpret_cast<const CWPSTRUCT*>(LParam);
    BOOLEAN Minimized = FALSE;
    LRESULT Result;

    switch (Message->message)
    {
        case WM_KILLFOCUS:
            DiFocusLost = TRUE;
            break;
        case WM_SIZE:
            if (Message->wParam == SIZE_MINIMIZED && DiFocusLost)
                Minimized = TRUE;
            DiFocusLost = FALSE;
            break;
        case WM_SETFOCUS:
            DiFocusLost = FALSE;
            break;
        default:
            break;
    }
    Result = CallNextHookEx(DiActivationHook, Code, WParam, LParam);
    if (Code == HC_ACTION && (Message->message == WM_ACTIVATE || Minimized))
    {
        DiDevice** Devices;
        UINT Count;

        EnterCriticalSection(&DiLock);
        Devices = DiForegroundDevices;
        Count = DiForegroundCount;
        DiForegroundDevices = nullptr;
        DiForegroundCount = 0;
        DiForegroundCapacity = 0;
        LeaveCriticalSection(&DiLock);
        for (UINT Index = 0; Index < Count; Index++)
        {
            Devices[Index]->InternalUnacquire();
            Devices[Index]->Release();
        }
        free(Devices);
        DiDropForegroundHook();
    }
    return Result;
}

static
HRESULT
DiAddForegroundDevice(
    _In_ DiDevice* Device)
{
    HRESULT Result = S_OK;

    EnterCriticalSection(&DiLock);
    if (DiForegroundCount == DiForegroundCapacity)
    {
        UINT Capacity = DiForegroundCapacity == 0 ? 8 : DiForegroundCapacity * 2;
        DiDevice** Grown = static_cast<DiDevice**>(realloc(DiForegroundDevices, Capacity * sizeof(*Grown)));

        if (Grown == nullptr)
        {
            LeaveCriticalSection(&DiLock);
            return E_OUTOFMEMORY;
        }
        DiForegroundDevices = Grown;
        DiForegroundCapacity = Capacity;
    }
    DiForegroundDevices[DiForegroundCount++] = Device;
    Device->AddRef();
    if (DiActivationHook == nullptr)
    {
        DiActivationHook = SetWindowsHookExW(WH_CALLWNDPROC, DiActivationProc, DiInstance,
                                             GetWindowThreadProcessId(Device->Window, nullptr));
        if (DiActivationHook == nullptr)
            DiTrace(L"cannot watch window activation (%lu)", GetLastError());
    }
    LeaveCriticalSection(&DiLock);
    return Result;
}

static
VOID
DiRemoveForegroundDevice(
    _In_ DiDevice* Device)
{
    BOOLEAN Found = FALSE;

    EnterCriticalSection(&DiLock);
    for (UINT Index = 0; Index < DiForegroundCount; Index++)
    {
        if (DiForegroundDevices[Index] == Device)
        {
            DiForegroundDevices[Index] = DiForegroundDevices[--DiForegroundCount];
            Found = TRUE;
            break;
        }
    }
    LeaveCriticalSection(&DiLock);
    if (Found)
        Device->Release();
}

/* Exclusive ownership is recorded in the section every process shares */

static
DI_EXCLUSIVE_OWNER*
DiFindOwner(
    _In_ REFGUID Device)
{
    for (DWORD Index = 0; Index < DiShared->OwnerCount && Index < DI_EXCLUSIVE_MAX; Index++)
    {
        if (IsEqualGUID(DiShared->Owners[Index].Device, Device))
            return &DiShared->Owners[Index];
    }
    return nullptr;
}

static
HRESULT
DiTakeExclusive(
    _In_ REFGUID Device,
    _In_opt_ HWND Window,
    _In_ DWORD Cooperation)
{
    DI_EXCLUSIVE_OWNER* Owner;
    HRESULT Result = S_OK;

    if (!(Cooperation & DISCL_EXCLUSIVE))
        return S_OK;
    if (DiShared == nullptr || DiGlobalMutex == nullptr)
        return E_FAIL;
    if (!DiWindowIsOurs(Window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

    WaitForSingleObject(DiGlobalMutex, INFINITE);
    Owner = DiFindOwner(Device);
    if (Owner == nullptr)
    {
        if (DiShared->OwnerCount >= DI_EXCLUSIVE_MAX)
        {
            Result = E_FAIL;
            goto Exit;
        }
        Owner = &DiShared->Owners[DiShared->OwnerCount++];
        Owner->Device = Device;
    }
    else
    {
        DWORD Process = 0;

        /* A live owner holding the device for its foreground window keeps it while that window is not in front */
        GetWindowThreadProcessId(Owner->Window, &Process);
        if (Process == Owner->ProcessId && (Owner->CooperativeLevel & DISCL_FOREGROUND) &&
            GetForegroundWindow() != Owner->Window)
        {
            Result = DIERR_OTHERAPPHASPRIO;
            goto Exit;
        }
    }
    Owner->Window = Window;
    Owner->ProcessId = GetCurrentProcessId();
    Owner->CooperativeLevel = Cooperation;

Exit:
    ReleaseMutex(DiGlobalMutex);
    return Result;
}

static
VOID
DiDropExclusive(
    _In_ REFGUID Device,
    _In_opt_ HWND Window,
    _In_ DWORD Cooperation)
{
    DI_EXCLUSIVE_OWNER* Owner;

    if (!(Cooperation & DISCL_EXCLUSIVE) || DiShared == nullptr || DiGlobalMutex == nullptr)
        return;
    WaitForSingleObject(DiGlobalMutex, INFINITE);
    Owner = DiFindOwner(Device);
    if (Owner != nullptr && Owner->Window == Window && Owner->ProcessId == GetCurrentProcessId())
        *Owner = DiShared->Owners[--DiShared->OwnerCount];
    ReleaseMutex(DiGlobalMutex);
}

/* Data formats */

HRESULT
DiDevice::ParseFormat(
    _In_ LPCDIDATAFORMAT Format)
{
    const DI_FORMAT* Native = Backend->Format();
    DI_APP_OBJECT* Objects;
    LONG* ByOffset;
    LONG* ByNative;
    DWORD* Povs;
    DWORD PovCount = 0;
    HRESULT Result = S_OK;

    Objects = static_cast<DI_APP_OBJECT*>(malloc((Native->ObjectCount + 1) * sizeof(*Objects)));
    ByOffset = static_cast<LONG*>(malloc((Format->dwDataSize + 1) * sizeof(*ByOffset)));
    ByNative = static_cast<LONG*>(malloc((Native->DataSize + 1) * sizeof(*ByNative)));
    Povs = static_cast<DWORD*>(malloc((Format->dwNumObjs + 1) * sizeof(*Povs)));
    if (Objects == nullptr || ByOffset == nullptr || ByNative == nullptr || Povs == nullptr)
    {
        Result = E_OUTOFMEMORY;
        goto Exit;
    }
    for (DWORD Index = 0; Index <= Native->ObjectCount; Index++)
    {
        Objects[Index].Offset = -1;
        Objects[Index].AppData = static_cast<UINT_PTR>(-1);
    }
    memset(ByOffset, 0xFF, (Format->dwDataSize + 1) * sizeof(*ByOffset));
    memset(ByNative, 0xFF, (Native->DataSize + 1) * sizeof(*ByNative));

    for (DWORD Wanted = 0; Wanted < Format->dwNumObjs; Wanted++)
    {
        const DIOBJECTDATAFORMAT* App = &Format->rgodf[Wanted];
        DWORD Match = static_cast<DWORD>(-1);

        if (App->dwOfs >= Format->dwDataSize)
        {
            Result = E_INVALIDARG;
            goto Exit;
        }
        for (DWORD Index = 0; Index < Native->ObjectCount; Index++)
        {
            const DI_OBJECT* Object = &Native->Objects[Index];

            if ((Object->Type & App->dwType & 0xFF) == 0 ||
                (Object->Type & App->dwType & 0x1F000000) != (App->dwType & 0x1F000000) || Objects[Index].Offset != -1)
            {
                continue;
            }
            if (App->pguid != nullptr)
            {
                if (App->dwFlags & DIDOI_GUIDISUSAGE)
                {
                    if (static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(App->pguid)) != Backend->GetUsage(Index))
                        continue;
                }
                else if (!IsEqualGUID(*App->pguid, GUID_NULL) && !IsEqualGUID(*App->pguid, *Object->Guid))
                {
                    continue;
                }
            }
            if ((App->dwType & DIDFT_INSTANCEMASK) != DIDFT_ANYINSTANCE &&
                ((Object->Type ^ App->dwType) & DIDFT_INSTANCEMASK) != 0)
            {
                continue;
            }
            if ((App->dwFlags & DIDOI_ASPECTMASK) != 0 && (Object->Flags & DIDOI_ASPECTMASK) != 0 &&
                ((App->dwFlags ^ Object->Flags) & DIDOI_ASPECTMASK) != 0)
            {
                continue;
            }
            Match = Index;
            break;
        }

        if (Match == static_cast<DWORD>(-1))
        {
            if (!(App->dwType & DI_OBJECT_OPTIONAL))
            {
                Result = E_INVALIDARG;
                goto Exit;
            }
            if (App->dwType & DIDFT_POV)
                Povs[PovCount++] = App->dwOfs;
            continue;
        }
        if ((Native->Objects[Match].Type & (DIDFT_AXIS | DIDFT_POV)) && (App->dwOfs & 3) != 0)
        {
            Result = E_INVALIDARG;
            goto Exit;
        }
        Objects[Match].Offset = static_cast<LONG>(App->dwOfs);
        ByOffset[App->dwOfs] = static_cast<LONG>(Match);
        ByNative[Native->Objects[Match].Offset] = static_cast<LONG>(Match);
        if (Native->Objects[Match].Flags & DIDOI_POLLED)
            Flags |= DI_DEV_POLLED_FORMAT;
        {
            DI_PROPERTY Enable = { DI_PROP_ENABLE_OBJECT, Match, Native->Objects[Match].Type };
            DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), Native->Objects[Match].Type, DIPH_BYID }, 1 };

            Backend->SetProperty(&Enable, &Value.diph);
        }
    }

    if (Emulation != nullptr)
    {
        Result = DiEmSetObjectMap(Emulation, ByNative, Native->DataSize);
        if (FAILED(Result))
            goto Exit;
    }
    AppObjects = Objects;
    ObjectByOffset = ByOffset;
    MissingPovs = Povs;
    MissingPovCount = PovCount;
    AppDataSize = Format->dwDataSize;
    Objects = nullptr;
    ByOffset = nullptr;
    Povs = nullptr;

Exit:
    free(Objects);
    free(ByOffset);
    free(ByNative);
    free(Povs);
    return Result;
}

/* A format whose objects all sit at one shift from the native layout can be copied in one piece */
VOID
DiDevice::PlanStateCopy(VOID)
{
    const DI_FORMAT* Native = Backend->Format();
    BOOLEAN Uniform = TRUE;
    BOOLEAN Any = FALSE;
    LONG Shift = 0;
    DWORD First = static_cast<DWORD>(-1);
    DWORD End = 0;

    for (DWORD Index = Native->ObjectCount; Index-- > 0;)
    {
        const DI_OBJECT* Object = &Native->Objects[Index];
        LONG Delta;

        if (AppObjects[Index].Offset == -1)
            continue;
        Delta = AppObjects[Index].Offset - static_cast<LONG>(Object->Offset);
        if (Any && Delta != Shift)
        {
            Uniform = FALSE;
            break;
        }
        Any = TRUE;
        Shift = Delta;
        if (Object->Offset < First)
            First = Object->Offset;
        if (Object->Offset + ((Object->Type & (DIDFT_AXIS | DIDFT_POV)) ? 4 : 1) > End)
            End = Object->Offset + ((Object->Type & (DIDFT_AXIS | DIDFT_POV)) ? 4 : 1);
    }

    if (!Any || !Uniform)
    {
        CopyMode = DiCopyPerObject;
        return;
    }
    CopyShift = Shift;
    CopyFirst = First;
    CopyLength = End - First;
    if (Shift < 0 || Native->DataSize + Shift > AppDataSize)
        CopyMode = DiCopyRange;
    else if (Shift == 0 && AppDataSize == Native->DataSize)
        CopyMode = DiCopyWhole;
    else
        CopyMode = DiCopyShifted;
}

HRESULT
DiDevice::SetAxisMode(
    _In_ DWORD Mode)
{
    if (Mode & ~DIPROPAXISMODE_REL)
        return E_INVALIDARG;
    Relative = Mode == DIPROPAXISMODE_REL;
    if (Emulation != nullptr)
    {
        if (Relative)
            Emulation->Flags |= DI_EM_RELATIVE;
        else
            Emulation->Flags &= ~DI_EM_RELATIVE;
    }
    return AxisCount == 0 ? DI_PROPNOEFFECT : DI_OK;
}

STDMETHODIMP
DiDevice::SetDataFormat(
    _In_ LPCDIDATAFORMAT Format)
{
    HRESULT Result;

    if (Format == nullptr)
        return E_POINTER;
    if (Format->dwSize != sizeof(DIDATAFORMAT) || (Format->dwFlags & ~(DIDF_ABSAXIS | DIDF_RELAXIS)) != 0 ||
        (Format->dwDataSize & 3) != 0 || Format->dwObjSize != sizeof(DIOBJECTDATAFORMAT))
    {
        DI_TRACE_ONCE(L"SetDataFormat rejected a malformed format");
        return E_INVALIDARG;
    }
    if (Format->rgodf == nullptr && Format->dwNumObjs != 0)
        return E_POINTER;

    EnterCriticalSection(&Lock);
    if (Flags & DI_DEV_ACQUIRED)
    {
        Result = DIERR_ACQUIRED;
        goto Exit;
    }
    ClearFormat();
    Flags &= ~DI_DEV_POLLED_FORMAT;
    if (Backend == nullptr)
    {
        Result = DIERR_NOTINITIALIZED;
        goto Exit;
    }
    {
        DI_PROPERTY Disable = { DI_PROP_ENABLE_OBJECT, static_cast<DWORD>(-1), 0 };
        DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 0 };

        Result = Backend->SetProperty(&Disable, &Value.diph);
        if (FAILED(Result) && Result != E_NOTIMPL)
            goto Exit;
    }
    Result = ParseFormat(Format);
    if (FAILED(Result))
        goto Exit;
    PlanStateCopy();
    switch (Format->dwFlags)
    {
        case 0:
            Result = DI_OK;
            break;
        case DIDF_ABSAXIS:
            Result = SetAxisMode(DIPROPAXISMODE_ABS);
            Result = FAILED(Result) ? Result : DI_OK;
            break;
        case DIDF_RELAXIS:
            Result = SetAxisMode(DIPROPAXISMODE_REL);
            Result = FAILED(Result) ? Result : DI_OK;
            break;
        default:
            Result = E_INVALIDARG;
            break;
    }

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

/* Cooperation */

STDMETHODIMP
DiDevice::SetCooperativeLevel(
    _In_opt_ HWND NewWindow,
    _In_ DWORD NewFlags)
{
    HRESULT Result;

    EnterCriticalSection(&Lock);
    if (Flags & DI_DEV_ACQUIRED)
    {
        Result = DIERR_ACQUIRED;
        goto Exit;
    }
    if ((NewFlags & ~DI_COOPERATION_VALID) != 0 ||
        ((NewFlags & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE)) != DISCL_EXCLUSIVE &&
         (NewFlags & (DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE)) != DISCL_NONEXCLUSIVE) ||
        ((NewFlags & (DISCL_FOREGROUND | DISCL_BACKGROUND)) != DISCL_FOREGROUND &&
         (NewFlags & (DISCL_FOREGROUND | DISCL_BACKGROUND)) != DISCL_BACKGROUND))
    {
        DI_TRACE_ONCE(L"SetCooperativeLevel rejected flags %#lx", NewFlags);
        Result = E_INVALIDARG;
        goto Exit;
    }
    if (NewWindow != nullptr && (!IsWindow(NewWindow) || (GetWindowLongW(NewWindow, GWL_STYLE) & WS_CHILD)))
    {
        Result = E_HANDLE;
        goto Exit;
    }
    if ((NewFlags & (DISCL_EXCLUSIVE | DISCL_FOREGROUND)) != 0 && NewWindow == nullptr)
    {
        Result = E_HANDLE;
        goto Exit;
    }
    if (Backend == nullptr)
    {
        Result = DIERR_NOTINITIALIZED;
        goto Exit;
    }
    Result = Backend->SetCooperativeLevel(NewWindow, NewFlags);
    if (SUCCEEDED(Result))
    {
        Cooperation = NewFlags;
        Window = NewWindow;
        if (Emulation != nullptr)
            Emulation->Window = NewWindow;
    }

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::SetEventNotification(
    _In_opt_ HANDLE NewEvent)
{
    HANDLE Copy = nullptr;
    HRESULT Result;

    EnterCriticalSection(&Lock);
    if (Flags & DI_DEV_ACQUIRED)
    {
        Result = DIERR_ACQUIRED;
        goto Exit;
    }
    if (NewEvent != nullptr)
    {
        if (!DuplicateHandle(GetCurrentProcess(), NewEvent, GetCurrentProcess(), &Copy, EVENT_MODIFY_STATE, FALSE, 0))
        {
            Result = HRESULT_FROM_WIN32(GetLastError());
            goto Exit;
        }
        if (!ResetEvent(Copy))
        {
            CloseHandle(Copy);
            Result = E_HANDLE;
            goto Exit;
        }
    }
    if (Event != nullptr && Copy != nullptr)
    {
        CloseHandle(Copy);
        Result = DIERR_HANDLEEXISTS;
        goto Exit;
    }
    if (Backend == nullptr)
    {
        if (Copy != nullptr)
            CloseHandle(Copy);
        Result = DIERR_NOTINITIALIZED;
        goto Exit;
    }
    if (Emulation != nullptr)
    {
        HANDLE Old = Event;

        Event = Copy;
        DiEmSetEvent(Emulation, Copy);
        if (Old != nullptr)
            CloseHandle(Old);
        Result = PolledResult;
    }
    else
    {
        if (Copy != nullptr)
            CloseHandle(Copy);
        Result = E_NOTIMPL;
    }

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

/* Acquisition */

static
BOOLEAN
DiCanAcquireForeground(
    _In_opt_ HWND Window)
{
    return Window != nullptr && GetForegroundWindow() == Window && !IsIconic(Window);
}

HRESULT
DiDevice::ReleaseEmulation(VOID)
{
    HRESULT Result = Backend->Unacquire();

    if (Result != S_FALSE)
        return Result;
    if (!(Flags & DI_DEV_EM_ACQUIRED))
        return DI_OK;
    Flags &= ~DI_DEV_EM_ACQUIRED;
    if (Emulation == nullptr)
        return DIERR_GENERIC;
    return DiEmUnacquire(Emulation);
}

STDMETHODIMP
DiDevice::Acquire()
{
    HRESULT Result;

    EnterCriticalSection(&Lock);
    LostResult = DIERR_NOTACQUIRED;
    if (AppObjects == nullptr || Emulation == nullptr)
    {
        Result = DIERR_INVALIDPARAM;
        goto Exit;
    }
    if (Emulation->Flags & DI_EM_ACQUIRED)
    {
        Result = S_FALSE;
        goto Exit;
    }
    if (Cooperation & DISCL_FOREGROUND)
    {
        if (GetForegroundWindow() != Window || IsIconic(Window))
        {
            Result = DIERR_OTHERAPPHASPRIO;
            goto Exit;
        }
        if (!DiWindowIsOurs(Window))
        {
            Window = reinterpret_cast<HWND>(static_cast<LONG_PTR>(-1));
            Result = E_INVALIDARG;
            goto Exit;
        }
    }
    Result = DiTakeExclusive(Instance, Window, Cooperation);
    if (FAILED(Result))
        goto Exit;

    Result = Backend->Acquire();
    if (FAILED(Result))
        goto Undo;
    if (Result == S_FALSE)
    {
        Result = DiEmAcquire(Emulation);
        if (FAILED(Result))
            goto Undo;
        Flags |= DI_DEV_EM_ACQUIRED;
        if (Relative)
        {
            Result = Backend->GetDeviceState(Baseline);
            if (FAILED(Result))
                goto Undo;
        }
    }
    if (Cooperation & DISCL_FOREGROUND)
    {
        if (!DiCanAcquireForeground(Window))
        {
            Result = DIERR_OTHERAPPHASPRIO;
            goto Undo;
        }
        Flags |= DI_DEV_ACQUIRED;
        Result = DiAddForegroundDevice(this);
        if (FAILED(Result) || !DiCanAcquireForeground(Window))
        {
            LeaveCriticalSection(&Lock);
            InternalUnacquire();
            return FAILED(Result) ? Result : DIERR_OTHERAPPHASPRIO;
        }
    }
    Flags |= DI_DEV_ACQUIRED | DI_DEV_WAS_ACQUIRED;
    LostResult = DIERR_INPUTLOST;
    Result = DI_OK;
    goto Exit;

Undo:
    ReleaseEmulation();
    DiDropExclusive(Instance, Window, Cooperation);
Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

HRESULT
DiDevice::InternalUnacquire(VOID)
{
    HRESULT Result;
    BOOLEAN Foreground;

    EnterCriticalSection(&Lock);
    if (!(Flags & DI_DEV_ACQUIRED))
    {
        LeaveCriticalSection(&Lock);
        return DI_NOEFFECT;
    }
    Flags &= ~DI_DEV_ACQUIRED;
    DiDropExclusive(Instance, Window, Cooperation);
    Foreground = (Cooperation & DISCL_FOREGROUND) != 0;
    if (Foreground && IsIconic(Window))
        Flags |= DI_DEV_ICONIC;
    Result = ReleaseEmulation();
    if (LostResult == DIERR_INPUTLOST && Event != nullptr)
        SetEvent(Event);
    LeaveCriticalSection(&Lock);
    if (Foreground)
        DiRemoveForegroundDevice(this);
    return Result;
}

STDMETHODIMP
DiDevice::Unacquire()
{
    LostResult = DIERR_NOTACQUIRED;
    return InternalUnacquire();
}

/* A foreground device dropped by minimizing comes back once its window is restored */
VOID
DiDevice::RestoreIfNeeded(VOID)
{
    if (DiAppHacks.ReAcquire && !(Flags & DI_DEV_ACQUIRED) && (Flags & (DI_DEV_WAS_ACQUIRED | DI_DEV_FORCED_OFF)))
        Acquire();
    if ((Flags & DI_DEV_ICONIC) && !IsIconic(Window) && SUCCEEDED(Acquire()))
        Flags &= ~DI_DEV_ICONIC;
}

STDMETHODIMP
DiDevice::GetDeviceState(
    _In_ DWORD Size,
    _Out_writes_bytes_(Size) VOID* Data)
{
    BYTE* Target = static_cast<BYTE*>(Data);
    BYTE* Native;
    HRESULT Result;

    EnterCriticalSection(&Lock);
    RestoreIfNeeded();
    if (!(Flags & DI_DEV_ACQUIRED))
    {
        Result = LostResult;
        goto Exit;
    }
    if (Size != AppDataSize)
    {
        Result = E_INVALIDARG;
        goto Exit;
    }

    if (CopyMode == DiCopyWhole || CopyMode == DiCopyShifted)
    {
        ZeroMemory(Target, Size);
        Native = Target + CopyShift;
    }
    else
    {
        Native = NativeState;
    }
    Result = Backend->GetDeviceState(Native);
    if (FAILED(Result))
    {
        if (Result == DIERR_INPUTLOST)
        {
            LeaveCriticalSection(&Lock);
            InternalUnacquire();
            return DIERR_INPUTLOST;
        }
        goto Exit;
    }
    if (Relative)
    {
        for (DWORD Index = 0; Index < AxisCount; Index++)
        {
            LONG* Value = reinterpret_cast<LONG*>(Native + AxisOffsets[Index]);
            LONG* Last = reinterpret_cast<LONG*>(Baseline + AxisOffsets[Index]);
            LONG Current = *Value;

            *Value = Current - *Last;
            *Last = Current;
        }
    }

    if (CopyMode == DiCopyRange)
    {
        ZeroMemory(Target, Size);
        CopyMemory(Target + CopyFirst + CopyShift, Native + CopyFirst, CopyLength);
    }
    else if (CopyMode == DiCopyPerObject)
    {
        const DI_FORMAT* Format = Backend->Format();

        ZeroMemory(Target, Size);
        for (DWORD Index = Format->ObjectCount; Index-- > 0;)
        {
            const DI_OBJECT* Object = &Format->Objects[Index];

            if (AppObjects[Index].Offset == -1)
                continue;
            if (Object->Type & (DIDFT_AXIS | DIDFT_POV))
                *reinterpret_cast<LONG*>(Target + AppObjects[Index].Offset) = *reinterpret_cast<LONG*>(Native + Object->Offset);
            else
                Target[AppObjects[Index].Offset] = Native[Object->Offset];
        }
    }
    for (DWORD Index = 0; Index < MissingPovCount; Index++)
        *reinterpret_cast<DWORD*>(Target + MissingPovs[Index]) = static_cast<DWORD>(-1);
    Result = DI_OK;

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::GetDeviceData(
    _In_ DWORD ObjectSize,
    _Out_writes_opt_(*Count) LPDIDEVICEOBJECTDATA Data,
    _Inout_ LPDWORD Count,
    _In_ DWORD ReadFlags)
{
    HRESULT Result;

    if (ObjectSize != sizeof(DIDEVICEOBJECTDATA) || (ReadFlags & ~(DIGDD_PEEK | DI_GDD_RESIDUAL)) != 0)
        return E_INVALIDARG;
    if (Count == nullptr)
        return E_POINTER;

    EnterCriticalSection(&Lock);
    if (BufferSize == 0)
    {
        Result = DIERR_NOTBUFFERED;
        goto Exit;
    }
    if (Emulation == nullptr)
    {
        Result = E_NOTIMPL;
        goto Exit;
    }
    RestoreIfNeeded();

    if (((Flags & DI_DEV_ACQUIRED) && (Emulation->Flags & DI_EM_ACQUIRED)) || (ReadFlags & DI_GDD_RESIDUAL))
    {
        DWORD Limit = *Count < BufferSize ? *Count : BufferSize;
        DWORD Read = 0;
        DWORD Position;

        EnterCriticalSection(&Emulation->Lock);
        Position = Emulation->Tail;
        while (Read < Limit && Position != Emulation->Head && Emulation->Buffer != nullptr)
        {
            const DI_EM_EVENT* Entry = &Emulation->Buffer[Position];

            if (Data != nullptr)
            {
                Data[Read].dwOfs = AppObjects != nullptr ? static_cast<DWORD>(AppObjects[Entry->Object].Offset) : Entry->Object;
                Data[Read].dwData = Entry->Data;
                Data[Read].dwTimeStamp = Entry->TimeStamp;
                Data[Read].dwSequence = Entry->Sequence;
                Data[Read].uAppData = AppObjects != nullptr ? AppObjects[Entry->Object].AppData : static_cast<UINT_PTR>(-1);
            }
            Position = (Position + 1) % (Emulation->Capacity + 1);
            Read++;
        }
        if (!(ReadFlags & DIGDD_PEEK))
            Emulation->Tail = Position;
        Result = Emulation->Overflow ? DI_BUFFEROVERFLOW : DI_OK;
        Emulation->Overflow = FALSE;
        LeaveCriticalSection(&Emulation->Lock);
        *Count = Read;
    }
    else if (!(Flags & DI_DEV_ACQUIRED) || (Emulation->Flags & DI_EM_ACQUIRED))
    {
        Result = LostResult;
    }
    else
    {
        LeaveCriticalSection(&Lock);
        InternalUnacquire();
        return DIERR_INPUTLOST;
    }

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::Poll()
{
    HRESULT Result;

    EnterCriticalSection(&Lock);
    if (Flags & DI_DEV_POLLED_FORMAT)
        Result = (Flags & DI_DEV_ACQUIRED) ? Backend->Poll() : LostResult;
    else
        Result = (Flags & DI_DEV_ACQUIRED) ? DI_NOEFFECT : LostResult;
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::SendDeviceData(
    _In_ DWORD ObjectSize,
    _In_reads_(*Count) LPCDIDEVICEOBJECTDATA Data,
    _Inout_ LPDWORD Count,
    _In_ DWORD SendFlags)
{
    HRESULT Result;

    UNREFERENCED_PARAMETER(ObjectSize);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(SendFlags);
    EnterCriticalSection(&Lock);
    if (!(Flags & DI_DEV_ACQUIRED))
    {
        Result = LostResult;
    }
    else
    {
        if (Count != nullptr)
            *Count = 0;
        Result = E_NOTIMPL;
    }
    LeaveCriticalSection(&Lock);
    return Result;
}
