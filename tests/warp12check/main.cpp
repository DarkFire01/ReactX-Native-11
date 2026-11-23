/*
 * PROJECT:     ReactX Basic tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs Direct3D 12 scenes on the system WARP and on ours and compares the results
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <winternl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3d10umddi.h>
#include <d3d12umddi.h>
#include <dxgi1_6.h>
#include <dwmapi.h>
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
    TcTagAllocate,
};

static PFND3D12DDI_OPENADAPTER TcRealOpen;
static D3D12DDI_ADAPTERFUNCS TcRealAdapter;
static D3D12DDI_ADAPTERFUNCS *TcRuntimeAdapter;

/* Device DDI slots are traced as 0 to 127, extended feature slots from 200; slots returning more than two registers go unwrapped */
static constexpr UINT TcDeviceSlots = 128;
static constexpr UINT TcFeatureSlotBase = 200;
static constexpr UINT TcTracedSlots = 256;
static constexpr UINT TcProgramIdentifierSlot = 122;
static constexpr UINT TcAllocationInfoSlot = 75;
static constexpr UINT TcExistingAllocationInfoSlot = 77;

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
    TcPair Value;

    TcLog(Record);
    Value = TcRealDevice[Slot](A0, A1, A2, A3, A4, A5, A6, A7);

    /* Allocation info answers decide what the runtime does next, so they are part of the traffic */
    if constexpr (Slot == TcAllocationInfoSlot)
        TcLogBytes(reinterpret_cast<const VOID *>(A5), sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO_0022));
    if constexpr (Slot == TcExistingAllocationInfoSlot)
        TcLogBytes(reinterpret_cast<const VOID *>(A2), sizeof(D3D12DDI_RESOURCE_ALLOCATION_INFO_0022));
    return Value;
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

static PFND3D12DDI_ALLOCATE_CB_0022 TcRealAllocate;

/* Kernel allocations: what the driver tells the kernel driver about each one */
static
HRESULT
APIENTRY
TcAllocate(
    _In_ D3D12DDI_HRTDEVICE Device,
    _Inout_ D3D12DDICB_ALLOCATE_0022 *Args)
{
    HRESULT hr = TcRealAllocate(Device, Args);
    UINT Record[5] = { TcTagAllocate, (UINT)hr, Args->NumAllocations, Args->PrivateDriverDataSize, Args->hResource != NULL };

    TcLog(Record);
    for (UINT Index = 0; Index < Args->NumAllocations; Index++)
    {
        const D3D12DDI_ALLOCATION_INFO_0022 &Info = Args->pAllocationInfo[Index];
        UINT Header[3] = { Info.PrivateDriverDataSize, Info.VidPnSourceId, (UINT)Info.Flags };

        TcLog(Header);
        if (Info.pPrivateDriverData != NULL)
            TcLogBytes(Info.pPrivateDriverData, Info.PrivateDriverDataSize);
    }
    return hr;
}

static
HRESULT
APIENTRY
TcCreateDevice(
    _In_ D3D12DDI_HADAPTER Adapter,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args)
{
    HRESULT hr;

    /* The runtime's callback table is patched in place; it is larger than the SDK's view of it */
    if (Args->p12UMCallbacks_0022 != NULL && Args->p12UMCallbacks_0022->pfnAllocateCb != TcAllocate)
    {
        PFND3D12DDI_ALLOCATE_CB_0022 *Slot = const_cast<PFND3D12DDI_ALLOCATE_CB_0022 *>(&Args->p12UMCallbacks_0022->pfnAllocateCb);
        DWORD Protect;

        if (VirtualProtect(Slot, sizeof(*Slot), PAGE_READWRITE, &Protect))
        {
            TcRealAllocate = *Slot;
            *Slot = TcAllocate;
            VirtualProtect(Slot, sizeof(*Slot), Protect, &Protect);
        }
    }
    hr = TcRealAdapter.pfnCreateDevice(Adapter, Args);
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

/* A queue, an allocator, a list and a fence; Submit runs the list and waits for it */
class TcQueue
{
public:
    ~TcQueue()
    {
        if (Event != NULL)
            CloseHandle(Event);
        if (Fence)
            Fence->Release();
        if (List)
            List->Release();
        if (Allocator)
            Allocator->Release();
        if (Queue)
            Queue->Release();
    }

    HRESULT
    Create(
        _In_ ID3D12Device *Device,
        _In_ D3D12_COMMAND_LIST_TYPE Type)
    {
        D3D12_COMMAND_QUEUE_DESC Desc = {};
        HRESULT hr;

        Desc.Type = Type;
        hr = Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Queue));
        if (SUCCEEDED(hr))
            hr = Device->CreateCommandAllocator(Type, IID_PPV_ARGS(&Allocator));
        if (SUCCEEDED(hr))
            hr = Device->CreateCommandList(0, Type, Allocator, NULL, IID_PPV_ARGS(&List));
        if (SUCCEEDED(hr))
            hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
        Event = CreateEventW(NULL, FALSE, FALSE, NULL);
        return hr;
    }

    /* Closes and runs the list, waits for it, and opens it again */
    HRESULT
    Submit(VOID)
    {
        ID3D12CommandList *Lists[] = { List };
        HRESULT hr = List->Close();

        if (FAILED(hr))
            return hr;
        Queue->ExecuteCommandLists(1, Lists);
        hr = Wait();
        if (SUCCEEDED(hr))
            hr = Allocator->Reset();
        if (SUCCEEDED(hr))
            hr = List->Reset(Allocator, NULL);
        return hr;
    }

    HRESULT
    Wait(VOID)
    {
        HRESULT hr;

        Value += 1;
        hr = Queue->Signal(Fence, Value);
        if (FAILED(hr))
            return hr;
        if (Fence->GetCompletedValue() < Value)
        {
            hr = Fence->SetEventOnCompletion(Value, Event);
            if (SUCCEEDED(hr) && WaitForSingleObject(Event, 10000) != WAIT_OBJECT_0)
                hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        }
        return hr;
    }

    ID3D12CommandQueue *Queue = NULL;
    ID3D12CommandAllocator *Allocator = NULL;
    ID3D12GraphicsCommandList *List = NULL;
    ID3D12Fence *Fence = NULL;
    HANDLE Event = NULL;
    UINT64 Value = 0;
};

static
ID3D12Resource *
TcBuffer(
    _In_ ID3D12Device *Device,
    _In_ D3D12_HEAP_TYPE Type,
    _In_ UINT64 Size,
    _In_ D3D12_RESOURCE_FLAGS Flags)
{
    D3D12_HEAP_PROPERTIES Heap = { Type };
    D3D12_RESOURCE_DESC Desc = {};
    ID3D12Resource *Resource = NULL;
    D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Size;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Desc.Flags = Flags;
    if (Type == D3D12_HEAP_TYPE_UPLOAD)
        State = D3D12_RESOURCE_STATE_GENERIC_READ;
    if (Type == D3D12_HEAP_TYPE_READBACK)
        State = D3D12_RESOURCE_STATE_COPY_DEST;
    if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, NULL, IID_PPV_ARGS(&Resource))))
        return NULL;
    return Resource;
}

static
ID3D12Resource *
TcTexture(
    _In_ ID3D12Device *Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT16 Slices,
    _In_ UINT16 Mips,
    _In_ D3D12_RESOURCE_FLAGS Flags,
    _In_ D3D12_RESOURCE_STATES State)
{
    D3D12_HEAP_PROPERTIES Heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC Desc = {};
    ID3D12Resource *Resource = NULL;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.DepthOrArraySize = Slices;
    Desc.MipLevels = Mips;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = Flags;
    if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, NULL, IID_PPV_ARGS(&Resource))))
        return NULL;
    return Resource;
}

/* Fills an upload buffer with a byte pattern that depends on Seed */
static
HRESULT
TcFill(
    _In_ ID3D12Resource *Buffer,
    _In_ SIZE_T Size,
    _In_ UINT Seed)
{
    BYTE *Data = NULL;
    HRESULT hr = Buffer->Map(0, NULL, reinterpret_cast<VOID **>(&Data));

    if (FAILED(hr))
        return hr;
    for (SIZE_T Index = 0; Index < Size; Index++)
        Data[Index] = (BYTE)(Index * 7 + Seed + (Index >> 8) * 13);
    Buffer->Unmap(0, NULL);
    return S_OK;
}

static
VOID
TcReadback(
    _In_ ID3D12Resource *Buffer,
    _In_ SIZE_T Size,
    _Inout_ TcResult *Result)
{
    D3D12_RANGE Range = { 0, Size };
    BYTE *Data = NULL;
    HRESULT hr = Buffer->Map(0, &Range, reinterpret_cast<VOID **>(&Data));

    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return;
    TcAppend(Result, Data, Size);
    Buffer->Unmap(0, NULL);
}

/* Copies every subresource of a texture into a readback buffer through its footprints, then logs the bytes */
static
HRESULT
TcReadTexture(
    _In_ ID3D12Device *Device,
    _In_ TcQueue *Queue,
    _In_ ID3D12Resource *Texture,
    _In_ UINT Subresources,
    _Inout_ TcResult *Result)
{
    D3D12_RESOURCE_DESC Desc = Texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[16];
    UINT64 Total = 0;
    ID3D12Resource *Readback;
    HRESULT hr;

    if (Subresources > ARRAYSIZE(Layouts))
        return E_INVALIDARG;
    Device->GetCopyableFootprints(&Desc, 0, Subresources, 0, Layouts, NULL, NULL, &Total);
    Readback = TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, Total, D3D12_RESOURCE_FLAG_NONE);
    if (Readback == NULL)
        return E_OUTOFMEMORY;
    for (UINT Index = 0; Index < Subresources; Index++)
    {
        D3D12_TEXTURE_COPY_LOCATION To = {};
        D3D12_TEXTURE_COPY_LOCATION From = {};

        To.pResource = Readback;
        To.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        To.PlacedFootprint = Layouts[Index];
        From.pResource = Texture;
        From.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        From.SubresourceIndex = Index;
        Queue->List->CopyTextureRegion(&To, 0, 0, 0, &From, NULL);
    }
    hr = Queue->Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        TcReadback(Readback, (SIZE_T)Total, Result);
    Readback->Release();
    return hr;
}

