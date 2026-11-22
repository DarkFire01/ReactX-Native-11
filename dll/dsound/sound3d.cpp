/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     3D buffers and the listener: parameters, deferred settings and speaker panning
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <float.h>
#include <math.h>

#define DS_3D_FLAGS         DS3D_DEFERRED

/* What changed since the last commit */
#define DS_3D_MOVED         0x0001
#define DS_3D_CONE          0x0002
#define DS_3D_DISTANCE      0x0004
#define DS_3D_MODE          0x0008
#define DS_3D_VELOCITY      0x0010
#define DS_3D_ALL           0x001F

/* A 3D sound at the near distance plays this far below its buffer volume, in hundredths of a decibel */
#define DS_3D_BASE_VOLUME   -500


class DsListenerFace final : public IDirectSound3DListener
{
public:
    DsFaceLink Link;
    DsListener* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetAllParameters(
        _Out_ LPDS3DLISTENER Listener) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDistanceFactor(
        _Out_ D3DVALUE* DistanceFactor) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDopplerFactor(
        _Out_ D3DVALUE* DopplerFactor) override;

    HRESULT
    STDMETHODCALLTYPE
    GetOrientation(
        _Out_ D3DVECTOR* Front,
        _Out_ D3DVECTOR* Top) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPosition(
        _Out_ D3DVECTOR* Position) override;

    HRESULT
    STDMETHODCALLTYPE
    GetRolloffFactor(
        _Out_ D3DVALUE* RolloffFactor) override;

    HRESULT
    STDMETHODCALLTYPE
    GetVelocity(
        _Out_ D3DVECTOR* Velocity) override;

