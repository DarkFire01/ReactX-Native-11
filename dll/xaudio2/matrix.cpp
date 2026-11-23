/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Default send matrices from the speaker layouts on both ends
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

#include <math.h>

/* Azimuth of each speaker position in mask order; below zero means it has no direction */
static const float XaSpeakerAngles[11] =
{
    330.0f, 30.0f, 0.0f, -360.0f, 225.0f, 135.0f, 345.0f, 15.0f, 180.0f, 250.0f, 110.0f,
};

#define XA_NO_DIRECTION     -360.0f
#define XA_LFE_POSITION     3

/* Center and surrounds folded in at -3 dB */
#define XA_HALF_POWER       0.70794576f

static const float XaFiveOneToStereo[2 * 6] =
{
    1.0f, 0.0f, XA_HALF_POWER, 0.0f, XA_HALF_POWER, 0.0f,
    0.0f, 1.0f, XA_HALF_POWER, 0.0f, 0.0f, XA_HALF_POWER,
};

static const float XaSevenOneToStereo[2 * 8] =
{
    1.0f, 0.0f, XA_HALF_POWER, 0.0f, XA_HALF_POWER, 0.0f, XA_HALF_POWER, 0.0f,
    0.0f, 1.0f, XA_HALF_POWER, 0.0f, 0.0f, XA_HALF_POWER, 0.0f, XA_HALF_POWER,
};

/* The front center pair folds into the fronts, the sides spread over the rears */
static const float XaSevenOneToFiveOne[6 * 8] =
{
    1.0f, 0.0f, 0.0f, 0.0f, 0.38268343f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.38268343f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.90096885f, 0.43388373f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.43388373f, 0.90096885f,
};

static
BOOLEAN
XaIsFiveOne(
    _In_ DWORD Mask)
{
    return Mask == KSAUDIO_SPEAKER_5POINT1 || Mask == KSAUDIO_SPEAKER_5POINT1_SURROUND;
}

static
BOOLEAN
XaIsSevenOne(
    _In_ DWORD Mask)
{
    return Mask == KSAUDIO_SPEAKER_7POINT1 || Mask == KSAUDIO_SPEAKER_7POINT1_SURROUND;
}

static
float
XaWrapDegrees(
    _In_ float Angle)
{
    return Angle - floorf(Angle / 360.0f) * 360.0f;
}

/* Sorts the angles upward and carries each channel's index along, keeping equal angles in order */
static
VOID
XaSortAngles(
    _Inout_updates_(Count) float* Angles,
    _Inout_updates_(Count) UINT32* Channels,
    _In_ UINT32 Count)
{
    BOOLEAN Swapped = TRUE;

    while (Swapped && Count > 1)
    {
        Swapped = FALSE;
        for (UINT32 Index = 0; Index + 1 < Count; Index++)
        {
            if (Angles[Index] > Angles[Index + 1])
            {
                float Angle = Angles[Index];
                UINT32 Channel = Channels[Index];

                Angles[Index] = Angles[Index + 1];
                Angles[Index + 1] = Angle;
                Channels[Index] = Channels[Index + 1];
                Channels[Index + 1] = Channel;
                Swapped = TRUE;
            }
        }
    }
}

static
UINT32
XaFirstDirectional(
    _In_reads_(Count) const float* Angles,
    _In_ UINT32 Count)
{
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        if (Angles[Index] >= 0.0f)
            return Index;
    }
    return Count;
}

