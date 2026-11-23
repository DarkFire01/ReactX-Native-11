/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Wave format checks and channel masks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"

/* Masks a bare channel count stands for */
static const DWORD XaPseudoMasks[8] =
{
    KSAUDIO_SPEAKER_MONO,
    KSAUDIO_SPEAKER_STEREO,
    KSAUDIO_SPEAKER_STEREO | SPEAKER_LOW_FREQUENCY,
    KSAUDIO_SPEAKER_QUAD,
    KSAUDIO_SPEAKER_QUAD | SPEAKER_LOW_FREQUENCY,
    KSAUDIO_SPEAKER_5POINT1_SURROUND,
    KSAUDIO_SPEAKER_5POINT1_SURROUND | SPEAKER_BACK_CENTER,
    KSAUDIO_SPEAKER_7POINT1_SURROUND,
};

/* The MS ADPCM coefficient pairs every valid header carries */
static const SHORT XaAdpcmCoefficients[7][2] =
{
    { 256, 0 }, { 512, -256 }, { 0, 0 }, { 192, 64 }, { 240, 0 }, { 460, -208 }, { 392, -232 },
};

static const GUID XaSubFormatBase = { 0x00000000, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };


WORD
XaFormatTag(
    _In_ const WAVEFORMATEX* Format)
{
    const WAVEFORMATEXTENSIBLE* Extensible;

    if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        return Format->wFormatTag;

    /* Only subformats built on the base GUID carry a tag */
    Extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);
    if (memcmp(&Extensible->SubFormat.Data2, &XaSubFormatBase.Data2, sizeof(GUID) - sizeof(ULONG)) != 0)
        return 0;
    return static_cast<WORD>(Extensible->SubFormat.Data1);
}

DWORD
XaPseudoMask(
    _In_ UINT32 Channels)
{
    if (Channels == 0 || Channels > ARRAYSIZE(XaPseudoMasks))
        return 0;
    return XaPseudoMasks[Channels - 1];
}

UINT32
XaMaskChannels(
    _In_ DWORD Mask)
{
    UINT32 Count = 0;

    for (; Mask != 0; Mask &= Mask - 1)
        Count += 1;
    return Count;
}

DWORD
XaFormatMask(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE || Format->wFormatTag == XA_TAG_XMA2)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->dwChannelMask;
    if (Format->nChannels == 1)
        return KSAUDIO_SPEAKER_MONO;
    if (Format->nChannels == 2)
        return KSAUDIO_SPEAKER_STEREO;
    return XaPseudoMask(Format->nChannels);
}

static
BOOLEAN
XaPcmValid(
    _In_ const WAVEFORMATEX* Format)
{
    BOOLEAN Valid;
    WORD Bits = Format->wBitsPerSample;

    Valid = Bits == 8 || Bits == 16 || Bits == 24 || Bits == 32;
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WORD ValidBits = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample;

        /* Only the common precisions, or left unset */
        if (ValidBits > Bits)
            Valid = FALSE;
        if (ValidBits != 0 && ValidBits != 8 && ValidBits != 16 && ValidBits != 20 && ValidBits != 24 && ValidBits != 32)
            Valid = FALSE;
    }
    if (Format->nBlockAlign != Format->nChannels * Bits / 8)
        Valid = FALSE;
    if (Format->nAvgBytesPerSec != Format->nSamplesPerSec * Format->nBlockAlign)
        Valid = FALSE;
    return Valid;
}

static
BOOLEAN
XaFloatValid(
    _In_ const WAVEFORMATEX* Format)
{
    BOOLEAN Valid = Format->wBitsPerSample == 32;

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WORD ValidBits = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample;

        if (ValidBits != 0 && ValidBits != 32)
            Valid = FALSE;
    }
    if (Format->nBlockAlign != Format->nChannels * Format->wBitsPerSample / 8)
        Valid = FALSE;
    if (Format->nAvgBytesPerSec != Format->nSamplesPerSec * Format->nBlockAlign)
        Valid = FALSE;
    return Valid;
}

UINT32
XaAdpcmFramesPerBlock(
    _In_ const WAVEFORMATEX* Format)
{
    return reinterpret_cast<const ADPCMWAVEFORMAT*>(Format)->wSamplesPerBlock;
}

static
BOOLEAN
XaAdpcmValid(
    _In_ const WAVEFORMATEX* Format)
{
    const ADPCMWAVEFORMAT* Adpcm = reinterpret_cast<const ADPCMWAVEFORMAT*>(Format);
    BOOLEAN Valid;
    UINT32 Expected;

    Valid = (Format->nChannels == 1 || Format->nChannels == 2) && Format->wBitsPerSample == 4 && Format->cbSize == 32;
    if (!Valid)
        return FALSE;
    if (Adpcm->wNumCoef != ARRAYSIZE(XaAdpcmCoefficients))
    {
        Valid = FALSE;
    }
    else
    {
        for (UINT32 Index = 0; Index < ARRAYSIZE(XaAdpcmCoefficients); Index++)
        {
            if (Adpcm->aCoef[Index].iCoef1 != XaAdpcmCoefficients[Index][0] ||
                Adpcm->aCoef[Index].iCoef2 != XaAdpcmCoefficients[Index][1])
            {
                Valid = FALSE;
            }
        }
    }

    /* Each block holds a seven byte header per channel, then two samples a byte */
    Expected = 8 * (Format->nBlockAlign - 7 * Format->nChannels) / (4 * Format->nChannels) + 2;
    if (Adpcm->wSamplesPerBlock != Expected || Adpcm->wSamplesPerBlock == 0)
        Valid = FALSE;
    return Valid;
}

