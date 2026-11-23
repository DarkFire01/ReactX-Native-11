/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Turns a DXBC token stream and its signatures into a DXIL module
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"
#include "dxil.hpp"
#include "translate.hpp"

/* Token stream opcodes */
#define DXBC_OP_MOV                 54
#define DXBC_OP_RET                 62
#define DXBC_OP_DCL_INPUT           95
#define DXBC_OP_DCL_INPUT_SGV       96
#define DXBC_OP_DCL_INPUT_SIV       97
#define DXBC_OP_DCL_INPUT_PS        98
#define DXBC_OP_DCL_OUTPUT          101
#define DXBC_OP_DCL_OUTPUT_SGV      102
#define DXBC_OP_DCL_OUTPUT_SIV      103
#define DXBC_OP_DCL_GLOBAL_FLAGS    106
#define DXBC_OP_CUSTOM_DATA         53

/* Operand register files */
#define DXBC_FILE_INPUT             1
#define DXBC_FILE_OUTPUT            2
#define DXBC_FILE_IMMEDIATE32       4

#define DXBC_FLAG_REFACTORING       (1u << 11)

/* DXIL operation numbers */
#define DXIL_OP_LOAD_INPUT          4
#define DXIL_OP_STORE_OUTPUT        5

/* Shader flag bits that land in the entry point properties */
#define DXIL_FLAG_NO_REFACTORING    0x2ULL
#define DXIL_FLAG_RESOURCES_BOUND   0x100ULL

/* A decoded operand: register file, first index and per component sources */
typedef struct _DXC_OPERAND
{
    ULONG File;
    ULONG Index;
    ULONG Components;
    UCHAR Mask;
    UCHAR Swizzle[4];
    ULONG Immediate[4];
} DXC_OPERAND;

HRESULT
DxcReadSignature(
    _In_ const DXC_PART* Part,
    _In_ ULONG Stride,
    _Out_ std::vector<DXC_SIGNATURE_ELEMENT>& Elements)
{
    const BYTE* Data = Part->Data;
    ULONG Count;
    ULONG Offset;

    Elements.clear();
    if (Part->Size < 8)
        return E_FAIL;

    memcpy(&Count, Data, sizeof(Count));
    memcpy(&Offset, Data + 4, sizeof(Offset));
    if (Offset > Part->Size || static_cast<ULONGLONG>(Count) * Stride > Part->Size - Offset)
        return E_FAIL;

    for (ULONG Index = 0; Index < Count; Index++)
    {
        const BYTE* Entry = Data + Offset + Index * Stride;
        DXC_SIGNATURE_ELEMENT Element = {};
        ULONG Fields[6];
        ULONG NameOffset;

        if (Stride != 24)
        {
            memcpy(&Element.Stream, Entry, sizeof(ULONG));
            Entry += 4;
        }

        memcpy(Fields, Entry, sizeof(Fields));
        NameOffset = Fields[0];
        if (NameOffset >= Part->Size)
            return E_FAIL;

        Element.Name = reinterpret_cast<const char*>(Data + NameOffset);
        Element.SemanticIndex = Fields[1];
        Element.SystemValue = Fields[2];
        Element.ComponentType = Fields[3];
        Element.Register = Fields[4];
        Element.Mask = Entry[20];
        Element.UsedMask = Entry[21];
        if (Stride == 32)
            memcpy(&Element.MinPrecision, Entry + 24, sizeof(ULONG));

        Elements.push_back(Element);
    }

    return S_OK;
}

static
ULONG
DxcSemanticKind(
    _In_ ULONG SystemValue)
{
    switch (SystemValue)
    {
    case 1:
        return 3;
    case 2:
        return 6;
    case 3:
        return 7;
    case 4:
        return 4;
    case 5:
        return 5;
    case 6:
        return 1;
    case 7:
        return 10;
    case 8:
        return 2;
    case 9:
        return 13;
    case 10:
        return 12;
    case 11:
    case 13:
    case 15:
    case 16:
        return 25;
    case 12:
    case 14:
        return 26;
    case 64:
        return 16;
    case 65:
        return 17;
    case 66:
        return 14;
    case 67:
        return 19;
    case 68:
        return 18;
    case 69:
        return 20;
    case 70:
        return 15;
    default:
        return 0;
    }
}

