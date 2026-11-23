/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Process wide lock, display names and registry switches
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

extern const WCHAR RxTraceModule[] = L"ddraw";

DdGlobals DdState;

/* gdi32 bumps this whenever the display settings change */
typedef ULONG (WINAPI* PDD_QUERY_UNIQUENESS)(VOID);

static PDD_QUERY_UNIQUENESS DdQueryUniqueness;

UINT
DdQueryDisplayUniqueness(VOID)
{
    if (DdQueryUniqueness == nullptr)
        return 0;
    return DdQueryUniqueness();
}

VOID
DdEnterLock(VOID)
{
    UINT Uniqueness;

    EnterCriticalSection(&DdState.Lock);
    DdState.Recursion++;

    Uniqueness = DdQueryDisplayUniqueness();
    if ((Uniqueness != DdState.DisplayUniqueness || DdState.DeviceLost) && DdState.Recursion == 1)
    {
        DdTrace(L"display settings uniqueness %u -> %u, device lost %d: resetting objects", DdState.DisplayUniqueness,
                Uniqueness, DdState.DeviceLost);
        DdState.RestoreFailed = FALSE;
        DdState.Restoring = TRUE;
        DdResetAllObjects(TRUE);
        DdState.Restoring = FALSE;
        DdState.DisplayUniqueness = Uniqueness;
        if (!DdState.RestoreFailed)
            DdState.DeviceLost = FALSE;
    }
}

VOID
DdLeaveLock(VOID)
{
    DdState.Recursion -= 1;
    LeaveCriticalSection(&DdState.Lock);
}

DdLockScope::DdLockScope()
{
    DdEnterLock();
}

DdLockScope::~DdLockScope()
{
    DdLeaveLock();
}

BOOLEAN
DdEnumDisplayDevice(
    _In_ DWORD Index,
    _Out_ DISPLAY_DEVICEA* Device)
{
    ZeroMemory(Device, sizeof(*Device));
    Device->cb = sizeof(*Device);
    return EnumDisplayDevicesA(NULL, Index, Device, 0) != FALSE;
}

VOID
DdRefreshPrimaryName(VOID)
{
    DISPLAY_DEVICEA Device;
    DWORD Index;

    for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
    {
        if ((Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0)
        {
            strncpy_s(DdState.PrimaryDisplay, Device.DeviceName, _TRUNCATE);
            return;
        }
    }

    strcpy_s(DdState.PrimaryDisplay, "DISPLAY");
}

BOOLEAN
DdIsMultiMonitor(VOID)
{
    DISPLAY_DEVICEA Device;
    DWORD Index;
    DWORD Count = 0;

    for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
    {
        if ((Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) == 0 && ++Count > 1)
            return TRUE;
    }

    return FALSE;
}

_Ret_maybenull_
HDC
DdCreateDisplayDc(
    _In_z_ PCSTR Name)
{
    if (GetSystemMetrics(SM_CMONITORS) > 1 && _stricmp(Name, "display") == 0)
    {
        DdRefreshPrimaryName();
        Name = DdState.PrimaryDisplay;
    }

    /* Device paths go in the device argument, the old "display" name goes in the driver one */
    if (Name[0] == '\\' && Name[1] == '\\' && Name[2] == '.')
        return CreateDCA(NULL, Name, NULL, NULL);

    return CreateDCA(Name, NULL, NULL, NULL);
}

static
BOOLEAN
DdReadFlag(
    _In_ HKEY Key,
    _In_z_ PCSTR Name,
    _Out_opt_ DWORD* Value)
{
    DWORD Data = 0;
    DWORD Size = sizeof(Data);
    DWORD Type = 0;

    if (Value != nullptr)
        *Value = 0;

    if (RegQueryValueExA(Key, Name, NULL, &Type, reinterpret_cast<BYTE*>(&Data), &Size) != ERROR_SUCCESS || Data == 0)
        return FALSE;

    if (Value != nullptr)
        *Value = Data;
    return TRUE;
}

VOID
DdReadRegistry(VOID)
{
    static const struct
    {
        PCSTR Name;
        ULONG Flag;
    } Switches[] =
    {
        { "ModeXOnly", DdRegModeXOnly },
        { "EmulationOnly", DdRegEmulationOnly },
        { "ShowFrameRate", DdRegShowFrameRate },
        { "EnablePrintScreen", DdRegEnablePrintScreen },
        { "ForceAGPSupport", DdRegForceAgp },
        { "DisableAGPSupport", DdRegDisableAgp },
        { "DisableMMX", DdRegDisableMmx },
        { "DisableDDSCAPSInDDSD", DdRegDisableCapsInDesc },
        { "DisableWiderSurfaces", DdRegDisableWiderSurfaces },
        { "UseNonLocalVidMem", DdRegUseNonLocalVidMem },
    };
    HKEY Key;
    DWORD Value;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\DirectDraw", 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        DdState.RegFlags &= 0xFFFF7190;
        for (const auto& Switch : Switches)
        {
            if (DdReadFlag(Key, Switch.Name, nullptr))
                DdState.RegFlags |= Switch.Flag;
        }

        if (DdReadFlag(Key, "ForceRefreshRate", &Value))
        {
            DdState.RegFlags |= DdRegForceRefreshRate;
            DdState.ForcedRefreshRate = Value;
        }
        RegCloseKey(Key);
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        DdState.RegFlags &= ~DdRegFlipNoVsync;
        if (DdReadFlag(Key, "FlipNoVsync", nullptr))
            DdState.RegFlags |= DdRegFlipNoVsync;
        RegCloseKey(Key);
    }
}

struct DdMonitorSearch
{
    PCSTR DeviceName;
    HMONITOR Found;
};

static
BOOL
CALLBACK
DdMatchMonitor(
    _In_ HMONITOR Monitor,
    _In_ HDC Dc,
    _In_ LPRECT Rect,
    _In_ LPARAM Context)
{
    DdMonitorSearch* Search = reinterpret_cast<DdMonitorSearch*>(Context);
    MONITORINFOEXA Info = {};

    Info.cbSize = sizeof(Info);
    if (GetMonitorInfoA(Monitor, &Info) && _stricmp(Info.szDevice, Search->DeviceName) == 0)
    {
        Search->Found = Monitor;
        return FALSE;
    }

    return TRUE;
}

_Ret_maybenull_
HMONITOR
DdMonitorFromDevice(
    _In_z_ PCSTR DeviceName)
{
    DdMonitorSearch Search = { DeviceName, NULL };

    EnumDisplayMonitors(NULL, NULL, DdMatchMonitor, reinterpret_cast<LPARAM>(&Search));
    return Search.Found;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    HMODULE Gdi;

    if (Reason == DLL_PROCESS_ATTACH)
    {
        DdState.Module = Instance;
        if (!InitializeCriticalSectionAndSpinCount(&DdState.Lock, 0))
            return FALSE;

        Gdi = GetModuleHandleW(L"gdi32.dll");
        if (Gdi != NULL)
            DdQueryUniqueness = reinterpret_cast<PDD_QUERY_UNIQUENESS>(GetProcAddress(Gdi, "GdiEntry13"));

        DisableThreadLibraryCalls(Instance);
        DdRefreshPrimaryName();
        DdState.DisplayUniqueness = DdQueryDisplayUniqueness();
    }
    else if (Reason == DLL_PROCESS_DETACH && Reserved == nullptr)
    {
        DeleteCriticalSection(&DdState.Lock);
    }

    return TRUE;
}
