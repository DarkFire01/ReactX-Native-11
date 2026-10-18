/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AV1 OBU, sequence header and uncompressed frame header syntax (AV1 spec 5)
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "av1syntax.hpp"
#include "bitreader.hpp"

#include <algorithm>
#include <string.h>

/* Reference frame names as indices into the 8 entry per-reference arrays */
enum : UINT
{
    Av1IntraFrame = 0,
    Av1Last = 1,
    Av1Last2 = 2,
    Av1Last3 = 3,
    Av1Golden = 4,
    Av1BwdRef = 5,
    Av1AltRef2 = 6,
    Av1AltRef = 7,
};

constexpr UINT8 Av1SelectScreenContentTools = 2;
constexpr UINT8 Av1SelectIntegerMv = 2;

static const UINT8 SegmentationFeatureBits[Av1SegLevels] = { 8, 6, 6, 6, 6, 3, 0, 0 };
static const BOOLEAN SegmentationFeatureSigned[Av1SegLevels] = { 1, 1, 1, 1, 1, 0, 0, 0 };
static const UINT8 SegmentationFeatureMax[Av1SegLevels] = { 255, 63, 63, 63, 63, 7, 0, 0 };

/* Coded lr_type to FrameRestorationType: NONE, SWITCHABLE, WIENER, SGRPROJ */
static const UINT8 RemapLrType[4] = { 0, 3, 1, 2 };

static const INT8 DefaultLoopFilterRefDeltas[Av1NumRefFrames] = { 1, 0, 0, 0, -1, 0, -1, -1 };

static
UINT
Av1FloorLog2(
    _In_ UINT32 Value)
{
    UINT Result = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Result++;
    }

    return Result;
}

static
UINT32
Av1ReadUvlc(
    _Inout_ BitReader& Reader)
{
    UINT LeadingZeros = 0;

    while (!Reader.ReadBit())
    {
        if (Reader.HasFailed() || ++LeadingZeros >= 32)
            return UINT32_MAX;
    }

    return Reader.ReadBits(LeadingZeros) + (1u << LeadingZeros) - 1;
}

static
INT32
Av1ReadSu(
    _Inout_ BitReader& Reader,
    _In_ UINT Bits)
{
    INT32 Value = (INT32)Reader.ReadBits(Bits);
    INT32 SignMask = 1 << (Bits - 1);

    if (Value & SignMask)
        Value -= 2 * SignMask;
    return Value;
}

static
UINT32
Av1ReadNs(
    _Inout_ BitReader& Reader,
    _In_ UINT32 Count)
{
    UINT Width = Av1FloorLog2(Count) + 1;
    UINT32 Limit = (1u << Width) - Count;
    UINT32 Value = Reader.ReadBits(Width - 1);

    if (Value < Limit)
        return Value;
    return (Value << 1) - Limit + Reader.ReadBit();
}

static
VOID
Av1ByteAlign(
    _Inout_ BitReader& Reader)
{
    SIZE_T Position = Reader.GetPosition();

    if (Position % 8 != 0)
        Reader.SkipBits(8 - Position % 8);
}

BOOLEAN
Av1ReadObu(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ SIZE_T* Offset,
    _Out_ Av1Obu* Obu)
{
    SIZE_T Position = *Offset;
    UINT64 PayloadSize = 0;
    UINT8 Header;
    UINT8 Byte;
    UINT Index;

    if (Position >= Size)
        return FALSE;

    Header = Data[Position++];
    if (Header & 0x80)
        return FALSE;

    Obu->Type = (Header >> 3) & 0xF;
    Obu->TemporalId = 0;
    Obu->SpatialId = 0;
    if (Header & 0x04)
    {
        if (Position >= Size)
            return FALSE;
        Obu->TemporalId = Data[Position] >> 5;
        Obu->SpatialId = (Data[Position] >> 3) & 3;
        Position++;
    }

    if (Header & 0x02)
    {
        /* leb128: seven bits per byte, low group first */
        for (Index = 0; Index < 8; Index++)
        {
            if (Position >= Size)
                return FALSE;
            Byte = Data[Position++];
            PayloadSize |= (UINT64)(Byte & 0x7F) << (Index * 7);
            if (!(Byte & 0x80))
                break;
        }
    }
    else
    {
        PayloadSize = Size - Position;
    }

    if (PayloadSize > Size - Position)
        return FALSE;

    Obu->Payload = Data + Position;
    Obu->Size = (SIZE_T)PayloadSize;
    *Offset = Position + (SIZE_T)PayloadSize;
    return TRUE;
}

static
VOID
Av1ParseColorConfig(
    _Inout_ BitReader& Reader,
    _Inout_ Av1SequenceHeader* Sequence)
{
    BOOLEAN HighBitdepth = Reader.ReadFlag();

    if (Sequence->Profile == 2 && HighBitdepth)
        Sequence->BitDepth = Reader.ReadFlag() ? 12 : 10;
    else
        Sequence->BitDepth = HighBitdepth ? 10 : 8;

    Sequence->MonoChrome = (Sequence->Profile == 1) ? FALSE : Reader.ReadFlag();
    Sequence->NumPlanes = Sequence->MonoChrome ? 1 : 3;

    Sequence->ColorDescriptionPresent = Reader.ReadFlag();
    Sequence->ColorPrimaries = 2;
    Sequence->TransferCharacteristics = 2;
    Sequence->MatrixCoefficients = 2;
    if (Sequence->ColorDescriptionPresent)
    {
        Sequence->ColorPrimaries = (UINT8)Reader.ReadBits(8);
        Sequence->TransferCharacteristics = (UINT8)Reader.ReadBits(8);
        Sequence->MatrixCoefficients = (UINT8)Reader.ReadBits(8);
    }

    if (Sequence->MonoChrome)
    {
        Sequence->ColorRange = Reader.ReadFlag();
        Sequence->SubsamplingX = TRUE;
        Sequence->SubsamplingY = TRUE;
        Sequence->SeparateUvDeltaQ = FALSE;
        return;
    }

    /* BT.709 primaries with sRGB transfer and identity matrix means 4:4:4 RGB */
    if (Sequence->ColorPrimaries == 1 && Sequence->TransferCharacteristics == 13 &&
        Sequence->MatrixCoefficients == 0)
    {
        Sequence->ColorRange = TRUE;
        Sequence->SubsamplingX = FALSE;
        Sequence->SubsamplingY = FALSE;
    }
    else
    {
        Sequence->ColorRange = Reader.ReadFlag();
        if (Sequence->Profile == 0)
        {
            Sequence->SubsamplingX = TRUE;
            Sequence->SubsamplingY = TRUE;
        }
        else if (Sequence->Profile == 1)
        {
            Sequence->SubsamplingX = FALSE;
            Sequence->SubsamplingY = FALSE;
        }
        else if (Sequence->BitDepth == 12)
        {
            Sequence->SubsamplingX = Reader.ReadFlag();
            Sequence->SubsamplingY = Sequence->SubsamplingX ? Reader.ReadFlag() : FALSE;
        }
        else
        {
            Sequence->SubsamplingX = TRUE;
            Sequence->SubsamplingY = FALSE;
        }

        if (Sequence->SubsamplingX && Sequence->SubsamplingY)
            Sequence->ChromaSamplePosition = (UINT8)Reader.ReadBits(2);
    }

    Sequence->SeparateUvDeltaQ = Reader.ReadFlag();
}

