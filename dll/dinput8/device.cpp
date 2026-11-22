/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation, identity, capabilities and objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <stdlib.h>

/* Default force feedback gain and the highest buffer size a device accepts */
#define DI_DEFAULT_GAIN             10000
#define DI_DEFAULT_MAX_BUFFER       1023

/* EnumObjects flags outside these are rejected */
#define DI_ENUM_OBJECTS_INVALID     0xE0000020

/* Types EnumObjects matches when the caller names none */
#define DI_ENUM_OBJECTS_ALL         (DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV | DIDFT_COLLECTION | DIDFT_NODATA)

DiDevice::DiDevice(
    _In_opt_ IUnknown* Outer) : DiObject(Outer)
{
    Ansi.Owner = this;
    InitializeCriticalSection(&Lock);
    Backend = nullptr;
    Emulation = nullptr;
    Instance = GUID_NULL;
    Version = 0;
    CapsFlags = 0;
    Flags = 0;
    Window = nullptr;
    Cooperation = 0;
    Event = nullptr;
    LostResult = DIERR_NOTACQUIRED;
    PolledResult = DI_OK;
    AppObjects = nullptr;
    ObjectByOffset = nullptr;
    AppDataSize = 0;
    MissingPovs = nullptr;
    MissingPovCount = 0;
    CopyMode = DiCopyPerObject;
    CopyShift = 0;
    CopyFirst = 0;
    CopyLength = 0;
    NativeState = nullptr;
    AxisOffsets = nullptr;
    AxisCount = 0;
    Baseline = nullptr;
    Relative = FALSE;
    BufferSize = 0;
    MaxBufferSize = DI_DEFAULT_MAX_BUFFER;
    Gain = DI_DEFAULT_GAIN;
    AutoCenter = 1;
}

