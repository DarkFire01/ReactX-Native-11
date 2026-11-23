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

/* texcoord and texcrd: the coordinate itself, components the vertex shader left out read 0, 0, 0, 1 */
static
VOID
ScTranslateTexCoord(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    const NineScOutputs* Decls = static_cast<ScPixelContext*>(Context)->Decls;
    SB::COperandBase Dst;
    SB::COperandBase Coordinate;
    UINT Mask = ScMaskAll;
    UINT Flags = Instr.Flags;
    UINT Rest;

    Instr.Destination(&Dst);
    Context->SourceOperand(&Coordinate, Instr, 0, 0, 4);

    for (UINT Slot = 0; Slot < Decls->Count; Slot += 1)
    {
        NineScOutput Decl = Decls->Outputs[Slot];

        if ((Decl & 0x1E000) && Coordinate.m_Index[0].m_RegIndex == ((Decl >> 9) & 0xF))
        {
            Mask = ((Decl >> 13) & 0xF) << 4;
            break;
        }
    }

    /* Before ps_1_4 texture coordinates are clamped */
    if (Context->Version <= 0xFFFF0103)
        Flags |= ScFlagSaturate;

    Dst.m_WriteMask = static_cast<BYTE>(Mask);
    ScFinish(Context, Flags, D3D10_SB_OPCODE_MOV, 0, Dst, &Coordinate);

    switch (Mask)
    {
        case ScMaskAll:
            return;
        case ScMaskX:
            Rest = ScMaskY | ScMaskZ | ScMaskW;
            break;
        case ScMaskX | ScMaskY:
            Rest = ScMaskZ | ScMaskW;
            break;
        case ScMaskX | ScMaskY | ScMaskZ:
            Rest = ScMaskW;
            break;
        default:
            Rest = Mask;
            break;
    }

    SB::COperand Defaults(0.0f, 0.0f, 0.0f, 1.0f);

    Dst.m_WriteMask = static_cast<BYTE>(Rest);
    ScEmit(Context->Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Defaults);
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

/*
 * The bump environment matrix in cb4[n] applied to a perturbation, added to a coordinate, into r0.xy.
 * Entries holds the swizzles of the two matrix entries each component multiplies.
 */
static
VOID
ScBumpOffset(
    _In_ ScContext* Context,
    _In_ UINT Stage,
    _In_ const SB::COperandBase& Base,
    _In_ const SB::COperandBase& Perturbation,
    _In_ const UINT (&Entries)[4])
{
    ScAsm* Asm = Context->Asm;

    for (UINT Component = 0; Component < 2; Component += 1)
    {
        UINT Same = ScSwz(Component, Component, Component, Component);
        SB::COperandBase Out = ScTempDst(0, ScMaskX << Component);
        SB::COperandBase First = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 4, Stage),
                                            Entries[2 * Component]);
        SB::COperandBase Second = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 4, Stage),
                                             Entries[2 * Component + 1]);
        SB::COperandBase DuX = ScSwizzled(Perturbation, ScSwzXXXX);
        SB::COperandBase DuY = ScSwizzled(Perturbation, ScSwzYYYY);
        SB::COperandBase Partial = ScScratchRead(0, Same);
        SB::COperandBase Coordinate = ScSwizzled(Base, Same);

        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &Out, &First, &DuX);
        ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Out, &Second, &DuY, &Partial);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Out, &Partial, &Coordinate);
    }
}

