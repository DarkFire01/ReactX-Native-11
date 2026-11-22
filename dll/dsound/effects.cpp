/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Buffer effects run as DMOs over the written audio ahead of the mixer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dsound.hpp"

#include <mediaobj.h>

#define DS_MAX_EFFECTS 16

static const GUID DsMediaTypeAudio = { 0x73647561, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID DsSubtypePcm = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID DsSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 } };
static const GUID DsFormatWave = { 0x05589F81, 0xC356, 0x11CE, { 0xBF, 0x01, 0x00, 0xAA, 0x00, 0x55, 0x59, 0x5A } };

/* The written audio stays in Dry; the mixer plays the processed copy */
struct DsEffectChain
{
    IMediaObjectInPlace* Objects[DS_MAX_EFFECTS];
    IMediaObject* Media[DS_MAX_EFFECTS];
    IUnknown* Unknowns[DS_MAX_EFFECTS];
    GUID Classes[DS_MAX_EFFECTS];
    DWORD Count;
    BYTE* Dry;
    ULONG ProcessedTo;
    BOOLEAN Primed;
};

static
VOID
DsReleaseChain(
    _In_ DsEffectChain* Chain)
{
    for (DWORD Index = 0; Index < Chain->Count; Index++)
    {
        if (Chain->Objects[Index] != nullptr)
            Chain->Objects[Index]->Release();
        if (Chain->Media[Index] != nullptr)
        {
            Chain->Media[Index]->FreeStreamingResources();
            Chain->Media[Index]->Release();
        }
        if (Chain->Unknowns[Index] != nullptr)
            Chain->Unknowns[Index]->Release();
    }
    HeapFree(GetProcessHeap(), 0, Chain->Dry);
    delete Chain;
}

VOID
DsFreeEffects(
    _In_ DsSecondary* Buffer)
{
    DsEffectChain* Chain = Buffer->Effects;

    if (Chain == nullptr)
        return;
    if (Buffer->Voice.Device != nullptr)
    {
        Buffer->Voice.Device->Lock();
        Buffer->Voice.Effects = nullptr;
        Buffer->Voice.Device->Unlock();
    }
    memcpy(Buffer->Data, Chain->Dry, Buffer->Bytes);
    Buffer->Effects = nullptr;
    DsReleaseChain(Chain);
}

static
HRESULT
DsPrepareEffect(
    _In_ IUnknown* Unknown,
    _In_ const WAVEFORMATEX* Format,
    _Outptr_ IMediaObject** Media,
    _Outptr_ IMediaObjectInPlace** InPlace)
{
    IMediaObject* Object = nullptr;
    DMO_MEDIA_TYPE Type = {};
    HRESULT hr;

    *InPlace = nullptr;
    *Media = nullptr;
    hr = Unknown->QueryInterface(__uuidof(IMediaObject), reinterpret_cast<void**>(&Object));
    if (FAILED(hr))
        return hr;
    *Media = Object;

    Type.majortype = DsMediaTypeAudio;
    Type.subtype = DsFormatTag(Format) == WAVE_FORMAT_IEEE_FLOAT ? DsSubtypeFloat : DsSubtypePcm;
    Type.bFixedSizeSamples = TRUE;
    Type.lSampleSize = Format->nBlockAlign;
    Type.formattype = DsFormatWave;
    Type.cbFormat = DsFormatSize(Format);
    Type.pbFormat = reinterpret_cast<BYTE*>(const_cast<WAVEFORMATEX*>(Format));
    hr = Object->SetInputType(0, &Type, 0);
    if (SUCCEEDED(hr))
        hr = Object->SetOutputType(0, &Type, 0);
    if (SUCCEEDED(hr))
        hr = Object->AllocateStreamingResources();
    if (SUCCEEDED(hr))
        hr = Unknown->QueryInterface(__uuidof(IMediaObjectInPlace), reinterpret_cast<void**>(InPlace));
    return hr;
}

