/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA decoder interface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <mmreg.h>

/* One piece of the compressed stream handed to the decoder */
struct XaWmaInput
{
    const BYTE* Data;
    UINT32 Bytes;

    /* Nothing follows this piece, so the decoder drains what it holds */
    BOOLEAN Last;
};

/* Called whenever the decoder runs out of stream */
typedef
VOID
(*XaWmaPull)(
    _In_ void* Context,
    _Out_ XaWmaInput* Input);

/* What the decoder wants next */
enum XaWmaState
{
    XaWmaIdle,
    XaWmaFailed,
    XaWmaWantsDecode,
    XaWmaHasPcm,
};

struct XaWmaDecoder;

HRESULT
XaWmaCreate(
    _In_ const WAVEFORMATEX* Format,
    _In_ XaWmaPull Pull,
    _In_ void* Context,
    _Outptr_ XaWmaDecoder** Decoder);

VOID
XaWmaFree(
    _In_opt_ XaWmaDecoder* Decoder);

/* Packet size, in bytes, the stream is cut into */
UINT32
XaWmaPacketBytes(
    _In_ const XaWmaDecoder* Decoder);

/* Most frames a single decode step can produce */
UINT32
XaWmaMaxFrames(
    _In_ const XaWmaDecoder* Decoder);

/* Starts a new stream; the next step decodes */
VOID
XaWmaReset(
    _Inout_ XaWmaDecoder* Decoder);

/* Decodes the next frame, pulling stream as it goes */
HRESULT
XaWmaDecode(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ UINT32* Frames,
    _Out_ XaWmaState* State);

/* Hands out the 16 bit frames the last decode step produced */
HRESULT
XaWmaReadPcm(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Frames,
    _Out_writes_(_Inexpressible_("Frames times channels")) SHORT* Output,
    _Out_ UINT32* Produced,
    _Out_ XaWmaState* State);
