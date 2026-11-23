/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Swap chains: the back buffers of a window and their presentation
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "kmt.hpp"

typedef NTSTATUS (APIENTRY* PD9_GET_SCAN_LINE)(D3DKMT_GETSCANLINE*);

/* A present flag the runtime keeps for itself and lets through */
static constexpr DWORD D9PresentFlagsRuntime = 0x80000000;

D9SwapChain::D9SwapChain(
    _In_ D9Device* Owner,
    _In_ UINT Position)
    : D9Child(Owner),
      Index(Position),
      FocusWindow(Owner->Focus())
{
}

D9SwapChain::~D9SwapChain()
{
    ReleaseBuffers();
}

/* The buffers go newest first */
VOID
D9SwapChain::ReleaseBuffers(VOID)
{
    while (BackBufferCount > 0)
    {
        BackBufferCount -= 1;
        BackBuffers[BackBufferCount]->Abandon();
        BackBuffers[BackBufferCount] = nullptr;
    }
}

/* Whether the application holds any buffer of the chain */
BOOLEAN
D9SwapChain::BuffersHeld(VOID) const
{
    for (UINT Buffer = 0; Buffer < BackBufferCount; Buffer++)
    {
        if (BackBuffers[Buffer]->IsHeld())
            return TRUE;
    }
    return FALSE;
}

/* A flip hands each buffer the surface of the one after it; the first one's surface moves to the last */
VOID
D9SwapChain::RotateBuffers(VOID)
{
    D9Resource* Moving;

    if (BackBufferCount <= 1)
        return;

    Moving = BackBuffers[0]->DriverResource();
    for (UINT Remaining = BackBufferCount; Remaining > 0; Remaining -= 1)
        BackBuffers[Remaining - 1]->ExchangeResource(&Moving);
    Device->NoteTargetsMoved();
}

/* Builds the back buffers the parameters ask for; zero sizes take the window's client area */
HRESULT
D9SwapChain::Reset(
    _Inout_ D3DPRESENT_PARAMETERS* Requested)
{
    HWND Target = Requested->hDeviceWindow != NULL ? Requested->hDeviceWindow : FocusWindow;
    D3DSURFACE_DESC Desc = {};
    RECT Client = {};
    UINT Count;
    HRESULT hr;

    if (Requested->BackBufferCount == 0)
        Requested->BackBufferCount = 1;
    if (Target == NULL)
    {
        D9_TRACE_ONCE(L"CreateDevice: a windowed device needs a device or focus window");
        return D3DERR_INVALIDCALL;
    }

    if (Requested->BackBufferWidth == 0 || Requested->BackBufferHeight == 0)
    {
        GetClientRect(Target, &Client);
        if (Requested->BackBufferWidth == 0)
            Requested->BackBufferWidth = Client.right > 0 ? Client.right : 1;
        if (Requested->BackBufferHeight == 0)
            Requested->BackBufferHeight = Client.bottom > 0 ? Client.bottom : 1;
    }
    if ((Requested->Flags & D3DPRESENTFLAG_RESTRICT_SHARED_RESOURCE_DRIVER) != 0)
    {
        D9_TRACE_ONCE(L"CreateDevice: restricting shared resources to the driver is not supported");
        return D3DERR_INVALIDCALL;
    }

    ReleaseBuffers();
    Device->FlushDriver();
    Desc.Format = Requested->BackBufferFormat;
    Desc.Type = D3DRTYPE_SURFACE;
    Desc.Usage = D3DUSAGE_RENDERTARGET;
    Desc.Pool = D3DPOOL_DEFAULT;
    Desc.MultiSampleType = Requested->MultiSampleType;
    Desc.MultiSampleQuality = Requested->MultiSampleQuality;
    Desc.Width = Requested->BackBufferWidth;
    Desc.Height = Requested->BackBufferHeight;

    /* Flip swap chains also make the buffer the window shows */
    Count = Requested->BackBufferCount;
    if (Requested->SwapEffect == D3DSWAPEFFECT_FLIP || Requested->SwapEffect == D3DSWAPEFFECT_FLIPEX)
        Count += 1;
    for (UINT Buffer = 0; Buffer < Count; Buffer++)
    {
        D9Surface* Surface = new (std::nothrow) D9Surface(Device, static_cast<IDirect3DSwapChain9*>(this), &Desc,
                                                          (Requested->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) != 0,
                                                          Requested->SwapEffect == D3DSWAPEFFECT_DISCARD);

        if (Surface == nullptr)
            return E_OUTOFMEMORY;
        BackBuffers[BackBufferCount] = Surface;
        BackBufferCount += 1;

        hr = Surface->Create();
        if (FAILED(hr))
            return hr;
    }

    Parameters = *Requested;
    return S_OK;
}

