/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXGI factory, adapter and output objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"
#include "dxgievents.hpp"

/* Basic Render, the adapter Windows falls back to when no driver renders */
constexpr UINT RxBasicRenderVendor = 0x1414;
constexpr UINT RxBasicRenderDevice = 0x008C;

class RxDxgiFactory;
class RxSwapChain;

/* One display output, as the factory found it when it was created */
struct RxOutputInfo
{
    WCHAR DeviceName[CCHDEVICENAME];
    UINT VidPnSourceId;
};

/* One adapter in the factory's list; outputs are the attached ones, primary first */
struct RxAdapterInfo
{
    LUID Luid;
    std::vector<RxOutputInfo> Outputs;
    D3DKMT_ADAPTERTYPE Type;
    BOOLEAN IsBasicRender;
};

/*
 * The process wide kernel handle for an adapter. Handles stay open for the life of the
 * process, as the kernel keeps per process adapter state only while one is open.
 */
HRESULT
RxOpenKernelAdapter(
    _In_ const LUID& Luid,
    _Out_ D3DKMT_HANDLE* Adapter);

/* One kernel adapter as DXGI describes it; the factory that made it is its parent */
class RxDxgiAdapter final : public IDXGIAdapter4
{
public:
    static
    HRESULT
    Create(
        _In_ RxDxgiFactory* Factory,
        _In_ const RxAdapterInfo& Info,
        _Outptr_ RxDxgiAdapter** Adapter);

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

#include "dxgiadapter.inl"

    const LUID&
    GetLuid() const
    {
        return Desc.AdapterLuid;
    }

private:
    explicit RxDxgiAdapter(
        _In_ RxDxgiFactory* Factory);

    ~RxDxgiAdapter();

    HRESULT
    Describe(
        _In_ const RxAdapterInfo& Info);

    LONG RefCount = 1;
    RxDxgiFactory* Factory;
    D3DKMT_HANDLE KmtAdapter = 0;
    DXGI_ADAPTER_DESC1 Desc = {};
    DXGI_GRAPHICS_PREEMPTION_GRANULARITY GraphicsPreemption = DXGI_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
    DXGI_COMPUTE_PREEMPTION_GRANULARITY ComputePreemption = DXGI_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY;
    UINT ExtraFlags = 0;
    RxEventRegistry BudgetEvents;
    RxEventRegistry TeardownEvents;
    std::vector<RxOutputInfo> Outputs;
    LARGE_INTEGER UmdVersion = {};
    BOOLEAN HasUmdVersion = FALSE;
    RxPrivateDataStore PrivateData;
};

/* One display an adapter drives; the adapter that enumerated it is its parent */
class RxDxgiOutput final : public IDXGIOutput6, public IDXGIOutputDWM
{
public:
    static
    HRESULT
    Create(
        _In_ RxDxgiAdapter* Adapter,
        _In_ const RxOutputInfo& Info,
        _Outptr_ RxDxgiOutput** Output);

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

#include "dxgioutput.inl"

    /* The kernel's id for the video present source behind this output */
    UINT
    GetVidPnSourceId() const
    {
        return Info.VidPnSourceId;
    }

    const LUID&
    GetAdapterLuid() const;

    /* The process wide kernel adapter handle that answers for this output */
    D3DKMT_HANDLE
    GetKernelAdapter() const
    {
        return KmtAdapter;
    }

    /* What the desktop window manager asks an output; see dwmoutput.cpp */
    BOOL
    STDMETHODCALLTYPE
    HasDDAClient() override;

