/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Polyphase Kaiser windowed sinc rate conversion
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <math.h>

/* Products and sums round separately; only the table reads fuse */
#pragma fp_contract(off)

/* Filter order every buffer is designed with, in zero crossings each side */
#define DS_FILTER_ORDER         30

/* The sinc is tabled out to this many zero crossings */
#define DS_SINC_REACH           35.0

/* Table sizes, both read with linear interpolation between points */
#define DS_SINC_POINTS          16384
#define DS_KAISER_POINTS        1024
#define DS_KAISER_BETA          10.0

/* Polyphase designs stay within these sizes */
#define DS_MAX_PHASES           1280
#define DS_MAX_DESIGN           100000

/* Frames of slack in the history ring beyond two filter spans */
#define DS_RING_SLACK           500

/* Fixed point scales of the table reads: the integer part picks a point, the rest interpolates */
static const float DsSincScale = static_cast<float>(1073741824.0 / DS_SINC_REACH);
static const float DsKaiserScale = 1073741824.0f;

static float DsSincBase[DS_SINC_POINTS + 1];
static float DsSincSlope[DS_SINC_POINTS];
static float DsKaiserBase[DS_KAISER_POINTS + 1];
static float DsKaiserSlope[DS_KAISER_POINTS];
static INIT_ONCE DsTablesOnce = INIT_ONCE_STATIC_INIT;

/* Zeroth order modified Bessel function by its power series */
static
double
DsBesselI0(
    _In_ double Value)
{
    double Quarter = Value * 0.25 * Value;
    double Sum = 1.0;
    double Term = Quarter;

    for (UINT Order = 2; ; Order++)
    {
        Sum = Term + Sum;
        Term = 1.0 / (Order * Order) * Quarter * Term;
        if (!(Term >= 1e-8) || Order >= 22)
            break;
    }
    return Sum;
}

static
BOOL
CALLBACK
DsBuildTables(
    _Inout_ PINIT_ONCE Once,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_ PVOID* Context)
{
    const double Pi = 3.1415926535;
    double Spacing = 65536.0 / DsSincScale;
    double Norm = 1.0 / DsBesselI0(DS_KAISER_BETA);

    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    DsSincBase[0] = 1.0f;
    for (UINT Point = 1; Point <= DS_SINC_POINTS; Point++)
    {
        double Angle = Pi * (Point * Spacing);

        DsSincBase[Point] = static_cast<float>(sin(Angle) / Angle);
    }
    for (UINT Point = 0; Point < DS_SINC_POINTS; Point++)
        DsSincSlope[Point] = (DsSincBase[Point + 1] - DsSincBase[Point]) * (1.0f / 65536.0f);

    for (UINT Point = 0; Point <= DS_KAISER_POINTS; Point++)
    {
        double Place = Point / static_cast<double>(DS_KAISER_POINTS);
        double Inside = 1.0 - Place * Place;

        DsKaiserBase[Point] = static_cast<float>(DsBesselI0(DS_KAISER_BETA * sqrt(Inside > 0.0 ? Inside : 0.0)) * Norm);
    }
    for (UINT Point = 0; Point < DS_KAISER_POINTS; Point++)
        DsKaiserSlope[Point] = (DsKaiserBase[Point + 1] - DsKaiserBase[Point]) * (1.0f / 1048576.0f);
    return TRUE;
}

/* Reads a table at a fixed point place whose low Shift bits interpolate */
static
float
DsTableRead(
    _In_ const float* Base,
    _In_ const float* Slope,
    _In_ float Scaled,
    _In_ UINT Shift,
    _In_ UINT Points)
{
    UINT Fixed = static_cast<UINT>(Scaled);
    UINT Point = (Fixed >> Shift) & (Points - 1);
    float Fraction = static_cast<float>(Fixed & ((1u << Shift) - 1));

    return fmaf(Slope[Point], Fraction, Base[Point]);
}

/* Kaiser windowed sinc at a distance in zero crossings */
static
float
DsWindowedSinc(
    _In_ double Distance)
{
    float Near;
    float Sinc = 0.0f;
    float Edge;
    float Window = 0.0f;

    if (!(Distance > -DS_SINC_REACH && Distance < DS_SINC_REACH))
        return 0.0f;

    Near = fabsf(static_cast<float>(Distance));
    if (Near < static_cast<float>(DS_SINC_REACH))
        Sinc = DsTableRead(DsSincBase, DsSincSlope, Near * DsSincScale, 16, DS_SINC_POINTS);

    Edge = fabsf(static_cast<float>(1.0 / DS_FILTER_ORDER * Distance));
    if (Edge < 1.0f)
        Window = DsTableRead(DsKaiserBase, DsKaiserSlope, Edge * DsKaiserScale, 20, DS_KAISER_POINTS);
    return Window * Sinc;
}

