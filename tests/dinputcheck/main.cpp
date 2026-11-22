/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectInput answers across interfaces and argument shapes, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dinputcheck [-runtime system|reactx|<path>]
 */

#define DIRECTINPUT_VERSION 0x0700

#include <windows.h>
#include <initguid.h>
#include <dinput.h>
#include <dinputd.h>

#include <cstdio>
#include <string>

typedef HRESULT (WINAPI* PDC_CREATE)(HINSTANCE, DWORD, PVOID*, LPUNKNOWN);
typedef HRESULT (WINAPI* PDC_CREATE_EX)(HINSTANCE, DWORD, REFIID, PVOID*, LPUNKNOWN);
typedef HRESULT (WINAPI* PDC_GET_CLASS)(REFCLSID, REFIID, PVOID*);
typedef HRESULT (WINAPI* PDC_CAN_UNLOAD)(VOID);

static PDC_CREATE DcCreateA;
static PDC_CREATE DcCreateW;
static PDC_CREATE_EX DcCreateEx;
static PDC_GET_CLASS DcGetClassObject;
static PDC_CAN_UNLOAD DcCanUnloadNow;
static HINSTANCE DcSelf;
static HWND DcWindow;
static HWND DcChild;
static PCWSTR DcScratch;

typedef struct _DC_NAMED_IID
{
    PCSTR Name;
    const IID* Iid;
} DC_NAMED_IID;

static const DC_NAMED_IID DcInputIids[] =
{
    { "IUnknown", &IID_IUnknown },
    { "IDirectInputA", &IID_IDirectInputA },
    { "IDirectInputW", &IID_IDirectInputW },
    { "IDirectInput2A", &IID_IDirectInput2A },
    { "IDirectInput2W", &IID_IDirectInput2W },
    { "IDirectInput7A", &IID_IDirectInput7A },
    { "IDirectInput7W", &IID_IDirectInput7W },
    { "IDirectInputDeviceW", &IID_IDirectInputDeviceW },
    { "IDirectInputDevice2A", &IID_IDirectInputDevice2A },
    { "IDirectInputDevice7W", &IID_IDirectInputDevice7W },
    { "IDirectInputEffect", &IID_IDirectInputEffect },
    { "IClassFactory", &IID_IClassFactory },
};

typedef struct _DC_NAMED_GUID
{
    PCSTR Name;
    const GUID* Guid;
} DC_NAMED_GUID;

static const GUID DcNullGuid = {};
static const GUID DcBogusGuid = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };

/* GUID_Joystick with a joystick id in the low nibble */
static const GUID DcJoystick0 = { 0x6F1D2B70, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };

static const DC_NAMED_GUID DcDevices[] =
{
    { "SysMouse", &GUID_SysMouse },
    { "SysMouseEm", &GUID_SysMouseEm },
    { "SysMouseEm2", &GUID_SysMouseEm2 },
    { "SysKeyboard", &GUID_SysKeyboard },
    { "SysKeyboardEm", &GUID_SysKeyboardEm },
    { "SysKeyboardEm2", &GUID_SysKeyboardEm2 },
    { "Joystick0", &DcJoystick0 },
    { "Bogus", &DcBogusGuid },
    { "Null", &DcNullGuid },
};

static const DWORD DcVersions[] =
{
    0, 0x0100, 0x0300, 0x0301, 0x0400, 0x0500, 0x050A, 0x05B2, 0x0600, 0x0602, 0x061A, 0x0700, 0x0701, 0x0800,
};

static
VOID
DcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    printf("%s =", Name);
    for (SIZE_T Index = 0; Index < Size; Index++)
        printf(" %02X", static_cast<const BYTE*>(Data)[Index]);
    printf("\n");
}

static
std::string
DcGuid(
    _In_ const GUID& Guid)
{
    CHAR Text[64];

    sprintf_s(Text, "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", Guid.Data1, Guid.Data2, Guid.Data3,
              Guid.Data4[0], Guid.Data4[1], Guid.Data4[2], Guid.Data4[3], Guid.Data4[4], Guid.Data4[5],
              Guid.Data4[6], Guid.Data4[7]);
    return Text;
}

static
std::wstring
DcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"dinput.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(nullptr, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\dinput.dll";
    }

    return Runtime;
}

/* Releases an interface and prints what Release said */
static
VOID
DcRelease(
    _In_z_ PCSTR Name,
    _In_opt_ IUnknown* Object)
{
    if (Object != nullptr)
        printf("%s release = %lu\n", Name, Object->Release());
}

static
VOID
DcQueryMatrix(
    _In_z_ PCSTR Prefix,
    _In_ IUnknown* Object,
    _In_ PVOID AsA,
    _In_ PVOID AsW)
{
    for (ULONG Index = 0; Index < _countof(DcInputIids); Index++)
    {
        PVOID Found = reinterpret_cast<PVOID>(1);
        HRESULT hr = Object->QueryInterface(*DcInputIids[Index].Iid, &Found);
        PCSTR Which = "other";

        if (Found == nullptr)
            Which = "null";
        else if (Found == AsA)
            Which = "A";
        else if (Found == AsW)
            Which = "W";
        else if (Found == reinterpret_cast<PVOID>(1))
            Which = "untouched";

        printf("%s qi %s = %08lX %s\n", Prefix, DcInputIids[Index].Name, hr, Which);
        if (SUCCEEDED(hr) && Found != nullptr)
            static_cast<IUnknown*>(Found)->Release();
    }

    printf("%s qi null out = %08lX\n", Prefix, Object->QueryInterface(IID_IUnknown, nullptr));
    printf("%s addref = %lu\n", Prefix, Object->AddRef());
    printf("%s release = %lu\n", Prefix, Object->Release());
}

static
VOID
DcCheckCreate(VOID)
{
    for (ULONG Index = 0; Index < _countof(DcVersions); Index++)
    {
        PVOID Object = reinterpret_cast<PVOID>(1);
        HRESULT hr;

        hr = DcCreateA(DcSelf, DcVersions[Index], &Object, nullptr);
        printf("createA ver %04lX = %08lX %s\n", DcVersions[Index], hr, Object ? "set" : "null");
        if (SUCCEEDED(hr))
            DcRelease("  createA", static_cast<IUnknown*>(Object));

        Object = reinterpret_cast<PVOID>(1);
        hr = DcCreateW(nullptr, DcVersions[Index], &Object, nullptr);
        printf("createW nullinst ver %04lX = %08lX %s\n", DcVersions[Index], hr, Object ? "set" : "null");
        if (SUCCEEDED(hr))
            DcRelease("  createW", static_cast<IUnknown*>(Object));
    }

    printf("createA null out = %08lX\n", DcCreateA(DcSelf, 0x0700, nullptr, nullptr));
    printf("createA bad instance = %08lX\n", DcCreateA(reinterpret_cast<HINSTANCE>(0x10), 0x0700, nullptr, nullptr));
    {
        PVOID Object = reinterpret_cast<PVOID>(1);
        HRESULT hr = DcCreateA(reinterpret_cast<HINSTANCE>(0x10), 0x0700, &Object, nullptr);

        printf("createA bad instance out = %08lX %s\n", hr, Object ? "set" : "null");
    }

    for (ULONG Index = 0; Index < _countof(DcInputIids); Index++)
    {
        PVOID Object = reinterpret_cast<PVOID>(1);
        HRESULT hr = DcCreateEx(DcSelf, 0x0700, *DcInputIids[Index].Iid, &Object, nullptr);

        printf("createEx %s = %08lX %s\n", DcInputIids[Index].Name, hr, Object == reinterpret_cast<PVOID>(1) ? "untouched" : Object ? "set" : "null");
        if (SUCCEEDED(hr))
            DcRelease("  createEx", static_cast<IUnknown*>(Object));
    }

    printf("cancanunload = %08lX\n", DcCanUnloadNow());
}

static
VOID
DcCheckFactory(VOID)
{
    static const DC_NAMED_GUID Classes[] =
    {
        { "CLSID_DirectInput", &CLSID_DirectInput },
        { "CLSID_DirectInputDevice", &CLSID_DirectInputDevice },
        { "CLSID_DirectInput8", &CLSID_DirectInput8 },
        { "Bogus", &DcBogusGuid },
    };

    for (ULONG Index = 0; Index < _countof(Classes); Index++)
    {
        IClassFactory* Factory = nullptr;
        PVOID Object = nullptr;
        HRESULT hr;

        hr = DcGetClassObject(*Classes[Index].Guid, IID_IClassFactory, reinterpret_cast<PVOID*>(&Factory));
        printf("getclassobject %s = %08lX %s\n", Classes[Index].Name, hr, Factory ? "set" : "null");
        if (FAILED(hr))
            continue;

        hr = DcGetClassObject(*Classes[Index].Guid, IID_IUnknown, &Object);
        printf("  getclassobject iunknown = %08lX\n", hr);
        DcRelease("  unknown", static_cast<IUnknown*>(Object));
        Object = reinterpret_cast<PVOID>(1);
        hr = DcGetClassObject(*Classes[Index].Guid, IID_IDirectInputA, &Object);
        printf("  getclassobject dinputA = %08lX %s\n", hr, Object ? "set" : "null");

        printf("  createinstance null out = %08lX\n", Factory->CreateInstance(nullptr, IID_IUnknown, nullptr));
        Object = reinterpret_cast<PVOID>(1);
        hr = Factory->CreateInstance(reinterpret_cast<IUnknown*>(Factory), IID_IUnknown, &Object);
        printf("  createinstance aggregate = %08lX %s\n", hr, Object ? "set" : "null");

        for (ULONG Iid = 0; Iid < _countof(DcInputIids); Iid++)
        {
            Object = reinterpret_cast<PVOID>(1);
            hr = Factory->CreateInstance(nullptr, *DcInputIids[Iid].Iid, &Object);
            printf("  createinstance %s = %08lX %s\n", DcInputIids[Iid].Name, hr, Object ? "set" : "null");
            if (SUCCEEDED(hr))
                DcRelease("    instance", static_cast<IUnknown*>(Object));
        }

        printf("  lockserver = %08lX\n", Factory->LockServer(TRUE));
        printf("  cancanunload locked = %08lX\n", DcCanUnloadNow());
        printf("  unlockserver = %08lX\n", Factory->LockServer(FALSE));
        DcRelease("  factory", Factory);
    }

    /* A DirectInput object made by the factory starts uninitialized */
    {
        IDirectInputW* Input = nullptr;
        IDirectInputDeviceW* Device = nullptr;
        IClassFactory* Factory = nullptr;
        HRESULT hr;

        DcGetClassObject(CLSID_DirectInput, IID_IClassFactory, reinterpret_cast<PVOID*>(&Factory));
        hr = Factory->CreateInstance(nullptr, IID_IDirectInputW, reinterpret_cast<PVOID*>(&Input));
        printf("factory dinput = %08lX\n", hr);
        printf("  uninit createdevice = %08lX\n", Input->CreateDevice(GUID_SysKeyboard, &Device, nullptr));
        printf("  uninit getdevicestatus = %08lX\n", Input->GetDeviceStatus(GUID_SysKeyboard));
        printf("  uninit runcontrolpanel flags = %08lX\n", Input->RunControlPanel(nullptr, 1));
        for (ULONG Index = 0; Index < _countof(DcVersions); Index++)
            printf("  initialize %04lX = %08lX\n", DcVersions[Index], Input->Initialize(DcSelf, DcVersions[Index]));
        printf("  initialize nullinst 300 = %08lX\n", Input->Initialize(nullptr, 0x0300));
        printf("  initialize nullinst 500 = %08lX\n", Input->Initialize(nullptr, 0x0500));
        DcRelease("  dinput", Input);
        DcRelease("  factory", Factory);
    }
}

