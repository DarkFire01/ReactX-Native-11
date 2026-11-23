/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     X3DAudio positional calculations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include <x3daudio.h>

#include <float.h>
#include <math.h>

#pragma fp_contract(off)

/* What X3DAudioInitialize keeps in the caller's handle bytes */
struct XaX3DHandle
{
    UINT32 ChannelMask;
    UINT32 ChannelCount;
    UINT32 LfeIndex;
    FLOAT32 SpeedOfSound;
    FLOAT32 SpeedLimit;
};

C_ASSERT(sizeof(XaX3DHandle) == X3DAUDIO_HANDLE_BYTESIZE);

#define XA_X3D_NO_LFE           0xFFFFFFFFu
#define XA_X3D_HALF_PI          (X3DAUDIO_PI * 0.5f)
#define XA_X3D_QUARTER_PI       (X3DAUDIO_PI * 0.25f)
#define XA_X3D_EIGHTH_PI        (X3DAUDIO_PI * 0.125f)
#define XA_X3D_BACK_PI          (X3DAUDIO_PI * 0.75f)

/* Matrix and cone flags that need the distance curves */
#define XA_X3D_CURVE_FLAGS      (X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_LPF_DIRECT | \
                                 X3DAUDIO_CALCULATE_LPF_REVERB | X3DAUDIO_CALCULATE_REVERB)
#define XA_X3D_LPF_FLAGS        (X3DAUDIO_CALCULATE_LPF_DIRECT | X3DAUDIO_CALCULATE_LPF_REVERB)

static const X3DAUDIO_DISTANCE_CURVE_POINT XaX3DLpfDirectPoints[] = { { 0.0f, 1.0f }, { 1.0f, 0.75f } };
static const X3DAUDIO_DISTANCE_CURVE_POINT XaX3DLpfReverbPoints[] = { { 0.0f, 0.75f }, { 1.0f, 0.75f } };
static const X3DAUDIO_DISTANCE_CURVE_POINT XaX3DReverbPoints[] = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };

/* Speaker azimuths in channel order, LFE left out; the second set also leaves out front center */
static const float XaX3DStereoAngles[] = { -XA_X3D_HALF_PI, XA_X3D_HALF_PI };
static const float XaX3DQuadAngles[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, -XA_X3D_BACK_PI, XA_X3D_BACK_PI };
static const float XaX3DSurroundAngles[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, 0.0f, X3DAUDIO_PI };
static const float XaX3DSurroundSideAngles[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, X3DAUDIO_PI };
static const float XaX3D51Angles[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, 0.0f, -XA_X3D_BACK_PI, XA_X3D_BACK_PI };
static const float XaX3D51SideAngles[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, 0.0f, -XA_X3D_HALF_PI, XA_X3D_HALF_PI };
static const float XaX3D51SideNoCenter[] = { -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, -XA_X3D_HALF_PI, XA_X3D_HALF_PI };
static const float XaX3D71Angles[] =
{
    -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, 0.0f, -XA_X3D_BACK_PI, XA_X3D_BACK_PI, -XA_X3D_EIGHTH_PI, XA_X3D_EIGHTH_PI
};
static const float XaX3D71NoCenter[] =
{
    -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, -XA_X3D_BACK_PI, XA_X3D_BACK_PI, -XA_X3D_EIGHTH_PI, XA_X3D_EIGHTH_PI
};
static const float XaX3D71SideAngles[] =
{
    -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, 0.0f, -XA_X3D_BACK_PI, XA_X3D_BACK_PI, -XA_X3D_HALF_PI, XA_X3D_HALF_PI
};
static const float XaX3D71SideNoCenter[] =
{
    -XA_X3D_QUARTER_PI, XA_X3D_QUARTER_PI, -XA_X3D_BACK_PI, XA_X3D_BACK_PI, -XA_X3D_HALF_PI, XA_X3D_HALF_PI
};

struct XaX3DLayout
{
    UINT32 Mask;
    const float* Angles;
    UINT32 Count;
    const float* CenterlessAngles;
    UINT32 CenterlessCount;
};

