/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     TrueType instruction interpreter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "hinter.hpp"

#include <cmath>

/* Unit vectors are 2.14 */
constexpr INT32 RxOne14 = 0x4000;
constexpr RxF26Dot6 RxOnePixel = 64;

/* Point flags inside a zone */
constexpr BYTE RxTouchedX = 0x01;
constexpr BYTE RxTouchedY = 0x02;
constexpr BYTE RxOnCurve = 0x04;

/* Round states, numbered as the state switches set them */
constexpr UINT32 RxRoundHalfGrid = 0;
constexpr UINT32 RxRoundGrid = 1;
constexpr UINT32 RxRoundDoubleGrid = 2;
constexpr UINT32 RxRoundOff = 3;
constexpr UINT32 RxRoundUpGrid = 4;
constexpr UINT32 RxRoundDownGrid = 5;
constexpr UINT32 RxRoundSuper = 6;
constexpr UINT32 RxRoundSuper45 = 7;

/* INSTCTRL selectors */
constexpr UINT32 RxInhibitGridFit = 0x1;
constexpr UINT32 RxIgnorePrepState = 0x2;
constexpr UINT32 RxNativeClearType = 0x4;

/* A runaway program stops here and the glyph is drawn unhinted */
constexpr ULONG RxInstructionLimit = 1000000;
constexpr ULONG RxCallDepthLimit = 64;
constexpr UINT32 RxPhantomCount = 4;

/* Composite component flags the hinter cares about */
constexpr USHORT RxCompWordArgs = 0x0001;
constexpr USHORT RxCompXyValues = 0x0002;
constexpr USHORT RxCompRoundXy = 0x0004;
constexpr USHORT RxCompScale = 0x0008;
constexpr USHORT RxCompMore = 0x0020;
constexpr USHORT RxCompXyScale = 0x0040;
constexpr USHORT RxCompTwoByTwo = 0x0080;
constexpr USHORT RxCompInstructions = 0x0100;
constexpr USHORT RxCompUseMetrics = 0x0200;
constexpr USHORT RxCompScaledOffset = 0x0800;

#define RX_HINT_FAIL E_FAIL

static
INT32
RxMulDiv(
    _In_ INT64 A,
    _In_ INT64 B,
    _In_ INT64 C)
{
    INT64 Product;
    BOOLEAN Negative = FALSE;

    if (C == 0)
        return (A * B >= 0) ? MAXINT32 : MININT32;

    if (A < 0)
    {
        A = -A;
        Negative = !Negative;
    }
    if (B < 0)
    {
        B = -B;
        Negative = !Negative;
    }
    if (C < 0)
    {
        C = -C;
        Negative = !Negative;
    }

    Product = (A * B + C / 2) / C;
    return static_cast<INT32>(Negative ? -Product : Product);
}

static
INT32
RxMulDivTruncate(
    _In_ INT64 A,
    _In_ INT64 B,
    _In_ INT64 C)
{
    INT64 Product;
    BOOLEAN Negative = FALSE;

    if (C == 0)
        return (A * B >= 0) ? MAXINT32 : MININT32;

    if (A < 0)
    {
        A = -A;
        Negative = !Negative;
    }
    if (B < 0)
    {
        B = -B;
        Negative = !Negative;
    }
    if (C < 0)
    {
        C = -C;
        Negative = !Negative;
    }

    Product = (A * B) / C;
    return static_cast<INT32>(Negative ? -Product : Product);
}

/* 16.16 scale of a font unit value, rounded */
static
RxF26Dot6
RxScale(
    _In_ INT32 Value,
    _In_ INT32 Scale)
{
    INT64 Product = static_cast<INT64>(Value) * Scale;

    return static_cast<RxF26Dot6>(Product >= 0 ? (Product + 0x8000) >> 16 : -((-Product + 0x8000) >> 16));
}

static
RxF26Dot6
RxRoundToGrid(
    _In_ RxF26Dot6 Value)
{
    return Value >= 0 ? (Value + 32) & ~63 : -((-Value + 32) & ~63);
}

VOID
RxTrueTypeHinter::Zone::Resize(
    _In_ UINT32 Size)
{
    CurrentX.assign(Size, 0);
    CurrentY.assign(Size, 0);
    OriginalX.assign(Size, 0);
    OriginalY.assign(Size, 0);
    UnscaledX.assign(Size, 0);
    UnscaledY.assign(Size, 0);
    Flags.assign(Size, 0);
}

RxTrueTypeHinter::RxTrueTypeHinter() :
    Face(nullptr), FontReady(FALSE), SizeReady(FALSE), StackTop(0), State{}, DefaultState{}, ProjectionDot(RxOne14),
    Size{}, Where(Context::Font), NativeClearType(FALSE), DidIupX(FALSE), DidIupY(FALSE), InstructionBudget(0),
    GlyphDropout{}
{
}

RxTrueTypeHinter::~RxTrueTypeHinter()
{
    if (Face != nullptr)
        Face->Release();
}

HRESULT
RxTrueTypeHinter::Initialize(
    _In_ RxFaceTables* Tables)
{
    const RxFaceHeader& Header = Tables->Header();
    RxSpan Fpgm;
    RxSpan Prep;
    RxSpan CvtTable;
    HRESULT hr;

    Tables->AddRef();
    Face = Tables;

    Fpgm = Tables->Sfnt().Table(RxTag('f', 'p', 'g', 'm'));
    Prep = Tables->Sfnt().Table(RxTag('p', 'r', 'e', 'p'));
    CvtTable = Tables->Sfnt().Table(RxTag('c', 'v', 't', ' '));

    FontProgram.assign(Fpgm.Bytes(), Fpgm.Bytes() + Fpgm.Length());
    PrepProgram.assign(Prep.Bytes(), Prep.Bytes() + Prep.Length());

    OriginalCvt.resize(CvtTable.Length() / 2);
    for (UINT32 Index = 0; Index < OriginalCvt.size(); Index++)
        OriginalCvt[Index] = CvtTable.S16(Index * 2);

    /* Fonts often understate these; give the programs some room */
    Stack.resize(static_cast<SIZE_T>(Header.MaxStackElements) + 32);
    Storage.assign(Header.MaxStorage, 0);
    Functions.assign(Header.MaxFunctionDefs, Definition{});
    Instructions.clear();
    Zones[0].Resize(Header.MaxTwilightPoints);
    Cvt.assign(OriginalCvt.size(), 0);

    State = GraphicsState{};
    State.Projection = { RxOne14, 0 };
    State.Freedom = { RxOne14, 0 };
    State.Dual = { RxOne14, 0 };
    State.RoundState = RxRoundGrid;
    State.AutoFlip = TRUE;
    State.CvtCutIn = 68;
    State.DeltaBase = 9;
    State.DeltaShift = 3;
    State.MinimumDistance = RxOnePixel;
    State.Loop = 1;
    State.Zp0 = State.Zp1 = State.Zp2 = 1;
    DefaultState = State;
    UpdateProjectionDot();

    /* The font program sees no size; it only defines functions */
    Size = RxHintRequest{};
    Size.ScaleX = Size.ScaleY = 0x10000;
    Size.PpemX = Size.PpemY = Header.UnitsPerEm >> 6;

    hr = Execute(FontProgram.data(), static_cast<UINT32>(FontProgram.size()), Context::Font);
    FontReady = SUCCEEDED(hr);
    if (!FontReady)
        RX_TRACE(L"fpgm failed, glyphs of this face draw unhinted");
    return S_OK;
}

HRESULT
RxTrueTypeHinter::SetSize(
    _In_ const RxHintRequest& Request)
{
    HRESULT hr;

    SizeReady = FALSE;
    if (!FontReady)
        return S_OK;

    Size = Request;

    for (UINT32 Index = 0; Index < OriginalCvt.size(); Index++)
        Cvt[Index] = RxScale(OriginalCvt[Index], Size.ScaleY);

    std::fill(Storage.begin(), Storage.end(), 0);
    Zones[0].Resize(Face->Header().MaxTwilightPoints);

    State = GraphicsState{};
    State.Projection = { RxOne14, 0 };
    State.Freedom = { RxOne14, 0 };
    State.Dual = { RxOne14, 0 };
    State.RoundState = RxRoundGrid;
    State.AutoFlip = TRUE;
    State.CvtCutIn = 68;
    State.DeltaBase = 9;
    State.DeltaShift = 3;
    State.MinimumDistance = RxOnePixel;
    State.Loop = 1;
    State.Zp0 = State.Zp1 = State.Zp2 = 1;
    UpdateProjectionDot();
    NativeClearType = FALSE;

    hr = Execute(PrepProgram.data(), static_cast<UINT32>(PrepProgram.size()), Context::Prep);
    if (FAILED(hr))
    {
        RX_TRACE(L"prep failed at %u ppem, drawing unhinted", Size.PpemY);
        return S_OK;
    }

    NativeClearType = (State.InstructControl & RxNativeClearType) != 0;

    /* Glyph programs start from the state prep left, less the per call fields */
    DefaultState = State;
    DefaultState.Projection = { RxOne14, 0 };
    DefaultState.Freedom = { RxOne14, 0 };
    DefaultState.Dual = { RxOne14, 0 };
    DefaultState.Loop = 1;
    DefaultState.Rp0 = DefaultState.Rp1 = DefaultState.Rp2 = 0;
    DefaultState.Zp0 = DefaultState.Zp1 = DefaultState.Zp2 = 1;
    if (State.InstructControl & RxIgnorePrepState)
    {
        DefaultState.RoundState = RxRoundGrid;
        DefaultState.AutoFlip = TRUE;
        DefaultState.CvtCutIn = 68;
        DefaultState.SingleWidthCutIn = 0;
        DefaultState.SingleWidth = 0;
        DefaultState.DeltaBase = 9;
        DefaultState.DeltaShift = 3;
        DefaultState.MinimumDistance = RxOnePixel;
    }

    PrepCvt = Cvt;
    PrepStorage = Storage;
    SizeReady = TRUE;
    return S_OK;
}

/* Execution */

