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

/* What a list type can record; a command names the ones it needs */
constexpr UINT RxListDoesCopy = 0x1;
constexpr UINT RxListDoesCompute = 0x2;
constexpr UINT RxListDoesGraphics = 0x4;
constexpr UINT RxListTopLevel = 0x1000;

/* A render pass as it began, kept for its end and for a later resume */
struct RxRenderPass
{
    UINT TargetCount;
    D3D12_RENDER_PASS_RENDER_TARGET_DESC Targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    BOOLEAN HasDepthStencil;
    D3D12_RENDER_PASS_DEPTH_STENCIL_DESC DepthStencil;
    D3D12_RENDER_PASS_FLAGS Flags;
    D3D12_RENDER_PASS_ENDING_ACCESS_RESOLVE_SUBRESOURCE_PARAMETERS* Resolves;
    UINT ResolveCapacity;
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

    /* Open and not failed; anything else drops what is recorded */
    BOOLEAN
    Records() const
    {
        return State == RxListState::Recording && SUCCEEDED(Error);
    }

    /* The first failure is what Close returns; later commands are dropped */
    VOID
    Fail(
        _In_ HRESULT hr);

    /* Whether the list type records a command needing these abilities; the list fails when not */
    BOOLEAN
    Takes(
        _In_ UINT Needed);

    /* A direct list does no GPU work while a pass is suspended, and some commands none inside one */
    BOOLEAN
    PassAllows(
        _In_ BOOLEAN InsidePass);

    VOID
    StartEmulatedPass(
        _In_ UINT NumRenderTargets,
        _In_reads_opt_(NumRenderTargets) const D3D12_RENDER_PASS_RENDER_TARGET_DESC* pRenderTargets,
        _In_opt_ const D3D12_RENDER_PASS_DEPTH_STENCIL_DESC* pDepthStencil,
        _In_ D3D12_RENDER_PASS_FLAGS Flags);

    VOID
    EndEmulatedPass();

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

    /* Recording state the runtime checks commands against */
    VOID* BoundHeaps[2] = {};
    INT OpenQueries = 0;
    BOOLEAN InPass = FALSE;
    BOOLEAN PassSuspended = FALSE;
    BOOLEAN PassResumedFresh = FALSE;
    RxRenderPass Pass = {};
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

    /* What residency calls name for this heap */
    D3D12DDI_HHEAP
    GetDriverHeap() const
    {
        return DriverHeap;
    }

    D3DKMT_HANDLE
    GetKernelResource() const
    {
        return Runtime.KmResource;
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

/* How a new resource starts out: its barrier layout and the formats its views may cast it to */
struct RxResourceStart12
{
    D3D12DDI_BARRIER_LAYOUT Layout;
    UINT32 NumCastableFormats;
    const DXGI_FORMAT* CastableFormats;
};

/* The layout a resource made with an API state starts in */
D3D12DDI_BARRIER_LAYOUT
RxLayoutForState(
    _In_ D3D12_RESOURCE_STATES State);

/*
 * A committed resource owns one driver heap with itself at its start; a placed one lives in an RxHeap;
 * a reserved one has no memory until tiles are mapped to it.
 */
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
        _In_ const RxResourceStart12& Start,
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
        _In_ const RxResourceStart12& Start,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue,
        _Outptr_ RxResource** Resource);

