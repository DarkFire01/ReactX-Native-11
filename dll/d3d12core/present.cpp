/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Swapchain buffers and presents a command queue runs for the ReactX dxgi.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "sharedrecord.hpp"

/* A queue's present limit semaphore starts at DXGI's three queued frames and goes up to sixteen */
constexpr LONG RxPresentLimitInitial12 = 3;
constexpr LONG RxPresentLimitMaximum12 = 16;

/* What a flip fence is created with: shared by NT handle, never touched by the GPU */
constexpr UINT RxFlipFenceFlags12 = 0x80000083;

/* What the kernel answers a present with, in the codes DXGI hands back to the application */
static
HRESULT
RxPresentResult12(
    _In_ RxDevice12& Device,
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
        return Status == STATUS_PENDING ? DXGI_STATUS_DDA_WAS_STILL_DRAWING : S_OK;

    if (Device.IsKernelDeviceRemoved() || FAILED(Device.CheckUsable()))
        return DXGI_ERROR_DEVICE_REMOVED;

    switch (Status)
    {
        case STATUS_GRAPHICS_WINDOWDC_NOT_AVAILABLE:
            return S_OK;

        case STATUS_GRAPHICS_NOT_EXCLUSIVE_MODE_OWNER:
        case STATUS_GRAPHICS_PRESENT_OCCLUDED:
        case STATUS_GRAPHICS_PRESENT_DENIED:
            return DXGI_STATUS_OCCLUDED;

        case STATUS_GRAPHICS_PRESENT_MODE_CHANGED:
            return DXGI_STATUS_MODE_CHANGED;

        case STATUS_GRAPHICS_PRESENT_REDIRECTION_DISABLED:
        case STATUS_GRAPHICS_WINDOWLESS_PRESENT_DISABLED:
        case STATUS_GRAPHICS_PRESENT_INVALID_WINDOW:
            return DXGI_STATUS_NO_REDIRECTION;

        case STATUS_GRAPHICS_ALLOCATION_BUSY:
            return DXGI_ERROR_WAS_STILL_DRAWING;

        case STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE:
            return DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE;

        default:
            return RxKernelResult(Device, Status, "D3DKMTPresent", nullptr);
    }
}

/* The queue side */

HRESULT
RxCommandQueue::PreparePresentObjects()
{
    HRESULT hr;
    UINT Index;

    if (PresentList != nullptr)
        return S_OK;

    hr = RxFence::Create(Device, 0, D3D12_FENCE_FLAG_NONE, &PresentFence);
    for (Index = 0; SUCCEEDED(hr) && Index < RxPresentRecordings12; Index++)
        hr = RxCommandAllocator::Create(Device, Desc.Type, &Recordings[Index].Allocator);

    /* A list made without an allocator starts closed */
    if (SUCCEEDED(hr))
        hr = RxCommandList::Create(Device, Desc.Type, nullptr, nullptr, &PresentList);

    if (FAILED(hr))
    {
        RX_TRACE(L"present recordings could not be made 0x%08lX", hr);
        ReleasePresentObjects();
    }

    return hr;
}

VOID
RxCommandQueue::ReleasePresentObjects()
{
    UINT Index;

    /* The GPU has to be done with a recording before its memory goes */
    if (PresentFence != nullptr)
        WaitForPresentFence(PresentFenceValue);

    if (PresentList != nullptr)
        PresentList->Release();
    PresentList = nullptr;

    for (Index = 0; Index < RxPresentRecordings12; Index++)
    {
        if (Recordings[Index].Allocator != nullptr)
            Recordings[Index].Allocator->Release();
        Recordings[Index] = {};
    }

    if (PresentFence != nullptr)
        PresentFence->Release();
    PresentFence = nullptr;

    delete[] PresentDriverData;
    PresentDriverData = nullptr;
    PresentDriverDataSize = 0;

    if (LimitSemaphore != NULL)
        CloseHandle(LimitSemaphore);
    LimitSemaphore = NULL;
}

