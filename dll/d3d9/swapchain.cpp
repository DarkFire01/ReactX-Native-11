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

/* Windowed flip model chains with buffers this small also look the adapter up, as the runtime does */
static constexpr UINT64 D9IdentifiedBufferArea = 0x10000;

static
BOOLEAN
D9ComposesDirectly(
    _In_ const D3DPRESENT_PARAMETERS* Parameters);

static
HRESULT
D9ShareWithCompositor(
    _In_ D3DKMT_HANDLE Object,
    _In_ PSECURITY_DESCRIPTOR Security,
    _Out_ HANDLE* Handle);

D9SwapChain::D9SwapChain(
    _In_ D9Device* Owner,
    _In_ UINT Position)
    : D9Child(Owner),
      Index(Position),
      FocusWindow(Owner->Focus())
{
    /* Each channel starts as its own index, not scaled to the full sixteen bits */
    for (UINT Entry = 0; Entry < 256; Entry++)
    {
        Gamma.red[Entry] = static_cast<WORD>(Entry);
        Gamma.green[Entry] = static_cast<WORD>(Entry);
        Gamma.blue[Entry] = static_cast<WORD>(Entry);
    }
}

D9SwapChain::~D9SwapChain()
{
    Cursor.Release();
    ReleaseBuffers();
}

