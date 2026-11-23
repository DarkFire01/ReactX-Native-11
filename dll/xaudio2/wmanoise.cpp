/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA noise substitution: noise bands, integer levels and the mid rate inverse quantizer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmacodes.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_WMA_E_FORMAT         static_cast<HRESULT>(0x80040000)
#define XA_WMA_E_CORRUPT        static_cast<HRESULT>(0x80040002)
#define XA_WMA_E_STARVED        static_cast<HRESULT>(0x80040004)

/* Noise powers: the first is sent whole, the rest as deltas */
#define XA_WMA_NOISE_FIRST_BITS 7
#define XA_WMA_NOISE_FIRST_BIAS 19
#define XA_WMA_NOISE_DELTA_BIAS 18

/* Linear congruential noise generator */
#define XA_WMA_NOISE_MULTIPLY   1664525u
#define XA_WMA_NOISE_ADD        1013904223u

/* Mask values move in sixteenths of a decade */
#define XA_WMA_MASK_FALLS       72
#define XA_WMA_MASK_RISES       62

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
UINT32
XaWmaLog2(
    _In_ UINT32 Value)
{
    UINT32 Result = 0;

    while ((Value >> (Result + 1)) != 0)
        Result += 1;
    return Result;
}

/* Ten to the power of Steps sixteenths, from the encoder's tables */
static
float
XaWmaMaskPower(
    _In_ INT32 Steps)
{
    if (Steps > 0)
    {
        INT32 Index = Steps < XA_WMA_MASK_RISES ? Steps : XA_WMA_MASK_RISES;

        return static_cast<float>(1 << (Index >> 2)) * XaWmaBitsToFloat(XaWmaMaskRises[Index - 1]);
    }
    else
    {
        INT32 Index = -(Steps > -XA_WMA_MASK_FALLS ? Steps : 1 - XA_WMA_MASK_FALLS);

        return XaWmaBitsToFloat(XaWmaMaskSteps[Index]) / static_cast<float>(1 << (Index >> 2));
    }
}

static
float
XaWmaQuantStepOf(
    _In_ INT32 Index)
{
    return static_cast<float>(pow(10.0, static_cast<double>(static_cast<float>(Index) * 0.05f)));
}

/* Next noise value: the generator output, smoothed, minus the one before it */
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

/**
 * @brief Works out whether the stream fills bands with noise, from which frequency, and the first noise band per tile size.
 */
HRESULT
XaWmaSetupNoise(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Rate = static_cast<INT32>(Decoder->Layout.Rate);
    float Bits = Decoder->Standard.BitsPerSample;
    float Weighted = Decoder->Standard.WeightedBitsPerSample;
    float Fraction = 0.0f;
    float Inverse;

    Decoder->NoiseScale = Decoder->Standard.Bark ? XaWmaBitsToFloat(0x3CA3D70A) : XaWmaBitsToFloat(0x3D23D70A);
    Decoder->NoiseFrequency = static_cast<float>(Rate) * 0.5f;
    Decoder->NoiseSub = FALSE;
    if (Decoder->Layout.Version != XA_WMA_STANDARD_MAX || Rate > 48000)
        return S_OK;

    if (Rate >= 44100)
    {
        if (Weighted < XaWmaBitsToFloat(0x3F1C28F6))
            Fraction = XaWmaBitsToFloat(0x3E4CCCCD);
    }
    else if (Rate >= 22050)
    {
        if (Weighted < XaWmaBitsToFloat(0x3F947AE1))
            Fraction = Weighted >= XaWmaBitsToFloat(0x3F3851EC) ? XaWmaBitsToFloat(0x3EB33333) : XaWmaBitsToFloat(0x3E99999A);
    }
    else if (Rate >= 16000)
    {
        Fraction = Bits > 0.5f ? 0.25f : XaWmaBitsToFloat(0x3E19999A);
    }
    else if (Rate >= 11025)
    {
        Fraction = XaWmaBitsToFloat(0x3EB33333);
    }
    else if (Rate >= 8000)
    {
        if (Bits <= 0.625f)
            Fraction = 0.25f;
        else if (Bits <= 0.75f)
            Fraction = XaWmaBitsToFloat(0x3EA66666);
    }
    else if (Bits >= XaWmaBitsToFloat(0x3F4CCCCD))
    {
        Fraction = 0.375f;
    }
    else
    {
        Fraction = Bits >= XaWmaBitsToFloat(0x3F19999A) ? XaWmaBitsToFloat(0x3E99999A) : 0.25f;
    }
    if (Fraction == 0.0f)
        return S_OK;

    Decoder->NoiseSub = TRUE;
    Decoder->NoiseFrequency = static_cast<float>(Rate) * Fraction;

    /* The first noise band of each tile size is the band the noise frequency falls in */
    Inverse = 1.0f / static_cast<float>(Rate);
    for (UINT32 Level = 0; Level < Decoder->Levels; Level++)
    {
        INT32 Count = Decoder->BandCounts[Level];
        const INT32* Edges = Decoder->BandEdges[Level];
        INT32 Span = (2 * Decoder->FrameSize) / (1 << Level);
        INT32 Start = static_cast<INT32>(fmaf(static_cast<float>(Span) * Decoder->NoiseFrequency, Inverse, 0.5f));
        INT32 Band = Count - 1;

        for (INT32 Edge = 1; Edge < Count; Edge++)
        {
            if (Edges[Edge] > Start)
            {
                Band = Edge - 1;
                break;
            }
        }
        if (Band <= 0)
            return XA_WMA_E_FORMAT;
        Decoder->NoiseBandAt[Level] = Band;
    }
    Decoder->NoiseBand = Decoder->NoiseBandAt[0];
    return S_OK;
}

