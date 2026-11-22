/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects, data formats, cooperative levels, acquisition and properties of the system devices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

static const GUID* DcDataDevices[] = { &GUID_SysKeyboard, &GUID_SysMouse, &GUID_SysKeyboardEm2, &GUID_SysMouseEm2 };

/* The system devices and every HID device, for the checks that only read or set per process state */
static
const std::vector<const GUID*>&
DcReadDevices(VOID)
{
    static std::vector<GUID> Hid;
    static std::vector<const GUID*> List;

    if (!List.empty())
        return List;
    for (const GUID* Guid : DcDataDevices)
        List.push_back(Guid);

    IDirectInput8W* Wide = DcCreateWide();

    if (Wide == nullptr)
        return List;
    Wide->EnumDevices(DI8DEVCLASS_ALL | DIDEVTYPE_HID, [](LPCDIDEVICEINSTANCEW Info, VOID* Context) -> BOOL
    {
        static_cast<std::vector<GUID>*>(Context)->push_back(Info->guidInstance);
        return DIENUM_CONTINUE;
    }, &Hid, DIEDFL_ALLDEVICES);
    Wide->Release();
    for (const GUID& Guid : Hid)
        List.push_back(&Guid);
    return List;
}

static
VOID
DcPrintObjectW(
    _In_z_ PCSTR Prefix,
    _In_ LPCDIDEVICEOBJECTINSTANCEW Object)
{
    printf("%s size %lu %s ofs %lu type %08lX flags %08lX '%s'", Prefix, Object->dwSize, DcGuid(&Object->guidType).c_str(),
           Object->dwOfs, Object->dwType, Object->dwFlags, DcWide(Object->tszName).c_str());
    if (Object->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEW))
    {
        printf(" ff %lu %lu coll %u design %u page %04X usage %04X dim %lu exp %u report %u", Object->dwFFMaxForce,
               Object->dwFFForceResolution, Object->wCollectionNumber, Object->wDesignatorIndex, Object->wUsagePage,
               Object->wUsage, Object->dwDimension, Object->wExponent, Object->wReportId);
    }
    printf("\n");
}

static
BOOL
CALLBACK
DcEnumObjectW(
    _In_ LPCDIDEVICEOBJECTINSTANCEW Object,
    _In_ VOID* Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    if (Count[1] == 0)
        DcPrintObjectW("    obj", Object);
    Count[0]++;
    return DIENUM_CONTINUE;
}

static
BOOL
CALLBACK
DcEnumObjectA(
    _In_ LPCDIDEVICEOBJECTINSTANCEA Object,
    _In_ VOID* Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    if (Count[0] < 4)
    {
        printf("    objA size %lu %s ofs %lu type %08lX '%s'\n", Object->dwSize, DcGuid(&Object->guidType).c_str(),
               Object->dwOfs, Object->dwType, Object->tszName);
    }
    Count[0]++;
    return DIENUM_CONTINUE;
}

static
BOOL
CALLBACK
DcEnumObjectStop(
    _In_ LPCDIDEVICEOBJECTINSTANCEW Object,
    _In_ VOID* Context)
{
    UNREFERENCED_PARAMETER(Object);
    (*static_cast<ULONG*>(Context))++;
    return DIENUM_STOP;
}

static
IDirectInputDevice8W*
DcOpen(
    _In_ IDirectInput8W* Input,
    _In_ const GUID* Guid)
{
    IDirectInputDevice8W* Device = nullptr;

    if (FAILED(Input->CreateDevice(*Guid, &Device, nullptr)))
        return nullptr;
    return Device;
}

