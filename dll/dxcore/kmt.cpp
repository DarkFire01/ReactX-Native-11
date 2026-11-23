/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel thunks taken from win32u, with gdi32 as the fallback
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

static INIT_ONCE DcKmtOnce = INIT_ONCE_STATIC_INIT;
static DcKmtThunks DcKmtTable;

static
PFN_DC_KMT
DcResolve(
    _In_opt_ HMODULE Module,
    _In_z_ PCSTR Name)
{
    if (Module == NULL)
        return nullptr;

    return reinterpret_cast<PFN_DC_KMT>(GetProcAddress(Module, Name));
}

/* Takes the win32u system call when there is one, else the gdi32 export */
static
PFN_DC_KMT
DcResolvePair(
    _In_opt_ HMODULE Win32u,
    _In_z_ PCSTR SystemName,
    _In_opt_ HMODULE Gdi32,
    _In_z_ PCSTR GdiName)
{
    PFN_DC_KMT Thunk = DcResolve(Win32u, SystemName);

    if (Thunk == nullptr)
        Thunk = DcResolve(Gdi32, GdiName);

    return Thunk;
}

static
BOOL
CALLBACK
DcLoadKmt(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Out_opt_ PVOID* Context)
{
    HMODULE Win32u = LoadLibraryExW(L"win32u.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE Gdi32 = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    DcKmtThunks& T = DcKmtTable;

    if (Win32u == NULL && Gdi32 == NULL)
        DcTrace(L"neither win32u.dll nor gdi32.dll loaded (%lu)", GetLastError());

    T.EnumAdapters3 = DcResolvePair(Win32u, "NtDxgkEnumAdapters3", Gdi32, "D3DKMTEnumAdapters3");
    T.EnumAdapters2 = DcResolvePair(Win32u, "NtGdiDdDDIEnumAdapters2", Gdi32, "D3DKMTEnumAdapters2");
    T.QueryAdapterInfo = DcResolvePair(Win32u, "NtGdiDdDDIQueryAdapterInfo", Gdi32, "D3DKMTQueryAdapterInfo");
    T.CloseAdapter = DcResolvePair(Win32u, "NtGdiDdDDICloseAdapter", Gdi32, "D3DKMTCloseAdapter");
    T.QueryStatistics = DcResolvePair(Win32u, "NtGdiDdDDIQueryStatistics", Gdi32, "D3DKMTQueryStatistics");
    T.QueryVideoMemoryInfo = DcResolvePair(Win32u, "NtGdiDdDDIQueryVideoMemoryInfo", Gdi32, "D3DKMTQueryVideoMemoryInfo");
    T.ChangeVideoMemoryReservation = DcResolvePair(Win32u, "NtGdiDdDDIChangeVideoMemoryReservation",
                                                   Gdi32, "D3DKMTChangeVideoMemoryReservation");
    T.IsFeatureEnabled = DcResolvePair(Win32u, "NtDxgkIsFeatureEnabled", Gdi32, "D3DKMTIsFeatureEnabled");
    T.EnumProcesses = DcResolvePair(Win32u, "NtDxgkEnumProcesses", Gdi32, "D3DKMTEnumProcesses");
    T.GetProperties = DcResolvePair(Win32u, "NtDxgkGetProperties", Gdi32, "D3DKMTGetProperties");
    T.Escape = DcResolvePair(Win32u, "NtGdiDdDDIEscape", Gdi32, "D3DKMTEscape");

    T.CreateAllocation = DcResolve(Win32u, "NtGdiDdDDICreateAllocation");
    T.OpenResource = DcResolve(Win32u, "NtGdiDdDDIOpenResource");
    T.CreateSynchronizationObject = DcResolve(Win32u, "NtGdiDdDDICreateSynchronizationObject");
    T.WaitForSynchronizationObject = DcResolve(Win32u, "NtGdiDdDDIWaitForSynchronizationObject");
    T.SignalSynchronizationObject = DcResolve(Win32u, "NtGdiDdDDISignalSynchronizationObject");
    T.SetVidPnSourceOwner = DcResolve(Win32u, "NtGdiDdDDISetVidPnSourceOwner");
    T.NetDispStartMiracastDisplayDevice = DcResolve(Win32u, "NtGdiDdDDINetDispStartMiracastDisplayDevice");
    T.PinResources = DcResolve(Win32u, "NtDxgkPinResources");
    T.UnpinResources = DcResolve(Win32u, "NtDxgkUnpinResources");

    T.LegacyCreateAllocation = DcResolve(Gdi32, "D3DKMTCreateAllocation");
    T.LegacyOpenResource = DcResolve(Gdi32, "D3DKMTOpenResource");
    T.LegacyCreateSynchronizationObject = DcResolve(Gdi32, "D3DKMTCreateSynchronizationObject");
    T.LegacyWaitForSynchronizationObject = DcResolve(Gdi32, "D3DKMTWaitForSynchronizationObject");
    T.LegacySignalSynchronizationObject = DcResolve(Gdi32, "D3DKMTSignalSynchronizationObject");
    T.LegacySetVidPnSourceOwner = DcResolve(Gdi32, "D3DKMTSetVidPnSourceOwner");
    T.LegacySetVidPnSourceOwner1 = DcResolve(Gdi32, "D3DKMTSetVidPnSourceOwner1");
    T.LegacyNetDispStartMiracastDisplayDevice = DcResolve(Gdi32, "D3DKMTNetDispStartMiracastDisplayDevice");
    T.LegacyNetDispStartMiracastDisplayDevice2 = DcResolve(Gdi32, "D3DKMTNetDispStartMiracastDisplayDevice2");
    T.LegacyPinDirectFlipResources = DcResolve(Gdi32, "D3DKMTPinDirectFlipResources");
    T.LegacyUnpinDirectFlipResources = DcResolve(Gdi32, "D3DKMTUnpinDirectFlipResources");

    return TRUE;
}

const DcKmtThunks*
DcKmt(VOID)
{
    InitOnceExecuteOnce(&DcKmtOnce, DcLoadKmt, nullptr, nullptr);
    return &DcKmtTable;
}

NTSTATUS
DcKmtCall(
    _In_opt_ PFN_DC_KMT Thunk,
    _Inout_ void* Arguments)
{
    if (Thunk == nullptr)
        return STATUS_NOT_IMPLEMENTED;

    return Thunk(Arguments);
}

NTSTATUS
DcQueryAdapterInfo(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Out_writes_bytes_(Size) void* Data,
    _In_ UINT Size)
{
    D3DKMT_QUERYADAPTERINFO Query = {};

    Query.hAdapter = Adapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return DcKmtCall(DcKmt()->QueryAdapterInfo, &Query);
}
