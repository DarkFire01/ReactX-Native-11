/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     In process tracer of the D3D12 calls a layer makes on a device and its queue
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * Each traced method is a slot in the object's vtable, found by reading the compiler's virtual call
 * thunk for the member pointer. Objects print as names given in the order they first appear, CPU and
 * GPU descriptor handles as heap[index], and GPU addresses inside a known buffer as buffer+offset.
 */

#include <windows.h>
#include <d3d12.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "trace12.hpp"

/* The object kinds the tracer hooks, each with its own method list */
enum Tr12Family
{
    Tr12FamilyDevice,
    Tr12FamilyQueue,
    Tr12FamilyList,
    Tr12FamilyAllocator,
    Tr12FamilyFence,
    Tr12FamilyResource,
    Tr12FamilyHeap,
    Tr12FamilyDescriptorHeap,
    Tr12FamilyOther,
    Tr12FamilyCount,
};

struct Tr12DescriptorRange
{
    SIZE_T CpuStart;
    UINT64 GpuStart;
    UINT Increment;
    UINT Count;
    std::string Name;
};

struct Tr12Buffer
{
    UINT64 Size;
    std::string Name;
};

struct Tr12Slot
{
    PCSTR Name;
    void* Thunk;
    void* Hook;
    UINT* SlotIndex;
    void** Fallback;
};

struct Tr12State
{
    std::recursive_mutex Lock;
    BOOLEAN Recording = FALSE;
    std::vector<std::string> Lines;
    std::map<const void*, std::string> Names;
    std::map<const void*, std::string> Pinned;
    std::map<std::string, UINT> Counters;
    std::map<DWORD, UINT> Threads;
    std::vector<Tr12DescriptorRange> Ranges;
    std::map<UINT64, Tr12Buffer> Buffers;
    std::map<void**, std::vector<void*>> Originals;
};

static Tr12State Tr;
static thread_local UINT TrDepth;

static
std::string
TrFormat(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Text[512];
    va_list Arguments;

    va_start(Arguments, Format);
    vsnprintf(Text, sizeof(Text), Format, Arguments);
    va_end(Arguments);
    return Text;
}

static
std::string
TrObjectName(
    _In_opt_ const void* Object)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
    auto Found = Tr.Names.find(Object);

    if (Object == nullptr)
        return "null";
    if (Found != Tr.Names.end())
        return Found->second;
    return "obj?";
}

static
std::string
TrNameObject(
    _In_ const void* Object,
    _In_z_ PCSTR Prefix)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
    std::string Name;

    if (Tr.Names.count(Object) != 0)
        return Tr.Names[Object];

    Name = TrFormat("%s%u", Prefix, Tr.Counters[Prefix]);
    Tr.Counters[Prefix] += 1;
    Tr.Names[Object] = Name;
    return Name;
}

/* Reads the vtable byte offset out of an ARM64 virtual call thunk */
static
BOOLEAN
TrSlotFromThunk(
    _In_ const void* Thunk,
    _Out_ UINT* Slot)
{
    const ULONG* Code = static_cast<const ULONG*>(Thunk);
    UINT Index;
    UINT Offset = 0;
    UINT Loads = 0;

    for (Index = 0; Index < 8; Index++)
    {
        ULONG Instruction = Code[Index];

        /* LDR Xt, [Xn, #imm]; the first load reads the vtable, the second the method */
        if ((Instruction & 0xFFC00000) == 0xF9400000)
        {
            Offset = ((Instruction >> 10) & 0xFFF) * 8;
            Loads += 1;
        }

        /* BR Xn ends the thunk */
        if ((Instruction & 0xFFFFFC1F) == 0xD61F0000)
            break;
    }

    if (Loads != 2)
        return FALSE;

    *Slot = Offset / sizeof(void*);
    return TRUE;
}

/* The real method for an object, or the first one hooked when its vtable was folded into another */
static
void*
TrOriginal(
    _In_ const void* Object,
    _In_ UINT Slot,
    _In_ void* Fallback)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
    void** Table = *reinterpret_cast<void** const*>(Object);
    auto Found = Tr.Originals.find(Table);

    if (Found == Tr.Originals.end() || Found->second[Slot] == nullptr)
        return Fallback;
    return Found->second[Slot];
}

/* One call being printed */
class TrLine
{
public:
    TrLine(
        _In_z_ PCSTR Name,
        _In_ const void* This)
    {
        Active = Tr.Recording && TrDepth == 0;
        TrDepth += 1;
        if (!Active)
            return;

        Text = TrObjectName(This) + "." + Name + "(";
    }

    ~TrLine()
    {
        TrDepth -= 1;
    }

    BOOLEAN Active;

    template <typename T>
    VOID
    Arg(
        _In_ const T& Value)
    {
        Separate();
        Print(Value);
    }

    VOID
    SetCreated(
        _In_ const std::string& Name)
    {
        Created = Name;
    }

    VOID
    Result(
        _In_ HRESULT Value)
    {
        Tail = TrFormat(" = 0x%08lX", Value);
    }

    template <typename T>
    VOID
    Result(
        _In_ const T& Value)
    {
        if constexpr (std::is_integral_v<T>)
            Tail = TrFormat(" = %llu", static_cast<unsigned long long>(Value));
    }

    VOID
    Finish()
    {
        std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
        DWORD Thread = GetCurrentThreadId();
        UINT ThreadIndex;

        if (Tr.Threads.count(Thread) == 0)
            Tr.Threads[Thread] = static_cast<UINT>(Tr.Threads.size());
        ThreadIndex = Tr.Threads[Thread];

        Text += ")" + Tail;
        if (!Created.empty())
            Text += " -> " + Created;
        if (ThreadIndex != 0)
            Text = TrFormat("[t%u] ", ThreadIndex) + Text;
        Tr.Lines.push_back(Text);
    }

private:
    VOID
    Separate()
    {
        if (Text.back() != '(')
            Text += ", ";
    }


