/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Queries, predication, mip generation and resource LOD clamps
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "queries.hpp"
#include "views.hpp"

/*
 * The driver numbers the D3D11 statistics and per stream kinds after the D3D10 ones.
 * The API alternates statistics and overflow per stream; the driver groups them by kind.
 */
static
D3D10DDI_QUERY
RxDdiQueryKind(
    _In_ D3D11_QUERY Kind)
{
    UINT Stream;

    if (Kind == D3D11_QUERY_PIPELINE_STATISTICS)
        return D3D11DDI_QUERY_PIPELINESTATS;
    if (Kind < D3D11_QUERY_SO_STATISTICS_STREAM0)
        return (D3D10DDI_QUERY)Kind;

    Stream = (Kind - D3D11_QUERY_SO_STATISTICS_STREAM0) / 2;
    if (((Kind - D3D11_QUERY_SO_STATISTICS_STREAM0) % 2) == 0)
        return (D3D10DDI_QUERY)(D3D11DDI_QUERY_STREAMOUTPUTSTATS_STREAM0 + Stream);

    return (D3D10DDI_QUERY)(D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0 + Stream);
}

UINT
RxQuery::DataSizeOf(
    _In_ D3D11_QUERY Kind)
{
    switch (Kind)
    {
        case D3D11_QUERY_EVENT:
        case D3D11_QUERY_OCCLUSION_PREDICATE:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM0:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM1:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM2:
        case D3D11_QUERY_SO_OVERFLOW_PREDICATE_STREAM3:
            return sizeof(BOOL);
        case D3D11_QUERY_OCCLUSION:
        case D3D11_QUERY_TIMESTAMP:
            return sizeof(UINT64);
        case D3D11_QUERY_TIMESTAMP_DISJOINT:
            return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
        case D3D11_QUERY_PIPELINE_STATISTICS:
            return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
        case D3D11_QUERY_SO_STATISTICS:
        case D3D11_QUERY_SO_STATISTICS_STREAM0:
        case D3D11_QUERY_SO_STATISTICS_STREAM1:
        case D3D11_QUERY_SO_STATISTICS_STREAM2:
        case D3D11_QUERY_SO_STATISTICS_STREAM3:
            return sizeof(D3D11_QUERY_DATA_SO_STATISTICS);
        default:
            return 0;
    }
}

/* Only these two can drive predication; the per stream overflow kinds are queries only */
BOOLEAN
RxQuery::IsPredicateKind(
    _In_ D3D11_QUERY Kind)
{
    return Kind == D3D11_QUERY_OCCLUSION_PREDICATE || Kind == D3D11_QUERY_SO_OVERFLOW_PREDICATE;
}

RxQuery::~RxQuery()
{
    if (IsCreated)
        Device.GetDeviceFuncs().pfnDestroyQuery(Device.GetDriverDevice(), Handle);
}

HRESULT
RxQuery::Create(
    _In_ RxDevice& Device,
    _In_ const D3D11_QUERY_DESC& Desc,
    _Outptr_ RxQuery** Query)
{
    const D3DWDDM2_0DDI_DEVICEFUNCS& Funcs = Device.GetWddm2DeviceFuncs();
    D3DWDDM2_0DDIARG_CREATEQUERY Args = {};
    D3D10DDI_HRTQUERY RuntimeHandle;
    RxQuery* NewQuery;
    HRESULT hr;

    *Query = nullptr;

    NewQuery = new (std::nothrow) RxQuery(Device);
    if (NewQuery == nullptr)
        return E_OUTOFMEMORY;

    NewQuery->Desc = Desc;
    Args.Query = RxDdiQueryKind(Desc.Query);
    Args.MiscFlags = Desc.MiscFlags;

    /* Drivers before WDDM 2.0 stop reading ahead of the context type */
    Args.ContextType = D3DWDDM2_0DDI_CONTEXTTYPE_3D;

    NewQuery->Handle.pDrvPrivate = NewQuery->Memory.Allocate(Funcs.pfnCalcPrivateQuerySize(Device.GetDriverDevice(), &Args));
    if (NewQuery->Handle.pDrvPrivate == nullptr)
    {
        NewQuery->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<ID3D11Predicate*>(NewQuery);
    Funcs.pfnCreateQuery(Device.GetDriverDevice(), &Args, NewQuery->Handle, RuntimeHandle);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateQuery %u failed 0x%08lX", Desc.Query, hr);
        NewQuery->Release();
        return hr;
    }

    NewQuery->IsCreated = TRUE;
    *Query = NewQuery;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxQuery::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Asynchronous) || riid == __uuidof(ID3D11Query) ||
        (riid == __uuidof(ID3D11Predicate) && IsPredicateKind(Desc.Query)))
    {
        *ppvObject = static_cast<ID3D11Predicate*>(this);
        AddRef();
        return S_OK;
    }

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

