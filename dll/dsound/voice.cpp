/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The mixer side of a buffer: cursors, notifications, conversion, rate change and channel mixing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <math.h>

DsVoice::DsVoice()
{
    ZeroMemory(this, sizeof(*this));
    Ratio = 1.0f;
}

DsVoice::~DsVoice()
{
    if (Device != nullptr)
        Device->RemoveVoice(this);
    HeapFree(GetProcessHeap(), 0, Points);
    HeapFree(GetProcessHeap(), 0, Format);
}

HRESULT
DsVoice::Setup(
    _In_ DsDevice* Owner,
    _In_ const WAVEFORMATEX* Wave,
    _In_ BYTE* Bytes,
    _In_ ULONG Length)
{
    ULONG Half;

    Format = DsCopyFormat(Wave);
    if (Format == nullptr)
        return E_OUTOFMEMORY;

    Data = Bytes;
    Size = Length;
    BlockAlign = Wave->nBlockAlign;
    Channels = Wave->nChannels;

    /* The play cursor trails the mixer by 30 ms, or half the buffer when that is less */
    Lead = Wave->nSamplesPerSec * DS_WRITE_LEAD_MS / 1000 * BlockAlign;
    Half = Size / 2;
    if (Half < Lead)
        Lead = Half / BlockAlign * BlockAlign;

    Device = Owner;
    Ratio = static_cast<float>(Wave->nSamplesPerSec) / static_cast<float>(Owner->Rate);
    Owner->Lock();
    if (Owner->CubicRates)
        StartCubic();
    else if (Wave->nSamplesPerSec != Owner->Rate)
        Resampler.Configure(Wave->nSamplesPerSec, Owner->Rate, Channels);
    Owner->Unlock();
    if (Ratio != 1.0f && !Resampler.Active)
        return E_OUTOFMEMORY;
    Owner->AddVoice(this);
    return S_OK;
}

/* Where play is, given where the mixer has read to, never moving back past the last answer */
ULONG
DsVoice::PlayFromRead(
    _In_ ULONG Read,
    _In_ ULONG Before)
{
    LONG Candidate = static_cast<LONG>(Read) - static_cast<LONG>(Lead);
    ULONG Play;

    if (Candidate < 0)
        Candidate += static_cast<LONG>(Size);
    Play = static_cast<ULONG>(Candidate);

    if (Read >= Before)
    {
        if (Play < Before || Play >= Read)
            Play = Before;
    }
    else if (Play < Before && Play >= Read)
    {
        Play = Before;
    }
    return Play;
}

VOID
DsVoice::GetCursors(
    _Out_opt_ DWORD* Play,
    _Out_opt_ DWORD* Write)
{
    ULONG Read = StopPosition;

    Device->Lock();
    if (State & DS_VOICE_PLAYING)
    {
        LONG Seen = Pass;

        Read = Seen == DS_PASS_SEEN ? ReadPosition : StopPosition;
        if (State == DS_VOICE_PLAYING && Seen == DS_PASS_FINISHED)
        {
            Read = 0;
            StopPosition = 0;
            State = 0;
        }
    }
    else
    {
        Read = StopPosition;
    }

    if (Write != nullptr)
        *Write = Read;
    if (Play != nullptr)
    {
        ULONG Where = State == 0 ? Read : PlayFromRead(Read, LastPlay);

        *Play = Where;
        LastPlay = Where;
    }
    Device->Unlock();
}

ULONG
DsVoice::GetState(VOID)
{
    if (State == DS_VOICE_PLAYING)
        GetCursors(nullptr, nullptr);
    return State;
}

