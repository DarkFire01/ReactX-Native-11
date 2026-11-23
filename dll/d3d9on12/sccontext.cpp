/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: operands, the token writer and the translation context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

#include <math.h>
#include <stdarg.h>

VOID
ScAsm::Begin(
    _In_ D3D10_SB_TOKENIZED_PROGRAM_TYPE Type,
    _In_ UINT NewOptions)
{
    Options = NewOptions;
    Emitted = 0;
    Patched = 0;
    StartShader(Type, 5, 0);
}

VOID
ScAsm::Add(
    _In_ const SB::CInstruction& Instruction)
{
    Emitted += 1;
    EmitInstruction(Instruction);
}

VOID
ScEmit(
    _In_ ScAsm* Asm,
    _In_ D3D10_SB_OPCODE_TYPE Opcode,
    _In_ UINT Count,
    ...)
{
    SB::CInstruction Instruction(Opcode);
    va_list Operands;

    va_start(Operands, Count);
    for (UINT Index = 0; Index < Count; Index += 1)
        Instruction.m_Operands[Index] = *va_arg(Operands, const SB::COperandBase*);
    va_end(Operands);
    Instruction.m_NumOperands = Count;
    Asm->Add(Instruction);
}

SB::COperandBase
ScTempOperand(
    _In_ UINT Register)
{
    return SB::COperand4(D3D10_SB_OPERAND_TYPE_TEMP, Register);
}

SB::COperandBase
ScTempDst(
    _In_ UINT Register,
    _In_ UINT Mask)
{
    return SB::COperandDst(D3D10_SB_OPERAND_TYPE_TEMP, Register, Mask);
}

VOID
ScSetSwizzle(
    _Inout_ SB::COperandBase* Operand,
    _In_ UINT Swizzle)
{
    if (!Swizzle)
        return;

    if (Operand->m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        UINT Values[4];

        for (UINT Component = 0; Component < 4; Component += 1)
            Values[Component] = Operand->m_Value[(Swizzle >> (2 * Component + 1)) & 3];
        memcpy(Operand->m_Value, Values, sizeof(Values));
        return;
    }

    Operand->m_ComponentSelection = D3D10_SB_OPERAND_4_COMPONENT_SWIZZLE_MODE;
    for (UINT Component = 0; Component < 4; Component += 1)
        Operand->m_Swizzle[Component] = static_cast<BYTE>((Swizzle >> (2 * Component + 1)) & 3);
}

SB::COperandBase
ScSwizzled(
    _In_ const SB::COperandBase& Operand,
    _In_ UINT Swizzle)
{
    SB::COperandBase Result(Operand);

    ScSetSwizzle(&Result, Swizzle);
    return Result;
}

SB::COperandBase
ScNegated(
    _In_ const SB::COperandBase& Operand)
{
    SB::COperandBase Result(Operand);

    if (Result.m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        for (UINT Component = 0; Component < 4; Component += 1)
            Result.m_Valuef[Component] = -Result.m_Valuef[Component];
        return Result;
    }

    switch (Result.m_Modifier)
    {
        case D3D10_SB_OPERAND_MODIFIER_NONE:
            Result.m_Modifier = D3D10_SB_OPERAND_MODIFIER_NEG;
            break;
        case D3D10_SB_OPERAND_MODIFIER_NEG:
            Result.m_Modifier = D3D10_SB_OPERAND_MODIFIER_NONE;
            return Result;
        case D3D10_SB_OPERAND_MODIFIER_ABS:
            Result.m_Modifier = D3D10_SB_OPERAND_MODIFIER_ABSNEG;
            break;
        case D3D10_SB_OPERAND_MODIFIER_ABSNEG:
            Result.m_Modifier = D3D10_SB_OPERAND_MODIFIER_ABS;
            break;
        default:
            return Result;
    }
    Result.m_bExtendedOperand = TRUE;
    Result.m_ExtendedOperandType = D3D10_SB_EXTENDED_OPERAND_MODIFIER;
    return Result;
}

SB::COperandBase
ScAbsolute(
    _In_ const SB::COperandBase& Operand)
{
    SB::COperandBase Result(Operand);

    if (Result.m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        for (UINT Component = 0; Component < 4; Component += 1)
            Result.m_Valuef[Component] = fabsf(Result.m_Valuef[Component]);
        return Result;
    }

    if (Result.m_Modifier == D3D10_SB_OPERAND_MODIFIER_NONE || Result.m_Modifier == D3D10_SB_OPERAND_MODIFIER_NEG ||
        Result.m_Modifier == D3D10_SB_OPERAND_MODIFIER_ABSNEG)
    {
        Result.m_ExtendedOperandType = D3D10_SB_EXTENDED_OPERAND_MODIFIER;
        Result.m_bExtendedOperand = TRUE;
        Result.m_Modifier = D3D10_SB_OPERAND_MODIFIER_ABS;
    }
    return Result;
}

SB::COperandBase
ScComponent(
    _In_ const SB::COperandBase& Operand,
    _In_ UINT Component)
{
    SB::COperandBase Result(Operand);

    if (Result.m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        Result.m_NumComponents = D3D10_SB_OPERAND_1_COMPONENT;
        Result.m_Value[0] = Result.m_Value[Component];
    }
    else
    {
        Result.m_NumComponents = D3D10_SB_OPERAND_4_COMPONENT;
    }
    Result.m_ComponentSelection = D3D10_SB_OPERAND_4_COMPONENT_SELECT_1_MODE;
    Result.m_ComponentName = static_cast<D3D10_SB_4_COMPONENT_NAME>(Component);
    return Result;
}