static
VOID
DcCheckObjects(VOID)
{
    static const DWORD Flags[] =
    {
        DIDFT_ALL, DIDFT_AXIS, DIDFT_RELAXIS, DIDFT_ABSAXIS, DIDFT_BUTTON, DIDFT_PSHBUTTON, DIDFT_TGLBUTTON, DIDFT_POV,
        DIDFT_COLLECTION, DIDFT_NODATA, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(3), DIDFT_AXIS | DIDFT_MAKEINSTANCE(1),
        DIDFT_ALIAS, DIDFT_VENDORDEFINED, DIDFT_ALIAS | DIDFT_VENDORDEFINED, DIDFT_FFACTUATOR, DIDFT_OUTPUT, 0x20,
        0x10000000, 0x20000000, 0x40000000, 0x80000000, DIDFT_BUTTON | DIDFT_ANYINSTANCE,
    };
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();

    if (Wide == nullptr || Ansi == nullptr)
        return;
    for (const GUID* Guid : DcReadDevices())
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);
        IDirectInputDevice8A* DeviceA = nullptr;

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr || FAILED(Ansi->CreateDevice(*Guid, &DeviceA, nullptr)))
            continue;
        {
            ULONG Count[2] = { 0, 0 };

            printf("  enum all = %08lX", Device->EnumObjects(DcEnumObjectW, Count, DIDFT_ALL));
            printf(" count %lu\n", Count[0]);
        }
        for (DWORD Flag : Flags)
        {
            ULONG Count[2] = { 0, 1 };
            HRESULT Result = Device->EnumObjects(DcEnumObjectW, Count, Flag);

            printf("  enum %08lX = %08lX count %lu\n", Flag, Result, Count[0]);
        }
        {
            ULONG Count = 0;
            HRESULT Result = Device->EnumObjects(DcEnumObjectStop, &Count, DIDFT_ALL);

            printf("  enum stop = %08lX count %lu\n", Result, Count);
            printf("  enum null = %08lX\n", Device->EnumObjects(nullptr, nullptr, DIDFT_ALL));
            Count = 0;
            Result = DeviceA->EnumObjects(DcEnumObjectA, &Count, DIDFT_ALL);
            printf("  enum ansi = %08lX count %lu\n", Result, Count);
        }

        for (DWORD How : { 0ul, 1ul, 2ul, 3ul, 4ul })
        {
            for (DWORD Object : { 0ul, 1ul, 4ul, 12ul, 0x1Eul, 0x0002ul, 0x0102ul, 0x0304ul, 0x1E04ul, 0x00010001ul })
            {
                DIDEVICEOBJECTINSTANCEW Info;
                HRESULT Result;

                memset(&Info, 0xCC, sizeof(Info));
                Info.dwSize = sizeof(Info);
                Result = Device->GetObjectInfo(&Info, Object, How);
                printf("  objinfo how %lu obj %#lx = %08lX\n", How, Object, Result);
                if (SUCCEEDED(Result))
                    DcPrintObjectW("    info", &Info);
            }
        }
        for (DWORD Size : { static_cast<DWORD>(sizeof(DIDEVICEOBJECTINSTANCE_DX3W)), 0ul, 580ul })
        {
            DIDEVICEOBJECTINSTANCEW Info;
            HRESULT Result;

            memset(&Info, 0xCC, sizeof(Info));
            Info.dwSize = Size;
            Result = Device->GetObjectInfo(&Info, 0x0102, DIPH_BYID);
            printf("  objinfo size %lu = %08lX\n", Size, Result);
            if (SUCCEEDED(Result))
                DcBytes("    bytes", &Info, sizeof(Info));
        }
        printf("  objinfo null = %08lX\n", Device->GetObjectInfo(nullptr, 0, DIPH_BYID));
        for (DWORD Size : { static_cast<DWORD>(sizeof(DIDEVICEOBJECTINSTANCEA)), static_cast<DWORD>(sizeof(DIDEVICEOBJECTINSTANCE_DX3A)),
                            static_cast<DWORD>(sizeof(DIDEVICEOBJECTINSTANCEW)) })
        {
            DIDEVICEOBJECTINSTANCEA Info;
            HRESULT Result;

            memset(&Info, 0xCC, sizeof(Info));
            Info.dwSize = Size;
            Result = DeviceA->GetObjectInfo(&Info, 0x0102, DIPH_BYID);
            printf("  objinfoa size %lu = %08lX\n", Size, Result);
            if (SUCCEEDED(Result))
                DcBytes("    bytes", &Info, sizeof(Info));
        }

        /* Offsets resolve once a format is set */
        Device->SetDataFormat(IsEqualGUID(*Guid, GUID_SysKeyboard) || IsEqualGUID(*Guid, GUID_SysKeyboardEm2) ?
                              &DcKeyboardFormat : &DcMouse2Format);
        for (DWORD Object : { 0ul, 1ul, 4ul, 8ul, 12ul, 13ul, 0x1Eul, 19ul, 20ul, 255ul, 256ul })
        {
            DIDEVICEOBJECTINSTANCEW Info;
            HRESULT Result;

            memset(&Info, 0xCC, sizeof(Info));
            Info.dwSize = sizeof(Info);
            Result = Device->GetObjectInfo(&Info, Object, DIPH_BYOFFSET);
            printf("  objinfo formatted ofs %lu = %08lX\n", Object, Result);
            if (SUCCEEDED(Result))
                DcPrintObjectW("    info", &Info);
        }
        {
            ULONG Count[2] = { 0, 0 };

            printf("  enum formatted = %08lX\n", Device->EnumObjects(DcEnumObjectW, Count, DIDFT_ALL));
        }
        DeviceA->Release();
        Device->Release();
    }
    Ansi->Release();
    Wide->Release();
}

static DIOBJECTDATAFORMAT DcCustomObjects[8];

static
VOID
DcTryFormat(
    _In_ IDirectInputDevice8W* Device,
    _In_z_ PCSTR Name,
    _In_ DWORD Flags,
    _In_ DWORD DataSize,
    _In_ UINT Count)
{
    DIDATAFORMAT Format = { sizeof(Format), sizeof(DIOBJECTDATAFORMAT), Flags, DataSize, Count, DcCustomObjects };
    HRESULT Result = Device->SetDataFormat(&Format);

    printf("  format %s = %08lX", Name, Result);
    if (SUCCEEDED(Result))
    {
        DIPROPDWORD Mode = { { sizeof(Mode), sizeof(DIPROPHEADER), 0, DIPH_DEVICE } };

        Device->GetProperty(DIPROP_AXISMODE, &Mode.diph);
        printf(" axismode %lu", Mode.dwData);
        for (UINT Index = 0; Index < Count; Index++)
        {
            DIDEVICEOBJECTINSTANCEW Info = { sizeof(Info) };

            if (SUCCEEDED(Device->GetObjectInfo(&Info, DcCustomObjects[Index].dwOfs, DIPH_BYOFFSET)))
                printf(" [%lu->%08lX]", DcCustomObjects[Index].dwOfs, Info.dwType);
            else
                printf(" [%lu->none]", DcCustomObjects[Index].dwOfs);
        }
    }
    printf("\n");
}

static
VOID
DcSetObject(
    _In_ UINT Index,
    _In_opt_ const GUID* Guid,
    _In_ DWORD Offset,
    _In_ DWORD Type,
    _In_ DWORD Flags)
{
    DcCustomObjects[Index].pguid = Guid;
    DcCustomObjects[Index].dwOfs = Offset;
    DcCustomObjects[Index].dwType = Type;
    DcCustomObjects[Index].dwFlags = Flags;
}

