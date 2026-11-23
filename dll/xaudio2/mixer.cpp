/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     One processing pass: sources, then submixes by stage, then the mastering voice
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

static const INT32 XaAdpcmAdaptation[16] =
{
    230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230,
};

static const INT32 XaAdpcmCoef1[7] = { 256, 512, 0, 192, 240, 460, 392 };
static const INT32 XaAdpcmCoef2[7] = { 0, -256, 0, 64, 0, -208, -232 };

UINT32
XaQuantumFrames(
    _In_ UINT32 Rate,
    _In_ const XaEngine* Engine)
{
    return static_cast<UINT32>(static_cast<UINT64>(Rate) * Engine->QuantumNumerator / Engine->QuantumDenominator);
}

/* Grows a scratch buffer to hold at least Count floats */
static
float*
XaScratch(
    _Inout_ XaScratchBuffer* Scratch,
    _In_ SIZE_T Count)
{
    if (Scratch->Capacity < Count)
    {
        float* Grown = new (std::nothrow) float[Count];

        if (Grown == nullptr)
            return nullptr;
        delete[] Scratch->Data;
        Scratch->Data = Grown;
        Scratch->Capacity = Count;
    }
    return Scratch->Data;
}

VOID
XaScratchFree(
    _Inout_ XaScratchBuffer* Scratch)
{
    delete[] Scratch->Data;
    Scratch->Data = nullptr;
    Scratch->Capacity = 0;
}

static
INT32
XaClamp16(
    _In_ INT32 Value)
{
    return Value < -32768 ? -32768 : Value > 32767 ? 32767 : Value;
}

/* Decodes one MS ADPCM block into 16 bit samples */
static
VOID
XaDecodeAdpcmBlock(
    _In_reads_bytes_(BlockAlign) const BYTE* Block,
    _In_ UINT32 BlockAlign,
    _In_ UINT32 Channels,
    _In_ UINT32 Frames,
    _Out_writes_(Frames * Channels) SHORT* Output)
{
    INT32 Predictor[2];
    INT32 Delta[2];
    INT32 Sample1[2];
    INT32 Sample2[2];
    const BYTE* Data = Block;
    UINT32 Written;

    for (UINT32 Channel = 0; Channel < Channels; Channel++)
        Predictor[Channel] = XaMin<INT32>(Data[Channel], 6);
    Data += Channels;
    for (UINT32 Channel = 0; Channel < Channels; Channel++)
        Delta[Channel] = static_cast<SHORT>(Data[Channel * 2] | (Data[Channel * 2 + 1] << 8));
    Data += 2 * Channels;
    for (UINT32 Channel = 0; Channel < Channels; Channel++)
        Sample1[Channel] = static_cast<SHORT>(Data[Channel * 2] | (Data[Channel * 2 + 1] << 8));
    Data += 2 * Channels;
    for (UINT32 Channel = 0; Channel < Channels; Channel++)
        Sample2[Channel] = static_cast<SHORT>(Data[Channel * 2] | (Data[Channel * 2 + 1] << 8));
    Data += 2 * Channels;

    for (UINT32 Channel = 0; Channel < Channels; Channel++)
    {
        Output[Channel] = static_cast<SHORT>(Sample2[Channel]);
        if (Frames > 1)
            Output[Channels + Channel] = static_cast<SHORT>(Sample1[Channel]);
    }
    Written = 2 * Channels;
    while (Written < Frames * Channels && Data < Block + BlockAlign)
    {
        for (UINT32 Half = 0; Half < 2 && Written < Frames * Channels; Half++)
        {
            UINT32 Channel = Written % Channels;
            INT32 Nibble = Half == 0 ? (*Data >> 4) : (*Data & 0xF);
            INT32 Signed = Nibble >= 8 ? Nibble - 16 : Nibble;
            INT32 Value;

            Value = (Sample1[Channel] * XaAdpcmCoef1[Predictor[Channel]] + Sample2[Channel] * XaAdpcmCoef2[Predictor[Channel]]) >> 8;
            Value = XaClamp16(Value + Signed * Delta[Channel]);
            Sample2[Channel] = Sample1[Channel];
            Sample1[Channel] = Value;
            Delta[Channel] = XaMax<INT32>((XaAdpcmAdaptation[Nibble] * Delta[Channel]) >> 8, 16);
            Output[Written++] = static_cast<SHORT>(Value);
        }
        Data += 1;
    }
    while (Written < Frames * Channels)
        Output[Written++] = 0;
}

