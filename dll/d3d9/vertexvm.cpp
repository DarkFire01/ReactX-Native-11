/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shader interpreter of the software vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "vertexvm.hpp"

#include <math.h>
#include <new>

/* Register types the interpreter tells apart */
enum : UINT
{
    D9VmTemp = 0,
    D9VmInput = 1,
    D9VmConst = 2,
    D9VmAddress = 3,
    D9VmRasterizer = 4,
    D9VmColor = 5,
    D9VmOutput = 6,
    D9VmConstInt = 7,
    D9VmSampler = 10,
    D9VmConst2 = 11,
    D9VmConst3 = 12,
    D9VmConst4 = 13,
    D9VmConstBool = 14,
    D9VmLoopCounter = 15,
    D9VmPredicate = 19,
};

/* Opcodes the interpreter runs */
enum : WORD
{
    D9OpNop = 0,
    D9OpMov = 1,
    D9OpAdd = 2,
    D9OpMad = 4,
    D9OpMul = 5,
    D9OpRcp = 6,
    D9OpRsq = 7,
    D9OpDp3 = 8,
    D9OpDp4 = 9,
    D9OpMin = 10,
    D9OpMax = 11,
    D9OpSlt = 12,
    D9OpSge = 13,
    D9OpExp = 14,
    D9OpLog = 15,
    D9OpLit = 16,
    D9OpDst = 17,
    D9OpLrp = 18,
    D9OpFrc = 19,
    D9OpM4x4 = 20,
    D9OpM4x3 = 21,
    D9OpM3x4 = 22,
    D9OpM3x3 = 23,
    D9OpM3x2 = 24,
    D9OpCall = 25,
    D9OpCallNz = 26,
    D9OpLoop = 27,
    D9OpRet = 28,
    D9OpEndLoop = 29,
    D9OpLabel = 30,
    D9OpDcl = 31,
    D9OpPow = 32,
    D9OpCrs = 33,
    D9OpSgn = 34,
    D9OpAbs = 35,
    D9OpNrm = 36,
    D9OpSinCos = 37,
    D9OpRep = 38,
    D9OpEndRep = 39,
    D9OpIf = 40,
    D9OpIfC = 41,
    D9OpElse = 42,
    D9OpEndIf = 43,
    D9OpBreak = 44,
    D9OpBreakC = 45,
    D9OpMova = 46,
    D9OpDefB = 47,
    D9OpDefI = 48,
    D9OpExpP = 78,
    D9OpLogP = 79,
    D9OpDef = 81,
    D9OpSetP = 94,
    D9OpTexLdl = 95,
    D9OpBreakP = 96,
    D9OpComment = 0xFFFE,
    D9OpEnd = 0xFFFF,
};

static constexpr DWORD D9VmModel2 = 0xFFFE0200;
static constexpr DWORD D9VmModel3 = 0xFFFE0300;
static constexpr DWORD D9VmIdentitySwizzle = 0xE4;
static constexpr DWORD D9VmLargest = 0x7F7FFFFF;
static constexpr DWORD D9VmLowest = 0xFF7FFFFF;

/* The system's math routines come from msvcrt, whose results the interpreter has to repeat */
typedef float (__cdecl* D9VmMath1)(float);
typedef float (__cdecl* D9VmMath2)(float, float);

static D9VmMath2 D9VmPow;
static D9VmMath1 D9VmLog;
static D9VmMath1 D9VmSin;
static D9VmMath1 D9VmCos;
static FLOAT D9VmInverseLog2;
static volatile LONG D9VmMathReady;

static
float
__cdecl
D9VmOwnPow(
    _In_ float Base,
    _In_ float Exponent)
{
    return powf(Base, Exponent);
}

static
float
__cdecl
D9VmOwnLog(
    _In_ float Value)
{
    return logf(Value);
}

static
float
__cdecl
D9VmOwnSin(
    _In_ float Value)
{
    return sinf(Value);
}

static
float
__cdecl
D9VmOwnCos(
    _In_ float Value)
{
    return cosf(Value);
}

static
VOID
D9VmLoadMath(VOID)
{
    HMODULE Crt;

    if (ReadAcquire(&D9VmMathReady) != 0)
        return;
    Crt = LoadLibraryW(L"msvcrt.dll");
    D9VmPow = D9VmOwnPow;
    D9VmLog = D9VmOwnLog;
    D9VmSin = D9VmOwnSin;
    D9VmCos = D9VmOwnCos;
    if (Crt != NULL)
    {
        FARPROC Pow = GetProcAddress(Crt, "powf");
        FARPROC Log = GetProcAddress(Crt, "logf");
        FARPROC Sin = GetProcAddress(Crt, "sinf");
        FARPROC Cos = GetProcAddress(Crt, "cosf");

        if (Pow != nullptr && Log != nullptr && Sin != nullptr && Cos != nullptr)
        {
            D9VmPow = reinterpret_cast<D9VmMath2>(Pow);
            D9VmLog = reinterpret_cast<D9VmMath1>(Log);
            D9VmSin = reinterpret_cast<D9VmMath1>(Sin);
            D9VmCos = reinterpret_cast<D9VmMath1>(Cos);
        }
    }
    D9VmInverseLog2 = 1.0f / D9VmLog(2.0f);
    WriteRelease(&D9VmMathReady, 1);
}

