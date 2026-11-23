/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry points and the compatibility shim switches
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"
#include "shadervalidator.hpp"

#include <sddl.h>
#include <aclapi.h>

extern const WCHAR RxTraceModule[] = L"d3d9";

D9Globals D9State;

/* The newest header version this runtime implements; 31 is the one older version still accepted */
static constexpr UINT D9SdkVersion = 32;
static constexpr UINT D9SdkVersionOld = 31;
static constexpr UINT D9SdkVersionLastRejected = 131;

/* Debug runtime request bit applications may or into the version */
static constexpr UINT D9SdkDebugFlag = 0x80000000;

/* Opens or creates one of the exclusive mode mutexes DirectDraw and Direct3D share across a session */
static
HANDLE
D9CreateSessionMutex(
    _In_z_ PCWSTR Name)
{
    PSECURITY_DESCRIPTOR Descriptor = nullptr;
    SECURITY_ATTRIBUTES Attributes;
    PSID LowLabel = nullptr;
    HANDLE Mutex;
    ACL* Sacl;
    BYTE SaclBuffer[64];
    DWORD Error = ERROR_INVALID_PARAMETER;

    /* Everyone gets query, synchronize and read control */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(A;;0x120001;;;WD)", SDDL_REVISION_1,
                                                              &Descriptor, nullptr))
    {
        return OpenMutexW(SYNCHRONIZE, FALSE, Name);
    }

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = Descriptor;
    Attributes.bInheritHandle = TRUE;
    Mutex = CreateMutexW(&Attributes, FALSE, Name);
    LocalFree(Descriptor);
    if (Mutex == NULL)
        return OpenMutexW(SYNCHRONIZE, FALSE, Name);

    /* Low integrity processes must be able to wait on it too */
    if (ConvertStringSidToSidW(L"LW", &LowLabel))
    {
        Sacl = reinterpret_cast<ACL*>(SaclBuffer);
        if (InitializeAcl(Sacl, sizeof(SaclBuffer), ACL_REVISION) &&
            AddMandatoryAce(Sacl, ACL_REVISION, 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, LowLabel))
        {
            Error = SetSecurityInfo(Mutex, SE_KERNEL_OBJECT, LABEL_SECURITY_INFORMATION, nullptr, nullptr,
                                    nullptr, Sacl);
        }
        LocalFree(LowLabel);
    }

    if (Error != ERROR_SUCCESS)
    {
        CloseHandle(Mutex);
        return NULL;
    }
    return Mutex;
}

static
HRESULT
D9CreateExclusiveModeMutexes(VOID)
{
    HRESULT hr = S_OK;

    EnterCriticalSection(&D9State.Lock);
    if (D9State.ExclusiveMode == NULL)
    {
        D9State.ExclusiveMode = D9CreateSessionMutex(L"Local\\__DDrawExclMode__");
        D9State.CheckExclusiveMode = D9CreateSessionMutex(L"Local\\__DDrawCheckExclMode__");
        if (D9State.ExclusiveMode == NULL || D9State.CheckExclusiveMode == NULL)
        {
            D9Trace(L"cannot create the exclusive mode mutexes (%lu)", GetLastError());
            if (D9State.ExclusiveMode != NULL)
                CloseHandle(D9State.ExclusiveMode);
            if (D9State.CheckExclusiveMode != NULL)
                CloseHandle(D9State.CheckExclusiveMode);
            D9State.ExclusiveMode = NULL;
            D9State.CheckExclusiveMode = NULL;
            hr = E_FAIL;
        }
    }
    LeaveCriticalSection(&D9State.Lock);
    return hr;
}

/* Header versions from before Direct3D 9.0c, and the range reserved after it, are refused */
static
BOOLEAN
D9IsSdkVersionAccepted(
    _In_ UINT SdkVersion)
{
    UINT Version = SdkVersion & ~D9SdkDebugFlag;

    if (Version == D9SdkVersionOld)
        return TRUE;
    return Version >= D9SdkVersion && (Version == D9SdkVersion || Version > D9SdkVersionLastRejected);
}

static
HRESULT
D9CreateObject(
    _In_ UINT SdkVersion,
    _In_ BOOLEAN IsEx,
    _Outptr_result_maybenull_ IDirect3D9Ex** Result,
    _In_reads_opt_(OverrideCount) const D3D9ON12_ARGS* Overrides,
    _In_ UINT OverrideCount)
{
    SYSTEM_INFO System = {};
    CHAR Message[256];
    D9Enum* Object;
    HRESULT hr;

    *Result = nullptr;
    if (FAILED(D9CreateExclusiveModeMutexes()))
        return E_OUTOFMEMORY;

    GetNativeSystemInfo(&System);
    if (System.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64)
        return D3DERR_NOTAVAILABLE;

    if (!D9IsSdkVersionAccepted(SdkVersion))
    {
        StringCbPrintfA(Message, sizeof(Message),
                        "\nD3D ERROR: D3D header version mismatch.\n"
                        "The application was compiled against and will only work with D3D_SDK_VERSION (%d), "
                        "but the currently installed runtime is version (%d).\n"
                        "Recompile the application against the appropriate SDK for the installed runtime.\n\n",
                        SdkVersion & ~D9SdkDebugFlag, D9SdkVersion);
        OutputDebugStringA(Message);
        return D3DERR_NOTAVAILABLE;
    }

    Object = new (std::nothrow) D9Enum(SdkVersion, IsEx);
    if (Object == nullptr)
        return E_OUTOFMEMORY;

    hr = Object->Initialize(Overrides, OverrideCount);
    if (FAILED(hr))
    {
        Object->Release();
        return hr;
    }

    /* The extended object needs a display driver of the Vista driver model or later */
    if (IsEx && !D9State.HasWddmDriver)
    {
        D9Trace(L"Direct3DCreate9Ex refused: no display has a WDDM driver");
        Object->Release();
        return D3DERR_NOTAVAILABLE;
    }

    *Result = Object;
    return S_OK;
}