STDMETHODIMP
D9SwapChain::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DSwapChain9)) ||
        (Device->IsExtended() && IsEqualGUID(Iid, __uuidof(IDirect3DSwapChain9Ex))))
    {
        *Object = static_cast<IDirect3DSwapChain9Ex*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9SwapChain::AddRef()
{
    return AddRefChild();
}

STDMETHODIMP_(ULONG)
D9SwapChain::Release()
{
    return ReleaseChild();
}

/* The present flags each swap effect and window mode allow */
static
HRESULT
D9ValidatePresent(
    _In_ const D3DPRESENT_PARAMETERS* Parameters,
    _In_ BOOLEAN IsEx,
    _In_opt_ const RECT* SourceRect,
    _In_opt_ const RECT* DestRect,
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    DWORD Swap = Parameters->SwapEffect;

    if (Swap == D3DSWAPEFFECT_FLIP && (SourceRect != nullptr || DestRect != nullptr || DirtyRegion != nullptr))
        return D3DERR_INVALIDCALL;
    if (Swap == D3DSWAPEFFECT_FLIPEX &&
        (SourceRect != nullptr || DestRect != nullptr || (Flags & D3DPRESENT_LINEAR_CONTENT) != 0))
    {
        return D3DERR_INVALIDCALL;
    }
    if ((Flags & D3DPRESENT_DONOTFLIP) != 0)
    {
        if ((Swap == D3DSWAPEFFECT_FLIP && Parameters->Windowed) ||
            (Swap != D3DSWAPEFFECT_FLIP && Swap != D3DSWAPEFFECT_FLIPEX) || !IsEx)
        {
            return D3DERR_INVALIDCALL;
        }
    }
    if ((Flags & D3DPRESENT_FLIPRESTART) != 0 && (Swap != D3DSWAPEFFECT_FLIP || Parameters->Windowed || !IsEx))
        return D3DERR_INVALIDCALL;
    if ((Flags & D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR) != 0 &&
        (!Parameters->Windowed || (Parameters->Flags & D3DPRESENTFLAG_VIDEO) == 0))
    {
        return D3DERR_INVALIDCALL;
    }
    return S_OK;
}

HRESULT
D9SwapChain::PresentWindowed(
    _In_opt_ const RECT* SourceRect,
    _In_opt_ const RECT* DestRect,
    _In_opt_ HWND WindowOverride,
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    HWND Target = WindowOverride != NULL ? WindowOverride : Window();
    RECT From;
    RECT To;
    RECT Client = {};
    HRESULT hr;

    hr = D9ValidatePresent(&Parameters, Device->IsExtended(), SourceRect, DestRect, DirtyRegion, Flags);
    if (FAILED(hr))
    {
        D9_TRACE_ONCE(L"Present: flags 0x%08lX do not fit the swap chain", Flags);
        return hr;
    }
    if (BackBufferCount == 0 || (Parameters.SwapEffect & 0x40000000) != 0)
        return S_OK;

    SetRect(&From, 0, 0, Parameters.BackBufferWidth, Parameters.BackBufferHeight);
    if (SourceRect != nullptr && !IntersectRect(&From, &From, SourceRect))
        return S_OK;

    GetClientRect(Target, &Client);
    To = DestRect != nullptr ? *DestRect : Client;
    if (IsRectEmpty(&To))
        return D9CheckOcclusion(Target) == S_PRESENT_OCCLUDED ? S_PRESENT_OCCLUDED : S_OK;

    hr = Device->PresentToWindow(BackBuffers[0]->DriverResource(), Target, &From, &To);
    if (SUCCEEDED(hr) && Parameters.SwapEffect == D3DSWAPEFFECT_FLIP)
        RotateBuffers();
    return hr;
}

STDMETHODIMP
D9SwapChain::Present(
    _In_opt_ const RECT* SourceRect,
    _In_opt_ const RECT* DestRect,
    _In_opt_ HWND WindowOverride,
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    return PresentWindowed(SourceRect, DestRect, WindowOverride, DirtyRegion, Flags);
}

STDMETHODIMP
D9SwapChain::GetFrontBufferData(
    _In_ IDirect3DSurface9* Destination)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
    D9Surface* Target = static_cast<D9Surface*>(Destination);
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATEDCFROMMEMORY Create = {};
    D3DKMT_DESTROYDCFROMMEMORY Destroy = {};
    D3DLOCKED_RECT Locked = {};
    D3DDISPLAYMODE Screen = {};
    PCSTR Display = Device->DisplayName();
    HDC ScreenDc;
    HRESULT hr;

    if (Target == nullptr || Target->Owner() != Device || Kmt == nullptr || Kmt->CreateDCFromMemory == nullptr)
        return D3DERR_INVALIDCALL;

    /* A windowed chain copies what the screen shows into an A8R8G8B8 memory surface at least as large */
    const D3DSURFACE_DESC& Desc = Target->Describe();
    if (Desc.Format != D3DFMT_A8R8G8B8 || Desc.Type != D3DRTYPE_SURFACE ||
        (Desc.Pool != D3DPOOL_SYSTEMMEM && Desc.Pool != D3DPOOL_SCRATCH))
    {
        D9_TRACE_ONCE(L"GetFrontBufferData: the destination must be an A8R8G8B8 memory surface");
        return D3DERR_INVALIDCALL;
    }
    hr = GetDisplayMode(&Screen);
    if (FAILED(hr))
        return hr;
    if (Desc.Width < Screen.Width || Desc.Height < Screen.Height)
    {
        D9_TRACE_ONCE(L"GetFrontBufferData: the destination is smaller than the screen");
        return D3DERR_INVALIDCALL;
    }

    hr = Target->LockRect(&Locked, nullptr, 0);
    if (FAILED(hr))
        return hr;
    if (Display[0] == '\\' && Display[1] == '\\' && Display[2] == '.')
        ScreenDc = CreateDCA(nullptr, Display, nullptr, nullptr);
    else
        ScreenDc = CreateDCA(Display, nullptr, nullptr, nullptr);
    Create.pMemory = Locked.pBits;
    Create.Format = D3DDDIFMT_A8R8G8B8;
    Create.Width = Desc.Width;
    Create.Height = Desc.Height;
    Create.Pitch = static_cast<UINT>(Locked.Pitch);
    Create.hDeviceDc = ScreenDc;
    if (!NT_SUCCESS(Kmt->CreateDCFromMemory(&Create)))
    {
        hr = D3DERR_INVALIDCALL;
    }
    else
    {
        hr = BitBlt(Create.hDc, 0, 0, static_cast<INT>(Screen.Width), static_cast<INT>(Screen.Height), ScreenDc, 0,
                    0, SRCCOPY) ? S_OK : D3DERR_INVALIDCALL;
        Destroy.hDc = Create.hDc;
        Destroy.hBitmap = Create.hBitmap;
        Kmt->DestroyDCFromMemory(&Destroy);
        if (SUCCEEDED(hr))
            hr = Device->ExecutionStatus();
    }
    if (ScreenDc != NULL)
        DeleteDC(ScreenDc);
    Target->UnlockRect();
    if (FAILED(hr))
        D9Trace(L"GetFrontBufferData: the screen could not be copied");
    return hr;
}