typedef struct _DC_ENUM_CONTEXT
{
    ULONG Count;
    ULONG StopAfter;
} DC_ENUM_CONTEXT;

static
VOID
DcPrintDeviceW(
    _In_ const DIDEVICEINSTANCEW* Info)
{
    printf("    size %lu inst %s prod %s type %08lX inst '%ls' prod '%ls' ff %s page %04X usage %04X\n",
           Info->dwSize, DcGuid(Info->guidInstance).c_str(), DcGuid(Info->guidProduct).c_str(), Info->dwDevType,
           Info->tszInstanceName, Info->tszProductName, DcGuid(Info->guidFFDriver).c_str(), Info->wUsagePage,
           Info->wUsage);
}

static
BOOL
CALLBACK
DcEnumDevicesW(
    _In_ LPCDIDEVICEINSTANCEW Info,
    _In_ LPVOID Context)
{
    DC_ENUM_CONTEXT* State = static_cast<DC_ENUM_CONTEXT*>(Context);

    DcPrintDeviceW(Info);
    return ++State->Count < State->StopAfter;
}

static
BOOL
CALLBACK
DcEnumDevicesA(
    _In_ LPCDIDEVICEINSTANCEA Info,
    _In_ LPVOID Context)
{
    DC_ENUM_CONTEXT* State = static_cast<DC_ENUM_CONTEXT*>(Context);

    printf("    size %lu inst %s prod %s type %08lX inst '%s' prod '%s' ff %s page %04X usage %04X\n",
           Info->dwSize, DcGuid(Info->guidInstance).c_str(), DcGuid(Info->guidProduct).c_str(), Info->dwDevType,
           Info->tszInstanceName, Info->tszProductName, DcGuid(Info->guidFFDriver).c_str(), Info->wUsagePage,
           Info->wUsage);
    return ++State->Count < State->StopAfter;
}

static
VOID
DcCheckEnumDevices(
    _In_ DWORD Version)
{
    static const DWORD Types[] = { 0, 1, 2, 3, 4, 5, DIDEVTYPE_HID, DIDEVTYPE_HID | 2, DIDEVTYPE_HID | 4, 0x100, 0x20000 };
    static const DWORD Flags[] = { 0, DIEDFL_ATTACHEDONLY, DIEDFL_FORCEFEEDBACK, DIEDFL_INCLUDEALIASES,
                                   DIEDFL_INCLUDEPHANTOMS, DIEDFL_INCLUDEALIASES | DIEDFL_INCLUDEPHANTOMS | DIEDFL_ATTACHEDONLY,
                                   0x2, 0x40000 };
    IDirectInputW* InputW = nullptr;
    IDirectInputA* InputA = nullptr;
    HRESULT hr;

    hr = DcCreateW(DcSelf, Version, reinterpret_cast<PVOID*>(&InputW), nullptr);
    printf("enum ver %04lX create = %08lX\n", Version, hr);
    if (FAILED(hr))
        return;
    InputW->QueryInterface(IID_IDirectInputA, reinterpret_cast<PVOID*>(&InputA));

    for (ULONG Type = 0; Type < _countof(Types); Type++)
    {
        for (ULONG Flag = 0; Flag < _countof(Flags); Flag++)
        {
            DC_ENUM_CONTEXT State = { 0, MAXDWORD };

            hr = InputW->EnumDevices(Types[Type], DcEnumDevicesW, &State, Flags[Flag]);
            printf("  enumW type %08lX flags %08lX = %08lX count %lu\n", Types[Type], Flags[Flag], hr, State.Count);
        }
    }

    {
        DC_ENUM_CONTEXT State = { 0, 1 };

        hr = InputW->EnumDevices(0, DcEnumDevicesW, &State, DIEDFL_INCLUDEALIASES);
        printf("  enumW stop after one = %08lX count %lu\n", hr, State.Count);
        State.Count = 0;
        State.StopAfter = MAXDWORD;
        hr = InputA->EnumDevices(0, DcEnumDevicesA, &State, DIEDFL_INCLUDEALIASES);
        printf("  enumA aliases = %08lX count %lu\n", hr, State.Count);
        printf("  enumA null callback = %08lX\n", InputA->EnumDevices(0, nullptr, nullptr, 0));
        printf("  enumW null callback = %08lX\n", InputW->EnumDevices(0, nullptr, nullptr, 0));
    }

    for (ULONG Index = 0; Index < _countof(DcDevices); Index++)
        printf("  devicestatus %s = %08lX\n", DcDevices[Index].Name, InputW->GetDeviceStatus(*DcDevices[Index].Guid));

    printf("  runcontrolpanel badwnd = %08lX\n", InputW->RunControlPanel(reinterpret_cast<HWND>(0x1234), 0));
    printf("  runcontrolpanel flags = %08lX\n", InputW->RunControlPanel(nullptr, 1));

    {
        IDirectInput2W* Input2 = nullptr;
        GUID Found = DcBogusGuid;

        InputW->QueryInterface(IID_IDirectInput2W, reinterpret_cast<PVOID*>(&Input2));
        printf("  finddevice wrong class = %08lX\n", Input2->FindDevice(GUID_SysMouse, L"x", &Found));
        printf("  finddevice hid bogus = %08lX\n", Input2->FindDevice(GUID_HIDClass, L"\\\\?\\hid#none", &Found));
        printf("  finddevice empty = %08lX\n", Input2->FindDevice(GUID_HIDClass, L"", &Found));
        printf("  finddevice null name = %08lX\n", Input2->FindDevice(GUID_HIDClass, nullptr, &Found));
        printf("  finddevice null out = %08lX\n", Input2->FindDevice(GUID_HIDClass, L"x", nullptr));
        printf("  finddevice out %s\n", DcGuid(Found).c_str());
        Input2->Release();
    }

    DcRelease("  inputA", InputA);
    DcRelease("  inputW", InputW);
}

static
BOOL
CALLBACK
DcEnumJoyTypes(
    _In_ LPCWSTR Name,
    _In_ LPVOID Context)
{
    printf("    type '%ls'\n", Name);
    return ++*static_cast<ULONG*>(Context) < 64;
}

