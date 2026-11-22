/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Action mapping on the system keyboard and mouse: building, setting and enumerating by semantics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

static const GUID DcMapGuid = { 0x5A5A0001, 0x1111, 0x2222, { 0x33, 0x33, 0x44, 0x44, 0x55, 0x55, 0x66, 0x66 } };

static const DWORD DcSemantics[] =
{
    DIAXIS_DRIVINGR_STEER, DIAXIS_DRIVINGR_ACCELERATE, DIBUTTON_DRIVINGR_SHIFTUP, DIBUTTON_DRIVINGR_SHIFTDOWN,
    DIKEYBOARD_SPACE, DIKEYBOARD_A, DIKEYBOARD_ESCAPE, DIMOUSE_XAXIS, DIMOUSE_YAXIS, DIMOUSE_WHEEL, DIMOUSE_BUTTON0,
    DIMOUSE_BUTTON1, DIBUTTON_DRIVINGR_MENU, DIBUTTON_DRIVINGR_BRAKE, DIAXIS_DRIVINGR_BRAKE,
};

static DIACTIONW DcActions[ARRAYSIZE(DcSemantics)];

static
VOID
DcResetActions(
    _Out_ DIACTIONFORMATW* Format)
{
    ZeroMemory(Format, sizeof(*Format));
    ZeroMemory(DcActions, sizeof(DcActions));
    for (UINT Index = 0; Index < ARRAYSIZE(DcSemantics); Index++)
    {
        DcActions[Index].uAppData = Index;
        DcActions[Index].dwSemantic = DcSemantics[Index];
        DcActions[Index].lptszActionName = L"action";
    }
    Format->dwSize = sizeof(*Format);
    Format->dwActionSize = sizeof(DIACTIONW);
    Format->dwDataSize = ARRAYSIZE(DcSemantics) * 4;
    Format->dwNumActions = ARRAYSIZE(DcSemantics);
    Format->rgoAction = DcActions;
    Format->guidActionMap = DcMapGuid;
    Format->dwGenre = DIVIRTUAL_DRIVING_RACE;
    Format->dwBufferSize = 16;
    Format->lAxisMin = -100;
    Format->lAxisMax = 100;
    wcscpy_s(Format->tszActionMap, L"dinput8check");
}

static
VOID
DcPrintActions(
    _In_ const DIACTIONFORMATW* Format)
{
    printf("    crc %08lX time %08lX %08lX\n", Format->dwCRC, Format->ftTimeStamp.dwHighDateTime,
           Format->ftTimeStamp.dwLowDateTime);
    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        const DIACTIONW* Action = &Format->rgoAction[Index];

        printf("    action %2lu sem %08lX flags %08lX inst %s obj %08lX how %08lX\n", Index, Action->dwSemantic,
               Action->dwFlags, DcGuid(&Action->guidInstance).c_str(), Action->dwObjID, Action->dwHow);
    }
}

static
BOOL
CALLBACK
DcSemanticsCallback(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ LPDIRECTINPUTDEVICE8W Device,
    _In_ DWORD Flags,
    _In_ DWORD Remaining,
    _In_ VOID* Context)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Context);
    printf("    device %s type %08lX flags %08lX remaining %lu\n", DcGuid(&Instance->guidInstance).c_str(),
           Instance->dwDevType, Flags, Remaining);
    return DIENUM_CONTINUE;
}

VOID
DcCheckActions(VOID)
{
    static const GUID* Devices[] = { &GUID_SysKeyboard, &GUID_SysMouse };
    IDirectInput8W* Wide = DcCreateWide();
    DIACTIONFORMATW Format;

    if (Wide == nullptr)
        return;
    for (const GUID* Guid : Devices)
    {
        IDirectInputDevice8W* Device = nullptr;

        printf("device %s\n", DcGuid(Guid).c_str());
        if (FAILED(Wide->CreateDevice(*Guid, &Device, nullptr)))
            continue;
        for (DWORD Flags : { 0ul, 1ul, 2ul, 4ul, 3ul, 8ul })
        {
            DcResetActions(&Format);
            printf("  build flags %lu = %08lX\n", Flags, Device->BuildActionMap(&Format, L"dc user", Flags));
            DcPrintActions(&Format);
        }
        DcResetActions(&Format);
        Format.dwSize = 336;
        printf("  build bad size = %08lX\n", Device->BuildActionMap(&Format, nullptr, 0));
        DcResetActions(&Format);
        Format.dwDataSize = 4;
        printf("  build bad data size = %08lX\n", Device->BuildActionMap(&Format, nullptr, 0));
        DcResetActions(&Format);
        Format.dwGenre = 0;
        printf("  build no genre = %08lX\n", Device->BuildActionMap(&Format, nullptr, 0));
        printf("  build null = %08lX\n", Device->BuildActionMap(nullptr, nullptr, 0));

        DcResetActions(&Format);
        printf("  build default = %08lX\n", Device->BuildActionMap(&Format, nullptr, 0));
        for (DWORD Flags : { 0ul, 1ul, 2ul, 8ul })
            printf("  set flags %lu = %08lX\n", Flags, Device->SetActionMap(&Format, nullptr, Flags));
        {
            DIPROPDWORD Buffer = { { sizeof(Buffer), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
            DIPROPSTRING User = { { sizeof(User), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
            BYTE State[256];
            HRESULT Result = Device->GetProperty(DIPROP_BUFFERSIZE, &Buffer.diph);

            printf("  after set buffer = %08lX %lu", Result, Buffer.dwData);
            Result = Device->GetProperty(DIPROP_USERNAME, &User.diph);
            printf(" user %08lX '%s'", Result, DcWide(User.wsz).c_str());
            Device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            printf(" acquire %08lX", Device->Acquire());
            printf(" state %08lX\n", Device->GetDeviceState(Format.dwDataSize, State));
            Device->Unacquire();
        }
        {
            DIDEVICEIMAGEINFOHEADERW Header = { sizeof(Header), sizeof(DIDEVICEIMAGEINFOW) };

            printf("  image info = %08lX\n", Device->GetImageInfo(&Header));
        }
        Device->Release();
    }

    for (DWORD Flags : { 0ul, 0x10ul, 0x20ul, 0x100ul })
    {
        DcResetActions(&Format);
        printf("enum semantics flags %#lx = %08lX\n", Flags,
               Wide->EnumDevicesBySemantics(L"dc user", &Format, DcSemanticsCallback, nullptr, Flags));
    }
    Wide->Release();
}
