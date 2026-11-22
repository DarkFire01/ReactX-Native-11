/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirectInput8 object: device creation, enumeration and lookup
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8.hpp"
#include "actionmap.hpp"

#include <ddraw.h>
#include <stdlib.h>

/* The parts of a Direct3D 8 surface a configuration target check reads */
static const GUID DiIidDirect3DSurface8 =
    { 0xB96EEBCA, 0xB326, 0x4EA5, { 0x88, 0x2F, 0x2F, 0xF5, 0xBA, 0xE0, 0x21, 0xDD } };

struct DI_D3D8_SURFACE_DESC
{
    DWORD Format;
    DWORD Type;
    DWORD Usage;
    DWORD Pool;
    UINT Size;
    DWORD MultiSampleType;
    UINT Width;
    UINT Height;
};

class DiD3D8Surface : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetDevice(VOID** Device) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Guid, const VOID* Data, DWORD Size, DWORD Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Guid, VOID* Data, DWORD* Size) = 0;
    virtual HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID Guid) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetContainer(REFIID Riid, VOID** Container) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(DI_D3D8_SURFACE_DESC* Desc) = 0;
};

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

/* Sees one enumerated device; returning FALSE stops the enumeration */
typedef
BOOL
(*PDI_DEVICE_VISIT)(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ IDirectInputDevice8W* Device,
    _In_opt_ VOID* Context);

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
            _In_ DWORD Flags) override;

        STDMETHODIMP
        ConfigureDevices(
            _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
            _In_ LPDICONFIGUREDEVICESPARAMSA Params,
            _In_ DWORD Flags,
            _In_opt_ VOID* Data) override;
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
        _In_ DWORD Flags) override;

    STDMETHODIMP
    ConfigureDevices(
        _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
        _In_ LPDICONFIGUREDEVICESPARAMSW Params,
        _In_ DWORD Flags,
        _In_opt_ VOID* Data) override;

    /* Hands each device enumeration would report to Visit, which may keep it with a reference */
    HRESULT
    VisitDevices(
        _In_ DWORD Type,
        _In_ DWORD Flags,
        _In_ PDI_DEVICE_VISIT Visit,
        _In_opt_ VOID* Context);

    HRESULT
    EnumBySemanticsCore(
        _In_opt_z_ LPCWSTR UserName,
        _In_ LPCDIACTIONFORMATW Format,
        _In_ LPDIENUMDEVICESBYSEMANTICSCBW Callback,
        _In_opt_ VOID* Context,
        _In_ DWORD Flags);
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

struct DI_ENUM_VISIT
{
    LPDIENUMDEVICESCALLBACKW Callback;
    VOID* Context;
};

static
BOOL
DiEnumVisit(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ IDirectInputDevice8W* Device,
    _In_opt_ VOID* Context)
{
    DI_ENUM_VISIT* Visit = static_cast<DI_ENUM_VISIT*>(Context);

    UNREFERENCED_PARAMETER(Device);
    return Visit->Callback(Instance, Visit->Context) != DIENUM_STOP;
}

HRESULT
DiInput::EnumDevicesCore(
    _In_ DWORD Type,
    _In_ LPDIENUMDEVICESCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    BYTE Class = LOBYTE(Type);
    DI_ENUM_VISIT Visit;
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
    Visit.Callback = Callback;
    Visit.Context = Context;
    return VisitDevices(Type, Flags, DiEnumVisit, &Visit);
}

