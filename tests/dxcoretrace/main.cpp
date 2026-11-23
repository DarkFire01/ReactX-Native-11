/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXCore traffic to the kernel, printed for comparison
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dxcoretrace [-runtime system|reactx|<path>] [scenario ...]
 *
 * The runtime's win32u imports and its GetProcAddress import are pointed at wrappers inside this
 * process only, so every kernel call DXCore makes on the main thread is printed with its arguments.
 * Kernel handles print as k ids and LUIDs as l ids, in the order they first appear. Values that change
 * from run to run, like usage counters and clock rates, are left out.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <initguid.h>
#include <dxcore_interface.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

typedef NTSTATUS (APIENTRY* PFN_TR_KMT)(void*);
typedef HRESULT (WINAPI* PFN_TR_CREATE)(REFIID, void**);

/* The kernel calls the trace knows, under their win32u and gdi32 names */
enum TR_CALL
{
    TrCallQueryAdapterInfo,
    TrCallCloseAdapter,
    TrCallQueryStatistics,
    TrCallQueryVideoMemoryInfo,
    TrCallChangeVideoMemoryReservation,
    TrCallEscape,
    TrCallEnumAdapters2,
    TrCallEnumAdapters3,
    TrCallIsFeatureEnabled,
    TrCallEnumProcesses,
    TrCallGetProperties,
    TrCallCount,
};

struct TrCallName
{
    PCSTR System;
    PCSTR Gdi;
    PCSTR Printed;
};

static const TrCallName TrCallNames[TrCallCount] =
{
    { "NtGdiDdDDIQueryAdapterInfo", "D3DKMTQueryAdapterInfo", "QueryAdapterInfo" },
    { "NtGdiDdDDICloseAdapter", "D3DKMTCloseAdapter", "CloseAdapter" },
    { "NtGdiDdDDIQueryStatistics", "D3DKMTQueryStatistics", "QueryStatistics" },
    { "NtGdiDdDDIQueryVideoMemoryInfo", "D3DKMTQueryVideoMemoryInfo", "QueryVideoMemoryInfo" },
    { "NtGdiDdDDIChangeVideoMemoryReservation", "D3DKMTChangeVideoMemoryReservation", "ChangeVideoMemoryReservation" },
    { "NtGdiDdDDIEscape", "D3DKMTEscape", "Escape" },
    { "NtGdiDdDDIEnumAdapters2", "D3DKMTEnumAdapters2", "EnumAdapters2" },
    { "NtDxgkEnumAdapters3", "D3DKMTEnumAdapters3", "EnumAdapters3" },
    { "NtDxgkIsFeatureEnabled", "D3DKMTIsFeatureEnabled", "IsFeatureEnabled" },
    { "NtDxgkEnumProcesses", "D3DKMTEnumProcesses", "EnumProcesses" },
    { "NtDxgkGetProperties", "D3DKMTGetProperties", "GetProperties" },
};

struct TrState
{
    DWORD MainThread;
    PFN_TR_KMT Originals[TrCallCount];
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    std::map<UINT, UINT> Handles;
    std::map<ULONG64, UINT> Luids;
    UINT Quiet;
};

static TrState Tr;

static
VOID
TrAppend(
    _Inout_ std::string* Text,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[512];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Buffer, sizeof(Buffer), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    Text->append(Buffer);
}

static
std::string
TrHandle(
    _In_ UINT Handle)
{
    std::string Text;

    if (Handle == 0)
        return "0";

    auto Found = Tr.Handles.find(Handle);
    if (Found == Tr.Handles.end())
        Found = Tr.Handles.emplace(Handle, static_cast<UINT>(Tr.Handles.size() + 1)).first;

    TrAppend(&Text, "k%u", Found->second);
    return Text;
}

