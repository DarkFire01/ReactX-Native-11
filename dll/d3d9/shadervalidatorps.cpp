/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pixel shader rules for ps_2_0, ps_2_x, ps_2_sw, ps_3_0 and ps_3_sw
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shadervalidatorcore.hpp"

#include <new>

static constexpr DWORD D9PsVersion14 = 0xFFFF0104;
static constexpr DWORD D9PsVersion20 = 0xFFFF0200;
static constexpr DWORD D9PsVersion2x = 0xFFFF0201;
static constexpr DWORD D9PsVersion2sw = 0xFFFF02FF;
static constexpr DWORD D9PsVersion30 = 0xFFFF0300;
static constexpr DWORD D9PsVersion3sw = 0xFFFF03FF;

static constexpr UINT D9PsOpTexkill = 65;
static constexpr UINT D9PsOpTexld = 66;
static constexpr UINT D9PsOpCmp = 88;
static constexpr UINT D9PsOpDp2add = 90;
static constexpr UINT D9PsOpDsx = 91;
static constexpr UINT D9PsOpDsy = 92;
static constexpr UINT D9PsOpTexldd = 93;

static constexpr UINT D9PsRegTexture = 3;

/* PS20Caps bits the rules read */
static constexpr DWORD D9PsCapsSwizzle = 0x1;
static constexpr DWORD D9PsCapsGradient = 0x2;
static constexpr DWORD D9PsCapsPredication = 0x4;
static constexpr DWORD D9PsCapsDependentReads = 0x8;
static constexpr DWORD D9PsCapsTexLimit = 0x10;

static const PCSTR D9PsOrdinals[] = { "first", "second", "third", "fourth" };

/* The swizzles a ps_2_0 source may use without arbitrary swizzle support */
static
BOOLEAN
D9PsBasicSwizzle(
    _In_ DWORD Swizzle)
{
    switch (Swizzle)
    {
    case 0x000000:
    case 0x550000:
    case 0xAA0000:
    case 0xFF0000:
    case 0xE40000:
    case 0xC90000:
    case 0xD20000:
    case 0x1B0000:
        return TRUE;
    default:
        return FALSE;
    }
}

static
BOOLEAN
D9PsIsXOrY(
    _In_ DWORD Swizzle)
{
    return Swizzle == D9SvSwizzleX || Swizzle == D9SvSwizzleY;
}

/* ".rgba/xyzw" style text for a swizzle */
static
VOID
D9PsSwizzleName(
    _In_ DWORD Swizzle,
    _Out_writes_(32) PSTR Text)
{
    static const char Colors[] = "rgba";
    static const char Axes[] = "xyzw";
    char Built[16];
    UINT Length = 0;

    Built[Length++] = '.';
    if (D9SvReplicate(Swizzle))
    {
        Built[Length++] = Colors[(Swizzle >> 16) & 3];
        Built[Length++] = '/';
        Built[Length++] = Axes[(Swizzle >> 16) & 3];
    }
    else
    {
        for (UINT Position = 0; Position < 4; Position++)
            Built[Length++] = Colors[(Swizzle >> (16 + 2 * Position)) & 3];
        Built[Length++] = '/';
        for (UINT Position = 0; Position < 4; Position++)
            Built[Length++] = Axes[(Swizzle >> (16 + 2 * Position)) & 3];
    }
    Built[Length] = '\0';
    strcpy_s(Text, 32, Built);
}

/* A pixel shader instruction with the facts the pixel rules add */
struct D9PsInstruction : public D9SvInstruction
{
    BOOLEAN TexOp;
    BOOLEAN CoIssue;
    DWORD DestRead;

    VOID
    ClearExtra() override
    {
        TexOp = FALSE;
        CoIssue = FALSE;
        DestRead = 0;
    }

    VOID
    ComputeReads(
        _In_ DWORD ShaderVersion) override;
};

/* The components each source position stands for in pixel shaders */
VOID
D9PsInstruction::ComputeReads(
    _In_ DWORD ShaderVersion)
{
    if (Opcode == D9PsOpTexkill)
        DestRead = ShaderVersion >= D9PsVersion30 ? Dest.Mask : D9SvMaskXyz;

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
            case 1: case 2: case 4: case 5: case 10: case 11: case 18: case 19: case 35: case 88: case 91: case 92:
            case 94:
                Positions = Dest.Mask;
                break;
            case 6: case 7: case 14: case 15: case 26: case 32: case 40: case 41: case 45: case 96:
                Positions = D9SvComponentW;
                break;
            case 8: case 22: case 23: case 24: case 33: case 36:
                Positions = D9SvMaskXyz;
                break;
            case 9: case 20: case 21:
                Positions = D9SvMaskAll;
                break;
            case 27:
                if (Index == 0)
                    Positions = D9SvComponentW;
                else if (Index == 1)
                    Positions = D9SvMaskXyz;
                break;
            case 37:
                Positions = Index == 0 ? D9SvComponentW : D9SvMaskAll;
                break;
            case 38:
                Positions = D9SvComponentX;
                break;
            case 90:
                Positions = Index == 2 ? D9SvComponentX : 0x30000;
                break;
            default:
                break;
            }
        }
        Target->Read = D9SvSwizzled(Target->Swizzle, Positions);
    }
}

/* One vs_3_0 style input declaration: semantic, register and components */
struct D9PsInput
{
    BYTE Semantic;
    UINT Register;
    DWORD Mask;
};

class D9PsChecker : public D9SvChecker
{
public:
    UINT Model = 2;
    BOOLEAN Basic = FALSE;
    BOOLEAN DefOpen = TRUE;
    BOOLEAN DclOpen = TRUE;
    BOOLEAN AfterRet = FALSE;
    BOOLEAN DepthWritten = FALSE;
    DWORD ColorsWritten = 0;
    DWORD CubeSamplers = 0;
    UINT TexSlots = 0;
    UINT ArithSlots = 0;
    UINT TotalSlots = 0;
    UINT TexLimit = 0;
    UINT ArithLimit = 0;
    UINT TotalLimit = 0;
    UINT CallLimit = 0;
    UINT LoopLimit = 0;
    UINT StaticIfLimit = 0;
    UINT DynamicIfLimit = 0;
    UINT Chain[32] = {};
    BYTE Clobbered[4][32] = {};
    D9PsInput Inputs3[56] = {};
    UINT InputCount3 = 0;
    UINT Centroid[14] = {};
    D9SvLabelSet Called = {};
    D9SvLabelSet Defined = {};
    D9SvFlow* Flow = nullptr;
    D9SvRegisters Temps;
    D9SvRegisters Inputs;
    D9SvRegisters Consts;
    D9SvRegisters IntConsts;
    D9SvRegisters BoolConsts;
    D9SvRegisters Texcoords;
    D9SvRegisters Colors;
    D9SvRegisters Depth;
    D9SvRegisters Predicates;
    D9SvRegisters Labels;
    D9SvRegisters Samplers;
    D9SvRegisters Misc;
    D9SvRegisters LoopCounter;

