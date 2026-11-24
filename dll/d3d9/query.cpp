/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Queries: driver queries and the memory pressure query the runtime answers itself
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include <intsafe.h>

/* A new query reads as this byte until a result lands */
static constexpr BYTE D9QueryFill = 0xDD;

/* Driver issue flags: the start of a bracket and its end */
static constexpr UINT D9DriverIssueBegin = 0x1;
static constexpr UINT D9DriverIssueEnd = 0x2;

/* Video memory statistics of this process on the adapter, folded into what the memory pressure query reports */
static
HRESULT
D9ReadMemoryPressure(
    _In_ const LUID* AdapterLuid,
    _Out_ D3DMEMORYPRESSURE* Pressure)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYSTATISTICS Stats = {};
    const D3DKMT_QUERYSTATISTICS_COMMITMENT_DATA* Commit;
    ULONGLONG Weighted[3];
    ULONGLONG Total;
    ULONGLONG Score;
    NTSTATUS Status;

    ZeroMemory(Pressure, sizeof(*Pressure));
    Stats.Type = D3DKMT_QUERYSTATISTICS_PROCESS_ADAPTER;
    Stats.AdapterLuid = *AdapterLuid;
    Status = Kmt != nullptr && Kmt->QueryStatistics != nullptr ? Kmt->QueryStatistics(&Stats) : STATUS_NOT_IMPLEMENTED;
    if (!NT_SUCCESS(Status))
        return HRESULT_FROM_NT(Status);

    Commit = &Stats.QueryResult.ProcessAdapterInformation.CommitmentData;
    Pressure->BytesEvictedFromProcess = Commit->TotalBytesEvictedFromProcess;

    /* Bytes that landed in the second, third and fourth preferred segments count as badly placed */
    Total = Commit->BytesBySegmentPreference[2] + Commit->BytesBySegmentPreference[3];
    if (Total < Commit->BytesBySegmentPreference[2])
        return E_FAIL;
    if (Total + Commit->BytesBySegmentPreference[4] < Total)
    {
        Pressure->SizeOfInefficientAllocation = ~0ULL;
        return E_FAIL;
    }
    Total += Commit->BytesBySegmentPreference[4];
    Pressure->SizeOfInefficientAllocation = Total;
    if (Total == 0)
    {
        Pressure->LevelOfEfficiency = 100;
        return S_OK;
    }

    if (FAILED(ULongLongMult(Commit->BytesBySegmentPreference[2], 80, &Weighted[0])) ||
        FAILED(ULongLongMult(Commit->BytesBySegmentPreference[3], 50, &Weighted[1])) ||
        FAILED(ULongLongMult(Commit->BytesBySegmentPreference[4], 20, &Weighted[2])))
    {
        return E_FAIL;
    }
    Score = Weighted[0] + Weighted[1];
    if (Score < Weighted[1])
        return E_FAIL;
    if (Score + Weighted[2] < Weighted[2])
        return E_FAIL;
    Score += Weighted[2];
    Pressure->LevelOfEfficiency = static_cast<UINT>(Score / Total);
    return S_OK;
}

D9Query::D9Query(
    _In_ D9Device* Owner,
    _In_ D3DQUERYTYPE Type)
    : D9Child(Owner),
      Kind(Type),
      DataSize(ResultSize(Type))
{
    FillMemory(Stored, sizeof(Stored), D9QueryFill);
}

DWORD
D9Query::ResultSize(
    _In_ D3DQUERYTYPE Type)
{
    switch (static_cast<UINT>(Type))
    {
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        return sizeof(DWORD);
    case D3DQUERYTYPE_VERTEXSTATS:
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
    case D3DQUERYTYPE_VERTEXTIMINGS:
    case D3DQUERYTYPE_PIXELTIMINGS:
    case D3DQUERYTYPE_CACHEUTILIZATION:
        return sizeof(UINT64);
    case D3DQUERYTYPE_VCACHE:
    case 7:
    case D3DQUERYTYPE_PIPELINETIMINGS:
        return sizeof(D3DDEVINFO_VCACHE);
    case D3DQUERYTYPE_INTERFACETIMINGS:
    case D3DQUERYTYPE_BANDWIDTHTIMINGS:
        return sizeof(D3DDEVINFO_D3D9INTERFACETIMINGS);
    case D3DQUERYTYPE_MEMORYPRESSURE:
        return sizeof(D3DMEMORYPRESSURE);
    case D3DQUERYTYPE_RESOURCEMANAGER:
        return sizeof(D3DDEVINFO_RESOURCEMANAGER);
    default:
        return 0;
    }
}

