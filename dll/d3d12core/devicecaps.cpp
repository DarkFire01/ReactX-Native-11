/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the device learns from the driver while it is made, in the order Windows asks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatsupport12.hpp"
#include "session.hpp"
#include "shadercache.hpp"

#include <dxcore_interface.h>

/* Feature numbers the runtime asks the driver to enable */
constexpr UINT RxFeatureVideo12 = D3D12DDI_FEATURE_0020_VIDEO;
constexpr UINT RxFeatureSessions12 = D3D12DDI_FEATURE_0030_CONTENT_PROTECTION_RESOURCES;

/* The newest versions this runtime knows of the features it drives */
constexpr UINT RxNewestVideoVersion12 = D3D12DDI_FEATURE_VERSION_VIDEO_0113_0;
constexpr UINT RxNewestSessionVersion12 = D3D12DDI_FEATURE_VERSION_CONTENT_PROTECTION_RESOURCES_0074_0;

/* A shader model as the DDI lists it: major in the high word, minor and a release nibble below */
static
UINT
RxDdiModel(
    _In_ UINT Major,
    _In_ UINT Minor,
    _In_ BOOLEAN Released)
{
    return (Major << 16) | (Minor << 4) | (Released ? 5 : 0);
}

/* Released shader models from 5.1 up, the order gaps are judged in */
static
INT
RxReleasedModelSlot(
    _In_ UINT Model)
{
    if (Model == RxDdiModel(5, 1, TRUE))
        return 0;
    if ((Model >> 16) == 6 && (Model & 0xF) == 5 && ((Model >> 4) & 0xFFF) <= 8)
        return 1 + (INT)((Model >> 4) & 0xFFF);
    return -1;
}

HRESULT
RxDevice12::QueryShaderModels()
{
    D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 Query = {};
    D3D12DDI_SHADER_MODEL* Models = nullptr;
    UINT Count = 0;
    UINT Pass;
    UINT Index;
    UINT Best = 0;
    UINT Present = 0;
    BOOLEAN Has51 = FALSE;
    BOOLEAN Seen = FALSE;
    BOOLEAN Gap = FALSE;
    HRESULT hr = S_OK;

    /* The first pass counts, the second reads the list */
    for (Pass = 0; Pass < 2; Pass++)
    {
        if (Pass == 1)
        {
            Models = new (std::nothrow) D3D12DDI_SHADER_MODEL[Count];
            if (Models == nullptr)
                return E_OUTOFMEMORY;
        }

        Query.pNumShaderModelsSupported = &Count;
        Query.pShaderModelsSupported = Models;
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0011_SHADER_MODELS, nullptr, &Query, sizeof(Query));
        if (FAILED(hr) || Count == 0)
        {
            RX_TRACE(L"driver lists no shader models (0x%08lX)", hr);
            delete[] Models;
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
    }

    for (Index = 0; Index < Count; Index++)
    {
        UINT Model = (UINT)Models[Index];
        INT Slot = RxReleasedModelSlot(Model);

        /* Preview encodings of 6.0 to 6.9 let the shader model rules clear caps instead of failing */
        if ((Model >> 16) == 6 && (Model & 0xF) == 0 && ((Model >> 4) & 0xFFF) <= 9)
            PreviewShaderModels |= 1u << ((Model >> 4) & 0xF);

        if (Model == RxDdiModel(5, 1, TRUE))
            Has51 = TRUE;
        if (Slot < 0)
            continue;

        Present |= 1u << Slot;
        if (Model > Best)
            Best = Model;
    }
    delete[] Models;

    if (!Has51 || Best == 0)
    {
        RX_TRACE(L"driver shader model list lacks 5.1 or any release");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    /* Released models must form one run */
    for (Index = 0; Index < 10; Index++)
    {
        if ((Present & (1u << Index)) != 0)
        {
            if (Gap)
            {
                RX_TRACE(L"driver shader model list has gaps");
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            }
            Seen = TRUE;
        }
        else if (Seen)
        {
            Gap = TRUE;
        }
    }

    HighestShaderModel = (D3D_SHADER_MODEL)(((Best >> 16) << 4) | ((Best >> 4) & 0xF));
    return S_OK;
}

/* Tiers past what the API names come down to the highest the API has */
VOID
RxDevice12::ClampOptions()
{
    if (Options.TiledResourcesTier >= D3D12DDI_TILED_RESOURCES_TIER_3)
        Options.SRVOnlyTiledResourceTier3 = TRUE;

    if ((INT)Options.ResourceHeapTier < 1)
        RX_TRACE(L"driver resource heap tier %d is invalid", Options.ResourceHeapTier);
    else if (Options.ResourceHeapTier >= 3)
        Options.ResourceHeapTier = D3D12DDI_RESOURCE_HEAP_TIER_2;

    if ((INT)Options.ProgrammableSamplePositionsTier >= 3)
        Options.ProgrammableSamplePositionsTier = (D3D12DDI_PROGRAMMABLE_SAMPLE_POSITIONS_TIER)2;
    if ((INT)Options.ViewInstancingTier >= 4)
        Options.ViewInstancingTier = (D3D12DDI_VIEW_INSTANCING_TIER)3;
    if ((INT)Options.TiledResourcesTier >= 5)
        Options.TiledResourcesTier = (D3D12DDI_TILED_RESOURCES_TIER)4;

    if ((INT)Options.ResourceBindingTier < 1)
        RX_TRACE(L"driver resource binding tier %d is invalid", Options.ResourceBindingTier);
    else if (Options.ResourceBindingTier >= 4)
        Options.ResourceBindingTier = D3D12DDI_RESOURCE_BINDING_TIER_3;

    if ((INT)Options.ConservativeRasterizationTier >= 4)
        Options.ConservativeRasterizationTier = (D3D12DDI_CONSERVATIVE_RASTERIZATION_TIER)3;
    if ((INT)Options.CrossNodeSharingTier >= 5)
        Options.CrossNodeSharingTier = (D3D12DDI_CROSS_NODE_SHARING_TIER)4;
    if ((INT)Options.RaytracingTier > 12)
        Options.RaytracingTier = (D3D12DDI_RAYTRACING_TIER)12;
    if ((INT)Options.RenderPassTier >= 3)
        Options.RenderPassTier = (D3D12DDI_RENDER_PASS_TIER)2;
    if ((INT)Options.MeshShaderTier >= 11)
        Options.MeshShaderTier = (D3D12DDI_MESH_SHADER_TIER)10;
    if ((INT)Options.SamplerFeedbackTier >= 101)
        Options.SamplerFeedbackTier = (D3D12DDI_SAMPLER_FEEDBACK_TIER)100;
    if ((INT)Options.VariableShadingRateTier >= 3)
        Options.VariableShadingRateTier = (D3D12DDI_VARIABLE_SHADING_RATE_TIER)2;
}

/* Shader caps, shader models, the options and the optional caps that follow them */
HRESULT
RxDevice12::QueryDeviceCaps()
{
    HRESULT hr;

    if (Level != D3D_FEATURE_LEVEL_1_0_GENERIC)
    {
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_SHADER, nullptr, &ShaderCaps, sizeof(ShaderCaps));
        if (FAILED(hr))
        {
            RX_TRACE(L"driver shader caps query failed 0x%08lX", hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
        if (ShaderCaps.WaveLaneCountMin < 4 || ShaderCaps.WaveLaneCountMin > ShaderCaps.WaveLaneCountMax ||
            ShaderCaps.TotalLaneCount < ShaderCaps.WaveLaneCountMax)
        {
            RX_TRACE(L"driver wave lane counts are invalid");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
        if ((INT)ShaderCaps.WaveMMATier >= 11)
            ShaderCaps.WaveMMATier = (D3D12DDI_WAVE_MMA_TIER)10;

        hr = QueryShaderModels();
        if (FAILED(hr))
            return hr;
    }
    else
    {
        HighestShaderModel = (D3D_SHADER_MODEL)0;
    }

    hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_D3D12_OPTIONS, nullptr, &Options, sizeof(Options));
    if (FAILED(hr))
    {
        RX_TRACE(L"driver options query failed 0x%08lX", hr);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }
    if ((Options.WriteBufferImmediateQueueFlags & ~0x77) != 0)
    {
        RX_TRACE(L"driver write buffer immediate flags 0x%X are invalid", Options.WriteBufferImmediateQueueFlags);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }
    ClampOptions();

    /* Each optional cap reads as zero when the driver does not answer */
    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0090, nullptr, &Casting, sizeof(Casting))))
        ZeroMemory(&Casting, sizeof(Casting));
    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0091, nullptr, &CopyAndViewport, sizeof(CopyAndViewport))))
        ZeroMemory(&CopyAndViewport, sizeof(CopyAndViewport));
    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0093, nullptr, &TextureOps, sizeof(TextureOps))))
        ZeroMemory(&TextureOps, sizeof(TextureOps));
    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0098, nullptr, &L1Access, sizeof(L1Access))))
        ZeroMemory(&L1Access, sizeof(L1Access));

    /* Custom heaps on compute only devices are only asked about by such devices */
    if ((Level == D3D_FEATURE_LEVEL_1_0_CORE || Level == D3D_FEATURE_LEVEL_1_0_GENERIC) &&
        FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0101, nullptr, &ComputeHeap, sizeof(ComputeHeap))))
    {
        ZeroMemory(&ComputeHeap, sizeof(ComputeHeap));
    }

    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS1_0103, nullptr, &WorkGraphs, sizeof(WorkGraphs))))
        ZeroMemory(&WorkGraphs, sizeof(WorkGraphs));
    if ((INT)WorkGraphs.WorkGraphsTier < 0)
        RX_TRACE(L"driver work graphs tier %d is invalid", WorkGraphs.WorkGraphsTier);
    else if (WorkGraphs.WorkGraphsTier > D3D12DDI_WORK_GRAPHS_TIER_1_0)
        WorkGraphs.WorkGraphsTier = D3D12DDI_WORK_GRAPHS_TIER_1_0;

    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0109, nullptr, &RecreateAt, sizeof(RecreateAt))))
        ZeroMemory(&RecreateAt, sizeof(RecreateAt));

    /* Indirect execution defaults to tier 1.0 */
    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0110, nullptr, &ExecuteIndirect, sizeof(ExecuteIndirect))))
        ExecuteIndirect.ExecuteIndirectTier = (D3D12DDI_EXECUTE_INDIRECT_TIER)10;
    if ((INT)ExecuteIndirect.ExecuteIndirectTier < 10)
        RX_TRACE(L"driver execute indirect tier %d is invalid", ExecuteIndirect.ExecuteIndirectTier);
    else if ((INT)ExecuteIndirect.ExecuteIndirectTier >= 12)
        ExecuteIndirect.ExecuteIndirectTier = (D3D12DDI_EXECUTE_INDIRECT_TIER)11;

    if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0118, nullptr, &Reordering, sizeof(Reordering))))
        ZeroMemory(&Reordering, sizeof(Reordering));
    if (Reordering.ShaderExecutionReorderingActuallyReorders && HighestShaderModel < D3D_SHADER_MODEL_6_9)
    {
        RX_TRACE(L"driver reorders shaders without shader model 6.9");
        Reordering.ShaderExecutionReorderingActuallyReorders = FALSE;
    }

    return S_OK;
}

