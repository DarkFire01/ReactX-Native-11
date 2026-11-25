/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device children: queues, allocators, command lists, fences, descriptor heaps, resources
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"
#include "rootsignature.hpp"
#include "rxbridge.hpp"

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

class RxCommandQueue;
class RxCommandList;
class RxFence;

/* Recordings a queue keeps for driver presents, each reused once the GPU has run it */
constexpr UINT RxPresentRecordings12 = 4;

/* The queue as the ReactX dxgi.dll sees it; lifetime and QueryInterface stay with the queue */
class RxQueuePresent final : public IRxPresentQueue
{
public:
    explicit RxQueuePresent(
        _In_ RxCommandQueue& Owner)
        : Queue(Owner)
    {
    }

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

    HRESULT
    STDMETHODCALLTYPE
    GetAdapterLuid(
        _Out_ LUID* Luid) override;

    HRESULT
    STDMETHODCALLTYPE
    CreatePresentBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) override;

    VOID
    STDMETHODCALLTYPE
    DetachPresentBuffer(
        _In_ IUnknown* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    OpenPresentTarget(
        _In_ HANDLE SharedHandle,
        _COM_Outptr_ IUnknown** Target) override;

    HRESULT
    STDMETHODCALLTYPE
    ShareBuffer(
        _In_ IUnknown* Buffer,
        _Out_ HANDLE* SharedHandle) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFlipFence(
        _Out_ UINT* Fence,
        _Out_ HANDLE* SharedHandle) override;

    VOID
    STDMETHODCALLTYPE
    DestroyFlipFence(
        _In_ UINT Fence) override;

    HANDLE
    STDMETHODCALLTYPE
    GetPresentLimitSemaphore() override;

    HRESULT
    STDMETHODCALLTYPE
    PresentFlip(
        _In_ IUnknown* Buffer,
        _In_ RxPresentContext* Context) override;

    HRESULT
    STDMETHODCALLTYPE
    RotateBuffers(
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) override;

    HRESULT
    STDMETHODCALLTYPE
    SubmitPresent(
        _In_ IUnknown* Buffer,
        _In_opt_ IUnknown* Target,
        _In_ RxPresentContext* Context) override;

    D3DKMT_HANDLE
    STDMETHODCALLTYPE
    GetKernelDevice() override;

    HRESULT
    STDMETHODCALLTYPE
    CreatePrimaryBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    SetDisplaySurface(
        _In_ IUnknown* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    PresentPrimary(
        _In_ IUnknown* Buffer,
        _In_ UINT FlipInterval,
        _In_ RxPresentContext* Context) override;

    HRESULT
    STDMETHODCALLTYPE
    CopyBufferRegions(
        _In_ IUnknown* Destination,
        _In_ IUnknown* Source,
        _In_reads_(Count) const RECT* Rects,
        _In_ UINT Count,
        _In_ POINT Offset) override;

    HRESULT
    STDMETHODCALLTYPE
    GetRemovedReason() override;

    HRESULT
    STDMETHODCALLTYPE
    BltToTarget(
        _In_ IUnknown* Buffer,
        _In_ IUnknown* Target,
        _In_ const RECT* DestRect) override;

    HRESULT
    STDMETHODCALLTYPE
    GetTargetSize(
        _In_ IUnknown* Target,
        _Out_ UINT* Width,
        _Out_ UINT* Height) override;

    BOOL
    STDMETHODCALLTYPE
    IsBufferDcHeld(
        _In_ IUnknown* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    WaitForIdle() override;

private:
    RxCommandQueue& Queue;
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

    /* Swapchain presents, see present.cpp */
    HRESULT
    SubmitSwapChainPresent(
        _In_ RxResource& Buffer,
        _Inout_ RxPresentContext& Context);

    HRESULT
    WaitForIdle();

    HANDLE
    GetPresentLimitSemaphore();

private:
    /* One recording of a driver present and the fence value its GPU work finishes at */
    struct PresentRecording
    {
        RxCommandAllocator* Allocator;
        UINT64 FenceValue;
    };

    using RxDeviceChild12::RxDeviceChild12;
    ~RxCommandQueue() override;

    /* The objects presents record and wait with, made on the first present */
    HRESULT
    PreparePresentObjects();

    /* Opens the present list on a recording the GPU is done with */
    HRESULT
    BeginPresentRecording(
        _Outptr_ PresentRecording** Recording);

    HRESULT
    SubmitKernelPresent(
        _Inout_ RxPresentContext& Context,
        _In_ const D3D12DDIARG_PRESENT_0001& Args,
        _In_ const D3D12DDI_PRESENT_0051& Result,
        _In_ const D3D12DDI_PRESENT_CONTEXTS_0051& Engines,
        _In_ const D3D12DDI_PRESENT_HWQUEUES_0051& Queues);

    /* Signals the present fence after the work queued so far and returns the value */
    HRESULT
    SignalPresentFence(
        _Out_ UINT64* Value);

    HRESULT
    WaitForPresentFence(
        _In_ UINT64 Value);

    VOID
    ReleasePresentObjects();

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

    /* Swapchain presents: the list the driver records them into and what it waits on */
    RxQueuePresent PresentFace{ *this };
    SRWLOCK PresentLock = SRWLOCK_INIT;
    RxCommandList* PresentList = nullptr;
    RxFence* PresentFence = nullptr;
    UINT64 PresentFenceValue = 0;
    PresentRecording Recordings[RxPresentRecordings12] = {};
    UINT NextRecording = 0;
    BYTE* PresentDriverData = nullptr;
    UINT PresentDriverDataSize = 0;

    /* What the kernel said about composition at the last present; the driver hears it with the next */
    BOOLEAN OptimizeForComposition = TRUE;
    HANDLE LimitSemaphore = NULL;
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

/* What the driver's command list callbacks reach: the runtime handle of every list points here */
class RxListCallbacks12
{
public:
    virtual
    VOID
    OnDriverError(
        _In_ HRESULT hr) = 0;

    virtual
    VOID
    OnDriverTable(
        _In_ D3D12DDI_HRTTABLE Table) = 0;
};

class RxCommandList final : public RxDeviceChild12<ID3D12GraphicsCommandList10>, public RxListCallbacks12
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

    /* Has the driver record a swapchain present for a queue into this open list */
    VOID
    RecordPresent(
        _In_ D3D12DDI_HCOMMANDQUEUE Queue,
        _In_ const D3D12DDIARG_PRESENT_0001& Args,
        _Out_ D3D12DDI_PRESENT_0051* Result,
        _Out_opt_ D3D12DDI_PRESENT_CONTEXTS_0051* Contexts,
        _Out_opt_ D3D12DDI_PRESENT_HWQUEUES_0051* HwQueues);

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

    VOID
    OnDriverError(
        _In_ HRESULT hr) override;

    VOID
    OnDriverTable(
        _In_ D3D12DDI_HRTTABLE Table) override;

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

    /* A fence another device shared by NT handle */
    static
    HRESULT
    Open(
        _In_ RxDevice12& Device,
        _In_ HANDLE SharedHandle,
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

    HRESULT
    InitializeOpened(
        _In_ HANDLE SharedHandle);

    /* The driver fence over the monitored fence this object holds */
    HRESULT
    CreateDriverFence(
        _In_ D3D12_GPU_VIRTUAL_ADDRESS FenceAddress);

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

struct RxSharedRecord12;

/* A kernel resource opened from a shared handle, with the descs its record gave */
struct RxOpenedShare12
{
    D3DKMT_HANDLE KmResource;
    D3DKMT_HANDLE KeyedMutex;
    D3DKMT_HANDLE KeyedMutexSync;
    UINT AllocationCount;
    D3DDDI_OPENALLOCATIONINFO* Allocations;
    VOID* ResourceData;
    UINT ResourceDataSize;
    D3D12_HEAP_DESC HeapDesc;
    D3D12_RESOURCE_DESC1 ResourceDesc;
};

/* The runtime record a shared heap is made with; Resource names the committed resource at its start */
VOID
RxFillSharedRecord12(
    _In_ const D3D12_HEAP_DESC& Heap,
    _In_opt_ const D3D12_RESOURCE_DESC1* Resource,
    _Out_ RxSharedRecord12& Record);

/* Makes the driver heap and resource over an opened kernel resource */
HRESULT
RxOpenDriverHeap12(
    _In_ RxDevice12& Device,
    _In_ const RxOpenedShare12& Opened,
    _Inout_ RxRuntimeResource12& Runtime,
    _Inout_ RxDriverMemory12& HeapMemory,
    _Inout_ RxDriverMemory12& ResourceMemory,
    _Out_ D3D12DDI_HHEAP* DriverHeap,
    _Out_ D3D12DDI_HRESOURCE* DriverResource);

/* Opens a shared heap or committed resource by NT or global handle; the object comes back with one reference */
HRESULT
RxOpenSharedResource12(
    _In_ RxDevice12& Device,
    _In_ HANDLE Handle,
    _Outptr_ IUnknown** Object);

/* A kernel status as API methods return it: bad handles are E_HANDLE, a lost device is DXGI's error */
HRESULT
RxApiResult12(
    _In_ RxDevice12& Device,
    _In_ NTSTATUS Status,
    _In_z_ PCSTR Call);

/* Closes what an opened kernel resource still holds once the driver let go of it */
VOID
RxCloseOpenedShare12(
    _In_ RxDevice12& Device,
    _Inout_ RxRuntimeResource12& Runtime);

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

    /* A heap over a kernel resource opened from a shared handle */
    static
    HRESULT
    Open(
        _In_ RxDevice12& Device,
        _In_ const RxOpenedShare12& Opened,
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

    const RxRuntimeResource12&
    GetRuntime() const
    {
        return Runtime;
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

    HRESULT
    InitializeOpened(
        _In_ const RxOpenedShare12& Opened);

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

    /* Swapchain buffers only: the record their kernel resource is shared with */
    const VOID* SharedRecord;
    UINT SharedRecordSize;
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

    /* A committed resource over a kernel resource opened from a shared handle */
    static
    HRESULT
    Open(
        _In_ RxDevice12& Device,
        _In_ const RxOpenedShare12& Opened,
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

    /* Committed resources own their heap; only those can be shared */
    BOOLEAN
    IsCommitted() const
    {
        return Placement == nullptr && !IsReservedResource;
    }

    D3D12_HEAP_FLAGS
    GetHeapFlags() const
    {
        return Flags;
    }

    const RxRuntimeResource12&
    GetRuntime() const
    {
        return Runtime;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxResource() override;

    HRESULT
    InitializeOpened(
        _In_ const RxOpenedShare12& Opened);

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

    /* Maps the memory under the resource, counted together with Map */
    HRESULT
    MapMemory(
        _Outptr_ BYTE** Base);

    VOID
    UnmapMemory();

    /* WriteToSubresource and ReadFromSubresource, which differ only in direction */
    HRESULT
    CopySubresourceData(
        _In_ UINT Subresource,
        _In_opt_ const D3D12_BOX* Box,
        _Inout_ BYTE* Linear,
        _In_ UINT LinearPitch,
        _In_ UINT LinearDepthPitch,
        _In_ BOOLEAN Write);

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
    /* The layout the driver picked at creation, which CPU copies of texels follow */
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 CreationLayout = {};
    BOOLEAN HasCreationLayout = FALSE;
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
    _Out_opt_ D3D12DDI_TEXTURE_LAYOUT* DriverLayout,
    _Out_opt_ D3D12DDI_RESOURCE_ALLOCATION_INFO_0022* DriverInfo = nullptr);

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