static const XaX3DLayout XaX3DLayouts[] =
{
    { SPEAKER_STEREO, XaX3DStereoAngles, 2, XaX3DStereoAngles, 2 },
    { SPEAKER_2POINT1, XaX3DStereoAngles, 2, XaX3DStereoAngles, 2 },
    { SPEAKER_QUAD, XaX3DQuadAngles, 4, XaX3DQuadAngles, 4 },
    { SPEAKER_4POINT1, XaX3DQuadAngles, 4, XaX3DQuadAngles, 4 },
    { SPEAKER_SURROUND, XaX3DSurroundAngles, 4, XaX3DSurroundSideAngles, 3 },
    { SPEAKER_5POINT1, XaX3D51Angles, 5, XaX3DQuadAngles, 4 },
    { SPEAKER_5POINT1_SURROUND, XaX3D51SideAngles, 5, XaX3D51SideNoCenter, 4 },
    { SPEAKER_7POINT1, XaX3D71Angles, 7, XaX3D71NoCenter, 6 },
    { SPEAKER_7POINT1_SURROUND, XaX3D71SideAngles, 7, XaX3D71SideNoCenter, 6 },
};

static
inline
float
XaX3DDot(
    _In_ const X3DAUDIO_VECTOR& A,
    _In_ const X3DAUDIO_VECTOR& B)
{
    return (A.y * B.y + A.x * B.x) + A.z * B.z;
}

static
inline
X3DAUDIO_VECTOR
XaX3DCross(
    _In_ const X3DAUDIO_VECTOR& A,
    _In_ const X3DAUDIO_VECTOR& B)
{
    X3DAUDIO_VECTOR Result;

    Result.x = A.y * B.z - A.z * B.y;
    Result.y = A.z * B.x - A.x * B.z;
    Result.z = A.x * B.y - A.y * B.x;
    return Result;
}

static
inline
X3DAUDIO_VECTOR
XaX3DSubtract(
    _In_ const X3DAUDIO_VECTOR& A,
    _In_ const X3DAUDIO_VECTOR& B)
{
    X3DAUDIO_VECTOR Result;

    Result.x = A.x - B.x;
    Result.y = A.y - B.y;
    Result.z = A.z - B.z;
    return Result;
}

static
inline
FLOAT32&
XaX3DCell(
    _In_ X3DAUDIO_DSP_SETTINGS* Settings,
    _In_ SIZE_T Source,
    _In_ SIZE_T Destination)
{
    return Settings->pMatrixCoefficients[Source + Destination * Settings->SrcChannelCount];
}

/* Linear interpolation over the segment holding the scaled distance */
static
float
XaX3DCurveValue(
    _In_reads_(Count) const X3DAUDIO_DISTANCE_CURVE_POINT* Points,
    _In_ UINT32 Count,
    _In_ float Distance)
{
    for (UINT32 Index = 1; Index < Count; Index++)
    {
        const X3DAUDIO_DISTANCE_CURVE_POINT& Low = Points[Index - 1];
        const X3DAUDIO_DISTANCE_CURVE_POINT& High = Points[Index];

        if (High.Distance > Distance)
            return Low.DSPSetting + ((High.DSPSetting - Low.DSPSetting) / (High.Distance - Low.Distance)) * (Distance - Low.Distance);
    }
    return Points[Count - 1].DSPSetting;
}

static
float
XaX3DCurveOrDefault(
    _In_opt_ const X3DAUDIO_DISTANCE_CURVE* Curve,
    _In_reads_(2) const X3DAUDIO_DISTANCE_CURVE_POINT* Fallback,
    _In_ float Distance)
{
    if (Curve == nullptr)
        return XaX3DCurveValue(Fallback, 2, Distance);
    return XaX3DCurveValue(Curve->pPoints, Curve->PointCount, Distance);
}

/* Volume curves without points fall off with the inverse of the distance past one */
static
float
XaX3DLevelCurve(
    _In_opt_ const X3DAUDIO_DISTANCE_CURVE* Curve,
    _In_ float Distance)
{
    if (Curve != nullptr)
        return XaX3DCurveValue(Curve->pPoints, Curve->PointCount, Distance);
    if (Distance > 1.0f)
        return 1.0f / Distance;
    return 1.0f;
}

static
VOID
XaX3DClearColumn(
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings,
    _In_ UINT32 Source)
{
    for (UINT32 Destination = 0; Destination < Settings->DstChannelCount; Destination++)
        XaX3DCell(Settings, Source, Destination) = 0.0f;
}