/* Coefficient where the noise starts in a tile of Size, and the band it starts in */
VOID
XaWmaEnterNoiseTile(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Level,
    _In_ INT32 Size)
{
    float Twice = static_cast<float>(Size) + static_cast<float>(Size);
    float Scaled = Decoder->NoiseFrequency * Twice / static_cast<float>(static_cast<INT32>(Decoder->Layout.Rate));
    INT32 Start = static_cast<INT32>(Scaled + 0.5f);

    Decoder->NoiseStart = Start > Size ? Size : Start;
    Decoder->NoiseBand = Decoder->NoiseBandAt[Level];
}

/**
 * @brief Reads which bands above the noise frequency each coded channel fills with noise, then their powers.
 */
HRESULT
XaWmaReadNoiseHeader(
    _Inout_ XaWmaDecoder* Decoder)
{
    const INT32* Edges = Decoder->Edges;
    INT32 High = Decoder->HighIndex;

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];
        UINT8* Marks = Channel->NoiseMarks;
        INT32 Band = Decoder->NoiseBand;
        INT32 Coded = Decoder->NoiseStart - Decoder->LowIndex;

        if (!Channel->Coded)
        {
            Channel->CodedCount = 0;
            continue;
        }

        Marks[0] = 0;
        for (;;)
        {
            INT32 Start = Edges[Band] > Decoder->NoiseStart ? Edges[Band] : Decoder->NoiseStart;
            INT32 End;
            UINT32 Bit;

            if (Start >= High)
                break;
            if (Band + 1 >= XA_WMA_MAX_BANDS)
                return XA_WMA_E_CORRUPT;
            End = Edges[Band + 1] < High ? Edges[Band + 1] : High;
            if (!XaWmaGetBits(Decoder, 1, &Bit))
                return XA_WMA_E_STARVED;
            Marks[Band] = static_cast<UINT8>(Bit);
            if (Bit != 0)
                Marks[0] += 1;
            else
                Coded += End - Start;
            Band += 1;
        }
        if (Band < Decoder->Bands)
            Marks[Band] = 0;
        Channel->CodedCount = Coded;
    }

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];
        UINT32 Value;

        if (!Channel->Coded || Channel->NoiseMarks[0] == 0)
            continue;
        if (!XaWmaGetBits(Decoder, XA_WMA_NOISE_FIRST_BITS, &Value))
            return XA_WMA_E_STARVED;
        Channel->NoisePowers[0] = static_cast<INT32>(Value) - XA_WMA_NOISE_FIRST_BIAS;
        for (UINT32 Power = 1; Power < Channel->NoiseMarks[0]; Power++)
        {
            UINT32 After;

            if (Power >= XA_WMA_MAX_BANDS)
                return XA_WMA_E_CORRUPT;
            if (!XaWmaGetSymbol(Decoder, XaWmaNoiseTree, &Value, &After))
                return XA_WMA_E_STARVED;
            Channel->NoisePowers[Power] = Channel->NoisePowers[Power - 1] + static_cast<INT32>(Value) - XA_WMA_NOISE_DELTA_BIAS;
        }
    }
    return S_OK;
}