static
VOID
DcCheckFormats(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();

    if (Wide == nullptr)
        return;
    for (const GUID* Guid : DcReadDevices())
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);
        DIDATAFORMAT Format;

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr)
            continue;
        printf("  null = %08lX\n", Device->SetDataFormat(nullptr));
        Format = DcKeyboardFormat;
        Format.dwSize = 24;
        printf("  bad size = %08lX\n", Device->SetDataFormat(&Format));
        Format = DcKeyboardFormat;
        Format.dwObjSize = 16;
        printf("  bad objsize = %08lX\n", Device->SetDataFormat(&Format));
        Format = DcKeyboardFormat;
        Format.dwFlags = 3;
        printf("  flags 3 = %08lX\n", Device->SetDataFormat(&Format));
        Format.dwFlags = 4;
        printf("  flags 4 = %08lX\n", Device->SetDataFormat(&Format));
        Format = DcKeyboardFormat;
        Format.dwDataSize = 255;
        printf("  datasize 255 = %08lX\n", Device->SetDataFormat(&Format));
        Format = DcKeyboardFormat;
        Format.rgodf = nullptr;
        printf("  null objects = %08lX\n", Device->SetDataFormat(&Format));
        Format.dwNumObjs = 0;
        printf("  no objects = %08lX\n", Device->SetDataFormat(&Format));
        printf("  keyboard = %08lX\n", Device->SetDataFormat(&DcKeyboardFormat));
        printf("  mouse = %08lX\n", Device->SetDataFormat(&DcMouseFormat));
        printf("  mouse2 = %08lX\n", Device->SetDataFormat(&DcMouse2Format));
        printf("  joystick = %08lX\n", Device->SetDataFormat(&DcJoystickFormat));

        DcSetObject(0, &GUID_Key, 0, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(0x1E), 0);
        DcSetObject(1, nullptr, 4, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "key a then any", 0, 8, 2);
        DcSetObject(0, &GUID_XAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcSetObject(1, &GUID_YAxis, 4, DIDFT_RELAXIS | DIDFT_ANYINSTANCE, 0);
        DcSetObject(2, nullptr, 8, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "axes abs", DIDF_ABSAXIS, 12, 3);
        DcTryFormat(Device, "axes rel", DIDF_RELAXIS, 12, 3);
        DcTryFormat(Device, "axes none", 0, 12, 3);
        DcSetObject(0, &GUID_XAxis, 2, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "axis misaligned", 0, 12, 1);
        DcSetObject(0, &GUID_XAxis, 12, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "axis past end", 0, 12, 1);
        DcSetObject(0, &GUID_ZAxis, 0, DC_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcSetObject(1, &GUID_RxAxis, 4, DC_OPTIONAL | DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcSetObject(2, &GUID_POV, 8, DC_OPTIONAL | DIDFT_POV | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "optional", 0, 12, 3);
        DcSetObject(0, &GUID_RxAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "missing", 0, 4, 1);
        DcSetObject(0, nullptr, 0, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcSetObject(1, nullptr, 1, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcSetObject(2, nullptr, 2, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcSetObject(3, &GUID_Button, 3, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcSetObject(4, &GUID_Key, 4, DIDFT_PSHBUTTON | DIDFT_ANYINSTANCE, 0);
        DcSetObject(5, &GUID_NULL, 5, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "buttons", 0, 8, 6);
        DcSetObject(0, nullptr, 0, DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "no type", 0, 4, 1);
        DcSetObject(0, nullptr, 0, DIDFT_BUTTON | DIDFT_ANYINSTANCE, DIDOI_ASPECTPOSITION);
        DcTryFormat(Device, "aspect", 0, 4, 1);
        DcSetObject(0, &GUID_XAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, DIDOI_ASPECTVELOCITY);
        DcTryFormat(Device, "axis velocity", 0, 4, 1);
        DcSetObject(0, &GUID_XAxis, 0, DIDFT_AXIS | DIDFT_FFACTUATOR | DIDFT_ANYINSTANCE, 0);
        DcTryFormat(Device, "actuator", 0, 4, 1);
        DcSetObject(0, reinterpret_cast<const GUID*>(static_cast<ULONG_PTR>(0x00010030)), 0, DIDFT_AXIS | DIDFT_ANYINSTANCE,
                    DIDOI_GUIDISUSAGE);
        DcTryFormat(Device, "usage", 0, 4, 1);

        Device->SetDataFormat(IsEqualGUID(*Guid, GUID_SysKeyboard) || IsEqualGUID(*Guid, GUID_SysKeyboardEm2) ?
                              &DcKeyboardFormat : &DcMouse2Format);
        Device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        printf("  acquire = %08lX\n", Device->Acquire());
        printf("  format acquired = %08lX\n", Device->SetDataFormat(&DcKeyboardFormat));
        printf("  unacquire = %08lX\n", Device->Unacquire());
        Device->Release();
    }
    Wide->Release();
}

static
VOID
DcCheckCooperation(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    HWND Child;

    if (Wide == nullptr)
        return;
    Child = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD, 0, 0, 4, 4, DcWindow, nullptr, GetModuleHandleW(nullptr),
                            nullptr);
    for (const GUID* Guid : DcReadDevices())
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr)
            continue;
        for (DWORD Flags = 0; Flags < 0x22; Flags++)
        {
            HRESULT Null = Device->SetCooperativeLevel(nullptr, Flags);
            HRESULT Window = Device->SetCooperativeLevel(DcWindow, Flags);

            printf("  coop %#lx null %08lX window %08lX\n", Flags, Null, Window);
        }
        printf("  coop bad window = %08lX\n",
               Device->SetCooperativeLevel(reinterpret_cast<HWND>(static_cast<ULONG_PTR>(0x12345)), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
        printf("  coop child = %08lX\n", Device->SetCooperativeLevel(Child, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
        printf("  coop desktop = %08lX\n", Device->SetCooperativeLevel(GetDesktopWindow(), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));

        /* A foreground level needs the window to be in front when acquiring */
        Device->SetDataFormat(IsEqualGUID(*Guid, GUID_SysKeyboard) || IsEqualGUID(*Guid, GUID_SysKeyboardEm2) ?
                              &DcKeyboardFormat : &DcMouse2Format);
        printf("  coop fg = %08lX", Device->SetCooperativeLevel(DcWindow, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND));
        printf(" acquire %08lX\n", Device->Acquire());
        printf("  coop exclusive fg = %08lX", Device->SetCooperativeLevel(DcWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND));
        printf(" acquire %08lX\n", Device->Acquire());
        Device->Unacquire();
        Device->Release();
    }
    if (Child != nullptr)
        DestroyWindow(Child);
    Wide->Release();
}

static
VOID
DcCheckAcquire(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    HANDLE Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    if (Wide == nullptr)
        return;
    for (const GUID* Guid : DcReadDevices())
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);
        BOOLEAN Keyboard = IsEqualGUID(*Guid, GUID_SysKeyboard) || IsEqualGUID(*Guid, GUID_SysKeyboardEm2);
        BYTE State[512];
        DIDEVICEOBJECTDATA Data[8];
        DWORD Count;
        DIPROPDWORD Buffer = { { sizeof(Buffer), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 16 };

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr)
            continue;
        printf("  acquire no format = %08lX\n", Device->Acquire());
        printf("  state no format = %08lX\n", Device->GetDeviceState(sizeof(State), State));
        printf("  poll no format = %08lX\n", Device->Poll());
        printf("  unacquire idle = %08lX\n", Device->Unacquire());
        printf("  format = %08lX\n", Device->SetDataFormat(Keyboard ? &DcKeyboardFormat : &DcMouse2Format));
        printf("  state not acquired = %08lX\n", Device->GetDeviceState(Keyboard ? 256 : 20, State));
        Count = 8;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, 0);

            printf("  data not buffered = %08lX count %lu\n", Status, Count);
        }
        printf("  event = %08lX\n", Device->SetEventNotification(Event));
        printf("  event again = %08lX\n", Device->SetEventNotification(Event));
        printf("  event clear = %08lX\n", Device->SetEventNotification(nullptr));
        printf("  event bad = %08lX\n", Device->SetEventNotification(reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x1234))));
        printf("  event set = %08lX\n", Device->SetEventNotification(Event));
        printf("  buffer = %08lX\n", Device->SetProperty(DIPROP_BUFFERSIZE, &Buffer.diph));
        Count = 8;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, 0);

            printf("  data not acquired = %08lX count %lu\n", Status, Count);
        }
        printf("  coop = %08lX\n", Device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
        printf("  acquire = %08lX\n", Device->Acquire());
        printf("  acquire again = %08lX\n", Device->Acquire());
        {
            RAWINPUTDEVICE Registered[8];
            UINT RawCount = ARRAYSIZE(Registered);
            UINT Found = GetRegisteredRawInputDevices(Registered, &RawCount, sizeof(Registered[0]));

            printf("  raw input registered %d", static_cast<int>(Found));
            for (UINT Index = 0; Index < Found && Index < ARRAYSIZE(Registered); Index++)
            {
                printf(" [%04X:%04X flags %08lX target %s]", Registered[Index].usUsagePage, Registered[Index].usUsage,
                       Registered[Index].dwFlags, Registered[Index].hwndTarget != nullptr ? "window" : "none");
            }
            printf("\n");
        }
        printf("  poll = %08lX\n", Device->Poll());
        printf("  event while acquired = %08lX\n", Device->SetEventNotification(nullptr));
        printf("  coop while acquired = %08lX\n", Device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
        printf("  buffer while acquired = %08lX\n", Device->SetProperty(DIPROP_BUFFERSIZE, &Buffer.diph));
        for (DWORD Size : { 0ul, 4ul, 16ul, 20ul, 256ul, 257ul })
            printf("  state %lu = %08lX\n", Size, Device->GetDeviceState(Size, State));
        memset(State, 0xCC, sizeof(State));
        printf("  state = %08lX\n", Device->GetDeviceState(Keyboard ? 256 : 20, State));
        if (!Keyboard)
            DcBytes("    mouse", State, 20);
        for (DWORD Size : { 0ul, 16ul, 20ul, 24ul, 28ul })
        {
            Count = 8;
            printf("  data size %lu = %08lX count %lu\n", Size, Device->GetDeviceData(Size, Data, &Count, 0), Count);
        }
        for (DWORD Flags : { 0ul, 1ul, 2ul, 3ul, 4ul })
        {
            HRESULT Status;

            Count = 8;
            Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, Flags);
            printf("  data flags %lu = %08lX count %lu\n", Flags, Status, Count);
        }
        Count = INFINITE;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), nullptr, &Count, 0);

            printf("  data flush = %08lX count %lu\n", Status, Count);
        }
        Count = 0;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, 0);

            printf("  data zero = %08lX count %lu\n", Status, Count);
        }
        printf("  send = %08lX\n", Device->SendDeviceData(sizeof(Data[0]), Data, &Count, 0));
        printf("  unacquire = %08lX\n", Device->Unacquire());
        printf("  unacquire again = %08lX\n", Device->Unacquire());
        printf("  poll after = %08lX\n", Device->Poll());
        printf("  state after = %08lX\n", Device->GetDeviceState(Keyboard ? 256 : 20, State));
        Count = 8;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, 0);

            printf("  data after = %08lX count %lu\n", Status, Count);
        }
        Count = 8;
        {
            HRESULT Status = Device->GetDeviceData(sizeof(Data[0]), Data, &Count, 2);

            printf("  data after residual = %08lX count %lu\n", Status, Count);
        }
        printf("  coop exclusive bg = %08lX", Device->SetCooperativeLevel(DcWindow, DISCL_EXCLUSIVE | DISCL_BACKGROUND));
        printf(" acquire %08lX", Device->Acquire());
        printf(" unacquire %08lX\n", Device->Unacquire());
        Device->Release();
    }
    CloseHandle(Event);
    Wide->Release();
}

