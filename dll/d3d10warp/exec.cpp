/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs decoded shader programs over four lanes with per lane masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "sample.hpp"
#include "dxil.hpp"

typedef UINT RX_WARP_LANE_VALUES[RxWarpLanes][4];

static
FLOAT
RxWarpAsFloat(
    _In_ UINT Value)
{
    FLOAT Result;

    memcpy(&Result, &Value, sizeof(Result));
    return Result;
}

static
UINT
RxWarpAsUint(
    _In_ FLOAT Value)
{
    UINT Result;

    memcpy(&Result, &Value, sizeof(Result));
    return Result;
}

/* Saturate maps NaN to zero */
static
FLOAT
RxWarpSaturate(
    _In_ FLOAT Value)
{
    if (!(Value > 0.0f))
        return 0.0f;
    return (Value < 1.0f) ? Value : 1.0f;
}

static
INT
RxWarpFloatToInt(
    _In_ FLOAT Value)
{
    if (Value != Value)
        return 0;
    if (Value >= 2147483648.0f)
        return INT_MAX;
    if (Value <= -2147483648.0f)
        return INT_MIN;
    return (INT)Value;
}

static
UINT
RxWarpFloatToUint(
    _In_ FLOAT Value)
{
    if (!(Value > 0.0f))
        return 0;
    if (Value >= 4294967296.0f)
        return UINT_MAX;
    return (UINT)Value;
}

static
UINT
RxWarpReverseBits(
    _In_ UINT Value)
{
    UINT Result = 0;

    for (UINT Bit = 0; Bit < 32; Bit++)
        Result |= ((Value >> Bit) & 1) << (31 - Bit);
    return Result;
}

static
UINT
RxWarpCountBits(
    _In_ UINT Value)
{
    UINT Count = 0;

    while (Value != 0)
    {
        Value &= Value - 1;
        Count++;
    }
    return Count;
}

/* Bit position counted from the top; UINT_MAX when no bit is set */
static
UINT
RxWarpFirstHigh(
    _In_ UINT Value)
{
    for (UINT Bit = 0; Bit < 32; Bit++)
    {
        if (Value & (0x80000000u >> Bit))
            return Bit;
    }
    return UINT_MAX;
}

static
UINT
RxWarpFirstLow(
    _In_ UINT Value)
{
    for (UINT Bit = 0; Bit < 32; Bit++)
    {
        if (Value & (1u << Bit))
            return Bit;
    }
    return UINT_MAX;
}


/* Memory an instruction addresses by bytes: a buffer view, a texture view, or group shared memory */
typedef struct _RX_WARP_REGION
{
    BYTE *Data;
    SIZE_T Size;
    UINT Stride;
    RxWarpView *View;
    UINT Dimension;
    BOOLEAN Typed;
} RX_WARP_REGION;

class RxWarpMachine
{
public:
    RxWarpMachine(
        _In_ const RxWarpShader *Program,
        _In_ RxWarpInvocation *Context,
        _In_ const RX_WARP_STAGE_BINDINGS *Stage)
        : Shader(Program), Invocation(Context), Bindings(Stage), Frames(Context->Frames), Active(Context->Active),
          Dead(Context->Dead), Next(Context->Next)
    {
    }

    /* Runs until the end, or until a barrier that the whole group waits on; TRUE at a barrier */
    BOOLEAN
    Execute(VOID);

private:
    UINT *
    Locate(
        _In_ const RX_WARP_OPERAND &Operand,
        _In_ UINT Lane,
        _Out_ BOOLEAN *Writable);

    INT
    ResolveIndex(
        _In_ const RX_WARP_INDEX &Index,
        _In_ UINT Lane);

    VOID
    Fetch(
        _In_ const RX_WARP_OPERAND &Operand,
        _In_ UINT Lane,
        _In_ BOOLEAN Integer,
        _Out_writes_(4) UINT *Value);

    VOID
    FetchAll(
        _In_ const RX_WARP_OPERAND &Operand,
        _In_ BOOLEAN Integer,
        _Out_ RX_WARP_LANE_VALUES Values);

    VOID
    Store(
        _In_ const RX_WARP_INSTRUCTION &Instruction,
        _In_ UINT OperandIndex,
        _In_ RX_WARP_LANE_VALUES Values,
        _In_ BOOLEAN FloatResult);

    UINT
    Condition(
        _In_ const RX_WARP_INSTRUCTION &Instruction,
        _In_ const RX_WARP_OPERAND &Operand);

    VOID
    Leave(
        _In_ UINT Lanes,
        _In_ BOOLEAN Continue);

