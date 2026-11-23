/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXCore adapter factory behavior, printed as comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * dxcorecheck [-runtime system|reactx|<path>] [check ...]
 */

#include <windows.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <initguid.h>
#include <dxcore_interface.h>
#include <dxgi1_6.h>

#include <cstdio>
#include <string>
#include <vector>

typedef HRESULT (WINAPI* PFN_CC_CREATE)(REFIID, void**);

struct CoreCheck
{
    HMODULE Module = NULL;
    PFN_CC_CREATE Create = nullptr;
    PFN_CC_CREATE CreateInternal = nullptr;
    IDXCoreAdapterFactory* Factory = nullptr;
    std::vector<LUID> Seen;
    std::vector<IDXCoreAdapter*> Adapters;
};

typedef VOID (*PCC_CHECK)(_Inout_ CoreCheck* Check);

struct CoreCheckInfo
{
    PCSTR Name;
    PCC_CHECK Run;
};

static const GUID CcWsl = {0x42696d9d, 0xd678, 0x4006, {0xa9, 0x3d, 0x30, 0x0c, 0x35, 0x65, 0xbb, 0xe5}};
static const GUID CcUnknown = {0x12345678, 0x1234, 0x1234, {0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34}};
static const GUID CcThreadOptions = {0x0d82d832, 0xe474, 0x4da9, {0xa1, 0x62, 0xc3, 0xdf, 0xfd, 0xa0, 0xfa, 0x6d}};

struct CcNamedGuid
{
    PCSTR Name;
    const GUID* Id;
};

static const CcNamedGuid CcAttributes[] =
{
    { "d3d11", &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS },
    { "d3d12", &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS },
    { "compute", &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE },
    { "ml", &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML },
    { "media", &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA },
    { "gpu", &DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU },
    { "accel", &DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR },
    { "npu", &DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU },
    { "mediaaccel", &DXCORE_HARDWARE_TYPE_ATTRIBUTE_MEDIA_ACCELERATOR },
    { "wsl", &CcWsl },
    { "unknown", &CcUnknown },
};

/* A stable name for a LUID: the order it was first seen in */
static
int
CcLuidId(
    _Inout_ CoreCheck* Check,
    _In_ const LUID& Luid)
{
    for (size_t Index = 0; Index < Check->Seen.size(); Index++)
    {
        if (Check->Seen[Index].LowPart == Luid.LowPart && Check->Seen[Index].HighPart == Luid.HighPart)
            return static_cast<int>(Index);
    }

    Check->Seen.push_back(Luid);
    return static_cast<int>(Check->Seen.size() - 1);
}

static
int
CcAdapterId(
    _Inout_ CoreCheck* Check,
    _In_ IDXCoreAdapter* Adapter)
{
    LUID Luid = {};
    int Id;

    if (FAILED(Adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(Luid), &Luid)))
        return -1;

    Id = CcLuidId(Check, Luid);
    if (Id >= static_cast<int>(Check->Adapters.size()))
    {
        Adapter->AddRef();
        Check->Adapters.push_back(Adapter);
    }

    return Id;
}

/* Prints a list as its count, staleness and adapter ids in order */
static
VOID
CcPrintList(
    _Inout_ CoreCheck* Check,
    _In_ PCSTR Label,
    _In_ HRESULT hr,
    _In_opt_ IDXCoreAdapterList* List)
{
    printf("%s = %08lX", Label, hr);
    if (SUCCEEDED(hr) && List != nullptr)
    {
        UINT32 Count = List->GetAdapterCount();

        printf(" count=%u stale=%d [", Count, List->IsStale() ? 1 : 0);
        for (UINT32 Index = 0; Index < Count; Index++)
        {
            IDXCoreAdapter* Adapter = nullptr;
            HRESULT Got = List->GetAdapter(Index, IID_PPV_ARGS(&Adapter));

            if (SUCCEEDED(Got))
            {
                printf("%s%d", Index ? " " : "", CcAdapterId(Check, Adapter));
                Adapter->Release();
            }
            else
            {
                printf("%s!%08lX", Index ? " " : "", Got);
            }
        }
        printf("]");
    }
    printf("\n");
}

static
VOID
CcPrintBytes(
    _In_reads_bytes_(Size) const void* Data,
    _In_ size_t Size)
{
    for (size_t Index = 0; Index < Size; Index++)
        printf("%02X", static_cast<const BYTE*>(Data)[Index]);
}

static
VOID
CcCheckCreate(
    _Inout_ CoreCheck* Check)
{
    IUnknown* First = nullptr;
    IUnknown* Second = nullptr;
    void* Object = reinterpret_cast<void*>(1);
    HRESULT hr;

    printf("create_null = %08lX\n", Check->Create(__uuidof(IDXCoreAdapterFactory), nullptr));

    hr = Check->Create(__uuidof(IDXCoreAdapterList), &Object);
    printf("create_list_iid = %08lX object=%d\n", hr, Object != nullptr);
    if (SUCCEEDED(hr))
        static_cast<IUnknown*>(Object)->Release();

    hr = Check->Create(__uuidof(IDXGIFactory), &Object);
    printf("create_dxgi_iid = %08lX object=%d\n", hr, Object != nullptr);
    if (SUCCEEDED(hr))
        static_cast<IUnknown*>(Object)->Release();

    hr = Check->Create(__uuidof(IUnknown), reinterpret_cast<void**>(&First));
    printf("create_unknown = %08lX\n", hr);
    hr = Check->Create(__uuidof(IUnknown), reinterpret_cast<void**>(&Second));
    printf("create_again = %08lX same=%d\n", hr, First == Second);

    for (const IID* Riid : { &__uuidof(IDXCoreAdapterFactory), &__uuidof(IDXCoreAdapterFactory1),
                             &__uuidof(IDXCoreAdapterFactory2) })
    {
        IUnknown* Typed = nullptr;
        IUnknown* Identity = nullptr;

        hr = Check->Create(*Riid, reinterpret_cast<void**>(&Typed));
        printf("create_typed = %08lX", hr);
        if (SUCCEEDED(hr))
        {
            Typed->QueryInterface(IID_PPV_ARGS(&Identity));
            printf(" same=%d", Identity == First);
            Identity->Release();
            Typed->Release();
        }
        printf("\n");
    }

    if (Second != nullptr)
        Second->Release();
    if (First != nullptr)
        First->Release();
}