/* The buffers go newest first, after what the compositor held */
VOID
D9SwapChain::ReleaseBuffers(VOID)
{
    ReleaseComposition();
    while (BackBufferCount > 0)
    {
        BackBufferCount -= 1;
        if (BufferHandles[BackBufferCount] != NULL)
        {
            CloseHandle(BufferHandles[BackBufferCount]);
            BufferHandles[BackBufferCount] = NULL;
        }
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
    const D9CompositionCalls* Calls = D9Composition();
    PSECURITY_DESCRIPTOR Security = nullptr;
    D3DSURFACE_DESC Desc = {};
    RECT Client = {};
    BOOLEAN DirectFlip = FALSE;
    UINT Count;
    HRESULT hr = S_OK;

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

    Cursor.Release();
    ReleaseBuffers();
    Device->FlushDriver();
    Parameters = *Requested;
    FlipPresents = 0;
    StatsReported = 0;
    StatsDisjoint = FALSE;
    PresentMark = 0;
    ComposedIndex = 0;
    FenceValue = 0;
    FenceWaited = 0;
    ZeroMemory(&CachedStats, sizeof(CachedStats));
    ZeroMemory(&LastStats, sizeof(LastStats));

    /* A flip model chain the compositor shows needs DWM's security on what it shares */
    Composed = D9ComposesDirectly(Requested);
    if (Composed)
    {
        DirectFlip = MatchesDirectFlip();
        hr = Calls->AllocateSecurity != nullptr ? Calls->AllocateSecurity(&Security, GENERIC_ALL) : E_NOTIMPL;
        if (FAILED(hr))
        {
            Composed = FALSE;
            return hr;
        }
        if (static_cast<UINT64>(Requested->BackBufferWidth) * Requested->BackBufferHeight < D9IdentifiedBufferArea)
        {
            D3DADAPTER_IDENTIFIER9 Identifier;

            Device->Owner()->GetAdapterIdentifier(Device->AdapterInfo()->Ordinal, 0, &Identifier);
        }
    }

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
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        BackBuffers[BackBufferCount] = Surface;
        BackBufferCount += 1;

        if (Composed)
        {
            D3DKMT_UMDFILENAMEINFO Name;

            D9QueryUmdName(Device->AdapterInfo()->KernelAdapter, KMTUMDVERSION_DX9, &Name);
            Surface->AssignRole(D9SurfaceRole::CompositionBuffer);
        }
        hr = Surface->Create();
        if (SUCCEEDED(hr) && Composed)
            hr = D9ShareWithCompositor(Surface->DriverResource()->KernelResource, Security, &BufferHandles[Buffer]);
        if (FAILED(hr))
            break;
    }
    if (SUCCEEDED(hr) && Composed)
        hr = CreateClearTargets();
    if (SUCCEEDED(hr) && Composed)
        hr = CreateComposition(Security, DirectFlip);
    if (SUCCEEDED(hr) && Composed)
        hr = BindComposition();
    if (Security != nullptr && Calls->FreeSecurity != nullptr)
        Calls->FreeSecurity(Security);
    if (FAILED(hr))
        return hr;

    /* The cursor starts in the middle of the back buffers, or of the display a fullscreen chain takes */
    if (Parameters.Windowed)
    {
        Cursor.Start(Device, static_cast<INT>(Parameters.BackBufferWidth / 2),
                     static_cast<INT>(Parameters.BackBufferHeight / 2), TRUE);
    }
    else
    {
        D3DDISPLAYMODE Mode = {};

        Device->Owner()->GetAdapterDisplayMode(Device->AdapterInfo()->Ordinal, &Mode);
        Cursor.Start(Device, static_cast<INT>(Mode.Width / 2), static_cast<INT>(Mode.Height / 2), FALSE);
    }
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
    _In_opt_ HWND WindowOverride,
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    DWORD Swap = Parameters->SwapEffect;

    if (Swap == D3DSWAPEFFECT_FLIP && (SourceRect != nullptr || DestRect != nullptr || DirtyRegion != nullptr))
        return D3DERR_INVALIDCALL;
    if (Swap == D3DSWAPEFFECT_FLIPEX && (SourceRect != nullptr || DestRect != nullptr || WindowOverride != NULL ||
                                         (Flags & D3DPRESENT_LINEAR_CONTENT) != 0))
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

    hr = D9ValidatePresent(&Parameters, Device->IsExtended(), SourceRect, DestRect, WindowOverride, DirtyRegion,
                           Flags);
    if (FAILED(hr))
    {
        D9_TRACE_ONCE(L"Present: flags 0x%08lX do not fit the swap chain", Flags);
        return hr;
    }
    if (BackBufferCount == 0 || (Parameters.SwapEffect & 0x40000000) != 0)
        return S_OK;
    if (Composed)
        return Device->NoteDisplayChange(PresentComposed(DirtyRegion, Flags));

    SetRect(&From, 0, 0, Parameters.BackBufferWidth, Parameters.BackBufferHeight);
    if (SourceRect != nullptr && !IntersectRect(&From, &From, SourceRect))
        return S_OK;

    GetClientRect(Target, &Client);
    To = DestRect != nullptr ? *DestRect : Client;
    if (IsRectEmpty(&To))
        return D9CheckOcclusion(Target) == S_PRESENT_OCCLUDED ? S_PRESENT_OCCLUDED : S_OK;

    hr = Device->NoteDisplayChange(Device->PresentToWindow(BackBuffers[0]->DriverResource(), Target, &From, &To));
    if (SUCCEEDED(hr) && Parameters.SwapEffect == D3DSWAPEFFECT_FLIP)
        RotateBuffers();

    /* Statistics of a flip chain break off at its first present */
    if (SUCCEEDED(hr) && Parameters.SwapEffect == D3DSWAPEFFECT_FLIPEX)
    {
        if (FlipPresents == 0)
            StatsDisjoint = TRUE;
        FlipPresents += 1;
    }
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

/* A windowed chain counts only the presents of the flip model; others report none */
STDMETHODIMP
D9SwapChain::GetLastPresentCount(
    _Out_ UINT* Count)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    if (!Device->KeepsPresentStats())
    {
        D9_TRACE_ONCE(L"GetLastPresentCount: the device keeps no present statistics");
        return D3DERR_INVALIDCALL;
    }
    if (!Parameters.Windowed)
    {
        D9_TRACE_ONCE(L"GetLastPresentCount: fullscreen present counts are not kept");
        *Count = 0;
        return S_OK;
    }
    if (Parameters.SwapEffect != D3DSWAPEFFECT_FLIPEX)
        *Count = 0;
    else
        *Count = Composed ? PresentMark : FlipPresents;
    return S_OK;
}

