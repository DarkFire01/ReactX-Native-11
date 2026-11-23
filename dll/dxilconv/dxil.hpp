/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     In memory DXIL module: types, values, instructions and metadata
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

typedef enum _DXIL_TYPE_KIND
{
    DxilTypeVoid,
    DxilTypeHalf,
    DxilTypeFloat,
    DxilTypeDouble,
    DxilTypeLabel,
    DxilTypeMetadata,
    DxilTypeInteger,
    DxilTypeFunction,
    DxilTypeStruct,
    DxilTypeArray,
    DxilTypePointer,
    DxilTypeVector,
} DXIL_TYPE_KIND;

struct DXIL_TYPE
{
    DXIL_TYPE_KIND Kind;
    ULONG Width;                          /**< integer bits, array or vector length */
    ULONG AddressSpace;
    BOOLEAN Packed;
    std::string Name;                     /**< named structs only */
    std::vector<DXIL_TYPE*> Children;     /**< function: return then parameters; struct: members; else element */
};

typedef enum _DXIL_VALUE_KIND
{
    DxilValueFunction,
    DxilValueArgument,
    DxilValueBlock,
    DxilValueInteger,
    DxilValueFloat,
    DxilValueNull,
    DxilValueUndef,
    DxilValueInstruction,
} DXIL_VALUE_KIND;

/* Bitcode instruction kinds used by the writer */
typedef enum _DXIL_OPCODE
{
    DxilOpReturn,
    DxilOpBranch,
    DxilOpCall,
    DxilOpBinary,
    DxilOpCast,
    DxilOpCompare,
    DxilOpSelect,
    DxilOpPhi,
    DxilOpExtractValue,
} DXIL_OPCODE;

struct DXIL_BLOCK;
struct DXIL_FUNCTION;

struct DXIL_VALUE
{
    DXIL_VALUE_KIND Kind;
    DXIL_TYPE* Type;
    ULONGLONG Bits;                       /**< constant payload */
    std::string Name;
};

struct DXIL_INSTRUCTION : DXIL_VALUE
{
    DXIL_OPCODE Opcode;
    ULONG Detail;                         /**< binary or cast opcode, predicate, flags */
    std::vector<DXIL_VALUE*> Operands;    /**< call: callee first, then arguments */
};

struct DXIL_BLOCK : DXIL_VALUE
{
    std::vector<DXIL_INSTRUCTION*> Instructions;
};

/* One function attribute list, the bitcode enum ids in canonical order */
typedef std::vector<ULONG> DXIL_ATTRIBUTES;

struct DXIL_FUNCTION : DXIL_VALUE
{
    DXIL_TYPE* Signature;
    DXIL_ATTRIBUTES Attributes;
    std::vector<DXIL_BLOCK*> Blocks;
    std::vector<DXIL_VALUE*> Arguments;
};

typedef enum _DXIL_MD_KIND
{
    DxilMdString,
    DxilMdValue,
    DxilMdTuple,
} DXIL_MD_KIND;

struct DXIL_MD
{
    DXIL_MD_KIND Kind;
    std::string Text;
    DXIL_VALUE* Value;
    std::vector<DXIL_MD*> Operands;       /**< nullptr stands for a null operand */
};

struct DXIL_NAMED_MD
{
    std::string Name;
    std::vector<DXIL_MD*> Operands;
};

/* Bitcode ids of the function attributes the converter uses */
#define DXIL_ATTR_NOUNWIND      18
#define DXIL_ATTR_READNONE      20
#define DXIL_ATTR_READONLY      21

/**
 * Owns everything in a module. Types, constants and metadata are uniqued so that
 * equal requests hand back the same object, which the writer relies on.
 */
class CDxilModule
{
public:
    CDxilModule();
    ~CDxilModule();

    DXIL_TYPE* VoidType();
    DXIL_TYPE* FloatType();
    DXIL_TYPE* MetadataType();
    DXIL_TYPE* IntType(_In_ ULONG Width);

    DXIL_TYPE*
    FunctionType(
        _In_ DXIL_TYPE* Return,
        _In_ const std::vector<DXIL_TYPE*>& Parameters);

    DXIL_TYPE*
    PointerType(
        _In_ DXIL_TYPE* Element);

    DXIL_VALUE*
    Int(
        _In_ ULONG Width,
        _In_ ULONGLONG Value);

    DXIL_VALUE*
    Float(
        _In_ ULONG Bits);

    DXIL_VALUE*
    Undef(
        _In_ DXIL_TYPE* Type);

    DXIL_FUNCTION*
    GetFunction(
        _In_ const std::string& Name,
        _In_ DXIL_TYPE* Signature,
        _In_ const DXIL_ATTRIBUTES& Attributes);

    DXIL_BLOCK*
    AddBlock(
        _In_ DXIL_FUNCTION* Function,
        _In_ const std::string& Name);

    DXIL_INSTRUCTION*
    AddInstruction(
        _In_ DXIL_BLOCK* Block,
        _In_ DXIL_OPCODE Opcode,
        _In_ DXIL_TYPE* Type,
        _In_ ULONG Detail,
        _In_ const std::vector<DXIL_VALUE*>& Operands);

    DXIL_MD*
    MdString(
        _In_ const std::string& Text);

    DXIL_MD*
    MdValue(
        _In_ DXIL_VALUE* Value);

    DXIL_MD*
    MdTuple(
        _In_ const std::vector<DXIL_MD*>& Operands);

    VOID
    AddNamed(
        _In_ const std::string& Name,
        _In_ DXIL_MD* Operand);

    std::vector<DXIL_FUNCTION*> Functions;
    std::vector<DXIL_NAMED_MD*> Named;
    std::vector<std::string> MdKinds;

private:
    DXIL_TYPE*
    InternType(
        _In_ const DXIL_TYPE& Shape);

    std::vector<std::unique_ptr<DXIL_TYPE>> m_Types;
    std::vector<std::unique_ptr<DXIL_VALUE>> m_Values;
    std::vector<std::unique_ptr<DXIL_MD>> m_Metadata;
    std::vector<std::unique_ptr<DXIL_NAMED_MD>> m_NamedStore;
    std::map<std::pair<DXIL_TYPE*, ULONGLONG>, DXIL_VALUE*> m_Constants;
    std::map<DXIL_TYPE*, DXIL_VALUE*> m_Undefs;
    std::map<std::string, DXIL_MD*> m_Strings;
    std::map<DXIL_VALUE*, DXIL_MD*> m_ValueMds;
    std::map<std::vector<DXIL_MD*>, DXIL_MD*> m_Tuples;
};

/* Writes the module as LLVM 3.7 bitcode, the form DXIL carries */
VOID
DxilWriteBitcode(
    _In_ const CDxilModule& Module,
    _Out_ std::vector<BYTE>& Out);
