/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Raytracing state objects: checking a desc, resolving associations, and the driver form
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "stateobject.hpp"

/* State object config bits raytracing state objects may carry */
constexpr UINT RxConfigFlagMask = D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS |
                                  D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS |
                                  D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS;

constexpr UINT RxSkipFlags = D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES |
                             D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES;

/* What stack size queries answer when the driver is not asked */
constexpr UINT64 RxNoStackSize = 0xFFFFFFFF;

/* Two associated subobjects that would mean the same to a shader */
static
BOOLEAN
RxSameDefinition(
    _In_opt_ const RxSubobjectRecord* First,
    _In_opt_ const RxSubobjectRecord* Second)
{
    if (First == Second)
        return TRUE;
    if (First == nullptr || Second == nullptr)
        return FALSE;
    return First->Slot == Second->Slot && First->Root == Second->Root && First->Values[0] == Second->Values[0] &&
           First->Values[1] == Second->Values[1];
}

static
BOOLEAN
RxIsRaytracingShader(
    _In_ UINT Kind)
{
    return Kind >= RxDxilKindRayGeneration && Kind <= RxDxilKindCallable;
}

/* Library functions take root signatures, node masks and configs; shaders take every kind */
static
BOOLEAN
RxSlotFits(
    _In_ RxSlot Slot,
    _In_ UINT Kind)
{
    if (RxIsRaytracingShader(Kind))
        return TRUE;
    return Slot == RxSlotGlobalRoot || Slot == RxSlotLocalRoot || Slot == RxSlotNodeMask || Slot == RxSlotConfig;
}

_Ret_maybenull_
static
const RxSubobjectRecord*
RxFirstInSlot(
    _In_ const RxFunctionExport& Function,
    _In_ RxSlot Slot)
{
    for (UINT Index = 0; Index < Function.AssociationCount; Index++)
    {
        if (Function.Associations[Index]->Slot == Slot)
            return Function.Associations[Index];
    }
    return nullptr;
}

