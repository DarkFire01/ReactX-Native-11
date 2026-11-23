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

/* A read of scratch temp r0 through a swizzle */
static
SB::COperandBase
ScScratch(
    _In_ UINT Swizzle)
{
    return ScSwizzled(ScTempOperand(0), Swizzle);
}

/* m4x4, m4x3, m3x4, m3x3 and m3x2: one dot product per row into r0, then the result */
static
VOID
ScTranslateMatrix(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ UINT Columns,
    _In_ UINT Rows)
{
    SB::COperandBase Dst;
    SB::COperandBase Vector;
    SB::COperandBase Product = ScTempOperand(0);
    D3D10_SB_OPCODE_TYPE Dot = Columns == 3 ? D3D10_SB_OPCODE_DP3 : D3D10_SB_OPCODE_DP4;

    Instr.Destination(&Dst);
    Context->SourceOperand(&Vector, Instr, 0, 0, 4);
    for (UINT Row = 0; Row < Rows; Row += 1)
    {
        SB::COperandBase Line;
        SB::COperandBase Part = ScTempDst(0, ScMaskX << Row);

        Context->SourceOperand(&Line, Instr, 1, Row, 4);
        ScEmit(Context->Asm, Dot, 3, &Part, &Vector, &Line);
    }

    /* Fewer than four rows write only the components they produced */
    if (Rows < 4)
        Dst.m_WriteMask = static_cast<BYTE>(((1u << Rows) - 1) << 4);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Product);
}

/* lit, built in r0 one component at a time */
static
VOID
ScTranslateLit(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperand Limit(0x1.FFFC02p6f);
    SB::COperand Floor(-0x1.FFFC02p6f);
    SB::COperand Zero(0.0f);
    SB::COperand Lowest(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    SB::COperand Ends(1.0f, 0.0f, 0.0f, 1.0f);
    SB::COperandBase PowerX = ScTempDst(0, ScMaskX);
    SB::COperandBase BaseYZ = ScTempDst(0, ScMaskY | ScMaskZ);
    SB::COperandBase LogW = ScTempDst(0, ScMaskW);
    SB::COperandBase WorkZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase EndsXW = ScTempDst(0, ScMaskX | ScMaskW);
    SB::COperandBase Result = ScTempOperand(0);
    SB::COperandBase ReadX = ScScratch(ScSwzXXXX);
    SB::COperandBase ReadZ = ScScratch(ScSwzZZZZ);
    SB::COperandBase ReadW = ScScratch(ScSwzWWWW);
    SB::COperandBase MagnitudeZ = ScAbsolute(ReadZ);
    SB::COperandBase Exponent;
    SB::COperandBase Bases;

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);
    Exponent = ScSwizzled(Source, ScSwzWWWW);
    Bases = ScSwizzled(Source, ScSwz(0, 0, 1, 1));

    /* The exponent is clamped to just inside [-128, 128] */
    ScEmit(Asm, D3D10_SB_OPCODE_MIN, 3, &PowerX, &Exponent, &Limit);
    ScEmit(Asm, D3D10_SB_OPCODE_MAX, 3, &PowerX, &ReadX, &Floor);
    ScEmit(Asm, D3D10_SB_OPCODE_MAX, 3, &BaseYZ, &Bases, &Zero);
    ScEmit(Asm, D3D10_SB_OPCODE_LOG, 2, &LogW, &ReadZ);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &WorkZ, &MagnitudeZ);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &WorkZ, &ReadZ, &ReadW, &Lowest);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &WorkZ, &ReadZ, &ReadX);
    ScEmit(Asm, D3D10_SB_OPCODE_EXP, 2, &WorkZ, &ReadZ);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &EndsXW, &Ends);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* dst: (1, a.y * b.y, a.z, b.w) */
static
VOID
ScTranslateDistance(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase First;
    SB::COperandBase Second;
    SB::COperand One(1.0f);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&First, Instr, 0, 0, 4);
    Context->SourceOperand(&Second, Instr, 1, 0, 4);

    SB::COperandBase OutX = ScTempDst(0, ScMaskX);
    SB::COperandBase OutY = ScTempDst(0, ScMaskY);
    SB::COperandBase OutZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase OutW = ScTempDst(0, ScMaskW);
    SB::COperandBase FirstY = ScSwizzled(First, ScSwzYYYY);
    SB::COperandBase SecondY = ScSwizzled(Second, ScSwzYYYY);
    SB::COperandBase FirstZ = ScSwizzled(First, ScSwzZZZZ);
    SB::COperandBase SecondW = ScSwizzled(Second, ScSwzWWWW);

    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutX, &One);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &OutY, &FirstY, &SecondY);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutZ, &FirstZ);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutW, &SecondW);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* pow: exp(b * log|a|), with log 0 as the lowest float */