HRESULT
RxTrueTypeHinter::Execute(
    _In_reads_bytes_(Length) const BYTE* Code,
    _In_ UINT32 Length,
    _In_ Context Context)
{
    Program Current = { Code, Length, 0 };

    Where = Context;
    StackTop = 0;
    InstructionBudget = RxInstructionLimit;
    if (Length == 0)
        return S_OK;
    return Run(Current, 0);
}

HRESULT
RxTrueTypeHinter::Run(
    _Inout_ Program& Current,
    _In_ ULONG Depth)
{
    HRESULT hr;

    while (Current.Ip < Current.Length)
    {
        BYTE Opcode = Current.Code[Current.Ip++];

        if (InstructionBudget-- == 0)
            return RX_HINT_FAIL;

        /* ENDF closes the function being run */
        if (Opcode == 0x2D)
            return Depth != 0 ? S_OK : RX_HINT_FAIL;

        hr = Step(Current, Opcode, Depth);
        if (FAILED(hr))
            return hr;
    }
    return Depth == 0 ? S_OK : RX_HINT_FAIL;
}

BOOLEAN
RxTrueTypeHinter::Push(
    _In_ INT32 Value)
{
    if (StackTop >= Stack.size())
        return FALSE;
    Stack[StackTop++] = Value;
    return TRUE;
}

BOOLEAN
RxTrueTypeHinter::Pop(
    _Out_ INT32* Value)
{
    if (StackTop == 0)
    {
        *Value = 0;
        return FALSE;
    }
    *Value = Stack[--StackTop];
    return TRUE;
}

/* Skips the data bytes of a push while hunting for ELSE, EIF or ENDF */
BOOLEAN
RxTrueTypeHinter::SkipPush(
    _Inout_ Program& Current,
    _In_ BYTE Opcode)
{
    UINT32 Bytes = 0;

    if (Opcode == 0x40 || Opcode == 0x41)
    {
        if (Current.Ip >= Current.Length)
            return FALSE;
        Bytes = Current.Code[Current.Ip++];
        if (Opcode == 0x41)
            Bytes *= 2;
    }
    else if (Opcode >= 0xB0 && Opcode <= 0xB7)
    {
        Bytes = Opcode - 0xB0 + 1;
    }
    else if (Opcode >= 0xB8 && Opcode <= 0xBF)
    {
        Bytes = (Opcode - 0xB8 + 1) * 2;
    }

    if (Current.Ip + Bytes > Current.Length)
        return FALSE;
    Current.Ip += Bytes;
    return TRUE;
}

/* Moves past the rest of an IF block; StopAtElse lands just after a matching ELSE */
BOOLEAN
RxTrueTypeHinter::SkipBranch(
    _Inout_ Program& Current,
    _In_ BOOLEAN StopAtElse)
{
    ULONG Nesting = 1;

    while (Current.Ip < Current.Length)
    {
        BYTE Opcode = Current.Code[Current.Ip++];

        if (Opcode == 0x58)
        {
            Nesting++;
        }
        else if (Opcode == 0x59)
        {
            if (--Nesting == 0)
                return TRUE;
        }
        else if (Opcode == 0x1B)
        {
            if (Nesting == 1 && StopAtElse)
                return TRUE;
        }
        else if (!SkipPush(Current, Opcode))
        {
            return FALSE;
        }
    }
    return FALSE;
}

/* Geometry */

VOID
RxTrueTypeHinter::UpdateProjectionDot(VOID)
{
    INT64 Dot = static_cast<INT64>(State.Freedom.X) * State.Projection.X +
                static_cast<INT64>(State.Freedom.Y) * State.Projection.Y;

    ProjectionDot = static_cast<INT32>((Dot + 0x2000) >> 14);
    /* A freedom vector almost perpendicular to the projection vector would explode moves */
    if (ProjectionDot > -0x400 && ProjectionDot < 0x400)
        ProjectionDot = RxOne14;
}

RxF26Dot6
RxTrueTypeHinter::Project(
    _In_ RxF26Dot6 Dx,
    _In_ RxF26Dot6 Dy) const
{
    INT64 Dot = static_cast<INT64>(Dx) * State.Projection.X + static_cast<INT64>(Dy) * State.Projection.Y;

    return static_cast<RxF26Dot6>((Dot + 0x2000) >> 14);
}

RxF26Dot6
RxTrueTypeHinter::DualProject(
    _In_ RxF26Dot6 Dx,
    _In_ RxF26Dot6 Dy) const
{
    INT64 Dot = static_cast<INT64>(Dx) * State.Dual.X + static_cast<INT64>(Dy) * State.Dual.Y;

    return static_cast<RxF26Dot6>((Dot + 0x2000) >> 14);
}

RxF26Dot6
RxTrueTypeHinter::RoundWith(
    _In_ UINT32 RoundState,
    _In_ RxF26Dot6 Distance) const
{
    RxF26Dot6 Value;

    switch (RoundState)
    {
        case RxRoundHalfGrid:
            if (Distance >= 0)
                return (Distance & ~63) + 32;
            return -(((-Distance) & ~63) + 32);

        case RxRoundGrid:
            if (Distance >= 0)
            {
                Value = (Distance + 32) & ~63;
                return Value < 0 ? 0 : Value;
            }
            Value = -((-Distance + 32) & ~63);
            return Value > 0 ? 0 : Value;

        case RxRoundDoubleGrid:
            if (Distance >= 0)
            {
                Value = (Distance + 16) & ~31;
                return Value < 0 ? 0 : Value;
            }
            Value = -((-Distance + 16) & ~31);
            return Value > 0 ? 0 : Value;

        case RxRoundOff:
            return Distance;

        case RxRoundUpGrid:
            if (Distance >= 0)
                return (Distance + 63) & ~63;
            return -((-Distance + 63) & ~63);

        case RxRoundDownGrid:
            if (Distance >= 0)
                return Distance & ~63;
            return -((-Distance) & ~63);

        case RxRoundSuper:
        case RxRoundSuper45:
        {
            RxF26Dot6 Period = State.Period;

            if (Period == 0)
                return Distance;
            if (Distance >= 0)
            {
                Value = Distance - State.Phase + State.Threshold;
                Value = (Value >= 0) ? (Value / Period) * Period : 0;
                Value += State.Phase;
                return Value < 0 ? State.Phase : Value;
            }
            Value = -Distance - State.Phase + State.Threshold;
            Value = (Value >= 0) ? (Value / Period) * Period : 0;
            Value = -(Value + State.Phase);
            return Value > 0 ? -State.Phase : Value;
        }
    }
    return Distance;
}

RxF26Dot6
RxTrueTypeHinter::Round(
    _In_ RxF26Dot6 Distance) const
{
    return RoundWith(State.RoundState, Distance);
}

VOID
RxTrueTypeHinter::SetSuperRound(
    _In_ INT32 Selector,
    _In_ RxF26Dot6 GridPeriod)
{
    switch (Selector & 0xC0)
    {
        case 0x00:
            State.Period = GridPeriod / 2;
            break;
        case 0x40:
            State.Period = GridPeriod;
            break;
        case 0x80:
            State.Period = GridPeriod * 2;
            break;
        default:
            State.Period = GridPeriod;
            break;
    }

    switch (Selector & 0x30)
    {
        case 0x00:
            State.Phase = 0;
            break;
        case 0x10:
            State.Phase = State.Period / 4;
            break;
        case 0x20:
            State.Phase = State.Period / 2;
            break;
        case 0x30:
            State.Phase = State.Period * 3 / 4;
            break;
    }

    if ((Selector & 0x0F) == 0)
        State.Threshold = State.Period - 1;
    else
        State.Threshold = ((Selector & 0x0F) - 4) * State.Period / 8;
}

BOOLEAN
RxTrueTypeHinter::ValidPoint(
    _In_ UINT32 ZoneIndex,
    _In_ UINT32 Point) const
{
    return ZoneIndex <= 1 && Point < Zones[ZoneIndex].Count();
}

RxF26Dot6
RxTrueTypeHinter::CurrentProjection(
    _In_ UINT32 ZoneIndex,
    _In_ UINT32 Point) const
{
    return Project(Zones[ZoneIndex].CurrentX[Point], Zones[ZoneIndex].CurrentY[Point]);
}

RxF26Dot6
RxTrueTypeHinter::OriginalProjection(
    _In_ UINT32 ZoneIndex,
    _In_ UINT32 Point) const
{
    return DualProject(Zones[ZoneIndex].OriginalX[Point], Zones[ZoneIndex].OriginalY[Point]);
}

VOID
RxTrueTypeHinter::MovePoint(
    _In_ UINT32 ZoneIndex,
    _In_ UINT32 Point,
    _In_ RxF26Dot6 Distance,
    _In_ BOOLEAN Touch)
{
    Zone& Target = Zones[ZoneIndex];
    BOOLEAN Locked = Size.IgnoreXMoves && !NativeClearType && DidIupX && DidIupY;

    if (State.Freedom.X != 0)
    {
        /* ClearType compatibility keeps glyph points where the outline put them horizontally */
        if (!(Size.IgnoreXMoves && !NativeClearType && ZoneIndex == 1) && !Locked)
        {
            if (State.Freedom.X == RxOne14 && State.Projection.X == RxOne14)
                Target.CurrentX[Point] += Distance;
            else
                Target.CurrentX[Point] += RxMulDiv(Distance, State.Freedom.X, ProjectionDot);
        }
        if (Touch)
            Target.Flags[Point] |= RxTouchedX;
    }

    if (State.Freedom.Y != 0)
    {
        if (!Locked)
        {
            if (State.Freedom.Y == RxOne14 && State.Projection.Y == RxOne14)
                Target.CurrentY[Point] += Distance;
            else
                Target.CurrentY[Point] += RxMulDiv(Distance, State.Freedom.Y, ProjectionDot);
        }
        if (Touch)
            Target.Flags[Point] |= RxTouchedY;
    }
}

VOID
RxTrueTypeHinter::MoveOriginal(
    _In_ UINT32 ZoneIndex,
    _In_ UINT32 Point,
    _In_ RxF26Dot6 Distance)
{
    Zone& Target = Zones[ZoneIndex];

    if (State.Freedom.X != 0)
        Target.OriginalX[Point] += RxMulDiv(Distance, State.Freedom.X, ProjectionDot);
    if (State.Freedom.Y != 0)
        Target.OriginalY[Point] += RxMulDiv(Distance, State.Freedom.Y, ProjectionDot);
}