/* Windowed chains have statistics only in the flip model, where the compositor keeps them */
STDMETHODIMP
D9SwapChain::GetPresentStats(
    _Out_ D3DPRESENTSTATS* Stats)
{
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
    HRESULT hr = S_OK;

    if (!Device->KeepsPresentStats())
    {
        D9_TRACE_ONCE(L"GetPresentStats: the device keeps no present statistics");
        return D3DERR_INVALIDCALL;
    }
    ZeroMemory(Stats, sizeof(*Stats));
    if (!Parameters.Windowed)
    {
        D9_TRACE_ONCE(L"GetPresentStats: fullscreen statistics are not kept");
        return D3DERR_NOTAVAILABLE;
    }
    if (Parameters.SwapEffect != D3DSWAPEFFECT_FLIPEX)
        return D3DERR_NOTAVAILABLE;
    if (Composed)
        return ComposedStats(Stats);
    if (StatsReported == FlipPresents)
        return S_OK;
    if (StatsDisjoint)
    {
        StatsDisjoint = FALSE;
        hr = D3DERR_PRESENT_STATISTICS_DISJOINT;
    }
    return hr;
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

/* Flip model composition */

/* What the composition objects are opened with, and how many buffers one binding takes */
static constexpr DWORD D9SharedObjectAccess = STANDARD_RIGHTS_REQUIRED | GENERIC_READ | GENERIC_WRITE | GENERIC_ALL;
static constexpr DWORD D9CompositionSurfaceAccess = 3;
static constexpr UINT D9BindableBuffers = 31;
static constexpr UINT D9ClearTargetSize = 16;

/* Display drivers of this model and later get a monitored fence the compositor waits on */
static constexpr UINT D9MonitoredFenceModel = 2000;

/* A display configuration query only the runtime makes, and where its answer says the output is cloned */
static constexpr UINT D9OutputInfoType = 0xFFFFFFFE;
static constexpr UINT D9OutputInfoSize = 0x808;
static constexpr UINT D9OutputInfoClone = 1644;

/* The layout NtBindCompositionSurface reads */
static constexpr UINT D9BindInfoSize = 0x518;
static constexpr UINT D9BindMonitor = 0x10;
static constexpr UINT D9BindOpacity = 0x4C;
static constexpr UINT D9BindScaleX = 0x50;
static constexpr UINT D9BindScaleY = 0x5C;
static constexpr UINT D9BindFlags = 0x90;
static constexpr UINT D9BindKind = 0x98;
static constexpr UINT D9BindBufferCount = 0x9C;
static constexpr UINT D9BindBuffers = 0xA0;
static constexpr UINT D9BindWindow = 0x480;
static constexpr UINT D9BindSemaphore = 0x488;
static constexpr UINT D9BindFence = 0x490;
static constexpr DWORD D9BindDirectFlip = 0x10;
static constexpr DWORD D9BindOverlays = 0x40;
static constexpr DWORD D9BindWindowed = 0x200;

/* One buffer of the binding: its NT handle and the adapter it lives on */
struct D9BoundBuffer
{
    HANDLE Handle;
    LUID Adapter;
    UINT64 Reserved[2];
};

static_assert(sizeof(D9BoundBuffer) == 32);

/* The kernel's statistics of a composition surface, as far as Direct3D 9 reads them */
static constexpr UINT D9SurfaceStatisticsSize = 80;
static constexpr UINT D9StatisticsPresentCount = 0x18;
static constexpr UINT D9StatisticsPresentRefresh = 0x1C;
static constexpr UINT D9StatisticsSyncRefresh = 0x20;
static constexpr UINT D9StatisticsSyncTime = 0x28;

template <class Value>
static
inline
VOID
D9PutField(
    _Out_writes_bytes_(Offset + sizeof(Value)) BYTE* Block,
    _In_ UINT Offset,
    _In_ Value Field)
{
    CopyMemory(Block + Offset, &Field, sizeof(Field));
}

template <class Value>
static
inline
Value
D9GetField(
    _In_reads_bytes_(Offset + sizeof(Value)) const BYTE* Block,
    _In_ UINT Offset)
{
    Value Field;

    CopyMemory(&Field, Block + Offset, sizeof(Field));
    return Field;
}

/* Flip model presents of a windowed chain go through the compositor unless the buffers need a copy first */
static
BOOLEAN
D9ComposesDirectly(
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    return Parameters->SwapEffect == D3DSWAPEFFECT_FLIPEX && Parameters->Windowed &&
           (Parameters->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER) == 0 &&
           Parameters->MultiSampleType == D3DMULTISAMPLE_NONE && D9IsFlipExFormat(Parameters->BackBufferFormat);
}

static
inline
BOOLEAN
D9SameLuid(
    _In_ const LUID& Left,
    _In_ const LUID& Right)
{
    return Left.LowPart == Right.LowPart && Left.HighPart == Right.HighPart;
}

/* Whether an output shows the buffers unscaled: same size, not cloned for a small chain, rotated only when the
   application handles rotation */
static
BOOLEAN
D9OutputFitsBuffers(
    _In_ const D9Adapter* Entry,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    BYTE Info[D9OutputInfoSize] = {};
    DISPLAYCONFIG_DEVICE_INFO_HEADER* Header = reinterpret_cast<DISPLAYCONFIG_DEVICE_INFO_HEADER*>(Info);
    D3DKMT_CURRENTDISPLAYMODE Current = {};

    Header->type = static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(D9OutputInfoType);
    Header->size = D9OutputInfoSize;
    Header->adapterId = Entry->AdapterLuid;
    Header->id = Entry->SourceId;
    if (DisplayConfigGetDeviceInfo(Header) != ERROR_SUCCESS)
        return FALSE;
    if (Parameters->BackBufferCount <= 2 && (D9GetField<DWORD>(Info, D9OutputInfoClone) != 0 ||
                                             D9GetField<DWORD>(Info, D9OutputInfoClone + 4) != 0))
    {
        return FALSE;
    }

    Current.VidPnSourceId = Entry->SourceId;
    if (!NT_SUCCESS(D9QueryKernel(Entry->KernelAdapter, KMTQAITYPE_CURRENTDISPLAYMODE, &Current, sizeof(Current))))
        return FALSE;
    if (Current.DisplayMode.Width != Parameters->BackBufferWidth ||
        Current.DisplayMode.Height != Parameters->BackBufferHeight)
    {
        return FALSE;
    }
    return Current.DisplayMode.DisplayOrientation == D3DDDI_ROTATION_IDENTITY ||
           (Parameters->Flags & D3DPRESENTFLAG_NOAUTOROTATE) != 0;
}

/* A hardware device can flip a chain straight to the screen when an output of its adapter matches the buffers;
   the window's own output is tried first */
BOOLEAN
D9SwapChain::MatchesDirectFlip(VOID) const
{
    const D9Adapter* Ours = Device->AdapterInfo();
    const D9Enum* Enum = Device->Owner();
    MONITORINFOEXA Monitor = {};
    HMONITOR Shown;
    const D9Adapter* Entry;

    if (Device->Type() != D3DDEVTYPE_HAL || !Ours->DirectFlip)
        return FALSE;

    Shown = MonitorFromWindow(Window(), MONITOR_DEFAULTTONULL);
    Monitor.cbSize = sizeof(Monitor);
    if (Shown != NULL && GetMonitorInfoA(Shown, &Monitor))
    {
        for (UINT Position = 0; (Entry = Enum->AdapterEntry(Position)) != nullptr; Position++)
        {
            if (lstrcmpA(Monitor.szDevice, Entry->DeviceName) != 0)
                continue;
            if (D9SameLuid(Entry->AdapterLuid, Ours->AdapterLuid) && D9OutputFitsBuffers(Entry, &Parameters))
                return TRUE;
            break;
        }
    }
    for (UINT Position = 0; (Entry = Enum->AdapterEntry(Position)) != nullptr; Position++)
    {
        if (D9SameLuid(Entry->AdapterLuid, Ours->AdapterLuid) && D9OutputFitsBuffers(Entry, &Parameters))
            return TRUE;
    }
    return FALSE;
}

/* An NT handle the compositor opens a buffer or the fence by */
static
HRESULT
D9ShareWithCompositor(
    _In_ D3DKMT_HANDLE Object,
    _In_ PSECURITY_DESCRIPTOR Security,
    _Out_ HANDLE* Handle)
{
    OBJECT_ATTRIBUTES Attributes = {};
    NTSTATUS Status;

    *Handle = NULL;
    Attributes.Length = sizeof(Attributes);
    Attributes.SecurityDescriptor = Security;
    Status = RxGetKmtThunks()->ShareObjects(1, &Object, &Attributes, D9SharedObjectAccess, Handle);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTShareObjects failed 0x%08lX for a flip model object", Status);
        return D9HresultFromStatus(Status);
    }
    return S_OK;
}

