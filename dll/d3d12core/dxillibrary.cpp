/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXIL library runtime data: the functions and subobjects a library defines
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "stateobject.hpp"

/* Arena */

constexpr SIZE_T RxArenaBlockSize = 16 * 1024;

RxArena::~RxArena()
{
    while (Head != nullptr)
    {
        RxBlock* Next = Head->Next;

        free(Head);
        Head = Next;
    }
}

_Ret_maybenull_
VOID*
RxArena::Allocate(
    _In_ SIZE_T Size)
{
    const SIZE_T HeaderSize = (sizeof(RxBlock) + 15) & ~(SIZE_T)15;
    BYTE* Memory;

    Size = (Size + 15) & ~(SIZE_T)15;
    if (Size == 0)
        Size = 16;

    if (Head == nullptr || Head->Size - Head->Used < Size)
    {
        SIZE_T Capacity = Size > RxArenaBlockSize ? Size : RxArenaBlockSize;
        RxBlock* Block = static_cast<RxBlock*>(malloc(HeaderSize + Capacity));

        if (Block == nullptr)
            return nullptr;
        Block->Next = Head;
        Block->Used = 0;
        Block->Size = Capacity;
        Head = Block;
    }

    Memory = reinterpret_cast<BYTE*>(Head) + HeaderSize + Head->Used;
    Head->Used += Size;
    ZeroMemory(Memory, Size);
    return Memory;
}

_Ret_maybenull_
LPWSTR
RxArena::CopyString(
    _In_opt_z_ LPCWSTR Text)
{
    SIZE_T Length;
    LPWSTR Copy;

    if (Text == nullptr)
        return nullptr;

    Length = wcslen(Text);
    Copy = AllocateArray<WCHAR>(Length + 1);
    if (Copy != nullptr)
        CopyMemory(Copy, Text, Length * sizeof(*Copy));
    return Copy;
}

_Ret_maybenull_
LPWSTR
RxArena::Widen(
    _In_z_ const char* Text)
{
    LPWSTR Wide;
    int Count;

    Count =MultiByteToWideChar(CP_UTF8, 0, Text, -1, nullptr, 0);
    if (Count <= 0)
        return nullptr;

    Wide = AllocateArray<WCHAR>((SIZE_T)Count);
    if (Wide == nullptr)
        return nullptr;
    MultiByteToWideChar(CP_UTF8, 0, Text, -1, Wide, Count);
    return Wide;
}

/* Runtime data */

constexpr UINT RxRuntimeDataVersion = 0x10;

/* Container parts a library is read from: the program and its runtime data */
constexpr UINT RxProgramPart = 0x4C495844;
constexpr UINT RxRuntimeDataPart = 0x54414452;

enum RxRuntimePart
{
    RxPartStrings = 1,
    RxPartIndices = 2,
    RxPartResources = 3,
    RxPartFunctions = 4,
    RxPartRawBytes = 5,
    RxPartSubobjects = 6,
    RxPartNodeIds = 7,
    RxPartNodeRecordAttributes = 8,
    RxPartNodeAttributes = 9,
    RxPartNodeRecords = 10,
    RxPartNodes = 11,
};

/* Record layouts; newer compilers append fields, so only the stride is trusted */
struct RxRuntimeFunction
{
    UINT Name;
    UINT UnmangledName;
    UINT Resources;
    UINT Dependencies;
    UINT ShaderKind;
    UINT PayloadSize;
    UINT AttributeSize;
    UINT Features[2];
    UINT StageFlags;
    UINT MinimumTarget;
    /* Wave lane counts in the low half, shader flags above; then the per kind record */
    UINT WaveAndFlags;
    UINT Detail;
};

/* The shader flag marking a node as a program entry */
constexpr UINT RxNodeEntryFlag = 0x10000;

struct RxRuntimeNodeId
{
    UINT Name;
    UINT Index;
};

/* An attribute: a kind and one value, an index list or a record reference */
struct RxRuntimeAttribute
{
    UINT Kind;
    UINT Value;
};

struct RxRuntimeNodeRecord
{
    UINT KindAndFlags;
    UINT Attributes;
};

struct RxRuntimeNode
{
    UINT Launch;
    UINT GroupShared;
    UINT Attributes;
    UINT Outputs;
    UINT Inputs;
};