UINT
ScMaskSwizzle(
    _In_ UINT Swizzle,
    _In_ UINT Mask)
{
    UINT Result = 0;

    for (UINT Component = 0; Component < 4; Component += 1)
    {
        UINT Source = (Swizzle >> (2 * Component + 1)) & 3;

        if ((1u << (Source | 4)) & Mask & ScMaskAll)
            Result |= 1 | (Source << (2 * Component + 1));
    }
    return Result;
}

UINT
ScSwizzleFromMask(
    _In_ UINT Mask)
{
    UINT Result = 1;
    UINT Slot = 0;
    UINT Last = 0;

    /* The components the mask names, in order, then the last one named again */
    for (UINT Component = 0; Component < 4; Component += 1)
    {
        if (Mask & (ScMaskX << Component))
        {
            Result |= Component << (2 * Slot + 1);
            Last = Component;
            Slot += 1;
        }
    }
    for (; Slot < 4; Slot += 1)
        Result |= Last << (2 * Slot + 1);
    return Result;
}

ScInstr::ScInstr(
    _In_ DWORD Token,
    _In_ ScContext* Owner)
{
    Dst.Token = 0;
    Dst.Address = 0;
    Flags = 0;
    ZeroMemory(Src, sizeof(Src));
    Version = Owner->Version;
    Map = Owner->Map;
    OutputTemps = Owner->OutputTemps;
    if (Token & 0x40000000)
        Flags |= ScFlagCoissue;
    if (Token & 0x10000000)
        Flags |= ScFlagPredicated;
    Opcode = Token & 0xFFFF;
    OpcodeToken = Token;
    Length = ScInstrLength(Token, Version);
    SrcCount = 0;
    Context = Owner;
}

const DWORD*
ScInstr::SetDst(
    _In_ const DWORD* Tokens)
{
    DWORD Token = Tokens[0];
    DWORD Address = 0;
    const DWORD* Next = Tokens + 1;

    if (Token & 0x2000)
    {
        if (((Version & 0xFFFF0000) == 0xFFFE0000 && Version >= 0xFFFE0200) ||
            ((Version & 0xFFFF0000) == 0xFFFF0000 && Version >= 0xFFFF0300))
        {
            Address = Tokens[1];
            Next = Tokens + 2;
        }
    }

    if (ScIsPixel1x(Version))
        Flags = (Flags & ~ScFlagShiftMask) | ((Token >> 24) & ScFlagShiftMask);

    Flags &= ~ScFlagSaturate;
    if (Token & 0x100000)
        Flags |= ScFlagSaturate;
    if (Token & 0x200000)
        Flags |= ScFlagPartial;
    if (Token & 0x400000)
        Flags |= ScFlagCentroid;

    if (ScIsPixel1x(Version) && ScRegType(Token) == ScRegTexture)
    {
        UINT Number = ScRegNumber(Token);

        if (Opcode != 71 && Opcode != 73)
            Token = (Token & 0xCFFFF7FF) | 0x40001000;

        switch (Opcode)
        {
            case 64:
            case 66:
            case 67:
            case 68:
            case 71:
            case 72:
            case 73:
            case 74:
            case 76:
            case 77:
            case 83:
            case 84:
            case 85:
            case 86:
            {
                /* The texture coordinate is an implicit source, projected when the state asks for it */
                DWORD Implicit = (Context->Raster->Words[1] & (1u << Number) & 0xFF) ? 0xBAE40000 : 0xB0E40000;

                Src[SrcCount].Token = Implicit | Number;
                Src[SrcCount].Address = 0;
                SrcCount += 1;
                break;
            }
        }
    }

    Dst.Token = Token;
    Dst.Address = Address;
    return Next;
}

const DWORD*
ScInstr::AddSrc(
    _In_ const DWORD* Tokens)
{
    DWORD Token = Tokens[0];
    DWORD Address = 0;
    const DWORD* Next = Tokens + 1;

    if (Token & 0x2000)
    {
        if (((Version & 0xFFFF0000) == 0xFFFE0000 && Version >= 0xFFFE0200) ||
            ((Version & 0xFFFF0000) == 0xFFFF0000 && Version >= 0xFFFF0300))
        {
            Address = Tokens[1];
            Next = Tokens + 2;
        }
    }

    /* Below ps_1_4 texture registers read as sources are temps, except by texkill */
    if ((Version & 0xFFFF0000) == 0xFFFF0000 && Version <= 0xFFFF0103 && Opcode != 65 &&
        ScRegType(Token) == ScRegTexture)
    {
        Token = (Token & 0xCFFFF7FF) | 0x40001000;
    }

    if (SrcCount < 5)
    {
        Src[SrcCount].Token = Token;
        Src[SrcCount].Address = Address;
        SrcCount += 1;
    }
    return Next;
}