STDMETHODIMP
D9SwapChain::GetBackBuffer(
    _In_ UINT Buffer,
    _In_ D3DBACKBUFFER_TYPE Type,
    _COM_Outptr_ IDirect3DSurface9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = nullptr;
    if (Buffer >= BackBufferCount || Buffer >= Parameters.BackBufferCount)
    {
        D9_TRACE_ONCE(L"GetBackBuffer: back buffer %u does not exist", Buffer);
        return D3DERR_INVALIDCALL;
    }

    *Result = BackBuffers[Buffer];
    BackBuffers[Buffer]->AddRef();
    return S_OK;
}

STDMETHODIMP
D9SwapChain::GetRasterStatus(
    _Out_ D3DRASTER_STATUS* Status)
{
    static PD9_GET_SCAN_LINE GetScanLine =
        reinterpret_cast<PD9_GET_SCAN_LINE>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"), "D3DKMTGetScanLine"));
    const D9Adapter* Adapter = Device->AdapterInfo();
    D3DKMT_GETSCANLINE Scan = {};

    if (Status == nullptr)
        return D3DERR_INVALIDCALL;

    ZeroMemory(Status, sizeof(*Status));
    if (GetScanLine == nullptr)
        return S_OK;

    Scan.hAdapter = Adapter->KernelAdapter;
    Scan.VidPnSourceId = Adapter->SourceId;
    if (NT_SUCCESS(GetScanLine(&Scan)))
    {
        Status->InVBlank = Scan.InVerticalBlank;
        Status->ScanLine = Scan.ScanLine;
    }
    return S_OK;
}

