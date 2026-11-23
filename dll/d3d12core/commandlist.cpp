/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12GraphicsCommandList: recording, and the commands that need no pipeline state
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "pipeline.hpp"
#include "formatlayout12.hpp"
#include "formatsupport12.hpp"

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

    hr = Created->Initialize(Type, Allocator);
    if (SUCCEEDED(hr) && Allocator != nullptr)
        hr = Created->Begin(InitialState);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *List = Created;
    return S_OK;
}

/* Takes the allocator a recording goes into and the id the driver knows that recording by */
HRESULT
RxCommandList::Claim(
    _In_ RxCommandAllocator* NewAllocator)
{
    if (!NewAllocator->Acquire())
        return E_INVALIDARG;

    Allocator = NewAllocator;
    Allocator->AddRef();
    ListId = Device.NextCommandListId();
    return S_OK;
}

HRESULT
RxCommandList::Initialize(
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _In_opt_ RxCommandAllocator* FirstAllocator)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_COMMAND_LIST_0040 Args = {};
    D3D12DDI_HRTCOMMANDLIST Runtime;
    SIZE_T Size;
    HRESULT hr;

    Type = ListType;

    /* Sizing needs only the type, queue and node */
    Args.Type = Type == D3D12_COMMAND_LIST_TYPE_BUNDLE ? D3D12DDI_COMMAND_LIST_TYPE_BUNDLE
                                                       : D3D12DDI_COMMAND_LIST_TYPE_DIRECT;
    Args.QueueFlags = RxQueueFlagsOf(Type);
    Args.NodeMask = 1;
    Size = Ddi.pfnCalcPrivateCommandListSize(Device.Handle(), &Args);

    /* Every list type but bundles drops its protected session on each reset where sessions exist */
    if (Type != D3D12_COMMAND_LIST_TYPE_BUNDLE)
    {
        hr = Device.SessionSupport(0, &TakesSessions);
        if (FAILED(hr))
            return hr;
    }

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

/* Starts recording into the claimed allocator, which the list holds until it closes */
HRESULT
RxCommandList::Begin(
    _In_opt_ ID3D12PipelineState* InitialState)
{
    D3D12DDIARG_RESETCOMMANDLIST_0040 Args = {};

    Args.hDrvCommandRecorder = Allocator->GetRecorder();
    Args.ID = ListId;
    DriverTable->pfnResetCommandList(DriverList, &Args);

    if (Type != D3D12_COMMAND_LIST_TYPE_COPY)
        ResetPipeline(InitialState);

    State = RxListState::Recording;
    Error = S_OK;
    ResetState();
    return S_OK;
}

