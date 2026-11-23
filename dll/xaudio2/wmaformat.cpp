/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA formats: the encoding a header stands for and the stream parameters it implies
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xaudio2.hpp"
#include "wmadec.hpp"
#include "wmatables.hpp"

/* Rate table flags */
#define XA_WMA_RATE_DECODABLE   0x0002
#define XA_WMA_RATE_STANDARD    0x0008
#define XA_WMA_RATE_PRO         0x0010

/* A bit rate the tables use to mean a lossless stream */
#define XA_WMA_LOSSLESS_RATE    128

static
UINT32
XaWmaFloorLog2(
    _In_ UINT32 Value)
{
    UINT32 Result = 0;

    while ((Value >> (Result + 1)) != 0)
        Result += 1;
    return Result;
}

/* Rounds half away from zero the way the encoder does */
static
INT32
XaWmaRound(
    _In_ float Value)
{
    return static_cast<INT32>(Value >= 0.0f ? Value + 0.5f : Value - 0.5f);
}

static
UINT32
XaWmaVersionOf(
    _In_ WORD Tag)
{
    switch (Tag)
    {
        case 0x0160:
            return 1;
        case 0x0161:
        case 0x0165:
            return 2;
        case 0x0162:
        case 0x0163:
        case 0x0164:
        case 0x0166:
        case 0x0167:
            return 3;
        default:
            return 0;
    }
}

/* Samples each frame codes; low bit rates double it until a frame holds at least a byte */
static
UINT32
XaWmaFrameSamplesFor(
    _In_ UINT32 Rate,
    _In_ UINT32 BitRate,
    _In_ UINT32 Version,
    _In_ UINT32 Options)
{
    UINT32 Samples;

    if ((BitRate == 0 && Version < XA_WMA_PRO) || Version > XA_WMA_PRO || static_cast<INT32>(Rate) <= 0)
        return 0;

    if (Rate <= 16000)
        Samples = 512;
    else if (Rate <= 22050)
        Samples = 1024;
    else if (Rate <= 32000)
        Samples = Version == 1 ? 1024 : 2048;
    else if (Rate <= 48000)
        Samples = 2048;
    else if (Rate <= 96000)
        Samples = 4096;
    else
        Samples = 8192;

    if (Version == XA_WMA_PRO)
    {
        switch (Options & 6)
        {
            case 2:
                Samples *= 2;
                break;
            case 4:
                Samples >>= 1;
                break;
            case 6:
                Samples >>= 2;
                break;
        }
        return Samples;
    }
    while ((Samples * BitRate + Rate / 2) / Rate + 7 < 8)
        Samples *= 2;
    return Samples;
}

static
UINT32
XaWmaPacketBytesFor(
    _In_ UINT32 Rate,
    _In_ UINT32 Channels,
    _In_ UINT32 BitRate,
    _In_ UINT32 PacketFrames,
    _In_ UINT32 Version,
    _In_ UINT32 Options)
{
    UINT32 Samples;
    UINT32 Bits;
    UINT32 Bytes;

    if (BitRate == XA_WMA_LOSSLESS_RATE)
        return 1;
    Samples = XaWmaFrameSamplesFor(Rate, BitRate, Version, Options) * PacketFrames;
    Bits = static_cast<UINT32>((Rate + static_cast<UINT64>(Samples) * BitRate - 1) / Rate) + 7;
    Bytes = Bits >> 3;

    /* 44.1 kHz stereo at 32 kbps keeps its packets under 32000 bits per second */
    if (Rate == 44100 && Channels == 2 && Version == 2 && BitRate == 32000 && Samples != 0)
    {
        UINT64 Limit = static_cast<UINT64>(Samples) - 1 + static_cast<UINT64>(Bits & ~7u) * Rate;

        if (Limit / Samples + 7 >= 32000)
        {
            for (;;)
            {
                UINT64 Trial = static_cast<UINT64>(Samples) - 1 + static_cast<UINT64>(8 * Bytes - 8) * Rate;

                Bytes -= 1;
                if (Trial / Samples + 7 < 32000)
                    break;
            }
        }
    }
    return Bytes;
}

