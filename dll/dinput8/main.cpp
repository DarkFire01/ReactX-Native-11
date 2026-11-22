/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Library entry points, the class factory and state shared between processes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8.hpp"
#include "shared.hpp"

#include <strsafe.h>

HINSTANCE DiInstance;
LONG DiModuleRefs;
CRITICAL_SECTION DiLock;
BOOLEAN DiLockReady;
UINT DiNotifyMessage;
UINT DiJoystickMessage;
DWORD DiEmulation;
HANDLE DiAcquireEvent;
HANDLE DiThreadEvent;
HANDLE DiHidEvent;

/* Cross process state, laid out as every DirectInput 8 library in the session expects */
HANDLE DiGlobalMutex;
HANDLE DiJoystickMutex;
static HANDLE DiSharedMapping;
DI_SHARED_STATE* DiShared;

/* A debug build of the library registered in place of this one takes over creation */
static HMODULE DiDebugModule;

typedef HRESULT (WINAPI* PDI_CREATE8)(HINSTANCE, DWORD, REFIID, VOID**, LPUNKNOWN);

VOID
DiModuleAddRef(VOID)
{
    InterlockedIncrement(&DiModuleRefs);
}

VOID
DiModuleRelease(VOID)
{
    InterlockedDecrement(&DiModuleRefs);
}

BOOLEAN
DiIsUnknown(
    _In_ REFIID Riid)
{
    return IsEqualIID(Riid, IID_IUnknown) ? TRUE : FALSE;
}

HRESULT
DiCheckVersion(
    _In_opt_ HINSTANCE Instance,
    _In_ DWORD Version)
{
    WCHAR Probe[3];

    if (Instance == nullptr || GetModuleFileNameW(Instance, Probe, _countof(Probe)) == 0)
        return E_INVALIDARG;
    if (Version == DI_VERSION)
        return S_OK;
    if (Version == 0)
        return DIERR_NOTINITIALIZED;
    if (Version > DI_VERSION)
        return DIERR_OLDDIRECTINPUTVERSION;
    return DIERR_BETADIRECTINPUTVERSION;
}

/* Object names carry a GUID ending in the "DEST" signature every DirectInput object name uses */
static
VOID
DiFormatObjectName(
    _Out_writes_(Count) PWSTR Name,
    _In_ SIZE_T Count,
    _In_ DWORD First,
    _In_ WORD Second)
{
    StringCchPrintfW(Name, Count, L"DirectInput.{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", First, Second,
                     0x11CF, 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00);
}

static
DWORD
DiReadEmulation(VOID)
{
    HKEY Key;
    DWORD Value = 0;
    DWORD Size = sizeof(Value);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\DirectInput", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return 0;
    if (RegQueryValueExW(Key, L"Emulation", nullptr, nullptr, reinterpret_cast<BYTE*>(&Value), &Size) != ERROR_SUCCESS ||
        Size != sizeof(Value))
    {
        Value = 0;
    }
    RegCloseKey(Key);
    return Value;
}

