/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What every video command list records: recording, barriers, queries and extension commands
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"
#include "commandlist_internal.hpp"

/* Driver handle arrays a recorded command points at, freed when the list starts over */
struct RxScratchBlock
{
    RxScratchBlock* Next;
};

template <typename Interface>
RxVideoList12<Interface>::~RxVideoList12()
{
    if (Allocator != nullptr)
    {
        Allocator->Unhold();
        Allocator->Release();
    }

    if (IsDriverListCreated)
        this->Device.Ddi().pfnDestroyCommandList(this->Device.Handle(), DriverList);

    ReleaseScratch();
}

template <typename Interface>
HRESULT
RxVideoList12<Interface>::Initialize(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_opt_ RxCommandAllocator* FirstAllocator)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = this->Device.Ddi();
    D3D12DDIARG_CREATE_COMMAND_LIST_0040 Args = {};
    D3D12DDI_HRTCOMMANDLIST Runtime;
    SIZE_T Size;
    HRESULT hr;

    Type = ListType;

    Args.Type = D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    Args.QueueFlags = RxQueueFlagsOf(Type);
    Args.NodeMask = 1;
    Size = Ddi.pfnCalcPrivateCommandListSize(this->Device.Handle(), &Args);

    hr = this->Device.SessionSupport(0, &TakesSessions);
    if (FAILED(hr))
        return hr;

    if (FirstAllocator != nullptr)
    {
        hr = Claim(FirstAllocator);
        if (FAILED(hr))
            return hr;
    }

    DriverList.pDrvPrivate = Memory.Allocate(Size);
    if (DriverList.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;
    Args.ID = ListId;

    /* The driver names the kind's table from inside the call */
    Runtime.handle = static_cast<RxListCallbacks12*>(this);
    hr = Ddi.pfnCreateCommandList(this->Device.Handle(), &Args, DriverList, Runtime);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateCommandList for video type %d failed 0x%08lX", Type, hr);
        return hr;
    }
    IsDriverListCreated = TRUE;

    if (Table == nullptr)
        Table = Video().ListTable(RxVideoKindOf(Type));

    if (FirstAllocator != nullptr)
        Begin();
    return S_OK;
}

template <typename Interface>
HRESULT
RxVideoList12<Interface>::Claim(
    _In_ RxCommandAllocator* NewAllocator)
{
    if (!NewAllocator->Acquire())
        return E_INVALIDARG;

    Allocator = NewAllocator;
    Allocator->AddRef();
    ListId = this->Device.NextCommandListId();
    return S_OK;
}

/* Starts recording into the claimed allocator, then clears what the last recording left set */
template <typename Interface>
VOID
RxVideoList12<Interface>::Begin()
{
    D3D12DDIARG_RESETCOMMANDLIST_0040 Args = {};

    ReleaseScratch();

    Args.hDrvCommandRecorder = Allocator->GetRecorder();
    Args.ID = ListId;
    Table->pfnResetCommandList(DriverList, &Args);

    Recording = TRUE;
    OpenQueries = 0;
    ClearState();
}

template <typename Interface>
VOID
RxVideoList12<Interface>::Fail(
    _In_ HRESULT hr)
{
    RX_TRACE(L"video command list failed with 0x%08lX", hr);
    if (SUCCEEDED(Error))
        Error = hr;
}

template <typename Interface>
_Ret_maybenull_
VOID*
RxVideoList12<Interface>::Scratch(
    _In_ SIZE_T Size)
{
    RxScratchBlock* Block = static_cast<RxScratchBlock*>(malloc(sizeof(RxScratchBlock) + Size));

    if (Block == nullptr)
    {
        Fail(E_OUTOFMEMORY);
        return nullptr;
    }

    Block->Next = ScratchBlocks;
    ScratchBlocks = Block;
    ZeroMemory(Block + 1, Size);
    return Block + 1;
}

template <typename Interface>
VOID
RxVideoList12<Interface>::ReleaseScratch()
{
    while (ScratchBlocks != nullptr)
    {
        RxScratchBlock* Next = ScratchBlocks->Next;

        free(ScratchBlocks);
        ScratchBlocks = Next;
    }
}

/* Driver callbacks */

template <typename Interface>
VOID
RxVideoList12<Interface>::OnDriverError(
    _In_ HRESULT hr)
{
    RX_TRACE(L"driver failed a video command list with 0x%08lX", hr);

    if (hr == D3DDDIERR_OUTOFVIDEOMEMORY)
        hr = E_OUTOFMEMORY;
    if (FAILED(hr) && hr != E_OUTOFMEMORY)
    {
        if (hr == D3DDDIERR_DEVICEREMOVED)
            this->Device.MarkRemoved(DXGI_ERROR_DEVICE_REMOVED);
        else if (hr == D3DDDIERR_APPLICATIONERROR)
            this->Device.MarkRemoved(DXGI_ERROR_INVALID_CALL);
        else
            this->Device.MarkRemoved(DXGI_ERROR_DRIVER_INTERNAL_ERROR);
        hr = DXGI_ERROR_DEVICE_REMOVED;
    }

    Fail(hr);
}