/* The small target and depth buffer presents bind while the compositor takes the back buffer */
HRESULT
D9SwapChain::CreateClearTargets(VOID)
{
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    Desc.Format = D3DFMT_A8R8G8B8;
    Desc.Type = D3DRTYPE_SURFACE;
    Desc.Usage = D3DUSAGE_RENDERTARGET;
    Desc.Pool = D3DPOOL_DEFAULT;
    Desc.Width = D9ClearTargetSize;
    Desc.Height = D9ClearTargetSize;
    ClearTarget = new (std::nothrow) D9Surface(Device, static_cast<IDirect3DSwapChain9*>(this), &Desc, FALSE);
    if (ClearTarget == nullptr)
        return E_OUTOFMEMORY;
    ClearTarget->AssignRole(D9SurfaceRole::CompositionTarget);
    hr = ClearTarget->Create();
    if (FAILED(hr))
        return hr;

    Desc.Format = D3DFMT_D24S8;
    Desc.Usage = D3DUSAGE_DEPTHSTENCIL;
    ClearDepth = new (std::nothrow) D9Surface(Device, static_cast<IDirect3DSwapChain9*>(this), &Desc, FALSE);
    if (ClearDepth == nullptr)
        return E_OUTOFMEMORY;
    return ClearDepth->Create();
}