    template <typename T>
    VOID
    Print(
        _In_ const T& Value)
    {
        if constexpr (std::is_same_v<T, float>)
        {
            Text += TrFormat("%g", Value);
        }
        else if constexpr (std::is_same_v<T, UINT64>)
        {
            PrintAddress(Value);
        }
        else if constexpr (std::is_enum_v<T> || std::is_integral_v<T>)
        {
            LastCount = static_cast<UINT>(Value);
            Text += TrFormat("0x%llX", static_cast<unsigned long long>(Value));
        }
        else if constexpr (std::is_same_v<T, D3D12_CPU_DESCRIPTOR_HANDLE>)
        {
            PrintCpuHandle(Value);
        }
        else if constexpr (std::is_same_v<T, D3D12_GPU_DESCRIPTOR_HANDLE>)
        {
            PrintGpuHandle(Value);
        }
        else if constexpr (std::is_same_v<T, GUID>)
        {
            PrintIid(Value);
        }
        else if constexpr (std::is_pointer_v<T>)
        {
            PrintPointer(Value);
        }
        else
        {
            Text += "?";
        }
    }

    template <typename T>
    VOID
    PrintPointer(
        _In_ T Value)
    {
        using Target = std::remove_cv_t<std::remove_pointer_t<T>>;

        if (Value == nullptr)
        {
            Text += "null";
            return;
        }

        if constexpr (std::is_base_of_v<IUnknown, Target>)
            Text += TrObjectName(Value);
        else if constexpr (std::is_same_v<Target, D3D12_RESOURCE_DESC>)
            PrintResourceDesc(*Value);
        else if constexpr (std::is_same_v<Target, D3D12_HEAP_PROPERTIES>)
            Text += TrFormat("heap{type %u cpu %u pool %u}", Value->Type, Value->CPUPageProperty,
                             Value->MemoryPoolPreference);
        else if constexpr (std::is_same_v<Target, D3D12_CLEAR_VALUE>)
            Text += TrFormat("clear{fmt %u}", Value->Format);
        else if constexpr (std::is_same_v<Target, D3D12_COMMAND_QUEUE_DESC>)
            Text += TrFormat("queue{type %u prio %d flags 0x%X mask 0x%X}", Value->Type, Value->Priority,
                             Value->Flags, Value->NodeMask);
        else if constexpr (std::is_same_v<Target, D3D12_DESCRIPTOR_HEAP_DESC>)
            Text += TrFormat("dheap{type %u count %u flags 0x%X mask 0x%X}", Value->Type, Value->NumDescriptors,
                             Value->Flags, Value->NodeMask);
        else if constexpr (std::is_same_v<Target, D3D12_QUERY_HEAP_DESC>)
            Text += TrFormat("qheap{type %u count %u mask 0x%X}", Value->Type, Value->Count, Value->NodeMask);
        else if constexpr (std::is_same_v<Target, D3D12_HEAP_DESC>)
            Text += TrFormat("heapdesc{size 0x%llX type %u align 0x%llX flags 0x%X}", Value->SizeInBytes,
                             Value->Properties.Type, Value->Alignment, Value->Flags);
        else if constexpr (std::is_same_v<Target, D3D12_RESOURCE_BARRIER>)
            PrintBarriers(Value);
        else if constexpr (std::is_same_v<Target, ID3D12CommandList*>)
            PrintObjects(reinterpret_cast<IUnknown* const*>(Value));
        else if constexpr (std::is_same_v<Target, ID3D12DescriptorHeap*>)
            PrintObjects(reinterpret_cast<IUnknown* const*>(Value));
        else if constexpr (std::is_same_v<Target, ID3D12Pageable*>)
            PrintObjects(reinterpret_cast<IUnknown* const*>(Value));
        else if constexpr (std::is_same_v<Target, D3D12_VIEWPORT>)
            Text += TrFormat("vp{%g %g %g %g %g %g}", Value->TopLeftX, Value->TopLeftY, Value->Width, Value->Height,
                             Value->MinDepth, Value->MaxDepth);
        else if constexpr (std::is_same_v<Target, D3D12_RECT>)
            Text += TrFormat("rect{%ld %ld %ld %ld}", Value->left, Value->top, Value->right, Value->bottom);
        else if constexpr (std::is_same_v<Target, D3D12_CPU_DESCRIPTOR_HANDLE>)
            PrintCpuHandle(*Value);
        else if constexpr (std::is_same_v<Target, FLOAT>)
            Text += TrFormat("{%g %g %g %g}", Value[0], Value[1], Value[2], Value[3]);
        else if constexpr (std::is_same_v<Target, D3D12_TEXTURE_COPY_LOCATION>)
            PrintCopyLocation(*Value);
        else if constexpr (std::is_same_v<Target, D3D12_BOX>)
            Text += TrFormat("box{%u %u %u %u %u %u}", Value->left, Value->top, Value->front, Value->right,
                             Value->bottom, Value->back);
        else if constexpr (std::is_same_v<Target, D3D12_RANGE>)
            Text += TrFormat("range{0x%llX 0x%llX}", static_cast<UINT64>(Value->Begin),
                             static_cast<UINT64>(Value->End));
        else if constexpr (std::is_same_v<Target, D3D12_VERTEX_BUFFER_VIEW>)
            Text += "vbv{" + Address(Value->BufferLocation) + TrFormat(" %u %u}", Value->SizeInBytes,
                                                                        Value->StrideInBytes);
        else if constexpr (std::is_same_v<Target, D3D12_INDEX_BUFFER_VIEW>)
            Text += "ibv{" + Address(Value->BufferLocation) + TrFormat(" %u %u}", Value->SizeInBytes, Value->Format);
        else if constexpr (std::is_same_v<Target, D3D12_RENDER_TARGET_VIEW_DESC>)
            Text += TrFormat("rtv{fmt %u dim %u", Value->Format, Value->ViewDimension) + Words(&Value->Buffer, 4) + "}";
        else if constexpr (std::is_same_v<Target, D3D12_DEPTH_STENCIL_VIEW_DESC>)
            Text += TrFormat("dsv{fmt %u dim %u flags 0x%X", Value->Format, Value->ViewDimension, Value->Flags) +
                    Words(&Value->Texture1D, 3) + "}";
        else if constexpr (std::is_same_v<Target, D3D12_SHADER_RESOURCE_VIEW_DESC>)
            Text += TrFormat("srv{fmt %u dim %u map 0x%X", Value->Format, Value->ViewDimension,
                             Value->Shader4ComponentMapping) + Words(&Value->Buffer, 6) + "}";
        else if constexpr (std::is_same_v<Target, D3D12_UNORDERED_ACCESS_VIEW_DESC>)
            Text += TrFormat("uav{fmt %u dim %u", Value->Format, Value->ViewDimension) + Words(&Value->Buffer, 6) + "}";
        else if constexpr (std::is_same_v<Target, D3D12_CONSTANT_BUFFER_VIEW_DESC>)
            Text += "cbv{" + Address(Value->BufferLocation) + TrFormat(" %u}", Value->SizeInBytes);
        else if constexpr (std::is_same_v<Target, D3D12_SAMPLER_DESC>)
            Text += TrFormat("sampler{filter 0x%X addr %u %u %u bias %g aniso %u cmp %u border %g %g %g %g lod %g %g}",
                             Value->Filter, Value->AddressU, Value->AddressV, Value->AddressW, Value->MipLODBias,
                             Value->MaxAnisotropy, Value->ComparisonFunc, Value->BorderColor[0],
                             Value->BorderColor[1], Value->BorderColor[2], Value->BorderColor[3], Value->MinLOD,
                             Value->MaxLOD);
        else
            Text += "ptr";
    }