DiDevice::~DiDevice()
{
    if (Flags & DI_DEV_ACQUIRED)
        InternalUnacquire();
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
DiDevice::ClearFormat(VOID)
{
    free(AppObjects);
    free(ObjectByOffset);
    free(MissingPovs);
    AppObjects = nullptr;
    ObjectByOffset = nullptr;
    MissingPovs = nullptr;
    MissingPovCount = 0;
    AppDataSize = 0;
    CopyMode = DiCopyPerObject;
}

VOID
DiDevice::Reset(VOID)
{
    ClearFormat();
    if (Event != nullptr)
        CloseHandle(Event);
    Event = nullptr;
    if (Emulation != nullptr)
        DiEmRelease(Emulation);
    Emulation = nullptr;
    delete Backend;
    Backend = nullptr;
    free(NativeState);
    free(AxisOffsets);
    free(Baseline);
    NativeState = nullptr;
    AxisOffsets = nullptr;
    Baseline = nullptr;
    AxisCount = 0;
    CapsFlags = 0;
    Flags = 0;
    Window = nullptr;
    Cooperation = 0;
    LostResult = DIERR_NOTACQUIRED;
    PolledResult = DI_OK;
    Relative = FALSE;
    BufferSize = 0;
    MaxBufferSize = DI_DEFAULT_MAX_BUFFER;
    Gain = DI_DEFAULT_GAIN;
    AutoCenter = 1;
}

STDMETHODIMP
DiDevice::Initialize(
    _In_ HINSTANCE Module,
    _In_ DWORD NewVersion,
    _In_ REFGUID Guid)
{
    PDI_BACKEND_CREATE Create = nullptr;
    DiBackend* Created = nullptr;
    const DI_FORMAT* Native;
    DIDEVCAPS Caps;
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
    if (Flags & DI_DEV_ACQUIRED)
    {
        Result = DIERR_ACQUIRED;
        goto Exit;
    }

    Reset();
    Result = Create(Guid, &Created);
    if (FAILED(Result))
        goto Exit;
    Backend = Created;
    Emulation = Created->Emulation();
    Native = Created->Format();

    NativeState = static_cast<BYTE*>(calloc(1, Native->DataSize + sizeof(DWORD)));
    Baseline = static_cast<BYTE*>(calloc(1, Native->DataSize + sizeof(DWORD)));
    AxisOffsets = static_cast<DWORD*>(calloc(Native->ObjectCount + 1, sizeof(DWORD)));
    if (NativeState == nullptr || Baseline == nullptr || AxisOffsets == nullptr)
    {
        Reset();
        Result = E_OUTOFMEMORY;
        goto Exit;
    }
    for (DWORD Index = 0; Index < Native->ObjectCount; Index++)
    {
        if (Native->Objects[Index].Type & DIDFT_AXIS)
            AxisOffsets[AxisCount++] = Native->Objects[Index].Offset;
    }

    ZeroMemory(&Caps, sizeof(Caps));
    Caps.dwSize = sizeof(DIDEVCAPS_DX3);
    Result = Created->GetCapabilities(&Caps);
    if (FAILED(Result))
    {
        Reset();
        goto Exit;
    }
    PolledResult = (Caps.dwFlags & DIDC_POLLEDDEVICE) ? DI_POLLEDDEVICE : DI_OK;
    CapsFlags = Emulation != nullptr && (Emulation->Flags & DI_EM_ACTIVE) ? DIDC_EMULATED : 0;
    Instance = Guid;
    Version = NewVersion;
    Result = DI_OK;

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
    if (Flags & DI_DEV_POLLED_FORMAT)
        Caps->dwFlags |= DIDC_POLLEDDATAFORMAT;
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

/* Finds the device object an object identifier names */
HRESULT
DiDevice::ResolveObject(
    _In_ DWORD Object,
    _In_ DWORD How,
    _Out_ DWORD* Index)
{
    const DI_FORMAT* Native = Backend->Format();

    *Index = static_cast<DWORD>(-1);
    switch (How)
    {
        case DIPH_DEVICE:
            return E_INVALIDARG;
        case DIPH_BYOFFSET:
            if (AppObjects == nullptr || ObjectByOffset == nullptr || Object >= AppDataSize || ObjectByOffset[Object] < 0)
                return DIERR_OBJECTNOTFOUND;
            *Index = static_cast<DWORD>(ObjectByOffset[Object]);
            return S_OK;
        case DIPH_BYID:
            for (DWORD Candidate = Native->ObjectCount; Candidate-- > 0;)
            {
                if (((Native->Objects[Candidate].Type ^ Object) & 0xFFFFFF) == 0)
                {
                    *Index = Candidate;
                    return S_OK;
                }
            }
            return DIERR_OBJECTNOTFOUND;
        case DIPH_BYUSAGE:
            return Backend->MapUsage(Object, Index);
        default:
            return E_INVALIDARG;
    }
}

HRESULT
DiDevice::GetObjectInfoCore(
    _In_ DWORD Object,
    _In_ DWORD How,
    _Inout_ LPDIDEVICEOBJECTINSTANCEW Info)
{
    const DI_OBJECT* Source;
    DWORD Index;
    HRESULT Result;

    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    Result = ResolveObject(Object, How, &Index);
    if (FAILED(Result))
        return Result;
    Source = &Backend->Format()->Objects[Index];
    Info->guidType = *Source->Guid;
    Info->dwOfs = Source->Offset;
    Info->dwType = Source->Type;
    Info->dwFlags = Source->Flags;
    ZeroMemory(Info->tszName, Info->dwSize - FIELD_OFFSET(DIDEVICEOBJECTINSTANCEW, tszName));
    return Backend->GetObjectInfo(Index, Source->Type, Info);
}

STDMETHODIMP
DiDevice::GetObjectInfo(
    _Inout_ LPDIDEVICEOBJECTINSTANCEW Info,
    _In_ DWORD Object,
    _In_ DWORD How)
{
    HRESULT Result;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCEW) && Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCE_DX3W))
        return E_INVALIDARG;
    EnterCriticalSection(&Lock);
    Result = GetObjectInfoCore(Object, How, Info);
    LeaveCriticalSection(&Lock);
    return Result;
}

static
VOID
DiObjectToAnsi(
    _Inout_ LPDIDEVICEOBJECTINSTANCEA Ansi,
    _In_ LPCDIDEVICEOBJECTINSTANCEW Wide)
{
    Ansi->guidType = Wide->guidType;
    Ansi->dwOfs = Wide->dwOfs;
    Ansi->dwType = Wide->dwType;
    Ansi->dwFlags = Wide->dwFlags;
    WideCharToMultiByte(CP_ACP, 0, Wide->tszName, -1, Ansi->tszName, MAX_PATH, nullptr, nullptr);
    if (Ansi->dwSize >= sizeof(DIDEVICEOBJECTINSTANCEA))
    {
        Ansi->dwFFMaxForce = Wide->dwFFMaxForce;
        Ansi->dwFFForceResolution = Wide->dwFFForceResolution;
        Ansi->wCollectionNumber = Wide->wCollectionNumber;
        Ansi->wDesignatorIndex = Wide->wDesignatorIndex;
        Ansi->wUsagePage = Wide->wUsagePage;
        Ansi->wUsage = Wide->wUsage;
        Ansi->dwDimension = Wide->dwDimension;
        Ansi->wExponent = Wide->wExponent;
        Ansi->wReportId = Wide->wReportId;
    }
}

