/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device children: queues, allocators, command lists, fences, descriptor heaps, resources
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"
#include "rootsignature.hpp"

class RxCommandAllocator;
class RxResource;

/* Kernel contexts and hardware queues one API queue can own */
constexpr UINT RxMaxQueueEngines12 = 8;

/* Hardware scheduled contexts one direct queue and a few compute queues share */
class RxSchedulingGroup
{
public:
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ UINT NodeMask,
        _In_ const GUID& Creator,
        _Outptr_ RxSchedulingGroup** Group);

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

    BOOLEAN
    IsFor(
        _In_ UINT Node,
        _In_ const GUID& Creator) const
    {
        return NodeIndex == Node && IsEqualGUID(CreatorId, Creator);
    }

    /* Whether a queue of this type still fits; the device lock is held */
    BOOLEAN
    HasRoomFor(
        _In_ D3D12_COMMAND_LIST_TYPE Type) const;

    /* Counts a queue in; TRUE when the group had none before */
    BOOLEAN
    Attach(
        _In_ D3D12_COMMAND_LIST_TYPE Type);

    /* Counts a queue out; TRUE when the group has none left */
    BOOLEAN
    Detach(
        _In_ D3D12_COMMAND_LIST_TYPE Type);

    D3D12DDI_HSCHEDULINGGROUP_0050
    GetDriverHandle() const
    {
        return DriverGroup;
    }

    BOOLEAN
    OwnsContext(
        _In_ D3DKMT_HANDLE Context) const;

    static
    HRESULT
    APIENTRY
    CreateContextCb(
        _In_ D3D12DDI_HRTSCHEDULINGGROUP_0050 hRTSchedulingGroup,
        _Inout_ D3DDDICB_CREATECONTEXT* Args);

    static
    HRESULT
    APIENTRY
    CreateContextVirtualCb(
        _In_ D3D12DDI_HRTSCHEDULINGGROUP_0050 hRTSchedulingGroup,
        _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args);

    /* Link in the device's list of groups open to new queues */
    RxSchedulingGroup* Next = nullptr;

private:
    RxSchedulingGroup(
        _In_ RxDevice12& Owner,
        _In_ UINT NodeMask,
        _In_ const GUID& Creator);

    ~RxSchedulingGroup();

    HRESULT
    Initialize();

    BOOLEAN
    CanMakeContext(
        _In_ D3DDDI_CREATECONTEXTFLAGS Flags) const;

    RxDevice12& Device;
    LONG Uses = 1;
    UINT Mask;
    UINT NodeIndex;
    GUID CreatorId;
    UINT DirectQueues = 0;
    UINT ComputeQueues = 0;
    RxDriverMemory12 Memory;
    D3D12DDI_HSCHEDULINGGROUP_0050 DriverGroup = {};
    BOOLEAN IsDriverGroupCreated = FALSE;
    DWORD CreatingThread = 0;
    D3DKMT_HANDLE Contexts[RxMaxQueueEngines12] = {};
    UINT ContextCount = 0;
};

class RxCommandQueue final : public RxDeviceChild12<ID3D12CommandQueue>
{
public:
    static constexpr GUID PrivateId = {0x9d3c1f62, 0x47a8, 0x4e0b, {0x8b, 0x15, 0x6e, 0x2a, 0x90, 0x4c, 0xd7, 0x31}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_COMMAND_QUEUE_DESC& Desc,
        _In_ const GUID& Creator,
        _Outptr_ RxCommandQueue** Queue);

    /* The checks a queue description passes before anything is made */
    static
    HRESULT
    Validate(
        _In_ const D3D12_COMMAND_QUEUE_DESC& Desc);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "queue.inl"

    /* What the driver asks of the runtime while the queue is being made */
    static
    HRESULT
    APIENTRY
    CreateContextCb(
        _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
        _Inout_ D3DDDICB_CREATECONTEXT* Args);

    static
    HRESULT
    APIENTRY
    CreateContextVirtualCb(
        _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
        _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args);

    static
    HRESULT
    APIENTRY
    DestroyContextCb(
        _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
        _In_ const D3DDDICB_DESTROYCONTEXT* Args);

    static
    HRESULT
    APIENTRY
    CreateHwQueueCb(
        _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
        _Inout_ D3DDDICB_CREATEHWQUEUE* Args);

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxCommandQueue() override;

    HRESULT
    Initialize(
        _In_ const D3D12_COMMAND_QUEUE_DESC& QueueDesc);

