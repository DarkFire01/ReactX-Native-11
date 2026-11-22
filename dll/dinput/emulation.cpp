/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     System keyboard and mouse input through low level and window hooks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#define DI_MOUSE_STATE_SIZE     20
#define DI_KEYBOARD_STATE_SIZE  256
#define DI_MOUSE_BUTTONS_OFFSET 12

/* Scan codes the Japanese keyboards report as toggles */
#define DI_SCAN_KANA            0x70
#define DI_SCAN_KANJI           0x94
#define DI_SCAN_CAPITAL         0x3A

static BYTE DiMouseState[DI_MOUSE_STATE_SIZE];
static BYTE DiKeyboardState[DI_KEYBOARD_STATE_SIZE];

static HRESULT DiKeyboardAcquire(_Inout_ PRX_DI_SINK Sink, _In_ BOOL Acquire);
static HRESULT DiMouseAcquire(_Inout_ PRX_DI_SINK Sink, _In_ BOOL Acquire);

RX_DI_SOURCE DiKeyboardSource = { DiKeyboardState, nullptr, DiKeyboardAcquire, -1, DI_KEYBOARD_STATE_SIZE, 0 };
RX_DI_SOURCE DiMouseSource = { DiMouseState, nullptr, DiMouseAcquire, -1, DI_MOUSE_STATE_SIZE, 0 };

static PRX_DI_SINK DiSinks;
static const BYTE* DiKeyboardXlat;
static HHOOK DiKeyboardHook;
static BOOL DiKeyboardExclusive;
static BOOL DiKeyboardNoWinKey;
static BOOL DiKeyboardToggles;
static LONG DiMouseThreshold1 = MAXLONG;
static LONG DiMouseThreshold2 = MAXLONG;

/* The thread that owns the low level hooks for the process */
typedef struct _RX_DI_HOOK_SLOT
{
    LONG Count;
    LONG ExclusiveCount;
    HHOOK Hook;
} RX_DI_HOOK_SLOT;

typedef struct _RX_DI_WORKER
{
    DWORD ThreadId;
    LONG RefCount;
    RX_DI_HOOK_SLOT Slots[2];
    HANDLE Thread;
    HANDLE Ready;
} RX_DI_WORKER;

#define DI_SLOT_KEYBOARD    0
#define DI_SLOT_MOUSE       1

static RX_DI_WORKER* DiWorker;

static LRESULT CALLBACK DiLowLevelKeyboardProc(_In_ int Code, _In_ WPARAM WParam, _In_ LPARAM LParam);
static LRESULT CALLBACK DiLowLevelMouseProc(_In_ int Code, _In_ WPARAM WParam, _In_ LPARAM LParam);

static
VOID
DiFreeSink(
    _In_ PRX_DI_SINK Sink)
{
    PRX_DI_SINK* Link;

    if (Sink->UsesWorker)
    {
        RX_DI_WORKER* Worker;
        DWORD ThreadId = 0;

        EnterCriticalSection(&DiLock);
        Worker = DiWorker;
        if (Worker != nullptr)
        {
            ThreadId = Worker->ThreadId;
            if (InterlockedDecrement(&Worker->RefCount) == 0)
                DiWorker = nullptr;
        }
        LeaveCriticalSection(&DiLock);

        /* Wake the thread so it can notice it is no longer needed */
        if (Worker != nullptr)
            PostThreadMessageW(ThreadId, WM_NULL, 0, 0);
    }

    EnterCriticalSection(&DiLock);
    for (Link = &DiSinks; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Sink)
        {
            *Link = Sink->Next;
            break;
        }
    }
    LeaveCriticalSection(&DiLock);

    DiFree(reinterpret_cast<PVOID*>(&Sink->OffsetMap));
    DiFree(reinterpret_cast<PVOID*>(&Sink->Buffer));

    if (InterlockedDecrement(&Sink->Source->RefCount) == 0)
        DiFree(reinterpret_cast<PVOID*>(&Sink->Source->Types));

    LocalFree(Sink);
}

