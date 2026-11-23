/*
 * PROJECT:     ReactX Basic tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs Direct3D 12 scenes on the system WARP and on ours and compares the results
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <winternl.h>
#include <d3d12.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <utility>
#include <vector>

struct TcResult
{
    std::vector<BYTE> Data;
};

struct TcScene
{
    PCSTR Name;
    HRESULT (*Run)(ID3D12Device *Device, TcResult *Result);
};

/* Everything the runtime asked the driver, in call order */
static std::vector<BYTE> TcDdiLog;
static CRITICAL_SECTION TcDdiLock;

static
VOID
TcAppendTo(
    _Inout_ std::vector<BYTE> *Target,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    const BYTE *Bytes = static_cast<const BYTE *>(Data);

    Target->insert(Target->end(), Bytes, Bytes + Size);
}

template <typename Value>
static
VOID
TcLog(
    _In_ const Value &Item)
{
    EnterCriticalSection(&TcDdiLock);
    TcAppendTo(&TcDdiLog, &Item, sizeof(Item));
    LeaveCriticalSection(&TcDdiLock);
}

static
VOID
TcLogBytes(
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    EnterCriticalSection(&TcDdiLock);
    TcAppendTo(&TcDdiLog, Data, Size);
    LeaveCriticalSection(&TcDdiLock);
}

/* Record tags */
enum TC_TAG : UINT
{
    TcTagOpen = 0x10,
    TcTagVersions,
    TcTagCaps,
    TcTagDeviceSize,
    TcTagCreateDevice,
    TcTagOptionalTables,
    TcTagFillTable,
    TcTagDeviceCall,
    TcTagDestroyDevice,
    TcTagClose,
};

static PFND3D12DDI_OPENADAPTER TcRealOpen;
static D3D12DDI_ADAPTERFUNCS TcRealAdapter;
static D3D12DDI_ADAPTERFUNCS *TcRuntimeAdapter;

/* Device DDI slots are traced as 0 to 127, extended feature slots from 200; slots returning more than two registers go unwrapped */
static constexpr UINT TcDeviceSlots = 128;
static constexpr UINT TcFeatureSlotBase = 200;
static constexpr UINT TcTracedSlots = 256;
static constexpr UINT TcProgramIdentifierSlot = 122;

struct TcPair
{
    UINT64 Low;
    UINT64 High;
};

typedef TcPair (APIENTRY *TC_ANY_DDI)(UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64, UINT64);
static TC_ANY_DDI TcRealDevice[TcTracedSlots];

template <UINT Slot>
static
TcPair
APIENTRY
TcDeviceThunk(
    _In_ UINT64 A0,
    _In_ UINT64 A1,
    _In_ UINT64 A2,
    _In_ UINT64 A3,
    _In_ UINT64 A4,
    _In_ UINT64 A5,
    _In_ UINT64 A6,
    _In_ UINT64 A7)
{
    UINT Record[2] = { TcTagDeviceCall, Slot };

    TcLog(Record);
    return TcRealDevice[Slot](A0, A1, A2, A3, A4, A5, A6, A7);
}

template <UINT... Slots>
static
const TC_ANY_DDI *
TcThunkTable(
    _In_ std::integer_sequence<UINT, Slots...>)
{
    static const TC_ANY_DDI Table[] = { TcDeviceThunk<Slots>... };

    return Table;
}

static
SIZE_T
APIENTRY
TcCalcPrivateDeviceSize(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_CALCPRIVATEDEVICESIZE *Args)
{
    SIZE_T Size = TcRealAdapter.pfnCalcPrivateDeviceSize(Adapter, Args);
    UINT64 Record[2] = { TcTagDeviceSize, Size };

    TcLog(Record);
    TcLog(*Args);
    return Size;
}

static
HRESULT
APIENTRY
TcCreateDevice(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args)
{
    HRESULT hr = TcRealAdapter.pfnCreateDevice(Adapter, Args);
    UINT Record[5] = { TcTagCreateDevice, (UINT)hr, Args->Interface, Args->Version, (UINT)Args->Flags };

    TcLog(Record);
    return hr;
}

static
HRESULT
APIENTRY
TcCloseAdapter(
    _In_ D3D12DDI_HADAPTER Adapter)
{
    HRESULT hr = TcRealAdapter.pfnCloseAdapter(Adapter);
    UINT Record[2] = { TcTagClose, (UINT)hr };

    TcLog(Record);
    return hr;
}