HRESULT
RxCommandQueue::SignalPresentFence(
    _Out_ UINT64* Value)
{
    HRESULT hr;

    *Value = ++PresentFenceValue;
    hr = Signal(PresentFence, *Value);
    if (FAILED(hr))
        *Value = 0;

    return hr;
}

HRESULT
RxCommandQueue::WaitForPresentFence(
    _In_ UINT64 Value)
{
    if (Value == 0 || PresentFence->GetCompletedValue() >= Value)
        return S_OK;

    return PresentFence->SetEventOnCompletion(Value, NULL);
}

HRESULT
RxCommandQueue::BeginPresentRecording(
    _Outptr_ PresentRecording** Recording)
{
    PresentRecording* Next = &Recordings[NextRecording];
    HRESULT hr;

    *Recording = nullptr;
    NextRecording = (NextRecording + 1) % RxPresentRecordings12;

    hr = WaitForPresentFence(Next->FenceValue);
    if (SUCCEEDED(hr))
        hr = Next->Allocator->Reset();
    if (SUCCEEDED(hr))
        hr = PresentList->Reset(Next->Allocator, nullptr);
    if (FAILED(hr))
        return hr;

    Next->FenceValue = 0;
    *Recording = Next;
    return S_OK;
}

HRESULT
RxCommandQueue::SubmitSwapChainPresent(
    _In_ RxResource& Buffer,
    _Inout_ RxPresentContext& Context)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDI_ARG_PRESENTSURFACE Surface = {};
    D3D12DDIARG_PRESENT_0001 Args = {};
    D3D12DDI_PRESENT_0051 Result = {};
    D3D12DDI_PRESENT_CONTEXTS_0051 Engines = {};
    D3D12DDI_PRESENT_HWQUEUES_0051 Queues = {};
    BOOLEAN UsesHwQueues = HwQueueCount != 0;
    PresentRecording* Recording = nullptr;
    ID3D12CommandList* List;
    UINT DataSize = 0;
    HRESULT hr;

    if (FAILED(Device.CheckUsable()))
        return DXGI_ERROR_DEVICE_REMOVED;

    AcquireSRWLockExclusive(&PresentLock);

    hr = PreparePresentObjects();
    if (SUCCEEDED(hr))
        hr = BeginPresentRecording(&Recording);
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&PresentLock);
        RX_TRACE(L"present list could not be opened 0x%08lX", hr);
        return hr;
    }

    /* The buffer itself goes to the kernel; a redirected flip names its binding in the token */
    Surface.hSurface = Buffer.GetDriverHandle();
    Args.phSurfacesToPresent = &Surface;
    Args.SurfacesToPresent = 1;
    Args.Flags.Blt = 1;
    Args.FlipInterval = DXGI_DDI_FLIP_INTERVAL_IMMEDIATE;
    Args.VidPnSourceID = Context.Template.VidPnSourceId;
    Args.OptimizeForComposition = OptimizeForComposition;

    /* The kernel half of the driver may want private data with the present, sized from the rest */
    if (Ddi.pfnGetPresentPrivateDriverDataSize != nullptr)
        DataSize = Ddi.pfnGetPresentPrivateDriverDataSize(Device.Handle(), &Args);
    if (DataSize > PresentDriverDataSize)
    {
        delete[] PresentDriverData;
        PresentDriverData = new (std::nothrow) BYTE[DataSize];
        PresentDriverDataSize = PresentDriverData != nullptr ? DataSize : 0;
    }
    if (DataSize != 0 && PresentDriverData != nullptr)
    {
        ZeroMemory(PresentDriverData, DataSize);
        Args.PrivateDriverDataSize = DataSize;
        Args.pPrivateDriverData = PresentDriverData;
    }

    PresentList->RecordPresent(DriverQueue, Args, &Result, UsesHwQueues ? nullptr : &Engines,
                               UsesHwQueues ? &Queues : nullptr);
    hr = PresentList->Close();

    /* Work the driver added for the present runs on the queue ahead of the kernel present */
    if (SUCCEEDED(hr) && Result.AddedGpuWork)
    {
        List = PresentList;
        ExecuteCommandLists(1, &List);
        hr = SignalPresentFence(&Recording->FenceValue);
    }

    if (SUCCEEDED(hr))
        hr = SubmitKernelPresent(Context, Args, Result, Engines, Queues);
    else
        RX_TRACE(L"driver present failed 0x%08lX", hr);

    /* Lost redirection drops every queued flip, so all frame slots are free again */
    if (hr == DXGI_STATUS_NO_REDIRECTION && LimitSemaphore != NULL)
    {
        while (WaitForSingleObject(LimitSemaphore, 0) == WAIT_OBJECT_0)
            ;
        ReleaseSemaphore(LimitSemaphore, RxPresentLimitInitial12, nullptr);
    }

    ReleaseSRWLockExclusive(&PresentLock);
    return hr;
}