HRESULT
DiOpenSink(
    _In_ const RX_DI_SINK_PARAMS* Params,
    _In_ RX_DI_SOURCE* Source,
    _Outptr_ PRX_DI_SINK* Result)
{
    PRX_DI_SINK Sink = nullptr;
    HRESULT hr;

    *Result = nullptr;
    hr = DiAllocate(sizeof(*Sink), reinterpret_cast<PVOID*>(&Sink));
    if (FAILED(hr))
        return hr;

    Sink->Extra = Params->Extra;
    Sink->Source = Source;
    Sink->AcquireCount = -1;
    Sink->Flags = (Params->Mode << 16) | DI_SINK_EMULATED;
    Sink->State = Source->State;
    Sink->RefCount = 1;

    EnterCriticalSection(&DiLock);

    /* The first sink of a class records which offsets hold dwords */
    if (Source->Types == nullptr)
    {
        hr = DiAllocate(sizeof(ULONG) * Params->StateSize, reinterpret_cast<PVOID*>(&Source->Types));
        if (FAILED(hr))
        {
            LeaveCriticalSection(&DiLock);
            LocalFree(Sink);
            return hr;
        }

        for (ULONG Index = 0; Index < Params->ObjectCount; Index++)
        {
            ULONG Type = Params->Objects[Index].dwType;

            if (Type != 0 && (Type & DIDFT_NODATA) == 0)
                Source->Types[Params->Objects[Index].dwOfs] = Type;
        }
    }

    Sink->Next = DiSinks;
    DiSinks = Sink;
    InterlockedIncrement(&Source->RefCount);
    *Result = Sink;

    LeaveCriticalSection(&DiLock);
    return S_OK;
}

VOID
DiCloseSink(
    _In_opt_ PRX_DI_SINK Sink)
{
    if (Sink != nullptr && InterlockedDecrement(&Sink->RefCount) == 0)
        DiFreeSink(Sink);
}

HRESULT
DiOpenKeyboardSink(
    _Inout_ RX_DI_SINK_PARAMS* Params,
    _Outptr_ PRX_DI_SINK* Sink)
{
    if (DiKeyboardXlat == nullptr)
    {
        DiKeyboardXlat = reinterpret_cast<const BYTE*>(Params->Extra);
        if (DiKeyboardXlat == nullptr)
            DiKeyboardXlat = static_cast<const BYTE*>(DiFindTable(IDR_KEYS_IDENTITY));
    }

    /* Without low level hooks the per thread keyboard hook is all there is */
    if (!DiLowLevelHooks)
        Params->Mode = DI_EMULATE_HOOK_KBD;
    else if (Params->Mode != DI_EMULATE_HOOK_KBD)
        Params->Mode = DI_EMULATE_LL_KEYBOARD;

    return DiOpenSink(Params, &DiKeyboardSource, Sink);
}

HRESULT
DiOpenMouseSink(
    _Inout_ RX_DI_SINK_PARAMS* Params,
    _Outptr_ PRX_DI_SINK* Sink)
{
    if (!DiLowLevelHooks)
        Params->Mode = DI_EMULATE_SUBCLASS;
    else if (Params->Mode != DI_EMULATE_SUBCLASS)
        Params->Mode = DI_EMULATE_LL_MOUSE;

    return DiOpenSink(Params, &DiMouseSource, Sink);
}

HRESULT
DiAcquireSink(
    _Inout_ PRX_DI_SINK Sink)
{
    RX_DI_SOURCE* Source = Sink->Source;
    HRESULT hr;

    Sink->Flags |= DI_SINK_ACQUIRED;
    if (InterlockedIncrement(&Sink->AcquireCount) != 0)
        return S_OK;

    InterlockedIncrement(&Source->AcquireCount);
    hr = Source->Acquire(Sink, TRUE);
    if (FAILED(hr))
    {
        Sink->Flags &= ~DI_SINK_ACQUIRED;
        InterlockedDecrement(&Sink->AcquireCount);
        DiTrace(L"input capture start failed, 0x%08lx", hr);
    }
    return hr;
}

