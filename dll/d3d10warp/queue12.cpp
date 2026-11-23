/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 command queues, their kernel contexts, fences and presents
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"

static
RxWarp12CommandQueue *
RxWarp12GetQueue(
    _In_ D3D12DDI_HCOMMANDQUEUE Queue)
{
    return RxWarpObject<RxWarp12CommandQueue>(Queue);
}

/* Talks to the Basic Render kernel driver through its escape interface */
static
HRESULT
RxWarp12Escape(
    _In_ RxWarp12Device *Device,
    _Inout_updates_bytes_(Size) VOID *Data,
    _In_ UINT Size,
    _In_opt_ HANDLE Context)
{
    D3DDDICB_ESCAPE Escape = {};

    if (Device->KernelCallbacks == nullptr || Device->KernelCallbacks->pfnEscapeCb == nullptr)
        return E_NOTIMPL;
    Escape.hDevice = Device->RuntimeDevice.handle;
    Escape.pPrivateDriverData = Data;
    Escape.PrivateDriverDataSize = Size;
    Escape.hContext = Context;
    return Device->KernelCallbacks->pfnEscapeCb(Device->Adapter->RuntimeAdapter.handle, &Escape);
}

/* A plain kernel buffer the context hands the kernel driver for its own bookkeeping */
static
HRESULT
RxWarp12AllocateContextBuffer(
    _In_ RxWarp12Device *Device,
    _In_ UINT Size,
    _Out_ D3DKMT_HANDLE *Allocation,
    _Out_ VOID **Memory)
{
    RX_WARP_ALLOCATION_DATA Data = {};
    D3DDDI_ALLOCATIONINFO Info = {};
    D3DDDICB_ALLOCATE Allocate = {};
    D3DDDICB_LOCK Lock = {};
    HRESULT hr;

    *Allocation = 0;
    *Memory = nullptr;
    if (Device->KernelCallbacks->pfnAllocateCb == nullptr || Device->KernelCallbacks->pfnLockCb == nullptr)
        return E_NOTIMPL;
    Data.Dimension = D3D10DDIRESOURCE_BUFFER;
    Data.Width = Size;
    Data.Height = 1;
    Data.Depth = 1;
    Data.RowPitch = Size;
    Data.Size = Size;
    Data.ArraySize = 1;
    Data.MipLevels = 1;
    Data.Flags = RX_WARP_ALLOCATION_LINEAR;
    Info.pPrivateDriverData = &Data;
    Info.PrivateDriverDataSize = sizeof(Data);
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &Info;
    hr = Device->KernelCallbacks->pfnAllocateCb(Device->RuntimeDevice.handle, &Allocate);
    if (FAILED(hr))
        return hr;
    *Allocation = Info.hAllocation;
    Lock.hAllocation = Info.hAllocation;
    Lock.Flags.DonotWait = 1;
    Lock.Flags.IgnoreSync = 1;
    hr = Device->KernelCallbacks->pfnLockCb(Device->RuntimeDevice.handle, &Lock);
    if (SUCCEEDED(hr))
        *Memory = Lock.pData;
    return hr;
}

/*
 * Each queue gets a kernel context on the engine node the kernel driver reports, a page the
 * kernel driver knows beside it and a buffer that page sizes. The device registers once.
 */
