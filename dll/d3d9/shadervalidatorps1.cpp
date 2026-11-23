/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pixel shader rules for ps_1_0 through ps_1_4
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shadervalidatorcore.hpp"

#include <new>

D9SvChecker*
D9SvCreatePixel1Checker(
    _In_ DWORD Version,
    _In_ BOOLEAN Model14);

static constexpr DWORD D9Ps1Version10 = 0xFFFF0100;
static constexpr DWORD D9Ps1Version11 = 0xFFFF0101;
static constexpr DWORD D9Ps1Version12 = 0xFFFF0102;
static constexpr DWORD D9Ps1Version13 = 0xFFFF0103;
static constexpr DWORD D9Ps1Version14 = 0xFFFF0104;

/* ps_1_x opcodes the rules single out */
static constexpr UINT D9Ps1OpTexcoord = 64;
static constexpr UINT D9Ps1OpTexkill = 65;
static constexpr UINT D9Ps1OpTex = 66;
static constexpr UINT D9Ps1OpTexbem = 67;
static constexpr UINT D9Ps1OpTexbeml = 68;
static constexpr UINT D9Ps1OpTexreg2ar = 69;
static constexpr UINT D9Ps1OpTexreg2gb = 70;
static constexpr UINT D9Ps1OpTexm3x2pad = 71;
static constexpr UINT D9Ps1OpTexm3x2tex = 72;
static constexpr UINT D9Ps1OpTexm3x3pad = 73;
static constexpr UINT D9Ps1OpTexm3x3tex = 74;
static constexpr UINT D9Ps1OpTexm3x3spec = 76;
static constexpr UINT D9Ps1OpTexm3x3vspec = 77;
static constexpr UINT D9Ps1OpTexreg2rgb = 82;
static constexpr UINT D9Ps1OpTexdp3tex = 83;
static constexpr UINT D9Ps1OpTexm3x2depth = 84;
static constexpr UINT D9Ps1OpTexdp3 = 85;
static constexpr UINT D9Ps1OpTexm3x3 = 86;
static constexpr UINT D9Ps1OpTexdepth = 87;
static constexpr UINT D9Ps1OpCmp = 88;
static constexpr UINT D9Ps1OpBem = 89;

static constexpr UINT D9Ps1RegTexture = 3;

/* Source modifiers past the ones the core names */
static constexpr DWORD D9Ps1ModBias = 0x2000000;
static constexpr DWORD D9Ps1ModBiasNegate = 0x3000000;
static constexpr DWORD D9Ps1ModSign = 0x4000000;
static constexpr DWORD D9Ps1ModSignNegate = 0x5000000;
static constexpr DWORD D9Ps1ModComplement = 0x6000000;
static constexpr DWORD D9Ps1ModTimes2 = 0x7000000;
static constexpr DWORD D9Ps1ModTimes2Negate = 0x8000000;
static constexpr DWORD D9Ps1ModDivideZ = 0x9000000;
static constexpr DWORD D9Ps1ModDivideW = 0xA000000;

static constexpr DWORD D9Ps1Saturate = 0x100000;
static constexpr DWORD D9Ps1Color = 0x70000;
static constexpr DWORD D9Ps1Alpha = 0x80000;
static constexpr DWORD D9Ps1MaskXy = 0x30000;
static constexpr DWORD D9Ps1SwizzleXyz = 0xA40000;
static constexpr DWORD D9Ps1SwizzleXyw = 0xF40000;

static const PCSTR D9Ps1Ordinals[] = { "first", "second", "third", "fourth" };

/* One read or write of a register component, linked both by age and by kind */
struct D9Ps1Access
{
    D9Ps1Access* Older;
    D9Ps1Access* Newer;
    D9Ps1Access* PriorWrite;
    D9Ps1Access* PriorRead;
    D9SvInstruction* Instruction;
    BOOLEAN Write;
    BOOLEAN Read;
    D9Ps1Access* Chain;
};

struct D9Ps1History
{
    D9Ps1Access* Oldest;
    D9Ps1Access* Newest;
    D9Ps1Access* LastWrite;
    D9Ps1Access* LastRead;
};

/* A register file: size, whether arithmetic may write it, read ports, and access history per component */
struct D9Ps1File
{
    UINT Count = 0;
    BOOLEAN Writable = FALSE;
    UINT Ports = 0;
    D9Ps1History* History[4] = {};
    BYTE* Defined = nullptr;

    BOOLEAN
    Create(
        _In_ UINT Registers,
        _In_ BOOLEAN CanWrite,
        _In_ UINT ReadPorts)
    {
        Count = Registers;
        Writable = CanWrite;
        Ports = ReadPorts;
        for (UINT Component = 0; Component < 4; Component++)
        {
            History[Component] = new (std::nothrow) D9Ps1History[Registers]();
            if (History[Component] == nullptr)
                return FALSE;
        }
        Defined = new (std::nothrow) BYTE[Registers]();
        return Defined != nullptr;
    }

    ~D9Ps1File()
    {
        for (UINT Component = 0; Component < 4; Component++)
            delete[] History[Component];
        delete[] Defined;
    }
};

class D9Ps1Instruction : public D9SvInstruction
{
public:
    BOOLEAN TexOp;
    BOOLEAN CoIssue;
    UINT Pair;
    BOOLEAN TexMatrix;
    BOOLEAN Dependent;

    VOID
    ClearExtra() override
    {
        TexOp = FALSE;
        CoIssue = FALSE;
        Pair = MAXUINT;
        TexMatrix = FALSE;
        Dependent = FALSE;
    }

    VOID
    ComputeReads(
        _In_ DWORD ShaderVersion) override;
};

static
inline
D9Ps1Instruction*
D9Ps1Cast(
    _In_opt_ D9SvInstruction* Instruction)
{
    return static_cast<D9Ps1Instruction*>(Instruction);
}

static
inline
D9Ps1Instruction*
D9Ps1Writer(
    _In_opt_ const D9Ps1Access* Access)
{
    return Access != nullptr ? D9Ps1Cast(Access->Instruction) : nullptr;
}

/* Which components each source reads, and what tex* instructions read through their destination */
VOID
D9Ps1Instruction::ComputeReads(
    _In_ DWORD ShaderVersion)
{
    BOOLEAN Model14 = ShaderVersion == D9Ps1Version14;

    if (Opcode == D9Ps1OpTexkill)
        Dest.Coordinates = (Model14 && Dest.Type == D9SvRegTemp) ? D9SvMaskXyz : D9SvMaskAll;
    else if (Opcode == D9Ps1OpTexm3x2depth || Opcode == D9Ps1OpTexdepth)
        Dest.Coordinates = D9Ps1MaskXy;

    for (UINT Index = 0; Index < SourceCount; Index++)
    {
        DWORD Positions;

        switch (Opcode)
        {
        case D9SvOpDp3:
        case D9Ps1OpTexbem:
        case D9Ps1OpTexbeml:
            Positions = D9SvMaskXyz;
            break;
        case D9SvOpDp4:
            Positions = D9SvMaskAll;
            break;
        case D9Ps1OpTexcoord:
        case D9Ps1OpTex:
            Positions = Model14 ? D9SvMaskXyz : D9SvMaskAll;
            break;
        case D9Ps1OpBem:
            Positions = D9Ps1MaskXy;
            break;
        default:
            Positions = Dest.Mask;
            break;
        }
        Source[Index].Read = D9SvSwizzled(Source[Index].Swizzle, Positions);
    }
}

/* ps_1_0 to ps_1_3 state past the register files, laid out so the co-issue port lists overrun into the fields after
   them the way the reference's do */
struct D9Ps10State
{
    DWORD PairRegs[16];
    DWORD PairCounts[4];
    DWORD TexDeclared;
    BYTE SawArith;
    BYTE OrderReported;
    BYTE Pad0[2];
    DWORD TexmState;
    DWORD PrevOpcode;
    DWORD CmpCount;
    BYTE DefOpen;
    BYTE Pad1[3];
    DWORD Dp4Count;
    BYTE ArithSeen;
    BYTE Pad2[3];
    DWORD TexSlots;
    DWORD ArithSlots;
    DWORD TotalSlots;
    DWORD TexLimit;
    DWORD ArithLimit;
    DWORD TotalLimit;
};
static_assert(sizeof(D9Ps10State) == 136, "ps_1_x state layout");

/* ps_1_4 state; the texture coordinate swizzle table overruns into the fields after it as the reference's does */
struct D9Ps14State
{
    DWORD CoordSwizzle[6];
    BYTE CoordSeen[6];
    BYTE Pad0[2];
    DWORD DzCount;
    BYTE DzReported;
    BYTE PhaseSeen;
    BYTE Pad1[2];
    DWORD StageMask;
    BYTE SawArith;
    BYTE OrderReported;
    BYTE DepthUsed;
    BYTE DepthBeforePhase;
    BYTE KillBeforePhase;
    BYTE BemUsed;
    BYTE DefOpen;
    BYTE PairOpen;
    DWORD TexSlots;
    DWORD ArithSlots;
    DWORD TotalSlots;
    DWORD TexLimit;
    DWORD ArithLimit;
};
static_assert(sizeof(D9Ps14State) == 72, "ps_1_4 state layout");

template <class T>
static
DWORD
D9Ps1PeekDword(
    _In_ const T* State,
    _In_ SIZE_T Offset)
{
    DWORD Value = 0;

    if (Offset + sizeof(Value) <= sizeof(*State))
        memcpy(&Value, reinterpret_cast<const BYTE*>(State) + Offset, sizeof(Value));
    return Value;
}

template <class T>
static
VOID
D9Ps1PokeDword(
    _Inout_ T* State,
    _In_ SIZE_T Offset,
    _In_ DWORD Value)
{
    if (Offset + sizeof(Value) <= sizeof(*State))
        memcpy(reinterpret_cast<BYTE*>(State) + Offset, &Value, sizeof(Value));
}

template <class T>
static
BYTE
D9Ps1PeekByte(
    _In_ const T* State,
    _In_ SIZE_T Offset)
{
    return Offset < sizeof(*State) ? reinterpret_cast<const BYTE*>(State)[Offset] : 0;
}

template <class T>
static
VOID
D9Ps1PokeByte(
    _Inout_ T* State,
    _In_ SIZE_T Offset,
    _In_ BYTE Value)
{
    if (Offset < sizeof(*State))
        reinterpret_cast<BYTE*>(State)[Offset] = Value;
}

class D9Ps1Checker : public D9SvChecker
{
public:
    BOOLEAN Model14 = FALSE;
    UINT PairCounter = 0;
    D9Ps1File Temps;
    D9Ps1File Inputs;
    D9Ps1File Consts;
    D9Ps1File Textures;
    D9Ps1Access* Accesses = nullptr;
    UINT TexmBase = 0;
    D9Ps10State Old = {};
    D9SvInstruction* Phase = nullptr;
    DWORD AlphaLost = 0;
    DWORD BlueLost = 0;
    BOOLEAN DzBeforePhase = FALSE;
    BOOLEAN InputBeforePhase = FALSE;
    D9Ps14State New = {};

    ~D9Ps1Checker() override
    {
        while (Accesses != nullptr)
        {
            D9Ps1Access* Next = Accesses->Chain;

            delete Accesses;
            Accesses = Next;
        }
    }

    HRESULT
    Begin(
        _In_ D9SvParams* Settings) override;

    BOOLEAN
    Decode(
        _In_ const DWORD* Tokens,
        _Inout_ UINT* Used) override;

    BOOLEAN
    CheckInstruction() override;

    VOID
    CheckEnd() override;

    BOOLEAN
    KeepsHistory() const override
    {
        return TRUE;
    }

    D9SvInstruction*
    NewInstruction() override
    {
        return new (std::nothrow) D9Ps1Instruction;
    }

    D9Ps1Instruction*
    Ins() const
    {
        return D9Ps1Cast(Current);
    }

    BOOLEAN
    NoDevice() const
    {
        return Params->Device == nullptr;
    }

    D9Ps1File*
    FileOf(
        _In_ UINT Type);

    D9Ps1Access*
    MakeAccess(
        _In_opt_ D9Ps1Access* Older,
        _In_opt_ D9Ps1Access* PriorWrite,
        _In_opt_ D9Ps1Access* PriorRead,
        _In_ D9SvInstruction* Instruction,
        _In_ BOOLEAN Write);

    BOOLEAN
    Record(
        _Inout_ D9Ps1History* History,
        _In_ D9SvInstruction* Instruction,
        _In_ BOOLEAN Write);

    BOOLEAN
    RecordBefore(
        _Inout_ D9Ps1History* History,
        _Inout_ D9Ps1Access* Writer,
        _In_ D9SvInstruction* Instruction);

    VOID
    RecordReads(
        _In_ D9Ps1File* File,
        _In_ UINT Number,
        _In_ DWORD Read,
        _In_reads_(4) D9Ps1Access* const* SamePair);

    VOID
    MarkTexOp(
        _Inout_ D9Ps1Instruction* Instruction) const;

    BOOLEAN
    Recognized();

    /* ps_1_0 to ps_1_3 */
    BOOLEAN
    Supported10();

    BOOLEAN
    ParamCount10();

    VOID
    SourceParams10();

    VOID
    NegateAfterSat10();

    VOID
    SatBeforeModifier10();

    VOID
    DependentReads10();

    VOID
    NoLongerAvailable10();

    VOID
    SourceInitialized10();

    VOID
    DestParam10();

    VOID
    Ports10();

    VOID
    TexDeclaredInOrder10();

    VOID
    TexAfterArithmetic10();

    VOID
    TexmSequence10();

    VOID
    TexmRegisters10();

    VOID
    Lrp10();

    VOID
    Definition10();

    VOID
    Pairing10();

    VOID
    CountSlots10();

    VOID
    LogTextures10();

    VOID
    R0Written10();

    /* ps_1_4 */
    BOOLEAN
    Supported14();

    BOOLEAN
    ParamCount14();

    BOOLEAN
    Marker14();

    VOID
    SourceParams14();

    VOID
    DependentReads14();

    VOID
    SatBeforeComplement14();

    VOID
    SourceInitialized14();

    VOID
    DestParam14();