HRESULT
DiUnacquireSink(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG KeepFlags)
{
    if ((Sink->Flags & DI_SINK_ACQUIRED) == 0)
        return S_OK;

    Sink->Flags = (Sink->Flags & ~DI_SINK_ACQUIRED) | KeepFlags;
    if (InterlockedDecrement(&Sink->AcquireCount) >= 0)
        return S_OK;

    InterlockedDecrement(&Sink->Source->AcquireCount);
    return Sink->Source->Acquire(Sink, FALSE);
}

HRESULT
DiSetSinkFormat(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG Size,
    _In_reads_(Size) const ULONG* Map)
{
    HRESULT hr = DiAllocate(sizeof(ULONG) * Size, reinterpret_cast<PVOID*>(&Sink->OffsetMap));

    if (SUCCEEDED(hr))
        CopyMemory(Sink->OffsetMap, Map, sizeof(ULONG) * Size);
    return hr;
}

HRESULT
DiSetSinkBuffer(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG Count)
{
    HRESULT hr = DiAllocate(sizeof(DIDEVICEOBJECTDATA_DX3) * Count, reinterpret_cast<PVOID*>(&Sink->Buffer));

    if (SUCCEEDED(hr))
    {
        Sink->Head = Sink->Buffer;
        Sink->Tail = Sink->Buffer;
        Sink->BufferEnd = Sink->Buffer + Count;
    }
    return hr;
}

VOID
DiPrimeKeyboard(
    _In_ ULONG Toggles)
{
    if (DiKeyboardSource.AcquireCount >= 0)
        return;

    ZeroMemory(DiKeyboardState, sizeof(DiKeyboardState));
    if (Toggles & 1)
        DiKeyboardState[DI_SCAN_KANA] = 0x80;
    if (Toggles & 2)
        DiKeyboardState[DI_SCAN_CAPITAL] = 0x80;
    if (Toggles & 8)
        DiKeyboardState[DI_SCAN_KANJI] = 0x80;
    DiKeyboardToggles = (Toggles & 0x10) != 0;
}

static
DWORD
DiNextSequence(VOID)
{
    /* Zero never names an event */
    if (InterlockedIncrement(DiSequence) == 0)
        InterlockedIncrement(DiSequence);
    return static_cast<DWORD>(*DiSequence);
}

/* Queues one record while the list lock is dropped, returns the next sink to visit */
static
PRX_DI_SINK
DiBufferRecord(
    _In_ PRX_DI_SINK Sink,
    _In_ DWORD Data,
    _In_ ULONG Offset,
    _In_ DWORD Time,
    _In_ DWORD Sequence)
{
    PRX_DI_SINK Next;
    ULONG Mapped;

    InterlockedIncrement(&Sink->RefCount);
    LeaveCriticalSection(&DiLock);

    Sink->Device->Enter();
    Mapped = Sink->OffsetMap[Offset];
    if (Mapped != MAXULONG)
    {
        DIDEVICEOBJECTDATA_DX3* Record = Sink->Head;

        Record->dwOfs = Mapped;
        Record->dwData = Data;
        Record->dwTimeStamp = Time;
        Record->dwSequence = Sequence;

        if (++Record >= Sink->BufferEnd)
            Record = Sink->Buffer;
        Sink->Head = Record;

        /* A full ring drops its oldest record */
        if (Record == Sink->Tail)
        {
            Sink->Tail = Record + 1;
            if (Sink->Tail == Sink->BufferEnd)
                Sink->Tail = Sink->Buffer;
            Sink->Overflow = 1;
        }
    }
    Sink->Device->Leave();

    EnterCriticalSection(&DiLock);
    Next = Sink->Next;
    if (InterlockedDecrement(&Sink->RefCount) == 0)
    {
        LeaveCriticalSection(&DiLock);
        DiFreeSink(Sink);
        EnterCriticalSection(&DiLock);
    }
    return Next;
}

