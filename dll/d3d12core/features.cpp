/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12Device::CheckFeatureSupport
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatsupport12.hpp"
#include "shadercache.hpp"

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

static
UINT
RxQueryDriverFormat(
    _In_ VOID* Context,
    _In_ DXGI_FORMAT Format)
{
    RxDevice12* Device = static_cast<RxDevice12*>(Context);
    UINT Bits = 0;

    Device->Ddi().pfnCheckFormatSupport(Device->Handle(), Format, &Bits);
    return Bits;
}

/* Queue flags in the driver's terms turned into the list types they let commands run on */
static
UINT
RxListSupportOf(
    _In_ UINT QueueFlags)
{
    UINT Support = 0;

    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_3D) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_DIRECT | D3D12_COMMAND_LIST_SUPPORT_FLAG_BUNDLE;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_COMPUTE) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_COMPUTE;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_COPY) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_COPY;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_DECODE;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_PROCESS;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_ENCODE;
    return Support;
}

RxFormatContext12
RxDevice12::FormatContext()
{
    RxFormatContext12 Context = {};

    Context.Level = Level;
    Context.Video = VideoVersion >= 8;
    Context.LogicOp = Options.OutputMergerLogicOp != FALSE;
    Context.Tiled = Options.TiledResourcesTier != D3D12DDI_TILED_RESOURCES_TIER_NOT_SUPPORTED;
    Context.SamplerFeedback = Options.SamplerFeedbackTier != 0;
    Context.TypedLoadExtra = Level >= D3D_FEATURE_LEVEL_11_0 && ShaderCaps.TypedUAVLoadAdditionalFormats;
    Context.TypedLoadMinimal = Level >= D3D_FEATURE_LEVEL_11_0 && TypedLoadMinimal;
    Context.DisplayableDdi = TRUE;
    return Context;
}

/* Tiled resources below 11.1 stop at tier 1 */
static
UINT
RxApiTiledTier(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ UINT Tier)
{
    if (Level < D3D_FEATURE_LEVEL_11_1)
        return Tier > 1 ? 1 : Tier;
    return Tier;
}

/* The heap a standard heap type stands for on this device, as custom properties */
static
VOID
RxCustomPropertiesOf(
    _In_ D3D12_HEAP_TYPE Type,
    _In_ BOOLEAN Uma,
    _In_ BOOLEAN CacheCoherent,
    _Out_ D3D12_CPU_PAGE_PROPERTY* Page,
    _Out_ D3D12_MEMORY_POOL* Pool)
{
    switch (Type)
    {
        case D3D12_HEAP_TYPE_DEFAULT:
            *Page = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            *Pool = Uma ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
        case D3D12_HEAP_TYPE_UPLOAD:
            *Page = CacheCoherent ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            *Pool = D3D12_MEMORY_POOL_L0;
            break;
        case D3D12_HEAP_TYPE_READBACK:
            *Page = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            *Pool = D3D12_MEMORY_POOL_L0;
            break;
        default:
            *Page = Uma ? (CacheCoherent ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE) :
                          D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            *Pool = Uma ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
    }
}

