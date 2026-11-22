/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     System keyboard and mouse input gathered on a worker thread for every acquired device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "input.hpp"
#include "keyboard.hpp"
#include "shared.hpp"

#include <stdlib.h>

DI_EM_CLASS DiKeyboardClass = { TRUE, 256 };
DI_EM_CLASS DiMouseClass = { FALSE, 20 };

/* Every live instance, guarded by DiLock */
static DI_EM_INSTANCE* DiEmFirst;

/* The worker thread and what it listens with */
static HANDLE DiEmThread;
static DWORD DiEmThreadId;
static HWND DiEmWindow;
static LONG DiEmThreadUsers;
static HANDLE DiEmReady;
static BOOLEAN DiEmUseRawInput = TRUE;
static HHOOK DiEmKeyboardLowHook;
static HHOOK DiEmMouseLowHook;
static HHOOK DiEmKeyboardHook;

/* Keys the low level and message hooks keep from the system while a keyboard is held */
static BOOLEAN DiEmBlockKeys;
static BOOLEAN DiEmBlockWinKeys;

/* Raw input state carried between packets */
static BOOLEAN DiEmPausePending;
static BOOLEAN DiEmAbsoluteSeen;
static LONG DiEmLastAbsoluteX;
static LONG DiEmLastAbsoluteY;
static BYTE DiEmButtons[5];

/* Mouse acceleration thresholds the low level hook undoes */
static int DiEmAccelFirst = 0x7FFFFFFF;
static int DiEmAccelSecond = 0x7FFFFFFF;

static const WCHAR DiEmWindowClass[] = L"ReactXDInputEmulation";

static
DWORD
DiEmNextSequence(VOID)
{
    static DWORD Local;
    volatile LONG* Counter = DiShared != nullptr ? reinterpret_cast<volatile LONG*>(&DiShared->Sequence) :
                                                   reinterpret_cast<volatile LONG*>(&Local);
    LONG Value = InterlockedIncrement(Counter);

    if (Value == 0)
        Value = InterlockedIncrement(Counter);
    return static_cast<DWORD>(Value);
}

/* Appends one change to an instance's ring, dropping the oldest when it is full */
static
VOID
DiEmBufferEvent(
    _In_ DI_EM_INSTANCE* Instance,
    _In_ DWORD Offset,
    _In_ DWORD Data,
    _In_ DWORD Time,
    _In_ DWORD Sequence)
{
    DI_EM_EVENT* Event;
    LONG Object;

    EnterCriticalSection(&Instance->Lock);
    if (Instance->Buffer != nullptr && Instance->ObjectMap != nullptr && Offset < Instance->ObjectMapSize)
    {
        Object = Instance->ObjectMap[Offset];
        if (Object >= 0)
        {
            Event = &Instance->Buffer[Instance->Head];
            Event->Object = static_cast<DWORD>(Object);
            Event->Data = Data;
            Event->TimeStamp = Time;
            Event->Sequence = Sequence;
            Instance->Head = (Instance->Head + 1) % (Instance->Capacity + 1);
            if (Instance->Head == Instance->Tail)
            {
                Instance->Tail = (Instance->Tail + 1) % (Instance->Capacity + 1);
                Instance->Overflow = TRUE;
            }
        }
    }
    LeaveCriticalSection(&Instance->Lock);
}

/*
 * Applies a new native state to a class, handing each changed object to the instances that
 * listen. Axes buffer the change in relative mode and the position otherwise.
 */
