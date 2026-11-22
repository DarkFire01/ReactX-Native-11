/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Wave format and buffer description checks, volume and pan arithmetic
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <math.h>

/* 1/65535 rounded to single precision, as the engine scales amplitudes */
const float DsAmplitudeScale = 1.0f / 65535.0f;

/* Amplitudes for -96.4 dB up to -0.1 dB in tenth of a decibel steps */
static ULONG DsAmplitudeTable[964];
static INIT_ONCE DsAmplitudeOnce = INIT_ONCE_STATIC_INIT;

/* The KSDATAFORMAT base GUID past the low word that carries the format tag */
static const BYTE DsKsBaseTail[14] = { 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };

static
BOOL
CALLBACK
DsBuildAmplitudeTable(
    _Inout_ PINIT_ONCE Once,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_ PVOID* Context)
{
    UNREFERENCED_PARAMETER(Once);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    for (int Step = 0; Step < 964; Step++)
        DsAmplitudeTable[Step] = static_cast<ULONG>(floor(65536.0 * pow(10.0, (Step - 964) / 200.0)));
    return TRUE;
}

static
const ULONG*
DsAmplitudes(VOID)
{
    InitOnceExecuteOnce(&DsAmplitudeOnce, DsBuildAmplitudeTable, nullptr, nullptr);
    return DsAmplitudeTable;
}

ULONG
DsDecibelsToAmplitude(
    _In_ LONG Hundredths)
{
    LONG Tenths;

    if (Hundredths >= 0)
        return 0xFFFF;
    Tenths = (Hundredths + 5) / 10;
    if (Tenths > -1)
        Tenths = -1;
    if (Tenths < -964)
        Tenths = -964;
    return DsAmplitudes()[Tenths + 964];
}

LONG
DsAmplitudeToDecibels(
    _In_ ULONG Amplitude)
{
    const ULONG* Table = DsAmplitudes();
    LONG Step = 0;

    if (Amplitude >= 0xFFFF)
        return 0;
    while (Step < 963 && Amplitude > Table[Step])
        Step++;
    return Step * 10 - 9640;
}

VOID
DsResolveVolumePan(
    _In_ LONG Volume,
    _In_ LONG Pan,
    _Out_ DsAttenuation* Attenuation)
{
    Volume = DsMin<LONG>(DsMax<LONG>(Volume, DSBVOLUME_MIN), DSBVOLUME_MAX);
    Pan = DsMin<LONG>(DsMax<LONG>(Pan, DSBPAN_LEFT), DSBPAN_RIGHT);

    Attenuation->Volume = Volume;
    Attenuation->Pan = Pan;
    if (Pan >= 0)
    {
        Attenuation->LeftAmplitude = DsDecibelsToAmplitude(Volume - Pan);
        Attenuation->RightAmplitude = DsDecibelsToAmplitude(Volume);
        Attenuation->PanLeftAmplitude = DsDecibelsToAmplitude(-Pan);
        Attenuation->PanRightAmplitude = DsDecibelsToAmplitude(0);
    }
    else
    {
        Attenuation->LeftAmplitude = DsDecibelsToAmplitude(Volume);
        Attenuation->RightAmplitude = DsDecibelsToAmplitude(Volume + Pan);
        Attenuation->PanLeftAmplitude = DsDecibelsToAmplitude(0);
        Attenuation->PanRightAmplitude = DsDecibelsToAmplitude(Pan);
    }
    Attenuation->VolumeAmplitude = DsDecibelsToAmplitude(Volume);
}

/* The format tag, looking through WAVE_FORMAT_EXTENSIBLE to its sub format */
WORD
DsFormatTag(
    _In_ const WAVEFORMATEX* Format)
{
    const WAVEFORMATEXTENSIBLE* Extensible;

    if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        return Format->wFormatTag;
    if (Format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return 0;

    Extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);
    if (memcmp(reinterpret_cast<const BYTE*>(&Extensible->SubFormat) + 2, DsKsBaseTail, sizeof(DsKsBaseTail)) != 0)
        return 0;
    return static_cast<WORD>(Extensible->SubFormat.Data1);
}

/* The average byte rate may stray five percent from the exact one */
static
BOOLEAN
DsIsCloseRate(
    _In_ const WAVEFORMATEX* Format)
{
    ULONG Exact = Format->nSamplesPerSec * Format->nBlockAlign;
    ULONG Slack = Exact / 20;

    return Format->nAvgBytesPerSec <= Exact + Slack && Format->nAvgBytesPerSec >= Exact - Slack;
}

