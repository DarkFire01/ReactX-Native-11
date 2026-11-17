/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Decodes the tokenized shader program into instructions
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

/* Reads one operand starting at Code[*Position] */
static
BOOLEAN
RxWarpDecodeOperand(
    _In_ const UINT *Code,
    _In_ UINT End,
    _Inout_ UINT *Position,
    _Out_ RX_WARP_OPERAND *Operand)
{
    UINT Token;
    UINT Components;

    ZeroMemory(Operand, sizeof(*Operand));
    if (*Position >= End)
        return FALSE;

    Token = Code[(*Position)++];
    Operand->Type = (UINT8)DECODE_D3D10_SB_OPERAND_TYPE(Token);
    Operand->Dimensions = (UINT8)DECODE_D3D10_SB_OPERAND_INDEX_DIMENSION(Token);
    for (UINT Index = 0; Index < 4; Index++)
        Operand->Swizzle[Index] = (UINT8)Index;
    Operand->Mask = 0xF;

    Components = DECODE_D3D10_SB_OPERAND_NUM_COMPONENTS(Token);
    if (Components == D3D10_SB_OPERAND_4_COMPONENT)
    {
        switch (DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECTION_MODE(Token))
        {
            case D3D10_SB_OPERAND_4_COMPONENT_MASK_MODE:
                Operand->Mask = (UINT8)(DECODE_D3D10_SB_OPERAND_4_COMPONENT_MASK(Token) >> D3D10_SB_OPERAND_4_COMPONENT_MASK_SHIFT);
                break;
            case D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE:
                for (UINT Index = 0; Index < 4; Index++)
                    Operand->Swizzle[Index] = (UINT8)DECODE_D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_SOURCE(Token, Index);
                break;
            case D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE:
                for (UINT Index = 0; Index < 4; Index++)
                    Operand->Swizzle[Index] = (UINT8)DECODE_D3D10_SB_OPERAND_4_COMPONENT_SELECT_1(Token);
                Operand->Mask = (UINT8)(1u << Operand->Swizzle[0]);
                break;
        }
    }
    else if (Components == D3D10_SB_OPERAND_1_COMPONENT)
    {
        for (UINT Index = 0; Index < 4; Index++)
            Operand->Swizzle[Index] = 0;
        Operand->Mask = 1;
    }
    else
    {
        Operand->Mask = 0;
    }

    /* Extended operand: modifiers, possibly chained */
    while (DECODE_IS_D3D10_SB_OPERAND_EXTENDED(Token))
    {
        if (*Position >= End)
            return FALSE;
        Token = Code[(*Position)++];
        if (DECODE_D3D10_SB_EXTENDED_OPERAND_TYPE(Token) == D3D10_SB_EXTENDED_OPERAND_MODIFIER)
        {
            UINT Modifier = DECODE_D3D10_SB_OPERAND_MODIFIER(Token);

            Operand->Negate = (Modifier == D3D10_SB_OPERAND_MODIFIER_NEG || Modifier == D3D10_SB_OPERAND_MODIFIER_ABSNEG);
            Operand->Absolute = (Modifier == D3D10_SB_OPERAND_MODIFIER_ABS || Modifier == D3D10_SB_OPERAND_MODIFIER_ABSNEG);
        }
    }

    if (Operand->Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        UINT Count = (Components == D3D10_SB_OPERAND_4_COMPONENT) ? 4 : 1;

        if (*Position + Count > End)
            return FALSE;
        for (UINT Index = 0; Index < Count; Index++)
            Operand->Immediate[Index] = Code[(*Position)++];
        if (Count == 1)
            Operand->Immediate[1] = Operand->Immediate[2] = Operand->Immediate[3] = Operand->Immediate[0];
        return TRUE;
    }
    if (Operand->Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE64)
        return FALSE;

    for (UINT Dimension = 0; Dimension < Operand->Dimensions && Dimension < 3; Dimension++)
    {
        UINT Representation = DECODE_D3D10_SB_OPERAND_INDEX_REPRESENTATION(Dimension, Token);
        RX_WARP_INDEX *Index = &Operand->Index[Dimension];

        Index->RelativeType = RxWarpNoRelative;
        switch (Representation)
        {
            case D3D10_SB_OPERAND_INDEX_IMMEDIATE32:
                Index->Immediate = Code[(*Position)++];
                break;
            case D3D10_SB_OPERAND_INDEX_IMMEDIATE64:
                Index->Immediate = Code[(*Position)++];
                (*Position)++;
                break;
            case D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE:
            case D3D10_SB_OPERAND_INDEX_RELATIVE:
            {
                RX_WARP_OPERAND Relative;

                if (Representation == D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE)
                    Index->Immediate = Code[(*Position)++];
                if (!RxWarpDecodeOperand(Code, End, Position, &Relative))
                    return FALSE;
                Index->RelativeType = Relative.Type;
                Index->RelativeComponent = Relative.Swizzle[0];
                Index->RelativeRegister = Relative.Index[0].Immediate;
                Index->RelativeElement = Relative.Index[1].Immediate;
                break;
            }
            default:
                return FALSE;
        }
    }
    return TRUE;
}