    /* Raw dwords of a view union, enough to tell two descriptions apart */
    static
    std::string
    Words(
        _In_ const void* Data,
        _In_ UINT Count)
    {
        const UINT* Each = static_cast<const UINT*>(Data);
        std::string Out;
        UINT Index;

        for (Index = 0; Index < Count; Index++)
            Out += TrFormat(" %X", Each[Index]);
        return Out;
    }

public:
    /* CheckFeatureSupport inputs: the feature and the leading dwords of its data */
    VOID
    FeatureInput(
        _In_ D3D12_FEATURE Feature,
        _In_ const void* Data,
        _In_ UINT Size)
    {
        Text += TrFormat("%u, {", Feature);
        if (Feature == D3D12_FEATURE_FEATURE_LEVELS)
        {
            const D3D12_FEATURE_DATA_FEATURE_LEVELS* Levels = static_cast<const D3D12_FEATURE_DATA_FEATURE_LEVELS*>(Data);
            UINT Index;

            for (Index = 0; Index < Levels->NumFeatureLevels; Index++)
                Text += TrFormat("%s%X", Index == 0 ? "" : " ", Levels->pFeatureLevelsRequested[Index]);
            Text += TrFormat(" max %X", Levels->MaxSupportedFeatureLevel);
        }
        else
        {
            UINT Inputs = 0;

            /* Only the fields a query reads; the rest is whatever the caller's memory held */
            if (Feature == D3D12_FEATURE_ARCHITECTURE || Feature == D3D12_FEATURE_ARCHITECTURE1 ||
                Feature == D3D12_FEATURE_FORMAT_SUPPORT || Feature == D3D12_FEATURE_FORMAT_INFO ||
                Feature == D3D12_FEATURE_SHADER_MODEL)
            {
                Inputs = 1;
            }
            else if (Feature == D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS)
            {
                Inputs = 3;
            }

            if (Inputs != 0 && Size >= Inputs * 4)
                Text += Words(Data, Inputs).substr(1);
        }
        Text += TrFormat("}, 0x%X", Size);
    }

private:
    VOID
    PrintObjects(
        _In_ IUnknown* const* Objects)
    {
        UINT Index;

        Text += "[";
        for (Index = 0; Index < LastCount && Index < 16; Index++)
        {
            if (Index != 0)
                Text += " ";
            Text += TrObjectName(Objects[Index]);
        }
        Text += "]";
    }

    VOID
    PrintResourceDesc(
        _In_ const D3D12_RESOURCE_DESC& Desc)
    {
        Text += TrFormat("desc{dim %u align 0x%llX %llux%ux%u mips %u fmt %u ms %u/%u layout %u flags 0x%X}",
                         Desc.Dimension, Desc.Alignment, Desc.Width, Desc.Height, Desc.DepthOrArraySize,
                         Desc.MipLevels, Desc.Format, Desc.SampleDesc.Count, Desc.SampleDesc.Quality, Desc.Layout,
                         Desc.Flags);
    }

    VOID
    PrintBarriers(
        _In_ const D3D12_RESOURCE_BARRIER* Barriers)
    {
        UINT Index;

        Text += "[";
        for (Index = 0; Index < LastCount; Index++)
        {
            const D3D12_RESOURCE_BARRIER& Each = Barriers[Index];

            if (Index != 0)
                Text += " ";
            if (Each.Type == D3D12_RESOURCE_BARRIER_TYPE_TRANSITION)
            {
                Text += TrFormat("trans{%s sub 0x%X 0x%X->0x%X f%u}", TrObjectName(Each.Transition.pResource).c_str(),
                                 Each.Transition.Subresource, Each.Transition.StateBefore,
                                 Each.Transition.StateAfter, Each.Flags);
            }
            else if (Each.Type == D3D12_RESOURCE_BARRIER_TYPE_UAV)
            {
                Text += "uav{" + TrObjectName(Each.UAV.pResource) + "}";
            }
            else
            {
                Text += "alias{" + TrObjectName(Each.Aliasing.pResourceBefore) + " " +
                        TrObjectName(Each.Aliasing.pResourceAfter) + "}";
            }
        }
        Text += "]";
    }

