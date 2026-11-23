/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA decoder: frames, tiles and the PCM they leave behind
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmacodes.hpp"
#include "wmaxform.hpp"

#include <math.h>

#pragma fp_contract(off)

/* What the decoder returns for a header it has no encoding for */
#define XA_WMA_E_FORMAT         static_cast<HRESULT>(0x80040000)
#define XA_WMA_E_CORRUPT        static_cast<HRESULT>(0x80040002)
#define XA_WMA_E_STARVED        static_cast<HRESULT>(0x80040004)

/* Quantizer index bits and the value that says more follow */
#define XA_WMA_QUANT_BITS       7
#define XA_WMA_QUANT_MORE       127

/* Mask values are sent as deltas from the band below, the first from a fixed start */
#define XA_WMA_MASK_START       36
#define XA_WMA_MASK_BIAS        60

/* Largest PCM block handed out at once */
#define XA_WMA_PCM_MAX          0x7FFF

static const XaWmaCodeBook XaWmaBooks[3][2] =
{
    { { XaWmaLowTree, XaWmaLowRuns, XaWmaLowLevels }, { XaWmaLowSideTree, XaWmaLowSideRuns, XaWmaLowSideLevels } },
    { { XaWmaMidTree, XaWmaMidRuns, XaWmaMidLevels }, { XaWmaMidSideTree, XaWmaMidSideRuns, XaWmaMidSideLevels } },
    { { XaWmaHighTree, XaWmaHighRuns, XaWmaHighLevels }, { XaWmaHighSideTree, XaWmaHighSideRuns, XaWmaHighSideLevels } },
};

static
UINT32
XaWmaLog2(
    _In_ UINT32 Value)
{
    UINT32 Result = 0;

    while ((Value >> (Result + 1)) != 0)
        Result += 1;
    return Result;
}

/* Whether the decoder can reconstruct the stream; anything else only drains */
static
BOOLEAN
XaWmaDecodable(
    _In_ const XaWmaDecoder* Decoder)
{
    if (Decoder->Layout.Channels < 1 || Decoder->Layout.Channels > XA_WMA_MAX_CHANNELS)
        return FALSE;
    if (Decoder->Layout.Version == XA_WMA_PRO)
        return Decoder->Layout.FrameSamples != 0;
    return Decoder->Layout.Version == XA_WMA_STANDARD_MAX && Decoder->Standard.SuperFrames;
}

static
float
XaWmaQuantStep(
    _In_ INT32 Index)
{
    return static_cast<float>(pow(10.0, static_cast<double>(static_cast<float>(Index) * 0.05f)));
}

/* Escape levels get fewer bits as the quantizer coarsens */
static
UINT32
XaWmaEscapeBits(
    _In_ INT32 Index)
{
    if (Index < 15)
        return 13;
    if (Index < 32)
        return 12;
    if (Index < 40)
        return 11;
    if (Index < 45)
        return 10;
    return 9;
}

/* Brings the stream state back to how a new stream starts */
static
VOID
XaWmaRestart(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaReaderReset(Decoder);
    Decoder->FramesLeft = 0;
    Decoder->SkipOffset = TRUE;
    Decoder->ReadTileHeader = TRUE;
    Decoder->FirstTile = TRUE;
    Decoder->Read = 0;
    Decoder->End = 0;
    Decoder->Discard = 0;
    Decoder->Seeking = TRUE;
    Decoder->EndTrim = 0;
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];

        Channel->Tiles.Sizes[0] = Decoder->FrameSize;
        Channel->Tiles.Sizes[1] = Decoder->FrameSize;
        Channel->Tiles.Count = 1;
        Channel->Tiles.Covered = 0;
        Channel->Tiles.Index = 0;
        Channel->Fill = 0;
        if (Channel->Pcm != nullptr)
            memset(Channel->Pcm, 0, sizeof(*Channel->Pcm) * (3 * Decoder->FrameSize / 2));
    }
}

/* Reads a tile size code; sizes halve from the whole frame */
static
HRESULT
XaWmaReadTileSize(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ INT32* Size)
{
    UINT32 Code;

    if (!XaWmaGetBits(Decoder, Decoder->TileCodeBits, &Code))
        return XA_WMA_E_STARVED;
    if (Code > 30)
        return XA_WMA_E_CORRUPT;
    *Size = static_cast<INT16>(Decoder->FrameSize / (1 << Code));
    if (*Size < Decoder->MinTile || *Size > Decoder->FrameSize)
        return XA_WMA_E_CORRUPT;
    return S_OK;
}

