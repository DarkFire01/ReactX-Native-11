/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The system keyboard and mouse backends
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"
#include "hid.hpp"

#include <strsafe.h>

#define DI_KEY_COUNT        256
#define DI_MOUSE_OBJECTS    11
#define DI_MOUSE_DATA_SIZE  20
#define DI_MOUSE_DEVTYPE    MAKEWORD(DIDEVTYPE_MOUSE, DIDEVTYPEMOUSE_UNKNOWN)
#define DI_WHEEL_DELTA      120

/* Which keyboard layouts carry a scan code, bit 4 being every layout */
static const BYTE DiKeyLayouts[DI_KEY_COUNT] =
{
     0, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,  5, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13,  5,  5, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 61, 13, 13, 13, 13, 13,
    13, 13, 13, 13, 13,  5,  5, 13, 13, 13, 13, 13, 13, 13, 13, 13,
    13, 13, 13, 13,  0,  0,  4, 12, 12,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  8,  8,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    56,  0,  0,  4,  0,  0,  0,  0,  0,  8,  0,  8,  0,  8,  4,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  8,  0,  0,
    12,  8,  8,  8, 40,  8,  8,  8,  0,  4,  0,  0,  4,  4,  0,  0,
     4,  4,  4,  0,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  4,  0,
     4,  0,  4,  8,  0, 12,  0, 13,  4,  0,  0,  0,  0,  0,  0,  0,
     0,  0,  0,  0,  0,  4,  0, 12, 12, 12,  0, 12,  0, 12,  0, 12,
    12, 12, 12, 12,  0,  0,  0,  0,  0,  0,  0, 12, 12, 12,  4,  4,
     0,  0,  0,  4,  0,  4,  4,  4,  4,  4,  4,  4,  4,  4,  0,  0,
     0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};

/* Layout bits that make a key a toggle rather than a push button */
#define DI_KEY_TOGGLE_BITS  0x30

/* X, Y and the wheel, then the buttons, the way the mouse state lays them out */
static const DIOBJECTDATAFORMAT DiMouseObjects[DI_MOUSE_OBJECTS] =
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

#define DI_MOUSE_FIRST_BUTTON 3

ULONG
DiGetKeyboardType(
    _In_ INT Which)
{
    RAWINPUTDEVICELIST* List;
    UINT Count = 0;
    ULONG Result = 0;

    /* Prefer what the first real keyboard reports over the session default */
    if (GetRawInputDeviceList(nullptr, &Count, sizeof(*List)) == static_cast<UINT>(-1) || Count == 0)
        return GetKeyboardType(Which);

    List = static_cast<RAWINPUTDEVICELIST*>(malloc(sizeof(*List) * Count));
    if (List == nullptr)
        return GetKeyboardType(Which);

    if (GetRawInputDeviceList(List, &Count, sizeof(*List)) != static_cast<UINT>(-1))
    {
        for (UINT Index = 0; Index < Count; Index++)
        {
            RID_DEVICE_INFO Info = {};
            UINT NameLength = 0;
            UINT Size;

            if (List[Index].dwType != RIM_TYPEKEYBOARD)
                continue;
            if (GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICENAME, nullptr, &NameLength) != 0)
                continue;

            Info.cbSize = sizeof(Info);
            Size = sizeof(Info);
            if (GetRawInputDeviceInfoW(List[Index].hDevice, RIDI_DEVICEINFO, &Info, &Size) == static_cast<UINT>(-1))
                continue;

            if (Which <= 1 && (Info.keyboard.dwType == 4 || Info.keyboard.dwType == 7 || Info.keyboard.dwType == 8))
            {
                Result = (Which != 0) ? Info.keyboard.dwSubType : Info.keyboard.dwType;
                break;
            }
        }
    }

    free(List);
    return (Result != 0) ? Result : GetKeyboardType(Which);
}