    VOID
    Arithmetic(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    FetchDoubles(
        _In_ const RX_WARP_OPERAND &Operand,
        _Out_ double (*Values)[2]);

    VOID
    Doubles(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    MaskedDifference(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Evaluate(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Texture(
        _In_ const RX_WARP_INSTRUCTION &Instruction,
        _In_opt_ const RX_WARP_OPERAND *Clamp = nullptr);

    VOID
    TextureFeedback(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    FullyMapped(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Wave(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Operate(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    HalfTowardZero(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Long(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    PairScatter(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    HalfToFloat(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Grouped(
        _In_ const RX_WARP_INSTRUCTION &Prefix,
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    VOID
    Access(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    BOOLEAN
    Region(
        _In_ const RX_WARP_OPERAND &Operand,
        _Out_ RX_WARP_REGION *Region);

    const RxWarpShader *Shader;
    RxWarpInvocation *Invocation;
    const RX_WARP_STAGE_BINDINGS *Bindings;
    std::vector<RX_WARP_FRAME> &Frames;
    UINT &Active;
    UINT &Dead;
    UINT &Next;
    UINT Zero[4] = {};
    UINT Scratch[4] = {};
};

INT
RxWarpMachine::ResolveIndex(
    _In_ const RX_WARP_INDEX &Index,
    _In_ UINT Lane)
{
    RX_WARP_OPERAND Relative = {};
    UINT Value[4];

    if (Index.RelativeType == RxWarpNoRelative)
        return (INT)Index.Immediate;

    Relative.Type = Index.RelativeType;
    Relative.Dimensions = (Index.RelativeType == D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP) ? 2 : 1;
    Relative.Index[0].Immediate = Index.RelativeRegister;
    Relative.Index[0].RelativeType = RxWarpNoRelative;
    Relative.Index[1].Immediate = Index.RelativeElement;
    Relative.Index[1].RelativeType = RxWarpNoRelative;
    for (UINT Component = 0; Component < 4; Component++)
        Relative.Swizzle[Component] = Index.RelativeComponent;
    Fetch(Relative, Lane, TRUE, Value);
    return (INT)Index.Immediate + (INT)Value[0];
}

UINT *
RxWarpMachine::Locate(
    _In_ const RX_WARP_OPERAND &Operand,
    _In_ UINT Lane,
    _Out_ BOOLEAN *Writable)
{
    INT First;
    INT Second;

    *Writable = TRUE;
    switch (Operand.Type)
    {
        case D3D10_SB_OPERAND_TYPE_TEMP:
            First = ResolveIndex(Operand.Index[0], Lane);
            if (First < 0 || (UINT)First >= Shader->TempCount)
                break;
            return &Invocation->Temps[((SIZE_T)First * RxWarpLanes + Lane) * 4];
        case D3D10_SB_OPERAND_TYPE_INPUT:
            /* Geometry shaders read v[vertex][register] from the primitive's vertices */
            if (Operand.Dimensions > 1 && Invocation->InputVertexCount != 0)
            {
                First = ResolveIndex(Operand.Index[0], Lane);
                Second = ResolveIndex(Operand.Index[1], Lane);
                if (First < 0 || (UINT)First >= Invocation->InputVertexCount || Second < 0 || (UINT)Second >= RxWarpMaxRegisters)
                    break;
                *Writable = FALSE;
                return (UINT *)Invocation->InputVertex[First] + (SIZE_T)Second * 4;
            }
            First = ResolveIndex(Operand.Index[Operand.Dimensions > 1 ? 1 : 0], Lane);
            if (First < 0 || (UINT)First >= RxWarpMaxRegisters)
                break;
            *Writable = FALSE;
            return Invocation->Inputs[First][Lane];
        case D3D10_SB_OPERAND_TYPE_OUTPUT:
            First = ResolveIndex(Operand.Index[0], Lane);
            if (First < 0 || (UINT)First >= RxWarpMaxRegisters)
                break;
            return Invocation->Outputs[First][Lane];
        case D3D10_SB_OPERAND_TYPE_INDEXABLE_TEMP:
        {
            First = ResolveIndex(Operand.Index[0], Lane);
            Second = ResolveIndex(Operand.Index[1], Lane);
            if (First < 0 || (UINT)First >= Shader->Indexables.size())
                break;
            const RX_WARP_INDEXABLE &Array = Shader->Indexables[First];
            if (Second < 0 || (UINT)Second >= Array.Size)
                break;
            return &Invocation->IndexableStorage[(((SIZE_T)Array.Offset + Second) * RxWarpLanes + Lane) * 4];
        }
        case D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER:
            *Writable = FALSE;
            First = ResolveIndex(Operand.Index[0], Lane);
            Second = ResolveIndex(Operand.Index[1], Lane);
            if (First < 0 || First >= RxWarpConstantBufferSlots)
                break;
            if (Bindings->ConstantBuffer[First] == nullptr || Second < 0 || (UINT)Second >= Bindings->ConstantCount[First])
                break;
            return (UINT *)(Bindings->ConstantBuffer[First] + (SIZE_T)Second * 16);
        case D3D10_SB_OPERAND_TYPE_IMMEDIATE_CONSTANT_BUFFER:
            *Writable = FALSE;
            First = ResolveIndex(Operand.Index[0], Lane);
            if (First < 0 || (SIZE_T)First * 4 + 4 > Shader->ImmediateConstants.size())
                break;
            return (UINT *)&Shader->ImmediateConstants[(SIZE_T)First * 4];
        case D3D10_SB_OPERAND_TYPE_IMMEDIATE32:
            *Writable = FALSE;
            return (UINT *)Operand.Immediate;
        case D3D10_SB_OPERAND_TYPE_INPUT_PRIMITIVEID:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->PrimitiveId;
            return Scratch;
        case D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH:
        case D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_GREATER_EQUAL:
        case D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL:
            return (UINT *)&Invocation->Depth[Lane];
        case D3D11_SB_OPERAND_TYPE_INPUT_COVERAGE_MASK:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->CoverageIn[Lane];
            return Scratch;
        case D3D10_SB_OPERAND_TYPE_OUTPUT_COVERAGE_MASK:
            return &Invocation->CoverageOut[Lane];
        case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID:
            *Writable = FALSE;
            memcpy(Scratch, Invocation->ThreadId[Lane], 3 * sizeof(UINT));
            Scratch[3] = 0;
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_GROUP_ID:
            *Writable = FALSE;
            memcpy(Scratch, Invocation->GroupId, 3 * sizeof(UINT));
            Scratch[3] = 0;
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP:
            *Writable = FALSE;
            memcpy(Scratch, Invocation->ThreadInGroup[Lane], 3 * sizeof(UINT));
            Scratch[3] = 0;
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_GS_INSTANCE_ID:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->GsInstance;
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_THREAD_ID_IN_GROUP_FLATTENED:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->Flattened[Lane];
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_CONTROL_POINT:
        case D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT:
        {
            BOOLEAN Outputs = (Operand.Type == D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT);
            UINT Count = Outputs ? Invocation->OutputPointCount : Invocation->InputVertexCount;

            *Writable = FALSE;
            First = ResolveIndex(Operand.Index[0], Lane);
            Second = ResolveIndex(Operand.Index[1], Lane);
            if (First < 0 || (UINT)First >= Count || Second < 0 || (UINT)Second >= RxWarpMaxRegisters)
                break;
            return (UINT *)(Outputs ? Invocation->OutputPoint[First] : Invocation->InputVertex[First]) + (SIZE_T)Second * 4;
        }
        case D3D11_SB_OPERAND_TYPE_INPUT_PATCH_CONSTANT:
            *Writable = FALSE;
            First = ResolveIndex(Operand.Index[0], Lane);
            if (Invocation->PatchConstants == nullptr || First < 0 || (UINT)First >= RxWarpMaxRegisters)
                break;
            return (UINT *)Invocation->PatchConstants + (SIZE_T)First * 4;
        case D3D11_SB_OPERAND_TYPE_INPUT_DOMAIN_POINT:
            *Writable = FALSE;
            return (UINT *)Invocation->Domain[Lane];
        case D3D11_SB_OPERAND_TYPE_OUTPUT_CONTROL_POINT_ID:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->ControlPointId[Lane];
            return Scratch;
        case D3D11_SB_OPERAND_TYPE_INPUT_FORK_INSTANCE_ID:
        case D3D11_SB_OPERAND_TYPE_INPUT_JOIN_INSTANCE_ID:
            *Writable = FALSE;
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = Invocation->PhaseInstance;
            return Scratch;
        default:
            break;
    }
    *Writable = FALSE;
    return Zero;
}

VOID
RxWarpMachine::Fetch(
    _In_ const RX_WARP_OPERAND &Operand,
    _In_ UINT Lane,
    _In_ BOOLEAN Integer,
    _Out_writes_(4) UINT *Value)
{
    BOOLEAN Writable;
    const UINT *Source = Locate(Operand, Lane, &Writable);
    UINT Raw[4];

    memcpy(Raw, Source, sizeof(Raw));
    for (UINT Component = 0; Component < 4; Component++)
    {
        UINT Bits = Raw[Operand.Swizzle[Component] & 3];

        if (Integer)
        {
            if (Operand.Absolute)
                Bits = (UINT)abs((INT)Bits);
            if (Operand.Negate)
                Bits = (UINT)(-(INT)Bits);
        }
        else
        {
            if (Operand.Absolute)
                Bits &= 0x7FFFFFFF;
            if (Operand.Negate)
                Bits ^= 0x80000000;
        }
        Value[Component] = Bits;
    }
}

VOID
RxWarpMachine::FetchAll(
    _In_ const RX_WARP_OPERAND &Operand,
    _In_ BOOLEAN Integer,
    _Out_ RX_WARP_LANE_VALUES Values)
{
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        Fetch(Operand, Lane, Integer, Values[Lane]);
}

VOID
RxWarpMachine::Store(
    _In_ const RX_WARP_INSTRUCTION &Instruction,
    _In_ UINT OperandIndex,
    _In_ RX_WARP_LANE_VALUES Values,
    _In_ BOOLEAN FloatResult)
{
    const RX_WARP_OPERAND &Operand = Instruction.Operands[OperandIndex];

    if (Operand.Type == D3D10_SB_OPERAND_TYPE_NULL)
        return;

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        BOOLEAN Writable;
        UINT *Target;

        if (!(Active & (1u << Lane)))
            continue;
        Target = Locate(Operand, Lane, &Writable);
        if (!Writable)
            continue;

        /* Depth output is a single value */
        if (Operand.Type == D3D10_SB_OPERAND_TYPE_OUTPUT_DEPTH ||
            Operand.Type == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_GREATER_EQUAL ||
            Operand.Type == D3D11_SB_OPERAND_TYPE_OUTPUT_DEPTH_LESS_EQUAL)
        {
            UINT Bits = Values[Lane][0];

            if (Instruction.Saturate && FloatResult)
                Bits = RxWarpAsUint(RxWarpSaturate(RxWarpAsFloat(Bits)));
            *Target = Bits;
            continue;
        }

        for (UINT Component = 0; Component < 4; Component++)
        {
            UINT Bits;

            if (!(Operand.Mask & (1u << Component)))
                continue;
            Bits = Values[Lane][Component];
            if (Instruction.Saturate && FloatResult)
                Bits = RxWarpAsUint(RxWarpSaturate(RxWarpAsFloat(Bits)));
            Target[Component] = Bits;
        }
    }
}

/* Lanes whose first selected component passes the instruction's zero test */
UINT
RxWarpMachine::Condition(
    _In_ const RX_WARP_INSTRUCTION &Instruction,
    _In_ const RX_WARP_OPERAND &Operand)
{
    UINT Lanes = 0;

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT Value[4];

        if (!(Active & (1u << Lane)))
            continue;
        Fetch(Operand, Lane, TRUE, Value);
        if ((Value[0] != 0) == (Instruction.NonZero != 0))
            Lanes |= 1u << Lane;
    }
    return Lanes;
}

/* Takes lanes out through break or continue: every block between here and the target remembers them */
VOID
RxWarpMachine::Leave(
    _In_ UINT Lanes,
    _In_ BOOLEAN Continue)
{
    for (SIZE_T Index = Frames.size(); Index-- > 0;)
    {
        RX_WARP_FRAME &Frame = Frames[Index];

        if (Frame.Opcode == D3D10_SB_OPCODE_LOOP || (!Continue && Frame.Opcode == D3D10_SB_OPCODE_SWITCH))
        {
            if (Continue)
                Frame.Continued |= Lanes;
            break;
        }
        Frame.Removed |= Lanes;
    }
    Active &= ~Lanes;
}

BOOLEAN
RxWarpMachine::Execute(VOID)
{
    const std::vector<RX_WARP_INSTRUCTION> &Program = Shader->Program;

    while (Next < Program.size())
    {
        const RX_WARP_INSTRUCTION &Instruction = Program[Next];
        UINT Current = Next++;
        UINT Lanes;

        switch (Instruction.Opcode)
        {
            case D3D10_SB_OPCODE_IF:
            {
                RX_WARP_FRAME Frame = {};

                Lanes = Condition(Instruction, Instruction.Operands[0]);
                Frame.Opcode = D3D10_SB_OPCODE_IF;
                Frame.Saved = Active;
                Frame.Else = Active & ~Lanes;
                Frames.push_back(Frame);
                Active = Lanes;
                /* Nobody takes the branch: go straight to the else or the endif */
                if (Active == 0)
                    Next = Instruction.Target;
                break;
            }
            case D3D10_SB_OPCODE_ELSE:
            {
                RX_WARP_FRAME &Frame = Frames.back();

                Active = Frame.Else & ~Frame.Removed & ~Dead;
                if (Active == 0)
                    Next = Instruction.Target;
                break;
            }
            case D3D10_SB_OPCODE_ENDIF:
            {
                RX_WARP_FRAME Frame = Frames.back();

                Frames.pop_back();
                Active = Frame.Saved & ~Frame.Removed & ~Dead;
                break;
            }
            case D3D10_SB_OPCODE_LOOP:
            {
                RX_WARP_FRAME Frame = {};

                Frame.Opcode = D3D10_SB_OPCODE_LOOP;
                Frame.Start = Next;
                Frame.Saved = Active;
                Frames.push_back(Frame);
                if (Active == 0)
                    Next = Instruction.Target;
                break;
            }
            case D3D10_SB_OPCODE_ENDLOOP:
            {
                RX_WARP_FRAME &Frame = Frames.back();

                Active = (Active | Frame.Continued) & ~Frame.Removed & ~Dead;
                Frame.Continued = 0;
                if (Active != 0)
                {
                    Next = Frame.Start;
                    break;
                }
                RX_WARP_FRAME Done = Frame;

                Frames.pop_back();
                Active = Done.Saved & ~Done.Removed & ~Dead;
                break;
            }
            case D3D10_SB_OPCODE_BREAK:
                Leave(Active, FALSE);
                break;
            case D3D10_SB_OPCODE_BREAKC:
                Leave(Condition(Instruction, Instruction.Operands[0]), FALSE);
                break;
            case D3D10_SB_OPCODE_CONTINUE:
                Leave(Active, TRUE);
                break;
            case D3D10_SB_OPCODE_CONTINUEC:
                Leave(Condition(Instruction, Instruction.Operands[0]), TRUE);
                break;
            case D3D10_SB_OPCODE_SWITCH:
            {
                RX_WARP_FRAME Frame = {};

                Frame.Opcode = D3D10_SB_OPCODE_SWITCH;
                Frame.Saved = Active;
                Frame.Selector = Current;
                Frames.push_back(Frame);
                Active = 0;
                break;
            }
            case D3D10_SB_OPCODE_CASE:
            case D3D10_SB_OPCODE_DEFAULT:
            {
                RX_WARP_FRAME &Frame = Frames.back();
                const RX_WARP_INSTRUCTION &Switch = Program[Frame.Selector];
                const std::vector<UINT> &Cases = Shader->SwitchCases[Switch.Extra];

                Lanes = 0;
                for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
                {
                    UINT Value[4];
                    BOOLEAN Take;

                    if (!(Frame.Saved & (1u << Lane)))
                        continue;
                    Fetch(Switch.Operands[0], Lane, TRUE, Value);
                    if (Instruction.Opcode == D3D10_SB_OPCODE_CASE)
                    {
                        Take = (Value[0] == Instruction.Operands[0].Immediate[0]);
                    }
                    else
                    {
                        Take = TRUE;
                        for (UINT Case : Cases)
                            Take = Take && (Value[0] != Case);
                    }
                    if (Take)
                        Lanes |= 1u << Lane;
                }
                /* Lanes already in a case keep falling through until a break */
                Active |= Lanes & ~Frame.Matched & ~Frame.Removed & ~Dead;
                Frame.Matched |= Lanes;
                break;
            }
            case D3D10_SB_OPCODE_ENDSWITCH:
            {
                RX_WARP_FRAME Frame = Frames.back();

                Frames.pop_back();
                Active = Frame.Saved & ~Frame.Removed & ~Dead;
                break;
            }
            case D3D10_SB_OPCODE_RET:
                if (Frames.empty())
                {
                    Next = (UINT)Program.size();
                    return FALSE;
                }
                Dead |= Active;
                Active = 0;
                break;
            case D3D10_SB_OPCODE_RETC:
                Lanes = Condition(Instruction, Instruction.Operands[0]);
                Dead |= Lanes;
                Active &= ~Lanes;
                break;
            case D3D10_SB_OPCODE_DISCARD:
                Lanes = Condition(Instruction, Instruction.Operands[0]);
                Invocation->Discarded |= Lanes;
                Dead |= Lanes;
                Active &= ~Lanes;
                break;
            case D3D10_SB_OPCODE_NOP:
            case D3D10_SB_OPCODE_LABEL:
            case D3D10_SB_OPCODE_CALL:
            case D3D10_SB_OPCODE_CALLC:
                break;
            case D3D11_SB_OPCODE_ABORT:
            case D3D11_SB_OPCODE_DEBUG_BREAK:
                break;
            case D3D10_SB_OPCODE_EMIT:
            case D3D10_SB_OPCODE_CUT:
            case D3D10_SB_OPCODE_EMITTHENCUT:
            case D3D11_SB_OPCODE_EMIT_STREAM:
            case D3D11_SB_OPCODE_CUT_STREAM:
            case D3D11_SB_OPCODE_EMITTHENCUT_STREAM:
            {
                BOOLEAN Emit = (Instruction.Opcode != D3D10_SB_OPCODE_CUT && Instruction.Opcode != D3D11_SB_OPCODE_CUT_STREAM);
                BOOLEAN Cut = (Instruction.Opcode != D3D10_SB_OPCODE_EMIT && Instruction.Opcode != D3D11_SB_OPCODE_EMIT_STREAM);
                UINT Stream = (Instruction.OperandCount != 0) ? Instruction.Operands[0].Index[0].Immediate : 0;

                if (Invocation->EmitRoutine == nullptr)
                    break;
                for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
                {
                    if (Active & Invocation->Live & (1u << Lane))
                        Invocation->EmitRoutine(Invocation->EmitContext, Invocation, Stream, Lane, Emit, Cut);
                }
                break;
            }
            case D3D11_SB_OPCODE_SYNC:
                if (DECODE_D3D11_SB_SYNC_FLAGS(Instruction.Control) & D3D11_SB_SYNC_THREADS_IN_GROUP)
                    return TRUE;
                break;
            case RxWarpOpcodeHeap:
                if (Active != 0 && Next < Program.size())
                    Grouped(Instruction, Program[Next]);
                Next++;
                break;
            default:
                if (Active != 0)
                    Operate(Instruction);
                break;
        }
    }
    return FALSE;
}

/* Everything but flow control, emits and barriers, for the running lanes */
VOID
RxWarpMachine::Operate(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    switch (Instruction.Opcode)
    {
        case D3D10_SB_OPCODE_SAMPLE:
        case D3D10_SB_OPCODE_SAMPLE_L:
        case D3D10_SB_OPCODE_SAMPLE_B:
        case D3D10_SB_OPCODE_SAMPLE_D:
        case D3D10_SB_OPCODE_SAMPLE_C:
        case D3D10_SB_OPCODE_SAMPLE_C_LZ:
        case D3D10_SB_OPCODE_LD:
        case D3D10_SB_OPCODE_RESINFO:
        case D3D10_1_SB_OPCODE_LOD:
        case D3D10_1_SB_OPCODE_GATHER4:
        case D3D11_SB_OPCODE_GATHER4_C:
        case D3D11_SB_OPCODE_GATHER4_PO:
        case D3D11_SB_OPCODE_GATHER4_PO_C:
        case D3D11_SB_OPCODE_BUFINFO:
        case D3D10_SB_OPCODE_LD_MS:
        case D3D10_1_SB_OPCODE_SAMPLE_INFO:
        case D3D10_1_SB_OPCODE_SAMPLE_POS:
            Texture(Instruction);
            break;
        case D3D11_SB_OPCODE_LD_RAW:
        case D3D11_SB_OPCODE_LD_STRUCTURED:
        case D3D11_SB_OPCODE_STORE_RAW:
        case D3D11_SB_OPCODE_STORE_STRUCTURED:
        case D3D11_SB_OPCODE_LD_UAV_TYPED:
        case D3D11_SB_OPCODE_STORE_UAV_TYPED:
        case D3D11_SB_OPCODE_ATOMIC_AND:
        case D3D11_SB_OPCODE_ATOMIC_OR:
        case D3D11_SB_OPCODE_ATOMIC_XOR:
        case D3D11_SB_OPCODE_ATOMIC_CMP_STORE:
        case D3D11_SB_OPCODE_ATOMIC_IADD:
        case D3D11_SB_OPCODE_ATOMIC_IMAX:
        case D3D11_SB_OPCODE_ATOMIC_IMIN:
        case D3D11_SB_OPCODE_ATOMIC_UMAX:
        case D3D11_SB_OPCODE_ATOMIC_UMIN:
        case D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC:
        case D3D11_SB_OPCODE_IMM_ATOMIC_CONSUME:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IADD:
        case D3D11_SB_OPCODE_IMM_ATOMIC_AND:
        case D3D11_SB_OPCODE_IMM_ATOMIC_OR:
        case D3D11_SB_OPCODE_IMM_ATOMIC_XOR:
        case D3D11_SB_OPCODE_IMM_ATOMIC_EXCH:
        case D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IMAX:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IMIN:
        case D3D11_SB_OPCODE_IMM_ATOMIC_UMAX:
        case D3D11_SB_OPCODE_IMM_ATOMIC_UMIN:
        case RxWarpOpcodeStoreHalves:
            Access(Instruction);
            break;
        case D3D11_SB_OPCODE_DADD:
        case D3D11_SB_OPCODE_DMUL:
        case D3D11_SB_OPCODE_DMAX:
        case D3D11_SB_OPCODE_DMIN:
        case D3D11_SB_OPCODE_DMOV:
        case D3D11_SB_OPCODE_DMOVC:
        case D3D11_SB_OPCODE_DEQ:
        case D3D11_SB_OPCODE_DNE:
        case D3D11_SB_OPCODE_DLT:
        case D3D11_SB_OPCODE_DGE:
        case D3D11_SB_OPCODE_DTOF:
        case D3D11_SB_OPCODE_FTOD:
        case D3D11_1_SB_OPCODE_DDIV:
        case D3D11_1_SB_OPCODE_DFMA:
        case D3D11_1_SB_OPCODE_DRCP:
        case D3D11_1_SB_OPCODE_DTOI:
        case D3D11_1_SB_OPCODE_DTOU:
        case D3D11_1_SB_OPCODE_ITOD:
        case D3D11_1_SB_OPCODE_UTOD:
            Doubles(Instruction);
            break;
        case D3D11_1_SB_OPCODE_MSAD:
            MaskedDifference(Instruction);
            break;
        case D3D11_SB_OPCODE_EVAL_SNAPPED:
        case D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX:
        case D3D11_SB_OPCODE_EVAL_CENTROID:
            Evaluate(Instruction);
            break;
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK:
        case D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK:
            TextureFeedback(Instruction);
            break;
        case D3DWDDM1_3_SB_OPCODE_CHECK_ACCESS_FULLY_MAPPED:
            FullyMapped(Instruction);
            break;
        case RxWarpOpcodeHalfTowardZero:
            HalfTowardZero(Instruction);
            break;
        case RxWarpOpcodeWave:
            Wave(Instruction);
            break;
        case RxWarpOpcodeLong:
            Long(Instruction);
            break;
        case RxWarpOpcodePairScatter:
            PairScatter(Instruction);
            break;
        case RxWarpOpcodeHalfToFloat:
            HalfToFloat(Instruction);
            break;
        default:
            Arithmetic(Instruction);
            break;
    }
}

/*
 * Runs the instruction after a heap prefix once for each set of lanes that name the same
 * descriptors, with those descriptors bound to the slots the prefix lists.
 */
VOID
RxWarpMachine::Grouped(
    _In_ const RX_WARP_INSTRUCTION &Prefix,
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    const RX_WARP_STAGE_BINDINGS *Original = Bindings;
    RX_WARP_LANE_VALUES Indices[RxWarpMaxOperands / 2] = {};
    UINT Binds = Prefix.OperandCount / 2;
    UINT Saved = Active;
    UINT Remaining = Active;

    for (UINT Bind = 0; Bind < Binds; Bind++)
        FetchAll(Prefix.Operands[Bind * 2], TRUE, Indices[Bind]);
    while (Remaining != 0)
    {
        RX_WARP_STAGE_BINDINGS Local = *Original;
        UINT Lead = 0;
        UINT Lanes = 0;

        while (!(Remaining & (1u << Lead)))
            Lead++;
        for (UINT Lane = Lead; Lane < RxWarpLanes; Lane++)
        {
            BOOLEAN Same = (Remaining & (1u << Lane)) != 0;

            for (UINT Bind = 0; Bind < Binds && Same; Bind++)
                Same = (Indices[Bind][Lane][0] == Indices[Bind][Lead][0]);
            if (Same)
                Lanes |= 1u << Lane;
        }
        if (Original->BindHeap != nullptr)
        {
            for (UINT Bind = 0; Bind < Binds; Bind++)
            {
                UINT Target = Prefix.Operands[Bind * 2 + 1].Immediate[0];

                Original->BindHeap(Original->HeapContext, Target >> 16, Target & 0xFFFF, Indices[Bind][Lead][0], &Local);
            }
        }
        Bindings = &Local;
        Active = Lanes;
        Operate(Instruction);
        Remaining &= ~Lanes;
    }
    Bindings = Original;
    Active = Saved & ~Dead;
}

/* Per component float operation over the masked components of every running lane */
template <typename Operation>
static
VOID
RxWarpFloatOp(
    _In_ UINT Sources,
    _In_reads_(Sources) RX_WARP_LANE_VALUES *Input,
    _Out_ RX_WARP_LANE_VALUES Result,
    _In_ Operation Function)
{
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            FLOAT A = RxWarpAsFloat(Input[0][Lane][Component]);
            FLOAT B = (Sources > 1) ? RxWarpAsFloat(Input[1][Lane][Component]) : 0.0f;
            FLOAT C = (Sources > 2) ? RxWarpAsFloat(Input[2][Lane][Component]) : 0.0f;

            Result[Lane][Component] = RxWarpAsUint(Function(A, B, C));
        }
    }
}

/* Float sources, raw bit results: comparisons and conversions */
template <typename Operation>
static
VOID
RxWarpFloatBitsOp(
    _In_ UINT Sources,
    _In_reads_(Sources) RX_WARP_LANE_VALUES *Input,
    _Out_ RX_WARP_LANE_VALUES Result,
    _In_ Operation Function)
{
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            FLOAT A = RxWarpAsFloat(Input[0][Lane][Component]);
            FLOAT B = (Sources > 1) ? RxWarpAsFloat(Input[1][Lane][Component]) : 0.0f;

            Result[Lane][Component] = Function(A, B);
        }
    }
}

template <typename Operation>
static
VOID
RxWarpIntOp(
    _In_ UINT Sources,
    _In_reads_(Sources) RX_WARP_LANE_VALUES *Input,
    _Out_ RX_WARP_LANE_VALUES Result,
    _In_ Operation Function)
{
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            UINT A = Input[0][Lane][Component];
            UINT B = (Sources > 1) ? Input[1][Lane][Component] : 0;
            UINT C = (Sources > 2) ? Input[2][Lane][Component] : 0;

            Result[Lane][Component] = Function(A, B, C);
        }
    }
}

static
BOOLEAN
RxWarpIsIntegerOp(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
        case D3D10_SB_OPCODE_AND:
        case D3D10_SB_OPCODE_OR:
        case D3D10_SB_OPCODE_XOR:
        case D3D10_SB_OPCODE_NOT:
        case D3D10_SB_OPCODE_IADD:
        case D3D10_SB_OPCODE_IMAD:
        case D3D10_SB_OPCODE_IMUL:
        case D3D10_SB_OPCODE_IMAX:
        case D3D10_SB_OPCODE_IMIN:
        case D3D10_SB_OPCODE_INEG:
        case D3D10_SB_OPCODE_IEQ:
        case D3D10_SB_OPCODE_INE:
        case D3D10_SB_OPCODE_ILT:
        case D3D10_SB_OPCODE_IGE:
        case D3D10_SB_OPCODE_ULT:
        case D3D10_SB_OPCODE_UGE:
        case D3D10_SB_OPCODE_UMAX:
        case D3D10_SB_OPCODE_UMIN:
        case D3D10_SB_OPCODE_UMUL:
        case D3D10_SB_OPCODE_UMAD:
        case D3D10_SB_OPCODE_UDIV:
        case D3D10_SB_OPCODE_ISHL:
        case D3D10_SB_OPCODE_ISHR:
        case D3D10_SB_OPCODE_USHR:
        case D3D10_SB_OPCODE_ITOF:
        case D3D10_SB_OPCODE_UTOF:
        case D3D10_SB_OPCODE_MOVC:
        case D3D11_SB_OPCODE_SWAPC:
        case D3D11_SB_OPCODE_UADDC:
        case D3D11_SB_OPCODE_USUBB:
        case D3D11_SB_OPCODE_COUNTBITS:
        case D3D11_SB_OPCODE_FIRSTBIT_HI:
        case D3D11_SB_OPCODE_FIRSTBIT_LO:
        case D3D11_SB_OPCODE_FIRSTBIT_SHI:
        case D3D11_SB_OPCODE_UBFE:
        case D3D11_SB_OPCODE_IBFE:
        case D3D11_SB_OPCODE_BFI:
        case D3D11_SB_OPCODE_BFREV:
        case D3D11_SB_OPCODE_F16TOF32:
            return TRUE;
        default:
            return FALSE;
    }
}

static
double
RxWarpJoinDouble(
    _In_ UINT Low,
    _In_ UINT High)
{
    UINT64 Bits = ((UINT64)High << 32) | Low;
    double Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
VOID
RxWarpSplitDouble(
    _In_ double Value,
    _Out_writes_(2) UINT *Words)
{
    UINT64 Bits;

    memcpy(&Bits, &Value, sizeof(Bits));
    Words[0] = (UINT)Bits;
    Words[1] = (UINT)(Bits >> 32);
}

/* Reads a source as two doubles in xy and zw, applying its modifiers to the doubles */
VOID
RxWarpMachine::FetchDoubles(
    _In_ const RX_WARP_OPERAND &Operand,
    _Out_ double (*Values)[2])
{
    RX_WARP_OPERAND Plain = Operand;
    RX_WARP_LANE_VALUES Raw;

    Plain.Negate = 0;
    Plain.Absolute = 0;
    FetchAll(Plain, TRUE, Raw);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Pair = 0; Pair < 2; Pair++)
        {
            double Value = RxWarpJoinDouble(Raw[Lane][Pair * 2], Raw[Lane][Pair * 2 + 1]);

            if (Operand.Absolute)
                Value = fabs(Value);
            if (Operand.Negate)
                Value = -Value;
            Values[Lane][Pair] = Value;
        }
    }
}

VOID
RxWarpMachine::Doubles(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    double Source[3][RxWarpLanes][2] = {};
    RX_WARP_LANE_VALUES Words[3] = {};
    RX_WARP_LANE_VALUES Result = {};
    UINT Opcode = Instruction.Opcode;
    UINT Mask = Instruction.Operands[0].Mask;
    UINT Sources = (Instruction.OperandCount > 1) ? Instruction.OperandCount - 1 : 0;
    BOOLEAN Narrow;
    UINT Slots[2];
    UINT SlotCount = 0;

    if (Sources > 3)
        Sources = 3;
    for (UINT Index = 0; Index < Sources; Index++)
    {
        FetchDoubles(Instruction.Operands[1 + Index], Source[Index]);
        FetchAll(Instruction.Operands[1 + Index], TRUE, Words[Index]);
    }

    /* Results narrower than a double land in the written components in order */
    Narrow = (Opcode == D3D11_SB_OPCODE_DEQ || Opcode == D3D11_SB_OPCODE_DNE || Opcode == D3D11_SB_OPCODE_DLT ||
              Opcode == D3D11_SB_OPCODE_DGE || Opcode == D3D11_SB_OPCODE_DTOF || Opcode == D3D11_1_SB_OPCODE_DTOI ||
              Opcode == D3D11_1_SB_OPCODE_DTOU);
    for (UINT Component = 0; Component < 4 && SlotCount < 2; Component++)
    {
        if (Narrow && (Mask & (1u << Component)))
            Slots[SlotCount++] = Component;
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Pair = 0; Pair < 2; Pair++)
        {
            double A = Source[0][Lane][Pair];
            double B = Source[1][Lane][Pair];
            double C = Source[2][Lane][Pair];
            double Value = 0.0;
            UINT Bits = 0;

            switch (Opcode)
            {
                case D3D11_SB_OPCODE_DADD:
                    Value = A + B;
                    break;
                case D3D11_SB_OPCODE_DMUL:
                    Value = A * B;
                    break;
                case D3D11_1_SB_OPCODE_DDIV:
                    Value = A / B;
                    break;
                case D3D11_1_SB_OPCODE_DFMA:
                    /* The reference only computes the first pair of lanes; the other two read as zero */
                    Value = (Lane < 2) ? fma(A, B, C) : 0.0;
                    break;
                case D3D11_1_SB_OPCODE_DRCP:
                    Value = 1.0 / A;
                    break;
                case D3D11_SB_OPCODE_DMAX:
                    Value = fmax(A, B);
                    break;
                case D3D11_SB_OPCODE_DMIN:
                    Value = fmin(A, B);
                    break;
                case D3D11_SB_OPCODE_DMOV:
                    Value = A;
                    break;
                case D3D11_SB_OPCODE_DMOVC:
                    /* The condition is 32 bit, one per double */
                    Value = Words[0][Lane][Pair * 2] ? B : C;
                    break;
                case D3D11_SB_OPCODE_FTOD:
                    Value = (double)RxWarpAsFloat(Words[0][Lane][Pair]);
                    break;
                case D3D11_1_SB_OPCODE_ITOD:
                    Value = (double)(INT)Words[0][Lane][Pair];
                    break;
                case D3D11_1_SB_OPCODE_UTOD:
                    Value = (double)Words[0][Lane][Pair];
                    break;
                case D3D11_SB_OPCODE_DEQ:
                    Bits = (A == B) ? UINT_MAX : 0;
                    break;
                case D3D11_SB_OPCODE_DNE:
                    Bits = (A != B) ? UINT_MAX : 0;
                    break;
                case D3D11_SB_OPCODE_DLT:
                    Bits = (A < B) ? UINT_MAX : 0;
                    break;
                case D3D11_SB_OPCODE_DGE:
                    Bits = (A >= B) ? UINT_MAX : 0;
                    break;
                case D3D11_SB_OPCODE_DTOF:
                    Bits = RxWarpAsUint((FLOAT)A);
                    break;
                case D3D11_1_SB_OPCODE_DTOI:
                    if (A != A)
                        Bits = 0;
                    else if (A >= 2147483648.0)
                        Bits = (UINT)INT_MAX;
                    else if (A <= -2147483648.0)
                        Bits = (UINT)INT_MIN;
                    else
                        Bits = (UINT)(INT)A;
                    break;
                case D3D11_1_SB_OPCODE_DTOU:
                    if (!(A > 0.0))
                        Bits = 0;
                    else if (A >= 4294967296.0)
                        Bits = UINT_MAX;
                    else
                        Bits = (UINT)A;
                    break;
            }

            if (Narrow)
            {
                if (Pair < SlotCount)
                    Result[Lane][Slots[Pair]] = Bits;
            }
            else
            {
                RxWarpSplitDouble(Value, &Result[Lane][Pair * 2]);
            }
        }
    }
    Store(Instruction, 0, Result, FALSE);
}

/* Float to 64 bit integer, saturating, with NaN as zero */
static
UINT64
RxWarpLongFromReal(
    _In_ double Value,
    _In_ BOOLEAN Signed)
{
    if (Value != Value)
        return 0;
    if (Signed)
    {
        if (Value >= 9223372036854775808.0)
            return (UINT64)INT64_MAX;
        if (Value <= -9223372036854775808.0)
            return (UINT64)INT64_MIN;
        return (UINT64)(INT64)Value;
    }
    if (!(Value > 0.0))
        return 0;
    if (Value >= 18446744073709551616.0)
        return UINT64_MAX;
    return (UINT64)Value;
}

static
UINT
RxWarpLongFirst(
    _In_ UINT Kind,
    _In_ UINT64 Value)
{
    if (Kind == RxWarpLongSignedFirstHigh && (INT64)Value < 0)
        Value = ~Value;
    for (UINT Bit = 0; Bit < 64; Bit++)
    {
        UINT64 Probe = (Kind == RxWarpLongFirstLow) ? (1ull << Bit) : (0x8000000000000000ull >> Bit);

        if (Value & Probe)
            return Bit;
    }
    return UINT_MAX;
}

static
UINT64
RxWarpLongApply(
    _In_ UINT Kind,
    _In_ UINT64 A,
    _In_ UINT64 B,
    _In_ UINT64 C)
{
    INT64 SignedA = (INT64)A;
    INT64 SignedB = (INT64)B;
    double Real;
    double Other;
    double Third;

    memcpy(&Real, &A, sizeof(Real));
    memcpy(&Other, &B, sizeof(Other));
    memcpy(&Third, &C, sizeof(Third));
    switch (Kind)
    {
        case RxWarpLongAdd: return A + B;
        case RxWarpLongSubtract: return A - B;
        case RxWarpLongMultiply: return A * B;
        case RxWarpLongDivide: return (B != 0) ? A / B : UINT64_MAX;
        case RxWarpLongRemainder: return (B != 0) ? A % B : UINT64_MAX;
        case RxWarpLongSignedDivide:
            if (B == 0)
                return (UINT64)INT64_MAX;
            if (SignedA == INT64_MIN && SignedB == -1)
                return A;
            return (UINT64)(SignedA / SignedB);
        case RxWarpLongSignedRemainder:
            if (B == 0)
                return (UINT64)INT64_MAX;
            if (SignedB == -1)
                return 0;
            return (UINT64)(SignedA % SignedB);
        case RxWarpLongShiftLeft: return A << (B & 63);
        case RxWarpLongShiftRight: return A >> (B & 63);
        case RxWarpLongSignedShiftRight: return (UINT64)(SignedA >> (B & 63));
        case RxWarpLongMax: return (A > B) ? A : B;
        case RxWarpLongMin: return (A < B) ? A : B;
        case RxWarpLongSignedMax: return (SignedA > SignedB) ? A : B;
        case RxWarpLongSignedMin: return (SignedA < SignedB) ? A : B;
        case RxWarpLongZeroExtend: return (UINT)A;
        case RxWarpLongSignExtend: return (UINT64)(INT64)(INT)(UINT)A;
        case RxWarpLongToFloat: return RxWarpAsUint((FLOAT)A);
        case RxWarpLongSignedToFloat: return RxWarpAsUint((FLOAT)SignedA);
        case RxWarpLongFromFloat: return RxWarpLongFromReal(RxWarpAsFloat((UINT)A), FALSE);
        case RxWarpLongSignedFromFloat: return RxWarpLongFromReal(RxWarpAsFloat((UINT)A), TRUE);
        case RxWarpLongFromDouble: return RxWarpLongFromReal(Real, FALSE);
        case RxWarpLongSignedFromDouble: return RxWarpLongFromReal(Real, TRUE);
        case RxWarpLongEqual: return (A == B) ? UINT_MAX : 0;
        case RxWarpLongNotEqual: return (A != B) ? UINT_MAX : 0;
        case RxWarpLongLess: return (A < B) ? UINT_MAX : 0;
        case RxWarpLongGreaterEqual: return (A >= B) ? UINT_MAX : 0;
        case RxWarpLongSignedLess: return (SignedA < SignedB) ? UINT_MAX : 0;
        case RxWarpLongSignedGreaterEqual: return (SignedA >= SignedB) ? UINT_MAX : 0;
        case RxWarpLongCountBits: return RxWarpCountBits((UINT)A) + RxWarpCountBits((UINT)(A >> 32));
        case RxWarpLongFirstHigh:
        case RxWarpLongFirstLow:
        case RxWarpLongSignedFirstHigh:
            return RxWarpLongFirst(Kind, A);
        default:
            break;
    }

    /* The rest make doubles */
    switch (Kind)
    {
        case RxWarpLongToDouble: Real = (double)A; break;
        case RxWarpLongSignedToDouble: Real = (double)SignedA; break;
        case RxWarpLongSaturateDouble: Real = !(Real > 0.0) ? 0.0 : (Real < 1.0) ? Real : 1.0; break;
        case RxWarpLongFusedDouble: Real = fma(Real, Other, Third); break;
        default: Real = 0.0; break;
    }
    memcpy(&A, &Real, sizeof(A));
    return A;
}

VOID
RxWarpMachine::Long(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Words[3] = {};
    RX_WARP_LANE_VALUES Result = {};
    UINT Kind;

    if (Instruction.OperandCount < 5)
        return;
    Kind = Instruction.Operands[4].Immediate[0];
    for (UINT Index = 0; Index < 3; Index++)
        FetchAll(Instruction.Operands[1 + Index], TRUE, Words[Index]);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT64 A = Words[0][Lane][0] | ((UINT64)Words[0][Lane][1] << 32);
        UINT64 B = Words[1][Lane][0] | ((UINT64)Words[1][Lane][1] << 32);
        UINT64 C = Words[2][Lane][0] | ((UINT64)Words[2][Lane][1] << 32);
        UINT64 Value = RxWarpLongApply(Kind, A, B, C);
        BOOLEAN Narrow = Kind >= RxWarpLongFirstNarrow || Kind == RxWarpLongToFloat || Kind == RxWarpLongSignedToFloat;

        Result[Lane][0] = Result[Lane][2] = (UINT)Value;
        Result[Lane][1] = Result[Lane][3] = Narrow ? (UINT)Value : (UINT)(Value >> 32);
    }
    Store(Instruction, 0, Result, FALSE);
}

/* Half NaNs keep their payload, made quiet; everything else converts as f16tof32 does */
VOID
RxWarpMachine::HalfToFloat(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Values;

    if (Instruction.OperandCount < 2)
        return;
    FetchAll(Instruction.Operands[1], TRUE, Values);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            UINT Half = Values[Lane][Component] & 0xFFFF;

            if ((Half & 0x7C00) == 0x7C00 && (Half & 0x3FF) != 0)
                Values[Lane][Component] = ((Half & 0x8000) << 16) | 0x7FC00000 | ((Half & 0x3FF) << 13);
            else
                Values[Lane][Component] = RxWarpAsUint(RxWarpHalfToFloat(Half));
        }
    }
    Store(Instruction, 0, Values, FALSE);
}