/* Read only questions, nothing here changes the joystick settings */
static
VOID
DcCheckJoyConfig(
    _In_ IDirectInputJoyConfig* Config)
{
    DIJOYTYPEINFO Type;
    DIJOYCONFIG Joy;
    DIJOYUSERVALUES User;
    HKEY Key = nullptr;
    ULONG Count = 0;
    HRESULT hr;

    printf("  joy acquire uncoop = %08lX\n", Config->Acquire());
    printf("  joy unacquire = %08lX\n", Config->Unacquire());
    printf("  joy coop null = %08lX\n", Config->SetCooperativeLevel(nullptr, DISCL_EXCLUSIVE | DISCL_BACKGROUND));
    printf("  joy coop fg = %08lX\n", Config->SetCooperativeLevel(DcWindow, DISCL_EXCLUSIVE | DISCL_FOREGROUND));
    printf("  joy coop desktop = %08lX\n", Config->SetCooperativeLevel(GetDesktopWindow(), DISCL_EXCLUSIVE | DISCL_BACKGROUND));
    printf("  joy sendnotify = %08lX\n", Config->SendNotify());
    hr = Config->EnumTypes(DcEnumJoyTypes, &Count);
    printf("  joy enumtypes = %08lX count %lu\n", hr, Count);
    printf("  joy enumtypes null = %08lX\n", Config->EnumTypes(nullptr, nullptr));

    static const PCWSTR Types[] = { L"#2", L"#4", L"#12", L"VID_045E&PID_0000", L"NoSuchType", L"" };
    static const DWORD TypeSizes[] = { sizeof(DIJOYTYPEINFO_DX5), sizeof(DIJOYTYPEINFO), 100 };
    for (ULONG Index = 0; Index < _countof(Types); Index++)
    {
        for (ULONG Size = 0; Size < _countof(TypeSizes); Size++)
        {
            ZeroMemory(&Type, sizeof(Type));
            Type.dwSize = TypeSizes[Size];
            hr = Config->GetTypeInfo(Types[Index], &Type, DITC_REGHWSETTINGS | DITC_DISPLAYNAME);
            printf("  joy typeinfo '%ls' %lu = %08lX flags %08lX name '%ls'\n", Types[Index], TypeSizes[Size], hr,
                   Type.hws.dwFlags, Type.wszDisplayName);
        }
    }
    ZeroMemory(&Type, sizeof(Type));
    Type.dwSize = sizeof(Type);
    printf("  joy typeinfo badflags = %08lX\n", Config->GetTypeInfo(L"#2", &Type, 0x100));
    printf("  joy typeinfo null = %08lX\n", Config->GetTypeInfo(L"#2", nullptr, 0));
    printf("  joy settypeinfo unacquired = %08lX\n", Config->SetTypeInfo(L"Test", &Type, 0));
    printf("  joy deletetype unacquired = %08lX\n", Config->DeleteType(L"Test"));

    for (UINT Id = 0; Id < 18; Id += (Id < 4) ? 1 : 6)
    {
        ZeroMemory(&Joy, sizeof(Joy));
        Joy.dwSize = sizeof(Joy);
        hr = Config->GetConfig(Id, &Joy, DIJC_REGHWCONFIGTYPE | DIJC_GUIDINSTANCE | DIJC_CALLOUT | DIJC_GAIN);
        printf("  joy getconfig %u = %08lX type '%ls' gain %lu\n", Id, hr, Joy.wszType, Joy.dwGain);
    }
    ZeroMemory(&Joy, sizeof(Joy));
    Joy.dwSize = sizeof(DIJOYCONFIG_DX5);
    printf("  joy getconfig dx5 = %08lX\n", Config->GetConfig(0, &Joy, DIJC_REGHWCONFIGTYPE));
    printf("  joy getconfig dx5 badflags = %08lX\n", Config->GetConfig(0, &Joy, DIJC_GAIN));
    Joy.dwSize = 12;
    printf("  joy getconfig badsize = %08lX\n", Config->GetConfig(0, &Joy, 0));
    printf("  joy setconfig unacquired = %08lX\n", Config->SetConfig(0, &Joy, 0));
    printf("  joy deleteconfig unacquired = %08lX\n", Config->DeleteConfig(0));

    ZeroMemory(&User, sizeof(User));
    User.dwSize = sizeof(User);
    printf("  joy uservalues = %08lX\n", Config->GetUserValues(&User, DIJU_USERVALUES | DIJU_GLOBALDRIVER | DIJU_GAMEPORTEMULATOR));
    printf("  joy uservalues badflags = %08lX\n", Config->GetUserValues(&User, 8));
    printf("  joy setuservalues unacquired = %08lX\n", Config->SetUserValues(&User, DIJU_USERVALUES));
    printf("  joy addnewhardware badwnd = %08lX\n", Config->AddNewHardware(reinterpret_cast<HWND>(0x1234), DcNullGuid));

    hr = Config->OpenTypeKey(L"#2", KEY_READ, &Key);
    printf("  joy opentypekey predef = %08lX\n", hr);
    hr = Config->OpenTypeKey(L"NoSuchType", KEY_ALL_ACCESS, &Key);
    printf("  joy opentypekey write unacquired = %08lX\n", hr);
    printf("  joy opentypekey null = %08lX\n", Config->OpenTypeKey(L"x", KEY_READ, nullptr));
    hr = Config->OpenConfigKey(0, KEY_READ, &Key);
    printf("  joy openconfigkey = %08lX\n", hr);
    if (SUCCEEDED(hr))
        RegCloseKey(Key);
    printf("  joy openconfigkey write = %08lX\n", Config->OpenConfigKey(0, KEY_ALL_ACCESS, &Key));
}

static
VOID
DcCheckInputObject(VOID)
{
    IDirectInputW* InputW = nullptr;
    IDirectInputA* InputA = nullptr;
    IDirectInput7W* Input7 = nullptr;
    PVOID Device;
    HRESULT hr;

    hr = DcCreateW(DcSelf, 0x0700, reinterpret_cast<PVOID*>(&InputW), nullptr);
    printf("object create = %08lX\n", hr);
    InputW->QueryInterface(IID_IDirectInputA, reinterpret_cast<PVOID*>(&InputA));
    InputW->QueryInterface(IID_IDirectInput7W, reinterpret_cast<PVOID*>(&Input7));
    DcQueryMatrix("object", InputW, InputA, InputW);

    {
        IUnknown* Config = nullptr;

        hr = InputW->QueryInterface(IID_IDirectInputJoyConfig, reinterpret_cast<PVOID*>(&Config));
        printf("object qi joyconfig = %08lX %s\n", hr, Config ? "set" : "null");
        if (Config != nullptr)
            DcCheckJoyConfig(reinterpret_cast<IDirectInputJoyConfig*>(Config));
        DcRelease("  joyconfig", Config);
    }

    for (ULONG Index = 0; Index < _countof(DcDevices); Index++)
    {
        Device = reinterpret_cast<PVOID>(1);
        hr = InputW->CreateDevice(*DcDevices[Index].Guid, reinterpret_cast<IDirectInputDeviceW**>(&Device), nullptr);
        printf("createdevice %s = %08lX %s\n", DcDevices[Index].Name, hr, Device ? "set" : "null");
        if (SUCCEEDED(hr))
            DcRelease("  device", static_cast<IUnknown*>(Device));

        for (ULONG Iid = 0; Iid < _countof(DcInputIids); Iid++)
        {
            Device = reinterpret_cast<PVOID>(1);
            hr = Input7->CreateDeviceEx(*DcDevices[Index].Guid, *DcInputIids[Iid].Iid, &Device, nullptr);
            if (Index < 1 || FAILED(hr))
                printf("  createdeviceex %s = %08lX %s\n", DcInputIids[Iid].Name, hr, Device ? "set" : "null");
            if (SUCCEEDED(hr))
                static_cast<IUnknown*>(Device)->Release();
        }
    }

    printf("createdevice null out = %08lX\n", InputW->CreateDevice(GUID_SysMouse, nullptr, nullptr));
    Device = reinterpret_cast<PVOID>(1);
    hr = InputA->CreateDevice(GUID_SysMouse, reinterpret_cast<IDirectInputDeviceA**>(&Device), reinterpret_cast<IUnknown*>(InputW));
    printf("createdevice aggregate = %08lX %s\n", hr, Device ? "set" : "null");
    if (SUCCEEDED(hr))
        DcRelease("  aggregate inner", static_cast<IUnknown*>(Device));

    DcRelease("input7", Input7);
    DcRelease("inputA", InputA);
    DcRelease("inputW", InputW);
}

static
VOID
DcPrintObjectW(
    _In_ const DIDEVICEOBJECTINSTANCEW* Info)
{
    printf("    guid %s ofs %lu type %08lX flags %08lX '%ls' ff %lu %lu coll %u desig %u page %04X usage %04X dim %lu exp %u rep %u\n",
           DcGuid(Info->guidType).c_str(), Info->dwOfs, Info->dwType, Info->dwFlags, Info->tszName,
           Info->dwFFMaxForce, Info->dwFFForceResolution, Info->wCollectionNumber, Info->wDesignatorIndex,
           Info->wUsagePage, Info->wUsage, Info->dwDimension, Info->wExponent, Info->wReportId);
}

static
BOOL
CALLBACK
DcEnumObjectsW(
    _In_ LPCDIDEVICEOBJECTINSTANCEW Info,
    _In_ LPVOID Context)
{
    DC_ENUM_CONTEXT* State = static_cast<DC_ENUM_CONTEXT*>(Context);

    if (State->StopAfter != 0)
        DcPrintObjectW(Info);
    State->Count++;
    return TRUE;
}

static
BOOL
CALLBACK
DcEnumObjectsA(
    _In_ LPCDIDEVICEOBJECTINSTANCEA Info,
    _In_ LPVOID Context)
{
    DC_ENUM_CONTEXT* State = static_cast<DC_ENUM_CONTEXT*>(Context);

    if (State->StopAfter != 0)
        printf("    A size %lu guid %s ofs %lu type %08lX flags %08lX '%s' coll %u\n", Info->dwSize,
               DcGuid(Info->guidType).c_str(), Info->dwOfs, Info->dwType, Info->dwFlags, Info->tszName,
               Info->wCollectionNumber);
    State->Count++;
    return TRUE;
}