HRESULT
DiInput::VisitDevices(
    _In_ DWORD Type,
    _In_ DWORD Flags,
    _In_ PDI_DEVICE_VISIT Visit,
    _In_opt_ VOID* Context)
{
    BYTE Class = LOBYTE(Type);
    GUID* HidDevices = nullptr;
    DWORD HidCount = 0;
    HRESULT Result;

    /* The HID list is rebuilt before anything is enumerated */
    Result = DiListHidDevices(&HidDevices, &HidCount);
    if (FAILED(Result))
        return Result;

    AddRef();
    for (UINT Index = (Type & DIDEVTYPE_HID) ? DI_STATIC_DEVICE_COUNT : 0; Index < DI_STATIC_DEVICE_COUNT + HidCount; Index++)
    {
        const GUID* Guid = Index < DI_STATIC_DEVICE_COUNT ? DiStaticDevices[Index].Guid : &HidDevices[Index - DI_STATIC_DEVICE_COUNT];
        IDirectInputDevice8W* Device;
        DIDEVCAPS Caps;
        DIDEVICEINSTANCEW Instance;
        BOOL Continue;

        if (FAILED(DiProbeDevice(this, *Guid, &Device, &Caps)))
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
        Continue = Visit(&Instance, Device, Context);
        Device->Release();
        if (!Continue)
            break;
    }
    Release();
    free(HidDevices);
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

/* Semantic enumeration reports at most this many devices */
#define DI_SEMANTIC_DEVICE_MAX      32

/* Devices whose map time stamps lie this close to the newest one count as recent, in 100 ns units */
#define DI_RECENT_WINDOW            100000000

/* Ranks a device by how well it fits the genre; ranks are 1 to 21, with 21 not reported */
#define DI_RANK_USER_CONFIGURED     20
#define DI_RANK_SKIP                21
#define DI_RANK_SUPPLEMENTAL        6
#define DI_RANK_KEYBOARD            5
#define DI_RANK_MOUSE               4
#define DI_RANK_HID_KEYBOARD        3
#define DI_RANK_HID_MOUSE           2
#define DI_RANK_DEVICE              1

/* Device types each genre prefers, best first, by the genre's number */
static const BYTE DiGenreDevices[DI_GENRE_LAST + 1][12] =
{
    { DI8DEVTYPE_DRIVING },
    { DI8DEVTYPE_DRIVING },
    { DI8DEVTYPE_DRIVING },
    { DI8DEVTYPE_FLIGHT, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_FLIGHT, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_1STPERSON },
    { DI8DEVTYPE_1STPERSON },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_JOYSTICK, DI8DEVTYPE_GAMEPAD },
    { DI8DEVTYPE_JOYSTICK, DI8DEVTYPE_GAMEPAD },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_1STPERSON, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_1STPERSON, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_1STPERSON, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_1STPERSON, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_GAMEPAD },
    { DI8DEVTYPE_GAMEPAD, DI8DEVTYPE_JOYSTICK },
    { DI8DEVTYPE_JOYSTICK },
};

/* Game controller ranks follow the genre's preference list; types it does not list rank 7 */
static
DWORD
DiGenreRank(
    _In_ DWORD Genre,
    _In_ BYTE Type)
{
    DWORD Slot = _countof(DiGenreDevices[0]);
    DWORD Number = (Genre >> 24) & 0x7F;

    if (Number <= DI_GENRE_LAST)
    {
        for (Slot = 0; Slot < _countof(DiGenreDevices[0]); Slot++)
        {
            if (DiGenreDevices[Number][Slot] == Type)
                break;
        }
    }
    return 19 - Slot;
}

/* Sort key of a device: rank, then how many actions it took, then whether it has force feedback */
static
DWORD
DiSemanticOrder(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ LPCDIACTIONFORMATW Format,
    _In_ DWORD Flags)
{
    BOOLEAN Hid = (Instance->dwDevType & DIDEVTYPE_HID) != 0;
    BOOLEAN ByUser = FALSE;
    DWORD Rank = 0;
    DWORD Mapped = 0;

    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        if (IsEqualGUID(Format->rgoAction[Index].guidInstance, Instance->guidInstance))
        {
            ByUser = (Format->rgoAction[Index].dwHow & DIAH_USERCONFIG) != 0;
            break;
        }
    }
    if (ByUser)
    {
        Rank = DI_RANK_USER_CONFIGURED;
    }
    else
    {
        switch (LOBYTE(Instance->dwDevType))
        {
            case DI8DEVTYPE_DEVICE:
                Rank = (Flags & DIEDBSFL_NONGAMINGDEVICES) ? DI_RANK_DEVICE : DI_RANK_SKIP;
                break;
            case DI8DEVTYPE_MOUSE:
                if (!Hid)
                    Rank = DI_RANK_MOUSE;
                else
                    Rank = (Flags & DIEDBSFL_MULTIMICEKEYBOARDS) ? DI_RANK_HID_MOUSE : DI_RANK_SKIP;
                break;
            case DI8DEVTYPE_KEYBOARD:
                if (!Hid)
                    Rank = DI_RANK_KEYBOARD;
                else
                    Rank = (Flags & DIEDBSFL_MULTIMICEKEYBOARDS) ? DI_RANK_HID_KEYBOARD : DI_RANK_SKIP;
                break;
            case DI8DEVTYPE_JOYSTICK:
            case DI8DEVTYPE_GAMEPAD:
            case DI8DEVTYPE_DRIVING:
            case DI8DEVTYPE_FLIGHT:
            case DI8DEVTYPE_1STPERSON:
            case DI8DEVTYPE_DEVICECTRL:
            case DI8DEVTYPE_SCREENPOINTER:
            case DI8DEVTYPE_REMOTE:
                Rank = DiGenreRank(Format->dwGenre, LOBYTE(Instance->dwDevType));
                break;
            case DI8DEVTYPE_SUPPLEMENTAL:
                Rank = DI_RANK_SUPPLEMENTAL;
                break;
            default:
                break;
        }
        if (Rank == DI_RANK_SKIP)
            return 0;
    }

    for (DWORD Index = 0; Index < Format->dwNumActions; Index++)
    {
        const DIACTIONW* Action = &Format->rgoAction[Index];

        if (IsEqualGUID(Action->guidInstance, Instance->guidInstance) && (Action->dwHow & DI_HOW_MAPPED_MASK))
            Mapped += 1;
    }
    return (Rank << 24) | (LOBYTE(Mapped) << 8) | (IsEqualGUID(Instance->guidFFDriver, GUID_NULL) ? 0 : 1);
}