/* Fills in Target for each block opener, so execution can jump without searching */
static
HRESULT
RxWarpLinkBlocks(
    _Inout_ RxWarpShader *Shader)
{
    std::vector<UINT> Open;

    for (UINT Index = 0; Index < (UINT)Shader->Program.size(); Index++)
    {
        RX_WARP_INSTRUCTION &Instruction = Shader->Program[Index];

        switch (Instruction.Opcode)
        {
            case D3D10_SB_OPCODE_IF:
            case D3D10_SB_OPCODE_LOOP:
                Open.push_back(Index);
                break;
            case D3D10_SB_OPCODE_SWITCH:
                Instruction.Extra = (UINT)Shader->SwitchCases.size();
                Shader->SwitchCases.emplace_back();
                Open.push_back(Index);
                break;
            case D3D10_SB_OPCODE_CASE:
                if (Open.empty())
                    return E_INVALIDARG;
                Shader->SwitchCases[Shader->Program[Open.back()].Extra].push_back(Instruction.Operands[0].Immediate[0]);
                Instruction.Extra = Open.back();
                break;
            case D3D10_SB_OPCODE_DEFAULT:
                if (Open.empty())
                    return E_INVALIDARG;
                Instruction.Extra = Open.back();
                break;
            case D3D10_SB_OPCODE_ELSE:
                if (Open.empty())
                    return E_INVALIDARG;
                Shader->Program[Open.back()].Target = Index;
                Instruction.Extra = Open.back();
                break;
            case D3D10_SB_OPCODE_ENDIF:
            case D3D10_SB_OPCODE_ENDLOOP:
            case D3D10_SB_OPCODE_ENDSWITCH:
            {
                RX_WARP_INSTRUCTION *Opener;

                if (Open.empty())
                    return E_INVALIDARG;
                Opener = &Shader->Program[Open.back()];
                Instruction.Extra = Open.back();
                /* An if keeps its else as the target; the else then points at the endif */
                if (Opener->Opcode == D3D10_SB_OPCODE_IF && Opener->Target != 0)
                    Shader->Program[Opener->Target].Target = Index;
                else
                    Opener->Target = Index;
                Open.pop_back();
                break;
            }
            default:
                break;
        }
    }
    return Open.empty() ? S_OK : E_INVALIDARG;
}