    VOID
    PrintCopyLocation(
        _In_ const D3D12_TEXTURE_COPY_LOCATION& Location)
    {
        if (Location.Type == D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX)
        {
            Text += TrFormat("loc{%s sub %u}", TrObjectName(Location.pResource).c_str(), Location.SubresourceIndex);
            return;
        }

        Text += TrFormat("loc{%s off 0x%llX fmt %u %ux%ux%u pitch %u}", TrObjectName(Location.pResource).c_str(),
                         Location.PlacedFootprint.Offset, Location.PlacedFootprint.Footprint.Format,
                         Location.PlacedFootprint.Footprint.Width, Location.PlacedFootprint.Footprint.Height,
                         Location.PlacedFootprint.Footprint.Depth, Location.PlacedFootprint.Footprint.RowPitch);
    }

    VOID
    PrintIid(
        _In_ const GUID& Iid);

    VOID
    PrintCpuHandle(
        _In_ D3D12_CPU_DESCRIPTOR_HANDLE Handle)
    {
        std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);

        for (const Tr12DescriptorRange& Range : Tr.Ranges)
        {
            if (Handle.ptr >= Range.CpuStart && Handle.ptr < Range.CpuStart + SIZE_T(Range.Count) * Range.Increment)
            {
                Text += TrFormat("%s[%u]", Range.Name.c_str(),
                                 static_cast<UINT>((Handle.ptr - Range.CpuStart) / Range.Increment));
                return;
            }
        }
        Text += Handle.ptr == 0 ? "cpu0" : "cpu?";
    }

    VOID
    PrintGpuHandle(
        _In_ D3D12_GPU_DESCRIPTOR_HANDLE Handle)
    {
        std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);

        for (const Tr12DescriptorRange& Range : Tr.Ranges)
        {
            if (Range.GpuStart != 0 && Handle.ptr >= Range.GpuStart &&
                Handle.ptr < Range.GpuStart + UINT64(Range.Count) * Range.Increment)
            {
                Text += TrFormat("%s.gpu[%u]", Range.Name.c_str(),
                                 static_cast<UINT>((Handle.ptr - Range.GpuStart) / Range.Increment));
                return;
            }
        }
        Text += Handle.ptr == 0 ? "gpu0" : "gpu?";
    }

    std::string
    Address(
        _In_ UINT64 Value)
    {
        std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
        auto Found = Tr.Buffers.upper_bound(Value);

        if (Found != Tr.Buffers.begin())
        {
            --Found;
            if (Value >= Found->first && Value < Found->first + Found->second.Size)
                return TrFormat("%s+0x%llX", Found->second.Name.c_str(), Value - Found->first);
        }
        return TrFormat("0x%llX", Value);
    }

    VOID
    PrintAddress(
        _In_ UINT64 Value)
    {
        Text += Address(Value);
    }

    std::string Text;
    std::string Tail;
    std::string Created;
    UINT LastCount = 0;
    const GUID* LastIid = nullptr;
};

static
VOID
TrInstallFamily(
    _In_ void* Object,
    _In_ Tr12Family Family);

template <auto Method>
constexpr
bool
TrIsFeatureQuery()
{
    if constexpr (std::is_same_v<decltype(Method), decltype(&ID3D12Device::CheckFeatureSupport)>)
        return Method == &ID3D12Device::CheckFeatureSupport;
    else
        return false;
}

/* Wrapper for one traced method */
template <auto Method>
struct TrHook;

template <typename R, typename C, typename... A, R (STDMETHODCALLTYPE C::*Method)(A...)>
struct TrHook<Method>
{
    using Function = R (STDMETHODCALLTYPE*)(C*, A...);

    static inline UINT Slot;
    static inline PCSTR Name;
    static inline void* Fallback;
    static inline BOOLEAN IsFeatureQuery;

    static
    R
    STDMETHODCALLTYPE
    Call(
        C* This,
        A... Arguments)
    {
        Function Original = reinterpret_cast<Function>(TrOriginal(This, Slot, Fallback));
        TrLine Line(Name, This);

        if constexpr (std::is_same_v<std::tuple<A...>, std::tuple<D3D12_FEATURE, void*, UINT>>)
        {
            if (Line.Active && IsFeatureQuery)
                Line.FeatureInput(Arguments...);
            else if (Line.Active)
                (Line.Arg(Arguments), ...);
        }
        else
        {
            if (Line.Active)
                (Line.Arg(Arguments), ...);
        }

        if constexpr (std::is_void_v<R>)
        {
            Original(This, Arguments...);
            if (Line.Active)
                Line.Finish();
        }
        else
        {
            R Result = Original(This, Arguments...);

            if (Line.Active)
            {
                Line.Result(Result);
                Line.Finish();
            }
            return Result;
        }
    }
};

/* Methods that hand back objects register them even when not recording */
template <auto Method>
struct TrCreateHook;

template <typename R, typename C, typename... A, R (STDMETHODCALLTYPE C::*Method)(A...)>
struct TrCreateHook<Method>
{
    using Function = R (STDMETHODCALLTYPE*)(C*, A...);

    static inline UINT Slot;
    static inline PCSTR Name;
    static inline void* Fallback;

    static
    R
    STDMETHODCALLTYPE
    Call(
        C* This,
        A... Arguments);
};

template <auto Method>
static
Tr12Slot
TrSlot(
    _In_z_ PCSTR Name)
{
    auto Pointer = Method;
    Tr12Slot Slot;

    TrHook<Method>::Name = Name;
    Slot.Name = Name;
    Slot.Thunk = *reinterpret_cast<void**>(&Pointer);
    Slot.Hook = reinterpret_cast<void*>(&TrHook<Method>::Call);
    Slot.SlotIndex = &TrHook<Method>::Slot;
    Slot.Fallback = &TrHook<Method>::Fallback;
    return Slot;
}