struct DI_SEMANTIC_DEVICE
{
    DWORD Order;
    DWORD Flags;
    IDirectInputDevice8W* Device;
    DIDEVICEINSTANCEW Instance;
    FILETIME Stamp;
};

struct DI_SEMANTIC_ENUM
{
    LPCWSTR User;
    LPDIACTIONFORMATW Format;
    DWORD Flags;
    DWORD Count;
    DI_SEMANTIC_DEVICE Devices[DI_SEMANTIC_DEVICE_MAX];
};

/* Whether the device's recorded user starts with the name asked for */
static
BOOLEAN
DiDeviceUserIs(
    _In_ REFGUID Device,
    _In_opt_z_ LPCWSTR User)
{
    WCHAR Owner[MAX_PATH];
    SIZE_T Length = User != nullptr ? wcslen(User) : 0;

    if (DiGetDeviceUser(Device, Owner) != S_OK || Length >= MAX_PATH)
        return FALSE;
    return memcmp(User, Owner, Length * sizeof(WCHAR)) == 0;
}

static
BOOL
DiSemanticVisit(
    _In_ LPCDIDEVICEINSTANCEW Instance,
    _In_ IDirectInputDevice8W* Device,
    _In_opt_ VOID* Context)
{
    DI_SEMANTIC_ENUM* Enum = static_cast<DI_SEMANTIC_ENUM*>(Context);
    DI_SEMANTIC_DEVICE* Entry;
    BOOLEAN NeedsUser = FALSE;
    DWORD Order;
    DWORD Priorities = 0;

    if (Enum->Count >= DI_SEMANTIC_DEVICE_MAX)
        return FALSE;
    if (FAILED(Device->BuildActionMap(Enum->Format, Enum->User, Enum->User != nullptr ? 0 : DIDBAM_HWDEFAULTS)))
        return TRUE;

    /* Available devices are the ones nobody owns, unless this user owns them */
    if (Enum->Flags & DIEDBSFL_AVAILABLEDEVICES)
    {
        WCHAR Owner[MAX_PATH];

        NeedsUser = DiGetDeviceUser(Instance->guidInstance, Owner) == S_OK;
    }
    else
    {
        NeedsUser = (Enum->Flags & DIEDBSFL_THISUSER) != 0;
    }
    if (NeedsUser && (!(Enum->Flags & DIEDBSFL_THISUSER) || !DiDeviceUserIs(Instance->guidInstance, Enum->User)))
        return TRUE;

    Order = DiSemanticOrder(Instance, Enum->Format, Enum->Flags);
    if (Order == 0)
        return TRUE;
    for (DWORD Index = 0; Index < Enum->Format->dwNumActions && Priorities != 3; Index++)
    {
        const DIACTIONW* Action = &Enum->Format->rgoAction[Index];

        if (!IsEqualGUID(Action->guidInstance, Instance->guidInstance) || !(Action->dwHow & DI_HOW_MAPPED_MASK))
            continue;
        Priorities |= (Action->dwSemantic & DI_SEMANTIC_PRIORITY2) ? DIEDBS_MAPPEDPRI2 : DIEDBS_MAPPEDPRI1;
    }

    Entry = &Enum->Devices[Enum->Count++];
    Device->AddRef();
    Entry->Order = Order;
    Entry->Flags = Priorities;
    Entry->Device = Device;
    Entry->Instance = *Instance;
    Entry->Stamp = Enum->Format->ftTimeStamp;
    return TRUE;
}