/* The fence the compositor waits on before it shows a buffer, and the composition surface the chain binds */
HRESULT
D9SwapChain::CreateComposition(
    _In_ PSECURITY_DESCRIPTOR Security,
    _In_ BOOLEAN DirectFlip)
{
    const D9CompositionCalls* Calls = D9Composition();
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    NTSTATUS Status;
    HRESULT hr;

    if (BackBufferCount > D9BindableBuffers)
    {
        D9_TRACE_ONCE(L"CreateDevice: %u flip model buffers are more than the compositor binds", BackBufferCount);
        return E_INVALIDARG;
    }

    Create.hDevice = Device->KernelDevice();
    Create.Info.Flags.Shared = TRUE;
    Create.Info.Flags.NtSecuritySharing = TRUE;
    Create.Info.Flags.D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS_RESERVED0 = TRUE;
    if (Device->AdapterInfo()->DriverModel >= D9MonitoredFenceModel)
    {
        Create.Info.Type = D3DDDI_MONITORED_FENCE;
        Create.Info.Flags.NoGPUAccess = TRUE;
        Create.Info.MonitoredFence.InitialFenceValue = FenceValue;
    }
    else
    {
        Create.Info.Type = D3DDDI_FENCE;
        Create.Info.Fence.FenceValue = FenceValue;
    }
    Status = RxGetKmtThunks()->CreateSynchronizationObject2(&Create);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"D3DKMTCreateSynchronizationObject2 failed 0x%08lX for the flip model fence", Status);
        return D9HresultFromStatus(Status);
    }
    Fence = new (std::nothrow) D9TrackedFence;
    if (Fence == nullptr)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};

        Destroy.hSyncObject = Create.hSyncObject;
        RxGetKmtThunks()->DestroySynchronizationObject(&Destroy);
        return E_OUTOFMEMORY;
    }
    Fence->References = 1;
    Fence->Handle = Create.hSyncObject;

    hr = D9ShareWithCompositor(Fence->Handle, Security, &FenceHandle);
    if (FAILED(hr))
        return hr;

    if (CompositionSurface == NULL)
    {
        hr = Calls->CreateSurfaceHandle != nullptr ?
             Calls->CreateSurfaceHandle(D9CompositionSurfaceAccess, nullptr, &CompositionSurface) : E_NOTIMPL;
        if (FAILED(hr))
        {
            D9Trace(L"the composition surface could not be made (0x%08lX)", hr);
            return hr;
        }
    }
    DirectFlipBound = DirectFlip;
    return S_OK;
}

/* What the monitor search looks for and finds */
struct D9MonitorSearch
{
    PCSTR Name;
    HMONITOR Found;
};

/* Stops at the monitor named, or at one GDI cannot describe */
static
BOOL
CALLBACK
D9FindMonitor(
    _In_ HMONITOR Monitor,
    _In_opt_ HDC Dc,
    _In_opt_ LPRECT Area,
    _In_ LPARAM Context)
{
    D9MonitorSearch* Search = reinterpret_cast<D9MonitorSearch*>(Context);
    MONITORINFO Plain = {};
    MONITORINFOEXA Named = {};

    UNREFERENCED_PARAMETER(Dc);
    UNREFERENCED_PARAMETER(Area);
    Plain.cbSize = sizeof(Plain);
    Named.cbSize = sizeof(Named);
    if (!GetMonitorInfoA(Monitor, &Plain) || !GetMonitorInfoA(Monitor, &Named))
        return FALSE;
    if (strcmp(Search->Name, Named.szDevice) != 0)
        return TRUE;
    Search->Found = Monitor;
    return FALSE;
}

