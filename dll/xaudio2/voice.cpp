/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What every voice kind shares: sends, volumes, matrices and filters
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

#define XA_VOLUME_LIMIT     16777216.0f
#define XA_FILTER_TYPES     6
#define XA_FILTER_MAX_Q     1.5f

static const XAUDIO2_FILTER_PARAMETERS XaDefaultFilter = { LowPassOnePoleFilter, 1.0f, 1.0f };

BOOLEAN
XaParameterInit(
    _Out_ XaParameterSlot* Slot,
    _In_ UINT32 Size,
    _In_reads_bytes_(Size) const VOID* Initial)
{
    Slot->Pending = new (std::nothrow) BYTE[Size];
    Slot->Current = new (std::nothrow) BYTE[Size];
    Slot->Size = Size;
    Slot->State = XaSlotIdle;
    Slot->OperationSet = XA_OPSET_NOW;
    if (Slot->Pending == nullptr || Slot->Current == nullptr)
    {
        XaParameterFree(Slot);
        return FALSE;
    }
    memcpy(Slot->Pending, Initial, Size);
    memcpy(Slot->Current, Initial, Size);
    return TRUE;
}

VOID
XaParameterFree(
    _Inout_ XaParameterSlot* Slot)
{
    delete[] Slot->Pending;
    delete[] Slot->Current;
    Slot->Pending = nullptr;
    Slot->Current = nullptr;
    Slot->Size = 0;
}

VOID
XaParameterSet(
    _Inout_ XaParameterSlot* Slot,
    _In_reads_bytes_(Slot->Size) const VOID* Value,
    _In_ UINT32 OperationSet)
{
    memcpy(Slot->Pending, Value, Slot->Size);
    Slot->OperationSet = OperationSet;
    Slot->State = OperationSet == XA_OPSET_NOW ? XaSlotReady : XaSlotWaiting;
}

VOID
XaParameterCommit(
    _Inout_ XaParameterSlot* Slot,
    _In_ UINT32 OperationSet)
{
    if (Slot->State == XaSlotWaiting && (OperationSet == XAUDIO2_COMMIT_ALL || OperationSet == Slot->OperationSet))
        Slot->State = XaSlotReady;
}

BOOLEAN
XaParameterTake(
    _Inout_ XaParameterSlot* Slot)
{
    if (Slot->Size == 0 || Slot->State != XaSlotReady)
        return FALSE;
    memcpy(Slot->Current, Slot->Pending, Slot->Size);
    Slot->State = XaSlotIdle;
    return TRUE;
}

static
BOOLEAN
XaFilterValid(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters)
{
    if (static_cast<UINT32>(Parameters->Type) >= XA_FILTER_TYPES)
        return FALSE;
    if (!(Parameters->Frequency >= 0.0f && Parameters->Frequency <= XAUDIO2_MAX_FILTER_FREQUENCY))
        return FALSE;
    return Parameters->OneOverQ > 0.0f && Parameters->OneOverQ <= XA_FILTER_MAX_Q;
}

XaVoice::XaVoice(
    _In_ XaEngine* NewEngine,
    _In_ XaVoiceKind NewKind)
{
    Engine = NewEngine;
    Kind = NewKind;
    NextVoice = nullptr;
    CreationFlags = 0;
    ActiveFlags = 0;
    InputChannels = 0;
    InputRate = 0;
    InputMask = 0;
    OutputRate = 0;
    ProcessingStage = 0;
    Volume = 1.0f;
    ChannelVolumes = nullptr;
    Sends = nullptr;
    SendCount = 0;
    SenderCount = 0;
    Effects = nullptr;
    memset(&Filter, 0, sizeof(Filter));
    Destroyed = FALSE;
    InputMix = nullptr;
    InputMixFrames = 0;
    Resampler = nullptr;
    FilterState = nullptr;
    memset(&InputScratch, 0, sizeof(InputScratch));
    memset(&OutputScratch, 0, sizeof(OutputScratch));
}

XaVoice::~XaVoice()
{
    ReleaseSends();
    XaEffectsFree(Effects);
    XaParameterFree(&Filter);
    delete[] ChannelVolumes;
    delete[] InputMix;
    delete[] FilterState;
    XaResamplerFree(Resampler);
    XaScratchFree(&InputScratch);
    XaScratchFree(&OutputScratch);
}

