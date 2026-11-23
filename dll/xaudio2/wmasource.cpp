/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA source voices: packet queues feeding the decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wma.hpp"

/* Play length that never runs out, used while a buffer loops */
#define XA_WMA_UNBOUNDED        0xFFFFFFFFu

/* Regions waiting for their decoded output, one per buffer the decoder took up */
#define XA_WMA_REGIONS          256

/* A submitted xWMA buffer, with its play region already in decoded bytes */
struct XaWmaBuffer
{
    XaWmaBuffer* Next;
    UINT32 Flags;
    UINT32 Bytes;
    const BYTE* Data;
    UINT32 BeginBytes;
    UINT32 LengthBytes;
    UINT32 LoopCount;
    void* Context;
    const UINT32* Cumulative;
    UINT32 PacketCount;
};

/* How much decoded output to drop and then to keep for one buffer */
struct XaWmaRegion
{
    UINT32 Skip;
    UINT32 Length;
};

struct XaWmaSource
{
    XaWmaDecoder* Decoder;
    XaWmaState State;
    UINT32 ReadyFrames;
    UINT32 FrameBytes;
    UINT32 PacketBytes;

    /* Buffers the decoder still reads from, then buffers it has read in full */
    XaWmaBuffer* Pending;
    XaWmaBuffer* PendingTail;
    XaWmaBuffer* Taken;
    XaWmaBuffer* TakenTail;

    /* Buffers a flush of the stopped voice dropped, waiting for their callbacks */
    XaWmaBuffer* Dropped;

    /* What is left of the buffer being handed to the decoder */
    const BYTE* Input;
    UINT32 InputLeft;
    BOOLEAN InputLast;
    BOOLEAN Looping;
    BOOLEAN Started;

    XaWmaRegion Regions[XA_WMA_REGIONS];
    UINT32 RegionFirst;
    UINT32 RegionCount;
    UINT32 Skip;
    UINT32 Length;

    /* Decoded frames waiting for the mixer */
    SHORT* Pcm;
    UINT32 PcmCapacity;
    UINT32 PcmStart;
    UINT32 PcmCount;

    /* Decoded bytes passed on since the last stream end; output joins it a batch at a time */
    UINT64 DeliveredBytes;

    /* The ring decoded output goes through: where the next batch lands, and where it wraps */
    UINT32 RingOffset;
    UINT32 RingLimit;
};

static
VOID
XaWmaPushRegion(
    _Inout_ XaWmaSource* Wma,
    _In_ UINT32 Skip,
    _In_ UINT32 Length)
{
    XaWmaRegion* Region;

    if (Wma->RegionCount == XA_WMA_REGIONS)
        return;
    Region = &Wma->Regions[(Wma->RegionFirst + Wma->RegionCount) % XA_WMA_REGIONS];
    Region->Skip = Skip;
    Region->Length = Length;
    Wma->RegionCount += 1;
}

static
VOID
XaWmaPopRegion(
    _Inout_ XaWmaSource* Wma,
    _Out_ XaWmaRegion* Region)
{
    if (Wma->RegionCount == 0)
    {
        Region->Skip = 0;
        Region->Length = 0;
        return;
    }
    *Region = Wma->Regions[Wma->RegionFirst];
    Wma->RegionFirst = (Wma->RegionFirst + 1) % XA_WMA_REGIONS;
    Wma->RegionCount -= 1;
}

/* Reports the oldest buffer the decoder finished reading as done */
static
VOID
XaWmaCompleteTaken(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer* Buffer = Wma->Taken;
    void* Context;
    UINT32 Flags;

    if (Buffer == nullptr)
        return;
    Wma->Taken = Buffer->Next;
    if (Wma->Taken == nullptr)
        Wma->TakenTail = nullptr;
    Context = Buffer->Context;
    Flags = Buffer->Flags;
    delete Buffer;
    Voice->QueuedCount -= 1;
    if (Flags == XAUDIO2_END_OF_STREAM)
        Wma->DeliveredBytes = 0;
    if (Voice->Callback != nullptr)
    {
        Voice->Callback->OnBufferEnd(Context);
        if ((Flags & XAUDIO2_END_OF_STREAM) != 0)
            Voice->Callback->OnStreamEnd();
    }
}

