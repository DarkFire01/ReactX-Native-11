/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DirectSound object and its IDirectSound8 interface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

/* Software mixing on every buffer, with a primary buffer and continuous rate support */
#define DS_DEVICE_CAPS_FLAGS    0x00000F1F
#define DS_REPORTED_CAPS_FLAGS  0x00000F3F

/* Geometry values that may not be set, the ones below the narrowest real angle */
#define DS_GEOMETRY_LOWEST      5
#define DS_GEOMETRY_HIGHEST     180

HWND
DsRootWindow(
    _In_ HWND Window)
{
    HWND Parent;

    while ((Parent = GetParent(Window)) != nullptr)
        Window = Parent;
    return Window;
}

DsSound::DsSound()
{
    Version = DS_VERSION_7;
    InitStatus = DSERR_UNINITIALIZED;
    LevelThread = 0;
    Level = DS_LEVEL_NONE;
    Device = nullptr;
    Primary = nullptr;
    Buffers = nullptr;
    NextSound = nullptr;
    Face.Link.Object = this;
    Face.Link.Count = 0;
    Face.Self = this;
    Expose(IID_IDirectSound, &Face, &Face.Link);
    DsRegisterSound(this);
}

/* Buffers die with the object that made them */
DsSound::~DsSound()
{
    while (Buffers != nullptr)
        delete Buffers;
    DsUnregisterSound(this);
    if (Primary != nullptr)
        delete Primary;
    delete Device;
}

VOID
DsSound::SetVersion(
    _In_ ULONG NewVersion)
{
    Version = NewVersion;
    if (NewVersion >= DS_VERSION_8)
        Expose(IID_IDirectSound8, &Face, &Face.Link);
}

HRESULT
DsSound::Initialize(
    _In_opt_ LPCGUID Guid)
{
    DsDevice* NewDevice = new (std::nothrow) DsDevice();
    DsPrimary* NewPrimary;
    HRESULT hr;

    if (NewDevice == nullptr)
        return E_OUTOFMEMORY;
    hr = NewDevice->Open(Guid);
    if (FAILED(hr))
    {
        delete NewDevice;
        return hr;
    }

    NewPrimary = new (std::nothrow) DsPrimary(this);
    if (NewPrimary == nullptr)
    {
        delete NewDevice;
        return E_OUTOFMEMORY;
    }
    Device = NewDevice;
    Primary = NewPrimary;
    hr = Primary->Initialize();
    if (FAILED(hr))
    {
        delete Primary;
        Primary = nullptr;
        delete Device;
        Device = nullptr;
        return hr;
    }
    InitStatus = S_OK;
    return S_OK;
}

/* Writing the primary buffer takes the device from every secondary buffer */
HRESULT
DsSound::SetLevel(
    _In_ DWORD ThreadId,
    _In_ DWORD NewLevel)
{
    DWORD OldThread = LevelThread;
    DWORD OldLevel = Level;
    HRESULT hr;

    if (ThreadId == LevelThread && NewLevel == Level)
        return S_OK;

    LevelThread = ThreadId;
    Level = NewLevel;
    hr = Primary->SetLevel(NewLevel);
    if (FAILED(hr))
    {
        LevelThread = OldThread;
        Level = OldLevel;
        return hr;
    }
    DsUpdateFocus(TRUE);
    return S_OK;
}

HRESULT
DsSound::CreateBuffer(
    _In_ const DSBUFFERDESC* Desc,
    _Outptr_ DsBuffer** Buffer)
{
    DsSecondary* Secondary;
    HRESULT hr;

    *Buffer = nullptr;
    if (Desc->dwFlags & DSBCAPS_PRIMARYBUFFER)
    {
        hr = Primary->OnCreate(Desc->dwFlags);
        if (SUCCEEDED(hr))
            *Buffer = Primary;
        return hr;
    }

    Secondary = new (std::nothrow) DsSecondary(this);
    if (Secondary == nullptr)
        return E_OUTOFMEMORY;
    hr = Secondary->Initialize(Desc, nullptr);
    if (FAILED(hr))
    {
        delete Secondary;
        return hr;
    }
    *Buffer = Secondary;
    return S_OK;
}