    ~D9PsChecker() override
    {
        delete Flow;
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

    D9SvInstruction*
    NewInstruction() override
    {
        return new (std::nothrow) D9PsInstruction;
    }

    D9PsInstruction*
    Ins() const
    {
        return static_cast<D9PsInstruction*>(Current);
    }

    BOOLEAN
    TargetMessages() const
    {
        return Params->PixelTargetMessages;
    }

    DWORD
    PsCaps() const
    {
        return Caps->PS20Caps.Caps;
    }

    BOOLEAN
    Begin2();

    BOOLEAN
    Begin3();

    BOOLEAN
    Recognized();

    BOOLEAN
    Supported2();

    BOOLEAN
    Supported3();

    BOOLEAN
    ParamCount();

    VOID
    SourceParams2();

    VOID
    SourceParams3();

    VOID
    SourcePredicate();

    VOID
    SourceInitialized2();

    VOID
    SourceInitialized3();

    VOID
    DestParam2();

    VOID
    DestParam3();

    VOID
    Ports2();

    VOID
    Ports3();

    VOID
    Definitions();

    VOID
    Declaration2();

    VOID
    Declaration3();

    VOID
    Arithmetic();

    VOID
    ColorWrite();

    VOID
    DepthWrite2();

    VOID
    DepthWrite3();

    BOOLEAN
    FlowControl();

    VOID
    FlowControlEnd();

    VOID
    Gradients();

    VOID
    CountSlots2();

    VOID
    CountSlots3();

    VOID
    CountSlotsEnd();

    VOID
    LogInputs();

    BOOLEAN
    ReportNesting(
        _In_ DWORD TargetId,
        _In_z_ PCSTR TargetFormat,
        _In_ DWORD Id,
        _In_z_ PCSTR Format,
        _In_ UINT Limit);

    VOID
    SourcePrefix(
        _In_ UINT Index,
        _Out_writes_(32) PSTR Prefix) const;
};

D9SvChecker*
D9SvCreatePixelChecker(
    _In_ DWORD Version)
{
    D9PsChecker* Checker;

    if (Version < D9PsVersion14)
        return D9SvCreatePixel1Checker(Version, FALSE);
    if (Version < D9PsVersion20)
        return D9SvCreatePixel1Checker(Version, TRUE);
    Checker = new (std::nothrow) D9PsChecker;
    if (Checker == nullptr)
        return nullptr;
    Checker->Model = Version < D9PsVersion30 ? 2 : 3;
    Checker->Flow = new (std::nothrow) D9SvFlow;
    if (Checker->Flow == nullptr)
    {
        delete Checker;
        return nullptr;
    }
    Checker->Flow->Reset();
    return Checker;
}

/* Version checks, register files and the limits of each model */
HRESULT
D9PsChecker::Begin(
    _In_ D9SvParams* Settings)
{
    WORD Kind;

    Settings->HardwareCapable = TRUE;
    Settings->SoftwareCapable = TRUE;
    Bind(Settings);
    Params->Outputs = MAXDWORD;
    Kind = HIWORD(Version);
    if (Kind == 0xFFFE)
    {
        Report(6, Model == 2 ? 186 : 502, "Version Token: 0x%x indicates a vertex shader.  Pixel shader version token "
               "must be of the form 0xffff****.", Version);
        return E_FAIL;
    }
    if (Kind != 0xFFFF)
    {
        Report(6, Model == 2 ? 187 : 517, "Version Token: 0x%x is invalid. Pixel shader version token must be of the "
               "form 0xffff****. Aborting pixel shader validation.", Version);
        return E_FAIL;
    }
    if (!(Model == 2 ? Begin2() : Begin3()))
        return E_FAIL;
    Ready = TRUE;
    return S_OK;
}

BOOLEAN
D9PsChecker::Begin2()
{
    char Name[32];
    BOOLEAN Made;

    if (Caps != nullptr)
    {
        DWORD Wanted = Version == D9PsVersion2x ? 0x200 : LOWORD(Version);

        if (LOWORD(Caps->PixelShaderVersion) < Wanted && (Wanted & 0xFF) != 0xFF)
        {
            D9SvVersionName(Version, Name);
            Report(6, 188, "Version Token: Shader version %s is not supported by device. ", Name);
            return FALSE;
        }
    }

    switch (Version)
    {
    case D9PsVersion20:
        Made = Inputs.Create(2, FALSE, 1) && Consts.Create(32, FALSE, 1) && IntConsts.Create(0, FALSE, 1) &&
               BoolConsts.Create(0, FALSE, 1) && Texcoords.Create(8, FALSE, 1) && Temps.Create(12, TRUE, 3) &&
               Colors.Create(4, TRUE, 0) && Depth.Create(1, TRUE, 0) && Samplers.Create(16, FALSE, 1) &&
               Predicates.Create(0, TRUE, 1) && Labels.Create(0, FALSE, 1);
        Basic = TRUE;
        break;
    case D9PsVersion2x:
    {
        UINT TempCount = 32;
        UINT Static = 4;
        UINT Dynamic = 24;

        if (Caps != nullptr)
        {
            TempCount = static_cast<UINT>(D9SvMax(12, D9SvMin(32, Caps->PS20Caps.NumTemps)));
            Static = static_cast<UINT>(D9SvMax(0, D9SvMin(4, Caps->PS20Caps.StaticFlowControlDepth)));
            Dynamic = static_cast<UINT>(D9SvMax(0, D9SvMin(24, Caps->PS20Caps.DynamicFlowControlDepth)));
        }
        Made = Inputs.Create(2, FALSE, 1) && Consts.Create(32, FALSE, 1) && IntConsts.Create(16, FALSE, 1) &&
               BoolConsts.Create(16, FALSE, 1) && Texcoords.Create(8, FALSE, 1) && Temps.Create(TempCount, TRUE, 3) &&
               Colors.Create(4, TRUE, 0) && Depth.Create(1, TRUE, 0) && Samplers.Create(16, FALSE, 1) &&
               Predicates.Create(1, TRUE, 1) && Labels.Create(0x800, FALSE, 1);
        CallLimit = Static;
        LoopLimit = Static;
        StaticIfLimit = (Caps == nullptr || Static != 0) ? 24 : 0;
        DynamicIfLimit = Dynamic;
        Basic = Caps != nullptr && Static == 0 && Dynamic == 0 && StaticIfLimit == 0 &&
                (PsCaps() & (D9PsCapsPredication | D9PsCapsDependentReads)) == 0;
        break;
    }
    case D9PsVersion2sw:
        if (DeviceType == D3DDEVTYPE_HAL)
        {
            Report(6, 0x160, "Version Token: Pixel shader version ps_2_sw is for software rasterizers only (e.g. the "
                   "reference device). Aborting shader validation.");
            return FALSE;
        }
        Made = Inputs.Create(2, FALSE, 3) && Consts.Create(0x2000, FALSE, 3) && IntConsts.Create(0x800, FALSE, 1) &&
               BoolConsts.Create(0x800, FALSE, 1) && Texcoords.Create(8, FALSE, 3) && Temps.Create(32, TRUE, 3) &&
               Colors.Create(4, TRUE, 0) && Depth.Create(1, TRUE, 0) && Samplers.Create(16, FALSE, 1) &&
               Predicates.Create(1, TRUE, 1) && Labels.Create(0x800, FALSE, 1);
        CallLimit = 4;
        LoopLimit = 4;
        StaticIfLimit = 24;
        DynamicIfLimit = 24;
        break;
    default:
        D9SvVersionName(Version, Name);
        Report(6, 189, "Version Token: Shader version %s is not supported. Aborting pixel shader validation.", Name);
        return FALSE;
    }
    if (!Made)
    {
        Report(6, 0, "Out of memory.");
        return FALSE;
    }

    if (Version == D9PsVersion2sw)
    {
        TexLimit = MAXUINT;
        ArithLimit = MAXUINT;
        TotalLimit = MAXUINT;
    }
    else if (Version == D9PsVersion2x)
    {
        TotalLimit = 512;
        if (Caps != nullptr)
            TotalLimit = static_cast<UINT>(D9SvMax(96, D9SvMin(512, Caps->PS20Caps.NumInstructionSlots)));
        if (Caps != nullptr && (PsCaps() & D9PsCapsTexLimit) == 0)
        {
            TexLimit = 32;
            ArithLimit = TotalLimit - 32;
        }
        else
        {
            TexLimit = TotalLimit;
            ArithLimit = TotalLimit;
        }
    }
    else
    {
        TotalLimit = 96;
        TexLimit = 32;
        ArithLimit = 64;
    }
    return TRUE;
}

BOOLEAN
D9PsChecker::Begin3()
{
    char Name[32];
    BOOLEAN Made;

    if (Caps != nullptr && LOWORD(Caps->PixelShaderVersion) < LOWORD(Version) && LOBYTE(Version) != 0xFF)
    {
        D9SvVersionName(Version, Name);
        Report(6, 214, "Version Token: Shader version %s is not supported by device. ", Name);
        return FALSE;
    }
    if (Version == D9PsVersion3sw)
    {
        if (DeviceType == D3DDEVTYPE_HAL)
        {
            Report(6, 0x282, "Version Token: Pixel shader version ps_3_sw is for use with software rasterizers only "
                   "(e.g. the reference device). Aborting shader validation.");
            return FALSE;
        }
        Made = Temps.Create(32, TRUE, 3) && Inputs.Create(14, FALSE, 3) && Consts.Create(0x2000, FALSE, 3) &&
               LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(0x800, FALSE, 1) &&
               BoolConsts.Create(0x800, FALSE, 1) && Predicates.Create(1, TRUE, 1) && Colors.Create(4, TRUE, 0) &&
               Depth.Create(1, TRUE, 0) && Misc.Create(2, FALSE, 3) && Samplers.Create(16, FALSE, 1) &&
               Labels.Create(0x800, FALSE, 1);
    }
    else if (Version == D9PsVersion30)
    {
        Made = Temps.Create(32, TRUE, 3) && Inputs.Create(10, FALSE, 1) && Consts.Create(224, FALSE, 1) &&
               LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(16, FALSE, 1) && BoolConsts.Create(16, FALSE, 1) &&
               Predicates.Create(1, TRUE, 1) && Colors.Create(4, TRUE, 0) && Depth.Create(1, TRUE, 0) &&
               Misc.Create(2, FALSE, 1) && Samplers.Create(16, FALSE, 1) && Labels.Create(0x800, FALSE, 1);
    }
    else
    {
        D9SvVersionName(Version, Name);
        Report(6, 222, "Version Token: Shader version %s is not supported. Aborting pixel shader validation.", Name);
        return FALSE;
    }
    if (!Made)
    {
        Report(6, 0, "Out of memory.");
        return FALSE;
    }
    CallLimit = 4;
    LoopLimit = 4;
    StaticIfLimit = 24;
    DynamicIfLimit = 24;
    TotalLimit = (Version == D9PsVersion30 && Caps != nullptr) ? Caps->MaxPixelShader30InstructionSlots : MAXUINT;
    return TRUE;
}

/* Splits one instruction into its parameters; malformed encodings stop validation */
BOOLEAN
D9PsChecker::Decode(
    _In_ const DWORD* Tokens,
    _Inout_ UINT* Used)
{
    D9PsInstruction* Instr = Ins();
    UINT Limit = Stream ? MAXUINT : *Used;
    UINT Position = 1;
    DWORD Reserved;
    DWORD Token = Tokens[0];
    BOOLEAN Special;

    *Used = 0;
    Instr->Opcode = Token & 0xFFFF;
    if (Instr->Opcode == D9SvOpComment)
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

    switch (Instr->Opcode)
    {
    case 64: case D9PsOpTexkill: case D9PsOpTexld: case D9PsOpTexldd:
        Instr->TexOp = TRUE;
        break;
    case 95:
        Instr->TexOp = Model == 3;
        break;
    default:
        break;
    }
    if ((Token & 0x40000000) != 0)
        Instr->CoIssue = TRUE;
    Statements += 1;
    Instr->Statement = Statements;
    Instr->Offset = Stream ? static_cast<SIZE_T>(Tokens - Start) : MAXSIZE_T;

    Special = Instr->Opcode == D9SvOpIfc || Instr->Opcode == D9SvOpBreakc || Instr->Opcode == D9PsOpTexld ||
              Instr->Opcode == D9SvOpSetp;
    if (Version < D9PsVersion2x)
        Reserved = Special ? 0xB0000000 : 0xB0FF0000;
    else
        Reserved = Special ? 0xA0000000 : 0xA0FF0000;
    if ((Token & Reserved) != 0)
    {
        Report(2, 1, "Reserved bit(s) set in instruction parameter token!  Aborting validation.");
        return FALSE;
    }
    Instr->Controls = Token & 0xFF0000;
    Instr->Length = (Token >> 24) & 0xF;
    if (Version >= D9PsVersion2x && (Token & 0x10000000) != 0)
        Instr->Predicated = TRUE;

    if (Instr->Opcode == D9SvOpDcl)
    {
        if ((Token & 0xF000000) != 0x2000000)
        {
            Report(2, 2, "dcl instruction should have exactly 2 parameter tokens.  Aborting validation.");
            return FALSE;
        }
        if (Limit == 0)
            goto Done;
        *Used = 1;
        if (Limit == 1)
            goto Done;
        if ((Tokens[1] & 0x80000000) == 0)
        {
            Report(2, 541, "Bit 31 not set in dcl info token!  Aborting validation.");
            *Used = 0;
            return FALSE;
        }
        Instr->DclInfo = Tokens[1] & 0x7FFFFFFF;
        Position = 2;
    }

    if (Position > Limit)
        goto Done;
    *Used = Position;
    if (Position == Limit)
        goto Done;

    switch (Instr->Opcode)
    {
    case 25: case 26: case 27: case 28: case 29: case 30: case 38: case 39: case 40: case 41: case 42: case 43:
    case 44: case 45: case 96: case D9SvOpEnd:
        goto Sources;
    default:
        break;
    }

    if ((Tokens[Position] & 0x80000000) != 0)
    {
        DWORD Dest = Tokens[Position];

        Instr->DestCount += 1;
        Instr->Dest.Present = TRUE;
        Instr->Dest.Number = D9SvRegisterNumber(Dest);
        Instr->Dest.Mask = Dest & 0xF0000;
        Instr->Dest.Modifier = Dest & 0xF00000;
        Instr->Dest.Shift = (Dest >> 24) & 0xF;
        Instr->Dest.Type = D9SvRegisterType(Dest);
        Instr->Dest.Relative = Dest & D9SvRelativeBit;
        if ((Dest & 0xE000) != 0)
        {
            Report(2, 3, "Reserved bit(s) set in destination parameter token!  Aborting validation.");
            *Used = 0;
            return FALSE;
        }
        if (Instr->Opcode == D9SvOpDcl)
        {
            DWORD InfoReserved;

            if (Instr->Dest.Type == D9SvRegSampler)
                InfoReserved = 0x7FFFFFF;
            else
                InfoReserved = Version < D9PsVersion30 ? 0x7FFFFFFF : 0x7870FFF0;
            if ((Instr->DclInfo & InfoReserved) != 0)
            {
                Report(2, 487, "Reserved bit(s) set in dcl info token!  Aborting validation.");
                *Used = 0;
                return FALSE;
            }
        }
        if (Position + 1 > Limit)
            goto Done;
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;

        /* Definitions carry their values instead of sources */
        if (Instr->Opcode == D9SvOpDefb || Instr->Opcode == D9SvOpDefi || Instr->Opcode == D9SvOpDef)
        {
            UINT Total = Position + (Instr->Opcode == D9SvOpDefb ? 2 : 5);

            if (!Stream && Total != Limit)
            {
                Report(2, 5, "Unexpected DWORD count for instruction.  Aborting validation.");
                return FALSE;
            }
            if (Instr->Opcode == D9SvOpDefb)
                Instr->Values[0] = Tokens[Position + 1];
            else
                CopyMemory(Instr->Values, &Tokens[Position + 1], sizeof(Instr->Values));
            *Used = Total;
            if (!Stream && Total == Limit)
                goto Done;
            return TRUE;
        }
        Position += 1;
    }

Sources:
    if (Instr->Predicated)
    {
        Instr->Predicate.Load(Tokens[Position]);
        if (Position + 1 > Limit)
            goto Done;
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;
        Position += 1;
    }
    while ((Tokens[Position] & 0x80000000) != 0)
    {
        DWORD Param = Tokens[Position];
        DWORD SourceReserved = Version < D9PsVersion30 ? 0xE000 : 0xC000;
        D9SvSource* Source;

        Instr->SourceCount += 1;
        if (Instr->DestCount + Instr->SourceCount > 5 || Instr->SourceCount > 4)
        {
            Instr->SourceCount -= 1;
            Report(2, 5, "Unexpected DWORD count for instruction.  Aborting validation.");
            return FALSE;
        }
        Source = &Instr->Source[Instr->SourceCount - 1];
        Source->Load(Param);
        if ((Param & SourceReserved) != 0)
        {
            Report(2, 4, "Reserved bit(s) set in source %d parameter token!  Aborting validation.",
                   Instr->SourceCount);
            *Used = 0;
            return FALSE;
        }
        if (Version > D9PsVersion20 && Source->Relative == D9SvRelativeBit)
        {
            DWORD RelToken;

            if (Position + 1 > Limit)
            {
                *Used = Position;
                goto Done;
            }
            *Used = Position + 1;
            if (Position + 1 == Limit)
                goto Done;
            RelToken = Tokens[Position + 1];
            if ((RelToken & 0x80000000) == 0)
            {
                Report(2, 474, "Expected relative address token.  Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            if ((RelToken & 0x0F00E000) != 0)
            {
                Report(2, 476, "Reserved bit(s) set in relative address token for source parameter %d!  Aborting "
                       "validation.", Instr->SourceCount);
                *Used = 0;
                return FALSE;
            }
            if (!Source->LoadRelative(RelToken))
            {
                if (Source->RelativeType == D9SvRegLoop)
                    Report(2, 371, "When aL is used as a relative address, it must not specify any swizzle, as it is "
                           "a scalar register. Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            Position += 1;
        }
        if (Position + 1 > Limit)
        {
            *Used = Position;
            goto Done;
        }
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;
        Position += 1;
    }

Done:
    Instr->ComputeReads(Version);
    if (Instr->Opcode != D9SvOpComment && Instr->Length + 1 != *Used)
    {
        Report(2, 0x21E, "Instruction length specified for instruction (%d) does not match the token count "
               "encountered (%d). Aborting validation.", Instr->Length, *Used - 1);
        *Used = 0;
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
D9PsChecker::Recognized()
{
    UINT Op = Current->Opcode;

    if ((Op <= 48 && Op != 12 && Op != 13 && Op != 16 && Op != 17 && Op != 34 && Op != 46) ||
        (Op >= 64 && Op <= 96 && Op != 75 && Op != 78 && Op != 79) || Op == D9SvOpPhase)
        return TRUE;
    Fail(2, 6, "Unrecognized instruction. Aborting pixel shader validation.");
    return FALSE;
}

/* Instructions of ps_2_0, and the ps_2_x ones each depend on a device cap */
BOOLEAN
D9PsChecker::Supported2()
{
    UINT Op = Current->Opcode;
    char Name[32];
    DWORD Needed = 0;
    DWORD Id = 0;
    PCSTR Text = nullptr;

    switch (Op)
    {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 14: case 15:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 31: case 32: case 33: case 35: case 36:
    case 37: case 65: case 66: case 81: case 88: case 90:
        return TRUE;
    default:
        break;
    }

    if (Version >= D9PsVersion2x)
    {
        enum : UINT
        {
            NeedNone,
            NeedStaticOrPredicate,
            NeedStatic,
            NeedStaticDefine,
            NeedDynamic,
            NeedGradient,
            NeedPredicate,
        };

        switch (Op)
        {
        case 26: case 28: case 30: case 40: case 42: case 43:
            Needed = NeedStaticOrPredicate;
            break;
        case 25: case 38: case 39: case 44:
            Needed = NeedStatic;
            break;
        case 47: case 48:
            Needed = NeedStaticDefine;
            break;
        case 41: case 45:
            Needed = NeedDynamic;
            break;
        case 91: case 92: case 93:
            Needed = NeedGradient;
            break;
        case 94: case 96:
            Needed = NeedPredicate;
            break;
        default:
            Needed = NeedNone;
            break;
        }
        if (Needed != NeedNone)
        {
            if (Version == D9PsVersion2sw || Caps == nullptr)
                return TRUE;
            switch (Needed)
            {
            case NeedStaticOrPredicate:
                if (Caps->PS20Caps.StaticFlowControlDepth != 0 || (PsCaps() & D9PsCapsPredication) != 0)
                    return TRUE;
                Id = 649;
                Text = "ps_2_x extended instruction not supported by the hardware (neither predication or static flow "
                       "control supported).";
                break;
            case NeedStatic:
            case NeedStaticDefine:
                if (Caps->PS20Caps.StaticFlowControlDepth != 0)
                    return TRUE;
                Id = Needed == NeedStatic ? 650 : 1080;
                Text = "ps_2_x extended instruction not supported by the hardware (static flow control not "
                       "supported).";
                break;
            case NeedDynamic:
                if (Caps->PS20Caps.DynamicFlowControlDepth != 0)
                    return TRUE;
                Id = 648;
                Text = "ps_2_x extended instruction not supported by the hardware (dynamic flow control not "
                       "supported).";
                break;
            case NeedGradient:
                if ((PsCaps() & D9PsCapsGradient) != 0)
                    return TRUE;
                Id = 356;
                Text = "ps_2_x extended instruction not supported by the hardware (gradient instructions not "
                       "supported).";
                break;
            default:
                if ((PsCaps() & D9PsCapsPredication) != 0)
                    return TRUE;
                Id = 647;
                Text = "ps_2_x extended instruction not supported by the hardware (predication not supported).";
                break;
            }
            Fail(2, Id, "%s", Text);
            return FALSE;
        }
    }

    if (Op == D9SvOpEnd || Op == D9SvOpNop)
        return TRUE;
    D9SvVersionName(Version, Name);
    Fail(2, 0xBE, "Instruction not supported by version %s pixel shader.", Name);
    return FALSE;
}

BOOLEAN
D9PsChecker::Supported3()
{
    UINT Op = Current->Opcode;
    char Name[32];

    switch (Op)
    {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 14: case 15:
    case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29:
    case 30: case 31: case 32: case 33: case 35: case 36: case 37: case 38: case 39: case 40: case 41: case 42:
    case 43: case 44: case 45: case 47: case 48: case 65: case 66: case 81: case 88: case 90: case 91: case 92:
    case 93: case 94: case 95: case 96: case D9SvOpEnd:
        return TRUE;
    default:
        break;
    }
    D9SvVersionName(Version, Name);
    Fail(2, 0x206, "Instruction not supported by version %s shader.", Name);
    return FALSE;
}

/* How many destinations and sources each instruction takes */
BOOLEAN
D9PsChecker::ParamCount()
{
    D9SvInstruction* Instr = Current;
    UINT Dests = Instr->DestCount;
    UINT Sources = Instr->SourceCount;
    BOOLEAN Bad = Dests + Sources > 5;
    INT WantDests = -1;
    UINT WantSources = 0;

    switch (Instr->Opcode)
    {
    case 0: case 28: case 39: case 42: case 43: case 44:
        WantDests = 0;
        WantSources = 0;
        break;
    case 29:
        if (Model == 3)
        {
            WantDests = 0;
            WantSources = 0;
        }
        break;
    case 37:
        WantDests = 1;
        WantSources = Model == 3 ? 1 : 3;
        break;
    case 1: case 6: case 7: case 14: case 15: case 19: case 35: case 36: case 91: case 92:
        WantDests = 1;
        WantSources = 1;
        break;
    case 2: case 5: case 8: case 9: case 10: case 11: case 20: case 21: case 22: case 23: case 24: case 32: case 33:
    case 66: case 94:
        WantDests = 1;
        WantSources = 2;
        break;
    case 95:
        if (Model == 3)
        {
            WantDests = 1;
            WantSources = 2;
        }
        break;
    case 4: case 18: case 88: case 90:
        WantDests = 1;
        WantSources = 3;
        break;
    case 25: case 30: case 38: case 40: case 96:
        WantDests = 0;
        WantSources = 1;
        break;
    case 27:
        if (Model == 3)
        {
            WantDests = 0;
            WantSources = 2;
        }
        break;
    case 26: case 41: case 45:
        WantDests = 0;
        WantSources = 2;
        break;
    case 31: case 47: case 48: case 65: case 81:
        WantDests = 1;
        WantSources = 0;
        break;
    case 93:
        WantDests = 1;
        WantSources = 4;
        break;
    default:
        break;
    }
    if (WantDests >= 0)
        Bad = !(Dests == static_cast<UINT>(WantDests) && Sources == WantSources);
    if (!Bad)
        return TRUE;
    Fail(2, Model == 2 ? 0xBF : 0x209, "Invalid parameter count.");
    return FALSE;
}

VOID
D9PsChecker::SourcePrefix(
    _In_ UINT Index,
    _Out_writes_(32) PSTR Prefix) const
{
    static const PCSTR Names[] =
    {
        "(First source param) ", "(Second source param) ", "(Third source param) ", "(Fourth source param) ",
    };

    if (Index == 0 && Current->SourceCount == 1)
        strcpy_s(Prefix, 32, "(Source param) ");
    else
        strcpy_s(Prefix, 32, Names[Index]);
}

/* ps_2_*: register types, numbers, modifiers and swizzles of each source */
VOID
D9PsChecker::SourceParams2()
{
    D9PsInstruction* Instr = Ins();
    UINT Op = Instr->Opcode;
    PCSTR OpName = D9SvOpcodeName(Op);

    for (UINT Index = 0; Index < Instr->SourceCount; Index++)
    {
        D9SvSource* Source = &Instr->Source[Index];
        DWORD Swizzle = Source->Swizzle;
        char Prefix[32];
        BOOLEAN Bad = FALSE;
        UINT Limit = 0;
        UINT Rows = 1;

        SourcePrefix(Index, Prefix);
        if (Instr->TexOp)
        {
            if (Op == D9PsOpTexld || Op == D9PsOpTexldd)
            {
                if (Index == 0)
                {
                    if (Source->Type == D9SvRegTemp)
                    {
                        Limit = Temps.Count;
                    }
                    else if (Source->Type == D9PsRegTexture)
                    {
                        Limit = Texcoords.Count;
                    }
                    else
                    {
                        Fail(2, 192, "First source parameter for texld must be temp (r#) or texture coordinate "
                             "input(t#) register.");
                        SkipSource[Index] = TRUE;
                        continue;
                    }
                }
                else if (Index == 1)
                {
                    if (Source->Type != D9SvRegSampler)
                    {
                        Fail(2, 193, "Second source parameter for texld must be sampler stage (s#).");
                        SkipSource[Index] = TRUE;
                        continue;
                    }
                    Limit = Samplers.Count;
                }
                if (Op == D9PsOpTexldd && Index > 1)
                {
                    switch (Source->Type)
                    {
                    case D9SvRegTemp:
                        Limit = Temps.Count;
                        break;
                    case D9SvRegInput:
                        Limit = Inputs.Count;
                        break;
                    case D9SvRegConst:
                        Limit = Consts.Count;
                        break;
                    case D9PsRegTexture:
                        Limit = Texcoords.Count;
                        break;
                    default:
                        Fail(2, 523, "Third and fourth parameters to texldd must be temp (r#), texture (t#), input "
                             "(v#) or constant (c#) register.");
                        SkipSource[Index] = TRUE;
                        continue;
                    }
                }
            }
            if (Limit == 0)
            {
                Fail(2, 0xC3, "%sUnexpected source parameter.", Prefix);
                Bad = TRUE;
            }
            if (Source->Number >= Limit)
            {
                Fail(2, 0xC4, "%sInvalid register number: %d.  Max allowed for this type is %d.", Prefix,
                     Source->Number, static_cast<INT>(Limit) - 1);
                Bad = TRUE;
            }
            if (Source->Modifier != 0)
            {
                Fail(2, 0xC5, "%sSource modifier not allowed for tex* instruction.", Prefix);
                Bad = TRUE;
            }
            if (Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 198, "%sSource swizzles not allowed on tex* instruction.", Prefix);
                Bad = TRUE;
            }
            if (Bad)
                SkipSource[Index] = TRUE;
            continue;
        }

        /* Register file the source names */
        if (Op == 30)
        {
            if (Source->Type != D9SvRegLabel)
            {
                Fail(2, 515, "%s instruction requires parameter to be label (l#). ", D9SvOpcodeName(30));
                Bad = TRUE;
                goto Modifiers;
            }
            Limit = Labels.Count;
        }
        else if (Op == 38)
        {
            if (Source->Type != D9SvRegConstInt)
            {
                Fail(2, 928, "rep requires parameter to be integer constant register (i#). ");
                Bad = TRUE;
                goto Modifiers;
            }
            Limit = IntConsts.Count;
        }
        else if (Op == 25 || Op == 26 || Op == 40)
        {
            if (Index == 0 && Op != 40)
            {
                if (Source->Type != D9SvRegLabel)
                {
                    Fail(2, 927, "%s requires the first parameter to be a label (l#). ", OpName);
                    Bad = TRUE;
                    goto Modifiers;
                }
                Limit = Labels.Count;
                goto Number;
            }
            if (Index >= 2 && Op != 40)
            {
                Limit = 0;
            }
            else
            {
                PCSTR Which = Index == 1 ? " (second)" : "";

                if (Version == D9PsVersion2x && Op != 25 && Caps != nullptr)
                {
                    if ((PsCaps() & D9PsCapsPredication) == 0 && Source->Type != D9SvRegConstBool)
                    {
                        Fail(2, 513, "On current device, %s requires the condition%s parameter to be boolean "
                             "constant register (b#). ", OpName, Which);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                    if (Caps->PS20Caps.StaticFlowControlDepth == 0 && Source->Type != D9SvRegPredicate)
                    {
                        Fail(2, 913, "On current device, %s requires the condition%s parameter to be predicate "
                             "register (p0). ", OpName, Which);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                }
                if ((Caps == nullptr || Version == D9PsVersion2sw) && Op != 25 &&
                    Source->Type != D9SvRegConstBool && Source->Type != D9SvRegPredicate)
                {
                    Fail(2, 914, "%s requires the condition parameter%s to be boolean constant register (b#), or "
                         "predicate register (p0). ", OpName, Which);
                    Bad = TRUE;
                    goto Modifiers;
                }
                if (Version == D9PsVersion20 && Op != 25 && Source->Type != D9SvRegConstBool)
                {
                    Fail(2, 915, "%s requires the condition parameter%s to be boolean constant register (b#). ",
                         OpName, Which);
                    Bad = TRUE;
                    goto Modifiers;
                }
                Limit = Source->Type == D9SvRegConstBool ? BoolConsts.Count : Predicates.Count;
            }
        }
        else if (Op == 96)
        {
            if (Source->Type != D9SvRegPredicate)
            {
                Fail(2, 926, "breakp requires parameter to be predicate register (p0). ");
                Bad = TRUE;
                goto Modifiers;
            }
            Limit = Predicates.Count;
        }
        else
        {
            switch (Source->Type)
            {
            case D9SvRegTemp:
                Limit = Temps.Count;
                break;
            case D9SvRegInput:
                Limit = Inputs.Count;
                break;
            case D9SvRegConst:
                Limit = Consts.Count;
                break;
            case D9PsRegTexture:
                Limit = Texcoords.Count;
                break;
            default:
                Fail(2, 199, "%sInvalid source register type for instruction.", Prefix);
                Bad = TRUE;
                goto Modifiers;
            }
        }
        if (Index == 1)
            Rows = D9SvMatrixRows(Op);

    Number:
        if (Source->Number >= Limit)
        {
            Fail(2, 0xC8, "%sInvalid register number: %d. Max allowed for this type is %d.", Prefix, Source->Number,
                 static_cast<INT>(Limit) - 1);
            Bad = TRUE;
        }
        else if (Rows > 1 && Source->Number + Rows - 1 >= Limit)
        {
            Fail(2, 0x235, "%sRegister number: %d on this parameter for a matrix instruction causes attempt to access "
                 "out of range register number %d. Max allowed for this type is %d.", Prefix, Source->Number,
                 Source->Number + Rows - 1, static_cast<INT>(Limit) - 1);
            Bad = TRUE;
        }

    Modifiers:
        if (Source->Modifier == D9SvModNegate)
        {
            if (Index == 1 && D9SvIsMatrix(Op))
            {
                Fail(2, 0x236, "Cannot negate second source parameter to vector*matrix instructions.");
                Bad = TRUE;
            }
            if (Op == 25 || Op == 26 || Op == 30 || Op == 38 || Op == 40 || Op == 96)
            {
                Fail(2, 363, "Cannot use negate on source parameter(s) to %s instruction.", OpName);
                Bad = TRUE;
            }
        }
        else if (Source->Modifier != 0 && (Source->Modifier != D9SvModNot || (Op != 40 && Op != 26 && Op != 96)))
        {
            Fail(2, 201, "%sInvalid source modifier.", Prefix);
            Bad = TRUE;
        }

        /* Swizzles */
        if (Op == 38)
        {
            if (Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 942, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or none "
                     "specified.) ", OpName);
                SkipSource[Index] = TRUE;
                continue;
            }
        }
        else if (Op == 40)
        {
            if (Source->Type == D9SvRegConstBool && Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 943, "Const bool (b#) parameter to %s instruction must specify default swizzle (.xyzw, or "
                     "none specified.) ", OpName);
                Bad = TRUE;
            }
            else if (Source->Type == D9SvRegPredicate && !D9SvReplicate(Swizzle))
            {
                Fail(2, 944, "Predicate register (p0) parameter to %s instruction must specify replicate swizzle in "
                     "order to select component. i.e. .x | .y | .z | .w (or rgba equivalent)", OpName);
                Bad = TRUE;
            }
        }
        else if (Op == 41 || Op == 45 || Op == 96 || Op == 32 || Op == 6 || Op == 7 || Op == 14 || Op == 15)
        {
            if (!D9SvReplicate(Swizzle))
                Fail(2, 0x1A6, "%s%s requires replicate swizzle for this parameter in order to select component. "
                     "i.e. .x | .y | .z | .w (or rgba equivalent)", Prefix, OpName);
        }
        else if (Op == 26)
        {
            if ((Index == 0 || Source->Type == D9SvRegConstBool) && Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 516, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or none "
                     "specified.).  The exception is (on shader version > ps_2_0) if the condition is the predicate "
                     "register, p0, in which case a replicate swizzle must be used in order to select a component. "
                     "i.e. .x | .y | .z | .w (or rgba equivalent) ", OpName);
                SkipSource[Index] = TRUE;
                continue;
            }
            if (Index == 1 && Source->Type == D9SvRegPredicate && !D9SvReplicate(Swizzle))
            {
                Fail(2, 941, "When predicate register is used as the condition for a %s instruction, a replicate "
                     "swizzle must be specified, to select a component. i.e. .x | .y | .z | .w (or rgba "
                     "equivalent)", OpName);
                Bad = TRUE;
            }
        }
        else
        {
            if (Swizzle != D9SvSwizzleIdentity && Index == 1 && D9SvIsMatrix(Op))
            {
                Fail(2, 567, "Cannot swizzle second source parameter to vector*matrix instructions.");
                SkipSource[Index] = TRUE;
                continue;
            }
            if (Bad)
            {
                SkipSource[Index] = TRUE;
                continue;
            }
            if (Version != D9PsVersion20 && (Caps == nullptr || (PsCaps() & D9PsCapsSwizzle) != 0))
                continue;
            if (!D9PsBasicSwizzle(Swizzle))
            {
                char Name[32];

                D9PsSwizzleName(Swizzle, Name);
                Fail(2, 0xCA, "%sInvalid source selector: %s. The only available source swizzles in this shader "
                     "version are: .rgba/xyzw (same as not specifying swizzle), .r/x, .g/y, .b/z, .a/w, .gbra/yzxw, "
                     ".brga/zxyw and .abgr/wzyx. ", Prefix, Name);
                SkipSource[Index] = TRUE;
            }
            continue;
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

/* ps_3_*: register types, numbers, relative addressing, modifiers and swizzles of each source */
VOID
D9PsChecker::SourceParams3()
{
    D9PsInstruction* Instr = Ins();
    UINT Op = Instr->Opcode;
    PCSTR OpName = D9SvOpcodeName(Op);

    for (UINT Index = 0; Index < Instr->SourceCount; Index++)
    {
        D9SvSource* Source = &Instr->Source[Index];
        DWORD Swizzle = Source->Swizzle;
        PCSTR Ordinal = D9PsOrdinals[Index];
        BOOLEAN Bad = FALSE;
        BOOLEAN IsPos;
        BOOLEAN IsFace;
        UINT Limit = 0;
        UINT Rows;

        if (Op == D9PsOpTexld || Op == D9PsOpTexldd || Op == 95)
        {
            if (Index == 0)
            {
                switch (Source->Type)
                {
                case D9SvRegTemp:
                    Limit = Temps.Count;
                    break;
                case D9SvRegInput:
                    Limit = Inputs.Count;
                    break;
                case D9SvRegConst:
                    Limit = Consts.Count;
                    break;
                default:
                    Fail(2, 525, "First source parameter for texld* must be temp (r#), input (v#) or constant (c#) "
                         "register.");
                    SkipSource[Index] = TRUE;
                    continue;
                }
            }
            else if (Index == 1)
            {
                if (Source->Type != D9SvRegSampler)
                {
                    Fail(2, 528, "Second source parameter for texld* must be sampler stage (s#).");
                    SkipSource[Index] = TRUE;
                    continue;
                }
                Limit = Samplers.Count;
                if (Source->Modifier != 0)
                {
                    Fail(2, 0x22F, "Source modifier not allowed on sampler (s#) parameter to texld* instruction.");
                    Bad = TRUE;
                }
            }
            if (Op == D9PsOpTexldd && Index > 1)
            {
                switch (Source->Type)
                {
                case D9SvRegTemp:
                    Limit = Temps.Count;
                    break;
                case D9SvRegInput:
                    Limit = Inputs.Count;
                    break;
                case D9SvRegConst:
                    Limit = Consts.Count;
                    break;
                default:
                    Fail(2, 526, "Third and fourth parameters to texldd must be temp (r#), input (v#) or constant "
                         "(c#) register.");
                    SkipSource[Index] = TRUE;
                    continue;
                }
            }
            if (Limit == 0)
            {
                Fail(2, 0x213, "Unexpected source parameter.");
                Bad = TRUE;
            }
            if (Source->Number >= Limit)
            {
                Fail(2, 0x214, "(%s source param) Invalid register number: %d.  Max allowed for this type is %d.",
                     Ordinal, Source->Number, static_cast<INT>(Limit) - 1);
                Bad = TRUE;
            }
            if (Source->Modifier != 0)
            {
                Fail(2, 561, "(%s source param) Negate or abs modifiers not permitted on source parameters to texld*",
                     Ordinal);
                Bad = TRUE;
            }
        }
        else if (Op == 40)
        {
            if (Source->Type == D9SvRegConstBool)
            {
                Limit = BoolConsts.Count;
            }
            else if (Source->Type == D9SvRegPredicate)
            {
                Limit = Predicates.Count;
            }
            else
            {
                Fail(2, 917, "%s requires parameter (the branch condition) to be boolean constant register (b#), or "
                     "predicate register (p0). ", D9SvOpcodeName(40));
                Bad = TRUE;
            }
        }
        else if (Op == 25 || Op == 26)
        {
            if (Index == 0)
            {
                if (Source->Type == D9SvRegLabel)
                {
                    Limit = Labels.Count;
                }
                else
                {
                    Fail(2, 931, "%s requires %s parameter to be label (l#). ", OpName, Ordinal);
                    Bad = TRUE;
                }
            }
            else if (Index == 1)
            {
                if (Op != 26 || Source->Type == D9SvRegConstBool || Source->Type == D9SvRegPredicate)
                {
                    Limit = Source->Type == D9SvRegConstBool ? BoolConsts.Count : Predicates.Count;
                }
                else
                {
                    Fail(2, 916, "%s requires second source parameter (the condition) to be boolean constant register "
                         "(b#), or predicate register (p0). ", D9SvOpcodeName(26));
                    Bad = TRUE;
                }
            }
        }
        else if (Op == 27 && Index == 0)
        {
            if (Source->Type == D9SvRegLoop)
            {
                Limit = LoopCounter.Count;
            }
            else
            {
                Fail(2, 514, "loop requires first parameter to be loop counter (aL). ");
                Bad = TRUE;
            }
        }
        else if (Op == 27 || Op == 38)
        {
            if (Source->Type == D9SvRegConstInt)
            {
                Limit = IntConsts.Count;
            }
            else
            {
                Fail(2, 932, "%s requires %s parameter to be integer constant register (i#). ",
                     Op == 27 ? "loop" : "rep", Ordinal);
                Bad = TRUE;
            }
        }
        else if (Op == 30)
        {
            if (Source->Type == D9SvRegLabel)
            {
                Limit = Labels.Count;
            }
            else
            {
                Fail(2, 929, "%s instruction requires parameter to be label (l#). ", D9SvOpcodeName(30));
                Bad = TRUE;
            }
        }
        else if (Op == 96)
        {
            if (Source->Type == D9SvRegPredicate)
            {
                Limit = Predicates.Count;
            }
            else
            {
                Fail(2, 930, "breakp requires parameter to be predicate register (p0). ");
                Bad = TRUE;
            }
        }
        else
        {
            switch (Source->Type)
            {
            case D9SvRegTemp:
                Limit = Temps.Count;
                break;
            case D9SvRegInput:
                Limit = Inputs.Count;
                break;
            case D9SvRegConst:
                Limit = Consts.Count;
                break;
            case D9SvRegMisc:
                if (Source->Number > Misc.Count)
                {
                    Fail(2, 0x166, "Invalid reg num for MISCTYPE.");
                    return;
                }
                Limit = Misc.Count;
                break;
            default:
                Fail(2, 679, "Invalid reg type for %s source param.", Ordinal);
                Bad = TRUE;
                break;
            }
        }

        /* Relative addressing and register numbers */
        if (Source->Relative != 0)
        {
            if (Source->Type != D9SvRegInput)
            {
                Fail(2, 365, "Relative addressing only permitted on input (v#) registers (%s source param).", Ordinal);
                Bad = TRUE;
                goto Modifiers;
            }
            if (Source->RelativeType != D9SvRegLoop)
            {
                Fail(2, 533, "Relative address register must be aL register (%s source param).", Ordinal);
                Bad = TRUE;
                goto Modifiers;
            }
            if (Source->RelativeNumber >= LoopCounter.Count)
            {
                Fail(2, 0x216, "Invalid register number, %d, specified for aL register. Max # supported is %d (%s "
                     "source param).", Source->RelativeNumber, static_cast<INT>(LoopCounter.Count) - 1, Ordinal);
                Bad = TRUE;
                goto Modifiers;
            }
        }
        if (Bad)
            goto Modifiers;
        Rows = Index == 1 ? D9SvMatrixRows(Op) : 1;
        if (Source->Number >= Limit && Source->Relative == 0)
        {
            Fail(2, 0x167, "Invalid reg num: %d for %s source param. Max allowed for this type is %d.",
                 Source->Number, Ordinal, static_cast<INT>(Limit) - 1);
            Bad = TRUE;
            goto Modifiers;
        }
        if (Rows > 1)
        {
            UINT Last = Source->Number + Rows - 1;

            if (Last >= Limit && (Source->Type != D9SvRegInput || Source->Relative != D9SvRelativeBit))
            {
                Fail(2, 0x2AC, "Reg num: %d for %s source param on matrix instruction causes attempt to access out of "
                     "range register number %d. Max allowed for this type is %d.", Source->Number, Ordinal, Last,
                     static_cast<INT>(Limit) - 1);
                Bad = TRUE;
            }
        }

    Modifiers:
        if (Source->Modifier == D9SvModNegate || Source->Modifier == D9SvModAbs ||
            Source->Modifier == D9SvModAbsNegate)
        {
            if (Source->Modifier != D9SvModNegate && Source->Type == D9SvRegMisc && Source->Number == 1)
            {
                Fail(2, 0x437, "Cannot use abs on source parameter(s) on to vFace register.");
                Bad = TRUE;
            }
            if (Index == 1 && D9SvIsMatrix(Op))
            {
                Fail(2, 0x2B1, "Cannot use negate or abs on second source parameter to vector*matrix instructions.");
                Bad = TRUE;
            }
            switch (Op)
            {
            case 25: case 26: case 27: case 30: case 38: case 40: case 91: case 92: case 96:
                Fail(2, 690, "Cannot use negate or abs on source parameter(s) to %s instruction.", OpName);
                Bad = TRUE;
                break;
            default:
                break;
            }
        }
        else if (Source->Modifier != 0 && (Source->Modifier != D9SvModNot || (Op != 40 && Op != 26 && Op != 96)))
        {
            Fail(2, 364, "Invalid src mod for %s source param.", Ordinal);
            Bad = TRUE;
        }

        /* vPos and vFace */
        IsPos = Source->Type == D9SvRegMisc && Source->Number == 0;
        IsFace = Source->Type == D9SvRegMisc && Source->Number == 1;
        if (IsFace)
        {
            BOOLEAN Allowed = Op == D9SvOpIfc || Op == D9SvOpBreakc || Op == D9SvOpSetp || Op == D9PsOpCmp;

            if (Allowed && Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 0x2B3, "vFace register must not specify a swizzle (i.e. default swizzle), as it is an implied "
                     "scalar.");
                Bad = TRUE;
            }
            if (!Allowed || (Op == D9PsOpCmp && Index != 0))
            {
                Fail(2, 0x2B4, "vFace register can only be used as one of the sources to if_(cond), break_(cond), "
                     "setp_(cond), or the condition in a cmp.");
                Bad = TRUE;
            }
        }

        switch (Op)
        {
        case 6: case 7: case 14: case 15: case 32: case 37: case 41: case 45: case 96:
            if (IsPos && !D9PsIsXOrY(Swizzle))
            {
                Fail(2, 466, "%s requires replicate swizzle for source parameter(s) in order to select component. For "
                     "the vPos register, this can either be .x or .y (z,w are not available). ", OpName);
                Bad = TRUE;
            }
            else if (!D9SvReplicate(Swizzle) && !IsFace)
            {
                Fail(2, 793, "%s requires replicate swizzle for source parameter(s) in order to select component. "
                     "i.e. .x | .y | .z | .w (or rgba equivalent)", OpName);
                Bad = TRUE;
            }
            break;
        default:
            break;
        }

        /* Flow control swizzles */
        if (Op == 26)
        {
            if ((Index != 0 && Source->Type != D9SvRegConstBool) || Swizzle == D9SvSwizzleIdentity)
            {
                if (Index == 1 && Source->Type == D9SvRegPredicate && !D9SvReplicate(Swizzle))
                {
                    Fail(2, 946, "When predicate register is used as the condition for a %s instruction, a replicate "
                         "swizzle must be specified, to select a component. i.e. .x | .y | .z | .w (or rgba "
                         "equivalent)", D9SvOpcodeName(26));
                    Bad = TRUE;
                }
            }
            else
            {
                Fail(2, 945, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or none "
                     "specified.).  The exception is if the condition is the predicate register, p0, in which case a "
                     "replicate swizzle must be used in order to select a component. i.e. .x | .y | .z | .w (or rgba "
                     "equivalent)", D9SvOpcodeName(26));
                Bad = TRUE;
            }
        }
        else if (Op == 27 || Op == 38)
        {
            if (Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, 947, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or none "
                     "specified.) ", OpName);
                Bad = TRUE;
            }
        }
        else if (Op == 40)
        {
            if (Source->Type == D9SvRegConstBool)
            {
                if (Swizzle != D9SvSwizzleIdentity)
                {
                    Fail(2, 948, "Const bool (b#) parameter to %s instruction must specify default swizzle (.xyzw, or "
                         "none specified.) ", D9SvOpcodeName(40));
                    Bad = TRUE;
                }
            }
            else if (Source->Type == D9SvRegPredicate && !D9SvReplicate(Swizzle))
            {
                Fail(2, 949, "Predicate register (p0) parameter to %s instruction must specify replicate swizzle in "
                     "order to select component. i.e. .x | .y | .z | .w (or rgba equivalent)", D9SvOpcodeName(40));
                Bad = TRUE;
            }
        }

        if (Swizzle != D9SvSwizzleIdentity && Index == 1 && D9SvIsMatrix(Op))
        {
            Fail(2, 327, "Cannot swizzle second source parameter to vector*matrix instructions.");
            Bad = TRUE;
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

/* Which instructions may be predicated, and the form of the predicate */
VOID
D9PsChecker::SourcePredicate()
{
    D9PsInstruction* Instr = Ins();
    D9SvSource* Pred = &Instr->Predicate;
    BOOLEAN Refused;

    if (!Instr->Predicated)
        return;
    if (Model == 2)
    {
        if (Version == D9PsVersion20)
        {
            Fail(2, 651, "ps_2_0 does not support predication.");
            return;
        }
        if (Version != D9PsVersion2sw && Caps != nullptr && (PsCaps() & D9PsCapsPredication) == 0)
        {
            Report(2, 0x28C, "vs_2_x predication not supported by the hardware.");
            return;
        }
    }
    switch (Instr->Opcode)
    {
    case 25: case 26: case 28: case 31: case 38: case 39: case 40: case 41: case 42: case 43: case 44: case 45:
    case 47: case 48: case 81: case 94: case 96:
        Refused = TRUE;
        break;
    case 27: case 29:
        Refused = Model == 3;
        break;
    default:
        Refused = FALSE;
        break;
    }
    if (Refused)
    {
        Fail(2, Model == 2 ? 0x165 : 0x299, "%s instruction cannot be predicated.", D9SvOpcodeName(Instr->Opcode));
        return;
    }
    if (Pred->Type != D9SvRegPredicate || Pred->Number != 0 || Pred->Relative != 0)
    {
        Fail(2, Model == 2 ? 662 : 666, "Source predicate must be p0 register.");
        return;
    }
    if (!D9SvReplicate(Pred->Swizzle) && Pred->Swizzle != D9SvSwizzleIdentity)
        Fail(2, Model == 2 ? 0x297 : 0x29B, "Source predicate p0 must use one of the following swizzles: .x | .y | .z "
             "| .w | .xyzw (same as swizzle not present)");
    if (Pred->Modifier != D9SvModNot && Pred->Modifier != 0)
        Fail(2, Model == 2 ? 0x298 : 0x29C, "The only modifier allowed for source predicate p0 is the not (!) "
             "modifier.");
    if (Instr->Dest.Type == D9SvRegDepthOut && !D9SvReplicate(Pred->Swizzle))
        Fail(2, Model == 2 ? 1068 : 1069, "When writing to oDepth with predication, p0 must use a replicate swizzle "
             "to select a single component: .x | .y | .z | .w");
}

/* ps_2_*: reads of unwritten temps, undeclared inputs and undeclared sampler components */
VOID
D9PsChecker::SourceInitialized2()
{
    D9PsInstruction* Instr = Ins();
    BOOLEAN FromDest = Instr->DestRead != 0;
    UINT Count = FromDest ? 1 : Instr->SourceCount;

    for (UINT Index = 0; Index < Count; Index++)
    {
        D9SvRegisters* File;
        PCSTR Name;
        UINT Type;
        UINT Number;
        DWORD Read;
        UINT Rows = 1;
        BOOLEAN IsTemp = FALSE;
        BOOLEAN IsConst = FALSE;

        if (FromDest)
        {
            Type = Instr->Dest.Type;
            Number = Instr->Dest.Number;
            Read = Instr->DestRead;
        }
        else
        {
            if (SkipSource[Index])
                continue;
            Type = Instr->Source[Index].Type;
            Number = Instr->Source[Index].Number;
            Read = Instr->Source[Index].Read;
        }
        if (Index == 1)
            Rows = D9SvMatrixRows(Instr->Opcode);

        /* texld reads the coordinates its sampler's texture type needs */
        if ((Instr->Opcode == D9PsOpTexld || Instr->Opcode == D9PsOpTexldd) && Count == 2 && !SkipSource[1])
        {
            DWORD Sampled = Samplers.Written(Instr->Source[1].Number);

            if (Index != 0)
            {
                if (Sampled == 0)
                    Fail(2, 0xCB, "Sampler register s%d has not been declared.", Number);
                continue;
            }
            if (Instr->Opcode == D9PsOpTexld && (Instr->Controls == 0x10000 || Instr->Controls == 0x20000))
                Sampled |= D9SvComponentW;
            Read |= D9SvSwizzled(Instr->Source[0].Swizzle, Sampled);
        }

        switch (Type)
        {
        case D9SvRegTemp:
            Name = "r";
            File = &Temps;
            IsTemp = TRUE;
            break;
        case D9SvRegInput:
            Name = "v";
            File = &Inputs;
            break;
        case D9SvRegConst:
            Name = "c";
            File = &Consts;
            IsConst = TRUE;
            break;
        case D9PsRegTexture:
            Name = "t";
            File = &Texcoords;
            break;
        default:
            continue;
        }
        if (Number >= File->Count)
            continue;

        for (UINT Row = 0; Row < Rows; Row++)
        {
            UINT Register = Number + Row;
            DWORD Missing = 0;
            DWORD Lost = 0;
            UINT MissingCount = 0;
            char Text[32];

            for (UINT Component = 0; Component < 4; Component++)
            {
                DWORD Bit = D9SvComponent(Component);

                if ((Read & Bit) == 0 || (File->Written(Register) & Bit) != 0)
                    continue;
                MissingCount += 1;
                Missing |= Bit;
                if (IsTemp && Basic && Clobbered[Component][Register] != 0)
                    Lost |= Bit;
            }
            if (MissingCount == 0)
                continue;
            ComponentText(Missing, TRUE, TRUE, Text);
            if (IsTemp && Basic)
            {
                Fail(2, 0xCC, "Read of uninitialized component%s(*) in %s%d: %s", MissingCount > 1 ? "s" : "", Name,
                     Register, Text);
            }
            else if (!IsConst && !IsTemp)
            {
                char VersionText[32];

                D9SvVersionName(Version, VersionText);
                Fail(2, 0x222, "Component%s of register %s%d being read, but not declared. v# and t# registers must be "
                     "declared using a dcl statement (down to the component level) in %s. Undeclared component%s(*) "
                     "being read: %s", MissingCount > 1 ? "s" : "", Name, Register, VersionText,
                     MissingCount > 1 ? "s" : "", Text);
            }
            if (Lost != 0 && Basic)
            {
                static const PCSTR Names[] = { "r/x", "g/y", "b/z", "a/w" };

                for (UINT Component = 0; Component < 4; Component++)
                {
                    if ((Lost & D9SvComponent(Component)) != 0)
                        Fail(2, 0xCD, "Note that the %s component of %s%d was previously initialized, but then became "
                             "uninitialized by a previous instruction (e.g. SINCOS clobbers components).",
                             Names[Component], Name, Register);
                }
            }
        }
    }
}

/* ps_3_*: reads of undeclared inputs, vPos and vFace, and of aL outside loops */
VOID
D9PsChecker::SourceInitialized3()
{
    D9PsInstruction* Instr = Ins();
    BOOLEAN FromDest = Instr->DestRead != 0;
    UINT Count = FromDest ? 1 : Instr->SourceCount;

    for (UINT Index = 0; Index < Count; Index++)
    {
        D9SvSource* Source = FromDest ? nullptr : &Instr->Source[Index];
        D9SvRegisters* File;
        PCSTR Name = nullptr;
        UINT Type;
        UINT Number;
        DWORD Read;
        UINT Rows = 1;

        if (FromDest)
        {
            Type = Instr->Dest.Type;
            Number = Instr->Dest.Number;
            Read = Instr->DestRead;
        }
        else
        {
            if (SkipSource[Index])
                continue;
            Type = Source->Type;
            Number = Source->Number;
            Read = Source->Read;
        }
        if (Index == 1)
            Rows = D9SvMatrixRows(Instr->Opcode);

        if ((Instr->Opcode == D9PsOpTexld || Instr->Opcode == D9PsOpTexldd || Instr->Opcode == 95) &&
            !SkipSource[1])
        {
            DWORD Sampled = Samplers.Written(Instr->Source[1].Number);

            if (Index != 0)
            {
                if (Sampled == 0)
                    Fail(2, 0x240, "Sampler register s%d has not been declared.", Number);
                continue;
            }
            if (Instr->Opcode == D9PsOpTexld && (Instr->Controls == 0x10000 || Instr->Controls == 0x20000))
                Sampled |= D9SvComponentW;
            if (Instr->Opcode == 95)
                Sampled |= D9SvComponentW;
            Read |= D9SvSwizzled(Instr->Source[0].Swizzle, Sampled);
        }

        switch (Type)
        {
        case D9SvRegTemp:
            Name = "r";
            File = &Temps;
            break;
        case D9SvRegInput:
            if (Source != nullptr && Source->Relative == D9SvRelativeBit && Source->RelativeType == D9SvRegLoop)
            {
                if (!Flow->LoopCounterUsable())
                    Fail(2, 0x172, "aL can only be referenced in the following scenarios: (1) inside a loop/endloop "
                         "block, or (2) in a subroutine for which in all cases some call below in the stack is in a "
                         "loop/endloop block. ");
                continue;
            }
            Name = "v";
            File = &Inputs;
            break;
        case D9SvRegMisc:
            File = &Misc;
            if (Number == 0)
                Name = "vPos";
            else if (Number == 1)
                Name = "vFace";
            break;
        default:
            continue;
        }
        if (Number >= File->Count)
            continue;

        for (UINT Row = 0; Row < Rows; Row++)
        {
            UINT Register = Number + Row;
            DWORD Missing = 0;
            UINT MissingCount = 0;
            char Text[32];

            for (UINT Component = 0; Component < 4; Component++)
            {
                DWORD Bit = D9SvComponent(Component);

                if ((Read & Bit) != 0 && (File->Written(Register) & Bit) == 0)
                {
                    MissingCount += 1;
                    Missing |= Bit;
                }
            }
            if (Type == D9SvRegInput)
            {
                if (MissingCount != 0)
                {
                    ComponentText(Missing, TRUE, TRUE, Text);
                    Fail(2, 0x3D8, "Component%s of register %s%d being read, but not declared. v# registers must be "
                         "declared using dcl statement(s) (down to the component level). Affected components (*): %s",
                         MissingCount > 1 ? "s" : "", Name, Register, Text);
                }
            }
            else if (Type == D9SvRegMisc)
            {
                if (Number == 1)
                {
                    if (Missing != 0)
                        Fail(2, 0x3DA, "vFace being read, but not declared. vFace must be declared if it is used:  dcl "
                             "vFace");
                    Read = D9SvMaskAll;
                }
                else if (Number == 0)
                {
                    if ((Missing & 0x30000) != 0)
                    {
                        ComponentText(Missing, TRUE, TRUE, Text);
                        Fail(2, 985, "Component(s) of register vPos being read, but not declared. vPos must be declared "
                             "using a dcl statement. Affected components (*): %s", Text);
                    }
                    else if (Missing != 0)
                    {
                        ComponentText(Missing & 0xC0000, TRUE, TRUE, Text);
                        Fail(2, 1063, "vPos does not have .z or .w channels in this shader model. Attempt to read "
                             "following unavailable component(s) (*): %s", Text);
                    }
                }
            }
        }
    }
}

/* ps_2_*: destination register, mask, modifiers, and what the write leaves behind */
VOID
D9PsChecker::DestParam2()
{
    D9PsInstruction* Instr = Ins();
    D9SvDest* Dest = &Instr->Dest;
    UINT Op = Instr->Opcode;
    D9SvRegisters* File = nullptr;
    BOOLEAN Bad = FALSE;

    if (Op == D9SvOpDcl || Op == D9SvOpDefb || Op == D9SvOpDefi || Op == D9SvOpDef || !Dest->Present)
        return;

    switch (Dest->Type)
    {
    case D9SvRegTemp:
        File = &Temps;
        break;
    case D9PsRegTexture:
        File = &Texcoords;
        break;
    case D9SvRegColorOut:
        File = &Colors;
        break;
    case D9SvRegDepthOut:
        File = &Depth;
        break;
    case D9SvRegPredicate:
        File = &Predicates;
        break;
    default:
        break;
    }
    if (File == nullptr || File->Count == 0 || (Dest->Type == D9PsRegTexture && Op != D9PsOpTexkill))
    {
        Fail(2, 0xCE, "Invalid register type for destination param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 0xCF, "Invalid destination register number: %d. Max allowed for this register type is %d.",
             Dest->Number, static_cast<INT>(File->Count) - 1);
        Bad = TRUE;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 0x1A9, "Writemask cannot be empty.");
        Bad = TRUE;
    }
    if (Op == D9SvOpSincos)
    {
        if (((Dest->Mask - 0x10000) & 0xFFFCFFFF) != 0 || Dest->Mask == D9SvComponentZ)
            Fail(2, 0xD1, "Dest writemask for SINCOS must be .xy, .x or .y.");
    }
    else if (Op == D9PsOpTexkill)
    {
        if (Dest->Mask != D9SvMaskAll)
        {
            Fail(2, 208, "texkill must write all components.");
            Bad = TRUE;
        }
    }
    else if ((Op == D9PsOpTexld || Op == D9PsOpTexldd) && Dest->Mask != D9SvMaskAll)
    {
        Fail(2, 773, "texld* must not specify a writemask (same as full mask). texld* instructions always write 4 "
             "components, including defaults if the source texture being sampled contains fewer than 4 components. ");
        Bad = TRUE;
    }
    if ((Dest->Modifier & 0x100000) != 0)
    {
        DWORD Id = 0;
        PCSTR Text = nullptr;

        if (Instr->TexOp)
        {
            Id = 508;
            Text = "_sat not permitted on tex* instructions.";
        }
        else if (Op == D9SvOpFrc)
        {
            Id = 509;
            Text = "_sat not permitted with frc instruction.";
        }
        else if (Op == D9SvOpSincos)
        {
            Id = 510;
            Text = "_sat not permitted with SINCOS instruction.";
        }
        else if (Op == D9SvOpSetp)
        {
            Id = 270;
            Text = "_sat not permitted with setp instruction.";
        }
        if (Text != nullptr)
        {
            Fail(2, Id, "%s", Text);
            Bad = TRUE;
        }
    }
    if ((Dest->Modifier & 0xFFCFFFFF) != 0)
    {
        Fail(2, 0xD2, "Instruction modifiers other than _sat or _pp not permitted. (_sat is permitted only on "
             "arithmetic ops, except frc or SINCOS)");
        Bad = TRUE;
    }
    if (Dest->Shift != 0)
    {
        Fail(2, 0x199, "Instruction shifts not permitted.");
        Bad = TRUE;
    }
    if (D9SvIsMatrix(Op))
    {
        static const DWORD Masks[] = { 0, 0x10000, 0x30000, 0x70000, 0xF0000 };
        static const PCSTR Names[] = { "M4x4", "M4x3", "M3x4", "M3x3", "M3x2" };
        static const PCSTR MaskNames[] = { "xyzw (same as not specifying mask)", "xyz",
                                           "xyzw (same as not specifying mask)", "xyz", "xy" };
        UINT Columns = Op == D9SvOpM4x4 || Op == D9SvOpM3x4 ? 4 : Op == D9SvOpM3x2 ? 2 : 3;

        if (Dest->Mask != Masks[Columns])
        {
            Fail(2, 0x238, "%s instruction must use destination writemask: .%s", Names[Op - D9SvOpM4x4],
                 MaskNames[Op - D9SvOpM4x4]);
            Bad = TRUE;
        }
        for (UINT Index = 0; Index < Instr->SourceCount; Index++)
        {
            D9SvSource* Source = &Instr->Source[Index];
            UINT Last = Source->Number + D9SvMatrixRows(Op) - 1;

            if (Dest->Type != Source->Type)
                continue;
            if (Index == 0 && Dest->Number == Source->Number && (Source->Read & Dest->Mask) != 0)
            {
                Fail(2, 569, "Dest register cannot be the same as first source register for m*x* instructions.");
                Bad = TRUE;
            }
            else if (Index == 1 && Source->Number < Dest->Number && Dest->Number <= Last)
            {
                Fail(2, 570, "Dest register for m*x* instructions cannot be the same as one of the additional "
                     "registers implied by the second source register.");
                Bad = TRUE;
            }
        }
    }
    if (Bad)
        return;

    if (Dest->Type == D9SvRegTemp && Basic)
    {
        UINT Number = Dest->Number;
        UINT Order;

        if (Instr->TexOp || Temps.Written(Number) != 0 || Clobbered[0][Number] != 0 || Clobbered[1][Number] != 0 ||
            Clobbered[2][Number] != 0 || Clobbered[3][Number] != 0)
            Order = Chain[Number];
        else
            Order = 1;
        for (UINT Index = 0; Index < Instr->SourceCount; Index++)
        {
            if (!SkipSource[Index] && Instr->Source[Index].Type == D9SvRegTemp)
                Order = D9SvMax(Order, Chain[Instr->Source[Index].Number]);
        }
        if (Instr->TexOp)
            Order += 1;
        Chain[Number] = Order;
        if (Instr->TexOp && Order > 4)
        {
            if (TargetMessages())
            {
                char Name[32];

                D9SvVersionName(Version, Name);
                Fail(0x10002, 0x1AA, "Shader uses texture addressing operations in a dependency chain that is too "
                     "complex for the target shader model (%s) to handle.  ", Name);
            }
            else
            {
                Fail(2, 0x307, "Dependent tex-op sequence too long (%dth order). A 1st order dependent tex-op is a "
                     "tex[ld*|kill] instruction in which either: (1) an r# reg is input (NOT t# reg), or (2) output "
                     "r# reg was previously written, now being written AGAIN. A 2nd order dependent tex-op occurs if: "
                     "a tex-op reads OR WRITES to an r# reg whose contents, BEFORE executing the tex-op, depend "
                     "(perhaps indirectly) on the outcome of a 1st order dependent tex-op. An (n)th order dependent "
                     "tex-op derives from an (n-1)th order tex-op. A given tex-op may be dependent to at most 3rd "
                     "order (ps_2_0/x only).", Order - 1);
            }
        }
    }

    if (Dest->Type == D9PsRegTexture)
        return;
    if (Op != D9PsOpTexkill)
    {
        for (UINT Component = 0; Component < 4; Component++)
        {
            if ((Dest->Mask & D9SvComponent(Component)) == 0)
                continue;
            File->Marks[Component][Dest->Number] = 1;
            if (Dest->Type == D9SvRegTemp && Basic)
                Clobbered[Component][Dest->Number] = 0;
        }
    }

    /* SINCOS leaves the components it does not write undefined */
    if (Basic && Op == D9SvOpSincos)
    {
        for (UINT Component = 0; Component < 3; Component++)
        {
            if (Component < 2 && (Dest->Mask & D9SvComponent(Component)) != 0)
                continue;
            if (File->Marks[Component][Dest->Number] != 0)
            {
                File->Marks[Component][Dest->Number] = 0;
                Clobbered[Component][Dest->Number] = 1;
            }
        }
    }
}

/* ps_3_*: destination register, mask and modifiers */
VOID
D9PsChecker::DestParam3()
{
    D9PsInstruction* Instr = Ins();
    D9SvDest* Dest = &Instr->Dest;
    UINT Op = Instr->Opcode;
    D9SvRegisters* File = nullptr;
    BOOLEAN Bad = FALSE;

    if (Op == D9SvOpDcl || Op == D9SvOpDefb || Op == D9SvOpDefi || Op == D9SvOpDef || !Dest->Present)
        return;

    switch (Dest->Type)
    {
    case D9SvRegTemp:
        File = &Temps;
        break;
    case D9SvRegInput:
        File = &Inputs;
        break;
    case D9SvRegColorOut:
        File = &Colors;
        break;
    case D9SvRegDepthOut:
        File = &Depth;
        break;
    case D9SvRegLabel:
        File = &Labels;
        break;
    case D9SvRegPredicate:
        File = &Predicates;
        break;
    default:
        break;
    }
    if (File == nullptr || File->Count == 0 || (Op != D9PsOpTexkill && !File->Writable))
    {
        Fail(2, 0x242, "Invalid register type for destination param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 0x243, "Invalid destination register number: %d. Max allowed for this register type is %d.",
             Dest->Number, static_cast<INT>(File->Count) - 1);
        Bad = TRUE;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 0x306, "Writemask cannot be empty.");
        Bad = TRUE;
    }
    if (Op == D9SvOpSincos && (((Dest->Mask - 0x10000) & 0xFFFCFFFF) != 0 || Dest->Mask == D9SvComponentZ))
        Fail(2, 0x244, "Dest writemask for SINCOS must be .xy, .x or .y.");
    if ((Dest->Modifier & 0x100000) != 0)
    {
        DWORD Id = 0;
        PCSTR Text = nullptr;

        if (Instr->TexOp)
        {
            Id = 911;
            Text = "_sat not permitted on tex* instructions.";
        }
        else if (Op == D9SvOpFrc)
        {
            Id = 912;
            Text = "_sat not permitted with frc instruction.";
        }
        else if (Op == D9SvOpSincos)
        {
            Id = 1061;
            Text = "_sat not permitted with SINCOS instruction.";
        }
        else if (Op == D9SvOpSetp)
        {
            Id = 1066;
            Text = "_sat not permitted with setp instruction.";
        }
        if (Text != nullptr)
        {
            Fail(2, Id, "%s", Text);
            Bad = TRUE;
        }
    }
    if ((Dest->Modifier & 0x400000) != 0)
    {
        Fail(2, 0x245, "_centroid flag only permitted on dcl v# statements.");
        Bad = TRUE;
    }
    if ((Dest->Modifier & 0xFF8FFFFF) != 0)
    {
        Fail(2, 0x3FB, "Invalid instruction modifier.");
        Bad = TRUE;
    }
    if (Dest->Shift != 0)
    {
        Fail(2, 0x2F5, "Instruction shifts not permitted.");
        Bad = TRUE;
    }
    if (Dest->Type == D9SvRegPredicate)
    {
        if (Op != D9SvOpSetp)
        {
            Fail(2, 373, "The only instruction allowed to write to the predicate register is setp.");
            Bad = TRUE;
        }
    }
    else if (Op == D9SvOpSetp)
    {
        Fail(2, 714, "The setp instruction must write to the predicate register.");
        Bad = TRUE;
    }
    if (D9SvIsMatrix(Op))
    {
        static const DWORD Masks[] = { 0, 0x10000, 0x30000, 0x70000, 0xF0000 };
        static const PCSTR Names[] = { "M4x4", "M4x3", "M3x4", "M3x3", "M3x2" };
        static const PCSTR MaskNames[] = { "xyzw (same as not specifying mask)", "xyz",
                                           "xyzw (same as not specifying mask)", "xyz", "xy" };
        UINT Columns = Op == D9SvOpM4x4 || Op == D9SvOpM3x4 ? 4 : Op == D9SvOpM3x2 ? 2 : 3;

        if (Dest->Mask != Masks[Columns])
        {
            Fail(2, 0x3F6, "%s instruction must use destination writemask: .%s", Names[Op - D9SvOpM4x4],
                 MaskNames[Op - D9SvOpM4x4]);
            Bad = TRUE;
        }
        for (UINT Index = 0; Index < Instr->SourceCount; Index++)
        {
            D9SvSource* Source = &Instr->Source[Index];
            UINT Last = Source->Number + D9SvMatrixRows(Op) - 1;

            if (Dest->Type != Source->Type)
                continue;
            if (Index == 0 && Dest->Number == Source->Number && (Source->Read & Dest->Mask) != 0)
            {
                Fail(2, 1015, "Dest register cannot be the same as first source register for m*x* instructions.");
                Bad = TRUE;
            }
            else if (Index == 1 && Source->Number < Dest->Number && Dest->Number <= Last)
            {
                Fail(2, 1016, "Dest register for m*x* instructions cannot be the same as one of the additional "
                     "registers implied by the second source register.");
                Bad = TRUE;
            }
        }
    }
    if (Bad || Op == D9PsOpTexkill)
        return;
    if (Dest->Type == D9SvRegTemp || Dest->Type == D9SvRegColorOut || Dest->Type == D9SvRegDepthOut ||
        Dest->Type == D9SvRegPredicate)
        File->Mark(Dest->Number, Dest->Mask);
}

/* Read port limits of ps_2_* */
VOID
D9PsChecker::Ports2()
{
    D9SvInstruction* Instr = Current;
    UINT Seen[4][4];
    UINT Unique[4] = {};
    UINT ConstReads = 0;
    UINT InputReads = 0;
    UINT TexReads = 0;

    if (Version == D9PsVersion2sw || Instr->Opcode == D9SvOpSincos)
        return;
    for (UINT Index = 0; Index < Instr->SourceCount; Index++)
    {
        D9SvSource* Source = &Instr->Source[Index];
        UINT Kind;
        BOOLEAN Found = FALSE;

        switch (Source->Type)
        {
        case D9SvRegTemp:
            Kind = 0;
            break;
        case D9SvRegInput:
            Kind = 1;
            InputReads += 1;
            break;
        case D9SvRegConst:
            Kind = 2;
            ConstReads += 1;
            break;
        case D9PsRegTexture:
            Kind = 3;
            TexReads += 1;
            break;
        default:
            continue;
        }
        for (UINT Entry = 0; Entry < Unique[Kind]; Entry++)
        {
            if (Seen[Kind][Entry] == Source->Number)
                Found = TRUE;
        }
        if (!Found)
        {
            Seen[Kind][Unique[Kind]] = Source->Number;
            Unique[Kind] += 1;
        }
    }
    if (Unique[0] > Temps.Ports)
        Fail(2, 0xD3, "%d different temp registers (r#) read by instruction.  Max. different temp registers readable "
             "per instruction is %d.", Unique[0], Temps.Ports);
    if (Unique[1] + Unique[3] > Inputs.Ports)
        Fail(2, 0xD4, "%d different input (v#) or texture (t#) registers read by instruction.  Max. different "
             "input/texture registers readable per instruction is %d.", Unique[1] + Unique[3], Inputs.Ports);
    if (Unique[2] > Consts.Ports)
        Fail(2, 0xD5, "%d different constant registers (c#) read by instruction.  Max. different constant registers "
             "readable per instruction is %d.(There is one exception: this rule does not apply to the SINCOS macro "
             "instruction.)", Unique[2], Consts.Ports);
    if (ConstReads > 2)
        Fail(2, 0x40E, "%d constant registers (c#) read by instruction.  Max. constant registers readable per "
             "instruction (even if they are the same) is %d.  This is in addition to the read port limit of %d for "
             "constants, which restricts how many *different* constants can be read. (There is one exception: none "
             "of this applies to the SINCOS macro instruction.)", ConstReads, 2, Consts.Ports);
    if (D9SvIsMatrix(Instr->Opcode))
    {
        if (ConstReads > 1)
            Fail(2, 0x23B, "Multiple constant registers cannot be read by a matrix op.");
        if (InputReads + TexReads > 1)
            Fail(2, 0x23C, "Multiple input/texture registers cannot be read by a matrix op.");
    }
}

/* Read port limits of ps_3_0, and how constants and inputs may be mixed */
VOID
D9PsChecker::Ports3()
{
    D9SvInstruction* Instr = Current;
    UINT Seen[3][4];
    UINT Unique[3] = {};
    UINT ConstReads = 0;
    UINT InputReads = 0;
    BOOLEAN SawAbs = FALSE;
    BOOLEAN SawPlain = FALSE;
    BOOLEAN AbsReported = FALSE;
    BOOLEAN SawAbsolute = FALSE;
    BOOLEAN SawRelative = FALSE;
    UINT RelativeRegister = 0;

    if (Version == D9PsVersion3sw)
        return;
    for (UINT Index = 0; Index < Instr->SourceCount; Index++)
    {
        D9SvSource* Source = &Instr->Source[Index];
        UINT Kind;
        BOOLEAN Found = FALSE;

        if (Source->Type == D9SvRegTemp)
        {
            Kind = 0;
        }
        else if (Source->Type == D9SvRegInput)
        {
            Kind = 1;
            InputReads += 1;
            if (Source->Relative != D9SvRelativeBit)
            {
                if (SawRelative)
                    Fail(2, 0x186, "Absolute and relative addressing of input registers cannot be combined in one "
                         "instruction.");
                SawAbsolute = TRUE;
            }
            else
            {
                if (SawAbsolute)
                    Fail(2, 388, "Absolute and relative addressing of input registers cannot be combined in one "
                         "instruction.");
                else if (SawRelative && RelativeRegister != Source->Number)
                    Fail(2, 389, "Different relative addressing of input registers cannot be combined in one "
                         "instruction.");
                SawRelative = TRUE;
                RelativeRegister = Source->Number;
            }
        }
        else if (Source->Type == D9SvRegConst)
        {
            BOOLEAN Abs = Source->Modifier == D9SvModAbs || Source->Modifier == D9SvModAbsNegate;
            BOOLEAN Mixed;

            Kind = 2;
            ConstReads += 1;
            if (Abs)
            {
                SawAbs = TRUE;
                Mixed = SawPlain;
            }
            else
            {
                SawPlain = TRUE;
                Mixed = SawAbs;
            }
            if (Mixed && !AbsReported)
            {
                Fail(2, 0x415, "When constant registers are read multiple times in a single instruction, the _abs "
                     "modifier must either be present on all of the constants, or none of them. ");
                AbsReported = TRUE;
            }
        }
        else
        {
            continue;
        }
        for (UINT Entry = 0; Entry < Unique[Kind]; Entry++)
        {
            if (Seen[Kind][Entry] == Source->Number)
                Found = TRUE;
        }
        if (!Found)
        {
            Seen[Kind][Unique[Kind]] = Source->Number;
            Unique[Kind] += 1;
        }
    }
    if (Unique[0] > Temps.Ports)
        Fail(2, 0x246, "%d different temp registers (r#) read by instruction.  Max. different temp registers readable "
             "per instruction is %d.", Unique[0], Temps.Ports);
    if (Unique[1] > Inputs.Ports)
        Fail(2, 0x247, "%d different input registers (v*#) read by instruction.  Max. different input registers "
             "readable per instruction is %d.", Unique[1], Inputs.Ports);
    if (Unique[2] > Consts.Ports)
        Fail(2, 0x248, "%d different constant registers (c#) read by instruction.  Max. different constant registers "
             "readable per instruction is %d.", Unique[2], Consts.Ports);
    if (D9SvIsMatrix(Instr->Opcode))
    {
        if (ConstReads > 1)
            Fail(2, 0x3F9, "Multiple constant registers cannot be read by a matrix op.");
        if (InputReads > 1)
            Fail(2, 0x3FA, "Multiple input registers cannot be read by a matrix op.");
    }
}

/* def, defi and defb: placement, destination, values, and each register defined once */
VOID
D9PsChecker::Definitions()
{
    D9SvInstruction* Instr = Current;
    D9SvDest* Dest = &Instr->Dest;
    UINT Op = Instr->Opcode;
    BOOLEAN Three = Model == 3;
    BOOLEAN PlainDest = Dest->Mask == D9SvMaskAll && Dest->Modifier == 0 && Dest->Shift == 0 && Dest->Relative == 0;

    if (Op != D9SvOpDcl)
    {
        if (Op == D9SvOpDefb || Op == D9SvOpDefi || Op == D9SvOpDef)
        {
            if (!DefOpen)
                Fail(2, Three ? 0x2D9 : 0x17A, "def or dcl instructions must appear before other instructions.");
        }
        else
        {
            DefOpen = FALSE;
        }
    }

    if (Op == D9SvOpDef)
    {
        BOOLEAN Good = TRUE;

        if (!PlainDest || Dest->Type != D9SvRegConst)
        {
            Fail(2, Three ? 0x24B : 0xDF, "Destination for def instruction must be of the form c# (where # = reg "
                 "number). In addition there may be no mask (equivalent to full .xyzw, which is fine).");
            Good = FALSE;
        }
        if (Dest->Type == D9SvRegConst && Dest->Number >= Consts.Count)
        {
            Fail(2, Three ? 0x251 : 0x24D, "Invalid const register num: %d. Max allowed is %d.", Dest->Number,
                 static_cast<INT>(Consts.Count) - 1);
        }
        else if (Good)
        {
            if (Consts.Marks[0][Dest->Number] != 0)
                Fail(2, Three ? 0x252 : 0x24E, "c%d already def'd in the shader. ", Dest->Number);
            Consts.Marks[0][Dest->Number] = 1;
        }
    }

    if (Op == D9SvOpDefi)
    {
        if (!PlainDest || Dest->Type != D9SvRegConstInt)
            Fail(2, Three ? 0x2DC : 0x17B, "Destination for defi instruction must be i# register (where # = reg "
                 "number). In addition there may be no mask (equivalent to full .xyzw, which is fine).");
        if (Dest->Type == D9SvRegConstInt)
        {
            UINT Number = Dest->Number;

            if (Number < IntConsts.Count)
            {
                INT Count = static_cast<INT>(Instr->Values[0]);

                if (Count < 0)
                {
                    if (Three)
                        Fail(2, 0x3DE, "i%d.x (which represents loop or rep iteration count) cannot be negative. i%d.x "
                             "is currently def'd as %d. ", Number, Number, Count);
                    else
                        Fail(2, 0x22B, "i%d.x (which represents rep iteration count) cannot be negative. i%d.x is "
                             "currently def'd as %d. ", Number, Number, Count);
                }
                if (Version != (Three ? D9PsVersion3sw : D9PsVersion2sw))
                {
                    if (Count > 255)
                    {
                        if (Three)
                            Fail(2, 0x3E3, "i%d.x (which represents loop or rep iteration count) cannot be greater than "
                                 "%d. i%d.x is currently def'd as %d. ", Number, 255, Number, Count);
                        else
                            Fail(2, 0x22C, "i%d.x (which represents rep iteration count) cannot be greater than %d. "
                                 "i%d.x is currently def'd as %d. ", Number, 255, Number, Count);
                    }
                    if (Three && Instr->Values[1] > 0xFF)
                        Fail(2, 0x22E, "i%d.y (which represents loop initial value) must be in the range [0, %d]. "
                             "i%d.y is currently def'd as %d. ", Number, 255, Number,
                             static_cast<INT>(Instr->Values[1]));
                    if (Three && Instr->Values[2] + 128 > 0xFF)
                        Fail(2, 0x230, "i%d.z (which represents loop step value) must be in the range [-%d, %d]. i%d.z "
                             "is currently def'd as %d. ", Number, 128, 127, Number,
                             static_cast<INT>(Instr->Values[2]));
                }
                if (Three)
                {
                    if (Instr->Values[3] != 0)
                        Fail(2, 0x3E4, "i%d.w has no meaning; it must simply be 0. ", Number);
                }
                else if (Instr->Values[1] != 0 || Instr->Values[2] != 0 || Instr->Values[3] != 0)
                {
                    Fail(2, 0x3E1, "i%d.yzw have no meaning in this shader model; they must simply be 0. ", Number);
                }
                if (IntConsts.Marks[0][Number] != 0)
                    Fail(2, Three ? 0x253 : 0x24F, "i%d already def'd in the shader. ", Number);
                IntConsts.Marks[0][Number] = 1;
            }
            else
            {
                Fail(2, Three ? 0x3DB : 0x22A, "Invalid constant integer (i#) register num: %d. Max allowed is %d.",
                     Number, static_cast<INT>(IntConsts.Count) - 1);
            }
        }
    }

    if (Op == D9SvOpDefb)
    {
        if (!PlainDest || Dest->Type != D9SvRegConstBool)
            Fail(2, Three ? 0x3F3 : 0x233, "Destination for defb instruction must be b# register (where # = reg "
                 "number). In addition there may be no mask (equivalent to full .xyzw, which is fine).");
        if (Dest->Type == D9SvRegConstBool)
        {
            UINT Number = Dest->Number;

            if (Number < BoolConsts.Count)
            {
                if (Instr->Values[0] > 1)
                    Fail(2, Three ? 0x3E5 : 0x3E2, "b%d must only be defined as true (0x00000001) or false "
                         "(0x00000000). ", Number);
                if (BoolConsts.Marks[0][Number] != 0)
                    Fail(2, Three ? 0x254 : 0x250, "b%d already def'd in the shader. ", Number);
                BoolConsts.Marks[0][Number] = 1;
            }
            else
            {
                Fail(2, Three ? 0x3F0 : 0x232, "Invalid constant bool (b#) register num: %d. Max allowed is %d.",
                     Number, static_cast<INT>(BoolConsts.Count) - 1);
            }
        }
    }
}

/* ps_2_* declarations of v#, t# and s# */
VOID
D9PsChecker::Declaration2()
{
    D9SvInstruction* Instr = Current;
    D9SvDest* Dest = &Instr->Dest;
    D9SvRegisters* File;
    PCSTR Name;
    BYTE Fill[4] = {};
    UINT Number;

    if (Instr->Opcode != D9SvOpDcl)
    {
        if (Instr->Opcode != D9SvOpDef && Instr->Opcode != D9SvOpDefb && Instr->Opcode != D9SvOpDefi)
            DclOpen = FALSE;
        return;
    }
    if (!DclOpen)
        Fail(2, 0xD7, "def and dcl instructions must appear before other instructions.");
    if (!Dest->Present)
    {
        Fail(2, 216, "dcl instruction is missing a parameter.");
        return;
    }
    switch (Dest->Type)
    {
    case D9SvRegInput:
        Name = "v";
        File = &Inputs;
        break;
    case D9PsRegTexture:
        Name = "t";
        File = &Texcoords;
        break;
    case D9SvRegSampler:
        Name = "s";
        File = &Samplers;
        break;
    default:
        File = nullptr;
        Name = nullptr;
        break;
    }
    if (File == nullptr || File->Count == 0)
    {
        Fail(2, 217, "Invalid register type for dcl.  Must be t#, v# or s#.");
        return;
    }
    Number = Dest->Number;
    if (Number >= File->Count)
    {
        Fail(2, 0xDA, "Invalid register number: %d. Max allowed for %s# register is %d.", Number, Name,
             static_cast<INT>(File->Count) - 1);
        return;
    }
    if (File->Written(Number) != 0)
    {
        Fail(2, 0xDB, "Register %s%d has already been declared.", Name, Number);
        return;
    }

    if (Dest->Type == D9SvRegInput)
    {
        for (UINT Component = 0; Component < 4; Component++)
            Fill[Component] = (Dest->Mask & D9SvComponent(Component)) != 0;
        if (Dest->Modifier != 0)
        {
            Fail(2, 280, "dcl %s# does not support any modifiers.", Name);
            return;
        }
        if (Dest->Mask == 0)
        {
            Fail(2, 403, "dcl must specify non-empty mask.");
            return;
        }
    }
    else if (Dest->Type == D9PsRegTexture)
    {
        switch (Dest->Mask)
        {
        case 0xF0000:
            Fill[3] = 1;
            /* Fall through */
        case 0x70000:
            Fill[2] = 1;
            /* Fall through */
        case 0x30000:
            Fill[1] = 1;
            /* Fall through */
        case 0x10000:
            break;
        default:
            Fail(2, 220, "dcl %s# must have one of the following masks: x, xy, xyz or xyzw (alternatively: r, rg, rgb, "
                 "or rgba).", Name);
            return;
        }
        Fill[0] = 1;
        if ((Dest->Modifier & 0xFF9FFFFF) != 0)
        {
            Fail(2, 536, "dcl %s# only supports the _centroid modifier (optional hint), and/or _pp modifier (partial "
                 "precision hint). No other modifiers supported.", Name);
            return;
        }
    }
    else
    {
        DWORD Kind = Instr->DclInfo & 0x78000000;

        if (Kind == 0x18000000)
            CubeSamplers |= 1u << Number;
        else if (Kind != 0x10000000 && Kind != 0x20000000)
        {
            Fail(2, 221, "dcl %s# must be one of the following types: dcl_2d, dcl_cube or dcl_volume.", Name);
            return;
        }
        if (Kind != 0x10000000)
            Fill[2] = 1;
        Fill[0] = 1;
        Fill[1] = 1;
        if (Dest->Mask != D9SvMaskAll)
        {
            Fail(2, 424, "dcl %s# must not specify a writemask (equivalent to a full mask). When samplers are used for "
                 "texture lookup, 4 components are always returned, including defaults for components that may not be "
                 "present in the texture format set at the sampler. ", Name);
            return;
        }
        if (Dest->Modifier != 0)
        {
            Fail(2, 968, "dcl %s# does not support any modifiers. ", Name);
            return;
        }
    }
    for (UINT Component = 0; Component < 4; Component++)
        File->Marks[Component][Number] |= Fill[Component];
}

/* ps_3_* declarations of v#, s#, vPos and vFace */
VOID
D9PsChecker::Declaration3()
{
    D9SvInstruction* Instr = Current;
    D9SvDest* Dest = &Instr->Dest;
    D9SvRegisters* File;
    PCSTR Name = nullptr;
    BYTE Fill[4] = {};
    UINT Number = Dest->Number;
    BOOLEAN IsInput = FALSE;
    BOOLEAN IsSampler = FALSE;
    BOOLEAN IsPos = FALSE;
    BOOLEAN IsFace = FALSE;

    if (Instr->Opcode != D9SvOpDcl)
    {
        if (Instr->Opcode != D9SvOpDef && Instr->Opcode != D9SvOpDefb && Instr->Opcode != D9SvOpDefi)
            DclOpen = FALSE;
        return;
    }
    if (!DclOpen)
        Fail(2, 0x129, "def and dcl instructions must appear before other instructions.");
    if (!Dest->Present)
    {
        Fail(2, 298, "dcl instruction is missing a parameter.");
        return;
    }
    switch (Dest->Type)
    {
    case D9SvRegInput:
        Name = "v";
        File = &Inputs;
        IsInput = TRUE;
        break;
    case D9SvRegSampler:
        Name = "s";
        File = &Samplers;
        IsSampler = TRUE;
        break;
    case D9SvRegMisc:
        File = &Misc;
        if (Number == 0)
        {
            Name = "vPos";
            IsPos = TRUE;
        }
        else if (Number == 1)
        {
            Name = "vFace";
            IsFace = TRUE;
        }
        break;
    default:
        Fail(2, 299, "Invalid register type for dcl.  Must be input register (v#), sampler (s#), position (vPos) or "
             "face (vFace).");
        return;
    }
    if (Dest->Type != D9SvRegMisc && Number >= File->Count)
    {
        Fail(2, 0x12C, "Invalid register number: %d. Max allowed for %s# register is %d.", Number, Name,
             static_cast<INT>(File->Count) - 1);
        return;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 473, "dcl can't specify empty writemask.");
        return;
    }
    if (IsSampler || IsFace || IsPos)
    {
        if (Dest->Modifier != 0)
        {
            Fail(2, 969, "dcl for %s register does not support any modifiers. ", Name);
            return;
        }
    }
    else if ((Dest->Modifier & 0xFF9FFFFF) != 0)
    {
        Fail(2, 970, "dcl %s# only supports the (optional) _pp modifier (partial precision hint), and/or the _centroid "
             "modifier (relevant to multisampling). No other modifiers supported.", Name);
        return;
    }

    if (IsSampler)
    {
        DWORD Kind;

        if ((File->Written(Number) & Dest->Mask) != 0)
        {
            Fail(2, 769, "Sampler %s%d already declared. ", Name, Number);
            return;
        }
        Kind = Instr->DclInfo & 0x78000000;
        if (Kind == 0x18000000)
            CubeSamplers |= 1u << Number;
        else if (Kind != 0x10000000 && Kind != 0x20000000)
        {
            Fail(2, 585, "dcl %s# must be one of the following types: dcl_2d, dcl_cube or dcl_volume.", Name);
            return;
        }
        if (Kind != 0x10000000)
            Fill[2] = 1;
        Fill[0] = 1;
        Fill[1] = 1;
        if (Dest->Mask != D9SvMaskAll)
        {
            Fail(2, 770, "dcl %s# must not specify a writemask (equivalent to a full mask). ", Name);
            return;
        }
    }
    else if (IsInput)
    {
        DWORD Overlap = File->Written(Number) & Dest->Mask;
        UINT Usage;
        UINT UsageIndex;
        BYTE Semantic;

        if (Overlap != 0)
        {
            char Text[32];

            ComponentText(Overlap, FALSE, TRUE, Text);
            Fail(2, 0x12D, "Declaration for register %s%d overlaps previous declaration for the following component(s) "
                 "of the same register (*): %s", Name, Number, Text);
            return;
        }
        Usage = Instr->DclInfo & 0xFFFF;
        UsageIndex = Instr->DclInfo >> 16;
        if (Usage > 13)
        {
            Fail(2, 302, "Unrecognized usage type in dcl statement.");
            return;
        }
        if (UsageIndex > 15)
        {
            Fail(2, 0x12F, "dcl usage index, %d, is too high.  Max allowed is %d.", UsageIndex, 15);
            return;
        }

        /* Centroid must be all or nothing per register; color counts as centroid */
        if (Usage == 10)
        {
            if (Centroid[Number] == 2)
                Fail(2, 0x430, "When multple usages are declared for various components of a given input v# register, "
                     "the _centroid hint can only be specified on either (a) all usages declared for the register, or "
                     "(b) on none of them.  Also note that the usage 'color' (with any index) has special behavior: "
                     "even if the centroid hint is not specified for color, it is assumed to be set. ");
            else
                Centroid[Number] = 1;
        }
        else
        {
            BOOLEAN Wants = (Dest->Modifier & 0x400000) != 0;
            UINT Clash = Wants ? 2 : 1;

            if (Centroid[Number] == 0)
                Centroid[Number] = Wants ? 1 : 2;
            else if (Centroid[Number] == Clash)
                Fail(2, Wants ? 1073 : 1067, "When multple usages are declared for various components of a given input "
                     "v# register, the _centroid hint can only be specified on either (a) all usages declared for the "
                     "register, or (b) on none of them.  Also note that the usage 'color' (with any index) has special "
                     "behavior: even if the centroid hint is not specified for color, it is assumed to be set. ");

            if (Usage == 4 && UsageIndex == 0)
            {
                Fail(2, 537, "dcl usage+index psize0 cannot be used in a pixel shader.");
                return;
            }
            if (Usage == 9 && UsageIndex == 0)
            {
                Fail(2, 972, "dcl usage+index positiont0 cannot be used in a pixel shader.");
                return;
            }
            if (Usage == 8)
            {
                Fail(2, 973, "dcl usage 'tessfactor' is not avaliable to the pixel shader. ");
                return;
            }
            if (Usage == 13)
            {
                Fail(2, 974, "dcl usage 'sample' is not avaliable to the pixel shader. ");
                return;
            }
            if (Usage == 0 && UsageIndex == 0)
            {
                Fail(2, 0x3CB, "dcl for input v# register must specify usage+index (semantic). i.e. dcl_texcoord5 v%d. "
                     "Note that the semantics position0, positiont0, psize0, tessfactor(n) and sample(n) are not "
                     "permitted.", Number);
                return;
            }
        }

        Semantic = static_cast<BYTE>(Usage | (UsageIndex << 4));
        for (UINT Entry = 0; Entry < InputCount3; Entry++)
        {
            if (Inputs3[Entry].Semantic == Semantic)
            {
                Fail(2, 304, "dcl usage+index: %s,%d has already been specified for an input register.",
                     D9SvUsageName(Usage), UsageIndex);
                return;
            }
        }
        if (InputCount3 < _countof(Inputs3))
        {
            Inputs3[InputCount3].Semantic = Semantic;
            Inputs3[InputCount3].Register = Number;
            Inputs3[InputCount3].Mask = Dest->Mask;
            InputCount3 += 1;
        }
    }
    else if (IsPos || IsFace)
    {
        if (File->Written(Number) != 0)
        {
            Fail(2, 630, "%s register already declared.", Name);
            return;
        }
    }

    if (IsInput || IsPos || IsFace)
    {
        for (UINT Component = 0; Component < 4; Component++)
            Fill[Component] = (Dest->Mask & D9SvComponent(Component)) != 0;
        if (IsPos && (Fill[2] != 0 || Fill[3] != 0))
        {
            Fail(2, 631, "z or w components of vPos register are not available in this shader version, and cannot be "
                 "dcl'd.");
            return;
        }
    }
    if (Number < File->Count)
    {
        for (UINT Component = 0; Component < 4; Component++)
            File->Marks[Component][Number] |= Fill[Component];
    }
}

/* nrm, crs, pow, lrp, sincos and dp2add operand rules */
VOID
D9PsChecker::Arithmetic()
{
    D9PsInstruction* Instr = Ins();
    D9SvDest* Dest = &Instr->Dest;
    D9SvSource* Source = Instr->Source;
    BOOLEAN Three = Model == 3;
    UINT Op = Instr->Opcode;

    if (Op == D9SvOpNrm)
    {
        if (Source[0].Type == Dest->Type && Source[0].Number == Dest->Number)
            Fail(2, Three ? 0x25C : 0xE1, "Src and dest registers for NRM cannot be the same.");
        if (((Dest->Mask - D9SvMaskXyz) & 0xFFF7FFFF) != 0)
            Fail(2, Three ? 0x212 : 0xC2, "Dest writemask for NRM must be .xyzw (default) or .xyz (or equivalent rgba "
                 "notation)");
    }
    if (Op == D9SvOpCrs)
    {
        if ((Source[0].Type == Dest->Type && Source[0].Number == Dest->Number) ||
            (Source[1].Type == Dest->Type && Source[1].Number == Dest->Number))
            Fail(2, Three ? 0x2F9 : 0x1A2, "Dest register for CRS cannot be the same as a source register.");
        if (Source[0].Swizzle != D9SvSwizzleIdentity || Source[1].Swizzle != D9SvSwizzleIdentity)
            Fail(2, Three ? 0x2FA : 0x1A3, "Swizzle not permitted on source paramters to CRS.");
        if ((Dest->Mask & D9SvComponentW) != 0)
            Fail(2, Three ? 0x2FB : 0x1A4, "Write mask for CRS must be one of: .x | .y | .z | .xy | .xz | .yz | .xyz "
                 "(or rgba equivalent)");
    }
    if (Op == D9SvOpPow && Source[1].Type == Dest->Type && Source[1].Number == Dest->Number)
        Fail(2, Three ? 0x2FC : 0x1A5, "Dest register for POW cannot be the same as second source register.");
    if (Op == D9SvOpLrp && ((Source[0].Type == Dest->Type && Source[0].Number == Dest->Number) ||
                            (Source[2].Type == Dest->Type && Source[2].Number == Dest->Number)))
        Fail(2, Three ? 0x2FE : 0x2FD, "Dest register for LRP cannot be the same as first or third source register.");
    if (Op == D9SvOpSincos)
    {
        if (Source[0].Type == Dest->Type && Source[0].Number == Dest->Number)
            Fail(2, Three ? 0x2FF : 0x1A7, "Dest register for SINCOS cannot be the same as first source register.");
        if (Three)
        {
            if (Dest->Type != D9SvRegTemp)
                Fail(2, 0x433, "Dest for SINCOS must be a temp (r#) register.");
            if (!D9SvReplicate(Source[0].Swizzle))
                Fail(2, 0x25D, "Source selector for first source parameter for SINCOS must be .x, .y, .z or .w.");
        }
        else
        {
            BOOLEAN SecondConst = Source[1].Type == D9SvRegConst;

            if (!SecondConst)
                Fail(2, 0xE2, "Second source paramter for SINCOS must be a constant register.");
            if (Source[2].Type != D9SvRegConst)
                Fail(2, 0xE3, "Third source paramter for SINCOS must be a constant register.");
            else if (SecondConst && Source[1].Number == Source[2].Number)
                Fail(2, 0xE4, "Second and third source parameters for SINCOS cannot be the same constant register.");
            if (!D9SvReplicate(Source[0].Swizzle))
                Fail(2, 0xE5, "Source selector for first source parameter for SINCOS must be .x, .y, .z or .w.");
            for (UINT Index = 1; Index < 3; Index++)
            {
                if (Source[Index].Modifier != 0)
                    Fail(2, 0xE6, "Second and third source parameters for SINCOS cannot have a modifier.");
                if (Source[Index].Swizzle != D9SvSwizzleIdentity)
                    Fail(2, 0xE7, "Second and third source parameters for SINCOS must use default swizzle (no swizzle "
                         "specified, or .xyzw). ");
            }
        }
    }
    if (Op == D9PsOpDp2add && Source[2].Present && !D9SvReplicate(Source[2].Swizzle))
        Fail(2, Three ? 0x3C1 : 0x207, "Third source parameter for dp2add must use a replicate swizzle (.r, .g, .b, "
             ".a) to select a scalar component.");
    if (Instr->CoIssue)
        Fail(2, Three ? 0x25E : 0xE8, "Co-issue ('+') is not supported in this shader version.");
}

/* ps_2_*: oC# writes; at the end, which colors were written */
VOID
D9PsChecker::ColorWrite()
{
    D9SvInstruction* Instr = Current;

    if (Ended)
    {
        if (!Basic)
            return;
        if ((ColorsWritten & 1) == 0)
            Fail(6, 0x195, "Pixel shader must minimally write to oC0 (with a mov instruction).");
        if (((ColorsWritten + 1) & ColorsWritten) != 0)
        {
            char Name[32];

            D9SvVersionName(Version, Name);
            Fail(6, 0x23D, "When a %s shader writes to oCn, it must write to all of oC(n-1) down to oC0 (no gaps).",
                 Name);
        }
        return;
    }
    if (Instr->Dest.Type != D9SvRegColorOut)
        return;
    if (Instr->Opcode != D9SvOpMov)
    {
        Fail(2, 0x196, "oCn registers may only be written using the mov instruction.");
        return;
    }
    if (!SkipSource[0])
    {
        if (Instr->Source[0].Modifier != 0)
            Fail(2, 0x197, "Source parameter for instruction writing to oCn cannot use a modifier.");
        if (Instr->Source[0].Swizzle != D9SvSwizzleIdentity)
            Fail(2, 0x198, "Source parameter for instruction writing to oCn cannot use swizzles.");
    }
    if (Instr->Dest.Number < Colors.Count)
    {
        if ((Instr->Dest.Modifier & 0x100000) != 0)
            Fail(2, 0x200, "Write to oCn register cannot use _sat instruction modifier.");
        if (Instr->Dest.Mask != D9SvMaskAll)
            Fail(2, 0x19A, "Write to oCn register must use full writemask (equivalent to not specifying writemask at "
                 "all).");
        if (Basic)
        {
            if ((ColorsWritten & (1u << Instr->Dest.Number)) != 0)
                Fail(2, 0x19B, "oC%d already written. (oCn registers can only be written at most once each)",
                     Instr->Dest.Number);
            ColorsWritten |= 1u << Instr->Dest.Number;
        }
    }
}

VOID
D9PsChecker::DepthWrite2()
{
    D9SvInstruction* Instr = Current;

    if (Instr->Dest.Type != D9SvRegDepthOut)
        return;
    if (Instr->Opcode != D9SvOpMov)
    {
        Fail(2, 0x19C, "oDepth may only be written using the mov instruction.");
        return;
    }
    if (!SkipSource[0])
    {
        if (Instr->Source[0].Modifier != 0)
            Fail(2, 0x19D, "Source parameter for instruction writing to oDepth cannot use a modifier.");
        if (!D9SvReplicate(Instr->Source[0].Swizzle))
            Fail(2, 0x19E, "Source parameter for instruction writing to oDepth must use a replicate source swizzle. "
                 "i.e.: .x | .y | .z | .w or .r | .g | .b | .a.  This identifies the scalar component of the source to "
                 "send to oDepth.");
    }
    if (Instr->Dest.Number < Depth.Count)
    {
        if ((Instr->Dest.Modifier & 0x100000) != 0)
            Fail(2, 0x1FF, "Write to oDepth register cannot use _sat instruction modifier.");
        if (Instr->Dest.Mask != D9SvMaskAll)
            Fail(2, 0x19F, "Write to oDepth register must not specify any writemask. (Note, this is equivalent to "
                 "using a full writemask) oDepth is a scalar output, however a specific rgba/xyzw channel does not "
                 "apply; the writemask is meaningless. ");
        if (Basic && DepthWritten)
            Fail(2, 0x1A0, "oDepth already written.");
        DepthWritten = TRUE;
    }
}

VOID
D9PsChecker::DepthWrite3()
{
    D9SvInstruction* Instr = Current;
    UINT Op = Instr->Opcode;

    if (Instr->Dest.Type != D9SvRegDepthOut)
        return;
    switch (Op)
    {
    case 7: case 20: case 21: case 22: case 23: case 24: case 33: case 36: case 37: case 65: case 66: case 93:
    case 94: case 95:
        Fail(2, 758, "oDepth may not be written by the %s instruction.", D9SvOpcodeName(Op));
        break;
    case 8: case 9: case 90:
        break;
    default:
        for (UINT Index = 0; Index < Instr->SourceCount; Index++)
        {
            if (!D9SvReplicate(Instr->Source[Index].Swizzle))
            {
                Fail(2, 1062, "When writing to oDepth, %s instruction must use replicate swizzle on source "
                     "parameter(s), in order to select single component. i.e. .x | .y | .z | .w (or rgba equivalent)",
                     D9SvOpcodeName(Op));
                break;
            }
        }
        break;
    }
    if (Instr->Dest.Mask != D9SvMaskAll)
        Fail(2, 0x2F7, "Write to oDepth register must not specify any writemask. (Note, this is equivalent to using a "
             "full writemask) oDepth is a scalar output, however a specific rgba/xyzw channel does not apply -> the "
             "writemask is meaningless. ");
}

/* A nesting limit hit: reports it the way the target asked and stops validation */
BOOLEAN
D9PsChecker::ReportNesting(
    _In_ DWORD TargetId,
    _In_z_ PCSTR TargetFormat,
    _In_ DWORD Id,
    _In_z_ PCSTR Format,
    _In_ UINT Limit)
{
    if (TargetMessages())
    {
        char Name[32];

        D9SvVersionName(Version, Name);
        Report(0x10002, TargetId, TargetFormat, Limit, Name);
    }
    else
    {
        Report(2, Id, Format, Limit);
    }
    Errors += 1;
    return FALSE;
}

/* Flow control nesting, subroutines and labels */
BOOLEAN
D9PsChecker::FlowControl()
{
    D9SvInstruction* Instr = Current;
    UINT Op = Instr->Opcode;
    BOOLEAN Three = Model == 3;
    UINT Kind = Flow->TopKind();

    if (Version == D9PsVersion20)
        return TRUE;

    if ((Op == 25 || Op == 26 || Op == 30) && !SkipSource[0] && Instr->Source[0].Type == D9SvRegLabel)
    {
        UINT Label = Instr->Source[0].Number;

        if (Label >= 0x800)
        {
            Fail(2, Three ? 0x332 : 0x1EA, "Subroutine label number, %d, is too large. Maximum label number is %d. "
                 "Aborting shader validation.", Label, 2047);
            return FALSE;
        }
        if (Defined.Has(Label))
        {
            if (Op == 30)
                Fail(2, Three ? 821 : 491, "Subroutine label number: %d already defined earlier. Aborting shader "
                     "validation.", Label);
            else
                Fail(2, Three ? 824 : 492, "Cannot call subroutine label defined earlier in shader (label #%d in this "
                     "case).  Subroutine call must reference label defined later in shader. Aborting shader "
                     "validation. ", Label);
            return FALSE;
        }
    }

    switch (Op)
    {
    case 25:
    case 26:
    {
        BOOLEAN Dynamic;

        if (SkipSource[0])
            break;
        Dynamic = Op == 26 && !SkipSource[1] && Instr->Source[1].Type == D9SvRegPredicate;
        Called.Add(Instr->Source[0].Number);
        if (Dynamic)
        {
            if (Flow->DynamicIfDepth >= DynamicIfLimit)
            {
                if (Three)
                    return ReportNesting(1049, "Compiled code results in dynamic flow control nesting depth that "
                                         "exceeds limit of %d for target (%s). ", 1050, "Depth of dynamic 'if' nesting "
                                         "exceeds limit of %d (callnz with predicate counts as a dynamic 'if' nesting "
                                         "level). Aborting shader validation. ", DynamicIfLimit);
                return ReportNesting(1047, "Compiled code results in dynamic flow control nesting depth that exceeds "
                                     "limit of %d for target (%s). ", 1048, "Depth of dynamic 'if' nesting exceeds "
                                     "limit of %d (callnz with predicate counts as a dynamic 'if' nesting level). "
                                     "Aborting shader validation. ", DynamicIfLimit);
            }
            Flow->Push(D9SvBlockDynamicIf);
        }
        if (Flow->CallDepth >= CallLimit)
            return ReportNesting(Three ? 850 : 498, "Compiled code results in subroutine call nesting depth that "
                                 "exceeds limit of %d for target (%s). ", Three ? 851 : 843, "Depth of subroutine call "
                                 "nesting exceeds limit of %d. Aborting shader validation. ", CallLimit);
        if (!Flow->Call(Instr->Source[0].Number))
        {
            Report(6, 0, "Out of memory. Aborting shader validation. ");
            Errors += 1;
            return FALSE;
        }
        if (Dynamic)
            Flow->Pop();
        break;
    }
    case 27:
    case 38:
        if (Op == 27 && !Three)
            break;
        if ((Three ? Flow->LoopOrRepDepth : Flow->RepDepth) >= LoopLimit)
            return ReportNesting(Three ? 852 : 844, "Compiled code results in loop nesting depth that exceeds limit of "
                                 "%d for target (%s). ", Three ? 827 : 493, "Depth of loop nesting exceeds limit of "
                                 "%d. Aborting shader validation. ", LoopLimit);
        Flow->Push(Op == 27 ? D9SvBlockLoop : D9SvBlockRep);
        break;
    case 28:
        if (Kind != D9SvBlockNone)
        {
            Fail(2, Three ? 833 : 495, "'ret' instruction not permitted from inside any flow control block. Aborting "
                 "shader validation. ");
            return FALSE;
        }
        Flow->Returned();
        break;
    case 29:
        if (!Three)
            break;
        if (Kind != D9SvBlockLoop)
        {
            Fail(2, 494, "'endloop' instruction does not match up to an 'loop'. Aborting shader validation. ");
            return FALSE;
        }
        Flow->Pop();
        break;
    case 30:
        if (SkipSource[0])
            break;
        if (!AfterRet)
        {
            Fail(2, Three ? 813 : 489, "'label' is only permitted directly after a 'ret' instruction. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Defined.Add(Instr->Source[0].Number);
        if (Kind != D9SvBlockNone)
        {
            Fail(2, Three ? 814 : 812, "Label is defined inside a subroutine or main program. Label must follow the "
                 "ret instruction. Aborting shader validation. ");
            return FALSE;
        }
        Flow->BeginSubroutine(Instr->Source[0].Number & 0x7FF);
        break;
    case 39:
        if (Kind != D9SvBlockRep)
        {
            Fail(2, Three ? 837 : 836, "'endrep' instruction does not match up to a 'rep'. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Flow->Pop();
        break;
    case 40:
        if (SkipSource[0])
            break;
        if (Instr->Source[0].Type == D9SvRegPredicate)
        {
            if (Flow->DynamicIfDepth >= DynamicIfLimit)
                return ReportNesting(Three ? 853 : 845, "Compiled code results in dynamic 'if'/'endif' nesting depth "
                                     "that exceeds limit of %d for target (%s). ", Three ? 854 : 846, "Depth of dynamic "
                                     "'if' nesting exceeds limit of %d. Aborting shader validation. ", DynamicIfLimit);
            Flow->Push(D9SvBlockDynamicIf);
        }
        else
        {
            if (Flow->StaticIfDepth >= StaticIfLimit)
                return ReportNesting(Three ? 855 : 1022, "Compiled code results in static 'if'/'endif' nesting depth "
                                     "that exceeds limit of %d for target (%s). ", Three ? 856 : 847, "Depth of static "
                                     "'if' nesting exceeds limit of %d. Aborting shader validation. ", StaticIfLimit);
            Flow->Push(D9SvBlockStaticIf);
        }
        break;
    case 41:
        if (Flow->DynamicIfDepth >= DynamicIfLimit)
            return ReportNesting(Three ? 857 : 848, "Compiled code results in dynamic 'if'/'endif' nesting depth that "
                                 "exceeds limit of %d for target (%s). ", Three ? 858 : 849, "Depth of dynamic 'if' "
                                 "nesting exceeds limit of %d. Aborting shader validation. ", DynamicIfLimit);
        Flow->Push(D9SvBlockDynamicIf);
        break;
    case 42:
        if (Kind != D9SvBlockDynamicIf && Kind != D9SvBlockStaticIf)
        {
            Fail(2, Three ? 895 : 506, "'else' instruction does not match up to an 'if'. Aborting shader validation. ");
            return FALSE;
        }
        if (Flow->Blocks[Flow->Depth].SeenElse)
        {
            Fail(2, Three ? 896 : 893, "'else' already used for current 'if' construct. Aborting shader validation. ");
            return FALSE;
        }
        Flow->Blocks[Flow->Depth].SeenElse = TRUE;
        break;
    case 43:
        if (Kind != D9SvBlockDynamicIf && Kind != D9SvBlockStaticIf)
        {
            Fail(2, Three ? 840 : 497, "'endif' instruction does not match up to an 'if' or 'else'. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Flow->Pop();
        break;
    case 44:
    case 45:
    case 96:
        if (Op == 45 && Flow->DynamicIfDepth >= DynamicIfLimit)
        {
            if (Three)
                return ReportNesting(1043, "Compiled code results in dynamic 'if'/'endif' nesting depth that exceeds "
                                     "limit of %d for target (%s) (due to conditional break that counts towards the "
                                     "same limit). ", 614, "Depth of dynamic 'if' nesting exceeds limit of %d, due to "
                                     "break_cmp that counts towards same limit. Aborting shader validation. ",
                                     DynamicIfLimit);
            return ReportNesting(879, "Compiled code results in dynamic 'if'/'endif' nesting depth that exceeds limit "
                                 "of %d for target (%s) (due to conditional break that counts towards the same "
                                 "limit). ", 880, "Depth of dynamic 'if' nesting exceeds limit of %d, due to break_cmp "
                                 "that counts towards same limit. Aborting shader validation. ", DynamicIfLimit);
        }
        if (Flow->LoopOrRepCount != 0)
            Flow->LoopBroke[Flow->LoopOrRepCount - 1] = 1;
        else if (Three)
            Fail(2, 0x381, "'break' instruction not within a local loop/rep construct. Aborting shader validation. ");
        else
            Fail(2, 0x37E, "'break' instruction not within a local rep/endrep construct. Aborting shader "
                 "validation. ");
        break;
    default:
        break;
    }

    if (AfterRet && Op != 30)
    {
        Fail(2, Three ? 881 : 499, "A 'ret' instruction must be followed by either (1) the end of the shader or (2) a "
             "'label' instruction. Aborting shader validation.");
        return FALSE;
    }
    AfterRet = Op == 28;
    return TRUE;
}

VOID
D9PsChecker::FlowControlEnd()
{
    BOOLEAN Three = Model == 3;

    if (Version == D9PsVersion20)
        return;
    for (UINT Label = 0; Label < D9SvLabels; Label++)
    {
        if (Called.Has(Label) && !Defined.Has(Label))
            Fail(6, Three ? 0x374 : 0x1F7, "Subroutine with label #%d called, but never defined.", Label);
    }
    for (UINT Label = 0; Label < D9SvLabels; Label++)
    {
        if (Defined.Has(Label) && !Called.Has(Label))
            Fail(6, Three ? 0x377 : 0x1F8, "Subroutine label #%d never referenced.", Label);
    }
    if (Flow->InSubroutine)
    {
        Fail(6, Three ? 890 : 505, "End of shader reached without 'ret' to terminate subroutine.");
        return;
    }
    if (Three)
    {
        if (Flow->LoopDepth != 0)
            Fail(6, 0x389, "End of shader reached and '%s' construct was not completed with a '%s'. ", "loop",
                 "endloop");
        else if (Flow->RepDepth != 0)
            Fail(6, 0x389, "End of shader reached and '%s' construct was not completed with a '%s'. ", "rep",
                 "endrep");
        else if (Flow->InsideIf())
            Fail(6, 906, "End of shader reached and 'if' construct was not completed with 'else'/'endif'. ");
        return;
    }
    if (Flow->RepDepth != 0)
        Fail(6, 507, "End of shader reached and rep construct was not completed with an endrep. ");
    else if (Flow->InsideIf())
        Fail(6, 904, "End of shader reached and 'if' construct was not completed with 'else'/'endif'. ");
}

/* Gradients taken from temps inside dynamic flow control or a loop that breaks */
VOID
D9PsChecker::Gradients()
{
    D9SvInstruction* Instr = Current;
    UINT Op = Instr->Opcode;
    UINT Level = Flow->LoopOrRepCount;
    BOOLEAN Dynamic = FALSE;

    if (Version == D9PsVersion20)
        return;
    if (Op == D9PsOpTexld || Op == D9PsOpDsx || Op == D9PsOpDsy)
    {
        if (Instr->Source[0].Type == D9SvRegTemp)
        {
            if (Level != 0)
                Flow->LoopGradient[Level - 1] = 1;
            Dynamic = Flow->DynamicIfDepth != 0 || Instr->Predicated || Flow->FromBreakingLoop;
        }
    }
    else if (Op != 44 && Op != 45 && Op != 96)
    {
        return;
    }
    if ((Level != 0 && Flow->LoopBroke[Level - 1] != 0 && Flow->LoopGradient[Level - 1] != 0) || Dynamic)
    {
        if (Model == 3)
            Fail(2, 0x435, "texld/texldb/texldp/dsx/dsy instructions with r# as source cannot be used inside dynamic "
                 "conditional 'if' blocks, dynamic conditional subroutine calls, or loop/rep with break*. ");
        else
            Fail(2, 0x436, "texld/texldb/texldp/dsx/dsy instructions with r# as source cannot be used inside dynamic "
                 "conditional 'if' blocks, dynamic conditional subroutine calls, or rep with break*. ");
    }
}

/* ps_2_*: texture, arithmetic and total instruction slots */
VOID
D9PsChecker::CountSlots2()
{
    D9SvInstruction* Instr = Current;
    BOOLEAN Expanded = Version == D9PsVersion2x && Caps != nullptr && (PsCaps() & D9PsCapsTexLimit) != 0;
    UINT Arith = 0;
    UINT Total = 0;

    switch (Instr->Opcode)
    {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 14: case 15:
    case 19: case 28: case 35: case 42: case 43: case 44: case 88: case 94:
        Arith = 1;
        break;
    case 18: case 24: case 25: case 33: case 39: case 90: case 91: case 92:
        Arith = 2;
        break;
    case 20: case 22:
        Arith = 4;
        break;
    case 21: case 23: case 26: case 32: case 36: case 38: case 40: case 41: case 45: case 96:
        Arith = 3;
        break;
    case 37:
        Arith = 8;
        break;
    case D9PsOpTexkill:
        TexSlots += 1;
        Total = 1;
        if (Expanded)
        {
            Arith = 1;
            Total = 2;
        }
        break;
    case D9PsOpTexld:
        TexSlots += 1;
        Total = 1;
        if (Expanded)
        {
            BOOLEAN Cube = (CubeSamplers & (1u << (Instr->Source[1].Number & 31))) != 0;

            if (Instr->Controls == 0x10000)
            {
                Arith = Cube ? 3 : 2;
                Total = Cube ? 4 : 3;
            }
            else if (Instr->Controls == 0x20000)
            {
                Arith = 5;
                Total = 6;
            }
            else if (Cube)
            {
                Arith = 3;
                Total = 4;
            }
        }
        break;
    case D9PsOpTexldd:
        TexSlots += 1;
        Arith = 2;
        Total = 3;
        break;
    default:
        break;
    }
    if (Total == 0)
        Total = Arith;
    ArithSlots += Arith;
    TotalSlots += Total;
    if (Instr->Predicated)
    {
        ArithSlots += 1;
        TotalSlots += 1;
    }
}

/* ps_3_*: instruction slots */
VOID
D9PsChecker::CountSlots3()
{
    D9SvInstruction* Instr = Current;
    BOOLEAN Cube = (CubeSamplers & (1u << (Instr->Source[1].Number & 31))) != 0;
    UINT Slots = 0;

    switch (Instr->Opcode)
    {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 14: case 15:
    case 19: case 28: case 35: case 42: case 43: case 44: case 88: case 94:
        Slots = 1;
        break;
    case 18: case 24: case 25: case 29: case 33: case 39: case 65: case 90: case 91: case 92:
        Slots = 2;
        break;
    case 20: case 22:
        Slots = 4;
        break;
    case 21: case 23: case 26: case 27: case 32: case 36: case 38: case 40: case 41: case 45: case 93: case 96:
        Slots = 3;
        break;
    case 37:
        Slots = 8;
        break;
    case D9PsOpTexld:
        if (Instr->Controls == 0x20000)
            Slots = 6;
        else if (Cube)
            Slots = 4;
        else
            Slots = Instr->Controls == 0x10000 ? 3 : 1;
        break;
    case 95:
        Slots = Cube ? 5 : 2;
        break;
    default:
        break;
    }
    TotalSlots += Slots;
    if (Instr->Predicated)
        TotalSlots += 1;
}

VOID
D9PsChecker::CountSlotsEnd()
{
    char Name[32];

    D9SvVersionName(Version, Name);
    if (Model == 3)
    {
        if (TotalSlots > TotalLimit)
        {
            if (TargetMessages())
                Fail(0x10006, 0x262, "Compiled shader code uses too many instruction slots (%d). Max. allowed by the "
                     "target (%s) is %d.", TotalSlots, Name, TotalLimit);
            else
                Fail(6, 0x263, "Too many instruction slots (%d) used by shader. Max. allowed is %d.", TotalSlots,
                     TotalLimit);
        }
        return;
    }
    if (TexSlots > TexLimit)
    {
        if (TargetMessages())
            Fail(0x10006, 0xE9, "Compiled shader code has has many operations that use the texture unit (%d). Max. "
                 "allowed by the target (%s) is %d.", TexSlots, Name, TexLimit);
        else
            Fail(6, 0x25F, "Too many (%d) tex* instruction slots used. Max. allowed is %d.", TexSlots, TexLimit);
    }
    if (ArithSlots > ArithLimit)
    {
        if (TargetMessages())
            Fail(0x10006, 0x260, "Compiled shader code uses too many arithmetic instruction slots (%d). Max. allowed by "
                 "the target (%s) is %d.", ArithSlots, Name, ArithLimit);
        else
            Fail(6, 0xEA, "Too many (%d) standard (non tex*) instruction slots used. Max. allowed is %d.", ArithSlots,
                 ArithLimit);
    }
    if (TotalSlots > TotalLimit)
    {
        if (TargetMessages())
            Fail(0x10006, 0x261, "Compiled shader code uses too many instruction slots (%d). Max. allowed by the target "
                 "(%s) is %d.", TotalSlots, Name, TotalLimit);
        else
            Fail(6, 0x269, "Too many (%d) total instruction slots used. Max. allowed is %d.", TotalSlots, TotalLimit);
    }
}

/* ps_2_*: the highest texture coordinate the shader declares */
VOID
D9PsChecker::LogInputs()
{
    INT Highest = -1;

    if (Errors != 0)
    {
        Params->Outputs = MAXDWORD;
        return;
    }
    for (UINT Set = 0; Set < Texcoords.Count; Set++)
    {
        if (Texcoords.Written(Set) != 0)
            Highest = D9SvMax(Highest, static_cast<INT>(Set));
    }
    Params->Outputs = static_cast<DWORD>(Highest);
}

BOOLEAN
D9PsChecker::CheckInstruction()
{
    if (!Recognized())
        return FALSE;
    if (!(Model == 2 ? Supported2() : Supported3()) || !ParamCount())
        return TRUE;
    if (Model == 2)
    {
        SourceParams2();
        SourcePredicate();
        SourceInitialized2();
        DestParam2();
        Ports2();
        Definitions();
        Declaration2();
        Arithmetic();
        ColorWrite();
        DepthWrite2();
    }
    else
    {
        SourceParams3();
        SourcePredicate();
        SourceInitialized3();
        DestParam3();
        Ports3();
        Definitions();
        Declaration3();
        Arithmetic();
        DepthWrite3();
    }
    if (!FlowControl())
        return FALSE;
    Gradients();
    if (Model == 2)
        CountSlots2();
    else
        CountSlots3();
    return TRUE;
}

VOID
D9PsChecker::CheckEnd()
{
    if (Model == 2)
        ColorWrite();
    CountSlotsEnd();
    FlowControlEnd();
    if (Model == 2)
        LogInputs();
    if (Errors != 0)
    {
        Params->HardwareCapable = FALSE;
        Params->SoftwareCapable = FALSE;
    }
}