static
VOID
DiEmApplyState(
    _In_ DI_EM_CLASS* Class,
    _In_reads_bytes_(Class->DataSize) const BYTE* State,
    _In_ DWORD Time)
{
    DI_EM_INSTANCE* Listeners[64];
    UINT ListenerCount = 0;
    DWORD Sequence = DiEmNextSequence();
    BOOLEAN Changed = FALSE;

    if (!Class->TypesReady)
        return;

    EnterCriticalSection(&DiLock);
    for (DI_EM_INSTANCE* Instance = DiEmFirst; Instance != nullptr && ListenerCount < ARRAYSIZE(Listeners);
         Instance = Instance->Next)
    {
        if (Instance->Class == Class && (Instance->Flags & DI_EM_ACQUIRED))
        {
            InterlockedIncrement(&Instance->Refs);
            Listeners[ListenerCount++] = Instance;
        }
    }

    for (DWORD Offset = 0; Offset < Class->DataSize;)
    {
        DWORD Type = Class->ObjectTypes[Offset];

        if (Type & (DIDFT_AXIS | DIDFT_POV))
        {
            LONG New = *reinterpret_cast<const LONG*>(State + Offset);
            LONG Old = *reinterpret_cast<LONG*>(Class->State + Offset);

            if (New != Old)
            {
                LONG Delta = (Type & DIDFT_POV) ? New : New - Old;

                *reinterpret_cast<LONG*>(Class->State + Offset) = New;
                Changed = TRUE;
                for (UINT Index = 0; Index < ListenerCount; Index++)
                {
                    DI_EM_INSTANCE* Instance = Listeners[Index];

                    DiEmBufferEvent(Instance, Offset, (Instance->Flags & DI_EM_RELATIVE) ? Delta : New, Time, Sequence);
                }
            }
            Offset += 4;
        }
        else
        {
            if (Type != 0 && State[Offset] != Class->State[Offset])
            {
                Class->State[Offset] = State[Offset];
                Changed = TRUE;
                for (UINT Index = 0; Index < ListenerCount; Index++)
                    DiEmBufferEvent(Listeners[Index], Offset, State[Offset], Time, Sequence);
            }
            Offset++;
        }
    }

    for (UINT Index = 0; Index < ListenerCount; Index++)
    {
        if (Changed && Listeners[Index]->Event != nullptr)
            SetEvent(Listeners[Index]->Event);
    }
    LeaveCriticalSection(&DiLock);
    for (UINT Index = 0; Index < ListenerCount; Index++)
        DiEmRelease(Listeners[Index]);
}

/* One key changed state */
static
VOID
DiEmKeyEvent(
    _In_ BYTE Code,
    _In_ BYTE Value,
    _In_ DWORD Time)
{
    BYTE State[256];

    EnterCriticalSection(&DiLock);
    CopyMemory(State, DiKeyboardClass.State, sizeof(State));
    LeaveCriticalSection(&DiLock);
    State[Code] = Value;
    DiEmApplyState(&DiKeyboardClass, State, Time);
}

/* Mouse movement and buttons, with the positions kept as running sums of the motion */
static
VOID
DiEmMouseEvent(
    _In_ LONG DeltaX,
    _In_ LONG DeltaY,
    _In_ LONG DeltaZ,
    _In_ DWORD Time)
{
    BYTE State[20];

    EnterCriticalSection(&DiLock);
    CopyMemory(State, DiMouseClass.State, sizeof(State));
    LeaveCriticalSection(&DiLock);
    *reinterpret_cast<LONG*>(State + 0) += DeltaX;
    *reinterpret_cast<LONG*>(State + 4) += DeltaY;
    if (DiMouseClass.ObjectTypes[8] != 0)
        *reinterpret_cast<LONG*>(State + 8) += DeltaZ;
    CopyMemory(State + 12, DiEmButtons, sizeof(DiEmButtons));
    DiEmApplyState(&DiMouseClass, State, Time);
}

VOID
DiEmSeedMouseButtons(
    _In_ BYTE Buttons[3])
{
    CopyMemory(DiEmButtons, Buttons, 3);
}

/* Raw input */