static
HRESULT
RxWarp12CreateKernelContext(
    _Inout_ RxWarp12CommandQueue *Queue)
{
    RxWarp12Device *Device = Queue->Device;
    D3DDDICB_CREATECONTEXTVIRTUAL Create = {};
    UINT Revision[2] = { 3, 2 };
    UINT Engine[4] = { 1, 0, 0, 0 };
    UINT64 Values[2] = { 0, 8 };
    UINT64 *Private[2] = { &Values[0], &Values[1] };
    HRESULT hr;

    if (Device->Callbacks->pfnCreateContextVirtualCb == nullptr)
        return S_OK;
    if (!Device->Registered)
        RxWarp12Escape(Device, Revision, sizeof(Revision), nullptr);
    hr = RxWarp12Escape(Device, Engine, sizeof(Engine), nullptr);
    if (FAILED(hr))
    {
        RX_WARP_TRACE("D3D12 engine query failed: %08lx\n", hr);
        return hr;
    }

    Create.NodeOrdinal = Engine[2];
    Create.EngineAffinity = Engine[3];
    Create.pPrivateDriverData = Private;
    Create.PrivateDriverDataSize = sizeof(Private);
    hr = Device->Callbacks->pfnCreateContextVirtualCb(Queue->RuntimeQueue, &Create);
    if (FAILED(hr))
    {
        RX_WARP_TRACE("D3D12 kernel context creation failed: %08lx\n", hr);
        return hr;
    }
    Queue->KernelContext = Create.hContext;

    Queue->ContextPage = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (Queue->ContextPage != nullptr)
    {
        UINT64 ContextData[6] = { 7, (UINT64)Queue, (UINT64)Queue->ContextPage, 4096, 0, 0 };
        UINT64 DeviceData[7] = { 0, (UINT64)Device, (UINT64)Queue->ContextPage, 4096, 0, 0, 0 };

        /* The buffer size comes back in the last field */
        if (SUCCEEDED(RxWarp12Escape(Device, ContextData, sizeof(ContextData), Create.hContext)) &&
            SUCCEEDED(RxWarp12AllocateContextBuffer(Device, (UINT)ContextData[5], &Queue->ContextAllocation, &Queue->ContextBuffer)) &&
            !Device->Registered)
        {
            RxWarp12Escape(Device, DeviceData, sizeof(DeviceData), nullptr);
        }
    }
    Device->Registered = TRUE;
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarp12CreateCommandQueue(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATECOMMANDQUEUE_0050 *Args,
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _In_ D3D12DDI_HRTCOMMANDQUEUE RuntimeQueue)
{
    RxWarpLock Lock;
    RxWarp12CommandQueue *Object;
    HRESULT hr;

    Object = new (std::nothrow) RxWarp12CommandQueue();
    RxWarpBind(Queue, Object);
    if (Object == nullptr)
        return E_OUTOFMEMORY;
    Object->Device = RxWarpObject<RxWarp12Device>(Device);
    Object->RuntimeQueue = RuntimeQueue;
    Object->Flags = Args->QueueFlags;
    hr = RxWarp12CreateKernelContext(Object);
    if (FAILED(hr))
    {
        delete Object;
        RxWarpBind(Queue, static_cast<RxWarp12CommandQueue *>(nullptr));
    }
    return hr;
}

static
VOID
APIENTRY
RxWarp12DestroyCommandQueue(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDQUEUE Queue)
{
    RxWarpLock Lock;
    RxWarp12CommandQueue *Object = RxWarp12GetQueue(Queue);
    RxWarp12Device *This = RxWarpObject<RxWarp12Device>(Device);

    if (Object == nullptr)
        return;
    if (Object->ContextAllocation != 0)
    {
        D3DDDICB_UNLOCK Unlock = {};
        D3DDDICB_DEALLOCATE Deallocate = {};

        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Object->ContextAllocation;
        This->KernelCallbacks->pfnUnlockCb(This->RuntimeDevice.handle, &Unlock);
        Deallocate.NumAllocations = 1;
        Deallocate.HandleList = &Object->ContextAllocation;
        This->KernelCallbacks->pfnDeallocateCb(This->RuntimeDevice.handle, &Deallocate);
    }
    if (Object->KernelContext != nullptr && This->Callbacks->pfnDestroyContextCb != nullptr)
    {
        D3DDDICB_DESTROYCONTEXT Destroy = {};

        Destroy.hContext = Object->KernelContext;
        This->Callbacks->pfnDestroyContextCb(Object->RuntimeQueue, &Destroy);
    }
    if (Object->ContextPage != nullptr)
        VirtualFree(Object->ContextPage, 0, MEM_RELEASE);
    delete Object;
}

/* Work runs as the queue receives it, so it is done before any later signal */
static
VOID
APIENTRY
RxWarp12ExecuteCommandLists(
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_HCOMMANDLIST *Lists)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Queue);
    for (UINT Index = 0; Index < Count; Index++)
    {
        RxWarp12CommandList *List = RxWarpObject<RxWarp12CommandList>(Lists[Index]);

        if (List == nullptr)
            continue;
        for (const std::function<VOID()> &Command : List->Commands)
            Command();
    }
}

/* Monitored fences are signaled and waited on by the runtime on every adapter; native ones are not offered */
static
VOID
APIENTRY
RxWarp12SignalFence(
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _Inout_ D3D12DDIARG_FENCE_OPERATION *Operation)
{
    UNREFERENCED_PARAMETER(Queue);
    Operation->PhysicalAdapterMask = 0xFFFFFFFF;
}

static
VOID
APIENTRY
RxWarp12WaitForFence(
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _Inout_ D3D12DDIARG_FENCE_OPERATION *Operation)
{
    UNREFERENCED_PARAMETER(Queue);
    Operation->PhysicalAdapterMask = 0xFFFFFFFF;
}

static
HRESULT
APIENTRY
RxWarp12CreateFence(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HFENCE Fence,
    _In_ D3D12DDI_HRTFENCE RuntimeFence,
    _In_ const D3D12DDIARG_CREATE_FENCE_0116 *Args)
{
    RxWarpLock Lock;
    RxWarp12Fence *Object;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(RuntimeFence);
    if (Args->FenceType != D3D12DDI_FENCE_TYPE_MONITORED)
    {
        RX_WARP_TRACE("D3D12 native fences are not supported\n");
        return E_INVALIDARG;
    }
    Object = new (std::nothrow) RxWarp12Fence();
    RxWarpBind(Fence, Object);
    if (Object == nullptr)
        return E_OUTOFMEMORY;
    Object->Type = Args->FenceType;
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyFence(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HFENCE Fence)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    delete RxWarpObject<RxWarp12Fence>(Fence);
}

