/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windowed swapchain: flip model through a composition surface, blt model through DWM's surface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swapchain.hpp"

#include <algorithm>

/* dwmapi hands out the surface DWM composes this window from */
typedef HRESULT (WINAPI* PFN_RX_GET_WINDOW_SHARED_SURFACE)(
    _In_ HWND Window,
    _In_ LUID AdapterLuid,
    _In_opt_ HMONITOR Monitor,
    _In_ DWORD Flags,
    _Inout_ DXGI_FORMAT* Format,
    _Out_ HANDLE* SharedSurface,
    _Out_opt_ UINT64* UpdateId);

/* dwmapi shows what a presenter drew into that surface */
typedef HRESULT (WINAPI* PFN_RX_UPDATE_WINDOW_SHARED_SURFACE)(
    _In_ HWND Window,
    _In_ UINT64 UpdateId,
    _In_ DWORD Flags,
    _In_opt_ HMONITOR Monitor,
    _In_opt_ const RECT* Update);

/* Flags for the call above: always set, and set when the present waits for a DWM frame */
constexpr DWORD RxSharedSurfaceDefault = 0x10;
constexpr DWORD RxSharedSurfaceSyncToDwm = 0x01;

/* Success codes from the call above for the two GDI flavors of redirection */
constexpr HRESULT RxDwmGdiRedirectionSurface = 0x00263005;
constexpr HRESULT RxDwmBltViaGdi = 0x00263008;

constexpr UINT RxMaximumSyncInterval = 4;

/* Frame latency starts at one frame and can go up to forty */
constexpr UINT RxDefaultFrameLatency = 1;
constexpr UINT RxMaximumFrameLatency = 40;
constexpr UINT RxMaximumBufferCount = DXGI_MAX_SWAP_CHAIN_BUFFERS;

static
PFN_RX_GET_WINDOW_SHARED_SURFACE
RxGetSharedSurfaceRoutine(VOID)
{
    static PFN_RX_GET_WINDOW_SHARED_SURFACE Routine = []()
    {
        HMODULE Dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        return Dwm ? reinterpret_cast<PFN_RX_GET_WINDOW_SHARED_SURFACE>(GetProcAddress(Dwm, "DwmpDxGetWindowSharedSurface"))
                   : nullptr;
    }();

    return Routine;
}

static
PFN_RX_UPDATE_WINDOW_SHARED_SURFACE
RxUpdateSharedSurfaceRoutine(VOID)
{
    static PFN_RX_UPDATE_WINDOW_SHARED_SURFACE Routine = []()
    {
        HMODULE Dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        return Dwm ? reinterpret_cast<PFN_RX_UPDATE_WINDOW_SHARED_SURFACE>(GetProcAddress(Dwm, "DwmpDxUpdateWindowSharedSurface"))
                   : nullptr;
    }();

    return Routine;
}