static
VOID
DcCheckObjects(
    _In_ IDirectInputDevice7W* Device,
    _In_ IDirectInputDevice7A* DeviceA,
    _In_ BOOLEAN Verbose)
{
    static const DWORD Flags[] =
    {
        DIDFT_ALL, DIDFT_AXIS, DIDFT_RELAXIS, DIDFT_ABSAXIS, DIDFT_BUTTON, DIDFT_PSHBUTTON, DIDFT_TGLBUTTON,
        DIDFT_POV, DIDFT_NODATA, DIDFT_COLLECTION, DIDFT_ALIAS, DIDFT_VENDORDEFINED, DIDFT_ALIAS | DIDFT_BUTTON,
        DIDFT_ENUMCOLLECTION(0), DIDFT_ENUMCOLLECTION(1), DIDFT_FFACTUATOR, DIDFT_OUTPUT, 0x20, 0x80000000,
        DIDFT_BUTTON | DIDFT_ANYINSTANCE,
    };

    for (ULONG Index = 0; Index < _countof(Flags); Index++)
    {
        DC_ENUM_CONTEXT State = { 0, (Verbose && Index == 0) ? 1UL : 0UL };
        HRESULT hr = Device->EnumObjects(DcEnumObjectsW, &State, Flags[Index]);

        printf("  enumobjects %08lX = %08lX count %lu\n", Flags[Index], hr, State.Count);
    }

    {
        DC_ENUM_CONTEXT State = { 0, Verbose ? 1UL : 0UL };

        printf("  enumobjectsA buttons = %08lX\n", DeviceA->EnumObjects(DcEnumObjectsA, &State, DIDFT_AXIS));
        printf("  enumobjects null = %08lX\n", Device->EnumObjects(nullptr, nullptr, 0));
    }

    for (DWORD Offset = 0; Offset < 300; Offset += (Offset < 24) ? 1 : 37)
    {
        DIDEVICEOBJECTINSTANCEW Info;
        HRESULT hr;

        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        hr = Device->GetObjectInfo(&Info, Offset, DIPH_BYOFFSET);
        printf("  objinfo ofs %lu = %08lX\n", Offset, hr);
        if (SUCCEEDED(hr))
            DcPrintObjectW(&Info);
    }

    {
        DIDEVICEOBJECTINSTANCEW Info;
        DIDEVICEOBJECTINSTANCEA InfoA;
        static const DWORD Ids[] = { DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(1), DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(2),
                                     DIDFT_BUTTON | DIDFT_MAKEINSTANCE(4), 0x00000104, 0x00FFFF04, 0x1E04, 0x1E0C, 0x0001 };

        for (ULONG Index = 0; Index < _countof(Ids); Index++)
        {
            ZeroMemory(&Info, sizeof(Info));
            Info.dwSize = sizeof(Info);
            printf("  objinfo id %08lX = %08lX\n", Ids[Index], Device->GetObjectInfo(&Info, Ids[Index], DIPH_BYID));
            if (Info.dwType != 0)
                DcPrintObjectW(&Info);
        }

        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(DIDEVICEOBJECTINSTANCE_DX3W);
        printf("  objinfo dx3 = %08lX\n", Device->GetObjectInfo(&Info, 0, DIPH_BYOFFSET));
        DcBytes("    dx3", &Info, sizeof(Info));
        Info.dwSize = 100;
        printf("  objinfo badsize = %08lX\n", Device->GetObjectInfo(&Info, 0, DIPH_BYOFFSET));
        Info.dwSize = sizeof(Info);
        printf("  objinfo device = %08lX\n", Device->GetObjectInfo(&Info, 0, DIPH_DEVICE));
        printf("  objinfo device obj = %08lX\n", Device->GetObjectInfo(&Info, 1, DIPH_DEVICE));
        printf("  objinfo usage = %08lX\n", Device->GetObjectInfo(&Info, 0x00010030, DIPH_BYUSAGE));
        printf("  objinfo how 9 = %08lX\n", Device->GetObjectInfo(&Info, 0, 9));
        printf("  objinfo null = %08lX\n", Device->GetObjectInfo(nullptr, 0, DIPH_BYOFFSET));

        ZeroMemory(&InfoA, sizeof(InfoA));
        InfoA.dwSize = sizeof(InfoA);
        printf("  objinfoA = %08lX\n", DeviceA->GetObjectInfo(&InfoA, 1, DIPH_BYOFFSET));
        DcBytes("    A", &InfoA, sizeof(InfoA));
        ZeroMemory(&InfoA, sizeof(InfoA));
        InfoA.dwSize = sizeof(DIDEVICEOBJECTINSTANCE_DX3A);
        printf("  objinfoA dx3 = %08lX\n", DeviceA->GetObjectInfo(&InfoA, 1, DIPH_BYOFFSET));
        DcBytes("    A dx3", &InfoA, sizeof(InfoA));
    }
}