static
VOID
XaX3DApplyCurves(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    float Scaled = Settings->EmitterToListenerDistance / Emitter->CurveDistanceScaler;
    float Volume;
    float LfeLevel = 0.0f;
    BOOLEAN LfeTaken = FALSE;

    if (Flags & X3DAUDIO_CALCULATE_LPF_DIRECT)
        Settings->LPFDirectCoefficient = XaX3DCurveOrDefault(Emitter->pLPFDirectCurve, XaX3DLpfDirectPoints, Scaled);
    if (Flags & X3DAUDIO_CALCULATE_LPF_REVERB)
        Settings->LPFReverbCoefficient = XaX3DCurveOrDefault(Emitter->pLPFReverbCurve, XaX3DLpfReverbPoints, Scaled);
    if (Flags & X3DAUDIO_CALCULATE_REVERB)
        Settings->ReverbLevel = XaX3DCurveOrDefault(Emitter->pReverbCurve, XaX3DReverbPoints, Scaled);
    if (!(Flags & X3DAUDIO_CALCULATE_MATRIX))
        return;

    Volume = XaX3DLevelCurve(Emitter->pVolumeCurve, Scaled);
    if (Emitter->ChannelCount == 1)
    {
        Settings->pMatrixCoefficients[0] = Volume;
        for (UINT32 Destination = 1; Destination < Settings->DstChannelCount; Destination++)
            XaX3DCell(Settings, 0, Destination) = 0.0f;
        if (Flags & X3DAUDIO_CALCULATE_REDIRECT_TO_LFE)
            Settings->pMatrixCoefficients[Handle->LfeIndex] = XaX3DLevelCurve(Emitter->pLFECurve, Scaled);
        return;
    }

    /* Row zero holds each channel's level until the panning spreads it */
    for (UINT32 Channel = 0; Channel < Emitter->ChannelCount; Channel++)
    {
        XaX3DClearColumn(Settings, Channel);
        if (Emitter->pChannelAzimuths[Channel] != X3DAUDIO_2PI)
        {
            Settings->pMatrixCoefficients[Channel] = Volume;
            continue;
        }
        if (Handle->LfeIndex == XA_X3D_NO_LFE)
            continue;
        if (!LfeTaken)
        {
            LfeLevel = XaX3DLevelCurve(Emitter->pLFECurve, Scaled);
            LfeTaken = TRUE;
        }
        XaX3DCell(Settings, Channel, Handle->LfeIndex) = LfeLevel;
    }

    if (!LfeTaken && (Flags & X3DAUDIO_CALCULATE_REDIRECT_TO_LFE))
    {
        LfeLevel = XaX3DLevelCurve(Emitter->pLFECurve, Scaled);
        LfeLevel = LfeLevel / static_cast<float>(Settings->SrcChannelCount);
        for (UINT32 Source = 0; Source < Settings->SrcChannelCount; Source++)
            XaX3DCell(Settings, Source, Handle->LfeIndex) = LfeLevel;
    }
}

/* Volume, filter and reverb factors for a direction measured against a cone */
static
VOID
XaX3DConeFactors(
    _In_ const X3DAUDIO_CONE* Cone,
    _In_ const X3DAUDIO_VECTOR& Axis,
    _In_ const X3DAUDIO_VECTOR& Offset,
    _In_ float Distance,
    _Out_writes_(3) float* Factors)
{
    float Facing;
    float InsideEdge;
    float OutsideEdge;
    float Blend;

    if (Cone->OuterAngle == 0.0f)
    {
        Factors[0] = Cone->OuterVolume;
        Factors[1] = Cone->OuterLPF;
        Factors[2] = Cone->OuterReverb;
        return;
    }
    if (Cone->InnerAngle == X3DAUDIO_2PI || Distance <= FLT_EPSILON)
    {
        Factors[0] = Cone->InnerVolume;
        Factors[1] = Cone->InnerLPF;
        Factors[2] = Cone->InnerReverb;
        return;
    }

    Facing = XaX3DDot(Axis, Offset) / Distance;
    InsideEdge = cosf(Cone->InnerAngle * 0.5f);
    if (Facing >= InsideEdge)
    {
        Factors[0] = Cone->InnerVolume;
        Factors[1] = Cone->InnerLPF;
        Factors[2] = Cone->InnerReverb;
        return;
    }
    OutsideEdge = cosf(Cone->OuterAngle * 0.5f);
    if (Facing <= OutsideEdge)
    {
        Factors[0] = Cone->OuterVolume;
        Factors[1] = Cone->OuterLPF;
        Factors[2] = Cone->OuterReverb;
        return;
    }
    Blend = (Facing - InsideEdge) / (OutsideEdge - InsideEdge);
    Factors[0] = (Cone->OuterVolume - Cone->InnerVolume) * Blend + Cone->InnerVolume;
    Factors[1] = (Cone->OuterLPF - Cone->InnerLPF) * Blend + Cone->InnerLPF;
    Factors[2] = (Cone->OuterReverb - Cone->InnerReverb) * Blend + Cone->InnerReverb;
}

