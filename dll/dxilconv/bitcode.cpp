/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     LLVM 3.7 bitstream writer and value numbering for DXIL modules
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"
#include "dxil.hpp"

#include <algorithm>
#include <unordered_map>

/* Block ids */
#define BC_BLOCK_INFO           0
#define BC_BLOCK_MODULE         8
#define BC_BLOCK_ATTRIBUTES     9
#define BC_BLOCK_ATTR_GROUPS    10
#define BC_BLOCK_CONSTANTS      11
#define BC_BLOCK_FUNCTION       12
#define BC_BLOCK_SYMBOLS        14
#define BC_BLOCK_METADATA       15
#define BC_BLOCK_TYPES          17

typedef enum _BC_OP_KIND
{
    BcLiteral,
    BcFixed,
    BcVbr,
    BcArray,
    BcChar6,
} BC_OP_KIND;

typedef struct _BC_OP
{
    BC_OP_KIND Kind;
    ULONGLONG Value;
} BC_OP;

typedef std::vector<BC_OP> BC_ABBREV;

/* Fixed abbreviation ids handed out by the block info block */
#define BC_SYMBOL_ENTRY8        4
#define BC_SYMBOL_ENTRY7        5
#define BC_SYMBOL_ENTRY6        6
#define BC_SYMBOL_BLOCK6        7
#define BC_CONSTANT_SETTYPE     4
#define BC_CONSTANT_INTEGER     5
#define BC_FUNCTION_RETVOID     8

static
ULONG
BcBitsFor(
    _In_ ULONG Count)
{
    ULONG Bits = 0;

    while ((1ULL << Bits) < Count)
        Bits += 1;

    return Bits;
}

static
ULONG
BcChar6Code(
    _In_ CHAR Letter)
{
    if (Letter >= 'a' && Letter <= 'z')
        return Letter - 'a';
    if (Letter >= 'A' && Letter <= 'Z')
        return Letter - 'A' + 26;
    if (Letter >= '0' && Letter <= '9')
        return Letter - '0' + 52;
    if (Letter == '.')
        return 62;
    return 63;
}

static
BOOLEAN
BcIsChar6(
    _In_ CHAR Letter)
{
    return (Letter >= 'a' && Letter <= 'z') ||
           (Letter >= 'A' && Letter <= 'Z') ||
           (Letter >= '0' && Letter <= '9') ||
           Letter == '.' ||
           Letter == '_';
}

/* Bit level output with nested blocks and abbreviations */
class CBcStream
{
public:
    explicit CBcStream(
        _Out_ std::vector<BYTE>& Out)
        : m_Out(Out),
          m_Pending(0),
          m_PendingBits(0),
          m_Width(2),
          m_InfoTarget(0)
    {
    }

    VOID
    Emit(
        _In_ ULONGLONG Value,
        _In_ ULONG Bits)
    {
        for (ULONG Bit = 0; Bit < Bits; Bit++)
        {
            m_Pending |= ((Value >> Bit) & 1ULL) << m_PendingBits;
            m_PendingBits += 1;
            if (m_PendingBits == 32)
                FlushWord();
        }
    }

    VOID
    EmitVbr(
        _In_ ULONGLONG Value,
        _In_ ULONG Bits)
    {
        ULONGLONG Limit = 1ULL << (Bits - 1);

        while (Value >= Limit)
        {
            Emit((Value & (Limit - 1)) | Limit, Bits);
            Value >>= Bits - 1;
        }

        Emit(Value, Bits);
    }

    VOID
    AlignWord()
    {
        if (m_PendingBits != 0)
            FlushWord();
    }

    VOID
    EnterBlock(
        _In_ ULONG Id,
        _In_ ULONG Width)
    {
        SCOPE Scope;

        Emit(1, m_Width);
        EmitVbr(Id, 8);
        EmitVbr(Width, 4);
        AlignWord();

        Scope.OuterWidth = m_Width;
        Scope.SizeWord = m_Out.size() / 4;
        Scope.Abbrevs = m_Info[Id];
        m_Scopes.push_back(Scope);
        Emit(0, 32);
        m_Width = Width;
    }

    VOID
    ExitBlock()
    {
        SCOPE& Scope = m_Scopes.back();
        ULONG Words;

        Emit(0, m_Width);
        AlignWord();
        Words = static_cast<ULONG>(m_Out.size() / 4 - Scope.SizeWord - 1);
        memcpy(&m_Out[Scope.SizeWord * 4], &Words, sizeof(Words));
        m_Width = Scope.OuterWidth;
        m_Scopes.pop_back();
    }

