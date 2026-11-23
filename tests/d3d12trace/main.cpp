/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 runtime traffic to its user mode driver and the kernel, printed for comparison
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d12trace [-runtime system|reactx|<dir>] [-threads] [-ddi] [-nokernel] [scenario ...]
 *
 * The core's imports of GetProcAddress and the D3DKMT thunks are pointed at wrappers in this process
 * only. The wrapper for the driver's OpenAdapter12 wraps the adapter table, every DDI table the driver
 * fills and the callback tables handed to it, so each call prints with its arguments. Handles print as
 * ids in the order they first appear: r for runtime handles, d for driver handles, k for kernel handles.
 * With -ddi the driver's callbacks and what they call are left out. With -nokernel the D3DKMT thunks are left
 * out, since the system core reaches the kernel through calls the tracer does not see.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>
#include <d3d12.h>
#include <d3dkmthk.h>
#include <d3d12umddi.h>
#include <wrl/client.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include "ddi.hpp"

using Microsoft::WRL::ComPtr;

/* The tables the tracer wraps; command list tables can come several to a type */
enum TrFamily
{
    TrFamilyAdapter,
    TrFamilyAdapterCallback,
    TrFamilyDevice,
    TrFamilyList0,
    TrFamilyList1,
    TrFamilyList2,
    TrFamilyList3,
    TrFamilyQueue,
    TrFamilyKernelCallback,
    TrFamilyCoreCallback,
    TrFamilyKernel,
    TrFamilyCount,
};

enum TrKind
{
    TrKindRuntime,
    TrKindDriver,
    TrKindKernel,
    TrKindCount,
};

static constexpr UINT TrSlotLimit = 512;

struct TrState
{
    DWORD MainThread;
    BOOLEAN AllThreads;
    BOOLEAN DdiOnly;
    BOOLEAN NoKernel;
    BOOLEAN LineOpen;
    UINT Depth;
    VOID* Originals[TrFamilyCount][TrSlotLimit];
    PFND3D12DDI_OPENADAPTER OpenAdapter;
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    std::map<ULONG_PTR, UINT> Ids[TrKindCount];
    UINT NextId[TrKindCount];
    SRWLOCK IdLock;
};

static TrState Tr;

/* Set while a call runs that is not printed, so it uses up no ids */
static thread_local BOOLEAN TrQuiet;

/* How deep the driver is in runtime callbacks on this thread */
static thread_local UINT TrCallbackDepth;

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

/* A handle as a stable id; zero stays zero */
static
std::string
TrId(
    _In_ TrKind Kind,
    _In_ ULONG_PTR Value)
{
    static const CHAR Prefix[] = { 'r', 'd', 'k' };
    std::string Text;
    UINT Id = 0;

    if (Value == 0)
        return "0";

    AcquireSRWLockExclusive(&Tr.IdLock);
    auto Found = Tr.Ids[Kind].find(Value);
    if (Found != Tr.Ids[Kind].end())
    {
        Id = Found->second;
    }
    else if (!TrQuiet)
    {
        Tr.NextId[Kind] += 1;
        Id = Tr.NextId[Kind];
        Tr.Ids[Kind].emplace(Value, Id);
    }
    ReleaseSRWLockExclusive(&Tr.IdLock);

    TrAppend(&Text, "%c%u", Prefix[Kind], Id);
    return Text;
}

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
        Hash = (Hash ^ Bytes[Index]) * 16777619u;
    return Hash;
}

static
VOID
TrAppendBytes(
    _Inout_ std::string* Text,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    for (SIZE_T Index = 0; Index < Size && Index < 96; Index++)
        TrAppend(Text, "%02X", Bytes[Index]);
    if (Size > 96)
        TrAppend(Text, "..#%08X", TrHash(Data, Size));
}

static
BOOLEAN
TrShouldLog(VOID)
{
    if (Tr.DdiOnly && TrCallbackDepth != 0)
        return FALSE;
    return Tr.AllThreads || GetCurrentThreadId() == Tr.MainThread;
}

static
VOID
TrCloseLine(VOID)
{
    if (Tr.LineOpen)
    {
        fputc('\n', stdout);
        Tr.LineOpen = FALSE;
    }
}

static
VOID
TrPhase(
    _In_z_ PCSTR Name)
{
    TrCloseLine();
    printf("== %s\n", Name);
}