static
HRESULT
APIENTRY
TcGetSupportedVersions(
    _In_ D3D12DDI_HADAPTER Adapter,
    _Inout_ UINT32 *Entries,
    _Out_writes_opt_(*Entries) UINT64 *Versions)
{
    UINT32 Given = *Entries;
    HRESULT hr = TcRealAdapter.pfnGetSupportedVersions(Adapter, Entries, Versions);
    UINT Record[5] = { TcTagVersions, (UINT)hr, Given, *Entries, Versions != NULL };

    TcLog(Record);
    if (SUCCEEDED(hr) && Versions != NULL)
        TcLogBytes(Versions, *Entries * sizeof(*Versions));
    return hr;
}

static
HRESULT
APIENTRY
TcGetCaps(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_GETCAPS *Args)
{
    HRESULT hr = TcRealAdapter.pfnGetCaps(Adapter, Args);
    UINT Record[5] = { TcTagCaps, (UINT)Args->Type, Args->DataSize, (UINT)hr, Args->pInfo != NULL };

    TcLog(Record);
    if (FAILED(hr) || Args->pData == NULL)
        return hr;

    /* The shader model answer is two pointers; log what they point at */
    if (Args->Type == D3D12DDICAPS_TYPE_0011_SHADER_MODELS && Args->DataSize == sizeof(D3D12DDI_D3D12_SHADER_MODELS_DATA_0011))
    {
        const D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *Models = static_cast<const D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 *>(Args->pData);
        UINT Count = Models->pNumShaderModelsSupported ? *Models->pNumShaderModelsSupported : 0xffffffff;

        UINT Pair[2] = { Count, Models->pShaderModelsSupported != NULL && Count != 0xffffffff };

        TcLog(Pair);
        if (Pair[1])
            TcLogBytes(Models->pShaderModelsSupported, Count * sizeof(*Models->pShaderModelsSupported));
        return hr;
    }
    TcLogBytes(Args->pData, Args->DataSize);
    return hr;
}

static
HRESULT
APIENTRY
TcGetOptionalDDITables(
    _In_ D3D12DDI_HADAPTER Adapter,
    _Inout_ UINT32 *Entries,
    _Out_writes_opt_(*Entries) D3D12DDI_TABLE_REQUEST *Requests)
{
    UINT32 Given = *Entries;
    HRESULT hr = TcRealAdapter.pfnGetOptionalDDITables(Adapter, Entries, Requests);
    UINT Record[5] = { TcTagOptionalTables, (UINT)hr, Given, *Entries, Requests != NULL };

    TcLog(Record);
    if (SUCCEEDED(hr) && Requests != NULL)
        TcLogBytes(Requests, *Entries * sizeof(*Requests));
    return hr;
}

static
HRESULT
APIENTRY
TcFillDDITable(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ D3D12DDI_TABLE_TYPE Type,
    _Inout_ VOID *Table,
    _In_ SIZE_T Size,
    _In_ UINT Index,
    _In_opt_ D3D12DDI_HRTTABLE RuntimeTable)
{
    static const TC_ANY_DDI *Thunks = TcThunkTable(std::make_integer_sequence<UINT, TcTracedSlots>());
    HRESULT hr = TcRealAdapter.pfnFillDDITable(Adapter, Type, Table, Size, Index, RuntimeTable);
    TC_ANY_DDI *Slots = static_cast<TC_ANY_DDI *>(Table);
    UINT Count = (UINT)(Size / sizeof(VOID *));
    UINT Record[5] = { TcTagFillTable, (UINT)Type, (UINT)Size, Index, (UINT)hr };
    std::vector<BYTE> Present(Count);

    for (UINT Slot = 0; SUCCEEDED(hr) && Slot < Count; Slot++)
        Present[Slot] = Slots[Slot] != NULL;
    TcLog(Record);
    if (SUCCEEDED(hr))
        TcLogBytes(Present.data(), Present.size());

    /* Device calls are traced by slot */
    if (SUCCEEDED(hr) && Type == D3D12DDI_TABLE_TYPE_DEVICE_CORE)
    {
        for (UINT Slot = 0; Slot < Count && Slot < TcDeviceSlots; Slot++)
        {
            if (Slots[Slot] == NULL || Slot == TcProgramIdentifierSlot)
                continue;
            TcRealDevice[Slot] = Slots[Slot];
            Slots[Slot] = Thunks[Slot];
        }
    }
    if (SUCCEEDED(hr) && Type == D3D12DDI_TABLE_TYPE_0096_EXTENDED_FEATURES)
    {
        for (UINT Slot = 0; Slot < Count && Slot < TcTracedSlots - TcFeatureSlotBase; Slot++)
        {
            if (Slots[Slot] == NULL)
                continue;
            TcRealDevice[TcFeatureSlotBase + Slot] = Slots[Slot];
            Slots[Slot] = Thunks[TcFeatureSlotBase + Slot];
        }
    }
    return hr;
}