BOOLEAN
Av1ParseSequenceHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _Out_ Av1SequenceHeader* Sequence)
{
    BitReader Reader(Payload, Size);
    BOOLEAN InitialDisplayDelayPresent;
    UINT8 BufferDelayLength = 0;
    UINT Index;

    memset(Sequence, 0, sizeof(*Sequence));

    Sequence->Profile = (UINT8)Reader.ReadBits(3);
    Sequence->StillPicture = Reader.ReadFlag();
    Sequence->ReducedStillPictureHeader = Reader.ReadFlag();

    if (Sequence->ReducedStillPictureHeader)
    {
        Sequence->OperatingPoints = 1;
        Sequence->SeqLevelIdx[0] = (UINT8)Reader.ReadBits(5);
    }
    else
    {
        Sequence->TimingInfoPresent = Reader.ReadFlag();
        if (Sequence->TimingInfoPresent)
        {
            Sequence->NumUnitsInDisplayTick = Reader.ReadBits(32);
            Sequence->TimeScale = Reader.ReadBits(32);
            Sequence->EqualPictureInterval = Reader.ReadFlag();
            if (Sequence->EqualPictureInterval)
                Av1ReadUvlc(Reader);

            Sequence->DecoderModelInfoPresent = Reader.ReadFlag();
            if (Sequence->DecoderModelInfoPresent)
            {
                BufferDelayLength = (UINT8)Reader.ReadBits(5) + 1;
                Reader.SkipBits(32);
                Sequence->BufferRemovalTimeLength = (UINT8)Reader.ReadBits(5) + 1;
                Sequence->FramePresentationTimeLength = (UINT8)Reader.ReadBits(5) + 1;
            }
        }

        InitialDisplayDelayPresent = Reader.ReadFlag();
        Sequence->OperatingPoints = (UINT8)Reader.ReadBits(5) + 1;
        for (Index = 0; Index < Sequence->OperatingPoints; Index++)
        {
            Sequence->OperatingPointIdc[Index] = (UINT16)Reader.ReadBits(12);
            Sequence->SeqLevelIdx[Index] = (UINT8)Reader.ReadBits(5);
            Sequence->SeqTier[Index] = (Sequence->SeqLevelIdx[Index] > 7) ? (UINT8)Reader.ReadBit() : 0;

            if (Sequence->DecoderModelInfoPresent)
            {
                Sequence->DecoderModelPresentForOp[Index] = Reader.ReadFlag();
                if (Sequence->DecoderModelPresentForOp[Index])
                    Reader.SkipBits(2 * BufferDelayLength + 1);
            }

            if (InitialDisplayDelayPresent && Reader.ReadFlag())
                Reader.SkipBits(4);
        }
    }

    Sequence->FrameWidthBits = (UINT8)Reader.ReadBits(4) + 1;
    Sequence->FrameHeightBits = (UINT8)Reader.ReadBits(4) + 1;
    Sequence->MaxFrameWidth = Reader.ReadBits(Sequence->FrameWidthBits) + 1;
    Sequence->MaxFrameHeight = Reader.ReadBits(Sequence->FrameHeightBits) + 1;

    if (!Sequence->ReducedStillPictureHeader)
        Sequence->FrameIdNumbersPresent = Reader.ReadFlag();
    if (Sequence->FrameIdNumbersPresent)
    {
        Sequence->DeltaFrameIdLength = (UINT8)Reader.ReadBits(4) + 2;
        Sequence->FrameIdLength = (UINT8)Reader.ReadBits(3) + 1 + Sequence->DeltaFrameIdLength;
    }

    Sequence->Use128x128Superblock = Reader.ReadFlag();
    Sequence->EnableFilterIntra = Reader.ReadFlag();
    Sequence->EnableIntraEdgeFilter = Reader.ReadFlag();

    Sequence->SeqForceScreenContentTools = Av1SelectScreenContentTools;
    Sequence->SeqForceIntegerMv = Av1SelectIntegerMv;
    if (!Sequence->ReducedStillPictureHeader)
    {
        Sequence->EnableInterintraCompound = Reader.ReadFlag();
        Sequence->EnableMaskedCompound = Reader.ReadFlag();
        Sequence->EnableWarpedMotion = Reader.ReadFlag();
        Sequence->EnableDualFilter = Reader.ReadFlag();
        Sequence->EnableOrderHint = Reader.ReadFlag();
        if (Sequence->EnableOrderHint)
        {
            Sequence->EnableJntComp = Reader.ReadFlag();
            Sequence->EnableRefFrameMvs = Reader.ReadFlag();
        }

        if (!Reader.ReadFlag())
            Sequence->SeqForceScreenContentTools = (UINT8)Reader.ReadBit();

        if (Sequence->SeqForceScreenContentTools > 0)
        {
            if (!Reader.ReadFlag())
                Sequence->SeqForceIntegerMv = (UINT8)Reader.ReadBit();
        }

        if (Sequence->EnableOrderHint)
            Sequence->OrderHintBits = (UINT8)Reader.ReadBits(3) + 1;
    }

    Sequence->EnableSuperres = Reader.ReadFlag();
    Sequence->EnableCdef = Reader.ReadFlag();
    Sequence->EnableRestoration = Reader.ReadFlag();
    Av1ParseColorConfig(Reader, Sequence);
    Sequence->FilmGrainParamsPresent = Reader.ReadFlag();

    Sequence->IsValid = !Reader.HasFailed();
    return Sequence->IsValid;
}

/* Parses one uncompressed header; the process names in comments follow AV1 spec section 7 */
class Av1FrameHeaderParser
{
public:
    Av1FrameHeaderParser(
        _Inout_ BitReader& Reader,
        _In_ const Av1SequenceHeader& Sequence,
        _In_reads_(Av1NumRefFrames) const Av1RefSlot* Refs,
        _Out_ Av1FrameHeader& Header)
        : Reader(Reader),
          Seq(Sequence),
          Refs(Refs),
          Hdr(Header)
    {
    }

    BOOLEAN
    Parse(
        _In_ UINT8 TemporalId,
        _In_ UINT8 SpatialId);

private:
    INT32
    RelativeDistance(
        _In_ UINT32 A,
        _In_ UINT32 B) const
    {
        INT32 Difference;
        INT32 Mask;

        if (!Seq.EnableOrderHint)
            return 0;

        Difference = (INT32)A - (INT32)B;
        Mask = 1 << (Seq.OrderHintBits - 1);
        return (Difference & (Mask - 1)) - (Difference & Mask);
    }

    VOID SuperresParams();
    VOID ComputeImageSize();
    VOID FrameSize();
    VOID RenderSize();
    VOID FrameSizeWithRefs();
    VOID SetFrameRefs(_In_ UINT LastFrameIdx, _In_ UINT GoldFrameIdx);
    VOID SetupPastIndependence();
    VOID LoadPrevious();
    BOOLEAN TileInfo();
    VOID QuantizationParams();
    INT8 ReadDeltaQ();
    VOID SegmentationParams();
    VOID DeltaParams();
    VOID ComputeLossless();
    VOID LoopFilterParams();
    VOID CdefParams();
    VOID LoopRestorationParams();
    VOID SkipModeParams();
    VOID GlobalMotionParams();
    VOID ReadGlobalParam(_In_ UINT Type, _In_ UINT Ref, _In_ UINT Index);
    VOID FilmGrainParams();

    BitReader& Reader;
    const Av1SequenceHeader& Seq;
    const Av1RefSlot* Refs;
    Av1FrameHeader& Hdr;
    INT32 PrevGmParams[Av1NumRefFrames][6] = {};
    BOOLEAN RefValid[Av1NumRefFrames] = {};
    UINT32 RefOrderHint[Av1NumRefFrames] = {};
};

VOID
Av1FrameHeaderParser::SuperresParams()
{
    Hdr.UseSuperres = Seq.EnableSuperres ? Reader.ReadFlag() : FALSE;
    Hdr.SuperresDenom = Hdr.UseSuperres ? (UINT8)(Reader.ReadBits(3) + 9) : 8;
    Hdr.UpscaledWidth = Hdr.FrameWidth;
    Hdr.FrameWidth = (Hdr.UpscaledWidth * 8 + Hdr.SuperresDenom / 2) / Hdr.SuperresDenom;
}

VOID
Av1FrameHeaderParser::ComputeImageSize()
{
    Hdr.MiCols = 2 * ((Hdr.FrameWidth + 7) >> 3);
    Hdr.MiRows = 2 * ((Hdr.FrameHeight + 7) >> 3);
}

VOID
Av1FrameHeaderParser::FrameSize()
{
    if (Hdr.FrameSizeOverride)
    {
        Hdr.FrameWidth = Reader.ReadBits(Seq.FrameWidthBits) + 1;
        Hdr.FrameHeight = Reader.ReadBits(Seq.FrameHeightBits) + 1;
    }
    else
    {
        Hdr.FrameWidth = Seq.MaxFrameWidth;
        Hdr.FrameHeight = Seq.MaxFrameHeight;
    }

    SuperresParams();
    ComputeImageSize();
}