static
UINT32
XaWmaBytesPerSecondFor(
    _In_ UINT32 Rate,
    _In_ UINT32 Channels,
    _In_ UINT32 BitRate,
    _In_ UINT32 PacketFrames,
    _In_ UINT32 Version,
    _In_ UINT32 Options)
{
    UINT64 Samples = static_cast<UINT64>(XaWmaFrameSamplesFor(Rate, BitRate, Version, Options)) * PacketFrames;
    UINT64 Packet = static_cast<UINT16>(XaWmaPacketBytesFor(Rate, Channels, BitRate, PacketFrames, Version, Options));

    if (Samples == 0)
        return 0;
    return static_cast<UINT32>((static_cast<INT64>(Samples + Packet * Rate * 8 - 1) / static_cast<INT64>(Samples) + 7) >> 3);
}

/* The rate table entry nearest the header's byte rate */
static
const XaWmaRateEntry*
XaWmaNearestEntry(
    _In_ const WAVEFORMATEX* Format,
    _In_ UINT32 Version)
{
    const XaWmaRateGroup* Group = nullptr;
    const XaWmaRateEntry* Best = nullptr;
    UINT32 Wanted = Format->nAvgBytesPerSec * 8;
    UINT32 Closest = MAXUINT32;
    UINT32 VersionFlag = Version == 2 ? XA_WMA_RATE_STANDARD : Version == XA_WMA_PRO ? XA_WMA_RATE_PRO : 0;

    for (UINT32 Index = 0; Index < ARRAYSIZE(XaWmaRateGroups); Index++)
    {
        if (XaWmaRateGroups[Index].Rate == Format->nSamplesPerSec && XaWmaRateGroups[Index].Channels == Format->nChannels)
        {
            Group = &XaWmaRateGroups[Index];
            break;
        }
    }
    if (Group == nullptr)
        return nullptr;

    for (UINT32 Index = 0; Index < Group->Count; Index++)
    {
        const XaWmaRateEntry* Entry = &Group->Entries[Index];
        INT32 Difference;
        UINT32 Distance;

        if ((Entry->Flags & XA_WMA_RATE_DECODABLE) == 0 || (Entry->Flags & VersionFlag) == 0 || Entry->Bits != 16)
            continue;
        Difference = static_cast<INT32>(Wanted - Entry->BitRate);
        Distance = static_cast<UINT32>(Difference < 0 ? ~Difference : Difference);
        if (Distance == 0)
            return Entry;
        if (Closest > Distance)
        {
            Closest = Distance;
            Best = Entry;
        }
    }
    return Best;
}

static
UINT32
XaWmaStandardOptions(
    _In_ const XaWmaRateEntry* Entry,
    _In_ UINT32 Channels)
{
    UINT32 Options = Entry->Weighting == 1 ? XA_WMA_OPT_BARK : XA_WMA_OPT_LPC;
    INT32 Adjust;

    if (Entry->BitRate == XA_WMA_LOSSLESS_RATE)
        return Options;
    Options |= XA_WMA_OPT_SUPERFRAME | XA_WMA_OPT_TILING;
    Adjust = Entry->BitRate / Channels < 32000 ? -1 : -3;
    Options |= static_cast<UINT32>(static_cast<INT32>(XaWmaFloorLog2(Entry->Subframes)) + Adjust) << XA_WMA_OPT_TILE_SHIFT;
    return Options & 0xFFFF;
}

