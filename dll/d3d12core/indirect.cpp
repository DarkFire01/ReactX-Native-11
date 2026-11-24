/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Query heaps and command signatures
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

/* Query heaps */

/* What one query of a heap type resolves to, in bytes */
static
UINT
RxQueryResultSize(
    _In_ D3D12_QUERY_HEAP_TYPE Type)
{
    switch (Type)
    {
        case D3D12_QUERY_HEAP_TYPE_OCCLUSION:
        case D3D12_QUERY_HEAP_TYPE_TIMESTAMP:
        case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
            return sizeof(UINT64);
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS:
            return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS);
        case D3D12_QUERY_HEAP_TYPE_SO_STATISTICS:
            return sizeof(D3D12_QUERY_DATA_SO_STATISTICS);
        case D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS:
            return 4 * sizeof(UINT64);
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1:
            return sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1);
        default:
            return 0;
    }
}

HRESULT
RxQueryHeap::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_QUERY_HEAP_DESC& Desc,
    _In_ UINT DriverNodeMask,
    _Outptr_ RxQueryHeap** Heap)
{
    RxQueryHeap* Created;
    HRESULT hr;

    *Heap = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxQueryHeap(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, DriverNodeMask);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxQueryHeap::Initialize(
    _In_ const D3D12_QUERY_HEAP_DESC& HeapDesc,
    _In_ UINT DriverNodeMask)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_QUERY_HEAP_0001 Args = {};
    HRESULT hr;

    Type = HeapDesc.Type;
    Count = HeapDesc.Count;
    ResultSize = RxQueryResultSize(HeapDesc.Type);

    /* The API and driver heap types share their numbering */
    Args.Type = (D3D12DDI_QUERY_HEAP_TYPE)HeapDesc.Type;
    Args.Count = HeapDesc.Count;
    Args.NodeMask = DriverNodeMask;

    DriverHeap.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateQueryHeapSize(Device.Handle(), &Args));
    if (DriverHeap.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateQueryHeap(Device.Handle(), &Args, DriverHeap);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateQueryHeap failed 0x%08lX", hr);
        return hr;
    }
    IsDriverHeapCreated = TRUE;

    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_QUERY_HEAP);
}

RxQueryHeap::~RxQueryHeap()
{
    if (IsDriverHeapCreated)
        Device.Ddi().pfnDestroyQueryHeap(Device.Handle(), DriverHeap);
}

HRESULT
STDMETHODCALLTYPE
RxQueryHeap::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12QueryHeap) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12QueryHeap*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateQueryHeap(
    _In_ const D3D12_QUERY_HEAP_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvHeap)
{
    RxQueryHeap* Heap;
    UINT DriverMask;
    HRESULT hr;

    if (ppvHeap != nullptr)
        *ppvHeap = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* Core and generic devices only time things */
    if ((Level == D3D_FEATURE_LEVEL_1_0_CORE || Level == D3D_FEATURE_LEVEL_1_0_GENERIC) &&
        pDesc->Type != D3D12_QUERY_HEAP_TYPE_TIMESTAMP && pDesc->Type != D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS)
    {
        return E_INVALIDARG;
    }

    switch (pDesc->Type)
    {
        case D3D12_QUERY_HEAP_TYPE_VIDEO_DECODE_STATISTICS:
            if (!TakesListType(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE))
                return E_INVALIDARG;

            /* These need the driver's video support set up first */
            RX_TRACE(L"video decode statistics query heaps not supported");
            return E_NOTIMPL;
        case D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP:
            if (!Options.CopyQueueTimestampQueriesSupported)
                return E_INVALIDARG;
            break;
        case D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1:
            if (Options.MeshShaderTier == 0)
                return E_INVALIDARG;
            break;
        default:
            if ((UINT)pDesc->Type > D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1)
                return E_INVALIDARG;
            break;
    }

    if (pDesc->Count == 0)
        return E_INVALIDARG;

    hr = TranslateNodeMask(pDesc->NodeMask, TRUE, &DriverMask);
    if (FAILED(hr))
        return hr;

    /* The system fails the call without an out pointer rather than only checking it */
    if (ppvHeap == nullptr)
        return E_UNEXPECTED;

    hr = RxQueryHeap::Create(*this, *pDesc, DriverMask, &Heap);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Heap, riid, ppvHeap);
}

