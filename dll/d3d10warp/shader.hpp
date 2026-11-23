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
    UINT Control;               /* the opcode token, for flags like the sync kind */
    RX_WARP_OPERAND Operands[RxWarpMaxOperands];
} RX_WARP_INSTRUCTION;

/* Block bookkeeping for structured flow; masks hold one bit per lane */
typedef struct _RX_WARP_FRAME
{
    UINT16 Opcode;
    UINT Start;             /* first instruction of a loop body */
    UINT Saved;             /* lanes running when the block began */
    UINT Else;              /* lanes for the else branch */
    UINT Removed;           /* lanes that left through break or continue of an outer block */
    UINT Continued;         /* loop lanes waiting for the next iteration */
    UINT Selector;          /* switch: index into the case lists */
    UINT Matched;           /* switch: lanes that already took a case */
} RX_WARP_FRAME;

/* Thread group shared memory g#: bytes, and the structure stride when structured */
typedef struct _RX_WARP_SHARED
{
    UINT Offset;
    UINT Size;
    UINT Stride;
} RX_WARP_SHARED;

typedef struct _RX_WARP_INDEXABLE
{
    UINT Size;
    UINT Offset;                /* into the invocation's indexable storage, in registers */
} RX_WARP_INDEXABLE;

class RxWarpInvocation;

/* eval_* in pixel shaders: an input evaluated somewhere other than where it was interpolated */
typedef enum _RX_WARP_EVALUATE_KIND
{
    RxWarpEvaluateSnapped,
    RxWarpEvaluateSample,
    RxWarpEvaluateCentroid
} RX_WARP_EVALUATE_KIND;

typedef VOID (*RX_WARP_EVALUATE_ROUTINE)(
    _In_ VOID *Context,
    _In_ UINT Lane,
    _In_ UINT Register,
    _In_ RX_WARP_EVALUATE_KIND Kind,
    _In_reads_(2) const INT *Parameter,
    _Out_writes_(4) UINT *Value);

/* A pixel shader input recomputed in double precision, as the reference does for texture coordinates */
typedef BOOLEAN (*RX_WARP_PRECISE_ROUTINE)(
    _In_ VOID *Context,
    _In_ UINT Lane,
    _In_ UINT Register,
    _Out_writes_(4) UINT *Value);

/* Called by emit and cut in geometry shaders, once per running lane */
typedef VOID (*RX_WARP_EMIT_ROUTINE)(
    _In_ VOID *Context,
    _In_ RxWarpInvocation *Invocation,
    _In_ UINT Stream,
    _In_ UINT Lane,
    _In_ BOOLEAN Emit,
    _In_ BOOLEAN Cut);

class RxWarpShader
{
public:
    HRESULT
    Initialize(
        _In_ const UINT *Code);

    HRESULT
    Decode(
        _In_ const UINT *Code,
        _In_ UINT Position,
        _In_ UINT Length);

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
    UINT8 ClipMask[RxWarpMaxRegisters] = {};    /* components holding SV_ClipDistance */
    UINT8 CullMask[RxWarpMaxRegisters] = {};
    UINT InputCount = 0;
    UINT OutputCount = 0;
    UINT PositionOutput = UINT_MAX;
    UINT PositionInput = UINT_MAX;
    UINT ResourceDimension[128] = {};
    UINT ResourceReturn[128] = {};
    UINT ResourceStride[128] = {};
    UINT UavDimension[64] = {};
    UINT UavStride[64] = {};
    UINT ConstantMask = 0;              /* declared cb# slots, a bit each */
    UINT SamplerMask = 0;               /* declared s# slots */
    UINT GroupSize[3] = { 1, 1, 1 };
    std::vector<RX_WARP_SHARED> Shared;
    UINT SharedBytes = 0;

    /* Lowered DXIL: class, slot, space and register for each slot not named by its own register */
    std::vector<UINT> SlotSources;

