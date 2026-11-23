/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Internal state of the xWMA decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "wma.hpp"

struct XaWmaTransform;

/* Bitstream generations: 1 and 2 are WMA Standard, 3 is WMA Pro */
#define XA_WMA_STANDARD_MAX     2
#define XA_WMA_PRO              3

/* Encoder option bits of WMA Standard */
#define XA_WMA_OPT_BARK         0x0001
#define XA_WMA_OPT_SUPERFRAME   0x0002
#define XA_WMA_OPT_TILING       0x0004
#define XA_WMA_OPT_TILE_SHIFT   3
#define XA_WMA_OPT_LPC          0x0020

/* Encoder option bits of WMA Pro */
#define XA_WMA_PRO_OPT_SPLITS   3
#define XA_WMA_PRO_OPT_LENGTH   0x0040
#define XA_WMA_PRO_OPT_DRC      0x0080
#define XA_WMA_PRO_OPT_LONG     0x1000

/* Limits of WMA Standard streams */
#define XA_WMA_MAX_CHANNELS     2
#define XA_WMA_MAX_BANDS        29
#define XA_WMA_MAX_LEVELS       8
#define XA_WMA_MAX_TILES        64

/* Limits of WMA Pro streams */
#define XA_WMA_PRO_BANDS        28
#define XA_WMA_PRO_SUBFRAMES    32

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

/* Fixed parameters of a WMA Pro stream */
struct XaWmaPro
{
    UINT32 MaxSplits;
    UINT32 FrameLengthBits;
    BOOLEAN FrameLength;
    BOOLEAN DrcByte;
    UINT32 TrimBits;
    INT32 BaseQuant;
};

/* Where the frames of one packet sit */
struct XaWmaPacketHead
{
    UINT32 Frames;
    UINT32 Offset;

    /* WMA Pro: a frame starts in the packet */
    BOOLEAN Starts;
};

/* Packets the bit reader holds: the one it reads from and at most one after it */
struct XaWmaReader
{
    BYTE* Store[2];
    UINT32 StoreBits[2];
    UINT32 Held;
    UINT32 Position;
    XaWmaPacketHead Heads[2];
    UINT32 HeadCount;
    BOOLEAN Dry;

    /* Header layout of the stream's packets */
    UINT32 HeaderBits;
    UINT32 OffsetAt;
    UINT32 OffsetBits;

    /* Bits taken since the counter was last cleared, packet headers left out */
    UINT32 Taken;
};

/* A set of channels coded through one inverse transform */
struct XaWmaGroup
{
    INT32 Count;
    BOOLEAN Members[XA_WMA_MAX_CHANNELS];

    /* Which of the two transform passes runs it */
    BOOLEAN Early;
    BOOLEAN Fixed;
    INT32 Kind;
    BOOLEAN BandOn[XA_WMA_PRO_BANDS];
    INT8 Angles[XA_WMA_MAX_CHANNELS * (XA_WMA_MAX_CHANNELS - 1) / 2];
    UINT8 Signs[XA_WMA_MAX_CHANNELS];
    float Matrix[XA_WMA_MAX_CHANNELS * XA_WMA_MAX_CHANNELS];
};

/* Where the WMA Pro coefficient coder is between symbols */
enum XaWmaCoderStep
{
    XaCoderPickBook,
    XaCoderQuad,
    XaCoderPair,
    XaCoderSingle,
    XaCoderLarge,
    XaCoderSigns,
    XaCoderEmit,
    XaCoderRunLevel,
};

struct XaWmaCoder
{
    XaWmaCoderStep Step;
    INT32 Slot;
    INT32 Zeros;
    BOOLEAN Leave;
    INT32 Values[4];
    INT32 Signs[4];
    INT32 Vectors;
};

/* A Huffman code book and the run and level each symbol stands for */
struct XaWmaCodeBook
{
    const UINT16* Tree;
    const UINT16* Runs;
    const UINT16* Levels;
};

/* The tiles of one channel in the frame being decoded; slot 0 is the tile before the frame */
struct XaWmaTiling
{
    INT32 Sizes[XA_WMA_MAX_TILES + 2];
    INT32 Count;
    INT32 Covered;
    INT32 Index;
};

struct XaWmaChannel
{
    XaWmaTiling Tiles;

    /* Window of the current tile */
    INT32 PrevSize;
    INT32 Size;
    INT32 NextSize;
    float TurnSin;
    float TurnCos;
    float TurnPrevSin;
    float TurnPrevCos;
    float TurnStep;

    BOOLEAN Coded;
    BOOLEAN FreshMask;
    BOOLEAN Audible;
    INT32 Mask[XA_WMA_MAX_BANDS];
    INT32 MaskPeak;
    INT32 Width;
    const XaWmaCodeBook* Book;

    /* One and a half frames of output; tiles land at half a frame plus Fill */
    float* Pcm;
    float* Coefs;
    INT32 Fill;

