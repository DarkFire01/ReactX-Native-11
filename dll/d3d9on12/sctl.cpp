/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: the vertex shader for pretransformed vertices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

static constexpr UINT ScTlPosition = 0;
static constexpr UINT ScTlPositionT = 9;
static constexpr UINT ScTlColor = 10;
static constexpr UINT ScTlFog = 11;

/* Input flag: the position is in screen space and goes back to clip space */
static constexpr NineScInput ScTlScreenSpace = 0x1000;

/* Constant buffer 3 rows holding the screen to clip space offset and scale */
static constexpr UINT ScTlOffsetRow = 8;
static constexpr UINT ScTlScaleRow = 9;

/* The inputs and outputs the shader is built from: every element but POSITIONT, passed through */
struct ScTlShader
{
    NineScInputs Inputs;
    NineScOutputs Outputs;
};

static
VOID
ScTlDescribe(
    _In_ const NineScInputs* Declared,
    _Out_ ScTlShader* Shader)
{
    Shader->Inputs.UsageMask = 0;
    Shader->Inputs.Limit = 16;
    ZeroMemory(&Shader->Outputs, sizeof(Shader->Outputs));

    for (const NineScInput& Input : Declared->Inputs)
    {
        UINT Usage = NineScInputUsage(Input);
        UINT Index = NineScInputIndex(Input);
        UINT Register = NineScInputRegister(Input);

        if (Usage == ScTlPositionT)
            continue;
        ScAddInput(&Shader->Inputs, Usage, Index, Register, (Input & ScTlScreenSpace) != 0, 0);
        ScAddOutput(&Shader->Outputs, Usage, Index, Register, 0xF0000, FALSE);
    }
}

static
HRESULT
ScTlDeclare(
    _In_ ScAsm* Asm,
    _In_ const ScTlShader* Shader)
{
    Asm->EmitTempsDecl(12);
    Asm->EmitConstantBufferDecl(3, 10, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);

    for (const NineScInput& Input : Shader->Inputs.Inputs)
        Asm->EmitInputDecl(D3D10_SB_OPERAND_TYPE_INPUT, NineScInputRegister(Input), ScMaskAll);

    for (const NineScInput& Input : Shader->Inputs.Inputs)
    {
        UINT Usage = NineScInputUsage(Input);
        BOOL First = NineScInputIndex(Input) == 0;
        UINT Register = NineScInputRegister(Input);

        if (Usage == ScTlPositionT && First)
            return E_FAIL;
        if (Usage == ScTlPosition && First)
            Asm->EmitOutputSystemInterpretedValueDecl(Register, ScMaskAll, D3D10_SB_NAME_POSITION);
        else
            Asm->EmitOutputDecl(Register, ScMaskAll);
    }
    return S_OK;
}

/* Undoes the viewport mapping: clip = (screen + offset) * scale / rhw with w = 1 / rhw */
static
VOID
ScTlUnproject(
    _In_ ScAsm* Asm)
{
    SB::COperandBase Position = SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, 0);
    SB::COperandBase Offset = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, ScTlOffsetRow);
    SB::COperandBase Scale = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, ScTlScaleRow);
    SB::COperandBase Moved = ScTempDst(0, ScMaskAll);
    SB::COperandBase Scaled = ScTempDst(1, ScMaskAll);
    SB::COperandBase Inverse = ScTempDst(2, ScMaskX);
    SB::COperandBase Rhw = ScSwizzled(Position, ScSwzWWWW);
    SB::COperandBase W = ScSwizzled(ScTempOperand(2), ScSwzXXXX);
    SB::COperandBase Clip = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, ScMaskAll);
    SB::COperandBase ClipW = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, 0, ScMaskW);
    SB::COperandBase MovedValue = ScTempOperand(0);
    SB::COperandBase ScaledValue = ScTempOperand(1);

    ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Moved, &Position, &Offset);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &Scaled, &MovedValue, &Scale);
    ScEmit(Asm, D3D11_SB_OPCODE_RCP, 2, &Inverse, &Rhw);
    ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &Clip, &W, &ScaledValue);
    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &ClipW, &W);
}

static
VOID
ScTlPassThrough(
    _In_ ScAsm* Asm,
    _In_ UINT RuntimeVersion,
    _In_ const ScTlShader* Shader)
{
    for (const NineScInput& Input : Shader->Inputs.Inputs)
    {
        UINT Usage = NineScInputUsage(Input);
        BOOL First = NineScInputIndex(Input) == 0;
        UINT Register = NineScInputRegister(Input);
        BOOL Saturate = FALSE;

        if (Usage == ScTlPositionT && First)
            continue;
        if (Usage == ScTlPosition && First && (Input & ScTlScreenSpace))
        {
            ScTlUnproject(Asm);
            continue;
        }
        if (Usage == ScTlColor)
            Saturate = TRUE;
        else if (Usage == ScTlFog)
            Saturate = RuntimeVersion < 9;

        SB::COperandBase Dst = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Register, ScMaskAll);
        SB::COperandBase Source = SB::COperand4(D3D10_SB_OPERAND_TYPE_INPUT, Register);
        SB::CInstruction Move(D3D10_SB_OPCODE_MOV, Dst, Source);

        Move.m_bSaturate = Saturate;
        Asm->Add(Move);
    }
}

HRESULT
NineScConvertTlShader(
    _Inout_ NineScTranslator** Translator,
    _Inout_ NineScTlArgs* Args)
{
    NineScTranslator* Owner = *Translator;
    ScTlShader Shader;
    ScAsm* Asm;
    HRESULT hr;

    if (!Owner)
    {
        Owner = ScCreateTranslator(Args->Version);
        if (!Owner)
            return E_OUTOFMEMORY;
        *Translator = Owner;
    }

    Asm = Owner->Asm;
    ScTlDescribe(Args->Inputs, &Shader);
    Asm->Begin(D3D10_SB_VERTEX_SHADER, Args->Flags);
    hr = ScTlDeclare(Asm, &Shader);
    if (SUCCEEDED(hr))
    {
        ScTlPassThrough(Asm, Owner->RuntimeVersion, &Shader);
        Asm->EndShader();

        Args->TokenSize = Asm->ShaderSizeInDWORDs() * sizeof(DWORD);
        Args->Tokens = malloc(Args->TokenSize);
        if (!Args->Tokens)
        {
            Args->TokenSize = 0;
            hr = E_OUTOFMEMORY;
        }
        else
        {
            NineScOutputs* Outputs = Args->Outputs;

            memcpy(Args->Tokens, Asm->GetShader(), Args->TokenSize);
            Outputs->Flags = Shader.Outputs.Flags;
            Outputs->Indexed = (Outputs->Indexed & ~((1ull << 45) - 1)) | Shader.Outputs.Indexed;
            memcpy(Outputs->Outputs, Shader.Outputs.Outputs, Shader.Outputs.Count * sizeof(NineScOutput));
            Outputs->Count = Shader.Outputs.Count;
        }
    }

    Args->Emitted = Asm->Emitted;
    Args->Patched = Asm->Patched;
    return hr;
}
