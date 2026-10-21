/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DXGI interfaces only the desktop window manager uses
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* What a DWM output reports about itself; 0xC8 bytes, the fields past Flags are not used yet */
struct DXGI_OUTPUT_DWM_DESC
{
    LUID AdapterLuid;
    UINT VidPnSourceId;
    UINT VidPnTargetId;
    UINT Reserved0;
    BYTE Reserved1[8];
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    DXGI_RATIONAL RefreshRate;
    DXGI_RATIONAL MinimumRefreshRate;
    DXGI_RATIONAL MaximumRefreshRate;
    UINT RefreshMultiplier;
    DXGI_MODE_ROTATION Rotation;
    DXGI_MODE_SCANLINE_ORDER ScanlineOrdering;
    RECT Reserved2;
    RECT Reserved3;
    UINT Flags;
    BYTE Reserved4[64];
    FLOAT SdrWhiteLevel;
    UINT SyncLockGroup;
    BOOL Reserved5;
    UINT Reserved6;
    UINT64 Reserved7;
};

static_assert(sizeof(DXGI_OUTPUT_DWM_DESC) == 0xC8);
static_assert(FIELD_OFFSET(DXGI_OUTPUT_DWM_DESC, Width) == 0x1C);
static_assert(FIELD_OFFSET(DXGI_OUTPUT_DWM_DESC, RefreshRate) == 0x28);
static_assert(FIELD_OFFSET(DXGI_OUTPUT_DWM_DESC, Flags) == 0x6C);
static_assert(FIELD_OFFSET(DXGI_OUTPUT_DWM_DESC, SdrWhiteLevel) == 0xB0);

/* The kernel's DWM present statistics without their size field */
struct DXGI_FRAME_STATISTICS_DWM
{
    UINT PresentCount;
    UINT PresentRefreshCount;
    LARGE_INTEGER PresentQPCTime;
    UINT SyncRefreshCount;
    LARGE_INTEGER SyncQPCTime;
    UINT CustomPresentDuration;
    UINT VirtualSyncRefreshCount;
    LARGE_INTEGER VirtualSyncQPCTime;
    UINT VirtualPresentRefreshCount;
    LARGE_INTEGER VirtualPresentQPCTime;
    LARGE_INTEGER VSyncDurationQPCTime;
    UINT VSyncMultiplier;
};

static_assert(sizeof(DXGI_FRAME_STATISTICS_DWM) == 0x50);

struct DXGI_SCROLL_RECT;
struct DXGI_MULTIPLANE_OVERLAY_CAPS;
struct DXGI_CHECK_MULTIPLANEOVERLAYSUPPORT_PLANE_INFO;
struct DXGI_PRESENT_MULTIPLANE_OVERLAY;
struct DXGI_PRESENT_PLANE_FLIP_AWAY_SIGNAL;

