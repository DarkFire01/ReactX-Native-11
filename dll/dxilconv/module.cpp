/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Module object storage and uniquing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"
#include "dxil.hpp"

/* Metadata kinds every module context starts with, in id order */
static const char* const DxilFixedKinds[] =
{
    "dbg",
    "tbaa",
    "prof",
    "fpmath",
    "range",
    "tbaa.struct",
    "invariant.load",
    "alias.scope",
    "noalias",
    "nontemporal",
    "llvm.mem.parallel_loop_access",
    "nonnull",
    "dereferenceable",
    "dereferenceable_or_null",
};

CDxilModule::CDxilModule()
{
    for (ULONG Index = 0; Index < _countof(DxilFixedKinds); Index++)
        MdKinds.push_back(DxilFixedKinds[Index]);
}

CDxilModule::~CDxilModule()
{
}

static
BOOLEAN
DxilSameShape(
    _In_ const DXIL_TYPE& Left,
    _In_ const DXIL_TYPE& Right)
{
    return Left.Kind == Right.Kind &&
           Left.Width == Right.Width &&
           Left.AddressSpace == Right.AddressSpace &&
           Left.Packed == Right.Packed &&
           Left.Name == Right.Name &&
           Left.Children == Right.Children;
}

DXIL_TYPE*
CDxilModule::InternType(
    _In_ const DXIL_TYPE& Shape)
{
    for (const std::unique_ptr<DXIL_TYPE>& Known : m_Types)
    {
        if (DxilSameShape(*Known, Shape))
            return Known.get();
    }

    m_Types.push_back(std::make_unique<DXIL_TYPE>(Shape));
    return m_Types.back().get();
}

DXIL_TYPE*
CDxilModule::VoidType()
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeVoid;
    return InternType(Shape);
}

DXIL_TYPE*
CDxilModule::FloatType()
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeFloat;
    return InternType(Shape);
}

DXIL_TYPE*
CDxilModule::MetadataType()
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeMetadata;
    return InternType(Shape);
}

DXIL_TYPE*
CDxilModule::IntType(
    _In_ ULONG Width)
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeInteger;
    Shape.Width = Width;
    return InternType(Shape);
}

DXIL_TYPE*
CDxilModule::FunctionType(
    _In_ DXIL_TYPE* Return,
    _In_ const std::vector<DXIL_TYPE*>& Parameters)
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeFunction;
    Shape.Children.push_back(Return);
    Shape.Children.insert(Shape.Children.end(), Parameters.begin(), Parameters.end());
    return InternType(Shape);
}

DXIL_TYPE*
CDxilModule::PointerType(
    _In_ DXIL_TYPE* Element)
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypePointer;
    Shape.Children.push_back(Element);
    return InternType(Shape);
}

DXIL_VALUE*
CDxilModule::Int(
    _In_ ULONG Width,
    _In_ ULONGLONG Value)
{
    DXIL_TYPE* Type = IntType(Width);
    ULONGLONG Masked = Width >= 64 ? Value : (Value & ((1ULL << Width) - 1));
    auto Found = m_Constants.find(std::make_pair(Type, Masked));

    if (Found != m_Constants.end())
        return Found->second;

    m_Values.push_back(std::make_unique<DXIL_VALUE>());
    DXIL_VALUE* Constant = m_Values.back().get();
    Constant->Kind = DxilValueInteger;
    Constant->Type = Type;
    Constant->Bits = Masked;
    m_Constants[std::make_pair(Type, Masked)] = Constant;
    return Constant;
}

DXIL_VALUE*
CDxilModule::Float(
    _In_ ULONG Bits)
{
    DXIL_TYPE* Type = FloatType();
    auto Found = m_Constants.find(std::make_pair(Type, static_cast<ULONGLONG>(Bits)));

    if (Found != m_Constants.end())
        return Found->second;

    m_Values.push_back(std::make_unique<DXIL_VALUE>());
    DXIL_VALUE* Constant = m_Values.back().get();
    Constant->Kind = DxilValueFloat;
    Constant->Type = Type;
    Constant->Bits = Bits;
    m_Constants[std::make_pair(Type, static_cast<ULONGLONG>(Bits))] = Constant;
    return Constant;
}

DXIL_VALUE*
CDxilModule::Undef(
    _In_ DXIL_TYPE* Type)
{
    auto Found = m_Undefs.find(Type);

    if (Found != m_Undefs.end())
        return Found->second;

    m_Values.push_back(std::make_unique<DXIL_VALUE>());
    DXIL_VALUE* Constant = m_Values.back().get();
    Constant->Kind = DxilValueUndef;
    Constant->Type = Type;
    m_Undefs[Type] = Constant;
    return Constant;
}