static
VOID
CcCheckQuery(
    _Inout_ CoreCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        const IID* Riid;
    } Interfaces[] =
    {
        { "unknown", &__uuidof(IUnknown) },
        { "factory", &__uuidof(IDXCoreAdapterFactory) },
        { "factory1", &__uuidof(IDXCoreAdapterFactory1) },
        { "factory2", &__uuidof(IDXCoreAdapterFactory2) },
        { "list", &__uuidof(IDXCoreAdapterList) },
        { "adapter", &__uuidof(IDXCoreAdapter) },
        { "adapter1", &__uuidof(IDXCoreAdapter1) },
        { "dxgi", &__uuidof(IDXGIFactory) },
    };

    for (const auto& Interface : Interfaces)
    {
        IUnknown* Object = nullptr;
        HRESULT hr = Check->Factory->QueryInterface(*Interface.Riid, reinterpret_cast<void**>(&Object));

        printf("factory_qi %s = %08lX\n", Interface.Name, hr);
        if (SUCCEEDED(hr))
            Object->Release();
    }
}

static
VOID
CcCheckLists(
    _Inout_ CoreCheck* Check)
{
    char Label[96];

    for (const CcNamedGuid& Attribute : CcAttributes)
    {
        IDXCoreAdapterList* List = nullptr;
        HRESULT hr = Check->Factory->CreateAdapterList(1, Attribute.Id, IID_PPV_ARGS(&List));

        sprintf_s(Label, "list %s", Attribute.Name);
        CcPrintList(Check, Label, hr, List);
        if (List != nullptr)
            List->Release();
    }

    for (size_t First = 0; First < 5; First++)
    {
        for (size_t Second = First + 1; Second < _countof(CcAttributes); Second++)
        {
            GUID Pair[2] = { *CcAttributes[First].Id, *CcAttributes[Second].Id };
            IDXCoreAdapterList* List = nullptr;
            HRESULT hr = Check->Factory->CreateAdapterList(2, Pair, IID_PPV_ARGS(&List));

            sprintf_s(Label, "list %s+%s", CcAttributes[First].Name, CcAttributes[Second].Name);
            CcPrintList(Check, Label, hr, List);
            if (List != nullptr)
                List->Release();
        }
    }

    {
        IDXCoreAdapterList* List = reinterpret_cast<IDXCoreAdapterList*>(1);
        IUnknown* Wrong = reinterpret_cast<IUnknown*>(1);
        HRESULT hr;

        hr = Check->Factory->CreateAdapterList(0, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_PPV_ARGS(&List));
        printf("list_zero = %08lX out=%d\n", hr, List != nullptr);
        List = reinterpret_cast<IDXCoreAdapterList*>(1);
        hr = Check->Factory->CreateAdapterList(1, nullptr, IID_PPV_ARGS(&List));
        printf("list_null_attributes = %08lX out=%d\n", hr, List != nullptr);
        hr = Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS,
                                               __uuidof(IDXCoreAdapterList), nullptr);
        printf("list_null_out = %08lX\n", hr);
        hr = Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, __uuidof(IDXCoreAdapter),
                                               reinterpret_cast<void**>(&Wrong));
        printf("list_wrong_iid = %08lX out=%d\n", hr, Wrong != nullptr);
        Wrong = nullptr;
        hr = Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, __uuidof(IUnknown),
                                               reinterpret_cast<void**>(&Wrong));
        printf("list_unknown_iid = %08lX\n", hr);
        if (SUCCEEDED(hr))
            Wrong->Release();
    }

    {
        IDXCoreAdapterList* List = nullptr;
        IDXCoreAdapter* Adapter = reinterpret_cast<IDXCoreAdapter*>(1);
        HRESULT hr;

        if (SUCCEEDED(Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS,
                                                        IID_PPV_ARGS(&List))))
        {
            hr = List->GetAdapter(List->GetAdapterCount(), IID_PPV_ARGS(&Adapter));
            printf("get_adapter_past_end = %08lX out=%d\n", hr, Adapter != nullptr);
            hr = List->GetAdapter(0, __uuidof(IDXCoreAdapter), nullptr);
            printf("get_adapter_null = %08lX\n", hr);
            hr = List->GetAdapter(0, __uuidof(IDXCoreAdapterList), reinterpret_cast<void**>(&Adapter));
            printf("get_adapter_wrong_iid = %08lX out=%d\n", hr, Adapter != nullptr);
            List->Release();
        }
    }
}

/* Every list on the factory, so each adapter gets an id before the per adapter checks */
static
VOID
CcGatherAdapters(
    _Inout_ CoreCheck* Check)
{
    for (const CcNamedGuid& Attribute : CcAttributes)
    {
        IDXCoreAdapterList* List = nullptr;

        if (FAILED(Check->Factory->CreateAdapterList(1, Attribute.Id, IID_PPV_ARGS(&List))))
            continue;

        for (UINT32 Index = 0; Index < List->GetAdapterCount(); Index++)
        {
            IDXCoreAdapter* Adapter = nullptr;

            if (SUCCEEDED(List->GetAdapter(Index, IID_PPV_ARGS(&Adapter))))
            {
                CcAdapterId(Check, Adapter);
                Adapter->Release();
            }
        }
        List->Release();
    }
}

