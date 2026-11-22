/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The system mouse device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "input.hpp"
#include "actionmap.hpp"

/* The mouse layout: three axes, then up to eight buttons */
#define DI_MOUSE_DATA_SIZE          20
#define DI_MOUSE_AXES               3
#define DI_MOUSE_BUTTONS            8

/* Wheel detents report this many units each */
#define DI_WHEEL_GRANULARITY        WHEEL_DELTA

static const DI_OBJECT DiMouseObjects[DI_MOUSE_AXES + DI_MOUSE_BUTTONS] =
{
    { &GUID_XAxis, 0, DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(0), DIDOI_ASPECTPOSITION },
    { &GUID_YAxis, 4, DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(1), DIDOI_ASPECTPOSITION },
    { &GUID_ZAxis, 8, DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(2), DIDOI_ASPECTPOSITION },
    { &GUID_Button, 12, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(3), 0 },
    { &GUID_Button, 13, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(4), 0 },
    { &GUID_Button, 14, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(5), 0 },
    { &GUID_Button, 15, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(6), 0 },
    { &GUID_Button, 16, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(7), 0 },
    { &GUID_Button, 17, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(8), 0 },
    { &GUID_Button, 18, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(9), 0 },
    { &GUID_Button, 19, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(10), 0 },
};

/* Axes the mouse reports: a wheel makes three, otherwise whatever the widest HID mouse has */
static
DWORD
DiMouseAxisCount(VOID)
{
    if (GetSystemMetrics(SM_MOUSEWHEELPRESENT))
        return 3;
    return 2;
}

class DiMouse : public DiBackend
{
public:
    DI_FORMAT NativeFormat;
    DI_OBJECT Objects[DI_MOUSE_AXES + DI_MOUSE_BUTTONS];
    DWORD Axes;
    DWORD Buttons;
    DWORD Level;
    DI_EM_INSTANCE* Instance = nullptr;
    HWND Window = nullptr;
    POINT SavedCursor = {};

    ~DiMouse() override
    {
        if (Instance != nullptr)
            DiEmRelease(Instance);
    }

    const DI_FORMAT*
    Format(VOID) override
    {
        return &NativeFormat;
    }

