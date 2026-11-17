/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Queries and predication
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

static
RxWarpDevice *
RxWarpGetDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarpDevice>(Device);
}

static
BOOLEAN
RxWarpIsOverflowPredicate(
    _In_ D3D10DDI_QUERY Type)
{
    return Type == D3D10DDI_QUERY_STREAMOVERFLOWPREDICATE ||
           (Type >= D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM0 && Type <= D3D11DDI_QUERY_STREAMOVERFLOWPREDICATE_STREAM3);
}

/* Work runs as soon as it is recorded, so a query's result is final once it ends */
static
BOOL
RxWarpPredicateResult(
    _In_ const RxWarpQuery *Query)
{
    if (RxWarpIsOverflowPredicate(Query->Type))
    {
        return (Query->Stop[RxWarpCountSoNeeded] - Query->Start[RxWarpCountSoNeeded]) !=
               (Query->Stop[RxWarpCountSoWritten] - Query->Start[RxWarpCountSoWritten]);
    }
    return Query->Stop[RxWarpCountSamplesPassed] != Query->Start[RxWarpCountSamplesPassed];
}

BOOLEAN
RxWarpDevice::Predicated(VOID)
{
    if (Predicate == nullptr || !Predicate->Done)
        return FALSE;
    return RxWarpPredicateResult(Predicate) == (PredicateValue != FALSE);
}

static
VOID
RxWarpCreateQueryOfType(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_QUERY Type,
    _In_ D3D10DDI_HQUERY Query)
{
    RxWarpQuery *Object = new (std::nothrow) RxWarpQuery();

    if (Object == nullptr)
    {
        RxWarpGetDevice(Device)->SetError(E_OUTOFMEMORY);
        return;
    }
    Object->Type = Type;
    RxWarpBind(Query, Object);
}

VOID
APIENTRY
RxWarpDdi::CreateQuery(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10DDIARG_CREATEQUERY *Args,
    _In_ D3D10DDI_HQUERY Query,
    _In_ D3D10DDI_HRTQUERY RuntimeQuery)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(RuntimeQuery);
    RxWarpCreateQueryOfType(Device, Args->Query, Query);
}

VOID
APIENTRY
RxWarpDdi::CreateQuery2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDIARG_CREATEQUERY *Args,
    _In_ D3D10DDI_HQUERY Query,
    _In_ D3D10DDI_HRTQUERY RuntimeQuery)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(RuntimeQuery);
    RxWarpCreateQueryOfType(Device, Args->Query, Query);
}

VOID
APIENTRY
RxWarpDdi::DestroyQuery(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HQUERY Query)
{
    RxWarpLock Lock;
    RxWarpDevice *Owner = RxWarpGetDevice(Device);
    RxWarpQuery *Object = RxWarpObject<RxWarpQuery>(Query);

    if (Owner->Predicate == Object)
        Owner->Predicate = nullptr;
    delete Object;
    RxWarpBind<RxWarpQuery>(Query, nullptr);
}

VOID
APIENTRY
RxWarpDdi::QueryBegin(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HQUERY Query)
{
    RxWarpLock Lock;
    RxWarpQuery *Object = RxWarpObject<RxWarpQuery>(Query);

    if (Object == nullptr)
        return;
    memcpy(Object->Start, RxWarpGetDevice(Device)->Counters, sizeof(Object->Start));
    Object->Done = FALSE;
}

VOID
APIENTRY
RxWarpDdi::QueryEnd(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HQUERY Query)
{
    RxWarpLock Lock;
    RxWarpQuery *Object = RxWarpObject<RxWarpQuery>(Query);
    LARGE_INTEGER Now;

    if (Object == nullptr)
        return;
    memcpy(Object->Stop, RxWarpGetDevice(Device)->Counters, sizeof(Object->Stop));
    if (Object->Type == D3D10DDI_QUERY_TIMESTAMP)
    {
        QueryPerformanceCounter(&Now);
        Object->Timestamp = (UINT64)Now.QuadPart;
    }
    Object->Done = TRUE;
}