/* Decodes the tokens in [Position, Length) into this program */
HRESULT
RxWarpShader::Decode(
    _In_ const UINT *Code,
    _In_ UINT Position,
    _In_ UINT Length)
{
    while (Position < Length)
    {
        UINT Token = Code[Position];
        UINT Opcode = DECODE_D3D10_SB_OPCODE_TYPE(Token);
        UINT Size;
        UINT End;
        UINT Cursor;
        RX_WARP_INSTRUCTION Instruction = {};
        RX_WARP_OPERAND Operand;

        if (Opcode == D3D10_SB_OPCODE_CUSTOMDATA)
        {
            Size = Code[Position + 1];
            if (DECODE_D3D10_SB_CUSTOMDATA_CLASS(Token) == D3D10_SB_CUSTOMDATA_DCL_IMMEDIATE_CONSTANT_BUFFER)
                ImmediateConstants.assign(Code + Position + 2, Code + Position + Size);
            if (Size < 2)
                return E_INVALIDARG;
            Position += Size;
            continue;
        }

        Size = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(Token);
        if (Size == 0 || Position + Size > Length)
            return E_INVALIDARG;
        End = Position + Size;
        Cursor = Position + 1;

        Instruction.Opcode = (UINT16)Opcode;
        Instruction.Control = Token;
        Instruction.Saturate = DECODE_IS_D3D10_SB_INSTRUCTION_SATURATE_ENABLED(Token) ? 1 : 0;
        Instruction.NonZero = (DECODE_D3D10_SB_INSTRUCTION_TEST_BOOLEAN(Token) == D3D10_SB_INSTRUCTION_TEST_NONZERO);
        Instruction.ReturnType = (UINT8)DECODE_D3D10_SB_RESINFO_INSTRUCTION_RETURN_TYPE(Token);

        /* Extended opcode tokens: texel offsets are the only ones that change results here */
        if (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(Token))
        {
            UINT Extended;

            do
            {
                Extended = Code[Cursor++];
                if (DECODE_D3D10_SB_EXTENDED_OPCODE_TYPE(Extended) == D3D10_SB_EXTENDED_OPCODE_SAMPLE_CONTROLS)
                {
                    for (UINT Coord = 0; Coord < 3; Coord++)
                    {
                        INT Value = (INT)DECODE_IMMEDIATE_D3D10_SB_ADDRESS_OFFSET(Coord, Extended);

                        Instruction.Offset[Coord] = (INT8)((Value & 8) ? Value - 16 : Value);
                    }
                }
            } while (DECODE_IS_D3D10_SB_OPCODE_EXTENDED(Extended) && Cursor < End);
        }

        switch (Opcode)
        {
            case D3D10_SB_OPCODE_DCL_TEMPS:
                TempCount = Code[Cursor];
                break;
            case D3D10_SB_OPCODE_DCL_INDEXABLE_TEMP:
            {
                UINT Register = Code[Cursor];

                if (Register >= Indexables.size())
                    Indexables.resize(Register + 1);
                Indexables[Register].Size = Code[Cursor + 1];
                Indexables[Register].Offset = IndexableRegisters;
                IndexableRegisters += Code[Cursor + 1];
                break;
            }
            case D3D10_SB_OPCODE_DCL_INPUT:
            case D3D10_SB_OPCODE_DCL_INPUT_SGV:
            case D3D10_SB_OPCODE_DCL_INPUT_SIV:
            case D3D10_SB_OPCODE_DCL_INPUT_PS:
            case D3D10_SB_OPCODE_DCL_INPUT_PS_SGV:
            case D3D10_SB_OPCODE_DCL_INPUT_PS_SIV:
            {
                UINT Register;

                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                if (Operand.Type != D3D10_SB_OPERAND_TYPE_INPUT)
                    break;

                /* Geometry inputs are v[vertex][register] */
                Register = Operand.Index[(Operand.Dimensions > 1) ? 1 : 0].Immediate;
                if (Register >= RxWarpMaxRegisters)
                    return E_INVALIDARG;
                InputMask[Register] |= Operand.Mask;
                if (Register + 1 > InputCount)
                    InputCount = Register + 1;
                if (Opcode == D3D10_SB_OPCODE_DCL_INPUT_PS || Opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SIV ||
                    Opcode == D3D10_SB_OPCODE_DCL_INPUT_PS_SGV)
                {
                    InputMode[Register] = (UINT8)DECODE_D3D10_SB_INPUT_INTERPOLATION_MODE(Token);
                }
                if (Opcode != D3D10_SB_OPCODE_DCL_INPUT && Opcode != D3D10_SB_OPCODE_DCL_INPUT_PS && Cursor < End)
                {
                    InputName[Register] = (UINT8)DECODE_D3D10_SB_NAME(Code[Cursor]);
                    if (InputName[Register] == D3D10_SB_NAME_POSITION)
                        PositionInput = Register;
                }
                break;
            }
            case D3D10_SB_OPCODE_DCL_OUTPUT:
            case D3D10_SB_OPCODE_DCL_OUTPUT_SGV:
            case D3D10_SB_OPCODE_DCL_OUTPUT_SIV:
            {
                UINT Register;

                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                if (Operand.Type == D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH ||
                    Operand.Type == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_GREATER_EQUAL ||
                    Operand.Type == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL)
                {
                    WritesDepth = TRUE;
                    break;
                }
                if (Operand.Type != D3D10_SB_OPERAND_TYPE_OUTPUT)
                    break;
                Register = Operand.Index[0].Immediate;
                if (Register >= RxWarpMaxRegisters)
                    return E_INVALIDARG;
                OutputMask[Register] |= Operand.Mask;
                if (Register + 1 > OutputCount)
                    OutputCount = Register + 1;
                if (Opcode != D3D10_SB_OPCODE_DCL_OUTPUT && Cursor < End)
                {
                    UINT Name = DECODE_D3D10_SB_NAME(Code[Cursor]);

                    OutputName[Register] = (UINT8)Name;
                    if (Name == D3D10_SB_NAME_POSITION)
                        PositionOutput = Register;
                    if (Name == D3D10_SB_NAME_CLIP_DISTANCE)
                        ClipMask[Register] |= Operand.Mask;
                    if (Name == D3D10_SB_NAME_CULL_DISTANCE)
                        CullMask[Register] |= Operand.Mask;
                    if (Name >= D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR && Name <= D3D11_SB_NAME_FINAL_LINE_DENSITY_TESSFACTOR)
                    {
                        ULONG Component = 0;

                        _BitScanForward(&Component, Operand.Mask ? Operand.Mask : 1);
                        TessFactorSlot[Name - D3D11_SB_NAME_FINAL_QUAD_U_EQ_0_EDGE_TESSFACTOR] = Register * 4 + Component + 1;
                    }
                }
                break;
            }
            case D3D10_SB_OPCODE_DCL_RESOURCE:
                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                if (Operand.Index[0].Immediate < 128)
                {
                    ResourceDimension[Operand.Index[0].Immediate] = DECODE_D3D10_SB_RESOURCE_DIMENSION(Token);
                    ResourceReturn[Operand.Index[0].Immediate] = (Cursor < End) ? Code[Cursor] : 0;
                }
                break;
            case D3D11_SB_OPCODE_DCL_THREAD_GROUP:
                GroupSize[0] = Code[Cursor];
                GroupSize[1] = Code[Cursor + 1];
                GroupSize[2] = Code[Cursor + 2];
                break;
            case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW:
            case D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_STRUCTURED:
            {
                RX_WARP_SHARED Region = {};
                UINT Register;

                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                Register = Operand.Index[0].Immediate;
                if (Opcode == D3D11_SB_OPCODE_DCL_THREAD_GROUP_SHARED_MEMORY_RAW)
                {
                    Region.Size = Code[Cursor];
                }
                else
                {
                    Region.Stride = Code[Cursor];
                    Region.Size = Code[Cursor] * Code[Cursor + 1];
                }
                Region.Offset = SharedBytes;
                SharedBytes += (Region.Size + 15) & ~15u;
                if (Register >= Shared.size())
                    Shared.resize(Register + 1);
                Shared[Register] = Region;
                break;
            }
            case D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED:
            case D3D11_SB_OPCODE_DCL_RESOURCE_RAW:
                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                if (Operand.Index[0].Immediate < 128)
                {
                    BOOLEAN Structured = (Opcode == D3D11_SB_OPCODE_DCL_RESOURCE_STRUCTURED);

                    ResourceDimension[Operand.Index[0].Immediate] = Structured ? D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER :
                                                                                 D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER;
                    ResourceStride[Operand.Index[0].Immediate] = Structured ? Code[Cursor] : 0;
                }
                break;
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED:
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW:
            case D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_STRUCTURED:
            {
                UINT Register;

                if (!RxWarpDecodeOperand(Code, End, &Cursor, &Operand))
                    return E_INVALIDARG;
                Register = Operand.Index[0].Immediate;
                if (Register >= 64)
                    break;
                if (Opcode == D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_TYPED)
                {
                    UavDimension[Register] = DECODE_D3D10_SB_RESOURCE_DIMENSION(Token);
                }
                else if (Opcode == D3D11_SB_OPCODE_DCL_UNORDERED_ACCESS_VIEW_RAW)
                {
                    UavDimension[Register] = D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER;
                }
                else
                {
                    UavDimension[Register] = D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER;
                    UavStride[Register] = Code[Cursor];
                }
                break;
            }
            case D3D10_SB_OPCODE_DCL_CONSTANT_BUFFER:
            case D3D10_SB_OPCODE_DCL_SAMPLER:
            case D3D10_SB_OPCODE_DCL_GLOBAL_FLAGS:
            case D3D10_SB_OPCODE_DCL_INDEX_RANGE:
            case D3D11_SB_OPCODE_DCL_STREAM:
                break;
            case D3D10_SB_OPCODE_DCL_GS_INPUT_PRIMITIVE:
                GsInputPrimitive = DECODE_D3D10_SB_GS_INPUT_PRIMITIVE(Token);
                break;
            case D3D10_SB_OPCODE_DCL_GS_OUTPUT_PRIMITIVE_TOPOLOGY:
                GsOutputTopology = DECODE_D3D10_SB_GS_OUTPUT_PRIMITIVE_TOPOLOGY(Token);
                break;
            case D3D10_SB_OPCODE_DCL_MAX_OUTPUT_VERTEX_COUNT:
                GsMaxVertices = Code[Cursor];
                break;
            case D3D11_SB_OPCODE_DCL_GS_INSTANCE_COUNT:
                GsInstances = Code[Cursor] ? Code[Cursor] : 1;
                break;
            case D3D11_SB_OPCODE_DCL_INPUT_CONTROL_POINT_COUNT:
                InputControlPoints = DECODE_D3D11_SB_INPUT_CONTROL_POINT_COUNT(Token);
                break;
            case D3D11_SB_OPCODE_DCL_OUTPUT_CONTROL_POINT_COUNT:
                OutputControlPoints = DECODE_D3D11_SB_OUTPUT_CONTROL_POINT_COUNT(Token);
                break;
            case D3D11_SB_OPCODE_DCL_TESS_DOMAIN:
                TessDomain = DECODE_D3D11_SB_TESS_DOMAIN(Token);
                break;
            case D3D11_SB_OPCODE_DCL_TESS_PARTITIONING:
                TessPartitioning = DECODE_D3D11_SB_TESS_PARTITIONING(Token);
                break;
            case D3D11_SB_OPCODE_DCL_TESS_OUTPUT_PRIMITIVE:
                TessOutput = DECODE_D3D11_SB_TESS_OUTPUT_PRIMITIVE(Token);
                break;
            case D3D11_SB_OPCODE_DCL_HS_FORK_PHASE_INSTANCE_COUNT:
            case D3D11_SB_OPCODE_DCL_HS_JOIN_PHASE_INSTANCE_COUNT:
                PhaseInstances = Code[Cursor];
                break;
            case D3D11_SB_OPCODE_DCL_HS_MAX_TESSFACTOR:
            case D3D11_SB_OPCODE_HS_DECLS:
                break;
            default:
            {
                UINT Count = 0;

                while (Cursor < End)
                {
                    if (Count >= RxWarpMaxOperands)
                        return E_INVALIDARG;
                    if (!RxWarpDecodeOperand(Code, End, &Cursor, &Instruction.Operands[Count]))
                        return E_NOTIMPL;
                    Count++;
                }
                Instruction.OperandCount = (UINT8)Count;
                if (Opcode == D3D10_SB_OPCODE_DISCARD)
                    Discards = TRUE;
                Program.push_back(Instruction);
                break;
            }
        }
        Position = End;
    }
    return RxWarpLinkBlocks(this);
}