/* System value names that carry their meaning even when the element says undefined */
static const struct
{
    const char* Name;
    ULONG Kind;
} DxcSemanticNames[] =
{
    { "SV_VertexID", 1 },
    { "SV_InstanceID", 2 },
    { "SV_Position", 3 },
    { "SV_RenderTargetArrayIndex", 4 },
    { "SV_ViewportArrayIndex", 5 },
    { "SV_ClipDistance", 6 },
    { "SV_CullDistance", 7 },
    { "SV_OutputControlPointID", 8 },
    { "SV_DomainLocation", 9 },
    { "SV_PrimitiveID", 10 },
    { "SV_GSInstanceID", 11 },
    { "SV_SampleIndex", 12 },
    { "SV_IsFrontFace", 13 },
    { "SV_Coverage", 14 },
    { "SV_InnerCoverage", 15 },
    { "SV_Target", 16 },
    { "SV_Depth", 17 },
    { "SV_DepthLessEqual", 18 },
    { "SV_DepthGreaterEqual", 19 },
    { "SV_StencilRef", 20 },
    { "SV_TessFactor", 25 },
    { "SV_InsideTessFactor", 26 },
};

static
ULONG
DxcSemanticKindOf(
    _In_ const DXC_SIGNATURE_ELEMENT* Element)
{
    if (Element->SystemValue != 0)
        return DxcSemanticKind(Element->SystemValue);

    for (ULONG Index = 0; Index < _countof(DxcSemanticNames); Index++)
    {
        if (_stricmp(Element->Name, DxcSemanticNames[Index].Name) == 0)
            return DxcSemanticNames[Index].Kind;
    }

    return 0;
}

static
ULONG
DxcComponentType(
    _In_ ULONG RegisterType)
{
    switch (RegisterType)
    {
    case 1:
        return 5;
    case 2:
        return 4;
    case 3:
        return 9;
    default:
        return 0;
    }
}

/* Builds the module while walking the program once */
class CDxbcTranslator
{
public:
    CDxbcTranslator(
        _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Inputs,
        _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Outputs)
        : m_Inputs(Inputs),
          m_Outputs(Outputs),
          m_Kind(0),
          m_Flags(DXIL_FLAG_NO_REFACTORING | DXIL_FLAG_RESOURCES_BOUND),
          m_Main(nullptr),
          m_Block(nullptr)
    {
    }

    HRESULT
    Run(
        _In_reads_(TokenCount) const ULONG* Tokens,
        _In_ ULONG TokenCount,
        _Out_ DXC_TRANSLATION* Result)
    {
        ULONG Length;
        ULONG Cursor = 2;
        HRESULT hr;

        if (TokenCount < 2)
            return DXC_STATUS_BAD_BYTECODE;

        m_Kind = (Tokens[0] >> 16) & 0xFFFF;
        Length = Tokens[1];
        if (Length > TokenCount || Length < 2)
            return DXC_STATUS_BAD_BYTECODE;

        m_Main = m_Module.GetFunction("main", m_Module.FunctionType(m_Module.VoidType(), {}), {});
        m_Block = m_Module.AddBlock(m_Main, "entry");

        while (Cursor < Length)
        {
            ULONG Opcode = Tokens[Cursor] & 0x7FF;
            ULONG Size = (Tokens[Cursor] >> 24) & 0x7F;

            if (Opcode == DXBC_OP_CUSTOM_DATA)
            {
                if (Cursor + 1 >= Length)
                    return DXC_STATUS_BAD_BYTECODE;
                Size = Tokens[Cursor + 1];
            }

            if (Size == 0 || Cursor + Size > Length)
                return DXC_STATUS_BAD_BYTECODE;

            hr = Instruction(Tokens + Cursor, Size);
            if (FAILED(hr))
                return hr;

            Cursor += Size;
        }

        EmitMetadata();

        Result->ProgramKind = m_Kind;
        Result->Major = 6;
        Result->Minor = 0;
        memset(Result->StageInfo, 0, sizeof(Result->StageInfo));
        Result->Features = 0;
        if (m_Kind == 1)
            Result->StageInfo[0] = HasSystemOutput(1) ? 1 : 0;

        DxilWriteBitcode(m_Module, Result->Bitcode);
        return S_OK;
    }

private:
    BOOLEAN
    HasSystemOutput(
        _In_ ULONG SystemValue) const
    {
        for (const DXC_SIGNATURE_ELEMENT& Element : m_Outputs)
        {
            if (Element.SystemValue == SystemValue)
                return TRUE;
        }

        return FALSE;
    }

