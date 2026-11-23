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
    UINT32 Flags;
    BOOLEAN Enabled;
    BOOLEAN PendingEnabled;
    UINT32 EnableSet;
    BOOLEAN EnableWaiting;
    BOOLEAN Locked;
    float* Output;
    UINT32 OutputFrames;
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

HRESULT
XaEffectsCreate(
    _In_ XaVoice* Voice,
    _In_ const XAUDIO2_EFFECT_CHAIN* Chain,
    _Outptr_ XaEffectChain** Result)
{
    XaEffectChain* Effects;
    UINT32 Channels = Voice->InputChannels;

    *Result = nullptr;
    Effects = new (std::nothrow) XaEffectChain;
    if (Effects == nullptr)
        return E_OUTOFMEMORY;
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
        Entry->Enabled = Descriptor->InitialState != FALSE;
        Entry->PendingEnabled = Entry->Enabled;
        Channels = Descriptor->OutputChannels;
    }
    Effects->OutputChannels = Channels;
    Effects->OutputMask = XaPseudoMask(Channels);
    *Result = Effects;
    return S_OK;
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
    Entry->EnableWaiting = OperationSet != XA_OPSET_NOW;
    if (!Entry->EnableWaiting)
        Entry->Enabled = Enable;
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

        if (Entry->EnableWaiting && (OperationSet == XAUDIO2_COMMIT_ALL || OperationSet == Entry->EnableSet))
        {
            Entry->EnableWaiting = FALSE;
            Entry->Enabled = Entry->PendingEnabled;
        }
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

    UNREFERENCED_PARAMETER(OperationSet);
    if (Entry->Parameters == nullptr)
        return E_NOTIMPL;
    Entry->Parameters->SetParameters(Parameters, Size);
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
