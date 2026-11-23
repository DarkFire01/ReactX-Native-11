/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel thunks taken from win32u, with gdi32 as the fallback
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Every kernel graphics call takes a single argument block */
typedef NTSTATUS (APIENTRY* PFN_DC_KMT)(_Inout_ void* Arguments);

/* Each entry is optional, so one binary runs on every release it targets */
struct DcKmtThunks
{
    PFN_DC_KMT EnumAdapters3;
    PFN_DC_KMT EnumAdapters2;
    PFN_DC_KMT QueryAdapterInfo;
    PFN_DC_KMT CloseAdapter;
    PFN_DC_KMT QueryStatistics;
    PFN_DC_KMT QueryVideoMemoryInfo;
    PFN_DC_KMT ChangeVideoMemoryReservation;
    PFN_DC_KMT IsFeatureEnabled;
    PFN_DC_KMT EnumProcesses;
    PFN_DC_KMT GetProperties;
    PFN_DC_KMT Escape;

    /* The system call forms the legacy exports translate into */
    PFN_DC_KMT CreateAllocation;
    PFN_DC_KMT OpenResource;
    PFN_DC_KMT CreateSynchronizationObject;
    PFN_DC_KMT WaitForSynchronizationObject;
    PFN_DC_KMT SignalSynchronizationObject;
    PFN_DC_KMT SetVidPnSourceOwner;
    PFN_DC_KMT NetDispStartMiracastDisplayDevice;
    PFN_DC_KMT PinResources;
    PFN_DC_KMT UnpinResources;

    /* The gdi32 exports used when win32u does not have a system call form */
    PFN_DC_KMT LegacyCreateAllocation;
    PFN_DC_KMT LegacyOpenResource;
    PFN_DC_KMT LegacyCreateSynchronizationObject;
    PFN_DC_KMT LegacyWaitForSynchronizationObject;
    PFN_DC_KMT LegacySignalSynchronizationObject;
    PFN_DC_KMT LegacySetVidPnSourceOwner;
    PFN_DC_KMT LegacySetVidPnSourceOwner1;
    PFN_DC_KMT LegacyNetDispStartMiracastDisplayDevice;
    PFN_DC_KMT LegacyNetDispStartMiracastDisplayDevice2;
    PFN_DC_KMT LegacyPinDirectFlipResources;
    PFN_DC_KMT LegacyUnpinDirectFlipResources;
};

_Ret_notnull_
const DcKmtThunks*
DcKmt(VOID);

/* Calls a thunk, or reports STATUS_NOT_IMPLEMENTED when it is missing */
NTSTATUS
DcKmtCall(
    _In_opt_ PFN_DC_KMT Thunk,
    _Inout_ void* Arguments);

/* Fills a D3DKMT_QUERYADAPTERINFO and makes the call */
NTSTATUS
DcQueryAdapterInfo(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Out_writes_bytes_(Size) void* Data,
    _In_ UINT Size);

/* Turns a failed kernel status into the HRESULT the system hands back */
static
inline
HRESULT
DcStatusHresult(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return S_OK;

    return static_cast<HRESULT>(static_cast<ULONG>(Status) | 0x10000000);
}