static
VOID
CcPrintProperty(
    _Inout_ CoreCheck* Check,
    _In_ IDXCoreAdapter* Adapter,
    _In_ int Id,
    _In_ UINT Property)
{
    DXCoreAdapterProperty Which = static_cast<DXCoreAdapterProperty>(Property);
    std::vector<BYTE> Buffer;
    size_t Size = 0xDEAD;
    HRESULT hr;

    printf("adapter %d property %u supported=%d", Id, Property, Adapter->IsPropertySupported(Which) ? 1 : 0);
    hr = Adapter->GetPropertySize(Which, &Size);
    printf(" size=%08lX:%zu", hr, Size);
    printf(" size_null=%08lX", Adapter->GetPropertySize(Which, nullptr));
    printf(" null=%08lX", Adapter->GetProperty(Which, 16, nullptr));

    Buffer.assign(SUCCEEDED(hr) ? Size + 8 : 64, 0xCC);
    if (SUCCEEDED(hr) && Size > 0)
    {
        printf(" short=%08lX", Adapter->GetProperty(Which, Size - 1, Buffer.data()));
        Buffer.assign(Size + 8, 0xCC);
    }

    hr = Adapter->GetProperty(Which, SUCCEEDED(hr) ? Size : 64, Buffer.data());
    printf(" get=%08lX", hr);
    if (SUCCEEDED(hr))
    {
        printf(" value=");
        if (Which == DXCoreAdapterProperty::InstanceLuid)
            printf("luid%d", CcLuidId(Check, *reinterpret_cast<LUID*>(Buffer.data())));
        else if (Which == DXCoreAdapterProperty::DriverDescription)
            printf("\"%s\"", reinterpret_cast<char*>(Buffer.data()));
        else
            CcPrintBytes(Buffer.data(), Size);
    }
    else
    {
        printf(" cleared=%d", Buffer[0] == 0);
    }
    printf("\n");
}

static
VOID
CcCheckAdapters(
    _Inout_ CoreCheck* Check)
{
    CcGatherAdapters(Check);

    for (size_t Id = 0; Id < Check->Adapters.size(); Id++)
    {
        IDXCoreAdapter* Adapter = Check->Adapters[Id];
        IDXCoreAdapter1* Adapter1 = nullptr;
        IUnknown* Object = nullptr;
        UINT PhysicalCount = 0;
        HRESULT hr;

        printf("adapter %zu valid=%d\n", Id, Adapter->IsValid() ? 1 : 0);
        for (const IID* Riid : { &__uuidof(IUnknown), &__uuidof(IDXCoreAdapter), &__uuidof(IDXCoreAdapter1),
                                 &__uuidof(IDXCoreAdapterList), &__uuidof(IDXCoreAdapterFactory) })
        {
            hr = Adapter->QueryInterface(*Riid, reinterpret_cast<void**>(&Object));
            printf("adapter %zu qi = %08lX\n", Id, hr);
            if (SUCCEEDED(hr))
                Object->Release();
        }

        for (const CcNamedGuid& Attribute : CcAttributes)
            printf("adapter %zu attribute %s = %d\n", Id, Attribute.Name, Adapter->IsAttributeSupported(*Attribute.Id));

        for (UINT Property = 0; Property <= 20; Property++)
            CcPrintProperty(Check, Adapter, static_cast<int>(Id), Property);
        CcPrintProperty(Check, Adapter, static_cast<int>(Id), 10000);
        CcPrintProperty(Check, Adapter, static_cast<int>(Id), 10001);

        if (FAILED(Adapter->QueryInterface(IID_PPV_ARGS(&Adapter1))))
            continue;

        Adapter->GetProperty(DXCoreAdapterProperty::PhysicalAdapterCount, sizeof(PhysicalCount), &PhysicalCount);
        for (UINT Physical = 0; Physical <= PhysicalCount; Physical++)
        {
            DXCoreAdapterEngineIndex Index = { Physical, 0 };
            UINT EngineCount = 0xDEAD;

            hr = Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::AdapterEngineCount, sizeof(Index), &Index,
                                                sizeof(EngineCount), &EngineCount);
            printf("adapter %zu engines physical %u = %08lX:%u\n", Id, Physical, hr, EngineCount);
            if (FAILED(hr))
                continue;

            for (UINT Engine = 0; Engine <= EngineCount; Engine++)
            {
                WCHAR Name[64] = {};
                DXCoreEngineNamePropertyInput Input = { { Physical, Engine }, _countof(Name), Name };
                DXCoreEngineNamePropertyOutput Output = { 0xDEAD };

                hr = Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::AdapterEngineName, sizeof(Input), &Input,
                                                    sizeof(Output), &Output);
                printf("adapter %zu engine %u.%u = %08lX len=%u name=\"%ls\"\n", Id, Physical, Engine, hr,
                       Output.engineNameLength, Name);
            }
        }

        {
            DXCoreAdapterEngineIndex Index = {};
            UINT Value = 0xDEAD;
            LUID Luid = {};

            printf("adapter %zu with_input_small = %08lX\n", Id,
                   Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::AdapterEngineCount, 2, &Index, 4, &Value));
            printf("adapter %zu with_input_null = %08lX\n", Id,
                   Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::AdapterEngineCount, 8, &Index, 4, nullptr));
            printf("adapter %zu with_input_plain = %08lX\n", Id,
                   Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::InstanceLuid, 0, nullptr, 8, &Luid));
            printf("adapter %zu with_input_plain_extra = %08lX\n", Id,
                   Adapter1->GetPropertyWithInput(DXCoreAdapterProperty::InstanceLuid, 4, &Value, 8, &Luid));
            printf("adapter %zu with_input_unknown = %08lX\n", Id,
                   Adapter1->GetPropertyWithInput(static_cast<DXCoreAdapterProperty>(30), 0, nullptr, 8, &Luid));
        }
        Adapter1->Release();
    }
}

/* A value class that stays the same from run to run */
static
PCSTR
CcClass(
    _In_ UINT64 Value)
{
    return Value ? "nonzero" : "zero";
}

