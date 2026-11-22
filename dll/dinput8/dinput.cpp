/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirectInput8 object: device creation, enumeration and lookup
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8.hpp"

const DI_STATIC_DEVICE DiStaticDevices[DI_STATIC_DEVICE_COUNT] =
{
    { &GUID_SysMouse, DI8DEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysMouseEm, DI8DEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysMouseEm2, DI8DEVTYPE_MOUSE, DiCreateMouse },
    { &GUID_SysKeyboard, DI8DEVTYPE_KEYBOARD, DiCreateKeyboard },
    { &GUID_SysKeyboardEm, DI8DEVTYPE_KEYBOARD, DiCreateKeyboard },
    { &GUID_SysKeyboardEm2, DI8DEVTYPE_KEYBOARD, DiCreateKeyboard },
};

/* Device types each DI8DEVCLASS value covers */
#define DI_GAME_CONTROLLER_TYPES    0x15F00000

/* Flags EnumDevices accepts and the device type bits it accepts */
#define DI_ENUM_FLAGS_VALID         (DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK | DIEDFL_INCLUDEALIASES | \
                                     DIEDFL_INCLUDEPHANTOMS | DIEDFL_INCLUDEHIDDEN)

static
BOOLEAN
DiTypeMatches(
    _In_ BYTE Filter,
    _In_ BYTE Type)
{
    if (Filter == 0 || Filter == Type)
        return TRUE;
    switch (Filter)
    {
        case DI8DEVCLASS_DEVICE:
            return Type == DI8DEVTYPE_DEVICE;
        case DI8DEVCLASS_POINTER:
            return Type == DI8DEVTYPE_MOUSE || Type == DI8DEVTYPE_SCREENPOINTER;
        case DI8DEVCLASS_KEYBOARD:
            return Type == DI8DEVTYPE_KEYBOARD;
        case DI8DEVCLASS_GAMECTRL:
            return Type < 32 && (DI_GAME_CONTROLLER_TYPES >> Type) & 1;
        default:
            return FALSE;
    }
}

/*
 * A device passes when it has every capability the low flags ask for and none of the
 * alias, phantom and hidden capabilities the caller did not ask to see.
 */
static
BOOLEAN
DiFlagsMatch(
    _In_ DWORD Wanted,
    _In_ DWORD Have)
{
    DWORD Required = Wanted & ~DI_CAPS_FLAGS_MASK;
    DWORD Excluded = ~Wanted & DI_CAPS_FLAGS_MASK;

    return (Have & Required) == Required && (Have & Excluded) == 0;
}

/* Opens a device to read its capabilities and identity, as enumeration does for every candidate */
static
HRESULT
DiProbeDevice(
    _In_ IDirectInput8W* Input,
    _In_ REFGUID Guid,
    _Out_ IDirectInputDevice8W** Device,
    _Out_ DIDEVCAPS* Caps)
{
    HRESULT Result;

    *Device = nullptr;
    Result = Input->CreateDevice(Guid, Device, nullptr);
    if (FAILED(Result))
        return Result;
    ZeroMemory(Caps, sizeof(*Caps));
    Caps->dwSize = sizeof(DIDEVCAPS_DX3);
    Result = (*Device)->GetCapabilities(Caps);
    if (FAILED(Result))
    {
        (*Device)->Release();
        *Device = nullptr;
    }
    return Result;
}

class DiInput : public DiObject, public IDirectInput8W
{
public:
    DWORD Version;
    IUnknown* JoyConfig;
    CRITICAL_SECTION Lock;

    class AnsiFace : public IDirectInput8A
    {
    public:
        DiInput* Owner;

        STDMETHODIMP
        QueryInterface(
            _In_ REFIID Riid,
            _Outptr_ VOID** Object) override
        {
            return Owner->OuterQuery(Riid, Object);
        }

        STDMETHODIMP_(ULONG)
        AddRef() override
        {
            return Owner->OuterAddRef();
        }

        STDMETHODIMP_(ULONG)
        Release() override
        {
            return Owner->OuterRelease();
        }

        STDMETHODIMP
        CreateDevice(
            _In_ REFGUID Guid,
            _Out_ LPDIRECTINPUTDEVICE8A* Device,
            _In_opt_ LPUNKNOWN Outer) override
        {
            return Owner->CreateDeviceCore(Guid, reinterpret_cast<VOID**>(Device), Outer, IID_IDirectInputDevice8A);
        }

        STDMETHODIMP
        EnumDevices(
            _In_ DWORD Type,
            _In_ LPDIENUMDEVICESCALLBACKA Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Flags) override;