VOID
Av1FrameHeaderParser::RenderSize()
{
    if (Reader.ReadFlag())
    {
        Hdr.RenderWidth = Reader.ReadBits(16) + 1;
        Hdr.RenderHeight = Reader.ReadBits(16) + 1;
    }
    else
    {
        Hdr.RenderWidth = Hdr.UpscaledWidth;
        Hdr.RenderHeight = Hdr.FrameHeight;
    }
}

VOID
Av1FrameHeaderParser::FrameSizeWithRefs()
{
    const Av1RefSlot* Found = nullptr;
    UINT Index;

    for (Index = 0; Index < Av1RefsPerFrame; Index++)
    {
        if (Reader.ReadFlag())
        {
            Found = &Refs[Hdr.RefFrameIdx[Index]];
            break;
        }
    }

    if (Found == nullptr)
    {
        FrameSize();
        RenderSize();
        return;
    }

    Hdr.UpscaledWidth = Found->UpscaledWidth;
    Hdr.FrameWidth = Hdr.UpscaledWidth;
    Hdr.FrameHeight = Found->FrameHeight;
    Hdr.RenderWidth = Found->RenderWidth;
    Hdr.RenderHeight = Found->RenderHeight;
    SuperresParams();
    ComputeImageSize();
}

/* 7.8: derive the five references that frame_refs_short_signaling leaves out */
VOID
Av1FrameHeaderParser::SetFrameRefs(
    _In_ UINT LastFrameIdx,
    _In_ UINT GoldFrameIdx)
{
    static const UINT RefFrameList[5] = { Av1Last2, Av1Last3, Av1BwdRef, Av1AltRef2, Av1AltRef };
    BOOLEAN UsedFrame[Av1NumRefFrames] = {};
    INT32 ShiftedOrderHints[Av1NumRefFrames];
    INT32 CurFrameHint = 1 << (Seq.OrderHintBits - 1);
    INT32 Best;
    INT32 Hint;
    INT Ref;
    UINT Index;
    UINT Pass;

    for (Index = 0; Index < Av1RefsPerFrame; Index++)
        Hdr.RefFrameIdx[Index] = -1;
    Hdr.RefFrameIdx[Av1Last - Av1Last] = (INT8)LastFrameIdx;
    Hdr.RefFrameIdx[Av1Golden - Av1Last] = (INT8)GoldFrameIdx;
    UsedFrame[LastFrameIdx] = TRUE;
    UsedFrame[GoldFrameIdx] = TRUE;

    for (Index = 0; Index < Av1NumRefFrames; Index++)
        ShiftedOrderHints[Index] = CurFrameHint + RelativeDistance(RefOrderHint[Index], Hdr.OrderHint);

    /* ALTREF takes the latest backward reference, then BWDREF and ALTREF2 the earliest ones */
    for (Pass = 0; Pass < 3; Pass++)
    {
        Ref = -1;
        Best = 0;
        for (Index = 0; Index < Av1NumRefFrames; Index++)
        {
            Hint = ShiftedOrderHints[Index];
            if (UsedFrame[Index] || Hint < CurFrameHint)
                continue;
            if (Ref < 0 || (Pass == 0 ? Hint >= Best : Hint < Best))
            {
                Ref = (INT)Index;
                Best = Hint;
            }
        }

        if (Ref >= 0)
        {
            Hdr.RefFrameIdx[(Pass == 0 ? Av1AltRef : (Pass == 1 ? Av1BwdRef : Av1AltRef2)) - Av1Last] = (INT8)Ref;
            UsedFrame[Ref] = TRUE;
        }
    }

    /* The remaining slots take the latest forward references */
    for (UINT Frame : RefFrameList)
    {
        if (Hdr.RefFrameIdx[Frame - Av1Last] >= 0)
            continue;

        Ref = -1;
        Best = 0;
        for (Index = 0; Index < Av1NumRefFrames; Index++)
        {
            Hint = ShiftedOrderHints[Index];
            if (!UsedFrame[Index] && Hint < CurFrameHint && (Ref < 0 || Hint >= Best))
            {
                Ref = (INT)Index;
                Best = Hint;
            }
        }

        if (Ref >= 0)
        {
            Hdr.RefFrameIdx[Frame - Av1Last] = (INT8)Ref;
            UsedFrame[Ref] = TRUE;
        }
    }

    /* Anything still unset points at the earliest frame of all */
    Ref = -1;
    Best = 0;
    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        Hint = ShiftedOrderHints[Index];
        if (Ref < 0 || Hint < Best)
        {
            Ref = (INT)Index;
            Best = Hint;
        }
    }

    for (Index = 0; Index < Av1RefsPerFrame; Index++)
    {
        if (Hdr.RefFrameIdx[Index] < 0)
            Hdr.RefFrameIdx[Index] = (INT8)Ref;
    }
}

VOID
Av1FrameHeaderParser::SetupPastIndependence()
{
    UINT Ref;
    UINT Index;

    memset(Hdr.FeatureData, 0, sizeof(Hdr.FeatureData));
    memset(Hdr.FeatureEnabled, 0, sizeof(Hdr.FeatureEnabled));

    for (Ref = Av1Last; Ref <= Av1AltRef; Ref++)
    {
        for (Index = 0; Index < 6; Index++)
            PrevGmParams[Ref][Index] = (Index % 3 == 2) ? (1 << Av1WarpedModelPrecBits) : 0;
    }

    Hdr.LoopFilterDeltaEnabled = TRUE;
    memcpy(Hdr.LoopFilterRefDeltas, DefaultLoopFilterRefDeltas, sizeof(Hdr.LoopFilterRefDeltas));
    Hdr.LoopFilterModeDeltas[0] = 0;
    Hdr.LoopFilterModeDeltas[1] = 0;
}

VOID
Av1FrameHeaderParser::LoadPrevious()
{
    const Av1RefSlot& Previous = Refs[Hdr.RefFrameIdx[Hdr.PrimaryRefFrame]];

    memcpy(PrevGmParams, Previous.GmParams, sizeof(PrevGmParams));
    memcpy(Hdr.LoopFilterRefDeltas, Previous.LoopFilterRefDeltas, sizeof(Hdr.LoopFilterRefDeltas));
    memcpy(Hdr.LoopFilterModeDeltas, Previous.LoopFilterModeDeltas, sizeof(Hdr.LoopFilterModeDeltas));
    memcpy(Hdr.FeatureEnabled, Previous.FeatureEnabled, sizeof(Hdr.FeatureEnabled));
    memcpy(Hdr.FeatureData, Previous.FeatureData, sizeof(Hdr.FeatureData));
}

static
UINT32
Av1TileLog2(
    _In_ UINT32 BlockSize,
    _In_ UINT32 Target)
{
    UINT32 Shift = 0;

    while ((BlockSize << Shift) < Target)
        Shift++;
    return Shift;
}

