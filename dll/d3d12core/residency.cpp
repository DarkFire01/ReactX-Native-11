/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Residency control and waits over several fences
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Objects handed to the driver per residency call */
constexpr UINT RxResidencyBatch = 32;

/* A fence to signal once paging work the driver queued is done */
struct RxResidencySignal
{
    RxDevice12* Device;
    D3DKMT_HANDLE PagingFence;
    UINT64 PagingValue;
    ID3D12Fence* Fence;
    UINT64 Value;
};

/*
 * The driver object residency changes act on. Committed resources stand for the heap they own;
 * placed and reserved resources, pipelines and the like have no memory of their own and are left out.
 */
static
BOOLEAN
RxResidencyHandle(
    _In_opt_ ID3D12Pageable* Object,
    _Out_ D3D12DDI_HANDLE_AND_TYPE* Handle)
{
    RxHeap* Heap;
    RxResource* Resource;
    RxDescriptorHeap* Descriptors;
    RxQueryHeap* Queries;

    Handle->Handle = nullptr;
    Handle->Type = D3D12DDI_HT_HEAP;

    if (Object == nullptr)
        return FALSE;

    Heap = RxUnwrap12<RxHeap>(Object);
    if (Heap != nullptr)
    {
        Handle->Handle = Heap->GetDriverHeap().pDrvPrivate;
        return Handle->Handle != nullptr;
    }

    Resource = RxUnwrap12<RxResource>(Object);
    if (Resource != nullptr)
    {
        Handle->Handle = Resource->GetOwnHeap().pDrvPrivate;
        return Handle->Handle != nullptr;
    }

    Descriptors = RxUnwrap12<RxDescriptorHeap>(Object);
    if (Descriptors != nullptr)
    {
        Handle->Handle = Descriptors->GetDriverHandle().pDrvPrivate;
        Handle->Type = D3D12DDI_HT_DESCRIPTOR_HEAP;
        return TRUE;
    }

    Queries = RxUnwrap12<RxQueryHeap>(Object);
    if (Queries != nullptr)
    {
        Handle->Handle = Queries->GetDriverHandle().pDrvPrivate;
        Handle->Type = D3D12DDI_HT_QUERY_HEAP;
        return TRUE;
    }

    return FALSE;
}

/* Fills one batch from the objects left; returns how many objects it went through */
static
UINT
RxFillResidencyBatch(
    _In_ UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects,
    _Out_writes_(RxResidencyBatch) D3D12DDI_HANDLE_AND_TYPE* Batch,
    _Out_ UINT* BatchCount)
{
    UINT Taken = 0;

    *BatchCount = 0;
    while (Taken < NumObjects && *BatchCount < RxResidencyBatch)
    {
        if (RxResidencyHandle(ppObjects[Taken], &Batch[*BatchCount]))
            *BatchCount += 1;
        Taken++;
    }

    return Taken;
}

/* Eviction never fails the call: the driver's answer for each batch is not looked at */
static
VOID
RxEvictObjects(
    _In_ RxDevice12& Device,
    _In_ UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects)
{
    D3D12DDI_HANDLE_AND_TYPE Batch[RxResidencyBatch];
    D3D12DDIARG_EVICT Args = {};
    UINT Done = 0;

    while (Done < NumObjects)
    {
        Done += RxFillResidencyBatch(NumObjects - Done, ppObjects + Done, Batch, &Args.NumObjects);
        Args.pObjects = Batch;
        Args.Flags.EvictOnlyIfNecessary = TRUE;
        Device.Ddi().pfnEvict(Device.Handle(), &Args);
        RxDevice12::TakeDriverError();
    }
}

static
VOID
CALLBACK
RxSignalAfterPaging(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_ PVOID Context)
{
    RxResidencySignal* Signal = static_cast<RxResidencySignal*>(Context);
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    NTSTATUS Status;

    Wait.hDevice = Signal->Device->KernelDevice();
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Signal->PagingFence;
    Wait.FenceValueArray = &Signal->PagingValue;
    Status = RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait);
    if (NT_SUCCESS(Status))
        Signal->Fence->Signal(Signal->Value);
    else
        RX_TRACE(L"paging fence wait failed 0x%08lX", Status);

    Signal->Fence->Release();
    Signal->Device->Release();
    delete Signal;
}