static
std::string
TrLuid(
    _In_ const LUID& Luid)
{
    ULONG64 Key = (static_cast<ULONG64>(static_cast<ULONG>(Luid.HighPart)) << 32) | Luid.LowPart;
    std::string Text;

    if (Key == 0)
        return "0";

    auto Found = Tr.Luids.find(Key);
    if (Found == Tr.Luids.end())
        Found = Tr.Luids.emplace(Key, static_cast<UINT>(Tr.Luids.size() + 1)).first;

    TrAppend(&Text, "l%u", Found->second);
    return Text;
}

/* FNV-1a over a block, so outputs compare as one number */
static
UINT
TrHash(
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 2166136261u;

    if (Data == nullptr)
        return 0;
    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619u;
    }
    return Hash;
}

/* Adapter info kinds whose answers move while the machine runs */
static
BOOLEAN
TrIsLiveInfo(
    _In_ UINT Type)
{
    return Type == KMTQAITYPE_NODEPERFDATA || Type == KMTQAITYPE_ADAPTERPERFDATA ||
           Type == KMTQAITYPE_CHECKDRIVERUPDATESTATUS;
}

static
std::string
TrDescribeBefore(
    _In_ UINT Call,
    _In_ const VOID* Arguments)
{
    std::string Text;

    switch (Call)
    {
        case TrCallQueryAdapterInfo:
        {
            const D3DKMT_QUERYADAPTERINFO* Query = static_cast<const D3DKMT_QUERYADAPTERINFO*>(Arguments);

            TrAppend(&Text, " %s type %u size %u", TrHandle(Query->hAdapter).c_str(), Query->Type,
                     Query->PrivateDriverDataSize);
            if (Query->pPrivateDriverData != nullptr && Query->PrivateDriverDataSize <= 4096)
                TrAppend(&Text, " in %08X", TrHash(Query->pPrivateDriverData, Query->PrivateDriverDataSize));
            break;
        }

        case TrCallCloseAdapter:
            TrAppend(&Text, " %s", TrHandle(static_cast<const D3DKMT_CLOSEADAPTER*>(Arguments)->hAdapter).c_str());
            break;

        case TrCallQueryStatistics:
        {
            const D3DKMT_QUERYSTATISTICS* Stats = static_cast<const D3DKMT_QUERYSTATISTICS*>(Arguments);
            const BYTE* Tail = reinterpret_cast<const BYTE*>(Stats) + offsetof(D3DKMT_QUERYSTATISTICS, QuerySegment);

            TrAppend(&Text, " type %u %s process %d query %08X", Stats->Type, TrLuid(Stats->AdapterLuid).c_str(),
                     Stats->hProcess != nullptr, TrHash(Tail, sizeof(*Stats) - (Tail - reinterpret_cast<const BYTE*>(Stats))));
            break;
        }

        case TrCallQueryVideoMemoryInfo:
        {
            const D3DKMT_QUERYVIDEOMEMORYINFO* Info = static_cast<const D3DKMT_QUERYVIDEOMEMORYINFO*>(Arguments);

            TrAppend(&Text, " %s group %u physical %u process %d", TrHandle(Info->hAdapter).c_str(),
                     Info->MemorySegmentGroup, Info->PhysicalAdapterIndex, Info->hProcess != nullptr);
            break;
        }

        case TrCallChangeVideoMemoryReservation:
        {
            const D3DKMT_CHANGEVIDEOMEMORYRESERVATION* Change =
                static_cast<const D3DKMT_CHANGEVIDEOMEMORYRESERVATION*>(Arguments);

            TrAppend(&Text, " %s group %u physical %u reservation %llu", TrHandle(Change->hAdapter).c_str(),
                     Change->MemorySegmentGroup, Change->PhysicalAdapterIndex, Change->Reservation);
            break;
        }

        case TrCallEscape:
        {
            const D3DKMT_ESCAPE* Escape = static_cast<const D3DKMT_ESCAPE*>(Arguments);

            TrAppend(&Text, " %s device %u type %u flags %08X size %u in %08X", TrHandle(Escape->hAdapter).c_str(),
                     Escape->hDevice, Escape->Type, Escape->Flags.Value, Escape->PrivateDriverDataSize,
                     TrHash(Escape->pPrivateDriverData, Escape->PrivateDriverDataSize));
            break;
        }

        case TrCallEnumAdapters2:
        {
            const D3DKMT_ENUMADAPTERS2* Enum = static_cast<const D3DKMT_ENUMADAPTERS2*>(Arguments);

            TrAppend(&Text, " count %u buffer %d", Enum->NumAdapters, Enum->pAdapters != nullptr);
            break;
        }

        case TrCallEnumAdapters3:
        {
            const D3DKMT_ENUMADAPTERS3* Enum = static_cast<const D3DKMT_ENUMADAPTERS3*>(Arguments);

            TrAppend(&Text, " filter %llX count %u buffer %d", *reinterpret_cast<const ULONG64*>(&Enum->Filter),
                     Enum->NumAdapters, Enum->pAdapters != nullptr);
            break;
        }

        case TrCallIsFeatureEnabled:
        {
            const D3DKMT_ISFEATUREENABLED* Feature = static_cast<const D3DKMT_ISFEATUREENABLED*>(Arguments);

            TrAppend(&Text, " %s feature %08X", TrHandle(Feature->hAdapter).c_str(), Feature->FeatureId);
            break;
        }

        case TrCallEnumProcesses:
        {
            const BYTE* Block = static_cast<const BYTE*>(Arguments);

            TrAppend(&Text, " %s buffer %d count %llu", TrLuid(*reinterpret_cast<const LUID*>(Block)).c_str(),
                     *reinterpret_cast<void* const*>(Block + 8) != nullptr, *reinterpret_cast<const UINT64*>(Block + 16));
            break;
        }

        case TrCallGetProperties:
        {
            const UINT* Block = static_cast<const UINT*>(Arguments);

            TrAppend(&Text, " property %u size %u", Block[0], Block[1]);
            break;
        }

        default:
            break;
    }

    return Text;
}