BOOLEAN
Av1FrameHeaderParser::TileInfo()
{
    UINT32 SbCols = Seq.Use128x128Superblock ? ((Hdr.MiCols + 31) >> 5) : ((Hdr.MiCols + 15) >> 4);
    UINT32 SbRows = Seq.Use128x128Superblock ? ((Hdr.MiRows + 31) >> 5) : ((Hdr.MiRows + 15) >> 4);
    UINT32 SbShift = Seq.Use128x128Superblock ? 5 : 4;
    UINT32 SbSize = SbShift + 2;
    UINT32 MaxTileWidthSb = 4096 >> SbSize;
    UINT32 MaxTileAreaSb = (4096 * 2304) >> (2 * SbSize);
    UINT32 MinLog2TileCols = Av1TileLog2(MaxTileWidthSb, SbCols);
    UINT32 MaxLog2TileCols = Av1TileLog2(1, std::min(SbCols, Av1MaxTileCols));
    UINT32 MaxLog2TileRows = Av1TileLog2(1, std::min(SbRows, Av1MaxTileRows));
    UINT32 MinLog2Tiles = std::max(MinLog2TileCols, Av1TileLog2(MaxTileAreaSb, SbRows * SbCols));
    UINT32 MinLog2TileRows;
    UINT32 TileWidthSb;
    UINT32 TileHeightSb;
    UINT32 WidestTileSb = 0;
    UINT32 MaxTileHeightSb;
    UINT32 StartSb;
    UINT32 SizeSb;
    UINT32 Index;

    if (Reader.ReadFlag())
    {
        /* uniform_tile_spacing_flag */
        Hdr.TileColsLog2 = MinLog2TileCols;
        while (Hdr.TileColsLog2 < MaxLog2TileCols && Reader.ReadFlag())
            Hdr.TileColsLog2++;

        TileWidthSb = (SbCols + (1u << Hdr.TileColsLog2) - 1) >> Hdr.TileColsLog2;
        for (Index = 0, StartSb = 0; StartSb < SbCols; StartSb += TileWidthSb, Index++)
            Hdr.MiColStarts[Index] = StartSb << SbShift;
        Hdr.MiColStarts[Index] = Hdr.MiCols;
        Hdr.TileCols = Index;

        MinLog2TileRows = (MinLog2Tiles > Hdr.TileColsLog2) ? MinLog2Tiles - Hdr.TileColsLog2 : 0;
        Hdr.TileRowsLog2 = MinLog2TileRows;
        while (Hdr.TileRowsLog2 < MaxLog2TileRows && Reader.ReadFlag())
            Hdr.TileRowsLog2++;

        TileHeightSb = (SbRows + (1u << Hdr.TileRowsLog2) - 1) >> Hdr.TileRowsLog2;
        for (Index = 0, StartSb = 0; StartSb < SbRows; StartSb += TileHeightSb, Index++)
            Hdr.MiRowStarts[Index] = StartSb << SbShift;
        Hdr.MiRowStarts[Index] = Hdr.MiRows;
        Hdr.TileRows = Index;
    }
    else
    {
        for (Index = 0, StartSb = 0; StartSb < SbCols && Index < Av1MaxTileCols; Index++)
        {
            Hdr.MiColStarts[Index] = StartSb << SbShift;
            SizeSb = Av1ReadNs(Reader, std::min(SbCols - StartSb, MaxTileWidthSb)) + 1;
            WidestTileSb = std::max(SizeSb, WidestTileSb);
            StartSb += SizeSb;
        }
        Hdr.MiColStarts[Index] = Hdr.MiCols;
        Hdr.TileCols = Index;
        Hdr.TileColsLog2 = Av1TileLog2(1, Hdr.TileCols);

        if (MinLog2Tiles > 0)
            MaxTileAreaSb = (SbRows * SbCols) >> (MinLog2Tiles + 1);
        else
            MaxTileAreaSb = SbRows * SbCols;
        MaxTileHeightSb = std::max<UINT32>(MaxTileAreaSb / std::max<UINT32>(WidestTileSb, 1), 1);

        for (Index = 0, StartSb = 0; StartSb < SbRows && Index < Av1MaxTileRows; Index++)
        {
            Hdr.MiRowStarts[Index] = StartSb << SbShift;
            SizeSb = Av1ReadNs(Reader, std::min(SbRows - StartSb, MaxTileHeightSb)) + 1;
            StartSb += SizeSb;
        }
        Hdr.MiRowStarts[Index] = Hdr.MiRows;
        Hdr.TileRows = Index;
        Hdr.TileRowsLog2 = Av1TileLog2(1, Hdr.TileRows);
    }

    if (Hdr.TileColsLog2 > 0 || Hdr.TileRowsLog2 > 0)
    {
        Hdr.ContextUpdateTileId = Reader.ReadBits(Hdr.TileRowsLog2 + Hdr.TileColsLog2);
        Hdr.TileSizeBytes = Reader.ReadBits(2) + 1;
    }
    else
    {
        Hdr.ContextUpdateTileId = 0;
        Hdr.TileSizeBytes = 4;
    }

    return Hdr.TileCols > 0 && Hdr.TileRows > 0 && Hdr.ContextUpdateTileId < Hdr.TileCols * Hdr.TileRows;
}

INT8
Av1FrameHeaderParser::ReadDeltaQ()
{
    if (!Reader.ReadFlag())
        return 0;

    return (INT8)Av1ReadSu(Reader, 7);
}

VOID
Av1FrameHeaderParser::QuantizationParams()
{
    BOOLEAN DiffUvDelta = FALSE;

    Hdr.BaseQIdx = (UINT8)Reader.ReadBits(8);
    Hdr.DeltaQYDc = ReadDeltaQ();
    Hdr.DeltaQUDc = 0;
    Hdr.DeltaQUAc = 0;
    Hdr.DeltaQVDc = 0;
    Hdr.DeltaQVAc = 0;

    if (Seq.NumPlanes > 1)
    {
        if (Seq.SeparateUvDeltaQ)
            DiffUvDelta = Reader.ReadFlag();

        Hdr.DeltaQUDc = ReadDeltaQ();
        Hdr.DeltaQUAc = ReadDeltaQ();
        if (DiffUvDelta)
        {
            Hdr.DeltaQVDc = ReadDeltaQ();
            Hdr.DeltaQVAc = ReadDeltaQ();
        }
        else
        {
            Hdr.DeltaQVDc = Hdr.DeltaQUDc;
            Hdr.DeltaQVAc = Hdr.DeltaQUAc;
        }
    }

    Hdr.UsingQmatrix = Reader.ReadFlag();
    if (Hdr.UsingQmatrix)
    {
        Hdr.QmY = (UINT8)Reader.ReadBits(4);
        Hdr.QmU = (UINT8)Reader.ReadBits(4);
        Hdr.QmV = Seq.SeparateUvDeltaQ ? (UINT8)Reader.ReadBits(4) : Hdr.QmU;
    }
}

VOID
Av1FrameHeaderParser::SegmentationParams()
{
    INT32 Value;
    INT32 Limit;
    UINT Segment;
    UINT Feature;

    Hdr.SegmentationEnabled = Reader.ReadFlag();
    Hdr.SegmentationUpdateMap = FALSE;
    Hdr.SegmentationTemporalUpdate = FALSE;
    Hdr.SegmentationUpdateData = FALSE;

    if (!Hdr.SegmentationEnabled)
    {
        memset(Hdr.FeatureEnabled, 0, sizeof(Hdr.FeatureEnabled));
        memset(Hdr.FeatureData, 0, sizeof(Hdr.FeatureData));
        return;
    }

    if (Hdr.PrimaryRefFrame == Av1PrimaryRefNone)
    {
        Hdr.SegmentationUpdateMap = TRUE;
        Hdr.SegmentationUpdateData = TRUE;
    }
    else
    {
        Hdr.SegmentationUpdateMap = Reader.ReadFlag();
        if (Hdr.SegmentationUpdateMap)
            Hdr.SegmentationTemporalUpdate = Reader.ReadFlag();
        Hdr.SegmentationUpdateData = Reader.ReadFlag();
    }

    if (!Hdr.SegmentationUpdateData)
        return;

    for (Segment = 0; Segment < Av1MaxSegments; Segment++)
    {
        for (Feature = 0; Feature < Av1SegLevels; Feature++)
        {
            Value = 0;
            Hdr.FeatureEnabled[Segment][Feature] = Reader.ReadFlag();
            if (Hdr.FeatureEnabled[Segment][Feature])
            {
                Limit = SegmentationFeatureMax[Feature];
                if (SegmentationFeatureSigned[Feature])
                {
                    Value = Av1ReadSu(Reader, 1 + SegmentationFeatureBits[Feature]);
                    Value = std::clamp(Value, -Limit, Limit);
                }
                else
                {
                    Value = (INT32)Reader.ReadBits(SegmentationFeatureBits[Feature]);
                    Value = std::clamp(Value, 0, Limit);
                }
            }

            Hdr.FeatureData[Segment][Feature] = (INT16)Value;
        }
    }
}