    DI_EM_INSTANCE*
    Emulation(VOID) override
    {
        return Instance;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = Level != 0 ? DIDC_ATTACHED | DIDC_ALIAS : DIDC_ATTACHED;
        Caps->dwDevType = (DI8DEVTYPEMOUSE_UNKNOWN << 8) | DI8DEVTYPE_MOUSE;
        Caps->dwAxes = Axes;
        Caps->dwButtons = Buttons;
        return S_OK;
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override
    {
        Info->guidProduct = GUID_SysMouse;
        Info->dwDevType = (DI8DEVTYPEMOUSE_UNKNOWN << 8) | DI8DEVTYPE_MOUSE;
        LoadStringW(DiInstance, IDS_MOUSE, Info->tszProductName, MAX_PATH);
        LoadStringW(DiInstance, IDS_MOUSE, Info->tszInstanceName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    GetObjectInfo(
        _In_ DWORD Index,
        _In_ DWORD Type,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) override
    {
        if (Index >= NativeFormat.ObjectCount)
            return E_INVALIDARG;
        LoadStringW(DiInstance, IDS_MOUSE_OBJECT + DIDFT_GETINSTANCE(Type), Info->tszName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    GetProperty(
        _In_ const DI_PROPERTY* Property,
        _Inout_ LPDIPROPHEADER Header) override
    {
        if (Property->Id != 3 || Property->Type != (DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(2)))
            return E_NOTIMPL;
        reinterpret_cast<LPDIPROPDWORD>(Header)->dwData = DI_WHEEL_GRANULARITY;
        return DI_OK;
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND NewWindow,
        _In_ DWORD Flags) override
    {
        if (Instance->Flags & DI_EM_MOUSE_SUBCLASS)
        {
            if ((Flags & (DISCL_EXCLUSIVE | DISCL_FOREGROUND)) != (DISCL_EXCLUSIVE | DISCL_FOREGROUND))
                return E_NOTIMPL;
            Instance->Flags |= DI_EM_EXCLUSIVE;
        }
        else if (!(Flags & DISCL_EXCLUSIVE))
        {
            Instance->Flags &= ~DI_EM_EXCLUSIVE;
        }
        else if (Flags & DISCL_FOREGROUND)
        {
            Instance->Flags |= DI_EM_EXCLUSIVE;
        }
        else
        {
            return E_NOTIMPL;
        }
        Window = NewWindow;
        return DI_OK;
    }

    /* The buttons start out as they are now; an exclusive mouse parks the cursor in its window */
    HRESULT
    Acquire(VOID) override
    {
        BYTE Seed[3];

        Seed[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 0x80 : 0;
        Seed[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 0x80 : 0;
        Seed[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) ? 0x80 : 0;
        if (Instance->Flags & DI_EM_EXCLUSIVE)
        {
            RECT Frame = {};

            GetCursorPos(&SavedCursor);
            GetWindowRect(Window, &Frame);
            SetCursorPos((Frame.left + Frame.right) >> 1, (Frame.top + Frame.bottom) >> 1);
            ShowCursor(FALSE);
        }
        DiEmSeedMouseButtons(Seed);
        return S_FALSE;
    }

    /* The cursor goes back where it was unless the window in front covers the whole desktop */
    HRESULT
    Unacquire(VOID) override
    {
        if (Instance->Flags & DI_EM_EXCLUSIVE)
        {
            RECT Desktop = {};
            RECT Front = {};

            GetWindowRect(GetDesktopWindow(), &Desktop);
            GetWindowRect(GetForegroundWindow(), &Front);
            SubtractRect(&Desktop, &Desktop, &Front);
            if (!IsRectEmpty(&Desktop))
                SetCursorPos(SavedCursor.x, SavedCursor.y);
            ShowCursor(TRUE);
        }
        return S_FALSE;
    }

    HRESULT
    GetDeviceState(
        _Out_ VOID* State) override
    {
        if (!(Instance->Flags & DI_EM_ACQUIRED))
            return DIERR_INPUTLOST;
        EnterCriticalSection(&DiLock);
        CopyMemory(State, DiMouseClass.State, DI_MOUSE_DATA_SIZE);
        LeaveCriticalSection(&DiLock);
        return DI_OK;
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Owner,
        _In_ DWORD Flags) override
    {
        UNREFERENCED_PARAMETER(Owner);
        UNREFERENCED_PARAMETER(Flags);
        return DiLaunchControlPanel(L"mouse");
    }

    HRESULT
    BuildDefaultActionMap(
        _Inout_ LPDIACTIONFORMATW Format,
        _In_ DWORD Flags,
        _In_ REFGUID Device) override
    {
        UNREFERENCED_PARAMETER(Flags);
        return DiBuildSystemDefaultMap(Format, DI_SEMANTIC_MOUSE, Device, &NativeFormat, DI_MOUSE_AXES);
    }

    HRESULT
    Build(
        _In_ REFGUID Guid)
    {
        int Reported;

        Axes = DiMouseAxisCount();
        Reported = GetSystemMetrics(SM_CMOUSEBUTTONS);
        if (Reported >= DI_MOUSE_BUTTONS)
            Buttons = DI_MOUSE_BUTTONS;
        else if (Axes == 3 && Reported < 3)
            Buttons = 3;
        else
            Buttons = Reported;

        if (IsEqualGUID(Guid, GUID_SysMouseEm))
            Level = DI_EMULATE_MOUSE;
        else if (IsEqualGUID(Guid, GUID_SysMouseEm2))
            Level = DI_EMULATE_MOUSE_HOOK;
        else
            Level = 0;

        for (DWORD Index = 0; Index < Axes; Index++)
            Objects[Index] = DiMouseObjects[Index];
        for (DWORD Index = 0; Index < Buttons; Index++)
            Objects[Axes + Index] = DiMouseObjects[DI_MOUSE_AXES + Index];
        NativeFormat.DataSize = DI_MOUSE_DATA_SIZE;
        NativeFormat.ObjectCount = Axes + Buttons;
        NativeFormat.Objects = Objects;

        /* Only the first emulation level applies to the mouse; the alias GUIDs still report as aliases */
        return DiEmCreate(&DiMouseClass, &NativeFormat, DI_EM_MOUSE_RAW, &Instance);
    }
};

HRESULT
DiCreateMouse(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend)
{
    DiMouse* Mouse;
    HRESULT Result;

    *Backend = nullptr;
    if (!GetSystemMetrics(SM_MOUSEPRESENT))
        return DIERR_DEVICENOTREG;
    Mouse = new (std::nothrow) DiMouse();
    if (Mouse == nullptr)
        return E_OUTOFMEMORY;
    Result = Mouse->Build(Guid);
    if (FAILED(Result))
    {
        delete Mouse;
        return Result;
    }
    *Backend = Mouse;
    return S_OK;
}
