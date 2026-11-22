/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Action mapping on the system keyboard and mouse: building, setting and enumerating by semantics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

#include <ddraw.h>

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

/* One action of a hand built format */
struct DC_ACTION_SPEC
{
    DWORD Semantic;
    DWORD Flags;
    const GUID* Device;
    DWORD Object;
    DWORD How;
};

#define DC_MAX_SPECS        20

static DIACTIONW DcSpecActions[DC_MAX_SPECS];

#define DC_KEY(Code)        (DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(Code))

static
VOID
DcFillFormat(
    _Out_ DIACTIONFORMATW* Format,
    _In_reads_(Count) const DC_ACTION_SPEC* Specs,
    _In_ DWORD Count,
    _In_ DWORD Genre)
{
    ZeroMemory(Format, sizeof(*Format));
    ZeroMemory(DcSpecActions, sizeof(DcSpecActions));
    for (DWORD Index = 0; Index < Count; Index++)
    {
        DcSpecActions[Index].uAppData = 0x100 + Index;
        DcSpecActions[Index].dwSemantic = Specs[Index].Semantic;
        DcSpecActions[Index].dwFlags = Specs[Index].Flags;
        DcSpecActions[Index].guidInstance = Specs[Index].Device != nullptr ? *Specs[Index].Device : GUID_NULL;
        DcSpecActions[Index].dwObjID = Specs[Index].Object;
        DcSpecActions[Index].dwHow = Specs[Index].How;
    }
    Format->dwSize = sizeof(*Format);
    Format->dwActionSize = sizeof(DIACTIONW);
    Format->dwDataSize = Count * 4;
    Format->dwNumActions = Count;
    Format->rgoAction = DcSpecActions;
    Format->guidActionMap = DcMapGuid;
    Format->dwGenre = Genre;
    Format->dwBufferSize = 8;
    Format->ftTimeStamp.dwLowDateTime = 0x11;
    Format->ftTimeStamp.dwHighDateTime = 0x22;
}

struct DC_MAP_CASE
{
    PCSTR Name;
    DWORD Genre;
    DWORD Count;
    DC_ACTION_SPEC Specs[DC_MAX_SPECS];
};

static const DC_MAP_CASE DcMapCases[] =
{
    { "flight", DIVIRTUAL_FLYING_MILITARY, 6,
      { { DIAXIS_FLYINGM_BANK }, { DIBUTTON_FLYINGM_FIRE }, { DIKEYBOARD_W }, { DIKEYBOARD_S }, { DIMOUSE_XAXIS },
        { DIMOUSE_BUTTON0 } } },
    { "genre only", DIVIRTUAL_FLYING_MILITARY, 2, { { DIAXIS_FLYINGM_BANK }, { DIBUTTON_FLYINGM_FIRE } } },
    { "priority", DIVIRTUAL_DRIVING_RACE, 4,
      { { DIKEYBOARD_A | 0x4000 }, { DIKEYBOARD_B }, { DIMOUSE_BUTTON1 | 0x4000 }, { DIMOUSE_YAXIS } } },
    { "mouse edge", DIVIRTUAL_DRIVING_RACE, 5,
      { { DIMOUSE_BUTTON7 }, { DIMOUSE_BUTTON5 }, { DIMOUSE_XAXISAB }, { DIMOUSE_WHEEL }, { 0x82000414 } } },
    { "duplicate", DIVIRTUAL_DRIVING_RACE, 3, { { DIKEYBOARD_A }, { DIKEYBOARD_Z }, { DIKEYBOARD_A } } },
    { "duplicate fixed", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_A, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x1E) }, { DIKEYBOARD_A } } },
    { "app mapped", DIVIRTUAL_DRIVING_RACE, 5,
      { { DIKEYBOARD_B, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x30) },
        { DIKEYBOARD_C, DIA_APPMAPPED, nullptr, DC_KEY(0x2E) },
        { DIMOUSE_BUTTON2, DIA_APPMAPPED, nullptr, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(5) },
        { DIBUTTON_DRIVINGR_MENU, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x01) },
        { DIKEYBOARD_B } } },
    { "app mapped missing", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_B, DIA_APPMAPPED, &GUID_SysKeyboard, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0) }, { DIKEYBOARD_C } } },
    { "app mapped class", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_B, DIA_APPMAPPED, nullptr, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(0) },
        { DIMOUSE_BUTTON3, DIA_APPMAPPED, nullptr, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(9) } } },
    { "app mapped twice", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_B, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x30) },
        { DIKEYBOARD_C, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x30) } } },
    { "force feedback", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_B, DIA_APPMAPPED | DIA_FORCEFEEDBACK, &GUID_SysKeyboard, DC_KEY(0x30) },
        { DIKEYBOARD_D, DIA_FORCEFEEDBACK } } },
    { "no map", DIVIRTUAL_DRIVING_RACE, 3,
      { { DIKEYBOARD_A, DIA_APPNOMAP }, { DIKEYBOARD_A }, { DIMOUSE_BUTTON0, DIA_APPNOMAP | DIA_NORANGE } } },
    { "bad flags", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_A }, { DIKEYBOARD_B, 0x20 } } },
    { "hows", DIVIRTUAL_DRIVING_RACE, 7,
      { { DIKEYBOARD_1, 0, &GUID_SysKeyboard, DC_KEY(0x02), DIAH_USERCONFIG },
        { DIKEYBOARD_2, 0, &GUID_SysKeyboard, DC_KEY(0x03), DIAH_APPREQUESTED },
        { DIKEYBOARD_3, 0, &GUID_SysKeyboard, DC_KEY(0x04), DIAH_HWAPP },
        { DIKEYBOARD_4, 0, &GUID_SysKeyboard, DC_KEY(0x05), DIAH_HWDEFAULT },
        { DIKEYBOARD_5, 0, &GUID_SysMouse, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(3), 0x10 },
        { DIKEYBOARD_6, 0, &GUID_SysKeyboard, DC_KEY(0x07), DIAH_DEFAULT },
        { DIKEYBOARD_7 } } },
    { "how three", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_1 }, { DIKEYBOARD_2, 0, &GUID_SysKeyboard, DC_KEY(0x03), 3 } } },
    { "how error", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_1 }, { DIKEYBOARD_2, 0, &GUID_SysKeyboard, DC_KEY(0x03), DIAH_ERROR } } },
    { "how no device", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_1 }, { DIKEYBOARD_2, 0, nullptr, DC_KEY(0x03), DIAH_DEFAULT } } },
    { "how wrong object", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_1 }, { DIKEYBOARD_2, 0, &GUID_SysKeyboard, DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(1), DIAH_DEFAULT } } },
    { "semantic 0xFF", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_1 }, { 0x810004FF } } },
    { "semantic high bits", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_1 }, { 0x81080402 } } },
    { "semantic no type", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_1 }, { 0x01000001 } } },
    { "semantic other genre", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_1 }, { DIBUTTON_FLYINGM_FIRE } } },
    { "semantic any", DIVIRTUAL_DRIVING_RACE, 3, { { DIKEYBOARD_1 }, { DIAXIS_ANY_X_1 }, { 0xFF000401 } } },
    { "keyboard missing", DIVIRTUAL_DRIVING_RACE, 2,
      { { DIKEYBOARD_1 }, { 0x81000400, 0, &GUID_SysKeyboard } } },
    { "keyboard unaimed missing", DIVIRTUAL_DRIVING_RACE, 2, { { DIKEYBOARD_1 }, { 0x81000400 } } },
    { "aimed other class", DIVIRTUAL_DRIVING_RACE, 3,
      { { DIMOUSE_BUTTON0, 0, &GUID_SysKeyboard }, { DIKEYBOARD_1, 0, &GUID_SysKeyboard }, { DIKEYBOARD_2 } } },
    { "many", DIVIRTUAL_DRIVING_RACE, 14,
      { { DIKEYBOARD_Q }, { DIAXIS_DRIVINGR_STEER }, { DIMOUSE_BUTTON0 }, { DIKEYBOARD_W }, { DIBUTTON_DRIVINGR_MENU },
        { DIMOUSE_XAXIS }, { DIKEYBOARD_E }, { DIAXIS_ANY_X_1 }, { DIKEYBOARD_R }, { DIMOUSE_YAXIS }, { DIKEYBOARD_T },
        { DIBUTTON_DRIVINGR_SHIFTUP }, { DIKEYBOARD_Y }, { DIMOUSE_WHEEL } } },
    { "many duplicate", DIVIRTUAL_DRIVING_RACE, 14,
      { { DIKEYBOARD_Q }, { DIAXIS_DRIVINGR_STEER }, { DIMOUSE_BUTTON0 }, { DIKEYBOARD_W }, { DIBUTTON_DRIVINGR_MENU },
        { DIMOUSE_XAXIS }, { DIKEYBOARD_E }, { DIAXIS_ANY_X_1 }, { DIKEYBOARD_R }, { DIMOUSE_YAXIS }, { DIKEYBOARD_T },
        { DIBUTTON_DRIVINGR_SHIFTUP }, { DIKEYBOARD_Q }, { DIMOUSE_WHEEL } } },
    { "many fixed", DIVIRTUAL_DRIVING_RACE, 12,
      { { DIKEYBOARD_Q, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x10) }, { DIAXIS_DRIVINGR_STEER },
        { DIMOUSE_BUTTON0, DIA_APPMAPPED, &GUID_SysMouse, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(3) },
        { DIKEYBOARD_W }, { DIBUTTON_DRIVINGR_MENU, DIA_APPMAPPED, nullptr, DC_KEY(0x11) }, { DIMOUSE_XAXIS },
        { DIKEYBOARD_E, DIA_APPNOMAP }, { 0xFF000402 }, { DIKEYBOARD_R, DIA_APPMAPPED, &GUID_SysKeyboard, DC_KEY(0x13) },
        { DIMOUSE_YAXIS }, { DIKEYBOARD_T }, { DIKEYBOARD_Y | 0x4000 } } },
};

