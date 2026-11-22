/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback effects and effect files
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput.hpp"

#include <math.h>

HRESULT
DiReadAttributes(
    _In_ HKEY Key,
    _Out_ LPDIFFDEVICEATTRIBUTES Attributes)
{
    return DiReadValue(Key, L"Attributes", Attributes, sizeof(*Attributes));
}

BOOLEAN
DiReadEffectEntry(
    _In_ HKEY Effects,
    _In_ DWORD Index,
    _Inout_ RX_DI_EFFECT_ENTRY* Entry)
{
    WCHAR Name[39];
    BOOLEAN Usable = FALSE;
    HKEY Key;
    LONG Error;

    /* Each effect is a subkey named by its GUID */
    if (RegEnumKeyW(Effects, Index, Name, _countof(Name)) != ERROR_SUCCESS || !DiParseGuid(Name, &Entry->Guid))
        return FALSE;
    if (RegOpenKeyExW(Effects, Name, 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return FALSE;

    /* A name too long for the record is cut and the effect is left out */
    Error = DiReadString(Key, nullptr, Entry->Name, _countof(Entry->Name));
    if (Error == ERROR_MORE_DATA)
    {
        Entry->Name[_countof(Entry->Name) - 1] = L'\0';
    }
    else
    {
        if (Error != ERROR_SUCCESS)
            Entry->Name[0] = L'\0';

        /* Only effects with a direction system the driver understands are offered */
        if ((Error == ERROR_SUCCESS || Error == ERROR_FILE_NOT_FOUND) &&
            SUCCEEDED(DiReadValue(Key, L"Attributes", Entry, FIELD_OFFSET(RX_DI_EFFECT_ENTRY, Guid))) &&
            (Entry->Coordinates & (DIEFF_CARTESIAN | DIEFF_POLAR | DIEFF_SPHERICAL)) != 0)
        {
            Usable = TRUE;
        }
    }

    RegCloseKey(Key);
    return Usable;
}


#define DI_EFF_MAX_AXES         32
#define DI_EFF_PARAM_MASK       0x000003FF
#define DI_EFF_FLAGS_INVALID    0xFFFFFF8C
#define DI_EFF_OBJECT_MASK      (DIEFF_OBJECTIDS | DIEFF_OBJECTOFFSETS)
#define DI_EFF_COORD_MASK       (DIEFF_CARTESIAN | DIEFF_POLAR | DIEFF_SPHERICAL)
#define DI_EFF_ANGLE_MASK       (DIEFF_POLAR | DIEFF_SPHERICAL)
#define DI_EFF_FULL_CIRCLE      36000
#define DI_EFF_QUARTER          9000
#define DI_EFF_NOMINAL          10000.0
#define DI_EFF_TWO_PI           6.283185307179586

/* Parameters an effect needs before it can be downloaded the first time */
#define DI_EFF_REQUIRED         (DIEP_TYPESPECIFICPARAMS | DIEP_DIRECTION | DIEP_AXES)

/* Start and stop requests handed to the start delay thread */
#define DI_EFF_DELAY_IDLE       0
#define DI_EFF_DELAY_START      1
#define DI_EFF_DELAY_STOP       2

/* Object conversion for triggers and axes, see RxDiDevice::ConvertObjects */
#define DI_EFF_TRIGGER_IN       (DIDFT_FFEFFECTTRIGGER | DI_CONVERT_TO_ID | DIDFT_BUTTON)
#define DI_EFF_AXES_IN          (DIDFT_FFACTUATOR | DI_CONVERT_TO_ID | DIDFT_AXIS)
#define DI_EFF_TRIGGER_OUT      (DI_CONVERT_FROM_ID | DIDFT_BUTTON)
#define DI_EFF_AXES_OUT         (DI_CONVERT_FROM_ID | DIDFT_AXIS)

typedef
HRESULT
(*PRX_DI_TSD_CHECK)(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes);

static
HRESULT
DiCheckAnyParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(Axes);
    return S_OK;
}

static
HRESULT
DiCheckConstantParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    UNREFERENCED_PARAMETER(Axes);
    return (Params->cbTypeSpecificParams == sizeof(DICONSTANTFORCE)) ? S_OK : E_INVALIDARG;
}

static
HRESULT
DiCheckRampParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    UNREFERENCED_PARAMETER(Axes);
    return (Params->cbTypeSpecificParams == sizeof(DIRAMPFORCE)) ? S_OK : E_INVALIDARG;
}

static
HRESULT
DiCheckPeriodicParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    UNREFERENCED_PARAMETER(Axes);
    return (Params->cbTypeSpecificParams == sizeof(DIPERIODIC)) ? S_OK : E_INVALIDARG;
}

/* One condition for every axis, or one shared by all of them */
static
HRESULT
DiCheckConditionParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    if (Params->cbTypeSpecificParams == sizeof(DICONDITION) ||
        Params->cbTypeSpecificParams == static_cast<ULONGLONG>(sizeof(DICONDITION)) * Axes)
    {
        return S_OK;
    }
    return E_INVALIDARG;
}

static
HRESULT
DiCheckCustomParams(
    _In_ LPCDIEFFECT Params,
    _In_ ULONG Axes)
{
    const DICUSTOMFORCE* Custom = static_cast<const DICUSTOMFORCE*>(Params->lpvTypeSpecificParams);

    UNREFERENCED_PARAMETER(Axes);
    if (Params->cbTypeSpecificParams != sizeof(DICUSTOMFORCE))
        return E_INVALIDARG;
    if (Custom->cChannels == 0 || (Custom->cSamples % Custom->cChannels) != 0)
        return E_INVALIDARG;
    return (Custom->rglForceData != nullptr) ? S_OK : E_INVALIDARG;
}