HRESULT
DiSharedInitialize(VOID)
{
    WCHAR Name[64];
    HRESULT Result = S_OK;

    EnterCriticalSection(&DiLock);
    if (DiGlobalMutex == nullptr)
    {
        DiFormatObjectName(Name, _countof(Name), 0x89521361, 0xAA8A);
        DiGlobalMutex = CreateMutexW(nullptr, TRUE, Name);
        if (DiGlobalMutex == nullptr)
        {
            DiTrace(L"cannot create the global mutex (%lu)", GetLastError());
            Result = E_FAIL;
            goto Exit;
        }
        DiEmulation = DiReadEmulation();
        ReleaseMutex(DiGlobalMutex);
    }
    if (DiShared == nullptr)
    {
        DiFormatObjectName(Name, _countof(Name), 0x5944E681, 0xC92E);
        DiSharedMapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DI_SHARED_SIZE,
                                             Name);
        if (DiSharedMapping == nullptr)
        {
            DiTrace(L"cannot create the shared section (%lu)", GetLastError());
            Result = E_FAIL;
            goto Exit;
        }
        DiShared = static_cast<DI_SHARED_STATE*>(MapViewOfFile(DiSharedMapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 0));
        if (DiShared == nullptr)
        {
            DiTrace(L"cannot map the shared section (%lu)", GetLastError());
            Result = E_FAIL;
            goto Exit;
        }
    }
    if (DiJoystickMutex == nullptr)
    {
        DiFormatObjectName(Name, _countof(Name), 0x5944E682, 0xC92E);
        DiJoystickMutex = CreateMutexW(nullptr, FALSE, Name);
        if (DiJoystickMutex == nullptr)
        {
            DiTrace(L"cannot create the joystick mutex (%lu)", GetLastError());
            Result = E_FAIL;
        }
    }

Exit:
    LeaveCriticalSection(&DiLock);
    return Result;
}

VOID
DiSharedClose(VOID)
{
    if (DiShared != nullptr)
        UnmapViewOfFile(DiShared);
    if (DiSharedMapping != nullptr)
        CloseHandle(DiSharedMapping);
    if (DiGlobalMutex != nullptr)
        CloseHandle(DiGlobalMutex);
    if (DiJoystickMutex != nullptr)
        CloseHandle(DiJoystickMutex);
    DiShared = nullptr;
    DiSharedMapping = nullptr;
    DiGlobalMutex = nullptr;
    DiJoystickMutex = nullptr;
}

HRESULT
DiLaunchControlPanel(
    _In_z_ PCWSTR Applet)
{
    WCHAR Command[MAX_PATH];
    STARTUPINFOW Startup = {};
    PROCESS_INFORMATION Process = {};

    Startup.cb = sizeof(Startup);
    StringCchPrintfW(Command, _countof(Command), L"control.exe %s", Applet);
    if (!CreateProcessW(nullptr, Command, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Startup, &Process))
    {
        DWORD Error = GetLastError();

        DiTrace(L"cannot start the control panel (%lu)", Error);
        return HRESULT_FROM_WIN32(Error);
    }
    CloseHandle(Process.hProcess);
    CloseHandle(Process.hThread);
    return S_OK;
}

/* DiObject */

DiObject::DiObject(
    _In_opt_ IUnknown* Outer)
{
    Inner.Owner = this;
    Controller = Outer != nullptr ? Outer : &Inner;
    Refs = 1;
    DiModuleAddRef();
}

DiObject::~DiObject()
{
    DiModuleRelease();
}

STDMETHODIMP
DiObject::Unknown::QueryInterface(
    _In_ REFIID Riid,
    _Outptr_ VOID** Object)
{
    VOID* Face;

    if (Object == nullptr)
        return E_POINTER;
    *Object = nullptr;

    /* IUnknown names the primary face, as with the system library, even through the inner unknown */
    Face = Owner->FindInterface(Riid);
    if (Face == nullptr)
        return Owner->QueryExtra(Riid, Object);
    *Object = Face;
    Owner->OuterAddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG)
DiObject::Unknown::AddRef()
{
    return InterlockedIncrement(&Owner->Refs);
}

STDMETHODIMP_(ULONG)
DiObject::Unknown::Release()
{
    ULONG Count = InterlockedDecrement(&Owner->Refs);

    if (Count == 0)
        delete Owner;
    return Count;
}

HRESULT
DiFinishCreate(
    _In_ DiObject* Object,
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result)
{
    HRESULT Status;

    if (Outer != nullptr)
    {
        *Result = Object->InnerUnknown();
        return S_OK;
    }
    Status = Object->InnerUnknown()->QueryInterface(Riid, Result);
    Object->InnerUnknown()->Release();
    return Status;
}

/* Exports */

