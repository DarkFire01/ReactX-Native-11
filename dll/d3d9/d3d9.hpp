/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects shared by every part of the Direct3D 9 runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "rxcommon.hpp"

#include <d3d9.h>
#include <d3d9on12.h>
#include <d3dumddi.h>
#include <strsafe.h>

#include <utility>

#include "adapteridentity.hpp"
#include "trace.hpp"

/* The most displays one IDirect3D9 object enumerates */
static constexpr UINT D9MaxAdapters = 12;

/* Process wide switches the shim exports and the runtime share */
struct D9Globals
{
    CRITICAL_SECTION Lock;
    HMODULE Module;
    HANDLE ExclusiveMode;
    HANDLE CheckExclusiveMode;

    /* Set once any display turned out to have a WDDM user mode driver */
    BOOLEAN HasWddmDriver;

    /* Tri-state shim switches: 0 unset, 1 on, 2 off */
    LONG MaximizedWindowedMode;
    LONG SwapEffectUpgrade;
    BOOLEAN ShimDecisionsMade;
    BOOLEAN ShimDecisionsApplied;

    /* Where both shims stand: on, and whether they also cover adapters without game capture support */
    BOOLEAN ShimDefaultsSeeded;
    BOOLEAN MaximizedShimOn;
    BOOLEAN MaximizedShimAnyAdapter;
    BOOLEAN UpgradeShimOn;
    BOOLEAN UpgradeShimAnyAdapter;

    LONG Force9On12;
    BOOLEAN VendorIdLie;
    DWORD FakeVendorId;
    UINT HybridEnumeration;
    BOOLEAN MaximizedHwndOverride;

    LONG LiveObjects;
};

extern D9Globals D9State;

/* One display as an IDirect3D9 object sees it */
struct D9Adapter
{
    CHAR DeviceName[32];
    DWORD StateFlags;
    UINT Ordinal;

    D3DKMT_HANDLE KernelAdapter;
    LUID AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId;

    /* The adapter LUID GetAdapterLUID reports and groups are formed by; zero without a driver */
    LUID GroupLuid;
    UINT MasterOrdinal;
    UINT OrdinalInGroup;
    UINT AdaptersInGroup;

    /* The user mode driver, present when OpenAdapter and the capability queries succeeded */
    BOOLEAN HasDriver;
    HMODULE Module;
    D3DDDI_ADAPTERCALLBACKS Callbacks;
    D3DDDI_ADAPTERFUNCS Functions;
    HANDLE DriverAdapter;
    UINT RuntimeVersion;
    UINT DriverVersion;
    UINT DriverModel;
    UINT FirstUmdVersion;

    /* What the kernel says about the adapter */
    D3DKMT_FLIPQUEUEINFO FlipQueue;
    D3DKMT_ADAPTERTYPE AdapterType;
    D3DKMT_WDDM_1_2_CAPS Wddm12Caps;
    D3DKMT_WDDM_1_3_CAPS Wddm13Caps;
    UINT64 VideoMemory;
    UINT PhysicalAdapterCount;
    BOOLEAN NoOverlayCap;
    BOOLEAN Wddm11Plus;

    /* Presentation support the object notes once every display is open */
    BOOLEAN MaximizedWindowedForGameDvr;
    BOOLEAN DirectFlip;
    BOOLEAN MultiPlaneOverlay;

    /* The desktop mode when the object was created */
    D3DDISPLAYMODEEX DesktopMode;
    D3DDDI_ROTATION Rotation;

    /* Driver answers, with the runtime's corrections applied */
    D3DCAPS9 DriverCaps;
    FORMATOP* Formats;
    UINT FormatCount;
    D3DDDIQUERYTYPE* Queries;
    UINT QueryCount;

    /* Which 16 and 32 bit desktop formats the driver can display */
    D3DFORMAT Display16Format;
    BOOLEAN Displays16;
    BOOLEAN Displays32;

    /* Kernel modes in the 10 bit and float desktop formats */
    D3DDISPLAYMODE* WideModes;
    UINT WideModeCount;

    /* The mode table: listed modes, then wide format copies, then kernel modes the monitor did not validate
       (for tables read from GDI that last count is the spare room instead) */
    D3DDISPLAYMODEEX* Modes;
    UINT ModeCount;
    UINT ExtendedModeCount;
    UINT UnvalidatedModeCount;
};

/* The IDirect3D9Ex object Direct3DCreate9 and its siblings hand out */
class D9Enum final : public IDirect3D9Ex
{
public:
    D9Enum(
        _In_ UINT SdkVersion,
        _In_ BOOLEAN IsEx);

    ~D9Enum();

    D9Enum(const D9Enum&) = delete;
    D9Enum& operator=(const D9Enum&) = delete;

    HRESULT
    Initialize(
        _In_reads_opt_(OverrideCount) const D3D9ON12_ARGS* Overrides,
        _In_ UINT OverrideCount);

    /* IUnknown */
    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    /* IDirect3D9 */
    STDMETHOD(RegisterSoftwareDevice)(
        _In_ VOID* InitializeFunction) override;