/* Node attribute kinds */
constexpr UINT RxNodeAttributeId = 1;
constexpr UINT RxNodeAttributeShareInput = 3;
constexpr UINT RxNodeAttributeDispatchGrid = 4;
constexpr UINT RxNodeAttributeRecursion = 5;
constexpr UINT RxNodeAttributeLocalRootIndex = 6;
constexpr UINT RxNodeAttributeMaxDispatchGrid = 7;

/* Record attribute kinds */
constexpr UINT RxRecordAttributeTarget = 1;
constexpr UINT RxRecordAttributeMaxRecords = 2;
constexpr UINT RxRecordAttributeSharedWith = 3;
constexpr UINT RxRecordAttributeSize = 4;
constexpr UINT RxRecordAttributeGrid = 5;
constexpr UINT RxRecordAttributeArraySize = 6;
constexpr UINT RxRecordAttributeSparse = 7;
constexpr UINT RxRecordAttributeAlignment = 8;

/* Records declare kind in the low bits and tracking of shared writes above */
constexpr UINT RxRecordKindMask = 0x7F;
constexpr UINT RxRecordFlagMask = 0x100;

/* Grid components: 16 or 32 bit unsigned */
constexpr UINT RxGridComponent16 = 3;
constexpr UINT RxGridComponent32 = 5;

/* More outputs than this is not a valid node */
constexpr UINT RxMaxNodeOutputs = 0x400;

struct RxRuntimeResource
{
    UINT Class;
    UINT Kind;
    UINT Id;
    UINT Space;
    UINT Lower;
    UINT Upper;
    UINT Name;
    UINT Flags;
};

struct RxRuntimeSubobject
{
    UINT Kind;
    UINT Name;
    UINT Data[4];
};

/* A bounds checked view of one runtime data part */
struct RxPartView
{
    const BYTE* Data;
    UINT Size;
};

/* A table part: a count and stride, then the records */
struct RxTableView
{
    const BYTE* Records;
    UINT Count;
    UINT Stride;
};

class RxRuntimeData
{
public:
    BOOLEAN
    Read(
        _In_reads_bytes_(Size) const BYTE* Data,
        _In_ UINT Size);

    _Ret_maybenull_
    const char*
    String(
        _In_ UINT Offset) const;

    /* An index list: its count, then that many values */
    BOOLEAN
    List(
        _In_ UINT Index,
        _Out_ UINT* Count,
        _Out_ const UINT** Values) const;

    BOOLEAN
    Record(
        _In_ const RxTableView& Table,
        _In_ UINT Index,
        _Out_writes_bytes_(Size) VOID* Out,
        _In_ UINT Size) const;

    RxPartView Parts[RxPartNodes + 1] = {};
    RxTableView Functions = {};
    RxTableView Resources = {};
    RxTableView Subobjects = {};
    RxTableView NodeIds = {};
    RxTableView NodeRecordAttributes = {};
    RxTableView NodeAttributes = {};
    RxTableView NodeRecords = {};
    RxTableView Nodes = {};

private:
    BOOLEAN
    Table(
        _In_ UINT Kind,
        _Out_ RxTableView* View) const;
};

BOOLEAN
RxRuntimeData::Read(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ UINT Size)
{
    UINT Version;
    UINT Count;

    if (Size < 2 * sizeof(UINT))
        return FALSE;
    CopyMemory(&Version, Data, sizeof(Version));
    CopyMemory(&Count, Data + 4, sizeof(Count));
    if (Version != RxRuntimeDataVersion || Count > (Size - 8) / sizeof(UINT))
        return FALSE;

    for (UINT Index = 0; Index < Count; Index++)
    {
        UINT Offset;
        UINT Kind;
        UINT Length;

        CopyMemory(&Offset, Data + 8 + Index * sizeof(UINT), sizeof(Offset));
        if (Offset > Size || Size - Offset < 8)
            return FALSE;
        CopyMemory(&Kind, Data + Offset, sizeof(Kind));
        CopyMemory(&Length, Data + Offset + 4, sizeof(Length));
        if (Length > Size - Offset - 8)
            return FALSE;

        /* Parts this runtime has no use for are skipped */
        if (Kind < _countof(Parts))
        {
            Parts[Kind].Data = Data + Offset + 8;
            Parts[Kind].Size = Length;
        }
    }

    return Table(RxPartFunctions, &Functions) && Table(RxPartResources, &Resources) && Table(RxPartSubobjects, &Subobjects) &&
           Table(RxPartNodeIds, &NodeIds) && Table(RxPartNodeRecordAttributes, &NodeRecordAttributes) &&
           Table(RxPartNodeAttributes, &NodeAttributes) && Table(RxPartNodeRecords, &NodeRecords) && Table(RxPartNodes, &Nodes);
}

