/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: pixel shader texture and arithmetic instructions
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

/* Emits the final instruction with the destination modifiers applied */
static
VOID
ScFinish(
    _In_ ScContext* Context,
    _In_ UINT Flags,
    _In_ D3D10_SB_OPCODE_TYPE Opcode,
    _In_ UINT Sampler,
    _In_ const SB::COperandBase& Dst,
    _In_ const SB::COperandBase* Source0,
    _In_opt_ const SB::COperandBase* Source1 = NULL,
    _In_opt_ const SB::COperandBase* Source2 = NULL,
    _In_opt_ const SB::COperandBase* Source3 = NULL,
    _In_opt_ const SB::COperandBase* Source4 = NULL)
{
    const SB::COperandBase* Sources[5] = { Source0, Source1, Source2, Source3, Source4 };
    UINT Count = 1;

    while (Count < 5 && Sources[Count])
        Count += 1;
    Context->EmitResult(Flags, Opcode, Sampler, Dst, Count, Sources);
}

/* A read of r0 or r1 through a swizzle */
static
SB::COperandBase
ScScratchRead(
    _In_ UINT Temp,
    _In_ UINT Swizzle)
{
    return ScSwizzled(ScTempOperand(Temp), Swizzle);
}

/* sample from the texture and sampler a ps_1_x destination register names */
static
VOID
ScSampleOwn(
    _In_ ScContext* Context,
    _In_ const ScInstr& Instr,
    _In_ const SB::COperandBase& Dst,
    _In_ UINT Sampler,
    _In_ const SB::COperandBase& Coordinate)
{
    SB::COperandBase Resource = SB::COperand4(D3D10_SB_OPERAND_TYPE_RESOURCE, Sampler);
    SB::COperand State(D3D10_SB_OPERAND_TYPE_SAMPLER, Sampler);

    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_SAMPLE, Sampler, Dst, &Coordinate, &Resource, &State);
}

/* texld, texldp, texldb and the ps_1_x tex */
static
VOID
ScTranslateTex(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Full;
    SB::COperandBase Coordinate;
    UINT Sampler;
    UINT Swizzle;
    UINT Components;
    BOOL Shadow;

    Instr.Destination(&Dst);

    if (Context->Version < 0xFFFF0200)
    {
        Sampler = Instr.Dst.Token & 0x7FF;
        Context->SourceOperand(&Coordinate, Instr, 0, 0, 4);
        if (ScUsesShadowCompare(Context, Sampler))
            Context->EmitSampleCompare(Instr, Dst, Sampler, Coordinate);
        else
            ScSampleOwn(Context, Instr, Dst, Sampler, Coordinate);
        return;
    }

    Sampler = Instr.Src[1].Token & 0x7FF;
    Shadow = ScUsesShadowCompare(Context, Sampler);

    /* The coordinate keeps as many components as the bound texture has */
    switch (Context->Raster->Samplers[Sampler] & 3)
    {
        case 1:
            Components = Shadow ? 3 : 2;
            break;
        case 2:
        case 3:
            Components = 3;
            break;
        default:
            Components = 4;
            break;
    }

    Context->SourceOperand(&Full, Instr, 0, 0, 4);
    Context->SourceOperand(&Coordinate, Instr, 0, 0, Components);
    Swizzle = ScTokenSwizzle(Instr.Src[1].Token, 4);

    SB::COperandBase Resource = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_RESOURCE, Sampler), Swizzle);
    SB::COperand State(D3D10_SB_OPERAND_TYPE_SAMPLER, Sampler);

    if (Instr.OpcodeToken & 0x10000)
    {
        /* texldp divides by w first */
        SB::COperandBase Projected = ScTempDst(0, ScMaskAll);
        SB::COperandBase W = ScSwizzled(Full, ScSwzWWWW);
        SB::COperandBase Result = ScTempOperand(0);

        ScEmit(Context->Asm, D3D10_SB_OPCODE_DIV, 3, &Projected, &Coordinate, &W);
        if (ScUsesShadowCompare(Context, Sampler))
            Context->EmitSampleCompare(Instr, Dst, Sampler, Result);
        else
            ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_SAMPLE, Sampler, Dst, &Result, &Resource, &State);
        return;
    }

    if (Shadow)
    {
        Context->EmitSampleCompare(Instr, Dst, Sampler, Coordinate);
        return;
    }

    if (Instr.OpcodeToken & 0x20000)
    {
        /* texldb biases by w */
        SB::COperandBase Bias = ScComponent(Full, 3);

        ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_SAMPLE_B, Sampler, Dst, &Coordinate, &Resource, &State, &Bias);
        return;
    }

    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_SAMPLE, Sampler, Dst, &Coordinate, &Resource, &State);
}

