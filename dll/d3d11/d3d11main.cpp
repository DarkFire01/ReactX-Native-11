/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation entry points
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

extern const WCHAR RxTraceModule[] = L"d3d11";

/* exports.cpp */
VOID
RxInitializeLayers(VOID);

struct FeatureLevelMapping
{
    D3D_FEATURE_LEVEL FeatureLevel;
    D3D11DDI_3DPIPELINELEVEL PipelineLevel;
    UINT MinimumWinnt;
};

static constexpr FeatureLevelMapping FeatureLevelTable[] =
{
    { D3D_FEATURE_LEVEL_12_1, D3DWDDM2_0DDI_3DPIPELINELEVEL_12_1, 0x0A00 },
    { D3D_FEATURE_LEVEL_12_0, D3DWDDM2_0DDI_3DPIPELINELEVEL_12_0, 0x0A00 },
    { D3D_FEATURE_LEVEL_11_1, D3D11_1DDI_3DPIPELINELEVEL_11_1, 0x0602 },
    { D3D_FEATURE_LEVEL_11_0, D3D11DDI_3DPIPELINELEVEL_11_0, 0x0602 },
    { D3D_FEATURE_LEVEL_10_1, D3D11DDI_3DPIPELINELEVEL_10_1, 0x0602 },
    { D3D_FEATURE_LEVEL_10_0, D3D11DDI_3DPIPELINELEVEL_10_0, 0x0602 },
    { D3D_FEATURE_LEVEL_9_3, D3D11_1DDI_3DPIPELINELEVEL_9_3, 0x0602 },
    { D3D_FEATURE_LEVEL_9_2, D3D11_1DDI_3DPIPELINELEVEL_9_2, 0x0602 },
    { D3D_FEATURE_LEVEL_9_1, D3D11_1DDI_3DPIPELINELEVEL_9_1, 0x0602 },
};

/* Used when the caller passes no list; 11_1 and above must be asked for by name */
static constexpr D3D_FEATURE_LEVEL DefaultFeatureLevels[] =
{
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
};



_Ret_maybenull_
static
const FeatureLevelMapping*
RxFindFeatureLevel(
    _In_ D3D_FEATURE_LEVEL FeatureLevel)
{
    for (const FeatureLevelMapping& Mapping : FeatureLevelTable)
    {
        if (Mapping.FeatureLevel == FeatureLevel && Mapping.MinimumWinnt <= _WIN32_WINNT)
            return &Mapping;
    }

    return nullptr;
}

/* WARP is the user mode driver of the kernel's software adapter, Microsoft Basic Render */
static
HRESULT
RxFindSoftwareAdapter(
    _Out_ LUID* Luid)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_ADAPTERINFO Adapters[16];
    D3DKMT_ENUMADAPTERS2 Enum = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    D3DKMT_ADAPTERTYPE Type;
    D3DKMT_CLOSEADAPTER Close;
    BOOLEAN IsFound = FALSE;
    UINT Index;

    if (Kmt == nullptr || Kmt->EnumAdapters2 == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Enum.NumAdapters = _countof(Adapters);
    Enum.pAdapters = Adapters;
    if (!NT_SUCCESS(Kmt->EnumAdapters2(&Enum)))
        return DXGI_ERROR_UNSUPPORTED;

    for (Index = 0; Index < Enum.NumAdapters; Index++)
    {
        Type = {};
        Query.hAdapter = Adapters[Index].hAdapter;
        Query.Type = KMTQAITYPE_ADAPTERTYPE;
        Query.pPrivateDriverData = &Type;
        Query.PrivateDriverDataSize = sizeof(Type);
        if (!IsFound && NT_SUCCESS(Kmt->QueryAdapterInfo(&Query)) && Type.SoftwareDevice && Type.RenderSupported)
        {
            *Luid = Adapters[Index].AdapterLuid;
            IsFound = TRUE;
        }

        Close.hAdapter = Adapters[Index].hAdapter;
        Kmt->CloseAdapter(&Close);
    }

    return IsFound ? S_OK : DXGI_ERROR_UNSUPPORTED;
}

static
HRESULT
RxOpenAdapter(
    _In_opt_ IDXGIAdapter* Adapter,
    _In_ D3D_DRIVER_TYPE DriverType,
    _Inout_ UmdAdapter& Umd)
{
    DXGI_ADAPTER_DESC Desc;
    LUID Software;
    HRESULT hr;

    if (DriverType == D3D_DRIVER_TYPE_WARP)
    {
        hr = RxFindSoftwareAdapter(&Software);
        if (FAILED(hr))
            return hr;
        return Umd.Open(&Software);
    }

    if (Adapter == nullptr)
        return Umd.Open(nullptr);

    hr = Adapter->GetDesc(&Desc);
    if (FAILED(hr))
        return hr;

    return Umd.Open(&Desc.AdapterLuid);
}