    /** Defines an abbreviation in the current block and returns its id */
    ULONG
    DefineAbbrev(
        _In_ const BC_ABBREV& Abbrev)
    {
        WriteAbbrev(Abbrev);
        m_Scopes.back().Abbrevs.push_back(Abbrev);
        return static_cast<ULONG>(m_Scopes.back().Abbrevs.size() + 3);
    }

    /** Selects the block the following block info abbreviations belong to */
    VOID
    SetInfoTarget(
        _In_ ULONG Id)
    {
        std::vector<ULONGLONG> Values(1, Id);

        Record(1, Values, 0);
        m_InfoTarget = Id;
    }

    VOID
    DefineInfoAbbrev(
        _In_ const BC_ABBREV& Abbrev)
    {
        WriteAbbrev(Abbrev);
        m_Info[m_InfoTarget].push_back(Abbrev);
    }

    VOID
    Record(
        _In_ ULONG Code,
        _In_ const std::vector<ULONGLONG>& Values,
        _In_ ULONG Abbrev)
    {
        if (Abbrev == 0)
        {
            Emit(3, m_Width);
            EmitVbr(Code, 6);
            EmitVbr(Values.size(), 6);
            for (ULONGLONG Value : Values)
                EmitVbr(Value, 6);
            return;
        }

        const BC_ABBREV& Shape = m_Scopes.back().Abbrevs[Abbrev - 4];
        std::vector<ULONGLONG> All;
        SIZE_T Next = 0;

        All.push_back(Code);
        All.insert(All.end(), Values.begin(), Values.end());
        Emit(Abbrev, m_Width);

        for (SIZE_T Op = 0; Op < Shape.size(); Op++)
        {
            if (Shape[Op].Kind == BcArray)
            {
                const BC_OP& Element = Shape[Op + 1];

                EmitVbr(All.size() - Next, 6);
                while (Next < All.size())
                {
                    EmitScalar(Element, All[Next]);
                    Next += 1;
                }
                break;
            }

            if (Shape[Op].Kind != BcLiteral)
                EmitScalar(Shape[Op], All[Next]);
            Next += 1;
        }
    }

private:
    struct SCOPE
    {
        ULONG OuterWidth;
        SIZE_T SizeWord;
        std::vector<BC_ABBREV> Abbrevs;
    };

    VOID
    FlushWord()
    {
        ULONG Word = static_cast<ULONG>(m_Pending);
        const BYTE* Bytes = reinterpret_cast<const BYTE*>(&Word);

        m_Out.insert(m_Out.end(), Bytes, Bytes + 4);
        m_Pending = 0;
        m_PendingBits = 0;
    }

    VOID
    EmitScalar(
        _In_ const BC_OP& Op,
        _In_ ULONGLONG Value)
    {
        if (Op.Kind == BcFixed)
            Emit(Value, static_cast<ULONG>(Op.Value));
        else if (Op.Kind == BcVbr)
            EmitVbr(Value, static_cast<ULONG>(Op.Value));
        else if (Op.Kind == BcChar6)
            Emit(BcChar6Code(static_cast<CHAR>(Value)), 6);
    }

    VOID
    WriteAbbrev(
        _In_ const BC_ABBREV& Abbrev)
    {
        Emit(2, m_Width);
        EmitVbr(Abbrev.size(), 5);
        for (const BC_OP& Op : Abbrev)
        {
            Emit(Op.Kind == BcLiteral ? 1 : 0, 1);
            if (Op.Kind == BcLiteral)
            {
                EmitVbr(Op.Value, 8);
                continue;
            }

            if (Op.Kind == BcFixed)
            {
                Emit(1, 3);
                EmitVbr(Op.Value, 5);
            }
            else if (Op.Kind == BcVbr)
            {
                Emit(2, 3);
                EmitVbr(Op.Value, 5);
            }
            else if (Op.Kind == BcArray)
            {
                Emit(3, 3);
            }
            else
            {
                Emit(4, 3);
            }
        }
    }

    std::vector<BYTE>& m_Out;
    ULONGLONG m_Pending;
    ULONG m_PendingBits;
    ULONG m_Width;
    ULONG m_InfoTarget;
    std::vector<SCOPE> m_Scopes;
    std::map<ULONG, std::vector<BC_ABBREV>> m_Info;
};

static
BC_OP
BcLit(
    _In_ ULONGLONG Value)
{
    return BC_OP{ BcLiteral, Value };
}