HRESULT
DsVoice::SetState(
    _In_ ULONG NewState)
{
    if (State == DS_VOICE_PLAYING)
        GetCursors(nullptr, nullptr);
    if (NewState == State)
        return S_OK;

    Device->Lock();
    if (!(NewState & DS_VOICE_PLAYING))
    {
        LONG Before = Pass;

        Pass = DS_PASS_MOVED;
        if (PointCount != 0 && Points[PointCount - 1].Offset == DSBPN_OFFSETSTOP &&
            !(State == DS_VOICE_PLAYING && Before == DS_PASS_FINISHED))
        {
            SetEvent(Points[PointCount - 1].Event);
        }
        StopPosition = ReadPosition;
        State = NewState;
        Device->Unlock();
        return S_OK;
    }

    if (State == 0)
    {
        Pass = DS_PASS_MOVED;
        ReadPosition = StopPosition;
        LastPlay = StopPosition;
        Finished = FALSE;
        Resampler.Reset();
        if (Effects != nullptr)
            DsResetEffects(Effects);
        if (PointCount != 0)
        {
            NotifyPlay = StopPosition;
            NotifySeen = StopPosition;
        }
    }
    State = NewState;
    Device->Unlock();
    return Device->StartStreaming();
}

VOID
DsVoice::SetPosition(
    _In_ DWORD Position)
{
    if (State == DS_VOICE_PLAYING)
        GetCursors(nullptr, nullptr);

    Device->Lock();
    if (State & DS_VOICE_PLAYING)
    {
        ReadPosition = Position;
        Resampler.Reset();
        if (Effects != nullptr)
            DsResetEffects(Effects);
        Finished = FALSE;
        if (Pass == DS_PASS_SEEN)
            Pass = DS_PASS_MOVED;
    }
    LastPlay = Position;
    StopPosition = Position;
    Device->Unlock();
}

VOID
DsVoice::SetFrequency(
    _In_ DWORD Frequency)
{
    DWORD Old;
    DWORD Gap;

    Device->Lock();
    Old = Format->nSamplesPerSec;
    Gap = Old >= Frequency ? Old - Frequency : Frequency - Old - 1;

    /* A real rate change moves every buffer on the device to the cubic converter for good */
    if (!Device->CubicRates && Gap > Old / 500)
    {
        Device->UseCubicRates();
        DsTrace(L"rate change to %lu Hz moved the device to the cubic converter", Frequency);
    }

    Format->nSamplesPerSec = Frequency;
    Format->nAvgBytesPerSec = Frequency * Format->nBlockAlign;
    Ratio = static_cast<float>(Frequency) / static_cast<float>(Device->Rate);
    if (Resampler.Cubic)
    {
        Resampler.Retune(Frequency, Device->Rate);
    }
    else if (Frequency == Device->Rate)
    {
        Resampler.Release();
    }
    else if (Resampler.Active)
    {
        /* The converter takes the rate back from the ratio of the device rate to it */
        float Out = static_cast<float>(Device->Rate);
        float Scale = Out / static_cast<float>(Frequency);

        if (FAILED(Resampler.Rerate(Out / Scale)))
            DS_TRACE_ONCE(L"no memory for a %lu Hz rate converter", Frequency);
    }
    else if (Frequency != Device->Rate && FAILED(Resampler.Configure(Frequency, Device->Rate, Channels)))
    {
        DS_TRACE_ONCE(L"no memory for a %lu Hz rate converter", Frequency);
    }
    Device->Unlock();
}

/* Restarts conversion with the cubic converter from the current read position */
VOID
DsVoice::StartCubic(VOID)
{
    BOOLEAN Whole16 = DsFormatTag(Format) == WAVE_FORMAT_PCM && Format->wBitsPerSample == 16;

    if (FAILED(Resampler.ConfigureCubic(Format->nSamplesPerSec, Device->Rate, Channels, Whole16)))
        DS_TRACE_ONCE(L"no memory for a cubic rate converter");
}

