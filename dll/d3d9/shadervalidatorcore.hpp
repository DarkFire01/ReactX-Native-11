/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader validator internals shared by the vertex and pixel shader models
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "shadervalidator.hpp"

/* Opcodes the rules single out */
static constexpr UINT D9SvOpNop = 0;
static constexpr UINT D9SvOpMov = 1;
static constexpr UINT D9SvOpAdd = 2;
static constexpr UINT D9SvOpMad = 4;
static constexpr UINT D9SvOpMul = 5;
static constexpr UINT D9SvOpRcp = 6;
static constexpr UINT D9SvOpRsq = 7;
static constexpr UINT D9SvOpDp3 = 8;
static constexpr UINT D9SvOpDp4 = 9;
static constexpr UINT D9SvOpExp = 14;
static constexpr UINT D9SvOpLog = 15;
static constexpr UINT D9SvOpLit = 16;
static constexpr UINT D9SvOpDst = 17;
static constexpr UINT D9SvOpLrp = 18;
static constexpr UINT D9SvOpFrc = 19;
static constexpr UINT D9SvOpM4x4 = 20;
static constexpr UINT D9SvOpM4x3 = 21;
static constexpr UINT D9SvOpM3x4 = 22;
static constexpr UINT D9SvOpM3x3 = 23;
static constexpr UINT D9SvOpM3x2 = 24;
static constexpr UINT D9SvOpCall = 25;
static constexpr UINT D9SvOpCallnz = 26;
static constexpr UINT D9SvOpLoop = 27;
static constexpr UINT D9SvOpRet = 28;
static constexpr UINT D9SvOpEndloop = 29;
static constexpr UINT D9SvOpLabel = 30;
static constexpr UINT D9SvOpDcl = 31;
static constexpr UINT D9SvOpPow = 32;
static constexpr UINT D9SvOpCrs = 33;
static constexpr UINT D9SvOpSgn = 34;
static constexpr UINT D9SvOpAbs = 35;
static constexpr UINT D9SvOpNrm = 36;
static constexpr UINT D9SvOpSincos = 37;
static constexpr UINT D9SvOpRep = 38;
static constexpr UINT D9SvOpEndrep = 39;
static constexpr UINT D9SvOpIf = 40;
static constexpr UINT D9SvOpIfc = 41;
static constexpr UINT D9SvOpElse = 42;
static constexpr UINT D9SvOpEndif = 43;
static constexpr UINT D9SvOpBreak = 44;
static constexpr UINT D9SvOpBreakc = 45;
static constexpr UINT D9SvOpMova = 46;
static constexpr UINT D9SvOpDefb = 47;
static constexpr UINT D9SvOpDefi = 48;
static constexpr UINT D9SvOpTexcoord = 64;
static constexpr UINT D9SvOpTexkill = 65;
static constexpr UINT D9SvOpTex = 66;
static constexpr UINT D9SvOpExpp = 78;
static constexpr UINT D9SvOpLogp = 79;
static constexpr UINT D9SvOpCnd = 80;
static constexpr UINT D9SvOpDef = 81;
static constexpr UINT D9SvOpSetp = 94;
static constexpr UINT D9SvOpTexldl = 95;
static constexpr UINT D9SvOpBreakp = 96;
static constexpr UINT D9SvOpPhase = 0xFFFD;
static constexpr UINT D9SvOpComment = 0xFFFE;
static constexpr UINT D9SvOpEnd = 0xFFFF;

/* Register types */
static constexpr UINT D9SvRegTemp = 0;
static constexpr UINT D9SvRegInput = 1;
static constexpr UINT D9SvRegConst = 2;
static constexpr UINT D9SvRegAddress = 3;
static constexpr UINT D9SvRegRastOut = 4;
static constexpr UINT D9SvRegAttrOut = 5;
static constexpr UINT D9SvRegOutput = 6;
static constexpr UINT D9SvRegConstInt = 7;
static constexpr UINT D9SvRegColorOut = 8;
static constexpr UINT D9SvRegDepthOut = 9;
static constexpr UINT D9SvRegSampler = 10;
static constexpr UINT D9SvRegConstBool = 14;
static constexpr UINT D9SvRegLoop = 15;
static constexpr UINT D9SvRegMisc = 17;
static constexpr UINT D9SvRegLabel = 18;
static constexpr UINT D9SvRegPredicate = 19;