/* Command signatures */

/* Bytes an argument takes in each command */
static
UINT
RxArgumentSize(
    _In_ const D3D12_INDIRECT_ARGUMENT_DESC& Argument)
{
    switch (Argument.Type)
    {
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
            return sizeof(D3D12_DRAW_ARGUMENTS);
        case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            return sizeof(D3D12_VERTEX_BUFFER_VIEW);
        case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
            return sizeof(D3D12_INDEX_BUFFER_VIEW);
        case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
            return sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
            return sizeof(D3D12_DISPATCH_ARGUMENTS);
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            return Argument.Constant.Num32BitValuesToSet * sizeof(UINT);
        case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
        case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
            return sizeof(D3D12_GPU_VIRTUAL_ADDRESS);
        case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
            return sizeof(D3D12_DISPATCH_RAYS_DESC);
        default:
            return 0;
    }
}

/* Root arguments a signature changes come in rising order of slot, then of first constant */
struct RxRootCursor
{
    INT Slot = -1;
    INT LastConstant = -1;
};

static
BOOLEAN
RxAdvanceRootCursor(
    _Inout_ RxRootCursor& Cursor,
    _In_ UINT Slot,
    _In_ UINT FirstConstant,
    _In_ UINT ConstantCount)
{
    if ((INT)Slot < Cursor.Slot)
        return FALSE;
    if ((INT)Slot == Cursor.Slot && (INT)FirstConstant <= Cursor.LastConstant)
        return FALSE;

    Cursor.Slot = (INT)Slot;
    Cursor.LastConstant = (INT)(FirstConstant + ConstantCount - 1);
    return TRUE;
}

/* The root parameter a slot names, or nullptr past the end */
static
const D3D12_ROOT_PARAMETER1*
RxRootSlot(
    _In_ RxRootSignature& Root,
    _In_ UINT Slot)
{
    const D3D12_ROOT_SIGNATURE_DESC2& Layout = Root.GetLayout();

    return Slot < Layout.NumParameters ? &Layout.pParameters[Slot] : nullptr;
}

/* Root constants changed from the argument buffer fit the slot's constants */
static
BOOLEAN
RxRootConstantsFit(
    _In_ RxRootSignature& Root,
    _In_ UINT Slot,
    _In_ UINT FirstConstant,
    _In_ UINT ConstantCount)
{
    const D3D12_ROOT_PARAMETER1* Parameter = RxRootSlot(Root, Slot);
    UINT End = FirstConstant + ConstantCount;

    if (Parameter == nullptr || Parameter->ParameterType != D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS)
        return FALSE;
    if (End < FirstConstant)
        return FALSE;
    return Parameter->Constants.Num32BitValues >= End;
}

static
BOOLEAN
RxRootViewFits(
    _In_ RxRootSignature& Root,
    _In_ UINT Slot,
    _In_ D3D12_ROOT_PARAMETER_TYPE Type)
{
    const D3D12_ROOT_PARAMETER1* Parameter = RxRootSlot(Root, Slot);

    return Parameter != nullptr && Parameter->ParameterType == Type;
}