static
VOID
TrBegin(
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
    {
        printf("[worker] %s\n", Text.c_str());
        return;
    }
    TrCloseLine();
    printf("%*s%s", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = TRUE;
    Tr.Depth += 1;
}

static
VOID
TrEnd(
    _In_ const std::string& Text)
{
    if (GetCurrentThreadId() != Tr.MainThread)
        return;
    Tr.Depth -= 1;
    if (Tr.LineOpen)
        printf("%s%s\n", Text.empty() ? "" : " = ", Text.c_str());
    else if (!Text.empty())
        printf("%*s= %s\n", static_cast<int>(Tr.Depth * 2), "", Text.c_str());
    Tr.LineOpen = FALSE;
}

/* Argument printing, chosen by the argument's type */

template <typename T, typename = void>
struct TrIsDriverHandle : std::false_type
{
};

template <typename T>
struct TrIsDriverHandle<T, std::void_t<decltype(std::declval<T>().pDrvPrivate)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsRuntimeHandle : std::false_type
{
};

template <typename T>
struct TrIsRuntimeHandle<T, std::void_t<decltype(std::declval<T>().handle)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsDescriptor : std::false_type
{
};

template <typename T>
struct TrIsDescriptor<T, std::void_t<decltype(std::declval<T>().ptr)>> : std::true_type
{
};

template <typename T, typename = void>
struct TrIsComplete : std::false_type
{
};

template <typename T>
struct TrIsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type
{
};

/* What a pointed to structure holds; specific structures print their own way below */
static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const VOID* Pointer,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{");
    TrAppendBytes(Text, Pointer, Size);
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_GETCAPS* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u info %s size %u}", Args->Type, Args->pInfo != nullptr ? "p" : "0", Args->DataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{rt %s interface %u version %08X drv %s flags %08X}", TrId(TrKindRuntime,
                                                                             (ULONG_PTR)Args->hRTDevice.handle).c_str(),
             Args->Interface, Args->Version, TrId(TrKindDriver, (ULONG_PTR)Args->hDrvDevice.pDrvPrivate).c_str(),
             Args->Flags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATE_COMMAND_LIST_0040* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u queue %08X id %llu flags %08X node %u}", Args->Type, Args->QueueFlags, Args->ID,
             Args->CommandListFlags, Args->NodeMask);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3D12DDIARG_CREATECOMMANDQUEUE_0050* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{flags %08X node %u creation %08X group %s}", Args->QueueFlags, Args->NodeMask,
             Args->QueueCreationFlags, TrId(TrKindDriver, (ULONG_PTR)Args->SchedulingGroup.pDrvPrivate).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_QUERYADAPTERINFO* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s type %u size %u}", TrId(TrKindKernel, Args->hAdapter).c_str(), Args->Type,
             Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_ESCAPE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s device %s type %u flags %08X size %u data #%08X context %s}",
             TrId(TrKindKernel, Args->hAdapter).c_str(), TrId(TrKindKernel, Args->hDevice).c_str(), Args->Type,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize),
             TrId(TrKindKernel, Args->hContext).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEDEVICE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{adapter %s flags %08X}", TrId(TrKindKernel, Args->hAdapter).c_str(), *(const UINT*)&Args->Flags);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATECONTEXTVIRTUAL* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s node %u affinity %u flags %08X size %u data #%08X hint %u}",
             TrId(TrKindKernel, Args->hDevice).c_str(), Args->NodeOrdinal, Args->EngineAffinity, Args->Flags.Value,
             Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData, Args->PrivateDriverDataSize),
             Args->ClientHint);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATECONTEXT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{node %u affinity %u flags %08X size %u data #%08X}", Args->NodeOrdinal, Args->EngineAffinity,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATEHWQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{context %s flags %08X size %u data #%08X}", TrId(TrKindKernel, (ULONG_PTR)Args->hHwContext).c_str(),
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEHWQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{context %s flags %08X size %u data #%08X}", TrId(TrKindKernel, Args->hHwContext).c_str(),
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATEPAGINGQUEUE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s priority %d node %u}", TrId(TrKindKernel, Args->hDevice).c_str(), Args->Priority,
             Args->PhysicalAdapterIndex);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_ALLOCATE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{res %s count %u size %u data #%08X", TrId(TrKindRuntime, (ULONG_PTR)Args->hResource).c_str(),
             Args->NumAllocations, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                       Args->PrivateDriverDataSize));
    for (UINT Index = 0; Index < Args->NumAllocations && Index < 4; Index++)
    {
        const D3DDDI_ALLOCATIONINFO* Info = &Args->pAllocationInfo[Index];

        TrAppend(Text, " [%u #%08X src %u flags %08X]", Info->PrivateDriverDataSize,
                 TrHash(Info->pPrivateDriverData, Info->PrivateDriverDataSize), Info->VidPnSourceId, Info->Flags.Value);
    }
    TrAppend(Text, "}");
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDI_MAKERESIDENT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{queue %s count %u flags %08X}", TrId(TrKindKernel, Args->hPagingQueue).c_str(), Args->NumAllocations,
             Args->Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_SUBMITCOMMAND* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{length %u size %u flags %08X contexts %u}", Args->CommandLength, Args->PrivateDriverDataSize,
             Args->Flags.Value, Args->BroadcastContextCount);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATESYNCHRONIZATIONOBJECT2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u flags %08X}", Args->Info.Type, Args->Info.Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DKMT_CREATESYNCHRONIZATIONOBJECT2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s type %u flags %08X}", TrId(TrKindKernel, Args->hDevice).c_str(), Args->Info.Type,
             Args->Info.Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_QUERYADAPTERINFO* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{size %u}", Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_QUERYADAPTERINFO2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u size %u}", Args->QueryType, Args->PrivateDriverDataSize);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_ESCAPE* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{device %s flags %08X size %u data #%08X context %s}",
             TrId(TrKindRuntime, (ULONG_PTR)Args->hDevice).c_str(), Args->Flags.Value, Args->PrivateDriverDataSize,
             TrHash(Args->pPrivateDriverData, Args->PrivateDriverDataSize),
             TrId(TrKindRuntime, (ULONG_PTR)Args->hContext).c_str());
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATESYNCHRONIZATIONOBJECT* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{type %u}", Args->Info.Type);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_CREATECONTEXTVIRTUAL* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{node %u affinity %u flags %08X size %u data #%08X}", Args->NodeOrdinal, Args->EngineAffinity,
             Args->Flags.Value, Args->PrivateDriverDataSize, TrHash(Args->pPrivateDriverData,
                                                                    Args->PrivateDriverDataSize));
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_LOCK2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{alloc %s flags %08X}", TrId(TrKindKernel, Args->hAllocation).c_str(), Args->Flags.Value);
}

static
VOID
TrDescribe(
    _Inout_ std::string* Text,
    _In_ const D3DDDICB_DEALLOCATE2* Args,
    _In_ SIZE_T Size)
{
    TrAppend(Text, "{res %s count %u flags %08X}", TrId(TrKindRuntime, (ULONG_PTR)Args->hResource).c_str(),
             Args->NumAllocations, Args->Flags.Value);
}

