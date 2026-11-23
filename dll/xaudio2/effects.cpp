/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Effect chains: hosting application and built in XAPOs on a voice
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

struct XaEffect
{
    IXAPO* Apo;
    IXAPOParameters* Parameters;
    UINT32 InputChannels;
    UINT32 OutputChannels;
    DWORD InputMask;
    DWORD OutputMask;
    UINT32 Flags;
    BOOLEAN InPlace;
    BOOLEAN Locked;

    /* What the mixer runs the effect with, and the changes waiting for it */
    BOOLEAN Enabled;
    BOOLEAN PendingEnabled;
    UINT32 EnableSet;
    XaSlotState EnableState;
    BYTE* PendingParameters;
    UINT32 PendingSize;
    UINT32 ParameterSet;
    XaSlotState ParameterState;

    float* Output;
};

BOOLEAN
XaEffectsValidate(
    _In_ XaEngine* Engine,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain,
    _In_ UINT32 InputChannels)
{
    BOOLEAN Valid = TRUE;
    UINT32 Channels = InputChannels;

    UNREFERENCED_PARAMETER(Engine);
    if (Chain == nullptr)
        return TRUE;
    if (Chain->EffectCount == 0 || Chain->pEffectDescriptors == nullptr)
    {
        XA_TRACE_ONCE(L"effects: empty chain");
        return FALSE;
    }
    for (UINT32 Index = 0; Index < Chain->EffectCount && Valid; Index++)
    {
        const XAUDIO2_EFFECT_DESCRIPTOR* Descriptor = &Chain->pEffectDescriptors[Index];
        XAPO_REGISTRATION_PROPERTIES* Properties = nullptr;
        IXAPO* Apo = nullptr;

        if (Descriptor->pEffect == nullptr || *reinterpret_cast<void**>(Descriptor->pEffect) == nullptr)
        {
            XA_TRACE_ONCE(L"effects: descriptor %u has no effect", Index);
            return FALSE;
        }
        if (Descriptor->OutputChannels == 0 || Descriptor->OutputChannels > XA_MAX_CHANNELS)
        {
            XA_TRACE_ONCE(L"effects: descriptor %u wants %u channels", Index, Descriptor->OutputChannels);
            return FALSE;
        }
        if (FAILED(Descriptor->pEffect->QueryInterface(__uuidof(IXAPO), reinterpret_cast<void**>(&Apo))))
        {
            XA_TRACE_ONCE(L"effects: descriptor %u is not an XAPO", Index);
            return FALSE;
        }
        if (FAILED(Apo->GetRegistrationProperties(&Properties)))
        {
            XA_TRACE_ONCE(L"effects: descriptor %u has no registration properties", Index);
            Apo->Release();
            return FALSE;
        }
        if ((Properties->Flags & XAPO_FLAG_INPLACE_REQUIRED) != 0 && (Properties->Flags & XAPO_FLAG_INPLACE_SUPPORTED) == 0)
            Valid = FALSE;
        if (Properties->MinInputBufferCount > 1 || Properties->MaxInputBufferCount == 0)
            Valid = FALSE;
        if (Properties->MinOutputBufferCount > 1 || Properties->MaxOutputBufferCount == 0)
            Valid = FALSE;
        if ((Properties->Flags & XAPO_FLAG_INPLACE_REQUIRED) != 0 && Channels != Descriptor->OutputChannels)
            Valid = FALSE;
        if (!Valid)
            XA_TRACE_ONCE(L"effects: descriptor %u cannot run here", Index);
        CoTaskMemFree(Properties);
        Apo->Release();
        Channels = Descriptor->OutputChannels;
    }
    return Valid;
}

static
VOID
XaFloatFormat(
    _Out_ WAVEFORMATEXTENSIBLE* Format,
    _In_ UINT32 Channels,
    _In_ UINT32 Rate,
    _In_ DWORD Mask)
{
    memset(Format, 0, sizeof(*Format));
    Format->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    Format->Format.nChannels = static_cast<WORD>(Channels);
    Format->Format.nSamplesPerSec = Rate;
    Format->Format.wBitsPerSample = 32;
    Format->Format.nBlockAlign = static_cast<WORD>(Channels * sizeof(float));
    Format->Format.nAvgBytesPerSec = Rate * Format->Format.nBlockAlign;
    Format->Format.cbSize = sizeof(*Format) - sizeof(Format->Format);
    Format->Samples.wValidBitsPerSample = 32;
    Format->dwChannelMask = Mask;
    Format->SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
}