static
VOID
ScTranslatePower(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Base;
    SB::COperandBase Exponent;
    SB::COperandBase Magnitude;
    SB::COperand Lowest(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    SB::COperandBase LogY = ScTempDst(0, ScMaskY);
    SB::COperandBase KeepX = ScTempDst(0, ScMaskX);
    SB::COperandBase All = ScTempDst(0, ScMaskAll);
    SB::COperandBase ReadX = ScScratch(ScSwzXXXX);
    SB::COperandBase ReadY = ScScratch(ScSwzYYYY);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Base, Instr, 0, 0, 4);
    Context->SourceOperand(&Exponent, Instr, 1, 0, 4);
    Magnitude = ScAbsolute(Base);

    ScEmit(Asm, D3D10_SB_OPCODE_LOG, 2, &LogY, &Magnitude);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &KeepX, &Magnitude);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &All, &ReadX, &ReadY, &Lowest);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &All, &Result, &Exponent);
    ScResult(Context, Instr, D3D10_SB_OPCODE_EXP, Dst, &Result);
}

/* crs: each component is a product pair, then a difference */
static
VOID
ScTranslateCross(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    static const struct
    {
        UINT Products;
        UINT Left;
        UINT Right;
        UINT Target;
        UINT Minuend;
        UINT Subtrahend;
    } Steps[3] =
    {
        { ScMaskX | ScMaskY, ScSwz(1, 2, 2, 2), ScSwz(2, 1, 1, 1), ScMaskX, ScSwzXXXX, ScSwzYYYY },
        { ScMaskY | ScMaskZ, ScSwz(0, 2, 0, 0), ScSwz(0, 0, 2, 2), ScMaskY, ScSwzYYYY, ScSwzZZZZ },
        { ScMaskZ | ScMaskW, ScSwz(0, 0, 0, 1), ScSwz(0, 0, 1, 0), ScMaskZ, ScSwzZZZZ, ScSwzWWWW },
    };
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase First;
    SB::COperandBase Second;
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&First, Instr, 0, 0, 4);
    Context->SourceOperand(&Second, Instr, 1, 0, 4);

    for (const auto& Step : Steps)
    {
        SB::COperandBase Pair = ScTempDst(0, Step.Products);
        SB::COperandBase Left = ScSwizzled(First, Step.Left);
        SB::COperandBase Right = ScSwizzled(Second, Step.Right);
        SB::COperandBase Out = ScTempDst(0, Step.Target);
        SB::COperandBase Minuend = ScScratch(Step.Minuend);
        SB::COperandBase Subtrahend = ScNegated(ScScratch(Step.Subtrahend));

        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &Pair, &Left, &Right);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Out, &Minuend, &Subtrahend);
    }
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* sgn: 0 where the source is zero, otherwise -1 or 1 */
static
VOID
ScTranslateSign(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperand Zero(0.0f);
    SB::COperand One(1.0f);
    SB::COperand MinusOne(-1.0f);
    SB::COperandBase Test = ScTempDst(0, ScMaskAll);
    SB::COperandBase Sign = ScTempDst(1, ScMaskAll);
    SB::COperandBase ReadTest = ScTempOperand(0);
    SB::COperandBase ReadSign = ScTempOperand(1);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);

    ScEmit(Asm, D3D10_SB_OPCODE_EQ, 3, &Test, &Source, &Zero);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Sign, &ReadTest, &Zero, &One);
    ScEmit(Asm, D3D10_SB_OPCODE_LT, 3, &Test, &Source, &Zero);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &ReadTest, &MinusOne, &ReadSign);
}