class RxDiKeyboard final : public RxDiBackend
{
public:
    HRESULT
    Init(
        _In_ REFGUID Instance)
    {
        RX_DI_SINK_PARAMS Params = {};
        const BYTE* Xlat = nullptr;
        BOOLEAN Seen[DI_KEY_COUNT] = {};
        ULONG Layouts;

        Format.dwSize = sizeof(Format);
        Format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
        Format.dwFlags = 0;
        Format.dwDataSize = DI_KEY_COUNT;
        Format.dwNumObjs = 0;
        Format.rgodf = Objects;

        Type = DiGetKeyboardType(0);

        /* Every scan code but the two fake shift codes, which the emulation layer uses to size its state */
        for (ULONG Scan = 0; Scan < DI_KEY_COUNT; Scan++)
        {
            if (Scan == 0xAA || Scan == 0xB6)
                continue;
            Objects[Scan].pguid = &GUID_Key;
            Objects[Scan].dwOfs = Scan;
            Objects[Scan].dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(Scan);
        }

        Params.StateSize = DI_KEY_COUNT;
        Params.ObjectCount = DI_KEY_COUNT;
        Params.Objects = Objects;

        if (Type != 7)
        {
            Layouts = (Type == 1) ? 1 : (Type == 3) ? 2 : 4;
        }
        else
        {
            ULONG SubType = DiGetKeyboardType(1);
            ULONG Table;

            /* Japanese keyboards need a scan code table picked by vendor and model */
            if (HIBYTE(SubType) == 0x0D)
            {
                ULONG Plain = 1;
                ULONG Shifted = 7;

                switch (LOBYTE(SubType))
                {
                    case 4:
                        Type = 8;
                        break;
                    case 5:
                        Plain = 2;
                        Type = 9;
                        Shifted = 8;
                        break;
                    default:
                        Type = 7;
                        break;
                }
                Table = (MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC) == 1) ? Shifted : Plain;
            }
            else
            {
                switch (SubType)
                {
                    case 0:
                        Type = 4;
                        Table = 0;
                        break;
                    case 1:
                        Table = 4;
                        Type = 11;
                        break;
                    case 13:
                    case 14:
                    case 15:
                        Table = 5;
                        Type = 12;
                        break;
                    default:
                        Table = 3;
                        Type = 10;
                        break;
                }
            }

            Layouts = 4;
            if (Table != 0)
            {
                Layouts = 0x2F;
                Xlat = static_cast<const BYTE*>(DiFindTable(Table));
                if (Xlat == nullptr)
                {
                    DiTrace(L"scan code table %lu missing", Table);
                    return E_FAIL;
                }
            }
        }
        Params.Extra = reinterpret_cast<ULONG_PTR>(Xlat);

        if (IsEqualGUID(Instance, GUID_SysKeyboardEm))
            Variant = DI_EMULATE_LL_KEYBOARD;
        else if (IsEqualGUID(Instance, GUID_SysKeyboardEm2))
            Variant = DI_EMULATE_HOOK_KBD;

        Params.Mode = (Variant | DiEmulation) & (DI_EMULATE_LL_KEYBOARD | DI_EMULATE_HOOK_KBD);
        if (FAILED(DiOpenKeyboardSink(&Params, &Sink)))
            return E_FAIL;

        /* The object list holds each key this layout has, once */
        for (ULONG Index = 0; Index < DI_KEY_COUNT; Index++)
        {
            ULONG Scan = (Xlat != nullptr) ? Xlat[Index] : Index;
            ULONG Has = DiKeyLayouts[Scan] & Layouts;
            DIOBJECTDATAFORMAT* Object;

            if (Has == 0 || Seen[Scan])
                continue;

            Seen[Scan] = TRUE;
            Object = &Objects[Format.dwNumObjs++];
            Object->pguid = &GUID_Key;
            Object->dwOfs = Scan;
            Object->dwType = ((Has & DI_KEY_TOGGLE_BITS) ? DIDFT_TGLBUTTON : DIDFT_PSHBUTTON) | DIDFT_MAKEINSTANCE(Scan);
        }