static const struct
{
    const GUID* Property;
    PCSTR Name;
    DWORD Size;
} DcProperties[] =
{
    { &DIPROP_BUFFERSIZE, "buffersize", sizeof(DIPROPDWORD) },
    { &DIPROP_AXISMODE, "axismode", sizeof(DIPROPDWORD) },
    { &DIPROP_GRANULARITY, "granularity", sizeof(DIPROPDWORD) },
    { &DIPROP_RANGE, "range", sizeof(DIPROPRANGE) },
    { &DIPROP_DEADZONE, "deadzone", sizeof(DIPROPDWORD) },
    { &DIPROP_SATURATION, "saturation", sizeof(DIPROPDWORD) },
    { &DIPROP_FFGAIN, "ffgain", sizeof(DIPROPDWORD) },
    { &DIPROP_FFLOAD, "ffload", sizeof(DIPROPDWORD) },
    { &DIPROP_AUTOCENTER, "autocenter", sizeof(DIPROPDWORD) },
    { &DIPROP_CALIBRATIONMODE, "calibrationmode", sizeof(DIPROPDWORD) },
    { &DIPROP_CALIBRATION, "calibration", sizeof(DIPROPCAL) },
    { &DIPROP_GUIDANDPATH, "guidandpath", sizeof(DIPROPGUIDANDPATH) },
    { &DIPROP_INSTANCENAME, "instancename", sizeof(DIPROPSTRING) },
    { &DIPROP_PRODUCTNAME, "productname", sizeof(DIPROPSTRING) },
    { &DIPROP_JOYSTICKID, "joystickid", sizeof(DIPROPDWORD) },
    { &DIPROP_GETPORTDISPLAYNAME, "portdisplayname", sizeof(DIPROPSTRING) },
    { &DIPROP_PHYSICALRANGE, "physicalrange", sizeof(DIPROPRANGE) },
    { &DIPROP_LOGICALRANGE, "logicalrange", sizeof(DIPROPRANGE) },
    { &DIPROP_KEYNAME, "keyname", sizeof(DIPROPSTRING) },
    { &DIPROP_CPOINTS, "cpoints", sizeof(DIPROPCPOINTS) },
    { &DIPROP_APPDATA, "appdata", sizeof(DIPROPPOINTER) },
    { &DIPROP_SCANCODE, "scancode", sizeof(DIPROPDWORD) },
    { &DIPROP_VIDPID, "vidpid", sizeof(DIPROPDWORD) },
    { &DIPROP_USERNAME, "username", sizeof(DIPROPSTRING) },
    { &DIPROP_TYPENAME, "typename", sizeof(DIPROPSTRING) },
    { &MAKEDIPROP(17), "prop17", sizeof(DIPROPDWORD) },
    { &MAKEDIPROP(27), "prop27", sizeof(DIPROPDWORD) },
    { &MAKEDIPROP(0xFFFF), "propffff", sizeof(DIPROPDWORD) },
    { &MAKEDIPROP(0xFFFD), "propfffd", sizeof(DIPROPSTRING) },
    { &MAKEDIPROP(0xFFFB), "propfffb", sizeof(DIPROPDWORD) },
    { &MAKEDIPROP(0), "prop0", sizeof(DIPROPDWORD) },
};