static
VOID
DiEmProcessKeyboardRaw(
    _In_ const RAWKEYBOARD* Keyboard)
{
    BYTE Code = static_cast<BYTE>(Keyboard->MakeCode);

    if (Keyboard->Flags & RI_KEY_E0)
    {
        if (Code == 0x2A)
            return;
        Code |= 0x80;
        DiEmPausePending = FALSE;
    }
    else if (Keyboard->Flags & RI_KEY_E1)
    {
        DiEmPausePending = TRUE;
        if (Code == 0x1D)
            return;
        Code |= 0x80;
        DiEmPausePending = FALSE;
    }
    else if (DiEmPausePending)
    {
        Code |= 0x80;
        DiEmPausePending = FALSE;
    }
    if ((Code & 0x7F) != 0x45 && DiKeyTranslation != nullptr)
        Code = DiKeyTranslation[Code];
    DiEmKeyEvent(Code, (Keyboard->Flags & RI_KEY_BREAK) ? 0 : 0x80, GetTickCount());
}

static
VOID
DiEmProcessMouseRaw(
    _In_ const RAWMOUSE* Mouse)
{
    static const USHORT Downs[] =
    {
        RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN,
    };
    LONG DeltaX = Mouse->lLastX;
    LONG DeltaY = Mouse->lLastY;
    LONG Wheel = 0;

    if (Mouse->usFlags & MOUSE_MOVE_ABSOLUTE)
    {
        if (!DiEmAbsoluteSeen)
        {
            DiEmAbsoluteSeen = TRUE;
            DiEmLastAbsoluteX = Mouse->lLastX;
            DiEmLastAbsoluteY = Mouse->lLastY;
            return;
        }
        DeltaX = Mouse->lLastX - DiEmLastAbsoluteX;
        DeltaY = Mouse->lLastY - DiEmLastAbsoluteY;
        DiEmLastAbsoluteX = Mouse->lLastX;
        DiEmLastAbsoluteY = Mouse->lLastY;
    }
    if (Mouse->usButtonFlags & RI_MOUSE_WHEEL)
        Wheel = static_cast<SHORT>(Mouse->usButtonData);
    for (UINT Index = 0; Index < ARRAYSIZE(Downs); Index++)
    {
        if (Mouse->usButtonFlags & Downs[Index])
            DiEmButtons[Index] = 0x80;
        else if (Mouse->usButtonFlags & (Downs[Index] << 1))
            DiEmButtons[Index] = 0;
    }
    DiEmMouseEvent(DeltaX, DeltaY, Wheel, GetTickCount());
}

static
VOID
DiEmProcessRawInput(
    _In_ HRAWINPUT Handle)
{
    BYTE Small[sizeof(RAWINPUT) + 64];
    RAWINPUT* Input = reinterpret_cast<RAWINPUT*>(Small);
    UINT Size = 0;

    if (GetRawInputData(Handle, RID_INPUT, nullptr, &Size, sizeof(RAWINPUTHEADER)) != 0)
        return;
    if (Size > sizeof(Small))
    {
        Input = static_cast<RAWINPUT*>(malloc(Size));
        if (Input == nullptr)
            return;
    }
    if (GetRawInputData(Handle, RID_INPUT, Input, &Size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1))
    {
        if (Input->header.dwType == RIM_TYPEMOUSE && DiMouseClass.Acquired > 0)
            DiEmProcessMouseRaw(&Input->data.mouse);
        else if (Input->header.dwType == RIM_TYPEKEYBOARD && DiKeyboardClass.Acquired > 0)
            DiEmProcessKeyboardRaw(&Input->data.keyboard);
    }
    if (Input != reinterpret_cast<RAWINPUT*>(Small))
        free(Input);
}

/* Registers or drops raw input for one class in the mode its instances need */
static
HRESULT
DiEmRegisterRaw(
    _In_ BOOLEAN Keyboard,
    _In_ BOOLEAN Remove,
    _In_ DWORD Mode)
{
    RAWINPUTDEVICE Device;

    Device.usUsagePage = 1;
    Device.usUsage = Keyboard ? 6 : 2;
    Device.hwndTarget = Remove ? nullptr : DiEmWindow;
    if (Remove)
        Device.dwFlags = RIDEV_REMOVE;
    else if (Mode == DI_EM_EXCLUSIVE)
        Device.dwFlags = RIDEV_NOLEGACY | (Keyboard ? RIDEV_NOHOTKEYS : RIDEV_CAPTUREMOUSE);
    else if (Mode == DI_EM_NOWINKEY)
        Device.dwFlags = RIDEV_NOHOTKEYS;
    else
        Device.dwFlags = RIDEV_INPUTSINK;
    if (!RegisterRawInputDevices(&Device, 1, sizeof(Device)))
    {
        DiTrace(L"cannot %s raw input for the %s (%lu)", Remove ? L"remove" : L"register",
                Keyboard ? L"keyboard" : L"mouse", GetLastError());
        return E_FAIL;
    }
    return S_OK;
}

