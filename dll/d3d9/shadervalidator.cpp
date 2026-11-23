/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader token validation: decoding, the shared checker and the vertex shader models
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shadervalidatorcore.hpp"

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Names of the opcodes as messages print them */
PCSTR
D9SvOpcodeName(
    _In_ UINT Opcode)
{
    static const PCSTR Names[] =
    {
        "nop", "mov", "add", "sub", "mad", "mul", "rcp", "rsq", "dp3", "dp4", "min", "max", "slt", "sge", "exp", "log",
        "lit", "dst", "lrp", "frc", "m4x4", "m4x3", "m3x4", "m3x3", "m3x2", "call", "callnz", "loop", "ret",
        "endloop", "label", "dcl", "pow", "crs", "sgn", "abs", "nrm", "sincos", "rep", "endrep", "if", "if_cmp",
        "else", "endif", "break", "break_cmp", "mova", "defb", "defi",
    };
    static const PCSTR PixelNames[] =
    {
        "texcoord/texcrd", "texkill", "texld[b|p]*", "texbem", "texbeml", "texreg2ar", "texreg2gb", "texm3x2pad",
        "texm3x2tex", "texm3x3pad", "texm3x3tex", "???", "texm3x3spec", "texm3x3vspec", "expp", "logp", "cnd", "def",
        "texreg2rgb", "texdp3tex", "texm3x2depth", "texdp3", "texm3x3", "texdepth", "cmp", "bem", "dp2add", "dsx",
        "dsy", "texldd", "setp", "texldl", "breakp",
    };

    if (Opcode < _countof(Names))
        return Names[Opcode];
    if (Opcode >= 64 && Opcode < 64 + _countof(PixelNames))
        return PixelNames[Opcode - 64];
    if (Opcode == D9SvOpPhase)
        return "phase";
    if (Opcode == D9SvOpComment)
        return "comment";
    if (Opcode == D9SvOpEnd)
        return "end";
    return "???";
}

PCSTR
D9SvUsageName(
    _In_ UINT Usage)
{
    static const PCSTR Names[] =
    {
        "position", "blendweight", "blendindices", "normal", "psize", "texcoord", "tangent", "binormal",
        "tessfactor", "positiont", "color", "fog", "depth", "sample",
    };

    return Usage < _countof(Names) ? Names[Usage] : "unknown";
}

/* "vs_2_x" style names, with the plain form for the rest */
VOID
D9SvVersionName(
    _In_ DWORD Version,
    _Out_writes_(32) PSTR Name)
{
    PCSTR Special = nullptr;

    switch (Version)
    {
    case 0xFFFE0201:
        Special = "vs_2_x";
        break;
    case 0xFFFE02FF:
        Special = "vs_2_sw";
        break;
    case 0xFFFE03FF:
        Special = "vs_3_sw";
        break;
    case 0xFFFF0201:
        Special = "ps_2_x";
        break;
    case 0xFFFF02FF:
        Special = "ps_2_sw";
        break;
    case 0xFFFF03FF:
        Special = "ps_3_sw";
        break;
    default:
        break;
    }
    if (Special != nullptr)
    {
        strcpy_s(Name, 32, Special);
        return;
    }
    _snprintf_s(Name, 32, _TRUNCATE, "%cs_%d_%d", (Version >> 16) == 0xFFFF ? 'p' : 'v', (Version >> 8) & 0xFF,
                Version & 0xFF);
}

BOOLEAN
D9SvReplicate(
    _In_ DWORD Swizzle)
{
    return Swizzle == D9SvSwizzleX || Swizzle == D9SvSwizzleY || Swizzle == D9SvSwizzleZ || Swizzle == D9SvSwizzleW;
}

BOOLEAN
D9SvIsMatrix(
    _In_ UINT Opcode)
{
    return Opcode >= D9SvOpM4x4 && Opcode <= D9SvOpM3x2;
}

