/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DDI adapter the Direct3D 11 runtime opens over a D3D12 device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device.hpp"

/* The DDI revisions this layer speaks, oldest first */
static const UINT64 O12DdiRevisions[] =
{
    D3DWDDM2_6_DDI_SUPPORTED,
    D3DWDDM2_7_DDI_SUPPORTED,
};

/* Levels asked for once at open to learn whether the device draws at all */
static const D3D_FEATURE_LEVEL O12OpenLevels[] =
{
    D3D_FEATURE_LEVEL_1_0_CORE,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_9_1,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_3,
};

/* Levels the 3D pipeline caps answer is built from */
static const D3D_FEATURE_LEVEL O12PipelineLevels[] =
{
    D3D_FEATURE_LEVEL_1_0_CORE,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_12_1,
};

/* D3D12CreateDevice from whichever d3d12.dll the process has */
static
HRESULT
O12CreateD3D12Device(
    _In_opt_ IUnknown* Adapter,
    _Outptr_ ID3D12Device** Device)
{
    PFN_D3D12_CREATE_DEVICE Create;
    HMODULE Module;

    Module = GetModuleHandleW(L"d3d12.dll");
    if (Module == NULL)
        Module = LoadLibraryW(L"d3d12.dll");
    if (Module == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    Create = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(Module, "D3D12CreateDevice"));
    if (Create == nullptr)
        return E_NOINTERFACE;

    return Create(Adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device1), reinterpret_cast<void**>(Device));
}

HRESULT
O12Adapter::Open(
    _Inout_ D3D10DDIARG_OPENADAPTER* Args,
    _Inout_ On12OpenArgs* On12)
{
    O12Adapter* Adapter;
    HRESULT hr;

    Adapter = new (std::nothrow) O12Adapter();
    if (Adapter == nullptr)
        return E_OUTOFMEMORY;

    Args->pAdapterFuncs_2->pfnCalcPrivateDeviceSize = CalcPrivateDeviceSize;
    Args->pAdapterFuncs_2->pfnCreateDevice = CreateDevice;
    Args->pAdapterFuncs_2->pfnCloseAdapter = CloseAdapter;
    Args->pAdapterFuncs_2->pfnGetSupportedVersions = GetSupportedVersions;
    Args->pAdapterFuncs_2->pfnGetCaps = GetCaps;
    Args->hAdapter.pDrvPrivate = Adapter;

    hr = Adapter->Initialize(On12);
    if (FAILED(hr))
    {
        O12Trace(L"adapter open failed (0x%08lX)", hr);
        delete Adapter;
    }

    return hr;
}

HRESULT
O12Adapter::Initialize(
    _Inout_ On12OpenArgs* On12)
{
    D3D12_FEATURE_DATA_FEATURE_LEVELS Levels = {};
    D3D12_FEATURE_DATA_SHADER_MODEL ShaderModel;
    BOOLEAN HasRevision5 = On12->ContractVersion >= 5;
    BOOLEAN HasRevision7 = On12->ContractVersion >= 7;
    HRESULT hr;

    Device = On12->Device;
    if (Device != nullptr)
        Device->AddRef();
    GraphicsQueue = On12->GraphicsQueue;
    NodeIndex = On12->NodeIndex;
    Callbacks = On12->Callbacks;
    PresentCallback = HasRevision7 ? reinterpret_cast<PVOID>(On12->Callbacks2->Present) : nullptr;
    DisableGpuTimeout = On12->DisableGpuTimeout;
    DisplayableTextures = HasRevision5 ? On12->DisplayableTextures : false;
    DeferredContexts = HasRevision5 ? On12->DeferredContexts : true;
    PresentCallbackPresent = HasRevision7 && On12->Callbacks2->Present != nullptr;
    PrepatchedShaders = HasRevision7 ? On12->PrepatchedShaders : false;
    Architecture.NodeIndex = NodeIndex;

    if (Device == nullptr)
    {
        hr = O12CreateD3D12Device(On12->Adapter, &Device);
        if (FAILED(hr))
            return hr;
    }

    Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &Architecture, sizeof(Architecture));
    Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options));

    Levels.NumFeatureLevels = _countof(O12OpenLevels);
    Levels.pFeatureLevelsRequested = O12OpenLevels;
    Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &Levels, sizeof(Levels));
    ComputeOnly = Levels.MaxSupportedFeatureLevel == D3D_FEATURE_LEVEL_1_0_CORE;

    ShaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_2;
    Device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &ShaderModel, sizeof(ShaderModel));
    HighestShaderModel = ShaderModel.HighestShaderModel;

    if (On12->ContractVersion < On12ContractVersion)
        On12->ContractVersion = On12ContractVersion;

    return S_OK;
}

O12Adapter::~O12Adapter()
{
    if (Compatibility != nullptr)
        Compatibility->Release();
    if (Device != nullptr)
        Device->Release();
}

SIZE_T
APIENTRY
O12Adapter::CalcPrivateDeviceSize(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE* Args)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Args);

    return sizeof(O12Device);
}

