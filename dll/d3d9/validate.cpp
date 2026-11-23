/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The rules CreateDevice and CreateDeviceEx hold their arguments to
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* Behavior flags Direct3D 9 does not define */
static constexpr DWORD D9UndefinedBehaviorFlags = 0x0FFF1009;

/* Every vertex processing choice; exactly one of them must be asked for */
static constexpr DWORD D9VertexProcessingFlags =
    D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING;

/* Presentation flags a swap chain may carry */
static constexpr DWORD D9KnownPresentFlags = 0x00000FF7;

/* The most back buffers a swap chain of each object kind may ask for */
static constexpr UINT D9MaxBackBuffers = 3;
static constexpr UINT D9MaxBackBuffersEx = 30;

/* Rotations that swap a mode's width and height */
static
BOOLEAN
D9IsQuarterTurn(
    _In_ D3DDISPLAYROTATION Rotation)
{
    return Rotation == D3DDISPLAYROTATION_90 || Rotation == D3DDISPLAYROTATION_270;
}

/* The opaque twin a fullscreen back buffer format scans out as */
static
D3DFORMAT
D9ScanOutFormat(
    _In_ D3DFORMAT Format)
{
    switch (Format)
    {
    case D3DFMT_A8R8G8B8:
        return D3DFMT_X8R8G8B8;
    case D3DFMT_A1R5G5B5:
        return D3DFMT_X1R5G5B5;
    case D3DFMT_A4R4G4B4:
        return D3DFMT_X4R4G4B4;
    case D3DFMT_A8B8G8R8:
        return D3DFMT_X8B8G8R8;
    default:
        return Format;
    }
}

/* Television rates of 29 and 59 Hz count as 30 and 60 */
static
UINT
D9RoundTelevisionRate(
    _In_ UINT Rate)
{
    if (Rate == 29 || Rate == 59)
        return Rate + 1;
    return Rate;
}

/* Back buffer formats a flip model swap chain can share with the compositor */
static
BOOLEAN
D9IsFlipExFormat(
    _In_ D3DFORMAT Format)
{
    return Format == D3DFMT_A8R8G8B8 || Format == D3DFMT_X8R8G8B8 || Format == D3DFMT_A2B10G10R10 ||
           Format == D3DFMT_A16B16G16R16F;
}

/* How many displays a device made with these flags drives */
UINT
D9Enum::HeadCount(
    _In_ UINT Adapter,
    _In_ DWORD BehaviorFlags) const
{
    if ((BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) == 0)
        return 1;
    return Adapters[Adapter].AdaptersInGroup;
}

