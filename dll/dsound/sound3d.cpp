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

/* Within this distance a sound spreads around the listener instead of coming from one side */
#define DS_3D_NEAR_SPREAD   2.5f

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

/* The speakers of the final mix as angles in radians, clockwise from the front */
static
UINT
DsSpeakerRadians(
    _In_ DsDevice* Device,
    _Out_writes_(DS_MAX_CHANNELS) double* Angles)
{
    static const double Degrees[11] = { -30.0, 30.0, 0.0, 0.0, -150.0, 150.0, -15.0, 15.0, 180.0, -110.0, 110.0 };
    UINT Count = 0;

    for (UINT Slot = 0; Slot < 11 && Count < Device->Channels; Slot++)
    {
        if (Device->Mask & (1UL << Slot))
            Angles[Count++] = Degrees[Slot] * 3.14159265358979 / 180.0;
    }
    while (Count < Device->Channels)
        Angles[Count++] = 0.0;
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
    int Before = -1;
    int After = -1;
    double BeforeGap = -4.0;
    double AfterGap = 4.0;

    for (UINT Index = 0; Index < Count; Index++)
    {
        double Gap = Angle - Speakers[Index];

        if (Gap > 3.14159274)
            Gap -= 6.28318548;
        else if (Gap < -3.14159274)
            Gap += 6.28318548;

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
        double Ratio = tan(BeforeGap) / tan(AfterGap);
        double Part = Share / (Ratio + 1.0);

        Power[Before] += Part;
        Power[After] += Ratio * Part;
    }
}

/* The listener relative position of a sound, x to the right, y up, z ahead */
static
D3DVECTOR
DsRelativePosition(
    _In_ Ds3dBuffer* Spatial)
{
    const DS3DLISTENER* Listener = &Spatial->Listener->Current;
    D3DVECTOR Offset = Spatial->Current.vPosition;
    D3DVECTOR Right;
    D3DVECTOR Result;

    if (Spatial->Current.dwMode == DS3DMODE_HEADRELATIVE)
        return Offset;
    Offset.x -= Listener->vPosition.x;
    Offset.y -= Listener->vPosition.y;
    Offset.z -= Listener->vPosition.z;
    Right = DsCross(&Listener->vOrientTop, &Listener->vOrientFront);
    Result.x = Offset.x * Right.x + Offset.y * Right.y + Offset.z * Right.z;
    Result.y = Offset.x * Listener->vOrientTop.x + Offset.y * Listener->vOrientTop.y + Offset.z * Listener->vOrientTop.z;
    Result.z = Offset.x * Listener->vOrientFront.x + Offset.y * Listener->vOrientFront.y + Offset.z * Listener->vOrientFront.z;
    return Result;
}