/* The emitter cone scales the mono level and its LFE send, and scales both filters */
static
VOID
XaX3DEmitterCone(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    X3DAUDIO_VECTOR Offset = XaX3DSubtract(Listener->Position, Emitter->Position);
    float Factors[3];

    XaX3DConeFactors(Emitter->pCone, Emitter->OrientFront, Offset, Settings->EmitterToListenerDistance, Factors);
    if (Flags & X3DAUDIO_CALCULATE_MATRIX)
    {
        Settings->pMatrixCoefficients[0] = Settings->pMatrixCoefficients[0] * Factors[0];
        if (Handle->LfeIndex != XA_X3D_NO_LFE)
            Settings->pMatrixCoefficients[Handle->LfeIndex] = Settings->pMatrixCoefficients[Handle->LfeIndex] * Factors[0];
    }
    if (Flags & X3DAUDIO_CALCULATE_LPF_DIRECT)
        Settings->LPFDirectCoefficient = Settings->LPFDirectCoefficient * Factors[1];
    if (Flags & X3DAUDIO_CALCULATE_LPF_REVERB)
        Settings->LPFReverbCoefficient = Settings->LPFReverbCoefficient * Factors[1];
    if (Flags & X3DAUDIO_CALCULATE_REVERB)
        Settings->ReverbLevel = Settings->ReverbLevel * Factors[2];
}

/* The listener cone subtracts its filter values instead, never going below zero */
static
VOID
XaX3DListenerCone(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    X3DAUDIO_VECTOR Offset = XaX3DSubtract(Emitter->Position, Listener->Position);
    FLOAT32* Matrix = Settings->pMatrixCoefficients;
    float Factors[3];

    XaX3DConeFactors(Listener->pCone, Listener->OrientFront, Offset, Settings->EmitterToListenerDistance, Factors);
    if (Flags & X3DAUDIO_CALCULATE_MATRIX)
    {
        for (UINT32 Channel = 0; Channel < Emitter->ChannelCount; Channel++)
            Matrix[Channel] = Matrix[Channel] * Factors[0];
        if (Handle->LfeIndex != XA_X3D_NO_LFE)
        {
            if (Emitter->ChannelCount == 1)
            {
                Matrix[Handle->LfeIndex] = Matrix[Handle->LfeIndex] * Factors[0];
            }
            else
            {
                for (UINT32 Channel = 0; Channel < Emitter->ChannelCount; Channel++)
                {
                    if (Emitter->pChannelAzimuths[Channel] == X3DAUDIO_2PI)
                        XaX3DCell(Settings, Channel, Handle->LfeIndex) = XaX3DCell(Settings, Channel, Handle->LfeIndex) * Factors[0];
                }
            }
        }
    }
    if (Flags & X3DAUDIO_CALCULATE_LPF_DIRECT)
    {
        Settings->LPFDirectCoefficient = Settings->LPFDirectCoefficient - Factors[1];
        if (Settings->LPFDirectCoefficient < 0.0f)
            Settings->LPFDirectCoefficient = 0.0f;
    }
    if (Flags & X3DAUDIO_CALCULATE_LPF_REVERB)
    {
        Settings->LPFReverbCoefficient = Settings->LPFReverbCoefficient - Factors[1];
        if (Settings->LPFReverbCoefficient < 0.0f)
            Settings->LPFReverbCoefficient = 0.0f;
    }
    if (Flags & X3DAUDIO_CALCULATE_REVERB)
        Settings->ReverbLevel = Settings->ReverbLevel * Factors[2];
}

static
const XaX3DLayout*
XaX3DFindLayout(
    _In_ UINT32 Mask)
{
    for (UINT32 Index = 0; Index < ARRAYSIZE(XaX3DLayouts); Index++)
    {
        if (XaX3DLayouts[Index].Mask == Mask)
            return &XaX3DLayouts[Index];
    }
    return nullptr;
}