/* The monitor GDI shows the adapter's output on */
static
HMONITOR
D9MonitorOfDisplay(
    _In_z_ PCSTR DisplayName)
{
    D9MonitorSearch Search = { DisplayName, NULL };

    EnumDisplayMonitors(NULL, nullptr, D9FindMonitor, reinterpret_cast<LPARAM>(&Search));
    return Search.Found;
}

/* Hands the compositor the buffers, the window they show in, the present limit and the fence */
HRESULT
D9SwapChain::BindComposition(VOID)
{
    const D9CompositionCalls* Calls = D9Composition();
    const D9Adapter* Adapter = Device->AdapterInfo();
    BYTE Block[sizeof(DWORD) * 2 + D9BindInfoSize] = {};
    BYTE* Info = Block + sizeof(DWORD) * 2;
    HANDLE Semaphore = Device->AcquireFlipSemaphore();
    D9BoundBuffer Bound = {};
    DWORD Flags = DirectFlipBound ? D9BindDirectFlip : 0;
    LUID Luid = {};
    NTSTATUS Status;

    if (Device->Type() == D3DDEVTYPE_HAL && Adapter->MultiPlaneOverlay)
        Flags |= D9BindOverlays;
    Flags |= D9BindWindowed;

    D9PutField(Info, D9BindMonitor, D9MonitorOfDisplay(Adapter->DeviceName));
    D9PutField(Info, D9BindOpacity, 1UL);
    D9PutField(Info, D9BindScaleX, 1.0f);
    D9PutField(Info, D9BindScaleY, 1.0f);
    D9PutField(Info, D9BindFlags, Flags);
    D9PutField(Info, D9BindKind, 2UL);
    D9PutField(Info, D9BindBufferCount, BackBufferCount);
    Device->Owner()->GetAdapterLUID(Adapter->Ordinal, &Luid);
    for (UINT Buffer = 0; Buffer < BackBufferCount; Buffer++)
    {
        Bound.Handle = BufferHandles[Buffer];
        Bound.Adapter = Luid;
        D9PutField(Info, D9BindBuffers + Buffer * static_cast<UINT>(sizeof(Bound)), Bound);
    }
    D9PutField(Info, D9BindWindow, Window());
    D9PutField(Info, D9BindSemaphore, Semaphore);
    D9PutField(Info, D9BindFence, FenceHandle);

    if (Calls->Bind == nullptr)
        return E_NOTIMPL;
    D9PutField(Block, 0, 2UL);
    Status = Calls->Bind(CompositionSurface, 1, 0, nullptr, Block, Info);
    if (!NT_SUCCESS(Status))
    {
        D9Trace(L"NtBindCompositionSurface failed 0x%08lX", Status);
        return HRESULT_FROM_NT(Status);
    }
    BindingId = D9GetField<UINT64>(Info, 0);
    StatsDisjoint = TRUE;
    return S_OK;
}

/* Unbinds and lets go of everything the compositor knew; the buffers follow in ReleaseBuffers */
VOID
D9SwapChain::ReleaseComposition(VOID)
{
    const D9CompositionCalls* Calls = D9Composition();

    if (!Composed)
        return;

    if (BackBufferCount > 0)
    {
        if (CompositionSurface != NULL && Calls->Unbind != nullptr)
        {
            NTSTATUS Status = Calls->Unbind(CompositionSurface, 1);

            if (!NT_SUCCESS(Status))
                D9Trace(L"NtUnBindCompositionSurface failed 0x%08lX", Status);
        }
        BindingId = 0;
        if (CompositionSurface != NULL)
        {
            CloseHandle(CompositionSurface);
            CompositionSurface = NULL;
        }
        if (Fence != nullptr)
        {
            D9ReleaseTrackedFence(Fence);
            Fence = nullptr;
            if (FenceHandle != NULL)
            {
                CloseHandle(FenceHandle);
                FenceHandle = NULL;
            }
        }
        Device->ReleaseFlipSemaphore();
    }
    if (ClearTarget != nullptr)
    {
        ClearTarget->Abandon();
        ClearTarget = nullptr;
    }
    if (ClearDepth != nullptr)
    {
        ClearDepth->Abandon();
        ClearDepth = nullptr;
    }
    Composed = FALSE;
}

