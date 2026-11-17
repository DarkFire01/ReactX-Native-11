/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs decoded shader programs over four lanes with per lane masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"
#include "sample.hpp"

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

/* Block bookkeeping for structured flow; masks hold one bit per lane */
typedef struct _RX_WARP_FRAME
{
    UINT16 Opcode;
    UINT Start;             /* first instruction of a loop body */
    UINT Saved;             /* lanes running when the block began */
    UINT Else;              /* lanes for the else branch */
    UINT Removed;           /* lanes that left through break or continue of an outer block */
    UINT Continued;         /* loop lanes waiting for the next iteration */
    UINT Selector;          /* switch: index into the shader's case lists */
    UINT Matched;           /* switch: lanes that already took a case */
} RX_WARP_FRAME;

class RxWarpMachine
{
public:
    RxWarpMachine(
        _In_ const RxWarpShader *Program,
        _In_ RxWarpInvocation *Context,
        _In_ const RX_WARP_STAGE_BINDINGS *Stage)
        : Shader(Program), Invocation(Context), Bindings(Stage)
    {
    }

    VOID
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
    Texture(
        _In_ const RX_WARP_INSTRUCTION &Instruction);

    const RxWarpShader *Shader;
    RxWarpInvocation *Invocation;
    const RX_WARP_STAGE_BINDINGS *Bindings;
    std::vector<RX_WARP_FRAME> Frames;
    UINT Active = 0;
    UINT Dead = 0;
    UINT Next = 0;
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
            Scratch[0] = Scratch[1] = Scratch[2] = Scratch[3] = 1;
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

VOID
RxWarpMachine::Execute(VOID)
{
    const std::vector<RX_WARP_INSTRUCTION> &Program = Shader->Program;

    Active = Invocation->Live | Invocation->Helper;
    Next = 0;

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
                if (!Frames.empty())
                    Frames.back().Removed |= Frame.Removed;
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
                if (!Frames.empty())
                    Frames.back().Removed |= Done.Removed;
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
                if (!Frames.empty())
                    Frames.back().Removed |= Frame.Removed;
                break;
            }
            case D3D10_SB_OPCODE_RET:
                if (Frames.empty())
                    return;
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
            case D3D11_SB_OPCODE_BUFINFO:
                if (Active != 0)
                    Texture(Instruction);
                break;
            default:
                if (Active != 0)
                    Arithmetic(Instruction);
                break;
        }
    }
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
            RxWarpFloatOp(3, Source, Result, [](FLOAT A, FLOAT B, FLOAT C) { return A * B + C; });
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

VOID
RxWarpMachine::Texture(
    _In_ const RX_WARP_INSTRUCTION &Instruction)
{
    RX_WARP_LANE_VALUES Address = {};
    RX_WARP_LANE_VALUES Result = {};
    RX_WARP_LANE_VALUES Extra = {};
    RX_WARP_LANE_VALUES DerivX = {};
    RX_WARP_LANE_VALUES DerivY = {};
    const RX_WARP_OPERAND *Resource;
    const RX_WARP_OPERAND *SamplerOperand = nullptr;
    RX_WARP_SAMPLE_REQUEST Request = {};
    UINT Opcode = Instruction.Opcode;
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

            RxWarpResourceInfo((Slot < 128) ? Bindings->Views[Slot] : nullptr, Mip[Lane][0], Instruction.ReturnType, Raw);
            for (UINT Component = 0; Component < 4; Component++)
                Result[Lane][Component] = Raw[Resource->Swizzle[Component]];
        }
        Store(Instruction, 0, Result, FALSE);
        return;
    }

    if (Opcode == D3D11_SB_OPCODE_BUFINFO)
        return;

    if (Instruction.OperandCount < 3)
        return;
    Resource = &Instruction.Operands[2];
    if (Opcode != D3D10_SB_OPCODE_LD)
    {
        if (Instruction.OperandCount < 4)
            return;
        SamplerOperand = &Instruction.Operands[3];
    }
    Slot = Resource->Index[0].Immediate;

    Request.View = (Slot < 128) ? Bindings->Views[Slot] : nullptr;
    Request.Sampler = (SamplerOperand != nullptr && SamplerOperand->Index[0].Immediate < 16) ?
                      Bindings->Samplers[SamplerOperand->Index[0].Immediate] : nullptr;
    Request.Dimension = (Slot < 128) ? Shader->ResourceDimension[Slot] : 0;
    Request.Offset[0] = Instruction.Offset[0];
    Request.Offset[1] = Instruction.Offset[1];
    Request.Offset[2] = Instruction.Offset[2];

    FetchAll(Instruction.Operands[1], Opcode == D3D10_SB_OPCODE_LD, Address);
    if (Instruction.OperandCount > 4)
        FetchAll(Instruction.Operands[4], FALSE, Extra);
    if (Opcode == D3D10_SB_OPCODE_SAMPLE_D && Instruction.OperandCount > 5)
    {
        memcpy(DerivX, Extra, sizeof(DerivX));
        FetchAll(Instruction.Operands[5], FALSE, DerivY);
    }

    for (UINT Lane = 0; Lane < RxWarpLanes; Lane++)
    {
        FLOAT Texel[4] = {};
        UINT Raw[4] = {};

        if (!(Active & (1u << Lane)))
            continue;

        switch (Opcode)
        {
            case D3D10_SB_OPCODE_LD:
                RxWarpLoad(&Request, (const INT *)Address[Lane], Raw);
                break;
            case D3D10_SB_OPCODE_SAMPLE:
            case D3D10_SB_OPCODE_SAMPLE_B:
            case D3D10_SB_OPCODE_SAMPLE_C:
            case D3D10_1_SB_OPCODE_LOD:
            case D3D10_1_SB_OPCODE_GATHER4:
            case D3D11_SB_OPCODE_GATHER4_C:
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
                Request.Compare = (Opcode == D3D10_SB_OPCODE_SAMPLE_C || Opcode == D3D11_SB_OPCODE_GATHER4_C);
                Request.Reference = RxWarpAsFloat(Extra[Lane][0]);
                if (Opcode == D3D10_1_SB_OPCODE_LOD)
                {
                    RxWarpComputeLod(&Request, Dx, Dy, Texel);
                    for (UINT Component = 0; Component < 4; Component++)
                        Raw[Component] = RxWarpAsUint(Texel[Component]);
                    break;
                }
                if (Opcode == D3D10_1_SB_OPCODE_GATHER4 || Opcode == D3D11_SB_OPCODE_GATHER4_C)
                {
                    Request.Gather = TRUE;
                    Request.GatherComponent = SamplerOperand->Swizzle[0];
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

        for (UINT Component = 0; Component < 4; Component++)
            Result[Lane][Component] = Raw[Resource->Swizzle[Component]];
    }
    Store(Instruction, 0, Result, TRUE);
}

VOID
RxWarpInvocation::Prepare(
    _In_ const RxWarpShader *Shader)
{
    Temps.assign((SIZE_T)Shader->TempCount * RxWarpLanes * 4, 0);
    IndexableStorage.assign((SIZE_T)Shader->IndexableRegisters * RxWarpLanes * 4, 0);
    ZeroMemory(Outputs, sizeof(Outputs));
    ZeroMemory(Depth, sizeof(Depth));
    Discarded = 0;
}

VOID
RxWarpInvocation::Run(
    _In_ const RxWarpShader *Shader,
    _In_ const RX_WARP_STAGE_BINDINGS *Bindings)
{
    RxWarpMachine Machine(Shader, this, Bindings);

    Machine.Execute();
}