/**
 * @brief Decodes the run level codes of a channel into CodedCount integer levels.
 */
HRESULT
XaWmaDecodeLevels(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    INT32 Count = Channel->CodedCount;

    Channel->Width = Count;
    Decoder->CoefIndex = 0;
    while (Decoder->CoefIndex < Count)
    {
        const XaWmaRun* Run = &Decoder->RunLevel;
        INT32 Slot;

        if (!XaWmaNextRun(Decoder, Channel))
            return Decoder->Reader.Dry ? XA_WMA_E_STARVED : XA_WMA_E_CORRUPT;
        Slot = static_cast<INT16>(Run->Run + Decoder->CoefIndex);
        if (Slot >= Count || Slot < 0)
            return XA_WMA_E_CORRUPT;
        Channel->Levels[Slot] = (Run->Level ^ Run->Sign) - Run->Sign;
        Decoder->CoefIndex = Slot + 1;
    }
    return S_OK;
}

/* Walks the bands of the tile the mask was sent for, in step with positions of the current tile */
struct XaWmaMaskWalk
{
    const INT32* Edges;
    INT32 Count;
    INT32 Shift;
    INT32 Index;
};

/* Position in the current tile of a mask band edge */
static
INT32
XaWmaFromMask(
    _In_ const XaWmaMaskWalk* Walk,
    _In_ INT32 Edge)
{
    if (Walk->Shift > 6)
        return (Edge + (1 << (Walk->Shift - 7))) >> (Walk->Shift - 6);
    return Edge << (6 - Walk->Shift);
}

/* Moves to the mask band holding Position and returns where that band ends, at most Limit */
static
INT32
XaWmaMaskBandEnd(
    _Inout_ XaWmaMaskWalk* Walk,
    _In_ INT32 Position,
    _In_ INT32 Limit)
{
    INT32 Target = (Position << Walk->Shift) >> 6;
    INT32 End;

    while (Walk->Edges[Walk->Index + 1] <= Target)
        Walk->Index += 1;
    if (Walk->Index >= Walk->Count)
        return Walk->Count;
    End = XaWmaFromMask(Walk, Walk->Edges[Walk->Index + 1]);
    return End > Limit ? Limit : End;
}

/* Steps to the next mask band when the position after Position already lies in it */
static
VOID
XaWmaMaskStep(
    _Inout_ XaWmaMaskWalk* Walk,
    _In_ INT32 Position)
{
    if (Walk->Edges[Walk->Index + 1] <= ((Position + 1) << Walk->Shift) >> 6)
        Walk->Index += 1;
}

/**
 * @brief Weights of the noise bands of a channel relative to its last one, from the mask energy under each.
 */