/* Converts Count frames starting at Frame of a buffer to float */
static
VOID
XaReadFrames(
    _Inout_ XaSourceVoice* Voice,
    _In_ const XaQueuedBuffer* Buffer,
    _In_ UINT32 Frame,
    _In_ UINT32 Count,
    _Out_writes_(Count * Voice->InputChannels) float* Output)
{
    UINT32 Channels = Voice->InputChannels;
    UINT32 Samples = Count * Channels;
    WORD Bits = Voice->Format->wBitsPerSample;

    if (Voice->FormatTag == WAVE_FORMAT_ADPCM)
    {
        UINT32 PerBlock = Voice->FramesPerBlock;

        for (UINT32 Index = 0; Index < Count; Index++)
        {
            UINT32 Block = (Frame + Index) / PerBlock;

            if (Voice->Decoded == nullptr)
            {
                Voice->Decoded = new (std::nothrow) SHORT[PerBlock * Channels];
                if (Voice->Decoded == nullptr)
                {
                    memset(Output, 0, sizeof(*Output) * Samples);
                    return;
                }
                Voice->DecodedBlock = MAXUINT32;
            }
            if (Voice->DecodedData != Buffer->Data || Voice->DecodedBlock != Block)
            {
                XaDecodeAdpcmBlock(Buffer->Data + static_cast<SIZE_T>(Block) * Voice->BlockAlign, Voice->BlockAlign, Channels,
                                   PerBlock, Voice->Decoded);
                Voice->DecodedData = Buffer->Data;
                Voice->DecodedBlock = Block;
            }
            for (UINT32 Channel = 0; Channel < Channels; Channel++)
                Output[Index * Channels + Channel] = Voice->Decoded[((Frame + Index) % PerBlock) * Channels + Channel] / 32768.0f;
        }
        return;
    }
    if (Voice->FormatTag != WAVE_FORMAT_PCM && Voice->FormatTag != WAVE_FORMAT_IEEE_FLOAT)
    {
        /* Compressed streams this runtime cannot decode play as silence */
        memset(Output, 0, sizeof(*Output) * Samples);
        return;
    }

    {
        const BYTE* Data = Buffer->Data + static_cast<SIZE_T>(Frame) * Voice->BlockAlign;

        if (Voice->FormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            memcpy(Output, Data, sizeof(float) * Samples);
        }
        else if (Bits == 8)
        {
            for (UINT32 Index = 0; Index < Samples; Index++)
                Output[Index] = (static_cast<INT32>(Data[Index]) - 128) / 128.0f;
        }
        else if (Bits == 16)
        {
            const SHORT* Words = reinterpret_cast<const SHORT*>(Data);

            for (UINT32 Index = 0; Index < Samples; Index++)
                Output[Index] = Words[Index] * (1.0f / 32768.0f);
        }
        else if (Bits == 24)
        {
            for (UINT32 Index = 0; Index < Samples; Index++)
            {
                INT32 Value = (Data[Index * 3] << 8) | (Data[Index * 3 + 1] << 16) | (Data[Index * 3 + 2] << 24);

                Output[Index] = static_cast<float>(Value >> 8) / 8388608.0f;
            }
        }
        else
        {
            const INT32* Longs = reinterpret_cast<const INT32*>(Data);

            for (UINT32 Index = 0; Index < Samples; Index++)
                Output[Index] = static_cast<float>(Longs[Index]) / 2147483648.0f;
        }
    }
}

/* Frames left to play in the queue, counting the loops still to come */
static
UINT64
XaQueuedFrames(
    _In_ const XaSourceVoice* Voice)
{
    UINT64 Total = 0;

    for (const XaQueuedBuffer* Buffer = Voice->Queue; Buffer != nullptr; Buffer = Buffer->Next)
    {
        UINT32 Position = Buffer->Position;

        if (Buffer->LoopCount != 0 && Buffer->LoopsDone < Buffer->LoopCount && Position < Buffer->LoopEnd)
        {
            if (Buffer->LoopCount == XAUDIO2_LOOP_INFINITE)
                return MAXUINT64;
            Total += Buffer->LoopEnd - Position;
            Total += static_cast<UINT64>(Buffer->LoopCount - Buffer->LoopsDone - 1) * (Buffer->LoopEnd - Buffer->LoopBegin);
            Position = Buffer->LoopBegin;
        }
        Total += Buffer->PlayEnd - Position;
    }
    return Total;
}

