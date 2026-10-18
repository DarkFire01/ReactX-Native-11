/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     H.264 parameter set and slice header syntax (ITU-T H.264 7.3)
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

#include <vector>

enum H264NalType : UINT8
{
    H264NalSlice = 1,
    H264NalSliceIdr = 5,
    H264NalSei = 6,
    H264NalSps = 7,
    H264NalPps = 8,
    H264NalAccessUnitDelimiter = 9,
    H264NalEndOfSequence = 10,
    H264NalEndOfStream = 11,
    H264NalFiller = 12,
};

enum H264SliceType : UINT8
{
    H264SliceP = 0,
    H264SliceB = 1,
    H264SliceI = 2,
    H264SliceSp = 3,
    H264SliceSi = 4,
};

constexpr UINT H264MaxSpsCount = 32;
constexpr UINT H264MaxPpsCount = 256;
constexpr UINT H264MaxReferenceFrames = 16;

struct H264Sps
{
    BOOLEAN IsValid;
    UINT8 ProfileIdc;
    UINT8 ConstraintFlags;
    UINT8 LevelIdc;
    UINT8 SpsId;

    UINT8 ChromaFormatIdc;
    BOOLEAN SeparateColourPlane;
    UINT8 BitDepthLumaMinus8;
    UINT8 BitDepthChromaMinus8;
    BOOLEAN QpPrimeYZeroTransformBypass;

    BOOLEAN ScalingMatrixPresent;
    BOOLEAN ScalingListPresent[12];
    UINT8 ScalingList4x4[6][16];
    UINT8 ScalingList8x8[6][64];
    BOOLEAN UseDefault4x4[6];
    BOOLEAN UseDefault8x8[6];

    UINT8 Log2MaxFrameNumMinus4;
    UINT8 PicOrderCntType;
    UINT8 Log2MaxPicOrderCntLsbMinus4;
    BOOLEAN DeltaPicOrderAlwaysZero;
    INT32 OffsetForNonRefPic;
    INT32 OffsetForTopToBottomField;
    UINT8 NumRefFramesInPicOrderCntCycle;
    INT32 OffsetForRefFrame[256];

    UINT8 MaxNumRefFrames;
    BOOLEAN GapsInFrameNumAllowed;
    UINT32 PicWidthInMbsMinus1;
    UINT32 PicHeightInMapUnitsMinus1;
    BOOLEAN FrameMbsOnly;
    BOOLEAN MbAdaptiveFrameField;
    BOOLEAN Direct8x8Inference;

    BOOLEAN FrameCropping;
    UINT32 CropLeft;
    UINT32 CropRight;
    UINT32 CropTop;
    UINT32 CropBottom;

    BOOLEAN VuiPresent;
    UINT16 SarWidth;
    UINT16 SarHeight;
    BOOLEAN VideoFullRange;
    BOOLEAN ColourDescriptionPresent;
    UINT8 ColourPrimaries;
    UINT8 TransferCharacteristics;
    UINT8 MatrixCoefficients;
    BOOLEAN TimingInfoPresent;
    UINT32 NumUnitsInTick;
    UINT32 TimeScale;
    BOOLEAN BitstreamRestriction;
    UINT32 MaxNumReorderFrames;
    UINT32 MaxDecFrameBuffering;

    /* Derived */
    UINT32 WidthInMbs;
    UINT32 HeightInMbs;
    UINT32 CodedWidth;
    UINT32 CodedHeight;
    RECT Crop;
    UINT32 MaxFrameNum;
    UINT32 MaxDpbFrames;
    UINT32 ReorderFrames;
};

struct H264Pps
{
    BOOLEAN IsValid;
    UINT8 PpsId;
    UINT8 SpsId;
    BOOLEAN EntropyCodingMode;
    BOOLEAN BottomFieldPicOrderInFramePresent;
    UINT32 NumSliceGroupsMinus1;
    UINT8 NumRefIdxL0DefaultActiveMinus1;
    UINT8 NumRefIdxL1DefaultActiveMinus1;
    BOOLEAN WeightedPred;
    UINT8 WeightedBipredIdc;
    INT8 PicInitQpMinus26;
    INT8 PicInitQsMinus26;
    INT8 ChromaQpIndexOffset;
    BOOLEAN DeblockingFilterControlPresent;
    BOOLEAN ConstrainedIntraPred;
    BOOLEAN RedundantPicCntPresent;
    BOOLEAN Transform8x8Mode;
    BOOLEAN ScalingMatrixPresent;
    BOOLEAN ScalingListPresent[12];
    UINT8 ScalingList4x4[6][16];
    UINT8 ScalingList8x8[6][64];
    BOOLEAN UseDefault4x4[6];
    BOOLEAN UseDefault8x8[6];
    INT8 SecondChromaQpIndexOffset;
};

enum H264MmcoOperation : UINT8
{
    H264MmcoEnd = 0,
    H264MmcoForgetShort = 1,
    H264MmcoForgetLong = 2,
    H264MmcoShortToLong = 3,
    H264MmcoSetMaxLongIndex = 4,
    H264MmcoForgetAll = 5,
    H264MmcoCurrentToLong = 6,
};

struct H264Mmco
{
    H264MmcoOperation Operation;
    UINT32 DifferenceOfPicNumsMinus1;
    UINT32 LongTermPicNum;
    UINT32 LongTermFrameIdx;
    UINT32 MaxLongTermFrameIdxPlus1;
};

struct H264SliceHeader
{
    UINT8 NalRefIdc;
    UINT8 NalType;
    UINT32 FirstMbInSlice;
    H264SliceType SliceType;
    UINT8 PpsId;
    UINT32 FrameNum;
    BOOLEAN FieldPic;
    BOOLEAN BottomField;
    UINT32 IdrPicId;
    UINT32 PicOrderCntLsb;
    INT32 DeltaPicOrderCntBottom;
    INT32 DeltaPicOrderCnt[2];

    BOOLEAN NoOutputOfPriorPics;
    BOOLEAN LongTermReference;
    BOOLEAN AdaptiveRefPicMarking;
    std::vector<H264Mmco> Mmcos;
};

/* One NAL unit inside an Annex B buffer: Offset is the start code, Payload the header byte */
struct H264NalUnit
{
    SIZE_T StartCodeOffset;
    SIZE_T PayloadOffset;
    SIZE_T PayloadSize;
    UINT8 Type;
    UINT8 RefIdc;
};

VOID
H264SplitAnnexB(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ std::vector<H264NalUnit>& Units);

BOOLEAN
H264ParseSps(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _Out_ H264Sps* Sps);

BOOLEAN
H264ParsePps(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_reads_(H264MaxSpsCount) const H264Sps* SpsTable,
    _Out_ H264Pps* Pps);

BOOLEAN
H264ParseSliceHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_reads_(H264MaxSpsCount) const H264Sps* SpsTable,
    _In_reads_(H264MaxPpsCount) const H264Pps* PpsTable,
    _Out_ H264SliceHeader* Header);

/* Applies the fall-back rules of Table 7-2 and writes lists in the order DXVA expects */
VOID
H264ResolveScalingLists(
    _In_ const H264Sps& Sps,
    _In_ const H264Pps& Pps,
    _Out_writes_(6 * 16) UINT8 (*Lists4x4)[16],
    _Out_writes_(2 * 64) UINT8 (*Lists8x8)[64]);

PCSTR
H264ProfileName(
    _In_ UINT8 ProfileIdc);
