/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation, identity and capabilities
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

DiDevice::DiDevice(
    _In_opt_ IUnknown* Outer) : DiObject(Outer), Backend(nullptr), Instance(GUID_NULL), Version(0), CapsFlags(0)
{
    Ansi.Owner = this;
    InitializeCriticalSection(&Lock);
}

DiDevice::~DiDevice()
{
    Reset();
    DeleteCriticalSection(&Lock);
}

VOID*
DiDevice::FindInterface(
    _In_ REFIID Riid)
{
    if (IsEqualIID(Riid, IID_IDirectInputDevice8W) || DiIsUnknown(Riid))
        return static_cast<IDirectInputDevice8W*>(this);
    if (IsEqualIID(Riid, IID_IDirectInputDevice8A))
        return static_cast<IDirectInputDevice8A*>(&Ansi);
    return nullptr;
}

VOID
DiDevice::Reset(VOID)
{
    delete Backend;
    Backend = nullptr;
    CapsFlags = 0;
}

STDMETHODIMP
DiDevice::Initialize(
    _In_ HINSTANCE Module,
    _In_ DWORD NewVersion,
    _In_ REFGUID Guid)
{
    PDI_BACKEND_CREATE Create = nullptr;
    DiBackend* Created = nullptr;
    HRESULT Result;

    if (&Guid == nullptr)
        return E_POINTER;
    EnterCriticalSection(&Lock);
    Result = DiCheckVersion(Module, NewVersion);
    if (FAILED(Result))
        goto Exit;
    for (const auto& Device : DiStaticDevices)
    {
        if (IsEqualGUID(Guid, *Device.Guid))
        {
            Create = Device.Create;
            break;
        }
    }
    if (Create == nullptr)
    {
        Result = DIERR_DEVICENOTREG;
        goto Exit;
    }

    Reset();
    Result = Create(Guid, &Created);
    if (FAILED(Result))
        goto Exit;
    Backend = Created;
    CapsFlags = Created->Emulated() ? DIDC_EMULATED : 0;
    Instance = Guid;
    Version = NewVersion;

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::GetCapabilities(
    _Inout_ LPDIDEVCAPS Caps)
{
    HRESULT Result;

    if (Caps == nullptr)
        return E_POINTER;
    if (Caps->dwSize != sizeof(DIDEVCAPS) && Caps->dwSize != sizeof(DIDEVCAPS_DX3))
        return E_INVALIDARG;

    EnterCriticalSection(&Lock);
    ZeroMemory(reinterpret_cast<BYTE*>(Caps) + sizeof(Caps->dwSize), Caps->dwSize - sizeof(Caps->dwSize));
    if (Backend == nullptr)
    {
        Result = DIERR_NOTINITIALIZED;
        goto Exit;
    }
    Result = Backend->GetCapabilities(Caps);
    if (FAILED(Result))
        goto Exit;
    Caps->dwFlags |= CapsFlags;
    if (Caps->dwSize >= sizeof(DIDEVCAPS))
    {
        DIDRIVERVERSIONS Versions = { sizeof(Versions) };

        Backend->GetVersions(&Versions);
        Caps->dwFirmwareRevision = Versions.dwFirmwareRevision;
        Caps->dwHardwareRevision = Versions.dwHardwareRevision;
        Caps->dwFFDriverVersion = Versions.dwFFDriverVersion;
    }
    Result = S_OK;

Exit:
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::GetDeviceInfo(
    _Inout_ LPDIDEVICEINSTANCEW Info)
{
    HRESULT Result;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEINSTANCEW) && Info->dwSize != sizeof(DIDEVICEINSTANCE_DX3W))
        return E_INVALIDARG;

    EnterCriticalSection(&Lock);
    Info->guidInstance = Instance;
    Info->guidProduct = Instance;
    ZeroMemory(&Info->dwDevType, Info->dwSize - FIELD_OFFSET(DIDEVICEINSTANCEW, dwDevType));
    Result = Backend != nullptr ? Backend->GetDeviceInfo(Info) : DIERR_NOTINITIALIZED;
    LeaveCriticalSection(&Lock);
    return Result;
}

STDMETHODIMP
DiDevice::AnsiFace::GetDeviceInfo(
    _Inout_ LPDIDEVICEINSTANCEA Info)
{
    DIDEVICEINSTANCEW Wide;
    HRESULT Result;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEINSTANCEA) && Info->dwSize != sizeof(DIDEVICEINSTANCE_DX3A))
        return E_INVALIDARG;
    ZeroMemory(&Wide, sizeof(Wide));
    Wide.dwSize = sizeof(Wide);
    Result = Owner->GetDeviceInfo(&Wide);
    if (SUCCEEDED(Result))
    {
        DiInstanceToAnsi(Info, &Wide);
        Result = S_OK;
    }
    return Result;
}

STDMETHODIMP
DiDevice::RunControlPanel(
    _In_opt_ HWND Window,
    _In_ DWORD Flags)
{
    if (Window != nullptr && !IsWindow(Window))
        return E_HANDLE;
    if (Flags != 0)
        return E_INVALIDARG;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return Backend->RunControlPanel(Window, 0);
}

/* Members the data path fills in */

#define DI_DEVICE_PENDING() (Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL)