/* Rows a matrix instruction reads from its second source */
UINT
D9SvMatrixRows(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
    case D9SvOpM4x4:
    case D9SvOpM3x4:
        return 4;
    case D9SvOpM4x3:
    case D9SvOpM3x3:
        return 3;
    case D9SvOpM3x2:
        return 2;
    default:
        return 1;
    }
}

/* Register files */

BOOLEAN
D9SvRegisters::Create(
    _In_ UINT Registers,
    _In_ BOOLEAN CanWrite,
    _In_ UINT ReadPorts)
{
    Count = Registers;
    Writable = CanWrite;
    Ports = ReadPorts;
    if (Count == 0)
        return TRUE;
    Storage = new (std::nothrow) BYTE[Count * 4];
    if (Storage == nullptr)
    {
        Count = 0;
        return FALSE;
    }
    ZeroMemory(Storage, Count * 4);
    for (UINT Component = 0; Component < 4; Component++)
        Marks[Component] = Storage + Component * Count;
    return TRUE;
}

D9SvRegisters::~D9SvRegisters()
{
    delete[] Storage;
}

DWORD
D9SvRegisters::Written(
    _In_ UINT Number) const
{
    DWORD Mask = 0;

    if (Number >= Count)
        return 0;
    for (UINT Component = 0; Component < 4; Component++)
    {
        if (Marks[Component][Number] != 0)
            Mask |= D9SvComponent(Component);
    }
    return Mask;
}

VOID
D9SvRegisters::Mark(
    _In_ UINT Number,
    _In_ DWORD Mask)
{
    if (Number >= Count)
        return;
    for (UINT Component = 0; Component < 4; Component++)
    {
        if ((Mask & D9SvComponent(Component)) != 0)
            Marks[Component][Number] = 1;
    }
}

/* Flow control blocks */

VOID
D9SvFlow::Reset()
{
    ZeroMemory(this, sizeof(*this));
    CurrentLabel = MAXUINT;
    memset(LabelMinLoop, 0xFF, sizeof(LabelMinLoop));
}

D9SvFlow::~D9SvFlow()
{
    for (UINT Level = 0; Level < _countof(Calls); Level++)
        ClearCalls(Level);
}

VOID
D9SvFlow::ClearCalls(
    _In_ UINT Level)
{
    D9SvCall* Call = Calls[Level];

    while (Call != nullptr)
    {
        D9SvCall* Next = Call->Next;

        delete Call;
        Call = Next;
    }
    Calls[Level] = nullptr;
}

/* Labels called from inside a loop that breaks learn so when the loop closes */
VOID
D9SvFlow::CloseCalls(
    _In_ UINT Level)
{
    BOOLEAN Broke;

    if (Level == 0)
        return;
    Broke = LoopBroke[Level - 1] != 0;
    for (D9SvCall* Call = Calls[Level - 1]; Call != nullptr; Call = Call->Next)
    {
        if (Broke)
            LabelFromBreakingLoop[Call->Label] = 1;
    }
    ClearCalls(Level - 1);
}

VOID
D9SvFlow::Push(
    _In_ UINT Kind)
{
    Depth += 1;
    Blocks[Depth].Kind = Kind;
    Blocks[Depth].SeenElse = FALSE;
    switch (Kind)
    {
    case D9SvBlockDynamicIf:
        DynamicIfDepth += 1;
        break;
    case D9SvBlockStaticIf:
        StaticIfDepth += 1;
        break;
    case D9SvBlockLoop:
    case D9SvBlockRep:
        if (Kind == D9SvBlockLoop)
            LoopDepth += 1;
        else
            RepDepth += 1;
        LoopOrRepDepth += 1;
        LoopBroke[LoopOrRepCount] = 0;
        LoopGradient[LoopOrRepCount] = 0;
        ClearCalls(LoopOrRepCount);
        if (Kind == D9SvBlockLoop)
            LoopCount += 1;
        LoopOrRepCount += 1;
        break;
    default:
        break;
    }
}

