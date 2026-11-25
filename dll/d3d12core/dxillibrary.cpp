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
};

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

    RxPartView Parts[RxPartSubobjects + 1] = {};
    RxTableView Functions = {};
    RxTableView Resources = {};
    RxTableView Subobjects = {};

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

    return Table(RxPartFunctions, &Functions) && Table(RxPartResources, &Resources) && Table(RxPartSubobjects, &Subobjects);
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
    const BYTE* Copy;
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