/* Ends the buffer at the head of the queue */
static
VOID
XaRetireHead(
    _Inout_ XaSourceVoice* Voice)
{
    XaQueuedBuffer* Buffer = Voice->Queue;
    BOOLEAN EndOfStream = (Buffer->Flags & XAUDIO2_END_OF_STREAM) != 0;
    void* Context = Buffer->Context;

    Voice->Queue = Buffer->Next;
    if (Voice->Queue == nullptr)
        Voice->QueueTail = nullptr;
    Voice->QueuedCount -= 1;
    delete Buffer;
    if (Voice->Callback != nullptr)
        Voice->Callback->OnBufferEnd(Context);
    if (EndOfStream)
    {
        Voice->PlayedFrames = 0;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnStreamEnd();
    }
}

/* Pulls Needed frames out of the queue, starting and ending buffers on the way */
static
UINT32
XaGatherFrames(
    _Inout_ XaSourceVoice* Voice,
    _In_ UINT32 Needed,
    _Out_writes_(Needed * Voice->InputChannels) float* Output)
{
    UINT32 Gathered = 0;
    UINT32 Channels = Voice->InputChannels;

    while (Gathered < Needed && Voice->Queue != nullptr)
    {
        XaQueuedBuffer* Buffer = Voice->Queue;
        BOOLEAN Looping = Buffer->LoopCount != 0 && Buffer->LoopsDone < Buffer->LoopCount && Buffer->Position < Buffer->LoopEnd;
        UINT32 End = Looping ? Buffer->LoopEnd : Buffer->PlayEnd;
        UINT32 Count;

        if (!Buffer->Started)
        {
            Buffer->Started = TRUE;
            if (Voice->Callback != nullptr)
                Voice->Callback->OnBufferStart(Buffer->Context);
        }
        Count = XaMin(End - Buffer->Position, Needed - Gathered);
        XaReadFrames(Voice, Buffer, Buffer->Position, Count, Output + static_cast<SIZE_T>(Gathered) * Channels);
        if (Voice->RawFrames != nullptr)
        {
            memcpy(Voice->RawFrames + static_cast<SIZE_T>(Gathered) * Channels,
                   Buffer->Data + static_cast<SIZE_T>(Buffer->Position) * Voice->BlockAlign, sizeof(SHORT) * Count * Channels);
        }
        Buffer->Position += Count;
        Gathered += Count;
        Voice->PlayedFrames += Count;
        if (Buffer->Position < End)
            break;
        if (Looping)
        {
            if (Buffer->LoopCount != XAUDIO2_LOOP_INFINITE)
                Buffer->LoopsDone += 1;
            Buffer->Position = Buffer->LoopBegin;
            if (Voice->Callback != nullptr)
                Voice->Callback->OnLoopEnd(Buffer->Context);
            continue;
        }
        XaRetireHead(Voice);
    }
    if (Gathered < Needed)
    {
        memset(Output + static_cast<SIZE_T>(Gathered) * Channels, 0, sizeof(*Output) * (Needed - Gathered) * Channels);
        if (Voice->RawFrames != nullptr)
            memset(Voice->RawFrames + static_cast<SIZE_T>(Gathered) * Channels, 0, sizeof(SHORT) * (Needed - Gathered) * Channels);
    }
    return Gathered;
}

/* Mixes a voice's output through each of its sends into the voices it feeds */
static
VOID
XaMixSends(
    _Inout_ XaVoice* Voice,
    _In_reads_(Frames * Voice->OutputChannels()) const float* Output,
    _In_ UINT32 Frames,
    _In_ BOOLEAN Silent)
{
    UINT32 Inputs = Voice->OutputChannels();

    if (Silent)
        return;

    for (XaSend* Send = Voice->Sends; Send != nullptr; Send = Send->Next)
    {
        XaVoice* Target = Send->Target;
        UINT32 Count = XaMin(Frames, Target->InputMixFrames);
        BOOLEAN First;

        if (Target->InputMix == nullptr)
            continue;
        First = !Target->InputFed;
        Target->InputFed = TRUE;
        XaMixThroughSend(Send, Output, Inputs, Target->InputMix, Count, Target->InputRate, !First);
    }
}