/* Adds the tile after the current one, or marks the current one as the frame's last */
static
HRESULT
XaWmaPlanTiles(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ INT32 NextSize)
{
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;
        INT32 Slot = Tiles->Index + 1;

        if (Slot + 1 >= ARRAYSIZE(Tiles->Sizes))
            return XA_WMA_E_CORRUPT;
        if (Tiles->Sizes[Slot] + Tiles->Covered < Decoder->FrameSize)
        {
            Tiles->Sizes[Slot + 1] = NextSize;
            Tiles->Covered += Tiles->Sizes[Slot];
            if (Tiles->Sizes[Slot + 1] + Tiles->Covered > Decoder->FrameSize)
                return XA_WMA_E_CORRUPT;
            Tiles->Count += 1;
        }
        else
        {
            Decoder->LastTile = TRUE;
            Tiles->Sizes[Slot + 1] = NextSize;
        }
    }
    return S_OK;
}

/* Points every channel at its band layout, coefficient range, window and output slot for the tile */
static
HRESULT
XaWmaEnterTile(
    _Inout_ XaWmaDecoder* Decoder)
{
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];
        XaWmaTiling* Tiles = &Channel->Tiles;
        INT32 Slot = Tiles->Index + 1;
        UINT32 Level = 0;

        Channel->PrevSize = Tiles->Sizes[Slot - 1];
        Channel->Size = Tiles->Sizes[Slot];
        Channel->NextSize = Tiles->Sizes[Slot + 1];
        if (Channel->Size <= 0)
            return XA_WMA_E_CORRUPT;
        if (Tiles->Count > 1)
            Level = XaWmaLog2(static_cast<UINT32>(Decoder->FrameSize / Channel->Size));
        if (Level >= Decoder->Levels)
            return XA_WMA_E_CORRUPT;

        Decoder->Bands = Decoder->BandCounts[Level];
        Decoder->Edges = Decoder->BandEdges[Level];
        Decoder->LowIndex = static_cast<INT32>(Decoder->Standard.LowCutoff) * Channel->Size / Decoder->FrameSize;
        Decoder->HighIndex = static_cast<INT32>(Decoder->Standard.HighCutoff) * Channel->Size / Decoder->FrameSize;
        Channel->Width = Decoder->HighIndex - Decoder->LowIndex;
        Channel->CodedCount = Channel->Width;
        if (Decoder->NoiseSub)
            XaWmaEnterNoiseTile(Decoder, Level, Channel->Size);
        XaWmaSetTurn(Channel);

        if (Channel->Fill + Channel->Size > Decoder->FrameSize)
            return XA_WMA_E_CORRUPT;
        Channel->Coefs = Channel->Pcm + Decoder->FrameSize / 2 + Channel->Fill;
        Channel->Fill += Channel->Size;
    }
    return S_OK;
}

/* Reads the per band mask of every coded channel when the tile sends a new one */
static
HRESULT
XaWmaReadMasks(
    _Inout_ XaWmaDecoder* Decoder)
{
    XaWmaChannel* First = &Decoder->Channels[0];
    UINT32 Fresh = 1;

    if (Decoder->Standard.Tiling && First->Tiles.Count > 1)
    {
        if (!XaWmaGetBits(Decoder, 1, &Fresh))
            return XA_WMA_E_STARVED;
        if (First->Tiles.Index == 0 && Fresh != 1)
            return XA_WMA_E_CORRUPT;
    }

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
        Decoder->Channels[Index].FreshMask = Fresh != 0;
    if (!Decoder->Standard.Bark)
        return XaWmaReadLpcHeader(Decoder);

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];
        INT32 Peak;

        if (!Channel->Coded)
        {
            if (Channel->FreshMask)
            {
                memset(Channel->Mask, 0, sizeof(*Channel->Mask) * Decoder->Bands);
                Channel->MaskPeak = 0;
            }
            continue;
        }
        if (!Channel->FreshMask)
            continue;

        for (INT32 Band = 0; Band < Decoder->Bands; Band++)
        {
            UINT32 Symbol;
            UINT32 After;

            if (!XaWmaGetSymbol(Decoder, XaWmaMaskTree, &Symbol, &After))
                return XA_WMA_E_STARVED;
            Channel->Mask[Band] = static_cast<INT32>(Symbol) - XA_WMA_MASK_BIAS +
                                  (Band != 0 ? Channel->Mask[Band - 1] : XA_WMA_MASK_START);
        }
        Decoder->MaskSize = Channel->Size;

        Peak = Channel->Mask[0];
        for (INT32 Band = 1; Band < Decoder->Bands; Band++)
        {
            if (Channel->Mask[Band] > Peak)
                Peak = Channel->Mask[Band];
        }
        Channel->MaskPeak = Peak;
    }
    return S_OK;
}