    /** Decodes one operand starting at Tokens[*Cursor] and moves the cursor past it */
    HRESULT
    ReadOperand(
        _In_reads_(Count) const ULONG* Tokens,
        _In_ ULONG Count,
        _Inout_ ULONG* Cursor,
        _Out_ DXC_OPERAND* Operand)
    {
        ULONG Token;
        ULONG Dimension;

        memset(Operand, 0, sizeof(*Operand));
        if (*Cursor >= Count)
            return DXC_STATUS_BAD_BYTECODE;

        Token = Tokens[*Cursor];
        *Cursor += 1;
        if ((Token & 0x80000000) != 0)
            *Cursor += 1;

        Operand->File = (Token >> 12) & 0xFF;
        Operand->Components = (Token & 3) == 2 ? 4 : ((Token & 3) == 1 ? 1 : 0);
        Operand->Mask = 0xF;
        for (ULONG Lane = 0; Lane < 4; Lane++)
            Operand->Swizzle[Lane] = static_cast<UCHAR>(Lane);

        if (Operand->Components == 4)
        {
            ULONG Mode = (Token >> 2) & 3;

            if (Mode == 0)
            {
                Operand->Mask = static_cast<UCHAR>((Token >> 4) & 0xF);
            }
            else if (Mode == 1)
            {
                for (ULONG Lane = 0; Lane < 4; Lane++)
                    Operand->Swizzle[Lane] = static_cast<UCHAR>((Token >> (4 + Lane * 2)) & 3);
            }
            else
            {
                for (ULONG Lane = 0; Lane < 4; Lane++)
                    Operand->Swizzle[Lane] = static_cast<UCHAR>((Token >> 4) & 3);
            }
        }

        if (Operand->File == DXBC_FILE_IMMEDIATE32)
        {
            ULONG Values = Operand->Components == 4 ? 4 : 1;

            if (*Cursor + Values > Count)
                return DXC_STATUS_BAD_BYTECODE;

            for (ULONG Lane = 0; Lane < 4; Lane++)
                Operand->Immediate[Lane] = Tokens[*Cursor + (Values == 4 ? Lane : 0)];
            *Cursor += Values;
            return S_OK;
        }

        Dimension = (Token >> 20) & 3;
        for (ULONG Level = 0; Level < Dimension; Level++)
        {
            ULONG Representation = (Token >> (22 + Level * 3)) & 7;

            if (Representation != 0 || *Cursor >= Count)
            {
                DxcTrace(L"operand index representation %u is not supported yet", Representation);
                return E_NOTIMPL;
            }

            if (Level == 0)
                Operand->Index = Tokens[*Cursor];
            *Cursor += 1;
        }

        return S_OK;
    }

    HRESULT
    Instruction(
        _In_reads_(Size) const ULONG* Tokens,
        _In_ ULONG Size)
    {
        ULONG Opcode = Tokens[0] & 0x7FF;
        ULONG Cursor = 1;

        if ((Tokens[0] & 0x80000000) != 0)
            Cursor += 1;

        switch (Opcode)
        {
        case DXBC_OP_DCL_GLOBAL_FLAGS:
            if ((Tokens[0] & DXBC_FLAG_REFACTORING) != 0)
                m_Flags &= ~DXIL_FLAG_NO_REFACTORING;
            return S_OK;

        case DXBC_OP_DCL_INPUT:
        case DXBC_OP_DCL_INPUT_SGV:
        case DXBC_OP_DCL_INPUT_SIV:
        case DXBC_OP_DCL_INPUT_PS:
        case DXBC_OP_DCL_OUTPUT:
        case DXBC_OP_DCL_OUTPUT_SGV:
        case DXBC_OP_DCL_OUTPUT_SIV:
            return S_OK;

        case DXBC_OP_MOV:
            return Move(Tokens, Size, Cursor);

        case DXBC_OP_RET:
            m_Module.AddInstruction(m_Block, DxilOpReturn, m_Module.VoidType(), 0, {});
            return S_OK;

        default:
            DxcTrace(L"DXBC opcode %u is not supported yet", Opcode);
            return E_NOTIMPL;
        }
    }