/*
 * Blt presents hand the runtime the back buffer's kernel allocation and the queue's context;
 * the runtime submits the present itself.
 */
static
VOID
APIENTRY
RxWarp12Present(
    _In_ D3D12DDI_HCOMMANDLIST List,
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _In_ const D3D12DDIARG_PRESENT_0001 *Args,
    _Out_ D3D12DDI_PRESENT_0051 *Present,
    _Out_opt_ D3D12DDI_PRESENT_CONTEXTS_0051 *Contexts,
    _Out_opt_ D3D12DDI_PRESENT_HWQUEUES_0051 *HwQueues)
{
    RxWarpLock Lock;
    RxWarp12CommandQueue *Object = RxWarp12GetQueue(Queue);
    RxWarp12CommandList *Recorder = RxWarpObject<RxWarp12CommandList>(List);
    HRESULT hr = S_OK;

    memset(Present, 0, sizeof(*Present));
    if (HwQueues != nullptr)
    {
        memset(HwQueues, 0, sizeof(*HwQueues));
        HwQueues->BroadcastQueueCount = 1;
    }
    else if (Contexts != nullptr)
    {
        memset(Contexts, 0, sizeof(*Contexts));
        Contexts->hContext = Object->KernelContext;
    }

    if (Args->hDstResource.pDrvPrivate != nullptr || !Args->Flags.Blt || Args->Flags.Flip)
    {
        hr = E_INVALIDARG;
    }
    else
    {
        for (UINT Index = 0; Index < Args->SurfacesToPresent; Index++)
        {
            RxWarp12Resource *Source = RxWarp12GetResource(Args->phSurfacesToPresent[Index].hSurface);

            if (Source == nullptr || Source->Heap == nullptr)
            {
                hr = E_INVALIDARG;
                break;
            }
            Present->BroadcastSrcAllocation[0] = Source->Heap->Allocation;
        }
    }
    if (FAILED(hr))
    {
        RX_WARP_TRACE("D3D12 present failed: %08lx\n", hr);
        Object->Device->Callbacks->pfnSetCommandListErrorCb(Recorder->RuntimeList, hr);
    }
}

static
VOID
APIENTRY
RxWarp12UpdateTileMappings(
    _In_ D3D12DDI_HCOMMANDQUEUE Queue,
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_ UINT RegionCount,
    _In_reads_(RegionCount) const D3D12DDI_TILED_RESOURCE_COORDINATE *Coordinates,
    _In_reads_(RegionCount) const D3D12DDI_TILE_REGION_SIZE *Sizes,
    _In_ D3D12DDI_HHEAP Heap,
    _In_ UINT RangeCount,
    _In_reads_(RangeCount) const D3D12DDI_TILE_RANGE_FLAGS *Flags,
    _In_reads_(RangeCount) const UINT *HeapStarts,
    _In_reads_(RangeCount) const UINT *TileCounts,
    _In_ D3D12DDI_TILE_MAPPING_FLAGS MappingFlags)
{
    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(RegionCount);
    UNREFERENCED_PARAMETER(Coordinates);
    UNREFERENCED_PARAMETER(Sizes);
    UNREFERENCED_PARAMETER(Heap);
    UNREFERENCED_PARAMETER(RangeCount);
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(HeapStarts);
    UNREFERENCED_PARAMETER(TileCounts);
    UNREFERENCED_PARAMETER(MappingFlags);
    RX_WARP_TRACE("D3D12 tile mappings are not written yet\n");
}

VOID
RxWarp12FillQueueTables(
    _Inout_opt_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Device,
    _Inout_opt_ D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *Queue,
    _Inout_opt_ D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *List)
{
    if (Device != nullptr)
    {
        Device->pfnCreateCommandQueue = RxWarp12CreateCommandQueue;
        Device->pfnDestroyCommandQueue = RxWarp12DestroyCommandQueue;
        Device->pfnCreateFence = RxWarp12CreateFence;
        Device->pfnDestroyFence = RxWarp12DestroyFence;
    }
    if (Queue != nullptr)
    {
        Queue->pfnExecuteCommandLists = RxWarp12ExecuteCommandLists;
        Queue->pfnSignalFence = RxWarp12SignalFence;
        Queue->pfnWaitForFence = RxWarp12WaitForFence;
        Queue->pfnUpdateTileMappings = RxWarp12UpdateTileMappings;
    }
    if (List != nullptr)
        List->pfnPresent = RxWarp12Present;
}
