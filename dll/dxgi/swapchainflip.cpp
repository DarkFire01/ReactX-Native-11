/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Flip model presentation: buffers bound to a composition surface DWM shows directly
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "swapchain.hpp"

/* dcomp.dll makes the composition surface; win32u binds buffers to it and announces presents */
typedef HRESULT (WINAPI* PFN_RX_CREATE_SURFACE_HANDLE)(
    _In_ DWORD DesiredAccess,
    _In_opt_ SECURITY_ATTRIBUTES* Attributes,
    _Out_ HANDLE* SurfaceHandle);

typedef NTSTATUS (APIENTRY* PFN_RX_BIND_SURFACE)(
    _In_ HANDLE Surface,
    _In_ UINT64 Binding,
    _In_ UINT64 Upgrade,
    _In_ BOOLEAN Option,
    _Inout_ UINT64* Version,
    _Inout_ VOID* BindInfo);

typedef NTSTATUS (APIENTRY* PFN_RX_UNBIND_SURFACE)(
    _In_ HANDLE Surface,
    _In_ UINT64 Binding);

typedef NTSTATUS (APIENTRY* PFN_RX_NOTIFY_SURFACE)(
    _In_ HANDLE Surface,
    _Inout_ D3DKMT_PRESENTHISTORYTOKEN* Token);

struct RxCompositionRoutines
{
    PFN_RX_CREATE_SURFACE_HANDLE CreateSurfaceHandle;
    PFN_RX_BIND_SURFACE Bind;
    PFN_RX_UNBIND_SURFACE Unbind;
    PFN_RX_NOTIFY_SURFACE Notify;
};

/* Access asked for on the composition surface: everything a swapchain does with it */
constexpr DWORD RxSurfaceAllAccess = 3;

/* Binding kind for a flip model swapchain, and the bind info revision this layout is */
constexpr UINT64 RxFlipBinding = 1;
constexpr UINT64 RxBindInfoVersion = 2;

/*
 * The bind info as Windows fills it for a windowed RGB swapchain. Only the fields
 * below are set; the rest stays zero.
 */
constexpr SIZE_T RxBindInfoSize = 0x518;
constexpr SIZE_T RxBindIdOffset = 0x000;
constexpr SIZE_T RxBindAlphaOffset = 0x008;
constexpr SIZE_T RxBindColorSpaceOffset = 0x038;
constexpr SIZE_T RxBindEnabledOffset = 0x04C;
constexpr SIZE_T RxBindFlagsOffset = 0x090;
constexpr SIZE_T RxBindKindOffset = 0x098;
constexpr SIZE_T RxBindCountOffset = 0x09C;
constexpr SIZE_T RxBindBuffersOffset = 0x0A0;
constexpr SIZE_T RxBindWindowOffset = 0x480;
constexpr SIZE_T RxBindLimitOffset = 0x488;
constexpr SIZE_T RxBindFenceOffset = 0x490;

/* Bind flags: always present, three or more discard buffers, and tearing allowed */
constexpr UINT RxBindFlagsBase = 0x250;
constexpr UINT RxBindFlagsManyDiscard = 0x100;
constexpr UINT RxBindFlagsTearing = 0x800;

/* Surface kinds: plain RGB, and RGB with a second eye */
constexpr UINT RxBindKindRgb = 2;
constexpr UINT RxBindKindStereo = 3;

/* How long a present waits for the compositor to give a frame slot back */
constexpr DWORD RxPresentLimitTimeout = 2000;

/* One buffer in the bind info */
struct RxBindBuffer
{
    HANDLE Buffer;
    LUID AdapterLuid;
    UINT64 Reserved;
    UINT Subresource;
    UINT Padding;
};

static_assert(sizeof(RxBindBuffer) == 32);
static_assert(RxBindBuffersOffset + sizeof(RxBindBuffer) * DXGI_MAX_SWAP_CHAIN_BUFFERS <= RxBindWindowOffset);