VOID
Av1FrameHeaderParser::DeltaParams()
{
    Hdr.DeltaQPresent = FALSE;
    Hdr.DeltaQRes = 0;
    if (Hdr.BaseQIdx > 0)
        Hdr.DeltaQPresent = Reader.ReadFlag();
    if (Hdr.DeltaQPresent)
        Hdr.DeltaQRes = (UINT8)Reader.ReadBits(2);

    Hdr.DeltaLfPresent = FALSE;
    Hdr.DeltaLfRes = 0;
    Hdr.DeltaLfMulti = FALSE;
    if (Hdr.DeltaQPresent)
    {
        if (!Hdr.AllowIntrabc)
            Hdr.DeltaLfPresent = Reader.ReadFlag();
        if (Hdr.DeltaLfPresent)
        {
            Hdr.DeltaLfRes = (UINT8)Reader.ReadBits(2);
            Hdr.DeltaLfMulti = Reader.ReadFlag();
        }
    }
}

VOID
Av1FrameHeaderParser::ComputeLossless()
{
    INT32 QIndex;
    UINT Segment;

    Hdr.CodedLossless = TRUE;
    for (Segment = 0; Segment < Av1MaxSegments; Segment++)
    {
        /* get_qindex with ignoreDeltaQ: the segment's alternate quantizer, if any */
        QIndex = Hdr.BaseQIdx;
        if (Hdr.SegmentationEnabled && Hdr.FeatureEnabled[Segment][0])
            QIndex = std::clamp(QIndex + Hdr.FeatureData[Segment][0], 0, 255);

        if (QIndex != 0 || Hdr.DeltaQYDc != 0 || Hdr.DeltaQUAc != 0 || Hdr.DeltaQUDc != 0 ||
            Hdr.DeltaQVAc != 0 || Hdr.DeltaQVDc != 0)
        {
            Hdr.CodedLossless = FALSE;
            break;
        }
    }

    Hdr.AllLossless = Hdr.CodedLossless && Hdr.FrameWidth == Hdr.UpscaledWidth;
}

VOID
Av1FrameHeaderParser::LoopFilterParams()
{
    UINT Index;

    if (Hdr.CodedLossless || Hdr.AllowIntrabc)
    {
        Hdr.LoopFilterLevel[0] = 0;
        Hdr.LoopFilterLevel[1] = 0;
        memcpy(Hdr.LoopFilterRefDeltas, DefaultLoopFilterRefDeltas, sizeof(Hdr.LoopFilterRefDeltas));
        Hdr.LoopFilterModeDeltas[0] = 0;
        Hdr.LoopFilterModeDeltas[1] = 0;
        return;
    }

    Hdr.LoopFilterLevel[0] = (UINT8)Reader.ReadBits(6);
    Hdr.LoopFilterLevel[1] = (UINT8)Reader.ReadBits(6);
    if (Seq.NumPlanes > 1 && (Hdr.LoopFilterLevel[0] || Hdr.LoopFilterLevel[1]))
    {
        Hdr.LoopFilterLevel[2] = (UINT8)Reader.ReadBits(6);
        Hdr.LoopFilterLevel[3] = (UINT8)Reader.ReadBits(6);
    }

    Hdr.LoopFilterSharpness = (UINT8)Reader.ReadBits(3);
    Hdr.LoopFilterDeltaEnabled = Reader.ReadFlag();
    Hdr.LoopFilterDeltaUpdate = FALSE;
    if (!Hdr.LoopFilterDeltaEnabled)
        return;

    Hdr.LoopFilterDeltaUpdate = Reader.ReadFlag();
    if (!Hdr.LoopFilterDeltaUpdate)
        return;

    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        if (Reader.ReadFlag())
            Hdr.LoopFilterRefDeltas[Index] = (INT8)Av1ReadSu(Reader, 7);
    }
    for (Index = 0; Index < 2; Index++)
    {
        if (Reader.ReadFlag())
            Hdr.LoopFilterModeDeltas[Index] = (INT8)Av1ReadSu(Reader, 7);
    }
}

VOID
Av1FrameHeaderParser::CdefParams()
{
    UINT Index;

    if (Hdr.CodedLossless || Hdr.AllowIntrabc || !Seq.EnableCdef)
    {
        Hdr.CdefBits = 0;
        Hdr.CdefYPriStrength[0] = 0;
        Hdr.CdefYSecStrength[0] = 0;
        Hdr.CdefUvPriStrength[0] = 0;
        Hdr.CdefUvSecStrength[0] = 0;
        Hdr.CdefDampingMinus3 = 0;
        return;
    }

    Hdr.CdefDampingMinus3 = (UINT8)Reader.ReadBits(2);
    Hdr.CdefBits = (UINT8)Reader.ReadBits(2);
    for (Index = 0; Index < (1u << Hdr.CdefBits); Index++)
    {
        Hdr.CdefYPriStrength[Index] = (UINT8)Reader.ReadBits(4);
        Hdr.CdefYSecStrength[Index] = (UINT8)Reader.ReadBits(2);
        if (Seq.NumPlanes > 1)
        {
            Hdr.CdefUvPriStrength[Index] = (UINT8)Reader.ReadBits(4);
            Hdr.CdefUvSecStrength[Index] = (UINT8)Reader.ReadBits(2);
        }
    }
}

VOID
Av1FrameHeaderParser::LoopRestorationParams()
{
    BOOLEAN UsesLr = FALSE;
    BOOLEAN UsesChromaLr = FALSE;
    UINT32 UnitShift;
    UINT32 UvShift = 0;
    UINT Plane;

    for (Plane = 0; Plane < 3; Plane++)
    {
        Hdr.FrameRestorationType[Plane] = 0;
        Hdr.LoopRestorationSizeLog2[Plane] = 8;
    }

    if (Hdr.AllLossless || Hdr.AllowIntrabc || !Seq.EnableRestoration)
        return;

    for (Plane = 0; Plane < Seq.NumPlanes; Plane++)
    {
        Hdr.FrameRestorationType[Plane] = RemapLrType[Reader.ReadBits(2)];
        if (Hdr.FrameRestorationType[Plane] != 0)
        {
            UsesLr = TRUE;
            if (Plane > 0)
                UsesChromaLr = TRUE;
        }
    }

    if (!UsesLr)
        return;

    if (Seq.Use128x128Superblock)
    {
        UnitShift = Reader.ReadBit() + 1;
    }
    else
    {
        UnitShift = Reader.ReadBit();
        if (UnitShift)
            UnitShift += Reader.ReadBit();
    }

    if (Seq.SubsamplingX && Seq.SubsamplingY && UsesChromaLr)
        UvShift = Reader.ReadBit();

    /* RESTORATION_TILESIZE_MAX is 256, scaled down by (2 - lr_unit_shift) */
    Hdr.LoopRestorationSizeLog2[0] = (UINT8)(6 + UnitShift);
    Hdr.LoopRestorationSizeLog2[1] = (UINT8)(6 + UnitShift - UvShift);
    Hdr.LoopRestorationSizeLog2[2] = Hdr.LoopRestorationSizeLog2[1];
}

VOID
Av1FrameHeaderParser::SkipModeParams()
{
    INT ForwardIdx = -1;
    INT BackwardIdx = -1;
    INT SecondForwardIdx = -1;
    UINT32 ForwardHint = 0;
    UINT32 BackwardHint = 0;
    UINT32 SecondForwardHint = 0;
    UINT32 RefHint;
    BOOLEAN IsAllowed;
    UINT Index;

    if (Hdr.FrameIsIntra || !Hdr.ReferenceSelect || !Seq.EnableOrderHint)
    {
        Hdr.SkipModePresent = FALSE;
        return;
    }

    for (Index = 0; Index < Av1RefsPerFrame; Index++)
    {
        RefHint = RefOrderHint[Hdr.RefFrameIdx[Index]];
        if (RelativeDistance(RefHint, Hdr.OrderHint) < 0)
        {
            if (ForwardIdx < 0 || RelativeDistance(RefHint, ForwardHint) > 0)
            {
                ForwardIdx = (INT)Index;
                ForwardHint = RefHint;
            }
        }
        else if (RelativeDistance(RefHint, Hdr.OrderHint) > 0)
        {
            if (BackwardIdx < 0 || RelativeDistance(RefHint, BackwardHint) < 0)
            {
                BackwardIdx = (INT)Index;
                BackwardHint = RefHint;
            }
        }
    }

    if (ForwardIdx < 0)
    {
        IsAllowed = FALSE;
    }
    else if (BackwardIdx >= 0)
    {
        IsAllowed = TRUE;
    }
    else
    {
        for (Index = 0; Index < Av1RefsPerFrame; Index++)
        {
            RefHint = RefOrderHint[Hdr.RefFrameIdx[Index]];
            if (RelativeDistance(RefHint, ForwardHint) < 0 &&
                (SecondForwardIdx < 0 || RelativeDistance(RefHint, SecondForwardHint) > 0))
            {
                SecondForwardIdx = (INT)Index;
                SecondForwardHint = RefHint;
            }
        }
        IsAllowed = (SecondForwardIdx >= 0);
    }

    Hdr.SkipModePresent = IsAllowed ? Reader.ReadFlag() : FALSE;
}