/* Whether any coefficient of the tile is nonzero, sign included */
static
BOOLEAN
XaWmaHasSignal(
    _In_ const XaWmaChannel* Channel)
{
    for (INT32 Index = 0; Index < Channel->Size; Index++)
    {
        UINT32 Bits;

        memcpy(&Bits, &Channel->Coefs[Index], sizeof(Bits));
        if (Bits != 0)
            return TRUE;
    }
    return FALSE;
}

/**
 * @brief Decodes the levels of every coded channel, then runs the inverse quantizer on each.
 */
static
HRESULT
XaWmaDecodeQuantized(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    HRESULT hr;

    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];

        if (!Channel->Coded)
            continue;
        if (Channel->CodedCount < 0 || Channel->CodedCount > Channel->Size)
            return XA_WMA_E_CORRUPT;
        memset(Channel->Levels, 0, sizeof(*Channel->Levels) * Channel->CodedCount);
        hr = XaWmaDecodeLevels(Decoder, Channel);
        if (FAILED(hr))
            return hr;
        if (Decoder->FirstTile)
        {
            for (INT32 Slot = 0; Slot < Channel->CodedCount && !Channel->Audible; Slot++)
                Channel->Audible = Channel->Levels[Slot] != 0;
        }
    }
    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];

        if (!Channel->Coded)
            continue;
        if (!Decoder->Standard.Bark)
        {
            XaWmaQuantizeLow(Decoder, Channel);
            continue;
        }
        hr = XaWmaQuantizeMid(Decoder, Channel);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

/**
 * @brief Decodes one tile: its header, masks and coefficients, then runs the inverse transform.
 */