/* The absolute value as the system takes it, through a double: a signaling NaN comes back quiet */
static
FLOAT
D9VmAbs(
    _In_ FLOAT Value)
{
    D9VmValue Bits;

    Bits.Float = Value;
    Bits.Bits &= 0x7FFFFFFF;
    if (Bits.Bits > 0x7F800000)
        Bits.Bits |= 0x00400000;
    return Bits.Float;
}

static
UINT
D9VmRegisterType(
    _In_ DWORD Token)
{
    return ((Token >> 28) & 7) | ((Token >> 8) & 0x18);
}

/* Tokens an instruction takes, its own included; zero for one the interpreter does not know */
static
UINT
D9VmLength(
    _In_ DWORD Token,
    _In_ DWORD Version)
{
    WORD Opcode = LOWORD(Token);

    if (Opcode == D9OpComment)
        return ((Token >> 16) & 0x7FFF) + 1;
    if (Version >= D9VmModel2)
        return ((Token >> 24) & 0xF) + 1;

    switch (Opcode)
    {
    case D9OpNop:
    case D9OpEnd:
        return 1;
    case D9OpMov:
    case D9OpRcp:
    case D9OpRsq:
    case D9OpExp:
    case D9OpLog:
    case D9OpLit:
    case D9OpFrc:
    case D9OpDcl:
    case D9OpExpP:
    case D9OpLogP:
        return 3;
    case D9OpAdd:
    case D9OpMul:
    case D9OpDp3:
    case D9OpDp4:
    case D9OpMin:
    case D9OpMax:
    case D9OpSlt:
    case D9OpSge:
    case D9OpDst:
    case D9OpM4x4:
    case D9OpM4x3:
    case D9OpM3x4:
    case D9OpM3x3:
    case D9OpM3x2:
        return 4;
    case D9OpMad:
        return 5;
    case D9OpDef:
        return 6;
    default:
        return 0;
    }
}

D9VmProgram::~D9VmProgram()
{
    delete[] Code;
    delete[] Steps;
}

