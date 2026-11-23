/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The linear interpolating rate converter every voice uses
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

#include <math.h>

/* The most frames of history a block can leave behind */
#define XA_HISTORY_FRAMES   4

XaResampler*
XaResamplerCreate(
    _In_ UINT32 Channels,
    _In_ UINT32 InputRate,
    _In_ UINT32 OutputRate,
    _In_ BOOLEAN FixedRatio)
{
    XaResampler* Resampler = new (std::nothrow) XaResampler;

    if (Resampler == nullptr)
        return nullptr;
    memset(Resampler, 0, sizeof(*Resampler));
    Resampler->History = new (std::nothrow) float[static_cast<SIZE_T>(Channels) * XA_HISTORY_FRAMES];
    if (Resampler->History == nullptr)
    {
        delete Resampler;
        return nullptr;
    }
    Resampler->Channels = Channels;
    Resampler->InputRate = InputRate;
    Resampler->OutputRate = OutputRate;
    Resampler->BaseStep = static_cast<float>(InputRate) / static_cast<float>(OutputRate);
    Resampler->Step = Resampler->BaseStep;
    Resampler->FixedRatio = FixedRatio;
    XaResamplerReset(Resampler);
    return Resampler;
}

VOID
XaResamplerFree(
    _In_opt_ XaResampler* Resampler)
{
    if (Resampler == nullptr)
        return;
    delete[] Resampler->History;
    delete Resampler;
}

VOID
XaResamplerReset(
    _Inout_ XaResampler* Resampler)
{
    Resampler->Fraction = 0.0f;
    Resampler->Position = 0;
    Resampler->HistoryFrames = 1;
    memset(Resampler->History, 0, sizeof(float) * Resampler->Channels * XA_HISTORY_FRAMES);
}

VOID
XaResamplerSetRatio(
    _Inout_ XaResampler* Resampler,
    _In_ float Ratio)
{
    Resampler->Step = Resampler->BaseStep * Ratio;
}

UINT32
XaResamplerInputFrames(
    _Inout_ XaResampler* Resampler,
    _In_ UINT32 OutputFrames)
{
    float Span;

    if (Resampler->Step == 1.0f)
    {
        Resampler->Fraction = 0.0f;
        Resampler->Position = 0;
        return OutputFrames;
    }
    Span = static_cast<float>(OutputFrames) * Resampler->Step;
    if (Resampler->FixedRatio)
        return static_cast<UINT32>(Span + 0.5f);
    return static_cast<UINT32>(Resampler->Fraction + Span) + static_cast<UINT32>(Resampler->Position) + 1;
}

/* Reads frame Index of the block, where negative frames come from the previous block */
static
const float*
XaResamplerFrame(
    _In_ const XaResampler* Resampler,
    _In_reads_(InputFrames * Resampler->Channels) const float* Input,
    _In_ UINT32 InputFrames,
    _In_ INT64 Index)
{
    UNREFERENCED_PARAMETER(InputFrames);
    if (Index >= 0)
        return Input + static_cast<SIZE_T>(Index) * Resampler->Channels;
    return Resampler->History + static_cast<SIZE_T>(Resampler->HistoryFrames + Index) * Resampler->Channels;
}

VOID
XaResamplerProcess(
    _Inout_ XaResampler* Resampler,
    _In_reads_(InputFrames * Resampler->Channels) const float* Input,
    _In_ UINT32 InputFrames,
    _In_reads_opt_(InputFrames * Resampler->Channels) const SHORT* Raw,
    _Out_writes_(OutputFrames * Resampler->Channels) float* Output,
    _In_ UINT32 OutputFrames)
{
    UINT32 Channels = Resampler->Channels;
    INT64 Whole;
    float Part;
    float Fraction;
    INT64 Index;
    INT64 Kept;

    if (Resampler->Step == 1.0f)
    {
        memcpy(Output, Input, sizeof(float) * OutputFrames * Channels);
        if (OutputFrames != 0)
        {
            memcpy(Resampler->History, Output + static_cast<SIZE_T>(OutputFrames - 1) * Channels, sizeof(float) * Channels);
            Resampler->HistoryFrames = 1;
        }
        return;
    }

    Whole = static_cast<INT64>(Resampler->Step);
    Part = Resampler->Step - static_cast<float>(Whole);
    Fraction = Resampler->Fraction;
    Index = Resampler->Position;
    for (UINT32 Frame = 0; Frame < OutputFrames; Frame++)
    {
        if (Raw != nullptr && Index >= 1)
        {
            /* 16 bit input interpolates on the integers and scales afterwards */
            const SHORT* Before = Raw + static_cast<SIZE_T>(Index - 1) * Channels;
            const SHORT* After = Before + Channels;

            for (UINT32 Channel = 0; Channel < Channels; Channel++)
            {
                float Rise = static_cast<float>(After[Channel] - Before[Channel]);

                Output[Frame * Channels + Channel] = fmaf(Rise, Fraction, static_cast<float>(Before[Channel])) * XA_INT16_SCALE;
            }
        }
        else
        {
            const float* Before = XaResamplerFrame(Resampler, Input, InputFrames, Index - 1);
            const float* After = XaResamplerFrame(Resampler, Input, InputFrames, Index);

            for (UINT32 Channel = 0; Channel < Channels; Channel++)
                Output[Frame * Channels + Channel] = fmaf(After[Channel] - Before[Channel], Fraction, Before[Channel]);
        }
        Fraction = Part + Fraction;
        if (Fraction < 1.0f)
        {
            Index += Whole;
        }
        else
        {
            Fraction = Fraction - 1.0f;
            Index += Whole + 1;
        }
    }
    Resampler->Fraction = Fraction;

    /* The next block starts reading from here, keeping the frame before it */
    if (Index < static_cast<INT64>(InputFrames) - 1)
        Index = static_cast<INT64>(InputFrames) - 1;
    Index -= InputFrames;
    Kept = 1 - Index;
    if (Kept > XA_HISTORY_FRAMES)
        Kept = XA_HISTORY_FRAMES;
    {
        float Saved[XA_HISTORY_FRAMES * XA_MAX_CHANNELS];

        for (INT64 Frame = 0; Frame < Kept; Frame++)
        {
            const float* Source = XaResamplerFrame(Resampler, Input, InputFrames, InputFrames - Kept + Frame);

            memcpy(Saved + Frame * Channels, Source, sizeof(float) * Channels);
        }
        memcpy(Resampler->History, Saved, sizeof(float) * Channels * static_cast<SIZE_T>(Kept));
    }
    Resampler->HistoryFrames = static_cast<UINT32>(Kept);
    Resampler->Position = Index;
    if (Resampler->FixedRatio)
    {
        Resampler->Fraction = 0.0f;
        Resampler->Position = 0;
    }
}