static
std::string
TrDescribeAfter(
    _In_ UINT Call,
    _In_ const VOID* Arguments,
    _In_ NTSTATUS Status)
{
    std::string Text;

    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_TOO_SMALL)
        return Text;

    switch (Call)
    {
        case TrCallQueryAdapterInfo:
        {
            const D3DKMT_QUERYADAPTERINFO* Query = static_cast<const D3DKMT_QUERYADAPTERINFO*>(Arguments);

            if (!TrIsLiveInfo(Query->Type) && Query->pPrivateDriverData != nullptr && Query->PrivateDriverDataSize <= 0x2000)
                TrAppend(&Text, " out %08X", TrHash(Query->pPrivateDriverData, Query->PrivateDriverDataSize));
            break;
        }

        case TrCallEnumAdapters2:
        case TrCallEnumAdapters3:
        {
            ULONG Count;
            const D3DKMT_ADAPTERINFO* Infos;

            if (Call == TrCallEnumAdapters2)
            {
                Count = static_cast<const D3DKMT_ENUMADAPTERS2*>(Arguments)->NumAdapters;
                Infos = static_cast<const D3DKMT_ENUMADAPTERS2*>(Arguments)->pAdapters;
            }
            else
            {
                Count = static_cast<const D3DKMT_ENUMADAPTERS3*>(Arguments)->NumAdapters;
                Infos = static_cast<const D3DKMT_ENUMADAPTERS3*>(Arguments)->pAdapters;
            }

            TrAppend(&Text, " count %u", Count);
            for (ULONG Index = 0; Infos != nullptr && NT_SUCCESS(Status) && Index < Count; Index++)
            {
                TrAppend(&Text, " [%s %s sources %u]", TrHandle(Infos[Index].hAdapter).c_str(),
                         TrLuid(Infos[Index].AdapterLuid).c_str(), Infos[Index].NumOfSources);
            }
            break;
        }

        case TrCallIsFeatureEnabled:
            TrAppend(&Text, " result %08X", static_cast<const D3DKMT_ISFEATUREENABLED*>(Arguments)->Result.Value);
            break;

        default:
            break;
    }

    return Text;
}