/* Hands the decoder the next packet of the buffer it reads from */
static
VOID
XaWmaPullInput(
    _In_ void* Context,
    _Out_ XaWmaInput* Input)
{
    XaSourceVoice* Voice = static_cast<XaSourceVoice*>(Context);
    XaWmaSource* Wma = Voice->Wma;

    XaWmaCompleteTaken(Voice);
    Input->Data = Wma->Input;
    Input->Bytes = 0;
    Input->Last = TRUE;
    if (Wma->Input == nullptr)
        return;

    if (Wma->InputLeft > Wma->PacketBytes)
    {
        Input->Bytes = Wma->PacketBytes;
        Input->Last = FALSE;
        Wma->Input += Wma->PacketBytes;
        Wma->InputLeft -= Wma->PacketBytes;
        return;
    }

    /* The rest of the buffer goes out, so it either loops or moves on */
    Input->Bytes = Wma->InputLeft;
    Input->Last = Wma->InputLast;
    if (Wma->Looping)
    {
        if (Wma->Pending->LoopCount != XAUDIO2_LOOP_INFINITE)
            Wma->Pending->LoopCount -= 1;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnLoopEnd(Wma->Pending->Context);
    }
    else
    {
        XaWmaBuffer* Buffer = Wma->Pending;

        Wma->Pending = Buffer->Next;
        if (Wma->Pending == nullptr)
            Wma->PendingTail = nullptr;
        Buffer->Next = nullptr;
        if (Wma->TakenTail != nullptr)
            Wma->TakenTail->Next = Buffer;
        else
            Wma->Taken = Buffer;
        Wma->TakenTail = Buffer;
        Wma->Started = FALSE;
    }
    Wma->Input = nullptr;
    Wma->InputLeft = 0;
    Wma->InputLast = FALSE;
}

/* Points the input at the head buffer, from the packet holding its first played frame */
static
VOID
XaWmaLoadHead(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer* Buffer = Wma->Pending;
    UINT32 Packet = 0;
    UINT32 Offset;
    UINT32 Skip;

    if (Buffer->BeginBytes != 0)
    {
        while (Buffer->BeginBytes >= Buffer->Cumulative[Packet])
            Packet += 1;
    }
    Offset = Wma->PacketBytes * Packet;
    Wma->Input = Buffer->Data + Offset;
    Wma->InputLeft = Buffer->Bytes - Offset;
    Skip = Buffer->BeginBytes;
    if (Packet != 0)
        Skip -= Buffer->Cumulative[Packet - 1];

    /* Later passes through a loop start from the top */
    Buffer->BeginBytes = 0;
    Wma->Looping = Buffer->LoopCount != 0;
    XaWmaPushRegion(Wma, Skip, Wma->Looping ? XA_WMA_UNBOUNDED : Buffer->LengthBytes);
    Wma->InputLast = Wma->Looping || Buffer->Flags == XAUDIO2_END_OF_STREAM;
    if (!Wma->Started)
    {
        if (Voice->Callback != nullptr)
            Voice->Callback->OnBufferStart(Buffer->Context);
        Wma->Started = TRUE;
    }
}

/* Makes room behind the queued frames for one more decode step */
static
SHORT*
XaWmaReserve(
    _Inout_ XaWmaSource* Wma,
    _In_ UINT32 Frames)
{
    UINT32 Channels = Wma->FrameBytes / sizeof(SHORT);

    if (Wma->PcmStart != 0)
    {
        memmove(Wma->Pcm, Wma->Pcm + static_cast<SIZE_T>(Wma->PcmStart) * Channels,
                sizeof(SHORT) * Wma->PcmCount * Channels);
        Wma->PcmStart = 0;
    }
    if (Wma->PcmCount + Frames > Wma->PcmCapacity)
    {
        UINT32 Capacity = Wma->PcmCount + Frames;
        SHORT* Grown = new (std::nothrow) SHORT[static_cast<SIZE_T>(Capacity) * Channels];

        if (Grown == nullptr)
            return nullptr;
        memcpy(Grown, Wma->Pcm, sizeof(SHORT) * Wma->PcmCount * Channels);
        delete[] Wma->Pcm;
        Wma->Pcm = Grown;
        Wma->PcmCapacity = Capacity;
    }
    return Wma->Pcm + static_cast<SIZE_T>(Wma->PcmCount) * Channels;
}