/* Low level hooks, used when raw input cannot be registered */

static
LONG
DiEmRemoveAcceleration(
    _In_ LONG Delta)
{
    LONG Magnitude = Delta >= 0 ? Delta : ~Delta;

    if (Magnitude > DiEmAccelFirst)
        return Magnitude <= DiEmAccelSecond ? Delta / 2 : Delta / 4;
    return Delta;
}

static
VOID
DiEmReadAcceleration(VOID)
{
    int Values[3];

    if (SystemParametersInfoW(SPI_GETMOUSE, 0, Values, 0) && Values[2] != 0)
    {
        DiEmAccelFirst = Values[0];
        DiEmAccelSecond = Values[2] < 2 ? 0x7FFFFFFF : 2 * Values[1];
    }
    else
    {
        DiEmAccelFirst = 0x7FFFFFFF;
    }
}

static
LRESULT
CALLBACK
DiEmLowKeyboardProc(
    _In_ int Code,
    _In_ WPARAM Message,
    _In_ LPARAM Param)
{
    const KBDLLHOOKSTRUCT* Key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(Param);
    LRESULT Result;

    if (Code == HC_ACTION)
    {
        BYTE Scan = static_cast<BYTE>(Key->scanCode);

        if (Scan == 0)
        {
            Scan = static_cast<BYTE>(MapVirtualKeyW(Key->vkCode, MAPVK_VK_TO_VSC));
            if (MapVirtualKeyW(Scan, MAPVK_VSC_TO_VK_EX) != Key->vkCode)
                Scan |= 0x80;
        }
        else if (Key->flags & LLKHF_EXTENDED)
        {
            Scan |= 0x80;
        }
        if (DiKeyTranslation != nullptr)
            Scan = DiKeyTranslation[Scan];
        DiEmKeyEvent(Scan, (Key->flags & LLKHF_UP) ? 0 : 0x80, GetTickCount());
    }
    Result = CallNextHookEx(DiEmKeyboardLowHook, Code, Message, Param);
    if (DiEmBlockKeys)
    {
        if (((Key->flags & LLKHF_ALTDOWN) && Key->vkCode == VK_TAB) ||
            ((Key->flags & LLKHF_UP) && (Key->vkCode == VK_LMENU || Key->vkCode == VK_RMENU)))
        {
            return Result;
        }
        return 1;
    }
    if (DiEmBlockWinKeys && (Key->vkCode == VK_LWIN || Key->vkCode == VK_RWIN))
        return 1;
    return Result;
}

