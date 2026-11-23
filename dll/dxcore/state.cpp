/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXCoreAdapter state queries and changes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

/* The process enumeration block NtDxgkEnumProcesses takes */
struct DC_ENUM_PROCESSES
{
    LUID AdapterLuid;
    UINT32* Buffer;
    UINT64 Count;
};

VOID
DcAdapter::ClassifyQueryState(
    _In_ ULONG State,
    _Out_ BOOLEAN* Known,
    _Out_ BOOLEAN* Present)
{
    BOOLEAN HasPerformanceData = KmdModelVersion >= 2400;

    *Known = TRUE;
    *Present = FALSE;

    switch (State)
    {
        case static_cast<ULONG>(DXCoreAdapterState::IsDriverUpdateInProgress):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryBudget):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterInUseProcessCount):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterInUseProcessSet):
            *Present = TRUE;
            break;

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryUsageBytes):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryUsageByProcessBytes):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds):
        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineRunningTimeByProcessMicroseconds):
            *Known = (FactoryCapabilities & DcFactoryEngineStatistics) != 0;
            *Present = KmdModelVersion >= 2000;
            break;

        case static_cast<ULONG>(DXCoreAdapterState::AdapterTemperatureCelsius):
            *Present = HasPerformanceData && Traits.HasTemperature;
            break;

        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineFrequencyHertz):
            *Present = HasPerformanceData && Traits.HasEngineFrequency;
            break;

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryFrequencyHertz):
            *Present = HasPerformanceData && Traits.HasMemoryFrequency;
            break;

        case static_cast<ULONG>(DXCoreAdapterState::SingleAdapterHybridMode):
            *Known = DcIsSystemFeatureEnabled(DcFeatureSingleAdapterHybridMode);
            *Present = *Known && Traits.HasHybridMode;
            break;

        default:
            *Known = FALSE;
            break;
    }
}

bool
STDMETHODCALLTYPE
DcAdapter::IsQueryStateSupported(
    _In_ DXCoreAdapterState State)
{
    BOOLEAN Known;
    BOOLEAN Present;

    ClassifyQueryState(static_cast<ULONG>(State), &Known, &Present);
    return Known && Present;
}

VOID
DcAdapter::ClassifySetState(
    _In_ ULONG State,
    _Out_ BOOLEAN* Known,
    _Out_ BOOLEAN* Present)
{
    *Known = FALSE;
    *Present = FALSE;

    if (State == static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryBudget))
    {
        *Known = TRUE;
        *Present = TRUE;
    }
    else if (State == static_cast<ULONG>(DXCoreAdapterState::SingleAdapterHybridMode) &&
             DcIsSystemFeatureEnabled(DcFeatureSingleAdapterHybridMode))
    {
        *Known = TRUE;
        *Present = Traits.HasHybridMode;
    }
}

bool
STDMETHODCALLTYPE
DcAdapter::IsSetStateSupported(
    _In_ DXCoreAdapterState State)
{
    BOOLEAN Known;
    BOOLEAN Present;

    ClassifySetState(static_cast<ULONG>(State), &Known, &Present);
    return Known && Present;
}

/* Sums the committed and resident bytes of every segment in a list */
static
NTSTATUS
DcSumSegments(
    _In_ const LUID& AdapterLuid,
    _In_ UINT PhysicalIndex,
    _In_ const std::vector<UINT16>& Segments,
    _Out_ UINT64* Committed,
    _Out_ UINT64* Resident)
{
    D3DKMT_QUERYSTATISTICS Stats;
    UINT64 CommittedSum = 0;
    UINT64 ResidentSum = 0;
    NTSTATUS Status;

    *Committed = 0;
    *Resident = 0;

    memset(&Stats, 0, sizeof(Stats));
    Stats.Type = D3DKMT_QUERYSTATISTICS_SEGMENT2;
    Stats.AdapterLuid = AdapterLuid;
    Stats.QuerySegment2.PhysicalAdapterIndex = static_cast<UINT16>(PhysicalIndex);

    for (size_t Index = 0; Index < Segments.size(); Index++)
    {
        Stats.QuerySegment2.SegmentId = Segments[Index];
        Status = DcKmtCall(DcKmt()->QueryStatistics, &Stats);
        if (!NT_SUCCESS(Status))
            return Status;

        CommittedSum += Stats.QueryResult.SegmentInformation.BytesCommitted;
        ResidentSum += Stats.QueryResult.SegmentInformation.BytesResident;
    }

    *Committed = CommittedSum;
    *Resident = ResidentSum;
    return STATUS_SUCCESS;
}

