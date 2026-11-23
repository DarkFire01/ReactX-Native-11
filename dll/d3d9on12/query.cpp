/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Queries: events, occlusion, timestamps and the constant vertex cache answer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Query types the runtime asks for by number */
static constexpr UINT NineQueryVcache = 4;
static constexpr UINT NineQueryEvent = 8;
static constexpr UINT NineQueryOcclusion = 9;
static constexpr UINT NineQueryTimestamp = 10;
static constexpr UINT NineQueryTimestampDisjoint = 11;
static constexpr UINT NineQueryTimestampFrequency = 12;

/* Types from here on are counters and private types, none of which are answered */
static constexpr UINT NineQueryFirstCounter = 0x40000000;

static constexpr UINT NineIssueBegin = 0x1;
static constexpr UINT NineIssueEnd = 0x2;

/* The vertex cache answer: the "CACH" tag and no cache description */
static constexpr DWORD NineVcacheTag = 0x48434143;

/* One query: its type and the translation layer query behind it, if the type needs one */
struct NineQuery
{
    UINT Type;
    std::unique_ptr<TL::Async> Async;
    UINT64 Reserved;
};

/* Builds the translation layer query a type is answered from */
static
VOID
NineInitQuery(
    _Inout_ NineQuery* Query,
    _In_ NineDevice* Device)
{
    TL::ImmediateContext* Context = &*Device->Context;
    UINT Mask = 0;

    switch (Query->Type)
    {
        case NineQueryEvent:
            /* Events only follow the graphics queue */
            if (Context->GetCommandListManager(TL::COMMAND_LIST_TYPE::GRAPHICS) != nullptr)
                Mask = TL::COMMAND_LIST_TYPE_GRAPHICS_MASK;
            Query->Async.reset(new TL::EventQuery(Context, Mask));
            break;
        case NineQueryOcclusion:
            Query->Async.reset(new TL::Query(Context, TL::e_QUERY_OCCLUSION, TL::COMMAND_LIST_TYPE_GRAPHICS_MASK));
            break;
        case NineQueryTimestamp:
            Query->Async.reset(new TL::Query(Context, TL::e_QUERY_TIMESTAMP, TL::COMMAND_LIST_TYPE_GRAPHICS_MASK));
            break;
        case NineQueryTimestampDisjoint:
        case NineQueryTimestampFrequency:
            Query->Async.reset(new TL::TimestampDisjointQuery(Context, TL::COMMAND_LIST_TYPE_GRAPHICS_MASK));
            break;
        default:
            return;
    }
    Query->Async->Initialize();
}

/* Answers without waiting: S_FALSE while the GPU has not reached the query */
static
HRESULT
NineQueryAnswer(
    _In_ NineQuery* Query,
    _Out_ VOID* Data)
{
    BYTE Raw[16] = {};
    UINT Size = 0;

    if (Query->Type >= NineQueryFirstCounter)
        return E_NOTIMPL;

    switch (Query->Type)
    {
        case NineQueryVcache:
            ZeroMemory(Data, 16);
            *static_cast<DWORD*>(Data) = NineVcacheTag;
            return S_OK;
        case NineQueryEvent:
            Size = sizeof(BOOL);
            break;
        case NineQueryOcclusion:
        case NineQueryTimestamp:
            Size = sizeof(UINT64);
            break;
        case NineQueryTimestampDisjoint:
        case NineQueryTimestampFrequency:
            Size = sizeof(TL::QUERY_DATA_TIMESTAMP_DISJOINT);
            break;
        default:
            return E_NOTIMPL;
    }

    /* Submitting is left to the runtime's own flush, which comes before every poll that asks for one */
    if (!Query->Async->GetData(Raw, Size, true, false))
        return S_FALSE;

    /* Occlusion counts and timestamps reach the runtime as their low 32 bits only */
    if (Query->Type == NineQueryTimestampFrequency)
    {
        const TL::QUERY_DATA_TIMESTAMP_DISJOINT* Disjoint = reinterpret_cast<const TL::QUERY_DATA_TIMESTAMP_DISJOINT*>(Raw);

        *static_cast<UINT64*>(Data) = Disjoint->Disjoint ? 0 : Disjoint->Frequency;
    }
    else if (Query->Type == NineQueryTimestampDisjoint)
    {
        *static_cast<DWORD*>(Data) = reinterpret_cast<const TL::QUERY_DATA_TIMESTAMP_DISJOINT*>(Raw)->Disjoint;
    }
    else
    {
        CopyMemory(Data, Raw, sizeof(DWORD));
    }
    return S_OK;
}

HRESULT
APIENTRY
NineCreateQuery(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEQUERY* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineQuery* Query;
    HRESULT hr = S_OK;

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    Query = new (std::nothrow) NineQuery();
    if (Query == nullptr)
        return E_OUTOFMEMORY;
    Query->Type = Args->QueryType;
    Query->Reserved = ~0ULL;

    try
    {
        NineInitQuery(Query, Device);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    if (FAILED(hr))
    {
        NineTrace(L"query type %u is not created (0x%08lX)", Args->QueryType, hr);
        delete Query;
        return NineDxgiResult(hr);
    }

    Args->hQuery = Query;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineDestroyQuery(
    _In_ HANDLE hDevice,
    _In_ HANDLE hQuery)
{
    if (hDevice == NULL || hQuery == NULL)
        return E_INVALIDARG;

    delete static_cast<NineQuery*>(hQuery);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineIssueQuery(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_ISSUEQUERY* Args)
{
    NineQuery* Query;

    if (hDevice == NULL || Args == nullptr || Args->hQuery == NULL)
        return E_INVALIDARG;

    Query = static_cast<NineQuery*>(Args->hQuery);
    if (Query->Type >= NineQueryFirstCounter)
        return NineDxgiResult(E_NOTIMPL);

    switch (Query->Type)
    {
        case NineQueryVcache:
            break;
        case NineQueryEvent:
        case NineQueryOcclusion:
        case NineQueryTimestamp:
        case NineQueryTimestampDisjoint:
        case NineQueryTimestampFrequency:
            if ((Args->Flags.Value & NineIssueBegin) != 0)
                Query->Async->Begin();
            else if ((Args->Flags.Value & NineIssueEnd) != 0)
                Query->Async->End();
            break;
        default:
            return NineDxgiResult(E_NOTIMPL);
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineGetQueryData(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_GETQUERYDATA* Args)
{
    if (hDevice == NULL || Args == nullptr || Args->hQuery == NULL)
        return E_INVALIDARG;

    return NineDxgiResult(NineQueryAnswer(static_cast<NineQuery*>(Args->hQuery), Args->pData));
}
