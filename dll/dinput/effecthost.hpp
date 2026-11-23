/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What force feedback effects need from the device that created them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <dinput.h>

/* How ConvertObjects reads and writes objects, combined with the wanted type bits */
#define DI_CONVERT_FROM_ID      0x00000100
#define DI_CONVERT_FROM_OFFSET  0x00000200
#define DI_CONVERT_TO_ID        0x00001000
#define DI_CONVERT_TO_OFFSET    0x00002000

/* SetForceFeedbackState command the runtime sends when a device is acquired */
#define DI_FF_ACQUIRE           0x80000000

/* What the device hands the effect shepherd to name itself */
typedef struct _RX_DI_FF_HANDLE
{
    DWORD Id;
    DWORD Tag;
} RX_DI_FF_HANDLE;

/* Per device go between for the force feedback driver */
class RxDiShepherd
{
public:
    virtual ~RxDiShepherd() {}

    virtual HRESULT DeviceID(_In_ DWORD ExternalId, _In_ DWORD Begin, _In_ PVOID Info) = 0;
    virtual HRESULT GetVersions(_Inout_ LPDIDRIVERVERSIONS Versions) = 0;
    virtual HRESULT Escape(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIEFFESCAPE Request) = 0;
    virtual HRESULT DeviceEscape(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIEFFESCAPE Request) = 0;
    virtual HRESULT SetGain(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Gain) = 0;
    virtual HRESULT SetForceFeedbackState(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Command) = 0;
    virtual HRESULT GetForceFeedbackState(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIDEVICESTATE State) = 0;
    virtual HRESULT DownloadEffect(_Inout_ RX_DI_FF_HANDLE* Handle, _In_ DWORD EffectId, _In_ LPCDIEFFECT Params, _In_ DWORD Flags) = 0;
    virtual HRESULT DestroyEffect(_Inout_ RX_DI_FF_HANDLE* Handle) = 0;
    virtual HRESULT StartEffect(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Mode, _In_ DWORD Count) = 0;
    virtual HRESULT StopEffect(_In_ RX_DI_FF_HANDLE* Handle) = 0;
    virtual HRESULT GetEffectStatus(_In_ RX_DI_FF_HANDLE* Handle, _Out_ LPDWORD Status) = 0;
    virtual HRESULT SetGlobalGain(_In_ DWORD Gain) = 0;
};

/* One effect a force feedback driver lists in the registry */
struct RX_DI_EFFECT_ENTRY
{
    DWORD Id;
    DWORD EffectType;
    DWORD StaticParams;
    DWORD DynamicParams;
    DWORD Coordinates;
    GUID Guid;
    WCHAR Name[MAX_PATH];
};

static_assert(sizeof(RX_DI_EFFECT_ENTRY) == 556, "registry effect record");

/* The device side of an effect, whichever DirectInput version the device speaks */
class RxDiEffectHost
{
public:
    /* The device lock, which nests */
    virtual VOID EnterHost(VOID) = 0;
    virtual VOID LeaveHost(VOID) = 0;

    /* Keeps the device's memory around while an effect points at it */
    virtual VOID HoldHost(VOID) = 0;
    virtual VOID UnholdHost(VOID) = 0;

    virtual DWORD HostVersion(VOID) = 0;

    /* S_OK while the device is acquired exclusively, otherwise the error an effect reports */
    virtual HRESULT CheckExclusive(VOID) = 0;

    virtual RxDiShepherd* HostDriver(VOID) = 0;
    virtual DWORD HostDriverTag(VOID) = 0;

    /* Copies the registry record of an effect the driver offers */
    virtual BOOLEAN FindEffect(_In_ REFGUID Guid, _Out_ RX_DI_EFFECT_ENTRY* Entry) = 0;

    /* The list of created effects the device walks and lets go of */
    virtual HRESULT ListEffect(_In_ PVOID Effect) = 0;
    virtual BOOLEAN UnlistEffect(_In_ PVOID Effect) = 0;

    /* Turns object ids or data format offsets into ids or offsets of objects with the wanted type */
    virtual HRESULT ConvertObjects(_In_ ULONG Count, _Inout_updates_(Count) PDWORD Values, _In_ DWORD How) = 0;
};

/* effect.cpp */

HRESULT
DiReadAttributes(
    _In_ HKEY Key,
    _Out_ LPDIFFDEVICEATTRIBUTES Attributes);

BOOLEAN
DiReadEffectEntry(
    _In_ HKEY Effects,
    _In_ DWORD Index,
    _Inout_ RX_DI_EFFECT_ENTRY* Entry);

HRESULT
DiCreateEffect(
    _In_ RxDiEffectHost* Device,
    _In_ REFGUID Guid,
    _In_opt_ LPCDIEFFECT Params,
    _Outptr_ LPDIRECTINPUTEFFECT* Effect,
    _In_opt_ IUnknown* OuterUnknown);

LPDIRECTINPUTEFFECT
DiHoldEffect(
    _In_ PVOID Effect);

VOID
DiUnholdEffect(
    _In_ LPDIRECTINPUTEFFECT Effect);

VOID
DiReleaseCreatedEffects(
    _In_reads_opt_(Count) PVOID* Effects,
    _In_ ULONG Count);

/* shepherd.cpp */

HRESULT
DiCreateShepherd(
    _In_opt_ HKEY Config,
    _Outptr_ RxDiShepherd** Shepherd);