static
NTSTATUS
TrForward(
    _In_ UINT Call,
    _Inout_ VOID* Arguments)
{
    BOOLEAN Log = GetCurrentThreadId() == Tr.MainThread && Tr.Quiet == 0;
    std::string Before;
    NTSTATUS Status;

    if (Log)
        Before = TrDescribeBefore(Call, Arguments);

    Status = Tr.Originals[Call](Arguments);

    if (Log)
    {
        printf("%s%s = %08lX%s\n", TrCallNames[Call].Printed, Before.c_str(), Status,
               TrDescribeAfter(Call, Arguments, Status).c_str());
    }
    return Status;
}

template <UINT Call>
static
NTSTATUS
APIENTRY
TrThunk(
    _Inout_ VOID* Arguments)
{
    return TrForward(Call, Arguments);
}

static const PFN_TR_KMT TrThunks[TrCallCount] =
{
    TrThunk<0>, TrThunk<1>, TrThunk<2>, TrThunk<3>, TrThunk<4>, TrThunk<5>,
    TrThunk<6>, TrThunk<7>, TrThunk<8>, TrThunk<9>, TrThunk<10>,
};

static
INT
TrCallSlot(
    _In_z_ PCSTR Name)
{
    for (UINT Slot = 0; Slot < TrCallCount; Slot++)
    {
        if (strcmp(TrCallNames[Slot].System, Name) == 0 || strcmp(TrCallNames[Slot].Gdi, Name) == 0)
            return static_cast<INT>(Slot);
    }
    return -1;
}

static
FARPROC
WINAPI
TrGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Found = Tr.GetProcAddress(Module, Name);
    INT Slot;

    if (Found == nullptr || IS_INTRESOURCE(Name))
        return Found;

    Slot = TrCallSlot(Name);
    if (Slot < 0)
        return Found;

    Tr.Originals[Slot] = reinterpret_cast<PFN_TR_KMT>(Found);
    return reinterpret_cast<FARPROC>(TrThunks[Slot]);
}

static
VOID
TrWriteImport(
    _Inout_ ULONG_PTR* Entry,
    _In_ ULONG_PTR Value)
{
    DWORD Old;

    if (!VirtualProtect(Entry, sizeof(*Entry), PAGE_READWRITE, &Old))
        return;
    *Entry = Value;
    VirtualProtect(Entry, sizeof(*Entry), Old, &Old);
}

/* Points the runtime's GetProcAddress and kernel imports at the wrappers */
static
UINT
TrPatchImports(
    _In_ HMODULE Module)
{
    BYTE* Base = reinterpret_cast<BYTE*>(Module);
    const IMAGE_DOS_HEADER* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
    const IMAGE_NT_HEADERS* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY* Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    const IMAGE_IMPORT_DESCRIPTOR* Import;
    UINT Patched = 0;

    if (Directory->VirtualAddress == 0)
        return 0;

    for (Import = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(Base + Directory->VirtualAddress);
         Import->Name != 0; Import++)
    {
        const IMAGE_THUNK_DATA* Names;
        IMAGE_THUNK_DATA* Addresses;

        if (Import->OriginalFirstThunk == 0)
            continue;
        Names = reinterpret_cast<const IMAGE_THUNK_DATA*>(Base + Import->OriginalFirstThunk);
        Addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->FirstThunk);
        for (; Names->u1.AddressOfData != 0; Names++, Addresses++)
        {
            const IMAGE_IMPORT_BY_NAME* ByName;
            INT Slot;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData);
            if (strcmp(ByName->Name, "GetProcAddress") == 0)
            {
                Tr.GetProcAddress = reinterpret_cast<decltype(Tr.GetProcAddress)>(Addresses->u1.Function);
                TrWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(TrGetProcAddress));
                Patched += 1;
                continue;
            }

            Slot = TrCallSlot(ByName->Name);
            if (Slot < 0)
                continue;
            Tr.Originals[Slot] = reinterpret_cast<PFN_TR_KMT>(Addresses->u1.Function);
            TrWriteImport(&Addresses->u1.Function, reinterpret_cast<ULONG_PTR>(TrThunks[Slot]));
            Patched += 1;
        }
    }
    return Patched;
}