static
HRESULT
XaWmaNoiseWeights(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel,
    _In_ const XaWmaMaskWalk* Walk)
{
    const INT32* Edges = Decoder->Edges;
    const INT32* MaskEdges = Walk->Edges;
    INT32 High = Decoder->HighIndex;
    INT32 Rate = static_cast<INT32>(Decoder->Layout.Rate);
    BOOLEAN Exact = Walk->Shift == 6 || (Decoder->Layout.Version != 1 && static_cast<UINT32>(Rate - 32000) <= 0x2F43);
    INT32 Cut = 0;
    INT32 First = 0;
    INT32 Last = 0;
    UINT8 Count = 0;
    INT32 Peaks[XA_WMA_MAX_BANDS];
    float Energies[XA_WMA_MAX_BANDS];

    if (!Exact)
        Cut = ((static_cast<INT32>(static_cast<INT16>(Channel->Size)) << Walk->Shift) >> 6) *
              static_cast<INT32>(Decoder->Standard.HighCutoff) / Decoder->FrameSize;

    for (INT32 Band = Decoder->NoiseBand;; Band++)
    {
        INT32 Start = Edges[Band] > Decoder->NoiseStart ? Edges[Band] : Decoder->NoiseStart;

        if (Start >= High)
            break;
        if (Channel->NoiseMarks[Band] != 1)
            continue;
        if (Count >= XA_WMA_MAX_BANDS)
            return XA_WMA_E_CORRUPT;

        while (MaskEdges[First + 1] <= (Start << Walk->Shift) >> 6)
            First += 1;
        if (Exact)
        {
            Peaks[Count] = Channel->Mask[First];
        }
        else
        {
            INT32 End = High < Edges[Band + 1] ? High : Edges[Band + 1];
            INT32 Split = MaskEdges[First + 1] < Cut ? MaskEdges[First + 1] : Cut;
            INT32 Reach = (End << Walk->Shift) >> 6;

            Reach = (Reach < Cut ? Reach : Cut) - 1;
            while (MaskEdges[Last + 1] <= Reach)
                Last += 1;
            if (Last == First)
            {
                float Power = XaWmaMaskPower(Channel->Mask[First]);

                Energies[Count] = Power * Power;
            }
            else if (Last == First + 1)
            {
                /* The band spans two mask bands: weigh each by the share it covers */
                float Lower = XaWmaMaskPower(Channel->Mask[First]);
                float Upper = XaWmaMaskPower(Channel->Mask[Last]);
                float At = static_cast<float>(XaWmaFromMask(Walk, Split));
                float Inverse = 1.0f / static_cast<float>(static_cast<UINT32>(End - Start));
                float Below = (At - static_cast<float>(Start)) * Inverse * (Lower * Lower);

                Energies[Count] = fmaf((static_cast<float>(End) - At) * Inverse, Upper * Upper, Below);
            }
            else
            {
                return XA_WMA_E_CORRUPT;
            }
        }
        Count += 1;
    }

    for (INT32 Band = 0; Band + 1 < Count; Band++)
    {
        if (Exact)
            Channel->NoiseWeights[Band] = XaWmaMaskPower(Peaks[Band] - Peaks[Count - 1]);
        else
            Channel->NoiseWeights[Band] = static_cast<float>(sqrt(static_cast<double>(Energies[Band] / Energies[Count - 1])));
    }
    if (Count != 0)
        Channel->NoiseWeights[Count - 1] = 1.0f;
    Channel->NoiseMarks[0] = Count;
    return S_OK;
}

/**
 * @brief Inverse quantizer of Bark streams with noise substitution.
 *
 * Coded coefficients take the step of their mask band with a little noise added; noise bands take
 * their sent power and weight; everything outside the coded range is noise at the mask level.
 */
