/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device, the user mode driver behind it, and what every device child shares
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"
#include "privatedata.hpp"

#include <malloc.h>

class RxDevice12;
class RxSchedulingGroup;
class RxRootSignature;
class RxPipelineState;
class RxPipelinePart;
struct RxFormatContext12;

/* The one DDI revision this runtime drives: release 8, build 118 */
constexpr UINT RxDdiInterface12 = D3D12DDI_INTERFACE_VERSION_R8;
constexpr UINT RxDdiBuild12 = 118;

/* Driver private memory sized by a CalcPrivate*Size call, freed with its owner */
class RxDriverMemory12
{
public:
    RxDriverMemory12() = default;

    ~RxDriverMemory12()
    {
        if (Memory != nullptr)
            _aligned_free(Memory);
    }

    RxDriverMemory12(const RxDriverMemory12&) = delete;
    RxDriverMemory12& operator=(const RxDriverMemory12&) = delete;

    _Ret_maybenull_
    PVOID
    Allocate(
        _In_ SIZE_T Size)
    {
        Memory = _aligned_malloc(Size ? Size : 1, 16);
        if (Memory != nullptr)
            ZeroMemory(Memory, Size ? Size : 1);
        return Memory;
    }

    PVOID
    Get() const
    {
        return Memory;
    }

private:
    PVOID Memory = nullptr;
};

/* What the driver gets as a runtime resource handle: the kernel resource its allocations hang off */
struct RxRuntimeResource12
{
    D3DKMT_HANDLE KmResource;
};

/* What the driver gets as a runtime paging queue handle */
struct RxPagingQueue12
{
    D3DKMT_HANDLE Handle;
    D3DKMT_HANDLE SyncObject;
    VOID* FenceCpuVa;
};

/* The most GPU nodes the runtime takes from a driver */
constexpr UINT RxNodeLimit12 = 32;

/* One physical node as the driver describes its memory */
struct RxNodeCaps12
{
    BOOLEAN Uma;
    BOOLEAN CacheCoherent;
    BOOLEAN IoCoherent;
    BOOLEAN IsolatedMmu;
    D3D12_HEAP_SERIALIZATION_TIER HeapSerializationTier;
};

/* Protected session support of one node, read the first time it is asked for */
struct RxSessionCaps12
{
    UINT Support;
    UINT TypeCount;
    GUID* Types;
};

/* RxDevice12::Get sharing: hand back the adapter's singleton, or record a new device as it */
constexpr UINT RxDeviceReuseSingleton = 0x1;
constexpr UINT RxDeviceStoreSingleton = 0x2;

/* Device answers resource validation reads, gathered once the device is up */
struct RxValidationCaps12
{
    UINT TiledTier;
    UINT ResourceHeapTier;
    UINT RaytracingTier;
    UINT ShadingRateTier;
    UINT SamplerFeedbackTier;
    UINT SamplePositionsTier;
    BOOLEAN StandardSwizzle;
    BOOLEAN CrossAdapterRowMajor;
    BOOLEAN SrvOnlyTiledTier3;
    BOOLEAN Msaa64KbAligned;
    BOOLEAN UnalignedBlocks;
    BOOLEAN WriteableMsaa;
    BOOLEAN GpuUploadHeaps;
    BOOLEAN ManualWriteTracking;
    BOOLEAN ComputeWriteWatch;
};

/* What the driver hands back through SetCommandListDDITableCb: the table a list should use */
struct RxListTableToken12
{
    const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114* Table;
};

/* The kernel adapter and the Direct3D 12 user mode driver that drives it */
class RxAdapter12
{
public:
    RxAdapter12() = default;
    ~RxAdapter12();

    RxAdapter12(const RxAdapter12&) = delete;
    RxAdapter12& operator=(const RxAdapter12&) = delete;

    HRESULT
    Open(
        _In_ const LUID& AdapterLuid);

    /* One pfnGetCaps answer */
    HRESULT
    GetCaps(
        _In_ D3D12DDICAPS_TYPE Type,
        _In_opt_ VOID* Info,
        _Out_writes_bytes_(Size) VOID* Data,
        _In_ UINT Size) const;