        State = Sink->State;
        return S_OK;
    }

    ~RxDiKeyboard() override
    {
        if (Sink != nullptr && (Sink->Flags & DI_SINK_EMULATED))
            DiCloseSink(Sink);
    }

    PRX_DI_SINK GetSink(VOID) override { return Sink; }

    HRESULT
    GetDataFormat(
        _Outptr_ const DIDATAFORMAT** Result) override
    {
        *Result = &Format;
        return S_OK;
    }

    HRESULT
    GetObjectInfo(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) override
    {
        if (Target->Index >= Format.dwNumObjs)
            return E_INVALIDARG;

        LoadStringW(DiModule, IDS_KEY_OBJECT + DIDFT_GETINSTANCE(Target->Type), Info->tszName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = DIDC_ATTACHED;
        Caps->dwDevType = MAKEWORD(DIDEVTYPE_KEYBOARD, Type);
        if (Variant != 0)
            Caps->dwFlags = DIDC_ATTACHED | DIDC_ALIAS;
        Caps->dwButtons = Format.dwNumObjs;
        return S_OK;
    }

    HRESULT
    Acquire(VOID) override
    {
        ULONG Toggles = 0;

        /* Japanese keyboards start with their toggle keys where the system has them */
        if (DiGetKeyboardType(0) == 7)
        {
            Toggles = (GetAsyncKeyState(VK_KANA) & 0x8000) ? 0x11 : 0x10;
            if (GetAsyncKeyState(VK_CAPITAL) & 0x8000)
                Toggles |= 2;
            if (GetAsyncKeyState(VK_KANJI) & 0x8000)
                Toggles |= 8;
        }

        if (Sink->Flags & DI_SINK_EXCLUSIVE)
            Toggles |= 4;

        if (Sink->Flags & DI_SINK_EMULATED)
            DiPrimeKeyboard(Toggles);

        /* Keys held when exclusive mode starts are released for the rest of the system */
        if (Sink->Flags & DI_SINK_EXCLUSIVE)
        {
            for (ULONG Key = 8; Key < 0xFE; Key++)
            {
                if (Key == VK_CAPITAL || Key == VK_KANA || Key == VK_KANJI)
                    continue;
                if (GetAsyncKeyState(Key) & 0x8000)
                    keybd_event(static_cast<BYTE>(Key), 0, KEYEVENTF_KEYUP, 0);
            }
        }

        return S_FALSE;
    }

    HRESULT
    GetDeviceState(
        _Out_ PVOID Target) override
    {
        if ((Sink->Flags & DI_SINK_ACQUIRED) == 0)
            return DIERR_INPUTLOST;

        CopyMemory(Target, State, DI_KEY_COUNT);
        return S_OK;
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override
    {
        Info->guidProduct = GUID_SysKeyboard;
        Info->dwDevType = MAKEWORD(DIDEVTYPE_KEYBOARD, Type);
        LoadStringW(DiModule, IDS_KEYBOARD, Info->tszProductName, MAX_PATH);
        LoadStringW(DiModule, IDS_KEYBOARD, Info->tszInstanceName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        if ((Sink->Flags & DI_SINK_HOOK_KEYBOARD) == 0)
        {
            if (Flags & DISCL_EXCLUSIVE)
            {
                /* Exclusive keyboards only make sense in the foreground */
                if ((Flags & DISCL_FOREGROUND) == 0)
                    return E_NOTIMPL;
                Sink->Flags = (Sink->Flags | DI_SINK_EXCLUSIVE) & ~DI_SINK_NOWINKEY;
                return S_OK;
            }
            Sink->Flags &= ~DI_SINK_EXCLUSIVE;
        }
        else if (Flags & DISCL_BACKGROUND)
        {
            return E_NOTIMPL;
        }

        Sink->Flags &= ~DI_SINK_NOWINKEY;
        if (Flags & DISCL_NOWINKEY)
        {
            if ((Flags & DISCL_FOREGROUND) == 0)
                return E_NOTIMPL;
            Sink->Flags |= DI_SINK_NOWINKEY;
        }
        return S_OK;
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Owner,
        _In_ DWORD Flags) override
    {
        return DiRunControlPanel(L"keyboard");
    }

    ULONG GetUsage(_In_ ULONG Index) override { return 0; }

private:
    PRX_DI_SINK Sink = nullptr;
    PBYTE State = nullptr;
    ULONG Type = 0;
    ULONG Variant = 0;
    DIDATAFORMAT Format = {};
    DIOBJECTDATAFORMAT Objects[DI_KEY_COUNT] = {};
};

class RxDiMouse final : public RxDiBackend
{
public:
    HRESULT
    Init(
        _In_ REFGUID Instance)
    {
        RX_DI_SINK_PARAMS Params = {};
        ULONG Buttons;

        Format.dwSize = sizeof(Format);
        Format.dwObjSize = sizeof(DIOBJECTDATAFORMAT);
        Format.dwDataSize = DI_MOUSE_DATA_SIZE;
        Format.rgodf = Objects;

        /* A wheel is assumed when the system knows one or any HID mouse reports one */
        Axes = GetSystemMetrics(SM_MOUSEWHEELPRESENT) ? 3 : DiHidMouseAxes();
        Params.Extra = (Axes < 3) ? 3 : Axes;
        AddObjects(&DiMouseObjects[0], Axes);

        Params.StateSize = DI_MOUSE_DATA_SIZE;
        Params.ObjectCount = DI_MOUSE_OBJECTS;
        Params.Objects = DiMouseObjects;

        if (IsEqualGUID(Instance, GUID_SysMouseEm))
            Variant = DI_EMULATE_LL_MOUSE;
        else if (IsEqualGUID(Instance, GUID_SysMouseEm2))
            Variant = DI_EMULATE_SUBCLASS;

        Params.Mode = (Variant | DiEmulation) & DI_EMULATE_LL_MOUSE;
        if (FAILED(DiOpenMouseSink(&Params, &Sink)))
            return E_FAIL;

        State = Sink->State;

        Buttons = GetSystemMetrics(SM_CMOUSEBUTTONS);
        if (Buttons >= 8)
            Buttons = 8;
        else if (Axes == 3 && Buttons < 3)
            Buttons = 3;
        ButtonCount = Buttons;
        AddObjects(&DiMouseObjects[DI_MOUSE_FIRST_BUTTON], Buttons);
        return S_OK;
    }

    ~RxDiMouse() override
    {
        if (Sink != nullptr && (Sink->Flags & DI_SINK_EMULATED))
            DiCloseSink(Sink);
    }

    PRX_DI_SINK GetSink(VOID) override { return Sink; }

    HRESULT
    GetDataFormat(
        _Outptr_ const DIDATAFORMAT** Result) override
    {
        *Result = &Format;
        return S_OK;
    }

    HRESULT
    GetObjectInfo(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) override
    {
        if (Target->Index >= Format.dwNumObjs)
            return E_INVALIDARG;

        LoadStringW(DiModule, IDS_MOUSE_OBJECT + DIDFT_GETINSTANCE(Target->Type), Info->tszName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = DIDC_ATTACHED;
        Caps->dwDevType = DI_MOUSE_DEVTYPE;
        if (Variant != 0)
            Caps->dwFlags = DIDC_ATTACHED | DIDC_ALIAS;
        Caps->dwAxes = Axes;
        Caps->dwButtons = ButtonCount;
        return S_OK;
    }

    HRESULT
    Acquire(VOID) override
    {
        ULONG Pressed = 0;
        DWORD Releases = 0;

        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        {
            Pressed |= 0x80;
            Releases |= MOUSEEVENTF_LEFTUP;
        }
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000)
        {
            Pressed |= 0x8000;
            Releases |= MOUSEEVENTF_RIGHTUP;
        }
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000)
        {
            Pressed |= 0x800000;
            Releases |= MOUSEEVENTF_MIDDLEUP;
        }

        /* Exclusive mode parks the cursor in the middle of the window and hides it */
        if (Sink->Flags & DI_SINK_EXCLUSIVE)
        {
            RECT Rect = {};

            GetCursorPos(&Saved);
            GetWindowRect(Window, &Rect);
            SetCursorPos((Rect.right + Rect.left) >> 1, (Rect.bottom + Rect.top) >> 1);
            ShowCursor(FALSE);
            if ((Sink->Flags & DI_SINK_EMULATED) == 0 && Releases != 0)
                mouse_event(Releases, 0, 0, 0, 0);
        }

        if (Sink->Flags & DI_SINK_EMULATED)
            DiSeedMouse(Pressed);

        return S_FALSE;
    }

    HRESULT
    Unacquire(VOID) override
    {
        if (Sink->Flags & DI_SINK_EXCLUSIVE)
        {
            RECT Desktop = {};
            RECT Foreground = {};

            /* Put the cursor back unless the foreground window covers the whole desktop */
            GetWindowRect(GetDesktopWindow(), &Desktop);
            GetWindowRect(GetForegroundWindow(), &Foreground);
            SubtractRect(&Desktop, &Desktop, &Foreground);
            if (!IsRectEmpty(&Desktop))
                SetCursorPos(Saved.x, Saved.y);
            ShowCursor(TRUE);
        }
        return S_FALSE;
    }

    HRESULT
    GetDeviceState(
        _Out_ PVOID Target) override
    {
        if ((Sink->Flags & DI_SINK_ACQUIRED) == 0)
            return DIERR_INPUTLOST;

        CopyMemory(Target, State, DI_MOUSE_DATA_SIZE);
        return S_OK;
    }

    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) override
    {
        Info->guidProduct = GUID_SysMouse;
        Info->dwDevType = DI_MOUSE_DEVTYPE;
        LoadStringW(DiModule, IDS_MOUSE, Info->tszProductName, MAX_PATH);
        LoadStringW(DiModule, IDS_MOUSE, Info->tszInstanceName, MAX_PATH);
        return S_OK;
    }

    HRESULT
    GetProperty(
        _In_ const RX_DI_PROP_TARGET* Target,
        _Inout_ LPDIPROPHEADER Header) override
    {
        /* Only the wheel has a granularity of its own */
        if (Target->Property != 3 || Target->Type != (DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(2)))
            return E_NOTIMPL;

        reinterpret_cast<LPDIPROPDWORD>(Header)->dwData = DI_WHEEL_DELTA;
        return S_OK;
    }

    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND NewWindow,
        _In_ DWORD Flags) override
    {
        if (Sink->Flags & DI_SINK_SUBCLASS_MOUSE)
        {
            if ((Flags & (DISCL_EXCLUSIVE | DISCL_FOREGROUND)) != (DISCL_EXCLUSIVE | DISCL_FOREGROUND))
                return E_NOTIMPL;
            Sink->Flags |= DI_SINK_EXCLUSIVE;
        }
        else if (Flags & DISCL_EXCLUSIVE)
        {
            if ((Flags & DISCL_FOREGROUND) == 0)
                return E_NOTIMPL;
            Sink->Flags |= DI_SINK_EXCLUSIVE;
        }
        else
        {
            Sink->Flags &= ~DI_SINK_EXCLUSIVE;
        }

        Window = NewWindow;
        return S_OK;
    }

    HRESULT
    RunControlPanel(
        _In_opt_ HWND Owner,
        _In_ DWORD Flags) override
    {
        return DiRunControlPanel(L"mouse");
    }

    ULONG GetUsage(_In_ ULONG Index) override { return 0; }