BOOLEAN
RxRuntimeData::Table(
    _In_ UINT Kind,
    _Out_ RxTableView* View) const
{
    const RxPartView& Part = Parts[Kind];
    UINT Count;
    UINT Stride;

    View->Records = nullptr;
    View->Count = 0;
    View->Stride = 0;
    if (Part.Data == nullptr)
        return TRUE;
    if (Part.Size < 8)
        return FALSE;

    CopyMemory(&Count, Part.Data, sizeof(Count));
    CopyMemory(&Stride, Part.Data + 4, sizeof(Stride));
    if (Stride == 0 ? Count != 0 : Count > (Part.Size - 8) / Stride)
        return FALSE;

    View->Records = Part.Data + 8;
    View->Count = Count;
    View->Stride = Stride;
    return TRUE;
}

_Ret_maybenull_
const char*
RxRuntimeData::String(
    _In_ UINT Offset) const
{
    const RxPartView& Part = Parts[RxPartStrings];

    if (Part.Data == nullptr || Offset >= Part.Size)
        return nullptr;
    if (memchr(Part.Data + Offset, 0, Part.Size - Offset) == nullptr)
        return nullptr;
    return reinterpret_cast<const char*>(Part.Data + Offset);
}

BOOLEAN
RxRuntimeData::List(
    _In_ UINT Index,
    _Out_ UINT* Count,
    _Out_ const UINT** Values) const
{
    const RxPartView& Part = Parts[RxPartIndices];
    UINT Total = Part.Size / sizeof(UINT);
    UINT Length;

    *Count = 0;
    *Values = nullptr;

    /* No list at all reads as an empty one */
    if (Index == UINT_MAX)
        return TRUE;
    if (Part.Data == nullptr || Index >= Total)
        return FALSE;

    CopyMemory(&Length, Part.Data + Index * sizeof(UINT), sizeof(Length));
    if (Length > Total - Index - 1)
        return FALSE;

    *Count = Length;
    *Values = reinterpret_cast<const UINT*>(Part.Data + (Index + 1) * sizeof(UINT));
    return TRUE;
}

BOOLEAN
RxRuntimeData::Record(
    _In_ const RxTableView& Table,
    _In_ UINT Index,
    _Out_writes_bytes_(Size) VOID* Out,
    _In_ UINT Size) const
{
    ZeroMemory(Out, Size);
    if (Index >= Table.Count)
        return FALSE;
    CopyMemory(Out, Table.Records + (SIZE_T)Index * Table.Stride, Size < Table.Stride ? Size : Table.Stride);
    return TRUE;
}

/* Library */

static
_Ret_maybenull_
LPWSTR
RxWidenOffset(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Offset)
{
    const char* Text = Data.String(Offset);

    return Text != nullptr ? Arena.Widen(Text) : nullptr;
}

/* The names in a list of string offsets, widened */
static
BOOLEAN
RxWidenList(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Index,
    _Out_ UINT* Count,
    _Out_ LPCWSTR** Names)
{
    const UINT* Offsets;
    LPCWSTR* Out;

    *Count = 0;
    *Names = nullptr;
    if (!Data.List(Index, Count, &Offsets))
        return FALSE;

    Out = Arena.AllocateArray<LPCWSTR>(*Count);
    if (Out == nullptr)
        return FALSE;

    for (UINT Each = 0; Each < *Count; Each++)
    {
        Out[Each] = RxWidenOffset(Arena, Data, Offsets[Each]);
        if (Out[Each] == nullptr)
            return FALSE;
    }

    *Names = Out;
    return TRUE;
}

/* Node shaders */

