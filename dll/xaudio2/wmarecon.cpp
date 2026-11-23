/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA reconstruction: band layout, coefficient scaling, tile overlap and PCM output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmacodes.hpp"

#include <math.h>

#pragma fp_contract(off)

#define XA_WMA_E_CORRUPT        static_cast<HRESULT>(0x80040002)

/* Mask distance past which every band gets the smallest step */
#define XA_WMA_MASK_STEPS       72

/* Symbols of the coefficient trees below the run level table */
#define XA_WMA_SYMBOL_ESCAPE    0
#define XA_WMA_SYMBOL_END       1
#define XA_WMA_SYMBOL_FIRST     2

/* Fixed band edges the encoder uses for some tile sizes, before rounding to a multiple of four */
struct XaWmaFixedBands
{
    UINT32 MinRate;
    INT32 Span;
    INT32 Count;
    INT32 Edges[XA_WMA_MAX_BANDS - 1];
};

static const XaWmaFixedBands XaWmaFixed[] =
{
    { 44100, 1024, 17, { 5, 12, 18, 25, 34, 46, 54, 63, 86, 102, 123, 149, 179, 221, 279, 360, 512 } },
    { 44100, 512, 15, { 5, 11, 17, 23, 31, 37, 43, 51, 62, 74, 89, 110, 139, 180, 256 } },
    { 44100, 256, 12, { 4, 9, 12, 16, 21, 26, 37, 45, 55, 70, 90, 128 } },
    { 32000, 1024, 16, { 6, 13, 20, 29, 41, 55, 74, 101, 141, 170, 205, 246, 304, 384, 496, 512 } },
    { 32000, 512, 15, { 5, 10, 15, 20, 28, 37, 50, 70, 85, 102, 123, 152, 192, 248, 256 } },
    { 32000, 256, 11, { 4, 9, 14, 19, 25, 35, 51, 76, 96, 124, 128 } },
    { 22050, 512, 14, { 5, 12, 18, 25, 34, 46, 63, 86, 102, 123, 149, 179, 221, 256 } },
    { 22050, 256, 10, { 5, 11, 17, 23, 31, 43, 62, 89, 110, 128 } },
};