/* Pans a level between the two speakers around an azimuth, tangent law */
static
VOID
XaX3DPan(
    _In_ const XaX3DHandle* Handle,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings,
    _In_ UINT32 Source,
    _In_ float Level,
    _In_ float Azimuth)
{
    BOOLEAN NoCenter = (Flags & X3DAUDIO_CALCULATE_ZEROCENTER) != 0;
    const XaX3DLayout* Layout;
    const float* Angles = nullptr;
    UINT32 Count = 0;
    INT32 Left = -1;
    INT32 Right = -1;
    float LeftGap = -4.0f;
    float RightGap = 4.0f;
    float Split;
    float Share;

    if (Handle->ChannelMask == SPEAKER_MONO)
    {
        Settings->pMatrixCoefficients[Source] = Settings->pMatrixCoefficients[Source] + Level;
        return;
    }
    Layout = XaX3DFindLayout(Handle->ChannelMask);
    if (Layout != nullptr)
    {
        Angles = NoCenter ? Layout->CenterlessAngles : Layout->Angles;
        Count = NoCenter ? Layout->CenterlessCount : Layout->Count;
    }

    /* Nearest speaker on each side, the left one at or below the azimuth */
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        float Gap = Azimuth - Angles[Index];

        if (Gap > X3DAUDIO_PI)
            Gap = Gap - X3DAUDIO_2PI;
        else if (Gap < -X3DAUDIO_PI)
            Gap = Gap + X3DAUDIO_2PI;
        if (Gap <= 0.0f && Gap > LeftGap)
        {
            Left = static_cast<INT32>(Index);
            LeftGap = Gap;
        }
        else if (Gap >= 0.0f && Gap < RightGap)
        {
            Right = static_cast<INT32>(Index);
            RightGap = Gap;
        }
    }
    if (LeftGap < 0.0f)
        LeftGap = -LeftGap;
    if (RightGap < 0.0f)
        RightGap = -RightGap;
    if (Left == -1 || Right == -1)
    {
        Left = 0;
        Right = 0;
    }

    /* Move table positions onto destination channels past center and LFE */
    if (NoCenter)
    {
        if (Left > 1)
            Left += 1;
        if (Right > 1)
            Right += 1;
    }
    if (Handle->LfeIndex != XA_X3D_NO_LFE)
    {
        INT32 Before = (Handle->ChannelMask & SPEAKER_FRONT_CENTER) ? 2 : 1;

        if (Left > Before)
            Left += 1;
        if (Right > Before)
            Right += 1;
    }

    if (LeftGap < FLT_EPSILON)
    {
        XaX3DCell(Settings, Source, Left) = XaX3DCell(Settings, Source, Left) + Level;
        return;
    }
    if (RightGap < FLT_EPSILON)
    {
        XaX3DCell(Settings, Source, Right) = XaX3DCell(Settings, Source, Right) + Level;
        return;
    }
    Split = tanf((LeftGap * XA_X3D_HALF_PI) / (RightGap + LeftGap));
    Share = Level / (Split + 1.0f);
    XaX3DCell(Settings, Source, Left) = XaX3DCell(Settings, Source, Left) + Share;
    XaX3DCell(Settings, Source, Right) = XaX3DCell(Settings, Source, Right) + Split * Share;
}

/* Speakers an even spread reaches: all but LFE, and front center when it is zeroed */
static
UINT32
XaX3DSpreadCount(
    _In_ const XaX3DHandle* Handle,
    _In_ UINT32 Flags,
    _In_ const X3DAUDIO_DSP_SETTINGS* Settings)
{
    UINT32 Count = Settings->DstChannelCount;

    if (Handle->LfeIndex != XA_X3D_NO_LFE)
        Count -= 1;
    if (Flags & X3DAUDIO_CALCULATE_ZEROCENTER)
        Count -= 1;
    return Count;
}

static
inline
BOOLEAN
XaX3DSpreadSkips(
    _In_ const XaX3DHandle* Handle,
    _In_ UINT32 Flags,
    _In_ UINT32 Destination)
{
    if (Destination == Handle->LfeIndex)
        return TRUE;
    return Destination == 2 && (Flags & X3DAUDIO_CALCULATE_ZEROCENTER) != 0;
}