STDMETHODIMP
D9SwapChain::GetDisplayMode(
    _Out_ D3DDISPLAYMODE* Mode)
{
    if (Mode == nullptr)
        return D3DERR_INVALIDCALL;
    return Device->Owner()->GetAdapterDisplayMode(Device->AdapterInfo()->Ordinal, Mode);
}

STDMETHODIMP
D9SwapChain::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

STDMETHODIMP
D9SwapChain::GetPresentParameters(
    _Out_ D3DPRESENT_PARAMETERS* Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Parameters;
    return S_OK;
}

STDMETHODIMP
D9SwapChain::GetLastPresentCount(
    _Out_ UINT* Count)
{
    D3DDEVICE_CREATION_PARAMETERS Creation = {};

    Device->GetCreationParameters(&Creation);
    if (Count == nullptr || (Creation.BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) == 0)
        return D3DERR_INVALIDCALL;

    RX_UNIMPLEMENTED();
    *Count = 0;
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9SwapChain::GetPresentStats(
    _Out_ D3DPRESENTSTATS* Stats)
{
    D3DDEVICE_CREATION_PARAMETERS Creation = {};

    Device->GetCreationParameters(&Creation);
    if (Stats == nullptr || (Creation.BehaviorFlags & D3DCREATE_ENABLE_PRESENTSTATS) == 0)
        return D3DERR_INVALIDCALL;

    RX_UNIMPLEMENTED();
    return D3DERR_INVALIDCALL;
}

STDMETHODIMP
D9SwapChain::GetDisplayModeEx(
    _Out_ D3DDISPLAYMODEEX* Mode,
    _Out_opt_ D3DDISPLAYROTATION* Rotation)
{
    if (Mode == nullptr)
        return D3DERR_INVALIDCALL;
    return Device->Owner()->GetAdapterDisplayModeEx(Device->AdapterInfo()->Ordinal, Mode, Rotation);
}