static
PRX_DI_TSD_CHECK
DiPickParamsCheck(
    _In_ DWORD EffectType)
{
    switch (DIEFT_GETTYPE(EffectType))
    {
        case DIEFT_CONSTANTFORCE: return DiCheckConstantParams;
        case DIEFT_RAMPFORCE: return DiCheckRampParams;
        case DIEFT_PERIODIC: return DiCheckPeriodicParams;
        case DIEFT_CONDITION: return DiCheckConditionParams;
        case DIEFT_CUSTOMFORCE: return DiCheckCustomParams;
    }
    return DiCheckAnyParams;
}

/* Spherical or polar angles to a unit vector scaled to the nominal maximum */
static
VOID
DiAnglesToVector(
    _In_ ULONG Count,
    _Out_writes_(Count) PLONG Vector,
    _In_reads_(Count) const LONG* Angles,
    _In_ DWORD Coordinates)
{
    double Parts[DI_EFF_MAX_AXES];

    Parts[0] = 1.0;
    for (ULONG Axis = 1; Axis < Count; Axis++)
    {
        LONG Angle = Angles[Axis - 1];
        double Radians;
        double Cosine;

        /* Polar angles start at north, spherical ones at east */
        if (Axis == 1 && (Coordinates & DIEFF_POLAR))
            Angle -= DI_EFF_QUARTER;

        Radians = Angle * DI_EFF_TWO_PI / DI_EFF_FULL_CIRCLE;
        Parts[Axis] = sin(Radians);
        Cosine = cos(Radians);
        for (ULONG Earlier = 0; Earlier < Axis; Earlier++)
            Parts[Earlier] *= Cosine;
    }

    for (ULONG Axis = 0; Axis < Count; Axis++)
        Vector[Axis] = static_cast<LONG>(Parts[Axis] * DI_EFF_NOMINAL);
}

/* A cartesian vector to spherical angles, rotated a quarter turn for polar */
static
VOID
DiVectorToAngles(
    _In_ ULONG Count,
    _Out_writes_(Count) PLONG Angles,
    _In_reads_(Count) const LONG* Vector,
    _In_ DWORD Coordinates)
{
    double Length = Vector[0];

    for (ULONG Axis = 1; Axis < Count; Axis++)
    {
        double Next = Vector[Axis];
        double Radians = (Next == 0.0 && Length == 0.0) ? 0.0 : atan2(Next, Length);
        LONG Angle = static_cast<LONG>((Radians + DI_EFF_TWO_PI) * DI_EFF_FULL_CIRCLE / DI_EFF_TWO_PI);

        if (Angle >= DI_EFF_FULL_CIRCLE)
            Angle -= DI_EFF_FULL_CIRCLE;
        Angles[Axis - 1] = Angle;
        Length = sqrt(Next * Next + Length * Length);
    }

    if (Coordinates & DIEFF_POLAR)
        Angles[0] += DI_EFF_QUARTER;
}

/* Rewrites a direction for the wanted coordinate system and returns the system it ended up in */
static
DWORD
DiConvertDirection(
    _In_ ULONG Count,
    _Out_writes_(Count) PLONG Out,
    _In_ DWORD Wanted,
    _In_reads_(Count) const LONG* In,
    _In_ DWORD Given)
{
    DWORD Result;

    if (Given & Wanted & DI_EFF_COORD_MASK)
    {
        CopyMemory(Out, In, sizeof(*Out) * Count);
        Result = Given & DI_EFF_COORD_MASK;
        if ((Given & DI_EFF_ANGLE_MASK) == 0)
            return Result;
    }
    else if (Given & DIEFF_CARTESIAN)
    {
        Result = (Wanted & DIEFF_SPHERICAL) ? DIEFF_SPHERICAL : DIEFF_POLAR;
        DiVectorToAngles(Count, Out, In, Result);
    }
    else if ((Wanted & DI_EFF_ANGLE_MASK) == 0)
    {
        DiAnglesToVector(Count, Out, In, Given & DI_EFF_COORD_MASK);
        return DIEFF_CARTESIAN;
    }
    else
    {
        /* Between polar and spherical, which differ by where zero points */
        Result = Wanted & DI_EFF_ANGLE_MASK;
        CopyMemory(Out, In, sizeof(*Out) * Count);
        Out[0] += (Given & DIEFF_POLAR) ? -DI_EFF_QUARTER : DI_EFF_QUARTER;
    }

    /* Angles wrap into one turn and the unused last slot is cleared */
    for (ULONG Axis = 0; Axis + 1 < Count; Axis++)
    {
        Out[Axis] %= DI_EFF_FULL_CIRCLE;
        if (Out[Axis] < 0)
            Out[Axis] += DI_EFF_FULL_CIRCLE;
    }
    Out[Count - 1] = 0;
    return Result;
}

/* A structure the caller sized as either the full or the DirectX 5 layout */
static
HRESULT
DiCheckSized(
    _In_opt_ const DWORD* Size,
    _In_ DWORD Wanted,
    _In_ DWORD Other)
{
    if (Size == nullptr)
        return E_POINTER;
    if (*Size == 0 || (*Size != Wanted && *Size != Other))
        return E_INVALIDARG;
    return S_OK;
}