HRESULT
RxWarpShader::Initialize(
    _In_ const UINT *Code)
{
    std::vector<UINT> Marks;
    std::vector<RxWarpShader> Built;
    UINT Length;
    HRESULT Status;

    if (Code == nullptr)
        return E_INVALIDARG;

    Type = DECODE_D3D10_SB_TOKENIZED_PROGRAM_TYPE(Code[0]);
    Length = DECODE_D3D10_SB_TOKENIZED_PROGRAM_LENGTH(Code[1]);

    /* A hull shader is global declarations followed by phases, each starting at its marker token */
    if (Type == D3D11_SB_HULL_SHADER)
    {
        for (UINT Position = 2; Position < Length;)
        {
            UINT Opcode = DECODE_D3D10_SB_OPCODE_TYPE(Code[Position]);
            UINT Size;

            if (Opcode == D3D10_SB_OPCODE_CUSTOMDATA)
                Size = (Position + 1 < Length) ? Code[Position + 1] : 0;
            else
                Size = DECODE_D3D10_SB_TOKENIZED_INSTRUCTION_LENGTH(Code[Position]);
            if (Size == 0)
                return E_INVALIDARG;
            if (Opcode == D3D11_SB_OPCODE_HS_CONTROL_POINT_PHASE || Opcode == D3D11_SB_OPCODE_HS_FORK_PHASE ||
                Opcode == D3D11_SB_OPCODE_HS_JOIN_PHASE)
            {
                Marks.push_back(Position);
            }
            Position += Size;
        }
    }
    Marks.push_back(Length);

    Status = Decode(Code, 2, Marks[0]);
    if (FAILED(Status))
        return Status;

    for (SIZE_T Index = 0; Index + 1 < Marks.size(); Index++)
    {
        RxWarpShader Phase = *this;

        Phase.PhaseKind = DECODE_D3D10_SB_OPCODE_TYPE(Code[Marks[Index]]);
        Phase.PhaseInstances = 1;
        Phase.TempCount = 0;
        Phase.IndexableRegisters = 0;
        Phase.Indexables.clear();
        Phase.Program.clear();
        Phase.SwitchCases.clear();
        ZeroMemory(Phase.OutputMask, sizeof(Phase.OutputMask));
        ZeroMemory(Phase.OutputName, sizeof(Phase.OutputName));
        ZeroMemory(Phase.ClipMask, sizeof(Phase.ClipMask));
        ZeroMemory(Phase.CullMask, sizeof(Phase.CullMask));
        ZeroMemory(Phase.TessFactorSlot, sizeof(Phase.TessFactorSlot));
        Phase.OutputCount = 0;
        Phase.PositionOutput = UINT_MAX;
        Status = Phase.Decode(Code, Marks[Index] + 1, Marks[Index + 1]);
        if (FAILED(Status))
            return Status;
        for (UINT Slot = 0; Slot < 12; Slot++)
        {
            if (Phase.TessFactorSlot[Slot] != 0)
                TessFactorSlot[Slot] = Phase.TessFactorSlot[Slot];
        }
        Built.push_back(std::move(Phase));
    }
    Phases = std::move(Built);
    return S_OK;
}