    STDMETHOD_(UINT, GetAdapterCount)() override;

    STDMETHOD(GetAdapterIdentifier)(
        _In_ UINT Adapter,
        _In_ DWORD Flags,
        _Out_ D3DADAPTER_IDENTIFIER9* Identifier) override;

    STDMETHOD_(UINT, GetAdapterModeCount)(
        _In_ UINT Adapter,
        _In_ D3DFORMAT Format) override;

    STDMETHOD(EnumAdapterModes)(
        _In_ UINT Adapter,
        _In_ D3DFORMAT Format,
        _In_ UINT Mode,
        _Out_ D3DDISPLAYMODE* DisplayMode) override;

    STDMETHOD(GetAdapterDisplayMode)(
        _In_ UINT Adapter,
        _Out_ D3DDISPLAYMODE* DisplayMode) override;

    STDMETHOD(CheckDeviceType)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ D3DFORMAT DisplayFormat,
        _In_ D3DFORMAT BackBufferFormat,
        _In_ BOOL Windowed) override;

    STDMETHOD(CheckDeviceFormat)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ D3DFORMAT AdapterFormat,
        _In_ DWORD Usage,
        _In_ D3DRESOURCETYPE ResourceType,
        _In_ D3DFORMAT CheckFormat) override;

    STDMETHOD(CheckDeviceMultiSampleType)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ D3DFORMAT SurfaceFormat,
        _In_ BOOL Windowed,
        _In_ D3DMULTISAMPLE_TYPE MultiSampleType,
        _Out_opt_ DWORD* QualityLevels) override;

    STDMETHOD(CheckDepthStencilMatch)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ D3DFORMAT AdapterFormat,
        _In_ D3DFORMAT RenderTargetFormat,
        _In_ D3DFORMAT DepthStencilFormat) override;

    STDMETHOD(CheckDeviceFormatConversion)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ D3DFORMAT SourceFormat,
        _In_ D3DFORMAT TargetFormat) override;

    STDMETHOD(GetDeviceCaps)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _Out_ D3DCAPS9* Caps) override;

    STDMETHOD_(HMONITOR, GetAdapterMonitor)(
        _In_ UINT Adapter) override;

    STDMETHOD(CreateDevice)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ HWND FocusWindow,
        _In_ DWORD BehaviorFlags,
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _COM_Outptr_ IDirect3DDevice9** Device) override;

    /* IDirect3D9Ex */
    STDMETHOD_(UINT, GetAdapterModeCountEx)(
        _In_ UINT Adapter,
        _In_ const D3DDISPLAYMODEFILTER* Filter) override;

    STDMETHOD(EnumAdapterModesEx)(
        _In_ UINT Adapter,
        _In_ const D3DDISPLAYMODEFILTER* Filter,
        _In_ UINT Mode,
        _Out_ D3DDISPLAYMODEEX* DisplayMode) override;

    STDMETHOD(GetAdapterDisplayModeEx)(
        _In_ UINT Adapter,
        _Out_opt_ D3DDISPLAYMODEEX* DisplayMode,
        _Out_opt_ D3DDISPLAYROTATION* Rotation) override;

    STDMETHOD(CreateDeviceEx)(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ HWND FocusWindow,
        _In_ DWORD BehaviorFlags,
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _Inout_opt_ D3DDISPLAYMODEEX* FullscreenMode,
        _COM_Outptr_ IDirect3DDevice9Ex** Device) override;

    STDMETHOD(GetAdapterLUID)(
        _In_ UINT Adapter,
        _Out_ LUID* Luid) override;

    BOOLEAN
    IsExtended() const
    {
        return IsEx;
    }

    HRESULT
    ValidateReset(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ DWORD BehaviorFlags,
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode);

