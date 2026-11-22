/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectSoundBuffer8 argument checks and the secondary buffer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

/* Marks a buffer interface this runtime made */
#define DS_BUFFER_SIGNATURE 0x46425344

/* Play flags beyond looping that only deferred buffers take */
#define DS_PLAY_DEFER_FLAGS 0x0000003E
#define DS_PLAY_ALL_FLAGS   0x0000003F
#define DS_PLAY_TERMINATE   (DSBPLAY_TERMINATEBY_TIME | DSBPLAY_TERMINATEBY_DISTANCE)

#define DS_ACQUIRE_FLAGS    0x0000003E

DsBuffer::DsBuffer(
    _In_ DsSound* NewOwner)
{
    Owner = NewOwner;
    InitStatus = DSERR_UNINITIALIZED;
    Version = NewOwner->Version;
    Flags = 0;
    Bytes = 0;
    Format = nullptr;
    StatusFlags = 0;
    Face.Link.Object = this;
    Face.Link.Count = 0;
    Face.Self = this;
    Face.Signature = DS_BUFFER_SIGNATURE;
    PropertyFace.Link.Object = this;
    PropertyFace.Link.Count = 0;
    Expose(IID_IDirectSoundBuffer, &Face, &Face.Link);
    Expose(IID_IKsPropertySet, &PropertyFace, &PropertyFace.Link);
}

DsBuffer*
DsBufferFromInterface(
    _In_opt_ IUnknown* Interface)
{
    DsBufferFace* Face = static_cast<DsBufferFace*>(static_cast<IDirectSoundBuffer8*>(static_cast<IUnknown*>(Interface)));

    if (Interface == nullptr || Face->Signature != DS_BUFFER_SIGNATURE)
        return nullptr;
    return Face->Self;
}