static
const RxCompositionRoutines*
RxGetCompositionRoutines(VOID)
{
    static const RxCompositionRoutines Routines = []()
    {
        RxCompositionRoutines Found = {};
        HMODULE Composition = LoadLibraryExW(L"dcomp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE Win32u = LoadLibraryExW(L"win32u.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (Composition != NULL)
        {
            Found.CreateSurfaceHandle = reinterpret_cast<PFN_RX_CREATE_SURFACE_HANDLE>(
                GetProcAddress(Composition, "DCompositionCreateSurfaceHandle"));
        }

        if (Win32u != NULL)
        {
            Found.Bind = reinterpret_cast<PFN_RX_BIND_SURFACE>(GetProcAddress(Win32u, "NtBindCompositionSurface"));
            Found.Unbind = reinterpret_cast<PFN_RX_UNBIND_SURFACE>(GetProcAddress(Win32u, "NtUnBindCompositionSurface"));
            Found.Notify = reinterpret_cast<PFN_RX_NOTIFY_SURFACE>(
                GetProcAddress(Win32u, "NtNotifyPresentToCompositionSurface"));
        }

        return Found;
    }();

    return &Routines;
}

/* A window's flip buffers only show while DWM composes the desktop */
static
BOOLEAN
RxIsDesktopComposed(VOID)
{
    typedef HRESULT (WINAPI* PFN_RX_IS_COMPOSITION_ENABLED)(_Out_ BOOL* Enabled);
    static PFN_RX_IS_COMPOSITION_ENABLED Routine = []()
    {
        HMODULE Dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        return Dwm ? reinterpret_cast<PFN_RX_IS_COMPOSITION_ENABLED>(GetProcAddress(Dwm, "DwmIsCompositionEnabled"))
                   : nullptr;
    }();
    BOOL Enabled = FALSE;

    return Routine != nullptr && SUCCEEDED(Routine(&Enabled)) && Enabled;
}

template <typename Value>
static
VOID
RxPutBindField(
    _Inout_updates_bytes_(RxBindInfoSize) BYTE* BindInfo,
    _In_ SIZE_T Offset,
    _In_ const Value& Field)
{
    memcpy(BindInfo + Offset, &Field, sizeof(Field));
}

BOOLEAN
RxSwapChain::UsesFlipPresent() const
{
    return CompositionSurface != NULL;
}

HRESULT
RxSwapChain::OpenCompositionSurface()
{
    const RxCompositionRoutines* Routines = RxGetCompositionRoutines();

    /* Without the composition calls a flip swapchain falls back to copying into DWM's surface */
    if (Routines->CreateSurfaceHandle == nullptr || Routines->Bind == nullptr || Routines->Unbind == nullptr ||
        Routines->Notify == nullptr)
    {
        return S_FALSE;
    }

    /* Nothing would show the buffers, so the window is presented to directly */
    if (Window != NULL && !RxIsDesktopComposed())
        return S_FALSE;

    HRESULT hr = Routines->CreateSurfaceHandle(RxSurfaceAllAccess, nullptr, &CompositionSurface);
    if (FAILED(hr))
        RX_TRACE(L"composition surface could not be created 0x%08lX", hr);
    return hr;
}

HRESULT
RxSwapChain::BindBuffers()
{
    const RxCompositionRoutines* Routines = RxGetCompositionRoutines();
    struct
    {
        UINT64 Version;
        BYTE Info[RxBindInfoSize];
    } Bind = {};
    RxBindBuffer Entry = {};
    UINT BindFlags = RxBindFlagsBase;
    HANDLE Limit;
    HANDLE Shared;
    UINT Index;
    NTSTATUS Status;
    HRESULT hr;

    /* Drivers before monitored fences have none to give; presents then go without one */
    hr = Device->CreateFlipFence(&FlipFence, &FlipFenceHandle);
    if (FAILED(hr))
    {
        RX_TRACE(L"no flip fence (0x%08lX), presenting without one", hr);
        FlipFence = 0;
        FlipFenceHandle = NULL;
    }

    /* Each buffer goes to the compositor as an NT handle to its kernel resource */
    for (Index = 0; Index < Buffers.size(); Index++)
    {
        hr = Device->ShareBuffer(Buffers[Index], &Shared);
        if (FAILED(hr))
        {
            RX_TRACE(L"buffer %u could not be shared 0x%08lX", Index, hr);
            UnbindBuffers();
            return hr;
        }

        SharedBuffers.push_back(Shared);
        Entry.Buffer = Shared;
        Entry.AdapterLuid = AdapterLuid;
        RxPutBindField(Bind.Info, RxBindBuffersOffset + Index * sizeof(Entry), Entry);
    }

    /* A waitable swapchain throttles on its own semaphore; others share the device's */
    Limit = (LatencySemaphore != NULL) ? LatencySemaphore : Device->GetPresentLimitSemaphore();

    if (Desc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD && Buffers.size() >= 3)
        BindFlags |= RxBindFlagsManyDiscard;
    if (Desc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)
        BindFlags |= RxBindFlagsTearing;

    Bind.Version = RxBindInfoVersion;
    RxPutBindField(Bind.Info, RxBindAlphaOffset, (UINT)Desc.AlphaMode);
    RxPutBindField(Bind.Info, RxBindColorSpaceOffset, (UINT)CurrentColorSpace);
    RxPutBindField(Bind.Info, RxBindEnabledOffset, (UINT)1);
    RxPutBindField(Bind.Info, RxBindFlagsOffset, BindFlags);
    RxPutBindField(Bind.Info, RxBindKindOffset, Desc.Stereo ? RxBindKindStereo : RxBindKindRgb);
    RxPutBindField(Bind.Info, RxBindCountOffset, (UINT)Buffers.size());
    RxPutBindField(Bind.Info, RxBindWindowOffset, Window);
    RxPutBindField(Bind.Info, RxBindLimitOffset, Limit);
    RxPutBindField(Bind.Info, RxBindFenceOffset, FlipFenceHandle);

    Status = Routines->Bind(CompositionSurface, RxFlipBinding, 0, FALSE, &Bind.Version, Bind.Info);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"binding the composition surface failed 0x%08lX", Status);
        UnbindBuffers();
        return HRESULT_FROM_NT(Status);
    }

    /* Every bind of the surface gets a new id; presents name the binding they belong to */
    memcpy(&BindingId, Bind.Info + RxBindIdOffset, sizeof(BindingId));
    IsBound = TRUE;
    CurrentIndex = 0;
    PresentedIndex = 0;
    RX_TRACE(L"flip swapchain bound: %u buffers, binding %llu", (UINT)Buffers.size(), BindingId);
    return S_OK;
}