/* Updates one value of a class state and queues it for every sink listening */
static
BOOL
DiPostValue(
    _In_ RX_DI_SOURCE* Source,
    _In_ DWORD Value,
    _In_ ULONG Offset,
    _In_ DWORD Time,
    _In_ DWORD Sequence)
{
    DWORD Delta;
    DWORD Native;
    ULONG Type;
    BOOL Changed = FALSE;

    if (Source->Types == nullptr)
        return FALSE;

    Type = Source->Types[Offset];
    if (Type & DI_TYPE_HAS_DWORD)
    {
        DWORD UNALIGNED* Slot = reinterpret_cast<DWORD UNALIGNED*>(Source->State + Offset);

        if (*Slot == Value)
            return FALSE;

        /* POVs report where they point, axes report how far they moved */
        Delta = (Type & DIDFT_POV) ? Value : Value - *Slot;
        *Slot = Value;
        Native = (Type & DIDFT_RELAXIS) ? Delta : Value;
    }
    else
    {
        if (Source->State[Offset] == static_cast<BYTE>(Value))
            return FALSE;

        Source->State[Offset] = static_cast<BYTE>(Value);
        Delta = Value;
        Native = Value;
    }

    EnterCriticalSection(&DiLock);
    for (PRX_DI_SINK Sink = DiSinks; Sink != nullptr;)
    {
        if ((Sink->Flags & (DI_SINK_ACQUIRED | DI_SINK_BUFFER_ALWAYS)) == 0 || Sink->Source != Source)
        {
            Sink = Sink->Next;
            continue;
        }

        Changed = TRUE;
        if (Sink->Buffer == nullptr)
        {
            Sink = Sink->Next;
            continue;
        }

        if (Sink->Flags & DI_SINK_NATIVE_AXES)
            Sink = DiBufferRecord(Sink, Native, Offset, Time, Sequence);
        else if (Sink->Flags & DI_SINK_RELATIVE)
            Sink = DiBufferRecord(Sink, Delta, Offset, Time, Sequence);
        else
            Sink = DiBufferRecord(Sink, Value, Offset, Time, Sequence);
    }
    LeaveCriticalSection(&DiLock);
    return Changed;
}

static
VOID
DiSignalSinks(
    _In_ RX_DI_SOURCE* Source)
{
    EnterCriticalSection(&DiLock);
    for (PRX_DI_SINK Sink = DiSinks; Sink != nullptr; Sink = Sink->Next)
    {
        HANDLE Event;

        if ((Sink->Flags & DI_SINK_ACQUIRED) == 0 || Sink->Source != Source)
            continue;

        Event = Sink->Device->NotifyEvent();
        if (Event != nullptr)
            SetEvent(Event);
    }
    LeaveCriticalSection(&DiLock);
}

static
VOID
DiPostKey(
    _In_ DWORD Time,
    _In_ DWORD Data,
    _In_ ULONG Scan)
{
    DWORD Sequence = DiNextSequence();

    if (DiPostValue(&DiKeyboardSource, Data, Scan, Time, Sequence))
        DiSignalSinks(&DiKeyboardSource);
}

VOID
DiPostState(
    _In_ RX_DI_SOURCE* Source,
    _In_reads_bytes_(Source->StateSize) const BYTE* Values,
    _In_ DWORD Time)
{
    DWORD Sequence = DiNextSequence();
    BOOL Changed = FALSE;
    ULONG Offset = 0;

    if (Source->Types == nullptr)
        return;

    while (Offset < Source->StateSize)
    {
        if (Source->Types[Offset] & DI_TYPE_HAS_DWORD)
        {
            Changed |= DiPostValue(Source, *reinterpret_cast<const DWORD UNALIGNED*>(Values + Offset), Offset, Time, Sequence);
            Offset += sizeof(DWORD);
        }
        else
        {
            Changed |= DiPostValue(Source, Values[Offset], Offset, Time, Sequence);
            Offset += sizeof(BYTE);
        }
    }

    if (Changed)
        DiSignalSinks(Source);
}

