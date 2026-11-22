/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirectInput object, device enumeration and creation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"
#include "hid.hpp"

/* Device types EnumDevices accepts, DIDEVTYPE_JOYSTICK being the last */
#define DI_ENUM_TYPE_LIMIT      5
#define DI_ENUM_TYPE_INVALID    0xFFFEFF00
#define DI_ENUM_FLAGS_INVALID   0xFFFCFEFE

/* Capability bits EnumDevices flags select on, with the alias and phantom bits inverted */
#define DI_ENUM_INVERTED_BITS   0x00FF0000

typedef struct _RX_DI_ENUM_A
{
    LPDIENUMDEVICESCALLBACKA Callback;
    LPVOID Context;
} RX_DI_ENUM_A;

static
VOID
DiDeviceInfoToA(
    _Inout_ LPDIDEVICEINSTANCEA Target,
    _In_ const DIDEVICEINSTANCEW* Source)
{
    Target->guidInstance = Source->guidInstance;
    Target->guidProduct = Source->guidProduct;
    Target->dwDevType = Source->dwDevType;
    WideCharToMultiByte(CP_ACP, 0, Source->tszInstanceName, -1, Target->tszInstanceName, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, Source->tszProductName, -1, Target->tszProductName, MAX_PATH, nullptr, nullptr);
    if (Target->dwSize >= sizeof(DIDEVICEINSTANCEA))
    {
        Target->guidFFDriver = Source->guidFFDriver;
        Target->wUsage = Source->wUsage;
        Target->wUsagePage = Source->wUsagePage;
    }
}

static
BOOL
CALLBACK
DiEnumDevicesToA(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ LPVOID Context)
{
    RX_DI_ENUM_A* Forward = static_cast<RX_DI_ENUM_A*>(Context);
    DIDEVICEINSTANCEA Narrow = {};

    Narrow.dwSize = sizeof(Narrow);
    DiDeviceInfoToA(&Narrow, Instance);
    return Forward->Callback(&Narrow, Forward->Context);
}

RxDirectInput::RxDirectInput(
    _In_opt_ IUnknown* OuterUnknown)
    : RxDiUnknown(OuterUnknown), CreatedVersion(0), JoyConfig(nullptr)
{
    InitializeCriticalSection(&Lock);
    LockReady = TRUE;
}

RxDirectInput::~RxDirectInput()
{
    IUnknown* Config = static_cast<IUnknown*>(InterlockedExchangePointer(reinterpret_cast<PVOID*>(&JoyConfig), nullptr));

    if (Config != nullptr)
        Config->Release();
    if (LockReady)
        DeleteCriticalSection(&Lock);
}

HRESULT
RxDirectInput::Create(
    _In_opt_ IUnknown* OuterUnknown,
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    RxDirectInput* Created;
    HRESULT hr;

    hr = DiStartup();
    if (FAILED(hr))
        return hr;

    if (&Iid == nullptr)
        return E_POINTER;

    if (OuterUnknown != nullptr && !IsEqualIID(Iid, IID_IUnknown))
    {
        *Object = nullptr;
        return CLASS_E_NOAGGREGATION;
    }

    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    Created = new RxDirectInput(OuterUnknown);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    if (OuterUnknown != nullptr)
    {
        *Object = Created->InnerUnknown();
        return S_OK;
    }

    hr = Created->InnerUnknown()->QueryInterface(Iid, Object);
    Created->InnerUnknown()->Release();
    return hr;
}

PVOID
RxDirectInput::QueryOwn(
    _In_ REFIID Iid)
{
    if (IsEqualIID(Iid, IID_IDirectInput7W))
        return static_cast<IDirectInput7W*>(this);
    if (IsEqualIID(Iid, IID_IDirectInput7A))
        return static_cast<IDirectInput7A*>(this);
    return nullptr;
}

