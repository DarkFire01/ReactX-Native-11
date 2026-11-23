/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: the instruction walk and the arithmetic instructions
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

#include <float.h>

/* Instructions whose parameters are all sources */
static
BOOL
ScTakesNoDestination(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
        case 25:
        case 26:
        case 27:
        case 30:
        case 38:
        case 40:
        case 41:
        case 44:
        case 45:
        case 65:
        case 96:
            return TRUE;
        default:
            return FALSE;
    }
}

HRESULT
ScContext::TranslateInstructions()
{
    const DWORD* Start = Shader->Code.data();
    SIZE_T Size = Shader->Code.size() * sizeof(DWORD);
    const DWORD* Token = Start + 1;

    while (*Token != 0xFFFF)
    {
        HRESULT hr;

        if (static_cast<SIZE_T>(reinterpret_cast<const BYTE*>(Token) - reinterpret_cast<const BYTE*>(Start)) > Size)
            return E_FAIL;

        ScInstr Instr(*Token, this);

        switch (Instr.Opcode)
        {
            case 0:
            case 0x1F:
            case 0x2F:
            case 0x30:
            case 0x51:
            case 0xFFFE:
                Token += Instr.Length;
                continue;
        }

        Token += 1;
        if (*Token & 0x80000000)
        {
            if (!ScTakesNoDestination(Instr.Opcode))
                Token = Instr.SetDst(Token);
            if (Instr.Flags & ScFlagPredicated)
            {
                Instr.Src[4].Token = *Token;
                Token += 1;
            }
            while (*Token & 0x80000000)
                Token = Instr.AddSrc(Token);
        }

        if (Instr.Opcode > 0xFFFD)
            return E_INVALIDARG;
        if (Instr.Opcode != 0xFFFD)
        {
            hr = Translate(Instr);
            if (FAILED(hr))
                return hr;
        }

        LastWasBreak = Instr.Opcode == 44;

        if (Instr.Flags & ScFlagPredicated)
        {
            SB::COperandBase Dst;
            SB::COperandBase Predicate;
            SB::COperandBase Computed = ScTempOperand(ScPredicateTemp);
            BOOL Inverted = (Instr.Src[4].Token & 0xD000000) != 0;

            /* The instruction wrote the scratch temp; keep it where the predicate is set */
            Instr.Flags &= ~ScFlagPredicated;
            Instr.Destination(&Dst);
            SourceOperand(&Predicate, Instr, 4, 0, 4);
            ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Dst, &Predicate, Inverted ? &Dst : &Computed,
                   Inverted ? &Computed : &Dst);
        }
    }
    return S_OK;
}

/* Emits the instruction with the destination and sources the translation built */
static
VOID
ScResult(
    _In_ ScContext* Context,
    _In_ const ScInstr& Instr,
    _In_ D3D10_SB_OPCODE_TYPE Opcode,
    _In_ const SB::COperandBase& Dst,
    _In_ const SB::COperandBase* Source0,
    _In_opt_ const SB::COperandBase* Source1 = NULL,
    _In_opt_ const SB::COperandBase* Source2 = NULL)
{
    const SB::COperandBase* Sources[3] = { Source0, Source1, Source2 };
    UINT Count = Source2 ? 3 : (Source1 ? 2 : 1);

    Context->EmitResult(Instr.Flags, Opcode, 0, Dst, Count, Sources);
}

VOID
ScContext::TranslateMov(
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Source;

    Instr.Destination(&Dst);
    SourceOperand(&Source, Instr, 0, 0, 4);

    /* Writes to the address register round to the nearest integer first */
    if (ScRegType(Instr.Dst.Token) == ScRegAddress)
    {
        SB::COperandBase Rounded = ScTempDst(0, ScMaskAll);
        SB::COperandBase Read = ScTempOperand(0);

        ScEmit(Asm, D3D10_SB_OPCODE_ROUND_NE, 2, &Rounded, &Source);
        ScResult(this, Instr, D3D10_SB_OPCODE_FTOI, Dst, &Read);
        return;
    }
    ScResult(this, Instr, D3D10_SB_OPCODE_MOV, Dst, &Source);
}

VOID
ScContext::TranslateBinary(
    _In_ ScInstr& Instr,
    _In_ D3D10_SB_OPCODE_TYPE Opcode)
{
    SB::COperandBase Dst;
    SB::COperandBase Left;
    SB::COperandBase Right;
    UINT Components = Opcode == D3D10_SB_OPCODE_DP3 ? 3 : 4;

    Instr.Destination(&Dst);
    SourceOperand(&Left, Instr, 0, 0, Components);
    SourceOperand(&Right, Instr, 1, 0, Components);
    ScResult(this, Instr, Opcode, Dst, &Left, &Right);
}