    BOOLEAN
    OwnsContext(
        _In_ D3DKMT_HANDLE Context) const;

    HRESULT
    ApplyPriority();

    BOOLEAN
    NoteContextKind(
        _In_ D3DDDI_CREATECONTEXTFLAGS Flags);

    D3D12_COMMAND_QUEUE_DESC Desc = {};
    RxSchedulingGroup* Group = nullptr;
    BOOLEAN HasSoftwareContexts = FALSE;
    BOOLEAN UsesGroupContexts = FALSE;
    RxDriverMemory12 Memory;
    D3D12DDI_HCOMMANDQUEUE DriverQueue = {};
    BOOLEAN IsDriverQueueCreated = FALSE;
    DWORD CreatingThread = 0;
    D3DKMT_HANDLE Contexts[RxMaxQueueEngines12] = {};
    UINT ContextCount = 0;
    D3DKMT_HANDLE HwQueues[RxMaxQueueEngines12] = {};
    UINT HwQueueCount = 0;
    SRWLOCK Lock = SRWLOCK_INIT;
};

class RxCommandAllocator final : public RxDeviceChild12<ID3D12CommandAllocator>
{
public:
    static constexpr GUID PrivateId = {0x2f7d0a91, 0x63c4, 0x4d58, {0xa2, 0x0e, 0x51, 0x8b, 0x3f, 0xc6, 0x94, 0x07}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ D3D12_COMMAND_LIST_TYPE Type,
        _Outptr_ RxCommandAllocator** Allocator);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "allocator.inl"

    D3D12_COMMAND_LIST_TYPE
    GetType() const
    {
        return Type;
    }

    D3D12DDI_HCOMMANDRECORDER_0040
    GetRecorder() const
    {
        return Recorder;
    }

    /* A recording list holds its allocator; only one may at a time */
    BOOLEAN
    Acquire()
    {
        return InterlockedCompareExchange(&IsHeld, 1, 0) == 0;
    }

    VOID
    Unhold()
    {
        InterlockedExchange(&IsHeld, 0);
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxCommandAllocator() override;

    HRESULT
    Initialize(
        _In_ D3D12_COMMAND_LIST_TYPE ListType);

    D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    RxDriverMemory12 PoolMemory;
    RxDriverMemory12 RecorderMemory;
    D3D12DDI_HCOMMANDPOOL_0040 Pool = {};
    D3D12DDI_HCOMMANDRECORDER_0040 Recorder = {};
    BOOLEAN IsPoolCreated = FALSE;
    BOOLEAN IsRecorderCreated = FALSE;
    volatile LONG IsHeld = 0;
};

class RxCommandList final : public RxDeviceChild12<ID3D12GraphicsCommandList10>
{
public:
    static constexpr GUID PrivateId = {0x7b41e5c3, 0x0d9f, 0x4a26, {0x93, 0x7c, 0x12, 0xe8, 0x5a, 0x06, 0xbf, 0x4d}};

    /* An open list records into the allocator; a closed one waits for Reset */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ D3D12_COMMAND_LIST_TYPE Type,
        _In_opt_ RxCommandAllocator* Allocator,
        _In_opt_ ID3D12PipelineState* InitialState,
        _Outptr_ RxCommandList** List);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "commandlist.inl"

    D3D12DDI_HCOMMANDLIST
    GetDriverHandle() const
    {
        return DriverList;
    }

    BOOLEAN
    IsRecording() const
    {
        return State == RxListState::Recording;
    }

    HRESULT
    GetError() const
    {
        return Error;
    }

    static
    VOID
    APIENTRY
    SetCommandListErrorCb(
        _In_ D3D12DDI_HRTCOMMANDLIST hRTCommandList,
        _In_ HRESULT hr);

    static
    VOID
    APIENTRY
    SetCommandListDdiTableCb(
        _In_ D3D12DDI_HRTCOMMANDLIST hRTCommandList,
        _In_ D3D12DDI_HRTTABLE hRTTable);

private:
    enum class RxListState
    {
        Closed,
        Recording,
    };

    using RxDeviceChild12::RxDeviceChild12;
    ~RxCommandList() override;

    HRESULT
    Initialize(
        _In_ D3D12_COMMAND_LIST_TYPE ListType,
        _In_opt_ RxCommandAllocator* FirstAllocator);

    HRESULT
    Claim(
        _In_ RxCommandAllocator* NewAllocator);

