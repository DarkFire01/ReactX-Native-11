/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA LPC weighting: line spectral pairs, the fixed point spectrum and the low rate inverse quantizer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmacodes.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_WMA_E_CORRUPT        static_cast<HRESULT>(0x80040002)
#define XA_WMA_E_STARVED        static_cast<HRESULT>(0x80040004)

/* Fixed point ones: Q30 for factors, Q27 for the pair polynomials, Q25 for the spectrum terms */
#define XA_WMA_Q30              0x40000000
#define XA_WMA_Q27              0x08000000
#define XA_WMA_Q25              0x02000000

/* Linear congruential noise generator */
#define XA_WMA_NOISE_MULTIPLY   1664525u
#define XA_WMA_NOISE_ADD        1013904223u

/* Longest pair polynomial: ten roots and the two fixed ones */
#define XA_WMA_PAIR_TERMS       (XA_WMA_LSP_COUNT + 2)

static
float
XaWmaBitsToFloat(
    _In_ UINT32 Bits)
{
    float Value;

    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

static
float
XaWmaQuantStepOf(
    _In_ INT32 Index)
{
    return static_cast<float>(pow(10.0, static_cast<double>(static_cast<float>(Index) * 0.05f)));
}

static
INT32
XaWmaNoise(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Seed = Decoder->NoiseSeed * XA_WMA_NOISE_MULTIPLY + XA_WMA_NOISE_ADD;
    INT32 Signed = static_cast<INT32>(Seed);
    INT32 Value = (Signed >> 2) + (Signed >> 4);
    INT32 Delta = Value - Decoder->NoiseLast;

    Decoder->NoiseSeed = Seed;
    Decoder->NoiseLast = Value;
    return Delta;
}

/* Q30 product */
static
INT32
XaWmaMul(
    _In_ INT32 Left,
    _In_ INT32 Right)
{
    return static_cast<INT32>((static_cast<INT64>(Left) * Right) >> 30);
}

/**
 * @brief Multiplies a symmetric polynomial by 1 + Cosine z + z^2, keeping it symmetric.
 */
static
VOID
XaWmaAddRoot(
    _Inout_updates_(XA_WMA_PAIR_TERMS) INT32* Terms,
    _Inout_ UINT32* Count,
    _In_ INT32 Cosine)
{
    const INT32 Factor[3] = { XA_WMA_Q30, Cosine, XA_WMA_Q30 };
    INT32 Product[XA_WMA_PAIR_TERMS] = {};
    UINT32 Grown = *Count + 2;
    UINT32 Half = Grown / 2;
    UINT32 Needed = (Grown & 1) != 0 ? Half + 1 : Half;

    for (UINT32 Term = 0; Term < Needed; Term++)
    {
        UINT32 Sum = 0;

        for (UINT32 Tap = 0; Tap < 3 && Tap <= Term; Tap++)
            Sum += static_cast<UINT32>(XaWmaMul(Terms[Term - Tap], Factor[Tap]));
        Product[Term] = static_cast<INT32>(Sum);
    }
    for (UINT32 Term = 0; Term < Half; Term++)
    {
        Terms[Term] = Product[Term];
        Terms[Grown - 1 - Term] = Product[Term];
    }
    if ((Grown & 1) != 0)
        Terms[Half] = Product[Half];
    *Count = Grown;
}

/**
 * @brief Turns the ten line spectral pair codes into the ten LPC coefficients, in fixed point.
 */
static
VOID
XaWmaLspToLpc(
    _In_reads_(XA_WMA_LSP_COUNT) const UINT8* Codes,
    _Out_writes_(XA_WMA_LSP_COUNT) INT32* Lpc)
{
    INT32 Even[XA_WMA_PAIR_TERMS] = { XA_WMA_Q27, XA_WMA_Q27 };
    INT32 Odd[XA_WMA_PAIR_TERMS] = {};
    UINT32 Count = 2;

    for (UINT32 Pair = 0; Pair < XA_WMA_LSP_COUNT; Pair += 2)
        XaWmaAddRoot(Even, &Count, XaWmaLspCosines[16 * Pair + Codes[Pair]]);

    Odd[0] = XA_WMA_Q27;
    Odd[1] = XaWmaLspCosines[16 + Codes[1]] >> 3;
    Odd[2] = XA_WMA_Q27;
    Count = 3;
    for (UINT32 Pair = 3; Pair < XA_WMA_LSP_COUNT; Pair += 2)
        XaWmaAddRoot(Odd, &Count, XaWmaLspCosines[16 * Pair + Codes[Pair]]);

    for (UINT32 Term = 1; Term <= XA_WMA_LSP_COUNT / 2; Term++)
    {
        INT32 Difference = (Odd[Term] - Odd[Term - 1]) >> 1;
        INT32 Sum = Even[Term] >> 1;

        Lpc[Term - 1] = -(Difference + Sum);
        Lpc[XA_WMA_LSP_COUNT - Term] = Difference - Sum;
    }
}

/* Counts the leading zero bits of a byte */
static
UINT32
XaWmaLeadingZeros(
    _In_ UINT32 Byte)
{
    UINT32 Zeros = 0;

    while (Zeros < 8 && (Byte & (0x80u >> Zeros)) == 0)
        Zeros += 1;
    return Zeros;
}

/**
 * @brief One over the fourth root of Real squared plus Imaginary squared, in fixed point.
 */
static
UINT32
XaWmaQuadRoot(
    _In_ INT32 Real,
    _In_ INT32 Imaginary)
{
    UINT64 Square = static_cast<UINT64>(static_cast<INT64>(Real) * Real + static_cast<INT64>(Imaginary) * Imaginary);
    UINT64 High = Square >> 52;
    UINT32 Top = static_cast<UINT32>(Square >> 20);
    INT32 Base = 32;
    INT32 Adjust;
    INT32 Exponent;
    UINT64 Scaled;
    UINT32 Mantissa;
    UINT32 Index;
    UINT32 Root;

    if (High != 0)
    {
        Top = static_cast<UINT32>(High);
        Base = 0;
    }
    if (Top == 0)
        return 0xFFFFFFFF;

    if ((Top & 0xFF000000) != 0)
    {
        Adjust = High != 0 ? -51 : -19;
    }
    else
    {
        INT32 Before;

        do
        {
            Top <<= 8;
            Before = Base;
            Base += 8;
        } while ((Top & 0xFF000000) == 0);
        Adjust = Before - 43;
    }

    Exponent = static_cast<INT32>(XaWmaLeadingZeros(Top >> 24)) + Base;
    if (Exponent - 19 > 32)
        Scaled = Square << ((Exponent - Base + Adjust) & 63);
    else
        Scaled = Square >> ((51 - Exponent) & 63);

    Mantissa = static_cast<UINT32>(Scaled);
    Index = Mantissa >> 24;
    Root = XaWmaQuadRoots[Index] -
           static_cast<UINT32>((static_cast<UINT64>(XaWmaQuadRoots[Index] - XaWmaQuadRoots[Index + 1]) * (Mantissa << 8)) >> 32);
    return static_cast<UINT32>((static_cast<UINT64>(Root) * XaWmaQuadScales[Exponent]) >> 32);
}

/**
 * @brief Evaluates four spectrum points at once from one set of eight partial sums.
 *
 * Point lands at Point, Half minus Point, Half plus Point and twice Half minus Point.
 */
static
VOID
XaWmaSpectrumPoints(
    _In_ INT32 Point,
    _In_reads_(8) const INT32* Sums,
    _Out_ INT32* Spectrum,
    _In_ INT32 Half,
    _In_ INT32 Cos,
    _In_ INT32 Sin)
{
    INT32 Plus = Cos + Sin;
    INT32 Minus = Sin - Cos;
    INT32 Im1 = XaWmaMul(Sums[3], Plus) + XaWmaMul(Sums[2], Minus);
    INT32 Re1 = XaWmaMul(Plus, Sums[2]) - XaWmaMul(Sums[3], Minus);
    INT32 Product = XaWmaMul(Cos, Sin);
    INT32 Cos2 = XA_WMA_Q30 - (XaWmaMul(Cos, Cos) << 1);
    INT32 Sin2 = Product << 1;
    INT32 Up = Cos2 + (Product << 1);
    INT32 Down = Cos2 - (Product << 1);
    INT32 Im2 = XaWmaMul(Sums[1], Up) + XaWmaMul(Sums[0], Down);
    INT32 Re2 = XaWmaMul(Up, Sums[0]) - XaWmaMul(Sums[1], Down);
    INT32 Cos3 = XaWmaMul(Sin2, Sin) + XaWmaMul(Cos2, Cos);
    INT32 Sin3 = XaWmaMul(Cos2, Sin) - XaWmaMul(Sin2, Cos);
    INT32 Plus3 = Cos3 + Sin3;
    INT32 Minus3 = Sin3 - Cos3;
    INT32 Im3 = XaWmaMul(Sums[5], Plus3) + XaWmaMul(Sums[4], Minus3);
    INT32 Re3 = XaWmaMul(Plus3, Sums[4]) - XaWmaMul(Sums[5], Minus3);
    INT32 Re0 = Sums[6] - Sums[7];
    INT32 Im0 = Sums[7] + Sums[6];

    Spectrum[Point] = static_cast<INT32>(XaWmaQuadRoot((Im0 + Im3 + Im1 + Im2) >> 1, (Re0 + Re1 + Re2 + Re3) >> 1));
    Spectrum[Half - Point] = static_cast<INT32>(XaWmaQuadRoot((Im0 - Im2 - Re3 + Re1) >> 1, (Im1 - Re0 - Im3 + Re2) >> 1));
    Spectrum[Half + Point] = static_cast<INT32>(XaWmaQuadRoot((Im0 - Im2 - Re1 + Re3) >> 1, (Re0 - Im3 - Re2 + Im1) >> 1));
    Spectrum[2 * Half - Point] = static_cast<INT32>(XaWmaQuadRoot((Re1 - Re0 - Re2 + Re3) >> 1, (Im0 - Im3 - Im1 + Im2) >> 1));
}

/* The eight partial sums of the LPC polynomial at one frequency, from its rotation terms */
struct XaWmaLpcTerms
{
    INT32 Up;
    INT32 Down;
    INT32 Sum;
    INT32 Difference;
};

/* Fills the four groups of partial sums for the points one rotation step apart */
static
VOID
XaWmaLpcGroups(
    _In_reads_(XA_WMA_LSP_COUNT) const INT32* A,
    _In_ const XaWmaLpcTerms* Terms,
    _Out_writes_(32) INT32* Groups)
{
    INT32 One = XA_WMA_Q25;
    INT32 M1 = XaWmaMul(Terms->Up, A[7]);
    INT32 M6 = XaWmaMul(Terms->Down, A[7]);
    INT32 M3 = XaWmaMul(A[3], Terms->Sum);
    INT32 M15 = XaWmaMul(A[3], Terms->Difference);
    INT32 N20 = XaWmaMul(Terms->Up, A[9]);
    INT32 N0 = XaWmaMul(A[5], Terms->Sum);
    INT32 N4 = XaWmaMul(Terms->Down, A[9]);
    INT32 N6 = XaWmaMul(A[5], Terms->Difference);
    INT32 O6 = XaWmaMul(Terms->Up, A[8]);
    INT32 O7 = XaWmaMul(A[4], Terms->Sum);
    INT32 O9 = XaWmaMul(Terms->Down, A[8]);
    INT32 O0 = XaWmaMul(A[4], Terms->Difference);
    INT32 Q7 = XaWmaMul(A[6], Terms->Sum);
    INT32 Q8 = XaWmaMul(A[6], Terms->Difference);
    const INT32 Values[32] =
    {
        A[1] + N20 + N0, A[1] - N4 - N6, A[0] + O6 + O7, A[0] - O9 - O0, A[2] + Q7, A[2] - Q8, M1 + M3 + One, One - M6 - M15,
        A[1] + N4 + N0, A[1] - N20 + N6, A[0] + O9 + O7, A[0] - O6 + O0, A[2] + Q7, A[2] + Q8, M6 + M3 + One, M15 - M1 + One,
        A[1] - N20 - N6, A[1] - N0 + N4, A[0] - O6 - O0, A[0] - O7 + O9, A[2] - Q8, A[2] - Q7, One - M1 - M15, M6 - M3 + One,
        A[1] - N4 + N6, A[1] - N0 + N20, A[0] - O9 + O0, A[0] - O7 + O6, A[2] + Q8, A[2] - Q7, M15 - M6 + One, M1 - M3 + One,
    };

    memcpy(Groups, Values, sizeof(Values));
}

/* Twin rotation that advances by a fixed angle, kept as the value before and the one now */
struct XaWmaRotor
{
    INT32 Cos;
    INT32 Sin;
    INT32 CosBefore;
    INT32 SinBefore;
    INT32 Step;
};

static
VOID
XaWmaRotorStep(
    _Inout_ XaWmaRotor* Rotor)
{
    INT32 Cos = XaWmaMul(Rotor->Step, Rotor->Sin) + Rotor->CosBefore;
    INT32 Sin = Rotor->SinBefore - XaWmaMul(Rotor->Step, Rotor->Cos);

    Rotor->CosBefore = Rotor->Cos;
    Rotor->SinBefore = Rotor->Sin;
    Rotor->Cos = Cos;
    Rotor->Sin = Sin;
}

/**
 * @brief Spectral weights of a tile from its LPC coefficients, as one over the fourth root of the power response.
 */
static
HRESULT
XaWmaLpcSpectrum(
    _In_ const XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel,
    _In_reads_(XA_WMA_LSP_COUNT) const INT32* Lpc)
{
    INT32 Size = Decoder->Standard.Lpc ? static_cast<INT16>(Channel->Size) : Decoder->FrameSize;
    INT32 Half = Size >> 1;
    INT32 Quarter = Size >> 2;
    INT32 Eighth = Size >> 3;
    INT32 Sixteenth = Size >> 4;
    INT32* Spectrum = Channel->Levels;
    INT32 One = XA_WMA_Q25;
    INT32 Root2 = 0x5A827998;
    INT32 Root2Half = 0x2D413CCC;
    INT32 A[XA_WMA_LSP_COUNT];
    INT32 Groups[32];
    UINT32 Peak = 0;

    if (Size < 64 || Size > Decoder->FrameSize || (Size & (Size - 1)) != 0)
        return XA_WMA_E_CORRUPT;
    for (UINT32 Index = 0; Index < XA_WMA_LSP_COUNT; Index++)
        A[Index] = -(Lpc[Index] >> 2);

    {
        INT32 R3 = XaWmaMul(A[3], Root2);
        INT32 R4 = XaWmaMul(A[4], Root2);
        INT32 R5 = XaWmaMul(A[5], Root2);
        INT32 R6 = XaWmaMul(A[6], Root2);
        INT32 Even = A[0] + A[8];
        INT32 Odd = A[1] + A[9];
        INT32 Low = Even - A[2];
        INT32 Cross = A[4] - A[6];
        INT32 Tilt = XaWmaMul(Low - Cross, Root2Half);
        INT32 Bend = XaWmaMul(A[2] - (A[6] + A[4]) + Even, Root2Half);
        INT32 Edge = A[7] + One - A[3];
        const INT32 First[24] =
        {
            R5 + Odd, A[1] - A[9], Even + R4, A[0] - A[8], A[2] + R6, A[2], R3 + A[7] + One, One - A[7],
            A[5] + A[1] - A[9], A[1] - A[9] - A[5], A[4] + A[0] - A[8], A[0] - A[8] - A[4], A[2] + A[6], A[2] - A[6],
            A[3] + One - A[7], One - A[7] - A[3],
            A[1] - A[9], Odd - R5, A[0] - A[8], Even - R4, A[2], A[2] - R6, One - A[7], A[7] + One - R3,
        };

        Spectrum[0] = static_cast<INT32>(XaWmaQuadRoot(A[2] + A[3] + A[7] + A[6] + A[4] + Even + A[5] + Odd + One, 0));
        Spectrum[Quarter] = static_cast<INT32>(XaWmaQuadRoot(Tilt + Edge, Bend - A[5] + Odd));
        Spectrum[Half] = static_cast<INT32>(XaWmaQuadRoot((A[3] + A[7]) - (A[5] + Odd) + One, Low + Cross));
        Spectrum[Half + Quarter] = static_cast<INT32>(XaWmaQuadRoot(Edge - Tilt, A[5] - Odd + Bend));

        XaWmaSpectrumPoints(Sixteenth, &First[0], Spectrum, Half, 0x0C7C5C1E, 0x3EC52F9E);
        XaWmaSpectrumPoints(Eighth, &First[8], Spectrum, Half, 0x187DE2A6, 0x3B20D79D);
        XaWmaSpectrumPoints(Eighth + Sixteenth, &First[16], Spectrum, Half, 0x238E7672, 0x3536CC51);
    }

    if (Sixteenth > 1)
    {
        UINT32 Row = 0;
        const UINT32* Turn;
        XaWmaRotor Fine;
        XaWmaRotor Coarse;
        float Scale = XaWmaBitsToFloat(0x4E800000);
        INT32 Fold;

        while ((64 << Row) < Size)
            Row += 1;
        if (Row >= ARRAYSIZE(XaWmaTurns))
            return XA_WMA_E_CORRUPT;
        Turn = XaWmaTurns[Row];

        Coarse.Cos = static_cast<INT32>(XaWmaBitsToFloat(Turn[11]) * Scale);
        Coarse.Sin = static_cast<INT32>(XaWmaBitsToFloat(Turn[12]) * Scale);
        Coarse.CosBefore = 0;
        Coarse.SinBefore = XA_WMA_Q30;
        Coarse.Step = Coarse.Cos << 1;
        Fine.Cos = static_cast<INT32>(XaWmaBitsToFloat(Turn[4]) * Scale);
        Fine.Sin = static_cast<INT32>(XaWmaBitsToFloat(Turn[5]) * Scale);
        Fine.CosBefore = 0;
        Fine.SinBefore = XA_WMA_Q30;
        Fold = static_cast<INT32>(XaWmaBitsToFloat(Turn[10]) * Scale);
        Fine.Step = Fold;

        for (INT32 Point = 1; Point < Sixteenth; Point++)
        {
            INT32 X = Coarse.Cos;
            INT32 Y = Coarse.Sin;
            INT32 Twice = XaWmaMul(Y, X) << 1;
            INT32 Square = XA_WMA_Q30 - (XaWmaMul(X, X) << 1);
            XaWmaLpcTerms Terms;
            INT32 C410 = XaWmaMul(Fine.Sin, 0x187DE2A6);
            INT32 C992 = XaWmaMul(Fine.Cos, 0x3B20D79D);
            INT32 S992 = XaWmaMul(Fine.Sin, 0x3B20D79D);
            INT32 S410 = XaWmaMul(Fine.Cos, 0x187DE2A6);
            INT32 C759 = XaWmaMul(Fine.Cos, Root2Half);
            INT32 S759 = XaWmaMul(Fine.Sin, Root2Half);

            XaWmaRotorStep(&Coarse);
            Terms.Up = Twice + Square;
            Terms.Down = Twice - Square;
            Terms.Sum = Y + X;
            Terms.Difference = X - Y;
            XaWmaLpcGroups(A, &Terms, Groups);

            XaWmaSpectrumPoints(Point, &Groups[0], Spectrum, Half, Fine.Cos, Fine.Sin);
            XaWmaSpectrumPoints(Eighth - Point, &Groups[8], Spectrum, Half, C410 - C992, S410 + S992);
            XaWmaSpectrumPoints(Eighth + Point, &Groups[16], Spectrum, Half, C992 + C410, S992 - S410);
            XaWmaSpectrumPoints(Quarter - Point, &Groups[24], Spectrum, Half, S759 - C759, C759 + S759);
            XaWmaRotorStep(&Fine);
        }
    }

    for (INT32 Index = 0; Index < Channel->Size; Index++)
    {
        if (static_cast<UINT32>(Spectrum[Index]) > Peak)
            Peak = static_cast<UINT32>(Spectrum[Index]);
    }
    for (INT32 Index = 0; Index < Channel->Size; Index++)
        Channel->Weights[Index] = static_cast<float>(static_cast<UINT32>(Spectrum[Index])) * XaWmaBitsToFloat(0x35000000);
    if (Peak == 0)
        return E_FAIL;
    Channel->WeightPeak = static_cast<float>(Peak) * XaWmaBitsToFloat(0x35000000);
    return S_OK;
}

/* Carries the weights of the tile before over to a tile of another size by repeating or dropping them */
static
VOID
XaWmaResampleWeights(
    _Inout_ XaWmaChannel* Channel)
{
    float* Weights = Channel->Weights;
    INT32 Before = Channel->PrevSize;
    INT32 Size = Channel->Size;

    if (Before > Size)
    {
        INT32 Ratio = Before / Size;

        for (INT32 Index = 0; Index < Size; Index++)
            Weights[Index] = Weights[Index * Ratio];
    }
    else if (Before < Size)
    {
        INT32 Ratio = Size / Before;

        for (INT32 Remaining = Size / Ratio; Remaining > 0; Remaining -= 1)
        {
            INT32 Index = Remaining - 1;

            for (INT32 Copy = 0; Copy < Ratio; Copy++)
                Weights[Index * Ratio + Copy] = Weights[Index];
        }
    }
}

/**
 * @brief Weights of the noise bands of a channel relative to its last one, from the mean square LPC weight under each.
 */
static
VOID
XaWmaLpcNoiseWeights(
    _In_ const XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    const INT32* Edges = Decoder->Edges;
    INT32 High = Decoder->HighIndex;
    float Energies[XA_WMA_MAX_BANDS];
    UINT8 Count = 0;

    for (INT32 Band = Decoder->NoiseBand; Band + 1 < XA_WMA_MAX_BANDS; Band++)
    {
        INT32 Start = Edges[Band] > Decoder->NoiseStart ? Edges[Band] : Decoder->NoiseStart;
        INT32 End = Edges[Band + 1] < High ? Edges[Band + 1] : High;
        float Sum = 0.0f;

        if (Start >= High)
            break;
        if (Channel->NoiseMarks[Band] != 1 || Count >= XA_WMA_MAX_BANDS)
            continue;
        for (INT32 Index = Start; Index < End; Index++)
            Sum = fmaf(Channel->Weights[Index], Channel->Weights[Index], Sum);
        Energies[Count] = Sum / static_cast<float>(End - Start);
        Count += 1;
    }
    for (INT32 Band = 0; Band + 1 < Count; Band++)
        Channel->NoiseWeights[Band] = static_cast<float>(sqrt(static_cast<double>(Energies[Band] / Energies[Count - 1])));
    if (Count != 0)
        Channel->NoiseWeights[Count - 1] = 1.0f;
    Channel->NoiseMarks[0] = Count;
}

/**
 * @brief Reads the line spectral pairs of every channel that sends new weights and works out the weights.
 */
HRESULT
XaWmaReadLpcHeader(
    _Inout_ XaWmaDecoder* Decoder)
{
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];

        if (Channel->FreshMask && Channel->Coded)
        {
            INT32 Lpc[XA_WMA_LSP_COUNT];
            HRESULT hr;

            for (UINT32 Pair = 0; Pair < XA_WMA_LSP_COUNT; Pair++)
            {
                UINT32 Bits = (Pair >= 1 && Pair <= 7) ? 4 : 3;
                UINT32 Code;

                if (!XaWmaGetBits(Decoder, Bits, &Code))
                    return XA_WMA_E_STARVED;
                Channel->LspCodes[Pair] = static_cast<UINT8>(Code);
            }
            XaWmaLspToLpc(Channel->LspCodes, Lpc);
            hr = XaWmaLpcSpectrum(Decoder, Channel, Lpc);
            if (FAILED(hr))
                return hr;
        }
        else if (Channel->FreshMask)
        {
            for (INT32 Slot = 0; Slot < Channel->Size; Slot++)
                Channel->Weights[Slot] = 1.0f;
            Channel->WeightPeak = 1.0f;
        }
        else if (Channel->Tiles.Index > 0)
        {
            XaWmaResampleWeights(Channel);
        }
    }

    if (Decoder->NoiseSub)
    {
        for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
        {
            if (Decoder->Channels[Index].Coded)
                XaWmaLpcNoiseWeights(Decoder, &Decoder->Channels[Index]);
        }
    }
    return S_OK;
}