/* Components as write mask bits, and the swizzles that pick one of them */
static constexpr DWORD D9SvComponentX = 0x10000;
static constexpr DWORD D9SvComponentY = 0x20000;
static constexpr DWORD D9SvComponentZ = 0x40000;
static constexpr DWORD D9SvComponentW = 0x80000;
static constexpr DWORD D9SvMaskXyz = 0x70000;
static constexpr DWORD D9SvMaskAll = 0xF0000;
static constexpr DWORD D9SvSwizzleIdentity = 0xE40000;
static constexpr DWORD D9SvSwizzleX = 0x000000;
static constexpr DWORD D9SvSwizzleY = 0x550000;
static constexpr DWORD D9SvSwizzleZ = 0xAA0000;
static constexpr DWORD D9SvSwizzleW = 0xFF0000;
static constexpr DWORD D9SvRelativeBit = 0x2000;

/* Source modifiers */
static constexpr DWORD D9SvModNegate = 0x1000000;
static constexpr DWORD D9SvModAbs = 0xB000000;
static constexpr DWORD D9SvModAbsNegate = 0xC000000;
static constexpr DWORD D9SvModNot = 0xD000000;

template <class T>
static
inline
T
D9SvMax(
    _In_ T Left,
    _In_ T Right)
{
    return Left > Right ? Left : Right;
}

template <class T>
static
inline
T
D9SvMin(
    _In_ T Left,
    _In_ T Right)
{
    return Left < Right ? Left : Right;
}

static
inline
DWORD
D9SvComponent(
    _In_ UINT Index)
{
    return 0x10000u << Index;
}

/* The constant register files past the first fold into one numbering */
static
inline
UINT
D9SvRawType(
    _In_ DWORD Token)
{
    return ((Token >> 28) & 7) | ((Token >> 8) & 0x18);
}

static
inline
UINT
D9SvRegisterType(
    _In_ DWORD Token)
{
    UINT Type = D9SvRawType(Token);

    return (Type >= 11 && Type <= 13) ? D9SvRegConst : Type;
}

static
inline
UINT
D9SvRegisterNumber(
    _In_ DWORD Token)
{
    UINT Type = D9SvRawType(Token);
    UINT Number = Token & 0x7FF;

    if (Type >= 11 && Type <= 13)
        Number |= (Type - 10) << 11;
    return Number;
}

PCSTR
D9SvOpcodeName(
    _In_ UINT Opcode);

PCSTR
D9SvUsageName(
    _In_ UINT Usage);

VOID
D9SvVersionName(
    _In_ DWORD Version,
    _Out_writes_(32) PSTR Name);

BOOLEAN
D9SvReplicate(
    _In_ DWORD Swizzle);

BOOLEAN
D9SvIsMatrix(
    _In_ UINT Opcode);

UINT
D9SvMatrixRows(
    _In_ UINT Opcode);

DWORD
D9SvSwizzled(
    _In_ DWORD Swizzle,
    _In_ DWORD Positions);

/* One register file: its size, whether instructions may write it, how many it reads at once, and a mark per
   register and component */
struct D9SvRegisters
{
    UINT Count = 0;
    BOOLEAN Writable = FALSE;
    UINT Ports = 0;
    BYTE* Marks[4] = {};
    BYTE* Storage = nullptr;

    BOOLEAN
    Create(
        _In_ UINT Registers,
        _In_ BOOLEAN CanWrite,
        _In_ UINT ReadPorts);

    DWORD
    Written(
        _In_ UINT Number) const;

    VOID
    Mark(
        _In_ UINT Number,
        _In_ DWORD Mask);

    ~D9SvRegisters();
};

struct D9SvSource
{
    BOOLEAN Present;
    UINT Number;
    DWORD Swizzle;
    DWORD Modifier;
    UINT Type;
    DWORD Read;
    DWORD Relative;
    DWORD RelativeComponent;
    UINT RelativeType;
    UINT RelativeNumber;