static
BOOLEAN
DiStampIsUnsaved(
    _In_ const FILETIME* Stamp)
{
    return Stamp->dwLowDateTime == 0xFFFFFFFF && Stamp->dwHighDateTime == 0xFFFFFFFF;
}

/* Marks the device whose map is newest, the ones built shortly before it, and those never saved */
static
VOID
DiMarkRecentDevices(
    _Inout_ DI_SEMANTIC_ENUM* Enum)
{
    FILETIME Newest = {};
    DWORD Best = MAXDWORD;

    for (DWORD Index = 0; Index < Enum->Count; Index++)
    {
        const FILETIME* Stamp = &Enum->Devices[Index].Stamp;

        if (Stamp->dwLowDateTime == 0xFFFFFFFF || Stamp->dwHighDateTime == 0xFFFFFFFF)
            continue;
        if (CompareFileTime(Stamp, &Newest) == 1)
        {
            Best = Index;
            Newest = *Stamp;
        }
    }
    if (Best != MAXDWORD)
        Enum->Devices[Best].Flags |= DIEDBS_RECENTDEVICE;
    for (DWORD Index = 0; Index < Enum->Count; Index++)
    {
        DI_SEMANTIC_DEVICE* Entry = &Enum->Devices[Index];

        if (Best != MAXDWORD && Index != Best && Entry->Stamp.dwHighDateTime == Newest.dwHighDateTime &&
            Newest.dwLowDateTime - Entry->Stamp.dwLowDateTime < DI_RECENT_WINDOW)
        {
            Entry->Flags |= DIEDBS_RECENTDEVICE;
        }
        if (DiStampIsUnsaved(&Entry->Stamp))
            Entry->Flags |= DIEDBS_NEWDEVICE;
    }
}

HRESULT
DiInput::EnumBySemanticsCore(
    _In_opt_z_ LPCWSTR UserName,
    _In_ LPCDIACTIONFORMATW Format,
    _In_ LPDIENUMDEVICESBYSEMANTICSCBW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    DI_SEMANTIC_ENUM* Enum;
    DIACTIONFORMATW Copy;
    DWORD Visible;
    HRESULT Result;

    /* Every device builds into one private copy, so the caller's format stays as it was */
    Copy = *Format;
    Copy.rgoAction = static_cast<LPDIACTIONW>(malloc(Format->dwNumActions * sizeof(DIACTIONW)));
    Enum = static_cast<DI_SEMANTIC_ENUM*>(calloc(1, sizeof(*Enum)));
    if (Copy.rgoAction == nullptr || Enum == nullptr)
    {
        free(Copy.rgoAction);
        free(Enum);
        return E_OUTOFMEMORY;
    }
    memcpy(Copy.rgoAction, Format->rgoAction, Format->dwNumActions * sizeof(DIACTIONW));
    Enum->User = UserName;
    Enum->Format = &Copy;
    Enum->Flags = Flags;

    Visible = (Flags != 0 ? Flags : DIEDFL_ATTACHEDONLY) &
              ~(DIEDBSFL_THISUSER | DIEDBSFL_AVAILABLEDEVICES | DIEDBSFL_MULTIMICEKEYBOARDS | DIEDBSFL_NONGAMINGDEVICES);
    Result = VisitDevices(0, Visible, DiSemanticVisit, Enum);
    if (SUCCEEDED(Result))
    {
        /* Best first: each round moves the lowest remaining order to the end */
        for (DWORD Last = Enum->Count; Last > 1; Last -= 1)
        {
            DWORD Lowest = 0;

            for (DWORD Index = 1; Index < Last; Index++)
            {
                if (Enum->Devices[Index].Order < Enum->Devices[Lowest].Order)
                    Lowest = Index;
            }
            if (Lowest != Last - 1)
            {
                DI_SEMANTIC_DEVICE Swap = Enum->Devices[Lowest];

                Enum->Devices[Lowest] = Enum->Devices[Last - 1];
                Enum->Devices[Last - 1] = Swap;
            }
        }
        DiMarkRecentDevices(Enum);
    }

    for (DWORD Index = 0; Index < Enum->Count; Index++)
    {
        DI_SEMANTIC_DEVICE* Entry = &Enum->Devices[Index];

        if (Callback != nullptr &&
            !Callback(&Entry->Instance, Entry->Device, Entry->Flags, Enum->Count - Index - 1, Context))
        {
            Callback = nullptr;
        }
        Entry->Device->Release();
    }
    free(Copy.rgoAction);
    free(Enum);
    return Result;
}