    VOID
    Ports14();

    VOID
    TexAfterArithmetic14();

    VOID
    TexStages14();

    VOID
    DzLimit14();

    VOID
    Texdepth14();

    VOID
    Bem14();

    VOID
    Lrp14();

    VOID
    Definition14();

    VOID
    Pairing14();

    VOID
    CountSlots14();

    VOID
    LogTextures14();

    VOID
    R0Written14();
};

D9SvChecker*
D9SvCreatePixel1Checker(
    _In_ DWORD Version,
    _In_ BOOLEAN Model14)
{
    D9Ps1Checker* Checker = new (std::nothrow) D9Ps1Checker;

    UNREFERENCED_PARAMETER(Version);
    if (Checker == nullptr)
        return nullptr;
    Checker->Model14 = Model14;
    Checker->Old.DefOpen = 1;
    for (UINT Index = 0; Index < 6; Index++)
        Checker->New.CoordSwizzle[Index] = D9SvSwizzleIdentity;
    Checker->New.DefOpen = 1;
    return Checker;
}

/* Setup: version checks and register files */

HRESULT
D9Ps1Checker::Begin(
    _In_ D9SvParams* Settings)
{
    char Name[32];
    WORD Kind = HIWORD(Settings->Version);
    BOOLEAN Made;

    Bind(Settings);
    Params->Outputs = MAXDWORD;
    if (Kind == 0xFFFE)
    {
        Report(6, Model14 ? 91 : 7, "Version Token: 0x%x indicates a vertex shader.  Pixel shader version token must "
               "be of the form 0xffff****.", Version);
        return E_FAIL;
    }
    if (Kind != 0xFFFF)
    {
        Report(6, Model14 ? 92 : 8, "Version Token: 0x%x is invalid. Pixel shader version token must be of the form "
               "0xffff****. Aborting pixel shader validation.", Version);
        return E_FAIL;
    }
    if (Caps != nullptr && LOWORD(Caps->PixelShaderVersion) < LOWORD(Version))
    {
        D9SvVersionName(Version, Name);
        if (Model14)
            Report(6, 93, "Version Token: Shader version %s is not supported by device. ", Name);
        else
            Report(6, 9, "Version Token: Shader version %s is not supported by hardware. ", Name);
        return E_FAIL;
    }

    if (Model14)
    {
        if (Version != D9Ps1Version14)
        {
            D9SvVersionName(Version, Name);
            Report(6, 94, "Version Token: Shader version %s is not supported. Aborting pixel shader validation.",
                   Name);
            return E_FAIL;
        }
        Made = Inputs.Create(2, FALSE, 2) && Consts.Create(8, FALSE, 2) && Textures.Create(6, FALSE, 1) &&
               Temps.Create(6, TRUE, 3);
        New.TexLimit = 6;
        New.ArithLimit = 8;
    }
    else
    {
        if (Version == D9Ps1Version10)
        {
            Report(6, 85, "ps_1_0 is no longer supported.  It turned out that pixel shader capable hardware will "
                   "always support at least ps_1_1 (which is not as limited a model as ps_1_0 was). To convert a "
                   "ps_1_0 shader up to ps_1_1, the only change needed is to make the version number 1_1. ");
            return E_FAIL;
        }
        if (Version != D9Ps1Version11 && Version != D9Ps1Version12 && Version != D9Ps1Version13)
        {
            D9SvVersionName(Version, Name);
            Report(6, 10, "Version Token: Shader version %s is not supported. Aborting pixel shader validation.",
                   Name);
            return E_FAIL;
        }
        Made = Temps.Create(2, TRUE, 2) && Inputs.Create(2, FALSE, 2) && Consts.Create(8, FALSE, 2) &&
               Textures.Create(4, TRUE, Version == D9Ps1Version11 ? 2 : 3);
        Old.TexLimit = 4;
        Old.ArithLimit = 8;
        Old.TotalLimit = 12;
    }
    if (!Made)
    {
        Report(6, 0, "Out of memory.");
        return E_FAIL;
    }
    Ready = TRUE;
    return S_OK;
}

/* Access history */

D9Ps1File*
D9Ps1Checker::FileOf(
    _In_ UINT Type)
{
    switch (Type)
    {
    case D9SvRegTemp:
        return &Temps;
    case D9SvRegInput:
        return &Inputs;
    case D9SvRegConst:
        return &Consts;
    case D9Ps1RegTexture:
        return &Textures;
    default:
        return nullptr;
    }
}

D9Ps1Access*
D9Ps1Checker::MakeAccess(
    _In_opt_ D9Ps1Access* Older,
    _In_opt_ D9Ps1Access* PriorWrite,
    _In_opt_ D9Ps1Access* PriorRead,
    _In_ D9SvInstruction* Instruction,
    _In_ BOOLEAN Write)
{
    D9Ps1Access* Access = new (std::nothrow) D9Ps1Access;

    if (Access == nullptr)
        return nullptr;
    Access->Older = Older;
    Access->Newer = nullptr;
    if (Older != nullptr)
        Older->Newer = Access;
    Access->PriorWrite = PriorWrite;
    Access->PriorRead = PriorRead;
    Access->Instruction = Instruction;
    Access->Write = Write;
    Access->Read = !Write;
    Access->Chain = Accesses;
    Accesses = Access;
    return Access;
}

BOOLEAN
D9Ps1Checker::Record(
    _Inout_ D9Ps1History* History,
    _In_ D9SvInstruction* Instruction,
    _In_ BOOLEAN Write)
{
    D9Ps1Access* Access = MakeAccess(History->Newest, History->LastWrite, History->LastRead, Instruction, Write);

    History->Newest = Access;
    if (Access == nullptr)
        return FALSE;
    if (History->Oldest == nullptr)
        History->Oldest = Access;
    if (Write)
        History->LastWrite = Access;
    else
        History->LastRead = Access;
    return TRUE;
}

/* A read by the second half of a co-issued pair goes ahead of the first half's write */
BOOLEAN
D9Ps1Checker::RecordBefore(
    _Inout_ D9Ps1History* History,
    _Inout_ D9Ps1Access* Writer,
    _In_ D9SvInstruction* Instruction)
{
    D9Ps1Access* Access = MakeAccess(Writer->Older, Writer->PriorWrite, Writer->PriorRead, Instruction, FALSE);

    if (Access == nullptr)
        return FALSE;
    if (History->Oldest == Writer)
        History->Oldest = Access;
    if (History->LastRead == Writer->PriorRead)
        History->LastRead = Access;
    for (D9Ps1Access* Walk = Writer; Walk != nullptr; Walk = Walk->Older)
    {
        if (Walk->Read && Walk->Older != nullptr && Walk->Older->Read)
            break;
        Walk->PriorRead = Access;
    }
    Writer->Older = Access;
    Access->Newer = Writer;
    return TRUE;
}

/* Logs the reads of one source, ordering them ahead of writes made earlier in the same co-issued pair */
VOID
D9Ps1Checker::RecordReads(
    _In_ D9Ps1File* File,
    _In_ UINT Number,
    _In_ DWORD Read,
    _In_reads_(4) D9Ps1Access* const* SamePair)
{
    for (UINT Component = 0; Component < 4; Component++)
    {
        D9Ps1History* History;
        BOOLEAN Done;

        if ((Read & D9SvComponent(Component)) == 0)
            continue;
        History = &File->History[Component][Number];
        if (SamePair[Component] != nullptr)
        {
            D9Ps1Access* Prior = SamePair[Component]->PriorRead;

            if (Prior != nullptr && Prior->Instruction == Current)
                continue;
            Done = RecordBefore(History, SamePair[Component], Current);
        }
        else
        {
            if (D9Ps1Writer(History->LastRead) == Current)
                continue;
            Done = Record(History, Current, FALSE);
        }
        if (!Done)
            Fail(6, 0, "Out of memory");
    }
}

/* Instruction decoding */

VOID
D9Ps1Checker::MarkTexOp(
    _Inout_ D9Ps1Instruction* Instruction) const
{
    UINT Opcode = Instruction->Opcode;

    if (Model14)
    {
        if (Opcode == D9Ps1OpTexcoord || Opcode == D9Ps1OpTexkill || Opcode == D9Ps1OpTex ||
            Opcode == D9Ps1OpTexdepth)
        {
            Instruction->TexOp = TRUE;
        }
        if (Opcode == D9Ps1OpTex)
            Instruction->Dependent = TRUE;
        else if (Opcode == D9Ps1OpTexcoord || Opcode == D9Ps1OpTexkill || Opcode == D9Ps1OpTexdepth)
            Instruction->Dependent = FALSE;
        return;
    }

    switch (Opcode)
    {
    case D9Ps1OpTexm3x2pad:
    case D9Ps1OpTexm3x2tex:
    case D9Ps1OpTexm3x3pad:
    case D9Ps1OpTexm3x3tex:
    case D9Ps1OpTexm3x3spec:
    case D9Ps1OpTexm3x3vspec:
    case D9Ps1OpTexm3x2depth:
    case D9Ps1OpTexm3x3:
        Instruction->TexMatrix = TRUE;
        Instruction->TexOp = TRUE;
        break;
    case D9Ps1OpTexcoord:
    case D9Ps1OpTexkill:
    case D9Ps1OpTex:
    case D9Ps1OpTexbem:
    case D9Ps1OpTexbeml:
    case D9Ps1OpTexreg2ar:
    case D9Ps1OpTexreg2gb:
    case D9Ps1OpTexreg2rgb:
    case D9Ps1OpTexdp3tex:
    case D9Ps1OpTexdp3:
        Instruction->TexOp = TRUE;
        break;
    default:
        break;
    }

    switch (Opcode)
    {
    case D9Ps1OpTexcoord:
    case D9Ps1OpTexkill:
    case D9Ps1OpTexm3x2pad:
    case D9Ps1OpTexm3x3pad:
    case D9Ps1OpTexm3x2depth:
    case D9Ps1OpTexdp3:
    case D9Ps1OpTexm3x3:
        Instruction->Dependent = FALSE;
        break;
    case D9Ps1OpTex:
    case D9Ps1OpTexbem:
    case D9Ps1OpTexbeml:
    case D9Ps1OpTexreg2ar:
    case D9Ps1OpTexreg2gb:
    case D9Ps1OpTexm3x2tex:
    case D9Ps1OpTexm3x3tex:
    case D9Ps1OpTexm3x3spec:
    case D9Ps1OpTexm3x3vspec:
    case D9Ps1OpTexreg2rgb:
    case D9Ps1OpTexdp3tex:
        Instruction->Dependent = TRUE;
        break;
    default:
        break;
    }
}

BOOLEAN
D9Ps1Checker::Decode(
    _In_ const DWORD* Tokens,
    _Inout_ UINT* Used)
{
    D9Ps1Instruction* Op = Ins();
    UINT Limit = Stream ? MAXUINT : *Used;
    UINT Position;
    DWORD Token = Tokens[0];

    *Used = 0;
    Op->Opcode = Token & 0xFFFF;
    if (Op->Opcode == D9SvOpComment)
    {
        UINT Length = ((Token >> 16) & 0x7FFF) + 1;

        if (!Stream && Length != Limit)
        {
            Report(2, 5, "Unexpected DWORD count for instruction.  Aborting validation.");
            return FALSE;
        }
        *Used = Length;
        return TRUE;
    }

    MarkTexOp(Op);
    if ((Token & 0x40000000) != 0)
        Op->CoIssue = TRUE;
    else if (Op->Opcode != D9SvOpNop)
        PairCounter += 1;
    Op->Pair = PairCounter;
    Statements += 1;
    Op->Statement = Statements;
    Op->Offset = Stream ? static_cast<SIZE_T>(Tokens - Start) : MAXSIZE_T;
    if ((Token & 0xBFFF0000) != 0)
    {
        Report(2, 1, "Reserved bit(s) set in instruction parameter token!  Aborting validation.");
        return FALSE;
    }
    Op->Controls = Token & 0xFF0000;

    /* Only a zero length nibble gets a dcl past the reserved bits, and it needs two */
    if (Op->Opcode == D9SvOpDcl)
    {
        Report(2, 2, "dcl instruction should have exactly 2 parameter tokens.  Aborting validation.");
        return FALSE;
    }

    *Used = 1;
    if (Limit == 1)
        goto Done;
    Position = 1;

    switch (Op->Opcode)
    {
    case 25: case 26: case 27: case 28: case 29: case 30: case 38: case 39: case 40: case 41: case 42: case 43:
    case 44: case 45: case 96: case D9SvOpEnd:
        goto Sources;
    default:
        break;
    }

    if ((Tokens[1] & 0x80000000) != 0)
    {
        DWORD Dest = Tokens[1];

        Op->DestCount += 1;
        Op->Dest.Present = TRUE;
        Op->Dest.Number = D9SvRegisterNumber(Dest);
        Op->Dest.Mask = Dest & 0xF0000;
        Op->Dest.Modifier = Dest & 0xF00000;
        Op->Dest.Shift = (Dest >> 24) & 0xF;
        Op->Dest.Type = D9SvRegisterType(Dest);
        Op->Dest.Relative = Dest & D9SvRelativeBit;
        if ((Dest & 0x4000E000) != 0)
        {
            Report(2, 3, "Reserved bit(s) set in destination parameter token!  Aborting validation.");
            *Used = 0;
            return FALSE;
        }
        *Used = 2;
        if (Limit == 2)
            goto Done;
        if (Op->Opcode == D9SvOpDefb || Op->Opcode == D9SvOpDefi || Op->Opcode == D9SvOpDef)
        {
            UINT Total = Op->Opcode == D9SvOpDefb ? 3 : 6;

            if (!Stream && Total != Limit)
            {
                Report(2, 5, "Unexpected DWORD count for instruction.  Aborting validation.");
                return FALSE;
            }
            *Used = Total;
            if (Stream)
                return TRUE;
            goto Done;
        }
        Position = 2;
    }

Sources:
    while ((Tokens[Position] & 0x80000000) != 0)
    {
        DWORD Param = Tokens[Position];

        Op->SourceCount += 1;
        if (Op->DestCount + Op->SourceCount > 5 || Op->SourceCount > 4)
        {
            Op->SourceCount -= 1;
            Report(2, 5, "Unexpected DWORD count for instruction.  Aborting validation.");
            return FALSE;
        }
        Op->Source[Op->SourceCount - 1].Load(Param);
        if ((Param & 0x4000E000) != 0)
        {
            Report(2, 4, "Reserved bit(s) set in source %d parameter token!  Aborting validation.", Op->SourceCount);
            *Used = 0;
            return FALSE;
        }
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;
        Position += 1;
    }

Done:
    Op->ComputeReads(Version);
    return TRUE;
}

