/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shader rules for vs_1_1, vs_2_0, vs_2_x, vs_2_sw, vs_3_0 and vs_3_sw
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "shadervalidatorcore.hpp"

#include <new>

static constexpr DWORD D9VsVersion11 = 0xFFFE0101;
static constexpr DWORD D9VsVersion20 = 0xFFFE0200;
static constexpr DWORD D9VsVersion2x = 0xFFFE0201;
static constexpr DWORD D9VsVersion2sw = 0xFFFE02FF;
static constexpr DWORD D9VsVersion30 = 0xFFFE0300;
static constexpr DWORD D9VsVersion3sw = 0xFFFE03FF;

static const PCSTR D9VsOrdinals[] = { "first", "second", "third", "fourth" };

/* Message numbers of the rules vs_2_* and vs_3_* share; the two models number them apart */
struct D9VsIds
{
    DWORD OnlyMova;
    DWORD MovaDest;
    DWORD DestType;
    DWORD DestNumber;
    DWORD DestShift;
    DWORD DestEmpty;
    DWORD SetpOnly;
    DWORD SetpDest;
    DWORD ScalarSwizzle;
    DWORD MatrixMask;
    DWORD MatrixSameFirst;
    DWORD MatrixSameRows;
    DWORD DefOrder;
    DWORD DefDest;
    DWORD DefNumber;
    DWORD DefTwice;
    DWORD DefiDest;
    DWORD DefiNegative;
    DWORD DefiCount;
    DWORD DefiStart;
    DWORD DefiStep;
    DWORD DefiW;
    DWORD DefiTwice;
    DWORD DefiNumber;
    DWORD DefbDest;
    DWORD DefbValue;
    DWORD DefbTwice;
    DWORD DefbNumber;
    DWORD CrsSame;
    DWORD CrsSwizzle;
    DWORD CrsMask;
    DWORD CrsType;
    DWORD LrpSame;
    DWORD LrpType;
    DWORD NrmSame;
    DWORD NrmMask;
    DWORD NrmType;
    DWORD PowSame;
    DWORD PowType;
    DWORD SgnTemps;
    DWORD SgnFirst;
    DWORD SgnPlain;
    DWORD SincosSame;
    DWORD PortTemps;
    DWORD PortInputs;
    DWORD PortConsts;
    DWORD MatrixConsts;
    DWORD MatrixInputs;
    DWORD ConstMixed;
    DWORD ConstMixedRelative;
    DWORD ConstDifferent;
    DWORD InputMixed;
    DWORD InputMixedRelative;
    DWORD InputDifferent;
    DWORD CannotPredicate;
    DWORD PredicateRegister;
    DWORD PredicateSwizzle;
    DWORD PredicateModifier;
    DWORD SourceType;
    DWORD SoftwareNumber;
    DWORD SoftwareMatrix;
    DWORD HardwareNumber;
    DWORD HardwareMatrix;
    DWORD SourceModifier;
    DWORD Replicate;
    DWORD MatrixSwizzle;
    DWORD CallLabel;
    DWORD LoopCounter;
    DWORD LoopInteger;
    DWORD LabelParam;
    DWORD BreakpParam;
    DWORD CallnzCondition;
    DWORD LoopRegister;
    DWORD AddressRegister;
    DWORD LoopRepSwizzle;
    DWORD IfBoolSwizzle;
    DWORD IfPredicateSwizzle;
    DWORD CallnzPredicateSwizzle;
    DWORD LabelTooLarge;
    DWORD LabelTwice;
    DWORD CallBackwards;
    DWORD BreakOutside;
    DWORD DynamicCallnz;
    DWORD DynamicCallnzTarget;
    DWORD CallDepth;
    DWORD CallDepthTarget;
    DWORD LoopDepth;
    DWORD LoopDepthTarget;
    DWORD RetInside;
    DWORD EndloopAlone;
    DWORD LabelAfterRet;
    DWORD LabelInside;
    DWORD EndrepAlone;
    DWORD DynamicIf;
    DWORD DynamicIfTarget;
    DWORD StaticIf;
    DWORD StaticIfTarget;
    DWORD Ifc;
    DWORD IfcTarget;
    DWORD ElseAlone;
    DWORD ElseTwice;
    DWORD EndifAlone;
    DWORD Breakc;
    DWORD BreakcTarget;
    DWORD RetFollow;
    DWORD Undefined;
    DWORD Unreferenced;
    DWORD NoRet;
    DWORD OpenLoop;
    DWORD OpenIf;
};

static const D9VsIds D9Vs2Ids =
{
    711, 372, 0x2CB, 0x176, 0x178, 0x179, 716, 717, 0x18E, 0x1DE, 479, 480, 0x2DA, 0x2DD, 0x17C, 0x256, 0x2DE, 0x3DF,
    0x3E6, 0x3EC, 0x3EE, 0x3E7, 0x257, 0x3DC, 0x3F4, 0x3E8, 0x258, 0x3F1, 0x1C4, 0x1C5, 0x1C6, 0x1C3, 0x1C8, 0x1D3,
    0x1C1, 0x1C2, 0x1D0, 0x1C7, 0x1D1, 0x1D6, 0x1D7, 0x1D8, 0x1C9, 0x187, 0x188, 0x189, 0x18A, 0x18B, 0x2EB, 739, 743,
    0x2EA, 738, 742, 0x29E, 671, 0x2A0, 0x2A1, 680, 0x2AA, 0x2AD, 0x169, 0x16A, 697, 0x31A, 0x280, 935, 924, 936, 933,
    934, 919, 965, 535, 953, 954, 955, 952, 0x333, 822, 825, 0x384, 1053, 1052, 861, 860, 829, 862, 834, 831, 496, 815,
    838, 865, 864, 868, 867, 871, 870, 898, 899, 841, 557, 1044, 882, 0x375, 0x378, 891, 0x38B, 908,
};

static const D9VsIds D9Vs3Ids =
{
    712, 713, 0x2CE, 0x2D1, 0x2D4, 0x2D6, 719, 720, 0x308, 0x327, 808, 809, 0x2DB, 0x2DF, 0x2E1, 0x259, 0x2E0, 0x3E0,
    0x3E9, 0x3ED, 0x3EF, 0x3EA, 0x25A, 0x3DD, 0x3F5, 0x3EB, 0x25B, 0x3F2, 0x312, 0x313, 0x314, 0x311, 0x316, 0x31C,
    0x30F, 0x310, 0x317, 0x315, 0x318, 0x31D, 0x31E, 0x31F, 0x300, 0x2EE, 0x2EF, 0x2F0, 0x2F1, 0x2F2, 0x2ED, 741, 745,
    0x2EC, 740, 744, 0x2A3, 676, 0x2A5, 0x2A6, 681, 0x2AB, 0x2AE, 0x2AF, 0x2B0, 698, 0x31B, 641, 939, 925, 940, 937,
    938, 922, 966, 967, 958, 959, 960, 957, 0x334, 823, 826, 0x387, 1055, 1054, 873, 872, 830, 874, 835, 832, 816, 817,
    839, 876, 875, 878, 877, 575, 396, 901, 902, 842, 701, 417, 883, 0x376, 0x379, 892, 0x38D, 910,
};

/* A declared vs_3_0 register: usage and index packed as the runtime packs them, register and mask */
struct D9VsDeclared
{
    BYTE Usage;
    UINT Register;
    DWORD Mask;
};

class D9VsChecker : public D9SvChecker
{
public:
    UINT Model = 0;
    const D9VsIds* Ids = nullptr;
    BOOLEAN NoDevice = TRUE;
    UINT CapsConsts = 0;
    UINT ConstLimit = 0x2000;
    BOOLEAN DclOpen = TRUE;
    BOOLEAN DefOpen = TRUE;
    BOOLEAN AfterRet = FALSE;
    BOOLEAN PsizeDeclared = FALSE;
    BOOLEAN OutputIndexRestricted = FALSE;
    BYTE InputUsage[16];
    D9VsDeclared Inputs3[64];
    UINT InputCount3 = 0;
    D9VsDeclared Outputs3[64];
    UINT OutputCount3 = 0;
    DWORD CubeSamplers = 0;
    UINT Slots = 0;
    UINT SlotLimit = 0;
    UINT DeviceSlotLimit = MAXUINT;
    UINT StaticFlowCount = 0;
    UINT CallLimit = 0;
    UINT LoopLimit = 0;
    UINT StaticIfLimit = 0;
    UINT DynamicIfLimit = 0;
    UINT CallHwLimit = 0;
    UINT LoopHwLimit = 0;
    UINT StaticIfHwLimit = 0;
    UINT DynamicIfHwLimit = 0;
    D9SvLabelSet Called = {};
    D9SvLabelSet Defined = {};
    D9SvFlow* Flow = nullptr;
    D9SvRegisters Temps;
    D9SvRegisters InputRegs;
    D9SvRegisters Consts;
    D9SvRegisters Address;
    D9SvRegisters TexOut;
    D9SvRegisters AttrOut;
    D9SvRegisters RastOut;
    D9SvRegisters LoopCounter;
    D9SvRegisters IntConsts;
    D9SvRegisters BoolConsts;
    D9SvRegisters Predicates;
    D9SvRegisters Labels;
    D9SvRegisters Outputs;
    D9SvRegisters Samplers;

    D9VsChecker()
    {
        memset(InputUsage, 0xFF, sizeof(InputUsage));
    }

    ~D9VsChecker() override
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

    D9SvRegisters*
    SourceFile(
        _In_ UINT Type);

    BOOLEAN
    Recognized();

    BOOLEAN
    Supported();

    BOOLEAN
    ParamCount();

    VOID
    SourceParams1();

    VOID
    SourceParams23();

    VOID
    SourcePredicate();

    VOID
    SourceInitialized();

    VOID
    AddressWrite1();

    BOOLEAN
    DestParam1();

    BOOLEAN
    DestParam2();

    BOOLEAN
    DestParam3();

    VOID
    MatrixOverlap(
        _Inout_ BOOLEAN* Bad,
        _In_ DWORD FirstId,
        _In_ DWORD RowsId);

    VOID
    Definitions1();

    VOID
    Definitions23();

    VOID
    Declaration12();

    VOID
    Declaration3();

    VOID
    Arithmetic();

    VOID
    Ports1();

    VOID
    Ports23();

    BOOLEAN
    FlowControl();

    VOID
    FlowControlEnd();

    VOID
    CountSlots();

    VOID
    CountSlotsEnd();

    VOID
    TexCoordGaps();

    VOID
    PositionWritten();

    VOID
    LogOutputs12();

    VOID
    LogOutputs3();

    BOOLEAN
    ReportVersionLimit(
        _In_ DWORD Id,
        _In_z_ PCSTR Format,
        _In_ UINT Limit);

    BOOLEAN
    ScalarDest() const;
};

D9SvChecker*
D9SvCreateVertexChecker(
    _In_ DWORD Version)
{
    D9VsChecker* Checker = new (std::nothrow) D9VsChecker;

    if (Checker == nullptr)
        return nullptr;
    if (Version < D9VsVersion20)
    {
        Checker->Model = 1;
    }
    else
    {
        Checker->Model = Version < D9VsVersion30 ? 2 : 3;
        Checker->Ids = Checker->Model == 2 ? &D9Vs2Ids : &D9Vs3Ids;
        Checker->Flow = new (std::nothrow) D9SvFlow;
        if (Checker->Flow == nullptr)
        {
            delete Checker;
            return nullptr;
        }
        Checker->Flow->Reset();
    }
    return Checker;
}

/* Register file sizes per version, and the device limits that apply */
HRESULT
D9VsChecker::Begin(
    _In_ D9SvParams* Settings)
{
    char Name[32];
    BOOLEAN Made = TRUE;
    WORD Kind;

    Settings->HardwareCapable = TRUE;
    Settings->SoftwareCapable = TRUE;
    Settings->Outputs = 0;
    Bind(Settings);
    ConstLimit = 0x2000;
    if (Caps != nullptr)
    {
        DWORD Wanted = Version & 0xFFFF;

        CapsConsts = Caps->MaxVertexShaderConst;
        NoDevice = FALSE;
        if (Model == 2 && Version == D9VsVersion2x)
            Wanted = 0x200;
        if ((Caps->VertexShaderVersion & 0xFFFF) < Wanted && (Model == 1 || (Version & 0xFF) != 0xFF))
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                D9SvVersionName(Version, Name);
                Report(6, Model == 1 ? 263 : Model == 2 ? 313 : 369,
                       "Version Token: Shader version %s is not supported by device. ", Name);
                return E_FAIL;
            }
        }
    }
    Kind = HIWORD(Version);
    if (Kind != 0xFFFE)
    {
        if (Kind == 0xFFFF)
            Report(6, Model == 1 ? 311 : Model == 2 ? 353 : 645,
                   "Version token: 0x%x indicates a pixel shader.  Vertex shader version token must be of the form "
                   "0xfffe****.", Version);
        else
            Report(6, Model == 1 ? 312 : Model == 2 ? 354 : 646,
                   "Version Token: 0x%x is invalid. Vertex shader version token must be of the form 0xfffe****. "
                   "Aborting vertex shader validation.", Version);
        return E_FAIL;
    }

    if (Model == 1)
    {
        if (Version == 0xFFFE0100)
        {
            Report(6, 86, "vs_1_0 is no longer supported.  It turned out that vertex shader capable hardware will "
                   "always support at least vs_1_1 (which is not as limited a model as vs_1_0 was). To convert a "
                   "vs_1_0 shader up to vs_1_1, the only change needed is to make the version number 1_1. ");
            return E_FAIL;
        }
        if (Version != D9VsVersion11)
        {
            D9SvVersionName(Version, Name);
            Report(6, 310, "Version Token: Shader version %s is not supported. Aborting vertex shader validation.",
                   Name);
            return E_FAIL;
        }
        Made = Temps.Create(12, TRUE, 3) && InputRegs.Create(16, FALSE, 1) &&
               Consts.Create(NoDevice ? 0 : D9SvMax(CapsConsts, ConstLimit), FALSE, 1) && Address.Create(1, TRUE, 0) &&
               TexOut.Create(8, TRUE, 0) && AttrOut.Create(2, TRUE, 0) && RastOut.Create(3, TRUE, 0);
        SlotLimit = 128;
    }
    else if (Model == 2)
    {
        UINT ConstCount = NoDevice ? 0 : D9SvMax(CapsConsts, ConstLimit);

        switch (Version)
        {
        case D9VsVersion20:
            Made = Temps.Create(12, TRUE, 3) && InputRegs.Create(16, FALSE, 1) && Consts.Create(ConstCount, FALSE, 1) &&
                   Address.Create(1, TRUE, 0) && TexOut.Create(8, TRUE, 0) && AttrOut.Create(2, TRUE, 0) &&
                   RastOut.Create(3, TRUE, 0) && LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(16, FALSE, 1) &&
                   BoolConsts.Create(16, FALSE, 1) && Predicates.Create(0, TRUE, 1) && Labels.Create(16, FALSE, 1);
            CallLimit = 1;
            LoopLimit = 1;
            StaticIfLimit = 24;
            DynamicIfLimit = 0;
            CallHwLimit = 1;
            LoopHwLimit = 1;
            StaticIfHwLimit = 24;
            DynamicIfHwLimit = 0;
            break;
        case D9VsVersion2x:
        {
            UINT Static = 4;
            UINT Dynamic = 24;

            Made = Temps.Create(32, TRUE, 3) && InputRegs.Create(16, FALSE, 1) && Consts.Create(ConstCount, FALSE, 1) &&
                   Address.Create(1, TRUE, 0) && TexOut.Create(8, TRUE, 0) && AttrOut.Create(2, TRUE, 0) &&
                   RastOut.Create(3, TRUE, 0) && LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(16, FALSE, 1) &&
                   BoolConsts.Create(16, FALSE, 1) && Predicates.Create(1, TRUE, 1) && Labels.Create(16, FALSE, 1);
            if (Caps != nullptr)
            {
                INT CapsStatic = Caps->VS20Caps.StaticFlowControlDepth;
                INT CapsDynamic = Caps->VS20Caps.DynamicFlowControlDepth;

                Static = static_cast<UINT>(D9SvMax(1, D9SvMin(4, CapsStatic)));
                Dynamic = static_cast<UINT>(D9SvMax(0, D9SvMin(24, CapsDynamic)));
            }
            CallLimit = 4;
            LoopLimit = 4;
            StaticIfLimit = 24;
            DynamicIfLimit = 24;
            CallHwLimit = Static;
            LoopHwLimit = Static;
            StaticIfHwLimit = 24;
            DynamicIfHwLimit = Dynamic;
            break;
        }
        case D9VsVersion2sw:
            if (DeviceType == D3DDEVTYPE_HAL)
            {
                Params->HardwareCapable = FALSE;
                if (HardwareRequired())
                {
                    Report(6, 0x283, "Version Token: Vertex shader version vs_2_sw is for software use only. Aborting "
                           "shader validation.");
                    return E_FAIL;
                }
            }
            Made = Temps.Create(32, TRUE, 3) && InputRegs.Create(16, FALSE, 3) && Consts.Create(0x2000, FALSE, 3) &&
                   Address.Create(1, TRUE, 0) && TexOut.Create(8, TRUE, 0) && AttrOut.Create(2, TRUE, 0) &&
                   RastOut.Create(3, TRUE, 0) && LoopCounter.Create(1, FALSE, 1) &&
                   IntConsts.Create(0x800, FALSE, 1) && BoolConsts.Create(0x800, FALSE, 1) &&
                   Predicates.Create(1, TRUE, 1) && Labels.Create(0x800, FALSE, 1);
            CallLimit = 4;
            LoopLimit = 4;
            StaticIfLimit = 24;
            DynamicIfLimit = 24;
            CallHwLimit = 4;
            LoopHwLimit = 4;
            StaticIfHwLimit = 24;
            DynamicIfHwLimit = 24;
            break;
        default:
            D9SvVersionName(Version, Name);
            Report(6, 355, "Version Token: Shader version %s is not supported. Aborting vertex shader validation.",
                   Name);
            return E_FAIL;
        }
        SlotLimit = Version == D9VsVersion2sw ? MAXUINT : 256;
    }
    else
    {
        if (Version == D9VsVersion3sw)
        {
            if (DeviceType == D3DDEVTYPE_HAL)
            {
                Params->HardwareCapable = FALSE;
                if (HardwareRequired())
                {
                    Report(6, 0x284, "Version Token: Vertex shader version vs_3_sw is for software use only. Aborting "
                           "shader validation.");
                    return E_FAIL;
                }
            }
            Made = Temps.Create(32, TRUE, 3) && InputRegs.Create(16, FALSE, 3) && Consts.Create(0x2000, FALSE, 3) &&
                   Address.Create(1, TRUE, 0) && LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(0x800, FALSE, 1) &&
                   BoolConsts.Create(0x800, FALSE, 1) && Predicates.Create(1, TRUE, 1) && Outputs.Create(16, TRUE, 0) &&
                   Labels.Create(0x800, FALSE, 1) && Samplers.Create(4, FALSE, 1);
            SlotLimit = MAXUINT;
            DeviceSlotLimit = MAXUINT;
        }
        else if (Version == D9VsVersion30)
        {
            UINT ConstCount = NoDevice ? 0x2000 : D9SvMax(CapsConsts, ConstLimit);

            Made = Temps.Create(32, TRUE, 3) && InputRegs.Create(16, FALSE, 1) && Consts.Create(ConstCount, FALSE, 1) &&
                   Address.Create(1, TRUE, 0) && LoopCounter.Create(1, FALSE, 1) && IntConsts.Create(16, FALSE, 1) &&
                   BoolConsts.Create(16, FALSE, 1) && Predicates.Create(1, TRUE, 1) && Outputs.Create(12, TRUE, 0) &&
                   Labels.Create(0x800, FALSE, 1) && Samplers.Create(4, FALSE, 1);
            SlotLimit = 0x8000;
            DeviceSlotLimit = Caps != nullptr ? Caps->MaxVertexShader30InstructionSlots : MAXUINT;
        }
        else
        {
            D9SvVersionName(Version, Name);
            Report(6, 381, "Version Token: Shader version %s is not supported. Aborting vertex shader validation. ",
                   Name);
            return E_FAIL;
        }
        CallLimit = 4;
        LoopLimit = 4;
        StaticIfLimit = 24;
        DynamicIfLimit = 24;
    }
    if (!Made)
    {
        Report(6, 0, "Out of memory.");
        return E_FAIL;
    }
    Ready = TRUE;
    return S_OK;
}

