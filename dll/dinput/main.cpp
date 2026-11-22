/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Entry points, class factory, registration and process wide state
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#include <aclapi.h>
#include <strsafe.h>

HINSTANCE DiModule;
LONG DiModuleRefs;
CRITICAL_SECTION DiLock;
BOOL DiLockReady;
BOOL DiLowLevelHooks;
ULONG DiEmulation;
HANDLE DiGlobalMutex;
volatile LONG* DiSequence;

static HHOOK DiHookProbe;
static UINT DiJoyChangedMessage;
static HANDLE DiWinmmEvent;
static HANDLE DiSharedMapping;
static HANDLE DiJoyMutex;

/* Exclusive owners shared by every process using DirectInput */
typedef struct _RX_DI_OWNER
{
    GUID Instance;
    HWND Window;
    DWORD Process;
    DWORD Flags;
} RX_DI_OWNER;

#define DI_OWNER_LIMIT  121

typedef struct _RX_DI_SHARED
{
    LONG OwnerCount;
    LONG Sequence;
    LONG GuidSequence;
    RX_DI_JOY_SLOT Joysticks[DI_JOY_SLOTS];
    BYTE Reserved[4];
    RX_DI_OWNER Owners[DI_OWNER_LIMIT];
} RX_DI_SHARED;

static_assert(sizeof(RX_DI_SHARED) <= 4096, "shared block must fit one page");

static RX_DI_SHARED* DiShared;

/* Classes DllGetClassObject serves */
typedef
HRESULT
(*PRX_DI_CLASS_CREATE)(
    _In_opt_ IUnknown* OuterUnknown,
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object);

typedef struct _RX_DI_CLASS
{
    const CLSID* Class;
    PRX_DI_CLASS_CREATE Create;
    UINT NameId;
} RX_DI_CLASS;

static const RX_DI_CLASS DiClasses[] =
{
    { &CLSID_DirectInput, RxDirectInput::Create, IDS_DIRECTINPUT },
    { &CLSID_DirectInputDevice, RxDiDevice::Create, IDS_DIRECTINPUTDEVICE },
};

RxDiUnknown::RxDiUnknown(
    _In_opt_ IUnknown* OuterUnknown)
    : RefCount(1), HoldCount(1)
{
    Inner.Owner = this;
    Outer = (OuterUnknown != nullptr) ? OuterUnknown : &Inner;
    InterlockedIncrement(&DiModuleRefs);
}

RxDiUnknown::~RxDiUnknown()
{
    InterlockedDecrement(&DiModuleRefs);
}

VOID
RxDiUnknown::Hold(VOID)
{
    InterlockedIncrement(&HoldCount);
}

VOID
RxDiUnknown::Unhold(VOID)
{
    if (InterlockedDecrement(&HoldCount) == 0)
        delete this;
}

HRESULT
STDMETHODCALLTYPE
RxDiUnknown::RxInner::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ PVOID* Object)
{
    PVOID Found;

    /* IUnknown answers with the primary interface, as the original object layout did */
    if (IsEqualIID(Iid, IID_IUnknown))
    {
        *Object = Owner->PrimaryInterface();
    }
    else
    {
        Found = Owner->QueryOwn(Iid);
        if (Found == nullptr)
            return Owner->QuerySlow(Iid, Object);
        *Object = Found;
    }

    Owner->Outer->AddRef();
    return S_OK;
}

