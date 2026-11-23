/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wma.hpp"

struct XaWmaDecoder
{
    XaWmaPull Pull;
    void* Context;
    UINT32 PacketBytes;
    UINT32 Channels;
};

HRESULT
XaWmaCreate(
    _In_ const WAVEFORMATEX* Format,
    _In_ XaWmaPull Pull,
    _In_ void* Context,
    _Outptr_ XaWmaDecoder** Decoder)
{
    XaWmaDecoder* Created = new (std::nothrow) XaWmaDecoder;

    *Decoder = nullptr;
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    memset(Created, 0, sizeof(*Created));
    Created->Pull = Pull;
    Created->Context = Context;
    Created->PacketBytes = Format->nBlockAlign;
    Created->Channels = Format->nChannels;
    *Decoder = Created;
    return S_OK;
}

VOID
XaWmaFree(
    _In_opt_ XaWmaDecoder* Decoder)
{
    delete Decoder;
}

UINT32
XaWmaPacketBytes(
    _In_ const XaWmaDecoder* Decoder)
{
    return Decoder->PacketBytes;
}

UINT32
XaWmaMaxFrames(
    _In_ const XaWmaDecoder* Decoder)
{
    UNREFERENCED_PARAMETER(Decoder);
    return 0;
}

VOID
XaWmaReset(
    _Inout_ XaWmaDecoder* Decoder)
{
    UNREFERENCED_PARAMETER(Decoder);
}

HRESULT
XaWmaDecode(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ UINT32* Frames,
    _Out_ XaWmaState* State)
{
    XaWmaInput Input;

    do
    {
        Decoder->Pull(Decoder->Context, &Input);
    } while (!Input.Last);
    *Frames = 0;
    *State = XaWmaIdle;
    return S_OK;
}

HRESULT
XaWmaReadPcm(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Frames,
    _Out_writes_(_Inexpressible_("Frames times channels")) SHORT* Output,
    _Out_ UINT32* Produced,
    _Out_ XaWmaState* State)
{
    UNREFERENCED_PARAMETER(Decoder);
    UNREFERENCED_PARAMETER(Frames);
    UNREFERENCED_PARAMETER(Output);
    *Produced = 0;
    *State = XaWmaWantsDecode;
    return S_OK;
}