    HRESULT
    Begin(
        _In_opt_ ID3D12PipelineState* InitialState);

    VOID
    ResetPipeline(
        _In_opt_ ID3D12PipelineState* InitialState);

    VOID
    ResetState();

    /* Newer list entries take an application handle the driver finds its own object through */
    D3D12DDI_API_HCOMMANDLIST
    Api()
    {
        D3D12DDI_API_HCOMMANDLIST Handle;

        Handle.pDrvPrivate = &Bypass;
        return Handle;
    }

    D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    RxDriverMemory12 Memory;
    D3D12DDI_HCOMMANDLIST DriverList = {};
    D3D12DDI_RUNTIME_BYPASS_HEADER Bypass = {};
    BOOLEAN IsDriverListCreated = FALSE;
    const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114* DriverTable = nullptr;
    RxListState State = RxListState::Closed;
    RxCommandAllocator* Allocator = nullptr;
    UINT64 ListId = UINT64_MAX;
    BOOLEAN TakesSessions = FALSE;
    HRESULT Error = S_OK;
};

class RxFence final : public RxDeviceChild12<ID3D12Fence1>
{
public:
    static constexpr GUID PrivateId = {0xc85e2b47, 0x1fa6, 0x4c39, {0xb0, 0x7d, 0x94, 0x3e, 0x21, 0x6a, 0x58, 0xcf}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ UINT64 InitialValue,
        _In_ D3D12_FENCE_FLAGS Flags,
        _Outptr_ RxFence** Fence);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "fence.inl"

