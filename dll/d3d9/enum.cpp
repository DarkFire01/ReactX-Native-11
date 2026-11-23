/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The IDirect3D9Ex object and the displays it enumerates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include <d3d12.h>

D9Enum::D9Enum(
    _In_ UINT SdkVersion,
    _In_ BOOLEAN IsEx)
    : SdkVersion(SdkVersion),
      IsEx(IsEx)
{
    InitializeCriticalSection(&Lock);
    InterlockedIncrement(&D9State.LiveObjects);
    D9AcquireKernelModes();
}

D9Enum::~D9Enum()
{
    D9ReleaseKernelModes();
    for (UINT Index = 0; Index < AdapterCount; Index++)
    {
        D9CloseDriver(&Adapters[Index]);
        D9CloseAdapter(&Adapters[Index]);
    }

    InterlockedDecrement(&D9State.LiveObjects);
    DeleteCriticalSection(&Lock);
}

/* Game capture keeps a maximized window in windowed mode only for drivers from WDDM 2.0 (2.4 when linked) */
static
BOOLEAN
D9SupportsMaximizedWindowed(
    _In_ D3DKMT_HANDLE Adapter)
{
    D3DKMT_DRIVERVERSION Version = static_cast<D3DKMT_DRIVERVERSION>(0);
    UINT PhysicalCount = 0;

    if (!NT_SUCCESS(D9QueryKernel(Adapter, KMTQAITYPE_DRIVERVERSION, &Version, sizeof(Version))) ||
        !NT_SUCCESS(D9QueryKernel(Adapter, KMTQAITYPE_PHYSICALADAPTERCOUNT, &PhysicalCount, sizeof(PhysicalCount))))
    {
        return FALSE;
    }

    if (PhysicalCount == 1)
        return Version >= KMT_DRIVERVERSION_WDDM_2_0;
    return Version >= KMT_DRIVERVERSION_WDDM_2_4;
}

/* Applies a shim setting the registry holds; bit 0 asks for maximized windowed mode, bit 1 for the upgrade */
static
VOID
D9ApplyShimValue(
    _In_ HKEY Key,
    _In_z_ PCSTR Name)
{
    DWORD Value = 0;
    DWORD Type = 0;
    DWORD Size = sizeof(Value);

    if (RegQueryValueExA(Key, Name, nullptr, &Type, reinterpret_cast<BYTE*>(&Value), &Size) != ERROR_SUCCESS ||
        Type != REG_DWORD)
    {
        return;
    }

    if ((Value & 1) != 0)
    {
        D9State.MaximizedShimOn = TRUE;
        D9State.MaximizedShimAnyAdapter = TRUE;
    }
    if ((Value & 2) != 0)
    {
        D9State.UpgradeShimOn = TRUE;
        D9State.UpgradeShimAnyAdapter = TRUE;
    }
}

/* Settles both shims from the game defaults, the shim exports and the registry, each once */
static
VOID
D9SettleShims(VOID)
{
    CHAR Path[MAX_PATH] = {};
    HKEY Key;

    /* Stands for the game configuration store on a default system: the process counts as a fullscreen game
       whose maximized windowed mode game capture has not turned off, so both shims follow adapter support */
    if (!D9State.ShimDefaultsSeeded)
    {
        D9State.MaximizedShimOn = TRUE;
        D9State.UpgradeShimOn = TRUE;
        D9State.ShimDefaultsSeeded = TRUE;
    }

    if (!D9State.ShimDecisionsApplied)
    {
        if (D9State.MaximizedWindowedMode != 0)
        {
            D9State.MaximizedShimOn = D9State.MaximizedWindowedMode == 1;
            D9State.MaximizedShimAnyAdapter = TRUE;
        }
        if (D9State.SwapEffectUpgrade != 0)
        {
            D9State.UpgradeShimOn = D9State.SwapEffectUpgrade == 1;
            D9State.UpgradeShimAnyAdapter = TRUE;
        }
        D9State.ShimDecisionsApplied = TRUE;
    }

    if (D9State.ShimDecisionsMade)
        return;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D\\Shims", 0, KEY_QUERY_VALUE, &Key) ==
        ERROR_SUCCESS)
    {
        D9ApplyShimValue(Key, "MaximizedWindowedMode");
        RegCloseKey(Key);
    }
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Direct3D\\Shims\\MaximizedWindowedMode", 0,
                      KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
    {
        if (GetModuleFileNameA(NULL, Path, _countof(Path)) < _countof(Path))
            D9ApplyShimValue(Key, Path);
        RegCloseKey(Key);
    }
    D9State.ShimDecisionsMade = TRUE;
}