    HRESULT
    QueryKernel(
        _In_ KMTQUERYADAPTERINFOTYPE Type,
        _Inout_updates_bytes_(Size) VOID* Data,
        _In_ UINT Size) const;

    const LUID&
    GetLuid() const
    {
        return Luid;
    }

    D3DKMT_HANDLE
    GetKmtHandle() const
    {
        return KmtAdapter;
    }

    D3D12DDI_HADAPTER
    GetDriverHandle() const
    {
        return DriverAdapter;
    }

    const D3D12DDI_ADAPTERFUNCS_0109&
    GetFunctions() const
    {
        return Functions;
    }

    /* The driver's own entry for the negotiated revision */
    UINT64
    GetVersion() const
    {
        return Version;
    }

    D3D_FEATURE_LEVEL
    GetMaxFeatureLevel() const
    {
        return MaxFeatureLevel;
    }

private:
    HRESULT
    NegotiateVersion();

    static
    HRESULT
    APIENTRY
    QueryAdapterInfoCb(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ const D3DDDICB_QUERYADAPTERINFO* Query);

    static
    HRESULT
    APIENTRY
    QueryAdapterInfoCb2(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ const D3DDDICB_QUERYADAPTERINFO2* Query);

    static
    HRESULT
    APIENTRY
    GetMultisampleMethodListCb(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ D3DDDICB_GETMULTISAMPLEMETHODLIST* Query);

    const KmtThunks* Kmt = nullptr;
    D3DKMT_HANDLE KmtAdapter = 0;
    LUID Luid = {};
    HMODULE Driver = NULL;
    D3DDDI_ADAPTERCALLBACKS Callbacks = {};
    D3D12DDI_ADAPTERFUNCS_0109 Functions = {};
    D3D12DDI_HADAPTER DriverAdapter = {};
    UINT64 Version = 0;
    D3D_FEATURE_LEVEL MaxFeatureLevel = (D3D_FEATURE_LEVEL)0;
};

class RxDevice12 final : public ID3D12Device14, public RxKernelDevice
{
public:
    /* The device for an adapter; Sharing says whether the adapter's singleton is handed back or becomes this one */
    static
    HRESULT
    Get(
        _In_ const LUID& AdapterLuid,
        _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
        _In_ UINT Sharing,
        _Outptr_ RxDevice12** Device);

    /* Whether an adapter could make a device at a level, without making one */
    static
    HRESULT
    CheckLevel(
        _In_ const LUID& AdapterLuid,
        _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel);

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

#include "device.inl"

    /* For the shared kernel callbacks */
    D3DKMT_HANDLE
    KernelDevice() const override
    {
        return KmDevice;
    }

    D3DKMT_HANDLE
    KernelAdapter() const override
    {
        return Adapter->GetKmtHandle();
    }

    D3DKMT_CLIENTHINT
    KernelClientHint() const override
    {
        return D3DKMT_CLIENTHINT_DX12;
    }

    D3DKMT_HANDLE
    KernelResourceOf(
        _In_opt_ HANDLE RuntimeResource) override
    {
        return RuntimeResource ? static_cast<RxRuntimeResource12*>(RuntimeResource)->KmResource : 0;
    }

    VOID
    RetireKernelResource(
        _In_ HANDLE RuntimeResource) override
    {
        static_cast<RxRuntimeResource12*>(RuntimeResource)->KmResource = 0;
    }

    D3D12DDI_HDEVICE
    Handle() const
    {
        return DriverDevice;
    }

    const D3D12DDI_DEVICE_FUNCS_CORE_0116&
    Ddi() const
    {
        return CoreFunctions;
    }

    const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001&
    QueueDdi() const
    {
        return QueueFunctions;
    }

    /* The table a command list dispatches through once the driver names it */
    const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114*
    ListTable(
        _In_ D3D12DDI_HRTTABLE Table) const
    {
        return static_cast<const RxListTableToken12*>(Table.handle)->Table;
    }

