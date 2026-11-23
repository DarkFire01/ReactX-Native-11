/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA subframe transform (DCT-IV over a half size complex FFT)
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmaxform.hpp"

#include <math.h>
#include <string.h>

#pragma fp_contract(off)

#define XA_WMA_XFORM_LEVELS     14

/* FFT stages are radix 2 on top of a first radix 4 pass */
#define XA_WMA_FFT_FIRST_SPAN   4

/* Input order of one complex FFT size */
struct XaWmaFftOrder
{
    UINT16* Source;
};

struct XaWmaTransform
{
    UINT32 MinLevel;
    UINT32 MaxLevel;
    XaWmaFftOrder Orders[XA_WMA_XFORM_LEVELS];

    /* Twiddles of the stage with span D start at index D minus the first span */
    float* TurnCos;
    float* TurnSin;

    float* Real;
    float* Imag;
};

/* Rotation constants for one DCT size, all as single precision */
struct XaWmaRotation
{
    float QuarterCos;
    float QuarterSin;
    float ThreeQuarterCos;
    float ThreeQuarterSin;
    float Step;
    float DoubleStep;
    float HalfCos;
    float HalfSin;
};

/* A rotation walked by the two term sine recurrence */
struct XaWmaSpin
{
    float Real;
    float Imag;
    float PrevReal;
    float PrevImag;
};

/*
 * Built in rows for sizes 64 through 2048, generated from the system tables:
 * sin(pi/4n), cos(pi/4n), sin(3pi/4n), cos(3pi/4n), sin(pi/n), cos(pi/n), sin(2pi/n), 2sin(pi/n).
 */
static const float XaWmaRotationRows[6][8] =
{
    { 0.0122715384f, 0.999924719f, 0.0368072242f, 0.999322355f, 0.0490676761f, 0.99879545f, 0.0980171412f, 0.0981353521f }, /* 64 */
    { 0.00613588467f, 0.999981165f, 0.0184067301f, 0.999830604f, 0.024541229f, 0.999698818f, 0.0490676761f, 0.049082458f }, /* 128 */
    { 0.00306795677f, 0.999995291f, 0.00920375437f, 0.999957621f, 0.0122715384f, 0.999924719f, 0.024541229f, 0.0245430768f }, /* 256 */
    { 0.00153398013f, 0.999998808f, 0.00460192608f, 0.99998939f, 0.00613588467f, 0.999981165f, 0.0122715384f, 0.0122717693f }, /* 512 */
    { 0.000766990299f, 0.999999702f, 0.00230096909f, 0.999997377f, 0.00306795677f, 0.999995291f, 0.00613588467f, 0.00613591354f }, /* 1024 */
    { 0.000383495179f, 0.99999994f, 0.00115048536f, 0.999999344f, 0.00153398013f, 0.999998808f, 0.00306795677f, 0.00306796026f }, /* 2048 */
};

static
UINT32
XaWmaLevelOf(
    _In_ UINT32 Size)
{
    UINT32 Level = 0;

    while ((Size >> Level) > 1)
        Level += 1;
    return Level;
}

static
BOOLEAN
XaWmaIsPowerOfTwo(
    _In_ UINT32 Size)
{
    return Size != 0 && (Size & (Size - 1)) == 0;
}

VOID
XaWmaFreeTransform(
    _In_opt_ XaWmaTransform* Transform)
{
    if (Transform == nullptr)
        return;
    for (UINT32 Level = 0; Level < XA_WMA_XFORM_LEVELS; Level += 1)
        delete[] Transform->Orders[Level].Source;
    delete[] Transform->TurnCos;
    delete[] Transform->TurnSin;
    delete[] Transform->Real;
    delete[] Transform->Imag;
    delete Transform;
}

/* Input order for an FFT of 2^Level points: the bit reversed index, negated */
static
BOOLEAN
XaWmaBuildOrder(
    _Inout_ XaWmaFftOrder* Order,
    _In_ UINT32 Level)
{
    UINT32 Points = 1u << Level;

    Order->Source = new (std::nothrow) UINT16[Points];
    if (Order->Source == nullptr)
        return FALSE;
    for (UINT32 Index = 0; Index < Points; Index += 1)
    {
        UINT32 Reversed = 0;

        for (UINT32 Bit = 0; Bit < Level; Bit += 1)
        {
            if (Index & (1u << Bit))
                Reversed |= 1u << (Level - 1 - Bit);
        }
        Order->Source[Index] = static_cast<UINT16>((Points - Reversed) & (Points - 1));
    }
    return TRUE;
}