HRESULT
XaWmaQuantizeMid(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    const INT32* Edges = Decoder->Edges;
    const INT32* Levels = Channel->Levels;
    const INT32* Mask = Channel->Mask;
    float* Coefs = Channel->Coefs;
    float Step = Decoder->QuantStep;
    float Small = XaWmaBitsToFloat(0x2E23D70A);
    float Unit = XaWmaBitsToFloat(0x31000000);
    INT32 Peak = Channel->MaskPeak;
    INT32 Size = static_cast<INT16>(Channel->Size);
    INT32 Low = Decoder->LowIndex;
    INT32 High = Decoder->HighIndex;
    XaWmaMaskWalk Walk;
    INT32 Position = 0;
    INT32 Band = 0;
    INT32 Taken = 0;
    INT32 NoiseIndex = 0;
    UINT32 Level;
    HRESULT hr;

    if (Decoder->MaskSize <= 0 || Size <= 0)
        return XA_WMA_E_CORRUPT;
    Walk.Shift = static_cast<INT32>(XaWmaLog2(static_cast<UINT32>((Decoder->MaskSize << 6) / Size)));
    Level = XaWmaLog2(static_cast<UINT32>(Decoder->FrameSize / Decoder->MaskSize));
    if (Level >= Decoder->Levels)
        return XA_WMA_E_CORRUPT;
    Walk.Edges = Decoder->BandEdges[Level];
    Walk.Count = Decoder->BandCounts[Level];
    Walk.Index = 0;

    hr = XaWmaNoiseWeights(Decoder, Channel, &Walk);
    if (FAILED(hr))
        return hr;

    XaWmaMaskBandEnd(&Walk, 0, Low);
    while (Edges[Band + 1] <= 0)
        Band += 1;

    /* Below the low cutoff */
    if (Low > 0)
    {
        double Wide = static_cast<double>(Step);

        for (; Position < Low; Position++)
        {
            INT32 Delta = XaWmaNoise(Decoder);
            float Power = static_cast<float>(pow(10.0, static_cast<double>(static_cast<float>(Mask[Walk.Index] - Peak) * 0.0625f)));
            float Value = Power * static_cast<float>(Delta);

            Value = Value * (Unit * Decoder->NoiseScale);
            Coefs[Position] = static_cast<float>(static_cast<double>(Value) * Wide);
        }
    }

    /* Coded up to where the noise starts */
    while (Position < Decoder->NoiseStart)
    {
        INT32 Was = Position;
        INT32 WasIndex = Walk.Index;
        INT32 End = XaWmaMaskBandEnd(&Walk, Position, Decoder->NoiseStart);
        float Scale = XaWmaMaskPower(Mask[Walk.Index] - Peak) * Step;

        for (; Position < End; Position++, Taken++)
            Coefs[Position] = fmaf(static_cast<float>(XaWmaNoise(Decoder)), Small, static_cast<float>(Levels[Taken])) * Scale;
        XaWmaMaskStep(&Walk, Position);
        while (Position >= Edges[Band + 1])
            Band += 1;
        if (Position == Was && Walk.Index == WasIndex)
            return XA_WMA_E_CORRUPT;
    }

    /* Noise bands and coded bands up to the high cutoff */
    while (Position < High)
    {
        INT32 Was = Position;
        INT32 WasIndex = Walk.Index;
        INT32 WasBand = Band;

        if (Channel->NoiseMarks[Band] == 1)
        {
            float Power = Channel->NoiseWeights[NoiseIndex] * XaWmaQuantStepOf(Channel->NoisePowers[NoiseIndex]);
            INT32 Stop = Edges[Band + 1] < High ? Edges[Band + 1] : High;

            while (Position < Stop)
            {
                INT32 From = Position;
                INT32 FromIndex = Walk.Index;
                float Scale = XaWmaMaskPower(Mask[Walk.Index] - Peak) * Power;
                INT32 End = XaWmaMaskBandEnd(&Walk, Position, Stop);

                for (; Position < End; Position++)
                    Coefs[Position] = static_cast<float>(XaWmaNoise(Decoder)) * Unit * Scale;
                XaWmaMaskStep(&Walk, Position);
                if (Position == From && Walk.Index == FromIndex)
                    return XA_WMA_E_CORRUPT;
            }
            NoiseIndex += 1;
        }
        else
        {
            INT32 End = XaWmaMaskBandEnd(&Walk, Position, High);
            float Scale;

            if (End > Edges[Band + 1])
                End = Edges[Band + 1];
            Scale = XaWmaMaskPower(Mask[Walk.Index] - Peak) * Step;
            for (; Position < End; Position++, Taken++)
                Coefs[Position] = fmaf(static_cast<float>(XaWmaNoise(Decoder)), Small, static_cast<float>(Levels[Taken])) * Scale;
        }
        XaWmaMaskStep(&Walk, Position);
        while (Position >= Edges[Band + 1])
            Band += 1;
        if (Position == Was && Walk.Index == WasIndex && Band == WasBand)
            return XA_WMA_E_CORRUPT;
    }

    /* Above the high cutoff: noise at the level of the mask band holding the cutoff */
    if (Position < Size)
    {
        INT32 Target = ((High - 1) << Walk.Shift) >> 6;
        float Scale;

        while (Walk.Edges[Walk.Index] > Target)
            Walk.Index -= 1;
        Scale = XaWmaMaskPower(Mask[Walk.Index] - Peak) * Step;
        for (; Position < Size; Position++)
            Coefs[Position] = static_cast<float>(XaWmaNoise(Decoder)) * Scale * Small;
    }
    return S_OK;
}