private:
    VOID
    CollectDisplays(VOID);

    VOID
    GroupAdapters(VOID);

    VOID
    ReadSwitches(VOID);

    VOID
    ReviewAdapterParents(VOID);

    VOID
    NotePresentSupport(VOID);

    HRESULT
    DeviceTable(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _Outptr_ const D9Adapter** Table);

    UINT
    HeadCount(
        _In_ UINT Adapter,
        _In_ DWORD BehaviorFlags) const;

    HRESULT
    AdapterCaps(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _Outptr_ const D3DCAPS9** Caps);

    HRESULT
    ValidateOverlay(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ DWORD BehaviorFlags,
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    HRESULT
    ValidatePresentation(
        _In_ UINT Adapter,
        _In_ UINT Heads,
        _In_ D3DDEVTYPE DeviceType,
        _In_ DWORD BehaviorFlags,
        _Inout_updates_(Heads) D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes);

    HRESULT
    ValidateCreation(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_opt_ HWND FocusWindow,
        _In_ DWORD BehaviorFlags,
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes);

    HRESULT
    ValidateWindows(
        _In_ UINT Heads,
        _In_opt_ HWND FocusWindow,
        _In_reads_(Heads) const D3DPRESENT_PARAMETERS* Parameters);

    BOOLEAN
    UpgradesSwapEffect(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_ DWORD BehaviorFlags,
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    HRESULT
    CreateDeviceCommon(
        _In_ UINT Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_opt_ HWND FocusWindow,
        _In_ DWORD BehaviorFlags,
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes,
        _COM_Outptr_ IDirect3DDevice9Ex** Device);

    HRESULT
    FindMode(
        _In_ UINT Adapter,
        _In_ const D3DDISPLAYMODEFILTER* Filter,
        _In_ UINT Mode,
        _Out_ D3DDISPLAYMODEEX* DisplayMode);

    LONG RefCount = 1;
    CRITICAL_SECTION Lock;
    UINT SdkVersion;
    BOOLEAN IsEx;

    D9Adapter Adapters[D9MaxAdapters] = {};
    UINT AdapterCount = 0;

    BOOLEAN SoftwareOnly = FALSE;
    BOOLEAN CanCalibrateGamma = FALSE;
};

/* Serializes one IDirect3D9 object's methods */
class D9LockScope
{
public:
    explicit
    D9LockScope(
        _In_ CRITICAL_SECTION* Section)
        : Held(Section)
    {
        EnterCriticalSection(Held);
    }

    ~D9LockScope()
    {
        LeaveCriticalSection(Held);
    }

    D9LockScope(const D9LockScope&) = delete;
    D9LockScope& operator=(const D9LockScope&) = delete;

private:
    CRITICAL_SECTION* Held;
};

/* adapter.cpp */
VOID
D9OpenAdapter(
    _Inout_ D9Adapter* Adapter);

VOID
D9CloseAdapter(
    _Inout_ D9Adapter* Adapter);

BOOLEAN
D9QueryUmdName(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTUMDVERSION Version,
    _Out_ D3DKMT_UMDFILENAMEINFO* Name);

NTSTATUS
D9QueryKernel(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size);

HRESULT
D9HresultFromStatus(
    _In_ NTSTATUS Status);

/* umd.cpp */
BOOLEAN
D9OpenDriver(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx);

VOID
D9CloseDriver(
    _Inout_ D9Adapter* Adapter);

BOOLEAN
D9DriverUpdating(
    _In_ D3DKMT_HANDLE Adapter);

BOOLEAN
D9RereadDriverCaps(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx);

BOOLEAN
D9ReadDriverAnswers(
    _Inout_ D9Adapter* Adapter,
    _In_ BOOLEAN IsEx);

VOID
D9FreeDriverAnswers(
    _Inout_ D9Adapter* Adapter);

_Ret_maybenull_
D9Adapter*
D9CopyAdapter(
    _In_ const D9Adapter* Adapter);

HRESULT
D9ProbeDriver(
    _In_ const D9Adapter* Adapter,
    _In_ BOOLEAN IsEx,
    _Out_ D3DCAPS9* Caps);

/* hardware.cpp */
BOOLEAN
D9DriverMayThread(
    _In_ DWORD BehaviorFlags,
    _In_opt_ HWND FocusWindow,
    _In_ BOOLEAN Windowed);

HRESULT
D9CallOpenAdapter(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDDI_ADAPTERCALLBACKS* Callbacks,
    _Out_ D3DDDI_ADAPTERFUNCS* Functions,
    _Out_ HANDLE* DriverAdapter,
    _Out_ UINT* RuntimeVersion,
    _Out_ UINT* DriverVersion);

HRESULT
D9DriverGetCaps(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDDICAPS_TYPE Type,
    _In_opt_ VOID* Info,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size);

/* modes.cpp */
HRESULT
D9ReadDesktopMode(
    _In_z_ PCSTR DeviceName,
    _Out_ D3DDISPLAYMODEEX* Mode,
    _Out_opt_ D3DDDI_ROTATION* Rotation);

VOID
D9AcquireKernelModes(VOID);

VOID
D9ReleaseKernelModes(VOID);

HRESULT
D9ReadKernelModes(
    _In_ const D9Adapter* Adapter,
    _In_ BOOLEAN Refresh,
    _Outptr_result_buffer_maybenull_(*Count) D3DKMT_DISPLAYMODE** Modes,
    _Out_ UINT* Count);

VOID
D9FreeModeTable(
    _Inout_ D9Adapter* Adapter);

BOOLEAN
D9IsDesktopFormat(
    _In_ D3DFORMAT Format,
    _In_ BOOLEAN IsEx);

/* validate.cpp */
BOOLEAN
D9IsFlipExFormat(
    _In_ D3DFORMAT Format);

/* formats.cpp */
BOOLEAN
D9IsBackBufferFormat(
    _In_ D3DFORMAT Format,
    _In_ BOOLEAN IsEx);

LONG
D9BytesPerPixel(
    _In_ D3DFORMAT Format);

BOOLEAN
D9DriverHasOps(
    _In_ const D9Adapter* Adapter,
    _In_ D3DFORMAT Format,
    _In_ UINT Ops);