static
BOOLEAN
XaWmaValid(
    _In_ const WAVEFORMATEX* Format)
{
    BOOLEAN Valid = Format->wBitsPerSample == 16;

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->Samples.wValidBitsPerSample != 16)
    {
        Valid = FALSE;
    }
    return Valid && Format->nBlockAlign != 0 && Format->nAvgBytesPerSec != 0;
}

/* The XMA2 header layout, which only the Xbox headers declare */
#pragma pack(push, 1)
struct XaXma2Format
{
    WAVEFORMATEX Base;
    WORD NumStreams;
    DWORD ChannelMask;
    DWORD SamplesEncoded;
    DWORD BytesPerBlock;
    DWORD PlayBegin;
    DWORD PlayLength;
    DWORD LoopBegin;
    DWORD LoopLength;
    BYTE LoopCount;
    BYTE EncoderVersion;
    WORD BlockCount;
};

struct XaXmaStream
{
    DWORD PacketsPerBlock;
    DWORD SampleRate;
    DWORD LoopStart;
    DWORD LoopEnd;
    BYTE SubframeData;
    BYTE Channels;
    WORD ChannelMask;
};

struct XaXmaFormat
{
    WORD FormatTag;
    WORD BitsPerSample;
    WORD EncodeOptions;
    WORD LargestSkip;
    WORD NumStreams;
    BYTE LoopCount;
    BYTE Version;
    XaXmaStream Streams[1];
};
#pragma pack(pop)

static
BOOLEAN
XaXma2Valid(
    _In_ const WAVEFORMATEX* Format)
{
    const XaXma2Format* Xma = reinterpret_cast<const XaXma2Format*>(Format);
    BOOLEAN Valid;

    if (Format->cbSize != 34)
        return FALSE;
    Valid = Format->nBlockAlign == 2 * Format->nChannels && Format->wBitsPerSample == 16;
    if (Xma->EncoderVersion < 3 || Xma->BlockCount == 0)
        Valid = FALSE;
    if (Xma->BytesPerBlock - 1 > 0x7FF7FF)
        Valid = FALSE;
    if (Xma->ChannelMask != 0 && XaMaskChannels(Xma->ChannelMask) != Format->nChannels)
        Valid = FALSE;
    if (Xma->NumStreams != (Format->nChannels + 1) / 2)
        Valid = FALSE;
    if (Xma->PlayBegin + Xma->PlayLength > Xma->SamplesEncoded)
        Valid = FALSE;
    if (Xma->LoopBegin + Xma->LoopLength > Xma->SamplesEncoded)
        Valid = FALSE;
    return Valid;
}

static
BOOLEAN
XaXmaValid(
    _In_ const WAVEFORMATEX* Format)
{
    const XaXmaFormat* Xma = reinterpret_cast<const XaXmaFormat*>(Format);
    DWORD Rate;
    BOOLEAN Valid;

    if (Format->cbSize < 32)
        return FALSE;
    Rate = Xma->Streams[0].SampleRate;
    Valid = Xma->BitsPerSample == 16 && Xma->Version == 2 && Xma->NumStreams != 0 && Rate != 0;
    for (UINT32 Index = 0; Index < Xma->NumStreams; Index++)
    {
        const XaXmaStream* Stream = &Xma->Streams[Index];

        if (Stream->SampleRate != Rate || (Stream->SubframeData & 0xF) > 4 || (Stream->SubframeData >> 4) > 3)
            Valid = FALSE;
        if (Stream->Channels != 1 && Stream->Channels != 2)
            Valid = FALSE;
        if (Stream->ChannelMask != 0 && XaMaskChannels(Stream->ChannelMask) != Stream->Channels)
            Valid = FALSE;
    }
    return Valid;
}

BOOLEAN
XaFormatValid(
    _In_ const WAVEFORMATEX* Format)
{
    BOOLEAN Valid = TRUE;
    WORD Tag;

    if (Format->wFormatTag != XA_TAG_XMA && (Format->nSamplesPerSec == 0 || Format->nChannels == 0))
        Valid = FALSE;

    Tag = XaFormatTag(Format);
    switch (Tag)
    {
        case WAVE_FORMAT_PCM:
            Valid = Valid && XaPcmValid(Format);
            break;
        case WAVE_FORMAT_ADPCM:
            Valid = Valid && XaAdpcmValid(Format);
            break;
        case WAVE_FORMAT_IEEE_FLOAT:
            Valid = Valid && XaFloatValid(Format);
            break;
        case XA_TAG_WMA2:
        case XA_TAG_WMA3:
            Valid = Valid && XaWmaValid(Format);
            break;
        case XA_TAG_XMA:
            Valid = Valid && XaXmaValid(Format);
            break;
        case XA_TAG_XMA2:
            Valid = Valid && XaXma2Valid(Format);
            break;
        default:
            break;
    }

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);

        if (Format->cbSize < sizeof(*Extensible) - sizeof(Extensible->Format))
            Valid = FALSE;
        else if (Extensible->dwChannelMask != 0 && XaMaskChannels(Extensible->dwChannelMask) != Format->nChannels)
            Valid = FALSE;
    }
    return Valid;
}

UINT32
XaQuantumRate(
    _In_ UINT32 Rate,
    _In_ UINT32 Denominator)
{
    UINT32 Rounded;

    if (Rate % Denominator == 0)
        return Rate;
    Rounded = Rate - Rate % Denominator;
    if (Rounded < XA_RATE_ROUND_UP_BELOW)
        Rounded += Denominator;
    return Rounded;
}