extern "C"
HRESULT
WINAPI
D3D11CreateDevice(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_reads_opt_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    _In_ UINT FeatureLevels,
    _In_ UINT SDKVersion,
    _COM_Outptr_opt_ ID3D11Device** ppDevice,
    _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
    _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext)
{
    const FeatureLevelMapping* Chosen = nullptr;
    const FeatureLevelMapping* Mapping;
    UmdAdapter* Umd;
    RxDevice* Device;
    UINT Index;
    HRESULT hr;

    if (ppDevice != nullptr)
        *ppDevice = nullptr;
    if (ppImmediateContext != nullptr)
        *ppImmediateContext = nullptr;
    if (pFeatureLevel != nullptr)
        *pFeatureLevel = (D3D_FEATURE_LEVEL)0;

    if (SDKVersion != D3D11_SDK_VERSION)
        return E_INVALIDARG;
    if ((Flags & ~RxKnownCreateFlags) != 0)
        return E_INVALIDARG;

    /* An explicit adapter goes with UNKNOWN, and UNKNOWN needs an adapter */
    if ((pAdapter != nullptr) != (DriverType == D3D_DRIVER_TYPE_UNKNOWN))
        return E_INVALIDARG;
    if (DriverType == D3D_DRIVER_TYPE_SOFTWARE && Software == NULL)
        return E_INVALIDARG;
    if (DriverType != D3D_DRIVER_TYPE_UNKNOWN && DriverType != D3D_DRIVER_TYPE_HARDWARE &&
        DriverType != D3D_DRIVER_TYPE_WARP)
    {
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (pFeatureLevels == nullptr)
    {
        pFeatureLevels = DefaultFeatureLevels;
        FeatureLevels = _countof(DefaultFeatureLevels);
    }
    else if (FeatureLevels == 0)
    {
        return E_INVALIDARG;
    }

    for (Index = 0; Index < FeatureLevels; Index++)
    {
        if (RxFindFeatureLevel(pFeatureLevels[Index]) == nullptr)
            return E_INVALIDARG;
    }

    Umd = new (std::nothrow) UmdAdapter();
    if (Umd == nullptr)
        return E_OUTOFMEMORY;

    hr = RxOpenAdapter(pAdapter, DriverType, *Umd);
    if (FAILED(hr))
        goto Cleanup;

    for (Index = 0; Index < FeatureLevels; Index++)
    {
        Mapping = RxFindFeatureLevel(pFeatureLevels[Index]);
        if (Umd->SupportsPipelineLevel(Mapping->PipelineLevel))
        {
            Chosen = Mapping;
            break;
        }
    }

    if (Chosen == nullptr)
    {
        hr = DXGI_ERROR_UNSUPPORTED;
        goto Cleanup;
    }

    RX_TRACE(L"feature level 0x%04X", Chosen->FeatureLevel);

    if (pFeatureLevel != nullptr)
        *pFeatureLevel = Chosen->FeatureLevel;

    /* Asking for neither interface only checks support */
    if (ppDevice == nullptr && ppImmediateContext == nullptr)
    {
        hr = S_FALSE;
        goto Cleanup;
    }

    /* The device owns the adapter from here on, even when creation fails */
    hr = RxDevice::Create(Umd, Chosen->FeatureLevel, Chosen->PipelineLevel, Flags, &Device);
    Umd = nullptr;
    if (FAILED(hr))
        goto Cleanup;

    if (pAdapter != nullptr)
        Device->SetDxgiAdapter(pAdapter);

    if (ppImmediateContext != nullptr)
        Device->GetImmediateContext(ppImmediateContext);

    if (ppDevice != nullptr)
        *ppDevice = Device;
    else
        Device->Release();

Cleanup:
    delete Umd;
    return hr;
}

extern "C"
HRESULT
WINAPI
D3D11CreateDeviceAndSwapChain(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_reads_opt_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    _In_ UINT FeatureLevels,
    _In_ UINT SDKVersion,
    _In_opt_ const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    _COM_Outptr_opt_ IDXGISwapChain** ppSwapChain,
    _COM_Outptr_opt_ ID3D11Device** ppDevice,
    _Out_opt_ D3D_FEATURE_LEVEL* pFeatureLevel,
    _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext)
{
    ID3D11Device* Device = nullptr;
    IDXGIDevice* DxgiDevice = nullptr;
    IDXGIAdapter* Adapter = nullptr;
    IDXGIFactory* Factory = nullptr;
    DXGI_SWAP_CHAIN_DESC Desc;
    HRESULT hr;

    if (ppSwapChain != nullptr)
        *ppSwapChain = nullptr;
    if (ppDevice != nullptr)
        *ppDevice = nullptr;
    if (ppImmediateContext != nullptr)
        *ppImmediateContext = nullptr;

    hr = D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                           (ppSwapChain || ppDevice || ppImmediateContext) ? &Device : nullptr, pFeatureLevel,
                           nullptr);
    if (FAILED(hr) || Device == nullptr)
        return hr;

    /* The swapchain comes from the factory the device's adapter belongs to; no description, no swapchain */
    if (ppSwapChain != nullptr && pSwapChainDesc != nullptr)
    {
        Desc = *pSwapChainDesc;
        hr = Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice));
        if (SUCCEEDED(hr))
            hr = DxgiDevice->GetAdapter(&Adapter);
        if (SUCCEEDED(hr))
            hr = Adapter->GetParent(IID_PPV_ARGS(&Factory));
        if (SUCCEEDED(hr))
            hr = Factory->CreateSwapChain(Device, &Desc, ppSwapChain);

        if (Factory != nullptr)
            Factory->Release();
        if (Adapter != nullptr)
            Adapter->Release();
        if (DxgiDevice != nullptr)
            DxgiDevice->Release();

        if (FAILED(hr))
        {
            Device->Release();
            return hr;
        }
    }

    if (ppImmediateContext != nullptr)
        Device->GetImmediateContext(ppImmediateContext);
    if (ppDevice != nullptr)
        *ppDevice = Device;
    else
        Device->Release();

    return hr;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ PVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        RxInitializeLayers();
    }

    return TRUE;
}