VOID
XaVoice::CommitParameters(
    _In_ UINT32 OperationSet)
{
    XaParameterCommit(&Filter, OperationSet);
    if (Effects != nullptr)
        XaEffectsCommit(Effects, OperationSet);
    for (XaSend* Send = Sends; Send != nullptr; Send = Send->Next)
        XaParameterCommit(&Send->Mix, OperationSet);
    if (Kind == XaKindSource)
    {
        XaSourceVoice* Source = static_cast<XaSourceVoice*>(this);

        XaParameterCommit(&Source->Ratio, OperationSet);
        if (Source->ExitLoopPending && (OperationSet == XAUDIO2_COMMIT_ALL || OperationSet == Source->ExitLoopSet))
            Source->ExitLoopSet = XA_OPSET_NOW;
    }
}

/* Picks up the parameters the API side changed since the voice last ran */
static
VOID
XaTakeParameters(
    _Inout_ XaVoice* Voice)
{
    XaParameterTake(&Voice->Filter);
    for (XaSend* Send = Voice->Sends; Send != nullptr; Send = Send->Next)
    {
        if (XaParameterTake(&Send->Mix))
            Send->Changed = TRUE;
    }
}

/* Drops the queued buffers a flush asked for, keeping the one playing */
static
VOID
XaApplyFlush(
    _Inout_ XaSourceVoice* Voice)
{
    XaQueuedBuffer** Link = &Voice->Queue;

    Voice->FlushPending = FALSE;
    if (Voice->PlayState == XaPlayRunning && *Link != nullptr && (*Link)->Started)
        Link = &(*Link)->Next;
    while (*Link != nullptr)
    {
        XaQueuedBuffer* Buffer = *Link;
        void* Context = Buffer->Context;

        *Link = Buffer->Next;
        Voice->QueuedCount -= 1;
        delete Buffer;
        if (Voice->Callback != nullptr)
            Voice->Callback->OnBufferEnd(Context);
    }
    Voice->QueueTail = nullptr;
    for (XaQueuedBuffer* Buffer = Voice->Queue; Buffer != nullptr; Buffer = Buffer->Next)
        Voice->QueueTail = Buffer;
}

/* The voice filter runs in place on what the voice is about to process */
static
VOID
XaApplyVoiceFilter(
    _Inout_ XaVoice* Voice,
    _Inout_ float* Buffer,
    _In_ UINT32 Frames,
    _Inout_ BOOLEAN* Silent)
{
    const XAUDIO2_FILTER_PARAMETERS* Parameters;
    UINT32 Channels = Voice->InputChannels;

    if (Voice->FilterState == nullptr)
        return;
    Parameters = reinterpret_cast<const XAUDIO2_FILTER_PARAMETERS*>(Voice->Filter.Current);
    if (*Silent || XaFilterSilences(Parameters))
    {
        if (Voice->FilterActive)
        {
            memset(Voice->FilterState, 0, sizeof(float) * 2 * Channels);
            Voice->FilterActive = FALSE;
        }
        if (!*Silent)
        {
            memset(Buffer, 0, sizeof(float) * Frames * Channels);
            *Silent = TRUE;
        }
        return;
    }
    XaFilterRun(Parameters, Voice->FilterState, Buffer, Buffer, Frames, Channels);
    Voice->FilterActive = TRUE;
}

/* 16 bit PCM through a converter keeps its integer frames for the interpolation */
static
VOID
XaPrepareRaw(
    _Inout_ XaSourceVoice* Voice,
    _In_ UINT32 Needed)
{
    SIZE_T Count = static_cast<SIZE_T>(Needed) * Voice->InputChannels;

    if (Voice->FormatTag != WAVE_FORMAT_PCM || Voice->Format->wBitsPerSample != 16 || !Voice->HasConverter)
        return;
    if (Voice->RawCapacity < Count)
    {
        delete[] Voice->RawFrames;
        Voice->RawFrames = new (std::nothrow) SHORT[Count];
        Voice->RawCapacity = Voice->RawFrames != nullptr ? Count : 0;
    }
}