/* Copies the tokens and links every branch to where it lands; definitions, declarations and comments drop out */
HRESULT
D9VmProgram::Compile(
    _In_reads_(Count) const DWORD* Tokens,
    _In_ UINT Count)
{
    UINT Opens[64];
    UINT OpenCount = 0;
    UINT Loops[64];
    UINT Breaks[64];
    UINT LoopCount = 0;
    UINT Position = 1;

    if (Count < 2)
        return D3DERR_INVALIDCALL;
    Code = new (std::nothrow) DWORD[Count];
    Steps = new (std::nothrow) D9VmStep[Count];
    if (Code == nullptr || Steps == nullptr)
        return E_OUTOFMEMORY;
    CopyMemory(Code, Tokens, Count * sizeof(*Code));
    Version = Tokens[0];

    while (Position < Count)
    {
        DWORD Token = Code[Position];
        WORD Opcode = LOWORD(Token);
        UINT Length = D9VmLength(Token, Version);
        D9VmStep* Step;

        if (Length == 0 || Position + Length > Count)
            return D3DERR_INVALIDCALL;
        if (Opcode == D9OpLabel)
        {
            UINT Label = Code[Position + 1] & 0x7FF;

            if (Label < D9VmLabelCount)
                Labels[Label] = StepCount;
            Position += Length;
            continue;
        }
        if (Opcode == D9OpDef || Opcode == D9OpDefI || Opcode == D9OpDefB || Opcode == D9OpDcl ||
            Opcode == D9OpComment)
        {
            Position += Length;
            continue;
        }

        Step = &Steps[StepCount];
        Step->Opcode = Token;
        Step->Operands = Position + 1;
        Step->Target = 0;

        switch (Opcode)
        {
        case D9OpIf:
        case D9OpIfC:
            if (OpenCount == _countof(Opens))
                return D3DERR_INVALIDCALL;
            Opens[OpenCount++] = StepCount;
            break;
        case D9OpElse:
            if (OpenCount == 0)
                return D3DERR_INVALIDCALL;
            Steps[Opens[OpenCount - 1]].Target = StepCount + 1;
            Opens[OpenCount - 1] = StepCount;
            break;
        case D9OpEndIf:
            if (OpenCount == 0)
                return D3DERR_INVALIDCALL;
            OpenCount -= 1;
            Steps[Opens[OpenCount]].Target = StepCount;
            break;
        case D9OpLoop:
        case D9OpRep:
            if (LoopCount == _countof(Loops))
                return D3DERR_INVALIDCALL;
            Loops[LoopCount] = StepCount;
            Breaks[LoopCount] = 0;
            LoopCount += 1;
            break;
        case D9OpBreak:
        case D9OpBreakC:
        case D9OpBreakP:
            /* Breaks of one loop chain through their targets until its end is known */
            if (LoopCount == 0)
                return D3DERR_INVALIDCALL;
            Step->Target = Breaks[LoopCount - 1];
            Breaks[LoopCount - 1] = StepCount + 1;
            break;
        case D9OpEndLoop:
        case D9OpEndRep:
        {
            UINT Chain;

            if (LoopCount == 0)
                return D3DERR_INVALIDCALL;
            LoopCount -= 1;
            Steps[Loops[LoopCount]].Target = StepCount + 1;
            Chain = Breaks[LoopCount];
            while (Chain != 0)
            {
                D9VmStep* Waiting = &Steps[Chain - 1];

                Chain = Waiting->Target;
                Waiting->Target = StepCount + 1;
            }
            break;
        }
        default:
            break;
        }

        StepCount += 1;
        Position += Length;
        if (Opcode == D9OpEnd)
            return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

D9VertexMachine::~D9VertexMachine()
{
    delete[] FloatConstants;
    delete[] IntConstants;
    delete[] BoolConstants;
}

HRESULT
D9VertexMachine::Initialize(
    _In_ UINT Floats,
    _In_ UINT Ints,
    _In_ UINT Bools)
{
    D9VmLoadMath();
    FloatConstants = new (std::nothrow) D9VmVector[Floats != 0 ? Floats : 1]();
    IntConstants = new (std::nothrow) D9VmVector[Ints != 0 ? Ints : 1]();
    BoolConstants = new (std::nothrow) D9VmValue[Bools != 0 ? Bools : 1]();
    if (FloatConstants == nullptr || IntConstants == nullptr || BoolConstants == nullptr)
        return E_OUTOFMEMORY;
    FloatCount = Floats;
    IntCount = Ints;
    BoolCount = Bools;
    for (UINT Index = 0; Index < D9VmSamplerCount; Index++)
        Registers.Samplers[Index][0].Bits = Index;
    return S_OK;
}

/* A register of a type; a number past the end of its file reads and writes the first one. Following is how many
   registers of the file start there, which a matrix reads its rows from */
D9VmValue*
D9VertexMachine::Locate(
    _In_ UINT Type,
    _In_ UINT Number,
    _Out_opt_ UINT* Following)
{
    D9VmValue* File;
    UINT Size;

    switch (Type)
    {
    case D9VmTemp:
        File = Registers.Temps[0];
        Size = D9VmTempCount;
        break;
    case D9VmInput:
        File = Registers.Inputs[0];
        Size = D9VmInputCount;
        break;
    case D9VmConst:
        File = FloatConstants[0];
        Size = FloatCount;
        break;
    case D9VmAddress:
        File = Registers.Address;
        Size = 1;
        Number = 0;
        break;
    case D9VmRasterizer:
        File = Registers.Position;
        Size = 3;
        break;
    case D9VmColor:
        File = Registers.Colors[0];
        Size = 2;
        break;
    case D9VmOutput:
        File = Registers.Outputs[0];
        Size = D9VmOutputCount;
        break;
    case D9VmConstInt:
        File = IntConstants[0];
        Size = IntCount;
        break;
    case D9VmSampler:
        File = Registers.Samplers[0];
        Size = D9VmSamplerCount;
        break;
    case D9VmConstBool:
        if (Following != nullptr)
            *Following = 1;
        return &BoolConstants[Number < BoolCount ? Number : 0];
    case D9VmPredicate:
        File = Registers.Predicate;
        Size = 1;
        Number = 0;
        break;
    default:
        return nullptr;
    }
    if (Number >= Size)
        Number = 0;
    if (Following != nullptr)
        *Following = Size - Number;
    return File + Number * 4;
}

/* Reads a source into a slot, or a run of rows into consecutive slots: swizzled, then negated, made absolute or,
   for booleans and predicates, inverted */
BOOLEAN
D9VertexMachine::ReadSource(
    _Inout_ UINT* Cursor,
    _In_ UINT Slot,
    _In_ UINT Rows)
{
    DWORD Token = Code[*Cursor];
    UINT Type = D9VmRegisterType(Token);
    UINT Number = Token & 0x7FF;
    DWORD Swizzle = (Token >> 16) & 0xFF;
    DWORD Modifier = (Token >> 24) & 0xF;
    BOOLEAN Relative = (Token & D3DSHADER_ADDRESSMODE_MASK) != 0;
    INT Offset = 0;
    UINT Following;
    D9VmValue* First;

    *Cursor += 1;
    if (Type == D9VmConst2 || Type == D9VmConst3 || Type == D9VmConst4)
    {
        Number |= (Type - D9VmConst2 + 1) << 11;
        Type = D9VmConst;
    }
    if (Relative)
    {
        if (Current->Version < D9VmModel2)
        {
            Offset = Registers.Address[0].Int;
        }
        else
        {
            DWORD Index = Code[*Cursor];

            *Cursor += 1;
            if (D9VmRegisterType(Index) == D9VmLoopCounter)
                Offset = Loops[LoopLevel].Counter;
            else
                Offset = Registers.Address[(Index >> 16) & 3].Int;
        }
    }

    /* A matrix reads its rows as they are from the registers after the first one; past the end of the file the
       last one repeats, where the system reads on past it */
    First = Locate(Type, static_cast<UINT>(static_cast<INT>(Number) + Offset), &Following);
    if (First == nullptr)
        return FALSE;
    if (Rows > 1)
    {
        for (UINT Row = 0; Row < Rows; Row++)
        {
            D9VmValue* Register = First + (Row < Following ? Row : Following - 1) * 4;

            for (UINT Lane = 0; Lane < 4; Lane++)
                Sources[Slot + Row][Lane] = Register[Lane];
        }
        return TRUE;
    }

    {
        D9VmValue* Register = First;
        D9VmValue* Into = Sources[Slot];

        if (Type == D9VmConstBool && !Relative)
        {
            Into[0] = Register[0];
        }
        else if (Swizzle == D9VmIdentitySwizzle)
        {
            for (UINT Lane = 0; Lane < 4; Lane++)
                Into[Lane] = Register[Lane];
        }
        else
        {
            for (UINT Lane = 0; Lane < 4; Lane++)
                Into[Lane] = Register[(Swizzle >> (Lane * 2)) & 3];
        }

        switch (Modifier)
        {
        case D3DSPSM_NEG >> D3DSP_SRCMOD_SHIFT:
            for (UINT Lane = 0; Lane < 4; Lane++)
                Into[Lane].Float = -Into[Lane].Float;
            break;
        case D3DSPSM_ABS >> D3DSP_SRCMOD_SHIFT:
            for (UINT Lane = 0; Lane < 4; Lane++)
                Into[Lane].Float = D9VmAbs(Into[Lane].Float);
            break;
        case D3DSPSM_ABSNEG >> D3DSP_SRCMOD_SHIFT:
            for (UINT Lane = 0; Lane < 4; Lane++)
                Into[Lane].Float = -D9VmAbs(Into[Lane].Float);
            break;
        case D3DSPSM_NOT >> D3DSP_SRCMOD_SHIFT:
            if (Type == D9VmConstBool || Type == D9VmPredicate)
                Into[0].Bits = Into[0].Bits == 0 ? 1 : 0;
            break;
        default:
            break;
        }
    }
    return TRUE;
}

/* Finds the register an instruction writes, the lanes it writes after its predicate and whether it saturates */
BOOLEAN
D9VertexMachine::ReadDestination(
    _Inout_ UINT* Cursor)
{
    DWORD Token = Code[*Cursor];
    UINT Number = Token & 0x7FF;

    *Cursor += 1;
    TargetMask = (Token >> 16) & 0xF;
    TargetSaturates = (Token & D3DSPDM_SATURATE) != 0;
    if ((Token & D3DSHADER_ADDRESSMODE_MASK) != 0)
    {
        DWORD Index = Code[*Cursor];

        *Cursor += 1;
        if (D9VmRegisterType(Index) == D9VmLoopCounter)
            Number += Loops[LoopLevel].Counter;
    }
    Target = Locate(D9VmRegisterType(Token), Number, nullptr);
    if (Target == nullptr)
        return FALSE;

    if (Predicated)
    {
        DWORD Guard = Code[*Cursor];
        DWORD Swizzle = (Guard >> 16) & 0xFF;
        UINT Lanes = 0;

        *Cursor += 1;
        for (UINT Lane = 0; Lane < 4; Lane++)
        {
            UINT From = Swizzle == D9VmIdentitySwizzle ? Lane : (Swizzle >> (Lane * 2)) & 3;

            if (Registers.Predicate[From].Bits != 0)
                Lanes |= 1u << Lane;
        }
        if (((Guard >> 24) & 0xF) == (D3DSPSM_NOT >> D3DSP_SRCMOD_SHIFT))
            TargetMask &= ~Lanes;
        else
            TargetMask &= Lanes;
    }
    return TRUE;
}

static
FLOAT
D9VmSaturate(
    _In_ FLOAT Value)
{
    if (Value <= 0.0f)
        return 0.0f;
    if (Value > 1.0f)
        return 1.0f;
    return Value;
}

/* Writes the lanes the destination keeps, then clamps them when it saturates */
VOID
D9VertexMachine::Store(
    _In_reads_(4) const D9VmValue* Result,
    _In_ BOOLEAN Saturate)
{
    for (UINT Lane = 0; Lane < 4; Lane++)
    {
        if ((TargetMask & (1u << Lane)) != 0)
            Target[Lane] = Result[Lane];
    }
    if (!Saturate || !TargetSaturates)
        return;
    for (UINT Lane = 0; Lane < 4; Lane++)
    {
        if ((TargetMask & (1u << Lane)) != 0)
            Target[Lane].Float = D9VmSaturate(Target[Lane].Float);
    }
}

/* A loop with iterations to run takes the next level; past the deepest it takes the deepest one again */
VOID
D9VertexMachine::EnterLoop(
    _In_ INT Iterations,
    _In_ INT Counter,
    _In_ INT Step,
    _In_ UINT Start)
{
    Loop* Entered;

    if (Iterations <= 0)
        return;
    if (LoopLevel < D9VmLoopDepth)
        LoopLevel += 1;
    Entered = &Loops[LoopLevel];
    Entered->Remaining = Iterations;
    Entered->Counter = Counter;
    Entered->Step = Step;
    Entered->Start = Start;
}

/* Steps the innermost loop; TRUE when it runs again, otherwise it ends */
BOOLEAN
D9VertexMachine::LeaveLoop(
    _In_ BOOLEAN Breaking)
{
    Loop* Inner;

    if (LoopLevel == 0)
        return FALSE;
    Inner = &Loops[LoopLevel];
    Inner->Counter += Inner->Step;
    Inner->Remaining -= 1;
    if (Inner->Remaining > 0 && !Breaking)
        return TRUE;
    Inner->Remaining = 0;
    LoopLevel -= 1;
    return FALSE;
}

static
BOOLEAN
D9VmCompare(
    _In_ FLOAT Left,
    _In_ FLOAT Right,
    _In_ UINT Test)
{
    switch (Test)
    {
    case 1:
        return Left > Right;
    case 2:
        return Left == Right;
    case 3:
        return Left >= Right;
    case 4:
        return Left < Right;
    case 5:
        return Left != Right;
    case 6:
        return Left <= Right;
    default:
        return FALSE;
    }
}

/* Two operand float math with the NaN the system's processor returns: a signaling one first, else the first
   NaN operand, quieted; the compiler is free to swap operands, which would change which NaN comes back */
static
BOOLEAN
D9VmPickNaN(
    _In_ FLOAT First,
    _In_ FLOAT Second,
    _Out_ FLOAT* Picked)
{
    D9VmValue A;
    D9VmValue B;

    A.Float = First;
    B.Float = Second;
    if ((A.Bits & 0x7FFFFFFF) <= 0x7F800000 && (B.Bits & 0x7FFFFFFF) <= 0x7F800000)
        return FALSE;
#if defined(_M_ARM64)
    if ((A.Bits & 0x7FFFFFFF) > 0x7F800000 && (A.Bits & 0x00400000) == 0)
        A.Bits |= 0x00400000;
    else if ((B.Bits & 0x7FFFFFFF) > 0x7F800000 && (B.Bits & 0x00400000) == 0)
        A.Bits = B.Bits | 0x00400000;
    else if ((A.Bits & 0x7FFFFFFF) <= 0x7F800000)
        A.Bits = B.Bits;
#else
    if ((A.Bits & 0x7FFFFFFF) <= 0x7F800000)
        A.Bits = B.Bits;
    A.Bits |= 0x00400000;
#endif
    *Picked = A.Float;
    return TRUE;
}

static
FLOAT
D9VmAdd(
    _In_ FLOAT First,
    _In_ FLOAT Second)
{
    FLOAT Picked;

    if (D9VmPickNaN(First, Second, &Picked))
        return Picked;
    return First + Second;
}

static
FLOAT
D9VmSub(
    _In_ FLOAT First,
    _In_ FLOAT Second)
{
    FLOAT Picked;

    if (D9VmPickNaN(First, Second, &Picked))
        return Picked;
    return First - Second;
}

static
FLOAT
D9VmMul(
    _In_ FLOAT First,
    _In_ FLOAT Second)
{
    FLOAT Picked;

    if (D9VmPickNaN(First, Second, &Picked))
        return Picked;
    return First * Second;
}

/* The dot product of the first lanes of two slots, summed the way the system sums them */
static
FLOAT
D9VmDot(
    _In_reads_(4) const D9VmValue* Left,
    _In_reads_(4) const D9VmValue* Right,
    _In_ UINT Lanes)
{
    FLOAT Sum = D9VmAdd(D9VmMul(Right[1].Float, Left[1].Float), D9VmMul(Right[0].Float, Left[0].Float));

    Sum = D9VmAdd(Sum, D9VmMul(Right[2].Float, Left[2].Float));
    if (Lanes == 4)
        Sum = D9VmAdd(Sum, D9VmMul(Right[3].Float, Left[3].Float));
    return Sum;
}

/* Rounds a float for the address register: half away from zero */
static
INT
D9VmRound(
    _In_ FLOAT Value)
{
    return static_cast<INT>(Value >= 0.0f ? Value + 0.5f : Value - 0.5f);
}

/* Instructions that read sources and write a register; FALSE when one names a register type it cannot use */
BOOLEAN
D9VertexMachine::Arithmetic(
    _In_ DWORD Opcode,
    _In_ UINT Cursor)
{
    D9VmValue Result[4] = {};
    D9VmValue* S0 = Sources[0];
    D9VmValue* S1 = Sources[1];
    D9VmValue* S2 = Sources[2];
    WORD Code16 = LOWORD(Opcode);
    UINT Sourced = 1;
    UINT Rows = 1;
    BOOLEAN Saturate = TRUE;

    switch (Code16)
    {
    case D9OpAdd:
    case D9OpMul:
    case D9OpDp3:
    case D9OpDp4:
    case D9OpMin:
    case D9OpMax:
    case D9OpSlt:
    case D9OpSge:
    case D9OpDst:
    case D9OpPow:
    case D9OpCrs:
    case D9OpSetP:
    case D9OpTexLdl:
        Sourced = 2;
        break;
    case D9OpMad:
    case D9OpLrp:
    case D9OpSgn:
        Sourced = 3;
        break;
    case D9OpM4x4:
    case D9OpM3x4:
        Sourced = 2;
        Rows = 4;
        break;
    case D9OpM4x3:
    case D9OpM3x3:
        Sourced = 2;
        Rows = 3;
        break;
    case D9OpM3x2:
        Sourced = 2;
        Rows = 2;
        break;
    case D9OpSinCos:
        Sourced = Current->Version < D9VmModel3 ? 3 : 1;
        break;
    default:
        break;
    }

    if (!ReadDestination(&Cursor))
        return FALSE;
    for (UINT Index = 0; Index < Sourced; Index++)
    {
        if (!ReadSource(&Cursor, Index, Index == 1 ? Rows : 1))
            return FALSE;
    }

    switch (Code16)
    {
    case D9OpMov:
        if (Target == Registers.Address)
        {
            Target[0].Int = D9VmRound(floorf(S0[0].Float));
            return TRUE;
        }
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane] = S0[Lane];
        break;
    case D9OpMova:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Int = D9VmRound(S0[Lane].Float);
        Saturate = FALSE;
        break;
    case D9OpAdd:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmAdd(S1[Lane].Float, S0[Lane].Float);
        break;
    case D9OpMul:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmMul(S1[Lane].Float, S0[Lane].Float);
        break;
    case D9OpMad:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmAdd(D9VmMul(S1[Lane].Float, S0[Lane].Float), S2[Lane].Float);
        break;
    case D9OpLrp:
        for (UINT Lane = 0; Lane < 4; Lane++)
        {
            FLOAT Span = D9VmSub(S1[Lane].Float, S2[Lane].Float);

            Result[Lane].Float = D9VmAdd(D9VmMul(Span, S0[Lane].Float), S2[Lane].Float);
        }
        break;
    case D9OpDp3:
    case D9OpDp4:
    {
        FLOAT Sum = D9VmDot(S0, S1, Code16 == D9OpDp4 ? 4 : 3);

        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = Sum;
        break;
    }
    case D9OpM4x4:
    case D9OpM4x3:
    case D9OpM3x4:
    case D9OpM3x3:
    case D9OpM3x2:
    {
        UINT Lanes = Code16 == D9OpM4x4 || Code16 == D9OpM4x3 ? 4 : 3;

        /* Lanes past the rows keep what the register held */
        for (UINT Row = 0; Row < 4; Row++)
        {
            if (Row < Rows)
                Result[Row].Float = D9VmDot(S0, Sources[1 + Row], Lanes);
            else
                Result[Row] = Target[Row];
        }
        break;
    }
    case D9OpMin:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane] = S0[Lane].Float < S1[Lane].Float ? S0[Lane] : S1[Lane];
        break;
    case D9OpMax:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane] = S0[Lane].Float > S1[Lane].Float ? S0[Lane] : S1[Lane];
        break;
    case D9OpSlt:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = S0[Lane].Float < S1[Lane].Float ? 1.0f : 0.0f;
        break;
    case D9OpSge:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = S0[Lane].Float >= S1[Lane].Float ? 1.0f : 0.0f;
        break;
    case D9OpSgn:
        for (UINT Lane = 0; Lane < 4; Lane++)
        {
            FLOAT Value = S0[Lane].Float;

            if (Value < 0.0f)
                Result[Lane].Float = -1.0f;
            else
                Result[Lane].Float = Value == 0.0f ? 0.0f : 1.0f;
        }
        Saturate = FALSE;
        break;
    case D9OpAbs:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmAbs(S0[Lane].Float);
        break;
    case D9OpFrc:
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmSub(S0[Lane].Float, floorf(S0[Lane].Float));
        break;
    case D9OpCrs:
        Result[0].Float = D9VmSub(D9VmMul(S1[2].Float, S0[1].Float), D9VmMul(S1[1].Float, S0[2].Float));
        Result[1].Float = D9VmSub(D9VmMul(S1[0].Float, S0[2].Float), D9VmMul(S1[2].Float, S0[0].Float));
        Result[2].Float = D9VmSub(D9VmMul(S1[1].Float, S0[0].Float), D9VmMul(S1[0].Float, S0[1].Float));
        Result[3] = Target[3];
        break;
    case D9OpRcp:
    case D9OpRsq:
    {
        FLOAT Value = Code16 == D9OpRsq ? D9VmAbs(S0[3].Float) : S0[3].Float;
        D9VmValue Out;

        if (Value == 0.0f)
            Out.Bits = D9VmLargest;
        else if (Value == 1.0f)
            Out.Float = 1.0f;
        else
            Out.Float = Code16 == D9OpRsq ? 1.0f / sqrtf(Value) : 1.0f / Value;
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane] = Out;
        break;
    }
    case D9OpNrm:
    {
        FLOAT Length = D9VmAdd(D9VmMul(S0[1].Float, S0[1].Float), D9VmMul(S0[0].Float, S0[0].Float));
        FLOAT Scale;

        Length = D9VmAdd(Length, D9VmMul(S0[2].Float, S0[2].Float));
        if (Length == 0.0f)
        {
            D9VmValue Largest;

            Largest.Bits = D9VmLargest;
            Scale = Largest.Float;
        }
        else
        {
            Scale = Length == 1.0f ? 1.0f : 1.0f / sqrtf(Length);
        }
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = D9VmMul(S0[Lane].Float, Scale);
        break;
    }
    case D9OpExp:
    {
        FLOAT Power = D9VmPow(2.0f, S0[3].Float);

        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = Power;
        break;
    }
    case D9OpExpP:
    {
        FLOAT Value = S0[3].Float;
        D9VmValue Power;

        Power.Float = D9VmPow(2.0f, Value);
        Power.Bits &= 0xFFFFFF00;
        if (Current->Version >= D9VmModel2)
        {
            for (UINT Lane = 0; Lane < 4; Lane++)
                Result[Lane] = Power;
            break;
        }
        Result[0].Float = D9VmPow(2.0f, floorf(Value));
        Result[1].Float = Value - floorf(Value);
        Result[2] = Power;
        Result[3].Float = 1.0f;
        break;
    }
    case D9OpLog:
    {
        FLOAT Value = D9VmAbs(S0[3].Float);
        D9VmValue Out;

        if (Value == 0.0f)
            Out.Bits = D9VmLowest;
        else
            Out.Float = D9VmLog(Value) * D9VmInverseLog2;
        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane] = Out;
        break;
    }
    case D9OpLogP:
    {
        D9VmValue Value;
        D9VmValue Out;

        Value.Float = D9VmAbs(S0[3].Float);
        if (Value.Float == 0.0f)
            Out.Bits = D9VmLowest;
        else
            Out.Float = D9VmLog(Value.Float) * D9VmInverseLog2;
        if (Value.Float != 0.0f)
            Out.Bits &= 0xFFFFFF00;
        if (Current->Version >= D9VmModel2)
        {
            for (UINT Lane = 0; Lane < 4; Lane++)
                Result[Lane] = Out;
            break;
        }
        if (Value.Float == 0.0f)
        {
            Result[0].Bits = D9VmLowest;
            Result[1].Float = 1.0f;
            Result[2].Bits = D9VmLowest;
        }
        else
        {
            Result[0].Float = static_cast<FLOAT>(static_cast<INT>(Value.Bits >> 23) - 127);
            Result[1].Bits = (Value.Bits & 0x7FFFFF) | 0x3F800000;
            Result[2] = Out;
        }
        Result[3].Float = 1.0f;
        break;
    }
    case D9OpLit:
    {
        D9VmValue Limit;
        FLOAT Power = S0[3].Float;

        /* The power stays within 127.9961 either way */
        Limit.Bits = 0x42FFFE01;
        if (Power < -Limit.Float)
            Power = -Limit.Float;
        else if (Limit.Float < Power)
            Power = Limit.Float;
        Result[0].Float = 1.0f;
        Result[3].Float = 1.0f;
        if (S0[0].Float > 0.0f)
        {
            Result[1] = S0[0];
            if (S0[1].Float > 0.0f)
                Result[2].Float = D9VmPow(S0[1].Float, Power);
        }
        break;
    }
    case D9OpDst:
        Result[0].Float = 1.0f;
        Result[1].Float = D9VmMul(S1[1].Float, S0[1].Float);
        Result[2] = S0[2];
        Result[3] = S1[3];
        break;
    case D9OpPow:
    {
        FLOAT Power = D9VmPow(D9VmAbs(S0[3].Float), S1[3].Float);

        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Float = Power;
        break;
    }
    case D9OpSinCos:
        Result[0].Float = D9VmCos(S0[3].Float);
        Result[1].Float = D9VmSin(S0[3].Float);
        Result[2] = Target[2];
        Result[3] = Target[3];
        break;
    case D9OpSetP:
    {
        UINT Test = (Opcode >> 16) & 7;

        for (UINT Lane = 0; Lane < 4; Lane++)
            Result[Lane].Bits = D9VmCompare(S0[Lane].Float, S1[Lane].Float, Test) ? 1 : 0;
        Saturate = FALSE;
        break;
    }
    case D9OpTexLdl:
    {
        FLOAT Lod = S0[3].Float;

        S0[3].Float = 1.0f;
        if (Sample == nullptr)
            return FALSE;
        Sample(SampleContext, S1[0].Bits, &S0[0].Float, Lod, Result);
        Saturate = FALSE;
        break;
    }
    default:
        return FALSE;
    }

    Store(Result, Saturate);
    return TRUE;
}