        STDMETHODIMP
        GetDeviceStatus(
            _In_ REFGUID Guid) override
        {
            return Owner->GetDeviceStatus(Guid);
        }

        STDMETHODIMP
        RunControlPanel(
            _In_opt_ HWND Window,
            _In_ DWORD Flags) override
        {
            return Owner->RunControlPanel(Window, Flags);
        }

        STDMETHODIMP
        Initialize(
            _In_ HINSTANCE Instance,
            _In_ DWORD Version) override
        {
            return Owner->Initialize(Instance, Version);
        }

        STDMETHODIMP
        FindDevice(
            _In_ REFGUID Class,
            _In_z_ LPCSTR Name,
            _Out_ LPGUID Guid) override;

        STDMETHODIMP
        EnumDevicesBySemantics(
            _In_opt_z_ LPCSTR UserName,
            _In_ LPDIACTIONFORMATA Format,
            _In_ LPDIENUMDEVICESBYSEMANTICSCBA Callback,
            _In_opt_ VOID* Context,
            _In_ DWORD Flags) override
        {
            UNREFERENCED_PARAMETER(UserName);
            UNREFERENCED_PARAMETER(Format);
            UNREFERENCED_PARAMETER(Callback);
            UNREFERENCED_PARAMETER(Context);
            UNREFERENCED_PARAMETER(Flags);
            DI_TRACE_ONCE(L"EnumDevicesBySemantics is not implemented");
            return E_NOTIMPL;
        }

        STDMETHODIMP
        ConfigureDevices(
            _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
            _In_ LPDICONFIGUREDEVICESPARAMSA Params,
            _In_ DWORD Flags,
            _In_opt_ VOID* Data) override
        {
            UNREFERENCED_PARAMETER(Callback);
            UNREFERENCED_PARAMETER(Params);
            UNREFERENCED_PARAMETER(Flags);
            UNREFERENCED_PARAMETER(Data);
            DI_TRACE_ONCE(L"ConfigureDevices is not implemented");
            return E_NOTIMPL;
        }
    };

    AnsiFace Ansi;

    DiInput(
        _In_opt_ IUnknown* Outer) : DiObject(Outer), Version(0), JoyConfig(nullptr)
    {
        Ansi.Owner = this;
        InitializeCriticalSection(&Lock);
    }

    ~DiInput() override
    {
        if (JoyConfig != nullptr)
            JoyConfig->Release();
        DeleteCriticalSection(&Lock);
    }

    VOID*
    FindInterface(
        _In_ REFIID Riid) override
    {
        if (IsEqualIID(Riid, IID_IDirectInput8W) || DiIsUnknown(Riid))
            return static_cast<IDirectInput8W*>(this);
        if (IsEqualIID(Riid, IID_IDirectInput8A))
            return static_cast<IDirectInput8A*>(&Ansi);
        return nullptr;
    }

    HRESULT
    CreateDeviceCore(
        _In_ REFGUID Guid,
        _Out_ VOID** Device,
        _In_opt_ IUnknown* Outer,
        _In_ REFIID Face);

    HRESULT
    EnumDevicesCore(
        _In_ DWORD Type,
        _In_ LPDIENUMDEVICESCALLBACKW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags);

