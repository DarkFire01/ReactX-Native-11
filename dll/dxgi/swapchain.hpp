/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Windowed swapchain presenting through the DWM redirection surface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dxgiobjects.hpp"

#include <array>

struct ID3D11Texture2D;
class RxSwapChain;

/* Window property naming who owns a CoreWindow's root: 1 for a composition target, 2 for a swapchain */
inline constexpr WCHAR RxCoreWindowRootProperty[] = L"DCompCoreWindowRoot";
inline const HANDLE RxCoreWindowRootSwapChain = (HANDLE)2;

/*
 * DirectComposition asks a swapchain handed to SetContent for this and reads the composition
 * surface its buffers are bound to from slot 11. The slots before it follow the same table.
 */
inline constexpr GUID IID_RxSwapChainComposition = { 0x7652c85a, 0x0df5, 0x4567, { 0xbc, 0xdd, 0x81, 0x76, 0xa3, 0x32, 0xb9, 0x9b } };

struct IRxSwapChainComposition : public IDXGIDeviceSubObject
{
    virtual
    HRESULT
    STDMETHODCALLTYPE
    QuerySurface(
        _Out_ HANDLE* Surface) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    SetAlpha(
        _In_ DXGI_ALPHA_MODE Mode) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    QueryPartnerStats(
        _Out_ VOID* Statistics) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    QuerySurfaceId(
        _Out_ UINT64* Handle) = 0;
};

/* A tear-off of RxSwapChain: lifetime, private data and QueryInterface stay with the swapchain */
class RxSwapChainComposition final : public IRxSwapChainComposition
{
public:
    explicit RxSwapChainComposition(
        _In_ RxSwapChain& Owner)
        : SwapChain(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateData(
        _In_ REFGUID Name,
        _In_ UINT DataSize,
        _In_reads_bytes_(DataSize) const void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID Name,
        _In_opt_ const IUnknown* pUnknown) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID Name,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_(*pDataSize) void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    GetParent(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppParent) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDevice(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppDevice) override;

    HRESULT
    STDMETHODCALLTYPE
    QuerySurface(
        _Out_ HANDLE* Surface) override;

    HRESULT
    STDMETHODCALLTYPE
    SetAlpha(
        _In_ DXGI_ALPHA_MODE Mode) override;

    HRESULT
    STDMETHODCALLTYPE
    QueryPartnerStats(
        _Out_ VOID* Statistics) override;

    HRESULT
    STDMETHODCALLTYPE
    QuerySurfaceId(
        _Out_ UINT64* Handle) override;

private:
    RxSwapChain& SwapChain;
};

class RxSwapChain final : public IDXGISwapChain4
{
public:
    static
    HRESULT
    Create(
        _In_ RxDxgiFactory& Factory,
        _In_ IRxPresentDevice* Device,
        _In_ HWND Window,
        _In_ const DXGI_SWAP_CHAIN_DESC1& Desc,
        _In_opt_ const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* FullscreenDesc,
        _Outptr_ RxSwapChain** SwapChain);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "swapchain.inl"

    HWND
    GetWindow() const
    {
        return Window;
    }

    /* A CoreWindow swapchain renders to the window's HWND but answers for the CoreWindow */
    VOID
    AttachCoreWindow(
        _In_ IUnknown* Owner);

    IUnknown*
    GetCoreWindowIdentity() const
    {
        return CoreWindow;
    }

    /* DXGI_MWA flags from the factory; without NO_WINDOW_CHANGES the window's messages are watched */
    VOID
    SetWindowAssociation(
        _In_ UINT Flags);

    /* Alt+Enter reached the window */
    VOID
    OnAltEnter();

private:
    /* How DWM wants this window's frames: the order matches what the shared surface call reports */
    enum class Redirection
    {
        Surface,
        GdiSurface,
        GdiBlt,
        NotComposed
    };

    friend class RxSwapChainComposition;

    RxSwapChain(
        _In_ RxDxgiFactory& Factory,
        _In_ IRxPresentDevice* Device,
        _In_ HWND Window);

    ~RxSwapChain();

    HRESULT
    AllocateBuffers();

    VOID
    ReleaseBuffers();

    VOID
    LeaveFullscreen();

    /* Takes the window fullscreen on Target, or on the output holding it when Target is NULL */
    HRESULT
    EnterFullscreen(
        _In_opt_ IDXGIOutput* Target);

    /* Switches the output to the mode closest to Wanted when that is not the mode it shows */
    HRESULT
    ApplyTargetMode(
        _In_ IDXGIOutput* Output,
        _In_ const DXGI_MODE_DESC& Wanted);

    /* Stretches the window over the whole output, above other windows */
    VOID
    CoverOutput(
        _In_ IDXGIOutput* Output);

    VOID
    KeepPreviousFrame(
        _In_ const DXGI_PRESENT_PARAMETERS& Parameters);

    VOID
    CopyRegion(
        _In_ IUnknown* Source,
        _In_ HRGN Region,
        _In_ POINT Offset);

    BOOLEAN
    AreBuffersReleased();