/* Each output speaker takes the left, right or overall amplitude depending on its side of the room */
VOID
DsVoice::SetAttenuation(
    _In_ const DsAttenuation* Attenuation)
{
    static const DWORD LeftSide = SPEAKER_FRONT_LEFT | SPEAKER_BACK_LEFT | SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_SIDE_LEFT |
                                  SPEAKER_TOP_FRONT_LEFT | SPEAKER_TOP_BACK_LEFT;
    static const DWORD RightSide = SPEAKER_FRONT_RIGHT | SPEAKER_BACK_RIGHT | SPEAKER_FRONT_RIGHT_OF_CENTER | SPEAKER_SIDE_RIGHT |
                                   SPEAKER_TOP_FRONT_RIGHT | SPEAKER_TOP_BACK_RIGHT;
    static const DWORD Middle = SPEAKER_FRONT_CENTER | SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_CENTER | SPEAKER_TOP_CENTER |
                                SPEAKER_TOP_FRONT_CENTER | SPEAKER_TOP_BACK_CENTER;
    float Built[DS_MAX_CHANNELS * DS_MAX_CHANNELS];
    UINT OutChannels = Device->Channels;
    DWORD InMask = DsChannelMask(Format);
    DWORD OutMask = Device->Mask;

    if ((Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && InMask == 0) || OutMask == 0 ||
        FAILED(DsBuildChannelMatrix(Channels, OutChannels, InMask, OutMask, Built)))
    {
        /* Direct out streams feed one output per row from their first channel */
        ZeroMemory(Built, sizeof(Built));
        for (UINT Out = 0; Out < OutChannels; Out++)
        {
            if (Out * OutChannels < Channels * OutChannels)
                Built[Out * OutChannels] = 1.0f;
        }
    }
    else
    {
        DWORD Remaining = OutMask;

        for (UINT Out = 0; Out < OutChannels; Out++)
        {
            DWORD Speaker = Remaining & (0 - Remaining);
            float Gain = 1.0f;

            Remaining &= ~Speaker;
            if (Speaker & LeftSide)
                Gain = static_cast<float>(Attenuation->LeftAmplitude) * DsAmplitudeScale;
            else if (Speaker & RightSide)
                Gain = static_cast<float>(Attenuation->RightAmplitude) * DsAmplitudeScale;
            else if (Speaker & Middle)
                Gain = static_cast<float>(Attenuation->VolumeAmplitude) * DsAmplitudeScale;

            for (UINT In = 0; In < Channels; In++)
                Built[Out * Channels + In] *= Gain;
        }
    }

    CommitMatrix(Built);
}

/* New coefficients fade in over a few milliseconds from whatever was last in effect, once the voice has played */
VOID
DsVoice::CommitMatrix(
    _In_reads_(DS_MAX_CHANNELS * DS_MAX_CHANNELS) const float* Built)
{
    Device->Lock();
    memcpy(Matrix, Built, sizeof(Matrix));
    if (!MatrixReady)
    {
        memcpy(Previous, Built, sizeof(Previous));
        memcpy(Mixed, Built, sizeof(Mixed));
        RampLeft = 0;
    }
    else
    {
        memcpy(Previous, Mixed, sizeof(Previous));
        RampTotal = Device->Rate * DS_RAMP_MS / 1000;
        RampLeft = RampTotal;
    }
    Device->Unlock();
}

/* One overall volume and one level per output speaker, as the 3D panner produces */
VOID
DsVoice::SetSpeakerVolumes(
    _In_ LONG Volume,
    _In_ UINT Count,
    _In_reads_(Count) const LONG* Levels)
{
    float Built[DS_MAX_CHANNELS * DS_MAX_CHANNELS] = {};
    float Overall = static_cast<float>(DsDecibelsToAmplitude(Volume)) * DsAmplitudeScale;

    for (UINT Out = 0; Out < Count && Out < Device->Channels; Out++)
    {
        float Gain = static_cast<float>(DsDecibelsToAmplitude(Levels[Out])) * DsAmplitudeScale * Overall;

        if (Channels == 1)
        {
            Built[Out] = Gain;
        }
        else
        {
            Built[Out * 2] = Gain;
            Built[Out * 2 + 1] = Gain;
        }
    }
    CommitMatrix(Built);
}