static
VOID
DiPostMouse(
    _Inout_updates_bytes_(DI_MOUSE_STATE_SIZE) PBYTE Values,
    _In_ DWORD Time)
{
    PLONG Axes = reinterpret_cast<PLONG>(Values);
    const LONG* Current = reinterpret_cast<const LONG*>(DiMouseState);

    if (DiMouseSource.Types == nullptr)
        return;

    /* The class state holds positions, the hook reports movement */
    Axes[0] += Current[0];
    Axes[1] += Current[1];
    if (DiMouseSource.Types[2 * sizeof(LONG)] != 0)
        Axes[2] += Current[2];

    DiPostState(&DiMouseSource, Values, Time);
}

static
VOID
DiReadMouseSpeed(VOID)
{
    INT Speed[3];

    if (SystemParametersInfoW(SPI_GETMOUSE, 0, Speed, 0) && Speed[2] != 0)
    {
        DiMouseThreshold1 = Speed[0];
        DiMouseThreshold2 = (Speed[2] < 2) ? MAXLONG : 2 * Speed[1];
    }
    else
    {
        DiMouseThreshold1 = MAXLONG;
    }
}

static
VOID
DiSyncHook(
    _Inout_ RX_DI_WORKER* Worker,
    _In_ ULONG Which)
{
    RX_DI_HOOK_SLOT* Slot = &Worker->Slots[Which];

    if ((Slot->Count == 0) == (Slot->Hook == nullptr))
        return;

    if (Slot->Hook != nullptr)
    {
        UnhookWindowsHookEx(Slot->Hook);
        Slot->Hook = nullptr;
        return;
    }

    Slot->Hook = SetWindowsHookExW((Which == DI_SLOT_KEYBOARD) ? WH_KEYBOARD_LL : WH_MOUSE_LL,
                                   (Which == DI_SLOT_KEYBOARD) ? DiLowLevelKeyboardProc : DiLowLevelMouseProc,
                                   DiModule, 0);
    if (Slot->Hook == nullptr)
        DiTrace(L"low level %s hook failed, error %lu", (Which == DI_SLOT_KEYBOARD) ? L"keyboard" : L"mouse", GetLastError());
}

static
DWORD
WINAPI
DiWorkerProc(
    _In_ PVOID Parameter)
{
    RX_DI_WORKER* Worker = static_cast<RX_DI_WORKER*>(Parameter);
    MSG Message;

    DiReadMouseSpeed();
    GetInputState();

    /* A periodic timer keeps the message loop turning */
    SetTimer(nullptr, 0, 2000, nullptr);
    SetEvent(Worker->Ready);

    for (;;)
    {
        DiSyncHook(Worker, DI_SLOT_KEYBOARD);
        DiSyncHook(Worker, DI_SLOT_MOUSE);

        while (MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT | QS_ALLPOSTMESSAGE,
                                           MWMO_INPUTAVAILABLE) == WAIT_IO_COMPLETION)
        {
        }

        while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }

        if (Worker->RefCount == 0)
            break;
    }

    for (ULONG Which = 0; Which < _countof(Worker->Slots); Which++)
    {
        if (Worker->Slots[Which].Hook != nullptr)
            UnhookWindowsHookEx(Worker->Slots[Which].Hook);
    }

    if (Worker->Ready != nullptr)
        CloseHandle(Worker->Ready);
    if (Worker->Thread != nullptr)
        CloseHandle(Worker->Thread);
    LocalFree(Worker);

    FreeLibraryAndExitThread(DiModule, 0);
}