    const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114&
    DefaultListTable() const
    {
        return ListFunctions[0];
    }

    RxAdapter12&
    GetAdapter() const
    {
        return *Adapter;
    }

    /* The error the driver raised during the last DDI call on this thread, cleared by reading it */
    static
    HRESULT
    TakeDriverError();

    static
    VOID
    NoteDriverError(
        _In_ HRESULT Error);

    /* The driver reported the device lost */
    VOID
    MarkRemoved(
        _In_ HRESULT Reason);

    /* Makes a new pageable resident through the driver, as creation does on Windows */
    HRESULT
    MakePageableResident(
        _In_ VOID* DriverHandle,
        _In_ D3D12DDI_HANDLETYPE Type);

    UINT64
    NextCommandListId()
    {
        return (UINT64)InterlockedIncrement64(&CommandListCounter);
    }

    /* The pipeline a list starts recording with when the application names none */
    RxPipelineState*
    DefaultPipeline(
        _In_ BOOLEAN Compute) const
    {
        return Compute ? DefaultCompute : DefaultGraphics;
    }

    /* Whether protected sessions exist on an API node; the caps are read on first use */
    HRESULT
    SessionSupport(
        _In_ UINT NodeIndex,
        _Out_ BOOLEAN* Supported);

    SIZE_T
    DriverRtvIncrement() const
    {
        return RtvDriverSize;
    }

    const D3D12DDI_D3D12_OPTIONS_DATA_0089&
    GetOptions() const
    {
        return Options;
    }

    const D3D12DDI_SHADER_CAPS_0084&
    GetShaderCaps() const
    {
        return ShaderCaps;
    }

    D3D12DDI_EXECUTE_INDIRECT_TIER
    GetExecuteIndirectTier() const
    {
        return ExecuteIndirect.ExecuteIndirectTier;
    }

    /* The highest released shader model the driver lists, in API form */
    D3D_SHADER_MODEL
    GetHighestShaderModel() const
    {
        return HighestShaderModel;
    }

    /* 64KB standard swizzle textures without sharing across adapters */
    BOOLEAN
    HasStandardSwizzle() const
    {
        return TextureLayout.Supports64KStandardSwizzle != FALSE;
    }

    BOOLEAN
    IsTileBased() const
    {
        return Architecture.TileBasedDeferredRenderer != FALSE;
    }

    UINT
    GetVaBitsPerResource() const
    {
        return VaBitsPerResource;
    }

    UINT
    GetVaBitsPerProcess() const
    {
        return VaBitsPerProcess;
    }

    BOOLEAN
    IsUma() const
    {
        return Nodes[ApiToPhysical[0]].Uma;
    }

    BOOLEAN
    IsCacheCoherentUma() const
    {
        return Nodes[ApiToPhysical[0]].Uma && Nodes[ApiToPhysical[0]].CacheCoherent;
    }

