/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The desktop window manager's swapchain: primaries flipped onto one output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dxgiobjects.hpp"

#include <array>

/*
 * Made through IDXGIFactoryDWM for an output with no window. It shares ownership of the
 * output's video present source as the DWM, which is what stops GDI's display driver from
 * presenting there, and flips its own primaries instead.
 */
class RxDwmSwapChain final : public IDXGISwapChainDWM1
{
public:
    static
    HRESULT
    Create(
        _In_ RxDxgiFactory& Factory,
        _In_ IRxPresentDevice* Device,
        _In_ RxDxgiOutput* Output,
        _In_ const DXGI_SWAP_CHAIN_DESC& Desc,
        _Outptr_ RxDwmSwapChain** SwapChain);

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID riid, _COM_Outptr_ void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef(VOID) override;
    ULONG STDMETHODCALLTYPE Release(VOID) override;

    HRESULT STDMETHODCALLTYPE SetPrivateData(_In_ REFGUID Name, _In_ UINT DataSize, _In_reads_bytes_(DataSize) const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(_In_ REFGUID Name, _In_opt_ const IUnknown* pUnknown) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(_In_ REFGUID Name, _Inout_ UINT* pDataSize, _Out_writes_bytes_(*pDataSize) void* pData) override;
    HRESULT STDMETHODCALLTYPE GetParent(_In_ REFIID riid, _COM_Outptr_ void** ppParent) override;
    HRESULT STDMETHODCALLTYPE GetDevice(_In_ REFIID riid, _COM_Outptr_ void** ppDevice) override;

    HRESULT STDMETHODCALLTYPE Present(_In_ UINT SyncInterval, _In_ UINT Flags) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(_In_ UINT Buffer, _In_ REFIID riid, _COM_Outptr_ void** ppSurface) override;
    HRESULT STDMETHODCALLTYPE GetDesc(_Out_ DXGI_SWAP_CHAIN_DESC* pDesc) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(_In_ UINT BufferCount, _In_ UINT Width, _In_ UINT Height, _In_ DXGI_FORMAT NewFormat, _In_ UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(_In_ const DXGI_MODE_DESC* pNewTargetParameters) override;
    HRESULT STDMETHODCALLTYPE GetContainingOutput(_COM_Outptr_ IDXGIOutput** ppOutput) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(_Out_ DXGI_FRAME_STATISTICS* pStats) override;
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(_Out_ UINT* pLastPresentCount) override;
    HRESULT STDMETHODCALLTYPE PresentDWM(_In_ UINT SyncInterval, _In_ UINT Flags, _In_ UINT DirtyRectCount, _In_reads_opt_(DirtyRectCount) const RECT* pDirtyRects, _In_ UINT ScrollRectCount, _In_reads_opt_(ScrollRectCount) const DXGI_SCROLL_RECT* pScrollRects, _In_opt_ IDXGIResource* pResource, _In_ UINT Reserved) override;
    HRESULT STDMETHODCALLTYPE GetLogicalSurfaceHandle(_Out_ UINT64* pHandle) override;
    HRESULT STDMETHODCALLTYPE CheckDirectFlipSupport(_In_ UINT Flags, _In_ IDXGIResource* pResource, _Out_ BOOL* pSupported) override;
    HRESULT STDMETHODCALLTYPE GetCompositionSurface(_Out_ HANDLE* pSurface) override;
    HRESULT STDMETHODCALLTYPE GetFrameStatisticsDWM(_Out_ DXGI_FRAME_STATISTICS_DWM* pStats) override;
    HRESULT STDMETHODCALLTYPE GetMultiplaneOverlayCaps(_Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps) override;
    HRESULT STDMETHODCALLTYPE CheckMultiplaneOverlaySupport(_In_ UINT Count, _In_ const DXGI_CHECK_MULTIPLANEOVERLAYSUPPORT_PLANE_INFO* pInfo, _Out_ BOOL* pSupported, _Out_ BOOL* pIndividuallySupported) override;
    HRESULT STDMETHODCALLTYPE PresentMultiplaneOverlay(_In_ UINT SyncInterval, _In_ UINT Flags, _In_ DXGI_HDR_METADATA_TYPE MetadataType, _In_opt_ const void* pMetadata, _In_ UINT Count, _In_ const DXGI_PRESENT_MULTIPLANE_OVERLAY* pPlanes) override;
    HRESULT STDMETHODCALLTYPE CheckPresentDurationSupport(_In_ UINT Duration, _Out_ UINT* pClosestSmaller, _Out_ UINT* pClosestLarger) override;
    HRESULT STDMETHODCALLTYPE SetPrivateFrameDuration(_In_ UINT Duration, _In_ UINT Flags) override;
    HRESULT STDMETHODCALLTYPE SetHardwareProtection(_In_ BOOL Protect) override;
    HRESULT STDMETHODCALLTYPE GetHardwareProtection(_Out_ BOOL* pProtected) override;
    HRESULT STDMETHODCALLTYPE SetLatencyHint(_In_ UINT Hint) override;
    HRESULT STDMETHODCALLTYPE SwapBuffers(_In_ UINT First, _In_ UINT Second) override;
    HRESULT STDMETHODCALLTYPE CheckDwmVidPnOwnership(_Out_ BOOL* pOwned) override;
    HRESULT STDMETHODCALLTYPE Reserved31() override;
    UINT STDMETHODCALLTYPE GetBackBufferImplicitRotationCount() override;
    UINT STDMETHODCALLTYPE GetFrontBufferRenderingCapability() override;
    HRESULT STDMETHODCALLTYPE SetFrontBufferRenderingMode(_In_ BOOL Enable) override;
    HRESULT STDMETHODCALLTYPE SetPlaneFlipAwaySignals(_In_ UINT Count, _In_reads_(Count) const DXGI_PRESENT_PLANE_FLIP_AWAY_SIGNAL* pSignals) override;

private:
    RxDwmSwapChain(
        _In_ RxDxgiFactory& Factory,
        _In_ IRxPresentDevice* Device,
        _In_ RxDxgiOutput* Output);

    ~RxDwmSwapChain();

    HRESULT
    TakeOwnership();

    VOID
    ReleaseOwnership();

    HRESULT
    CreateBuffers();

    VOID
    ReleaseBuffers();

    /* Hands buffer 0 to the kernel's desktop duplication; see dwmswapchain.cpp */
    VOID
    PresentToDuplication();

    LONG RefCount = 1;
    RxDxgiFactory& Factory;
    IRxPresentDevice* Device;
    RxDxgiOutput* Output;
    DXGI_SWAP_CHAIN_DESC Desc = {};
    std::vector<IUnknown*> Buffers;
    BOOLEAN OwnsSource = FALSE;
    /* A frame takes two contexts while it is duplicated */
    std::array<RxPresentContext, 8> Contexts;
    UINT NextContext = 0;
    UINT PresentCount = 0;
    RxPrivateDataStore PrivateData;
};