/* Rotation about an axis by an angle, as three rows */
static
VOID
XaX3DRotation(
    _In_ const X3DAUDIO_VECTOR& Axis,
    _In_ float Angle,
    _Out_writes_(3) X3DAUDIO_VECTOR* Rows)
{
    float Cosine = cosf(Angle);
    float Sine = sinf(Angle);
    float Rest = 1.0f - Cosine;
    float XRest = Axis.x * Rest;
    float YRest = Axis.y * Rest;
    float XY = XRest * Axis.y;
    float XZ = XRest * Axis.z;
    float YZ = YRest * Axis.z;

    Rows[0].x = XRest * Axis.x + Cosine;
    Rows[0].y = XY - Axis.z * Sine;
    Rows[0].z = XZ + Axis.y * Sine;
    Rows[1].x = XY + Axis.z * Sine;
    Rows[1].y = YRest * Axis.y + Cosine;
    Rows[1].z = YZ - Axis.x * Sine;
    Rows[2].x = XZ - Axis.y * Sine;
    Rows[2].y = YZ + Axis.x * Sine;
    Rows[2].z = (Axis.z * Rest) * Axis.z + Cosine;
}

static
VOID
XaX3DSpreadEvenly(
    _In_ const XaX3DHandle* Handle,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings,
    _In_ UINT32 Source,
    _In_ float Level)
{
    float Share = Level / static_cast<float>(XaX3DSpreadCount(Handle, Flags, Settings));

    for (UINT32 Destination = 0; Destination < Settings->DstChannelCount; Destination++)
    {
        if (!XaX3DSpreadSkips(Handle, Flags, Destination))
            XaX3DCell(Settings, Source, Destination) = Share;
    }
}

/* Close to the listener the panned level fades toward an even spread */
static
VOID
XaX3DBlendNear(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings,
    _In_ UINT32 Source,
    _In_ float Level,
    _In_ float Reach)
{
    float Edge = Emitter->InnerRadius * 0.5f;
    float Keep;
    float Add;
    UINT32 Count;

    if (3.0f * FLT_EPSILON > Edge)
        Edge = 3.0f * FLT_EPSILON;
    if (!(Reach <= Edge))
        return;

    Count = XaX3DSpreadCount(Handle, Flags, Settings);
    Keep = (Reach - FLT_EPSILON) / Edge;
    Add = (Level / static_cast<float>(Count)) * (1.0f - Keep);
    for (UINT32 Destination = 0; Destination < Settings->DstChannelCount; Destination++)
    {
        if (XaX3DSpreadSkips(Handle, Flags, Destination))
            continue;
        XaX3DCell(Settings, Source, Destination) = XaX3DCell(Settings, Source, Destination) * Keep;
        XaX3DCell(Settings, Source, Destination) = XaX3DCell(Settings, Source, Destination) + Add;
    }
}

