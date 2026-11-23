/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter internals: the analysis, the token writer and the translation context
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <D3D12TranslationLayerDependencyIncludes.h>
#include <D3D12TranslationLayerIncludes.h>
#include <ShaderBinary.h>

#include "shaderconv.hpp"

namespace SB = D3D10ShaderBinary;

/* Direct3D 9 register types, with the one the converter adds for ps_1_x texture registers written as temps */
enum ScRegisterType : UINT
{
    ScRegTemp = 0,
    ScRegInput = 1,
    ScRegConst = 2,
    ScRegAddress = 3,
    ScRegTexture = 3,
    ScRegRasterOut = 4,
    ScRegAttributeOut = 5,
    ScRegTexcoordOut = 6,
    ScRegOutput = 6,
    ScRegConstInt = 7,
    ScRegColorOut = 8,
    ScRegDepthOut = 9,
    ScRegSampler = 10,
    ScRegConstBool = 14,
    ScRegLoop = 15,
    ScRegMisc = 17,
    ScRegLabel = 18,
    ScRegPredicate = 19,
    ScRegTextureTemp = 20,
};

/* Where a Direct3D 9 input register is read from */
enum ScInputKind : UINT
{
    ScInputUnbound = 0,
    ScInputDirect = 1,
    ScInputTemp = 2,
};

struct ScInputRegister
{
    ScInputKind Kind;
    BYTE Register;
    BYTE Mask;
    BYTE Reserved[2];
};

/* Slots of the special temp table: texture temps and a0 share the first four */
static constexpr UINT ScSlotAddress = 0;
static constexpr UINT ScSlotLoop = 4;
static constexpr UINT ScSlotPredicate = 5;

static constexpr BYTE ScNoRegister = 0xFF;

/* The Direct3D 10 temps each Direct3D 9 register lives in */
struct ScRegisterMap
{
    BYTE SamplerKinds[16];
    ScInputRegister Inputs[16];
    BYTE Temps[32];
    BYTE Special[8];

    ScRegisterMap();
};

/* Number of Direct3D 10 temps the converter keeps for its own use below the shader's */
static constexpr BYTE ScFirstShaderTemp = 10;

/* Temp a predicated instruction writes before the result is selected */
static constexpr UINT ScPredicateTemp = 9;

/* Everything the analysis pass learns about one shader */
struct ScShader
{
    std::vector<NineScConstant> Constants[NineScBufferCount];
    UINT RangeStart[NineScBufferCount];
    UINT RangeEnd[NineScBufferCount];
    BOOLEAN Relative[NineScBufferCount];
    ScRegisterMap Map;
    BYTE OutputTemps[12];
    std::vector<DWORD> Code;
    BYTE LoopCount;
    BYTE TempCount;
    UINT Version;
    UINT Flags;

    /* Vertex shaders only */
    NineScOutputs Outputs;
    NineScInputs Inputs;
    UINT VertexFlags;

    ScShader();
};

/* Vertex flag: outputs are indexed, so they live in an indexable temp */
static constexpr UINT ScVertexIndexedOutputs = 1;

/* The Direct3D 10 token writer with the counters the converter reports */
class ScAsm : public SB::CShaderAsm
{
public:
    UINT Options;
    UINT Emitted;
    UINT Patched;

    ScAsm() : Options(0), Emitted(0), Patched(0) {}

    VOID
    Begin(
        _In_ D3D10_SB_TOKENIZED_PROGRAM_TYPE Type,
        _In_ UINT Options);

    VOID
    Add(
        _In_ const SB::CInstruction& Instruction);

private:
    VOID
    GuardFactor(
        _In_ UINT Temp,
        _In_ const SB::COperandBase& Other,
        _Inout_ SB::COperandBase* Value);
};

/* Option: multiplicands that are zero win over infinities, as Direct3D 9 hardware does */
static constexpr UINT ScOptionPatchMultiply = 1;

/* One Direct3D 9 instruction as the translation reads it */
struct ScToken
{
    DWORD Token;
    DWORD Address;
};

class ScContext;

