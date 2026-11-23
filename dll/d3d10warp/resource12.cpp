/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 heaps, resources, GPU addresses and residency
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"

/* Heaps above this size live in our own pages rather than one kernel allocation */
static constexpr UINT64 RxWarp12KernelHeapLimit = 0xFFFEFFFFull;

/* GPU addresses carry the buffer's table index above this bit */
static constexpr UINT RxWarp12AddressShift = 40;

static constexpr UINT RxWarp12Alignment = 65536;
static constexpr UINT RxWarp12MultisampleAlignment = 4 * 1024 * 1024;

static
RxWarp12Device *
RxWarp12GetDevice(
    _In_ D3D12DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarp12Device>(Device);
}

RxWarp12Resource *
RxWarp12GetResource(
    _In_ D3D12DDI_HRESOURCE Resource)
{
    return RxWarpObject<RxWarp12Resource>(Resource);
}

/* Lays out the resource the way the D3D11 executor reads it, with memory of its own for now */
static
HRESULT
RxWarp12LayOut(
    _Inout_ RxWarp12Resource *Resource,
    _In_ const D3D12DDIARG_CREATERESOURCE_0111 *Args)
{
    D3D10DDI_MIPINFO Mips[16] = {};
    D3D11DDIARG_CREATERESOURCE Create = {};
    UINT MipCount = Args->MipLevels ? Args->MipLevels : 1;

    if (MipCount > ARRAYSIZE(Mips))
        return E_INVALIDARG;

    for (UINT Mip = 0; Mip < MipCount; Mip++)
    {
        UINT Width = (UINT)(Args->Width >> Mip);
        UINT Height = Args->Height >> Mip;
        UINT Depth = (Args->ResourceType == D3D12DDI_RT_TEXTURE3D) ? (UINT)(Args->DepthOrArraySize >> Mip) : 1;

        Mips[Mip].TexelWidth = Width ? Width : 1;
        Mips[Mip].TexelHeight = Height ? Height : 1;
        Mips[Mip].TexelDepth = Depth ? Depth : 1;
        Mips[Mip].PhysicalWidth = Mips[Mip].TexelWidth;
        Mips[Mip].PhysicalHeight = Mips[Mip].TexelHeight;
        Mips[Mip].PhysicalDepth = Mips[Mip].TexelDepth;
    }
    if (Args->ResourceType == D3D12DDI_RT_BUFFER)
        Mips[0].TexelWidth = (UINT)Args->Width;

    Create.pMipInfoList = Mips;
    Create.ResourceDimension = (D3D10DDIRESOURCE_TYPE)Args->ResourceType;
    Create.Format = Args->Format;
    Create.SampleDesc = Args->SampleDesc;
    Create.MipLevels = MipCount;
    Create.ArraySize = (Args->ResourceType == D3D12DDI_RT_TEXTURE3D) ? 1 : Args->DepthOrArraySize;
    if (Args->Flags & D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET)
        Create.BindFlags |= D3D10_DDI_BIND_RENDER_TARGET;
    if (Args->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL)
        Create.BindFlags |= D3D10_DDI_BIND_DEPTH_STENCIL;
    if (Args->Flags & D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS)
        Create.BindFlags |= D3D11_DDI_BIND_UNORDERED_ACCESS;
    if (!(Args->Flags & D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL) || (Args->Flags & D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE))
        Create.BindFlags |= D3D10_DDI_BIND_SHADER_RESOURCE;
    return Resource->Storage.Initialize(&Create);
}

/* Points the resource at memory it does not own */
static
VOID
RxWarp12Place(
    _Inout_ RxWarp12Resource *Resource,
    _In_ BYTE *Memory)
{
    if (Resource->Storage.Memory != nullptr && !Resource->Storage.Locked)
        VirtualFree(Resource->Storage.Memory, 0, MEM_RELEASE);
    Resource->Storage.Memory = Memory;
    Resource->Storage.Locked = TRUE;
}

/* Gives buffers the lowest free slot of the device's table, which their GPU address names */
static
VOID
RxWarp12AssignIndex(
    _Inout_ RxWarp12Resource *Resource)
{
    std::vector<RxWarp12Resource *> &Table = Resource->Device->Buffers;

    if (Table.empty())
        Table.push_back(nullptr);
    for (UINT Index = 1; Index < Table.size(); Index++)
    {
        if (Table[Index] == nullptr)
        {
            Table[Index] = Resource;
            Resource->Index = Index;
            return;
        }
    }
    Resource->Index = (UINT)Table.size();
    Table.push_back(Resource);
}