static
LRESULT
CALLBACK
DiEmLowMouseProc(
    _In_ int Code,
    _In_ WPARAM Message,
    _In_ LPARAM Param)
{
    const MSLLHOOKSTRUCT* Mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(Param);

    if (Code == HC_ACTION)
    {
        LONG Wheel = 0;
        POINT Cursor = {};

        if (GetSystemMetrics(SM_SWAPBUTTON))
        {
            if (Message >= WM_LBUTTONDOWN && Message <= WM_LBUTTONDBLCLK)
                Message += 3;
            else if (Message >= WM_RBUTTONDOWN && Message <= WM_RBUTTONDBLCLK)
                Message -= 3;
        }
        switch (Message)
        {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                DiEmButtons[0] = 0x80;
                break;
            case WM_LBUTTONUP:
                DiEmButtons[0] = 0;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                DiEmButtons[1] = 0x80;
                break;
            case WM_RBUTTONUP:
                DiEmButtons[1] = 0;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONDBLCLK:
                DiEmButtons[2] = 0x80;
                break;
            case WM_MBUTTONUP:
                DiEmButtons[2] = 0;
                break;
            case WM_MOUSEWHEEL:
                Wheel = static_cast<SHORT>(HIWORD(Mouse->mouseData));
                break;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONDBLCLK:
            case WM_XBUTTONUP:
                if (HIWORD(Mouse->mouseData) == XBUTTON1 || HIWORD(Mouse->mouseData) == XBUTTON2)
                    DiEmButtons[2 + HIWORD(Mouse->mouseData)] = Message == WM_XBUTTONUP ? 0 : 0x80;
                break;
            case WM_MOUSEMOVE:
                break;
            default:
                goto Next;
        }
        GetCursorPos(&Cursor);
        DiEmMouseEvent(DiEmRemoveAcceleration(Mouse->pt.x - Cursor.x), DiEmRemoveAcceleration(Mouse->pt.y - Cursor.y),
                       Wheel, GetTickCount());
    }

Next:
    if (DiMouseClass.Exclusive > 0)
    {
        CallNextHookEx(DiEmMouseLowHook, Code, Message, Param);
        return 1;
    }
    return CallNextHookEx(DiEmMouseLowHook, Code, Message, Param);
}

/* Keeps the low level hooks in step with the acquired classes, on the worker thread */
static
VOID
DiEmUpdateHooks(VOID)
{
    if (DiEmUseRawInput)
        return;
    if ((DiKeyboardClass.Acquired > 0) != (DiEmKeyboardLowHook != nullptr))
    {
        if (DiEmKeyboardLowHook != nullptr)
        {
            UnhookWindowsHookEx(DiEmKeyboardLowHook);
            DiEmKeyboardLowHook = nullptr;
        }
        else
        {
            DiEmKeyboardLowHook = SetWindowsHookExW(WH_KEYBOARD_LL, DiEmLowKeyboardProc, DiInstance, 0);
            if (DiEmKeyboardLowHook == nullptr)
                DiTrace(L"cannot hook the keyboard (%lu)", GetLastError());
        }
    }
    if ((DiMouseClass.Acquired > 0) != (DiEmMouseLowHook != nullptr))
    {
        if (DiEmMouseLowHook != nullptr)
        {
            UnhookWindowsHookEx(DiEmMouseLowHook);
            DiEmMouseLowHook = nullptr;
        }
        else
        {
            DiEmMouseLowHook = SetWindowsHookExW(WH_MOUSE_LL, DiEmLowMouseProc, DiInstance, 0);
            if (DiEmMouseLowHook == nullptr)
                DiTrace(L"cannot hook the mouse (%lu)", GetLastError());
        }
    }
}

static
LRESULT
CALLBACK
DiEmWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Message == WM_INPUT && WParam <= RIM_INPUTSINK)
        DiEmProcessRawInput(reinterpret_cast<HRAWINPUT>(LParam));
    return DefWindowProcW(Window, Message, WParam, LParam);
}