static
VOID
XaX3DMatrix(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    BOOLEAN Multi = Emitter->ChannelCount > 1;
    X3DAUDIO_VECTOR Spoke = {};
    X3DAUDIO_VECTOR Right = XaX3DCross(Listener->OrientTop, Listener->OrientFront);
    X3DAUDIO_VECTOR Up = XaX3DCross(Listener->OrientFront, Right);

    /* Channels of a multichannel emitter sit on a circle around its position */
    if (Multi)
    {
        Spoke.x = Emitter->ChannelRadius * Emitter->OrientFront.x;
        Spoke.y = Emitter->ChannelRadius * Emitter->OrientFront.y;
        Spoke.z = Emitter->ChannelRadius * Emitter->OrientFront.z;
    }

    for (UINT32 Channel = 0; Channel < Emitter->ChannelCount; Channel++)
    {
        X3DAUDIO_VECTOR Point = Emitter->Position;
        X3DAUDIO_VECTOR Offset;
        float Level;
        float Side;
        float Ahead;
        float Reach;
        float Azimuth;
        float Fringe;

        if (Multi)
        {
            X3DAUDIO_VECTOR Rows[3];

            if (Emitter->pChannelAzimuths[Channel] == X3DAUDIO_2PI)
                continue;
            XaX3DRotation(Emitter->OrientTop, Emitter->pChannelAzimuths[Channel], Rows);
            Point.x = XaX3DDot(Rows[0], Spoke) + Emitter->Position.x;
            Point.y = XaX3DDot(Rows[1], Spoke) + Emitter->Position.y;
            Point.z = XaX3DDot(Rows[2], Spoke) + Emitter->Position.z;
        }

        Offset = XaX3DSubtract(Point, Listener->Position);
        Level = Settings->pMatrixCoefficients[Channel];
        Settings->pMatrixCoefficients[Channel] = 0.0f;
        Side = XaX3DDot(Offset, Right);
        Ahead = XaX3DDot(Listener->OrientFront, Offset);
        Reach = sqrtf(Ahead * Ahead + Side * Side);
        if (Reach <= FLT_EPSILON)
        {
            XaX3DSpreadEvenly(Handle, Flags, Settings, Channel, Level);
            continue;
        }

        /* Inside the inner radius part of the level also comes from the opposite side */
        Azimuth = atan2f(Side, Ahead);
        Fringe = sinf(Emitter->InnerRadiusAngle) * XaX3DDot(Up, Offset) + Emitter->InnerRadius;
        if (Reach >= Fringe)
        {
            XaX3DPan(Handle, Flags, Settings, Channel, Level, Azimuth);
        }
        else
        {
            float Split = tanf(((Fringe - Reach) * XA_X3D_HALF_PI) / (Fringe + Fringe));
            float Near = Level / (Split + 1.0f);

            XaX3DPan(Handle, Flags, Settings, Channel, Near, Azimuth);
            XaX3DPan(Handle, Flags, Settings, Channel, Near * Split, Azimuth + X3DAUDIO_PI);
        }
        XaX3DBlendNear(Handle, Emitter, Flags, Settings, Channel, Level, Reach);
    }
}

/* Interaural delay in the far ear, from the listener relative direction */
static
VOID
XaX3DDelay(
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    X3DAUDIO_VECTOR Offset = XaX3DSubtract(Emitter->Position, Listener->Position);
    X3DAUDIO_VECTOR Right = XaX3DCross(Listener->OrientTop, Listener->OrientFront);
    float Side = XaX3DDot(Offset, Right);
    float Ahead = XaX3DDot(Listener->OrientFront, Offset);
    double Angle;

    if (Side <= FLT_EPSILON && Ahead <= FLT_EPSILON && Side >= -FLT_EPSILON && Ahead >= -FLT_EPSILON)
    {
        Settings->pDelayTimes[0] = 0.0f;
        Settings->pDelayTimes[1] = 0.0f;
        return;
    }
    Angle = atan2(static_cast<double>(Side), static_cast<double>(Ahead));
    if (Side > 0.0f)
    {
        Settings->pDelayTimes[0] = static_cast<float>(sin(Angle));
        Settings->pDelayTimes[1] = 0.0f;
    }
    else
    {
        Settings->pDelayTimes[0] = 0.0f;
        Settings->pDelayTimes[1] = static_cast<float>(sin(-Angle));
    }
}

static
VOID
XaX3DDoppler(
    _In_ const XaX3DHandle* Handle,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    float Distance = Settings->EmitterToListenerDistance;
    X3DAUDIO_VECTOR Offset;
    float ListenerSpeed;
    float EmitterSpeed;
    float ListenerScaled;
    float EmitterScaled;
    float Limit = Handle->SpeedLimit;

    if (!(Distance > FLT_EPSILON))
    {
        Settings->DopplerFactor = 1.0f;
        Settings->EmitterVelocityComponent = 0.0f;
        Settings->ListenerVelocityComponent = 0.0f;
        return;
    }

    /* Speeds along the emitter to listener line, kept below the speed of sound */
    Offset = XaX3DSubtract(Listener->Position, Emitter->Position);
    ListenerSpeed = XaX3DDot(Listener->Velocity, Offset) / Distance;
    EmitterSpeed = XaX3DDot(Emitter->Velocity, Offset) / Distance;
    ListenerScaled = Emitter->DopplerScaler * ListenerSpeed;
    EmitterScaled = Emitter->DopplerScaler * EmitterSpeed;
    if (ListenerScaled > Limit)
        ListenerScaled = Limit;
    else if (-Limit > ListenerScaled)
        ListenerScaled = -Limit;
    if (EmitterScaled > Limit)
        EmitterScaled = Limit;
    else if (-Limit > EmitterScaled)
        EmitterScaled = -Limit;

    Settings->DopplerFactor = (Handle->SpeedOfSound - ListenerScaled) / (Handle->SpeedOfSound - EmitterScaled);
    Settings->EmitterVelocityComponent = EmitterSpeed;
    Settings->ListenerVelocityComponent = ListenerSpeed;
}