RxWarp12Resource *
RxWarp12ResolveAddress(
    _In_ RxWarp12Device *Device,
    _In_ D3D12DDI_GPU_VIRTUAL_ADDRESS Address,
    _Out_ UINT64 *Offset)
{
    UINT64 Index = (Address >> RxWarp12AddressShift) & 0x7FFFFF;

    *Offset = Address & ((1ull << RxWarp12AddressShift) - 1);
    if (Index == 0 || Index >= Device->Buffers.size())
        return nullptr;
    return Device->Buffers[(SIZE_T)Index];
}

/* One kernel allocation for the whole heap, described the way the kernel driver places it */
static
HRESULT
RxWarp12AllocateHeap(
    _Inout_ RxWarp12Heap *Heap,
    _In_ const D3D12DDIARG_CREATEHEAP_0001 *Args,
    _In_ D3D12DDI_HRTRESOURCE RuntimeResource,
    _In_opt_ const RxWarp12Resource *Resource)
{
    RxWarp12Device *Device = Heap->Device;
    const D3DDDI_DEVICECALLBACKS *Kernel = Device->KernelCallbacks;
    RX_WARP_ALLOCATION_DATA Data = {};
    D3DDDI_ALLOCATIONINFO Info = {};
    D3DDDICB_ALLOCATE Allocate = {};
    D3DDDICB_LOCK Lock = {};
    UINT Flags;
    HRESULT hr;

    if (Resource != nullptr)
    {
        const RX_WARP_SUBRESOURCE *First = &Resource->Storage.Subresources[0];

        Data.Dimension = Resource->Storage.Dimension;
        Data.Width = First->Width;
        Data.Height = First->Height;
        Data.Depth = First->Depth;
        Data.Format = Resource->Storage.Format;
        Data.RowPitch = First->RowPitch;
        Data.ArraySize = Resource->Storage.ArraySize;
        Data.MipLevels = Resource->Storage.MipLevels;
        Data.BindFlags = Resource->Storage.BindFlags;
        Data.SampleCount = Resource->Storage.SampleCount;
        Flags = (Resource->Flags & D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER) ? RX_WARP_ALLOCATION_CROSS_ADAPTER : 0;
        if (Args->Flags & D3D12DDI_HEAP_FLAG_PRIMARY)
        {
            Flags |= RX_WARP_ALLOCATION_PRIMARY;
            Data.VidPnSourceId = (UINT)-1;
        }
    }
    else
    {
        Data.Dimension = D3D10DDIRESOURCE_BUFFER;
        Data.Width = (UINT)Args->ByteSize;
        Data.Height = 1;
        Data.Depth = 1;
        Data.ArraySize = 1;
        Data.MipLevels = 1;
        Data.SampleCount = 1;
        Flags = RX_WARP_ALLOCATION_TILE_POOL;
    }

    /* Every heap is shareable; CPU visible ones are system memory, write combined ones say so */
    Flags |= RX_WARP_ALLOCATION_SHARED | RX_WARP_ALLOCATION_LINEAR;
    if (Args->CPUPageProperty == D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE)
        Flags |= 0x800;
    if (Args->CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
        Flags |= RX_WARP_ALLOCATION_SYSTEM;
    Data.Size = (UINT)Args->ByteSize;
    Data.Flags = Flags;
    Heap->AllocationFlags = Flags;

    if (Args->ByteSize > RxWarp12KernelHeapLimit || Kernel == nullptr || Kernel->pfnAllocateCb == nullptr || Kernel->pfnLockCb == nullptr)
    {
        Heap->Memory = static_cast<BYTE *>(VirtualAlloc(nullptr, (SIZE_T)Args->ByteSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        return Heap->Memory ? S_OK : E_OUTOFMEMORY;
    }

    Info.pPrivateDriverData = &Data;
    Info.PrivateDriverDataSize = sizeof(Data);
    Allocate.hResource = RuntimeResource.handle;
    Allocate.NumAllocations = 1;
    Allocate.pAllocationInfo = &Info;
    hr = Kernel->pfnAllocateCb(Device->RuntimeDevice.handle, &Allocate);
    if (FAILED(hr))
    {
        RX_WARP_TRACE("D3D12 heap allocation of %llu bytes failed: %08lx\n", Args->ByteSize, hr);
        return hr;
    }
    Heap->Allocation = Info.hAllocation;

    Lock.hAllocation = Info.hAllocation;
    Lock.Flags.DonotWait = 1;
    Lock.Flags.IgnoreSync = 1;
    hr = Kernel->pfnLockCb(Device->RuntimeDevice.handle, &Lock);
    if (FAILED(hr))
    {
        RX_WARP_TRACE("D3D12 heap lock failed: %08lx\n", hr);
        return hr;
    }
    Heap->Memory = static_cast<BYTE *>(Lock.pData);
    return S_OK;
}

static
VOID
RxWarp12FreeHeap(
    _In_ RxWarp12Heap *Heap)
{
    const D3DDDI_DEVICECALLBACKS *Kernel = Heap->Device->KernelCallbacks;
    D3DDDICB_UNLOCK Unlock = {};
    D3DDDICB_DEALLOCATE Deallocate = {};

    if (Heap->Allocation == 0)
    {
        if (Heap->Memory != nullptr)
            VirtualFree(Heap->Memory, 0, MEM_RELEASE);
        delete Heap;
        return;
    }
    if (Heap->Memory != nullptr && Kernel->pfnUnlockCb != nullptr)
    {
        Unlock.NumAllocations = 1;
        Unlock.phAllocations = &Heap->Allocation;
        Kernel->pfnUnlockCb(Heap->Device->RuntimeDevice.handle, &Unlock);
    }
    if (Kernel->pfnDeallocateCb != nullptr)
    {
        Deallocate.NumAllocations = 1;
        Deallocate.HandleList = &Heap->Allocation;
        Kernel->pfnDeallocateCb(Heap->Device->RuntimeDevice.handle, &Deallocate);
    }
    delete Heap;
}

D3D12DDI_HEAP_AND_RESOURCE_SIZES
APIENTRY
RxWarp12CalcPrivateHeapAndResourceSizes(
    _In_ D3D12DDI_HDEVICE Device,
    _In_opt_ const D3D12DDIARG_CREATEHEAP_0001 *Heap,
    _In_opt_ const D3D12DDIARG_CREATERESOURCE_0111 *Resource,
    _In_ D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Session)
{
    D3D12DDI_HEAP_AND_RESOURCE_SIZES Sizes = {};

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Session);
    if (Heap != nullptr)
        Sizes.Heap = RxWarpHandleSize;
    if (Resource != nullptr)
        Sizes.Resource = RxWarpHandleSize;
    return Sizes;
}

/*
 * Committed resources come with heap arguments and get a heap of their own. Placed ones
 * name the buffer resource that spans their heap and an offset into it; reserved ones have
 * neither and stay without memory until tiles are mapped.
 */
static
HRESULT
APIENTRY
RxWarp12CreateHeapAndResource(
    _In_ D3D12DDI_HDEVICE Device,
    _In_opt_ const D3D12DDIARG_CREATEHEAP_0001 *HeapArgs,
    _In_ D3D12DDI_HHEAP Heap,
    _In_ D3D12DDI_HRTRESOURCE RuntimeResource,
    _In_opt_ const D3D12DDIARG_CREATERESOURCE_0111 *ResourceArgs,
    _In_opt_ const D3D12DDI_CLEAR_VALUES *ClearValues,
    _In_ D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Session,
    _In_ D3D12DDI_HRESOURCE Resource)
{
    RxWarpLock Lock;
    RxWarp12Device *This = RxWarp12GetDevice(Device);
    RxWarp12Resource *NewResource = nullptr;
    RxWarp12Heap *NewHeap = nullptr;
    HRESULT hr = S_OK;

    UNREFERENCED_PARAMETER(ClearValues);
    UNREFERENCED_PARAMETER(Session);

    if (ResourceArgs != nullptr)
    {
        NewResource = new (std::nothrow) RxWarp12Resource();
        if (NewResource == nullptr)
            return E_OUTOFMEMORY;
        NewResource->Device = This;
        NewResource->Type = ResourceArgs->ResourceType;
        NewResource->Width = ResourceArgs->Width;
        NewResource->Height = ResourceArgs->Height;
        NewResource->DepthOrArraySize = ResourceArgs->DepthOrArraySize;
        NewResource->MipLevels = ResourceArgs->MipLevels;
        NewResource->Flags = ResourceArgs->Flags;
        NewResource->Layout = ResourceArgs->Layout;
        hr = RxWarp12LayOut(NewResource, ResourceArgs);
        if (FAILED(hr))
        {
            RX_WARP_TRACE("D3D12 resource layout failed: %08lx\n", hr);
            delete NewResource;
            return hr;
        }
    }

    if (HeapArgs != nullptr)
    {
        NewHeap = new (std::nothrow) RxWarp12Heap();
        if (NewHeap == nullptr)
        {
            delete NewResource;
            return E_OUTOFMEMORY;
        }
        NewHeap->Device = This;
        NewHeap->Size = HeapArgs->ByteSize;
        NewHeap->CpuPage = HeapArgs->CPUPageProperty;
        NewHeap->Flags = HeapArgs->Flags;
        hr = RxWarp12AllocateHeap(NewHeap, HeapArgs, RuntimeResource, NewResource);
        if (FAILED(hr))
        {
            RxWarp12FreeHeap(NewHeap);
            delete NewResource;
            return hr == E_OUTOFMEMORY ? hr : E_OUTOFMEMORY;
        }
        RxWarpBind(Heap, NewHeap);
    }

    if (NewResource != nullptr)
    {
        const D3D12DDIARG_HRESOURCE_PLACEMENT &Placement = ResourceArgs->ReuseBufferGPUVA.BaseAddress.UMD;
        RxWarp12Resource *Backing = Placement.hResource.pDrvPrivate ? RxWarp12GetResource(Placement.hResource) : nullptr;

        if (NewHeap != nullptr)
        {
            NewResource->Heap = NewHeap;
            RxWarp12Place(NewResource, NewHeap->Memory);
        }
        else if (Backing != nullptr)
        {
            RxWarp12Place(NewResource, Backing->Storage.Memory + Placement.Offset);
        }
        else
        {
            RxWarp12Place(NewResource, nullptr);
        }
        if (NewResource->Type == D3D12DDI_RT_BUFFER)
            RxWarp12AssignIndex(NewResource);
        RxWarpBind(Resource, NewResource);
    }
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyHeapAndResource(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HHEAP Heap,
    _In_ D3D12DDI_HRESOURCE Resource)
{
    RxWarpLock Lock;
    RxWarp12Resource *Object = Resource.pDrvPrivate ? RxWarp12GetResource(Resource) : nullptr;
    RxWarp12Heap *HeapObject = Heap.pDrvPrivate ? RxWarpObject<RxWarp12Heap>(Heap) : nullptr;

    UNREFERENCED_PARAMETER(Device);
    if (Object != nullptr)
    {
        if (Object->Index != 0)
            Object->Device->Buffers[Object->Index] = nullptr;
        delete Object;
    }
    if (HeapObject != nullptr)
        RxWarp12FreeHeap(HeapObject);
}

static
HRESULT
APIENTRY
RxWarp12MapHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HHEAP Heap,
    _Out_ VOID **Data)
{
    RxWarpLock Lock;
    RxWarp12Heap *Object = RxWarpObject<RxWarp12Heap>(Heap);

    UNREFERENCED_PARAMETER(Device);
    *Data = Object->Memory;
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12UnmapHeap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HHEAP Heap)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Heap);
}