static
DWORD
WINAPI
DiEmThreadProc(
    _In_ VOID* Parameter)
{
    MSG Message;

    UNREFERENCED_PARAMETER(Parameter);
    DiEmReadAcceleration();
    PeekMessageW(&Message, nullptr, 0, 0, PM_NOREMOVE);
    if (DiEmUseRawInput)
    {
        WNDCLASSW Class = {};

        Class.lpfnWndProc = DiEmWindowProc;
        Class.hInstance = DiInstance;
        Class.lpszClassName = DiEmWindowClass;
        RegisterClassW(&Class);
        DiEmWindow = CreateWindowExW(0, DiEmWindowClass, L"DIEmWin", WS_OVERLAPPEDWINDOW, -1, -1, 1, 1, nullptr, nullptr,
                                     DiInstance, nullptr);
        if (DiEmWindow == nullptr)
        {
            DiTrace(L"cannot create the input window (%lu), using hooks", GetLastError());
            DiEmUseRawInput = FALSE;
        }
    }
    SetEvent(DiEmReady);

    for (;;)
    {
        DiEmUpdateHooks();
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE);
        while (PeekMessageW(&Message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
        if (DiEmThreadUsers == 0)
            break;
    }

    if (DiEmKeyboardLowHook != nullptr)
        UnhookWindowsHookEx(DiEmKeyboardLowHook);
    if (DiEmMouseLowHook != nullptr)
        UnhookWindowsHookEx(DiEmMouseLowHook);
    DiEmKeyboardLowHook = nullptr;
    DiEmMouseLowHook = nullptr;
    if (DiEmWindow != nullptr)
        DestroyWindow(DiEmWindow);
    DiEmWindow = nullptr;
    FreeLibraryAndExitThread(DiInstance, 0);
}

/* Starts the worker thread the first time an instance needs it; it keeps the library loaded */
static
HRESULT
DiEmHoldThread(
    _In_ DI_EM_INSTANCE* Instance)
{
    HRESULT Result = S_OK;
    WCHAR Path[MAX_PATH];

    EnterCriticalSection(&DiLock);
    if (Instance->HoldsThread)
        goto Exit;
    if (DiEmThread != nullptr && DiEmThreadUsers > 0)
    {
        DiEmThreadUsers++;
        Instance->HoldsThread = TRUE;
        goto Exit;
    }
    if (DiEmThread != nullptr)
    {
        CloseHandle(DiEmThread);
        DiEmThread = nullptr;
    }
    if (GetModuleFileNameW(DiInstance, Path, _countof(Path)) == 0 || LoadLibraryW(Path) != DiInstance)
    {
        Result = E_FAIL;
        goto Exit;
    }
    if (DiEmReady == nullptr)
        DiEmReady = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    DiEmThreadUsers = 1;
    DiEmThread = CreateThread(nullptr, 0, DiEmThreadProc, nullptr, 0, &DiEmThreadId);
    if (DiEmThread == nullptr || DiEmReady == nullptr)
    {
        DiTrace(L"cannot start the input thread (%lu)", GetLastError());
        DiEmThreadUsers = 0;
        FreeLibrary(DiInstance);
        Result = E_FAIL;
        goto Exit;
    }
    SetThreadPriority(DiEmThread, THREAD_PRIORITY_HIGHEST);
    LeaveCriticalSection(&DiLock);
    WaitForSingleObject(DiEmReady, INFINITE);
    EnterCriticalSection(&DiLock);
    Instance->HoldsThread = TRUE;

Exit:
    LeaveCriticalSection(&DiLock);
    return Result;
}

static
VOID
DiEmWakeThread(VOID)
{
    if (DiEmThreadId != 0)
        PostThreadMessageW(DiEmThreadId, WM_NULL, 0, 0);
}

/* Instances */

HRESULT
DiEmCreate(
    _In_ DI_EM_CLASS* Class,
    _In_ const DI_FORMAT* Format,
    _In_ DWORD Mode,
    _Outptr_ DI_EM_INSTANCE** Instance)
{
    DI_EM_INSTANCE* Created = static_cast<DI_EM_INSTANCE*>(calloc(1, sizeof(*Created)));

    *Instance = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    Created->Flags = Mode | DI_EM_ACTIVE;
    Created->Class = Class;
    Created->Refs = 1;
    Created->AcquireCount = -1;
    InitializeCriticalSection(&Created->Lock);

    EnterCriticalSection(&DiLock);
    if (!Class->TypesReady)
    {
        for (DWORD Index = 0; Index < Format->ObjectCount; Index++)
        {
            const DI_OBJECT* Object = &Format->Objects[Index];

            if (Object->Type != 0 && !(Object->Type & DIDFT_NODATA) && Object->Offset < ARRAYSIZE(Class->ObjectTypes))
                Class->ObjectTypes[Object->Offset] = Object->Type;
        }
        Class->TypesReady = TRUE;
    }
    Created->Next = DiEmFirst;
    DiEmFirst = Created;
    LeaveCriticalSection(&DiLock);
    *Instance = Created;
    return S_OK;
}

VOID
DiEmRelease(
    _In_ DI_EM_INSTANCE* Instance)
{
    BOOLEAN WakeThread = FALSE;

    if (InterlockedDecrement(&Instance->Refs) != 0)
        return;

    EnterCriticalSection(&DiLock);
    for (DI_EM_INSTANCE** Link = &DiEmFirst; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Instance)
        {
            *Link = Instance->Next;
            break;
        }
    }
    if (Instance->HoldsThread && --DiEmThreadUsers == 0)
        WakeThread = TRUE;
    LeaveCriticalSection(&DiLock);
    if (WakeThread)
        DiEmWakeThread();

    DeleteCriticalSection(&Instance->Lock);
    free(Instance->ObjectMap);
    free(Instance->Buffer);
    free(Instance);
}