UINT32
XaVoice::OutputChannels(VOID) const
{
    if (Effects != nullptr)
        return Effects->OutputChannels;
    return InputChannels;
}

DWORD
XaVoice::OutputMask(VOID) const
{
    if (Effects != nullptr)
        return Effects->OutputMask;
    return InputMask;
}

/* The level each source channel enters the sends at */
float
XaVoice::SourceLevel(
    _In_ UINT32 Channel) const
{
    if (Kind == XaKindSource)
        return ChannelVolumes[Channel] * Volume;
    return ChannelVolumes[Channel];
}

VOID
XaVoice::ApplySendLevels(
    _Inout_ XaSend* Send,
    _In_ UINT32 OperationSet)
{
    UINT32 Inputs = OutputChannels();
    UINT32 Outputs = Send->Target->InputChannels;
    float* Block = reinterpret_cast<float*>(Send->Mix.Pending);

    /* The block holds the send filter first, then the scaled matrix */
    memcpy(Block, Send->Mix.Current, sizeof(XAUDIO2_FILTER_PARAMETERS));
    if (Send->Mix.State != XaSlotIdle)
        memcpy(Block, Send->Mix.Pending, sizeof(XAUDIO2_FILTER_PARAMETERS));
    for (UINT32 Output = 0; Output < Outputs; Output++)
    {
        for (UINT32 Input = 0; Input < Inputs; Input++)
        {
            UINT32 Cell = Output * Inputs + Input;

            Block[3 + Cell] = Send->Levels[Cell] * SourceLevel(Input) * Send->Target->Volume;
        }
    }
    Send->Mix.OperationSet = OperationSet;
    Send->Mix.State = OperationSet == XA_OPSET_NOW ? XaSlotReady : XaSlotWaiting;
}

BOOLEAN
XaVoice::ValidateSends(
    _In_ const XAUDIO2_VOICE_SENDS* SendList)
{
    BOOLEAN Valid = TRUE;
    UINT32 Rate = 0;

    if ((SendList->SendCount != 0) != (SendList->pSends != nullptr))
    {
        XA_TRACE_ONCE(L"sends: SendCount %u does not agree with pSends", SendList->SendCount);
        return FALSE;
    }
    for (UINT32 Index = 0; Index < SendList->SendCount && Valid; Index++)
    {
        const XAUDIO2_SEND_DESCRIPTOR* Send = &SendList->pSends[Index];
        XaVoice* Target;

        if ((Send->Flags & ~XAUDIO2_SEND_USEFILTER) != 0)
        {
            XA_TRACE_ONCE(L"sends: flags %08X on send %u", Send->Flags, Index);
            Valid = FALSE;
        }
        for (UINT32 Earlier = 0; Earlier < Index; Earlier++)
        {
            if (SendList->pSends[Earlier].pOutputVoice == Send->pOutputVoice)
            {
                XA_TRACE_ONCE(L"sends: one voice named twice");
                Valid = FALSE;
            }
        }
        Target = Engine->FindVoice(Send->pOutputVoice);
        if (Target == nullptr)
        {
            XA_TRACE_ONCE(L"sends: send %u has no valid output voice", Index);
            return FALSE;
        }
        if (Target->Kind == XaKindSource)
        {
            XA_TRACE_ONCE(L"sends: a source voice cannot take a send");
            Valid = FALSE;
        }
        if (Kind == XaKindSubmix && Target->Kind == XaKindSubmix && ProcessingStage >= Target->ProcessingStage)
        {
            XA_TRACE_ONCE(L"sends: stage %u cannot send to stage %u", ProcessingStage, Target->ProcessingStage);
            Valid = FALSE;
        }
        if (Index == 0)
        {
            Rate = Target->InputRate;
        }
        else if (Target->InputRate != Rate)
        {
            XA_TRACE_ONCE(L"sends: outputs run at different rates");
            return FALSE;
        }
    }
    return Valid;
}

VOID
XaVoice::ReleaseSends(VOID)
{
    while (Sends != nullptr)
    {
        XaSend* Send = Sends;

        Sends = Send->Next;
        Send->Target->SenderCount -= 1;
        delete[] Send->Levels;
        XaParameterFree(&Send->Mix);
        delete[] Send->MixState;
        delete Send;
    }
    SendCount = 0;
}