HRESULT
DsSound::Duplicate(
    _In_ DsSecondary* Source,
    _Outptr_ DsBuffer** Buffer)
{
    DsSecondary* Copy;
    HRESULT hr;

    *Buffer = nullptr;
    if (Source->Owner != this || (Source->Flags & (DSBCAPS_CTRLFX | DS_BUFFER_SINKIN)))
        return DSERR_INVALIDCALL;

    Copy = new (std::nothrow) DsSecondary(this);
    if (Copy == nullptr)
        return E_OUTOFMEMORY;
    hr = Copy->Initialize(nullptr, Source);
    if (FAILED(hr))
    {
        delete Copy;
        return hr;
    }
    *Buffer = Copy;
    return S_OK;
}

VOID
DsSound::Forget(
    _In_ DsSecondary* Buffer)
{
    for (DsSecondary** Link = &Buffers; *Link != nullptr; Link = &(*Link)->NextBuffer)
    {
        if (*Link == Buffer)
        {
            *Link = Buffer->NextBuffer;
            return;
        }
    }
}

HRESULT
DsCreateSound(
    _In_ ULONG Version,
    _In_opt_ LPCGUID Guid,
    _In_ BOOLEAN Initialize,
    _In_ REFIID Iid,
    _COM_Outptr_ void** Object)
{
    DsSound* Sound = new (std::nothrow) DsSound();
    HRESULT hr;

    *Object = nullptr;
    if (Sound == nullptr)
        return E_OUTOFMEMORY;
    Sound->SetVersion(Version);
    if (Initialize)
    {
        hr = Sound->Initialize(Guid);
        if (FAILED(hr))
        {
            Sound->Drop();
            return hr;
        }
    }
    hr = Sound->Query(Iid, TRUE, Object);
    if (FAILED(hr))
        Sound->Drop();
    return hr;
}

DsSound*
DsSoundFromInterface(
    _In_opt_ IUnknown* Interface)
{
    if (Interface == nullptr)
        return nullptr;
    return static_cast<DsSoundFace*>(Interface)->Self;
}

/* Each method holds the API lock, refuses an object that never initialized, then checks its arguments */
#define DS_SOUND_ENTER() \
    DsApiGuard Guard; \
    if (FAILED(Self->InitStatus)) \
        return Self->InitStatus