VOID
ScInstr::Relative(
    _Inout_ SB::COperandBase* Operand,
    _In_ DWORD Address,
    _In_ UINT Dimension) const
{
    UINT Register;
    UINT Component;

    if (!Address)
    {
        Register = Map->Special[ScSlotAddress];
        Component = 0;
    }
    else
    {
        Register = Map->Special[ScRegType(Address) == ScRegAddress ? ScSlotAddress : ScSlotLoop];
        switch (Address & 0x30000)
        {
            case 0:
                Component = 0;
                break;
            case 0x10000:
                Component = 1;
                break;
            case 0x20000:
                Component = 2;
                break;
            default:
                Component = 3;
                break;
        }
    }

    Operand->m_IndexType[Dimension] = D3D10_SB_OPERAND_INDEX_IMMEDIATE32_PLUS_RELATIVE;
    Operand->m_Index[Dimension].m_RelRegType = D3D10_SB_OPERAND_TYPE_TEMP;
    Operand->m_Index[Dimension].m_RelIndex = Register;
    Operand->m_Index[Dimension].m_IndexDimension = D3D10_SB_OPERAND_INDEX_1D;
    Operand->m_Index[Dimension].m_ComponentName = static_cast<D3D10_SB_4_COMPONENT_NAME>(Component);
}

VOID
ScInstr::Destination(
    _Out_ SB::COperandBase* Operand) const
{
    UINT Number = ScRegNumber(Dst.Token);
    UINT Mask = ScTokenWriteMask(Dst.Token);
    UINT Temp;

    if (Flags & ScFlagPredicated)
    {
        *Operand = ScTempDst(ScPredicateTemp, Mask);
        return;
    }

    switch (ScRegType(Dst.Token))
    {
        case ScRegTemp:
            Temp = Map->Temps[Number];
            break;
        case ScRegAddress:
            Temp = Map->Special[ScSlotAddress];
            break;
        case ScRegRasterOut:
            if (Number == 0)
            {
                Temp = OutputTemps[0];
            }
            else
            {
                if (Number == 1)
                    Mask = ScMaskX;
                else if (Number == 2)
                    Mask = ScMaskY;
                else
                    goto Indexed;
                Temp = OutputTemps[11];
            }
            break;
        case ScRegAttributeOut:
            Temp = OutputTemps[Number + 1];
            break;
        case ScRegOutput:
            Temp = OutputTemps[Version < 0xFFFE0300 ? Number + 3 : Number];
            break;
        case ScRegColorOut:
            Temp = OutputTemps[Number];
            break;
        case ScRegDepthOut:
            Temp = OutputTemps[4];
            break;
        case ScRegPredicate:
            Temp = Map->Special[ScSlotPredicate];
            break;
        case ScRegTextureTemp:
            Temp = Map->Special[Number];
            break;
        default:
            Temp = Map->Temps[Number];
            break;
    }

    if (Temp != ScNoRegister)
    {
        *Operand = ScTempDst(Temp, Mask);
        return;
    }

Indexed:
    /* Outputs without a temp live in the indexable temp, indexed by register */
    *Operand = SB::COperandDst(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0, Number, Mask);
    if (Dst.Token & 0x2000)
        Relative(Operand, Dst.Address, 1);
}

VOID
ScInstr::Source(
    _Out_ SB::COperandBase* Operand,
    _In_ DWORD Token,
    _In_ UINT Offset) const
{
    UINT Number = ScRegNumber(Token) + Offset;
    BOOL Direct = !(Token & 0x2000) && Context->RuntimeVersion >= 9;
    const ScShader* Shader = Context->Shader;

    switch (ScRegType(Token))
    {
        case ScRegTemp:
            *Operand = ScTempOperand(Map->Temps[Number]);
            return;

        case ScRegInput:
            Context->InputOperand(Operand, ScInputIndex(Version, Number), 0);
            return;

        case ScRegConst:
            if (Direct)
            {
                for (const NineScConstant& Constant : Shader->Constants[NineScFloatBuffer])
                {
                    if (Constant.Offset == 4 * Number)
                    {
                        float Values[4];

                        memcpy(Values, Constant.Value, sizeof(Values));
                        *Operand = SB::COperand(Values[0], Values[1], Values[2], Values[3]);
                        return;
                    }
                }
            }
            *Operand = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 0, Number);
            return;

        case ScRegTexture:
            Context->InputOperand(Operand, Number + 3, 0);
            return;

        case ScRegConstInt:
            if (Direct)
            {
                for (const NineScConstant& Constant : Shader->Constants[NineScIntegerBuffer])
                {
                    if (Constant.Offset == 4 * Number)
                    {
                        *Operand = SB::COperand(static_cast<int>(Constant.Value[0]), static_cast<int>(Constant.Value[1]),
                                                static_cast<int>(Constant.Value[2]), static_cast<int>(Constant.Value[3]));
                        return;
                    }
                }
            }
            *Operand = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 1, Number);
            return;

        case ScRegSampler:
            *Operand = SB::COperand(D3D10_SB_OPERAND_TYPE_SAMPLER, Number);
            return;

        case ScRegConstBool:
            if (Direct)
            {
                for (const NineScConstant& Constant : Shader->Constants[NineScBooleanBuffer])
                {
                    if (Constant.Offset == Number)
                    {
                        *Operand = SB::COperand(static_cast<int>(Constant.Value[0]));
                        return;
                    }
                }
            }
            *Operand = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 2, Number >> 2),
                                  ScSwz(Number & 3, Number & 3, Number & 3, Number & 3));
            return;

        case ScRegLoop:
            *Operand = ScTempOperand(Map->Special[ScSlotLoop]);
            return;

        case ScRegMisc:
            *Operand = ScTempOperand(Map->Inputs[Number ? 14 : 13].Register);
            return;

        case ScRegLabel:
            *Operand = SB::COperand(D3D10_SB_OPERAND_TYPE_LABEL, Number);
            return;

        case ScRegPredicate:
            *Operand = ScTempOperand(Map->Special[ScSlotPredicate]);
            return;

        case ScRegTextureTemp:
            *Operand = ScTempOperand(Map->Special[Number]);
            return;

        default:
            __fastfail(FAST_FAIL_INVALID_ARG);
    }
}