static
BOOLEAN
RxReadNodeId(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Index,
    _Out_ LPCWSTR* Name,
    _Out_ UINT* ArrayIndex)
{
    RxRuntimeNodeId Record;

    *Name = nullptr;
    *ArrayIndex = 0;
    if (!Data.Record(Data.NodeIds, Index, &Record, sizeof(Record)))
        return FALSE;

    *Name = RxWidenOffset(Arena, Data, Record.Name);
    *ArrayIndex = Record.Index;
    return *Name != nullptr;
}

/* Up to three values from an index list, missing ones as 1; an empty list is no grid */
static
BOOLEAN
RxReadGridValues(
    _In_ const RxRuntimeData& Data,
    _In_ UINT List,
    _Out_writes_(3) UINT* Grid)
{
    const UINT* Values;
    UINT Count;

    if (!Data.List(List, &Count, &Values) || Count == 0)
        return FALSE;

    for (UINT Each = 0; Each < 3; Each++)
        Grid[Each] = Each < Count ? Values[Each] : 1;
    return TRUE;
}

/* The grid a record carries: byte offset below, component count and type above; no components is no grid */
static
BOOLEAN
RxReadRecordGrid(
    _In_ UINT Value,
    _Inout_ RxDxilNodeRecord* Record)
{
    UINT Packed = Value >> 16;
    UINT Components = Packed & 3;
    UINT Type = Packed >> 2;

    if (Components == 0)
        return TRUE;

    Record->HasGrid = TRUE;
    Record->Grid.ByteOffset = (UINT16)Value;
    Record->Grid.NumComponents = (UINT8)Components;
    Record->Grid.b16BitsPerComponent = Type == RxGridComponent16;
    return Type == RxGridComponent16 || Type == RxGridComponent32;
}

static
BOOLEAN
RxIsInputKind(
    _In_ UINT Kind,
    _Out_ BOOLEAN* Writable)
{
    *Writable = Kind == D3D12DDI_NODE_IO_KIND_READ_WRITE_DISPATCH_NODE_INPUT_RECORD_0108 ||
                Kind == D3D12DDI_NODE_IO_KIND_READ_WRITE_GROUP_NODE_INPUT_RECORDS_0108 ||
                Kind == D3D12DDI_NODE_IO_KIND_READ_WRITE_THREAD_NODE_INPUT_RECORD_0108;

    return *Writable || Kind == D3D12DDI_NODE_IO_KIND_EMPTY_INPUT_0108 ||
           Kind == D3D12DDI_NODE_IO_KIND_DISPATCH_NODE_INPUT_RECORD_0108 ||
           Kind == D3D12DDI_NODE_IO_KIND_GROUP_NODE_INPUT_RECORDS_0108 ||
           Kind == D3D12DDI_NODE_IO_KIND_THREAD_NODE_INPUT_RECORD_0108;
}

static
BOOLEAN
RxIsOutputKind(
    _In_ UINT Kind,
    _Out_ BOOLEAN* Empty)
{
    *Empty = Kind == D3D12DDI_NODE_IO_KIND_EMPTY_OUTPUT_0108 || Kind == D3D12DDI_NODE_IO_KIND_EMPTY_OUTPUT_ARRAY_0108;

    return *Empty || Kind == D3D12DDI_NODE_IO_KIND_NODE_OUTPUT_0108 || Kind == D3D12DDI_NODE_IO_KIND_NODE_OUTPUT_ARRAY_0108;
}