    HRESULT
    STDMETHODCALLTYPE
    SetAllParameters(
        _In_ LPCDS3DLISTENER Listener,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetDistanceFactor(
        D3DVALUE DistanceFactor,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetDopplerFactor(
        D3DVALUE DopplerFactor,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetOrientation(
        D3DVALUE FrontX,
        D3DVALUE FrontY,
        D3DVALUE FrontZ,
        D3DVALUE TopX,
        D3DVALUE TopY,
        D3DVALUE TopZ,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPosition(
        D3DVALUE X,
        D3DVALUE Y,
        D3DVALUE Z,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetRolloffFactor(
        D3DVALUE RolloffFactor,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetVelocity(
        D3DVALUE X,
        D3DVALUE Y,
        D3DVALUE Z,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    CommitDeferredSettings(VOID) override;
};

class Ds3dBufferFace final : public IDirectSound3DBuffer
{
public:
    DsFaceLink Link;
    Ds3dBuffer* Self;

    DS_FACE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetAllParameters(
        _Out_ LPDS3DBUFFER Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    GetConeAngles(
        _Out_opt_ LPDWORD Inside,
        _Out_opt_ LPDWORD Outside) override;

    HRESULT
    STDMETHODCALLTYPE
    GetConeOrientation(
        _Out_ D3DVECTOR* Orientation) override;

    HRESULT
    STDMETHODCALLTYPE
    GetConeOutsideVolume(
        _Out_ LPLONG Volume) override;

    HRESULT
    STDMETHODCALLTYPE
    GetMaxDistance(
        _Out_ D3DVALUE* Distance) override;

    HRESULT
    STDMETHODCALLTYPE
    GetMinDistance(
        _Out_ D3DVALUE* Distance) override;

    HRESULT
    STDMETHODCALLTYPE
    GetMode(
        _Out_ LPDWORD Mode) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPosition(
        _Out_ D3DVECTOR* Position) override;

    HRESULT
    STDMETHODCALLTYPE
    GetVelocity(
        _Out_ D3DVECTOR* Velocity) override;

    HRESULT
    STDMETHODCALLTYPE
    SetAllParameters(
        _In_ LPCDS3DBUFFER Buffer,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetConeAngles(
        DWORD Inside,
        DWORD Outside,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetConeOrientation(
        D3DVALUE X,
        D3DVALUE Y,
        D3DVALUE Z,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetConeOutsideVolume(
        LONG Volume,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetMaxDistance(
        D3DVALUE Distance,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetMinDistance(
        D3DVALUE Distance,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetMode(
        DWORD Mode,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPosition(
        D3DVALUE X,
        D3DVALUE Y,
        D3DVALUE Z,
        DWORD Apply) override;

    HRESULT
    STDMETHODCALLTYPE
    SetVelocity(
        D3DVALUE X,
        D3DVALUE Y,
        D3DVALUE Z,
        DWORD Apply) override;
};

class DsListener
{
public:
    DsPrimary* Primary;
    DS3DLISTENER Current;
    DS3DLISTENER Pending;
    ULONG PendingMask;
    Ds3dBuffer* Buffers;
    DsListenerFace Face;
};

class Ds3dBuffer
{
public:
    DsSecondary* Buffer;
    DsListener* Listener;
    Ds3dBuffer* Next;
    DS3DBUFFER Current;
    DS3DBUFFER Pending;
    ULONG PendingMask;
    DsAttenuation Attenuation;
    DWORD Frequency;
    BOOLEAN MuteAtMax;
    Ds3dBufferFace Face;
};

/* Vectors too long to square are scaled back inside the float range */
static
VOID
DsClampVector(
    _Inout_ D3DVECTOR* Vector)
{
    float Length = sqrtf(Vector->x * Vector->x + Vector->y * Vector->y + Vector->z * Vector->z);

    if (Length > FLT_MAX)
    {
        float Scale = 3.36879523e38f / Length;

        Vector->x *= Scale;
        Vector->y *= Scale;
        Vector->z *= Scale;
    }
}

static
float
DsLength(
    _In_ const D3DVECTOR* Vector)
{
    return sqrtf(Vector->y * Vector->y + Vector->x * Vector->x + Vector->z * Vector->z);
}

static
BOOLEAN
DsNormalize(
    _Inout_ D3DVECTOR* Vector)
{
    float Length = DsLength(Vector);
    float Inverse;

    if (Length == 0.0f)
        return FALSE;
    Inverse = 1.0f / Length;
    Vector->x *= Inverse;
    Vector->y *= Inverse;
    Vector->z *= Inverse;
    return TRUE;
}

static
D3DVECTOR
DsCross(
    _In_ const D3DVECTOR* Left,
    _In_ const D3DVECTOR* Right)
{
    D3DVECTOR Result;

    Result.x = Left->y * Right->z - Left->z * Right->y;
    Result.y = Left->z * Right->x - Left->x * Right->z;
    Result.z = Left->x * Right->y - Left->y * Right->x;
    return Result;
}

static
BOOLEAN
DsHasNan(
    _In_ const D3DVECTOR* Vector)
{
    return _isnan(Vector->x) || _isnan(Vector->y) || _isnan(Vector->z);
}

/* Makes the top vector square to the front one, keeping it in the plane they share */
static
BOOLEAN
DsSquareOrientation(
    _Inout_ D3DVECTOR* Front,
    _Inout_ D3DVECTOR* Top)
{
    D3DVECTOR Side;

    if (!DsNormalize(Front) || !DsNormalize(Top))
        return FALSE;
    Side = DsCross(Front, Top);
    if (Side.x == 0.0f && Side.y == 0.0f && Side.z == 0.0f)
        return FALSE;
    *Top = DsCross(&Side, Front);
    DsNormalize(Top);
    return TRUE;
}

static
BOOLEAN
DsIsValidConeAngles(
    _In_ DWORD Inside,
    _In_ DWORD Outside)
{
    return Outside >= Inside && Inside <= DS3D_MAXCONEANGLE && Outside <= DS3D_MAXCONEANGLE;
}

static
BOOLEAN
DsIsValidVolume(
    _In_ LONG Volume)
{
    return Volume <= DSBVOLUME_MAX && Volume >= DSBVOLUME_MIN;
}

/* Angles kept in single precision, as the panner works with them */
static const float DsTurn = 6.2831855f;
static const float DsThreeQuarters = 4.712389f;
static const float DsHalf = 3.1415927f;
static const float DsQuarter = 1.5707964f;
static const float DsDegreesPerRadian = 57.29578f;

/* Sines of whole degrees and arc sines in hundredth and thousandth steps, kept to six decimals */
static float DsSineTable[91];
static float DsArcSineLow[91];
static float DsArcSineHigh[101];
static INIT_ONCE DsTrigOnce = INIT_ONCE_STATIC_INIT;

static
float
DsSixDigits(
    _In_ double Value)
{
    return static_cast<float>(floor(Value * 1e6 + 0.5) / 1e6);
}

static
BOOL
CALLBACK
DsBuildTrigTables(
    _Inout_ PINIT_ONCE Once,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_ PVOID* Context)
{
    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    for (int Step = 0; Step <= 90; Step++)
        DsSineTable[Step] = DsSixDigits(sin(Step * 3.14159265358979323846 / 180.0));
    for (int Step = 0; Step <= 90; Step++)
        DsArcSineLow[Step] = DsSixDigits(asin(Step / 100.0));
    for (int Step = 0; Step <= 100; Step++)
        DsArcSineHigh[Step] = DsSixDigits(asin(0.9 + Step / 1000.0));
    return TRUE;
}

static
float
DsWrapTurn(
    _In_ float Angle)
{
    while (Angle < 0.0f)
        Angle += DsTurn;
    while (Angle > DsTurn)
        Angle -= DsTurn;
    return Angle;
}

/* Sine by whole degree lookup, folding each quadrant onto the first */
static
float
DsTableSin(
    _In_ float Angle)
{
    Angle = DsWrapTurn(Angle);
    if (Angle > DsThreeQuarters)
        return -DsSineTable[static_cast<int>((DsQuarter - (Angle - DsThreeQuarters)) * DsDegreesPerRadian)];
    if (Angle > DsHalf)
        return -DsSineTable[static_cast<int>((Angle - DsHalf) * DsDegreesPerRadian)];
    if (Angle > DsQuarter)
        return DsSineTable[static_cast<int>((DsQuarter - (Angle - DsQuarter)) * DsDegreesPerRadian)];
    return DsSineTable[static_cast<int>(Angle * DsDegreesPerRadian)];
}

static
float
DsTableCos(
    _In_ float Angle)
{
    Angle = DsWrapTurn(Angle);
    if (Angle > DsThreeQuarters)
        return DsSineTable[90 - static_cast<int>((DsQuarter - (Angle - DsThreeQuarters)) * DsDegreesPerRadian)];
    if (Angle > DsHalf)
        return -DsSineTable[90 - static_cast<int>((Angle - DsHalf) * DsDegreesPerRadian)];
    if (Angle > DsQuarter)
        return -DsSineTable[90 - static_cast<int>((DsQuarter - (Angle - DsQuarter)) * DsDegreesPerRadian)];
    return DsSineTable[90 - static_cast<int>(Angle * DsDegreesPerRadian)];
}

/* Arc sine by lookup, finer near one; anything past one reads as a quarter turn */
static
float
DsTableAsin(
    _In_ float Value)
{
    BOOLEAN Negative = Value < 0.0f;
    double Wide;
    float Result;

    if (Negative)
        Value = -Value;
    if (Value > 1.0f)
        return DsQuarter;
    Wide = Value;
    if (Value < 0.9f)
        Result = DsArcSineLow[static_cast<int>((Wide + 0.005) * 100.0)];
    else
        Result = DsArcSineHigh[static_cast<int>(((Wide - 0.9) + 0.0005) * 1000.0)];
    return Negative ? -Result : Result;
}

/* The three turns that bring the listener orientation back to facing ahead */
struct DsTurns
{
    float Pitch;
    float Yaw;
    float Roll;
};

static
DsTurns
DsListenerTurns(
    _In_ const D3DVECTOR* Front,
    _In_ const D3DVECTOR* Top)
{
    DsTurns Turns;
    float Lean = 0.0f;
    float Spin = 0.0f;
    float Length;

    if ((Top->x != 0.0f || Top->y != 0.0f || Top->z != 0.0f) && (Length = DsLength(Top)) != 0.0f)
    {
        Lean = DsTableAsin(Top->z / Length);
        if (Top->x != 0.0f)
        {
            Spin = static_cast<float>(atan2(static_cast<double>(Top->y), static_cast<double>(Top->x)));
            if (Spin < 0.0f)
                Spin += DsTurn;
        }
        else
        {
            Spin = Top->y >= 0.0f ? DsQuarter : -DsQuarter + DsTurn;
        }
    }

    Spin -= DsQuarter;
    Turns.Roll = Spin;
    Turns.Pitch = Lean;
    {
        float SinRoll = DsTableSin(-Spin);
        float CosRoll = DsTableCos(-Spin);
        float Planar = Front->x * SinRoll + Front->y * CosRoll;
        float Rise = DsTableAsin(DsTableCos(-Lean) * Front->z + DsTableSin(-Lean) * Planar);
        float Side = Front->x * CosRoll - Front->y * SinRoll;

        Turns.Yaw = Side < 0.0f ? Rise - DsQuarter : DsQuarter - Rise;
    }
    return Turns;
}

/* A position in the listener frame, turned by roll, pitch and yaw in that order */
static
D3DVECTOR
DsHeadRelative(
    _In_ const D3DVECTOR* Position,
    _In_ const D3DVECTOR* Listener,
    _In_ const DsTurns* Turns)
{
    D3DVECTOR Offset;
    D3DVECTOR Result;
    float Sin;
    float Cos;
    float Across;
    float Up;
    float Ahead;

    Offset.x = Position->x - Listener->x;
    Offset.y = Position->y - Listener->y;
    Offset.z = Position->z - Listener->z;
    if (Offset.x == 0.0f && Offset.y == 0.0f && Offset.z == 0.0f)
        return Offset;

    Sin = DsTableSin(-Turns->Roll);
    Cos = DsTableCos(-Turns->Roll);
    Across = Offset.x * Cos - Offset.y * Sin;
    Up = Offset.x * Sin + Offset.y * Cos;

    Sin = DsTableSin(-Turns->Pitch);
    Cos = DsTableCos(-Turns->Pitch);
    Ahead = Offset.z * Cos + Up * Sin;
    Result.y = Up * Cos - Offset.z * Sin;

    Sin = DsTableSin(-Turns->Yaw);
    Cos = DsTableCos(-Turns->Yaw);
    Result.x = Ahead * Sin + Across * Cos;
    Result.z = Ahead * Cos - Across * Sin;
    return Result;
}

/* Speaker angles in radians for the layouts the panner knows, left negative */
static
UINT
DsSpeakerRadians(
    _In_ DWORD Mask,
    _Out_writes_(DS_MAX_CHANNELS) double* Angles)
{
    static const double Quarter = 3.14159265358979323846 / 4.0;
    static const double Unplaced = 42.0;
    const double* Layout = nullptr;
    UINT Count = 0;

    static const double Stereo[] = { -2.0 * Quarter, 2.0 * Quarter };
    static const double Quad[] = { -Quarter, Quarter, -3.0 * Quarter, 3.0 * Quarter };
    static const double Surround[] = { -Quarter, Quarter, 0.0, 4.0 * Quarter };
    static const double Back51[] = { -Quarter, Quarter, 0.0, Unplaced, -3.0 * Quarter, 3.0 * Quarter };
    static const double Side51[] = { -Quarter, Quarter, 0.0, Unplaced, -2.0 * Quarter, 2.0 * Quarter };
    static const double Side71[] = { -Quarter, Quarter, 0.0, Unplaced, -3.0 * Quarter, 3.0 * Quarter, -2.0 * Quarter, 2.0 * Quarter };
    static const double Wide71[] = { -Quarter, Quarter, 0.0, Unplaced, -3.0 * Quarter, 3.0 * Quarter, -0.5 * Quarter, 0.5 * Quarter };

    switch (Mask)
    {
        case KSAUDIO_SPEAKER_MONO:
            Angles[0] = 0.0;
            return 1;
        case KSAUDIO_SPEAKER_STEREO:
            Layout = Stereo;
            Count = 2;
            break;
        case KSAUDIO_SPEAKER_QUAD:
            Layout = Quad;
            Count = 4;
            break;
        case KSAUDIO_SPEAKER_SURROUND:
            Layout = Surround;
            Count = 4;
            break;
        case KSAUDIO_SPEAKER_5POINT1:
            Layout = Back51;
            Count = 6;
            break;
        case KSAUDIO_SPEAKER_5POINT1_SURROUND:
            Layout = Side51;
            Count = 6;
            break;
        case KSAUDIO_SPEAKER_7POINT1_SURROUND:
            Layout = Side71;
            Count = 8;
            break;
        case KSAUDIO_SPEAKER_7POINT1:
            Layout = Wide71;
            Count = 8;
            break;
        default:
            return 0;
    }
    for (UINT Index = 0; Index < Count; Index++)
        Angles[Index] = Layout[Index];
    return Count;
}

/* Splits a share of power between the two speakers either side of an angle */
static
VOID
DsSpreadPower(
    _In_ UINT Count,
    _In_reads_(Count) const double* Speakers,
    _Inout_updates_(Count) double* Power,
    _In_ double Share,
    _In_ double Angle)
{
    const double HalfTurn = static_cast<double>(DsHalf);
    const double FullTurn = static_cast<double>(DsTurn);
    int Before = -1;
    int After = -1;
    double BeforeGap = -4.0;
    double AfterGap = 4.0;

    for (UINT Index = 0; Index < Count; Index++)
    {
        double Gap = Angle - Speakers[Index];

        if (Gap > HalfTurn)
            Gap -= FullTurn;
        else if (Gap < -HalfTurn)
            Gap += FullTurn;

        if (Gap <= 0.0 && Gap > BeforeGap)
        {
            Before = static_cast<int>(Index);
            BeforeGap = Gap;
        }
        else if (Gap >= 0.0 && Gap < AfterGap)
        {
            After = static_cast<int>(Index);
            AfterGap = Gap;
        }
    }
    BeforeGap = fabs(BeforeGap);
    AfterGap = fabs(AfterGap);
    if (Before == -1 || After == -1)
    {
        Before = 0;
        After = 0;
    }

    if (BeforeGap < 0.00001)
    {
        Power[Before] += Share;
    }
    else if (AfterGap < 0.00001)
    {
        Power[After] += Share;
    }
    else
    {
        double Ratio = tan(BeforeGap * static_cast<double>(DsQuarter) / (AfterGap + BeforeGap));
        double Part = Share / (Ratio + 1.0);

        Power[Before] += Part;
        Power[After] += Ratio * Part;
    }
}

/* Hundredths of a decibel of power for each speaker */
static
VOID
DsPanLevels(
    _In_ const D3DVECTOR* Relative,
    _In_ UINT Count,
    _In_reads_(Count) const double* Speakers,
    _Out_writes_(Count) LONG* Levels)
{
    double Power[DS_MAX_CHANNELS] = {};
    float Across = Relative->x;
    float Ahead = Relative->z;
    double Angle = atan2(static_cast<double>(Across), static_cast<double>(Ahead));

    Across = fabsf(Across);
    Ahead = fabsf(Ahead);
    if (static_cast<double>(Across) < 0.00001 && static_cast<double>(Ahead) < 0.00001)
    {
        for (UINT Index = 0; Index < Count; Index++)
            Power[Index] = 1.0 / static_cast<double>(static_cast<int>(Count));
    }
    else
    {
        double Flat = 0.0;
        BOOLEAN Far = Across >= 2.5f || Ahead >= 2.5f;

        if (!Far)
        {
            Flat = sqrt(static_cast<double>(Ahead * Ahead + Across * Across));
            Far = Flat >= 2.5;
        }
        if (Far)
        {
            DsSpreadPower(Count, Speakers, Power, 1.0, Angle);
        }
        else
        {
            double Front = Flat / 5.0 + 0.5;

            DsSpreadPower(Count, Speakers, Power, Front, Angle);
            DsSpreadPower(Count, Speakers, Power, 1.0 - Front, Angle + static_cast<double>(DsHalf));
        }
    }

    for (UINT Index = 0; Index < Count; Index++)
        Levels[Index] = Power[Index] == 0.0 ? DSBVOLUME_MIN : static_cast<LONG>(log(Power[Index]) * 434.29448815880363);
}

/* How much the cone around the sound lets through toward the listener */
static
float
DsConeGain(
    _In_ const DS3DBUFFER* Params,
    _In_ const DS3DLISTENER* Listener)
{
    D3DVECTOR Toward = {};
    float Cosine;
    float InsideCos;
    float OutsideCos;
    float Volume;

    if (Params->dwInsideConeAngle >= DS3D_MAXCONEANGLE)
        return 1.0f;
    if (Params->dwMode == DS3DMODE_NORMAL)
    {
        Toward.x = Listener->vPosition.x - Params->vPosition.x;
        Toward.y = Listener->vPosition.y - Params->vPosition.y;
        Toward.z = Listener->vPosition.z - Params->vPosition.z;
    }
    else if (Params->dwMode == DS3DMODE_HEADRELATIVE)
    {
        Toward.x = 0.0f - Params->vPosition.x;
        Toward.y = 0.0f - Params->vPosition.y;
        Toward.z = 0.0f - Params->vPosition.z;
    }
    if (Toward.x == 0.0f && Toward.y == 0.0f && Toward.z == 0.0f)
        return 1.0f;

    Cosine = (Toward.x * Params->vConeOrientation.x + Toward.y * Params->vConeOrientation.y +
              Toward.z * Params->vConeOrientation.z) / DsLength(&Toward);
    InsideCos = static_cast<float>(cos(static_cast<double>(static_cast<float>(Params->dwInsideConeAngle) * 0.008726646f)));
    if (Cosine > InsideCos)
        return 1.0f;
    OutsideCos = static_cast<float>(cos(static_cast<double>(static_cast<float>(Params->dwOutsideConeAngle) * 0.008726646f)));
    Volume = static_cast<float>(Params->lConeOutsideVolume);
    if (Cosine > OutsideCos)
        Volume *= (InsideCos - Cosine) / (InsideCos - OutsideCos);
    return static_cast<float>(pow(2.0, static_cast<double>(Volume * 0.0016666667f)));
}

/* The playback rate after the shift from the sound and listener moving along the line between them */
static
DWORD
DsDopplerRate(
    _In_ const DS3DBUFFER* Params,
    _In_ const DS3DLISTENER* Listener,
    _In_ DWORD Rate)
{
    const D3DVECTOR* Velocity = &Params->vVelocity;
    float Closing = 0.0f;
    double Speed;
    float Clamped;

    if (Listener->flDopplerFactor <= 0.0f)
        return Rate;
    if (Listener->vVelocity.x == 0.0f && Listener->vVelocity.z == 0.0f && Listener->vVelocity.y == 0.0f &&
        Velocity->x == 0.0f && Velocity->y == 0.0f && Velocity->z == 0.0f)
    {
        return Rate;
    }

    if (Params->dwMode != DS3DMODE_DISABLE)
    {
        D3DVECTOR Line;
        D3DVECTOR Moving;

        if (Params->dwMode == DS3DMODE_NORMAL)
        {
            Line.x = Params->vPosition.x - Listener->vPosition.x;
            Line.y = Params->vPosition.y - Listener->vPosition.y;
            Line.z = Params->vPosition.z - Listener->vPosition.z;
            Moving.x = Velocity->x - Listener->vVelocity.x;
            Moving.y = Velocity->y - Listener->vVelocity.y;
            Moving.z = Velocity->z - Listener->vVelocity.z;
        }
        else
        {
            Line = Params->vPosition;
            Moving = *Velocity;
        }
        if (Line.z != 0.0f || Line.x != 0.0f || Line.y != 0.0f)
        {
            double Squares = Params->dwMode == DS3DMODE_NORMAL ?
                             static_cast<double>(Line.x) * Line.x + static_cast<double>(Line.y) * Line.y :
                             static_cast<double>(Line.y) * Line.y + static_cast<double>(Line.x) * Line.x;
            float Length = static_cast<float>(sqrt(Squares + static_cast<double>(Line.z) * Line.z));

            Closing = (Line.x * Moving.x + Moving.y * Line.y + Moving.z * Line.z) / Length;
        }
    }

    Speed = static_cast<double>(Closing) * static_cast<double>(Listener->flDistanceFactor) * 1000.0 *
            static_cast<double>(Listener->flDopplerFactor);
    if (Speed < -static_cast<double>(FLT_MAX))
        Clamped = -FLT_MAX;
    else if (Speed > static_cast<double>(FLT_MAX))
        Clamped = FLT_MAX;
    else
        Clamped = static_cast<float>(Speed);
    if (Clamped > 179830.0f)
        Clamped = 179830.0f;
    if (Clamped < -179830.0f)
        Clamped = -179830.0f;
    return static_cast<DWORD>(static_cast<float>(Rate) * 359660.0f / (Clamped + 359660.0f));
}

/* Recomputes volume, speaker levels and doppler shift from the committed parameters */
static
VOID
DsApply3d(
    _In_ Ds3dBuffer* Spatial)
{
    DsSecondary* Buffer = Spatial->Buffer;
    DsDevice* Device = Buffer->Owner->Device;
    const DS3DBUFFER* Params = &Spatial->Current;
    const DS3DLISTENER* Listener = &Spatial->Listener->Current;
    double Speakers[DS_MAX_CHANNELS];
    LONG Levels[DS_MAX_CHANNELS] = {};
    UINT Count;
    LONG Volume = Spatial->Attenuation.Volume;
    DWORD Rate = Spatial->Frequency;
    BOOLEAN Mute = FALSE;

    InitOnceExecuteOnce(&DsTrigOnce, DsBuildTrigTables, nullptr, nullptr);
    Count = DsSpeakerRadians(Device->Mask, Speakers);
    if (Count == 0)
        return;

    if (Params->dwMode != DS3DMODE_DISABLE)
    {
        D3DVECTOR Relative = {};
        float Distance;
        float DistanceGain = 1.0f;
        float Product;
        BOOLEAN AtMax = FALSE;

        if (Params->dwMode == DS3DMODE_NORMAL)
        {
            DsTurns Turns = DsListenerTurns(&Listener->vOrientFront, &Listener->vOrientTop);

            Relative = DsHeadRelative(&Params->vPosition, &Listener->vPosition, &Turns);
        }
        else
        {
            Relative = Params->vPosition;
        }

        Distance = (Relative.x == 0.0f && Relative.y == 0.0f && Relative.z == 0.0f) ? 0.0f : DsLength(&Relative);
        if (Distance >= Params->flMaxDistance)
        {
            Distance = Params->flMaxDistance;
            AtMax = TRUE;
        }
        if (Distance > Params->flMinDistance && Listener->flRolloffFactor > 0.0f)
            DistanceGain = Params->flMinDistance / ((Distance - Params->flMinDistance) * Listener->flRolloffFactor + Params->flMinDistance);

        Product = DistanceGain * DsConeGain(Params, Listener);
        if ((Spatial->MuteAtMax && AtMax) || static_cast<double>(Product) <= 0.0)
            Volume += DSBVOLUME_MIN;
        else
            Volume += DS_3D_BASE_VOLUME - static_cast<LONG>(log(static_cast<double>(Product)) * -868.588963806556);
        Mute = Volume <= DSBVOLUME_MIN;
        Rate = DsDopplerRate(Params, Listener, Spatial->Frequency);

        if (Count > 1)
            DsPanLevels(&Relative, Count, Speakers, Levels);
    }

    Buffer->Voice.SetMute(Mute);
    Buffer->Voice.SetFrequency(DsMin<DWORD>(DsMax<DWORD>(Rate, DSBFREQUENCY_MIN), DSBFREQUENCY_MAX));
    Buffer->Voice.SetSpeakerVolumes(Volume, Count, Levels);
}

static
VOID
DsCommitBuffer(
    _In_ Ds3dBuffer* Spatial)
{
    if (Spatial->PendingMask != 0)
    {
        Spatial->Current = Spatial->Pending;
        Spatial->PendingMask = 0;
    }
    DsApply3d(Spatial);
}

static
VOID
DsCommitListener(
    _In_ DsListener* Listener)
{
    if (Listener->PendingMask != 0)
    {
        Listener->Current = Listener->Pending;
        Listener->PendingMask = 0;
    }
    for (Ds3dBuffer* Spatial = Listener->Buffers; Spatial != nullptr; Spatial = Spatial->Next)
        DsCommitBuffer(Spatial);
}

/* Immediate changes apply now; deferred ones wait for CommitDeferredSettings */
#define DS_BUFFER_CHANGE(Field, Value, Mask) \
    do \
    { \
        if (!(Self->PendingMask)) \
            Self->Pending = Self->Current; \
        Self->Pending.Field = (Value); \
        Self->PendingMask |= (Mask); \
        if (!(Apply & DS3D_DEFERRED)) \
            DsCommitBuffer(Self); \
    } while (0)

#define DS_LISTENER_CHANGE(Field, Value, Mask) \
    do \
    { \
        if (!(Self->PendingMask)) \
            Self->Pending = Self->Current; \
        Self->Pending.Field = (Value); \
        Self->PendingMask |= (Mask); \
        if (!(Apply & DS3D_DEFERRED)) \
            DsCommitListener(Self); \
    } while (0)

HRESULT
DsCreateListener(
    _In_ DsPrimary* Primary,
    _Outptr_ DsListener** Created)
{
    DsListener* Listener = new (std::nothrow) DsListener();

    *Created = nullptr;
    if (Listener == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(&Listener->Current, sizeof(Listener->Current));
    Listener->Primary = Primary;
    Listener->Current.dwSize = sizeof(Listener->Current);
    Listener->Current.vOrientFront.z = 1.0f;
    Listener->Current.vOrientTop.y = 1.0f;
    Listener->Current.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
    Listener->Current.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
    Listener->Current.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
    Listener->Pending = Listener->Current;
    Listener->PendingMask = 0;
    Listener->Buffers = nullptr;
    Listener->Face.Link.Object = Primary;
    Listener->Face.Link.Count = 0;
    Listener->Face.Self = Listener;
    *Created = Listener;
    return S_OK;
}

VOID
DsDestroyListener(
    _In_ DsListener* Listener)
{
    for (Ds3dBuffer* Spatial = Listener->Buffers; Spatial != nullptr; Spatial = Spatial->Next)
        Spatial->Listener = nullptr;
    delete Listener;
}

VOID
DsExposeListener(
    _In_ DsPrimary* Primary,
    _In_ DsListener* Listener)
{
    Primary->Expose(IID_IDirectSound3DListener, &Listener->Face, &Listener->Face.Link);
}

HRESULT
DsCreate3dBuffer(
    _In_ DsSecondary* Buffer,
    _In_opt_ Ds3dBuffer* Source,
    _Outptr_ Ds3dBuffer** Created)
{
    Ds3dBuffer* Spatial = new (std::nothrow) Ds3dBuffer();
    DsListener* Listener = Buffer->Owner->Primary->Listener;

    *Created = nullptr;
    if (Spatial == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(&Spatial->Current, sizeof(Spatial->Current));
    Spatial->Buffer = Buffer;
    Spatial->Listener = Listener;
    Spatial->Current.dwSize = sizeof(Spatial->Current);
    Spatial->Current.dwInsideConeAngle = DS3D_DEFAULTCONEANGLE;
    Spatial->Current.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
    Spatial->Current.vConeOrientation.z = 1.0f;
    Spatial->Current.lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;
    Spatial->Current.flMinDistance = DS3D_DEFAULTMINDISTANCE;
    Spatial->Current.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
    Spatial->Current.dwMode = DS3DMODE_NORMAL;
    if (Source != nullptr)
        Spatial->Current = Source->Current;
    Spatial->Pending = Spatial->Current;
    Spatial->PendingMask = 0;
    Spatial->Frequency = Buffer->Frequency;
    Spatial->MuteAtMax = (Buffer->Flags & DSBCAPS_MUTE3DATMAXDISTANCE) != 0;
    DsResolveVolumePan(0, 0, &Spatial->Attenuation);
    Spatial->Face.Link.Object = Buffer;
    Spatial->Face.Link.Count = 0;
    Spatial->Face.Self = Spatial;
    Buffer->Expose(IID_IDirectSound3DBuffer, &Spatial->Face, &Spatial->Face.Link);

    Spatial->Next = Listener->Buffers;
    Listener->Buffers = Spatial;
    *Created = Spatial;
    return S_OK;
}

VOID
DsDestroy3dBuffer(
    _In_ Ds3dBuffer* Spatial)
{
    if (Spatial->Listener != nullptr)
    {
        for (Ds3dBuffer** Link = &Spatial->Listener->Buffers; *Link != nullptr; Link = &(*Link)->Next)
        {
            if (*Link == Spatial)
            {
                *Link = Spatial->Next;
                break;
            }
        }
    }
    delete Spatial;
}

/* Algorithms other than plain panning are not available, which DirectSound reports as a success code */
HRESULT
Ds3dSpecialCode(
    _In_ Ds3dBuffer* Spatial)
{
    const GUID* Algorithm = &Spatial->Buffer->Algorithm;

    if (*Algorithm == GUID_NULL || *Algorithm == DS3DALG_NO_VIRTUALIZATION)
        return S_OK;
    return DS_NO_VIRTUALIZATION;
}

BOOLEAN
Ds3dFilterAttenuation(
    _In_ Ds3dBuffer* Spatial,
    _In_ const DsAttenuation* Attenuation)
{
    Spatial->Attenuation = *Attenuation;
    if (Spatial->Listener != nullptr)
        DsApply3d(Spatial);
    return FALSE;
}

BOOLEAN
Ds3dFilterFrequency(
    _In_ Ds3dBuffer* Spatial,
    _In_ DWORD Frequency)
{
    Spatial->Frequency = Frequency;
    if (Spatial->Listener != nullptr)
        DsApply3d(Spatial);
    return FALSE;
}

#define DS_3D_ENTER() \
    DsApiGuard Guard

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetAllParameters(
    _Out_ LPDS3DLISTENER Listener)
{
    DS_3D_ENTER();
    if (Listener == nullptr || Listener->dwSize != sizeof(*Listener))
        return E_INVALIDARG;
    *Listener = Self->Current;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetDistanceFactor(
    _Out_ D3DVALUE* DistanceFactor)
{
    DS_3D_ENTER();
    if (DistanceFactor == nullptr)
        return E_INVALIDARG;
    *DistanceFactor = Self->Current.flDistanceFactor;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetDopplerFactor(
    _Out_ D3DVALUE* DopplerFactor)
{
    DS_3D_ENTER();
    if (DopplerFactor == nullptr)
        return E_INVALIDARG;
    *DopplerFactor = Self->Current.flDopplerFactor;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetOrientation(
    _Out_ D3DVECTOR* Front,
    _Out_ D3DVECTOR* Top)
{
    DS_3D_ENTER();
    if (Front == nullptr || Top == nullptr)
        return E_INVALIDARG;
    *Front = Self->Current.vOrientFront;
    *Top = Self->Current.vOrientTop;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetPosition(
    _Out_ D3DVECTOR* Position)
{
    DS_3D_ENTER();
    if (Position == nullptr)
        return E_INVALIDARG;
    *Position = Self->Current.vPosition;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetRolloffFactor(
    _Out_ D3DVALUE* RolloffFactor)
{
    DS_3D_ENTER();
    if (RolloffFactor == nullptr)
        return E_INVALIDARG;
    *RolloffFactor = Self->Current.flRolloffFactor;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::GetVelocity(
    _Out_ D3DVECTOR* Velocity)
{
    DS_3D_ENTER();
    if (Velocity == nullptr)
        return E_INVALIDARG;
    *Velocity = Self->Current.vVelocity;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetAllParameters(
    _In_ LPCDS3DLISTENER Listener,
    DWORD Apply)
{
    DS3DLISTENER Value;

    DS_3D_ENTER();
    if (Listener == nullptr || Listener->dwSize != sizeof(*Listener))
        return E_INVALIDARG;
    Value = *Listener;
    if (Value.flDistanceFactor < FLT_MIN || Value.flDistanceFactor > FLT_MAX)
        return E_INVALIDARG;
    if (Value.flDopplerFactor < DS3D_MINDOPPLERFACTOR || Value.flDopplerFactor > DS3D_MAXDOPPLERFACTOR)
        return E_INVALIDARG;
    if (DsHasNan(&Value.vOrientFront) || DsHasNan(&Value.vOrientTop))
        return E_INVALIDARG;
    DsClampVector(&Value.vOrientFront);
    DsClampVector(&Value.vOrientTop);
    if (!DsSquareOrientation(&Value.vOrientFront, &Value.vOrientTop))
        return E_INVALIDARG;
    if (Value.flRolloffFactor < DS3D_MINROLLOFFFACTOR || Value.flRolloffFactor > DS3D_MAXROLLOFFFACTOR)
        return E_INVALIDARG;
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    DsClampVector(&Value.vPosition);

    if (!Self->PendingMask)
        Self->Pending = Self->Current;
    Self->Pending = Value;
    Self->PendingMask |= DS_3D_ALL;
    if (!(Apply & DS3D_DEFERRED))
        DsCommitListener(Self);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetDistanceFactor(
    D3DVALUE DistanceFactor,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (DistanceFactor < FLT_MIN || DistanceFactor > FLT_MAX || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_LISTENER_CHANGE(flDistanceFactor, DistanceFactor, DS_3D_VELOCITY);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetDopplerFactor(
    D3DVALUE DopplerFactor,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (DopplerFactor < DS3D_MINDOPPLERFACTOR || DopplerFactor > DS3D_MAXDOPPLERFACTOR || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_LISTENER_CHANGE(flDopplerFactor, DopplerFactor, DS_3D_VELOCITY);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetOrientation(
    D3DVALUE FrontX,
    D3DVALUE FrontY,
    D3DVALUE FrontZ,
    D3DVALUE TopX,
    D3DVALUE TopY,
    D3DVALUE TopZ,
    DWORD Apply)
{
    D3DVECTOR Front = { FrontX, FrontY, FrontZ };
    D3DVECTOR Top = { TopX, TopY, TopZ };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    if (DsHasNan(&Front) || DsHasNan(&Top))
        return E_INVALIDARG;
    DsClampVector(&Front);
    DsClampVector(&Top);
    if (!DsSquareOrientation(&Front, &Top))
        return E_INVALIDARG;

    if (!Self->PendingMask)
        Self->Pending = Self->Current;
    Self->Pending.vOrientFront = Front;
    Self->Pending.vOrientTop = Top;
    Self->PendingMask |= DS_3D_MOVED;
    if (!(Apply & DS3D_DEFERRED))
        DsCommitListener(Self);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetPosition(
    D3DVALUE X,
    D3DVALUE Y,
    D3DVALUE Z,
    DWORD Apply)
{
    D3DVECTOR Position = { X, Y, Z };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    DsClampVector(&Position);
    DS_LISTENER_CHANGE(vPosition, Position, DS_3D_MOVED);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetRolloffFactor(
    D3DVALUE RolloffFactor,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (RolloffFactor < DS3D_MINROLLOFFFACTOR || RolloffFactor > DS3D_MAXROLLOFFFACTOR || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_LISTENER_CHANGE(flRolloffFactor, RolloffFactor, DS_3D_DISTANCE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::SetVelocity(
    D3DVALUE X,
    D3DVALUE Y,
    D3DVALUE Z,
    DWORD Apply)
{
    D3DVECTOR Velocity = { X, Y, Z };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    DsClampVector(&Velocity);
    DS_LISTENER_CHANGE(vVelocity, Velocity, DS_3D_VELOCITY);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsListenerFace::CommitDeferredSettings(VOID)
{
    DS_3D_ENTER();
    DsCommitListener(Self);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetAllParameters(
    _Out_ LPDS3DBUFFER Buffer)
{
    DS_3D_ENTER();
    if (Buffer == nullptr || Buffer->dwSize != sizeof(*Buffer))
        return E_INVALIDARG;
    *Buffer = Self->Current;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetConeAngles(
    _Out_opt_ LPDWORD Inside,
    _Out_opt_ LPDWORD Outside)
{
    DS_3D_ENTER();
    if (Inside == nullptr && Outside == nullptr)
        return E_INVALIDARG;
    if (Inside != nullptr)
        *Inside = Self->Current.dwInsideConeAngle;
    if (Outside != nullptr)
        *Outside = Self->Current.dwOutsideConeAngle;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetConeOrientation(
    _Out_ D3DVECTOR* Orientation)
{
    DS_3D_ENTER();
    if (Orientation == nullptr)
        return E_INVALIDARG;
    *Orientation = Self->Current.vConeOrientation;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetConeOutsideVolume(
    _Out_ LPLONG Volume)
{
    DS_3D_ENTER();
    if (Volume == nullptr)
        return E_INVALIDARG;
    *Volume = Self->Current.lConeOutsideVolume;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetMaxDistance(
    _Out_ D3DVALUE* Distance)
{
    DS_3D_ENTER();
    if (Distance == nullptr)
        return E_INVALIDARG;
    *Distance = Self->Current.flMaxDistance;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetMinDistance(
    _Out_ D3DVALUE* Distance)
{
    DS_3D_ENTER();
    if (Distance == nullptr)
        return E_INVALIDARG;
    *Distance = Self->Current.flMinDistance;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetMode(
    _Out_ LPDWORD Mode)
{
    DS_3D_ENTER();
    if (Mode == nullptr)
        return E_INVALIDARG;
    *Mode = Self->Current.dwMode;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetPosition(
    _Out_ D3DVECTOR* Position)
{
    DS_3D_ENTER();
    if (Position == nullptr)
        return E_INVALIDARG;
    *Position = Self->Current.vPosition;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::GetVelocity(
    _Out_ D3DVECTOR* Velocity)
{
    DS_3D_ENTER();
    if (Velocity == nullptr)
        return E_INVALIDARG;
    *Velocity = Self->Current.vVelocity;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetAllParameters(
    _In_ LPCDS3DBUFFER Buffer,
    DWORD Apply)
{
    DS3DBUFFER Value;

    DS_3D_ENTER();
    if (Buffer == nullptr || Buffer->dwSize != sizeof(*Buffer))
        return E_INVALIDARG;
    Value = *Buffer;
    if (!DsIsValidConeAngles(Value.dwInsideConeAngle, Value.dwOutsideConeAngle))
        return E_INVALIDARG;
    if (DsHasNan(&Value.vConeOrientation))
        return E_INVALIDARG;
    DsClampVector(&Value.vConeOrientation);
    if (!DsNormalize(&Value.vConeOrientation))
        return E_INVALIDARG;
    if (!DsIsValidVolume(Value.lConeOutsideVolume))
        return E_INVALIDARG;
    if (Value.flMaxDistance <= 0.0f || Value.flMinDistance <= 0.0f)
        return E_INVALIDARG;
    if (Value.dwMode > DS3DMODE_DISABLE || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DsClampVector(&Value.vPosition);

    if (!Self->PendingMask)
        Self->Pending = Self->Current;
    Self->Pending = Value;
    Self->PendingMask |= DS_3D_ALL;
    if (!(Apply & DS3D_DEFERRED))
        DsCommitBuffer(Self);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetConeAngles(
    DWORD Inside,
    DWORD Outside,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (!DsIsValidConeAngles(Inside, Outside) || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    if (!Self->PendingMask)
        Self->Pending = Self->Current;
    Self->Pending.dwInsideConeAngle = Inside;
    Self->Pending.dwOutsideConeAngle = Outside;
    Self->PendingMask |= DS_3D_CONE;
    if (!(Apply & DS3D_DEFERRED))
        DsCommitBuffer(Self);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetConeOrientation(
    D3DVALUE X,
    D3DVALUE Y,
    D3DVALUE Z,
    DWORD Apply)
{
    D3DVECTOR Orientation = { X, Y, Z };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    if (DsHasNan(&Orientation))
        return E_INVALIDARG;
    DsClampVector(&Orientation);
    if (!DsNormalize(&Orientation))
        return E_INVALIDARG;
    DS_BUFFER_CHANGE(vConeOrientation, Orientation, DS_3D_CONE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetConeOutsideVolume(
    LONG Volume,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (!DsIsValidVolume(Volume) || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_BUFFER_CHANGE(lConeOutsideVolume, Volume, DS_3D_CONE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetMaxDistance(
    D3DVALUE Distance,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (Distance <= 0.0f || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_BUFFER_CHANGE(flMaxDistance, Distance, DS_3D_DISTANCE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetMinDistance(
    D3DVALUE Distance,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (Distance <= 0.0f || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_BUFFER_CHANGE(flMinDistance, Distance, DS_3D_DISTANCE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetMode(
    DWORD Mode,
    DWORD Apply)
{
    DS_3D_ENTER();
    if (Mode > DS3DMODE_DISABLE || (Apply & ~DS_3D_FLAGS))
        return E_INVALIDARG;
    DS_BUFFER_CHANGE(dwMode, Mode, DS_3D_MODE);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetPosition(
    D3DVALUE X,
    D3DVALUE Y,
    D3DVALUE Z,
    DWORD Apply)
{
    D3DVECTOR Position = { X, Y, Z };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    DsClampVector(&Position);
    DS_BUFFER_CHANGE(vPosition, Position, DS_3D_MOVED);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
Ds3dBufferFace::SetVelocity(
    D3DVALUE X,
    D3DVALUE Y,
    D3DVALUE Z,
    DWORD Apply)
{
    D3DVECTOR Velocity = { X, Y, Z };

    DS_3D_ENTER();
    if (Apply & ~DS_3D_FLAGS)
        return E_INVALIDARG;
    DsClampVector(&Velocity);
    DS_BUFFER_CHANGE(vVelocity, Velocity, DS_3D_VELOCITY);
    return S_OK;
}