/* texbem and texbeml: sample at the coordinate moved by the bump matrix, texbeml then scales by luminance */
static
VOID
ScTranslateTexBem(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ BOOL Luminance)
{
    static const UINT Entries[4] = { ScSwzXXXX, ScSwzYYYY, ScSwzZZZZ, ScSwzWWWW };
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Coordinate;
    SB::COperandBase Perturbation;
    UINT Stage = Instr.Dst.Token & 0x7FF;
    SB::COperandBase Offset = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Coordinate, Instr, 0, 0, 4);
    Context->SourceOperand(&Perturbation, Instr, 1, 0, 4);
    ScBumpOffset(Context, Stage, Coordinate, Perturbation, Entries);

    if (!Luminance)
    {
        ScSampleOwn(Context, Instr, Dst, Stage, Offset);
        return;
    }

    SB::COperandBase Resource = SB::COperand4(D3D10_SB_OPERAND_TYPE_RESOURCE, Stage);
    SB::COperand State(D3D10_SB_OPERAND_TYPE_SAMPLER, Stage);
    SB::COperandBase Texel = ScTempDst(0, ScMaskAll);
    SB::COperandBase Read = ScTempOperand(0);
    UINT Mode = (Context->Raster->Words[4] >> (2 * Stage)) & 3;

    ScEmit(Asm, D3D10_SB_OPCODE_SAMPLE, 4, &Texel, &Offset, &Resource, &State);
    if (Mode)
        Context->EmitSamplerSwizzle(Texel, Read, Mode);
    if (Context->Raster->Words[2] & 3)
        Context->EmitColorKey(Stage, Texel, Read);

    /* Luminance scale and offset live in cb4[n + 8].xy */
    SB::COperandBase Scale = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 4, Stage + 8), ScSwzXXXX);
    SB::COperandBase Bias = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 4, Stage + 8), ScSwzYYYY);
    SB::COperandBase Level = ScSwizzled(Coordinate, ScSwzZZZZ);
    SB::COperandBase LuminanceX = ScTempDst(1, ScMaskX);
    SB::COperandBase Factor = ScScratchRead(1, ScSwzXXXX);

    ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &LuminanceX, &Scale, &Level, &Bias);
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MUL, 0, Dst, &Read, &Factor);
}

/* bem: the bump matrix offset without a sample */
static
VOID
ScTranslateBem(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    static const UINT Entries[4] = { ScSwzXXXX, ScSwzZZZZ, ScSwzYYYY, ScSwzWWWW };
    SB::COperandBase Dst;
    SB::COperandBase Base;
    SB::COperandBase Perturbation;
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Base, Instr, 0, 0, 4);
    Context->SourceOperand(&Perturbation, Instr, 1, 0, 4);
    ScBumpOffset(Context, Instr.Dst.Token & 0x7FF, Base, Perturbation, Entries);
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOV, 0, Dst, &Result);
}

/* texreg2ar, texreg2gb and texreg2rgb: sample at components of another texture register */
static
VOID
ScTranslateTexReg(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ UINT Swizzle)
{
    SB::COperandBase Dst;
    SB::COperandBase Source;
    SB::COperandBase Coordinate = ScTempDst(0, ScMaskAll);
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Source, Instr, 0, 0, 4);

    SB::COperandBase Selected = ScSwizzled(Source, Swizzle);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_MOV, 2, &Coordinate, &Selected);
    ScSampleOwn(Context, Instr, Dst, Instr.Dst.Token & 0x7FF, Result);
}

/* One row of a texm3x2 or texm3x3 matrix: dp3 of the coordinate and the texture register into r0 */
static
VOID
ScMatrixRow(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ UINT Mask)
{
    SB::COperandBase Row;
    SB::COperandBase Vector;
    SB::COperandBase Out = ScTempDst(0, Mask);

    Context->SourceOperand(&Row, Instr, 0, 0, 4);
    Context->SourceOperand(&Vector, Instr, 1, 0, 4);
    ScEmit(Context->Asm, D3D10_SB_OPCODE_DP3, 3, &Out, &Row, &Vector);
}

/* The last row of a matrix followed by a sample at r0 */
static
VOID
ScMatrixSample(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ UINT Mask)
{
    SB::COperandBase Dst;
    SB::COperandBase Result = ScTempOperand(0);

    Instr.Destination(&Dst);
    ScMatrixRow(Context, Instr, Mask);
    ScSampleOwn(Context, Instr, Dst, Instr.Dst.Token & 0x7FF, Result);
}