ULONG
STDMETHODCALLTYPE
RxDiUnknown::RxInner::AddRef(VOID)
{
    if (Owner->RefCount == 0)
        return 0;

    InterlockedIncrement(&Owner->HoldCount);
    return InterlockedIncrement(&Owner->RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDiUnknown::RxInner::Release(VOID)
{
    RxDiUnknown* Self = Owner;
    LONG Remaining = InterlockedDecrement(&Self->RefCount);

    if (Remaining > 0)
    {
        InterlockedDecrement(&Self->HoldCount);
        return Remaining;
    }

    if (Remaining == 0)
    {
        Self->OnFinalRelease();
        Self->Unhold();
    }
    return 0;
}

/* The class factory for CoCreateInstance */
class RxDiFactory final : public RxDiUnknown, public IClassFactory
{
public:
    explicit
    RxDiFactory(
        _In_ PRX_DI_CLASS_CREATE Creator)
        : RxDiUnknown(nullptr), Create(Creator)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID Iid,
        _COM_Outptr_ PVOID* Object) override
    {
        if (Object == nullptr)
            return E_POINTER;
        *Object = nullptr;
        return Outer->QueryInterface(Iid, Object);
    }

    ULONG STDMETHODCALLTYPE AddRef(VOID) override { return Outer->AddRef(); }
    ULONG STDMETHODCALLTYPE Release(VOID) override { return Outer->Release(); }

    HRESULT
    STDMETHODCALLTYPE
    CreateInstance(
        _In_opt_ IUnknown* OuterUnknown,
        _In_ REFIID Iid,
        _COM_Outptr_ PVOID* Object) override
    {
        if (Object == nullptr)
            return E_INVALIDARG;

        *Object = nullptr;
        if (OuterUnknown != nullptr)
            return CLASS_E_NOAGGREGATION;

        return Create(nullptr, Iid, Object);
    }

    HRESULT
    STDMETHODCALLTYPE
    LockServer(
        _In_ BOOL Lock) override
    {
        if (Lock)
            InterlockedIncrement(&DiModuleRefs);
        else
            InterlockedDecrement(&DiModuleRefs);
        return S_OK;
    }

private:
    PVOID PrimaryInterface(VOID) override { return static_cast<IClassFactory*>(this); }

    PVOID
    QueryOwn(
        _In_ REFIID Iid) override
    {
        if (IsEqualIID(Iid, IID_IClassFactory))
            return static_cast<IClassFactory*>(this);
        return nullptr;
    }

    PRX_DI_CLASS_CREATE Create;
};

HRESULT
DiCheckVersion(
    _In_opt_ HINSTANCE Instance,
    _In_ DWORD Version)
{
    WCHAR Probe[3];
    BOOL Valid;

    if (Instance != nullptr)
        Valid = GetModuleFileNameW(Instance, Probe, _countof(Probe)) != 0;
    else
        Valid = (Version == 0x0300);

    if (!Valid)
        return E_INVALIDARG;

    switch (Version)
    {
        case 0x0300:
        case 0x0500:
        case 0x050A:
        case 0x05B2:
        case 0x0602:
        case 0x061A:
        case 0x0700:
            return S_OK;
    }

    if (Version == 0)
        return DIERR_NOTINITIALIZED;
    if (Version >= 0x0700)
        return DIERR_OLDDIRECTINPUTVERSION;
    return DIERR_BETADIRECTINPUTVERSION;
}

BOOLEAN
DiGetApplicationId(
    _Out_writes_(276) PWSTR Id,
    _Out_writes_opt_(MAX_PATH) PWSTR Name)
{
    WCHAR Module[MAX_PATH];
    WCHAR Full[MAX_PATH];
    IMAGE_DOS_HEADER Dos;
    IMAGE_NT_HEADERS32 Headers;
    PWSTR Leaf = nullptr;
    HANDLE File;
    DWORD Read;
    DWORD Size;
    BOOLEAN Found = FALSE;

    static_assert(sizeof(Headers) == 248, "header prefix the id is built from");

    if (GetModuleFileNameW(GetModuleHandleW(nullptr), Module, _countof(Module)) == 0)
        return FALSE;

    if (GetFullPathNameW(Module, _countof(Full), Full, &Leaf) == 0)
        return FALSE;

    File = CreateFileW(Full, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (File == INVALID_HANDLE_VALUE || File == nullptr)
        return FALSE;

    if (ReadFile(File, &Dos, sizeof(Dos), &Read, nullptr) && Read == sizeof(Dos) &&
        Dos.e_magic == IMAGE_DOS_SIGNATURE &&
        SetFilePointer(File, Dos.e_lfanew, nullptr, FILE_BEGIN) == static_cast<DWORD>(Dos.e_lfanew))
    {
        BYTE Raw[264];

        if (ReadFile(File, Raw, sizeof(Raw), &Read, nullptr) && Read == sizeof(Raw) &&
            *reinterpret_cast<PDWORD>(Raw) == IMAGE_NT_SIGNATURE)
        {
            Size = GetFileSize(File, nullptr);
            if (Size != INVALID_FILE_SIZE)
            {
                CopyMemory(&Headers, Raw, sizeof(Headers));
                CharUpperW(Leaf);
                StringCchPrintfW(Id, 276, L"%s%8.8lX%8.8lX", Leaf, Headers.FileHeader.TimeDateStamp, Size);
                if (Name != nullptr)
                    StringCchCopyW(Name, MAX_PATH, Leaf);
                Found = TRUE;
            }
        }
    }

    CloseHandle(File);
    return Found;
}

VOID
DiGetAppHacks(
    _Out_ RX_DI_APP_HACKS* Hacks)
{
    static const struct
    {
        PCWSTR Name;
        SIZE_T Offset;
        ULONG Default;
    } Values[] =
    {
        { L"ReAcquire", FIELD_OFFSET(RX_DI_APP_HACKS, ReAcquire), 0 },
        { L"NoSubClass", FIELD_OFFSET(RX_DI_APP_HACKS, NoSubClass), 0 },
        { L"MaxDeviceNameLength", FIELD_OFFSET(RX_DI_APP_HACKS, MaxDeviceNameLength), MAX_PATH },
        { L"NativeAxisOnly", FIELD_OFFSET(RX_DI_APP_HACKS, NativeAxisOnly), 0 },
        { L"NoPollUnacquire", FIELD_OFFSET(RX_DI_APP_HACKS, NoPollUnacquire), 0 },
        { L"SucceedAcquire", FIELD_OFFSET(RX_DI_APP_HACKS, SucceedAcquire), 0 },
    };
    WCHAR Id[276];
    HKEY Compat = nullptr;
    HKEY App = nullptr;

    ZeroMemory(Hacks, sizeof(*Hacks));
    Hacks->MaxDeviceNameLength = MAX_PATH;

    if (!DiGetApplicationId(Id, nullptr))
        return;

    if (SUCCEEDED(DiOpenKey(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\DirectInput\\Compatibility",
                            KEY_READ, 0, &Compat)))
    {
        DiOpenKey(Compat, Id, KEY_READ, 0, &App);
        RegCloseKey(Compat);
    }

    if (App == nullptr || App == INVALID_HANDLE_VALUE)
        return;

    for (ULONG Index = 0; Index < _countof(Values); Index++)
    {
        PULONG Field = reinterpret_cast<PULONG>(reinterpret_cast<PBYTE>(Hacks) + Values[Index].Offset);

        if (!DiReadDword(App, Values[Index].Name, Field))
            *Field = Values[Index].Default;
    }

    DiTrace(L"applying compatibility values for %s", Id);
    RegCloseKey(App);
}

static
VOID
DiObjectName(
    _Out_writes_(51) PWSTR Name,
    _In_ const GUID* Guid)
{
    StringCchPrintfW(Name, 51, L"DirectInput.{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                     Guid->Data1, Guid->Data2, Guid->Data3, Guid->Data4[0], Guid->Data4[1],
                     Guid->Data4[2], Guid->Data4[3], Guid->Data4[4], Guid->Data4[5],
                     Guid->Data4[6], Guid->Data4[7]);
}

HRESULT
DiStartup(VOID)
{
    WCHAR Name[51];
    HRESULT hr = S_OK;
    HKEY Key;

    EnterCriticalSection(&DiLock);

    if (DiGlobalMutex == nullptr)
    {
        DiObjectName(Name, &IID_IDirectInputW);
        DiGlobalMutex = CreateMutexW(nullptr, TRUE, Name);
        if (DiGlobalMutex == nullptr)
        {
            DiTrace(L"global mutex creation failed, error %lu", GetLastError());
            hr = E_FAIL;
            goto Done;
        }

        DiEmulation = 0;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\DirectInput", 0, KEY_READ, &Key) == ERROR_SUCCESS)
        {
            DWORD Value = 0;
            DWORD Size = sizeof(Value);

            if (RegQueryValueExW(Key, L"Emulation", nullptr, nullptr, reinterpret_cast<PBYTE>(&Value), &Size) == ERROR_SUCCESS &&
                Size == sizeof(Value))
            {
                DiEmulation = Value;
            }
            RegCloseKey(Key);
        }

        if (DiEmulation != 0)
            DiTrace(L"emulation override 0x%lx", DiEmulation);
        ReleaseMutex(DiGlobalMutex);
    }

    if (DiShared == nullptr)
    {
        DiObjectName(Name, &IID_IDirectInputDeviceW);
        DiSharedMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 4096, Name);
        if (DiSharedMapping == nullptr)
        {
            DiTrace(L"shared section creation failed, error %lu", GetLastError());
            hr = E_FAIL;
            goto Done;
        }

        DiShared = static_cast<RX_DI_SHARED*>(MapViewOfFile(DiSharedMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0));
        if (DiShared == nullptr)
        {
            DiTrace(L"shared section map failed, error %lu", GetLastError());
            hr = E_FAIL;
            goto Done;
        }
    }

    if (DiJoyMutex == nullptr)
    {
        DiObjectName(Name, &IID_IDirectInputDevice2A);
        DiJoyMutex = CreateMutexW(nullptr, FALSE, Name);
        if (DiJoyMutex == nullptr)
        {
            DiTrace(L"joystick mutex creation failed, error %lu", GetLastError());
            hr = E_FAIL;
            goto Done;
        }
    }

    if (DiSequence == nullptr)
        DiSequence = &DiShared->Sequence;

Done:
    LeaveCriticalSection(&DiLock);
    return hr;
}

ULONG
DiNextGuidSequence(VOID)
{
    ULONG Value;

    WaitForSingleObject(DiGlobalMutex, INFINITE);
    Value = static_cast<ULONG>(++DiShared->GuidSequence);
    ReleaseMutex(DiGlobalMutex);
    return Value;
}

VOID
DiLockJoysticks(VOID)
{
    WaitForSingleObject(DiJoyMutex, INFINITE);
}

VOID
DiUnlockJoysticks(VOID)
{
    ReleaseMutex(DiJoyMutex);
}

RX_DI_JOY_SLOT*
DiJoystickSlot(
    _In_ ULONG Id)
{
    return &DiShared->Joysticks[Id];
}

static
RX_DI_OWNER*
DiFindOwner(
    _In_ const GUID* Instance)
{
    for (LONG Index = 0; Index < DiShared->OwnerCount; Index++)
    {
        if (IsEqualGUID(DiShared->Owners[Index].Instance, *Instance))
            return &DiShared->Owners[Index];
    }
    return nullptr;
}

HRESULT
DiAcquireExclusive(
    _In_ const GUID* Instance,
    _In_ HWND Window,
    _In_ DWORD Flags)
{
    RX_DI_OWNER* Owner;
    HRESULT hr = S_OK;

    if ((Flags & DISCL_EXCLUSIVE) == 0)
        return S_OK;

    if (!DiIsWindowOurs(Window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

    WaitForSingleObject(DiGlobalMutex, INFINITE);

    Owner = DiFindOwner(Instance);
    if (Owner != nullptr)
    {
        /* Another foreground owner keeps the device while its window is active */
        if (DiWindowProcess(Owner->Window) == Owner->Process &&
            (Owner->Flags & DISCL_FOREGROUND) != 0 &&
            GetForegroundWindow() != Owner->Window)
        {
            hr = DIERR_OTHERAPPHASPRIO;
        }
    }
    else if (DiShared->OwnerCount >= DI_OWNER_LIMIT)
    {
        hr = E_FAIL;
    }
    else
    {
        Owner = &DiShared->Owners[DiShared->OwnerCount++];
        Owner->Instance = *Instance;
    }

    if (SUCCEEDED(hr))
    {
        Owner->Window = Window;
        Owner->Process = GetCurrentProcessId();
        Owner->Flags = Flags;
    }
    else
    {
        DiTrace(L"exclusive acquire refused, 0x%08lx", hr);
    }

    ReleaseMutex(DiGlobalMutex);
    return hr;
}

VOID
DiReleaseExclusive(
    _In_ const GUID* Instance,
    _In_ HWND Window,
    _In_ DWORD Flags)
{
    RX_DI_OWNER* Owner;

    if ((Flags & DISCL_EXCLUSIVE) == 0)
        return;

    WaitForSingleObject(DiGlobalMutex, INFINITE);

    Owner = DiFindOwner(Instance);
    if (Owner != nullptr && Owner->Window == Window && Owner->Process == GetCurrentProcessId())
    {
        DiShared->OwnerCount -= 1;
        *Owner = DiShared->Owners[DiShared->OwnerCount];
    }

    ReleaseMutex(DiGlobalMutex);
}

static
LRESULT
CALLBACK
DiHookProbeProc(
    _In_ int Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    return CallNextHookEx(DiHookProbe, Code, WParam, LParam);
}

static
VOID
DiProcessAttach(VOID)
{
    DisableThreadLibraryCalls(DiModule);
    InitializeCriticalSection(&DiLock);
    DiLockReady = TRUE;

    /* Low level hooks can be refused, for example in a service session */
    DiHookProbe = SetWindowsHookExW(WH_KEYBOARD_LL, DiHookProbeProc, DiModule, 0);
    DiLowLevelHooks = (DiHookProbe != nullptr);
    if (DiHookProbe != nullptr)
        UnhookWindowsHookEx(DiHookProbe);
    else
        DiTrace(L"low level hooks unavailable, error %lu", GetLastError());

    DiJoyChangedMessage = RegisterWindowMessageW(L"MSJSTICK_VJOYD_MSGSTR");
    DiWinmmEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"DINPUTWINMM");
}

static
VOID
DiProcessDetach(
    _In_ BOOLEAN ProcessExit)
{
    UNREFERENCED_PARAMETER(ProcessExit);

    if (DiShared != nullptr)
        UnmapViewOfFile(DiShared);
    if (DiSharedMapping != nullptr)
        CloseHandle(DiSharedMapping);
    if (DiGlobalMutex != nullptr)
        CloseHandle(DiGlobalMutex);
    if (DiJoyMutex != nullptr)
        CloseHandle(DiJoyMutex);

    DiProcessShutdown();

    if (DiLockReady)
        DeleteCriticalSection(&DiLock);

    if (DiModuleRefs != 0 && DiWinmmEvent != nullptr)
    {
        SetEvent(DiWinmmEvent);
        Sleep(10);
    }
    if (DiWinmmEvent != nullptr)
        CloseHandle(DiWinmmEvent);
}

extern "C"
BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ PVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DiModule = Instance;
        DiProcessAttach();
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        DiProcessDetach(Reserved != nullptr);
    }
    return TRUE;
}