    LONG
    FindElement(
        _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Elements,
        _In_ ULONG Register,
        _In_ ULONG Component) const
    {
        for (SIZE_T Index = 0; Index < Elements.size(); Index++)
        {
            if (Elements[Index].Register == Register && (Elements[Index].Mask & (1u << Component)) != 0)
                return static_cast<LONG>(Index);
        }

        return -1;
    }

    static
    ULONG
    LowestComponent(
        _In_ UCHAR Mask)
    {
        for (ULONG Lane = 0; Lane < 4; Lane++)
        {
            if ((Mask & (1u << Lane)) != 0)
                return Lane;
        }

        return 0;
    }

    DXIL_FUNCTION*
    LoadInputFunction()
    {
        DXIL_TYPE* I32 = m_Module.IntType(32);
        DXIL_TYPE* I8 = m_Module.IntType(8);

        return m_Module.GetFunction("dx.op.loadInput.f32",
                                    m_Module.FunctionType(m_Module.FloatType(), { I32, I32, I32, I8, I32 }),
                                    { DXIL_ATTR_NOUNWIND, DXIL_ATTR_READNONE });
    }

    DXIL_FUNCTION*
    StoreOutputFunction()
    {
        DXIL_TYPE* I32 = m_Module.IntType(32);
        DXIL_TYPE* I8 = m_Module.IntType(8);

        return m_Module.GetFunction("dx.op.storeOutput.f32",
                                    m_Module.FunctionType(m_Module.VoidType(), { I32, I32, I32, I8, m_Module.FloatType() }),
                                    { DXIL_ATTR_NOUNWIND });
    }

    /** Produces the float value of one source component */
    HRESULT
    SourceValue(
        _In_ const DXC_OPERAND* Source,
        _In_ ULONG Lane,
        _Out_ DXIL_VALUE** Value)
    {
        ULONG Component = Source->Swizzle[Lane];
        LONG Element;

        if (Source->File == DXBC_FILE_IMMEDIATE32)
        {
            *Value = m_Module.Float(Source->Immediate[Component]);
            return S_OK;
        }

        if (Source->File != DXBC_FILE_INPUT)
        {
            DxcTrace(L"source register file %u is not supported yet", Source->File);
            return E_NOTIMPL;
        }

        Element = FindElement(m_Inputs, Source->Index, Component);
        if (Element < 0)
            return DXC_STATUS_BAD_BYTECODE;

        *Value = m_Module.AddInstruction(m_Block,
                                         DxilOpCall,
                                         m_Module.FloatType(),
                                         0,
                                         {
                                             LoadInputFunction(),
                                             m_Module.Int(32, DXIL_OP_LOAD_INPUT),
                                             m_Module.Int(32, static_cast<ULONG>(Element)),
                                             m_Module.Int(32, 0),
                                             m_Module.Int(8, Component - LowestComponent(m_Inputs[Element].Mask)),
                                             m_Module.Undef(m_Module.IntType(32)),
                                         });
        return S_OK;
    }

    HRESULT
    Move(
        _In_reads_(Size) const ULONG* Tokens,
        _In_ ULONG Size,
        _In_ ULONG Cursor)
    {
        DXC_OPERAND Target;
        DXC_OPERAND Source;
        DXIL_VALUE* Values[4] = {};
        HRESULT hr;

        hr = ReadOperand(Tokens, Size, &Cursor, &Target);
        if (SUCCEEDED(hr))
            hr = ReadOperand(Tokens, Size, &Cursor, &Source);
        if (FAILED(hr))
            return hr;

        if (Target.File != DXBC_FILE_OUTPUT)
        {
            DxcTrace(L"destination register file %u is not supported yet", Target.File);
            return E_NOTIMPL;
        }

        for (ULONG Lane = 0; Lane < 4; Lane++)
        {
            if ((Target.Mask & (1u << Lane)) == 0)
                continue;

            hr = SourceValue(&Source, Lane, &Values[Lane]);
            if (FAILED(hr))
                return hr;
        }

        for (ULONG Lane = 0; Lane < 4; Lane++)
        {
            LONG Element;

            if ((Target.Mask & (1u << Lane)) == 0)
                continue;

            Element = FindElement(m_Outputs, Target.Index, Lane);
            if (Element < 0)
                return DXC_STATUS_BAD_BYTECODE;

            m_Module.AddInstruction(m_Block,
                                    DxilOpCall,
                                    m_Module.VoidType(),
                                    0,
                                    {
                                        StoreOutputFunction(),
                                        m_Module.Int(32, DXIL_OP_STORE_OUTPUT),
                                        m_Module.Int(32, static_cast<ULONG>(Element)),
                                        m_Module.Int(32, 0),
                                        m_Module.Int(8, Lane - LowestComponent(m_Outputs[Element].Mask)),
                                        Values[Lane],
                                    });
        }

        return S_OK;
    }

