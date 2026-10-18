/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12Device::CheckFeatureSupport
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatsupport.hpp"

#include <d3d11.h>

/* Formats stored as more than one plane */
static
UINT
RxPlaneCount(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
        case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
        case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
        case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
        case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_420_OPAQUE:
        case DXGI_FORMAT_NV11:
        case DXGI_FORMAT_P208:
            return 2;
        case DXGI_FORMAT_V208:
        case DXGI_FORMAT_V408:
            return 3;
        default:
            return 1;
    }
}

static
BOOLEAN
RxIsKnownShaderModel(
    _In_ D3D_SHADER_MODEL Model)
{
    return Model == D3D_SHADER_MODEL_5_1 || (Model >= D3D_SHADER_MODEL_6_0 && Model <= D3D_SHADER_MODEL_6_9);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CheckFeatureSupport(
    D3D12_FEATURE Feature,
    _Inout_updates_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    if (pFeatureSupportData == nullptr)
        return E_INVALIDARG;

    switch (Feature)
    {
        case D3D12_FEATURE_D3D12_OPTIONS:
        {
            D3D12_FEATURE_DATA_D3D12_OPTIONS* Data = static_cast<D3D12_FEATURE_DATA_D3D12_OPTIONS*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            ZeroMemory(Data, sizeof(*Data));
            Data->DoublePrecisionFloatShaderOps = ShaderCaps.DoubleOps;
            Data->OutputMergerLogicOp = Options.OutputMergerLogicOp;
            Data->MinPrecisionSupport = (D3D12_SHADER_MIN_PRECISION_SUPPORT)ShaderCaps.MinPrecision;
            Data->TiledResourcesTier = (D3D12_TILED_RESOURCES_TIER)Options.TiledResourcesTier;
            Data->ResourceBindingTier = (D3D12_RESOURCE_BINDING_TIER)Options.ResourceBindingTier;
            Data->PSSpecifiedStencilRefSupported = ShaderCaps.ShaderSpecifiedStencilRef;
            Data->TypedUAVLoadAdditionalFormats = ShaderCaps.TypedUAVLoadAdditionalFormats;
            Data->ROVsSupported = ShaderCaps.ROVs;
            Data->ConservativeRasterizationTier = (D3D12_CONSERVATIVE_RASTERIZATION_TIER)Options.ConservativeRasterizationTier;
            Data->MaxGPUVirtualAddressBitsPerResource = VaBitsPerResource;
            Data->CrossNodeSharingTier = (D3D12_CROSS_NODE_SHARING_TIER)Options.CrossNodeSharingTier;
            Data->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
                Options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation;
            Data->ResourceHeapTier = (D3D12_RESOURCE_HEAP_TIER)Options.ResourceHeapTier;
            return S_OK;
        }

        case D3D12_FEATURE_ARCHITECTURE:
        case D3D12_FEATURE_ARCHITECTURE1:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE1* Data = static_cast<D3D12_FEATURE_DATA_ARCHITECTURE1*>(pFeatureSupportData);
            UINT Size = Feature == D3D12_FEATURE_ARCHITECTURE ? sizeof(D3D12_FEATURE_DATA_ARCHITECTURE) : sizeof(*Data);

            if (FeatureSupportDataSize != Size || Data->NodeIndex != 0)
                return E_INVALIDARG;

            Data->TileBasedRenderer = TileBased;
            Data->UMA = IsUma();
            Data->CacheCoherentUMA = IsCacheCoherentUma();
            if (Feature == D3D12_FEATURE_ARCHITECTURE1)
                Data->IsolatedMMU = FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS* Data = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);
            D3D_FEATURE_LEVEL Best = (D3D_FEATURE_LEVEL)0;
            UINT Index;

            if (FeatureSupportDataSize != sizeof(*Data) || Data->NumFeatureLevels == 0 ||
                Data->pFeatureLevelsRequested == nullptr)
            {
                return E_INVALIDARG;
            }

            for (Index = 0; Index < Data->NumFeatureLevels; Index++)
            {
                D3D_FEATURE_LEVEL Level = Data->pFeatureLevelsRequested[Index];

                if (Level <= Adapter->GetMaxFeatureLevel() && Level > Best)
                    Best = Level;
            }

            Data->MaxSupportedFeatureLevel = Best;
            return S_OK;
        }

        case D3D12_FEATURE_FORMAT_SUPPORT:
        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT* Data = static_cast<D3D12_FEATURE_DATA_FORMAT_SUPPORT*>(pFeatureSupportData);
            UINT Driver = 0;
            UINT Support1;
            UINT Support2;

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            Data->Support1 = D3D12_FORMAT_SUPPORT1_NONE;
            Data->Support2 = D3D12_FORMAT_SUPPORT2_NONE;

            if (Data->Format != DXGI_FORMAT_UNKNOWN)
            {
                CoreFunctions.pfnCheckFormatSupport(DriverDevice, Data->Format, &Driver);
                if ((Driver & D3D12DDI_FORMAT_SUPPORT_NOT_SUPPORTED) != 0)
                    return E_FAIL;
            }

            if (!RxDeriveFormatSupport(Data->Format, Driver, &Support1))
                return E_FAIL;
            if (!RxDeriveFormatSupport2(Data->Format, Driver, &Support2))
                Support2 = 0;

            /* Mip generation, CPU locking and sharing are Direct3D 11 answers with no Direct3D 12 meaning */
            Support1 &= ~(D3D11_FORMAT_SUPPORT_MIP_AUTOGEN | D3D11_FORMAT_SUPPORT_CPU_LOCKABLE | 0x80000000u);
            Support2 &= ~D3D11_FORMAT_SUPPORT2_SHAREABLE;

            /* Buffers can be tiled whenever the device has tiled resources at all */
            if (Data->Format == DXGI_FORMAT_UNKNOWN && Options.TiledResourcesTier != D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED)
                Support2 |= D3D12_FORMAT_SUPPORT2_TILED;

            Data->Support1 = (D3D12_FORMAT_SUPPORT1)Support1;
            Data->Support2 = (D3D12_FORMAT_SUPPORT2)Support2;
            return S_OK;
        }

        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:
        {
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS* Data =
                static_cast<D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS*>(pFeatureSupportData);
            UINT Levels = 0;

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            CoreFunctions.pfnCheckMultisampleQualityLevels(DriverDevice, Data->Format, Data->SampleCount,
                                                           (D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS)Data->Flags,
                                                           &Levels);
            Data->NumQualityLevels = Levels;
            return S_OK;
        }

        case D3D12_FEATURE_FORMAT_INFO:
        {
            D3D12_FEATURE_DATA_FORMAT_INFO* Data = static_cast<D3D12_FEATURE_DATA_FORMAT_INFO*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            Data->PlaneCount = (UINT8)RxPlaneCount(Data->Format);
            return S_OK;
        }

        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
        {
            D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT* Data =
                static_cast<D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            Data->MaxGPUVirtualAddressBitsPerResource = VaBitsPerResource;
            Data->MaxGPUVirtualAddressBitsPerProcess = VaBitsPerProcess;
            return S_OK;
        }

        case D3D12_FEATURE_SHADER_MODEL:
        {
            D3D12_FEATURE_DATA_SHADER_MODEL* Data = static_cast<D3D12_FEATURE_DATA_SHADER_MODEL*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data) || !RxIsKnownShaderModel(Data->HighestShaderModel))
                return E_INVALIDARG;

            if (Data->HighestShaderModel > HighestShaderModel)
                Data->HighestShaderModel = HighestShaderModel;
            return S_OK;
        }

        case D3D12_FEATURE_ROOT_SIGNATURE:
        {
            D3D12_FEATURE_DATA_ROOT_SIGNATURE* Data = static_cast<D3D12_FEATURE_DATA_ROOT_SIGNATURE*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data) || Data->HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_0)
                return E_INVALIDARG;

            if (Data->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_2)
                Data->HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_2;
            return S_OK;
        }

        default:
            RX_TRACE(L"CheckFeatureSupport feature %d not answered", Feature);
            return E_INVALIDARG;
    }
}