struct ScInstr
{
    UINT Opcode;
    DWORD OpcodeToken;
    UINT Length;
    ScToken Dst;
    UINT Flags;
    ScToken Src[5];
    UINT SrcCount;
    UINT Version;
    const ScRegisterMap* Map;
    const BYTE* OutputTemps;
    ScContext* Context;

    ScInstr(
        _In_ DWORD Token,
        _In_ ScContext* Owner);

    const DWORD*
    SetDst(
        _In_ const DWORD* Tokens);

    const DWORD*
    AddSrc(
        _In_ const DWORD* Tokens);

    VOID
    Destination(
        _Out_ SB::COperandBase* Operand) const;

    VOID
    Source(
        _Out_ SB::COperandBase* Operand,
        _In_ DWORD Token,
        _In_ UINT Offset) const;

    VOID
    Relative(
        _Inout_ SB::COperandBase* Operand,
        _In_ DWORD Address,
        _In_ UINT Dimension) const;
};

/* Instruction flags */
static constexpr UINT ScFlagShiftMask = 0xF;
static constexpr UINT ScFlagSaturate = 0x10;
static constexpr UINT ScFlagCentroid = 0x20;
static constexpr UINT ScFlagPartial = 0x40;
static constexpr UINT ScFlagPredicated = 0x80;
static constexpr UINT ScFlagCoissue = 0x100;

/* State shared by the vertex and pixel shader translations */
class ScContext
{
public:
    UINT RuntimeVersion;
    ScAsm* Asm;
    UINT Version;
    const NineScRasterStates* Raster;
    const ScRegisterMap* Map;
    const BYTE* OutputTemps;
    ScShader* Shader;
    BYTE InputTemps[16];
    UINT LoopTemps[4];
    /* Nesting of if, loop and rep blocks; ret below every block writes the outputs */
    BYTE IfDepth;
    BOOLEAN OutputsWritten;
    /* Index into LoopTemps of the innermost loop, 0xFF outside every loop */
    BYTE LoopDepth;
    /* Next temp a loop or rep counter takes */
    UINT TempBase;
    BOOLEAN LastWasBreak;

    ScContext(
        _In_ UINT Runtime,
        _In_ ScAsm* Writer,
        _In_ const NineScRasterStates* States,
        _In_ ScShader* Info);

    virtual ~ScContext() {}

    virtual HRESULT WriteDeclarations() = 0;
    virtual HRESULT TranslateInstructions();
    virtual VOID WriteOutputs() = 0;

    VOID
    SourceOperand(
        _Out_ SB::COperandBase* Operand,
        _In_ const ScInstr& Instr,
        _In_ UINT Index,
        _In_ UINT Offset,
        _In_ UINT Components);

    VOID
    InputOperand(
        _Out_ SB::COperandBase* Operand,
        _In_ UINT Register,
        _In_ UINT Swizzle) const;

    VOID
    Immediate(
        _Out_ SB::COperandBase* Operand,
        _Inout_ SB::COperandBase* Value,
        _In_ UINT Modifier,
        _In_ UINT Swizzle) const;

    VOID
    EmitResult(
        _In_ UINT Flags,
        _In_ D3D10_SB_OPCODE_TYPE Opcode,
        _In_ UINT Sampler,
        _In_ const SB::COperandBase& Dst,
        _In_ UINT SourceCount,
        _In_reads_(SourceCount) const SB::COperandBase* const* Sources);

    VOID
    EmitShift(
        _In_ UINT Shift,
        _In_ BOOL Saturate,
        _In_ const SB::COperandBase& Dst,
        _In_ const SB::COperandBase& Value);

    VOID
    EmitSamplerSwizzle(
        _In_ const SB::COperandBase& Dst,
        _In_ const SB::COperandBase& Value,
        _In_ UINT Mode);

    VOID
    EmitColorKey(
        _In_ UINT Sampler,
        _In_ const SB::COperandBase& Dst,
        _In_ const SB::COperandBase& Value);

    VOID
    WriteClipPlanes(
        _In_ UINT Planes,
        _In_ const SB::COperandBase& Position);

    template <class INPUTS>
    VOID
    AllocateInputTemps(
        _In_ const INPUTS& Inputs,
        _Inout_ BYTE* Next);

    VOID
    InitializeVertexInputTemps(
        _In_ const NineScInputs& Inputs,
        _In_ UINT TempCount);