BOOLEAN
RxTrueTypeHinter::UnitVector(
    _In_ RxF26Dot6 Dx,
    _In_ RxF26Dot6 Dy,
    _Out_ Vector* Result) const
{
    double Length = std::sqrt(static_cast<double>(Dx) * Dx + static_cast<double>(Dy) * Dy);

    if (Length == 0.0)
    {
        Result->X = RxOne14;
        Result->Y = 0;
        return FALSE;
    }

    Result->X = static_cast<INT32>(std::lround(Dx * RxOne14 / Length));
    Result->Y = static_cast<INT32>(std::lround(Dy * RxOne14 / Length));
    return TRUE;
}

/* Control values are scaled for y; other projections stretch them by the aspect */
RxF26Dot6
RxTrueTypeHinter::ScaledCvt(
    _In_ UINT32 Index) const
{
    double Ratio;
    double X;
    double Y;

    if (Size.ScaleX == Size.ScaleY || Size.ScaleY == 0)
        return Cvt[Index];

    X = static_cast<double>(State.Projection.X) * Size.ScaleX;
    Y = static_cast<double>(State.Projection.Y) * Size.ScaleY;
    Ratio = std::sqrt(X * X + Y * Y) / (static_cast<double>(RxOne14) * Size.ScaleY);
    return static_cast<RxF26Dot6>(std::lround(Cvt[Index] * Ratio));
}

RxF26Dot6
RxTrueTypeHinter::CurrentPpem(VOID) const
{
    if (State.Projection.Y == 0)
        return Size.PpemX;
    if (State.Projection.X == 0)
        return Size.PpemY;
    return static_cast<RxF26Dot6>(std::lround(std::sqrt(
        std::pow(Size.PpemX * State.Projection.X / static_cast<double>(RxOne14), 2) +
        std::pow(Size.PpemY * State.Projection.Y / static_cast<double>(RxOne14), 2))));
}

/* Instruction groups */