HRESULT
XaWmaCreateTransform(
    _In_ UINT32 MinSize,
    _In_ UINT32 MaxSize,
    _Outptr_ XaWmaTransform** Transform)
{
    XaWmaTransform* Created;
    UINT32 Points;

    *Transform = nullptr;
    if (!XaWmaIsPowerOfTwo(MinSize) || !XaWmaIsPowerOfTwo(MaxSize))
        return E_INVALIDARG;
    if (MinSize < XA_WMA_XFORM_MIN_SIZE || MaxSize > XA_WMA_XFORM_MAX_SIZE || MinSize > MaxSize)
        return E_INVALIDARG;

    Created = new (std::nothrow) XaWmaTransform;
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    memset(Created, 0, sizeof(*Created));
    Created->MinLevel = XaWmaLevelOf(MinSize) - 1;
    Created->MaxLevel = XaWmaLevelOf(MaxSize) - 1;
    Points = 1u << Created->MaxLevel;

    Created->TurnCos = new (std::nothrow) float[Points];
    Created->TurnSin = new (std::nothrow) float[Points];
    Created->Real = new (std::nothrow) float[Points];
    Created->Imag = new (std::nothrow) float[Points];
    if (Created->TurnCos == nullptr || Created->TurnSin == nullptr ||
        Created->Real == nullptr || Created->Imag == nullptr)
    {
        XaWmaFreeTransform(Created);
        return E_OUTOFMEMORY;
    }

    for (UINT32 Level = Created->MinLevel; Level <= Created->MaxLevel; Level += 1)
    {
        if (!XaWmaBuildOrder(&Created->Orders[Level], Level))
        {
            XaWmaFreeTransform(Created);
            return E_OUTOFMEMORY;
        }
    }

    /* A stage with span D turns by k * 2pi / 2D, evaluated in double */
    for (UINT32 Span = XA_WMA_FFT_FIRST_SPAN; Span < Points; Span *= 2)
    {
        double Angle = 6.28318530717958647692 / static_cast<double>(2 * Span);

        for (UINT32 Turn = 0; Turn < Span; Turn += 1)
        {
            double Phase = static_cast<double>(Turn) * Angle;

            Created->TurnCos[Span - XA_WMA_FFT_FIRST_SPAN + Turn] = static_cast<float>(cos(Phase));
            Created->TurnSin[Span - XA_WMA_FFT_FIRST_SPAN + Turn] = static_cast<float>(sin(Phase));
        }
    }

    *Transform = Created;
    return S_OK;
}

/* First pass: a radix 4 butterfly on every group of four points */
static
VOID
XaWmaFftQuads(
    _Inout_updates_(Points) float* Real,
    _Inout_updates_(Points) float* Imag,
    _In_ UINT32 Points)
{
    for (UINT32 Base = 0; Base < Points; Base += 4)
    {
        float* Re = Real + Base;
        float* Im = Imag + Base;
        float SumRe01 = Re[0] + Re[1];
        float DiffRe01 = Re[0] - Re[1];
        float SumRe23 = Re[2] + Re[3];
        float DiffRe23 = Re[2] - Re[3];
        float SumIm01 = Im[0] + Im[1];
        float DiffIm01 = Im[0] - Im[1];
        float SumIm23 = Im[2] + Im[3];
        float DiffIm23 = Im[2] - Im[3];

        Re[0] = SumRe01 + SumRe23;
        Re[1] = DiffRe01 - DiffIm23;
        Re[2] = SumRe01 - SumRe23;
        Re[3] = DiffRe01 + DiffIm23;
        Im[0] = SumIm01 + SumIm23;
        Im[1] = DiffIm01 + DiffRe23;
        Im[2] = SumIm01 - SumIm23;
        Im[3] = DiffIm01 - DiffRe23;
    }
}

