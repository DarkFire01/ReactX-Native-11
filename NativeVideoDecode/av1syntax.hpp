/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AV1 OBU, sequence header and uncompressed frame header syntax (AV1 spec 5)
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

#include <vector>

constexpr UINT Av1NumRefFrames = 8;
constexpr UINT Av1RefsPerFrame = 7;
constexpr UINT Av1MaxSegments = 8;
constexpr UINT Av1SegLevels = 8;
constexpr UINT Av1MaxTileCols = 64;
constexpr UINT Av1MaxTileRows = 64;
constexpr UINT Av1PrimaryRefNone = 7;
constexpr UINT Av1SwitchableFilter = 4;
constexpr INT32 Av1WarpedModelPrecBits = 16;

enum Av1ObuType : UINT8
{
    Av1ObuSequenceHeader = 1,
    Av1ObuTemporalDelimiter = 2,
    Av1ObuFrameHeader = 3,
    Av1ObuTileGroup = 4,
    Av1ObuMetadata = 5,
    Av1ObuFrame = 6,
    Av1ObuRedundantFrameHeader = 7,
    Av1ObuTileList = 8,
    Av1ObuPadding = 15,
};

enum Av1FrameType : UINT8
{
    Av1KeyFrame = 0,
    Av1InterFrame = 1,
    Av1IntraOnlyFrame = 2,
    Av1SwitchFrame = 3,
};

enum Av1MotionType : UINT8
{
    Av1Identity = 0,
    Av1Translation = 1,
    Av1RotZoom = 2,
    Av1Affine = 3,
};

struct Av1Obu
{
    UINT8 Type;
    UINT8 TemporalId;
    UINT8 SpatialId;
    const UINT8* Payload;
    SIZE_T Size;
};

struct Av1SequenceHeader
{
    BOOLEAN IsValid;
    UINT8 Profile;
    BOOLEAN StillPicture;
    BOOLEAN ReducedStillPictureHeader;
    BOOLEAN TimingInfoPresent;
    UINT32 NumUnitsInDisplayTick;
    UINT32 TimeScale;
    BOOLEAN EqualPictureInterval;
    BOOLEAN DecoderModelInfoPresent;
    UINT8 BufferRemovalTimeLength;
    UINT8 FramePresentationTimeLength;
    UINT8 OperatingPoints;
    UINT16 OperatingPointIdc[32];
    UINT8 SeqLevelIdx[32];
    UINT8 SeqTier[32];
    BOOLEAN DecoderModelPresentForOp[32];

    UINT8 FrameWidthBits;
    UINT8 FrameHeightBits;
    UINT32 MaxFrameWidth;
    UINT32 MaxFrameHeight;
    BOOLEAN FrameIdNumbersPresent;
    UINT8 DeltaFrameIdLength;
    UINT8 FrameIdLength;

    BOOLEAN Use128x128Superblock;
    BOOLEAN EnableFilterIntra;
    BOOLEAN EnableIntraEdgeFilter;
    BOOLEAN EnableInterintraCompound;
    BOOLEAN EnableMaskedCompound;
    BOOLEAN EnableWarpedMotion;
    BOOLEAN EnableDualFilter;
    BOOLEAN EnableOrderHint;
    BOOLEAN EnableJntComp;
    BOOLEAN EnableRefFrameMvs;
    UINT8 SeqForceScreenContentTools;
    UINT8 SeqForceIntegerMv;
    UINT8 OrderHintBits;
    BOOLEAN EnableSuperres;
    BOOLEAN EnableCdef;
    BOOLEAN EnableRestoration;

    UINT8 BitDepth;
    BOOLEAN MonoChrome;
    UINT8 NumPlanes;
    BOOLEAN ColorDescriptionPresent;
    UINT8 ColorPrimaries;
    UINT8 TransferCharacteristics;
    UINT8 MatrixCoefficients;
    BOOLEAN ColorRange;
    BOOLEAN SubsamplingX;
    BOOLEAN SubsamplingY;
    UINT8 ChromaSamplePosition;
    BOOLEAN SeparateUvDeltaQ;
    BOOLEAN FilmGrainParamsPresent;
};

struct Av1FilmGrain
{
    BOOLEAN ApplyGrain;
    UINT16 GrainSeed;
    BOOLEAN UpdateGrain;
    UINT8 NumYPoints;
    UINT8 PointYValue[14];
    UINT8 PointYScaling[14];
    BOOLEAN ChromaScalingFromLuma;
    UINT8 NumCbPoints;
    UINT8 PointCbValue[10];
    UINT8 PointCbScaling[10];
    UINT8 NumCrPoints;
    UINT8 PointCrValue[10];
    UINT8 PointCrScaling[10];
    UINT8 GrainScalingMinus8;
    UINT8 ArCoeffLag;
    UINT8 ArCoeffsYPlus128[24];
    UINT8 ArCoeffsCbPlus128[25];
    UINT8 ArCoeffsCrPlus128[25];
    UINT8 ArCoeffShiftMinus6;
    UINT8 GrainScaleShift;
    UINT8 CbMult;
    UINT8 CbLumaMult;
    UINT16 CbOffset;
    UINT8 CrMult;
    UINT8 CrLumaMult;
    UINT16 CrOffset;
    BOOLEAN OverlapFlag;
    BOOLEAN ClipToRestrictedRange;
};