private:
    VOID
    AddObjects(
        _In_reads_(Count) const DIOBJECTDATAFORMAT* Source,
        _In_ ULONG Count)
    {
        for (ULONG Index = 0; Index < Count; Index++)
            Objects[Format.dwNumObjs++] = Source[Index];
    }

    PRX_DI_SINK Sink = nullptr;
    PBYTE State = nullptr;
    HWND Window = nullptr;
    POINT Saved = {};
    ULONG Axes = 0;
    ULONG ButtonCount = 0;
    ULONG Variant = 0;
    DIDATAFORMAT Format = {};
    DIOBJECTDATAFORMAT Objects[DI_MOUSE_OBJECTS] = {};
};

HRESULT
DiCreateKeyboard(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend)
{
    RxDiKeyboard* Created = new RxDiKeyboard();
    HRESULT hr;

    *Backend = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Init(Instance);
    if (FAILED(hr))
    {
        delete Created;
        return hr;
    }

    *Backend = Created;
    return S_OK;
}

HRESULT
DiCreateMouse(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend)
{
    RxDiMouse* Created;
    HRESULT hr;

    *Backend = nullptr;
    if (!GetSystemMetrics(SM_MOUSEPRESENT))
        return DIERR_DEVICENOTREG;

    Created = new RxDiMouse();
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Init(Instance);
    if (FAILED(hr))
    {
        delete Created;
        return hr;
    }

    *Backend = Created;
    return S_OK;
}

const RX_DI_STATIC_DEVICE DiStaticDevices[6] =
{
    { &GUID_SysMouse, DIDEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysMouseEm, DIDEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysMouseEm2, DIDEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysKeyboard, DIDEVTYPE_KEYBOARD, DiCreateKeyboard },
    { &GUID_SysKeyboardEm, DIDEVTYPE_KEYBOARD, DiCreateKeyboard },
    { &GUID_SysKeyboardEm2, DIDEVTYPE_KEYBOARD, DiCreateKeyboard },
};

HRESULT
DiFindBackend(
    _In_ REFGUID Instance,
    _Out_ PRX_DI_BACKEND_CREATE* Create)
{
    for (ULONG Index = 0; Index < _countof(DiStaticDevices); Index++)
    {
        if (IsEqualGUID(Instance, *DiStaticDevices[Index].Instance))
        {
            *Create = DiStaticDevices[Index].Create;
            return S_OK;
        }
    }

    return DiFindHidBackend(Instance, Create);
}