ScContext::ScContext(
    _In_ UINT Runtime,
    _In_ ScAsm* Writer,
    _In_ const NineScRasterStates* States,
    _In_ ScShader* Info) :
    RuntimeVersion(Runtime),
    Asm(Writer),
    Version(Info->Version),
    Raster(States),
    Map(&Info->Map),
    OutputTemps(Info->OutputTemps),
    Shader(Info),
    IfDepth(0),
    OutputsWritten(FALSE),
    TempBase(0),
    LastWasBreak(FALSE)
{
    ZeroMemory(InputTemps, sizeof(InputTemps));
}

VOID
ScContext::InputOperand(
    _Out_ SB::COperandBase* Operand,
    _In_ UINT Register,
    _In_ UINT Swizzle) const
{
    const ScInputRegister* Entry = &Map->Inputs[Register];

    if (Entry->Kind == ScInputTemp)
        *Operand = ScTempOperand(Entry->Register);
    else if (Entry->Kind == ScInputDirect)
        *Operand = SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, Entry->Register);
    else
        *Operand = ScTempOperand(InputTemps[Register]);
    ScSetSwizzle(Operand, Swizzle);
}

VOID
ScContext::Immediate(
    _Out_ SB::COperandBase* Operand,
    _Inout_ SB::COperandBase* Value,
    _In_ UINT Modifier,
    _In_ UINT Swizzle) const
{
    float* Values = Value->m_Valuef;
    BOOL Negate = FALSE;

    if (ScIsPixel1x(Version))
    {
        for (UINT Component = 0; Component < 4; Component += 1)
        {
            float Clamped = -1.0f;

            if (Values[Component] > -1.0f)
                Clamped = Values[Component];
            if (Clamped >= 1.0f)
                Clamped = 1.0f;
            Values[Component] = Clamped;
        }
    }

    switch (Modifier >> 24)
    {
        case 1:
            Negate = TRUE;
            break;
        case 2:
        case 3:
            for (UINT Component = 0; Component < 4; Component += 1)
                Values[Component] = Values[Component] - 0.5f;
            Negate = (Modifier >> 24) == 3;
            break;
        case 4:
        case 5:
            for (UINT Component = 0; Component < 4; Component += 1)
                Values[Component] = (Values[Component] + Values[Component]) - 1.0f;
            Negate = (Modifier >> 24) == 5;
            break;
        case 6:
            for (UINT Component = 0; Component < 4; Component += 1)
                Values[Component] = 1.0f - Values[Component];
            break;
        case 7:
        case 8:
            for (UINT Component = 0; Component < 4; Component += 1)
                Values[Component] = Values[Component] + Values[Component];
            Negate = (Modifier >> 24) == 8;
            break;
        case 9:
        case 10:
        {
            float Divisor = Values[(Modifier >> 24) == 9 ? 2 : 3];
            float Y = Values[1] / Divisor;

            Values[0] = Values[0] / Divisor;
            Values[1] = Y;
            break;
        }
        case 11:
        case 12:
            for (UINT Component = 0; Component < 4; Component += 1)
                Values[Component] = fabsf(Values[Component]);
            Negate = (Modifier >> 24) == 12;
            break;
    }

    if (Negate)
    {
        for (UINT Component = 0; Component < 4; Component += 1)
            Values[Component] = -Values[Component];
    }

    ScSetSwizzle(Value, Swizzle);
    *Operand = *Value;
}

