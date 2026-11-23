/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 command queues, their kernel contexts, fences and presents
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"

static constexpr UINT RxWarp12ContextPageSize = 4096;

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

    Data.Dimension = D3D10DDIRESOURCE_BUFFER;
    Data.Width = Size;
    Data.Height = 1;
    Data.Depth = 1;
    Data.RowPitch = Size;
    Data.Size = Size;
    Data.ArraySize = 1;
    Data.MipLevels = 1;
    Data.Flags = RX_WARP_ALLOCATION_LINEAR;
    return RxWarp12AllocateKernel(Device, &Data, nullptr, FALSE, Allocation, Memory);
}

/* Each queue gets a kernel context on the engine node the kernel driver reports and a page beside it */
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

    /* Each context keeps a page of kernel memory beside it */
    hr = RxWarp12AllocateContextBuffer(Device, RxWarp12ContextPageSize, &Queue->ContextAllocation, &Queue->ContextBuffer);
    if (FAILED(hr))
        return hr;
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
        RxWarp12FreeKernel(This, nullptr, Object->ContextAllocation, Object->ContextBuffer != nullptr);
    if (Object->KernelContext != nullptr && This->Callbacks->pfnDestroyContextCb != nullptr)
    {
        D3DDDICB_DESTROYCONTEXT Destroy = {};

        Destroy.hContext = Object->KernelContext;
        This->Callbacks->pfnDestroyContextCb(Object->RuntimeQueue, &Destroy);
    }
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

/* The kernel driver presents eight bit BGR; other back buffers, multisampled ones and array slices go through a copy */
static
BOOLEAN
RxWarp12NeedsShadow(
    _In_ const RxWarp12Resource *Source,
    _In_ UINT Subresource)
{
    return (Source->Storage.Format != DXGI_FORMAT_B8G8R8A8_UNORM && Source->Storage.Format != DXGI_FORMAT_B8G8R8X8_UNORM) ||
           Source->Storage.SampleCount > 1 || Subresource != 0;
}

/* Converts the presented subresource into the resource's B8G8R8X8 shadow, making the shadow on first use */
static
HRESULT
RxWarp12FillShadow(
    _In_ RxWarp12Device *Device,
    _Inout_ RxWarp12Resource *Source,
    _In_ UINT Subresource)
{
    const RX_WARP_SUBRESOURCE *From = Source->Storage.Subresource(Subresource);
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(Source->Storage.Format);
    BOOLEAN Encode;
    HRESULT hr;

    if (From == nullptr || Info == nullptr || Source->Storage.Memory == nullptr)
        return E_INVALIDARG;
    if (Source->ShadowAllocation == 0)
    {
        RX_WARP_ALLOCATION_DATA Data = {};

        Data.Dimension = D3D10DDIRESOURCE_TEXTURE2D;
        Data.Width = From->Width;
        Data.Height = From->Height;
        Data.Depth = 1;
        Data.Format = DXGI_FORMAT_B8G8R8X8_UNORM;
        Data.RowPitch = From->Width * 4;
        Data.Size = From->Width * From->Height * 4;
        Data.ArraySize = 1;
        Data.MipLevels = 1;
        Data.Usage = D3D10_DDI_USAGE_STAGING;
        Data.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
        Data.SampleCount = 1;
        Data.Flags = RX_WARP_ALLOCATION_SYSTEM | RX_WARP_ALLOCATION_LINEAR;
        hr = RxWarp12AllocateKernel(Device, &Data, nullptr, FALSE, &Source->ShadowAllocation, reinterpret_cast<VOID **>(&Source->ShadowMemory));
        if (FAILED(hr))
            return hr;
    }

    /* Float back buffers are linear light and go out sRGB encoded */
    Encode = Info->Kind == RxKindPlain && Info->Channels[0].Type == RxChannelFloat;
    for (UINT Y = 0; Y < From->Height; Y++)
    {
        const BYTE *Row = Source->Storage.Data(Subresource) + (SIZE_T)Y * From->RowPitch;

        for (UINT X = 0; X < From->Width; X++)
        {
            UINT Raw[4];
            FLOAT Color[4];
            BYTE Element[16];

            if (!RxWarpDecodeTexel(Source->Storage.Format, Row + (SIZE_T)X * Info->Bytes * Source->Storage.SampleCount, Raw))
                return E_INVALIDARG;
            memcpy(Color, Raw, sizeof(Color));
            for (UINT Component = 0; Component < 3 && Encode; Component++)
                Color[Component] = (FLOAT)RxWarpLinearToSrgbByte(Color[Component]) * (1.0f / 255.0f);
            RxWarpEncodeColor(DXGI_FORMAT_B8G8R8X8_UNORM, Color, Element);
            memcpy(Source->ShadowMemory + ((SIZE_T)Y * From->Width + X) * 4, Element, 4);
        }
    }
    return S_OK;
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
            if (RxWarp12NeedsShadow(Source, Args->phSurfacesToPresent[Index].SubResourceIndex))
            {
                hr = RxWarp12FillShadow(Object->Device, Source, Args->phSurfacesToPresent[Index].SubResourceIndex);
                if (FAILED(hr))
                    break;
                Present->BroadcastSrcAllocation[0] = Source->ShadowAllocation;
                Present->AddedGpuWork = TRUE;
            }
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
