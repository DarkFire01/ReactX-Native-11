/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The state variable and one pole filters on voices and sends
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

#include <math.h>

/* A low pass closed all the way or a high pass open all the way lets nothing through */
BOOLEAN
XaFilterSilences(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters)
{
    if ((Parameters->Type == LowPassFilter || Parameters->Type == LowPassOnePoleFilter) && Parameters->Frequency == 0.0f)
        return TRUE;
    return (Parameters->Type == HighPassFilter || Parameters->Type == HighPassOnePoleFilter) && Parameters->Frequency == 1.0f;
}

/* Filters interleaved frames in place; State holds the low and band outputs of each channel */
VOID
XaFilterRun(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
    _Inout_updates_(2 * Channels) float* State,
    _In_reads_(Frames * Channels) const float* Input,
    _Out_writes_(Frames * Channels) float* Output,
    _In_ UINT32 Frames,
    _In_ UINT32 Channels)
{
    float Coefficient = Parameters->Frequency;
    float Damping = Parameters->OneOverQ;
    float Remainder = 1.0f - Coefficient;

    for (UINT32 Channel = 0; Channel < Channels; Channel++)
    {
        float Low = State[Channel];
        float Band = State[Channels + Channel];

        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            SIZE_T Index = static_cast<SIZE_T>(Frame) * Channels + Channel;
            float Sample = Input[Index];
            float High;

            switch (Parameters->Type)
            {
                case LowPassOnePoleFilter:
                    Low = fmaf(Remainder, Low, Sample * Coefficient);
                    Output[Index] = Low;
                    break;
                case HighPassOnePoleFilter:
                    Low = fmaf(Remainder, Low, Sample * Coefficient);
                    Output[Index] = Sample - Low;
                    break;
                default:
                    High = fmaf(-Damping, Band, Sample - Low);
                    Band = fmaf(High, Coefficient, Band);
                    Low = fmaf(Band, Coefficient, Low);
                    if (Parameters->Type == LowPassFilter)
                        Output[Index] = Low;
                    else if (Parameters->Type == BandPassFilter)
                        Output[Index] = Band;
                    else if (Parameters->Type == HighPassFilter)
                        Output[Index] = High;
                    else
                        Output[Index] = Low + High;
                    break;
            }
        }
        State[Channel] = Low;
        State[Channels + Channel] = Band;
    }
}