/* Lanes that agree on the element store their own words; otherwise the first two store the last two's */
VOID
RxWarpMachine::PairScatter(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Value = {};
    RX_WARP_LANE_VALUES Words[2] = {};
    UINT Saved = Active;
    UINT Writers = Active;
    INT Element = 0;
    BOOLEAN Seen = FALSE;
    BOOLEAN Uniform = TRUE;

    if (Instruction.OperandCount < 3)
        return;
    FetchAll(Instruction.Operands[2], TRUE, Value);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        INT Index;

        if (!(Active & (1u << Lane)))
            continue;
        Index = ResolveIndex(Instruction.Operands[0].Index[1], Lane);
        Uniform &= !Seen || Index == Element;
        Element = Index;
        Seen = TRUE;
    }
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT From = Uniform ? Lane : (Lane + 2) & (RxWarpLanes - 1);

        for (UINT Component = 0; Component < 4; Component++)
        {
            Words[0][Lane][Component] = Value[From][0];
            Words[1][Lane][Component] = Value[From][1];
        }
    }
    if (!Uniform)
        Writers &= 3;
    Active = Writers;
    Store(Instruction, 0, Words[0], FALSE);
    Store(Instruction, 1, Words[1], FALSE);
    Active = Saved;
}

/* eval_snapped, eval_sample_index and eval_centroid: the draw evaluates the input, the swizzle and modifiers apply after */
VOID
RxWarpMachine::Evaluate(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    const RX_WARP_OPERAND &Source = Instruction.Operands[1];
    RX_WARP_LANE_VALUES Parameter = {};
    RX_WARP_LANE_VALUES Result = {};
    RX_WARP_EVALUATE_KIND Kind;
    UINT Register;

    if (Instruction.OperandCount < 2 || Invocation->EvaluateRoutine == nullptr)
        return;
    Kind = (Instruction.Opcode == D3D11_SB_OPCODE_EVAL_SNAPPED) ? RxWarpEvaluateSnapped :
           (Instruction.Opcode == D3D11_SB_OPCODE_EVAL_SAMPLE_INDEX) ? RxWarpEvaluateSample : RxWarpEvaluateCentroid;
    if (Kind != RxWarpEvaluateCentroid && Instruction.OperandCount > 2)
        FetchAll(Instruction.Operands[2], TRUE, Parameter);
    Register = Source.Index[0].Immediate;

    /* The reference takes a sample index held in a register as that register's number, not its contents */
    if (Kind == RxWarpEvaluateSample && Instruction.OperandCount > 2 && Instruction.Operands[2].Type != D3D10_SB_OPERAND_TYPE_IMMEDIATE32)
    {
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            Parameter[Lane][0] = Instruction.Operands[2].Index[0].Immediate;
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT Raw[4] = {};

        if (!(Active & (1u << Lane)))
            continue;
        Invocation->EvaluateRoutine(Invocation->EvaluateContext, Lane, Register, Kind, (const INT *)Parameter[Lane], Raw);
        for (UINT Component = 0; Component < 4; Component++)
        {
            UINT Bits = Raw[Source.Swizzle[Component] & 3];

            if (Source.Absolute)
                Bits &= 0x7FFFFFFF;
            if (Source.Negate)
                Bits ^= 0x80000000;
            Result[Lane][Component] = Bits;
        }
    }
    Store(Instruction, 0, Result, TRUE);
}