/* Device */

static
BOOLEAN
RxIsValidQueryDesc(
    _In_opt_ const D3D11_QUERY_DESC* Desc)
{
    if (Desc == nullptr || RxQuery::DataSizeOf(Desc->Query) == 0 ||
        (Desc->MiscFlags & ~D3D11_QUERY_MISC_PREDICATEHINT) != 0)
    {
        return FALSE;
    }

    /* The hint only means something for a predicate */
    return !(Desc->MiscFlags & D3D11_QUERY_MISC_PREDICATEHINT) || RxQuery::IsPredicateKind(Desc->Query);
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateQuery(
    _In_ const D3D11_QUERY_DESC* pQueryDesc,
    _COM_Outptr_opt_ ID3D11Query** ppQuery)
{
    RxQuery* Query;
    HRESULT hr;

    if (ppQuery != nullptr)
        *ppQuery = nullptr;
    if (!RxIsValidQueryDesc(pQueryDesc))
        return E_INVALIDARG;

    hr = RxQuery::Create(*this, *pQueryDesc, &Query);
    if (FAILED(hr))
        return hr;

    /* A null output pointer only validates the arguments */
    if (ppQuery == nullptr)
    {
        Query->Release();
        return S_FALSE;
    }

    *ppQuery = Query;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CreatePredicate(
    _In_ const D3D11_QUERY_DESC* pPredicateDesc,
    _COM_Outptr_opt_ ID3D11Predicate** ppPredicate)
{
    RxQuery* Query;
    HRESULT hr;

    if (ppPredicate != nullptr)
        *ppPredicate = nullptr;
    if (!RxIsValidQueryDesc(pPredicateDesc) || !RxQuery::IsPredicateKind(pPredicateDesc->Query))
        return E_INVALIDARG;

    hr = RxQuery::Create(*this, *pPredicateDesc, &Query);
    if (FAILED(hr))
        return hr;

    if (ppPredicate == nullptr)
    {
        Query->Release();
        return S_FALSE;
    }

    *ppPredicate = Query;
    return S_OK;
}

/* Immediate context */

VOID
STDMETHODCALLTYPE
RxImmediateContext::Begin(
    _In_ ID3D11Asynchronous* pAsync)
{
    RxQuery* Query = RxUnwrap<RxQuery>(pAsync);

    /* Events and timestamps only mark a point, so they have no beginning */
    if (Query == nullptr || Query->GetQueryDesc().Query == D3D11_QUERY_EVENT ||
        Query->GetQueryDesc().Query == D3D11_QUERY_TIMESTAMP)
    {
        return;
    }

    Device.GetDeviceFuncs().pfnQueryBegin(Device.GetDriverDevice(), Query->GetHandle());
    Query->IsBuilding = TRUE;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::End(
    _In_ ID3D11Asynchronous* pAsync)
{
    RxQuery* Query = RxUnwrap<RxQuery>(pAsync);
    D3D11_QUERY Kind;

    if (Query == nullptr)
        return;

    /* Ranged queries need a Begin first */
    Kind = Query->GetQueryDesc().Query;
    if (!Query->IsBuilding && Kind != D3D11_QUERY_EVENT && Kind != D3D11_QUERY_TIMESTAMP)
        return;

    Device.GetDeviceFuncs().pfnQueryEnd(Device.GetDriverDevice(), Query->GetHandle());
    Query->IsBuilding = FALSE;
    Query->IsIssued = TRUE;
}

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::GetData(
    _In_ ID3D11Asynchronous* pAsync,
    _Out_writes_bytes_opt_(DataSize) void* pData,
    _In_ UINT DataSize,
    _In_ UINT GetDataFlags)
{
    RxQuery* Query = RxUnwrap<RxQuery>(pAsync);
    HRESULT hr;

    if (Query == nullptr || (GetDataFlags & ~D3D11_ASYNC_GETDATA_DONOTFLUSH) != 0)
        return E_INVALIDARG;
    if (pData != nullptr && DataSize != Query->GetDataSize())
        return E_INVALIDARG;
    if (pData == nullptr && DataSize != 0)
        return E_INVALIDARG;
    if (!Query->IsIssued || Query->IsBuilding)
        return DXGI_ERROR_INVALID_CALL;

    Device.GetDeviceFuncs().pfnQueryGetData(Device.GetDriverDevice(), Query->GetHandle(), pData, DataSize,
                                            GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH);
    hr = Device.TakeDriverError();
    if (hr == DXGI_DDI_ERR_WASSTILLDRAWING)
    {
        /* Make sure the work behind the answer is on its way, unless asked not to */
        if (!(GetDataFlags & D3D11_ASYNC_GETDATA_DONOTFLUSH))
            Flush();
        return S_FALSE;
    }

    return hr;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SetPredication(
    _In_opt_ ID3D11Predicate* pPredicate,
    _In_ BOOL PredicateValue)
{
    RxQuery* Query = RxUnwrap<RxQuery>(pPredicate);
    D3D10DDI_HQUERY Handle = {};

    if (Query != nullptr)
        Handle = Query->GetHandle();

    Device.GetDeviceFuncs().pfnSetPredication(Device.GetDriverDevice(), Handle, PredicateValue);
    Pipeline.Predicate = RxBound<RxQuery>(Query);
    Pipeline.PredicateValue = PredicateValue;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GetPredication(
    _Outptr_opt_result_maybenull_ ID3D11Predicate** ppPredicate,
    _Out_opt_ BOOL* pPredicateValue)
{
    if (ppPredicate != nullptr)
        *ppPredicate = RxHandBack<ID3D11Predicate>(Pipeline.Predicate);
    if (pPredicateValue != nullptr)
        *pPredicateValue = Pipeline.PredicateValue;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GenerateMips(
    _In_ ID3D11ShaderResourceView* pShaderResourceView)
{
    RxShaderResourceView* View = RxUnwrap<RxShaderResourceView>(pShaderResourceView);
    D3D11_RESOURCE_MISC_FLAG Needed = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    RxResourceInfo Info;
    ID3D11Resource* Resource;

    if (View == nullptr)
        return;

    /* Only resources made for it can have their mips generated */
    pShaderResourceView->GetResource(&Resource);
    RxDescribeResource(Resource, &Info);
    Resource->Release();
    if (!(Info.MiscFlags & Needed))
        return;

    View->GetBacking()->NoteWrite();
    Device.GetDeviceFuncs().pfnGenMips(Device.GetDriverDevice(), View->GetHandle());
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SetResourceMinLOD(
    _In_ ID3D11Resource* pResource,
    FLOAT MinLOD)
{
    DdiResource* Resource = RxGetDdiResource(pResource);

    if (Resource == nullptr)
        return;

    Resource->MinLod = MinLOD;
    Device.GetDeviceFuncs().pfnSetResourceMinLOD(Device.GetDriverDevice(), Resource->GetHandle(), MinLOD);
}

FLOAT
STDMETHODCALLTYPE
RxImmediateContext::GetResourceMinLOD(
    _In_ ID3D11Resource* pResource)
{
    DdiResource* Resource = RxGetDdiResource(pResource);

    return (Resource != nullptr) ? Resource->MinLod : 0.0f;
}