/* Depth from r0.x / r0.y, with y = 0 giving 1 */
static
VOID
ScWriteDepth(
    _In_ ScContext* Context,
    _In_ UINT Flags,
    _In_ UINT Source)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase IsSet = ScTempDst(0, ScMaskZ);
    SB::COperandBase SourceY = ScScratchRead(Source, ScSwzYYYY);
    SB::COperandBase SourceXY = ScScratchRead(Source, ScSwzXYYY);
    SB::COperandBase Pair = ScTempDst(0, ScMaskX | ScMaskY);
    SB::COperandBase Selector = ScScratchRead(0, ScSwzZZZZ);
    SB::COperandBase Depth = ScTempDst(Context->OutputTemps[4], ScMaskAll);
    SB::COperandBase Numerator = ScScratchRead(0, ScSwzXXXX);
    SB::COperandBase Denominator = ScScratchRead(0, ScSwzYYYY);
    SB::COperand Zero(0.0f);
    SB::COperand One(1.0f);

    ScEmit(Asm, D3D10_SB_OPCODE_NE, 3, &IsSet, &SourceY, &Zero);
    ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Pair, &Selector, &SourceXY, &One);
    ScFinish(Context, Flags, D3D10_SB_OPCODE_DIV, 0, Depth, &Numerator, &Denominator);
}

/* texm3x3spec and texm3x3vspec: reflect the eye vector about the normal in r0, then sample */
static
VOID
ScTranslateReflect(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr,
    _In_ BOOL EyeFromCoordinates)
{
    ScAsm* Asm = Context->Asm;
    SB::COperandBase Dst;
    SB::COperandBase Row;
    SB::COperandBase Vector;
    SB::COperandBase Eye;
    UINT Stage = Instr.Dst.Token & 0x7FF;
    SB::COperandBase NormalZ = ScTempDst(0, ScMaskZ);
    SB::COperandBase NormalW = ScTempDst(0, ScMaskW);
    SB::COperandBase EyeW = ScTempDst(1, ScMaskW);
    SB::COperandBase EyeAll = ScTempDst(1, ScMaskAll);
    SB::COperandBase NormalAll = ScTempDst(0, ScMaskAll);
    SB::COperandBase Normal = ScTempOperand(0);
    SB::COperandBase NormalWWWW = ScScratchRead(0, ScSwzWWWW);
    SB::COperandBase EyeRead = ScTempOperand(1);
    SB::COperandBase EyeWWWW = ScScratchRead(1, ScSwzWWWW);
    SB::COperandBase Away = ScNegated(ScTempOperand(1));
    SB::COperand Two(2.0f);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Row, Instr, 0, 0, 4);
    Context->SourceOperand(&Vector, Instr, 1, 0, 4);

    if (!EyeFromCoordinates)
    {
        Context->SourceOperand(&Eye, Instr, 2, 0, 4);
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &NormalZ, &Row, &Vector);
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &EyeW, &Normal, &Normal);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &EyeAll, &Eye, &EyeWWWW);
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &NormalW, &Normal, &Eye);
    }
    else
    {
        /* The eye vector is the w of the three rows' coordinates */
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &NormalZ, &Row, &Vector);
        for (UINT Component = 0; Component < 3; Component += 1)
        {
            SB::COperandBase Part = ScTempDst(1, ScMaskX << Component);
            SB::COperandBase W;

            Context->InputOperand(&W, Stage + 1 + Component, ScSwzWWWW);
            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Part, &W);
        }
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &NormalW, &Normal, &EyeRead);
        ScEmit(Asm, D3D10_SB_OPCODE_DP3, 3, &EyeW, &Normal, &Normal);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &EyeAll, &EyeRead, &EyeWWWW);
    }

    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &NormalAll, &Normal, &NormalWWWW);
    ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &NormalAll, &Two, &Normal, &Away);
    ScSampleOwn(Context, Instr, Dst, Stage, Normal);
}