STDMETHODIMP
DiInput::EnumDevicesBySemantics(
    _In_opt_z_ LPCWSTR UserName,
    _In_ LPDIACTIONFORMATW Format,
    _In_ LPDIENUMDEVICESBYSEMANTICSCBW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    HRESULT Result;

    Result = DiCheckActionFormat(Format);
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    Result = DiCheckActionNames(Format, FALSE);
    if (FAILED(Result))
        return Result;
    if (UserName != nullptr)
    {
        Result = DiCheckWideString(UserName, DI_MAP_USER_LIMIT);
        if (FAILED(Result))
            return Result;
    }
    if (Callback == nullptr || (Flags & ~DIEDBSFL_VALID) != 0)
    {
        DI_TRACE_ONCE(L"EnumDevicesBySemantics rejected flags %#lx", Flags);
        return E_INVALIDARG;
    }
    Result = DiValidateSemantics(Format, DIDBAM_PRESERVE);
    if (FAILED(Result))
        return Result;
    Result = DiCheckVersion(DiInstance, Version);
    if (FAILED(Result))
        return Result;
    return EnumBySemanticsCore(UserName, Format, Callback, Context, Flags);
}

struct DI_ANSI_SEMANTIC_ENUM
{
    LPDIENUMDEVICESBYSEMANTICSCBA Callback;
    VOID* Context;
};

static
BOOL
CALLBACK
DiAnsiSemanticThunk(
    _In_ LPCDIDEVICEINSTANCEW Wide,
    _In_ LPDIRECTINPUTDEVICE8W Device,
    _In_ DWORD Flags,
    _In_ DWORD Remaining,
    _In_opt_ VOID* Context)
{
    DI_ANSI_SEMANTIC_ENUM* Enum = static_cast<DI_ANSI_SEMANTIC_ENUM*>(Context);
    IDirectInputDevice8A* Ansi = nullptr;
    DIDEVICEINSTANCEA Instance;

    /* The device holds its ANSI face as long as itself, so no reference is kept here */
    Device->QueryInterface(IID_IDirectInputDevice8A, reinterpret_cast<VOID**>(&Ansi));
    if (Ansi != nullptr)
        Ansi->Release();
    ZeroMemory(&Instance, sizeof(Instance));
    Instance.dwSize = sizeof(Instance);
    DiInstanceToAnsi(&Instance, Wide);
    return Enum->Callback(&Instance, Ansi, Flags, Remaining, Enum->Context);
}