HRESULT
D9Enum::ValidateOverlay(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ DWORD BehaviorFlags,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    DDICHECKOVERLAYSUPPORTINPUT Input = {};
    D3DOVERLAYCAPS Support = {};
    D3DDISPLAYROTATION Rotation = static_cast<D3DDISPLAYROTATION>(0);
    D3DDISPLAYMODEEX Desktop = {};
    D3DCAPS9 Caps = {};
    DWORD Flags = Parameters->Flags;
    HRESULT hr;

    if (!Parameters->Windowed)
    {
        D9_TRACE_ONCE(L"CreateDevice: an overlay swap chain must be windowed");
        return D3DERR_INVALIDCALL;
    }
    if (!IsEx)
    {
        D9_TRACE_ONCE(L"CreateDevice: an overlay swap chain needs the extended object");
        return D3DERR_INVALIDCALL;
    }
    if (Parameters->MultiSampleType != D3DMULTISAMPLE_NONE)
    {
        D9_TRACE_ONCE(L"CreateDevice: an overlay swap chain cannot be multisampled");
        return D3DERR_INVALIDCALL;
    }
    if ((BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) != 0)
    {
        D9_TRACE_ONCE(L"CreateDevice: an overlay swap chain keeps no present statistics");
        return D3DERR_INVALIDCALL;
    }

    hr = GetDeviceCaps(Adapter, DeviceType, &Caps);
    if (FAILED(hr))
        return hr;
    if ((Caps.Caps & D3DCAPS_OVERLAY) == 0)
    {
        D9_TRACE_ONCE(L"CreateDevice: the adapter has no overlays");
        return D3DERR_INVALIDCALL;
    }

    Desktop.Size = sizeof(Desktop);
    hr = GetAdapterDisplayModeEx(Adapter, &Desktop, &Rotation);
    if (FAILED(hr))
        return hr;

    Input.OverlayWidth = Parameters->BackBufferWidth;
    Input.OverlayHeight = Parameters->BackBufferHeight;
    Input.OverlayFormat = static_cast<D3DDDIFORMAT>(Parameters->BackBufferFormat);
    Input.DisplayWidth = Desktop.Width;
    Input.DisplayHeight = Desktop.Height;
    Input.DisplayRefreshRate = Desktop.RefreshRate;
    Input.DisplayFormat = static_cast<D3DDDIFORMAT>(Desktop.Format);
    Input.DisplayScanLineOrdering = static_cast<D3DDDI_SCANLINEORDERING>(Desktop.ScanLineOrdering);
    Input.DisplayRotation = static_cast<D3DDDI_ROTATION>(Rotation);
    hr = D9DriverGetCaps(&Adapters[Adapter], D3DDDICAPS_CHECKOVERLAYSUPPORT, &Input, &Support, sizeof(Support));
    if (FAILED(hr))
        return hr;

    /* Limited range RGB stands alone; the YCbCr choices each need the driver to offer them */
    if ((Flags & D3DPRESENTFLAG_OVERLAY_LIMITEDRGB) != 0)
    {
        if ((Flags & (D3DPRESENTFLAG_OVERLAY_YCbCr_xvYCC | D3DPRESENTFLAG_OVERLAY_YCbCr_BT709)) != 0 ||
            (Support.Caps & D3DOVERLAYCAPS_LIMITEDRANGERGB) == 0)
        {
            return D3DERR_INVALIDCALL;
        }
        return S_OK;
    }

    if ((Flags & D3DPRESENTFLAG_OVERLAY_YCbCr_BT709) != 0)
    {
        if ((Support.Caps & D3DOVERLAYCAPS_YCbCr_BT709) == 0)
            return D3DERR_INVALIDCALL;
    }
    else if ((Support.Caps & D3DOVERLAYCAPS_YCbCr_BT601) == 0)
    {
        return D3DERR_INVALIDCALL;
    }

    if ((Flags & D3DPRESENTFLAG_OVERLAY_YCbCr_xvYCC) != 0)
    {
        if ((Flags & D3DPRESENTFLAG_OVERLAY_YCbCr_BT709) != 0 && (Support.Caps & D3DOVERLAYCAPS_YCbCr_BT709_xvYCC) == 0)
            return D3DERR_INVALIDCALL;
        if ((Support.Caps & D3DOVERLAYCAPS_YCbCr_BT601_xvYCC) == 0)
            return D3DERR_INVALIDCALL;
    }
    return S_OK;
}

/* The fullscreen mode of one head must be the mode its back buffer describes */
static
HRESULT
D9MatchFullscreenMode(
    _In_ const D3DPRESENT_PARAMETERS* Parameters,
    _In_ const D3DDISPLAYMODEEX* Mode,
    _In_ D3DDISPLAYROTATION Rotation)
{
    UINT Width = Mode->Width;
    UINT Height = Mode->Height;

    if ((Parameters->Flags & D3DPRESENTFLAG_NOAUTOROTATE) != 0 && D9IsQuarterTurn(Rotation))
    {
        Width = Mode->Height;
        Height = Mode->Width;
    }

    if (Parameters->BackBufferWidth != Width || Parameters->BackBufferHeight != Height ||
        Parameters->BackBufferFormat != Mode->Format ||
        D9RoundTelevisionRate(Parameters->FullScreen_RefreshRateInHz) != D9RoundTelevisionRate(Mode->RefreshRate))
    {
        D9_TRACE_ONCE(L"CreateDevice: the back buffer does not describe the fullscreen mode");
        return D3DERR_INVALIDCALL;
    }

    if (Mode->Size != sizeof(*Mode) || Mode->Format == D3DFMT_UNKNOWN ||
        static_cast<LONG>(Mode->ScanLineOrdering) >= 3)
    {
        D9_TRACE_ONCE(L"CreateDevice: the fullscreen mode holds unknown values");
        return D3DERR_INVALIDCALL;
    }
    return S_OK;
}