/* cnd: src1 where src0 > 0.5, else src2; ps_1_x tests src0.w and a coissued cnd is a plain move */
static
VOID
ScTranslateCnd(
    _In_ ScContext* Context,
    _In_ ScInstr& Instr)
{
    SB::COperandBase Dst;
    SB::COperandBase Condition;
    SB::COperandBase Above;
    SB::COperandBase Below;
    SB::COperand Half(0.5f);

    Instr.Destination(&Dst);
    Context->SourceOperand(&Condition, Instr, 0, 0, 4);
    Context->SourceOperand(&Above, Instr, 1, 0, 4);
    Context->SourceOperand(&Below, Instr, 2, 0, 4);

    if (Context->Version <= 0xFFFF0103)
    {
        if (Instr.Flags & ScFlagCoissue)
        {
            ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOV, 0, Dst, &Above);
            return;
        }

        SB::COperandBase Test = ScTempDst(0, ScMaskX);
        SB::COperandBase Alpha = ScSwizzled(Condition, ScSwzWWWW);
        SB::COperandBase Selector = ScScratchRead(0, ScSwzXXXX);

        ScEmit(Context->Asm, D3D10_SB_OPCODE_LT, 3, &Test, &Half, &Alpha);
        ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOVC, 0, Dst, &Selector, &Above, &Below);
        return;
    }

    SB::COperandBase Test = ScTempDst(0, ScMaskAll);
    SB::COperandBase Selector = ScTempOperand(0);

    ScEmit(Context->Asm, D3D10_SB_OPCODE_LT, 3, &Test, &Half, &Condition);
    ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOVC, 0, Dst, &Selector, &Above, &Below);
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
        case 64:
            ScTranslateTexCoord(Context, Instr);
            break;
        case 65:
            ScTranslateTexKill(Context, Instr);
            break;
        case 66:
            ScTranslateTex(Context, Instr);
            break;
        case 67:
            ScTranslateTexBem(Context, Instr, FALSE);
            break;
        case 68:
            ScTranslateTexBem(Context, Instr, TRUE);
            break;
        case 69:
            ScTranslateTexReg(Context, Instr, ScSwz(3, 0, 0, 0));
            break;
        case 70:
            ScTranslateTexReg(Context, Instr, ScSwz(1, 2, 2, 2));
            break;
        case 82:
            ScTranslateTexReg(Context, Instr, ScSwzXYZZ);
            break;
        case 71:
            ScMatrixRow(Context, Instr, ScMaskX);
            break;
        case 72:
            ScMatrixSample(Context, Instr, ScMaskY);
            break;
        case 73:
            /* The two pads of a 3x3 matrix fill x, then y */
            ScMatrixRow(Context, Instr, Context->SecondPad ? ScMaskY : ScMaskX);
            Context->SecondPad = !Context->SecondPad;
            break;
        case 74:
            ScMatrixSample(Context, Instr, ScMaskZ);
            break;
        case 76:
            ScTranslateReflect(Context, Instr, FALSE);
            break;
        case 77:
            ScTranslateReflect(Context, Instr, TRUE);
            break;
        case 80:
            ScTranslateCnd(Context, Instr);
            break;
        case 83:
        {
            SB::COperandBase Dst;
            SB::COperandBase Clear = ScTempDst(0, ScMaskY | ScMaskZ);
            SB::COperandBase Result = ScTempOperand(0);
            SB::COperand Zero(0.0f);

            Instr.Destination(&Dst);
            ScMatrixRow(Context, Instr, ScMaskX);
            ScEmit(Context->Asm, D3D10_SB_OPCODE_MOV, 2, &Clear, &Zero);
            ScSampleOwn(Context, Instr, Dst, Instr.Dst.Token & 0x7FF, Result);
            break;
        }
        case 84:
            ScMatrixRow(Context, Instr, ScMaskY);
            ScWriteDepth(Context, Instr.Flags | ScFlagSaturate, 0);
            break;
        case 85:
        {
            SB::COperandBase Dst;
            SB::COperandBase Row;
            SB::COperandBase Vector;

            Instr.Destination(&Dst);
            Context->SourceOperand(&Row, Instr, 0, 0, 4);
            Context->SourceOperand(&Vector, Instr, 1, 0, 4);
            ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_DP3, 0, Dst, &Row, &Vector);
            break;
        }
        case 86:
        {
            SB::COperandBase Dst;
            SB::COperandBase W = ScTempDst(0, ScMaskW);
            SB::COperandBase Result = ScTempOperand(0);
            SB::COperand One(1.0f);

            Instr.Destination(&Dst);
            ScMatrixRow(Context, Instr, ScMaskZ);
            ScEmit(Context->Asm, D3D10_SB_OPCODE_MOV, 2, &W, &One);
            ScFinish(Context, Instr.Flags, D3D10_SB_OPCODE_MOV, 0, Dst, &Result);
            break;
        }
        case 87:
            ScWriteDepth(Context, Instr.Flags, Context->Map->Temps[Instr.Dst.Token & 0x7FF]);
            break;
        case 88:
            ScTranslateCmp(Context, Instr);
            break;
        case 89:
            ScTranslateBem(Context, Instr);
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