/* Replaces the effect chain; the buffer keeps its contents as the dry signal */
HRESULT
DsSetEffects(
    _In_ DsSecondary* Buffer,
    _In_ DWORD Count,
    _In_reads_opt_(Count) const DSEFFECTDESC* Effects,
    _Out_writes_opt_(Count) DWORD* Results)
{
    DsEffectChain* Chain;
    HRESULT hr = S_OK;

    DsFreeEffects(Buffer);
    if (Count == 0)
        return S_OK;
    if (Count > DS_MAX_EFFECTS)
        return E_INVALIDARG;

    Chain = new (std::nothrow) DsEffectChain();
    if (Chain == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Chain, sizeof(*Chain));
    Chain->Dry = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, Buffer->Bytes));
    if (Chain->Dry == nullptr)
    {
        delete Chain;
        return E_OUTOFMEMORY;
    }

    for (DWORD Index = 0; Index < Count; Index++)
    {
        IUnknown* Unknown = nullptr;
        HRESULT Created = CoCreateInstance(Effects[Index].guidDSFXClass, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown,
                                           reinterpret_cast<void**>(&Unknown));

        if (SUCCEEDED(Created))
            Created = DsPrepareEffect(Unknown, Buffer->Format, &Chain->Media[Index], &Chain->Objects[Index]);
        Chain->Unknowns[Index] = Unknown;
        Chain->Classes[Index] = Effects[Index].guidDSFXClass;
        Chain->Count = Index + 1;
        if (Results != nullptr)
            Results[Index] = SUCCEEDED(Created) ? DSFXR_LOCSOFTWARE : DSFXR_UNKNOWN;
        if (FAILED(Created))
        {
            DsTrace(L"effect %08lX did not load (0x%08lX)", Effects[Index].guidDSFXClass.Data1, Created);
            hr = Created;
        }
    }
    if (FAILED(hr))
    {
        DsReleaseChain(Chain);
        return hr;
    }

    memcpy(Chain->Dry, Buffer->Data, Buffer->Bytes);
    DsFillSilence(Buffer->Data, Buffer->Bytes, Buffer->Format->wBitsPerSample);
    Buffer->Effects = Chain;
    Buffer->Voice.Device->Lock();
    Buffer->Voice.Effects = Chain;
    Buffer->Voice.Device->Unlock();
    return S_OK;
}

DWORD
DsEffectCount(
    _In_ DsSecondary* Buffer)
{
    return Buffer->Effects != nullptr ? Buffer->Effects->Count : 0;
}

BYTE*
DsEffectInput(
    _In_ DsSecondary* Buffer)
{
    return Buffer->Effects != nullptr ? Buffer->Effects->Dry : Buffer->Data;
}

HRESULT
DsFindEffect(
    _In_ DsSecondary* Buffer,
    _In_ REFGUID Object,
    _In_ DWORD Index,
    _In_ REFGUID Interface,
    _Outptr_ VOID** Found)
{
    DsEffectChain* Chain = Buffer->Effects;
    DWORD Seen = 0;

    *Found = nullptr;
    if (Chain == nullptr)
        return DSERR_OBJECTNOTFOUND;
    for (DWORD Slot = 0; Slot < Chain->Count; Slot++)
    {
        if (Object != GUID_All_Objects && Chain->Classes[Slot] != Object)
            continue;
        if (Seen++ != Index)
            continue;
        return Chain->Unknowns[Slot]->QueryInterface(Interface, Found);
    }
    return DSERR_OBJECTNOTFOUND;
}

/* Produces processed audio up to the given byte count past the read position */
VOID
DsRunEffects(
    _In_ DsEffectChain* Chain,
    _In_ DsVoice* Voice,
    _In_ ULONG Ahead)
{
    ULONG Target;

    if (!Chain->Primed)
    {
        for (DWORD Index = 0; Index < Chain->Count; Index++)
        {
            if (Chain->Media[Index] != nullptr)
                Chain->Media[Index]->Flush();
        }
        /* Processing starts 32 frames behind the read position, wrapping to the end of the buffer */
        Chain->ProcessedTo = (Voice->Size - 32 * Voice->BlockAlign + Voice->ReadPosition) % Voice->Size;
        Chain->Primed = TRUE;
    }

    Ahead = DsMin(Ahead, Voice->Size);
    Target = (Voice->ReadPosition + Ahead) % Voice->Size;
    while (Chain->ProcessedTo != Target)
    {
        ULONG Start = Chain->ProcessedTo;
        ULONG End = Target > Start ? Target : Voice->Size;
        ULONG Length = End - Start;

        memcpy(Voice->Data + Start, Chain->Dry + Start, Length);
        for (DWORD Index = 0; Index < Chain->Count; Index++)
        {
            if (Chain->Objects[Index] != nullptr)
                Chain->Objects[Index]->Process(Length, Voice->Data + Start, 0, DMO_INPLACE_NORMAL);
        }
        Chain->ProcessedTo = End % Voice->Size;
    }
}

VOID
DsResetEffects(
    _In_ DsEffectChain* Chain)
{
    Chain->Primed = FALSE;
}