BOOLEAN
DsIsValidPcmFormat(
    _In_ const WAVEFORMATEX* Format)
{
    WORD Bits = Format->wBitsPerSample;

    if (Format->nChannels == 0)
        return FALSE;
    if (Bits != 8 && Bits != 16 && Bits != 24 && Bits != 32)
        return FALSE;
    if (Format->nSamplesPerSec < DSBFREQUENCY_MIN || Format->nSamplesPerSec > DSBFREQUENCY_MAX)
        return FALSE;
    if (Format->nBlockAlign != (Bits / 8) * Format->nChannels)
        return FALSE;
    return DsIsCloseRate(Format);
}

static
BOOLEAN
DsIsValidFloatFormat(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->nChannels == 0 || Format->wBitsPerSample != 32)
        return FALSE;
    if (Format->nSamplesPerSec < DSBFREQUENCY_MIN || Format->nSamplesPerSec > DSBFREQUENCY_MAX)
        return FALSE;
    if (Format->nBlockAlign != 4 * Format->nChannels)
        return FALSE;
    return DsIsCloseRate(Format);
}

static
ULONG
DsCountBits(
    _In_ DWORD Value)
{
    ULONG Count = 0;

    for (; Value != 0; Value &= Value - 1)
        Count++;
    return Count;
}

BOOLEAN
DsIsValidFormat(
    _In_ const WAVEFORMATEX* Format)
{
    const WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format);
    WORD Tag = DsFormatTag(Format);

    if (Tag == WAVE_FORMAT_PCM && !DsIsValidPcmFormat(Format))
        return FALSE;
    if (Tag == WAVE_FORMAT_IEEE_FLOAT && !DsIsValidFloatFormat(Format))
        return FALSE;
    if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        return TRUE;

    if (Format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        return FALSE;
    if (Extensible->Samples.wValidBitsPerSample > Format->wBitsPerSample)
        return FALSE;
    if (DsCountBits(Extensible->dwChannelMask) > Format->nChannels)
        return FALSE;
    return Extensible->SubFormat != GUID_NULL;
}

/* What the mixer takes: valid bit depths per container and extensible multichannel layouts */
HRESULT
DsCheckEngineFormat(
    _Inout_ WAVEFORMATEX* Format)
{
    WAVEFORMATEXTENSIBLE* Extensible = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(Format);

    if (!DsIsValidFormat(Format))
        return E_INVALIDARG;

    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        WORD Tag = DsFormatTag(Format);
        WORD Valid;

        if (Extensible->Samples.wValidBitsPerSample == 0)
            Extensible->Samples.wValidBitsPerSample = Format->wBitsPerSample;
        Valid = Extensible->Samples.wValidBitsPerSample;

        if (Tag == WAVE_FORMAT_IEEE_FLOAT && Valid != 32)
            return E_INVALIDARG;
        if (Tag == WAVE_FORMAT_PCM)
        {
            switch (Format->wBitsPerSample)
            {
                case 8:
                case 16:
                    if (Valid != Format->wBitsPerSample)
                        return E_INVALIDARG;
                    break;
                case 24:
                    if (Valid != 20 && Valid != 24)
                        return E_INVALIDARG;
                    break;
                case 32:
                    if (Valid != 20 && Valid != 24 && Valid != 32)
                        return E_INVALIDARG;
                    break;
            }
        }
    }

    if (Format->nChannels > 2)
    {
        if (Format->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
            return E_INVALIDARG;
        if (Extensible->dwChannelMask != 0 && DsCountBits(Extensible->dwChannelMask) != Format->nChannels)
            return E_INVALIDARG;
    }
    return S_OK;
}

/* PCM formats are stored without a cbSize field, everything else with its extra bytes */
static
ULONG
DsFormatBytes(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == WAVE_FORMAT_PCM)
        return sizeof(PCMWAVEFORMAT);
    return sizeof(WAVEFORMATEX) + Format->cbSize;
}

ULONG
DsFormatSize(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == WAVE_FORMAT_PCM)
        return sizeof(WAVEFORMATEX);
    return sizeof(WAVEFORMATEX) + Format->cbSize;
}

WAVEFORMATEX*
DsCopyFormat(
    _In_ const WAVEFORMATEX* Format)
{
    ULONG Size = DsFormatSize(Format);
    WAVEFORMATEX* Copy;

    /* Room for a full WAVEFORMATEXTENSIBLE keeps later channel mask fixups in bounds */
    Copy = static_cast<WAVEFORMATEX*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DsMax<SIZE_T>(Size, sizeof(WAVEFORMATEXTENSIBLE))));
    if (Copy != nullptr)
        memcpy(Copy, Format, DsFormatBytes(Format));
    return Copy;
}