static
float
XaWmaFloat(
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

/* Edges are found in a table for the common rates; MinRate picks the rate band they apply to */
static
const XaWmaFixedBands*
XaWmaFindFixed(
    _In_ UINT32 Rate,
    _In_ INT32 Span)
{
    UINT32 Floor;

    if (Rate >= 44100)
        Floor = 44100;
    else if (Rate >= 32000)
        Floor = 32000;
    else if (Rate >= 22050)
        Floor = 22050;
    else
        return nullptr;

    for (UINT32 Index = 0; Index < ARRAYSIZE(XaWmaFixed); Index++)
    {
        if (XaWmaFixed[Index].MinRate == Floor && XaWmaFixed[Index].Span == Span)
            return &XaWmaFixed[Index];
    }
    return nullptr;
}

/**
 * @brief Lays out the bands of every tile size, largest tile first.
 */
VOID
XaWmaBuildBands(
    _Inout_ XaWmaDecoder* Decoder)
{
    float Inverse = 1.0f / static_cast<float>(static_cast<INT32>(Decoder->Layout.Rate));

    for (UINT32 Level = 0; Level < Decoder->Levels; Level++)
    {
        INT32* Edges = Decoder->BandEdges[Level];
        INT32 Span = (2 * Decoder->FrameSize) >> Level;
        const XaWmaFixedBands* Fixed = XaWmaFindFixed(Decoder->Layout.Rate, Span);
        INT32 Next = 1;

        Edges[0] = 0;
        if (Fixed != nullptr)
        {
            Decoder->BandCounts[Level] = Fixed->Count;
            for (INT32 Band = 0; Band < Fixed->Count; Band++)
                Edges[Band + 1] = (Fixed->Edges[Band] + 2) & ~3;
            continue;
        }

        for (UINT32 Bark = 0; Bark < ARRAYSIZE(XaWmaBarkEdges); Bark++)
        {
            float Scaled = static_cast<float>(static_cast<UINT32>(XaWmaBarkEdges[Bark])) *
                           (static_cast<float>(Span) * Inverse);
            INT32 Edge = 4 * static_cast<INT32>(fmaf(Scaled, 0.25f, 0.5f));

            if (Edge > Edges[Next - 1])
            {
                Edges[Next] = Edge;
                Next += 1;
            }
            if (Edges[Next - 1] >= Span / 2)
                break;
        }
        Edges[Next - 1] = Span / 2;
        Decoder->BandCounts[Level] = Next - 1;
    }
}

/* Rotation that crossfades a tile into the one before it */
VOID
XaWmaSetTurn(
    _Inout_ XaWmaChannel* Channel)
{
    INT32 Overlap = XaMin(Channel->PrevSize, Channel->Size);

    if (Overlap >= 64 && Overlap <= 2048 && (Overlap & (Overlap - 1)) == 0)
    {
        const UINT32* Turn = XaWmaTurns[XaWmaLog2(static_cast<UINT32>(Overlap / 64))];

        Channel->TurnSin = XaWmaFloat(Turn[0]);
        Channel->TurnCos = XaWmaFloat(Turn[1]);
        Channel->TurnStep = XaWmaFloat(Turn[8]) + XaWmaFloat(Turn[8]);
    }
    else
    {
        double Angle = 3.14159265358979323846 / (4.0 * Overlap);

        Channel->TurnSin = static_cast<float>(sin(Angle));
        Channel->TurnCos = static_cast<float>(cos(Angle));
        Channel->TurnStep = static_cast<float>(sin(2.0 * Angle)) * 2.0f;
    }
    Channel->TurnPrevSin = -Channel->TurnSin;
    Channel->TurnPrevCos = Channel->TurnCos;
}

/* Decodes the next run and level of a channel's coefficients */
static
BOOLEAN
XaWmaNextRun(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ const XaWmaChannel* Channel)
{
    XaWmaRun* Run = &Decoder->RunLevel;
    UINT32 Symbol;
    UINT32 After;
    UINT32 Value;

    if (!XaWmaGetSymbol(Decoder, Channel->Book->Tree, &Symbol, &After))
        return FALSE;

    if (Symbol == XA_WMA_SYMBOL_ESCAPE)
    {
        UINT32 Width = (Decoder->RunBits + 1) & 0xFFFF;
        INT32 Keep = static_cast<INT16>(0xFFFFFFFFu >> (32 - Width));

        if (!XaWmaGetBits(Decoder, Decoder->EscapeBits, &Value))
            return FALSE;
        Run->Level = static_cast<INT32>(Value);
        if (!XaWmaGetBits(Decoder, Width, &Value))
            return FALSE;
        Run->Sign = static_cast<INT32>(Value & 1) - 1;
        Run->Run = static_cast<INT32>(static_cast<UINT32>(Keep & static_cast<INT16>(Value)) >> 1);
        return TRUE;
    }
    if (Symbol == XA_WMA_SYMBOL_END)
    {
        Run->Level = 0;
        Run->Run = static_cast<INT16>(Channel->Width - Decoder->CoefIndex - 1);
        return TRUE;
    }
    if (!XaWmaSkipBits(Decoder, 1))
        return FALSE;
    Run->Run = Channel->Book->Runs[Symbol - XA_WMA_SYMBOL_FIRST];
    Run->Level = Channel->Book->Levels[Symbol - XA_WMA_SYMBOL_FIRST];
    Run->Sign = static_cast<INT32>(After) - 1;
    return TRUE;
}

/* Moves the coefficient position past the run just decoded */
static
BOOLEAN
XaWmaStepRun(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Target = Decoder->CoefIndex + Decoder->RunLevel.Run + 1;

    if (static_cast<UINT32>(Decoder->RunLevel.Run) >= 0x8000 || Decoder->RunLevel.Run > 0x7FFE)
        return FALSE;
    if (Target < -0x8000 || Target > 0x7FFF)
        return FALSE;
    Decoder->CoefIndex = static_cast<INT16>(Target);
    return TRUE;
}

/* Quantizer step of a band that sits Drop quarter steps below the loudest band */
static
float
XaWmaBandStep(
    _In_ const XaWmaDecoder* Decoder,
    _In_ INT32 Drop)
{
    INT32 Index = Drop < XA_WMA_MASK_STEPS ? Drop : XA_WMA_MASK_STEPS - 1;
    float Power = XaWmaFloat(XaWmaMaskSteps[Index]) / static_cast<float>(1 << (Index >> 2));

    return Power * Decoder->QuantStep;
}

static
float
XaWmaSigned(
    _In_ const XaWmaDecoder* Decoder,
    _In_ float Value)
{
    return Decoder->RunLevel.Sign == 0 ? Value : -Value;
}

/**
 * @brief Decodes the run level coded coefficients of a channel and scales them by their band.
 *
 * The bands come from the tile size the mask was sent for; positions in a smaller or larger
 * tile map onto them through a fixed point ratio.
 */
HRESULT
XaWmaReconstruct(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel)
{
    float* Coefs = Channel->Coefs;
    const INT32* Edges;
    INT32 Ratio;
    INT32 RatioLog;
    INT32 BandTotal;
    INT32 Band = 0;
    INT32 Last = static_cast<INT16>(Channel->Width - 1);
    INT32 Stop = -1;
    float Scale = 0.0f;
    UINT32 Level;

    if (Decoder->MaskSize <= 0 || Channel->Size <= 0)
        return XA_WMA_E_CORRUPT;
    Ratio = (Decoder->MaskSize << 12) / Channel->Size;
    RatioLog = static_cast<INT32>(XaWmaLog2(static_cast<UINT32>(Ratio)));
    Level = XaWmaLog2(static_cast<UINT32>(Decoder->FrameSize / Decoder->MaskSize));
    if (Level >= Decoder->Levels)
        return XA_WMA_E_CORRUPT;
    Edges = Decoder->BandEdges[Level];
    BandTotal = Decoder->BandCounts[Level];

    while (Edges[Band + 1] <= (Decoder->CoefIndex * Ratio) >> 12)
        Band += 1;
    if (!XaWmaNextRun(Decoder, Channel) || !XaWmaStepRun(Decoder))
        return XA_WMA_E_CORRUPT;

    while (Decoder->CoefIndex < Last && Band < BandTotal)
    {
        INT32 Position = (Decoder->CoefIndex * Ratio) >> 12;
        INT32 Scaled;
        INT32 Limit;
        float Value;

        while (Edges[Band + 1] <= Position)
            Band += 1;
        if (Band >= BandTotal)
            break;

        /* A coefficient right on the previous band's last slot still takes that band's step */
        if (Decoder->CoefIndex == Stop)
        {
            Value = static_cast<float>(Decoder->RunLevel.Level) * Scale;
            Scale = XaWmaBandStep(Decoder, Channel->MaskPeak - Channel->Mask[Band]);
        }
        else
        {
            Scale = XaWmaBandStep(Decoder, Channel->MaskPeak - Channel->Mask[Band]);
            Value = static_cast<float>(Decoder->RunLevel.Level) * Scale;
        }

        if (RatioLog > 12)
            Scaled = (Edges[Band + 1] + (1 << (RatioLog - 13))) >> (RatioLog - 12);
        else
            Scaled = Edges[Band + 1] << (12 - RatioLog);
        Limit = Last < static_cast<INT16>(Scaled - 1) ? Channel->Width : Scaled;
        Stop = static_cast<INT16>(Limit - 1);

        for (;;)
        {
            Coefs[Decoder->CoefIndex] = XaWmaSigned(Decoder, Value);
            if (!XaWmaNextRun(Decoder, Channel) || !XaWmaStepRun(Decoder))
                return XA_WMA_E_CORRUPT;
            if (Decoder->CoefIndex >= Stop)
                break;
            Value = static_cast<float>(Decoder->RunLevel.Level) * Scale;
        }
        Band += 1;
    }

    if (Decoder->CoefIndex == Last)
    {
        if (Decoder->CoefIndex >= Stop)
        {
            if (Band - 1 < BandTotal)
            {
                INT32 Position = (Decoder->CoefIndex * Ratio) >> 12;

                while (Edges[Band] <= Position)
                {
                    INT32 Checked = Band;

                    Band += 1;
                    if (Checked >= BandTotal)
                        break;
                }
            }
            if (Band - 1 <= BandTotal)
                Scale = XaWmaBandStep(Decoder, Channel->MaskPeak - Channel->Mask[Band - 1]);
        }
        Coefs[Decoder->CoefIndex] = XaWmaSigned(Decoder, static_cast<float>(Decoder->RunLevel.Level) * Scale);
    }

    if (Decoder->CoefIndex > Channel->Size)
        return XA_WMA_E_CORRUPT;
    if (Decoder->LowIndex > 0)
        memset(Coefs, 0, sizeof(*Coefs) * Decoder->LowIndex);
    memset(Coefs + Decoder->HighIndex, 0, sizeof(*Coefs) * (Channel->Size - Decoder->HighIndex));
    return S_OK;
}

/**
 * @brief Puts a transformed tile into time order and crossfades it with the tile before it.
 */
VOID
XaWmaOverlap(
    _Inout_ XaWmaChannel* Channel)
{
    float* Coefs = Channel->Coefs;
    INT32 Size = Channel->Size;
    INT32 Overlap = Size;
    float Sin = Channel->TurnSin;
    float Cos = Channel->TurnCos;
    float PrevSin = Channel->TurnPrevSin;
    float PrevCos = Channel->TurnPrevCos;
    float* Low;
    float* High;

    for (INT32 Index = 0; Index < Size / 2; Index++)
    {
        float Swap = Coefs[Index];

        Coefs[Index] = Coefs[Size - 1 - Index];
        Coefs[Size - 1 - Index] = Swap;
    }

    /* The crossfade spans the smaller of the two tiles */
    if (Channel->PrevSize < Size)
        Overlap = (Size + Channel->PrevSize) / 2 - (Size - Channel->PrevSize) / 2;
    Low = Coefs - Overlap / 2;
    High = Coefs + Overlap / 2 - 1;
    for (INT32 Remaining = Overlap / 2; Remaining > 0; Remaining -= 1)
    {
        float Early = *Low;
        float Late = *High;
        float NextSin;
        float NextCos;

        /* The reference fuses the second product of each pair into the add */
        *Low = fmaf(-Late, Sin, Early * Cos);
        *High = fmaf(Early, Sin, Late * Cos);
        Low += 1;
        High -= 1;
        NextSin = fmaf(Channel->TurnStep, Cos, PrevSin);
        NextCos = fmaf(-Channel->TurnStep, Sin, PrevCos);
        PrevSin = Sin;
        PrevCos = Cos;
        Sin = NextSin;
        Cos = NextCos;
    }
}

/**
 * @brief Rounds Frames samples of every channel from the read position to 16 bits, interleaved.
 */
VOID
XaWmaStorePcm(
    _In_ const XaWmaDecoder* Decoder,
    _In_ UINT32 Frames,
    _Out_writes_(_Inexpressible_("Frames times channels")) SHORT* Output)
{
    UINT32 Channels = Decoder->Layout.Channels;

    for (UINT32 Channel = 0; Channel < Channels; Channel++)
    {
        const float* Source = Decoder->Channels[Channel].Pcm + Decoder->Read;

        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            float Sample = Source[Frame];
            INT32 Value;

            if (Sample >= 0.0f)
            {
                Value = static_cast<INT32>(Sample + 0.5f);
                if (Value > 32767)
                    Value = 32767;
            }
            else
            {
                Value = static_cast<INT32>(Sample - 0.5f);
                if (Value < -32768)
                    Value = -32768;
            }
            Output[Frame * Channels + Channel] = static_cast<SHORT>(Value);
        }
    }
}