VOID
APIENTRY
RxWarpDdi::QueryGetData(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HQUERY Query,
    _Out_writes_bytes_opt_(Size) VOID *Data,
    _In_ UINT Size,
    _In_ UINT Flags)
{
    RxWarpLock Lock;
    RxWarpQuery *Object = RxWarpObject<RxWarpQuery>(Query);
    UINT64 Values[11] = {};
    UINT Bytes;

    UNREFERENCED_PARAMETER(Flags);
    if (Object == nullptr)
        return;
    if (!Object->Done)
    {
        RxWarpGetDevice(Device)->SetError(DXGI_DDI_ERR_WASSTILLDRAWING);
        return;
    }

    switch (Object->Type)
    {
        case D3D10DDI_QUERY_EVENT:
        {
            BOOL Value = TRUE;

            memcpy(Values, &Value, sizeof(Value));
            Bytes = sizeof(Value);
            break;
        }
        case D3D10DDI_QUERY_OCCLUSION:
            Values[0] = Object->Stop[RxWarpCountSamplesPassed] - Object->Start[RxWarpCountSamplesPassed];
            Bytes = sizeof(UINT64);
            break;
        case D3D10DDI_QUERY_TIMESTAMP:
            Values[0] = Object->Timestamp;
            Bytes = sizeof(UINT64);
            break;
        case D3D10DDI_QUERY_TIMESTAMPDISJOINT:
        {
            D3D10_DDI_QUERY_DATA_TIMESTAMP_DISJOINT Disjoint = {};
            LARGE_INTEGER Frequency;

            QueryPerformanceFrequency(&Frequency);
            Disjoint.Frequency = (UINT64)Frequency.QuadPart;
            memcpy(Values, &Disjoint, sizeof(Disjoint));
            Bytes = sizeof(Disjoint);
            break;
        }
        case D3D10DDI_QUERY_PIPELINESTATS:
        case D3D11DDI_QUERY_PIPELINESTATS:
            for (UINT Index = RxWarpCountIaVertices; Index <= RxWarpCountCsInvocations; Index++)
                Values[Index] = Object->Stop[Index] - Object->Start[Index];
            Bytes = (Object->Type == D3D11DDI_QUERY_PIPELINESTATS) ? sizeof(D3D11_DDI_QUERY_DATA_PIPELINE_STATISTICS) :
                                                                     sizeof(D3D10_DDI_QUERY_DATA_PIPELINE_STATISTICS);
            break;
        default:
            if (Object->Type == D3D10DDI_QUERY_OCCLUSIONPREDICATE || RxWarpIsOverflowPredicate(Object->Type))
            {
                BOOL Value = RxWarpPredicateResult(Object);

                memcpy(Values, &Value, sizeof(Value));
                Bytes = sizeof(Value);
                break;
            }

            /* Stream output statistics: primitives written, then primitives needed */
            Values[0] = Object->Stop[RxWarpCountSoWritten] - Object->Start[RxWarpCountSoWritten];
            Values[1] = Object->Stop[RxWarpCountSoNeeded] - Object->Start[RxWarpCountSoNeeded];
            Bytes = 2 * sizeof(UINT64);
            break;
    }

    if (Data != nullptr && Size != 0)
        memcpy(Data, Values, (Size < Bytes) ? Size : Bytes);
}

VOID
APIENTRY
RxWarpDdi::SetPredication(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HQUERY Query,
    _In_ BOOL Value)
{
    RxWarpLock Lock;
    RxWarpDevice *Owner = RxWarpGetDevice(Device);

    Owner->Predicate = RxWarpObject<RxWarpQuery>(Query);
    Owner->PredicateValue = (Owner->Predicate != nullptr) ? Value : FALSE;
}