HRESULT
RxCommandQueue::SubmitKernelPresent(
    _Inout_ RxPresentContext& Context,
    _In_ const D3D12DDIARG_PRESENT_0001& Args,
    _In_ const D3D12DDI_PRESENT_0051& Result,
    _In_ const D3D12DDI_PRESENT_CONTEXTS_0051& Engines,
    _In_ const D3D12DDI_PRESENT_HWQUEUES_0051& Queues)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_SUBMITPRESENTTOHWQUEUE Submit = {};
    D3DKMT_HANDLE QueueHandles[D3DDDI_MAX_BROADCAST_CONTEXT + 1];
    D3DKMT_PRESENT& Present = Submit.PrivatePresentData;
    D3DKMT_FLIPMODEL_PRESENTHISTORYTOKEN& Flip = Present.PresentHistoryToken.Token.Flip;
    D3DDDI_FLIPINTERVAL_TYPE Interval;
    BOOLEAN UsesHwQueues = HwQueueCount != 0;
    NTSTATUS Status;
    UINT Count;
    UINT Index;

    if (UsesHwQueues ? Kmt->SubmitPresentToHwQueue == nullptr : Kmt->Present == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The swapchain filled in everything but what the driver chose */
    Present = Context.Template;
    Present.pSrcSubRects = &Context.SourceRect;
    Present.hSource = Result.BroadcastSrcAllocation[0];
    Present.hDestination = Result.BroadcastDstAllocation[0];
    Present.PrivateDriverDataSize = Args.PrivateDriverDataSize;
    Present.pPrivateDriverData = Args.pPrivateDriverData;

    /* Flips keep their orientation in the flip itself */
    if (Present.Flags.Flip || Present.Flags.RedirectedFlip)
        Present.Flags.Rotate = 0;

    /* The driver may want another sync interval than the application asked for */
    if (Result.SyncIntervalOverrideValid)
    {
        if (Result.SyncIntervalOverride > DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING)
            return E_INVALIDARG;

        Interval = Result.SyncIntervalOverride == DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING
                       ? D3DDDI_FLIPINTERVAL_IMMEDIATE
                       : (D3DDDI_FLIPINTERVAL_TYPE)Result.SyncIntervalOverride;
        if (Present.Flags.Flip && !Present.Flags.DurationValid)
            Present.FlipInterval = Interval;
        if (Present.Flags.RedirectedFlip && Present.PresentHistoryToken.Model == D3DKMT_PM_REDIRECTED_FLIP &&
            !Flip.Flags.UseCustomDuration)
        {
            Flip.FlipInterval = Interval;
            Flip.Flags.IndependentFlipTrueImmediate =
                Result.SyncIntervalOverride == DXGI_DDI_FLIP_INTERVAL_IMMEDIATE_ALLOW_TEARING;
        }
    }

    /* Hardware queue 0 or context 0 carries the main allocations, the rest the broadcast ones */
    if (UsesHwQueues)
    {
        Count = Queues.BroadcastQueueCount;
        if (Count == 0 || Count > _countof(QueueHandles))
            return E_INVALIDARG;

        for (Index = 0; Index < Count; Index++)
            QueueHandles[Index] = (D3DKMT_HANDLE)(ULONG_PTR)Queues.hHwQueues[Index];

        Present.hDevice = Device.KernelDevice();
        Present.BroadcastContextCount = Count - 1;
        Submit.hHwQueues = QueueHandles;
    }
    else
    {
        Count = Engines.BroadcastContextCount;
        if (Count > D3DDDI_MAX_BROADCAST_CONTEXT)
            return E_INVALIDARG;

        Present.hContext = (D3DKMT_HANDLE)(ULONG_PTR)Engines.hContext;
        Present.BroadcastContextCount = Count;
        for (Index = 0; Index < Count; Index++)
            Present.BroadcastContext[Index] = (D3DKMT_HANDLE)(ULONG_PTR)Engines.BroadcastContext[Index];
    }

    if (Present.BroadcastContextCount != 0)
    {
        Present.BroadcastSrcAllocation = const_cast<D3DKMT_HANDLE*>(&Result.BroadcastSrcAllocation[1]);
        Present.BroadcastDstAllocation = const_cast<D3DKMT_HANDLE*>(&Result.BroadcastDstAllocation[1]);
    }

    /* Ordered after everything submitted to the queue before it */
    AcquireSRWLockExclusive(&Lock);
    Status = UsesHwQueues ? Kmt->SubmitPresentToHwQueue(&Submit) : Kmt->Present(&Present);
    ReleaseSRWLockExclusive(&Lock);

    Context.KernelStatus = Status;
    if (NT_SUCCESS(Status))
    {
        OptimizeForComposition = Present.bOptimizeForComposition;
    }
    else
    {
        RX_TRACE(L"%s failed 0x%08lX (flags %08X)", UsesHwQueues ? L"D3DKMTSubmitPresentToHwQueue" : L"D3DKMTPresent",
                 Status, Present.Flags.Value);
    }

    return RxPresentResult12(Device, Status);
}