    /* Geometry shaders, and the stream output that may come with them */
    UINT GsInputPrimitive = 0;          /* D3D10_SB_PRIMITIVE */
    UINT GsOutputTopology = 0;          /* D3D10_SB_PRIMITIVE_TOPOLOGY */
    UINT GsMaxVertices = 0;
    UINT GsInstances = 1;
    BOOLEAN PassThrough = FALSE;        /* stream output with no program: the vertex shader's output */
    std::vector<D3D11DDIARG_STREAM_OUTPUT_DECLARATION_ENTRY> StreamDecl;
    UINT StreamStrides[4] = {};
    UINT RasterizedStream = 0;

    /* Hull and domain shaders; a hull shader's phases are programs of their own */
    UINT TessDomain = 0;                /* D3D11_SB_TESSELLATOR_DOMAIN */
    UINT TessPartitioning = 0;
    UINT TessOutput = 0;
    UINT InputControlPoints = 0;
    UINT OutputControlPoints = 0;
    UINT PhaseKind = 0;                 /* the phase opcode, for phases */
    UINT PhaseInstances = 1;
    UINT TessFactorSlot[12] = {};       /* register * 4 + component for each tess factor name, plus one */
    std::vector<RxWarpShader> Phases;

    BOOLEAN WritesDepth = FALSE;
    BOOLEAN WritesCoverage = FALSE;
    BOOLEAN SampleRate = FALSE;         /* reads the sample index or interpolates at samples */
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
    RxWarpView *Uavs[64];
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

    /* Starts or continues the program; TRUE when it stopped at a group barrier */
    BOOLEAN
    Resume(
        _In_ const RxWarpShader *Shader,
        _In_ const RX_WARP_STAGE_BINDINGS *Bindings,
        _In_ BOOLEAN Restart);

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
    UINT CoverageIn[RxWarpLanes] = {};      /* SV_Coverage as the pixel shader reads it */
    UINT CoverageOut[RxWarpLanes] = {};     /* oMask */
    INT ByteShadow[RxWarpMaxRegisters][RxWarpLanes][4];    /* an output still holding a filtered 8.7 value, else -1 */
    UINT SampleIndex[RxWarpLanes] = {};
    UINT RasterSamples = 1;                 /* the bound targets' sample count, for the rasterizer operand */
    RX_WARP_EVALUATE_ROUTINE EvaluateRoutine = nullptr;
    VOID *EvaluateContext = nullptr;
    RX_WARP_PRECISE_ROUTINE PreciseRoutine = nullptr;
    UINT PrimitiveId = 0;
    UINT Live = 0xF;            /* lanes that run */
    UINT Helper = 0;            /* lanes that run only for derivatives */
    UINT Discarded = 0;
    std::vector<UINT> Temps;
    std::vector<UINT> IndexableStorage;

    /* Compute: the group, each lane's place in it, and the group's shared memory */
    UINT GroupId[3] = {};
    UINT ThreadInGroup[RxWarpLanes][3] = {};
    UINT ThreadId[RxWarpLanes][3] = {};
    UINT Flattened[RxWarpLanes] = {};
    BYTE *SharedMemory = nullptr;

    /* Geometry: the input primitive's vertices, each a block of registers, and where emits go */
    const UINT *InputVertex[32] = {};
    UINT InputVertexCount = 0;
    UINT GsInstance = 0;
    RX_WARP_EMIT_ROUTINE EmitRoutine = nullptr;
    VOID *EmitContext = nullptr;

    /* Tessellation: the hull shader's output points, the patch constants and each lane's domain point */
    const UINT *OutputPoint[32] = {};
    UINT OutputPointCount = 0;
    const UINT *PatchConstants = nullptr;
    UINT ControlPointId[RxWarpLanes] = {};
    UINT PhaseInstance = 0;
    FLOAT Domain[RxWarpLanes][4] = {};

    /* Where the program stands, so it can stop at a barrier and continue */
    std::vector<RX_WARP_FRAME> Frames;
    UINT Active = 0;
    UINT Dead = 0;
    UINT Next = 0;
};