static
VOID
TcLogSince(
    _In_ SIZE_T Start,
    _Inout_ TcResult *Result)
{
    EnterCriticalSection(&TcDdiLock);
    TcAppendTo(&Result->Data, TcDdiLog.data() + Start, TcDdiLog.size() - Start);
    LeaveCriticalSection(&TcDdiLock);
}

static
SIZE_T
TcLogMark(VOID)
{
    SIZE_T Size;

    EnterCriticalSection(&TcDdiLock);
    Size = TcDdiLog.size();
    LeaveCriticalSection(&TcDdiLock);
    return Size;
}

/* Allocation info, GPU addresses, buffer and texture copies through committed and placed resources */
static
HRESULT
SceneCopy(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const struct { D3D12_RESOURCE_DIMENSION Dimension; DXGI_FORMAT Format; UINT Width; UINT Height; UINT16 Depth; UINT16 Mips; D3D12_RESOURCE_FLAGS Flags; UINT Samples; } Descs[] =
    {
        { D3D12_RESOURCE_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, 1, 1, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_BUFFER, DXGI_FORMAT_UNKNOWN, 65537, 1, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE1D, DXGI_FORMAT_R8G8B8A8_UNORM, 100, 1, 3, 0, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R16G16B16A16_FLOAT, 333, 77, 6, 0, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_BC1_UNORM, 256, 256, 1, 0, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D32_FLOAT, 128, 128, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D24_UNORM_S8_UINT, 17, 9, 2, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_NV12, 64, 64, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R32_FLOAT, 32, 16, 8, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8_UNORM, 4096, 4096, 1, 0, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R32_FLOAT, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D32_FLOAT, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_D16_UNORM, 33, 7, 3, 3, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R16_UNORM, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R32_TYPELESS, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R8G8B8A8_UNORM, 17, 9, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 4 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_NV12, 34, 18, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_YUY2, 34, 18, 1, 1, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE2D, DXGI_FORMAT_R9G9B9E5_SHAREDEXP, 17, 9, 1, 0, D3D12_RESOURCE_FLAG_NONE, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE3D, DXGI_FORMAT_R8G8B8A8_UNORM, 17, 9, 5, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1 },
        { D3D12_RESOURCE_DIMENSION_TEXTURE1D, DXGI_FORMAT_R8G8B8A8_UNORM, 17, 1, 3, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, 1 },
    };
    SIZE_T LogStart = TcLogMark();
    TcQueue Queue;
    ID3D12Resource *Upload = NULL;
    ID3D12Resource *Middle = NULL;
    ID3D12Resource *Readback = NULL;
    ID3D12Resource *Texture = NULL;
    ID3D12Resource *Copy = NULL;
    ID3D12Heap *Heap = NULL;
    ID3D12Resource *Placed[2] = {};
    HRESULT hr;

    for (const auto &Item : Descs)
    {
        D3D12_RESOURCE_DESC Desc = {};
        D3D12_RESOURCE_ALLOCATION_INFO Info;

        Desc.Dimension = Item.Dimension;
        Desc.Width = Item.Width;
        Desc.Height = Item.Height;
        Desc.DepthOrArraySize = Item.Depth;
        Desc.MipLevels = Item.Mips;
        Desc.Format = Item.Format;
        Desc.SampleDesc.Count = Item.Samples;
        Desc.Layout = Item.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN;
        Desc.Flags = Item.Flags;
        Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
        TcAppend(Result, &Info, sizeof(Info));
        Desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
        Info = Device->GetResourceAllocationInfo(0, 1, &Desc);
        TcAppend(Result, &Info, sizeof(Info));
    }

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;

    Upload = TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 65536, D3D12_RESOURCE_FLAG_NONE);
    Middle = TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 65536, D3D12_RESOURCE_FLAG_NONE);
    Readback = TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, 65536, D3D12_RESOURCE_FLAG_NONE);
    Texture = TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 37, 21, 2, 3, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    Copy = TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 37, 21, 2, 3, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (Upload == NULL || Middle == NULL || Readback == NULL || Texture == NULL || Copy == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto Done;
    }

    /* Addresses as offsets from the first buffer, which keeps them independent of the process */
    {
        UINT64 Base = Upload->GetGPUVirtualAddress();
        UINT64 Offsets[3] = { Middle->GetGPUVirtualAddress() - Base, Readback->GetGPUVirtualAddress() - Base,
                              Texture->GetGPUVirtualAddress() };

        TcAppend(Result, Offsets, sizeof(Offsets));
    }

    hr = TcFill(Upload, 65536, 3);
    if (FAILED(hr))
        goto Done;

    /* Buffer to buffer in pieces, overlapping offsets and a whole resource copy */
    Queue.List->CopyBufferRegion(Middle, 0, Upload, 0, 65536);
    Queue.List->CopyBufferRegion(Middle, 100, Upload, 5000, 1234);
    Queue.List->CopyBufferRegion(Middle, 40000, Upload, 1, 3);
    {
        D3D12_RESOURCE_BARRIER Barrier = {};

        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Middle;
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        Queue.List->ResourceBarrier(1, &Barrier);
    }
    Queue.List->CopyResource(Readback, Middle);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        goto Done;
    TcReadback(Readback, 65536, Result);

    /* Upload footprints into every subresource, then a box from one texture to another */
    {
        D3D12_RESOURCE_DESC Desc = Texture->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT Layouts[6] = {};
        UINT Rows[6];
        UINT64 RowBytes[6];
        UINT64 Total;

        Device->GetCopyableFootprints(&Desc, 0, 6, 0, Layouts, Rows, RowBytes, &Total);
        TcAppend(Result, Layouts, sizeof(Layouts));
        TcAppend(Result, &Total, sizeof(Total));
        for (UINT Index = 0; Index < 6; Index++)
        {
            D3D12_TEXTURE_COPY_LOCATION To = {};
            D3D12_TEXTURE_COPY_LOCATION From = {};

            To.pResource = Texture;
            To.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            To.SubresourceIndex = Index;
            From.pResource = Upload;
            From.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            From.PlacedFootprint = Layouts[Index];
            Queue.List->CopyTextureRegion(&To, 0, 0, 0, &From, NULL);
        }
        {
            D3D12_TEXTURE_COPY_LOCATION To = {};
            D3D12_TEXTURE_COPY_LOCATION From = {};
            D3D12_BOX Box = { 3, 2, 0, 30, 19, 1 };

            Queue.List->CopyResource(Copy, Texture);
            To.pResource = Copy;
            To.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            To.SubresourceIndex = 3;
            From.pResource = Texture;
            From.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            From.SubresourceIndex = 0;
            Queue.List->CopyTextureRegion(&To, 5, 1, 0, &From, &Box);
        }
        hr = Queue.Submit();
        TcAppend(Result, &hr, sizeof(hr));
        if (SUCCEEDED(hr))
            hr = TcReadTexture(Device, &Queue, Copy, 6, Result);
        if (FAILED(hr))
            goto Done;
    }

    /* Two buffers placed over one heap see each other's bytes */
    {
        D3D12_HEAP_DESC HeapDesc = {};
        D3D12_RESOURCE_DESC Desc = {};

        HeapDesc.SizeInBytes = 4 * 65536;
        HeapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
        HeapDesc.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
        hr = Device->CreateHeap(&HeapDesc, IID_PPV_ARGS(&Heap));
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            goto Done;
        Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Desc.Width = 2 * 65536;
        Desc.Height = 1;
        Desc.DepthOrArraySize = 1;
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;
        Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = Device->CreatePlacedResource(Heap, 0, &Desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&Placed[0]));
        if (SUCCEEDED(hr))
            hr = Device->CreatePlacedResource(Heap, 65536, &Desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&Placed[1]));
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            goto Done;
        {
            UINT64 Offset = Placed[1]->GetGPUVirtualAddress() - Placed[0]->GetGPUVirtualAddress();

            TcAppend(Result, &Offset, sizeof(Offset));
        }
        Queue.List->CopyBufferRegion(Placed[1], 0, Upload, 0, 65536);
        Queue.List->CopyBufferRegion(Readback, 0, Placed[0], 65536, 65536);
        hr = Queue.Submit();
        TcAppend(Result, &hr, sizeof(hr));
        if (FAILED(hr))
            goto Done;
        TcReadback(Readback, 65536, Result);
    }

    /* A reserved resource comes up without memory */
    {
        D3D12_RESOURCE_DESC Desc = {};
        ID3D12Resource *Reserved = NULL;

        Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        Desc.Width = 256;
        Desc.Height = 256;
        Desc.DepthOrArraySize = 1;
        Desc.MipLevels = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;
        hr = Device->CreateReservedResource(&Desc, D3D12_RESOURCE_STATE_COMMON, NULL, IID_PPV_ARGS(&Reserved));
        TcAppend(Result, &hr, sizeof(hr));
        if (Reserved)
            Reserved->Release();
        hr = S_OK;
    }