VOID
ScContext::SourceOperand(
    _Out_ SB::COperandBase* Operand,
    _In_ const ScInstr& Instr,
    _In_ UINT Index,
    _In_ UINT Offset,
    _In_ UINT Components)
{
    DWORD Token = Instr.Src[Index].Token;
    UINT Type = ScRegType(Token);
    UINT Swizzle = ScTokenSwizzle(Token, Components);
    UINT Modifier;
    UINT Scratch = Index + 5;
    SB::COperandBase Value;

    Instr.Source(&Value, Token, Offset);
    if (Type == ScRegConstBool)
    {
        *Operand = Value;
        return;
    }

    Modifier = Token & 0xF000000;
    if (Value.m_Type == D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        Immediate(Operand, &Value, Modifier, Swizzle);
        return;
    }

    if (Token & 0x2000)
        Instr.Relative(&Value, Instr.Src[Index].Address, Type != ScRegInput);

    /* ps_1_x constants are clamped to [-1, 1] */
    if (ScIsPixel1x(Version) && Type == ScRegConst)
    {
        SB::COperand Low(-1.0f);
        SB::COperand High(1.0f);
        SB::COperandBase Clamp = ScTempDst(2, ScMaskAll);
        SB::COperandBase Clamped = ScTempOperand(2);
        SB::COperandBase Result = ScTempDst(Scratch, ScMaskAll);

        ScEmit(Asm, D3D10_SB_OPCODE_MAX, 3, &Clamp, &Value, &Low);
        ScEmit(Asm, D3D10_SB_OPCODE_MIN, 3, &Result, &Clamped, &High);
        Value = ScTempOperand(Scratch);
    }

    switch (Modifier >> 24)
    {
        case 6:
        {
            SB::COperand One(1.0f);
            SB::COperandBase Negative = ScNegated(Value);
            SB::COperandBase Result = ScTempDst(Scratch, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Result, &One, &Negative);
            Value = ScTempOperand(Scratch);
            goto Swizzle;
        }

        case 2:
        case 3:
        {
            SB::COperand Half(-0.5f);
            SB::COperandBase Result = ScTempDst(Scratch, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Result, &Value, &Half);
            Value = ScTempOperand(Scratch);
            break;
        }

        case 4:
        case 5:
        {
            SB::COperand Two(2.0f);
            SB::COperand MinusOne(-1.0f);
            SB::COperandBase Result = ScTempDst(Scratch, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Result, &Value, &Two, &MinusOne);
            Value = ScTempOperand(Scratch);
            break;
        }

        case 7:
        case 8:
        {
            SB::COperand Two(2.0f);
            SB::COperandBase Result = ScTempDst(Scratch, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &Result, &Value, &Two);
            Value = ScTempOperand(Scratch);
            break;
        }

        case 9:
        case 10:
        {
            SB::COperandBase Copy = ScTempDst(2, ScMaskAll);
            SB::COperandBase Result;
            SB::COperandBase Numerator = ScTempOperand(2);
            SB::COperandBase Divisor = ScTempOperand(2);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Copy, &Value);
            if ((Modifier >> 24) == 9)
            {
                ScSetSwizzle(&Divisor, ScSwzZZZZ);
                ScSetSwizzle(&Numerator, ScSwzXYYY);
                Result = ScTempDst(Scratch, ScMaskX | ScMaskY);
            }
            else
            {
                ScSetSwizzle(&Divisor, ScSwzWWWW);
                ScSetSwizzle(&Numerator, ScSwzXYZZ);
                Result = ScTempDst(Scratch, ScMaskX | ScMaskY | ScMaskZ);
            }
            ScEmit(Asm, D3D10_SB_OPCODE_DIV, 3, &Result, &Numerator, &Divisor);
            Value = ScTempOperand(Scratch);
            break;
        }
    }

    switch (Modifier >> 24)
    {
        case 1:
        case 3:
        case 5:
        case 8:
            Value.m_bExtendedOperand = TRUE;
            Value.m_Modifier = D3D10_SB_OPERAND_MODIFIER_NEG;
            Value.m_ExtendedOperandType = D3D10_SB_EXTENDED_OPERAND_MODIFIER;
            break;
        case 11:
            Value.m_bExtendedOperand = TRUE;
            Value.m_Modifier = D3D10_SB_OPERAND_MODIFIER_ABS;
            Value.m_ExtendedOperandType = D3D10_SB_EXTENDED_OPERAND_MODIFIER;
            break;
        case 12:
            Value.m_bExtendedOperand = TRUE;
            Value.m_Modifier = D3D10_SB_OPERAND_MODIFIER_ABSNEG;
            Value.m_ExtendedOperandType = D3D10_SB_EXTENDED_OPERAND_MODIFIER;
            break;
        default:
            Value.m_bExtendedOperand = FALSE;
            break;
    }

Swizzle:
    if (Type == ScRegInput || Type == ScRegTexture)
    {
        UINT Number = ScRegNumber(Token) + Offset;
        UINT Register = Type == ScRegInput ? ScInputIndex(Version, Number) : Number + 3;
        const ScInputRegister* Entry = &Map->Inputs[Register];

        if (Entry->Register != ScNoRegister)
        {
            UINT Mask = Entry->Mask << 4;

            Swizzle = Swizzle ? ScMaskSwizzle(Swizzle, Mask) : ScSwizzleFromMask(Mask);
        }
    }
    ScSetSwizzle(&Value, Swizzle);
    *Operand = Value;
}

VOID
ScContext::EmitShift(
    _In_ UINT Shift,
    _In_ BOOL Saturate,
    _In_ const SB::COperandBase& Dst,
    _In_ const SB::COperandBase& Value)
{
    float Scale;

    switch (Shift)
    {
        case 1:
            Scale = 2.0f;
            break;
        case 2:
            Scale = 4.0f;
            break;
        case 3:
            Scale = 8.0f;
            break;
        case 13:
            Scale = 0.125f;
            break;
        case 14:
            Scale = 0.25f;
            break;
        default:
            Scale = 0.5f;
            break;
    }

    SB::COperand Factor(Scale);
    SB::CInstruction Instruction(D3D10_SB_OPCODE_MUL, const_cast<SB::COperandBase&>(Dst),
                                 const_cast<SB::COperandBase&>(Value), Factor);

    Instruction.m_bSaturate = Saturate;
    Asm->Add(Instruction);
}