HRESULT
RxCommandQueue::WaitForIdle()
{
    UINT64 Value = 0;
    HRESULT hr;

    AcquireSRWLockExclusive(&PresentLock);
    hr = PreparePresentObjects();
    if (SUCCEEDED(hr))
        hr = SignalPresentFence(&Value);
    if (SUCCEEDED(hr))
        hr = WaitForPresentFence(Value);
    ReleaseSRWLockExclusive(&PresentLock);

    return hr;
}

HANDLE
RxCommandQueue::GetPresentLimitSemaphore()
{
    /* Made on first use with one count per frame the queue may have in flight */
    AcquireSRWLockExclusive(&PresentLock);
    if (LimitSemaphore == NULL)
    {
        LimitSemaphore = CreateSemaphoreExW(nullptr, RxPresentLimitInitial12, RxPresentLimitMaximum12, nullptr, 0,
                                            SEMAPHORE_MODIFY_STATE | SYNCHRONIZE);
    }
    ReleaseSRWLockExclusive(&PresentLock);

    return LimitSemaphore;
}

/* The face the ReactX dxgi.dll sees */

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Queue.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxQueuePresent::AddRef(VOID)
{
    return Queue.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxQueuePresent::Release(VOID)
{
    return Queue.Release();
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::GetAdapterLuid(
    _Out_ LUID* Luid)
{
    *Luid = Queue.GetOwner().GetAdapter().GetLuid();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::CreatePresentBuffer(
    _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
    _In_ IUnknown* Parent,
    _COM_Outptr_ IUnknown** Buffer)
{
    D3D12_HEAP_PROPERTIES Heap = {};
    D3D12_RESOURCE_DESC1 ResourceDesc = {};
    RxSharedTextureRecord Record = {};
    RxResourceStart12 Start = {};
    RxResource* Created;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Parent);

    *Buffer = nullptr;

    /* A stereo buffer holds one slice per eye */
    ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    ResourceDesc.Width = Desc->Width;
    ResourceDesc.Height = Desc->Height;
    ResourceDesc.DepthOrArraySize = Desc->Stereo ? 2 : 1;
    ResourceDesc.MipLevels = 1;
    ResourceDesc.Format = Desc->Format;
    ResourceDesc.SampleDesc = Desc->SampleDesc;
    ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (Desc->BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
        ResourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    Heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    Heap.CreationNodeMask = 1;
    Heap.VisibleNodeMask = 1;

    /* The compositor opens flip model buffers as D3D11 textures, so they carry a D3D11 record */
    if (Desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || Desc->SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)
    {
        Record.Size = sizeof(Record);
        Record.Version = RxSharedRecordVersion;
        Record.Width = Desc->Width;
        Record.Height = Desc->Height;
        Record.Format = Desc->Format;
        memcpy(Record.Constants, RxSharedRecordConstants, sizeof(Record.Constants));
        Record.Constants[RxSharedRecordArraySizeIndex] = ResourceDesc.DepthOrArraySize;
        Record.Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
        Record.Desc.Width = Desc->Width;
        Record.Desc.Height = Desc->Height;
        Record.Desc.MipLevels = 1;
        Record.Desc.ArraySize = ResourceDesc.DepthOrArraySize;
        Record.Desc.Format = Desc->Format;
        Record.Desc.SampleDesc = Desc->SampleDesc;
        Record.Desc.Usage = D3D11_USAGE_DEFAULT;
        Record.Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (Desc->BufferUsage & DXGI_USAGE_RENDER_TARGET_OUTPUT)
            Record.Desc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        Record.Desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        Start.SharedRecord = &Record;
        Start.SharedRecordSize = sizeof(Record);
    }

    /* Buffers start out in the present state, ready for the first frame, on a shared displayable heap */
    Start.Layout = RxLayoutForState(D3D12_RESOURCE_STATE_PRESENT);
    hr = RxResource::CreateCommitted(Queue.GetOwner(), Heap, D3D12_HEAP_FLAG_ALLOW_DISPLAY | D3D12_HEAP_FLAG_SHARED,
                                     ResourceDesc, Start, nullptr, &Created);
    if (FAILED(hr))
    {
        RX_TRACE(L"swapchain buffer %ux%u format %u could not be made 0x%08lX", Desc->Width, Desc->Height,
                 Desc->Format, hr);
        return hr;
    }

    *Buffer = static_cast<ID3D12Resource*>(Created);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxQueuePresent::DetachPresentBuffer(
    _In_ IUnknown* Buffer)
{
    /* Direct3D 12 buffers do not name the swapchain as their parent */
    UNREFERENCED_PARAMETER(Buffer);
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::OpenPresentTarget(
    _In_ HANDLE SharedHandle,
    _COM_Outptr_ IUnknown** Target)
{
    UNREFERENCED_PARAMETER(SharedHandle);

    /* Direct3D 12 swapchains are flip model only and never copy into a DWM surface */
    *Target = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::ShareBuffer(
    _In_ IUnknown* Buffer,
    _Out_ HANDLE* SharedHandle)
{
    RxResource* Resource = RxUnwrap12<RxResource>(Buffer);
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    D3DKMT_HANDLE KernelResource;

    *SharedHandle = NULL;
    if (Resource == nullptr || Resource->GetKernelResource() == 0)
        return DXGI_ERROR_INVALID_CALL;
    if (Kmt->ShareObjects == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr, nullptr);
    KernelResource = Resource->GetKernelResource();
    return RxHresultFromStatus(Kmt->ShareObjects(1, &KernelResource, &Attributes, GENERIC_ALL, SharedHandle));
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::CreateFlipFence(
    _Out_ UINT* Fence,
    _Out_ HANDLE* SharedHandle)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    *Fence = 0;
    *SharedHandle = NULL;
    if (Kmt->CreateSynchronizationObject2 == nullptr || Kmt->ShareObjects == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Create.hDevice = Queue.GetOwner().KernelDevice();
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.Value = RxFlipFenceFlags12;
    Status = Kmt->CreateSynchronizationObject2(&Create);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr, nullptr);
    Status = Kmt->ShareObjects(1, &Create.hSyncObject, &Attributes, GENERIC_ALL, SharedHandle);
    if (!NT_SUCCESS(Status))
    {
        Destroy.hSyncObject = Create.hSyncObject;
        Kmt->DestroySynchronizationObject(&Destroy);
        return RxHresultFromStatus(Status);
    }

    *Fence = Create.hSyncObject;
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxQueuePresent::DestroyFlipFence(
    _In_ UINT Fence)
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};

    if (Fence == 0)
        return;

    Destroy.hSyncObject = Fence;
    RxGetKmtThunks()->DestroySynchronizationObject(&Destroy);
}

HANDLE
STDMETHODCALLTYPE
RxQueuePresent::GetPresentLimitSemaphore()
{
    return Queue.GetPresentLimitSemaphore();
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::PresentFlip(
    _In_ IUnknown* Buffer,
    _In_ RxPresentContext* Context)
{
    RxResource* Resource = RxUnwrap12<RxResource>(Buffer);

    if (Resource == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    return Queue.SubmitSwapChainPresent(*Resource, *Context);
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::RotateBuffers(
    _In_reads_(Count) IUnknown* const* Buffers,
    _In_ UINT Count)
{
    UNREFERENCED_PARAMETER(Buffers);
    UNREFERENCED_PARAMETER(Count);

    /* Direct3D 12 buffers keep their identities; the back buffer index moves instead */
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::SubmitPresent(
    _In_ IUnknown* Buffer,
    _In_opt_ IUnknown* Target,
    _In_ RxPresentContext* Context)
{
    RxResource* Resource = RxUnwrap12<RxResource>(Buffer);

    if (Resource == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    if (Target != nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    return Queue.SubmitSwapChainPresent(*Resource, *Context);
}

D3DKMT_HANDLE
STDMETHODCALLTYPE
RxQueuePresent::GetKernelDevice()
{
    return Queue.GetOwner().KernelDevice();
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::CreatePrimaryBuffer(
    _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
    _In_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc,
    _In_ IUnknown* Parent,
    _COM_Outptr_ IUnknown** Buffer)
{
    UNREFERENCED_PARAMETER(Desc);
    UNREFERENCED_PARAMETER(PrimaryDesc);
    UNREFERENCED_PARAMETER(Parent);

    /* Primaries belong to the compositor's own swapchain, which is never made on a Direct3D 12 queue */
    *Buffer = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::SetDisplaySurface(
    _In_ IUnknown* Buffer)
{
    UNREFERENCED_PARAMETER(Buffer);
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::PresentPrimary(
    _In_ IUnknown* Buffer,
    _In_ UINT FlipInterval,
    _In_ RxPresentContext* Context)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(FlipInterval);
    UNREFERENCED_PARAMETER(Context);
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::CopyBufferRegions(
    _In_ IUnknown* Destination,
    _In_ IUnknown* Source,
    _In_reads_(Count) const RECT* Rects,
    _In_ UINT Count,
    _In_ POINT Offset)
{
    UNREFERENCED_PARAMETER(Destination);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(Rects);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Offset);

    /* Direct3D 12 does no work the application did not record, so a partial present copies nothing */
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::GetRemovedReason()
{
    return Queue.GetOwner().GetDeviceRemovedReason();
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::BltToTarget(
    _In_ IUnknown* Buffer,
    _In_ IUnknown* Target,
    _In_ const RECT* DestRect)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(DestRect);
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::GetTargetSize(
    _In_ IUnknown* Target,
    _Out_ UINT* Width,
    _Out_ UINT* Height)
{
    UNREFERENCED_PARAMETER(Target);

    *Width = 0;
    *Height = 0;
    return DXGI_ERROR_UNSUPPORTED;
}

BOOL
STDMETHODCALLTYPE
RxQueuePresent::IsBufferDcHeld(
    _In_ IUnknown* Buffer)
{
    /* Direct3D 12 swapchains can not be GDI compatible */
    UNREFERENCED_PARAMETER(Buffer);
    return FALSE;
}

HRESULT
STDMETHODCALLTYPE
RxQueuePresent::WaitForIdle()
{
    return Queue.WaitForIdle();
}