static
HRESULT
XaWmaDecodeTile(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    INT32 NextSize = Decoder->FrameSize;
    BOOLEAN AnyCoded = FALSE;
    UINT32 Bit;
    HRESULT hr;

    Decoder->LastTile = FALSE;
    if (Decoder->Standard.Tiling)
    {
        hr = XaWmaReadTileSize(Decoder, &NextSize);
        if (FAILED(hr))
            return hr;
    }
    hr = XaWmaPlanTiles(Decoder, NextSize);
    if (SUCCEEDED(hr))
        hr = XaWmaEnterTile(Decoder);
    if (FAILED(hr))
        return hr;

    Decoder->MidSide = FALSE;
    if (Channels == 2)
    {
        if (!XaWmaGetBits(Decoder, 1, &Bit))
            return XA_WMA_E_STARVED;
        Decoder->MidSide = Bit != 0;
        Decoder->Channels[1].Book = Decoder->MidSide ? Decoder->SideBook : Decoder->MidBook;
    }
    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        if (!XaWmaGetBits(Decoder, 1, &Bit))
            return XA_WMA_E_STARVED;
        Decoder->Channels[Index].Coded = Bit != 0;
        AnyCoded = AnyCoded || Bit != 0;
    }

    Decoder->QuantIndex = 1;
    if (AnyCoded)
    {
        do
        {
            if (!XaWmaGetBits(Decoder, XA_WMA_QUANT_BITS, &Bit))
                return XA_WMA_E_STARVED;
            Decoder->QuantIndex += static_cast<INT32>(Bit);
        } while (Bit == XA_WMA_QUANT_MORE);
        Decoder->EscapeBits = XaWmaEscapeBits(Decoder->QuantIndex);
        hr = Decoder->NoiseSub ? XaWmaReadNoiseHeader(Decoder) : S_OK;
        if (SUCCEEDED(hr))
            hr = XaWmaReadMasks(Decoder);
        if (FAILED(hr))
            return hr;
    }
    else if (!Decoder->Standard.Bark)
    {
        /* Without coded channels, LPC weights still restart on the frame's first tile */
        for (UINT32 Index = 0; Index < Channels; Index++)
            Decoder->Channels[Index].FreshMask = Decoder->Channels[Index].Tiles.Index == 0;
        hr = XaWmaReadLpcHeader(Decoder);
        if (FAILED(hr))
            return hr;
    }

    Decoder->QuantStep = XaWmaQuantStep(Decoder->QuantIndex);
    for (UINT32 Index = 0; Index < Channels; Index++)
    {
        XaWmaChannel* Channel = &Decoder->Channels[Index];

        memset(Channel->Coefs, 0, sizeof(*Channel->Coefs) * Channel->Size);
        Channel->Audible = FALSE;
    }
    if (Decoder->NoiseSub || !Decoder->Standard.Bark)
    {
        hr = XaWmaDecodeQuantized(Decoder);
        if (FAILED(hr))
            return hr;
    }
    else
    {
        Decoder->CoefIndex = Decoder->LowIndex - 1;
        for (UINT32 Index = 0; Index < Channels; Index++)
        {
            XaWmaChannel* Channel = &Decoder->Channels[Index];

            if (!Channel->Coded)
                continue;
            hr = XaWmaReconstruct(Decoder, Channel);
            if (FAILED(hr))
                return Decoder->Reader.Dry ? XA_WMA_E_STARVED : hr;
            Decoder->CoefIndex = Decoder->LowIndex - 1;
            if (Decoder->FirstTile)
                Channel->Audible = XaWmaHasSignal(Channel);
        }
    }

    if (Decoder->MidSide && AnyCoded)
    {
        float* Mid = Decoder->Channels[0].Coefs;
        float* Side = Decoder->Channels[1].Coefs;

        Decoder->Channels[0].Coded = TRUE;
        Decoder->Channels[1].Coded = TRUE;
        for (INT32 Index = 0; Index < Decoder->Channels[0].Size; Index++)
        {
            float Sum = Mid[Index];
            float Difference = Side[Index];

            Mid[Index] = Difference + Sum;
            Side[Index] = Sum - Difference;
        }
    }

    if (AnyCoded)
    {
        for (UINT32 Index = 0; Index < Channels; Index++)
        {
            XaWmaChannel* Channel = &Decoder->Channels[Index];

            if (Channel->Coded)
            {
                XaWmaInverseTransform(Decoder->Transform, Channel->Coefs, static_cast<UINT32>(Channel->Size),
                                      2.0f / static_cast<float>(Channel->Size));
            }
        }
    }
    return S_OK;
}

/* Samples the first frame hides: the encoder's lead in, or two frames when it starts silent */
static
VOID
XaWmaSetDiscard(
    _Inout_ XaWmaDecoder* Decoder)
{
    const XaWmaChannel* First = &Decoder->Channels[0];
    BOOLEAN Silent = First->FreshMask;
    INT32 Previous = First->Tiles.Sizes[0];
    INT32 Current = First->Tiles.Sizes[1];
    INT32 Middle;

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        if (!Decoder->Channels[Index].Coded || Decoder->Channels[Index].Audible)
            Silent = FALSE;
    }
    if (Silent)
    {
        Decoder->Discard = 2 * Decoder->FrameSize;
        return;
    }
    Middle = Current > Previous ? (Current + Previous) / 2 : Current;
    Decoder->Discard = Decoder->FrameSize / 2 - Current / 2 + Middle;
}

/* Moves the unfinished half frame to the front of every channel's buffer */
static
VOID
XaWmaShiftPcm(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Frame = Decoder->FrameSize;

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        float* Pcm = Decoder->Channels[Index].Pcm;

        memmove(Pcm, Pcm + Frame, sizeof(*Pcm) * (Frame / 2));
    }
    Decoder->Read -= Frame;
    Decoder->End -= Frame;
}