/* Fills one table, failing creation when the driver will not */
static
HRESULT
RxFillTable(
    _In_ const RxAdapter12& Adapter,
    _In_ D3D12DDI_TABLE_TYPE Type,
    _Out_writes_bytes_(Size) VOID* Table,
    _In_ SIZE_T Size,
    _In_ UINT Index,
    _In_opt_ VOID* Token)
{
    D3D12DDI_HRTTABLE RuntimeTable = {};
    HRESULT hr;

    RuntimeTable.handle = Token;
    hr = Adapter.GetFunctions().pfnFillDDITable(Adapter.GetDriverHandle(), Type, Table, Size, Index, RuntimeTable);
    if (FAILED(hr))
        RX_TRACE(L"driver did not fill table %u (0x%08lX)", Type, hr);
    return hr;
}

/* The highest version in a list this runtime can drive, 0 when there is none */
static
UINT
RxPickVersion(
    _In_reads_(Count) const UINT32* Versions,
    _In_ UINT Count,
    _In_ UINT Newest)
{
    UINT Best = 0;

    for (UINT Index = 0; Index < Count; Index++)
    {
        if (Versions[Index] <= Newest && Versions[Index] > Best)
            Best = Versions[Index];
    }
    return Best;
}

/* The driver's offered versions of one feature */
static
HRESULT
RxFeatureVersions(
    _In_ const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096& Functions,
    _In_ D3D12DDI_HDEVICE Device,
    _In_ UINT Feature,
    _Outptr_result_buffer_(*Count) UINT32** Versions,
    _Out_ UINT* Count)
{
    UINT32 Asked = 0;
    UINT32 Given;
    HRESULT hr;

    *Versions = nullptr;
    *Count = 0;

    hr = Functions.pfnGetSupportedExtendedFeatureVersions(Device, (D3D12DDI_FEATURE_0020)Feature, &Asked, nullptr);
    if (FAILED(hr))
        return hr;

    *Versions = new (std::nothrow) UINT32[Asked ? Asked : 1];
    if (*Versions == nullptr)
        return E_OUTOFMEMORY;

    Given = Asked;
    hr = Functions.pfnGetSupportedExtendedFeatureVersions(Device, (D3D12DDI_FEATURE_0020)Feature, &Given, *Versions);
    if (SUCCEEDED(hr) && Given != Asked)
        hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if (FAILED(hr))
    {
        delete[] *Versions;
        *Versions = nullptr;
        return hr;
    }

    *Count = Given;
    return S_OK;
}

