/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source voices: buffer queues, play state and pitch
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

XaSourceVoice::XaSourceVoice(
    _In_ XaEngine* NewEngine,
    _In_opt_ IXAudio2VoiceCallback* NewCallback) : XaVoice(NewEngine, XaKindSource)
{
    Callback = NewCallback;
    Format = nullptr;
    FormatTag = 0;
    BlockAlign = 0;
    FramesPerBlock = 1;
    MaxFrequencyRatio = 1.0f;
    PlayState = XaPlayStopped;
    DeferredList = nullptr;
    Queue = nullptr;
    QueueTail = nullptr;
    QueuedCount = 0;
    memset(&Ratio, 0, sizeof(Ratio));
    ExitLoopPending = FALSE;
    ExitLoopSet = XA_OPSET_NOW;
    FlushPending = FALSE;
    PlayedFrames = 0;
    Decoded = nullptr;
    RawFrames = nullptr;
    RawCapacity = 0;
    DecodedData = nullptr;
    DecodedBlock = 0;
    HasConverter = FALSE;
    Passthrough = FALSE;
    CountedResamplers = 0;
    CountedMatrices = 0;
    Wma = nullptr;
}

XaSourceVoice::~XaSourceVoice()
{
    while (Queue != nullptr)
    {
        XaQueuedBuffer* Buffer = Queue;

        Queue = Buffer->Next;
        delete Buffer;
    }
    while (DeferredList != nullptr)
    {
        XaDeferred* Deferred = DeferredList;

        DeferredList = Deferred->Next;
        delete Deferred;
    }
    XaWmaRelease(this);
    XaParameterFree(&Ratio);
    delete[] reinterpret_cast<BYTE*>(Format);
    delete[] Decoded;
    delete[] RawFrames;
}