/*
 * Sizes come from the executor's layout. Buffers align to 64 KB unless they ask for tight
 * alignment; textures to 16 bytes, or 64 KB for tiled layouts and cross adapter resources.
 */
static
VOID
RxWarp12DescribeAllocation(
    _In_ D3D12DDI_RESOURCE_TYPE Type,
    _In_ D3D12DDI_TEXTURE_LAYOUT Layout,
    _In_ D3D12DDI_RESOURCE_FLAGS_0003 Flags,
    _In_ UINT32 AlignmentRestriction,
    _In_ UINT64 Size,
    _Out_ D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *Info)
{
    UINT32 Alignment = 16;

    if (Layout == D3D12DDI_TL_64KB_TILE_UNDEFINED_SWIZZLE || Layout == D3D12DDI_TL_64KB_TILE_STANDARD_SWIZZLE)
        Alignment = RxWarp12Alignment;
    memset(Info, 0, sizeof(*Info));
    Info->AdditionalDataHeaderAlignment = 1;
    Info->AdditionalDataAlignment = 1;
    Info->Layout = D3D12DDI_TL_DEVICE_DEPENDENT_SWIZZLE_0;
    memset(Info->MipLevelSwizzleTransition, 0xFF, sizeof(Info->MipLevelSwizzleTransition));
    memset(Info->PlaneSliceSwizzleTransition, 0xFF, sizeof(Info->PlaneSliceSwizzleTransition));
    if (Type == D3D12DDI_RT_BUFFER)
    {
        Info->Layout = D3D12DDI_TL_ROW_MAJOR;
        if (Flags & D3D12DDI_RESOURCE_FLAG_0111_USE_TIGHT_ALIGNMENT)
            Alignment = (Alignment > AlignmentRestriction) ? Alignment : AlignmentRestriction;
        else
            Alignment = RxWarp12Alignment;
    }
    if ((Flags & D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER) && Alignment < RxWarp12Alignment)
        Alignment = RxWarp12Alignment;
    Info->ResourceDataAlignment = Alignment;
    Info->ResourceDataSize = (Size + Alignment - 1) & ~(UINT64)(Alignment - 1);
}

