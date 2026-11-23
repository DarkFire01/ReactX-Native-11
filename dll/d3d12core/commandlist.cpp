/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12GraphicsCommandList: recording, and the commands that need no pipeline state
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Values the API defines denormals away from before they reach the driver */
static
FLOAT
RxFlushDenormal(
    _In_ FLOAT Value)
{
    UINT Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    if ((Bits & 0x7F800000) == 0)
        Bits &= 0x80000000;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
D3D12DDI_HRESOURCE
RxDriverResourceOf(
    _In_opt_ ID3D12Resource* Resource)
{
    RxResource* Found = RxUnwrap12<RxResource>(Resource);
    D3D12DDI_HRESOURCE None = {};

    return Found != nullptr ? Found->GetDriverHandle() : None;
}

/* An application render target view handle points at a runtime slot; the driver's descriptor is inside */
static
D3D12DDI_CPU_DESCRIPTOR_HANDLE
RxDriverRtv(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE Handle)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Driver;

    Driver.ptr = reinterpret_cast<const RxRtvEntry*>(Handle.ptr)->DriverHandle;
    return Driver;
}

HRESULT
RxCommandList::Create(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_opt_ RxCommandAllocator* Allocator,
    _In_opt_ ID3D12PipelineState* InitialState,
    _Outptr_ RxCommandList** List)
{
    RxCommandList* Created;
    HRESULT hr;

    *List = nullptr;

    if (RxQueueFlagsOf(Type) == D3D12DDI_COMMAND_QUEUE_FLAG_NONE)
        return E_INVALIDARG;
    if (Allocator != nullptr && Allocator->GetType() != Type)
        return E_INVALIDARG;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxCommandList(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Type);
    if (SUCCEEDED(hr) && Allocator != nullptr)
        hr = Created->Begin(Allocator, InitialState);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *List = Created;
    return S_OK;
}

HRESULT
RxCommandList::Initialize(
    _In_ D3D12_COMMAND_LIST_TYPE ListType)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_COMMAND_LIST_0040 Args = {};
    D3D12DDI_HRTCOMMANDLIST Runtime;
    HRESULT hr;

    Type = ListType;

    Args.Type = Type == D3D12_COMMAND_LIST_TYPE_BUNDLE ? D3D12DDI_COMMAND_LIST_TYPE_BUNDLE
                                                       : D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    Args.QueueFlags = RxQueueFlagsOf(Type);
    Args.ID = Device.NextCommandListId();
    Args.NodeMask = 1;

    DriverList.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateCommandListSize(Device.Handle(), &Args));
    if (DriverList.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    Bypass.pInterfaceVtable = *reinterpret_cast<void**>(static_cast<ID3D12GraphicsCommandList10*>(this));
    Bypass.pDriverObject = DriverList.pDrvPrivate;

    /* The driver names the table this list dispatches through from inside the call */
    Runtime.handle = this;
    hr = Ddi.pfnCreateCommandList(Device.Handle(), &Args, DriverList, Runtime);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateCommandList failed 0x%08lX", hr);
        return hr;
    }
    IsDriverListCreated = TRUE;

    if (DriverTable == nullptr)
    {
        RX_TRACE(L"driver did not name a command list table, using the device one");
        DriverTable = &Device.DefaultListTable();
    }

    return S_OK;
}

RxCommandList::~RxCommandList()
{
    if (Allocator != nullptr)
    {
        Allocator->Unhold();
        Allocator->Release();
    }

    if (IsDriverListCreated)
        Device.Ddi().pfnDestroyCommandList(Device.Handle(), DriverList);
}

/* Starts recording into an allocator the list then holds until it closes */
HRESULT
RxCommandList::Begin(
    _In_ RxCommandAllocator* NewAllocator,
    _In_opt_ ID3D12PipelineState* InitialState)
{
    D3D12DDIARG_RESETCOMMANDLIST_0040 Args = {};

    if (!NewAllocator->Acquire())
        return E_INVALIDARG;

    Allocator = NewAllocator;
    Allocator->AddRef();

    Args.hDrvCommandRecorder = Allocator->GetRecorder();
    Args.ID = Device.NextCommandListId();
    DriverTable->pfnResetCommandList(DriverList, &Args);

    /* Pipeline states are not made yet, so there is never one to bind here */
    if (InitialState != nullptr)
        RX_TRACE(L"initial pipeline state ignored");

    State = RxListState::Recording;
    Error = S_OK;
    ResetState();
    return S_OK;
}