HRESULT
APIENTRY
O12Adapter::CreateDevice(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args)
{
    O12Adapter* Self = From(Adapter);
    HRESULT hr;

    hr = O12Device::Create(Self, Args);
    if (FAILED(hr))
    {
        O12Trace(L"device creation failed (0x%08lX)", hr);
        return hr;
    }

    /* Every device asks again; the reference taken by an earlier one is kept */
    return Self->Device->QueryInterface(__uuidof(ID3D12CompatibilityDevice),
                                        reinterpret_cast<void**>(&Self->Compatibility));
}

HRESULT
APIENTRY
O12Adapter::CloseAdapter(
    _In_ D3D10DDI_HADAPTER Adapter)
{
    delete From(Adapter);
    return S_OK;
}

HRESULT
APIENTRY
O12Adapter::GetSupportedVersions(
    _In_ D3D10DDI_HADAPTER Adapter,
    _Inout_ UINT32* Count,
    _Out_writes_opt_(*Count) UINT64* Versions)
{
    UNREFERENCED_PARAMETER(Adapter);

    *Count = _countof(O12DdiRevisions);
    if (Versions != nullptr)
        memcpy(Versions, O12DdiRevisions, sizeof(O12DdiRevisions));

    return S_OK;
}

HRESULT
O12Adapter::GetPipelineCaps(
    _Out_ UINT* Caps) const
{
    D3D12_FEATURE_DATA_FEATURE_LEVELS Levels;
    D3D_FEATURE_LEVEL Highest;

    Levels.NumFeatureLevels = _countof(O12PipelineLevels);
    Levels.pFeatureLevelsRequested = O12PipelineLevels;
    Levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_1;
    if (FAILED(Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &Levels, sizeof(Levels))))
        return E_INVALIDARG;

    /* Tier 2 tiled resources are reported as tier 1, which keeps 12_x off the list */
    Highest = Levels.MaxSupportedFeatureLevel;
    if (Options.TiledResourcesTier == D3D12_TILED_RESOURCES_TIER_2 && Highest > D3D_FEATURE_LEVEL_11_1)
        Highest = D3D_FEATURE_LEVEL_11_1;

    *Caps = D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_0) |
            D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_10_1) |
            D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11DDI_3DPIPELINELEVEL_11_0);
    if (Highest >= D3D_FEATURE_LEVEL_11_1)
        *Caps |= D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3D11_1DDI_3DPIPELINELEVEL_11_1);
    if (Highest >= D3D_FEATURE_LEVEL_12_0)
        *Caps |= D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3DWDDM2_0DDI_3DPIPELINELEVEL_12_0);
    if (Highest >= D3D_FEATURE_LEVEL_12_1)
        *Caps |= D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(D3DWDDM2_0DDI_3DPIPELINELEVEL_12_1);

    return S_OK;
}

UINT
O12Adapter::GetTiledResourceCaps() const
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 Options5 = {};

    if (Options.ResourceHeapTier < D3D12_RESOURCE_HEAP_TIER_2)
        return 0;

    switch (Options.TiledResourcesTier)
    {
        case D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED:
            return 0;

        case D3D12_TILED_RESOURCES_TIER_1:
            return D3DWDDM1_3DDI_TILED_RESOURCES_TIER_1_SUPPORTED;

        case D3D12_TILED_RESOURCES_TIER_2:
            if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &Options5, sizeof(Options5))) &&
                Options5.SRVOnlyTiledResourceTier3)
            {
                return D3DWDDM1_3DDI_TILED_RESOURCES_TIER_2_SUPPORTED;
            }
            return D3DWDDM1_3DDI_TILED_RESOURCES_TIER_1_SUPPORTED;

        default:
            return D3DWDDM2_0DDI_TILED_RESOURCES_TIER_3_SUPPORTED;
    }
}