static
VOID
APIENTRY
RxWarp12CheckResourceAllocationInfo(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATERESOURCE_0111 *Args,
    _In_ D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS Optimization,
    _In_ UINT32 AlignmentRestriction,
    _In_ UINT VisibleNodeMask,
    _Out_ D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *Info)
{
    RxWarpLock Lock;
    RxWarp12Resource Probe;
    UINT64 Size = 0;

    UNREFERENCED_PARAMETER(Optimization);
    UNREFERENCED_PARAMETER(VisibleNodeMask);
    Probe.Device = RxWarp12GetDevice(Device);
    if (Args->ResourceType == D3D12DDI_RT_BUFFER)
        Size = Args->Width;
    else if (SUCCEEDED(RxWarp12LayOut(&Probe, Args)))
        Size = Probe.Storage.MemorySize;
    RxWarp12DescribeAllocation(Args->ResourceType, Args->Layout, Args->Flags, AlignmentRestriction, Size, Info);
}

static
VOID
APIENTRY
RxWarp12CheckExistingResourceAllocationInfo(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HRESOURCE Resource,
    _Out_ D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 *Info)
{
    RxWarpLock Lock;
    RxWarp12Resource *Object = RxWarp12GetResource(Resource);
    UINT64 Size = (Object->Type == D3D12DDI_RT_BUFFER) ? Object->Width : Object->Storage.MemorySize;

    UNREFERENCED_PARAMETER(Device);
    RxWarp12DescribeAllocation(Object->Type, Object->Layout, Object->Flags, 0, Size, Info);
}