/* Pans every directional input between the two output speakers either side of it */
static
HRESULT
XaPanByAngles(
    _In_ UINT32 InputChannels,
    _In_ UINT32 OutputChannels,
    _Inout_updates_(InputChannels) float* InputAngles,
    _Inout_updates_(OutputChannels) float* OutputAngles,
    _Out_writes_(InputChannels * OutputChannels) float* Matrix)
{
    UINT32 InputOrder[XA_MAX_CHANNELS];
    UINT32 OutputOrder[XA_MAX_CHANNELS];
    float Loudness[XA_MAX_CHANNELS];
    UINT32 FirstInput;
    UINT32 FirstOutput;
    UINT32 Directional;
    float Loudest;

    for (UINT32 Index = 0; Index < InputChannels; Index++)
        InputOrder[Index] = Index;
    for (UINT32 Index = 0; Index < OutputChannels; Index++)
        OutputOrder[Index] = Index;
    XaSortAngles(InputAngles, InputOrder, InputChannels);
    XaSortAngles(OutputAngles, OutputOrder, OutputChannels);
    FirstInput = XaFirstDirectional(InputAngles, InputChannels);
    FirstOutput = XaFirstDirectional(OutputAngles, OutputChannels);

    memset(Matrix, 0, sizeof(*Matrix) * InputChannels * OutputChannels);
    memset(Loudness, 0, sizeof(Loudness));
    Directional = OutputChannels - FirstOutput;
    if (Directional == 0)
        return S_OK;

    for (UINT32 Input = FirstInput; Input < InputChannels; Input++)
    {
        UINT32 Above = FirstOutput;
        UINT32 After;
        UINT32 Before;
        UINT32 Column = InputOrder[Input];

        while (Above < OutputChannels && InputAngles[Input] > OutputAngles[Above])
            Above += 1;
        After = FirstOutput + (Above - FirstOutput) % Directional;
        Before = FirstOutput + (Above - FirstOutput + Directional - 1) % Directional;

        if (After == Before)
        {
            Matrix[OutputOrder[Before] * InputChannels + Column] = 1.0f;
            Loudness[OutputOrder[Before]] += 1.0f;
        }
        else
        {
            float Span = XaWrapDegrees(OutputAngles[After] - OutputAngles[Before]);
            float Offset = XaWrapDegrees(InputAngles[Input] - OutputAngles[Before]);
            float Toward;
            float Away;
            double Phase;

            if (Span == 0.0f)
                return E_INVALIDARG;

            /* Constant power between the pair */
            Phase = static_cast<double>(Offset / Span) * 3.141592653589793 * 0.5;
            Away = static_cast<float>(cos(Phase));
            Toward = static_cast<float>(sin(Phase));
            if (OutputChannels == 1)
            {
                Away = 1.0f;
                Toward = 1.0f;
            }
            else
            {
                Away = Away < 0.0f ? 0.0f : Away;
                Toward = Toward < 0.0f ? 0.0f : Toward;
            }
            Matrix[OutputOrder[Before] * InputChannels + Column] = Away;
            Matrix[OutputOrder[After] * InputChannels + Column] = Toward;
            Loudness[OutputOrder[Before]] += Away;
            Loudness[OutputOrder[After]] += Toward;
        }
    }

    /* Scale so the busiest speaker sits at unity, kept to three decimals */
    Loudest = 0.0f;
    for (UINT32 Output = 0; Output < OutputChannels; Output++)
        Loudest = Loudness[Output] > Loudest ? Loudness[Output] : Loudest;
    if (Loudest != 0.0f)
    {
        for (UINT32 Cell = 0; Cell < InputChannels * OutputChannels; Cell++)
        {
            int Rounded = static_cast<int>(Matrix[Cell] / Loudest * 1000.0f + 0.5f);

            Matrix[Cell] = static_cast<float>(Rounded) / 1000.0f;
        }
    }
    return S_OK;
}

/* Gives each channel of a mask its azimuth, with the rest left without a direction */
static
VOID
XaMaskAngles(
    _In_ DWORD Mask,
    _In_ UINT32 Channels,
    _Out_writes_(Channels) float* Angles)
{
    UINT32 Filled = 0;

    for (UINT32 Position = 0; Position < ARRAYSIZE(XaSpeakerAngles) && Filled < Channels; Position++)
    {
        if ((Mask & (1u << Position)) != 0)
            Angles[Filled++] = XaSpeakerAngles[Position];
    }
    while (Filled < Channels)
        Angles[Filled++] = XA_NO_DIRECTION;
}