static
BC_OP
BcFix(
    _In_ ULONGLONG Bits)
{
    return BC_OP{ BcFixed, Bits };
}

static
BC_OP
BcVar(
    _In_ ULONGLONG Bits)
{
    return BC_OP{ BcVbr, Bits };
}

static const BC_OP BcArr = { BcArray, 0 };
static const BC_OP BcCh6 = { BcChar6, 0 };

/* A constant or global with the number of times the module refers to it */
typedef struct _BC_SLOT
{
    const DXIL_VALUE* Value;
    ULONG Uses;
} BC_SLOT;

/**
 * Numbers types, values, metadata and attributes in the order the bitcode needs,
 * then writes the module.
 */
class CBcModuleWriter
{
public:
    CBcModuleWriter(
        _In_ const CDxilModule& Module,
        _Out_ std::vector<BYTE>& Out)
        : m_Module(Module),
          m_Stream(Out),
          m_TypeBits(0)
    {
    }

    VOID
    Write()
    {
        NumberModule();

        m_Stream.Emit('B', 8);
        m_Stream.Emit('C', 8);
        m_Stream.Emit(0x0, 4);
        m_Stream.Emit(0xC, 4);
        m_Stream.Emit(0xE, 4);
        m_Stream.Emit(0xD, 4);

        m_Stream.EnterBlock(BC_BLOCK_MODULE, 3);
        m_Stream.Record(1, std::vector<ULONGLONG>(1, 1), 0);
        WriteBlockInfo();
        WriteAttributes();
        WriteTypes();
        WriteGlobals();
        WriteConstants(m_FirstConstant, static_cast<ULONG>(m_Slots.size()), TRUE);
        WriteMetadata();
        WriteMetadataKinds();
        WriteModuleSymbols();

        for (const DXIL_FUNCTION* Function : m_Module.Functions)
        {
            if (!Function->Blocks.empty())
                WriteFunction(Function);
        }

        m_Stream.ExitBlock();
    }

private:
    /* Types are numbered after the types they are built from */
    VOID
    NumberType(
        _In_ const DXIL_TYPE* Type)
    {
        if (m_TypeIds.count(Type) != 0)
            return;

        for (const DXIL_TYPE* Child : Type->Children)
            NumberType(Child);

        if (m_TypeIds.count(Type) != 0)
            return;

        m_TypeIds[Type] = static_cast<ULONG>(m_Types.size());
        m_Types.push_back(Type);
    }

    VOID
    NumberValue(
        _In_ const DXIL_VALUE* Value)
    {
        auto Found = m_ValueIds.find(Value);

        if (Found != m_ValueIds.end())
        {
            m_Slots[Found->second].Uses += 1;
            return;
        }

        NumberType(Value->Type);
        m_ValueIds[Value] = static_cast<ULONG>(m_Slots.size());
        m_Slots.push_back(BC_SLOT{ Value, 1 });
    }

    VOID
    NumberAttributes(
        _In_ const DXIL_ATTRIBUTES& Attributes)
    {
        if (Attributes.empty())
            return;

        for (const DXIL_ATTRIBUTES& Known : m_AttributeLists)
        {
            if (Known == Attributes)
                return;
        }

        m_AttributeLists.push_back(Attributes);
    }

    ULONG
    AttributeId(
        _In_ const DXIL_ATTRIBUTES& Attributes) const
    {
        if (Attributes.empty())
            return 0;

        for (SIZE_T Index = 0; Index < m_AttributeLists.size(); Index++)
        {
            if (m_AttributeLists[Index] == Attributes)
                return static_cast<ULONG>(Index + 1);
        }

        return 0;
    }

    /* Leaves get ids as they are met, nodes once all their operands have ids */
    VOID
    NumberMetadata(
        _In_ const DXIL_MD* Root)
    {
        std::vector<std::pair<const DXIL_MD*, SIZE_T>> Pending;

        if (!ClaimMetadata(Root))
            return;

        Pending.push_back(std::make_pair(Root, 0));
        while (!Pending.empty())
        {
            const DXIL_MD* Node = Pending.back().first;
            SIZE_T& Cursor = Pending.back().second;
            const DXIL_MD* Descend = nullptr;

            while (Cursor < Node->Operands.size())
            {
                const DXIL_MD* Operand = Node->Operands[Cursor];

                Cursor += 1;
                if (ClaimMetadata(Operand))
                {
                    Descend = Operand;
                    break;
                }
            }

            if (Descend != nullptr)
            {
                Pending.push_back(std::make_pair(Descend, 0));
                continue;
            }

            Pending.pop_back();
            m_MdIds[Node] = static_cast<ULONG>(m_Mds.size());
            m_Mds.push_back(Node);
        }
    }