VOID
D9SvFlow::Pop()
{
    switch (Blocks[Depth].Kind)
    {
    case D9SvBlockDynamicIf:
        DynamicIfDepth -= 1;
        break;
    case D9SvBlockStaticIf:
        StaticIfDepth -= 1;
        break;
    case D9SvBlockLoop:
    case D9SvBlockRep:
        if (Blocks[Depth].Kind == D9SvBlockLoop)
            LoopDepth -= 1;
        else
            RepDepth -= 1;
        LoopOrRepDepth -= 1;
        CloseCalls(LoopOrRepCount);
        if (Blocks[Depth].Kind == D9SvBlockLoop)
            LoopCount -= 1;
        LoopOrRepCount -= 1;
        break;
    default:
        break;
    }
    if (Depth != 0)
        Depth -= 1;
}

VOID
D9SvFlow::BeginSubroutine(
    _In_ UINT Label)
{
    InSubroutine = TRUE;
    CurrentLabel = Label;
    FromBreakingLoop = LabelFromBreakingLoop[Label];
    CallDepth = static_cast<BYTE>(LabelCallDepth[Label] + 1);
    LoopDepth = LabelLoopDepth[Label];
    RepDepth = LabelRepDepth[Label];
    LoopOrRepDepth = LabelLoopOrRepDepth[Label];
    StaticIfDepth = LabelStaticIfDepth[Label];
    DynamicIfDepth = LabelDynamicIfDepth[Label];
}

/* A call records the deepest nesting each label is reached from */
BOOLEAN
D9SvFlow::Call(
    _In_ UINT Label)
{
    BYTE Smallest;

    Label &= 0x7FF;
    LabelCallDepth[Label] = D9SvMax(LabelCallDepth[Label], CallDepth);
    LabelLoopDepth[Label] = D9SvMax(LabelLoopDepth[Label], LoopDepth);
    LabelRepDepth[Label] = D9SvMax(LabelRepDepth[Label], RepDepth);
    LabelLoopOrRepDepth[Label] = D9SvMax(LabelLoopOrRepDepth[Label], LoopOrRepDepth);
    LabelStaticIfDepth[Label] = D9SvMax(LabelStaticIfDepth[Label], StaticIfDepth);
    LabelDynamicIfDepth[Label] = D9SvMax(LabelDynamicIfDepth[Label], DynamicIfDepth);
    if (InSubroutine)
        Smallest = D9SvMin(LabelMinLoop[Label], LabelMinLoop[CurrentLabel & 0x7FF]);
    else
        Smallest = D9SvMin(LabelMinLoop[Label], LoopDepth);
    LabelMinLoop[Label] = Smallest;
    if (FromBreakingLoop)
        LabelFromBreakingLoop[Label] = 1;
    if (LoopOrRepCount != 0)
    {
        D9SvCall* Entry = new (std::nothrow) D9SvCall;

        if (Entry == nullptr)
            return FALSE;
        Entry->Label = Label;
        Entry->Next = Calls[LoopOrRepCount - 1];
        Calls[LoopOrRepCount - 1] = Entry;
    }
    return TRUE;
}

BOOLEAN
D9SvFlow::InsideIf() const
{
    return StaticIfDepth != 0 || DynamicIfDepth != 0;
}

BOOLEAN
D9SvFlow::InSubroutineOutsideLoops() const
{
    return InSubroutine && LabelMinLoop[CurrentLabel & 0x7FF] == 0;
}

/* aL may be read inside a loop, or in a subroutine every caller reaches from inside one */
BOOLEAN
D9SvFlow::LoopCounterUsable() const
{
    if (InSubroutine)
        return LoopCount != 0 || !InSubroutineOutsideLoops();
    return LoopDepth != 0;
}

VOID
D9SvFlow::Returned()
{
    InSubroutine = FALSE;
    CurrentLabel = MAXUINT;
    CallDepth = 0;
    LoopDepth = 0;
    RepDepth = 0;
    LoopOrRepDepth = 0;
    StaticIfDepth = 0;
    DynamicIfDepth = 0;
}