HRESULT
XaDefaultMatrix(
    _In_ UINT32 InputChannels,
    _In_ UINT32 OutputChannels,
    _In_ DWORD InputMask,
    _In_ DWORD OutputMask,
    _Out_writes_(InputChannels * OutputChannels) float* Matrix)
{
    float InputAngles[XA_MAX_CHANNELS];
    float OutputAngles[XA_MAX_CHANNELS];
    UINT32 Before;
    HRESULT hr;

    if (InputChannels == 0 || OutputChannels == 0 || InputChannels > XA_MAX_CHANNELS || OutputChannels > XA_MAX_CHANNELS)
        return E_INVALIDARG;
    if (InputMask == 0)
        InputMask = XaPseudoMask(InputChannels);
    if (OutputMask == 0)
        OutputMask = XaPseudoMask(OutputChannels);
    if (InputMask != 0 && XaMaskChannels(InputMask) != InputChannels)
        return E_INVALIDARG;
    if (OutputMask != 0 && XaMaskChannels(OutputMask) != OutputChannels)
        return E_INVALIDARG;

    /* Unknown layouts and 5.1 to 5.1 pass straight through */
    if (InputMask == 0 || OutputMask == 0 || (XaIsFiveOne(InputMask) && XaIsFiveOne(OutputMask)))
    {
        memset(Matrix, 0, sizeof(*Matrix) * InputChannels * OutputChannels);
        for (UINT32 Channel = 0; Channel < XaMin(InputChannels, OutputChannels); Channel++)
            Matrix[Channel * InputChannels + Channel] = 1.0f;
        return S_OK;
    }

    /* A center mono source goes to the first two outputs */
    if (InputMask == KSAUDIO_SPEAKER_MONO)
    {
        memset(Matrix, 0, sizeof(*Matrix) * OutputChannels);
        Matrix[0] = 1.0f;
        if (OutputChannels > 1)
            Matrix[1] = 1.0f;
        return S_OK;
    }

    if (InputChannels == 8 && OutputChannels == 6 && XaIsSevenOne(InputMask) && XaIsFiveOne(OutputMask))
    {
        memcpy(Matrix, XaSevenOneToFiveOne, sizeof(XaSevenOneToFiveOne));
        return S_OK;
    }
    if (OutputChannels == 2 && OutputMask == KSAUDIO_SPEAKER_STEREO)
    {
        if (InputChannels == 6 && XaIsFiveOne(InputMask))
        {
            memcpy(Matrix, XaFiveOneToStereo, sizeof(XaFiveOneToStereo));
            return S_OK;
        }
        if (InputChannels == 8 && XaIsSevenOne(InputMask))
        {
            memcpy(Matrix, XaSevenOneToStereo, sizeof(XaSevenOneToStereo));
            return S_OK;
        }
    }

    XaMaskAngles(InputMask, InputChannels, InputAngles);
    XaMaskAngles(OutputMask, OutputChannels, OutputAngles);
    hr = XaPanByAngles(InputChannels, OutputChannels, InputAngles, OutputAngles, Matrix);
    if (FAILED(hr))
        return hr;

    /* The LFE goes to the LFE, or is spread evenly when the output has none */
    if ((InputMask & (1u << XA_LFE_POSITION)) == 0)
        return S_OK;
    Before = XaMaskChannels(InputMask & ((1u << XA_LFE_POSITION) - 1));
    if ((OutputMask & (1u << XA_LFE_POSITION)) != 0)
    {
        UINT32 Row = XaMaskChannels(OutputMask & ((1u << XA_LFE_POSITION) - 1));

        Matrix[Row * InputChannels + Before] = 1.0f;
        return S_OK;
    }
    if (InputChannels == 1)
    {
        for (UINT32 Output = 0; Output < OutputChannels; Output++)
            Matrix[Output] = 1.0f / static_cast<float>(OutputChannels);
        return S_OK;
    }
    {
        float Share = 1.0f / static_cast<float>(InputChannels - 1);
        float Total = static_cast<float>(OutputChannels) + Share;

        for (UINT32 Output = 0; Output < OutputChannels; Output++)
        {
            for (UINT32 Input = 0; Input < InputChannels; Input++)
            {
                float* Cell = &Matrix[Output * InputChannels + Input];

                if (Input == Before)
                    *Cell = Share / Total;
                else
                    *Cell = *Cell * static_cast<float>(OutputChannels) / Total;
            }
        }
    }
    return S_OK;
}