    HRESULT
    Translate(
        _In_ ScInstr& Instr);

    /* Instruction translations */
    VOID TranslateMov(_In_ ScInstr& Instr);
    VOID TranslateBinary(_In_ ScInstr& Instr, _In_ D3D10_SB_OPCODE_TYPE Opcode);

    const DWORD*
    TranslateBreak(
        _In_ const DWORD* Next);

    VOID
    BeginLoop(
        _In_ ScInstr& Instr);

    VOID
    EndLoop(
        _In_ BOOL Loop);
};

/* Emits one instruction through the writer */
VOID
ScEmit(
    _In_ ScAsm* Asm,
    _In_ D3D10_SB_OPCODE_TYPE Opcode,
    _In_ UINT Count,
    ...);

class ScVertexContext : public ScContext
{
public:
    NineScInputs* Inputs;
    NineScOutputs* Outputs;
    UINT VertexFlags;

    ScVertexContext(
        _In_ UINT Runtime,
        _In_ ScAsm* Writer,
        _In_ const NineScRasterStates* States,
        _In_ ScShader* Info);

    HRESULT WriteDeclarations() override;
    VOID WriteOutputs() override;
};

/* Operand construction */
SB::COperandBase
ScTempOperand(
    _In_ UINT Register);

SB::COperandBase
ScTempDst(
    _In_ UINT Register,
    _In_ UINT Mask);

VOID
ScSetSwizzle(
    _Inout_ SB::COperandBase* Operand,
    _In_ UINT Swizzle);

SB::COperandBase
ScSwizzled(
    _In_ const SB::COperandBase& Operand,
    _In_ UINT Swizzle);

SB::COperandBase
ScNegated(
    _In_ const SB::COperandBase& Operand);

SB::COperandBase
ScAbsolute(
    _In_ const SB::COperandBase& Operand);

SB::COperandBase
ScComponent(
    _In_ const SB::COperandBase& Operand,
    _In_ UINT Component);

/* Direct3D 9 token decoding */
UINT
ScRegType(
    _In_ DWORD Token);

UINT
ScRegNumber(
    _In_ DWORD Token);

UINT
ScTokenWriteMask(
    _In_ DWORD Token);

UINT
ScTokenSwizzle(
    _In_ DWORD Token,
    _In_ UINT Components);

UINT
ScInstrLength(
    _In_ DWORD Token,
    _In_ DWORD Version);

UINT
ScMaskSwizzle(
    _In_ UINT Swizzle,
    _In_ UINT Mask);

UINT
ScSwizzleFromMask(
    _In_ UINT Mask);

UINT
ScInputIndex(
    _In_ DWORD Version,
    _In_ UINT Register);

static
inline
BOOL
ScIsPixel1x(
    _In_ DWORD Version)
{
    return (Version & 0xFFFF0000) == 0xFFFF0000 && Version < 0xFFFF0200;
}

/* Swizzles in the converter's encoding: bit 0 set, then two bits per component from bit 1 */
static
inline
constexpr
UINT
ScSwz(
    _In_ UINT X,
    _In_ UINT Y,
    _In_ UINT Z,
    _In_ UINT W)
{
    return 1 | (X << 1) | (Y << 3) | (Z << 5) | (W << 7);
}

static constexpr UINT ScSwzXYZW = ScSwz(0, 1, 2, 3);
static constexpr UINT ScSwzXXXX = ScSwz(0, 0, 0, 0);
static constexpr UINT ScSwzYYYY = ScSwz(1, 1, 1, 1);
static constexpr UINT ScSwzXYYY = ScSwz(0, 1, 1, 1);
static constexpr UINT ScSwzWWWW = ScSwz(3, 3, 3, 3);
static constexpr UINT ScSwzZZZZ = ScSwz(2, 2, 2, 2);
static constexpr UINT ScSwzXYZZ = ScSwz(0, 1, 2, 2);

/* Write masks */
static constexpr UINT ScMaskX = 0x10;
static constexpr UINT ScMaskY = 0x20;
static constexpr UINT ScMaskZ = 0x40;
static constexpr UINT ScMaskW = 0x80;
static constexpr UINT ScMaskAll = 0xF0;