EXTERN_C
HRESULT
__cdecl
XaExportX3DInitialize(
    _In_ UINT32 SpeakerChannelMask,
    _In_ FLOAT32 SpeedOfSound,
    _Out_writes_bytes_(X3DAUDIO_HANDLE_BYTESIZE) X3DAUDIO_HANDLE Instance)
{
    XaX3DHandle Handle;
    UINT32 Bits;

    Handle.ChannelMask = SpeakerChannelMask;
    Handle.LfeIndex = XA_X3D_NO_LFE;
    if (SpeakerChannelMask & SPEAKER_LOW_FREQUENCY)
    {
        Handle.LfeIndex = (SpeakerChannelMask & SPEAKER_FRONT_LEFT) ? 1 : 0;
        Handle.LfeIndex += (SpeakerChannelMask & SPEAKER_FRONT_RIGHT) ? 1 : 0;
        Handle.LfeIndex += (SpeakerChannelMask & SPEAKER_FRONT_CENTER) ? 1 : 0;
    }

    /* An empty mask still counts as one channel */
    Handle.ChannelCount = 0;
    for (UINT32 Rest = SpeakerChannelMask; Rest != 0; Rest &= Rest - 1)
        Handle.ChannelCount += 1;
    if (Handle.ChannelCount == 0)
        Handle.ChannelCount = 1;

    /* Doppler clamps to the float just below the speed of sound */
    Handle.SpeedOfSound = SpeedOfSound;
    memcpy(&Bits, &SpeedOfSound, sizeof(Bits));
    Bits -= 1;
    memcpy(&Handle.SpeedLimit, &Bits, sizeof(Bits));
    memcpy(Instance, &Handle, sizeof(Handle));
    return S_OK;
}

EXTERN_C
void
__cdecl
XaExportX3DCalculate(
    _In_reads_bytes_(X3DAUDIO_HANDLE_BYTESIZE) const X3DAUDIO_HANDLE Instance,
    _In_ const X3DAUDIO_LISTENER* Listener,
    _In_ const X3DAUDIO_EMITTER* Emitter,
    _In_ UINT32 Flags,
    _Inout_ X3DAUDIO_DSP_SETTINGS* Settings)
{
    XaX3DHandle Handle;
    X3DAUDIO_VECTOR Offset = XaX3DSubtract(Listener->Position, Emitter->Position);
    double Squares;

    memcpy(&Handle, Instance, sizeof(Handle));

    /* The distance is summed in double precision */
    Squares = static_cast<double>(Offset.y) * Offset.y + static_cast<double>(Offset.x) * Offset.x;
    Squares = Squares + static_cast<double>(Offset.z) * Offset.z;
    Settings->EmitterToListenerDistance = static_cast<float>(sqrt(Squares));

    if (Flags & X3DAUDIO_CALCULATE_EMITTER_ANGLE)
    {
        float Facing = 0.0f;

        if (Settings->EmitterToListenerDistance > FLT_EPSILON)
            Facing = XaX3DDot(Emitter->OrientFront, Offset) / Settings->EmitterToListenerDistance;
        Settings->EmitterToListenerAngle = acosf(Facing);
    }

    if (Flags & XA_X3D_CURVE_FLAGS)
    {
        XaX3DApplyCurves(&Handle, Emitter, Flags, Settings);
        if (Emitter->ChannelCount == 1 && Emitter->pCone != nullptr)
            XaX3DEmitterCone(&Handle, Listener, Emitter, Flags, Settings);
        if (Listener->pCone != nullptr)
            XaX3DListenerCone(&Handle, Listener, Emitter, Flags, Settings);
        if (Flags & X3DAUDIO_CALCULATE_MATRIX)
            XaX3DMatrix(&Handle, Listener, Emitter, Flags, Settings);
    }
    if (Flags & X3DAUDIO_CALCULATE_DELAY)
        XaX3DDelay(Listener, Emitter, Settings);
    if (Flags & X3DAUDIO_CALCULATE_DOPPLER)
        XaX3DDoppler(&Handle, Listener, Emitter, Settings);
}