static
HRESULT
DiUseWorker(
    _Inout_ PRX_DI_SINK Sink,
    _Out_ RX_DI_WORKER** Result)
{
    WCHAR Path[MAX_PATH];
    RX_DI_WORKER* Worker;
    HANDLE Waits[2];
    DWORD Wait;
    HRESULT hr = S_OK;

    EnterCriticalSection(&DiLock);
    Worker = DiWorker;

    if (Sink->UsesWorker)
    {
        if (Worker == nullptr)
            hr = E_FAIL;
        goto Done;
    }

    if (Worker != nullptr)
    {
        Sink->UsesWorker = TRUE;
        InterlockedIncrement(&Worker->RefCount);
        goto Done;
    }

    Worker = nullptr;
    hr = DiAllocate(sizeof(*Worker), reinterpret_cast<PVOID*>(&Worker));
    if (FAILED(hr))
        goto Done;

    /* The thread holds its own reference on the DLL until it exits */
    if (GetModuleFileNameW(DiModule, Path, _countof(Path)) != 0 && LoadLibraryW(Path) == DiModule)
    {
        DiWorker = Worker;
        InterlockedIncrement(&Worker->RefCount);

        Worker->Ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (Worker->Ready != nullptr)
        {
            Worker->Thread = CreateThread(nullptr, 0, DiWorkerProc, Worker, 0, &Worker->ThreadId);
            if (Worker->Thread != nullptr)
            {
                SetThreadPriority(Worker->Thread, THREAD_PRIORITY_HIGHEST);
                Waits[0] = Worker->Thread;
                Waits[1] = Worker->Ready;
                do
                {
                    Wait = WaitForMultipleObjects(2, Waits, FALSE, INFINITE);
                } while (Wait == WAIT_FAILED && GetLastError() == ERROR_INVALID_PARAMETER);

                if (Wait == WAIT_OBJECT_0 + 1)
                {
                    Sink->UsesWorker = TRUE;
                    goto Done;
                }
            }
        }

        DiTrace(L"input thread start failed, error %lu", GetLastError());
        if (Worker->Ready != nullptr)
            CloseHandle(Worker->Ready);
        FreeLibrary(DiModule);
    }

    hr = E_FAIL;
    LocalFree(Worker);
    DiWorker = nullptr;
    Worker = nullptr;

Done:
    LeaveCriticalSection(&DiLock);
    *Result = Worker;
    return hr;
}

static
HRESULT
DiLowLevelAcquire(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOL Acquire,
    _In_ ULONG Flags,
    _In_ ULONG Which)
{
    RX_DI_WORKER* Worker = nullptr;
    RX_DI_HOOK_SLOT* Slot;
    HRESULT hr;

    hr = DiUseWorker(Sink, &Worker);
    if (FAILED(hr))
        return hr;

    Slot = &Worker->Slots[Which];
    if (Acquire)
    {
        InterlockedIncrement(&Slot->Count);
        if (Flags & DI_SINK_EXCLUSIVE)
            InterlockedIncrement(&Slot->ExclusiveCount);
    }
    else
    {
        if (Flags & DI_SINK_EXCLUSIVE)
            InterlockedDecrement(&Slot->ExclusiveCount);
        InterlockedDecrement(&Slot->Count);
    }

    PostThreadMessageW(Worker->ThreadId, WM_NULL, 0, 0);
    return hr;
}

static
LRESULT
CALLBACK
DiThreadKeyboardProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    ULONG Scan = 0;
    BOOL Released = FALSE;
    LRESULT Result;

    if (Code == HC_ACTION || Code == HC_NOREMOVE)
    {
        ULONG Raw = static_cast<ULONG>(HIWORD(LParam) & 0xFF) | ((LParam & (1 << 24)) ? 0x80 : 0);
        DWORD Time = GetMessageTime();

        Scan = DiKeyboardXlat[Raw];
        Released = (LParam & 0x80000000) != 0;
        DiPostKey(Time, Released ? 0 : 0x80, Scan);
    }

    Result = CallNextHookEx(DiKeyboardHook, Code, WParam, LParam);

    /* Exclusive mode keeps keys away from the window, Alt+Tab and Alt release aside */
    if (DiKeyboardExclusive)
    {
        BOOL AltTab = (HIWORD(LParam) & KF_ALTDOWN) != 0 && Scan == 0x0F;

        if (!AltTab && ((Scan - 0x38) & 0x7F) != 0)
            return 1;
        if (!AltTab && !Released)
            return 1;
    }
    else if (DiKeyboardNoWinKey && static_cast<BYTE>(Scan + 0x25) <= 1)
    {
        return 1;
    }
    return Result;
}

