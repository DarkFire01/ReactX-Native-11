/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12CommandQueue
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Context flags a driver may ask for; anything at or above this is reserved */
constexpr UINT RxReservedContextFlags = 0x80;

HRESULT
RxCommandQueue::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_COMMAND_QUEUE_DESC& Desc,
    _Outptr_ RxCommandQueue** Queue)
{
    RxCommandQueue* Created;
    HRESULT hr;

    *Queue = nullptr;

    hr = Validate(Desc);
    if (FAILED(hr))
        return hr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxCommandQueue(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    /* Grouped queues share the group's contexts; a group comes before the queue it serves */
    hr = Device.JoinSchedulingGroup(Desc.Type, 1, GUID_NULL, &Created->Group);
    if (SUCCEEDED(hr))
        hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Queue = Created;
    return S_OK;
}

HRESULT
RxCommandQueue::Validate(
    _In_ const D3D12_COMMAND_QUEUE_DESC& Desc)
{
    if (RxQueueFlagsOf(Desc.Type) == D3D12DDI_COMMAND_QUEUE_FLAG_NONE || Desc.Type == D3D12_COMMAND_LIST_TYPE_BUNDLE)
        return E_INVALIDARG;
    if (Desc.Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL && Desc.Priority != D3D12_COMMAND_QUEUE_PRIORITY_HIGH &&
        Desc.Priority != D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME)
    {
        return E_INVALIDARG;
    }
    if ((Desc.Flags & ~D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT) != 0 || Desc.NodeMask > 1)
        return E_INVALIDARG;

    return S_OK;
}

HRESULT
RxCommandQueue::Initialize(
    _In_ const D3D12_COMMAND_QUEUE_DESC& QueueDesc)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATECOMMANDQUEUE_0050 Args = {};
    D3D12DDI_HRTCOMMANDQUEUE Runtime;
    HRESULT hr;

    Desc = QueueDesc;
    Desc.NodeMask = 1;

    Args.QueueFlags = RxQueueFlagsOf(Desc.Type);
    Args.NodeMask = 1;
    if (Group != nullptr)
        Args.SchedulingGroup = Group->GetDriverHandle();

    Memory.Allocate(Ddi.pfnCalcPrivateCommandQueueSize(Device.Handle(), &Args));
    DriverQueue.pDrvPrivate = Memory.Get();
    if (DriverQueue.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    /* The driver makes its contexts and hardware queues through callbacks while this runs */
    Runtime.handle = this;
    CreatingThread = GetCurrentThreadId();
    hr = Ddi.pfnCreateCommandQueue(Device.Handle(), &Args, DriverQueue, Runtime);
    CreatingThread = 0;
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateCommandQueue failed 0x%08lX", hr);
        return hr;
    }
    IsDriverQueueCreated = TRUE;

    /* A grouped queue's contexts belong to the group, which keeps its own priority */
    if (Group != nullptr)
        return S_OK;

    return ApplyPriority();
}

/* Priority calls only fail on bad parameters or missing rights; other statuses pass */
static
HRESULT
RxPriorityResult(
    _In_ NTSTATUS Status)
{
    if (Status == STATUS_INVALID_PARAMETER)
        return E_INVALIDARG;
    if (Status == STATUS_ACCESS_DENIED)
        return E_ACCESSDENIED;

    return Status == STATUS_SUCCESS ? S_OK : S_FALSE;
}

HRESULT
RxCommandQueue::ApplyPriority()
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    HRESULT hr;
    UINT Index;

    if (Desc.Priority == D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME && Kmt->SetContextSchedulingPriority != nullptr)
    {
        for (Index = 0; Index < ContextCount; Index++)
        {
            D3DKMT_SETCONTEXTSCHEDULINGPRIORITY Global;

            /* The realtime band, top level */
            Global.hContext = Contexts[Index];
            Global.Priority = 0x4000001E;
            hr = RxPriorityResult(Kmt->SetContextSchedulingPriority(&Global));
            if (FAILED(hr))
                return hr;
        }
    }

    if (Kmt->SetContextInProcessSchedulingPriority != nullptr)
    {
        for (Index = 0; Index < ContextCount; Index++)
        {
            D3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY InProcess;

            InProcess.hContext = Contexts[Index];
            InProcess.Priority = Desc.Priority != D3D12_COMMAND_QUEUE_PRIORITY_NORMAL ? 1 : 0;
            hr = RxPriorityResult(Kmt->SetContextInProcessSchedulingPriority(&InProcess));
            if (FAILED(hr))
                return hr;
        }
    }

    return S_OK;
}