Done:
    for (ID3D12Resource *Item : Placed)
    {
        if (Item)
            Item->Release();
    }
    if (Heap)
        Heap->Release();
    if (Copy)
        Copy->Release();
    if (Texture)
        Texture->Release();
    if (Readback)
        Readback->Release();
    if (Middle)
        Middle->Release();
    if (Upload)
        Upload->Release();
    TcLogSince(LogStart, Result);
    return hr;
}

/* Render target, depth stencil and unordered access clears, whole and in rectangles */
static
HRESULT
SceneClear(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const DXGI_FORMAT Targets[] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R11G11B10_FLOAT,
    };
    static const DXGI_FORMAT Depths[] = { DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D32_FLOAT_S8X24_UINT };
    static const FLOAT Color[4] = { 0.25f, -0.5f, 0.75f, 1.5f };
    static const FLOAT Color2[4] = { 0.125f, 0.333f, 2.0f, 0.5f };
    static const D3D12_RECT Rects[2] = { { 1, 2, 9, 5 }, { 7, 6, 14, 11 } };
    SIZE_T LogStart = TcLogMark();
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    ID3D12DescriptorHeap *Rtvs = NULL;
    ID3D12DescriptorHeap *Dsvs = NULL;
    ID3D12DescriptorHeap *Visible = NULL;
    ID3D12DescriptorHeap *Hidden = NULL;
    TcQueue Queue;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 4;
    if (SUCCEEDED(hr))
        hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Rtvs));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    if (SUCCEEDED(hr))
        hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Dsvs));
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    if (SUCCEEDED(hr))
        hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Hidden));
    HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (SUCCEEDED(hr))
        hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Visible));
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        goto Done;

    for (DXGI_FORMAT Format : Targets)
    {
        ID3D12Resource *Target = TcTexture(Device, Format, 16, 12, 2, 2, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE Second = { Rtv.ptr + Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV) };
        D3D12_RENDER_TARGET_VIEW_DESC Desc = {};
        D3D12_RESOURCE_BARRIER Barrier = {};

        if (Target == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto Done;
        }
        Device->CreateRenderTargetView(Target, NULL, Rtv);
        Desc.Format = Format;
        Desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        Desc.Texture2DArray.MipSlice = 1;
        Desc.Texture2DArray.FirstArraySlice = 1;
        Desc.Texture2DArray.ArraySize = 1;
        Device->CreateRenderTargetView(Target, &Desc, Second);
        Queue.List->ClearRenderTargetView(Rtv, Color, 0, NULL);
        Queue.List->ClearRenderTargetView(Rtv, Color2, 2, Rects);
        Queue.List->ClearRenderTargetView(Second, Color2, 0, NULL);
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Target;
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        Queue.List->ResourceBarrier(1, &Barrier);
        hr = TcReadTexture(Device, &Queue, Target, 4, Result);
        Target->Release();
        if (FAILED(hr))
            goto Done;
    }

    for (DXGI_FORMAT Format : Depths)
    {
        ID3D12Resource *Target = TcTexture(Device, Format, 16, 12, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        D3D12_CPU_DESCRIPTOR_HANDLE Dsv = Dsvs->GetCPUDescriptorHandleForHeapStart();
        D3D12_RESOURCE_BARRIER Barrier = {};
        UINT Planes = (Format == DXGI_FORMAT_D24_UNORM_S8_UINT || Format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) ? 2 : 1;

        if (Target == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto Done;
        }
        Device->CreateDepthStencilView(Target, NULL, Dsv);
        Queue.List->ClearDepthStencilView(Dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.375f, 0x5A, 0, NULL);
        Queue.List->ClearDepthStencilView(Dsv, D3D12_CLEAR_FLAG_DEPTH, 0.875f, 0, 1, Rects);
        if (Planes == 2)
            Queue.List->ClearDepthStencilView(Dsv, D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0xC3, 1, Rects + 1);
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = Target;
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        Queue.List->ResourceBarrier(1, &Barrier);
        hr = TcReadTexture(Device, &Queue, Target, Planes, Result);
        Target->Release();
        if (FAILED(hr))
            goto Done;
    }

    /* Unordered access clears need the view in a shader visible heap and in a CPU only one */
    {
        static const UINT Values[4] = { 0x11223344, 5, 6, 7 };
        static const FLOAT Floats[4] = { 0.5f, 0.25f, -1.0f, 3.0f };
        ID3D12Resource *Buffer = TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 4096, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ID3D12Resource *Texture = TcTexture(Device, DXGI_FORMAT_R16G16B16A16_FLOAT, 16, 12, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        ID3D12Resource *Readback = TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, 4096, D3D12_RESOURCE_FLAG_NONE);
        UINT Increment = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_UNORDERED_ACCESS_VIEW_DESC Desc = {};
        D3D12_CPU_DESCRIPTOR_HANDLE Cpu = Hidden->GetCPUDescriptorHandleForHeapStart();
        D3D12_CPU_DESCRIPTOR_HANDLE VisibleCpu = Visible->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE Gpu = Visible->GetGPUDescriptorHandleForHeapStart();
        ID3D12DescriptorHeap *Heaps[] = { Visible };
        D3D12_RESOURCE_BARRIER Barrier = {};

        if (Buffer == NULL || Texture == NULL || Readback == NULL)
        {
            hr = E_OUTOFMEMORY;
        }
        else
        {
            Desc.Format = DXGI_FORMAT_R32_TYPELESS;
            Desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            Desc.Buffer.NumElements = 1024;
            Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            Device->CreateUnorderedAccessView(Buffer, NULL, &Desc, Cpu);
            Desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
            Desc.Buffer.FirstElement = 512;
            Desc.Buffer.NumElements = 100;
            Desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            Device->CreateUnorderedAccessView(Buffer, NULL, &Desc, { Cpu.ptr + Increment });
            Device->CreateUnorderedAccessView(Texture, NULL, NULL, { Cpu.ptr + 2 * Increment });
            Device->CopyDescriptorsSimple(3, VisibleCpu, Cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            Queue.List->SetDescriptorHeaps(1, Heaps);
            Queue.List->ClearUnorderedAccessViewUint(Gpu, Cpu, Buffer, Values, 0, NULL);
            Queue.List->ClearUnorderedAccessViewUint({ Gpu.ptr + Increment }, { Cpu.ptr + Increment }, Buffer, Values, 0, NULL);
            Queue.List->ClearUnorderedAccessViewFloat({ Gpu.ptr + 2 * Increment }, { Cpu.ptr + 2 * Increment }, Texture, Floats, 0, NULL);
            Queue.List->ClearUnorderedAccessViewFloat({ Gpu.ptr + 2 * Increment }, { Cpu.ptr + 2 * Increment }, Texture, Color, 1, Rects);
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            Barrier.Transition.pResource = Buffer;
            Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            Queue.List->ResourceBarrier(1, &Barrier);
            Queue.List->CopyResource(Readback, Buffer);
            Barrier.Transition.pResource = Texture;
            Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            Queue.List->ResourceBarrier(1, &Barrier);
            hr = Queue.Submit();
            TcAppend(Result, &hr, sizeof(hr));
            if (SUCCEEDED(hr))
            {
                TcReadback(Readback, 4096, Result);
                hr = TcReadTexture(Device, &Queue, Texture, 1, Result);
            }
        }
        if (Readback)
            Readback->Release();
        if (Texture)
            Texture->Release();
        if (Buffer)
            Buffer->Release();
    }

Done:
    if (Visible)
        Visible->Release();
    if (Hidden)
        Hidden->Release();
    if (Dsvs)
        Dsvs->Release();
    if (Rtvs)
        Rtvs->Release();
    TcLogSince(LogStart, Result);
    return hr;
}

/* Lists run in submission order across queues that wait on each other's fences */
static
HRESULT
SceneFence(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    SIZE_T LogStart = TcLogMark();
    TcQueue Direct;
    TcQueue Copy;
    ID3D12Resource *Upload = NULL;
    ID3D12Resource *Middle = NULL;
    ID3D12Resource *Readback = NULL;
    ID3D12CommandAllocator *SecondAllocator = NULL;
    ID3D12GraphicsCommandList *Second = NULL;
    ID3D12Fence *Shared = NULL;
    HRESULT hr;

    hr = Direct.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Copy.Create(Device, D3D12_COMMAND_LIST_TYPE_COPY);
    if (SUCCEEDED(hr))
        hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&SecondAllocator));
    if (SUCCEEDED(hr))
        hr = Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, SecondAllocator, NULL, IID_PPV_ARGS(&Second));
    if (SUCCEEDED(hr))
        hr = Device->CreateFence(5, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Shared));
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        goto Done;
    Upload = TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 8192, D3D12_RESOURCE_FLAG_NONE);
    Middle = TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 8192, D3D12_RESOURCE_FLAG_NONE);
    Readback = TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, 8192, D3D12_RESOURCE_FLAG_NONE);
    if (Upload == NULL || Middle == NULL || Readback == NULL)
    {
        hr = E_OUTOFMEMORY;
        goto Done;
    }
    hr = TcFill(Upload, 8192, 11);
    if (FAILED(hr))
        goto Done;

    {
        UINT64 Value = Shared->GetCompletedValue();

        TcAppend(Result, &Value, sizeof(Value));
    }

    /* The copy queue waits for the direct queue, which waits on a value the CPU signals */
    Direct.Queue->Wait(Shared, 6);
    Direct.List->CopyBufferRegion(Middle, 0, Upload, 0, 4096);
    hr = Direct.List->Close();
    Second->CopyBufferRegion(Middle, 4096, Upload, 100, 4096);
    if (SUCCEEDED(hr))
        hr = Second->Close();
    if (SUCCEEDED(hr))
    {
        ID3D12CommandList *Lists[] = { Direct.List, Second };
        UINT64 Value;

        Direct.Queue->ExecuteCommandLists(2, Lists);
        Direct.Queue->Signal(Shared, 7);
        Copy.Queue->Wait(Shared, 7);
        Copy.List->CopyResource(Readback, Middle);
        hr = Copy.List->Close();
        if (SUCCEEDED(hr))
        {
            ID3D12CommandList *CopyLists[] = { Copy.List };

            Copy.Queue->ExecuteCommandLists(1, CopyLists);
        }
        Value = Shared->GetCompletedValue();
        TcAppend(Result, &Value, sizeof(Value));
        hr = Shared->Signal(6);
        if (SUCCEEDED(hr))
            hr = Copy.Wait();
        Value = Shared->GetCompletedValue();
        TcAppend(Result, &Value, sizeof(Value));
    }
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        TcReadback(Readback, 8192, Result);

    /* Reusing the allocator after the queue is done with it */
    if (SUCCEEDED(hr))
        hr = Direct.Allocator->Reset();
    if (SUCCEEDED(hr))
        hr = Direct.List->Reset(Direct.Allocator, NULL);
    if (SUCCEEDED(hr))
    {
        Direct.List->CopyBufferRegion(Readback, 0, Upload, 4096, 4096);
        hr = Direct.Submit();
    }
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        TcReadback(Readback, 8192, Result);