static
VOID
DcPrintProperty(
    _In_ const BYTE* Buffer,
    _In_ DWORD Size)
{
    if (Size == sizeof(DIPROPSTRING) || Size == sizeof(DIPROPGUIDANDPATH))
    {
        const DIPROPSTRING* String = reinterpret_cast<const DIPROPSTRING*>(Buffer);

        printf(" '%s'", DcWide(String->wsz).c_str());
        return;
    }
    printf(" =");
    for (DWORD Offset = sizeof(DIPROPHEADER); Offset < Size; Offset += 4)
        printf(" %08lX", *reinterpret_cast<const DWORD*>(Buffer + Offset));
}

static
VOID
DcCheckProperties(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();

    if (Wide == nullptr)
        return;
    for (const GUID* Guid : DcDataDevices)
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);
        BOOLEAN Keyboard = IsEqualGUID(*Guid, GUID_SysKeyboard) || IsEqualGUID(*Guid, GUID_SysKeyboardEm2);
        static const struct
        {
            DWORD How;
            DWORD Object;
        } Targets[] =
        {
            { DIPH_DEVICE, 0 }, { DIPH_BYID, 0x0102 }, { DIPH_BYID, 0x0201 }, { DIPH_BYID, 0x1E04 }, { DIPH_BYID, 0x0304 },
            { DIPH_BYOFFSET, 0 }, { DIPH_BYOFFSET, 30 }, { DIPH_DEVICE, 1 }, { DIPH_BYUSAGE, 0x00010030 }, { 5, 0 },
        };

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr)
            continue;
        for (int Pass = 0; Pass < 2; Pass++)
        {
            for (const auto& Property : DcProperties)
            {
                for (const auto& Target : Targets)
                {
                    BYTE Buffer[1024];
                    LPDIPROPHEADER Header = reinterpret_cast<LPDIPROPHEADER>(Buffer);
                    HRESULT Result;

                    memset(Buffer, 0xCC, sizeof(Buffer));
                    Header->dwSize = Property.Size;
                    Header->dwHeaderSize = sizeof(DIPROPHEADER);
                    Header->dwObj = Target.Object;
                    Header->dwHow = Target.How;
                    Result = Device->GetProperty(*Property.Property, Header);
                    printf("  get%s %s how %lu obj %#lx = %08lX", Pass ? " formatted" : "", Property.Name, Target.How,
                           Target.Object, Result);
                    if (SUCCEEDED(Result))
                        DcPrintProperty(Buffer, Property.Size);
                    printf("\n");
                }
            }
            Device->SetDataFormat(Keyboard ? &DcKeyboardFormat : &DcMouse2Format);
        }
        {
            BYTE Buffer[64];
            LPDIPROPHEADER Header = reinterpret_cast<LPDIPROPHEADER>(Buffer);

            Header->dwHeaderSize = 12;
            Header->dwSize = sizeof(DIPROPDWORD);
            Header->dwObj = 0;
            Header->dwHow = DIPH_DEVICE;
            printf("  get bad header = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, Header));
            Header->dwHeaderSize = sizeof(DIPROPHEADER);
            Header->dwSize = 22;
            printf("  get odd size = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, Header));
            Header->dwSize = 24;
            printf("  get big size = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, Header));
            printf("  get null = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, nullptr));
            Header->dwSize = sizeof(DIPROPDWORD);
            printf("  get real guid = %08lX\n", Device->GetProperty(GUID_XAxis, Header));
        }

        /* Values that can be written, and what reading them back gives */
        {
            static const struct
            {
                const GUID* Property;
                PCSTR Name;
                DWORD How;
                DWORD Object;
                DWORD Value;
            } Writes[] =
            {
                { &DIPROP_BUFFERSIZE, "buffersize", DIPH_DEVICE, 0, 32 },
                { &DIPROP_BUFFERSIZE, "buffersize", DIPH_DEVICE, 0, 5000 },
                { &DIPROP_BUFFERSIZE, "buffersize", DIPH_BYID, 0x0102, 8 },
                { &DIPROP_AXISMODE, "axismode", DIPH_DEVICE, 0, DIPROPAXISMODE_REL },
                { &DIPROP_AXISMODE, "axismode", DIPH_DEVICE, 0, 2 },
                { &DIPROP_AXISMODE, "axismode", DIPH_DEVICE, 0, DIPROPAXISMODE_ABS },
                { &DIPROP_DEADZONE, "deadzone", DIPH_DEVICE, 0, 1000 },
                { &DIPROP_DEADZONE, "deadzone", DIPH_BYID, 0x0102, 1000 },
                { &DIPROP_SATURATION, "saturation", DIPH_DEVICE, 0, 9000 },
                { &DIPROP_FFGAIN, "ffgain", DIPH_DEVICE, 0, 5000 },
                { &DIPROP_FFGAIN, "ffgain", DIPH_DEVICE, 0, 70000 },
                { &DIPROP_AUTOCENTER, "autocenter", DIPH_DEVICE, 0, 0 },
                { &DIPROP_CALIBRATIONMODE, "calibrationmode", DIPH_DEVICE, 0, 1 },
                { &DIPROP_GRANULARITY, "granularity", DIPH_BYID, 0x0102, 5 },
                { &DIPROP_JOYSTICKID, "joystickid", DIPH_DEVICE, 0, 1 },
                { &DIPROP_SCANCODE, "scancode", DIPH_BYID, 0x1E04, 5 },
                { &DIPROP_FFLOAD, "ffload", DIPH_DEVICE, 0, 5 },
                { &MAKEDIPROP(0xFFFF), "propffff", DIPH_DEVICE, 0, 4 },
                { &DIPROP_BUFFERSIZE, "buffersize", DIPH_DEVICE, 0, 32 },
            };

            for (const auto& Write : Writes)
            {
                DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), Write.Object, Write.How }, Write.Value };
                HRESULT Result = Device->SetProperty(*Write.Property, &Value.diph);

                Value.dwData = 0xCCCCCCCC;
                printf("  set %s how %lu obj %#lx value %lu = %08lX", Write.Name, Write.How, Write.Object, Write.Value,
                       Result);
                printf(" get %08lX", Device->GetProperty(*Write.Property, &Value.diph));
                printf(" %lu\n", Value.dwData);
            }
        }
        {
            DIPROPRANGE Range = { { sizeof(Range), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, -100, 100 };
            DIPROPPOINTER Pointer = { { sizeof(Pointer), sizeof(DIPROPHEADER), Keyboard ? 30ul : 0ul, DIPH_BYOFFSET }, 0x1234 };
            DIPROPSTRING String = { { sizeof(String), sizeof(DIPROPHEADER), 0x1E04, DIPH_BYID } };

            printf("  set range device = %08lX\n", Device->SetProperty(DIPROP_RANGE, &Range.diph));
            Range.diph.dwHow = DIPH_BYID;
            Range.diph.dwObj = 0x0102;
            printf("  set range object = %08lX\n", Device->SetProperty(DIPROP_RANGE, &Range.diph));
            printf("  set appdata = %08lX", Device->SetProperty(DIPROP_APPDATA, &Pointer.diph));
            Pointer.uData = 0;
            {
                HRESULT Status = Device->GetProperty(DIPROP_APPDATA, &Pointer.diph);

                printf(" get %08lX %#llx\n", Status, static_cast<unsigned long long>(Pointer.uData));
            }
            wcscpy_s(String.wsz, L"x");
            printf("  set keyname = %08lX\n", Device->SetProperty(DIPROP_KEYNAME, &String.diph));
            String.diph.dwHow = DIPH_DEVICE;
            String.diph.dwObj = 0;
            printf("  set username = %08lX\n", Device->SetProperty(DIPROP_USERNAME, &String.diph));
            printf("  set instancename = %08lX\n", Device->SetProperty(DIPROP_INSTANCENAME, &String.diph));
            printf("  set productname = %08lX\n", Device->SetProperty(DIPROP_PRODUCTNAME, &String.diph));
        }
        {
            DIPROPDWORD Value = { { sizeof(Value), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 8 };

            Device->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            printf("  acquire = %08lX\n", Device->Acquire());
            printf("  set buffersize acquired = %08lX\n", Device->SetProperty(DIPROP_BUFFERSIZE, &Value.diph));
            printf("  set axismode acquired = %08lX\n", Device->SetProperty(DIPROP_AXISMODE, &Value.diph));
            Value.dwData = 0;
            {
                HRESULT Status = Device->GetProperty(DIPROP_BUFFERSIZE, &Value.diph);

                printf("  get buffersize acquired = %08lX %lu\n", Status, Value.dwData);
            }
            Value.diph.dwHow = DIPH_BYID;
            Value.diph.dwObj = 0x0102;
            {
                HRESULT Status = Device->GetProperty(DIPROP_GRANULARITY, &Value.diph);

                printf("  get granularity acquired = %08lX %lu\n", Status, Value.dwData);
            }
            Device->Unacquire();
        }
        Device->Release();
    }
    Wide->Release();
}

/* Reads every property from each HID device and a few of its objects, without setting any */
static
VOID
DcCheckHidProperties(VOID)
{
    const std::vector<const GUID*>& Devices = DcReadDevices();
    IDirectInput8W* Wide = DcCreateWide();

    if (Wide == nullptr)
        return;
    for (SIZE_T Index = ARRAYSIZE(DcDataDevices); Index < Devices.size(); Index++)
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Devices[Index]);
        std::vector<std::pair<DWORD, DWORD>> Targets = { { DIPH_DEVICE, 0 } };
        std::vector<DIDEVICEOBJECTINSTANCEW> Objects;

        printf("device %s\n", DcGuid(Devices[Index]).c_str());
        if (Device == nullptr)
            continue;
        Device->EnumObjects([](LPCDIDEVICEOBJECTINSTANCEW Info, VOID* Context) -> BOOL
        {
            auto* List = static_cast<std::vector<DIDEVICEOBJECTINSTANCEW>*>(Context);

            List->push_back(*Info);
            return List->size() < 4 ? DIENUM_CONTINUE : DIENUM_STOP;
        }, &Objects, DIDFT_ALL);
        for (const auto& Object : Objects)
        {
            Targets.push_back({ DIPH_BYID, Object.dwType });
            Targets.push_back({ DIPH_BYUSAGE, static_cast<DWORD>(MAKELONG(Object.wUsage, Object.wUsagePage)) });
        }
        for (int Pass = 0; Pass < 2; Pass++)
        {
            if (Pass != 0)
            {
                printf("  format = %08lX\n", Device->SetDataFormat(&DcJoystickFormat));
                Targets.push_back({ DIPH_BYOFFSET, 0 });
                Targets.push_back({ DIPH_BYOFFSET, 48 });
            }
            for (const auto& Property : DcProperties)
            {
                /* The private ids crash the system library on some objects */
                if (strncmp(Property.Name, "prop", 4) == 0)
                    continue;
                for (const auto& Target : Targets)
                {
                    BYTE Buffer[1024];
                    LPDIPROPHEADER Header = reinterpret_cast<LPDIPROPHEADER>(Buffer);
                    HRESULT Result;

                    memset(Buffer, 0xCC, sizeof(Buffer));
                    Header->dwSize = Property.Size;
                    Header->dwHeaderSize = sizeof(DIPROPHEADER);
                    Header->dwObj = Target.second;
                    Header->dwHow = Target.first;
                    Result = Device->GetProperty(*Property.Property, Header);
                    printf("  get%s %s how %lu obj %#lx = %08lX", Pass ? " formatted" : "", Property.Name, Target.first,
                           Target.second, Result);
                    if (SUCCEEDED(Result))
                        DcPrintProperty(Buffer, Property.Size);
                    printf("\n");
                }
            }
        }
        Device->Release();
    }
    Wide->Release();
}