/* What the reference update process (spec 7.20) keeps per slot, as far as parsing needs it */
struct Av1RefSlot
{
    BOOLEAN IsValid;
    Av1FrameType FrameType;
    UINT32 UpscaledWidth;
    UINT32 FrameWidth;
    UINT32 FrameHeight;
    UINT32 RenderWidth;
    UINT32 RenderHeight;
    UINT32 OrderHint;
    INT32 GmParams[Av1NumRefFrames][6];
    INT8 LoopFilterRefDeltas[Av1NumRefFrames];
    INT8 LoopFilterModeDeltas[2];
    BOOLEAN FeatureEnabled[Av1MaxSegments][Av1SegLevels];
    INT16 FeatureData[Av1MaxSegments][Av1SegLevels];
    Av1FilmGrain FilmGrain;
};

struct Av1FrameHeader
{
    BOOLEAN ShowExistingFrame;
    UINT8 FrameToShowMapIdx;
    Av1FrameType FrameType;
    BOOLEAN FrameIsIntra;
    BOOLEAN ShowFrame;
    BOOLEAN ShowableFrame;
    BOOLEAN ErrorResilientMode;
    BOOLEAN DisableCdfUpdate;
    BOOLEAN AllowScreenContentTools;
    BOOLEAN ForceIntegerMv;
    UINT32 CurrentFrameId;
    BOOLEAN FrameSizeOverride;
    UINT32 OrderHint;
    UINT8 PrimaryRefFrame;
    UINT8 RefreshFrameFlags;
    BOOLEAN AllowIntrabc;
    INT8 RefFrameIdx[Av1RefsPerFrame];
    BOOLEAN AllowHighPrecisionMv;
    UINT8 InterpolationFilter;
    BOOLEAN IsMotionModeSwitchable;
    BOOLEAN UseRefFrameMvs;
    BOOLEAN DisableFrameEndUpdateCdf;

    UINT32 FrameWidth;
    UINT32 FrameHeight;
    UINT32 UpscaledWidth;
    UINT32 RenderWidth;
    UINT32 RenderHeight;
    BOOLEAN UseSuperres;
    UINT8 SuperresDenom;
    UINT32 MiCols;
    UINT32 MiRows;

    UINT32 TileCols;
    UINT32 TileRows;
    UINT32 TileColsLog2;
    UINT32 TileRowsLog2;
    UINT32 MiColStarts[Av1MaxTileCols + 1];
    UINT32 MiRowStarts[Av1MaxTileRows + 1];
    UINT32 ContextUpdateTileId;
    UINT32 TileSizeBytes;

    UINT8 BaseQIdx;
    INT8 DeltaQYDc;
    INT8 DeltaQUDc;
    INT8 DeltaQUAc;
    INT8 DeltaQVDc;
    INT8 DeltaQVAc;
    BOOLEAN UsingQmatrix;
    UINT8 QmY;
    UINT8 QmU;
    UINT8 QmV;
    BOOLEAN DeltaQPresent;
    UINT8 DeltaQRes;
    BOOLEAN DeltaLfPresent;
    UINT8 DeltaLfRes;
    BOOLEAN DeltaLfMulti;

    BOOLEAN SegmentationEnabled;
    BOOLEAN SegmentationUpdateMap;
    BOOLEAN SegmentationTemporalUpdate;
    BOOLEAN SegmentationUpdateData;
    BOOLEAN FeatureEnabled[Av1MaxSegments][Av1SegLevels];
    INT16 FeatureData[Av1MaxSegments][Av1SegLevels];
    BOOLEAN CodedLossless;
    BOOLEAN AllLossless;

    UINT8 LoopFilterLevel[4];
    UINT8 LoopFilterSharpness;
    BOOLEAN LoopFilterDeltaEnabled;
    BOOLEAN LoopFilterDeltaUpdate;
    INT8 LoopFilterRefDeltas[Av1NumRefFrames];
    INT8 LoopFilterModeDeltas[2];

    UINT8 CdefDampingMinus3;
    UINT8 CdefBits;
    UINT8 CdefYPriStrength[8];
    UINT8 CdefYSecStrength[8];
    UINT8 CdefUvPriStrength[8];
    UINT8 CdefUvSecStrength[8];

    UINT8 FrameRestorationType[3];
    UINT8 LoopRestorationSizeLog2[3];

    UINT8 TxMode;
    BOOLEAN ReferenceSelect;
    BOOLEAN SkipModePresent;
    BOOLEAN AllowWarpedMotion;
    BOOLEAN ReducedTxSet;

    UINT8 GmType[Av1NumRefFrames];
    INT32 GmParams[Av1NumRefFrames][6];
    BOOLEAN GmInvalid[Av1NumRefFrames];

    Av1FilmGrain FilmGrain;

    /* Bytes of the frame header including its trailing byte alignment */
    SIZE_T HeaderBytes;
};

/* A tile's data inside a tile group OBU payload */
struct Av1Tile
{
    SIZE_T Offset;
    SIZE_T Size;
    UINT16 Row;
    UINT16 Column;
};

/* Reads one sized OBU at Offset and advances past it */
BOOLEAN
Av1ReadObu(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ SIZE_T* Offset,
    _Out_ Av1Obu* Obu);

BOOLEAN
Av1ParseSequenceHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _Out_ Av1SequenceHeader* Sequence);

BOOLEAN
Av1ParseFrameHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_ const Av1SequenceHeader& Sequence,
    _In_reads_(Av1NumRefFrames) const Av1RefSlot* Refs,
    _In_ UINT8 TemporalId,
    _In_ UINT8 SpatialId,
    _Out_ Av1FrameHeader* Header);

/* Appends the tiles of one tile group; Start skips a frame header inside an OBU_FRAME */
BOOLEAN
Av1ParseTileGroup(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_ SIZE_T Start,
    _In_ const Av1FrameHeader& Header,
    _Inout_ std::vector<Av1Tile>& Tiles,
    _Out_ BOOLEAN* IsLastGroup);

PCSTR
Av1FrameTypeName(
    _In_ Av1FrameType Type);
