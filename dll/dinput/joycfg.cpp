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
#define DI_JOY_KEY_ACCESS   0x0003003F
#define DI_JOY_COOP_LEVEL   (DISCL_EXCLUSIVE | DISCL_BACKGROUND)
#define DI_JOY_HWS_ISHIDDEV 0x10000000
#define DI_JOY_LIMIT        16

/* The first predefined types, named "#2" through "#12" */
#define DI_JOY_PREDEF_FIRST 2
#define DI_JOY_PREDEF_LAST  12

static const GUID DiJoyConfigOwner = IID_IDirectInputJoyConfig;

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

class RxDiJoyConfig final : public RxDiUnknown, public IDirectInputJoyConfig
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
        HKEY Key;
        HRESULT hr;

        hr = CheckTypeArgs(Type, Info, Flags);
        if (FAILED(hr))
            return hr;

        if (Type[0] == L'#')
            return GetPredefinedType(Type, Info, Flags);
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
    }

    HRESULT
    STDMETHODCALLTYPE
    SetTypeInfo(
        LPCWSTR Type,
        LPCDIJOYTYPEINFO Info,
        DWORD Flags) override
    {
        HKEY TypeKey;
        HRESULT hr;

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
        if (Id >= DI_JOY_LIMIT)
            return DIERR_NOMOREITEMS;

        /* No joystick is configured through the old gameport settings */
        return E_FAIL;
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

private:
    ~RxDiJoyConfig() override
    {
        if (Acquired)
            Unacquire();
        DeleteCriticalSection(&Lock);
    }

    PVOID PrimaryInterface(VOID) override { return static_cast<IDirectInputJoyConfig*>(this); }

    PVOID
    QueryOwn(
        _In_ REFIID Iid) override
    {
        if (IsEqualIID(Iid, IID_IDirectInputJoyConfig))
            return static_cast<IDirectInputJoyConfig*>(this);
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
        if (Info->dwSize >= sizeof(DIJOYTYPEINFO))
        {
            if (Flags & DITC_HARDWAREID)
                StringCchPrintfW(Info->wszHardwareId, _countof(Info->wszHardwareId), L"GAMEPORT\\VID_045E&PID_%04X", 0x0102 + Index);
        }
        return S_OK;
    }

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
        if (Info->dwSize != sizeof(DIJOYTYPEINFO_DX5) && Info->dwSize != sizeof(DIJOYTYPEINFO))
            return E_INVALIDARG;
        if (Flags & ~((Info->dwSize == sizeof(DIJOYTYPEINFO)) ? 0xFFUL : 0x0FUL))
            return E_INVALIDARG;
        return S_OK;
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