template <typename T>
static
VOID
TrArg(
    _Inout_ std::string* Text,
    _In_ const T& Value)
{
    if (!Text->empty() && Text->back() != '(')
        Text->append(" ");

    if constexpr (std::is_pointer_v<T>)
    {
        using Pointee = std::remove_cv_t<std::remove_pointer_t<T>>;

        if (Value == nullptr)
            Text->append("0");
        else if constexpr (std::is_void_v<Pointee>)
            Text->append(TrId(TrKindRuntime, (ULONG_PTR)Value));
        else if constexpr (std::is_function_v<Pointee> || !TrIsComplete<Pointee>::value || std::is_arithmetic_v<Pointee> ||
                           std::is_enum_v<Pointee>)
            Text->append("p");
        else
            TrDescribe(Text, Value, sizeof(Pointee));
    }
    else if constexpr (TrIsDriverHandle<T>::value)
    {
        Text->append(TrId(TrKindDriver, (ULONG_PTR)Value.pDrvPrivate));
    }
    else if constexpr (TrIsRuntimeHandle<T>::value)
    {
        Text->append(TrId(TrKindRuntime, (ULONG_PTR)Value.handle));
    }
    else if constexpr (TrIsDescriptor<T>::value)
    {
        Text->append(TrId(TrKindDriver, (ULONG_PTR)Value.ptr));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        FLOAT Single = (FLOAT)Value;
        UINT Bits;

        memcpy(&Bits, &Single, sizeof(Bits));
        TrAppend(Text, "f%08X", Bits);
    }
    else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    {
        TrAppend(Text, "%llu", (UINT64)Value);
    }
    else
    {
        TrAppend(Text, "<");
        TrAppendBytes(Text, &Value, sizeof(Value));
        TrAppend(Text, ">");
    }
}

template <typename T>
static
std::string
TrResult(
    _In_ const T& Value)
{
    std::string Text;

    if constexpr (std::is_same_v<T, HRESULT> || std::is_same_v<T, NTSTATUS>)
        TrAppend(&Text, "%08X", (UINT)Value);
    else
        TrArg(&Text, Value);
    return Text;
}

static
const CHAR*
TrName(
    _In_ TrFamily Family,
    _In_ UINT Slot);

/* Work some calls need around them, such as wrapping the tables they hand out */
template <TrFamily Family, UINT Slot>
struct TrAround
{
    template <typename... A>
    static
    VOID
    Before(
        A... Args)
    {
    }

    template <typename... A>
    static
    VOID
    After(
        A... Args)
    {
    }
};

template <TrFamily Family, UINT Slot, typename Pfn>
struct TrHook;

template <TrFamily Family, UINT Slot, typename R, typename... A>
struct TrHook<Family, Slot, R (APIENTRY*)(A...)>
{
    static
    R
    APIENTRY
    Call(
        A... Args)
    {
        using Pfn = R (APIENTRY*)(A...);
        Pfn Original = reinterpret_cast<Pfn>(Tr.Originals[Family][Slot]);
        BOOLEAN Callback = Family == TrFamilyAdapterCallback || Family == TrFamilyKernelCallback ||
                           Family == TrFamilyCoreCallback;
        BOOLEAN WasQuiet = TrQuiet;
        BOOLEAN Log;
        std::string Text;

        if (Callback)
            TrCallbackDepth += 1;
        Log = TrShouldLog() && !(Family == TrFamilyKernel && Tr.NoKernel);
        TrQuiet = !Log;

        if (Log)
        {
            Text = TrName(Family, Slot);
            Text.append("(");
            (TrArg(&Text, Args), ...);
            Text.append(")");
            TrBegin(Text);
        }

        TrAround<Family, Slot>::Before(Args...);
        if constexpr (std::is_void_v<R>)
        {
            Original(Args...);
            TrAround<Family, Slot>::After(Args...);
            if (Log)
                TrEnd("");
            TrQuiet = WasQuiet;
            if (Callback)
                TrCallbackDepth -= 1;
        }
        else
        {
            R Result = Original(Args...);

            TrAround<Family, Slot>::After(Args...);
            if (Log)
                TrEnd(TrResult(Result));
            TrQuiet = WasQuiet;
            if (Callback)
                TrCallbackDepth -= 1;
            return Result;
        }
    }
};

/* One wrapped table: names and the wrapper for each slot */
struct TrTable
{
    const CHAR* Names[TrSlotLimit];
    VOID* Thunks[TrSlotLimit];
    UINT Count;
};

static TrTable TrTables[TrFamilyCount];

template <TrFamily Family, typename Struct, SIZE_T Offset, typename Member>
static
VOID
TrRegister(
    _In_z_ const CHAR* Name)
{
    constexpr UINT Slot = (UINT)(Offset / sizeof(VOID*));

    static_assert(Slot < TrSlotLimit);
    TrTables[Family].Names[Slot] = Name;
    TrTables[Family].Thunks[Slot] = reinterpret_cast<VOID*>(&TrHook<Family, Slot, Member>::Call);
    if (TrTables[Family].Count < Slot + 1)
        TrTables[Family].Count = Slot + 1;
}

#define TR_ENTRY_FOR(Family) \
    [](){} /* placeholder */

