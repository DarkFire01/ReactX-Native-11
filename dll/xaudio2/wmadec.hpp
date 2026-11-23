/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Internal state of the xWMA decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "wma.hpp"

/* Bitstream generations: 1 and 2 are WMA Standard, 3 is WMA Pro */
#define XA_WMA_STANDARD_MAX     2
#define XA_WMA_PRO              3

/* Encoder option bits of WMA Standard */
#define XA_WMA_OPT_BARK         0x0001
#define XA_WMA_OPT_SUPERFRAME   0x0002
#define XA_WMA_OPT_TILING       0x0004
#define XA_WMA_OPT_TILE_SHIFT   3
#define XA_WMA_OPT_LPC          0x0020

/* Sample rate class of WMA Standard, from the bits spent per sample */
enum XaWmaRateClass
{
    XaWmaLowRate = 1,
    XaWmaMidRate = 2,
    XaWmaHighRate = 3,
};

/* What the encoder settled on for a format, as the encoder's rate table gives it */
struct XaWmaLayout
{
    UINT32 Version;
    UINT32 Channels;
    UINT32 Rate;
    UINT32 Bits;
    UINT32 BitRate;
    UINT32 BytesPerSecond;
    UINT32 PacketBytes;
    UINT32 Options;
    UINT32 FrameSamples;
};

/* Fixed parameters of a WMA Standard stream */
struct XaWmaStandard
{
    float BitsPerSample;
    float WeightedBitsPerSample;
    UINT32 PacketBits;
    UINT32 OffsetBits;
    BOOLEAN SuperFrames;
    BOOLEAN Tiling;
    BOOLEAN Bark;
    BOOLEAN Lpc;
    UINT32 MaxTiles;
    UINT32 TileSizeBits;
    UINT32 SmallestTiles[3];
    UINT32 FrameLog2;
    UINT32 LowCutoff;
    UINT32 HighCutoff;
    XaWmaRateClass RateClass;
};

struct XaWmaDecoder
{
    XaWmaLayout Layout;
    XaWmaStandard Standard;
    XaWmaPull Pull;
    void* Context;
    XaWmaState State;
};

BOOLEAN
XaWmaDescribe(
    _In_ const WAVEFORMATEX* Format,
    _Out_ XaWmaLayout* Layout);

VOID
XaWmaSetupStandard(
    _In_ const XaWmaLayout* Layout,
    _Out_ XaWmaStandard* Standard);
