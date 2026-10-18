/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CheckFeatureSupport
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Copies one answer out when the caller's buffer is exactly its size */
template <typename Data>
static
HRESULT
RxAnswer(
    _Out_writes_bytes_(Size) VOID* Output,
    _In_ UINT Size,
    _In_ const Data& Answer)
{
    if (Size != sizeof(Answer))
        return E_INVALIDARG;

    memcpy(Output, &Answer, sizeof(Answer));
    return S_OK;
}

/* Tiled resource tier from the driver's support flags */
static
D3D11_TILED_RESOURCES_TIER
RxTiledTier(
    _In_ const UmdAdapter& Adapter)
{
    D3DWDDM1_3DDI_D3D11_OPTIONS_DATA1 Options = {};

    if (FAILED(Adapter.GetCaps(D3DWDDM1_3DDICAPS_D3D11_OPTIONS1, &Options, sizeof(Options))))
        return D3D11_TILED_RESOURCES_NOT_SUPPORTED;

    if (Options.TiledResourcesSupportFlags & D3DWDDM2_0DDI_TILED_RESOURCES_TIER_3_SUPPORTED)
        return D3D11_TILED_RESOURCES_TIER_3;
    if (Options.TiledResourcesSupportFlags & D3DWDDM1_3DDI_TILED_RESOURCES_TIER_2_SUPPORTED)
        return D3D11_TILED_RESOURCES_TIER_2;
    if (Options.TiledResourcesSupportFlags & D3DWDDM1_3DDI_TILED_RESOURCES_TIER_1_SUPPORTED)
        return D3D11_TILED_RESOURCES_TIER_1;

    return D3D11_TILED_RESOURCES_NOT_SUPPORTED;
}