BOOLEAN
D9Ps1Checker::Recognized()
{
    switch (Current->Opcode)
    {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 14:
    case 15: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
    case 29: case 30: case 31: case 32: case 33: case 35: case 36: case 37: case 38: case 39: case 40: case 41:
    case 42: case 43: case 44: case 45: case 47: case 48: case 64: case 65: case 66: case 67: case 68: case 69:
    case 70: case 71: case 72: case 73: case 74: case 76: case 77: case 80: case 81: case 82: case 83: case 84:
    case 85: case 86: case 87: case 88: case 89: case 90: case 91: case 92: case 93: case 94: case 95: case 96:
    case D9SvOpPhase:
        return TRUE;
    default:
        Fail(2, 6, "Unrecognized instruction. Aborting pixel shader validation.");
        return FALSE;
    }
}

/* ps_1_0 to ps_1_3 rules */

BOOLEAN
D9Ps1Checker::Supported10()
{
    char Name[32];
    UINT Opcode = Current->Opcode;

    switch (Opcode)
    {
    case 1: case 2: case 3: case 4: case 5: case 8: case 18: case 64: case 65: case 66: case 67: case 68: case 69:
    case 70: case 71: case 72: case 73: case 74: case 76: case 77: case 80: case 81:
        return TRUE;
    default:
        break;
    }
    if (Version >= D9Ps1Version12)
    {
        switch (Opcode)
        {
        case 9: case 82: case 83: case 85: case 86: case 88:
            return TRUE;
        default:
            break;
        }
        if (Version >= D9Ps1Version13 && Opcode == D9Ps1OpTexm3x2depth)
            return TRUE;
    }
    if (Opcode == D9SvOpEnd || Opcode == D9SvOpNop)
        return TRUE;
    D9SvVersionName(Version, Name);
    Fail(2, 11, "Instruction not supported by %s shader.", Name);
    return FALSE;
}

BOOLEAN
D9Ps1Checker::ParamCount10()
{
    UINT Dests = Current->DestCount;
    UINT Sources = Current->SourceCount;
    BOOLEAN Bad;

    switch (Current->Opcode)
    {
    case 0:
        Bad = Dests != 0 || Sources != 0;
        break;
    case 1: case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 77: case 82: case 83:
    case 84: case 85: case 86:
        Bad = Dests != 1 || Sources != 1;
        break;
    case 2: case 3: case 5: case 8: case 9: case 76:
        Bad = Dests != 1 || Sources != 2;
        break;
    case 4: case 18: case 80: case 88:
        Bad = Dests != 1 || Sources != 3;
        break;
    case 64: case 65: case 66: case 81:
        Bad = Dests != 1 || Sources != 0;
        break;
    default:
        Bad = Dests + Sources > 5;
        break;
    }
    if (!Bad)
        return TRUE;
    Fail(2, 12, "Invalid parameter count.");
    return FALSE;
}

VOID
D9Ps1Checker::SourceParams10()
{
    D9Ps1Instruction* Op = Ins();

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        PCSTR Which = D9Ps1Ordinals[Index];
        BOOLEAN Bad = FALSE;
        D9Ps1File* File;

        if (Source->Type == D9SvRegConst && Source->Modifier != 0)
        {
            Fail(2, 539, "(%s source param) Modifiers are not allowed on constants for ps_1_x.", Which);
            SkipSource[Index] = TRUE;
            continue;
        }

        if (Op->TexOp)
        {
            if (Source->Type != D9Ps1RegTexture)
            {
                if (Op->Opcode == D9Ps1OpTexm3x3spec && Index == 1)
                {
                    if (Source->Type != D9SvRegConst || Source->Swizzle != D9SvSwizzleIdentity ||
                        Source->Modifier != 0)
                    {
                        Fail(2, 13, "Second source parameter for texm3x3spec must be c#.");
                        SkipSource[Index] = TRUE;
                        continue;
                    }
                }
                else
                {
                    Fail(2, 14, "Src reg for tex* instruction must be t# register (%s source param).", Which);
                    Bad = TRUE;
                }
            }
            if (Source->Type == D9SvRegConst)
            {
                File = &Consts;
            }
            else if (Source->Type == D9Ps1RegTexture)
            {
                File = &Textures;
            }
            else
            {
                Fail(2, 15, "Invalid reg type (%s source param).", Which);
                SkipSource[Index] = TRUE;
                continue;
            }
            if (Source->Number >= File->Count)
            {
                Fail(2, 16, "Invalid reg num %d (%s source param).  Max allowed for this type is %d.",
                     Source->Number, Which, File->Count - 1);
                Bad = TRUE;
            }
            if (Source->Modifier != 0)
            {
                if (Source->Modifier != D9Ps1ModSign)
                {
                    Fail(2, 20, "Invalid src mod for tex* instruction (%s source param).", Which);
                    Bad = TRUE;
                }
                else if (Version > D9Ps1Version11)
                {
                    if (Op->Opcode == D9Ps1OpTexbem || Op->Opcode == D9Ps1OpTexbeml)
                        Fail(2, 19, "_bx2 cannot be used on src register for texbem or texbeml instructions.");
                    else if (Op->Opcode == D9Ps1OpTexreg2ar || Op->Opcode == D9Ps1OpTexreg2gb)
                        Fail(2, 18, "_bx2 cannot be used on src register for texreg2ar or texreg2gb instructions.");
                }
                else if (!Op->TexMatrix)
                {
                    Fail(2, 17, "_bx2 is a valid src mod for texM* instructions only (%s source param).", Which);
                }
            }
            if (Source->Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 21, "Source swizzle not allowed for tex* instruction (%s source param).", Which);
                Bad = TRUE;
            }
            if (Bad)
                SkipSource[Index] = TRUE;
            continue;
        }

        File = FileOf(Source->Type);
        if (File == nullptr)
        {
            Fail(2, 22, "Invalid reg type for %s source param.", Which);
            Bad = TRUE;
        }
        else if (Source->Number >= File->Count)
        {
            Fail(2, 23, "Invalid reg num: %d for %s source param. Max allowed for this type is %d.", Source->Number,
                 Which, File->Count - 1);
            Bad = TRUE;
        }
        if ((Op->Opcode == D9Ps1OpCmp || Op->Opcode == D9SvOpDp4) && !Bad && Source->Number == Op->Dest.Number &&
            Source->Type == Op->Dest.Type)
        {
            Fail(2, 24, "For ps_1_2 or ps_1_3, the cmp or dp4 instructions cannot have a source register the same "
                 "as the destination register.");
            Bad = TRUE;
        }
        switch (Source->Modifier)
        {
        case 0:
        case D9SvModNegate:
        case D9Ps1ModBias:
        case D9Ps1ModBiasNegate:
        case D9Ps1ModSign:
        case D9Ps1ModSignNegate:
        case D9Ps1ModComplement:
            break;
        default:
            Fail(2, 25, "Invalid src mod for %s source param.", Which);
            Bad = TRUE;
            break;
        }
        if (Source->Swizzle == D9SvSwizzleZ)
        {
            if ((Op->Dest.Mask & D9Ps1Color) != 0 || Op->Opcode == D9SvOpDp3 || Op->Opcode == D9SvOpDp4)
            {
                Fail(2, 26, "Src selector .b (%s source param) is only valid for instructions that occur in the alpha "
                     "pipe.", Which);
                Bad = TRUE;
            }
        }
        else if (Source->Swizzle != D9SvSwizzleIdentity && Source->Swizzle != D9SvSwizzleW)
        {
            Fail(2, 27, "Invalid src swizzle for %s source param.", Which);
            Bad = TRUE;
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

VOID
D9Ps1Checker::NegateAfterSat10()
{
    D9Ps1Instruction* Op = Ins();

    if (Version >= D9Ps1Version12)
        return;
    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        DWORD Affected = 0;
        char Text[32];

        if (SkipSource[Index])
            continue;
        if (Source->Modifier != D9SvModNegate && Source->Modifier != D9Ps1ModBiasNegate &&
            Source->Modifier != D9Ps1ModSignNegate)
        {
            continue;
        }
        for (UINT Component = 0; Component < 4; Component++)
        {
            D9Ps1File* File;
            D9Ps1Access* Writer;

            if ((Source->Read & D9SvComponent(Component)) == 0)
                continue;
            if (Source->Type == D9SvRegTemp)
                File = &Temps;
            else if (Source->Type == D9Ps1RegTexture)
                File = &Textures;
            else
                continue;

            /* The write that counts comes from before this co-issued pair */
            Writer = File->History[Component][Source->Number].LastWrite;
            while (Writer != nullptr && Writer->Instruction != nullptr &&
                   D9Ps1Cast(Writer->Instruction)->Pair == Op->Pair)
            {
                Writer = Writer->PriorWrite;
            }
            if (Writer != nullptr && D9Ps1Writer(Writer) != nullptr &&
                Writer->Instruction->Dest.Modifier == D9Ps1Saturate)
            {
                Affected |= D9SvComponent(Component);
            }
        }
        if (Affected != 0)
        {
            ComponentText(Affected, TRUE, FALSE, Text);
            Fail(2, 28, "Cannot apply a negation source modifier on data that was last written with the saturate "
                 "destination modifier. Affected components(*) of %s source param: %s", D9Ps1Ordinals[Index], Text);
            SkipSource[Index] = TRUE;
        }
    }
}

VOID
D9Ps1Checker::SatBeforeModifier10()
{
    D9Ps1Instruction* Op = Ins();

    if (!NoDevice())
        return;
    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        DWORD Affected = 0;
        char Text[32];

        if (SkipSource[Index])
            continue;
        if (Source->Modifier != D9Ps1ModBias && Source->Modifier != D9Ps1ModComplement)
            continue;
        for (UINT Component = 0; Component < 4; Component++)
        {
            D9Ps1File* File;
            D9Ps1Instruction* Writer;

            if ((Source->Read & D9SvComponent(Component)) == 0)
                continue;
            if (Source->Type == D9SvRegTemp)
                File = &Temps;
            else if (Source->Type == D9Ps1RegTexture)
                File = &Textures;
            else
                continue;
            Writer = D9Ps1Writer(File->History[Component][Source->Number].LastWrite);
            if (Writer != nullptr && Writer->Pair != Op->Pair && !Writer->TexOp &&
                Writer->Dest.Modifier != D9Ps1Saturate)
            {
                Affected |= D9SvComponent(Component);
            }
        }
        if (Affected == 0)
            continue;
        ComponentText(Affected, TRUE, FALSE, Text);
        if (Source->Modifier == D9Ps1ModBias)
            Report(1, 29, "When using the bias source modifier on a register, the previous writer should apply the "
                   "saturate modifier. This would ensure consistent behaviour across different hardware. Affected "
                   "components(*) of %s source param: %s", D9Ps1Ordinals[Index], Text);
        else
            Report(1, 30, "When using the complement source modifier on a register, the previous writer should apply "
                   "the saturate destination modifier. This would ensure consistent behaviour across different "
                   "hardware. Affected components(*) of %s source param: %s", D9Ps1Ordinals[Index], Text);
    }
}

VOID
D9Ps1Checker::DependentReads10()
{
    D9Ps1Instruction* Op = Ins();

    if (!Op->Dependent)
        return;
    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        D9Ps1Instruction* Writer;

        if (SkipSource[Index])
            continue;
        if ((Source->Read & D9SvComponentX) == 0 || Source->Type != D9Ps1RegTexture)
            continue;
        Writer = D9Ps1Writer(Textures.History[0][Source->Number].LastWrite);
        if (Writer == nullptr || !Writer->TexOp || !Writer->Dependent)
            continue;

        /* A dependent read whose own address came from a dependent read */
        for (UINT Inner = 0; Inner < Writer->SourceCount; Inner++)
        {
            UINT Number = Writer->Source[Inner].Number;
            D9Ps1Instruction* Before;

            if (Writer->Source[Inner].Type != D9Ps1RegTexture)
                continue;
            if (Number >= Textures.Count)
                continue;
            Before = D9Ps1Writer(Textures.History[0][Number].LastWrite);
            if (Before == nullptr || !Before->Dependent)
                continue;
            Fail(2, 35, "Multiple dependent texture reads are disallowed (%s source param).  Texture read results can "
                 "be used as an address for subsequent read, but the results from that read cannot be used as an "
                 "address in yet another subsequent read.", D9Ps1Ordinals[Index]);
            SkipSource[Index] = TRUE;
            break;
        }
    }
}