VOID
DsVoice::SetMute(
    _In_ BOOLEAN Mute)
{
    Device->Lock();
    Muted = Mute;
    Device->Unlock();
}

/* A buffer whose application is out of focus keeps its cursors moving in silence */
VOID
DsVoice::SetFocusMute(
    _In_ BOOLEAN Mute)
{
    Device->Lock();
    FocusMuted = Mute;
    Device->Unlock();
}

HRESULT
DsVoice::SetNotifications(
    _In_ ULONG Count,
    _In_reads_opt_(Count) const DsNotifyPoint* NewPoints)
{
    DsNotifyPoint* Copy = nullptr;
    DsNotifyPoint* Old;

    if (Count != 0)
    {
        Copy = static_cast<DsNotifyPoint*>(HeapAlloc(GetProcessHeap(), 0, Count * sizeof(*Copy)));
        if (Copy == nullptr)
            return E_OUTOFMEMORY;
        memcpy(Copy, NewPoints, Count * sizeof(*Copy));
    }

    Device->Lock();
    Old = Points;
    Points = Copy;
    PointCount = Count;
    NotifyPlay = 0xFFFFFFFF;
    NotifySeen = 0xFFFFFFFF;
    Device->Unlock();
    HeapFree(GetProcessHeap(), 0, Old);
    return S_OK;
}

VOID
DsVoice::PassStart(VOID)
{
    if (PointCount != 0 && ReadPosition != NotifySeen)
        NotifyPlay = ReadPosition;
}

/* Fires the events the play cursor passed during this mix pass */
VOID
DsVoice::PassEnd(VOID)
{
    if (PointCount != 0)
    {
        ULONG NewPlay = 0;

        if (!Finished)
        {
            NotifySeen = ReadPosition;
            NewPlay = PlayFromRead(ReadPosition, NotifyPlay);
        }

        if (NewPlay != NotifyPlay)
        {
            for (ULONG Index = 0; Index < PointCount; Index++)
            {
                DWORD Offset = Points[Index].Offset;
                BOOLEAN Passed;

                if (Offset == DSBPN_OFFSETSTOP)
                    Passed = Finished;
                else if (NotifyPlay > NewPlay)
                    Passed = !(Offset < NotifyPlay && Offset >= NewPlay);
                else
                    Passed = Offset >= NotifyPlay && Offset < NewPlay;

                if (Passed)
                    SetEvent(Points[Index].Event);
            }
            NotifyPlay = NewPlay;
        }
    }

    if (!Finished)
        Pass = DS_PASS_SEEN;
    Finished = FALSE;
}

static
float
DsSample(
    _In_ const BYTE* Data,
    _In_ WORD Tag,
    _In_ WORD Bits,
    _In_ WORD ValidBits)
{
    if (Tag == WAVE_FORMAT_IEEE_FLOAT)
        return *reinterpret_cast<const float*>(Data);

    switch (Bits)
    {
        case 8:
            return (static_cast<float>(Data[0]) - 128.0f) * 0.0078125f;
        case 16:
            return static_cast<float>(*reinterpret_cast<const SHORT*>(Data)) * (1.0f / 32768.0f);
        case 24:
        {
            LONG Value = static_cast<LONG>(Data[0] | (Data[1] << 8) | (Data[2] << 16)) << 8 >> 8;

            return static_cast<float>(Value) * (1.0f / 8388608.0f);
        }
        case 32:
            if (ValidBits == 24)
                return static_cast<float>(*reinterpret_cast<const LONG*>(Data) >> 8) * (1.0f / 8388608.0f);
            return static_cast<float>(*reinterpret_cast<const LONG*>(Data)) * (1.0f / 2147483648.0f);
    }
    return 0.0f;
}