/* Forward complex FFT of 2^Level interleaved points, in place */
static
VOID
XaWmaFft(
    _In_ XaWmaTransform* Transform,
    _Inout_updates_(2u << Level) float* Data,
    _In_ UINT32 Level)
{
    float* Real = Transform->Real;
    float* Imag = Transform->Imag;
    const UINT16* Source;
    UINT32 Points = 1u << Level;

    if (Level < Transform->MinLevel || Level > Transform->MaxLevel)
    {
        memset(Data, 0, 2 * Points * sizeof(*Data));
        return;
    }

    Source = Transform->Orders[Level].Source;
    for (UINT32 Index = 0; Index < Points; Index += 1)
    {
        Real[Index] = Data[2 * Source[Index]];
        Imag[Index] = Data[2 * Source[Index] + 1];
    }

    XaWmaFftQuads(Real, Imag, Points);

    for (UINT32 Span = XA_WMA_FFT_FIRST_SPAN; Span < Points; Span *= 2)
    {
        const float* TurnCos = Transform->TurnCos + Span - XA_WMA_FFT_FIRST_SPAN;
        const float* TurnSin = Transform->TurnSin + Span - XA_WMA_FFT_FIRST_SPAN;

        for (UINT32 Group = 0; Group < Points; Group += 2 * Span)
        {
            for (UINT32 Turn = 0; Turn < Span; Turn += 1)
            {
                UINT32 Low = Group + Turn;
                UINT32 High = Low + Span;
                float TurnedRe = fmaf(-Imag[High], TurnSin[Turn], Real[High] * TurnCos[Turn]);
                float TurnedIm = fmaf(Real[High], TurnSin[Turn], Imag[High] * TurnCos[Turn]);
                float LowRe = Real[Low];
                float LowIm = Imag[Low];

                Real[Low] = LowRe + TurnedRe;
                Real[High] = LowRe - TurnedRe;
                Imag[Low] = LowIm + TurnedIm;
                Imag[High] = LowIm - TurnedIm;
            }
        }
    }

    for (UINT32 Index = 0; Index < Points; Index += 1)
    {
        Data[2 * Index] = Real[Index];
        Data[2 * Index + 1] = Imag[Index];
    }
}

static
VOID
XaWmaGetRotation(
    _In_ UINT32 Size,
    _In_ float Scale,
    _Out_ XaWmaRotation* Rotation)
{
    UINT32 Half = Size / 2;

    /* Sizes 64 through 2048 come from the built in rows */
    if (Size >= 64 && Size <= 2048 && XaWmaIsPowerOfTwo(Half))
    {
        const float* Row = XaWmaRotationRows[XaWmaLevelOf(Size) - 6];

        Rotation->QuarterCos = Row[1] * Scale;
        Rotation->QuarterSin = -(Scale * Row[0]);
        Rotation->ThreeQuarterCos = Row[3] * Scale;
        Rotation->ThreeQuarterSin = Row[2] * Scale;
        Rotation->Step = -Row[7];
        Rotation->DoubleStep = Row[6] * -2.0f;
        Rotation->HalfCos = Row[5];
        Rotation->HalfSin = Row[4];
    }
    else
    {
        double Inverse = 1.0 / static_cast<double>(static_cast<INT32>(Size));
        double Gain = static_cast<double>(Scale);
        double Quarter = -0.78539816339744830962 * Inverse;
        double ThreeQuarter = 2.35619449019234492885 * Inverse;
        double Whole = -3.14159265358979323846 * Inverse;

        Rotation->QuarterCos = static_cast<float>(cos(Quarter) * Gain);
        Rotation->QuarterSin = static_cast<float>(sin(Quarter) * Gain);
        Rotation->ThreeQuarterCos = static_cast<float>(cos(ThreeQuarter) * Gain);
        Rotation->ThreeQuarterSin = static_cast<float>(sin(ThreeQuarter) * Gain);
        Rotation->Step = static_cast<float>(sin(Whole) + sin(Whole));
        Rotation->DoubleStep = static_cast<float>(2.0 * sin(-6.28318530717958647692 * Inverse));
        Rotation->HalfCos = static_cast<float>(cos(Whole));
        Rotation->HalfSin = Rotation->Step * -0.5f;
    }
}

static
VOID
XaWmaSpinInit(
    _Out_ XaWmaSpin* Spin,
    _In_ float Real,
    _In_ float Imag,
    _In_ float PrevReal,
    _In_ float PrevImag)
{
    Spin->Real = Real;
    Spin->Imag = Imag;
    Spin->PrevReal = PrevReal;
    Spin->PrevImag = PrevImag;
}

static
VOID
XaWmaSpinAdvance(
    _Inout_ XaWmaSpin* Spin,
    _In_ float DoubleStep)
{
    float NextReal = fmaf(-DoubleStep, Spin->Imag, Spin->PrevReal);
    float NextImag = fmaf(DoubleStep, Spin->Real, Spin->PrevImag);

    Spin->PrevReal = Spin->Real;
    Spin->PrevImag = Spin->Imag;
    Spin->Real = NextReal;
    Spin->Imag = NextImag;
}

/* Rotates one interleaved pair by the spin, in place */
static
VOID
XaWmaSpinPair(
    _In_ const XaWmaSpin* Spin,
    _Inout_updates_(2) float* Pair)
{
    float Re = Pair[0];
    float Im = Pair[1];

    Pair[0] = fmaf(Spin->Imag, -Im, Spin->Real * Re);
    Pair[1] = fmaf(Spin->Imag, Re, Spin->Real * Im);
}