#define TR_REGISTER(FamilyValue, Struct, Member) \
    TrRegister<FamilyValue, Struct, offsetof(Struct, Member), decltype(Struct::Member)>(#Member + 3);

#define TR_ADAPTER(Struct, Member) TR_REGISTER(TrFamilyAdapter, Struct, Member)
#define TR_ADAPTERCALLBACK(Struct, Member) TR_REGISTER(TrFamilyAdapterCallback, Struct, Member)
#define TR_DEVICE(Struct, Member) TR_REGISTER(TrFamilyDevice, Struct, Member)
#define TR_LIST0(Struct, Member) TR_REGISTER(TrFamilyList0, Struct, Member)
#define TR_LIST1(Struct, Member) TR_REGISTER(TrFamilyList1, Struct, Member)
#define TR_LIST2(Struct, Member) TR_REGISTER(TrFamilyList2, Struct, Member)
#define TR_LIST3(Struct, Member) TR_REGISTER(TrFamilyList3, Struct, Member)
#define TR_QUEUE(Struct, Member) TR_REGISTER(TrFamilyQueue, Struct, Member)
#define TR_KERNELCALLBACK(Struct, Member) TR_REGISTER(TrFamilyKernelCallback, Struct, Member)
#define TR_CORECALLBACK(Struct, Member) TR_REGISTER(TrFamilyCoreCallback, Struct, Member)

/* Kernel thunks the core may call, by name and type */
#define TR_TABLE_KERNEL(Entry) \
    Entry(D3DKMTCreateDevice, PFND3DKMT_CREATEDEVICE) \
    Entry(D3DKMTDestroyDevice, PFND3DKMT_DESTROYDEVICE) \
    Entry(D3DKMTCreateContextVirtual, PFND3DKMT_CREATECONTEXTVIRTUAL) \
    Entry(D3DKMTDestroyContext, PFND3DKMT_DESTROYCONTEXT) \
    Entry(D3DKMTCreateHwQueue, PFND3DKMT_CREATEHWQUEUE) \
    Entry(D3DKMTDestroyHwQueue, PFND3DKMT_DESTROYHWQUEUE) \
    Entry(D3DKMTCreatePagingQueue, PFND3DKMT_CREATEPAGINGQUEUE) \
    Entry(D3DKMTDestroyPagingQueue, PFND3DKMT_DESTROYPAGINGQUEUE) \
    Entry(D3DKMTCreateAllocation2, PFND3DKMT_CREATEALLOCATION) \
    Entry(D3DKMTDestroyAllocation2, PFND3DKMT_DESTROYALLOCATION2) \
    Entry(D3DKMTMakeResident, PFND3DKMT_MAKERESIDENT) \
    Entry(D3DKMTEvict, PFND3DKMT_EVICT) \
    Entry(D3DKMTLock2, PFND3DKMT_LOCK2) \
    Entry(D3DKMTUnlock2, PFND3DKMT_UNLOCK2) \
    Entry(D3DKMTEscape, PFND3DKMT_ESCAPE) \
    Entry(D3DKMTQueryAdapterInfo, PFND3DKMT_QUERYADAPTERINFO) \
    Entry(D3DKMTSubmitCommand, PFND3DKMT_SUBMITCOMMAND) \
    Entry(D3DKMTSubmitCommandToHwQueue, PFND3DKMT_SUBMITCOMMANDTOHWQUEUE) \
    Entry(D3DKMTCreateSynchronizationObject2, PFND3DKMT_CREATESYNCHRONIZATIONOBJECT2) \
    Entry(D3DKMTDestroySynchronizationObject, PFND3DKMT_DESTROYSYNCHRONIZATIONOBJECT) \
    Entry(D3DKMTSignalSynchronizationObjectFromCpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU) \
    Entry(D3DKMTWaitForSynchronizationObjectFromCpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU) \
    Entry(D3DKMTSignalSynchronizationObjectFromGpu, PFND3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMGPU) \
    Entry(D3DKMTWaitForSynchronizationObjectFromGpu, PFND3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMGPU) \
    Entry(D3DKMTOpenAdapterFromLuid, PFND3DKMT_OPENADAPTERFROMLUID) \
    Entry(D3DKMTCloseAdapter, PFND3DKMT_CLOSEADAPTER) \
    Entry(D3DKMTMapGpuVirtualAddress, PFND3DKMT_MAPGPUVIRTUALADDRESS) \
    Entry(D3DKMTReserveGpuVirtualAddress, PFND3DKMT_RESERVEGPUVIRTUALADDRESS) \
    Entry(D3DKMTFreeGpuVirtualAddress, PFND3DKMT_FREEGPUVIRTUALADDRESS) \
    Entry(D3DKMTUpdateGpuVirtualAddress, PFND3DKMT_UPDATEGPUVIRTUALADDRESS) \
    Entry(D3DKMTSetContextSchedulingPriority, PFND3DKMT_SETCONTEXTSCHEDULINGPRIORITY) \
    Entry(D3DKMTSetContextInProcessSchedulingPriority, PFND3DKMT_SETCONTEXTINPROCESSSCHEDULINGPRIORITY) \
    Entry(D3DKMTQueryResourceInfo, PFND3DKMT_QUERYRESOURCEINFO) \
    Entry(D3DKMTOpenResource2, PFND3DKMT_OPENRESOURCE2) \
    Entry(D3DKMTGetDeviceState, PFND3DKMT_GETDEVICESTATE) \
    Entry(D3DKMTCreateNativeFence, PFND3DKMT_CREATENATIVEFENCE) \
    Entry(D3DKMTSubmitWaitForSyncObjectsToHwQueue, PFND3DKMT_SUBMITWAITFORSYNCOBJECTSTOHWQUEUE) \
    Entry(D3DKMTSubmitSignalSyncObjectsToHwQueue, PFND3DKMT_SUBMITSIGNALSYNCOBJECTSTOHWQUEUE) \
    Entry(D3DKMTChangeVideoMemoryReservation, PFND3DKMT_CHANGEVIDEOMEMORYRESERVATION) \
    Entry(D3DKMTQueryVideoMemoryInfo, PFND3DKMT_QUERYVIDEOMEMORYINFO) \
    Entry(D3DKMTSetAllocationPriority, PFND3DKMT_SETALLOCATIONPRIORITY) \
    Entry(D3DKMTRegisterTrimNotification, PFND3DKMT_REGISTERTRIMNOTIFICATION) \
    Entry(D3DKMTUnregisterTrimNotification, PFND3DKMT_UNREGISTERTRIMNOTIFICATION) \
    Entry(D3DKMTOfferAllocations, PFND3DKMT_OFFERALLOCATIONS) \
    Entry(D3DKMTReclaimAllocations2, PFND3DKMT_RECLAIMALLOCATIONS2) \
    Entry(D3DKMTInvalidateCache, PFND3DKMT_INVALIDATECACHE) \
    Entry(D3DKMTEnumAdapters2, PFND3DKMT_ENUMADAPTERS2) \
    Entry(D3DKMTQueryClockCalibration, PFND3DKMT_QUERYCLOCKCALIBRATION) \
    Entry(D3DKMTGetSharedResourceAdapterLuid, PFND3DKMT_GETSHAREDRESOURCEADAPTERLUID) \

/* The kernel thunk list as a table with one slot per entry */
struct TrKernelSlots
{
#define TR_KERNEL_MEMBER(Name, Type) Type Name;
    TR_TABLE_KERNEL(TR_KERNEL_MEMBER)
#undef TR_KERNEL_MEMBER
};

#define TR_KERNEL(Name, Type) TR_REGISTER(TrFamilyKernel, TrKernelSlots, Name)

static
VOID
TrRegisterAll(VOID)
{
    TR_TABLE_ADAPTER(TR_ADAPTER)
    TR_TABLE_ADAPTERCALLBACK(TR_ADAPTERCALLBACK)
    TR_TABLE_DEVICE(TR_DEVICE)
    TR_TABLE_LIST(TR_LIST0)
    TR_TABLE_LIST(TR_LIST1)
    TR_TABLE_LIST(TR_LIST2)
    TR_TABLE_LIST(TR_LIST3)
    TR_TABLE_QUEUE(TR_QUEUE)
    TR_TABLE_KERNELCALLBACK(TR_KERNELCALLBACK)
    TR_TABLE_CORECALLBACK(TR_CORECALLBACK)
    TR_TABLE_KERNEL(TR_KERNEL)
}

static
const CHAR*
TrName(
    _In_ TrFamily Family,
    _In_ UINT Slot)
{
    const CHAR* Name = TrTables[Family].Names[Slot];

    return Name != nullptr ? Name : "?";
}

/* Saves each filled entry of a table and points it at the wrapper for its slot */
static
VOID
TrWrapTable(
    _In_ TrFamily Family,
    _Inout_updates_bytes_(Size) VOID* Table,
    _In_ SIZE_T Size)
{
    VOID** Slots = static_cast<VOID**>(Table);
    UINT Count = (UINT)(Size / sizeof(VOID*));

    if (Count > TrTables[Family].Count)
        Count = TrTables[Family].Count;

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Slots[Slot] == nullptr || Slots[Slot] == TrTables[Family].Thunks[Slot] ||
            TrTables[Family].Thunks[Slot] == nullptr)
        {
            continue;
        }
        Tr.Originals[Family][Slot] = Slots[Slot];
        Slots[Slot] = TrTables[Family].Thunks[Slot];
    }
}