IDirect3D9*
WINAPI
Direct3DCreate9(
    _In_ UINT SdkVersion)
{
    IDirect3D9Ex* Object;

    D9CreateObject(SdkVersion, FALSE, &Object, nullptr, 0);
    return Object;
}

HRESULT
WINAPI
Direct3DCreate9Ex(
    _In_ UINT SdkVersion,
    _Outptr_ IDirect3D9Ex** Object)
{
    return D9CreateObject(SdkVersion, TRUE, Object, nullptr, 0);
}

IDirect3D9*
WINAPI
Direct3DCreate9On12(
    _In_ UINT SdkVersion,
    _In_reads_opt_(OverrideCount) D3D9ON12_ARGS* Overrides,
    _In_ UINT OverrideCount)
{
    IDirect3D9Ex* Object;

    D9CreateObject(SdkVersion, FALSE, &Object, Overrides, OverrideCount);
    return Object;
}

HRESULT
WINAPI
Direct3DCreate9On12Ex(
    _In_ UINT SdkVersion,
    _In_reads_opt_(OverrideCount) D3D9ON12_ARGS* Overrides,
    _In_ UINT OverrideCount,
    _Outptr_ IDirect3D9Ex** Object)
{
    return D9CreateObject(SdkVersion, TRUE, Object, Overrides, OverrideCount);
}

/* Remembers that this executable wants the maximized windowed mode shim from now on */
static
VOID
D9RecordMaximizedWindowedMode(VOID)
{
    CHAR Path[MAX_PATH] = {};
    DWORD Enabled = 1;
    HKEY Key;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Direct3D\\Shims\\MaximizedWindowedMode", 0,
                        nullptr, 0, KEY_ALL_ACCESS, nullptr, &Key, nullptr) != ERROR_SUCCESS)
    {
        return;
    }

    if (GetModuleFileNameA(NULL, Path, _countof(Path)) < _countof(Path))
        RegSetValueExA(Key, Path, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&Enabled), sizeof(Enabled));
    RegCloseKey(Key);
}

/* Moves a tri-state shim switch and makes the runtime decide its shims again */
static
BOOLEAN
D9SetShimSwitch(
    _Inout_ LONG* Switch,
    _In_ BOOL Enable)
{
    if (*Switch != 0 && (*Switch == 1) == (Enable != FALSE))
        return FALSE;

    *Switch = Enable ? 1 : 2;
    D9State.ShimDecisionsMade = FALSE;
    D9State.ShimDecisionsApplied = FALSE;
    return TRUE;
}

extern "C"
VOID
WINAPI
Direct3D9SetMaximizedWindowedModeShim(
    _In_ BOOL Persist,
    _In_ BOOL Enable)
{
    LONG Previous = D9State.MaximizedWindowedMode;

    if (D9SetShimSwitch(&D9State.MaximizedWindowedMode, Enable) && Enable && Persist)
        D9RecordMaximizedWindowedMode();
    if (Previous != D9State.MaximizedWindowedMode)
        D9Trace(L"maximized windowed mode shim %s", Enable ? L"on" : L"off");
}

extern "C"
VOID
WINAPI
Direct3D9EnableMaximizedWindowedModeShim(
    _In_ BOOL Persist)
{
    Direct3D9SetMaximizedWindowedModeShim(Persist, TRUE);
}

extern "C"
VOID
WINAPI
Direct3D9SetSwapEffectUpgradeShim(
    _In_ BOOL Enable)
{
    if (D9SetShimSwitch(&D9State.SwapEffectUpgrade, Enable))
        D9Trace(L"swap effect upgrade shim %s", Enable ? L"on" : L"off");
}

/* 0 follows the driver, 1 forces the mapping layer, 2 forbids it; other values are ignored */
extern "C"
VOID
WINAPI
Direct3D9Force9on12(
    _In_ UINT Mode)
{
    if (Mode > 2)
        return;
    D9State.Force9On12 = static_cast<LONG>(Mode);
}

extern "C"
VOID
WINAPI
Direct3D9SetVendorIDLieFor9on12(
    _In_ BOOL Enable,
    _In_ DWORD VendorId)
{
    D9State.VendorIdLie = Enable != FALSE;
    D9State.FakeVendorId = VendorId;
}

extern "C"
VOID
WINAPI
Direct3D9ForceHybridEnumeration(
    _In_ UINT Mode)
{
    D9State.HybridEnumeration = Mode;
}

extern "C"
VOID
WINAPI
Direct3D9SetMaximizedWindowHwndOverride(
    _In_ BOOL Enable)
{
    D9State.MaximizedHwndOverride = Enable != FALSE;
}

/* The standalone shader validator; null when out of memory */
extern "C"
VOID*
WINAPI
Direct3DShaderValidatorCreate9(VOID)
{
    return D9CreateShaderValidatorObject();
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ VOID* Reserved)
{
    switch (Reason)
    {
    case DLL_PROCESS_ATTACH:
        D9State.Module = Instance;
        InitializeCriticalSection(&D9State.Lock);
        DisableThreadLibraryCalls(Instance);
        break;

    case DLL_PROCESS_DETACH:
        if (Reserved != nullptr)
            break;
        if (D9State.ExclusiveMode != NULL)
            CloseHandle(D9State.ExclusiveMode);
        if (D9State.CheckExclusiveMode != NULL)
            CloseHandle(D9State.CheckExclusiveMode);
        DeleteCriticalSection(&D9State.Lock);
        break;
    }
    return TRUE;
}