/* Whether a placed resource of a format and dimension can live in a heap, asked of the driver */
HRESULT
RxDevice12::CheckPlacedSupport(
    _Inout_ D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO* Data)
{
    D3D12DDI_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO_106 Query = {};
    D3D12_HEAP_PROPERTIES& Heap = Data->DestHeapProperties;
    UINT Creation = 0;
    UINT Visible = 0;
    HRESULT hr;

    /* A standard heap type is asked about as the custom heap it stands for */
    if (Heap.Type != D3D12_HEAP_TYPE_CUSTOM)
    {
        D3D12_HEAP_TYPE Type = Heap.Type;
        UINT Translated;
        UINT Physical = 0;

        hr = TranslateNodeMask(Heap.CreationNodeMask, TRUE, &Translated);
        if (FAILED(hr))
            return hr;

        if (Type < D3D12_HEAP_TYPE_DEFAULT || Type > D3D12_HEAP_TYPE_GPU_UPLOAD || Type == D3D12_HEAP_TYPE_CUSTOM)
        {
            ZeroMemory(&Heap, sizeof(Heap));
            Heap.CreationNodeMask = 1;
            Heap.VisibleNodeMask = 1;
        }
        else
        {
            for (UINT Bit = 0; Bit < 32; Bit++)
            {
                if (((Translated ? Translated : 1) & (1u << Bit)) != 0)
                {
                    Physical = Bit;
                    break;
                }
            }

            Heap.Type = D3D12_HEAP_TYPE_CUSTOM;
            RxCustomPropertiesOf(Type, Nodes[Physical].Uma, Nodes[Physical].CacheCoherent, &Heap.CPUPageProperty,
                                 &Heap.MemoryPoolPreference);
            Heap.CreationNodeMask = Heap.CreationNodeMask ? Heap.CreationNodeMask : 1;
            Heap.VisibleNodeMask = Heap.CreationNodeMask;
        }
    }

    hr = TranslateNodeMask(Heap.CreationNodeMask, TRUE, &Creation);
    if (SUCCEEDED(hr))
        hr = TranslateNodeMask(Heap.VisibleNodeMask, TRUE, &Visible);
    if (FAILED(hr))
        return hr;

    Query.Format = Data->Format;
    Query.ResourceType = (D3D12DDI_RESOURCE_TYPE)Data->Dimension;
    Query.MemoryPool = Heap.MemoryPoolPreference == D3D12_MEMORY_POOL_L0 ? D3D12DDI_MEMORY_POOL_L0 : D3D12DDI_MEMORY_POOL_L1;
    Query.CPUPageProperty = (D3D12DDI_CPU_PAGE_PROPERTY)Heap.CPUPageProperty;
    Query.CreationNodeMask = Creation;
    Query.VisibleNodeMask = Visible;

    hr = Adapter->GetCaps(D3D12DDI_FEATURE_PLACED_RESOURCE_SUPPORT_INFO_106, nullptr, &Query, sizeof(Query));
    if (FAILED(hr))
        return hr;

    /* Every placement the API names is required beyond generic devices */
    if (Level > D3D_FEATURE_LEVEL_1_0_GENERIC && !Query.Supported)
    {
        RX_TRACE(L"driver refused a placement the level requires");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    Data->Supported = Query.Supported;
    return S_OK;
}

/* The shader cache abilities: the runtime's caches and whatever the system cache library offers */
UINT
RxDevice12::ShaderCacheSupport() const
{
    static INIT_ONCE Once = INIT_ONCE_STATIC_INIT;
    static UINT LibrarySupport = 0;
    UINT Support = D3D12_SHADER_CACHE_SUPPORT_SINGLE_PSO | D3D12_SHADER_CACHE_SUPPORT_LIBRARY;
    BOOL Pending = FALSE;

    /* The store behind the driver's shader cache callbacks is the automatic cache, on disk when it has a file */
    if (DriverShaderCache != nullptr)
    {
        Support |= D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_INPROC_CACHE;
        if (DriverShaderCache->GetMode() == D3D12_SHADER_CACHE_MODE_DISK)
            Support |= D3D12_SHADER_CACHE_SUPPORT_AUTOMATIC_DISK_CACHE;
    }

    if (Options.DriverManagedShaderCachePresent)
        Support |= D3D12_SHADER_CACHE_SUPPORT_DRIVER_MANAGED_CACHE;

    if (InitOnceBeginInitialize(&Once, 0, &Pending, nullptr) && Pending)
    {
        HMODULE Cache = LoadLibraryExW(L"D3DSCache.dll", NULL, 0);

        if (Cache != NULL)
        {
            if (GetProcAddress(Cache, "ShaderCache_Clear") != nullptr)
                LibrarySupport |= D3D12_SHADER_CACHE_SUPPORT_SHADER_CONTROL_CLEAR;
            if (GetProcAddress(Cache, "ShaderCache_Delete") != nullptr)
                LibrarySupport |= D3D12_SHADER_CACHE_SUPPORT_SHADER_SESSION_DELETE;
        }
        InitOnceComplete(&Once, 0, nullptr);
    }

    return Support | LibrarySupport;
}

/* A one BOOL answer the driver gives directly, which levels past generic must not deny */
static
HRESULT
RxDriverBool(
    _In_ const RxAdapter12& Adapter,
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ D3D12DDICAPS_TYPE Type,
    _Out_ BOOL* Value)
{
    HRESULT hr;

    hr = Adapter.GetCaps(Type, nullptr, Value, sizeof(*Value));
    if (FAILED(hr))
        return hr;
    if (Level > D3D_FEATURE_LEVEL_1_0_GENERIC && !*Value)
    {
        RX_TRACE(L"driver denied caps %u the level requires", Type);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }
    return S_OK;
}

/* Feature data checked against the size the caller gave */
#define RX_FEATURE_DATA(Type, Name) \
    Type* Name = static_cast<Type*>(pFeatureSupportData); \
    if (FeatureSupportDataSize != sizeof(*Name)) \
        return E_INVALIDARG

HRESULT
STDMETHODCALLTYPE
RxDevice12::CheckFeatureSupport(
    D3D12_FEATURE Feature,
    _Inout_updates_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    const RxNodeCaps12& Node0 = Nodes[ApiToPhysical[0]];
    UINT SharedTier = 2;
    UINT Physical;
    HRESULT hr;

    if (pFeatureSupportData == nullptr)
        return E_INVALIDARG;

    switch ((UINT)Feature)
    {
        case D3D12_FEATURE_D3D12_OPTIONS:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS, Data);

            Data->DoublePrecisionFloatShaderOps = ShaderCaps.DoubleOps != FALSE;
            Data->OutputMergerLogicOp = Options.OutputMergerLogicOp != FALSE;
            Data->MinPrecisionSupport = (D3D12_SHADER_MIN_PRECISION_SUPPORT)(
                (ShaderCaps.MinPrecision & 1) | ((ShaderCaps.MinPrecision & 2) != 0 ? 2 : 0));
            Data->TiledResourcesTier = (D3D12_TILED_RESOURCES_TIER)RxApiTiledTier(Level, Options.TiledResourcesTier);
            Data->ResourceBindingTier = (D3D12_RESOURCE_BINDING_TIER)Options.ResourceBindingTier;
            Data->PSSpecifiedStencilRefSupported = Level >= D3D_FEATURE_LEVEL_11_1 && ShaderCaps.ShaderSpecifiedStencilRef;
            Data->TypedUAVLoadAdditionalFormats = Level >= D3D_FEATURE_LEVEL_11_0 && ShaderCaps.TypedUAVLoadAdditionalFormats;
            Data->ROVsSupported = Level >= D3D_FEATURE_LEVEL_11_1 && ShaderCaps.ROVs;
            Data->ConservativeRasterizationTier = Level >= D3D_FEATURE_LEVEL_11_1 ?
                (D3D12_CONSERVATIVE_RASTERIZATION_TIER)Options.ConservativeRasterizationTier :
                D3D12_CONSERVATIVE_RASTERIZATION_TIER_NOT_SUPPORTED;
            Data->MaxGPUVirtualAddressBitsPerResource = VaBitsPerResource;
            Data->StandardSwizzle64KBSupported = TextureLayout.Supports64KStandardSwizzle != FALSE;
            Data->CrossNodeSharingTier = (D3D12_CROSS_NODE_SHARING_TIER)(Options.CrossNodeSharingTier > 3 ? 3 :
                                                                         Options.CrossNodeSharingTier);
            Data->CrossAdapterRowMajorTextureSupported = TextureLayout.SupportsRowMajorTexture != FALSE;
            Data->VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation =
                Options.VPAndRTArrayIndexFromAnyShaderFeedingRasterizerSupportedWithoutGSEmulation != FALSE;
            Data->ResourceHeapTier = (D3D12_RESOURCE_HEAP_TIER)Options.ResourceHeapTier;
            return S_OK;
        }

        case D3D12_FEATURE_ARCHITECTURE:
        {
            D3D12_FEATURE_DATA_ARCHITECTURE* Data = static_cast<D3D12_FEATURE_DATA_ARCHITECTURE*>(pFeatureSupportData);
            D3D12_FEATURE_DATA_ARCHITECTURE1 Full = {};

            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;

            Full.NodeIndex = Data->NodeIndex;
            hr = CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &Full, sizeof(Full));
            if (FAILED(hr))
                return hr;

            Data->TileBasedRenderer = Full.TileBasedRenderer;
            Data->UMA = Full.UMA;
            Data->CacheCoherentUMA = Full.CacheCoherentUMA;
            return S_OK;
        }

        case D3D12_FEATURE_FEATURE_LEVELS:
        {
            D3D12_FEATURE_DATA_FEATURE_LEVELS* Data = static_cast<D3D12_FEATURE_DATA_FEATURE_LEVELS*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data) || Data->NumFeatureLevels == 0 ||
                Data->pFeatureLevelsRequested == nullptr)
            {
                return E_INVALIDARG;
            }

            Data->MaxSupportedFeatureLevel = (D3D_FEATURE_LEVEL)0;
            for (UINT Index = 0; Index < Data->NumFeatureLevels; Index++)
            {
                D3D_FEATURE_LEVEL Asked = Data->pFeatureLevelsRequested[Index];

                if (Asked <= Level && Asked > Data->MaxSupportedFeatureLevel)
                    Data->MaxSupportedFeatureLevel = Asked;
            }

            return Data->MaxSupportedFeatureLevel != 0 ? S_OK : DXGI_ERROR_UNSUPPORTED;
        }

        case D3D12_FEATURE_FORMAT_SUPPORT:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_FORMAT_SUPPORT, Data);
            RxFormatContext12 Context = FormatContext();
            UINT Support1;
            UINT Support2;
            BOOLEAN Supported;

            Supported = RxCheckFormatSupport12(Context, Data->Format, RxQueryDriverFormat, this, &Support1, &Support2);
            Data->Support1 = (D3D12_FORMAT_SUPPORT1)Support1;
            Data->Support2 = (D3D12_FORMAT_SUPPORT2)Support2;
            return Supported ? S_OK : E_FAIL;
        }

        case D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS, Data);
            RxFormatContext12 Context = FormatContext();
            BOOLEAN Tiled;
            UINT Levels = 0;

            if (!RxFormatKnown12(Context, Data->Format))
                return E_FAIL;

            /* Tiled resources need the format to be tileable at that count and a tiled tier at all */
            Tiled = (Data->Flags & D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_TILED_RESOURCE) != 0;
            if (Data->SampleCount == 1)
            {
                Data->NumQualityLevels = !Tiled || (RxFormatTileable12(Context, Data->Format, 1) && Context.Tiled);
                return S_OK;
            }

            if (Data->SampleCount == 0 || Data->SampleCount > D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT)
            {
                Data->NumQualityLevels = 0;
                return E_FAIL;
            }

            CoreFunctions.pfnCheckMultisampleQualityLevels(DriverDevice, Data->Format, Data->SampleCount,
                                                           (D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS)Data->Flags,
                                                           &Levels);
            if (Tiled && (!RxFormatTileable12(Context, Data->Format, Data->SampleCount) || !Context.Tiled))
                Levels = 0;
            Data->NumQualityLevels = Levels > 0x80000000 ? 0x80000000 : Levels;
            return S_OK;
        }

        case D3D12_FEATURE_FORMAT_INFO:
        {
            D3D12_FEATURE_DATA_FORMAT_INFO* Data = static_cast<D3D12_FEATURE_DATA_FORMAT_INFO*>(pFeatureSupportData);

            if (FeatureSupportDataSize != sizeof(*Data) || !RxFormatKnown12(FormatContext(), Data->Format))
                return E_INVALIDARG;

            Data->PlaneCount = (UINT8)RxPlaneCount(Data->Format);
            return S_OK;
        }

        case D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT, Data);

            Data->MaxGPUVirtualAddressBitsPerResource = VaBitsPerResource;
            Data->MaxGPUVirtualAddressBitsPerProcess = VaBitsPerProcess;
            return S_OK;
        }

        case D3D12_FEATURE_SHADER_MODEL:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_SHADER_MODEL, Data);

            if (!RxIsKnownShaderModel(Data->HighestShaderModel))
                return E_INVALIDARG;
            if (Data->HighestShaderModel > HighestShaderModel)
                Data->HighestShaderModel = HighestShaderModel;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS1:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS1, Data);

            Data->WaveOps = ShaderCaps.WaveOps != FALSE;
            Data->WaveLaneCountMin = ShaderCaps.WaveLaneCountMin;
            Data->WaveLaneCountMax = ShaderCaps.WaveLaneCountMax;
            Data->TotalLaneCount = ShaderCaps.TotalLaneCount;
            Data->ExpandedComputeResourceStates = TRUE;
            Data->Int64ShaderOps = ShaderCaps.Int64Ops != FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT, Data);

            if (Data->NodeIndex >= ApiNodeCount)
                return E_INVALIDARG;
            hr = QuerySessionCaps();
            if (FAILED(hr))
                return hr;
            Data->Support = (D3D12_PROTECTED_RESOURCE_SESSION_SUPPORT_FLAGS)Sessions[Data->NodeIndex].Support;
            return S_OK;
        }

        case D3D12_FEATURE_ROOT_SIGNATURE:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_ROOT_SIGNATURE, Data);

            /* Every version the runtime knows is supported, so the answer is the question */
            if (Data->HighestVersion < D3D_ROOT_SIGNATURE_VERSION_1_0 ||
                Data->HighestVersion > D3D_ROOT_SIGNATURE_VERSION_1_2)
            {
                return E_INVALIDARG;
            }
            return S_OK;
        }

        case D3D12_FEATURE_ARCHITECTURE1:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_ARCHITECTURE1, Data);

            hr = NodeForCaps(Data->NodeIndex, &Physical);
            if (FAILED(hr))
                return hr;

            Data->TileBasedRenderer = Architecture.TileBasedDeferredRenderer;
            Data->UMA = Nodes[Physical].Uma;
            Data->CacheCoherentUMA = Nodes[Physical].CacheCoherent;
            Data->IsolatedMMU = Nodes[Physical].IsolatedMmu;
            return S_OK;
        }

        case 17:
        {
            UINT* Flags = static_cast<UINT*>(pFeatureSupportData);

            /* The pipeline state flags this driver takes */
            if (FeatureSupportDataSize != sizeof(*Flags) || (*Flags & ~0xFu) != 0)
                return E_INVALIDARG;
            *Flags &= 0xE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS2:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS2, Data);

            Data->DepthBoundsTestSupported = Options.DepthBoundsTestSupported != FALSE;
            Data->ProgrammableSamplePositionsTier =
                (D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER)Options.ProgrammableSamplePositionsTier;
            return S_OK;
        }

        case D3D12_FEATURE_SHADER_CACHE:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_SHADER_CACHE, Data);

            Data->SupportFlags = (D3D12_SHADER_CACHE_SUPPORT_FLAGS)ShaderCacheSupport();
            return S_OK;
        }

        case D3D12_FEATURE_COMMAND_QUEUE_PRIORITY:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY, Data);

            if ((UINT)Data->CommandListType + 1 > 7)
                return E_INVALIDARG;

            if (Data->Priority == D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ||
                Data->Priority == D3D12_COMMAND_QUEUE_PRIORITY_HIGH)
            {
                Data->PriorityForTypeIsSupported = TRUE;
            }
            else if (Data->Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
            {
                UINT Flags = RxQueueFlagsOf(Data->CommandListType);

                /* A type without queue flags is judged as video encode */
                if (Flags == D3D12DDI_COMMAND_QUEUE_FLAG_NONE)
                    Flags = D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE;

                /* The kernel's own global priority covers every type; without it the driver's flags decide */
                Data->PriorityForTypeIsSupported = GlobalPriorityAvailable ||
                                                   (Flags & ~RealtimeQueueFlags) == 0;
            }
            else
            {
                return E_INVALIDARG;
            }
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS3:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS3, Data);

            Data->CopyQueueTimestampQueriesSupported = Options.CopyQueueTimestampQueriesSupported;
            Data->CastingFullyTypedFormatSupported = TRUE;
            Data->WriteBufferImmediateSupportFlags =
                (D3D12_COMMAND_LIST_SUPPORT_FLAGS)RxListSupportOf(Options.WriteBufferImmediateQueueFlags);
            Data->ViewInstancingTier = (D3D12_VIEW_INSTANCING_TIER)Options.ViewInstancingTier;
            Data->BarycentricsSupported = Options.BarycentricsSupported != FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_EXISTING_HEAPS:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_EXISTING_HEAPS, Data);

            Data->Supported = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS4:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS4, Data);

            Data->MSAA64KBAlignedTextureSupported = Options.ReservedBufferPlacementSupported;
            Data->SharedResourceCompatibilityTier = (D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER)SharedTier;
            Data->Native16BitShaderOpsSupported = ShaderCaps.Native16BitOps != FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_SERIALIZATION:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_SERIALIZATION, Data);

            hr = NodeForCaps(Data->NodeIndex, &Physical);
            if (FAILED(hr))
                return hr;
            Data->HeapSerializationTier = Nodes[Physical].HeapSerializationTier;
            return S_OK;
        }

        case D3D12_FEATURE_CROSS_NODE:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_CROSS_NODE, Data);

            Data->SharingTier = (D3D12_CROSS_NODE_SHARING_TIER)(Options.CrossNodeSharingTier > 3 ? 3 :
                                                                Options.CrossNodeSharingTier);
            Data->AtomicShaderInstructions = Options.CrossNodeSharingTier >= 4;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS5:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS5, Data);

            Data->SRVOnlyTiledResourceTier3 = Options.SRVOnlyTiledResourceTier3;
            Data->RenderPassesTier = (D3D12_RENDER_PASS_TIER)Options.RenderPassTier;
            Data->RaytracingTier = (D3D12_RAYTRACING_TIER)Options.RaytracingTier;
            return S_OK;
        }

        case D3D12_FEATURE_DISPLAYABLE:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_DISPLAYABLE, Data);

            Data->DisplayableTexture = Displayable;
            Data->SharedResourceCompatibilityTier = (D3D12_SHARED_RESOURCE_COMPATIBILITY_TIER)SharedTier;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS6:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS6, Data);

            Data->AdditionalShadingRatesSupported = Options.AdditionalShadingRatesSupported;
            Data->PerPrimitiveShadingRateSupportedWithViewportIndexing =
                Options.PerPrimitiveShadingRateSupportedWithViewportIndexing;
            Data->VariableShadingRateTier = (D3D12_VARIABLE_SHADING_RATE_TIER)Options.VariableShadingRateTier;
            Data->ShadingRateImageTileSize = Options.ShadingRateImageTileSize;
            Data->BackgroundProcessingSupported = BackgroundProcessing;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS7:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS7, Data);

            Data->MeshShaderTier = (D3D12_MESH_SHADER_TIER)Options.MeshShaderTier;
            Data->SamplerFeedbackTier = (D3D12_SAMPLER_FEEDBACK_TIER)Options.SamplerFeedbackTier;
            return S_OK;
        }

        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, Data);

            if (Data->NodeIndex >= ApiNodeCount)
                return E_INVALIDARG;
            hr = QuerySessionCaps();
            if (FAILED(hr))
                return hr;
            Data->Count = Sessions[Data->NodeIndex].TypeCount;
            return S_OK;
        }

        case D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES, Data);

            if (Data->NodeIndex >= ApiNodeCount)
                return E_INVALIDARG;
            hr = QuerySessionCaps();
            if (FAILED(hr))
                return hr;
            if (Data->Count != Sessions[Data->NodeIndex].TypeCount)
                return E_INVALIDARG;
            if (Data->Count != 0)
                memcpy(Data->pTypes, Sessions[Data->NodeIndex].Types, Data->Count * sizeof(GUID));
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS8:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS8, Data);

            Data->UnalignedBlockTexturesSupported = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS9:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS9, Data);

            Data->MeshShaderPipelineStatsSupported = Options.MeshShaderTier != 0;
            Data->MeshShaderSupportsFullRangeRenderTargetArrayIndex =
                Options.MeshShaderTier != 0 && Options.MeshShaderSupportsFullRangeRenderTargetArrayIndex;
            Data->AtomicInt64OnTypedResourceSupported = ShaderCaps.AtomicInt64OnTypedResource;
            Data->AtomicInt64OnGroupSharedSupported = ShaderCaps.AtomicInt64OnGroupShared;
            Data->DerivativesInMeshAndAmplificationShadersSupported = ShaderCaps.DerivativesInMeshAndAmplificationShaders;
            Data->WaveMMATier = (D3D12_WAVE_MMA_TIER)ShaderCaps.WaveMMATier;
            return S_OK;
        }

        case 38:
        {
            UINT* Data = static_cast<UINT*>(pFeatureSupportData);

            /* Wave matrix support for an input type and shape; the size is never checked, bad inputs clear the answer */
            if (Data[0] - 1 > 2 || Data[1] - 1 > 1 || Data[2] - 1 > 1)
            {
                ZeroMemory(&Data[3], 5 * sizeof(*Data));
                return E_INVALIDARG;
            }
            return Adapter->GetCaps(D3D12DDICAPS_TYPE_0103_WAVE_MMA, nullptr, Data, 32);
        }

        case D3D12_FEATURE_D3D12_OPTIONS10:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS10, Data);

            Data->VariableRateShadingSumCombinerSupported =
                (INT)Options.VariableShadingRateTier >= 2 && Options.VariableRateShadingSumCombinerSupported;
            Data->MeshShaderPerPrimitiveShadingRateSupported = (INT)Options.VariableShadingRateTier >= 2 &&
                Options.MeshShaderTier != 0 && Options.MeshShaderPerPrimitiveShadingRateSupported;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS11:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS11, Data);

            Data->AtomicInt64OnDescriptorHeapResourceSupported = ShaderCaps.AtomicInt64OnDescriptorHeapResource;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS12:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS12, Data);

            Data->MSPrimitivesPipelineStatisticIncludesCulledPrimitives =
                Options.MSPrimitivesPipelineStatisticIncludesCulledPrimitives ? D3D12_TRI_STATE_TRUE : D3D12_TRI_STATE_FALSE;
            Data->EnhancedBarriersSupported = Options.EnhancedBarriersSupported != FALSE;
            Data->RelaxedFormatCastingSupported = Options.EnhancedBarriersSupported && Casting.RelaxedFormatCastingSupported;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS13:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS13, Data);

            Data->UnrestrictedBufferTextureCopyPitchSupported = CopyAndViewport.UnrestrictedBufferTextureCopyPitchSupported != FALSE;
            Data->UnrestrictedVertexElementAlignmentSupported = CopyAndViewport.UnrestrictedVertexElementAlignmentSupported != FALSE;
            Data->InvertedViewportHeightFlipsYSupported = CopyAndViewport.InvertedViewportHeightFlipsYSupported != FALSE;
            Data->InvertedViewportDepthFlipsZSupported = CopyAndViewport.InvertedViewportDepthFlipsZSupported != FALSE;
            Data->TextureCopyBetweenDimensionsSupported = TRUE;
            Data->AlphaBlendFactorSupported = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS14:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS14, Data);

            Data->AdvancedTextureOpsSupported = TextureOps.AdvancedTextureOpsSupported != FALSE;
            Data->WriteableMSAATexturesSupported = TextureOps.WriteableMSAATexturesSupported != FALSE;
            Data->IndependentFrontAndBackStencilRefMaskSupported = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS15:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS15, Data);

            Data->TriangleFanSupported = TRUE;
            Data->DynamicIndexBufferStripCutSupported = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS16:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS16, Data);

            /* GPU upload heaps need the driver to reach its memory and the kernel fix that makes it safe */
            Data->DynamicDepthBiasSupported = TRUE;
            Data->GPUUploadHeapSupported = (L1Access.L1MemoryFullyCpuAccessible || Node0.Uma) && GpuUploadOs;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS17:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS17, Data);

            Data->NonNormalizedCoordinateSamplersSupported = TRUE;
            Data->ManualWriteTrackingResourceSupported = FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS18:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS18, Data);

            Data->RenderPassesValid = TRUE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS19:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS19, Data);

            ZeroMemory(Data, sizeof(*Data));
            Data->RasterizerDesc2Supported = TRUE;
            if (Level >= D3D_FEATURE_LEVEL_11_0)
            {
                Data->MismatchingOutputDimensionsSupported = TRUE;
                Data->SupportedSampleCountsWithNoOutputs = Level >= D3D_FEATURE_LEVEL_11_1 ?
                                                           (HeapLimits.SupportedSampleCountsWithNoOutputs & 0x1F) : 1;
                Data->PointSamplingAddressesNeverRoundUp = TRUE;
                Data->NarrowQuadrilateralLinesSupported = TRUE;
                Data->AnisoFilterWithPointMipSupported = TRUE;
                Data->MaxSamplerDescriptorHeapSize = HeapLimits.MaxSamplerDescriptorHeapSize;
                Data->MaxSamplerDescriptorHeapSizeWithStaticSamplers =
                    HeapLimits.MaxSamplerDescriptorHeapSizeWithStaticSamplers;
            }
            Data->MaxViewDescriptorHeapSize = HeapLimits.MaxViewDescriptorHeapSize;
            Data->ComputeOnlyCustomHeapSupported = ComputeHeap.ComputeOnlyCustomHeapSupported != FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_OPTIONS20:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS20, Data);

            Data->ComputeOnlyWriteWatchSupported = KmdVersion >= 3000;
            Data->RecreateAtTier = D3D12_RECREATE_AT_TIER_NOT_SUPPORTED;
            return S_OK;
        }

        case D3D12_FEATURE_PREDICATION:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_PREDICATION, Data);

            return RxDriverBool(*Adapter, Level, D3D12DDI_FEATURE_D3D12_PREDICATION_106, &Data->Supported);
        }

        case D3D12_FEATURE_PLACED_RESOURCE_SUPPORT_INFO:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO, Data);

            return CheckPlacedSupport(Data);
        }

        case D3D12_FEATURE_HARDWARE_COPY:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_HARDWARE_COPY, Data);

            return RxDriverBool(*Adapter, Level, D3D12DDI_FEATURE_HARDWARE_COPY_106, &Data->Supported);
        }

        case D3D12_FEATURE_D3D12_OPTIONS21:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_D3D12_OPTIONS21, Data);

            Data->WorkGraphsTier = (INT)WorkGraphs.WorkGraphsTier < 10 ? D3D12_WORK_GRAPHS_TIER_NOT_SUPPORTED :
                                   (D3D12_WORK_GRAPHS_TIER)WorkGraphs.WorkGraphsTier;
            Data->ExecuteIndirectTier = (D3D12_EXECUTE_INDIRECT_TIER)ExecuteIndirect.ExecuteIndirectTier;
            Data->SampleCmpGradientAndBiasSupported =
                HighestShaderModel >= D3D_SHADER_MODEL_6_8 && Model68.SampleCmpGradientAndBiasSupported;
            Data->ExtendedCommandInfoSupported = HighestShaderModel >= D3D_SHADER_MODEL_6_8 &&
                                                 (Level >= D3D_FEATURE_LEVEL_12_2 || Model68.ExtendedCommandInfoSupported);
            return S_OK;
        }

        case D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_TIGHT_ALIGNMENT, Data);

            Data->SupportTier = D3D12_TIGHT_ALIGNMENT_TIER_NOT_SUPPORTED;
            return S_OK;
        }

        case 55:
        {
            UINT* Data = static_cast<UINT*>(pFeatureSupportData);

            /* No layout of this kind is offered, so the two answers are zero */
            if (FeatureSupportDataSize != 24)
                return E_INVALIDARG;
            Data[3] = 0;
            Data[4] = 0;
            return S_OK;
        }

        case D3D12_FEATURE_APPLICATION_SPECIFIC_DRIVER_STATE:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_APPLICATION_SPECIFIC_DRIVER_STATE, Data);

            Data->Supported = FALSE;
            return S_OK;
        }

        case D3D12_FEATURE_BYTECODE_BYPASS_HASH_SUPPORTED:
        {
            RX_FEATURE_DATA(D3D12_FEATURE_DATA_BYTECODE_BYPASS_HASH_SUPPORTED, Data);

            Data->Supported = TRUE;
            return S_OK;
        }

        case 60:
        {
            UINT* Data = static_cast<UINT*>(pFeatureSupportData);

            /* Compute queues the driver groups with each direct queue */
            if (FeatureSupportDataSize != sizeof(*Data))
                return E_INVALIDARG;
            *Data = Scheduling.ComputeQueuesPer3DQueue;
            return S_OK;
        }

        default:
            RX_TRACE(L"CheckFeatureSupport feature %d not answered", Feature);
            return E_INVALIDARG;
    }
}