/* A runtime callback table copied so the driver calls the copy, which is wrapped */
static
const VOID*
TrWrapCallbacks(
    _In_ TrFamily Family,
    _In_opt_ const VOID* Table,
    _In_ SIZE_T Size)
{
    VOID* Copy;

    if (Table == nullptr)
        return nullptr;

    /* The driver keeps these for the life of the device, so the copy is never freed */
    Copy = HeapAlloc(GetProcessHeap(), 0, Size);
    if (Copy == nullptr)
        return Table;
    memcpy(Copy, Table, Size);
    TrWrapTable(Family, Copy, Size);
    return Copy;
}

/* The adapter's FillDDITable: the tables it fills are wrapped once the driver is done */
template <>
struct TrAround<TrFamilyAdapter, offsetof(D3D12DDI_ADAPTERFUNCS, pfnFillDDITable) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HADAPTER Adapter,
        D3D12DDI_TABLE_TYPE Type,
        VOID* Table,
        SIZE_T Size,
        UINT Index,
        D3D12DDI_HRTTABLE RuntimeTable)
    {
    }

    static
    VOID
    After(
        D3D12DDI_HADAPTER Adapter,
        D3D12DDI_TABLE_TYPE Type,
        VOID* Table,
        SIZE_T Size,
        UINT Index,
        D3D12DDI_HRTTABLE RuntimeTable)
    {
        if (Type == D3D12DDI_TABLE_TYPE_DEVICE_CORE)
            TrWrapTable(TrFamilyDevice, Table, Size);
        else if (Type == D3D12DDI_TABLE_TYPE_COMMAND_LIST_3D && Index < 4)
            TrWrapTable((TrFamily)(TrFamilyList0 + Index), Table, Size);
        else if (Type == D3D12DDI_TABLE_TYPE_COMMAND_QUEUE_3D)
            TrWrapTable(TrFamilyQueue, Table, Size);
    }
};

