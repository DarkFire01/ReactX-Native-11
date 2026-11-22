/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The primary buffer: device format, master volume and direct writing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

/* The primary buffer holds this much audio in the device format */
#define DS_PRIMARY_BYTES    0x8000

/* The primary plays implicitly unless the application writes it */
#define DS_PRIMARY_IMPLICIT 0x4

DsPrimary::DsPrimary(
    _In_ DsSound* NewOwner) : DsBuffer(NewOwner)
{
    UserReferences = 0;
    WritePrimary = FALSE;
    PlayState = 0;
    Locked = FALSE;
    Voice = nullptr;
    Data = nullptr;
    Listener = nullptr;
    LockCount = 0;
}

DsPrimary::~DsPrimary()
{
    CloseVoice();
    if (Listener != nullptr)
        DsDestroyListener(Listener);
    HeapFree(GetProcessHeap(), 0, Format);
}

/* Starts out able to answer as a listener, then takes the flags of the first request */
HRESULT
DsPrimary::Initialize(VOID)
{
    HRESULT hr;

    hr = DsCreateListener(this, &Listener);
    if (FAILED(hr))
        return hr;

    Flags = DSBCAPS_LOCSOFTWARE;
    Bytes = DsAlignSize(Owner->Device->Global, DS_PRIMARY_BYTES);
    Format = DsCopyFormat(Owner->Device->Global);
    if (Format == nullptr)
        return E_OUTOFMEMORY;
    Flags |= DSBCAPS_CTRL3D;
    hr = SetFlags(DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D);
    if (FAILED(hr))
        return hr;
    hr = SetLevel(Owner->Level);
    if (FAILED(hr))
        return hr;
    InitStatus = S_OK;
    return S_OK;
}

HRESULT
DsPrimary::SetFlags(
    _In_ DWORD NewFlags)
{
    if ((NewFlags & DSBCAPS_CTRL3D) && Listener == nullptr)
        return DSERR_CONTROLUNAVAIL;
    if ((Flags & DSBCAPS_CTRL3D) != (NewFlags & DSBCAPS_CTRL3D) || (NewFlags & DSBCAPS_CTRL3D))
    {
        if (NewFlags & DSBCAPS_CTRL3D)
            DsExposeListener(this, Listener);
        else
            Hide(IID_IDirectSound3DListener);
    }
    Flags = (NewFlags & ~(DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE)) | (Flags & (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE));
    return S_OK;
}

/* A second request for the primary buffer gets the same one, its flags unchanged while it is held */
HRESULT
DsPrimary::OnCreate(
    _In_ DWORD NewFlags)
{
    HRESULT hr;

    if (UserReferences == 0)
    {
        hr = SetFlags(NewFlags);
        if (FAILED(hr))
            return hr;
    }
    InterlockedIncrement(&UserReferences);
    return S_OK;
}

ULONG
DsPrimary::Grab(VOID)
{
    return static_cast<ULONG>(InterlockedIncrement(&UserReferences));
}

ULONG
DsPrimary::Drop(VOID)
{
    if (UserReferences == 0)
        return 0;
    return static_cast<ULONG>(InterlockedDecrement(&UserReferences));
}

HRESULT
DsPrimary::OpenVoice(VOID)
{
    HRESULT hr;

    if (Voice != nullptr)
        return S_OK;
    Data = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Bytes));
    Voice = new (std::nothrow) DsVoice();
    if (Data == nullptr || Voice == nullptr)
    {
        CloseVoice();
        return E_OUTOFMEMORY;
    }
    hr = Voice->Setup(Owner->Device, Owner->Device->Global, Data, Bytes);
    if (FAILED(hr))
    {
        CloseVoice();
        return hr;
    }
    {
        DsAttenuation Attenuation;

        DsResolveVolumePan(0, 0, &Attenuation);
        Voice->SetAttenuation(&Attenuation);
    }
    return S_OK;
}

VOID
DsPrimary::CloseVoice(VOID)
{
    delete Voice;
    Voice = nullptr;
    HeapFree(GetProcessHeap(), 0, Data);
    Data = nullptr;
    LockCount = 0;
}

HRESULT
DsPrimary::ApplyState(
    _In_ ULONG State)
{
    HRESULT hr = S_OK;

    if (WritePrimary)
        State &= ~DS_PRIMARY_IMPLICIT;
    else
        State |= DS_PRIMARY_IMPLICIT;
    if (State == PlayState)
        return S_OK;

    if (!(State & DS_PRIMARY_IMPLICIT) && Voice != nullptr)
    {
        hr = Voice->SetState(State & (DS_VOICE_PLAYING | DS_VOICE_LOOPING));
        if (SUCCEEDED(hr) && !(State & DS_VOICE_PLAYING))
            Voice->SetPosition(0);
    }
    if (SUCCEEDED(hr))
        PlayState = State;
    return hr;
}