/* Each method holds the API lock and refuses a buffer that never initialized */
#define DS_BUFFER_ENTER() \
    DsApiGuard Guard; \
    if (FAILED(Self->IsInit())) \
        return Self->IsInit()

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetCaps(
    _Out_ LPDSBCAPS Caps)
{
    DS_BUFFER_ENTER();
    if (Caps == nullptr || Caps->dwSize != sizeof(*Caps))
        return E_INVALIDARG;
    return Self->GetCaps(Caps);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetCurrentPosition(
    _Out_opt_ LPDWORD Play,
    _Out_opt_ LPDWORD Write)
{
    DS_BUFFER_ENTER();
    if (Play == nullptr && Write == nullptr)
        return E_INVALIDARG;
    return Self->GetCurrentPosition(Play, Write);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetFormat(
    _Out_writes_bytes_opt_(SizeAllocated) LPWAVEFORMATEX Format,
    DWORD SizeAllocated,
    _Out_opt_ LPDWORD SizeWritten)
{
    DWORD Size = SizeAllocated;
    HRESULT hr;

    DS_BUFFER_ENTER();
    if (Format == nullptr)
    {
        if (SizeWritten == nullptr)
            return E_INVALIDARG;
        Size = 0;
    }
    hr = Self->GetFormat(Format, &Size);
    if (SUCCEEDED(hr) && SizeWritten != nullptr)
        *SizeWritten = Size;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetVolume(
    _Out_ LPLONG Volume)
{
    DS_BUFFER_ENTER();
    if (Volume == nullptr)
        return E_INVALIDARG;
    return Self->GetVolume(Volume);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetPan(
    _Out_ LPLONG Pan)
{
    DS_BUFFER_ENTER();
    if (Pan == nullptr)
        return E_INVALIDARG;
    return Self->GetPan(Pan);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetFrequency(
    _Out_ LPDWORD Frequency)
{
    DS_BUFFER_ENTER();
    if (Frequency == nullptr)
        return E_INVALIDARG;
    return Self->GetFrequency(Frequency);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetStatus(
    _Out_ LPDWORD Status)
{
    DS_BUFFER_ENTER();
    if (Status == nullptr)
        return E_INVALIDARG;
    return Self->GetStatus(Status);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Initialize(
    _In_ LPDIRECTSOUND Sound,
    _In_ LPCDSBUFFERDESC Desc)
{
    DS_BUFFER_ENTER();
    if (Sound == nullptr || Desc == nullptr)
        return E_INVALIDARG;
    return DSERR_ALREADYINITIALIZED;
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Lock(
    DWORD Offset,
    DWORD Bytes,
    _Outptr_result_bytebuffer_(*Bytes1) LPVOID* Part1,
    _Out_ LPDWORD Bytes1,
    _Outptr_opt_result_bytebuffer_(*Bytes2) LPVOID* Part2,
    _Out_opt_ LPDWORD Bytes2,
    DWORD Flags)
{
    DS_BUFFER_ENTER();
    if (Part1 == nullptr)
        return E_INVALIDARG;
    *Part1 = nullptr;
    if (Bytes1 == nullptr)
        return E_INVALIDARG;
    *Bytes1 = 0;
    if (Part2 != nullptr)
        *Part2 = nullptr;
    if (Bytes2 != nullptr)
        *Bytes2 = 0;
    if (Flags & ~(DSBLOCK_FROMWRITECURSOR | DSBLOCK_ENTIREBUFFER))
        return E_INVALIDARG;
    return Self->Lock(Offset, Bytes, Part1, Bytes1, Part2, Bytes2, Flags);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Play(
    DWORD Reserved,
    DWORD Priority,
    DWORD Flags)
{
    HRESULT hr;

    DS_BUFFER_ENTER();
    if (Reserved != 0)
        return E_INVALIDARG;
    if (Flags & ~DS_PLAY_ALL_FLAGS)
        return E_INVALIDARG;
    if ((Flags & DS_PLAY_TERMINATE) == DS_PLAY_TERMINATE)
        return E_INVALIDARG;
    hr = Self->Play(Priority, Flags);
    if (SUCCEEDED(hr))
        hr = Self->SuccessCode();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetCurrentPosition(
    DWORD Position)
{
    DS_BUFFER_ENTER();
    return Self->SetCurrentPosition(Position);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetFormat(
    _In_ LPCWAVEFORMATEX Format)
{
    DS_BUFFER_ENTER();
    if (Format == nullptr || !DsIsValidFormat(Format))
        return E_INVALIDARG;
    return Self->SetFormat(Format);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetVolume(
    LONG Volume)
{
    DS_BUFFER_ENTER();
    if (Volume > DSBVOLUME_MAX || Volume < DSBVOLUME_MIN)
        return E_INVALIDARG;
    return Self->SetVolume(Volume);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetPan(
    LONG Pan)
{
    DS_BUFFER_ENTER();
    if (Pan < DSBPAN_LEFT || Pan > DSBPAN_RIGHT)
        return E_INVALIDARG;
    return Self->SetPan(Pan);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetFrequency(
    DWORD Frequency)
{
    DS_BUFFER_ENTER();
    if (Frequency != DSBFREQUENCY_ORIGINAL && (Frequency < DSBFREQUENCY_MIN || Frequency > DSBFREQUENCY_MAX))
        return E_INVALIDARG;
    return Self->SetFrequency(Frequency);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Stop(VOID)
{
    DS_BUFFER_ENTER();
    return Self->Stop();
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Unlock(
    _In_reads_bytes_(Bytes1) LPVOID Part1,
    DWORD Bytes1,
    _In_reads_bytes_opt_(Bytes2) LPVOID Part2,
    DWORD Bytes2)
{
    DS_BUFFER_ENTER();
    if (Part1 == nullptr && Bytes1 != 0)
        return E_INVALIDARG;
    if (Bytes2 != 0 && Part2 == nullptr)
        return E_INVALIDARG;
    return Self->Unlock(Part1, Bytes1, Part2, Bytes2);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::Restore(VOID)
{
    DS_BUFFER_ENTER();
    return Self->Restore();
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::SetFX(
    DWORD Count,
    _In_reads_opt_(Count) LPDSEFFECTDESC Effects,
    _Out_writes_opt_(Count) LPDWORD Results)
{
    DS_BUFFER_ENTER();
    if ((Count == 0) != (Effects == nullptr))
        return E_INVALIDARG;
    if (Count == 0 && Results != nullptr)
        return E_INVALIDARG;
    for (DWORD Index = 0; Index < Count; Index++)
    {
        if (!DsIsValidEffectDesc(&Effects[Index]))
            return E_INVALIDARG;
    }
    return Self->SetFx(Count, Effects, Results);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::AcquireResources(
    DWORD Flags,
    DWORD Count,
    _Out_writes_opt_(Count) LPDWORD Results)
{
    DS_BUFFER_ENTER();
    if (Flags & ~DS_ACQUIRE_FLAGS)
        return E_INVALIDARG;
    if ((Flags & DS_PLAY_TERMINATE) == DS_PLAY_TERMINATE)
        return E_INVALIDARG;
    if ((Results == nullptr) != (Count == 0))
        return E_INVALIDARG;
    return Self->AcquireResources(Flags, Count, Results);
}

HRESULT
STDMETHODCALLTYPE
DsBufferFace::GetObjectInPath(
    _In_ REFGUID Object,
    DWORD Index,
    _In_ REFGUID Interface,
    _Outptr_ LPVOID* Found)
{
    DS_BUFFER_ENTER();
    if (Found == nullptr)
        return E_INVALIDARG;
    return Self->GetObjectInPath(Object, Index, Interface, Found);
}

HRESULT
STDMETHODCALLTYPE
DsNotifyFace::SetNotificationPositions(
    DWORD Count,
    _In_reads_(Count) LPCDSBPOSITIONNOTIFY Positions)
{
    DsApiGuard Guard;

    if (Count != 0 && Positions == nullptr)
        return E_INVALIDARG;
    if (Render != nullptr)
    {
        if (FAILED(Render->IsInit()))
            return Render->IsInit();
        return Render->SetNotifications(Count, Positions);
    }
    return DsCaptureNotify(Capture, Count, Positions);
}

HRESULT
STDMETHODCALLTYPE
DsPropertyFace::Get(
    _In_ REFGUID Set,
    ULONG Id,
    _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
    ULONG InstanceLength,
    _Out_writes_bytes_(DataLength) LPVOID Data,
    ULONG DataLength,
    _Out_opt_ PULONG Returned)
{
    UNREFERENCED_PARAMETER(Set);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(InstanceLength);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(DataLength);

    if (Returned != nullptr)
        *Returned = 0;
    return DS_PROPERTY_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
DsPropertyFace::Set(
    _In_ REFGUID Set,
    ULONG Id,
    _In_reads_bytes_opt_(InstanceLength) LPVOID Instance,
    ULONG InstanceLength,
    _In_reads_bytes_(DataLength) LPVOID Data,
    ULONG DataLength)
{
    UNREFERENCED_PARAMETER(Set);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(InstanceLength);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(DataLength);
    return DS_PROPERTY_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
DsPropertyFace::QuerySupport(
    _In_ REFGUID Set,
    ULONG Id,
    _Out_ PULONG Support)
{
    UNREFERENCED_PARAMETER(Set);
    UNREFERENCED_PARAMETER(Id);

    if (Support != nullptr)
        *Support = 0;
    return DS_PROPERTY_UNSUPPORTED;
}

DsSecondary::DsSecondary(
    _In_ DsSound* NewOwner) : DsBuffer(NewOwner)
{
    NextBuffer = nullptr;
    Data = nullptr;
    Volume = 0;
    Pan = 0;
    Frequency = 0;
    Mute = 0;
    Special = S_OK;
    Algorithm = GUID_NULL;
    LockCount = 0;
    Spatial = nullptr;
    Priority = 0;
    Effects = nullptr;
    NotifyFace.Link.Object = this;
    NotifyFace.Link.Count = 0;
    NotifyFace.Render = this;
    NotifyFace.Capture = nullptr;
}

DsSecondary::~DsSecondary()
{
    if (Voice.Device != nullptr)
    {
        Voice.Device->RemoveVoice(&Voice);
        Voice.Device = nullptr;
    }
    if (Spatial != nullptr)
        DsDestroy3dBuffer(Spatial);
    DsFreeEffects(this);
    Owner->Forget(this);
    HeapFree(GetProcessHeap(), 0, Data);
    HeapFree(GetProcessHeap(), 0, Format);
}

/* Builds the buffer from a description, or as a copy sharing nothing but contents and settings */
HRESULT
DsSecondary::Initialize(
    _In_opt_ const DSBUFFERDESC* Desc,
    _In_opt_ DsSecondary* Source)
{
    WAVEFORMATEX* Wave;
    HRESULT hr;

    if (Source != nullptr)
    {
        Flags = Source->Flags;
        Bytes = Source->Bytes;
        Algorithm = Source->Algorithm;
        Format = DsCopyFormat(Source->Format);
    }
    else
    {
        Flags = Desc->dwFlags;
        Algorithm = Desc->guid3DAlgorithm;
        Format = DsCopyFormat(Desc->lpwfxFormat);
        if (Format != nullptr)
            Bytes = DsAlignSize(Format, Desc->dwBufferBytes);
    }
    if (Format == nullptr)
        return E_OUTOFMEMORY;

    if (Flags & DSBCAPS_LOCHARDWARE)
    {
        DS_TRACE_ONCE(L"hardware buffers are not supported");
        return E_NOTIMPL;
    }
    hr = DsCheckEngineFormat(Format);
    if (FAILED(hr))
        return hr;

    /* The mixer works from its own copy, which may fill in a speaker layout */
    Wave = DsCopyFormat(Format);
    if (Wave == nullptr)
        return E_OUTOFMEMORY;
    hr = InitializeVoice(Wave, Source);
    HeapFree(GetProcessHeap(), 0, Wave);
    if (FAILED(hr))
        return hr;

    Frequency = Format->nSamplesPerSec;
    if (Source != nullptr)
    {
        if (Flags & DSBCAPS_CTRLVOLUME)
            Volume = Source->Volume;
        if (Flags & DSBCAPS_CTRLPAN)
            Pan = Source->Pan;
        if ((Flags & DSBCAPS_CTRLFREQUENCY) && Source->Frequency != Frequency)
        {
            Frequency = Source->Frequency;
            Voice.SetFrequency(Frequency);
        }
    }

    if (Flags & DSBCAPS_CTRL3D)
    {
        hr = DsCreate3dBuffer(this, Source != nullptr ? Source->Spatial : nullptr, &Spatial);
        if (FAILED(hr))
            return hr;
        Special = Ds3dSpecialCode(Spatial);
    }
    ApplyAttenuation();

    if (!(Flags & DSBCAPS_LOCDEFER))
        Flags |= DSBCAPS_LOCSOFTWARE;

    if (Version >= DS_VERSION_8)
        Expose(IID_IDirectSoundBuffer8, &Face, &Face.Link);
    if (Flags & DSBCAPS_CTRLPOSITIONNOTIFY)
        Expose(IID_IDirectSoundNotify, &NotifyFace, &NotifyFace.Link);

    NextBuffer = Owner->Buffers;
    Owner->Buffers = this;
    if (Owner->Level == DSSCL_WRITEPRIMARY)
        Lose();
    InitStatus = S_OK;
    return S_OK;
}

/* Checks the format against what the mixer takes and sets up the data and its voice */
HRESULT
DsSecondary::InitializeVoice(
    _Inout_ WAVEFORMATEX* Wave,
    _In_opt_ DsSecondary* Source)
{
    WORD Tag;

    Tag = DsFormatTag(Wave);
    if (Tag != WAVE_FORMAT_PCM && Tag != WAVE_FORMAT_IEEE_FLOAT)
    {
        DS_TRACE_ONCE(L"format tag 0x%04X has no mixer path", Tag);
        return E_NOTIMPL;
    }

    /* Unplaced mono and stereo streams take the usual speakers; wider ones must match the mix */
    if (Wave->wFormatTag == WAVE_FORMAT_EXTENSIBLE && reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Wave)->dwChannelMask == 0)
    {
        WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Wave);

        if (Wave->nChannels == 1)
        {
            Extensible->dwChannelMask = SPEAKER_FRONT_CENTER;
        }
        else if (Wave->nChannels == 2)
        {
            Extensible->dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        }
        else
        {
            if (Wave->nChannels != Owner->Device->Channels)
                return DSERR_BADFORMAT;
            if (Flags & (DSBCAPS_CTRL3D | DSBCAPS_CTRLPAN))
                return E_INVALIDARG;
        }
    }

    Data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Bytes));
    if (Data == nullptr)
        return E_OUTOFMEMORY;
    if (Source != nullptr)
        memcpy(Data, Source->Data, Bytes);
    else if (!(Flags & DS_BUFFER_SINKIN) && Version < DS_VERSION_8)
        DsFillSilence(Data, Bytes, Wave->wBitsPerSample);

    return Voice.Setup(Owner->Device, Wave, Data, Bytes);
}

HRESULT
DsSecondary::ApplyAttenuation(VOID)
{
    DsAttenuation Attenuation;

    DsResolveVolumePan(Volume, Pan, &Attenuation);
    if (Spatial != nullptr && !Ds3dFilterAttenuation(Spatial, &Attenuation))
        return S_OK;
    Voice.SetAttenuation(&Attenuation);
    return S_OK;
}

/* A lost buffer stops and refuses playback until restored */
VOID
DsSecondary::Lose(VOID)
{
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return;
    Voice.SetState(0);
    StatusFlags |= DS_STATUS_LOST_INTERNAL;
}

HRESULT
DsSecondary::GetCaps(
    _Inout_ DSBCAPS* Caps)
{
    Caps->dwFlags = Flags & DS_SECONDARY_FLAGS;
    Caps->dwBufferBytes = (Flags & DS_BUFFER_SINKIN) ? 0 : Bytes;
    Caps->dwUnlockTransferRate = 0;
    Caps->dwPlayCpuOverhead = 0;
    return S_OK;
}

HRESULT
DsSecondary::GetCurrentPosition(
    _Out_opt_ DWORD* Play,
    _Out_opt_ DWORD* Write)
{
    DWORD PlayAt = 0;
    DWORD WriteAt = 0;
    ULONG Align = Format->nBlockAlign;

    if (Flags & DS_BUFFER_SINKIN)
        return DSERR_INVALIDCALL;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;

    Voice.GetCursors(&PlayAt, &WriteAt);
    if (Play != nullptr)
        *Play = PlayAt / Align * Align;
    if (Write != nullptr)
        *Write = WriteAt / Align * Align;
    return S_OK;
}

HRESULT
DsSecondary::GetFormat(
    _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Out,
    _Inout_ DWORD* Size)
{
    return DsCopyFormatOut(Format, Out, Size);
}

HRESULT
DsSecondary::GetVolume(
    _Out_ LONG* Value)
{
    if (!(Flags & DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;
    *Value = Volume;
    return S_OK;
}

HRESULT
DsSecondary::GetPan(
    _Out_ LONG* Value)
{
    if (!(Flags & DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;
    *Value = Pan;
    return S_OK;
}

HRESULT
DsSecondary::GetFrequency(
    _Out_ DWORD* Value)
{
    if (!(Flags & DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;
    *Value = Frequency;
    return S_OK;
}

/* Reading the status also notices a one shot that ran out and stops it */
HRESULT
DsSecondary::GetStatus(
    _Out_opt_ DWORD* Status)
{
    DWORD Result = 0;
    ULONG State;

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
    {
        Result = DSBSTATUS_BUFFERLOST;
    }
    else
    {
        BOOLEAN WasPlaying = (StatusFlags & DSBSTATUS_PLAYING) != 0;

        State = Voice.GetState();
        StatusFlags &= ~(DSBSTATUS_PLAYING | DSBSTATUS_LOOPING);
        if (State & DS_VOICE_PLAYING)
        {
            StatusFlags |= DSBSTATUS_PLAYING;
            if (State & DS_VOICE_LOOPING)
                StatusFlags |= DSBSTATUS_LOOPING;
        }
        if (WasPlaying && !(StatusFlags & DSBSTATUS_PLAYING))
            Stop();
        Result = StatusFlags & (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING);
        if (Flags & DSBCAPS_LOCDEFER)
            Result |= (Flags & DSBCAPS_LOCSOFTWARE) ? DSBSTATUS_LOCSOFTWARE : 0;
    }
    if (Status != nullptr)
        *Status = Result;
    return S_OK;
}

/* Two locks may not share a byte; a zero length part never conflicts */
HRESULT
DsSecondary::TrackLock(
    _In_ BYTE* Pointer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (Length != 0)
    {
        for (UINT Index = 0; Index < LockCount; Index++)
        {
            const DsLockRegion* Held = &Locks[Index];

            if (Held->Bytes == 0)
                continue;
            if (Held->Offset == Offset)
                return E_INVALIDARG;
            if (Offset < Held->Offset && Offset + Length > Held->Offset)
                return E_INVALIDARG;
            if (Held->Offset < Offset && Held->Offset + Held->Bytes > Offset)
                return E_INVALIDARG;
        }
    }
    if (LockCount == DS_MAX_LOCKS)
        return E_OUTOFMEMORY;
    Locks[LockCount].Pointer = Pointer;
    Locks[LockCount].Offset = Offset;
    Locks[LockCount].Bytes = Length;
    LockCount++;
    return S_OK;
}

BOOLEAN
DsSecondary::UntrackLock(
    _In_ VOID* Pointer)
{
    for (UINT Index = 0; Index < LockCount; Index++)
    {
        if (Locks[Index].Pointer != Pointer)
            continue;
        Locks[Index] = Locks[LockCount - 1];
        LockCount -= 1;
        return TRUE;
    }
    return FALSE;
}

HRESULT
DsSecondary::Lock(
    DWORD Offset,
    DWORD Length,
    _Out_ VOID** Part1,
    _Out_ DWORD* Bytes1,
    _Out_opt_ VOID** Part2,
    _Out_opt_ DWORD* Bytes2,
    DWORD LockFlags)
{
    BYTE* Base = DsEffectInput(this);
    ULONG First;
    ULONG Second;
    HRESULT hr;

    if (Flags & DS_BUFFER_SINKIN)
        return DSERR_INVALIDCALL;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (LockFlags & DSBLOCK_FROMWRITECURSOR)
    {
        hr = GetCurrentPosition(nullptr, &Offset);
        if (FAILED(hr))
            return hr;
    }
    if (LockFlags & DSBLOCK_ENTIREBUFFER)
        Length = Bytes;
    if (Offset >= Bytes || Length > Bytes || Length == 0)
        return E_INVALIDARG;

    if (Offset + Length > Bytes)
    {
        First = Bytes - Offset;
        Second = Offset + Length - Bytes;
    }
    else
    {
        First = Length;
        Second = 0;
    }

    /* A second part that collides leaves the first one held, as the system does */
    hr = TrackLock(Base + Offset, Offset, First);
    if (SUCCEEDED(hr) && Second != 0)
        hr = TrackLock(Base, 0, Second);
    if (FAILED(hr))
        return hr;

    *Part1 = Base + Offset;
    *Bytes1 = First;
    if (Part2 != nullptr)
        *Part2 = Second != 0 ? Base : nullptr;
    if (Bytes2 != nullptr)
        *Bytes2 = Second;
    return S_OK;
}

HRESULT
DsSecondary::Unlock(
    _In_opt_ VOID* Part1,
    DWORD Bytes1,
    _In_opt_ VOID* Part2,
    DWORD Bytes2)
{
    UNREFERENCED_PARAMETER(Bytes1);
    UNREFERENCED_PARAMETER(Bytes2);

    if (Flags & DS_BUFFER_SINKIN)
        return DSERR_INVALIDCALL;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (Part1 != nullptr && !UntrackLock(Part1))
        return E_INVALIDARG;
    if (Part2 != nullptr && !UntrackLock(Part2))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
DsSecondary::Play(
    DWORD NewPriority,
    DWORD PlayFlags)
{
    HRESULT hr;

    if (Owner->LevelThread == 0 || Owner->Level == DS_LEVEL_NONE)
        return DSERR_PRIOLEVELNEEDED;
    if (NewPriority != 0 && !(Flags & DSBCAPS_LOCDEFER))
        return E_INVALIDARG;
    if ((PlayFlags & DS_PLAY_DEFER_FLAGS) && !(Flags & DSBCAPS_LOCDEFER))
        return E_INVALIDARG;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;

    GetStatus(nullptr);
    Priority = NewPriority;
    Special = S_OK;
    if (Spatial != nullptr)
        Special = Ds3dSpecialCode(Spatial);
    if (Flags & DSBCAPS_LOCDEFER)
        Flags |= DSBCAPS_LOCSOFTWARE;

    hr = Voice.SetState(DS_VOICE_PLAYING | ((PlayFlags & DSBPLAY_LOOPING) ? DS_VOICE_LOOPING : 0));
    if (FAILED(hr))
        return hr;
    StatusFlags |= DSBSTATUS_PLAYING;
    return S_OK;
}

HRESULT
DsSecondary::SetCurrentPosition(
    DWORD Position)
{
    if (Flags & DS_BUFFER_SINKIN)
        return DSERR_INVALIDCALL;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (Position >= Bytes)
        return E_INVALIDARG;
    Voice.SetPosition(Position / Format->nBlockAlign * Format->nBlockAlign);
    return S_OK;
}

HRESULT
DsSecondary::SetFormat(
    _In_ const WAVEFORMATEX* NewFormat)
{
    UNREFERENCED_PARAMETER(NewFormat);
    return DSERR_INVALIDCALL;
}

HRESULT
DsSecondary::SetVolume(
    LONG Value)
{
    LONG Old = Volume;

    if (!(Flags & DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;
    if (Value == Volume)
        return S_OK;
    Volume = Value;
    if (FAILED(ApplyAttenuation()))
        Volume = Old;
    return S_OK;
}

HRESULT
DsSecondary::SetPan(
    LONG Value)
{
    if (!(Flags & DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;
    if (Value == Pan)
        return S_OK;
    Pan = Value;
    return ApplyAttenuation();
}

HRESULT
DsSecondary::SetFrequency(
    DWORD Value)
{
    if (!(Flags & DSBCAPS_CTRLFREQUENCY))
        return DSERR_CONTROLUNAVAIL;
    if (Value == DSBFREQUENCY_ORIGINAL)
        Value = Format->nSamplesPerSec;
    if (Value < DSBFREQUENCY_MIN || Value > DSBFREQUENCY_MAX)
        return E_INVALIDARG;
    if (Value == Frequency)
        return S_OK;
    if (Spatial == nullptr || Ds3dFilterFrequency(Spatial, Value))
        Voice.SetFrequency(Value);
    Frequency = Value;
    return S_OK;
}

HRESULT
DsSecondary::Stop(VOID)
{
    HRESULT hr;

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    hr = Voice.SetState(0);
    if (FAILED(hr))
        return hr;
    StatusFlags &= ~(DSBSTATUS_PLAYING | DSBSTATUS_LOOPING);
    Priority = 0;
    return S_OK;
}

HRESULT
DsSecondary::Restore(VOID)
{
    if (Flags & DS_BUFFER_SINKIN)
        return DSERR_INVALIDCALL;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
    {
        if (Owner->Level == DSSCL_WRITEPRIMARY)
            return DSERR_BUFFERLOST;
        StatusFlags &= ~DS_STATUS_LOST_INTERNAL;
    }
    return S_OK;
}

HRESULT
DsSecondary::SetFx(
    DWORD Count,
    _In_reads_opt_(Count) DSEFFECTDESC* EffectList,
    _Out_writes_opt_(Count) DWORD* Results)
{
    if (!(Flags & DSBCAPS_CTRLFX))
        return DSERR_CONTROLUNAVAIL;
    if (Voice.GetState() & DS_VOICE_PLAYING)
        return DSERR_INVALIDCALL;
    if (LockCount != 0)
        return DSERR_INVALIDCALL;
    return DsSetEffects(this, Count, EffectList, Results);
}

HRESULT
DsSecondary::AcquireResources(
    DWORD AcquireFlags,
    DWORD Count,
    _Out_writes_opt_(Count) DWORD* Results)
{
    UNREFERENCED_PARAMETER(AcquireFlags);

    if (!(Flags & DSBCAPS_LOCDEFER))
        return DSERR_INVALIDCALL;
    if (Count != 0 && Count != DsEffectCount(this))
        return E_INVALIDARG;
    Flags |= DSBCAPS_LOCSOFTWARE;
    for (DWORD Index = 0; Index < Count; Index++)
        Results[Index] = DSFXR_LOCSOFTWARE;
    return S_OK;
}

HRESULT
DsSecondary::GetObjectInPath(
    REFGUID Object,
    DWORD Index,
    REFGUID Interface,
    _Outptr_ VOID** Found)
{
    *Found = nullptr;
    if (!(Flags & DSBCAPS_CTRLFX))
        return DSERR_CONTROLUNAVAIL;
    return DsFindEffect(this, Object, Index, Interface, Found);
}

/* Offsets snap to whole frames and sort; the stop entry may only come last */
HRESULT
DsSecondary::SetNotifications(
    _In_ DWORD Count,
    _In_reads_(Count) const DSBPOSITIONNOTIFY* Positions)
{
    DsNotifyPoint* Points = nullptr;
    ULONG Align = Format->nBlockAlign;
    HRESULT hr = S_OK;

    if (!(Flags & DSBCAPS_CTRLPOSITIONNOTIFY))
        return DSERR_CONTROLUNAVAIL;

    if (Count != 0)
    {
        Points = static_cast<DsNotifyPoint*>(HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*Points)));
        if (Points == nullptr)
            return E_OUTOFMEMORY;
        for (DWORD Index = 0; Index < Count; Index++)
        {
            Points[Index].Offset = Positions[Index].dwOffset;
            Points[Index].Event = Positions[Index].hEventNotify;
            if (Points[Index].Offset != DSBPN_OFFSETSTOP)
                Points[Index].Offset = Points[Index].Offset / Align * Align;
        }
        for (DWORD Index = 1; Index < Count; Index++)
        {
            DsNotifyPoint Point = Points[Index];
            DWORD Slot = Index;

            while (Slot > 0 && Points[Slot - 1].Offset > Point.Offset)
            {
                Points[Slot] = Points[Slot - 1];
                Slot -= 1;
            }
            Points[Slot] = Point;
        }
        for (DWORD Index = 0; Index < Count && SUCCEEDED(hr); Index++)
        {
            if (Points[Index].Offset != DSBPN_OFFSETSTOP && Points[Index].Offset >= Bytes)
                hr = E_INVALIDARG;
            else if (Points[Index].Event == nullptr)
                hr = E_INVALIDARG;
            else if (Index + 1 < Count &&
                     (Points[Index].Offset == DSBPN_OFFSETSTOP || Points[Index].Offset == Points[Index + 1].Offset))
                hr = E_INVALIDARG;
        }
    }

    if (SUCCEEDED(hr) && (Voice.GetState() & DS_VOICE_PLAYING))
        hr = DSERR_INVALIDCALL;
    if (SUCCEEDED(hr))
        hr = Voice.SetNotifications(Count, Points);
    HeapFree(GetProcessHeap(), 0, Points);
    return hr;
}