VOID
XaWmaInverseTransform(
    _In_ XaWmaTransform* Transform,
    _Inout_updates_(Size) float* Coefs,
    _In_ UINT32 Size,
    _In_ float Scale)
{
    XaWmaRotation Rot;
    XaWmaSpin Even;
    XaWmaSpin Odd;
    XaWmaSpin LowEven;
    XaWmaSpin LowOdd;
    XaWmaSpin HighEven;
    XaWmaSpin HighOdd;
    UINT32 Half = Size / 2;
    float Step;
    float Settled;
    float Lifted;

    XaWmaGetRotation(Size, Scale, &Rot);
    Step = Rot.Step;

    /* Fold the odd coefficients of the lower half against the mirrored upper half */
    for (UINT32 Index = 1; Index < Half; Index += 2)
    {
        float Kept = Coefs[Index];

        Coefs[Index] = Coefs[Size - Index];
        Coefs[Size - Index] = Kept;
    }

    /* Pre rotation: even pairs start at pi/4n, odd pairs one step further */
    XaWmaSpinInit(&Even,
                  Rot.QuarterCos,
                  Rot.QuarterSin,
                  fmaf(Step, Rot.ThreeQuarterSin, Rot.QuarterCos),
                  fmaf(-Step, Rot.ThreeQuarterCos, Rot.QuarterSin));
    {
        float OddReal = fmaf(-Step, Rot.QuarterSin, Rot.ThreeQuarterCos);
        float OddImag = fmaf(Step, Rot.QuarterCos, Rot.ThreeQuarterSin);

        XaWmaSpinInit(&Odd,
                      OddReal,
                      OddImag,
                      fmaf(Step, Rot.QuarterSin, OddReal),
                      fmaf(-Step, Rot.QuarterCos, OddImag));
    }
    for (UINT32 Index = 0; Index < Size; Index += 4)
    {
        XaWmaSpinPair(&Even, Coefs + Index);
        XaWmaSpinPair(&Odd, Coefs + Index + 2);
        XaWmaSpinAdvance(&Even, Rot.DoubleStep);
        XaWmaSpinAdvance(&Odd, Rot.DoubleStep);
    }

    XaWmaFft(Transform, Coefs, XaWmaLevelOf(Size) - 1);

    /* Post rotation walks from both ends toward the middle */
    Settled = fmaf(-Step, 0.0f, Rot.HalfCos);
    Lifted = fmaf(Step, 1.0f, Rot.HalfSin);
    XaWmaSpinInit(&LowEven, 1.0f, 0.0f, fmaf(Step, Rot.HalfSin, 1.0f), fmaf(-Step, Rot.HalfCos, 0.0f));
    XaWmaSpinInit(&LowOdd, Settled, Lifted, fmaf(Step, 0.0f, Settled), fmaf(-Step, 1.0f, Lifted));
    XaWmaSpinInit(&HighEven, Settled, Lifted, Rot.HalfCos, Rot.HalfSin);
    XaWmaSpinInit(&HighOdd, fmaf(-Step, Lifted, 1.0f), fmaf(Step, Settled, 0.0f), 1.0f, 0.0f);

    for (UINT32 Index = 0; Index < Half; Index += 4)
    {
        float* Low = Coefs + Index;
        float* High = Coefs + Size - 4 - Index;
        float L0 = Low[0];
        float L1 = Low[1];
        float L2 = Low[2];
        float L3 = Low[3];
        float H0 = High[0];
        float H1 = High[1];
        float H2 = High[2];
        float H3 = High[3];

        Low[0] = fmaf(-LowEven.Imag, L1, LowEven.Real * L0);
        Low[1] = fmaf(HighEven.Imag, H3, HighEven.Real * H2);
        Low[2] = fmaf(-LowOdd.Imag, L3, LowOdd.Real * L2);
        Low[3] = fmaf(HighOdd.Imag, H1, HighOdd.Real * H0);
        High[0] = fmaf(HighOdd.Imag, -H0, HighOdd.Real * H1);
        High[1] = fmaf(-LowOdd.Imag, L2, LowOdd.Real * -L3);
        High[2] = fmaf(HighEven.Imag, -H2, HighEven.Real * H3);
        High[3] = fmaf(-LowEven.Imag, L0, LowEven.Real * -L1);

        XaWmaSpinAdvance(&LowEven, Rot.DoubleStep);
        XaWmaSpinAdvance(&LowOdd, Rot.DoubleStep);
        XaWmaSpinAdvance(&HighEven, Rot.DoubleStep);
        XaWmaSpinAdvance(&HighOdd, Rot.DoubleStep);
    }
}