    D3DKMT_HANDLE
    GetSyncObject() const
    {
        return SyncObject;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxFence() override;

    HRESULT
    Initialize(
        _In_ UINT64 InitialValue,
        _In_ D3D12_FENCE_FLAGS FenceFlags);

    D3D12_FENCE_FLAGS Flags = D3D12_FENCE_FLAG_NONE;
    D3DKMT_HANDLE SyncObject = 0;
    volatile UINT64* CompletedValue = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HFENCE DriverFence = {};
    BOOLEAN IsDriverFenceCreated = FALSE;
};

/* One render target view slot as applications see it; the driver's own descriptor sits inside */
struct RxRtvEntry
{
    RxResource* Resource;
    VOID* Reserved;
    SIZE_T DriverHandle;
    SIZE_T Spare;
};
static_assert(sizeof(RxRtvEntry) == 4 * sizeof(VOID*));

class RxDescriptorHeap final : public RxDeviceChild12<ID3D12DescriptorHeap>
{
public:
    static constexpr GUID PrivateId = {0x4ea6930d, 0x8c12, 0x4f7a, {0x96, 0x2b, 0xd3, 0x5f, 0x08, 0x7e, 0xa1, 0x64}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_DESCRIPTOR_HEAP_DESC& Desc,
        _Outptr_ RxDescriptorHeap** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "descriptorheap.inl"

    D3D12DDI_HDESCRIPTORHEAP
    GetDriverHandle() const
    {
        return DriverHeap;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxDescriptorHeap() override;

    HRESULT
    Initialize(
        _In_ const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc);

    D3D12_DESCRIPTOR_HEAP_DESC Desc = {};
    RxDriverMemory12 Memory;
    D3D12DDI_HDESCRIPTORHEAP DriverHeap = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE CpuStart = {};
    D3D12DDI_GPU_DESCRIPTOR_HANDLE GpuStart = {};
    RxRtvEntry* RtvEntries = nullptr;
};

/* An application heap; the driver backs it with a buffer that placed resources are made in */
class RxHeap final : public RxDeviceChild12<ID3D12Heap1>
{
public:
    static constexpr GUID PrivateId = {0x8e2b47d1, 0x3c96, 0x4a05, {0xb1, 0x7e, 0x5d, 0x20, 0xc4, 0x9a, 0x63, 0xf8}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_HEAP_DESC& Desc,
        _Outptr_ RxHeap** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_HEAP_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override;

    const D3D12_HEAP_DESC&
    GetApiDesc() const
    {
        return Desc;
    }

    /* The buffer spanning the heap, which placed resources name as where they live */
    D3D12DDI_HRESOURCE
    GetBackingResource() const
    {
        return DriverResource;
    }

    D3D12_GPU_VIRTUAL_ADDRESS
    GetGpuAddress() const
    {
        return GpuAddress;
    }

    /* Maps the whole heap; every resource in it shares one mapping */
    HRESULT
    MapMemory(
        _Outptr_ BYTE** Base);

    VOID
    UnmapMemory();

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxHeap() override;

    HRESULT
    Initialize(
        _In_ const D3D12_HEAP_DESC& HeapDesc);

    D3D12_HEAP_DESC Desc = {};
    RxRuntimeResource12 Runtime = {};
    RxDriverMemory12 HeapMemory;
    RxDriverMemory12 ResourceMemory;
    D3D12DDI_HHEAP DriverHeap = {};
    D3D12DDI_HRESOURCE DriverResource = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0;
    SRWLOCK MapLock = SRWLOCK_INIT;
    UINT MapCount = 0;
    BYTE* MappedBase = nullptr;
};

/* A committed resource owns one driver heap with itself at its start; a placed one lives in an RxHeap */
class RxResource final : public RxDeviceChild12<ID3D12Resource2>
{
public:
    static constexpr GUID PrivateId = {0x61f03c8e, 0xb52d, 0x4e94, {0x8a, 0x47, 0x0c, 0x9b, 0xe3, 0x71, 0x2d, 0x5a}};

    static
    HRESULT
    CreateCommitted(
        _In_ RxDevice12& Device,
        _In_ const D3D12_HEAP_PROPERTIES& HeapProperties,
        _In_ D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC1& Desc,
        _In_ D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue,
        _Outptr_ RxResource** Resource);

    /* The desc is checked and has its defaults filled */
    static
    HRESULT
    CreatePlaced(
        _In_ RxDevice12& Device,
        _In_ RxHeap& Heap,
        _In_ UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC1& Desc,
        _In_ D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue,
        _Outptr_ RxResource** Resource);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "resource.inl"

    D3D12DDI_HRESOURCE
    GetDriverHandle() const
    {
        return DriverResource;
    }

    const D3D12_RESOURCE_DESC1&
    GetApiDesc() const
    {
        return Desc;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxResource() override;

    HRESULT
    Initialize(
        _In_ const D3D12_HEAP_PROPERTIES& HeapProperties,
        _In_ D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
        _In_ D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue);

    HRESULT
    InitializePlaced(
        _In_ RxHeap& Heap,
        _In_ UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
        _In_ D3D12_RESOURCE_STATES InitialState,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue);

    UINT
    SubresourceCount() const;

    D3D12_RESOURCE_DESC1 Desc = {};
    RxHeap* Placement = nullptr;
    UINT64 PlacementOffset = 0;
    D3D12_HEAP_PROPERTIES Properties = {};
    D3D12_HEAP_FLAGS Flags = D3D12_HEAP_FLAG_NONE;
    RxRuntimeResource12 Runtime = {};
    RxDriverMemory12 HeapMemory;
    RxDriverMemory12 ResourceMemory;
    D3D12DDI_HHEAP DriverHeap = {};
    D3D12DDI_HRESOURCE DriverResource = {};
    BOOLEAN IsDriverResourceCreated = FALSE;
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0;
    SRWLOCK MapLock = SRWLOCK_INIT;
    UINT MapCount = 0;
    BYTE* MappedBase = nullptr;
};

/* A root signature as the driver holds it; one per RTS0 part and node mask */
class RxRootSignature final : public RxDeviceChild12<ID3D12RootSignature>
{
public:
    static constexpr GUID PrivateId = {0x3a8c6d15, 0xe24b, 0x4f90, {0x9c, 0x61, 0x0b, 0x7e, 0x45, 0xd2, 0x18, 0xa3}};

    /* Part is the RTS0 part already found in the blob; the node mask is in API numbering */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ UINT NodeMask,
        _In_reads_bytes_(PartSize) const BYTE* Part,
        _In_ UINT PartSize,
        _In_ BOOLEAN Internal,
        _Outptr_ RxRootSignature** Signature);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12DDI_HROOTSIGNATURE
    GetDriverHandle() const
    {
        return DriverRoot;
    }

    BOOLEAN
    Matches(
        _In_reads_bytes_(Size) const BYTE* Part,
        _In_ UINT Size,
        _In_ UINT NodeMask) const
    {
        return Size == SerializedSize && NodeMask == Mask && memcmp(Part, Serialized, Size) == 0;
    }

    /* The desc the driver was given, in 1.2 form */
    const D3D12_ROOT_SIGNATURE_DESC2&
    GetLayout() const
    {
        return Layout;
    }

    /* Link in the device's cache */
    RxRootSignature* NextCached = nullptr;

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxRootSignature() override;

    HRESULT
    Initialize(
        _In_ UINT NodeMask,
        _In_reads_bytes_(PartSize) const BYTE* Part,
        _In_ UINT PartSize);

    BYTE* Serialized = nullptr;
    UINT SerializedSize = 0;
    UINT Mask = 0;
    BOOLEAN IsCached = FALSE;
    RxRootSignatureDesc Reader;
    D3D12_ROOT_SIGNATURE_DESC2 Layout = {};
    RxDriverMemory12 Memory;
    D3D12DDI_HROOTSIGNATURE DriverRoot = {};
    BOOLEAN IsDriverRootCreated = FALSE;

    friend class RxDevice12;
};

/* Descriptor and footprint helpers shared by the device and command lists */

/* Fills the mip count and alignment a desc leaves at 0, the way the runtime stores it */
VOID
RxFillResourceDefaults(
    _Inout_ D3D12_RESOURCE_DESC1& Desc);

/* The rules every resource description meets; the clear value is checked when given */
HRESULT
RxValidateResourceDesc(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear);

/* Heap properties and flags given to a heap, or to a committed resource's own heap */
HRESULT
RxValidateHeapProperties(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_PROPERTIES& Properties,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ UINT NodeIndex,
    _In_ BOOLEAN Committed);

/* A whole heap description: its properties, size and alignment */
HRESULT
RxValidateHeapDesc(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_DESC& Desc,
    _In_ UINT NodeIndex);

/* Whether a resource may live in a heap of this type and these flags, at this offset */
HRESULT
RxValidateHeapPlacement(
    _In_ RxDevice12& Device,
    _In_ D3D12_HEAP_TYPE Type,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ UINT64 HeapAlignment,
    _In_ UINT64 Offset,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_opt_ const D3D12_CLEAR_VALUE* Clear);

/* The state a new resource may start in */
HRESULT
RxValidateInitialState(
    _In_ RxDevice12& Device,
    _In_ D3D12_RESOURCE_STATES InitialState,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ D3D12_HEAP_TYPE HeapType);

/* The driver form of an API resource desc */
VOID
RxToDdiResource(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _Out_ D3D12DDIARG_CREATERESOURCE_0111& Ddi);

/* The driver form of a heap; an implicit heap is the one a committed resource owns */
VOID
RxToDdiHeap(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_PROPERTIES& Properties,
    _In_ D3D12_HEAP_FLAGS Flags,
    _In_ UINT64 Size,
    _In_ UINT64 Alignment,
    _In_ BOOLEAN Implicit,
    _Out_ D3D12DDIARG_CREATEHEAP_0001& Ddi);

/* The CPU page and memory pool a heap type stands for on this device */
D3D12_HEAP_PROPERTIES
RxCustomProperties(
    _In_ const RxDevice12& Device,
    _In_ D3D12_HEAP_TYPE Type);

/* What the driver says one resource needs, and the layout it picked */
D3D12_RESOURCE_ALLOCATION_INFO
RxAllocationInfo(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const D3D12DDIARG_CREATERESOURCE_0111& Ddi,
    _In_ BOOLEAN SizeQuery,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _Out_opt_ D3D12DDI_TEXTURE_LAYOUT* DriverLayout);

/* GetResourceAllocationInfo1 for descs not yet filled in */
D3D12_RESOURCE_ALLOCATION_INFO
RxAllocationInfoForDescs(
    _In_ RxDevice12& Device,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12_RESOURCE_DESC1* Descs,
    _Out_writes_opt_(Count) D3D12_RESOURCE_ALLOCATION_INFO1* Each);

/* GetCopyableFootprints for one desc; FALSE when the desc or range is invalid */
BOOLEAN
RxCopyableFootprints(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ UINT FirstSubresource,
    _In_ UINT NumSubresources,
    _In_ UINT64 BaseOffset,
    _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT* Layouts,
    _Out_writes_opt_(NumSubresources) UINT* NumRows,
    _Out_writes_opt_(NumSubresources) UINT64* RowSizeInBytes,
    _Out_opt_ UINT64* TotalBytes);

/* The block a format is stored in: texels across and down, and bytes per block; FALSE when unknown */
BOOLEAN
RxFormatBlock(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* BlockWidth,
    _Out_ UINT* BlockHeight,
    _Out_ UINT* BlockBytes);