HRESULT
XaVoice::BuildSends(
    _In_ const XAUDIO2_VOICE_SENDS* SendList)
{
    XaSend** Link = &Sends;
    UINT32 Inputs = OutputChannels();

    for (UINT32 Index = 0; Index < SendList->SendCount; Index++)
    {
        XaVoice* Target = Engine->FindVoice(SendList->pSends[Index].pOutputVoice);
        UINT32 Outputs = Target->InputChannels;
        UINT32 BlockSize = sizeof(XAUDIO2_FILTER_PARAMETERS) + sizeof(float) * Inputs * Outputs;
        XaSend* Send = new (std::nothrow) XaSend;
        BYTE* Initial;
        HRESULT hr;

        if (Send == nullptr)
            return E_OUTOFMEMORY;
        memset(Send, 0, sizeof(*Send));
        Send->Target = Target;
        Send->Flags = SendList->pSends[Index].Flags;
        Send->Levels = new (std::nothrow) float[Inputs * Outputs];
        Initial = new (std::nothrow) BYTE[BlockSize];
        if (Send->Levels == nullptr || Initial == nullptr)
        {
            delete[] Send->Levels;
            delete[] Initial;
            delete Send;
            return E_OUTOFMEMORY;
        }
        hr = XaDefaultMatrix(Inputs, Outputs, OutputMask(), Target->InputMask, Send->Levels);
        if (FAILED(hr))
        {
            delete[] Send->Levels;
            delete[] Initial;
            delete Send;
            return hr;
        }
        memcpy(Initial, &XaDefaultFilter, sizeof(XaDefaultFilter));
        memset(Initial + sizeof(XaDefaultFilter), 0, BlockSize - sizeof(XaDefaultFilter));
        if (!XaParameterInit(&Send->Mix, BlockSize, Initial))
        {
            delete[] Send->Levels;
            delete[] Initial;
            delete Send;
            return E_OUTOFMEMORY;
        }
        delete[] Initial;
        Target->SenderCount += 1;
        *Link = Send;
        Link = &Send->Next;
        SendCount += 1;
        ApplySendLevels(Send, XA_OPSET_NOW);
        XaParameterTake(&Send->Mix);
    }
    return S_OK;
}

XaSend*
XaVoice::FindSend(
    _In_opt_ IXAudio2Voice* Destination)
{
    if (Destination == nullptr)
        return SendCount == 1 ? Sends : nullptr;
    for (XaSend* Send = Sends; Send != nullptr; Send = Send->Next)
    {
        if (Send->Target->Face() == Destination)
            return Send;
    }
    return nullptr;
}

/* The rate a send list asks the voice to produce */
UINT32
XaVoice::TargetRate(
    _In_ const XAUDIO2_VOICE_SENDS* SendList)
{
    if (SendList->SendCount != 0)
        return Engine->FindVoice(SendList->pSends[0].pOutputVoice)->InputRate;
    return Kind == XaKindSource ? OutputRate : InputRate;
}

HRESULT
XaVoice::EnsureInputMix(VOID)
{
    UINT32 Frames = XaQuantumFrames(InputRate, Engine);

    if (InputMix != nullptr && InputMixFrames == Frames)
        return S_OK;
    delete[] InputMix;
    InputMix = new (std::nothrow) float[static_cast<SIZE_T>(Frames) * InputChannels];
    InputMixFrames = InputMix != nullptr ? Frames : 0;
    if (InputMix == nullptr)
        return E_OUTOFMEMORY;
    memset(InputMix, 0, sizeof(*InputMix) * Frames * InputChannels);
    return S_OK;
}