/* The adapter's CreateDevice: the callbacks handed in are swapped for wrapped copies */
template <>
struct TrAround<TrFamilyAdapter, offsetof(D3D12DDI_ADAPTERFUNCS, pfnCreateDevice) / sizeof(VOID*)>
{
    static
    VOID
    Before(
        D3D12DDI_HADAPTER Adapter,
        const D3D12DDIARG_CREATEDEVICE_0003* Args)
    {
        D3D12DDIARG_CREATEDEVICE_0003* Writable = const_cast<D3D12DDIARG_CREATEDEVICE_0003*>(Args);

        Writable->pKTCallbacks = static_cast<const D3DDDI_DEVICECALLBACKS*>(
            TrWrapCallbacks(TrFamilyKernelCallback, Args->pKTCallbacks, sizeof(D3DDDI_DEVICECALLBACKS)));
        Writable->p12UMCallbacks_0116 = static_cast<const D3D12DDI_CORELAYER_DEVICECALLBACKS_0116*>(
            TrWrapCallbacks(TrFamilyCoreCallback, Args->p12UMCallbacks_0116,
                            sizeof(D3D12DDI_CORELAYER_DEVICECALLBACKS_0116)));
    }

    static
    VOID
    After(
        D3D12DDI_HADAPTER Adapter,
        const D3D12DDIARG_CREATEDEVICE_0003* Args)
    {
    }
};

/* The driver's OpenAdapter12: adapter callbacks in, adapter table out */
static
HRESULT
APIENTRY
TrOpenAdapter12(
    _Inout_ D3D12DDIARG_OPENADAPTER* Args)
{
    std::string Text;
    HRESULT hr;

    Args->pAdapterCallbacks = static_cast<const D3DDDI_ADAPTERCALLBACKS*>(
        TrWrapCallbacks(TrFamilyAdapterCallback, Args->pAdapterCallbacks, sizeof(D3DDDI_ADAPTERCALLBACKS)));

    TrAppend(&Text, "OpenAdapter12(rt %s)", TrId(TrKindRuntime, (ULONG_PTR)Args->hRTAdapter.handle).c_str());
    TrBegin(Text);
    hr = Tr.OpenAdapter(Args);
    Text.clear();
    TrAppend(&Text, "%08X adapter %s", (UINT)hr, TrId(TrKindDriver, (ULONG_PTR)Args->hAdapter.pDrvPrivate).c_str());
    TrEnd(Text);

    if (SUCCEEDED(hr))
        TrWrapTable(TrFamilyAdapter, Args->pAdapterFuncs, sizeof(D3D12DDI_ADAPTERFUNCS_0109));
    return hr;
}

/* Kernel thunk slots by name; stored names drop the D3D prefix */
static
INT
TrKernelSlot(
    _In_z_ LPCSTR Name)
{
    for (UINT Slot = 0; Slot < TrTables[TrFamilyKernel].Count; Slot++)
    {
        if (TrTables[TrFamilyKernel].Names[Slot] != nullptr &&
            strcmp(TrTables[TrFamilyKernel].Names[Slot], Name + 3) == 0)
        {
            return (INT)Slot;
        }
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

    if (strcmp(Name, "OpenAdapter12") == 0)
    {
        Tr.OpenAdapter = reinterpret_cast<PFND3D12DDI_OPENADAPTER>(Found);
        return reinterpret_cast<FARPROC>(&TrOpenAdapter12);
    }

    if (strncmp(Name, "D3DKMT", 6) == 0)
    {
        Slot = TrKernelSlot(Name);
        if (Slot >= 0)
        {
            Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Found);
            return reinterpret_cast<FARPROC>(TrTables[TrFamilyKernel].Thunks[Slot]);
        }
    }

    return Found;
}

/* The exported name of a function, found in the module that holds it */
static
LPCSTR
TrExportName(
    _In_opt_ FARPROC Function)
{
    HMODULE Owner = NULL;
    BYTE* Base;
    IMAGE_NT_HEADERS* Nt;
    IMAGE_EXPORT_DIRECTORY* Exports;
    DWORD* Names;
    WORD* Ordinals;
    DWORD* Functions;

    if (Function == nullptr ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(Function), &Owner))
    {
        return nullptr;
    }

    Base = reinterpret_cast<BYTE*>(Owner);
    Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Base + reinterpret_cast<IMAGE_DOS_HEADER*>(Base)->e_lfanew);
    Exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(
        Base + Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    Names = reinterpret_cast<DWORD*>(Base + Exports->AddressOfNames);
    Ordinals = reinterpret_cast<WORD*>(Base + Exports->AddressOfNameOrdinals);
    Functions = reinterpret_cast<DWORD*>(Base + Exports->AddressOfFunctions);

    for (DWORD Index = 0; Index < Exports->NumberOfNames; Index++)
    {
        if (Base + Functions[Ordinals[Index]] == reinterpret_cast<BYTE*>(Function))
            return reinterpret_cast<LPCSTR>(Base + Names[Index]);
    }
    return nullptr;
}