/* The rules of D3D12_COMMAND_SIGNATURE_DESC; the root signature is given exactly when root arguments change */
static
HRESULT
RxValidateCommandSignature(
    _In_ RxDevice12& Device,
    _In_ const D3D12_COMMAND_SIGNATURE_DESC& Desc,
    _In_opt_ RxRootSignature* Root)
{
    const D3D12_INDIRECT_ARGUMENT_DESC* Arguments = Desc.pArgumentDescs;
    D3D12_INDIRECT_ARGUMENT_TYPE Command;
    BOOLEAN HasCommand = FALSE;
    BOOLEAN ChangesRoot = FALSE;
    BOOLEAN IndexSeen = FALSE;
    BOOLEAN Failed = FALSE;
    RxRootCursor Cursor;
    UINT VertexSlots = 0;
    UINT Needed = 0;
    UINT Index;

    if (Device.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return E_INVALIDARG;

    /* Exactly one draw or dispatch, and it comes last */
    for (Index = 0; Index < Desc.NumArgumentDescs; Index++)
    {
        if (HasCommand)
            return E_INVALIDARG;

        switch (Arguments[Index].Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS:
                if ((INT)Device.GetOptions().RaytracingTier < D3D12_RAYTRACING_TIER_1_1)
                    return E_INVALIDARG;
                HasCommand = TRUE;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
            case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH:
                HasCommand = TRUE;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                ChangesRoot = TRUE;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
                ChangesRoot = TRUE;
                if ((INT)Device.GetExecuteIndirectTier() < D3D12_EXECUTE_INDIRECT_TIER_1_1)
                    return E_INVALIDARG;
                break;
            default:
                return E_INVALIDARG;
        }
    }
    if (!HasCommand)
        return E_INVALIDARG;
    if (ChangesRoot != (Root != nullptr))
        return E_INVALIDARG;

    /* Slot and stride mistakes are all looked at before the call fails */
    Command = Arguments[Desc.NumArgumentDescs - 1].Type;
    for (Index = 0; Index < Desc.NumArgumentDescs; Index++)
    {
        const D3D12_INDIRECT_ARGUMENT_DESC& Argument = Arguments[Index];

        switch (Argument.Type)
        {
            case D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW:
                if (Command == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH || Command == D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_RAYS)
                    Failed = TRUE;
                if (Argument.VertexBuffer.Slot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
                    Failed = TRUE;
                if ((VertexSlots & (1u << (Argument.VertexBuffer.Slot & 31))) != 0)
                    Failed = TRUE;
                VertexSlots |= 1u << (Argument.VertexBuffer.Slot & 31);
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW:
                if (Command != D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED || IndexSeen)
                    Failed = TRUE;
                IndexSeen = TRUE;
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT:
                if (Argument.Constant.Num32BitValuesToSet == 0)
                    return E_INVALIDARG;
                if (!RxAdvanceRootCursor(Cursor, Argument.Constant.RootParameterIndex,
                                         Argument.Constant.DestOffsetIn32BitValues, Argument.Constant.Num32BitValuesToSet) ||
                    !RxRootConstantsFit(*Root, Argument.Constant.RootParameterIndex,
                                        Argument.Constant.DestOffsetIn32BitValues, Argument.Constant.Num32BitValuesToSet))
                {
                    return E_INVALIDARG;
                }
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT:
                if (!RxAdvanceRootCursor(Cursor, Argument.IncrementingConstant.RootParameterIndex,
                                         Argument.IncrementingConstant.DestOffsetIn32BitValues, 1) ||
                    !RxRootConstantsFit(*Root, Argument.IncrementingConstant.RootParameterIndex,
                                        Argument.IncrementingConstant.DestOffsetIn32BitValues, 1))
                {
                    return E_INVALIDARG;
                }
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW:
                if (!RxAdvanceRootCursor(Cursor, Argument.ConstantBufferView.RootParameterIndex, 0, 1) ||
                    !RxRootViewFits(*Root, Argument.ConstantBufferView.RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_CBV))
                {
                    return E_INVALIDARG;
                }
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_SHADER_RESOURCE_VIEW:
                if (!RxAdvanceRootCursor(Cursor, Argument.ShaderResourceView.RootParameterIndex, 0, 1) ||
                    !RxRootViewFits(*Root, Argument.ShaderResourceView.RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_SRV))
                {
                    return E_INVALIDARG;
                }
                break;
            case D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW:
                if (!RxAdvanceRootCursor(Cursor, Argument.UnorderedAccessView.RootParameterIndex, 0, 1) ||
                    !RxRootViewFits(*Root, Argument.UnorderedAccessView.RootParameterIndex, D3D12_ROOT_PARAMETER_TYPE_UAV))
                {
                    return E_INVALIDARG;
                }
                break;
            default:
                break;
        }
    }

    for (Index = 0; Index < Desc.NumArgumentDescs; Index++)
        Needed += RxArgumentSize(Arguments[Index]);
    if ((Desc.ByteStride & 3) != 0 || Desc.ByteStride < Needed)
        Failed = TRUE;

    return Failed ? E_INVALIDARG : S_OK;
}

HRESULT
RxCommandSignature::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_COMMAND_SIGNATURE_DESC& Desc,
    _In_opt_ RxRootSignature* Root,
    _In_ UINT DriverNodeMask,
    _Outptr_ RxCommandSignature** Signature)
{
    RxCommandSignature* Created;
    HRESULT hr;

    *Signature = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxCommandSignature(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, Root, DriverNodeMask);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Signature = Created;
    return S_OK;
}

HRESULT
RxCommandSignature::Initialize(
    _In_ const D3D12_COMMAND_SIGNATURE_DESC& Desc,
    _In_opt_ RxRootSignature* RootSignature,
    _In_ UINT DriverNodeMask)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_COMMAND_SIGNATURE_0001 Args = {};
    HRESULT hr;

    ByteStride = Desc.ByteStride;
    ArgumentCount = Desc.NumArgumentDescs;
    Arguments = new (std::nothrow) D3D12_INDIRECT_ARGUMENT_DESC[ArgumentCount];
    if (Arguments == nullptr)
        return E_OUTOFMEMORY;
    memcpy(Arguments, Desc.pArgumentDescs, ArgumentCount * sizeof(*Arguments));
    CommandType = Arguments[ArgumentCount - 1].Type;

    /* The signature keeps the root signature the application gave */
    Root = RootSignature;
    if (Root != nullptr)
        Root->AddRef();

    /* The driver's argument desc has the API's layout */
    static_assert(sizeof(D3D12DDI_INDIRECT_ARGUMENT_DESC) == sizeof(D3D12_INDIRECT_ARGUMENT_DESC));
    Args.ByteStride = ByteStride;
    Args.NumArgumentDescs = ArgumentCount;
    Args.pArgumentDescs = reinterpret_cast<const D3D12DDI_INDIRECT_ARGUMENT_DESC*>(Arguments);
    if (Root != nullptr)
        Args.hRootSignature = Root->GetDriverHandle();
    Args.NodeMask = DriverNodeMask;

    DriverSignature.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateCommandSignatureSize(Device.Handle(), &Args));
    if (DriverSignature.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateCommandSignature(Device.Handle(), &Args, DriverSignature);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateCommandSignature failed 0x%08lX", hr);
        return hr;
    }
    IsDriverSignatureCreated = TRUE;

    /* Like pipeline states it has no allocations, so residency is asked for nothing */
    return Device.MakePageableResident(nullptr, D3D12DDI_HT_COMMAND_SIGNATURE);
}