static
VOID
DcCheckMisc(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();

    if (Wide == nullptr)
        return;
    for (const GUID* Guid : DcDataDevices)
    {
        IDirectInputDevice8W* Device = DcOpen(Wide, Guid);
        IDirectInputEffect* Effect = reinterpret_cast<IDirectInputEffect*>(DC_SENTINEL);
        DWORD State = 0xCCCCCCCC;
        DIEFFECTINFOW Info = { sizeof(Info) };
        DIEFFESCAPE Escape = { sizeof(Escape) };
        DWORD Count = 0;

        printf("device %s\n", DcGuid(Guid).c_str());
        if (Device == nullptr)
            continue;
        printf("  create effect = %08lX", Device->CreateEffect(GUID_ConstantForce, nullptr, &Effect, nullptr));
        printf(" %s\n", DcPointer(Effect));
        printf("  enum effects = %08lX\n", Device->EnumEffects(
            [](LPCDIEFFECTINFOW, VOID*) -> BOOL { return DIENUM_CONTINUE; }, nullptr, DIEFT_ALL));
        printf("  enum effects null = %08lX\n", Device->EnumEffects(nullptr, nullptr, DIEFT_ALL));
        printf("  effect info = %08lX\n", Device->GetEffectInfo(&Info, GUID_ConstantForce));
        {
            HRESULT Status = Device->GetForceFeedbackState(&State);

            printf("  ff state = %08lX %08lX\n", Status, State);
        }
        printf("  ff command = %08lX\n", Device->SendForceFeedbackCommand(DISFFC_RESET));
        printf("  enum created = %08lX\n", Device->EnumCreatedEffectObjects(
            [](LPDIRECTINPUTEFFECT, VOID*) -> BOOL { return DIENUM_CONTINUE; }, nullptr, 0));
        printf("  escape = %08lX\n", Device->Escape(&Escape));
        printf("  send = %08lX\n", Device->SendDeviceData(sizeof(DIDEVICEOBJECTDATA), nullptr, &Count, 0));
        printf("  enum file = %08lX\n", Device->EnumEffectsInFile(L"c:\\nonexistent\\file.ffe",
            [](LPCDIFILEEFFECT, VOID*) -> BOOL { return DIENUM_CONTINUE; }, nullptr, 0));
        Device->Release();
    }
    Wide->Release();
}