static
Tr12Slot
TrFeatureSlot(
    _In_z_ PCSTR Name)
{
    TrHook<&ID3D12Device::CheckFeatureSupport>::IsFeatureQuery = TRUE;
    return TrSlot<&ID3D12Device::CheckFeatureSupport>(Name);
}

template <auto Method>
static
Tr12Slot
TrCreate(
    _In_z_ PCSTR Name)
{
    auto Pointer = Method;
    Tr12Slot Slot;

    TrCreateHook<Method>::Name = Name;
    Slot.Name = Name;
    Slot.Thunk = *reinterpret_cast<void**>(&Pointer);
    Slot.Hook = reinterpret_cast<void*>(&TrCreateHook<Method>::Call);
    Slot.SlotIndex = &TrCreateHook<Method>::Slot;
    Slot.Fallback = &TrCreateHook<Method>::Fallback;
    return Slot;
}

struct TrIidName
{
    const GUID* Iid;
    PCSTR Name;
    PCSTR Prefix;
    Tr12Family Family;
};

static const TrIidName TrIids[] =
{
    { &__uuidof(ID3D12Device), "Device", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12Device1), "Device1", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12Device2), "Device2", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12Device3), "Device3", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12Device4), "Device4", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12Device5), "Device5", "dev", Tr12FamilyDevice },
    { &__uuidof(ID3D12CommandQueue), "CommandQueue", "queue", Tr12FamilyQueue },
    { &__uuidof(ID3D12CommandAllocator), "CommandAllocator", "alloc", Tr12FamilyAllocator },
    { &__uuidof(ID3D12GraphicsCommandList), "GraphicsCommandList", "list", Tr12FamilyList },
    { &__uuidof(ID3D12GraphicsCommandList1), "GraphicsCommandList1", "list", Tr12FamilyList },
    { &__uuidof(ID3D12GraphicsCommandList2), "GraphicsCommandList2", "list", Tr12FamilyList },
    { &__uuidof(ID3D12GraphicsCommandList3), "GraphicsCommandList3", "list", Tr12FamilyList },
    { &__uuidof(ID3D12GraphicsCommandList4), "GraphicsCommandList4", "list", Tr12FamilyList },
    { &__uuidof(ID3D12CommandList), "CommandList", "list", Tr12FamilyList },
    { &__uuidof(ID3D12Fence), "Fence", "fence", Tr12FamilyFence },
    { &__uuidof(ID3D12Fence1), "Fence1", "fence", Tr12FamilyFence },
    { &__uuidof(ID3D12Resource), "Resource", "res", Tr12FamilyResource },
    { &__uuidof(ID3D12Resource1), "Resource1", "res", Tr12FamilyResource },
    { &__uuidof(ID3D12Resource2), "Resource2", "res", Tr12FamilyResource },
    { &__uuidof(ID3D12Heap), "Heap", "heap", Tr12FamilyHeap },
    { &__uuidof(ID3D12Heap1), "Heap1", "heap", Tr12FamilyHeap },
    { &__uuidof(ID3D12DescriptorHeap), "DescriptorHeap", "dheap", Tr12FamilyDescriptorHeap },
    { &__uuidof(ID3D12RootSignature), "RootSignature", "rs", Tr12FamilyOther },
    { &__uuidof(ID3D12PipelineState), "PipelineState", "pso", Tr12FamilyOther },
    { &__uuidof(ID3D12QueryHeap), "QueryHeap", "qheap", Tr12FamilyOther },
    { &__uuidof(ID3D12CommandSignature), "CommandSignature", "sig", Tr12FamilyOther },
    { &__uuidof(ID3D12PipelineLibrary), "PipelineLibrary", "lib", Tr12FamilyOther },
    { &__uuidof(ID3D12PipelineLibrary1), "PipelineLibrary1", "lib", Tr12FamilyOther },
};

VOID
TrLine::PrintIid(
    _In_ const GUID& Iid)
{
    for (const TrIidName& Each : TrIids)
    {
        if (IsEqualGUID(*Each.Iid, Iid))
        {
            Text += Each.Name;
            LastIid = Each.Iid;
            return;
        }
    }

    LastIid = &Iid;
    Text += TrFormat("{%08lX-%04X-%04X}", Iid.Data1, Iid.Data2, Iid.Data3);
}

static
const TrIidName*
TrFindIid(
    _In_opt_ const GUID* Iid)
{
    if (Iid == nullptr)
        return nullptr;

    for (const TrIidName& Each : TrIids)
    {
        if (IsEqualGUID(*Each.Iid, *Iid))
            return &Each;
    }
    return nullptr;
}