HRESULT
XaSourceVoice::Initialize(
    _In_ UINT32 Flags,
    _In_ const WAVEFORMATEX* NewFormat,
    _In_ float MaxRatio,
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    XAUDIO2_SEND_DESCRIPTOR Default;
    XAUDIO2_VOICE_SENDS DefaultList;
    UINT32 Target;
    UINT32 FormatSize;
    float InitialRatio;
    WORD SourceTag = XaFormatTag(NewFormat);
    const WAVEFORMATEX* Compressed = nullptr;
    HRESULT hr;

    if (NewFormat->nChannels == 0 || NewFormat->nChannels > XA_MAX_CHANNELS)
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: %u channels", NewFormat->nChannels);
        return XAUDIO2_E_INVALID_CALL;
    }
    if (NewFormat->nSamplesPerSec < XA_MIN_RATE || NewFormat->nSamplesPerSec > XA_MAX_RATE)
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: %u Hz", NewFormat->nSamplesPerSec);
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!(MaxRatio >= XA_MIN_RATIO && MaxRatio <= XA_MAX_RATIO))
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: maximum frequency ratio out of range");
        return XAUDIO2_E_INVALID_CALL;
    }
    if (SendList == nullptr)
    {
        Default.Flags = 0;
        Default.pOutputVoice = Engine->Master != nullptr ? Engine->Master->Face() : nullptr;
        DefaultList.SendCount = 1;
        DefaultList.pSends = &Default;
        SendList = &DefaultList;
    }
    if (!ValidateSends(SendList))
        return XAUDIO2_E_INVALID_CALL;

    /* No converter means no pitch control either */
    if ((Flags & XAUDIO2_VOICE_NOSRC) != 0)
        Flags |= XAUDIO2_VOICE_NOPITCH;
    MaxFrequencyRatio = (Flags & XAUDIO2_VOICE_NOPITCH) != 0 ? 1.0f : MaxRatio;

    Target = SendList->SendCount != 0 ? TargetRate(SendList) : XaQuantumRate(NewFormat->nSamplesPerSec, Engine->QuantumDenominator);
    if ((Flags & XAUDIO2_VOICE_NOSRC) != 0)
    {
        if (NewFormat->nSamplesPerSec % Engine->QuantumDenominator != 0)
        {
            XA_TRACE_ONCE(L"CreateSourceVoice: NOSRC needs a rate divisible by %u", Engine->QuantumDenominator);
            return XAUDIO2_E_INVALID_CALL;
        }
        if (NewFormat->nSamplesPerSec != Target)
        {
            XA_TRACE_ONCE(L"CreateSourceVoice: NOSRC voice at %u Hz cannot send to %u Hz", NewFormat->nSamplesPerSec, Target);
            return XAUDIO2_E_INVALID_CALL;
        }
    }

    FormatSize = NewFormat->wFormatTag == WAVE_FORMAT_PCM ? sizeof(PCMWAVEFORMAT) : sizeof(WAVEFORMATEX) + NewFormat->cbSize;
    Format = reinterpret_cast<WAVEFORMATEX*>(new (std::nothrow) BYTE[XaMax<UINT32>(FormatSize, sizeof(WAVEFORMATEX))]);
    if (Format == nullptr)
        return E_OUTOFMEMORY;
    memset(Format, 0, sizeof(WAVEFORMATEX));
    memcpy(Format, NewFormat, FormatSize);

    /* xWMA plays as the 16 bit PCM its decoder puts out */
    if (SourceTag == XA_TAG_WMA2 || SourceTag == XA_TAG_WMA3)
    {
        Compressed = NewFormat;
        if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        {
            WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format);

            Extensible->Samples.wValidBitsPerSample = 16;
            Extensible->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        }
        else
        {
            Format->wFormatTag = WAVE_FORMAT_PCM;
        }
        Format->wBitsPerSample = 16;
        Format->nBlockAlign = static_cast<WORD>(sizeof(SHORT) * (Format->nChannels & 0x7FFF));
        Format->nAvgBytesPerSec = Format->nSamplesPerSec * Format->nBlockAlign;
        NewFormat = Format;
    }
    FormatTag = XaFormatTag(NewFormat);
    BlockAlign = NewFormat->nBlockAlign;
    FramesPerBlock = FormatTag == WAVE_FORMAT_ADPCM ? XaAdpcmFramesPerBlock(NewFormat) : 1;
    InputChannels = NewFormat->nChannels;
    InputRate = NewFormat->nSamplesPerSec;
    InputMask = XaFormatMask(NewFormat);
    OutputRate = Target;
    HasConverter = (Flags & XAUDIO2_VOICE_NOPITCH) == 0 || InputRate != Target;
    Passthrough = !HasConverter && FormatTag != WAVE_FORMAT_ADPCM;

    InitialRatio = XaMin(XaMax(1.0f, XA_MIN_RATIO), MaxFrequencyRatio);
    if (!XaParameterInit(&Ratio, sizeof(InitialRatio), &InitialRatio))
        return E_OUTOFMEMORY;
    hr = InitializeCommon(Flags, SendList, Chain);
    if (SUCCEEDED(hr) && Compressed != nullptr)
        hr = XaWmaInitialize(this, Compressed);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::SetVolume(
    float NewVolume,
    UINT32 OperationSet)
{
    return SetVolumeCommon(NewVolume, OperationSet);
}