VOID
D9Ps1Checker::NoLongerAvailable10()
{
    D9Ps1Instruction* Op = Ins();

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        DWORD Hidden = 0;
        DWORD Bumped = 0;
        DWORD BumpRead = 0;
        char Text[32];

        if (SkipSource[Index])
            continue;
        if (Source->Type == D9Ps1RegTexture)
        {
            for (UINT Component = 0; Component < 4; Component++)
            {
                D9Ps1Instruction* Writer;

                if ((Source->Read & D9SvComponent(Component)) == 0)
                    continue;
                Writer = D9Ps1Writer(Textures.History[Component][Source->Number].LastWrite);
                if (Writer == nullptr)
                    continue;
                if (Writer->Opcode == D9Ps1OpTexkill || Writer->Opcode == D9Ps1OpTexm3x2pad ||
                    Writer->Opcode == D9Ps1OpTexm3x3pad || Writer->Opcode == D9Ps1OpTexm3x2depth)
                {
                    Hidden |= D9SvComponent(Component);
                }
            }
        }
        if (Hidden != 0)
        {
            ComponentText(Hidden, TRUE, FALSE, Text);
            Fail(2, 31, "Texture register result of texkill%s or texm*pad instructions must not be read. Affected "
                 "components(*) of %s source param: %s", Version >= D9Ps1Version13 ? ", texm3x2depth" : "",
                 D9Ps1Ordinals[Index], Text);
            SkipSource[Index] = TRUE;
        }

        if (Op->TexOp && Source->Type == D9Ps1RegTexture)
        {
            for (UINT Component = 0; Component < 4; Component++)
            {
                D9Ps1Instruction* Writer;

                if ((Source->Read & D9SvComponent(Component)) == 0)
                    continue;
                Writer = D9Ps1Writer(Textures.History[Component][Source->Number].LastWrite);
                if (Writer != nullptr && (Writer->Opcode == D9Ps1OpTexbem || Writer->Opcode == D9Ps1OpTexbeml))
                    Bumped |= D9SvComponent(Component);
            }
        }
        if (Bumped != 0)
        {
            ComponentText(Bumped, TRUE, FALSE, Text);
            Fail(2, 32, "Texture register result of texbem or texbeml instruction must not be read by tex* "
                 "instruction. Affected components(*) of %s source param: %s", D9Ps1Ordinals[Index], Text);
            SkipSource[Index] = TRUE;
        }

        if (Op->Opcode == D9Ps1OpTexbem || Op->Opcode == D9Ps1OpTexbeml)
            continue;
        if (Source->Type != D9Ps1RegTexture)
            continue;
        /* The reference gives up on the source at the first component it does not read */
        for (UINT Component = 0; Component < 4; Component++)
        {
            D9Ps1Access* Last;

            if ((Source->Read & D9SvComponent(Component)) == 0)
            {
                BumpRead = 0;
                break;
            }
            Last = Textures.History[Component][Source->Number].Newest;
            if (Last != nullptr && Last->Instruction != nullptr && Last->Read &&
                (Last->Instruction->Opcode == D9Ps1OpTexbem || Last->Instruction->Opcode == D9Ps1OpTexbeml))
            {
                BumpRead |= D9SvComponent(Component);
            }
        }
        if (BumpRead != 0)
        {
            Fail(2, 34, "Register data that has been read by a texbem or texbeml instruction cannot be read later, "
                 "except by another texbem/l.");
            SkipSource[Index] = TRUE;
        }
    }
}

VOID
D9Ps1Checker::SourceInitialized10()
{
    D9Ps1Instruction* Op = Ins();

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        D9Ps1Access* SamePair[4] = {};
        PCSTR Prefix;
        D9Ps1File* File;

        if (SkipSource[Index])
            continue;
        switch (Source->Type)
        {
        case D9SvRegTemp:
            Prefix = "r";
            break;
        case D9SvRegInput:
            Prefix = "v";
            break;
        case D9SvRegConst:
            Prefix = "c";
            break;
        case D9Ps1RegTexture:
            Prefix = "t";
            break;
        default:
            continue;
        }
        File = FileOf(Source->Type);
        if (Source->Type == D9SvRegTemp || Source->Type == D9Ps1RegTexture)
        {
            DWORD Missing = 0;
            UINT MissingCount = 0;
            char Text[32];

            for (UINT Component = 0; Component < 4; Component++)
            {
                D9Ps1Access* Writer;

                if ((Source->Read & D9SvComponent(Component)) == 0)
                    continue;

                /* Writes from this co-issued pair do not count yet */
                Writer = File->History[Component][Source->Number].LastWrite;
                while (Writer != nullptr && D9Ps1Cast(Writer->Instruction)->Pair == Op->Pair)
                {
                    SamePair[Component] = Writer;
                    Writer = Writer->PriorWrite;
                }
                if (Writer == nullptr)
                {
                    MissingCount += 1;
                    Missing |= D9SvComponent(Component);
                }
            }
            if (MissingCount != 0)
            {
                ComponentText(Missing, TRUE, FALSE, Text);
                Fail(2, 36, "Read of uninitialized component%s(*) in %s%d: %s", MissingCount > 1 ? "s" : "", Prefix,
                     Source->Number, Text);
            }
        }
        RecordReads(File, Source->Number, Source->Read, SamePair);
    }
}

VOID
D9Ps1Checker::DestParam10()
{
    D9Ps1Instruction* Op = Ins();
    D9SvDest* Dest = &Op->Dest;
    D9Ps1File* File = nullptr;
    BOOLEAN Bad = FALSE;
    BOOLEAN CanWrite = FALSE;

    if (Op->Opcode == D9SvOpDef || !Dest->Present)
        return;
    if (Dest->Type == D9SvRegTemp)
    {
        File = &Temps;
        CanWrite = Temps.Writable;
    }
    else if (Dest->Type == D9Ps1RegTexture)
    {
        File = &Textures;
        CanWrite = Op->TexOp || Textures.Writable;
    }
    if (File == nullptr || !CanWrite || File->Count == 0)
    {
        Fail(2, 37, "Invalid reg type for dest param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 38, "Invalid dest reg num: %d. Max allowed for this reg type is %d.", Dest->Number, File->Count - 1);
        Bad = TRUE;
    }
    else if (Dest->Type == D9Ps1RegTexture)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            D9Ps1Instruction* Writer = D9Ps1Writer(Textures.History[Component][Dest->Number].LastWrite);

            if (Writer != nullptr && Writer->Opcode == D9Ps1OpTexm3x2depth)
            {
                Fail(2, 39, "Destination of texm3x2depth instruction (t%d) is not available elsewhere in shader.",
                     Dest->Number);
                return;
            }
        }
    }

    if (Op->TexOp)
    {
        if (Dest->Type != D9Ps1RegTexture)
        {
            Fail(2, 40, "Must use texture register a dest param for tex* instructions.");
            Bad = TRUE;
        }
        if (Dest->Mask != D9SvMaskAll)
        {
            Fail(2, 41, "tex* instructions must write all components.");
            Bad = TRUE;
        }
        if (Dest->Modifier != 0)
        {
            Fail(2, 42, "Instruction modifiers are not allowed for tex* instructions.");
            Bad = TRUE;
        }
        if (Dest->Shift != 0)
        {
            Fail(2, 43, "Dest shift not allowed for tex* instructions.");
            Bad = TRUE;
        }
    }
    else
    {
        if (Dest->Modifier != 0 && Dest->Modifier != D9Ps1Saturate)
        {
            Fail(2, 44, "Invalid dst modifier.");
            Bad = TRUE;
        }
        if (Dest->Shift != 0 && Dest->Shift != 1 && Dest->Shift != 2 && Dest->Shift != 15)
        {
            Fail(2, 45, "Invalid dst shift.");
            Bad = TRUE;
        }
    }

    if (Dest->Mask != D9Ps1Color && Dest->Mask != D9Ps1Alpha && Dest->Mask != D9SvMaskAll)
    {
        Fail(2, 46, "Dest write mask must be .rgb, .a, or .rgba (all).");
        return;
    }
    if (Bad)
        return;
    for (UINT Component = 0; Component < 4; Component++)
    {
        if ((Dest->Mask & D9SvComponent(Component)) != 0)
            Record(&File->History[Component][Dest->Number], Current, TRUE);
    }
}

/* Distinct register numbers read per register file, alone and over a co-issued pair */
VOID
D9Ps1Checker::Ports10()
{
    D9Ps1Instruction* Op = Ins();
    D9Ps1Instruction* Before = D9Ps1Cast(Op->Previous);
    UINT Distinct[4][4];
    UINT Counts[4] = {};

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        UINT Type = Op->Source[Index].Type;
        UINT Number = Op->Source[Index].Number;
        BOOLEAN Seen = FALSE;

        if (Type > D9Ps1RegTexture)
            continue;
        for (UINT Slot = 0; Slot < Counts[Type]; Slot++)
        {
            if (Distinct[Type][Slot] == Number)
                Seen = TRUE;
        }
        if (!Seen)
        {
            Distinct[Type][Counts[Type]] = Number;
            Counts[Type] += 1;
        }
    }
    if (Counts[0] > Temps.Ports)
        Fail(2, 47, "%d different temp registers (r#) read by instruction.  Max. different temp registers "
             "readable per instruction is %d.", Counts[0], Temps.Ports);
    if (Counts[1] > Inputs.Ports)
        Fail(2, 48, "%d different input registers (v#) read by instruction.  Max. different input registers "
             "readable per instruction is %d.", Counts[1], Inputs.Ports);
    if (Counts[2] > Consts.Ports)
        Fail(2, 49, "%d different constant registers (c#) read by instruction.  Max. different constant "
             "registers readable per instruction is %d.", Counts[2], Consts.Ports);
    if (Counts[3] > Textures.Ports)
        Fail(2, 50, "%d different texture registers (t#) read by instruction.  Max. different texture "
             "registers readable per instruction is %d.", Counts[3], Textures.Ports);

    if (Op->CoIssue && Before != nullptr && !Before->CoIssue)
    {
        /* Add this instruction's numbers to the ones its partner read */
        for (UINT Index = 0; Index < Op->SourceCount; Index++)
        {
            UINT Type = Op->Source[Index].Type;
            UINT Number = Op->Source[Index].Number;
            SIZE_T ListOffset;
            SIZE_T CountOffset;
            DWORD Count;
            BOOLEAN Seen = FALSE;

            if (Type > D9Ps1RegTexture)
                continue;
            ListOffset = Type * 4 * sizeof(DWORD);
            CountOffset = FIELD_OFFSET(D9Ps10State, PairCounts) + Type * sizeof(DWORD);
            Count = D9Ps1PeekDword(&Old, CountOffset);
            for (DWORD Slot = 0; Slot < Count; Slot++)
            {
                if (D9Ps1PeekDword(&Old, ListOffset + Slot * sizeof(DWORD)) == Number)
                {
                    Seen = TRUE;
                    break;
                }
            }
            if (Seen)
                continue;
            D9Ps1PokeDword(&Old, ListOffset + Count * sizeof(DWORD), Number);
            D9Ps1PokeDword(&Old, CountOffset, D9Ps1PeekDword(&Old, CountOffset) + 1);
        }
        if (Old.PairCounts[0] > 3)
            Fail(2, 51, "%d different temp registers (r#) read over 2 co-issued instructions. Max. different "
                 "register numbers from any one register type readable across co-issued instructions is %d.",
                 Old.PairCounts[0], 3);
        if (Old.PairCounts[1] > 3)
            Fail(2, 399, "%d different input registers (v#) read over 2 co-issued instructions. Max. different "
                 "register numbers from any one register type readable across co-issued instructions is %d.",
                 Old.PairCounts[1], 3);
        if (Old.PairCounts[2] > 3)
            Fail(2, 400, "%d different constant registers (c#) read over 2 co-issued instructions. Max. different "
                 "register numbers from any one register type readable across co-issued instructions is %d.",
                 Old.PairCounts[2], 3);
        if (Old.PairCounts[3] > 3)
            Fail(2, 52, "%d different texture registers (t#) read over 2 co-issued instructions. Max. different "
                 "register numbers from any one register type readable across co-issued instructions is %d.",
                 Old.PairCounts[3], 3);
    }

    if (Op->CoIssue)
    {
        ZeroMemory(Old.PairCounts, sizeof(Old.PairCounts));
        return;
    }
    for (UINT Type = 0; Type < 4; Type++)
    {
        for (UINT Slot = 0; Slot < Counts[Type]; Slot++)
            Old.PairRegs[Type * 4 + Slot] = Distinct[Type][Slot];
        Old.PairCounts[Type] = Counts[Type];
    }
}

VOID
D9Ps1Checker::TexDeclaredInOrder10()
{
    D9Ps1Instruction* Op = Ins();
    DWORD Bit;

    if (!Op->TexOp || Op->Dest.Type != D9Ps1RegTexture || Op->Dest.Number > Textures.Count)
        return;
    Bit = 1u << Op->Dest.Number;
    if ((Old.TexDeclared & Bit) != 0)
        Fail(2, 53, "Tex register t%d already declared.", Op->Dest.Number);
    else if (Old.TexDeclared > Bit)
        Fail(2, 54, "t# registers must appear in sequence (i.e. t0 before t2 OK, but t1 before t0 not valid).");
    Old.TexDeclared |= Bit;
}

VOID
D9Ps1Checker::TexAfterArithmetic10()
{
    D9Ps1Instruction* Op = Ins();

    if (Old.OrderReported)
        return;
    if (Op->TexOp)
    {
        if (Old.SawArith)
        {
            Fail(2, 55, "Cannot use tex* instruction after non-tex* instruction.");
            Old.OrderReported = 1;
        }
    }
    else if (Op->Opcode != D9SvOpDef && Op->Opcode != D9SvOpNop)
    {
        Old.SawArith = 1;
    }
}

/* texm3x2 and texm3x3 come as pad instructions followed by the one that finishes the matrix */
VOID
D9Ps1Checker::TexmSequence10()
{
    D9Ps1Instruction* Op = Ins();
    BOOLEAN Broken = FALSE;

    if (Op == nullptr)
        return;
    if (Ended)
    {
        if (Old.TexmState != 0)
            Fail(2, 56, "Incomplete texm* sequence.");
        return;
    }
    if (Op->TexMatrix)
    {
        switch (Op->Opcode)
        {
        case D9Ps1OpTexm3x2pad:
            if (Old.TexmState != 0)
            {
                Broken = TRUE;
                break;
            }
            TexmBase = Op->Dest.Number;
            Old.TexmState = 1;
            break;
        case D9Ps1OpTexm3x3pad:
            if (Old.TexmState == 0)
            {
                TexmBase = Op->Dest.Number;
                Old.TexmState = 1;
            }
            else if (Old.TexmState == 1 && Old.PrevOpcode == D9Ps1OpTexm3x3pad)
            {
                Old.TexmState = 2;
            }
            else
            {
                Broken = TRUE;
            }
            break;
        case D9Ps1OpTexm3x2tex:
        case D9Ps1OpTexm3x2depth:
            if (Old.TexmState == 1 && Old.PrevOpcode == D9Ps1OpTexm3x2pad)
                Old.TexmState = 0;
            else
                Broken = TRUE;
            break;
        case D9Ps1OpTexm3x3tex:
        case D9Ps1OpTexm3x3spec:
        case D9Ps1OpTexm3x3vspec:
        case D9Ps1OpTexm3x3:
            if (Old.TexmState == 2 && Old.PrevOpcode == D9Ps1OpTexm3x3pad)
                Old.TexmState = 0;
            else
                Broken = TRUE;
            break;
        default:
            break;
        }
        if (Broken)
            Fail(2, 57, "Invalid texm* sequence.");
    }
    Old.PrevOpcode = Op->Opcode;
}