/* Converts the frame at the read position and moves past it; silence once a one shot runs out */
BOOLEAN
DsVoice::ReadFrame(
    _Out_writes_(Channels) float* Frame)
{
    WORD Tag = DsFormatTag(Format);
    WORD Bits = Format->wBitsPerSample;
    WORD ValidBits = Bits;
    UINT Width = Bits / 8;
    const BYTE* Source;

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        ValidBits = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample;

    if (ReadPosition + BlockAlign > Size)
    {
        if (!(State & DS_VOICE_LOOPING))
        {
            for (UINT Channel = 0; Channel < Channels; Channel++)
                Frame[Channel] = 0.0f;
            return FALSE;
        }
        ReadPosition = 0;
    }

    Source = Data + ReadPosition;
    for (UINT Channel = 0; Channel < Channels; Channel++)
        Frame[Channel] = DsSample(Source + Channel * Width, Tag, Bits, ValidBits);

    ReadPosition += BlockAlign;
    if (ReadPosition >= Size)
    {
        if (State & DS_VOICE_LOOPING)
        {
            ReadPosition = 0;
        }
        else
        {
            Finished = TRUE;
            Pass = DS_PASS_FINISHED;
        }
    }
    return TRUE;
}

VOID
DsVoice::ApplyMatrix(
    _In_reads_(Channels) const float* Frame,
    _Inout_updates_(OutChannels) float* Output,
    _In_ UINT OutChannels)
{
    const float* Use = Matrix;

    if (RampLeft != 0)
    {
        float Step = static_cast<float>(RampTotal - RampLeft + 1) / static_cast<float>(RampTotal);

        for (UINT Index = 0; Index < OutChannels * Channels; Index++)
            Mixed[Index] = Previous[Index] + (Matrix[Index] - Previous[Index]) * Step;
        RampLeft -= 1;
        Use = Mixed;
    }
    else
    {
        memcpy(Mixed, Matrix, OutChannels * Channels * sizeof(float));
    }

    if (Muted || FocusMuted)
        return;
    for (UINT Out = 0; Out < OutChannels; Out++)
    {
        float Sum = 0.0f;

        for (UINT In = 0; In < Channels; In++)
            Sum += Use[Out * Channels + In] * Frame[In];
        Output[Out] += Sum;
    }
}

/* Mixes one engine pass of this voice into the final mix */
VOID
DsVoice::Mix(
    _Inout_updates_(Frames * OutChannels) float* Output,
    _In_ ULONG Frames,
    _In_ UINT OutChannels)
{
    float Frame[DS_MAX_CHANNELS];

    if (!(State & DS_VOICE_PLAYING) || Pass == DS_PASS_FINISHED)
        return;
    MatrixReady = TRUE;
    if (Effects != nullptr)
    {
        ULONG Ahead = Resampler.Active ? Resampler.InputFor(Frames) : Frames;

        DsRunEffects(Effects, this, Ahead * BlockAlign);
    }

    if (Resampler.Active)
    {
        for (ULONG Index = 0; Index < Frames; Index++)
        {
            while (Resampler.WantsInput() && Pass != DS_PASS_FINISHED)
            {
                float Fresh[DS_MAX_CHANNELS];

                ReadFrame(Fresh);
                Resampler.Push(Fresh);
            }
            if (Pass == DS_PASS_FINISHED)
                break;
            Resampler.Pull(Frame);
            ApplyMatrix(Frame, Output + Index * OutChannels, OutChannels);
        }
        return;
    }

    /* Without a converter, from a failed allocation, the voice stays quiet */
    if (Ratio != 1.0f)
        return;
    for (ULONG Index = 0; Index < Frames && Pass != DS_PASS_FINISHED; Index++)
    {
        ReadFrame(Frame);
        ApplyMatrix(Frame, Output + Index * OutChannels, OutChannels);
    }
}