    /* IUnknown */

    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _Outptr_ VOID** Object) override
    {
        return OuterQuery(Riid, Object);
    }

    STDMETHODIMP_(ULONG)
    AddRef() override
    {
        return OuterAddRef();
    }

    STDMETHODIMP_(ULONG)
    Release() override
    {
        return OuterRelease();
    }

    /* IDirectInput8W */

    STDMETHODIMP
    CreateDevice(
        _In_ REFGUID Guid,
        _Out_ LPDIRECTINPUTDEVICE8W* Device,
        _In_opt_ LPUNKNOWN Outer) override
    {
        return CreateDeviceCore(Guid, reinterpret_cast<VOID**>(Device), Outer, IID_IDirectInputDevice8W);
    }

    STDMETHODIMP
    EnumDevices(
        _In_ DWORD Type,
        _In_ LPDIENUMDEVICESCALLBACKW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags) override
    {
        return EnumDevicesCore(Type, Callback, Context, Flags);
    }

    STDMETHODIMP
    GetDeviceStatus(
        _In_ REFGUID Guid) override;

    STDMETHODIMP
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) override;

    STDMETHODIMP
    Initialize(
        _In_ HINSTANCE Instance,
        _In_ DWORD NewVersion) override
    {
        HRESULT Result = DiCheckVersion(Instance, NewVersion);

        if (SUCCEEDED(Result))
            Version = NewVersion;
        return Result;
    }

    STDMETHODIMP
    FindDevice(
        _In_ REFGUID Class,
        _In_z_ LPCWSTR Name,
        _Out_ LPGUID Guid) override;

    STDMETHODIMP
    EnumDevicesBySemantics(
        _In_opt_z_ LPCWSTR UserName,
        _In_ LPDIACTIONFORMATW Format,
        _In_ LPDIENUMDEVICESBYSEMANTICSCBW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags) override
    {
        UNREFERENCED_PARAMETER(UserName);
        UNREFERENCED_PARAMETER(Format);
        UNREFERENCED_PARAMETER(Callback);
        UNREFERENCED_PARAMETER(Context);
        UNREFERENCED_PARAMETER(Flags);
        DI_TRACE_ONCE(L"EnumDevicesBySemantics is not implemented");
        return E_NOTIMPL;
    }

    STDMETHODIMP
    ConfigureDevices(
        _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
        _In_ LPDICONFIGUREDEVICESPARAMSW Params,
        _In_ DWORD Flags,
        _In_opt_ VOID* Data) override
    {
        UNREFERENCED_PARAMETER(Callback);
        UNREFERENCED_PARAMETER(Params);
        UNREFERENCED_PARAMETER(Flags);
        UNREFERENCED_PARAMETER(Data);
        DI_TRACE_ONCE(L"ConfigureDevices is not implemented");
        return E_NOTIMPL;
    }
};

HRESULT
DiInput::CreateDeviceCore(
    _In_ REFGUID Guid,
    _Out_ VOID** Device,
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Face)
{
    IDirectInputDevice8W* Created = nullptr;
    HRESULT Result;

    if (Device == nullptr)
        return E_POINTER;
    *Device = nullptr;
    Result = DiSharedInitialize();
    if (FAILED(Result))
        return Result;

    /* An aggregated device comes back uninitialized for its owner to set up */
    if (Outer != nullptr)
        return DiCreateDevice(Outer, IID_IUnknown, Device);

    Result = DiCreateDevice(nullptr, Face, reinterpret_cast<VOID**>(&Created));
    if (FAILED(Result))
        return Result;
    Result = Created->Initialize(DiInstance, Version, Guid);
    if (FAILED(Result))
    {
        Created->Release();
        return Result;
    }
    *Device = Created;
    return Result;
}

HRESULT
DiInput::EnumDevicesCore(
    _In_ DWORD Type,
    _In_ LPDIENUMDEVICESCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    BYTE Class = LOBYTE(Type);
    HRESULT Result;

    if (Callback == nullptr || (Class > DI8DEVCLASS_GAMECTRL && (Class < DI8DEVTYPE_DEVICE || Class > DI8DEVTYPE_SUPPLEMENTAL)))
    {
        DI_TRACE_ONCE(L"EnumDevices rejected type %#lx", Type);
        return E_INVALIDARG;
    }
    if ((Type & ~(0xFF | DIDEVTYPE_HID)) != 0 || (Flags & ~DI_ENUM_FLAGS_VALID) != 0)
    {
        DI_TRACE_ONCE(L"EnumDevices rejected type %#lx flags %#lx", Type, Flags);
        return E_INVALIDARG;
    }
    Result = DiCheckVersion(DiInstance, Version);
    if (FAILED(Result))
        return Result;

    AddRef();
    for (UINT Index = (Type & DIDEVTYPE_HID) ? DI_STATIC_DEVICE_COUNT : 0; Index < DI_STATIC_DEVICE_COUNT; Index++)
    {
        IDirectInputDevice8W* Device;
        DIDEVCAPS Caps;
        DIDEVICEINSTANCEW Instance;
        BOOL Continue;

        if (FAILED(DiProbeDevice(this, *DiStaticDevices[Index].Guid, &Device, &Caps)))
            continue;
        if (!DiFlagsMatch(Flags, Caps.dwFlags))
        {
            Device->Release();
            continue;
        }
        ZeroMemory(&Instance, sizeof(Instance));
        Instance.dwSize = sizeof(Instance);
        if (FAILED(Device->GetDeviceInfo(&Instance)) || !DiTypeMatches(Class, LOBYTE(Instance.dwDevType)))
        {
            Device->Release();
            continue;
        }
        Continue = Callback(&Instance, Context);
        Device->Release();
        if (Continue == DIENUM_STOP)
            break;
    }
    Release();
    return S_OK;
}