HRESULT
STDMETHODCALLTYPE
DsSoundFace::CreateSoundBuffer(
    _In_ LPCDSBUFFERDESC Desc,
    _Outptr_ LPDIRECTSOUNDBUFFER* Buffer,
    _Pre_null_ LPUNKNOWN Outer)
{
    DSBUFFERDESC Valid;
    DsBuffer* Created = nullptr;
    void* Face = nullptr;
    HRESULT hr;

    DS_SOUND_ENTER();
    if (Desc == nullptr)
    {
        DS_TRACE_ONCE(L"CreateSoundBuffer: no description");
        return E_INVALIDARG;
    }
    hr = DsBuildBufferDesc(Desc, &Valid, Self->Version);
    if (FAILED(hr))
    {
        DS_TRACE_ONCE(L"CreateSoundBuffer: rejected description, flags 0x%08lX (0x%08lX)", Desc->dwFlags, hr);
        return hr;
    }
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (Buffer == nullptr)
        return E_INVALIDARG;

    *Buffer = nullptr;
    hr = Self->CreateBuffer(&Valid, &Created);
    if (FAILED(hr))
        return hr;
    hr = Created->Query(IID_IDirectSoundBuffer, TRUE, &Face);
    if (FAILED(hr))
    {
        Created->Drop();
        return hr;
    }
    *Buffer = static_cast<LPDIRECTSOUNDBUFFER>(Face);
    return Created->SuccessCode();
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::GetCaps(
    _Out_ LPDSCAPS Caps)
{
    DS_SOUND_ENTER();
    if (Caps == nullptr || Caps->dwSize != sizeof(*Caps))
        return E_INVALIDARG;

    ZeroMemory(reinterpret_cast<BYTE*>(Caps) + sizeof(Caps->dwSize), sizeof(*Caps) - sizeof(Caps->dwSize));
    Caps->dwFlags = DS_DEVICE_CAPS_FLAGS & DS_REPORTED_CAPS_FLAGS;
    Caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    Caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
    Caps->dwMaxHwMixingAllBuffers = 1;
    Caps->dwMaxHwMixingStaticBuffers = 1;
    Caps->dwMaxHwMixingStreamingBuffers = 1;
    if (!(Caps->dwFlags & DSCAPS_EMULDRIVER))
        Caps->dwPrimaryBuffers = 1;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::DuplicateSoundBuffer(
    _In_ LPDIRECTSOUNDBUFFER Original,
    _Outptr_ LPDIRECTSOUNDBUFFER* Duplicate)
{
    DsBuffer* Source;
    DsBuffer* Created = nullptr;
    void* Face = nullptr;
    HRESULT hr;

    DS_SOUND_ENTER();
    Source = DsBufferFromInterface(Original);
    if (Source == nullptr)
        return E_INVALIDARG;
    if (FAILED(Source->IsInit()))
        return Source->IsInit();
    if (Duplicate == nullptr)
        return E_INVALIDARG;

    *Duplicate = nullptr;
    if (Source == Self->Primary)
        return DSERR_INVALIDCALL;
    hr = Self->Duplicate(static_cast<DsSecondary*>(Source), &Created);
    if (FAILED(hr))
        return hr;
    hr = Created->Query(IID_IDirectSoundBuffer, TRUE, &Face);
    if (FAILED(hr))
    {
        Created->Drop();
        return hr;
    }
    *Duplicate = static_cast<LPDIRECTSOUNDBUFFER>(Face);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::SetCooperativeLevel(
    HWND Window,
    DWORD Level)
{
    DS_SOUND_ENTER();
    if (Window == nullptr)
    {
        DS_TRACE_ONCE(L"SetCooperativeLevel: no window");
        return E_INVALIDARG;
    }
    if (Level < DSSCL_NORMAL || Level > DSSCL_WRITEPRIMARY)
    {
        DS_TRACE_ONCE(L"SetCooperativeLevel: level %lu", Level);
        return E_INVALIDARG;
    }
    return Self->SetLevel(GetWindowThreadProcessId(DsRootWindow(Window), nullptr), Level);
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::Compact(VOID)
{
    DS_SOUND_ENTER();
    if (Self->Level < DSSCL_PRIORITY)
        return DSERR_PRIOLEVELNEEDED;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::GetSpeakerConfig(
    _Out_ LPDWORD Config)
{
    DWORD Speakers;

    DS_SOUND_ENTER();
    if (Config == nullptr)
        return E_INVALIDARG;
    *Config = 0;
    Speakers = Self->Device->SpeakerConfig();
    if (Speakers == 0xFFFFFFFF)
        return E_FAIL;
    *Config = Speakers;
    return S_OK;
}

/* The endpoint layout decides what the mixer renders to, so a new configuration changes nothing audible */
HRESULT
STDMETHODCALLTYPE
DsSoundFace::SetSpeakerConfig(
    DWORD Config)
{
    BYTE Type = static_cast<BYTE>(DSSPEAKER_CONFIG(Config));
    BYTE Geometry = static_cast<BYTE>(DSSPEAKER_GEOMETRY(Config));

    DS_SOUND_ENTER();
    if ((Config & 0xFF00FF00) != 0 || Type > DSSPEAKER_7POINT1_SURROUND)
        return E_INVALIDARG;
    if (Type != DSSPEAKER_STEREO && Geometry != 0)
        return E_INVALIDARG;
    if (Geometry != 0 && (Geometry < DS_GEOMETRY_LOWEST || Geometry > DS_GEOMETRY_HIGHEST))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::Initialize(
    _In_opt_ LPCGUID Device)
{
    DsApiGuard Guard;

    if (Self->InitStatus == S_OK)
        return DSERR_ALREADYINITIALIZED;
    if (Self->InitStatus != DSERR_UNINITIALIZED)
        return Self->InitStatus;
    return Self->Initialize(Device);
}

HRESULT
STDMETHODCALLTYPE
DsSoundFace::VerifyCertification(
    _Out_ LPDWORD Certified)
{
    DS_SOUND_ENTER();
    if (Certified == nullptr)
        return E_INVALIDARG;
    *Certified = DS_UNCERTIFIED;
    return S_OK;
}