/* Locks every effect for the rate and quantum the voice processes at */
static
HRESULT
XaEffectsLock(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Rate,
    _In_ UINT32 MaxFrames)
{
    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];
        WAVEFORMATEXTENSIBLE InputFormat;
        WAVEFORMATEXTENSIBLE OutputFormat;
        XAPO_LOCKFORPROCESS_PARAMETERS Input;
        XAPO_LOCKFORPROCESS_PARAMETERS Output;
        HRESULT hr;

        XaFloatFormat(&InputFormat, Entry->InputChannels, Rate, Entry->InputMask);
        XaFloatFormat(&OutputFormat, Entry->OutputChannels, Rate, Entry->OutputMask);
        Input.pFormat = &InputFormat.Format;
        Input.MaxFrameCount = MaxFrames;
        Output.pFormat = &OutputFormat.Format;
        Output.MaxFrameCount = MaxFrames;
        hr = Entry->Apo->LockForProcess(1, &Input, 1, &Output);
        if (FAILED(hr))
        {
            XaTrace(L"effects: effect %u refused to lock (%08lX)", Index, hr);
            return XAUDIO2_E_XAPO_CREATION_FAILED;
        }
        Entry->Locked = TRUE;
        if (!Entry->InPlace)
        {
            Entry->Output = new (std::nothrow) float[static_cast<SIZE_T>(MaxFrames) * Entry->OutputChannels];
            if (Entry->Output == nullptr)
                return E_OUTOFMEMORY;
        }
    }
    Chain->Rate = Rate;
    Chain->MaxFrames = MaxFrames;
    return S_OK;
}

HRESULT
XaEffectsCreate(
    _In_ XaVoice* Voice,
    _In_ const XAUDIO2_EFFECT_CHAIN* Chain,
    _Outptr_ XaEffectChain** Result)
{
    XaEffectChain* Effects;
    UINT32 Channels = Voice->InputChannels;
    DWORD Mask = Voice->InputMask;
    UINT32 Rate = Voice->Kind == XaKindSource ? Voice->OutputRate : Voice->InputRate;
    HRESULT hr;

    *Result = nullptr;
    Effects = new (std::nothrow) XaEffectChain;
    if (Effects == nullptr)
        return E_OUTOFMEMORY;
    memset(Effects, 0, sizeof(*Effects));
    Effects->Entries = new (std::nothrow) XaEffect[Chain->EffectCount];
    if (Effects->Entries == nullptr)
    {
        delete Effects;
        return E_OUTOFMEMORY;
    }
    memset(Effects->Entries, 0, sizeof(XaEffect) * Chain->EffectCount);
    Effects->Count = Chain->EffectCount;
    for (UINT32 Index = 0; Index < Chain->EffectCount; Index++)
    {
        const XAUDIO2_EFFECT_DESCRIPTOR* Descriptor = &Chain->pEffectDescriptors[Index];
        XaEffect* Entry = &Effects->Entries[Index];
        XAPO_REGISTRATION_PROPERTIES* Properties = nullptr;

        Descriptor->pEffect->QueryInterface(__uuidof(IXAPO), reinterpret_cast<void**>(&Entry->Apo));
        Descriptor->pEffect->QueryInterface(__uuidof(IXAPOParameters), reinterpret_cast<void**>(&Entry->Parameters));
        if (Entry->Apo != nullptr && SUCCEEDED(Entry->Apo->GetRegistrationProperties(&Properties)))
        {
            Entry->Flags = Properties->Flags;
            CoTaskMemFree(Properties);
        }
        Entry->InputChannels = Channels;
        Entry->OutputChannels = Descriptor->OutputChannels;
        Entry->InPlace = (Entry->Flags & XAPO_FLAG_INPLACE_SUPPORTED) != 0 && Channels == Descriptor->OutputChannels;

        /* The layout carries through an effect that works in place, otherwise it is left open */
        Entry->InputMask = Mask;
        Entry->OutputMask = Entry->InPlace ? Mask : 0;
        Entry->Enabled = Descriptor->InitialState != FALSE;
        Entry->PendingEnabled = Entry->Enabled;
        Channels = Descriptor->OutputChannels;
        Mask = Entry->OutputMask;
    }
    Effects->OutputChannels = Channels;
    Effects->OutputMask = Mask;
    hr = XaEffectsLock(Effects, Rate, XaQuantumFrames(Rate, Voice->Engine));
    if (FAILED(hr))
    {
        XaEffectsFree(Effects);
        return hr;
    }
    *Result = Effects;
    return S_OK;
}

/* Each effect asks for its properties again and locks for the new rate, enabled or not */
HRESULT
XaEffectsRelock(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Rate,
    _In_ UINT32 MaxFrames)
{
    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];
        XAPO_REGISTRATION_PROPERTIES* Properties = nullptr;

        if (SUCCEEDED(Entry->Apo->GetRegistrationProperties(&Properties)))
            CoTaskMemFree(Properties);
        if (Entry->Locked)
        {
            Entry->Apo->UnlockForProcess();
            Entry->Locked = FALSE;
        }
        delete[] Entry->Output;
        Entry->Output = nullptr;
    }
    return XaEffectsLock(Chain, Rate, MaxFrames);
}