/* msad, per component: absolute byte differences against the reference, skipping its zero bytes, plus the accumulator */
VOID
RxWarpMachine::MaskedDifference(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Reference;
    RX_WARP_LANE_VALUES Window;
    RX_WARP_LANE_VALUES Accumulate;
    RX_WARP_LANE_VALUES Result;

    if (Instruction.OperandCount < 4)
        return;
    FetchAll(Instruction.Operands[1], TRUE, Reference);
    FetchAll(Instruction.Operands[2], TRUE, Window);
    FetchAll(Instruction.Operands[3], TRUE, Accumulate);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            UINT Sum = Accumulate[Lane][Component];

            for (UINT Byte = 0; Byte < 4; Byte++)
            {
                INT Wanted = (INT)((Reference[Lane][Component] >> (Byte * 8)) & 0xFF);
                INT Found = (INT)((Window[Lane][Component] >> (Byte * 8)) & 0xFF);

                if (Wanted != 0)
                    Sum += (UINT)abs(Wanted - Found);
            }
            Result[Lane][Component] = Sum;
        }
    }
    Store(Instruction, 0, Result, FALSE);
}

VOID
RxWarpMachine::Arithmetic(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Source[4] = {};
    RX_WARP_LANE_VALUES Result = {};
    RX_WARP_LANE_VALUES Second = {};
    UINT Opcode = Instruction.Opcode;
    BOOLEAN Integer = RxWarpIsIntegerOp(Opcode);
    UINT Destinations = 1;
    UINT Sources;

    switch (Opcode)
    {
        case D3D10_SB_OPCODE_SINCOS:
        case D3D10_SB_OPCODE_UDIV:
        case D3D10_SB_OPCODE_IMUL:
        case D3D10_SB_OPCODE_UMUL:
        case D3D11_SB_OPCODE_UADDC:
        case D3D11_SB_OPCODE_USUBB:
        case D3D11_SB_OPCODE_SWAPC:
            Destinations = 2;
            break;
        default:
            break;
    }
    if (Instruction.OperandCount <= Destinations)
        return;
    Sources = Instruction.OperandCount - Destinations;
    if (Sources > 4)
        Sources = 4;

    /* movc and swapc test integers but move float values untouched, so read them raw */
    for (UINT Index = 0; Index < Sources; Index++)
        FetchAll(Instruction.Operands[Destinations + Index], Integer, Source[Index]);

    switch (Opcode)
    {
        case D3D10_SB_OPCODE_MOV:
            memcpy(Result, Source[0], sizeof(Result));
            break;
        case D3D10_SB_OPCODE_ADD:
            RxWarpFloatOp(2, Source, Result, [](FLOAT A, FLOAT B, FLOAT) { return A + B; });
            break;
        case D3D10_SB_OPCODE_MUL:
            RxWarpFloatOp(2, Source, Result, [](FLOAT A, FLOAT B, FLOAT) { return A * B; });
            break;
        case D3D10_SB_OPCODE_MAD:
            /* A negated addend, or one negated factor, makes the reference multiply and subtract apart */
            if (Instruction.Operands[Destinations + 2].Negate ||
                Instruction.Operands[Destinations].Negate != Instruction.Operands[Destinations + 1].Negate)
            {
                RxWarpFloatOp(3, Source, Result, [](FLOAT A, FLOAT B, FLOAT C)
                              {
                                  volatile FLOAT Product = A * B;

                                  return Product + C;
                              });
            }
            else
            {
                RxWarpFloatOp(3, Source, Result, [](FLOAT A, FLOAT B, FLOAT C) { return fmaf(A, B, C); });
            }
            break;
        case D3D10_SB_OPCODE_DIV:
            RxWarpFloatOp(2, Source, Result, [](FLOAT A, FLOAT B, FLOAT) { return A / B; });
            break;
        case D3D10_SB_OPCODE_MIN:
            RxWarpFloatOp(2, Source, Result, [](FLOAT A, FLOAT B, FLOAT) { return fminf(A, B); });
            break;
        case D3D10_SB_OPCODE_MAX:
            RxWarpFloatOp(2, Source, Result, [](FLOAT A, FLOAT B, FLOAT) { return fmaxf(A, B); });
            break;
        case D3D10_SB_OPCODE_FRC:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return A - floorf(A); });
            break;
        case D3D10_SB_OPCODE_ROUND_NE:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return nearbyintf(A); });
            break;
        case D3D10_SB_OPCODE_ROUND_NI:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return floorf(A); });
            break;
        case D3D10_SB_OPCODE_ROUND_PI:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return ceilf(A); });
            break;
        case D3D10_SB_OPCODE_ROUND_Z:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return truncf(A); });
            break;
        case D3D10_SB_OPCODE_EXP:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return exp2f(A); });
            break;
        case D3D10_SB_OPCODE_LOG:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return log2f(A); });
            break;
        case D3D10_SB_OPCODE_SQRT:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return sqrtf(A); });
            break;
        case D3D10_SB_OPCODE_RSQ:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return 1.0f / sqrtf(A); });
            break;
        case D3D11_SB_OPCODE_RCP:
            RxWarpFloatOp(1, Source, Result, [](FLOAT A, FLOAT, FLOAT) { return 1.0f / A; });
            break;
        case D3D10_SB_OPCODE_DP2:
        case D3D10_SB_OPCODE_DP3:
        case D3D10_SB_OPCODE_DP4:
        {
            UINT Count = (Opcode == D3D10_SB_OPCODE_DP2) ? 2 : (Opcode == D3D10_SB_OPCODE_DP3) ? 3 : 4;

            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                FLOAT Sum = RxWarpAsFloat(Source[0][Lane][0]) * RxWarpAsFloat(Source[1][Lane][0]);

                for (UINT Component = 1; Component < Count; Component++)
                    Sum = Sum + RxWarpAsFloat(Source[0][Lane][Component]) * RxWarpAsFloat(Source[1][Lane][Component]);
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Lane][Component] = RxWarpAsUint(Sum);
            }
            break;
        }
        case D3D10_SB_OPCODE_EQ:
            RxWarpFloatBitsOp(2, Source, Result, [](FLOAT A, FLOAT B) { return (UINT)(A == B ? UINT_MAX : 0); });
            break;
        case D3D10_SB_OPCODE_NE:
            RxWarpFloatBitsOp(2, Source, Result, [](FLOAT A, FLOAT B) { return (UINT)(A != B ? UINT_MAX : 0); });
            break;
        case D3D10_SB_OPCODE_LT:
            RxWarpFloatBitsOp(2, Source, Result, [](FLOAT A, FLOAT B) { return (UINT)(A < B ? UINT_MAX : 0); });
            break;
        case D3D10_SB_OPCODE_GE:
            RxWarpFloatBitsOp(2, Source, Result, [](FLOAT A, FLOAT B) { return (UINT)(A >= B ? UINT_MAX : 0); });
            break;
        case D3D10_SB_OPCODE_FTOI:
            RxWarpFloatBitsOp(1, Source, Result, [](FLOAT A, FLOAT) { return (UINT)RxWarpFloatToInt(A); });
            break;
        case D3D10_SB_OPCODE_FTOU:
            RxWarpFloatBitsOp(1, Source, Result, [](FLOAT A, FLOAT) { return RxWarpFloatToUint(A); });
            break;
        case D3D11_SB_OPCODE_F32TOF16:
            RxWarpFloatBitsOp(1, Source, Result, [](FLOAT A, FLOAT) { return RxWarpFloatToHalf(A); });
            break;
        case D3D11_SB_OPCODE_F16TOF32:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpAsUint(RxWarpHalfToFloat(A & 0xFFFF)); });
            break;
        case D3D10_SB_OPCODE_ITOF:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpAsUint((FLOAT)(INT)A); });
            break;
        case D3D10_SB_OPCODE_UTOF:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpAsUint((FLOAT)A); });
            break;
        case D3D10_SB_OPCODE_AND:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A & B; });
            break;
        case D3D10_SB_OPCODE_OR:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A | B; });
            break;
        case D3D10_SB_OPCODE_XOR:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A ^ B; });
            break;
        case D3D10_SB_OPCODE_NOT:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return ~A; });
            break;
        case D3D10_SB_OPCODE_IADD:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A + B; });
            break;
        case D3D10_SB_OPCODE_IMAD:
        case D3D10_SB_OPCODE_UMAD:
            RxWarpIntOp(3, Source, Result, [](UINT A, UINT B, UINT C) { return A * B + C; });
            break;
        case D3D10_SB_OPCODE_INEG:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return 0u - A; });
            break;
        case D3D10_SB_OPCODE_IMAX:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return ((INT)A > (INT)B) ? A : B; });
            break;
        case D3D10_SB_OPCODE_IMIN:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return ((INT)A < (INT)B) ? A : B; });
            break;
        case D3D10_SB_OPCODE_UMAX:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A > B) ? A : B; });
            break;
        case D3D10_SB_OPCODE_UMIN:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A < B) ? A : B; });
            break;
        case D3D10_SB_OPCODE_IEQ:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A == B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_INE:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A != B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_ILT:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return ((INT)A < (INT)B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_IGE:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return ((INT)A >= (INT)B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_ULT:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A < B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_UGE:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (A >= B) ? UINT_MAX : 0u; });
            break;
        case D3D10_SB_OPCODE_ISHL:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A << (B & 31); });
            break;
        case D3D10_SB_OPCODE_ISHR:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return (UINT)((INT)A >> (B & 31)); });
            break;
        case D3D10_SB_OPCODE_USHR:
            RxWarpIntOp(2, Source, Result, [](UINT A, UINT B, UINT) { return A >> (B & 31); });
            break;
        case D3D11_SB_OPCODE_COUNTBITS:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpCountBits(A); });
            break;
        case D3D11_SB_OPCODE_FIRSTBIT_HI:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpFirstHigh(A); });
            break;
        case D3D11_SB_OPCODE_FIRSTBIT_LO:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpFirstLow(A); });
            break;
        case D3D11_SB_OPCODE_FIRSTBIT_SHI:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpFirstHigh(((INT)A < 0) ? ~A : A); });
            break;
        case D3D11_SB_OPCODE_BFREV:
            RxWarpIntOp(1, Source, Result, [](UINT A, UINT, UINT) { return RxWarpReverseBits(A); });
            break;
        case D3D11_SB_OPCODE_UBFE:
        case D3D11_SB_OPCODE_IBFE:
        {
            BOOLEAN Signed = (Opcode == D3D11_SB_OPCODE_IBFE);

            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    UINT Width = Source[0][Lane][Component] & 31;
                    UINT Offset = Source[1][Lane][Component] & 31;
                    UINT Value = Source[2][Lane][Component];

                    if (Width == 0)
                        Result[Lane][Component] = 0;
                    else if (Width + Offset < 32)
                        Result[Lane][Component] = Signed ? (UINT)((INT)(Value << (32 - Width - Offset)) >> (32 - Width)) :
                                                           (Value << (32 - Width - Offset)) >> (32 - Width);
                    else
                        Result[Lane][Component] = Signed ? (UINT)((INT)Value >> Offset) : Value >> Offset;
                }
            }
            break;
        }
        case D3D11_SB_OPCODE_BFI:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    UINT Width = Source[0][Lane][Component] & 31;
                    UINT Offset = Source[1][Lane][Component] & 31;
                    UINT Mask = (Width == 0) ? 0 : (((Width == 32) ? UINT_MAX : ((1u << Width) - 1)) << Offset);

                    Result[Lane][Component] = ((Source[2][Lane][Component] << Offset) & Mask) | (Source[3][Lane][Component] & ~Mask);
                }
            }
            break;
        case D3D10_SB_OPCODE_MOVC:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Lane][Component] = Source[0][Lane][Component] ? Source[1][Lane][Component] : Source[2][Lane][Component];
            }
            Store(Instruction, 0, Result, TRUE);
            return;
        case D3D11_SB_OPCODE_SWAPC:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    BOOLEAN Swap = Source[0][Lane][Component] != 0;

                    Result[Lane][Component] = Swap ? Source[2][Lane][Component] : Source[1][Lane][Component];
                    Second[Lane][Component] = Swap ? Source[1][Lane][Component] : Source[2][Lane][Component];
                }
            }
            break;
        case D3D10_SB_OPCODE_SINCOS:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    FLOAT Angle = RxWarpAsFloat(Source[0][Lane][Component]);

                    Result[Lane][Component] = RxWarpAsUint(sinf(Angle));
                    Second[Lane][Component] = RxWarpAsUint(cosf(Angle));
                }
            }
            break;
        case D3D10_SB_OPCODE_UDIV:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    UINT A = Source[0][Lane][Component];
                    UINT B = Source[1][Lane][Component];

                    Result[Lane][Component] = B ? A / B : UINT_MAX;
                    Second[Lane][Component] = B ? A % B : UINT_MAX;
                }
            }
            break;
        case D3D10_SB_OPCODE_IMUL:
        case D3D10_SB_OPCODE_UMUL:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    UINT64 Product;

                    if (Opcode == D3D10_SB_OPCODE_IMUL)
                        Product = (UINT64)((INT64)(INT)Source[0][Lane][Component] * (INT64)(INT)Source[1][Lane][Component]);
                    else
                        Product = (UINT64)Source[0][Lane][Component] * Source[1][Lane][Component];
                    Result[Lane][Component] = (UINT)(Product >> 32);
                    Second[Lane][Component] = (UINT)Product;
                }
            }
            break;
        case D3D11_SB_OPCODE_UADDC:
        case D3D11_SB_OPCODE_USUBB:
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                for (UINT Component = 0; Component < 4; Component++)
                {
                    UINT A = Source[0][Lane][Component];
                    UINT B = Source[1][Lane][Component];

                    if (Opcode == D3D11_SB_OPCODE_UADDC)
                    {
                        Result[Lane][Component] = A + B;
                        Second[Lane][Component] = (A + B < A) ? 1 : 0;
                    }
                    else
                    {
                        Result[Lane][Component] = A - B;
                        Second[Lane][Component] = (A < B) ? 1 : 0;
                    }
                }
            }
            break;
        case D3D10_SB_OPCODE_DERIV_RTX:
        case D3D10_SB_OPCODE_DERIV_RTY:
        case D3D11_SB_OPCODE_DERIV_RTX_COARSE:
        case D3D11_SB_OPCODE_DERIV_RTY_COARSE:
        case D3D11_SB_OPCODE_DERIV_RTX_FINE:
        case D3D11_SB_OPCODE_DERIV_RTY_FINE:
        {
            BOOLEAN Horizontal = (Opcode == D3D10_SB_OPCODE_DERIV_RTX || Opcode == D3D11_SB_OPCODE_DERIV_RTX_COARSE ||
                                  Opcode == D3D11_SB_OPCODE_DERIV_RTX_FINE);
            BOOLEAN Fine = (Opcode == D3D11_SB_OPCODE_DERIV_RTX_FINE || Opcode == D3D11_SB_OPCODE_DERIV_RTY_FINE);

            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                UINT From;
                UINT To;

                /* Coarse uses the top row or left column for the whole quad */
                if (Horizontal)
                {
                    From = Fine ? (Lane & 2) : 0;
                    To = From + 1;
                }
                else
                {
                    From = Fine ? (Lane & 1) : 0;
                    To = From + 2;
                }
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Lane][Component] = RxWarpAsUint(RxWarpAsFloat(Source[0][To][Component]) - RxWarpAsFloat(Source[0][From][Component]));
            }
            break;
        }
        default:
            return;
    }

    Store(Instruction, 0, Result, !Integer);
    if (Destinations == 2)
    {
        /* The second destination: cosine, remainder, low bits, carry or the swapped value */
        RX_WARP_INSTRUCTION Copy = Instruction;

        Copy.Operands[0] = Instruction.Operands[1];
        Store(Copy, 0, Second, !Integer);
    }
}