    VOID
    Clear();

    VOID
    Load(
        _In_ DWORD Token);

    BOOLEAN
    LoadRelative(
        _In_ DWORD Token);
};

struct D9SvDest
{
    BOOLEAN Present;
    UINT Number;
    DWORD Mask;
    DWORD Modifier;
    UINT Shift;
    UINT Type;
    DWORD Coordinates;
    DWORD Relative;
    DWORD RelativeComponent;
    UINT RelativeType;
    UINT RelativeNumber;
};

/* One decoded instruction; pixel models that look back keep the whole chain */
struct D9SvInstruction
{
    UINT Opcode;
    DWORD Controls;
    BOOLEAN Predicated;
    UINT DestCount;
    UINT SourceCount;
    UINT Length;
    D9SvInstruction* Previous;
    D9SvInstruction* Next;
    PCSTR File;
    INT Line;
    UINT Statement;
    SIZE_T Offset;
    D9SvDest Dest;
    D9SvSource Predicate;
    D9SvSource Source[4];
    DWORD DclInfo;
    DWORD Values[4];

    VOID
    Clear(
        _In_opt_ D9SvInstruction* Before);

    virtual
    VOID
    ClearExtra();

    virtual
    VOID
    ComputeReads(
        _In_ DWORD ShaderVersion);

    virtual ~D9SvInstruction() = default;
};

/* Flow control nesting */
static constexpr UINT D9SvBlockNone = 0;
static constexpr UINT D9SvBlockDynamicIf = 1;
static constexpr UINT D9SvBlockStaticIf = 2;
static constexpr UINT D9SvBlockLoop = 3;
static constexpr UINT D9SvBlockRep = 4;
static constexpr UINT D9SvFlowLevels = 53;
static constexpr UINT D9SvLabels = 2048;

struct D9SvCall
{
    UINT Label;
    D9SvCall* Next;
};

struct D9SvFlow
{
    struct
    {
        UINT Kind;
        BOOLEAN SeenElse;
    } Blocks[D9SvFlowLevels];
    UINT Depth;
    BYTE LoopDepth;
    BYTE RepDepth;
    BYTE LoopOrRepDepth;
    UINT LoopCount;
    UINT LoopOrRepCount;
    BYTE StaticIfDepth;
    BYTE DynamicIfDepth;
    BYTE CallDepth;
    BOOLEAN InSubroutine;
    BOOLEAN FromBreakingLoop;
    UINT CurrentLabel;
    DWORD PredicateWritten;
    BYTE LoopBroke[D9SvFlowLevels];
    BYTE LoopGradient[D9SvFlowLevels];
    D9SvCall* Calls[D9SvFlowLevels];
    BYTE LabelCallDepth[D9SvLabels];
    BYTE LabelLoopDepth[D9SvLabels];
    BYTE LabelMinLoop[D9SvLabels];
    BYTE LabelRepDepth[D9SvLabels];
    BYTE LabelLoopOrRepDepth[D9SvLabels];
    BYTE LabelStaticIfDepth[D9SvLabels];
    BYTE LabelDynamicIfDepth[D9SvLabels];
    BYTE LabelFromBreakingLoop[D9SvLabels];

    VOID
    Reset();

    VOID
    Push(
        _In_ UINT Kind);

    VOID
    Pop();

    VOID
    BeginSubroutine(
        _In_ UINT Label);

    BOOLEAN
    Call(
        _In_ UINT Label);

    VOID
    Returned();

    BOOLEAN
    InsideIf() const;

    BOOLEAN
    InSubroutineOutsideLoops() const;

    BOOLEAN
    LoopCounterUsable() const;

    UINT
    TopKind() const
    {
        return Blocks[Depth].Kind;
    }

    VOID
    ClearCalls(
        _In_ UINT Level);

    VOID
    CloseCalls(
        _In_ UINT Level);

    ~D9SvFlow();
};

/* Packed set of the 2048 labels */
struct D9SvLabelSet
{
    DWORD Bits[D9SvLabels / 32];