/* A flip model present: the small target is cleared so the driver lets go of the back buffer, the compositor
   gets the buffer, and the buffers move one place as a flip does */
HRESULT
D9SwapChain::PresentComposed(
    _In_opt_ const RGNDATA* DirtyRegion,
    _In_ DWORD Flags)
{
    BOOLEAN KeepBuffer = (Flags & D3DPRESENT_DONOTFLIP) != 0;
    D9Surface* Saved[D9MaxRenderTargets] = {};
    D9Surface* SavedDepth = nullptr;
    D9ComposedPresent Present = {};
    D9Surface* Source;
    D3DVIEWPORT9 Viewport = {};
    D3DVIEWPORT9 Small = {};
    RECT Scissor = {};
    RECT SmallScissor = {};
    RECT Client = {};
    HRESULT hr;

    /* Each dirty rectangle check looks at the first rectangle, as the runtime always has */
    if (DirtyRegion != nullptr)
    {
        const RECT* First = reinterpret_cast<const RECT*>(DirtyRegion->Buffer);

        for (DWORD Checked = 0; Checked < DirtyRegion->rdh.nCount; Checked++)
        {
            if (First->top < DirtyRegion->rdh.rcBound.top || First->bottom > DirtyRegion->rdh.rcBound.bottom ||
                First->left < DirtyRegion->rdh.rcBound.left || First->right > DirtyRegion->rdh.rcBound.right)
            {
                D9_TRACE_ONCE(L"Present: a dirty rectangle lies outside the region's bounds");
                return D3DERR_INVALIDCALL;
            }
        }
    }

    if ((Flags & D3DPRESENT_DONOTWAIT) != 0 && Device->PresentLimitReached())
        return D3DERR_WASSTILLDRAWING;

    if (KeepBuffer)
    {
        Present.SwapChainIndex = ComposedIndex != 0 ? ComposedIndex - 1 : BackBufferCount - 1;
        Present.FenceValue = FenceValue;
        Source = BackBuffers[BackBufferCount - 1];
    }
    else
    {
        Present.SwapChainIndex = ComposedIndex;
        Present.FenceValue = FenceValue + 1;
        Source = BackBuffers[0];
    }
    Present.Surface = CompositionSurface;
    Present.BindingId = BindingId;
    Present.FenceHandle = Fence != nullptr ? FenceHandle : NULL;
    Present.Flags = Flags;
    Present.DirtyRegion = DirtyRegion;
    Present.Width = Parameters.BackBufferWidth;
    Present.Height = Parameters.BackBufferHeight;
    switch (Parameters.PresentationInterval)
    {
    case D3DPRESENT_INTERVAL_IMMEDIATE:
        Present.FlipInterval = 0;
        break;
    case D3DPRESENT_INTERVAL_TWO:
        Present.FlipInterval = 2;
        break;
    case D3DPRESENT_INTERVAL_THREE:
        Present.FlipInterval = 3;
        break;
    case D3DPRESENT_INTERVAL_FOUR:
        Present.FlipInterval = 4;
        break;
    default:
        Present.FlipInterval = 1;
        break;
    }
    if ((Flags & D3DPRESENT_FORCEIMMEDIATE) != 0)
        Present.FlipInterval = 0;

    /* The driver must not render to the buffer the compositor reads, so the small target takes its place */
    if (!KeepBuffer)
    {
        Device->GetViewport(&Viewport);
        Small.Width = D9ClearTargetSize;
        Small.Height = D9ClearTargetSize;
        Device->SetViewport(&Small);
        Device->GetScissorRect(&Scissor);
        SetRect(&SmallScissor, 0, 0, D9ClearTargetSize, D9ClearTargetSize);
        Device->SetScissorRect(&SmallScissor);
        Saved[0] = ClearTarget;
        SavedDepth = ClearDepth;
        Device->ExchangeTargets(Saved, &SavedDepth);
        Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFFFFFFFF, 0.0f, 0);
        Device->SetViewport(&Viewport);
        Device->SetScissorRect(&Scissor);
    }

    hr = Device->PresentToCompositor(Source->DriverResource(), &Present);
    if (FAILED(hr))
    {
        if (!KeepBuffer)
            Device->ExchangeTargets(Saved, &SavedDepth);
        return hr;
    }

    if (hr == S_PRESENT_OCCLUDED)
        StatsDisjoint = TRUE;
    if (Device->KeepsPresentStats())
    {
        PresentMark = Device->PresentCount();
        PresentTime = Device->PresentTime();
    }
    if (Device->IsExtended() && GetClientRect(Window(), &Client) && IsRectEmpty(&Client))
        hr = S_PRESENT_OCCLUDED;

    if (!KeepBuffer && BackBufferCount > 1)
    {
        HANDLE Moving = BufferHandles[0];

        RotateBuffers();
        for (UINT Remaining = BackBufferCount; Remaining > 0; Remaining -= 1)
        {
            HANDLE Held = BufferHandles[Remaining - 1];

            BufferHandles[Remaining - 1] = Moving;
            Moving = Held;
        }
        Device->ExchangeTargets(Saved, &SavedDepth);
    }
    if (!KeepBuffer)
    {
        ComposedIndex += 1;
        if (ComposedIndex >= BackBufferCount)
            ComposedIndex = 0;
        if (CompositionSurface != NULL)
            FenceValue += 1;
    }
    return hr;
}