/* One input or output; FALSE when the record is malformed or breaks the rules for its kind */
static
BOOLEAN
RxReadNodeRecord(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Index,
    _In_ BOOLEAN Output,
    _Out_ RxDxilNodeRecord* Record)
{
    RxRuntimeNodeRecord Raw;
    const UINT* Attributes;
    UINT Count;
    BOOLEAN Special;
    BOOLEAN Valid;

    ZeroMemory(Record, sizeof(*Record));
    if (!Data.Record(Data.NodeRecords, Index, &Raw, sizeof(Raw)) || !Data.List(Raw.Attributes, &Count, &Attributes))
        return FALSE;

    Record->Kind = Raw.KindAndFlags & RxRecordKindMask;
    Record->Flags = Raw.KindAndFlags & RxRecordFlagMask;

    /* Inputs track shared writes only when writable; empty outputs never do */
    if (Output)
        Valid = RxIsOutputKind(Record->Kind, &Special) && (Record->Flags == 0 || !Special);
    else
        Valid = RxIsInputKind(Record->Kind, &Special) && (Record->Flags == 0 || Special);

    for (UINT Each = 0; Each < Count; Each++)
    {
        RxRuntimeAttribute Attribute;

        if (!Data.Record(Data.NodeRecordAttributes, Attributes[Each], &Attribute, sizeof(Attribute)))
            return FALSE;

        switch (Attribute.Kind)
        {
            case RxRecordAttributeTarget:
                Valid = Valid && Output && RxReadNodeId(Arena, Data, Attribute.Value, &Record->Target, &Record->TargetIndex);
                break;

            case RxRecordAttributeMaxRecords:
                Record->HasMaxRecords = TRUE;
                Record->MaxRecords = Attribute.Value;
                Valid = Valid && (Output || Attribute.Value != 0);
                break;

            case RxRecordAttributeSharedWith:
                Record->HasSharedWith = TRUE;
                Record->SharedWith = Attribute.Value;
                Valid = Valid && Output;
                break;

            case RxRecordAttributeSize:
                Record->Size = Attribute.Value;
                break;

            case RxRecordAttributeGrid:
                Valid = RxReadRecordGrid(Attribute.Value, Record) && Valid;
                break;

            case RxRecordAttributeArraySize:
                Record->ArraySize = Attribute.Value;
                Valid = Valid && Output;
                break;

            case RxRecordAttributeSparse:
                Record->AllowSparse = Attribute.Value != 0;
                Valid = Valid && Output;
                break;

            case RxRecordAttributeAlignment:
                Record->Alignment = Attribute.Value;
                break;

            default:
                break;
        }
    }

    /* A limit of its own or one shared with another output, not both */
    return Valid && !(Record->HasMaxRecords && Record->HasSharedWith);
}

static
HRESULT
RxReadNode(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ const RxRuntimeFunction& Function,
    _In_z_ LPCWSTR Name,
    _Outptr_ const RxDxilNode** Read)
{
    RxRuntimeNode Raw;
    RxDxilNode* Node;
    const UINT* Attributes;
    const UINT* Inputs;
    const UINT* Outputs;
    UINT AttributeCount;
    UINT InputCount;
    UINT OutputCount;

    *Read = nullptr;
    if (Data.Functions.Stride < sizeof(Function) || !Data.Record(Data.Nodes, Function.Detail, &Raw, sizeof(Raw)))
        return E_INVALIDARG;
    if (!Data.List(Raw.Attributes, &AttributeCount, &Attributes) || !Data.List(Raw.Inputs, &InputCount, &Inputs) ||
        !Data.List(Raw.Outputs, &OutputCount, &Outputs))
    {
        return E_INVALIDARG;
    }

    Node = Arena.AllocateArray<RxDxilNode>(1);
    if (Node == nullptr)
        return E_OUTOFMEMORY;

    /* Launch types number the same as the driver's node property kinds */
    if (Raw.Launch < D3D12DDI_NODE_PROPERTIES_TYPE_BROADCASTING_LAUNCH_0108 ||
        Raw.Launch > D3D12DDI_NODE_PROPERTIES_TYPE_THREAD_LAUNCH_0108 || InputCount > 1 || OutputCount > RxMaxNodeOutputs)
    {
        return E_INVALIDARG;
    }

    Node->Launch = Raw.Launch;
    Node->GroupShared = Raw.GroupShared;
    Node->ProgramEntry = (Function.WaveAndFlags & RxNodeEntryFlag) != 0;
    Node->Name = Name;

    for (UINT Each = 0; Each < AttributeCount; Each++)
    {
        RxRuntimeAttribute Attribute;

        if (!Data.Record(Data.NodeAttributes, Attributes[Each], &Attribute, sizeof(Attribute)))
            return E_INVALIDARG;

        switch (Attribute.Kind)
        {
            case RxNodeAttributeId:
                if (!RxReadNodeId(Arena, Data, Attribute.Value, &Node->Name, &Node->Index))
                    return E_INVALIDARG;
                break;

            case RxNodeAttributeShareInput:
                if (!RxReadNodeId(Arena, Data, Attribute.Value, &Node->ShareInputOf, &Node->ShareInputIndex))
                    return E_INVALIDARG;
                break;

            case RxNodeAttributeDispatchGrid:
                Node->HasDispatchGrid = RxReadGridValues(Data, Attribute.Value, Node->DispatchGrid);
                break;

            case RxNodeAttributeRecursion:
                Node->MaxRecursionDepth = Attribute.Value;
                break;

            case RxNodeAttributeLocalRootIndex:
                Node->HasLocalRootIndex = TRUE;
                Node->LocalRootIndex = Attribute.Value;
                break;

            case RxNodeAttributeMaxDispatchGrid:
                Node->HasMaxDispatchGrid = RxReadGridValues(Data, Attribute.Value, Node->MaxDispatchGrid);
                break;

            default:
                break;
        }
    }

    if (InputCount != 0)
    {
        if (!RxReadNodeRecord(Arena, Data, Inputs[0], FALSE, &Node->Input))
            return E_INVALIDARG;
        Node->HasInput = TRUE;
    }

    Node->Outputs = Arena.AllocateArray<RxDxilNodeRecord>(OutputCount);
    if (Node->Outputs == nullptr)
        return E_OUTOFMEMORY;
    Node->OutputCount = OutputCount;
    for (UINT Each = 0; Each < OutputCount; Each++)
    {
        if (!RxReadNodeRecord(Arena, Data, Outputs[Each], TRUE, &Node->Outputs[Each]))
            return E_INVALIDARG;
    }

    *Read = Node;
    return S_OK;
}

