/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectInputJoyConfig and the joystick registry
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "hid.hpp"

#include <strsafe.h>

#define DI_JOY_OEM_ROOT     L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM"
#define DI_JOY_CONFIG_ROOT  L"System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\DINPUT.DLL"
#define DI_JOY_PROP_ROOT    L"System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\DirectInput"
#define DI_JOY_KEY_ACCESS   0x0003003F
#define DI_JOY_COOP_LEVEL   (DISCL_EXCLUSIVE | DISCL_BACKGROUND)
#define DI_JOY_HWS_ISHIDDEV 0x10000000
#define DI_JOY_LIMIT        16

/* The first predefined types, named "#2" through "#12" */
#define DI_JOY_PREDEF_FIRST 2
#define DI_JOY_PREDEF_LAST  12

static const GUID DiJoyConfigOwner = IID_IDirectInputJoyConfig;

/* DirectInput 8 hands out its own revision of the interface, with the application status key last */
#if DIRECTINPUT_VERSION >= 0x0800
typedef IDirectInputJoyConfig8 RX_DI_JOY_CONFIG_FACE;
#define DI_JOY_CONFIG_IID   IID_IDirectInputJoyConfig8
#else
typedef IDirectInputJoyConfig RX_DI_JOY_CONFIG_FACE;
#define DI_JOY_CONFIG_IID   IID_IDirectInputJoyConfig
#endif

HRESULT
DiOpenJoyTypeKey(
    _In_z_ PCWSTR Type,
    _In_ REGSAM Access,
    _Out_ HKEY* Key)
{
    HKEY Root;
    HRESULT hr;

    *Key = nullptr;
    hr = DiOpenKey(HKEY_CURRENT_USER, DI_JOY_OEM_ROOT, Access, 0, &Root);
    if (FAILED(hr))
        return hr;

    hr = DiOpenKey(Root, Type, Access, 0, Key);
    RegCloseKey(Root);
    if (FAILED(hr))
        *Key = nullptr;
    return hr;
}