/* Moves the voice to a new play state and keeps the engine counters in step */
VOID
XaSourceVoice::Transition(
    _In_ XaPlayState NewState)
{
    UINT32 Resamplers = NewState == XaPlayRunning && HasConverter ? 1 : 0;
    UINT32 Matrices = 0;
    UINT32 Active = Engine->ActiveSources;

    if (NewState == XaPlayRunning)
        Matrices = SendCount + (Passthrough ? 1 : 0);
    else if (NewState == XaPlayTails)
        Matrices = SendCount;
    if ((PlayState != XaPlayStopped) != (NewState != XaPlayStopped))
    {
        if (NewState != XaPlayStopped)
            Engine->ActiveSources += 1;
        else
            Engine->ActiveSources -= 1;
        Engine->NoteActiveChange(Active);
    }
    Engine->ActiveResamplers = Engine->ActiveResamplers - CountedResamplers + Resamplers;
    Engine->ActiveMatrices = Engine->ActiveMatrices - CountedMatrices + Matrices;
    CountedResamplers = Resamplers;
    CountedMatrices = Matrices;
    PlayState = NewState;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::Start(
    UINT32 Flags,
    UINT32 OperationSet)
{
    HRESULT hr = S_OK;

    Engine->Lock();
    if (Flags != 0 || OperationSet == XAUDIO2_INVALID_OPSET)
    {
        XA_TRACE_ONCE(L"Start: flags %08X operation set %08X", Flags, OperationSet);
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (OperationSet == XA_OPSET_NOW)
    {
        if (PlayState != XaPlayRunning)
            Transition(XaPlayRunning);
    }
    else
    {
        hr = AddDeferred(OperationSet, XaDeferStart);
    }
    Engine->Unlock();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::Stop(
    UINT32 Flags,
    UINT32 OperationSet)
{
    BOOLEAN Tails = (Flags & XAUDIO2_PLAY_TAILS) != 0;
    HRESULT hr = S_OK;

    Engine->Lock();
    if ((Flags & ~XAUDIO2_PLAY_TAILS) != 0 || OperationSet == XAUDIO2_INVALID_OPSET)
    {
        XA_TRACE_ONCE(L"Stop: flags %08X operation set %08X", Flags, OperationSet);
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (OperationSet == XA_OPSET_NOW)
    {
        if (PlayState == XaPlayRunning || (PlayState == XaPlayTails && !Tails))
            Transition(Tails ? XaPlayTails : XaPlayStopped);
    }
    else
    {
        hr = AddDeferred(OperationSet, Tails ? XaDeferStopTails : XaDeferStop);
    }
    Engine->Unlock();
    return hr;
}

/* Queues a start or stop for CommitChanges; a set that already holds one keeps it */
HRESULT
XaSourceVoice::AddDeferred(
    _In_ UINT32 OperationSet,
    _In_ XaDeferredKind DeferredKind)
{
    XaDeferred** Link = &DeferredList;
    XaDeferred* Entry;

    for (; *Link != nullptr; Link = &(*Link)->Next)
    {
        if ((*Link)->OperationSet == OperationSet)
            return S_OK;
    }
    Entry = new (std::nothrow) XaDeferred;
    if (Entry == nullptr)
        return E_OUTOFMEMORY;
    Entry->Next = nullptr;
    Entry->OperationSet = OperationSet;
    Entry->Kind = DeferredKind;
    *Link = Entry;
    return S_OK;
}

VOID
XaSourceVoice::CommitDeferred(
    _In_ UINT32 OperationSet)
{
    XaDeferred* Chosen = nullptr;
    XaDeferred** Link = &DeferredList;

    if (DeferredList == nullptr)
        return;
    if (OperationSet == XAUDIO2_COMMIT_ALL)
    {
        /* Committing everything leaves only the latest change standing */
        while (DeferredList != nullptr)
        {
            XaDeferred* Entry = DeferredList;

            DeferredList = Entry->Next;
            if (Chosen != nullptr)
                delete Chosen;
            Chosen = Entry;
        }
    }
    else
    {
        for (; *Link != nullptr; Link = &(*Link)->Next)
        {
            if ((*Link)->OperationSet == OperationSet)
            {
                Chosen = *Link;
                *Link = Chosen->Next;
                break;
            }
        }
    }
    if (Chosen == nullptr)
        return;
    if (Chosen->Kind == XaDeferStart)
        Transition(XaPlayRunning);
    else
        Transition(Chosen->Kind == XaDeferStopTails ? XaPlayTails : XaPlayStopped);
    delete Chosen;
}

BOOLEAN
XaSourceVoice::ValidateBuffer(
    _In_ const XAUDIO2_BUFFER* Buffer)
{
    BOOLEAN Valid = TRUE;
    UINT32 Frames;

    if ((Buffer->Flags & ~XAUDIO2_END_OF_STREAM) != 0)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: flags %08X", Buffer->Flags);
        Valid = FALSE;
    }
    if (Buffer->pAudioData == nullptr || Buffer->AudioBytes == 0 || Buffer->AudioBytes > XA_MAX_BUFFER_BYTES)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: no audio, or too much");
        Valid = FALSE;
    }
    if (Buffer->LoopCount == 0)
    {
        if (Buffer->LoopBegin != 0 || Buffer->LoopLength != 0)
        {
            XA_TRACE_ONCE(L"SubmitSourceBuffer: a loop region without a loop count");
            return FALSE;
        }
    }
    else
    {
        if (Buffer->LoopCount > XA_MAX_LOOP_COUNT)
        {
            XA_TRACE_ONCE(L"SubmitSourceBuffer: loop count %u", Buffer->LoopCount);
            Valid = FALSE;
        }
        if (Buffer->PlayLength != 0 && Buffer->LoopBegin >= Buffer->PlayBegin + Buffer->PlayLength)
            Valid = FALSE;
        if (Buffer->LoopLength != 0)
        {
            if (Buffer->LoopBegin + Buffer->LoopLength <= Buffer->PlayBegin)
                Valid = FALSE;
            else if (Buffer->PlayLength != 0 &&
                     Buffer->LoopBegin + Buffer->LoopLength > Buffer->PlayBegin + Buffer->PlayLength)
                return FALSE;
        }
        if (!Valid)
            XA_TRACE_ONCE(L"SubmitSourceBuffer: the loop region does not fit the play region");
    }

    if (Buffer->AudioBytes % BlockAlign != 0)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: %u bytes is not whole blocks of %u", Buffer->AudioBytes, BlockAlign);
        return FALSE;
    }
    Frames = Buffer->AudioBytes / BlockAlign * FramesPerBlock;
    if (FramesPerBlock != 1 &&
        (Buffer->PlayBegin % FramesPerBlock != 0 || Buffer->PlayLength % FramesPerBlock != 0 ||
         Buffer->LoopBegin % FramesPerBlock != 0 || Buffer->LoopLength % FramesPerBlock != 0))
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: regions must be whole ADPCM blocks");
        Valid = FALSE;
    }
    if (Buffer->PlayBegin >= Frames || Buffer->PlayLength > Frames - Buffer->PlayBegin)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: play region outside the buffer");
        Valid = FALSE;
    }
    if (Buffer->LoopCount != 0)
    {
        if (Buffer->LoopBegin >= Frames)
            Valid = FALSE;
        if (Buffer->LoopLength > Frames - Buffer->LoopBegin)
            return FALSE;
    }
    return Valid;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::SubmitSourceBuffer(
    _In_ const XAUDIO2_BUFFER* Buffer,
    _In_opt_ const XAUDIO2_BUFFER_WMA* BufferWma)
{
    XaQueuedBuffer* Entry;
    UINT32 Frames;
    HRESULT hr;

    Engine->Lock();
    if (Wma != nullptr)
    {
        hr = XaWmaSubmit(this, Buffer, BufferWma);
        Engine->Unlock();
        return hr;
    }
    if (Buffer == nullptr || BufferWma != nullptr || QueuedCount >= XA_MAX_QUEUED)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: no buffer, WMA data, or the queue is full");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!ValidateBuffer(Buffer))
    {
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    Entry = new (std::nothrow) XaQueuedBuffer;
    if (Entry == nullptr)
    {
        Engine->Unlock();
        return E_OUTOFMEMORY;
    }
    memset(Entry, 0, sizeof(*Entry));
    Frames = Buffer->AudioBytes / BlockAlign * FramesPerBlock;
    Entry->Data = Buffer->pAudioData;
    Entry->Bytes = Buffer->AudioBytes;
    Entry->Flags = Buffer->Flags;
    Entry->PlayBegin = Buffer->PlayBegin;
    Entry->PlayEnd = Buffer->PlayLength != 0 ? Buffer->PlayBegin + Buffer->PlayLength : Frames;
    Entry->LoopCount = Buffer->LoopCount;
    if (Buffer->LoopCount != 0)
    {
        Entry->LoopBegin = Buffer->LoopBegin;
        Entry->LoopEnd = Buffer->LoopLength != 0 ? Buffer->LoopBegin + Buffer->LoopLength : Entry->PlayEnd;
    }
    Entry->Context = Buffer->pContext;
    Entry->Position = Entry->PlayBegin;
    if (QueueTail != nullptr)
        QueueTail->Next = Entry;
    else
        Queue = Entry;
    QueueTail = Entry;
    QueuedCount += 1;
    Engine->Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::FlushSourceBuffers()
{
    Engine->Lock();

    /* A stopped xWMA voice drops its buffers at once; only the callbacks wait for the engine */
    if (Wma != nullptr && PlayState != XaPlayRunning)
        XaWmaFlushStopped(this);
    else
        FlushPending = TRUE;
    Engine->Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::Discontinuity()
{
    Engine->Lock();
    if (Wma != nullptr)
        XaWmaDiscontinuity(this);
    else if (QueueTail != nullptr)
        QueueTail->Flags |= XAUDIO2_END_OF_STREAM;
    Engine->Unlock();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::ExitLoop(
    UINT32 OperationSet)
{
    Engine->Lock();
    if (OperationSet == XAUDIO2_INVALID_OPSET)
    {
        XA_TRACE_ONCE(L"ExitLoop: invalid operation set");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    ExitLoopPending = TRUE;
    ExitLoopSet = OperationSet;
    Engine->Unlock();
    return S_OK;
}

void
STDMETHODCALLTYPE
XaSourceVoice::GetState(
    _Out_ XAUDIO2_VOICE_STATE* State,
    UINT32 Flags)
{
    Engine->Lock();
    if (State == nullptr || (Flags & ~XAUDIO2_VOICE_NOSAMPLESPLAYED) != 0)
    {
        XA_TRACE_ONCE(L"GetState: no state or flags %08X", Flags);
        Engine->Unlock();
        return;
    }
    if (Wma != nullptr)
    {
        XaWmaGetState(this, State, (Flags & XAUDIO2_VOICE_NOSAMPLESPLAYED) == 0);
        Engine->Unlock();
        return;
    }
    State->pCurrentBufferContext = Queue != nullptr ? Queue->Context : nullptr;
    State->BuffersQueued = QueuedCount;
    if ((Flags & XAUDIO2_VOICE_NOSAMPLESPLAYED) == 0)
        State->SamplesPlayed = PlayedFrames;
    Engine->Unlock();
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::SetFrequencyRatio(
    float NewRatio,
    UINT32 OperationSet)
{
    Engine->Lock();
    if (OperationSet == XAUDIO2_INVALID_OPSET || (CreationFlags & XAUDIO2_VOICE_NOPITCH) != 0)
    {
        XA_TRACE_ONCE(L"SetFrequencyRatio: invalid operation set or no pitch control");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!(NewRatio >= XA_MIN_RATIO))
        NewRatio = XA_MIN_RATIO;
    else if (NewRatio > MaxFrequencyRatio)
        NewRatio = MaxFrequencyRatio;
    XaParameterSet(&Ratio, &NewRatio, OperationSet);
    Engine->Unlock();
    return S_OK;
}

void
STDMETHODCALLTYPE
XaSourceVoice::GetFrequencyRatio(
    _Out_ float* Result)
{
    BOOLEAN Locked = !Engine->InCallback();

    if (Locked)
        Engine->Lock();
    if (Result == nullptr || (CreationFlags & XAUDIO2_VOICE_NOPITCH) != 0)
        XA_TRACE_ONCE(L"GetFrequencyRatio: no ratio to read");
    else
        memcpy(Result, Ratio.Current, sizeof(*Result));
    if (Locked)
        Engine->Unlock();
}

HRESULT
STDMETHODCALLTYPE
XaSourceVoice::SetSourceSampleRate(
    UINT32 NewRate)
{
    Engine->Lock();
    if ((CreationFlags & XAUDIO2_VOICE_NOPITCH) != 0 || NewRate < XA_MIN_RATE || NewRate > XA_MAX_RATE || QueuedCount != 0)
    {
        XA_TRACE_ONCE(L"SetSourceSampleRate: %u Hz refused", NewRate);
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (NewRate != InputRate)
    {
        InputRate = NewRate;
        XaResamplerFree(Resampler);
        Resampler = nullptr;
    }
    Engine->Unlock();
    return S_OK;
}

VOID
XaSourceVoice::Teardown()
{
    Engine->Lock();
    if (PlayState != XaPlayStopped)
        Transition(XaPlayStopped);
    Engine->SourceCount -= 1;
    Engine->Unlock();
}