template <typename Interface>
VOID
RxVideoList12<Interface>::OnDriverTable(
    _In_ D3D12DDI_HRTTABLE DriverTable)
{
    Table = static_cast<const RxVideoListTable12*>(DriverTable.handle);
}

/* IUnknown and ID3D12CommandList */

template <typename Interface>
HRESULT
STDMETHODCALLTYPE
RxVideoList12<Interface>::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    static const IID* const Decode[] =
    {
        &__uuidof(ID3D12VideoDecodeCommandList), &__uuidof(ID3D12VideoDecodeCommandList1),
        &__uuidof(ID3D12VideoDecodeCommandList2), &__uuidof(ID3D12VideoDecodeCommandList3),
    };
    static const IID* const Process[] =
    {
        &__uuidof(ID3D12VideoProcessCommandList), &__uuidof(ID3D12VideoProcessCommandList1),
        &__uuidof(ID3D12VideoProcessCommandList2), &__uuidof(ID3D12VideoProcessCommandList3),
    };
    static const IID* const Encode[] =
    {
        &__uuidof(ID3D12VideoEncodeCommandList), &__uuidof(ID3D12VideoEncodeCommandList1),
        &__uuidof(ID3D12VideoEncodeCommandList2), &__uuidof(ID3D12VideoEncodeCommandList3),
    };
    const IID* const* Faces = Type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ? Process
                            : Type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE  ? Encode
                                                                            : Decode;
    UINT Index;

    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = this->Identity();
        this->AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object))
    {
        *ppvObject = static_cast<ID3D12Object*>(this->ChildFace());
        this->AddRef();
        return S_OK;
    }

    if (riid == RxVideoListBase12::PrivateId)
    {
        *ppvObject = static_cast<RxVideoListBase12*>(this);
        this->AddRef();
        return S_OK;
    }

    for (Index = 0; Index < 4; Index++)
    {
        if (riid == *Faces[Index])
            break;
    }

    if (Index < 4 || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12CommandList))
    {
        *ppvObject = static_cast<Interface*>(this);
        this->AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

template <typename Interface>
HRESULT
STDMETHODCALLTYPE
RxVideoList12<Interface>::Close(VOID)
{
    HRESULT hr;

    if (!Recording)
        return E_FAIL;

    /* A query begun and not ended fails the list */
    if (OpenQueries != 0)
        Fail(E_FAIL);

    Table->pfnCloseCommandList(DriverList);
    Recording = FALSE;
    ListId = UINT64_MAX;

    Allocator->Unhold();
    Allocator->Release();
    Allocator = nullptr;

    hr = RxDevice12::TakeDriverError();
    if (FAILED(Error))
        return Error;
    return FAILED(hr) ? hr : S_OK;
}

template <typename Interface>
HRESULT
STDMETHODCALLTYPE
RxVideoList12<Interface>::Reset(
    _In_ ID3D12CommandAllocator* pAllocator)
{
    RxCommandAllocator* NewAllocator = RxUnwrap12<RxCommandAllocator>(pAllocator);
    HRESULT hr;

    /* A list that failed stays failed; only a new list records again */
    if (Recording)
        return E_FAIL;
    if (FAILED(Error) || NewAllocator == nullptr || NewAllocator->GetType() != Type)
        return E_INVALIDARG;

    hr = Claim(NewAllocator);
    if (FAILED(hr))
        return hr;

    Begin();
    return S_OK;
}

/* Predication goes off and the protected session is dropped, as when recording starts */
template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::ClearState(VOID)
{
    D3D12DDI_HRESOURCE NoResource = {};
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 NoSession = {};

    if (!Recording)
    {
        Fail(E_INVALIDARG);
        return;
    }

    Table->pfnSetPredication(DriverList, NoResource, 0, D3D12DDI_PREDICATION_OP_EQUAL_ZERO);

    if (!TakesSessions)
        return;

    if (OpenQueries != 0)
        Fail(E_FAIL);
    Session = nullptr;
    Table->pfnSetProtectedResourceSession(DriverList, NoSession);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::ResourceBarrier(
    _In_ UINT NumBarriers,
    _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER* pBarriers)
{
    if (!Records())
        return;

    RxRecordResourceBarriers(this->Device, Type, DriverList, Table->pfnResourceBarrier, Table->pfnBarrier, NumBarriers,
                             pBarriers);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::Barrier(
    UINT32 NumBarrierGroups,
    _In_reads_(NumBarrierGroups) const D3D12_BARRIER_GROUP* pBarrierGroups)
{
    HRESULT hr;

    if (!Records())
        return;

    hr = RxRecordBarrierGroups(this->Device, Type, DriverList, Table->pfnBarrier, NumBarrierGroups, pBarrierGroups);
    if (FAILED(hr))
        Fail(hr);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::DiscardResource(
    _In_ ID3D12Resource* pResource,
    _In_opt_ const D3D12_DISCARD_REGION* pRegion)
{
    RxResourceFacts Facts;

    if (!Records())
        return;

    if (!RxReadResource(pResource, &Facts) || !RxDiscardIsValid(Facts, pRegion))
    {
        Fail(E_INVALIDARG);
        return;
    }

    Table->pfnDiscardResource(DriverList, Facts.Resource->GetDriverHandle(),
                              reinterpret_cast<const D3D12DDIARG_DISCARD_RESOURCE_0003*>(pRegion));
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::BeginQuery(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT Index)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);

    if (!Records())
        return;

    if (Heap == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* Timestamps and decode statistics are only ever ended */
    if (!RxQueryIsValid(this->Device, Type, *Heap, QueryType, Index) || QueryType == D3D12_QUERY_TYPE_TIMESTAMP ||
        QueryType == D3D12_QUERY_TYPE_VIDEO_DECODE_STATISTICS)
    {
        Fail(E_INVALIDARG);
    }

    OpenQueries += RxHeapPairs(Heap->GetType());
    if (!Records())
        return;

    Table->pfnBeginQuery(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, Index);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::EndQuery(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT Index)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);

    if (!Records())
        return;

    if (Heap == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (!RxQueryIsValid(this->Device, Type, *Heap, QueryType, Index))
        Fail(E_INVALIDARG);

    OpenQueries -= RxHeapPairs(Heap->GetType());
    if (!Records())
        return;

    Table->pfnEndQuery(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, Index);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::ResolveQueryData(
    _In_ ID3D12QueryHeap* pQueryHeap,
    _In_ D3D12_QUERY_TYPE QueryType,
    _In_ UINT StartIndex,
    _In_ UINT NumQueries,
    _In_ ID3D12Resource* pDestinationBuffer,
    _In_ UINT64 AlignedDestinationBufferOffset)
{
    RxQueryHeap* Heap = RxUnwrap12<RxQueryHeap>(pQueryHeap);
    RxResourceFacts Destination;

    if (!Records())
        return;

    if (Heap == nullptr || !RxReadResource(pDestinationBuffer, &Destination) ||
        !RxResolveIsValid(this->Device, Type, *Heap, QueryType, StartIndex, NumQueries, Destination,
                          AlignedDestinationBufferOffset))
    {
        Fail(E_INVALIDARG);
        return;
    }

    Table->pfnResolveQueryData(DriverList, Heap->GetDriverHandle(), (D3D12DDI_QUERY_TYPE)QueryType, StartIndex,
                               NumQueries, Destination.Resource->GetDriverHandle(), AlignedDestinationBufferOffset);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::SetPredication(
    _In_opt_ ID3D12Resource* pBuffer,
    _In_ UINT64 AlignedBufferOffset,
    _In_ D3D12_PREDICATION_OP Operation)
{
    D3D12DDI_HRESOURCE Handle;

    if (!Records())
        return;

    if (!RxPredicationIsValid(pBuffer, AlignedBufferOffset, Operation, &Handle))
    {
        Fail(E_INVALIDARG);
        return;
    }

    Table->pfnSetPredication(DriverList, Handle, AlignedBufferOffset, (D3D12DDI_PREDICATION_OP)Operation);
}

/* Markers carry nothing without tools attached */

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::SetMarker(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::BeginEvent(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::EndEvent(VOID)
{
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::WriteBufferImmediate(
    UINT Count,
    _In_reads_(Count) const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* pParams,
    _In_reads_opt_(Count) const D3D12_WRITEBUFFERIMMEDIATE_MODE* pModes)
{
    UINT Index;

    if (!Records())
        return;

    if ((this->Device.GetOptions().WriteBufferImmediateQueueFlags & RxEngineOf(Type)) == 0 ||
        Table->pfnWriteBufferImmediate == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    if (pModes != nullptr)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if ((pParams[Index].Dest & 3) != 0 || (UINT)pModes[Index] > D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT)
            {
                Fail(E_INVALIDARG);
                return;
            }
        }
    }

    Table->pfnWriteBufferImmediate(DriverList, Count,
                                   reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_PARAMETER_0032*>(pParams),
                                   reinterpret_cast<const D3D12DDI_WRITEBUFFERIMMEDIATE_MODE_0032*>(pModes));
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::SetProtectedResourceSession(
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession)
{
    RxProtectedResourceSession* Given = RxUnwrap12<RxProtectedResourceSession>(pProtectedResourceSession);
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 Handle = {};

    if (!Records())
        return;

    /* A list on a node without sessions only takes none, which then changes nothing */
    if (!TakesSessions || (pProtectedResourceSession != nullptr && Given == nullptr))
    {
        if (pProtectedResourceSession != nullptr)
            Fail(E_INVALIDARG);
        return;
    }

    if (OpenQueries != 0)
    {
        Fail(E_FAIL);
        return;
    }

    Session = Given;
    if (Given != nullptr)
        Handle = Given->GetDriverHandle();
    Table->pfnSetProtectedResourceSession(DriverList, Handle);
}

/* The list type flag an extension command must have been offered for */
static
UINT
RxExtensionListFlag(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS;
        default:
            return D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE;
    }
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::InitializeExtensionCommand(
    _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
    _In_reads_bytes_(InitializationParametersSizeInBytes) const void* pInitializationParameters,
    SIZE_T InitializationParametersSizeInBytes)
{
    RxVideoExtensionCommand12* Command = RxUnwrap12<RxVideoExtensionCommand12>(pExtensionCommand);
    HRESULT hr;

    if (!Records())
        return;

    if (Command == nullptr || Table->pfnInitializeExtensionCommand == nullptr ||
        (Command->GetQueueFlags() & RxExtensionListFlag(Type)) == 0)
    {
        Fail(E_INVALIDARG);
        return;
    }

    hr = Table->pfnInitializeExtensionCommand(DriverList, Command->GetDriverHandle(), pInitializationParameters,
                                              InitializationParametersSizeInBytes);
    if (FAILED(hr))
        Fail(hr);
}

template <typename Interface>
VOID
STDMETHODCALLTYPE
RxVideoList12<Interface>::ExecuteExtensionCommand(
    _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
    _In_reads_bytes_(ExecutionParametersSizeInBytes) const void* pExecutionParameters,
    SIZE_T ExecutionParametersSizeInBytes)
{
    RxVideoExtensionCommand12* Command = RxUnwrap12<RxVideoExtensionCommand12>(pExtensionCommand);
    HRESULT hr;

    if (!Records())
        return;

    if (Command == nullptr || Table->pfnExecuteExtensionCommand == nullptr ||
        (Command->GetQueueFlags() & RxExtensionListFlag(Type)) == 0)
    {
        Fail(E_INVALIDARG);
        return;
    }

    hr = Table->pfnExecuteExtensionCommand(DriverList, Command->GetDriverHandle(), pExecutionParameters,
                                           ExecutionParametersSizeInBytes);
    if (FAILED(hr))
        Fail(hr);
}

template class RxVideoList12<ID3D12VideoDecodeCommandList3>;
template class RxVideoList12<ID3D12VideoProcessCommandList3>;
template class RxVideoList12<ID3D12VideoEncodeCommandList3>;

/* Creation and submission */

template <typename List>
static
HRESULT
RxMakeVideoList(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_opt_ RxCommandAllocator* Allocator,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppCommandList)
{
    List* Created = new (std::nothrow) List(Device);
    HRESULT hr;

    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Type, Allocator);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    return RxHandOut12(Created, riid, ppCommandList);
}

HRESULT
RxCreateVideoList12(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_opt_ RxCommandAllocator* Allocator,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppCommandList)
{
    HRESULT hr;

    if (Allocator != nullptr && Allocator->GetType() != Type)
        return E_INVALIDARG;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
            return RxMakeVideoList<RxVideoDecodeList12>(Device, Type, Allocator, riid, ppCommandList);
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return RxMakeVideoList<RxVideoProcessList12>(Device, Type, Allocator, riid, ppCommandList);
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            if (Device.GetVideo()->GetVersion() < RxVideo0053)
                return E_INVALIDARG;
            return RxMakeVideoList<RxVideoEncodeList12>(Device, Type, Allocator, riid, ppCommandList);
        default:
            return E_INVALIDARG;
    }
}

BOOLEAN
RxVideoListForQueue12(
    _In_opt_ ID3D12CommandList* List,
    _In_ D3D12_COMMAND_LIST_TYPE QueueType,
    _Out_ D3D12DDI_HCOMMANDLIST* Handle)
{
    RxVideoListBase12* Found = nullptr;
    BOOLEAN Ready;

    ZeroMemory(Handle, sizeof(*Handle));
    if (List == nullptr || FAILED(List->QueryInterface(RxVideoListBase12::PrivateId, reinterpret_cast<void**>(&Found))))
        return FALSE;

    /* The lookup does not keep a reference */
    List->Release();

    Ready = Found->IsReadyFor(QueueType);
    if (Ready)
        *Handle = Found->GetDriverList();
    return Ready;
}