HRESULT
XaVoice::InitializeCommon(
    _In_ UINT32 Flags,
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    HRESULT hr;

    if (!XaEffectsValidate(Engine, Chain, InputChannels))
    {
        XA_TRACE_ONCE(L"voice: the effect chain is not valid");
        return XAUDIO2_E_INVALID_CALL;
    }
    CreationFlags = Flags;
    ActiveFlags = Flags;
    if ((Flags & XAUDIO2_VOICE_USEFILTER) != 0 && !XaParameterInit(&Filter, sizeof(XaDefaultFilter), &XaDefaultFilter))
        return E_OUTOFMEMORY;
    if (Chain != nullptr)
    {
        hr = XaEffectsCreate(this, Chain, &Effects);
        if (FAILED(hr))
            return hr;
    }
    ChannelVolumes = new (std::nothrow) float[OutputChannels()];
    if (ChannelVolumes == nullptr)
        return E_OUTOFMEMORY;
    for (UINT32 Channel = 0; Channel < OutputChannels(); Channel++)
        ChannelVolumes[Channel] = 1.0f;
    if (SendList != nullptr)
    {
        hr = SetOutputRate(TargetRate(SendList));
        if (FAILED(hr))
            return hr;
        hr = BuildSends(SendList);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/* Changes the rate the voice produces, which only a voice with a converter can do */
HRESULT
XaVoice::SetOutputRate(
    _In_ UINT32 Rate)
{
    if (Rate == OutputRate)
        return S_OK;
    if (Kind == XaKindSource && (CreationFlags & XAUDIO2_VOICE_NOSRC) != 0)
    {
        XA_TRACE_ONCE(L"voice: a NOSRC voice cannot change to %u Hz", Rate);
        return XAUDIO2_E_INVALID_CALL;
    }
    OutputRate = Rate;
    return S_OK;
}

VOID
XaVoice::GetVoiceDetailsCommon(
    _Out_ XAUDIO2_VOICE_DETAILS* Details)
{
    Engine->Lock();
    if (Details == nullptr)
    {
        XA_TRACE_ONCE(L"GetVoiceDetails: NULL details");
        Engine->Unlock();
        return;
    }
    Details->CreationFlags = CreationFlags;
    Details->ActiveFlags = ActiveFlags;
    Details->InputChannels = InputChannels;
    Details->InputSampleRate = InputRate;
    Engine->Unlock();
}

HRESULT
XaVoice::SetOutputVoicesCommon(
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList)
{
    XAUDIO2_SEND_DESCRIPTOR Default;
    XAUDIO2_VOICE_SENDS DefaultList;
    HRESULT hr;

    Engine->Lock();
    if (Engine->InCallback() || Kind == XaKindMaster)
    {
        XA_TRACE_ONCE(L"SetOutputVoices: not allowed here");
        Engine->Unlock();
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
    {
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    ReleaseSends();
    hr = SetOutputRate(TargetRate(SendList));
    if (SUCCEEDED(hr))
        hr = BuildSends(SendList);
    Engine->Unlock();
    return hr;
}

HRESULT
XaVoice::SetEffectChainCommon(
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    XaEffectChain* Replacement = nullptr;
    HRESULT hr;

    Engine->Lock();
    if (Engine->InCallback())
    {
        XA_TRACE_ONCE(L"SetEffectChain: not allowed in a callback");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (!XaEffectsValidate(Engine, Chain, InputChannels))
    {
        XA_TRACE_ONCE(L"SetEffectChain: the chain is not valid");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (Chain == nullptr)
    {
        if (OutputChannels() != InputChannels)
        {
            XA_TRACE_ONCE(L"SetEffectChain: removing the chain would change the channel count");
            Engine->Unlock();
            return XAUDIO2_E_INVALID_CALL;
        }
    }
    else
    {
        hr = XaEffectsCreate(this, Chain, &Replacement);
        if (FAILED(hr))
        {
            Engine->Unlock();
            return hr;
        }
        if (Replacement->OutputChannels != OutputChannels())
        {
            XA_TRACE_ONCE(L"SetEffectChain: the chain would change the channel count");
            XaEffectsFree(Replacement);
            Engine->Unlock();
            return XAUDIO2_E_INVALID_CALL;
        }
    }
    XaEffectsFree(Effects);
    Effects = Replacement;
    Engine->Unlock();
    return S_OK;
}

HRESULT
XaVoice::EnableEffectCommon(
    _In_ UINT32 Index,
    _In_ BOOLEAN Enable,
    _In_ UINT32 OperationSet)
{
    HRESULT hr = XAUDIO2_E_INVALID_CALL;

    Engine->Lock();
    if (Effects == nullptr || Index >= Effects->Count)
        XA_TRACE_ONCE(L"EnableEffect: no effect %u", Index);
    else if (OperationSet == XAUDIO2_INVALID_OPSET)
        XA_TRACE_ONCE(L"EnableEffect: invalid operation set");
    else
        hr = XaEffectsEnable(Effects, Index, Enable, OperationSet);
    Engine->Unlock();
    return hr;
}

VOID
XaVoice::GetEffectStateCommon(
    _In_ UINT32 Index,
    _Out_ BOOL* Enabled)
{
    Engine->Lock();
    if (Effects == nullptr || Index >= Effects->Count || Enabled == nullptr)
        XA_TRACE_ONCE(L"GetEffectState: no effect %u", Index);
    else
        *Enabled = XaEffectsEnabled(Effects, Index);
    Engine->Unlock();
}

HRESULT
XaVoice::SetEffectParametersCommon(
    _In_ UINT32 Index,
    _In_reads_bytes_(Size) const void* Parameters,
    _In_ UINT32 Size,
    _In_ UINT32 OperationSet)
{
    HRESULT hr = XAUDIO2_E_INVALID_CALL;

    Engine->Lock();
    if (Effects == nullptr || Index >= Effects->Count)
        XA_TRACE_ONCE(L"SetEffectParameters: no effect %u", Index);
    else if (Parameters == nullptr || Size == 0)
        XA_TRACE_ONCE(L"SetEffectParameters: no parameters");
    else if (OperationSet == XAUDIO2_INVALID_OPSET)
        XA_TRACE_ONCE(L"SetEffectParameters: invalid operation set");
    else
        hr = XaEffectsSetParameters(Effects, Index, Parameters, Size, OperationSet);
    Engine->Unlock();
    return hr;
}

HRESULT
XaVoice::GetEffectParametersCommon(
    _In_ UINT32 Index,
    _Out_writes_bytes_(Size) void* Parameters,
    _In_ UINT32 Size)
{
    HRESULT hr = XAUDIO2_E_INVALID_CALL;

    Engine->Lock();
    if (Effects == nullptr || Index >= Effects->Count)
        XA_TRACE_ONCE(L"GetEffectParameters: no effect %u", Index);
    else if (Parameters == nullptr || Size == 0)
        XA_TRACE_ONCE(L"GetEffectParameters: no room");
    else
        hr = XaEffectsGetParameters(Effects, Index, Parameters, Size);
    Engine->Unlock();
    return hr;
}

HRESULT
XaVoice::SetFilterParametersCommon(
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
    _In_ UINT32 OperationSet)
{
    HRESULT hr = XAUDIO2_E_INVALID_CALL;

    Engine->Lock();
    if (Kind == XaKindMaster)
        XA_TRACE_ONCE(L"SetFilterParameters: not for a mastering voice");
    else if (Parameters == nullptr)
        XA_TRACE_ONCE(L"SetFilterParameters: NULL parameters");
    else if (!XaFilterValid(Parameters))
        XA_TRACE_ONCE(L"SetFilterParameters: parameters out of range");
    else if (OperationSet == XAUDIO2_INVALID_OPSET)
        XA_TRACE_ONCE(L"SetFilterParameters: invalid operation set");
    else if ((CreationFlags & XAUDIO2_VOICE_USEFILTER) == 0)
        XA_TRACE_ONCE(L"SetFilterParameters: the voice has no filter");
    else
    {
        XaParameterSet(&Filter, Parameters, OperationSet);
        hr = S_OK;
    }
    Engine->Unlock();
    return hr;
}

VOID
XaVoice::GetFilterParametersCommon(
    _Out_ XAUDIO2_FILTER_PARAMETERS* Parameters)
{
    Engine->Lock();
    if (Kind == XaKindMaster || Parameters == nullptr || (CreationFlags & XAUDIO2_VOICE_USEFILTER) == 0)
        XA_TRACE_ONCE(L"GetFilterParameters: no filter to read");
    else
        memcpy(Parameters, Filter.Current, sizeof(*Parameters));
    Engine->Unlock();
}

HRESULT
XaVoice::SetOutputFilterParametersCommon(
    _In_opt_ IXAudio2Voice* Destination,
    _In_ const XAUDIO2_FILTER_PARAMETERS* Parameters,
    _In_ UINT32 OperationSet)
{
    HRESULT hr = XAUDIO2_E_INVALID_CALL;
    XaSend* Send;

    Engine->Lock();
    if (Kind == XaKindMaster)
    {
        XA_TRACE_ONCE(L"SetOutputFilterParameters: not for a mastering voice");
    }
    else if (Parameters == nullptr)
    {
        XA_TRACE_ONCE(L"SetOutputFilterParameters: NULL parameters");
    }
    else if (!XaFilterValid(Parameters))
    {
        XA_TRACE_ONCE(L"SetOutputFilterParameters: parameters out of range");
    }
    else if (OperationSet == XAUDIO2_INVALID_OPSET)
    {
        XA_TRACE_ONCE(L"SetOutputFilterParameters: invalid operation set");
    }
    else
    {
        Send = FindSend(Destination);
        if (Send == nullptr)
        {
            XA_TRACE_ONCE(L"SetOutputFilterParameters: no such send");
        }
        else if ((Send->Flags & XAUDIO2_SEND_USEFILTER) == 0)
        {
            XA_TRACE_ONCE(L"SetOutputFilterParameters: the send has no filter");
        }
        else
        {
            if (Send->Mix.State == XaSlotIdle)
                memcpy(Send->Mix.Pending, Send->Mix.Current, Send->Mix.Size);
            memcpy(Send->Mix.Pending, Parameters, sizeof(*Parameters));
            Send->Mix.OperationSet = OperationSet;
            Send->Mix.State = OperationSet == XA_OPSET_NOW ? XaSlotReady : XaSlotWaiting;
            hr = S_OK;
        }
    }
    Engine->Unlock();
    return hr;
}

VOID
XaVoice::GetOutputFilterParametersCommon(
    _In_opt_ IXAudio2Voice* Destination,
    _Out_ XAUDIO2_FILTER_PARAMETERS* Parameters)
{
    XaSend* Send;

    Engine->Lock();
    Send = Kind == XaKindMaster || Parameters == nullptr ? nullptr : FindSend(Destination);
    if (Send == nullptr || (Send->Flags & XAUDIO2_SEND_USEFILTER) == 0)
        XA_TRACE_ONCE(L"GetOutputFilterParameters: no send filter to read");
    else
        memcpy(Parameters, Send->Mix.Current, sizeof(*Parameters));
    Engine->Unlock();
}

HRESULT
XaVoice::SetVolumeCommon(
    _In_ float NewVolume,
    _In_ UINT32 OperationSet)
{
    Engine->Lock();
    if (OperationSet == XAUDIO2_INVALID_OPSET || !(NewVolume >= -XA_VOLUME_LIMIT && NewVolume <= XA_VOLUME_LIMIT))
    {
        XA_TRACE_ONCE(L"SetVolume: invalid volume or operation set");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (NewVolume != Volume)
    {
        Volume = NewVolume;
        if (Kind == XaKindSource)
        {
            for (XaSend* Send = Sends; Send != nullptr; Send = Send->Next)
                ApplySendLevels(Send, OperationSet);
        }
        else
        {
            /* A submix or mastering voice applies its volume where audio enters it */
            for (XaVoice* Sender = Engine->Voices; Sender != nullptr; Sender = Sender->NextVoice)
            {
                for (XaSend* Send = Sender->Sends; Send != nullptr; Send = Send->Next)
                {
                    if (Send->Target == this)
                        Sender->ApplySendLevels(Send, OperationSet);
                }
            }
        }
    }
    Engine->Unlock();
    return S_OK;
}

VOID
XaVoice::GetVolumeCommon(
    _Out_ float* Result)
{
    Engine->Lock();
    if (Result == nullptr)
        XA_TRACE_ONCE(L"GetVolume: NULL volume");
    else
        *Result = Volume;
    Engine->Unlock();
}

HRESULT
XaVoice::SetChannelVolumesCommon(
    _In_ UINT32 Channels,
    _In_reads_(Channels) const float* Volumes,
    _In_ UINT32 OperationSet)
{
    Engine->Lock();
    if (Kind == XaKindMaster || OperationSet == XAUDIO2_INVALID_OPSET || Volumes == nullptr || Channels != OutputChannels())
    {
        XA_TRACE_ONCE(L"SetChannelVolumes: invalid call");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    if (Kind != XaKindSource || memcmp(Volumes, ChannelVolumes, sizeof(*Volumes) * Channels) != 0)
    {
        memcpy(ChannelVolumes, Volumes, sizeof(*Volumes) * Channels);
        for (XaSend* Send = Sends; Send != nullptr; Send = Send->Next)
            ApplySendLevels(Send, OperationSet);
    }
    Engine->Unlock();
    return S_OK;
}

VOID
XaVoice::GetChannelVolumesCommon(
    _In_ UINT32 Channels,
    _Out_writes_(Channels) float* Volumes)
{
    Engine->Lock();
    if (Kind == XaKindMaster || Volumes == nullptr || Channels != OutputChannels())
        XA_TRACE_ONCE(L"GetChannelVolumes: invalid call");
    else
        memcpy(Volumes, ChannelVolumes, sizeof(*Volumes) * Channels);
    Engine->Unlock();
}

HRESULT
XaVoice::SetOutputMatrixCommon(
    _In_opt_ IXAudio2Voice* Destination,
    _In_ UINT32 SourceChannels,
    _In_ UINT32 DestinationChannels,
    _In_reads_(SourceChannels * DestinationChannels) const float* Matrix,
    _In_ UINT32 OperationSet)
{
    XaSend* Send;
    UINT32 Outputs;

    Engine->Lock();
    if (Kind == XaKindMaster || OperationSet == XAUDIO2_INVALID_OPSET || Matrix == nullptr ||
        SourceChannels != OutputChannels())
    {
        XA_TRACE_ONCE(L"SetOutputMatrix: invalid call");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    Send = FindSend(Destination);
    if (Send == nullptr)
    {
        XA_TRACE_ONCE(L"SetOutputMatrix: no such send");
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }
    Outputs = Send->Target->InputChannels;
    if (DestinationChannels > Outputs)
    {
        XA_TRACE_ONCE(L"SetOutputMatrix: %u destination channels, the voice has %u", DestinationChannels, Outputs);
        Engine->Unlock();
        return XAUDIO2_E_INVALID_CALL;
    }

    /* A short matrix replaces the leading levels and leaves the rest */
    if (DestinationChannels < Outputs)
        XA_TRACE_ONCE(L"SetOutputMatrix: matrix is %ux%u, the send is %ux%u", SourceChannels, DestinationChannels,
                      SourceChannels, Outputs);
    if (memcmp(Send->Levels, Matrix, sizeof(*Matrix) * SourceChannels * DestinationChannels) != 0)
    {
        memcpy(Send->Levels, Matrix, sizeof(*Matrix) * SourceChannels * DestinationChannels);
        ApplySendLevels(Send, OperationSet);
    }
    Engine->Unlock();
    return S_OK;
}

VOID
XaVoice::GetOutputMatrixCommon(
    _In_opt_ IXAudio2Voice* Destination,
    _In_ UINT32 SourceChannels,
    _In_ UINT32 DestinationChannels,
    _Out_writes_(SourceChannels * DestinationChannels) float* Matrix)
{
    XaSend* Send;

    Engine->Lock();
    Send = Kind == XaKindMaster || Matrix == nullptr || SourceChannels != OutputChannels() ? nullptr : FindSend(Destination);
    if (Send == nullptr || DestinationChannels != Send->Target->InputChannels)
        XA_TRACE_ONCE(L"GetOutputMatrix: invalid call");
    else
        memcpy(Matrix, Send->Levels, sizeof(*Matrix) * SourceChannels * DestinationChannels);
    Engine->Unlock();
}

VOID
XaVoice::DestroyVoiceCommon(VOID)
{
    Engine->Lock();
    if (Engine->InCallback())
    {
        XA_TRACE_ONCE(L"DestroyVoice: not allowed in a callback");
        Engine->Unlock();
        return;
    }
    if (SenderCount != 0)
    {
        XA_TRACE_ONCE(L"DestroyVoice: %u voices still send to this one", SenderCount);
        Engine->Unlock();
        return;
    }
    Destroyed = TRUE;
    Engine->UnlinkVoice(this);
    Engine->Unlock();

    /* The render thread may be waiting on the lock, so it stops without it held */
    Teardown();

    Engine->Lock();
    ReleaseSends();
    Engine->Unlock();
    delete this;
}

VOID
XaVoice::Teardown(VOID)
{
}