static
VOID
XaProcessSource(
    _Inout_ XaSourceVoice* Voice)
{
    UINT32 Frames = XaQuantumFrames(Voice->OutputRate, Voice->Engine);
    UINT32 Channels = Voice->InputChannels;
    UINT32 Needed = Frames;
    UINT64 Available;
    UINT32 Required = 0;
    UINT32 Gathered;
    float* Input;
    float* Output;

    if (Voice->FlushPending)
        XaApplyFlush(Voice);
    if (Voice->PlayState != XaPlayRunning)
        return;

    if (XaParameterTake(&Voice->Ratio) && Voice->Resampler != nullptr)
        XaResamplerSetRatio(Voice->Resampler, *reinterpret_cast<float*>(Voice->Ratio.Current));
    if (Voice->ExitLoopPending && Voice->ExitLoopSet == XA_OPSET_NOW)
    {
        Voice->ExitLoopPending = FALSE;
        if (Voice->Queue != nullptr && Voice->Queue->LoopCount != 0)
            Voice->Queue->LoopCount = Voice->Queue->LoopsDone;
    }

    if (Voice->HasConverter)
    {
        if (Voice->Resampler == nullptr)
        {
            Voice->Resampler = XaResamplerCreate(Channels, Voice->InputRate, Voice->OutputRate,
                                                 (Voice->CreationFlags & XAUDIO2_VOICE_NOPITCH) != 0);
            if (Voice->Resampler == nullptr)
                return;
            XaResamplerSetRatio(Voice->Resampler, *reinterpret_cast<float*>(Voice->Ratio.Current));
        }
        Needed = XaResamplerInputFrames(Voice->Resampler, Frames);
    }

    Available = XaQueuedFrames(Voice);
    if (Needed > Available)
        Required = static_cast<UINT32>((Needed - Available) * Voice->BlockAlign / Voice->FramesPerBlock);
    if (Voice->Callback != nullptr)
        Voice->Callback->OnVoiceProcessingPassStart(Required);

    /* What the callback changed at once still applies to this pass */
    XaTakeParameters(Voice);

    Input = XaScratch(&Voice->InputScratch, static_cast<SIZE_T>(Needed) * Channels);
    Output = XaScratch(&Voice->OutputScratch, static_cast<SIZE_T>(Frames) * Channels);
    if (Input != nullptr && Output != nullptr)
    {
        XaPrepareRaw(Voice, Needed);
        Gathered = XaGatherFrames(Voice, Needed, Input);
        if (Voice->HasConverter)
        {
            if (Gathered == 0)
                XaResamplerReset(Voice->Resampler);
            XaResamplerProcess(Voice->Resampler, Input, Needed, Voice->RawFrames, Output, Frames);
        }
        else
        {
            memcpy(Output, Input, sizeof(*Output) * Frames * Channels);
        }
        {
            BOOLEAN Silent = Gathered == 0;
            float* Result = Output;

            XaApplyVoiceFilter(Voice, Output, Frames, &Silent);
            if (Voice->Effects != nullptr)
                Result = XaEffectsProcess(Voice->Effects, Output, Frames, &Silent);
            XaMixSends(Voice, Result, Frames, Silent);
        }
    }
    if (Voice->Callback != nullptr)
        Voice->Callback->OnVoiceProcessingPassEnd();
}