/*
 * A Direct3D 12 view's component mapping over what a load or sample returned. Gathers chose
 * their channel before sampling; a forced one there fills all four texels.
 */
static
VOID
RxWarpMapComponents(
    _In_ const RxWarpView *View,
    _In_ UINT Opcode,
    _Inout_updates_(4) UINT *Raw,
    _Inout_ BOOLEAN *Filtered,
    _Inout_updates_(4) INT *Fixed)
{
    BOOLEAN Integer = RxWarpFormatIsInteger(View->Format);
    UINT One = Integer ? 1 : 0x3F800000;
    UINT Source[4];
    INT SourceFixed[4];

    if (Opcode == D3D10_1_SB_OPCODE_GATHER4 || Opcode == D3D11_SB_OPCODE_GATHER4_C || Opcode == D3D11_SB_OPCODE_GATHER4_PO ||
        Opcode == D3D11_SB_OPCODE_GATHER4_PO_C)
    {
        return;
    }
    memcpy(Source, Raw, sizeof(Source));
    memcpy(SourceFixed, Fixed, sizeof(SourceFixed));
    for (UINT Component = 0; Component < 4; Component++)
    {
        UINT Picked = (View->Mapping >> (3 * Component)) & 7;

        if (Picked < 4)
        {
            Raw[Component] = Source[Picked];
            Fixed[Component] = SourceFixed[Picked];
            continue;
        }
        Raw[Component] = (Picked == 5) ? One : 0;
        *Filtered = FALSE;
    }
}

/* The residency status a sample of a fully resident resource leaves */
static constexpr UINT RxWarpResidentStatus = 1;

/*
 * The status forms carry a residency status after the destination and, for the clamp forms, a
 * lowest level of detail last; with every page resident the status reads as fully mapped.
 */
VOID
RxWarpMachine::TextureFeedback(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    static const UINT16 Plain[][2] =
    {
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK, D3D10_SB_OPCODE_SAMPLE },
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_B_CLAMP_FEEDBACK, D3D10_SB_OPCODE_SAMPLE_B },
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK, D3D10_SB_OPCODE_SAMPLE_C },
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_D_CLAMP_FEEDBACK, D3D10_SB_OPCODE_SAMPLE_D },
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_L_FEEDBACK, D3D10_SB_OPCODE_SAMPLE_L },
        { D3DWDDM1_3_SB_OPCODE_SAMPLE_C_LZ_FEEDBACK, D3D10_SB_OPCODE_SAMPLE_C_LZ },
        { D3DWDDM1_3_SB_OPCODE_LD_FEEDBACK, D3D10_SB_OPCODE_LD },
        { D3DWDDM1_3_SB_OPCODE_LD_MS_FEEDBACK, D3D10_SB_OPCODE_LD_MS },
        { D3DWDDM1_3_SB_OPCODE_GATHER4_FEEDBACK, D3D10_1_SB_OPCODE_GATHER4 },
        { D3DWDDM1_3_SB_OPCODE_GATHER4_C_FEEDBACK, D3D11_SB_OPCODE_GATHER4_C },
        { D3DWDDM1_3_SB_OPCODE_GATHER4_PO_FEEDBACK, D3D11_SB_OPCODE_GATHER4_PO },
        { D3DWDDM1_3_SB_OPCODE_GATHER4_PO_C_FEEDBACK, D3D11_SB_OPCODE_GATHER4_PO_C },
    };
    RX_WARP_INSTRUCTION Base = Instruction;
    RX_WARP_LANE_VALUES Status = {};
    UINT Clamped = (Instruction.Opcode >= D3DWDDM1_3_SB_OPCODE_SAMPLE_CLAMP_FEEDBACK &&
                    Instruction.Opcode <= D3DWDDM1_3_SB_OPCODE_SAMPLE_C_CLAMP_FEEDBACK) ? 1 : 0;
    UINT Count = Instruction.OperandCount;

    if (Count < 3 + Clamped)
        return;
    for (UINT Index = 0; Index < ARRAYSIZE(Plain); Index++)
    {
        if (Plain[Index][0] == Instruction.Opcode)
            Base.Opcode = Plain[Index][1];
    }
    for (UINT Index = 2; Index < Count - Clamped; Index++)
        Base.Operands[Index - 1] = Instruction.Operands[Index];
    Base.OperandCount = (UINT8)(Count - 1 - Clamped);
    Texture(Base, Clamped ? &Instruction.Operands[Count - 1] : nullptr);

    if (Instruction.Operands[1].Type == D3D10_SB_OPERAND_TYPE_NULL)
        return;
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
            Status[Lane][Component] = RxWarpResidentStatus;
    }
    Store(Instruction, 1, Status, FALSE);
}

/* A status of zero means some page was missing */
VOID
RxWarpMachine::FullyMapped(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Status;
    RX_WARP_LANE_VALUES Result;

    if (Instruction.OperandCount < 2)
        return;
    FetchAll(Instruction.Operands[1], TRUE, Status);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
            Result[Lane][Component] = (Status[Lane][Component] != 0) ? UINT_MAX : 0;
    }
    Store(Instruction, 0, Result, FALSE);
}