/* Keeps Bytes of the decoded output at Out, after trimming it to the regions in play */
static
UINT32
XaWmaTrim(
    _Inout_ XaWmaSource* Wma,
    _Inout_ SHORT* Out,
    _In_ UINT32 Bytes)
{
    UINT32 Length;

    if (Wma->Skip != 0)
    {
        if (Wma->Skip >= Bytes)
        {
            Wma->Skip -= Bytes;
            return 0;
        }
        memmove(Out, reinterpret_cast<BYTE*>(Out) + Wma->Skip, Bytes - Wma->Skip);
        Bytes -= Wma->Skip;
        Wma->Skip = 0;
    }
    if (Bytes == 0)
        return 0;

    Length = Wma->Length;
    if (Length > Bytes)
    {
        Wma->Length = Length - Bytes;
        return Bytes;
    }

    /* The region ran out; the regions of buffers the decoder has moved on to may still take the rest */
    if (Length < Bytes)
    {
        while (Wma->RegionCount != 0)
        {
            XaWmaRegion Region;

            XaWmaPopRegion(Wma, &Region);
            Wma->Skip = Region.Skip;
            Length = Wma->Length + Region.Length;
            Wma->Length = Length;
            if (Length > Bytes)
            {
                Wma->Length = Length - Bytes;
                return Bytes;
            }
        }
    }
    Wma->Length = 0;
    return Length;
}

/* Counts output toward SamplesPlayed once the batch reaches the end of the ring, or when Flush is set */
static
VOID
XaWmaPassOn(
    _Inout_ XaWmaSource* Wma,
    _Inout_ UINT32* Batch,
    _In_ BOOLEAN Flush)
{
    if (*Batch == 0)
        return;
    if (Flush)
    {
        Wma->DeliveredBytes += *Batch;
        Wma->RingOffset += *Batch;
        *Batch = 0;
    }
    else if (Wma->RingOffset + *Batch >= Wma->RingLimit)
    {
        Wma->DeliveredBytes += *Batch;
        Wma->RingOffset = 0;
        *Batch = 0;
    }
}

/* Decodes until Bytes more output is queued or the stream runs dry */
static
HRESULT
XaWmaFill(
    _Inout_ XaSourceVoice* Voice,
    _In_ INT32 Bytes)
{
    XaWmaSource* Wma = Voice->Wma;
    UINT32 Batch = 0;
    HRESULT hr = S_OK;

    while (Bytes > 0 && SUCCEEDED(hr))
    {
        if (Wma->Input == nullptr && Wma->Pending != nullptr)
            XaWmaLoadHead(Voice);

        if (Wma->State == XaWmaIdle)
        {
            XaWmaCompleteTaken(Voice);
            if (Wma->Input != nullptr)
            {
                XaWmaRegion Region;

                XaWmaReset(Wma->Decoder);
                Wma->State = XaWmaWantsDecode;
                XaWmaPopRegion(Wma, &Region);
                Wma->Skip = Region.Skip;
                Wma->Length = Region.Length;
            }
            else if (Wma->Pending == nullptr)
            {
                break;
            }
        }
        else if (Wma->State == XaWmaFailed)
        {
            hr = E_FAIL;
        }
        else if (Wma->State == XaWmaWantsDecode)
        {
            hr = XaWmaDecode(Wma->Decoder, &Wma->ReadyFrames, &Wma->State);
        }
        else
        {
            /* A read is sized in bytes by the decode step's frame count, so each takes a fraction of it */
            UINT32 Frames = XaMax(Wma->ReadyFrames / Wma->FrameBytes, 1u);
            SHORT* Out = XaWmaReserve(Wma, Frames);
            UINT32 Produced = 0;
            UINT32 Kept;

            if (Out == nullptr)
            {
                hr = E_OUTOFMEMORY;
                break;
            }
            hr = XaWmaReadPcm(Wma->Decoder, Frames, Out, &Produced, &Wma->State);
            if (FAILED(hr))
                break;
            Kept = XaWmaTrim(Wma, Out, Wma->FrameBytes * Produced);
            Wma->PcmCount += Kept / Wma->FrameBytes;
            Bytes -= static_cast<INT32>(Kept);
            Batch += Kept;
            XaWmaPassOn(Wma, &Batch, FALSE);
        }
    }
    XaWmaPassOn(Wma, &Batch, TRUE);
    return hr;
}

