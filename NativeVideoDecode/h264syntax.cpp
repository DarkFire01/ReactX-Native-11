/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     H.264 parameter set and slice header syntax (ITU-T H.264 7.3)
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "h264syntax.hpp"
#include "bitreader.hpp"

#include <algorithm>
#include <string.h>

/* Table 7-3 and 7-4 default scaling lists, in coded (zig-zag) order */
static const UINT8 Default4x4Intra[16] =
{
    6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32, 32, 37, 37, 42
};

static const UINT8 Default4x4Inter[16] =
{
    10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34
};

static const UINT8 Default8x8Intra[64] =
{
    6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18, 18, 18, 18, 23,
    23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
    27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
    31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

static const UINT8 Default8x8Inter[64] =
{
    9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19, 19, 19, 19, 21,
    21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 24, 24, 24, 24,
    24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27, 27,
    27, 28, 28, 28, 28, 28, 30, 30, 30, 30, 32, 32, 32, 33, 33, 35
};

/* Table A-1 MaxDpbMbs */
static
UINT32
NvdMaxDpbMbs(
    _In_ UINT8 LevelIdc)
{
    switch (LevelIdc)
    {
        case 9:
        case 10: return 396;
        case 11: return 900;
        case 12:
        case 13:
        case 20: return 2376;
        case 21: return 4752;
        case 22:
        case 30: return 8100;
        case 31: return 18000;
        case 32: return 20480;
        case 40:
        case 41: return 32768;
        case 42: return 34816;
        case 50: return 110400;
        case 51:
        case 52: return 184320;
        default: return 696320;
    }
}

static
BOOLEAN
NvdIsHighProfile(
    _In_ UINT8 ProfileIdc)
{
    switch (ProfileIdc)
    {
        case 100:
        case 110:
        case 122:
        case 244:
        case 44:
        case 83:
        case 86:
        case 118:
        case 128:
        case 138:
        case 139:
        case 134:
        case 135:
            return TRUE;

        default:
            return FALSE;
    }
}

VOID
H264SplitAnnexB(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ std::vector<H264NalUnit>& Units)
{
    H264NalUnit Unit = {};
    BOOLEAN HasUnit = FALSE;
    SIZE_T Index = 0;
    SIZE_T StartCode;
    SIZE_T End;

    while (Index + 3 <= Size)
    {
        if (Data[Index] != 0 || Data[Index + 1] != 0 || Data[Index + 2] != 1)
        {
            Index++;
            continue;
        }

        StartCode = (Index > 0 && Data[Index - 1] == 0) ? Index - 1 : Index;
        if (HasUnit)
        {
            /* Trailing zero bytes belong to the next start code, not the payload */
            End = StartCode;
            while (End > Unit.PayloadOffset && Data[End - 1] == 0)
                End--;
            Unit.PayloadSize = End - Unit.PayloadOffset;
            Units.push_back(Unit);
        }

        Unit.StartCodeOffset = StartCode;
        Unit.PayloadOffset = Index + 3;
        HasUnit = (Unit.PayloadOffset < Size);
        if (HasUnit)
        {
            Unit.Type = Data[Unit.PayloadOffset] & 0x1F;
            Unit.RefIdc = (Data[Unit.PayloadOffset] >> 5) & 3;
        }
        Index += 3;
    }

    if (HasUnit)
    {
        Unit.PayloadSize = Size - Unit.PayloadOffset;
        Units.push_back(Unit);
    }
}

static
VOID
NvdParseScalingList(
    _Inout_ BitReader& Reader,
    _Out_writes_(Count) UINT8* List,
    _In_ UINT Count,
    _Out_ BOOLEAN* UseDefault)
{
    INT32 LastScale = 8;
    INT32 NextScale = 8;
    UINT Index;

    *UseDefault = FALSE;
    for (Index = 0; Index < Count; Index++)
    {
        if (NextScale != 0)
        {
            NextScale = (LastScale + Reader.ReadSe() + 256) % 256;
            if (Index == 0 && NextScale == 0)
            {
                *UseDefault = TRUE;
                return;
            }
        }

        List[Index] = (UINT8)((NextScale == 0) ? LastScale : NextScale);
        LastScale = List[Index];
    }
}

static
VOID
NvdParseScalingMatrix(
    _Inout_ BitReader& Reader,
    _In_ UINT ListCount,
    _Out_writes_(12) BOOLEAN* Present,
    _Out_writes_(6 * 16) UINT8 (*Lists4x4)[16],
    _Out_writes_(6 * 64) UINT8 (*Lists8x8)[64],
    _Out_writes_(6) BOOLEAN* UseDefault4x4,
    _Out_writes_(6) BOOLEAN* UseDefault8x8)
{
    UINT Index;

    for (Index = 0; Index < ListCount; Index++)
    {
        Present[Index] = Reader.ReadFlag();
        if (!Present[Index])
            continue;

        if (Index < 6)
            NvdParseScalingList(Reader, Lists4x4[Index], 16, &UseDefault4x4[Index]);
        else
            NvdParseScalingList(Reader, Lists8x8[Index - 6], 64, &UseDefault8x8[Index - 6]);
    }
}

static
VOID
NvdParseHrd(
    _Inout_ BitReader& Reader)
{
    UINT32 CpbCount;
    UINT32 Index;

    CpbCount = Reader.ReadUe() + 1;
    Reader.SkipBits(8);
    for (Index = 0; Index < CpbCount && !Reader.HasFailed(); Index++)
    {
        Reader.ReadUe();
        Reader.ReadUe();
        Reader.SkipBits(1);
    }
    Reader.SkipBits(20);
}

static
VOID
NvdParseVui(
    _Inout_ BitReader& Reader,
    _Inout_ H264Sps* Sps)
{
    BOOLEAN NalHrd;
    BOOLEAN VclHrd;
    UINT32 AspectRatioIdc;

    if (Reader.ReadFlag())
    {
        AspectRatioIdc = Reader.ReadBits(8);
        if (AspectRatioIdc == 255)
        {
            Sps->SarWidth = (UINT16)Reader.ReadBits(16);
            Sps->SarHeight = (UINT16)Reader.ReadBits(16);
        }
    }

    if (Reader.ReadFlag())
        Reader.SkipBits(1);

    if (Reader.ReadFlag())
    {
        Reader.SkipBits(3);
        Sps->VideoFullRange = Reader.ReadFlag();
        Sps->ColourDescriptionPresent = Reader.ReadFlag();
        if (Sps->ColourDescriptionPresent)
        {
            Sps->ColourPrimaries = (UINT8)Reader.ReadBits(8);
            Sps->TransferCharacteristics = (UINT8)Reader.ReadBits(8);
            Sps->MatrixCoefficients = (UINT8)Reader.ReadBits(8);
        }
    }

    if (Reader.ReadFlag())
    {
        Reader.ReadUe();
        Reader.ReadUe();
    }

    Sps->TimingInfoPresent = Reader.ReadFlag();
    if (Sps->TimingInfoPresent)
    {
        Sps->NumUnitsInTick = Reader.ReadBits(32);
        Sps->TimeScale = Reader.ReadBits(32);
        Reader.SkipBits(1);
    }

    NalHrd = Reader.ReadFlag();
    if (NalHrd)
        NvdParseHrd(Reader);
    VclHrd = Reader.ReadFlag();
    if (VclHrd)
        NvdParseHrd(Reader);
    if (NalHrd || VclHrd)
        Reader.SkipBits(1);

    Reader.SkipBits(1);

    Sps->BitstreamRestriction = Reader.ReadFlag();
    if (Sps->BitstreamRestriction)
    {
        Reader.SkipBits(1);
        Reader.ReadUe();
        Reader.ReadUe();
        Reader.ReadUe();
        Reader.ReadUe();
        Sps->MaxNumReorderFrames = Reader.ReadUe();
        Sps->MaxDecFrameBuffering = Reader.ReadUe();
    }
}

BOOLEAN
H264ParseSps(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _Out_ H264Sps* Sps)
{
    std::vector<UINT8> Rbsp = NvdUnescapeRbsp(Payload + 1, Size - 1);
    BitReader Reader(Rbsp.data(), Rbsp.size());
    UINT32 CropUnitX;
    UINT32 CropUnitY;
    UINT32 FrameSize;
    UINT32 Index;
    UINT32 Value;

    memset(Sps, 0, sizeof(*Sps));

    Sps->ProfileIdc = (UINT8)Reader.ReadBits(8);
    Sps->ConstraintFlags = (UINT8)Reader.ReadBits(8);
    Sps->LevelIdc = (UINT8)Reader.ReadBits(8);
    Value = Reader.ReadUe();
    if (Value >= H264MaxSpsCount)
        return FALSE;
    Sps->SpsId = (UINT8)Value;

    Sps->ChromaFormatIdc = 1;
    if (NvdIsHighProfile(Sps->ProfileIdc))
    {
        Sps->ChromaFormatIdc = (UINT8)Reader.ReadUe();
        if (Sps->ChromaFormatIdc > 3)
            return FALSE;
        if (Sps->ChromaFormatIdc == 3)
            Sps->SeparateColourPlane = Reader.ReadFlag();

        Sps->BitDepthLumaMinus8 = (UINT8)Reader.ReadUe();
        Sps->BitDepthChromaMinus8 = (UINT8)Reader.ReadUe();
        Sps->QpPrimeYZeroTransformBypass = Reader.ReadFlag();
        Sps->ScalingMatrixPresent = Reader.ReadFlag();
        if (Sps->ScalingMatrixPresent)
        {
            NvdParseScalingMatrix(Reader,
                                  (Sps->ChromaFormatIdc != 3) ? 8 : 12,
                                  Sps->ScalingListPresent,
                                  Sps->ScalingList4x4,
                                  Sps->ScalingList8x8,
                                  Sps->UseDefault4x4,
                                  Sps->UseDefault8x8);
        }
    }

    Sps->Log2MaxFrameNumMinus4 = (UINT8)Reader.ReadUe();
    if (Sps->Log2MaxFrameNumMinus4 > 12)
        return FALSE;

    Sps->PicOrderCntType = (UINT8)Reader.ReadUe();
    if (Sps->PicOrderCntType == 0)
    {
        Sps->Log2MaxPicOrderCntLsbMinus4 = (UINT8)Reader.ReadUe();
        if (Sps->Log2MaxPicOrderCntLsbMinus4 > 12)
            return FALSE;
    }
    else if (Sps->PicOrderCntType == 1)
    {
        Sps->DeltaPicOrderAlwaysZero = Reader.ReadFlag();
        Sps->OffsetForNonRefPic = Reader.ReadSe();
        Sps->OffsetForTopToBottomField = Reader.ReadSe();
        Value = Reader.ReadUe();
        if (Value > 255)
            return FALSE;
        Sps->NumRefFramesInPicOrderCntCycle = (UINT8)Value;
        for (Index = 0; Index < Value; Index++)
            Sps->OffsetForRefFrame[Index] = Reader.ReadSe();
    }
    else if (Sps->PicOrderCntType != 2)
    {
        return FALSE;
    }

    Value = Reader.ReadUe();
    if (Value > H264MaxReferenceFrames)
        return FALSE;
    Sps->MaxNumRefFrames = (UINT8)Value;
    Sps->GapsInFrameNumAllowed = Reader.ReadFlag();
    Sps->PicWidthInMbsMinus1 = Reader.ReadUe();
    Sps->PicHeightInMapUnitsMinus1 = Reader.ReadUe();
    Sps->FrameMbsOnly = Reader.ReadFlag();
    if (!Sps->FrameMbsOnly)
        Sps->MbAdaptiveFrameField = Reader.ReadFlag();
    Sps->Direct8x8Inference = Reader.ReadFlag();

    Sps->FrameCropping = Reader.ReadFlag();
    if (Sps->FrameCropping)
    {
        Sps->CropLeft = Reader.ReadUe();
        Sps->CropRight = Reader.ReadUe();
        Sps->CropTop = Reader.ReadUe();
        Sps->CropBottom = Reader.ReadUe();
    }

    Sps->VuiPresent = Reader.ReadFlag();
    if (Sps->VuiPresent)
        NvdParseVui(Reader, Sps);

    if (Reader.HasFailed() ||
        Sps->PicWidthInMbsMinus1 > 1023 ||
        Sps->PicHeightInMapUnitsMinus1 > 1023)
    {
        return FALSE;
    }

    Sps->WidthInMbs = Sps->PicWidthInMbsMinus1 + 1;
    Sps->HeightInMbs = (Sps->PicHeightInMapUnitsMinus1 + 1) * (Sps->FrameMbsOnly ? 1 : 2);
    Sps->CodedWidth = Sps->WidthInMbs * 16;
    Sps->CodedHeight = Sps->HeightInMbs * 16;
    Sps->MaxFrameNum = 1u << (Sps->Log2MaxFrameNumMinus4 + 4);

    /* Crop units from Table 6-1, scaled by two for field coded heights */
    if (Sps->SeparateColourPlane || Sps->ChromaFormatIdc == 0)
    {
        CropUnitX = 1;
        CropUnitY = Sps->FrameMbsOnly ? 1 : 2;
    }
    else
    {
        CropUnitX = (Sps->ChromaFormatIdc == 3) ? 1 : 2;
        CropUnitY = ((Sps->ChromaFormatIdc == 1) ? 2 : 1) * (Sps->FrameMbsOnly ? 1 : 2);
    }

    Sps->Crop.left = (LONG)(Sps->CropLeft * CropUnitX);
    Sps->Crop.top = (LONG)(Sps->CropTop * CropUnitY);
    Sps->Crop.right = (LONG)(Sps->CodedWidth - Sps->CropRight * CropUnitX);
    Sps->Crop.bottom = (LONG)(Sps->CodedHeight - Sps->CropBottom * CropUnitY);
    if (Sps->Crop.right <= Sps->Crop.left || Sps->Crop.bottom <= Sps->Crop.top)
        Sps->Crop = { 0, 0, (LONG)Sps->CodedWidth, (LONG)Sps->CodedHeight };

    FrameSize = Sps->WidthInMbs * Sps->HeightInMbs;
    Sps->MaxDpbFrames = std::min<UINT32>(NvdMaxDpbMbs(Sps->LevelIdc) / FrameSize, 16);
    if (Sps->BitstreamRestriction)
        Sps->MaxDpbFrames = Sps->MaxDecFrameBuffering;
    Sps->MaxDpbFrames = std::max<UINT32>(Sps->MaxDpbFrames, Sps->MaxNumRefFrames);
    Sps->MaxDpbFrames = std::clamp<UINT32>(Sps->MaxDpbFrames, 1, 16);

    if (Sps->BitstreamRestriction)
        Sps->ReorderFrames = std::min(Sps->MaxNumReorderFrames, Sps->MaxDpbFrames);
    else if (Sps->ProfileIdc == 66 || Sps->ProfileIdc == 83)
        Sps->ReorderFrames = 0;
    else
        Sps->ReorderFrames = Sps->MaxDpbFrames;

    Sps->IsValid = TRUE;
    return TRUE;
}

BOOLEAN
H264ParsePps(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_reads_(H264MaxSpsCount) const H264Sps* SpsTable,
    _Out_ H264Pps* Pps)
{
    std::vector<UINT8> Rbsp = NvdUnescapeRbsp(Payload + 1, Size - 1);
    BitReader Reader(Rbsp.data(), Rbsp.size());
    const H264Sps* Sps;
    UINT32 Lists8x8;
    UINT32 Value;

    memset(Pps, 0, sizeof(*Pps));

    Value = Reader.ReadUe();
    if (Value >= H264MaxPpsCount)
        return FALSE;
    Pps->PpsId = (UINT8)Value;

    Value = Reader.ReadUe();
    if (Value >= H264MaxSpsCount || !SpsTable[Value].IsValid)
        return FALSE;
    Pps->SpsId = (UINT8)Value;
    Sps = &SpsTable[Value];

    Pps->EntropyCodingMode = Reader.ReadFlag();
    Pps->BottomFieldPicOrderInFramePresent = Reader.ReadFlag();
    Pps->NumSliceGroupsMinus1 = Reader.ReadUe();
    if (Pps->NumSliceGroupsMinus1 > 0)
        return FALSE;

    Pps->NumRefIdxL0DefaultActiveMinus1 = (UINT8)Reader.ReadUe();
    Pps->NumRefIdxL1DefaultActiveMinus1 = (UINT8)Reader.ReadUe();
    Pps->WeightedPred = Reader.ReadFlag();
    Pps->WeightedBipredIdc = (UINT8)Reader.ReadBits(2);
    Pps->PicInitQpMinus26 = (INT8)Reader.ReadSe();
    Pps->PicInitQsMinus26 = (INT8)Reader.ReadSe();
    Pps->ChromaQpIndexOffset = (INT8)Reader.ReadSe();
    Pps->DeblockingFilterControlPresent = Reader.ReadFlag();
    Pps->ConstrainedIntraPred = Reader.ReadFlag();
    Pps->RedundantPicCntPresent = Reader.ReadFlag();
    Pps->SecondChromaQpIndexOffset = Pps->ChromaQpIndexOffset;

    if (Reader.HasMoreRbspData())
    {
        Pps->Transform8x8Mode = Reader.ReadFlag();
        Lists8x8 = (Sps->ChromaFormatIdc != 3) ? 2 : 6;
        Pps->ScalingMatrixPresent = Reader.ReadFlag();
        if (Pps->ScalingMatrixPresent)
        {
            NvdParseScalingMatrix(Reader,
                                  6 + (Pps->Transform8x8Mode ? Lists8x8 : 0),
                                  Pps->ScalingListPresent,
                                  Pps->ScalingList4x4,
                                  Pps->ScalingList8x8,
                                  Pps->UseDefault4x4,
                                  Pps->UseDefault8x8);
        }
        Pps->SecondChromaQpIndexOffset = (INT8)Reader.ReadSe();
    }

    if (Pps->NumRefIdxL0DefaultActiveMinus1 > 31 || Pps->NumRefIdxL1DefaultActiveMinus1 > 31)
        return FALSE;

    Pps->IsValid = !Reader.HasFailed();
    return Pps->IsValid;
}

static
VOID
NvdSkipRefPicListModification(
    _Inout_ BitReader& Reader)
{
    UINT32 Idc;

    if (!Reader.ReadFlag())
        return;

    do
    {
        Idc = Reader.ReadUe();
        if (Idc < 3)
            Reader.ReadUe();
    } while (Idc != 3 && !Reader.HasFailed());
}

static
VOID
NvdSkipPredWeightTable(
    _Inout_ BitReader& Reader,
    _In_ BOOLEAN HasChroma,
    _In_ UINT32 RefCountL0,
    _In_ UINT32 RefCountL1,
    _In_ BOOLEAN IsB)
{
    UINT32 List;
    UINT32 Index;
    UINT32 Count;

    Reader.ReadUe();
    if (HasChroma)
        Reader.ReadUe();

    for (List = 0; List < (IsB ? 2u : 1u); List++)
    {
        Count = (List == 0) ? RefCountL0 : RefCountL1;
        for (Index = 0; Index < Count && !Reader.HasFailed(); Index++)
        {
            if (Reader.ReadFlag())
            {
                Reader.ReadSe();
                Reader.ReadSe();
            }

            if (HasChroma && Reader.ReadFlag())
            {
                Reader.ReadSe();
                Reader.ReadSe();
                Reader.ReadSe();
                Reader.ReadSe();
            }
        }
    }
}

BOOLEAN
H264ParseSliceHeader(
    _In_reads_bytes_(Size) const UINT8* Payload,
    _In_ SIZE_T Size,
    _In_reads_(H264MaxSpsCount) const H264Sps* SpsTable,
    _In_reads_(H264MaxPpsCount) const H264Pps* PpsTable,
    _Out_ H264SliceHeader* Header)
{
    /* The header sits well inside the first bytes; skip unescaping the whole slice */
    std::vector<UINT8> Rbsp = NvdUnescapeRbsp(Payload + 1, std::min<SIZE_T>(Size - 1, 1024));
    BitReader Reader(Rbsp.data(), Rbsp.size());
    const H264Pps* Pps;
    const H264Sps* Sps;
    H264Mmco Mmco;
    UINT32 RefCountL0;
    UINT32 RefCountL1;
    UINT32 Value;
    BOOLEAN IsIdr;
    BOOLEAN IsB;
    BOOLEAN IsP;

    Header->NalRefIdc = (Payload[0] >> 5) & 3;
    Header->NalType = Payload[0] & 0x1F;
    Header->Mmcos.clear();
    IsIdr = (Header->NalType == H264NalSliceIdr);

    Header->FirstMbInSlice = Reader.ReadUe();
    Value = Reader.ReadUe();
    if (Value > 9)
        return FALSE;
    Header->SliceType = (H264SliceType)(Value % 5);

    Value = Reader.ReadUe();
    if (Value >= H264MaxPpsCount || !PpsTable[Value].IsValid)
        return FALSE;
    Header->PpsId = (UINT8)Value;
    Pps = &PpsTable[Value];
    Sps = &SpsTable[Pps->SpsId];
    if (!Sps->IsValid)
        return FALSE;

    if (Sps->SeparateColourPlane)
        Reader.SkipBits(2);

    Header->FrameNum = Reader.ReadBits(Sps->Log2MaxFrameNumMinus4 + 4);
    Header->FieldPic = FALSE;
    Header->BottomField = FALSE;
    if (!Sps->FrameMbsOnly)
    {
        Header->FieldPic = Reader.ReadFlag();
        if (Header->FieldPic)
            Header->BottomField = Reader.ReadFlag();
    }

    Header->IdrPicId = IsIdr ? Reader.ReadUe() : 0;

    Header->PicOrderCntLsb = 0;
    Header->DeltaPicOrderCntBottom = 0;
    Header->DeltaPicOrderCnt[0] = 0;
    Header->DeltaPicOrderCnt[1] = 0;
    if (Sps->PicOrderCntType == 0)
    {
        Header->PicOrderCntLsb = Reader.ReadBits(Sps->Log2MaxPicOrderCntLsbMinus4 + 4);
        if (Pps->BottomFieldPicOrderInFramePresent && !Header->FieldPic)
            Header->DeltaPicOrderCntBottom = Reader.ReadSe();
    }
    else if (Sps->PicOrderCntType == 1 && !Sps->DeltaPicOrderAlwaysZero)
    {
        Header->DeltaPicOrderCnt[0] = Reader.ReadSe();
        if (Pps->BottomFieldPicOrderInFramePresent && !Header->FieldPic)
            Header->DeltaPicOrderCnt[1] = Reader.ReadSe();
    }

    if (Pps->RedundantPicCntPresent)
        Reader.ReadUe();

    IsB = (Header->SliceType == H264SliceB);
    IsP = (Header->SliceType == H264SliceP || Header->SliceType == H264SliceSp);
    if (IsB)
        Reader.SkipBits(1);

    RefCountL0 = Pps->NumRefIdxL0DefaultActiveMinus1 + 1u;
    RefCountL1 = Pps->NumRefIdxL1DefaultActiveMinus1 + 1u;
    if (IsP || IsB)
    {
        if (Reader.ReadFlag())
        {
            RefCountL0 = Reader.ReadUe() + 1;
            if (IsB)
                RefCountL1 = Reader.ReadUe() + 1;
        }

        NvdSkipRefPicListModification(Reader);
        if (IsB)
            NvdSkipRefPicListModification(Reader);
    }

    if ((Pps->WeightedPred && IsP) || (Pps->WeightedBipredIdc == 1 && IsB))
    {
        NvdSkipPredWeightTable(Reader,
                               !Sps->SeparateColourPlane && Sps->ChromaFormatIdc != 0,
                               RefCountL0,
                               RefCountL1,
                               IsB);
    }

    Header->NoOutputOfPriorPics = FALSE;
    Header->LongTermReference = FALSE;
    Header->AdaptiveRefPicMarking = FALSE;
    if (Header->NalRefIdc != 0)
    {
        if (IsIdr)
        {
            Header->NoOutputOfPriorPics = Reader.ReadFlag();
            Header->LongTermReference = Reader.ReadFlag();
        }
        else
        {
            Header->AdaptiveRefPicMarking = Reader.ReadFlag();
            while (Header->AdaptiveRefPicMarking && !Reader.HasFailed())
            {
                Mmco = {};
                Mmco.Operation = (H264MmcoOperation)Reader.ReadUe();
                if (Mmco.Operation == H264MmcoEnd)
                    break;
                if (Mmco.Operation > H264MmcoCurrentToLong || Header->Mmcos.size() >= 66)
                    return FALSE;

                if (Mmco.Operation == H264MmcoForgetShort || Mmco.Operation == H264MmcoShortToLong)
                    Mmco.DifferenceOfPicNumsMinus1 = Reader.ReadUe();
                if (Mmco.Operation == H264MmcoForgetLong)
                    Mmco.LongTermPicNum = Reader.ReadUe();
                if (Mmco.Operation == H264MmcoShortToLong ||
                    Mmco.Operation == H264MmcoCurrentToLong)
                {
                    Mmco.LongTermFrameIdx = Reader.ReadUe();
                }
                if (Mmco.Operation == H264MmcoSetMaxLongIndex)
                    Mmco.MaxLongTermFrameIdxPlus1 = Reader.ReadUe();

                Header->Mmcos.push_back(Mmco);
            }
        }
    }

    return !Reader.HasFailed();
}

static
const UINT8*
NvdDefault4x4(
    _In_ UINT Index)
{
    return (Index < 3) ? Default4x4Intra : Default4x4Inter;
}

static
const UINT8*
NvdDefault8x8(
    _In_ UINT Index)
{
    return (Index % 2 == 0) ? Default8x8Intra : Default8x8Inter;
}

VOID
H264ResolveScalingLists(
    _In_ const H264Sps& Sps,
    _In_ const H264Pps& Pps,
    _Out_writes_(6 * 16) UINT8 (*Lists4x4)[16],
    _Out_writes_(2 * 64) UINT8 (*Lists8x8)[64])
{
    UINT8 Sequence4x4[6][16];
    UINT8 Sequence8x8[6][64];
    UINT8 Picture4x4[6][16];
    UINT8 Picture8x8[6][64];
    const UINT8* Source;
    UINT Index;

    /* Sequence level: flat unless the SPS carries a matrix, then fall-back rule A */
    for (Index = 0; Index < 6; Index++)
    {
        if (!Sps.ScalingMatrixPresent)
            Source = nullptr;
        else if (Sps.ScalingListPresent[Index])
            Source = Sps.UseDefault4x4[Index] ? NvdDefault4x4(Index) : Sps.ScalingList4x4[Index];
        else if (Index == 0 || Index == 3)
            Source = NvdDefault4x4(Index);
        else
            Source = Sequence4x4[Index - 1];

        if (Source == nullptr)
            memset(Sequence4x4[Index], 16, 16);
        else
            memcpy(Sequence4x4[Index], Source, 16);

        if (!Sps.ScalingMatrixPresent)
            Source = nullptr;
        else if (Sps.ScalingListPresent[Index + 6])
            Source = Sps.UseDefault8x8[Index] ? NvdDefault8x8(Index) : Sps.ScalingList8x8[Index];
        else if (Index < 2)
            Source = NvdDefault8x8(Index);
        else
            Source = Sequence8x8[Index - 2];

        if (Source == nullptr)
            memset(Sequence8x8[Index], 16, 64);
        else
            memcpy(Sequence8x8[Index], Source, 64);
    }

    if (!Pps.ScalingMatrixPresent)
    {
        memcpy(Picture4x4, Sequence4x4, sizeof(Picture4x4));
        memcpy(Picture8x8, Sequence8x8, sizeof(Picture8x8));
    }
    else
    {
        /* Picture level: rule A without an SPS matrix, else rule B inherits the sequence lists */
        for (Index = 0; Index < 6; Index++)
        {
            if (Pps.ScalingListPresent[Index])
                Source = Pps.UseDefault4x4[Index] ? NvdDefault4x4(Index)
                                                  : Pps.ScalingList4x4[Index];
            else if (Index == 0 || Index == 3)
                Source = Sps.ScalingMatrixPresent ? Sequence4x4[Index] : NvdDefault4x4(Index);
            else
                Source = Picture4x4[Index - 1];
            memcpy(Picture4x4[Index], Source, 16);

            if (Pps.ScalingListPresent[Index + 6])
                Source = Pps.UseDefault8x8[Index] ? NvdDefault8x8(Index)
                                                  : Pps.ScalingList8x8[Index];
            else if (Index < 2)
                Source = Sps.ScalingMatrixPresent ? Sequence8x8[Index] : NvdDefault8x8(Index);
            else
                Source = Picture8x8[Index - 2];
            memcpy(Picture8x8[Index], Source, 64);
        }
    }

    memcpy(Lists4x4, Picture4x4, sizeof(Picture4x4));
    memcpy(Lists8x8[0], Picture8x8[0], 64);
    memcpy(Lists8x8[1], Picture8x8[1], 64);
}

PCSTR
H264ProfileName(
    _In_ UINT8 ProfileIdc)
{
    switch (ProfileIdc)
    {
        case 66: return "Baseline";
        case 77: return "Main";
        case 88: return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4";
        default: return "Unknown";
    }
}
