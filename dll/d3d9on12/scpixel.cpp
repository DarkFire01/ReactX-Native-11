/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: pixel shader declarations, outputs, fog and alpha test
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

ScPixelContext::ScPixelContext(
    _In_ UINT Runtime,
    _In_ ScAsm* Writer,
    _In_ const NineScRasterStates* States,
    _In_ ScShader* Info,
    _In_ NineScOutputs* Inputs) :
    ScContext(Runtime, Writer, States, Info),
    Decls(Inputs),
    PixelFlags(Info->PixelFlags),
    FogDecl(NULL),
    SecondColorDecl(NULL),
    PositionRegister(Info->PositionRegister)
{
}

/* Maps a texture kind to its resource dimension; unknown kinds are 2D */
static
D3D10_SB_RESOURCE_DIMENSION
ScPixelResourceDimension(
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
ScPixelContext::WriteDeclarations()
{
    std::vector<ScInputUse> Uses;
    BYTE Written[16];
    BOOL NeedsPosition = FALSE;
    BYTE Next;
    UINT TempCount;
    UINT FloatSize = Shader->RangeEnd[NineScFloatBuffer];
    UINT IntegerSize = Shader->RangeEnd[NineScIntegerBuffer];
    UINT BooleanSize = Shader->RangeEnd[NineScBooleanBuffer];

    for (UINT Slot = 0; Slot < Decls->Count; Slot += 1)
    {
        ScInputUse Use;

        Use.Register = static_cast<BYTE>((Decls->Outputs[Slot] >> 9) & 0xF);
        Use.Usage = static_cast<BYTE>(Decls->Outputs[Slot] & 0x1F);
        Uses.push_back(Use);
    }

    TempBase = Shader->TempCount;
    Next = static_cast<BYTE>(Shader->TempCount + Shader->LoopCount);
    AllocateInputTemps(Uses, &Next);
    TempCount = Next;
    Asm->EmitTempsDecl(TempCount);

    /* Relatively addressed constants get a buffer declared without a size */
    if (FloatSize)
    {
        Asm->EmitConstantBufferDecl(0, (Shader->Relative[NineScFloatBuffer] ? 0 : FloatSize) >> 2,
                                    D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    }
    if (IntegerSize)
        Asm->EmitConstantBufferDecl(1, IntegerSize >> 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    if (BooleanSize)
        Asm->EmitConstantBufferDecl(2, (BooleanSize + 3) >> 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    if (ScImplicitFog(Raster, Version) || (Raster->Words[1] & 0x20000000))
        Asm->EmitConstantBufferDecl(3, 2, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    if (PixelFlags & ScPixelBumpMatrix)
        Asm->EmitConstantBufferDecl(4, 16, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    if (Raster->Words[2] & 3)
        Asm->EmitConstantBufferDecl(5, 8, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);

    for (UINT Sampler = 0; Sampler < 16; Sampler += 1)
    {
        UINT Kind = Map->SamplerKinds[Sampler];

        if (Kind == ScNoRegister)
            continue;

        /* Samplers ps_1_x uses implicitly take their kind from the bound texture */
        if (Kind == 0)
            Kind = Raster->Samplers[Sampler] & 3;
        Asm->EmitSamplerDecl(Sampler, ScUsesShadowCompare(this, Sampler) ? D3D10_SB_SAMPLER_MODE_COMPARISON
                                                                         : D3D10_SB_SAMPLER_MODE_DEFAULT);
        Asm->EmitResourceDecl(ScPixelResourceDimension(Kind), Sampler, D3D10_SB_RETURN_TYPE_FLOAT,
                              D3D10_SB_RETURN_TYPE_FLOAT, D3D10_SB_RETURN_TYPE_FLOAT, D3D10_SB_RETURN_TYPE_FLOAT);
    }

    ZeroMemory(Written, sizeof(Written));
    for (UINT Slot = 0; Slot < Decls->Count; Slot += 1)
    {
        const NineScOutput* Decl = &Decls->Outputs[Slot];
        UINT Usage = *Decl & 0x1F;
        UINT Index = (*Decl >> 5) & 0xF;
        UINT Register = (*Decl >> 9) & 0xF;
        UINT Mask = (*Decl >> 13) & 0xF;
        D3D10_SB_INTERPOLATION_MODE Mode;

        if (!Mask)
            continue;

        if (Usage == 0 || (Usage == 15 && Index == 0))
        {
            NeedsPosition = TRUE;
            continue;
        }
        if (Usage == 14 && Index == 0)
        {
            Asm->EmitPSInputSystemGeneratedValueDecl(14, Mask << 4, D3D10_SB_INTERPOLATION_CONSTANT,
                                                     D3D10_SB_NAME_IS_FRONT_FACE);
            continue;
        }

        /* Two inputs can share a register as long as their components do not overlap */
        if (Written[Register] & Mask)
            continue;
        Written[Register] |= Mask;

        if (Usage == 10 && Index == 0 && (Raster->Words[1] & 0x30000) == 0x10000)
        {
            /* Flat shading */
            Mode = D3D10_SB_INTERPOLATION_CONSTANT;
        }
        else
        {
            Mode = ((Decls->Indexed & ((1ull << 45) - 1)) >> Slot) & 1 ? D3D10_SB_INTERPOLATION_LINEAR_CENTROID
                                                                       : D3D10_SB_INTERPOLATION_LINEAR;
            if (Usage == 11)
                FogDecl = Decl;
            else if (Usage == 10 && Index == 1)
                SecondColorDecl = Decl;
        }
        Asm->EmitPSInputDecl(Register, Mask << 4, Mode);
    }

    /* Indexed inputs are declared as runs of consecutive registers */
    if (PixelFlags & ScPixelIndexedInputs)
    {
        UINT Start = 0;

        while (Start < 10)
        {
            UINT Run = 0;

            while (Start + Run < 10 && Written[Start + Run])
                Run += 1;
            if (Run)
                Asm->EmitInputIndexingRangeDecl(Start, Run, ScMaskAll);
            Start += Run + 1;
        }
    }

    if (NeedsPosition)
    {
        Asm->EmitPSInputSystemInterpretedValueDecl(PositionRegister, ScMaskAll,
                                                   D3D10_SB_INTERPOLATION_LINEAR_NOPERSPECTIVE, D3D10_SB_NAME_POSITION);
    }

    for (UINT Output = 0; Output < 4; Output += 1)
    {
        if (OutputTemps[Output] != ScNoRegister)
            Asm->EmitOutputDecl(Output, ScMaskAll);
    }
    if (OutputTemps[4] != ScNoRegister)
        Asm->EmitODepthDecl();

    InitializeInputTemps(Uses, TempCount);

    /* vPos is the pixel's integer position */
    if (Map->Inputs[13].Register != ScNoRegister && Version >= 0xFFFF0300)
    {
        SB::COperandBase Dst = ScTempDst(Map->Inputs[13].Register, ScMaskX | ScMaskY);
        SB::COperandBase Position = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, PositionRegister), ScSwzXYYY);

        ScEmit(Asm, D3D10_SB_OPCODE_ROUND_NI, 2, &Dst, &Position);
    }

    /* vFace is 1 for front faces and -1 for back faces */
    if (Map->Inputs[14].Register != ScNoRegister)
    {
        SB::COperandBase Dst = ScTempDst(Map->Inputs[14].Register, ScMaskAll);
        SB::COperandBase Face = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, 14), ScSwzXXXX);
        SB::COperand Front(1.0f);
        SB::COperand Back(-1.0f);

        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Dst, &Face, &Front, &Back);
    }
    return S_OK;
}

/* Alpha test: discard where the output alpha fails the comparison with the reference in cb3[0].w */
VOID
ScPixelContext::WriteAlphaTest()
{
    UINT Function = Raster->Words[1] & 0x1E00000;
    UINT Comparison;

    if (Function == 0x200000)
    {
        SB::COperand Zero(0.0f);
        SB::CInstruction Discard(D3D10_SB_OPCODE_DISCARD, Zero, D3D10_SB_INSTRUCTION_TEST_ZERO);

        Asm->Add(Discard);
        return;
    }

    if (OutputTemps[0] == ScNoRegister)
        return;

    switch (Function)
    {
        case 0x400000:
            Comparison = 4;
            break;
        case 0x600000:
            Comparison = 2;
            break;
        case 0x800000:
            Comparison = 6;
            break;
        case 0xA00000:
            Comparison = 1;
            break;
        case 0xC00000:
            Comparison = 5;
            break;
        default:
            Comparison = 3;
            break;
    }

    SB::COperandBase Reference = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 0), ScSwzWWWW);
    SB::COperandBase Alpha = ScSwizzled(ScTempOperand(OutputTemps[0]), ScSwzWWWW);
    SB::COperandBase Test = ScTempDst(0, ScMaskX);
    SB::COperandBase Passed = SB::COperand4(D3D10_SB_OPERAND_TYPE_TEMP, 0, D3D10_SB_4_COMPONENT_X);

    ScCompare(this, Comparison, Test, Alpha, Reference);

    SB::CInstruction Discard(D3D10_SB_OPCODE_DISCARD, Passed, D3D10_SB_INSTRUCTION_TEST_ZERO);

    Asm->Add(Discard);
}