HRESULT
RxTrueTypeHinter::DoShift(
    _In_ BYTE Opcode)
{
    UINT32 RefZone;
    UINT32 RefPoint;
    RxF26Dot6 Delta;
    INT32 Value;

    /* SHP, SHC and SHZ measure how far the reference point moved */
    if (Opcode & 1)
    {
        RefZone = State.Zp0;
        RefPoint = State.Rp1;
    }
    else
    {
        RefZone = State.Zp1;
        RefPoint = State.Rp2;
    }

    if (!ValidPoint(RefZone, RefPoint))
        return RX_HINT_FAIL;

    Delta = Project(Zones[RefZone].CurrentX[RefPoint] - Zones[RefZone].OriginalX[RefPoint],
                    Zones[RefZone].CurrentY[RefPoint] - Zones[RefZone].OriginalY[RefPoint]);

    switch (Opcode & ~1)
    {
        case 0x32:
            while (State.Loop-- > 0)
            {
                if (!Pop(&Value) || !ValidPoint(State.Zp2, Value))
                    return RX_HINT_FAIL;
                MovePoint(State.Zp2, Value, Delta, TRUE);
            }
            State.Loop = 1;
            break;

        case 0x34:
        {
            const Zone& Target = Zones[State.Zp2];
            UINT32 Start;
            UINT32 End;

            if (!Pop(&Value) || Value < 0 || static_cast<SIZE_T>(Value) >= Target.ContourEnds.size())
                return RX_HINT_FAIL;

            Start = Value == 0 ? 0 : Target.ContourEnds[Value - 1] + 1u;
            End = Target.ContourEnds[Value];
            for (UINT32 Point = Start; Point <= End && Point < Target.Count(); Point++)
            {
                if (RefZone == State.Zp2 && Point == RefPoint)
                    continue;
                MovePoint(State.Zp2, Point, Delta, TRUE);
            }
            break;
        }

        case 0x36:
        {
            UINT32 Limit;

            if (!Pop(&Value) || Value < 0 || Value > 1)
                return RX_HINT_FAIL;

            Limit = Zones[Value].Count();
            if (Value == 1 && Limit >= RxPhantomCount)
                Limit -= RxPhantomCount;
            for (UINT32 Point = 0; Point < Limit; Point++)
            {
                if (RefZone == static_cast<UINT32>(Value) && Point == RefPoint)
                    continue;
                MovePoint(Value, Point, Delta, FALSE);
            }
            break;
        }
    }
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoDelta(
    _In_ BYTE Opcode)
{
    INT32 Count;
    INT32 Base = State.DeltaBase;
    BOOLEAN IsCvt = (Opcode >= 0x73 && Opcode <= 0x75);
    RxF26Dot6 Ppem;

    if (Opcode == 0x71 || Opcode == 0x74)
        Base += 16;
    else if (Opcode == 0x72 || Opcode == 0x75)
        Base += 32;

    if (!Pop(&Count) || Count < 0)
        return RX_HINT_FAIL;

    Ppem = CurrentPpem();
    while (Count-- > 0)
    {
        INT32 Target;
        INT32 Argument;
        INT32 Step;
        RxF26Dot6 Delta;

        if (!Pop(&Target) || !Pop(&Argument))
            return RX_HINT_FAIL;

        if (Base + ((Argument >> 4) & 0xF) != Ppem)
            continue;

        Step = Argument & 0xF;
        Step = Step >= 8 ? Step - 7 : Step - 8;
        Delta = Step * (RxOnePixel >> State.DeltaShift);

        if (IsCvt)
        {
            if (Target < 0 || static_cast<SIZE_T>(Target) >= Cvt.size())
                return RX_HINT_FAIL;
            Cvt[Target] += Delta;
            continue;
        }

        if (!ValidPoint(State.Zp0, Target))
            return RX_HINT_FAIL;

        /* Compatibility mode only honors deltas on points already placed vertically, before IUP */
        if (Size.IgnoreXMoves && !NativeClearType)
        {
            if (DidIupX && DidIupY)
                continue;
            if (!(Zones[State.Zp0].Flags[Target] & RxTouchedY))
                continue;
        }
        MovePoint(State.Zp0, Target, Delta, TRUE);
    }
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoMoveDirect(
    _In_ BYTE Opcode)
{
    INT32 Point;
    Zone& Reference = Zones[State.Zp0];
    RxF26Dot6 Original;
    RxF26Dot6 Current;
    RxF26Dot6 Distance;

    if (!Pop(&Point) || !ValidPoint(State.Zp1, Point) || !ValidPoint(State.Zp0, State.Rp0))
        return RX_HINT_FAIL;

    Original = DualProject(Zones[State.Zp1].OriginalX[Point] - Reference.OriginalX[State.Rp0],
                           Zones[State.Zp1].OriginalY[Point] - Reference.OriginalY[State.Rp0]);

    if (std::abs(Original - State.SingleWidth) < State.SingleWidthCutIn)
        Original = Original >= 0 ? State.SingleWidth : -State.SingleWidth;

    Distance = (Opcode & 0x04) ? Round(Original) : Original;

    if (Opcode & 0x08)
    {
        if (Original >= 0)
        {
            if (Distance < State.MinimumDistance)
                Distance = State.MinimumDistance;
        }
        else if (Distance > -State.MinimumDistance)
        {
            Distance = -State.MinimumDistance;
        }
    }

    Current = Project(Zones[State.Zp1].CurrentX[Point] - Reference.CurrentX[State.Rp0],
                      Zones[State.Zp1].CurrentY[Point] - Reference.CurrentY[State.Rp0]);
    MovePoint(State.Zp1, Point, Distance - Current, TRUE);

    State.Rp1 = State.Rp0;
    State.Rp2 = Point;
    if (Opcode & 0x10)
        State.Rp0 = Point;
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoMoveIndirect(
    _In_ BYTE Opcode)
{
    INT32 Index;
    INT32 Point;
    RxF26Dot6 CvtDistance;
    RxF26Dot6 Original;
    RxF26Dot6 Current;
    RxF26Dot6 Distance;

    if (!Pop(&Index) || !Pop(&Point) || !ValidPoint(State.Zp1, Point) || !ValidPoint(State.Zp0, State.Rp0))
        return RX_HINT_FAIL;

    if (Index == -1 || Index == 0xFFFF)
        CvtDistance = 0;
    else if (Index < 0 || static_cast<SIZE_T>(Index) >= Cvt.size())
        return RX_HINT_FAIL;
    else
        CvtDistance = ScaledCvt(Index);

    if (std::abs(CvtDistance - State.SingleWidth) < State.SingleWidthCutIn)
        CvtDistance = CvtDistance >= 0 ? State.SingleWidth : -State.SingleWidth;

    /* A twilight point gets its original position from the reference point and the distance */
    if (State.Zp1 == 0)
    {
        Zone& Twilight = Zones[0];

        Twilight.OriginalX[Point] = Zones[State.Zp0].OriginalX[State.Rp0] +
                                    RxMulDiv(CvtDistance, State.Freedom.X, RxOne14);
        Twilight.OriginalY[Point] = Zones[State.Zp0].OriginalY[State.Rp0] +
                                    RxMulDiv(CvtDistance, State.Freedom.Y, RxOne14);
        Twilight.CurrentX[Point] = Twilight.OriginalX[Point];
        Twilight.CurrentY[Point] = Twilight.OriginalY[Point];
    }

    Original = DualProject(Zones[State.Zp1].OriginalX[Point] - Zones[State.Zp0].OriginalX[State.Rp0],
                           Zones[State.Zp1].OriginalY[Point] - Zones[State.Zp0].OriginalY[State.Rp0]);
    Current = Project(Zones[State.Zp1].CurrentX[Point] - Zones[State.Zp0].CurrentX[State.Rp0],
                      Zones[State.Zp1].CurrentY[Point] - Zones[State.Zp0].CurrentY[State.Rp0]);

    if (State.AutoFlip && ((Original ^ CvtDistance) < 0))
        CvtDistance = -CvtDistance;

    if (Opcode & 0x04)
    {
        if (State.Zp0 == State.Zp1 && std::abs(CvtDistance - Original) > State.CvtCutIn)
            CvtDistance = Original;
        Distance = Round(CvtDistance);
    }
    else
    {
        Distance = CvtDistance;
    }

    if (Opcode & 0x08)
    {
        if (Original >= 0)
        {
            if (Distance < State.MinimumDistance)
                Distance = State.MinimumDistance;
        }
        else if (Distance > -State.MinimumDistance)
        {
            Distance = -State.MinimumDistance;
        }
    }

    MovePoint(State.Zp1, Point, Distance - Current, TRUE);

    State.Rp1 = State.Rp0;
    State.Rp2 = Point;
    if (Opcode & 0x10)
        State.Rp0 = Point;
    return S_OK;
}

/* Spreads the moves of touched points over the untouched points of each contour */
HRESULT
RxTrueTypeHinter::DoInterpolateUntouched(
    _In_ BOOLEAN XAxis)
{
    Zone& Glyph = Zones[1];
    BYTE Mask = XAxis ? RxTouchedX : RxTouchedY;
    std::vector<RxF26Dot6>& Current = XAxis ? Glyph.CurrentX : Glyph.CurrentY;
    std::vector<RxF26Dot6>& Original = XAxis ? Glyph.OriginalX : Glyph.OriginalY;
    std::vector<INT32>& Unscaled = XAxis ? Glyph.UnscaledX : Glyph.UnscaledY;
    UINT32 Start = 0;

    if (XAxis)
        DidIupX = TRUE;
    else
        DidIupY = TRUE;

    /* Compatibility mode leaves x where the outline put it */
    if (XAxis && Size.IgnoreXMoves && !NativeClearType)
        return S_OK;

    for (USHORT End : Glyph.ContourEnds)
    {
        UINT32 First = MAXUINT32;
        UINT32 Point;

        if (End >= Glyph.Count())
            return RX_HINT_FAIL;

        for (Point = Start; Point <= End; Point++)
        {
            if (Glyph.Flags[Point] & Mask)
            {
                First = Point;
                break;
            }
        }

        if (First != MAXUINT32)
        {
            UINT32 Previous = First;
            UINT32 Index = First;

            do
            {
                UINT32 Next = (Index == End) ? Start : Index + 1;

                while (!(Glyph.Flags[Next] & Mask))
                    Next = (Next == End) ? Start : Next + 1;

                /* Untouched run strictly between Previous and Next */
                {
                    INT32 UnscaledA = Unscaled[Previous];
                    INT32 UnscaledB = Unscaled[Next];
                    RxF26Dot6 OriginalA = Original[Previous];
                    RxF26Dot6 OriginalB = Original[Next];
                    RxF26Dot6 CurrentA = Current[Previous];
                    RxF26Dot6 CurrentB = Current[Next];
                    UINT32 Walk = (Previous == End) ? Start : Previous + 1;

                    if (UnscaledA > UnscaledB || (UnscaledA == UnscaledB && OriginalA > OriginalB))
                    {
                        std::swap(UnscaledA, UnscaledB);
                        std::swap(OriginalA, OriginalB);
                        std::swap(CurrentA, CurrentB);
                    }

                    while (Walk != Next)
                    {
                        INT32 Value = Unscaled[Walk];

                        if (Value <= UnscaledA)
                            Current[Walk] = Original[Walk] + (CurrentA - OriginalA);
                        else if (Value >= UnscaledB)
                            Current[Walk] = Original[Walk] + (CurrentB - OriginalB);
                        else
                            Current[Walk] = CurrentA + RxMulDiv(Value - UnscaledA, CurrentB - CurrentA,
                                                                UnscaledB - UnscaledA);
                        Walk = (Walk == End) ? Start : Walk + 1;
                    }
                }

                Previous = Next;
                Index = Next;
            } while (Index != First);
        }

        Start = End + 1u;
    }
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoInterpolatePoints(VOID)
{
    RxF26Dot6 OriginalRange;
    RxF26Dot6 CurrentRange;
    RxF26Dot6 OriginalBase;
    RxF26Dot6 CurrentBase;
    INT32 Point;

    if (!ValidPoint(State.Zp0, State.Rp1) || !ValidPoint(State.Zp1, State.Rp2))
        return RX_HINT_FAIL;

    OriginalBase = OriginalProjection(State.Zp0, State.Rp1);
    CurrentBase = CurrentProjection(State.Zp0, State.Rp1);
    OriginalRange = OriginalProjection(State.Zp1, State.Rp2) - OriginalBase;
    CurrentRange = CurrentProjection(State.Zp1, State.Rp2) - CurrentBase;

    while (State.Loop-- > 0)
    {
        RxF26Dot6 Original;
        RxF26Dot6 Wanted;
        RxF26Dot6 Current;

        if (!Pop(&Point) || !ValidPoint(State.Zp2, Point))
            return RX_HINT_FAIL;

        Original = OriginalProjection(State.Zp2, Point) - OriginalBase;
        Current = CurrentProjection(State.Zp2, Point) - CurrentBase;
        Wanted = OriginalRange != 0 ? RxMulDiv(Original, CurrentRange, OriginalRange) : Original;
        MovePoint(State.Zp2, Point, Wanted - Current, TRUE);
    }
    State.Loop = 1;
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoIntersect(VOID)
{
    INT32 B1;
    INT32 B0;
    INT32 A1;
    INT32 A0;
    INT32 Point;
    double Ax;
    double Ay;
    double Bx;
    double By;
    double Denominator;

    if (!Pop(&B1) || !Pop(&B0) || !Pop(&A1) || !Pop(&A0) || !Pop(&Point))
        return RX_HINT_FAIL;
    if (!ValidPoint(State.Zp0, B0) || !ValidPoint(State.Zp0, B1) || !ValidPoint(State.Zp1, A0) ||
        !ValidPoint(State.Zp1, A1) || !ValidPoint(State.Zp2, Point))
        return RX_HINT_FAIL;

    const Zone& LineA = Zones[State.Zp1];
    const Zone& LineB = Zones[State.Zp0];
    Zone& Target = Zones[State.Zp2];

    Ax = static_cast<double>(LineA.CurrentX[A1]) - LineA.CurrentX[A0];
    Ay = static_cast<double>(LineA.CurrentY[A1]) - LineA.CurrentY[A0];
    Bx = static_cast<double>(LineB.CurrentX[B1]) - LineB.CurrentX[B0];
    By = static_cast<double>(LineB.CurrentY[B1]) - LineB.CurrentY[B0];
    Denominator = Ax * By - Ay * Bx;

    if (std::fabs(Denominator) < 1.0)
    {
        /* Parallel lines meet at the middle of the four points */
        Target.CurrentX[Point] = (LineA.CurrentX[A0] + LineA.CurrentX[A1] + LineB.CurrentX[B0] + LineB.CurrentX[B1]) / 4;
        Target.CurrentY[Point] = (LineA.CurrentY[A0] + LineA.CurrentY[A1] + LineB.CurrentY[B0] + LineB.CurrentY[B1]) / 4;
    }
    else
    {
        double Dx = static_cast<double>(LineB.CurrentX[B0]) - LineA.CurrentX[A0];
        double Dy = static_cast<double>(LineB.CurrentY[B0]) - LineA.CurrentY[A0];
        double T = (Dx * By - Dy * Bx) / Denominator;

        Target.CurrentX[Point] = static_cast<RxF26Dot6>(std::lround(LineA.CurrentX[A0] + T * Ax));
        Target.CurrentY[Point] = static_cast<RxF26Dot6>(std::lround(LineA.CurrentY[A0] + T * Ay));
    }

    Target.Flags[Point] |= RxTouchedX | RxTouchedY;
    return S_OK;
}

HRESULT
RxTrueTypeHinter::DoGetInfo(VOID)
{
    INT32 Selector;
    INT32 Result = 0;

    if (!Pop(&Selector))
        return RX_HINT_FAIL;

    if (Selector & 0x1)
        Result |= Size.InfoVersion & 0xFF;
    if ((Selector & 0x2) && Size.Rotated)
        Result |= 1 << 8;
    if ((Selector & 0x4) && Size.Stretched)
        Result |= 1 << 9;
    if ((Selector & 0x8) && Face->Sfnt().HasTable(RxTag('f', 'v', 'a', 'r')))
        Result |= 1 << 10;

    /* Selector bits 5 to 12 ask for result bits 12 to 19 */
    for (UINT32 Bit = 5; Bit <= 12; Bit++)
    {
        if (Selector & (1 << Bit))
            Result |= Size.InfoFlags & (1u << (Bit + 7));
    }

    return Push(Result) ? S_OK : RX_HINT_FAIL;
}

HRESULT
RxTrueTypeHinter::Step(
    _Inout_ Program& Current,
    _In_ BYTE Opcode,
    _In_ ULONG Depth)
{
    INT32 A;
    INT32 B;
    INT32 C;

    /* Pushes */
    if (Opcode >= 0xB0 && Opcode <= 0xB7)
    {
        UINT32 Count = Opcode - 0xB0 + 1;

        if (Current.Ip + Count > Current.Length)
            return RX_HINT_FAIL;
        while (Count-- != 0)
        {
            if (!Push(Current.Code[Current.Ip++]))
                return RX_HINT_FAIL;
        }
        return S_OK;
    }

    if (Opcode >= 0xB8 && Opcode <= 0xBF)
    {
        UINT32 Count = Opcode - 0xB8 + 1;

        if (Current.Ip + Count * 2 > Current.Length)
            return RX_HINT_FAIL;
        while (Count-- != 0)
        {
            SHORT Word = static_cast<SHORT>((Current.Code[Current.Ip] << 8) | Current.Code[Current.Ip + 1]);

            Current.Ip += 2;
            if (!Push(Word))
                return RX_HINT_FAIL;
        }
        return S_OK;
    }

    if (Opcode >= 0xC0 && Opcode <= 0xDF)
        return DoMoveDirect(Opcode);
    if (Opcode >= 0xE0)
        return DoMoveIndirect(Opcode);

    switch (Opcode)
    {
        /* Vectors */
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        {
            Vector Axis = (Opcode & 1) ? Vector{ RxOne14, 0 } : Vector{ 0, RxOne14 };

            if (Opcode <= 0x03)
            {
                State.Projection = Axis;
                State.Dual = Axis;
            }
            if (Opcode <= 0x01 || Opcode >= 0x04)
                State.Freedom = Axis;
            UpdateProjectionDot();
            return S_OK;
        }

        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        case 0x86:
        case 0x87:
        {
            Vector Line;
            RxF26Dot6 Dx;
            RxF26Dot6 Dy;

            if (!Pop(&A) || !Pop(&B) || !ValidPoint(State.Zp2, B) || !ValidPoint(State.Zp1, A))
                return RX_HINT_FAIL;

            /* A is p2 in zp1, B is p1 in zp2; the vector runs from p2 to p1 */
            Dx = Zones[State.Zp2].CurrentX[B] - Zones[State.Zp1].CurrentX[A];
            Dy = Zones[State.Zp2].CurrentY[B] - Zones[State.Zp1].CurrentY[A];
            if (Opcode & 1)
            {
                RxF26Dot6 Swap = Dx;

                Dx = -Dy;
                Dy = Swap;
            }
            UnitVector(Dx, Dy, &Line);

            if (Opcode == 0x08 || Opcode == 0x09)
            {
                State.Freedom = Line;
            }
            else
            {
                State.Projection = Line;
                State.Dual = Line;
                if (Opcode >= 0x86)
                {
                    Dx = Zones[State.Zp2].OriginalX[B] - Zones[State.Zp1].OriginalX[A];
                    Dy = Zones[State.Zp2].OriginalY[B] - Zones[State.Zp1].OriginalY[A];
                    if (Opcode & 1)
                    {
                        RxF26Dot6 Swap = Dx;

                        Dx = -Dy;
                        Dy = Swap;
                    }
                    UnitVector(Dx, Dy, &State.Dual);
                }
            }
            UpdateProjectionDot();
            return S_OK;
        }

        case 0x0A:
        case 0x0B:
        {
            Vector Value;

            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            UnitVector(static_cast<SHORT>(A), static_cast<SHORT>(B), &Value);
            if (Opcode == 0x0A)
            {
                State.Projection = Value;
                State.Dual = Value;
            }
            else
            {
                State.Freedom = Value;
            }
            UpdateProjectionDot();
            return S_OK;
        }

        case 0x0C:
            return (Push(State.Projection.X) && Push(State.Projection.Y)) ? S_OK : RX_HINT_FAIL;

        case 0x0D:
            return (Push(State.Freedom.X) && Push(State.Freedom.Y)) ? S_OK : RX_HINT_FAIL;

        case 0x0E:
            State.Freedom = State.Projection;
            UpdateProjectionDot();
            return S_OK;

        case 0x0F:
            return DoIntersect();

        /* Reference points and zones */
        case 0x10:
        case 0x11:
        case 0x12:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            (Opcode == 0x10 ? State.Rp0 : Opcode == 0x11 ? State.Rp1 : State.Rp2) = A;
            return S_OK;

        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
            if (!Pop(&A) || A < 0 || A > 1)
                return RX_HINT_FAIL;
            if (Opcode == 0x13 || Opcode == 0x16)
                State.Zp0 = A;
            if (Opcode == 0x14 || Opcode == 0x16)
                State.Zp1 = A;
            if (Opcode == 0x15 || Opcode == 0x16)
                State.Zp2 = A;
            return S_OK;

        case 0x17:
            if (!Pop(&A) || A < 0)
                return RX_HINT_FAIL;
            State.Loop = std::min(A, 0xFFFF);
            return S_OK;

        /* Rounding */
        case 0x18:
            State.RoundState = RxRoundGrid;
            return S_OK;
        case 0x19:
            State.RoundState = RxRoundHalfGrid;
            return S_OK;
        case 0x3D:
            State.RoundState = RxRoundDoubleGrid;
            return S_OK;
        case 0x7A:
            State.RoundState = RxRoundOff;
            return S_OK;
        case 0x7C:
            State.RoundState = RxRoundUpGrid;
            return S_OK;
        case 0x7D:
            State.RoundState = RxRoundDownGrid;
            return S_OK;
        case 0x76:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            SetSuperRound(A, RxOnePixel);
            State.RoundState = RxRoundSuper;
            return S_OK;
        case 0x77:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            /* Grid period of sqrt(2)/2 pixel */
            SetSuperRound(A, 45);
            State.RoundState = RxRoundSuper45;
            return S_OK;

        case 0x1A:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.MinimumDistance = A;
            return S_OK;

        case 0x1B:
            return SkipBranch(Current, FALSE) ? S_OK : RX_HINT_FAIL;

        case 0x1C:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            A += static_cast<INT32>(Current.Ip) - 1;
            if (A < 0 || static_cast<UINT32>(A) > Current.Length)
                return RX_HINT_FAIL;
            Current.Ip = A;
            return S_OK;

        case 0x1D:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.CvtCutIn = A;
            return S_OK;

        case 0x1E:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.SingleWidthCutIn = A;
            return S_OK;

        case 0x1F:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.SingleWidth = RxScale(A, Size.ScaleY);
            return S_OK;

        /* Stack */
        case 0x20:
            if (!Pop(&A) || !Push(A) || !Push(A))
                return RX_HINT_FAIL;
            return S_OK;

        case 0x21:
            return Pop(&A) ? S_OK : RX_HINT_FAIL;

        case 0x22:
            StackTop = 0;
            return S_OK;

        case 0x23:
            if (!Pop(&B) || !Pop(&A) || !Push(B) || !Push(A))
                return RX_HINT_FAIL;
            return S_OK;

        case 0x24:
            return Push(static_cast<INT32>(StackTop)) ? S_OK : RX_HINT_FAIL;

        case 0x25:
            if (!Pop(&A) || A <= 0 || static_cast<UINT32>(A) > StackTop)
                return RX_HINT_FAIL;
            return Push(Stack[StackTop - A]) ? S_OK : RX_HINT_FAIL;

        case 0x26:
        {
            UINT32 Index;

            if (!Pop(&A) || A <= 0 || static_cast<UINT32>(A) > StackTop)
                return RX_HINT_FAIL;
            Index = StackTop - A;
            B = Stack[Index];
            memmove(&Stack[Index], &Stack[Index + 1], (StackTop - Index - 1) * sizeof(INT32));
            Stack[StackTop - 1] = B;
            return S_OK;
        }

        case 0x8A:
            if (StackTop < 3)
                return RX_HINT_FAIL;
            A = Stack[StackTop - 3];
            Stack[StackTop - 3] = Stack[StackTop - 2];
            Stack[StackTop - 2] = Stack[StackTop - 1];
            Stack[StackTop - 1] = A;
            return S_OK;

        /* Points */
        case 0x27:
        {
            RxF26Dot6 Distance;

            if (!Pop(&B) || !Pop(&A) || !ValidPoint(State.Zp1, A) || !ValidPoint(State.Zp0, B))
                return RX_HINT_FAIL;
            Distance = Project(Zones[State.Zp0].CurrentX[B] - Zones[State.Zp1].CurrentX[A],
                               Zones[State.Zp0].CurrentY[B] - Zones[State.Zp1].CurrentY[A]) / 2;
            MovePoint(State.Zp1, A, Distance, TRUE);
            MovePoint(State.Zp0, B, -Distance, TRUE);
            return S_OK;
        }

        case 0x29:
            if (!Pop(&A) || !ValidPoint(State.Zp0, A))
                return RX_HINT_FAIL;
            if (State.Freedom.X != 0)
                Zones[State.Zp0].Flags[A] &= ~RxTouchedX;
            if (State.Freedom.Y != 0)
                Zones[State.Zp0].Flags[A] &= ~RxTouchedY;
            return S_OK;

        /* Functions */
        case 0x2A:
        case 0x2B:
        {
            INT32 Count = 1;
            Definition* Function;

            if (!Pop(&A))
                return RX_HINT_FAIL;
            if (Opcode == 0x2A && !Pop(&Count))
                return RX_HINT_FAIL;
            if (A < 0 || static_cast<SIZE_T>(A) >= Functions.size() || !Functions[A].Active)
                return RX_HINT_FAIL;
            if (Depth >= RxCallDepthLimit)
                return RX_HINT_FAIL;

            Function = &Functions[A];
            while (Count-- > 0)
            {
                Program Call = { Function->Start, Function->Length, 0 };
                HRESULT hr = Run(Call, Depth + 1);

                if (FAILED(hr))
                    return hr;
            }
            return S_OK;
        }

        case 0x2C:
        case 0x89:
        {
            UINT32 Start;

            if (Where == Context::Glyph)
                return RX_HINT_FAIL;
            if (!Pop(&A))
                return RX_HINT_FAIL;

            Start = Current.Ip;
            while (Current.Ip < Current.Length && Current.Code[Current.Ip] != 0x2D)
            {
                BYTE Skip = Current.Code[Current.Ip++];

                if (Skip == 0x2C || Skip == 0x89)
                    return RX_HINT_FAIL;
                if (!SkipPush(Current, Skip))
                    return RX_HINT_FAIL;
            }
            if (Current.Ip >= Current.Length)
                return RX_HINT_FAIL;

            Definition Entry = { Current.Code + Start, Current.Ip - Start, static_cast<UINT32>(A), TRUE };
            Current.Ip++;

            if (Opcode == 0x2C)
            {
                if (A < 0)
                    return RX_HINT_FAIL;
                if (static_cast<SIZE_T>(A) >= Functions.size())
                    Functions.resize(static_cast<SIZE_T>(A) + 1, Definition{});
                Functions[A] = Entry;
            }
            else
            {
                for (auto& Existing : Instructions)
                {
                    if (Existing.Opcode == static_cast<UINT32>(A))
                    {
                        Existing = Entry;
                        return S_OK;
                    }
                }
                Instructions.push_back(Entry);
            }
            return S_OK;
        }

        /* Moves */
        case 0x2E:
        case 0x2F:
        {
            RxF26Dot6 Distance = 0;

            if (!Pop(&A) || !ValidPoint(State.Zp0, A))
                return RX_HINT_FAIL;
            if (Opcode & 1)
            {
                RxF26Dot6 Position = CurrentProjection(State.Zp0, A);

                Distance = Round(Position) - Position;
            }
            MovePoint(State.Zp0, A, Distance, TRUE);
            State.Rp0 = State.Rp1 = A;
            return S_OK;
        }

        case 0x30:
        case 0x31:
            return DoInterpolateUntouched(Opcode == 0x31);

        case 0x32:
        case 0x33:
        case 0x34:
        case 0x35:
        case 0x36:
        case 0x37:
            return DoShift(Opcode);

        case 0x38:
        {
            RxF26Dot6 Dx;
            RxF26Dot6 Dy;

            if (!Pop(&C))
                return RX_HINT_FAIL;
            Dx = RxMulDiv(C, State.Freedom.X, RxOne14);
            Dy = RxMulDiv(C, State.Freedom.Y, RxOne14);
            while (State.Loop-- > 0)
            {
                Zone* Target;
                BOOLEAN Locked = Size.IgnoreXMoves && !NativeClearType && DidIupX && DidIupY;

                if (!Pop(&A) || !ValidPoint(State.Zp2, A))
                    return RX_HINT_FAIL;
                Target = &Zones[State.Zp2];
                if (!Locked)
                {
                    if (!(Size.IgnoreXMoves && !NativeClearType && State.Zp2 == 1))
                        Target->CurrentX[A] += Dx;
                    Target->CurrentY[A] += Dy;
                }
                if (State.Freedom.X != 0)
                    Target->Flags[A] |= RxTouchedX;
                if (State.Freedom.Y != 0)
                    Target->Flags[A] |= RxTouchedY;
            }
            State.Loop = 1;
            return S_OK;
        }

        case 0x39:
            return DoInterpolatePoints();

        case 0x3A:
        case 0x3B:
        {
            RxF26Dot6 Distance;

            if (!Pop(&B) || !Pop(&A) || !ValidPoint(State.Zp1, A) || !ValidPoint(State.Zp0, State.Rp0))
                return RX_HINT_FAIL;

            if (State.Zp1 == 0)
            {
                Zones[0].OriginalX[A] = Zones[State.Zp0].OriginalX[State.Rp0];
                Zones[0].OriginalY[A] = Zones[State.Zp0].OriginalY[State.Rp0];
                MoveOriginal(0, A, B);
                Zones[0].CurrentX[A] = Zones[0].OriginalX[A];
                Zones[0].CurrentY[A] = Zones[0].OriginalY[A];
            }

            Distance = Project(Zones[State.Zp1].CurrentX[A] - Zones[State.Zp0].CurrentX[State.Rp0],
                               Zones[State.Zp1].CurrentY[A] - Zones[State.Zp0].CurrentY[State.Rp0]);
            MovePoint(State.Zp1, A, B - Distance, TRUE);
            State.Rp1 = State.Rp0;
            State.Rp2 = A;
            if (Opcode & 1)
                State.Rp0 = A;
            return S_OK;
        }

        case 0x3C:
            if (!ValidPoint(State.Zp0, State.Rp0))
                return RX_HINT_FAIL;
            while (State.Loop-- > 0)
            {
                RxF26Dot6 Distance;

                if (!Pop(&A) || !ValidPoint(State.Zp1, A))
                    return RX_HINT_FAIL;
                Distance = Project(Zones[State.Zp1].CurrentX[A] - Zones[State.Zp0].CurrentX[State.Rp0],
                                   Zones[State.Zp1].CurrentY[A] - Zones[State.Zp0].CurrentY[State.Rp0]);
                MovePoint(State.Zp1, A, -Distance, TRUE);
            }
            State.Loop = 1;
            return S_OK;

        case 0x3E:
        case 0x3F:
        {
            RxF26Dot6 Distance;
            RxF26Dot6 Position;

            if (!Pop(&B) || !Pop(&A) || !ValidPoint(State.Zp0, A))
                return RX_HINT_FAIL;
            if (B < 0 || static_cast<SIZE_T>(B) >= Cvt.size())
                return RX_HINT_FAIL;

            Distance = ScaledCvt(B);
            if (State.Zp0 == 0)
            {
                Zones[0].OriginalX[A] = RxMulDiv(Distance, State.Freedom.X, RxOne14);
                Zones[0].OriginalY[A] = RxMulDiv(Distance, State.Freedom.Y, RxOne14);
                Zones[0].CurrentX[A] = Zones[0].OriginalX[A];
                Zones[0].CurrentY[A] = Zones[0].OriginalY[A];
            }

            Position = CurrentProjection(State.Zp0, A);
            if (Opcode & 1)
            {
                if (std::abs(Distance - Position) > State.CvtCutIn)
                    Distance = Position;
                Distance = Round(Distance);
            }
            MovePoint(State.Zp0, A, Distance - Position, TRUE);
            State.Rp0 = State.Rp1 = A;
            return S_OK;
        }

        case 0x40:
        case 0x41:
        {
            UINT32 Count;

            if (Current.Ip >= Current.Length)
                return RX_HINT_FAIL;
            Count = Current.Code[Current.Ip++];
            if (Current.Ip + Count * (Opcode == 0x41 ? 2 : 1) > Current.Length)
                return RX_HINT_FAIL;
            while (Count-- != 0)
            {
                INT32 Value;

                if (Opcode == 0x40)
                {
                    Value = Current.Code[Current.Ip++];
                }
                else
                {
                    Value = static_cast<SHORT>((Current.Code[Current.Ip] << 8) | Current.Code[Current.Ip + 1]);
                    Current.Ip += 2;
                }
                if (!Push(Value))
                    return RX_HINT_FAIL;
            }
            return S_OK;
        }

        /* Storage and control values */
        case 0x42:
            if (!Pop(&B) || !Pop(&A) || A < 0 || static_cast<SIZE_T>(A) >= Storage.size())
                return RX_HINT_FAIL;
            Storage[A] = B;
            return S_OK;

        case 0x43:
            if (!Pop(&A) || A < 0 || static_cast<SIZE_T>(A) >= Storage.size())
                return RX_HINT_FAIL;
            return Push(Storage[A]) ? S_OK : RX_HINT_FAIL;

        case 0x44:
            if (!Pop(&B) || !Pop(&A) || A < 0 || static_cast<SIZE_T>(A) >= Cvt.size())
                return RX_HINT_FAIL;
            Cvt[A] = B;
            return S_OK;

        case 0x70:
            if (!Pop(&B) || !Pop(&A) || A < 0 || static_cast<SIZE_T>(A) >= Cvt.size())
                return RX_HINT_FAIL;
            Cvt[A] = RxScale(B, Size.ScaleY);
            return S_OK;

        case 0x45:
            if (!Pop(&A) || A < 0 || static_cast<SIZE_T>(A) >= Cvt.size())
                return RX_HINT_FAIL;
            return Push(ScaledCvt(A)) ? S_OK : RX_HINT_FAIL;

        /* Measurements */
        case 0x46:
        case 0x47:
            if (!Pop(&A) || !ValidPoint(State.Zp2, A))
                return RX_HINT_FAIL;
            return Push(Opcode == 0x46 ? CurrentProjection(State.Zp2, A) : OriginalProjection(State.Zp2, A)) ?
                       S_OK : RX_HINT_FAIL;

        case 0x48:
        {
            RxF26Dot6 Position;

            if (!Pop(&B) || !Pop(&A) || !ValidPoint(State.Zp2, A))
                return RX_HINT_FAIL;
            Position = CurrentProjection(State.Zp2, A);
            MovePoint(State.Zp2, A, B - Position, TRUE);
            if (State.Zp2 == 0)
            {
                Zones[0].OriginalX[A] = Zones[0].CurrentX[A];
                Zones[0].OriginalY[A] = Zones[0].CurrentY[A];
            }
            return S_OK;
        }

        case 0x49:
        case 0x4A:
        {
            RxF26Dot6 Distance;

            if (!Pop(&B) || !Pop(&A) || !ValidPoint(State.Zp0, B) || !ValidPoint(State.Zp1, A))
                return RX_HINT_FAIL;
            if (Opcode == 0x4A)
                Distance = Project(Zones[State.Zp0].CurrentX[B] - Zones[State.Zp1].CurrentX[A],
                                   Zones[State.Zp0].CurrentY[B] - Zones[State.Zp1].CurrentY[A]);
            else
                Distance = DualProject(Zones[State.Zp0].OriginalX[B] - Zones[State.Zp1].OriginalX[A],
                                       Zones[State.Zp0].OriginalY[B] - Zones[State.Zp1].OriginalY[A]);
            return Push(Distance) ? S_OK : RX_HINT_FAIL;
        }

        case 0x4B:
            return Push(CurrentPpem()) ? S_OK : RX_HINT_FAIL;

        case 0x4C:
            return Push(Size.PointSize) ? S_OK : RX_HINT_FAIL;

        case 0x4D:
            State.AutoFlip = TRUE;
            return S_OK;

        case 0x4E:
            State.AutoFlip = FALSE;
            return S_OK;

        case 0x4F:
            return Pop(&A) ? S_OK : RX_HINT_FAIL;

        /* Logic */
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        {
            BOOLEAN Result;

            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            switch (Opcode)
            {
                case 0x50: Result = A < B; break;
                case 0x51: Result = A <= B; break;
                case 0x52: Result = A > B; break;
                case 0x53: Result = A >= B; break;
                case 0x54: Result = A == B; break;
                default: Result = A != B; break;
            }
            return Push(Result ? 1 : 0) ? S_OK : RX_HINT_FAIL;
        }

        case 0x56:
        case 0x57:
        {
            INT32 Rounded;

            if (!Pop(&A))
                return RX_HINT_FAIL;
            Rounded = Round(A) & 127;
            return Push((Opcode == 0x56) ? (Rounded == 64) : (Rounded == 0)) ? S_OK : RX_HINT_FAIL;
        }

        case 0x58:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            if (A == 0 && !SkipBranch(Current, TRUE))
                return RX_HINT_FAIL;
            return S_OK;

        case 0x59:
            return S_OK;

        case 0x5A:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push((A != 0 && B != 0) ? 1 : 0) ? S_OK : RX_HINT_FAIL;

        case 0x5B:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push((A != 0 || B != 0) ? 1 : 0) ? S_OK : RX_HINT_FAIL;

        case 0x5C:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push(A == 0 ? 1 : 0) ? S_OK : RX_HINT_FAIL;

        case 0x5D:
        case 0x71:
        case 0x72:
        case 0x73:
        case 0x74:
        case 0x75:
            return DoDelta(Opcode);

        case 0x5E:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.DeltaBase = A;
            return S_OK;

        case 0x5F:
            if (!Pop(&A) || A < 0 || A > 6)
                return RX_HINT_FAIL;
            State.DeltaShift = A;
            return S_OK;

        /* Arithmetic */
        case 0x60:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push(A + B) ? S_OK : RX_HINT_FAIL;

        case 0x61:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push(A - B) ? S_OK : RX_HINT_FAIL;

        case 0x62:
            if (!Pop(&B) || !Pop(&A) || B == 0)
                return RX_HINT_FAIL;
            return Push(RxMulDivTruncate(A, 64, B)) ? S_OK : RX_HINT_FAIL;

        case 0x63:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push(RxMulDiv(A, B, 64)) ? S_OK : RX_HINT_FAIL;

        case 0x64:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push(A < 0 ? -A : A) ? S_OK : RX_HINT_FAIL;

        case 0x65:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push(-A) ? S_OK : RX_HINT_FAIL;

        case 0x66:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push(A & ~63) ? S_OK : RX_HINT_FAIL;

        case 0x67:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push((A + 63) & ~63) ? S_OK : RX_HINT_FAIL;

        case 0x68:
        case 0x69:
        case 0x6A:
        case 0x6B:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            return Push(Round(A)) ? S_OK : RX_HINT_FAIL;

        case 0x6C:
        case 0x6D:
        case 0x6E:
        case 0x6F:
            /* No engine compensation, so NROUND leaves the value alone */
            return S_OK;

        case 0x78:
        case 0x79:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            if ((Opcode == 0x78) == (B != 0))
            {
                A += static_cast<INT32>(Current.Ip) - 1;
                if (A < 0 || static_cast<UINT32>(A) > Current.Length)
                    return RX_HINT_FAIL;
                Current.Ip = A;
            }
            return S_OK;

        case 0x7E:
        case 0x7F:
            return Pop(&A) ? S_OK : RX_HINT_FAIL;

        case 0x80:
            while (State.Loop-- > 0)
            {
                if (!Pop(&A) || !ValidPoint(State.Zp0, A))
                    return RX_HINT_FAIL;
                Zones[State.Zp0].Flags[A] ^= RxOnCurve;
            }
            State.Loop = 1;
            return S_OK;

        case 0x81:
        case 0x82:
            if (!Pop(&B) || !Pop(&A) || A < 0 || B < A || !ValidPoint(State.Zp0, B))
                return RX_HINT_FAIL;
            for (INT32 Point = A; Point <= B; Point++)
            {
                if (Opcode == 0x81)
                    Zones[State.Zp0].Flags[Point] |= RxOnCurve;
                else
                    Zones[State.Zp0].Flags[Point] &= ~RxOnCurve;
            }
            return S_OK;

        case 0x85:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.ScanControl = A;
            return S_OK;

        case 0x8D:
            if (!Pop(&A))
                return RX_HINT_FAIL;
            State.ScanType = A;
            return S_OK;

        case 0x88:
            return DoGetInfo();

        case 0x8B:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push(std::max(A, B)) ? S_OK : RX_HINT_FAIL;

        case 0x8C:
            if (!Pop(&B) || !Pop(&A))
                return RX_HINT_FAIL;
            return Push(std::min(A, B)) ? S_OK : RX_HINT_FAIL;

        case 0x8E:
            if (!Pop(&A) || !Pop(&B))
                return RX_HINT_FAIL;
            /* Only prep may change instruction control */
            if (Where != Context::Prep || A < 1 || A > 3)
                return S_OK;
            {
                UINT32 Bit = 1u << (A - 1);

                State.InstructControl = (State.InstructControl & ~Bit) | (B & Bit);
            }
            return S_OK;

        case 0x91:
        {
            /* Default instance: every normalized axis coordinate is zero */
            RxSpan Fvar = Face->Sfnt().Table(RxTag('f', 'v', 'a', 'r'));
            USHORT Axes = Fvar.U16(8);

            if (Axes == 0)
                return RX_HINT_FAIL;
            for (USHORT Axis = 0; Axis < Axes; Axis++)
            {
                if (!Push(0))
                    return RX_HINT_FAIL;
            }
            return S_OK;
        }

        case 0x92:
            return Push(17) ? S_OK : RX_HINT_FAIL;
    }

    /* A font defined instruction */
    for (const auto& Defined : Instructions)
    {
        if (Defined.Active && Defined.Opcode == Opcode)
        {
            Program Call = { Defined.Start, Defined.Length, 0 };

            if (Depth >= RxCallDepthLimit)
                return RX_HINT_FAIL;
            return Run(Call, Depth + 1);
        }
    }

    return RX_HINT_FAIL;
}

/* Glyphs */

HRESULT
RxTrueTypeHinter::RunGlyphProgram(
    _In_ const RxSpan& Program,
    _In_ UINT32 FirstPoint,
    _In_reads_(Count) const INT32* UnscaledX,
    _In_reads_(Count) const INT32* UnscaledY,
    _In_ UINT32 Count,
    _Inout_ std::vector<RxHintPoint>& Points,
    _In_ const std::vector<USHORT>& ContourEnds)
{
    Zone& Glyph = Zones[1];
    HRESULT hr;

    Glyph.Resize(Count);
    Glyph.ContourEnds.clear();
    for (USHORT End : ContourEnds)
    {
        if (End >= FirstPoint && End < FirstPoint + Count)
            Glyph.ContourEnds.push_back(static_cast<USHORT>(End - FirstPoint));
    }

    for (UINT32 Index = 0; Index < Count; Index++)
    {
        const RxHintPoint& Point = Points[FirstPoint + Index];

        Glyph.CurrentX[Index] = Point.X;
        Glyph.CurrentY[Index] = Point.Y;
        Glyph.OriginalX[Index] = Point.X;
        Glyph.OriginalY[Index] = Point.Y;
        Glyph.UnscaledX[Index] = UnscaledX[Index];
        Glyph.UnscaledY[Index] = UnscaledY[Index];
        Glyph.Flags[Index] = Point.OnCurve ? RxOnCurve : 0;
    }

    Cvt = PrepCvt;
    Storage = PrepStorage;
    State = DefaultState;
    UpdateProjectionDot();
    DidIupX = FALSE;
    DidIupY = FALSE;

    if (DefaultState.InstructControl & RxInhibitGridFit)
        return S_OK;

    hr = Execute(Program.Bytes(), Program.Length(), Context::Glyph);
    if (FAILED(hr))
        return hr;

    for (UINT32 Index = 0; Index < Count; Index++)
    {
        RxHintPoint& Point = Points[FirstPoint + Index];

        Point.X = Glyph.CurrentX[Index];
        Point.Y = Glyph.CurrentY[Index];
        Point.OnCurve = (Glyph.Flags[Index] & RxOnCurve) ? 1 : 0;
    }

    GlyphDropout.Enabled = FALSE;
    GlyphDropout.Mode = 0;
    {
        UINT32 Control = State.ScanControl;
        UINT32 Threshold = Control & 0xFF;
        USHORT Ppem = Size.PpemY;

        if (Threshold == 0xFF || (Ppem <= Threshold && (Control & 0x100)) ||
            (Size.Rotated && (Control & 0x200)) || (Size.Stretched && (Control & 0x400)))
            GlyphDropout.Enabled = TRUE;
        if ((Control & 0x800) && Ppem > Threshold && Threshold != 0xFF)
            GlyphDropout.Enabled = FALSE;
        if ((Control & 0x1000) && !Size.Rotated)
            GlyphDropout.Enabled = FALSE;
        if ((Control & 0x2000) && !Size.Stretched)
            GlyphDropout.Enabled = FALSE;
        GlyphDropout.Mode = static_cast<USHORT>(State.ScanType);
    }
    return S_OK;
}

/* Phantom points: origin, advance, top and bottom */
static
VOID
RxAddPhantoms(
    _In_ const RxFaceTables* Face,
    _In_ USHORT Glyph,
    _In_ SHORT XMin,
    _In_ SHORT YMax,
    _Inout_ std::vector<INT32>& UnscaledX,
    _Inout_ std::vector<INT32>& UnscaledY)
{
    const RxFaceHeader& Header = Face->Header();
    USHORT Advance;
    SHORT Bearing;
    USHORT VerticalAdvance;
    SHORT TopBearing;
    INT32 Top;

    Face->HorizontalMetrics(Glyph, &Advance, &Bearing);
    if (!Face->VerticalMetrics(Glyph, &VerticalAdvance, &TopBearing))
    {
        INT32 Ascent = Header.HasOs2 ? Header.TypoAscender : Header.HheaAscender;
        INT32 Descent = Header.HasOs2 ? Header.TypoDescender : Header.HheaDescender;

        VerticalAdvance = static_cast<USHORT>(Ascent - Descent);
        TopBearing = static_cast<SHORT>(Ascent - YMax);
    }

    Top = YMax + TopBearing;
    UnscaledX.push_back(XMin - Bearing);
    UnscaledY.push_back(0);
    UnscaledX.push_back(XMin - Bearing + Advance);
    UnscaledY.push_back(0);
    UnscaledX.push_back(0);
    UnscaledY.push_back(Top);
    UnscaledX.push_back(0);
    UnscaledY.push_back(Top - VerticalAdvance);
}

HRESULT
RxTrueTypeHinter::HintSimple(
    _In_ USHORT Glyph,
    _In_ const RxGlyphOutline& Outline,
    _Inout_ std::vector<RxHintPoint>& Points,
    _Inout_ std::vector<USHORT>& ContourEnds)
{
    std::vector<INT32> UnscaledX;
    std::vector<INT32> UnscaledY;
    UINT32 First = static_cast<UINT32>(Points.size());
    UINT32 Count;
    RxF26Dot6 Shift;
    HRESULT hr;

    for (const auto& Point : Outline.Points)
    {
        UnscaledX.push_back(Point.X);
        UnscaledY.push_back(Point.Y);
    }
    RxAddPhantoms(Face, Glyph, Outline.XMin, Outline.YMax, UnscaledX, UnscaledY);

    Count = static_cast<UINT32>(UnscaledX.size());
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        BYTE OnCurve = Index < Outline.Points.size() ? Outline.Points[Index].OnCurve : 1;

        Points.push_back({ RxScale(UnscaledX[Index], Size.ScaleX), RxScale(UnscaledY[Index], Size.ScaleY), OnCurve });
    }

    /* The origin phantom point sits on the pixel grid before the program runs */
    Shift = RxRoundToGrid(Points[First + Count - 4].X) - Points[First + Count - 4].X;
    for (UINT32 Index = First; Index < First + Count; Index++)
        Points[Index].X += Shift;

    for (USHORT End : Outline.ContourEnds)
        ContourEnds.push_back(static_cast<USHORT>(First + End));

    hr = RunGlyphProgram(Outline.Instructions, First, UnscaledX.data(), UnscaledY.data(), Count, Points, ContourEnds);
    if (FAILED(hr))
    {
        /* Back to the scaled outline */
        for (UINT32 Index = 0; Index < Count; Index++)
        {
            Points[First + Index].X = RxScale(UnscaledX[Index], Size.ScaleX) + Shift;
            Points[First + Index].Y = RxScale(UnscaledY[Index], Size.ScaleY);
        }
    }
    return S_OK;
}

HRESULT
RxTrueTypeHinter::HintComposite(
    _In_ USHORT Glyph,
    _In_ ULONG Depth,
    _Inout_ std::vector<RxHintPoint>& Points,
    _Inout_ std::vector<USHORT>& ContourEnds,
    _Out_ RxF26Dot6* PhantomX)
{
    RxSpan Data = Face->GlyphData(Glyph);
    std::vector<RxHintPoint> Combined;
    std::vector<USHORT> CombinedEnds;
    std::vector<INT32> UnscaledX;
    std::vector<INT32> UnscaledY;
    std::vector<RxHintPoint> Phantoms;
    UINT32 Offset = 10;
    USHORT Flags = 0;
    BOOLEAN HavePhantoms = FALSE;
    HRESULT hr;

    *PhantomX = 0;

    if (Data.IsEmpty())
    {
        RxGlyphOutline Empty;

        Empty.Clear();
        hr = HintSimple(Glyph, Empty, Points, ContourEnds);
        if (FAILED(hr))
            return hr;
        *PhantomX = Points[Points.size() - RxPhantomCount].X;
        return S_OK;
    }

    if (Data.S16(0) >= 0 || Depth >= 16)
    {
        RxGlyphOutline Outline;

        hr = Face->LoadOutline(Glyph, Outline);
        if (FAILED(hr))
            return hr;
        hr = HintSimple(Glyph, Outline, Points, ContourEnds);
        if (FAILED(hr))
            return hr;
        *PhantomX = Points[Points.size() - RxPhantomCount].X;
        return S_OK;
    }

    do
    {
        std::vector<RxHintPoint> Part;
        std::vector<USHORT> PartEnds;
        USHORT Component;
        INT32 Arg1;
        INT32 Arg2;
        double Xx = 1.0;
        double Xy = 0.0;
        double Yx = 0.0;
        double Yy = 1.0;
        RxF26Dot6 Dx;
        RxF26Dot6 Dy;
        RxF26Dot6 PartPhantom;
        UINT32 Base;

        Flags = Data.U16(Offset);
        Component = Data.U16(Offset + 2);
        Offset += 4;
        if (Flags & RxCompWordArgs)
        {
            Arg1 = (Flags & RxCompXyValues) ? Data.S16(Offset) : Data.U16(Offset);
            Arg2 = (Flags & RxCompXyValues) ? Data.S16(Offset + 2) : Data.U16(Offset + 2);
            Offset += 4;
        }
        else
        {
            Arg1 = (Flags & RxCompXyValues) ? static_cast<CHAR>(Data.U8(Offset)) : Data.U8(Offset);
            Arg2 = (Flags & RxCompXyValues) ? static_cast<CHAR>(Data.U8(Offset + 1)) : Data.U8(Offset + 1);
            Offset += 2;
        }
        if (Flags & RxCompScale)
        {
            Xx = Yy = Data.S16(Offset) / 16384.0;
            Offset += 2;
        }
        else if (Flags & RxCompXyScale)
        {
            Xx = Data.S16(Offset) / 16384.0;
            Yy = Data.S16(Offset + 2) / 16384.0;
            Offset += 4;
        }
        else if (Flags & RxCompTwoByTwo)
        {
            Xx = Data.S16(Offset) / 16384.0;
            Xy = Data.S16(Offset + 2) / 16384.0;
            Yx = Data.S16(Offset + 4) / 16384.0;
            Yy = Data.S16(Offset + 6) / 16384.0;
            Offset += 8;
        }

        hr = HintComposite(Component, Depth + 1, Part, PartEnds, &PartPhantom);
        if (FAILED(hr))
            return hr;
        if (Part.size() < RxPhantomCount)
            continue;

        /* Origin of the component at zero before placing it */
        for (auto& Point : Part)
            Point.X -= PartPhantom;

        if (Xx != 1.0 || Xy != 0.0 || Yx != 0.0 || Yy != 1.0)
        {
            for (auto& Point : Part)
            {
                double X = Point.X * Xx + Point.Y * Yx;
                double Y = Point.X * Xy + Point.Y * Yy;

                Point.X = static_cast<RxF26Dot6>(std::lround(X));
                Point.Y = static_cast<RxF26Dot6>(std::lround(Y));
            }
        }

        if (Flags & RxCompXyValues)
        {
            double OffsetX = Arg1;
            double OffsetY = Arg2;

            if (Flags & RxCompScaledOffset)
            {
                double X = OffsetX * Xx + OffsetY * Yx;
                double Y = OffsetX * Xy + OffsetY * Yy;

                OffsetX = X;
                OffsetY = Y;
            }
            Dx = RxScale(static_cast<INT32>(std::lround(OffsetX)), Size.ScaleX);
            Dy = RxScale(static_cast<INT32>(std::lround(OffsetY)), Size.ScaleY);
            if (Flags & RxCompRoundXy)
            {
                Dy = RxRoundToGrid(Dy);
                if (!(Size.IgnoreXMoves && !NativeClearType))
                    Dx = RxRoundToGrid(Dx);
            }
        }
        else
        {
            if (static_cast<SIZE_T>(Arg1) >= Combined.size() || static_cast<SIZE_T>(Arg2) >= Part.size() - RxPhantomCount)
                return DWRITE_E_FILEFORMAT;
            Dx = Combined[Arg1].X - Part[Arg2].X;
            Dy = Combined[Arg1].Y - Part[Arg2].Y;
        }

        for (auto& Point : Part)
        {
            Point.X += Dx;
            Point.Y += Dy;
        }

        if (Flags & RxCompUseMetrics)
        {
            Phantoms.assign(Part.end() - RxPhantomCount, Part.end());
            HavePhantoms = TRUE;
        }

        Base = static_cast<UINT32>(Combined.size());
        Combined.insert(Combined.end(), Part.begin(), Part.end() - RxPhantomCount);
        for (USHORT End : PartEnds)
            CombinedEnds.push_back(static_cast<USHORT>(Base + End));
    } while (Flags & RxCompMore);

    /* Phantom points of the composite itself */
    {
        SHORT XMin = Data.S16(2);
        SHORT YMax = Data.S16(8);

        RxAddPhantoms(Face, Glyph, XMin, YMax, UnscaledX, UnscaledY);
        if (!HavePhantoms)
        {
            /* Components were placed with the origin at zero */
            for (UINT32 Index = 0; Index < RxPhantomCount; Index++)
                Phantoms.push_back({ RxScale(UnscaledX[Index] - UnscaledX[0], Size.ScaleX),
                                     RxScale(UnscaledY[Index], Size.ScaleY), 1 });
        }
    }

    {
        UINT32 Count = static_cast<UINT32>(Combined.size());
        std::vector<INT32> FullX(Count + RxPhantomCount);
        std::vector<INT32> FullY(Count + RxPhantomCount);
        UINT32 First = static_cast<UINT32>(Points.size());
        std::vector<RxHintPoint> Saved;

        Combined.insert(Combined.end(), Phantoms.begin(), Phantoms.end());

        /* The composite program sees the component results as its original outline */
        for (UINT32 Index = 0; Index < Count + RxPhantomCount; Index++)
        {
            FullX[Index] = Size.ScaleX != 0 ? RxMulDiv(Combined[Index].X, 0x10000, Size.ScaleX) : 0;
            FullY[Index] = Size.ScaleY != 0 ? RxMulDiv(Combined[Index].Y, 0x10000, Size.ScaleY) : 0;
        }

        Points.insert(Points.end(), Combined.begin(), Combined.end());
        for (USHORT End : CombinedEnds)
            ContourEnds.push_back(static_cast<USHORT>(First + End));

        if (Flags & RxCompInstructions)
        {
            USHORT Length = Data.U16(Offset);
            RxSpan Program = Data.Sub(Offset + 2, Length);

            Saved.assign(Points.begin() + First, Points.end());
            hr = RunGlyphProgram(Program, First, FullX.data(), FullY.data(), Count + RxPhantomCount, Points, ContourEnds);
            if (FAILED(hr))
                std::copy(Saved.begin(), Saved.end(), Points.begin() + First);
        }

        *PhantomX = Points[Points.size() - RxPhantomCount].X;
    }
    return S_OK;
}

HRESULT
RxTrueTypeHinter::HintGlyph(
    _In_ USHORT Glyph,
    _Inout_ std::vector<RxHintPoint>& Points,
    _Inout_ std::vector<USHORT>& ContourEnds)
{
    RxF26Dot6 Origin;
    HRESULT hr;

    Points.clear();
    ContourEnds.clear();

    hr = HintComposite(Glyph, 0, Points, ContourEnds, &Origin);
    if (FAILED(hr))
        return hr;

    /* Glyph origin at zero */
    for (auto& Point : Points)
        Point.X -= Origin;
    return S_OK;
}
