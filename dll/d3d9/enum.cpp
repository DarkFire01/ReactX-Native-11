/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirect3D9Ex object and the displays it enumerates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

D9Enum::D9Enum(
    _In_ UINT SdkVersion,
    _In_ BOOLEAN IsEx)
    : SdkVersion(SdkVersion),
      IsEx(IsEx)
{
    InitializeCriticalSection(&Lock);
    InterlockedIncrement(&D9State.LiveObjects);
}

D9Enum::~D9Enum()
{
    for (UINT Index = 0; Index < AdapterCount; Index++)
    {
        D9CloseDriver(&Adapters[Index]);
        D9CloseAdapter(&Adapters[Index]);
    }

    InterlockedDecrement(&D9State.LiveObjects);
    DeleteCriticalSection(&Lock);
}

/* Copies one display into a slot of the adapter table */
static
VOID
D9TakeDisplay(
    _Out_ D9Adapter* Adapter,
    _In_ const DISPLAY_DEVICEA* Device)
{
    ZeroMemory(Adapter, sizeof(*Adapter));
    StringCchCopyNA(Adapter->DeviceName, _countof(Adapter->DeviceName), Device->DeviceName,
                    _countof(Adapter->DeviceName) - 1);
    Adapter->StateFlags = Device->StateFlags;
}

/* The primary display is adapter 0; the other attached, non mirroring displays follow in order */
VOID
D9Enum::CollectDisplays(VOID)
{
    DISPLAY_DEVICEA Device = {};
    BOOLEAN HavePrimary = FALSE;
    BOOLEAN Overflow = FALSE;
    UINT Count = 1;

    Device.cb = sizeof(Device);
    for (DWORD Index = 0; EnumDisplayDevicesA(nullptr, Index, &Device, 0); Index++)
    {
        DWORD Flags = Device.StateFlags;

        if ((Flags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0)
        {
            if (!HavePrimary && (Flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0 &&
                (Flags & (DISPLAY_DEVICE_MIRRORING_DRIVER | DISPLAY_DEVICE_DISCONNECT)) == 0)
            {
                HavePrimary = TRUE;
                D9TakeDisplay(&Adapters[0], &Device);
            }
            continue;
        }

        if ((Flags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0 || (Flags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;

        if (Count >= D9MaxAdapters)
        {
            Overflow = TRUE;
            break;
        }
        D9TakeDisplay(&Adapters[Count], &Device);
        Count += 1;
    }

    /* Without a primary the secondaries move up, and a display that did not fit takes the last slot */
    if (!HavePrimary)
    {
        MoveMemory(&Adapters[0], &Adapters[1], (Count - 1) * sizeof(Adapters[0]));
        ZeroMemory(&Adapters[D9MaxAdapters - 1], sizeof(Adapters[0]));
        if (Overflow)
            D9TakeDisplay(&Adapters[D9MaxAdapters - 1], &Device);
        else
            Count -= 1;
    }

    for (UINT Index = 0; Index < Count; Index++)
        Adapters[Index].Ordinal = Index;
    AdapterCount = Count;
}

HRESULT
D9Enum::Initialize(
    _In_reads_opt_(OverrideCount) const D3D9ON12_ARGS* Overrides,
    _In_ UINT OverrideCount)
{
    D9LockScope Scope(&Lock);

    CollectDisplays();
    for (UINT Index = 0; Index < AdapterCount; Index++)
        D9OpenAdapter(&Adapters[Index]);

    /* The extended object only drives displays that have a WDDM driver */
    for (UINT Index = 0; Index < AdapterCount; Index++)
    {
        if ((D9State.HasWddmDriver || !IsEx) && D9OpenDriver(&Adapters[Index], IsEx))
            Adapters[Index].GroupLuid = Adapters[Index].AdapterLuid;
    }
    GroupAdapters();
    ReadSwitches();

    /* The mapping layer is not part of ReactX; a request for it gets the native driver */
    for (UINT Index = 0; Overrides != nullptr && Index < OverrideCount; Index++)
    {
        if (Overrides[Index].Enable9On12)
            D9_TRACE_ONCE(L"Direct3D 9 on 12 was requested; using the native driver instead");
    }
    return S_OK;
}

STDMETHODIMP
D9Enum::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    D9LockScope Scope(&Lock);

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3D9)) ||
        (IsEx && IsEqualGUID(Iid, __uuidof(IDirect3D9Ex))))
    {
        *Object = static_cast<IDirect3D9Ex*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Enum::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&RefCount));
}

STDMETHODIMP_(ULONG)
D9Enum::Release()
{
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return static_cast<ULONG>(Count);
}

STDMETHODIMP_(UINT)
D9Enum::GetAdapterCount()
{
    D9LockScope Scope(&Lock);

    return AdapterCount;
}

/* Finds the monitor whose device name matches the display */
static
BOOL
CALLBACK
D9MatchMonitor(
    _In_ HMONITOR Monitor,
    _In_ HDC Dc,
    _In_ RECT* Rect,
    _In_ LPARAM Context)
{
    std::pair<PCSTR, HMONITOR>* Search = reinterpret_cast<std::pair<PCSTR, HMONITOR>*>(Context);
    MONITORINFOEXA Info = {};

    Info.cbSize = sizeof(Info);
    if (!GetMonitorInfoA(Monitor, &Info) || _stricmp(Info.szDevice, Search->first) != 0)
        return TRUE;

    Search->second = Monitor;
    return FALSE;
}