static
VOID
XaProcessSubmix(
    _Inout_ XaVoice* Voice)
{
    UINT32 Frames = Voice->InputMixFrames;
    UINT32 Channels = Voice->OutputChannels();
    const float* Output = Voice->InputMix;

    BOOLEAN Silent = !Voice->InputFed;

    XaTakeParameters(Voice);
    if (Output == nullptr)
        return;
    XaApplyVoiceFilter(Voice, Voice->InputMix, Frames, &Silent);
    if (Voice->Effects != nullptr)
        Output = XaEffectsProcess(Voice->Effects, Voice->InputMix, Frames, &Silent);
    if (Voice->OutputRate != Voice->InputRate)
    {
        UINT32 OutputFrames = XaQuantumFrames(Voice->OutputRate, Voice->Engine);
        float* Converted;

        if (Voice->Resampler == nullptr)
        {
            Voice->Resampler = XaResamplerCreate(Channels, Voice->InputRate, Voice->OutputRate, TRUE);
            if (Voice->Resampler == nullptr)
                return;
        }
        Converted = XaScratch(&Voice->OutputScratch, static_cast<SIZE_T>(OutputFrames) * Channels);
        if (Converted == nullptr)
            return;
        XaResamplerProcess(Voice->Resampler, Output, Frames, nullptr, Converted, OutputFrames);
        Output = Converted;
        Frames = OutputFrames;
    }
    XaMixSends(Voice, Output, Frames, Silent);
}