static
HRESULT
DiCreateHelper(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _Out_ VOID** Object,
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid)
{
    IDirectInput8W* Created = nullptr;
    HRESULT Result;

    if (Object == nullptr)
        return E_POINTER;
    *Object = nullptr;
    if (!DiLockReady)
        return E_OUTOFMEMORY;

    /* An aggregated object is made but never handed back, the way the system library behaves */
    Result = DiCreateInput(Outer, Outer != nullptr ? IID_IUnknown : Riid, reinterpret_cast<VOID**>(&Created));
    if (FAILED(Result))
        return Result;
    DiRegisterApplication(Version, FALSE);
    if (Outer != nullptr)
        return Result;

    Result = Created->Initialize(Instance, Version);
    if (FAILED(Result))
    {
        Created->Release();
        *Object = nullptr;
        return Result;
    }
    *Object = Created;
    return Result;
}

/* Hands creation to a registered debug build of the library, if one is installed */
static
HRESULT
DiTryDebugLibrary(
    _In_ HINSTANCE Instance,
    _In_ DWORD Version,
    _In_ REFIID Riid,
    _Out_ VOID** Object,
    _In_opt_ LPUNKNOWN Outer)
{
    static const WCHAR DebugName[] = L"dinput8d.dll";
    WCHAR Key[160];
    WCHAR Path[MAX_PATH];
    HKEY Server;
    LONG Size = sizeof(Path);
    HRESULT Result = E_NOINTERFACE;
    int Tail;

    StringCchPrintfW(Key, _countof(Key), L"CLSID\\{25E609E4-B259-11CF-BFC7-444553540000}\\InProcServer32");
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, Key, 0, KEY_QUERY_VALUE, &Server) != ERROR_SUCCESS)
        return E_NOINTERFACE;
    if (RegQueryValueW(Server, nullptr, Path, &Size) == ERROR_SUCCESS)
    {
        Tail = lstrlenW(Path) - lstrlenW(DebugName);
        if (Tail >= 0 && lstrcmpiW(DebugName, Path + Tail) == 0)
        {
            DiDebugModule = LoadLibraryW(Path);
            if (DiDebugModule != nullptr)
            {
                PDI_CREATE8 Create = reinterpret_cast<PDI_CREATE8>(GetProcAddress(DiDebugModule, "DirectInput8Create"));

                if (Create != nullptr && DiDebugModule != DiInstance)
                    Result = Create(Instance, Version, Riid, Object, Outer);
            }
        }
    }
    RegCloseKey(Server);
    return Result;
}