RxCommandQueue::~RxCommandQueue()
{
    D3DKMT_DESTROYHWQUEUE DestroyHwQueue;
    D3DKMT_DESTROYCONTEXT DestroyContext;
    UINT Index;

    if (IsDriverQueueCreated)
        Device.Ddi().pfnDestroyCommandQueue(Device.Handle(), DriverQueue);

    /* The runtime owns the kernel objects the driver asked for */
    for (Index = 0; Index < HwQueueCount; Index++)
    {
        DestroyHwQueue.hHwQueue = HwQueues[Index];
        RxGetKmtThunks()->DestroyHwQueue(&DestroyHwQueue);
    }

    for (Index = 0; Index < ContextCount; Index++)
    {
        DestroyContext.hContext = Contexts[Index];
        RxGetKmtThunks()->DestroyContext(&DestroyContext);
    }

    if (Group != nullptr)
        Device.LeaveSchedulingGroup(Group, Desc.Type);
}

HRESULT
STDMETHODCALLTYPE
RxCommandQueue::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12CommandQueue) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12CommandQueue*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Software contexts and hardware queues on group contexts do not mix in one queue */
BOOLEAN
RxCommandQueue::NoteContextKind(
    _In_ D3DDDI_CREATECONTEXTFLAGS Flags)
{
    if (Flags.HwQueueSupported)
        return TRUE;
    if (UsesGroupContexts)
        return FALSE;

    HasSoftwareContexts = TRUE;
    return TRUE;
}

BOOLEAN
RxCommandQueue::OwnsContext(
    _In_ D3DKMT_HANDLE Context) const
{
    UINT Index;

    for (Index = 0; Index < ContextCount; Index++)
    {
        if (Contexts[Index] == Context)
            return TRUE;
    }

    return FALSE;
}

/* Driver callbacks during queue creation */