    /** Returns TRUE for a tuple that still needs its operands walked */
    BOOLEAN
    ClaimMetadata(
        _In_opt_ const DXIL_MD* Node)
    {
        if (Node == nullptr || m_MdSeen.count(Node) != 0)
            return FALSE;

        m_MdSeen[Node] = TRUE;
        if (Node->Kind == DxilMdTuple)
            return TRUE;

        m_MdIds[Node] = static_cast<ULONG>(m_Mds.size());
        m_Mds.push_back(Node);
        if (Node->Kind == DxilMdValue)
            NumberValue(Node->Value);

        return FALSE;
    }

    BOOLEAN
    IsIntegerSlot(
        _In_ const BC_SLOT& Slot) const
    {
        return Slot.Value->Type->Kind == DxilTypeInteger;
    }

    /**
     * Groups constants by type, most used first, then moves integers to the front
     * with the same unstable swap order the reference produces.
     */
    VOID
    OrderConstants(
        _In_ ULONG First,
        _In_ ULONG Last)
    {
        if (Last - First < 2)
            return;

        std::stable_sort(m_Slots.begin() + First,
                         m_Slots.begin() + Last,
                         [this](const BC_SLOT& Left, const BC_SLOT& Right)
                         {
                             ULONG LeftType = m_TypeIds.at(Left.Value->Type);
                             ULONG RightType = m_TypeIds.at(Right.Value->Type);

                             if (LeftType != RightType)
                                 return LeftType < RightType;
                             return Left.Uses > Right.Uses;
                         });

        ULONG Front = First;
        ULONG Back = Last;

        for (;;)
        {
            while (Front != Back && IsIntegerSlot(m_Slots[Front]))
                Front += 1;
            if (Front == Back)
                break;

            do
            {
                Back -= 1;
            } while (Front != Back && !IsIntegerSlot(m_Slots[Back]));
            if (Front == Back)
                break;

            std::swap(m_Slots[Front], m_Slots[Back]);
            Front += 1;
        }

        for (ULONG Index = First; Index < Last; Index++)
            m_ValueIds[m_Slots[Index].Value] = Index;
    }

    BOOLEAN
    IsConstant(
        _In_ const DXIL_VALUE* Value) const
    {
        return Value->Kind == DxilValueInteger ||
               Value->Kind == DxilValueFloat ||
               Value->Kind == DxilValueNull ||
               Value->Kind == DxilValueUndef;
    }

    VOID
    NumberModule()
    {
        for (const DXIL_FUNCTION* Function : m_Module.Functions)
        {
            NumberValue(Function);
            NumberAttributes(Function->Attributes);
        }

        m_FirstConstant = static_cast<ULONG>(m_Slots.size());
        NumberType(MetadataTypeOf());

        for (const DXIL_NAMED_MD* Named : m_Module.Named)
        {
            for (const DXIL_MD* Operand : Named->Operands)
                NumberMetadata(Operand);
        }

        for (const DXIL_FUNCTION* Function : m_Module.Functions)
        {
            for (const DXIL_VALUE* Argument : Function->Arguments)
                NumberType(Argument->Type);

            for (const DXIL_BLOCK* Block : Function->Blocks)
            {
                for (const DXIL_INSTRUCTION* Instruction : Block->Instructions)
                {
                    for (const DXIL_VALUE* Operand : Instruction->Operands)
                        NumberType(Operand->Type);

                    NumberType(Instruction->Type);
                }
            }
        }

        OrderConstants(m_FirstConstant, static_cast<ULONG>(m_Slots.size()));
        m_TypeBits = BcBitsFor(static_cast<ULONG>(m_Types.size() + 1));
    }

    const DXIL_TYPE*
    MetadataTypeOf() const
    {
        return const_cast<CDxilModule&>(m_Module).MetadataType();
    }