BOOLEAN
XaWmaDescribe(
    _In_ const WAVEFORMATEX* Format,
    _Out_ XaWmaLayout* Layout)
{
    const XaWmaRateEntry* Entry;
    WORD Tag = XaFormatTag(Format);

    memset(Layout, 0, sizeof(*Layout));
    Layout->Version = XaWmaVersionOf(Tag);
    if (Layout->Version != 2)
        return FALSE;
    Entry = XaWmaNearestEntry(Format, Layout->Version);
    if (Entry == nullptr)
        return FALSE;

    Layout->Channels = Format->nChannels;
    Layout->Rate = Format->nSamplesPerSec;
    Layout->Bits = Entry->Bits;
    Layout->BitRate = Entry->BitRate;
    Layout->Options = XaWmaStandardOptions(Entry, Layout->Channels);
    Layout->BytesPerSecond = XaWmaBytesPerSecondFor(Layout->Rate, Layout->Channels, Entry->BitRate, Entry->PacketFrames,
                                                    Layout->Version, Layout->Options);
    Layout->PacketBytes = static_cast<UINT16>(XaWmaPacketBytesFor(Layout->Rate, Layout->Channels, Entry->BitRate,
                                                                  Entry->PacketFrames, Layout->Version, Layout->Options));

    /* The decoder sizes its frames from the byte rate the header ends up with */
    Layout->FrameSamples = XaWmaFrameSamplesFor(Layout->Rate, Layout->BytesPerSecond * 8, Layout->Version, Layout->Options);
    return Layout->FrameSamples != 0 && Layout->PacketBytes != 0;
}

VOID
XaWmaSetupStandard(
    _In_ const XaWmaLayout* Layout,
    _Out_ XaWmaStandard* Standard)
{
    UINT32 Options = Layout->Options;
    UINT32 Frame = Layout->FrameSamples;
    UINT32 Tiles = 1;

    memset(Standard, 0, sizeof(*Standard));
    Standard->BitsPerSample = static_cast<float>(static_cast<INT32>(Layout->BytesPerSecond)) * 8.0f /
                              static_cast<float>(static_cast<INT32>(Layout->Rate * Layout->Channels));
    Standard->WeightedBitsPerSample = Standard->BitsPerSample;
    if (Layout->Channels == 2)
        Standard->WeightedBitsPerSample = Standard->BitsPerSample * 1.6f;
    else if (Layout->Channels > 2)
        Standard->WeightedBitsPerSample = Standard->BitsPerSample * 1.48f;

    Standard->PacketBits = Layout->PacketBytes * 8;
    Standard->Bark = (Options & XA_WMA_OPT_BARK) != 0;
    Standard->Lpc = (Options & XA_WMA_OPT_LPC) != 0;
    Standard->SuperFrames = (Options & XA_WMA_OPT_SUPERFRAME) != 0;
    Standard->Tiling = Standard->SuperFrames && (Options & XA_WMA_OPT_TILING) != 0;
    if (Standard->Tiling)
    {
        UINT32 Shift = (Options >> XA_WMA_OPT_TILE_SHIFT) & 3;

        Tiles = (Layout->BytesPerSecond / Layout->Channels < 4000 ? 2u : 8u) << Shift;
    }
    Standard->MaxTiles = XaMin(Tiles, 2 * Frame / 256);
    Standard->TileSizeBits = Layout->Version == 1 ? 1 : XaWmaFloorLog2(Standard->MaxTiles) + 1;
    Standard->SmallestTiles[0] = 2 * Frame / Standard->MaxTiles;
    Standard->SmallestTiles[1] = Standard->SmallestTiles[0] / 2;
    Standard->SmallestTiles[2] = Standard->SmallestTiles[0] / 4;
    Standard->FrameLog2 = XaWmaFloorLog2(Frame);
    Standard->LowCutoff = Layout->Version == 1 ? 3 : 0;
    Standard->HighCutoff = Frame - static_cast<UINT32>(static_cast<INT32>(9 * Frame) / 100);

    Standard->RateClass = XaWmaHighRate;
    if (Standard->WeightedBitsPerSample < 0.72f)
    {
        if (static_cast<INT32>(Layout->Rate) >= 32000)
            Standard->RateClass = XaWmaLowRate;
    }
    else if (Standard->WeightedBitsPerSample < 1.16f && static_cast<INT32>(Layout->Rate) >= 32000)
    {
        Standard->RateClass = XaWmaMidRate;
    }

    /* Each packet starts with a sequence number, a frame count and where its first new frame begins */
    Standard->OffsetBits = XaWmaFloorLog2(static_cast<UINT32>(XaWmaRound(static_cast<float>(static_cast<INT32>(Frame)) *
                                                                         Standard->BitsPerSample * 0.125f))) + 2;
}