    /* WMA Pro: the mask as sent, at MaskSize, and resampled to the current subframe */
    BOOLEAN HasMask;
    BOOLEAN MaskSent;
    INT32 MaskStep;
    INT32 MaskSize;
    INT32 Resampled[XA_WMA_PRO_BANDS];
    UINT32 QuantShift;
    INT32 CodedLength;
    INT32 Vectors;
    BOOLEAN Grouped;
    BOOLEAN MaskRead;
};

/* The run level symbol last decoded */
struct XaWmaRun
{
    INT32 Run;
    INT32 Level;
    INT32 Sign;
};

struct XaWmaDecoder
{
    XaWmaLayout Layout;
    XaWmaStandard Standard;
    XaWmaPull Pull;
    void* Context;
    XaWmaState State;

    XaWmaReader Reader;
    XaWmaTransform* Transform;
    XaWmaChannel Channels[XA_WMA_MAX_CHANNELS];

    /* Band layout for each tile size, largest first */
    UINT32 Levels;
    INT32 BandCounts[XA_WMA_MAX_LEVELS];
    INT32 BandEdges[XA_WMA_MAX_LEVELS][XA_WMA_MAX_BANDS];

    INT32 FrameSize;
    INT32 MinTile;
    UINT32 TileCodeBits;
    UINT32 RunBits;
    const XaWmaCodeBook* MidBook;
    const XaWmaCodeBook* SideBook;

    /* Stream position */
    UINT32 FramesLeft;
    BOOLEAN SkipOffset;
    BOOLEAN ReadTileHeader;
    BOOLEAN FirstTile;

    /* Tile being decoded */
    BOOLEAN MidSide;
    BOOLEAN LastTile;
    INT32 Bands;
    const INT32* Edges;
    INT32 LowIndex;
    INT32 HighIndex;
    INT32 QuantIndex;
    float QuantStep;
    UINT32 EscapeBits;
    INT32 MaskSize;
    INT32 CoefIndex;
    XaWmaRun RunLevel;

    /* Output in each channel's buffer runs from Read to End */
    INT32 Read;
    INT32 End;
    INT32 Discard;

    /* WMA Pro stream and frame state */
    XaWmaPro Pro;
    BOOLEAN SameTiling;
    BOOLEAN Counted;
    BOOLEAN FrameDone;
    BOOLEAN Seeking;
    BOOLEAN StartTrimmed;
    INT32 StartTrim;
    INT32 EndTrim;
    UINT32 FrameBits;
    UINT32 Picked[XA_WMA_MAX_CHANNELS];
    UINT32 PickedCount;
    INT32 CodedLimit;
    INT32 RunBitsPro;
    UINT8 Resample[XA_WMA_MAX_LEVELS][XA_WMA_MAX_LEVELS][XA_WMA_PRO_BANDS];
    XaWmaGroup Groups[XA_WMA_MAX_CHANNELS];
    INT32 GroupCount;
    XaWmaCoder Coder;
};

BOOLEAN
XaWmaDescribe(
    _In_ const WAVEFORMATEX* Format,
    _Out_ XaWmaLayout* Layout);

VOID
XaWmaSetupStandard(
    _In_ const XaWmaLayout* Layout,
    _Out_ XaWmaStandard* Standard);

VOID
XaWmaSetupPro(
    _In_ const XaWmaLayout* Layout,
    _Out_ XaWmaPro* Pro);

/* WMA Pro frames */
VOID
XaWmaBuildProBands(
    _Inout_ XaWmaDecoder* Decoder);

HRESULT
XaWmaDecodeProFrame(
    _Inout_ XaWmaDecoder* Decoder);

/* Bit reader */
HRESULT
XaWmaReaderCreate(
    _Inout_ XaWmaDecoder* Decoder);

VOID
XaWmaReaderFree(
    _Inout_ XaWmaDecoder* Decoder);

VOID
XaWmaReaderReset(
    _Inout_ XaWmaDecoder* Decoder);

BOOLEAN
XaWmaNextPacket(
    _Inout_ XaWmaDecoder* Decoder);

BOOLEAN
XaWmaProSeek(
    _Inout_ XaWmaDecoder* Decoder);

BOOLEAN
XaWmaGetBits(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Count,
    _Out_ UINT32* Value);

BOOLEAN
XaWmaSkipBits(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ UINT32 Count);

BOOLEAN
XaWmaGetSymbol(
    _Inout_ XaWmaDecoder* Decoder,
    _In_ const UINT16* Tree,
    _Out_ UINT32* Symbol,
    _Out_ UINT32* After);

/* Reconstruction */
VOID
XaWmaBuildBands(
    _Inout_ XaWmaDecoder* Decoder);

VOID
XaWmaSetTurn(
    _Inout_ XaWmaChannel* Channel);

HRESULT
XaWmaReconstruct(
    _Inout_ XaWmaDecoder* Decoder,
    _Inout_ XaWmaChannel* Channel);

VOID
XaWmaOverlap(
    _Inout_ XaWmaChannel* Channel);

VOID
XaWmaStorePcm(
    _In_ const XaWmaDecoder* Decoder,
    _In_ UINT32 Frames,
    _Out_writes_(_Inexpressible_("Frames times channels")) SHORT* Output);
