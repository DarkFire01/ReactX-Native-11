/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader programs: decoded instructions and the four lane interpreter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d11TokenizedProgramFormat.hpp>
#include <vector>

class RxWarpView;

/* Lanes run together; in pixel shaders they are one 2x2 quad: top left, top right, bottom left, bottom right */
static constexpr UINT RxWarpLanes = 4;
static constexpr UINT RxWarpMaxRegisters = 32;
static constexpr UINT RxWarpMaxOperands = 7;
static constexpr UINT RxWarpConstantBufferSlots = 14;

static constexpr UINT8 RxWarpNoRelative = 0xFF;

typedef struct _RX_WARP_INDEX
{
    UINT Immediate;
    UINT8 RelativeType;         /* D3D10_SB_OPERAND_TYPE, or RxWarpNoRelative */
    UINT8 RelativeComponent;
    UINT RelativeRegister;
    UINT RelativeElement;       /* for x#[n] used as the address */
} RX_WARP_INDEX;

typedef struct _RX_WARP_OPERAND
{
    UINT8 Type;                 /* D3D10_SB_OPERAND_TYPE */
    UINT8 Dimensions;
    UINT8 Mask;                 /* destination write mask */
    UINT8 Swizzle[4];
    UINT8 Negate;
    UINT8 Absolute;
    RX_WARP_INDEX Index[3];
    UINT Immediate[4];
} RX_WARP_OPERAND;

typedef struct _RX_WARP_INSTRUCTION
{
    UINT16 Opcode;
    UINT8 OperandCount;
    UINT8 Saturate;
    UINT8 NonZero;              /* test for conditional instructions */
    UINT8 ReturnType;           /* resinfo and sampleinfo */
    INT8 Offset[3];             /* texel offsets from the extended opcode */
    UINT Target;                /* matching else, endif, endloop or endswitch */
    UINT Extra;                 /* loop start, switch case list */
    RX_WARP_OPERAND Operands[RxWarpMaxOperands];
} RX_WARP_INSTRUCTION;

typedef struct _RX_WARP_INDEXABLE
{
    UINT Size;
    UINT Offset;                /* into the invocation's indexable storage, in registers */
} RX_WARP_INDEXABLE;

class RxWarpShader
{
public:
    HRESULT
    Initialize(
        _In_ const UINT *Code);

    UINT Type = 0;              /* D3D10_SB_TOKENIZED_PROGRAM_TYPE */
    UINT TempCount = 0;
    UINT IndexableRegisters = 0;
    std::vector<RX_WARP_INDEXABLE> Indexables;
    std::vector<UINT> ImmediateConstants;
    std::vector<RX_WARP_INSTRUCTION> Program;
    std::vector<std::vector<UINT>> SwitchCases;

    UINT8 InputMode[RxWarpMaxRegisters] = {};   /* D3D10_SB_INTERPOLATION_MODE */
    UINT8 InputMask[RxWarpMaxRegisters] = {};
    UINT8 InputName[RxWarpMaxRegisters] = {};   /* D3D10_SB_NAME */
    UINT8 OutputMask[RxWarpMaxRegisters] = {};
    UINT8 OutputName[RxWarpMaxRegisters] = {};
    UINT InputCount = 0;
    UINT OutputCount = 0;
    UINT PositionOutput = UINT_MAX;
    UINT PositionInput = UINT_MAX;
    UINT ResourceDimension[128] = {};
    UINT ResourceReturn[128] = {};
    BOOLEAN WritesDepth = FALSE;
    BOOLEAN Discards = FALSE;
};

typedef struct _RX_WARP_SAMPLER
{
    D3D10_DDI_SAMPLER_DESC Desc;
} RX_WARP_SAMPLER;

/* What one stage sees: constant buffers, views and samplers */
typedef struct _RX_WARP_STAGE_BINDINGS
{
    const BYTE *ConstantBuffer[RxWarpConstantBufferSlots];
    UINT ConstantCount[RxWarpConstantBufferSlots];
    RxWarpView *Views[128];
    const RX_WARP_SAMPLER *Samplers[16];
} RX_WARP_STAGE_BINDINGS;

/* One run of a program over four lanes */
class RxWarpInvocation
{
public:
    VOID
    Prepare(
        _In_ const RxWarpShader *Shader);

    VOID
    Run(
        _In_ const RxWarpShader *Shader,
        _In_ const RX_WARP_STAGE_BINDINGS *Bindings);

    UINT *
    Input(
        _In_ UINT Register,
        _In_ UINT Lane)
    {
        return Inputs[Register][Lane];
    }

    UINT *
    Output(
        _In_ UINT Register,
        _In_ UINT Lane)
    {
        return Outputs[Register][Lane];
    }

    UINT Inputs[RxWarpMaxRegisters][RxWarpLanes][4];
    UINT Outputs[RxWarpMaxRegisters][RxWarpLanes][4];
    FLOAT Depth[RxWarpLanes];
    UINT PrimitiveId = 0;
    UINT Live = 0xF;            /* lanes that run */
    UINT Helper = 0;            /* lanes that run only for derivatives */
    UINT Discarded = 0;
    std::vector<UINT> Temps;
    std::vector<UINT> IndexableStorage;
};