static
LRESULT
CALLBACK
DiLowLevelKeyboardProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    const KBDLLHOOKSTRUCT* Key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(LParam);
    RX_DI_WORKER* Worker;
    LRESULT Result;

    if (Code == HC_ACTION)
    {
        ULONG Raw = Key->scanCode & 0xFF;
        DWORD Data = (Key->flags & LLKHF_UP) ? 0 : 0x80;
        ULONG Scan;

        if (Raw != 0)
        {
            if (Key->flags & LLKHF_EXTENDED)
                Raw |= 0x80;
        }
        else
        {
            /* Injected keys can come without a scan code */
            Raw = MapVirtualKeyW(Key->vkCode, MAPVK_VK_TO_VSC);
            if (MapVirtualKeyW(Raw, MAPVK_VSC_TO_VK_EX) != Key->vkCode)
                Raw |= 0x80;
        }

        Scan = DiKeyboardXlat[Raw & 0xFF];

        if (DiKeyboardToggles && (Scan == DI_SCAN_KANA || Scan == DI_SCAN_KANJI || Scan == DI_SCAN_CAPITAL))
        {
            if (Key->flags & LLKHF_UP)
                goto Forward;
            Data = DiKeyboardState[Scan] ^ 0x80;
        }

        DiPostKey(GetTickCount(), Data, Scan);
    }

Forward:
    Worker = DiWorker;
    if (Worker == nullptr)
        return 1;

    Result = CallNextHookEx(Worker->Slots[DI_SLOT_KEYBOARD].Hook, Code, WParam, LParam);

    if (DiKeyboardExclusive)
    {
        if ((Key->flags & LLKHF_ALTDOWN) && Key->vkCode == VK_TAB)
            return Result;
        if ((Key->flags & LLKHF_UP) && (Key->vkCode == VK_LMENU || Key->vkCode == VK_RMENU))
            return Result;
        return 1;
    }

    if (DiKeyboardNoWinKey && (Key->vkCode == VK_LWIN || Key->vkCode == VK_RWIN))
        return 1;
    return Result;
}

static
LONG
DiDecelerate(
    _In_ LONG Delta)
{
    LONG Magnitude = (Delta >= 0) ? Delta : ~Delta;

    if (Magnitude <= DiMouseThreshold1)
        return Delta;
    return (Magnitude <= DiMouseThreshold2) ? Delta / 2 : Delta / 4;
}

static
LRESULT
CALLBACK
DiLowLevelMouseProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    const MSLLHOOKSTRUCT* Mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(LParam);
    RX_DI_WORKER* Worker;
    LRESULT Result;

    if (Code == HC_ACTION)
    {
        BYTE Values[DI_MOUSE_STATE_SIZE] = {};
        PLONG Axes = reinterpret_cast<PLONG>(Values);
        PBYTE Buttons = Values + DI_MOUSE_BUTTONS_OFFSET;
        WPARAM Message = WParam;
        BOOL Known = TRUE;
        POINT Cursor = {};

        CopyMemory(Buttons, DiMouseState + DI_MOUSE_BUTTONS_OFFSET, 8);

        /* Report the physical buttons, before the user swapped them */
        if (GetSystemMetrics(SM_SWAPBUTTON))
        {
            if (Message >= WM_LBUTTONDOWN && Message <= WM_LBUTTONDBLCLK)
                Message += 3;
            else if (Message >= WM_RBUTTONDOWN && Message <= WM_RBUTTONDBLCLK)
                Message -= 3;
        }

        switch (Message)
        {
            case WM_MOUSEMOVE:
                break;
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                Buttons[0] = 0x80;
                break;
            case WM_LBUTTONUP:
                Buttons[0] = 0;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                Buttons[1] = 0x80;
                break;
            case WM_RBUTTONUP:
                Buttons[1] = 0;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
                Buttons[2] = 0x80;
                break;
            case WM_MBUTTONUP:
                Buttons[2] = 0;
                break;
            case WM_MOUSEWHEEL:
                Axes[2] = static_cast<LONG>(Mouse->mouseData) >> 16;
                break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONDBLCLK:
            case WM_XBUTTONUP:
            {
                BYTE Down = (Message == WM_XBUTTONUP) ? 0 : 0x80;

                if (HIWORD(Mouse->mouseData) == XBUTTON1)
                    Buttons[3] = Down;
                else if (HIWORD(Mouse->mouseData) == XBUTTON2)
                    Buttons[4] = Down;
                break;
            }
            default:
                Known = FALSE;
                break;
        }

        if (Known)
        {
            GetCursorPos(&Cursor);
            Axes[0] = DiDecelerate(Mouse->pt.x - Cursor.x);
            Axes[1] = DiDecelerate(Mouse->pt.y - Cursor.y);
            DiPostMouse(Values, GetTickCount());
        }
    }

    Worker = DiWorker;
    if (Worker == nullptr)
        return 1;

    Result = CallNextHookEx(Worker->Slots[DI_SLOT_MOUSE].Hook, Code, WParam, LParam);
    if (Worker->Slots[DI_SLOT_MOUSE].ExclusiveCount != 0)
        Result = 1;
    return Result;
}