/* Adds products four lanes at a time, the way the vector kernel walks a row */
static
ULONG
DsLaneSum(
    _In_reads_(Count) const float* Weights,
    _In_reads_(Count) const float* Samples,
    _In_ ULONG Count,
    _Out_writes_(4) float* Lanes)
{
    float Even[4] = {};
    float Odd[4] = {};
    ULONG Index = 0;

    for (; Index + 8 <= Count; Index += 8)
    {
        for (UINT Lane = 0; Lane < 4; Lane++)
        {
            float Low = Samples[Index + Lane] * Weights[Index + Lane];
            float High = Samples[Index + 4 + Lane] * Weights[Index + 4 + Lane];

            Even[Lane] = Even[Lane] + Low;
            Odd[Lane] = Odd[Lane] + High;
        }
    }
    for (UINT Lane = 0; Lane < 4; Lane++)
        Lanes[Lane] = Even[Lane] + Odd[Lane];
    if (Index + 4 <= Count)
    {
        for (UINT Lane = 0; Lane < 4; Lane++)
        {
            float Product = Samples[Index + Lane] * Weights[Index + Lane];

            Lanes[Lane] = Lanes[Lane] + Product;
        }
        Index += 4;
    }
    return Index;
}

static
float
DsFold(
    _In_reads_(4) const float* Lanes)
{
    return (Lanes[0] + Lanes[2]) + (Lanes[1] + Lanes[3]);
}

DsResampler::DsResampler()
{
    Active = FALSE;
    Coefficients = nullptr;
    Ring = nullptr;
    Release();
}

DsResampler::~DsResampler()
{
    Release();
}

VOID
DsResampler::Release(VOID)
{
    HeapFree(GetProcessHeap(), 0, Coefficients);
    HeapFree(GetProcessHeap(), 0, Ring);
    Coefficients = nullptr;
    Ring = nullptr;
    Active = FALSE;
    Up = 1;
    Down = 1;
    Half = 0;
    Taps = 0;
    Capacity = 0;
    Channels = 0;
    Phase = 0;
    Position = 0;
    Filled = 0;
}

HRESULT
DsResampler::Configure(
    _In_ ULONG InRate,
    _In_ ULONG OutRate,
    _In_ UINT Count)
{
    float In = static_cast<float>(InRate);
    float Out = static_cast<float>(OutRate);
    float Logarithm;
    float Shape;
    float Ratio;
    float Cutoff;
    double Span;
    double Relative;
    double Spacing;
    ULONG Common;
    ULONG Phases;
    ULONG Lowest;
    LONG Mirror;
    float Lanes[4] = {};
    float Total;
    float Scale;
    ULONG Index;

    InitOnceExecuteOnce(&DsTablesOnce, DsBuildTables, nullptr, nullptr);
    Release();

    /* Passband edge as a fraction of the lower Nyquist rate, fitted to the order */
    Logarithm = logf(static_cast<float>(DS_FILTER_ORDER));
    {
        double Log = Logarithm;
        double Curve = fma(-(Log * 0.005794964), Log, 0.804445578);

        Shape = static_cast<float>(fma(Log, 0.06730802, Curve));
    }
    Ratio = In / Out;
    Cutoff = (Ratio < 1.0f ? Ratio : 1.0f) * Shape;

    Span = InRate <= OutRate ? static_cast<double>(DS_FILTER_ORDER) :
                               static_cast<double>(In) / static_cast<double>(Out) * DS_FILTER_ORDER;
    Relative = static_cast<double>(fmaxf(Out, In) / fmaxf(Out, In / Cutoff));

    Common = InRate;
    for (ULONG Other = OutRate; Other != 0;)
    {
        ULONG Rest = Common % Other;

        Common = Other;
        Other = Rest;
    }
    Down = InRate / Common;
    Up = OutRate / Common;
    Half = static_cast<ULONG>(ceil(Span / Relative));
    Capacity = 2 * (static_cast<ULONG>(ceil((Span + Span) / Relative + 1.0)) + 5) + DS_RING_SLACK;
    Taps = (2 * Half + 4) / 4 * 4;
    Channels = Count;

    Phases = Up > Down ? Up : Down;
    if (Phases > DS_MAX_PHASES || Up + Half * Up >= DS_MAX_DESIGN)
    {
        Release();
        return S_FALSE;
    }

    Coefficients = static_cast<float*>(HeapAlloc(GetProcessHeap(), 0, Up * Taps * sizeof(*Coefficients)));
    Ring = static_cast<float*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Count * Capacity * sizeof(*Ring)));
    if (Coefficients == nullptr || Ring == nullptr)
    {
        Release();
        return E_OUTOFMEMORY;
    }

    /* Each phase row runs backwards over the prototype, and the near negative half mirrors the positive one */
    Spacing = Relative / Phases;
    Lowest = Up + Half * Up - 1;
    for (ULONG Row = 0; Row < Up; Row++)
    {
        float* Line = Coefficients + Row * Taps;

        for (ULONG Tap = 0; Tap < Taps; Tap++)
        {
            LONG Prototype = static_cast<LONG>(Row) + static_cast<LONG>(Up) * (static_cast<LONG>(Taps - 1 - Tap) - static_cast<LONG>(Half));

            Mirror = Prototype;
            if (Prototype < 0 && static_cast<ULONG>(-Prototype) <= Lowest)
                Mirror = -Prototype;
            Line[Tap] = DsWindowedSinc(Mirror * Spacing);
        }
    }

    /* Every phase row then sums to unity */
    {
        float Even[4] = {};
        float Odd[4] = {};
        ULONG Size = Up * Taps;

        for (Index = 0; Index + 8 <= Size; Index += 8)
        {
            for (UINT Lane = 0; Lane < 4; Lane++)
            {
                Even[Lane] = Even[Lane] + Coefficients[Index + Lane];
                Odd[Lane] = Odd[Lane] + Coefficients[Index + 4 + Lane];
            }
        }
        for (UINT Lane = 0; Lane < 4; Lane++)
            Lanes[Lane] = Even[Lane] + Odd[Lane];
        Total = (Lanes[0] + Lanes[1]) + (Lanes[2] + Lanes[3]);
        for (; Index + 4 <= Size; Index += 4)
            Total = Total + Coefficients[Index] + Coefficients[Index + 1] + Coefficients[Index + 2] + Coefficients[Index + 3];
        for (; Index < Size; Index++)
            Total = Total + Coefficients[Index];

        Scale = static_cast<float>(Up);
        if (Total != 0.0f)
            Scale = Scale / Total;
        for (Index = 0; Index < Size; Index++)
            Coefficients[Index] = Coefficients[Index] * Scale;
    }

    Active = TRUE;
    Reset();
    return S_OK;
}