static
UINT32
Av1InverseRecenter(
    _In_ UINT32 R,
    _In_ UINT32 V)
{
    if (V > 2 * R)
        return V;
    if (V & 1)
        return R - ((V + 1) >> 1);
    return R + (V >> 1);
}

VOID
Av1FrameHeaderParser::ReadGlobalParam(
    _In_ UINT Type,
    _In_ UINT Ref,
    _In_ UINT Index)
{
    INT32 AbsBits = 12;
    INT32 PrecBits = 15;
    INT32 PrecDiff;
    INT32 Round;
    INT32 Sub;
    INT32 Mx;
    INT32 R;
    UINT32 NumSyms;
    UINT32 Decoded;
    UINT32 SubexpI = 0;
    UINT32 Mk = 0;
    UINT32 B2;
    UINT32 A;
    UINT32 Value;

    if (Index < 2)
    {
        if (Type == Av1Translation)
        {
            AbsBits = 9 - !Hdr.AllowHighPrecisionMv;
            PrecBits = 3 - !Hdr.AllowHighPrecisionMv;
        }
        else
        {
            AbsBits = 12;
            PrecBits = 6;
        }
    }

    PrecDiff = Av1WarpedModelPrecBits - PrecBits;
    Round = (Index % 3 == 2) ? (1 << Av1WarpedModelPrecBits) : 0;
    Sub = (Index % 3 == 2) ? (1 << PrecBits) : 0;
    Mx = 1 << AbsBits;
    R = (PrevGmParams[Ref][Index] >> PrecDiff) - Sub;

    /* decode_signed_subexp_with_ref(-Mx, Mx + 1, R) */
    NumSyms = (UINT32)(2 * Mx + 1);
    for (;;)
    {
        B2 = SubexpI ? 3 + SubexpI - 1 : 3;
        A = 1u << B2;
        if (NumSyms <= Mk + 3 * A)
        {
            Value = Av1ReadNs(Reader, NumSyms - Mk) + Mk;
            break;
        }

        if (!Reader.ReadFlag())
        {
            Value = Reader.ReadBits(B2) + Mk;
            break;
        }

        SubexpI++;
        Mk += A;
        if (Reader.HasFailed())
        {
            Value = 0;
            break;
        }
    }

    R -= -Mx;
    if (((UINT32)R << 1) <= NumSyms)
        Decoded = Av1InverseRecenter((UINT32)R, Value);
    else
        Decoded = NumSyms - 1 - Av1InverseRecenter(NumSyms - 1 - (UINT32)R, Value);

    Hdr.GmParams[Ref][Index] = (((INT32)Decoded - Mx) << PrecDiff) + Round;
}

/* 7.11.3.6 setupShear: only its verdict matters here, so the warp factors are discarded */
static
BOOLEAN
Av1IsShearValid(
    _In_reads_(6) const INT32* Params)
{
    INT64 Alpha0;
    INT64 Beta0;
    INT64 Gamma0;
    INT64 Delta0;
    INT64 Alpha;
    INT64 Beta;
    INT64 Gamma;
    INT64 Delta;
    INT64 Divisor;
    INT64 Factor;
    INT64 Product;
    UINT32 Absolute;
    UINT32 Remainder;
    UINT32 Fraction;
    UINT Shift;
    UINT Log2;

    auto Round2Signed = [](INT64 Value, UINT Bits) -> INT64
    {
        if (Bits == 0)
            return Value;
        if (Value >= 0)
            return (Value + (1ll << (Bits - 1))) >> Bits;
        return -((-Value + (1ll << (Bits - 1))) >> Bits);
    };

    if (Params[2] <= 0)
        return FALSE;

    Alpha0 = std::clamp<INT64>(Params[2] - (1 << Av1WarpedModelPrecBits), -32768, 32767);
    Beta0 = std::clamp<INT64>(Params[3], -32768, 32767);

    /* resolve_divisor: Div_Lut[f] is round(2^14 * 256 / (256 + f)) */
    Absolute = (UINT32)Params[2];
    Log2 = Av1FloorLog2(Absolute);
    Remainder = Absolute - (1u << Log2);
    Fraction = (Log2 > 8) ? (UINT32)Round2Signed(Remainder, Log2 - 8) : (Remainder << (8 - Log2));
    Divisor = 256 + Fraction;
    Factor = ((1ll << 14) * 256 + Divisor / 2) / Divisor;
    Shift = Log2 + 14;

    Product = (INT64)Params[4] << Av1WarpedModelPrecBits;
    Gamma0 = std::clamp<INT64>(Round2Signed(Product * Factor, Shift), -32768, 32767);
    Product = (INT64)Params[3] * Params[4];
    Delta0 = std::clamp<INT64>(Params[5] - Round2Signed(Product * Factor, Shift) - (1 << Av1WarpedModelPrecBits),
                               -32768, 32767);

    Alpha = Round2Signed(Alpha0, 6) << 6;
    Beta = Round2Signed(Beta0, 6) << 6;
    Gamma = Round2Signed(Gamma0, 6) << 6;
    Delta = Round2Signed(Delta0, 6) << 6;

    if (4 * llabs(Alpha) + 7 * llabs(Beta) >= (1 << Av1WarpedModelPrecBits))
        return FALSE;
    if (4 * llabs(Gamma) + 4 * llabs(Delta) >= (1 << Av1WarpedModelPrecBits))
        return FALSE;
    return TRUE;
}

VOID
Av1FrameHeaderParser::GlobalMotionParams()
{
    UINT Type;
    UINT Ref;
    UINT Index;

    for (Ref = Av1Last; Ref <= Av1AltRef; Ref++)
    {
        Hdr.GmType[Ref] = Av1Identity;
        Hdr.GmInvalid[Ref] = FALSE;
        for (Index = 0; Index < 6; Index++)
            Hdr.GmParams[Ref][Index] = (Index % 3 == 2) ? (1 << Av1WarpedModelPrecBits) : 0;
    }

    if (Hdr.FrameIsIntra)
        return;

    for (Ref = Av1Last; Ref <= Av1AltRef; Ref++)
    {
        Type = Av1Identity;
        if (Reader.ReadFlag())
        {
            if (Reader.ReadFlag())
                Type = Av1RotZoom;
            else
                Type = Reader.ReadFlag() ? Av1Translation : Av1Affine;
        }
        Hdr.GmType[Ref] = (UINT8)Type;

        if (Type >= Av1RotZoom)
        {
            ReadGlobalParam(Type, Ref, 2);
            ReadGlobalParam(Type, Ref, 3);
            if (Type == Av1Affine)
            {
                ReadGlobalParam(Type, Ref, 4);
                ReadGlobalParam(Type, Ref, 5);
            }
            else
            {
                Hdr.GmParams[Ref][4] = -Hdr.GmParams[Ref][3];
                Hdr.GmParams[Ref][5] = Hdr.GmParams[Ref][2];
            }
        }

        if (Type >= Av1Translation)
        {
            ReadGlobalParam(Type, Ref, 0);
            ReadGlobalParam(Type, Ref, 1);
        }

        if (Type > Av1Translation)
            Hdr.GmInvalid[Ref] = !Av1IsShearValid(Hdr.GmParams[Ref]);
    }
}