    VOID
    WriteBlockInfo()
    {
        m_Stream.EnterBlock(BC_BLOCK_INFO, 2);

        m_Stream.SetInfoTarget(BC_BLOCK_SYMBOLS);
        m_Stream.DefineInfoAbbrev({ BcFix(3), BcVar(8), BcArr, BcFix(8) });
        m_Stream.DefineInfoAbbrev({ BcLit(1), BcVar(8), BcArr, BcFix(7) });
        m_Stream.DefineInfoAbbrev({ BcLit(1), BcVar(8), BcArr, BcCh6 });
        m_Stream.DefineInfoAbbrev({ BcLit(2), BcVar(8), BcArr, BcCh6 });

        m_Stream.SetInfoTarget(BC_BLOCK_CONSTANTS);
        m_Stream.DefineInfoAbbrev({ BcLit(1), BcFix(m_TypeBits) });
        m_Stream.DefineInfoAbbrev({ BcLit(4), BcVar(8) });
        m_Stream.DefineInfoAbbrev({ BcLit(11), BcFix(4), BcFix(m_TypeBits), BcVar(8) });
        m_Stream.DefineInfoAbbrev({ BcLit(2) });

        m_Stream.SetInfoTarget(BC_BLOCK_FUNCTION);
        m_Stream.DefineInfoAbbrev({ BcLit(20), BcVar(6), BcFix(m_TypeBits), BcVar(4), BcFix(1) });
        m_Stream.DefineInfoAbbrev({ BcLit(2), BcVar(6), BcVar(6), BcFix(4) });
        m_Stream.DefineInfoAbbrev({ BcLit(2), BcVar(6), BcVar(6), BcFix(4), BcFix(7) });
        m_Stream.DefineInfoAbbrev({ BcLit(3), BcVar(6), BcFix(m_TypeBits), BcFix(4) });
        m_Stream.DefineInfoAbbrev({ BcLit(10) });
        m_Stream.DefineInfoAbbrev({ BcLit(10), BcVar(6) });
        m_Stream.DefineInfoAbbrev({ BcLit(15) });
        m_Stream.DefineInfoAbbrev({ BcLit(43), BcFix(1), BcFix(m_TypeBits), BcArr, BcVar(6) });

        m_Stream.ExitBlock();
    }

    /* Every list here holds one function level group, so group and list ids coincide */
    VOID
    WriteAttributes()
    {
        if (m_AttributeLists.empty())
            return;

        m_Stream.EnterBlock(BC_BLOCK_ATTR_GROUPS, 3);
        for (SIZE_T Index = 0; Index < m_AttributeLists.size(); Index++)
        {
            std::vector<ULONGLONG> Values;

            Values.push_back(Index + 1);
            Values.push_back(0xFFFFFFFF);
            for (ULONG Kind : m_AttributeLists[Index])
            {
                Values.push_back(0);
                Values.push_back(Kind);
            }
            m_Stream.Record(3, Values, 0);
        }
        m_Stream.ExitBlock();

        m_Stream.EnterBlock(BC_BLOCK_ATTRIBUTES, 3);
        for (SIZE_T Index = 0; Index < m_AttributeLists.size(); Index++)
            m_Stream.Record(2, std::vector<ULONGLONG>(1, Index + 1), 0);
        m_Stream.ExitBlock();
    }

    VOID
    WriteTypes()
    {
        ULONG Pointer;
        ULONG Function;

        m_Stream.EnterBlock(BC_BLOCK_TYPES, 4);
        Pointer = m_Stream.DefineAbbrev({ BcLit(8), BcFix(m_TypeBits), BcLit(0) });
        Function = m_Stream.DefineAbbrev({ BcLit(21), BcFix(1), BcArr, BcFix(m_TypeBits) });
        m_Stream.DefineAbbrev({ BcLit(18), BcFix(1), BcArr, BcFix(m_TypeBits) });
        m_Stream.DefineAbbrev({ BcLit(19), BcArr, BcCh6 });
        m_Stream.DefineAbbrev({ BcLit(20), BcFix(1), BcArr, BcFix(m_TypeBits) });
        m_Stream.DefineAbbrev({ BcLit(11), BcVar(8), BcFix(m_TypeBits) });

        m_Stream.Record(1, std::vector<ULONGLONG>(1, m_Types.size()), 0);
        for (const DXIL_TYPE* Type : m_Types)
        {
            std::vector<ULONGLONG> Values;

            switch (Type->Kind)
            {
            case DxilTypeVoid:
                m_Stream.Record(2, Values, 0);
                break;
            case DxilTypeHalf:
                m_Stream.Record(10, Values, 0);
                break;
            case DxilTypeFloat:
                m_Stream.Record(3, Values, 0);
                break;
            case DxilTypeDouble:
                m_Stream.Record(4, Values, 0);
                break;
            case DxilTypeLabel:
                m_Stream.Record(5, Values, 0);
                break;
            case DxilTypeMetadata:
                m_Stream.Record(16, Values, 0);
                break;
            case DxilTypeInteger:
                Values.push_back(Type->Width);
                m_Stream.Record(7, Values, 0);
                break;
            case DxilTypePointer:
                Values.push_back(m_TypeIds.at(Type->Children[0]));
                if (Type->AddressSpace != 0)
                {
                    Values.push_back(Type->AddressSpace);
                    m_Stream.Record(8, Values, 0);
                }
                else
                {
                    Values.push_back(0);
                    m_Stream.Record(8, Values, Pointer);
                }
                break;
            case DxilTypeFunction:
                Values.push_back(0);
                for (const DXIL_TYPE* Child : Type->Children)
                    Values.push_back(m_TypeIds.at(Child));
                m_Stream.Record(21, Values, Function);
                break;
            default:
                DxcTrace(L"type kind %u has no bitcode form yet", Type->Kind);
                break;
            }
        }
        m_Stream.ExitBlock();
    }