VOID
DsResampler::Reset(VOID)
{
    if (Ring != nullptr)
        ZeroMemory(Ring, Channels * Capacity * sizeof(*Ring));
    Phase = 0;
    Position = 0;
    Filled = 0;
}

VOID
DsResampler::Push(
    _In_reads_(Channels) const float* Frame)
{
    ULONG Slot = static_cast<ULONG>(Filled % Capacity);

    for (UINT Channel = 0; Channel < Channels; Channel++)
        Ring[Channel * Capacity + Slot] = Frame[Channel];
    Filled += 1;
}

/* One row against the history from a ring slot, split where the ring wraps */
float
DsResampler::Convolve(
    _In_reads_(Taps) const float* Row,
    _In_ const float* Line,
    _In_ ULONG Slot) const
{
    ULONG First = Capacity - Slot;
    float Lanes[4];
    float Sum;
    ULONG Index;

    if (First >= Taps)
    {
        Index = DsLaneSum(Row, Line + Slot, Taps, Lanes);
        Sum = DsFold(Lanes);
        for (; Index < Taps; Index++)
            Sum = Sum + Line[Slot + Index] * Row[Index];
        return Sum;
    }

    Index = DsLaneSum(Row, Line + Slot, First, Lanes);
    Sum = DsFold(Lanes);
    for (; Index < First; Index++)
        Sum = Sum + Line[Slot + Index] * Row[Index];

    Index = DsLaneSum(Row + First, Line, Taps - First, Lanes);
    Sum = DsFold(Lanes) + Sum;
    for (; Index < Taps - First; Index++)
        Sum = Sum + Line[Index] * Row[First + Index];
    return Sum;
}

VOID
DsResampler::Pull(
    _Out_writes_(Channels) float* Frame)
{
    LONGLONG Start = Position - static_cast<LONGLONG>(Taps - Half - 1);
    ULONG Slot = static_cast<ULONG>(((Start % Capacity) + Capacity) % Capacity);
    const float* Row = Coefficients + Phase * Taps;

    for (UINT Channel = 0; Channel < Channels; Channel++)
        Frame[Channel] = Convolve(Row, Ring + Channel * Capacity, Slot);

    Phase += Down;
    Position += Phase / Up;
    Phase %= Up;
}

ULONG
DsResampler::InputFor(
    _In_ ULONG Outputs) const
{
    ULONGLONG Last = Phase + static_cast<ULONGLONG>(Down) * Outputs;
    LONGLONG Need = Position + static_cast<LONGLONG>(Last / Up) + Half + 1 - Filled;

    return Need > 0 ? static_cast<ULONG>(Need) : 0;
}