/* Offsets and pitches of one subresource inside the resource's storage */
static
VOID
APIENTRY
RxWarp12CheckSubresourceInfo(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HRESOURCE Resource,
    _In_ UINT Subresource,
    _Out_ D3D12DDI_SUBRESOURCE_INFO *Info)
{
    RxWarpLock Lock;
    RxWarp12Resource *Object = RxWarp12GetResource(Resource);
    RX_WARP_SUBRESOURCE *Sub = Object->Storage.Subresource(Subresource);

    UNREFERENCED_PARAMETER(Device);
    memset(Info, 0, sizeof(*Info));
    if (Sub == nullptr)
        return;
    Info->Offset = Sub->Offset;
    Info->RowStride = Sub->RowPitch;
    Info->DepthStride = Sub->DepthPitch;
}

static
D3D12DDI_GPU_VIRTUAL_ADDRESS
APIENTRY
RxWarp12CheckResourceVirtualAddress(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HRESOURCE Resource)
{
    RxWarpLock Lock;
    RxWarp12Resource *Object = RxWarp12GetResource(Resource);

    UNREFERENCED_PARAMETER(Device);
    return (UINT64)(Object->Index & 0x7FFFFF) << RxWarp12AddressShift;
}

/* The heaps among the objects, as their kernel allocations */
static
VOID
RxWarp12CollectAllocations(
    _In_ UINT Count,
    _In_reads_(Count) const D3D12DDI_HANDLE_AND_TYPE *Objects,
    _Out_ std::vector<D3DKMT_HANDLE> *Allocations)
{
    for (UINT Index = 0; Index < Count; Index++)
    {
        RxWarp12Heap *Heap;

        if (Objects[Index].Type != D3D12DDI_HT_HEAP || Objects[Index].Handle == nullptr)
            continue;
        Heap = *static_cast<RxWarp12Heap **>(Objects[Index].Handle);
        if (Heap != nullptr && Heap->Allocation != 0)
            Allocations->push_back(Heap->Allocation);
    }
}