    /* The most descriptors a shader visible heap of a type may hold, 0 for no limit */
    UINT
    GetShaderVisibleHeapLimit(
        _In_ D3D12_DESCRIPTOR_HEAP_TYPE Type) const
    {
        if (Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
            return HeapLimits.MaxSamplerDescriptorHeapSize;
        if (HasHeapLimits)
            return HeapLimits.MaxViewDescriptorHeapSize;
        return Options.ResourceBindingTier < D3D12DDI_RESOURCE_BINDING_TIER_3 ? D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_2 : 0;
    }

    /* The checks before any child object is made */
    HRESULT
    CheckUsable() const
    {
        return RemovedReason;
    }

    /* Queue flags the driver lets global realtime queues use */
    UINT
    GetRealtimeQueueFlags() const
    {
        return RealtimeQueueFlags;
    }

    /* A group with room for a queue of this type, made when none has room; holds one use */
    HRESULT
    JoinSchedulingGroup(
        _In_ D3D12_COMMAND_LIST_TYPE Type,
        _In_ UINT NodeMask,
        _In_ const GUID& Creator,
        _Outptr_result_maybenull_ RxSchedulingGroup** Group);

    VOID
    LeaveSchedulingGroup(
        _In_ RxSchedulingGroup* Group,
        _In_ D3D12_COMMAND_LIST_TYPE Type);

    const RxValidationCaps12&
    GetValidationCaps() const
    {
        return ValidationCaps;
    }

    /* Vertex elements may then start at any byte */
    BOOLEAN
    UnrestrictedElementAlignment() const
    {
        return CopyAndViewport.UnrestrictedVertexElementAlignmentSupported != FALSE;
    }

    /* Buffer footprints of copies may then start and step at any multiple of a texel */
    BOOLEAN
    UnrestrictedCopyPitch() const
    {
        return CopyAndViewport.UnrestrictedBufferTextureCopyPitchSupported != FALSE;
    }

    D3D_FEATURE_LEVEL
    GetFeatureLevel() const
    {
        return Level;
    }

    /* Whether the memory of an API node is the system's own */
    BOOLEAN
    IsNodeUma(
        _In_ UINT NodeIndex) const
    {
        return NodeIndex < ApiNodeCount && Nodes[ApiToPhysical[NodeIndex]].Uma;
    }

    /* Compute queues the driver puts beside one direct queue, 0 when it groups nothing */
    UINT
    GetComputeQueuesPerGroup() const
    {
        return Scheduling.ComputeQueuesPer3DQueue;
    }

    /* An API node mask in the driver's numbering; Single demands exactly one node */
    HRESULT
    TranslateNodeMask(
        _In_ UINT Mask,
        _In_ BOOLEAN Single,
        _Out_ UINT* Translated) const;

    /* Every API node, as internal objects use */
    UINT
    GetAllNodesMask() const
    {
        return ApiNodeCount >= 32 ? ~0u : (1u << ApiNodeCount) - 1;
    }

    UINT
    GetDdiBuild() const
    {
        return DdiBuild;
    }

    /* A root signature already made from this RTS0 part for these nodes, with a reference added */
    _Ret_maybenull_
    RxRootSignature*
    FindRootSignature(
        _In_reads_bytes_(Size) const BYTE* Part,
        _In_ UINT Size,
        _In_ UINT NodeMask);

    VOID
    RememberRootSignature(
        _In_ RxRootSignature* Signature);

    VOID
    ForgetRootSignature(
        _In_ RxRootSignature* Signature);

private:
    friend class RxPipelinePart;

    RxDevice12() = default;
    ~RxDevice12();

    HRESULT
    Initialize(
        _In_ RxAdapter12* OpenedAdapter);

    HRESULT
    QueryDeviceCaps();

    HRESULT
    QueryShaderModels();

    VOID
    ClampOptions();

    HRESULT
    FillTables();

    HRESULT
    EnableExtendedFeatures();

    HRESULT
    QueryLateCaps();

    HRESULT
    ApplyShaderModelRules();

    HRESULT
    CheckFormatFamilies();

    HRESULT
    QueryNodes();

    HRESULT
    QueryHeapCaps();

    HRESULT
    QueryVirtualAddressBits();

    HRESULT
    CreateInternalObjects();

    /* The empty root signature and the graphics and compute pipelines every device keeps */
    HRESULT
    CreateDefaults();

    HRESULT
    QueryValidationCaps();

    HRESULT
    QuerySessionCaps();

    /* The physical node an API node index answers for */
    HRESULT
    NodeForCaps(
        _In_ UINT NodeIndex,
        _Out_ UINT* Physical) const;

    HRESULT
    CheckPlacedSupport(
        _Inout_ D3D12_FEATURE_DATA_PLACED_RESOURCE_SUPPORT_INFO* Data);

    UINT
    ShaderCacheSupport() const;

    /* The answers CheckFeatureSupport gives for formats depend on these */
    RxFormatContext12
    FormatContext();

    LONG RefCount = 1;
    RxAdapter12* Adapter = nullptr;
    D3DKMT_HANDLE KmDevice = 0;
    RxDriverMemory12 DriverMemory;
    D3D12DDI_HDEVICE DriverDevice = {};
    BOOLEAN IsDriverDeviceCreated = FALSE;
    UINT DdiBuild = 0;
    D3D_FEATURE_LEVEL Level = (D3D_FEATURE_LEVEL)0;
    D3D12DDI_DEVICE_FUNCS_CORE_0116 CoreFunctions = {};
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 ListFunctions[2] = {};
    RxListTableToken12 ListTokens[2] = {};
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 QueueFunctions = {};
    D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 ExtendedFunctions = {};
    D3D12DDI_DEVICE_FUNCS_CONTENT_PROTECTION_RESOURCES_0074 SessionFunctions = {};
    UINT VideoVersion = 0;
    UINT SessionVersion = 0;
    BOOLEAN TypedLoadMinimal = FALSE;
    UINT PreviewShaderModels = 0;
    D3D12DDI_SHADER_CAPS_0084 ShaderCaps = {};
    D3D12DDI_D3D12_OPTIONS_DATA_0089 Options = {};
    D3D12DDI_OPTIONS_DATA_0090 Casting = {};
    D3D12DDI_OPTIONS_DATA_0091 CopyAndViewport = {};
    D3D12DDI_OPTIONS_DATA_0093 TextureOps = {};
    D3D12DDI_OPTIONS_DATA_0098 L1Access = {};
    D3D12DDI_OPTIONS_DATA_0101 ComputeHeap = {};
    D3D12DDI_OPTIONS_DATA_0102 HeapLimits = {};
    BOOLEAN HasHeapLimits = FALSE;
    D3D12DDI_OPTIONS_DATA_0103 WorkGraphs = {};
    D3D12DDI_OPTIONS_DATA_0109 RecreateAt = {};
    D3D12DDI_OPTIONS_DATA_0110 ExecuteIndirect = {};
    D3D12DDI_OPTIONS_DATA_0118 Reordering = {};
    D3D12DDI_SHADER_MODEL_6_8_OPTIONS_0110 Model68 = {};
    D3D12DDI_ARCHITECTURE_INFO_DATA Architecture = {};
    D3D12DDICAPS_HARDWARE_SCHEDULING_CAPS_0050 Scheduling = {};
    D3D12DDI_TEXTURE_LAYOUT_CAPS_0026 TextureLayout = {};
    D3D12DDI_ROW_MAJOR_LAYOUT_CAPS RowMajorLayouts[3] = {};
    D3D_SHADER_MODEL HighestShaderModel = D3D_SHADER_MODEL_5_1;
    UINT VaBitsPerResource = 0;
    UINT VaBitsPerProcess = 0;
    UINT RtvDriverSize = 0;
    UINT KmdVersion = 0;
    UINT RealtimeQueueFlags = 0;
    BOOLEAN GlobalPriorityAvailable = FALSE;
    BOOLEAN GpuUploadOs = FALSE;
    BOOLEAN Displayable = FALSE;
    BOOLEAN BatchedMarkers = FALSE;
    BOOLEAN BackgroundProcessing = FALSE;
    UINT PhysicalNodeCount = 0;
    UINT ApiNodeCount = 0;
    UINT ApiToPhysical[RxNodeLimit12] = {};
    RxNodeCaps12 Nodes[RxNodeLimit12] = {};
    SRWLOCK SessionLock = SRWLOCK_INIT;
    BOOLEAN SessionsQueried = FALSE;
    RxSessionCaps12 Sessions[RxNodeLimit12] = {};
    RxDriverMemory12 PoolMemory;
    D3D12DDI_HCOMMANDPOOL_0040 CommandPool = {};
    RxPagingQueue12 PagingQueue = {};
    volatile LONG64 CommandListCounter = 0;
    HRESULT RemovedReason = S_OK;
    SRWLOCK GroupLock = SRWLOCK_INIT;
    RxSchedulingGroup* OpenGroups = nullptr;
    RxValidationCaps12 ValidationCaps = {};
    SRWLOCK RootCacheLock = SRWLOCK_INIT;
    RxRootSignature* RootCache = nullptr;
    SRWLOCK PartCacheLock = SRWLOCK_INIT;
    RxPipelinePart* PartCache = nullptr;
    RxRootSignature* DefaultRoot = nullptr;
    RxPipelineState* DefaultGraphics = nullptr;
    RxPipelineState* DefaultCompute = nullptr;
    RxPrivateDataStore PrivateData;
};

/* Lifetime, device link and private data shared by every device child */
template <typename Interface>
class RxDeviceChild12 : public Interface
{
public:
    explicit RxDeviceChild12(
        _In_ RxDevice12& Owner)
        : Device(Owner)
    {
        Owner.AddRef();
    }