/* The fog factor from the pixel's depth or w: exp, exp2 or linear with the constants in cb3[1] */
VOID
ScPixelContext::WritePixelFog()
{
    SB::COperandBase Depth = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, PositionRegister),
                                        (Raster->Words[1] & 0x10000000) ? ScSwzWWWW : ScSwzZZZZ);
    SB::COperandBase Params = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 1);
    SB::COperandBase FactorX = ScTempDst(0, ScMaskX);
    SB::COperandBase FactorW = ScTempDst(0, ScMaskW);
    SB::COperandBase ReadX = ScSwizzled(ScTempOperand(0), ScSwzXXXX);
    UINT Mode = Raster->Words[1] & 0xC000000;

    if (Mode == 0x4000000 || Mode == 0x8000000)
    {
        SB::COperandBase Density = ScSwizzled(Params, ScSwzWWWW);
        SB::COperand Scale(-0x1.715476p0f);

        /* exp: 2^(-d * z * log2 e), exp2 squares d * z first */
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &FactorX, &Depth, &Density);
        if (Mode == 0x8000000)
            ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &FactorX, &ReadX, &ReadX);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &FactorX, &ReadX, &Scale);
        ScEmit(Asm, D3D10_SB_OPCODE_EXP, 2, &FactorW, &ReadX);
        return;
    }

    SB::COperandBase End = ScSwizzled(Params, ScSwzYYYY);
    SB::COperandBase Start = ScSwizzled(Params, ScSwzXXXX);
    SB::COperandBase Range = ScSwizzled(Params, ScSwzZZZZ);
    SB::COperandBase ReadW = ScSwizzled(ScTempOperand(0), ScSwzWWWW);
    SB::COperandBase Near = ScNegated(Depth);
    SB::COperandBase Test = SB::COperand4(D3D10_SB_OPERAND_TYPE_TEMP, 0, D3D10_SB_4_COMPONENT_X);
    SB::COperand Zero(0.0f);
    SB::COperand One(1.0f);
    SB::CInstruction Beyond(D3D10_SB_OPCODE_IF, Test, D3D10_SB_INSTRUCTION_TEST_NONZERO);
    SB::CInstruction Within(D3D10_SB_OPCODE_IF, Test, D3D10_SB_INSTRUCTION_TEST_NONZERO);
    SB::CInstruction Else(D3D10_SB_OPCODE_ELSE);
    SB::CInstruction EndIf(D3D10_SB_OPCODE_ENDIF);

    /* Linear: 0 past the end, 1 before the start, (end - z) * scale between */
    ScEmit(Asm, D3D10_SB_OPCODE_GE, 3, &FactorX, &Depth, &End);
    Asm->Add(Beyond);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &FactorW, &Zero);
    Asm->Add(Else);
    ScEmit(Asm, D3D10_SB_OPCODE_GE, 3, &FactorX, &Start, &Depth);
    Asm->Add(Within);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &FactorW, &One);
    Asm->Add(Else);
    ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &FactorW, &End, &Near);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &FactorW, &ReadW, &Range);
    Asm->Add(EndIf);
    Asm->Add(EndIf);
}

