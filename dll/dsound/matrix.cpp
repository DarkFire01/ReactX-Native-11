/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Channel mixing matrices from speaker positions around the listener
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <math.h>

/* The speakers a channel mask can name, in mask bit order */
#define DS_SPEAKER_SLOTS    11

/* Low frequency and unplaced channels sit at a negative angle */
#define DS_NO_ANGLE         -90.0f

/* Speaker angles in degrees, clockwise from straight ahead */
static const float DsSpeakerAngle[DS_SPEAKER_SLOTS] =
{
    330.0f, 30.0f, 0.0f, DS_NO_ANGLE, 225.0f, 135.0f, 345.0f, 15.0f, 180.0f, 250.0f, 110.0f,
};

/* pi as the engine writes it, to ten digits */
static const double DsHalfTurn = 3.1415926535;

static
float
DsWrapDegrees(
    _In_ float Angle)
{
    return Angle - floorf(Angle / 360.0f) * 360.0f;
}

/* Stable ascending sort of angles that carries each one's original channel along */
static
VOID
DsSortAngles(
    _Inout_updates_(Count) float* Angles,
    _Inout_updates_(Count) UINT* Channels,
    _In_ UINT Count)
{
    for (UINT Index = 1; Index < Count; Index++)
    {
        float Angle = Angles[Index];
        UINT Channel = Channels[Index];
        UINT Slot = Index;

        while (Slot > 0 && Angles[Slot - 1] > Angle)
        {
            Angles[Slot] = Angles[Slot - 1];
            Channels[Slot] = Channels[Slot - 1];
            Slot -= 1;
        }
        Angles[Slot] = Angle;
        Channels[Slot] = Channel;
    }
}

/* Pans every placed input between the two output speakers around it with constant power */
static
HRESULT
DsMatrixFromAngles(
    _In_ UINT InCount,
    _In_ UINT OutCount,
    _Inout_updates_(InCount) float* InAngles,
    _Inout_updates_(OutCount) float* OutAngles,
    _Out_writes_(InCount * OutCount) float* Matrix)
{
    UINT InChannel[DS_MAX_CHANNELS];
    UINT OutChannel[DS_MAX_CHANNELS];
    float Weight[DS_MAX_CHANNELS] = {};
    UINT FirstIn = InCount;
    UINT FirstOut = OutCount;
    UINT Placed;
    float Loudest = 0.0f;

    for (UINT Index = 0; Index < InCount; Index++)
        InChannel[Index] = Index;
    for (UINT Index = 0; Index < OutCount; Index++)
        OutChannel[Index] = Index;
    DsSortAngles(InAngles, InChannel, InCount);
    DsSortAngles(OutAngles, OutChannel, OutCount);

    for (UINT Index = 0; Index < InCount; Index++)
    {
        if (InAngles[Index] >= 0.0f)
        {
            FirstIn = Index;
            break;
        }
    }
    for (UINT Index = 0; Index < OutCount; Index++)
    {
        if (OutAngles[Index] >= 0.0f)
        {
            FirstOut = Index;
            break;
        }
    }

    ZeroMemory(Matrix, InCount * OutCount * sizeof(*Matrix));
    Placed = OutCount - FirstOut;
    if (Placed == 0)
        return S_OK;

    for (UINT In = FirstIn; In < InCount; In++)
    {
        UINT Above = FirstOut;
        UINT Right;
        UINT Left;
        float Span;

        while (Above < OutCount && InAngles[In] > OutAngles[Above])
            Above++;
        Right = FirstOut + (Above - FirstOut) % Placed;
        Left = FirstOut + (Above - FirstOut + Placed - 1) % Placed;
        Span = DsWrapDegrees(OutAngles[Right] - OutAngles[Left]);

        if (Left == Right)
        {
            Matrix[OutChannel[Left] * InCount + InChannel[In]] = 1.0f;
            Weight[OutChannel[Left]] += 1.0f;
            continue;
        }
        if (Span == 0.0f)
            return E_INVALIDARG;

        {
            float Share = DsWrapDegrees(InAngles[In] - OutAngles[Left]) / Span;
            double Turn = static_cast<double>(Share) * DsHalfTurn * 0.5;
            float LeftGain = static_cast<float>(cos(Turn));
            float RightGain = static_cast<float>(sin(Turn));

            if (OutCount == 1)
            {
                LeftGain = 1.0f;
                RightGain = 1.0f;
            }
            else
            {
                LeftGain = DsMax(LeftGain, 0.0f);
                RightGain = DsMax(RightGain, 0.0f);
            }
            Matrix[OutChannel[Left] * InCount + InChannel[In]] = LeftGain;
            Matrix[OutChannel[Right] * InCount + InChannel[In]] = RightGain;
            Weight[OutChannel[Left]] += LeftGain;
            Weight[OutChannel[Right]] += RightGain;
        }
    }

    /* Scale so the busiest output sums to one, kept to three decimals */
    for (UINT Out = 0; Out < OutCount; Out++)
        Loudest = DsMax(Loudest, Weight[Out]);
    if (Loudest == 0.0f)
        return S_OK;
    for (UINT Index = 0; Index < InCount * OutCount; Index++)
        Matrix[Index] = static_cast<float>(static_cast<int>(Matrix[Index] / Loudest * 1000.0f + 0.5f)) / 1000.0f;
    return S_OK;
}