    /* The desc is checked and has its defaults filled */
    static
    HRESULT
    CreateReserved(
        _In_ RxDevice12& Device,
        _In_ const D3D12_RESOURCE_DESC1& Desc,
        _In_ const RxResourceStart12& Start,
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

    /* Where a placed resource starts in its heap; committed resources start at 0 */
    UINT64
    GetPlacementOffset() const
    {
        return PlacementOffset;
    }

    /* The heap a committed resource owns; placed and reserved resources have none */
    D3D12DDI_HHEAP
    GetOwnHeap() const
    {
        return Placement == nullptr ? DriverHeap : D3D12DDI_HHEAP{};
    }

    D3DKMT_HANDLE
    GetKernelResource() const
    {
        return Runtime.KmResource;
    }

    BOOLEAN
    IsReserved() const
    {
        return IsReservedResource;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxResource() override;

    HRESULT
    Initialize(
        _In_ const D3D12_HEAP_PROPERTIES& HeapProperties,
        _In_ D3D12_HEAP_FLAGS HeapFlags,
        _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
        _In_ const RxResourceStart12& Start,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue);

    HRESULT
    InitializePlaced(
        _In_ RxHeap& Heap,
        _In_ UINT64 HeapOffset,
        _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
        _In_ const RxResourceStart12& Start,
        _In_opt_ const D3D12_CLEAR_VALUE* ClearValue);

    HRESULT
    InitializeReserved(
        _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
        _In_ const RxResourceStart12& Start,
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
    BOOLEAN IsReservedResource = FALSE;
    D3D12_GPU_VIRTUAL_ADDRESS GpuAddress = 0;
    SRWLOCK MapLock = SRWLOCK_INIT;
    UINT MapCount = 0;
    BYTE* MappedBase = nullptr;
};

/* Queries the driver keeps results of until they are resolved */
class RxQueryHeap final : public RxDeviceChild12<ID3D12QueryHeap>
{
public:
    static constexpr GUID PrivateId = {0x5c09e3a4, 0x7d21, 0x4b6f, {0x90, 0x3a, 0xe8, 0x14, 0x6b, 0xc2, 0x5f, 0x07}};

    /* The desc is checked */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_QUERY_HEAP_DESC& Desc,
        _In_ UINT DriverNodeMask,
        _Outptr_ RxQueryHeap** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12DDI_HQUERYHEAP
    GetDriverHandle() const
    {
        return DriverHeap;
    }

    D3D12_QUERY_HEAP_TYPE
    GetType() const
    {
        return Type;
    }

    UINT
    GetCount() const
    {
        return Count;
    }

    /* Bytes one resolved query takes */
    UINT
    GetResultSize() const
    {
        return ResultSize;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxQueryHeap() override;

    HRESULT
    Initialize(
        _In_ const D3D12_QUERY_HEAP_DESC& HeapDesc,
        _In_ UINT DriverNodeMask);

    D3D12_QUERY_HEAP_TYPE Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
    UINT Count = 0;
    UINT ResultSize = 0;
    RxDriverMemory12 Memory;
    D3D12DDI_HQUERYHEAP DriverHeap = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
};

class RxRootSignature;

/* The layout of the arguments ExecuteIndirect reads from a buffer */
class RxCommandSignature final : public RxDeviceChild12<ID3D12CommandSignature>
{
public:
    static constexpr GUID PrivateId = {0xa17f6b28, 0x94c3, 0x4e5d, {0x8b, 0x02, 0x3f, 0xd6, 0x71, 0x9e, 0x24, 0xb5}};

    /* The desc is checked; the root signature gets a reference */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_COMMAND_SIGNATURE_DESC& Desc,
        _In_opt_ RxRootSignature* Root,
        _In_ UINT DriverNodeMask,
        _Outptr_ RxCommandSignature** Signature);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12DDI_HCOMMANDSIGNATURE
    GetDriverHandle() const
    {
        return DriverSignature;
    }

    UINT
    GetByteStride() const
    {
        return ByteStride;
    }

    /* The draw or dispatch every command ends with */
    D3D12_INDIRECT_ARGUMENT_TYPE
    GetCommandType() const
    {
        return CommandType;
    }

    /* Bytes the arguments of one command take */
    UINT
    GetCommandSize() const
    {
        return CommandSize;
    }

    UINT
    GetArgumentCount() const
    {
        return ArgumentCount;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxCommandSignature() override;

    HRESULT
    Initialize(
        _In_ const D3D12_COMMAND_SIGNATURE_DESC& Desc,
        _In_opt_ RxRootSignature* RootSignature,
        _In_ UINT DriverNodeMask);

    UINT ByteStride = 0;
    UINT CommandSize = 0;
    D3D12_INDIRECT_ARGUMENT_DESC* Arguments = nullptr;
    UINT ArgumentCount = 0;
    D3D12_INDIRECT_ARGUMENT_TYPE CommandType = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    RxRootSignature* Root = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HCOMMANDSIGNATURE DriverSignature = {};
    BOOLEAN IsDriverSignatureCreated = FALSE;
};

/* Named pipeline states an application keeps between runs */
class RxPipelineLibrary final : public RxDeviceChild12<ID3D12PipelineLibrary1>
{
public:
    static constexpr GUID PrivateId = {0x3e8d5a17, 0xc640, 0x4f2b, {0x9d, 0x71, 0x08, 0xb3, 0x5e, 0xa6, 0xc4, 0x19}};

    /* An empty library */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _Outptr_ RxPipelineLibrary** Library);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    StorePipeline(
        _In_opt_ LPCWSTR pName,
        _In_ ID3D12PipelineState* pPipeline) override;

    HRESULT
    STDMETHODCALLTYPE
    LoadGraphicsPipeline(
        _In_ LPCWSTR pName,
        _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC* pDesc,
        REFIID riid,
        _COM_Outptr_ void** ppPipelineState) override;

    HRESULT
    STDMETHODCALLTYPE
    LoadComputePipeline(
        _In_ LPCWSTR pName,
        _In_ const D3D12_COMPUTE_PIPELINE_STATE_DESC* pDesc,
        REFIID riid,
        _COM_Outptr_ void** ppPipelineState) override;

    SIZE_T
    STDMETHODCALLTYPE
    GetSerializedSize(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    Serialize(
        _Out_writes_(DataSizeInBytes) void* pData,
        SIZE_T DataSizeInBytes) override;

    HRESULT
    STDMETHODCALLTYPE
    LoadPipeline(
        _In_ LPCWSTR pName,
        _In_ const D3D12_PIPELINE_STATE_STREAM_DESC* pDesc,
        REFIID riid,
        _COM_Outptr_ void** ppPipelineState) override;

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxPipelineLibrary() override;

    HRESULT
    Initialize();

    RxDriverMemory12 Memory;
    D3D12DDI_HPIPELINELIBRARY DriverLibrary = {};
    BOOLEAN IsDriverLibraryCreated = FALSE;
    SRWLOCK Lock = SRWLOCK_INIT;
    SIZE_T SerializedSize = 0;
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

    /* What list entries take: a bypass header that leads the driver to its root signature */
    D3D12DDI_API_HROOTSIGNATURE
    Api()
    {
        D3D12DDI_API_HROOTSIGNATURE Handle;

        Handle.pDrvPrivate = &Bypass;
        return Handle;
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
    D3D12DDI_RUNTIME_BYPASS_HEADER Bypass = {};
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

/* The barrier layout a new resource may start in, for the creation methods that take one */
HRESULT
RxValidateInitialLayout(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const RxResourceStart12& Start);

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

/* GetResourceAllocationInfo1 for descs not yet filled in; castable format lists are optional */
D3D12_RESOURCE_ALLOCATION_INFO
RxAllocationInfoForDescs(
    _In_ RxDevice12& Device,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12_RESOURCE_DESC1* Descs,
    _Out_writes_opt_(Count) D3D12_RESOURCE_ALLOCATION_INFO1* Each,
    _In_opt_count_(Count) const UINT32* CastableCounts = nullptr,
    _In_opt_count_(Count) const DXGI_FORMAT* const* Castables = nullptr);

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