class RxDiEffect final : public RxDiUnknown, public IDirectInputEffect
{
public:
    static
    HRESULT
    Create(
        _In_ RxDiDevice* Owner,
        _Outptr_ RxDiEffect** Created)
    {
        RxDiEffect* Effect = new RxDiEffect();
        PVOID* Grown;

        *Created = nullptr;
        if (Effect == nullptr)
            return E_OUTOFMEMORY;

        Effect->Device = Owner;
        Owner->Hold();
        Effect->Driver = Owner->EffectDriver;

        /* The device keeps its own hold on every effect it hands out */
        Owner->Enter();
        Grown = Owner->CreatedEffects;
        if (FAILED(DiAllocate(sizeof(*Grown) * (Owner->CreatedEffectCount + 1), reinterpret_cast<PVOID*>(&Grown))))
        {
            Owner->Leave();
            Effect->InnerUnknown()->Release();
            return E_OUTOFMEMORY;
        }
        Owner->CreatedEffects = Grown;
        Grown[Owner->CreatedEffectCount++] = Effect;
        Owner->Leave();

        Effect->Hold();
        Effect->Listed = TRUE;
        *Created = Effect;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override
    {
        if (&Iid == nullptr || Object == nullptr)
            return E_POINTER;
        *Object = nullptr;
        return Outer->QueryInterface(Iid, Object);
    }

    ULONG STDMETHODCALLTYPE AddRef(VOID) override { return Outer->AddRef(); }
    ULONG STDMETHODCALLTYPE Release(VOID) override { return Outer->Release(); }

    HRESULT
    STDMETHODCALLTYPE
    Initialize(
        HINSTANCE Instance,
        DWORD Version,
        REFGUID Guid) override
    {
        RX_DI_EFFECT_ENTRY Entry = {};
        BOOLEAN Found = FALSE;
        HRESULT hr;

        hr = DiCheckVersion(Instance, Version);
        if (FAILED(hr))
            return hr;
        if (&Guid == nullptr)
            return E_POINTER;

        Device->Enter();
        for (ULONG Index = 0; Index < Device->EffectCount; Index++)
        {
            if (IsEqualGUID(Device->EffectList[Index].Guid, Guid))
            {
                Entry = Device->EffectList[Index];
                Found = TRUE;
                break;
            }
        }

        if (!Found)
        {
            Device->Leave();
            return REGDB_E_CLASSNOTREG;
        }

        Reset();
        Ready = TRUE;
        EffectId = Entry.Id;
        EffectType = Entry.EffectType;
        StaticParams = Entry.StaticParams;
        DynamicParams = Entry.DynamicParams;
        DriverCoordinates = Entry.Coordinates;
        EffectGuid = Guid;

        /* Drivers that cannot delay a start get a thread that does it for them */
        if (Version >= 0x0600 && (StaticParams & DIEP_STARTDELAY) == 0)
        {
            StopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DelayThread = CreateThread(nullptr, 0, DelayProc, this, 0, nullptr);
            if (DelayThread != nullptr)
            {
                WakeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            }
            else
            {
                DiTrace(L"effect start delay thread failed, error %lu", GetLastError());
                CloseEvent(&StopEvent);
                CloseEvent(&DoneEvent);
            }
        }

        Supported = Entry.Coordinates & DI_EFF_COORD_MASK;
        CheckSpecific = DiPickParamsCheck(EffectType);
        Device->Leave();
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetEffectGuid(
        LPGUID Guid) override
    {
        if (Guid == nullptr)
            return E_POINTER;
        if (!Ready)
            return DIERR_NOTINITIALIZED;
        *Guid = EffectGuid;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetParameters(
        LPDIEFFECT Params,
        DWORD Flags) override
    {
        HRESULT hr;

        if (Params == nullptr)
            return S_OK;

        /* The DirectX 5 layout has no start delay */
        if (Params->dwSize == sizeof(DIEFFECT_DX5) || FAILED(DiCheckSized(&Params->dwSize, sizeof(DIEFFECT), 0)) ||
            (Flags & ~DI_EFF_PARAM_MASK) != 0)
        {
            hr = DiCheckSized(&Params->dwSize, sizeof(DIEFFECT_DX5), 0);
            if (FAILED(hr))
                return hr;
            if (Flags & ~(DI_EFF_PARAM_MASK & ~DIEP_STARTDELAY))
                return E_INVALIDARG;
        }

        Device->Enter();
        hr = CheckReadable(Params, Flags);
        if (SUCCEEDED(hr))
            hr = ReadParameters(Params, Flags);
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    SetParameters(
        LPCDIEFFECT Params,
        DWORD Flags) override
    {
        BOOLEAN Restart = FALSE;
        HRESULT hr;

        hr = DiCheckSized(Params ? &Params->dwSize : nullptr, sizeof(DIEFFECT), sizeof(DIEFFECT_DX5));
        if (FAILED(hr))
            return hr;
        if (Flags & ((Params->dwSize == sizeof(DIEFFECT)) ? 0x1FFFFC00 : 0x1FFFFE00))
            return E_INVALIDARG;

        Device->Enter();
        hr = CheckWritable(Params, Flags);
        if (FAILED(hr))
            goto Done;

        hr = Stage(Params, Flags);
        if (FAILED(hr))
            goto Release;

        /* A start delay the driver cannot handle only changes when the thread waits */
        if ((Flags & DIEP_STARTDELAY) != 0 && (StaticParams & DIEP_STARTDELAY) == 0)
        {
            Flags &= ~DIEP_STARTDELAY;
            Missing &= ~DIEP_STARTDELAY;
            Restart = (Current.dwStartDelay != Trial.dwStartDelay);
            hr = (Flags != 0) ? Download(&Trial, Flags) : S_OK;
        }
        else
        {
            hr = Download(&Trial, Flags);
        }
        if (FAILED(hr))
            goto Release;

        if (Restart && DelayState == DI_EFF_DELAY_START && hr != DI_DOWNLOADSKIPPED &&
            (Flags & DIEP_NORESTART) == 0 && StopEvent != nullptr && WakeEvent != nullptr)
        {
            Wake(DI_EFF_DELAY_START);
            if ((hr & ~(DI_EFFECTRESTARTED | DI_TRUNCATED)) == 0)
                hr |= DI_EFFECTRESTARTED;
            else if (hr == S_FALSE)
                hr = DI_EFFECTRESTARTED;
        }

        Missing &= ~Flags;
        if (hr == DI_DOWNLOADSKIPPED)
            Dirty |= Flags & DI_EFF_PARAM_MASK;
        Commit(Params, Flags);

    Release:
        DiFree(&StagedParams);

    Done:
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Start(
        DWORD Iterations,
        DWORD Flags) override
    {
        HRESULT hr;

        if (Flags & ~(DIES_SOLO | DIES_NODOWNLOAD))
            return E_INVALIDARG;

        Device->Enter();
        hr = CanAccess();
        if (SUCCEEDED(hr))
        {
            StartCount = Iterations;
            StartFlags = Flags;
            if (StopEvent != nullptr)
            {
                hr = Download(&Current, 0);
                if (WakeEvent != nullptr)
                    Wake(DI_EFF_DELAY_START);
            }
            else
            {
                hr = StartNow(Iterations, Flags);
            }
        }
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Stop(VOID) override
    {
        HRESULT hr;

        Device->Enter();
        hr = CanAccess();
        if (SUCCEEDED(hr))
            hr = Driver->StopEffect(&Handle);
        if (WakeEvent != nullptr)
            Wake(DI_EFF_DELAY_STOP);
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetEffectStatus(
        LPDWORD Status) override
    {
        HRESULT hr;

        if (Status == nullptr)
            return E_POINTER;

        *Status = 0;
        Device->Enter();
        hr = CanAccess();
        if (SUCCEEDED(hr))
        {
            /* Waiting out a start delay counts as playing */
            if (DelayState == DI_EFF_DELAY_START)
            {
                *Status = DIEGES_PLAYING;
                hr = S_OK;
            }
            else
            {
                hr = Driver->GetEffectStatus(&Handle, Status);
            }
        }
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Download(VOID) override
    {
        HRESULT hr;

        Device->Enter();
        hr = Download(&Current, 0);
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Unload(VOID) override
    {
        HRESULT hr;

        Device->Enter();
        hr = Remove();
        Device->Leave();
        return hr;
    }

    HRESULT
    STDMETHODCALLTYPE
    Escape(
        LPDIEFFESCAPE Request) override
    {
        HRESULT hr;

        hr = DiCheckSized(Request ? &Request->dwSize : nullptr, sizeof(DIEFFESCAPE), 0);
        if (FAILED(hr))
            return hr;
        if ((Request->lpvInBuffer == nullptr && Request->cbInBuffer != 0) ||
            (Request->lpvOutBuffer == nullptr && Request->cbOutBuffer != 0))
        {
            return E_POINTER;
        }

        Device->Enter();
        if (FAILED(Download(&Current, 0)))
            hr = DIERR_NOTDOWNLOADED;
        else
            hr = Driver->Escape(&Handle, Request);
        Device->Leave();
        return hr;
    }

    /* The device lets go of its effects when the application lets go of the device */
    VOID
    DeviceGone(VOID)
    {
        Stop();
        Unhold();
    }

private:
    RxDiEffect()
        : RxDiUnknown(nullptr)
    {
        ResetFields();
    }

    ~RxDiEffect() override
    {
        if (Device != nullptr)
        {
            Reset();
            Device->Unhold();
        }
    }

    PVOID PrimaryInterface(VOID) override { return static_cast<IDirectInputEffect*>(this); }

    PVOID
    QueryOwn(
        _In_ REFIID Iid) override
    {
        return IsEqualIID(Iid, IID_IDirectInputEffect) ? static_cast<IDirectInputEffect*>(this) : nullptr;
    }

    /* The application is done with the effect, take it off the device */
    VOID
    OnFinalRelease(VOID) override
    {
        BOOLEAN Removed = FALSE;

        if (!Listed)
            return;
        Listed = FALSE;

        Device->Enter();
        StopDelayThread();
        if (FAILED(CanAccess()))
            Handle.Id = 0;
        else
            Driver->DestroyEffect(&Handle);
        Dirty = DI_EFF_PARAM_MASK;
        Device->Leave();

        Device->Enter();
        for (ULONG Index = 0; Index < Device->CreatedEffectCount; Index++)
        {
            if (Device->CreatedEffects[Index] == this)
            {
                Device->CreatedEffectCount -= 1;
                Device->CreatedEffects[Index] = Device->CreatedEffects[Device->CreatedEffectCount];
                Removed = TRUE;
                break;
            }
        }
        Device->Leave();

        if (Removed)
            Unhold();
    }

    static
    VOID
    CloseEvent(
        _Inout_ HANDLE* Event)
    {
        if (*Event != nullptr)
        {
            CloseHandle(*Event);
            *Event = nullptr;
        }
    }

    VOID
    StopDelayThread(VOID)
    {
        if (StopEvent != nullptr && SetEvent(StopEvent) && DoneEvent != nullptr)
        {
            WaitForSingleObject(DoneEvent, INFINITE);
            CloseEvent(&DoneEvent);
        }
        if (DelayThread != nullptr)
        {
            CloseHandle(DelayThread);
            DelayThread = nullptr;
        }
    }

    VOID
    ResetFields(VOID)
    {
        Ready = FALSE;
        EffectId = 0;
        EffectType = 0;
        StaticParams = 0;
        DynamicParams = 0;
        DriverCoordinates = 0;
        DelayState = DI_EFF_DELAY_IDLE;
        StartCount = 0;
        StartFlags = 0;
        Dirty = 0;
        Missing = DI_EFF_REQUIRED;
        GivenCoordinates = 0;
        Supported = 0;
        StagedParams = nullptr;
        Handle = {};
        EffectGuid = GUID_NULL;
        CheckSpecific = DiCheckAnyParams;
        ZeroMemory(&Trial, sizeof(Trial));
        ZeroMemory(&Envelope, sizeof(Envelope));
        ZeroMemory(GivenDirection, sizeof(GivenDirection));
        ZeroMemory(Axes, sizeof(Axes));
        ZeroMemory(Direction, sizeof(Direction));
        ZeroMemory(TrialDirection, sizeof(TrialDirection));

        ZeroMemory(&Current, sizeof(Current));
        Current.dwSize = sizeof(Current);
        Current.dwTriggerButton = DIEB_NOTRIGGER;
        Current.rgdwAxes = Axes;
        Current.rglDirection = Direction;
        Envelope.dwSize = sizeof(Envelope);
    }

    /* Forgets everything about the effect, removing it from the device first */
    VOID
    Reset(VOID)
    {
        Device->Enter();
        Driver->DestroyEffect(&Handle);
        DiFree(&Current.lpvTypeSpecificParams);
        StopDelayThread();
        CloseEvent(&StopEvent);
        CloseEvent(&WakeEvent);
        ResetFields();
        Device->Leave();
    }

    HRESULT
    CanAccess(VOID)
    {
        if (!Ready)
            return DIERR_NOTINITIALIZED;
        if ((Device->CoopFlags & DISCL_EXCLUSIVE) == 0)
            return DIERR_NOTEXCLUSIVEACQUIRED;
        if (Device->State & DI_DEV_ACQUIRED)
            return S_OK;
        return (Device->LostResult == DIERR_NOTACQUIRED) ? DIERR_NOTEXCLUSIVEACQUIRED : Device->LostResult;
    }

    /* A device reset hands out a new tag, after which the driver has to hear everything again */
    VOID
    SyncTag(VOID)
    {
        if (Handle.Tag != Device->DriverId.Tag)
        {
            Handle.Id = 0;
            Handle.Tag = Device->DriverId.Tag;
            Dirty = DI_EFF_PARAM_MASK;
        }
    }

    HRESULT
    Download(
        _In_ LPCDIEFFECT Params,
        _In_ DWORD Flags)
    {
        HRESULT hr = CanAccess();

        /* Trial parameters can still be checked by a driver the device does not own */
        if ((hr == DIERR_INPUTLOST || hr == DIERR_NOTEXCLUSIVEACQUIRED) && Params == &Trial)
            Flags |= DIEP_NODOWNLOAD;
        else if (FAILED(hr))
            return hr;

        SyncTag();
        if ((Flags & DIEP_NODOWNLOAD) == 0)
        {
            if (Missing & ~Flags)
                return DIERR_INCOMPLETEEFFECT;
            Flags |= Dirty;
        }

        hr = Driver->DownloadEffect(&Handle, EffectId, Params, Flags);
        if (SUCCEEDED(hr))
        {
            if (Flags & DIEP_NODOWNLOAD)
                hr = DI_DOWNLOADSKIPPED;
            else
                Dirty = 0;
        }
        return hr;
    }

    HRESULT
    Remove(VOID)
    {
        HRESULT hr = CanAccess();

        if (FAILED(hr))
            Handle.Id = 0;
        else
            hr = Driver->DestroyEffect(&Handle);
        Dirty = DI_EFF_PARAM_MASK;
        return hr;
    }

    HRESULT
    StartNow(
        _In_ DWORD Iterations,
        _In_ DWORD Flags)
    {
        HRESULT hr;

        if (Flags & ~(DIES_SOLO | DIES_NODOWNLOAD))
            return E_INVALIDARG;

        Device->Enter();
        hr = CanAccess();
        if (SUCCEEDED(hr) && (Flags & DIES_NODOWNLOAD) == 0)
        {
            SyncTag();
            if (Dirty != 0 || Handle.Id == 0)
            {
                /* A plain single start goes along with the download */
                if (Flags == 0 && Iterations == 1)
                {
                    hr = Download(&Current, DIEP_START);
                    Device->Leave();
                    return hr;
                }
                hr = Download(&Current, 0);
            }
        }
        if (SUCCEEDED(hr))
            hr = Driver->StartEffect(&Handle, Flags & DIES_SOLO, Iterations);
        Device->Leave();
        return hr;
    }

    VOID
    Wake(
        _In_ LONG Request)
    {
        DelayState = Request;
        ResetEvent(WakeEvent);
        SetEvent(WakeEvent);
    }

    static
    DWORD
    WINAPI
    DelayProc(
        _In_ PVOID Parameter)
    {
        RxDiEffect* Self = static_cast<RxDiEffect*>(Parameter);
        HANDLE Waits[2];
        BOOLEAN Waiting = FALSE;
        DWORD Result = WAIT_TIMEOUT;

        if (Self == nullptr)
            return MAXDWORD;
        if (Self->StopEvent == nullptr || Self->DoneEvent == nullptr)
            return 0;

        Waits[0] = Self->StopEvent;
        Waits[1] = Self->WakeEvent;
        ResetEvent(Self->DoneEvent);

        /* The wake event is created after the thread, pick it up once it exists */
        for (;;)
        {
            DWORD Timeout = INFINITE;

            if (Result == WAIT_FAILED || Result == WAIT_OBJECT_0)
                break;

            if (Result == WAIT_TIMEOUT)
            {
                if (Waiting)
                {
                    Self->StartNow(Self->StartCount, Self->StartFlags);
                    Waiting = FALSE;
                    Self->DelayState = DI_EFF_DELAY_IDLE;
                }
            }
            else
            {
                if (Result == WAIT_OBJECT_0 + 1)
                {
                    if (Self->DelayState == DI_EFF_DELAY_START)
                    {
                        if (Self->Current.dwStartDelay < 1000)
                        {
                            Self->StartNow(Self->StartCount, Self->StartFlags);
                            Self->DelayState = DI_EFF_DELAY_IDLE;
                            Waiting = FALSE;
                        }
                        else
                        {
                            Waiting = TRUE;
                        }
                    }
                    else if (Self->DelayState == DI_EFF_DELAY_STOP)
                    {
                        Self->DelayState = DI_EFF_DELAY_IDLE;
                        Waiting = FALSE;
                    }
                    ResetEvent(Self->WakeEvent);
                }
                if (Waiting)
                    Timeout = Self->Current.dwStartDelay / 1000;
            }

            Waits[1] = Self->WakeEvent;
            Result = WaitForMultipleObjects((Waits[1] != nullptr) ? 2 : 1, Waits, FALSE, Timeout);
        }

        SetEvent(Self->DoneEvent);
        return 0;
    }

    HRESULT
    MapObjects(
        _In_ DWORD ParamsFlags,
        _In_ ULONG Count,
        _Out_writes_(Count) PDWORD Out,
        _In_reads_(Count) const DWORD* In,
        _In_ DWORD How)
    {
        if (Count == 0)
            return S_OK;

        MoveMemory(Out, In, sizeof(*Out) * Count);
        if (ParamsFlags & DIEFF_OBJECTOFFSETS)
            How |= (How & DI_CONVERT_FROM_ID) ? DI_CONVERT_TO_OFFSET : DI_CONVERT_FROM_OFFSET;
        else
            How |= (How & DI_CONVERT_FROM_ID) ? DI_CONVERT_TO_ID : DI_CONVERT_FROM_ID;
        return Device->ConvertObjects(Count, Out, How);
    }

    /* What SetParameters accepts before anything reaches the driver */
    HRESULT
    CheckWritable(
        _In_ LPCDIEFFECT Params,
        _In_ DWORD Flags)
    {
        ULONG Count = Current.cAxes;
        HRESULT hr;

        if (!Ready)
            return DIERR_NOTINITIALIZED;
        if ((Params->dwFlags & DI_EFF_FLAGS_INVALID) != 0)
            return E_INVALIDARG;
        if ((Flags & (DIEP_AXES | DIEP_TRIGGERBUTTON)) != 0 &&
            (Params->dwFlags & DI_EFF_OBJECT_MASK) != DIEFF_OBJECTIDS && (Params->dwFlags & DI_EFF_OBJECT_MASK) != DIEFF_OBJECTOFFSETS)
        {
            return E_INVALIDARG;
        }

        if (Flags & DIEP_DIRECTION)
        {
            DWORD System = Params->dwFlags & DI_EFF_COORD_MASK;

            if (System != DIEFF_CARTESIAN && System != DIEFF_SPHERICAL && !(System == DIEFF_POLAR && Params->cAxes == 2))
                return E_INVALIDARG;
        }

        if (Flags & (DIEP_AXES | DIEP_DIRECTION))
        {
            if (Params->cAxes == 0 || Params->cAxes > DI_EFF_MAX_AXES)
                return E_INVALIDARG;

            if (Flags & DIEP_AXES)
            {
                /* Axes are fixed once set */
                if (Count != 0)
                    return DIERR_ALREADYINITIALIZED;
                if (Params->rgdwAxes == nullptr)
                    return E_INVALIDARG;
                Count = Params->cAxes;
            }

            if ((Flags & DIEP_DIRECTION) && (Params->cAxes != Count || Params->rglDirection == nullptr))
                return E_INVALIDARG;
        }

        if (Flags & DIEP_TYPESPECIFICPARAMS)
        {
            if (Params->lpvTypeSpecificParams == nullptr && Params->cbTypeSpecificParams != 0)
                return E_INVALIDARG;
            hr = CheckSpecific(Params, Count);
            if (FAILED(hr))
                return hr;
        }

        if ((Flags & DIEP_ENVELOPE) && Params->lpEnvelope != nullptr &&
            FAILED(DiCheckSized(&Params->lpEnvelope->dwSize, sizeof(DIENVELOPE), 0)))
        {
            return E_INVALIDARG;
        }
        return S_OK;
    }

    /* What GetParameters needs from the caller, which may have its polar flag dropped */
    HRESULT
    CheckReadable(
        _Inout_ LPDIEFFECT Params,
        _In_ DWORD Flags)
    {
        if (!Ready)
            return DIERR_NOTINITIALIZED;
        if ((Params->dwFlags & DI_EFF_FLAGS_INVALID) != 0)
            return E_INVALIDARG;
        if ((Flags & (DIEP_AXES | DIEP_TRIGGERBUTTON)) != 0 &&
            (Params->dwFlags & DI_EFF_OBJECT_MASK) != DIEFF_OBJECTIDS && (Params->dwFlags & DI_EFF_OBJECT_MASK) != DIEFF_OBJECTOFFSETS)
        {
            return E_INVALIDARG;
        }

        if (Flags & DIEP_DIRECTION)
        {
            /* Polar only describes two axes */
            if (Current.cAxes != 2 && (Params->dwFlags & DIEFF_POLAR))
                Params->dwFlags &= ~DIEFF_POLAR;
            if ((Params->dwFlags & DI_EFF_COORD_MASK) == 0)
                return E_INVALIDARG;
        }

        if (((Flags & DIEP_TYPESPECIFICPARAMS) && Params->lpvTypeSpecificParams == nullptr && Params->cbTypeSpecificParams != 0) ||
            ((Flags & DIEP_AXES) && Params->rgdwAxes == nullptr && Params->cAxes != 0) ||
            ((Flags & DIEP_DIRECTION) && Params->rglDirection == nullptr && Params->cAxes != 0))
        {
            return E_POINTER;
        }

        if (Flags & DIEP_ENVELOPE)
            return DiCheckSized(Params->lpEnvelope ? &Params->lpEnvelope->dwSize : nullptr, sizeof(DIENVELOPE), 0);
        return S_OK;
    }

    HRESULT
    ReadParameters(
        _Inout_ LPDIEFFECT Params,
        _In_ DWORD Flags)
    {
        HRESULT hr = S_OK;

        if (Flags & DIEP_DURATION)
            Params->dwDuration = Current.dwDuration;
        if (Flags & DIEP_SAMPLEPERIOD)
            Params->dwSamplePeriod = Current.dwSamplePeriod;
        if (Flags & DIEP_GAIN)
            Params->dwGain = Current.dwGain;
        if (Flags & DIEP_STARTDELAY)
            Params->dwStartDelay = Current.dwStartDelay;

        if (Flags & DIEP_TRIGGERBUTTON)
        {
            Params->dwTriggerButton = Current.dwTriggerButton;
            if (Params->dwTriggerButton != DIEB_NOTRIGGER)
            {
                hr = MapObjects(Params->dwFlags, 1, &Params->dwTriggerButton, &Params->dwTriggerButton, DI_EFF_TRIGGER_OUT);
                if (FAILED(hr))
                    return hr;
            }
        }

        if (Flags & DIEP_TRIGGERREPEATINTERVAL)
            Params->dwTriggerRepeatInterval = Current.dwTriggerRepeatInterval;

        if (Flags & DIEP_TYPESPECIFICPARAMS)
        {
            DWORD Room = Params->cbTypeSpecificParams;

            Params->cbTypeSpecificParams = Current.cbTypeSpecificParams;
            CopyMemory(Params->lpvTypeSpecificParams, Current.lpvTypeSpecificParams,
                       (Room < Current.cbTypeSpecificParams) ? Room : Current.cbTypeSpecificParams);
            if (Room < Current.cbTypeSpecificParams)
                hr = DIERR_MOREDATA;
        }

        if (Flags & DIEP_ENVELOPE)
        {
            if (Current.lpEnvelope != nullptr)
            {
                *Params->lpEnvelope = *Current.lpEnvelope;
            }
            else
            {
                ZeroMemory(&Params->lpEnvelope->dwAttackLevel, sizeof(DIENVELOPE) - sizeof(DWORD));
                Params->lpEnvelope = nullptr;
            }
        }

        if (Flags & (DIEP_AXES | DIEP_DIRECTION))
        {
            ULONG Count = Params->cAxes;
            HRESULT Mapped;

            if (Count >= Current.cAxes)
            {
                Count = Current.cAxes;
            }
            else
            {
                hr = DIERR_MOREDATA;
            }
            Params->cAxes = Current.cAxes;

            if ((Flags & DIEP_AXES) &&
                FAILED(Mapped = MapObjects(Params->dwFlags, Count, Params->rgdwAxes, Current.rgdwAxes, DI_EFF_AXES_OUT)))
            {
                hr = Mapped;
            }
            else if (Flags & DIEP_DIRECTION)
            {
                if (GivenCoordinates != 0)
                {
                    LONG Converted[DI_EFF_MAX_AXES];
                    DWORD System = DiConvertDirection(Current.cAxes, Converted, Params->dwFlags, GivenDirection, GivenCoordinates);

                    Params->dwFlags = (Params->dwFlags & ~DI_EFF_COORD_MASK) | System;
                    CopyMemory(Params->rglDirection, Converted, sizeof(*Converted) * Count);
                }
                else
                {
                    Params->rglDirection = nullptr;
                    Params->dwFlags &= ~DI_EFF_COORD_MASK;
                }
            }
        }
        return hr;
    }

    /* Builds the trial parameters the driver is asked to accept */
    HRESULT
    Stage(
        _In_ LPCDIEFFECT Params,
        _In_ DWORD Flags)
    {
        HRESULT hr = S_OK;

        Trial = Current;
        if (Flags & DIEP_DURATION)
            Trial.dwDuration = Params->dwDuration;
        if (Flags & DIEP_SAMPLEPERIOD)
            Trial.dwSamplePeriod = Params->dwSamplePeriod;
        if (Flags & DIEP_GAIN)
            Trial.dwGain = Params->dwGain;
        if (Flags & DIEP_STARTDELAY)
            Trial.dwStartDelay = Params->dwStartDelay;

        if (Flags & DIEP_TRIGGERBUTTON)
        {
            Trial.dwTriggerButton = Params->dwTriggerButton;
            if (Trial.dwTriggerButton != DIEB_NOTRIGGER)
            {
                hr = MapObjects(Params->dwFlags, 1, &Trial.dwTriggerButton, &Trial.dwTriggerButton, DI_EFF_TRIGGER_IN);
                if (FAILED(hr))
                    return hr;
            }
            hr = S_OK;
        }

        if (Flags & DIEP_TRIGGERREPEATINTERVAL)
            Trial.dwTriggerRepeatInterval = Params->dwTriggerRepeatInterval;

        if (Flags & DIEP_TYPESPECIFICPARAMS)
        {
            Trial.cbTypeSpecificParams = Params->cbTypeSpecificParams;
            Trial.lpvTypeSpecificParams = Params->lpvTypeSpecificParams;
            if (Current.cbTypeSpecificParams != Params->cbTypeSpecificParams)
            {
                StagedParams = nullptr;
                hr = DiAllocate(Params->cbTypeSpecificParams, &StagedParams);
                if (FAILED(hr))
                    return hr;
            }
        }

        if (Flags & DIEP_AXES)
        {
            hr = MapObjects(Params->dwFlags, Params->cAxes, Trial.rgdwAxes, Params->rgdwAxes, DI_EFF_AXES_IN);
            if (FAILED(hr))
                return hr;

            for (ULONG Axis = 1; Axis < Params->cAxes; Axis++)
            {
                for (ULONG Earlier = 0; Earlier < Axis; Earlier++)
                {
                    if (Trial.rgdwAxes[Axis] == Trial.rgdwAxes[Earlier])
                        return E_INVALIDARG;
                }
            }
            Trial.cAxes = Params->cAxes;
        }

        if (Flags & DIEP_DIRECTION)
        {
            Trial.rglDirection = TrialDirection;
            Trial.dwFlags = (Trial.dwFlags & ~DI_EFF_COORD_MASK) |
                            DiConvertDirection(Trial.cAxes, TrialDirection, Supported, Params->rglDirection,
                                               Params->dwFlags & DI_EFF_COORD_MASK);
            hr = S_OK;
        }

        if (Flags & DIEP_ENVELOPE)
            Trial.lpEnvelope = Params->lpEnvelope;
        return hr;
    }

    /* Keeps the parameters the driver accepted */
    VOID
    Commit(
        _In_ LPCDIEFFECT Params,
        _In_ DWORD Flags)
    {
        Current.dwDuration = Trial.dwDuration;
        Current.dwSamplePeriod = Trial.dwSamplePeriod;
        Current.dwGain = Trial.dwGain;
        Current.dwTriggerButton = Trial.dwTriggerButton;
        Current.dwStartDelay = Trial.dwStartDelay;
        Current.dwTriggerRepeatInterval = Trial.dwTriggerRepeatInterval;
        Current.cAxes = Trial.cAxes;

        if (Flags & DIEP_TYPESPECIFICPARAMS)
        {
            if (Current.cbTypeSpecificParams != Trial.cbTypeSpecificParams)
            {
                Current.cbTypeSpecificParams = Trial.cbTypeSpecificParams;
                DiFree(&Current.lpvTypeSpecificParams);
                Current.lpvTypeSpecificParams = StagedParams;
                StagedParams = nullptr;
            }
            CopyMemory(Current.lpvTypeSpecificParams, Trial.lpvTypeSpecificParams, Current.cbTypeSpecificParams);
        }

        if (Flags & DIEP_DIRECTION)
        {
            GivenCoordinates = Params->dwFlags & DI_EFF_COORD_MASK;
            CopyMemory(GivenDirection, Params->rglDirection, sizeof(*GivenDirection) * Current.cAxes);
            Current.dwFlags = Trial.dwFlags;
            CopyMemory(Direction, TrialDirection, sizeof(Direction));
        }

        if (Flags & DIEP_ENVELOPE)
        {
            if (Trial.lpEnvelope != nullptr)
            {
                Current.lpEnvelope = &Envelope;
                Envelope = *Trial.lpEnvelope;
            }
            else
            {
                Current.lpEnvelope = nullptr;
            }
        }
    }

    RxDiDevice* Device = nullptr;
    RxDiShepherd* Driver = nullptr;
    BOOLEAN Listed = FALSE;
    BOOLEAN Ready;
    PRX_DI_TSD_CHECK CheckSpecific;
    DWORD EffectId;
    DWORD EffectType;
    DWORD StaticParams;
    DWORD DynamicParams;
    DWORD DriverCoordinates;
    HANDLE StopEvent = nullptr;
    HANDLE WakeEvent = nullptr;
    HANDLE DoneEvent = nullptr;
    HANDLE DelayThread = nullptr;
    volatile LONG DelayState;
    DWORD StartCount;
    DWORD StartFlags;
    DWORD Dirty;
    DWORD Missing;
    DWORD GivenCoordinates;
    DWORD Supported;
    PVOID StagedParams;
    RX_DI_FF_HANDLE Handle;
    DIEFFECT Current;
    DIEFFECT Trial;
    DIENVELOPE Envelope;
    GUID EffectGuid;
    LONG GivenDirection[DI_EFF_MAX_AXES];
    DWORD Axes[DI_EFF_MAX_AXES];
    LONG Direction[DI_EFF_MAX_AXES];
    LONG TrialDirection[DI_EFF_MAX_AXES];
};

HRESULT
DiCreateEffect(
    _In_ RxDiDevice* Device,
    _In_ REFGUID Guid,
    _In_opt_ LPCDIEFFECT Params,
    _Outptr_ LPDIRECTINPUTEFFECT* Effect,
    _In_opt_ IUnknown* OuterUnknown)
{
    RxDiEffect* Created = nullptr;
    HRESULT hr;

    *Effect = nullptr;
    if (OuterUnknown != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (&Guid == nullptr)
        return E_POINTER;

    hr = RxDiEffect::Create(Device, &Created);
    if (FAILED(hr))
        return hr;

    hr = Created->Initialize(DiModule, Device->Version(), Guid);
    if (SUCCEEDED(hr) && Params != nullptr)
        hr = Created->SetParameters(Params, (Params->dwSize < sizeof(DIEFFECT)) ? 0x1FF : DI_EFF_PARAM_MASK);

    if (FAILED(hr))
    {
        DI_TRACE_ONCE(L"effect creation failed, 0x%08lx", hr);
        Created->Release();
        return hr;
    }

    *Effect = Created;
    return S_OK;
}

LPDIRECTINPUTEFFECT
DiHoldEffect(
    _In_ PVOID Effect)
{
    RxDiEffect* Held = static_cast<RxDiEffect*>(Effect);

    Held->Hold();
    return Held;
}

VOID
DiUnholdEffect(
    _In_ LPDIRECTINPUTEFFECT Effect)
{
    static_cast<RxDiEffect*>(Effect)->Unhold();
}

VOID
DiReleaseCreatedEffects(
    _In_reads_opt_(Count) PVOID* Effects,
    _In_ ULONG Count)
{
    for (ULONG Index = 0; Index < Count; Index++)
        static_cast<RxDiEffect*>(Effects[Index])->DeviceGone();
}