    BOOLEAN
    Has(
        _In_ UINT Label) const
    {
        return (Bits[(Label >> 5) & 63] & (1u << (Label & 31))) != 0;
    }

    VOID
    Add(
        _In_ UINT Label)
    {
        Bits[(Label >> 5) & 63] |= 1u << (Label & 31);
    }
};

/* What the caller hands a checker and what it reads back */
struct D9SvParams
{
    const DWORD* Tokens;
    DWORD Version;
    BOOLEAN Interface;
    BOOLEAN DevicePath;
    const D9_SHADER_CHECK_DEVICE* Device;
    DWORD Flags;
    BOOLEAN TargetMessages;
    BOOLEAN PixelTargetMessages;
    BOOLEAN HardwareCapable;
    BOOLEAN SoftwareCapable;
    DWORD Outputs;
};

class D9SvChecker
{
public:
    D9SvParams* Params = nullptr;
    D9_SHADER_MESSAGE_SINK Sink = nullptr;
    PVOID SinkContext = nullptr;
    const D3DCAPS9* Caps = nullptr;
    DWORD Behavior = 0;
    DWORD DeviceType = 0;
    DWORD Version = 0;
    BOOLEAN Ready = FALSE;
    BOOLEAN Stream = FALSE;
    BOOLEAN Ended = FALSE;
    const DWORD* Start = nullptr;
    const DWORD* Cursor = nullptr;
    UINT Statements = 0;
    UINT Errors = 0;
    D9SvInstruction* Current = nullptr;
    D9SvInstruction* First = nullptr;
    BOOLEAN SkipSource[4];

    D9SvChecker();

    virtual ~D9SvChecker();

    VOID
    Bind(
        _In_ D9SvParams* Settings);

    virtual
    HRESULT
    Begin(
        _In_ D9SvParams* Settings) = 0;

    virtual
    BOOLEAN
    Decode(
        _In_ const DWORD* Tokens,
        _Inout_ UINT* Used) = 0;

    virtual
    BOOLEAN
    CheckInstruction() = 0;

    virtual
    VOID
    CheckEnd() = 0;

    virtual
    BOOLEAN
    KeepsHistory() const
    {
        return FALSE;
    }

    virtual
    D9SvInstruction*
    NewInstruction();

    BOOLEAN
    RunStream();

    HRESULT
    Feed(
        _In_opt_z_ PCSTR File,
        _In_ INT Line,
        _In_reads_(Count) const DWORD* Tokens,
        _In_ UINT Count);

    HRESULT
    Finish();

    VOID
    Report(
        _In_ DWORD Level,
        _In_ DWORD Id,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...);

    VOID
    Fail(
        _In_ DWORD Level,
        _In_ DWORD Id,
        _In_z_ _Printf_format_string_ PCSTR Format,
        ...);

    static
    VOID
    ComponentText(
        _In_ DWORD Mask,
        _In_ BOOLEAN Colors,
        _In_ BOOLEAN Coordinates,
        _Out_writes_(32) PSTR Text);

    BOOLEAN
    PrepareInstruction(
        _Out_ BOOLEAN* Fresh);

    VOID
    DropComment(
        _In_ BOOLEAN Fresh);

    BOOLEAN
    HardwareRequired() const
    {
        return (Params->Flags & D9ShaderCheckHardware) != 0 || (Behavior & D3DCREATE_HARDWARE_VERTEXPROCESSING) != 0;
    }

    BOOLEAN
    SoftwareRequired() const
    {
        return (Params->Flags & D9ShaderCheckSoftware) != 0 || (Behavior & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0;
    }
};

D9SvChecker*
D9SvCreateVertexChecker(
    _In_ DWORD Version);

D9SvChecker*
D9SvCreatePixelChecker(
    _In_ DWORD Version);

D9SvChecker*
D9SvStart(
    _In_ D9SvParams* Settings,
    _In_ BOOLEAN Pixel,
    _In_opt_ D9_SHADER_MESSAGE_SINK Sink,
    _In_opt_ PVOID Context);
