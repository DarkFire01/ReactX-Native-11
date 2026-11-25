/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Records shared heaps carry, and heaps and resources over opened kernel resources
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "sharedrecord12.hpp"

HRESULT
RxApiResult12(
    _In_ RxDevice12& Device,
    _In_ NTSTATUS Status,
    _In_z_ PCSTR Call)
{
    HRESULT hr = RxKernelResult(Device, Status, Call, nullptr);

    switch (hr)
    {
        case D3DDDIERR_INVALIDHANDLE:
            return E_HANDLE;
        case D3DDDIERR_NOTAVAILABLE:
            return E_ACCESSDENIED;
        case D3DDDIERR_OUTOFVIDEOMEMORY:
            return E_OUTOFMEMORY;
        case D3DDDIERR_WASSTILLDRAWING:
            return DXGI_ERROR_WAS_STILL_DRAWING;
        case D3DDDIERR_DEVICEREMOVED:
            return DXGI_ERROR_DEVICE_REMOVED;
        default:
            return hr;
    }
}

VOID
RxFillSharedRecord12(
    _In_ const D3D12_HEAP_DESC& Heap,
    _In_opt_ const D3D12_RESOURCE_DESC1* Resource,
    _Out_ RxSharedRecord12& Record)
{
    ZeroMemory(&Record, sizeof(Record));

    /* The D3D11 part only says the handle is an NT one, and the size of a plain texture */
    Record.Api11.Size = sizeof(Record.Api11);
    Record.Api11.Constants[RxSharedRecordNtHandleIndex] = 1;
    if (Resource != nullptr && Resource->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D &&
        Resource->MipLevels == 1 && Resource->SampleDesc.Count == 1)
    {
        Record.Api11.Width = (UINT)Resource->Width;
        Record.Api11.Height = Resource->Height;
        Record.Api11.Format = Resource->Format;
        Record.Api11.Constants[RxSharedRecordArraySizeIndex] = Resource->DepthOrArraySize;
    }

    Record.Heap.Size = sizeof(Record.Heap);
    Record.Heap.Version = RxSharedPartVersion12;
    Record.Heap.Part = RxSharedHeapPart12;
    Record.Heap.Desc = Heap;
    Record.Heap.Sharing = RxShareNtHandle12;
    if ((Heap.Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) != 0)
    {
        Record.Api11.Constants[RxSharedRecordCrossAdapterIndex] = 1;
        Record.Heap.Sharing |= RxShareCrossAdapter12;
    }

    Record.Resource.Size = sizeof(Record.Resource);
    Record.Resource.Version = RxSharedPartVersion12;
    Record.Resource.Part = RxSharedResourcePart12;
    if (Resource != nullptr)
    {
        Record.Heap.Sharing |= RxShareCommitted12 | RxShareHasResource12;
        Record.Resource.Desc = *Resource;
        return;
    }

    /* A heap is described as the buffer the driver backs it with */
    Record.Resource.Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Record.Resource.Desc.Alignment = Heap.Alignment;
    Record.Resource.Desc.Width = Heap.SizeInBytes;
    Record.Resource.Desc.Height = 1;
    Record.Resource.Desc.DepthOrArraySize = 1;
    Record.Resource.Desc.MipLevels = 1;
    Record.Resource.Desc.SampleDesc.Count = 1;
    Record.Resource.Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Record.Resource.OpenAs = RxShareOpenAsHeap12;
}

/* An opened kernel resource is closed with the object that took it */
static
VOID
RxAdoptOpenedShare12(
    _In_ const RxOpenedShare12& Opened,
    _Out_ RxRuntimeResource12& Runtime)
{
    Runtime.KmResource = Opened.KmResource;
    Runtime.IsOpened = TRUE;
    Runtime.KeyedMutex = Opened.KeyedMutex;
    Runtime.KeyedMutexSync = Opened.KeyedMutexSync;
}

VOID
RxCloseOpenedShare12(
    _In_ RxDevice12& Device,
    _Inout_ RxRuntimeResource12& Runtime)
{
    const KmtThunks* Kmt = RxGetKmtThunks();

    if (!Runtime.IsOpened)
        return;

    /* Drivers that did not hand the opened resource back through deallocation leave it to the runtime */
    if (Runtime.KmResource != 0 && Kmt->DestroyAllocation2 != nullptr)
    {
        D3DKMT_DESTROYALLOCATION2 Destroy = {};

        Destroy.hDevice = Device.KernelDevice();
        Destroy.hResource = Runtime.KmResource;
        Kmt->DestroyAllocation2(&Destroy);
        Runtime.KmResource = 0;
    }

    if (Runtime.KeyedMutexSync != 0)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};

        Destroy.hSyncObject = Runtime.KeyedMutexSync;
        Kmt->DestroySynchronizationObject(&Destroy);
        Runtime.KeyedMutexSync = 0;
    }

    if (Runtime.KeyedMutex != 0 && Kmt->DestroyKeyedMutex != nullptr)
    {
        D3DKMT_DESTROYKEYEDMUTEX Destroy = {};

        Destroy.hKeyedMutex = Runtime.KeyedMutex;
        Kmt->DestroyKeyedMutex(&Destroy);
        Runtime.KeyedMutex = 0;
    }
}