/* Works out how far the frame's output is final and starts the next frame's tiling */
static
HRESULT
XaWmaFinishFrame(
    _Inout_ XaWmaDecoder* Decoder)
{
    INT32 Frame = Decoder->FrameSize;
    INT32 End = MAXINT32;
    INT32 Produced;

    if (Decoder->Read < 0 || Decoder->Read >= Frame / 2)
        return E_FAIL;
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        const XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;
        INT32 Last = Tiles->Sizes[Tiles->Count];
        INT32 Next = Tiles->Sizes[Tiles->Count + 1];
        INT32 Settled = Last > Next ? Last + (Last - Next) / 2 : Last;
        INT32 Reach = Frame / 2 - (3 * Last) / 2 + Frame + Settled;

        if (Reach < End)
            End = Reach;
    }

    Produced = End - Decoder->Read;
    Decoder->End = End;
    if (Decoder->Discard > 0)
    {
        if (Decoder->Discard < Produced)
        {
            Decoder->Read += Decoder->Discard;
            Decoder->Discard = 0;
        }
        else
        {
            Decoder->Discard -= Produced;
            Decoder->Read = Decoder->End;
        }
        if (Decoder->Read >= Frame)
            XaWmaShiftPcm(Decoder);
    }
    if (Decoder->End < Decoder->Read)
        return XA_WMA_E_CORRUPT;

    for (UINT32 Index = 0; Index < Decoder->Layout.Channels; Index++)
    {
        XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

        Tiles->Sizes[0] = Tiles->Sizes[Tiles->Count];
        Tiles->Sizes[1] = Tiles->Sizes[Tiles->Count + 1];
        Tiles->Covered = 0;
        Tiles->Count = 1;
    }
    return S_OK;
}

static
HRESULT
XaWmaDecodeFrame(
    _Inout_ XaWmaDecoder* Decoder)
{
    UINT32 Channels = Decoder->Layout.Channels;
    HRESULT hr;

    for (UINT32 Index = 0; Index < Channels; Index++)
        Decoder->Channels[Index].Tiles.Index = 0;

    /* The first frame of a packet says which tile came before it and which one starts it */
    if (Decoder->ReadTileHeader)
    {
        Decoder->ReadTileHeader = FALSE;
        if (Decoder->Standard.Tiling)
        {
            INT32 Previous = 0;
            INT32 Current = 0;

            hr = XaWmaReadTileSize(Decoder, &Previous);
            if (SUCCEEDED(hr))
                hr = XaWmaReadTileSize(Decoder, &Current);
            if (FAILED(hr))
                return hr;
            for (UINT32 Index = 0; Index < Channels; Index++)
            {
                XaWmaTiling* Tiles = &Decoder->Channels[Index].Tiles;

                Tiles->Sizes[0] = Previous;
                Tiles->Sizes[1] = Current;
                Tiles->Count = 1;
                Tiles->Covered = 0;
            }
        }
    }
    for (UINT32 Index = 0; Index < Channels; Index++)
        Decoder->Channels[Index].Fill = 0;

    do
    {
        hr = XaWmaDecodeTile(Decoder);
        if (FAILED(hr))
            return hr;
        for (UINT32 Index = 0; Index < Channels; Index++)
        {
            XaWmaChannel* Channel = &Decoder->Channels[Index];

            XaWmaOverlap(Channel);
            if (Channel->Tiles.Index >= Channel->Tiles.Count)
                return XA_WMA_E_CORRUPT;
            Channel->Tiles.Index += 1;
        }
        if (Decoder->FirstTile)
        {
            XaWmaSetDiscard(Decoder);
            Decoder->FirstTile = FALSE;
        }
    } while (!Decoder->LastTile);

    return XaWmaFinishFrame(Decoder);
}