static
VOID
APIENTRY
TcDestroyDevice(
    _In_ D3D12DDI_HDEVICE Device)
{
    UINT Record = TcTagDestroyDevice;

    TcLog(Record);
    TcRealAdapter.pfnDestroyDevice(Device);
}

static
HRESULT
APIENTRY
TcOpenAdapter12(
    _Inout_ D3D12DDIARG_OPENADAPTER *Args)
{
    HRESULT hr = TcRealOpen(Args);
    UINT Record[2] = { TcTagOpen, (UINT)hr };

    TcLog(Record);
    if (FAILED(hr))
        return hr;

    TcRuntimeAdapter = Args->pAdapterFuncs;
    TcRealAdapter = *Args->pAdapterFuncs;
    Args->pAdapterFuncs->pfnCalcPrivateDeviceSize = TcCalcPrivateDeviceSize;
    Args->pAdapterFuncs->pfnCreateDevice = TcCreateDevice;
    Args->pAdapterFuncs->pfnCloseAdapter = TcCloseAdapter;
    Args->pAdapterFuncs->pfnGetSupportedVersions = TcGetSupportedVersions;
    Args->pAdapterFuncs->pfnGetCaps = TcGetCaps;
    if (TcRealAdapter.pfnGetOptionalDDITables != NULL)
        Args->pAdapterFuncs->pfnGetOptionalDDITables = TcGetOptionalDDITables;
    if (TcRealAdapter.pfnFillDDITable != NULL)
        Args->pAdapterFuncs->pfnFillDDITable = TcFillDDITable;
    if (TcRealAdapter.pfnDestroyDevice != NULL)
        Args->pAdapterFuncs->pfnDestroyDevice = TcDestroyDevice;
    return hr;
}

static HMODULE TcWarp;

static
FARPROC
WINAPI
TcGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Proc = GetProcAddress(Module, Name);

    if (Module == TcWarp && getenv("WARP12CHECK_TRACE") != NULL)
    {
        if (((ULONG_PTR)Name >> 16) != 0)
            fprintf(stderr, "GetProcAddress %s -> %p\n", Name, Proc);
        else
            fprintf(stderr, "GetProcAddress #%u -> %p\n", (UINT)(ULONG_PTR)Name, Proc);
    }

    if (Module == TcWarp && ((ULONG_PTR)Name >> 16) != 0 && strcmp(Name, "OpenAdapter12") == 0 && Proc != NULL)
    {
        TcRealOpen = reinterpret_cast<PFND3D12DDI_OPENADAPTER>(Proc);
        return reinterpret_cast<FARPROC>(TcOpenAdapter12);
    }
    return Proc;
}