/* One step of a wave reduction or prefix: sum, product, min or max, then and, or, xor at four and up */
static
UINT64
RxWarpWaveCombine(
    _In_ UINT Variant,
    _In_ UINT Type,
    _In_ UINT64 A,
    _In_ UINT64 B)
{
    FLOAT Left = RxWarpAsFloat((UINT)A);
    FLOAT Right = RxWarpAsFloat((UINT)B);

    /* 64 bit values: doubles, or integers compared signed or unsigned */
    if (Type & RxWarpWaveWide)
    {
        UINT Base = Type & 0xFF;
        double Wide[2];
        UINT64 Bits;

        if (Variant >= 4)
            return (Variant == 4) ? (A & B) : (Variant == 5) ? (A | B) : (A ^ B);
        if (Base != RxWarpWaveFloat)
        {
            BOOLEAN Below = (Base == RxWarpWaveSigned) ? ((INT64)A < (INT64)B) : (A < B);

            return (Variant == 0) ? A + B : (Variant == 1) ? A * B : ((Variant == 2) == Below) ? A : B;
        }
        memcpy(&Wide[0], &A, sizeof(Wide[0]));
        memcpy(&Wide[1], &B, sizeof(Wide[1]));
        Wide[0] = (Variant == 0) ? Wide[0] + Wide[1] : (Variant == 1) ? Wide[0] * Wide[1] :
                  (Variant == 2) ? fmin(Wide[0], Wide[1]) : fmax(Wide[0], Wide[1]);
        memcpy(&Bits, &Wide[0], sizeof(Bits));
        return Bits;
    }

    switch (Variant)
    {
        case 0:
            return (Type == RxWarpWaveFloat) ? RxWarpAsUint(Left + Right) : (UINT)(A + B);
        case 1:
            return (Type == RxWarpWaveFloat) ? RxWarpAsUint(Left * Right) : (UINT)A * (UINT)B;
        case 2:
            if (Type == RxWarpWaveFloat)
                return RxWarpAsUint(fminf(Left, Right));
            if (Type == RxWarpWaveSigned)
                return ((INT)A < (INT)B) ? A : B;
            return (A < B) ? A : B;
        case 3:
            if (Type == RxWarpWaveFloat)
                return RxWarpAsUint(fmaxf(Left, Right));
            if (Type == RxWarpWaveSigned)
                return ((INT)A > (INT)B) ? A : B;
            return (A > B) ? A : B;
        case 4:
            return A & B;
        case 5:
            return A | B;
        default:
            return A ^ B;
    }
}

/* The half nearest zero, as a float: finite values past the range stop at the largest half */
static
UINT
RxWarpHalfTowardZero(
    _In_ UINT Bits)
{
    UINT Sign = Bits & 0x80000000;
    UINT Field = (Bits >> 23) & 0xFF;
    INT Exponent = (INT)Field - 127;
    UINT Keep;

    if (Field == 0xFF)
        return Bits;
    if (Exponent > 15)
        return Sign | 0x477FE000;
    if (Exponent < -24)
        return Sign;

    /* Normal halves keep 10 mantissa bits; smaller ones lose one more per step down */
    Keep = (Exponent >= -14) ? 10 : (UINT)(10 - (-14 - Exponent));
    return Bits & ~((1u << (23 - Keep)) - 1);
}

VOID
RxWarpMachine::HalfTowardZero(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Values;

    if (Instruction.OperandCount < 2)
        return;
    FetchAll(Instruction.Operands[1], TRUE, Values);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        for (UINT Component = 0; Component < 4; Component++)
            Values[Lane][Component] = RxWarpHalfTowardZero(Values[Lane][Component]);
    }
    Store(Instruction, 0, Values, FALSE);
}

/* The identity a prefix starts from */
static
UINT64
RxWarpWaveIdentity(
    _In_ UINT Variant,
    _In_ UINT Type)
{
    if (Variant != 1)
        return 0;
    if ((Type & 0xFF) != RxWarpWaveFloat)
        return 1;
    return (Type & RxWarpWaveWide) ? 0x3FF0000000000000ull : 0x3F800000;
}

/*
 * Wave operations over the running lanes that are not helpers. Reductions pair lanes two apart
 * first and then neighbors, the order the reference's float results show.
 */
VOID
RxWarpMachine::Wave(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Words = {};
    RX_WARP_LANE_VALUES Where = {};
    RX_WARP_LANE_VALUES Result = {};
    UINT64 Value[RxWarpLanes];
    UINT Lanes = Active & Invocation->Live & ~Invocation->Helper;
    UINT Kind;
    UINT Variant;
    UINT Type;
    BOOLEAN Wide;
    UINT First;

    if (Instruction.OperandCount < 4)
        return;
    Kind = Instruction.Operands[3].Immediate[0] & 0xFF;
    Variant = (Instruction.Operands[3].Immediate[0] >> 8) & 0xFF;
    Wide = (Instruction.Operands[3].Immediate[0] & RxWarpWaveWide) != 0;
    Type = ((Instruction.Operands[3].Immediate[0] >> 16) & 0xFF) | (Wide ? RxWarpWaveWide : 0);
    FetchAll(Instruction.Operands[1], TRUE, Words);
    FetchAll(Instruction.Operands[2], TRUE, Where);
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        Value[Lane] = Words[Lane][0] | (Wide ? ((UINT64)Words[Lane][1] << 32) : 0);

    /* Reads of other lanes see zero from lanes that are not running; helper lanes run */
    if (Kind == RxWarpWaveReadLane || Kind == RxWarpWaveQuadRead || Kind == RxWarpWaveQuadSwap)
    {
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            if (!(Active & (1u << Lane)))
                Value[Lane] = 0;
        }
    }
    if (Lanes == 0)
        return;
    First = 0;
    while (!(Lanes & (1u << First)))
        First++;

    /* Pairs of lanes 0 and 2, 1 and 3, then the two pair results */
    UINT64 Totals[2] = {};

    if (Kind == RxWarpWaveReduce || Kind == RxWarpWaveBits)
    {
        UINT Code = (Kind == RxWarpWaveBits) ? Variant + 4 : Variant;
        UINT64 Pair[2] = {};
        UINT Filled = 0;

        for (UINT Half = 0; Half < 2; Half++)
        {
            UINT Low = Half;
            UINT High = Half + 2;

            if ((Lanes & (1u << Low)) && (Lanes & (1u << High)))
                Pair[Half] = RxWarpWaveCombine(Code, Type, Value[Low], Value[High]);
            else if (Lanes & (1u << Low))
                Pair[Half] = Value[Low];
            else if (Lanes & (1u << High))
                Pair[Half] = Value[High];
            else
                continue;
            Filled |= 1u << Half;
        }
        Totals[0] = (Filled == 3) ? RxWarpWaveCombine(Code, Type, Pair[0], Pair[1]) : Pair[(Filled == 2) ? 1 : 0];
        Totals[1] = Totals[0];
    }

    /*
     * Double sums and products run on lane pairs: lanes 0 and 1 against each other, then lanes
     * 2 and 3 added in, and then 3 and 2; even lanes take the first result and odd lanes the
     * second. Lanes not running count as the identity. Fitted to the reference's outputs.
     */
    if (Kind == RxWarpWaveReduce && Variant <= 1 && Type == (RxWarpWaveFloat | RxWarpWaveWide))
    {
        UINT64 Held[RxWarpLanes];

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            Held[Lane] = (Lanes & (1u << Lane)) ? Value[Lane] : RxWarpWaveIdentity(Variant, Type);
        for (UINT Side = 0; Side < 2; Side++)
        {
            Totals[Side] = RxWarpWaveCombine(Variant, Type, Held[Side], Held[Side ^ 1]);
            Totals[Side] = RxWarpWaveCombine(Variant, Type, Totals[Side], Held[2 + Side]);
            Totals[Side] = RxWarpWaveCombine(Variant, Type, Totals[Side], Held[3 - Side]);
        }
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        UINT *Out = Result[Lane];
        UINT Below = Lanes & ((1u << Lane) - 1);
        UINT Matches = 1;
        UINT64 Scalar = 0;

        switch (Kind)
        {
            case RxWarpWaveLaneIndex:
                Scalar = Lane;
                break;
            case RxWarpWaveIsHelper:
                Scalar = (Invocation->Helper & (1u << Lane)) ? UINT_MAX : 0;
                break;
            case RxWarpWaveFirstLane:
                Scalar = (Lane == First) ? UINT_MAX : 0;
                break;
            case RxWarpWaveAnyTrue:
            case RxWarpWaveAllTrue:
            case RxWarpWaveBallot:
            case RxWarpWaveCount:
            case RxWarpWavePrefixCount:
            {
                UINT Set = 0;

                for (UINT Other = 0; Other < RxWarpLanes; Other++)
                {
                    if ((Lanes & (1u << Other)) && Value[Other] != 0)
                        Set |= 1u << Other;
                }
                if (Kind == RxWarpWaveAnyTrue)
                    Scalar = (Set != 0) ? UINT_MAX : 0;
                else if (Kind == RxWarpWaveAllTrue)
                    Scalar = (Set == Lanes) ? UINT_MAX : 0;
                else if (Kind == RxWarpWaveBallot)
                    Scalar = Set;
                else if (Kind == RxWarpWaveCount)
                    Scalar = RxWarpCountBits(Set);
                else
                    Scalar = RxWarpCountBits(Set & Below);
                break;
            }
            case RxWarpWaveAllEqual:
                for (UINT Other = 0; Other < RxWarpLanes; Other++)
                {
                    double Left;
                    double Right;

                    if (!(Lanes & (1u << Other)))
                        continue;
                    if (Type == (RxWarpWaveFloat | RxWarpWaveWide))
                    {
                        memcpy(&Left, &Value[Other], sizeof(Left));
                        memcpy(&Right, &Value[First], sizeof(Right));
                        Matches &= (Left == Right);
                    }
                    else if (Type == RxWarpWaveFloat)
                    {
                        Matches &= (RxWarpAsFloat((UINT)Value[Other]) == RxWarpAsFloat((UINT)Value[First]));
                    }
                    else
                    {
                        Matches &= (Value[Other] == Value[First]);
                    }
                }
                Scalar = Matches ? UINT_MAX : 0;
                break;
            case RxWarpWaveReadLane:
            case RxWarpWaveQuadRead:
                /* A lane past the wave reads as zero */
                Scalar = (Where[Lane][0] < RxWarpLanes) ? Value[Where[Lane][0]] : 0;
                break;
            case RxWarpWaveMatch:
                for (UINT Other = 0; Other < RxWarpLanes; Other++)
                {
                    BOOLEAN Same = Value[Other] == Value[Lane];

                    if (Type == RxWarpWaveFloat)
                    {
                        Same = RxWarpAsFloat((UINT)Value[Other]) == RxWarpAsFloat((UINT)Value[Lane]);
                    }
                    else if (Type == (RxWarpWaveFloat | RxWarpWaveWide))
                    {
                        double Left;
                        double Right;

                        memcpy(&Left, &Value[Other], sizeof(Left));
                        memcpy(&Right, &Value[Lane], sizeof(Right));
                        Same = Left == Right;
                    }
                    if ((Lanes & (1u << Other)) && Same)
                        Scalar |= 1u << Other;
                }
                break;
            case RxWarpWaveMultiPrefix:
            case RxWarpWaveMultiCount:
            {
                static const UINT Codes[] = { 0, 4, 5, 6, 1 };
                UINT Members = Lanes & Below & Where[Lane][0];
                UINT Code = Codes[(Variant < ARRAYSIZE(Codes)) ? Variant : 0];

                if (Kind == RxWarpWaveMultiCount)
                {
                    for (UINT Other = 0; Other < Lane; Other++)
                        Scalar += ((Members & (1u << Other)) && Value[Other] != 0) ? 1 : 0;
                    break;
                }
                Scalar = (Variant == 4) ? RxWarpWaveIdentity(1, Type) : (Variant == 1) ? UINT64_MAX : 0;
                for (UINT Other = 0; Other < Lane; Other++)
                {
                    if (Members & (1u << Other))
                        Scalar = RxWarpWaveCombine(Code, Type, Scalar, Value[Other]);
                }
                break;
            }
            case RxWarpWaveReadFirst:
                /* The reference's 64 bit form merges every running lane's bits instead of picking the first */
                Scalar = Value[First];
                for (UINT Other = 0; Other < RxWarpLanes && Wide; Other++)
                {
                    if (Lanes & (1u << Other))
                        Scalar |= Value[Other];
                }
                break;
            case RxWarpWaveReduce:
            case RxWarpWaveBits:
                Scalar = Totals[Lane & 1];
                break;
            case RxWarpWavePrefix:
                Scalar = RxWarpWaveIdentity(Variant, Type);
                for (UINT Other = 0; Other < Lane; Other++)
                {
                    if (Below & (1u << Other))
                        Scalar = RxWarpWaveCombine(Variant, Type, Scalar, Value[Other]);
                }
                break;
            case RxWarpWaveQuadSwap:
                Scalar = Value[Lane ^ (Variant + 1)];
                break;
            default:
                break;
        }

        /* Scalars land in whichever component the destination masks, 64 bit ones as pairs; a ballot fills x to w */
        Out[0] = Out[2] = (UINT)Scalar;
        Out[1] = Out[3] = (Wide && Kind != RxWarpWaveAllEqual) ? (UINT)(Scalar >> 32) : (UINT)Scalar;
        if (Kind == RxWarpWaveBallot || Kind == RxWarpWaveMatch)
            Out[1] = Out[2] = Out[3] = 0;
    }
    Store(Instruction, 0, Result, FALSE);
}