class RxDiJoyConfig final : public RxDiUnknown, public RX_DI_JOY_CONFIG_FACE
{
public:
    explicit
    RxDiJoyConfig(
        _In_ IUnknown* OuterUnknown)
        : RxDiUnknown(OuterUnknown)
    {
        InitializeCriticalSection(&Lock);
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override
    {
        if (&Iid == nullptr || Object == nullptr)
            return E_POINTER;
        *Object = nullptr;
        return Outer->QueryInterface(Iid, Object);
    }

    ULONG STDMETHODCALLTYPE AddRef(VOID) override { return Outer->AddRef(); }
    ULONG STDMETHODCALLTYPE Release(VOID) override { return Outer->Release(); }

    HRESULT
    STDMETHODCALLTYPE
    Acquire(VOID) override
    {
        HRESULT hr;

        EnterCriticalSection(&Lock);
        if (CoopFlags == 0)
        {
            hr = E_FAIL;
        }
        else if (Acquired)
        {
            hr = S_FALSE;
        }
        else
        {
            hr = DiAcquireExclusive(&DiJoyConfigOwner, Window, CoopFlags);
            if (SUCCEEDED(hr))
            {
                hr = DiOpenKey(HKEY_CURRENT_USER, DI_JOY_OEM_ROOT, DI_JOY_KEY_ACCESS, 0, &TypeRoot);
                if (FAILED(hr))
                {
                    TypeRoot = nullptr;
                    hr = DIERR_INSUFFICIENTPRIVS;
                }
                else
                {
                    Acquired = TRUE;
                    DiTrace(L"joystick configuration acquired");
                }
            }
        }
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Unacquire(VOID) override
    {
        HRESULT hr = S_FALSE;

        EnterCriticalSection(&Lock);
        if (Acquired)
        {
            RegCloseKey(TypeRoot);
            TypeRoot = nullptr;
            DiReleaseExclusive(&DiJoyConfigOwner, Window, CoopFlags);
            Acquired = FALSE;
            hr = S_OK;
        }
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    SetCooperativeLevel(
        HWND NewWindow,
        DWORD Flags) override
    {
        /* Only exclusive background access is offered, from a window of this process */
        if (Flags != DI_JOY_COOP_LEVEL)
            return E_NOTIMPL;
        if (!DiIsWindowOurs(NewWindow))
            return E_HANDLE;

        Window = NewWindow;
        CoopFlags = Flags;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    SendNotify(VOID) override
    {
        HRESULT hr = DIERR_NOTACQUIRED;

        EnterCriticalSection(&Lock);
        if (Acquired)
        {
            PostMessageW(HWND_BROADCAST, RegisterWindowMessageW(L"MSJSTICK_VJOYD_MSGSTR"), 0, 0);
            hr = S_OK;
        }
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    EnumTypes(
        LPDIJOYTYPECALLBACK Callback,
        LPVOID Context) override
    {
        WCHAR Name[MAX_PATH];
        HKEY Root;

        if (Callback == nullptr)
            return E_INVALIDARG;

        for (ULONG Index = DI_JOY_PREDEF_FIRST; Index <= DI_JOY_PREDEF_LAST; Index++)
        {
            StringCchPrintfW(Name, _countof(Name), L"#%c", static_cast<WCHAR>(L'0' + Index));
            if (!Callback(Name, Context))
                return S_OK;
        }

        if (RegOpenKeyExW(HKEY_CURRENT_USER, DI_JOY_OEM_ROOT, 0, KEY_READ, &Root) == ERROR_SUCCESS)
        {
            for (DWORD Index = 0; RegEnumKeyW(Root, Index, Name, _countof(Name)) == ERROR_SUCCESS; Index++)
            {
                if (IsListedType(Root, Name) && !Callback(Name, Context))
                    break;
            }
            RegCloseKey(Root);
        }
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetTypeInfo(
        LPCWSTR Type,
        LPDIJOYTYPEINFO Info,
        DWORD Flags) override
    {
        HRESULT hr;

        hr = CheckTypeArgs(Type, Info, Flags);
        if (FAILED(hr))
            return hr;

        if (Type[0] == L'#')
            return GetPredefinedType(Type, Info, Flags);
#if DIRECTINPUT_VERSION >= 0x0800
        return ReadTypeInfo(Type, Info, Flags);
#else
        HKEY Key;

        if (Type[0] == L'\0')
            return DIERR_NOTFOUND;

        hr = DiOpenKey(HKEY_LOCAL_MACHINE, DI_JOY_OEM_ROOT, KEY_READ, 0, &Key);
        if (SUCCEEDED(hr))
        {
            HKEY TypeKey;

            if (RegOpenKeyExW(Key, Type, 0, KEY_READ, &TypeKey) == ERROR_SUCCESS)
            {
                if (Flags & DITC_DISPLAYNAME)
                {
                    DWORD Size = sizeof(Info->wszDisplayName);

                    if (RegQueryValueExW(TypeKey, L"OEMName", nullptr, nullptr, reinterpret_cast<PBYTE>(Info->wszDisplayName), &Size) != ERROR_SUCCESS)
                        Info->wszDisplayName[0] = L'\0';
                }
                if (Flags & DITC_REGHWSETTINGS)
                    DiReadValue(TypeKey, L"OEMData", &Info->hws, sizeof(Info->hws));
                RegCloseKey(TypeKey);
            }
            else
            {
                hr = DIERR_NOTFOUND;
            }
            RegCloseKey(Key);
        }
        return hr;
#endif
    }

    HRESULT
    STDMETHODCALLTYPE
    SetTypeInfo(
        LPCWSTR Type,
        LPCDIJOYTYPEINFO Info,
#if DIRECTINPUT_VERSION >= 0x0800
        DWORD Flags,
        LPWSTR NewName) override
#else
        DWORD Flags) override
#endif
    {
        HKEY TypeKey;
        HRESULT hr;

#if DIRECTINPUT_VERSION >= 0x0800
        UNREFERENCED_PARAMETER(NewName);

        /* Only the top bit of the first flags may be set by a caller */
        if (DiCheckString(Type, 256) == S_OK && Info != nullptr && Info->dwSize >= sizeof(DIJOYTYPEINFO_DX6) &&
            (Info->dwSize == sizeof(DIJOYTYPEINFO_DX6) || Info->dwSize == sizeof(DIJOYTYPEINFO)) &&
            (Info->dwFlags1 & 0x7FFFFFFF) != 0)
        {
            return E_INVALIDARG;
        }
#endif
        hr = CheckTypeArgs(Type, const_cast<LPDIJOYTYPEINFO>(Info), Flags);
        if (FAILED(hr))
            return hr;

        EnterCriticalSection(&Lock);
        if (!Acquired)
            hr = DIERR_NOTACQUIRED;
        else if (Type[0] == L'\0')
            hr = E_INVALIDARG;
        else if (Type[0] == L'#')
            hr = E_ACCESSDENIED;
        else if (RegCreateKeyExW(TypeRoot, Type, 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &TypeKey, nullptr) != ERROR_SUCCESS)
            hr = DIERR_INSUFFICIENTPRIVS;
        else
        {
            if (Flags & DITC_DISPLAYNAME)
                RegSetValueExW(TypeKey, L"OEMName", 0, REG_SZ, reinterpret_cast<const BYTE*>(Info->wszDisplayName),
                               (lstrlenW(Info->wszDisplayName) + 1) * sizeof(WCHAR));
            if (Flags & DITC_REGHWSETTINGS)
                RegSetValueExW(TypeKey, L"OEMData", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&Info->hws), sizeof(Info->hws));
            RegCloseKey(TypeKey);
            hr = S_OK;
        }
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    DeleteType(
        LPCWSTR Type) override
    {
        HRESULT hr = DiCheckString(Type, 256);

        if (FAILED(hr))
            return hr;

        EnterCriticalSection(&Lock);
        if (!Acquired)
            hr = DIERR_NOTACQUIRED;
        else if (Type[0] == L'#' || Type[0] == L'\0')
            hr = E_INVALIDARG;
        else
            hr = HRESULT_FROM_WIN32(RegDeleteTreeW(TypeRoot, Type));
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetConfig(
        UINT Id,
        LPDIJOYCONFIG Config,
        DWORD Flags) override
    {
        if (Config == nullptr)
            return E_POINTER;
        if (Config->dwSize != sizeof(DIJOYCONFIG_DX5) && Config->dwSize != sizeof(DIJOYCONFIG))
            return E_INVALIDARG;
        if (Flags & ~((Config->dwSize == sizeof(DIJOYCONFIG)) ? 0x1FUL : 0x0FUL))
            return E_INVALIDARG;
#if DIRECTINPUT_VERSION >= 0x0800
        /* DirectInput 8 reads the saved configuration, and none is kept for any id */
        UNREFERENCED_PARAMETER(Id);
        return DIERR_NOTFOUND;
#else
        if (Id >= DI_JOY_LIMIT)
            return DIERR_NOMOREITEMS;

        /* No joystick is configured through the old gameport settings */
        return E_FAIL;
#endif
    }

    HRESULT
    STDMETHODCALLTYPE
    SetConfig(
        UINT Id,
        LPCDIJOYCONFIG Config,
        DWORD Flags) override
    {
        HRESULT hr;

        if (Config == nullptr)
            return E_POINTER;
        if (Config->dwSize != sizeof(DIJOYCONFIG_DX5) && Config->dwSize != sizeof(DIJOYCONFIG))
            return E_INVALIDARG;

        EnterCriticalSection(&Lock);
        hr = Acquired ? E_NOTIMPL : DIERR_NOTACQUIRED;
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    DeleteConfig(
        UINT Id) override
    {
        HRESULT hr;

        EnterCriticalSection(&Lock);
        hr = Acquired ? E_NOTIMPL : DIERR_NOTACQUIRED;
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetUserValues(
        LPDIJOYUSERVALUES Values,
        DWORD Flags) override
    {
        HKEY Key;

        if (Values == nullptr)
            return E_POINTER;
        if (Values->dwSize != sizeof(DIJOYUSERVALUES))
            return E_INVALIDARG;
        if (Flags & ~0x7UL)
            return E_INVALIDARG;

        if (RegOpenKeyExW(HKEY_CURRENT_USER, DI_JOY_CONFIG_ROOT L"\\CurrentJoystickSettings", 0, KEY_READ, &Key) == ERROR_SUCCESS)
        {
            if (Flags & DIJU_USERVALUES)
                DiReadValue(Key, L"JoystickUserValues", &Values->ruv, sizeof(Values->ruv));
            RegCloseKey(Key);
        }
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    SetUserValues(
        LPCDIJOYUSERVALUES Values,
        DWORD Flags) override
    {
        HRESULT hr;

        if (Values == nullptr)
            return E_POINTER;
        if (Values->dwSize != sizeof(DIJOYUSERVALUES) || (Flags & ~0x7UL) != 0)
            return E_INVALIDARG;

        EnterCriticalSection(&Lock);
        hr = Acquired ? E_NOTIMPL : DIERR_NOTACQUIRED;
        LeaveCriticalSection(&Lock);
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    AddNewHardware(
        HWND Owner,
        REFGUID Class) override
    {
        if (Owner != nullptr && !IsWindow(Owner))
            return E_HANDLE;
        return DIERR_UNSUPPORTED;
    }

    HRESULT
    STDMETHODCALLTYPE
    OpenTypeKey(
        LPCWSTR Type,
        DWORD Access,
        PHKEY Key) override
    {
        HRESULT hr = DiCheckString(Type, 256);

        if (FAILED(hr))
            return hr;
        if (Key == nullptr)
            return E_POINTER;

        *Key = nullptr;
        if (Type[0] == L'#')
            return E_INVALIDARG;
        if ((Access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | MAXIMUM_ALLOWED)) != 0 && !Acquired)
            return DIERR_NOTACQUIRED;

        return DiOpenJoyTypeKey(Type, Access, Key);
    }

#if DIRECTINPUT_VERSION >= 0x0800
    HRESULT
    STDMETHODCALLTYPE
    OpenAppStatusKey(
        PHKEY Key) override
    {
        if (Key == nullptr)
            return E_POINTER;
        return DiOpenKey(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectInput", KEY_READ, 0, Key);
    }
#else
    HRESULT
    STDMETHODCALLTYPE
    OpenConfigKey(
        UINT Id,
        DWORD Access,
        PHKEY Key) override
    {
        if (Key == nullptr)
            return E_POINTER;
        *Key = nullptr;
        if ((Access & (KEY_SET_VALUE | KEY_CREATE_SUB_KEY | MAXIMUM_ALLOWED)) != 0 && !Acquired)
            return DIERR_NOTACQUIRED;
        return E_INVALIDARG;
    }
#endif

private:
    ~RxDiJoyConfig() override
    {
        if (Acquired)
            Unacquire();
        DeleteCriticalSection(&Lock);
    }

    PVOID PrimaryInterface(VOID) override { return static_cast<RX_DI_JOY_CONFIG_FACE*>(this); }

    PVOID
    QueryOwn(
        _In_ REFIID Iid) override
    {
        if (IsEqualIID(Iid, DI_JOY_CONFIG_IID))
            return static_cast<RX_DI_JOY_CONFIG_FACE*>(this);
        return nullptr;
    }

    /* Built in gameport types "#2" to "#<" */
    static
    HRESULT
    GetPredefinedType(
        _In_z_ LPCWSTR Type,
        _Inout_ LPDIJOYTYPEINFO Info,
        _In_ DWORD Flags)
    {
        static const JOYREGHWSETTINGS Settings[] =
        {
            { 0x00000, 2 }, { 0x00000, 4 }, { 0x00020, 2 }, { 0x00010, 2 }, { 0x00011, 2 }, { 0x00001, 2 },
            { 0x00001, 4 }, { 0x00020, 4 }, { 0x00010, 4 }, { 0x00011, 4 }, { 0x80000, 2 },
        };
        ULONG Index = static_cast<ULONG>(Type[1]) - L'2';

        if (Index >= _countof(Settings) || Type[2] != L'\0')
            return DIERR_NOTFOUND;

        Info->hws = Settings[Index];
        LoadStringW(DiModule, 976 + Type[1], Info->wszDisplayName, _countof(Info->wszDisplayName));
        Info->wszCallout[0] = L'\0';
        ZeroMemory(&Info->clsidConfig, sizeof(Info->clsidConfig));
        if (Info->dwSize >= sizeof(DIJOYTYPEINFO_DX6))
        {
#if DIRECTINPUT_VERSION >= 0x0800
            Info->dwFlags1 = 0;
#endif
            if (Flags & DITC_HARDWAREID)
                StringCchPrintfW(Info->wszHardwareId, _countof(Info->wszHardwareId), L"GAMEPORT\\VID_045E&PID_%04X", 0x0102 + Index);
        }
#if DIRECTINPUT_VERSION >= 0x0800
        if (Info->dwSize >= sizeof(DIJOYTYPEINFO))
        {
            Info->dwFlags2 = 0;
            Info->wszMapFile[0] = L'\0';
        }
#endif
        return S_OK;
    }

#if DIRECTINPUT_VERSION >= 0x0800
    /* Reads a string value into a buffer of Count characters, the way the type reader expects */
    static
    LONG
    ReadTypeString(
        _In_ HKEY Key,
        _In_z_ PCWSTR Name,
        _Out_writes_(Count) PWSTR Buffer,
        _In_ ULONG Count)
    {
        LONG Error;

        Buffer[Count - 1] = L'\0';
        Error = DiReadString(Key, Name, Buffer, Count - 1);
        if (Error == ERROR_MORE_DATA)
            Buffer[Count - 2] = L'\0';
        else if (Error != ERROR_SUCCESS)
            Buffer[0] = L'\0';
        return Error;
    }

    /* The DirectInput 8 reader of a named type, which takes what it can and says when parts were missing */
    static
    HRESULT
    ReadTypeInfo(
        _In_z_ LPCWSTR Type,
        _Inout_ LPDIJOYTYPEINFO Info,
        _In_ DWORD Flags)
    {
        HRESULT Result = S_FALSE;
        BOOLEAN Partial = FALSE;
        HKEY Root;
        HKEY Key;

        ZeroMemory(&Info->clsidConfig, sizeof(Info->clsidConfig));
        if (Flags & (DITC_FLAGS2 | DITC_MAPFILE))
        {
            Key = nullptr;
            if (SUCCEEDED(DiOpenKey(HKEY_CURRENT_USER, DI_JOY_PROP_ROOT, KEY_QUERY_VALUE, 0, &Root)))
            {
                if (FAILED(DiOpenKey(Root, Type, KEY_QUERY_VALUE, 0, &Key)))
                    Key = nullptr;
                RegCloseKey(Root);
            }
            if (Key == nullptr)
            {
                Info->dwFlags2 = 0;
                ZeroMemory(Info->wszMapFile, sizeof(Info->wszMapFile));
                Partial = TRUE;
            }
            else
            {
                if (Flags & DITC_FLAGS2)
                {
                    HRESULT Read = DiReadValue(Key, L"Flags2", &Info->dwFlags2, sizeof(Info->dwFlags2));

                    Info->dwFlags2 &= 0x01FFFFFF;
                    if (FAILED(Read))
                    {
                        Info->dwFlags2 = 0;
                        Partial = TRUE;
                    }
                }
                if ((Flags & DITC_MAPFILE) && ReadTypeString(Key, L"OEMMapFile", Info->wszMapFile, MAX_JOYSTRING) != ERROR_SUCCESS)
                {
                    ZeroMemory(Info->wszMapFile, sizeof(Info->wszMapFile));
                    Partial = TRUE;
                }
                RegCloseKey(Key);
            }
            Result = S_OK;
        }
        if ((Flags & 0x3F) == 0)
            return (SUCCEEDED(Result) && Partial) ? S_FALSE : Result;

        if (FAILED(DiOpenKey(HKEY_CURRENT_USER, DI_JOY_OEM_ROOT, KEY_QUERY_VALUE, 0, &Root)))
            return DIERR_NOTFOUND;
        Result = DiOpenKey(Root, Type, KEY_QUERY_VALUE, 0, &Key);
        RegCloseKey(Root);
        if (FAILED(Result))
            return DIERR_NOTFOUND;

        Result = S_OK;
        if (Flags & DITC_REGHWSETTINGS)
            Result = DiReadValue(Key, L"OEMData", &Info->hws, sizeof(Info->hws));
        if (SUCCEEDED(Result))
        {
            if (Flags & DITC_CLSIDCONFIG)
            {
                WCHAR Text[39];

                if (ReadTypeString(Key, L"ConfigCLSID", Text, _countof(Text)) != ERROR_SUCCESS ||
                    !DiParseGuid(Text, &Info->clsidConfig))
                {
                    ZeroMemory(&Info->clsidConfig, sizeof(Info->clsidConfig));
                }
            }
            if (Flags & DITC_DISPLAYNAME)
            {
                LONG Error = ReadTypeString(Key, L"OEMName", Info->wszDisplayName, MAX_JOYSTRING);

                if (Error != ERROR_SUCCESS && Error != ERROR_FILE_NOT_FOUND)
                {
                    RegCloseKey(Key);
                    return HRESULT_FROM_WIN32(Error);
                }
            }
            if ((Flags & DITC_HARDWAREID) && ReadTypeString(Key, L"OEMHardwareID", Info->wszHardwareId, MAX_JOYSTRING) != ERROR_SUCCESS)
            {
                ZeroMemory(Info->wszHardwareId, sizeof(Info->wszHardwareId));
                Partial = TRUE;
            }
            if (Flags & DITC_FLAGS1)
            {
                if (FAILED(DiReadValue(Key, L"Flags1", &Info->dwFlags1, sizeof(Info->dwFlags1))))
                {
                    Info->dwFlags1 = 0;
                    Partial = TRUE;
                }
                Info->dwFlags1 &= 0x8000000F;
            }
            Result = S_OK;
        }
        RegCloseKey(Key);
        return (SUCCEEDED(Result) && Partial) ? S_FALSE : Result;
    }
#endif

    /* A type is offered once it has a name and either a HID id or the HID flag */
    static
    BOOLEAN
    IsListedType(
        _In_ HKEY Root,
        _In_z_ PCWSTR Name)
    {
        JOYREGHWSETTINGS Settings = {};
        DWORD Size = sizeof(Settings);
        USHORT Vendor;
        USHORT Product;
        BOOLEAN Listed = FALSE;
        HKEY Key;

        if (RegOpenKeyExW(Root, Name, 0, KEY_READ, &Key) != ERROR_SUCCESS)
            return FALSE;

        if (RegQueryValueExW(Key, L"OEMName", nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS)
        {
            RegQueryValueExW(Key, L"OEMData", nullptr, nullptr, reinterpret_cast<PBYTE>(&Settings), &Size);
            Listed = (Settings.dwFlags & DI_JOY_HWS_ISHIDDEV) != 0 || DiHidParseIds(Name, &Vendor, &Product);
        }
        RegCloseKey(Key);
        return Listed;
    }

    static
    HRESULT
    CheckTypeArgs(
        _In_opt_z_ LPCWSTR Type,
        _In_opt_ LPDIJOYTYPEINFO Info,
        _In_ DWORD Flags)
    {
        HRESULT hr = DiCheckString(Type, 256);

        if (FAILED(hr))
            return hr;
        if (Info == nullptr)
            return E_POINTER;
#if DIRECTINPUT_VERSION >= 0x0800
        if (Info->dwSize != sizeof(DIJOYTYPEINFO_DX5) && Info->dwSize != sizeof(DIJOYTYPEINFO_DX6) &&
            Info->dwSize != sizeof(DIJOYTYPEINFO))
        {
            return E_INVALIDARG;
        }
        if (Info->dwSize == sizeof(DIJOYTYPEINFO))
            return (Flags & ~0xFFUL) ? E_INVALIDARG : S_OK;
        if (Info->dwSize == sizeof(DIJOYTYPEINFO_DX6))
            return (Flags & ~0x3FUL) ? E_INVALIDARG : S_OK;
        return (Flags & ~0x0FUL) ? E_INVALIDARG : S_OK;
#else
        if (Info->dwSize != sizeof(DIJOYTYPEINFO_DX5) && Info->dwSize != sizeof(DIJOYTYPEINFO))
            return E_INVALIDARG;
        if (Flags & ~((Info->dwSize == sizeof(DIJOYTYPEINFO)) ? 0xFFUL : 0x0FUL))
            return E_INVALIDARG;
        return S_OK;
#endif
    }

    CRITICAL_SECTION Lock;
    HWND Window = nullptr;
    DWORD CoopFlags = 0;
    BOOL Acquired = FALSE;
    HKEY TypeRoot = nullptr;
};

HRESULT
DiCreateJoyConfig(
    _In_ IUnknown* OuterUnknown,
    _Outptr_ IUnknown** Inner)
{
    RxDiJoyConfig* Created = new RxDiJoyConfig(OuterUnknown);

    *Inner = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    *Inner = Created->InnerUnknown();
    return S_OK;
}
