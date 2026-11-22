/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The mixer side of a buffer: cursors, notifications, conversion, rate change and channel mixing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <math.h>

/* Blackman window over the resampler taps, sampled finely enough for nearest lookup */
#define DS_WINDOW_STEPS 4096

static float DsWindow[DS_WINDOW_STEPS + 1];
static INIT_ONCE DsWindowOnce = INIT_ONCE_STATIC_INIT;

static
BOOL
CALLBACK
DsBuildWindow(
    _Inout_ PINIT_ONCE Once,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_ PVOID* Context)
{
    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    for (int Step = 0; Step <= DS_WINDOW_STEPS; Step++)
    {
        double Place = static_cast<double>(Step) / DS_WINDOW_STEPS;

        DsWindow[Step] = static_cast<float>(0.42 + 0.5 * cos(3.14159265358979 * Place) + 0.08 * cos(2 * 3.14159265358979 * Place));
    }
    return TRUE;
}

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

    InitOnceExecuteOnce(&DsWindowOnce, DsBuildWindow, nullptr, nullptr);

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
        HistoryFill = 0;
        Phase = 0.0;
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
        HistoryFill = 0;
        Phase = 0.0;
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
    Device->Lock();
    Format->nSamplesPerSec = Frequency;
    Format->nAvgBytesPerSec = Frequency * Format->nBlockAlign;
    Ratio = static_cast<float>(Frequency) / static_cast<float>(Device->Rate);
    Device->Unlock();
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

/* New coefficients fade in over a few milliseconds from whatever was last in effect */
VOID
DsVoice::CommitMatrix(
    _In_reads_(DS_MAX_CHANNELS * DS_MAX_CHANNELS) const float* Built)
{
    Device->Lock();
    memcpy(Matrix, Built, sizeof(Matrix));
    if (!MatrixReady)
    {
        memcpy(Previous, Built, sizeof(Previous));
        MatrixReady = TRUE;
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

    if (Muted)
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
    const UINT Taps = DS_SINC_HALF * 2;
    float Frame[DS_MAX_CHANNELS];

    if (!(State & DS_VOICE_PLAYING) || Pass == DS_PASS_FINISHED)
        return;
    if (Effects != nullptr)
        DsRunEffects(Effects, this, (static_cast<ULONG>(Frames * Ratio) + Taps + 2) * BlockAlign);

    if (Ratio == 1.0f)
    {
        for (ULONG Index = 0; Index < Frames && Pass != DS_PASS_FINISHED; Index++)
        {
            ReadFrame(Frame);
            ApplyMatrix(Frame, Output + Index * OutChannels, OutChannels);
        }
        return;
    }

    {
        double Step = Ratio;
        double Cutoff = Step > 1.0 ? 0.97 / Step : 0.97;

        /* A restart begins from a silent window */
        if (HistoryFill == 0)
        {
            ZeroMemory(History, sizeof(History));
            HistoryFill = 1;
        }

        for (ULONG Index = 0; Index < Frames && Pass != DS_PASS_FINISHED; Index++)
        {
            double Base = -(DS_SINC_HALF - 1) - Phase;
            double Angle = 3.14159265358979 * Cutoff * Base;
            double Turn = 3.14159265358979 * Cutoff;
            double SinValue = sin(Angle);
            double CosValue = cos(Angle);
            double SinStep = sin(Turn);
            double CosStep = cos(Turn);

            for (UINT Channel = 0; Channel < Channels; Channel++)
                Frame[Channel] = 0.0f;

            /* Windowed sinc taps around the fractional read point between taps 15 and 16 */
            for (UINT Tap = 0; Tap < Taps; Tap++)
            {
                double Distance = Base + Tap;
                double Weight;
                double Rotated;
                UINT Slot = static_cast<UINT>(fabs(Distance) / DS_SINC_HALF * DS_WINDOW_STEPS + 0.5);

                if (fabs(Distance) < 1e-9)
                    Weight = Cutoff;
                else
                    Weight = SinValue / (3.14159265358979 * Distance);
                if (Slot > DS_WINDOW_STEPS)
                    Slot = DS_WINDOW_STEPS;
                Weight *= DsWindow[Slot];

                for (UINT Channel = 0; Channel < Channels; Channel++)
                    Frame[Channel] += static_cast<float>(Weight) * History[Tap * Channels + Channel];

                Rotated = SinValue * CosStep + CosValue * SinStep;
                CosValue = CosValue * CosStep - SinValue * SinStep;
                SinValue = Rotated;
            }

            ApplyMatrix(Frame, Output + Index * OutChannels, OutChannels);

            Phase += Step;
            while (Phase >= 1.0)
            {
                float Fresh[DS_MAX_CHANNELS];

                Phase -= 1.0;
                ReadFrame(Fresh);
                memmove(History, &History[Channels], (Taps - 1) * Channels * sizeof(float));
                memcpy(&History[(Taps - 1) * Channels], Fresh, Channels * sizeof(float));
                if (Pass == DS_PASS_FINISHED)
                    break;
            }
        }
    }
}