/* Replicates channels the way luminance and alpha only formats read in Direct3D 9 */
VOID
ScContext::EmitSamplerSwizzle(
    _In_ const SB::COperandBase& Dst,
    _In_ const SB::COperandBase& Value,
    _In_ UINT Mode)
{
    SB::COperandBase Swizzled(Value);
    BYTE Fill;

    switch (Mode)
    {
        case 1:
            Fill = Swizzled.m_Swizzle[0];
            Swizzled.m_Swizzle[1] = Fill;
            Swizzled.m_Swizzle[2] = Fill;
            break;
        case 2:
            Fill = Swizzled.m_Swizzle[3];
            Swizzled.m_Swizzle[1] = Fill;
            Swizzled.m_Swizzle[2] = Fill;
            break;
        case 3:
            Swizzled.m_Swizzle[2] = Swizzled.m_Swizzle[3];
            break;
    }
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Swizzled);
}

/* Color keying: texels equal to the key become transparent black, or are discarded */
VOID
ScContext::EmitColorKey(
    _In_ UINT Sampler,
    _In_ const SB::COperandBase& Dst,
    _In_ const SB::COperandBase& Value)
{
    if ((1u << Sampler) & (Raster->Words[2] >> 2) & 0xFF)
    {
        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
        return;
    }

    SB::COperandBase Key = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 5, Sampler);
    SB::COperandBase Match = ScTempDst(5, ScMaskAll);
    SB::COperandBase Combined = ScTempDst(5, ScMaskX);
    SB::COperandBase X = ScSwizzled(ScTempOperand(5), ScSwz(0, 0, 0, 0));

    ScEmit(Asm, D3D10_SB_OPCODE_EQ, 3, &Match, &Value, &Key);
    for (UINT Component = 1; Component < 4; Component += 1)
    {
        SB::COperandBase Other = ScSwizzled(ScTempOperand(5), ScSwz(Component, Component, Component, Component));

        ScEmit(Asm, D3D10_SB_OPCODE_AND, 3, &Combined, &X, &Other);
    }

    if (Raster->Words[2] & 1)
    {
        SB::COperandBase Test = SB::COperand4(D3D10_SB_OPERAND_TYPE_TEMP, 5, D3D10_SB_4_COMPONENT_X);
        SB::CInstruction Discard(D3D10_SB_OPCODE_DISCARD, Test, D3D10_SB_INSTRUCTION_TEST_NONZERO);

        Asm->Add(Discard);
        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
    }
    else
    {
        SB::COperand Zero(0.0f);

        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Dst, &X, &Zero, &Value);
    }
}

VOID
ScContext::EmitResult(
    _In_ UINT Flags,
    _In_ D3D10_SB_OPCODE_TYPE Opcode,
    _In_ UINT Sampler,
    _In_ const SB::COperandBase& Dst,
    _In_ UINT SourceCount,
    _In_reads_(SourceCount) const SB::COperandBase* const* Sources)
{
    SB::CInstruction Instruction(Opcode);
    BOOL Sample = Opcode == D3D10_SB_OPCODE_SAMPLE || Opcode == D3D10_SB_OPCODE_SAMPLE_L ||
                  Opcode == D3D10_SB_OPCODE_SAMPLE_D || Opcode == D3D10_SB_OPCODE_SAMPLE_B;
    UINT SwizzleMode = (Raster->Words[4] >> (2 * Sampler)) & 3;
    BOOL Swizzle = Sample && SwizzleMode != 0;

    for (UINT Index = 0; Index < SourceCount; Index += 1)
        Instruction.m_Operands[Index + 1] = *Sources[Index];
    Instruction.m_NumOperands = SourceCount + 1;

    if ((Flags & ScFlagShiftMask) || Swizzle || (Raster->Words[2] & 3))
    {
        SB::COperandBase Scratch = ScTempDst(0, ScMaskAll);
        SB::COperandBase Result = ScTempOperand(0);

        Instruction.m_Operands[0] = Scratch;
        Asm->Add(Instruction);
        if (Swizzle)
            EmitSamplerSwizzle(Scratch, Result, SwizzleMode);
        if (Raster->Words[2] & 3)
            EmitColorKey(Sampler, Scratch, Result);

        if (Flags & ScFlagShiftMask)
        {
            EmitShift(Flags & ScFlagShiftMask, (Flags & ScFlagSaturate) != 0, Dst, Result);
        }
        else
        {
            SB::CInstruction Move(D3D10_SB_OPCODE_MOV, const_cast<SB::COperandBase&>(Dst), Result);

            Move.m_bSaturate = (Flags & ScFlagSaturate) != 0;
            Asm->Add(Move);
        }
        return;
    }

    Instruction.m_Operands[0] = Dst;
    if (!SourceCount)
        Instruction.m_NumOperands = 1;
    Instruction.m_bSaturate = (Flags & ScFlagSaturate) != 0;
    Asm->Add(Instruction);
}

template <class INPUTS>
VOID
ScContext::AllocateInputTemps(
    _In_ const INPUTS& Inputs,
    _Inout_ BYTE* Next)
{
    for (const NineScInput& Input : Inputs.Inputs)
    {
        UINT Register = NineScInputRegister(Input);

        if (Map->Inputs[Register].Kind == ScInputUnbound)
        {
            InputTemps[Register] = *Next;
            *Next += 1;
        }
    }
}