struct TrContext
{
    PFN_TR_CREATE Create;
};

typedef VOID (*PTR_SCENARIO)(_In_ TrContext* Context);

static
IDXCoreAdapterFactory*
TrFactory(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = nullptr;

    printf("create = %08lX\n", Context->Create(IID_PPV_ARGS(&Factory)));
    return Factory;
}

/* Every adapter the factory hands out for any attribute, without duplicates */
static
std::vector<IDXCoreAdapter*>
TrAdapters(
    _In_ IDXCoreAdapterFactory* Factory)
{
    static const GUID* const Attributes[] =
    {
        &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE,
        &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML, &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU,
        &DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR, &DXCORE_HARDWARE_TYPE_ATTRIBUTE_MEDIA_ACCELERATOR,
    };
    std::vector<IDXCoreAdapter*> Found;
    std::vector<ULONG64> Seen;

    Tr.Quiet += 1;
    for (const GUID* Attribute : Attributes)
    {
        IDXCoreAdapterList* List = nullptr;

        if (FAILED(Factory->CreateAdapterList(1, Attribute, IID_PPV_ARGS(&List))))
            continue;

        for (UINT32 Index = 0; Index < List->GetAdapterCount(); Index++)
        {
            IDXCoreAdapter* Adapter = nullptr;
            LUID Luid = {};
            ULONG64 Key;

            if (FAILED(List->GetAdapter(Index, IID_PPV_ARGS(&Adapter))))
                continue;

            Adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(Luid), &Luid);
            Key = (static_cast<ULONG64>(static_cast<ULONG>(Luid.HighPart)) << 32) | Luid.LowPart;
            if (std::find(Seen.begin(), Seen.end(), Key) != Seen.end())
            {
                Adapter->Release();
                continue;
            }

            Seen.push_back(Key);
            Found.push_back(Adapter);
        }
        List->Release();
    }
    Tr.Quiet -= 1;
    return Found;
}

static
VOID
TrScenarioFactory(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = TrFactory(Context);

    if (Factory != nullptr)
        Factory->Release();
}

static
VOID
TrScenarioLists(
    _In_ TrContext* Context)
{
    static const GUID* const Attributes[] =
    {
        &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS,
        &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML, &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU,
    };
    IDXCoreAdapterFactory* Factory = TrFactory(Context);
    IDXCoreAdapterFactory1* Factory1 = nullptr;

    if (Factory == nullptr)
        return;

    for (const GUID* Attribute : Attributes)
    {
        IDXCoreAdapterList* List = nullptr;
        HRESULT hr = Factory->CreateAdapterList(1, Attribute, IID_PPV_ARGS(&List));

        printf("list = %08lX count %u\n", hr, SUCCEEDED(hr) ? List->GetAdapterCount() : 0);
        if (List != nullptr)
            List->Release();
    }

    if (SUCCEEDED(Factory->QueryInterface(IID_PPV_ARGS(&Factory1))))
    {
        for (UINT Workload = 0; Workload <= 3; Workload++)
        {
            IDXCoreAdapterList* List = nullptr;
            HRESULT hr = Factory1->CreateAdapterListByWorkload(static_cast<DXCoreWorkload>(Workload),
                                                               DXCoreRuntimeFilterFlags::None,
                                                               DXCoreHardwareTypeFilterFlags::None, IID_PPV_ARGS(&List));

            printf("workload %u = %08lX count %u\n", Workload, hr, SUCCEEDED(hr) ? List->GetAdapterCount() : 0);
            if (List != nullptr)
                List->Release();
        }
        Factory1->Release();
    }
    Factory->Release();
}