VOID
RxWarpMachine::Texture(
    _In_ const RX_WARP_INSTRUCTION &Instruction,
    _In_opt_ const RX_WARP_OPERAND *Clamp)
{
    RX_WARP_LANE_VALUES Lowest = {};
    RX_WARP_LANE_VALUES Address = {};
    RX_WARP_LANE_VALUES Result = {};
    RX_WARP_LANE_VALUES Extra = {};
    RX_WARP_LANE_VALUES DerivX = {};
    RX_WARP_LANE_VALUES DerivY = {};
    const RX_WARP_OPERAND *Resource;
    const RX_WARP_OPERAND *SamplerOperand = nullptr;
    RX_WARP_LANE_VALUES Offsets = {};
    BOOLEAN Filtered[RxWarpLanes] = {};
    INT FilteredFixed[RxWarpLanes][4] = {};
    RX_WARP_SAMPLE_REQUEST Request = {};
    UINT Opcode = Instruction.Opcode;
    BOOLEAN Programmable;
    UINT Shift;
    UINT Slot;

    if (Opcode == D3D10_SB_OPCODE_RESINFO)
    {
        RX_WARP_LANE_VALUES Mip;

        if (Instruction.OperandCount < 3)
            return;
        Resource = &Instruction.Operands[2];
        Slot = Resource->Index[0].Immediate;
        FetchAll(Instruction.Operands[1], TRUE, Mip);
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            UINT Raw[4];
            RxWarpView *View = nullptr;

            if (Resource->Type == D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW)
                View = (Slot < 64) ? Bindings->Uavs[Slot] : nullptr;
            else
                View = (Slot < 128) ? Bindings->Views[Slot] : nullptr;
            RxWarpResourceInfo(View, Mip[Lane][0], Instruction.ReturnType, Raw);
            for (UINT Component = 0; Component < 4; Component++)
                Result[Lane][Component] = Raw[Resource->Swizzle[Component]];
        }
        Store(Instruction, 0, Result, FALSE);
        return;
    }

    /* Sample counts and positions, of a resource or of the bound targets */
    if (Opcode == D3D10_1_SB_OPCODE_SAMPLE_INFO || Opcode == D3D10_1_SB_OPCODE_SAMPLE_POS)
    {
        RX_WARP_LANE_VALUES Index = {};
        UINT Count = 0;

        if (Instruction.OperandCount < 2)
            return;
        Resource = &Instruction.Operands[1];
        if (Resource->Type == D3D10_SB_OPERAND_TYPE_RASTERIZER)
        {
            Count = Invocation->RasterSamples;
        }
        else
        {
            Slot = Resource->Index[0].Immediate;
            if (Slot < 128 && Bindings->Views[Slot] != nullptr && Bindings->Views[Slot]->Resource != nullptr)
                Count = Bindings->Views[Slot]->Resource->SampleCount;
        }
        if (Opcode == D3D10_1_SB_OPCODE_SAMPLE_POS && Instruction.OperandCount > 2)
            FetchAll(Instruction.Operands[2], TRUE, Index);
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            UINT Raw[4] = {};

            if (Opcode == D3D10_1_SB_OPCODE_SAMPLE_INFO)
            {
                Raw[0] = (Instruction.ReturnType == D3D10_SB_RESINFO_INSTRUCTION_RETURN_RCPFLOAT) ? Count : RxWarpAsUint((FLOAT)Count);
            }
            else
            {
                FLOAT Position[2];

                RxWarpSamplePosition(Count, Index[Lane][0], Position);
                Raw[0] = RxWarpAsUint(Position[0]);
                Raw[1] = RxWarpAsUint(Position[1]);
            }
            for (UINT Component = 0; Component < 4; Component++)
                Result[Lane][Component] = Raw[Resource->Swizzle[Component]];
        }
        Store(Instruction, 0, Result, FALSE);
        return;
    }

    if (Opcode == D3D11_SB_OPCODE_BUFINFO)
    {
        RX_WARP_REGION Memory;
        UINT Count = 0;

        if (Instruction.OperandCount < 2)
            return;
        Resource = &Instruction.Operands[1];
        if (Region(*Resource, &Memory) && Memory.View != nullptr)
        {
            if (Memory.Dimension == D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER)
                Count = (UINT)Memory.Size;
            else
                Count = Memory.View->ElementCount;
        }
        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            for (UINT Component = 0; Component < 4; Component++)
                Result[Lane][Component] = Count;
        }
        Store(Instruction, 0, Result, FALSE);
        return;
    }

    /* Programmable offsets come before the resource */
    Programmable = (Opcode == D3D11_SB_OPCODE_GATHER4_PO || Opcode == D3D11_SB_OPCODE_GATHER4_PO_C);
    Shift = Programmable ? 1 : 0;
    if (Instruction.OperandCount < 3 + Shift)
        return;
    Resource = &Instruction.Operands[2 + Shift];
    if (Opcode != D3D10_SB_OPCODE_LD && Opcode != D3D10_SB_OPCODE_LD_MS)
    {
        if (Instruction.OperandCount < 4 + Shift)
            return;
        SamplerOperand = &Instruction.Operands[3 + Shift];
    }
    if (Opcode == D3D10_SB_OPCODE_LD_MS && Instruction.OperandCount > 3)
        FetchAll(Instruction.Operands[3], TRUE, Offsets);
    if (Programmable)
        FetchAll(Instruction.Operands[2], TRUE, Offsets);
    Slot = Resource->Index[0].Immediate;

    Request.View = (Slot < 128) ? Bindings->Views[Slot] : nullptr;
    Request.Sampler = (SamplerOperand != nullptr && SamplerOperand->Index[0].Immediate < 16) ?
                      Bindings->Samplers[SamplerOperand->Index[0].Immediate] : nullptr;
    Request.Dimension = (Slot < 128) ? Shader->ResourceDimension[Slot] : 0;
    Request.Offset[0] = Instruction.Offset[0];
    Request.Offset[1] = Instruction.Offset[1];
    Request.Offset[2] = Instruction.Offset[2];

    FetchAll(Instruction.Operands[1], Opcode == D3D10_SB_OPCODE_LD || Opcode == D3D10_SB_OPCODE_LD_MS, Address);

    /* Coordinates read straight from an input are interpolated again in double precision */
    if (Invocation->PreciseRoutine != nullptr && Opcode != D3D10_SB_OPCODE_LD && Opcode != D3D10_SB_OPCODE_LD_MS &&
        Instruction.Operands[1].Type == D3D10_SB_OPERAND_TYPE_INPUT && Instruction.Operands[1].Dimensions == 1 &&
        Instruction.Operands[1].Index[0].RelativeType == RxWarpNoRelative)
    {
        const RX_WARP_OPERAND &Source = Instruction.Operands[1];

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            UINT Raw[4];

            if (!Invocation->PreciseRoutine(Invocation->EvaluateContext, Lane, Source.Index[0].Immediate, Raw))
                break;
            for (UINT Component = 0; Component < 4; Component++)
            {
                UINT Bits = Raw[Source.Swizzle[Component] & 3];

                if (Source.Absolute)
                    Bits &= 0x7FFFFFFF;
                if (Source.Negate)
                    Bits ^= 0x80000000;
                Address[Lane][Component] = Bits;
            }
        }
    }
    if (Instruction.OperandCount > 4 + Shift)
        FetchAll(Instruction.Operands[4 + Shift], FALSE, Extra);
    if (Opcode == D3D10_SB_OPCODE_SAMPLE_D && Instruction.OperandCount > 5)
    {
        memcpy(DerivX, Extra, sizeof(DerivX));
        FetchAll(Instruction.Operands[5], FALSE, DerivY);
    }
    if (Clamp != nullptr)
        FetchAll(*Clamp, FALSE, Lowest);

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        FLOAT Texel[4] = {};
        UINT Raw[4] = {};

        if (!(Active & (1u << Lane)))
            continue;
        Request.Clamped = (Clamp != nullptr);
        Request.LodClamp = RxWarpAsFloat(Lowest[Lane][0]);

        switch (Opcode)
        {
            case D3D10_SB_OPCODE_LD:
                RxWarpLoad(&Request, (const INT *)Address[Lane], Raw);
                break;
            case D3D10_SB_OPCODE_LD_MS:
                Request.Sample = Offsets[Lane][0];
                RxWarpLoad(&Request, (const INT *)Address[Lane], Raw);
                break;
            case D3D10_SB_OPCODE_SAMPLE:
            case D3D10_SB_OPCODE_SAMPLE_B:
            case D3D10_SB_OPCODE_SAMPLE_C:
            case D3D10_1_SB_OPCODE_LOD:
            case D3D10_1_SB_OPCODE_GATHER4:
            case D3D11_SB_OPCODE_GATHER4_C:
            case D3D11_SB_OPCODE_GATHER4_PO:
            case D3D11_SB_OPCODE_GATHER4_PO_C:
            {
                /* Screen space derivatives across the quad, coarse */
                FLOAT Dx[3];
                FLOAT Dy[3];

                for (UINT Component = 0; Component < 3; Component++)
                {
                    Dx[Component] = RxWarpAsFloat(Address[1][Component]) - RxWarpAsFloat(Address[0][Component]);
                    Dy[Component] = RxWarpAsFloat(Address[2][Component]) - RxWarpAsFloat(Address[0][Component]);
                }
                Request.Mode = RxWarpSampleAuto;
                Request.Bias = (Opcode == D3D10_SB_OPCODE_SAMPLE_B) ? RxWarpAsFloat(Extra[Lane][0]) : 0.0f;
                Request.Compare = (Opcode == D3D10_SB_OPCODE_SAMPLE_C || Opcode == D3D11_SB_OPCODE_GATHER4_C ||
                                   Opcode == D3D11_SB_OPCODE_GATHER4_PO_C);
                Request.Reference = RxWarpAsFloat(Extra[Lane][0]);
                if (Opcode == D3D10_1_SB_OPCODE_LOD)
                {
                    RxWarpComputeLod(&Request, Dx, Dy, Texel);
                    for (UINT Component = 0; Component < 4; Component++)
                        Raw[Component] = RxWarpAsUint(Texel[Component]);
                    break;
                }
                if (Opcode == D3D10_1_SB_OPCODE_GATHER4 || Opcode == D3D11_SB_OPCODE_GATHER4_C || Programmable)
                {
                    Request.Gather = TRUE;
                    Request.GatherComponent = SamplerOperand->Swizzle[0];

                    /* The view's mapping decides which channel a gather reads */
                    if (Request.View != nullptr && Request.View->Mapping != 0x1688 && Request.GatherComponent < 4)
                    {
                        UINT Picked = (Request.View->Mapping >> (3 * Request.GatherComponent)) & 7;

                        if (Picked < 4)
                            Request.GatherComponent = Picked;
                    }
                }

                /* Only the low six bits of a programmable offset count, signed */
                if (Programmable)
                {
                    for (UINT Axis = 0; Axis < 2; Axis++)
                        Request.Offset[Axis] = (INT)(Offsets[Lane][Axis] << 26) >> 26;
                }
                RxWarpSample(&Request, (const FLOAT *)Address[Lane], Dx, Dy, Texel);
                for (UINT Component = 0; Component < 4; Component++)
                    Raw[Component] = RxWarpAsUint(Texel[Component]);
                break;
            }
            case D3D10_SB_OPCODE_SAMPLE_L:
            case D3D10_SB_OPCODE_SAMPLE_C_LZ:
                Request.Mode = RxWarpSampleLevel;
                Request.Level = (Opcode == D3D10_SB_OPCODE_SAMPLE_L) ? RxWarpAsFloat(Extra[Lane][0]) : 0.0f;
                Request.Compare = (Opcode == D3D10_SB_OPCODE_SAMPLE_C_LZ);
                Request.Reference = RxWarpAsFloat(Extra[Lane][0]);
                RxWarpSample(&Request, (const FLOAT *)Address[Lane], nullptr, nullptr, Texel);
                for (UINT Component = 0; Component < 4; Component++)
                    Raw[Component] = RxWarpAsUint(Texel[Component]);
                break;
            case D3D10_SB_OPCODE_SAMPLE_D:
                Request.Mode = RxWarpSampleAuto;
                RxWarpSample(&Request, (const FLOAT *)Address[Lane], (const FLOAT *)DerivX[Lane], (const FLOAT *)DerivY[Lane], Texel);
                for (UINT Component = 0; Component < 4; Component++)
                    Raw[Component] = RxWarpAsUint(Texel[Component]);
                break;
        }

        Filtered[Lane] = RxWarpLastByteFilter(FilteredFixed[Lane]);
        if (Request.View != nullptr && Request.View->Mapping != 0x1688)
            RxWarpMapComponents(Request.View, Opcode, Raw, &Filtered[Lane], FilteredFixed[Lane]);
        for (UINT Component = 0; Component < 4; Component++)
            Result[Lane][Component] = Raw[Resource->Swizzle[Component]];
    }
    Store(Instruction, 0, Result, TRUE);

    /* A filtered result written straight to an output keeps its fixed point form for eight bit targets */
    if (Instruction.Operands[0].Type == D3D10_SB_OPERAND_TYPE_OUTPUT && !Instruction.Saturate &&
        Instruction.Operands[0].Index[0].RelativeType == RxWarpNoRelative && Instruction.Operands[0].Index[0].Immediate < RxWarpMaxRegisters)
    {
        UINT Register = Instruction.Operands[0].Index[0].Immediate;

        for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        {
            if (!(Active & (1u << Lane)) || !Filtered[Lane])
                continue;
            for (UINT Component = 0; Component < 4; Component++)
            {
                if (Instruction.Operands[0].Mask & (1u << Component))
                    Invocation->ByteShadow[Register][Lane][Component] = FilteredFixed[Lane][Resource->Swizzle[Component]];
            }
        }
    }
}

VOID
RxWarpInvocation::Prepare(
    _In_ const RxWarpShader *Shader)
{
    Temps.assign((SIZE_T)Shader->TempCount * RxWarpLanes * 4, 0);
    IndexableStorage.assign((SIZE_T)Shader->IndexableRegisters * RxWarpLanes * 4, 0);
    ZeroMemory(Outputs, sizeof(Outputs));
    ZeroMemory(Depth, sizeof(Depth));
    memset(ByteShadow, 0xFF, sizeof(ByteShadow));
    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
        CoverageOut[Lane] = UINT_MAX;
    Discarded = 0;
}

/* Bytes per element of a buffer view: four for raw views, the stride for structured ones, else the format */
static
UINT
RxWarpElementBytes(
    _In_ RxWarpView *View,
    _In_ UINT Stride,
    _In_ BOOLEAN Raw)
{
    const RX_WARP_FORMAT_INFO *Info;

    if (Raw)
        return 4;
    if (Stride != 0)
        return Stride;
    if (View->Resource->ByteStride != 0 && View->Format == DXGI_FORMAT_UNKNOWN)
        return View->Resource->ByteStride;
    Info = RxWarpFormatInfo(View->Format);
    return (Info != nullptr && Info->Bytes != 0) ? Info->Bytes : 4;
}

BOOLEAN
RxWarpMachine::Region(
    _In_ const RX_WARP_OPERAND &Operand,
    _Out_ RX_WARP_REGION *Region)
{
    UINT Slot = Operand.Index[0].Immediate;
    RxWarpView *View;
    BOOLEAN Raw;
    BOOLEAN Structured;
    UINT Bytes;
    SIZE_T Start;

    ZeroMemory(Region, sizeof(*Region));
    if (Operand.Type == D3D11_SB_OPERAND_TYPE_THREAD_GROUP_SHARED_MEMORY)
    {
        if (Slot >= Shader->Shared.size() || Invocation->SharedMemory == nullptr)
            return FALSE;
        Region->Data = Invocation->SharedMemory + Shader->Shared[Slot].Offset;
        Region->Size = Shader->Shared[Slot].Size;
        Region->Stride = Shader->Shared[Slot].Stride;
        return TRUE;
    }

    if (Operand.Type == D3D11_SB_OPERAND_TYPE_UNORDERED_ACCESS_VIEW && Slot < 64)
    {
        View = Bindings->Uavs[Slot];
        Region->Dimension = Shader->UavDimension[Slot];
        Region->Stride = Shader->UavStride[Slot];
    }
    else if (Operand.Type == D3D10_SB_OPERAND_TYPE_RESOURCE && Slot < 128)
    {
        View = Bindings->Views[Slot];
        Region->Dimension = Shader->ResourceDimension[Slot];
        Region->Stride = Shader->ResourceStride[Slot];
    }
    else
    {
        return FALSE;
    }
    if (View == nullptr || View->Resource == nullptr || View->Resource->Memory == nullptr)
        return FALSE;

    Region->View = View;
    Raw = (Region->Dimension == D3D11_SB_RESOURCE_DIMENSION_RAW_BUFFER);
    Structured = (Region->Dimension == D3D11_SB_RESOURCE_DIMENSION_STRUCTURED_BUFFER);
    Region->Typed = !Raw && !Structured;
    if (Region->Typed && View->Resource->Dimension != D3D10DDIRESOURCE_BUFFER)
        return TRUE;

    Bytes = RxWarpElementBytes(View, Region->Stride, Raw);
    if (Structured && Region->Stride == 0)
        Region->Stride = Bytes;
    Start = (SIZE_T)View->FirstElement * Bytes;
    if (Start > View->Resource->MemorySize)
        return FALSE;
    Region->Data = View->Resource->Memory + Start;
    Region->Size = (SIZE_T)View->ElementCount * Bytes;
    if (Region->Size > View->Resource->MemorySize - Start)
        Region->Size = View->Resource->MemorySize - Start;
    return TRUE;
}