VOID
XaEffectsFree(
    _In_opt_ XaEffectChain* Chain)
{
    if (Chain == nullptr)
        return;
    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];

        if (Entry->Locked)
            Entry->Apo->UnlockForProcess();
        if (Entry->Parameters != nullptr)
            Entry->Parameters->Release();
        if (Entry->Apo != nullptr)
            Entry->Apo->Release();
        delete[] Entry->Output;
        delete[] Entry->PendingParameters;
    }
    delete[] Chain->Entries;
    delete Chain;
}

HRESULT
XaEffectsEnable(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _In_ BOOLEAN Enable,
    _In_ UINT32 OperationSet)
{
    XaEffect* Entry = &Chain->Entries[Index];

    Entry->PendingEnabled = Enable;
    Entry->EnableSet = OperationSet;
    Entry->EnableState = OperationSet == XA_OPSET_NOW ? XaSlotReady : XaSlotWaiting;
    return S_OK;
}

BOOL
XaEffectsEnabled(
    _In_ XaEffectChain* Chain,
    _In_ UINT32 Index)
{
    return Chain->Entries[Index].Enabled;
}

VOID
XaEffectsCommit(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 OperationSet)
{
    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];

        if (Entry->EnableState == XaSlotWaiting && (OperationSet == XAUDIO2_COMMIT_ALL || OperationSet == Entry->EnableSet))
            Entry->EnableState = XaSlotReady;
        if (Entry->ParameterState == XaSlotWaiting && (OperationSet == XAUDIO2_COMMIT_ALL || OperationSet == Entry->ParameterSet))
            Entry->ParameterState = XaSlotReady;
    }
}

HRESULT
XaEffectsSetParameters(
    _Inout_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _In_reads_bytes_(Size) const void* Parameters,
    _In_ UINT32 Size,
    _In_ UINT32 OperationSet)
{
    XaEffect* Entry = &Chain->Entries[Index];

    if (Entry->Parameters == nullptr)
        return E_NOTIMPL;
    if (Entry->PendingSize < Size)
    {
        BYTE* Grown = new (std::nothrow) BYTE[Size];

        if (Grown == nullptr)
            return E_OUTOFMEMORY;
        delete[] Entry->PendingParameters;
        Entry->PendingParameters = Grown;
    }
    memcpy(Entry->PendingParameters, Parameters, Size);
    Entry->PendingSize = Size;
    Entry->ParameterSet = OperationSet;
    Entry->ParameterState = OperationSet == XA_OPSET_NOW ? XaSlotReady : XaSlotWaiting;
    return S_OK;
}

HRESULT
XaEffectsGetParameters(
    _In_ XaEffectChain* Chain,
    _In_ UINT32 Index,
    _Out_writes_bytes_(Size) void* Parameters,
    _In_ UINT32 Size)
{
    XaEffect* Entry = &Chain->Entries[Index];

    if (Entry->Parameters == nullptr)
        return E_NOTIMPL;
    Entry->Parameters->GetParameters(Parameters, Size);
    return S_OK;
}

/* Hands the effects the changes that are due, on the processing thread or with the engine idle */
VOID
XaEffectsApplyReady(
    _Inout_ XaEffectChain* Chain)
{
    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];

        if (Entry->ParameterState == XaSlotReady)
        {
            Entry->ParameterState = XaSlotIdle;
            Entry->Parameters->SetParameters(Entry->PendingParameters, Entry->PendingSize);
        }
        if (Entry->EnableState == XaSlotReady)
        {
            Entry->EnableState = XaSlotIdle;
            Entry->Enabled = Entry->PendingEnabled;
        }
    }
}

/* Runs the chain over one quantum and returns the buffer the last effect wrote */
float*
XaEffectsProcess(
    _Inout_ XaEffectChain* Chain,
    _Inout_ float* Buffer,
    _In_ UINT32 Frames,
    _Inout_ BOOLEAN* Silent)
{
    float* Current = Buffer;
    XAPO_BUFFER_FLAGS Flags = *Silent ? XAPO_BUFFER_SILENT : XAPO_BUFFER_VALID;

    for (UINT32 Index = 0; Index < Chain->Count; Index++)
    {
        XaEffect* Entry = &Chain->Entries[Index];
        XAPO_PROCESS_BUFFER_PARAMETERS Input;
        XAPO_PROCESS_BUFFER_PARAMETERS Output;

        Input.pBuffer = Current;
        Input.BufferFlags = Flags;
        Input.ValidFrameCount = Frames;
        Output.pBuffer = Entry->InPlace ? Current : Entry->Output;
        Output.BufferFlags = Flags;
        Output.ValidFrameCount = Frames;
        Entry->Apo->Process(1, &Input, 1, &Output, Entry->Enabled);
        Current = static_cast<float*>(Output.pBuffer);
        Flags = Output.BufferFlags;
    }
    *Silent = Flags == XAPO_BUFFER_SILENT;
    return Current;
}