/* nrm: the source times the reciprocal length, the largest float for a zero vector */
static
VOID
ScTranslateNormalize(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperand Largest(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX);
    SB::COperandBase LengthX = ScTempDst(0, ScMaskX);
    SB::COperandBase InverseY = ScTempDst(0, ScMaskY);
    SB::COperandBase ScaleZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase ReadX = ScScratch(ScSwzXXXX);
    SB::COperandBase ReadY = ScScratch(ScSwzYYYY);
    SB::COperandBase ReadZ = ScScratch(ScSwzZZZZ);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);

    ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &LengthX, &Source, &Source);
    ScEmit(Asm, D3D10_SB_OPCODE_RSQ, 2, &InverseY, &ReadX);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &ScaleZ, &ReadX, &ReadY, &Largest);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MUL, Dst, &Source, &ReadZ);
}

/* sincos: cosine to r0.x and sine to r0.y */
static
VOID
ScTranslateSinCos(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Angle;
    SB::COperandBase Sine = ScTempDst(0, ScMaskY);
    SB::COperandBase Cosine = ScTempDst(0, ScMaskX);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Angle, Instr, 0, 0, 4);
    ScEmit(Context->Asm, D3D10_SB_OPCODE_SINCOS, 3, &Sine, &Cosine, &Angle);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* log: log2|a|, with log 0 as the lowest float */
static
VOID
ScTranslateLog(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperandBase Magnitude;
    SB::COperand Lowest(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    SB::COperandBase KeepX = ScTempDst(0, ScMaskX);
    SB::COperandBase ReadX = ScScratch(ScSwzXXXX);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);
    Magnitude = ScAbsolute(Source);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_MOV, 2, &KeepX, &Magnitude);
    ScResult(Context, Instr, D3D10_SB_OPCODE_LOG, Dst, &Magnitude);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOVC, Dst, &ReadX, &Dst, &Lowest);
}

/* expp: vs_1_1 builds all four components, later versions keep the reduced precision exp */
static
VOID
ScTranslateExpPartial(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperand Truncate(-256);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);

    if (Context->Version >= 0xFFFE0200)
    {
        SB::COperandBase ExpW = ScTempDst(0, ScMaskW);
        SB::COperandBase ReadW = ScScratch(ScSwzWWWW);

        ScEmit(Asm, D3D10_SB_OPCODE_EXP, 2, &ExpW, &Source);
        ScResult(Context, Instr, D3D10_SB_OPCODE_AND, Dst, &ReadW, &Truncate);
        return;
    }

    SB::COperand One(1.0f);
    SB::COperandBase OutX = ScTempDst(0, ScMaskX);
    SB::COperandBase OutY = ScTempDst(0, ScMaskY);
    SB::COperandBase OutZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase OutW = ScTempDst(0, ScMaskW);
    SB::COperandBase ReadY = ScScratch(ScSwzYYYY);
    SB::COperandBase ReadZ = ScScratch(ScSwzZZZZ);
    SB::COperandBase Whole = ScNegated(ReadY);
    SB::COperandBase Result = ScTempOperand(0);

    ScEmit(Asm, D3D10_SB_OPCODE_ROUND_NI, 2, &OutY, &Source);
    ScEmit(Asm, D3D10_SB_OPCODE_EXP, 2, &OutX, &ReadY);
    ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &OutY, &Source, &Whole);
    ScEmit(Asm, D3D10_SB_OPCODE_EXP, 2, &OutZ, &Source);
    ScEmit(Asm, D3D10_SB_OPCODE_AND, 3, &OutZ, &ReadZ, &Truncate);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutW, &One);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* logp: vs_1_1 splits exponent and mantissa from the bits, later versions keep the reduced precision log */