HRESULT
XaWmaInitialize(
    _Inout_ XaSourceVoice* Voice,
    _In_ const WAVEFORMATEX* Format,
    _In_ float MaxRatio,
    _In_ BOOLEAN Pitch)
{
    XaWmaSource* Wma = new (std::nothrow) XaWmaSource;
    float Stretch = (static_cast<float>(Format->nSamplesPerSec) / static_cast<float>(Voice->OutputRate)) * MaxRatio;
    double Reach = static_cast<double>(XaQuantumFrames(Voice->OutputRate, Voice->Engine)) * Stretch;
    UINT32 PassBytes;
    HRESULT hr;

    if (Wma == nullptr)
        return E_OUTOFMEMORY;
    memset(Wma, 0, sizeof(*Wma));
    Wma->FrameBytes = sizeof(SHORT) * Format->nChannels;

    /* The ring holds at least two of the largest passes the converter can ask for */
    PassBytes = Wma->FrameBytes * (Pitch ? static_cast<UINT32>(Reach + 1.0) + 1 : static_cast<UINT32>(Reach + 0.5));
    Wma->RingLimit = PassBytes * 2 * (2048 / XaMax(PassBytes, 1u) + 1);
    Voice->Wma = Wma;
    hr = XaWmaCreate(Format, XaWmaPullInput, Voice, &Wma->Decoder);
    if (FAILED(hr))
    {
        XA_TRACE_ONCE(L"CreateSourceVoice: the xWMA decoder refused the format (%08lX)", hr);
        return hr;
    }
    Wma->PacketBytes = XaWmaPacketBytes(Wma->Decoder);
    Wma->State = XaWmaIdle;
    return S_OK;
}

VOID
XaWmaRelease(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer* Lists[3];

    if (Wma == nullptr)
        return;
    Lists[0] = Wma->Pending;
    Lists[1] = Wma->Taken;
    Lists[2] = Wma->Dropped;
    for (UINT32 Index = 0; Index < ARRAYSIZE(Lists); Index++)
    {
        while (Lists[Index] != nullptr)
        {
            XaWmaBuffer* Buffer = Lists[Index];

            Lists[Index] = Buffer->Next;
            delete Buffer;
        }
    }
    XaWmaFree(Wma->Decoder);
    delete[] Wma->Pcm;
    delete Wma;
    Voice->Wma = nullptr;
}

/* Only the pointers are checked; the packet table is taken as it comes */
HRESULT
XaWmaSubmit(
    _Inout_ XaSourceVoice* Voice,
    _In_opt_ const XAUDIO2_BUFFER* Buffer,
    _In_opt_ const XAUDIO2_BUFFER_WMA* BufferWma)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer* Entry;

    if (Voice->QueuedCount >= XA_MAX_QUEUED || Buffer == nullptr || BufferWma == nullptr)
    {
        XA_TRACE_ONCE(L"SubmitSourceBuffer: the queue is full or an xWMA buffer pointer is NULL");
        return XAUDIO2_E_INVALID_CALL;
    }
    Entry = new (std::nothrow) XaWmaBuffer;
    if (Entry == nullptr)
        return E_OUTOFMEMORY;
    memset(Entry, 0, sizeof(*Entry));
    Entry->Flags = Buffer->Flags;
    Entry->Bytes = Buffer->AudioBytes;
    Entry->Data = Buffer->pAudioData;
    Entry->BeginBytes = Wma->FrameBytes * Buffer->PlayBegin;
    if (Buffer->PlayLength != 0)
    {
        Entry->LengthBytes = Wma->FrameBytes * Buffer->PlayLength;
    }
    else
    {
        Entry->LengthBytes = BufferWma->pDecodedPacketCumulativeBytes[BufferWma->PacketCount - 1];
        if (Buffer->LoopCount == 0)
            Entry->LengthBytes -= Entry->BeginBytes;
    }
    Entry->LoopCount = Buffer->LoopCount;
    Entry->Context = Buffer->pContext;
    Entry->Cumulative = BufferWma->pDecodedPacketCumulativeBytes;
    Entry->PacketCount = BufferWma->PacketCount;
    if (Wma->PendingTail != nullptr)
        Wma->PendingTail->Next = Entry;
    else
        Wma->Pending = Entry;
    Wma->PendingTail = Entry;
    Voice->QueuedCount += 1;
    return S_OK;
}

/* A running voice keeps the buffer it plays; a stopped one drops everything */
VOID
XaWmaFlush(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer* Keep = nullptr;
    XaWmaBuffer* Buffer = Wma->Pending;

    if (Voice->PlayState == XaPlayRunning && Buffer != nullptr)
    {
        Keep = Buffer;
        Buffer = Buffer->Next;
        Keep->Next = nullptr;
    }
    else
    {
        Wma->Input = nullptr;
        Wma->InputLeft = 0;
        Wma->InputLast = FALSE;
    }
    Wma->Pending = Keep;
    Wma->PendingTail = Keep;
    while (Buffer != nullptr)
    {
        XaWmaBuffer* Next = Buffer->Next;
        void* Context = Buffer->Context;

        delete Buffer;
        Voice->QueuedCount -= 1;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnBufferEnd(Context);
        Buffer = Next;
    }
    while (Wma->Taken != nullptr)
    {
        Buffer = Wma->Taken;
        Wma->Taken = Buffer->Next;
        Voice->QueuedCount -= 1;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnBufferEnd(Buffer->Context);
        delete Buffer;
    }
    Wma->TakenTail = nullptr;
}