Done:
    if (Shared)
        Shared->Release();
    if (Second)
        Second->Release();
    if (SecondAllocator)
        SecondAllocator->Release();
    if (Readback)
        Readback->Release();
    if (Middle)
        Middle->Release();
    if (Upload)
        Upload->Release();
    TcLogSince(LogStart, Result);
    return hr;
}

/* A windowed flip swap chain: back buffers cleared on the GPU, presented, then read from the window through GDI */
static
HRESULT
ScenePresent(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const DXGI_FORMAT Formats[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM };
    static const FLOAT Colors[3][4] = { { 1.0f, 0.5f, 0.25f, 1.0f }, { 0.0f, 0.75f, 0.5f, 1.0f }, { 0.2f, 0.4f, 0.8f, 1.0f } };
    SIZE_T LogStart = TcLogMark();
    WNDCLASSW Class = {};
    IDXGIFactory4 *Factory = NULL;
    ID3D12DescriptorHeap *Rtvs = NULL;
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    TcQueue Queue;
    HWND Window;
    HRESULT hr;

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"Warp12CheckPresent";
    RegisterClassW(&Class);
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, Class.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 0, 0, 64, 64,
                             NULL, NULL, Class.hInstance, NULL);
    if (Window == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory));
    if (SUCCEEDED(hr))
        hr = Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Rtvs));
    TcAppend(Result, &hr, sizeof(hr));

    for (UINT Case = 0; Case < ARRAYSIZE(Formats) && SUCCEEDED(hr); Case++)
    {
        DXGI_SWAP_CHAIN_DESC1 Desc = {};
        IDXGISwapChain1 *Chain = NULL;
        IDXGISwapChain3 *Chain3 = NULL;
        BITMAPINFO Info = {};
        VOID *Bits = NULL;
        HDC WindowDc;
        HDC MemoryDc;
        HBITMAP Bitmap;
        MSG Message;

        Desc.Width = 64;
        Desc.Height = 64;
        Desc.Format = Formats[Case];
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 2;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        hr = Factory->CreateSwapChainForHwnd(Queue.Queue, Window, &Desc, NULL, NULL, &Chain);
        if (SUCCEEDED(hr))
            hr = Chain->QueryInterface(IID_PPV_ARGS(&Chain3));
        for (UINT Frame = 0; Frame < 2 && SUCCEEDED(hr); Frame++)
        {
            ID3D12Resource *Back = NULL;
            D3D12_RESOURCE_BARRIER Barrier = {};
            D3D12_CPU_DESCRIPTOR_HANDLE Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();

            hr = Chain3->GetBuffer(Chain3->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&Back));
            if (FAILED(hr))
                break;
            Device->CreateRenderTargetView(Back, NULL, Rtv);
            Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            Barrier.Transition.pResource = Back;
            Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            Queue.List->ResourceBarrier(1, &Barrier);
            Queue.List->ClearRenderTargetView(Rtv, Colors[(Case + Frame) % 3], 0, NULL);
            Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            Queue.List->ResourceBarrier(1, &Barrier);
            hr = Queue.Submit();
            Back->Release();
            if (SUCCEEDED(hr))
                hr = Chain3->Present(0, 0);
            TcAppend(Result, &hr, sizeof(hr));
            if (SUCCEEDED(hr))
                hr = Queue.Wait();
        }
        DwmFlush();
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);

        Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
        Info.bmiHeader.biWidth = 64;
        Info.bmiHeader.biHeight = -64;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        WindowDc = GetDC(Window);
        MemoryDc = CreateCompatibleDC(WindowDc);
        Bitmap = CreateDIBSection(MemoryDc, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
        if (Bitmap != NULL)
        {
            HGDIOBJ Previous = SelectObject(MemoryDc, Bitmap);

            BitBlt(MemoryDc, 0, 0, 64, 64, WindowDc, 0, 0, SRCCOPY);
            GdiFlush();
            for (UINT Index = 0; Index < 64 * 64; Index++)
                TcAppend(Result, static_cast<BYTE *>(Bits) + Index * 4, 3);
            SelectObject(MemoryDc, Previous);
            DeleteObject(Bitmap);
        }
        DeleteDC(MemoryDc);
        ReleaseDC(Window, WindowDc);
        if (Chain3)
            Chain3->Release();
        if (Chain)
            Chain->Release();
    }

    if (Rtvs)
        Rtvs->Release();
    if (Factory)
        Factory->Release();
    DestroyWindow(Window);
    TcLogSince(LogStart, Result);
    return hr;
}

static const char TcShaderSource[] =
    "cbuffer Root : register(b0) { float4 Tint; };\n"
    "cbuffer Frame : register(b1) { float4 Place; };\n"
    "Texture2D Image : register(t0);\n"
    "Texture2DMS<float4> Multi : register(t0);\n"
    "SamplerState Point : register(s0);\n"
    "SamplerState Smooth : register(s1);\n"
    "struct VsIn { float2 Pos : POSITION; float4 Color : COLOR; };\n"
    "struct VsOut { float4 Pos : SV_Position; float4 Color : COLOR; float2 Uv : TEXCOORD; };\n"
    "VsOut vs(VsIn i) { VsOut o; o.Pos = float4(i.Pos * Place.w + Place.xy, Place.z, 1); o.Color = i.Color; o.Uv = i.Pos * 0.5 + 0.5; return o; }\n"
    "float4 ps(VsOut i) : SV_Target { return i.Color * Tint; }\n"
    "float4 pstex(VsOut i) : SV_Target { return Image.Sample(Smooth, i.Uv * 1.7) * Tint + Image.Sample(Point, i.Uv.yx) * i.Color; }\n"
    "float4 vsquad(uint Id : SV_VertexID) : SV_Position { return float4(Id == 1 ? 3 : -1, Id == 2 ? -3 : 1, 0, 1); }\n"
    "float4 psms(float4 Pos : SV_Position) : SV_Target { uint x = (uint)Pos.x; return Multi.Load(int2(x / 4, (int)Pos.y), x % 4); }\n"
    "RWStructuredBuffer<uint> Output : register(u0);\n"
    "RWTexture2D<float4> Picture : register(u1);\n"
    "ByteAddressBuffer Input : register(t0);\n"
    "cbuffer Group : register(b0) { uint Base; };\n"
    "groupshared uint Shared[64];\n"
    "[numthreads(8, 8, 1)] void cs(uint3 t : SV_DispatchThreadID, uint g : SV_GroupIndex)\n"
    "{\n"
    "    Shared[g] = Input.Load(((t.x + t.y * 16) * 4) % 256) + Base;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    Output[t.y * 16 + t.x] = Shared[63 - g] * 3 + g;\n"
    "    Picture[t.xy] = float4(t.x / 16.0, t.y / 16.0, g / 64.0, 1);\n"
    "}\n";

static
HRESULT
TcCompile(
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _Out_ ID3DBlob **Blob)
{
    ID3DBlob *Errors = NULL;
    HRESULT hr;

    hr = D3DCompile(TcShaderSource, sizeof(TcShaderSource) - 1, "warp12check", NULL, NULL, Entry, Profile, 0, 0, Blob, &Errors);
    if (Errors)
    {
        if (FAILED(hr))
            printf("%s", (const char *)Errors->GetBufferPointer());
        Errors->Release();
    }
    return hr;
}

static
D3D12_SHADER_BYTECODE
TcCode(
    _In_opt_ ID3DBlob *Blob)
{
    D3D12_SHADER_BYTECODE Code = {};

    if (Blob != NULL)
    {
        Code.pShaderBytecode = Blob->GetBufferPointer();
        Code.BytecodeLength = Blob->GetBufferSize();
    }
    return Code;
}