static
HRESULT
APIENTRY
RxWarp12MakeResident(
    _In_ D3D12DDI_HDEVICE Device,
    _Inout_ D3D12DDIARG_MAKERESIDENT_0001 *Args)
{
    RxWarpLock Lock;
    RxWarp12Device *This = RxWarp12GetDevice(Device);
    std::vector<D3DKMT_HANDLE> Allocations;
    D3DDDI_MAKERESIDENT Resident = {};
    HRESULT hr;

    if (Args->NumObjects == 0 || Args->NumAdapters == 0)
    {
        Args->WaitMask = 0;
        return S_OK;
    }
    *Args->pPagingFenceValue = 0;
    Args->WaitMask = 1;

    RxWarp12CollectAllocations(Args->NumObjects, Args->pObjects, &Allocations);
    if (Allocations.empty() || This->Callbacks->pfnMakeResidentCb == nullptr)
        return S_OK;

    Resident.NumAllocations = (UINT)Allocations.size();
    Resident.AllocationList = Allocations.data();
    Resident.Flags = Args->Flags;
    hr = This->Callbacks->pfnMakeResidentCb(This->RuntimeDevice, Args->pRTPagingQueue[0], &Resident);
    if (SUCCEEDED(hr) || hr == E_PENDING)
        *Args->pPagingFenceValue = Resident.PagingFenceValue;
    if (FAILED(hr))
        RX_WARP_TRACE("D3D12 MakeResident of %u allocations: %08lx\n", Resident.NumAllocations, hr);
    return hr;
}

static
HRESULT
APIENTRY
RxWarp12Evict(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_EVICT *Args)
{
    RxWarpLock Lock;
    RxWarp12Device *This = RxWarp12GetDevice(Device);
    std::vector<D3DKMT_HANDLE> Allocations;
    D3DDDICB_EVICT Evict = {};
    HRESULT hr;

    if (Args->NumObjects == 0)
        return S_OK;
    RxWarp12CollectAllocations(Args->NumObjects, Args->pObjects, &Allocations);
    if (Allocations.empty() || This->Callbacks->pfnEvictCb == nullptr)
        return S_OK;

    Evict.NumAllocations = (UINT)Allocations.size();
    Evict.AllocationList = Allocations.data();
    hr = This->Callbacks->pfnEvictCb(This->RuntimeDevice, &Evict);
    if (FAILED(hr))
        RX_WARP_TRACE("D3D12 Evict of %u allocations: %08lx\n", Evict.NumAllocations, hr);
    return hr;
}

VOID
RxWarp12FillResourceTable(
    _Inout_ D3D12DDI_DEVICE_FUNCS_CORE_0116 *Table)
{
    Table->pfnCalcPrivateHeapAndResourceSizes = RxWarp12CalcPrivateHeapAndResourceSizes;
    Table->pfnCreateHeapAndResource = RxWarp12CreateHeapAndResource;
    Table->pfnDestroyHeapAndResource = RxWarp12DestroyHeapAndResource;
    Table->pfnMapHeap = RxWarp12MapHeap;
    Table->pfnUnmapHeap = RxWarp12UnmapHeap;
    Table->pfnCheckResourceAllocationInfo = RxWarp12CheckResourceAllocationInfo;
    Table->pfnCheckResourceVirtualAddress = RxWarp12CheckResourceVirtualAddress;
    Table->pfnCheckExistingResourceAllocationInfo = RxWarp12CheckExistingResourceAllocationInfo;
    Table->pfnCheckSubresourceInfo = RxWarp12CheckSubresourceInfo;
    Table->pfnMakeResident = RxWarp12MakeResident;
    Table->pfnEvict = RxWarp12Evict;
}