VOID
ScContext::InitializeVertexInputTemps(
    _In_ const NineScInputs& Inputs,
    _In_ UINT TempCount)
{
    std::vector<bool> Written(TempCount, false);

    for (const NineScInput& Input : Inputs.Inputs)
    {
        const ScInputRegister* Entry = &Map->Inputs[NineScInputRegister(Input)];

        if (Entry->Kind == ScInputUnbound)
        {
            /* An input the vertex declaration does not feed reads as Direct3D 9 defaults */
            UINT Register = InputTemps[NineScInputRegister(Input)];
            SB::COperandBase Default;
            SB::COperandBase Dst = ScTempDst(Register, ScMaskAll);
            UINT Usage = NineScInputUsage(Input);

            if (Usage == 4)
                Default = SB::COperand(1.0f);
            else if (Usage == 5 || Usage == 10)
                Default = SB::COperand(0.0f, 0.0f, 0.0f, 1.0f);
            else
                Default = SB::COperand(0.0f);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Default);
            Written[Register] = true;
        }
        else if (Entry->Kind == ScInputTemp)
        {
            Written[Entry->Register] = true;
        }
    }

    for (UINT Register = 0; Register < TempCount; Register += 1)
    {
        if (!Written[Register])
        {
            SB::COperand Zero(0.0f);
            SB::COperandBase Dst = ScTempDst(Register, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Zero);
            Written[Register] = true;
        }
    }
}

ScVertexContext::ScVertexContext(
    _In_ UINT Runtime,
    _In_ ScAsm* Writer,
    _In_ const NineScRasterStates* States,
    _In_ ScShader* Info) :
    ScContext(Runtime, Writer, States, Info),
    Inputs(&Info->Inputs),
    Outputs(&Info->Outputs),
    VertexFlags(Info->VertexFlags)
{
}

static
D3D10_SB_RESOURCE_DIMENSION
ScResourceDimension(
    _In_ UINT Kind)
{
    switch (Kind)
    {
        case 2:
            return D3D10_SB_RESOURCE_DIMENSION_TEXTURECUBE;
        case 3:
            return D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D;
        default:
            return D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D;
    }
}

HRESULT
ScVertexContext::WriteDeclarations()
{
    BYTE Next;
    UINT TempCount;
    UINT FloatSize = Shader->RangeEnd[NineScFloatBuffer];
    UINT IntegerSize = Shader->RangeEnd[NineScIntegerBuffer];
    UINT BooleanSize = Shader->RangeEnd[NineScBooleanBuffer];

    TempBase = Shader->TempCount;
    Next = static_cast<BYTE>(Shader->LoopCount + Shader->TempCount);
    AllocateInputTemps(*Inputs, &Next);
    TempCount = Next;

    Asm->EmitTempsDecl(TempCount);
    if (VertexFlags & ScVertexIndexedOutputs)
        Asm->EmitIndexableTempDecl(0, 12, 4);

    if (FloatSize)
    {
        if (Shader->Relative[NineScFloatBuffer])
        {
            Asm->EmitConstantBufferDecl(0, 246 - (BooleanSize >> 2) - (IntegerSize >> 2),
                                        D3D10_SB_CONSTANT_BUFFER_DYNAMIC_INDEXED);
        }
        else
        {
            Asm->EmitConstantBufferDecl(0, FloatSize >> 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
        }
    }
    if (IntegerSize)
        Asm->EmitConstantBufferDecl(1, IntegerSize >> 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    if (BooleanSize)
        Asm->EmitConstantBufferDecl(2, (BooleanSize + 3) >> 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    Asm->EmitConstantBufferDecl(3, 10, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);

    for (UINT Sampler = 0; Sampler < 4; Sampler += 1)
    {
        UINT Kind = Map->SamplerKinds[Sampler];

        if (Kind == ScNoRegister)
            continue;
        Asm->EmitSamplerDecl(Sampler, ((1u << Sampler) & (Raster->Words[2] >> 27) & 0xF)
                                          ? D3D10_SB_SAMPLER_MODE_COMPARISON
                                          : D3D10_SB_SAMPLER_MODE_DEFAULT);
        Asm->EmitResourceDecl(ScResourceDimension(Kind), Sampler, D3D10_SB_RETURN_TYPE_FLOAT,
                              D3D10_SB_RETURN_TYPE_FLOAT, D3D10_SB_RETURN_TYPE_FLOAT, D3D10_SB_RETURN_TYPE_FLOAT);
    }

    for (const NineScInput& Input : Inputs->Inputs)
    {
        UINT Register = NineScInputRegister(Input);

        if (Map->Inputs[Register].Mask)
            Asm->EmitInputDecl(D3D10_SB_OPERAND_TYPE_INPUT, Register, ScMaskAll);
    }

    for (UINT Slot = 0; Slot < Outputs->Count; Slot += 1)
    {
        NineScOutput Output = Outputs->Outputs[Slot];
        UINT Usage = Output & 0x1F;
        UINT Register = (Output >> 9) & 0xF;
        UINT Mask = ((Output >> 13) & 0xF) << 4;

        if (Usage == 0 && !(Output & 0x1E0))
            Asm->EmitOutputSystemInterpretedValueDecl(Register, Mask, D3D10_SB_NAME_POSITION);
        else if (Usage == 16)
            Asm->EmitOutputSystemInterpretedValueDecl(Register, Mask, D3D10_SB_NAME_CLIP_DISTANCE);
        else
            Asm->EmitOutputDecl(Register, Mask);
    }

    InitializeVertexInputTemps(*Inputs, TempCount);

    for (const NineScInput& Input : Inputs->Inputs)
    {
        UINT Register = NineScInputRegister(Input);
        UINT Conversion = NineScInputConversion(Input);
        UINT Temp = Map->Inputs[Register].Register;
        SB::COperandBase Source = SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, Register);

        if (Conversion == 1 || Conversion == 2)
        {
            /* Integer elements become floats; the second kind also gets w = 1 */
            SB::COperandBase Dst = ScTempDst(Temp, ScMaskAll);

            ScEmit(Asm, D3D10_SB_OPCODE_ITOF, 2, &Dst, &Source);
            if (Conversion == 2)
            {
                SB::COperand One(1.0f);
                SB::COperandBase W = ScTempDst(Temp, ScMaskW);

                ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &W, &One);
            }
        }
        else if (Conversion == 3)
        {
            /* Ten bit signed normalized elements */
            SB::COperand Offsets(0, 0, 0, 0);
            SB::COperand Widths(10, 10, 10, 0);
            SB::COperand Scale(511.0f, 511.0f, 511.0f, 1.0f);
            SB::COperand Floor(-1.0f, -1.0f, -1.0f, 1.0f);
            SB::COperandBase Bits = ScTempDst(0, ScMaskAll);
            SB::COperandBase Floats = ScTempDst(1, ScMaskAll);
            SB::COperandBase Scaled = ScTempDst(2, ScMaskAll);
            SB::COperandBase Dst = ScTempDst(Temp, ScMaskAll);
            SB::COperandBase Read0 = ScTempOperand(0);
            SB::COperandBase Read1 = ScTempOperand(1);
            SB::COperandBase Read2 = ScTempOperand(2);

            static_assert(D3D11_SB_OPCODE_IBFE == 139, "signed bit field extract");
            ScEmit(Asm, D3D11_SB_OPCODE_IBFE, 4, &Bits, &Widths, &Offsets, &Source);
            ScEmit(Asm, D3D10_SB_OPCODE_ITOF, 2, &Floats, &Read0);
            ScEmit(Asm, D3D10_SB_OPCODE_DIV, 3, &Scaled, &Read1, &Scale);
            ScEmit(Asm, D3D10_SB_OPCODE_MAX, 3, &Dst, &Read2, &Floor);
        }
    }
    return S_OK;
}

/* Writes the user clip distances: dp4 of the position with each enabled plane */
VOID
ScContext::WriteClipPlanes(
    _In_ UINT Planes,
    _In_ const SB::COperandBase& Position)
{
    UINT Written = 0;

    for (UINT Plane = 0; Plane < 6; Plane += 1)
    {
        if (!(Planes & (1u << Plane)))
            continue;

        SB::COperandBase Equation = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, Plane + 2);
        SB::COperandBase Dst = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Written < 4 ? 13 : 14,
                                               ScMaskX << (Written & 3));

        ScEmit(Asm, D3D10_SB_OPCODE_DP4, 3, &Dst, &Position, &Equation);
        Written += 1;
    }
}