static
VOID
DcCheckMapCases(
    _In_ IDirectInput8W* Wide)
{
    static const GUID* Devices[] = { &GUID_SysKeyboard, &GUID_SysMouse };
    DIACTIONFORMATW Format;

    for (const GUID* Guid : Devices)
    {
        IDirectInputDevice8W* Device = nullptr;

        if (FAILED(Wide->CreateDevice(*Guid, &Device, nullptr)))
            continue;
        for (const auto& Case : DcMapCases)
        {
            for (DWORD Flags : { 0ul, 1ul, 2ul, 4ul })
            {
                DcFillFormat(&Format, Case.Specs, Case.Count, Case.Genre);
                printf("map %s %s flags %lu = %08lX\n", DcGuid(Guid).c_str(), Case.Name, Flags,
                       Device->BuildActionMap(&Format, L"dc user", Flags));
                DcPrintActions(&Format);
            }
        }
        Device->Release();
    }
}

static
VOID
DcCheckFormatShapes(
    _In_ IDirectInputDevice8W* Device)
{
    static const DC_ACTION_SPEC Specs[] = { { DIKEYBOARD_A }, { DIKEYBOARD_B } };
    WCHAR Long[300];
    DIACTIONFORMATW Format;

    for (UINT Shape = 0; Shape < 16; Shape++)
    {
        PCWSTR User = L"dc user";

        DcFillFormat(&Format, Specs, ARRAYSIZE(Specs), DIVIRTUAL_DRIVING_RACE);
        switch (Shape)
        {
            case 0: Format.lAxisMin = 10; Format.lAxisMax = 5; break;
            case 1: Format.lAxisMin = -5; Format.lAxisMax = -5; break;
            case 2: Format.dwNumActions = 0; Format.dwDataSize = 0; break;
            case 3: Format.rgoAction = nullptr; break;
            case 4: Format.dwGenre = 0x2A000000; break;
            case 5: Format.dwGenre = 0x80000000; break;
            case 6: Format.dwGenre = 0xA9000000; break;
            case 7: Format.dwGenre = 0x29000000; break;
            case 8: Format.dwActionSize = 40; break;
            case 9: Format.guidActionMap = GUID_NULL; break;
            case 10: Format.dwDataSize = 12; break;
            case 11:
                wmemset(Long, L'u', 256);
                Long[256] = L'\0';
                User = Long;
                break;
            case 12:
                wmemset(Long, L'u', 257);
                Long[257] = L'\0';
                User = Long;
                break;
            case 13: User = L""; break;
            case 14: Format.dwBufferSize = 0x1FF; break;
            case 15: Format.dwBufferSize = 0x200; break;
        }
        printf("shape %u = %08lX", Shape, Device->BuildActionMap(&Format, User, 0));
        printf(" crc %08lX\n", Format.dwCRC);
    }
}