static
BOOLEAN
RxIsFlipModel(
    _In_ DXGI_SWAP_EFFECT Effect)
{
    return Effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || Effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

static
BOOLEAN
RxIsBackBufferFormat(
    _In_ DXGI_FORMAT Format,
    _In_ BOOLEAN IsFlipModel)
{
    switch (Format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return TRUE;

        /* Flip model swapchains take sRGB through render target views, not their buffers */
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return !IsFlipModel;

        default:
            return FALSE;
    }
}

RxSwapChain::RxSwapChain(
    _In_ RxDxgiFactory& Owner,
    _In_ IRxPresentDevice* BackingDevice,
    _In_ HWND TargetWindow)
    : Factory(Owner),
      Device(BackingDevice),
      Window(TargetWindow),
      Composition(*this)
{
    D3DKMT_WDDM_1_3_CAPS Caps = {};
    D3DKMT_ADAPTERTYPE Type = {};
    D3DKMT_QUERY_DEVICE_IDS Ids = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    BOOLEAN IsSoftware;

    Factory.AddRef();
    Device->AddRef();
    Device->GetAdapterLuid(&AdapterLuid);

    /* Basic Render only counts when it stands in as the discrete half of a hybrid pair */
    if (SUCCEEDED(RxOpenKernelAdapter(AdapterLuid, &Query.hAdapter)))
    {
        Query.Type = KMTQAITYPE_WDDM_1_3_CAPS;
        Query.pPrivateDriverData = &Caps;
        Query.PrivateDriverDataSize = sizeof(Caps);
        RxGetKmtThunks()->QueryAdapterInfo(&Query);
        Query.Type = KMTQAITYPE_ADAPTERTYPE;
        Query.pPrivateDriverData = &Type;
        Query.PrivateDriverDataSize = sizeof(Type);
        RxGetKmtThunks()->QueryAdapterInfo(&Query);
        Query.Type = KMTQAITYPE_PHYSICALADAPTERDEVICEIDS;
        Query.pPrivateDriverData = &Ids;
        Query.PrivateDriverDataSize = sizeof(Ids);
        RxGetKmtThunks()->QueryAdapterInfo(&Query);

        IsSoftware = Ids.DeviceIds.VendorID == RxBasicRenderVendor && Ids.DeviceIds.DeviceID == RxBasicRenderDevice && Type.RenderSupported &&
                     !Type.DisplaySupported;
        IsCrossAdapter = Caps.SupportCrossAdapterResource && !(IsSoftware && !Type.HybridDiscrete);
    }
    Factory.TrackSwapChain(this);
}

RxSwapChain::~RxSwapChain()
{
    LeaveFullscreen();
    ReleaseBuffers();
    if (CompositionSurface != NULL)
        CloseHandle(CompositionSurface);
    if (RedirectionSurface != nullptr)
        RedirectionSurface->Release();
    if (LatencySemaphore != NULL)
        CloseHandle(LatencySemaphore);
    if (CoreWindow != nullptr)
        CoreWindow->Release();
    Device->Release();
    Factory.UntrackSwapChain(this);
    Factory.Release();
}

HRESULT
RxSwapChain::Create(
    _In_ RxDxgiFactory& Factory,
    _In_ IRxPresentDevice* Device,
    _In_ HWND Window,
    _In_ const DXGI_SWAP_CHAIN_DESC1& Desc,
    _In_opt_ const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* FullscreenDesc,
    _Outptr_ RxSwapChain** SwapChain)
{
    BOOLEAN IsFlipModel = RxIsFlipModel(Desc.SwapEffect);
    RxSwapChain* NewSwapChain;
    RECT Client;
    HRESULT hr;

    *SwapChain = nullptr;

    /* Flip model needs a buffer to render while another is shown; blt model can live with one */
    if (Desc.BufferCount > RxMaximumBufferCount || Desc.BufferCount < (IsFlipModel ? 2u : 1u) ||
        !RxIsBackBufferFormat(Desc.Format, IsFlipModel) || (IsFlipModel && Desc.SampleDesc.Count != 1) ||
        Desc.SampleDesc.Count == 0 || (Desc.Stereo && !IsFlipModel) || (Desc.BufferUsage & ~DXGI_USAGE_SHADER_INPUT &
                                                     ~DXGI_USAGE_RENDER_TARGET_OUTPUT & ~DXGI_USAGE_UNORDERED_ACCESS &
                                                     ~DXGI_USAGE_BACK_BUFFER) != 0)
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    /* Without a window there is nothing but composition to show the buffers */
    if (Window == NULL && !IsFlipModel)
        return DXGI_ERROR_INVALID_CALL;

    NewSwapChain = new (std::nothrow) RxSwapChain(Factory, Device, Window);
    if (NewSwapChain == nullptr)
        return E_OUTOFMEMORY;

    /* A zero size means the window's client area */
    NewSwapChain->Desc = Desc;
    if (Window != NULL && (Desc.Width == 0 || Desc.Height == 0))
    {
        GetClientRect(Window, &Client);
        if (Desc.Width == 0)
            NewSwapChain->Desc.Width = std::max<LONG>(Client.right - Client.left, 1);
        if (Desc.Height == 0)
            NewSwapChain->Desc.Height = std::max<LONG>(Client.bottom - Client.top, 1);
    }

    NewSwapChain->SourceWidth = NewSwapChain->Desc.Width;
    NewSwapChain->SourceHeight = NewSwapChain->Desc.Height;

    /* Without a fullscreen description the swapchain is windowed at an unspecified rate */
    if (FullscreenDesc != nullptr)
    {
        NewSwapChain->FullscreenDesc = *FullscreenDesc;
    }
    else
    {
        NewSwapChain->FullscreenDesc.RefreshRate = { 0, 1 };
        NewSwapChain->FullscreenDesc.Windowed = TRUE;
    }

    /* One count per frame the application may have queued; presents give counts back */
    if (Desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
    {
        NewSwapChain->LatencySemaphore = CreateSemaphoreW(nullptr, RxDefaultFrameLatency, RxMaximumFrameLatency, nullptr);
        if (NewSwapChain->LatencySemaphore == NULL)
        {
            NewSwapChain->Release();
            return E_OUTOFMEMORY;
        }
    }

    /* Flip model swapchains hand their buffers to DWM instead of copying into its surface */
    if (IsFlipModel)
    {
        hr = NewSwapChain->OpenCompositionSurface();
        if (FAILED(hr) || (Window == NULL && !NewSwapChain->UsesFlipPresent()))
        {
            NewSwapChain->Release();
            return FAILED(hr) ? hr : DXGI_ERROR_UNSUPPORTED;
        }
    }

    hr = NewSwapChain->AllocateBuffers();
    if (FAILED(hr))
    {
        NewSwapChain->Release();
        return hr;
    }

    *SwapChain = NewSwapChain;
    return S_OK;
}

HRESULT
RxSwapChain::AllocateBuffers()
{
    IUnknown* Buffer;
    UINT Index;
    HRESULT hr;

    /* New buffers hold nothing a partial present could build on */
    IsPreviousFrameValid = FALSE;

    for (Index = 0; Index < Desc.BufferCount; Index++)
    {
        hr = Device->CreatePresentBuffer(&Desc, static_cast<IDXGISwapChain4*>(this), &Buffer);
        if (FAILED(hr))
        {
            RX_TRACE(L"buffer %u could not be created 0x%08lX", Index, hr);
            ReleaseBuffers();
            return hr;
        }
        Buffers.push_back(Buffer);
    }

    if (UsesFlipPresent())
    {
        hr = BindBuffers();

        /* A window DWM would not take the buffers for still gets them copied into its surface */
        if (FAILED(hr) && Window != NULL)
        {
            RX_TRACE(L"window %p takes no flip buffers (0x%08lX), copying instead", Window, hr);
            CloseHandle(CompositionSurface);
            CompositionSurface = NULL;
            hr = S_OK;
        }
        if (FAILED(hr))
        {
            ReleaseBuffers();
            return hr;
        }
    }

    return S_OK;
}

VOID
RxSwapChain::ReleaseBuffers()
{
    /* DWM lets go of the buffers before they are destroyed */
    if (UsesFlipPresent())
        UnbindBuffers();

    ReleaseGdiCopies();
    for (IUnknown* Buffer : Buffers)
    {
        Device->DetachPresentBuffer(Buffer);
        Buffer->Release();
    }
    Buffers.clear();
}

BOOLEAN
RxSwapChain::AreBuffersReleased()
{
    /* The swapchain's own reference is the only one allowed to remain */
    for (IUnknown* Buffer : Buffers)
    {
        Buffer->AddRef();
        if (Buffer->Release() != 1)
            return FALSE;
    }

    return TRUE;
}

RxSwapChain::Redirection
RxSwapChain::PrepareRedirection(
    _In_ UINT SyncInterval,
    _Out_ UINT64* UpdateId)
{
    DXGI_FORMAT Format = Desc.Format;
    HANDLE Surface = NULL;
    Redirection Kind;
    DWORD Flags = RxSharedSurfaceDefault;
    HRESULT hr;

    *UpdateId = 0;
    if (SyncInterval == 1)
        Flags |= RxSharedSurfaceSyncToDwm;

    /* No DWM at all: nothing composes this window */
    if (RxGetSharedSurfaceRoutine() == nullptr)
        return Redirection::NotComposed;

    /* Every present needs DWM's update id; only the wait for its frame depends on the interval */
    hr = RxGetSharedSurfaceRoutine()(Window, AdapterLuid, NULL, Flags, &Format, &Surface, UpdateId);
    if (hr == RxDwmGdiRedirectionSurface)
    {
        Kind = Redirection::GdiSurface;
    }
    else if (hr == RxDwmBltViaGdi)
    {
        Kind = Redirection::GdiBlt;
        Surface = NULL;
    }
    else if (SUCCEEDED(hr) && Surface != NULL)
    {
        Kind = Redirection::Surface;
    }
    else
    {
        RX_TRACE(L"DWM has no surface for window %p (0x%08lX)", Window, hr);
        return Redirection::NotComposed;
    }

    /* The surface is swapped out when the window resizes; open the new one */
    if (Surface != SharedSurface)
    {
        if (RedirectionSurface != nullptr)
            RedirectionSurface->Release();
        RedirectionSurface = nullptr;
        SharedSurface = NULL;

        if (Surface != NULL)
        {
            hr = Device->OpenPresentTarget(Surface, &RedirectionSurface);
            if (FAILED(hr))
            {
                RX_TRACE(L"opening the DWM surface failed 0x%08lX", hr);
                return Redirection::NotComposed;
            }

            RX_TRACE(L"DWM surface 0x%p opened, format %u, kind %u", Surface, Format, (UINT)Kind);
            SharedSurface = Surface;
        }
    }

    return Kind;
}

HRESULT
RxSwapChain::PresentToWindow(
    _In_ UINT SyncInterval)
{
    IDXGIOutput* Output = nullptr;
    RxPresentContext* Context;
    D3DKMT_PRESENT* Template;
    HRESULT hr;

    if (IsIconic(Window) || !IsWindowVisible(Window))
        return DXGI_STATUS_OCCLUDED;

    Context = &Contexts[NextContext];
    NextContext = (NextContext + 1) % Contexts.size();
    *Context = {};
    Template = &Context->Template;

    /* The kernel clips to what is visible of the window on the output that shows it */
    Context->SourceRect = { 0, 0, (LONG)Desc.Width, (LONG)Desc.Height };
    Template->hWindow = Window;
    Template->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    if (SUCCEEDED(GetContainingOutput(&Output)))
    {
        Template->VidPnSourceId = static_cast<RxDxgiOutput*>(Output)->GetVidPnSourceId();
        Output->Release();
    }

    Template->SrcRect = Context->SourceRect;
    Template->SubRectCnt = 1;
    Template->pSrcSubRects = &Context->SourceRect;
    Template->PresentCount = PresentCount + 1;
    Template->FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    Template->Flags.Blt = 1;
    Template->Flags.SrcRectValid = 1;
    Template->Flags.PresentCountValid = 1;

    /* A blt has no interval of its own, so the wait for the blank happens first */
    RxWaitForInterval(this, SyncInterval);

    hr = Device->SubmitPresent(Buffers[0], nullptr, Context);
    if (FAILED(hr))
    {
        RX_TRACE(L"window present failed 0x%08lX, drawing with GDI instead", hr);
        return PresentGdi(0);
    }

    if (Context->KernelStatus == STATUS_GRAPHICS_PRESENT_OCCLUDED)
        hr = DXGI_STATUS_OCCLUDED;

    PresentCount++;
    if (LatencySemaphore != NULL)
        ReleaseSemaphore(LatencySemaphore, 1, nullptr);

    return hr;
}

HRESULT
RxSwapChain::PresentByCopy(
    _In_ UINT SyncInterval)
{
    RxPresentContext* Context;
    D3DKMT_PRESENT* Template;
    Redirection Kind;
    UINT64 UpdateId;
    HRESULT hr;

    if (RxGetPresentOverride() == RxPresentOverride::Gdi)
        return PresentGdi(SyncInterval);
    if (RxGetPresentOverride() == RxPresentOverride::Window)
        return PresentToWindow(SyncInterval);

    /* A window DWM does not compose, as on a desktop without DWM, is presented to directly */
    Kind = PrepareRedirection(SyncInterval, &UpdateId);
    if (Kind == Redirection::NotComposed || Kind == Redirection::GdiBlt)
        return PresentToWindow(SyncInterval);

    /* The UMD may call back after this returns, so each present gets its own slot */
    Context = &Contexts[NextContext];
    NextContext = (NextContext + 1) % Contexts.size();
    *Context = {};
    Template = &Context->Template;

    Context->SourceRect = { 0, 0, (LONG)Desc.Width, (LONG)Desc.Height };
    Template->hWindow = Window;
    Template->VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    Template->SrcRect = Context->SourceRect;
    Template->SubRectCnt = 1;
    Template->pSrcSubRects = &Context->SourceRect;
    Template->PresentCount = ++PresentCount;
    Template->FlipInterval = D3DDDI_FLIPINTERVAL_ONE;
    Template->Flags.Blt = 1;
    Template->Flags.SrcRectValid = 1;
    Template->Flags.PresentCountValid = 1;
    Template->Flags.RedirectedBlt = 1;
    Template->Flags.CrossAdapter = IsCrossAdapter;

    /* The kernel copies into DWM's surface and tells DWM which frame it is */
    Template->PresentHistoryToken.Model = D3DKMT_PM_REDIRECTED_BLT;
    Template->PresentHistoryToken.Token.Blt.hPhysicalSurface = (ULONG64)(ULONG_PTR)SharedSurface;
    Template->PresentHistoryToken.Token.Blt.EventId = UpdateId;
    Template->PresentHistoryToken.Token.Blt.DirtyRegions.NumRects = 1;
    Template->PresentHistoryToken.Token.Blt.DirtyRegions.Rects[0] = Context->SourceRect;

    hr = Device->SubmitPresent(Buffers[0], RedirectionSurface, Context);
    if (FAILED(hr))
        RX_TRACE(L"present failed 0x%08lX", hr);

    /* The kernel copied nothing because nothing of the window shows */
    if (SUCCEEDED(hr) && Context->KernelStatus == STATUS_GRAPHICS_PRESENT_OCCLUDED)
        hr = DXGI_STATUS_OCCLUDED;

    /* DWM keeps showing the window's GDI contents until it hears the frame is in its surface */
    if (hr == S_OK && Kind == Redirection::Surface && RxUpdateSharedSurfaceRoutine() != nullptr)
    {
        HRESULT UpdateHr = RxUpdateSharedSurfaceRoutine()(Window, UpdateId, 0, NULL, &Context->SourceRect);

        if (FAILED(UpdateHr))
            RX_TRACE(L"DWM did not take update %llu for window %p (0x%08lX)", UpdateId, Window, UpdateHr);
    }

    /* The copy is queued behind the frame's rendering, so the frame slot is free again */
    if (SUCCEEDED(hr) && LatencySemaphore != NULL)
        ReleaseSemaphore(LatencySemaphore, 1, nullptr);

    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IDXGISwapChain1) || riid == __uuidof(IDXGISwapChain2) ||
        riid == __uuidof(IDXGISwapChain3) || riid == __uuidof(IDXGISwapChain4))
    {
        *ppvObject = static_cast<IDXGISwapChain4*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == IID_RxSwapChainComposition)
    {
        *ppvObject = static_cast<IRxSwapChainComposition*>(&Composition);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxSwapChain::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxSwapChain::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    if (ppParent == nullptr)
        return E_INVALIDARG;

    return Factory.QueryInterface(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetDevice(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppDevice)
{
    if (ppDevice == nullptr)
        return E_INVALIDARG;

    return Device->QueryInterface(riid, ppDevice);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::Present(
    _In_ UINT SyncInterval,
    _In_ UINT Flags)
{
    if (SyncInterval > RxMaximumSyncInterval)
        return DXGI_ERROR_INVALID_CALL;

    /* Tearing needs a flip swapchain made for it and an immediate present */
    if ((Flags & DXGI_PRESENT_ALLOW_TEARING) &&
        (SyncInterval != 0 || !RxIsFlipModel(Desc.SwapEffect) || !(Desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    /* A test present only asks whether presenting would work right now */
    if (Flags & DXGI_PRESENT_TEST)
        return S_OK;

    RxDumpFrame(Buffers[0], PresentCount + 1);
    if (((PresentCount + 1) % 600) == 0)
        RX_TRACE(L"present %u", PresentCount + 1);

    if (UsesFlipPresent() && RxGetPresentOverride() == RxPresentOverride::None)
        return PresentFlip(SyncInterval, Flags);
    return PresentByCopy(SyncInterval);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetBuffer(
    _In_ UINT Buffer,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppSurface)
{
    if (ppSurface == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppSurface = nullptr;

    /* Only the current back buffer can be written in flip model, and that is always buffer 0 */
    if (Buffer >= Buffers.size() || (RxIsFlipModel(Desc.SwapEffect) && Buffer != 0 &&
                                     (Desc.BufferUsage & DXGI_USAGE_SHADER_INPUT) == 0))
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    return Buffers[Buffer]->QueryInterface(riid, ppSurface);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetFullscreenState(
    _In_ BOOL Fullscreen,
    _In_opt_ IDXGIOutput* pTarget)
{
    IDXGIOutput* Target = pTarget;
    DXGI_OUTPUT_DESC OutputDesc;
    RECT Area;
    HRESULT hr;

    if (Window == NULL || CoreWindow != nullptr)
        return DXGI_ERROR_INVALID_CALL;

    if (!Fullscreen)
    {
        LeaveFullscreen();
        return S_OK;
    }

    /* The output shown on is the one asked for, else the one holding most of the window */
    if (Target != nullptr)
        Target->AddRef();
    else if (FAILED(GetContainingOutput(&Target)))
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    hr = Target->GetDesc(&OutputDesc);
    if (FAILED(hr))
    {
        Target->Release();
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    if (FullscreenTarget == nullptr)
    {
        WindowedStyle = GetWindowLongPtrW(Window, GWL_STYLE);
        WindowedExStyle = GetWindowLongPtrW(Window, GWL_EXSTYLE);
        GetWindowRect(Window, &WindowedRect);
    }
    else
    {
        FullscreenTarget->Release();
    }

    FullscreenTarget = Target;
    FullscreenDesc.Windowed = FALSE;

    Area = OutputDesc.DesktopCoordinates;
    SetWindowLongPtrW(Window, GWL_STYLE, (WindowedStyle & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME)) | WS_POPUP);
    SetWindowLongPtrW(Window, GWL_EXSTYLE, WindowedExStyle | WS_EX_TOPMOST);
    SetWindowPos(Window, HWND_TOPMOST, Area.left, Area.top, Area.right - Area.left, Area.bottom - Area.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    return S_OK;
}

VOID
RxSwapChain::LeaveFullscreen()
{
    if (FullscreenTarget == nullptr)
        return;

    FullscreenTarget->Release();
    FullscreenTarget = nullptr;
    FullscreenDesc.Windowed = TRUE;

    SetWindowLongPtrW(Window, GWL_STYLE, WindowedStyle);
    SetWindowLongPtrW(Window, GWL_EXSTYLE, WindowedExStyle);
    SetWindowPos(Window, (WindowedExStyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, WindowedRect.left,
                 WindowedRect.top, WindowedRect.right - WindowedRect.left, WindowedRect.bottom - WindowedRect.top,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetFullscreenState(
    _Out_opt_ BOOL* pFullscreen,
    _COM_Outptr_opt_result_maybenull_ IDXGIOutput** ppTarget)
{
    if (pFullscreen != nullptr)
        *pFullscreen = FullscreenTarget != nullptr;
    if (ppTarget != nullptr)
    {
        *ppTarget = FullscreenTarget;
        if (FullscreenTarget != nullptr)
            FullscreenTarget->AddRef();
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetDesc(
    _Out_ DXGI_SWAP_CHAIN_DESC* pDesc)
{
    if (pDesc == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pDesc = {};
    pDesc->BufferDesc.Width = Desc.Width;
    pDesc->BufferDesc.Height = Desc.Height;
    pDesc->BufferDesc.Format = Desc.Format;
    pDesc->BufferDesc.RefreshRate = FullscreenDesc.RefreshRate;
    pDesc->BufferDesc.ScanlineOrdering = FullscreenDesc.ScanlineOrdering;
    pDesc->BufferDesc.Scaling = FullscreenDesc.Scaling;
    pDesc->SampleDesc = Desc.SampleDesc;
    pDesc->BufferUsage = Desc.BufferUsage;
    pDesc->BufferCount = Desc.BufferCount;
    pDesc->OutputWindow = Window;
    pDesc->Windowed = FullscreenDesc.Windowed;
    pDesc->SwapEffect = Desc.SwapEffect;
    pDesc->Flags = Desc.Flags;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::ResizeBuffers(
    _In_ UINT BufferCount,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT NewFormat,
    _In_ UINT SwapChainFlags)
{
    DXGI_SWAP_CHAIN_DESC1 Previous = Desc;
    RECT Client;
    HRESULT hr;

    /* Every buffer reference the app took has to be gone first */
    if (!AreBuffersReleased())
        return DXGI_ERROR_INVALID_CALL;

    if (BufferCount != 0)
        Desc.BufferCount = BufferCount;
    if (NewFormat != DXGI_FORMAT_UNKNOWN)
        Desc.Format = NewFormat;
    Desc.Flags = SwapChainFlags;

    GetClientRect(Window, &Client);
    Desc.Width = Width ? Width : std::max<LONG>(Client.right - Client.left, 1);
    Desc.Height = Height ? Height : std::max<LONG>(Client.bottom - Client.top, 1);

    if (Desc.BufferCount > RxMaximumBufferCount || !RxIsBackBufferFormat(Desc.Format, RxIsFlipModel(Desc.SwapEffect)))
    {
        Desc = Previous;
        return DXGI_ERROR_INVALID_CALL;
    }

    ReleaseBuffers();
    hr = AllocateBuffers();
    if (FAILED(hr))
    {
        /* Try to leave the swapchain usable at its old size */
        Desc = Previous;
        AllocateBuffers();
    }

    SourceWidth = Desc.Width;
    SourceHeight = Desc.Height;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetContainingOutput(
    _COM_Outptr_ IDXGIOutput** ppOutput)
{
    MONITORINFOEXW Monitor = {};
    DXGI_OUTPUT_DESC OutputDesc;
    IDXGIDevice* DxgiDevice = nullptr;
    IDXGIAdapter* Adapter = nullptr;
    IDXGIOutput* Output;
    HRESULT hr;
    UINT Index;

    if (ppOutput == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *ppOutput = nullptr;

    /* The output is the one on the device's adapter that shows most of the window */
    Monitor.cbSize = sizeof(Monitor);
    GetMonitorInfoW(MonitorFromWindow(Window, MONITOR_DEFAULTTONEAREST), &Monitor);

    hr = Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice));
    if (SUCCEEDED(hr))
        hr = DxgiDevice->GetAdapter(&Adapter);

    for (Index = 0; SUCCEEDED(hr) && SUCCEEDED(Adapter->EnumOutputs(Index, &Output)); Index++)
    {
        if (SUCCEEDED(Output->GetDesc(&OutputDesc)) && _wcsicmp(OutputDesc.DeviceName, Monitor.szDevice) == 0)
        {
            *ppOutput = Output;
            break;
        }
        Output->Release();
    }

    if (Adapter != nullptr)
        Adapter->Release();
    if (DxgiDevice != nullptr)
        DxgiDevice->Release();

    return (*ppOutput != nullptr) ? S_OK : DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetLastPresentCount(
    _Out_ UINT* pLastPresentCount)
{
    if (pLastPresentCount == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pLastPresentCount = PresentCount;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetDesc1(
    _Out_ DXGI_SWAP_CHAIN_DESC1* pDesc)
{
    if (pDesc == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pDesc = Desc;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetFullscreenDesc(
    _Out_ DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pDesc)
{
    if (pDesc == nullptr || CoreWindow != nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pDesc = FullscreenDesc;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetHwnd(
    _Out_ HWND* pHwnd)
{
    if (pHwnd == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* The HWND behind a CoreWindow is not handed out */
    *pHwnd = (CoreWindow == nullptr) ? Window : NULL;
    return (*pHwnd != NULL) ? S_OK : DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetCoreWindow(
    _In_ REFIID refiid,
    _COM_Outptr_ void** ppUnk)
{
    if (ppUnk == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *ppUnk = nullptr;
    if (CoreWindow == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    return CoreWindow->QueryInterface(refiid, ppUnk);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::Present1(
    _In_ UINT SyncInterval,
    _In_ UINT PresentFlags,
    _In_ const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
    /*
     * A sequential flip swapchain only redraws what the rects name; the rest of the frame
     * comes from the buffer presented last, like on Windows.
     */
    if (pPresentParameters != nullptr && Desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL &&
        (PresentFlags & DXGI_PRESENT_TEST) == 0 && IsPreviousFrameValid && Buffers.size() > 1 &&
        (pPresentParameters->DirtyRectsCount != 0 || pPresentParameters->pScrollRect != nullptr))
    {
        KeepPreviousFrame(*pPresentParameters);
    }

    return Present(SyncInterval, PresentFlags);
}

/* Copies the parts of the last frame the application did not redraw into the back buffer */
VOID
RxSwapChain::KeepPreviousFrame(
    _In_ const DXGI_PRESENT_PARAMETERS& Parameters)
{
    IUnknown* Previous = Buffers[Buffers.size() - 1];
    HRGN Kept = CreateRectRgn(0, 0, (int)Desc.Width, (int)Desc.Height);
    HRGN Redrawn = CreateRectRgn(0, 0, 0, 0);
    HRGN Scrolled = CreateRectRgn(0, 0, 0, 0);
    HRGN Piece;
    POINT Offset = { 0, 0 };
    UINT Index;

    if (Kept == NULL || Redrawn == NULL || Scrolled == NULL)
        goto Done;

    for (Index = 0; Parameters.pDirtyRects != nullptr && Index < Parameters.DirtyRectsCount; Index++)
    {
        Piece = CreateRectRgnIndirect(&Parameters.pDirtyRects[Index]);
        if (Piece != NULL)
        {
            CombineRgn(Redrawn, Redrawn, Piece, RGN_OR);
            DeleteObject(Piece);
        }
    }

    /* The scrolled area lands where the offset moves it, under whatever was redrawn there */
    if (Parameters.pScrollRect != nullptr)
    {
        if (Parameters.pScrollOffset != nullptr)
            Offset = *Parameters.pScrollOffset;

        SetRectRgn(Scrolled, Parameters.pScrollRect->left + Offset.x, Parameters.pScrollRect->top + Offset.y,
                   Parameters.pScrollRect->right + Offset.x, Parameters.pScrollRect->bottom + Offset.y);
        CombineRgn(Scrolled, Scrolled, Kept, RGN_AND);
        CombineRgn(Scrolled, Scrolled, Redrawn, RGN_DIFF);
        OffsetRgn(Scrolled, -Offset.x, -Offset.y);
        CopyRegion(Previous, Scrolled, Offset);
        OffsetRgn(Scrolled, Offset.x, Offset.y);
        CombineRgn(Kept, Kept, Scrolled, RGN_DIFF);
    }

    CombineRgn(Kept, Kept, Redrawn, RGN_DIFF);
    CopyRegion(Previous, Kept, { 0, 0 });

Done:
    if (Kept != NULL)
        DeleteObject(Kept);
    if (Redrawn != NULL)
        DeleteObject(Redrawn);
    if (Scrolled != NULL)
        DeleteObject(Scrolled);
}

/* Copies a region of the given buffer into the back buffer, moved by Offset */
VOID
RxSwapChain::CopyRegion(
    _In_ IUnknown* Source,
    _In_ HRGN Region,
    _In_ POINT Offset)
{
    std::vector<BYTE> Data;
    const RGNDATA* Rects;
    DWORD Size;

    Size = GetRegionData(Region, 0, nullptr);
    if (Size == 0)
        return;

    Data.resize(Size);
    if (GetRegionData(Region, Size, reinterpret_cast<RGNDATA*>(Data.data())) == 0)
        return;

    Rects = reinterpret_cast<const RGNDATA*>(Data.data());
    if (Rects->rdh.nCount != 0)
    {
        Device->CopyBufferRegions(Buffers[0], Source, reinterpret_cast<const RECT*>(Rects->Buffer), Rects->rdh.nCount,
                                  Offset);
    }
}

BOOL
STDMETHODCALLTYPE
RxSwapChain::IsTemporaryMonoSupported(VOID)
{
    /* A windowed stereo swapchain can always show one eye to both */
    return Desc.Stereo && FullscreenDesc.Windowed;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetRestrictToOutput(
    _Out_ IDXGIOutput** ppRestrictToOutput)
{
    if (ppRestrictToOutput == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *ppRestrictToOutput = nullptr;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetBackgroundColor(
    _In_ const DXGI_RGBA* pColor)
{
    if (pColor == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* Written so that NaN passes, as it does on Windows */
    if (pColor->r < 0.0f || pColor->r > 1.0f || pColor->g < 0.0f || pColor->g > 1.0f || pColor->b < 0.0f ||
        pColor->b > 1.0f || pColor->a < 0.0f || pColor->a > 1.0f)
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    Background = *pColor;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetBackgroundColor(
    _Out_ DXGI_RGBA* pColor)
{
    if (pColor == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pColor = Background;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetRotation(
    _In_ DXGI_MODE_ROTATION NewRotation)
{
    /* Only flip model buffers can be scanned out turned */
    if (!RxIsFlipModel(Desc.SwapEffect) || NewRotation < DXGI_MODE_ROTATION_IDENTITY ||
        NewRotation > DXGI_MODE_ROTATION_ROTATE270)
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    Rotation = NewRotation;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetRotation(
    _Out_ DXGI_MODE_ROTATION* pRotation)
{
    if (pRotation == nullptr || !RxIsFlipModel(Desc.SwapEffect))
        return DXGI_ERROR_INVALID_CALL;

    *pRotation = Rotation;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::ResizeTarget(
    _In_ const DXGI_MODE_DESC* pNewTargetParameters)
{
    RECT Frame;

    if (pNewTargetParameters == nullptr || Window == NULL ||
        (pNewTargetParameters->RefreshRate.Numerator != 0 && pNewTargetParameters->RefreshRate.Denominator == 0))
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    /* Windowed, the target is the window: its client area takes the new size, its position stays */
    Frame = { 0, 0, (LONG)pNewTargetParameters->Width, (LONG)pNewTargetParameters->Height };
    AdjustWindowRectEx(&Frame, (DWORD)GetWindowLongW(Window, GWL_STYLE), FALSE,
                       (DWORD)GetWindowLongW(Window, GWL_EXSTYLE));
    SetWindowPos(Window, NULL, 0, 0, Frame.right - Frame.left, Frame.bottom - Frame.top, SWP_NOMOVE | SWP_NOZORDER);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetFrameStatistics(
    _Out_ DXGI_FRAME_STATISTICS* pStats)
{
    if (pStats == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    /* Composition statistics come from DWM, which this swapchain does not query yet */
    *pStats = {};
    return DXGI_ERROR_FRAME_STATISTICS_DISJOINT;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetSourceSize(
    _In_ UINT Width,
    _In_ UINT Height)
{
    if (!RxIsFlipModel(Desc.SwapEffect) || Width > Desc.Width || Height > Desc.Height)
        return DXGI_ERROR_INVALID_CALL;

    SourceWidth = Width;
    SourceHeight = Height;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetSourceSize(
    _Out_ UINT* pWidth,
    _Out_ UINT* pHeight)
{
    if (pWidth == nullptr || pHeight == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pWidth = SourceWidth;
    *pHeight = SourceHeight;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetMatrixTransform(
    _In_ const DXGI_MATRIX_3X2_F* pMatrix)
{
    UNREFERENCED_PARAMETER(pMatrix);

    /* Transforms belong to composition swapchains; window swapchains have none */
    return DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetMatrixTransform(
    _Out_ DXGI_MATRIX_3X2_F* pMatrix)
{
    UNREFERENCED_PARAMETER(pMatrix);
    return DXGI_ERROR_INVALID_CALL;
}

/* What a color space can do with this swapchain's format, as CheckColorSpaceSupport reports it */
static
UINT
RxColorSpaceSupport(
    _In_ DXGI_FORMAT Format,
    _In_ DXGI_COLOR_SPACE_TYPE ColorSpace)
{
    BOOLEAN IsHalfFloat = (Format == DXGI_FORMAT_R16G16B16A16_FLOAT);

    /* Half float buffers are linear scRGB; everything else starts from sRGB */
    if (IsHalfFloat)
        return (ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT
                                                                       : 0;

    return (ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) ? DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT
                                                                   : 0;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::CheckColorSpaceSupport(
    _In_ DXGI_COLOR_SPACE_TYPE ColorSpace,
    _Out_ UINT* pColorSpaceSupport)
{
    if (pColorSpaceSupport == nullptr)
        return E_INVALIDARG;

    *pColorSpaceSupport = RxColorSpaceSupport(Desc.Format, ColorSpace);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetColorSpace1(
    _In_ DXGI_COLOR_SPACE_TYPE NewColorSpace)
{
    if (RxColorSpaceSupport(Desc.Format, NewColorSpace) == 0)
        return E_INVALIDARG;

    CurrentColorSpace = NewColorSpace;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::ResizeBuffers1(
    _In_ UINT BufferCount,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SwapChainFlags,
    _In_reads_(BufferCount) const UINT* pCreationNodeMask,
    _In_reads_(BufferCount) IUnknown* const* ppPresentQueue)
{
    UNREFERENCED_PARAMETER(Width);
    UNREFERENCED_PARAMETER(Height);
    UNREFERENCED_PARAMETER(Format);
    UNREFERENCED_PARAMETER(SwapChainFlags);
    UNREFERENCED_PARAMETER(pCreationNodeMask);
    UNREFERENCED_PARAMETER(ppPresentQueue);

    /* Per node queues are a Direct3D 12 feature; D3D11 swapchains always refuse */
    if (BufferCount > DXGI_MAX_SWAP_CHAIN_BUFFERS)
        return E_INVALIDARG;
    return DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetHDRMetaData(
    _In_ DXGI_HDR_METADATA_TYPE Type,
    _In_ UINT Size,
    _In_reads_opt_(Size) void* pMetaData)
{
    UINT Needed;

    switch (Type)
    {
        case DXGI_HDR_METADATA_TYPE_NONE:
            if (Size != 0 || pMetaData != nullptr)
                return E_INVALIDARG;
            IsMetadataChanged = IsMetadataChanged || MetadataType != Type;
            MetadataType = Type;
            return S_OK;

        case DXGI_HDR_METADATA_TYPE_HDR10:
            Needed = sizeof(DXGI_HDR_METADATA_HDR10);
            break;

        case DXGI_HDR_METADATA_TYPE_HDR10PLUS:
            Needed = sizeof(DXGI_HDR_METADATA_HDR10PLUS);
            break;

        default:
            return E_INVALIDARG;
    }

    if (Size != Needed || pMetaData == nullptr)
        return E_INVALIDARG;

    /* Sent with the next present, once, and only when something changed */
    if (MetadataType != Type || memcmp(Metadata, pMetaData, Size) != 0)
        IsMetadataChanged = TRUE;
    MetadataType = Type;
    memcpy(Metadata, pMetaData, Size);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::SetMaximumFrameLatency(
    _In_ UINT MaxLatency)
{
    if (LatencySemaphore == NULL || MaxLatency == 0 || MaxLatency > RxMaximumFrameLatency)
        return DXGI_ERROR_INVALID_CALL;

    /* Raising the limit frees the extra slots at once; lowering it lets presents use them up */
    if (MaxLatency > MaximumLatency)
        ReleaseSemaphore(LatencySemaphore, (LONG)(MaxLatency - MaximumLatency), nullptr);
    MaximumLatency = MaxLatency;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChain::GetMaximumFrameLatency(
    _Out_ UINT* pMaxLatency)
{
    if (pMaxLatency == nullptr || LatencySemaphore == NULL)
        return DXGI_ERROR_INVALID_CALL;

    *pMaxLatency = MaximumLatency;
    return S_OK;
}

HANDLE
STDMETHODCALLTYPE
RxSwapChain::GetFrameLatencyWaitableObject(VOID)
{
    HANDLE Duplicate;

    /* Every call hands out a new handle the caller closes */
    if (LatencySemaphore == NULL ||
        !DuplicateHandle(GetCurrentProcess(), LatencySemaphore, GetCurrentProcess(), &Duplicate, SYNCHRONIZE, TRUE, 0))
    {
        return NULL;
    }

    return Duplicate;
}

UINT
STDMETHODCALLTYPE
RxSwapChain::GetCurrentBackBufferIndex(VOID)
{
    /* D3D11 buffers trade identities on present, so buffer 0 is always the one to draw to */
    return 0;
}

/* The composition face */

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return SwapChain.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxSwapChainComposition::AddRef(VOID)
{
    return SwapChain.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxSwapChainComposition::Release(VOID)
{
    return SwapChain.Release();
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return SwapChain.SetPrivateData(Name, DataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return SwapChain.SetPrivateDataInterface(Name, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return SwapChain.GetPrivateData(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    return SwapChain.GetParent(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::GetDevice(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppDevice)
{
    return SwapChain.GetDevice(riid, ppDevice);
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::QuerySurface(
    _Out_ HANDLE* Surface)
{
    *Surface = NULL;
    if (SwapChain.CompositionSurface == NULL)
        return DXGI_ERROR_UNSUPPORTED;

    /* The caller gets its own handle to close */
    if (!DuplicateHandle(GetCurrentProcess(), SwapChain.CompositionSurface, GetCurrentProcess(), Surface, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::SetAlpha(
    _In_ DXGI_ALPHA_MODE Mode)
{
    RX_TRACE(L"composition SetAlpha %u", Mode);
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::QueryPartnerStats(
    _Out_ VOID* Statistics)
{
    UNREFERENCED_PARAMETER(Statistics);

    RX_TRACE(L"composition QueryPartnerStats");
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxSwapChainComposition::QuerySurfaceId(
    _Out_ UINT64* Handle)
{
    /* Only a swapchain bound for composition has a surface DirectComposition may show */
    *Handle = 0;
    if (SwapChain.Window != NULL || SwapChain.CompositionSurface == NULL)
        return DXGI_ERROR_UNSUPPORTED;

    *Handle = (UINT64)(ULONG_PTR)SwapChain.CompositionSurface;
    return S_OK;
}