/* Names a new object, notes where its descriptors or GPU addresses sit and hooks its vtable */
static
std::string
TrAdopt(
    _In_ void* Object,
    _In_opt_ const GUID* Iid,
    _In_ BOOLEAN Fresh)
{
    const TrIidName* Kind = TrFindIid(Iid);
    std::string Name;

    TrDepth += 1;
    if (Fresh)
        Tr.Names.erase(Object);
    Name = TrNameObject(Object, Kind != nullptr ? Kind->Prefix : "obj");
    if (Kind != nullptr)
    {
        if (Kind->Family == Tr12FamilyDescriptorHeap)
        {
            ID3D12DescriptorHeap* Heap = static_cast<ID3D12DescriptorHeap*>(Object);
            D3D12_DESCRIPTOR_HEAP_DESC Desc = Heap->GetDesc();
            ID3D12Device* Device = nullptr;
            Tr12DescriptorRange Range;

            Heap->GetDevice(IID_PPV_ARGS(&Device));
            Range.CpuStart = Heap->GetCPUDescriptorHandleForHeapStart().ptr;
            Range.GpuStart = (Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0 ?
                             Heap->GetGPUDescriptorHandleForHeapStart().ptr : 0;
            Range.Increment = Device != nullptr ? Device->GetDescriptorHandleIncrementSize(Desc.Type) : 1;
            Range.Count = Desc.NumDescriptors;
            Range.Name = Name;
            if (Device != nullptr)
                Device->Release();

            std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
            Tr.Ranges.push_back(Range);
        }
        else if (Kind->Family == Tr12FamilyResource)
        {
            ID3D12Resource* Resource = static_cast<ID3D12Resource*>(Object);
            D3D12_RESOURCE_DESC Desc = Resource->GetDesc();

            if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
            {
                UINT64 Start = Resource->GetGPUVirtualAddress();

                if (Start != 0)
                {
                    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
                    Tr.Buffers[Start] = { Desc.Width, Name };
                }
            }
        }

        TrInstallFamily(Object, Kind->Family);
    }
    TrDepth -= 1;
    return Name;
}


template <typename R, typename C, typename... A, R (STDMETHODCALLTYPE C::*Method)(A...)>
R
STDMETHODCALLTYPE
TrCreateHook<Method>::Call(
    C* This,
    A... Arguments)
{
    Function Original = reinterpret_cast<Function>(TrOriginal(This, Slot, Fallback));
    TrLine Line(Name, This);
    const GUID* Iid = nullptr;
    void** Out = nullptr;
    R Result;

    if (Line.Active)
        (Line.Arg(Arguments), ...);

    Result = Original(This, Arguments...);

    auto Note = [&](const auto& Value)
    {
        using T = std::decay_t<decltype(Value)>;

        if constexpr (std::is_same_v<T, GUID>)
            Iid = &Value;
        else if constexpr (std::is_same_v<T, void**>)
            Out = Value;
    };
    (Note(Arguments), ...);

    if (SUCCEEDED(Result) && Out != nullptr && *Out != nullptr)
    {
        std::string Created = TrAdopt(*Out, Iid, strcmp(Name, "QueryInterface") != 0);

        if (Line.Active)
            Line.SetCreated(Created);
    }

    if (Line.Active)
    {
        Line.Result(Result);
        Line.Finish();
    }

    return Result;
}

static
std::vector<Tr12Slot>
TrFamilySlots(
    _In_ Tr12Family Family)
{
    switch (Family)
    {
        case Tr12FamilyDevice:
            return {
                TrCreate<static_cast<HRESULT (STDMETHODCALLTYPE IUnknown::*)(REFIID, void**)>(&IUnknown::QueryInterface)>(
                    "QueryInterface"),
                TrSlot<&ID3D12Device::SetName>("SetName"),
                TrSlot<&ID3D12Device::SetPrivateData>("SetPrivateData"),
                TrSlot<&ID3D12Device::SetPrivateDataInterface>("SetPrivateDataInterface"),
                TrSlot<&ID3D12Device::GetPrivateData>("GetPrivateData"),
                TrSlot<&ID3D12Device::GetNodeCount>("GetNodeCount"),
                TrCreate<&ID3D12Device::CreateCommandQueue>("CreateCommandQueue"),
                TrCreate<&ID3D12Device::CreateCommandAllocator>("CreateCommandAllocator"),
                TrCreate<&ID3D12Device::CreateGraphicsPipelineState>("CreateGraphicsPipelineState"),
                TrCreate<&ID3D12Device::CreateComputePipelineState>("CreateComputePipelineState"),
                TrCreate<&ID3D12Device::CreateCommandList>("CreateCommandList"),
                TrFeatureSlot("CheckFeatureSupport"),
                TrCreate<&ID3D12Device::CreateDescriptorHeap>("CreateDescriptorHeap"),
                TrSlot<&ID3D12Device::GetDescriptorHandleIncrementSize>("GetDescriptorHandleIncrementSize"),
                TrCreate<&ID3D12Device::CreateRootSignature>("CreateRootSignature"),
                TrSlot<&ID3D12Device::CreateConstantBufferView>("CreateConstantBufferView"),
                TrSlot<&ID3D12Device::CreateShaderResourceView>("CreateShaderResourceView"),
                TrSlot<&ID3D12Device::CreateUnorderedAccessView>("CreateUnorderedAccessView"),
                TrSlot<&ID3D12Device::CreateRenderTargetView>("CreateRenderTargetView"),
                TrSlot<&ID3D12Device::CreateDepthStencilView>("CreateDepthStencilView"),
                TrSlot<&ID3D12Device::CreateSampler>("CreateSampler"),
                TrSlot<&ID3D12Device::CopyDescriptors>("CopyDescriptors"),
                TrSlot<&ID3D12Device::CopyDescriptorsSimple>("CopyDescriptorsSimple"),
                TrCreate<&ID3D12Device::CreateCommittedResource>("CreateCommittedResource"),
                TrCreate<&ID3D12Device::CreateHeap>("CreateHeap"),
                TrCreate<&ID3D12Device::CreatePlacedResource>("CreatePlacedResource"),
                TrCreate<&ID3D12Device::CreateReservedResource>("CreateReservedResource"),
                TrSlot<&ID3D12Device::CreateSharedHandle>("CreateSharedHandle"),
                TrCreate<&ID3D12Device::OpenSharedHandle>("OpenSharedHandle"),
                TrSlot<&ID3D12Device::MakeResident>("MakeResident"),
                TrSlot<&ID3D12Device::Evict>("Evict"),
                TrCreate<&ID3D12Device::CreateFence>("CreateFence"),
                TrSlot<&ID3D12Device::GetDeviceRemovedReason>("GetDeviceRemovedReason"),
                TrSlot<&ID3D12Device::GetCopyableFootprints>("GetCopyableFootprints"),
                TrCreate<&ID3D12Device::CreateQueryHeap>("CreateQueryHeap"),
                TrSlot<&ID3D12Device::SetStablePowerState>("SetStablePowerState"),
                TrCreate<&ID3D12Device::CreateCommandSignature>("CreateCommandSignature"),
                TrSlot<&ID3D12Device::GetResourceTiling>("GetResourceTiling"),
                TrCreate<&ID3D12Device1::CreatePipelineLibrary>("CreatePipelineLibrary"),
                TrSlot<&ID3D12Device1::SetEventOnMultipleFenceCompletion>("SetEventOnMultipleFenceCompletion"),
                TrSlot<&ID3D12Device1::SetResidencyPriority>("SetResidencyPriority"),
                TrCreate<&ID3D12Device2::CreatePipelineState>("CreatePipelineState"),
                TrSlot<&ID3D12Device3::EnqueueMakeResident>("EnqueueMakeResident"),
                TrCreate<&ID3D12Device4::CreateCommandList1>("CreateCommandList1"),
                TrCreate<&ID3D12Device4::CreateCommittedResource1>("CreateCommittedResource1"),
                TrCreate<&ID3D12Device4::CreateHeap1>("CreateHeap1"),
                TrCreate<&ID3D12Device4::CreateReservedResource1>("CreateReservedResource1"),
            };

        case Tr12FamilyQueue:
            return {
                TrSlot<&ID3D12CommandQueue::SetName>("SetName"),
                TrSlot<&ID3D12CommandQueue::SetPrivateData>("SetPrivateData"),
                TrSlot<&ID3D12CommandQueue::UpdateTileMappings>("UpdateTileMappings"),
                TrSlot<&ID3D12CommandQueue::CopyTileMappings>("CopyTileMappings"),
                TrSlot<&ID3D12CommandQueue::ExecuteCommandLists>("ExecuteCommandLists"),
                TrSlot<&ID3D12CommandQueue::SetMarker>("SetMarker"),
                TrSlot<&ID3D12CommandQueue::BeginEvent>("BeginEvent"),
                TrSlot<&ID3D12CommandQueue::EndEvent>("EndEvent"),
                TrSlot<&ID3D12CommandQueue::Signal>("Signal"),
                TrSlot<&ID3D12CommandQueue::Wait>("Wait"),
                TrSlot<&ID3D12CommandQueue::GetTimestampFrequency>("GetTimestampFrequency"),
                TrSlot<&ID3D12CommandQueue::GetClockCalibration>("GetClockCalibration"),
            };

        case Tr12FamilyAllocator:
            return {
                TrSlot<&ID3D12CommandAllocator::SetName>("SetName"),
                TrSlot<&ID3D12CommandAllocator::Reset>("Reset"),
            };

        case Tr12FamilyFence:
            return {
                TrSlot<&ID3D12Fence::SetName>("SetName"),
                TrSlot<&ID3D12Fence::SetEventOnCompletion>("SetEventOnCompletion"),
                TrSlot<&ID3D12Fence::Signal>("Signal"),
            };

        case Tr12FamilyResource:
            return {
                TrSlot<&ID3D12Resource::SetName>("SetName"),
                TrSlot<&ID3D12Resource::SetPrivateData>("SetPrivateData"),
                TrSlot<&ID3D12Resource::SetPrivateDataInterface>("SetPrivateDataInterface"),
                TrSlot<&ID3D12Resource::Map>("Map"),
                TrSlot<&ID3D12Resource::Unmap>("Unmap"),
                TrSlot<&ID3D12Resource::WriteToSubresource>("WriteToSubresource"),
                TrSlot<&ID3D12Resource::ReadFromSubresource>("ReadFromSubresource"),
            };

        case Tr12FamilyHeap:
        case Tr12FamilyDescriptorHeap:
        case Tr12FamilyOther:
            return {
                TrSlot<&ID3D12Pageable::SetName>("SetName"),
            };

        case Tr12FamilyList:
            return {
                TrSlot<&ID3D12GraphicsCommandList::SetName>("SetName"),
                TrSlot<&ID3D12GraphicsCommandList::Close>("Close"),
                TrSlot<&ID3D12GraphicsCommandList::Reset>("Reset"),
                TrSlot<&ID3D12GraphicsCommandList::ClearState>("ClearState"),
                TrSlot<&ID3D12GraphicsCommandList::DrawInstanced>("DrawInstanced"),
                TrSlot<&ID3D12GraphicsCommandList::DrawIndexedInstanced>("DrawIndexedInstanced"),
                TrSlot<&ID3D12GraphicsCommandList::Dispatch>("Dispatch"),
                TrSlot<&ID3D12GraphicsCommandList::CopyBufferRegion>("CopyBufferRegion"),
                TrSlot<&ID3D12GraphicsCommandList::CopyTextureRegion>("CopyTextureRegion"),
                TrSlot<&ID3D12GraphicsCommandList::CopyResource>("CopyResource"),
                TrSlot<&ID3D12GraphicsCommandList::CopyTiles>("CopyTiles"),
                TrSlot<&ID3D12GraphicsCommandList::ResolveSubresource>("ResolveSubresource"),
                TrSlot<&ID3D12GraphicsCommandList::IASetPrimitiveTopology>("IASetPrimitiveTopology"),
                TrSlot<&ID3D12GraphicsCommandList::RSSetViewports>("RSSetViewports"),
                TrSlot<&ID3D12GraphicsCommandList::RSSetScissorRects>("RSSetScissorRects"),
                TrSlot<&ID3D12GraphicsCommandList::OMSetBlendFactor>("OMSetBlendFactor"),
                TrSlot<&ID3D12GraphicsCommandList::OMSetStencilRef>("OMSetStencilRef"),
                TrSlot<&ID3D12GraphicsCommandList::SetPipelineState>("SetPipelineState"),
                TrSlot<&ID3D12GraphicsCommandList::ResourceBarrier>("ResourceBarrier"),
                TrSlot<&ID3D12GraphicsCommandList::ExecuteBundle>("ExecuteBundle"),
                TrSlot<&ID3D12GraphicsCommandList::SetDescriptorHeaps>("SetDescriptorHeaps"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRootSignature>("SetComputeRootSignature"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRootSignature>("SetGraphicsRootSignature"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRootDescriptorTable>("SetComputeRootDescriptorTable"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable>("SetGraphicsRootDescriptorTable"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRoot32BitConstant>("SetComputeRoot32BitConstant"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant>("SetGraphicsRoot32BitConstant"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRoot32BitConstants>("SetComputeRoot32BitConstants"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstants>("SetGraphicsRoot32BitConstants"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRootConstantBufferView>("SetComputeRootConstantBufferView"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView>(
                    "SetGraphicsRootConstantBufferView"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRootShaderResourceView>("SetComputeRootShaderResourceView"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView>(
                    "SetGraphicsRootShaderResourceView"),
                TrSlot<&ID3D12GraphicsCommandList::SetComputeRootUnorderedAccessView>(
                    "SetComputeRootUnorderedAccessView"),
                TrSlot<&ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView>(
                    "SetGraphicsRootUnorderedAccessView"),
                TrSlot<&ID3D12GraphicsCommandList::IASetIndexBuffer>("IASetIndexBuffer"),
                TrSlot<&ID3D12GraphicsCommandList::IASetVertexBuffers>("IASetVertexBuffers"),
                TrSlot<&ID3D12GraphicsCommandList::SOSetTargets>("SOSetTargets"),
                TrSlot<&ID3D12GraphicsCommandList::OMSetRenderTargets>("OMSetRenderTargets"),
                TrSlot<&ID3D12GraphicsCommandList::ClearDepthStencilView>("ClearDepthStencilView"),
                TrSlot<&ID3D12GraphicsCommandList::ClearRenderTargetView>("ClearRenderTargetView"),
                TrSlot<&ID3D12GraphicsCommandList::ClearUnorderedAccessViewUint>("ClearUnorderedAccessViewUint"),
                TrSlot<&ID3D12GraphicsCommandList::ClearUnorderedAccessViewFloat>("ClearUnorderedAccessViewFloat"),
                TrSlot<&ID3D12GraphicsCommandList::DiscardResource>("DiscardResource"),
                TrSlot<&ID3D12GraphicsCommandList::BeginQuery>("BeginQuery"),
                TrSlot<&ID3D12GraphicsCommandList::EndQuery>("EndQuery"),
                TrSlot<&ID3D12GraphicsCommandList::ResolveQueryData>("ResolveQueryData"),
                TrSlot<&ID3D12GraphicsCommandList::SetPredication>("SetPredication"),
                TrSlot<&ID3D12GraphicsCommandList::SetMarker>("SetMarker"),
                TrSlot<&ID3D12GraphicsCommandList::BeginEvent>("BeginEvent"),
                TrSlot<&ID3D12GraphicsCommandList::EndEvent>("EndEvent"),
                TrSlot<&ID3D12GraphicsCommandList::ExecuteIndirect>("ExecuteIndirect"),
                TrSlot<&ID3D12GraphicsCommandList1::OMSetDepthBounds>("OMSetDepthBounds"),
                TrSlot<&ID3D12GraphicsCommandList1::SetSamplePositions>("SetSamplePositions"),
                TrSlot<&ID3D12GraphicsCommandList1::ResolveSubresourceRegion>("ResolveSubresourceRegion"),
                TrSlot<&ID3D12GraphicsCommandList1::SetViewInstanceMask>("SetViewInstanceMask"),
                TrSlot<&ID3D12GraphicsCommandList2::WriteBufferImmediate>("WriteBufferImmediate"),
            };

        default:
            return {};
    }
}

static
VOID
TrInstallFamily(
    _In_ void* Object,
    _In_ Tr12Family Family)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
    void** Table = *reinterpret_cast<void***>(Object);
    std::vector<void*>& Saved = Tr.Originals[Table];
    DWORD OldProtect;

    if (!Saved.empty())
        return;

    Saved.assign(160, nullptr);
    for (const Tr12Slot& Each : TrFamilySlots(Family))
    {
        UINT Slot;

        if (!TrSlotFromThunk(Each.Thunk, &Slot) || Slot >= Saved.size())
        {
            fprintf(stderr, "trace12: no slot for %s\n", Each.Name);
            continue;
        }

        *Each.SlotIndex = Slot;
        if (Table[Slot] == Each.Hook)
            continue;

        Saved[Slot] = Table[Slot];
        if (*Each.Fallback == nullptr)
            *Each.Fallback = Table[Slot];
        if (VirtualProtect(&Table[Slot], sizeof(void*), PAGE_READWRITE, &OldProtect))
        {
            Table[Slot] = Each.Hook;
            VirtualProtect(&Table[Slot], sizeof(void*), OldProtect, &OldProtect);
        }
    }
}

BOOLEAN
Tr12Install(
    _In_ ID3D12Device* Device,
    _In_ ID3D12CommandQueue* Queue)
{
    Tr12Name(Device, "dev");
    Tr12Name(Queue, "queue");
    TrInstallFamily(Device, Tr12FamilyDevice);
    TrInstallFamily(Queue, Tr12FamilyQueue);
    return TRUE;
}

VOID
Tr12Name(
    _In_ IUnknown* Object,
    _In_z_ PCSTR Name)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);

    Tr.Names[Object] = Name;
    Tr.Pinned[Object] = Name;
}

VOID
Tr12Reset(VOID)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);

    Tr.Names = Tr.Pinned;
    Tr.Counters.clear();
    Tr.Ranges.clear();
    Tr.Buffers.clear();
}

VOID
Tr12Begin(VOID)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);

    Tr.Lines.clear();
    Tr.Threads.clear();
    Tr.Threads[GetCurrentThreadId()] = 0;
    Tr.Recording = TRUE;
}

std::vector<std::string>
Tr12End(VOID)
{
    std::lock_guard<std::recursive_mutex> Guard(Tr.Lock);
    std::vector<std::string> Lines;

    Tr.Recording = FALSE;
    Lines.swap(Tr.Lines);
    return Lines;
}