HRESULT
APIENTRY
RxCommandQueue::CreateContextCb(
    _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
    _Inout_ D3DDDICB_CREATECONTEXT* Args)
{
    RxCommandQueue* Self = static_cast<RxCommandQueue*>(hRTCommandQueue.handle);
    D3DKMT_CREATECONTEXT Create = {};
    NTSTATUS Status;

    if (Self->CreatingThread != GetCurrentThreadId() || Args->Flags.Value >= RxReservedContextFlags ||
        Self->ContextCount == RxMaxQueueEngines12)
    {
        return E_INVALIDARG;
    }

    if (!Self->NoteContextKind(Args->Flags))
        return E_INVALIDARG;

    Create.hDevice = Self->Device.KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags.HwQueueSupported = Args->Flags.HwQueueSupported;
    if ((Self->Desc.Flags & D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT) != 0)
        Create.Flags.DisableGpuTimeout = TRUE;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = D3DKMT_CLIENTHINT_DX12;

    Status = RxGetKmtThunks()->CreateContext(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContext failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Self->Contexts[Self->ContextCount++] = Create.hContext;
    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    Args->pCommandBuffer = Create.pCommandBuffer;
    Args->CommandBufferSize = Create.CommandBufferSize;
    Args->pAllocationList = Create.pAllocationList;
    Args->AllocationListSize = Create.AllocationListSize;
    Args->pPatchLocationList = Create.pPatchLocationList;
    Args->PatchLocationListSize = Create.PatchLocationListSize;
    Args->CommandBuffer = Create.CommandBuffer;
    return S_OK;
}

HRESULT
APIENTRY
RxCommandQueue::CreateContextVirtualCb(
    _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
    _Inout_ D3DDDICB_CREATECONTEXTVIRTUAL* Args)
{
    RxCommandQueue* Self = static_cast<RxCommandQueue*>(hRTCommandQueue.handle);
    D3DKMT_CREATECONTEXTVIRTUAL Create = {};
    NTSTATUS Status;

    if (Self->CreatingThread != GetCurrentThreadId() || Args->Flags.Value >= RxReservedContextFlags ||
        Self->ContextCount == RxMaxQueueEngines12 || RxGetKmtThunks()->CreateContextVirtual == nullptr)
    {
        return E_INVALIDARG;
    }

    if (!Self->NoteContextKind(Args->Flags))
        return E_INVALIDARG;

    Create.hDevice = Self->Device.KernelDevice();
    Create.NodeOrdinal = Args->NodeOrdinal;
    Create.EngineAffinity = Args->EngineAffinity;
    Create.Flags.HwQueueSupported = Args->Flags.HwQueueSupported;
    if ((Self->Desc.Flags & D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT) != 0)
        Create.Flags.DisableGpuTimeout = TRUE;
    Create.pPrivateDriverData = Args->pPrivateDriverData;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.ClientHint = D3DKMT_CLIENTHINT_DX12;

    Status = RxGetKmtThunks()->CreateContextVirtual(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateContextVirtual failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Self->Contexts[Self->ContextCount++] = Create.hContext;
    Args->hContext = (HANDLE)(ULONG_PTR)Create.hContext;
    return S_OK;
}

/* Contexts live as long as their queue; the queue frees them */
HRESULT
APIENTRY
RxCommandQueue::DestroyContextCb(
    _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
    _In_ const D3DDDICB_DESTROYCONTEXT* Args)
{
    return S_OK;
}

HRESULT
APIENTRY
RxCommandQueue::CreateHwQueueCb(
    _In_ D3D12DDI_HRTCOMMANDQUEUE hRTCommandQueue,
    _Inout_ D3DDDICB_CREATEHWQUEUE* Args)
{
    RxCommandQueue* Self = static_cast<RxCommandQueue*>(hRTCommandQueue.handle);
    D3DKMT_CREATEHWQUEUE Create = {};
    D3DKMT_HANDLE Context = (D3DKMT_HANDLE)(ULONG_PTR)Args->hHwContext;
    NTSTATUS Status;

    if (Self->CreatingThread != GetCurrentThreadId() || Args->Flags.Value >= RxReservedContextFlags ||
        Self->HwQueueCount == RxMaxQueueEngines12 || RxGetKmtThunks()->CreateHwQueue == nullptr)
    {
        return E_INVALIDARG;
    }

    /* A hardware queue may sit on a group context only when the queue made no software contexts */
    if (!Self->OwnsContext(Context))
    {
        if (Self->Group == nullptr || !Self->Group->OwnsContext(Context) || Self->HasSoftwareContexts)
            return E_INVALIDARG;
        Self->UsesGroupContexts = TRUE;
    }

    Create.hHwContext = Context;
    Create.Flags = Args->Flags;
    Create.PrivateDriverDataSize = Args->PrivateDriverDataSize;
    Create.pPrivateDriverData = Args->pPrivateDriverData;

    Status = RxGetKmtThunks()->CreateHwQueue(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateHwQueue failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    Self->HwQueues[Self->HwQueueCount++] = Create.hHwQueue;
    Args->hHwQueue = (HANDLE)(ULONG_PTR)Create.hHwQueue;
    Args->hHwQueueProgressFence = Create.hHwQueueProgressFence;
    Args->HwQueueProgressFenceCPUVirtualAddress = Create.HwQueueProgressFenceCPUVirtualAddress;
    Args->HwQueueProgressFenceGPUVirtualAddress = Create.HwQueueProgressFenceGPUVirtualAddress;
    return S_OK;
}

/* ID3D12CommandQueue */

VOID
STDMETHODCALLTYPE
RxCommandQueue::ExecuteCommandLists(
    _In_ UINT NumCommandLists,
    _In_reads_(NumCommandLists) ID3D12CommandList* const* ppCommandLists)
{
    D3D12DDI_HCOMMANDLIST Stack[16];
    D3D12DDI_HCOMMANDLIST* Handles = Stack;
    UINT Index;

    if (NumCommandLists == 0 || ppCommandLists == nullptr)
        return;

    if (NumCommandLists > _countof(Stack))
    {
        Handles = new (std::nothrow) D3D12DDI_HCOMMANDLIST[NumCommandLists];
        if (Handles == nullptr)
            return;
    }

    /* Open, failed, foreign or mismatched lists make the whole call a no-op */
    for (Index = 0; Index < NumCommandLists; Index++)
    {
        RxCommandList* List = RxUnwrap12<RxCommandList>(ppCommandLists[Index]);

        if (List == nullptr || List->IsRecording() || FAILED(List->GetError()) || List->GetType() != Desc.Type)
        {
            RX_TRACE(L"ExecuteCommandLists: list %u can not run on this queue", Index);
            if (Handles != Stack)
                delete[] Handles;
            return;
        }

        Handles[Index] = List->GetDriverHandle();
    }

    AcquireSRWLockExclusive(&Lock);
    Device.QueueDdi().pfnExecuteCommandLists(DriverQueue, NumCommandLists, Handles);
    ReleaseSRWLockExclusive(&Lock);

    if (Handles != Stack)
        delete[] Handles;
}

VOID
STDMETHODCALLTYPE
RxCommandQueue::SetMarker(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

VOID
STDMETHODCALLTYPE
RxCommandQueue::BeginEvent(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

VOID
STDMETHODCALLTYPE
RxCommandQueue::EndEvent(VOID)
{
}

/* Monitored fences are signaled and waited on by the kernel, after the work already queued */
HRESULT
STDMETHODCALLTYPE
RxCommandQueue::Signal(
    ID3D12Fence* pFence,
    UINT64 Value)
{
    RxFence* Fence = RxUnwrap12<RxFence>(pFence);
    D3DKMT_HANDLE SyncObject;
    NTSTATUS Status;

    if (Fence == nullptr)
        return E_INVALIDARG;

    SyncObject = Fence->GetSyncObject();
    AcquireSRWLockExclusive(&Lock);

    if (HwQueueCount != 0)
    {
        D3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE Submit = {};

        Submit.Flags.AllowFenceRewind = TRUE;
        Submit.BroadcastHwQueueCount = HwQueueCount;
        Submit.BroadcastHwQueueArray = HwQueues;
        Submit.ObjectCount = 1;
        Submit.ObjectHandleArray = &SyncObject;
        Submit.FenceValueArray = &Value;
        Status = RxGetKmtThunks()->SubmitSignalSyncObjectsToHwQueue(&Submit);
    }
    else
    {
        D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU2 Signal = {};

        Signal.ObjectCount = 1;
        Signal.ObjectHandleArray = &SyncObject;
        Signal.Flags.AllowFenceRewind = TRUE;
        Signal.BroadcastContextCount = ContextCount;
        Signal.BroadcastContextArray = Contexts;
        Signal.MonitoredFenceValueArray = &Value;
        Status = RxGetKmtThunks()->SignalSynchronizationObjectFromGpu2(&Signal);
    }

    ReleaseSRWLockExclusive(&Lock);

    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"queue signal failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxCommandQueue::Wait(
    ID3D12Fence* pFence,
    UINT64 Value)
{
    RxFence* Fence = RxUnwrap12<RxFence>(pFence);
    D3DKMT_HANDLE SyncObject;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT Index;

    if (Fence == nullptr)
        return E_INVALIDARG;

    SyncObject = Fence->GetSyncObject();
    AcquireSRWLockExclusive(&Lock);

    if (HwQueueCount != 0)
    {
        for (Index = 0; Index < HwQueueCount && NT_SUCCESS(Status); Index++)
        {
            D3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE Submit = {};

            Submit.hHwQueue = HwQueues[Index];
            Submit.ObjectCount = 1;
            Submit.ObjectHandleArray = &SyncObject;
            Submit.FenceValueArray = &Value;
            Status = RxGetKmtThunks()->SubmitWaitForSyncObjectsToHwQueue(&Submit);
        }
    }
    else
    {
        for (Index = 0; Index < ContextCount && NT_SUCCESS(Status); Index++)
        {
            D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU GpuWait = {};

            GpuWait.hContext = Contexts[Index];
            GpuWait.ObjectCount = 1;
            GpuWait.ObjectHandleArray = &SyncObject;
            GpuWait.MonitoredFenceValueArray = &Value;
            Status = RxGetKmtThunks()->WaitForSynchronizationObjectFromGpu(&GpuWait);
        }
    }

    ReleaseSRWLockExclusive(&Lock);
    return RxHresultFromStatus(Status);
}

/* The clock of node 0, the only node this runtime drives */
static
HRESULT
RxQueryClock(
    _In_ RxDevice12& Device,
    _Out_ D3DKMT_QUERYCLOCKCALIBRATION* Query)
{
    ZeroMemory(Query, sizeof(*Query));
    if (RxGetKmtThunks()->QueryClockCalibration == nullptr)
        return E_NOTIMPL;

    Query->hAdapter = Device.KernelAdapter();
    Query->NodeOrdinal = 0;
    Query->PhysicalAdapterIndex = 0;
    return RxHresultFromStatus(RxGetKmtThunks()->QueryClockCalibration(Query));
}

HRESULT
STDMETHODCALLTYPE
RxCommandQueue::GetTimestampFrequency(
    _Out_ UINT64* pFrequency)
{
    D3DKMT_QUERYCLOCKCALIBRATION Query;
    HRESULT hr;

    if (pFrequency == nullptr)
        return E_INVALIDARG;

    hr = RxQueryClock(Device, &Query);
    *pFrequency = SUCCEEDED(hr) ? Query.ClockData.GpuFrequency : 0;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxCommandQueue::GetClockCalibration(
    _Out_ UINT64* pGpuTimestamp,
    _Out_ UINT64* pCpuTimestamp)
{
    D3DKMT_QUERYCLOCKCALIBRATION Query;
    HRESULT hr;

    if (pGpuTimestamp == nullptr || pCpuTimestamp == nullptr)
        return E_INVALIDARG;

    hr = RxQueryClock(Device, &Query);
    *pGpuTimestamp = SUCCEEDED(hr) ? Query.ClockData.GpuClockCounter : 0;
    *pCpuTimestamp = SUCCEEDED(hr) ? Query.ClockData.CpuClockCounter : 0;
    return hr;
}

D3D12_COMMAND_QUEUE_DESC
STDMETHODCALLTYPE
RxCommandQueue::GetDesc(VOID)
{
    return Desc;
}