static
VOID
CcCheckStates(
    _Inout_ CoreCheck* Check)
{
    CcGatherAdapters(Check);

    for (size_t Id = 0; Id < Check->Adapters.size(); Id++)
    {
        IDXCoreAdapter* Adapter = Check->Adapters[Id];
        DWORD Self = GetCurrentProcessId();
        BYTE Scratch[64];
        HRESULT hr;

        for (UINT State = 0; State <= 13; State++)
        {
            printf("adapter %zu state %u query=%d set=%d\n", Id, State,
                   Adapter->IsQueryStateSupported(static_cast<DXCoreAdapterState>(State)) ? 1 : 0,
                   Adapter->IsSetStateSupported(static_cast<DXCoreAdapterState>(State)) ? 1 : 0);
        }

        for (UINT State = 0; State <= 13; State++)
        {
            printf("adapter %zu state %u null = %08lX\n", Id, State,
                   Adapter->QueryState(static_cast<DXCoreAdapterState>(State), 0, nullptr, 8, nullptr));
        }

        {
            BYTE Updating = 0xCC;

            hr = Adapter->QueryState(DXCoreAdapterState::IsDriverUpdateInProgress, 0, nullptr, 1, &Updating);
            printf("adapter %zu updating = %08lX:%u\n", Id, hr, Updating);
            hr = Adapter->QueryState(DXCoreAdapterState::IsDriverUpdateInProgress, 0, nullptr, 0, &Updating);
            printf("adapter %zu updating_empty = %08lX\n", Id, hr);
        }

        for (UINT Group = 0; Group <= 2; Group++)
        {
            DXCoreAdapterMemoryBudgetNodeSegmentGroup Input = { 0, static_cast<DXCoreSegmentGroup>(Group) };
            DXCoreAdapterMemoryBudget Budget;

            memset(&Budget, 0xCC, sizeof(Budget));
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(Input), &Input, sizeof(Budget),
                                     &Budget);
            printf("adapter %zu budget %u = %08lX budget=%s usage=%s available=%s reservation=%s\n", Id, Group, hr,
                   CcClass(Budget.budget), CcClass(Budget.currentUsage), CcClass(Budget.availableForReservation),
                   CcClass(Budget.currentReservation));
        }

        {
            DXCoreAdapterMemoryBudgetNodeSegmentGroup Input = {};

            printf("adapter %zu budget_null_input = %08lX\n", Id,
                   Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, 8, nullptr, 32, Scratch));
            printf("adapter %zu budget_small_input = %08lX\n", Id,
                   Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, 4, &Input, 32, Scratch));
            printf("adapter %zu budget_small_output = %08lX\n", Id,
                   Adapter->QueryState(DXCoreAdapterState::AdapterMemoryBudget, 8, &Input, 31, Scratch));
        }

        for (UINT Type = 0; Type <= 3; Type++)
        {
            DXCoreMemoryQueryInput Input = { 0, static_cast<DXCoreMemoryType>(Type) };
            DXCoreMemoryUsage Usage;
            DXCoreProcessMemoryQueryInput ProcessInput = { 0, static_cast<DXCoreMemoryType>(Type), Self };
            DXCoreProcessMemoryQueryOutput ProcessUsage;

            memset(&Usage, 0xCC, sizeof(Usage));
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, sizeof(Input), &Input, sizeof(Usage),
                                     &Usage);
            printf("adapter %zu usage %u = %08lX\n", Id, Type, hr);

            memset(&ProcessUsage, 0xCC, sizeof(ProcessUsage));
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageByProcessBytes, sizeof(ProcessInput),
                                     &ProcessInput, sizeof(ProcessUsage), &ProcessUsage);
            printf("adapter %zu process_usage %u = %08lX ok=%d resident=%s\n", Id, Type, hr,
                   ProcessUsage.processQuerySucceeded, CcClass(ProcessUsage.memoryUsage.resident));
        }

        {
            DXCoreProcessMemoryQueryInput Input = { 0, DXCoreMemoryType::Dedicated, 0 };

            printf("adapter %zu process_usage_pid0 = %08lX\n", Id,
                   Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageByProcessBytes, sizeof(Input), &Input,
                                       24, Scratch));
            printf("adapter %zu usage_small = %08lX\n", Id,
                   Adapter->QueryState(DXCoreAdapterState::AdapterMemoryUsageBytes, 4, &Input, 16, Scratch));
        }

        {
            DXCoreEngineQueryInput Input = { { 0, 0 }, Self };
            DXCoreEngineQueryOutput Output;
            UINT64 Running = 0;

            hr = Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds, sizeof(Input), &Input,
                                     sizeof(Running), &Running);
            printf("adapter %zu running = %08lX\n", Id, hr);
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeMicroseconds, 8, &Input,
                                     sizeof(Running), &Running);
            printf("adapter %zu running_small = %08lX\n", Id, hr);

            memset(&Output, 0xCC, sizeof(Output));
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeByProcessMicroseconds, sizeof(Input),
                                     &Input, sizeof(Output), &Output);
            printf("adapter %zu process_running = %08lX ok=%d\n", Id, hr, Output.processQuerySucceeded);
            Input.processId = 0;
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterEngineRunningTimeByProcessMicroseconds, sizeof(Input),
                                     &Input, sizeof(Output), &Output);
            printf("adapter %zu process_running_pid0 = %08lX\n", Id, hr);
        }

        {
            UINT Physical = 0;
            float Celsius = -1.0f;

            hr = Adapter->QueryState(DXCoreAdapterState::AdapterTemperatureCelsius, sizeof(Physical), &Physical,
                                     sizeof(Celsius), &Celsius);
            printf("adapter %zu temperature = %08lX\n", Id, hr);
        }

        {
            UINT32 Count = 0xDEAD;
            UINT32 Processes[256] = {};
            DXCoreAdapterProcessSetQueryInput Input = { 0, Processes };
            DXCoreAdapterProcessSetQueryOutput Output = { 0xDEAD, 0xDEAD };

            hr = Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessCount, 0, nullptr, sizeof(Count), &Count);
            printf("adapter %zu process_count = %08lX\n", Id, hr);
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessCount, 4, &Count, sizeof(Count), &Count);
            printf("adapter %zu process_count_input = %08lX\n", Id, hr);

            hr = Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessSet, sizeof(Input), &Input,
                                     sizeof(Output), &Output);
            printf("adapter %zu process_set_empty = %08lX written=%u\n", Id, hr, Output.processesWritten);
            Input.arraySize = _countof(Processes);
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterInUseProcessSet, sizeof(Input), &Input,
                                     sizeof(Output), &Output);
            printf("adapter %zu process_set = %08lX\n", Id, hr);
        }

        {
            DXCoreAdapterEngineIndex Index = { 0, 0 };
            DXCoreFrequencyQueryOutput Frequency;
            UINT Physical = 0;

            hr = Adapter->QueryState(DXCoreAdapterState::AdapterEngineFrequencyHertz, sizeof(Index), &Index,
                                     sizeof(Frequency), &Frequency);
            printf("adapter %zu engine_frequency = %08lX\n", Id, hr);
            hr = Adapter->QueryState(DXCoreAdapterState::AdapterMemoryFrequencyHertz, sizeof(Physical), &Physical,
                                     sizeof(Frequency), &Frequency);
            printf("adapter %zu memory_frequency = %08lX\n", Id, hr);
        }

        {
            UINT Mode = 0xDEAD;

            hr = Adapter->QueryState(DXCoreAdapterState::SingleAdapterHybridMode, 0, nullptr, sizeof(Mode), &Mode);
            printf("adapter %zu hybrid = %08lX:%u\n", Id, hr, Mode);
        }

        printf("adapter %zu state_unknown = %08lX\n", Id,
               Adapter->QueryState(static_cast<DXCoreAdapterState>(13), 0, nullptr, 8, Scratch));
    }
}