/**
 * @brief Inverse quantizer of LPC streams: levels times the spectral weight, with noise where the encoder asks for it.
 */
VOID
XaWmaQuantizeLow(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    const INT32* Edges = Decoder->Edges;
    const INT32* Levels = Channel->Levels;
    const float* Weights = Channel->Weights;
    float* Coefs = Channel->Coefs;
    float Inverse = 1.0f / Channel->WeightPeak;
    float Scale = Inverse * Decoder->QuantStep;
    float Unit = XaWmaBitsToFloat(0x31000000);
    INT32 Size = static_cast<INT16>(Channel->Size);
    INT32 Low = Decoder->LowIndex;
    INT32 High = Decoder->HighIndex;
    INT32 Position = 0;
    INT32 Taken = 0;

    if (!Decoder->NoiseSub)
    {
        memset(Coefs, 0, sizeof(*Coefs) * Low);
        for (Position = Low; Position < High; Position++, Taken++)
            Coefs[Position] = static_cast<float>(Levels[Taken]) * Weights[Position] * Scale;
        memset(Coefs + High, 0, sizeof(*Coefs) * (Size - High));
        return;
    }

    {
        float Dither = Decoder->NoiseScale * Unit;
        INT32 Band = 0;
        INT32 NoiseIndex = 0;

        for (; Position < Low; Position++)
            Coefs[Position] = Weights[Low] * static_cast<float>(XaWmaNoise(Decoder)) * (Dither * Scale);

        for (; Position < Decoder->NoiseStart; Position++, Taken++)
        {
            INT32 Delta = XaWmaNoise(Decoder);

            if (Position >= Edges[Band + 1])
                Band += 1;
            Coefs[Position] = fmaf(static_cast<float>(Delta), Dither, static_cast<float>(Levels[Taken])) * Weights[Position] * Scale;
        }

        while (Position < High && Band + 1 < XA_WMA_MAX_BANDS)
        {
            INT32 End;

            if (Position >= Edges[Band + 1])
                Band += 1;
            End = Edges[Band + 1] < High ? Edges[Band + 1] : High;
            if (Channel->NoiseMarks[Band] == 1)
            {
                float Power = Channel->NoiseWeights[NoiseIndex] * XaWmaQuantStepOf(Channel->NoisePowers[NoiseIndex]) * (Inverse * Unit);

                for (; Position < End; Position++)
                    Coefs[Position] = Weights[Position] * (static_cast<float>(XaWmaNoise(Decoder)) * Power);
                NoiseIndex += 1;
            }
            else
            {
                for (; Position < End; Position++, Taken++)
                {
                    INT32 Delta = XaWmaNoise(Decoder);

                    Coefs[Position] = fmaf(static_cast<float>(Delta), Dither, static_cast<float>(Levels[Taken])) * Weights[Position] * Scale;
                }
            }
        }

        {
            float Tail = Weights[High - 1] * Scale * Dither;

            for (; Position < Size; Position++)
                Coefs[Position] = static_cast<float>(XaWmaNoise(Decoder)) * Tail;
        }
    }
}