/* Entering or leaving the write primary level swaps the direct voice in or out, silent */
HRESULT
DsPrimary::SetLevel(
    _In_ DWORD Level)
{
    BOOLEAN Writing = Level == DSSCL_WRITEPRIMARY;
    HRESULT hr;

    if (Writing != WritePrimary)
    {
        WritePrimary = Writing;
        if (Writing)
        {
            hr = OpenVoice();
            if (FAILED(hr))
            {
                WritePrimary = FALSE;
                return hr;
            }
            DsFillSilence(Data, Bytes, Format->wBitsPerSample);
        }
        else
        {
            CloseVoice();
        }
        ApplyState(0);
    }
    if ((StatusFlags & DS_STATUS_LOST_INTERNAL) && !Writing)
        StatusFlags &= ~DS_STATUS_LOST_INTERNAL;
    return S_OK;
}

HRESULT
DsPrimary::GetCaps(
    _Inout_ DSBCAPS* Caps)
{
    Caps->dwFlags = Flags;
    Caps->dwBufferBytes = Bytes;
    Caps->dwUnlockTransferRate = 0;
    Caps->dwPlayCpuOverhead = 0;
    return S_OK;
}

HRESULT
DsPrimary::GetCurrentPosition(
    _Out_opt_ DWORD* Play,
    _Out_opt_ DWORD* Write)
{
    DWORD PlayAt = 0;
    DWORD WriteAt = 0;
    ULONG Align = Format->nBlockAlign;

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (!WritePrimary)
        return DSERR_PRIOLEVELNEEDED;
    Voice->GetCursors(&PlayAt, &WriteAt);
    if (Play != nullptr)
        *Play = PlayAt / Align * Align;
    if (Write != nullptr)
        *Write = WriteAt / Align * Align;
    return S_OK;
}

HRESULT
DsPrimary::GetFormat(
    _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Out,
    _Inout_ DWORD* Size)
{
    return DsCopyFormatOut(Format, Out, Size);
}