STDMETHODIMP
DiDevice::AnsiFace::GetObjectInfo(
    _Inout_ LPDIDEVICEOBJECTINSTANCEA Info,
    _In_ DWORD Object,
    _In_ DWORD How)
{
    DIDEVICEOBJECTINSTANCEW Wide;
    HRESULT Result;

    if (Info == nullptr)
        return E_POINTER;
    if (Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCEA) && Info->dwSize != sizeof(DIDEVICEOBJECTINSTANCE_DX3A))
        return E_INVALIDARG;
    ZeroMemory(&Wide, sizeof(Wide));
    Wide.dwSize = sizeof(Wide);
    EnterCriticalSection(&Owner->Lock);
    Result = Owner->GetObjectInfoCore(Object, How, &Wide);
    LeaveCriticalSection(&Owner->Lock);
    if (SUCCEEDED(Result))
        DiObjectToAnsi(Info, &Wide);
    return Result;
}

/*
 * The type bits select object kinds, the high bits pick aspects, the instance field names a
 * collection, and aliased or vendor objects only come back when asked for.
 */
STDMETHODIMP
DiDevice::EnumObjects(
    _In_ LPDIENUMDEVICEOBJECTSCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    const DI_FORMAT* Native;
    DWORD Hidden = DIDFT_ALIAS | DIDFT_VENDORDEFINED;
    DWORD Wanted;
    DWORD Collection;
    HRESULT Result = DI_OK;

    if (Callback == nullptr || (Flags & DI_ENUM_OBJECTS_INVALID) != 0)
        return E_INVALIDARG;
    EnterCriticalSection(&Lock);
    if (Backend == nullptr)
    {
        LeaveCriticalSection(&Lock);
        return DIERR_NOTINITIALIZED;
    }
    Native = Backend->Format();
    LeaveCriticalSection(&Lock);

    if (Flags & DIDFT_ALIAS)
        Hidden &= ~DIDFT_ALIAS;
    if (Flags & DIDFT_VENDORDEFINED)
        Hidden &= ~DIDFT_VENDORDEFINED;
    Wanted = Flags & ~(DIDFT_ALIAS | DIDFT_VENDORDEFINED);
    if (Wanted == 0)
        Wanted = DI_ENUM_OBJECTS_ALL;
    if ((Wanted & DIDFT_INSTANCEMASK) == 0)
        Collection = 0xFFFF;
    else
        Collection = DIDFT_GETINSTANCE(Wanted) == 0xFFFF ? 0 : DIDFT_GETINSTANCE(Wanted);

    AddRef();
    for (DWORD Index = 0; Index < Native->ObjectCount; Index++)
    {
        const DI_OBJECT* Object = &Native->Objects[Index];
        DIDEVICEOBJECTINSTANCEW Info;
        HRESULT Status;

        if ((Object->Type & Wanted & 0xFF) == 0 || (Object->Type & Wanted & 0xFF000000) != (Wanted & 0xFF000000) ||
            (Object->Type & Hidden) != 0)
        {
            continue;
        }
        ZeroMemory(&Info, sizeof(Info));
        Info.dwSize = sizeof(Info);
        Info.guidType = *Object->Guid;
        Info.dwOfs = Object->Offset;
        Info.dwType = Object->Type;
        Info.dwFlags = Object->Flags;
        Status = Backend->GetObjectInfo(Index, Object->Type, &Info);
        if (Status == DIERR_OBJECTNOTFOUND)
            continue;
        if (FAILED(Status))
        {
            Result = Status;
            break;
        }
        if (Collection != 0xFFFF && Collection != Info.wCollectionNumber)
        {
            Result = Status;
            break;
        }
        if (Callback(&Info, Context) == DIENUM_STOP)
            break;
    }
    Release();
    return Result;
}

struct DI_ANSI_OBJECTS
{
    LPDIENUMDEVICEOBJECTSCALLBACKA Callback;
    VOID* Context;
};

static
BOOL
CALLBACK
DiAnsiObjectThunk(
    _In_ LPCDIDEVICEOBJECTINSTANCEW Wide,
    _In_ VOID* Context)
{
    DI_ANSI_OBJECTS* Enum = static_cast<DI_ANSI_OBJECTS*>(Context);
    DIDEVICEOBJECTINSTANCEA Ansi;

    ZeroMemory(&Ansi, sizeof(Ansi));
    Ansi.dwSize = sizeof(Ansi);
    DiObjectToAnsi(&Ansi, Wide);
    return Enum->Callback(&Ansi, Enum->Context);
}

STDMETHODIMP
DiDevice::AnsiFace::EnumObjects(
    _In_ LPDIENUMDEVICEOBJECTSCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    DI_ANSI_OBJECTS Enum;

    if (Callback == nullptr)
        return E_INVALIDARG;
    Enum.Callback = Callback;
    Enum.Context = Context;
    return Owner->EnumObjects(DiAnsiObjectThunk, &Enum, Flags);
}