VOID
ScVertexContext::WriteOutputs()
{
    if (OutputsWritten)
        return;

    for (UINT Slot = 0; Slot < Outputs->Count; Slot += 1)
    {
        NineScOutput Output = Outputs->Outputs[Slot];
        UINT Usage = Output & 0x1F;
        UINT Register = (Output >> 9) & 0xF;
        UINT Mask = ((Output >> 13) & 0xF) << 4;
        UINT Temp = OutputTemps[Register];
        SB::COperandBase Source;
        SB::COperandBase Target;
        SB::COperandBase Dst = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Register, Mask);
        BOOL Saturate = FALSE;

        if (Temp == ScNoRegister)
        {
            Target = SB::COperandDst(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0, Register, ScMaskAll);
            Source = SB::COperand2D(D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP, 0, Register);
        }
        else
        {
            Target = ScTempDst(Temp, ScMaskAll);
            Source = ScTempOperand(Temp);
        }

        if (Usage == 0)
        {
            if (!(Output & 0x1E0))
            {
                /* Position moves by half a pixel and gets the user clip distances */
                SB::COperandBase Offset = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 0);
                SB::COperandBase W = ScSwizzled(Source, ScSwzWWWW);
                SB::COperandBase XY = ScSwizzled(Source, ScSwzXYYY);

                if (Raster->Words[1] & 0x3F00)
                    WriteClipPlanes((Raster->Words[1] >> 8) & 0x3F, Source);

                ScSetSwizzle(&Offset, ScSwzXYYY);
                Target.m_WriteMask = ScMaskX | ScMaskY;
                ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Target, &Offset, &W, &XY);
            }
        }
        else if (Usage == 10)
        {
            Saturate = Version < 0xFFFE0300;
        }
        else if (Usage == 11)
        {
            Saturate = RuntimeVersion < 9;
        }
        else if (Usage == 16)
        {
            continue;
        }

        SB::CInstruction Move(D3D10_SB_OPCODE_MOV, Dst, Source);

        Move.m_bSaturate = Saturate;
        Asm->Add(Move);
    }
    OutputsWritten = TRUE;
}