/* The state a list starts recording with, sent to the driver so nothing carries over from the last use */
VOID
RxCommandList::ResetState()
{
    static const FLOAT BlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    if (Type == D3D12_COMMAND_LIST_TYPE_COPY)
        return;

    DriverTable->pfnSetDescriptorHeaps(DriverList, 0, nullptr);
    if (DriverTable->pfnClearRootArguments != nullptr)
        DriverTable->pfnClearRootArguments(DriverList);

    if (Type != D3D12_COMMAND_LIST_TYPE_DIRECT && Type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
        return;

    DriverTable->pfnIaSetTopology(Api(), D3D12DDI_PRIMITIVE_TOPOLOGY_UNDEFINED);
    DriverTable->pfnOMSetRenderTargets(Api(), 0, nullptr, TRUE, nullptr);
    DriverTable->pfnRsSetViewports(Api(), 0, nullptr);
    DriverTable->pfnRsSetScissorRects(Api(), 0, nullptr);
    DriverTable->pfnOmSetBlendFactor(Api(), BlendFactor);
    if (DriverTable->pfnOmSetFrontAndBackStencilRef != nullptr)
        DriverTable->pfnOmSetFrontAndBackStencilRef(Api(), 0, 0);
    else
        DriverTable->pfnOmSetStencilRef(Api(), 0);
}

HRESULT
STDMETHODCALLTYPE
RxCommandList::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    static const IID* const Faces[] =
    {
        &__uuidof(IUnknown), &__uuidof(ID3D12Object), &__uuidof(ID3D12DeviceChild), &__uuidof(ID3D12CommandList),
        &__uuidof(ID3D12GraphicsCommandList), &__uuidof(ID3D12GraphicsCommandList1),
        &__uuidof(ID3D12GraphicsCommandList2), &__uuidof(ID3D12GraphicsCommandList3),
        &__uuidof(ID3D12GraphicsCommandList4), &__uuidof(ID3D12GraphicsCommandList5),
        &__uuidof(ID3D12GraphicsCommandList6), &__uuidof(ID3D12GraphicsCommandList7),
        &__uuidof(ID3D12GraphicsCommandList8), &__uuidof(ID3D12GraphicsCommandList9),
        &__uuidof(ID3D12GraphicsCommandList10), &PrivateId,
    };

    if (ppvObject == nullptr)
        return E_POINTER;

    for (const IID* Face : Faces)
    {
        if (riid == *Face)
        {
            *ppvObject = static_cast<ID3D12GraphicsCommandList10*>(this);
            AddRef();
            return S_OK;
        }
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Driver callbacks */

VOID
APIENTRY
RxCommandList::SetCommandListErrorCb(
    _In_ D3D12DDI_HRTCOMMANDLIST hRTCommandList,
    _In_ HRESULT hr)
{
    RxCommandList* Self = static_cast<RxCommandList*>(hRTCommandList.handle);

    RX_TRACE(L"driver failed a command list with 0x%08lX", hr);
    if (SUCCEEDED(Self->Error))
        Self->Error = hr;
}

VOID
APIENTRY
RxCommandList::SetCommandListDdiTableCb(
    _In_ D3D12DDI_HRTCOMMANDLIST hRTCommandList,
    _In_ D3D12DDI_HRTTABLE hRTTable)
{
    RxCommandList* Self = static_cast<RxCommandList*>(hRTCommandList.handle);

    Self->DriverTable = Self->Device.ListTable(hRTTable);
}

/* ID3D12CommandList and recording */

D3D12_COMMAND_LIST_TYPE
STDMETHODCALLTYPE
RxCommandList::GetType(VOID)
{
    return Type;
}

HRESULT
STDMETHODCALLTYPE
RxCommandList::Close(VOID)
{
    HRESULT hr;

    if (State != RxListState::Recording)
        return E_FAIL;

    DriverTable->pfnCloseCommandList(DriverList);
    State = RxListState::Closed;

    Allocator->Unhold();
    Allocator->Release();
    Allocator = nullptr;

    hr = RxDevice12::TakeDriverError();
    if (FAILED(Error))
        return Error;
    return FAILED(hr) ? hr : S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxCommandList::Reset(
    _In_ ID3D12CommandAllocator* pAllocator,
    _In_opt_ ID3D12PipelineState* pInitialState)
{
    RxCommandAllocator* NewAllocator = RxUnwrap12<RxCommandAllocator>(pAllocator);

    /* A list still recording can not start over */
    if (State == RxListState::Recording)
        return E_FAIL;
    if (NewAllocator == nullptr || NewAllocator->GetType() != Type)
        return E_INVALIDARG;

    return Begin(NewAllocator, pInitialState);
}

/* Copies */

VOID
STDMETHODCALLTYPE
RxCommandList::CopyBufferRegion(
    _In_ ID3D12Resource* pDstBuffer,
    UINT64 DstOffset,
    _In_ ID3D12Resource* pSrcBuffer,
    UINT64 SrcOffset,
    UINT64 NumBytes)
{
    D3D12DDIARG_BUFFER_PLACEMENT Destination = {};
    D3D12DDIARG_BUFFER_PLACEMENT Source = {};

    Destination.BaseAddress.UMD.hResource = RxDriverResourceOf(pDstBuffer);
    Destination.BaseAddress.UMD.Offset = DstOffset;
    Source.BaseAddress.UMD.hResource = RxDriverResourceOf(pSrcBuffer);
    Source.BaseAddress.UMD.Offset = SrcOffset;
    DriverTable->pfnCopyBufferRegion(DriverList, Destination, Source, NumBytes);
}

/* One end of a texture copy in the form the driver takes it */
struct RxCopyLocation
{
    D3D12DDIARG_BUFFER_PLACEMENT Placement;
    D3D12DDIARG_PLACED_RESOURCE Placed;
    D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT Pitched;
};

static
VOID
RxToCopyLocation(
    _In_ const D3D12_TEXTURE_COPY_LOCATION& Location,
    _Out_ RxCopyLocation& Out)
{
    UINT BlockWidth;
    UINT BlockHeight;
    UINT BlockBytes;

    ZeroMemory(&Out, sizeof(Out));
    Out.Placement.BaseAddress.UMD.hResource = RxDriverResourceOf(Location.pResource);

    if (Location.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
    {
        Out.Placement.BaseAddress.UMD.Offset = Location.SubresourceIndex;
        Out.Placed.Layout = D3D12DDI_RL_SELECT_SUBRESOURCE;
        return;
    }

    if (!RxFormatBlock(Location.PlacedFootprint.Footprint.Format, &BlockWidth, &BlockHeight, &BlockBytes))
        BlockHeight = 1;

    Out.Placement.BaseAddress.UMD.Offset = Location.PlacedFootprint.Offset;
    Out.Pitched.Format = Location.PlacedFootprint.Footprint.Format;
    Out.Pitched.PhysicalWidth = Location.PlacedFootprint.Footprint.Width;
    Out.Pitched.PhysicalHeight = Location.PlacedFootprint.Footprint.Height;
    Out.Pitched.PhysicalDepth = Location.PlacedFootprint.Footprint.Depth;
    Out.Pitched.Pitch = Location.PlacedFootprint.Footprint.RowPitch;
    Out.Pitched.SlicePitch = Location.PlacedFootprint.Footprint.Height * Location.PlacedFootprint.Footprint.RowPitch /
                             BlockHeight;
    Out.Placed.Layout = D3D12DDI_RL_PLACED_PHYSICAL_SUBRESOURCE_PITCHED;
    Out.Placed.pLayout = &Out.Pitched;
}

VOID
STDMETHODCALLTYPE
RxCommandList::CopyTextureRegion(
    _In_ const D3D12_TEXTURE_COPY_LOCATION* pDst,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    _In_ const D3D12_TEXTURE_COPY_LOCATION* pSrc,
    _In_opt_ const D3D12_BOX* pSrcBox)
{
    RxCopyLocation Destination;
    RxCopyLocation Source;
    D3D12DDI_BOX Box;

    if (pDst == nullptr || pSrc == nullptr)
        return;

    RxToCopyLocation(*pDst, Destination);
    RxToCopyLocation(*pSrc, Source);

    if (pSrcBox != nullptr)
    {
        Box.Left = (LONG)pSrcBox->left;
        Box.Top = (LONG)pSrcBox->top;
        Box.Front = (LONG)pSrcBox->front;
        Box.Right = (LONG)pSrcBox->right;
        Box.Bottom = (LONG)pSrcBox->bottom;
        Box.Back = (LONG)pSrcBox->back;
    }

    DriverTable->pfnCopyTextureRegion(DriverList, &Destination.Placement, Destination.Placed, DstX, DstY, DstZ,
                                      &Source.Placement, Source.Placed, pSrcBox != nullptr ? &Box : nullptr);
}

VOID
STDMETHODCALLTYPE
RxCommandList::CopyResource(
    _In_ ID3D12Resource* pDstResource,
    _In_ ID3D12Resource* pSrcResource)
{
    DriverTable->pfnResourceCopy(DriverList, RxDriverResourceOf(pDstResource), RxDriverResourceOf(pSrcResource));
}

/* Barriers */

VOID
STDMETHODCALLTYPE
RxCommandList::ResourceBarrier(
    _In_ UINT NumBarriers,
    _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER* pBarriers)
{
    D3D12DDIARG_RESOURCE_BARRIER_0022 Stack[16];
    D3D12DDIARG_RESOURCE_BARRIER_0022* Barriers = Stack;
    UINT Count = 0;
    UINT Index;

    if (NumBarriers == 0 || pBarriers == nullptr)
        return;

    /* Aliasing can take two driver entries for one API barrier */
    if (NumBarriers * 2 > _countof(Stack))
    {
        Barriers = new (std::nothrow) D3D12DDIARG_RESOURCE_BARRIER_0022[NumBarriers * 2];
        if (Barriers == nullptr)
            return;
    }

    for (Index = 0; Index < NumBarriers; Index++)
    {
        const D3D12_RESOURCE_BARRIER& Barrier = pBarriers[Index];
        D3D12DDIARG_RESOURCE_BARRIER_0022& Out = Barriers[Count];

        ZeroMemory(&Out, sizeof(Out));
        Out.Flags = (D3D12DDI_RESOURCE_BARRIER_FLAGS)Barrier.Flags;

        switch (Barrier.Type)
        {
            case D3D12_RESOURCE_BARRIER_TYPE_TRANSITION:
                Out.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_TRANSITION;
                Out.Transition.hResource = RxDriverResourceOf(Barrier.Transition.pResource);
                Out.Transition.Subresource = Barrier.Transition.Subresource;
                Out.Transition.StateBefore = (D3D12DDI_RESOURCE_STATES)Barrier.Transition.StateBefore;
                Out.Transition.StateAfter = (D3D12DDI_RESOURCE_STATES)Barrier.Transition.StateAfter;
                Count++;
                break;

            case D3D12_RESOURCE_BARRIER_TYPE_UAV:
                Out.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_UAV;
                Out.UAV.hResource = RxDriverResourceOf(Barrier.UAV.pResource);
                Count++;
                break;

            case D3D12_RESOURCE_BARRIER_TYPE_ALIASING:
            {
                D3D12DDI_HRESOURCE Before = RxDriverResourceOf(Barrier.Aliasing.pResourceBefore);
                D3D12DDI_HRESOURCE After = RxDriverResourceOf(Barrier.Aliasing.pResourceAfter);

                /* The driver sees aliasing as whole range barriers on each side */
                Out.Type = D3D12DDI_RESOURCE_BARRIER_TYPE_0022_RANGED;
                Out.Ranged.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                Out.Ranged.Range.End = ~0ull;
                Out.Ranged.hResource = Before;
                if (Before.pDrvPrivate == After.pDrvPrivate)
                {
                    Out.Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING;
                    Count++;
                    break;
                }

                Out.Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING | D3D12DDI_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
                Barriers[Count + 1] = Out;
                Barriers[Count + 1].Ranged.hResource = After;
                Barriers[Count + 1].Flags = D3D12DDI_RESOURCE_BARRIER_FLAG_0022_ALIASING |
                                            D3D12DDI_RESOURCE_BARRIER_FLAG_END_ONLY;
                Count += 2;
                break;
            }

            default:
                break;
        }
    }

    if (Count != 0)
        DriverTable->pfnResourceBarrier(DriverList, Count, Barriers);

    if (Barriers != Stack)
        delete[] Barriers;
}

/* Clears and output binding */

VOID
STDMETHODCALLTYPE
RxCommandList::ClearRenderTargetView(
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView,
    _In_ const FLOAT ColorRGBA[4],
    _In_ UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT* pRects)
{
    FLOAT Color[4];
    UINT Index;

    /* Rectangles that are all empty clear nothing */
    if (NumRects != 0 && pRects != nullptr)
    {
        for (Index = 0; Index < NumRects; Index++)
        {
            if (pRects[Index].right > pRects[Index].left && pRects[Index].bottom > pRects[Index].top)
                break;
        }
        if (Index == NumRects)
            return;
    }

    for (Index = 0; Index < 4; Index++)
        Color[Index] = RxFlushDenormal(ColorRGBA[Index]);

    DriverTable->pfnClearRenderTargetView(DriverList, RxDriverRtv(RenderTargetView), Color, NumRects, pRects);
}

VOID
STDMETHODCALLTYPE
RxCommandList::OMSetRenderTargets(
    _In_ UINT NumRenderTargetDescriptors,
    _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE* pRenderTargetDescriptors,
    _In_ BOOL RTsSingleHandleToDescriptorRange,
    _In_opt_ const D3D12_CPU_DESCRIPTOR_HANDLE* pDepthStencilDescriptor)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Targets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT];
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DepthStencil;
    UINT Index;

    if (NumRenderTargetDescriptors > D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT ||
        (NumRenderTargetDescriptors != 0 && pRenderTargetDescriptors == nullptr))
    {
        return;
    }

    /* A single range is contiguous runtime slots, each holding its own driver descriptor */
    for (Index = 0; Index < NumRenderTargetDescriptors; Index++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE Handle = RTsSingleHandleToDescriptorRange
                                                 ? D3D12_CPU_DESCRIPTOR_HANDLE{ pRenderTargetDescriptors[0].ptr +
                                                                                Index * sizeof(RxRtvEntry) }
                                                 : pRenderTargetDescriptors[Index];

        Targets[Index] = RxDriverRtv(Handle);
    }

    if (pDepthStencilDescriptor != nullptr)
        DepthStencil.ptr = pDepthStencilDescriptor->ptr;

    DriverTable->pfnOMSetRenderTargets(Api(), NumRenderTargetDescriptors, Targets, FALSE,
                                       pDepthStencilDescriptor != nullptr ? &DepthStencil : nullptr);
}

VOID
STDMETHODCALLTYPE
RxCommandList::SetDescriptorHeaps(
    _In_ UINT NumDescriptorHeaps,
    _In_reads_(NumDescriptorHeaps) ID3D12DescriptorHeap* const* ppDescriptorHeaps)
{
    D3D12DDI_HDESCRIPTORHEAP Heaps[2];
    UINT Index;

    if (NumDescriptorHeaps > _countof(Heaps))
        return;

    for (Index = 0; Index < NumDescriptorHeaps; Index++)
    {
        RxDescriptorHeap* Heap = RxUnwrap12<RxDescriptorHeap>(ppDescriptorHeaps[Index]);

        if (Heap == nullptr)
            return;
        Heaps[Index] = Heap->GetDriverHandle();
    }

    DriverTable->pfnSetDescriptorHeaps(DriverList, NumDescriptorHeaps, Heaps);
}

/* Fixed function state */

VOID
STDMETHODCALLTYPE
RxCommandList::IASetPrimitiveTopology(
    _In_ D3D12_PRIMITIVE_TOPOLOGY PrimitiveTopology)
{
    DriverTable->pfnIaSetTopology(Api(), (D3D12DDI_PRIMITIVE_TOPOLOGY)PrimitiveTopology);
}

VOID
STDMETHODCALLTYPE
RxCommandList::RSSetViewports(
    _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumViewports,
    _In_reads_(NumViewports) const D3D12_VIEWPORT* pViewports)
{
    static_assert(sizeof(D3D12_VIEWPORT) == sizeof(D3D12DDI_VIEWPORT));

    if (NumViewports > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
        return;

    DriverTable->pfnRsSetViewports(Api(), NumViewports, reinterpret_cast<const D3D12DDI_VIEWPORT*>(pViewports));
}

VOID
STDMETHODCALLTYPE
RxCommandList::RSSetScissorRects(
    _In_range_(0, D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE) UINT NumRects,
    _In_reads_(NumRects) const D3D12_RECT* pRects)
{
    if (NumRects > D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)
        return;

    DriverTable->pfnRsSetScissorRects(Api(), NumRects, pRects);
}

VOID
STDMETHODCALLTYPE
RxCommandList::OMSetBlendFactor(
    _In_reads_opt_(4) const FLOAT BlendFactor[4])
{
    static const FLOAT Ones[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    DriverTable->pfnOmSetBlendFactor(Api(), BlendFactor != nullptr ? BlendFactor : Ones);
}

VOID
STDMETHODCALLTYPE
RxCommandList::OMSetStencilRef(
    _In_ UINT StencilRef)
{
    if (DriverTable->pfnOmSetFrontAndBackStencilRef != nullptr)
        DriverTable->pfnOmSetFrontAndBackStencilRef(Api(), StencilRef, StencilRef);
    else
        DriverTable->pfnOmSetStencilRef(Api(), StencilRef);
}

/* Markers carry nothing without tools attached */

VOID
STDMETHODCALLTYPE
RxCommandList::SetMarker(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

VOID
STDMETHODCALLTYPE
RxCommandList::BeginEvent(
    UINT Metadata,
    _In_reads_bytes_opt_(Size) const void* pData,
    UINT Size)
{
}

VOID
STDMETHODCALLTYPE
RxCommandList::EndEvent(VOID)
{
}