HRESULT
RxOpenDriverHeap12(
    _In_ RxDevice12& Device,
    _In_ const RxOpenedShare12& Opened,
    _Inout_ RxRuntimeResource12& Runtime,
    _Inout_ RxDriverMemory12& HeapMemory,
    _Inout_ RxDriverMemory12& ResourceMemory,
    _Out_ D3D12DDI_HHEAP* DriverHeap,
    _Out_ D3D12DDI_HRESOURCE* DriverResource)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_OPENHEAP_0003 Args = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES Sizes;
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 NoSession = {};
    D3D12DDI_HRTRESOURCE RuntimeHandle;
    HRESULT hr;

    DriverHeap->pDrvPrivate = nullptr;
    DriverResource->pDrvPrivate = nullptr;

    /* The driver rebuilds the heap and the resource in it from its own private data */
    Args.NumAllocations = Opened.AllocationCount;
    Args.pOpenAllocationInfo = Opened.Allocations;
    Args.hKMResource.handle = Opened.KmResource;
    Args.pPrivateDriverData = Opened.ResourceData;
    Args.PrivateDriverDataSize = Opened.ResourceDataSize;
    Args.InitialResourceState = D3D12DDI_RESOURCE_STATE_COMMON;

    Sizes = Ddi.pfnCalcPrivateOpenedHeapAndResourceSizes(Device.Handle(), &Args, NoSession);
    DriverHeap->pDrvPrivate = HeapMemory.Allocate(Sizes.Heap);
    DriverResource->pDrvPrivate = ResourceMemory.Allocate(Sizes.Resource);
    if (DriverHeap->pDrvPrivate == nullptr || DriverResource->pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    RuntimeHandle.handle = &Runtime;
    hr = Ddi.pfnOpenHeapAndResource(Device.Handle(), &Args, *DriverHeap, RuntimeHandle, NoSession, *DriverResource);
    if (FAILED(hr))
        RX_TRACE(L"driver OpenHeapAndResource failed 0x%08lX", hr);

    return hr;
}

HRESULT
RxHeap::Open(
    _In_ RxDevice12& Device,
    _In_ const RxOpenedShare12& Opened,
    _Outptr_ RxHeap** Heap)
{
    RxRuntimeResource12 Unowned = {};
    RxHeap* Created;
    HRESULT hr;

    *Heap = nullptr;

    Created = new (std::nothrow) RxHeap(Device);
    if (Created == nullptr)
    {
        RxAdoptOpenedShare12(Opened, Unowned);
        RxCloseOpenedShare12(Device, Unowned);
        return E_OUTOFMEMORY;
    }

    hr = Created->InitializeOpened(Opened);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxHeap::InitializeOpened(
    _In_ const RxOpenedShare12& Opened)
{
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Existing = {};
    HRESULT hr;

    RxAdoptOpenedShare12(Opened, Runtime);
    Desc = Opened.HeapDesc;

    hr = RxOpenDriverHeap12(Device, Opened, Runtime, HeapMemory, ResourceMemory, &DriverHeap, &DriverResource);
    if (FAILED(hr))
        return hr;
    IsDriverHeapCreated = TRUE;

    Device.Ddi().pfnCheckExistingResourceAllocationInfo(Device.Handle(), DriverResource, &Existing);
    GpuAddress = Device.Ddi().pfnCheckResourceVirtualAddress(Device.Handle(), DriverResource);
    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_HEAP);
}

HRESULT
RxResource::Open(
    _In_ RxDevice12& Device,
    _In_ const RxOpenedShare12& Opened,
    _Outptr_ RxResource** Resource)
{
    RxRuntimeResource12 Unowned = {};
    RxResource* Created;
    HRESULT hr;

    *Resource = nullptr;

    Created = new (std::nothrow) RxResource(Device);
    if (Created == nullptr)
    {
        RxAdoptOpenedShare12(Opened, Unowned);
        RxCloseOpenedShare12(Device, Unowned);
        return E_OUTOFMEMORY;
    }

    hr = Created->InitializeOpened(Opened);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Resource = Created;
    return S_OK;
}

HRESULT
RxResource::InitializeOpened(
    _In_ const RxOpenedShare12& Opened)
{
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Existing = {};
    HRESULT hr;

    RxAdoptOpenedShare12(Opened, Runtime);
    Desc = Opened.ResourceDesc;
    Properties = Opened.HeapDesc.Properties;
    Flags = Opened.HeapDesc.Flags;

    hr = RxOpenDriverHeap12(Device, Opened, Runtime, HeapMemory, ResourceMemory, &DriverHeap, &DriverResource);
    if (FAILED(hr))
        return hr;
    IsDriverResourceCreated = TRUE;

    Device.Ddi().pfnCheckExistingResourceAllocationInfo(Device.Handle(), DriverResource, &Existing);
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        GpuAddress = Device.Ddi().pfnCheckResourceVirtualAddress(Device.Handle(), DriverResource);

    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_HEAP);
}