static
VOID
TrScenarioProperties(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = TrFactory(Context);

    if (Factory == nullptr)
        return;

    for (IDXCoreAdapter* Adapter : TrAdapters(Factory))
    {
        for (UINT Property = 0; Property <= 17; Property++)
        {
            BYTE Buffer[256] = {};
            size_t Size = 0;
            HRESULT hr;

            hr = Adapter->GetPropertySize(static_cast<DXCoreAdapterProperty>(Property), &Size);
            if (SUCCEEDED(hr) && Size <= sizeof(Buffer))
                hr = Adapter->GetProperty(static_cast<DXCoreAdapterProperty>(Property), Size, Buffer);
            printf("property %u = %08lX\n", Property, hr);
        }
        Adapter->Release();
    }
    Factory->Release();
}

static
VOID
TrScenarioStates(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = TrFactory(Context);
    DWORD Self = GetCurrentProcessId();

    if (Factory == nullptr)
        return;

    for (IDXCoreAdapter* Adapter : TrAdapters(Factory))
    {
        BYTE Output[64];
        UINT32 Processes[64];

        {
            DXCoreAdapterMemoryBudgetNodeSegmentGroup Input = { 0, DXCoreSegmentGroup::Local };

            printf("budget = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(Input),
                                                           &Input, 32, Output));
            Input.segmentGroup = DXCoreSegmentGroup::NonLocal;
            printf("budget = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(Input),
                                                           &Input, 32, Output));
        }

        printf("updating = %08lX\n",
               Adapter->QueryState(DXCoreAdapterState::IsDriverUpdateInProgress, 0, nullptr, 1, Output));

        for (UINT Type = 0; Type <= 2; Type++)
        {
            DXCoreMemoryQueryInput Input = { 0, static_cast<DXCoreMemoryType>(Type) };
            DXCoreProcessMemoryQueryInput ProcessInput = { 0, static_cast<DXCoreMemoryType>(Type), Self };

            printf("usage = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, sizeof(Input),
                                                          &Input, 16, Output));
            printf("process_usage = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageByProcessBytes,
                                                                  sizeof(ProcessInput), &ProcessInput, 24, Output));
        }

        {
            DXCoreEngineQueryInput Input = { { 0, 0 }, Self };
            UINT Physical = 0;

            printf("running = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds,
                                                            sizeof(Input), &Input, 8, Output));
            printf("process_running = %08lX\n",
                   Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeByProcessMicroseconds,
                                       sizeof(Input), &Input, 16, Output));
            printf("temperature = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterTemperatureCelsius,
                                                                sizeof(Physical), &Physical, 4, Output));
            printf("engine_frequency = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterEngineFrequencyHertz,
                                                                     8, &Input, 24, Output));
            printf("memory_frequency = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterMemoryFrequencyHertz,
                                                                     sizeof(Physical), &Physical, 24, Output));
        }

        {
            DXCoreAdapterProcessSetQueryInput Input = { _countof(Processes), Processes };

            printf("process_count = %08lX\n",
                   Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessCount, 0, nullptr, 4, Output));
            printf("process_set = %08lX\n", Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessSet,
                                                                sizeof(Input), &Input, 8, Output));
        }
        Adapter->Release();
    }
    Factory->Release();
}

static
VOID
TrScenarioSetState(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = TrFactory(Context);

    if (Factory == nullptr)
        return;

    for (IDXCoreAdapter* Adapter : TrAdapters(Factory))
    {
        DXCoreAdapterMemoryBudgetNodeSegmentGroup Input = { 0, DXCoreSegmentGroup::Local };
        UINT64 Reservation = 0;

        /* No reservation is the starting state, so this changes nothing */
        printf("reserve = %08lX\n", Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(Input), &Input,
                                                      sizeof(Reservation), &Reservation));
        Adapter->Release();
    }
    Factory->Release();
}