/* The segments of one memory type on one physical adapter */
static
const std::vector<UINT16>*
DcSegmentsOfType(
    _In_ const DcPhysicalAdapter& Physical,
    _In_ ULONG MemoryType)
{
    if (MemoryType == static_cast<ULONG>(DXCoreMemoryType::Dedicated))
        return &Physical.DedicatedSegments;
    if (MemoryType == static_cast<ULONG>(DXCoreMemoryType::Shared))
        return &Physical.SharedSegments;
    if (MemoryType == static_cast<ULONG>(DXCoreMemoryType::ReservedPartition) &&
        DcIsSystemFeatureEnabled(DcFeatureCarveoutApi))
    {
        return &Physical.PartitionSegments;
    }

    return nullptr;
}

/* Another process's usage of one memory type, which the caller does not fail on */
static
VOID
DcReadProcessMemory(
    _In_ const DcAdapter* Adapter,
    _In_ const DXCoreProcessMemoryQueryInput& Request,
    _Out_ DXCoreProcessMemoryQueryOutput* Output)
{
    const std::vector<UINT16>* Segments = DcSegmentsOfType(Adapter->Traits.Physical[Request.physicalAdapterIndex],
                                                           static_cast<ULONG>(Request.memoryType));
    D3DKMT_QUERYSTATISTICS Stats;
    HANDLE Process;
    UINT64 Committed = 0;

    memset(&Stats, 0, sizeof(Stats));
    Stats.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT2;
    Stats.AdapterLuid = Adapter->Luid;
    Stats.QueryProcessSegment2.PhysicalAdapterIndex = static_cast<UINT16>(Request.physicalAdapterIndex);

    Process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, Request.processId);
    if (Process == NULL)
        return;

    Stats.hProcess = Process;
    if (Segments != nullptr)
    {
        for (size_t Index = 0; Index < Segments->size(); Index++)
        {
            Stats.QueryProcessSegment2.SegmentId = (*Segments)[Index];
            if (!NT_SUCCESS(DcKmtCall(DcKmt()->QueryStatistics, &Stats)))
            {
                Output->memoryUsage.resident = 0;
                CloseHandle(Process);
                return;
            }

            Committed += Stats.QueryResult.ProcessSegmentInformation.BytesCommitted;
        }
    }

    Output->processQuerySucceeded = true;
    Output->memoryUsage.committed = Committed;
    Output->memoryUsage.resident = 0;
    CloseHandle(Process);
}