/* sub, min, max, slt, sge and the single source instructions */
static
VOID
ScTranslateArithmetic(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Left;
    SB::COperandBase Right;
    SB::COperandBase Test = ScTempDst(0, ScMaskAll);
    SB::COperandBase Mask = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Left, Instr, 0, 0, 4);

    switch (Instr.Opcode)
    {
        case 3:
        {
            SB::COperandBase Negative;

            Context->SourceOperand(&Right, Instr, 1, 0, 4);
            Negative = ScNegated(Right);
            ScResult(Context, Instr, D3D10_SB_OPCODE_ADD, Dst, &Left, &Negative);
            break;
        }

        case 4:
        {
            SB::COperandBase Add;

            Context->SourceOperand(&Right, Instr, 1, 0, 4);
            Context->SourceOperand(&Add, Instr, 2, 0, 4);
            ScResult(Context, Instr, D3D10_SB_OPCODE_MAD, Dst, &Left, &Right, &Add);
            break;
        }

        case 6:
        {
            SB::COperand One(1.0f);

            /* rcp of zero keeps the zero */
            ScResult(Context, Instr, D3D10_SB_OPCODE_DIV, Dst, &One, &Left);
            if (!(Instr.Src[0].Token & 0xF000000))
            {
                ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &Left, &Dst, &Left);
            }
            else
            {
                SB::COperandBase Keep = ScTempDst(0, ScMaskZ);
                SB::COperandBase Kept = ScTempOperand(0);

                ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Keep, &Left);
                ScSetSwizzle(&Kept, ScSwzZZZZ);
                ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &Kept, &Dst, &Left);
            }
            break;
        }

        case 7:
        {
            SB::COperandBase Keep = ScTempDst(0, ScMaskZ);
            SB::COperandBase Kept = ScTempOperand(0);
            SB::COperandBase Magnitude = ScAbsolute(Left);
            SB::COperand Largest(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);

            /* rsq of zero is the largest float */
            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Keep, &Magnitude);
            ScResult(Context, Instr, D3D10_SB_OPCODE_RSQ, Dst, &Magnitude);
            ScSetSwizzle(&Kept, ScSwzZZZZ);
            ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &Kept, &Dst, &Largest);
            break;
        }

        case 10:
        case 11:
            Context->SourceOperand(&Right, Instr, 1, 0, 4);
            ScEmit(Asm, Instr.Opcode == 10 ? D3D10_SB_OPCODE_LT : D3D10_SB_OPCODE_GE, 3, &Test, &Left, &Right);
            ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &Mask, &Left, &Right);
            break;

        case 12:
        case 13:
        {
            SB::COperand Zero(0.0f);
            SB::COperand One(1.0f);

            Context->SourceOperand(&Right, Instr, 1, 0, 4);
            ScEmit(Asm, Instr.Opcode == 12 ? D3D10_SB_OPCODE_LT : D3D10_SB_OPCODE_GE, 3, &Test, &Left, &Right);
            ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &Mask, &One, &Zero);
            break;
        }

        case 19:
            ScResult(Context, Instr, D3D10_SB_OPCODE_FRC, Dst, &Left);
            break;

        case 35:
        {
            SB::COperandBase Magnitude = ScAbsolute(Left);

            ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Magnitude);
            break;
        }
    }
}

HRESULT
ScContext::Translate(
    _In_ ScInstr& Instr)
{
    switch (Instr.Opcode)
    {
        case 1:
            TranslateMov(Instr);
            break;
        case 2:
            TranslateBinary(Instr, D3D10_SB_OPCODE_ADD);
            break;
        case 5:
            TranslateBinary(Instr, D3D10_SB_OPCODE_MUL);
            break;
        case 8:
            TranslateBinary(Instr, D3D10_SB_OPCODE_DP3);
            break;
        case 9:
            TranslateBinary(Instr, D3D10_SB_OPCODE_DP4);
            break;
        case 3:
        case 4:
        case 6:
        case 7:
        case 10:
        case 11:
        case 12:
        case 13:
        case 19:
        case 35:
            ScTranslateArithmetic(this, Instr);
            break;
        case 42:
        {
            SB::CInstruction Else(D3D10_SB_OPCODE_ELSE);

            Asm->Add(Else);
            break;
        }
        case 43:
        {
            SB::CInstruction EndIf(D3D10_SB_OPCODE_ENDIF);

            Asm->Add(EndIf);
            IfDepth -= 1;
            break;
        }
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