    DXIL_MD*
    I32(
        _In_ ULONG Value)
    {
        return m_Module.MdValue(m_Module.Int(32, Value));
    }

    DXIL_MD*
    I8(
        _In_ ULONG Value)
    {
        return m_Module.MdValue(m_Module.Int(8, Value));
    }

    /** One signature list, or null when the stage has no elements there */
    DXIL_MD*
    SignatureMetadata(
        _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Elements)
    {
        std::vector<DXIL_MD*> Entries;

        if (Elements.empty())
            return nullptr;

        for (SIZE_T Index = 0; Index < Elements.size(); Index++)
        {
            const DXC_SIGNATURE_ELEMENT& Element = Elements[Index];
            ULONG Columns = 0;

            for (ULONG Lane = 0; Lane < 4; Lane++)
            {
                if ((Element.Mask & (1u << Lane)) != 0)
                    Columns += 1;
            }

            Entries.push_back(m_Module.MdTuple({
                I32(static_cast<ULONG>(Index)),
                m_Module.MdString(Element.Name),
                I8(DxcComponentType(Element.ComponentType)),
                I8(DxcSemanticKindOf(&Element)),
                m_Module.MdTuple({ I32(Element.SemanticIndex) }),
                I8(0),
                I32(1),
                I8(Columns),
                I32(Element.Register),
                I8(LowestComponent(Element.Mask)),
                nullptr,
            }));
        }

        return m_Module.MdTuple(Entries);
    }

    VOID
    EmitMetadata()
    {
        static const char* const StageNames[] = { "ps", "vs", "gs", "hs", "ds", "cs" };
        DXIL_MD* Version = m_Module.MdTuple({ I32(1), I32(0) });
        DXIL_MD* Signatures;
        DXIL_MD* Properties;

        m_Module.AddNamed("dx.version", Version);
        m_Module.AddNamed("dx.valver", Version);
        m_Module.AddNamed("dx.shaderModel",
                          m_Module.MdTuple({ m_Module.MdString(StageNames[m_Kind < 6 ? m_Kind : 0]), I32(6), I32(0) }));

        Signatures = m_Module.MdTuple({ SignatureMetadata(m_Inputs), SignatureMetadata(m_Outputs), nullptr });
        Properties = m_Module.MdTuple({ I32(0), m_Module.MdValue(m_Module.Int(64, m_Flags)) });

        m_Module.AddNamed("dx.entryPoints",
                          m_Module.MdTuple({
                              m_Module.MdValue(m_Main),
                              m_Module.MdString("main"),
                              Signatures,
                              nullptr,
                              Properties,
                          }));
        m_Module.AddNamed("llvm.ident", m_Module.MdTuple({ m_Module.MdString("dxbc2dxil 1.2") }));
    }

    const std::vector<DXC_SIGNATURE_ELEMENT>& m_Inputs;
    const std::vector<DXC_SIGNATURE_ELEMENT>& m_Outputs;
    CDxilModule m_Module;
    ULONG m_Kind;
    ULONGLONG m_Flags;
    DXIL_FUNCTION* m_Main;
    DXIL_BLOCK* m_Block;
};

HRESULT
DxcTranslateProgram(
    _In_reads_(TokenCount) const ULONG* Tokens,
    _In_ ULONG TokenCount,
    _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Inputs,
    _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Outputs,
    _Out_ DXC_TRANSLATION* Result)
{
    CDxbcTranslator Translator(Inputs, Outputs);

    return Translator.Run(Tokens, TokenCount, Result);
}