/* Checker core */

D9SvChecker::D9SvChecker()
{
    ZeroMemory(SkipSource, sizeof(SkipSource));
}

D9SvChecker::~D9SvChecker()
{
    while (Current != nullptr)
    {
        D9SvInstruction* Previous = Current->Previous;

        delete Current;
        Current = Previous;
    }
}

VOID
D9SvChecker::Bind(
    _In_ D9SvParams* Settings)
{
    Params = Settings;
    if (Settings->Device != nullptr)
    {
        Caps = Settings->Device->Caps;
        Behavior = Settings->Device->Behavior;
        DeviceType = Settings->Device->Type;
    }
    Version = Settings->Version;
    if (Settings->Interface)
    {
        Stream = FALSE;
    }
    else
    {
        Stream = TRUE;
        Start = Settings->Tokens;
        Cursor = Settings->Tokens + 1;
        Statements += 1;
    }
}

/* Builds "*x/0 y/1 z/2 w/3" style text naming the components a message is about */
VOID
D9SvChecker::ComponentText(
    _In_ DWORD Mask,
    _In_ BOOLEAN Colors,
    _In_ BOOLEAN Coordinates,
    _Out_writes_(32) PSTR Text)
{
    static const PCSTR ColorNames[] = { "r/", "g/", "b/", "a/" };
    static const PCSTR AxisNames[] = { "x/", "y/", "z/", "w/" };
    static const PCSTR Numbers[] = { "0 ", "1 ", "2 ", "3" };

    Text[0] = '\0';
    for (UINT Component = 0; Component < 4; Component++)
    {
        if ((Mask & D9SvComponent(Component)) != 0)
            strcat_s(Text, 28, "*");
        if (Colors)
            strcat_s(Text, 28, ColorNames[Component]);
        if (Coordinates)
            strcat_s(Text, 28, AxisNames[Component]);
        strncat_s(Text, 28, Numbers[Component], _TRUNCATE);
    }
}