/* How far cross API sharing goes, which grows with the WDDM version of the driver */
static
D3D11_SHARED_RESOURCE_TIER
RxSharedResourceTier(
    _In_ const UmdAdapter& Adapter)
{
    D3DKMT_DRIVERVERSION Version = Adapter.GetWddmVersion();

    if (Version >= KMT_DRIVERVERSION_WDDM_2_5)
        return D3D11_SHARED_RESOURCE_TIER_3;
    if (Version >= KMT_DRIVERVERSION_WDDM_2_4)
        return D3D11_SHARED_RESOURCE_TIER_2;
    if (Version >= KMT_DRIVERVERSION_WDDM_2_1)
        return D3D11_SHARED_RESOURCE_TIER_1;

    return D3D11_SHARED_RESOURCE_TIER_0;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CheckFeatureSupport(
    D3D11_FEATURE Feature,
    _Out_writes_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    const UmdAdapter& Umd = *Adapter;
    D3D_FEATURE_LEVEL Level = GetFeatureLevel();
    D3D11DDI_SHADER_CAPS Shader = {};
    UINT Size = FeatureSupportDataSize;
    VOID* Data = pFeatureSupportData;

    Umd.GetCaps(D3D11DDICAPS_SHADER, &Shader, sizeof(Shader));

    switch (Feature)
    {
        case D3D11_FEATURE_THREADING:
        {
            D3D11DDI_THREADING_CAPS Threading = {};
            D3D11_FEATURE_DATA_THREADING Answer = {};

            Umd.GetCaps(D3D11DDICAPS_THREADING, &Threading, sizeof(Threading));
            Answer.DriverConcurrentCreates = (Threading.Caps & D3D11DDICAPS_FREETHREADED) != 0;

            /* Deferred contexts are always built by the runtime, so drivers never see them */
            Answer.DriverCommandLists = FALSE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_DOUBLES:
        {
            D3D11_FEATURE_DATA_DOUBLES Answer = {};

            Answer.DoublePrecisionFloatShaderOps = (Shader.Caps & D3D11DDICAPS_SHADER_DOUBLES) != 0;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_FORMAT_SUPPORT:
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT* Query = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT*>(Data);

            if (Size != sizeof(*Query))
                return E_INVALIDARG;
            return GetFormatSupport(Query->InFormat, &Query->OutFormatSupport);
        }

        case D3D11_FEATURE_FORMAT_SUPPORT2:
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2* Query = static_cast<D3D11_FEATURE_DATA_FORMAT_SUPPORT2*>(Data);

            if (Size != sizeof(*Query))
                return E_INVALIDARG;
            return GetFormatSupport2(Query->InFormat, &Query->OutFormatSupport2);
        }

        case D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS:
        {
            D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS Answer = {};

            Answer.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x =
                Level >= D3D_FEATURE_LEVEL_11_0 ||
                (Shader.Caps & D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X) != 0;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D11_OPTIONS:
        {
            D3D11_1DDI_D3D11_OPTIONS_DATA Options = {};
            D3D11_FEATURE_DATA_D3D11_OPTIONS Answer = {};

            Umd.GetCaps(D3D11_1DDICAPS_D3D11_OPTIONS, &Options, sizeof(Options));
            Answer.OutputMergerLogicOp = Options.OutputMergerLogicOp != FALSE;
            Answer.UAVOnlyRenderingForcedSampleCount = Level >= D3D_FEATURE_LEVEL_11_0;
            Answer.DiscardAPIsSeenByDriver = TRUE;
            Answer.FlagsForUpdateAndCopySeenByDriver = TRUE;
            Answer.ClearView = TRUE;
            Answer.CopyWithOverlap = TRUE;
            Answer.ConstantBufferPartialUpdate = TRUE;
            Answer.ConstantBufferOffsetting = TRUE;
            Answer.MapNoOverwriteOnDynamicConstantBuffer = TRUE;
            Answer.MapNoOverwriteOnDynamicBufferSRV = Level >= D3D_FEATURE_LEVEL_10_0;
            Answer.MultisampleRTVWithForcedSampleCountOne = Level >= D3D_FEATURE_LEVEL_11_0;
            Answer.SAD4ShaderInstructions = Level >= D3D_FEATURE_LEVEL_11_0;
            Answer.ExtendedDoublesShaderInstructions = (Shader.Caps & D3D11DDICAPS_SHADER_DOUBLES) != 0;
            Answer.ExtendedResourceSharing = Level >= D3D_FEATURE_LEVEL_10_0;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_ARCHITECTURE_INFO:
        {
            D3D11_1DDI_ARCHITECTURE_INFO_DATA Info = {};
            D3D11_FEATURE_DATA_ARCHITECTURE_INFO Answer = {};

            Umd.GetCaps(D3D11_1DDICAPS_ARCHITECTURE_INFO, &Info, sizeof(Info));
            Answer.TileBasedDeferredRenderer = Info.TileBasedDeferredRenderer != FALSE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D9_OPTIONS:
        {
            D3D11_FEATURE_DATA_D3D9_OPTIONS Answer = {};

            Answer.FullNonPow2TextureSupport = TRUE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT:
        {
            D3D11_DDI_SHADER_MIN_PRECISION_SUPPORT_DATA Precision = {};
            D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT Answer = {};

            Umd.GetCaps(D3D11_1DDICAPS_SHADER_MIN_PRECISION_SUPPORT, &Precision, sizeof(Precision));
            Answer.PixelShaderMinPrecision = Precision.PixelShaderMinPrecision;
            Answer.AllOtherShaderStagesMinPrecision = Precision.AllOtherStagesMinPrecision;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D9_SHADOW_SUPPORT:
        {
            D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT Answer = {};

            Answer.SupportsDepthAsTextureWithLessEqualComparisonFilter = TRUE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D11_OPTIONS1:
        {
            D3D11_FEATURE_DATA_D3D11_OPTIONS1 Answer = {};
            D3D11_TILED_RESOURCES_TIER Tier = RxTiledTier(Umd);

            /* Tiled resources are not implemented yet, whatever the driver could do */
            Answer.TiledResourcesTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;
            Answer.MinMaxFiltering = Tier >= D3D11_TILED_RESOURCES_TIER_2;
            Answer.ClearViewAlsoSupportsDepthOnlyFormats = TRUE;
            Answer.MapOnDefaultBuffers = TRUE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT:
        {
            D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT Answer = {};

            Answer.SimpleInstancingSupported = TRUE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_MARKER_SUPPORT:
        {
            UINT Marker = 0;
            D3D11_FEATURE_DATA_MARKER_SUPPORT Answer = {};

            Umd.GetCaps(D3DWDDM1_3DDICAPS_MARKER, &Marker, sizeof(Marker));
            Answer.Profile = Marker != D3DWDDM1_3DDI_MARKER_TYPE_NONE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D9_OPTIONS1:
        {
            D3D11_FEATURE_DATA_D3D9_OPTIONS1 Answer = {};

            Answer.FullNonPow2TextureSupported = TRUE;
            Answer.DepthAsTextureWithLessEqualComparisonFilterSupported = TRUE;
            Answer.SimpleInstancingSupported = TRUE;
            Answer.TextureCubeFaceRenderTargetWithNonCubeDepthStencilSupported = TRUE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D11_OPTIONS2:
        {
            D3DWDDM2_0DDI_D3D11_OPTIONS2_DATA Options = {};
            D3DWDDM2_0DDI_MEMORY_ARCHITECTURE_CAPS Memory = {};
            D3D11_FEATURE_DATA_D3D11_OPTIONS2 Answer = {};

            /* An older, one field longer layout is still accepted, with the extra field zeroed */
            if (Size != sizeof(Answer) && Size != sizeof(Answer) + sizeof(UINT))
                return E_INVALIDARG;

            Umd.GetCaps(D3DWDDM2_0DDICAPS_D3D11_OPTIONS2, &Options, sizeof(Options));
            Umd.GetCaps(D3DWDDM2_0DDICAPS_MEMORY_ARCHITECTURE, &Memory, sizeof(Memory));
            Answer.PSSpecifiedStencilRefSupported = (Shader.Caps & D3D11DDICAPS_SHADER_SPECIFIED_STENCIL_REF) != 0;
            Answer.TypedUAVLoadAdditionalFormats = (Shader.Caps & D3D11DDICAPS_SHADER_TYPED_UAV_LOAD_ADDITIONAL_FORMATS) != 0;
            Answer.ROVsSupported = (Shader.Caps & D3D11DDICAPS_SHADER_ROVS) != 0;
            Answer.ConservativeRasterizationTier = (D3D11_CONSERVATIVE_RASTERIZATION_TIER)Options.ConservativeRasterizationTier;
            Answer.TiledResourcesTier = D3D11_TILED_RESOURCES_NOT_SUPPORTED;

            /* Mapping default textures and standard swizzle need a WDDM 2 device interface */
            Answer.MapOnDefaultTextures = FALSE;
            Answer.StandardSwizzle = FALSE;
            Answer.UnifiedMemoryArchitecture = Memory.UMA != FALSE;

            ZeroMemory(Data, Size);
            memcpy(Data, &Answer, sizeof(Answer));
            return S_OK;
        }

        case D3D11_FEATURE_D3D11_OPTIONS3:
        {
            D3DWDDM2_0DDI_D3D11_OPTIONS3_DATA Options = {};
            D3D11_FEATURE_DATA_D3D11_OPTIONS3 Answer = {};

            Umd.GetCaps(D3DWDDM2_0DDICAPS_D3D11_OPTIONS3, &Options, sizeof(Options));
            Answer.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer = Options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizer;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
        {
            D3DWDDM2_0DDI_GPUVA_CAPS_DATA Va = {};
            D3DKMT_QUERY_GPUMMU_CAPS Mmu = {};
            D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Answer = {};

            Umd.GetCaps(D3DWDDM2_0DDICAPS_GPUVA_CAPS, &Va, sizeof(Va));
            Answer.MaxGPUVirtualAddressBitsPerResource = Va.MaxGPUVirtualAddressBitsPerResource;
            if (SUCCEEDED(Umd.QueryKernel(KMTQAITYPE_QUERY_GPUMMU_CAPS, &Mmu, sizeof(Mmu))))
                Answer.MaxGPUVirtualAddressBitsPerProcess = Mmu.Caps.VirtualAddressBitCount;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D11_OPTIONS4:
        {
            D3D11_FEATURE_DATA_D3D11_OPTIONS4 Answer = {};

            Answer.ExtendedNV12SharedTextureSupported = Umd.GetWddmVersion() >= KMT_DRIVERVERSION_WDDM_2_1;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_SHADER_CACHE:
        {
            D3D11_FEATURE_DATA_SHADER_CACHE Answer = {};

            /* This runtime keeps no shader cache of its own */
            Answer.SupportFlags = D3D11_SHADER_CACHE_SUPPORT_NONE;
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_D3D11_OPTIONS5:
        {
            D3D11_FEATURE_DATA_D3D11_OPTIONS5 Answer = {};

            Answer.SharedResourceTier = RxSharedResourceTier(Umd);
            return RxAnswer(Data, Size, Answer);
        }

        case D3D11_FEATURE_DISPLAYABLE:
        {
            D3D11_FEATURE_DATA_DISPLAYABLE Answer = {};

            Answer.DisplayableTexture = Umd.GetWddmVersion() >= KMT_DRIVERVERSION_WDDM_2_9;
            Answer.SharedResourceTier = RxSharedResourceTier(Umd);
            return RxAnswer(Data, Size, Answer);
        }

        default:
            return E_INVALIDARG;
    }
}