/* Points the runtime's GetProcAddress import at ours so the adapter DDI can be traced */
static
BOOLEAN
TcHookRuntime(
    _In_ HMODULE Core)
{
    BYTE *Base = reinterpret_cast<BYTE *>(Core);
    IMAGE_NT_HEADERS *Nt = reinterpret_cast<IMAGE_NT_HEADERS *>(Base + reinterpret_cast<IMAGE_DOS_HEADER *>(Base)->e_lfanew);
    IMAGE_DATA_DIRECTORY *Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR *Import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(Base + Directory->VirtualAddress);
    BOOLEAN Hooked = FALSE;

    for (; Import->Name != 0; Import++)
    {
        IMAGE_THUNK_DATA *Names = reinterpret_cast<IMAGE_THUNK_DATA *>(Base + Import->OriginalFirstThunk);
        IMAGE_THUNK_DATA *Slots = reinterpret_cast<IMAGE_THUNK_DATA *>(Base + Import->FirstThunk);

        for (; Names->u1.AddressOfData != 0; Names++, Slots++)
        {
            IMAGE_IMPORT_BY_NAME *ByName;
            DWORD Protect;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(Base + Names->u1.AddressOfData);
            if (strcmp(reinterpret_cast<char *>(ByName->Name), "GetProcAddress") != 0)
                continue;
            if (!VirtualProtect(&Slots->u1.Function, sizeof(Slots->u1.Function), PAGE_READWRITE, &Protect))
                continue;
            Slots->u1.Function = reinterpret_cast<ULONG_PTR>(TcGetProcAddress);
            VirtualProtect(&Slots->u1.Function, sizeof(Slots->u1.Function), Protect, &Protect);
            Hooked = TRUE;
        }
    }
    return Hooked;
}

static
VOID
TcAppend(
    _Inout_ TcResult *Result,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    TcAppendTo(&Result->Data, Data, Size);
}

/* Appends the status and, when it succeeded, the answer */
template <typename Value>
static
VOID
TcFeature(
    _In_ ID3D12Device *Device,
    _In_ D3D12_FEATURE Feature,
    _Inout_ Value *Data,
    _Inout_ TcResult *Result)
{
    HRESULT hr = Device->CheckFeatureSupport(Feature, Data, sizeof(*Data));
    UINT Record[3] = { (UINT)Feature, (UINT)sizeof(*Data), (UINT)hr };

    TcAppend(Result, Record, sizeof(Record));
    if (SUCCEEDED(hr))
        TcAppend(Result, Data, sizeof(*Data));
}

/* The driver calls the runtime made while the device came up */
static
HRESULT
SceneDdi(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    UNREFERENCED_PARAMETER(Device);

    EnterCriticalSection(&TcDdiLock);
    Result->Data = TcDdiLog;
    LeaveCriticalSection(&TcDdiLock);
    return S_OK;
}