static
HRESULT
RxReadFunction(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Index,
    _Out_ RxDxilFunction* Function)
{
    RxRuntimeFunction Record;
    const UINT* Indices;
    UINT Count;
    RxDxilBinding* Bindings;

    if (!Data.Record(Data.Functions, Index, &Record, sizeof(Record)))
        return E_INVALIDARG;

    Function->Name = RxWidenOffset(Arena, Data, Record.UnmangledName);
    Function->Mangled = RxWidenOffset(Arena, Data, Record.Name);
    Function->Kind = Record.ShaderKind;
    if (Function->Name == nullptr || Function->Mangled == nullptr)
        return E_INVALIDARG;

    if (!Data.List(Record.Resources, &Count, &Indices))
        return E_INVALIDARG;
    Bindings = Arena.AllocateArray<RxDxilBinding>(Count);
    if (Bindings == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Each = 0; Each < Count; Each++)
    {
        RxRuntimeResource Resource;

        if (!Data.Record(Data.Resources, Indices[Each], &Resource, sizeof(Resource)))
            return E_INVALIDARG;
        Bindings[Each].Class = Resource.Class;
        Bindings[Each].Space = Resource.Space;
        Bindings[Each].Lower = Resource.Lower;
        Bindings[Each].Upper = Resource.Upper;
    }
    Function->BindingCount = Count;
    Function->Bindings = Bindings;

    if (!RxWidenList(Arena, Data, Record.Dependencies, &Function->DependencyCount, &Function->Dependencies))
        return E_INVALIDARG;

    if (Function->Kind == RxDxilKindNode)
        return RxReadNode(Arena, Data, Record, Function->Name, &Function->Node);
    return S_OK;
}