static
HRESULT
DiCreate(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _In_ REFIID Iid,
    _Out_ PVOID* Object,
    _In_opt_ IUnknown* OuterUnknown)
{
    PVOID Created = nullptr;
    HRESULT hr;

    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (!DiLockReady)
        return E_OUTOFMEMORY;

    hr = RxDirectInput::Create(OuterUnknown, (OuterUnknown != nullptr) ? IID_IUnknown : Iid, &Created);
    if (FAILED(hr) || OuterUnknown != nullptr)
        return hr;

    /* Every interface shares the first slots, Initialize included */
    hr = static_cast<IDirectInputW*>(Created)->Initialize(Instance, Version);
    if (FAILED(hr))
    {
        static_cast<IUnknown*>(Created)->Release();
        DiTrace(L"DirectInputCreate version 0x%lx failed, 0x%08lx", Version, hr);
        return hr;
    }

    *Object = Created;
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectInputCreateA(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _Out_ LPDIRECTINPUTA* Object,
    _In_opt_ LPUNKNOWN OuterUnknown)
{
    HRESULT hr;

    InterlockedIncrement(&DiModuleRefs);
    hr = DiCreate(Instance, Version, IID_IDirectInputA, reinterpret_cast<PVOID*>(Object), OuterUnknown);
    InterlockedDecrement(&DiModuleRefs);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectInputCreateW(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _Out_ LPDIRECTINPUTW* Object,
    _In_opt_ LPUNKNOWN OuterUnknown)
{
    HRESULT hr;

    InterlockedIncrement(&DiModuleRefs);
    hr = DiCreate(Instance, Version, IID_IDirectInputW, reinterpret_cast<PVOID*>(Object), OuterUnknown);
    InterlockedDecrement(&DiModuleRefs);
    return hr;
}

extern "C"
HRESULT
WINAPI
DirectInputCreateEx(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _In_ REFIID Iid,
    _Out_ LPVOID* Object,
    _In_opt_ LPUNKNOWN OuterUnknown)
{
    static const IID* const Known[] =
    {
        &IID_IDirectInputA, &IID_IDirectInputW, &IID_IDirectInput2A,
        &IID_IDirectInput2W, &IID_IDirectInput7A, &IID_IDirectInput7W,
    };
    HRESULT hr = E_NOINTERFACE;

    InterlockedIncrement(&DiModuleRefs);
    for (ULONG Index = 0; Index < _countof(Known); Index++)
    {
        if (IsEqualIID(Iid, *Known[Index]))
        {
            hr = DiCreate(Instance, Version, Iid, Object, OuterUnknown);
            break;
        }
    }
    InterlockedDecrement(&DiModuleRefs);
    return hr;
}

extern "C"
HRESULT
WINAPI
DllCanUnloadNow(VOID)
{
    return (DiModuleRefs != 0) ? S_FALSE : S_OK;
}

extern "C"
HRESULT
WINAPI
DllGetClassObject(
    _In_ REFCLSID Class,
    _In_ REFIID Iid,
    _Outptr_ LPVOID* Object)
{
    RxDiFactory* Factory;
    HRESULT hr = CLASS_E_CLASSNOTAVAILABLE;

    if (!DiLockReady)
    {
        *Object = nullptr;
        return E_OUTOFMEMORY;
    }

    InterlockedIncrement(&DiModuleRefs);
    for (ULONG Index = 0; Index < _countof(DiClasses); Index++)
    {
        if (!IsEqualCLSID(Class, *DiClasses[Index].Class))
            continue;

        if (Object == nullptr)
        {
            hr = E_POINTER;
            break;
        }

        *Object = nullptr;
        Factory = new RxDiFactory(DiClasses[Index].Create);
        if (Factory == nullptr)
        {
            hr = E_OUTOFMEMORY;
            break;
        }

        hr = Factory->QueryInterface(Iid, Object);
        Factory->Release();
        break;
    }

    if (hr == CLASS_E_CLASSNOTAVAILABLE)
        *Object = nullptr;
    InterlockedDecrement(&DiModuleRefs);
    return hr;
}

static
HRESULT
DiRegisterClasses(
    _In_ BOOLEAN Register)
{
    WCHAR Module[MAX_PATH];
    WCHAR Path[46];
    WCHAR Name[128];
    HRESULT Result = S_OK;

    GetModuleFileNameW(DiModule, Module, _countof(Module));

    for (ULONG Index = 0; Index < _countof(DiClasses); Index++)
    {
        const GUID* Class = DiClasses[Index].Class;
        HKEY ClassKey = nullptr;
        HKEY ServerKey = nullptr;
        LONG Error;

        StringCbPrintfW(Path, sizeof(Path), L"CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                        Class->Data1, Class->Data2, Class->Data3, Class->Data4[0], Class->Data4[1],
                        Class->Data4[2], Class->Data4[3], Class->Data4[4], Class->Data4[5],
                        Class->Data4[6], Class->Data4[7]);

        Error = Register ? RegCreateKeyW(HKEY_CLASSES_ROOT, Path, &ClassKey)
                         : RegOpenKeyW(HKEY_CLASSES_ROOT, Path, &ClassKey);
        if (Error != ERROR_SUCCESS)
        {
            if (SUCCEEDED(Result))
                Result = HRESULT_FROM_WIN32(Error);
            continue;
        }

        LoadStringW(DiModule, DiClasses[Index].NameId, Name, 127);
        if (Register)
            Error = RegSetValueExW(ClassKey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(Name),
                                   (lstrlenW(Name) + 1) * sizeof(WCHAR));
        else
            Error = RegDeleteValueW(ClassKey, nullptr);
        if (Error != ERROR_SUCCESS && SUCCEEDED(Result))
            Result = HRESULT_FROM_WIN32(Error);

        Error = Register ? RegCreateKeyW(ClassKey, L"InProcServer32", &ServerKey)
                         : RegOpenKeyW(ClassKey, L"InProcServer32", &ServerKey);
        if (Error == ERROR_SUCCESS)
        {
            if (Register)
            {
                Error = RegSetValueExW(ServerKey, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(Module),
                                       (lstrlenW(Module) + 1) * sizeof(WCHAR));
                if (Error != ERROR_SUCCESS && SUCCEEDED(Result))
                    Result = HRESULT_FROM_WIN32(Error);
                Error = RegSetValueExW(ServerKey, L"ThreadingModel", 0, REG_SZ,
                                       reinterpret_cast<const BYTE*>(L"Both"), sizeof(L"Both"));
                if (Error != ERROR_SUCCESS && SUCCEEDED(Result))
                    Result = HRESULT_FROM_WIN32(Error);
                RegCloseKey(ServerKey);
            }
            else
            {
                RegDeleteValueW(ServerKey, nullptr);
                RegDeleteValueW(ServerKey, L"ThreadingModel");
                RegCloseKey(ServerKey);
                Error = RegDeleteKeyW(ClassKey, L"InProcServer32");
            }
        }
        if (Error != ERROR_SUCCESS && SUCCEEDED(Result))
            Result = HRESULT_FROM_WIN32(Error);

        if (Register)
        {
            RegCloseKey(ClassKey);
        }
        else
        {
            DWORD Keys = 0;
            DWORD Values = 0;

            /* Only remove the class key once nothing else lives under it */
            RegQueryInfoKeyW(ClassKey, nullptr, nullptr, nullptr, &Keys, nullptr, nullptr, &Values,
                             nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(ClassKey);
            if (Keys == 0 && Values == 0)
            {
                Error = RegDeleteKeyW(HKEY_CLASSES_ROOT, Path);
                if (Error != ERROR_SUCCESS && SUCCEEDED(Result))
                    Result = HRESULT_FROM_WIN32(Error);
            }
        }
    }

    return Result;
}

extern "C"
HRESULT
WINAPI
DllRegisterServer(VOID)
{
    return DiRegisterClasses(TRUE);
}

extern "C"
HRESULT
WINAPI
DllUnregisterServer(VOID)
{
    return DiRegisterClasses(FALSE);
}