static
VOID
DcCheckProperties(
    _In_ IDirectInputDevice7W* Device)
{
    static const struct
    {
        ULONG_PTR Id;
        DWORD Size;
    } Props[] =
    {
        { 1, sizeof(DIPROPDWORD) }, { 2, sizeof(DIPROPDWORD) }, { 3, sizeof(DIPROPDWORD) }, { 4, sizeof(DIPROPRANGE) },
        { 5, sizeof(DIPROPDWORD) }, { 6, sizeof(DIPROPDWORD) }, { 7, sizeof(DIPROPDWORD) }, { 8, sizeof(DIPROPDWORD) },
        { 9, sizeof(DIPROPDWORD) }, { 10, sizeof(DIPROPDWORD) }, { 11, sizeof(DIPROPCAL) },
        { 12, sizeof(DIPROPGUIDANDPATH) }, { 13, sizeof(DIPROPSTRING) }, { 14, sizeof(DIPROPSTRING) },
        { 15, sizeof(DIPROPDWORD) }, { 16, sizeof(DIPROPSTRING) }, { 17, sizeof(DIPROPDWORD) },
        { 0xFFFF, sizeof(DIPROPDWORD) }, { 0xFFFB, sizeof(DIPROPDWORD) }, { 0xFFFA, sizeof(DIPROPDWORD) },
    };
    static const struct
    {
        DWORD Object;
        DWORD How;
    } Targets[] =
    {
        { 0, DIPH_DEVICE }, { 0, DIPH_BYOFFSET }, { 8, DIPH_BYOFFSET }, { 12, DIPH_BYOFFSET }, { 1, DIPH_BYOFFSET },
        { DIDFT_RELAXIS | DIDFT_MAKEINSTANCE(2), DIPH_BYID }, { DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(30), DIPH_BYID },
        { 5, DIPH_DEVICE },
    };
    BYTE Buffer[600];
    LPDIPROPHEADER Header = reinterpret_cast<LPDIPROPHEADER>(Buffer);

    for (ULONG Prop = 0; Prop < _countof(Props); Prop++)
    {
        for (ULONG Target = 0; Target < _countof(Targets); Target++)
        {
            HRESULT hr;

            FillMemory(Buffer, sizeof(Buffer), 0xCC);
            Header->dwSize = Props[Prop].Size;
            Header->dwHeaderSize = sizeof(DIPROPHEADER);
            Header->dwObj = Targets[Target].Object;
            Header->dwHow = Targets[Target].How;
            hr = Device->GetProperty(*reinterpret_cast<const GUID*>(Props[Prop].Id), Header);
            printf("  getprop %lX obj %08lX how %lu = %08lX", static_cast<ULONG>(Props[Prop].Id), Targets[Target].Object,
                   Targets[Target].How, hr);
            if (SUCCEEDED(hr))
            {
                printf(" data");
                for (DWORD Index = sizeof(DIPROPHEADER); Index < Props[Prop].Size && Index < 40; Index++)
                    printf(" %02X", Buffer[Index]);
            }
            printf("\n");
        }
    }

    /* Header shapes */
    {
        DIPROPDWORD Value = {};

        Value.diph.dwSize = sizeof(Value);
        Value.diph.dwHeaderSize = 12;
        printf("  getprop badheader = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        Value.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        Value.diph.dwSize = 22;
        printf("  getprop oddsize = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        Value.diph.dwSize = 24;
        printf("  getprop bigsize = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        Value.diph.dwSize = 12;
        printf("  getprop small = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        printf("  getprop null = %08lX\n", Device->GetProperty(DIPROP_BUFFERSIZE, nullptr));
        Value.diph.dwSize = sizeof(Value);
        printf("  getprop guid = %08lX\n", Device->GetProperty(GUID_SysMouse, &Value.diph));
    }

    /* Settable values */
    {
        static const struct
        {
            ULONG_PTR Id;
            DWORD Data;
            DWORD Object;
            DWORD How;
        } Sets[] =
        {
            { 1, 0, 0, DIPH_DEVICE }, { 1, 16, 0, DIPH_DEVICE }, { 1, 5000, 0, DIPH_DEVICE }, { 1, 16, 0, DIPH_BYOFFSET },
            { 2, 0, 0, DIPH_DEVICE }, { 2, 1, 0, DIPH_DEVICE }, { 2, 2, 0, DIPH_DEVICE }, { 2, 0, 1, DIPH_BYOFFSET },
            { 3, 5, 0, DIPH_DEVICE }, { 5, 100, 0, DIPH_DEVICE }, { 6, 100, 0, DIPH_DEVICE }, { 5, 100, 0, DIPH_BYOFFSET },
            { 7, 5000, 0, DIPH_DEVICE }, { 7, 0x10000, 0, DIPH_DEVICE }, { 8, 1, 0, DIPH_DEVICE }, { 9, 0, 0, DIPH_DEVICE },
            { 9, 2, 0, DIPH_DEVICE }, { 10, 1, 0, DIPH_DEVICE }, { 15, 1, 0, DIPH_DEVICE }, { 0xFFFF, 100, 0, DIPH_DEVICE },
            { 0xFFFB, 1, 0, DIPH_DEVICE }, { 1, 2000, 0, DIPH_DEVICE }, { 0xFFFF, 1023, 0, DIPH_DEVICE }, { 1, 32, 0, DIPH_DEVICE },
            { 2, 0, 0, DIPH_DEVICE },
        };

        for (ULONG Index = 0; Index < _countof(Sets); Index++)
        {
            DIPROPDWORD Value = {};
            HRESULT hr;

            Value.diph.dwSize = sizeof(Value);
            Value.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            Value.diph.dwObj = Sets[Index].Object;
            Value.diph.dwHow = Sets[Index].How;
            Value.dwData = Sets[Index].Data;
            hr = Device->SetProperty(*reinterpret_cast<const GUID*>(Sets[Index].Id), &Value.diph);
            Value.dwData = 0xCCCCCCCC;
            Value.diph.dwObj = 0;
            Value.diph.dwHow = DIPH_DEVICE;
            printf("  setprop %lX = %lu -> %08lX, get %08lX", static_cast<ULONG>(Sets[Index].Id), Sets[Index].Data, hr,
                   Device->GetProperty(*reinterpret_cast<const GUID*>(Sets[Index].Id), &Value.diph));
            printf(" %08lX\n", Value.dwData);
        }

        {
            DIPROPRANGE Range = {};

            Range.diph.dwSize = sizeof(Range);
            Range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            Range.lMin = -100;
            Range.lMax = 100;
            printf("  setprop range = %08lX\n", Device->SetProperty(DIPROP_RANGE, &Range.diph));
        }
    }
}

static
VOID
DcCheckFormats(
    _In_ IDirectInputDevice7W* Device)
{
    static const struct
    {
        PCSTR Name;
        const DIDATAFORMAT* Format;
    } Formats[] =
    {
        { "keyboard", &c_dfDIKeyboard }, { "mouse", &c_dfDIMouse }, { "mouse2", &c_dfDIMouse2 },
        { "joystick", &c_dfDIJoystick }, { "joystick2", &c_dfDIJoystick2 },
    };
    DIOBJECTDATAFORMAT Custom[4];
    DIDATAFORMAT Format;

    for (ULONG Index = 0; Index < _countof(Formats); Index++)
    {
        DIDEVICEOBJECTINSTANCEW Info;
        HRESULT hr = Device->SetDataFormat(Formats[Index].Format);

        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        printf("  setdataformat %s = %08lX objinfo %08lX", Formats[Index].Name, hr, Device->GetObjectInfo(&Info, 4, DIPH_BYOFFSET));
        printf(" type %08lX ofs %lu\n", Info.dwType, Info.dwOfs);
    }

    printf("  setdataformat null = %08lX\n", Device->SetDataFormat(nullptr));
    Format = c_dfDIMouse;
    Format.dwSize = 28;
    printf("  setdataformat size = %08lX\n", Device->SetDataFormat(&Format));
    Format = c_dfDIMouse;
    Format.dwObjSize = 16;
    printf("  setdataformat objsize = %08lX\n", Device->SetDataFormat(&Format));
    Format = c_dfDIMouse;
    Format.dwFlags = 3;
    printf("  setdataformat flags 3 = %08lX\n", Device->SetDataFormat(&Format));
    Format.dwFlags = 4;
    printf("  setdataformat flags 4 = %08lX\n", Device->SetDataFormat(&Format));
    Format = c_dfDIMouse;
    Format.dwDataSize = 18;
    printf("  setdataformat datasize = %08lX\n", Device->SetDataFormat(&Format));
    Format.dwDataSize = 16;
    Format.rgodf = nullptr;
    printf("  setdataformat null objects = %08lX\n", Device->SetDataFormat(&Format));
    Format.dwNumObjs = 0;
    printf("  setdataformat no objects = %08lX\n", Device->SetDataFormat(&Format));

    /* Hand built layouts that shift and reorder the objects */
    Custom[0] = { &GUID_YAxis, 8, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0 };
    Custom[1] = { &GUID_XAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0 };
    Custom[2] = { nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 };
    Custom[3] = { &GUID_POV, 16, 0x80000000 | DIDFT_POV | DIDFT_ANYINSTANCE, 0 };
    Format = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 20, 4, Custom };
    printf("  setdataformat custom = %08lX\n", Device->SetDataFormat(&Format));
    Custom[1].dwOfs = 5;
    printf("  setdataformat misaligned = %08lX\n", Device->SetDataFormat(&Format));
    Custom[1].dwOfs = 40;
    printf("  setdataformat beyond = %08lX\n", Device->SetDataFormat(&Format));
    Custom[1].dwOfs = 4;
    Custom[3].dwType = DIDFT_POV | DIDFT_ANYINSTANCE;
    printf("  setdataformat required pov = %08lX\n", Device->SetDataFormat(&Format));
    Custom[3] = { nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0 };
    printf("  setdataformat two buttons = %08lX\n", Device->SetDataFormat(&Format));
    Custom[0] = { &GUID_Key, 0, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(DIK_ESCAPE), 0 };
    Custom[1] = { &GUID_Key, 1, DIDFT_BUTTON | DIDFT_MAKEINSTANCE(DIK_SPACE), 0 };
    Format = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), 0, 4, 2, Custom };
    printf("  setdataformat keys = %08lX\n", Device->SetDataFormat(&Format));
}

static
VOID
DcCheckAcquire(
    _In_ IDirectInputDevice7W* Device,
    _In_ BOOLEAN Keyboard,
    _In_ BOOLEAN Uninitialized)
{
    static const DWORD Levels[] =
    {
        DISCL_NONEXCLUSIVE | DISCL_BACKGROUND, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND, DISCL_EXCLUSIVE | DISCL_BACKGROUND,
        DISCL_EXCLUSIVE | DISCL_FOREGROUND, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND | DISCL_NOWINKEY,
        DISCL_NONEXCLUSIVE | DISCL_FOREGROUND | DISCL_NOWINKEY, 0, DISCL_EXCLUSIVE | DISCL_NONEXCLUSIVE | DISCL_BACKGROUND,
        DISCL_NONEXCLUSIVE, DISCL_BACKGROUND, 0x20 | DISCL_NONEXCLUSIVE | DISCL_BACKGROUND,
    };
    BYTE State[300];
    DIDEVICEOBJECTDATA_DX3 Records[8];
    DWORD Count;
    HANDLE Event;
    HRESULT hr;

    for (ULONG Index = 0; Index < _countof(Levels); Index++)
    {
        printf("  coop %02lX null = %08lX", Levels[Index], Device->SetCooperativeLevel(nullptr, Levels[Index]));
        printf(" own = %08lX", Device->SetCooperativeLevel(DcWindow, Levels[Index]));
        printf(" child = %08lX", Device->SetCooperativeLevel(DcChild, Levels[Index]));
        printf(" desktop = %08lX\n", Device->SetCooperativeLevel(GetDesktopWindow(), Levels[Index]));
    }
    printf("  coop badwnd = %08lX\n", Device->SetCooperativeLevel(reinterpret_cast<HWND>(0x1234), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));

    printf("  getdevicestate early = %08lX\n", Device->GetDeviceState(sizeof(State), State));
    printf("  unacquire early = %08lX\n", Device->Unacquire());
    printf("  getdevicestate after unacquire = %08lX\n", Device->GetDeviceState(sizeof(State), State));
    printf("  acquire noformat = %08lX\n", Device->Acquire());
    printf("  poll noformat = %08lX\n", Device->Poll());
    Count = 1;
    printf("  getdevicedata noformat = %08lX\n", Device->GetDeviceData(sizeof(Records[0]), reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, 0));
    if (Uninitialized)
        return;

    hr = Device->SetDataFormat(Keyboard ? &c_dfDIKeyboard : &c_dfDIMouse2);
    printf("  setdataformat = %08lX\n", hr);

    /* Foreground needs our window to be the active one, which a console test never has */
    printf("  coop fg = %08lX\n", Device->SetCooperativeLevel(DcWindow, DISCL_NONEXCLUSIVE | DISCL_FOREGROUND));
    printf("  acquire fg = %08lX\n", Device->Acquire());
    printf("  getdevicestate fg = %08lX\n", Device->GetDeviceState(Keyboard ? 256 : sizeof(DIMOUSESTATE2), State));

    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    printf("  coop bg = %08lX\n", Device->SetCooperativeLevel(DcWindow, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
    printf("  event bad = %08lX\n", Device->SetEventNotification(reinterpret_cast<HANDLE>(0x1234)));
    printf("  event = %08lX\n", Device->SetEventNotification(Event));
    printf("  event again = %08lX\n", Device->SetEventNotification(Event));
    printf("  event clear = %08lX\n", Device->SetEventNotification(nullptr));
    printf("  event clear again = %08lX\n", Device->SetEventNotification(nullptr));
    printf("  event set = %08lX\n", Device->SetEventNotification(Event));

    {
        DIPROPDWORD Value = {};

        Value.diph.dwSize = sizeof(Value);
        Value.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        Value.dwData = 4;
        printf("  setprop buffer = %08lX\n", Device->SetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        if (!Keyboard)
        {
            Value.dwData = DIPROPAXISMODE_REL;
            printf("  setprop axismode = %08lX\n", Device->SetProperty(DIPROP_AXISMODE, &Value.diph));
        }
    }

    printf("  acquire = %08lX\n", Device->Acquire());
    printf("  acquire again = %08lX\n", Device->Acquire());
    printf("  event while acquired = %08lX\n", Device->SetEventNotification(nullptr));
    printf("  coop while acquired = %08lX\n", Device->SetCooperativeLevel(DcWindow, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND));
    printf("  setdataformat while acquired = %08lX\n", Device->SetDataFormat(&c_dfDIKeyboard));
    {
        DIPROPDWORD Value = {};

        Value.diph.dwSize = sizeof(Value);
        Value.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        Value.dwData = 8;
        printf("  setprop buffer while acquired = %08lX\n", Device->SetProperty(DIPROP_BUFFERSIZE, &Value.diph));
        printf("  setprop axismode while acquired = %08lX\n", Device->SetProperty(DIPROP_AXISMODE, &Value.diph));
    }
    printf("  poll = %08lX\n", Device->Poll());

    for (DWORD Size = 0; Size < 300; Size += (Size < 24) ? 4 : 100)
        printf("  getdevicestate %lu = %08lX\n", Size, Device->GetDeviceState(Size, State));

    FillMemory(State, sizeof(State), 0xCC);
    hr = Device->GetDeviceState(Keyboard ? 256 : sizeof(DIMOUSESTATE2), State);
    printf("  getdevicestate = %08lX\n", hr);
    if (Keyboard)
    {
        ULONG Down = 0;

        for (ULONG Index = 0; Index < 256; Index++)
            Down += (State[Index] != 0);
        printf("    keys down %lu tail %02X\n", Down, State[256]);
    }
    else
    {
        /* Relative axes only move when the user does, buttons are left out */
        printf("    tail %02X %02X\n", State[sizeof(DIMOUSESTATE2)], State[sizeof(DIMOUSESTATE2) + 1]);
    }

    for (DWORD Size = 0; Size <= 32; Size += 8)
    {
        Count = 8;
        FillMemory(Records, sizeof(Records), 0xCC);
        hr = Device->GetDeviceData(Size, reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, 0);
        printf("  getdevicedata size %lu = %08lX count %lu\n", Size, hr, Count);
    }
    Count = INFINITE;
    hr = Device->GetDeviceData(sizeof(Records[0]), nullptr, &Count, 0);
    printf("  getdevicedata flush = %08lX count %s\n", hr, Count < 100 ? "small" : "large");
    Count = 8;
    hr = Device->GetDeviceData(sizeof(Records[0]), reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, DIGDD_PEEK);
    printf("  getdevicedata peek = %08lX count %lu\n", hr, Count);
    Count = 8;
    printf("  getdevicedata residual = %08lX\n",
           Device->GetDeviceData(sizeof(Records[0]), reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, 2));
    Count = 8;
    printf("  getdevicedata badflags = %08lX\n",
           Device->GetDeviceData(sizeof(Records[0]), reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, 4));

    {
        DIDEVICEOBJECTDATA_DX3 Out[2] = {};

        Out[0].dwOfs = 0;
        Out[1].dwOfs = Keyboard ? DIK_A : 12;
        Count = 2;
        hr = Device->SendDeviceData(sizeof(Out[0]), reinterpret_cast<LPCDIDEVICEOBJECTDATA>(Out), &Count, 0);
        printf("  senddevicedata = %08lX count %lu ofs %08lX %08lX\n", hr, Count, Out[0].dwOfs, Out[1].dwOfs);
        Count = 2;
        hr = Device->SendDeviceData(24, reinterpret_cast<LPCDIDEVICEOBJECTDATA>(Out), &Count, 0);
        printf("  senddevicedata size = %08lX count %lu\n", hr, Count);
        printf("  senddevicedata flags = %08lX\n",
               Device->SendDeviceData(sizeof(Out[0]), reinterpret_cast<LPCDIDEVICEOBJECTDATA>(Out), &Count, 2));
    }

    printf("  unacquire = %08lX\n", Device->Unacquire());
    printf("  unacquire again = %08lX\n", Device->Unacquire());
    printf("  getdevicestate unacquired = %08lX\n", Device->GetDeviceState(Keyboard ? 256 : sizeof(DIMOUSESTATE2), State));
    Count = 8;
    printf("  getdevicedata unacquired = %08lX\n",
           Device->GetDeviceData(sizeof(Records[0]), reinterpret_cast<LPDIDEVICEOBJECTDATA>(Records), &Count, 0));
    printf("  poll unacquired = %08lX\n", Device->Poll());
    printf("  event wait = %lu\n", WaitForSingleObject(Event, 0));
    printf("  event clear after = %08lX\n", Device->SetEventNotification(nullptr));
    CloseHandle(Event);
}

static
VOID
DcCheckForceFeedback(
    _In_ IDirectInputDevice7W* Device)
{
    DIEFFESCAPE Escape = {};
    DWORD Flags = 0xCCCCCCCC;
    LPDIRECTINPUTEFFECT Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(1);
    DIEFFECTINFOW Info = {};

    HRESULT hr = Device->GetForceFeedbackState(&Flags);

    printf("  ffstate = %08lX %08lX\n", hr, Flags);
    printf("  ffstate null = %08lX\n", Device->GetForceFeedbackState(nullptr));
    printf("  ffcommand 0 = %08lX\n", Device->SendForceFeedbackCommand(0));
    printf("  ffcommand 3 = %08lX\n", Device->SendForceFeedbackCommand(3));
    printf("  ffcommand 40 = %08lX\n", Device->SendForceFeedbackCommand(0x40));
    printf("  ffcommand reset = %08lX\n", Device->SendForceFeedbackCommand(DISFFC_RESET));
    printf("  escape null = %08lX\n", Device->Escape(nullptr));
    printf("  escape size = %08lX\n", Device->Escape(&Escape));
    Escape.dwSize = sizeof(Escape);
    Escape.cbInBuffer = 4;
    printf("  escape inbuffer = %08lX\n", Device->Escape(&Escape));
    Escape.cbInBuffer = 0;
    printf("  escape = %08lX\n", Device->Escape(&Escape));
    hr = Device->CreateEffect(GUID_ConstantForce, nullptr, &Effect, nullptr);
    printf("  createeffect = %08lX %s\n", hr, Effect ? "set" : "null");
    printf("  createeffect null = %08lX\n", Device->CreateEffect(GUID_ConstantForce, nullptr, nullptr, nullptr));

    /* Argument shapes the effect object would reject, which a device without a driver never reaches */
    {
        DIEFFECT Params = {};
        IUnknown* Outer = reinterpret_cast<IUnknown*>(Device);

        Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(1);
        hr = Device->CreateEffect(GUID_ConstantForce, nullptr, &Effect, Outer);
        printf("  createeffect outer = %08lX %s\n", hr, Effect ? "set" : "null");
        Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(1);
        hr = Device->CreateEffect(DcBogusGuid, nullptr, &Effect, nullptr);
        printf("  createeffect bogus = %08lX %s\n", hr, Effect ? "set" : "null");
        Params.dwSize = 12;
        Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(1);
        hr = Device->CreateEffect(GUID_Sine, &Params, &Effect, nullptr);
        printf("  createeffect badsize = %08lX %s\n", hr, Effect ? "set" : "null");
        Params.dwSize = sizeof(DIEFFECT);
        Params.dwFlags = DIEFF_CARTESIAN;
        Effect = reinterpret_cast<LPDIRECTINPUTEFFECT>(1);
        hr = Device->CreateEffect(GUID_Sine, &Params, &Effect, nullptr);
        printf("  createeffect noaxes = %08lX %s\n", hr, Effect ? "set" : "null");
    }
    printf("  enumeffects = %08lX\n", Device->EnumEffects([](LPCDIEFFECTINFOW, LPVOID) -> BOOL { return TRUE; }, nullptr, 0));
    printf("  enumeffects badtype = %08lX\n", Device->EnumEffects([](LPCDIEFFECTINFOW, LPVOID) -> BOOL { return TRUE; }, nullptr, 0x100));
    printf("  enumeffects null = %08lX\n", Device->EnumEffects(nullptr, nullptr, 0));
    Info.dwSize = sizeof(Info);
    printf("  effectinfo = %08lX\n", Device->GetEffectInfo(&Info, GUID_ConstantForce));
    Info.dwSize = 10;
    printf("  effectinfo size = %08lX\n", Device->GetEffectInfo(&Info, GUID_ConstantForce));
    printf("  enumcreated = %08lX\n", Device->EnumCreatedEffectObjects([](LPDIRECTINPUTEFFECT, LPVOID) -> BOOL { return TRUE; }, nullptr, 0));
    printf("  enumcreated flags = %08lX\n", Device->EnumCreatedEffectObjects([](LPDIRECTINPUTEFFECT, LPVOID) -> BOOL { return TRUE; }, nullptr, 1));
    printf("  enumcreated null = %08lX\n", Device->EnumCreatedEffectObjects(nullptr, nullptr, 0));
    printf("  runcontrolpanel badwnd = %08lX\n", Device->RunControlPanel(reinterpret_cast<HWND>(0x1234), 0));
    printf("  runcontrolpanel flags = %08lX\n", Device->RunControlPanel(nullptr, 1));
}

static
VOID
DcCheckDevice(
    _In_z_ PCSTR Name,
    _In_ IDirectInputDevice7W* Device,
    _In_ BOOLEAN Keyboard,
    _In_ BOOLEAN Verbose,
    _In_ BOOLEAN Uninitialized)
{
    IDirectInputDevice7A* DeviceA = nullptr;
    DIDEVCAPS Caps;
    DIDEVICEINSTANCEW Info;
    DIDEVICEINSTANCEA InfoA;
    static const DWORD CapSizes[] = { 0, 20, sizeof(DIDEVCAPS_DX3), sizeof(DIDEVCAPS), 48 };
    static const DWORD InfoSizes[] = { 0, sizeof(DIDEVICEINSTANCE_DX3W), sizeof(DIDEVICEINSTANCEW), 1104 };
    static const DWORD InfoSizesA[] = { sizeof(DIDEVICEINSTANCE_DX3A), sizeof(DIDEVICEINSTANCEA), 1100 };

    printf("device %s\n", Name);
    Device->QueryInterface(IID_IDirectInputDevice7A, reinterpret_cast<PVOID*>(&DeviceA));
    if (Verbose)
        DcQueryMatrix("  device", Device, DeviceA, Device);

    for (ULONG Index = 0; Index < _countof(CapSizes); Index++)
    {
        FillMemory(&Caps, sizeof(Caps), 0xCC);
        Caps.dwSize = CapSizes[Index];
        printf("  caps %lu = %08lX\n", CapSizes[Index], Device->GetCapabilities(&Caps));
        DcBytes("    caps", &Caps, sizeof(Caps));
    }
    printf("  caps null = %08lX\n", Device->GetCapabilities(nullptr));

    for (ULONG Index = 0; Index < _countof(InfoSizes); Index++)
    {
        FillMemory(&Info, sizeof(Info), 0xCC);
        Info.dwSize = InfoSizes[Index];
        printf("  info %lu = %08lX\n", InfoSizes[Index], Device->GetDeviceInfo(&Info));
        DcPrintDeviceW(&Info);
    }
    for (ULONG Index = 0; Index < _countof(InfoSizesA); Index++)
    {
        FillMemory(&InfoA, sizeof(InfoA), 0xCC);
        InfoA.dwSize = InfoSizesA[Index];
        printf("  infoA %lu = %08lX '%s' '%s' %02X %02X\n", InfoSizesA[Index], DeviceA->GetDeviceInfo(&InfoA),
               InfoA.tszInstanceName, InfoA.tszProductName, reinterpret_cast<PBYTE>(&InfoA)[560],
               reinterpret_cast<PBYTE>(&InfoA)[579]);
    }

    DcCheckObjects(Device, DeviceA, Verbose);
    DcCheckProperties(Device);
    if (!Uninitialized)
        DcCheckFormats(Device);
    DcCheckObjects(Device, DeviceA, FALSE);
    DcCheckAcquire(Device, Keyboard, Uninitialized);
    DcCheckForceFeedback(Device);

    printf("  initialize null = %08lX\n", Device->Initialize(DcSelf, 0x0700, *static_cast<const GUID*>(nullptr)));
    printf("  initialize bogus = %08lX\n", Device->Initialize(DcSelf, 0x0700, DcBogusGuid));
    printf("  initialize badver = %08lX\n", Device->Initialize(DcSelf, 0x0800, GUID_SysMouse));
    printf("  initialize mouse 500 = %08lX\n", Device->Initialize(DcSelf, 0x0500, GUID_SysMouse));
    FillMemory(&Caps, sizeof(Caps), 0xCC);
    Caps.dwSize = sizeof(Caps);
    printf("  caps after = %08lX\n", Device->GetCapabilities(&Caps));
    DcBytes("    caps", &Caps, sizeof(Caps));
    printf("  initialize keyboard 700 = %08lX\n", Device->Initialize(DcSelf, 0x0700, GUID_SysKeyboard));

    DcRelease("  deviceA", DeviceA);
}

static
VOID
DcCheckDevices(VOID)
{
    static const DWORD Versions[] = { 0x0300, 0x0500, 0x0700 };

    for (ULONG Version = 0; Version < _countof(Versions); Version++)
    {
        IDirectInput7W* Input = nullptr;

        printf("devices ver %04lX create = %08lX\n", Versions[Version],
               DcCreateEx(DcSelf, Versions[Version], IID_IDirectInput7W, reinterpret_cast<PVOID*>(&Input), nullptr));

        for (ULONG Index = 0; Index < 6; Index++)
        {
            IDirectInputDevice7W* Device = nullptr;
            HRESULT hr = Input->CreateDeviceEx(*DcDevices[Index].Guid, IID_IDirectInputDevice7W, reinterpret_cast<PVOID*>(&Device), nullptr);
            CHAR Name[64];

            sprintf_s(Name, "%s ver %04lX = %08lX", DcDevices[Index].Name, Versions[Version], hr);
            if (FAILED(hr))
            {
                printf("device %s\n", Name);
                continue;
            }

            DcCheckDevice(Name, Device, Index >= 3, Versions[Version] == 0x0700 && (Index == 0 || Index == 3), FALSE);
            DcRelease("  device", Device);
        }

        /* Every HID collection, the way games walking the device list see them */
        if (Versions[Version] == 0x0700)
        {
            GUID Found[64] = {};
            ULONG Count;

            Input->EnumDevices(DIDEVTYPE_HID, [](LPCDIDEVICEINSTANCEW Info, LPVOID Context) -> BOOL
            {
                GUID* List = static_cast<GUID*>(Context);
                ULONG* Total = reinterpret_cast<ULONG*>(List + 63);

                if (*Total < 62)
                    List[(*Total)++] = Info->guidInstance;
                return TRUE;
            }, Found, DIEDFL_ALLDEVICES);
            Count = *reinterpret_cast<ULONG*>(Found + 63);

            for (ULONG Index = 0; Index < Count && Index < 62; Index++)
            {
                IDirectInputDevice7W* Device = nullptr;
                HRESULT hr = Input->CreateDeviceEx(Found[Index], IID_IDirectInputDevice7W, reinterpret_cast<PVOID*>(&Device), nullptr);
                CHAR Name[64];

                sprintf_s(Name, "hid %lu = %08lX", Index, hr);
                if (FAILED(hr))
                {
                    printf("device %s\n", Name);
                    continue;
                }
                DcCheckDevice(Name, Device, FALSE, TRUE, FALSE);
                DcRelease("  device", Device);
            }
        }

        DcRelease("input", Input);
    }

    /* A device the class factory made, before anything initializes it */
    {
        IClassFactory* Factory = nullptr;
        IDirectInputDevice7W* Device = nullptr;
        HRESULT hr;

        DcGetClassObject(CLSID_DirectInputDevice, IID_IClassFactory, reinterpret_cast<PVOID*>(&Factory));
        hr = Factory->CreateInstance(nullptr, IID_IDirectInputDevice7W, reinterpret_cast<PVOID*>(&Device));
        printf("factory device = %08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            DcCheckDevice("uninitialized", Device, FALSE, TRUE, TRUE);
            DcRelease("  device", Device);
        }
        DcRelease("  factory", Factory);
    }
}

static
VOID
DcDumpFile(
    _In_z_ PCSTR Path)
{
    BYTE Data[4096];
    DWORD Read = 0;
    HANDLE File = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);

    if (File == INVALID_HANDLE_VALUE)
    {
        printf("    file missing\n");
        return;
    }
    ReadFile(File, Data, sizeof(Data), &Read, nullptr);
    CloseHandle(File);
    printf("    file size %lu\n", Read);
    for (DWORD Offset = 0; Offset < Read; Offset += 32)
        DcBytes("     ", Data + Offset, (Read - Offset < 32) ? Read - Offset : 32);
}

static
BOOL
CALLBACK
DcEnumFileEffects(
    _In_ LPCDIFILEEFFECT Entry,
    _In_ LPVOID Context)
{
    const DIEFFECT* Effect = Entry->lpDiEffect;
    ULONG* Count = static_cast<ULONG*>(Context);

    printf("    effect %lu size %lu guid %s name '%.64s'\n", *Count, Entry->dwSize, DcGuid(Entry->GuidEffect).c_str(),
           Entry->szFriendlyName);
    printf("      size %lu flags %08lX dur %lu period %lu gain %lu trig %08lX rep %lu axes %lu cb %lu start %lu tsp %s\n",
           Effect->dwSize, Effect->dwFlags, Effect->dwDuration, Effect->dwSamplePeriod, Effect->dwGain,
           Effect->dwTriggerButton, Effect->dwTriggerRepeatInterval, Effect->cAxes, Effect->cbTypeSpecificParams,
           Effect->dwStartDelay, Effect->lpvTypeSpecificParams ? "set" : "null");
    for (DWORD Axis = 0; Axis < Effect->cAxes && Axis < 4; Axis++)
        printf("      axis %lu ofs %lu dir %ld\n", Axis, Effect->rgdwAxes[Axis], Effect->rglDirection[Axis]);
    if (Effect->lpEnvelope != nullptr)
        DcBytes("      envelope", Effect->lpEnvelope, sizeof(DIENVELOPE));
    if (Effect->cbTypeSpecificParams != 0 && Effect->cbTypeSpecificParams < 64 && Effect->lpvTypeSpecificParams != nullptr)
        DcBytes("      params", Effect->lpvTypeSpecificParams, Effect->cbTypeSpecificParams);
    return ++*Count < 3 ? DIENUM_CONTINUE : DIENUM_STOP;
}

/* Effect files under the scratch folder, written and read back with the keyboard device */
/* Prints what the registry says about a device's effect driver, opening keys for reading only */
static
VOID
DcPrintFFRegistry(
    _In_ const DIDEVICEINSTANCEW* Info)
{
    WCHAR Path[160];
    WCHAR Clsid[64] = {};
    WCHAR Threading[32] = {};
    DWORD Size = sizeof(Clsid);
    DWORD Count = 0;
    HKEY Type = nullptr;
    HKEY Driver = nullptr;
    HKEY Effects = nullptr;
    HKEY Server = nullptr;
    LONG Error;

    swprintf_s(Path, L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\VID_%04X&PID_%04X",
               LOWORD(Info->guidProduct.Data1), HIWORD(Info->guidProduct.Data1));
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, Path, 0, KEY_READ, &Type);
    printf("  ffreg type = %ld\n", Error);
    if (Error != ERROR_SUCCESS)
        return;

    Error = RegOpenKeyExW(Type, L"OEMForceFeedback", 0, KEY_READ, &Driver);
    printf("  ffreg driver = %ld\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegQueryValueExW(Driver, L"CLSID", nullptr, nullptr, reinterpret_cast<PBYTE>(Clsid), &Size);
        printf("  ffreg clsid = %ld '%ls'\n", Error, Clsid);
        if (Error == ERROR_SUCCESS)
        {
            swprintf_s(Path, L"CLSID\\%s\\InProcServer32", Clsid);
            Size = sizeof(Threading);
            Error = RegOpenKeyExW(HKEY_CLASSES_ROOT, Path, 0, KEY_READ, &Server);
            if (Error == ERROR_SUCCESS)
            {
                Error = RegQueryValueExW(Server, L"ThreadingModel", nullptr, nullptr, reinterpret_cast<PBYTE>(Threading), &Size);
                RegCloseKey(Server);
            }
            printf("  ffreg threading = %ld '%ls'\n", Error, Threading);
        }

        Error = RegOpenKeyExW(Driver, L"Effects", 0, KEY_READ, &Effects);
        if (Error == ERROR_SUCCESS)
        {
            RegQueryInfoKeyW(Effects, nullptr, nullptr, nullptr, &Count, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(Effects);
        }
        printf("  ffreg effects = %ld %lu\n", Error, Count);
        RegCloseKey(Driver);
    }
    RegCloseKey(Type);
}

/* Matches each HID device's registry entries against the effects and flags the runtime reports */
static
VOID
DcCheckFFRegistry(VOID)
{
    IDirectInput7W* Input = nullptr;
    GUID Found[64] = {};
    ULONG Count;

    printf("ffregistry create = %08lX\n", DcCreateEx(DcSelf, 0x0700, IID_IDirectInput7W, reinterpret_cast<PVOID*>(&Input), nullptr));
    Input->EnumDevices(DIDEVTYPE_HID, [](LPCDIDEVICEINSTANCEW Info, LPVOID Context) -> BOOL
    {
        GUID* List = static_cast<GUID*>(Context);
        ULONG* Total = reinterpret_cast<ULONG*>(List + 63);

        if (*Total < 62)
            List[(*Total)++] = Info->guidInstance;
        return TRUE;
    }, Found, DIEDFL_ALLDEVICES);
    Count = *reinterpret_cast<ULONG*>(Found + 63);

    for (ULONG Index = 0; Index < Count && Index < 62; Index++)
    {
        IDirectInputDevice7W* Device = nullptr;
        DIDEVICEINSTANCEW Info = {};
        DIDEVCAPS Caps = {};
        ULONG Effects = 0;
        HRESULT hr = Input->CreateDeviceEx(Found[Index], IID_IDirectInputDevice7W, reinterpret_cast<PVOID*>(&Device), nullptr);

        printf("ffregistry hid %lu = %08lX\n", Index, hr);
        if (FAILED(hr))
            continue;

        Info.dwSize = sizeof(Info);
        Device->GetDeviceInfo(&Info);
        DcPrintFFRegistry(&Info);

        Caps.dwSize = sizeof(Caps);
        hr = Device->GetCapabilities(&Caps);
        printf("  ffcaps = %08lX %08lX %lu %lu %08lX\n", hr, Caps.dwFlags & 0xFF00, Caps.dwFFSamplePeriod,
               Caps.dwFFMinTimeResolution, Caps.dwFFDriverVersion);

        hr = Device->EnumEffects([](LPCDIEFFECTINFOW Effect, LPVOID Context) -> BOOL
        {
            (*static_cast<ULONG*>(Context))++;
            printf("  ffeffect %s %08lX %08lX %08lX '%ls'\n", DcGuid(Effect->guid).c_str(), Effect->dwEffType,
                   Effect->dwStaticParams, Effect->dwDynamicParams, Effect->tszName);
            return TRUE;
        }, &Effects, DIEFT_ALL);
        printf("  ffeffects = %08lX %lu\n", hr, Effects);
        DcRelease("  device", Device);
    }

    DcRelease("ffregistry input", Input);
}

static
VOID
DcCheckEffectFiles(VOID)
{
    static const GUID Custom = { 0x11223344, 0x5566, 0x7788, { 9, 10, 11, 12, 13, 14, 15, 16 } };
    IDirectInput7W* Input = nullptr;
    IDirectInputDevice7W* Device = nullptr;
    IDirectInputDevice7A* DeviceA = nullptr;
    DWORD Axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG Directions[2] = { 4500, -100 };
    DIENVELOPE Envelope = { sizeof(DIENVELOPE), 100, 200, 300, 400 };
    DICONSTANTFORCE Constant = { 5000 };
    DIPERIODIC Periodic = { 1000, -200, 9000, 50000 };
    DIEFFECT Effects[4] = {};
    DIFILEEFFECT Entries[4] = {};
    CHAR Path[MAX_PATH];
    CHAR Other[MAX_PATH];
    WCHAR WidePath[MAX_PATH];
    ULONG Count;
    HRESULT hr;

    DcCreateEx(DcSelf, 0x0700, IID_IDirectInput7W, reinterpret_cast<PVOID*>(&Input), nullptr);
    Input->CreateDeviceEx(GUID_SysKeyboard, IID_IDirectInputDevice7W, reinterpret_cast<PVOID*>(&Device), nullptr);
    Device->QueryInterface(IID_IDirectInputDevice7A, reinterpret_cast<PVOID*>(&DeviceA));

    for (ULONG Index = 0; Index < _countof(Effects); Index++)
    {
        Effects[Index].dwSize = sizeof(DIEFFECT);
        Effects[Index].dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
        Effects[Index].dwDuration = 1000000 + Index;
        Effects[Index].dwSamplePeriod = 10 + Index;
        Effects[Index].dwGain = 9000;
        Effects[Index].dwTriggerButton = DIEB_NOTRIGGER;
        Effects[Index].dwTriggerRepeatInterval = 7;
        Effects[Index].cAxes = 2;
        Effects[Index].rgdwAxes = Axes;
        Effects[Index].rglDirection = Directions;
        Effects[Index].dwStartDelay = 33;
        Entries[Index].dwSize = sizeof(DIFILEEFFECT);
        Entries[Index].lpDiEffect = &Effects[Index];
        sprintf_s(Entries[Index].szFriendlyName, "effect number %lu", Index);
    }
    Entries[0].GuidEffect = GUID_ConstantForce;
    Effects[0].lpEnvelope = &Envelope;
    Effects[0].cbTypeSpecificParams = sizeof(Constant);
    Effects[0].lpvTypeSpecificParams = &Constant;
    Entries[1].GuidEffect = Custom;
    Entries[2].GuidEffect = GUID_Sine;
    Effects[2].cbTypeSpecificParams = sizeof(Periodic);
    Effects[2].lpvTypeSpecificParams = &Periodic;
    Effects[2].cAxes = 1;
    Entries[3].GuidEffect = GUID_Spring;
    Effects[3].rgdwAxes = nullptr;
    Effects[3].rglDirection = nullptr;

    for (ULONG Flags = 0; Flags < 2; Flags++)
    {
        sprintf_s(Path, "%ls\\dinputcheck_effects_%lu.ffe", DcScratch, Flags);
        DeleteFileA(Path);
        hr = DeviceA->WriteEffectToFile(Path, _countof(Entries), Entries, Flags);
        printf("  writeeffects flags %lu = %08lX\n", Flags, hr);
        DcDumpFile(Path);

        for (DWORD EnumFlags = 0; EnumFlags <= DIFEF_MODIFYIFNEEDED; EnumFlags += (EnumFlags == 0) ? 1 : 15)
        {
            Count = 0;
            hr = DeviceA->EnumEffectsInFile(Path, DcEnumFileEffects, &Count, EnumFlags);
            printf("  enumeffectsinfile flags %lu = %08lX count %lu\n", EnumFlags, hr, Count);
        }
    }

    MultiByteToWideChar(CP_ACP, 0, Path, -1, WidePath, _countof(WidePath));
    Count = 0;
    printf("  enumeffectsinfileW = %08lX\n", Device->EnumEffectsInFile(WidePath, DcEnumFileEffects, &Count, DIFEF_INCLUDENONSTANDARD));

    /* A long friendly name is refused, a long path is refused, and so are odd flags */
    memset(Entries[0].szFriendlyName, 'x', 259);
    Entries[0].szFriendlyName[259] = '\0';
    sprintf_s(Other, "%ls\\dinputcheck_longname.ffe", DcScratch);
    DeleteFileA(Other);
    printf("  writeeffects longname = %08lX\n", DeviceA->WriteEffectToFile(Other, 1, Entries, 0));
    DcDumpFile(Other);
    sprintf_s(Entries[0].szFriendlyName, "renamed");

    printf("  writeeffects null effects = %08lX\n", DeviceA->WriteEffectToFile(Other, 0, nullptr, 0));
    printf("  writeeffects badflags = %08lX\n", DeviceA->WriteEffectToFile(Other, 1, Entries, 2));
    printf("  writeeffects null name = %08lX\n", DeviceA->WriteEffectToFile(nullptr, 1, Entries, 0));
    printf("  writeeffects empty name = %08lX\n", DeviceA->WriteEffectToFile("", 1, Entries, 0));
    DeleteFileA(Other);
    printf("  writeeffects none = %08lX\n", DeviceA->WriteEffectToFile(Other, 0, Entries, 0));
    DcDumpFile(Other);
    printf("  writeeffectsW = %08lX\n", Device->WriteEffectToFile(L"", 1, Entries, 0));

    Count = 0;
    printf("  enumeffectsinfile missing = %08lX\n", DeviceA->EnumEffectsInFile("C:\\nonexistent\\nothing.ffe", DcEnumFileEffects, &Count, 0));
    printf("  enumeffectsinfile null callback = %08lX\n", DeviceA->EnumEffectsInFile(Path, nullptr, nullptr, 0));
    printf("  enumeffectsinfile badflags = %08lX\n", DeviceA->EnumEffectsInFile(Path, DcEnumFileEffects, &Count, 2));
    printf("  enumeffectsinfile null name = %08lX\n", DeviceA->EnumEffectsInFile(nullptr, DcEnumFileEffects, &Count, 0));

    /* A file that is not an effect file */
    sprintf_s(Other, "%ls\\dinputcheck_notriff.ffe", DcScratch);
    {
        HANDLE File = CreateFileA(Other, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD Written;

        WriteFile(File, "RIFF\x10\x00\x00\x00WAVEjunkjunkjunk", 24, &Written, nullptr);
        CloseHandle(File);
    }
    printf("  enumeffectsinfile notriff = %08lX\n", DeviceA->EnumEffectsInFile(Other, DcEnumFileEffects, &Count, 0));

    DeviceA->Release();
    Device->Release();
    Input->Release();
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    PCWSTR Runtime = L"system";
    HMODULE Module;
    WCHAR Path[MAX_PATH];

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
        else if (_wcsicmp(argv[Index], L"-scratch") == 0 && Index + 1 < argc)
            DcScratch = argv[++Index];
    }

    Module = LoadLibraryW(DcRuntimePath(Runtime).c_str());
    if (Module == nullptr)
    {
        fprintf(stderr, "error: cannot load the runtime (%lu)\n", GetLastError());
        return 1;
    }

    GetModuleFileNameW(Module, Path, _countof(Path));
    fprintf(stderr, "runtime %ls\n", Path);

    DcCreateA = reinterpret_cast<PDC_CREATE>(GetProcAddress(Module, "DirectInputCreateA"));
    DcCreateW = reinterpret_cast<PDC_CREATE>(GetProcAddress(Module, "DirectInputCreateW"));
    DcCreateEx = reinterpret_cast<PDC_CREATE_EX>(GetProcAddress(Module, "DirectInputCreateEx"));
    DcGetClassObject = reinterpret_cast<PDC_GET_CLASS>(GetProcAddress(Module, "DllGetClassObject"));
    DcCanUnloadNow = reinterpret_cast<PDC_CAN_UNLOAD>(GetProcAddress(Module, "DllCanUnloadNow"));
    if (DcCreateA == nullptr || DcCreateW == nullptr || DcCreateEx == nullptr || DcGetClassObject == nullptr || DcCanUnloadNow == nullptr)
    {
        fprintf(stderr, "error: missing exports\n");
        return 1;
    }

    DcSelf = GetModuleHandleW(nullptr);
    DcWindow = CreateWindowExW(0, L"STATIC", L"dinputcheck", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, DcSelf, nullptr);
    DcChild = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD, 0, 0, 10, 10, DcWindow, nullptr, DcSelf, nullptr);

    printf("cancanunload start = %08lX\n", DcCanUnloadNow());
    DcCheckCreate();
    DcCheckFactory();
    DcCheckEnumDevices(0x0300);
    DcCheckEnumDevices(0x0500);
    DcCheckEnumDevices(0x0700);
    DcCheckInputObject();
    DcCheckDevices();
    DcCheckFFRegistry();
    if (DcScratch != nullptr)
        DcCheckEffectFiles();
    printf("cancanunload end = %08lX\n", DcCanUnloadNow());

    DestroyWindow(DcChild);
    DestroyWindow(DcWindow);
    return 0;
}