/* Points a module's imports of GetProcAddress and the D3DKMT thunks at the wrappers */
static
VOID
TrPatchImports(
    _In_ HMODULE Module)
{
    BYTE* Base = reinterpret_cast<BYTE*>(Module);
    IMAGE_NT_HEADERS* Nt = reinterpret_cast<IMAGE_NT_HEADERS*>(Base + reinterpret_cast<IMAGE_DOS_HEADER*>(Base)->e_lfanew);
    IMAGE_DATA_DIRECTORY* Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    IMAGE_IMPORT_DESCRIPTOR* Import = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(Base + Directory->VirtualAddress);

    for (; Import->Name != 0; Import++)
    {
        IMAGE_THUNK_DATA* Names = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->OriginalFirstThunk);
        IMAGE_THUNK_DATA* Slots = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->FirstThunk);

        for (; Names->u1.AddressOfData != 0; Names++, Slots++)
        {
            VOID* Replacement = nullptr;
            LPCSTR Name;
            DWORD Old;
            INT Slot;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            Name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData)->Name;

            if (strcmp(Name, "GetProcAddress") == 0)
            {
                Tr.GetProcAddress = reinterpret_cast<FARPROC (WINAPI*)(HMODULE, LPCSTR)>(Slots->u1.Function);
                Replacement = reinterpret_cast<VOID*>(&TrGetProcAddress);
            }
            else if (strncmp(Name, "D3DKMT", 6) == 0 && (Slot = TrKernelSlot(Name)) >= 0)
            {
                Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Slots->u1.Function);
                Replacement = TrTables[TrFamilyKernel].Thunks[Slot];
            }

            if (Replacement == nullptr)
                continue;
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), PAGE_READWRITE, &Old);
            Slots->u1.Function = reinterpret_cast<ULONG_PTR>(Replacement);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), Old, &Old);
        }
    }

    /* Delay loaded thunks are resolved here so the loader stub never replaces the wrapper */
    Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (Directory->VirtualAddress == 0)
        return;

    for (IMAGE_DELAYLOAD_DESCRIPTOR* Delay = reinterpret_cast<IMAGE_DELAYLOAD_DESCRIPTOR*>(Base + Directory->VirtualAddress);
         Delay->DllNameRVA != 0;
         Delay++)
    {
        IMAGE_THUNK_DATA* Names = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Delay->ImportNameTableRVA);
        IMAGE_THUNK_DATA* Slots = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Delay->ImportAddressTableRVA);
        HMODULE Target = NULL;

        for (; Names->u1.AddressOfData != 0; Names++, Slots++)
        {
            LPCSTR Name;
            FARPROC Real;
            DWORD Old;
            INT Slot;

            if (Target == NULL)
                Target = LoadLibraryA(reinterpret_cast<LPCSTR>(Base + Delay->DllNameRVA));
            if (Target == NULL)
                break;

            /* The system core imports these by ordinal; the name comes from the target's exports */
            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
            {
                Real = GetProcAddress(Target, MAKEINTRESOURCEA(IMAGE_ORDINAL(Names->u1.Ordinal)));
                Name = TrExportName(Real);
            }
            else
            {
                Name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData)->Name;
                Real = GetProcAddress(Target, Name);
            }

            if (Real == nullptr || Name == nullptr || strncmp(Name, "D3DKMT", 6) != 0 ||
                (Slot = TrKernelSlot(Name)) < 0)
            {
                continue;
            }

            Tr.Originals[TrFamilyKernel][Slot] = reinterpret_cast<VOID*>(Real);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), PAGE_READWRITE, &Old);
            Slots->u1.Function = reinterpret_cast<ULONG_PTR>(TrTables[TrFamilyKernel].Thunks[Slot]);
            VirtualProtect(&Slots->u1.Function, sizeof(VOID*), Old, &Old);
        }
    }
}

/* Scenarios */

struct TrRun
{
    PFN_D3D12_CREATE_DEVICE CreateDevice;
};

static
VOID
TrScenarioDevice(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;

    TrPhase("D3D12CreateDevice");
    printf("result %08X\n", (UINT)Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
    TrPhase("D3D12CreateDevice again");
    printf("result %08X\n", (UINT)Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)));
}

static
VOID
TrScenarioObjects(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12DescriptorHeap> Heap;
    ComPtr<ID3D12Fence> Fence;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    TrPhase("CreateCommandQueue direct");
    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    printf("result %08X\n", (UINT)Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue)));
    TrPhase("CreateCommandAllocator direct");
    printf("result %08X\n", (UINT)Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                                 IID_PPV_ARGS(&Allocator)));
    TrPhase("CreateCommandList direct");
    printf("result %08X\n", (UINT)Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(),
                                                            nullptr, IID_PPV_ARGS(&List)));
    TrPhase("Close");
    printf("result %08X\n", (UINT)List->Close());
    TrPhase("CreateDescriptorHeap rtv");
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 4;
    printf("result %08X\n", (UINT)Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap)));
    TrPhase("CreateFence");
    printf("result %08X\n", (UINT)Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)));
    TrPhase("Release");
    Fence.Reset();
    Heap.Reset();
    List.Reset();
    Allocator.Reset();
    Queue.Reset();
}

static
VOID
TrScenarioResources(
    _In_ TrRun& Run)
{
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12Resource> Buffer;
    ComPtr<ID3D12Resource> Texture;
    D3D12_HEAP_PROPERTIES Upload = { D3D12_HEAP_TYPE_UPLOAD };
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC Desc = {};

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = 65536;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    TrPhase("CreateCommittedResource upload buffer");
    printf("result %08X\n", (UINT)Device->CreateCommittedResource(&Upload, D3D12_HEAP_FLAG_NONE, &Desc,
                                                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                                  IID_PPV_ARGS(&Buffer)));

    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    TrPhase("CreateCommittedResource default texture");
    printf("result %08X\n", (UINT)Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc,
                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                                                  IID_PPV_ARGS(&Texture)));
    TrPhase("Release");
    Texture.Reset();
    Buffer.Reset();
}