/* Each step of a texm* matrix writes the next register and reads the same source */
VOID
D9Ps1Checker::TexmRegisters10()
{
    D9Ps1Instruction* Op = Ins();
    D9Ps1Instruction* Before = D9Ps1Cast(Op->Previous);
    UINT Expected;
    UINT PriorSource;

    if (!Op->TexMatrix)
        return;
    switch (Op->Opcode)
    {
    case D9Ps1OpTexm3x3pad:
        if (Before == nullptr || Before->Opcode != D9Ps1OpTexm3x3pad)
            return;
        if (Op->Dest.Number != TexmBase + 1)
        {
            Fail(2, 58, "Invalid texm* register.");
            return;
        }
        if (Op->Source[0].Number != Before->Source[0].Number)
            Fail(2, 58, "Invalid texm* register.");
        return;
    case D9Ps1OpTexm3x2tex:
    case D9Ps1OpTexm3x2depth:
        Expected = TexmBase + 1;
        break;
    case D9Ps1OpTexm3x3spec:
        if (Op->Source[1].Type != D9SvRegConst)
        {
            Fail(2, 58, "Invalid texm* register.");
            return;
        }
        Expected = TexmBase + 2;
        break;
    case D9Ps1OpTexm3x3tex:
    case D9Ps1OpTexm3x3vspec:
    case D9Ps1OpTexm3x3:
        Expected = TexmBase + 2;
        break;
    default:
        return;
    }
    PriorSource = Before != nullptr ? Before->Source[0].Number : MAXUINT;
    if (Op->Dest.Number != Expected || Op->Source[0].Number != PriorSource)
        Fail(2, 58, "Invalid texm* register.");
}

VOID
D9Ps1Checker::Lrp10()
{
    D9Ps1Instruction* Op = Ins();
    D9SvSource* Blend = &Op->Source[0];
    DWORD Affected = 0;
    char Text[32];

    if (Op->Opcode != D9SvOpLrp || !Blend->Present)
        return;
    if (Blend->Modifier != D9Ps1ModComplement && Blend->Modifier != 0)
        Fail(2, 61, "The only valid modifiers for the first source parameter of lrp are: reg (no mod) or 1-reg "
             "(complement).");
    if (!NoDevice() || SkipSource[0])
        return;
    for (UINT Component = 0; Component < 4; Component++)
    {
        D9Ps1File* File;
        D9Ps1Access* Writer;
        D9Ps1Instruction* By;

        if ((Blend->Read & D9SvComponent(Component)) == 0)
            continue;
        if (Blend->Type == D9SvRegTemp)
            File = &Temps;
        else if (Blend->Type == D9Ps1RegTexture)
            File = &Textures;
        else
            continue;
        Writer = File->History[Component][Blend->Number].LastWrite;
        if (Writer == nullptr)
            continue;
        if (Writer->Instruction != nullptr && Writer->Instruction == Current)
        {
            Writer = Writer->PriorWrite;
            if (Writer == nullptr)
                continue;
        }
        By = D9Ps1Writer(Writer);
        if (By != nullptr && !By->TexOp && By->Dest.Modifier != D9Ps1Saturate)
            Affected |= D9SvComponent(Component);
    }
    if (Affected == 0)
        return;
    ComponentText(Affected, TRUE, FALSE, Text);
    Report(1, 62, "Previous writer to the first source register of lrp instruction should apply the saturate "
           "destination modifier.  This ensures consistent behaviour across different hardware. Affected "
           "components(*) of first source register: %s", Text);
}

VOID
D9Ps1Checker::Definition10()
{
    D9Ps1Instruction* Op = Ins();
    D9SvDest* Dest = &Op->Dest;
    BOOLEAN Good = TRUE;

    if (Op->Opcode != D9SvOpDef)
    {
        Old.DefOpen = 0;
        return;
    }
    if (!Old.DefOpen)
    {
        Fail(2, 63, "Const declaration (def) must appear before other instructions.");
        Good = FALSE;
    }
    if (Dest->Mask != D9SvMaskAll || Dest->Modifier != 0 || Dest->Shift != 0 || Dest->Type != D9SvRegConst)
    {
        Fail(2, 64, "Destination for def instruction must be of the form c# (where # = reg number). In addition "
             "there may be no mask (equivalent to full .xyzw, which is fine).");
        Good = FALSE;
    }
    if (Dest->Type == D9SvRegConst && Dest->Number >= Consts.Count)
    {
        Fail(2, 65, "Invalid const register num: %d. Max allowed is %d.", Dest->Number, Consts.Count - 1);
        return;
    }
    if (!Good)
        return;
    if (Consts.Defined[Dest->Number])
        Fail(2, 224, "c%d already def'd in the shader. ", Dest->Number);
    Consts.Defined[Dest->Number] = 1;
}

VOID
D9Ps1Checker::Pairing10()
{
    D9Ps1Instruction* Op = Ins();
    D9Ps1Instruction* Before = D9Ps1Cast(Op->Previous);
    DWORD Mine = 0;
    DWORD Theirs = 0;

    if (!Old.ArithSeen && !Op->TexOp)
    {
        if (Op->CoIssue)
            Fail(2, 68, "First arithmetic instruction cannot have co-issue ('+') set; there is no previous "
                 "arithmetic instruction to pair with.");
        Old.ArithSeen = 1;
    }
    if (Op->TexOp && Op->CoIssue)
        Fail(2, 69, "Cannot set co-issue ('+') on a texture instruction.  Co-issue only applies to arithmetic "
             "instructions.");
    if (!Op->CoIssue)
        return;
    if (Before != nullptr && Before->CoIssue)
        Fail(2, 70, "Cannot set co-issue ('+') on consecutive instructions.");
    if (Before != nullptr && Before->Opcode == D9SvOpNop)
        Fail(2, 71, "(previous instruction) nop instruction cannot be co-issued.");
    if (Op->Opcode == D9SvOpNop)
        Fail(2, 72, "nop instruction cannot be co-issued.");
    if (Before != nullptr && Before->Opcode == D9SvOpDp4)
        Fail(2, 73, "(previous instruction) dp4 instruction cannot be co-issued.");
    if (Op->Opcode == D9SvOpDp4)
        Fail(2, 74, "dp4 instruction cannot be co-issued.");
    if (Op->TexOp || Before == nullptr || Before->TexOp || Before->CoIssue)
        return;

    if (Op->Dest.Present)
        Mine = Op->Dest.Mask;
    if (Before->Dest.Present)
        Theirs = Before->Dest.Mask;
    if (Op->Opcode == D9SvOpDp3)
    {
        if (Before->Opcode == D9SvOpDp3)
        {
            Fail(2, 75, "Co-issued instructions cannot both be dot-product, since each require use of the "
                 "color/vector pipeline to execute.");
            return;
        }
        if ((Theirs & D9Ps1Color) != 0)
            Fail(2, 76, "(previous instruction) Dot-product needs color/vector pipeline to execute, so instruction "
                 "co-issued with it cannot write to color components.");
        if ((Mine & D9Ps1Alpha) != 0)
            Fail(2, 77, "Dot-product which writes alpha cannot co-issue, because both alpha and color/vector "
                 "pipelines used.");
        return;
    }
    if (Before->Opcode == D9SvOpDp3)
    {
        if ((Mine & D9Ps1Color) != 0)
            Fail(2, 78, "Dot-product needs color/vector pipeline to execute, so instruction co-issued with it cannot "
                 "write to color components.");
        if ((Theirs & D9Ps1Alpha) != 0)
            Fail(2, 79, "(previous instruction) Dot-product which writes alpha cannot co-issue, because both alpha "
                 "and color/vector pipelines used by the dot product.");
        return;
    }
    if (Theirs == D9SvMaskAll)
        Fail(2, 80, "(previous instruction) Co-issued instruction cannot write all components - must write either "
             "alpha or color.");
    if (Mine == D9SvMaskAll)
        Fail(2, 81, "Co-issued instruction cannot write all components - must write either alpha or color.");
    if (Op->Dest.Type == Before->Dest.Type && Op->Dest.Number == Before->Dest.Number && (Mine & Theirs) != 0)
    {
        char Text[32];

        ComponentText(Mine & Theirs, TRUE, FALSE, Text);
        Fail(2, 82, "Co-issued instructions cannot both write to the same components of a register.  Affected "
             "components: %s", Text);
    }
    if ((Mine & D9Ps1Color) != 0 && (Theirs & D9Ps1Color) != 0)
        Fail(2, 83, "Co-issued instructions cannot both write to color components.");
    if ((Mine & D9Ps1Alpha) != 0 && (Theirs & D9Ps1Alpha) != 0)
        Fail(2, 84, "Co-issued instructions cannot both write to alpha component.");
}

VOID
D9Ps1Checker::CountSlots10()
{
    D9Ps1Instruction* Op = Ins();

    if (Op == nullptr)
        return;
    if (!Ended)
    {
        switch (Op->Opcode)
        {
        case 1: case 2: case 3: case 4: case 5: case 8: case 9: case 18: case 80: case 88:
            if (!Op->CoIssue)
            {
                Old.ArithSlots += 1;
                Old.TotalSlots += 1;
                if (Op->Opcode == D9SvOpDp4 || Op->Opcode == D9Ps1OpCmp)
                    Old.ArithSlots += 1;
            }
            else if (Op->Opcode == D9Ps1OpCmp && Op->Previous != nullptr && Op->Previous->Opcode != D9Ps1OpCmp)
            {
                Old.ArithSlots += 1;
            }
            break;
        case 68:
            Old.ArithSlots += 1;
            Old.TotalSlots += 1;
            Old.TexSlots += 1;
            Old.TotalSlots += 1;
            break;
        case 64: case 65: case 66: case 67: case 69: case 70: case 71: case 72: case 73: case 74: case 76: case 77:
        case 82: case 83: case 84: case 85: case 86:
            Old.TexSlots += 1;
            Old.TotalSlots += 1;
            break;
        default:
            break;
        }
        return;
    }
    if (Old.TexSlots > Old.TexLimit)
        Fail(6, 87, "Too many texture addressing instruction slots used: %d. Max. allowed is %d. (Note that some "
             "texture addressing instructions may use up more than one instruction slot)", Old.TexSlots, Old.TexLimit);
    if (Old.ArithSlots > Old.ArithLimit)
        Fail(6, 88, "Too many arithmetic instruction slots used: %d. Max. allowed (counting any co-issued pairs as 1) "
             "is %d.", Old.ArithSlots, Old.ArithLimit);
    if ((Old.TexSlots <= Old.TexLimit || Old.ArithSlots <= Old.ArithLimit) && Old.TotalSlots > Old.TotalLimit)
        Fail(6, 89, "Total number of instruction slots used too high: %d. Max. allowed (counting any co-issued pairs "
             "as 1) is %d.", Old.TotalSlots, Old.TotalLimit);
}

/* The highest texture stage the shader samples; any error clears it */
VOID
D9Ps1Checker::LogTextures10()
{
    D9Ps1Instruction* Op = Ins();

    if (Ended)
    {
        if (Errors != 0)
            Params->Outputs = MAXDWORD;
        return;
    }
    if (Errors != 0)
        return;
    switch (Op->Opcode)
    {
    case 64: case 65: case 66: case 67: case 68: case 71: case 72: case 73: case 74: case 76: case 77: case 83:
    case 84: case 85: case 86:
        if (static_cast<INT>(Params->Outputs) <= static_cast<INT>(Op->Dest.Number))
            Params->Outputs = Op->Dest.Number;
        break;
    default:
        break;
    }
}

VOID
D9Ps1Checker::R0Written10()
{
    DWORD Missing = 0;
    UINT MissingCount = 0;
    char Text[32];

    for (UINT Component = 0; Component < 4; Component++)
    {
        if (Temps.History[Component][0].LastWrite == nullptr)
        {
            MissingCount += 1;
            Missing |= D9SvComponent(Component);
        }
    }
    if (MissingCount == 0)
        return;
    ComponentText(Missing, TRUE, FALSE, Text);
    Fail(6, 90, "r0 must be written by shader. Uninitialized component%s(*): %s", MissingCount > 1 ? "s" : "",
         Text);
}

/* ps_1_4 rules */

BOOLEAN
D9Ps1Checker::Supported14()
{
    char Name[32];

    switch (Current->Opcode)
    {
    case 1: case 2: case 3: case 4: case 5: case 8: case 9: case 18: case 64: case 65: case 66: case 80: case 81:
    case 87: case 88: case 89: case D9SvOpPhase: case D9SvOpEnd: case D9SvOpNop:
        return TRUE;
    default:
        break;
    }
    D9SvVersionName(Version, Name);
    Fail(2, 95, "Instruction not supported by version %s shader.", Name);
    return FALSE;
}

BOOLEAN
D9Ps1Checker::ParamCount14()
{
    UINT Dests = Current->DestCount;
    UINT Sources = Current->SourceCount;
    BOOLEAN Bad;

    switch (Current->Opcode)
    {
    case 0:
    case D9SvOpPhase:
        Bad = Dests != 0 || Sources != 0;
        break;
    case 1: case 64: case 66:
        Bad = Dests != 1 || Sources != 1;
        break;
    case 2: case 3: case 5: case 8: case 9: case 89:
        Bad = Dests != 1 || Sources != 2;
        break;
    case 4: case 18: case 80: case 88:
        Bad = Dests != 1 || Sources != 3;
        break;
    case 65: case 81: case 87:
        Bad = Dests != 1 || Sources != 0;
        break;
    default:
        Bad = Dests + Sources > 5;
        break;
    }
    if (!Bad)
        return TRUE;
    Fail(2, 96, "Invalid parameter count.");
    return FALSE;
}