static
HRESULT
TcRootSignature(
    _In_ ID3D12Device *Device,
    _In_ const D3D12_ROOT_SIGNATURE_DESC *Desc,
    _Out_ ID3D12RootSignature **Signature)
{
    ID3DBlob *Blob = NULL;
    ID3DBlob *Errors = NULL;
    HRESULT hr = D3D12SerializeRootSignature(Desc, D3D_ROOT_SIGNATURE_VERSION_1, &Blob, &Errors);

    *Signature = NULL;
    if (Errors)
        Errors->Release();
    if (SUCCEEDED(hr))
        hr = Device->CreateRootSignature(0, Blob->GetBufferPointer(), Blob->GetBufferSize(), IID_PPV_ARGS(Signature));
    if (Blob)
        Blob->Release();
    return hr;
}

/* The draw scenes' root signature: tint constants, a root constant buffer, a texture table, a sampler table and a static sampler */
static
HRESULT
TcDrawSignature(
    _In_ ID3D12Device *Device,
    _Out_ ID3D12RootSignature **Signature)
{
    D3D12_DESCRIPTOR_RANGE Texture = { D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND };
    D3D12_DESCRIPTOR_RANGE Sampler = { D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 1, 0, 0 };
    D3D12_ROOT_PARAMETER Parameters[4] = {};
    D3D12_STATIC_SAMPLER_DESC Static = {};
    D3D12_ROOT_SIGNATURE_DESC Desc = {};

    Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    Parameters[0].Constants.Num32BitValues = 4;
    Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    Parameters[1].Descriptor.ShaderRegister = 1;
    Parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[2].DescriptorTable.NumDescriptorRanges = 1;
    Parameters[2].DescriptorTable.pDescriptorRanges = &Texture;
    Parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    Parameters[3].DescriptorTable.NumDescriptorRanges = 1;
    Parameters[3].DescriptorTable.pDescriptorRanges = &Sampler;
    Parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Static.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    Static.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Static.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Static.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    Static.MaxLOD = D3D12_FLOAT32_MAX;
    Static.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    Desc.NumParameters = ARRAYSIZE(Parameters);
    Desc.pParameters = Parameters;
    Desc.NumStaticSamplers = 1;
    Desc.pStaticSamplers = &Static;
    Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return TcRootSignature(Device, &Desc, Signature);
}

static const D3D12_INPUT_ELEMENT_DESC TcVertexLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

struct TcVertex
{
    FLOAT Pos[2];
    FLOAT Color[4];
};

/* Graphics pipeline defaults: no blending, no depth, solid fill without culling, one eight bit target */
static
D3D12_GRAPHICS_PIPELINE_STATE_DESC
TcPipelineDesc(
    _In_ ID3D12RootSignature *Signature,
    _In_ ID3DBlob *Vertex,
    _In_ ID3DBlob *Pixel)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = {};

    Desc.pRootSignature = Signature;
    Desc.VS = TcCode(Vertex);
    Desc.PS = TcCode(Pixel);
    for (UINT Index = 0; Index < 8; Index++)
    {
        Desc.BlendState.RenderTarget[Index].SrcBlend = D3D12_BLEND_ONE;
        Desc.BlendState.RenderTarget[Index].DestBlend = D3D12_BLEND_ZERO;
        Desc.BlendState.RenderTarget[Index].BlendOp = D3D12_BLEND_OP_ADD;
        Desc.BlendState.RenderTarget[Index].SrcBlendAlpha = D3D12_BLEND_ONE;
        Desc.BlendState.RenderTarget[Index].DestBlendAlpha = D3D12_BLEND_ZERO;
        Desc.BlendState.RenderTarget[Index].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        Desc.BlendState.RenderTarget[Index].LogicOp = D3D12_LOGIC_OP_NOOP;
        Desc.BlendState.RenderTarget[Index].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    Desc.SampleMask = UINT_MAX;
    Desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    Desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    Desc.RasterizerState.DepthClipEnable = TRUE;
    Desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    Desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    Desc.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    Desc.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    Desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    Desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    Desc.DepthStencilState.BackFace = Desc.DepthStencilState.FrontFace;
    Desc.DepthStencilState.StencilReadMask = 0xFF;
    Desc.DepthStencilState.StencilWriteMask = 0xFF;
    Desc.InputLayout.pInputElementDescs = TcVertexLayout;
    Desc.InputLayout.NumElements = Vertex ? ARRAYSIZE(TcVertexLayout) : 0;
    Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    Desc.NumRenderTargets = 1;
    Desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    return Desc;
}

static
VOID
TcBarrier(
    _In_ ID3D12GraphicsCommandList *List,
    _In_ ID3D12Resource *Resource,
    _In_ D3D12_RESOURCE_STATES Before,
    _In_ D3D12_RESOURCE_STATES After)
{
    D3D12_RESOURCE_BARRIER Barrier = {};

    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Resource;
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = Before;
    Barrier.Transition.StateAfter = After;
    List->ResourceBarrier(1, &Barrier);
}

/* An upload buffer holding Size bytes of Data */
static
ID3D12Resource *
TcUpload(
    _In_ ID3D12Device *Device,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    ID3D12Resource *Buffer = TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, Size, D3D12_RESOURCE_FLAG_NONE);
    VOID *Mapped = NULL;

    if (Buffer != NULL && SUCCEEDED(Buffer->Map(0, NULL, &Mapped)))
    {
        memcpy(Mapped, Data, Size);
        Buffer->Unmap(0, NULL);
    }
    return Buffer;
}

/* A render or depth target texture with its own sample count */
static
ID3D12Resource *
TcTarget(
    _In_ ID3D12Device *Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Samples,
    _In_ D3D12_RESOURCE_FLAGS Flags,
    _In_ D3D12_RESOURCE_STATES State)
{
    D3D12_HEAP_PROPERTIES Heap = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC Desc = {};
    ID3D12Resource *Resource = NULL;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = Width;
    Desc.Height = Height;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = Samples;
    Desc.Flags = Flags;
    if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &Desc, State, NULL, IID_PPV_ARGS(&Resource))))
        return NULL;
    return Resource;
}

/* Everything a draw scene keeps; released in one place */
struct TcDrawKit
{
    ~TcDrawKit()
    {
        for (IUnknown *Item : Objects)
        {
            if (Item)
                Item->Release();
        }
    }

    template <typename Type>
    Type *
    Keep(
        _In_opt_ Type *Item)
    {
        Objects.push_back(Item);
        return Item;
    }

    std::vector<IUnknown *> Objects;
};

static
ID3D12PipelineState *
TcPipeline(
    _In_ ID3D12Device *Device,
    _In_ const D3D12_GRAPHICS_PIPELINE_STATE_DESC &Desc,
    _Inout_ TcDrawKit *Kit)
{
    ID3D12PipelineState *State = NULL;

    Device->CreateGraphicsPipelineState(&Desc, IID_PPV_ARGS(&State));
    return Kit->Keep(State);
}

static
D3D12_CPU_DESCRIPTOR_HANDLE
TcHandle(
    _In_ ID3D12Device *Device,
    _In_ ID3D12DescriptorHeap *Heap,
    _In_ UINT Index)
{
    D3D12_DESCRIPTOR_HEAP_DESC Desc = Heap->GetDesc();
    D3D12_CPU_DESCRIPTOR_HANDLE Handle = Heap->GetCPUDescriptorHandleForHeapStart();

    Handle.ptr += (SIZE_T)Index * Device->GetDescriptorHandleIncrementSize(Desc.Type);
    return Handle;
}

static
D3D12_GPU_DESCRIPTOR_HANDLE
TcGpuHandle(
    _In_ ID3D12Device *Device,
    _In_ ID3D12DescriptorHeap *Heap,
    _In_ UINT Index)
{
    D3D12_DESCRIPTOR_HEAP_DESC Desc = Heap->GetDesc();
    D3D12_GPU_DESCRIPTOR_HANDLE Handle = Heap->GetGPUDescriptorHandleForHeapStart();

    Handle.ptr += (UINT64)Index * Device->GetDescriptorHandleIncrementSize(Desc.Type);
    return Handle;
}

static
ID3D12DescriptorHeap *
TcDescriptorHeap(
    _In_ ID3D12Device *Device,
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE Type,
    _In_ UINT Count,
    _In_ BOOLEAN Visible,
    _Inout_ TcDrawKit *Kit)
{
    D3D12_DESCRIPTOR_HEAP_DESC Desc = { Type, Count, Visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE };
    ID3D12DescriptorHeap *Heap = NULL;

    Device->CreateDescriptorHeap(&Desc, IID_PPV_ARGS(&Heap));
    return Kit->Keep(Heap);
}

/* Per draw placement constants, one 256 byte slot each: offset, depth and scale */
static const FLOAT TcPlaces[4][64] =
{
    { 0.0f, 0.0f, 0.5f, 1.0f },
    { -0.3f, 0.2f, 0.25f, 0.6f },
    { 0.35f, -0.25f, 0.75f, 0.8f },
    { 0.1f, 0.1f, 0.4f, 0.45f },
};