/* Prints what the keyboard and mouse report for a while; only run by hand, its output depends on the user */
static
VOID
DcWatchInput(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInputDevice8W* Devices[2] = {};
    DIPROPDWORD Buffer = { { sizeof(Buffer), sizeof(DIPROPHEADER), 0, DIPH_DEVICE }, 64 };

    if (Wide == nullptr)
        return;
    Wide->CreateDevice(GUID_SysKeyboard, &Devices[0], nullptr);
    Wide->CreateDevice(GUID_SysMouse, &Devices[1], nullptr);
    for (UINT Index = 0; Index < 2; Index++)
    {
        if (Devices[Index] == nullptr)
            continue;
        Devices[Index]->SetDataFormat(Index == 0 ? &DcKeyboardFormat : &DcMouse2Format);
        Devices[Index]->SetCooperativeLevel(nullptr, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
        Devices[Index]->SetProperty(DIPROP_BUFFERSIZE, &Buffer.diph);
        printf("watch acquire %u = %08lX\n", Index, Devices[Index]->Acquire());
    }
    for (UINT Tick = 0; Tick < 100; Tick++)
    {
        Sleep(100);
        for (UINT Index = 0; Index < 2; Index++)
        {
            DIDEVICEOBJECTDATA Data[64];
            DWORD Count = ARRAYSIZE(Data);
            HRESULT Result;

            if (Devices[Index] == nullptr)
                continue;
            Result = Devices[Index]->GetDeviceData(sizeof(Data[0]), Data, &Count, 0);
            for (DWORD Event = 0; Event < Count; Event++)
            {
                printf("  %s %08lX ofs %lu data %ld time %lu seq %lu\n", Index == 0 ? "key" : "mouse", Result,
                       Data[Event].dwOfs, static_cast<LONG>(Data[Event].dwData), Data[Event].dwTimeStamp,
                       Data[Event].dwSequence);
            }
        }
        if (Devices[1] != nullptr && Tick % 10 == 9)
        {
            DIMOUSESTATE2 State = {};

            printf("  mouse state %08lX", Devices[1]->GetDeviceState(sizeof(State), &State));
            printf(" %ld %ld %ld %02X %02X %02X\n", State.lX, State.lY, State.lZ, State.rgbButtons[0], State.rgbButtons[1],
                   State.rgbButtons[2]);
        }
    }
    for (UINT Index = 0; Index < 2; Index++)
    {
        if (Devices[Index] != nullptr)
            Devices[Index]->Release();
    }
    Wide->Release();
}

VOID
DcRunDataChecks(
    _In_z_ PCSTR Name)
{
    if (strcmp(Name, "watch") == 0)
        DcWatchInput();
    if (strcmp(Name, "objects") == 0)
        DcCheckObjects();
    else if (strcmp(Name, "formats") == 0)
        DcCheckFormats();
    else if (strcmp(Name, "coop") == 0)
        DcCheckCooperation();
    else if (strcmp(Name, "acquire") == 0)
        DcCheckAcquire();
    else if (strcmp(Name, "props") == 0)
        DcCheckProperties();
    else if (strcmp(Name, "hidprops") == 0)
        DcCheckHidProperties();
    else if (strcmp(Name, "misc") == 0)
        DcCheckMisc();
}