/* The phase marker: once only, and it drops alpha from every temp register written before it */
BOOLEAN
D9Ps1Checker::Marker14()
{
    if (Current->Opcode != D9SvOpPhase)
        return TRUE;
    if (New.PhaseSeen)
    {
        Fail(2, 142, "Multiple phase markers not permitted.  Aborting shader validation.");
        return FALSE;
    }
    if (DzBeforePhase)
        Fail(2, 143, "Earlier texld instruction used _dz(=_db) modifier on source param. When a phase marker is "
             "present, the _dz modifier is only permitted after the phase marker.");
    if (InputBeforePhase)
        Fail(2, 144, "v# register read by instruction(s) before phase marker.  This is not permitted. When a phase "
             "marker is present, v# register reads are only permitted after the phase marker.");
    New.PhaseSeen = 1;
    Phase = Current;
    for (UINT Number = 0; Number < Temps.Count; Number++)
    {
        D9Ps1History* Alpha = &Temps.History[3][Number];

        if (Alpha->LastWrite != nullptr)
        {
            ZeroMemory(Alpha, sizeof(*Alpha));
            AlphaLost |= 1u << Number;
        }
    }
    return TRUE;
}

VOID
D9Ps1Checker::SourceParams14()
{
    D9Ps1Instruction* Op = Ins();
    char Which[32] = "";

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        DWORD Swizzle = Source->Swizzle;
        BOOLEAN Bad = FALSE;
        D9Ps1File* File;

        if (Index == 0)
            strcpy_s(Which, Op->SourceCount == 1 ? "(Source param) " : "(First source param) ");
        else if (Index == 1)
            strcpy_s(Which, "(Second source param) ");
        else if (Index == 2)
            strcpy_s(Which, "(Third source param) ");

        if (Source->Type == D9SvRegConst && Source->Modifier != 0)
        {
            Fail(2, 97, "%sModifiers are not allowed on constants for ps_1_4.", Which);
            SkipSource[Index] = TRUE;
            continue;
        }

        if (!Op->TexOp)
        {
            switch (Source->Type)
            {
            case D9SvRegTemp:
                File = &Temps;
                break;
            case D9SvRegInput:
                if (Phase == nullptr)
                    InputBeforePhase = TRUE;
                File = &Inputs;
                break;
            case D9SvRegConst:
                File = &Consts;
                break;
            default:
                File = nullptr;
                break;
            }
            if (File == nullptr)
            {
                if (Source->Type == D9Ps1RegTexture)
                    Fail(2, 112, "%sTexture coordinate registers (t#) are not available to arithmetic instructions.",
                         Which);
                else
                    Fail(2, 113, "%sInvalid register type.", Which);
                Bad = TRUE;
            }
            else if (Source->Number >= File->Count)
            {
                Fail(2, 114, "%sInvalid register number: %d. Max allowed for this type is %d.", Which, Source->Number,
                     File->Count - 1);
                Bad = TRUE;
            }
            switch (Source->Modifier)
            {
            case 0:
            case D9SvModNegate:
            case D9Ps1ModBias:
            case D9Ps1ModBiasNegate:
            case D9Ps1ModSign:
            case D9Ps1ModSignNegate:
            case D9Ps1ModComplement:
            case D9Ps1ModTimes2:
            case D9Ps1ModTimes2Negate:
                break;
            default:
                Fail(2, 115, "%sInvalid source modifier.", Which);
                Bad = TRUE;
                break;
            }
            if (Swizzle != D9SvSwizzleX && Swizzle != D9SvSwizzleY && Swizzle != D9SvSwizzleZ &&
                Swizzle != D9SvSwizzleW && Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 116, "%sInvalid source selector.", Which);
                SkipSource[Index] = TRUE;
                continue;
            }
            if (Bad)
                SkipSource[Index] = TRUE;
            continue;
        }

        if (Op->Opcode == D9Ps1OpTexcoord)
        {
            if (Source->Type != D9Ps1RegTexture)
            {
                Fail(2, 98, "%sSource register type must be texture coordinate input (t#) for texcrd instruction.",
                     Which);
                Bad = TRUE;
            }
            File = &Textures;
        }
        else if (Source->Type == D9SvRegTemp)
        {
            File = &Temps;
        }
        else if (Source->Type == D9Ps1RegTexture)
        {
            File = &Textures;
        }
        else
        {
            Fail(2, 99, "%sSource register type must be temp (r#) or texture coordinate input (t#) for tex* "
                 "instruction.", Which);
            SkipSource[Index] = TRUE;
            continue;
        }
        if (Source->Number >= File->Count)
        {
            Fail(2, 100, "%sInvalid register number: %d.  Max allowed for this type is %d.", Which, Source->Number,
                 File->Count - 1);
            Bad = TRUE;
        }

        if (Source->Modifier == D9Ps1ModDivideZ && Op->Opcode == D9Ps1OpTexcoord)
        {
            Fail(2, 103, "_dz(=_db) modifier cannot be used on source parameter for texcrd. It is only available to "
                 "texld instruction, when source parameter is temp register (r#).");
            Bad = TRUE;
        }
        else if (Source->Modifier == D9Ps1ModDivideZ && Op->Opcode == D9Ps1OpTex)
        {
            if (Source->Type != D9SvRegTemp)
            {
                Fail(2, 101, "_dz(=_db) modifier on source param for texld only allowed if source is a temp register "
                     "(r#).");
                Bad = TRUE;
            }
            if (Phase == nullptr)
                DzBeforePhase = TRUE;
            if (Swizzle != D9Ps1SwizzleXyz && Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 102, "_dz(=_db) modifier on source param for texld must be paired with source selector "
                     ".xyz(=.rgb). Note: Using no selector is treated same as .xyz here.");
                Bad = TRUE;
            }
        }
        else if (Source->Modifier == D9Ps1ModDivideW &&
                 (Op->Opcode == D9Ps1OpTexcoord || Op->Opcode == D9Ps1OpTex))
        {
            if (Op->Opcode == D9Ps1OpTex && Source->Type != D9Ps1RegTexture)
            {
                Fail(2, 104, "_dw(=_da) modifier on source param for texld only allowed if source is a texture "
                     "coordinate register (t#).");
                Bad = TRUE;
            }
            if (Swizzle != D9Ps1SwizzleXyw)
            {
                Fail(2, 105, "_dw(=_da) modifier on source param must be paired with source selector .xyw(=.rga).");
                Bad = TRUE;
            }
        }
        else if (Source->Modifier != 0)
        {
            Fail(2, 106, "%sInvalid source modifier for tex* instruction.", Which);
            Bad = TRUE;
        }

        {
            BOOLEAN CoordSelector = Swizzle == D9Ps1SwizzleXyz || Swizzle == D9SvSwizzleIdentity ||
                                    Swizzle == D9Ps1SwizzleXyw;
            BOOLEAN TempSelector = Swizzle == D9Ps1SwizzleXyz || Swizzle == D9SvSwizzleIdentity;

            if (Op->Opcode == D9Ps1OpTexcoord)
            {
                if (!CoordSelector)
                {
                    Fail(2, 107, "Source for texcrd requires component selector .xyw(==.rga), or .xyz(==.rgb). Note: "
                         "Using no selector is treated same as .xyz here.");
                    Bad = TRUE;
                }
            }
            else if (Op->Opcode == D9Ps1OpTex)
            {
                if (Source->Type == D9Ps1RegTexture && !CoordSelector)
                {
                    Fail(2, 108, "Using a texture coordinate register (t#) as source for texld requires component "
                         "selector .xyw(=.rga), or .xyz(=.rgb). Note: Using no selector is treated same as .xyz "
                         "here.");
                    Bad = TRUE;
                }
                else if (Source->Type == D9SvRegTemp && !TempSelector)
                {
                    Fail(2, 109, "Using a temp register (r#) as source for texld requires component selector "
                         ".xyz(==.rgb). Note: Using no selector is treated same as .xyz here.");
                    Bad = TRUE;
                }
            }
            else if (Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 110, "%sInvalid source selector for tex* instruction.", Which);
                Bad = TRUE;
            }
        }

        /* Every read of one coordinate register picks the same components; the table is not bounds checked */
        if ((Op->Opcode == D9Ps1OpTexcoord || Op->Opcode == D9Ps1OpTex) && Source->Type == D9Ps1RegTexture)
        {
            SIZE_T SeenOffset = FIELD_OFFSET(D9Ps14State, CoordSeen) + Source->Number;
            SIZE_T SwizzleOffset = static_cast<SIZE_T>(Source->Number) * sizeof(DWORD);

            if (D9Ps1PeekByte(&New, SeenOffset) != 0 &&
                (Swizzle & 0x3F0000) != (D9Ps1PeekDword(&New, SwizzleOffset) & 0x3F0000))
            {
                Fail(2, 111, "Texture coordinate register t%d read more than once in shader with different source "
                     "selector (swizzle). Multiple reads of identical texture coordinate register throughout shader "
                     "must all use identical source selector. Note this does not restrict mixing use and non-use of a "
                     "source modifier (i.e. _dw/_da or _dz/_db, depending what the swizzle allows) on these "
                     "coordinate register reads.", Source->Number);
                Bad = TRUE;
            }
            D9Ps1PokeByte(&New, SeenOffset, 1);
            D9Ps1PokeDword(&New, SwizzleOffset, Swizzle);
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

/* Within one phase a tex* instruction may not read what another tex* instruction of that phase wrote */
VOID
D9Ps1Checker::DependentReads14()
{
    D9Ps1Instruction* Op = Ins();
    BOOLEAN FromDest = Op->Dest.Coordinates != 0;
    UINT Reads = FromDest ? 1 : Op->SourceCount;

    if (!Op->TexOp || Op->Dest.Type != D9SvRegTemp)
        return;
    for (UINT Index = 0; Index < Reads; Index++)
    {
        UINT Number;
        UINT Type;
        D9Ps1File* File;
        char Letter;

        if (FromDest)
        {
            Number = Op->Dest.Number;
            Type = Op->Dest.Type;
        }
        else
        {
            if (SkipSource[Index])
                continue;
            Number = Op->Source[Index].Number;
            Type = Op->Source[Index].Type;
        }
        if (Type == D9SvRegTemp)
        {
            File = &Temps;
            Letter = 'r';
        }
        else if (Type == D9Ps1RegTexture)
        {
            File = &Textures;
            Letter = 't';
        }
        else
        {
            continue;
        }
        if (Number >= File->Count)
            continue;
        for (UINT Component = 0; Component < 3; Component++)
        {
            D9Ps1Instruction* Writer = D9Ps1Writer(File->History[Component][Number].LastWrite);

            if (Writer == nullptr || !Writer->TexOp)
                continue;
            if (Phase != nullptr && Writer->Pair <= D9Ps1Cast(Phase)->Pair)
                continue;
            Fail(2, 122, "The current tex* instruction reads from %c%d, which was written earlier by another tex* "
                 "instruction in the same block of tex* instructions.  Dependent reads are not permitted within a "
                 "single block of tex* instructions.  To perform a dependent read, separate texture coordinate "
                 "derivation from the tex* instruction using the coordinates with a 'phase' marker.", Letter, Number);
            return;
        }
    }
}

VOID
D9Ps1Checker::SatBeforeComplement14()
{
    D9Ps1Instruction* Op = Ins();

    if (!NoDevice())
        return;
    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        D9SvSource* Source = &Op->Source[Index];
        DWORD Affected = 0;
        char Text[32];

        if (SkipSource[Index] || Source->Modifier != D9Ps1ModComplement)
            continue;
        for (UINT Component = 0; Component < 4; Component++)
        {
            D9Ps1Instruction* Writer;

            if ((Source->Read & D9SvComponent(Component)) == 0 || Source->Type != D9SvRegTemp)
                continue;
            Writer = D9Ps1Writer(Temps.History[Component][Source->Number].LastWrite);
            if (Writer != nullptr && Writer->Pair != Op->Pair && !Writer->TexOp &&
                Writer->Dest.Modifier != D9Ps1Saturate)
            {
                Affected |= D9SvComponent(Component);
            }
        }
        if (Affected == 0)
            continue;
        ComponentText(Affected, TRUE, FALSE, Text);
        Report(1, 522, "When using the complement source modifier on a register, the previous writer should apply "
               "the saturate destination modifier. This would ensure consistent behaviour across different hardware. "
               "Affected components(*) of %s source param: %s", D9Ps1Ordinals[Index], Text);
    }
}