HRESULT
DsPrimary::GetVolume(
    _Out_ LONG* Value)
{
    if (!(Flags & DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;
    return Owner->Device->GetMasterVolume(Value);
}

HRESULT
DsPrimary::GetPan(
    _Out_ LONG* Value)
{
    if (!(Flags & DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;
    *Value = Owner->Device->Pan;
    return S_OK;
}

HRESULT
DsPrimary::GetFrequency(
    _Out_ DWORD* Value)
{
    UNREFERENCED_PARAMETER(Value);
    return DSERR_CONTROLUNAVAIL;
}

HRESULT
DsPrimary::GetStatus(
    _Out_opt_ DWORD* Status)
{
    DWORD Result;

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
    {
        Result = DSBSTATUS_BUFFERLOST;
    }
    else
    {
        ULONG State = PlayState;

        if (!(State & DS_PRIMARY_IMPLICIT) && Voice != nullptr && (State & DS_VOICE_PLAYING) &&
            !(Voice->GetState() & DS_VOICE_PLAYING))
        {
            State = 0;
        }
        Result = 0;
        if (State & DS_VOICE_PLAYING)
        {
            Result |= DSBSTATUS_PLAYING;
            if (State & DS_VOICE_LOOPING)
                Result |= DSBSTATUS_LOOPING;
        }
        if (Flags & DSBCAPS_LOCDEFER)
            Result |= (Flags & DSBCAPS_LOCSOFTWARE) ? DSBSTATUS_LOCSOFTWARE : 0;
    }
    if (Status != nullptr)
        *Status = Result;
    return S_OK;
}

HRESULT
DsPrimary::Lock(
    DWORD Offset,
    DWORD Length,
    _Out_ VOID** Part1,
    _Out_ DWORD* Bytes1,
    _Out_opt_ VOID** Part2,
    _Out_opt_ DWORD* Bytes2,
    DWORD LockFlags)
{
    ULONG First;
    ULONG Second;
    HRESULT hr;

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (!WritePrimary)
        return DSERR_PRIOLEVELNEEDED;
    Locked = TRUE;
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
    for (UINT Index = 0; Index < LockCount; Index++)
    {
        const DsLockRegion* Held = &Locks[Index];

        if (Held->Bytes == 0 || First == 0)
            continue;
        if (Held->Offset == Offset || (Offset < Held->Offset && Offset + First > Held->Offset) ||
            (Held->Offset < Offset && Held->Offset + Held->Bytes > Offset))
        {
            return E_INVALIDARG;
        }
    }
    if (LockCount + 2 > DS_MAX_LOCKS)
        return E_OUTOFMEMORY;
    Locks[LockCount++] = { Data + Offset, Offset, First };
    if (Second != 0)
        Locks[LockCount++] = { Data, 0, Second };

    *Part1 = Data + Offset;
    *Bytes1 = First;
    if (Part2 != nullptr)
        *Part2 = Second != 0 ? Data : nullptr;
    if (Bytes2 != nullptr)
        *Bytes2 = Second;
    return S_OK;
}

HRESULT
DsPrimary::Unlock(
    _In_opt_ VOID* Part1,
    DWORD Bytes1,
    _In_opt_ VOID* Part2,
    DWORD Bytes2)
{
    VOID* Parts[2] = { Part1, Part2 };

    UNREFERENCED_PARAMETER(Bytes1);
    UNREFERENCED_PARAMETER(Bytes2);

    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    if (!WritePrimary)
        return DSERR_PRIOLEVELNEEDED;
    for (UINT Part = 0; Part < 2; Part++)
    {
        BOOLEAN Found = FALSE;

        if (Parts[Part] == nullptr)
            continue;
        for (UINT Index = 0; Index < LockCount; Index++)
        {
            if (Locks[Index].Pointer == Parts[Part])
            {
                LockCount -= 1;
                Locks[Index] = Locks[LockCount];
                Found = TRUE;
                break;
            }
        }
        if (!Found)
            return E_INVALIDARG;
    }
    return S_OK;
}

HRESULT
DsPrimary::Play(
    DWORD Priority,
    DWORD PlayFlags)
{
    if (PlayFlags != DSBPLAY_LOOPING || Priority != 0)
        return E_INVALIDARG;
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    return ApplyState(DS_VOICE_PLAYING | DS_VOICE_LOOPING);
}

HRESULT
DsPrimary::SetCurrentPosition(
    DWORD Position)
{
    UNREFERENCED_PARAMETER(Position);
    return DSERR_INVALIDCALL;
}

/* The device format follows the primary; written directly it must be one the mixer takes as is */
HRESULT
DsPrimary::SetFormat(
    _In_ const WAVEFORMATEX* NewFormat)
{
    WAVEFORMATEX* Copy;
    BOOLEAN Resized;
    HRESULT hr;

    if (Owner->Level < DSSCL_PRIORITY)
        return DSERR_PRIOLEVELNEEDED;
    Copy = DsCopyFormat(NewFormat);
    if (Copy == nullptr)
        return E_OUTOFMEMORY;

    if (Owner->Level == DSSCL_WRITEPRIMARY)
    {
        hr = DsCheckEngineFormat(Copy);
        if (FAILED(hr))
        {
            HeapFree(GetProcessHeap(), 0, Copy);
            return hr;
        }
    }

    Resized = Copy->wBitsPerSample != Format->wBitsPerSample;
    HeapFree(GetProcessHeap(), 0, Format);
    Format = Copy;

    if (DsSameFormat(Owner->Device->Global, Copy))
        return S_OK;
    HeapFree(GetProcessHeap(), 0, Owner->Device->Global);
    Owner->Device->Global = DsCopyFormat(Copy);
    if (Owner->Device->Global == nullptr)
        return E_OUTOFMEMORY;

    if (WritePrimary)
    {
        ULONG State = PlayState;

        CloseVoice();
        hr = OpenVoice();
        if (FAILED(hr))
            return hr;
        if (Resized && !Locked)
            DsFillSilence(Data, Bytes, Format->wBitsPerSample);
        PlayState = 0;
        ApplyState(State);
    }
    return S_OK;
}

HRESULT
DsPrimary::SetVolume(
    LONG Value)
{
    if (!(Flags & DSBCAPS_CTRLVOLUME))
        return DSERR_CONTROLUNAVAIL;
    return Owner->Device->SetMasterVolume(Value);
}

HRESULT
DsPrimary::SetPan(
    LONG Value)
{
    if (!(Flags & DSBCAPS_CTRLPAN))
        return DSERR_CONTROLUNAVAIL;
    Owner->Device->Pan = Value;
    return S_OK;
}

HRESULT
DsPrimary::SetFrequency(
    DWORD Value)
{
    UNREFERENCED_PARAMETER(Value);
    return DSERR_CONTROLUNAVAIL;
}

HRESULT
DsPrimary::Stop(VOID)
{
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
        return DSERR_BUFFERLOST;
    return ApplyState(0);
}

HRESULT
DsPrimary::Restore(VOID)
{
    if (StatusFlags & DS_STATUS_LOST_INTERNAL)
    {
        StatusFlags &= ~DS_STATUS_LOST_INTERNAL;
        return SetLevel(Owner->Level);
    }
    return S_OK;
}

HRESULT
DsPrimary::SetFx(
    DWORD Count,
    _In_reads_opt_(Count) DSEFFECTDESC* Effects,
    _Out_writes_opt_(Count) DWORD* Results)
{
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Effects);
    UNREFERENCED_PARAMETER(Results);
    return DSERR_CONTROLUNAVAIL;
}

HRESULT
DsPrimary::AcquireResources(
    DWORD AcquireFlags,
    DWORD Count,
    _Out_writes_opt_(Count) DWORD* Results)
{
    UNREFERENCED_PARAMETER(AcquireFlags);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Results);
    return DSERR_INVALIDCALL;
}

HRESULT
DsPrimary::GetObjectInPath(
    REFGUID Object,
    DWORD Index,
    REFGUID Interface,
    _Outptr_ VOID** Found)
{
    UNREFERENCED_PARAMETER(Object);
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Interface);

    *Found = nullptr;
    return DSERR_CONTROLUNAVAIL;
}