VOID
RxSwapChain::UnbindBuffers()
{
    if (IsBound)
    {
        RxGetCompositionRoutines()->Unbind(CompositionSurface, RxFlipBinding);
        IsBound = FALSE;
        BindingId = 0;
    }

    Device->DestroyFlipFence(FlipFence);
    FlipFence = 0;
    if (FlipFenceHandle != NULL)
        CloseHandle(FlipFenceHandle);
    FlipFenceHandle = NULL;
    FenceValue = 0;

    for (HANDLE Shared : SharedBuffers)
        CloseHandle(Shared);
    SharedBuffers.clear();
}

HRESULT
RxSwapChain::PresentFlip(
    _In_ UINT SyncInterval,
    _In_ UINT Flags)
{
    static const FLOAT Identity[6] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    D3DKMT_FLIPMODEL_PRESENTHISTORYTOKEN* Flip;
    D3DKMT_PRESENTHISTORYTOKEN* Token;
    RxPresentContext* Context;
    D3DKMT_PRESENT* Template;
    BOOLEAN IsSequenced = !(Flags & DXGI_PRESENT_DO_NOT_SEQUENCE);
    HANDLE Limit = NULL;
    NTSTATUS Status;
    HRESULT hr;

    if (!IsBound)
        return DXGI_ERROR_INVALID_CALL;

    /* The UMD may call back after this returns, so each present gets its own slot */
    Context = &Contexts[NextContext];
    NextContext = (NextContext + 1) % Contexts.size();
    *Context = {};
    Template = &Context->Template;

    Template->hWindow = Window;
    Template->PresentCount = PresentCount + 1;
    Template->Flags.RedirectedFlip = 1;
    Template->Flags.PresentCountValid = 1;
    Template->Flags.FlipDoNotWait = (Flags & DXGI_PRESENT_DO_NOT_WAIT) ? 1 : 0;

    /* The token tells the compositor which buffer of which binding to show, and how */
    Token = &Template->PresentHistoryToken;
    Token->Model = D3DKMT_PM_REDIRECTED_FLIP;
    Token->CompositionBindingId = BindingId;
    Flip = &Token->Token.Flip;
    /* An unsequenced present shows the last buffer again without moving the chain; without a fence there is nothing to wait for */
    if (FlipFenceHandle != NULL)
        Flip->FenceValue = IsSequenced ? FenceValue + 1 : FenceValue;
    Flip->hLogicalSurface = (ULONG64)(ULONG_PTR)CompositionSurface;
    Flip->SwapChainIndex = IsSequenced ? CurrentIndex : PresentedIndex;
    Flip->PresentLimitSemaphoreId = 1;
    Flip->FlipInterval = (D3DDDI_FLIPINTERVAL_TYPE)SyncInterval;
    Flip->Flags.AlphaMode = Desc.AlphaMode;
    Flip->Flags.FlipRestart = (Flags & DXGI_PRESENT_RESTART) ? 1 : 0;
    Flip->Flags.TemporaryMono = (Desc.Stereo && (Flags & DXGI_PRESENT_STEREO_TEMPORARY_MONO)) ? 1 : 0;
    Flip->Flags.StereoPreferRight = (Desc.Stereo && (Flags & DXGI_PRESENT_STEREO_PREFER_RIGHT)) ? 1 : 0;
    Flip->Flags.IndependentFlipDoNotFlip = IsSequenced ? 0 : 1;
    Flip->Flags.IndependentFlipTrueImmediate = (Flags & DXGI_PRESENT_ALLOW_TEARING) ? 1 : 0;
    Flip->PresentCount = Template->PresentCount;
    Flip->Rotation = D3DDDI_ROTATION_IDENTITY;
    Flip->hSyncObject = FlipFenceHandle;
    Flip->SourceRect = { 0, 0, (LONG)SourceWidth, (LONG)SourceHeight };
    Flip->DestWidth = SourceWidth;
    Flip->DestHeight = SourceHeight;
    memcpy(Flip->Transform, Identity, sizeof(Identity));
    Flip->ColorSpace = (D3DDDI_COLOR_SPACE_TYPE)CurrentColorSpace;
    Flip->DirtyRegions.NumRects = 1;
    Flip->DirtyRegions.Rects[0] = Flip->SourceRect;

    /* Metadata rides along with every present once set; the flag marks the one that changed it */
    Flip->HDRMetaDataType = (D3DDDI_HDR_METADATA_TYPE)MetadataType;
    if (MetadataType == DXGI_HDR_METADATA_TYPE_HDR10)
        memcpy(&Flip->HDRMetaDataHDR10, Metadata, sizeof(Flip->HDRMetaDataHDR10));
    else if (MetadataType == DXGI_HDR_METADATA_TYPE_HDR10PLUS)
        memcpy(&Flip->HDRMetaDataHDR10Plus, Metadata, sizeof(Flip->HDRMetaDataHDR10Plus));
    Flip->Flags.HDRMetaDataChanged = IsMetadataChanged ? 1 : 0;

    /* One frame slot per queued flip; the kernel hands it back when the flip retires */
    if (LatencySemaphore == NULL)
    {
        Limit = Device->GetPresentLimitSemaphore();
        if (WaitForSingleObject(Limit, (Flags & DXGI_PRESENT_DO_NOT_WAIT) ? 0 : RxPresentLimitTimeout) != WAIT_OBJECT_0)
        {
            if (Flags & DXGI_PRESENT_DO_NOT_WAIT)
                return DXGI_ERROR_WAS_STILL_DRAWING;

            /* Timed out: the present goes ahead, but holds no slot to give back on failure */
            RX_TRACE(L"present limit wait timed out");
            Limit = NULL;
        }
    }

    Status = RxGetCompositionRoutines()->Notify(CompositionSurface, Token);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"announcing the flip failed 0x%08lX", Status);
        if (Limit != NULL)
            ReleaseSemaphore(Limit, 1, nullptr);
        return DXGI_STATUS_NO_REDIRECTION;
    }
    Flip->Flags.CompSurfaceNotifiedEarly = 1;

    hr = Device->PresentFlip(Buffers[0], Context);
    if (FAILED(hr))
    {
        RX_TRACE(L"flip present failed 0x%08lX", hr);
        if (Limit != NULL)
            ReleaseSemaphore(Limit, 1, nullptr);
        return hr;
    }

    PresentCount++;
    IsPreviousFrameValid = TRUE;
    IsMetadataChanged = FALSE;
    if (!IsSequenced)
        return hr;

    /* Buffer 0 always stays the one to draw to: identities move along with the index */
    FenceValue++;
    PresentedIndex = CurrentIndex;
    CurrentIndex = (CurrentIndex + 1) % (UINT)Buffers.size();
    Device->RotateBuffers(Buffers.data(), (UINT)Buffers.size());
    return hr;
}
