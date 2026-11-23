/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared declarations for the adapter factory, lists and adapters
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <objbase.h>

#include <d3dkmthk.h>
#include <d3dukmdt.h>
#include <dxcore_interface.h>

#include <new>
#include <vector>
#include <string>
#include <algorithm>

#include "trace.hpp"
#include "kmt.hpp"

class DcFactory;
class DcAdapter;
class DcAdapterList;

/* The adapter kinds a factory keeps, mirrored from the kernel adapter type bits */
enum DC_ADAPTER_FLAGS : ULONG
{
    DcFlagIntegrated = 0x1,
    DcFlagDiscrete = 0x2,
    DcFlagDetachable = 0x8,
    DcFlagHybrid = 0x10,
    DcFlagRender = 0x20,
    DcFlagComputeOnly = 0x40,
    DcFlagUmd9 = 0x80,
    DcFlagUmd10 = 0x100,
    DcFlagUmd11 = 0x200,
    DcFlagUmd12 = 0x400,
    DcFlagUmdAny = 0x780,
    DcFlagPlainSubSystem = 0x800,
};

/* Where an adapter is in the enumeration life cycle */
enum DC_ADAPTER_STATE : LONG
{
    DcStateNew = 0,
    DcStateRemoved = 1,
    DcStateRecheck = 2,
    DcStateValid = 3,
};

/* The hardware type attribute an adapter carries, as a column index */
enum DC_HARDWARE_KIND : ULONG
{
    DcKindNone = 0,
    DcKindGpu = 1,
    DcKindCompute = 2,
    DcKindNpu = 3,
    DcKindMedia = 4,
};

/* Factory capability bits granted by the kernel feature checks */
static constexpr ULONG DcFactoryEngineStatistics = 0x1;
static constexpr ULONG DcFactoryCarveout = 0x2;

/* The kernel adapter type bits the enumeration reads */
static constexpr UINT DcKmtRenderSupported = 0x1;
static constexpr UINT DcKmtSoftwareDevice = 0x4;
static constexpr UINT DcKmtHybridDiscrete = 0x10;
static constexpr UINT DcKmtHybridIntegrated = 0x20;
static constexpr UINT DcKmtAcgSupported = 0x100;
static constexpr UINT DcKmtDetachable = 0x400;
static constexpr UINT DcKmtComputeOnly = 0x800;

/* Property and state numbers outside the public enumerations */
static constexpr ULONG DcPropertyUniqueGuid = 10000;
static constexpr SIZE_T DcUniqueGuidSize = 80;

/* An engine name as the adapter reports it */
struct DcEngineName
{
    WCHAR Name[32];
};

/* What a factory learns about one physical adapter of a logical adapter */
struct DcPhysicalAdapter
{
    UINT EngineCount = 0;
    std::vector<UINT16> DedicatedSegments;
    std::vector<UINT16> SharedSegments;
    std::vector<UINT16> PartitionSegments;
    std::vector<DcEngineName> Engines;
};

/* Capabilities gathered while an adapter is being enumerated */
struct DcAdapterTraits
{
    BOOLEAN IsHardware = FALSE;
    BOOLEAN AcgCompatible = FALSE;
    BOOLEAN HasTemperature = FALSE;
    BOOLEAN HasEngineFrequency = FALSE;
    BOOLEAN HasMemoryFrequency = FALSE;
    BOOLEAN IsParavirtualized = FALSE;
    BOOLEAN HasHybridMode = FALSE;
    std::vector<DcPhysicalAdapter> Physical;
    ULONG Kind = DcKindNone;
};

/* One registered callback */
struct DcNotification
{
    DXCoreNotificationType Type;
    PFN_DXCORE_NOTIFICATION_CALLBACK Callback;
    void* Context;
    UINT32 Cookie;
};

/* Feature switches the system build checks at run time */
BOOLEAN
DcIsSystemFeatureEnabled(
    _In_ UINT32 FeatureId);

static constexpr UINT32 DcFeatureUniqueGuidCopyFix = 62385204;
static constexpr UINT32 DcFeatureNonFatalMonitoring = 61168129;
static constexpr UINT32 DcFeatureSingleAdapterHybridMode = 56684010;
static constexpr UINT32 DcFeatureCarveoutApi = 62681759;
static constexpr UINT32 DcFeatureCarveout = 61121285;

/* The global factory lock and the factory it guards */
extern CRITICAL_SECTION DcGlobalLock;
extern DcFactory* DcGlobalFactory;

/* GPU preference answers from the shared DirectX settings helper */
HRESULT
DcQueryGpuPreference(
    _In_reads_(Count) DcAdapter* const* Adapters,
    _In_ UINT Count,
    _Out_ UINT* Preference,
    _Out_ UINT* Index,
    _Out_ UINT* Factor);

HRESULT
DcQueryUserSetting(
    _In_ UINT Setting,
    _Out_writes_bytes_(Size) void* Buffer,
    _In_ UINT Size);

/* Sends a notification to its callback on a worker thread */
VOID
DcPostNotification(
    _In_ IUnknown* Object,
    _In_ const DcNotification& Notification);

#include "adapter.hpp"
#include "list.hpp"
#include "factory.hpp"