static
VOID
ScTranslateLogPartial(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperandBase Magnitude;
    SB::COperand Lowest(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
    SB::COperand Truncate(-256);
    SB::COperandBase OutX = ScTempDst(0, ScMaskX);
    SB::COperandBase OutW = ScTempDst(0, ScMaskW);
    SB::COperandBase ReadX = ScScratch(ScSwzXXXX);
    SB::COperandBase ReadW = ScScratch(ScSwzWWWW);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);
    Magnitude = ScAbsolute(Source);

    if (Context->Version >= 0xFFFE0200)
    {
        ScEmit(Asm, D3D10_SB_OPCODE_LOG, 2, &OutW, &Magnitude);
        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutX, &Magnitude);
        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &OutW, &ReadX, &ReadW, &Lowest);
        ScResult(Context, Instr, D3D10_SB_OPCODE_AND, Dst, &ReadW, &Truncate);
        return;
    }

    SB::COperand Shift(23);
    SB::COperand Bias(-127);
    SB::COperand Mantissa(0x7FFFFF);
    SB::COperand OneBits(0x3F800000);
    SB::COperand Zero(0.0f);
    SB::COperand LowestBits(static_cast<int>(0xFF7FFFFF));
    SB::COperand One(1.0f);
    SB::COperandBase OutY = ScTempDst(0, ScMaskY);
    SB::COperandBase OutZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase OutXZ = ScTempDst(0, ScMaskX | ScMaskZ);
    SB::COperandBase ReadY = ScScratch(ScSwzYYYY);
    SB::COperandBase ReadZ = ScScratch(ScSwzZZZZ);
    SB::COperandBase ReadXZ = ScScratch(ScSwz(0, 0, 2, 2));
    SB::COperandBase Result = ScTempOperand(0);

    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutW, &Magnitude);
    ScEmit(Asm, D3D10_SB_OPCODE_USHR, 3, &OutX, &ReadW, &Shift);
    ScEmit(Asm, D3D10_SB_OPCODE_IADD, 3, &OutX, &ReadX, &Bias);
    ScEmit(Asm, D3D10_SB_OPCODE_ITOF, 2, &OutX, &ReadX);
    ScEmit(Asm, D3D10_SB_OPCODE_AND, 3, &OutY, &ReadW, &Mantissa);
    ScEmit(Asm, D3D10_SB_OPCODE_OR, 3, &OutY, &ReadY, &OneBits);
    ScEmit(Asm, D3D10_SB_OPCODE_LOG, 2, &OutZ, &ReadW);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &OutZ, &ReadW, &ReadZ, &Lowest);
    ScEmit(Asm, D3D10_SB_OPCODE_AND, 3, &OutZ, &ReadZ, &Truncate);
    ScEmit(Asm, D3D10_SB_OPCODE_EQ, 3, &OutW, &ReadW, &Zero);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &OutXZ, &ReadW, &LowestBits, &ReadXZ);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &OutY, &ReadW, &One, &ReadY);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &OutW, &One);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MOV, Dst, &Result);
}

/* lrp: (b - c) into r0, then a * r0 + c */
static
VOID
ScTranslateLerp(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Weight;
    SB::COperandBase To;
    SB::COperandBase From;
    SB::COperandBase Negative;
    SB::COperandBase Span = ScTempDst(0, ScMaskAll);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Weight, Instr, 0, 0, 4);
    Context->SourceOperand(&To, Instr, 1, 0, 4);
    Context->SourceOperand(&From, Instr, 2, 0, 4);
    Negative = ScNegated(From);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_ADD, 3, &Span, &To, &Negative);
    ScResult(Context, Instr, D3D10_SB_OPCODE_MAD, Dst, &Weight, &Result, &From);
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
        case 14:
        {
            SB::COperandBase Dst;
            SB::COperandBase Source;

            Instr.Destination(&Dst);
            SourceOperand(&Source, Instr, 0, 0, 4);
            ScResult(this, Instr, D3D10_SB_OPCODE_EXP, Dst, &Source);
            break;
        }
        case 15:
            ScTranslateLog(this, Instr);
            break;
        case 16:
            ScTranslateLit(this, Instr);
            break;
        case 17:
            ScTranslateDistance(this, Instr);
            break;
        case 18:
            ScTranslateLerp(this, Instr);
            break;
        case 20:
            ScTranslateMatrix(this, Instr, 4, 4);
            break;
        case 21:
            ScTranslateMatrix(this, Instr, 4, 3);
            break;
        case 22:
            ScTranslateMatrix(this, Instr, 3, 4);
            break;
        case 23:
            ScTranslateMatrix(this, Instr, 3, 3);
            break;
        case 24:
            ScTranslateMatrix(this, Instr, 3, 2);
            break;
        case 32:
            ScTranslatePower(this, Instr);
            break;
        case 33:
            ScTranslateCross(this, Instr);
            break;
        case 34:
            ScTranslateSign(this, Instr);
            break;
        case 36:
            ScTranslateNormalize(this, Instr);
            break;
        case 37:
            ScTranslateSinCos(this, Instr);
            break;
        case 78:
            ScTranslateExpPartial(this, Instr);
            break;
        case 79:
            ScTranslateLogPartial(this, Instr);
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
