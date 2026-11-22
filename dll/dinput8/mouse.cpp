/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The system mouse device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8.hpp"

/* The mouse layout: three axes, then up to eight buttons */
#define DI_MOUSE_DATA_SIZE          20
#define DI_MOUSE_AXES               3
#define DI_MOUSE_BUTTONS            8

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
    DWORD Emulation;

    const DI_FORMAT*
    Format(VOID) override
    {
        return &NativeFormat;
    }

    BOOLEAN
    Emulated(VOID) override
    {
        return TRUE;
    }

    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) override
    {
        Caps->dwFlags = Emulation != 0 ? DIDC_ATTACHED | DIDC_ALIAS : DIDC_ATTACHED;
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
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override
    {
        UNREFERENCED_PARAMETER(Window);
        UNREFERENCED_PARAMETER(Flags);
        return DiLaunchControlPanel(L"mouse");
    }

    VOID
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
            Emulation = DI_EMULATE_MOUSE;
        else if (IsEqualGUID(Guid, GUID_SysMouseEm2))
            Emulation = DI_EMULATE_MOUSE_HOOK;
        else
            Emulation = 0;

        for (DWORD Index = 0; Index < Axes; Index++)
            Objects[Index] = DiMouseObjects[Index];
        for (DWORD Index = 0; Index < Buttons; Index++)
            Objects[Axes + Index] = DiMouseObjects[DI_MOUSE_AXES + Index];
        NativeFormat.DataSize = DI_MOUSE_DATA_SIZE;
        NativeFormat.ObjectCount = Axes + Buttons;
        NativeFormat.Objects = Objects;
    }
};

HRESULT
DiCreateMouse(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend)
{
    DiMouse* Mouse;

    *Backend = nullptr;
    if (!GetSystemMetrics(SM_MOUSEPRESENT))
        return DIERR_DEVICENOTREG;
    Mouse = new (std::nothrow) DiMouse();
    if (Mouse == nullptr)
        return E_OUTOFMEMORY;
    Mouse->Build(Guid);
    *Backend = Mouse;
    return S_OK;
}