RxCommandSignature::~RxCommandSignature()
{
    if (IsDriverSignatureCreated)
        Device.Ddi().pfnDestroyCommandSignature(Device.Handle(), DriverSignature);
    if (Root != nullptr)
        Root->Release();
    delete[] Arguments;
}

HRESULT
STDMETHODCALLTYPE
RxCommandSignature::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12CommandSignature) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12CommandSignature*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommandSignature(
    _In_ const D3D12_COMMAND_SIGNATURE_DESC* pDesc,
    _In_opt_ ID3D12RootSignature* pRootSignature,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvCommandSignature)
{
    RxCommandSignature* Signature;
    RxRootSignature* Root = nullptr;
    UINT DriverMask;
    HRESULT hr;

    if (ppvCommandSignature != nullptr)
        *ppvCommandSignature = nullptr;
    if (pDesc == nullptr || (pDesc->NumArgumentDescs != 0 && pDesc->pArgumentDescs == nullptr))
        return E_INVALIDARG;

    if (pRootSignature != nullptr)
    {
        Root = RxUnwrap12<RxRootSignature>(pRootSignature);
        if (Root == nullptr)
            return E_INVALIDARG;
    }

    hr = RxValidateCommandSignature(*this, *pDesc, Root);
    if (FAILED(hr))
        return hr;

    hr = TranslateNodeMask(pDesc->NodeMask, FALSE, &DriverMask);
    if (FAILED(hr))
        return hr;

    /* The system fails the call without an out pointer rather than only checking it */
    if (ppvCommandSignature == nullptr)
        return E_UNEXPECTED;

    hr = RxCommandSignature::Create(*this, *pDesc, Root, DriverMask, &Signature);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Signature, riid, ppvCommandSignature);
}