    VOID
    WriteGlobals()
    {
        for (const DXIL_FUNCTION* Function : m_Module.Functions)
        {
            std::vector<ULONGLONG> Values(15, 0);

            Values[0] = m_TypeIds.at(Function->Signature);
            Values[2] = Function->Blocks.empty() ? 1 : 0;
            Values[4] = AttributeId(Function->Attributes);
            m_Stream.Record(8, Values, 0);
        }
    }

    static
    ULONGLONG
    SignedVbrValue(
        _In_ const DXIL_VALUE* Value)
    {
        ULONG Width = Value->Type->Width;
        LONGLONG Signed = static_cast<LONGLONG>(Value->Bits);

        if (Width < 64 && (Value->Bits & (1ULL << (Width - 1))) != 0)
            Signed = static_cast<LONGLONG>(Value->Bits | ~((1ULL << Width) - 1));

        if (Signed >= 0)
            return static_cast<ULONGLONG>(Signed) << 1;

        return (static_cast<ULONGLONG>(0) - static_cast<ULONGLONG>(Signed)) << 1 | 1;
    }

    VOID
    WriteConstants(
        _In_ ULONG First,
        _In_ ULONG Last,
        _In_ BOOLEAN ModuleLevel)
    {
        const DXIL_TYPE* LastType = nullptr;

        if (First == Last)
            return;

        m_Stream.EnterBlock(BC_BLOCK_CONSTANTS, 4);
        if (ModuleLevel)
        {
            ULONG ValueBits = BcBitsFor(Last + 1);

            m_Stream.DefineAbbrev({ BcLit(7), BcArr, BcFix(ValueBits) });
            m_Stream.DefineAbbrev({ BcLit(8), BcArr, BcFix(8) });
            m_Stream.DefineAbbrev({ BcLit(9), BcArr, BcFix(7) });
            m_Stream.DefineAbbrev({ BcLit(9), BcArr, BcCh6 });
        }

        for (ULONG Index = First; Index < Last; Index++)
        {
            const DXIL_VALUE* Value = m_Slots[Index].Value;
            std::vector<ULONGLONG> Values;

            if (Value->Type != LastType)
            {
                LastType = Value->Type;
                m_Stream.Record(1, std::vector<ULONGLONG>(1, m_TypeIds.at(LastType)), BC_CONSTANT_SETTYPE);
            }

            if (Value->Kind == DxilValueUndef)
            {
                m_Stream.Record(3, Values, 0);
            }
            else if (Value->Kind == DxilValueNull || Value->Bits == 0)
            {
                m_Stream.Record(2, Values, 0);
            }
            else if (Value->Kind == DxilValueInteger)
            {
                Values.push_back(SignedVbrValue(Value));
                m_Stream.Record(4, Values, BC_CONSTANT_INTEGER);
            }
            else
            {
                Values.push_back(Value->Bits);
                m_Stream.Record(6, Values, 0);
            }
        }
        m_Stream.ExitBlock();
    }