struct __declspec(uuid("6f66a9a0-bece-4ee8-b11b-990eb38ed976")) IDXGIOutputDWM : public IUnknown
{
    virtual BOOL STDMETHODCALLTYPE HasDDAClient() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_OUTPUT_DWM_DESC* pDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE FindClosestMatchingModeFromDesktop(_In_ const DXGI_MODE_DESC1* pModeToMatch, _Out_ DXGI_MODE_DESC1* pClosestMatch, _In_opt_ IUnknown* pConcernedDevice) = 0;
    virtual HRESULT STDMETHODCALLTYPE WaitForVBlankOrObjects(_In_ UINT Count, _In_reads_(Count) const HANDLE* pHandles) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSyncRefreshCountWaitTarget(_In_ UINT Target) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatisticsDWM(_Out_ DXGI_FRAME_STATISTICS_DWM* pStats) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetVBlankEvent(_Out_ HANDLE* pEvent) = 0;
    virtual BOOL STDMETHODCALLTYPE IsIndependentFlipSupported() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMultiplaneOverlayCaps(_In_ IUnknown* pDevice, _Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetStereoCaps(_Out_ UINT* pCaps) = 0;
};

/* One vtable serves both DWM swapchain interfaces */
struct __declspec(uuid("fc4f7700-8c88-43fb-aa4f-44c4a584dc19")) IDXGISwapChainDWM1 : public IDXGIDeviceSubObject
{
    virtual HRESULT STDMETHODCALLTYPE Present(_In_ UINT SyncInterval, _In_ UINT Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(_In_ UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResizeBuffers(_In_ UINT BufferCount, _In_ UINT Width, _In_ UINT Height, _In_ DXGI_FORMAT NewFormat, _In_ UINT SwapChainFlags) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount) = 0;
    virtual HRESULT STDMETHODCALLTYPE PresentDWM(_In_ UINT SyncInterval, _In_ UINT Flags, _In_ UINT DirtyRectCount, _In_reads_opt_(DirtyRectCount) const RECT* pDirtyRects, _In_ UINT ScrollRectCount, _In_reads_opt_(ScrollRectCount) const DXGI_SCROLL_RECT* pScrollRects, _In_opt_ IDXGIResource* pResource, _In_ UINT Reserved) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetLogicalSurfaceHandle(_Out_ UINT64* pHandle) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDirectFlipSupport(_In_ UINT Flags, _In_ IDXGIResource* pResource, _Out_ BOOL* pSupported) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCompositionSurface(_Out_ HANDLE* pSurface) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetFrameStatisticsDWM(_Out_ DXGI_FRAME_STATISTICS_DWM* pStats) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMultiplaneOverlayCaps(_Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckMultiplaneOverlaySupport(_In_ UINT Count, _In_ const DXGI_CHECK_MULTIPLANEOVERLAYSUPPORT_PLANE_INFO* pInfo, _Out_ BOOL* pSupported, _Out_ BOOL* pIndividuallySupported) = 0;
    virtual HRESULT STDMETHODCALLTYPE PresentMultiplaneOverlay(_In_ UINT SyncInterval, _In_ UINT Flags, _In_ DXGI_HDR_METADATA_TYPE MetadataType, _In_opt_ const void* pMetadata, _In_ UINT Count, _In_ const DXGI_PRESENT_MULTIPLANE_OVERLAY* pPlanes) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckPresentDurationSupport(_In_ UINT Duration, _Out_ UINT* pClosestSmaller, _Out_ UINT* pClosestLarger) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPrivateFrameDuration(_In_ UINT Duration, _In_ UINT Flags) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetHardwareProtection(_In_ BOOL Protect) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetHardwareProtection(_Out_ BOOL* pProtected) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetLatencyHint(_In_ UINT Hint) = 0;
    virtual HRESULT STDMETHODCALLTYPE SwapBuffers(_In_ UINT First, _In_ UINT Second) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckDwmVidPnOwnership(_Out_ BOOL* pOwned) = 0;
    virtual HRESULT STDMETHODCALLTYPE Reserved31() = 0;
    virtual UINT STDMETHODCALLTYPE GetBackBufferImplicitRotationCount() = 0;
    virtual UINT STDMETHODCALLTYPE GetFrontBufferRenderingCapability() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetFrontBufferRenderingMode(_In_ BOOL Enable) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPlaneFlipAwaySignals(_In_ UINT Count, _In_reads_(Count) const DXGI_PRESENT_PLANE_FLIP_AWAY_SIGNAL* pSignals) = 0;
};

inline constexpr GUID IID_IDXGISwapChainDWM = { 0xf69f223b, 0x45d3, 0x4aa0, { 0x98, 0xc8, 0xc4, 0x0c, 0x2b, 0x23, 0x10, 0x29 } };

struct __declspec(uuid("713f394e-92ca-47e7-ab81-1159c2791e54")) IDXGIFactoryDWM : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE CreateSwapChain(_In_ IUnknown* pDevice, _In_ DXGI_SWAP_CHAIN_DESC* pDesc, _In_ IDXGIOutput* pOutput, _COM_Outptr_ IDXGISwapChainDWM1** ppSwapChain) = 0;
};