static
VOID
CcCheckSetState(
    _Inout_ CoreCheck* Check)
{
    CcGatherAdapters(Check);

    for (size_t Id = 0; Id < Check->Adapters.size(); Id++)
    {
        IDXCoreAdapter* Adapter = Check->Adapters[Id];
        DXCoreAdapterMemoryBudgetNodeSegmentGroup Input = { 0, DXCoreSegmentGroup::Local };
        UINT64 Reservation = 0;
        UINT Mode = 0;
        HRESULT hr;

        /* No reservation is what every process starts with, so asking for none changes nothing */
        hr = Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, sizeof(Input), &Input, sizeof(Reservation),
                               &Reservation);
        printf("adapter %zu reserve_none = %08lX\n", Id, hr);
        printf("adapter %zu reserve_null_input = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, 8, nullptr, 8, &Reservation));
        printf("adapter %zu reserve_small_input = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, 4, &Input, 8, &Reservation));
        printf("adapter %zu reserve_null_data = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, 8, &Input, 8, nullptr));
        printf("adapter %zu reserve_small_data = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, 8, &Input, 4, &Reservation));
        Input.segmentGroup = static_cast<DXCoreSegmentGroup>(2);
        printf("adapter %zu reserve_bad_group = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::AdapterMemoryBudget, 8, &Input, 8, &Reservation));

        /* The hybrid mode lives in the adapter object only; it is put back to what it was */
        Adapter->QueryState(DXCoreAdapterState::SingleAdapterHybridMode, 0, nullptr, sizeof(Mode), &Mode);
        printf("adapter %zu hybrid_set = %08lX\n", Id,
               Adapter->SetState(DXCoreAdapterState::SingleAdapterHybridMode, 0, nullptr, sizeof(Mode), &Mode));

        for (UINT State = 0; State <= 13; State++)
        {
            if (State == 1 || State == 11)
                continue;

            printf("adapter %zu set %u = %08lX\n", Id, State,
                   Adapter->SetState(static_cast<DXCoreAdapterState>(State), 0, nullptr, 8, &Reservation));
        }
    }
}

static
VOID
CcCheckSort(
    _Inout_ CoreCheck* Check)
{
    static const std::vector<std::vector<DXCoreAdapterPreference>> Orders =
    {
        { DXCoreAdapterPreference::Hardware },
        { DXCoreAdapterPreference::MinimumPower },
        { DXCoreAdapterPreference::HighPerformance },
        { DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::MinimumPower },
        { DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::HighPerformance },
        { DXCoreAdapterPreference::MinimumPower, DXCoreAdapterPreference::Hardware },
        { DXCoreAdapterPreference::HighPerformance, DXCoreAdapterPreference::MinimumPower },
        { static_cast<DXCoreAdapterPreference>(7) },
        { static_cast<DXCoreAdapterPreference>(7), DXCoreAdapterPreference::HighPerformance },
    };
    char Label[96];

    for (const CcNamedGuid& Attribute : CcAttributes)
    {
        IDXCoreAdapterList* List = nullptr;

        if (FAILED(Check->Factory->CreateAdapterList(1, Attribute.Id, IID_PPV_ARGS(&List))))
            continue;

        for (size_t Index = 0; Index < Orders.size(); Index++)
        {
            HRESULT hr = List->Sort(static_cast<uint32_t>(Orders[Index].size()), Orders[Index].data());

            sprintf_s(Label, "sort %s %zu", Attribute.Name, Index);
            CcPrintList(Check, Label, hr, List);
        }

        printf("sort %s zero = %08lX\n", Attribute.Name, List->Sort(0, Orders[0].data()));
        printf("sort %s null = %08lX\n", Attribute.Name, List->Sort(1, nullptr));
        for (UINT Preference = 0; Preference <= 4; Preference++)
        {
            printf("sort %s preference_supported %u = %d\n", Attribute.Name, Preference,
                   List->IsAdapterPreferenceSupported(static_cast<DXCoreAdapterPreference>(Preference)) ? 1 : 0);
        }
        List->Release();
    }
}

static
VOID
CcCheckLuid(
    _Inout_ CoreCheck* Check)
{
    LUID Zero = {};
    IDXCoreAdapter* Adapter = reinterpret_cast<IDXCoreAdapter*>(1);
    HRESULT hr;

    CcGatherAdapters(Check);
    for (size_t Id = 0; Id < Check->Seen.size(); Id++)
    {
        IUnknown* Object = nullptr;

        hr = Check->Factory->GetAdapterByLuid(Check->Seen[Id], IID_PPV_ARGS(&Adapter));
        printf("luid %zu = %08lX", Id, hr);
        if (SUCCEEDED(hr))
        {
            printf(" id=%d", CcAdapterId(Check, Adapter));
            Adapter->Release();
        }
        printf("\n");

        hr = Check->Factory->GetAdapterByLuid(Check->Seen[Id], __uuidof(IDXCoreAdapterList),
                                              reinterpret_cast<void**>(&Object));
        printf("luid %zu wrong_iid = %08lX\n", Id, hr);
        printf("luid %zu null = %08lX\n", Id,
               Check->Factory->GetAdapterByLuid(Check->Seen[Id], __uuidof(IDXCoreAdapter), nullptr));
    }

    Adapter = reinterpret_cast<IDXCoreAdapter*>(1);
    hr = Check->Factory->GetAdapterByLuid(Zero, IID_PPV_ARGS(&Adapter));
    printf("luid_zero = %08lX out=%d\n", hr, Adapter != nullptr);

    /* Every adapter the kernel lists, including ones the factory does not hand out */
    {
        typedef NTSTATUS (APIENTRY* PFN_CC_ENUM)(D3DKMT_ENUMADAPTERS2*);
        HMODULE Gdi32 = LoadLibraryW(L"gdi32.dll");
        PFN_CC_ENUM Enum = reinterpret_cast<PFN_CC_ENUM>(GetProcAddress(Gdi32, "D3DKMTEnumAdapters2"));
        D3DKMT_ADAPTERINFO Infos[32] = {};
        D3DKMT_ENUMADAPTERS2 Request = { _countof(Infos), Infos };

        if (Enum != nullptr && Enum(&Request) >= 0)
        {
            for (ULONG Index = 0; Index < Request.NumAdapters; Index++)
            {
                hr = Check->Factory->GetAdapterByLuid(Infos[Index].AdapterLuid, IID_PPV_ARGS(&Adapter));
                printf("kernel_luid %d = %08lX\n", CcLuidId(Check, Infos[Index].AdapterLuid), hr);
                if (SUCCEEDED(hr))
                    Adapter->Release();
            }
        }
    }
}

static
VOID
CcCheckFactoryLinks(
    _Inout_ CoreCheck* Check)
{
    IDXCoreAdapterList* List = nullptr;
    IUnknown* Identity = nullptr;

    Check->Factory->QueryInterface(IID_PPV_ARGS(&Identity));
    if (SUCCEEDED(Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_PPV_ARGS(&List))))
    {
        IUnknown* Object = nullptr;
        IDXCoreAdapter* Adapter = nullptr;
        HRESULT hr;

        hr = List->GetFactory(IID_PPV_ARGS(&Object));
        printf("list_factory = %08lX same=%d\n", hr, Object == Identity);
        if (SUCCEEDED(hr))
            Object->Release();
        printf("list_factory_null = %08lX\n", List->GetFactory(__uuidof(IUnknown), nullptr));
        printf("list_factory_wrong = %08lX\n", List->GetFactory(__uuidof(IDXCoreAdapter), reinterpret_cast<void**>(&Object)));

        if (List->GetAdapterCount() != 0 && SUCCEEDED(List->GetAdapter(0, IID_PPV_ARGS(&Adapter))))
        {
            hr = Adapter->GetFactory(IID_PPV_ARGS(&Object));
            printf("adapter_factory = %08lX same=%d\n", hr, Object == Identity);
            if (SUCCEEDED(hr))
                Object->Release();
            printf("adapter_factory_null = %08lX\n", Adapter->GetFactory(__uuidof(IUnknown), nullptr));
            Adapter->Release();
        }
        List->Release();
    }
    Identity->Release();
}

static LONG CcCallbacks;

static
VOID
STDMETHODCALLTYPE
CcCallback(
    _In_ DXCoreNotificationType Type,
    _In_ IUnknown* Object,
    _In_opt_ void* Context)
{
    InterlockedIncrement(&CcCallbacks);
}

/* An object DXCore did not make */
class CcStranger final : public IUnknown
{
public:
    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override
    {
        *Object = nullptr;
        if (Riid != __uuidof(IUnknown))
            return E_NOINTERFACE;
        *Object = this;
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef(VOID) override { return 2; }
    STDMETHODIMP_(ULONG) Release(VOID) override { return 1; }
};

static
VOID
CcCheckNotify(
    _Inout_ CoreCheck* Check)
{
    IDXCoreAdapterList* List = nullptr;
    IDXCoreAdapter* Adapter = nullptr;
    std::vector<uint32_t> Cookies;
    CcStranger Stranger;
    uint32_t Cookie;
    HRESULT hr;

    for (UINT Type = 0; Type <= 5; Type++)
    {
        printf("notify_supported %u = %d\n", Type,
               Check->Factory->IsNotificationTypeSupported(static_cast<DXCoreNotificationType>(Type)) ? 1 : 0);
    }

    if (FAILED(Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_PPV_ARGS(&List))))
        return;
    if (List->GetAdapterCount() == 0 || FAILED(List->GetAdapter(0, IID_PPV_ARGS(&Adapter))))
    {
        List->Release();
        return;
    }

    for (UINT Type = 0; Type <= 4; Type++)
    {
        Cookie = 0xDEAD;
        hr = Check->Factory->RegisterEventNotification(List, static_cast<DXCoreNotificationType>(Type), CcCallback,
                                                       nullptr, &Cookie);
        printf("notify list %u = %08lX cookie=%u\n", Type, hr, Cookie);
        if (SUCCEEDED(hr))
            Cookies.push_back(Cookie);
    }

    InterlockedExchange(&CcCallbacks, 0);
    for (UINT Type = 0; Type <= 4; Type++)
    {
        Cookie = 0xDEAD;
        hr = Check->Factory->RegisterEventNotification(Adapter, static_cast<DXCoreNotificationType>(Type), CcCallback,
                                                       nullptr, &Cookie);
        printf("notify adapter %u = %08lX cookie=%u\n", Type, hr, Cookie);
        if (SUCCEEDED(hr))
            Cookies.push_back(Cookie);
    }

    /* Budget and teardown watchers hear once right away */
    for (UINT Wait = 0; Wait < 200 && ReadAcquire(&CcCallbacks) < 2; Wait++)
        Sleep(10);
    printf("notify immediate = %d\n", ReadAcquire(&CcCallbacks) >= 2);

    Cookie = 0xDEAD;
    hr = Check->Factory->RegisterEventNotification(List, DXCoreNotificationType::AdapterListStale, CcCallback, nullptr,
                                                   nullptr);
    printf("notify null_cookie = %08lX\n", hr);
    hr = Check->Factory->RegisterEventNotification(nullptr, DXCoreNotificationType::AdapterListStale, CcCallback,
                                                   nullptr, &Cookie);
    printf("notify null_object = %08lX cookie=%u\n", hr, Cookie);
    Cookie = 0xDEAD;
    hr = Check->Factory->RegisterEventNotification(List, DXCoreNotificationType::AdapterListStale, nullptr, nullptr,
                                                   &Cookie);
    printf("notify null_callback = %08lX cookie=%u\n", hr, Cookie);
    Cookie = 0xDEAD;
    hr = Check->Factory->RegisterEventNotification(&Stranger, DXCoreNotificationType::AdapterListStale, CcCallback,
                                                   nullptr, &Cookie);
    printf("notify stranger = %08lX cookie=%u\n", hr, Cookie);
    Cookie = 0xDEAD;
    hr = Check->Factory->RegisterEventNotification(List, static_cast<DXCoreNotificationType>(9), CcCallback, nullptr,
                                                   &Cookie);
    printf("notify bad_type = %08lX cookie=%u\n", hr, Cookie);

    for (uint32_t Taken : Cookies)
        printf("unregister %u = %08lX\n", Taken, Check->Factory->UnregisterEventNotification(Taken));
    for (uint32_t Taken : Cookies)
        printf("unregister_again %u = %08lX\n", Taken, Check->Factory->UnregisterEventNotification(Taken));
    for (uint32_t Guess = 1; Guess <= 12; Guess++)
        printf("unregister_guess %u = %08lX\n", Guess, Check->Factory->UnregisterEventNotification(Guess));

    Cookie = 0xDEAD;
    hr = Check->Factory->RegisterEventNotification(List, DXCoreNotificationType::AdapterListStale, CcCallback, nullptr,
                                                   &Cookie);
    printf("notify after = %08lX cookie=%u\n", hr, Cookie);
    if (SUCCEEDED(hr))
        Check->Factory->UnregisterEventNotification(Cookie);

    Adapter->Release();
    List->Release();
}

static
VOID
CcCheckWorkload(
    _Inout_ CoreCheck* Check)
{
    IDXCoreAdapterFactory1* Factory1 = nullptr;
    static const UINT Runtimes[] = { 0, 1, 2, 3, 4 };
    static const UINT Kinds[] = { 0, 1, 2, 4, 8, 15, 16 };
    char Label[96];

    if (FAILED(Check->Factory->QueryInterface(IID_PPV_ARGS(&Factory1))))
        return;

    for (UINT Workload = 0; Workload <= 4; Workload++)
    {
        for (UINT Runtime : Runtimes)
        {
            for (UINT Kind : Kinds)
            {
                IDXCoreAdapterList* List = nullptr;
                HRESULT hr = Factory1->CreateAdapterListByWorkload(static_cast<DXCoreWorkload>(Workload),
                                                                   static_cast<DXCoreRuntimeFilterFlags>(Runtime),
                                                                   static_cast<DXCoreHardwareTypeFilterFlags>(Kind),
                                                                   IID_PPV_ARGS(&List));

                sprintf_s(Label, "workload %u runtime %u kind %u", Workload, Runtime, Kind);
                CcPrintList(Check, Label, hr, List);
                if (List != nullptr)
                    List->Release();
            }
        }
    }

    printf("workload_null = %08lX\n", Factory1->CreateAdapterListByWorkload(DXCoreWorkload::Graphics,
                                                                             DXCoreRuntimeFilterFlags::None,
                                                                             DXCoreHardwareTypeFilterFlags::None,
                                                                             __uuidof(IDXCoreAdapterList), nullptr));
    Factory1->Release();
}

static
VOID
CcCheckReserved(
    _Inout_ CoreCheck* Check)
{
    IDXCoreAdapterFactory2* Factory2 = nullptr;
    DXCoreReservedAcceleratorMemoryInfo Info = {};

    if (FAILED(Check->Factory->QueryInterface(IID_PPV_ARGS(&Factory2))))
        return;

    printf("reserved = %08lX\n", Factory2->GetReservedAcceleratorMemoryInfo(&Info));
    printf("reserved_null = %08lX\n", Factory2->GetReservedAcceleratorMemoryInfo(nullptr));
    Factory2->Release();
}

/* The interface DXCoreCreateInternalFactory hands out has one method after IUnknown */
struct CcThreadOptionsVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(void*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(void*);
    ULONG (STDMETHODCALLTYPE* Release)(void*);
    HRESULT (STDMETHODCALLTYPE* SetForThread)(void*, UINT32, BOOL);
};

static
VOID
CcCheckInternal(
    _Inout_ CoreCheck* Check)
{
    void* Options = nullptr;
    IUnknown* Unknown = nullptr;
    HRESULT hr;

    if (Check->CreateInternal == nullptr)
    {
        printf("internal = missing\n");
        return;
    }

    printf("internal_null = %08lX\n", Check->CreateInternal(CcThreadOptions, nullptr));
    hr = Check->CreateInternal(__uuidof(IDXCoreAdapterFactory), reinterpret_cast<void**>(&Unknown));
    printf("internal_factory_iid = %08lX\n", hr);
    hr = Check->CreateInternal(__uuidof(IUnknown), reinterpret_cast<void**>(&Unknown));
    printf("internal_unknown = %08lX\n", hr);
    hr = Check->CreateInternal(CcThreadOptions, &Options);
    printf("internal = %08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        CcThreadOptionsVtbl* Table = *static_cast<CcThreadOptionsVtbl**>(Options);

        printf("internal_set 1 = %08lX\n", Table->SetForThread(Options, 1, TRUE));
        printf("internal_set 0 = %08lX\n", Table->SetForThread(Options, 0, TRUE));

        /* With the switch on, any adapter the kernel lists can be fetched by LUID */
        for (size_t Id = 0; Id < Check->Seen.size(); Id++)
        {
            IDXCoreAdapter* Adapter = nullptr;

            hr = Check->Factory->GetAdapterByLuid(Check->Seen[Id], IID_PPV_ARGS(&Adapter));
            printf("internal_luid %zu = %08lX\n", Id, hr);
            if (SUCCEEDED(hr))
                Adapter->Release();
        }

        printf("internal_set 0 off = %08lX\n", Table->SetForThread(Options, 0, FALSE));
        Table->Release(Options);
    }
    if (Unknown != nullptr)
        Unknown->Release();
}

static
VOID
CcCheckExports(
    _Inout_ CoreCheck* Check)
{
    for (UINT Ordinal = 1; Ordinal <= 250; Ordinal++)
    {
        FARPROC Entry = GetProcAddress(Check->Module, MAKEINTRESOURCEA(Ordinal));
        HMODULE Owner = NULL;
        WCHAR Path[MAX_PATH] = {};
        PCWSTR Name = L"-";

        if (Entry != nullptr &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(Entry), &Owner))
        {
            GetModuleFileNameW(Owner, Path, _countof(Path));
            Name = wcsrchr(Path, L'\\') ? wcsrchr(Path, L'\\') + 1 : Path;
            if (Owner == Check->Module)
                Name = L"self";
        }
        printf("ordinal %u = %ls\n", Ordinal, Name);
    }

    printf("named DXCoreCreateAdapterFactory = %d\n", GetProcAddress(Check->Module, "DXCoreCreateAdapterFactory") != nullptr);
    printf("named DXCoreCreateInternalFactory = %d\n", GetProcAddress(Check->Module, "DXCoreCreateInternalFactory") != nullptr);
    printf("named D3DKMTCreateAllocation = %d\n", GetProcAddress(Check->Module, "D3DKMTCreateAllocation") != nullptr);
}

static
VOID
CcCheckLifetime(
    _Inout_ CoreCheck* Check)
{
    IDXCoreAdapterList* List = nullptr;
    IUnknown* Before = nullptr;
    IUnknown* After = nullptr;

    /* A list keeps the factory alive after the caller lets go of it */
    Check->Factory->QueryInterface(IID_PPV_ARGS(&Before));
    Check->Factory->CreateAdapterList(1, &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS, IID_PPV_ARGS(&List));
    for (IDXCoreAdapter* Adapter : Check->Adapters)
        Adapter->Release();
    Check->Adapters.clear();
    Check->Factory->Release();
    Check->Factory = nullptr;
    Before->Release();

    Check->Create(IID_PPV_ARGS(&After));
    printf("lifetime kept = %d\n", Before == After);
    After->Release();

    List->Release();
    After = nullptr;
    Check->Create(__uuidof(IUnknown), reinterpret_cast<void**>(&After));
    printf("lifetime fresh = %d\n", After != nullptr);
    Check->Create(IID_PPV_ARGS(&Check->Factory));
    After->Release();
}

static const CoreCheckInfo CcChecks[] =
{
    { "create", CcCheckCreate },
    { "query", CcCheckQuery },
    { "lists", CcCheckLists },
    { "adapters", CcCheckAdapters },
    { "states", CcCheckStates },
    { "setstate", CcCheckSetState },
    { "sort", CcCheckSort },
    { "luid", CcCheckLuid },
    { "links", CcCheckFactoryLinks },
    { "notify", CcCheckNotify },
    { "workload", CcCheckWorkload },
    { "reserved", CcCheckReserved },
    { "internal", CcCheckInternal },
    { "exports", CcCheckExports },
    { "lifetime", CcCheckLifetime },
};

static
std::wstring
CcRuntimePath(
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
    std::wstring Runtime = CcRuntimePath(L"system");
    std::vector<std::string> Wanted;
    CoreCheck Check;
    char Name[64];
    HRESULT hr;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = CcRuntimePath(argv[++Index]);
            continue;
        }
        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Check.Module = LoadLibraryW(Runtime.c_str());
    if (Check.Module == NULL)
    {
        fprintf(stderr, "error: cannot load %ls (%lu)\n", Runtime.c_str(), GetLastError());
        return 1;
    }
    {
        WCHAR Loaded[MAX_PATH] = {};

        GetModuleFileNameW(Check.Module, Loaded, _countof(Loaded) - 1);
        fprintf(stderr, "runtime: %ls\n", Loaded);
    }

    Check.Create = reinterpret_cast<PFN_CC_CREATE>(GetProcAddress(Check.Module, "DXCoreCreateAdapterFactory"));
    Check.CreateInternal = reinterpret_cast<PFN_CC_CREATE>(GetProcAddress(Check.Module, MAKEINTRESOURCEA(218)));
    if (Check.Create == nullptr)
    {
        fprintf(stderr, "error: no DXCoreCreateAdapterFactory\n");
        return 1;
    }

    hr = Check.Create(IID_PPV_ARGS(&Check.Factory));
    printf("create = %08lX\n", hr);
    if (FAILED(hr))
        return 1;

    for (const CoreCheckInfo& Info : CcChecks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Item : Wanted)
            IsWanted = IsWanted || Item == Info.Name;
        if (IsWanted)
            Info.Run(&Check);
    }

    for (IDXCoreAdapter* Adapter : Check.Adapters)
        Adapter->Release();
    if (Check.Factory != nullptr)
        Check.Factory->Release();
    return 0;
}