HRESULT
DcAdapter::ReadState(
    _In_ ULONG State,
    _In_ size_t InputSize,
    _In_reads_bytes_opt_(InputSize) const void* Input,
    _In_ size_t OutputSize,
    _Out_writes_bytes_(OutputSize) void* Output)
{
    NTSTATUS Status;

    switch (State)
    {
        case static_cast<ULONG>(DXCoreAdapterState::IsDriverUpdateInProgress):
        {
            UINT Updating = 0;

            if (!IsValid())
                return DXGI_ERROR_DEVICE_REMOVED;
            if (OutputSize == 0)
                return E_INVALIDARG;

            Status = DcQueryAdapterInfo(Handle, KMTQAITYPE_CHECKDRIVERUPDATESTATUS, &Updating, sizeof(Updating));
            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            *static_cast<BYTE*>(Output) = Updating != 0;
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryBudget):
        {
            const DXCoreAdapterMemoryBudgetNodeSegmentGroup* Request =
                static_cast<const DXCoreAdapterMemoryBudgetNodeSegmentGroup*>(Input);
            DXCoreAdapterMemoryBudget* Budget = static_cast<DXCoreAdapterMemoryBudget*>(Output);
            D3DKMT_QUERYVIDEOMEMORYINFO Info = {};

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Budget))
                return E_INVALIDARG;
            if (static_cast<ULONG>(Request->segmentGroup) > 1)
                return E_INVALIDARG;

            Info.hAdapter = Handle;
            Info.MemorySegmentGroup = static_cast<D3DKMT_MEMORY_SEGMENT_GROUP>(Request->segmentGroup);
            Info.PhysicalAdapterIndex = Request->nodeIndex;
            Status = DcKmtCall(DcKmt()->QueryVideoMemoryInfo, &Info);
            if (NT_SUCCESS(Status))
            {
                Budget->budget = Info.Budget;
                Budget->currentUsage = Info.CurrentUsage;
                Budget->availableForReservation = Info.AvailableForReservation;
                Budget->currentReservation = Info.CurrentReservation;
            }
            return DcStatusHresult(Status);
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryUsageBytes):
        {
            const DXCoreMemoryQueryInput* Request = static_cast<const DXCoreMemoryQueryInput*>(Input);
            DXCoreMemoryUsage* Usage = static_cast<DXCoreMemoryUsage*>(Output);
            const std::vector<UINT16>* Segments;
            UINT64 Committed;
            UINT64 Resident;

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Usage))
                return E_INVALIDARG;

            Segments = DcSegmentsOfType(Traits.Physical[Request->physicalAdapterIndex],
                                        static_cast<ULONG>(Request->memoryType));
            if (Segments == nullptr)
                Status = DcSumSegments(Luid, Request->physicalAdapterIndex, std::vector<UINT16>(), &Committed, &Resident);
            else
                Status = DcSumSegments(Luid, Request->physicalAdapterIndex, *Segments, &Committed, &Resident);

            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            Usage->committed = Committed;
            Usage->resident = Resident;
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryUsageByProcessBytes):
        {
            const DXCoreProcessMemoryQueryInput* Request = static_cast<const DXCoreProcessMemoryQueryInput*>(Input);
            DXCoreProcessMemoryQueryOutput* Usage = static_cast<DXCoreProcessMemoryQueryOutput*>(Output);

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Usage))
                return E_INVALIDARG;
            if (Request->processId == 0)
                return E_INVALIDARG;

            Usage->processQuerySucceeded = false;
            DcReadProcessMemory(this, *Request, Usage);
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds):
        {
            const DXCoreAdapterEngineIndex* Request = static_cast<const DXCoreAdapterEngineIndex*>(Input);
            D3DKMT_QUERYSTATISTICS Stats;

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(DXCoreEngineQueryInput) || OutputSize < sizeof(UINT64))
                return E_INVALIDARG;

            *static_cast<UINT64*>(Output) = 0;
            memset(&Stats, 0, sizeof(Stats));
            Stats.Type = D3DKMT_QUERYSTATISTICS_NODE2;
            Stats.AdapterLuid = Luid;
            Stats.QueryNode2.PhysicalAdapterIndex = static_cast<UINT16>(Request->physicalAdapterIndex);
            Stats.QueryNode2.NodeOrdinal = static_cast<UINT16>(Request->engineIndex);
            Status = DcKmtCall(DcKmt()->QueryStatistics, &Stats);
            if (NT_SUCCESS(Status))
            {
                *static_cast<UINT64*>(Output) =
                    Stats.QueryResult.NodeInformation.GlobalInformation.RunningTime.QuadPart / 10;
            }
            return DcStatusHresult(Status);
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineRunningTimeByProcessMicroseconds):
        {
            const DXCoreEngineQueryInput* Request = static_cast<const DXCoreEngineQueryInput*>(Input);
            DXCoreEngineQueryOutput* Result = static_cast<DXCoreEngineQueryOutput*>(Output);
            D3DKMT_QUERYSTATISTICS Stats;
            HANDLE Process;

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Result))
                return E_INVALIDARG;
            if (Request->processId == 0)
                return E_INVALIDARG;

            Result->processQuerySucceeded = false;
            Result->runningTime = 0;
            memset(&Stats, 0, sizeof(Stats));
            Stats.Type = D3DKMT_QUERYSTATISTICS_PROCESS_NODE2;
            Stats.AdapterLuid = Luid;
            Stats.QueryProcessNode2.PhysicalAdapterIndex =
                static_cast<UINT16>(Request->adapterEngineIndex.physicalAdapterIndex);
            Stats.QueryProcessNode2.NodeOrdinal = static_cast<UINT16>(Request->adapterEngineIndex.engineIndex);

            Process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, Request->processId);
            if (Process != NULL)
            {
                Stats.hProcess = Process;
                if (NT_SUCCESS(DcKmtCall(DcKmt()->QueryStatistics, &Stats)))
                {
                    Result->runningTime = Stats.QueryResult.ProcessNodeInformation.RunningTime.QuadPart / 10;
                    Result->processQuerySucceeded = true;
                }
                CloseHandle(Process);
            }
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterTemperatureCelsius):
        {
            const UINT* Request = static_cast<const UINT*>(Input);
            D3DKMT_QUERYSTATISTICS Stats;

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(UINT) || OutputSize < sizeof(float))
                return E_INVALIDARG;

            memset(&Stats, 0, sizeof(Stats));
            Stats.Type = D3DKMT_QUERYSTATISTICS_PHYSICAL_ADAPTER;
            Stats.AdapterLuid = Luid;
            Stats.QueryPhysAdapter.PhysicalAdapterIndex = *reinterpret_cast<const UINT16*>(Request);
            Status = DcKmtCall(DcKmt()->QueryStatistics, &Stats);
            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            *static_cast<float*>(Output) =
                static_cast<float>(Stats.QueryResult.PhysAdapterInformation.AdapterPerfData.Temperature) / 10.0f;
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterInUseProcessCount):
        {
            DC_ENUM_PROCESSES Enum = {};

            if (InputSize != 0)
                return E_INVALIDARG;
            if (OutputSize < sizeof(UINT32))
                return E_INVALIDARG;

            Enum.AdapterLuid = Luid;
            Status = DcKmtCall(DcKmt()->EnumProcesses, &Enum);
            if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
                return E_FAIL;

            *static_cast<UINT32*>(Output) = static_cast<UINT32>(Enum.Count);
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterInUseProcessSet):
        {
            const DXCoreAdapterProcessSetQueryInput* Request = static_cast<const DXCoreAdapterProcessSetQueryInput*>(Input);
            DXCoreAdapterProcessSetQueryOutput* Result = static_cast<DXCoreAdapterProcessSetQueryOutput*>(Output);
            DC_ENUM_PROCESSES Enum = {};

            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Result))
                return E_INVALIDARG;

            Enum.AdapterLuid = Luid;
            Enum.Buffer = Request->processIds;
            Enum.Count = Request->arraySize;
            Status = DcKmtCall(DcKmt()->EnumProcesses, &Enum);
            Result->processesTotal = static_cast<uint32_t>(Enum.Count);
            if (Status == STATUS_BUFFER_TOO_SMALL)
            {
                /* A short array is reported with a positive Win32 code, not a failure */
                Result->processesWritten = 0;
                return ERROR_INSUFFICIENT_BUFFER;
            }
            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            Result->processesWritten = static_cast<uint32_t>(Enum.Count);
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterEngineFrequencyHertz):
        {
            const DXCoreAdapterEngineIndex* Request = static_cast<const DXCoreAdapterEngineIndex*>(Input);
            DXCoreFrequencyQueryOutput* Result = static_cast<DXCoreFrequencyQueryOutput*>(Output);
            D3DKMT_NODE_PERFDATA Perf = {};

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(*Request) || OutputSize < sizeof(*Result))
                return E_INVALIDARG;
            if (Request->physicalAdapterIndex >= Traits.Physical.size())
                return E_INVALIDARG;
            if (Request->engineIndex >= Traits.Physical[Request->physicalAdapterIndex].EngineCount)
                return E_INVALIDARG;

            Perf.NodeOrdinal = Request->engineIndex;
            Perf.PhysicalAdapterIndex = Request->physicalAdapterIndex;
            Status = DcQueryAdapterInfo(Handle, KMTQAITYPE_NODEPERFDATA, &Perf, sizeof(Perf));
            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            Result->frequency = Perf.Frequency;
            Result->maxFrequency = Perf.MaxFrequency;
            Result->maxOverclockedFrequency = Perf.MaxFrequencyOC;
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::AdapterMemoryFrequencyHertz):
        {
            const UINT* Request = static_cast<const UINT*>(Input);
            DXCoreFrequencyQueryOutput* Result = static_cast<DXCoreFrequencyQueryOutput*>(Output);
            D3DKMT_ADAPTER_PERFDATA Perf = {};

            if (Request == nullptr)
                return E_POINTER;
            if (InputSize < sizeof(UINT) || OutputSize < sizeof(*Result))
                return E_INVALIDARG;
            if (*Request >= Traits.Physical.size())
                return E_INVALIDARG;

            Perf.PhysicalAdapterIndex = *Request;
            Status = DcQueryAdapterInfo(Handle, KMTQAITYPE_ADAPTERPERFDATA, &Perf, sizeof(Perf));
            if (!NT_SUCCESS(Status))
                return DcStatusHresult(Status);

            Result->frequency = Perf.MemoryFrequency;
            Result->maxFrequency = Perf.MaxMemoryFrequency;
            Result->maxOverclockedFrequency = Perf.MaxMemoryFrequencyOC;
            return S_OK;
        }

        case static_cast<ULONG>(DXCoreAdapterState::SingleAdapterHybridMode):
            if (!DcIsSystemFeatureEnabled(DcFeatureSingleAdapterHybridMode))
                return E_INVALIDARG;
            if (OutputSize < sizeof(UINT))
                return E_INVALIDARG;

            *static_cast<UINT*>(Output) = HybridMode;
            return S_OK;

        default:
            return E_INVALIDARG;
    }
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::QueryState(
    _In_ DXCoreAdapterState State,
    _In_ size_t InputSize,
    _In_reads_bytes_opt_(InputSize) const void* Input,
    _In_ size_t OutputSize,
    _Out_writes_bytes_(OutputSize) void* Output)
{
    BOOLEAN Known;
    BOOLEAN Present;

    if (Output == nullptr)
        return E_POINTER;

    memset(Output, 0, OutputSize);
    if (!IsValid())
        return DXGI_ERROR_DEVICE_REMOVED;

    ClassifyQueryState(static_cast<ULONG>(State), &Known, &Present);
    if (!Known)
        return DXGI_ERROR_INVALID_CALL;
    if (!Present)
        return DXGI_ERROR_UNSUPPORTED;

    return ReadState(static_cast<ULONG>(State), InputSize, Input, OutputSize, Output);
}