BOOLEAN
DsSameFormat(
    _In_ const WAVEFORMATEX* Left,
    _In_ const WAVEFORMATEX* Right)
{
    ULONG Size = DsFormatBytes(Left);

    return Size == DsFormatBytes(Right) && memcmp(Left, Right, Size) == 0;
}

HRESULT
DsCopyFormatOut(
    _In_ const WAVEFORMATEX* Format,
    _Out_writes_bytes_opt_(*Size) WAVEFORMATEX* Out,
    _Inout_ DWORD* Size)
{
    ULONG Needed = DsFormatSize(Format);
    ULONG Room = *Size;

    *Size = Needed;
    if (Out == nullptr)
        return S_OK;
    if (Needed > Room)
        return E_INVALIDARG;
    memcpy(Out, Format, DsFormatBytes(Format));
    memset(reinterpret_cast<BYTE*>(Out) + DsFormatBytes(Format), 0, Needed - DsFormatBytes(Format));
    return S_OK;
}

/* The speakers a stream feeds: its mask, or front center for mono and front pair otherwise */
DWORD
DsChannelMask(
    _In_ const WAVEFORMATEX* Format)
{
    if (Format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(Format)->dwChannelMask;
    if (Format->nChannels == 1)
        return SPEAKER_FRONT_CENTER;
    return SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
}

ULONG
DsAlignSize(
    _In_ const WAVEFORMATEX* Format,
    _In_ ULONG Size)
{
    ULONG Remainder;

    if (Format->nBlockAlign == 0)
        return Size;
    Remainder = Size % Format->nBlockAlign;
    if (Remainder != 0)
        Size += Format->nBlockAlign - Remainder;
    return Size;
}

VOID
DsFillSilence(
    _Out_writes_bytes_(Size) BYTE* Data,
    _In_ ULONG Size,
    _In_ WORD Bits)
{
    memset(Data, Bits == 8 ? 0x80 : 0x00, Size);
}

static
BOOLEAN
DsIsKnownAlgorithm(
    _In_ const GUID* Algorithm)
{
    return *Algorithm == GUID_NULL || *Algorithm == DS3DALG_NO_VIRTUALIZATION || *Algorithm == DS3DALG_HRTF_LIGHT ||
           *Algorithm == DS3DALG_HRTF_FULL;
}

static
BOOLEAN
DsIsValidBufferFlags(
    _In_ DWORD Flags,
    _In_ DWORD Allowed)
{
    if ((Flags & ~Allowed) != 0)
        return FALSE;
    if ((Flags & (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE)) == (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE))
        return FALSE;
    if ((Flags & DSBCAPS_LOCDEFER) && (Flags & (DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE)))
        return FALSE;
    if ((Flags & DSBCAPS_MUTE3DATMAXDISTANCE) && !(Flags & DSBCAPS_CTRL3D))
        return FALSE;
    if ((Flags & DSBCAPS_STATIC) && (Flags & (DSBCAPS_CTRLFX | DS_BUFFER_MIXIN | DS_BUFFER_SINKIN)))
        return FALSE;
    if ((Flags & DSBCAPS_PRIMARYBUFFER) && (Flags & DS_PRIMARY_DENIED))
        return FALSE;
    return TRUE;
}

static
HRESULT
DsCheckBufferDesc(
    _In_ const DSBUFFERDESC* Desc,
    _In_ ULONG Version)
{
    const WAVEFORMATEX* Format = Desc->lpwfxFormat;
    DWORD Flags = Desc->dwFlags;
    WORD Tag;

    if (Desc->dwReserved != 0)
        return E_INVALIDARG;
    if (!DsIsValidBufferFlags(Flags, (Flags & DS_BUFFER_MIXIN) ? DS_MIXIN_FLAGS : DS_SECONDARY_FLAGS))
        return E_INVALIDARG;
    if (Version < DS_VERSION_7 && (Flags & DSBCAPS_LOCDEFER))
        return E_INVALIDARG;
    if (Version < DS_VERSION_8 && (Flags & (DSBCAPS_CTRLFX | DS_BUFFER_MIXIN)))
        return DSERR_DS8_REQUIRED;

    if (Flags & DSBCAPS_PRIMARYBUFFER)
    {
        if (Desc->dwBufferBytes != 0 || Format != nullptr || Desc->guid3DAlgorithm != GUID_NULL)
            return E_INVALIDARG;
        return S_OK;
    }

    if (Format == nullptr || !DsIsValidFormat(Format))
        return E_INVALIDARG;
    if (Desc->dwBufferBytes < DSBSIZE_MIN || Desc->dwBufferBytes > DSBSIZE_MAX)
        return E_INVALIDARG;

    if (Flags & DSBCAPS_CTRLFX)
    {
        ULONG Needed = DS_FX_MINIMUM_MS * Format->nAvgBytesPerSec / 1000;

        Needed = Needed / Format->nBlockAlign * Format->nBlockAlign;
        if (Desc->dwBufferBytes < Needed)
            return DSERR_BUFFERTOOSMALL;
        if (Format->nChannels != 1 && Format->nChannels != 2)
            return E_INVALIDARG;
        Tag = DsFormatTag(Format);
        if (Tag != WAVE_FORMAT_PCM && Tag != WAVE_FORMAT_IEEE_FLOAT)
            return E_INVALIDARG;
        if (Tag == WAVE_FORMAT_PCM && Format->wBitsPerSample != 8 && Format->wBitsPerSample != 16)
            return E_INVALIDARG;
    }

    if (Desc->guid3DAlgorithm != GUID_NULL && !(Flags & DSBCAPS_CTRL3D))
        return E_INVALIDARG;
    if (Flags & DSBCAPS_CTRL3D)
    {
        if (Format->nChannels > 2)
            return E_INVALIDARG;
        if (Version >= DS_VERSION_8 && (Format->nChannels != 1 || (Flags & DSBCAPS_CTRLPAN)))
            return E_INVALIDARG;
    }
    return S_OK;
}

/* Copies a DirectX 3 or later description into the full layout, then checks it */
HRESULT
DsBuildBufferDesc(
    _In_ const DSBUFFERDESC* Desc,
    _Out_ DSBUFFERDESC* Valid,
    _In_ ULONG Version)
{
    if (Desc->dwSize == sizeof(DSBUFFERDESC1))
        Version = 0;
    else if (Desc->dwSize != sizeof(DSBUFFERDESC))
        return E_INVALIDARG;

    ZeroMemory(Valid, sizeof(*Valid));
    memcpy(Valid, Desc, Desc->dwSize);
    Valid->dwSize = sizeof(*Valid);

    /* Unknown algorithms, the obsolete ITD one among them, fall back to the default */
    if (!DsIsKnownAlgorithm(&Valid->guid3DAlgorithm))
        Valid->guid3DAlgorithm = GUID_NULL;
    return DsCheckBufferDesc(Valid, Version);
}

HRESULT
DsBuildCaptureDesc(
    _In_ const DSCBUFFERDESC* Desc,
    _Out_ DSCBUFFERDESC* Valid,
    _In_ ULONG Version)
{
    if (Desc->dwSize == sizeof(DSCBUFFERDESC1))
        Version = 0;
    else if (Desc->dwSize != sizeof(DSCBUFFERDESC))
        return E_INVALIDARG;

    ZeroMemory(Valid, sizeof(*Valid));
    memcpy(Valid, Desc, Desc->dwSize);
    Valid->dwSize = sizeof(*Valid);

    if (Valid->dwFlags & ~DSCBCAPS_WAVEMAPPED & ~DSCBCAPS_CTRLFX)
        return E_INVALIDARG;
    if (Valid->dwFlags & DSCBCAPS_CTRLFX)
    {
        if (Version < DS_VERSION_8)
            return DSERR_DS8_REQUIRED;
        if ((Valid->dwFXCount == 0) != (Valid->lpDSCFXDesc == nullptr))
            return E_INVALIDARG;
    }
    else if (Valid->dwFXCount != 0 || Valid->lpDSCFXDesc != nullptr)
    {
        return E_INVALIDARG;
    }
    if (Valid->dwReserved != 0)
        return E_INVALIDARG;
    if (Valid->dwBufferBytes < DSBSIZE_MIN || Valid->dwBufferBytes > DSBSIZE_MAX)
        return E_INVALIDARG;
    if (Valid->lpwfxFormat == nullptr || !DsIsValidFormat(Valid->lpwfxFormat))
        return E_INVALIDARG;
    for (DWORD Index = 0; Index < Valid->dwFXCount; Index++)
    {
        const DSCEFFECTDESC* Effect = &Valid->lpDSCFXDesc[Index];

        if (Effect->dwSize != sizeof(*Effect) || Effect->dwReserved1 != 0 || Effect->dwReserved2 != 0)
            return E_INVALIDARG;
    }
    return S_OK;
}

/* Send effects belong to DirectMusic buffers, which this runtime does not make */
BOOLEAN
DsIsValidEffectDesc(
    _In_ const DSEFFECTDESC* Effect)
{
    if (Effect->dwSize != sizeof(*Effect))
        return FALSE;
    if (Effect->dwFlags & ~(DSFX_LOCHARDWARE | DSFX_LOCSOFTWARE))
        return FALSE;
    if (Effect->guidDSFXClass == DsSendEffect)
        return FALSE;
    return Effect->dwReserved1 == 0 && Effect->dwReserved2 == 0;
}