    VOID
    WriteMetadata()
    {
        ULONG StringAbbrev = 0;
        ULONG NameAbbrev = 0;

        if (m_Mds.empty() && m_Module.Named.empty())
            return;

        m_Stream.EnterBlock(BC_BLOCK_METADATA, 3);
        for (const DXIL_MD* Node : m_Mds)
        {
            if (Node->Kind == DxilMdString)
            {
                StringAbbrev = m_Stream.DefineAbbrev({ BcLit(1), BcArr, BcFix(8) });
                break;
            }
        }

        if (!m_Module.Named.empty())
            NameAbbrev = m_Stream.DefineAbbrev({ BcLit(4), BcArr, BcFix(8) });

        for (const DXIL_MD* Node : m_Mds)
        {
            std::vector<ULONGLONG> Values;

            if (Node->Kind == DxilMdString)
            {
                for (CHAR Letter : Node->Text)
                    Values.push_back(static_cast<BYTE>(Letter));
                m_Stream.Record(1, Values, StringAbbrev);
            }
            else if (Node->Kind == DxilMdValue)
            {
                Values.push_back(m_TypeIds.at(Node->Value->Type));
                Values.push_back(m_ValueIds.at(Node->Value));
                m_Stream.Record(2, Values, 0);
            }
            else
            {
                for (const DXIL_MD* Operand : Node->Operands)
                    Values.push_back(Operand == nullptr ? 0 : m_MdIds.at(Operand) + 1);
                m_Stream.Record(3, Values, 0);
            }
        }

        for (const DXIL_NAMED_MD* Named : m_Module.Named)
        {
            std::vector<ULONGLONG> Values;

            for (CHAR Letter : Named->Name)
                Values.push_back(static_cast<BYTE>(Letter));
            m_Stream.Record(4, Values, NameAbbrev);

            Values.clear();
            for (const DXIL_MD* Operand : Named->Operands)
                Values.push_back(m_MdIds.at(Operand));
            m_Stream.Record(10, Values, 0);
        }
        m_Stream.ExitBlock();
    }

    VOID
    WriteMetadataKinds()
    {
        m_Stream.EnterBlock(BC_BLOCK_METADATA, 3);
        for (SIZE_T Index = 0; Index < m_Module.MdKinds.size(); Index++)
        {
            std::vector<ULONGLONG> Values(1, Index);

            for (CHAR Letter : m_Module.MdKinds[Index])
                Values.push_back(static_cast<BYTE>(Letter));
            m_Stream.Record(6, Values, 0);
        }
        m_Stream.ExitBlock();
    }

    /** Writes named values sorted by name, which keeps the output independent of hashing */
    VOID
    WriteSymbols(
        _In_ std::vector<std::pair<std::string, std::pair<ULONG, BOOLEAN>>>& Entries)
    {
        if (Entries.empty())
            return;

        std::sort(Entries.begin(), Entries.end(), [](const auto& Left, const auto& Right)
        {
            return Left.first < Right.first;
        });

        m_Stream.EnterBlock(BC_BLOCK_SYMBOLS, 4);
        for (const auto& Entry : Entries)
        {
            BOOLEAN Char6 = TRUE;
            BOOLEAN Seven = TRUE;
            std::vector<ULONGLONG> Values(1, Entry.second.first);
            ULONG Abbrev = BC_SYMBOL_ENTRY8;
            ULONG Code;

            for (CHAR Letter : Entry.first)
            {
                if (Char6)
                    Char6 = BcIsChar6(Letter);
                if ((static_cast<BYTE>(Letter) & 0x80) != 0)
                {
                    Seven = FALSE;
                    break;
                }
            }

            if (Entry.second.second)
            {
                Code = 2;
                if (Char6)
                    Abbrev = BC_SYMBOL_BLOCK6;
            }
            else
            {
                Code = 1;
                if (Char6)
                    Abbrev = BC_SYMBOL_ENTRY6;
                else if (Seven)
                    Abbrev = BC_SYMBOL_ENTRY7;
            }

            for (CHAR Letter : Entry.first)
                Values.push_back(static_cast<BYTE>(Letter));
            m_Stream.Record(Code, Values, Abbrev);
        }
        m_Stream.ExitBlock();
    }

    VOID
    WriteModuleSymbols()
    {
        std::vector<std::pair<std::string, std::pair<ULONG, BOOLEAN>>> Entries;

        for (const DXIL_FUNCTION* Function : m_Module.Functions)
            Entries.push_back(std::make_pair(Function->Name, std::make_pair(m_ValueIds.at(Function), FALSE)));

        WriteSymbols(Entries);
    }

    ULONGLONG
    Relative(
        _In_ const DXIL_VALUE* Value,
        _In_ ULONG InstructionId) const
    {
        return static_cast<ULONG>(InstructionId - m_ValueIds.at(Value));
    }