/* Whether one parameter set binds every register of a range a function reads */
static
BOOLEAN
RxRootBindsRegister(
    _In_ const D3D12_ROOT_SIGNATURE_DESC2& Layout,
    _In_ UINT Class,
    _In_ UINT Space,
    _In_ UINT Register)
{
    for (UINT Index = 0; Index < Layout.NumParameters; Index++)
    {
        const D3D12_ROOT_PARAMETER1& Parameter = Layout.pParameters[Index];

        /* Raytracing shaders see only what every stage sees */
        if (Parameter.ShaderVisibility != D3D12_SHADER_VISIBILITY_ALL)
            continue;

        switch (Parameter.ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (UINT Range = 0; Range < Parameter.DescriptorTable.NumDescriptorRanges; Range++)
                {
                    const D3D12_DESCRIPTOR_RANGE1& Each = Parameter.DescriptorTable.pDescriptorRanges[Range];

                    if ((UINT)Each.RangeType != Class || Each.RegisterSpace != Space || Register < Each.BaseShaderRegister)
                        continue;
                    if (Each.NumDescriptors == UINT_MAX || Register - Each.BaseShaderRegister < Each.NumDescriptors)
                        return TRUE;
                }
                break;

            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                if (Class == RxDxilClassCbv && Parameter.Constants.RegisterSpace == Space &&
                    Parameter.Constants.ShaderRegister == Register)
                {
                    return TRUE;
                }
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
            {
                UINT Wanted = Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_CBV   ? RxDxilClassCbv
                              : Parameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_SRV ? RxDxilClassSrv
                                                                                         : RxDxilClassUav;

                if (Class == Wanted && Parameter.Descriptor.RegisterSpace == Space &&
                    Parameter.Descriptor.ShaderRegister == Register)
                {
                    return TRUE;
                }
                break;
            }

            default:
                break;
        }
    }

    if (Class == RxDxilClassSampler)
    {
        for (UINT Index = 0; Index < Layout.NumStaticSamplers; Index++)
        {
            const D3D12_STATIC_SAMPLER_DESC1& Sampler = Layout.pStaticSamplers[Index];

            if (Sampler.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL && Sampler.RegisterSpace == Space &&
                Sampler.ShaderRegister == Register)
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* Unbounded or very wide ranges must sit in one unbounded table range */
static
BOOLEAN
RxRootsBind(
    _In_opt_ const RxRootSignature* Global,
    _In_opt_ const RxRootSignature* Local,
    _In_ const RxDxilBinding& Binding)
{
    const RxRootSignature* Roots[2] = { Global, Local };
    UINT Last = Binding.Upper;

    if (Binding.Upper < Binding.Lower)
        return FALSE;
    if (Binding.Upper - Binding.Lower > 256)
        Last = Binding.Lower;

    for (UINT Register = Binding.Lower; Register <= Last; Register++)
    {
        BOOLEAN Bound = FALSE;

        for (UINT Index = 0; Index < _countof(Roots) && !Bound; Index++)
        {
            if (Roots[Index] != nullptr)
                Bound = RxRootBindsRegister(Roots[Index]->GetLayout(), Binding.Class, Binding.Space, Register);
        }
        if (!Bound)
            return FALSE;
        if (Register == UINT_MAX)
            break;
    }
    return TRUE;
}

/* One register range a root signature binds */
struct RxRegisterSpan
{
    UINT Class;
    UINT Space;
    UINT First;
    UINT64 Last;
};

static
HRESULT
RxAddSpan(
    _In_ RxArena& Arena,
    _Inout_ RxGrowList<RxRegisterSpan>* Spans,
    _In_ UINT Class,
    _In_ UINT Space,
    _In_ UINT First,
    _In_ UINT Count)
{
    RxRegisterSpan Span;

    Span.Class = Class;
    Span.Space = Space;
    Span.First = First;
    Span.Last = Count == UINT_MAX ? UINT64_MAX : (UINT64)First + Count - 1;
    return Count == 0 ? S_OK : Spans->Push(Arena, Span);
}

static
HRESULT
RxListSpans(
    _In_ RxArena& Arena,
    _In_ const D3D12_ROOT_SIGNATURE_DESC2& Layout,
    _Inout_ RxGrowList<RxRegisterSpan>* Spans)
{
    HRESULT hr = S_OK;

    for (UINT Index = 0; Index < Layout.NumParameters && SUCCEEDED(hr); Index++)
    {
        const D3D12_ROOT_PARAMETER1& Parameter = Layout.pParameters[Index];

        switch (Parameter.ParameterType)
        {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                for (UINT Range = 0; Range < Parameter.DescriptorTable.NumDescriptorRanges && SUCCEEDED(hr); Range++)
                {
                    const D3D12_DESCRIPTOR_RANGE1& Each = Parameter.DescriptorTable.pDescriptorRanges[Range];

                    hr = RxAddSpan(Arena, Spans, (UINT)Each.RangeType, Each.RegisterSpace, Each.BaseShaderRegister,
                                   Each.NumDescriptors);
                }
                break;

            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                hr = RxAddSpan(Arena, Spans, RxDxilClassCbv, Parameter.Constants.RegisterSpace,
                               Parameter.Constants.ShaderRegister, 1);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_CBV:
                hr = RxAddSpan(Arena, Spans, RxDxilClassCbv, Parameter.Descriptor.RegisterSpace,
                               Parameter.Descriptor.ShaderRegister, 1);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_SRV:
                hr = RxAddSpan(Arena, Spans, RxDxilClassSrv, Parameter.Descriptor.RegisterSpace,
                               Parameter.Descriptor.ShaderRegister, 1);
                break;

            case D3D12_ROOT_PARAMETER_TYPE_UAV:
                hr = RxAddSpan(Arena, Spans, RxDxilClassUav, Parameter.Descriptor.RegisterSpace,
                               Parameter.Descriptor.ShaderRegister, 1);
                break;

            default:
                break;
        }
    }

    for (UINT Index = 0; Index < Layout.NumStaticSamplers && SUCCEEDED(hr); Index++)
    {
        hr = RxAddSpan(Arena, Spans, RxDxilClassSampler, Layout.pStaticSamplers[Index].RegisterSpace,
                       Layout.pStaticSamplers[Index].ShaderRegister, 1);
    }
    return hr;
}

/* Local root signatures bind for every stage, so any register both bind is a conflict */
static
HRESULT
RxCheckRootsCombine(
    _In_ RxArena& Arena,
    _In_ const RxRootSignature& Global,
    _In_ const RxRootSignature& Local)
{
    RxGrowList<RxRegisterSpan> GlobalSpans;
    RxGrowList<RxRegisterSpan> LocalSpans;
    HRESULT hr;

    hr = RxListSpans(Arena, Global.GetLayout(), &GlobalSpans);
    if (SUCCEEDED(hr))
        hr = RxListSpans(Arena, Local.GetLayout(), &LocalSpans);
    if (FAILED(hr))
        return hr;

    for (UINT First = 0; First < GlobalSpans.Count; First++)
    {
        const RxRegisterSpan& One = GlobalSpans.Items[First];

        for (UINT Second = 0; Second < LocalSpans.Count; Second++)
        {
            const RxRegisterSpan& Other = LocalSpans.Items[Second];

            if (One.Class == Other.Class && One.Space == Other.Space && One.First <= Other.Last && Other.First <= One.Last)
            {
                RX_TRACE(L"state object: local and global root signatures both bind class %u register %u space %u",
                         One.Class, Other.First > One.First ? Other.First : One.First, One.Space);
                return E_INVALIDARG;
            }
        }
    }
    return S_OK;
}

/* A root signature for an RTS0 part, shared through the device cache; the reference is the caller's */
static
HRESULT
RxAcquireRootSignature(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_reads_bytes_(Size) const BYTE* Bytes,
    _In_ UINT Size,
    _Outptr_ RxRootSignature** Signature)
{
    const BYTE* Part = Bytes;
    UINT PartSize = Size;
    UINT Translated;
    HRESULT hr;

    *Signature = nullptr;

    /* Libraries store the bare part; a whole serialized blob is taken too */
    if (Size >= 4 && Bytes[0] == 'D' && Bytes[1] == 'X' && Bytes[2] == 'B' && Bytes[3] == 'C' &&
        !RxFindRootPart(Bytes, Size, &Part, &PartSize))
    {
        return E_INVALIDARG;
    }

    hr = Device.TranslateNodeMask(NodeMask, FALSE, &Translated);
    if (FAILED(hr))
        return hr;

    NodeMask = NodeMask != 0 ? NodeMask : 1;
    *Signature = Device.FindRootSignature(Part, PartSize, NodeMask);
    if (*Signature != nullptr)
        return S_OK;

    hr = RxRootSignature::Create(Device, NodeMask, Part, PartSize, FALSE, Signature);
    if (FAILED(hr))
        return hr;
    Device.RememberRootSignature(*Signature);
    return S_OK;
}

static
BOOLEAN
RxIsLocalRoot(
    _In_ const RxRootSignature* Root)
{
    return (Root->GetLayout().Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE) != 0;
}

/* One association the desc makes: a subobject and the export names it applies to */
struct RxAssociationEntry
{
    RxSubobjectRecord* Record;
    UINT NameCount;
    LPCWSTR* Names;
    BOOLEAN FromApi;
};

/* A library the desc adds */
struct RxLibraryEntry
{
    RxDxilLibrary Code;
    D3D12DDI_DXIL_LIBRARY_DESC_0054* Ddi;
};

/* A library association waiting until every library subobject is known */
struct RxPendingAssociation
{
    const RxDxilSubobject* Source;
    UINT LibraryIndex;
};

/* A record handed to the driver, and the entry it went to */
struct RxEmittedRecord
{
    const RxSubobjectRecord* Record;
    const D3D12DDI_STATE_SUBOBJECT_0054* Entry;
};

/* Turns one desc into a checked state object and the driver desc for it */
class RxStateBuilder
{
public:
    RxStateBuilder(
        _In_ RxDevice12& Owner,
        _In_ const D3D12_STATE_OBJECT_DESC& StateDesc,
        _In_ RxStateObject& Object)
        : Device(Owner),
          Desc(StateDesc),
          Target(Object),
          Arena(Object.Arena)
    {
    }

    HRESULT
    Build();

private:
    HRESULT
    ReadSubobjects();

    HRESULT
    ReadSubobject(
        _In_ UINT Index);

    HRESULT
    AddRecord(
        _In_ RxSlot Slot,
        _In_opt_ const D3D12_STATE_SUBOBJECT* Api,
        _In_ UINT LibraryIndex,
        _In_opt_z_ LPCWSTR Name,
        _In_opt_ RxRootSignature* Root,
        _In_ UINT First,
        _In_ UINT Second,
        _Outptr_opt_result_maybenull_ RxSubobjectRecord** Added);

    HRESULT
    CheckPipelineConfig(
        _In_ UINT Depth,
        _In_ UINT Flags);

    HRESULT
    AddLibrary(
        _In_ UINT Index);

    HRESULT
    AddLibrarySubobject(
        _In_ const RxDxilSubobject& Subobject,
        _In_z_ LPCWSTR Name,
        _In_ UINT LibraryIndex);

    HRESULT
    AddFunction(
        _In_ const RxDxilFunction& Source,
        _In_z_ LPCWSTR Name,
        _In_z_ LPCWSTR Mangled,
        _In_ UINT LibraryIndex);

    HRESULT
    AddCollection(
        _In_ UINT Index);

    HRESULT
    AddHitGroup(
        _In_opt_z_ LPCWSTR Name,
        _In_ UINT Type,
        _In_reads_(3) const LPCWSTR* Imports,
        _In_ UINT LibraryIndex,
        _Outptr_opt_ D3D12DDI_HIT_GROUP_DESC_0054** Ddi);

    _Ret_maybenull_
    D3D12DDI_EXPORT_DESC_0054*
    CopyExports(
        _In_ UINT Count,
        _In_reads_opt_(Count) const D3D12_EXPORT_DESC* Exports);

    BOOLEAN
    IsNameTaken(
        _In_z_ LPCWSTR Name) const;

    _Ret_maybenull_
    const RxFunctionExport*
    FindVisibleFunction(
        _In_z_ LPCWSTR Name) const;

    BOOLEAN
    IsDefinedAnywhere(
        _In_z_ LPCWSTR Mangled) const;

    _Ret_maybenull_
    RxSubobjectRecord*
    FindLibraryRecord(
        _In_z_ LPCWSTR Name,
        _In_ UINT PreferredLibrary) const;

    HRESULT
    GroupByType();

    HRESULT
    ReadAssociations();

    HRESULT
    AddAssociation(
        _In_ RxSubobjectRecord* Record,
        _In_ UINT NameCount,
        _In_reads_opt_(NameCount) const LPCWSTR* Names,
        _In_ BOOLEAN FromApi,
        _In_ UINT LibraryIndex);

    UINT
    TimesAssociated(
        _In_ const RxAssociationEntry& Entry,
        _In_z_ LPCWSTR Name) const;

    HRESULT
    ResolveFunction(
        _Inout_ RxFunctionExport& Function);

    HRESULT
    ResolveHitGroup(
        _Inout_ RxHitGroupExport& Group);

    HRESULT
    CheckShared();

    HRESULT
    BuildWorkGraphs();

    VOID
    EmitApiEntry(
        _In_ UINT Index);

    HRESULT
    Emit();

    _Ret_maybenull_
    const D3D12DDI_STATE_SUBOBJECT_0054*
    EntryFor(
        _In_ const RxSubobjectRecord* Record);

    HRESULT
    CreateDriverObject();

    HRESULT
    HoldRoot(
        _In_ RxRootSignature* Root);

    /* Collections that opted in may leave dependencies for a later state object */
    BOOLEAN
    MayDefer() const
    {
        return Target.Type == D3D12_STATE_OBJECT_TYPE_COLLECTION &&
               (Target.Flags & D3D12_STATE_OBJECT_FLAG_ALLOW_LOCAL_DEPENDENCIES_ON_EXTERNAL_DEFINITIONS) != 0;
    }

    /* Collections that opted in may associate with exports defined elsewhere */
    BOOLEAN
    MayReachOut() const
    {
        return Target.Type == D3D12_STATE_OBJECT_TYPE_COLLECTION &&
               (Target.Flags & D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS) != 0;
    }

    RxDevice12& Device;
    const D3D12_STATE_OBJECT_DESC& Desc;
    RxStateObject& Target;
    RxArena& Arena;
    RxArena Scratch;

    /* Per API subobject: its record, or the driver entry it becomes */
    RxSubobjectRecord** ApiRecords = nullptr;
    D3D12DDI_STATE_SUBOBJECT_0054* ApiEntries = nullptr;
    UINT* ApiLibrary = nullptr;

    /* API subobjects in the order the driver gets them */
    UINT* ApiOrder = nullptr;

    UINT NextOrder = 0;
    RxGrowList<RxSubobjectRecord*> Records;
    RxGrowList<RxLibraryEntry*> Libraries;
    RxGrowList<RxFunctionExport> Functions;
    RxGrowList<RxHitGroupExport> HitGroups;
    RxGrowList<RxAssociationEntry> Associations;
    RxGrowList<RxPendingAssociation> Pending;
    RxGrowList<RxRootSignature*> Roots;
    RxGrowList<RxStateObject*> Collections;
    RxGrowList<UINT> GraphSubobjects;
    RxGrowList<RxWorkGraph> Graphs;

    D3D12DDI_STATE_SUBOBJECT_0054* Entries = nullptr;
    UINT EntryCount = 0;
    UINT EntryCapacity = 0;
    RxGrowList<RxEmittedRecord> Emitted;
};

HRESULT
RxStateBuilder::HoldRoot(
    _In_ RxRootSignature* Root)
{
    HRESULT hr = Roots.Push(Arena, Root);

    if (FAILED(hr))
        return hr;

    /* The object releases what it lists, so the list is current even when building fails */
    Root->AddUse();
    Target.Roots = Roots.Items;
    Target.RootCount = Roots.Count;
    return S_OK;
}

_Ret_maybenull_
D3D12DDI_EXPORT_DESC_0054*
RxStateBuilder::CopyExports(
    _In_ UINT Count,
    _In_reads_opt_(Count) const D3D12_EXPORT_DESC* Exports)
{
    D3D12DDI_EXPORT_DESC_0054* Copy;

    if (Count == 0)
        return nullptr;

    Copy = Arena.AllocateArray<D3D12DDI_EXPORT_DESC_0054>(Count);
    if (Copy == nullptr)
        return nullptr;

    for (UINT Index = 0; Index < Count; Index++)
    {
        Copy[Index].Name = Arena.CopyString(Exports[Index].Name);
        Copy[Index].ExportToRename = Arena.CopyString(Exports[Index].ExportToRename);
        Copy[Index].Flags = (D3D12DDI_EXPORT_FLAGS)Exports[Index].Flags;
    }
    return Copy;
}

BOOLEAN
RxStateBuilder::IsNameTaken(
    _In_z_ LPCWSTR Name) const
{
    for (UINT Index = 0; Index < Functions.Count; Index++)
    {
        if (wcscmp(Functions.Items[Index].Name, Name) == 0)
            return TRUE;
    }

    for (UINT Index = 0; Index < HitGroups.Count; Index++)
    {
        if (wcscmp(HitGroups.Items[Index].Name, Name) == 0)
            return TRUE;
    }

    /* An addition can not reuse a name an earlier version exports */
    if (Target.Parent != nullptr)
        return Target.Parent->FindHitGroup(Name) != nullptr || Target.Parent->FindFunction(Name, nullptr) != nullptr;
    return FALSE;
}

_Ret_maybenull_
const RxFunctionExport*
RxStateBuilder::FindVisibleFunction(
    _In_z_ LPCWSTR Name) const
{
    for (UINT Index = 0; Index < Functions.Count; Index++)
    {
        if (wcscmp(Functions.Items[Index].Name, Name) == 0)
            return &Functions.Items[Index];
    }

    return Target.Parent != nullptr ? Target.Parent->FindFunction(Name, nullptr) : nullptr;
}

/* Functions a dependency may name: anything in a library here, an export, or an earlier version */
BOOLEAN
RxStateBuilder::IsDefinedAnywhere(
    _In_z_ LPCWSTR Mangled) const
{
    for (UINT Library = 0; Library < Libraries.Count; Library++)
    {
        const RxDxilLibrary& Code = Libraries.Items[Library]->Code;

        for (UINT Index = 0; Index < Code.FunctionCount; Index++)
        {
            if (wcscmp(Code.Functions[Index].Mangled, Mangled) == 0)
                return TRUE;
        }
    }

    for (UINT Index = 0; Index < Functions.Count; Index++)
    {
        if (wcscmp(Functions.Items[Index].Mangled, Mangled) == 0)
            return TRUE;
    }

    for (const RxStateObject* Each = Target.Parent; Each != nullptr; Each = Each->Parent)
    {
        for (UINT Index = 0; Index < Each->FunctionCount; Index++)
        {
            if (wcscmp(Each->Functions[Index].Mangled, Mangled) == 0)
                return TRUE;
        }
    }
    return FALSE;
}

HRESULT
RxStateBuilder::AddRecord(
    _In_ RxSlot Slot,
    _In_opt_ const D3D12_STATE_SUBOBJECT* Api,
    _In_ UINT LibraryIndex,
    _In_opt_z_ LPCWSTR Name,
    _In_opt_ RxRootSignature* Root,
    _In_ UINT First,
    _In_ UINT Second,
    _Outptr_opt_result_maybenull_ RxSubobjectRecord** Added)
{
    RxSubobjectRecord* Record;
    HRESULT hr;

    if (Added != nullptr)
        *Added = nullptr;

    Record = Arena.AllocateArray<RxSubobjectRecord>(1);
    if (Record == nullptr)
        return E_OUTOFMEMORY;

    Record->Slot = Slot;
    Record->Api = Api;
    Record->LibraryIndex = LibraryIndex;
    Record->Name = Arena.CopyString(Name);
    Record->Order = NextOrder++;
    Record->DefaultLibrary = RxNoLibrary;
    Record->Root = Root;
    Record->Values[0] = First;
    Record->Values[1] = Second;

    /* Libraries may each declare a subobject of one name as long as they agree on it */
    if (Name != nullptr)
    {
        for (UINT Index = 0; Index < Records.Count; Index++)
        {
            RxSubobjectRecord* Other = Records.Items[Index];

            if (Other->Name == nullptr || wcscmp(Other->Name, Name) != 0)
                continue;
            if (!RxSameDefinition(Other, Record))
            {
                RX_TRACE(L"state object: library subobject %ls is defined differently twice", Name);
                return E_INVALIDARG;
            }
            if (Added != nullptr)
                *Added = Other;
            return S_OK;
        }
    }

    switch (Slot)
    {
        case RxSlotConfig:
        {
            D3D12DDI_STATE_OBJECT_CONFIG_0054* Config = Arena.AllocateArray<D3D12DDI_STATE_OBJECT_CONFIG_0054>(1);

            if (Config == nullptr)
                return E_OUTOFMEMORY;
            Config->Flags = (D3D12DDI_STATE_OBJECT_FLAGS)First;
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG;
            Record->DdiDesc = Config;
            break;
        }

        case RxSlotGlobalRoot:
        {
            D3D12DDI_GLOBAL_ROOT_SIGNATURE_0054* Global = Arena.AllocateArray<D3D12DDI_GLOBAL_ROOT_SIGNATURE_0054>(1);

            if (Global == nullptr)
                return E_OUTOFMEMORY;
            if (Root != nullptr)
                Global->hGlobalRootSignature = Root->GetDriverHandle();
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
            Record->DdiDesc = Global;
            break;
        }

        case RxSlotLocalRoot:
        {
            D3D12DDI_LOCAL_ROOT_SIGNATURE_0054* Local = Arena.AllocateArray<D3D12DDI_LOCAL_ROOT_SIGNATURE_0054>(1);

            if (Local == nullptr)
                return E_OUTOFMEMORY;
            Local->hLocalRootSignature = Root->GetDriverHandle();
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
            Record->DdiDesc = Local;
            break;
        }

        case RxSlotNodeMask:
        {
            D3D12DDI_NODE_MASK_0054* Mask = Arena.AllocateArray<D3D12DDI_NODE_MASK_0054>(1);

            if (Mask == nullptr)
                return E_OUTOFMEMORY;
            Mask->NodeMask = First;
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_NODE_MASK;
            Record->DdiDesc = Mask;
            break;
        }

        case RxSlotShaderConfig:
        {
            D3D12DDI_RAYTRACING_SHADER_CONFIG_0054* Config = Arena.AllocateArray<D3D12DDI_RAYTRACING_SHADER_CONFIG_0054>(1);

            if (Config == nullptr)
                return E_OUTOFMEMORY;
            Config->MaxPayloadSizeInBytes = First;
            Config->MaxAttributeSizeInBytes = Second;
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
            Record->DdiDesc = Config;
            break;
        }

        case RxSlotPipelineConfig:
        {
            /* Both API forms reach the driver as one config that carries flags */
            D3D12DDI_RAYTRACING_PIPELINE_CONFIG_0075* Config = Arena.AllocateArray<D3D12DDI_RAYTRACING_PIPELINE_CONFIG_0075>(1);

            if (Config == nullptr)
                return E_OUTOFMEMORY;
            Config->MaxTraceRecursionDepth = First;
            Config->Flags = (D3D12DDI_RAYTRACING_PIPELINE_FLAGS)Second;
            Record->DdiType = D3D12DDI_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
            Record->DdiDesc = Config;
            break;
        }

        default:
            return E_INVALIDARG;
    }

    if (Root != nullptr)
    {
        hr = HoldRoot(Root);
        if (FAILED(hr))
            return hr;
    }

    hr = Records.Push(Arena, Record);
    if (FAILED(hr))
        return hr;

    if (Added != nullptr)
        *Added = Record;
    return S_OK;
}

HRESULT
RxStateBuilder::CheckPipelineConfig(
    _In_ UINT Depth,
    _In_ UINT Flags)
{
    UINT Tier = (UINT)Device.GetOptions().RaytracingTier;
    UINT Known = RxSkipFlags | D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS;

    if (Depth > D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH)
    {
        RX_TRACE(L"state object: trace recursion depth %u is over the limit", Depth);
        return E_INVALIDARG;
    }
    if ((Flags & ~Known) != 0 || (Flags & RxSkipFlags) == RxSkipFlags)
    {
        RX_TRACE(L"state object: pipeline flags 0x%X are not valid", Flags);
        return E_INVALIDARG;
    }
    if ((Flags != 0 && Tier < D3D12DDI_RAYTRACING_TIER_1_1) ||
        ((Flags & D3D12_RAYTRACING_PIPELINE_FLAG_ALLOW_OPACITY_MICROMAPS) != 0 && Tier < D3D12DDI_RAYTRACING_TIER_1_2))
    {
        RX_TRACE(L"state object: pipeline flags 0x%X need a higher raytracing tier", Flags);
        return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT
RxStateBuilder::AddFunction(
    _In_ const RxDxilFunction& Source,
    _In_z_ LPCWSTR Name,
    _In_z_ LPCWSTR Mangled,
    _In_ UINT LibraryIndex)
{
    RxFunctionExport Function = {};
    BOOLEAN NodeShader = Source.Kind == RxDxilKindNode && Target.Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;

    if (Source.Kind != RxDxilKindLibrary && !RxIsRaytracingShader(Source.Kind) && !NodeShader)
    {
        RX_TRACE(L"state object: export %ls is a kind %u shader, which raytracing state objects do not take", Name,
                 Source.Kind);
        return E_INVALIDARG;
    }
    if (RxIsRaytracingShader(Source.Kind) && Device.GetOptions().RaytracingTier == D3D12DDI_RAYTRACING_TIER_NOT_SUPPORTED)
    {
        RX_TRACE(L"state object: export %ls is a raytracing shader and the device has no raytracing", Name);
        return E_INVALIDARG;
    }
    if (IsNameTaken(Name))
    {
        RX_TRACE(L"state object: export %ls is already defined", Name);
        return E_INVALIDARG;
    }

    Function.Name = Arena.CopyString(Name);
    Function.Mangled = Arena.CopyString(Mangled);
    Function.Kind = Source.Kind;
    Function.Source = &Source;
    Function.Owner = &Target;
    Function.LibraryIndex = LibraryIndex;
    if (Function.Name == nullptr || Function.Mangled == nullptr)
        return E_OUTOFMEMORY;
    return Functions.Push(Arena, Function);
}

HRESULT
RxStateBuilder::AddHitGroup(
    _In_opt_z_ LPCWSTR Name,
    _In_ UINT Type,
    _In_reads_(3) const LPCWSTR* Imports,
    _In_ UINT LibraryIndex,
    _Outptr_opt_ D3D12DDI_HIT_GROUP_DESC_0054** Ddi)
{
    RxHitGroupExport Group = {};
    D3D12DDI_HIT_GROUP_DESC_0054* Driver;

    if (Name == nullptr || Name[0] == L'\0')
    {
        RX_TRACE(L"state object: a hit group has no name");
        return E_INVALIDARG;
    }
    if (Type != D3D12_HIT_GROUP_TYPE_TRIANGLES && Type != D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE)
    {
        RX_TRACE(L"state object: hit group %ls has type %u", Name, Type);
        return E_INVALIDARG;
    }
    if (IsNameTaken(Name))
    {
        RX_TRACE(L"state object: hit group %ls is already defined", Name);
        return E_INVALIDARG;
    }

    Driver = Arena.AllocateArray<D3D12DDI_HIT_GROUP_DESC_0054>(1);
    if (Driver == nullptr)
        return E_OUTOFMEMORY;

    Group.Name = Arena.CopyString(Name);
    Group.Type = (D3D12_HIT_GROUP_TYPE)Type;
    for (UINT Index = 0; Index < 3; Index++)
        Group.Imports[Index] = Arena.CopyString(Imports[Index]);
    Group.Owner = &Target;
    Group.LibraryIndex = LibraryIndex;
    Group.Ddi = Driver;

    Driver->HitGroupExport = Group.Name;
    Driver->Type = (D3D12DDI_HIT_GROUP_TYPE)Type;
    Driver->AnyHitShaderImport = Group.Imports[RxImportAnyHit];
    Driver->ClosestHitShaderImport = Group.Imports[RxImportClosestHit];
    Driver->IntersectionShaderImport = Group.Imports[RxImportIntersection];

    if (Ddi != nullptr)
        *Ddi = Driver;
    return HitGroups.Push(Arena, Group);
}

HRESULT
RxStateBuilder::AddLibrarySubobject(
    _In_ const RxDxilSubobject& Subobject,
    _In_z_ LPCWSTR Name,
    _In_ UINT LibraryIndex)
{
    RxRootSignature* Root;
    RxPendingAssociation Waiting;
    HRESULT hr;

    switch (Subobject.Kind)
    {
        case RxDxilSubobjectConfig:
            if ((Subobject.Values[0] & ~RxConfigFlagMask) != 0)
            {
                RX_TRACE(L"state object: library config %ls has flags 0x%X", Name, Subobject.Values[0]);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotConfig, nullptr, LibraryIndex, Name, nullptr, Subobject.Values[0], 0, nullptr);

        case RxDxilSubobjectGlobalRoot:
        case RxDxilSubobjectLocalRoot:
        {
            BOOLEAN Local = Subobject.Kind == RxDxilSubobjectLocalRoot;

            hr = RxAcquireRootSignature(Device, 0, Subobject.Bytes, Subobject.ByteCount, &Root);
            if (FAILED(hr))
            {
                RX_TRACE(L"state object: library root signature %ls failed 0x%08lX", Name, hr);
                return E_INVALIDARG;
            }

            /* The record takes a use of its own; the reference from creation goes */
            if (RxIsLocalRoot(Root) != Local)
            {
                Root->Release();
                RX_TRACE(L"state object: library root signature %ls has the wrong local flag", Name);
                return E_INVALIDARG;
            }
            hr = AddRecord(Local ? RxSlotLocalRoot : RxSlotGlobalRoot, nullptr, LibraryIndex, Name, Root, 0, 0, nullptr);
            Root->Release();
            return hr;
        }

        case RxDxilSubobjectAssociation:
            Waiting.Source = &Subobject;
            Waiting.LibraryIndex = LibraryIndex;
            return Pending.Push(Scratch, Waiting);

        case RxDxilSubobjectShaderConfig:
            if (Subobject.Values[1] > D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES)
            {
                RX_TRACE(L"state object: library shader config %ls has %u attribute bytes", Name, Subobject.Values[1]);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotShaderConfig, nullptr, LibraryIndex, Name, nullptr, Subobject.Values[0],
                             Subobject.Values[1], nullptr);

        case RxDxilSubobjectPipelineConfig:
            hr = CheckPipelineConfig(Subobject.Values[0], 0);
            if (FAILED(hr))
                return hr;
            return AddRecord(RxSlotPipelineConfig, nullptr, LibraryIndex, Name, nullptr, Subobject.Values[0], 0, nullptr);

        case RxDxilSubobjectPipelineConfig1:
            hr = CheckPipelineConfig(Subobject.Values[0], Subobject.Values[1]);
            if (FAILED(hr))
                return hr;
            return AddRecord(RxSlotPipelineConfig, nullptr, LibraryIndex, Name, nullptr, Subobject.Values[0],
                             Subobject.Values[1], nullptr);

        case RxDxilSubobjectHitGroup:
            return AddHitGroup(Name, Subobject.Values[0], Subobject.Strings, LibraryIndex, nullptr);

        default:
            RX_TRACE(L"state object: library subobject %ls of kind %u is not used", Name, Subobject.Kind);
            return S_OK;
    }
}

HRESULT
RxStateBuilder::AddLibrary(
    _In_ UINT Index)
{
    const D3D12_DXIL_LIBRARY_DESC* Api = static_cast<const D3D12_DXIL_LIBRARY_DESC*>(Desc.pSubobjects[Index].pDesc);
    UINT LibraryIndex = Libraries.Count;
    VOID* Memory = Arena.AllocateArray<RxLibraryEntry>(1);
    RxLibraryEntry* Library;
    BOOLEAN* Matched;
    UINT Version;
    UINT Model;
    HRESULT hr;

    if (Memory == nullptr)
        return E_OUTOFMEMORY;
    Library = new (Memory) RxLibraryEntry();
    hr = Libraries.Push(Arena, Library);
    if (FAILED(hr))
        return hr;

    hr = Library->Code.Load(Arena, Api->DXILLibrary.pShaderBytecode, Api->DXILLibrary.BytecodeLength);
    if (FAILED(hr))
    {
        RX_TRACE(L"state object: DXIL library in subobject %u could not be read", Index);
        return hr == E_OUTOFMEMORY ? hr : E_INVALIDARG;
    }

    /* Preview shaders are never enabled here, so unsigned libraries fail like altered ones */
    if (!Library->Code.IsSigned())
    {
        RX_TRACE(L"state object: DXIL library in subobject %u fails its hash check", Index);
        return E_INVALIDARG;
    }

    /* Only library targets from 6.3 up to what the driver compiles */
    Version = Library->Code.GetVersion();
    Model = ((Version >> 4) & 0xF) << 4 | (Version & 0xF);
    if ((Version >> 16) != RxDxilKindLibrary || Model < D3D_SHADER_MODEL_6_3 || Model > (UINT)Device.GetHighestShaderModel())
    {
        RX_TRACE(L"state object: DXIL library in subobject %u has version 0x%X", Index, Version);
        return E_INVALIDARG;
    }

    if (Api->NumExports != 0 && Api->pExports == nullptr)
        return E_INVALIDARG;

    Library->Ddi = Arena.AllocateArray<D3D12DDI_DXIL_LIBRARY_DESC_0054>(1);
    Matched = Scratch.AllocateArray<BOOLEAN>(Api->NumExports);
    if (Library->Ddi == nullptr || Matched == nullptr)
        return E_OUTOFMEMORY;
    Library->Ddi->pDXILLibrary = Library->Code.GetProgram();
    Library->Ddi->NumExports = Api->NumExports;
    Library->Ddi->pExports = CopyExports(Api->NumExports, Api->pExports);

    for (UINT Export = 0; Export < Api->NumExports; Export++)
    {
        const D3D12_EXPORT_DESC& Each = Api->pExports[Export];
        LPCWSTR Source = Each.ExportToRename != nullptr ? Each.ExportToRename : Each.Name;
        UINT Overloads = 0;

        if (Each.Name == nullptr || Each.Name[0] == L'\0' || Source[0] == L'\0')
        {
            RX_TRACE(L"state object: export %u of subobject %u has no name", Export, Index);
            return E_INVALIDARG;
        }

        for (UINT Function = 0; Function < Library->Code.FunctionCount; Function++)
        {
            if (wcscmp(Library->Code.Functions[Function].Name, Source) == 0)
                Overloads++;
        }
        if (Overloads > 1)
        {
            RX_TRACE(L"state object: export %ls matches %u overloads", Source, Overloads);
            return E_INVALIDARG;
        }
    }

    /* Functions reach the driver summary in library order, whatever order the exports list them */
    for (UINT Function = 0; Function < Library->Code.FunctionCount; Function++)
    {
        const RxDxilFunction& Code = Library->Code.Functions[Function];

        if (Api->NumExports == 0)
        {
            hr = AddFunction(Code, Code.Name, Code.Mangled, LibraryIndex);
            if (FAILED(hr))
                return hr;
            continue;
        }

        for (UINT Export = 0; Export < Api->NumExports; Export++)
        {
            const D3D12_EXPORT_DESC& Each = Api->pExports[Export];
            LPCWSTR Source = Each.ExportToRename != nullptr ? Each.ExportToRename : Each.Name;
            LPCWSTR Mangled = Code.Mangled;

            /* Names may also be given mangled, with or without the marker */
            if (wcscmp(Code.Name, Source) != 0 && wcscmp(Code.Mangled, Source) != 0 &&
                (Code.Mangled[0] != L'\x01' || wcscmp(Code.Mangled + 1, Source) != 0))
            {
                continue;
            }

            Matched[Export] = TRUE;
            if (wcscmp(Each.Name, Code.Name) != 0)
            {
                Mangled = RxRenameMangled(Arena, Code.Mangled, Code.Name, Each.Name);
                if (Mangled == nullptr)
                    return E_OUTOFMEMORY;
            }

            hr = AddFunction(Code, Each.Name, Mangled, LibraryIndex);
            if (FAILED(hr))
                return hr;
        }
    }

    for (UINT Subobject = 0; Subobject < Library->Code.SubobjectCount; Subobject++)
    {
        const RxDxilSubobject& Code = Library->Code.Subobjects[Subobject];
        LPCWSTR Name = Api->NumExports == 0 ? Code.Name : nullptr;

        for (UINT Export = 0; Export < Api->NumExports && Name == nullptr; Export++)
        {
            const D3D12_EXPORT_DESC& Each = Api->pExports[Export];
            LPCWSTR Source = Each.ExportToRename != nullptr ? Each.ExportToRename : Each.Name;

            if (wcscmp(Code.Name, Source) == 0)
            {
                Matched[Export] = TRUE;
                Name = Each.Name;
            }
        }
        if (Name == nullptr)
            continue;

        hr = AddLibrarySubobject(Code, Name, LibraryIndex);
        if (FAILED(hr))
            return hr;
    }

    for (UINT Export = 0; Export < Api->NumExports; Export++)
    {
        if (!Matched[Export])
        {
            RX_TRACE(L"state object: listed export %ls is not in the library", Api->pExports[Export].Name);
            return E_INVALIDARG;
        }
    }

    ApiLibrary[Index] = LibraryIndex;
    ApiEntries[Index].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    ApiEntries[Index].pDesc = Library->Ddi;
    return S_OK;
}

HRESULT
RxStateBuilder::AddCollection(
    _In_ UINT Index)
{
    const D3D12_EXISTING_COLLECTION_DESC* Api = static_cast<const D3D12_EXISTING_COLLECTION_DESC*>(Desc.pSubobjects[Index].pDesc);
    RxStateObject* Collection = RxUnwrap12<RxStateObject>(Api->pExistingCollection);
    D3D12DDI_EXISTING_COLLECTION_DESC_0054* Ddi;
    BOOLEAN* Matched;
    BOOLEAN Sealed;
    HRESULT hr;

    if (Collection == nullptr || Collection->GetType() != D3D12_STATE_OBJECT_TYPE_COLLECTION)
    {
        RX_TRACE(L"state object: subobject %u names something that is not a collection", Index);
        return E_INVALIDARG;
    }
    if (Api->NumExports != 0 && Api->pExports == nullptr)
        return E_INVALIDARG;

    hr = Collections.Push(Arena, Collection);
    if (FAILED(hr))
        return hr;
    Collection->AddUse();
    Target.Collections = Collections.Items;
    Target.CollectionCount = Collections.Count;

    Ddi = Arena.AllocateArray<D3D12DDI_EXISTING_COLLECTION_DESC_0054>(1);
    Matched = Scratch.AllocateArray<BOOLEAN>(Api->NumExports);
    if (Ddi == nullptr || Matched == nullptr)
        return E_OUTOFMEMORY;
    Ddi->hExistingCollection = Collection->GetDriverHandle();
    Ddi->NumExports = Api->NumExports;
    Ddi->pExports = CopyExports(Api->NumExports, Api->pExports);

    Sealed = (Collection->GetFlags() & D3D12_STATE_OBJECT_FLAG_ALLOW_EXTERNAL_DEPENDENCIES_ON_LOCAL_DEFINITIONS) == 0;

    for (UINT Function = 0; Function < Collection->FunctionCount; Function++)
    {
        RxFunctionExport Copy = Collection->Functions[Function];
        LPCWSTR Name = Api->NumExports == 0 ? Copy.Name : nullptr;

        for (UINT Export = 0; Export < Api->NumExports && Name == nullptr; Export++)
        {
            const D3D12_EXPORT_DESC& Each = Api->pExports[Export];

            if (wcscmp(Copy.Name, Each.ExportToRename != nullptr ? Each.ExportToRename : Each.Name) == 0)
            {
                Matched[Export] = TRUE;
                Name = Each.Name;
            }
        }
        if (Name == nullptr)
            continue;

        if (IsNameTaken(Name))
        {
            RX_TRACE(L"state object: export %ls from a collection is already defined", Name);
            return E_INVALIDARG;
        }
        if (wcscmp(Name, Copy.Name) != 0)
        {
            Copy.Mangled = RxRenameMangled(Arena, Copy.Mangled, Copy.Name, Name);
            Copy.Name = Arena.CopyString(Name);
            if (Copy.Mangled == nullptr || Copy.Name == nullptr)
                return E_OUTOFMEMORY;
        }

        /* What the collection left open is finished here; the rest keeps the collection's node */
        Copy.LibraryIndex = RxNoLibrary;
        Copy.Sealed = Sealed;
        if (Copy.Flags != 0)
            Copy.Node = nullptr;
        hr = Functions.Push(Arena, Copy);
        if (FAILED(hr))
            return hr;
    }

    for (UINT Group = 0; Group < Collection->HitGroupCount; Group++)
    {
        RxHitGroupExport Copy = Collection->HitGroups[Group];
        LPCWSTR Name = Api->NumExports == 0 ? Copy.Name : nullptr;

        for (UINT Export = 0; Export < Api->NumExports && Name == nullptr; Export++)
        {
            const D3D12_EXPORT_DESC& Each = Api->pExports[Export];

            if (wcscmp(Copy.Name, Each.ExportToRename != nullptr ? Each.ExportToRename : Each.Name) == 0)
            {
                Matched[Export] = TRUE;
                Name = Each.Name;
            }
        }
        if (Name == nullptr)
            continue;

        if (IsNameTaken(Name))
        {
            RX_TRACE(L"state object: hit group %ls from a collection is already defined", Name);
            return E_INVALIDARG;
        }
        Copy.Name = Arena.CopyString(Name);
        Copy.LibraryIndex = RxNoLibrary;
        hr = HitGroups.Push(Arena, Copy);
        if (FAILED(hr))
            return hr;
    }

    for (UINT Export = 0; Export < Api->NumExports; Export++)
    {
        if (!Matched[Export])
        {
            RX_TRACE(L"state object: listed export %ls is not in the collection", Api->pExports[Export].Name);
            return E_INVALIDARG;
        }
    }

    ApiEntries[Index].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION;
    ApiEntries[Index].pDesc = Ddi;
    return S_OK;
}

HRESULT
RxStateBuilder::ReadSubobject(
    _In_ UINT Index)
{
    const D3D12_STATE_SUBOBJECT& Entry = Desc.pSubobjects[Index];
    RxSubobjectRecord** Record = &ApiRecords[Index];
    HRESULT hr;

    if (Entry.pDesc == nullptr)
    {
        RX_TRACE(L"state object: subobject %u has no desc", Index);
        return E_INVALIDARG;
    }

    switch (Entry.Type)
    {
        case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
        {
            UINT Flags = (UINT)static_cast<const D3D12_STATE_OBJECT_CONFIG*>(Entry.pDesc)->Flags;

            if ((Flags & ~RxConfigFlagMask) != 0)
            {
                RX_TRACE(L"state object: config flags 0x%X are not valid", Flags);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotConfig, &Entry, RxNoLibrary, nullptr, nullptr, Flags, 0, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE:
        {
            ID3D12RootSignature* Api = static_cast<const D3D12_GLOBAL_ROOT_SIGNATURE*>(Entry.pDesc)->pGlobalRootSignature;
            RxRootSignature* Root = RxUnwrap12<RxRootSignature>(Api);

            if ((Api != nullptr && Root == nullptr) || (Root != nullptr && RxIsLocalRoot(Root)))
            {
                RX_TRACE(L"state object: global root signature in subobject %u is not valid", Index);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotGlobalRoot, &Entry, RxNoLibrary, nullptr, Root, 0, 0, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE:
        {
            ID3D12RootSignature* Api = static_cast<const D3D12_LOCAL_ROOT_SIGNATURE*>(Entry.pDesc)->pLocalRootSignature;
            RxRootSignature* Root = RxUnwrap12<RxRootSignature>(Api);

            if (Root == nullptr || !RxIsLocalRoot(Root))
            {
                RX_TRACE(L"state object: local root signature in subobject %u lacks the local flag", Index);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotLocalRoot, &Entry, RxNoLibrary, nullptr, Root, 0, 0, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_NODE_MASK:
        {
            UINT Translated;

            hr = Device.TranslateNodeMask(static_cast<const D3D12_NODE_MASK*>(Entry.pDesc)->NodeMask, FALSE, &Translated);
            if (FAILED(hr))
            {
                RX_TRACE(L"state object: node mask in subobject %u is not valid", Index);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotNodeMask, &Entry, RxNoLibrary, nullptr, nullptr, Translated, 0, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY:
            return AddLibrary(Index);

        case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION:
            return AddCollection(Index);

        /* Associations wait until every export and subobject is known */
        case D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            return S_OK;

        /* Graphs are built once every node shader is known and resolved */
        case D3D12_STATE_SUBOBJECT_TYPE_WORK_GRAPH:
            if (Target.Type != D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
            {
                RX_TRACE(L"state object: work graphs belong only in executable state objects");
                return E_INVALIDARG;
            }
            return GraphSubobjects.Push(Scratch, Index);

        /* Graphics state only a generic program would use; executable objects take it and leave it out */
        case D3D12_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
        case D3D12_STATE_SUBOBJECT_TYPE_BLEND:
        case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
        case D3D12_STATE_SUBOBJECT_TYPE_RASTERIZER:
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL:
        case D3D12_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
        case D3D12_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
        case D3D12_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
        case D3D12_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
        case D3D12_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
        case D3D12_STATE_SUBOBJECT_TYPE_FLAGS:
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1:
        case D3D12_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING:
        case D3D12_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
            if (Target.Type != D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
            {
                RX_TRACE(L"state object: subobject %u is graphics state outside an executable state object", Index);
                return E_INVALIDARG;
            }
            return S_OK;

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
        {
            const D3D12_RAYTRACING_SHADER_CONFIG* Config = static_cast<const D3D12_RAYTRACING_SHADER_CONFIG*>(Entry.pDesc);

            if (Config->MaxAttributeSizeInBytes > D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES)
            {
                RX_TRACE(L"state object: %u attribute bytes is over the limit", Config->MaxAttributeSizeInBytes);
                return E_INVALIDARG;
            }
            return AddRecord(RxSlotShaderConfig, &Entry, RxNoLibrary, nullptr, nullptr, Config->MaxPayloadSizeInBytes,
                             Config->MaxAttributeSizeInBytes, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
        {
            UINT Depth = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG*>(Entry.pDesc)->MaxTraceRecursionDepth;

            hr = CheckPipelineConfig(Depth, 0);
            if (FAILED(hr))
                return hr;
            return AddRecord(RxSlotPipelineConfig, &Entry, RxNoLibrary, nullptr, nullptr, Depth, 0, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
        {
            const D3D12_RAYTRACING_PIPELINE_CONFIG1* Config = static_cast<const D3D12_RAYTRACING_PIPELINE_CONFIG1*>(Entry.pDesc);

            hr = CheckPipelineConfig(Config->MaxTraceRecursionDepth, (UINT)Config->Flags);
            if (FAILED(hr))
                return hr;
            return AddRecord(RxSlotPipelineConfig, &Entry, RxNoLibrary, nullptr, nullptr, Config->MaxTraceRecursionDepth,
                             (UINT)Config->Flags, Record);
        }

        case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP:
        {
            const D3D12_HIT_GROUP_DESC* Group = static_cast<const D3D12_HIT_GROUP_DESC*>(Entry.pDesc);
            LPCWSTR Imports[3];
            D3D12DDI_HIT_GROUP_DESC_0054* Ddi;

            Imports[RxImportAnyHit] = Group->AnyHitShaderImport;
            Imports[RxImportClosestHit] = Group->ClosestHitShaderImport;
            Imports[RxImportIntersection] = Group->IntersectionShaderImport;
            hr = AddHitGroup(Group->HitGroupExport, (UINT)Group->Type, Imports, RxNoLibrary, &Ddi);
            if (FAILED(hr))
                return hr;

            ApiEntries[Index].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            ApiEntries[Index].pDesc = Ddi;
            return S_OK;
        }

        default:
            RX_TRACE(L"state object: subobject %u has type %d, which raytracing state objects do not take", Index,
                     Entry.Type);
            return E_INVALIDARG;
    }
}

HRESULT
RxStateBuilder::ReadSubobjects()
{
    const RxSubobjectRecord* Config = nullptr;
    HRESULT hr;

    for (UINT Index = 0; Index < Desc.NumSubobjects; Index++)
    {
        hr = ReadSubobject(Index);
        if (FAILED(hr))
            return hr;
    }

    /* Every config in the desc and its libraries must agree; that is the object's flags */
    for (UINT Index = 0; Index < Records.Count; Index++)
    {
        const RxSubobjectRecord* Record = Records.Items[Index];

        if (Record->Slot != RxSlotConfig)
            continue;
        if (Config != nullptr && !RxSameDefinition(Config, Record))
        {
            RX_TRACE(L"state object: configs disagree on flags");
            return E_INVALIDARG;
        }
        Config = Record;
    }

    Target.Flags = Config != nullptr ? Config->Values[0] : 0;
    return S_OK;
}

/* Subobjects of one type sit together, types in the order they first appear; defaults follow that order */
HRESULT
RxStateBuilder::GroupByType()
{
    BOOLEAN* Placed = Scratch.AllocateArray<BOOLEAN>(Desc.NumSubobjects);
    UINT Count = 0;

    ApiOrder = Scratch.AllocateArray<UINT>(Desc.NumSubobjects);
    if (Placed == nullptr || ApiOrder == nullptr)
        return E_OUTOFMEMORY;

    for (UINT First = 0; First < Desc.NumSubobjects; First++)
    {
        if (Placed[First])
            continue;

        for (UINT Index = First; Index < Desc.NumSubobjects; Index++)
        {
            if (Placed[Index] || Desc.pSubobjects[Index].Type != Desc.pSubobjects[First].Type)
                continue;

            Placed[Index] = TRUE;
            if (ApiRecords[Index] != nullptr)
                ApiRecords[Index]->Order = Count;
            ApiOrder[Count++] = Index;
        }
    }

    for (UINT Index = 0; Index < Records.Count; Index++)
    {
        if (Records.Items[Index]->Api == nullptr)
            Records.Items[Index]->Order += Desc.NumSubobjects;
    }
    return S_OK;
}

_Ret_maybenull_
RxSubobjectRecord*
RxStateBuilder::FindLibraryRecord(
    _In_z_ LPCWSTR Name,
    _In_ UINT PreferredLibrary) const
{
    RxSubobjectRecord* Found = nullptr;

    for (UINT Index = 0; Index < Records.Count; Index++)
    {
        RxSubobjectRecord* Record = Records.Items[Index];

        if (Record->Name == nullptr || wcscmp(Record->Name, Name) != 0)
            continue;
        if (Record->LibraryIndex == PreferredLibrary)
            return Record;
        if (Found == nullptr)
            Found = Record;
    }
    return Found;
}

HRESULT
RxStateBuilder::AddAssociation(
    _In_ RxSubobjectRecord* Record,
    _In_ UINT NameCount,
    _In_reads_opt_(NameCount) const LPCWSTR* Names,
    _In_ BOOLEAN FromApi,
    _In_ UINT LibraryIndex)
{
    RxAssociationEntry Entry = {};

    Record->Referenced = TRUE;

    /* No names: the default for the whole object, or for the library that declared it */
    if (NameCount == 0)
    {
        if (FromApi)
            Record->DefaultForState = TRUE;
        else
            Record->DefaultLibrary = LibraryIndex;
        return S_OK;
    }

    if (Names == nullptr)
        return E_INVALIDARG;

    Entry.Names = Scratch.AllocateArray<LPCWSTR>(NameCount);
    if (Entry.Names == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Index = 0; Index < NameCount; Index++)
    {
        BOOLEAN Known = FALSE;

        if (Names[Index] == nullptr)
            return E_INVALIDARG;

        for (UINT Function = 0; Function < Functions.Count && !Known; Function++)
            Known = wcscmp(Functions.Items[Function].Name, Names[Index]) == 0;
        for (UINT Group = 0; Group < HitGroups.Count && !Known; Group++)
            Known = wcscmp(HitGroups.Items[Group].Name, Names[Index]) == 0;

        if (!Known && !MayReachOut())
        {
            RX_TRACE(L"state object: association names %ls, which is not an export", Names[Index]);
            return E_INVALIDARG;
        }
        Entry.Names[Index] = Names[Index];
    }

    Entry.Record = Record;
    Entry.NameCount = NameCount;
    Entry.FromApi = FromApi;
    return Associations.Push(Scratch, Entry);
}

HRESULT
RxStateBuilder::ReadAssociations()
{
    HRESULT hr;

    for (UINT Index = 0; Index < Desc.NumSubobjects; Index++)
    {
        const D3D12_STATE_SUBOBJECT& Entry = Desc.pSubobjects[Index];

        if (Entry.Type == D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION* Api =
                static_cast<const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(Entry.pDesc);
            const D3D12_STATE_SUBOBJECT* Subobject = Api->pSubobjectToAssociate;
            SIZE_T Offset = (SIZE_T)((const BYTE*)Subobject - (const BYTE*)Desc.pSubobjects);
            RxSubobjectRecord* Record = nullptr;

            /* The subobject must be one of this desc's own entries */
            if (Subobject != nullptr && Subobject >= Desc.pSubobjects && Subobject < Desc.pSubobjects + Desc.NumSubobjects &&
                Offset % sizeof(*Subobject) == 0)
            {
                Record = ApiRecords[Offset / sizeof(*Subobject)];
            }
            if (Record == nullptr)
            {
                RX_TRACE(L"state object: association %u names a subobject that is not associable here", Index);
                return E_INVALIDARG;
            }

            hr = AddAssociation(Record, Api->NumExports, Api->pExports, TRUE, RxNoLibrary);
            if (FAILED(hr))
                return hr;
        }
        else if (Entry.Type == D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
        {
            const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION* Api =
                static_cast<const D3D12_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION*>(Entry.pDesc);
            RxSubobjectRecord* Record =
                Api->SubobjectToAssociate != nullptr ? FindLibraryRecord(Api->SubobjectToAssociate, RxNoLibrary) : nullptr;

            if (Record == nullptr)
            {
                RX_TRACE(L"state object: association %u names a library subobject that is not defined", Index);
                return E_INVALIDARG;
            }

            hr = AddAssociation(Record, Api->NumExports, Api->pExports, TRUE, RxNoLibrary);
            if (FAILED(hr))
                return hr;
        }
    }

    for (UINT Index = 0; Index < Pending.Count; Index++)
    {
        const RxDxilSubobject& Source = *Pending.Items[Index].Source;
        RxSubobjectRecord* Record = FindLibraryRecord(Source.Strings[0], Pending.Items[Index].LibraryIndex);

        if (Record == nullptr)
        {
            RX_TRACE(L"state object: library association %ls names %ls, which is not defined", Source.Name, Source.Strings[0]);
            return E_INVALIDARG;
        }

        hr = AddAssociation(Record, Source.ExportCount, Source.Exports, FALSE, Pending.Items[Index].LibraryIndex);
        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

/* How often an association reaches a function: by its name, or through hit groups that import it */
UINT
RxStateBuilder::TimesAssociated(
    _In_ const RxAssociationEntry& Entry,
    _In_z_ LPCWSTR Name) const
{
    UINT Times = 0;

    for (UINT Index = 0; Index < Entry.NameCount; Index++)
    {
        if (wcscmp(Entry.Names[Index], Name) == 0)
        {
            Times++;
            continue;
        }

        for (UINT Group = 0; Group < HitGroups.Count; Group++)
        {
            const RxHitGroupExport& Each = HitGroups.Items[Group];

            if (wcscmp(Each.Name, Entry.Names[Index]) != 0)
                continue;
            for (UINT Import = 0; Import < 3; Import++)
            {
                if (Each.Imports[Import] != nullptr && wcscmp(Each.Imports[Import], Name) == 0)
                    Times++;
            }
        }
    }
    return Times;
}

HRESULT
RxStateBuilder::ResolveFunction(
    _Inout_ RxFunctionExport& Function)
{
    RxGrowList<RxSubobjectRecord*> Chosen;
    RxGrowList<RxSubobjectRecord*> Ordered;
    BOOLEAN Covered[RxSlotCount] = {};
    BOOLEAN Explicit[RxSlotCount] = {};
    const RxSubobjectRecord* Global;
    const RxSubobjectRecord* Local;
    UINT Flags = 0;
    HRESULT hr;

    /* What a collection already associated stays and comes first */
    for (UINT Index = 0; Index < Function.AssociationCount; Index++)
    {
        hr = Chosen.Push(Scratch, Function.Associations[Index]);
        if (FAILED(hr))
            return hr;
        Covered[Function.Associations[Index]->Slot] = TRUE;
    }

    /* Associations the API made come next in the order made; library ones sort with the defaults */
    for (UINT Index = 0; Index < Associations.Count; Index++)
    {
        const RxAssociationEntry& Entry = Associations.Items[Index];
        UINT Times = TimesAssociated(Entry, Function.Name);
        RxSlot Slot = Entry.Record->Slot;

        if (Times == 0 || Covered[Slot])
            continue;
        if (!RxSlotFits(Slot, Function.Kind))
        {
            RX_TRACE(L"state object: subobject of slot %u can not be associated with %ls", Slot, Function.Name);
            return E_INVALIDARG;
        }

        Explicit[Slot] = TRUE;
        for (UINT Each = 0; Each < Times; Each++)
        {
            hr = Entry.FromApi ? Chosen.Push(Scratch, Entry.Record) : Ordered.Push(Scratch, Entry.Record);
            if (FAILED(hr))
                return hr;
        }
    }

    /* Defaults: the library's explicit one, then its unassociated ones, then the object's */
    for (UINT Slot = 0; Slot < RxSlotCount; Slot++)
    {
        if (Covered[Slot] || Explicit[Slot] || !RxSlotFits((RxSlot)Slot, Function.Kind))
            continue;

        for (UINT Level = 0; Level < 4; Level++)
        {
            BOOLEAN Found = FALSE;

            for (UINT Index = 0; Index < Records.Count; Index++)
            {
                RxSubobjectRecord* Record = Records.Items[Index];
                BOOLEAN Applies;

                if ((UINT)Record->Slot != Slot)
                    continue;

                switch (Level)
                {
                    case 0:
                        Applies = Function.LibraryIndex != RxNoLibrary && Record->DefaultLibrary == Function.LibraryIndex;
                        break;
                    case 1:
                        Applies = Function.LibraryIndex != RxNoLibrary && Record->LibraryIndex == Function.LibraryIndex &&
                                  !Record->Referenced;
                        break;
                    case 2:
                        Applies = Record->DefaultForState;
                        break;
                    default:
                        Applies = Record->Api != nullptr && !Record->Referenced;
                        break;
                }
                if (!Applies)
                    continue;

                hr = Ordered.Push(Scratch, Record);
                if (FAILED(hr))
                    return hr;
                Found = TRUE;
            }
            if (Found)
                break;
        }
    }

    /* Definition order, keeping the order of equal ones */
    for (UINT Index = 1; Index < Ordered.Count; Index++)
    {
        RxSubobjectRecord* Moving = Ordered.Items[Index];
        UINT Place = Index;

        while (Place > 0 && Ordered.Items[Place - 1]->Order > Moving->Order)
        {
            Ordered.Items[Place] = Ordered.Items[Place - 1];
            Place -= 1;
        }
        Ordered.Items[Place] = Moving;
    }
    for (UINT Index = 0; Index < Ordered.Count; Index++)
    {
        hr = Chosen.Push(Scratch, Ordered.Items[Index]);
        if (FAILED(hr))
            return hr;
    }

    Function.AssociationCount = Chosen.Count;
    Function.Associations = Arena.AllocateArray<RxSubobjectRecord*>(Chosen.Count);
    if (Function.Associations == nullptr)
        return E_OUTOFMEMORY;
    if (Chosen.Count != 0)
        CopyMemory(Function.Associations, Chosen.Items, sizeof(*Chosen.Items) * Chosen.Count);

    /* Several subobjects of one kind may apply only if they mean the same */
    for (UINT Index = 0; Index < Chosen.Count; Index++)
    {
        if (!RxSameDefinition(RxFirstInSlot(Function, Chosen.Items[Index]->Slot), Chosen.Items[Index]))
        {
            RX_TRACE(L"state object: export %ls has conflicting associations of slot %u", Function.Name,
                     Chosen.Items[Index]->Slot);
            return E_INVALIDARG;
        }
    }

    if (RxIsRaytracingShader(Function.Kind) &&
        (RxFirstInSlot(Function, RxSlotShaderConfig) == nullptr || RxFirstInSlot(Function, RxSlotPipelineConfig) == nullptr))
    {
        if (!MayDefer())
        {
            RX_TRACE(L"state object: export %ls has no shader config or pipeline config", Function.Name);
            return E_INVALIDARG;
        }
        Flags |= D3D12DDI_EXPORT_SUMMARY_FLAG_UNRESOLVED_ASSOCIATIONS;
    }

    Global = RxFirstInSlot(Function, RxSlotGlobalRoot);
    Local = RxFirstInSlot(Function, RxSlotLocalRoot);
    if (Global != nullptr && Global->Root != nullptr && Local != nullptr && Local->Root != nullptr)
    {
        hr = RxCheckRootsCombine(Scratch, *Global->Root, *Local->Root);
        if (FAILED(hr))
            return hr;
    }

    for (UINT Index = 0; Index < Function.Source->BindingCount; Index++)
    {
        if (RxRootsBind(Global != nullptr ? Global->Root : nullptr, Local != nullptr ? Local->Root : nullptr,
                        Function.Source->Bindings[Index]))
        {
            continue;
        }
        if (!MayDefer())
        {
            RX_TRACE(L"state object: export %ls reads registers its root signatures do not bind", Function.Name);
            return E_INVALIDARG;
        }
        Flags |= D3D12DDI_EXPORT_SUMMARY_FLAG_UNRESOLVED_RESOURCE_BINDINGS;
        break;
    }

    for (UINT Index = 0; Index < Function.Source->DependencyCount; Index++)
    {
        if (IsDefinedAnywhere(Function.Source->Dependencies[Index]))
            continue;
        if (!MayDefer())
        {
            RX_TRACE(L"state object: export %ls calls %ls, which is not defined", Function.Name,
                     Function.Source->Dependencies[Index]);
            return E_INVALIDARG;
        }
        Flags |= D3D12DDI_EXPORT_SUMMARY_FLAG_UNRESOLVED_FUNCTIONS;
    }

    Function.Flags = Flags;
    return S_OK;
}

HRESULT
RxStateBuilder::ResolveHitGroup(
    _Inout_ RxHitGroupExport& Group)
{
    static const UINT Kinds[3] = { RxDxilKindAnyHit, RxDxilKindClosestHit, RxDxilKindIntersection };
    const RxSubobjectRecord* Local = nullptr;
    UINT Flags = 0;

    for (UINT Import = 0; Import < 3; Import++)
    {
        const RxFunctionExport* Function;
        const RxSubobjectRecord* Root;

        if (Group.Imports[Import] == nullptr)
            continue;

        Function = FindVisibleFunction(Group.Imports[Import]);
        if (Function == nullptr)
        {
            if (!MayDefer())
            {
                RX_TRACE(L"state object: hit group %ls imports %ls, which is not an export", Group.Name, Group.Imports[Import]);
                return E_INVALIDARG;
            }
            Flags |= D3D12DDI_EXPORT_SUMMARY_FLAG_UNRESOLVED_FUNCTIONS;
            continue;
        }
        if (Function->Kind != Kinds[Import])
        {
            RX_TRACE(L"state object: hit group %ls imports %ls, which is the wrong kind of shader", Group.Name,
                     Group.Imports[Import]);
            return E_INVALIDARG;
        }
        if (Function->Sealed)
        {
            RX_TRACE(L"state object: hit group %ls imports %ls from a collection that does not share it", Group.Name,
                     Group.Imports[Import]);
            return E_INVALIDARG;
        }

        /* Members that have a local root signature must agree on it */
        Root = RxFirstInSlot(*Function, RxSlotLocalRoot);
        if (Root != nullptr)
        {
            if (Local != nullptr && !RxSameDefinition(Local, Root))
            {
                RX_TRACE(L"state object: members of hit group %ls have different local root signatures", Group.Name);
                return E_INVALIDARG;
            }
            Local = Root;
        }
        Flags |= Function->Flags;
    }

    Group.Flags = Flags;
    Group.Ddi->SummaryFlags = (D3D12DDI_EXPORT_SUMMARY_FLAGS)Flags;
    return S_OK;
}

/* Shader and pipeline configs, global root signatures and node masks must agree across the object */
HRESULT
RxStateBuilder::CheckShared()
{
    static const RxSlot Slots[] = { RxSlotShaderConfig, RxSlotPipelineConfig, RxSlotGlobalRoot, RxSlotNodeMask };

    for (UINT Each = 0; Each < _countof(Slots); Each++)
    {
        RxSlot Slot = Slots[Each];
        const RxSubobjectRecord* Shared = Target.Parent != nullptr ? Target.Parent->GetShared(Slot) : nullptr;

        for (UINT Index = 0; Index < Functions.Count; Index++)
        {
            const RxFunctionExport& Function = Functions.Items[Index];
            const RxSubobjectRecord* Record;

            if (!RxSlotFits(Slot, Function.Kind))
                continue;
            Record = RxFirstInSlot(Function, Slot);
            if (Record == nullptr)
                continue;

            if (Shared != nullptr && !RxSameDefinition(Shared, Record))
            {
                RX_TRACE(L"state object: export %ls disagrees with the rest of the object on slot %u", Function.Name, Slot);
                return E_INVALIDARG;
            }
            Shared = Record;
        }
        Target.Shared[Slot] = Shared;
    }
    return S_OK;
}

/* Program names are unique within the object */
HRESULT
RxStateBuilder::BuildWorkGraphs()
{
    HRESULT hr;

    for (UINT Each = 0; Each < GraphSubobjects.Count; Each++)
    {
        const D3D12_WORK_GRAPH_DESC* Api =
            static_cast<const D3D12_WORK_GRAPH_DESC*>(Desc.pSubobjects[GraphSubobjects.Items[Each]].pDesc);
        RxWorkGraph Graph;

        hr = RxBuildWorkGraph(Arena, *Api, Functions.Count, Functions.Items, &Graph);
        if (FAILED(hr))
            return hr;

        for (UINT Other = 0; Other < Graphs.Count; Other++)
        {
            if (wcscmp(Graphs.Items[Other].Name, Graph.Name) == 0)
            {
                RX_TRACE(L"state object: program %ls is defined twice", Graph.Name);
                return E_INVALIDARG;
            }
        }

        hr = Graphs.Push(Arena, Graph);
        if (FAILED(hr))
            return hr;
    }

    Target.WorkGraphs = Graphs.Items;
    Target.WorkGraphCount = Graphs.Count;
    return S_OK;
}

/* A library, collection or hit group the desc gave, with the hit groups a library defines after it */
VOID
RxStateBuilder::EmitApiEntry(
    _In_ UINT Index)
{
    Entries[EntryCount++] = ApiEntries[Index];
    if (ApiEntries[Index].Type != D3D12DDI_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY)
        return;

    for (UINT Group = 0; Group < HitGroups.Count; Group++)
    {
        const RxHitGroupExport& Each = HitGroups.Items[Group];

        if (Each.Owner == &Target && Each.LibraryIndex == ApiLibrary[Index])
        {
            Entries[EntryCount].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_HIT_GROUP;
            Entries[EntryCount++].pDesc = Each.Ddi;
        }
    }
}

_Ret_maybenull_
const D3D12DDI_STATE_SUBOBJECT_0054*
RxStateBuilder::EntryFor(
    _In_ const RxSubobjectRecord* Record)
{
    RxEmittedRecord Added;

    for (UINT Index = 0; Index < Emitted.Count; Index++)
    {
        if (Emitted.Items[Index].Record == Record)
            return Emitted.Items[Index].Entry;
    }

    if (EntryCount == EntryCapacity)
        return nullptr;

    Entries[EntryCount].Type = Record->DdiType;
    Entries[EntryCount].pDesc = Record->DdiDesc;
    Added.Record = Record;
    Added.Entry = &Entries[EntryCount++];
    if (FAILED(Emitted.Push(Scratch, Added)))
        return nullptr;
    return Added.Entry;
}

/*
 * The driver gets the desc grouped by type without associations; library hit groups follow their
 * library, subobjects only exports refer to are added as first referenced, and the summary is last.
 */
HRESULT
RxStateBuilder::Emit()
{
    D3D12DDI_FUNCTION_SUMMARY_NODE_0054* Nodes;
    D3D12DDI_FUNCTION_SUMMARY_0054* Summary;
    UINT Overall = 0;

    EntryCapacity = Desc.NumSubobjects + HitGroups.Count + Records.Count + Graphs.Count + 1;
    for (UINT Index = 0; Index < Functions.Count; Index++)
        EntryCapacity += Functions.Items[Index].AssociationCount;

    Entries = Arena.AllocateArray<D3D12DDI_STATE_SUBOBJECT_0054>(EntryCapacity);
    Nodes = Arena.AllocateArray<D3D12DDI_FUNCTION_SUMMARY_NODE_0054>(Functions.Count);
    Summary = Arena.AllocateArray<D3D12DDI_FUNCTION_SUMMARY_0054>(1);
    if (Entries == nullptr || Nodes == nullptr || Summary == nullptr)
        return E_OUTOFMEMORY;

    /* Executable objects lead with their code, then their programs, wherever the desc put them */
    if (Target.Type == D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
    {
        for (UINT Index = 0; Index < Desc.NumSubobjects; Index++)
        {
            if (ApiEntries[Index].Type == D3D12DDI_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY ||
                ApiEntries[Index].Type == D3D12DDI_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION)
            {
                if (ApiEntries[Index].pDesc != nullptr)
                    EmitApiEntry(Index);
            }
        }

        for (UINT Graph = 0; Graph < Graphs.Count; Graph++)
        {
            Entries[EntryCount].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_WORK_GRAPH;
            Entries[EntryCount++].pDesc = Graphs.Items[Graph].Ddi;
        }
    }

    for (UINT Position = 0; Position < Desc.NumSubobjects; Position++)
    {
        UINT Index = ApiOrder[Position];

        if (ApiRecords[Index] != nullptr)
        {
            if (EntryFor(ApiRecords[Index]) == nullptr)
                return E_OUTOFMEMORY;
            continue;
        }
        if (ApiEntries[Index].pDesc == nullptr)
            continue;
        if (Target.Type == D3D12_STATE_OBJECT_TYPE_EXECUTABLE &&
            (ApiEntries[Index].Type == D3D12DDI_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY ||
             ApiEntries[Index].Type == D3D12DDI_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION))
        {
            continue;
        }

        EmitApiEntry(Index);
    }

    for (UINT Index = 0; Index < Functions.Count; Index++)
    {
        RxFunctionExport& Function = Functions.Items[Index];
        D3D12DDI_FUNCTION_SUMMARY_NODE_0054& Node = Nodes[Index];

        if (Function.Node != nullptr)
        {
            /* A finished collection export keeps pointing at the collection's subobjects */
            Node = *Function.Node;
        }
        else
        {
            const D3D12DDI_STATE_SUBOBJECT_0054** Linked =
                Arena.AllocateArray<const D3D12DDI_STATE_SUBOBJECT_0054*>(Function.AssociationCount);

            if (Linked == nullptr)
                return E_OUTOFMEMORY;
            for (UINT Each = 0; Each < Function.AssociationCount; Each++)
            {
                Linked[Each] = EntryFor(Function.Associations[Each]);
                if (Linked[Each] == nullptr)
                    return E_OUTOFMEMORY;
            }

            Node.NumAssociatedSubobjects = Function.AssociationCount;
            Node.ppAssociatedSubobjects = Linked;
            Node.Flags = (D3D12DDI_EXPORT_SUMMARY_FLAGS)Function.Flags;
        }

        Node.ExportNameUnmangled = Function.Name;
        Node.ExportNameMangled = Function.Mangled;
        Overall |= (UINT)Node.Flags;
        Function.Node = &Node;
    }

    for (UINT Group = 0; Group < HitGroups.Count; Group++)
    {
        if (HitGroups.Items[Group].Owner == &Target)
            Overall |= HitGroups.Items[Group].Flags;
    }

    Summary->NumExportedFunctions = Functions.Count;
    Summary->pSummaries = Nodes;
    Summary->OverallFlags = (D3D12DDI_EXPORT_SUMMARY_FLAGS)Overall;
    if (EntryCount == EntryCapacity)
        return E_OUTOFMEMORY;
    Entries[EntryCount].Type = D3D12DDI_STATE_SUBOBJECT_TYPE_SHADER_EXPORT_SUMMARY;
    Entries[EntryCount++].pDesc = Summary;

    Target.Functions = Functions.Items;
    Target.FunctionCount = Functions.Count;
    Target.HitGroups = HitGroups.Items;
    Target.HitGroupCount = HitGroups.Count;
    Target.Nodes = Nodes;
    Target.NodeCount = Functions.Count;
    return S_OK;
}

HRESULT
RxStateBuilder::CreateDriverObject()
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDI_HRTSTATEOBJECT_0054 Runtime;
    SIZE_T Size;
    HRESULT hr;

    Runtime.handle = static_cast<ID3D12StateObject*>(&Target);

    if (Target.Parent != nullptr)
    {
        D3D12DDIARG_ADD_TO_STATE_OBJECT_0072 Args = {};

        if (Ddi.pfnCalcPrivateAddToStateObjectSize == nullptr || Ddi.pfnAddToStateObject == nullptr)
            return E_NOTIMPL;

        Args.Type = (D3D12DDI_STATE_OBJECT_TYPE)Target.Type;
        Args.NumSubobjects = EntryCount;
        Args.pSubobjects = Entries;
        Args.StateObjectToGrowFrom = Target.Parent->GetDriverHandle();

        Size = Ddi.pfnCalcPrivateAddToStateObjectSize(Device.Handle(), &Args);
        Target.DriverObject.pDrvPrivate = Target.Memory.Allocate(Size);
        if (Target.DriverObject.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;
        hr = Ddi.pfnAddToStateObject(Device.Handle(), &Args, Target.DriverObject, Runtime);
    }
    else
    {
        D3D12DDIARG_CREATE_STATE_OBJECT_0054 Args = {};

        if (Ddi.pfnCalcPrivateStateObjectSize == nullptr || Ddi.pfnCreateStateObject == nullptr)
            return E_NOTIMPL;

        Args.Type = (D3D12DDI_STATE_OBJECT_TYPE)Target.Type;
        Args.NumSubobjects = EntryCount;
        Args.pSubobjects = Entries;

        Size = Ddi.pfnCalcPrivateStateObjectSize(Device.Handle(), &Args);
        Target.DriverObject.pDrvPrivate = Target.Memory.Allocate(Size);
        if (Target.DriverObject.pDrvPrivate == nullptr)
            return E_OUTOFMEMORY;
        hr = Ddi.pfnCreateStateObject(Device.Handle(), &Args, Target.DriverObject, Runtime);
    }

    if (FAILED(hr))
    {
        RX_TRACE(L"driver state object failed 0x%08lX", hr);
        return hr;
    }
    Target.IsDriverObjectCreated = TRUE;

    Target.Bypass.pInterfaceVtable = *reinterpret_cast<void**>(static_cast<ID3D12StateObject*>(&Target));
    Target.Bypass.pDriverObject = Target.DriverObject.pDrvPrivate;
    return Device.MakePageableResident(Target.DriverObject.pDrvPrivate, D3D12DDI_HT_0054_STATE_OBJECT);
}

HRESULT
RxStateBuilder::Build()
{
    HRESULT hr;

    if (Desc.NumSubobjects != 0 && Desc.pSubobjects == nullptr)
    {
        RX_TRACE(L"state object: subobjects missing");
        return E_INVALIDARG;
    }

    ApiRecords = Scratch.AllocateArray<RxSubobjectRecord*>(Desc.NumSubobjects);
    ApiEntries = Scratch.AllocateArray<D3D12DDI_STATE_SUBOBJECT_0054>(Desc.NumSubobjects);
    ApiLibrary = Scratch.AllocateArray<UINT>(Desc.NumSubobjects);
    if (ApiRecords == nullptr || ApiEntries == nullptr || ApiLibrary == nullptr)
        return E_OUTOFMEMORY;

    hr = ReadSubobjects();
    if (SUCCEEDED(hr))
        hr = GroupByType();
    if (SUCCEEDED(hr))
        hr = ReadAssociations();

    for (UINT Index = 0; Index < Functions.Count && SUCCEEDED(hr); Index++)
    {
        RxFunctionExport& Function = Functions.Items[Index];

        /* Collection exports that left nothing open keep what the collection resolved */
        if (Function.Node == nullptr)
            hr = ResolveFunction(Function);
    }

    for (UINT Index = 0; Index < HitGroups.Count && SUCCEEDED(hr); Index++)
    {
        if (HitGroups.Items[Index].Owner == &Target)
            hr = ResolveHitGroup(HitGroups.Items[Index]);
    }

    if (SUCCEEDED(hr))
        hr = CheckShared();
    if (SUCCEEDED(hr))
        hr = BuildWorkGraphs();
    if (SUCCEEDED(hr))
        hr = Emit();
    if (SUCCEEDED(hr))
        hr = CreateDriverObject();
    return hr;
}

/* The object */

HRESULT
RxStateObject::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_STATE_OBJECT_DESC& Desc,
    _In_opt_ RxStateObject* Parent,
    _Outptr_ RxStateObject** Object)
{
    RxStateObject* Created;
    HRESULT hr;

    *Object = nullptr;

    if (Desc.Type != D3D12_STATE_OBJECT_TYPE_COLLECTION && Desc.Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE &&
        Desc.Type != D3D12_STATE_OBJECT_TYPE_EXECUTABLE)
    {
        RX_TRACE(L"state object: type %d is not valid", Desc.Type);
        return E_INVALIDARG;
    }

    Created = new (std::nothrow) RxStateObject(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    Created->Type = Desc.Type;
    if (Parent != nullptr)
    {
        Parent->AddUse();
        Created->Parent = Parent;
    }

    {
        RxStateBuilder Builder(Device, Desc, *Created);

        hr = Builder.Build();
    }
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Object = Created;
    return S_OK;
}

RxStateObject::~RxStateObject()
{
    if (IsDriverObjectCreated)
        Device.Ddi().pfnDestroyStateObject(Device.Handle(), DriverObject);

    for (UINT Index = 0; Index < RootCount; Index++)
        Roots[Index]->ReleaseUse();
    for (UINT Index = 0; Index < CollectionCount; Index++)
        Collections[Index]->ReleaseUse();
    if (Parent != nullptr)
        Parent->ReleaseUse();
}

HRESULT
STDMETHODCALLTYPE
RxStateObject::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable))
    {
        *ppvObject = ChildFace();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12StateObject) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12StateObject*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12StateObjectProperties) || riid == __uuidof(ID3D12StateObjectProperties1))
    {
        *ppvObject = static_cast<ID3D12StateObjectProperties1*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12WorkGraphProperties))
    {
        *ppvObject = static_cast<ID3D12WorkGraphProperties*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

_Ret_maybenull_
const RxHitGroupExport*
RxStateObject::FindHitGroup(
    _In_z_ LPCWSTR Name) const
{
    for (const RxStateObject* Each = this; Each != nullptr; Each = Each->Parent)
    {
        for (UINT Index = 0; Index < Each->HitGroupCount; Index++)
        {
            if (wcscmp(Each->HitGroups[Index].Name, Name) == 0)
                return &Each->HitGroups[Index];
        }
    }
    return nullptr;
}

_Ret_maybenull_
const RxFunctionExport*
RxStateObject::FindFunction(
    _In_z_ LPCWSTR Name,
    _Out_opt_ UINT* Count) const
{
    const RxFunctionExport* Found = nullptr;
    UINT Matches = 0;

    for (const RxStateObject* Each = this; Each != nullptr; Each = Each->Parent)
    {
        for (UINT Index = 0; Index < Each->FunctionCount; Index++)
        {
            if (wcscmp(Each->Functions[Index].Name, Name) == 0)
            {
                Found = &Each->Functions[Index];
                Matches++;
            }
        }
    }

    if (Count != nullptr)
        *Count = Matches;
    return Matches == 1 ? Found : nullptr;
}

void*
STDMETHODCALLTYPE
RxStateObject::GetShaderIdentifier(
    _In_ LPCWSTR pExportName)
{
    const RxHitGroupExport* Group;
    const RxFunctionExport* Function;
    BOOLEAN Eligible;
    void* Identifier = nullptr;

    if (pExportName == nullptr)
        return nullptr;

    AcquireSRWLockExclusive(&Lock);

    /* Hit groups with everything resolved, and ray generation, miss and callable shaders */
    Group = FindHitGroup(pExportName);
    if (Group != nullptr)
    {
        Eligible = Group->Flags == 0;
    }
    else
    {
        Function = FindFunction(pExportName, nullptr);
        Eligible = Function != nullptr && Function->Flags == 0 &&
                   (Function->Kind == RxDxilKindRayGeneration || Function->Kind == RxDxilKindMiss ||
                    Function->Kind == RxDxilKindCallable);
    }

    if (Eligible)
        Identifier = Device.Ddi().pfnGetShaderIdentifier(DriverObject, pExportName);

    ReleaseSRWLockExclusive(&Lock);
    return Identifier;
}

UINT64
STDMETHODCALLTYPE
RxStateObject::GetShaderStackSize(
    _In_ LPCWSTR pExportName)
{
    const WCHAR* Separator;
    BOOLEAN Eligible = FALSE;
    UINT64 Size = RxNoStackSize;

    if (pExportName == nullptr)
        return RxNoStackSize;

    AcquireSRWLockExclusive(&Lock);

    /* Hit group members are named group::closesthit, group::anyhit or group::intersection */
    Separator = wcsstr(pExportName, L"::");
    if (Separator != nullptr)
    {
        SIZE_T Length = (SIZE_T)(Separator - pExportName);
        WCHAR* GroupName = static_cast<WCHAR*>(_malloca((Length + 1) * sizeof(WCHAR)));

        if (GroupName != nullptr)
        {
            const RxHitGroupExport* Group;

            CopyMemory(GroupName, pExportName, Length * sizeof(WCHAR));
            GroupName[Length] = L'\0';
            Group = FindHitGroup(GroupName);

            if (Group != nullptr)
            {
                LPCWSTR Member = Separator + 2;
                LPCWSTR Import = nullptr;

                if (wcscmp(Member, L"closesthit") == 0)
                    Import = Group->Imports[RxImportClosestHit];
                else if (wcscmp(Member, L"anyhit") == 0)
                    Import = Group->Imports[RxImportAnyHit];
                else if (wcscmp(Member, L"intersection") == 0)
                    Import = Group->Imports[RxImportIntersection];

                if (Import != nullptr)
                {
                    const RxFunctionExport* Function = Group->Owner->FindFunction(Import, nullptr);

                    Eligible = Function == nullptr || Function->Flags == 0;
                }
            }
            _freea(GroupName);
        }
    }
    else
    {
        const RxFunctionExport* Function = FindFunction(pExportName, nullptr);

        Eligible = Function != nullptr && Function->Flags == 0 && Function->Kind != RxDxilKindLibrary &&
                   Function->Kind != RxDxilKindIntersection && Function->Kind != RxDxilKindAnyHit &&
                   Function->Kind != RxDxilKindClosestHit;
    }

    if (Eligible)
        Size = Device.Ddi().pfnGetShaderStackSize(DriverObject, pExportName);

    ReleaseSRWLockExclusive(&Lock);
    return Size;
}

UINT64
STDMETHODCALLTYPE
RxStateObject::GetPipelineStackSize(VOID)
{
    if (Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
        return RxNoStackSize;
    return Device.Ddi().pfnGetPipelineStackSize(DriverObject);
}

void
STDMETHODCALLTYPE
RxStateObject::SetPipelineStackSize(
    UINT64 PipelineStackSizeInBytes)
{
    if (PipelineStackSizeInBytes >= RxNoStackSize || Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
        return;
    Device.Ddi().pfnSetPipelineStackSize(DriverObject, (UINT)PipelineStackSizeInBytes);
}

/* Device entry points */

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateStateObject(
    _In_ const D3D12_STATE_OBJECT_DESC* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppStateObject)
{
    RxStateObject* Object;
    HRESULT hr;

    if (ppStateObject != nullptr)
        *ppStateObject = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;
    if (FAILED(CheckUsable()))
        return DXGI_ERROR_DEVICE_REMOVED;

    hr = RxStateObject::Create(*this, *pDesc, nullptr, &Object);
    if (FAILED(hr))
        return hr;
    return RxHandOut12(Object, riid, ppStateObject);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::AddToStateObject(
    _In_ const D3D12_STATE_OBJECT_DESC* pAddition,
    _In_ ID3D12StateObject* pStateObjectToGrowFrom,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppNewStateObject)
{
    RxStateObject* Parent = RxUnwrap12<RxStateObject>(pStateObjectToGrowFrom);
    RxStateObject* Object;
    HRESULT hr;

    if (ppNewStateObject != nullptr)
        *ppNewStateObject = nullptr;
    if (pAddition == nullptr || Parent == nullptr)
        return E_INVALIDARG;
    if (FAILED(CheckUsable()))
        return DXGI_ERROR_DEVICE_REMOVED;

    /* Only raytracing pipelines that opted in grow, and only by more of the same */
    if (Parent->GetType() != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE ||
        pAddition->Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE ||
        (Parent->GetFlags() & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS) == 0)
    {
        RX_TRACE(L"state object: AddToStateObject on an object that does not take additions");
        return E_INVALIDARG;
    }

    hr = RxStateObject::Create(*this, *pAddition, Parent, &Object);
    if (FAILED(hr))
        return hr;
    return RxHandOut12(Object, riid, ppNewStateObject);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateRootSignatureFromSubobjectInLibrary(
    _In_ UINT nodeMask,
    _In_reads_(blobLengthInBytes) const void* pLibraryBlob,
    _In_ SIZE_T blobLengthInBytes,
    _In_opt_ LPCWSTR subobjectName,
    REFIID riid,
    _COM_Outptr_ void** ppvRootSignature)
{
    const RxDxilSubobject* Found = nullptr;
    RxRootSignature* Signature;
    RxDxilLibrary Library;
    RxArena Temporary;
    HRESULT hr;

    if (ppvRootSignature == nullptr)
        return S_FALSE;
    *ppvRootSignature = nullptr;

    hr = Library.Load(Temporary, pLibraryBlob, blobLengthInBytes);
    if (FAILED(hr))
        return hr == E_OUTOFMEMORY ? hr : E_INVALIDARG;

    /* Without a name the library must hold exactly one root signature */
    for (UINT Index = 0; Index < Library.SubobjectCount; Index++)
    {
        const RxDxilSubobject& Each = Library.Subobjects[Index];

        if (Each.Kind != RxDxilSubobjectGlobalRoot && Each.Kind != RxDxilSubobjectLocalRoot)
            continue;
        if (subobjectName != nullptr)
        {
            if (wcscmp(Each.Name, subobjectName) == 0)
            {
                Found = &Each;
                break;
            }
            continue;
        }
        if (Found != nullptr)
            return E_INVALIDARG;
        Found = &Each;
    }

    if (Found == nullptr)
    {
        RX_TRACE(L"library has no root signature subobject %ls", subobjectName != nullptr ? subobjectName : L"(none)");
        return E_INVALIDARG;
    }

    hr = RxAcquireRootSignature(*this, nodeMask, Found->Bytes, Found->ByteCount, &Signature);
    if (FAILED(hr))
        return hr;

    hr = Signature->QueryInterface(riid, ppvRootSignature);
    Signature->Release();
    return hr;
}