static
VOID
TrScenarioCopy(
    _In_ TrRun& Run)
{
    static const FLOAT Color[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
    ComPtr<ID3D12Device> Device;
    ComPtr<ID3D12CommandQueue> Queue;
    ComPtr<ID3D12CommandAllocator> Allocator;
    ComPtr<ID3D12GraphicsCommandList> List;
    ComPtr<ID3D12DescriptorHeap> Heap;
    ComPtr<ID3D12Resource> Target;
    ComPtr<ID3D12Resource> Readback;
    ComPtr<ID3D12Fence> Fence;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    D3D12_HEAP_PROPERTIES Default = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES Read = { D3D12_HEAP_TYPE_READBACK };
    D3D12_RESOURCE_DESC Desc = {};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint;
    D3D12_TEXTURE_COPY_LOCATION Destination = {};
    D3D12_TEXTURE_COPY_LOCATION Source = {};
    D3D12_RESOURCE_BARRIER Barrier = {};
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv;
    ID3D12CommandList* Lists[1];
    UINT64 Total;
    const UINT* Pixels;
    HANDLE Event;

    if (FAILED(Run.CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device))))
        return;

    QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HeapDesc.NumDescriptors = 1;
    if (FAILED(Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue))) ||
        FAILED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&Allocator))) ||
        FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&List))) ||
        FAILED(Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Heap))) ||
        FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence))))
    {
        printf("setup failed\n");
        return;
    }

    Desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    Desc.Width = 64;
    Desc.Height = 64;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    Device->CreateCommittedResource(&Default, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                    IID_PPV_ARGS(&Target));
    Device->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, nullptr, nullptr, &Total);
    Desc = {};
    Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    Desc.Width = Total;
    Desc.Height = 1;
    Desc.DepthOrArraySize = 1;
    Desc.MipLevels = 1;
    Desc.SampleDesc.Count = 1;
    Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Device->CreateCommittedResource(&Read, D3D12_HEAP_FLAG_NONE, &Desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    IID_PPV_ARGS(&Readback));
    if (Target == nullptr || Readback == nullptr)
    {
        printf("resources failed\n");
        return;
    }

    TrPhase("CreateRenderTargetView");
    Rtv = Heap->GetCPUDescriptorHandleForHeapStart();
    Device->CreateRenderTargetView(Target.Get(), nullptr, Rtv);
    TrPhase("ClearRenderTargetView");
    List->ClearRenderTargetView(Rtv, Color, 0, nullptr);
    TrPhase("ResourceBarrier");
    Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    Barrier.Transition.pResource = Target.Get();
    Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    Barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    Barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    List->ResourceBarrier(1, &Barrier);
    TrPhase("CopyTextureRegion");
    Destination.pResource = Readback.Get();
    Destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    Destination.PlacedFootprint = Footprint;
    Source.pResource = Target.Get();
    Source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    List->CopyTextureRegion(&Destination, 0, 0, 0, &Source, nullptr);
    TrPhase("Close");
    printf("result %08X\n", (UINT)List->Close());
    TrPhase("ExecuteCommandLists");
    Lists[0] = List.Get();
    Queue->ExecuteCommandLists(1, Lists);
    TrPhase("Signal");
    printf("result %08X\n", (UINT)Queue->Signal(Fence.Get(), 1));
    TrPhase("SetEventOnCompletion");
    Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    printf("result %08X\n", (UINT)Fence->SetEventOnCompletion(1, Event));
    printf("wait %lu\n", WaitForSingleObject(Event, 5000));
    CloseHandle(Event);
    TrPhase("Map");
    if (SUCCEEDED(Readback->Map(0, nullptr, (void**)&Pixels)))
    {
        printf("pixel %08X\n", Pixels[0]);
        Readback->Unmap(0, nullptr);
    }
    TrPhase("Release");
}

struct TrScenario
{
    PCSTR Name;
    VOID (*Run)(TrRun& Run);
};

static const TrScenario TrScenarios[] =
{
    { "device", TrScenarioDevice },
    { "objects", TrScenarioObjects },
    { "resources", TrScenarioResources },
    { "copy", TrScenarioCopy },
};

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Directory;
    std::vector<std::string> Wanted;
    PCWSTR Runtime = L"system";
    WCHAR Module[MAX_PATH];
    HMODULE D3d12;
    HMODULE Core;
    TrRun Run = {};
    PWSTR Slash;
    CHAR Name[64];

    setvbuf(stdout, nullptr, _IONBF, 0);
    Tr.MainThread = GetCurrentThreadId();
    InitializeSRWLock(&Tr.IdLock);
    TrRegisterAll();

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Index += 1;
            Runtime = argv[Index];
        }
        else if (_wcsicmp(argv[Index], L"-threads") == 0)
        {
            Tr.AllThreads = TRUE;
        }
        else if (_wcsicmp(argv[Index], L"-ddi") == 0)
        {
            Tr.DdiOnly = TRUE;
        }
        else if (_wcsicmp(argv[Index], L"-nokernel") == 0)
        {
            Tr.NoKernel = TRUE;
        }
        else
        {
            WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
            Wanted.push_back(Name);
        }
    }

    if (_wcsicmp(Runtime, L"system") == 0)
    {
        GetSystemDirectoryW(Module, _countof(Module));
        Directory = std::wstring(Module) + L"\\";
    }
    else if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = UNICODE_NULL;
        Directory = std::wstring(Module) + L"reactx\\";
    }
    else
    {
        Directory = std::wstring(Runtime) + L"\\";
    }

    /* The core is loaded first so its imports point at the wrappers before d3d12.dll uses it */
    Core = LoadLibraryExW((Directory + L"d3d12core.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    D3d12 = LoadLibraryExW((Directory + L"d3d12.dll").c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Core == NULL || D3d12 == NULL)
    {
        fprintf(stderr, "error: cannot load the runtime\n");
        return 1;
    }
    TrPatchImports(Core);

    for (HMODULE Each : { D3d12, Core })
    {
        GetModuleFileNameW(Each, Module, _countof(Module));
        fwprintf(stderr, L"loaded %s\n", Module);
    }

    Run.CreateDevice = reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(D3d12, "D3D12CreateDevice"));
    if (Run.CreateDevice == nullptr)
        return 1;

    for (const TrScenario& Scenario : TrScenarios)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Each : Wanted)
            IsWanted = IsWanted || Each == Scenario.Name;
        if (!IsWanted)
            continue;

        TrCloseLine();
        printf("### %s\n", Scenario.Name);
        Scenario.Run(Run);
    }

    TrCloseLine();
    fflush(stdout);

    /* Driver threads may still call in; leave without running their teardown */
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