/* Until the chain presents again the last statistics stand; otherwise the compositor says what it showed */
HRESULT
D9SwapChain::ComposedStats(
    _Out_ D3DPRESENTSTATS* Stats)
{
    const D9CompositionCalls* Calls = D9Composition();
    BYTE Block[D9SurfaceStatisticsSize] = {};
    HRESULT hr = S_OK;
    NTSTATUS Status;

    if (CachedStats.PresentCount == PresentMark)
    {
        *Stats = CachedStats;
        LastStats = CachedStats;
        return S_OK;
    }

    if (StatsDisjoint)
    {
        StatsDisjoint = FALSE;
        hr = D3DERR_PRESENT_STATISTICS_DISJOINT;
    }
    Status = Calls->QueryStatistics != nullptr ? Calls->QueryStatistics(CompositionSurface, Block) : STATUS_NOT_IMPLEMENTED;
    if (!NT_SUCCESS(Status))
    {
        *Stats = LastStats;
    }
    else
    {
        Stats->PresentCount = D9GetField<UINT>(Block, D9StatisticsPresentCount);
        Stats->PresentRefreshCount = D9GetField<UINT>(Block, D9StatisticsPresentRefresh);
        Stats->SyncRefreshCount = D9GetField<UINT>(Block, D9StatisticsSyncRefresh);
        Stats->SyncQPCTime.QuadPart = D9GetField<LONGLONG>(Block, D9StatisticsSyncTime);
    }
    LastStats = *Stats;
    return hr;
}

/* The compositor may still show a buffer the chain hands out again; once every buffer went around, writing one
   waits for the present that let it go */
VOID
D9SwapChain::AcquireWriteAccess(
    _In_ const D9Surface* Surface)
{
    UINT Limit;
    UINT64 Value;

    if (!Composed || CompositionSurface == NULL || Fence == nullptr)
        return;

    for (UINT Buffer = 0; Buffer < BackBufferCount; Buffer++)
    {
        if (BackBuffers[Buffer] != Surface)
            continue;

        Limit = BackBufferCount;
        if (FenceValue + Buffer < Limit)
            return;
        Value = FenceValue + Buffer - (Limit - 1);
        if (Value <= FenceWaited)
            return;
        FenceWaited = Value;
        Device->WaitOnFence(Fence, Value);
        return;
    }
}

/* A windowed chain only notes the mode; flip model chains never take it */
HRESULT
D9SwapChain::SwitchDialogMode(
    _In_ BOOL Enable)
{
    if (Parameters.SwapEffect == D3DSWAPEFFECT_FLIPEX)
        return D3DERR_INVALIDCALL;
    if (!Parameters.Windowed)
    {
        D9_TRACE_ONCE(L"dialog box mode on a full screen chain is not implemented");
        return D3DERR_INVALIDCALL;
    }
    DialogMode = Enable != FALSE;
    return S_OK;
}