static
LRESULT
CALLBACK
DiEmKeyboardProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    LRESULT Result;
    BYTE Key = 0;
    BYTE Value = 0;

    if (Code == HC_ACTION || Code == HC_NOREMOVE)
    {
        BYTE Scan = static_cast<BYTE>((LParam >> 16) & 0xFF) | ((LParam & 0x01000000) ? 0x80 : 0);

        Value = (LParam & 0x80000000) ? 0 : 0x80;
        Key = DiKeyTranslation != nullptr ? DiKeyTranslation[Scan] : Scan;
        DiEmKeyEvent(Key, Value, GetMessageTime());
    }
    Result = CallNextHookEx(DiEmKeyboardHook, Code, WParam, LParam);
    if (DiEmBlockKeys)
    {
        if (!((LParam & 0x20000000) && Key == 0x0F) && ((Key & 0x7F) != 0x38 || Value != 0))
            return 1;
    }
    else if (DiEmBlockWinKeys && (Key == 0xDB || Key == 0xDC))
    {
        return 1;
    }
    return Result;
}

/* Starts or stops the input path an instance uses as its first holder arrives or the last leaves */
static
HRESULT
DiEmSwitchInput(
    _In_ DI_EM_INSTANCE* Instance,
    _In_ BOOLEAN On)
{
    DI_EM_CLASS* Class = Instance->Class;
    HRESULT Result = S_OK;

    if (Class->Keyboard)
    {
        DiEmBlockKeys = On && (Instance->Flags & DI_EM_EXCLUSIVE);
        DiEmBlockWinKeys = On && !(Instance->Flags & DI_EM_EXCLUSIVE) && (Instance->Flags & DI_EM_NOWINKEY);
    }

    if (Instance->Flags & DI_EM_KEYBOARD_HOOK)
    {
        EnterCriticalSection(&DiLock);
        if (!On)
        {
            if (DiEmKeyboardHook != nullptr)
                UnhookWindowsHookEx(DiEmKeyboardHook);
            DiEmKeyboardHook = nullptr;
        }
        else if (Instance->Window != nullptr && DiEmKeyboardHook == nullptr)
        {
            DiEmKeyboardHook = SetWindowsHookExW(WH_KEYBOARD, DiEmKeyboardProc, DiInstance,
                                                 GetWindowThreadProcessId(Instance->Window, nullptr));
        }
        else
        {
            Result = E_FAIL;
        }
        LeaveCriticalSection(&DiLock);
        return Result;
    }

    Result = DiEmHoldThread(Instance);
    if (FAILED(Result))
        return Result;
    if (On)
    {
        InterlockedIncrement(&Class->Acquired);
        if (Instance->Flags & DI_EM_EXCLUSIVE)
            InterlockedIncrement(&Class->Exclusive);
    }
    else
    {
        if (Instance->Flags & DI_EM_EXCLUSIVE)
            InterlockedDecrement(&Class->Exclusive);
        InterlockedDecrement(&Class->Acquired);
    }

    if (DiEmUseRawInput && DiEmWindow != nullptr)
    {
        if (!On)
        {
            DiEmRegisterRaw(Class->Keyboard, TRUE, 0);
            if (Class->Acquired > 0)
                DiEmRegisterRaw(Class->Keyboard, FALSE, 0);
        }
        else
        {
            DWORD Mode = 0;

            if (Instance->Flags & DI_EM_EXCLUSIVE)
                Mode = DI_EM_EXCLUSIVE;
            else if (Class->Keyboard && (Instance->Flags & DI_EM_NOWINKEY))
                Mode = DI_EM_NOWINKEY;
            if (FAILED(DiEmRegisterRaw(Class->Keyboard, FALSE, Mode)))
            {
                DiEmUseRawInput = FALSE;
                Result = S_FALSE;
            }
        }
    }
    DiEmWakeThread();
    return Result;
}