/* The element of a typed view that an integer address names, or null outside the view */
static
BYTE *
RxWarpTypedElement(
    _In_ const RX_WARP_REGION *Region,
    _In_reads_(4) const UINT *Address,
    _Out_ UINT *Bytes)
{
    RxWarpView *View = Region->View;
    RxWarpResource *Resource = View->Resource;
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
    RX_WARP_SUBRESOURCE *Sub;
    UINT X = Address[0];
    UINT Y = 0;
    UINT Z = 0;
    UINT Slice = 0;

    *Bytes = 0;
    if (Info == nullptr || Info->Bytes == 0)
        return nullptr;
    *Bytes = Info->Bytes;

    if (Resource->Dimension == D3D10DDIRESOURCE_BUFFER)
    {
        SIZE_T Offset = (SIZE_T)X * Info->Bytes;

        if (Region->Data == nullptr || X >= View->ElementCount || Offset + Info->Bytes > Region->Size)
            return nullptr;
        return Region->Data + Offset;
    }

    switch (Region->Dimension)
    {
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE1DARRAY:
            Slice = Address[1];
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2D:
            Y = Address[1];
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE2DARRAY:
            Y = Address[1];
            Slice = Address[2];
            break;
        case D3D10_SB_RESOURCE_DIMENSION_TEXTURE3D:
            Y = Address[1];
            Z = Address[2];
            break;
        default:
            break;
    }

    if (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D)
    {
        Sub = Resource->Subresource(View->MipSlice);
        Z += View->FirstSlice;
        if (Sub == nullptr || Z >= Sub->Depth || Z >= View->FirstSlice + View->SliceCount)
            return nullptr;
    }
    else
    {
        if (Slice >= (View->SliceCount ? View->SliceCount : 1))
            return nullptr;
        Sub = Resource->Subresource((View->FirstSlice + Slice) * Resource->MipLevels + View->MipSlice);
    }
    if (Sub == nullptr || X >= Sub->Width || Y >= Sub->Height)
        return nullptr;
    return Resource->Memory + Sub->Offset + (SIZE_T)Z * Sub->DepthPitch + (SIZE_T)Y * Sub->RowPitch + (SIZE_T)X * Info->Bytes;
}

/* The DWORD an atomic works on: raw byte offset, structure and offset, or a typed element */
static
UINT *
RxWarpAtomicTarget(
    _In_ const RX_WARP_REGION *Region,
    _In_reads_(4) const UINT *Address)
{
    SIZE_T Offset;
    UINT Bytes;

    if (Region->Typed && Region->View != nullptr)
    {
        BYTE *Element = RxWarpTypedElement(Region, Address, &Bytes);

        return (Element != nullptr && Bytes == 4) ? (UINT *)Element : nullptr;
    }
    Offset = (Region->Stride != 0) ? (SIZE_T)Address[0] * Region->Stride + Address[1] : Address[0];
    if (Region->Data == nullptr || (Offset & 3) != 0 || Offset + 4 > Region->Size)
        return nullptr;
    return (UINT *)(Region->Data + Offset);
}

static
UINT
RxWarpAtomicApply(
    _In_ UINT Opcode,
    _In_ UINT Old,
    _In_ UINT Value,
    _In_ UINT Compare,
    _Out_ BOOLEAN *Write)
{
    *Write = TRUE;
    switch (Opcode)
    {
        case D3D11_SB_OPCODE_ATOMIC_AND:
        case D3D11_SB_OPCODE_IMM_ATOMIC_AND:
            return Old & Value;
        case D3D11_SB_OPCODE_ATOMIC_OR:
        case D3D11_SB_OPCODE_IMM_ATOMIC_OR:
            return Old | Value;
        case D3D11_SB_OPCODE_ATOMIC_XOR:
        case D3D11_SB_OPCODE_IMM_ATOMIC_XOR:
            return Old ^ Value;
        case D3D11_SB_OPCODE_ATOMIC_IADD:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IADD:
            return Old + Value;
        case D3D11_SB_OPCODE_ATOMIC_IMAX:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IMAX:
            return ((INT)Value > (INT)Old) ? Value : Old;
        case D3D11_SB_OPCODE_ATOMIC_IMIN:
        case D3D11_SB_OPCODE_IMM_ATOMIC_IMIN:
            return ((INT)Value < (INT)Old) ? Value : Old;
        case D3D11_SB_OPCODE_ATOMIC_UMAX:
        case D3D11_SB_OPCODE_IMM_ATOMIC_UMAX:
            return (Value > Old) ? Value : Old;
        case D3D11_SB_OPCODE_ATOMIC_UMIN:
        case D3D11_SB_OPCODE_IMM_ATOMIC_UMIN:
            return (Value < Old) ? Value : Old;
        case D3D11_SB_OPCODE_IMM_ATOMIC_EXCH:
            return Value;
        case D3D11_SB_OPCODE_ATOMIC_CMP_STORE:
        case D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH:
            *Write = (Old == Compare);
            return Value;
        default:
            *Write = FALSE;
            return Old;
    }
}

VOID
RxWarpMachine::Access(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Result = {};
    RX_WARP_LANE_VALUES First = {};
    RX_WARP_LANE_VALUES Second = {};
    RX_WARP_LANE_VALUES Third = {};
    RX_WARP_REGION Memory;
    UINT Opcode = Instruction.Opcode;
    const RX_WARP_OPERAND *Target;

    /* Helper lanes only exist for derivatives and must not change memory */
    UINT Writers = Active & Invocation->Live;

    switch (Opcode)
    {
        case D3D11_SB_OPCODE_LD_RAW:
        case D3D11_SB_OPCODE_LD_STRUCTURED:
        {
            BOOLEAN Structured = (Opcode == D3D11_SB_OPCODE_LD_STRUCTURED);

            Target = &Instruction.Operands[Structured ? 3 : 2];
            if (!Region(*Target, &Memory))
            {
                Store(Instruction, 0, Result, FALSE);
                return;
            }
            FetchAll(Instruction.Operands[1], TRUE, First);
            if (Structured)
                FetchAll(Instruction.Operands[2], TRUE, Second);
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                SIZE_T Offset = Structured ? (SIZE_T)First[Lane][0] * Memory.Stride + Second[Lane][0] : First[Lane][0];
                UINT Raw[4] = {};

                if (!(Active & (1u << Lane)))
                    continue;
                for (UINT Component = 0; Component < 4; Component++)
                {
                    SIZE_T At = Offset + (SIZE_T)Component * 4;

                    /* A word a 16 bit load reads across the end keeps its two bytes inside */
                    if (Memory.Data != nullptr && At + 4 <= Memory.Size)
                        memcpy(&Raw[Component], Memory.Data + At, 4);
                    else if (Memory.Data != nullptr && At + 2 == Memory.Size)
                        memcpy(&Raw[Component], Memory.Data + At, 2);
                }
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Lane][Component] = Raw[Target->Swizzle[Component] & 3];
            }
            Store(Instruction, 0, Result, FALSE);
            return;
        }
        case D3D11_SB_OPCODE_STORE_RAW:
        case D3D11_SB_OPCODE_STORE_STRUCTURED:
        case RxWarpOpcodeStoreHalves:
        {
            BOOLEAN Structured = (Opcode == D3D11_SB_OPCODE_STORE_STRUCTURED) ||
                                 (Opcode == RxWarpOpcodeStoreHalves && Instruction.OperandCount == 4);
            SIZE_T Unit = (Opcode == RxWarpOpcodeStoreHalves) ? 2 : 4;
            const RX_WARP_OPERAND &Value = Instruction.Operands[Structured ? 3 : 2];

            Target = &Instruction.Operands[0];
            if (!Region(*Target, &Memory) || Memory.Data == nullptr)
                return;
            FetchAll(Instruction.Operands[1], TRUE, First);
            if (Structured)
                FetchAll(Instruction.Operands[2], TRUE, Second);
            FetchAll(Value, TRUE, Third);
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                SIZE_T Offset = Structured ? (SIZE_T)First[Lane][0] * Memory.Stride + Second[Lane][0] : First[Lane][0];

                if (!(Writers & (1u << Lane)))
                    continue;
                for (UINT Component = 0; Component < 4; Component++)
                {
                    SIZE_T At = Offset + (SIZE_T)Component * Unit;

                    if ((Target->Mask & (1u << Component)) && At + Unit <= Memory.Size)
                        memcpy(Memory.Data + At, &Third[Lane][Component], Unit);
                }
            }
            return;
        }
        case D3D11_SB_OPCODE_LD_UAV_TYPED:
            Target = &Instruction.Operands[2];
            if (!Region(*Target, &Memory))
            {
                Store(Instruction, 0, Result, FALSE);
                return;
            }
            FetchAll(Instruction.Operands[1], TRUE, First);
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                BYTE Element[16] = {};
                UINT Raw[4] = {};
                UINT Bytes;
                BYTE *Texel;

                if (!(Active & (1u << Lane)))
                    continue;
                Texel = RxWarpTypedElement(&Memory, First[Lane], &Bytes);
                if (Texel != nullptr)
                {
                    memcpy(Element, Texel, (Bytes <= 16) ? Bytes : 16);
                    RxWarpDecodeTexel(Memory.View->Format, Element, Raw);
                }
                for (UINT Component = 0; Component < 4; Component++)
                    Result[Lane][Component] = Raw[Target->Swizzle[Component] & 3];
            }
            Store(Instruction, 0, Result, FALSE);
            return;
        case D3D11_SB_OPCODE_STORE_UAV_TYPED:
            Target = &Instruction.Operands[0];
            if (!Region(*Target, &Memory))
                return;
            FetchAll(Instruction.Operands[1], TRUE, First);
            FetchAll(Instruction.Operands[2], TRUE, Second);
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                BYTE Element[16];
                UINT Bytes;
                BYTE *Texel;
                BOOLEAN Encoded;

                if (!(Writers & (1u << Lane)))
                    continue;
                Texel = RxWarpTypedElement(&Memory, First[Lane], &Bytes);
                if (Texel == nullptr)
                    continue;
                if (RxWarpFormatIsInteger(Memory.View->Format))
                    Encoded = RxWarpEncodeUint(Memory.View->Format, Second[Lane], Element);
                else
                    Encoded = RxWarpEncodeColor(Memory.View->Format, (const FLOAT *)Second[Lane], Element);
                if (Encoded)
                    memcpy(Texel, Element, Bytes);
            }
            return;
        case D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC:
        case D3D11_SB_OPCODE_IMM_ATOMIC_CONSUME:
            Target = &Instruction.Operands[1];
            if (!Region(*Target, &Memory))
                return;
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                if (!(Writers & (1u << Lane)))
                    continue;
                UINT *Count = (Memory.View->CounterMemory != nullptr) ? Memory.View->CounterMemory : &Memory.View->Counter;

                if (Opcode == D3D11_SB_OPCODE_IMM_ATOMIC_ALLOC)
                {
                    Result[Lane][0] = *Count;
                    *Count += 1;
                }
                else
                {
                    *Count -= 1;
                    Result[Lane][0] = *Count;
                }
                Result[Lane][1] = Result[Lane][2] = Result[Lane][3] = Result[Lane][0];
            }
            Store(Instruction, 0, Result, FALSE);
            return;
        default:
        {
            /* Atomics: the immediate forms return the old value through an extra first operand */
            BOOLEAN Immediate = (Opcode >= D3D11_SB_OPCODE_IMM_ATOMIC_IADD);
            UINT Base = Immediate ? 1 : 0;
            BOOLEAN Compares = (Opcode == D3D11_SB_OPCODE_ATOMIC_CMP_STORE || Opcode == D3D11_SB_OPCODE_IMM_ATOMIC_CMP_EXCH);

            Target = &Instruction.Operands[Base];
            if (!Region(*Target, &Memory))
            {
                if (Immediate)
                    Store(Instruction, 0, Result, FALSE);
                return;
            }
            FetchAll(Instruction.Operands[Base + 1], TRUE, First);
            if (Compares)
            {
                FetchAll(Instruction.Operands[Base + 2], TRUE, Third);
                FetchAll(Instruction.Operands[Base + 3], TRUE, Second);
            }
            else
            {
                FetchAll(Instruction.Operands[Base + 2], TRUE, Second);
            }
            for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
            {
                UINT *Word;
                UINT Old;
                UINT New;
                BOOLEAN Write;

                if (!(Writers & (1u << Lane)))
                    continue;
                Word = RxWarpAtomicTarget(&Memory, First[Lane]);
                if (Word == nullptr)
                    continue;
                Old = *Word;
                New = RxWarpAtomicApply(Opcode, Old, Second[Lane][0], Third[Lane][0], &Write);
                if (Write)
                    *Word = New;
                Result[Lane][0] = Result[Lane][1] = Result[Lane][2] = Result[Lane][3] = Old;
            }
            if (Immediate)
                Store(Instruction, 0, Result, FALSE);
            return;
        }
    }
}

BOOLEAN
RxWarpInvocation::Resume(
    _In_ const RxWarpShader *Shader,
    _In_ const RX_WARP_STAGE_BINDINGS *Bindings,
    _In_ BOOLEAN Restart)
{
    RxWarpMachine Machine(Shader, this, Bindings);

    if (Restart)
    {
        Frames.clear();
        Active = Live | Helper;
        Dead = 0;
        Next = 0;
    }
    return Machine.Execute();
}

VOID
RxWarpInvocation::Run(
    _In_ const RxWarpShader *Shader,
    _In_ const RX_WARP_STAGE_BINDINGS *Bindings)
{
    Resume(Shader, Bindings, TRUE);
}