/* Sizes the WMA Pro buffers and tables of a decoder whose layout is set */
static
HRESULT
XaWmaCreatePro(
    _Inout_ XaWmaDecoder* Decoder)
{
    HRESULT hr = S_OK;

    Decoder->FrameSize = static_cast<INT32>(Decoder->Layout.FrameSamples);
    Decoder->MinTile = Decoder->FrameSize / static_cast<INT32>(Decoder->Pro.MaxSplits);
    Decoder->Levels = XaMin(XaWmaLog2(Decoder->Pro.MaxSplits) + 1, static_cast<UINT32>(XA_WMA_MAX_LEVELS));
    if (Decoder->MinTile < XA_WMA_XFORM_MIN_SIZE)
        return XA_WMA_E_FORMAT;
    XaWmaBuildProBands(Decoder);
    for (UINT32 Index = 0; Index < Decoder->Layout.Channels && SUCCEEDED(hr); Index++)
    {
        Decoder->Channels[Index].Pcm = new (std::nothrow) float[3 * Decoder->FrameSize / 2];
        if (Decoder->Channels[Index].Pcm == nullptr)
            hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = XaWmaReaderCreate(Decoder);
    if (SUCCEEDED(hr))
        hr = XaWmaCreateTransform(static_cast<UINT32>(Decoder->MinTile), static_cast<UINT32>(Decoder->FrameSize),
                                  &Decoder->Transform);
    return hr;
}

HRESULT
XaWmaCreate(
    _In_ const WAVEFORMATEX* Format,
    _In_ XaWmaPull Pull,
    _In_ void* Context,
    _Outptr_ XaWmaDecoder** Decoder)
{
    XaWmaDecoder* Created;
    XaWmaLayout Layout;
    HRESULT hr = S_OK;

    *Decoder = nullptr;
    if (!XaWmaDescribe(Format, &Layout))
    {
        /* A WMA Pro header the rate table has no entry for only drains, with the header's packet size */
        if (XaFormatTag(Format) != XA_TAG_WMA3)
            return XA_WMA_E_FORMAT;
        memset(&Layout, 0, sizeof(Layout));
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
    Created->Pull = Pull;
    Created->Context = Context;
    if (Layout.Version <= XA_WMA_STANDARD_MAX)
        XaWmaSetupStandard(&Created->Layout, &Created->Standard);
    else
        XaWmaSetupPro(&Created->Layout, &Created->Pro);

    if (XaWmaDecodable(Created) && Layout.Version == XA_WMA_PRO)
    {
        hr = XaWmaCreatePro(Created);
        if (FAILED(hr))
        {
            XaWmaFree(Created);
            return hr;
        }
        XaWmaRestart(Created);
    }
    else if (XaWmaDecodable(Created))
    {
        XaWmaStandard* Standard = &Created->Standard;
        UINT32 BookIndex = static_cast<UINT32>(Standard->RateClass) - 1;

        Created->FrameSize = static_cast<INT32>(Layout.FrameSamples);
        Created->MinTile = Created->FrameSize / static_cast<INT32>(Standard->MaxTiles);
        Created->Levels = XaMin(XaWmaLog2(Standard->MaxTiles) + 1, static_cast<UINT32>(XA_WMA_MAX_LEVELS));
        Created->TileCodeBits = XaWmaLog2(XaWmaLog2(Standard->MaxTiles)) + 1;
        Created->RunBits = Standard->FrameLog2;
        Created->MidBook = &XaWmaBooks[BookIndex][0];
        Created->SideBook = &XaWmaBooks[BookIndex][1];
        XaWmaBuildBands(Created);
        hr = XaWmaSetupNoise(Created);
        for (UINT32 Index = 0; Index < Layout.Channels && SUCCEEDED(hr); Index++)
        {
            XaWmaChannel* Channel = &Created->Channels[Index];

            Channel->Book = Created->MidBook;
            Channel->Pcm = new (std::nothrow) float[3 * Created->FrameSize / 2];
            Channel->Levels = new (std::nothrow) INT32[Created->FrameSize];
            if (!Standard->Bark)
                Channel->Weights = new (std::nothrow) float[Created->FrameSize];
            if (Channel->Pcm == nullptr || Channel->Levels == nullptr || (!Standard->Bark && Channel->Weights == nullptr))
                hr = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hr))
            hr = XaWmaReaderCreate(Created);
        if (SUCCEEDED(hr))
            hr = XaWmaCreateTransform(static_cast<UINT32>(Created->MinTile), static_cast<UINT32>(Created->FrameSize),
                                      &Created->Transform);
        if (FAILED(hr))
        {
            XaWmaFree(Created);
            return hr;
        }
        XaWmaRestart(Created);
    }
    *Decoder = Created;
    return S_OK;
}

VOID
XaWmaFree(
    _In_opt_ XaWmaDecoder* Decoder)
{
    if (Decoder == nullptr)
        return;
    XaWmaReaderFree(Decoder);
    XaWmaFreeTransform(Decoder->Transform);
    for (UINT32 Index = 0; Index < XA_WMA_MAX_CHANNELS; Index++)
    {
        delete[] Decoder->Channels[Index].Pcm;
        delete[] Decoder->Channels[Index].Levels;
        delete[] Decoder->Channels[Index].Weights;
    }
    delete Decoder;
}

UINT32
XaWmaPacketBytes(
    _In_ const XaWmaDecoder* Decoder)
{
    return Decoder->Layout.PacketBytes;
}

BOOLEAN
XaWmaDrainsPerBuffer(
    _In_ const XaWmaDecoder* Decoder)
{
    return Decoder->Layout.Version != XA_WMA_PRO;
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
    if (XaWmaDecodable(Decoder))
        XaWmaRestart(Decoder);
}

HRESULT
XaWmaDecode(
    _Inout_ XaWmaDecoder* Decoder,
    _Out_ UINT32* Frames,
    _Out_ XaWmaState* State)
{
    *Frames = 0;
    if (!XaWmaDecodable(Decoder))
    {
        XaWmaInput Input;

        do
        {
            Decoder->Pull(Decoder->Context, &Input);
        } while (!Input.Last);
        *State = XaWmaIdle;
        return S_OK;
    }

    for (;;)
    {
        HRESULT hr;

        if (Decoder->End > Decoder->Read)
        {
            *Frames = static_cast<UINT32>(Decoder->End - Decoder->Read);
            *State = XaWmaHasPcm;
            return S_OK;
        }
        if (Decoder->Layout.Version == XA_WMA_PRO)
        {
            if (Decoder->Seeking)
            {
                if (!XaWmaProSeek(Decoder))
                {
                    *State = XaWmaIdle;
                    return S_OK;
                }
                Decoder->Seeking = FALSE;
            }
            hr = XaWmaDecodeProFrame(Decoder);
            if (hr == XA_WMA_E_STARVED)
            {
                *State = XaWmaIdle;
                return S_OK;
            }
            if (FAILED(hr))
            {
                XaTrace(L"WMA Pro frame failed %08lX, restarting at the next packet", hr);
                XaWmaRestart(Decoder);
            }
            continue;
        }
        if (Decoder->FramesLeft == 0)
        {
            if (!XaWmaNextPacket(Decoder))
            {
                *State = XaWmaIdle;
                return S_OK;
            }
            continue;
        }

        hr = XaWmaDecodeFrame(Decoder);
        if (hr == XA_WMA_E_STARVED)
        {
            *State = XaWmaIdle;
            return S_OK;
        }
        if (FAILED(hr))
        {
            /* A broken frame starts the stream over from the next packet, as a reset does */
            XaTrace(L"xWMA frame failed %08lX, restarting at the next packet", hr);
            XaWmaRestart(Decoder);
            continue;
        }
        Decoder->FramesLeft -= 1;
    }
}

HRESULT
XaWmaReadPcm(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Frames,
    _Out_writes_(_Inexpressible_("Frames times channels")) SHORT* Output,
    _Out_ UINT32* Produced,
    _Out_ XaWmaState* State)
{
    INT32 Frame = Decoder->FrameSize;
    UINT32 Count;

    *Produced = 0;
    *State = XaWmaWantsDecode;
    if (!XaWmaDecodable(Decoder) || Decoder->End <= Decoder->Read)
        return S_OK;

    Count = XaMin(XaMin(Frames, static_cast<UINT32>(Decoder->End - Decoder->Read)), static_cast<UINT32>(XA_WMA_PCM_MAX));
    XaWmaStorePcm(Decoder, Count, Output);
    Decoder->Read += static_cast<INT32>(Count);
    if (Decoder->Read >= Frame)
    {
        XaWmaShiftPcm(Decoder);
        if (Decoder->Read < 0 || Decoder->Read >= Frame / 2)
            return E_FAIL;
    }
    *Produced = Count;
    if (Decoder->End > Decoder->Read)
        *State = XaWmaHasPcm;
    return S_OK;
}