/* Runs the program once over the registers; the position output starts at (0, 0, 0, 1) */
HRESULT
D9VertexMachine::Run(
    _In_ const D9VmProgram* Program)
{
    UINT Returns[D9VmCallDepth];
    UINT CallLevel = 0;
    UINT Next = 0;

    if (Program == nullptr || Program->Steps == nullptr)
        return D3DERR_INVALIDCALL;
    Current = Program;
    Code = Program->Code;
    LoopLevel = 0;
    Registers.Position[0].Float = 0.0f;
    Registers.Position[1].Float = 0.0f;
    Registers.Position[2].Float = 0.0f;
    Registers.Position[3].Float = 1.0f;
    Returns[CallLevel++] = Program->StepCount - 1;

    while (Next < Program->StepCount)
    {
        const D9VmStep* Step = &Program->Steps[Next];
        WORD Opcode = LOWORD(Step->Opcode);
        UINT Cursor = Step->Operands;

        Next += 1;
        Predicated = (Step->Opcode & D3DSHADER_INSTRUCTION_PREDICATED) != 0;
        switch (Opcode)
        {
        case D9OpNop:
        case D9OpEndIf:
            break;
        case D9OpEnd:
            return S_OK;
        case D9OpCall:
        case D9OpCallNz:
        {
            UINT Label = Code[Cursor] & 0x7FF;

            Cursor += 1;
            if (Opcode == D9OpCallNz)
            {
                if (!ReadSource(&Cursor, 0, 1))
                    return D3DERR_INVALIDCALL;
                if (Sources[0][0].Bits == 0)
                    break;
            }
            if (CallLevel < D9VmCallDepth)
                Returns[CallLevel++] = Next;
            Next = Label < D9VmLabelCount ? Program->Labels[Label] : Program->StepCount - 1;
            break;
        }
        case D9OpRet:
            if (CallLevel == 0)
                return S_OK;
            CallLevel -= 1;
            Next = Returns[CallLevel];
            break;
        case D9OpLoop:
        case D9OpRep:
        {
            UINT Bank = Code[Cursor + (Opcode == D9OpLoop ? 1 : 0)] & 0x7FF;
            const D9VmValue* Count = IntConstants[Bank < IntCount ? Bank : 0];

            if (Opcode == D9OpLoop)
                EnterLoop(Count[0].Int, Count[1].Int, Count[2].Int, Next);
            else
                EnterLoop(Count[0].Int, Loops[LoopLevel].Counter, 0, Next);
            if (Count[0].Int <= 0)
                Next = Step->Target;
            break;
        }
        case D9OpEndLoop:
        case D9OpEndRep:
            if (LeaveLoop(FALSE))
                Next = Loops[LoopLevel].Start;
            break;
        case D9OpBreak:
            LeaveLoop(TRUE);
            Next = Step->Target;
            break;
        case D9OpBreakC:
        case D9OpIfC:
        {
            UINT Test = (Step->Opcode >> 16) & 7;
            BOOLEAN Holds;

            if (!ReadSource(&Cursor, 0, 1) || !ReadSource(&Cursor, 1, 1))
                return D3DERR_INVALIDCALL;
            Holds = D9VmCompare(Sources[0][0].Float, Sources[1][0].Float, Test);
            if (Opcode == D9OpIfC)
            {
                if (!Holds)
                    Next = Step->Target;
            }
            else if (Holds)
            {
                LeaveLoop(TRUE);
                Next = Step->Target;
            }
            break;
        }
        case D9OpBreakP:
        case D9OpIf:
            if (!ReadSource(&Cursor, 0, 1))
                return D3DERR_INVALIDCALL;
            if (Opcode == D9OpIf)
            {
                if (Sources[0][0].Bits == 0)
                    Next = Step->Target;
            }
            else if (Sources[0][0].Bits != 0)
            {
                LeaveLoop(TRUE);
                Next = Step->Target;
            }
            break;
        case D9OpElse:
            Next = Step->Target;
            break;
        default:
            if (!Arithmetic(Step->Opcode, Cursor))
                return D3DERR_INVALIDCALL;
            break;
        }
    }
    return S_OK;
}