STDMETHODIMP
DiDevice::EnumObjects(
    _In_ LPDIENUMDEVICEOBJECTSCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::EnumObjects(
    _In_ LPDIENUMDEVICEOBJECTSCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetProperty(
    _In_ REFGUID Property,
    _Inout_ LPDIPROPHEADER Header)
{
    UNREFERENCED_PARAMETER(Property);
    UNREFERENCED_PARAMETER(Header);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::GetProperty(
    _In_ REFGUID Property,
    _Inout_ LPDIPROPHEADER Header)
{
    return Owner->GetProperty(Property, Header);
}

STDMETHODIMP
DiDevice::SetProperty(
    _In_ REFGUID Property,
    _In_ LPCDIPROPHEADER Header)
{
    UNREFERENCED_PARAMETER(Property);
    UNREFERENCED_PARAMETER(Header);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::SetProperty(
    _In_ REFGUID Property,
    _In_ LPCDIPROPHEADER Header)
{
    return Owner->SetProperty(Property, Header);
}

STDMETHODIMP
DiDevice::Acquire()
{
    return E_INVALIDARG;
}

STDMETHODIMP
DiDevice::Unacquire()
{
    return DI_NOEFFECT;
}

STDMETHODIMP
DiDevice::GetDeviceState(
    _In_ DWORD Size,
    _Out_writes_bytes_(Size) VOID* Data)
{
    UNREFERENCED_PARAMETER(Size);
    UNREFERENCED_PARAMETER(Data);
    return DIERR_NOTACQUIRED;
}

STDMETHODIMP
DiDevice::GetDeviceData(
    _In_ DWORD ObjectSize,
    _Out_writes_opt_(*Count) LPDIDEVICEOBJECTDATA Data,
    _Inout_ LPDWORD Count,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(ObjectSize);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Flags);
    return DIERR_NOTBUFFERED;
}

STDMETHODIMP
DiDevice::SetDataFormat(
    _In_ LPCDIDATAFORMAT Format)
{
    UNREFERENCED_PARAMETER(Format);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::SetEventNotification(
    _In_opt_ HANDLE Event)
{
    UNREFERENCED_PARAMETER(Event);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::SetCooperativeLevel(
    _In_opt_ HWND Window,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Window);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::GetObjectInfo(
    _Inout_ LPDIDEVICEOBJECTINSTANCEW Info,
    _In_ DWORD Object,
    _In_ DWORD How)
{
    UNREFERENCED_PARAMETER(Info);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(How);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::GetObjectInfo(
    _Inout_ LPDIDEVICEOBJECTINSTANCEA Info,
    _In_ DWORD Object,
    _In_ DWORD How)
{
    UNREFERENCED_PARAMETER(Info);
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(How);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::CreateEffect(
    _In_ REFGUID Effect,
    _In_opt_ LPCDIEFFECT Params,
    _Out_ LPDIRECTINPUTEFFECT* Created,
    _In_opt_ LPUNKNOWN Outer)
{
    UNREFERENCED_PARAMETER(Effect);
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(Outer);
    if (Created != nullptr)
        *Created = nullptr;
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Type);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Type);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOW Info,
    _In_ REFGUID Effect)
{
    UNREFERENCED_PARAMETER(Info);
    UNREFERENCED_PARAMETER(Effect);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOA Info,
    _In_ REFGUID Effect)
{
    UNREFERENCED_PARAMETER(Info);
    UNREFERENCED_PARAMETER(Effect);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetForceFeedbackState(
    _Out_ LPDWORD State)
{
    UNREFERENCED_PARAMETER(State);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::SendForceFeedbackCommand(
    _In_ DWORD Command)
{
    UNREFERENCED_PARAMETER(Command);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::EnumCreatedEffectObjects(
    _In_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::Escape(
    _Inout_ LPDIEFFESCAPE Escape)
{
    UNREFERENCED_PARAMETER(Escape);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::Poll()
{
    return DIERR_NOTACQUIRED;
}

STDMETHODIMP
DiDevice::SendDeviceData(
    _In_ DWORD ObjectSize,
    _In_reads_(*Count) LPCDIDEVICEOBJECTDATA Data,
    _Inout_ LPDWORD Count,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(ObjectSize);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::EnumEffectsInFile(
    _In_z_ LPCWSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffectsInFile(
    _In_z_ LPCSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::WriteEffectToFile(
    _In_z_ LPCWSTR FileName,
    _In_ DWORD Count,
    _In_reads_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::WriteEffectToFile(
    _In_z_ LPCSTR FileName,
    _In_ DWORD Count,
    _In_reads_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(FileName);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Flags);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::BuildActionMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_opt_z_ LPCWSTR UserName,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(UserName);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::BuildActionMap(
    _Inout_ LPDIACTIONFORMATA Format,
    _In_opt_z_ LPCSTR UserName,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(UserName);
    UNREFERENCED_PARAMETER(Flags);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::SetActionMap(
    _In_ LPDIACTIONFORMATW Format,
    _In_opt_z_ LPCWSTR UserName,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(UserName);
    UNREFERENCED_PARAMETER(Flags);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::SetActionMap(
    _In_ LPDIACTIONFORMATA Format,
    _In_opt_z_ LPCSTR UserName,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(UserName);
    UNREFERENCED_PARAMETER(Flags);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERW Header)
{
    UNREFERENCED_PARAMETER(Header);
    return DI_DEVICE_PENDING();
}

STDMETHODIMP
DiDevice::AnsiFace::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERA Header)
{
    UNREFERENCED_PARAMETER(Header);
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

HRESULT
DiCreateDevice(
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result)
{
    DiDevice* Device;
    HRESULT Status;

    if (Result == nullptr)
        return E_POINTER;
    *Result = nullptr;
    Status = DiSharedInitialize();
    if (FAILED(Status))
        return Status;
    if (Outer != nullptr && !DiIsUnknown(Riid))
        return CLASS_E_NOAGGREGATION;
    Device = new (std::nothrow) DiDevice(Outer);
    if (Device == nullptr)
        return E_OUTOFMEMORY;
    return DiFinishCreate(Device, Outer, Riid, Result);
}