STDMETHODIMP
DiInput::AnsiFace::EnumDevicesBySemantics(
    _In_opt_z_ LPCSTR UserName,
    _In_ LPDIACTIONFORMATA Format,
    _In_ LPDIENUMDEVICESBYSEMANTICSCBA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    DI_ANSI_SEMANTIC_ENUM Enum;
    DIACTIONFORMATW Wide;
    WCHAR User[MAX_PATH];
    HRESULT Result;

    Result = DiCheckActionFormat(reinterpret_cast<const DIACTIONFORMATW*>(Format));
    if (FAILED(Result))
        return Result;
    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    Result = DiCheckActionNames(reinterpret_cast<const DIACTIONFORMATW*>(Format), TRUE);
    if (FAILED(Result))
        return Result;
    if (UserName != nullptr)
    {
        Result = DiCheckAnsiString(UserName, DI_MAP_USER_LIMIT);
        if (FAILED(Result))
            return Result;
    }
    if (Callback == nullptr || (Flags & ~DIEDBSFL_VALID) != 0)
        return E_INVALIDARG;

    /* The wide request always names a user, an empty one when none was given */
    User[0] = L'\0';
    if (UserName != nullptr)
        MultiByteToWideChar(CP_ACP, 0, UserName, -1, User, _countof(User));

    /* It works on its own copy of the actions, names still pointing at the ANSI strings */
    DiActionHeaderFromAnsi(&Wide, Format);
    Wide.rgoAction = static_cast<LPDIACTIONW>(malloc(Format->dwNumActions * sizeof(DIACTIONW)));
    if (Wide.rgoAction == nullptr)
        return E_OUTOFMEMORY;
    memcpy(Wide.rgoAction, Format->rgoAction, Format->dwNumActions * sizeof(DIACTIONW));
    Enum.Callback = Callback;
    Enum.Context = Context;
    Result = Owner->EnumDevicesBySemantics(User, &Wide, DiAnsiSemanticThunk, &Enum, Flags);
    free(Wide.rgoAction);
    return Result;
}

/* A configuration target must be a surface the configuration UI could draw on */
static
HRESULT
DiCheckConfigureTarget(
    _In_ IUnknown* Target)
{
    DiD3D8Surface* Surface3D = nullptr;
    IDirectDrawSurface7* Surface = nullptr;
    HRESULT Result;

    if (SUCCEEDED(Target->QueryInterface(DiIidDirect3DSurface8, reinterpret_cast<VOID**>(&Surface3D))))
    {
        DI_D3D8_SURFACE_DESC Desc = {};

        Result = Surface3D->GetDesc(&Desc);
        if (SUCCEEDED(Result) && (Desc.Width < 640 || Desc.Height < 480 || Desc.Format < 20 || Desc.Format > 25))
            Result = E_INVALIDARG;
        Surface3D->Release();
        return Result;
    }

    if (FAILED(Target->QueryInterface(IID_IDirectDrawSurface7, reinterpret_cast<VOID**>(&Surface))))
    {
        Result = Target->QueryInterface(IID_IDirectDrawSurface4, reinterpret_cast<VOID**>(&Surface));
        if (FAILED(Result))
            return Result;
    }
    {
        DDSURFACEDESC2 Desc = {};
        const DDPIXELFORMAT* Pixels = &Desc.ddpfPixelFormat;

        Desc.dwSize = sizeof(Desc);
        Result = Surface->GetSurfaceDesc(&Desc);
        if (SUCCEEDED(Result))
        {
            BOOLEAN Usable = Desc.dwWidth >= 640 && Desc.dwHeight >= 480 && (Pixels->dwFlags & DDPF_RGB);

            if (Usable && Pixels->dwRGBBitCount > 16)
            {
                Usable = Pixels->dwRBitMask == 0xFF0000 && Pixels->dwGBitMask == 0xFF00 && Pixels->dwBBitMask == 0xFF;
            }
            else if (Usable && Pixels->dwRGBBitCount == 16)
            {
                Usable = (Pixels->dwBBitMask == 0x1F && Pixels->dwGBitMask == 0x3E0 && Pixels->dwRBitMask == 0x7C00) ||
                         (Pixels->dwGBitMask == 0x7E0 && Pixels->dwRBitMask == 0xF800);
            }
            else
            {
                Usable = FALSE;
            }
            if (!Usable)
                Result = E_INVALIDARG;
        }
    }
    Surface->Release();
    return Result;
}

/* The checks the configuration UI's host makes before loading it */
static
HRESULT
DiConfigureCore(
    _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
    _In_opt_ IUnknown* Target,
    _In_ DWORD Flags)
{
    HRESULT Result;

    if (Flags & ~DICD_EDIT)
        return E_INVALIDARG;
    if (Callback == nullptr)
        return Target != nullptr ? E_INVALIDARG : DI_MAP_STORE_MISSING;
    if (Target == nullptr)
        return E_INVALIDARG;
    if (*reinterpret_cast<VOID***>(Target) == nullptr || (*reinterpret_cast<VOID***>(Target))[0] == nullptr ||
        (*reinterpret_cast<VOID***>(Target))[1] == nullptr || (*reinterpret_cast<VOID***>(Target))[2] == nullptr)
    {
        return E_INVALIDARG;
    }
    Result = DiCheckConfigureTarget(Target);
    if (FAILED(Result))
        return Result;

    /* The configuration UI is not installed, so loading it fails as a missing class does */
    DI_TRACE_ONCE(L"ConfigureDevices has no configuration UI to load");
    return DI_MAP_STORE_MISSING;
}

