/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback driver loading and the effect shepherd
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#include <strsafe.h>

#define DI_FULL_GAIN            10000
#define DI_GUID_CHARS           39
#define DI_JOY_SETTINGS_KEY     L"System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\DINPUT.DLL\\CurrentJoystickSettings"

/* Download flags that mean the driver keeps the effect where it is */
#define DI_DOWNLOAD_KEEP        0x80000000

/* The HID PID driver serves every device that describes its own effects */
static const GUID DiPidDriver = { 0xEEC6993A, 0xB3FD, 0x11D2, { 0xA9, 0x16, 0x00, 0xC0, 0x4F, 0xB9, 0x86, 0x38 } };

DWORD
DiReadJoystickGain(
    _In_ ULONG Id)
{
    WCHAR Name[40];
    DWORD Gain = 0;
    HKEY Key;

    if (RegOpenKeyExW(HKEY_CURRENT_USER, DI_JOY_SETTINGS_KEY, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return DI_FULL_GAIN;

    StringCchPrintfW(Name, _countof(Name), L"Joystick%uFFConfiguration", Id + 1);
    if (FAILED(DiReadValue(Key, Name, &Gain, sizeof(Gain))) || (Gain & 0xFFFF0000) != 0)
        Gain = DI_FULL_GAIN;
    RegCloseKey(Key);
    return Gain;
}

/* Loads an in process server the way COM would, without needing COM initialized */
static
HRESULT
DiLoadEffectDriver(
    _In_z_ PCWSTR Clsid,
    _Outptr_ IDirectInputEffectDriver** Driver,
    _Out_ HMODULE* Library)
{
    typedef HRESULT (STDAPICALLTYPE* PDI_GET_CLASS)(REFCLSID, REFIID, LPVOID*);
    WCHAR Path[80];
    WCHAR Server[MAX_PATH];
    WCHAR Threading[20];
    IClassFactory* Factory = nullptr;
    PDI_GET_CLASS GetClass;
    LONG Size = sizeof(Server);
    HMODULE Module;
    GUID Class;
    HKEY Key;
    HRESULT hr;

    *Driver = nullptr;
    *Library = nullptr;
    if (!DiParseGuid(Clsid, &Class))
        return REGDB_E_CLASSNOTREG;

    StringCbPrintfW(Path, sizeof(Path), L"CLSID\\%s\\InProcServer32", Clsid);
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, Path, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return REGDB_E_CLASSNOTREG;

    /* Effects are driven from any thread, so apartment only servers are refused */
    if (RegQueryValueW(Key, nullptr, Server, &Size) != ERROR_SUCCESS ||
        DiReadString(Key, L"ThreadingModel", Threading, _countof(Threading)) != ERROR_SUCCESS ||
        (lstrcmpiW(Threading, L"Both") != 0 && lstrcmpiW(Threading, L"Free") != 0))
    {
        RegCloseKey(Key);
        DiTrace(L"effect driver %ls is not registered for free threading", Clsid);
        return REGDB_E_CLASSNOTREG;
    }
    RegCloseKey(Key);

    Module = LoadLibraryW(Server);
    if (Module == nullptr)
    {
        DiTrace(L"effect driver %ls cannot be loaded, error %lu", Server, GetLastError());
        return REGDB_E_CLASSNOTREG;
    }

    GetClass = reinterpret_cast<PDI_GET_CLASS>(GetProcAddress(Module, "DllGetClassObject"));
    hr = (GetClass != nullptr) ? GetClass(Class, IID_IClassFactory, reinterpret_cast<LPVOID*>(&Factory)) : REGDB_E_CLASSNOTREG;
    if (SUCCEEDED(hr))
    {
        hr = Factory->CreateInstance(nullptr, IID_IDirectInputEffectDriver, reinterpret_cast<LPVOID*>(Driver));
        Factory->Release();
        if (FAILED(hr))
            *Driver = nullptr;
    }

    if (FAILED(hr))
    {
        FreeLibrary(Module);
        return hr;
    }

    *Library = Module;
    return hr;
}

class RxDiEffectShepherd final : public RxDiShepherd
{
public:
    ~RxDiEffectShepherd() override
    {
        if (Driver != nullptr)
            Driver->Release();
        if (Library != nullptr)
            FreeLibrary(Library);
    }

    HRESULT
    Load(
        _In_opt_ HKEY Config)
    {
        WCHAR Clsid[DI_GUID_CHARS];
        HRESULT hr;

        if (Config == nullptr)
        {
            StringCchPrintfW(Clsid, _countof(Clsid), L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                             DiPidDriver.Data1, DiPidDriver.Data2, DiPidDriver.Data3, DiPidDriver.Data4[0],
                             DiPidDriver.Data4[1], DiPidDriver.Data4[2], DiPidDriver.Data4[3], DiPidDriver.Data4[4],
                             DiPidDriver.Data4[5], DiPidDriver.Data4[6], DiPidDriver.Data4[7]);
        }
        else if (DiReadString(Config, L"CLSID", Clsid, _countof(Clsid)) != ERROR_SUCCESS)
        {
            return E_NOTIMPL;
        }

        hr = DiLoadEffectDriver(Clsid, &Driver, &Library);
        return FAILED(hr) ? E_NOTIMPL : hr;
    }

    HRESULT
    DeviceID(
        _In_ DWORD ExternalId,
        _In_ DWORD Begin,
        _In_ PVOID Info) override
    {
        RX_DI_JOY_SLOT* Slot;

        if (ExternalId >= DI_JOY_SLOTS)
            return E_FAIL;

        /* The first process to use a joystick sets up its shared gains */
        DiLockJoysticks();
        Slot = DiJoystickSlot(ExternalId);
        if (Slot->Tag == 0)
        {
            Slot->Tag = 1;
            Slot->GlobalGain = DiReadJoystickGain(ExternalId);
            Slot->DeviceGain = DI_FULL_GAIN;
        }
        DiUnlockJoysticks();

        Joystick = ExternalId;
        return Driver->DeviceID(DIRECTINPUT_VERSION, ExternalId, Begin, ExternalId, Info);
    }

    HRESULT
    GetVersions(
        _Inout_ LPDIDRIVERVERSIONS Versions) override
    {
        return Driver->GetVersions(Versions);
    }

    HRESULT
    Escape(
        _In_ RX_DI_FF_HANDLE* Handle,
        _Inout_ LPDIEFFESCAPE Request) override
    {
        HRESULT hr = LockEffect(Handle);

        if (FAILED(hr))
            return hr;
        hr = Driver->Escape(Joystick, Handle->Id, Request);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    DeviceEscape(
        _In_ RX_DI_FF_HANDLE* Handle,
        _Inout_ LPDIEFFESCAPE Request) override
    {
        HRESULT hr = LockDevice(Handle, FALSE);

        if (FAILED(hr))
            return hr;
        hr = Driver->Escape(Joystick, 0, Request);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    SetGain(
        _In_ RX_DI_FF_HANDLE* Handle,
        _In_ DWORD Gain) override
    {
        HRESULT hr = LockDevice(Handle, FALSE);

        if (FAILED(hr))
            return hr;
        Slot()->DeviceGain = Gain;
        hr = ApplyGain();
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    SetForceFeedbackState(
        _In_ RX_DI_FF_HANDLE* Handle,
        _In_ DWORD Command) override
    {
        BOOLEAN Acquiring = (Command & DI_FF_ACQUIRE) != 0;
        HRESULT hr = LockDevice(Handle, Acquiring);

        if (FAILED(hr))
            return hr;

        /* Taking the device over resets it and hands out a new tag */
        if (Acquiring)
            Command = (Command & ~(DI_FF_ACQUIRE | DISFFC_RESET)) | DISFFC_RESET;

        hr = Driver->SendForceFeedbackCommand(Joystick, Command);
        if (SUCCEEDED(hr) && (Command & DISFFC_RESET))
            Handle->Tag = ++Slot()->Tag;
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    GetForceFeedbackState(
        _In_ RX_DI_FF_HANDLE* Handle,
        _Inout_ LPDIDEVICESTATE State) override
    {
        HRESULT hr = LockDevice(Handle, FALSE);

        if (FAILED(hr))
            return hr;
        hr = Driver->GetForceFeedbackState(Joystick, State);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    DownloadEffect(
        _Inout_ RX_DI_FF_HANDLE* Handle,
        _In_ DWORD EffectId,
        _In_ LPCDIEFFECT Params,
        _In_ DWORD Flags) override
    {
        HRESULT hr;

        DiLockJoysticks();
        if (Slot()->Tag == Handle->Tag)
        {
            /* A new effect needs every parameter */
            if ((Flags & DI_DOWNLOAD_KEEP) == 0 && Handle->Id == 0)
                Flags |= DIEP_ALLPARAMS;
        }
        else
        {
            Handle->Id = 0;
            if ((Flags & DI_DOWNLOAD_KEEP) == 0)
            {
                DiUnlockJoysticks();
                return DIERR_NOTEXCLUSIVEACQUIRED;
            }
        }

        hr = (Flags != 0) ? Driver->DownloadEffect(Joystick, EffectId, &Handle->Id, Params, Flags) : S_FALSE;
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    DestroyEffect(
        _Inout_ RX_DI_FF_HANDLE* Handle) override
    {
        DWORD Id;
        HRESULT hr;

        if (FAILED(LockEffect(Handle)))
            return S_FALSE;

        Id = Handle->Id;
        Handle->Id = 0;
        hr = Driver->DestroyEffect(Joystick, Id);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    StartEffect(
        _In_ RX_DI_FF_HANDLE* Handle,
        _In_ DWORD Mode,
        _In_ DWORD Count) override
    {
        HRESULT hr = LockEffect(Handle);

        if (FAILED(hr))
            return hr;
        hr = Driver->StartEffect(Joystick, Handle->Id, Mode, Count);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    StopEffect(
        _In_ RX_DI_FF_HANDLE* Handle) override
    {
        HRESULT hr = LockEffect(Handle);

        if (FAILED(hr))
            return hr;
        hr = Driver->StopEffect(Joystick, Handle->Id);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    GetEffectStatus(
        _In_ RX_DI_FF_HANDLE* Handle,
        _Out_ LPDWORD Status) override
    {
        HRESULT hr = LockEffect(Handle);

        if (FAILED(hr))
            return hr;
        hr = Driver->GetEffectStatus(Joystick, Handle->Id, Status);
        DiUnlockJoysticks();
        return hr;
    }

    HRESULT
    SetGlobalGain(
        _In_ DWORD Gain) override
    {
        HRESULT hr;

        DiLockJoysticks();
        Slot()->GlobalGain = Gain;
        hr = ApplyGain();
        DiUnlockJoysticks();
        return hr;
    }

private:
    /* Only valid with the joystick lock held */
    RX_DI_JOY_SLOT*
    Slot(VOID)
    {
        return DiJoystickSlot(Joystick);
    }

    /* Holds the joystick lock on success, as long as the device still owns the joystick */
    HRESULT
    LockDevice(
        _In_ const RX_DI_FF_HANDLE* Handle,
        _In_ BOOLEAN Always)
    {
        DiLockJoysticks();
        if (Always || Slot()->Tag == Handle->Tag)
            return S_OK;
        DiUnlockJoysticks();
        return DIERR_NOTEXCLUSIVEACQUIRED;
    }

    /* Holds the joystick lock on success, as long as the effect is still on the device */
    HRESULT
    LockEffect(
        _Inout_ RX_DI_FF_HANDLE* Handle)
    {
        DiLockJoysticks();
        if (Slot()->Tag == Handle->Tag && Handle->Id != 0)
            return S_OK;
        Handle->Id = 0;
        DiUnlockJoysticks();
        return DIERR_NOTDOWNLOADED;
    }

    HRESULT
    ApplyGain(VOID)
    {
        RX_DI_JOY_SLOT* Shared = Slot();

        return Driver->SetGain(Joystick, MulDiv(Shared->DeviceGain, Shared->GlobalGain, DI_FULL_GAIN));
    }

    IDirectInputEffectDriver* Driver = nullptr;
    HMODULE Library = nullptr;
    DWORD Joystick = 0;
};

HRESULT
DiCreateShepherd(
    _In_opt_ HKEY Config,
    _Outptr_ RxDiShepherd** Shepherd)
{
    RxDiEffectShepherd* Created = new RxDiEffectShepherd();
    HRESULT hr;

    *Shepherd = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Load(Config);
    if (FAILED(hr))
    {
        delete Created;
        return hr;
    }

    *Shepherd = Created;
    return hr;
}