/* Recomputes volume, speaker gains and doppler shift from the committed parameters */
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
    double Power[DS_MAX_CHANNELS] = {};
    LONG Pans[DS_MAX_CHANNELS];
    UINT Count = DsSpeakerRadians(Device, Speakers);
    LONG Volume = Spatial->Attenuation.Volume;
    DWORD Frequency = Spatial->Frequency;
    BOOLEAN Mute = FALSE;

    if (Params->dwMode == DS3DMODE_DISABLE)
    {
        Buffer->Voice.SetAttenuation(&Spatial->Attenuation);
        Buffer->Voice.SetFrequency(Frequency);
        Buffer->Voice.SetMute(FALSE);
        return;
    }

    {
        D3DVECTOR Relative = DsRelativePosition(Spatial);
        float Distance = DsLength(&Relative);
        float Near = Params->flMinDistance;
        float DistanceGain = 1.0f;
        float ConeGain = 1.0f;
        BOOLEAN AtMax = FALSE;

        if (Distance >= Params->flMaxDistance)
        {
            Distance = Params->flMaxDistance;
            AtMax = TRUE;
        }
        if (Distance > Near && Listener->flRolloffFactor > 0.0f)
            DistanceGain = Near / ((Distance - Near) * Listener->flRolloffFactor + Near);

        if (Params->dwInsideConeAngle < DS3D_MAXCONEANGLE)
        {
            D3DVECTOR Toward;
            float Length;

            if (Params->dwMode == DS3DMODE_HEADRELATIVE)
            {
                Toward.x = -Params->vPosition.x;
                Toward.y = -Params->vPosition.y;
                Toward.z = -Params->vPosition.z;
            }
            else
            {
                Toward.x = Listener->vPosition.x - Params->vPosition.x;
                Toward.y = Listener->vPosition.y - Params->vPosition.y;
                Toward.z = Listener->vPosition.z - Params->vPosition.z;
            }
            Length = DsLength(&Toward);
            if (Length != 0.0f)
            {
                float Cosine = (Toward.x * Params->vConeOrientation.x + Toward.y * Params->vConeOrientation.y +
                                Toward.z * Params->vConeOrientation.z) / Length;
                float InsideCos = static_cast<float>(cos(Params->dwInsideConeAngle * 3.14159265358979 / 360.0));
                float OutsideCos = static_cast<float>(cos(Params->dwOutsideConeAngle * 3.14159265358979 / 360.0));

                if (Cosine <= InsideCos)
                {
                    float Share = Cosine > OutsideCos ? (InsideCos - Cosine) / (InsideCos - OutsideCos) : 1.0f;

                    ConeGain = static_cast<float>(pow(10.0, Share * Params->lConeOutsideVolume / 2000.0));
                }
            }
        }

        if ((Spatial->MuteAtMax && AtMax) || DistanceGain * ConeGain <= 0.0f)
            Volume += DSBVOLUME_MIN;
        else
            Volume += DS_3D_BASE_VOLUME - static_cast<LONG>(log(DistanceGain * ConeGain) * -868.588964);
        Mute = Volume <= DSBVOLUME_MIN;

        if (Count > 1)
        {
            float Across = fabsf(Relative.x);
            float Ahead = fabsf(Relative.z);
            double Angle = atan2(Relative.x, Relative.z);

            if (Across >= 0.00001f || Ahead >= 0.00001f)
            {
                float Flat = sqrtf(Relative.z * Relative.z + Relative.x * Relative.x);

                if (Across >= DS_3D_NEAR_SPREAD || Ahead >= DS_3D_NEAR_SPREAD || Flat >= DS_3D_NEAR_SPREAD)
                {
                    DsSpreadPower(Count, Speakers, Power, 1.0, Angle);
                }
                else
                {
                    double Front = Flat / 5.0 + 0.5;

                    DsSpreadPower(Count, Speakers, Power, Front, Angle);
                    DsSpreadPower(Count, Speakers, Power, 1.0 - Front, Angle + 3.14159274);
                }
            }
            else
            {
                for (UINT Index = 0; Index < Count; Index++)
                    Power[Index] = 1.0 / Count;
            }
        }
        else
        {
            Power[0] = 1.0;
        }
        for (UINT Index = 0; Index < Count; Index++)
            Pans[Index] = Power[Index] == 0.0 ? DSBVOLUME_MIN : static_cast<LONG>(log(Power[Index]) * 434.294488);

        /* Doppler shift from the speeds along the line between sound and listener */
        if (Listener->flDopplerFactor > 0.0f)
        {
            D3DVECTOR Line;
            float Closing = 0.0f;
            float Length;

            Line.x = Params->vPosition.x - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vPosition.x);
            Line.y = Params->vPosition.y - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vPosition.y);
            Line.z = Params->vPosition.z - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vPosition.z);
            Length = DsLength(&Line);
            if (Length != 0.0f)
            {
                float VelocityX = Params->vVelocity.x - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vVelocity.x);
                float VelocityY = Params->vVelocity.y - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vVelocity.y);
                float VelocityZ = Params->vVelocity.z - (Params->dwMode == DS3DMODE_HEADRELATIVE ? 0.0f : Listener->vVelocity.z);

                Closing = (Line.x * VelocityX + VelocityY * Line.y + VelocityZ * Line.z) / Length;
            }
            {
                float Speed = Closing * Listener->flDistanceFactor * 1000.0f * Listener->flDopplerFactor;

                Speed = DsMin(DsMax(Speed, -179830.0f), 179830.0f);
                Frequency = static_cast<DWORD>(static_cast<float>(Spatial->Frequency) * 359660.0f / (Speed + 359660.0f));
            }
        }
    }

    Buffer->Voice.SetMute(Mute);
    Buffer->Voice.SetFrequency(DsMin<DWORD>(DsMax<DWORD>(Frequency, DSBFREQUENCY_MIN), DSBFREQUENCY_MAX));
    Buffer->Voice.SetSpeakerVolumes(Volume, Count, Pans);
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