VOID
Av1FrameHeaderParser::FilmGrainParams()
{
    Av1FilmGrain& Grain = Hdr.FilmGrain;
    UINT16 Seed;
    UINT RefIdx;
    UINT Positions;
    UINT Index;

    memset(&Grain, 0, sizeof(Grain));
    if (!Seq.FilmGrainParamsPresent || (!Hdr.ShowFrame && !Hdr.ShowableFrame))
        return;

    Grain.ApplyGrain = Reader.ReadFlag();
    if (!Grain.ApplyGrain)
        return;

    Grain.GrainSeed = (UINT16)Reader.ReadBits(16);
    Grain.UpdateGrain = (Hdr.FrameType == Av1InterFrame) ? Reader.ReadFlag() : TRUE;

    if (!Grain.UpdateGrain)
    {
        /* load_grain_params keeps this frame's seed */
        RefIdx = Reader.ReadBits(3);
        Seed = Grain.GrainSeed;
        Grain = Refs[RefIdx].FilmGrain;
        Grain.GrainSeed = Seed;
        return;
    }

    Grain.NumYPoints = (UINT8)Reader.ReadBits(4);
    for (Index = 0; Index < Grain.NumYPoints && Index < 14; Index++)
    {
        Grain.PointYValue[Index] = (UINT8)Reader.ReadBits(8);
        Grain.PointYScaling[Index] = (UINT8)Reader.ReadBits(8);
    }

    Grain.ChromaScalingFromLuma = Seq.MonoChrome ? FALSE : Reader.ReadFlag();
    if (Seq.MonoChrome || Grain.ChromaScalingFromLuma ||
        (Seq.SubsamplingX && Seq.SubsamplingY && Grain.NumYPoints == 0))
    {
        Grain.NumCbPoints = 0;
        Grain.NumCrPoints = 0;
    }
    else
    {
        Grain.NumCbPoints = (UINT8)Reader.ReadBits(4);
        for (Index = 0; Index < Grain.NumCbPoints && Index < 10; Index++)
        {
            Grain.PointCbValue[Index] = (UINT8)Reader.ReadBits(8);
            Grain.PointCbScaling[Index] = (UINT8)Reader.ReadBits(8);
        }
        Grain.NumCrPoints = (UINT8)Reader.ReadBits(4);
        for (Index = 0; Index < Grain.NumCrPoints && Index < 10; Index++)
        {
            Grain.PointCrValue[Index] = (UINT8)Reader.ReadBits(8);
            Grain.PointCrScaling[Index] = (UINT8)Reader.ReadBits(8);
        }
    }

    Grain.GrainScalingMinus8 = (UINT8)Reader.ReadBits(2);
    Grain.ArCoeffLag = (UINT8)Reader.ReadBits(2);
    Positions = 2 * Grain.ArCoeffLag * (Grain.ArCoeffLag + 1);

    if (Grain.NumYPoints)
    {
        for (Index = 0; Index < Positions; Index++)
            Grain.ArCoeffsYPlus128[Index] = (UINT8)Reader.ReadBits(8);
        Positions++;
    }
    if (Grain.ChromaScalingFromLuma || Grain.NumCbPoints)
    {
        for (Index = 0; Index < Positions; Index++)
            Grain.ArCoeffsCbPlus128[Index] = (UINT8)Reader.ReadBits(8);
    }
    if (Grain.ChromaScalingFromLuma || Grain.NumCrPoints)
    {
        for (Index = 0; Index < Positions; Index++)
            Grain.ArCoeffsCrPlus128[Index] = (UINT8)Reader.ReadBits(8);
    }

    Grain.ArCoeffShiftMinus6 = (UINT8)Reader.ReadBits(2);
    Grain.GrainScaleShift = (UINT8)Reader.ReadBits(2);
    if (Grain.NumCbPoints)
    {
        Grain.CbMult = (UINT8)Reader.ReadBits(8);
        Grain.CbLumaMult = (UINT8)Reader.ReadBits(8);
        Grain.CbOffset = (UINT16)Reader.ReadBits(9);
    }
    if (Grain.NumCrPoints)
    {
        Grain.CrMult = (UINT8)Reader.ReadBits(8);
        Grain.CrLumaMult = (UINT8)Reader.ReadBits(8);
        Grain.CrOffset = (UINT16)Reader.ReadBits(9);
    }
    Grain.OverlapFlag = Reader.ReadFlag();
    Grain.ClipToRestrictedRange = Reader.ReadFlag();
}