static
HRESULT
RxReadSubobject(
    _In_ RxArena& Arena,
    _In_ const RxRuntimeData& Data,
    _In_ UINT Index,
    _Out_ RxDxilSubobject* Subobject)
{
    const RxPartView& Raw = Data.Parts[RxPartRawBytes];
    RxRuntimeSubobject Record;

    if (!Data.Record(Data.Subobjects, Index, &Record, sizeof(Record)))
        return E_INVALIDARG;

    Subobject->Kind = Record.Kind;
    Subobject->Name = RxWidenOffset(Arena, Data, Record.Name);
    if (Subobject->Name == nullptr)
        return E_INVALIDARG;

    switch (Record.Kind)
    {
        case RxDxilSubobjectConfig:
        case RxDxilSubobjectPipelineConfig:
            Subobject->Values[0] = Record.Data[0];
            break;

        case RxDxilSubobjectShaderConfig:
        case RxDxilSubobjectPipelineConfig1:
            Subobject->Values[0] = Record.Data[0];
            Subobject->Values[1] = Record.Data[1];
            break;

        case RxDxilSubobjectGlobalRoot:
        case RxDxilSubobjectLocalRoot:
            if (Raw.Data == nullptr || Record.Data[0] > Raw.Size || Record.Data[1] > Raw.Size - Record.Data[0])
                return E_INVALIDARG;
            Subobject->Bytes = Raw.Data + Record.Data[0];
            Subobject->ByteCount = Record.Data[1];
            break;

        case RxDxilSubobjectAssociation:
            Subobject->Strings[0] = RxWidenOffset(Arena, Data, Record.Data[0]);
            if (Subobject->Strings[0] == nullptr ||
                !RxWidenList(Arena, Data, Record.Data[1], &Subobject->ExportCount, &Subobject->Exports))
            {
                return E_INVALIDARG;
            }
            break;

        case RxDxilSubobjectHitGroup:
            Subobject->Values[0] = Record.Data[0];
            for (UINT Each = 0; Each < 3; Each++)
            {
                /* Empty names are imports the group leaves out */
                const char* Text = Data.String(Record.Data[1 + Each]);

                if (Text == nullptr)
                    return E_INVALIDARG;
                if (Text[0] != '\0')
                    Subobject->Strings[Each] = Arena.Widen(Text);
            }
            break;

        default:
            break;
    }

    return S_OK;
}

HRESULT
RxDxilLibrary::Load(
    _In_ RxArena& Arena,
    _In_reads_bytes_(Size) const VOID* Container,
    _In_ SIZE_T Size)
{
    RxRuntimeData Data;
    const BYTE* Part;
    BYTE* Owned;
    UINT PartSize;
    UINT Declared;
    HRESULT hr;

    if (Container == nullptr || Size < 32 || Size > UINT_MAX)
        return E_INVALIDARG;

    CopyMemory(&Declared, static_cast<const BYTE*>(Container) + 24, sizeof(Declared));
    if (Declared > Size)
        return E_INVALIDARG;

    /* The driver keeps reading the program after creation returns */
    Owned = static_cast<BYTE*>(Arena.Allocate(Declared));
    if (Owned == nullptr)
        return E_OUTOFMEMORY;
    CopyMemory(Owned, Container, Declared);
    Copy = Owned;
    CopySize = Declared;

    if (!RxFindContainerPart(Copy, Declared, RxProgramPart, &Part, &PartSize) || PartSize < 24)
        return E_INVALIDARG;
    Program = reinterpret_cast<const UINT*>(Part);
    Version = Program[0];

    if (!RxFindContainerPart(Copy, Declared, RxRuntimeDataPart, &Part, &PartSize) || !Data.Read(Part, PartSize))
        return E_INVALIDARG;

    FunctionCount = Data.Functions.Count;
    Functions = Arena.AllocateArray<RxDxilFunction>(FunctionCount);
    SubobjectCount = Data.Subobjects.Count;
    Subobjects = Arena.AllocateArray<RxDxilSubobject>(SubobjectCount);
    if (Functions == nullptr || Subobjects == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Index = 0; Index < FunctionCount; Index++)
    {
        hr = RxReadFunction(Arena, Data, Index, &Functions[Index]);
        if (FAILED(hr))
            return hr;
    }

    for (UINT Index = 0; Index < SubobjectCount; Index++)
    {
        hr = RxReadSubobject(Arena, Data, Index, &Subobjects[Index]);
        if (FAILED(hr))
            return hr;
    }

    return S_OK;
}

/* Container digest */

/* The digest sits after the four byte magic and covers everything that follows it */
constexpr UINT RxDigestOffset = 4;
constexpr UINT RxDigestSize = 16;
constexpr UINT RxDigestedFrom = RxDigestOffset + RxDigestSize;