/* Blends the first color output toward the fog color in cb3[0] by the fog factor */
VOID
ScPixelContext::WriteFog()
{
    UINT Color = OutputTemps[0];

    if (Color == ScNoRegister)
        return;

    if (Raster->Words[1] & 0xC000000)
    {
        WritePixelFog();
    }
    else
    {
        SB::COperandBase Factor = ScTempDst(0, ScMaskW);
        SB::COperandBase Source;

        if (FogDecl)
        {
            Source = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, (*FogDecl >> 9) & 0xF),
                                ScSwizzleFromMask((*FogDecl >> 13) & 0xF));
        }
        else if (SecondColorDecl)
        {
            Source = ScSwizzled(SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, (*SecondColorDecl >> 9) & 0xF), ScSwzWWWW);
        }
        else
        {
            Source = SB::COperand(0.0f);
        }

        SB::CInstruction Move(D3D10_SB_OPCODE_MOV, Factor, Source);

        Move.m_bSaturate = TRUE;
        Asm->Add(Move);
    }

    SB::COperandBase FogColor = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 0), ScSwzXYZZ);
    SB::COperandBase Away = ScNegated(FogColor);
    SB::COperandBase Difference = ScTempDst(0, ScMaskX | ScMaskY | ScMaskZ);
    SB::COperandBase Output = ScTempOperand(Color);
    SB::COperandBase Blended = ScTempDst(Color, ScMaskX | ScMaskY | ScMaskZ);
    SB::COperandBase Scratch = ScTempOperand(0);
    SB::COperandBase Weight = ScSwizzled(ScTempOperand(0), ScSwzWWWW);

    ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Difference, &Output, &Away);
    ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Blended, &Scratch, &Weight, &FogColor);
}

