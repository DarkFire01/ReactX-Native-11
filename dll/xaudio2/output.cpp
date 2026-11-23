/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Submix and mastering voices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

HRESULT
XaSubmixVoice::Initialize(
    _In_ UINT32 Flags,
    _In_ UINT32 Channels,
    _In_ UINT32 Rate,
    _In_ UINT32 Stage,
    _In_opt_ const XAUDIO2_VOICE_SENDS* SendList,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    XAUDIO2_SEND_DESCRIPTOR Default;
    XAUDIO2_VOICE_SENDS DefaultList;

    InputChannels = Channels;
    InputRate = Rate;
    InputMask = XaPseudoMask(Channels);
    OutputRate = Rate;
    ProcessingStage = Stage;
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
    return InitializeCommon(Flags, SendList, Chain);
}

/* A submix voice counts as active for as long as it exists */
VOID
XaSubmixVoice::CountActive(
    _In_ BOOLEAN Active)
{
    UINT32 Resamplers = Active && OutputRate != InputRate ? 1 : 0;
    UINT32 Matrices = Active ? SendCount : 0;

    if (Active != Counted)
    {
        if (Active)
            Engine->ActiveSubmixes += 1;
        else
            Engine->ActiveSubmixes -= 1;
    }
    Engine->ActiveResamplers = Engine->ActiveResamplers - CountedResamplers + Resamplers;
    Engine->ActiveMatrices = Engine->ActiveMatrices - CountedMatrices + Matrices;
    CountedResamplers = Resamplers;
    CountedMatrices = Matrices;
    Counted = Active;
}

VOID
XaSubmixVoice::Teardown()
{
    Engine->Lock();
    CountActive(FALSE);
    Engine->Unlock();
}

HRESULT
XaMasterVoice::Initialize(
    _In_ UINT32 Flags,
    _In_ UINT32 Channels,
    _In_ UINT32 Rate,
    _In_opt_ const XAUDIO2_EFFECT_CHAIN* Chain)
{
    InputChannels = Channels;
    InputRate = Rate;
    InputMask = XaPseudoMask(Channels);
    OutputRate = Rate;
    return InitializeCommon(Flags, nullptr, Chain);
}

HRESULT
XaMasterVoice::OpenEndpoint(
    _In_z_ PCWSTR DeviceId,
    _In_ AUDIO_STREAM_CATEGORY Category)
{
    UINT32 Channels = OutputChannels();
    DWORD Mask = OutputMask();
    HRESULT hr;

    Endpoint = new (std::nothrow) XaEndpoint;
    if (Endpoint == nullptr)
        return E_OUTOFMEMORY;

    /* The stream takes the endpoint's own layout unless the voice asked to keep its channels */
    if ((CreationFlags & XA_MASTER_KEEP_CHANNELS) == 0)
    {
        UINT32 DeviceRate;

        hr = XaQueryEndpointFormat(DeviceId, Category, Channels, &Channels, &DeviceRate, &Mask);
        if (FAILED(hr))
            return hr;
    }
    hr = Endpoint->Open(DeviceId, Category, &Engine->Context, (CreationFlags & XA_MASTER_DEFAULT_DEVICE) != 0, Channels, Mask,
                        InputRate);
    if (FAILED(hr))
        return hr;
    return XaMixerPrepareMaster(this);
}

HRESULT
STDMETHODCALLTYPE
XaMasterVoice::GetChannelMask(
    _Out_ DWORD* ChannelMask)
{
    if (ChannelMask == nullptr)
    {
        XA_TRACE_ONCE(L"GetChannelMask: NULL mask");
        return XAUDIO2_E_INVALID_CALL;
    }
    *ChannelMask = InputMask;
    return S_OK;
}

VOID
XaMasterVoice::Teardown()
{
    Engine->StopRendering();
    if (Endpoint != nullptr)
    {
        Endpoint->Close();
        delete Endpoint;
        Endpoint = nullptr;
    }
    XaMixerReleaseMaster(this);
}