/* The runtime side of protected sessions the driver may call back into */
static
HRESULT
APIENTRY
RxCreateProtectedSessionCb(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTPROTECTEDSESSION_0030 hRTProtectedSession,
    _In_ D3D12DDICB_CREATE_PROTECTED_SESSION_0030* pArgs)
{
    /* Protected resource sessions are the only sessions this runtime makes */
    return RxCreateKernelSession12(*static_cast<RxKernelSession12*>(hRTProtectedSession.handle), *pArgs);
}

static const D3D12DDI_CONTENT_PROTECTION_CALLBACKS_0030 RxSessionCallbacks12 =
{
    RxCreateProtectedSessionCb,
};

/* Asks which extended features the driver has, then turns on the ones the runtime uses */
HRESULT
RxDevice12::EnableExtendedFeatures()
{
    D3D12DDI_FEATURE_0020* Features;
    UINT32 Count = 0;
    UINT32 Given;
    HRESULT hr;

    hr = ExtendedFunctions.pfnGetSupportedExtendedFeatures(DriverDevice, D3D12DDI_FEATURE_0076_PIN_RESOURCES, &Count,
                                                           nullptr);
    if (FAILED(hr))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if (Count == 0)
        return S_OK;

    Features = new (std::nothrow) D3D12DDI_FEATURE_0020[Count];
    if (Features == nullptr)
        return E_OUTOFMEMORY;

    Given = Count;
    hr = ExtendedFunctions.pfnGetSupportedExtendedFeatures(DriverDevice, D3D12DDI_FEATURE_0076_PIN_RESOURCES, &Given,
                                                           Features);
    if (SUCCEEDED(hr) && Given != Count)
        hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;

    for (UINT Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
    {
        UINT Feature = (UINT)Features[Index];
        UINT32* Versions;
        UINT VersionCount;
        UINT Chosen;

        /* Drivers compile state objects through the runtime's shader cache; a driver that refuses it still works */
        if (Feature == D3D12DDI_FEATURE_0021_SHADERCACHING)
        {
            if (FAILED(RxEnableShaderCache12(ExtendedFunctions, DriverDevice)))
                RX_TRACE(L"driver shader cache could not be turned on");
            continue;
        }

        /* Video is turned on the first time a video object needs it; others the runtime does not drive yet */
        if (Feature != RxFeatureVideo12 && Feature != RxFeatureSessions12)
        {
            RX_TRACE(L"driver extended feature %u is not used", Feature);
            continue;
        }

        hr = RxFeatureVersions(ExtendedFunctions, DriverDevice, Feature, &Versions, &VersionCount);
        if (FAILED(hr))
            break;

        Chosen = RxPickVersion(Versions, VersionCount,
                               Feature == RxFeatureVideo12 ? RxNewestVideoVersion12 : RxNewestSessionVersion12);
        delete[] Versions;
        if (Chosen == 0)
            continue;

        if (Feature == RxFeatureVideo12)
        {
            VideoVersion = Chosen;
            continue;
        }

        hr = ExtendedFunctions.pfnEnableExtendedFeature(DriverDevice, (D3D12DDI_FEATURE_0020)Feature, Chosen);
        if (SUCCEEDED(hr))
        {
            hr = ExtendedFunctions.pfnSetExtendedFeatureCallbacks(DriverDevice,
                                                                  D3D12DDI_TABLE_TYPE_0030_CONTENT_PROTECTION_CALLBACKS,
                                                                  &RxSessionCallbacks12, sizeof(RxSessionCallbacks12));
        }
        if (SUCCEEDED(hr))
            SessionVersion = Chosen;
    }

    delete[] Features;
    return FAILED(hr) ? DXGI_ERROR_DRIVER_INTERNAL_ERROR : S_OK;
}

HRESULT
RxDevice12::FillTables()
{
    const D3D12DDI_ADAPTERFUNCS_0109& Functions = Adapter->GetFunctions();
    UINT32 OptionalCount = 0;
    HRESULT hr;

    /* Optional tables the driver asks for; none are used by this runtime */
    hr = Functions.pfnGetOptionalDDITables(Adapter->GetDriverHandle(), &OptionalCount, nullptr);
    if (FAILED(hr))
        return hr;
    if (OptionalCount != 0)
    {
        D3D12DDI_TABLE_REQUEST* Requests = new (std::nothrow) D3D12DDI_TABLE_REQUEST[OptionalCount];

        if (Requests == nullptr)
            return E_OUTOFMEMORY;
        hr = Functions.pfnGetOptionalDDITables(Adapter->GetDriverHandle(), &OptionalCount, Requests);
        delete[] Requests;
        if (FAILED(hr))
            return hr;
    }

    hr = RxFillTable(*Adapter, D3D12DDI_TABLE_TYPE_DEVICE_CORE, &CoreFunctions, sizeof(CoreFunctions), 0, nullptr);
    if (FAILED(hr))
        return hr;

    /* Each list table comes back through SetCommandListDDITableCb as its own token */
    for (UINT Index = 0; Index < _countof(ListFunctions); Index++)
    {
        ListTokens[Index].Table = &ListFunctions[Index];
        hr = RxFillTable(*Adapter, D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D, &ListFunctions[Index],
                         sizeof(ListFunctions[Index]), Index, &ListTokens[Index]);
        if (FAILED(hr))
            return hr;
    }

    hr = RxFillTable(*Adapter, D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D, &QueueFunctions, sizeof(QueueFunctions), 0,
                     nullptr);
    if (FAILED(hr))
        return hr;

    if (ListFunctions[0].pfnCopyBufferRegion == nullptr || ListFunctions[0].pfnCopyTiles == nullptr ||
        QueueFunctions.pfnExecuteCommandLists == nullptr)
    {
        RX_TRACE(L"driver tables are incomplete");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    hr = RxFillTable(*Adapter, D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES, &ExtendedFunctions,
                     sizeof(ExtendedFunctions), 0, nullptr);
    if (FAILED(hr))
        return hr;

    hr = EnableExtendedFeatures();
    if (FAILED(hr))
        return hr;

    if (SessionVersion != 0)
    {
        hr = RxFillTable(*Adapter, D3D12DDI_TABLE_TYPE_0030_DEVICE_CONTENT_PROTECTION_RESOURCES, &SessionFunctions,
                         sizeof(SessionFunctions), 0, nullptr);
        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

/* Caps the shader model makes meaningless are cleared, or creation fails when the driver claims them */
HRESULT
RxDevice12::ApplyShaderModelRules()
{
    if (Level == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return S_OK;

    if (HighestShaderModel < D3D_SHADER_MODEL_6_6)
    {
        ShaderCaps.AtomicInt64OnDescriptorHeapResource = FALSE;
        if (ShaderCaps.AtomicInt64OnTypedResource || ShaderCaps.AtomicInt64OnGroupShared ||
            ShaderCaps.DerivativesInMeshAndAmplificationShaders || ShaderCaps.WaveMMATier != 0)
        {
            if ((PreviewShaderModels & 0x40) == 0)
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            ShaderCaps.AtomicInt64OnTypedResource = FALSE;
            ShaderCaps.AtomicInt64OnGroupShared = FALSE;
            ShaderCaps.DerivativesInMeshAndAmplificationShaders = FALSE;
            ShaderCaps.WaveMMATier = (D3D12DDI_WAVE_MMA_TIER)0;
        }
    }

    if (HighestShaderModel >= D3D_SHADER_MODEL_6_8)
    {
        if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_SHADER_MODEL_6_8_OPTIONS_0110, nullptr, &Model68,
                                    sizeof(Model68))))
        {
            RX_TRACE(L"driver shader model 6.8 options query failed");
        }
    }
    else
    {
        WorkGraphs.WorkGraphsTier = D3D12DDI_WORK_GRAPHS_TIER_NOT_SUPPORTED;
    }

    /* Matrix wave ops are never offered at the API */
    ShaderCaps.WaveMMATier = (D3D12DDI_WAVE_MMA_TIER)0;

    if (HighestShaderModel < D3D_SHADER_MODEL_6_7 && (TextureOps.AdvancedTextureOpsSupported ||
                                                      TextureOps.WriteableMSAATexturesSupported))
    {
        if ((PreviewShaderModels & 0x80) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        TextureOps.AdvancedTextureOpsSupported = FALSE;
        TextureOps.WriteableMSAATexturesSupported = FALSE;
    }

    if (HighestShaderModel < D3D_SHADER_MODEL_6_5 && ((INT)Options.RaytracingTier > 10 ||
                                                      (INT)Options.SamplerFeedbackTier > 0 ||
                                                      (INT)Options.MeshShaderTier > 0))
    {
        if ((PreviewShaderModels & 0x20) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        if ((INT)Options.RaytracingTier > 10)
            Options.RaytracingTier = (D3D12DDI_RAYTRACING_TIER)10;
        Options.SamplerFeedbackTier = (D3D12DDI_SAMPLER_FEEDBACK_TIER)0;
        Options.MeshShaderTier = (D3D12DDI_MESH_SHADER_TIER)0;
    }

    if (HighestShaderModel < D3D_SHADER_MODEL_6_4 && (INT)Options.VariableShadingRateTier > 1)
    {
        if ((PreviewShaderModels & 0x10) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Options.VariableShadingRateTier = (D3D12DDI_VARIABLE_SHADING_RATE_TIER)1;
    }

    if (HighestShaderModel < D3D_SHADER_MODEL_6_3 && Options.RaytracingTier != 0)
    {
        if ((PreviewShaderModels & 0x8) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Options.RaytracingTier = (D3D12DDI_RAYTRACING_TIER)0;
    }

    if (HighestShaderModel < D3D_SHADER_MODEL_6_2 && ShaderCaps.Native16BitOps)
    {
        if ((PreviewShaderModels & 0x4) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        ShaderCaps.Native16BitOps = FALSE;
    }

    if (HighestShaderModel < D3D_SHADER_MODEL_6_1 && ((INT)Options.ViewInstancingTier >= 1 ||
                                                      Options.BarycentricsSupported))
    {
        if ((PreviewShaderModels & 0x2) == 0)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Options.ViewInstancingTier = (D3D12DDI_VIEW_INSTANCING_TIER)0;
        Options.BarycentricsSupported = FALSE;
    }

    /* Below 6.0 wave and 64 bit integer ops go only when the driver also listed previews */
    if (HighestShaderModel < D3D_SHADER_MODEL_6_0 && PreviewShaderModels != 0)
    {
        ShaderCaps.WaveOps = FALSE;
        ShaderCaps.Int64Ops = FALSE;
    }

    return S_OK;
}

/* Typeless formats and the formats they cast to, each family the driver must agree on */
struct RxFormatFamily12
{
    UINT8 Parent;
    UINT8 Members[6];
};

static const RxFormatFamily12 RxFormatFamilies12[] =
{
    { 1, { 2, 3, 4 } },
    { 5, { 6, 7, 8 } },
    { 9, { 10, 11, 12, 13, 14 } },
    { 15, { 16, 17, 18 } },
    { 19, { 20, 21, 22 } },
    { 23, { 24, 25, 89 } },
    { 27, { 28, 29, 30, 31, 32 } },
    { 33, { 34, 35, 36, 37, 38 } },
    { 39, { 40, 41, 42, 43 } },
    { 44, { 45, 46, 47 } },
    { 48, { 49, 50, 51, 52 } },
    { 53, { 54, 55, 56, 57, 58, 59 } },
    { 60, { 61, 62, 63, 64 } },
    { 70, { 71, 72 } },
    { 73, { 74, 75 } },
    { 76, { 77, 78 } },
    { 79, { 80, 81 } },
    { 82, { 83, 84 } },
    { 90, { 87, 91 } },
    { 92, { 88, 93 } },
    { 94, { 95, 96 } },
    { 97, { 98, 99 } },
};

constexpr UINT RxMultisampleCounts12 = 30;

/* The quality level count at 2 to 31 samples, as the driver reports them */
static
VOID
RxQueryQualityLevels(
    _In_ const D3D12DDI_DEVICE_FUNCS_CORE_0116& Functions,
    _In_ D3D12DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _Out_writes_(RxMultisampleCounts12) UINT* Levels)
{
    for (UINT Count = 2; Count < 2 + RxMultisampleCounts12; Count++)
    {
        UINT Level = 0;

        Functions.pfnCheckMultisampleQualityLevels(Device, Format, Count, D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAG_NONE,
                                                   &Level);
        Levels[Count - 2] = Level;
    }
}

/* Every format of a family must report the multisample levels of its typeless parent, or none */
HRESULT
RxDevice12::CheckFormatFamilies()
{
    RxFormatContext12 Context = FormatContext();

    for (const RxFormatFamily12& Family : RxFormatFamilies12)
    {
        UINT ParentLevels[RxMultisampleCounts12];
        UINT ParentBits = 0;

        if (!RxFormatKnown12(Context, (DXGI_FORMAT)Family.Parent))
            continue;

        CoreFunctions.pfnCheckFormatSupport(DriverDevice, (DXGI_FORMAT)Family.Parent, &ParentBits);
        RxQueryQualityLevels(CoreFunctions, DriverDevice, (DXGI_FORMAT)Family.Parent, ParentLevels);

        for (UINT8 Member : Family.Members)
        {
            UINT Levels[RxMultisampleCounts12];
            UINT Bits = 0;

            /* Below 11.0 the extended range format is no member of its family */
            if (Member == 0 || (Member == DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM && Level < D3D_FEATURE_LEVEL_11_0))
                break;

            CoreFunctions.pfnCheckFormatSupport(DriverDevice, (DXGI_FORMAT)Member, &Bits);
            RxQueryQualityLevels(CoreFunctions, DriverDevice, (DXGI_FORMAT)Member, Levels);

            for (UINT Index = 0; Index < RxMultisampleCounts12; Index++)
            {
                BOOLEAN Targetable = (Bits & D3D12DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;

                if ((Targetable && Levels[Index] != ParentLevels[Index]) || (!Targetable && Levels[Index] != 0))
                {
                    RX_TRACE(L"driver multisample levels of format %u disagree with its family", Member);
                    return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
                }
            }
        }
    }

    return S_OK;
}

/* Which physical adapters the API sees as its nodes */
HRESULT
RxDevice12::QueryNodes()
{
    D3DKMT_PHYSICAL_ADAPTER_COUNT Physical = {};
    UINT Map[RxNodeLimit12];
    UINT Index;
    HRESULT hr;

    hr = Adapter->QueryKernel(KMTQAITYPE_PHYSICALADAPTERCOUNT, &Physical, sizeof(Physical));
    if (FAILED(hr))
        return hr;
    if (Physical.Count > RxNodeLimit12)
    {
        RX_TRACE(L"driver exposes %u nodes", Physical.Count);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    for (Index = 0; Index < RxNodeLimit12; Index++)
        Map[Index] = Index;
    CoreFunctions.pfnQueryNodeMap(DriverDevice, Physical.Count, Map);

    PhysicalNodeCount = Physical.Count;
    ApiNodeCount = 0;
    for (Index = 0; Index < PhysicalNodeCount; Index++)
    {
        if (Map[Index] != (UINT)-1)
            ApiNodeCount += 1;
    }
    if (ApiNodeCount == 0)
    {
        RX_TRACE(L"driver exposes no nodes");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    /* Remapping is only allowed while one node shows */
    for (Index = 0; Index < PhysicalNodeCount; Index++)
    {
        if (Map[Index] != Index && ApiNodeCount > 1)
        {
            RX_TRACE(L"driver remaps nodes with more than one showing");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
    }

    for (Index = 0; Index < ApiNodeCount; Index++)
        ApiToPhysical[Index] = (UINT)-1;
    for (Index = 0; Index < PhysicalNodeCount; Index++)
    {
        if (Map[Index] == (UINT)-1)
            continue;
        if (Map[Index] >= ApiNodeCount || ApiToPhysical[Map[Index]] != (UINT)-1)
        {
            RX_TRACE(L"driver node map is invalid");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
        ApiToPhysical[Map[Index]] = Index;
    }

    return S_OK;
}

/* Texture layouts, the memory of every node and the row major layout sets */
HRESULT
RxDevice12::QueryHeapCaps()
{
    static const UINT RowMajorRequest[2] = { D3D12DDI_TL_ROW_MAJOR, D3D12DDI_FUNCUNIT_COMBINED };
    UINT Node;
    HRESULT hr;

    hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT, nullptr, &TextureLayout, sizeof(TextureLayout));
    if (FAILED(hr) || TextureLayout.DeviceDependentLayoutCount > 0xFFFFFEFF ||
        TextureLayout.DeviceDependentSwizzleCount > 0xFFFFFEFF)
    {
        RX_TRACE(L"driver texture layout caps are invalid (0x%08lX)", hr);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    if (TextureLayout.SupportsRowMajorTexture && KmdVersion >= 3000)
    {
        D3DKMT_CROSSADAPTERRESOURCE_SUPPORT Cross = {};

        hr = Adapter->QueryKernel(KMTQAITYPE_CROSSADAPTERRESOURCE_SUPPORT, &Cross, sizeof(Cross));
        if (FAILED(hr))
            return hr;
        if (Cross.SupportTier < D3DKMT_CROSSADAPTERRESOURCE_SUPPORT_TIER_TEXTURE)
        {
            RX_TRACE(L"driver offers row major textures the kernel cannot share");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
    }

    for (Node = 0; Node < PhysicalNodeCount; Node++)
    {
        D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041 Memory = {};
        D3D12DDI_ROW_MAJOR_LAYOUT_CAPS Set = {};
        UINT FalsePositives = 0;
        UINT TrackedBits;
        UINT Index;

        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE, &Node, &Memory, sizeof(Memory));
        if (FAILED(hr))
        {
            ZeroMemory(&Memory, sizeof(Memory));
            hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_MEMORY_ARCHITECTURE, &Node, &Memory,
                                  sizeof(D3D12DDI_MEMORY_ARCHITECTURE_CAPS));
        }
        if (FAILED(hr) || (!Memory.UMA && Memory.CacheCoherent))
        {
            RX_TRACE(L"driver memory architecture caps are invalid (0x%08lX)", hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }

        if (CoreFunctions.pfnCalcPrivateHeapAndResourceSizes == nullptr || CoreFunctions.pfnDestroyHeapAndResource == nullptr ||
            CoreFunctions.pfnCalcPrivateOpenedHeapAndResourceSizes == nullptr || CoreFunctions.pfnOpenHeapAndResource == nullptr)
        {
            RX_TRACE(L"driver heap functions are missing");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }

        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS, const_cast<UINT*>(RowMajorRequest), &Set,
                              sizeof(Set));
        if (FAILED(hr))
        {
            RX_TRACE(L"driver row major layout caps failed 0x%08lX", hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }

        if (CoreFunctions.pfnMapHeap == nullptr || CoreFunctions.pfnUnmapHeap == nullptr ||
            CoreFunctions.pfnCreateHeapAndResource == nullptr || CoreFunctions.pfnCheckResourceAllocationInfo == nullptr ||
            CoreFunctions.pfnCheckSubresourceInfo == nullptr || CoreFunctions.pfnCheckExistingResourceAllocationInfo == nullptr)
        {
            RX_TRACE(L"driver resource functions are missing");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }

        /* Later functional units fall back to the one before when the driver has nothing for them */
        for (Index = 0; Index < _countof(RowMajorLayouts); Index++)
        {
            UINT Request[2] = { D3D12DDI_TL_ROW_MAJOR, Index };
            UINT Previous = 0;

            if (Index != 0 && FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_TEXTURE_LAYOUT_SETS, Request, &Set,
                                                      sizeof(Set))))
            {
                RowMajorLayouts[Index] = RowMajorLayouts[Index - 1];
                continue;
            }

            if (Set.SubCaps[0].MaxElementSize == 0)
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;

            for (const D3D12DDI_ROW_MAJOR_LAYOUT_SUB_CAPS& Sub : Set.SubCaps)
            {
                if (Previous >= Sub.MaxElementSize)
                    break;
                Previous = Sub.MaxElementSize;
                if (Sub.BaseOffsetAlignment == 0 || (Sub.BaseOffsetAlignment & (Sub.BaseOffsetAlignment - 1)) != 0 ||
                    Sub.PitchAlignment == 0 || (Sub.PitchAlignment & (Sub.PitchAlignment - 1)) != 0 ||
                    Sub.DepthPitchAlignment == 0 || (Sub.DepthPitchAlignment & (Sub.DepthPitchAlignment - 1)) != 0)
                {
                    RX_TRACE(L"driver row major alignments are invalid");
                    return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
                }
            }

            if ((Set.Flags & D3D12DDI_ROW_MAJOR_LAYOUT_FLAG_DEPTH_PITCH_4_8_16_HEIGHT_MULTIPLE) != 0 &&
                (Set.Flags & D3D12DDI_ROW_MAJOR_LAYOUT_FLAG_FLEXIBLE_DEPTH_PITCH) == 0)
            {
                RX_TRACE(L"driver row major layout flags are invalid");
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            }
            RowMajorLayouts[Index] = Set;
        }

        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0022_CPU_PAGE_TABLE_FALSE_POSITIVES, &Node, &FalsePositives,
                              sizeof(FalsePositives));
        if (FAILED(hr) || (FalsePositives & ~0x77) != 0)
        {
            RX_TRACE(L"driver page table false positives are invalid (0x%08lX)", hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }

        /* Which engines' false positives break isolation grows with the video feature */
        TrackedBits = VideoVersion >= 13 ? 0x77 : (VideoVersion >= 8 ? 0x37 : 0x7);

        Nodes[Node].Uma = Memory.UMA != FALSE;
        Nodes[Node].CacheCoherent = Memory.CacheCoherent != FALSE;
        Nodes[Node].IoCoherent = Memory.IOCoherent != FALSE;
        Nodes[Node].IsolatedMmu = (FalsePositives & TrackedBits) == 0;
        Nodes[Node].HeapSerializationTier = Memory.HeapSerializationTier == D3D12DDI_HEAP_SERIALIZATION_TIER_0041_1 ?
                                            D3D12_HEAP_SERIALIZATION_TIER_10 : D3D12_HEAP_SERIALIZATION_TIER_0;
    }

    return S_OK;
}

/* The DXCore beside this module, or the system one */
static
HMODULE
RxLoadCoreAdapterModule(VOID)
{
    WCHAR Path[MAX_PATH];
    HMODULE Self = NULL;
    HMODULE Module;
    PWSTR Slash;

    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&RxLoadCoreAdapterModule), &Self) &&
        GetModuleFileNameW(Self, Path, _countof(Path)) != 0 && (Slash = wcsrchr(Path, L'\\')) != nullptr &&
        wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"dxcore.dll") == 0)
    {
        Module = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (Module != NULL)
            return Module;
    }

    return LoadLibraryExW(L"dxcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

/* Whether the DXCore adapter for a LUID preempts both compute and graphics finer than DMA buffers */
static
BOOLEAN
RxCorePreemptsFinely(
    _In_ const LUID& Luid)
{
    using PFN_RX_CREATE_ADAPTER_FACTORY = HRESULT (WINAPI*)(REFIID, void**);
    PFN_RX_CREATE_ADAPTER_FACTORY CreateFactory;
    IDXCoreAdapterFactory* Factory = nullptr;
    IDXCoreAdapter* CoreAdapter = nullptr;
    UINT32 Compute = 0;
    UINT32 Graphics = 0;
    BOOLEAN Fine = FALSE;
    HMODULE Module;

    Module = RxLoadCoreAdapterModule();
    if (Module == NULL)
        return FALSE;

    CreateFactory = reinterpret_cast<PFN_RX_CREATE_ADAPTER_FACTORY>(GetProcAddress(Module, "DXCoreCreateAdapterFactory"));
    if (CreateFactory != nullptr && SUCCEEDED(CreateFactory(__uuidof(IDXCoreAdapterFactory),
                                                            reinterpret_cast<void**>(&Factory))))
    {
        if (SUCCEEDED(Factory->GetAdapterByLuid(Luid, __uuidof(IDXCoreAdapter), reinterpret_cast<void**>(&CoreAdapter))))
        {
            if (SUCCEEDED(CoreAdapter->GetProperty(DXCoreAdapterProperty::ComputePreemptionGranularity, sizeof(Compute),
                                                   &Compute)) &&
                SUCCEEDED(CoreAdapter->GetProperty(DXCoreAdapterProperty::GraphicsPreemptionGranularity,
                                                   sizeof(Graphics), &Graphics)))
            {
                Fine = Compute > D3DKMDT_COMPUTE_PREEMPTION_DMA_BUFFER_BOUNDARY &&
                       Graphics > D3DKMDT_GRAPHICS_PREEMPTION_DMA_BUFFER_BOUNDARY;
            }
            CoreAdapter->Release();
        }
        Factory->Release();
    }

    FreeLibrary(Module);
    return Fine;
}

/* The answers that follow the driver tables and come before the runtime's own objects */
HRESULT
RxDevice12::QueryLateCaps()
{
    HRESULT hr;

    if (Level >= D3D_FEATURE_LEVEL_12_0 &&
        (Options.TiledResourcesTier < D3D12DDI_TILED_RESOURCES_TIER_2 ||
         Options.ResourceBindingTier < D3D12DDI_RESOURCE_BINDING_TIER_2 || !ShaderCaps.TypedUAVLoadAdditionalFormats))
    {
        RX_TRACE(L"driver caps fall short of level 12.0");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }
    if (Level >= D3D_FEATURE_LEVEL_12_1 && ((INT)Options.ConservativeRasterizationTier < 1 || !ShaderCaps.ROVs))
    {
        RX_TRACE(L"driver caps fall short of level 12.1");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_ARCHITECTURE_INFO, nullptr, &Architecture, sizeof(Architecture));
    if (FAILED(hr))
    {
        RX_TRACE(L"driver architecture query failed 0x%08lX", hr);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    /* Minimal typed loads come from the additional formats cap or the two byte sized integer formats */
    TypedLoadMinimal = ShaderCaps.TypedUAVLoadAdditionalFormats != FALSE;
    for (DXGI_FORMAT Format : { DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8_UINT })
    {
        UINT Bits = 0;

        if (TypedLoadMinimal)
            break;
        if (RxTypedLoadRule12(Level, Format) == RxRuleRequired)
        {
            TypedLoadMinimal = TRUE;
            break;
        }
        CoreFunctions.pfnCheckFormatSupport(DriverDevice, Format, &Bits);
        TypedLoadMinimal = (Bits & D3D12DDI_FORMAT_SUPPORT_UAV_READS) != 0;
    }

    hr = ApplyShaderModelRules();
    if (FAILED(hr))
    {
        RX_TRACE(L"driver caps need a newer shader model than it offers");
        return hr;
    }

    {
        D3D12DDICAPS_UMD_BASED_COMMAND_QUEUE_PRIORITY_DATA_0023 Priority = {};

        /* Only the seven queue flag bits are allowed in the answer */
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0023_UMD_BASED_COMMAND_QUEUE_PRIORITY, nullptr, &Priority,
                              sizeof(Priority));
        if (FAILED(hr) || (Priority.SupportedQueueFlagsForGlobalRealtimeQueues & ~0x7F) != 0)
        {
            RX_TRACE(L"driver queue priority caps query failed 0x%08lX", hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
        RealtimeQueueFlags = Priority.SupportedQueueFlagsForGlobalRealtimeQueues;
    }

    hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0050_HARDWARE_SCHEDULING_CAPS, nullptr, &Scheduling, sizeof(Scheduling));
    if (FAILED(hr))
    {
        RX_TRACE(L"driver hardware scheduling caps query failed 0x%08lX", hr);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    hr = Adapter->QueryKernel(KMTQAITYPE_DRIVERVERSION, &KmdVersion, sizeof(KmdVersion));
    if (FAILED(hr))
        return hr;

    if (Level >= D3D_FEATURE_LEVEL_10_0)
    {
        hr = CheckFormatFamilies();
        if (FAILED(hr))
            return hr;
    }

    hr = QueryNodes();
    if (FAILED(hr))
        return hr;

    if ((ApiNodeCount > 1) != (Options.CrossNodeSharingTier != 0))
    {
        RX_TRACE(L"driver cross node sharing tier does not fit its nodes");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    hr = QueryHeapCaps();
    if (FAILED(hr))
        return hr;

    /* Shader visible heap limits; drivers without the query keep the tier limits */
    HasHeapLimits = SUCCEEDED(Adapter->GetCaps(D3D12DDICAPS_TYPE_OPTIONS_0102, nullptr, &HeapLimits,
                                                sizeof(HeapLimits)));
    if (!HasHeapLimits)
    {
        ZeroMemory(&HeapLimits, sizeof(HeapLimits));
        HeapLimits.MaxSamplerDescriptorHeapSize = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;
    }
    else if (HeapLimits.MaxViewDescriptorHeapSize < 1000000 ||
             (Level >= D3D_FEATURE_LEVEL_11_0 &&
              (HeapLimits.MaxSamplerDescriptorHeapSize < 4000 ||
               HeapLimits.MaxSamplerDescriptorHeapSizeWithStaticSamplers < 2048 ||
               HeapLimits.MaxSamplerDescriptorHeapSizeWithStaticSamplers > HeapLimits.MaxSamplerDescriptorHeapSize ||
               (Level >= D3D_FEATURE_LEVEL_11_1 && (HeapLimits.SupportedSampleCountsWithNoOutputs & 0x1D) != 0x1D))))
    {
        RX_TRACE(L"driver descriptor heap limits are too small");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    /* Global realtime priority needs preemption finer than whole DMA buffers, as DXCore reports it */
    GlobalPriorityAvailable = RxCorePreemptsFinely(Adapter->GetLuid());

    {
        const KmtThunks* Kmt = RxGetKmtThunks();
        D3DKMT_ISFEATUREENABLED Feature = {};

        /* The bug fix feature that makes GPU upload heaps safe on this kernel */
        Feature.hAdapter = Adapter->GetKmtHandle();
        Feature.FeatureId = (DXGK_FEATURE_ID)DXGK_DEFINE_FEATURE_ID(DXGK_FEATURE_CATEGORY_BUGFIX, 0);
        GpuUploadOs = Kmt->IsFeatureEnabled != nullptr && NT_SUCCESS(Kmt->IsFeatureEnabled(&Feature)) &&
                      Feature.Result.Enabled;
    }

    Displayable = TRUE;
    BackgroundProcessing = Options.BackgroundProcessingSupported != FALSE;
    return S_OK;
}

/* Process wide bits come from the kernel's MMU, per resource bits are also capped by the driver */
HRESULT
RxDevice12::QueryVirtualAddressBits()
{
    D3DKMT_WDDM_2_0_CAPS Caps = {};
    UINT Node;
    HRESULT hr;

    hr = Adapter->QueryKernel(KMTQAITYPE_WDDM_2_0_CAPS, &Caps, sizeof(Caps));
    if (FAILED(hr))
        return hr;

    VaBitsPerProcess = 64;
    if (Caps.IoMmuSupported)
    {
        SYSTEM_INFO System;
        UINT64 Span;

        GetSystemInfo(&System);
        Span = (UINT64)(ULONG_PTR)System.lpMaximumApplicationAddress - (UINT64)(ULONG_PTR)System.lpMinimumApplicationAddress;
        for (VaBitsPerProcess = 0; Span != 0; Span >>= 1)
            VaBitsPerProcess++;
    }

    VaBitsPerResource = VaBitsPerProcess;
    if (Caps.GpuMmuSupported)
    {
        for (Node = 0; Node < PhysicalNodeCount; Node++)
        {
            D3DKMT_QUERY_GPUMMU_CAPS Mmu = {};

            Mmu.PhysicalAdapterIndex = Node;
            if (FAILED(Adapter->QueryKernel(KMTQAITYPE_QUERY_GPUMMU_CAPS, &Mmu, sizeof(Mmu))))
            {
                VaBitsPerProcess = 38;
                break;
            }
            if (Mmu.Caps.VirtualAddressBitCount < VaBitsPerProcess)
                VaBitsPerProcess = Mmu.Caps.VirtualAddressBitCount;
        }
        VaBitsPerResource = VaBitsPerProcess;
    }

    for (Node = 0; Node < ApiNodeCount; Node++)
    {
        D3D12DDI_GPUVA_CAPS_0004 DriverCaps = {};
        UINT Physical = ApiToPhysical[Node];

        if (FAILED(Adapter->GetCaps(D3D12DDICAPS_TYPE_GPUVA_CAPS, &Physical, &DriverCaps, sizeof(DriverCaps))))
            continue;
        if (DriverCaps.MaxGPUVirtualAddressBitsPerResource == 0)
        {
            RX_TRACE(L"driver offers no virtual address bits per resource");
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
        if (DriverCaps.MaxGPUVirtualAddressBitsPerResource < VaBitsPerResource)
            VaBitsPerResource = DriverCaps.MaxGPUVirtualAddressBitsPerResource;
    }

    return S_OK;
}

/* Level 12.2 promises, then the runtime's own pool and the driver's background work */
HRESULT
RxDevice12::CreateInternalObjects()
{
    D3D12DDIARG_CREATE_COMMAND_POOL_0040 PoolArgs = {};
    UINT Batched = 0;
    HRESULT hr;

    if (Level >= D3D_FEATURE_LEVEL_12_2 &&
        (HighestShaderModel < D3D_SHADER_MODEL_6_5 || (INT)Options.RaytracingTier < 11 ||
         (INT)Options.VariableShadingRateTier < 2 || (INT)Options.MeshShaderTier < 10 ||
         (INT)Options.SamplerFeedbackTier < 90 || Options.ResourceBindingTier < D3D12DDI_RESOURCE_BINDING_TIER_3 ||
         Options.TiledResourcesTier < D3D12DDI_TILED_RESOURCES_TIER_3 || (INT)Options.ConservativeRasterizationTier < 3 ||
         !Options.DepthBoundsTestSupported || VaBitsPerProcess < 40 || VaBitsPerResource < 40 || !ShaderCaps.WaveOps ||
         !Options.OutputMergerLogicOp || !Options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation ||
         !Options.CopyQueueTimestampQueriesSupported || !ShaderCaps.Int64Ops ||
         (Options.WriteBufferImmediateQueueFlags & 0x3) != 0x3))
    {
        RX_TRACE(L"driver caps fall short of level 12.2");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    if (Options.VariableShadingRateTier != 0 && Options.ShadingRateImageTileSize != 0 &&
        Options.ShadingRateImageTileSize != 8 && Options.ShadingRateImageTileSize != 16 &&
        Options.ShadingRateImageTileSize != 32)
    {
        RX_TRACE(L"driver shading rate tile size %u is invalid", Options.ShadingRateImageTileSize);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    RtvDriverSize = CoreFunctions.pfnGetDescriptorSizeInBytes(DriverDevice, D3D12DDI_DESCRIPTOR_HEAP_TYPE_RTV);

    /* A pool of the runtime's own, which the driver can record internal work into */
    CommandPool.pDrvPrivate = PoolMemory.Allocate(CoreFunctions.pfnCalcPrivateCommandPoolSize(DriverDevice, &PoolArgs));
    if (CommandPool.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;
    hr = CoreFunctions.pfnCreateCommandPool(DriverDevice, &PoolArgs, CommandPool);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver command pool failed 0x%08lX", hr);
        CommandPool.pDrvPrivate = nullptr;
        return hr;
    }

    if (BackgroundProcessing && CoreFunctions.pfnSetBackgroundProcessingMode != nullptr)
    {
        BOOL Further = FALSE;

        CoreFunctions.pfnSetBackgroundProcessingMode(DriverDevice, D3D12DDI_BACKGROUND_PROCESSING_MODE_0062_ALLOWED,
                                                     D3D12DDI_MEASUREMENTS_ACTION_0062_KEEP_ALL, &Further);
    }

    BatchedMarkers = SUCCEEDED(Adapter->GetCaps(D3D12DDICAPS_TYPE_0073_SUPPORT_BATCHED_MARKERS, nullptr, &Batched,
                                                sizeof(Batched))) && Batched != 0;
    return S_OK;
}

HRESULT
RxDevice12::SessionSupport(
    _In_ UINT NodeIndex,
    _Out_ BOOLEAN* Supported)
{
    HRESULT hr = QuerySessionCaps();

    *Supported = SUCCEEDED(hr) && NodeIndex < ApiNodeCount && Sessions[NodeIndex].Support != 0;
    return hr;
}

/* Protected session support of every node, read once */
HRESULT
RxDevice12::QuerySessionCaps()
{
    HRESULT hr = S_OK;

    AcquireSRWLockExclusive(&SessionLock);
    if (SessionsQueried || SessionVersion == 0)
    {
        SessionsQueried = TRUE;
        ReleaseSRWLockExclusive(&SessionLock);
        return S_OK;
    }

    for (UINT Node = 0; Node < ApiNodeCount && SUCCEEDED(hr); Node++)
    {
        D3D12DDI_PROTECTED_RESOURCE_SESSION_SUPPORT_DATA_0030 Support = {};
        D3D12DDI_PROTECTED_RESOURCE_SESSION_TYPE_COUNT_DATA_0074 Count = {};
        D3D12DDI_PROTECTED_RESOURCE_SESSION_TYPES_DATA_0074 Types = {};
        RxSessionCaps12& Caps = Sessions[Node];
        BOOLEAN HasHardware = FALSE;

        Support.NodeIndex = ApiToPhysical[Node];
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0030_PROTECTED_RESOURCE_SESSION_SUPPORT, nullptr, &Support,
                              sizeof(Support));
        if (FAILED(hr) || ((UINT)Support.Support & ~1u) != 0)
        {
            hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            break;
        }

        Caps.Support = (UINT)Support.Support & 1;
        if (Caps.Support == 0)
            continue;

        /* Drivers before type lists only know hardware protected sessions */
        if (SessionVersion < D3D12DDI_FEATURE_VERSION_CONTENT_PROTECTION_RESOURCES_0074_0)
        {
            Caps.Types = new (std::nothrow) GUID[1];
            if (Caps.Types == nullptr)
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            Caps.Types[0] = D3D12_PROTECTED_RESOURCES_SESSION_HARDWARE_PROTECTED;
            Caps.TypeCount = 1;
            continue;
        }

        Count.NodeIndex = Support.NodeIndex;
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0074_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, nullptr, &Count,
                              sizeof(Count));
        if (FAILED(hr) || Count.Count == 0)
        {
            hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            break;
        }

        Caps.Types = new (std::nothrow) GUID[Count.Count];
        if (Caps.Types == nullptr)
        {
            hr = E_OUTOFMEMORY;
            break;
        }

        Types.NodeIndex = Support.NodeIndex;
        Types.Count = Count.Count;
        Types.pTypes = Caps.Types;
        hr = Adapter->GetCaps(D3D12DDICAPS_TYPE_0074_PROTECTED_RESOURCE_SESSION_TYPES, nullptr, &Types, sizeof(Types));
        if (FAILED(hr))
        {
            hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            break;
        }

        /* Only hardware protection is a registered type */
        for (UINT Index = 0; Index < Count.Count; Index++)
        {
            if (Caps.Types[Index] != D3D12_PROTECTED_RESOURCES_SESSION_HARDWARE_PROTECTED)
            {
                RX_TRACE(L"driver lists an unregistered protected session type");
                hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
                break;
            }
            HasHardware = TRUE;
        }
        if (SUCCEEDED(hr) && !HasHardware)
            hr = DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Caps.TypeCount = Count.Count;
    }

    SessionsQueried = TRUE;
    ReleaseSRWLockExclusive(&SessionLock);
    return hr;
}

HRESULT
RxDevice12::TranslateNodeMask(
    _In_ UINT Mask,
    _In_ BOOLEAN Single,
    _Out_ UINT* Translated) const
{
    UINT Effective = Mask != 0 ? Mask : 1;
    UINT Result = 0;

    *Translated = 0;
    if (Single && (Effective & (Effective - 1)) != 0)
        return E_INVALIDARG;
    if (ApiNodeCount < 32 && (Effective >> ApiNodeCount) != 0)
        return E_INVALIDARG;

    for (UINT Node = 0; Node < ApiNodeCount; Node++)
    {
        if ((Effective & (1u << Node)) != 0)
            Result |= 1u << ApiToPhysical[Node];
    }

    *Translated = Result;
    return S_OK;
}

HRESULT
RxDevice12::NodeForCaps(
    _In_ UINT NodeIndex,
    _Out_ UINT* Physical) const
{
    UINT Translated;
    HRESULT hr;

    *Physical = 0;
    if (NodeIndex >= 32)
        return E_INVALIDARG;

    hr = TranslateNodeMask(1u << NodeIndex, TRUE, &Translated);
    if (FAILED(hr))
        return hr;

    for (UINT Bit = 0; Bit < 32; Bit++)
    {
        if ((Translated & (1u << Bit)) != 0)
        {
            *Physical = Bit;
            break;
        }
    }
    return S_OK;
}