/** Returns the named function, declaring it on first use */
DXIL_FUNCTION*
CDxilModule::GetFunction(
    _In_ const std::string& Name,
    _In_ DXIL_TYPE* Signature,
    _In_ const DXIL_ATTRIBUTES& Attributes)
{
    for (DXIL_FUNCTION* Known : Functions)
    {
        if (Known->Name == Name)
            return Known;
    }

    m_Values.push_back(std::make_unique<DXIL_FUNCTION>());
    DXIL_FUNCTION* Function = static_cast<DXIL_FUNCTION*>(m_Values.back().get());
    Function->Kind = DxilValueFunction;
    Function->Type = PointerType(Signature);
    Function->Name = Name;
    Function->Signature = Signature;
    Function->Attributes = Attributes;
    Functions.push_back(Function);
    return Function;
}

DXIL_BLOCK*
CDxilModule::AddBlock(
    _In_ DXIL_FUNCTION* Function,
    _In_ const std::string& Name)
{
    DXIL_TYPE Shape = {};

    Shape.Kind = DxilTypeLabel;
    m_Values.push_back(std::make_unique<DXIL_BLOCK>());
    DXIL_BLOCK* Block = static_cast<DXIL_BLOCK*>(m_Values.back().get());
    Block->Kind = DxilValueBlock;
    Block->Type = InternType(Shape);
    Block->Name = Name;
    Function->Blocks.push_back(Block);
    return Block;
}

DXIL_INSTRUCTION*
CDxilModule::AddInstruction(
    _In_ DXIL_BLOCK* Block,
    _In_ DXIL_OPCODE Opcode,
    _In_ DXIL_TYPE* Type,
    _In_ ULONG Detail,
    _In_ const std::vector<DXIL_VALUE*>& Operands)
{
    m_Values.push_back(std::make_unique<DXIL_INSTRUCTION>());
    DXIL_INSTRUCTION* Instruction = static_cast<DXIL_INSTRUCTION*>(m_Values.back().get());
    Instruction->Kind = DxilValueInstruction;
    Instruction->Type = Type;
    Instruction->Opcode = Opcode;
    Instruction->Detail = Detail;
    Instruction->Operands = Operands;
    Block->Instructions.push_back(Instruction);
    return Instruction;
}

DXIL_MD*
CDxilModule::MdString(
    _In_ const std::string& Text)
{
    auto Found = m_Strings.find(Text);

    if (Found != m_Strings.end())
        return Found->second;

    m_Metadata.push_back(std::make_unique<DXIL_MD>());
    DXIL_MD* Node = m_Metadata.back().get();
    Node->Kind = DxilMdString;
    Node->Text = Text;
    Node->Value = nullptr;
    m_Strings[Text] = Node;
    return Node;
}

DXIL_MD*
CDxilModule::MdValue(
    _In_ DXIL_VALUE* Value)
{
    auto Found = m_ValueMds.find(Value);

    if (Found != m_ValueMds.end())
        return Found->second;

    m_Metadata.push_back(std::make_unique<DXIL_MD>());
    DXIL_MD* Node = m_Metadata.back().get();
    Node->Kind = DxilMdValue;
    Node->Value = Value;
    m_ValueMds[Value] = Node;
    return Node;
}

DXIL_MD*
CDxilModule::MdTuple(
    _In_ const std::vector<DXIL_MD*>& Operands)
{
    auto Found = m_Tuples.find(Operands);

    if (Found != m_Tuples.end())
        return Found->second;

    m_Metadata.push_back(std::make_unique<DXIL_MD>());
    DXIL_MD* Node = m_Metadata.back().get();
    Node->Kind = DxilMdTuple;
    Node->Value = nullptr;
    Node->Operands = Operands;
    m_Tuples[Operands] = Node;
    return Node;
}

/** Appends an operand to a named node, creating the node on first use */
VOID
CDxilModule::AddNamed(
    _In_ const std::string& Name,
    _In_ DXIL_MD* Operand)
{
    for (DXIL_NAMED_MD* Known : Named)
    {
        if (Known->Name == Name)
        {
            Known->Operands.push_back(Operand);
            return;
        }
    }

    m_NamedStore.push_back(std::make_unique<DXIL_NAMED_MD>());
    DXIL_NAMED_MD* Node = m_NamedStore.back().get();
    Node->Name = Name;
    Node->Operands.push_back(Operand);
    Named.push_back(Node);
}
