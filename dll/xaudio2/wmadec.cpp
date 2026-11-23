/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"

/* What the decoder returns for a header it has no encoding for */
#define XA_WMA_E_FORMAT         static_cast<HRESULT>(0x80040000)

HRESULT
XaWmaCreate(
    _In_ const WAVEFORMATEX* Format,
    _In_ XaWmaPull Pull,
    _In_ void* Context,
    _Outptr_ XaWmaDecoder** Decoder)
{
    XaWmaDecoder* Created;
    XaWmaLayout Layout;

    *Decoder = nullptr;
    if (!XaWmaDescribe(Format, &Layout))
    {
        /* WMA Pro is not decoded yet, so it keeps the header's packet size meanwhile */
        if (XaFormatTag(Format) != XA_TAG_WMA3)
            return XA_WMA_E_FORMAT;
        Layout.Version = XA_WMA_PRO;
        Layout.Channels = Format->nChannels;
        Layout.Rate = Format->nSamplesPerSec;
        Layout.PacketBytes = Format->nBlockAlign;
    }
    Created = new (std::nothrow) XaWmaDecoder;
    if (Created == nullptr)
        return E_OUTOFMEMORY;
    memset(Created, 0, sizeof(*Created));
    Created->Layout = Layout;
    if (Layout.Version <= XA_WMA_STANDARD_MAX)
        XaWmaSetupStandard(&Created->Layout, &Created->Standard);
    Created->Pull = Pull;
    Created->Context = Context;
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
    return Decoder->Layout.PacketBytes;
}

UINT32
XaWmaMaxFrames(
    _In_ const XaWmaDecoder* Decoder)
{
    return Decoder->Layout.FrameSamples;
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