    VOID
    WriteFunction(
        _In_ const DXIL_FUNCTION* Function)
    {
        ULONG ModuleValues = static_cast<ULONG>(m_Slots.size());
        ULONG FirstConstant;
        ULONG InstructionId;
        std::vector<std::pair<std::string, std::pair<ULONG, BOOLEAN>>> Entries;

        m_Stream.EnterBlock(BC_BLOCK_FUNCTION, 4);

        for (const DXIL_VALUE* Argument : Function->Arguments)
            NumberValue(Argument);

        FirstConstant = static_cast<ULONG>(m_Slots.size());
        for (const DXIL_BLOCK* Block : Function->Blocks)
        {
            for (const DXIL_INSTRUCTION* Instruction : Block->Instructions)
            {
                for (const DXIL_VALUE* Operand : Instruction->Operands)
                {
                    if (IsConstant(Operand))
                        NumberValue(Operand);
                }
            }
        }
        OrderConstants(FirstConstant, static_cast<ULONG>(m_Slots.size()));

        InstructionId = static_cast<ULONG>(m_Slots.size());
        for (const DXIL_BLOCK* Block : Function->Blocks)
        {
            for (const DXIL_INSTRUCTION* Instruction : Block->Instructions)
            {
                if (Instruction->Type->Kind != DxilTypeVoid)
                    NumberValue(Instruction);
            }
        }

        m_Stream.Record(1, std::vector<ULONGLONG>(1, Function->Blocks.size()), 0);
        WriteConstants(FirstConstant, InstructionId, FALSE);

        for (SIZE_T Index = 0; Index < Function->Blocks.size(); Index++)
        {
            const DXIL_BLOCK* Block = Function->Blocks[Index];

            if (!Block->Name.empty())
                Entries.push_back(std::make_pair(Block->Name, std::make_pair(static_cast<ULONG>(Index), TRUE)));

            for (const DXIL_INSTRUCTION* Instruction : Block->Instructions)
            {
                WriteInstruction(Instruction, InstructionId);
                if (Instruction->Type->Kind != DxilTypeVoid)
                    InstructionId += 1;
            }
        }

        WriteSymbols(Entries);
        m_Stream.ExitBlock();

        for (SIZE_T Index = ModuleValues; Index < m_Slots.size(); Index++)
            m_ValueIds.erase(m_Slots[Index].Value);
        m_Slots.resize(ModuleValues);
    }

    VOID
    WriteInstruction(
        _In_ const DXIL_INSTRUCTION* Instruction,
        _In_ ULONG InstructionId)
    {
        std::vector<ULONGLONG> Values;

        switch (Instruction->Opcode)
        {
        case DxilOpReturn:
            if (Instruction->Operands.empty())
            {
                m_Stream.Record(10, Values, BC_FUNCTION_RETVOID);
                return;
            }
            break;

        case DxilOpCall:
        {
            const DXIL_FUNCTION* Callee = static_cast<const DXIL_FUNCTION*>(Instruction->Operands[0]);

            Values.push_back(0);
            Values.push_back(1ULL << 15);
            Values.push_back(m_TypeIds.at(Callee->Signature));
            Values.push_back(Relative(Callee, InstructionId));
            for (SIZE_T Index = 1; Index < Instruction->Operands.size(); Index++)
                Values.push_back(Relative(Instruction->Operands[Index], InstructionId));
            m_Stream.Record(34, Values, 0);
            return;
        }

        default:
            break;
        }

        DxcTrace(L"instruction kind %u has no bitcode form yet", Instruction->Opcode);
    }

    const CDxilModule& m_Module;
    CBcStream m_Stream;
    ULONG m_TypeBits;
    ULONG m_FirstConstant;
    std::vector<const DXIL_TYPE*> m_Types;
    std::unordered_map<const DXIL_TYPE*, ULONG> m_TypeIds;
    std::vector<BC_SLOT> m_Slots;
    std::unordered_map<const DXIL_VALUE*, ULONG> m_ValueIds;
    std::vector<const DXIL_MD*> m_Mds;
    std::unordered_map<const DXIL_MD*, ULONG> m_MdIds;
    std::unordered_map<const DXIL_MD*, BOOLEAN> m_MdSeen;
    std::vector<DXIL_ATTRIBUTES> m_AttributeLists;
};

VOID
DxilWriteBitcode(
    _In_ const CDxilModule& Module,
    _Out_ std::vector<BYTE>& Out)
{
    CBcModuleWriter Writer(Module, Out);

    Out.clear();
    Writer.Write();
}