/* Drops every buffer of a stopped voice now, leaving OnBufferEnd for the next engine pass */
VOID
XaWmaFlushStopped(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;
    XaWmaBuffer** Tail = &Wma->Dropped;
    XaWmaBuffer* Lists[2] = { Wma->Taken, Wma->Pending };

    while (*Tail != nullptr)
        Tail = &(*Tail)->Next;
    for (UINT32 Index = 0; Index < ARRAYSIZE(Lists); Index++)
    {
        for (XaWmaBuffer* Buffer = Lists[Index]; Buffer != nullptr; Buffer = Buffer->Next)
            Voice->QueuedCount -= 1;
        *Tail = Lists[Index];
        while (*Tail != nullptr)
            Tail = &(*Tail)->Next;
    }
    Wma->Taken = nullptr;
    Wma->TakenTail = nullptr;
    Wma->Pending = nullptr;
    Wma->PendingTail = nullptr;
    Wma->Input = nullptr;
    Wma->InputLeft = 0;
    Wma->InputLast = FALSE;
}

VOID
XaWmaReportDropped(
    _Inout_ XaSourceVoice* Voice)
{
    XaWmaSource* Wma = Voice->Wma;

    while (Wma->Dropped != nullptr)
    {
        XaWmaBuffer* Buffer = Wma->Dropped;

        Wma->Dropped = Buffer->Next;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnBufferEnd(Buffer->Context);
        delete Buffer;
    }
}

VOID
XaWmaDiscontinuity(
    _Inout_ XaSourceVoice* Voice)
{
    if (Voice->Wma->PendingTail != nullptr)
        Voice->Wma->PendingTail->Flags |= XAUDIO2_END_OF_STREAM;
}

/* The loop in progress becomes the last one */
VOID
XaWmaExitLoop(
    _Inout_ XaSourceVoice* Voice)
{
    Voice->Wma->Looping = FALSE;
}

VOID
XaWmaGetState(
    _In_ const XaSourceVoice* Voice,
    _Out_ XAUDIO2_VOICE_STATE* State,
    _In_ BOOLEAN WithSamples)
{
    const XaWmaSource* Wma = Voice->Wma;
    const XaWmaBuffer* Oldest = Wma->Taken != nullptr ? Wma->Taken : Wma->Pending;

    State->pCurrentBufferContext = Oldest != nullptr ? Oldest->Context : nullptr;
    State->BuffersQueued = Voice->QueuedCount;
    if (WithSamples)
        State->SamplesPlayed = Wma->DeliveredBytes / Wma->FrameBytes;
}

/* Frames already decoded and waiting */
UINT32
XaWmaQueuedFrames(
    _In_ const XaSourceVoice* Voice)
{
    return Voice->Wma->PcmCount;
}

/* Decodes what a pass is short of, then hands out up to Needed frames */
UINT32
XaWmaGather(
    _Inout_ XaSourceVoice* Voice,
    _In_ UINT32 Needed,
    _Out_writes_(Needed * Voice->InputChannels) float* Output)
{
    XaWmaSource* Wma = Voice->Wma;
    UINT32 Channels = Voice->InputChannels;
    UINT32 Count;
    const SHORT* Words;

    if (Needed > Wma->PcmCount)
    {
        HRESULT hr = XaWmaFill(Voice, static_cast<INT32>((Needed - Wma->PcmCount) * Wma->FrameBytes));

        if (FAILED(hr))
            XA_TRACE_ONCE(L"xWMA decode failed (%08lX)", hr);
    }
    Count = XaMin(Needed, Wma->PcmCount);
    Words = Wma->Pcm + static_cast<SIZE_T>(Wma->PcmStart) * Channels;
    for (UINT32 Index = 0; Index < Count * Channels; Index++)
        Output[Index] = Words[Index] * (1.0f / 32768.0f);
    if (Voice->RawFrames != nullptr)
        memcpy(Voice->RawFrames, Words, sizeof(SHORT) * Count * Channels);
    Wma->PcmStart += Count;
    Wma->PcmCount -= Count;
    if (Count < Needed)
    {
        memset(Output + static_cast<SIZE_T>(Count) * Channels, 0, sizeof(*Output) * (Needed - Count) * Channels);
        if (Voice->RawFrames != nullptr)
            memset(Voice->RawFrames + static_cast<SIZE_T>(Count) * Channels, 0, sizeof(SHORT) * (Needed - Count) * Channels);
    }
    return Count;
}