extern "C"
HRESULT
WINAPI
DirectInput8Create(
    _In_ HINSTANCE hinst,
    _In_ DWORD dwVersion,
    _In_ REFIID riidltf,
    _Out_ LPVOID* ppvOut,
    _In_opt_ LPUNKNOWN punkOuter)
{
    HRESULT Result;

    DiModuleAddRef();
    Result = DiTryDebugLibrary(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    if (Result == E_NOINTERFACE && !IsEqualIID(riidltf, IID_IDirectInputJoyConfig8))
        Result = DiCreateHelper(hinst, dwVersion, ppvOut, punkOuter, riidltf);
    if (FAILED(Result))
        DI_TRACE_ONCE(L"DirectInput8Create version %#lx failed %08lX", dwVersion, Result);
    DiModuleRelease();
    return Result;
}

class DiFactory : public DiObject, public IClassFactory
{
public:
    HRESULT (*Create)(IUnknown*, REFIID, VOID**);

    DiFactory() : DiObject(nullptr), Create(nullptr)
    {
    }

    VOID*
    FindInterface(
        _In_ REFIID Riid) override
    {
        if (IsEqualIID(Riid, IID_IClassFactory) || DiIsUnknown(Riid))
            return static_cast<IClassFactory*>(this);
        return nullptr;
    }

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

    STDMETHODIMP
    CreateInstance(
        _In_opt_ IUnknown* Outer,
        _In_ REFIID Riid,
        _Outptr_ VOID** Object) override
    {
        if (Object == nullptr)
            return E_INVALIDARG;
        *Object = nullptr;
        if (Outer != nullptr)
            return CLASS_E_NOAGGREGATION;
        return Create(nullptr, Riid, Object);
    }

    STDMETHODIMP
    LockServer(
        _In_ BOOL Lock) override
    {
        if (Lock)
            DiModuleAddRef();
        else
            DiModuleRelease();
        return S_OK;
    }
};

static const struct
{
    const CLSID* Class;
    HRESULT (*Create)(IUnknown*, REFIID, VOID**);
    UINT NameId;
} DiClasses[] =
{
    { &CLSID_DirectInput8, DiCreateInput, IDS_DIRECTINPUT8 },
    { &CLSID_DirectInputDevice8, DiCreateDevice, IDS_DIRECTINPUTDEVICE8 },
};

extern "C"
HRESULT
WINAPI
DllGetClassObject(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _Outptr_ LPVOID* ppv)
{
    DiFactory* Factory;
    HRESULT Result = CLASS_E_CLASSNOTAVAILABLE;

    if (!DiLockReady)
    {
        *ppv = nullptr;
        return E_OUTOFMEMORY;
    }
    DiModuleAddRef();
    *ppv = nullptr;
    for (const auto& Entry : DiClasses)
    {
        if (!IsEqualCLSID(rclsid, *Entry.Class))
            continue;
        Factory = new (std::nothrow) DiFactory();
        if (Factory == nullptr)
        {
            Result = E_OUTOFMEMORY;
            break;
        }
        Factory->Create = Entry.Create;
        Result = DiFinishCreate(Factory, nullptr, riid, ppv);
        break;
    }
    DiModuleRelease();
    return Result;
}

extern "C"
HRESULT
WINAPI
DllCanUnloadNow(VOID)
{
    return DiModuleRefs != 0 ? S_FALSE : S_OK;
}

static
HRESULT
DiRegistryResult(
    _In_ LONG Error)
{
    if (Error == ERROR_SUCCESS)
        return S_OK;
    return HRESULT_FROM_WIN32(Error);
}

static
HRESULT
DiSetKeyString(
    _In_ HKEY Key,
    _In_opt_z_ PCWSTR Name,
    _In_z_ PCWSTR Value)
{
    return DiRegistryResult(RegSetValueExW(Key, Name, 0, REG_SZ, reinterpret_cast<const BYTE*>(Value),
                                           (lstrlenW(Value) + 1) * sizeof(WCHAR)));
}

/* Deletes a key once nothing is left below it */
static
HRESULT
DiDeleteIfEmpty(
    _In_ HKEY Parent,
    _In_z_ PCWSTR Name)
{
    HKEY Key;
    DWORD SubKeys = 0;
    DWORD Values = 0;
    LONG Error;

    if (RegOpenKeyExW(Parent, Name, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return S_OK;
    RegQueryInfoKeyW(Key, nullptr, nullptr, nullptr, &SubKeys, nullptr, nullptr, &Values, nullptr, nullptr, nullptr,
                     nullptr);
    RegCloseKey(Key);
    if (SubKeys != 0)
        return S_OK;
    Error = RegDeleteKeyW(Parent, Name);
    return Error == ERROR_SUCCESS ? S_OK : DiRegistryResult(Error);
}

static
HRESULT
DiServerAction(
    _In_ BOOLEAN Register)
{
    WCHAR Module[MAX_PATH];
    HRESULT Result = S_OK;

    GetModuleFileNameW(DiInstance, Module, _countof(Module));
    for (const auto& Entry : DiClasses)
    {
        WCHAR Key[48];
        const GUID* Class = Entry.Class;
        HRESULT Step;

        StringCbPrintfW(Key, sizeof(Key), L"CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", Class->Data1,
                        Class->Data2, Class->Data3, Class->Data4[0], Class->Data4[1], Class->Data4[2], Class->Data4[3],
                        Class->Data4[4], Class->Data4[5], Class->Data4[6], Class->Data4[7]);
        if (Register)
        {
            HKEY ClassKey;
            HKEY Server;
            WCHAR Name[128];

            Step = DiRegistryResult(RegCreateKeyW(HKEY_CLASSES_ROOT, Key, &ClassKey));
            if (FAILED(Step))
            {
                if (SUCCEEDED(Result))
                    Result = Step;
                continue;
            }
            LoadStringW(DiInstance, Entry.NameId, Name, _countof(Name));
            Step = DiSetKeyString(ClassKey, nullptr, Name);
            if (FAILED(Step) && SUCCEEDED(Result))
                Result = Step;
            Step = DiRegistryResult(RegCreateKeyW(ClassKey, L"InProcServer32", &Server));
            if (SUCCEEDED(Step))
            {
                Step = DiSetKeyString(Server, nullptr, Module);
                if (FAILED(Step) && SUCCEEDED(Result))
                    Result = Step;
                Step = DiSetKeyString(Server, L"ThreadingModel", L"Both");
                if (FAILED(Step) && SUCCEEDED(Result))
                    Result = Step;
                RegCloseKey(Server);
            }
            else if (SUCCEEDED(Result))
            {
                Result = Step;
            }
            RegCloseKey(ClassKey);
        }
        else
        {
            HKEY ClassKey;

            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, Key, 0, KEY_ALL_ACCESS, &ClassKey) != ERROR_SUCCESS)
                continue;
            Step = DiDeleteIfEmpty(ClassKey, L"InProcServer32");
            if (FAILED(Step) && SUCCEEDED(Result))
                Result = Step;
            RegCloseKey(ClassKey);
            Step = DiDeleteIfEmpty(HKEY_CLASSES_ROOT, Key);
            if (FAILED(Step) && SUCCEEDED(Result))
                Result = Step;
        }
    }
    return Result;
}

extern "C"
HRESULT
WINAPI
DllRegisterServer(VOID)
{
    return DiServerAction(TRUE);
}

extern "C"
HRESULT
WINAPI
DllUnregisterServer(VOID)
{
    return DiServerAction(FALSE);
}

extern "C"
LPCDIDATAFORMAT
WINAPI
GetdfDIJoystick(VOID)
{
    return &DiJoystickFormat;
}

static
VOID
DiProcessDetach(
    _In_ BOOLEAN Terminating)
{
    UNREFERENCED_PARAMETER(Terminating);

    if (DiAcquireEvent != nullptr)
        CloseHandle(DiAcquireEvent);
    if (DiThreadEvent != nullptr)
        CloseHandle(DiThreadEvent);
    if (DiHidEvent != nullptr)
        CloseHandle(DiHidEvent);
    DiSharedClose();
    if (DiLockReady)
        DeleteCriticalSection(&DiLock);
    DiLockReady = FALSE;
    if (DiDebugModule != nullptr)
        FreeLibrary(DiDebugModule);
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
        DiInstance = Instance;
        DisableThreadLibraryCalls(Instance);
        InitializeCriticalSection(&DiLock);
        DiLockReady = TRUE;
        DiJoystickMessage = RegisterWindowMessageW(L"MSJSTICK_VJOYD_MSGSTR");
        DiNotifyMessage = RegisterWindowMessageW(L"DIRECTINPUT_NOTIFICATION_MSGSTRING");
        DiAcquireEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DiThreadEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        DiHidEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (DiAcquireEvent == nullptr || DiThreadEvent == nullptr || DiHidEvent == nullptr)
        {
            DiTrace(L"cannot create the library events (%lu)", GetLastError());
            return FALSE;
        }
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        DiProcessDetach(Reserved != nullptr);
    }
    return TRUE;
}