/* Force feedback answers for devices without an effect driver */

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
    if (Created == nullptr)
        return E_POINTER;
    *Created = nullptr;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP
DiDevice::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKW Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Type);
    if (Callback == nullptr)
        return E_INVALIDARG;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DI_OK;
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffects(
    _In_ LPDIENUMEFFECTSCALLBACKA Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Type)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Type);
    if (Callback == nullptr)
        return E_INVALIDARG;
    if (Owner->Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DI_OK;
}

STDMETHODIMP
DiDevice::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOW Info,
    _In_ REFGUID Effect)
{
    UNREFERENCED_PARAMETER(Effect);
    if (Info == nullptr)
        return E_POINTER;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_DEVICENOTREG;
}

STDMETHODIMP
DiDevice::AnsiFace::GetEffectInfo(
    _Inout_ LPDIEFFECTINFOA Info,
    _In_ REFGUID Effect)
{
    UNREFERENCED_PARAMETER(Effect);
    if (Info == nullptr)
        return E_POINTER;
    if (Owner->Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_DEVICENOTREG;
}

STDMETHODIMP
DiDevice::GetForceFeedbackState(
    _Out_ LPDWORD State)
{
    if (State == nullptr)
        return E_POINTER;
    *State = 0;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP
DiDevice::SendForceFeedbackCommand(
    _In_ DWORD Command)
{
    UNREFERENCED_PARAMETER(Command);
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP
DiDevice::EnumCreatedEffectObjects(
    _In_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Context);
    if (Callback == nullptr || Flags != 0)
        return E_INVALIDARG;
    return DI_OK;
}

STDMETHODIMP
DiDevice::Escape(
    _Inout_ LPDIEFFESCAPE Escape)
{
    if (Escape == nullptr)
        return E_POINTER;
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_UNSUPPORTED;
}

/* Effect files open before anything else is checked, so a missing file reports first */
static
HRESULT
DiOpenEffectFile(
    _In_z_ LPCWSTR FileName)
{
    HANDLE File;

    if (FileName == nullptr)
        return E_POINTER;
    File = CreateFileW(FileName, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (File == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(File);
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP
DiDevice::EnumEffectsInFile(
    _In_z_ LPCWSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Callback);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Flags);
    return DiOpenEffectFile(FileName);
}

STDMETHODIMP
DiDevice::AnsiFace::EnumEffectsInFile(
    _In_z_ LPCSTR FileName,
    _In_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ VOID* Context,
    _In_ DWORD Flags)
{
    WCHAR Wide[MAX_PATH];

    if (FileName == nullptr)
        return E_POINTER;
    MultiByteToWideChar(CP_ACP, 0, FileName, -1, Wide, _countof(Wide));
    return Owner->EnumEffectsInFile(Wide, Callback, Context, Flags);
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
    if (Backend == nullptr)
        return DIERR_NOTINITIALIZED;
    return DIERR_UNSUPPORTED;
}

STDMETHODIMP
DiDevice::AnsiFace::WriteEffectToFile(
    _In_z_ LPCSTR FileName,
    _In_ DWORD Count,
    _In_reads_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags)
{
    WCHAR Wide[MAX_PATH];

    if (FileName == nullptr)
        return E_POINTER;
    MultiByteToWideChar(CP_ACP, 0, FileName, -1, Wide, _countof(Wide));
    return Owner->WriteEffectToFile(Wide, Count, Effects, Flags);
}

/* Action mapping */

STDMETHODIMP
DiDevice::BuildActionMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_opt_z_ LPCWSTR UserName,
    _In_ DWORD Flags)
{
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(UserName);
    UNREFERENCED_PARAMETER(Flags);
    DI_TRACE_ONCE(L"BuildActionMap is not implemented");
    return Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
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
    DI_TRACE_ONCE(L"BuildActionMap is not implemented");
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
    DI_TRACE_ONCE(L"SetActionMap is not implemented");
    return Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
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
    DI_TRACE_ONCE(L"SetActionMap is not implemented");
    return Owner->Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERW Header)
{
    UNREFERENCED_PARAMETER(Header);
    DI_TRACE_ONCE(L"GetImageInfo is not implemented");
    return Backend == nullptr ? DIERR_NOTINITIALIZED : E_NOTIMPL;
}

STDMETHODIMP
DiDevice::AnsiFace::GetImageInfo(
    _Inout_ LPDIDEVICEIMAGEINFOHEADERA Header)
{
    UNREFERENCED_PARAMETER(Header);
    DI_TRACE_ONCE(L"GetImageInfo is not implemented");
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