/* The rules every head's presentation parameters follow; a windowed head may get its back buffer format filled in */
HRESULT
D9Enum::ValidatePresentation(
    _In_ UINT Adapter,
    _In_ UINT Heads,
    _In_ D3DDEVTYPE DeviceType,
    _In_ DWORD BehaviorFlags,
    _Inout_updates_(Heads) D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes)
{
    const D3DPRESENT_PARAMETERS* First = &Parameters[0];
    HRESULT hr;

    for (UINT Head = 0; Head < Heads; Head++)
    {
        D3DPRESENT_PARAMETERS* Present = &Parameters[Head];
        D3DDISPLAYROTATION Rotation = static_cast<D3DDISPLAYROTATION>(0);
        UINT Display = Adapter + Head;
        D3DFORMAT DisplayFormat;
        DWORD Levels = 0;
        DWORD Swap;

        if (Present->Windowed)
        {
            D3DDISPLAYMODE Desktop = {};

            hr = GetAdapterDisplayMode(Display, &Desktop);
            if (FAILED(hr))
                return hr;
            DisplayFormat = Desktop.Format;
            if (Present->BackBufferFormat == D3DFMT_UNKNOWN)
                Present->BackBufferFormat = DisplayFormat;
        }
        else
        {
            D3DDISPLAYMODEEX Desktop = {};

            Desktop.Size = sizeof(Desktop);
            hr = GetAdapterDisplayModeEx(Display, &Desktop, &Rotation);
            if (FAILED(hr))
                return hr;
            DisplayFormat = D9ScanOutFormat(Present->BackBufferFormat);
        }

        Swap = Present->SwapEffect;
        if (Swap == D3DSWAPEFFECT_OVERLAY)
        {
            hr = ValidateOverlay(Adapter, DeviceType, BehaviorFlags, Present);
        }
        else
        {
            if (!D9IsBackBufferFormat(Present->BackBufferFormat, IsEx) && DeviceType != D3DDEVTYPE_NULLREF)
            {
                D9_TRACE_ONCE(L"CreateDevice: back buffer format %u is not allowed", Present->BackBufferFormat);
                return D3DERR_INVALIDCALL;
            }
            if (Swap == D3DSWAPEFFECT_FLIPEX && !D9IsFlipExFormat(Present->BackBufferFormat))
            {
                D9_TRACE_ONCE(L"CreateDevice: back buffer format %u cannot flip", Present->BackBufferFormat);
                return D3DERR_INVALIDCALL;
            }
            hr = CheckDeviceType(Display, DeviceType, DisplayFormat, Present->BackBufferFormat, Present->Windowed);
        }
        if (FAILED(hr))
            return hr;

        if (Present->BackBufferCount != 0)
        {
            if (Swap == D3DSWAPEFFECT_COPY && Present->BackBufferCount > 1)
            {
                D9_TRACE_ONCE(L"CreateDevice: a copy swap chain has one back buffer");
                return D3DERR_INVALIDCALL;
            }
            if (Present->BackBufferCount > (IsEx ? D9MaxBackBuffersEx : D9MaxBackBuffers))
            {
                D9_TRACE_ONCE(L"CreateDevice: %u back buffers is too many", Present->BackBufferCount);
                return D3DERR_INVALIDCALL;
            }
        }

        if (FAILED(CheckDeviceMultiSampleType(Display, DeviceType, Present->BackBufferFormat, Present->Windowed,
                                              Present->MultiSampleType, &Levels)))
        {
            return D3DERR_INVALIDCALL;
        }
        if (Present->MultiSampleQuality >= Levels)
        {
            D9_TRACE_ONCE(L"CreateDevice: multisample quality %u is out of range", Present->MultiSampleQuality);
            return D3DERR_INVALIDCALL;
        }

        switch (Swap)
        {
        case D3DSWAPEFFECT_DISCARD:
        case D3DSWAPEFFECT_FLIP:
        case D3DSWAPEFFECT_COPY:
        case D3DSWAPEFFECT_OVERLAY:
        case 0x40000000:
            break;
        case D3DSWAPEFFECT_FLIPEX:
            if (IsEx)
                break;
            D9_TRACE_ONCE(L"CreateDevice: the flip model needs the extended object");
            return D3DERR_INVALIDCALL;
        default:
            D9_TRACE_ONCE(L"CreateDevice: swap effect %u is not defined", Swap);
            return D3DERR_INVALIDCALL;
        }

        if (Present->MultiSampleType != D3DMULTISAMPLE_NONE)
        {
            if (Swap != D3DSWAPEFFECT_DISCARD || (Present->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0)
            {
                D9_TRACE_ONCE(L"CreateDevice: multisampling needs a discarded, unlockable back buffer");
                return D3DERR_INVALIDCALL;
            }
        }

        if ((Present->Flags & ~D9KnownPresentFlags) != 0)
        {
            D9_TRACE_ONCE(L"CreateDevice: presentation flags 0x%08lX are not defined", Present->Flags);
            return D3DERR_INVALIDCALL;
        }
        if ((Present->Flags & D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL) != 0 && !Present->EnableAutoDepthStencil)
        {
            D9_TRACE_ONCE(L"CreateDevice: a discarded depth buffer needs an automatic one");
            return D3DERR_INVALIDCALL;
        }
        if (Swap == D3DSWAPEFFECT_FLIPEX &&
            (Present->Flags & (D3DPRESENTFLAG_LOCKABLE_BACKBUFFER | D3DPRESENTFLAG_DEVICECLIP)) != 0)
        {
            D9_TRACE_ONCE(L"CreateDevice: a flip model back buffer cannot be lockable or clipped");
            return D3DERR_INVALIDCALL;
        }

        if (Present->EnableAutoDepthStencil)
        {
            hr = CheckDeviceFormat(Display, DeviceType, DisplayFormat, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE,
                                   Present->AutoDepthStencilFormat);
            if (FAILED(hr))
                return hr;
            hr = CheckDepthStencilMatch(Display, DeviceType, DisplayFormat, Present->BackBufferFormat,
                                        Present->AutoDepthStencilFormat);
            if (FAILED(hr))
                return hr;
        }

        if (Present->Windowed)
        {
            const D3DCAPS9* Caps;

            hr = AdapterCaps(Adapter, DeviceType, &Caps);
            if (FAILED(hr))
                return hr;
            if (Present->FullScreen_RefreshRateInHz != 0 || FullscreenModes != nullptr)
            {
                D9_TRACE_ONCE(L"CreateDevice: a windowed device takes no fullscreen mode");
                return D3DERR_INVALIDCALL;
            }
            if (Present->PresentationInterval != D3DPRESENT_INTERVAL_DEFAULT &&
                Present->PresentationInterval != D3DPRESENT_INTERVAL_ONE &&
                Present->PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE)
            {
                D9_TRACE_ONCE(L"CreateDevice: interval 0x%08lX is not allowed", Present->PresentationInterval);
                return D3DERR_INVALIDCALL;
            }
            if (Heads > 1 || (Present->Flags & D3DPRESENTFLAG_NOAUTOROTATE) != 0)
            {
                D9_TRACE_ONCE(L"CreateDevice: adapter groups and fixed rotation need fullscreen");
                return D3DERR_INVALIDCALL;
            }
            continue;
        }

        /* Fullscreen intervals are held to what the driver itself reports */
        {
            DWORD Interval = Present->PresentationInterval;

            if (Interval == D3DPRESENT_INTERVAL_TWO || Interval == D3DPRESENT_INTERVAL_THREE ||
                Interval == D3DPRESENT_INTERVAL_FOUR)
            {
                if ((Interval & Adapters[Adapter].DriverCaps.PresentationIntervals) == 0)
                {
                    D9_TRACE_ONCE(L"CreateDevice: interval 0x%08lX is not supported", Interval);
                    return D3DERR_INVALIDCALL;
                }
            }
            else if (Interval != D3DPRESENT_INTERVAL_DEFAULT && Interval != D3DPRESENT_INTERVAL_ONE &&
                     Interval != D3DPRESENT_INTERVAL_IMMEDIATE)
            {
                D9_TRACE_ONCE(L"CreateDevice: interval 0x%08lX is not allowed", Interval);
                return D3DERR_INVALIDCALL;
            }
        }

        if (Swap == 0x40000000)
        {
            D9_TRACE_ONCE(L"CreateDevice: a swap chain that never presents must be windowed");
            return D3DERR_INVALIDCALL;
        }

        if (Present->EnableAutoDepthStencil != First->EnableAutoDepthStencil ||
            (Present->EnableAutoDepthStencil &&
             (Present->AutoDepthStencilFormat != First->AutoDepthStencilFormat ||
              Present->BackBufferFormat != First->BackBufferFormat ||
              Present->BackBufferWidth != First->BackBufferWidth ||
              Present->BackBufferHeight != First->BackBufferHeight)))
        {
            D9_TRACE_ONCE(L"CreateDevice: heads sharing a depth buffer must match");
            return D3DERR_INVALIDCALL;
        }

        if (IsEx)
        {
            if (FullscreenModes == nullptr)
            {
                D9_TRACE_ONCE(L"CreateDevice: a fullscreen extended device needs its mode");
                return D3DERR_INVALIDCALL;
            }
            hr = D9MatchFullscreenMode(Present, &FullscreenModes[Head], Rotation);
            if (FAILED(hr))
                return hr;
        }
        else if ((Present->Flags & D3DPRESENTFLAG_NOAUTOROTATE) != 0)
        {
            D9_TRACE_ONCE(L"CreateDevice: fixed rotation needs the extended object");
            return D3DERR_INVALIDCALL;
        }
    }
    return S_OK;
}

/* Reset holds new parameters to the same rules creation did */
HRESULT
D9Enum::ValidateReset(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ DWORD BehaviorFlags,
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode)
{
    D9LockScope Scope(&Lock);

    return ValidatePresentation(Adapter, 1, DeviceType, BehaviorFlags, Parameters, FullscreenMode);
}

/* The adapter, device type and behavior flag rules, then the presentation rules */
HRESULT
D9Enum::ValidateCreation(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ DWORD BehaviorFlags,
    _Inout_ D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ const D3DDISPLAYMODEEX* FullscreenModes)
{
    const D3DCAPS9* Caps;
    DWORD VertexProcessing;
    UINT Heads;

    if (Adapter >= AdapterCount && !(AdapterCount == 0 && DeviceType == D3DDEVTYPE_NULLREF && Adapter == 0))
    {
        D9_TRACE_ONCE(L"CreateDevice: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    switch (DeviceType)
    {
    case D3DDEVTYPE_HAL:
    case D3DDEVTYPE_REF:
    case D3DDEVTYPE_NULLREF:
        break;
    case D3DDEVTYPE_SW:
        D9_TRACE_ONCE(L"CreateDevice: no software device was registered");
        return D3DERR_INVALIDCALL;
    default:
        D9_TRACE_ONCE(L"CreateDevice: invalid device type %u", DeviceType);
        return D3DERR_INVALIDCALL;
    }

    Heads = 1;
    if ((BehaviorFlags & D3DCREATE_ADAPTERGROUP_DEVICE) != 0)
    {
        if (Adapters[Adapter].MasterOrdinal != Adapter)
        {
            D9_TRACE_ONCE(L"CreateDevice: adapter %u does not lead its group", Adapter);
            return D3DERR_INVALIDCALL;
        }
        Heads = HeadCount(Adapter, BehaviorFlags);
        if (Heads == 1)
            BehaviorFlags &= ~D3DCREATE_ADAPTERGROUP_DEVICE;
    }

    /* The system opens the adapter's driver here; the reference devices are not part of ReactX yet */
    if (DeviceType != D3DDEVTYPE_HAL)
    {
        D9_TRACE_ONCE(L"CreateDevice: the reference rasterizer is not implemented yet");
        return D3DERR_NOTAVAILABLE;
    }
    if (!Adapters[Adapter].HasDriver)
        return D3DERR_NOTAVAILABLE;
    Caps = &Adapters[Adapter].DriverCaps;

    if ((BehaviorFlags & D9UndefinedBehaviorFlags) != 0)
    {
        D9_TRACE_ONCE(L"CreateDevice: behavior flags 0x%08lX are not defined", BehaviorFlags);
        return D3DERR_INVALIDCALL;
    }

    VertexProcessing = BehaviorFlags & D9VertexProcessingFlags;
    if (VertexProcessing != D3DCREATE_SOFTWARE_VERTEXPROCESSING &&
        VertexProcessing != D3DCREATE_HARDWARE_VERTEXPROCESSING && VertexProcessing != D3DCREATE_MIXED_VERTEXPROCESSING)
    {
        D9_TRACE_ONCE(L"CreateDevice: exactly one kind of vertex processing must be asked for");
        return D3DERR_INVALIDCALL;
    }
    if (VertexProcessing == D3DCREATE_SOFTWARE_VERTEXPROCESSING)
    {
        if ((BehaviorFlags & D3DCREATE_PUREDEVICE) != 0)
        {
            D9_TRACE_ONCE(L"CreateDevice: a pure device cannot process vertices in software");
            return D3DERR_INVALIDCALL;
        }
    }
    else if ((Caps->DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0 ||
             (VertexProcessing == D3DCREATE_MIXED_VERTEXPROCESSING && (BehaviorFlags & D3DCREATE_PUREDEVICE) != 0))
    {
        D9_TRACE_ONCE(L"CreateDevice: the device cannot process vertices in hardware this way");
        return D3DERR_INVALIDCALL;
    }

    return ValidatePresentation(Adapter, Heads, DeviceType, BehaviorFlags, Parameters, FullscreenModes);
}

/* What a hardware device creation checks the adapter's caps for */
HRESULT
D9Enum::AdapterCaps(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _Outptr_ const D3DCAPS9** Caps)
{
    *Caps = nullptr;
    if (DeviceType != D3DDEVTYPE_HAL || SoftwareOnly)
        return D3DERR_NOTAVAILABLE;

    *Caps = &Adapters[Adapter].DriverCaps;
    return S_OK;
}

/* Window rules that come after the argument checks */
HRESULT
D9Enum::ValidateWindows(
    _In_ UINT Heads,
    _In_opt_ HWND FocusWindow,
    _In_reads_(Heads) const D3DPRESENT_PARAMETERS* Parameters)
{
    if (FocusWindow != NULL && !IsWindow(FocusWindow))
    {
        D9_TRACE_ONCE(L"CreateDevice: the focus window is not a window");
        return D3DERR_INVALIDCALL;
    }

    for (UINT Head = 0; Head < Heads; Head++)
    {
        HWND Window = Parameters[Head].hDeviceWindow;

        if (Window != NULL && !IsWindow(Window))
        {
            D9_TRACE_ONCE(L"CreateDevice: the device window is not a window");
            return D3DERR_INVALIDCALL;
        }

        /* The extended object lets a device go without a focus window */
        if (FocusWindow == NULL)
        {
            if (IsEx)
                continue;
            if (!Parameters[Head].Windowed)
            {
                D9_TRACE_ONCE(L"CreateDevice: a fullscreen device needs a focus window");
                return D3DERR_INVALIDCALL;
            }
        }

        if (Head != 0 && (Window == NULL || Window == Parameters[0].hDeviceWindow || Window == FocusWindow))
        {
            D9_TRACE_ONCE(L"CreateDevice: each extra head needs its own device window");
            return D3DERR_INVALIDCALL;
        }
    }
    return S_OK;
}
