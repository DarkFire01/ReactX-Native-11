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
    /* The device for an adapter, made the first time and shared after that */
    static
    HRESULT
    Get(
        _In_ const LUID& AdapterLuid,
        _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
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
        return static_cast<const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114*>(Table.handle);
    }

    const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114&
    DefaultListTable() const
    {
        return ListFunctions;
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

    SIZE_T
    DriverRtvIncrement() const
    {
        return DescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
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

    /* The highest released shader model the driver lists, in API form */
    D3D_SHADER_MODEL
    GetHighestShaderModel() const
    {
        return HighestShaderModel;
    }

    BOOLEAN
    IsTileBased() const
    {
        return TileBased;
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
        return MemoryArchitecture.UMA != FALSE;
    }

    BOOLEAN
    IsCacheCoherentUma() const
    {
        return MemoryArchitecture.UMA && MemoryArchitecture.CacheCoherent;
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

private:
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
    QueryVirtualAddressBits();

    HRESULT
    FillTables();

    LONG RefCount = 1;
    RxAdapter12* Adapter = nullptr;
    D3DKMT_HANDLE KmDevice = 0;
    RxDriverMemory12 DriverMemory;
    D3D12DDI_HDEVICE DriverDevice = {};
    BOOLEAN IsDriverDeviceCreated = FALSE;
    D3D12DDI_DEVICE_FUNCS_CORE_0116 CoreFunctions = {};
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 ListFunctions = {};
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 QueueFunctions = {};
    D3D12DDI_D3D12_OPTIONS_DATA_0089 Options = {};
    D3D12DDI_MEMORY_ARCHITECTURE_CAPS_0041 MemoryArchitecture = {};
    D3D12DDI_SHADER_CAPS_0084 ShaderCaps = {};
    D3D12DDI_OPTIONS_DATA_0102 HeapLimits = {};
    BOOLEAN HasHeapLimits = FALSE;
    D3D_SHADER_MODEL HighestShaderModel = D3D_SHADER_MODEL_5_1;
    BOOLEAN TileBased = FALSE;
    UINT VaBitsPerResource = 0;
    UINT VaBitsPerProcess = 0;
    UINT DescriptorSizes[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};
    RxPagingQueue12 PagingQueue = {};
    volatile LONG64 CommandListCounter = 0;
    HRESULT RemovedReason = S_OK;
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

    virtual ~RxDeviceChild12()
    {
        Device.Release();
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            delete this;
        return Count;
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

protected:
    RxDevice12& Device;
    RxPrivateDataStore PrivateData;

private:
    LONG RefCount = 1;
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