/* Every feature block, then the driver calls they caused */
static
HRESULT
SceneFeatures(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_9_1, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2,
    };
    static const D3D_SHADER_MODEL Models[] =
    {
        D3D_SHADER_MODEL_5_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_8,
        D3D_SHADER_MODEL_6_9, D3D_HIGHEST_SHADER_MODEL, (D3D_SHADER_MODEL)0x6f,
    };
    static const D3D_ROOT_SIGNATURE_VERSION Signatures[] =
    {
        D3D_ROOT_SIGNATURE_VERSION_1_0, D3D_ROOT_SIGNATURE_VERSION_1_1, D3D_ROOT_SIGNATURE_VERSION_1_2,
        (D3D_ROOT_SIGNATURE_VERSION)7,
    };
    size_t LogStart;
    UINT Increment;

    EnterCriticalSection(&TcDdiLock);
    LogStart = TcDdiLog.size();
    LeaveCriticalSection(&TcDdiLock);

    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_ARCHITECTURE Data = {};
        TcFeature(Device, D3D12_FEATURE_ARCHITECTURE, &Data, Result);
    }
    for (UINT Count = 1; Count <= ARRAYSIZE(Levels); Count++)
    {
        D3D12_FEATURE_DATA_FEATURE_LEVELS Data = { Count, Levels };
        TcFeature(Device, D3D12_FEATURE_FEATURE_LEVELS, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT Data = {};
        TcFeature(Device, D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &Data, Result);
    }
    for (D3D_SHADER_MODEL Model : Models)
    {
        D3D12_FEATURE_DATA_SHADER_MODEL Data = { Model };
        TcFeature(Device, D3D12_FEATURE_SHADER_MODEL, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS1, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_SUPPORT Data = {};
        TcFeature(Device, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_SUPPORT, &Data, Result);
    }
    for (D3D_ROOT_SIGNATURE_VERSION Version : Signatures)
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE Data = { Version };
        TcFeature(Device, D3D12_FEATURE_ROOT_SIGNATURE, &Data, Result);
    }
    for (UINT Node = 0; Node < 2; Node++)
    {
        D3D12_FEATURE_DATA_ARCHITECTURE1 Data = { Node };
        TcFeature(Device, D3D12_FEATURE_ARCHITECTURE1, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS2 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS2, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_SHADER_CACHE Data = {};
        TcFeature(Device, D3D12_FEATURE_SHADER_CACHE, &Data, Result);
    }
    for (UINT Type = 0; Type < 4; Type++)
    {
        static const UINT Priorities[] = { D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_PRIORITY_HIGH,
                                           D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME };

        for (UINT Priority : Priorities)
        {
            D3D12_FEATURE_DATA_COMMAND_QUEUE_PRIORITY Data = { (D3D12_COMMAND_LIST_TYPE)Type, Priority };
            TcFeature(Device, D3D12_FEATURE_COMMAND_QUEUE_PRIORITY, &Data, Result);
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS3, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_EXISTING_HEAPS Data = {};
        TcFeature(Device, D3D12_FEATURE_EXISTING_HEAPS, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS4 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS4, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_SERIALIZATION Data = {};
        TcFeature(Device, D3D12_FEATURE_SERIALIZATION, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_CROSS_NODE Data = {};
        TcFeature(Device, D3D12_FEATURE_CROSS_NODE, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS5, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_DISPLAYABLE Data = {};
        TcFeature(Device, D3D12_FEATURE_DISPLAYABLE, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS6, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS7, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPE_COUNT Data = {};
        TcFeature(Device, D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, &Data, Result);
        if (SUCCEEDED(Device->CheckFeatureSupport(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPE_COUNT, &Data, sizeof(Data))) &&
            Data.Count > 0 && Data.Count <= 8)
        {
            GUID Types[8] = {};
            D3D12_FEATURE_DATA_PROTECTED_RESOURCE_SESSION_TYPES List = { 0, Data.Count, Types };
            HRESULT hr = Device->CheckFeatureSupport(D3D12_FEATURE_PROTECTED_RESOURCE_SESSION_TYPES, &List, sizeof(List));

            TcAppend(Result, &hr, sizeof(hr));
            TcAppend(Result, Types, sizeof(Types));
        }
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS8 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS8, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS9 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS9, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS10 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS10, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS11 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS11, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS12, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS13 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS13, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS14 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS14, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS15 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS15, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS16 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS16, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS17 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS17, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS18 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS18, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS19 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS19, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS20 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS20, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_PREDICATION Data = {};
        TcFeature(Device, D3D12_FEATURE_PREDICATION, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_HARDWARE_COPY Data = {};
        TcFeature(Device, D3D12_FEATURE_HARDWARE_COPY, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS21 Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_OPTIONS21, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_TIGHT_ALIGNMENT Data = {};
        TcFeature(Device, D3D12_FEATURE_D3D12_TIGHT_ALIGNMENT, &Data, Result);
    }
    {
        D3D12_FEATURE_DATA_BYTECODE_BYPASS_HASH_SUPPORTED Data = {};
        TcFeature(Device, D3D12_FEATURE_BYTECODE_BYPASS_HASH_SUPPORTED, &Data, Result);
    }

    Result->Data.push_back((BYTE)Device->GetNodeCount());
    for (UINT Type = 0; Type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; Type++)
    {
        Increment = Device->GetDescriptorHandleIncrementSize((D3D12_DESCRIPTOR_HEAP_TYPE)Type);
        TcAppend(Result, &Increment, sizeof(Increment));
    }

    EnterCriticalSection(&TcDdiLock);
    TcAppendTo(&Result->Data, TcDdiLog.data() + LogStart, TcDdiLog.size() - LogStart);
    LeaveCriticalSection(&TcDdiLock);
    return S_OK;
}

/* Format support, format info and multisample levels for every format */
static
HRESULT
SceneFormats(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const UINT Formats[] =
    {
        DXGI_FORMAT_A4B4G4R4_UNORM + 1, 0x7fffffff,
    };
    size_t LogStart;

    EnterCriticalSection(&TcDdiLock);
    LogStart = TcDdiLog.size();
    LeaveCriticalSection(&TcDdiLock);

    for (UINT Format = 0; Format <= DXGI_FORMAT_A4B4G4R4_UNORM + ARRAYSIZE(Formats); Format++)
    {
        DXGI_FORMAT Value = Format <= DXGI_FORMAT_A4B4G4R4_UNORM ? (DXGI_FORMAT)Format
                                                                  : (DXGI_FORMAT)Formats[Format - DXGI_FORMAT_A4B4G4R4_UNORM - 1];

        {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT Data = { Value };
            TcFeature(Device, D3D12_FEATURE_FORMAT_SUPPORT, &Data, Result);
        }
        {
            D3D12_FEATURE_DATA_FORMAT_INFO Data = { Value };
            TcFeature(Device, D3D12_FEATURE_FORMAT_INFO, &Data, Result);
        }
        for (UINT Samples = 1; Samples <= 32; Samples *= 2)
        {
            for (UINT Flags = 0; Flags < 2; Flags++)
            {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS Data = { Value, Samples,
                                                                       (D3D12_MULTISAMPLE_QUALITY_LEVEL_FLAGS)Flags };
                TcFeature(Device, D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &Data, Result);
            }
        }
    }

    EnterCriticalSection(&TcDdiLock);
    TcAppendTo(&Result->Data, TcDdiLog.data() + LogStart, TcDdiLog.size() - LogStart);
    LeaveCriticalSection(&TcDdiLock);
    return S_OK;
}

static const TcScene Scenes[] =
{
    { "ddi", SceneDdi },
    { "features", SceneFeatures },
    { "formats", SceneFormats },
};

static
HRESULT
TcRun(
    _In_ const TcScene &Scene,
    _Out_ TcResult *Result)
{
    IDXGIFactory4 *Factory = NULL;
    IDXGIAdapter *Adapter = NULL;
    ID3D12Device *Device = NULL;
    HRESULT hr;

    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory));
    if (FAILED(hr))
        return hr;
    hr = Factory->EnumWarpAdapter(IID_PPV_ARGS(&Adapter));
    if (SUCCEEDED(hr))
    {
        hr = D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device));
        if (SUCCEEDED(hr))
        {
            hr = Scene.Run(Device, Result);
            Device->Release();
        }
        Adapter->Release();
    }
    Factory->Release();
    return hr;
}

static
BOOLEAN
TcSaveResult(
    _In_z_ PCWSTR Path,
    _In_ HRESULT Status,
    _In_ const TcResult &Result)
{
    FILE *File;
    UINT64 Size = Result.Data.size();

    if (_wfopen_s(&File, Path, L"wb") != 0)
        return FALSE;
    fwrite(&Status, sizeof(Status), 1, File);
    fwrite(&Size, sizeof(Size), 1, File);
    fwrite(Result.Data.data(), 1, Result.Data.size(), File);
    fclose(File);
    return TRUE;
}

static
BOOLEAN
TcLoadResult(
    _In_z_ PCWSTR Path,
    _Out_ HRESULT *Status,
    _Out_ TcResult *Result)
{
    FILE *File;
    UINT64 Size = 0;
    BOOLEAN Complete;

    *Status = E_FAIL;
    if (_wfopen_s(&File, Path, L"rb") != 0)
        return FALSE;
    Complete = fread(Status, sizeof(*Status), 1, File) == 1 && fread(&Size, sizeof(Size), 1, File) == 1;
    if (Complete)
    {
        Result->Data.resize((SIZE_T)Size);
        Complete = fread(Result->Data.data(), 1, Result->Data.size(), File) == Result->Data.size();
    }
    fclose(File);
    return Complete;
}

/* Child: loads exactly one WARP under its module name, runs one scene and writes what it produced */
static
int
TcChild(
    _In_z_ PCWSTR ModulePath,
    _In_z_ PCWSTR SceneName,
    _In_z_ PCWSTR OutputPath)
{
    std::string Name(SceneName, SceneName + wcslen(SceneName));
    HMODULE Core;

    InitializeCriticalSection(&TcDdiLock);
    TcWarp = LoadLibraryExW(ModulePath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (TcWarp == NULL)
        return 3;
    Core = LoadLibraryExW(L"D3D12Core.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Core == NULL || !TcHookRuntime(Core))
        return 6;

    for (const TcScene &Scene : Scenes)
    {
        TcResult Result;
        HRESULT Status;

        if (_stricmp(Scene.Name, Name.c_str()) != 0)
            continue;
        Status = TcRun(Scene, &Result);

        /* A device that did not come up still shows how far the driver got */
        if (FAILED(Status))
            Result.Data = TcDdiLog;
        return TcSaveResult(OutputPath, Status, Result) ? 0 : 4;
    }
    return 5;
}

static
BOOLEAN
TcRunIsolated(
    _In_z_ PCWSTR ModulePath,
    _In_z_ PCSTR SceneName,
    _In_z_ PCWSTR OutputPath,
    _Out_ HRESULT *Status,
    _Out_ DWORD *ExitCode,
    _Out_ TcResult *Result)
{
    WCHAR Self[MAX_PATH];
    std::wstring Command;
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process = {};
    std::wstring Scene(SceneName, SceneName + strlen(SceneName));

    *ExitCode = (DWORD)-1;
    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    DeleteFileW(OutputPath);
    Command = L"\"" + std::wstring(Self) + L"\" /child \"" + ModulePath + L"\" " + Scene + L" \"" + OutputPath + L"\"";
    if (!CreateProcessW(NULL, &Command[0], NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
        return FALSE;
    WaitForSingleObject(Process.hProcess, INFINITE);
    GetExitCodeProcess(Process.hProcess, ExitCode);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return TcLoadResult(OutputPath, Status, Result);
}

static
VOID
TcDumpResult(
    _In_z_ PCSTR Path,
    _In_ const TcResult &Result)
{
    FILE *File;

    if (fopen_s(&File, Path, "wb") != 0)
        return;
    fwrite(Result.Data.data(), 1, Result.Data.size(), File);
    fclose(File);
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t **argv)
{
    WCHAR SystemPath[MAX_PATH];
    WCHAR OursPath[MAX_PATH];
    UINT Failures = 0;

    if (argc == 5 && wcscmp(argv[1], L"/child") == 0)
        return TcChild(argv[2], argv[3], argv[4]);

    if (argc < 2)
    {
        wprintf(L"usage: warp12check <our d3d10warp.dll> [scene]\n");
        return 2;
    }

    GetSystemDirectoryW(SystemPath, ARRAYSIZE(SystemPath));
    wcscat_s(SystemPath, L"\\d3d10warp.dll");
    if (GetFullPathNameW(argv[1], ARRAYSIZE(OursPath), OursPath, NULL) == 0 || _wcsicmp(SystemPath, OursPath) == 0)
    {
        wprintf(L"give the path of our WARP, not the system one\n");
        return 2;
    }

    for (const TcScene &Scene : Scenes)
    {
        TcResult Expected;
        TcResult Actual;
        HRESULT SystemHr;
        HRESULT OurHr;
        DWORD SystemExit = 0;
        DWORD OurExit = 0;
        SIZE_T Differ = 0;
        SIZE_T First = 0;
        char Name[96];

        if (argc > 2 && _stricmp(Scene.Name, std::string(argv[2], argv[2] + wcslen(argv[2])).c_str()) != 0)
            continue;

        if (!TcRunIsolated(SystemPath, Scene.Name, L"warp12check_system.out", &SystemHr, &SystemExit, &Expected) ||
            !TcRunIsolated(OursPath, Scene.Name, L"warp12check_ours.out", &OurHr, &OurExit, &Actual))
        {
            printf("%-8s FAIL a child process did not report back (exit %08lX, %08lX)\n", Scene.Name, SystemExit, OurExit);
            Failures++;
            continue;
        }

        sprintf_s(Name, "warp12check_%s_system.bin", Scene.Name);
        TcDumpResult(Name, Expected);
        sprintf_s(Name, "warp12check_%s_ours.bin", Scene.Name);
        TcDumpResult(Name, Actual);

        for (SIZE_T Index = 0; Index < Expected.Data.size() || Index < Actual.Data.size(); Index++)
        {
            if (Index < Expected.Data.size() && Index < Actual.Data.size() && Expected.Data[Index] == Actual.Data[Index])
                continue;
            if (Differ == 0)
                First = Index;
            Differ++;
        }

        if (SystemHr != OurHr || Differ != 0)
        {
            printf("%-8s FAIL hr %08lX vs %08lX, %zu vs %zu bytes, %zu differ from byte %zu\n", Scene.Name, SystemHr, OurHr,
                   Expected.Data.size(), Actual.Data.size(), Differ, First);
            Failures++;
            continue;
        }
        printf("%-8s ok (%zu bytes)\n", Scene.Name, Expected.Data.size());
    }

    printf("%u failed\n", Failures);
    return Failures ? 1 : 0;
}