HRESULT
DiEmAcquire(
    _In_ DI_EM_INSTANCE* Instance)
{
    HRESULT Result;

    Instance->Flags |= DI_EM_ACQUIRED;
    if (InterlockedIncrement(&Instance->AcquireCount) != 0)
        return S_OK;
    Result = DiEmSwitchInput(Instance, TRUE);
    if (FAILED(Result))
    {
        Instance->Flags &= ~DI_EM_ACQUIRED;
        InterlockedDecrement(&Instance->AcquireCount);
        return Result;
    }
    if (!Instance->Class->Keyboard && (Instance->Flags & DI_EM_EXCLUSIVE) && !Instance->CursorHidden)
    {
        ShowCursor(FALSE);
        Instance->CursorHidden = TRUE;
    }
    return Result;
}

HRESULT
DiEmUnacquire(
    _In_ DI_EM_INSTANCE* Instance)
{
    if (!(Instance->Flags & DI_EM_ACQUIRED))
        return S_OK;
    Instance->Flags &= ~DI_EM_ACQUIRED;
    if (InterlockedDecrement(&Instance->AcquireCount) >= 0)
        return S_OK;
    if (Instance->CursorHidden)
    {
        ShowCursor(TRUE);
        Instance->CursorHidden = FALSE;
    }
    return DiEmSwitchInput(Instance, FALSE);
}

HRESULT
DiEmSetBufferSize(
    _In_ DI_EM_INSTANCE* Instance,
    _In_ DWORD Count)
{
    DI_EM_EVENT* Buffer = nullptr;

    if (Count != 0)
    {
        Buffer = static_cast<DI_EM_EVENT*>(calloc(static_cast<SIZE_T>(Count) + 1, sizeof(*Buffer)));
        if (Buffer == nullptr)
            return E_OUTOFMEMORY;
    }
    EnterCriticalSection(&Instance->Lock);
    free(Instance->Buffer);
    Instance->Buffer = Buffer;
    Instance->Capacity = Count;
    Instance->Head = 0;
    Instance->Tail = 0;
    LeaveCriticalSection(&Instance->Lock);
    return S_OK;
}

HRESULT
DiEmSetObjectMap(
    _In_ DI_EM_INSTANCE* Instance,
    _In_reads_(Count) const LONG* Map,
    _In_ DWORD Count)
{
    LONG* Copy = nullptr;

    if (Count != 0)
    {
        Copy = static_cast<LONG*>(malloc(Count * sizeof(*Copy)));
        if (Copy == nullptr)
            return E_OUTOFMEMORY;
        CopyMemory(Copy, Map, Count * sizeof(*Copy));
    }
    EnterCriticalSection(&Instance->Lock);
    free(Instance->ObjectMap);
    Instance->ObjectMap = Copy;
    Instance->ObjectMapSize = Count;
    LeaveCriticalSection(&Instance->Lock);
    return S_OK;
}

VOID
DiEmSetEvent(
    _In_ DI_EM_INSTANCE* Instance,
    _In_opt_ HANDLE Event)
{
    EnterCriticalSection(&DiLock);
    Instance->Event = Event;
    LeaveCriticalSection(&DiLock);
}