STDMETHODIMP
D9Query::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return D3DERR_INVALIDCALL;

    *Object = nullptr;
    if (Device->AwaitsReset())
        return D3DERR_INVALIDCALL;
    if (IsEqualGUID(Iid, __uuidof(IDirect3DQuery9)) || IsEqualGUID(Iid, __uuidof(IUnknown)))
    {
        *Object = static_cast<IDirect3DQuery9*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Query::AddRef()
{
    return AddRefChild();
}

/* The driver query goes before the reference on the device does */
STDMETHODIMP_(ULONG)
D9Query::Release()
{
    LONG Count = InterlockedDecrement(&RefCount);
    D9Device* Owner = Device;

    if (Count != 0)
        return static_cast<ULONG>(Count);
    Owner->DestroyDriverQuery(this);
    delete this;
    Owner->Release();
    return 0;
}

STDMETHODIMP
D9Query::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Device->AwaitsReset() || Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

STDMETHODIMP_(D3DQUERYTYPE)
D9Query::GetType()
{
    return Kind;
}

STDMETHODIMP_(DWORD)
D9Query::GetDataSize()
{
    return DataSize;
}

STDMETHODIMP
D9Query::Issue(
    _In_ DWORD Flags)
{
    return Device->IssueQuery(this, Flags);
}

STDMETHODIMP
D9Query::GetData(
    _Out_writes_bytes_opt_(Size) VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    return Device->ReadQuery(this, Data, Size, Flags);
}

/* Types 5 to 7 never exist; the memory pressure query needs no driver; others need the driver to list them */
STDMETHODIMP
D9Device::CreateQuery(
    _In_ D3DQUERYTYPE Type,
    _Out_ IDirect3DQuery9** Query)
{
    D9_DEVICE_LOCK();
    D9Query* Made;
    BOOLEAN Listed = FALSE;
    HRESULT hr;

    if (NotReset)
        return D3DERR_INVALIDCALL;

    if (Type >= D3DQUERYTYPE_RESOURCEMANAGER && Type <= static_cast<D3DQUERYTYPE>(7))
        return D3DERR_NOTAVAILABLE;
    if (Type != D3DQUERYTYPE_MEMORYPRESSURE)
    {
        for (UINT Index = 0; Index < Adapter->QueryCount && !Listed; Index++)
            Listed = static_cast<UINT>(Adapter->Queries[Index]) == static_cast<UINT>(Type);
        if (!Listed)
            return D3DERR_NOTAVAILABLE;
    }
    if (Query == nullptr)
        return S_OK;

    Made = new (std::nothrow) D9Query(this, Type);
    if (Made == nullptr)
        return E_OUTOFMEMORY;
    hr = CreateDriverQuery(Made);
    if (FAILED(hr))
    {
        D9_TRACE_ONCE(L"the driver failed to create a query of type %u (0x%08lX)", Type, hr);
        delete Made;
        return hr;
    }
    Made->AddRef();
    *Query = Made;
    return S_OK;
}

HRESULT
D9Device::CreateDriverQuery(
    _Inout_ D9Query* Query)
{
    D3DDDIARG_CREATEQUERY Args = {};
    HRESULT hr;

    if (Query->Kind == D3DQUERYTYPE_MEMORYPRESSURE)
        return S_OK;

    Args.QueryType = static_cast<D3DDDIQUERYTYPE>(Query->Kind);
    if (Batch != nullptr)
        Batch->WaitForWorker();
    hr = DeviceFunctions.pfnCreateQuery(DriverDevice, &Args);
    if (FAILED(hr))
        return E_FAIL;
    Query->DriverQuery = Args.hQuery;
    return S_OK;
}

VOID
D9Device::DestroyDriverQuery(
    _In_ D9Query* Query)
{
    D9_DEVICE_LOCK();

    if (Query->Kind == D3DQUERYTYPE_MEMORYPRESSURE)
        return;
    if (Batch != nullptr)
        Batch->Settle(Query->DriverQuery);
    DeviceFunctions.pfnDestroyQuery(DriverDevice, Query->DriverQuery);
}

/* An end leaves the query waiting on the driver; anything else starts it over. The memory pressure query takes
   its snapshots here: the begin keeps the counters and the end turns the eviction count into a difference */
HRESULT
D9Device::IssueQuery(
    _Inout_ D9Query* Query,
    _In_ DWORD Flags)
{
    D9_DEVICE_LOCK();
    D3DDDIARG_ISSUEQUERY Args = {};
    D3DMEMORYPRESSURE* Kept = reinterpret_cast<D3DMEMORYPRESSURE*>(Query->Stored);
    D3DMEMORYPRESSURE Now;

    if (NotReset)
        return D3DERR_INVALIDCALL;

    Query->State = (Flags & D3DISSUE_END) != 0 ? D9QueryState::Issued : D9QueryState::Building;
    if (Query->Kind == D3DQUERYTYPE_MEMORYPRESSURE)
    {
        if ((Flags & D3DISSUE_BEGIN) != 0)
        {
            D9ReadMemoryPressure(&Adapter->AdapterLuid, &Now);
            *Kept = Now;
        }
        if ((Flags & D3DISSUE_END) != 0)
        {
            D9ReadMemoryPressure(&Adapter->AdapterLuid, &Now);
            Now.BytesEvictedFromProcess -= Kept->BytesEvictedFromProcess;
            *Kept = Now;
            Query->State = D9QueryState::Signaled;
        }
        return S_OK;
    }

    Args.hQuery = Query->DriverQuery;
    if ((Flags & D3DISSUE_BEGIN) != 0)
        Args.Flags.Value |= D9DriverIssueBegin;
    if ((Flags & D3DISSUE_END) != 0)
        Args.Flags.Value |= D9DriverIssueEnd;
    DdiIssueQuery(&Args);
    return S_OK;
}

/* A query not signaled yet asks the driver; a flush that finds the device lost reports why instead. Until the
   result arrives the answer is S_FALSE, and a flush pushes the driver's work along */
HRESULT
D9Device::ReadQuery(
    _Inout_ D9Query* Query,
    _Out_writes_bytes_opt_(Size) VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    D9_DEVICE_LOCK();
    BYTE Answer[D9QueryDataLimit];
    D3DDDIARG_GETQUERYDATA Args = {};
    HRESULT hr = S_FALSE;

    if (NotReset)
        return D3DERR_INVALIDCALL;

    if (Query->State != D9QueryState::Signaled && (Flags & D3DGETDATA_FLUSH) != 0 && Lost)
    {
        hr = ExecutionStatus();
        if (SUCCEEDED(hr))
            hr = D3DERR_DEVICELOST;
    }
    if (Query->State != D9QueryState::Signaled && SUCCEEDED(hr))
    {
        Args.hQuery = Query->DriverQuery;
        Args.pData = Answer;
        if (Batch != nullptr)
            Batch->Settle(Args.hQuery);
        if (DeviceFunctions.pfnGetQueryData(DriverDevice, &Args) == S_OK)
        {
            CopyMemory(Query->Stored, Answer, Query->DataSize);
            Query->State = D9QueryState::Signaled;
        }
        else if ((Flags & D3DGETDATA_FLUSH) != 0)
            FlushDriver();
    }
    if (Query->State == D9QueryState::Signaled)
    {
        if (Data != nullptr)
            CopyMemory(Data, Query->Stored, Size < sizeof(Query->Stored) ? Size : sizeof(Query->Stored));
        hr = S_OK;
    }
    return hr;
}