    HRESULT
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ DXGI_OUTPUT_DWM_DESC* pDesc) override;

    HRESULT
    STDMETHODCALLTYPE
    FindClosestMatchingModeFromDesktop(
        _In_ const DXGI_MODE_DESC1* pModeToMatch,
        _Out_ DXGI_MODE_DESC1* pClosestMatch,
        _In_opt_ IUnknown* pConcernedDevice) override;

    HRESULT
    STDMETHODCALLTYPE
    WaitForVBlankOrObjects(
        _In_ UINT Count,
        _In_reads_(Count) const HANDLE* pHandles) override;

    HRESULT
    STDMETHODCALLTYPE
    SetSyncRefreshCountWaitTarget(
        _In_ UINT Target) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFrameStatisticsDWM(
        _Out_ DXGI_FRAME_STATISTICS_DWM* pStats) override;

    HRESULT
    STDMETHODCALLTYPE
    GetVBlankEvent(
        _Out_ HANDLE* pEvent) override;

    BOOL
    STDMETHODCALLTYPE
    IsIndependentFlipSupported() override;

    HRESULT
    STDMETHODCALLTYPE
    GetMultiplaneOverlayCaps(
        _In_ IUnknown* pDevice,
        _Out_ DXGI_MULTIPLANE_OVERLAY_CAPS* pCaps) override;

    HRESULT
    STDMETHODCALLTYPE
    GetStereoCaps(
        _Out_ UINT* pCaps) override;

private:
    explicit RxDxgiOutput(
        _In_ RxDxgiAdapter* Adapter);

    ~RxDxgiOutput();

    /* Modes in DXGI terms for one format, sorted, before any flag filtering */
    HRESULT
    ListModes(
        _Out_ std::vector<DXGI_MODE_DESC1>& Modes);

    LONG RefCount = 1;
    RxDxgiAdapter* Adapter;
    RxOutputInfo Info = {};
    D3DKMT_HANDLE KmtAdapter = 0;
    RxPrivateDataStore PrivateData;
};

class RxDxgiFactory final : public IDXGIFactory7, public IDXGIFactoryDWM
{
public:
    /* Creation flag for factories made by CreateDXGIFactory; GetCreationFlags reports it as Windows does */
    static constexpr UINT LegacyFactory = 0x40000000;

    static
    HRESULT
    Create(
        _In_ UINT Flags,
        _Outptr_ RxDxgiFactory** Factory);

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

#include "dxgifactory.inl"


    /* Swapchains add themselves when made and remove themselves when destroyed */
    VOID
    TrackSwapChain(
        _In_ RxSwapChain* SwapChain);

    VOID
    UntrackSwapChain(
        _In_ RxSwapChain* SwapChain);

    /* The DXGI_MWA flags a new swapchain made here starts with */
    UINT
    GetAssociationFlags() const
    {
        return AssociationFlags;
    }

    /* Alt+Enter: every swapchain of this factory leaves fullscreen if one is in it, else all enter */
    VOID
    ToggleFullscreen();

    /* The desktop window manager's fullscreen swapchain for one output */
    HRESULT
    STDMETHODCALLTYPE
    CreateSwapChain(
        _In_ IUnknown* pDevice,
        _In_ DXGI_SWAP_CHAIN_DESC* pDesc,
        _In_ IDXGIOutput* pOutput,
        _COM_Outptr_ IDXGISwapChainDWM1** ppSwapChain) override;

private:
    RxDxgiFactory() = default;
    ~RxDxgiFactory() = default;

    /* Takes the adapter and output snapshot the factory enumerates from */
    HRESULT
    RefreshAdapterList();

    /* Makes the adapter for one snapshot entry and asks it for riid */
    HRESULT
    CreateAdapter(
        _In_ const RxAdapterInfo& Info,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvAdapter);

    LONG RefCount = 1;
    UINT CreationFlags = 0;
    std::vector<RxAdapterInfo> Adapters;
    RxPrivateDataStore PrivateData;
    RxEventRegistry AdapterEvents;
    RxEventRegistry OcclusionEvents;
    RxEventRegistry StereoEvents;

    /* Swapchains made here, so window association flags can reach them */
    SRWLOCK SwapChainLock = SRWLOCK_INIT;
    std::vector<RxSwapChain*> SwapChains;

    /* Flags a MakeWindowAssociation for every window left for swapchains made later */
    UINT AssociationFlags = 0;
};