HRESULT
RxDirectInput::QuerySlow(
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    IUnknown* Config;
    HRESULT hr = S_OK;

    if (IsEqualIID(Iid, IID_IDirectInputA) || IsEqualIID(Iid, IID_IDirectInput2A))
    {
        *Object = static_cast<IDirectInput7A*>(this);
        Outer->AddRef();
        return S_OK;
    }

    if (IsEqualIID(Iid, IID_IDirectInputW) || IsEqualIID(Iid, IID_IDirectInput2W))
    {
        *Object = static_cast<IDirectInput7W*>(this);
        Outer->AddRef();
        return S_OK;
    }

    if (!IsEqualIID(Iid, IID_IDirectInputJoyConfig))
    {
        *Object = nullptr;
        return E_NOINTERFACE;
    }

    /* The joystick configuration interface lives in an aggregated helper made on demand */
    *Object = nullptr;
    EnterCriticalSection(&Lock);
    if (JoyConfig == nullptr)
    {
        Config = nullptr;
        hr = DiCreateJoyConfig(Outer, &Config);
        JoyConfig = SUCCEEDED(hr) ? Config : nullptr;
    }
    LeaveCriticalSection(&Lock);

    if (FAILED(hr))
        return hr;
    return JoyConfig->QueryInterface(Iid, Object);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    if (&Iid == nullptr || Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    return Outer->QueryInterface(Iid, Object);
}

ULONG
STDMETHODCALLTYPE
RxDirectInput::AddRef(VOID)
{
    return Outer->AddRef();
}

ULONG
STDMETHODCALLTYPE
RxDirectInput::Release(VOID)
{
    return Outer->Release();
}

HRESULT
RxDirectInput::CreateDeviceCommon(
    _In_ REFGUID Guid,
    _In_ REFIID Iid,
    _Outptr_ PVOID* Device,
    _In_opt_ IUnknown* OuterUnknown)
{
    HRESULT hr;

    hr = RxDiDevice::Create(OuterUnknown, (OuterUnknown != nullptr) ? IID_IUnknown : Iid, Device);
    if (FAILED(hr) || OuterUnknown != nullptr)
        return hr;

    /* Initialize sits in the same slot of every device interface */
    hr = static_cast<IDirectInputDeviceW*>(*Device)->Initialize(DiModule, CreatedVersion, Guid);
    if (FAILED(hr))
    {
        IUnknown* Created = static_cast<IUnknown*>(InterlockedExchangePointer(Device, nullptr));

        if (Created != nullptr)
            Created->Release();
    }
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::CreateDevice(
    REFGUID Guid,
    LPDIRECTINPUTDEVICEW* Device,
    LPUNKNOWN OuterUnknown)
{
    return CreateDeviceCommon(Guid, IID_IDirectInputDeviceW, reinterpret_cast<PVOID*>(Device), OuterUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::CreateDevice(
    REFGUID Guid,
    LPDIRECTINPUTDEVICEA* Device,
    LPUNKNOWN OuterUnknown)
{
    return CreateDeviceCommon(Guid, IID_IDirectInputDeviceA, reinterpret_cast<PVOID*>(Device), OuterUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::CreateDeviceEx(
    REFGUID Guid,
    REFIID Iid,
    LPVOID* Device,
    LPUNKNOWN OuterUnknown)
{
    return CreateDeviceCommon(Guid, Iid, Device, OuterUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::EnumDevices(
    DWORD DevType,
    LPDIENUMDEVICESCALLBACKW Callback,
    LPVOID Context,
    DWORD Flags)
{
    RX_DI_HID_SNAPSHOT* Snapshot = nullptr;
    ULONG StaticIndex;
    ULONG HidIndex = 0;
    UCHAR Wanted = static_cast<UCHAR>(DevType);
    HRESULT hr;

    if (Callback == nullptr)
        return E_INVALIDARG;
    if (Wanted >= DI_ENUM_TYPE_LIMIT || (DevType & DI_ENUM_TYPE_INVALID) != 0)
        return E_INVALIDARG;
    if ((Flags & DI_ENUM_FLAGS_INVALID) != 0)
        return E_INVALIDARG;

    hr = DiCheckVersion(DiModule, CreatedVersion);
    if (FAILED(hr))
        return hr;

    DiRefreshHidList(TRUE);
    hr = DiSnapshotHidList(&Snapshot);
    if (FAILED(hr))
        return hr;

    AddRef();

    /* Asking for HID devices only skips the system keyboard and mouse */
    StaticIndex = (DevType & DIDEVTYPE_HID) ? _countof(DiStaticDevices) : 0;

    for (;;)
    {
        const GUID* Instance;
        UCHAR Type;
        IDirectInputDeviceW* Device = nullptr;
        DIDEVCAPS Caps;
        DIDEVICEINSTANCEW Info;
        BOOL Keep;

        if (StaticIndex < _countof(DiStaticDevices))
        {
            Instance = DiStaticDevices[StaticIndex].Instance;
            Type = DiStaticDevices[StaticIndex].Type;
            StaticIndex++;
        }
        else if (Snapshot != nullptr && HidIndex < Snapshot->Count)
        {
            Instance = &Snapshot->Entries[HidIndex].Instance;
            Type = Snapshot->Entries[HidIndex].DevType;
            HidIndex++;
        }
        else
        {
            break;
        }

        if (Wanted != 0 && Wanted != Type)
            continue;

        /* DirectInput 3 applications never saw joysticks here */
        if (CreatedVersion <= 0x0300 && Type == DIDEVTYPE_JOYSTICK)
            continue;

        if (FAILED(CreateDevice(*Instance, &Device, nullptr)))
            continue;

        ZeroMemory(&Caps, sizeof(Caps));
        Caps.dwSize = sizeof(DIDEVCAPS_DX3);
        Keep = SUCCEEDED(Device->GetCapabilities(&Caps)) &&
               ((Flags ^ DI_ENUM_INVERTED_BITS) & ~(Caps.dwFlags ^ DI_ENUM_INVERTED_BITS)) == 0;

        if (Keep)
        {
            ZeroMemory(&Info, sizeof(Info));
            Info.dwSize = sizeof(Info);
            Keep = SUCCEEDED(Device->GetDeviceInfo(&Info));
        }
        Device->Release();

        if (Keep && !Callback(&Info, Context))
            break;
    }

    Release();
    DiFree(reinterpret_cast<PVOID*>(&Snapshot));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::EnumDevices(
    DWORD DevType,
    LPDIENUMDEVICESCALLBACKA Callback,
    LPVOID Context,
    DWORD Flags)
{
    RX_DI_ENUM_A Forward;

    if (Callback == nullptr)
        return E_INVALIDARG;

    Forward.Callback = Callback;
    Forward.Context = Context;
    return EnumDevices(DevType, DiEnumDevicesToA, &Forward, Flags);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::GetDeviceStatus(
    REFGUID Guid)
{
    IDirectInputDeviceW* Device = nullptr;
    DIDEVCAPS Caps;
    HRESULT hr;

    hr = CreateDevice(Guid, &Device, nullptr);
    if (FAILED(hr))
        return hr;

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.dwSize = sizeof(DIDEVCAPS_DX3);
    hr = Device->GetCapabilities(&Caps);
    if (SUCCEEDED(hr))
    {
        /* Attached means the attached bit and none of the alias or phantom bits */
        hr = ((Caps.dwFlags & (DIDC_ATTACHED | DI_ENUM_INVERTED_BITS)) == DIDC_ATTACHED) ? DI_OK : DI_NOTATTACHED;
    }

    Device->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::RunControlPanel(
    HWND Owner,
    DWORD Flags)
{
    HRESULT hr;

    if (Owner != nullptr && !IsWindow(Owner))
        return E_HANDLE;
    if (Flags != 0)
        return E_INVALIDARG;

    hr = DiCheckVersion(DiModule, CreatedVersion);
    if (FAILED(hr))
        return hr;

    return DiRunControlPanel(L"");
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::Initialize(
    HINSTANCE Instance,
    DWORD Version)
{
    WCHAR Id[276] = {};
    WCHAR Name[MAX_PATH];
    HKEY Key;
    HRESULT hr;

    if (DiGetApplicationId(Id, Name) &&
        SUCCEEDED(DiOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectInput\\MostRecentApplication\\",
                            KEY_WRITE, 0, &Key)))
    {
        FILETIME Started = {};

        GetSystemTimeAsFileTime(&Started);
        RegSetValueExW(Key, L"Version", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Version), sizeof(Version));
        RegSetValueExW(Key, L"Name", 0, REG_SZ, reinterpret_cast<const BYTE*>(Name),
                       (lstrlenW(Name) + 1) * sizeof(WCHAR));
        RegSetValueExW(Key, L"Id", 0, REG_SZ, reinterpret_cast<const BYTE*>(Id),
                       (lstrlenW(Id) + 1) * sizeof(WCHAR));
        RegSetValueExW(Key, L"MostRecentStart", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Started),
                       sizeof(Started));
        RegCloseKey(Key);
    }

    hr = DiCheckVersion(Instance, Version);
    if (SUCCEEDED(hr))
        CreatedVersion = Version;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::FindDevice(
    REFGUID Class,
    LPCWSTR Name,
    LPGUID Instance)
{
    HRESULT hr;

    if (&Class == nullptr)
        return E_POINTER;

    hr = DiCheckString(Name, MAX_PATH);
    if (FAILED(hr))
        return hr;

    if (Instance == nullptr)
        return E_POINTER;

    if (!IsEqualGUID(Class, GUID_HIDClass))
        return DIERR_DEVICENOTREG;

    return DiFindHidByPath(Name, Instance);
}

HRESULT
STDMETHODCALLTYPE
RxDirectInput::FindDevice(
    REFGUID Class,
    LPCSTR Name,
    LPGUID Instance)
{
    WCHAR Wide[MAX_PATH];
    HRESULT hr;

    hr = DiCheckStringA(Name, MAX_PATH);
    if (FAILED(hr))
        return hr;

    MultiByteToWideChar(CP_ACP, 0, Name, -1, Wide, _countof(Wide));
    return FindDevice(Class, Wide, Instance);
}