STDMETHODIMP
DiInput::GetDeviceStatus(
    _In_ REFGUID Guid)
{
    IDirectInputDevice8W* Device;
    DIDEVCAPS Caps;
    HRESULT Result;

    Result = DiProbeDevice(this, Guid, &Device, &Caps);
    if (FAILED(Result))
        return Result;
    Device->Release();
    return DiFlagsMatch(DIEDFL_ATTACHEDONLY, Caps.dwFlags) ? DI_OK : DI_NOTATTACHED;
}

STDMETHODIMP
DiInput::RunControlPanel(
    _In_opt_ HWND Window,
    _In_ DWORD Flags)
{
    HRESULT Result;

    if (Window != nullptr && !IsWindow(Window))
        return E_HANDLE;
    if (Flags != 0)
        return E_INVALIDARG;
    Result = DiCheckVersion(DiInstance, Version);
    if (FAILED(Result))
        return Result;
    return DiLaunchControlPanel(L"");
}

STDMETHODIMP
DiInput::FindDevice(
    _In_ REFGUID Class,
    _In_z_ LPCWSTR Name,
    _Out_ LPGUID Guid)
{
    if (Name == nullptr || Guid == nullptr)
        return E_POINTER;
    if (!IsEqualGUID(Class, GUID_HIDClass))
        return DIERR_DEVICENOTREG;
    return DIERR_DEVICENOTREG;
}

/* The ANSI face converts each instance before handing it on */
struct DI_ANSI_ENUM
{
    LPDIENUMDEVICESCALLBACKA Callback;
    VOID* Context;
};

static
BOOL
CALLBACK
DiAnsiEnumThunk(
    _In_ LPCDIDEVICEINSTANCEW Wide,
    _In_ VOID* Context)
{
    DI_ANSI_ENUM* Enum = static_cast<DI_ANSI_ENUM*>(Context);
    DIDEVICEINSTANCEA Ansi;

    ZeroMemory(&Ansi, sizeof(Ansi));
    Ansi.dwSize = sizeof(Ansi);
    DiInstanceToAnsi(&Ansi, Wide);
    return Enum->Callback(&Ansi, Enum->Context);
}

STDMETHODIMP
DiInput::AnsiFace::EnumDevices(
    _In_ DWORD Type,
    _In_ LPDIENUMDEVICESCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    DI_ANSI_ENUM Enum;

    if (Callback == nullptr)
        return E_INVALIDARG;
    Enum.Callback = Callback;
    Enum.Context = Context;
    return Owner->EnumDevicesCore(Type, DiAnsiEnumThunk, &Enum, Flags);
}

STDMETHODIMP
DiInput::AnsiFace::FindDevice(
    _In_ REFGUID Class,
    _In_z_ LPCSTR Name,
    _Out_ LPGUID Guid)
{
    WCHAR Wide[MAX_PATH];

    if (Name == nullptr)
        return E_POINTER;
    MultiByteToWideChar(CP_ACP, 0, Name, -1, Wide, _countof(Wide));
    return Owner->FindDevice(Class, Wide, Guid);
}

VOID
DiInstanceToAnsi(
    _Inout_ LPDIDEVICEINSTANCEA Ansi,
    _In_ LPCDIDEVICEINSTANCEW Wide)
{
    Ansi->guidInstance = Wide->guidInstance;
    Ansi->guidProduct = Wide->guidProduct;
    Ansi->dwDevType = Wide->dwDevType;
    WideCharToMultiByte(CP_ACP, 0, Wide->tszInstanceName, -1, Ansi->tszInstanceName, MAX_PATH, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, Wide->tszProductName, -1, Ansi->tszProductName, MAX_PATH, nullptr, nullptr);
    if (Ansi->dwSize >= sizeof(DIDEVICEINSTANCEA))
    {
        Ansi->guidFFDriver = Wide->guidFFDriver;
        Ansi->wUsagePage = Wide->wUsagePage;
        Ansi->wUsage = Wide->wUsage;
    }
}

HRESULT
DiCreateInput(
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result)
{
    DiInput* Input;
    HRESULT Status;

    Status = DiSharedInitialize();
    if (FAILED(Status))
        return Status;
    if (Result == nullptr)
        return E_POINTER;
    *Result = nullptr;
    if (Outer != nullptr && !DiIsUnknown(Riid))
        return CLASS_E_NOAGGREGATION;
    Input = new (std::nothrow) DiInput(Outer);
    if (Input == nullptr)
        return E_OUTOFMEMORY;
    return DiFinishCreate(Input, Outer, Riid, Result);
}