STDMETHODIMP
DiInput::ConfigureDevices(
    _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
    _In_ LPDICONFIGUREDEVICESPARAMSW Params,
    _In_ DWORD Flags,
    _In_opt_ VOID* Data)
{
    PCWSTR Name;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Data);
    if (Params == nullptr)
        return E_POINTER;
    if (Params->lptszUserNames != nullptr)
    {
        Result = DiCheckWideString(Params->lptszUserNames, Params->dwcUsers << 8);
        if (FAILED(Result))
            return Result;
    }
    if (Params->lprgFormats == nullptr && Params->dwcFormats * static_cast<DWORD>(sizeof(DIACTIONFORMATW)) != 0)
        return E_POINTER;
    if (Params->dwSize != sizeof(*Params))
        return E_INVALIDARG;
    for (DWORD Index = 0; Index < Params->dwcFormats; Index++)
    {
        const DIACTIONFORMATW* Format = &Params->lprgFormats[Index];

        Result = DiCheckActionFormat(Format);
        if (SUCCEEDED(Result) && Format->dwSize != sizeof(*Format))
            Result = E_INVALIDARG;
        if (SUCCEEDED(Result))
            Result = DiCheckActionNames(Format, FALSE);
        if (FAILED(Result))
            return Result;
    }

    /* The user names follow one another, each ending in its own terminator */
    Name = Params->lptszUserNames;
    for (DWORD Index = 0; Name != nullptr && Index < Params->dwcUsers; Index++)
    {
        Result = DiCheckWideString(Name, DI_ACTION_NAME_LIMIT);
        if (FAILED(Result))
            return Result;
        if (*Name == L'\0')
            return E_INVALIDARG;
        Name += wcslen(Name) + 1;
    }
    return DiConfigureCore(Callback, Params->lpUnkDDSTarget, Flags);
}

STDMETHODIMP
DiInput::AnsiFace::ConfigureDevices(
    _In_opt_ LPDICONFIGUREDEVICESCALLBACK Callback,
    _In_ LPDICONFIGUREDEVICESPARAMSA Params,
    _In_ DWORD Flags,
    _In_opt_ VOID* Data)
{
    PCSTR Name;
    HRESULT Result;

    UNREFERENCED_PARAMETER(Data);
    if (Params == nullptr)
        return E_POINTER;
    if (Params->lptszUserNames != nullptr)
    {
        Result = DiCheckAnsiString(Params->lptszUserNames, Params->dwcUsers << 8);
        if (FAILED(Result))
            return Result;
    }
    if (Params->lprgFormats == nullptr && Params->dwcFormats * static_cast<DWORD>(sizeof(DIACTIONFORMATA)) != 0)
        return E_POINTER;
    if (Params->dwSize != sizeof(*Params))
        return E_INVALIDARG;
    for (DWORD Index = 0; Index < Params->dwcFormats; Index++)
    {
        const DIACTIONFORMATA* Format = &Params->lprgFormats[Index];

        Result = DiCheckActionFormat(reinterpret_cast<const DIACTIONFORMATW*>(Format));
        if (SUCCEEDED(Result) && Format->dwSize != sizeof(*Format))
            Result = E_INVALIDARG;
        if (SUCCEEDED(Result))
            Result = DiCheckActionNames(reinterpret_cast<const DIACTIONFORMATW*>(Format), TRUE);
        if (FAILED(Result))
            return Result;
    }
    Name = Params->lptszUserNames;
    for (DWORD Index = 0; Name != nullptr && Index < Params->dwcUsers; Index++)
    {
        Result = DiCheckAnsiString(Name, DI_ACTION_NAME_LIMIT);
        if (FAILED(Result))
            return Result;
        if (*Name == '\0')
            return E_INVALIDARG;
        Name += strlen(Name) + 1;
    }
    return DiConfigureCore(Callback, Params->lpUnkDDSTarget, Flags);
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