BOOLEAN
Av1FrameHeaderParser::Parse(
    _In_ UINT8 TemporalId,
    _In_ UINT8 SpatialId)
{
    const UINT8 AllFrames = 0xFF;
    UINT32 OpPtIdc;
    UINT LastFrameIdx;
    UINT GoldFrameIdx;
    UINT Index;
    BOOLEAN ShortSignaling;

    memset(&Hdr, 0, sizeof(Hdr));
    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        RefValid[Index] = Refs[Index].IsValid;
        RefOrderHint[Index] = Refs[Index].OrderHint;
    }

    if (Seq.ReducedStillPictureHeader)
    {
        Hdr.FrameType = Av1KeyFrame;
        Hdr.FrameIsIntra = TRUE;
        Hdr.ShowFrame = TRUE;
    }
    else
    {
        Hdr.ShowExistingFrame = Reader.ReadFlag();
        if (Hdr.ShowExistingFrame)
        {
            Hdr.FrameToShowMapIdx = (UINT8)Reader.ReadBits(3);
            if (Seq.DecoderModelInfoPresent && !Seq.EqualPictureInterval)
                Reader.SkipBits(Seq.FramePresentationTimeLength);
            if (Seq.FrameIdNumbersPresent)
                Reader.SkipBits(Seq.FrameIdLength);

            Hdr.FrameType = Refs[Hdr.FrameToShowMapIdx].FrameType;
            Hdr.RefreshFrameFlags = (Hdr.FrameType == Av1KeyFrame) ? AllFrames : 0;
            if (Seq.FilmGrainParamsPresent)
                Hdr.FilmGrain = Refs[Hdr.FrameToShowMapIdx].FilmGrain;
            Hdr.HeaderBytes = (Reader.GetPosition() + 7) / 8;
            return !Reader.HasFailed() && Refs[Hdr.FrameToShowMapIdx].IsValid;
        }

        Hdr.FrameType = (Av1FrameType)Reader.ReadBits(2);
        Hdr.FrameIsIntra = (Hdr.FrameType == Av1IntraOnlyFrame || Hdr.FrameType == Av1KeyFrame);
        Hdr.ShowFrame = Reader.ReadFlag();
        if (Hdr.ShowFrame && Seq.DecoderModelInfoPresent && !Seq.EqualPictureInterval)
            Reader.SkipBits(Seq.FramePresentationTimeLength);
        Hdr.ShowableFrame = Hdr.ShowFrame ? (Hdr.FrameType != Av1KeyFrame) : Reader.ReadFlag();

        if (Hdr.FrameType == Av1SwitchFrame || (Hdr.FrameType == Av1KeyFrame && Hdr.ShowFrame))
            Hdr.ErrorResilientMode = TRUE;
        else
            Hdr.ErrorResilientMode = Reader.ReadFlag();
    }

    if (Hdr.FrameType == Av1KeyFrame && Hdr.ShowFrame)
    {
        for (Index = 0; Index < Av1NumRefFrames; Index++)
        {
            RefValid[Index] = FALSE;
            RefOrderHint[Index] = 0;
        }
    }

    Hdr.DisableCdfUpdate = Reader.ReadFlag();
    if (Seq.SeqForceScreenContentTools == Av1SelectScreenContentTools)
        Hdr.AllowScreenContentTools = Reader.ReadFlag();
    else
        Hdr.AllowScreenContentTools = Seq.SeqForceScreenContentTools;

    if (Hdr.AllowScreenContentTools)
    {
        if (Seq.SeqForceIntegerMv == Av1SelectIntegerMv)
            Hdr.ForceIntegerMv = Reader.ReadFlag();
        else
            Hdr.ForceIntegerMv = Seq.SeqForceIntegerMv;
    }
    if (Hdr.FrameIsIntra)
        Hdr.ForceIntegerMv = TRUE;

    if (Seq.FrameIdNumbersPresent)
        Hdr.CurrentFrameId = Reader.ReadBits(Seq.FrameIdLength);

    if (Hdr.FrameType == Av1SwitchFrame)
        Hdr.FrameSizeOverride = TRUE;
    else if (Seq.ReducedStillPictureHeader)
        Hdr.FrameSizeOverride = FALSE;
    else
        Hdr.FrameSizeOverride = Reader.ReadFlag();

    Hdr.OrderHint = Reader.ReadBits(Seq.OrderHintBits);
    if (Hdr.FrameIsIntra || Hdr.ErrorResilientMode)
        Hdr.PrimaryRefFrame = Av1PrimaryRefNone;
    else
        Hdr.PrimaryRefFrame = (UINT8)Reader.ReadBits(3);

    if (Seq.DecoderModelInfoPresent && Reader.ReadFlag())
    {
        for (Index = 0; Index < Seq.OperatingPoints; Index++)
        {
            if (!Seq.DecoderModelPresentForOp[Index])
                continue;

            OpPtIdc = Seq.OperatingPointIdc[Index];
            if (OpPtIdc == 0 || (((OpPtIdc >> TemporalId) & 1) && ((OpPtIdc >> (SpatialId + 8)) & 1)))
                Reader.SkipBits(Seq.BufferRemovalTimeLength);
        }
    }

    if (Hdr.FrameType == Av1SwitchFrame || (Hdr.FrameType == Av1KeyFrame && Hdr.ShowFrame))
        Hdr.RefreshFrameFlags = AllFrames;
    else
        Hdr.RefreshFrameFlags = (UINT8)Reader.ReadBits(8);

    if ((!Hdr.FrameIsIntra || Hdr.RefreshFrameFlags != AllFrames) && Hdr.ErrorResilientMode &&
        Seq.EnableOrderHint)
    {
        for (Index = 0; Index < Av1NumRefFrames; Index++)
        {
            UINT32 Hint = Reader.ReadBits(Seq.OrderHintBits);
            if (Hint != RefOrderHint[Index] || !RefValid[Index])
            {
                RefOrderHint[Index] = Hint;
                RefValid[Index] = FALSE;
            }
        }
    }

    if (Hdr.FrameIsIntra)
    {
        FrameSize();
        RenderSize();
        if (Hdr.AllowScreenContentTools && Hdr.UpscaledWidth == Hdr.FrameWidth)
            Hdr.AllowIntrabc = Reader.ReadFlag();
    }
    else
    {
        ShortSignaling = Seq.EnableOrderHint ? Reader.ReadFlag() : FALSE;
        if (ShortSignaling)
        {
            LastFrameIdx = Reader.ReadBits(3);
            GoldFrameIdx = Reader.ReadBits(3);
            SetFrameRefs(LastFrameIdx, GoldFrameIdx);
        }

        for (Index = 0; Index < Av1RefsPerFrame; Index++)
        {
            if (!ShortSignaling)
                Hdr.RefFrameIdx[Index] = (INT8)Reader.ReadBits(3);
            if (Seq.FrameIdNumbersPresent)
                Reader.SkipBits(Seq.DeltaFrameIdLength);
        }

        if (Hdr.FrameSizeOverride && !Hdr.ErrorResilientMode)
        {
            FrameSizeWithRefs();
        }
        else
        {
            FrameSize();
            RenderSize();
        }

        Hdr.AllowHighPrecisionMv = Hdr.ForceIntegerMv ? FALSE : Reader.ReadFlag();
        if (Reader.ReadFlag())
            Hdr.InterpolationFilter = Av1SwitchableFilter;
        else
            Hdr.InterpolationFilter = (UINT8)Reader.ReadBits(2);
        Hdr.IsMotionModeSwitchable = Reader.ReadFlag();

        if (Hdr.ErrorResilientMode || !Seq.EnableRefFrameMvs)
            Hdr.UseRefFrameMvs = FALSE;
        else
            Hdr.UseRefFrameMvs = Reader.ReadFlag();
    }

    if (Seq.ReducedStillPictureHeader || Hdr.DisableCdfUpdate)
        Hdr.DisableFrameEndUpdateCdf = TRUE;
    else
        Hdr.DisableFrameEndUpdateCdf = Reader.ReadFlag();

    if (Hdr.PrimaryRefFrame == Av1PrimaryRefNone)
        SetupPastIndependence();
    else
        LoadPrevious();

    if (!TileInfo())
        return FALSE;

    QuantizationParams();
    SegmentationParams();
    DeltaParams();
    ComputeLossless();
    LoopFilterParams();
    CdefParams();
    LoopRestorationParams();

    if (Hdr.CodedLossless)
        Hdr.TxMode = 0;
    else
        Hdr.TxMode = Reader.ReadFlag() ? 2 : 1;

    Hdr.ReferenceSelect = Hdr.FrameIsIntra ? FALSE : Reader.ReadFlag();
    SkipModeParams();

    if (Hdr.FrameIsIntra || Hdr.ErrorResilientMode || !Seq.EnableWarpedMotion)
        Hdr.AllowWarpedMotion = FALSE;
    else
        Hdr.AllowWarpedMotion = Reader.ReadFlag();

    Hdr.ReducedTxSet = Reader.ReadFlag();
    GlobalMotionParams();
    FilmGrainParams();

    Hdr.HeaderBytes = (Reader.GetPosition() + 7) / 8;
    return !Reader.HasFailed();
}

BOOLEAN
Av1ParseFrameHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_ const Av1SequenceHeader& Sequence,
    _In_reads_(Av1NumRefFrames) const Av1RefSlot* Refs,
    _In_ UINT8 TemporalId,
    _In_ UINT8 SpatialId,
    _Out_ Av1FrameHeader* Header)
{
    BitReader Reader(Payload, Size);
    Av1FrameHeaderParser Parser(Reader, Sequence, Refs, *Header);

    return Parser.Parse(TemporalId, SpatialId);
}

BOOLEAN
Av1ParseTileGroup(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_ SIZE_T Start,
    _In_ const Av1FrameHeader& Header,
    _Inout_ std::vector<Av1Tile>& Tiles,
    _Out_ BOOLEAN* IsLastGroup)
{
    UINT32 NumTiles = Header.TileCols * Header.TileRows;
    UINT32 TileBits = Header.TileColsLog2 + Header.TileRowsLog2;
    UINT32 GroupStart = 0;
    UINT32 GroupEnd = NumTiles - 1;
    UINT32 TileNum;
    SIZE_T Offset;
    SIZE_T TileSize;
    UINT Byte;

    *IsLastGroup = FALSE;
    if (Start > Size)
        return FALSE;

    BitReader Reader(Payload + Start, Size - Start);
    if (NumTiles > 1 && Reader.ReadFlag())
    {
        GroupStart = Reader.ReadBits(TileBits);
        GroupEnd = Reader.ReadBits(TileBits);
    }
    Av1ByteAlign(Reader);
    if (Reader.HasFailed() || GroupEnd >= NumTiles || GroupStart > GroupEnd)
        return FALSE;

    Offset = Start + Reader.GetPosition() / 8;
    for (TileNum = GroupStart; TileNum <= GroupEnd; TileNum++)
    {
        /* Every tile but the group's last carries a little endian size */
        if (TileNum == GroupEnd)
        {
            TileSize = Size - Offset;
        }
        else
        {
            if (Offset + Header.TileSizeBytes > Size)
                return FALSE;

            TileSize = 0;
            for (Byte = 0; Byte < Header.TileSizeBytes; Byte++)
                TileSize |= (SIZE_T)Payload[Offset + Byte] << (8 * Byte);
            TileSize += 1;
            Offset += Header.TileSizeBytes;
            if (TileSize > Size - Offset)
                return FALSE;
        }

        Tiles.push_back({ Offset, TileSize, (UINT16)(TileNum / Header.TileCols), (UINT16)(TileNum % Header.TileCols) });
        Offset += TileSize;
    }

    *IsLastGroup = (GroupEnd == NumTiles - 1);
    return TRUE;
}

PCSTR
Av1FrameTypeName(
    _In_ Av1FrameType Type)
{
    switch (Type)
    {
        case Av1KeyFrame: return "key";
        case Av1InterFrame: return "inter";
        case Av1IntraOnlyFrame: return "intra-only";
        default: return "switch";
    }
}