    /* Asks DWM for the window's surface and opens it when it changed */
    Redirection
    PrepareRedirection(
        _In_ UINT SyncInterval,
        _Out_ UINT64* UpdateId);

    HRESULT
    PresentByCopy(
        _In_ UINT SyncInterval);

    /* Flip model through a composition surface; see swapchainflip.cpp */
    BOOLEAN
    UsesFlipPresent() const;

    HRESULT
    OpenCompositionSurface();

    HRESULT
    BindBuffers();

    VOID
    UnbindBuffers();

    HRESULT
    PresentFlip(
        _In_ UINT SyncInterval,
        _In_ UINT Flags);

    /* With no compositor, the kernel copies the frame straight into the window's visible part of the screen */
    HRESULT
    PresentToWindow(
        _In_ UINT SyncInterval);

    /* Last resort when the kernel will not take a window present: read back and draw with GDI */
    HRESULT
    PresentGdi(
        _In_ UINT SyncInterval);

    VOID
    ReleaseGdiCopies();

    LONG RefCount = 1;
    RxDxgiFactory& Factory;
    IRxPresentDevice* Device;
    LUID AdapterLuid = {};

    /* Blt presents are marked cross adapter when the adapter can share resources that way */
    BOOLEAN IsCrossAdapter = FALSE;
    /* NULL for a swapchain made for composition: DirectComposition shows its surface instead */
    HWND Window;
    /* IUnknown identity of the CoreWindow this swapchain was made for, if any */
    IUnknown* CoreWindow = nullptr;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    RxSwapChainComposition Composition;
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC FullscreenDesc = {};
    std::vector<IUnknown*> Buffers;

    /* Fullscreen covers the target output with the window; its windowed look is kept to put back */
    IDXGIOutput* FullscreenTarget = nullptr;
    LONG_PTR WindowedStyle = 0;
    LONG_PTR WindowedExStyle = 0;
    RECT WindowedRect = {};

    /* The display whose mode fullscreen changed, put back to its registry mode on leaving */
    BOOLEAN IsModeChanged = FALSE;
    WCHAR ModeDevice[CCHDEVICENAME] = {};

    /* Watches the window's thread for Alt+Enter */
    HHOOK WindowHook = NULL;

    HANDLE SharedSurface = NULL;
    IUnknown* RedirectionSurface = nullptr;

    /* Flip model: the surface DWM shows and this swapchain's binding of its buffers to it */
    HANDLE CompositionSurface = NULL;
    UINT64 BindingId = 0;
    BOOLEAN IsBound = FALSE;
    UINT FlipFence = 0;
    HANDLE FlipFenceHandle = NULL;
    UINT64 FenceValue = 0;
    std::vector<HANDLE> SharedBuffers;

    /* GDI presents: the CPU readable copy of the back buffer, and a resolve target when it is multisampled */
    ID3D11Texture2D* GdiStaging = nullptr;
    ID3D11Texture2D* GdiResolved = nullptr;
    UINT CurrentIndex = 0;
    UINT PresentedIndex = 0;

    std::array<RxPresentContext, 4> Contexts;
    UINT NextContext = 0;
    UINT PresentCount = 0;

    UINT AssociationFlags = 0;

    /* Frame latency semaphore, only for swapchains made with the waitable flag */
    HANDLE LatencySemaphore = NULL;
    UINT MaximumLatency = 1;
    DXGI_RGBA Background = { 0.0f, 0.0f, 0.0f, 1.0f };
    UINT SourceWidth = 0;
    UINT SourceHeight = 0;
    DXGI_COLOR_SPACE_TYPE CurrentColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_HDR_METADATA_TYPE MetadataType = DXGI_HDR_METADATA_TYPE_NONE;
    BOOLEAN IsMetadataChanged = FALSE;

    /* The last presented buffer still holds a whole frame, so partial presents can build on it */
    BOOLEAN IsPreviousFrameValid = FALSE;
    BYTE Metadata[sizeof(DXGI_HDR_METADATA_HDR10PLUS)] = {};
    DXGI_MODE_ROTATION Rotation = DXGI_MODE_ROTATION_IDENTITY;
    RxPrivateDataStore PrivateData;
};

/* Writes the back buffer to reactx-frame-<n>.bmp when dumpframes= in reactx.ini lists that present */
VOID
RxDumpFrame(
    _In_ IUnknown* Buffer,
    _In_ UINT PresentNumber);

/* How present=window or present=gdi in reactx.ini overrides the normal choice */
enum class RxPresentOverride
{
    None,
    Window,
    Gdi
};

RxPresentOverride
RxGetPresentOverride(VOID);

/* Waits out a sync interval on the output's vertical blank, or roughly when that is not available */
VOID
RxWaitForInterval(
    _In_ IDXGISwapChain* SwapChain,
    _In_ UINT SyncInterval);

/* One row of a back buffer format as 32 bit BGRA; FALSE for formats a swapchain can not have */
BOOLEAN
RxConvertRowToBgra(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(Width * 8) const BYTE* Source,
    _Out_writes_(Width) UINT* Destination,
    _In_ UINT Width);