/* A shim covers an adapter when it is on and either reaches every adapter or the adapter supports capture */
static
BOOLEAN
D9ShimCovers(
    _In_ BOOLEAN On,
    _In_ BOOLEAN AnyAdapter,
    _In_ BOOLEAN AdapterSupports)
{
    if (!On)
        return FALSE;
    return AnyAdapter || AdapterSupports;
}

/* Formats the upgraded swap chain can present directly, or through a flip format it converts to */
static
D3DFORMAT
D9UpgradeProxyFormat(
    _In_ D3DFORMAT Format)
{
    switch (Format)
    {
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
        return D3DFMT_A8R8G8B8;
    case D3DFMT_X8B8G8R8:
        return D3DFMT_A8B8G8R8;
    case D3DFMT_A2R10G10B10:
    case D3DFMT_A2B10G10R10_XR_BIAS:
        return D3DFMT_A2B10G10R10;
    default:
        return Format;
    }
}

/* Windowed devices on displays that keep games in maximized windowed mode present through the flip model,
   which takes them out of the kernel's legacy device mode */
BOOLEAN
D9Enum::UpgradesSwapEffect(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ DWORD BehaviorFlags,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    const D9Adapter* Display = &Adapters[Adapter];
    D3DFORMAT Proxy;

    D9SettleShims();
    if (!D9ShimCovers(D9State.MaximizedShimOn, D9State.MaximizedShimAnyAdapter, Display->MaximizedWindowedForGameDvr))
        return FALSE;
    if (DeviceType != D3DDEVTYPE_HAL || (BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0 ||
        Parameters->SwapEffect == D3DSWAPEFFECT_OVERLAY)
    {
        return FALSE;
    }
    if (!D9ShimCovers(D9State.UpgradeShimOn, D9State.UpgradeShimAnyAdapter, Display->MaximizedWindowedForGameDvr))
        return FALSE;

    if (D9IsFlipExFormat(Parameters->BackBufferFormat))
        return TRUE;
    Proxy = D9UpgradeProxyFormat(Parameters->BackBufferFormat);
    return D9IsFlipExFormat(Proxy) &&
           SUCCEEDED(CheckDeviceFormatConversion(Adapter, D3DDEVTYPE_HAL, Parameters->BackBufferFormat, Proxy));
}

/* The mapping layer settings the application passed for this adapter: one without a device fits any */
static
const D3D9ON12_ARGS*
D9FindMappingArgs(
    _In_ const D9Adapter* Adapter,
    _In_reads_opt_(Count) const D3D9ON12_ARGS* Overrides,
    _In_ UINT Count)
{
    for (UINT Index = 0; Overrides != nullptr && Index < Count; Index++)
    {
        ID3D12Device* Device = nullptr;
        LUID Luid;

        if (Overrides[Index].pD3D12Device == nullptr)
            return &Overrides[Index];
        if (FAILED(Overrides[Index].pD3D12Device->QueryInterface(IID_PPV_ARGS(&Device))))
            continue;

        Luid = Device->GetAdapterLuid();
        Device->Release();
        if (Luid.LowPart == Adapter->AdapterLuid.LowPart && Luid.HighPart == Adapter->AdapterLuid.HighPart)
            return &Overrides[Index];
    }
    return nullptr;
}

/* A display whose kernel driver has no Direct3D 9 user mode driver asks for the mapping layer */
static
BOOLEAN
D9DriverWantsMapping(
    _In_ D3DKMT_HANDLE Adapter)
{
    D3DKMT_UMDFILENAMEINFO Name = {};

    Name.Version = KMTUMDVERSION_DX9;
    return D9QueryKernel(Adapter, KMTQAITYPE_UMDRIVERNAME, &Name, sizeof(Name)) == STATUS_INVALID_PARAMETER;
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
    {
        D9Adapter* Adapter = &Adapters[Index];
        const D3D9ON12_ARGS* Mapping;
        BOOLEAN WantsMapping;

        D9OpenAdapter(Adapter);
        Adapter->MaximizedWindowedForGameDvr = D9SupportsMaximizedWindowed(Adapter->KernelAdapter);

        /* The mapping layer is not part of ReactX; a request for it gets the native driver */
        Mapping = D9FindMappingArgs(Adapter, Overrides, OverrideCount);
        WantsMapping = Mapping != nullptr ? Mapping->Enable9On12 != FALSE : D9DriverWantsMapping(Adapter->KernelAdapter);
        if (WantsMapping)
            D9_TRACE_ONCE(L"Direct3D 9 on 12 was requested; using the native driver instead");

        /* The extended object only drives displays once a WDDM driver was seen */
        if ((D9State.HasWddmDriver || !IsEx) && D9OpenDriver(Adapter, IsEx))
            Adapter->GroupLuid = Adapter->AdapterLuid;
    }
    GroupAdapters();
    ReadSwitches();
    ReviewAdapterParents();
    NotePresentSupport();
    return S_OK;
}

/* Hybrid and indirect display adapters may hand a display to another render adapter; the system checks them here */
VOID
D9Enum::ReviewAdapterParents(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_ENUMADAPTERS2 Enum = {};
    D3DKMT_ADAPTERTYPE* Types = nullptr;
    BOOLEAN* CrossAdapter = nullptr;
    const D9Adapter* Primary = nullptr;
    UINT PrimaryIndex = MAXUINT;
    BOOLEAN AnyIndirect = FALSE;

    if (Kmt == nullptr || Kmt->EnumAdapters2 == nullptr || Kmt->CloseAdapter == nullptr || AdapterCount == 0)
        return;
    if (!NT_SUCCESS(Kmt->EnumAdapters2(&Enum)))
        return;

    Enum.pAdapters = new (std::nothrow) D3DKMT_ADAPTERINFO[Enum.NumAdapters]();
    Types = new (std::nothrow) D3DKMT_ADAPTERTYPE[Enum.NumAdapters]();
    CrossAdapter = new (std::nothrow) BOOLEAN[Enum.NumAdapters]();
    if (Enum.pAdapters == nullptr || Types == nullptr || CrossAdapter == nullptr ||
        !NT_SUCCESS(Kmt->EnumAdapters2(&Enum)))
    {
        goto Cleanup;
    }

    for (UINT Index = 0; Index < AdapterCount && Primary == nullptr; Index++)
    {
        if ((Adapters[Index].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0)
            Primary = &Adapters[Index];
    }

    for (UINT Index = 0; Primary != nullptr && Index < Enum.NumAdapters; Index++)
    {
        const LUID* Luid = &Enum.pAdapters[Index].AdapterLuid;

        if (Luid->LowPart != Primary->AdapterLuid.LowPart || Luid->HighPart != Primary->AdapterLuid.HighPart)
            continue;
        if (NT_SUCCESS(D9QueryKernel(Enum.pAdapters[Index].hAdapter, KMTQAITYPE_ADAPTERTYPE, &Types[Index],
                                     sizeof(Types[Index]))))
        {
            PrimaryIndex = Index;
            break;
        }
    }

    for (UINT Index = 0; Index < Enum.NumAdapters; Index++)
    {
        D3DKMT_HANDLE Handle = Enum.pAdapters[Index].hAdapter;
        D3DKMT_QUERY_ADAPTER_UNIQUE_GUID Unique = {};
        D3DKMT_WDDM_1_3_CAPS Caps = {};

        if (!NT_SUCCESS(D9QueryKernel(Handle, KMTQAITYPE_ADAPTERTYPE, &Types[Index], sizeof(Types[Index]))) ||
            !NT_SUCCESS(D9QueryKernel(Handle, KMTQAITYPE_QUERY_ADAPTER_UNIQUE_GUID, &Unique, sizeof(Unique))))
        {
            continue;
        }

        if (!Types[Index].PostDevice && Types[Index].IndirectDisplayDevice)
            AnyIndirect = TRUE;
        if (NT_SUCCESS(D9QueryKernel(Handle, KMTQAITYPE_WDDM_1_3_CAPS, &Caps, sizeof(Caps))))
            CrossAdapter[Index] = Caps.SupportCrossAdapterResource;
    }

    if ((PrimaryIndex != MAXUINT && CrossAdapter[PrimaryIndex]) || AnyIndirect)
        D9_TRACE_ONCE(L"hybrid and indirect display adapters keep their own displays; reparenting is not implemented");

Cleanup:
    for (UINT Index = 0; Enum.pAdapters != nullptr && Index < Enum.NumAdapters; Index++)
    {
        D3DKMT_CLOSEADAPTER Close;

        if (Enum.pAdapters[Index].hAdapter == 0)
            continue;
        Close.hAdapter = Enum.pAdapters[Index].hAdapter;
        Kmt->CloseAdapter(&Close);
    }
    delete[] Enum.pAdapters;
    delete[] Types;
    delete[] CrossAdapter;
}

/* Notes which displays flip straight to the screen and which have overlay planes to present on */
VOID
D9Enum::NotePresentSupport(VOID)
{
    typedef NTSTATUS (APIENTRY* PD9_GET_OVERLAY_CAPS)(D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS*);
    static PD9_GET_OVERLAY_CAPS GetOverlayCaps = reinterpret_cast<PD9_GET_OVERLAY_CAPS>(
        GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTGetMultiPlaneOverlayCaps"));

    for (UINT Index = 0; Index < AdapterCount; Index++)
    {
        D9Adapter* Adapter = &Adapters[Index];
        D3DKMT_DIRECTFLIP_SUPPORT DirectFlip = {};
        D3DKMT_MPOKERNELCAPS_SUPPORT KernelOverlays = {};
        D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS OverlayCaps = {};
        BOOLEAN Overlays = Index == 0;

        Adapter->DirectFlip = NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_DIRECTFLIP_SUPPORT,
                                                       &DirectFlip, sizeof(DirectFlip))) &&
                              DirectFlip.Supported;
        Adapter->MultiPlaneOverlay = FALSE;

        /* Displays after the first need the driver to offer overlays on secondary displays */
        if (Index != 0)
        {
            D3DKMT_MULTIPLANEOVERLAY_SECONDARY_SUPPORT Secondary = {};

            Overlays = NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_MULTIPLANEOVERLAY_SECONDARY_SUPPORT,
                                                &Secondary, sizeof(Secondary))) &&
                       Secondary.Supported;
        }
        if (!Overlays)
            continue;

        if (!NT_SUCCESS(D9QueryKernel(Adapter->KernelAdapter, KMTQAITYPE_MPOKERNELCAPS_SUPPORT, &KernelOverlays,
                                      sizeof(KernelOverlays))) ||
            !KernelOverlays.Supported || GetOverlayCaps == nullptr)
        {
            continue;
        }

        OverlayCaps.hAdapter = Adapter->KernelAdapter;
        OverlayCaps.VidPnSourceId = Adapter->SourceId;
        Adapter->MultiPlaneOverlay = NT_SUCCESS(GetOverlayCaps(&OverlayCaps)) && OverlayCaps.MaxPlanes >= 2;
    }
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
    D9Device* Created;
    BOOLEAN Upgrade;
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

    hr = ValidateCreation(Adapter, DeviceType, FocusWindow, BehaviorFlags, Parameters, FullscreenModes);
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

    Upgrade = UpgradesSwapEffect(Adapter, DeviceType, BehaviorFlags, Parameters);
    Created = new (std::nothrow) D9Device(this, &Adapters[Adapter], DeviceType, FocusWindow, BehaviorFlags, IsEx,
                                          Upgrade);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Parameters);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Device = Created;
    return S_OK;
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