/* A stream with back speakers and no side ones plays its back pair at the side position, and the reverse */
static
VOID
DsMatchSideAndBack(
    _In_ DWORD InMask,
    _In_ DWORD OutMask,
    _Inout_updates_(DS_SPEAKER_SLOTS) float* Angles)
{
    static const struct
    {
        DWORD Back;
        DWORD Side;
        UINT BackSlot;
        UINT SideSlot;
    } Pairs[2] =
    {
        { SPEAKER_BACK_LEFT, SPEAKER_SIDE_LEFT, 4, 9 },
        { SPEAKER_BACK_RIGHT, SPEAKER_SIDE_RIGHT, 5, 10 },
    };

    for (UINT Index = 0; Index < 2; Index++)
    {
        BOOLEAN InBackOnly = (InMask & Pairs[Index].Back) && !(InMask & Pairs[Index].Side);
        BOOLEAN InSideOnly = (InMask & Pairs[Index].Side) && !(InMask & Pairs[Index].Back);
        BOOLEAN OutBackOnly = (OutMask & Pairs[Index].Back) && !(OutMask & Pairs[Index].Side);
        BOOLEAN OutSideOnly = (OutMask & Pairs[Index].Side) && !(OutMask & Pairs[Index].Back);

        if ((InBackOnly && OutSideOnly) || (InSideOnly && OutBackOnly))
            Angles[Pairs[Index].SideSlot] = Angles[Pairs[Index].BackSlot];
    }
}

HRESULT
DsBuildChannelMatrix(
    _In_ UINT InputChannels,
    _In_ UINT OutputChannels,
    _In_ DWORD InputMask,
    _In_ DWORD OutputMask,
    _Out_writes_(InputChannels * OutputChannels) float* Matrix)
{
    float Angles[DS_SPEAKER_SLOTS];
    float InAngles[DS_MAX_CHANNELS];
    float OutAngles[DS_MAX_CHANNELS];
    UINT InCount = 0;
    UINT OutCount = 0;
    HRESULT hr;

    if (InputChannels > DS_MAX_CHANNELS || OutputChannels > DS_MAX_CHANNELS)
        return E_INVALIDARG;

    memcpy(Angles, DsSpeakerAngle, sizeof(Angles));
    DsMatchSideAndBack(InputMask, OutputMask, Angles);

    for (UINT Slot = 0; Slot < DS_SPEAKER_SLOTS; Slot++)
    {
        DWORD Bit = 1UL << Slot;

        if (InputMask & Bit)
        {
            if (InCount == InputChannels)
                return E_INVALIDARG;
            InAngles[InCount++] = Angles[Slot];
        }
        if (OutputMask & Bit)
        {
            if (OutCount == OutputChannels)
                return E_INVALIDARG;
            OutAngles[OutCount++] = Angles[Slot];
        }
    }
    while (InCount < InputChannels)
        InAngles[InCount++] = DS_NO_ANGLE;
    while (OutCount < OutputChannels)
        OutAngles[OutCount++] = DS_NO_ANGLE;

    hr = DsMatrixFromAngles(InputChannels, OutputChannels, InAngles, OutAngles, Matrix);
    if (FAILED(hr))
        return hr;

    /* The low frequency channel goes straight across, or is spread over every output */
    {
        UINT InIndex = 0;
        UINT OutIndex = 0;

        for (UINT Slot = 0; Slot < DS_SPEAKER_SLOTS; Slot++)
        {
            DWORD Bit = 1UL << Slot;

            if (Angles[Slot] < 0.0f && (InputMask & Bit))
            {
                if (OutputMask & Bit)
                {
                    Matrix[OutIndex * InputChannels + InIndex] = 1.0f;
                }
                else if (Bit == SPEAKER_LOW_FREQUENCY)
                {
                    float Share = 1.0f / static_cast<float>(InputChannels - 1);
                    float Total = static_cast<float>(OutputChannels) + Share;

                    for (UINT Out = 0; Out < OutputChannels; Out++)
                    {
                        for (UINT In = 0; In < InputChannels; In++)
                        {
                            float* Cell = &Matrix[Out * InputChannels + In];

                            if (In == InIndex)
                                *Cell = Share / Total;
                            else
                                *Cell = *Cell * static_cast<float>(OutputChannels) / Total;
                        }
                    }
                }
                else
                {
                    return E_NOTIMPL;
                }
            }
            else if (Angles[Slot] < 0.0f && (OutputMask & Bit) && Bit != SPEAKER_LOW_FREQUENCY)
            {
                return E_NOTIMPL;
            }

            if (InputMask & Bit)
                InIndex++;
            if (OutputMask & Bit)
                OutIndex++;
        }
    }
    return S_OK;
}
