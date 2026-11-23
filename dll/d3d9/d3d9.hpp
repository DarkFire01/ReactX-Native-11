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

private:
    VOID
    CollectDisplays(VOID);

    LONG RefCount = 1;
    CRITICAL_SECTION Lock;
    UINT SdkVersion;
    BOOLEAN IsEx;

    D9Adapter Adapters[D9MaxAdapters] = {};
    UINT AdapterCount = 0;
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