/* Sends a message; instruction messages carry the file and line the caller gave the instruction */
VOID
D9SvChecker::Report(
    _In_ DWORD Level,
    _In_ DWORD Id,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Text[1024];
    va_list Arguments;
    PCSTR File = nullptr;
    INT Line = 0;
    WORD Kind = static_cast<WORD>(Level);

    if (Sink == nullptr)
        return;
    va_start(Arguments, Format);
    _vsnprintf_s(Text, sizeof(Text), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    if ((Kind == 1 || Kind == 2) && Current != nullptr && Current->Line != -1)
    {
        File = Current->File;
        Line = Current->Line;
    }
    Sink(File, Line, Level, Id, Text, SinkContext);
}

VOID
D9SvChecker::Fail(
    _In_ DWORD Level,
    _In_ DWORD Id,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    char Text[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf_s(Text, sizeof(Text), _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    Report(Level, Id, "%s", Text);
    Errors += 1;
}

/* Instructions are reused unless a model keeps the whole list */
BOOLEAN
D9SvChecker::PrepareInstruction(
    _Out_ BOOLEAN* Fresh)
{
    *Fresh = FALSE;
    if (!KeepsHistory() && Current != nullptr)
    {
        Current->Clear(nullptr);
    }
    else
    {
        D9SvInstruction* Made = NewInstruction();

        if (Made == nullptr)
        {
            Report(6, 0, "Out of memory.");
            return FALSE;
        }
        Made->Clear(Current);
        Current = Made;
        *Fresh = TRUE;
    }
    if (First == nullptr)
        First = Current;
    return TRUE;
}

/* A comment gets no record of its own */
VOID
D9SvChecker::DropComment(
    _In_ BOOLEAN Fresh)
{
    D9SvInstruction* Comment = Current;

    if (!Fresh)
        return;
    Current = Comment->Previous;
    if (Current != nullptr)
        Current->Next = nullptr;
    if (Comment == First)
        First = nullptr;
    delete Comment;
}

D9SvInstruction*
D9SvChecker::NewInstruction()
{
    return new (std::nothrow) D9SvInstruction;
}

BOOLEAN
D9SvChecker::RunStream()
{
    if (!Ready)
        return FALSE;
    for (;;)
    {
        const DWORD* Token = Cursor;
        BOOLEAN Fresh;
        UINT Used = 0;

        if (*Token == D9SvOpEnd)
            break;
        if (!PrepareInstruction(&Fresh))
            return FALSE;
        if (!Decode(Token, &Used))
            return FALSE;
        if (Current->Opcode == D9SvOpComment)
        {
            DropComment(Fresh);
        }
        else
        {
            Current->File = nullptr;
            Current->Line = 0;
            ZeroMemory(SkipSource, sizeof(SkipSource));
            if (!CheckInstruction())
                return FALSE;
        }
        Cursor += Used;
    }
    return SUCCEEDED(Finish());
}

HRESULT
D9SvChecker::Feed(
    _In_opt_z_ PCSTR File,
    _In_ INT Line,
    _In_reads_(Count) const DWORD* Tokens,
    _In_ UINT Count)
{
    BOOLEAN Fresh;
    UINT Used = Count;

    if (!PrepareInstruction(&Fresh))
        return E_FAIL;
    if (!Decode(Tokens, &Used))
        return E_FAIL;
    if (Current->Opcode == D9SvOpComment)
    {
        DropComment(Fresh);
        return S_OK;
    }
    Current->File = File;
    Current->Line = Line;
    ZeroMemory(SkipSource, sizeof(SkipSource));
    return CheckInstruction() ? S_OK : E_FAIL;
}

HRESULT
D9SvChecker::Finish()
{
    Ended = TRUE;
    CheckEnd();
    return Errors == 0 ? S_OK : E_FAIL;
}

/* Instruction records */

VOID
D9SvSource::Clear()
{
    Present = FALSE;
    Number = MAXUINT;
    Swizzle = D9SvSwizzleIdentity;
    Modifier = 0;
    Type = MAXUINT;
    Read = D9SvMaskAll;
    Relative = 0;
    RelativeComponent = 0;
    RelativeType = MAXUINT;
    RelativeNumber = MAXUINT;
}

/* Fills the register fields from a parameter token */
VOID
D9SvSource::Load(
    _In_ DWORD Token)
{
    Present = TRUE;
    Number = D9SvRegisterNumber(Token);
    Swizzle = Token & 0xFF0000;
    Modifier = Token & 0xF000000;
    Relative = Token & D9SvRelativeBit;
    Type = D9SvRegisterType(Token);
}

/* A relative address names one component; aL takes no swizzle at all */
BOOLEAN
D9SvSource::LoadRelative(
    _In_ DWORD Token)
{
    DWORD Selector = Token & 0xFF0000;

    RelativeNumber = D9SvRegisterNumber(Token);
    RelativeType = D9SvRegisterType(Token);
    if (RelativeType == D9SvRegLoop)
    {
        if (Selector != D9SvSwizzleIdentity)
            return FALSE;
        RelativeComponent = D9SvComponentX;
        return TRUE;
    }
    switch (Selector)
    {
    case D9SvSwizzleX:
        RelativeComponent = D9SvComponentX;
        return TRUE;
    case D9SvSwizzleY:
        RelativeComponent = D9SvComponentY;
        return TRUE;
    case D9SvSwizzleZ:
        RelativeComponent = D9SvComponentZ;
        return TRUE;
    case D9SvSwizzleW:
        RelativeComponent = D9SvComponentW;
        return TRUE;
    default:
        return FALSE;
    }
}

VOID
D9SvInstruction::Clear(
    _In_opt_ D9SvInstruction* Before)
{
    Opcode = 0;
    Controls = 0;
    Predicated = FALSE;
    DestCount = 0;
    SourceCount = 0;
    Length = MAXUINT;
    Previous = Before;
    Next = nullptr;
    File = nullptr;
    Line = -1;
    Statement = 0;
    Offset = MAXSIZE_T;
    DclInfo = 0;
    ZeroMemory(Values, sizeof(Values));
    for (UINT Index = 0; Index < 4; Index++)
        Source[Index].Clear();
    Predicate.Clear();
    Dest.Present = FALSE;
    Dest.Number = MAXUINT;
    Dest.Mask = 0;
    Dest.Modifier = 0;
    Dest.Shift = MAXUINT;
    Dest.Type = MAXUINT;
    Dest.Coordinates = 0;
    Dest.Relative = 0;
    Dest.RelativeComponent = 0;
    Dest.RelativeType = MAXUINT;
    Dest.RelativeNumber = MAXUINT;
    ClearExtra();
    if (Before != nullptr)
        Before->Next = this;
}

VOID
D9SvInstruction::ClearExtra()
{
}

/* Which destination components pick the swizzled source components each source reads */
DWORD
D9SvSwizzled(
    _In_ DWORD Swizzle,
    _In_ DWORD Positions)
{
    DWORD Read = 0;

    for (UINT Position = 0; Position < 4; Position++)
    {
        if ((Positions & D9SvComponent(Position)) != 0)
            Read |= D9SvComponent((Swizzle >> (16 + Position * 2)) & 3);
    }
    return Read;
}

/* Vertex shader instructions: the components each source position stands for */
VOID
D9SvInstruction::ComputeReads(
    _In_ DWORD ShaderVersion)
{
    UNREFERENCED_PARAMETER(ShaderVersion);

    for (UINT Index = 0; Index <= SourceCount; Index++)
    {
        DWORD Positions = 0;
        D9SvSource* Target;

        if (Index == SourceCount)
        {
            if (!Predicated)
                break;
            Target = &Predicate;
            Positions = Dest.Mask;
        }
        else
        {
            Target = &Source[Index];
            switch (Opcode)
            {
            case 1: case 2: case 4: case 5: case 10: case 11: case 12: case 13: case 18: case 19: case 34: case 35:
            case 46: case 94:
                Positions = Dest.Mask;
                break;
            case 6: case 7: case 14: case 15: case 26: case 32: case 40: case 41: case 45: case 78: case 79: case 96:
                Positions = D9SvComponentW;
                break;
            case 8: case 22: case 23: case 24: case 33: case 36:
                Positions = D9SvMaskXyz;
                break;
            case 9: case 20: case 21:
                Positions = D9SvMaskAll;
                break;
            case 16:
                Positions = (Dest.Mask & D9SvComponentZ) != 0 ? 0xB0000 : D9SvComponentX;
                break;
            case 17:
                if (Index == 0)
                    Positions = 0x60000;
                else if (Index == 1)
                    Positions = 0xA0000;
                break;
            case 27:
                if (Index == 0)
                    Positions = D9SvComponentW;
                else if (Index == 1)
                    Positions = D9SvMaskXyz;
                break;
            case 37:
                Positions = Index != 0 ? D9SvMaskAll : D9SvComponentW;
                break;
            case 38:
                Positions = D9SvComponentX;
                break;
            default:
                break;
            }
        }
        Target->Read = D9SvSwizzled(Target->Swizzle, Positions);
    }
}

/* Shared entry points */

/* Builds the checker a version token asks for */
static
D9SvChecker*
D9SvCreateChecker(
    _In_ DWORD Version)
{
    WORD Kind = HIWORD(Version);

    if (Kind == 0xFFFE)
        return D9SvCreateVertexChecker(Version);
    if (Kind == 0xFFFF)
        return D9SvCreatePixelChecker(Version);
    return nullptr;
}

/* Creates and starts a checker for one kind of shader; a token of the other kind gives none */
D9SvChecker*
D9SvStart(
    _In_ D9SvParams* Settings,
    _In_ BOOLEAN Pixel,
    _In_opt_ D9_SHADER_MESSAGE_SINK Sink,
    _In_opt_ PVOID Context)
{
    D9SvChecker* Checker;

    if (HIWORD(Settings->Version) != (Pixel ? 0xFFFF : 0xFFFE))
        return nullptr;
    Checker = D9SvCreateChecker(Settings->Version);
    if (Checker == nullptr)
        return nullptr;
    Checker->Sink = Sink;
    Checker->SinkContext = Context;
    if (FAILED(Checker->Begin(Settings)))
    {
        delete Checker;
        return nullptr;
    }
    return Checker;
}

/* IDirect3DShaderValidator9: a version token starts a checker, instructions arrive one at a time, End reports */
static const GUID D9IidShaderValidator = { 0x814D5A8F, 0x5A68, 0x47F4, { 0xBC, 0xC6, 0xD4, 0xD9, 0x10, 0xB2, 0xA9,
                                                                         0xE5 } };

class D9ShaderValidatorObject
{
public:
    enum : UINT
    {
        WaitingVersion = 0,
        InShader = 1,
        SawEnd = 2,
        Idle = 3,
        Broken = 4,
    };

    virtual
    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID Iid,
        _Out_ void** Object)
    {
        *Object = nullptr;
        if (!IsEqualGUID(Iid, IID_IUnknown) && !IsEqualGUID(Iid, D9IidShaderValidator))
            return E_NOINTERFACE;
        *Object = this;
        AddRef();
        return S_OK;
    }

    virtual
    ULONG
    STDMETHODCALLTYPE
    AddRef()
    {
        References += 1;
        return References;
    }

    virtual
    ULONG
    STDMETHODCALLTYPE
    Release()
    {
        ULONG Left = References - 1;

        References = Left;
        if (Left == 0)
            delete this;
        return Left;
    }

    virtual
    HRESULT
    STDMETHODCALLTYPE
    Begin(
        _In_opt_ D9_SHADER_MESSAGE_SINK Callback,
        _In_opt_ PVOID Context,
        _In_ DWORD BeginFlags)
    {
        if (State != Idle)
        {
            Say(0, 235, "IDirect3DShaderValidator9::Begin called out of order. ::End must be called first.");
            return E_FAIL;
        }
        delete Checker;
        Checker = nullptr;
        Sink = Callback;
        SinkContext = Context;
        State = WaitingVersion;
        Settings.Version = 0;
        Flags = BeginFlags;
        return S_OK;
    }

    virtual
    HRESULT
    STDMETHODCALLTYPE
    Instruction(
        _In_opt_z_ PCSTR File,
        _In_ INT Line,
        _In_reads_opt_(Count) const DWORD* Tokens,
        _In_ DWORD Count)
    {
        if (State == Broken)
            return E_FAIL;
        if (State == Idle)
        {
            Say(0, 236, "IDirect3DShaderValidator9::Instruction called out of order. ::Begin must be called first.");
            State = Broken;
            return E_FAIL;
        }
        if (State == SawEnd)
        {
            Say(0, 237, "IDirect3DShaderValidator9::Instruction called out of order. After end token there should be "
                "no more instructions.  Call ::End next.");
            State = Broken;
            return E_FAIL;
        }
        if (Tokens == nullptr || Count == 0)
        {
            Say(0, 238, "IDirect3DShaderValidator9::Instruction called with NULL == pdwInst or 0 == cdw.");
            State = Broken;
            return E_FAIL;
        }
        if (State == InShader)
        {
            if (Tokens[0] != D9SvOpEnd)
            {
                if (FAILED(Checker->Feed(File, Line, Tokens, Count)))
                {
                    State = Broken;
                    return E_FAIL;
                }
                return S_OK;
            }
            if (Count > 1)
                Say(6, 241, "Bad end token.  DWORD count > 1 given. Expected DWORD count to be 1 for end token.");
            State = SawEnd;
            return S_OK;
        }
        if (Count > 1)
            Say(6, 239, "Bad version token.  DWORD count > 1 given. Expected DWORD count to be 1 for version token.");
        VersionToken = Tokens[0];
        ZeroMemory(&Settings, sizeof(Settings));
        Settings.Tokens = &VersionToken;
        Settings.Version = VersionToken;
        Settings.Interface = TRUE;
        if ((VersionToken & 0xFFFF0000) == 0xFFFF0000)
        {
            Settings.PixelTargetMessages = (Flags & 1) != 0;
            Checker = D9SvStart(&Settings, TRUE, Sink, SinkContext);
        }
        else if ((VersionToken & 0xFFFE0000) == 0xFFFE0000)
        {
            Settings.TargetMessages = (Flags & 1) != 0;
            Checker = D9SvStart(&Settings, FALSE, Sink, SinkContext);
        }
        else
        {
            Say(6, 240, "Bad version token.  It indicates neither a pixel shader nor a vertex shader.");
            State = Broken;
            return E_FAIL;
        }
        if (Checker == nullptr)
        {
            State = Broken;
            return E_FAIL;
        }
        State = InShader;
        return S_OK;
    }

    virtual
    HRESULT
    STDMETHODCALLTYPE
    End()
    {
        if (State == Broken)
            return E_FAIL;
        if (State == Idle)
        {
            Say(0, 242, "IDirect3DShaderValidator9::End called out of order. Call to ::Begin, followed by calls to "
                "::Instruction must occur first.");
            State = Broken;
            return E_FAIL;
        }
        if (State != SawEnd)
        {
            Say(0, 243, "Shader missing end token.");
            State = Broken;
            return E_FAIL;
        }
        State = Idle;
        return Checker->Finish();
    }

    virtual ~D9ShaderValidatorObject()
    {
        delete Checker;
    }

private:
    VOID
    Say(
        _In_ DWORD Level,
        _In_ DWORD Id,
        _In_z_ PCSTR Text)
    {
        if (Sink != nullptr)
            Sink(nullptr, 0, Level, Id, Text, SinkContext);
    }

    UINT State = Idle;
    ULONG References = 1;
    D9_SHADER_MESSAGE_SINK Sink = nullptr;
    PVOID SinkContext = nullptr;
    DWORD Flags = 0;
    DWORD VersionToken = 0;
    D9SvParams Settings = {};
    D9SvChecker* Checker = nullptr;
};

void*
D9CreateShaderValidatorObject(VOID)
{
    return new (std::nothrow) D9ShaderValidatorObject;
}

BOOLEAN
D9ValidateShader(
    _In_ const DWORD* Tokens,
    _In_ BOOLEAN Pixel,
    _In_opt_ const D9_SHADER_CHECK_DEVICE* Device,
    _In_ DWORD Flags,
    _In_opt_ D9_SHADER_MESSAGE_SINK Sink,
    _In_opt_ PVOID Context,
    _Out_ D9_SHADER_CHECK_RESULT* Result)
{
    D9SvParams Settings = {};
    D9SvChecker* Checker;
    BOOLEAN Valid;

    ZeroMemory(Result, sizeof(*Result));
    Settings.Tokens = Tokens;
    Settings.Version = Tokens[0];
    Settings.Device = Device;
    Settings.Flags = Flags;
    Settings.DevicePath = TRUE;
    Checker = D9SvStart(&Settings, Pixel, Sink, Context);
    if (Checker == nullptr)
    {
        Result->Outputs = Settings.Outputs;
        return FALSE;
    }
    Valid = Checker->RunStream();
    delete Checker;
    Result->HardwareCapable = Settings.HardwareCapable;
    Result->SoftwareCapable = Settings.SoftwareCapable;
    Result->Outputs = Settings.Outputs;
    return Valid;
}