static
VOID
TrScenarioSort(
    _In_ TrContext* Context)
{
    static const DXCoreAdapterPreference Orders[][2] =
    {
        { DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::Hardware },
        { DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::HighPerformance },
        { DXCoreAdapterPreference::MinimumPower, DXCoreAdapterPreference::Hardware },
    };
    IDXCoreAdapterFactory* Factory = TrFactory(Context);
    IDXCoreAdapterList* List = nullptr;

    if (Factory == nullptr)
        return;

    if (SUCCEEDED(Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML, IID_PPV_ARGS(&List))))
    {
        for (const auto& Order : Orders)
            printf("sort = %08lX\n", List->Sort(2, Order));
        List->Release();
    }
    Factory->Release();
}

static
VOID
CALLBACK
TrIgnoreNotification(
    _In_ DXCoreNotificationType Type,
    _In_ IUnknown* Object,
    _In_opt_ void* Context)
{
}

static
VOID
TrScenarioNotify(
    _In_ TrContext* Context)
{
    IDXCoreAdapterFactory* Factory = TrFactory(Context);

    if (Factory == nullptr)
        return;

    for (IDXCoreAdapter* Adapter : TrAdapters(Factory))
    {
        for (UINT Type = 1; Type <= 3; Type++)
        {
            uint32_t Cookie = 0;
            HRESULT hr = Factory->RegisterEventNotification(Adapter, static_cast<DXCoreNotificationType>(Type),
                                                            TrIgnoreNotification, nullptr, &Cookie);

            printf("register %u = %08lX\n", Type, hr);
            if (SUCCEEDED(hr))
                printf("unregister = %08lX\n", Factory->UnregisterEventNotification(Cookie));
        }
        Adapter->Release();
    }
    Sleep(200);
    Factory->Release();
}

static const struct
{
    PCSTR Name;
    PTR_SCENARIO Run;
} TrScenarios[] =
{
    { "factory", TrScenarioFactory },
    { "lists", TrScenarioLists },
    { "properties", TrScenarioProperties },
    { "states", TrScenarioStates },
    { "setstate", TrScenarioSetState },
    { "sort", TrScenarioSort },
    { "notify", TrScenarioNotify },
};

static
std::wstring
TrRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"dxcore.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\dxcore.dll";
    }

    return Runtime;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Runtime = TrRuntimePath(L"system");
    std::vector<std::string> Wanted;
    TrContext Context = {};
    HMODULE Module;
    char Name[64];

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = TrRuntimePath(argv[++Index]);
            continue;
        }
        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Tr.MainThread = GetCurrentThreadId();
    Module = LoadLibraryW(Runtime.c_str());
    if (Module == NULL)
    {
        fprintf(stderr, "error: cannot load %ls (%lu)\n", Runtime.c_str(), GetLastError());
        return 1;
    }
    {
        WCHAR Loaded[MAX_PATH] = {};

        GetModuleFileNameW(Module, Loaded, _countof(Loaded) - 1);
        fprintf(stderr, "runtime: %ls\n", Loaded);
    }

    fprintf(stderr, "patched imports: %u\n", TrPatchImports(Module));
    Context.Create = reinterpret_cast<PFN_TR_CREATE>(GetProcAddress(Module, "DXCoreCreateAdapterFactory"));
    if (Context.Create == nullptr)
    {
        fprintf(stderr, "error: no DXCoreCreateAdapterFactory\n");
        return 1;
    }

    for (const auto& Scenario : TrScenarios)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Item : Wanted)
            IsWanted = IsWanted || Item == Scenario.Name;
        if (!IsWanted)
            continue;

        printf("== %s\n", Scenario.Name);
        Scenario.Run(&Context);
    }
    return 0;
}