VOID
D9Ps1Checker::SourceInitialized14()
{
    D9Ps1Instruction* Op = Ins();
    BOOLEAN FromDest = Op->Dest.Coordinates != 0;
    UINT Reads = FromDest ? 1 : Op->SourceCount;

    for (UINT Index = 0; Index < Reads; Index++)
    {
        D9Ps1Access* SamePair[4] = {};
        UINT Number;
        UINT Type;
        DWORD Read;
        PCSTR Prefix;
        D9Ps1File* File;

        if (FromDest)
        {
            Number = Op->Dest.Number;
            Type = Op->Dest.Type;
            Read = Op->Dest.Coordinates;
        }
        else
        {
            if (SkipSource[Index])
                continue;
            Number = Op->Source[Index].Number;
            Type = Op->Source[Index].Type;
            Read = Op->Source[Index].Read;
        }
        switch (Type)
        {
        case D9SvRegTemp:
            Prefix = "r";
            break;
        case D9SvRegInput:
            Prefix = "v";
            break;
        case D9SvRegConst:
            Prefix = "c";
            break;
        case D9Ps1RegTexture:
            Prefix = "t";
            break;
        default:
            continue;
        }
        File = FileOf(Type);
        if (Number >= File->Count)
            continue;

        if (Type == D9SvRegTemp)
        {
            DWORD Missing = 0;
            UINT MissingCount = 0;

            for (UINT Component = 0; Component < 4; Component++)
            {
                D9Ps1Access* Writer;

                if ((Read & D9SvComponent(Component)) == 0)
                    continue;
                Writer = File->History[Component][Number].LastWrite;
                while (Writer != nullptr && D9Ps1Cast(Writer->Instruction)->Pair == Op->Pair)
                {
                    SamePair[Component] = Writer;
                    Writer = Writer->PriorWrite;
                }
                if (Writer == nullptr)
                {
                    MissingCount += 1;
                    Missing |= D9SvComponent(Component);
                }
            }
            if (MissingCount != 0)
            {
                char Text[32];
                PCSTR Plural = MissingCount > 1 ? "s" : "";
                DWORD Bit = 1u << Number;
                BOOLEAN Alpha = (Missing & D9SvComponentW) != 0 && (AlphaLost & Bit) != 0;
                BOOLEAN Blue = (Missing & D9SvComponentZ) != 0 && (BlueLost & Bit) != 0;

                ComponentText(Missing, TRUE, TRUE, Text);
                if (Alpha && Blue)
                    Fail(2, 118, "Read of uninitialized component%s(*) in %s%d: %s. Note that when texcrd is used "
                         "with a .xy(==.rg) writemask, as it is in this shader, a side effect is that anything "
                         "previously written to the z(==b) component of the destination r# register is lost and this "
                         "component becomes uninitialized. In order to read blue again, write to it first. Also: Note "
                         "that an unfortunate effect of the phase marker earlier in the shader is that the moment it "
                         "is encountered in certain hardware, values previously written to alpha in any r# register, "
                         "including the one noted here, are lost. In order to read alpha from an r# register after the "
                         "phase marker, write to it first.", Plural, Prefix, Number, Text);
                else if (Alpha)
                    Fail(2, 119, "Read of uninitialized component%s(*) in %s%d: %s. Note that an unfortunate effect "
                         "of the phase marker earlier in the shader is that the moment it is encountered in certain "
                         "hardware, values previously written to alpha in any r# register, including the one noted "
                         "here, are lost. In order to read alpha from an r# register after the phase marker, write to "
                         "it first.", Plural, Prefix, Number, Text);
                else if (Blue)
                    Fail(2, 120, "Read of uninitialized component%s(*) in %s%d: %s. Note that when texcrd is used "
                         "with a .xy(==.rg) writemask, as it is in this shader, a side effect is that anything "
                         "previously written to the z(==b) component of the destination r# register is lost and this "
                         "component becomes uninitialized. In order to read blue again, write to it first.", Plural,
                         Prefix, Number, Text);
                else
                    Fail(2, 121, "Read of uninitialized component%s(*) in %s%d: %s", Plural, Prefix, Number, Text);
            }
        }
        RecordReads(File, Number, Read, SamePair);
    }
}

VOID
D9Ps1Checker::DestParam14()
{
    D9Ps1Instruction* Op = Ins();
    D9SvDest* Dest = &Op->Dest;
    BOOLEAN Bad = FALSE;
    UINT Number = Dest->Number;

    if (Op->Opcode == D9SvOpDef || !Dest->Present)
        return;
    if ((Dest->Type == D9SvRegTemp || Dest->Type == D9Ps1RegTexture) && Temps.Count != 0 &&
        (Dest->Type != D9Ps1RegTexture || Op->Opcode == D9Ps1OpTexkill))
    {
        if (Number >= Temps.Count)
        {
            Fail(2, 124, "Invalid destination register number: %d. Max allowed for this register type is %d.", Number,
                 Temps.Count - 1);
            Bad = TRUE;
        }
    }
    else
    {
        Fail(2, 123, "Invalid register type for destination param.");
        Bad = TRUE;
    }

    if (!Op->TexOp)
    {
        if (Dest->Modifier != 0 && Dest->Modifier != D9Ps1Saturate)
        {
            Fail(2, 130, "Invalid instruction modifier.");
            Bad = TRUE;
        }
        switch (Dest->Shift)
        {
        case 0: case 1: case 2: case 3: case 13: case 14: case 15:
            break;
        default:
            Fail(2, 131, "Invalid destination shift.");
            return;
        }
    }
    else
    {
        if (Op->Opcode == D9Ps1OpTexcoord)
        {
            if (Op->Source[0].Modifier == D9Ps1ModDivideW)
            {
                if (Dest->Mask != D9Ps1MaskXy)
                {
                    Fail(2, 125, "texcrd with _dw(=_da) source modifier must use .xy(=.rg) destination writemask.");
                    Bad = TRUE;
                }
            }
            else if (Dest->Mask != D9SvMaskXyz)
            {
                Fail(2, 126, "texcrd must use .xyz(=.rgb) destination writemask.");
                Bad = TRUE;
            }
        }
        else if (Dest->Mask != D9SvMaskAll)
        {
            Fail(2, 127, "texld/texkill/texdepth instructions must write all components.");
            Bad = TRUE;
        }
        if (Dest->Modifier != 0)
        {
            Fail(2, 128, "Instruction modifiers not allowed for tex* instructions.");
            Bad = TRUE;
        }
        if (Dest->Shift != 0)
        {
            Fail(2, 129, "Destination shift not allowed for tex* instructions.");
            Bad = TRUE;
        }
    }

    if (Bad || Op->Opcode == D9Ps1OpTexkill || Dest->Type != D9SvRegTemp)
        return;
    if ((Dest->Mask & D9SvComponentX) != 0)
        Record(&Temps.History[0][Number], Current, TRUE);
    if ((Dest->Mask & D9SvComponentY) != 0)
        Record(&Temps.History[1][Number], Current, TRUE);
    if ((Dest->Mask & D9SvComponentZ) != 0)
    {
        Record(&Temps.History[2][Number], Current, TRUE);
    }
    else if (Op->Opcode == D9Ps1OpTexcoord && Temps.History[2][Number].LastWrite != nullptr)
    {
        /* texcrd to .xy loses what blue held */
        ZeroMemory(&Temps.History[2][Number], sizeof(Temps.History[2][Number]));
        BlueLost |= 1u << Number;
    }
    if ((Dest->Mask & D9SvComponentW) != 0)
        Record(&Temps.History[3][Number], Current, TRUE);
}

VOID
D9Ps1Checker::Ports14()
{
    D9Ps1Instruction* Op = Ins();
    UINT Distinct[4][4];
    UINT Counts[4] = {};

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        UINT Type = Op->Source[Index].Type;
        UINT Number = Op->Source[Index].Number;
        BOOLEAN Seen = FALSE;

        if (Type > D9Ps1RegTexture)
            continue;
        for (UINT Slot = 0; Slot < Counts[Type]; Slot++)
        {
            if (Distinct[Type][Slot] == Number)
                Seen = TRUE;
        }
        if (!Seen)
        {
            Distinct[Type][Counts[Type]] = Number;
            Counts[Type] += 1;
        }
    }
    if (Counts[0] > Temps.Ports)
        Fail(2, 132, "%d different temp registers (r#) read by instruction.  Max. different temp registers readable "
             "per instruction is %d.", Counts[0], Temps.Ports);
    if (Counts[1] > Inputs.Ports)
        Fail(2, 133, "%d different input registers (v#) read by instruction.  Max. different input registers readable "
             "per instruction is %d.", Counts[1], Inputs.Ports);
    if (Counts[2] > Consts.Ports)
        Fail(2, 134, "%d different constant registers (c#) read by instruction.  Max. different constant registers "
             "readable per instruction is %d.", Counts[2], Consts.Ports);
    if (Counts[3] > Textures.Ports)
        Fail(2, 135, "%d different texture coordinate registers (t#) read by instruction.  Max. different texture "
             "registers readable per instruction is %d.", Counts[3], Textures.Ports);
}

VOID
D9Ps1Checker::TexAfterArithmetic14()
{
    D9Ps1Instruction* Op = Ins();

    if (!Op->TexOp && Op->Opcode != D9SvOpNop && Op->Opcode != D9SvOpDef && Op->Opcode != D9SvOpPhase)
    {
        New.SawArith = 1;
        return;
    }
    if (Op->Opcode == D9SvOpPhase)
    {
        New.SawArith = 0;
        New.OrderReported = 0;
    }
    else if (!New.OrderReported && Op->TexOp && New.SawArith)
    {
        Fail(2, 141, "tex* instructions cannot be after arithmetic instructions within one phase of a ps_1_4 shader.  "
             "Each phase can have a block of tex* instructions followed by a block of arithmetic instructions. ");
        New.OrderReported = 1;
    }
}

VOID
D9Ps1Checker::TexStages14()
{
    D9Ps1Instruction* Op = Ins();
    DWORD Bit;

    if (Op->Opcode == D9SvOpPhase)
        New.StageMask = 0;
    if (!Op->TexOp || Op->Dest.Type != D9SvRegTemp || Op->Dest.Number >= Temps.Count)
        return;
    Bit = 1u << Op->Dest.Number;
    if ((New.StageMask & Bit) != 0)
        Fail(2, 140, "Register r%d (and thus texture stage %d) already used as a destination for a tex* instruction "
             "in this block of the shader. Reuse of a texture stage in ps_1_4 may only be accmplished by splitting "
             "the tex* ops across a phase marker.", Op->Dest.Number, Op->Dest.Number);
    else
        New.StageMask |= Bit;
}

VOID
D9Ps1Checker::DzLimit14()
{
    D9Ps1Instruction* Op = Ins();

    for (UINT Index = 0; Index < Op->SourceCount; Index++)
    {
        if (SkipSource[Index])
            continue;
        if (Op->Source[Index].Modifier == D9Ps1ModDivideZ)
            New.DzCount += 1;
        if (New.DzCount > 2 && !New.DzReported)
        {
            Fail(2, 117, "_dz(=_db) modifier may only be used at most 2 times in a shader.");
            New.DzReported = 1;
        }
    }
}

VOID
D9Ps1Checker::Texdepth14()
{
    D9Ps1Instruction* Op = Ins();

    if (Op->Opcode == D9Ps1OpTexdepth)
    {
        if (New.DepthUsed)
        {
            Fail(2, 151, "Only one use of texdepth is permitted.");
            return;
        }
        New.DepthUsed = 1;
        if (Op->Dest.Number != 5 || Op->Dest.Type != D9SvRegTemp)
            Fail(2, 152, "Destination for texdepth must be r5.");
        if (Phase == nullptr)
            New.DepthBeforePhase = 1;
    }
    else if (New.DepthUsed)
    {
        /* Every source, then the destination */
        for (UINT Index = 0; Index <= Op->SourceCount; Index++)
        {
            BOOLEAN IsDest = Index == Op->SourceCount;
            UINT Number = IsDest ? Op->Dest.Number : Op->Source[Index].Number;
            UINT Type = IsDest ? Op->Dest.Type : Op->Source[Index].Type;

            if (Number == 5 && Type == D9SvRegTemp)
                Fail(2, 153, "After texdepth instruction, r5 is no longer available in shader.");
        }
    }
    if (Op->Opcode == D9SvOpPhase && New.DepthBeforePhase)
        Fail(2, 154, "When a phase marker is present in a shader, texdepth is only permitted after the phase marker.");
}

VOID
D9Ps1Checker::Bem14()
{
    D9Ps1Instruction* Op = Ins();

    if (Op->Opcode != D9Ps1OpBem)
        return;
    if (New.BemUsed)
        Fail(2, 146, "bem may only be used once in a shader.");
    New.BemUsed = 1;
    if (Phase != nullptr)
        Fail(2, 147, "bem can only be used before a phase marker (required) later in the shader.");
    if (Op->Dest.Mask != D9Ps1MaskXy)
        Fail(2, 148, "Writemask for bem must be '.rg'");
    for (UINT Index = 0; Index < Op->SourceCount && Index < 2; Index++)
    {
        UINT Type = Op->Source[Index].Type;

        if (SkipSource[Index])
            continue;
        if (Index == 0 && Type != D9SvRegTemp && Type != D9SvRegConst)
            Fail(2, 149, "First source parameter for bem must be temp (r#) or constant (c#) register.");
        else if (Index == 1 && Type != D9SvRegTemp)
            Fail(2, 150, "Second source parameter for bem must be temp (r#) register.");
    }
}

VOID
D9Ps1Checker::Lrp14()
{
    D9Ps1Instruction* Op = Ins();
    D9SvSource* Blend = &Op->Source[0];
    DWORD Affected = 0;
    char Text[32];

    if (Op->Opcode != D9SvOpLrp || !NoDevice() || !Blend->Present || SkipSource[0])
        return;
    for (UINT Component = 0; Component < 4; Component++)
    {
        D9Ps1Access* Writer;
        D9Ps1Instruction* By;

        if ((Blend->Read & D9SvComponent(Component)) == 0 || Blend->Type != D9SvRegTemp)
            continue;
        Writer = Temps.History[Component][Blend->Number].LastWrite;
        if (Writer == nullptr)
            continue;
        if (Writer->Instruction != nullptr && Writer->Instruction == Current)
        {
            Writer = Writer->PriorWrite;
            if (Writer == nullptr)
                continue;
        }
        By = D9Ps1Writer(Writer);
        if (By != nullptr && !By->TexOp && By->Dest.Modifier != D9Ps1Saturate)
            Affected |= D9SvComponent(Component);
    }
    if (Affected == 0)
        return;
    ComponentText(Affected, TRUE, FALSE, Text);
    Report(1, 524, "Previous writer to the first source register of lrp instruction should apply the saturate "
           "destination modifier.  This ensures consistent behaviour across different hardware. Affected "
           "components(*) of first source register: %s", Text);
}

VOID
D9Ps1Checker::Definition14()
{
    D9Ps1Instruction* Op = Ins();
    D9SvDest* Dest = &Op->Dest;
    BOOLEAN Good = TRUE;

    if (Op->Opcode != D9SvOpDef)
    {
        New.DefOpen = 0;
        return;
    }
    if (!New.DefOpen)
    {
        Fail(2, 155, "Const declaration (def) must appear before other instructions.");
        Good = FALSE;
    }
    if (Dest->Mask != D9SvMaskAll || Dest->Modifier != 0 || Dest->Shift != 0 || Dest->Type != D9SvRegConst)
    {
        Fail(2, 156, "Destination for def instruction must be of the form c# (where # = reg number). In addition "
             "there may be no mask (equivalent to full .xyzw, which is fine).");
        Good = FALSE;
    }
    if (Dest->Type == D9SvRegConst && Dest->Number >= Consts.Count)
    {
        Fail(2, 157, "Invalid const register num: %d. Max allowed is %d.", Dest->Number, Consts.Count - 1);
        return;
    }
    if (!Good)
        return;
    if (Consts.Defined[Dest->Number])
        Fail(2, 588, "c%d already def'd in the shader. ", Dest->Number);
    Consts.Defined[Dest->Number] = 1;
}