static
VOID
DcPrintUserState(
    _In_ IDirectInputDevice8W* Device)
{
    DIPROPDWORD Buffer = { { sizeof(Buffer), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
    DIPROPDWORD Mode = { { sizeof(Mode), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
    DIPROPSTRING User = { { sizeof(User), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
    HRESULT Result;

    Result = Device->GetProperty(DIPROP_USERNAME, &User.diph);
    printf("    user %08lX '%s'", Result, DcWide(User.wsz).c_str());
    Result = Device->GetProperty(DIPROP_BUFFERSIZE, &Buffer.diph);
    printf(" buffer %08lX %lu", Result, Buffer.dwData);
    Result = Device->GetProperty(DIPROP_AXISMODE, &Mode.diph);
    printf(" mode %08lX %lu\n", Result, Mode.dwData);
}

static
VOID
DcCheckSetMaps(
    _In_ IDirectInput8W* Wide)
{
    static const DC_ACTION_SPEC KeySpecs[] = { { DIKEYBOARD_A }, { DIKEYBOARD_B }, { DIMOUSE_BUTTON0 } };
    static const DC_ACTION_SPEC MouseSpecs[] =
    {
        { DIMOUSE_XAXIS }, { DIMOUSE_YAXIS }, { DIKEYBOARD_A }, { DIMOUSE_BUTTON1 }, { DIMOUSE_WHEEL },
    };
    static const DC_ACTION_SPEC GenreSpecs[] = { { DIAXIS_DRIVINGR_STEER }, { DIBUTTON_DRIVINGR_MENU } };
    IDirectInputDevice8W* Keyboard = nullptr;
    IDirectInputDevice8W* Mouse = nullptr;
    DIACTIONFORMATW Format;
    BYTE State[64];

    if (FAILED(Wide->CreateDevice(GUID_SysKeyboard, &Keyboard, nullptr)))
        return;
    if (FAILED(Wide->CreateDevice(GUID_SysMouse, &Mouse, nullptr)))
    {
        Keyboard->Release();
        return;
    }

    DcFillFormat(&Format, KeySpecs, ARRAYSIZE(KeySpecs), DIVIRTUAL_DRIVING_RACE);
    printf("set build = %08lX\n", Keyboard->BuildActionMap(&Format, L"alice", 0));
    DcPrintUserState(Keyboard);
    printf("set alice = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0));
    DcPrintUserState(Keyboard);
    printf("set alice again = %08lX crc %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0), Format.dwCRC);
    printf("set alice force = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", DIDSAM_FORCESAVE));
    printf("set bob = %08lX\n", Keyboard->SetActionMap(&Format, L"bob", 0));
    DcPrintUserState(Keyboard);
    printf("set bob again = %08lX\n", Keyboard->SetActionMap(&Format, L"bob", 0));
    Format.dwBufferSize = 3;
    printf("set changed = %08lX\n", Keyboard->SetActionMap(&Format, L"bob", 0));
    DcPrintUserState(Keyboard);
    printf("set no user = %08lX\n", Keyboard->SetActionMap(&Format, nullptr, DIDSAM_NOUSER));
    DcPrintUserState(Keyboard);
    printf("set no user again = %08lX\n", Keyboard->SetActionMap(&Format, nullptr, DIDSAM_NOUSER));
    printf("set no user force = %08lX\n", Keyboard->SetActionMap(&Format, nullptr, DIDSAM_NOUSER | DIDSAM_FORCESAVE));
    printf("set flags 4 = %08lX\n", Keyboard->SetActionMap(&Format, nullptr, 4));
    printf("set empty = %08lX\n", Keyboard->SetActionMap(&Format, L"", 0));
    printf("set empty again = %08lX\n", Keyboard->SetActionMap(&Format, L"", 0));
    DcPrintUserState(Keyboard);
    printf("set state = %08lX", Keyboard->GetDeviceState(Format.dwDataSize, State));
    printf(" wrong size %08lX\n", Keyboard->GetDeviceState(Format.dwDataSize + 4, State));

    for (DWORD Size : { 0ul, 7ul, 1023ul, 5000ul })
    {
        DcFillFormat(&Format, KeySpecs, ARRAYSIZE(KeySpecs), DIVIRTUAL_DRIVING_RACE);
        Format.dwBufferSize = Size;
        Keyboard->BuildActionMap(&Format, L"alice", 0);
        printf("set buffer %lu = %08lX\n", Size, Keyboard->SetActionMap(&Format, L"alice", 0));
        DcPrintUserState(Keyboard);
    }

    DcFillFormat(&Format, GenreSpecs, ARRAYSIZE(GenreSpecs), DIVIRTUAL_DRIVING_RACE);
    printf("set nothing build = %08lX\n", Keyboard->BuildActionMap(&Format, L"alice", 0));
    printf("set nothing = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0));
    printf("set nothing again = %08lX", Keyboard->SetActionMap(&Format, L"alice", 0));
    printf(" state %08lX\n", Keyboard->GetDeviceState(Format.dwDataSize, State));

    DcFillFormat(&Format, KeySpecs, ARRAYSIZE(KeySpecs), DIVIRTUAL_DRIVING_RACE);
    Format.rgoAction[0].guidInstance = GUID_SysKeyboard;
    Format.rgoAction[0].dwHow = DIAH_DEFAULT;
    Format.rgoAction[0].dwObjID = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(2);
    printf("set wrong object = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0));
    DcPrintActions(&Format);
    Format.rgoAction[0].dwObjID = DC_KEY(0x20);
    Format.rgoAction[0].dwHow = DIAH_USERCONFIG;
    printf("set hand mapped = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0));
    DcPrintActions(&Format);
    Format.rgoAction[0].dwHow = 3;
    printf("set bad how = %08lX\n", Keyboard->SetActionMap(&Format, L"alice", 0));
    DcPrintActions(&Format);

    DcFillFormat(&Format, KeySpecs, ARRAYSIZE(KeySpecs), DIVIRTUAL_DRIVING_RACE);
    Keyboard->BuildActionMap(&Format, L"alice", 0);
    Keyboard->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    printf("set acquire %08lX", Keyboard->Acquire());
    printf(" set acquired %08lX", Keyboard->SetActionMap(&Format, L"alice", 0));
    printf(" again %08lX\n", Keyboard->SetActionMap(&Format, L"carol", 0));
    Keyboard->Unacquire();

    DcFillFormat(&Format, MouseSpecs, ARRAYSIZE(MouseSpecs), DIVIRTUAL_DRIVING_RACE);
    Format.lAxisMin = -50;
    Format.lAxisMax = 50;
    printf("mouse build = %08lX\n", Mouse->BuildActionMap(&Format, L"alice", 0));
    DcPrintActions(&Format);
    printf("mouse set = %08lX\n", Mouse->SetActionMap(&Format, L"alice", 0));
    DcPrintUserState(Mouse);
    for (DWORD Index = 0; Index < Format.dwNumActions; Index++)
    {
        DIPROPPOINTER Data = { { sizeof(Data), sizeof(DIPROPHEADER), Index * 4, DIPH_BYOFFSET } };
        DIPROPRANGE Range = { { sizeof(Range), sizeof(DIPROPHEADER), Index * 4, DIPH_BYOFFSET } };
        HRESULT Result = Mouse->GetProperty(DIPROP_APPDATA, &Data.diph);

        printf("    offset %lu appdata %08lX %llX", Index * 4, Result, static_cast<ULONG64>(Data.uData));
        Result = Mouse->GetProperty(DIPROP_RANGE, &Range.diph);
        printf(" range %08lX %ld %ld\n", Result, Range.lMin, Range.lMax);
    }
    Mouse->Release();
    Keyboard->Release();
}

static
VOID
DcCheckAnsiMaps(
    _In_ IDirectInput8W* Wide)
{
    IDirectInputDevice8W* Device = nullptr;
    IDirectInputDevice8A* Ansi = nullptr;
    DIACTIONA Actions[3];
    DIACTIONFORMATA Format;

    if (FAILED(Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr)))
        return;
    Device->QueryInterface(IID_IDirectInputDevice8A, reinterpret_cast<VOID**>(&Ansi));
    if (Ansi != nullptr)
    {
        for (UINT Shape = 0; Shape < 4; Shape++)
        {
            ZeroMemory(Actions, sizeof(Actions));
            ZeroMemory(&Format, sizeof(Format));
            Actions[0].dwSemantic = DIKEYBOARD_A;
            Actions[1].dwSemantic = DIKEYBOARD_B;
            Actions[2].dwSemantic = DIMOUSE_BUTTON0;
            Actions[1].uAppData = 7;
            Format.dwSize = Shape == 1 ? sizeof(DIACTIONFORMATW) : sizeof(Format);
            Format.dwActionSize = sizeof(DIACTIONA);
            Format.dwDataSize = sizeof(DWORD) * ARRAYSIZE(Actions);
            Format.dwNumActions = ARRAYSIZE(Actions);
            Format.rgoAction = Actions;
            Format.guidActionMap = DcMapGuid;
            Format.dwGenre = DIVIRTUAL_DRIVING_RACE;
            Format.dwBufferSize = 4;
            strcpy_s(Format.tszActionMap, "ansi");
            printf("ansi %u build = %08lX", Shape,
                   Ansi->BuildActionMap(&Format, Shape == 2 ? "ansi user" : nullptr, Shape == 3 ? DIDBAM_HWDEFAULTS : 0));
            printf(" crc %08lX time %08lX", Format.dwCRC, Format.ftTimeStamp.dwLowDateTime);
            for (const auto& Action : Actions)
                printf(" %s/%08lX/%lX", DcGuid(&Action.guidInstance).c_str(), Action.dwObjID, Action.dwHow);
            printf(" set %08lX", Ansi->SetActionMap(&Format, "ansi user", 0));
            printf(" again %08lX\n", Ansi->SetActionMap(&Format, "ansi user", 0));
        }
        Ansi->Release();
    }
    Device->Release();
}

static
VOID
DcCheckImageInfo(
    _In_ IDirectInput8W* Wide)
{
    IDirectInputDevice8W* Device = nullptr;
    IDirectInputDevice8A* Ansi = nullptr;
    DIDEVICEIMAGEINFOW Images[2];
    DIDEVICEIMAGEINFOA AnsiImages[2];

    if (FAILED(Wide->CreateDevice(GUID_SysMouse, &Device, nullptr)))
        return;
    printf("image null = %08lX\n", Device->GetImageInfo(nullptr));
    for (UINT Shape = 0; Shape < 6; Shape++)
    {
        DIDEVICEIMAGEINFOHEADERW Header = { sizeof(Header), sizeof(DIDEVICEIMAGEINFOW) };

        switch (Shape)
        {
            case 0: Header.dwSize = 36; break;
            case 1: Header.dwSizeImageInfo = sizeof(DIDEVICEIMAGEINFOA); break;
            case 2: Header.dwBufferSize = sizeof(Images); break;
            case 3: Header.lprgImageInfoArray = Images; Header.dwBufferSize = 10; break;
            case 4: Header.lprgImageInfoArray = Images; Header.dwBufferSize = sizeof(Images); break;
            case 5: Header.lprgImageInfoArray = Images; break;
        }
        Header.dwBufferUsed = 0x55;
        printf("image %u = %08lX used %lX\n", Shape, Device->GetImageInfo(&Header), Header.dwBufferUsed);
    }
    Device->QueryInterface(IID_IDirectInputDevice8A, reinterpret_cast<VOID**>(&Ansi));
    if (Ansi != nullptr)
    {
        printf("image ansi null = %08lX\n", Ansi->GetImageInfo(nullptr));
        for (UINT Shape = 0; Shape < 7; Shape++)
        {
            DIDEVICEIMAGEINFOHEADERA Header = { sizeof(Header), sizeof(DIDEVICEIMAGEINFOA) };

            switch (Shape)
            {
                case 0: Header.dwSize = 36; break;
                case 1: Header.dwSizeImageInfo = sizeof(DIDEVICEIMAGEINFOW); break;
                case 2: Header.dwBufferSize = sizeof(AnsiImages); break;
                case 3: Header.lprgImageInfoArray = AnsiImages; Header.dwBufferSize = 10; break;
                case 4: Header.lprgImageInfoArray = AnsiImages; Header.dwBufferSize = sizeof(AnsiImages); break;
                case 5: Header.lprgImageInfoArray = AnsiImages; Header.dwBufferSize = 0xFFFFFFFF; break;
                case 6: Header.lprgImageInfoArray = AnsiImages; break;
            }
            Header.dwBufferUsed = 0x55;
            printf("image ansi %u = %08lX used %lX\n", Shape, Ansi->GetImageInfo(&Header), Header.dwBufferUsed);
        }
        Ansi->Release();
    }
    Device->Release();
}

struct DC_SEMANTIC_RUN
{
    UINT Calls;
    UINT StopAt;
};

static
BOOL
CALLBACK
DcCountingCallback(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ LPDIRECTINPUTDEVICE8W Device,
    _In_ DWORD Flags,
    _In_ DWORD Remaining,
    _In_ VOID* Context)
{
    DC_SEMANTIC_RUN* Run = static_cast<DC_SEMANTIC_RUN*>(Context);
    DIPROPSTRING User = { { sizeof(User), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };
    HRESULT Result = Device->GetProperty(DIPROP_USERNAME, &User.diph);

    printf("    device %s type %08lX flags %08lX remaining %lu user %08lX '%s'\n",
           DcGuid(&Instance->guidInstance).c_str(), Instance->dwDevType, Flags, Remaining, Result,
           DcWide(User.wsz).c_str());
    Run->Calls += 1;
    return Run->Calls == Run->StopAt ? DIENUM_STOP : DIENUM_CONTINUE;
}

static
BOOL
CALLBACK
DcAnsiSemanticsCallback(
    _In_ LPCDIDEVICEINSTANCEA Instance,
    _In_ LPDIRECTINPUTDEVICE8A Device,
    _In_ DWORD Flags,
    _In_ DWORD Remaining,
    _In_ VOID* Context)
{
    DIDEVICEINSTANCEA Info = { sizeof(Info) };
    HRESULT Result = Device->GetDeviceInfo(&Info);

    UNREFERENCED_PARAMETER(Context);
    printf("    ansi device %s size %lu type %08lX flags %08lX remaining %lu info %08lX '%s'\n",
           DcGuid(&Instance->guidInstance).c_str(), Instance->dwSize, Instance->dwDevType, Flags, Remaining, Result,
           Info.tszInstanceName);
    return DIENUM_CONTINUE;
}

static
VOID
DcCheckSemanticEnum(
    _In_ IDirectInput8W* Wide)
{
    static const DC_ACTION_SPEC Specs[] =
    {
        { DIKEYBOARD_A | 0x4000 }, { DIKEYBOARD_B }, { DIMOUSE_BUTTON0 }, { DIMOUSE_XAXIS | 0x4000 },
        { DIAXIS_DRIVINGR_STEER },
    };
    static const DC_ACTION_SPEC Low[] = { { DIKEYBOARD_A | 0x4000 }, { DIMOUSE_BUTTON0 | 0x4000 } };
    IDirectInputDevice8W* Keyboard = nullptr;
    IDirectInputDevice8W* Mouse = nullptr;
    IDirectInput8A* Ansi = nullptr;
    DIACTIONFORMATW Format;
    DC_SEMANTIC_RUN Run;

    if (FAILED(Wide->CreateDevice(GUID_SysKeyboard, &Keyboard, nullptr)))
        return;
    if (FAILED(Wide->CreateDevice(GUID_SysMouse, &Mouse, nullptr)))
    {
        Keyboard->Release();
        return;
    }

    struct
    {
        PCWSTR User;
        DWORD Flags;
    } Runs[] =
    {
        { nullptr, 0 }, { L"dc owner", 0 }, { L"dc owner", DIEDBSFL_THISUSER }, { L"dc", DIEDBSFL_THISUSER },
        { L"dc ownerx", DIEDBSFL_THISUSER }, { nullptr, DIEDBSFL_THISUSER }, { L"", DIEDBSFL_THISUSER },
        { L"dc owner", DIEDBSFL_AVAILABLEDEVICES }, { L"dc owner", DIEDBSFL_AVAILABLEDEVICES | DIEDBSFL_THISUSER },
        { L"bob", DIEDBSFL_AVAILABLEDEVICES | DIEDBSFL_THISUSER }, { L"dc owner", DIEDBSFL_MULTIMICEKEYBOARDS },
        { L"dc owner", DIEDBSFL_FORCEFEEDBACK }, { L"dc owner", 0x7110 & ~DIEDBSFL_NONGAMINGDEVICES },
        { L"dc owner", DIEDBSFL_NONGAMINGDEVICES }, { L"dc owner", 0x7110 }, { nullptr, DIEDBSFL_NONGAMINGDEVICES | DIEDBSFL_AVAILABLEDEVICES },
        { L"dc owner", 0x8000 }, { L"dc owner", 0x1 },
    };

    for (UINT Phase = 0; Phase < 2; Phase++)
    {
        if (Phase == 1)
        {
            DcFillFormat(&Format, Specs, ARRAYSIZE(Specs), DIVIRTUAL_DRIVING_RACE);
            Keyboard->BuildActionMap(&Format, L"dc owner", 0);
            printf("semantic own keyboard = %08lX\n", Keyboard->SetActionMap(&Format, L"dc owner", 0));
            printf("semantic free mouse = %08lX\n", Mouse->SetActionMap(&Format, nullptr, DIDSAM_NOUSER));
        }
        for (const auto& Entry : Runs)
        {
            DcFillFormat(&Format, Specs, ARRAYSIZE(Specs), DIVIRTUAL_DRIVING_RACE);
            Run = { 0, 0 };
            printf("semantic %u user %s flags %#lx = ", Phase, Entry.User != nullptr ? DcWide(Entry.User).c_str() : "(null)",
                   Entry.Flags);
            printf("%08lX\n", Wide->EnumDevicesBySemantics(Entry.User, &Format, DcCountingCallback, &Run, Entry.Flags));
        }
    }
    DcPrintActions(&Format);

    DcFillFormat(&Format, Low, ARRAYSIZE(Low), DIVIRTUAL_FLYING_MILITARY);
    Run = { 0, 1 };
    printf("semantic stop = %08lX calls %u\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0),
           Run.Calls);
    DcFillFormat(&Format, Low, ARRAYSIZE(Low), DIVIRTUAL_FLYING_MILITARY);
    Format.ftTimeStamp.dwLowDateTime = 0xFFFFFFFF;
    Format.ftTimeStamp.dwHighDateTime = 0xFFFFFFFF;
    Run = { 0, 0 };
    printf("semantic unsaved = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
    printf("semantic null callback = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, nullptr, nullptr, 0));
    printf("semantic null format = %08lX\n", Wide->EnumDevicesBySemantics(L"x", nullptr, DcCountingCallback, &Run, 0));
    Format.dwSize = sizeof(DIACTIONFORMATA);
    printf("semantic ansi size = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
    {
        static const DC_ACTION_SPEC Bad[] = { { DIKEYBOARD_A }, { DIKEYBOARD_B, 0x40 } };
        static const DC_ACTION_SPEC Twice[] = { { DIKEYBOARD_A }, { DIKEYBOARD_A } };
        static const DC_ACTION_SPEC Fixed[] =
        {
            { DIKEYBOARD_A, DIA_APPMAPPED, nullptr, DC_KEY(0x30) }, { DIKEYBOARD_B, 0, &GUID_SysKeyboard, DC_KEY(0x31), 3 },
        };
        static const DC_ACTION_SPEC Hows[] =
        {
            { DIKEYBOARD_A, 0, &GUID_SysKeyboard, DC_KEY(0x1E), DIAH_USERCONFIG }, { DIMOUSE_BUTTON0 },
        };
        WCHAR Long[300];

        DcFillFormat(&Format, Bad, ARRAYSIZE(Bad), DIVIRTUAL_DRIVING_RACE);
        printf("semantic bad flags = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
        DcPrintActions(&Format);
        DcFillFormat(&Format, Twice, ARRAYSIZE(Twice), DIVIRTUAL_DRIVING_RACE);
        printf("semantic twice = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
        DcFillFormat(&Format, Fixed, ARRAYSIZE(Fixed), DIVIRTUAL_DRIVING_RACE);
        printf("semantic bad how = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
        DcPrintActions(&Format);
        DcFillFormat(&Format, Hows, ARRAYSIZE(Hows), DIVIRTUAL_DRIVING_RACE);
        Run = { 0, 0 };
        printf("semantic user config = %08lX\n",
               Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
        DcPrintActions(&Format);
        wmemset(Long, L'n', 257);
        Long[257] = L'\0';
        DcFillFormat(&Format, Low, ARRAYSIZE(Low), DIVIRTUAL_DRIVING_RACE);
        printf("semantic long user = %08lX\n", Wide->EnumDevicesBySemantics(Long, &Format, DcCountingCallback, &Run, 0));
        Long[256] = L'\0';
        Run = { 0, 0 };
        printf("semantic user 256 = %08lX\n", Wide->EnumDevicesBySemantics(Long, &Format, DcCountingCallback, &Run, 0));
        Format.rgoAction[0].lptszActionName = Long;
        printf("semantic long name = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
        Long[255] = L'\0';
        Run = { 0, 0 };
        printf("semantic name 255 = %08lX\n", Wide->EnumDevicesBySemantics(L"x", &Format, DcCountingCallback, &Run, 0));
    }

    Wide->QueryInterface(IID_IDirectInput8A, reinterpret_cast<VOID**>(&Ansi));
    if (Ansi != nullptr)
    {
        DIACTIONA Actions[2] = {};
        DIACTIONFORMATA AnsiFormat = {};

        Actions[0].dwSemantic = DIKEYBOARD_A;
        Actions[1].dwSemantic = DIMOUSE_BUTTON0 | 0x4000;
        AnsiFormat.dwSize = sizeof(AnsiFormat);
        AnsiFormat.dwActionSize = sizeof(DIACTIONA);
        AnsiFormat.dwDataSize = sizeof(DWORD) * ARRAYSIZE(Actions);
        AnsiFormat.dwNumActions = ARRAYSIZE(Actions);
        AnsiFormat.rgoAction = Actions;
        AnsiFormat.guidActionMap = DcMapGuid;
        AnsiFormat.dwGenre = DIVIRTUAL_DRIVING_RACE;
        for (PCSTR User : { static_cast<PCSTR>(nullptr), "dc owner", "" })
        {
            for (DWORD Flags : { 0ul, static_cast<DWORD>(DIEDBSFL_THISUSER), static_cast<DWORD>(DIEDBSFL_AVAILABLEDEVICES) })
            {
                printf("semantic ansi user %s flags %#lx = ", User != nullptr ? User : "(null)", Flags);
                printf("%08lX\n", Ansi->EnumDevicesBySemantics(User, &AnsiFormat, DcAnsiSemanticsCallback, nullptr, Flags));
            }
        }
        printf("    ansi actions %s/%lX %s/%lX\n", DcGuid(&Actions[0].guidInstance).c_str(), Actions[0].dwHow,
               DcGuid(&Actions[1].guidInstance).c_str(), Actions[1].dwHow);
        Actions[1].dwFlags = 0x40;
        printf("semantic ansi bad = %08lX how %lX\n",
               Ansi->EnumDevicesBySemantics("x", &AnsiFormat, DcAnsiSemanticsCallback, nullptr, 0), Actions[1].dwHow);
        AnsiFormat.dwSize = sizeof(DIACTIONFORMATW);
        printf("semantic ansi wide size = %08lX\n",
               Ansi->EnumDevicesBySemantics("x", &AnsiFormat, DcAnsiSemanticsCallback, nullptr, 0));
        Ansi->Release();
    }
    Mouse->Release();
    Keyboard->Release();
}

/* A COM object with just enough of a surface's table for the target checks */
struct DC_FAKE_SURFACE
{
    VOID** Table;
    UINT Kind;
    DWORD Width;
    DWORD Height;
    DWORD Bits;
    DWORD Red;
    DWORD Green;
    DWORD Blue;
    DWORD PixelFlags;
    DWORD D3DFormat;
};

#define DC_FAKE_PLAIN       0
#define DC_FAKE_DDRAW7      1
#define DC_FAKE_DDRAW4      2
#define DC_FAKE_D3D8        3

static const GUID DcIidD3D8Surface = { 0xB96EEBCA, 0xB326, 0x4EA5, { 0x88, 0x2F, 0x2F, 0xF5, 0xBA, 0xE0, 0x21, 0xDD } };

static
HRESULT
STDMETHODCALLTYPE
DcFakeQuery(
    _In_ DC_FAKE_SURFACE* This,
    _In_ REFIID Riid,
    _Out_ VOID** Object)
{
    BOOLEAN Match = IsEqualIID(Riid, IID_IUnknown) ||
                    (This->Kind == DC_FAKE_DDRAW7 && IsEqualIID(Riid, IID_IDirectDrawSurface7)) ||
                    (This->Kind == DC_FAKE_DDRAW4 && IsEqualIID(Riid, IID_IDirectDrawSurface4)) ||
                    (This->Kind == DC_FAKE_D3D8 && IsEqualIID(Riid, DcIidD3D8Surface));

    *Object = Match ? This : nullptr;
    return Match ? S_OK : E_NOINTERFACE;
}

static
ULONG
STDMETHODCALLTYPE
DcFakeRefs(
    _In_ DC_FAKE_SURFACE* This)
{
    UNREFERENCED_PARAMETER(This);
    return 1;
}

static
HRESULT
STDMETHODCALLTYPE
DcFakeD3DDesc(
    _In_ DC_FAKE_SURFACE* This,
    _Out_ DWORD* Desc)
{
    ZeroMemory(Desc, 8 * sizeof(DWORD));
    Desc[0] = This->D3DFormat;
    Desc[6] = This->Width;
    Desc[7] = This->Height;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DcFakeDDrawDesc(
    _In_ DC_FAKE_SURFACE* This,
    _Inout_ DDSURFACEDESC2* Desc)
{
    if (Desc->dwSize != sizeof(*Desc))
        return DDERR_INVALIDPARAMS;
    Desc->dwWidth = This->Width;
    Desc->dwHeight = This->Height;
    Desc->ddpfPixelFormat.dwFlags = This->PixelFlags;
    Desc->ddpfPixelFormat.dwRGBBitCount = This->Bits;
    Desc->ddpfPixelFormat.dwRBitMask = This->Red;
    Desc->ddpfPixelFormat.dwGBitMask = This->Green;
    Desc->ddpfPixelFormat.dwBBitMask = This->Blue;
    return S_OK;
}

static VOID* DcFakeTable[32];

static
BOOLEAN
DcActionFrameworkRegistered(VOID)
{
    HKEY Key;

    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, L"CLSID\\{9F34AF20-6095-11D3-8FB2-00C04F8EC627}\\InProcServer32", 0,
                      KEY_READ, &Key) != ERROR_SUCCESS)
    {
        return FALSE;
    }
    RegCloseKey(Key);
    return TRUE;
}

static
BOOL
CALLBACK
DcConfigureCallback(
    _In_opt_ IUnknown* Target,
    _In_opt_ VOID* Context)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Context);
    return TRUE;
}

static
VOID
DcCheckConfigure(
    _In_ IDirectInput8W* Wide)
{
    static const DC_ACTION_SPEC Specs[] = { { DIKEYBOARD_A }, { DIKEYBOARD_B } };
    static const struct
    {
        UINT Kind;
        DWORD Width;
        DWORD Height;
        DWORD Bits;
        DWORD Red;
        DWORD Green;
        DWORD Blue;
        DWORD PixelFlags;
        DWORD D3DFormat;
    } Surfaces[] =
    {
        { DC_FAKE_PLAIN },
        { DC_FAKE_DDRAW7, 640, 480, 32, 0xFF0000, 0xFF00, 0xFF, DDPF_RGB },
        { DC_FAKE_DDRAW7, 639, 480, 32, 0xFF0000, 0xFF00, 0xFF, DDPF_RGB },
        { DC_FAKE_DDRAW7, 640, 479, 32, 0xFF0000, 0xFF00, 0xFF, DDPF_RGB },
        { DC_FAKE_DDRAW7, 800, 600, 32, 0xFF, 0xFF00, 0xFF0000, DDPF_RGB },
        { DC_FAKE_DDRAW7, 800, 600, 32, 0xFF0000, 0xFF00, 0xFF, 0 },
        { DC_FAKE_DDRAW4, 800, 600, 16, 0x7C00, 0x3E0, 0x1F, DDPF_RGB },
        { DC_FAKE_DDRAW4, 800, 600, 16, 0xF800, 0x7E0, 0x3, DDPF_RGB },
        { DC_FAKE_DDRAW4, 800, 600, 16, 0xF00, 0xF0, 0xF, DDPF_RGB },
        { DC_FAKE_DDRAW4, 800, 600, 8, 0, 0, 0, DDPF_RGB },
        { DC_FAKE_D3D8, 640, 480, 0, 0, 0, 0, 0, 22 },
        { DC_FAKE_D3D8, 640, 480, 0, 0, 0, 0, 0, 26 },
        { DC_FAKE_D3D8, 320, 480, 0, 0, 0, 0, 0, 21 },
    };
    IDirectInput8A* Ansi = nullptr;
    DICONFIGUREDEVICESPARAMSW Params;
    DIACTIONFORMATW Formats[2];
    DIACTIONW Actions[2][2];

    if (DcActionFrameworkRegistered())
    {
        printf("configure skipped, the configuration UI is installed\n");
        return;
    }
    DcFakeTable[0] = reinterpret_cast<VOID*>(DcFakeQuery);
    DcFakeTable[1] = reinterpret_cast<VOID*>(DcFakeRefs);
    DcFakeTable[2] = reinterpret_cast<VOID*>(DcFakeRefs);
    DcFakeTable[8] = reinterpret_cast<VOID*>(DcFakeD3DDesc);
    DcFakeTable[22] = reinterpret_cast<VOID*>(DcFakeDDrawDesc);

    for (UINT Index = 0; Index < 2; Index++)
    {
        DcFillFormat(&Formats[Index], Specs, ARRAYSIZE(Specs), DIVIRTUAL_DRIVING_RACE);
        memcpy(Actions[Index], DcSpecActions, sizeof(Actions[Index]));
        Formats[Index].rgoAction = Actions[Index];
    }
    printf("configure null = %08lX\n", Wide->ConfigureDevices(nullptr, nullptr, 0, nullptr));
    for (UINT Shape = 0; Shape < 15; Shape++)
    {
        LPDICONFIGUREDEVICESCALLBACK Callback = nullptr;
        DWORD Flags = 0;

        ZeroMemory(&Params, sizeof(Params));
        Params.dwSize = sizeof(Params);
        Params.dwcFormats = 2;
        Params.lprgFormats = Formats;
        Formats[1].dwSize = sizeof(DIACTIONFORMATW);
        Formats[1].dwGenre = DIVIRTUAL_DRIVING_RACE;
        switch (Shape)
        {
            case 1: Params.dwSize = 80; break;
            case 2: Params.lprgFormats = nullptr; break;
            case 3: Params.lprgFormats = nullptr; Params.dwcFormats = 0; break;
            case 4: Formats[1].dwSize = sizeof(DIACTIONFORMATA); break;
            case 5: Formats[1].dwGenre = 0; break;
            case 6: Params.dwcUsers = 2; Params.lptszUserNames = const_cast<LPWSTR>(L"ann\0bea\0"); break;
            case 7: Params.dwcUsers = 2; Params.lptszUserNames = const_cast<LPWSTR>(L"ann\0\0"); break;
            case 8: Params.dwcUsers = 0; Params.lptszUserNames = const_cast<LPWSTR>(L"ann\0"); break;
            case 9: Params.dwcUsers = 0; Params.lptszUserNames = const_cast<LPWSTR>(L"\0"); break;
            case 10: Flags = DICD_EDIT; break;
            case 11: Flags = 2; break;
            case 12: Callback = DcConfigureCallback; break;
            case 13: Params.lpUnkDDSTarget = reinterpret_cast<IUnknown*>(&Params); break;
            case 14: Params.dwcFormats = 0; break;
        }
        printf("configure %u = %08lX\n", Shape, Wide->ConfigureDevices(Callback, &Params, Flags, nullptr));
    }
    for (const auto& Surface : Surfaces)
    {
        DC_FAKE_SURFACE Fake = { DcFakeTable, Surface.Kind, Surface.Width, Surface.Height, Surface.Bits, Surface.Red,
                                 Surface.Green, Surface.Blue, Surface.PixelFlags, Surface.D3DFormat };

        ZeroMemory(&Params, sizeof(Params));
        Params.dwSize = sizeof(Params);
        Params.lpUnkDDSTarget = reinterpret_cast<IUnknown*>(&Fake);
        printf("configure surface %u %lux%lu %lu = %08lX\n", Surface.Kind, Surface.Width, Surface.Height,
               Surface.Bits + Surface.D3DFormat, Wide->ConfigureDevices(DcConfigureCallback, &Params, 0, nullptr));
    }

    Wide->QueryInterface(IID_IDirectInput8A, reinterpret_cast<VOID**>(&Ansi));
    if (Ansi != nullptr)
    {
        DIACTIONFORMATA AnsiFormat = {};
        DIACTIONA AnsiActions[1] = {};
        DICONFIGUREDEVICESPARAMSA AnsiParams;

        AnsiActions[0].dwSemantic = DIKEYBOARD_A;
        AnsiFormat.dwSize = sizeof(AnsiFormat);
        AnsiFormat.dwActionSize = sizeof(DIACTIONA);
        AnsiFormat.dwDataSize = sizeof(DWORD);
        AnsiFormat.dwNumActions = 1;
        AnsiFormat.rgoAction = AnsiActions;
        AnsiFormat.guidActionMap = DcMapGuid;
        AnsiFormat.dwGenre = DIVIRTUAL_DRIVING_RACE;
        printf("configure ansi null = %08lX\n", Ansi->ConfigureDevices(nullptr, nullptr, 0, nullptr));
        for (UINT Shape = 0; Shape < 7; Shape++)
        {
            ZeroMemory(&AnsiParams, sizeof(AnsiParams));
            AnsiParams.dwSize = sizeof(AnsiParams);
            AnsiParams.dwcFormats = 1;
            AnsiParams.lprgFormats = &AnsiFormat;
            AnsiFormat.dwSize = sizeof(AnsiFormat);
            switch (Shape)
            {
                case 1: AnsiParams.dwSize = sizeof(Params); break;
                case 2: AnsiFormat.dwSize = sizeof(DIACTIONFORMATW); break;
                case 3: AnsiParams.dwcUsers = 2; AnsiParams.lptszUserNames = const_cast<LPSTR>("ann\0bea\0"); break;
                case 4: AnsiParams.dwcUsers = 2; AnsiParams.lptszUserNames = const_cast<LPSTR>("ann\0\0"); break;
                case 5: AnsiParams.lprgFormats = nullptr; break;
                case 6: AnsiParams.lpUnkDDSTarget = reinterpret_cast<IUnknown*>(&AnsiParams); break;
            }
            printf("configure ansi %u = %08lX\n", Shape, Ansi->ConfigureDevices(nullptr, &AnsiParams, 0, nullptr));
        }
        Ansi->Release();
    }
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

    DcCheckMapCases(Wide);
    {
        IDirectInputDevice8W* Device = nullptr;

        if (SUCCEEDED(Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr)))
        {
            DcCheckFormatShapes(Device);
            Device->Release();
        }
    }
    DcCheckSetMaps(Wide);
    DcCheckAnsiMaps(Wide);
    DcCheckImageInfo(Wide);
    DcCheckSemanticEnum(Wide);
    DcCheckConfigure(Wide);
    Wide->Release();
}