/* MakeResident and EnqueueMakeResident: batches go to the driver, then the paging fence is waited for */
static
HRESULT
RxMakeObjectsResident(
    _In_ RxDevice12& Device,
    _In_ BOOLEAN DenyOverbudget,
    _In_ UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects,
    _In_opt_ ID3D12Fence* Fence,
    _In_ UINT64 FenceValue)
{
    RxPagingQueue12& Paging = Device.GetPagingQueue();
    D3D12DDI_HRTPAGINGQUEUE Queue = { &Paging };
    D3D12DDI_HANDLE_AND_TYPE Batch[RxResidencyBatch];
    UINT64 FenceValues[RxNodeLimit12];
    D3D12DDIARG_MAKERESIDENT_0001 Args = {};
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    RxResidencySignal* Signal;
    BOOLEAN Pending = FALSE;
    UINT64 PagingValue = 0;
    UINT Done = 0;
    UINT Taken;
    HRESULT hr;

    while (Done < NumObjects)
    {
        Taken = RxFillResidencyBatch(NumObjects - Done, ppObjects + Done, Batch, &Args.NumObjects);

        ZeroMemory(FenceValues, sizeof(FenceValues));
        Args.NumAdapters = 1;
        Args.pRTPagingQueue = &Queue;
        Args.pObjects = Batch;
        Args.Flags.CantTrimFurther = !DenyOverbudget;
        Args.pPagingFenceValue = FenceValues;
        Args.WaitMask = 0;

        hr = Device.Ddi().pfnMakeResident(Device.Handle(), &Args);
        if (SUCCEEDED(hr))
            hr = RxDevice12::TakeDriverError();

        if (hr == E_PENDING)
        {
            if ((Args.WaitMask & 1) == 0)
            {
                RX_TRACE(L"driver left paging pending without a fence to wait on");
                Device.MarkRemoved(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
            }
            else if (!Pending || FenceValues[0] > PagingValue)
            {
                PagingValue = FenceValues[0];
                Pending = TRUE;
            }
        }
        else if (FAILED(hr))
        {
            /* What earlier batches made resident is evicted again */
            RxEvictObjects(Device, Done, ppObjects);
            return hr == DXGI_ERROR_DEVICE_REMOVED ? S_OK : hr;
        }

        Done += Taken;
    }

    if (Fence != nullptr)
    {
        if (!Pending)
            return Fence->Signal(FenceValue);

        Signal = new (std::nothrow) RxResidencySignal{ &Device, Paging.SyncObject, PagingValue, Fence, FenceValue };
        if (Signal == nullptr)
            return E_OUTOFMEMORY;

        Device.AddRef();
        Fence->AddRef();
        if (!TrySubmitThreadpoolCallback(RxSignalAfterPaging, Signal, nullptr))
        {
            Fence->Release();
            Device.Release();
            delete Signal;
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    if (!Pending)
        return S_OK;

    Wait.hDevice = Device.KernelDevice();
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Paging.SyncObject;
    Wait.FenceValueArray = &PagingValue;
    hr = RxKernelResult(Device, RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait),
                        "D3DKMTWaitForSynchronizationObjectFromCpu", nullptr);
    return hr == DXGI_ERROR_DEVICE_REMOVED ? S_OK : hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::MakeResident(
    UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects)
{
    if ((ULONG_PTR)ppObjects + (ULONG_PTR)NumObjects * sizeof(*ppObjects) < (ULONG_PTR)ppObjects)
        return E_INVALIDARG;

    return RxMakeObjectsResident(*this, FALSE, NumObjects, ppObjects, nullptr, 0);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::EnqueueMakeResident(
    D3D12_RESIDENCY_FLAGS Flags,
    UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects,
    _In_ ID3D12Fence* pFenceToSignal,
    UINT64 FenceValueToSignal)
{
    if ((ULONG_PTR)ppObjects + (ULONG_PTR)NumObjects * sizeof(*ppObjects) < (ULONG_PTR)ppObjects)
        return E_INVALIDARG;
    if ((Flags & ~D3D12_RESIDENCY_FLAG_DENY_OVERBUDGET) != 0)
        return E_INVALIDARG;

    return RxMakeObjectsResident(*this, (Flags & D3D12_RESIDENCY_FLAG_DENY_OVERBUDGET) != 0, NumObjects, ppObjects,
                                 pFenceToSignal, FenceValueToSignal);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::Evict(
    UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects)
{
    if ((ULONG_PTR)ppObjects + (ULONG_PTR)NumObjects * sizeof(*ppObjects) < (ULONG_PTR)ppObjects)
        return E_INVALIDARG;

    RxEvictObjects(*this, NumObjects, ppObjects);
    return S_OK;
}

/* Only heaps and committed resources carry a priority; anything else loses the device */
HRESULT
STDMETHODCALLTYPE
RxDevice12::SetResidencyPriority(
    UINT NumObjects,
    _In_reads_(NumObjects) ID3D12Pageable* const* ppObjects,
    _In_reads_(NumObjects) const D3D12_RESIDENCY_PRIORITY* pPriorities)
{
    D3DKMT_SETALLOCATIONPRIORITY Priority = {};
    RxHeap* Heap;
    RxResource* Resource;
    UINT Value;
    UINT Index;
    HRESULT hr;

    if ((ULONG_PTR)ppObjects + (ULONG_PTR)NumObjects * sizeof(*ppObjects) < (ULONG_PTR)ppObjects ||
        (ULONG_PTR)pPriorities + (ULONG_PTR)NumObjects * sizeof(*pPriorities) < (ULONG_PTR)pPriorities)
    {
        return E_INVALIDARG;
    }

    for (Index = 0; Index < NumObjects; Index++)
    {
        Heap = RxUnwrap12<RxHeap>(ppObjects[Index]);
        Resource = Heap == nullptr ? RxUnwrap12<RxResource>(ppObjects[Index]) : nullptr;

        Priority.hResource = 0;
        if (Heap != nullptr)
            Priority.hResource = Heap->GetKernelResource();
        else if (Resource != nullptr && Resource->GetOwnHeap().pDrvPrivate != nullptr)
            Priority.hResource = Resource->GetKernelResource();

        if (Priority.hResource == 0)
        {
            RX_TRACE(L"residency priority given to an object without memory of its own");
            MarkRemoved(DXGI_ERROR_INVALID_CALL);
            return E_INVALIDARG;
        }

        Value = pPriorities[Index];
        Priority.hDevice = KmDevice;
        Priority.pPriorities = &Value;
        hr = RxKernelResult(*this, RxGetKmtThunks()->SetAllocationPriority(&Priority), "D3DKMTSetAllocationPriority",
                            nullptr);
        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::SetEventOnMultipleFenceCompletion(
    _In_reads_(NumFences) ID3D12Fence* const* ppFences,
    _In_reads_(NumFences) const UINT64* pFenceValues,
    UINT NumFences,
    D3D12_MULTIPLE_FENCE_WAIT_FLAGS Flags,
    HANDLE hEvent)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    D3DKMT_HANDLE Stack[8];
    D3DKMT_HANDLE* Objects = Stack;
    RxFence* Fence;
    NTSTATUS Status;
    UINT Index;

    if ((Flags & ~D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY) != 0)
        return E_INVALIDARG;

    if (NumFences > _countof(Stack))
    {
        Objects = new (std::nothrow) D3DKMT_HANDLE[NumFences];
        if (Objects == nullptr)
            return E_OUTOFMEMORY;
    }

    for (Index = 0; Index < NumFences; Index++)
    {
        Fence = RxUnwrap12<RxFence>(ppFences[Index]);
        if (Fence == nullptr)
        {
            if (Objects != Stack)
                delete[] Objects;
            return E_INVALIDARG;
        }
        Objects[Index] = Fence->GetSyncObject();
    }

    /* With no event the kernel waits here; with one it signals the event later */
    Wait.hDevice = KmDevice;
    Wait.ObjectCount = NumFences;
    Wait.ObjectHandleArray = Objects;
    Wait.FenceValueArray = pFenceValues;
    Wait.hAsyncEvent = hEvent;
    Wait.Flags.WaitAny = (Flags & D3D12_MULTIPLE_FENCE_WAIT_FLAG_ANY) != 0;
    Status = RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait);

    if (Objects != Stack)
        delete[] Objects;

    return RxKernelResult(*this, Status, "D3DKMTWaitForSynchronizationObjectFromCpu", nullptr);
}