VOID
D9Ps1Checker::Pairing14()
{
    D9Ps1Instruction* Op = Ins();
    D9Ps1Instruction* Before = D9Ps1Cast(Op->Previous);
    DWORD Mine = 0;
    DWORD Theirs = 0;
    BOOLEAN Pairable = Op->TexOp || (Op->Opcode != D9SvOpNop && Op->Opcode != D9SvOpDp4 &&
                                     Op->Opcode != D9SvOpDef && Op->Opcode != D9SvOpPhase);

    if (Op->Opcode == D9SvOpPhase)
        New.PairOpen = 0;
    else if (Pairable)
        New.PairOpen = 1;
    if (!Op->CoIssue)
        return;
    if (Op->TexOp)
    {
        Fail(2, 158, "Cannot set co-issue ('+') on a tex* instruction.  Co-issue only applies to arithmetic "
             "instructions.");
        return;
    }
    if (!New.PairOpen || Before == nullptr)
    {
        if (Op->Opcode == D9SvOpPhase)
            Fail(2, 159, "Phase marker cannot be co-issued.");
        else if (Before != nullptr && Before->Opcode == D9SvOpDp4)
            Fail(2, 160, "dp4 cannot be coissued.");
        else
            Fail(2, 383, "Instruction cannot have co-issue ('+') set without a previous arithmetic instruction to "
                 "pair with.");
        return;
    }
    if (Before->CoIssue)
    {
        Fail(2, 161, "Cannot set co-issue ('+') on consecutive instructions.");
        return;
    }
    for (UINT Half = 0; Half < 2; Half++)
    {
        D9Ps1Instruction* Which = Half == 0 ? Op : Before;
        PCSTR Lead = Half == 0 ? "" : "(previous instruction)";

        switch (Which->Opcode)
        {
        case D9SvOpNop:
            Fail(2, 164, "%s nop cannot be co-issued.", Lead);
            return;
        case D9SvOpDp4:
            Fail(2, 165, "%s dp4 cannot be co-issued.", Lead);
            return;
        case D9SvOpDef:
            Fail(2, 163, "%s def cannot be co-issued.", Lead);
            return;
        case D9Ps1OpBem:
            Fail(2, 166, "%s bem cannot be co-issued.", Lead);
            return;
        case D9SvOpPhase:
            Fail(2, 162, "%s phase marker cannot be co-issued.", Lead);
            return;
        default:
            break;
        }
    }

    if (Op->Dest.Present)
        Mine = Op->Dest.Mask;
    if (Before->Dest.Present)
        Theirs = Before->Dest.Mask;
    if (Op->Opcode == D9SvOpDp3)
    {
        if (Before->Opcode == D9SvOpDp3)
        {
            Fail(2, 167, "Co-issued instructions cannot both be dp3, since each require use of the color pipe to "
                 "execute.");
        }
        else
        {
            if ((Theirs & D9Ps1Color) != 0)
                Fail(2, 168, "(previous instruction) dp3 needs color pipe to execute, so instruction co-issued with "
                     "it cannot write to color components.");
            if ((Mine & D9Ps1Alpha) != 0)
                Fail(2, 169, "dp3 which writes alpha cannot co-issue since it uses up both the alpha and color "
                     "pipes.");
        }
    }
    else if (Before->Opcode == D9SvOpDp3)
    {
        if ((Mine & D9Ps1Color) != 0)
            Fail(2, 170, "(previous instruction) dp3 needs color pipe to execute, so instruction co-issued with it "
                 "cannot write to color components.");
        if ((Theirs & D9Ps1Alpha) != 0)
        {
            Fail(2, 171, "(previous instruction) dp3 which writes alpha cannot co-issue since it uses up both the "
                 "alpha and color pipes.");
            goto PreviousBoth;
        }
    }
    if ((Theirs & D9Ps1Alpha) == 0)
        goto Rest;

PreviousBoth:
    if ((Theirs & D9Ps1Color) != 0)
        Fail(2, 172, "(previous instruction) Individual instruction in co-issue pair cannot write both alpha and "
             "color component(s).");

Rest:
    if ((Mine & D9Ps1Alpha) != 0 && (Mine & D9Ps1Color) != 0)
        Fail(2, 173, "Individual instruction in co-issue pair cannot write both alpha and color component(s).");
    if ((Theirs & Mine) != 0)
        Fail(2, 174, "Co-issued instructions cannot both write to the same component(s).  One instruction must write "
             "to alpha and the other may write to any combination of red/green/blue.  Destination registers may "
             "differ.");
    if (((Theirs | Mine) & D9Ps1Alpha) == 0)
        Fail(2, 175, "One of the instructions in a co-issue pair must write to alpha only (.a writemask).");
}

VOID
D9Ps1Checker::CountSlots14()
{
    D9Ps1Instruction* Op = Ins();
    BOOLEAN AtPhase;

    if (Op == nullptr)
        return;
    if (!Ended && Op->Opcode != D9SvOpPhase)
    {
        switch (Op->Opcode)
        {
        case 1: case 2: case 3: case 4: case 5: case 8: case 9: case 18: case 80: case 88:
            if (!Op->CoIssue)
            {
                New.ArithSlots += 1;
                New.TotalSlots += 1;
            }
            break;
        case 89:
            New.ArithSlots += 2;
            New.TotalSlots += 2;
            break;
        case 64: case 65: case 66: case 87:
            New.TexSlots += 1;
            New.TotalSlots += 1;
            break;
        default:
            break;
        }
        return;
    }

    AtPhase = Op->Opcode == D9SvOpPhase;
    if (AtPhase)
    {
        if (New.TexSlots > New.TexLimit)
            Fail(6, 176, "Too many (%d) tex* instruction slots used before phase marker. Max. allowed in a phase is "
                 "%d.", New.TexSlots, New.TexLimit);
        if (New.ArithSlots > New.ArithLimit)
        {
            if (Params->PixelTargetMessages)
                Fail(0x10006, 177, "Cannot fit shader into length limits for target shader model (ps_1_4). ");
            else
                Fail(6, 486, "Too many (%d) arithmetic instruction slots used before phase marker. Max. allowed in a "
                     "phase (counting any co-issued pairs as 1) is %d.", New.ArithSlots, New.ArithLimit);
        }
    }
    else if (Phase != nullptr)
    {
        if (New.TexSlots > New.TexLimit)
            Fail(6, 178, "Too many (%d) tex* instruction slots used after phase marker. Max. allowed in a phase is "
                 "%d.", New.TexSlots, New.TexLimit);
        if (New.ArithSlots > New.ArithLimit)
        {
            if (Params->PixelTargetMessages)
                Fail(0x10006, 500, "Cannot fit shader into length limits for target shader model (ps_1_4). ");
            else
                Fail(6, 179, "Too many (%d) arithmetic instruction slots used after phase marker. Max. allowed in a "
                     "phase (counting any co-issued pairs as 1) is %d.", New.ArithSlots, New.ArithLimit);
        }
    }
    else
    {
        if (New.TexSlots > New.TexLimit)
            Fail(6, 180, "Too many (%d) tex* instruction slots used. Max. allowed is %d. Note that adding a phase "
                 "marker to the shader would double the number of instructions available.", New.TexSlots,
                 New.TexLimit);
        if (New.ArithSlots > New.ArithLimit)
        {
            if (Params->PixelTargetMessages)
                Fail(0x10006, 501, "Cannot fit shader into length limits for target shader model (ps_1_4). ");
            else
                Fail(6, 181, "Too many (%d) arithmetic instruction slots used. Max. allowed (counting any co-issued "
                     "pairs as 1) is %d. Note that adding a phase marker to the shader would double the number of "
                     "instructions available.", New.ArithSlots, New.ArithLimit);
        }
    }
    if (AtPhase)
    {
        New.TexSlots = 0;
        New.ArithSlots = 0;
        New.TotalSlots = 0;
    }
}

VOID
D9Ps1Checker::LogTextures14()
{
    D9Ps1Instruction* Op = Ins();
    UINT Stage;

    if (Ended)
    {
        if (Errors != 0)
            Params->Outputs = MAXDWORD;
        return;
    }
    if (Errors != 0)
        return;
    if (Op->Opcode == D9Ps1OpTexkill)
    {
        if (Op->Dest.Type != D9Ps1RegTexture)
            return;
        Stage = Op->Dest.Number;
    }
    else if (Op->Opcode == D9Ps1OpTexcoord || Op->Opcode == D9Ps1OpTex)
    {
        if (Op->Source[0].Type != D9Ps1RegTexture)
            return;
        Stage = Op->Source[0].Number;
    }
    else
    {
        return;
    }
    if (static_cast<INT>(Params->Outputs) <= static_cast<INT>(Stage))
        Params->Outputs = Stage;
}

VOID
D9Ps1Checker::R0Written14()
{
    DWORD Missing = 0;
    UINT MissingCount = 0;
    char Text[32];
    PCSTR Plural;
    BOOLEAN Alpha;
    BOOLEAN Blue;

    for (UINT Component = 0; Component < 4; Component++)
    {
        if (Temps.History[Component][0].LastWrite == nullptr)
        {
            MissingCount += 1;
            Missing |= D9SvComponent(Component);
        }
    }
    if (MissingCount == 0)
        return;
    ComponentText(Missing, TRUE, FALSE, Text);
    Plural = MissingCount > 1 ? "s" : "";
    Alpha = (Missing & D9SvComponentW) != 0 && (AlphaLost & 1) != 0;
    Blue = (Missing & D9SvComponentZ) != 0 && (BlueLost & 1) != 0;
    if (Alpha && Blue)
        Fail(6, 182, "r0 must be written by shader. Uninitialized component%s(*): %s. Note that when texcrd is used "
             "with a .xy(==.rg) writemask, as it is in this shader, a side effect is that anything previously written "
             "to the z(==b) component of the destination r# register is lost and this component becomes "
             "uninitialized. The blue component of r0 must to be written after the texcrd. Also: Note that an "
             "unfortunate effect of the phase marker in the shader is that the moment it is encountered in certain "
             "hardware, values previously written to alpha in any r# register, including r0, are lost. So after a "
             "phase marker, the alpha component of r0 must be written.", Plural, Text);
    else if (Alpha)
        Fail(6, 183, "r0 must be written by shader. Uninitialized component%s(*): %s. Note that an unfortunate "
             "effect of the phase marker in the shader is that the moment it is encountered in certain hardware, "
             "values previously written to alpha in any r# register, including r0, are lost. So after a phase "
             "marker, the alpha component of r0 must be written.", Plural, Text);
    else if (Blue)
        Fail(6, 184, "r0 must be written by shader. Uninitialized component%s(*): %s. Note that when texcrd is used "
             "with a .xy(==.rg) writemask, as it is in this shader, a side effect is that anything previously written "
             "to the z(==b) component of the destination r# register is lost and this component becomes "
             "uninitialized. The blue component of r0 must to be written after the texcrd.", Plural, Text);
    else
        Fail(6, 185, "r0 must be written by shader. Uninitialized component%s(*): %s", Plural, Text);
}

/* Rule order */

BOOLEAN
D9Ps1Checker::CheckInstruction()
{
    D9Ps1Instruction* Op = Ins();

    if (!Recognized())
        return FALSE;
    if (!Model14)
    {
        if (!Supported10() || !ParamCount10())
            return TRUE;
        SourceParams10();
        NegateAfterSat10();
        SatBeforeModifier10();
        DependentReads10();
        NoLongerAvailable10();
        SourceInitialized10();
        DestParam10();
        Ports10();
        TexDeclaredInOrder10();
        TexAfterArithmetic10();
        TexmSequence10();
        TexmRegisters10();
        if (Op->Opcode == 80)
        {
            D9SvSource* Selector = &Op->Source[0];

            if (!Selector->Present || Selector->Type != D9SvRegTemp || Selector->Number != 0 ||
                Selector->Swizzle != D9SvSwizzleW || Selector->Modifier != 0)
            {
                Fail(2, 59, "First source for cnd instruction must be 'r0.a'.");
            }
        }
        if (Op->Opcode == D9Ps1OpCmp)
        {
            Old.CmpCount += 1;
            if (Old.CmpCount > 3)
                Fail(2, 60, "Maximum of 3 cmp instructions allowed.");
        }
        Lrp10();
        Definition10();
        if (Op->Opcode == D9SvOpDp3 && Op->Dest.Mask != D9SvMaskXyz && Op->Dest.Mask != D9SvMaskAll)
            Fail(2, 66, "Dest write mask must be .rgb, or .rgba (all) for dp3.");
        if (Op->Opcode == D9SvOpDp4)
        {
            Old.Dp4Count += 1;
            if (Old.Dp4Count > 4)
                Fail(2, 67, "Maximum of 4 dp4 instructions allowed.");
        }
        Pairing10();
        CountSlots10();
        LogTextures10();
        return TRUE;
    }

    if (!Supported14() || !ParamCount14() || !Marker14())
        return TRUE;
    SourceParams14();
    DependentReads14();
    SatBeforeComplement14();
    SourceInitialized14();
    DestParam14();
    Ports14();
    TexAfterArithmetic14();
    TexStages14();
    DzLimit14();
    Texdepth14();
    if (Op->Opcode == D9Ps1OpTexkill)
    {
        if (Phase == nullptr)
            New.KillBeforePhase = 1;
    }
    else if (Op->Opcode == D9SvOpPhase && New.KillBeforePhase)
    {
        Fail(2, 145, "When a phase marker is present in a shader, texkill is only permitted after the phase marker.");
    }
    Bem14();
    Lrp14();
    Definition14();
    Pairing14();
    CountSlots14();
    LogTextures14();
    return TRUE;
}

VOID
D9Ps1Checker::CheckEnd()
{
    if (!Model14)
    {
        TexmSequence10();
        CountSlots10();
        R0Written10();
        LogTextures10();
        return;
    }
    CountSlots14();
    R0Written14();
    LogTextures14();
}