HRESULT
XaMixerPrepareMaster(
    _Inout_ XaMasterVoice* Master)
{
    UINT32 DeviceChannels = Master->Endpoint->Format.Format.nChannels;
    DWORD DeviceMask = Master->Endpoint->Format.Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE ?
                       Master->Endpoint->Format.dwChannelMask : XaPseudoMask(DeviceChannels);
    HRESULT hr;

    hr = Master->EnsureInputMix();
    if (FAILED(hr))
        return hr;
    if (DeviceChannels != Master->OutputChannels() || DeviceMask != Master->OutputMask())
    {
        Master->DeviceMatrix = new (std::nothrow) float[DeviceChannels * Master->OutputChannels()];
        if (Master->DeviceMatrix == nullptr)
            return E_OUTOFMEMORY;
        hr = XaDefaultMatrix(Master->OutputChannels(), DeviceChannels, Master->OutputMask(), DeviceMask, Master->DeviceMatrix);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

VOID
XaMixerReleaseMaster(
    _Inout_ XaMasterVoice* Master)
{
    delete[] Master->DeviceMatrix;
    Master->DeviceMatrix = nullptr;
}

/* Turns the mastering voice's mix into one device quantum */
static
VOID
XaProcessMaster(
    _Inout_ XaMasterVoice* Master,
    _Out_writes_(DeviceFrames * Master->Endpoint->Format.Format.nChannels) float* Device,
    _In_ UINT32 DeviceFrames)
{
    UINT32 Channels = Master->OutputChannels();
    UINT32 DeviceChannels = Master->Endpoint->Format.Format.nChannels;
    UINT32 Frames = Master->InputMixFrames;
    const float* Mix = Master->InputMix;
    BOOLEAN Silent = !Master->InputFed;

    if (Master->Effects != nullptr)
    {
        Mix = XaEffectsProcess(Master->Effects, Master->InputMix, Frames, &Silent);
        if (Silent)
            memset(const_cast<float*>(Mix), 0, sizeof(*Mix) * Frames * Channels);
    }

    if (Master->Endpoint->Format.Format.nSamplesPerSec != Master->InputRate)
    {
        float* Converted;

        if (Master->Resampler == nullptr)
        {
            Master->Resampler = XaResamplerCreate(Channels, Master->InputRate, Master->Endpoint->Format.Format.nSamplesPerSec, TRUE);
            if (Master->Resampler == nullptr)
            {
                memset(Device, 0, sizeof(*Device) * DeviceFrames * DeviceChannels);
                return;
            }
        }
        Converted = XaScratch(&Master->OutputScratch, static_cast<SIZE_T>(DeviceFrames) * Channels);
        if (Converted == nullptr)
        {
            memset(Device, 0, sizeof(*Device) * DeviceFrames * DeviceChannels);
            return;
        }
        XaResamplerProcess(Master->Resampler, Mix, Frames, nullptr, Converted, DeviceFrames);
        Mix = Converted;
        Frames = DeviceFrames;
    }
    Frames = XaMin(Frames, DeviceFrames);
    if (Master->DeviceMatrix == nullptr)
    {
        memcpy(Device, Mix, sizeof(*Device) * Frames * DeviceChannels);
    }
    else
    {
        for (UINT32 Frame = 0; Frame < Frames; Frame++)
        {
            for (UINT32 Row = 0; Row < DeviceChannels; Row++)
            {
                float Sum = 0.0f;

                for (UINT32 Column = 0; Column < Channels; Column++)
                    Sum += Mix[Frame * Channels + Column] * Master->DeviceMatrix[Row * Channels + Column];
                Device[Frame * DeviceChannels + Row] = Sum;
            }
        }
    }
    if (Frames < DeviceFrames)
        memset(Device + static_cast<SIZE_T>(Frames) * DeviceChannels, 0, sizeof(*Device) * (DeviceFrames - Frames) * DeviceChannels);
}

VOID
XaEngine::RunPass(
    _Out_writes_(DeviceFrames * Master->Endpoint->Format.Format.nChannels) float* Device,
    _In_ UINT32 DeviceFrames)
{
    UINT32 HighestStage = 0;

    for (UINT32 Index = 0; Index < CallbackCount; Index++)
        Callbacks[Index]->OnProcessingPassStart();
    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
    {
        XaParameterPromote(&Voice->Filter);
        for (XaSend* Send = Voice->Sends; Send != nullptr; Send = Send->Next)
            XaParameterPromote(&Send->Mix);
        if (Voice->Kind == XaKindSource)
            XaParameterPromote(&static_cast<XaSourceVoice*>(Voice)->Ratio);
    }

    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
    {
        if (Voice->Kind == XaKindSource)
            continue;
        if (FAILED(Voice->EnsureInputMix()))
            continue;
        memset(Voice->InputMix, 0, sizeof(*Voice->InputMix) * Voice->InputMixFrames * Voice->InputChannels);
        Voice->InputFed = FALSE;
        if (Voice->Kind == XaKindSubmix)
            HighestStage = XaMax(HighestStage, Voice->ProcessingStage);
    }
    for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
    {
        if (Voice->Kind == XaKindSource)
            XaProcessSource(static_cast<XaSourceVoice*>(Voice));
    }

    /* Stages run in order, each one after everything that can send to it */
    for (UINT64 Stage = 0; Stage <= HighestStage; Stage++)
    {
        UINT32 Next = MAXUINT32;

        for (XaVoice* Voice = Voices; Voice != nullptr; Voice = Voice->NextVoice)
        {
            if (Voice->Kind != XaKindSubmix)
                continue;
            if (Voice->ProcessingStage == Stage)
                XaProcessSubmix(Voice);
            else if (Voice->ProcessingStage > Stage && Voice->ProcessingStage < Next)
                Next = Voice->ProcessingStage;
        }
        if (Next == MAXUINT32)
            break;
        Stage = static_cast<UINT64>(Next) - 1;
    }

    XaTakeParameters(Master);
    XaProcessMaster(Master, Device, DeviceFrames);

    for (UINT32 Index = 0; Index < CallbackCount; Index++)
        Callbacks[Index]->OnProcessingPassEnd();
}

/* Runs passes while the endpoint has room for another device quantum */
VOID
XaEngine::RenderAvailable(VOID)
{
    XaEndpoint* Endpoint = Master->Endpoint;
    UINT32 DeviceFrames = XaQuantumFrames(Endpoint->Format.Format.nSamplesPerSec, this);

    for (;;)
    {
        UINT32 Padding = 0;
        BYTE* Data = nullptr;
        HRESULT hr;

        hr = Endpoint->Client->GetCurrentPadding(&Padding);
        if (FAILED(hr))
        {
            XA_TRACE_ONCE(L"render: GetCurrentPadding failed (%08lX)", hr);
            return;
        }
        if (Endpoint->BufferFrames - Padding < DeviceFrames)
            return;
        hr = Endpoint->Render->GetBuffer(DeviceFrames, &Data);
        if (FAILED(hr))
        {
            XA_TRACE_ONCE(L"render: GetBuffer failed (%08lX)", hr);
            return;
        }
        RunPass(reinterpret_cast<float*>(Data), DeviceFrames);
        Endpoint->Render->ReleaseBuffer(DeviceFrames, 0);
        if (Master == nullptr)
            return;
    }
}

HANDLE
XaJoinAudioTask(
    _Out_ DWORD* Task)
{
    *Task = 0;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    return nullptr;
}

VOID
XaLeaveAudioTask(
    _In_opt_ HANDLE Mmcss)
{
    UNREFERENCED_PARAMETER(Mmcss);
}