HRESULT
APIENTRY
O12Adapter::GetCaps(
    _In_ D3D10DDI_HADAPTER Adapter,
    _In_ const D3D10_2DDIARG_GETCAPS* Args)
{
    O12Adapter* Self = From(Adapter);
    UINT* Value = static_cast<UINT*>(Args->pData);
    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Address;
    UINT Precision;
    UINT Caps;

    switch (Args->Type)
    {
        case D3D11DDICAPS_THREADING:
            if (Args->DataSize != sizeof(D3D11DDI_THREADING_CAPS))
                return E_INVALIDARG;
            Caps = D3D11DDICAPS_FREETHREADED;
            if (!Self->ComputeOnly && Self->DeferredContexts)
                Caps |= D3D11DDICAPS_COMMANDLISTS | D3D11DDICAPS_COMMANDLISTS_BUILD_2;
            *Value = Caps;
            return S_OK;

        case D3D11DDICAPS_SHADER:
            if (Args->DataSize != sizeof(D3D11DDI_SHADER_CAPS))
                return E_INVALIDARG;
            Caps = D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X;
            if (Self->Options.DoublePrecisionFloatShaderOps)
                Caps |= D3D11DDICAPS_SHADER_DOUBLES;
            if (Self->Options.PSSpecifiedStencilRefSupported)
                Caps |= D3D11DDICAPS_SHADER_SPECIFIED_STENCIL_REF;
            if (Self->Options.ROVsSupported)
                Caps |= D3D11DDICAPS_SHADER_ROVS;
            if (Self->Options.TypedUAVLoadAdditionalFormats)
                Caps |= D3D11DDICAPS_SHADER_TYPED_UAV_LOAD_ADDITIONAL_FORMATS;
            *Value = Caps;
            return S_OK;

        case D3D11DDICAPS_3DPIPELINESUPPORT:
            if (Args->DataSize != sizeof(D3D11DDI_3DPIPELINESUPPORT_CAPS))
                return E_INVALIDARG;
            return Self->GetPipelineCaps(Value);

        case D3D11_1DDICAPS_D3D11_OPTIONS:
            if (Args->DataSize != sizeof(D3D11_1DDI_D3D11_OPTIONS_DATA))
                return E_INVALIDARG;
            *Value = Self->Options.OutputMergerLogicOp;
            return S_OK;

        case D3D11_1DDICAPS_ARCHITECTURE_INFO:
            if (Args->DataSize != sizeof(D3DDDICAPS_ARCHITECTURE_INFO))
                return E_INVALIDARG;
            *Value = Self->Architecture.TileBasedRenderer;
            return S_OK;

        case D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT:
            if (Args->DataSize != sizeof(D3DDDICAPS_SHADER_MIN_PRECISION_SUPPORT))
                return E_INVALIDARG;
            Precision = 0;
            if (Self->Options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_10_BIT)
                Precision |= D3D11_DDI_SHADER_MIN_PRECISION_10_BIT;
            if (Self->Options.MinPrecisionSupport & D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT)
                Precision |= D3D11_DDI_SHADER_MIN_PRECISION_16_BIT;
            Value[0] = Precision;
            Value[1] = Precision;
            return S_OK;

        case D3DWDDM1_3DDICAPS_D3D11_OPTIONS1:
            if (Args->DataSize != sizeof(D3DWDDM1_3DDI_D3D11_OPTIONS_DATA1))
                return E_INVALIDARG;
            *Value = Self->GetTiledResourceCaps();
            return S_OK;

        case D3DWDDM1_3DDICAPS_MARKER:
        case D3DWDDM2_2DDICAPS_SWIZZLE_PATTERN:
            return E_FAIL;

        case D3DWDDM2_0DDICAPS_D3D11_OPTIONS2:
            if (Args->DataSize != sizeof(D3DWDDM2_0DDI_D3D11_OPTIONS2_DATA))
                return E_INVALIDARG;
            *Value = Self->Options.ConservativeRasterizationTier;
            return S_OK;

        case D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE:
            if (Args->DataSize != sizeof(D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS))
                return E_INVALIDARG;
            Value[0] = Self->Architecture.UMA;
            Value[1] = Self->Architecture.CacheCoherentUMA;
            return S_OK;

        case D3DWDDM2_0DDICAPS_D3D11_OPTIONS3:
            if (Args->DataSize != sizeof(D3DWDDM2_0DDI_D3D11_OPTIONS3_DATA))
                return E_INVALIDARG;
            *Value = TRUE;
            return S_OK;

        case D3DWDDM2_0DDICAPS_GPUVA_CAPS:
            Address = {};
            if (FAILED(Self->Device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Address,
                                                         sizeof(Address))) ||
                Args->DataSize != sizeof(D3DWDDM2_0DDI_GPUVA_CAPS_DATA))
            {
                return E_INVALIDARG;
            }
            *Value = Address.MaxGPUVirtualAddressBitsPerResource;
            return S_OK;

        case D3DWDDM2_2DDICAPS_SHADERCACHE:
            if (Args->DataSize != sizeof(D3DWDDM2_2DDICAPS_SHADERCACHE_DATA))
                return E_INVALIDARG;
            *Value = Self->HighestShaderModel >= D3D_SHADER_MODEL_6_0;
            return S_OK;

        case D3DWDDM2_2DDICAPS_TEXTURE_LAYOUT:
        {
            D3DWDDM2_2DDI_TEXTURE_LAYOUT_CAPS* Layout = static_cast<D3DWDDM2_2DDI_TEXTURE_LAYOUT_CAPS*>(Args->pData);

            if (Args->pInfo != nullptr)
                return E_FAIL;
            if (Args->DataSize != sizeof(*Layout))
                return E_INVALIDARG;
            Layout->DeviceDependentLayoutCount = 0;
            Layout->DeviceDependentSwizzleCount = 0;
            Layout->Supports64KStandardSwizzle = Self->Options.StandardSwizzle64KBSupported;
            Layout->IndexableSwizzlePatterns = FALSE;
            return S_OK;
        }

        default:
            return E_INVALIDARG;
    }
}