static
HRESULT
DiKeyboardAcquire(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOL Acquire)
{
    HRESULT hr = E_FAIL;

    DiKeyboardNoWinKey = FALSE;
    DiKeyboardExclusive = FALSE;
    if (Acquire)
    {
        if (Sink->Flags & DI_SINK_EXCLUSIVE)
            DiKeyboardExclusive = TRUE;
        else if (Sink->Flags & DI_SINK_NOWINKEY)
            DiKeyboardNoWinKey = TRUE;
    }

    if (Sink->Flags & DI_SINK_LL_KEYBOARD)
        return DiLowLevelAcquire(Sink, Acquire, Sink->Flags, DI_SLOT_KEYBOARD);

    EnterCriticalSection(&DiLock);
    if (!Acquire)
    {
        UnhookWindowsHookEx(DiKeyboardHook);
        DiKeyboardHook = nullptr;
        hr = S_OK;
    }
    else if (Sink->Window != nullptr && DiKeyboardHook == nullptr)
    {
        DiKeyboardHook = SetWindowsHookExW(WH_KEYBOARD, DiThreadKeyboardProc, DiModule,
                                           GetWindowThreadProcessId(Sink->Window, nullptr));
        if (DiKeyboardHook == nullptr)
            DiTrace(L"keyboard hook failed, error %lu", GetLastError());
        hr = S_OK;
    }
    LeaveCriticalSection(&DiLock);
    return hr;
}

static
HRESULT
DiMouseAcquire(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOL Acquire)
{
    HRESULT hr;

    if ((Sink->Flags & DI_SINK_LL_MOUSE) == 0)
    {
        DiTrace(L"mouse capture without low level hooks is not supported");
        return E_FAIL;
    }

    hr = DiLowLevelAcquire(Sink, Acquire, Sink->Flags, DI_SLOT_MOUSE);
    if (FAILED(hr))
        return hr;

    if (Acquire && (Sink->Flags & DI_SINK_EXCLUSIVE))
    {
        if (!Sink->CursorHidden)
        {
            ShowCursor(FALSE);
            Sink->CursorHidden = TRUE;
        }
    }
    else if (Sink->CursorHidden)
    {
        ShowCursor(TRUE);
        Sink->CursorHidden = FALSE;
    }
    return hr;
}

VOID
DiSeedMouse(
    _In_ ULONG Buttons)
{
    PLONG Axes = reinterpret_cast<PLONG>(DiMouseState);

    if (DiMouseSource.AcquireCount >= 0)
        return;

    *reinterpret_cast<ULONG UNALIGNED*>(DiMouseState + DI_MOUSE_BUTTONS_OFFSET) = Buttons;

    /* Positions start somewhere arbitrary, only movement is meaningful */
    while (Axes[0] == 0)
    {
        ULONG Tick = GetTickCount();
        ULONG Spun = (Tick << 16) | (Tick >> 16);

        Axes[0] = static_cast<LONG>(static_cast<ULONG>(reinterpret_cast<ULONG_PTR>(&Tick)) * Spun);
        Axes[1] = static_cast<LONG>(Spun);
    }
}

VOID
DiProcessShutdown(VOID)
{
}