/* Splits one instruction into its parameters; malformed encodings stop validation */
BOOLEAN
D9VsChecker::Decode(
    _In_ const DWORD* Tokens,
    _Inout_ UINT* Used)
{
    D9SvInstruction* Ins = Current;
    UINT Limit = Stream ? MAXUINT : *Used;
    UINT Position = 1;
    DWORD Reserved;
    DWORD Token = Tokens[0];

    *Used = 0;
    Ins->Opcode = Token & 0xFFFF;
    if (Ins->Opcode == D9SvOpComment)
    {
        UINT Length = ((Token >> 16) & 0x7FFF) + 1;

        if (!Stream && Length != Limit)
        {
            Report(2, 309, "Unexpected DWORD count for instruction.  Aborting validation.");
            return FALSE;
        }
        *Used = Length;
        return TRUE;
    }

    Statements += 1;
    Ins->Statement = Statements;
    Ins->Offset = Stream ? static_cast<SIZE_T>(Tokens - Start) : MAXSIZE_T;
    if (Version < D9VsVersion20)
        Reserved = 0xFFFF0000;
    else if (Version < D9VsVersion2x)
        Reserved = 0xF0FF0000;
    else if (Ins->Opcode == D9SvOpIfc || Ins->Opcode == D9SvOpBreakc || Ins->Opcode == D9SvOpSetp)
        Reserved = 0xE0000000;
    else
        Reserved = 0xE0FF0000;
    if ((Token & Reserved) != 0)
    {
        Report(2, 305, "Reserved bit(s) set in instruction parameter token!  Aborting validation.");
        return FALSE;
    }
    Ins->Controls = Token & 0xFF0000;
    if (Version >= D9VsVersion20)
        Ins->Length = (Token >> 24) & 0xF;
    if (Version >= D9VsVersion2x && (Token & 0x10000000) != 0)
        Ins->Predicated = TRUE;

    if (Ins->Opcode == D9SvOpDcl)
    {
        if (Version >= D9VsVersion20 && (Token & 0xF000000) != 0x2000000)
        {
            Report(2, 306, "dcl instruction should have exactly 2 parameter tokens.  Aborting validation.");
            return FALSE;
        }
        if (Limit == 0)
            goto Done;
        *Used = 1;
        if (Limit == 1)
            goto Done;
        if ((Tokens[1] & 0x80000000) == 0)
        {
            Report(2, 540, "Bit 31 not set in dcl info token!  Aborting validation.");
            *Used = 0;
            return FALSE;
        }
        Ins->DclInfo = Tokens[1] & 0x7FFFFFFF;
        Position = 2;
    }

    if (Position > Limit)
        goto Done;
    *Used = Position;
    if (Position == Limit)
        goto Done;

    /* Flow control opcodes from version 2 on carry only sources */
    if (Version >= D9VsVersion20)
    {
        switch (Ins->Opcode)
        {
        case 25: case 26: case 27: case 28: case 29: case 30: case 38: case 39: case 40: case 41: case 42: case 43:
        case 44: case 45: case 96: case D9SvOpEnd:
            goto Sources;
        default:
            break;
        }
    }

    if ((Tokens[Position] & 0x80000000) != 0)
    {
        DWORD Dest = Tokens[Position];
        DWORD DestReserved = Version < D9VsVersion2x ? 0x0FF0E000 : 0x0F00C000;

        Ins->DestCount += 1;
        Ins->Dest.Present = TRUE;
        Ins->Dest.Number = D9SvRegisterNumber(Dest);
        Ins->Dest.Mask = Dest & 0xF0000;
        Ins->Dest.Modifier = Dest & 0xF00000;
        Ins->Dest.Shift = (Dest >> 24) & 0xF;
        Ins->Dest.Type = D9SvRegisterType(Dest);
        Ins->Dest.Relative = Dest & D9SvRelativeBit;
        if ((Dest & DestReserved) != 0)
        {
            Report(2, 307, "Reserved bit(s) set in destination parameter token!  Aborting validation.");
            *Used = 0;
            return FALSE;
        }
        if (Position + 1 > Limit)
            goto Done;
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;

        /* Definitions carry their values instead of sources */
        if (Ins->Opcode == D9SvOpDefb || Ins->Opcode == D9SvOpDefi || Ins->Opcode == D9SvOpDef)
        {
            UINT Total = Position + (Ins->Opcode == D9SvOpDefb ? 2 : 5);

            if (!Stream && Total != Limit)
            {
                Report(2, 309, "Unexpected DWORD count for instruction.  Aborting validation.");
                return FALSE;
            }
            if (Version >= D9VsVersion20)
            {
                if (Ins->Opcode == D9SvOpDefb && Ins->Dest.Type == D9SvRegConstBool)
                    Ins->Values[0] = Tokens[Position + 1];
                else if (Ins->Opcode == D9SvOpDefi && Ins->Dest.Type == D9SvRegConstInt)
                    CopyMemory(Ins->Values, &Tokens[Position + 1], sizeof(Ins->Values));
                else if (Ins->Opcode == D9SvOpDef && Ins->Dest.Type == D9SvRegConst)
                    CopyMemory(Ins->Values, &Tokens[Position + 1], sizeof(Ins->Values));
            }
            *Used = Total;
            if (!Stream && Total == Limit)
                goto Done;
            return TRUE;
        }
        Position += 1;

        /* From vs_2_x a relative destination names its address register in the next token */
        if (Version >= D9VsVersion2x && Ins->Dest.Relative == D9SvRelativeBit)
        {
            DWORD RelToken = Tokens[Position];
            D9SvSource Scratch;

            if ((RelToken & 0x80000000) == 0)
            {
                Report(2, 802, "Expected relative address token.  Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            if ((RelToken & 0x0F00E000) != 0)
            {
                Report(2, 805, "Reserved bit(s) set in relative address token for source parameter %d!  Aborting "
                       "validation.", Ins->SourceCount);
                *Used = 0;
                return FALSE;
            }
            Scratch.Clear();
            if (!Scratch.LoadRelative(RelToken))
            {
                Ins->Dest.RelativeNumber = Scratch.RelativeNumber;
                Ins->Dest.RelativeType = Scratch.RelativeType;
                if (Scratch.RelativeType == D9SvRegLoop)
                    Report(2, 709, "When aL is used as a relative address, it must not specify any swizzle, as it is "
                           "a scalar register. Aborting validation.");
                else
                    Report(2, 475, "Relative address must specify a single component selector (.x, .y, .z or .w) "
                           "to indicate relative address component. Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            Ins->Dest.RelativeNumber = Scratch.RelativeNumber;
            Ins->Dest.RelativeType = Scratch.RelativeType;
            Ins->Dest.RelativeComponent = Scratch.RelativeComponent;
            if (Position + 1 > Limit)
                goto Done;
            *Used = Position + 1;
            if (Position + 1 == Limit)
                goto Done;
            Position += 1;
        }
    }

Sources:
    if (Ins->Predicated)
    {
        Ins->Predicate.Load(Tokens[Position]);
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
        D9SvSource* Source;

        Ins->SourceCount += 1;
        if (Ins->DestCount + Ins->SourceCount > 5 || Ins->SourceCount > 4)
        {
            Ins->SourceCount -= 1;
            Report(2, 309, "Unexpected DWORD count for instruction.  Aborting validation.");
            return FALSE;
        }
        Source = &Ins->Source[Ins->SourceCount - 1];
        Source->Load(Param);
        if (Version < D9VsVersion20 && Source->Relative != 0)
        {
            Source->RelativeComponent = D9SvComponentX;
            Source->RelativeType = D9SvRegAddress;
            Source->RelativeNumber = 0;
        }
        if (Version >= D9VsVersion20 && (Param & 0xC000) != 0)
        {
            Report(2, 308, "Reserved bit(s) set in parameter %d source token!  Aborting validation.",
                   Ins->SourceCount);
            *Used = 0;
            return FALSE;
        }
        if (Version >= D9VsVersion20 && Source->Relative == D9SvRelativeBit)
        {
            DWORD RelToken;

            *Used = Position + 1;
            if (Position + 1 == Limit)
                goto Done;
            RelToken = Tokens[Position + 1];
            if ((RelToken & 0x80000000) == 0)
            {
                Report(2, 803, "Expected relative address token.  Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            if ((RelToken & 0x0F00E000) != 0)
            {
                Report(2, 806, "Reserved bit(s) set in relative address token for source parameter %d!  Aborting "
                       "validation.", Ins->SourceCount);
                *Used = 0;
                return FALSE;
            }
            if (!Source->LoadRelative(RelToken))
            {
                if (Source->RelativeType == D9SvRegLoop)
                    Report(2, 710, "When aL is used as a relative address, it must not specify any swizzle, as it is "
                           "a scalar register. Aborting validation.");
                else
                    Report(2, 804, "Relative address must specify a single component selector (.x, .y, .z or .w) "
                           "to indicate relative address component. Aborting validation.");
                *Used = 0;
                return FALSE;
            }
            Position += 1;
        }
        *Used = Position + 1;
        if (Position + 1 == Limit)
            goto Done;
        Position += 1;
    }

Done:
    Ins->ComputeReads(Version);
    if (Version >= D9VsVersion20 && Ins->Opcode != D9SvOpComment && Ins->Length + 1 != *Used)
    {
        Report(2, 0x21F, "Instruction length specified for instruction (%d) does not match the token count "
               "encountered (%d). Aborting validation.", Ins->Length, *Used - 1);
        *Used = 0;
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
D9VsChecker::Recognized()
{
    UINT Op = Current->Opcode;

    if ((Op <= 48 && Op != 3) || Op == 78 || Op == 79 || Op == 81 || Op == 94 || Op == 95 || Op == 96)
        return TRUE;
    Fail(2, 0x128, "Unrecognized instruction. Aborting vertex shader validation.");
    return FALSE;
}

BOOLEAN
D9VsChecker::Supported()
{
    UINT Op = Current->Opcode;
    char Name[32];

    if (Model == 1)
    {
        if ((Op >= 1 && Op <= 24 && Op != 3 && Op != 18) || Op == 31 || Op == 78 || Op == 79 || Op == 81 ||
            Op == 0 || Op == D9SvOpEnd)
            return TRUE;
        D9SvVersionName(Version, Name);
        Fail(2, 0x13A, "Instruction not supported by version %s shader.", Name);
        return FALSE;
    }
    if (Model == 3)
    {
        if ((Op <= 48 && Op != 3) || Op == 78 || Op == 79 || Op == 81 || Op == 94 || Op == 95 || Op == 96 ||
            Op == D9SvOpEnd)
            return TRUE;
        Fail(2, 0x295, "Instruction not supported by this vertex shader version.");
        return FALSE;
    }
    if ((Op <= 40 && Op != 3) || Op == 42 || Op == 43 || Op == 46 || Op == 47 || Op == 48 || Op == 78 || Op == 79 ||
        Op == 81)
        return TRUE;
    if (Version >= D9VsVersion2x)
    {
        if (Op == D9SvOpIfc || Op == D9SvOpBreak || Op == D9SvOpBreakc)
        {
            if (Version == D9VsVersion2sw || Caps == nullptr || Caps->VS20Caps.DynamicFlowControlDepth != 0)
                return TRUE;
            Params->HardwareCapable = FALSE;
            if (!HardwareRequired())
                return TRUE;
            Fail(2, 653, "vs_2_x extended instruction not supported by the hardware (dynamic flow control not "
                 "supported).");
            return FALSE;
        }
        if (Op == D9SvOpSetp || Op == D9SvOpBreakp)
        {
            if (Version == D9VsVersion2sw || Caps == nullptr || (Caps->VS20Caps.Caps & D3DVS20CAPS_PREDICATION) != 0)
                return TRUE;
            Params->HardwareCapable = FALSE;
            if (!HardwareRequired())
                return TRUE;
            Fail(2, 654, "vs_2_x extended instruction not supported by the hardware (predication not supported).");
            return FALSE;
        }
    }
    if (Op == D9SvOpEnd || Op == 0)
        return TRUE;
    D9SvVersionName(Version, Name);
    Fail(2, 0x28F, "Instruction not supported by version %s shader.", Name);
    return FALSE;
}

/* Destination and source counts each opcode takes */
BOOLEAN
D9VsChecker::ParamCount()
{
    D9SvInstruction* Ins = Current;
    UINT Dests = MAXUINT;
    UINT Sources = MAXUINT;
    BOOLEAN Bad;

    switch (Ins->Opcode)
    {
    case 0:
        Dests = 0;
        Sources = 0;
        break;
    case 28: case 29: case 39: case 44:
        if (Model >= 2)
        {
            Dests = 0;
            Sources = 0;
        }
        break;
    case 42: case 43:
        if (Model == 3)
        {
            Dests = 0;
            Sources = 0;
        }
        break;
    case 1: case 6: case 7: case 14: case 15: case 16: case 19: case 78: case 79:
        Dests = 1;
        Sources = 1;
        break;
    case 35: case 36:
        if (Model >= 2)
        {
            Dests = 1;
            Sources = 1;
        }
        break;
    case 46:
        if (Model == 2)
        {
            Dests = 1;
            Sources = 1;
        }
        break;
    case 2: case 5: case 8: case 9: case 10: case 11: case 12: case 13: case 17: case 20: case 21: case 22: case 23:
    case 24:
        Dests = 1;
        Sources = 2;
        break;
    case 32: case 33:
        if (Model >= 2)
        {
            Dests = 1;
            Sources = 2;
        }
        break;
    case 94: case 95:
        if (Model == 3)
        {
            Dests = 1;
            Sources = 2;
        }
        break;
    case 4:
        Dests = 1;
        Sources = 3;
        break;
    case 18: case 34:
        if (Model >= 2)
        {
            Dests = 1;
            Sources = 3;
        }
        break;
    case 37:
        if (Model >= 2)
        {
            Dests = 1;
            Sources = Model == 2 ? 3 : 1;
        }
        break;
    case 25: case 30: case 38: case 40: case 96:
        if (Model >= 2)
        {
            Dests = 0;
            Sources = 1;
        }
        break;
    case 26: case 27: case 41: case 45:
        if (Model >= 2)
        {
            Dests = 0;
            Sources = 2;
        }
        break;
    case 31: case 81:
        Dests = 1;
        Sources = 0;
        break;
    case 47: case 48:
        if (Model >= 2)
        {
            Dests = 1;
            Sources = 0;
        }
        break;
    default:
        break;
    }
    if (Dests == MAXUINT)
        Bad = Ins->DestCount + Ins->SourceCount > 5;
    else
        Bad = Ins->DestCount != Dests || Ins->SourceCount != Sources;
    if (!Bad)
        return TRUE;
    Fail(2, Model == 1 ? 0x13B : Model == 2 ? 0x29D : 0x2A2, "Invalid parameter count.");
    return FALSE;
}

D9SvRegisters*
D9VsChecker::SourceFile(
    _In_ UINT Type)
{
    switch (Type)
    {
    case D9SvRegTemp:
        return &Temps;
    case D9SvRegInput:
        return &InputRegs;
    case D9SvRegConst:
        return &Consts;
    case D9SvRegAddress:
        return &Address;
    case D9SvRegLoop:
        return &LoopCounter;
    case D9SvRegConstInt:
        return &IntConsts;
    case D9SvRegConstBool:
        return &BoolConsts;
    case D9SvRegPredicate:
        return &Predicates;
    case D9SvRegLabel:
        return &Labels;
    case D9SvRegSampler:
        return &Samplers;
    default:
        return nullptr;
    }
}

VOID
D9VsChecker::SourceParams1()
{
    D9SvInstruction* Ins = Current;

    for (UINT Index = 0; Index < Ins->SourceCount; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];
        PCSTR Ordinal = D9VsOrdinals[Index];
        BOOLEAN Bad = FALSE;
        BOOLEAN Skip = FALSE;
        UINT Hardware = 0;
        UINT Software = 0;

        switch (Source->Type)
        {
        case D9SvRegTemp:
            Hardware = Software = Temps.Count;
            break;
        case D9SvRegInput:
            Hardware = Software = InputRegs.Count;
            break;
        case D9SvRegConst:
            if (NoDevice)
            {
                Skip = TRUE;
                break;
            }
            Hardware = CapsConsts;
            Software = ConstLimit;
            break;
        default:
            Fail(2, 0x13C, "Invalid reg type for %s source param.", Ordinal);
            Bad = TRUE;
            Skip = TRUE;
            break;
        }

        if (!Skip)
        {
            UINT Rows = Index == 1 ? D9SvMatrixRows(Ins->Opcode) : 1;
            UINT Last = Source->Number + Rows - 1;
            BOOLEAN SoftwareFailed = FALSE;

            if (Source->Number >= Software && Source->Relative == 0)
            {
                if (SoftwareRequired())
                {
                    Fail(2, 0x13D, "Invalid reg num: %d for %s source param. Max allowed for this type is %d.",
                         Source->Number, Ordinal, Software - 1);
                    Bad = TRUE;
                }
                SoftwareFailed = TRUE;
                Params->SoftwareCapable = FALSE;
            }
            else if (Rows > 1 && Last >= Software && !(Source->Type == D9SvRegConst && Source->Relative == D9SvRelativeBit))
            {
                if (Source->Type != D9SvRegConst || SoftwareRequired())
                {
                    Fail(2, 0x13E, "Reg num: %d for %s source param on matrix instruction causes attempt to access out "
                         "of range register number %d. Max allowed for this type is %d.", Source->Number, Ordinal,
                         Last, Software - 1);
                    Bad = TRUE;
                }
                SoftwareFailed = TRUE;
                Params->SoftwareCapable = FALSE;
            }

            if (Source->Number >= Hardware && Source->Relative == 0)
            {
                if (HardwareRequired() || SoftwareFailed)
                {
                    Fail(2, 0x13F, "Invalid reg num: %d for %s source param. Max allowed for this type is %d.",
                         Source->Number, Ordinal, Hardware - 1);
                    Bad = TRUE;
                }
                Params->HardwareCapable = FALSE;
            }
            else if (Rows > 1 && Last >= Hardware)
            {
                if (Source->Type != D9SvRegConst)
                {
                    Fail(2, 0x140, "Reg num: %d for %s source param on matrix instruction causes attempt to access out "
                         "of range register number %d. Max allowed for this type is %d.", Source->Number, Ordinal,
                         Last, Hardware - 1);
                    Bad = TRUE;
                    Params->HardwareCapable = FALSE;
                }
                else if (Source->Relative != D9SvRelativeBit)
                {
                    if (HardwareRequired() || SoftwareFailed)
                    {
                        Fail(2, 0x140, "Reg num: %d for %s source param on matrix instruction causes attempt to access "
                             "out of range register number %d. Max allowed for this type is %d.", Source->Number,
                             Ordinal, Last, Hardware - 1);
                        Bad = TRUE;
                    }
                    Params->HardwareCapable = FALSE;
                }
            }
        }

        if (Source->Modifier != 0)
        {
            if (Source->Modifier != D9SvModNegate)
            {
                Fail(2, 0x142, "Invalid src mod for %s source param.", Ordinal);
                Bad = TRUE;
            }
            else if (Index == 1 && D9SvIsMatrix(Ins->Opcode))
            {
                Fail(2, 0x141, "Cannot negate second source parameter to vector*matrix instructions.");
                Bad = TRUE;
            }
        }
        switch (Ins->Opcode)
        {
        case 6: case 7: case 14: case 15: case 78: case 79:
            if (!D9SvReplicate(Source->Swizzle))
            {
                Fail(2, 0x1E5, "%s requires replicate swizzle for the source parameter in order to select component. "
                     "i.e. .x | .y | .z | .w (or rgba equivalent)", D9SvOpcodeName(Ins->Opcode));
                Bad = TRUE;
            }
            break;
        default:
            break;
        }
        if (Source->Relative != 0 && Source->Type != D9SvRegConst)
        {
            Fail(2, 0x143, "Address mode must be absolute (%s source param).", Ordinal);
            Bad = TRUE;
        }
        if (Source->Relative == D9SvRelativeBit && Source->Type == D9SvRegConst &&
            Source->RelativeComponent != D9SvComponentX)
        {
            Fail(2, 0x144, "Relative addressing of constant register must reference a0.x only.");
            Bad = TRUE;
        }
        if (Source->Swizzle != D9SvSwizzleIdentity && Index == 1 && D9SvIsMatrix(Ins->Opcode))
        {
            Fail(2, 0x16F, "Cannot swizzle second source parameter to vector*matrix instructions.");
            Bad = TRUE;
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

/* vs_2_* and vs_3_* source rules: register kinds per opcode, ranges against the device, modifiers and swizzles */
VOID
D9VsChecker::SourceParams23()
{
    D9SvInstruction* Ins = Current;
    UINT Op = Ins->Opcode;

    for (UINT Index = 0; Index < Ins->SourceCount; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];
        PCSTR Ordinal = D9VsOrdinals[Index];
        BOOLEAN Bad = FALSE;
        BOOLEAN NoConst = FALSE;
        BOOLEAN ByType = FALSE;
        UINT Hardware = 0;
        UINT Software = 0;
        D9SvRegisters* File = nullptr;

        if (Op == D9SvOpCall || Op == D9SvOpCallnz)
        {
            if (Index == 0)
            {
                if (Source->Type != D9SvRegLabel)
                {
                    Fail(2, Ids->CallLabel, "%s requires %s parameter to be label (l#). ", D9SvOpcodeName(Op), Ordinal);
                    Bad = TRUE;
                }
                else
                {
                    File = &Labels;
                }
            }
            else if (Index == 1)
            {
                if (Model == 2 && Source->Type == D9SvRegPredicate && Version == D9VsVersion2x && Caps != nullptr &&
                    (Caps->VS20Caps.Caps & D3DVS20CAPS_PREDICATION) == 0)
                {
                    Params->HardwareCapable = FALSE;
                    if (HardwareRequired())
                    {
                        Fail(2, 658, "vs_2_x predication not supported by the hardware.");
                        continue;
                    }
                }
                if (Model == 2 && Version == D9VsVersion20 && Op == D9SvOpCallnz && Source->Type != D9SvRegConstBool)
                {
                    Fail(2, 918, "%s requires second source parameter (the condition) to be boolean constant register "
                         "(b#). ", D9SvOpcodeName(Op));
                    Bad = TRUE;
                }
                else if (Op == D9SvOpCallnz && Source->Type != D9SvRegConstBool && Source->Type != D9SvRegPredicate)
                {
                    Fail(2, Ids->CallnzCondition, "%s requires second source parameter (the condition) to be boolean "
                         "constant register (b#), or predicate register (p0). ", D9SvOpcodeName(Op));
                    Bad = TRUE;
                }
                else
                {
                    File = Source->Type == D9SvRegConstBool ? &BoolConsts : &Predicates;
                }
            }
        }
        else if (Op == D9SvOpLoop && Index == 0)
        {
            if (Source->Type == D9SvRegLoop)
            {
                File = &LoopCounter;
            }
            else
            {
                Fail(2, Ids->LoopCounter, "loop requires first parameter to be loop counter (aL). ");
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpLoop || Op == D9SvOpRep)
        {
            if (Source->Type == D9SvRegConstInt)
            {
                File = &IntConsts;
            }
            else
            {
                Fail(2, Ids->LoopInteger, "%s requires %s parameter to be integer constant register (i#). ",
                     Op == D9SvOpLoop ? "loop" : "rep", Ordinal);
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpLabel)
        {
            if (Source->Type == D9SvRegLabel)
            {
                File = &Labels;
            }
            else
            {
                Fail(2, Ids->LabelParam, "%s instruction requires parameter to be label (l#). ", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpIf)
        {
            if (Model == 2 && Source->Type == D9SvRegPredicate && Version == D9VsVersion2x && Caps != nullptr &&
                (Caps->VS20Caps.Caps & D3DVS20CAPS_PREDICATION) == 0)
            {
                Params->HardwareCapable = FALSE;
                if (HardwareRequired())
                {
                    Fail(2, 659, "vs_2_x predication not supported by the hardware.");
                    continue;
                }
            }
            if (Source->Type == D9SvRegConstBool)
            {
                File = &BoolConsts;
            }
            else if (Source->Type == D9SvRegPredicate && !(Model == 2 && Version == D9VsVersion20))
            {
                File = &Predicates;
            }
            else if (Model == 2 && Version == D9VsVersion20)
            {
                Fail(2, 921, "%s requires parameter (the branch condition) to be boolean constant register (b#). ",
                     D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            else
            {
                Fail(2, Model == 2 ? 920 : 923, "%s requires parameter (the branch condition) to be boolean constant "
                     "register (b#), or predicate register (p0). ", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpBreakp)
        {
            if (Source->Type == D9SvRegPredicate)
            {
                File = &Predicates;
            }
            else
            {
                Fail(2, Ids->BreakpParam, "breakp requires parameter to be predicate register (p0). ");
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpTexldl && Model == 3)
        {
            if (Index == 0)
            {
                if (Source->Type == D9SvRegTemp || Source->Type == D9SvRegInput)
                {
                    File = Source->Type == D9SvRegTemp ? &Temps : &InputRegs;
                }
                else if (Source->Type == D9SvRegConst)
                {
                    if (NoDevice)
                    {
                        NoConst = TRUE;
                    }
                    else
                    {
                        Hardware = CapsConsts;
                        Software = ConstLimit;
                    }
                }
                else
                {
                    Fail(2, 527, "First source parameter for texldl must be temp (r#), input (v#) or const (c#) "
                         "register.");
                    SkipSource[Index] = TRUE;
                    continue;
                }
                if (File != nullptr)
                    Hardware = Software = File->Count;
                File = nullptr;
                if (Source->Modifier != 0)
                {
                    Fail(2, 1064, "(%s source param) Source modifier not allowed on texcoord for texldl.", Ordinal);
                    Bad = TRUE;
                }
            }
            else if (Index == 1)
            {
                if (Source->Type != D9SvRegSampler)
                {
                    Fail(2, 529, "Second source parameter for texldl must be sampler stage (s#).");
                    SkipSource[Index] = TRUE;
                    continue;
                }
                Hardware = Software = Samplers.Count;
                if (Source->Modifier != 0)
                {
                    Fail(2, 564, "(%s source param) Source modifier not allowed on sampler (s#).", Ordinal);
                    Bad = TRUE;
                }
            }
        }
        else
        {
            ByType = TRUE;
        }

        if (ByType)
        {
            switch (Source->Type)
            {
            case D9SvRegTemp:
                Software = Temps.Count;
                Hardware = (Model == 2 && Version == D9VsVersion2x && Caps != nullptr) ? Caps->VS20Caps.NumTemps :
                           Temps.Count;
                break;
            case D9SvRegInput:
                Hardware = Software = InputRegs.Count;
                break;
            case D9SvRegConst:
                if (NoDevice)
                {
                    NoConst = TRUE;
                }
                else
                {
                    Hardware = CapsConsts;
                    Software = ConstLimit;
                }
                break;
            default:
                Fail(2, Ids->SourceType, "Invalid reg type for %s source param.", Ordinal);
                Bad = TRUE;
                break;
            }
        }
        else if (File != nullptr)
        {
            Hardware = Software = File->Count;
        }

        /* Relative addressing */
        if (Source->Relative != 0)
        {
            if (Model == 2)
            {
                if (Source->Type != D9SvRegConst)
                {
                    Fail(2, 699, "Relative addressing only permitted on constant registers (%s source param).",
                         Ordinal);
                    Bad = TRUE;
                    goto Modifiers;
                }
                if (Source->RelativeType == D9SvRegLoop)
                {
                    if (Source->RelativeNumber >= LoopCounter.Count)
                    {
                        Fail(2, 965, "Invalid register number, %d, specified for aL register. Max # supported is %d "
                             "(%s source param).", Source->RelativeNumber, LoopCounter.Count - 1, Ordinal);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                }
                else if (Source->RelativeType != D9SvRegAddress)
                {
                    Fail(2, 962, "Relative address register must be a# register or aL register (%s source param).",
                         Ordinal);
                    Bad = TRUE;
                    goto Modifiers;
                }
                else if (Source->RelativeNumber >= Address.Count)
                {
                    Fail(2, 535, "Invalid register number, %d, specified for a# (address) register. Max # supported "
                         "is %d (%s source param).", Source->RelativeNumber, Address.Count - 1, Ordinal);
                    Bad = TRUE;
                    goto Modifiers;
                }
            }
            else
            {
                BOOLEAN CheckLoop = FALSE;

                if (Source->Type == D9SvRegConst)
                {
                    if (Source->RelativeType != D9SvRegLoop && Source->RelativeType != D9SvRegAddress)
                    {
                        Fail(2, 963, "Relative address register for constant (c#) must be a# register or aL register "
                             "(%s source param).", Ordinal);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                    if (Source->RelativeType == D9SvRegLoop)
                    {
                        CheckLoop = TRUE;
                    }
                    else if (Source->RelativeNumber >= Address.Count)
                    {
                        Fail(2, 967, "Invalid register number, %d, specified for a# (address) register. Max # "
                             "supported is %d (%s source param).", Source->RelativeNumber, Address.Count - 1, Ordinal);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                }
                else if (Source->Type == D9SvRegInput)
                {
                    if (Source->RelativeType != D9SvRegLoop)
                    {
                        Fail(2, 964, "Relative address for input (v#) can only be aL register (%s source param).",
                             Ordinal);
                        Bad = TRUE;
                        goto Modifiers;
                    }
                    CheckLoop = TRUE;
                }
                else
                {
                    Fail(2, 700, "Relative addressing of source register only permitted on input (v#) and constant "
                         "(c#) registers (%s source param).", Ordinal);
                    Bad = TRUE;
                    goto Modifiers;
                }
                if (CheckLoop && Source->RelativeNumber >= LoopCounter.Count)
                {
                    Fail(2, 966, "Invalid register number, %d, specified for aL register. Max # supported is %d (%s "
                         "source param).", Source->RelativeNumber, LoopCounter.Count - 1, Ordinal);
                    Bad = TRUE;
                    goto Modifiers;
                }
            }
        }

        /* Register ranges: past the software limit fails software processing, past the device's fails hardware */
        if (!Bad && !NoConst)
        {
            UINT Rows = Index == 1 ? D9SvMatrixRows(Op) : 1;
            UINT Last = Source->Number + Rows - 1;
            BOOLEAN RelativeConst = Source->Type == D9SvRegConst && Source->Relative == D9SvRelativeBit;
            BOOLEAN SoftwareFailed = FALSE;

            if (Source->Number >= Software && Source->Relative == 0)
            {
                if (SoftwareRequired() || Source->Type != D9SvRegConst)
                {
                    Fail(2, Ids->SoftwareNumber, "Invalid reg num: %d for %s source param. Max allowed for this type "
                         "is %d.", Source->Number, Ordinal, Software - 1);
                    Bad = TRUE;
                }
                Params->SoftwareCapable = FALSE;
                SoftwareFailed = TRUE;
            }
            else if (Rows > 1 && Last >= Software && !RelativeConst)
            {
                if (SoftwareRequired())
                {
                    Fail(2, Ids->SoftwareMatrix, "Reg num: %d for %s source param on matrix instruction causes attempt "
                         "to access out of range register number %d. Max allowed for this type is %d.", Source->Number,
                         Ordinal, Last, Software - 1);
                    Bad = TRUE;
                }
                Params->SoftwareCapable = FALSE;
                SoftwareFailed = TRUE;
            }
            if (Source->Number >= Hardware && Source->Relative == 0)
            {
                if (HardwareRequired() || SoftwareFailed)
                {
                    Fail(2, Ids->HardwareNumber, "Invalid reg num: %d for %s source param. Max allowed for this type "
                         "is %d.", Source->Number, Ordinal, Hardware - 1);
                    Bad = TRUE;
                }
                Params->HardwareCapable = FALSE;
            }
            else if (Rows > 1 && Last >= Hardware && !RelativeConst)
            {
                if (HardwareRequired() || SoftwareFailed)
                {
                    Fail(2, Ids->HardwareMatrix, "Reg num: %d for %s source param on matrix instruction causes attempt "
                         "to access out of range register number %d. Max allowed for this type is %d.", Source->Number,
                         Ordinal, Last, Hardware - 1);
                    Bad = TRUE;
                }
                Params->HardwareCapable = FALSE;
            }
        }

    Modifiers:
        if (Source->Modifier != 0)
        {
            BOOLEAN Allowed = Source->Modifier == D9SvModNegate ||
                              (Model == 3 && (Source->Modifier == D9SvModAbs || Source->Modifier == D9SvModAbsNegate));

            if (!Allowed)
            {
                if (!(Source->Modifier == D9SvModNot && (Op == D9SvOpIf || Op == D9SvOpCallnz || Op == D9SvOpBreakp)))
                {
                    Fail(2, Ids->SourceModifier, "Invalid src mod for %s source param.", Ordinal);
                    Bad = TRUE;
                }
            }
            else
            {
                if (Index == 1 && D9SvIsMatrix(Op))
                {
                    if (Model == 2)
                        Fail(2, 0x2B5, "Cannot use negate on second source parameter to vector*matrix instructions.");
                    else
                        Fail(2, 0x2B7, "Cannot use negate or abs on second source parameter to vector*matrix "
                             "instructions.");
                    Bad = TRUE;
                }
                switch (Op)
                {
                case 25: case 26: case 27: case 30: case 38: case 40: case 96:
                    if (Model == 2)
                        Fail(2, 694, "Cannot use negate on source parameter(s) to %s instruction.", D9SvOpcodeName(Op));
                    else
                        Fail(2, 696, "Cannot use negate or abs on source parameter(s) to %s instruction.",
                             D9SvOpcodeName(Op));
                    Bad = TRUE;
                    break;
                default:
                    break;
                }
            }
        }

        switch (Op)
        {
        case 6: case 7: case 14: case 15: case 32: case 41: case 45: case 78: case 79: case 96:
            if (!D9SvReplicate(Source->Swizzle))
            {
                Fail(2, Ids->Replicate, "%s requires replicate swizzle for source parameter(s) in order to select "
                     "component. i.e. .x | .y | .z | .w (or rgba equivalent)", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            break;
        case 37:
            if (Model == 3 && !D9SvReplicate(Source->Swizzle))
            {
                Fail(2, Ids->Replicate, "%s requires replicate swizzle for source parameter(s) in order to select "
                     "component. i.e. .x | .y | .z | .w (or rgba equivalent)", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            break;
        default:
            break;
        }

        if (Op == D9SvOpCallnz)
        {
            BOOLEAN Plain = Source->Swizzle == D9SvSwizzleIdentity;

            if (Model == 2 && Version == D9VsVersion20 && (Index == 0 || Source->Type == D9SvRegConstBool) && !Plain)
            {
                Fail(2, 950, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or none "
                     "specified.).  The exception is (on shader version > vs_2_0) if the condition is the predicate "
                     "register, p0, in which case a replicate swizzle must be used in order to select a component. "
                     "i.e. .x | .y | .z | .w (or rgba equivalent) ", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            else if ((Index == 0 || Source->Type == D9SvRegConstBool) && !Plain)
            {
                Fail(2, Model == 2 ? 951 : 956, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, "
                     "or none specified.).  The exception is if the condition is the predicate register, p0, in which "
                     "case a replicate swizzle must be used in order to select a component. i.e. .x | .y | .z | .w (or "
                     "rgba equivalent)", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            else if (Index == 1 && Source->Type == D9SvRegPredicate && !D9SvReplicate(Source->Swizzle))
            {
                Fail(2, Ids->CallnzPredicateSwizzle, "When predicate register is used as the condition for a %s "
                     "instruction, a replicate swizzle must be specified, to select a component. i.e. .x | .y | .z | "
                     ".w (or rgba equivalent)", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpRep || Op == D9SvOpLoop)
        {
            if (Source->Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, Ids->LoopRepSwizzle, "Parameter(s) for %s instruction must specify default swizzle (.xyzw, or "
                     "none specified.) ", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
        }
        else if (Op == D9SvOpIf)
        {
            if (Source->Type == D9SvRegConstBool && Source->Swizzle != D9SvSwizzleIdentity)
            {
                Fail(2, Ids->IfBoolSwizzle, "Const bool (b#) parameter to %s instruction must specify default swizzle "
                     "(.xyzw, or none specified.) ", D9SvOpcodeName(Op));
                Bad = TRUE;
            }
            else if (Source->Type == D9SvRegPredicate && !D9SvReplicate(Source->Swizzle))
            {
                Fail(2, Ids->IfPredicateSwizzle, "Predicate register (p0) parameter to %s instruction must specify "
                     "replicate swizzle in order to select component. i.e. .x | .y | .z | .w (or rgba equivalent)",
                     D9SvOpcodeName(Op));
                Bad = TRUE;
            }
        }

        if (Source->Swizzle != D9SvSwizzleIdentity && Index == 1 && D9SvIsMatrix(Op))
        {
            Fail(2, Ids->MatrixSwizzle, "Cannot swizzle second source parameter to vector*matrix instructions.");
            Bad = TRUE;
        }
        if (Bad)
            SkipSource[Index] = TRUE;
    }
}

VOID
D9VsChecker::SourcePredicate()
{
    D9SvInstruction* Ins = Current;
    D9SvSource* Predicate = &Ins->Predicate;

    if (!Ins->Predicated)
        return;
    if (Model == 2)
    {
        if (Version == D9VsVersion20)
        {
            Fail(2, 656, "vs_2_0 does not support predication.");
            return;
        }
        if (Version != D9VsVersion2sw && Caps != nullptr && (Caps->VS20Caps.Caps & D3DVS20CAPS_PREDICATION) == 0)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 657, "vs_2_x predication not supported by the hardware.");
                return;
            }
        }
    }
    switch (Ins->Opcode)
    {
    case 25: case 26: case 27: case 28: case 29: case 31: case 38: case 39: case 40: case 41: case 42: case 43:
    case 44: case 45: case 47: case 48: case 81: case 94: case 96:
        Fail(2, Ids->CannotPredicate, "%s instruction cannot be predicated.", D9SvOpcodeName(Ins->Opcode));
        return;
    default:
        break;
    }
    if (Predicate->Type != D9SvRegPredicate || Predicate->Number != 0 || Predicate->Relative != 0)
    {
        Fail(2, Ids->PredicateRegister, "Source predicate must be p0 register.");
        return;
    }
    if (!D9SvReplicate(Predicate->Swizzle) && Predicate->Swizzle != D9SvSwizzleIdentity)
        Fail(2, Ids->PredicateSwizzle, "Source predicate p0 must use one of the following swizzles: .x | .y | .z | .w "
             "| .xyzw (same as swizzle not present)");
    if (Predicate->Modifier != D9SvModNot && Predicate->Modifier != 0)
        Fail(2, Ids->PredicateModifier, "The only modifier allowed for source predicate p0 is the not (!) modifier.");
    if (Model == 2)
    {
        DWORD Missing = Predicate->Read & ~Flow->PredicateWritten;

        if (Missing != 0)
        {
            char Text[32];

            ComponentText(Missing, FALSE, TRUE, Text);
            Fail(2, 0x170, "Predicate register read, but uninitialized. Note that the predicate register must be "
                 "written by setp before it is used, and that flow control instructions invalidate the predicate "
                 "register's contents. Affected components(*): %s", Text);
        }
        if (Ins->Dest.Type == D9SvRegRastOut && (Ins->Dest.Number == 1 || Ins->Dest.Number == 2) &&
            !D9SvReplicate(Predicate->Swizzle))
            Fail(2, 1070, "When writing to scalar output (oFog, oPts) with predication, p0 must use a replicate "
                 "swizzle to select a single component: .x | .y | .z | .w");
        return;
    }
    if (Ins->Dest.Type == D9SvRegOutput)
    {
        for (UINT Index = 0; Index < OutputCount3; Index++)
        {
            if (Outputs3[Index].Usage == D3DDECLUSAGE_PSIZE && Outputs3[Index].Register == Ins->Dest.Number)
            {
                if (!D9SvReplicate(Predicate->Swizzle))
                    Fail(2, 1071, "When writing to scalar output (psize) with predication, p0 must use a replicate "
                         "swizzle to select a single component: .x | .y | .z | .w");
                return;
            }
        }
    }
}

/* Reads of components nothing declared or wrote */
VOID
D9VsChecker::SourceInitialized()
{
    D9SvInstruction* Ins = Current;
    static const PCSTR AlText = "aL can only be referenced in the following scenarios: (1) inside a loop/endloop "
                                "block, or (2) in a subroutine for which in all cases some call below in the stack is "
                                "in a loop/endloop block. ";

    for (UINT Index = 0; Index < Ins->SourceCount; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];
        DWORD Mask = Source->Read;
        UINT Rows = Index == 1 ? D9SvMatrixRows(Ins->Opcode) : 1;

        if (SkipSource[Index])
            continue;

        if (Model == 1)
        {
            D9SvRegisters* File;
            PCSTR Prefix;
            UINT Number = Source->Number;

            if (Source->Type == D9SvRegTemp)
            {
                File = &Temps;
                Prefix = "r";
            }
            else if (Source->Type == D9SvRegInput)
            {
                File = &InputRegs;
                Prefix = "v";
            }
            else if (Source->Type == D9SvRegConst && Source->Relative == D9SvRelativeBit)
            {
                File = &Address;
                Prefix = "a";
                Mask = Source->RelativeComponent;
                Number = 0;
                Rows = 1;
            }
            else
            {
                continue;
            }
            for (UINT Row = 0; Row < Rows; Row++)
            {
                DWORD Missing = Mask & ~File->Written(Number + Row);
                UINT Count = 0;
                char Text[32];

                if (Number + Row >= File->Count)
                    Missing = Mask;
                for (UINT Component = 0; Component < 4; Component++)
                {
                    if ((Missing & D9SvComponent(Component)) != 0)
                        Count += 1;
                }
                if (Count == 0)
                    continue;
                ComponentText(Missing, FALSE, TRUE, Text);
                Fail(2, 0x146, "Read of uninitialized component%s(*) in %s%d: %s", Count > 1 ? "s" : "", Prefix,
                     Number + Row, Text);
            }
            continue;
        }

        if (Model == 3 && Ins->Opcode == D9SvOpTexldl && Ins->SourceCount == 2 && !SkipSource[1])
        {
            if (Index == 1)
            {
                if (Samplers.Written(Ins->Source[1].Number) == 0)
                    Fail(2, 0x241, "Sampler register s%d has not been declared.", Source->Number);
                continue;
            }
            Mask |= D9SvSwizzled(Source->Swizzle, Samplers.Written(Ins->Source[1].Number) | D9SvComponentW);
        }

        if (Source->Type == D9SvRegInput && !(Model == 3 && Source->Relative == D9SvRelativeBit))
        {
            /* vs_2_* looks at the first row's declaration for every row it names */
            for (UINT Row = 0; Row < Rows; Row++)
            {
                DWORD Missing = Mask & ~InputRegs.Written(Source->Number + (Model == 3 ? Row : 0));
                UINT Count = 0;
                char Text[32];

                for (UINT Component = 0; Component < 4; Component++)
                {
                    if ((Missing & D9SvComponent(Component)) != 0)
                        Count += 1;
                }
                if (Count == 0)
                    continue;
                ComponentText(Missing, FALSE, TRUE, Text);
                if (Model == 2)
                    Fail(2, 0x2BE, "Input register v%d was not declared with a dcl statement.  Attempt to read "
                         "component%s(*): %s", Source->Number + Row, Count > 1 ? "s" : "", Text);
                else
                    Fail(2, 0x2BF, "Attempt to read undeclared component%s of input register v%d (*): %s. Components "
                         "of v# registers to be read must be declared by a dcl statement.", Count > 1 ? "s" : "",
                         Source->Number + Row, Text);
            }
            continue;
        }
        if (Source->Relative == D9SvRelativeBit && Source->RelativeType == D9SvRegLoop &&
            (Source->Type == D9SvRegConst || (Model == 3 && Source->Type == D9SvRegInput)) &&
            !Flow->LoopCounterUsable())
        {
            if (Model == 2)
                Fail(2, 0x2C1, "%s", AlText);
            else
                Fail(2, Source->Type == D9SvRegInput ? 706 : 707, "%s", AlText);
        }
    }
}

VOID
D9VsChecker::AddressWrite1()
{
    D9SvInstruction* Ins = Current;

    if (!Ins->Dest.Present || Ins->Dest.Type != D9SvRegAddress)
        return;
    if (Ins->Opcode != D9SvOpMov)
        Fail(2, 329, "Only the mov instruction is allowed to write to the address register.");
    else if (Ins->Dest.Number != 0 || Ins->Dest.Mask != D9SvComponentX || Ins->Dest.Modifier != 0 ||
             Ins->Dest.Shift != 0)
        Fail(2, 328, "Format for address register must be a0.x.");
}

/* vs_1_1 and vs_2_* treat oFog and oPts as scalars; vs_3_0 treats an o# declared psize0 as one */
BOOLEAN
D9VsChecker::ScalarDest() const
{
    const D9SvDest* Dest = &Current->Dest;

    if (Model < 3)
        return Dest->Type == D9SvRegRastOut && (Dest->Number == 1 || Dest->Number == 2);
    if (Dest->Type != D9SvRegOutput)
        return FALSE;
    for (UINT Index = 0; Index < OutputCount3; Index++)
    {
        if (Outputs3[Index].Usage == D3DDECLUSAGE_PSIZE && Outputs3[Index].Register == Dest->Number)
            return TRUE;
    }
    return FALSE;
}

static
DWORD
D9VsMatrixMask(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
    case D9SvOpM4x4:
    case D9SvOpM3x4:
        return D9SvMaskAll;
    case D9SvOpM4x3:
    case D9SvOpM3x3:
        return D9SvMaskXyz;
    default:
        return 0x30000;
    }
}

static
PCSTR
D9VsMatrixName(
    _In_ UINT Opcode,
    _Out_ PCSTR* Mask)
{
    switch (Opcode)
    {
    case D9SvOpM4x4:
        *Mask = "xyzw (same as not specifying mask)";
        return "M4x4";
    case D9SvOpM4x3:
        *Mask = "xyz";
        return "M4x3";
    case D9SvOpM3x4:
        *Mask = "xyzw (same as not specifying mask)";
        return "M3x4";
    case D9SvOpM3x3:
        *Mask = "xyz";
        return "M3x3";
    default:
        *Mask = "xy";
        return "M3x2";
    }
}

/* A matrix product cannot write over its own inputs */
VOID
D9VsChecker::MatrixOverlap(
    _Inout_ BOOLEAN* Bad,
    _In_ DWORD FirstId,
    _In_ DWORD RowsId)
{
    D9SvInstruction* Ins = Current;

    if (!D9SvIsMatrix(Ins->Opcode))
        return;
    for (UINT Index = 0; Index < Ins->SourceCount && Index < 2; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];
        UINT Last = Source->Number + D9SvMatrixRows(Ins->Opcode) - 1;

        if (Ins->Dest.Type != Source->Type)
            continue;
        if (Index == 0 && Ins->Dest.Number == Source->Number && (Source->Read & Ins->Dest.Mask) != 0)
        {
            Fail(2, FirstId, "Dest register cannot be the same as first source register for m*x* instructions.");
            *Bad = TRUE;
        }
        else if (Index == 1 && Source->Number < Ins->Dest.Number && Ins->Dest.Number <= Last)
        {
            Fail(2, RowsId, "Dest register for m*x* instructions cannot be the same as one of the additional "
                 "registers implied by the second source register.");
            *Bad = TRUE;
        }
    }
}

BOOLEAN
D9VsChecker::DestParam1()
{
    D9SvInstruction* Ins = Current;
    D9SvDest* Dest = &Ins->Dest;
    D9SvRegisters* File = nullptr;
    BOOLEAN Scalar = FALSE;
    BOOLEAN Bad = FALSE;
    PCSTR MaskName;

    if (Ins->Opcode == D9SvOpDef || Ins->Opcode == D9SvOpDcl || !Dest->Present)
        return TRUE;
    switch (Dest->Type)
    {
    case D9SvRegTemp:
        File = &Temps;
        break;
    case D9SvRegAddress:
        File = &Address;
        break;
    case D9SvRegRastOut:
        File = &RastOut;
        Scalar = Dest->Number == 1 || Dest->Number == 2;
        break;
    case D9SvRegAttrOut:
        File = &AttrOut;
        break;
    case D9SvRegOutput:
        File = &TexOut;
        break;
    default:
        break;
    }
    if (File == nullptr || !File->Writable || File->Count == 0)
    {
        Fail(2, 0x14A, "Invalid reg type for dest param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 0x14B, "Invalid dest reg num: %d. Max allowed for this reg type is %d.", Dest->Number, File->Count - 1);
        Bad = TRUE;
    }
    if (Dest->Modifier != 0)
    {
        Fail(2, 0x14C, "Dst modifiers not allowed for vertex shaders.");
        Bad = TRUE;
    }
    if (Dest->Shift != 0)
    {
        Fail(2, 0x14D, "Dest shifts not allowed for vertex shaders.");
        Bad = TRUE;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 0x14E, "Dest write mask cannot be empty.");
        Bad = TRUE;
    }

    switch (Ins->Opcode)
    {
    case D9SvOpDp3:
    case D9SvOpDp4:
        break;
    case D9SvOpLit:
    case D9SvOpDst:
    case D9SvOpFrc:
        if (Scalar)
        {
            Fail(2, 0x1B9, "%s instruction cannot write to scalar output registers (oFog, oPts).",
                 D9SvOpcodeName(Ins->Opcode));
            Bad = TRUE;
        }
        break;
    case D9SvOpM4x4: case D9SvOpM4x3: case D9SvOpM3x4: case D9SvOpM3x3: case D9SvOpM3x2:
        if (Scalar)
        {
            Fail(2, 482, "M*x* matrix instructions cannot write to scalar output registers (oFog, oPts).");
            Bad = TRUE;
        }
        else if (Dest->Mask != D9VsMatrixMask(Ins->Opcode))
        {
            PCSTR Name = D9VsMatrixName(Ins->Opcode, &MaskName);

            Fail(2, 0x1DD, "%s instruction must use destination writemask: .%s", Name, MaskName);
            Bad = TRUE;
        }
        break;
    default:
        if (Scalar)
        {
            for (UINT Index = 0; Index < Ins->SourceCount; Index++)
            {
                if (!D9SvReplicate(Ins->Source[Index].Swizzle))
                {
                    Fail(2, 0x1AE, "When writing to scalar output register, %s instruction must use replicate swizzle "
                         "on source parameter(s), in order to select single component. i.e. .x | .y | .z | .w (or "
                         "rgba equivalent)", D9SvOpcodeName(Ins->Opcode));
                    break;
                }
            }
        }
        break;
    }
    if (!Bad && Scalar && Dest->Mask != D9SvMaskAll)
    {
        Fail(2, 481, "Scalar output registers (oFog, oPts) must have full write mask.");
        Bad = TRUE;
    }
    MatrixOverlap(&Bad, 335, 336);
    if (!Bad && File != nullptr)
    {
        DWORD Mask = Dest->Mask;

        if (Ins->Opcode == D9SvOpM4x3 || Ins->Opcode == D9SvOpM3x3)
            Mask &= D9SvMaskXyz;
        else if (Ins->Opcode == D9SvOpM3x2)
            Mask &= 0x30000;
        File->Mark(Dest->Number, Mask);
    }
    return TRUE;
}

BOOLEAN
D9VsChecker::DestParam2()
{
    D9SvInstruction* Ins = Current;
    D9SvDest* Dest = &Ins->Dest;
    D9SvRegisters* File = nullptr;
    BOOLEAN Scalar = FALSE;
    BOOLEAN Bad = FALSE;
    PCSTR MaskName;

    if (Ins->Opcode == D9SvOpDcl || Ins->Opcode == D9SvOpDefb || Ins->Opcode == D9SvOpDefi ||
        Ins->Opcode == D9SvOpDef || !Dest->Present)
        return TRUE;
    switch (Dest->Type)
    {
    case D9SvRegTemp:
        File = &Temps;
        break;
    case D9SvRegAddress:
        File = &Address;
        break;
    case D9SvRegRastOut:
        File = &RastOut;
        Scalar = Dest->Number == 1 || Dest->Number == 2;
        break;
    case D9SvRegAttrOut:
        File = &AttrOut;
        break;
    case D9SvRegOutput:
        File = &TexOut;
        break;
    case D9SvRegLoop:
        File = &LoopCounter;
        break;
    case D9SvRegPredicate:
        File = &Predicates;
        if (Version == D9VsVersion2x && Caps != nullptr && (Caps->VS20Caps.Caps & D3DVS20CAPS_PREDICATION) == 0)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 0x294, "vs_2_x predication not supported by the hardware.");
                return FALSE;
            }
        }
        break;
    default:
        break;
    }
    if (File == nullptr || !File->Writable || File->Count == 0)
    {
        Fail(2, 0x2CB, "Invalid reg type for dest param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 0x176, "Invalid dest reg num: %d. Max allowed for this reg type is %d.", Dest->Number, File->Count - 1);
        Bad = TRUE;
    }
    if (Dest->Modifier != 0)
    {
        Fail(2, 0x177, "Dst modifiers not allowed for vertex shaders.");
        Bad = TRUE;
    }
    if (Dest->Shift != 0)
    {
        Fail(2, 0x178, "Dest shifts not allowed for vertex shaders.");
        Bad = TRUE;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 0x179, "Dest write mask cannot be empty.");
        Bad = TRUE;
    }
    if (Dest->RelativeType != MAXUINT)
    {
        Fail(2, 0x2D5, "Relative addressing of destination not permitted on destination in this shader model.");
        Bad = TRUE;
    }
    if (Dest->Type == D9SvRegPredicate)
    {
        if (Ins->Opcode != D9SvOpSetp && Version != D9VsVersion20)
        {
            Fail(2, 716, "The only instruction allowed to write to the predicate register is setp.");
            Bad = TRUE;
        }
    }
    else if (Ins->Opcode == D9SvOpSetp && Version != D9VsVersion20)
    {
        Fail(2, 717, "The setp instruction must write to the predicate register.");
        Bad = TRUE;
    }

    switch (Ins->Opcode)
    {
    case D9SvOpDp3:
    case D9SvOpDp4:
        break;
    case D9SvOpLit: case D9SvOpDst: case D9SvOpCrs: case D9SvOpNrm: case D9SvOpSincos:
        if (Scalar)
        {
            Fail(2, 0x1B4, "%s instruction cannot write to scalar output registers (oFog, oPts).",
                 D9SvOpcodeName(Ins->Opcode));
            Bad = TRUE;
            goto Overlap;
        }
        break;
    case D9SvOpM4x4: case D9SvOpM4x3: case D9SvOpM3x4: case D9SvOpM3x3: case D9SvOpM3x2:
        if (Scalar)
        {
            Fail(2, 484, "M*x* matrix instructions cannot write to scalar output registers (oFog, oPts).");
            Bad = TRUE;
            goto Overlap;
        }
        if (Dest->Mask != D9VsMatrixMask(Ins->Opcode))
        {
            PCSTR Name = D9VsMatrixName(Ins->Opcode, &MaskName);

            Fail(2, 0x1DE, "%s instruction must use destination writemask: .%s", Name, MaskName);
            Bad = TRUE;
            goto Overlap;
        }
        break;
    default:
        if (Scalar)
        {
            for (UINT Index = 0; Index < Ins->SourceCount; Index++)
            {
                if (!D9SvReplicate(Ins->Source[Index].Swizzle))
                {
                    Fail(2, 0x18E, "When writing to scalar output register, %s instruction must use replicate swizzle "
                         "on source parameter(s), in order to select single component. i.e. .x | .y | .z | .w (or "
                         "rgba equivalent)", D9SvOpcodeName(Ins->Opcode));
                    break;
                }
            }
        }
        break;
    }
    if (!Bad && Scalar && Dest->Mask != D9SvMaskAll)
    {
        Fail(2, 483, "Scalar output registers (oFog, oPts) must have full write mask.");
        Bad = TRUE;
    }

Overlap:
    MatrixOverlap(&Bad, 479, 480);
    if (Bad)
        return TRUE;
    if (Dest->Type == D9SvRegPredicate)
        Flow->PredicateWritten |= Dest->Mask & D9SvMaskAll;
    else if (Dest->Type != D9SvRegLoop && File != nullptr)
        File->Mark(Dest->Number, Dest->Mask);
    return TRUE;
}

BOOLEAN
D9VsChecker::DestParam3()
{
    D9SvInstruction* Ins = Current;
    D9SvDest* Dest = &Ins->Dest;
    D9SvRegisters* File = nullptr;
    BOOLEAN Scalar = FALSE;
    BOOLEAN Bad = FALSE;
    PCSTR MaskName;

    if (Ins->Opcode == D9SvOpDcl || Ins->Opcode == D9SvOpDefb || Ins->Opcode == D9SvOpDefi ||
        Ins->Opcode == D9SvOpDef || !Dest->Present)
        return TRUE;
    switch (Dest->Type)
    {
    case D9SvRegTemp:
        File = &Temps;
        break;
    case D9SvRegAddress:
        File = &Address;
        break;
    case D9SvRegOutput:
        File = &Outputs;
        Scalar = ScalarDest();
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
    if (File == nullptr || !File->Writable || File->Count == 0)
    {
        Fail(2, 0x2CE, "Invalid reg type for dest param.");
        Bad = TRUE;
    }
    else if (Dest->Number >= File->Count)
    {
        Fail(2, 0x2D1, "Invalid dest reg num: %d. Max allowed for this reg type is %d.", Dest->Number, File->Count - 1);
        Bad = TRUE;
    }
    if (Dest->Modifier != 0)
    {
        if (Dest->Modifier != 0x100000)
        {
            Fail(2, 0x2D3, "Invalid dst modifier for vertex shader.");
            Bad = TRUE;
        }
        else
        {
            switch (Ins->Opcode)
            {
            case D9SvOpSincos: case D9SvOpFrc: case D9SvOpMova: case D9SvOpSetp: case D9SvOpTexldl:
                Fail(2, 0x2D2, "%s instruction does not support _sat modifier.", D9SvOpcodeName(Ins->Opcode));
                Bad = TRUE;
                break;
            default:
                break;
            }
        }
    }
    if (Dest->Shift != 0)
    {
        Fail(2, 0x2D4, "Dest shifts not allowed for vertex shaders.");
        Bad = TRUE;
    }
    if (Dest->Mask == 0)
    {
        Fail(2, 0x2D6, "Dest write mask cannot be empty.");
        Bad = TRUE;
    }
    if (Dest->RelativeType != MAXUINT)
    {
        if (Dest->Type != D9SvRegOutput)
        {
            Fail(2, 0x2D7, "Relative addressing of destination only permitted on output registers (o#).");
            Bad = TRUE;
        }
        else
        {
            if (Dest->RelativeType != D9SvRegLoop || Dest->RelativeNumber != 0)
            {
                Fail(2, 728, "Relative addressing of o# register requires aL register.");
                Bad = TRUE;
            }
            else if (Flow->LoopDepth == 0 && !(Flow->InSubroutine && !Flow->InSubroutineOutsideLoops()))
            {
                Fail(2, 708, "aL can only be referenced in the following scenarios: (1) inside a loop/endloop block, "
                     "or (2) in a subroutine for which in all cases some call below in the stack is in a "
                     "loop/endloop block. ");
                Bad = TRUE;
            }
            if (OutputIndexRestricted)
            {
                Fail(2, 0x432, "When relative addressing of output o# registers is used, position0 must be declared in "
                     "o%d only, psize0 (if present) must be declared in o%d only, and indexing from/into o%d, or o%d "
                     "(psize0 case only) is undefined. Indexing into any other declared o# is fine. If relative "
                     "addressing of o# is not used in the shader, these restrictions on register numbers do not "
                     "apply. ", 0, 1, 0, 1);
                Bad = TRUE;
            }
        }
    }
    if (Dest->Type == D9SvRegPredicate)
    {
        if (Ins->Opcode != D9SvOpSetp)
        {
            Fail(2, 719, "The only instruction allowed to write to the predicate register is setp.");
            Bad = TRUE;
        }
    }
    else if (Ins->Opcode == D9SvOpSetp)
    {
        Fail(2, 720, "The setp instruction must write to the predicate register.");
        Bad = TRUE;
    }

    switch (Ins->Opcode)
    {
    case D9SvOpDp3:
    case D9SvOpDp4:
        break;
    case D9SvOpLit: case D9SvOpDst: case D9SvOpCrs: case D9SvOpNrm: case D9SvOpSincos: case D9SvOpTexldl:
        if (Scalar)
        {
            Fail(2, 0x30D, "%s instruction cannot write to scalar output register.", D9SvOpcodeName(Ins->Opcode));
            Bad = TRUE;
            goto Overlap;
        }
        break;
    case D9SvOpM4x4: case D9SvOpM4x3: case D9SvOpM3x4: case D9SvOpM3x3: case D9SvOpM3x2:
        if (Scalar)
        {
            Fail(2, 811, "M*x* matrix instructions cannot write to scalar output register.");
            Bad = TRUE;
            goto Overlap;
        }
        if (Dest->Mask != D9VsMatrixMask(Ins->Opcode))
        {
            PCSTR Name = D9VsMatrixName(Ins->Opcode, &MaskName);

            Fail(2, 0x327, "%s instruction must use destination writemask: .%s", Name, MaskName);
            Bad = TRUE;
            goto Overlap;
        }
        break;
    default:
        if (Scalar)
        {
            for (UINT Index = 0; Index < Ins->SourceCount; Index++)
            {
                if (!D9SvReplicate(Ins->Source[Index].Swizzle))
                {
                    Fail(2, 0x308, "When writing to scalar output register, %s instruction must use replicate swizzle "
                         "on source parameter(s), in order to select single component. i.e. .x | .y | .z | .w (or "
                         "rgba equivalent)", D9SvOpcodeName(Ins->Opcode));
                    break;
                }
            }
        }
        break;
    }
    if (!Bad && Scalar && Dest->Mask != D9SvMaskAll)
    {
        Fail(2, 810, "Scalar output register must have full write mask.");
        Bad = TRUE;
    }

Overlap:
    if (Dest->RelativeType != MAXUINT)
        return TRUE;
    MatrixOverlap(&Bad, 808, 809);
    if (Bad)
        return TRUE;
    if (Dest->Type == D9SvRegOutput)
    {
        DWORD Missing = Dest->Mask & ~Outputs.Written(Dest->Number);

        if (Missing != 0)
        {
            UINT Count = 0;
            char Text[32];

            for (UINT Component = 0; Component < 4; Component++)
            {
                if ((Missing & D9SvComponent(Component)) != 0)
                    Count += 1;
            }
            ComponentText(Missing, FALSE, TRUE, Text);
            Fail(2, 0x2C0, "Attempt to write undeclared component%s of output register o%d (*): %s. Components of o# "
                 "registers to be written must be declared by a dcl statement.", Count > 1 ? "s" : "", Dest->Number,
                 Text);
        }
    }
    if (Dest->Type == D9SvRegTemp || Dest->Type == D9SvRegAddress || Dest->Type == D9SvRegOutput ||
        Dest->Type == D9SvRegPredicate)
        File->Mark(Dest->Number, Dest->Mask);
    return TRUE;
}

VOID
D9VsChecker::Definitions1()
{
    D9SvInstruction* Ins = Current;
    BOOLEAN Usable = TRUE;

    if (Ins->Opcode != D9SvOpDef && Ins->Opcode != D9SvOpDcl && Ins->Opcode != D9SvOpComment)
        DefOpen = FALSE;
    if (Ins->Opcode != D9SvOpDef)
        return;
    if (!DefOpen)
    {
        Fail(2, 0x151, "def or dcl instructions must appear before other instructions.");
        Usable = FALSE;
    }
    if (Ins->Dest.Mask != D9SvMaskAll || Ins->Dest.Modifier != 0 || Ins->Dest.Shift != 0 ||
        Ins->Dest.Type != D9SvRegConst)
    {
        Fail(2, 0x152, "Destination for def instruction must be of the form c# (where # = reg number). In addition "
             "there may be no mask (equivalent to full .xyzw, which is fine).");
        Usable = FALSE;
    }
    if (NoDevice)
        return;
    if (Ins->Dest.Type == D9SvRegConst && Ins->Dest.Number >= Consts.Count)
    {
        Fail(2, 0x153, "Invalid const register num: %d. Max allowed is %d.", Ins->Dest.Number, Consts.Count - 1);
    }
    else if (Usable)
    {
        if (Consts.Marks[0][Ins->Dest.Number] != 0)
            Fail(2, 0x255, "c%d already def'd in the shader. ", Ins->Dest.Number);
        Consts.Marks[0][Ins->Dest.Number] = 1;
    }
}

VOID
D9VsChecker::Definitions23()
{
    D9SvInstruction* Ins = Current;
    D9SvDest* Dest = &Ins->Dest;
    UINT Op = Ins->Opcode;
    BOOLEAN PlainDest = Dest->Mask == D9SvMaskAll && Dest->Modifier == 0 && Dest->Shift == 0 && Dest->Relative == 0;

    if (Op != D9SvOpDcl)
    {
        if (Op == D9SvOpDefb || Op == D9SvOpDefi || Op == D9SvOpDef)
        {
            if (!DefOpen)
                Fail(2, Ids->DefOrder, "def or dcl instructions must appear before other instructions.");
        }
        else if (Op != D9SvOpComment)
        {
            DefOpen = FALSE;
        }
    }

    if (Op == D9SvOpDef)
    {
        BOOLEAN Usable = TRUE;

        if (!PlainDest || Dest->Type != D9SvRegConst)
        {
            Fail(2, Ids->DefDest, "Destination for def instruction must be c# register (where # = reg number). In "
                 "addition there may be no mask (equivalent to full .xyzw, which is fine).");
            Usable = FALSE;
        }
        if (!NoDevice)
        {
            if (Dest->Type == D9SvRegConst && Dest->Number >= Consts.Count)
            {
                Fail(2, Ids->DefNumber, "Invalid const register num: %d. Max allowed is %d.", Dest->Number,
                     Consts.Count - 1);
            }
            else if (Usable)
            {
                if (Consts.Marks[0][Dest->Number] != 0)
                    Fail(2, Ids->DefTwice, "c%d already def'd in the shader. ", Dest->Number);
                Consts.Marks[0][Dest->Number] = 1;
            }
        }
    }
    if (Op == D9SvOpDefi)
    {
        if (!PlainDest || Dest->Type != D9SvRegConstInt)
            Fail(2, Ids->DefiDest, "Destination for defi instruction must be i# register (where # = reg number). In "
                 "addition there may be no mask (equivalent to full .xyzw, which is fine).");
        if (Dest->Type == D9SvRegConstInt)
        {
            UINT Number = Dest->Number;

            if (Number < IntConsts.Count)
            {
                INT Count = static_cast<INT>(Ins->Values[0]);

                if (Count < 0)
                    Fail(2, Ids->DefiNegative, "i%d.x (which represents loop or rep iteration count) cannot be "
                         "negative. i%d.x is currently def'd as %d. ", Number, Number, Count);
                if (Version != D9VsVersion2sw && Version != D9VsVersion3sw)
                {
                    if (Count > 255)
                        Fail(2, Ids->DefiCount, "i%d.x (which represents loop or rep iteration count) cannot be "
                             "greater than %d. i%d.x is currently def'd as %d. ", Number, 255, Number, Count);
                    if (Ins->Values[1] > 255)
                        Fail(2, Ids->DefiStart, "i%d.y (which represents loop initial value) must be in the range "
                             "[0, %d]. i%d.y is currently def'd as %d. ", Number, 255, Number, Ins->Values[1]);
                    if (Ins->Values[2] + 128 > 255)
                        Fail(2, Ids->DefiStep, "i%d.z (which represents loop step value) must be in the range [-%d, "
                             "%d]. i%d.z is currently def'd as %d. ", Number, 128, 127, Number, Ins->Values[2]);
                }
                if (Ins->Values[3] != 0)
                    Fail(2, Ids->DefiW, "i%d.w has no meaning; it must simply be 0. ", Number);
                if (IntConsts.Marks[0][Number] != 0)
                    Fail(2, Ids->DefiTwice, "i%d already def'd in the shader. ", Number);
                IntConsts.Marks[0][Number] = 1;
            }
            else
            {
                Fail(2, Ids->DefiNumber, "Invalid constant integer (i#) register num: %d. Max allowed is %d.", Number,
                     IntConsts.Count - 1);
            }
        }
    }
    if (Op == D9SvOpDefb)
    {
        if (!PlainDest || Dest->Type != D9SvRegConstBool)
            Fail(2, Ids->DefbDest, "Destination for defb instruction must be b# register (where # = reg number). In "
                 "addition there may be no mask (equivalent to full .xyzw, which is fine).");
        if (Dest->Type == D9SvRegConstBool)
        {
            UINT Number = Dest->Number;

            if (Number < BoolConsts.Count)
            {
                if (Ins->Values[0] > 1)
                    Fail(2, Ids->DefbValue, "b%d must only be defined as true (0x00000001) or false (0x00000000). ",
                         Number);
                if (BoolConsts.Marks[0][Number] != 0)
                    Fail(2, Ids->DefbTwice, "b%d already def'd in the shader. ", Number);
                BoolConsts.Marks[0][Number] = 1;
            }
            else
            {
                Fail(2, Ids->DefbNumber, "Invalid constant bool (b#) register num: %d. Max allowed is %d.", Number,
                     BoolConsts.Count - 1);
            }
        }
    }
}

/* vs_1_1 and vs_2_* declare only inputs, one usage each */
VOID
D9VsChecker::Declaration12()
{
    D9SvInstruction* Ins = Current;
    UINT Number = Ins->Dest.Number;
    UINT Usage;
    UINT Index;
    BYTE Key;

    if (Ins->Opcode == D9SvOpComment)
        return;
    if (Ins->Opcode != D9SvOpDcl)
    {
        if (Ins->Opcode != D9SvOpDef && Ins->Opcode != D9SvOpDefb && Ins->Opcode != D9SvOpDefi)
            DclOpen = FALSE;
        return;
    }
    if (!DclOpen)
        Fail(2, 0x3FC, "def and dcl instructions must appear before other instructions.");
    if (!Ins->Dest.Present)
    {
        Fail(2, 624, "dcl instruction is missing a parameter.");
        return;
    }
    if (Ins->Dest.Type != D9SvRegInput || InputRegs.Count == 0)
    {
        Fail(2, 626, "Invalid register type for dcl.  Must be input register (v#).");
        return;
    }
    if (Number >= InputRegs.Count)
    {
        Fail(2, 0x274, "Invalid register number: %d. Max allowed for %s# register is %d.", Number, "v",
             InputRegs.Count - 1);
        return;
    }
    if (Ins->Dest.Mask != D9SvMaskAll)
    {
        Fail(2, 0x320, "dcl must specify full writemask in this shader version (same as not specifying mask) for %s# "
             "registers.", "v");
        return;
    }
    if (InputRegs.Written(Number) != 0)
    {
        Fail(2, 632, "Register %s%d has already been declared.", "v", Number);
        return;
    }
    Usage = Ins->DclInfo & 0xFFFF;
    Index = Ins->DclInfo >> 16;
    if (Usage > 13)
    {
        Fail(2, 634, "Unrecognized usage type in dcl statement.");
        return;
    }
    if (Index > 15)
    {
        Fail(2, 0x27C, "dcl usage index, %d, is too high.  Max allowed is %d.", Index, 15);
        return;
    }
    if (Usage == D3DDECLUSAGE_TESSFACTOR)
    {
        Fail(2, 975, "dcl usage 'tessfactor' is not permitted for vertex input shaders.");
        return;
    }
    if (Usage == D3DDECLUSAGE_POSITIONT && Index == 0)
    {
        Fail(2, 976, "Input cannot be dcl'd with usage+index positiont0, as vertex data containing this semantic has "
             "special meaning to the API: skip vertex processing altogether.");
        return;
    }
    Key = static_cast<BYTE>(Usage | (Index << 4));
    for (UINT Slot = 0; Slot < 15; Slot++)
    {
        if (InputUsage[Slot] == Key)
        {
            Fail(2, 637, "dcl usage: %s%d has already been specified.", D9SvUsageName(Usage), Index);
            return;
        }
    }
    InputUsage[Number] = Key;
    InputRegs.Mark(Number, D9SvMaskAll);
}

VOID
D9VsChecker::Declaration3()
{
    D9SvInstruction* Ins = Current;
    D9SvRegisters* File;
    PCSTR Prefix;
    BOOLEAN Input = FALSE;
    UINT Number;
    DWORD Mask;
    DWORD Overlap;
    UINT Usage;
    UINT Index;
    BYTE Key;

    if (Ended)
    {
        UINT Used = 0;

        if (PsizeDeclared || Outputs.Count == 0)
            return;
        for (UINT Register = 0; Register < Outputs.Count; Register++)
        {
            if (Outputs.Written(Register) != 0)
                Used += 1;
        }
        if (Used > Outputs.Count - 1)
            Fail(6, 0x26E, "Too many output registers declared (%d).  When no output register has been declared with "
                 "the semantic 'psize0', %d o# registers are available.  When an o# register has been declared with "
                 "the semantic 'psize0' (same as 'psize'), %d registers are available.", Used, Outputs.Count - 1,
                 Outputs.Count);
        return;
    }
    if (Ins->Opcode == D9SvOpComment)
        return;
    if (Ins->Opcode != D9SvOpDcl)
    {
        if (Ins->Opcode != D9SvOpDef && Ins->Opcode != D9SvOpDefb && Ins->Opcode != D9SvOpDefi)
            DclOpen = FALSE;
        return;
    }
    if (!DclOpen)
        Fail(2, 0x26F, "def and dcl instructions must appear before other instructions.");
    if (!Ins->Dest.Present)
    {
        Fail(2, 625, "dcl instruction is missing a parameter.");
        return;
    }
    switch (Ins->Dest.Type)
    {
    case D9SvRegInput:
        File = &InputRegs;
        Prefix = "v";
        Input = TRUE;
        break;
    case D9SvRegOutput:
        File = &Outputs;
        Prefix = "o";
        break;
    case D9SvRegSampler:
        File = &Samplers;
        Prefix = "s";
        break;
    default:
        File = nullptr;
        Prefix = "";
        break;
    }
    if (File == nullptr || File->Count == 0)
    {
        Fail(2, 627, "Invalid register type for dcl.  Must be input register (v#), output register (o#) or sampler "
             "(s#).");
        return;
    }
    Number = Ins->Dest.Number;
    if (Number >= File->Count)
    {
        Fail(2, 0x275, "Invalid register number: %d. Max allowed for %s# register is %d.", Number, Prefix,
             File->Count - 1);
        return;
    }
    Mask = Ins->Dest.Mask;
    if (Mask == 0)
    {
        Fail(2, 801, "dcl can't specify empty writemask.");
        return;
    }

    if (File == &Samplers)
    {
        DWORD Kind = Ins->DclInfo & 0x78000000;
        DWORD Coordinates;

        if ((Mask & Samplers.Written(Number)) != 0)
        {
            Fail(2, 771, "Sampler %s%d already declared. ", Prefix, Number);
            return;
        }
        if (Kind == 0x10000000)
        {
            Coordinates = 0x30000;
        }
        else if (Kind == 0x18000000 || Kind == 0x20000000)
        {
            if (Kind == 0x18000000)
                CubeSamplers |= 1u << Number;
            Coordinates = D9SvMaskXyz;
        }
        else
        {
            Fail(2, 586, "dcl %s# must be one of the following types: dcl_2d, dcl_cube or dcl_volume.", Prefix);
            return;
        }
        if (Mask != D9SvMaskAll)
        {
            Fail(2, 772, "dcl %s# must not specify a writemask (equivalent to a full mask). ", Prefix);
            return;
        }
        Samplers.Mark(Number, Coordinates);
        return;
    }

    Overlap = Mask & File->Written(Number);
    if (Overlap != 0)
    {
        char Text[32];

        ComponentText(Overlap, FALSE, TRUE, Text);
        Fail(2, 0x279, "Declaration for register %s%d overlaps previous declaration for the following component(s) of "
             "the same register (*): %s", Prefix, Number, Text);
        return;
    }
    Usage = Ins->DclInfo & 0xFFFF;
    Index = Ins->DclInfo >> 16;
    if (Usage > 13)
    {
        Fail(2, 635, "Unrecognized usage type in dcl statement.");
        return;
    }
    if (Index > 15)
    {
        Fail(2, 0x3FD, "dcl usage index, %d, is too high.  Max allowed is %d.", Index, 15);
        return;
    }
    if (Input)
    {
        if (Usage == D3DDECLUSAGE_TESSFACTOR)
        {
            Fail(2, 977, "dcl usage 'tessfactor' is not permitted for vertex shader input registers.");
            return;
        }
        if (Usage == D3DDECLUSAGE_POSITIONT && Index == 0)
        {
            Fail(2, 978, "Input cannot be dcl'd with usage+index positiont0, as vertex data containing this semantic "
                 "has special meaning to the API: skip vertex processing altogether.");
            return;
        }
    }
    else if (Usage == D3DDECLUSAGE_POSITIONT)
    {
        if (Index == 0)
        {
            Fail(2, 979, "Vertex shader output is not permitted to be dcl'd with the usage+index positiont0.");
            return;
        }
    }
    else if (Usage == D3DDECLUSAGE_TESSFACTOR)
    {
        if (Index != 0 || Mask != D9SvComponentX)
        {
            Fail(2, 980, "Vertex shader output dcl with usage 'tessfactor' must have usage index 0, and register must "
                 "have .x write mask. This semantic is for special purpose use only: creating vertex data that is to be "
                 "used with tesselation, where a scalar tessfactor is needed.  If you are not doing tesselation, do not "
                 "bother using this semantic. ");
            return;
        }
    }
    else if (Usage == D3DDECLUSAGE_PSIZE)
    {
        if (Index == 0)
        {
            if (Mask != D9SvMaskAll)
            {
                Fail(2, 981, "Output register dcl'd with usage+index psize0 takes as special meaning as a scalar, so no "
                     "writemask must be specified (identical to full write mask). In addition, no other semantics may "
                     "declared for the same output register as one assigned to psize0. ");
                return;
            }
            PsizeDeclared = TRUE;
            if (Number != 1)
                OutputIndexRestricted = TRUE;
        }
    }
    else if (Usage == D3DDECLUSAGE_SAMPLE)
    {
        Fail(2, 982, "Output register cannot be dcl'd with usage 'sample'.");
        return;
    }
    else if (Usage == D3DDECLUSAGE_POSITION && Index == 0 && Number != 0)
    {
        OutputIndexRestricted = TRUE;
    }

    Key = static_cast<BYTE>(Usage + 16 * Index);
    if (Input)
    {
        for (UINT Slot = 0; Slot < InputCount3; Slot++)
        {
            if (Inputs3[Slot].Usage == Key)
            {
                Fail(2, 638, "dcl usage+index: %s,%d has already been specified for an input register.",
                     D9SvUsageName(Usage), Index);
                return;
            }
        }
        if (InputCount3 < _countof(Inputs3))
        {
            Inputs3[InputCount3].Usage = Key;
            Inputs3[InputCount3].Register = Number;
            Inputs3[InputCount3].Mask = Mask;
            InputCount3 += 1;
        }
    }
    else
    {
        for (UINT Slot = 0; Slot < OutputCount3; Slot++)
        {
            if (Outputs3[Slot].Usage == Key)
            {
                Fail(2, 639, "dcl usage+index: %s,%d has already been specified for an output register.",
                     D9SvUsageName(Usage), Index);
                return;
            }
        }
        if (OutputCount3 < _countof(Outputs3))
        {
            Outputs3[OutputCount3].Usage = Key;
            Outputs3[OutputCount3].Register = Number;
            Outputs3[OutputCount3].Mask = Mask;
            OutputCount3 += 1;
        }
    }
    File->Mark(Number, Mask);
}

/* nrm, crs, pow, lrp, sincos and sgn operand rules */
VOID
D9VsChecker::Arithmetic()
{
    D9SvInstruction* Ins = Current;
    D9SvDest* Dest = &Ins->Dest;
    D9SvSource* Source = Ins->Source;

    switch (Ins->Opcode)
    {
    case D9SvOpNrm:
        if (Source[0].Type == Dest->Type && Source[0].Number == Dest->Number)
            Fail(2, Ids->NrmSame, "Src and dest registers for NRM cannot be the same.");
        if (Dest->Mask != D9SvMaskXyz && Dest->Mask != D9SvMaskAll)
            Fail(2, Ids->NrmMask, "Dest writemask for NRM must be .xyzw (default) or .xyz (or equivalent rgba "
                 "notation)");
        if (Dest->Type != D9SvRegTemp)
            Fail(2, Ids->NrmType, "Dest register type for NRM must be temp (r#).");
        break;
    case D9SvOpCrs:
        if ((Source[0].Type == Dest->Type && Source[0].Number == Dest->Number) ||
            (Source[1].Type == Dest->Type && Source[1].Number == Dest->Number))
            Fail(2, Ids->CrsSame, "Dest register for CRS cannot be the same as a source register.");
        if (Source[0].Swizzle != D9SvSwizzleIdentity || Source[1].Swizzle != D9SvSwizzleIdentity)
            Fail(2, Ids->CrsSwizzle, "Swizzle not permitted on source paramters to CRS.");
        if ((Dest->Mask & D9SvComponentW) != 0)
            Fail(2, Ids->CrsMask, "Write mask for CRS must be one of: .x | .y | .z | .xy | .xz | .yz | .xyz (or rgba "
                 "equivalent)");
        if (Dest->Type != D9SvRegTemp)
            Fail(2, Ids->CrsType, "Dest register type for CRS must be temp (r#).");
        break;
    case D9SvOpPow:
        if (Source[1].Type == Dest->Type && Source[1].Number == Dest->Number)
            Fail(2, Ids->PowSame, "Dest register for POW cannot be the same as second source register.");
        if (Dest->Type != D9SvRegTemp)
            Fail(2, Ids->PowType, "Dest register type for POW must be temp (r#).");
        break;
    case D9SvOpLrp:
        if ((Source[0].Type == Dest->Type && Source[0].Number == Dest->Number) ||
            (Source[2].Type == Dest->Type && Source[2].Number == Dest->Number))
            Fail(2, Ids->LrpSame, "Dest register for LRP cannot be the same as first or third source register.");
        if (Dest->Type != D9SvRegTemp)
            Fail(2, Ids->LrpType, "Dest register type for LRP must be temp (r#).");
        break;
    case D9SvOpSincos:
        if (Source[0].Type == Dest->Type && Source[0].Number == Dest->Number)
            Fail(2, Ids->SincosSame, "Dest register for SINCOS cannot be the same as first source register.");
        if (Model == 2)
        {
            BOOLEAN SecondConst = TRUE;

            if (Source[1].Type != D9SvRegConst)
            {
                Fail(2, 0x1CA, "Second source paramter for SINCOS must be a constant register.");
                SecondConst = FALSE;
            }
            if (Source[2].Type != D9SvRegConst)
                Fail(2, 0x1CB, "Third source paramter for SINCOS must be a constant register.");
            else if (SecondConst && Source[1].Number == Source[2].Number)
                Fail(2, 0x1CC, "Second and third source parameters for SINCOS cannot be the same constant register.");
            if (!D9SvReplicate(Source[0].Swizzle))
                Fail(2, 0x1CD, "Source selector for first source parameter for SINCOS must be .x, .y, .z or .w.");
            for (UINT Index = 1; Index < 3; Index++)
            {
                if (Source[Index].Modifier != 0)
                    Fail(2, 0x1CE, "Second and third source parameters for SINCOS cannot have a modifier.");
                if (Source[Index].Swizzle != D9SvSwizzleIdentity)
                    Fail(2, 0x1CF, "Second and third source parameters for SINCOS must use default swizzle (no swizzle "
                         "specified, or .xyzw). ");
            }
            if (Dest->Type != D9SvRegTemp)
                Fail(2, 0x1D5, "Dest register type for SINCOS must be temp (r#).");
            if (Dest->Mask != D9SvComponentX && Dest->Mask != D9SvComponentY && Dest->Mask != 0x30000)
                Fail(2, 0x221, "Dest mask for SINCOS must be one of: .x | .y | .xy ");
        }
        else
        {
            if (Dest->Type != D9SvRegTemp)
                Fail(2, 0x434, "Dest for SINCOS must be a temp (r#) register.");
            if (Dest->Mask != D9SvComponentX && Dest->Mask != D9SvComponentY && Dest->Mask != 0x30000)
                Fail(2, 0x3D7, "Dest mask for SINCOS must be one of: .x | .y | .xy ");
        }
        break;
    case D9SvOpSgn:
        if (Source[1].Type != D9SvRegTemp || Source[2].Type != D9SvRegTemp || Source[1].Number == Source[2].Number)
            Fail(2, Ids->SgnTemps, "Second and third source params for SGN must both be different temp (r#) "
                 "registers.");
        if (Source[0].Type == D9SvRegTemp &&
            ((Source[1].Type == D9SvRegTemp && Source[0].Number == Source[1].Number) ||
             (Source[2].Type == D9SvRegTemp && Source[0].Number == Source[2].Number)))
            Fail(2, Ids->SgnFirst, "First source param for SGN cannot be the same register as the second or third "
                 "params.");
        if (Source[1].Swizzle != D9SvSwizzleIdentity || Source[2].Swizzle != D9SvSwizzleIdentity ||
            Source[1].Modifier != 0 || Source[2].Modifier != 0)
            Fail(2, Ids->SgnPlain, "Second and third source params for SGN cannot use a source selector, and cannot have "
                 "a modifier. ");
        break;
    default:
        break;
    }
}

/* How many distinct registers of each file one instruction reads */
static
VOID
D9VsCountDistinct(
    _Inout_updates_(4) UINT* Seen,
    _Inout_ UINT* Count,
    _In_ UINT Number)
{
    for (UINT Index = 0; Index < *Count; Index++)
    {
        if (Seen[Index] == Number)
            return;
    }
    Seen[*Count] = Number;
    *Count += 1;
}

VOID
D9VsChecker::Ports1()
{
    D9SvInstruction* Ins = Current;
    UINT TempSeen[4];
    UINT InputSeen[4];
    UINT ConstSeen[4];
    UINT Temps1 = 0;
    UINT Inputs1 = 0;
    UINT Consts1 = 0;
    UINT ConstReads = 0;
    UINT InputReads = 0;
    BOOLEAN Absolute = FALSE;
    BOOLEAN RelativeSeen = FALSE;
    UINT RelativeNumber = 0;
    DWORD RelativeComponent = 0;

    for (UINT Index = 0; Index < 4; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];

        if (!Source->Present)
            continue;
        if (Source->Type == D9SvRegTemp)
        {
            D9VsCountDistinct(TempSeen, &Temps1, Source->Number);
        }
        else if (Source->Type == D9SvRegInput)
        {
            InputReads += 1;
            D9VsCountDistinct(InputSeen, &Inputs1, Source->Number);
        }
        else if (Source->Type == D9SvRegConst)
        {
            ConstReads += 1;
            if (Source->Relative != D9SvRelativeBit)
            {
                if (RelativeSeen)
                    Fail(2, 0x157, "Absolute and relative addressing of constant registers cannot be combined in one "
                         "instruction.");
                Absolute = TRUE;
            }
            else
            {
                if (Absolute)
                    Fail(2, 341, "Absolute and relative addressing of constant registers cannot be combined in one "
                         "instruction.");
                else if (RelativeSeen && !(RelativeNumber == Source->Number &&
                                           RelativeComponent == Source->RelativeComponent))
                    Fail(2, 342, "Different relative addressing of constant registers cannot be combined in one "
                         "instruction.");
                RelativeSeen = TRUE;
                RelativeNumber = Source->Number;
                RelativeComponent = Source->RelativeComponent;
            }
            D9VsCountDistinct(ConstSeen, &Consts1, Source->Number);
        }
    }
    if (Temps1 > Temps.Ports)
        Fail(2, 0x158, "%d different temp registers (r#) read by instruction.  Max. different temp registers readable "
             "per instruction is %d.", Temps1, Temps.Ports);
    if (Inputs1 > InputRegs.Ports)
        Fail(2, 0x159, "%d different input registers (v#) read by instruction.  Max. different input registers "
             "readable per instruction is %d.", Inputs1, InputRegs.Ports);
    if (Consts1 > Consts.Ports)
        Fail(2, 0x15A, "%d different constant registers (c#) read by instruction.  Max. different constant registers "
             "readable per instruction is %d.", Consts1, Consts.Ports);
    if (D9SvIsMatrix(Ins->Opcode))
    {
        if (ConstReads > 1)
            Fail(2, 0x15B, "Multiple constant registers cannot be read by a matrix op.");
        if (InputReads > 1)
            Fail(2, 0x15C, "Multiple input registers cannot be read by a matrix op.");
    }
}

VOID
D9VsChecker::Ports23()
{
    D9SvInstruction* Ins = Current;
    UINT TempSeen[4];
    UINT InputSeen[4];
    UINT ConstSeen[4];
    UINT Temps1 = 0;
    UINT Inputs1 = 0;
    UINT Consts1 = 0;
    UINT ConstReads = 0;
    UINT InputReads = 0;
    BOOLEAN ConstAbsolute = FALSE;
    BOOLEAN ConstRelative = FALSE;
    UINT ConstRelNumber = 0;
    DWORD ConstRelComponent = 0;
    UINT ConstRelType = 0x7FFFFFFF;
    BOOLEAN InputAbsolute = FALSE;
    BOOLEAN InputRelative = FALSE;
    UINT InputRelNumber = 0;
    DWORD InputRelComponent = 0;
    UINT InputRelType = 0x7FFFFFFF;
    BOOLEAN WithAbs = FALSE;
    BOOLEAN WithoutAbs = FALSE;
    BOOLEAN AbsReported = FALSE;

    if (Version == D9VsVersion2sw || Version == D9VsVersion3sw)
        return;
    if (!D9SvIsMatrix(Ins->Opcode) && (Ins->Opcode == D9SvOpSgn || Ins->Opcode == D9SvOpSincos))
        return;
    for (UINT Index = 0; Index < 4; Index++)
    {
        D9SvSource* Source = &Ins->Source[Index];

        if (!Source->Present)
            continue;
        if (Source->Type == D9SvRegTemp)
        {
            D9VsCountDistinct(TempSeen, &Temps1, Source->Number);
        }
        else if (Source->Type == D9SvRegInput)
        {
            InputReads += 1;
            if (Source->Relative != D9SvRelativeBit)
            {
                if (InputRelative)
                    Fail(2, Ids->InputMixed, "Absolute and relative addressing of input registers cannot be combined in "
                         "one instruction.");
                InputAbsolute = TRUE;
            }
            else
            {
                if (InputAbsolute)
                    Fail(2, Ids->InputMixedRelative, "Absolute and relative addressing of input registers cannot be "
                         "combined in one instruction.");
                else if (InputRelative && !(InputRelType == Source->RelativeType && InputRelNumber == Source->Number &&
                                            InputRelComponent == Source->RelativeComponent))
                    Fail(2, Ids->InputDifferent, "Different relative addressing of input registers cannot be combined "
                         "in one instruction.");
                InputRelative = TRUE;
                InputRelNumber = Source->Number;
                InputRelComponent = Source->RelativeComponent;
                InputRelType = Source->RelativeType;
            }
            D9VsCountDistinct(InputSeen, &Inputs1, Source->Number);
        }
        else if (Source->Type == D9SvRegConst)
        {
            ConstReads += 1;
            if (Model == 2 && ConstReads == 3 && Ins->Opcode != D9SvOpSincos)
                Fail(2, 0x16E, "More than 2 constants (even identical ones) cannot be read by an instruction. (There is "
                     "one exception: this rule does not apply to the SINCOS instruction.)");
            if (Source->Relative != D9SvRelativeBit)
            {
                if (ConstRelative)
                    Fail(2, Ids->ConstMixed, "Absolute and relative addressing of constant registers cannot be combined "
                         "in one instruction.");
                ConstAbsolute = TRUE;
            }
            else
            {
                if (ConstAbsolute)
                    Fail(2, Ids->ConstMixedRelative, "Absolute and relative addressing of constant registers cannot be "
                         "combined in one instruction.");
                else if (ConstRelative && !(ConstRelType == Source->RelativeType && ConstRelNumber == Source->Number &&
                                            ConstRelComponent == Source->RelativeComponent))
                    Fail(2, Ids->ConstDifferent, "Different relative addressing of constant registers cannot be "
                         "combined in one instruction.");
                ConstRelative = TRUE;
                ConstRelNumber = Source->Number;
                ConstRelComponent = Source->RelativeComponent;
                ConstRelType = Source->RelativeType;
            }
            if (Model == 3)
            {
                BOOLEAN Abs = Source->Modifier == D9SvModAbs || Source->Modifier == D9SvModAbsNegate;
                BOOLEAN Other;

                if (Abs)
                {
                    WithAbs = TRUE;
                    Other = WithoutAbs;
                }
                else
                {
                    WithoutAbs = TRUE;
                    Other = WithAbs;
                }
                if (Other && !AbsReported)
                {
                    Fail(2, 0x416, "When constant registers are read multiple times in a single instruction, the _abs "
                         "modifier must either be present on all of the constants, or none of them. ");
                    AbsReported = TRUE;
                }
            }
            D9VsCountDistinct(ConstSeen, &Consts1, Source->Number);
        }
    }
    if (Temps1 > Temps.Ports)
        Fail(2, Ids->PortTemps, "%d different temp registers (r#) read by instruction.  Max. different temp registers "
             "readable per instruction is %d.", Temps1, Temps.Ports);
    if (Inputs1 > InputRegs.Ports)
        Fail(2, Ids->PortInputs, "%d different input registers (v#) read by instruction.  Max. different input "
             "registers readable per instruction is %d. ", Inputs1, InputRegs.Ports);
    if (Consts1 > Consts.Ports)
    {
        if (Model == 2)
            Fail(2, 0x189, "%d different constant registers (c#) read by instruction.  Max. different constant "
                 "registers readable per instruction is %d. (There is one exception: this rule does not apply to the "
                 "SINCOS instruction.)", Consts1, Consts.Ports);
        else
            Fail(2, 0x2F0, "%d different constant registers (c#) read by instruction.  Max. different constant "
                 "registers readable per instruction is %d. ", Consts1, Consts.Ports);
    }
    if (D9SvIsMatrix(Ins->Opcode))
    {
        if (ConstReads > 1)
            Fail(2, Ids->MatrixConsts, "Multiple constant registers cannot be read by a matrix op.");
        if (InputReads > 1)
            Fail(2, Ids->MatrixInputs, "Multiple input registers cannot be read by a matrix op.");
    }
}

/* Reports a nesting limit; the target flavor speaks of compiled code */
BOOLEAN
D9VsChecker::ReportVersionLimit(
    _In_ DWORD Id,
    _In_z_ PCSTR Format,
    _In_ UINT Limit)
{
    char Name[32];

    D9SvVersionName(Version, Name);
    Fail(0x10002, Id, Format, Limit, Name);
    return FALSE;
}

BOOLEAN
D9VsChecker::FlowControl()
{
    D9SvInstruction* Ins = Current;
    D9SvFlow* Stack = Flow;
    UINT Op = Ins->Opcode;
    UINT LabelCap = Model == 2 ? 16 : 0x800;
    BOOLEAN Dynamic;

    if (Ins == nullptr)
        return TRUE;
    if ((Op == D9SvOpCall || Op == D9SvOpCallnz || Op == D9SvOpLabel) && !SkipSource[0] &&
        Ins->Source[0].Type == D9SvRegLabel)
    {
        UINT Label = Ins->Source[0].Number;

        if (Label >= LabelCap)
        {
            Fail(2, Ids->LabelTooLarge, "Subroutine label number, %d, is too large. Maximum label number is %d. "
                 "Aborting shader validation.", Label, LabelCap - 1);
            return FALSE;
        }
        if (Defined.Has(Label))
        {
            if (Op == D9SvOpLabel)
                Fail(2, Ids->LabelTwice, "Subroutine label number: %d already defined earlier. Aborting shader "
                     "validation.", Label);
            else
                Fail(2, Ids->CallBackwards, "Cannot call subroutine label defined earlier in shader (label #%d in this "
                     "case).  Subroutine call must reference label defined later in shader. Aborting shader "
                     "validation. ", Label);
            return FALSE;
        }
    }

    switch (Op)
    {
    case D9SvOpCall:
    case D9SvOpCallnz:
        if (SkipSource[0])
            break;
        Called.Add(Ins->Source[0].Number);
        Dynamic = Op == D9SvOpCallnz && !SkipSource[1] && Ins->Source[1].Type == D9SvRegPredicate;
        if (Dynamic)
        {
            if (Model == 2 && Stack->DynamicIfDepth >= DynamicIfHwLimit)
            {
                Params->HardwareCapable = FALSE;
                if (HardwareRequired())
                {
                    Fail(2, 1051, "Depth of dynamic 'if' nesting exceeds limit of %d (callnz with predicate counts as "
                         "a dynamic 'if' nesting level). Aborting shader validation. ", DynamicIfHwLimit);
                    return FALSE;
                }
            }
            if (Stack->DynamicIfDepth >= DynamicIfLimit)
            {
                if (Params->TargetMessages)
                    return ReportVersionLimit(Ids->DynamicCallnzTarget, "Compiled code results in dynamic flow control "
                                              "nesting depth that exceeds limit of %d for target (%s). ",
                                              DynamicIfLimit);
                Fail(2, Ids->DynamicCallnz, "Depth of dynamic 'if' nesting exceeds limit of %d (callnz with predicate "
                     "counts as a dynamic 'if' nesting level). Aborting shader validation. ", DynamicIfLimit);
                return FALSE;
            }
            Stack->Push(D9SvBlockDynamicIf);
        }
        if (Model == 2 && Stack->CallDepth >= CallHwLimit)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 859, "Depth of subroutine call nesting exceeds limit of %d. Aborting shader validation. ",
                     CallHwLimit);
                return FALSE;
            }
        }
        if (Stack->CallDepth >= CallLimit)
        {
            if (Params->TargetMessages)
                return ReportVersionLimit(Ids->CallDepthTarget, Model == 2 ? "Compiled code results in subroutine "
                                          "nesting depth that exceeds limit of %d for target (%s). " : "Compiled code "
                                          "results in subroutine call nesting depth that exceeds limit of %d for target "
                                          "(%s). ", CallLimit);
            Fail(2, Ids->CallDepth, "Depth of subroutine call nesting exceeds limit of %d. Aborting shader "
                 "validation. ", CallLimit);
            return FALSE;
        }
        if (!Stack->Call(Ins->Source[0].Number))
        {
            Fail(6, 0, "Out of memory. Aborting shader validation. ");
            return FALSE;
        }
        if (Dynamic)
            Stack->Pop();
        break;
    case D9SvOpLoop:
    case D9SvOpRep:
        if (Model == 2 && Stack->LoopOrRepDepth >= LoopHwLimit)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 828, "Depth of loop nesting exceeds limit of %d. Aborting shader validation. ", LoopHwLimit);
                return FALSE;
            }
        }
        if (Stack->LoopOrRepDepth >= LoopLimit)
        {
            if (Params->TargetMessages)
                return ReportVersionLimit(Ids->LoopDepthTarget, "Compiled code results in loop nesting depth that "
                                          "exceeds limit of %d for target (%s). ", LoopLimit);
            Fail(2, Ids->LoopDepth, "Depth of loop nesting exceeds limit of %d. Aborting shader validation. ",
                 LoopLimit);
            return FALSE;
        }
        Stack->Push(Op == D9SvOpLoop ? D9SvBlockLoop : D9SvBlockRep);
        break;
    case D9SvOpRet:
        if (Stack->TopKind() != D9SvBlockNone)
        {
            Fail(2, Ids->RetInside, "'ret' instruction not permitted from inside any flow control block. Aborting "
                 "shader validation. ");
            return FALSE;
        }
        Stack->Returned();
        break;
    case D9SvOpEndloop:
        if (Stack->TopKind() != D9SvBlockLoop)
        {
            Fail(2, Ids->EndloopAlone, "'endloop' instruction does not match up to an 'loop'. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Stack->Pop();
        break;
    case D9SvOpLabel:
        if (SkipSource[0])
            break;
        if (!AfterRet)
        {
            Fail(2, Ids->LabelAfterRet, "'label' is only permitted directly after a 'ret' instruction. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Defined.Add(Ins->Source[0].Number);
        if (Stack->TopKind() != D9SvBlockNone)
        {
            Fail(2, Ids->LabelInside, "Label is defined inside a subroutine or main program. Label must follow the ret "
                 "instruction. Aborting shader validation. ");
            return FALSE;
        }
        Stack->BeginSubroutine(Ins->Source[0].Number & 0x7FF);
        break;
    case D9SvOpEndrep:
        if (Stack->TopKind() != D9SvBlockRep)
        {
            Fail(2, Ids->EndrepAlone, "'endrep' instruction does not match up to a 'rep'. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Stack->Pop();
        break;
    case D9SvOpIf:
        if (SkipSource[0])
            break;
        if (Ins->Source[0].Type == D9SvRegPredicate)
        {
            if (Model == 2 && Stack->DynamicIfDepth >= DynamicIfHwLimit)
            {
                Params->HardwareCapable = FALSE;
                if (HardwareRequired())
                {
                    Fail(2, 863, "Depth of dynamic 'if' nesting exceeds limit of %d. Aborting shader validation. ",
                         DynamicIfHwLimit);
                    return FALSE;
                }
            }
            if (Stack->DynamicIfDepth >= DynamicIfLimit)
            {
                if (Params->TargetMessages)
                    return ReportVersionLimit(Ids->DynamicIfTarget, "Compiled code results in dynamic 'if'/'endif' "
                                              "nesting depth that exceeds limit of %d for target (%s). ",
                                              DynamicIfLimit);
                Fail(2, Ids->DynamicIf, "Depth of dynamic 'if' nesting exceeds limit of %d. Aborting shader "
                     "validation. ", DynamicIfLimit);
                return FALSE;
            }
            Stack->Push(D9SvBlockDynamicIf);
            break;
        }
        if (Model == 2 && Stack->StaticIfDepth >= StaticIfHwLimit)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 866, "Depth of static 'if' nesting exceeds limit of %d. Aborting shader validation. ",
                     StaticIfHwLimit);
                return FALSE;
            }
        }
        if (Stack->StaticIfDepth >= StaticIfLimit)
        {
            if (Params->TargetMessages)
                return ReportVersionLimit(Ids->StaticIfTarget, "Compiled code results in static 'if'/'endif' nesting "
                                          "depth that exceeds limit of %d for target (%s). ", StaticIfLimit);
            Fail(2, Ids->StaticIf, "Depth of static 'if' nesting exceeds limit of %d. Aborting shader validation. ",
                 StaticIfLimit);
            return FALSE;
        }
        Stack->Push(D9SvBlockStaticIf);
        break;
    case D9SvOpIfc:
        if (Model == 2 && Stack->DynamicIfDepth >= DynamicIfHwLimit)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 869, "Depth of dynamic 'if' nesting exceeds limit of %d. Aborting shader validation. ",
                     DynamicIfHwLimit);
                return FALSE;
            }
        }
        if (Stack->DynamicIfDepth >= DynamicIfLimit)
        {
            if (Params->TargetMessages)
                return ReportVersionLimit(Ids->IfcTarget, "Compiled code results in dynamic 'if'/'endif' nesting "
                                          "depth that exceeds limit of %d for target (%s). ", DynamicIfLimit);
            Fail(2, Ids->Ifc, "Depth of dynamic 'if' nesting exceeds limit of %d. Aborting shader validation. ",
                 DynamicIfLimit);
            return FALSE;
        }
        Stack->Push(D9SvBlockDynamicIf);
        break;
    case D9SvOpElse:
        if (Stack->TopKind() != D9SvBlockDynamicIf && Stack->TopKind() != D9SvBlockStaticIf)
        {
            Fail(2, Ids->ElseAlone, "'else' instruction does not match up to an 'if'. Aborting shader validation. ");
            return FALSE;
        }
        if (Stack->Blocks[Stack->Depth].SeenElse)
        {
            Fail(2, Ids->ElseTwice, "'else' already used for current 'if' construct. Aborting shader validation. ");
            return FALSE;
        }
        Stack->Blocks[Stack->Depth].SeenElse = TRUE;
        break;
    case D9SvOpEndif:
        if (Stack->TopKind() != D9SvBlockDynamicIf && Stack->TopKind() != D9SvBlockStaticIf)
        {
            Fail(2, Ids->EndifAlone, "'endif' instruction does not match up to an 'if' or 'else'. Aborting shader "
                 "validation. ");
            return FALSE;
        }
        Stack->Pop();
        break;
    case D9SvOpBreakc:
        if (Model == 2 && Stack->DynamicIfDepth >= DynamicIfHwLimit)
        {
            Params->HardwareCapable = FALSE;
            if (HardwareRequired())
            {
                Fail(2, 548, "Depth of dynamic 'if' nesting exceeds limit of %d, due to break_cmp that counts towards "
                     "same limit. Aborting shader validation. ", DynamicIfHwLimit);
                return FALSE;
            }
        }
        if (Stack->DynamicIfDepth >= DynamicIfLimit)
        {
            if (Params->TargetMessages)
                return ReportVersionLimit(Ids->BreakcTarget, "Compiled code results in dynamic 'if'/'endif' nesting "
                                          "depth that exceeds limit of %d for target (%s) (due to conditional break "
                                          "that counts towards the same limit). ", DynamicIfLimit);
            Fail(2, Ids->Breakc, "Depth of dynamic 'if' nesting exceeds limit of %d, due to break_cmp that counts "
                 "towards same limit. Aborting shader validation. ", DynamicIfLimit);
            return FALSE;
        }
        __fallthrough;
    case D9SvOpBreak:
    case D9SvOpBreakp:
        if (Stack->LoopOrRepCount == 0)
            Fail(2, Ids->BreakOutside, "'break' instruction not within a local loop/rep construct. Aborting shader "
                 "validation. ");
        break;
    default:
        break;
    }

    if (AfterRet && Op != D9SvOpLabel)
    {
        Fail(2, Ids->RetFollow, "A 'ret' instruction must be followed by either (1) the end of the shader or (2) a "
             "'label' instruction. Aborting shader validation.");
        return FALSE;
    }
    AfterRet = Op == D9SvOpRet;
    if (Model == 2)
    {
        switch (Op)
        {
        case D9SvOpCall:
        case D9SvOpLoop:
        case D9SvOpRep:
            StaticFlowCount += 1;
            break;
        case D9SvOpCallnz:
            if (Ins->Source[1].Type == D9SvRegConstBool)
                StaticFlowCount += 1;
            break;
        case D9SvOpIf:
            if (Ins->Source[0].Type == D9SvRegConstBool)
                StaticFlowCount += 1;
            break;
        case D9SvOpElse:
            if (Stack->TopKind() == D9SvBlockStaticIf)
                StaticFlowCount += 1;
            break;
        default:
            break;
        }
    }
    return TRUE;
}

VOID
D9VsChecker::FlowControlEnd()
{
    char Name[32];

    if (Current == nullptr)
        return;
    for (UINT Label = 0; Label < D9SvLabels; Label++)
    {
        if (Called.Has(Label) && !Defined.Has(Label))
            Fail(6, Ids->Undefined, "Subroutine with label #%d called, but never defined.", Label);
    }
    for (UINT Label = 0; Label < D9SvLabels; Label++)
    {
        if (Defined.Has(Label) && !Called.Has(Label))
            Fail(6, Ids->Unreferenced, "Subroutine label #%d never referenced.", Label);
    }
    if (Flow->InSubroutine)
        Fail(6, Ids->NoRet, "End of shader reached without 'ret' to terminate subroutine.");
    else if (Flow->LoopDepth != 0)
        Fail(6, Ids->OpenLoop, "End of shader reached and '%s' construct was not completed with a '%s'. ", "loop",
             "endloop");
    else if (Flow->RepDepth != 0)
        Fail(6, Ids->OpenLoop, "End of shader reached and '%s' construct was not completed with a '%s'. ", "rep",
             "endrep");
    else if (Flow->InsideIf())
        Fail(6, Ids->OpenIf, "End of shader reached and 'if' construct was not completed with 'else'/'endif'. ");
    if (Model == 2 && StaticFlowCount > 16 && Version != D9VsVersion2sw)
    {
        if (Params->TargetMessages)
        {
            D9SvVersionName(Version, Name);
            Fail(0x10006, 0x265, "Shader has too much flow control complexity for the target (%s). ", Name);
        }
        else
        {
            Fail(6, 0x208, "Too may static flow control instructions (%d) for this shader model.  Maximum allowed is %d. "
                 "The shader instructions that count as 1 static flow control instruction each are: (1)call, "
                 "(2)callnz (with static condition), (3)if (static branch), (4)else (paired with static if), (5)loop, "
                 "(6)rep. ", StaticFlowCount, 16);
        }
    }
}

/* Instruction slots each opcode costs */
VOID
D9VsChecker::CountSlots()
{
    D9SvInstruction* Ins = Current;
    UINT Cost = 0;

    if (Model == 1)
    {
        switch (Ins->Opcode)
        {
        case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 16:
        case 17: case 78: case 79:
            Cost = 1;
            break;
        case 14: case 15:
            Cost = 10;
            break;
        case 19: case 21: case 23:
            Cost = 3;
            break;
        case 20: case 22:
            Cost = 4;
            break;
        case 24:
            Cost = 2;
            break;
        default:
            break;
        }
        Slots += Cost;
        return;
    }
    switch (Ins->Opcode)
    {
    case 0: case 1: case 2: case 4: case 5: case 6: case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14:
    case 15: case 17: case 19: case 28: case 35: case 42: case 43: case 44: case 46: case 78: case 79: case 94:
        Cost = 1;
        break;
    case 16: case 21: case 23: case 26: case 27: case 32: case 34: case 36: case 38: case 40: case 41: case 45: case 96:
        Cost = 3;
        break;
    case 18: case 24: case 25: case 29: case 33: case 39:
        Cost = 2;
        break;
    case 20: case 22:
        Cost = 4;
        break;
    case 37:
        Cost = 8;
        break;
    case 95:
        if (Model == 3)
            Cost = (CubeSamplers & (1u << (Ins->Source[1].Number & 31))) != 0 ? 5 : 2;
        break;
    default:
        break;
    }
    Slots += Cost;
    if (Ins->Predicated)
        Slots += 1;
}

VOID
D9VsChecker::CountSlotsEnd()
{
    char Name[32];

    if (Current == nullptr)
        return;
    D9SvVersionName(Version, Name);
    if (Model < 3)
    {
        if (Slots <= SlotLimit)
            return;
        if (Params->TargetMessages)
            Fail(0x10006, Model == 1 ? 0x264 : 0x267, "Compiled shader code uses too many instruction slots (%d). Max. "
                 "allowed by the target (%s) is %d.", Slots, Name, SlotLimit);
        else
            Fail(6, Model == 1 ? 0x15D : 0x2F3, "Number of instruction slots used too high: %d. Max. allowed is %d.",
                 Slots, SlotLimit);
        return;
    }
    if (Slots > SlotLimit)
    {
        if (SoftwareRequired())
        {
            if (Params->TargetMessages)
                Fail(0x10006, 0x421, "Compiled shader code uses too many instruction slots (%d). Max. allowed by the "
                     "target (%s), including cap for instruction limit, is %d.", Slots, Name, SlotLimit);
            else
                Fail(6, 0x422, "Number of instruction slots used too high: %d. Max. allowed is %d.", Slots, SlotLimit);
        }
        Params->SoftwareCapable = FALSE;
    }
    if (Slots > DeviceSlotLimit)
    {
        if (HardwareRequired())
        {
            if (Params->TargetMessages)
                Fail(0x10006, 0x268, "Compiled shader code uses too many instructions (%d). Max. allowed by the target "
                     "(%s), including cap for instruction limit, is %d.", Slots, Name, DeviceSlotLimit);
            else
                Fail(6, 0x2F4, "Number of instruction slots used too high: %d. Max. allowed is %d.", Slots,
                     DeviceSlotLimit);
        }
        Params->HardwareCapable = FALSE;
    }
}

/* Each oTn written must fill its components from x on without gaps */
VOID
D9VsChecker::TexCoordGaps()
{
    for (UINT Register = 0; Register < TexOut.Count; Register++)
    {
        DWORD Mask = TexOut.Written(Register);
        DWORD Needed = 0;
        char Text[32];

        switch (Mask)
        {
        case 0x20000:
        case 0x60000:
        case 0xE0000:
            Needed = 0x10000;
            break;
        case 0x40000:
        case 0xC0000:
            Needed = 0x30000;
            break;
        case 0x50000:
        case 0xD0000:
            Needed = 0x20000;
            break;
        case 0x80000:
            Needed = 0x70000;
            break;
        case 0x90000:
            Needed = 0x60000;
            break;
        case 0xA0000:
            Needed = 0x50000;
            break;
        case 0xB0000:
            Needed = 0x40000;
            break;
        default:
            break;
        }
        if (Needed == 0)
            continue;
        ComponentText(Needed, FALSE, TRUE, Text);
        Fail(6, 0x1AB, "By the end of a vertex shader, any oTn register written must not have gaps in components, "
             "starting from x, in order xyzw. To satisfy this, oT%d requires writing of the additional component%s(*): "
             "%s ", Register, "", Text);
    }
}

VOID
D9VsChecker::PositionWritten()
{
    DWORD Missing = D9SvMaskAll & ~RastOut.Written(0);
    UINT Count = 0;
    char Text[32];

    for (UINT Component = 0; Component < 4; Component++)
    {
        if ((Missing & D9SvComponent(Component)) != 0)
            Count += 1;
    }
    if (Count == 4)
    {
        Fail(6, 0x15F, "Vertex shader must write all four components (xyzw) of oPos output register.");
        return;
    }
    if (Count == 0)
        return;
    ComponentText(Missing, FALSE, TRUE, Text);
    Fail(6, 0x15E, "Vertex shader must minimally write all four components (xyzw) of oPos output register.  Missing "
         "component%s(*): %s", Count > 1 ? "s" : "", Text);
}

/* The vertex format a vs_1 or vs_2 shader writes: coordinate sets sized by their last component, colors, the point
   size and fog; none at all without a position */
VOID
D9VsChecker::LogOutputs12()
{
    DWORD* Format = &Params->Outputs;
    INT Highest = -1;

    if (!Ended)
        return;
    if (Errors != 0)
    {
        *Format = 0;
        return;
    }
    for (UINT Set = 0; Set < D9SvMin(TexOut.Count, 8u); Set++)
    {
        DWORD Mask = TexOut.Written(Set);

        if (Mask == 0)
            continue;
        if (Highest <= static_cast<INT>(Set))
            Highest = static_cast<INT>(Set);
        if ((Mask & D9SvComponentW) != 0)
            *Format |= 2u << (2 * Set + 16);
        else if ((Mask & D9SvComponentZ) != 0)
            *Format |= 1u << (2 * Set + 16);
        else if ((Mask & D9SvComponentY) == 0)
            *Format |= 3u << (2 * Set + 16);
    }
    if (Highest >= 0 && Highest <= 7)
        *Format |= static_cast<DWORD>(Highest + 1) << 8;
    if (AttrOut.Count > 0 && AttrOut.Written(0) != 0)
        *Format |= 0x40;
    if (AttrOut.Count > 1 && AttrOut.Written(1) != 0)
        *Format |= 0x80;
    if (RastOut.Written(0) != 0)
        *Format |= 0x4;
    if (RastOut.Written(1) != 0)
        *Format |= 0x2000;
    if (RastOut.Written(2) != 0)
        *Format |= 0x20;
    if ((*Format & 0x4) == 0)
        *Format = 0;
}

/* The same format from vs_3_0 declarations; a declaration the format cannot express clears it */
VOID
D9VsChecker::LogOutputs3()
{
    DWORD* Format = &Params->Outputs;
    INT Highest = -1;

    if (!Ended)
        return;
    if (Errors != 0)
    {
        *Format = 0;
        return;
    }
    for (UINT Index = 0; Index < OutputCount3; Index++)
    {
        BYTE Usage = Outputs3[Index].Usage;
        DWORD Mask = Outputs3[Index].Mask;
        UINT Set = Usage >> 4;

        if (Usage == 0 || Usage == 4 || Usage == 10 || Usage == 26)
        {
            if (Mask != D9SvMaskAll)
            {
                *Format = 0;
                break;
            }
            *Format |= Usage == 0 ? 0x4 : Usage == 4 ? 0x20 : Usage == 10 ? 0x40 : 0x80;
            continue;
        }
        if (Usage == 11)
        {
            if (Mask != D9SvComponentX)
            {
                *Format = 0;
                break;
            }
            *Format |= 0x2000;
            continue;
        }
        if ((Usage & 0xF) != D3DDECLUSAGE_TEXCOORD || Set > 7)
        {
            *Format = 0;
            break;
        }
        if (Mask == D9SvComponentX)
            *Format |= 3u << (2 * (Set + 8));
        else if (Mask == D9SvMaskXyz)
            *Format |= 1u << (2 * (Set + 8));
        else if (Mask == D9SvMaskAll)
            *Format |= 2u << (2 * (Set + 8));
        else if (Mask != 0x30000)
        {
            *Format = 0;
            break;
        }
        if (Highest <= static_cast<INT>(Set))
            Highest = static_cast<INT>(Set);
    }
    if (Highest >= 0 && Highest <= 7)
        *Format |= static_cast<DWORD>(Highest + 1) << 8;
}

BOOLEAN
D9VsChecker::CheckInstruction()
{
    if (!Recognized())
        return FALSE;
    if (Model == 1)
    {
        if (!Supported() || !ParamCount())
            return TRUE;
        SourceParams1();
        SourceInitialized();
        AddressWrite1();
        DestParam1();
        Declaration12();
        Definitions1();
        if (Current->Opcode == D9SvOpFrc && Current->Dest.Mask != 0x20000 && Current->Dest.Mask != 0x30000)
            Fail(2, 0x154, "The only valid write masks for the FRC instruction are .xy and .y.");
        Ports1();
        CountSlots();
        return TRUE;
    }
    if (!Supported() || !ParamCount())
        return TRUE;
    SourceParams23();
    SourcePredicate();
    SourceInitialized();
    if (Current->Dest.Present)
    {
        if (Current->Dest.Type == D9SvRegAddress && Current->Opcode != D9SvOpMova)
            Fail(2, Ids->OnlyMova, "Only the mova instruction is allowed to write to the address register.");
        else if (Current->Dest.Type != D9SvRegAddress && Current->Opcode == D9SvOpMova)
            Fail(2, Ids->MovaDest, "mova instruction can only write to the address register.");
    }
    if (Model == 2)
    {
        if (!DestParam2())
            return TRUE;
    }
    else
    {
        DestParam3();
    }
    Definitions23();
    if (Model == 2)
        Declaration12();
    else
        Declaration3();
    Arithmetic();
    Ports23();
    if (!FlowControl())
        return FALSE;
    CountSlots();
    return TRUE;
}

VOID
D9VsChecker::CheckEnd()
{
    if (Model == 1)
    {
        CountSlotsEnd();
        TexCoordGaps();
        PositionWritten();
        LogOutputs12();
    }
    else
    {
        if (Model == 3)
            Declaration3();
        CountSlotsEnd();
        FlowControlEnd();
        if (Model == 2)
            LogOutputs12();
        else
            LogOutputs3();
    }
    if (Errors != 0)
    {
        Params->HardwareCapable = FALSE;
        Params->SoftwareCapable = FALSE;
    }
}