HRESULT
STDMETHODCALLTYPE
DcAdapter::SetState(
    _In_ DXCoreAdapterState State,
    _In_ size_t InputSize,
    _In_reads_bytes_opt_(InputSize) const void* Input,
    _In_ size_t DataSize,
    _In_reads_bytes_opt_(DataSize) const void* Data)
{
    HRESULT ReservationResult = S_OK;
    BOOLEAN Known;
    BOOLEAN Present;

    if (!IsValid())
        return DXGI_ERROR_DEVICE_REMOVED;

    ClassifySetState(static_cast<ULONG>(State), &Known, &Present);
    if (!Known)
        return DXGI_ERROR_INVALID_CALL;
    if (!Present)
        return DXGI_ERROR_UNSUPPORTED;

    if (State == DXCoreAdapterState::AdapterMemoryBudget)
    {
        const DXCoreAdapterMemoryBudgetNodeSegmentGroup* Request =
            static_cast<const DXCoreAdapterMemoryBudgetNodeSegmentGroup*>(Input);
        D3DKMT_CHANGEVIDEOMEMORYRESERVATION Change = {};
        NTSTATUS Status;

        if (Request == nullptr)
            return E_POINTER;
        if (InputSize < sizeof(*Request))
            return E_INVALIDARG;
        if (Data == nullptr)
            return E_POINTER;
        if (DataSize < sizeof(UINT64))
            return E_INVALIDARG;
        if (static_cast<ULONG>(Request->segmentGroup) > 1)
            return E_INVALIDARG;

        Change.hAdapter = Handle;
        Change.MemorySegmentGroup = static_cast<D3DKMT_MEMORY_SEGMENT_GROUP>(Request->segmentGroup);
        Change.Reservation = *static_cast<const UINT64*>(Data);
        Change.PhysicalAdapterIndex = Request->nodeIndex;
        Status = DcKmtCall(DcKmt()->ChangeVideoMemoryReservation, &Change);
        if (!NT_SUCCESS(Status))
            DcTrace(L"video memory reservation change failed (0x%08lX)", Status);
        ReservationResult = DcStatusHresult(Status);

        /* The system build falls through into the hybrid mode case here */
    }
    else if (State != DXCoreAdapterState::SingleAdapterHybridMode)
    {
        return E_INVALIDARG;
    }

    if (!DcIsSystemFeatureEnabled(DcFeatureSingleAdapterHybridMode))
        return E_NOTIMPL;
    if (Data == nullptr)
        return E_POINTER;
    if (DataSize < sizeof(UINT))
        return E_INVALIDARG;

    HybridMode = *static_cast<const UINT*>(Data);
    return ReservationResult;
}