/* MD5 rounds over one 64 byte block */
static
VOID
RxDigestBlock(
    _Inout_updates_(4) UINT* State,
    _In_reads_(16) const UINT* Words)
{
    static const UINT Sines[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
    };
    static const BYTE Shifts[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };
    UINT A = State[0];
    UINT B = State[1];
    UINT C = State[2];
    UINT D = State[3];

    for (UINT Step = 0; Step < 64; Step++)
    {
        UINT Round = Step / 16;
        UINT Mixed;
        UINT Word;
        UINT Sum;

        switch (Round)
        {
            case 0:
                Mixed = (B & C) | (~B & D);
                Word = Step;
                break;
            case 1:
                Mixed = (D & B) | (~D & C);
                Word = (5 * Step + 1) % 16;
                break;
            case 2:
                Mixed = B ^ C ^ D;
                Word = (3 * Step + 5) % 16;
                break;
            default:
                Mixed = C ^ (B | ~D);
                Word = (7 * Step) % 16;
                break;
        }

        Sum = A + Mixed + Sines[Step] + Words[Word];
        A = D;
        D = C;
        C = B;
        B += _rotl(Sum, Shifts[Round * 4 + Step % 4]);
    }

    State[0] += A;
    State[1] += B;
    State[2] += C;
    State[3] += D;
}

/* Signed containers hash with the byte count leading the last block and a tagged count ending it */
static
VOID
RxDigest(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ UINT Size,
    _Out_writes_(4) UINT* State)
{
    UINT Words[16];
    UINT Whole = Size / 64;
    UINT Tail = Size % 64;
    BYTE* Bytes = reinterpret_cast<BYTE*>(Words);

    State[0] = 0x67452301;
    State[1] = 0xefcdab89;
    State[2] = 0x98badcfe;
    State[3] = 0x10325476;

    for (UINT Block = 0; Block < Whole; Block++)
    {
        CopyMemory(Words, Data + Block * 64, sizeof(Words));
        RxDigestBlock(State, Words);
    }

    ZeroMemory(Words, sizeof(Words));
    if (Tail < 56)
    {
        Words[0] = Size << 3;
        CopyMemory(Bytes + 4, Data + Whole * 64, Tail);
        Bytes[4 + Tail] = 0x80;
    }
    else
    {
        CopyMemory(Bytes, Data + Whole * 64, Tail);
        Bytes[Tail] = 0x80;
        RxDigestBlock(State, Words);
        ZeroMemory(Words, sizeof(Words));
        Words[0] = Size << 3;
    }
    Words[15] = (Size << 1) | 1;
    RxDigestBlock(State, Words);
}

BOOLEAN
RxDxilLibrary::IsSigned() const
{
    static const BYTE Unsigned[RxDigestSize] = {};
    UINT State[4];

    if (Copy == nullptr || CopySize < RxDigestedFrom)
        return FALSE;
    if (memcmp(Copy + RxDigestOffset, Unsigned, RxDigestSize) == 0)
        return FALSE;

    RxDigest(Copy + RxDigestedFrom, CopySize - RxDigestedFrom, State);
    return memcmp(Copy + RxDigestOffset, State, RxDigestSize) == 0;
}

_Ret_maybenull_
LPWSTR
RxRenameMangled(
    _In_ RxArena& Arena,
    _In_z_ LPCWSTR Mangled,
    _In_z_ LPCWSTR OldName,
    _In_z_ LPCWSTR NewName)
{
    SIZE_T OldLength = wcslen(OldName);
    SIZE_T NewLength = wcslen(NewName);
    SIZE_T Prefix;
    SIZE_T Tail;
    LPWSTR Out;

    /* A decorated name is [0x01]?Identifier@...; anything else is the identifier alone */
    Prefix = Mangled[0] == L'\x01' ? 2 : 1;
    if (Mangled[Prefix - 1] != L'?' || wcsncmp(Mangled + Prefix, OldName, OldLength) != 0 ||
        Mangled[Prefix + OldLength] != L'@')
    {
        return Arena.CopyString(NewName);
    }

    Tail = wcslen(Mangled + Prefix + OldLength);
    Out = Arena.AllocateArray<WCHAR>(Prefix + NewLength + Tail + 1);
    if (Out == nullptr)
        return nullptr;

    CopyMemory(Out, Mangled, Prefix * sizeof(*Out));
    CopyMemory(Out + Prefix, NewName, NewLength * sizeof(*Out));
    CopyMemory(Out + Prefix + NewLength, Mangled + Prefix + OldLength, Tail * sizeof(*Out));
    return Out;
}
