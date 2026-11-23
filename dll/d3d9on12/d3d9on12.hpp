/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects shared by the Direct3D 9 user mode driver that runs on Direct3D 12
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

/* The translation layer brings its own Direct3D 12 and DXCore headers and defines these itself */
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN
#include <D3D12TranslationLayerDependencyIncludes.h>
#include <D3D12TranslationLayerIncludes.h>
#include <SharedResourceHelpers.hpp>

#include <dxgi1_6.h>
#include <d3d9.h>
#include <d3dumddi.h>

#include <featurestagingapi.h>
#include <stddef.h>

#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

#include "trace.hpp"

namespace TL = D3D12TranslationLayer;

/* Two values a newer runtime hands over beside the Direct3D 12 objects; kept as given */
struct NineOpenExtras
{
    PVOID Values[2];
};

/* What d3d9.dll passes the private open: the application's Direct3D 12 objects, if it has any */
struct NineOpenArgs
{
    IUnknown* Device;
    IUnknown* const* Queues;
    UINT QueueCount;
    UINT NodeMask;

    /* Present from the second form on; the first form is widened to it before use */
    UINT Version;
    const NineOpenExtras* Extras;
};

/* The first form of the arguments, as d3d9.dll lays them out */
struct NineOpenArgsV1
{
    IUnknown* Device;
    IUnknown* const* Queues;
    UINT QueueCount;
    UINT NodeMask;
};

typedef HRESULT (APIENTRY* PFN_NINE_OPEN_ADAPTER)(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ VOID* OpenArgs);

/* Entry points d3d9.dll looks up beside the regular adapter and device tables */
struct NinePrivateTable
{
    PFN_NINE_OPEN_ADAPTER OpenAdapter;
    PVOID GetSharedGdiHandle;
    PVOID CreateSharedNtHandle;
    PVOID GetExecutionState;
    PVOID KernelPresent;
    PVOID CreateFence;
    PVOID OpenFence;
    PVOID ShareFence;
    PVOID WaitForFence;
    PVOID SignalFence;
    PVOID GetCompletedFenceValue;
    PVOID DestroyTrackedFence;
    PVOID QueryResourceDriverDataSize;
    PVOID OpenResource;
    PVOID CreateKmtHandle;
    PVOID QueryResourceFromKmtHandle;
    PVOID DestroyKmtHandle;
    PVOID CreateWrappingHandle;
    PVOID DestroyWrappingHandle;
    PVOID GetD3D12Device;
    PVOID TransitionResource;
    PVOID SetResourceState;
    PVOID SetMaximumFrameLatency;
    PVOID IsMaximumFrameLatencyReached;
    PVOID GetD3D12Resource;
    PVOID AddResourceWaitsToQueue;
    PVOID AddDeferredWaitsToResource;

    /* The unversioned table ends here */
    PFN_NINE_OPEN_ADAPTER OpenAdapter2;
    PVOID SubmitForPresent;
    PVOID PreExecuteCommandList;
    PVOID PostExecuteCommandList;
};

/* How many bytes the unversioned table export fills */
static constexpr SIZE_T NineFirstTableSize = offsetof(NinePrivateTable, OpenAdapter2);

/* The newest runtime version this driver reports back */
static constexpr UINT NineNewestRuntimeVersion = D3D_UMD_INTERFACE_VERSION_WDDM3_2_2;

struct NineDevice;

/* One opened adapter: the Direct3D 12 device and queue every device of it shares */
struct NineAdapter
{
    const D3DDDI_ADAPTERCALLBACKS* Callbacks;

    /* Whether fully typed formats may be cast between each other */
    BOOLEAN TypedCasting;

    /* The device video caps are asked through */
    NineDevice* VideoDevice;

    ID3D12Device* Device;
    ID3D12CommandQueue* Queue;
    DXCoreHardwareID HardwareId;
    LARGE_INTEGER DriverVersion;
    NineOpenExtras Extras;
    BOOLEAN HasExtra[2];
};

/* adapter.cpp */
HRESULT
APIENTRY
NineOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ VOID* OpenArgs);

HRESULT
APIENTRY
NineOpenAdapter2(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ VOID* OpenArgs);

/* caps.cpp */
HRESULT
APIENTRY
NineGetCaps(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDIARG_GETCAPS* Args);

/* formats.cpp */
DXGI_FORMAT
NineDxgiFormat(
    _In_ D3DDDIFORMAT Format);

/* The Direct3D 9 format a shared Direct3D 12 resource is reported with; unknown when there is none */
D3DDDIFORMAT
NineD3dFormat(
    _In_ DXGI_FORMAT Format);

/* Formats the hardware vendors defined that map onto a Direct3D 12 format of their own */
BOOLEAN
NineIsIhvFormat(
    _In_ D3DDDIFORMAT Format);

/* The shader view component mapping a format is sampled with; ~0 for formats without one */
UINT
NineViewSwizzle(
    _In_ D3DDDIFORMAT Format,
    _In_ BOOLEAN AlphaIsOne);

UINT
NineCollectFormatOps(
    _In_ const NineAdapter* Adapter,
    _Out_writes_opt_(Limit + 1) FORMATOP* Formats,
    _In_ UINT Limit,
    _Out_ HRESULT* Result);

/* main.cpp */
HRESULT
NineDxgiResult(
    _In_ HRESULT Result);

/* What SetAppCompatData hands over, in the order d3d9.dll lays it out */
struct NineAppCompatInfo
{
    ULONG Flags;
    ULONG ShaderLimits[2];
    ULONG MaxUploadPerCommandList;
    ULONG MaxSrvHeapSize;
    ULONG BufferPoolTrimThreshold;
    ULONG Options;
};

extern NineAppCompatInfo NineAppCompat;

/* settings.cpp: values read once from the Direct3D9on12 registry key; ~0 when a value is absent */
struct NineSettings
{
    BOOLEAN SingleThread;
    ULONG MaxUploadPerCommandList;
    ULONG MaxSrvHeapSize;
    ULONG BufferPoolTrimThreshold;
};

const NineSettings&
NineGetSettings(VOID);