VOID
ScPixelContext::WriteOutputs()
{
    UINT Alpha = Raster->Words[1];

    if (OutputsWritten)
        return;

    /* ps_1_x colors are saturated */
    if (Version < 0xFFFF0200)
    {
        for (UINT Output = 0; Output < 4; Output += 1)
        {
            UINT Temp = OutputTemps[Output];

            if (Temp == ScNoRegister)
                continue;

            SB::COperandBase Dst = ScTempDst(Temp, ScMaskAll);
            SB::COperandBase Value = ScTempOperand(Temp);
            SB::CInstruction Move(D3D10_SB_OPCODE_MOV, Dst, Value);

            Move.m_bSaturate = TRUE;
            Asm->Add(Move);
        }
    }

    if ((Alpha & 0x20000000) && (Alpha & 0x1E00000) != 0x1000000)
        WriteAlphaTest();
    if (ScImplicitFog(Raster, Version))
        WriteFog();

    for (UINT Output = 0; Output < 4; Output += 1)
    {
        UINT Temp = OutputTemps[Output];

        if (Temp == ScNoRegister)
            continue;

        /* Render targets the state marks as BGRA get red and blue swapped */
        SB::COperandBase Value = ScSwizzled(ScTempOperand(Temp), (Raster->Words[3] & (1u << Output) & 0xF)
                                                                     ? ScSwz(2, 1, 0, 3)
                                                                     : ScSwzXYZW);
        SB::COperandDst Dst(D3D10_SB_OPERAND_TYPE_OUTPUT, Output);

        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
    }

    if (OutputTemps[4] != ScNoRegister)
    {
        SB::COperandBase Depth = ScComponent(ScTempOperand(OutputTemps[4]), 0);
        SB::COperandDst Dst(D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH);

        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Depth);
    }
    OutputsWritten = TRUE;
}