/* texldd: sample_d with the gradients from the third and fourth sources */
static
VOID
ScTranslateTexLdd(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Coordinate;
    SB::COperandBase AcrossX;
    SB::COperandBase AcrossY;
    DWORD SamplerToken = Instr.Src[1].Token;
    UINT Sampler = SamplerToken & 0x7FF;

    Instr.Destination(&Dst);
    Context->SourceOperand(&Coordinate, Instr, 0, 0, 4);
    Context->SourceOperand(&AcrossX, Instr, 2, 0, 4);
    Context->SourceOperand(&AcrossY, Instr, 3, 0, 4);

    SB::COperand State(D3D10_SB_OPERAND_TYPE_SAMPLER, Sampler);
    SB::COperandBase Resource = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_RESOURCE, Sampler),
                                           ScTokenSwizzle(SamplerToken, 4));

    /* The sampler token's low byte selects the swizzle and color key state */
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_SAMPLE_D, SamplerToken & 0xFF, Dst, &Coordinate, &Resource, &State,
             &AcrossX, &AcrossY);
}

/* texkill: discard when any tested component is negative */
static
VOID
ScTranslateTexKill(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Value;
    UINT Mask;
    UINT First = 0;
    BOOL Found = FALSE;

    Context->SourceOperand(&Value, Instr, 0, 0, 4);
    Mask = Context->Version < 0xFFFF0200 ? (ScMaskX | ScMaskY | ScMaskZ) : ScTokenWriteMask(Instr.Src[0].Token);

    SB::COperandBase Negative = ScTempDst(0, Mask);
    SB::COperandBase Tested = ScSwizzled(Value, ScSwzXYZW);
    SB::COperand Zero(0.0f);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_LT, 3, &Negative, &Tested, &Zero);

    for (UINT Component = 0; Component < 4; Component += 1)
    {
        if (!Found && (Mask & (ScMaskX << Component)))
        {
            First = Component;
            Found = TRUE;
        }
    }

    /* Fold the other tested components into the first */
    for (UINT Component = 0; Component < 4; Component += 1)
    {
        if (Component == First || !(Mask & (ScMaskX << Component)))
            continue;

        SB::COperandBase Into = ScTempDst(0, ScMaskX << First);
        SB::COperandBase Left = ScScratchRead(0, ScSwz(First, First, First, First));
        SB::COperandBase Right = ScScratchRead(0, ScSwz(Component, Component, Component, Component));

        ScEmit(Context->Asm, D3D10_SB_OPCODE_OR, 3, &Into, &Left, &Right);
    }

    SB::COperandBase Test = SB::COperand4(D3D10_SB_OPERAND_TYPE_TEMP, 0, static_cast<D3D10_SB_4_COMPONENT_NAME>(First));
    SB::CInstruction Discard(D3D10_SB_OPCODE_DISCARD, Test, D3D10_SB_INSTRUCTION_TEST_NONZERO);

    Context->Asm->Add(Discard);
}

/* cmp: src1 where src0 >= 0, else src2 */
static
VOID
ScTranslateCmp(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Condition;
    SB::COperandBase Above;
    SB::COperandBase Below;
    SB::COperand Zero(0.0f);
    SB::COperandBase Test = ScTempDst(0, ScMaskAll);
    SB::COperandBase Selector = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Condition, Instr, 0, 0, 4);
    Context->SourceOperand(&Above, Instr, 1, 0, 4);
    Context->SourceOperand(&Below, Instr, 2, 0, 4);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_GE, 3, &Test, &Condition, &Zero);
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOVC, 0, Dst, &Selector, &Above, &Below);
}

/* dp2add: dp2 into r0, then the add */
static
VOID
ScTranslateDp2Add(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Left;
    SB::COperandBase Right;
    SB::COperandBase Addend;
    SB::COperandBase Product = ScTempDst(0, ScMaskAll);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Left, Instr, 0, 0, 2);
    Context->SourceOperand(&Right, Instr, 1, 0, 2);
    Context->SourceOperand(&Addend, Instr, 2, 0, 4);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_DP2, 3, &Product, &Left, &Right);
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_ADD, 0, Dst, &Result, &Addend);
}

HRESULT
ScTranslatePixelInstruction(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    switch (Instr.Opcode)
    {
        case 65:
            ScTranslateTexKill(Context, Instr);
            break;
        case 66:
            ScTranslateTex(Context, Instr);
            break;
        case 88:
            ScTranslateCmp(Context, Instr);
            break;
        case 90:
            ScTranslateDp2Add(Context, Instr);
            break;
        case 91:
        case 92:
        {
            SB::COperandBase Dst;
            SB::COperandBase Source;

            Instr.Destination(&Dst);
            Context->SourceOperand(&Source, Instr, 0, 0, 4);
            ScFinish(Context, Instr.Flags,
                     Instr.Opcode == 91 ? D3D11_SB_OPCODE_DERIV_RTX_COARSE : D3D11_SB_OPCODE_DERIV_RTY_COARSE, 0, Dst,
                     &Source);
            break;
        }
        case 93:
            ScTranslateTexLdd(Context, Instr);
            break;
        default:
            return E_NOTIMPL;
    }
    return S_OK;
}