    /* Objects the device makes for itself do not keep it alive */
    RxDeviceChild12(
        _In_ RxDevice12& Owner,
        _In_ BOOLEAN Internal)
        : Device(Owner),
          HoldsDevice(!Internal)
    {
        if (HoldsDevice)
            Owner.AddRef();
    }

    virtual ~RxDeviceChild12()
    {
        if (HoldsDevice)
            Device.Release();
    }

    /* A reference taken only while the object is still alive, for caches that hold none */
    BOOLEAN
    TryAddRef()
    {
        LONG Count = Uses;

        while (Count != 0)
        {
            LONG Seen = InterlockedCompareExchange(&Uses, Count + 1, Count);

            if (Seen == Count)
            {
                AddRef();
                ReleaseUse();
                return TRUE;
            }
            Count = Seen;
        }
        return FALSE;
    }

    /* Application references together hold one use; the object lives while any use is left */
    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        LONG Count = InterlockedIncrement(&RefCount);

        if (Count == 1)
            AddUse();
        return Count;
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        LONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            ReleaseUse();
        return Count;
    }

    /* What other runtime objects hold, which applications do not see in the reference count */
    VOID
    AddUse()
    {
        InterlockedIncrement(&Uses);
    }

    VOID
    ReleaseUse()
    {
        if (InterlockedDecrement(&Uses) == 0)
            delete this;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID guid,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void* pData) override
    {
        return PrivateData.Get(guid, pDataSize, pData);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateData(
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void* pData) override
    {
        return PrivateData.Set(guid, DataSize, pData, nullptr);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID guid,
        _In_opt_ const IUnknown* pData) override
    {
        return PrivateData.Set(guid, 0, nullptr, pData);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetName(
        _In_z_ LPCWSTR Name) override
    {
        if (Name == nullptr)
            return PrivateData.Set(WKPDID_D3DDebugObjectNameW, 0, nullptr, nullptr);

        return PrivateData.Set(WKPDID_D3DDebugObjectNameW, (UINT)((wcslen(Name) + 1) * sizeof(WCHAR)), Name, nullptr);
    }

    HRESULT
    STDMETHODCALLTYPE
    GetDevice(
        _In_ REFIID riid,
        _COM_Outptr_opt_ void** ppvDevice) override
    {
        return Device.QueryInterface(riid, ppvDevice);
    }

    RxDevice12&
    GetOwner() const
    {
        return Device;
    }

    /* What QueryInterface hands out for IUnknown: an identity apart from the API interfaces, as on Windows */
    IUnknown*
    Identity()
    {
        return &Unknown;
    }

    /* The base interfaces some kinds hand out apart from the interface creation returned */
    ID3D12Pageable*
    ChildFace()
    {
        return &Face;
    }

protected:
    RxDevice12& Device;
    RxPrivateDataStore PrivateData;

private:
    class RxIdentity final : public IUnknown
    {
    public:
        explicit RxIdentity(
            _In_ RxDeviceChild12* Owner)
            : Object(Owner)
        {
        }

        HRESULT
        STDMETHODCALLTYPE
        QueryInterface(
            _In_ REFIID riid,
            _COM_Outptr_ void** ppvObject) override
        {
            return Object->QueryInterface(riid, ppvObject);
        }

        ULONG
        STDMETHODCALLTYPE
        AddRef(VOID) override
        {
            return Object->AddRef();
        }

        ULONG
        STDMETHODCALLTYPE
        Release(VOID) override
        {
            return Object->Release();
        }

    private:
        RxDeviceChild12* Object;
    };

    class RxChildFace final : public ID3D12Pageable
    {
    public:
        explicit RxChildFace(
            _In_ RxDeviceChild12* Owner)
            : Object(Owner)
        {
        }

        HRESULT
        STDMETHODCALLTYPE
        QueryInterface(
            _In_ REFIID riid,
            _COM_Outptr_ void** ppvObject) override
        {
            return Object->QueryInterface(riid, ppvObject);
        }

        ULONG
        STDMETHODCALLTYPE
        AddRef(VOID) override
        {
            return Object->AddRef();
        }

        ULONG
        STDMETHODCALLTYPE
        Release(VOID) override
        {
            return Object->Release();
        }

        HRESULT
        STDMETHODCALLTYPE
        GetPrivateData(
            _In_ REFGUID guid,
            _Inout_ UINT* pDataSize,
            _Out_writes_bytes_opt_(*pDataSize) void* pData) override
        {
            return Object->GetPrivateData(guid, pDataSize, pData);
        }

        HRESULT
        STDMETHODCALLTYPE
        SetPrivateData(
            _In_ REFGUID guid,
            _In_ UINT DataSize,
            _In_reads_bytes_opt_(DataSize) const void* pData) override
        {
            return Object->SetPrivateData(guid, DataSize, pData);
        }

        HRESULT
        STDMETHODCALLTYPE
        SetPrivateDataInterface(
            _In_ REFGUID guid,
            _In_opt_ const IUnknown* pData) override
        {
            return Object->SetPrivateDataInterface(guid, pData);
        }

        HRESULT
        STDMETHODCALLTYPE
        SetName(
            _In_z_ LPCWSTR Name) override
        {
            return Object->SetName(Name);
        }

        HRESULT
        STDMETHODCALLTYPE
        GetDevice(
            _In_ REFIID riid,
            _COM_Outptr_opt_ void** ppvDevice) override
        {
            return Object->GetDevice(riid, ppvDevice);
        }

    private:
        RxDeviceChild12* Object;
    };

    BOOLEAN HoldsDevice = TRUE;
    LONG RefCount = 1;
    LONG Uses = 1;
    RxIdentity Unknown{ this };
    RxChildFace Face{ this };
};

/* Hands back the interface asked for, as creation methods do: NULL output only checks */
template <typename Object>
HRESULT
RxHandOut12(
    _In_ Object* Created,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject)
{
    HRESULT hr;

    if (ppvObject == nullptr)
    {
        Created->Release();
        return S_FALSE;
    }

    hr = Created->QueryInterface(riid, ppvObject);
    Created->Release();
    return hr;
}

/* The ReactX object behind an API pointer, or nullptr for one from elsewhere */
template <typename Object>
_Ret_maybenull_
Object*
RxUnwrap12(
    _In_opt_ IUnknown* Unknown)
{
    Object* Found = nullptr;

    if (Unknown == nullptr || FAILED(Unknown->QueryInterface(Object::PrivateId, reinterpret_cast<void**>(&Found))))
        return nullptr;

    /* The lookup does not keep a reference */
    Found->Release();
    return Found;
}

/* Callback tables handed to the driver at device creation */
const D3DDDI_DEVICECALLBACKS*
RxGetKernelCallbacks12(VOID);

const D3D12DDI_CORELAYER_DEVICECALLBACKS_0116*
RxGetCoreLayerCallbacks12(VOID);

/* D3D12_COMMAND_LIST_TYPE as the queue flags the driver wants, 0 for an unknown type */
D3D12DDI_COMMAND_QUEUE_FLAGS
RxQueueFlagsOf(
    _In_ D3D12_COMMAND_LIST_TYPE Type);