static const TcVertex TcVertices[] =
{
    { { -0.9f, -0.8f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { 0.0f, 0.95f }, { 0.0f, 1.0f, 0.0f, 0.5f } },
    { { 0.85f, -0.7f }, { 0.0f, 0.0f, 1.0f, 0.25f } },
    { { -0.6f, 0.3f }, { 1.0f, 1.0f, 0.0f, 0.75f } },
    { { 0.7f, 0.45f }, { 0.0f, 1.0f, 1.0f, 1.0f } },
    { { -0.2f, -0.95f }, { 1.0f, 0.0f, 1.0f, 0.6f } },
    { { 0.33f, 0.12f }, { 0.5f, 0.25f, 0.75f, 0.9f } },
    { { -0.45f, -0.33f }, { 0.2f, 0.4f, 0.6f, 0.3f } },
};

/* Common objects of the draw scenes: shaders, the root signature, geometry and the placement buffer */
struct TcDrawSetup
{
    HRESULT
    Create(
        _In_ ID3D12Device *Device,
        _Inout_ TcDrawKit *Kit)
    {
        static const UINT16 Indices[] = { 0, 1, 2, 3, 4, 5, 6, 7, 2, 4 };
        HRESULT hr;

        hr = TcCompile("vs", "vs_5_0", &Vertex);
        if (SUCCEEDED(hr))
            hr = TcCompile("ps", "ps_5_0", &Pixel);
        if (SUCCEEDED(hr))
            hr = TcCompile("pstex", "ps_5_0", &Textured);
        if (SUCCEEDED(hr))
            hr = TcDrawSignature(Device, &Signature);
        Kit->Keep(Vertex);
        Kit->Keep(Pixel);
        Kit->Keep(Textured);
        Kit->Keep(Signature);
        if (FAILED(hr))
            return hr;
        Vertices = Kit->Keep(TcUpload(Device, TcVertices, sizeof(TcVertices)));
        IndexBuffer = Kit->Keep(TcUpload(Device, Indices, sizeof(Indices)));
        Places = Kit->Keep(TcUpload(Device, TcPlaces, sizeof(TcPlaces)));
        if (Vertices == NULL || IndexBuffer == NULL || Places == NULL)
            return E_OUTOFMEMORY;
        VertexView.BufferLocation = Vertices->GetGPUVirtualAddress();
        VertexView.SizeInBytes = sizeof(TcVertices);
        VertexView.StrideInBytes = sizeof(TcVertex);
        IndexView.BufferLocation = IndexBuffer->GetGPUVirtualAddress();
        IndexView.SizeInBytes = sizeof(Indices);
        IndexView.Format = DXGI_FORMAT_R16_UINT;
        return S_OK;
    }

    /* Root signature, geometry, a full viewport and scissor, and tint constants */
    VOID
    Bind(
        _In_ ID3D12GraphicsCommandList *List,
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ UINT Place)
    {
        static const FLOAT Tint[4] = { 1.0f, 0.75f, 0.5f, 1.0f };
        D3D12_VIEWPORT Viewport = { 0.0f, 0.0f, (FLOAT)Width, (FLOAT)Height, 0.0f, 1.0f };
        D3D12_RECT Scissor = { 0, 0, (LONG)Width, (LONG)Height };

        List->SetGraphicsRootSignature(Signature);
        List->SetGraphicsRoot32BitConstants(0, 4, Tint, 0);
        List->SetGraphicsRootConstantBufferView(1, Places->GetGPUVirtualAddress() + Place * 256);
        List->IASetVertexBuffers(0, 1, &VertexView);
        List->IASetIndexBuffer(&IndexView);
        List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        List->RSSetViewports(1, &Viewport);
        List->RSSetScissorRects(1, &Scissor);
    }

    ID3DBlob *Vertex = NULL;
    ID3DBlob *Pixel = NULL;
    ID3DBlob *Textured = NULL;
    ID3D12RootSignature *Signature = NULL;
    ID3D12Resource *Vertices = NULL;
    ID3D12Resource *IndexBuffer = NULL;
    ID3D12Resource *Places = NULL;
    D3D12_VERTEX_BUFFER_VIEW VertexView = {};
    D3D12_INDEX_BUFFER_VIEW IndexView = {};
};

/* Triangles, strips, lines and points through root constants and root constant buffers, scissors and indirect draws */
static
HRESULT
SceneDraw(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcQueue Queue;
    ID3D12Resource *Target;
    ID3D12DescriptorHeap *Rtvs;
    ID3D12PipelineState *Triangles;
    ID3D12PipelineState *Lines;
    ID3D12PipelineState *Points;
    ID3D12PipelineState *Culled;
    ID3D12CommandSignature *Indirect = NULL;
    ID3D12Resource *Arguments;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Target = Kit.Keep(TcTarget(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET));
    Rtvs = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, FALSE, &Kit);
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);

        Triangles = TcPipeline(Device, Desc, &Kit);
        Desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        Culled = TcPipeline(Device, Desc, &Kit);
        Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);
        Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        Lines = TcPipeline(Device, Desc, &Kit);
        Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        Points = TcPipeline(Device, Desc, &Kit);
    }
    {
        static const UINT Draws[2][4] = { { 3, 1, 3, 0 }, { 3, 1, 5, 0 } };
        D3D12_INDIRECT_ARGUMENT_DESC Argument = {};
        D3D12_COMMAND_SIGNATURE_DESC Desc = {};

        Argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
        Desc.ByteStride = sizeof(Draws[0]);
        Desc.NumArgumentDescs = 1;
        Desc.pArgumentDescs = &Argument;
        Device->CreateCommandSignature(&Desc, NULL, IID_PPV_ARGS(&Indirect));
        Kit.Keep(Indirect);
        Arguments = Kit.Keep(TcUpload(Device, Draws, sizeof(Draws)));
    }
    if (Target == NULL || Rtvs == NULL || Triangles == NULL || Culled == NULL || Lines == NULL || Points == NULL || Indirect == NULL || Arguments == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }

    Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();
    Device->CreateRenderTargetView(Target, NULL, Rtv);
    Queue.List->ClearRenderTargetView(Rtv, Clear, 0, NULL);
    Queue.List->OMSetRenderTargets(1, &Rtv, FALSE, NULL);
    Setup.Bind(Queue.List, 64, 64, 0);

    Queue.List->SetPipelineState(Triangles);
    Queue.List->DrawInstanced(6, 1, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 1);
    Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    Queue.List->DrawIndexedInstanced(6, 1, 2, 1, 0);
    {
        D3D12_RECT Scissor = { 8, 12, 40, 50 };

        Queue.List->RSSetScissorRects(1, &Scissor);
        Queue.List->SetGraphicsRootConstantBufferView(1, Setup.Places->GetGPUVirtualAddress() + 2 * 256);
        Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Queue.List->DrawInstanced(3, 2, 2, 0);
    }
    Setup.Bind(Queue.List, 64, 64, 3);
    Queue.List->SetPipelineState(Culled);
    Queue.List->DrawIndexedInstanced(9, 1, 0, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 0);
    Queue.List->SetPipelineState(Lines);
    Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    Queue.List->DrawInstanced(8, 1, 0, 0);
    Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    Queue.List->SetGraphicsRoot32BitConstant(0, 0x3F000000, 1);
    Queue.List->DrawIndexedInstanced(5, 1, 3, 0, 0);
    Queue.List->SetPipelineState(Points);
    Queue.List->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    Queue.List->DrawInstanced(8, 1, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 2);
    Queue.List->SetPipelineState(Triangles);
    Queue.List->ExecuteIndirect(Indirect, 2, Arguments, 0, NULL, 0);
    TcBarrier(Queue.List, Target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        hr = TcReadTexture(Device, &Queue, Target, 1, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* Wireframe triangles, culled and not */
static
HRESULT
SceneWireframe(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcQueue Queue;
    ID3D12Resource *Target;
    ID3D12DescriptorHeap *Rtvs;
    ID3D12PipelineState *Wire;
    ID3D12PipelineState *Culled;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Target = Kit.Keep(TcTarget(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET));
    Rtvs = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, FALSE, &Kit);
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);

        Desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        Wire = TcPipeline(Device, Desc, &Kit);
        Desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        Culled = TcPipeline(Device, Desc, &Kit);
    }
    if (Target == NULL || Rtvs == NULL || Wire == NULL || Culled == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();
    Device->CreateRenderTargetView(Target, NULL, Rtv);
    Queue.List->ClearRenderTargetView(Rtv, Clear, 0, NULL);
    Queue.List->OMSetRenderTargets(1, &Rtv, FALSE, NULL);
    Setup.Bind(Queue.List, 64, 64, 0);
    Queue.List->SetPipelineState(Wire);
    Queue.List->DrawInstanced(6, 1, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 3);
    Queue.List->SetPipelineState(Culled);
    Queue.List->DrawIndexedInstanced(9, 1, 0, 0, 0);
    TcBarrier(Queue.List, Target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        hr = TcReadTexture(Device, &Queue, Target, 1, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* A 64x64 eight bit target with its RTV, cleared, bound and set up for the draw scenes */
struct TcFrame
{
    HRESULT
    Create(
        _In_ ID3D12Device *Device,
        _In_ TcQueue *Queue,
        _In_reads_(4) const FLOAT *Clear,
        _In_ UINT Width,
        _In_ UINT Samples,
        _Inout_ TcDrawKit *Kit)
    {
        Target = Kit->Keep(TcTarget(Device, DXGI_FORMAT_R8G8B8A8_UNORM, Width, 64, Samples, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET));
        Rtvs = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, FALSE, Kit);
        if (Target == NULL || Rtvs == NULL)
            return E_OUTOFMEMORY;
        Rtv = Rtvs->GetCPUDescriptorHandleForHeapStart();
        Device->CreateRenderTargetView(Target, NULL, Rtv);
        Queue->List->ClearRenderTargetView(Rtv, Clear, 0, NULL);
        return S_OK;
    }

    /* Moves the target to copy source and reads it back */
    HRESULT
    Finish(
        _In_ ID3D12Device *Device,
        _In_ TcQueue *Queue,
        _Inout_ TcResult *Result)
    {
        HRESULT hr;

        TcBarrier(Queue->List, Target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        hr = Queue->Submit();
        TcAppend(Result, &hr, sizeof(hr));
        if (SUCCEEDED(hr))
            hr = TcReadTexture(Device, Queue, Target, 1, Result);
        return hr;
    }

    ID3D12Resource *Target = NULL;
    ID3D12DescriptorHeap *Rtvs = NULL;
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv = {};
};

/* Depth and stencil tests with a D24S8 buffer, an occlusion query and pipeline statistics */
static
HRESULT
SceneDepth(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcFrame Frame;
    TcQueue Queue;
    ID3D12Resource *Depth;
    ID3D12DescriptorHeap *Dsvs;
    ID3D12PipelineState *Write;
    ID3D12PipelineState *Equal;
    ID3D12PipelineState *Behind;
    ID3D12QueryHeap *Occlusion = NULL;
    ID3D12QueryHeap *Statistics = NULL;
    ID3D12Resource *Answers;
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    if (SUCCEEDED(hr))
        hr = Frame.Create(Device, &Queue, Clear, 64, 1, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Depth = Kit.Keep(TcTarget(Device, DXGI_FORMAT_D24_UNORM_S8_UINT, 64, 64, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    Dsvs = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, FALSE, &Kit);
    Answers = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, 256, D3D12_RESOURCE_FLAG_NONE));
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);
        D3D12_QUERY_HEAP_DESC Queries = { D3D12_QUERY_HEAP_TYPE_OCCLUSION, 2 };

        Desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        Desc.DepthStencilState.DepthEnable = TRUE;
        Desc.DepthStencilState.StencilEnable = TRUE;
        Desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
        Desc.DepthStencilState.BackFace = Desc.DepthStencilState.FrontFace;
        Write = TcPipeline(Device, Desc, &Kit);
        Desc.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
        Desc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
        Desc.DepthStencilState.BackFace = Desc.DepthStencilState.FrontFace;
        Equal = TcPipeline(Device, Desc, &Kit);
        Desc.DepthStencilState.StencilEnable = FALSE;
        Desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        Desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        Behind = TcPipeline(Device, Desc, &Kit);
        Device->CreateQueryHeap(&Queries, IID_PPV_ARGS(&Occlusion));
        Kit.Keep(Occlusion);
        Queries.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
        Queries.Count = 1;
        Device->CreateQueryHeap(&Queries, IID_PPV_ARGS(&Statistics));
        Kit.Keep(Statistics);
    }
    if (Depth == NULL || Dsvs == NULL || Answers == NULL || Write == NULL || Equal == NULL || Behind == NULL || Occlusion == NULL || Statistics == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    Dsv = Dsvs->GetCPUDescriptorHandleForHeapStart();
    Device->CreateDepthStencilView(Depth, NULL, Dsv);
    Queue.List->ClearDepthStencilView(Dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, NULL);
    Queue.List->OMSetRenderTargets(1, &Frame.Rtv, FALSE, &Dsv);
    Queue.List->BeginQuery(Statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    Setup.Bind(Queue.List, 64, 64, 0);
    Queue.List->SetPipelineState(Write);
    Queue.List->OMSetStencilRef(3);
    Queue.List->DrawInstanced(6, 1, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 1);
    Queue.List->SetPipelineState(Equal);
    Queue.List->BeginQuery(Occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
    Queue.List->DrawInstanced(6, 1, 2, 0);
    Queue.List->EndQuery(Occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0);
    Setup.Bind(Queue.List, 64, 64, 2);
    Queue.List->SetPipelineState(Behind);
    Queue.List->BeginQuery(Occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
    Queue.List->DrawIndexedInstanced(9, 1, 0, 0, 0);
    Queue.List->EndQuery(Occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1);
    Queue.List->EndQuery(Statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0);
    Queue.List->ResolveQueryData(Occlusion, D3D12_QUERY_TYPE_OCCLUSION, 0, 1, Answers, 0);
    Queue.List->ResolveQueryData(Occlusion, D3D12_QUERY_TYPE_BINARY_OCCLUSION, 1, 1, Answers, 8);
    Queue.List->ResolveQueryData(Statistics, D3D12_QUERY_TYPE_PIPELINE_STATISTICS, 0, 1, Answers, 16);
    TcBarrier(Queue.List, Depth, D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    hr = Frame.Finish(Device, &Queue, Result);
    if (SUCCEEDED(hr))
        hr = TcReadTexture(Device, &Queue, Depth, 2, Result);
    if (SUCCEEDED(hr))
        TcReadback(Answers, 16 + sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS), Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* Alpha blending, blend factors and write masks */
static
HRESULT
SceneBlend(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.2f, 0.4f, 0.6f, 0.8f };
    static const FLOAT Factor[4] = { 0.25f, 0.5f, 0.75f, 0.125f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcFrame Frame;
    TcQueue Queue;
    ID3D12PipelineState *States[3];
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    if (SUCCEEDED(hr))
        hr = Frame.Create(Device, &Queue, Clear, 64, 1, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);
        D3D12_RENDER_TARGET_BLEND_DESC *Blend = &Desc.BlendState.RenderTarget[0];

        Blend->BlendEnable = TRUE;
        Blend->SrcBlend = D3D12_BLEND_SRC_ALPHA;
        Blend->DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        Blend->SrcBlendAlpha = D3D12_BLEND_ONE;
        Blend->DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        States[0] = TcPipeline(Device, Desc, &Kit);
        Blend->SrcBlend = D3D12_BLEND_BLEND_FACTOR;
        Blend->DestBlend = D3D12_BLEND_INV_BLEND_FACTOR;
        Blend->BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        Blend->BlendOpAlpha = D3D12_BLEND_OP_MAX;
        States[1] = TcPipeline(Device, Desc, &Kit);
        Blend->SrcBlend = D3D12_BLEND_ONE;
        Blend->DestBlend = D3D12_BLEND_ONE;
        Blend->BlendOp = D3D12_BLEND_OP_ADD;
        Blend->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_ALPHA;
        States[2] = TcPipeline(Device, Desc, &Kit);
    }
    if (States[0] == NULL || States[1] == NULL || States[2] == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    Queue.List->OMSetRenderTargets(1, &Frame.Rtv, FALSE, NULL);
    Queue.List->OMSetBlendFactor(Factor);
    for (UINT Index = 0; Index < 3; Index++)
    {
        Setup.Bind(Queue.List, 64, 64, Index);
        Queue.List->SetPipelineState(States[Index]);
        Queue.List->DrawInstanced(6, 1, Index, 0);
    }
    hr = Frame.Finish(Device, &Queue, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* Four sample targets with alpha to coverage, read sample by sample through Texture2DMS loads */
static
HRESULT
SceneMultisample(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcFrame Samples;
    TcFrame Spread;
    TcQueue Queue;
    ID3DBlob *Quad = NULL;
    ID3DBlob *Load = NULL;
    ID3D12PipelineState *Plain;
    ID3D12PipelineState *Coverage;
    ID3D12PipelineState *Reader;
    ID3D12DescriptorHeap *Views;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    if (SUCCEEDED(hr))
        hr = TcCompile("vsquad", "vs_5_0", &Quad);
    Kit.Keep(Quad);
    if (SUCCEEDED(hr))
        hr = TcCompile("psms", "ps_5_0", &Load);
    Kit.Keep(Load);
    if (SUCCEEDED(hr))
        hr = Samples.Create(Device, &Queue, Clear, 16, 4, &Kit);
    if (SUCCEEDED(hr))
        hr = Spread.Create(Device, &Queue, Clear, 64, 1, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Views = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, TRUE, &Kit);
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC Desc = TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Pixel);

        Desc.SampleDesc.Count = 4;
        Plain = TcPipeline(Device, Desc, &Kit);
        Desc.BlendState.AlphaToCoverageEnable = TRUE;
        Coverage = TcPipeline(Device, Desc, &Kit);
        Desc = TcPipelineDesc(Setup.Signature, Quad, Load);
        Desc.InputLayout.NumElements = 0;
        Reader = TcPipeline(Device, Desc, &Kit);
    }
    if (Views == NULL || Plain == NULL || Coverage == NULL || Reader == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    Queue.List->OMSetRenderTargets(1, &Samples.Rtv, FALSE, NULL);
    Setup.Bind(Queue.List, 16, 64, 0);
    Queue.List->SetPipelineState(Plain);
    Queue.List->DrawInstanced(6, 1, 0, 0);
    Setup.Bind(Queue.List, 16, 64, 3);
    Queue.List->SetPipelineState(Coverage);
    Queue.List->DrawInstanced(6, 1, 2, 0);
    TcBarrier(Queue.List, Samples.Target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Device->CreateShaderResourceView(Samples.Target, NULL, Views->GetCPUDescriptorHandleForHeapStart());
    Queue.List->SetDescriptorHeaps(1, &Views);
    Queue.List->OMSetRenderTargets(1, &Spread.Rtv, FALSE, NULL);
    Setup.Bind(Queue.List, 64, 64, 0);
    Queue.List->SetGraphicsRootDescriptorTable(2, Views->GetGPUDescriptorHandleForHeapStart());
    Queue.List->SetPipelineState(Reader);
    Queue.List->DrawInstanced(3, 1, 0, 0);
    hr = Spread.Finish(Device, &Queue, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* A texture sampled through a descriptor table and a sampler heap, next to the static point sampler */
static
HRESULT
SceneTexture(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const FLOAT Clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcDrawSetup Setup;
    TcFrame Frame;
    TcQueue Queue;
    ID3D12Resource *Image;
    ID3D12Resource *Upload;
    ID3D12DescriptorHeap *Views;
    ID3D12DescriptorHeap *Samplers;
    ID3D12PipelineState *State;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = Setup.Create(Device, &Kit);
    if (SUCCEEDED(hr))
        hr = Frame.Create(Device, &Queue, Clear, 64, 1, &Kit);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Image = Kit.Keep(TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST));
    Upload = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 16 * 256, D3D12_RESOURCE_FLAG_NONE));
    Views = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, TRUE, &Kit);
    Samplers = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2, TRUE, &Kit);
    State = TcPipeline(Device, TcPipelineDesc(Setup.Signature, Setup.Vertex, Setup.Textured), &Kit);
    if (Image == NULL || Upload == NULL || Views == NULL || Samplers == NULL || State == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    hr = TcFill(Upload, 16 * 256, 21);
    if (FAILED(hr))
        return hr;
    {
        D3D12_TEXTURE_COPY_LOCATION To = {};
        D3D12_TEXTURE_COPY_LOCATION From = {};
        D3D12_SAMPLER_DESC Sampler = {};

        To.pResource = Image;
        To.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        From.pResource = Upload;
        From.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        From.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        From.PlacedFootprint.Footprint.Width = 16;
        From.PlacedFootprint.Footprint.Height = 16;
        From.PlacedFootprint.Footprint.Depth = 1;
        From.PlacedFootprint.Footprint.RowPitch = 256;
        Queue.List->CopyTextureRegion(&To, 0, 0, 0, &From, NULL);
        TcBarrier(Queue.List, Image, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        Device->CreateShaderResourceView(Image, NULL, TcHandle(Device, Views, 2));
        Sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
        Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        Sampler.MaxLOD = D3D12_FLOAT32_MAX;
        Device->CreateSampler(&Sampler, TcHandle(Device, Samplers, 1));
    }
    {
        ID3D12DescriptorHeap *Heaps[2] = { Views, Samplers };

        Queue.List->SetDescriptorHeaps(2, Heaps);
    }
    Queue.List->OMSetRenderTargets(1, &Frame.Rtv, FALSE, NULL);
    Setup.Bind(Queue.List, 64, 64, 0);
    Queue.List->SetGraphicsRootDescriptorTable(2, TcGpuHandle(Device, Views, 2));
    Queue.List->SetGraphicsRootDescriptorTable(3, TcGpuHandle(Device, Samplers, 1));
    Queue.List->SetPipelineState(State);
    Queue.List->DrawInstanced(6, 1, 0, 0);
    Setup.Bind(Queue.List, 64, 64, 2);
    Queue.List->SetGraphicsRootDescriptorTable(2, TcGpuHandle(Device, Views, 2));
    Queue.List->SetGraphicsRootDescriptorTable(3, TcGpuHandle(Device, Samplers, 1));
    Queue.List->DrawInstanced(3, 1, 5, 0);
    hr = Frame.Finish(Device, &Queue, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

/* Compute writes into a root UAV and a table UAV, with group shared memory, then indirect dispatches with root constants */
static
HRESULT
SceneCompute(
    _In_ ID3D12Device *Device,
    _Inout_ TcResult *Result)
{
    static const UINT Commands[2][4] = { { 100, 2, 1, 1 }, { 1000, 1, 1, 1 } };
    static const UINT CommandCount = 2;
    SIZE_T LogStart = TcLogMark();
    TcDrawKit Kit;
    TcQueue Queue;
    ID3DBlob *Shader = NULL;
    ID3D12RootSignature *Signature = NULL;
    ID3D12PipelineState *State = NULL;
    ID3D12CommandSignature *Indirect = NULL;
    ID3D12Resource *Output;
    ID3D12Resource *Picture;
    ID3D12Resource *Input;
    ID3D12Resource *Arguments;
    ID3D12Resource *Count;
    ID3D12Resource *Readback;
    ID3D12DescriptorHeap *Views;
    HRESULT hr;

    hr = Queue.Create(Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    if (SUCCEEDED(hr))
        hr = TcCompile("cs", "cs_5_0", &Shader);
    Kit.Keep(Shader);
    if (SUCCEEDED(hr))
    {
        D3D12_DESCRIPTOR_RANGE Range = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0 };
        D3D12_ROOT_PARAMETER Parameters[4] = {};
        D3D12_ROOT_SIGNATURE_DESC Desc = {};

        Parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        Parameters[0].Constants.Num32BitValues = 1;
        Parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        Parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        Parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        Parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        Parameters[3].DescriptorTable.pDescriptorRanges = &Range;
        Desc.NumParameters = ARRAYSIZE(Parameters);
        Desc.pParameters = Parameters;
        hr = TcRootSignature(Device, &Desc, &Signature);
    }
    Kit.Keep(Signature);
    if (SUCCEEDED(hr))
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC Desc = {};

        Desc.pRootSignature = Signature;
        Desc.CS = TcCode(Shader);
        hr = Device->CreateComputePipelineState(&Desc, IID_PPV_ARGS(&State));
    }
    Kit.Keep(State);
    if (SUCCEEDED(hr))
    {
        D3D12_INDIRECT_ARGUMENT_DESC Arguments[2] = {};
        D3D12_COMMAND_SIGNATURE_DESC Desc = {};

        Arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        Arguments[0].Constant.Num32BitValuesToSet = 1;
        Arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        Desc.ByteStride = sizeof(Commands[0]);
        Desc.NumArgumentDescs = 2;
        Desc.pArgumentDescs = Arguments;
        hr = Device->CreateCommandSignature(&Desc, Signature, IID_PPV_ARGS(&Indirect));
    }
    Kit.Keep(Indirect);
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    Output = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_DEFAULT, 1024, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS));
    Picture = Kit.Keep(TcTexture(Device, DXGI_FORMAT_R8G8B8A8_UNORM, 16, 16, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    Input = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_UPLOAD, 256, D3D12_RESOURCE_FLAG_NONE));
    Arguments = Kit.Keep(TcUpload(Device, Commands, sizeof(Commands)));
    Count = Kit.Keep(TcUpload(Device, &CommandCount, sizeof(CommandCount)));
    Readback = Kit.Keep(TcBuffer(Device, D3D12_HEAP_TYPE_READBACK, 1024, D3D12_RESOURCE_FLAG_NONE));
    Views = TcDescriptorHeap(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, TRUE, &Kit);
    if (Output == NULL || Picture == NULL || Input == NULL || Arguments == NULL || Count == NULL || Readback == NULL || Views == NULL)
    {
        hr = E_OUTOFMEMORY;
        TcAppend(Result, &hr, sizeof(hr));
        return hr;
    }
    hr = TcFill(Input, 256, 5);
    if (FAILED(hr))
        return hr;
    Device->CreateUnorderedAccessView(Picture, NULL, NULL, TcHandle(Device, Views, 1));
    Queue.List->SetDescriptorHeaps(1, &Views);
    Queue.List->SetComputeRootSignature(Signature);
    Queue.List->SetPipelineState(State);
    Queue.List->SetComputeRoot32BitConstant(0, 7, 0);
    Queue.List->SetComputeRootUnorderedAccessView(1, Output->GetGPUVirtualAddress());
    Queue.List->SetComputeRootShaderResourceView(2, Input->GetGPUVirtualAddress());
    Queue.List->SetComputeRootDescriptorTable(3, TcGpuHandle(Device, Views, 1));
    Queue.List->Dispatch(2, 2, 1);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (FAILED(hr))
        return hr;
    TcBarrier(Queue.List, Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Queue.List->CopyResource(Readback, Output);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        TcReadback(Readback, 1024, Result);

    /* Each indirect command sets the base constant, then dispatches */
    TcBarrier(Queue.List, Output, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    Queue.List->SetDescriptorHeaps(1, &Views);
    Queue.List->SetComputeRootSignature(Signature);
    Queue.List->SetPipelineState(State);
    Queue.List->SetComputeRootUnorderedAccessView(1, Output->GetGPUVirtualAddress());
    Queue.List->SetComputeRootShaderResourceView(2, Input->GetGPUVirtualAddress());
    Queue.List->SetComputeRootDescriptorTable(3, TcGpuHandle(Device, Views, 1));
    Queue.List->ExecuteIndirect(Indirect, 3, Arguments, 0, Count, 0);
    TcBarrier(Queue.List, Output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TcBarrier(Queue.List, Picture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Queue.List->CopyResource(Readback, Output);
    hr = Queue.Submit();
    TcAppend(Result, &hr, sizeof(hr));
    if (SUCCEEDED(hr))
        TcReadback(Readback, 1024, Result);
    if (SUCCEEDED(hr))
        hr = TcReadTexture(Device, &Queue, Picture, 1, Result);
    TcLogSince(LogStart, Result);
    return hr;
}

#include "ops.inl"

static const TcScene Scenes[] =
{
    { "ddi", SceneDdi },
    { "features", SceneFeatures },
    { "formats", SceneFormats },
    { "copy", SceneCopy },
    { "clear", SceneClear },
    { "fence", SceneFence },
    { "present", ScenePresent },
    { "draw", SceneDraw },
    { "wireframe", SceneWireframe },
    { "depth", SceneDepth },
    { "blend", SceneBlend },
    { "msaa", SceneMultisample },
    { "texture", SceneTexture },
    { "compute", SceneCompute },
    { "opsflow", SceneOpsFlow },
    { "opsread", SceneOpsRead },
    { "opsatom", SceneOpsAtom },
    { "opsaddr", SceneOpsAddress },
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