/* The application's initial pipeline, or the device's default one of the list's kind */
VOID
RxCommandList::ResetPipeline(
    _In_opt_ ID3D12PipelineState* InitialState)
{
    RxPipelineState* Pipeline = RxUnwrap12<RxPipelineState>(InitialState);

    if (Pipeline == nullptr)
        Pipeline = Device.DefaultPipeline(Type == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    if (Pipeline != nullptr)
        DriverTable->pfnSetPipelineState(Api(), Pipeline->Api());
}

/* The state a list starts recording with, sent to the driver so nothing carries over from the last use */
VOID
RxCommandList::ResetState()
{
    static const FLOAT BlendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const D3D12DDI_VERTEX_BUFFER_VIEW NoVertexBuffers[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
    static const D3D12DDI_STREAM_OUTPUT_BUFFER_VIEW NoStreamTargets[D3D12_SO_BUFFER_SLOT_COUNT] = {};
    static const D3D12DDI_SHADING_RATE_COMBINER_0062 Passthrough[D3D12_RS_SET_SHADING_RATE_COMBINER_COUNT] = {};
    const D3D12DDI_D3D12_OPTIONS_DATA_0089& Options = Device.GetOptions();
    D3D12DDI_HDESCRIPTORHEAP NoHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};
    D3D12DDI_CPU_DESCRIPTOR_HANDLE NoTargets[D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    D3D12DDI_HRESOURCE NoResource = {};
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 NoSession = {};

    if (Type != D3D12_COMMAND_LIST_TYPE_COPY)
    {
        if (Type != D3D12_COMMAND_LIST_TYPE_COMPUTE)
            DriverTable->pfnIaSetTopology(Api(), D3D12DDI_PRIMITIVE_TOPOLOGY_UNDEFINED);
        DriverTable->pfnSetDescriptorHeaps(DriverList, 0, NoHeaps);
    }

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        DriverTable->pfnIASetVertexBuffers(Api(), 0, _countof(NoVertexBuffers), NoVertexBuffers);
        DriverTable->pfnIASetIndexBuffer(Api(), nullptr);
        DriverTable->pfnSOSetTargets(Api(), 0, _countof(NoStreamTargets), NoStreamTargets);
        DriverTable->pfnOMSetRenderTargets(Api(), 0, NoTargets, TRUE, nullptr);
        DriverTable->pfnRsSetViewports(Api(), 0, nullptr);
        DriverTable->pfnRsSetScissorRects(Api(), 0, nullptr);
        DriverTable->pfnOmSetBlendFactor(Api(), BlendFactor);
        if (DriverTable->pfnOmSetFrontAndBackStencilRef != nullptr)
            DriverTable->pfnOmSetFrontAndBackStencilRef(Api(), 0, 0);
        else
            DriverTable->pfnOmSetStencilRef(Api(), 0);
        if (Options.DepthBoundsTestSupported)
            DriverTable->pfnOMSetDepthBounds(Api(), 0.0f, 1.0f);
        if (Options.VariableShadingRateTier != D3D12DDI_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED)
        {
            DriverTable->pfnRSSetShadingRate(Api(), D3D12DDI_SHADING_RATE_0062_1X1, Passthrough);
            if (Options.VariableShadingRateTier == D3D12DDI_VARIABLE_SHADING_RATE_TIER_2)
                DriverTable->pfnRSSetShadingRateImage(DriverList, NoResource);
        }
    }

    if (Type == D3D12_COMMAND_LIST_TYPE_DIRECT || Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        DriverTable->pfnSetPredication(DriverList, NoResource, 0, D3D12DDI_PREDICATION_OP_EQUAL_ZERO);
        if (DriverTable->pfnClearRootArguments != nullptr)
            DriverTable->pfnClearRootArguments(DriverList);
    }

    if (TakesSessions)
        DriverTable->pfnSetProtectedResourceSession(DriverList, NoSession);
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
    ListId = UINT64_MAX;

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
    HRESULT hr;

    /* A list still recording can not start over */
    if (State == RxListState::Recording)
        return E_FAIL;
    if (NewAllocator == nullptr || NewAllocator->GetType() != Type)
        return E_INVALIDARG;

    hr = Claim(NewAllocator);
    if (FAILED(hr))
        return hr;
    return Begin(pInitialState);
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

static
BOOLEAN
RxIsBlockCompressed(
    _In_ DXGI_FORMAT Format)
{
    return (Format >= DXGI_FORMAT_BC1_TYPELESS && Format <= DXGI_FORMAT_BC5_SNORM) ||
           (Format >= DXGI_FORMAT_BC6H_TYPELESS && Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

/* A buffer footprint must fit a format the device copies, the dimension it implies, and the buffer */
static
BOOLEAN
RxFootprintIsValid(
    _In_ RxDevice12& Device,
    _In_ const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Placed,
    _In_ const D3D12_RESOURCE_DESC1& BufferDesc)
{
    const D3D12_SUBRESOURCE_FOOTPRINT& Footprint = Placed.Footprint;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = {};
    BOOLEAN Unrestricted = Device.UnrestrictedCopyPitch();
    DXGI_FORMAT Family;
    UINT64 Needed;
    UINT64 LastRow;
    UINT Largest = Device.GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0 ? D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION
                                                                       : D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    UINT MaxWidth;
    UINT MaxHeight;
    UINT MaxDepth;
    UINT Dimension;
    UINT BlockWidth;
    UINT BlockHeight;
    UINT BlockBytes;
    UINT Rows;

    if (!Unrestricted && (Placed.Offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT) != 0)
        return FALSE;
    if ((UINT)Footprint.Format >= RxFormatRuleCount12 || !RxFormatInHeader(Footprint.Format))
        return FALSE;

    /* The device's support decides which dimensions a footprint of the format may stand for */
    Support.Format = Footprint.Format;
    if (FAILED(Device.CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))))
        return FALSE;

    if (Footprint.Depth > 1)
    {
        Dimension = D3D12_FORMAT_SUPPORT1_TEXTURE3D;
        MaxWidth = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        MaxHeight = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
        MaxDepth = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    }
    else if (Footprint.Height == 1 && (Support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE1D) != 0)
    {
        Dimension = D3D12_FORMAT_SUPPORT1_TEXTURE1D;
        MaxWidth = Largest;
        MaxHeight = 1;
        MaxDepth = 1;
    }
    else
    {
        Dimension = D3D12_FORMAT_SUPPORT1_TEXTURE2D;
        MaxWidth = Largest;
        MaxHeight = Largest;
        MaxDepth = 1;
    }
    if ((Support.Support1 & Dimension) == 0)
        return FALSE;

    if (!RxFormatBlock(Footprint.Format, &BlockWidth, &BlockHeight, &BlockBytes))
        return FALSE;
    if ((!RxIsBlockCompressed(Footprint.Format) || !Device.GetValidationCaps().UnalignedBlocks) &&
        ((Footprint.Width % BlockWidth) != 0 || (Footprint.Height % BlockHeight) != 0))
    {
        return FALSE;
    }

    if (Footprint.Width == 0 || Footprint.Height == 0 || Footprint.Depth == 0)
        return FALSE;
    if (Footprint.Width > MaxWidth || Footprint.Height > MaxHeight || Footprint.Depth > MaxDepth)
        return FALSE;

    if (Footprint.RowPitch < (UINT64)(Footprint.Width / BlockWidth) * BlockBytes)
        return FALSE;
    if (!Unrestricted && (Footprint.RowPitch % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) != 0)
        return FALSE;

    /* The last row only needs its own bytes, not the whole pitch */
    LastRow = (UINT64)((Footprint.Width + BlockWidth - 1) / BlockWidth) * BlockBytes;
    Rows = (Footprint.Height + BlockHeight - 1) / BlockHeight;
    Needed = (UINT64)Footprint.RowPitch * ((UINT64)Footprint.Depth * Rows - 1) + LastRow;
    if (Placed.Offset + Needed < Needed || Placed.Offset + Needed > BufferDesc.Width)
        return FALSE;

    /* Depth stencil formats are planar and copy one plane at a time through their plane formats */
    Family = RxFormatFamily(Footprint.Format);
    return Family != DXGI_FORMAT_R32G8X24_TYPELESS && Family != DXGI_FORMAT_R24G8_TYPELESS;
}

/* What one end of a texture copy names must exist on its resource */
static
BOOLEAN
RxCopyLocationIsValid(
    _In_ RxDevice12& Device,
    _In_ const D3D12_TEXTURE_COPY_LOCATION& Location)
{
    RxResource* Resource = RxUnwrap12<RxResource>(Location.pResource);
    const D3D12_RESOURCE_DESC1* Desc;
    UINT Subresources;

    if (Resource == nullptr)
        return FALSE;
    Desc = &Resource->GetApiDesc();

    if (Location.Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT)
    {
        if (Desc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
            return FALSE;
        return RxFootprintIsValid(Device, Location.PlacedFootprint, *Desc);
    }
    if (Location.Type != D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX || Desc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return FALSE;

    Subresources = Desc->MipLevels * RxFormatPlaneCount(Desc->Format);
    if (Desc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        Subresources *= Desc->DepthOrArraySize;
    return Location.SubresourceIndex < Subresources;
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

    /* A bad location fails the whole list, as the system core removes it */
    if (!RxCopyLocationIsValid(Device, *pDst) || !RxCopyLocationIsValid(Device, *pSrc))
    {
        RX_TRACE(L"invalid texture copy location");
        if (SUCCEEDED(Error))
            Error = E_INVALIDARG;
        return;
    }

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

/* What one legacy state bit means to a driver that takes enhanced barriers */
struct RxStateMeaning
{
    UINT State;
    UINT TextureSync;
    UINT BufferSync;
    UINT Access;
    UINT Layout;
};

constexpr UINT RxNoLayout = (UINT)D3D12_BARRIER_LAYOUT_UNDEFINED;
constexpr UINT RxUavBufferSync = D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW |
                                 D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                                 D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                                 D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
constexpr UINT RxStructureSync = D3D12_BARRIER_SYNC_ALL_SHADING |
                                 D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                                 D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                                 D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;
constexpr UINT RxUavTextureSync = D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
constexpr UINT RxUavBufferAccess = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS |
                                   D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
                                   D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE;

/* Layouts the core keeps for legacy states that have no public enhanced layout */
constexpr UINT RxLegacyCopySource = 0x80000000;
constexpr UINT RxLegacyCopyDest = 0x80000001;
constexpr UINT RxLegacyNonPixelResource = 0x80000002;
constexpr UINT RxLegacyPixelResource = 0x80000003;

static const RxStateMeaning RxStateMeanings[] =
{
    { D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_BARRIER_SYNC_ALL_SHADING, D3D12_BARRIER_SYNC_ALL_SHADING,
      D3D12_BARRIER_ACCESS_VERTEX_BUFFER | D3D12_BARRIER_ACCESS_CONSTANT_BUFFER, 0 },
    { D3D12_RESOURCE_STATE_INDEX_BUFFER, D3D12_BARRIER_SYNC_INDEX_INPUT, D3D12_BARRIER_SYNC_INDEX_INPUT,
      D3D12_BARRIER_ACCESS_INDEX_BUFFER, 0 },
    { D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET, D3D12_BARRIER_SYNC_RENDER_TARGET,
      D3D12_BARRIER_ACCESS_RENDER_TARGET, D3D12_BARRIER_LAYOUT_RENDER_TARGET },
    { D3D12_RESOURCE_STATE_UNORDERED_ACCESS, RxUavTextureSync, RxUavBufferSync, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS,
      D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS },
    { D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_SYNC_DEPTH_STENCIL,
      D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE },
    { D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_BARRIER_SYNC_DEPTH_STENCIL, D3D12_BARRIER_SYNC_DEPTH_STENCIL,
      D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ },
    { D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
      D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING,
      D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING |
          D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE,
      D3D12_BARRIER_ACCESS_SHADER_RESOURCE, RxLegacyNonPixelResource },
    { D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_PIXEL_SHADING,
      D3D12_BARRIER_ACCESS_SHADER_RESOURCE, RxLegacyPixelResource },
    { D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_BARRIER_SYNC_VERTEX_SHADING, D3D12_BARRIER_SYNC_VERTEX_SHADING,
      D3D12_BARRIER_ACCESS_STREAM_OUTPUT, RxNoLayout },
    { D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT, D3D12_BARRIER_SYNC_EXECUTE_INDIRECT,
      D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT, 0 },
    { D3D12_RESOURCE_STATE_COPY_DEST, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_ACCESS_COPY_DEST,
      RxLegacyCopyDest },
    { D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_BARRIER_SYNC_COPY, D3D12_BARRIER_SYNC_COPY,
      D3D12_BARRIER_ACCESS_COPY_SOURCE, RxLegacyCopySource },
    { D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_BARRIER_SYNC_RESOLVE, D3D12_BARRIER_SYNC_RESOLVE,
      D3D12_BARRIER_ACCESS_RESOLVE_DEST, D3D12_BARRIER_LAYOUT_RESOLVE_DEST },
    { D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_BARRIER_SYNC_RESOLVE, D3D12_BARRIER_SYNC_RESOLVE,
      D3D12_BARRIER_ACCESS_RESOLVE_SOURCE, D3D12_BARRIER_LAYOUT_RESOLVE_SOURCE },
    { D3D12_RESOURCE_STATE_VIDEO_DECODE_READ, D3D12_BARRIER_SYNC_VIDEO_DECODE, D3D12_BARRIER_SYNC_VIDEO_DECODE,
      D3D12_BARRIER_ACCESS_VIDEO_DECODE_READ, D3D12_BARRIER_LAYOUT_VIDEO_DECODE_READ },
    { D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE, D3D12_BARRIER_SYNC_VIDEO_DECODE, D3D12_BARRIER_SYNC_VIDEO_DECODE,
      D3D12_BARRIER_ACCESS_VIDEO_DECODE_WRITE, D3D12_BARRIER_LAYOUT_VIDEO_DECODE_WRITE },
    { D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ, D3D12_BARRIER_SYNC_VIDEO_PROCESS, D3D12_BARRIER_SYNC_VIDEO_PROCESS,
      D3D12_BARRIER_ACCESS_VIDEO_PROCESS_READ, D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_READ },
    { D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE, D3D12_BARRIER_SYNC_VIDEO_PROCESS, D3D12_BARRIER_SYNC_VIDEO_PROCESS,
      D3D12_BARRIER_ACCESS_VIDEO_PROCESS_WRITE, D3D12_BARRIER_LAYOUT_VIDEO_PROCESS_WRITE },
    { D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ, D3D12_BARRIER_SYNC_VIDEO_ENCODE, D3D12_BARRIER_SYNC_VIDEO_ENCODE,
      D3D12_BARRIER_ACCESS_VIDEO_ENCODE_READ, D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_READ },
    { D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, RxStructureSync, RxStructureSync,
      D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ |
          D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE,
      RxNoLayout },
    { D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE, D3D12_BARRIER_SYNC_VIDEO_ENCODE, D3D12_BARRIER_SYNC_VIDEO_ENCODE,
      D3D12_BARRIER_ACCESS_VIDEO_ENCODE_WRITE, D3D12_BARRIER_LAYOUT_VIDEO_ENCODE_WRITE },
    { D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE, D3D12_BARRIER_SYNC_PIXEL_SHADING, D3D12_BARRIER_SYNC_PIXEL_SHADING,
      D3D12_BARRIER_ACCESS_SHADING_RATE_SOURCE, D3D12_BARRIER_LAYOUT_SHADING_RATE_SOURCE },
};

/* States that write; a mix holding one of them has no enhanced meaning */
constexpr UINT RxWritingStates = D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
                                 D3D12_RESOURCE_STATE_DEPTH_WRITE | D3D12_RESOURCE_STATE_COPY_DEST |
                                 D3D12_RESOURCE_STATE_RESOLVE_DEST | D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE |
                                 D3D12_RESOURCE_STATE_VIDEO_PROCESS_WRITE | D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE;

/* The read states a mix of reads is made of, for textures and for buffers */
constexpr UINT RxTextureReads = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE |
                                D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
                                D3D12_RESOURCE_STATE_VIDEO_DECODE_READ | D3D12_RESOURCE_STATE_VIDEO_PROCESS_READ |
                                D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ | D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
constexpr UINT RxBufferReads = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                               D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT |
                               D3D12_RESOURCE_STATE_COPY_SOURCE | D3D12_RESOURCE_STATE_RESOLVE_SOURCE;

static
const RxStateMeaning*
RxMeaningOf(
    _In_ UINT State)
{
    for (const RxStateMeaning& Meaning : RxStateMeanings)
    {
        if (Meaning.State == State)
            return &Meaning;
    }

    return nullptr;
}

/* The work a legacy state waits on or blocks */
static
UINT
RxLegacySync(
    _In_ UINT State,
    _In_ BOOLEAN IsBuffer)
{
    const RxStateMeaning* Single = RxMeaningOf(State);
    UINT Reads = State & (IsBuffer ? RxBufferReads : RxTextureReads);
    UINT Sync = 0;

    if (State == D3D12_RESOURCE_STATE_COMMON)
        return D3D12_BARRIER_SYNC_ALL;
    if (Single != nullptr)
        return IsBuffer ? Single->BufferSync : Single->TextureSync;
    if ((State & RxWritingStates) != 0)
        return D3D12_BARRIER_SYNC_NONE;

    for (const RxStateMeaning& Meaning : RxStateMeanings)
    {
        if ((Reads & Meaning.State) != 0)
            Sync |= IsBuffer ? Meaning.BufferSync : Meaning.TextureSync;
    }

    return Sync;
}

static
UINT
RxLegacyAccess(
    _In_ UINT State,
    _In_ BOOLEAN IsBuffer)
{
    const RxStateMeaning* Single = RxMeaningOf(State);
    UINT Reads = State & (IsBuffer ? RxBufferReads : RxTextureReads);
    UINT Access = 0;

    if (State == D3D12_RESOURCE_STATE_COMMON)
        return D3D12_BARRIER_ACCESS_COMMON;
    if (Single != nullptr)
        return Single->Access;
    if ((State & RxWritingStates) != 0)
        return D3D12_BARRIER_ACCESS_NO_ACCESS;

    for (const RxStateMeaning& Meaning : RxStateMeanings)
    {
        if ((Reads & Meaning.State) != 0)
            Access |= Meaning.Access;
    }

    return Access;
}

/* The texture layout a legacy state stands for; a mix of reads takes a generic read layout */
static
UINT
RxLegacyLayout(
    _In_ UINT State)
{
    constexpr UINT BufferOnly = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    constexpr UINT GraphicsReads = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER |
                                   D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                   D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT | D3D12_RESOURCE_STATE_RESOLVE_SOURCE |
                                   D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    constexpr UINT DirectOnlyReads = D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                     D3D12_RESOURCE_STATE_RESOLVE_SOURCE | D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    const RxStateMeaning* Single = RxMeaningOf(State);

    if (State == D3D12_RESOURCE_STATE_COMMON)
        return D3D12_BARRIER_LAYOUT_COMMON;
    if (Single != nullptr && (Single->State & BufferOnly) == 0)
        return Single->Layout;
    if ((State & ~BufferOnly) == 0 || (State & RxWritingStates) != 0)
        return RxNoLayout;
    if ((State & ~GraphicsReads) == 0)
        return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ;
    if ((State & DirectOnlyReads) != 0)
        return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ_COMPUTE_QUEUE_ACCESSIBLE;
    return D3D12_BARRIER_LAYOUT_GENERIC_READ;
}

/* A legacy barrier in the enhanced form, for drivers that take only that form */
static
VOID
RxToEnhancedBarrier(
    _In_ const D3D12_RESOURCE_BARRIER& Barrier,
    _In_ D3D12_COMMAND_LIST_TYPE ListType,
    _Out_ D3D12DDIARG_BARRIER_0094& Out)
{
    RxResource* Resource;
    BOOLEAN IsBuffer;
    UINT Before;
    UINT After;

    ZeroMemory(&Out, sizeof(Out));

    if (Barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_ALIASING)
    {
        Out.Type = D3D12DDI_BARRIER_TYPE_GLOBAL;
        Out.GlobalBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)D3D12_BARRIER_SYNC_ALL;
        Out.GlobalBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)D3D12_BARRIER_SYNC_ALL;
        return;
    }

    if (Barrier.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
    {
        Resource = RxUnwrap12<RxResource>(Barrier.UAV.pResource);
        if (Resource == nullptr)
        {
            Out.Type = D3D12DDI_BARRIER_TYPE_GLOBAL;
            Out.GlobalBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)RxUavBufferSync;
            Out.GlobalBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)RxUavBufferSync;
            Out.GlobalBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)RxUavBufferAccess;
            Out.GlobalBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)RxUavBufferAccess;
        }
        else if (Resource->GetApiDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            Out.Type = D3D12DDI_BARRIER_TYPE_BUFFER;
            Out.BufferBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)RxUavBufferSync;
            Out.BufferBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)RxUavBufferSync;
            Out.BufferBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)RxUavBufferAccess;
            Out.BufferBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)RxUavBufferAccess;
            Out.BufferBarrier.hResource = Resource->GetDriverHandle();
        }
        else
        {
            Out.Type = D3D12DDI_BARRIER_TYPE_TEXTURE;
            Out.TextureBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)RxUavTextureSync;
            Out.TextureBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)RxUavTextureSync;
            Out.TextureBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            Out.TextureBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            Out.TextureBarrier.LayoutBefore = (D3D12DDI_BARRIER_LAYOUT)D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
            Out.TextureBarrier.LayoutAfter = (D3D12DDI_BARRIER_LAYOUT)D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
            Out.TextureBarrier.hResource = Resource->GetDriverHandle();
            Out.TextureBarrier.Subresources.IndexOrFirstMipLevel = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        }
        return;
    }

    /* A split barrier's missing half waits on nothing */
    Resource = RxUnwrap12<RxResource>(Barrier.Transition.pResource);
    IsBuffer = Resource != nullptr && Resource->GetApiDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    Before = Barrier.Transition.StateBefore;
    After = Barrier.Transition.StateAfter;

    if (IsBuffer)
    {
        Out.Type = D3D12DDI_BARRIER_TYPE_BUFFER;
        Out.BufferBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)((Barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_END_ONLY) != 0
                                                                   ? D3D12_BARRIER_SYNC_SPLIT
                                                                   : RxLegacySync(Before, TRUE));
        Out.BufferBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)((Barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY) != 0
                                                                  ? D3D12_BARRIER_SYNC_SPLIT
                                                                  : RxLegacySync(After, TRUE));
        Out.BufferBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)RxLegacyAccess(Before, TRUE);
        Out.BufferBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)RxLegacyAccess(After, TRUE);
        Out.BufferBarrier.hResource = Resource->GetDriverHandle();
        return;
    }

    Out.Type = D3D12DDI_BARRIER_TYPE_TEXTURE;
    Out.TextureBarrier.SyncBefore = (D3D12DDI_BARRIER_SYNC)((Barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_END_ONLY) != 0
                                                                ? D3D12_BARRIER_SYNC_SPLIT
                                                                : RxLegacySync(Before, FALSE));
    Out.TextureBarrier.SyncAfter = (D3D12DDI_BARRIER_SYNC)((Barrier.Flags & D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY) != 0
                                                               ? D3D12_BARRIER_SYNC_SPLIT
                                                               : RxLegacySync(After, FALSE));
    Out.TextureBarrier.AccessBefore = (D3D12DDI_BARRIER_ACCESS)RxLegacyAccess(Before, FALSE);
    Out.TextureBarrier.AccessAfter = (D3D12DDI_BARRIER_ACCESS)RxLegacyAccess(After, FALSE);
    Out.TextureBarrier.hResource = RxDriverResourceOf(Barrier.Transition.pResource);
    Out.TextureBarrier.Subresources.IndexOrFirstMipLevel = Barrier.Transition.Subresource;

    /* Simultaneous access textures and copy queue textures keep the common layout */
    if (ListType != D3D12_COMMAND_LIST_TYPE_COPY && Resource != nullptr &&
        (Resource->GetApiDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) == 0)
    {
        Out.TextureBarrier.LayoutBefore = (D3D12DDI_BARRIER_LAYOUT)RxLegacyLayout(Before);
        Out.TextureBarrier.LayoutAfter = (D3D12DDI_BARRIER_LAYOUT)RxLegacyLayout(After);
    }
}

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

    if (Device.GetOptions().EnhancedBarriersSupported && DriverTable->pfnBarrier != nullptr)
    {
        D3D12DDIARG_BARRIER_0094 EnhancedStack[8];
        D3D12DDIARG_BARRIER_0094* Enhanced = EnhancedStack;

        if (NumBarriers > _countof(EnhancedStack))
        {
            Enhanced = new (std::nothrow) D3D12DDIARG_BARRIER_0094[NumBarriers];
            if (Enhanced == nullptr)
                return;
        }

        for (Index = 0; Index < NumBarriers; Index++)
            RxToEnhancedBarrier(pBarriers[Index], Type, Enhanced[Index]);
        DriverTable->pfnBarrier(DriverList, NumBarriers, Enhanced);

        if (Enhanced != EnhancedStack)
            delete[] Enhanced;
        return;
    }

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