STDMETHODIMP_(HMONITOR)
D9Enum::GetAdapterMonitor(
    _In_ UINT Adapter)
{
    D9LockScope Scope(&Lock);
    std::pair<PCSTR, HMONITOR> Search;

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterMonitor: adapter %u does not exist", Adapter);
        return NULL;
    }

    Search.first = Adapters[Adapter].DeviceName;
    Search.second = NULL;
    EnumDisplayMonitors(NULL, nullptr, D9MatchMonitor, reinterpret_cast<LPARAM>(&Search));
    return Search.second;
}

/* The checks and creation CreateDevice and CreateDeviceEx share */
HRESULT
D9Enum::CreateDeviceCommon(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_opt_ HWND FocusWindow,
    _In_ DWORD BehaviorFlags,
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes,
    _COM_Outptr_ IDirect3DDevice9Ex** Device)
{
    UINT Heads = 1;
    HRESULT hr;

    *Device = nullptr;
    if (DeviceType == D3DDEVTYPE_NULLREF && (BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0)
    {
        D9_TRACE_ONCE(L"CreateDevice: the null reference device has no adapter groups");
        return D3DERR_INVALIDCALL;
    }

    /* Turning off driver management for resources also turns it off for the rest */
    if ((BehaviorFlags & D3DCREATE_DISABLE_DRIVER_MANAGEMENT_EX) != 0)
        BehaviorFlags |= D3DCREATE_DISABLE_DRIVER_MANAGEMENT;

    hr = ValidateCreation(Adapter, DeviceType, BehaviorFlags, Parameters, FullscreenModes);
    if (FAILED(hr))
        return hr;

    if ((BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0)
    {
        Heads = HeadCount(Adapter, BehaviorFlags);
        if (Heads == 1)
            BehaviorFlags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;
    }

    hr = ValidateWindows(Heads, FocusWindow, Parameters);
    if (FAILED(hr))
        return hr;

    /* ReactX never takes a display for itself */
    for (UINT Head = 0; Head < Heads; Head++)
    {
        if (!Parameters[Head].Windowed)
        {
            D9_TRACE_ONCE(L"CreateDevice: fullscreen devices are not implemented");
            return D3DERR_NOTAVAILABLE;
        }
    }

    RX_UNIMPLEMENTED();
    return D3DERR_NOTAVAILABLE;
}

STDMETHODIMP
D9Enum::CreateDevice(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ HWND FocusWindow,
    _In_ DWORD BehaviorFlags,
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _COM_Outptr_ IDirect3DDevice9** Device)
{
    D9LockScope Scope(&Lock);
    D3DDISPLAYMODEEX Modes[D9MaxAdapters];
    const D3DDISPLAYMODEEX* FullscreenModes = nullptr;
    IDirect3DDevice9Ex* Created = nullptr;
    HRESULT hr;

    *Device = nullptr;
    if (Adapter >= AdapterCount && !(AdapterCount == 0 && DeviceType == D3DDEVTYPE_NULLREF && Adapter == 0))
    {
        D9_TRACE_ONCE(L"CreateDevice: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    /* The plain entry point describes each fullscreen head's mode with its back buffer */
    if (!Parameters->Windowed)
    {
        UINT Heads = (BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0 ? Adapters[Adapter].AdaptersInGroup : 1;

        for (UINT Head = 0; Head < Heads && Head < D9MaxAdapters; Head++)
        {
            Modes[Head].Size = sizeof(Modes[Head]);
            Modes[Head].Width = Parameters[Head].BackBufferWidth;
            Modes[Head].Height = Parameters[Head].BackBufferHeight;
            Modes[Head].RefreshRate = Parameters[Head].FullScreen_RefreshRateInHz;
            Modes[Head].Format = Parameters[Head].BackBufferFormat;
            Modes[Head].ScanLineOrdering = D3DSCANLINEORDERING_UNKNOWN;
        }
        FullscreenModes = Modes;
    }

    hr = CreateDeviceCommon(Adapter, DeviceType, FocusWindow, BehaviorFlags, Parameters, FullscreenModes, &Created);
    *Device = Created;
    return hr;
}

STDMETHODIMP
D9Enum::CreateDeviceEx(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ HWND FocusWindow,
    _In_ DWORD BehaviorFlags,
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _Inout_opt_ D3DDISPLAYMODEEX* FullscreenMode,
    _COM_Outptr_ IDirect3DDevice9Ex** Device)
{
    D9LockScope Scope(&Lock);

    return CreateDeviceCommon(Adapter, DeviceType, FocusWindow, BehaviorFlags, Parameters, FullscreenMode, Device);
}

STDMETHODIMP
D9Enum::GetAdapterLUID(
    _In_ UINT Adapter,
    _Out_ LUID* Luid)
{
    D9LockScope Scope(&Lock);

    if (Adapter >= AdapterCount)
    {
        D9_TRACE_ONCE(L"GetAdapterLUID: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    *Luid = Adapters[Adapter].GroupLuid;
    return S_OK;
}
