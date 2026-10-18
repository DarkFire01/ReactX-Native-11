/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     H.264 picture management on top of the D3D11 video decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "h264decoder.hpp"

#include <algorithm>

/* Some decoders read past the last slice in 128 byte steps, so the bitstream is padded */
constexpr UINT BitstreamAlignment = 128;

static const CHAR PictureLetters[5] = { 'P', 'B', 'I', 'P', 'I' };

static
BOOLEAN
NvdAcceptShortSlices(
    _In_ const D3D11_VIDEO_DECODER_CONFIG& Candidate)
{
    /* Only the short slice format is produced here, which leaves slice parsing to the driver */
    return Candidate.ConfigBitstreamRaw == 2;
}

HRESULT
H264Decoder::Configure(
    _In_ const H264Sps& Sps)
{
    HRESULT hr;

    if (Sps.ChromaFormatIdc != 1 || Sps.BitDepthLumaMinus8 != 0 || Sps.BitDepthChromaMinus8 != 0)
    {
        wprintf(L"error: only 8 bit 4:2:0 H.264 is supported (chroma format %u, %u bit)\n",
                Sps.ChromaFormatIdc, Sps.BitDepthLumaMinus8 + 8);
        return E_NOTIMPL;
    }

    hr = CreateDecoder(D3D11_DECODER_PROFILE_H264_VLD_NOFGT,
                       L"H.264 VLD",
                       Sps.CodedWidth,
                       Sps.CodedHeight,
                       DXGI_FORMAT_NV12,
                       Sps.MaxDpbFrames + 2,
                       NvdAcceptShortSlices);
    if (FAILED(hr))
        return hr;

    ActiveSps = Sps;
    return S_OK;
}

VOID
H264Decoder::ComputePoc(
    _In_ const H264SliceHeader& Header,
    _Inout_ Picture& Current)
{
    const H264Sps& Sps = ActiveSps;
    BOOLEAN IsIdr = (Header.NalType == H264NalSliceIdr);
    UINT32 MaxPocLsb = 1u << (Sps.Log2MaxPicOrderCntLsbMinus4 + 4);
    INT64 ExpectedDeltaPerCycle = 0;
    INT64 ExpectedPoc = 0;
    UINT32 AbsFrameNum;
    UINT32 CycleCount;
    UINT32 InCycle;
    INT32 PocMsb;
    UINT32 Index;

    /* FrameNumOffset is shared by POC types 1 and 2 (8.2.1.2, 8.2.1.3) */
    if (IsIdr)
        FrameNumOffset = 0;
    else if (PrevFrameNum > Header.FrameNum)
        FrameNumOffset = (PrevHadMmco5 ? 0 : PrevFrameNumOffset) + Sps.MaxFrameNum;
    else
        FrameNumOffset = PrevHadMmco5 ? 0 : PrevFrameNumOffset;

    switch (Sps.PicOrderCntType)
    {
        case 0:
            if (IsIdr)
            {
                PrevPocMsb = 0;
                PrevPocLsb = 0;
            }

            if ((INT32)Header.PicOrderCntLsb < PrevPocLsb &&
                PrevPocLsb - (INT32)Header.PicOrderCntLsb >= (INT32)(MaxPocLsb / 2))
            {
                PocMsb = PrevPocMsb + (INT32)MaxPocLsb;
            }
            else if ((INT32)Header.PicOrderCntLsb > PrevPocLsb &&
                     (INT32)Header.PicOrderCntLsb - PrevPocLsb > (INT32)(MaxPocLsb / 2))
            {
                PocMsb = PrevPocMsb - (INT32)MaxPocLsb;
            }
            else
            {
                PocMsb = PrevPocMsb;
            }

            Current.TopPoc = PocMsb + (INT32)Header.PicOrderCntLsb;
            Current.BottomPoc = Current.TopPoc + Header.DeltaPicOrderCntBottom;

            if (Header.NalRefIdc != 0)
            {
                PrevPocMsb = PocMsb;
                PrevPocLsb = (INT32)Header.PicOrderCntLsb;
            }
            break;

        case 1:
            AbsFrameNum = (Sps.NumRefFramesInPicOrderCntCycle != 0) ? FrameNumOffset + Header.FrameNum : 0;
            if (Header.NalRefIdc == 0 && AbsFrameNum > 0)
                AbsFrameNum--;

            for (Index = 0; Index < Sps.NumRefFramesInPicOrderCntCycle; Index++)
                ExpectedDeltaPerCycle += Sps.OffsetForRefFrame[Index];

            if (AbsFrameNum > 0)
            {
                CycleCount = (AbsFrameNum - 1) / Sps.NumRefFramesInPicOrderCntCycle;
                InCycle = (AbsFrameNum - 1) % Sps.NumRefFramesInPicOrderCntCycle;
                ExpectedPoc = (INT64)CycleCount * ExpectedDeltaPerCycle;
                for (Index = 0; Index <= InCycle; Index++)
                    ExpectedPoc += Sps.OffsetForRefFrame[Index];
            }

            if (Header.NalRefIdc == 0)
                ExpectedPoc += Sps.OffsetForNonRefPic;

            Current.TopPoc = (INT32)ExpectedPoc + Header.DeltaPicOrderCnt[0];
            Current.BottomPoc = Current.TopPoc + Sps.OffsetForTopToBottomField + Header.DeltaPicOrderCnt[1];
            break;

        default:
            if (IsIdr)
                Current.TopPoc = 0;
            else if (Header.NalRefIdc == 0)
                Current.TopPoc = 2 * (INT32)(FrameNumOffset + Header.FrameNum) - 1;
            else
                Current.TopPoc = 2 * (INT32)(FrameNumOffset + Header.FrameNum);
            Current.BottomPoc = Current.TopPoc;
            break;
    }

    Current.Poc = std::min(Current.TopPoc, Current.BottomPoc);
    PrevFrameNumOffset = FrameNumOffset;
    PrevFrameNum = Header.FrameNum;
}

VOID
H264Decoder::UpdateFrameNumWrap(
    _In_ UINT32 CurrentFrameNum)
{
    for (Picture& Entry : Dpb)
    {
        if (!Entry.IsShortTerm)
            continue;

        if (Entry.FrameNum > CurrentFrameNum)
            Entry.FrameNumWrap = (INT32)Entry.FrameNum - (INT32)ActiveSps.MaxFrameNum;
        else
            Entry.FrameNumWrap = (INT32)Entry.FrameNum;
    }
}

VOID
H264Decoder::ApplySlidingWindow()
{
    Picture* Oldest = nullptr;
    UINT32 References = 0;

    for (Picture& Entry : Dpb)
    {
        if (Entry.IsShortTerm || Entry.IsLongTerm)
            References++;
        if (Entry.IsShortTerm && (Oldest == nullptr || Entry.FrameNumWrap < Oldest->FrameNumWrap))
            Oldest = &Entry;
    }

    if (References >= std::max<UINT32>(ActiveSps.MaxNumRefFrames, 1) && Oldest != nullptr)
        Oldest->IsShortTerm = FALSE;
}

VOID
H264Decoder::FillFrameNumGap(
    _In_ const H264SliceHeader& Header)
{
    UINT32 Missing;
    Picture Filler;

    if (Header.NalType == H264NalSliceIdr || !HasSeenIdr)
        return;
    if (Header.FrameNum == PrevRefFrameNum || Header.FrameNum == (PrevRefFrameNum + 1) % ActiveSps.MaxFrameNum)
        return;

    if (!ActiveSps.GapsInFrameNumAllowed)
    {
        if (!WarnedGap)
            wprintf(L"warning: frame_num jumped from %u to %u (lost pictures?)\n", PrevRefFrameNum, Header.FrameNum);
        WarnedGap = TRUE;
        return;
    }

    /* 8.2.5.2: each skipped frame_num becomes a non-existing short-term reference */
    for (Missing = (PrevRefFrameNum + 1) % ActiveSps.MaxFrameNum;
         Missing != Header.FrameNum;
         Missing = (Missing + 1) % ActiveSps.MaxFrameNum)
    {
        UpdateFrameNumWrap(Missing);
        ApplySlidingWindow();
        PruneDpb();

        Filler = Picture();
        Filler.FrameNum = Missing;
        Filler.IsShortTerm = TRUE;
        Filler.IsNonExisting = TRUE;
        Dpb.push_back(Filler);
        PrevRefFrameNum = Missing;
    }
}

VOID
H264Decoder::MarkCurrent(
    _In_ const H264SliceHeader& Header,
    _Inout_ Picture& Current,
    _Out_ BOOLEAN* HadMmco5)
{
    BOOLEAN IsLongTermCurrent = FALSE;
    INT32 PicNumX;
    INT32 Temp;

    *HadMmco5 = FALSE;
    if (Header.NalRefIdc == 0)
        return;

    UpdateFrameNumWrap(Header.FrameNum);

    if (Header.NalType == H264NalSliceIdr)
    {
        for (Picture& Entry : Dpb)
        {
            Entry.IsShortTerm = FALSE;
            Entry.IsLongTerm = FALSE;
        }

        if (Header.LongTermReference)
        {
            IsLongTermCurrent = TRUE;
            Current.LongTermFrameIdx = 0;
            MaxLongTermFrameIdx = 0;
        }
        else
        {
            MaxLongTermFrameIdx = -1;
        }
    }
    else if (!Header.AdaptiveRefPicMarking)
    {
        ApplySlidingWindow();
    }
    else
    {
        /* 8.2.5.4, frames only: PicNum is FrameNumWrap and LongTermPicNum is the index */
        for (const H264Mmco& Mmco : Header.Mmcos)
        {
            PicNumX = (INT32)Header.FrameNum - (INT32)(Mmco.DifferenceOfPicNumsMinus1 + 1);

            switch (Mmco.Operation)
            {
                case H264MmcoForgetShort:
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsShortTerm && Entry.FrameNumWrap == PicNumX)
                            Entry.IsShortTerm = FALSE;
                    }
                    break;

                case H264MmcoForgetLong:
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsLongTerm && Entry.LongTermFrameIdx == Mmco.LongTermPicNum)
                            Entry.IsLongTerm = FALSE;
                    }
                    break;

                case H264MmcoShortToLong:
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsLongTerm && Entry.LongTermFrameIdx == Mmco.LongTermFrameIdx)
                            Entry.IsLongTerm = FALSE;
                    }
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsShortTerm && Entry.FrameNumWrap == PicNumX)
                        {
                            Entry.IsShortTerm = FALSE;
                            Entry.IsLongTerm = TRUE;
                            Entry.LongTermFrameIdx = Mmco.LongTermFrameIdx;
                        }
                    }
                    break;

                case H264MmcoSetMaxLongIndex:
                    MaxLongTermFrameIdx = (INT32)Mmco.MaxLongTermFrameIdxPlus1 - 1;
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsLongTerm && (INT32)Entry.LongTermFrameIdx > MaxLongTermFrameIdx)
                            Entry.IsLongTerm = FALSE;
                    }
                    break;

                case H264MmcoForgetAll:
                    for (Picture& Entry : Dpb)
                    {
                        Entry.IsShortTerm = FALSE;
                        Entry.IsLongTerm = FALSE;
                    }
                    MaxLongTermFrameIdx = -1;
                    *HadMmco5 = TRUE;
                    break;

                case H264MmcoCurrentToLong:
                    for (Picture& Entry : Dpb)
                    {
                        if (Entry.IsLongTerm && Entry.LongTermFrameIdx == Mmco.LongTermFrameIdx)
                            Entry.IsLongTerm = FALSE;
                    }
                    IsLongTermCurrent = TRUE;
                    Current.LongTermFrameIdx = Mmco.LongTermFrameIdx;
                    break;

                default:
                    break;
            }
        }
    }

    Current.IsLongTerm = IsLongTermCurrent;
    Current.IsShortTerm = !IsLongTermCurrent;

    if (*HadMmco5)
    {
        /* The picture now counts as frame_num 0 with its POC rebased to zero */
        Temp = std::min(Current.TopPoc, Current.BottomPoc);
        Current.TopPoc -= Temp;
        Current.BottomPoc -= Temp;
        Current.Poc = 0;
        Current.FrameNum = 0;
        PrevPocMsb = 0;
        PrevPocLsb = Current.TopPoc;
        PrevFrameNum = 0;
    }
}

VOID
H264Decoder::PruneDpb()
{
    SIZE_T Index = 0;

    while (Index < Dpb.size())
    {
        Picture& Entry = Dpb[Index];

        if (Entry.IsShortTerm || Entry.IsLongTerm || Entry.NeedsOutput)
        {
            Index++;
            continue;
        }

        if (Entry.SurfaceIndex >= 0)
            SurfaceRefs[Entry.SurfaceIndex]--;
        Dpb.erase(Dpb.begin() + Index);
    }
}

VOID
H264Decoder::OutputOne(
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    Picture* Next = nullptr;
    DecodedFrame Frame;

    for (Picture& Entry : Dpb)
    {
        if (Entry.NeedsOutput && (Next == nullptr || Entry.Poc < Next->Poc))
            Next = &Entry;
    }

    if (Next == nullptr)
        return;

    /* The consumer holds its own reference until ReleaseFrame */
    Frame.SurfaceIndex = (UINT)Next->SurfaceIndex;
    Frame.PresentationTime = Next->PresentationTime;
    Frame.DisplayIndex = DisplayCount++;
    Frame.Visible = ActiveSps.Crop;
    Frame.Type = PictureLetters[Next->Type];
    SurfaceRefs[Next->SurfaceIndex]++;
    Ready.push_back(Frame);

    Next->NeedsOutput = FALSE;
    PruneDpb();
}

INT
H264Decoder::AcquireSurface(
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    UINT Attempt;
    INT Surface;

    for (Attempt = 0; Attempt < 2; Attempt++)
    {
        Surface = FindFreeSurface();
        if (Surface >= 0)
            return Surface;

        /* Everything is busy: push the oldest waiting picture out and try again */
        OutputOne(Ready);
    }

    return -1;
}

VOID
H264Decoder::FillPictureParameters(
    _In_ const Picture& Current,
    _In_ const H264SliceHeader& Header,
    _In_ H264SliceType PictureType,
    _Out_ DXVA_PicParams_H264* Params)
{
    const H264Sps& Sps = ActiveSps;
    const H264Pps& Pps = PpsTable[Header.PpsId];
    UINT Slot = 0;

    memset(Params, 0, sizeof(*Params));

    Params->wFrameWidthInMbsMinus1 = (USHORT)(Sps.WidthInMbs - 1);
    Params->wFrameHeightInMbsMinus1 = (USHORT)(Sps.HeightInMbs - 1);
    Params->CurrPic.Index7Bits = (UCHAR)Current.SurfaceIndex;
    Params->num_ref_frames = Sps.MaxNumRefFrames;

    Params->field_pic_flag = 0;
    Params->MbaffFrameFlag = Sps.MbAdaptiveFrameField ? 1 : 0;
    Params->residual_colour_transform_flag = Sps.SeparateColourPlane ? 1 : 0;
    Params->chroma_format_idc = Sps.ChromaFormatIdc;
    Params->RefPicFlag = (Header.NalRefIdc != 0) ? 1 : 0;
    Params->constrained_intra_pred_flag = Pps.ConstrainedIntraPred ? 1 : 0;
    Params->weighted_pred_flag = Pps.WeightedPred ? 1 : 0;
    Params->weighted_bipred_idc = Pps.WeightedBipredIdc;
    Params->MbsConsecutiveFlag = 1;
    Params->frame_mbs_only_flag = Sps.FrameMbsOnly ? 1 : 0;
    Params->transform_8x8_mode_flag = Pps.Transform8x8Mode ? 1 : 0;
    Params->MinLumaBipredSize8x8Flag = (Sps.LevelIdc >= 31) ? 1 : 0;
    Params->IntraPicFlag = (PictureType == H264SliceI) ? 1 : 0;

    Params->bit_depth_luma_minus8 = Sps.BitDepthLumaMinus8;
    Params->bit_depth_chroma_minus8 = Sps.BitDepthChromaMinus8;

    /* Same value FFmpeg uses for drivers that do not need a vendor marker here */
    Params->Reserved16Bits = 3;
    Params->StatusReportFeedbackNumber = ++StatusFeedback;

    memset(Params->RefFrameList, 0xFF, sizeof(Params->RefFrameList));
    for (const Picture& Entry : Dpb)
    {
        if ((!Entry.IsShortTerm && !Entry.IsLongTerm) || Slot >= 16)
            continue;

        Params->RefFrameList[Slot].Index7Bits = (UCHAR)(Entry.IsNonExisting ? Current.SurfaceIndex
                                                                            : Entry.SurfaceIndex);
        Params->RefFrameList[Slot].AssociatedFlag = Entry.IsLongTerm ? 1 : 0;
        Params->FieldOrderCntList[Slot][0] = Entry.TopPoc;
        Params->FieldOrderCntList[Slot][1] = Entry.BottomPoc;
        Params->FrameNumList[Slot] = (USHORT)(Entry.IsLongTerm ? Entry.LongTermFrameIdx : Entry.FrameNum);
        Params->UsedForReferenceFlags |= 3u << (2 * Slot);
        if (Entry.IsNonExisting)
            Params->NonExistingFrameFlags |= (USHORT)(1u << Slot);
        Slot++;
    }

    Params->CurrFieldOrderCnt[0] = Current.TopPoc;
    Params->CurrFieldOrderCnt[1] = Current.BottomPoc;

    Params->pic_init_qs_minus26 = Pps.PicInitQsMinus26;
    Params->chroma_qp_index_offset = Pps.ChromaQpIndexOffset;
    Params->second_chroma_qp_index_offset = Pps.SecondChromaQpIndexOffset;
    Params->ContinuationFlag = 1;
    Params->pic_init_qp_minus26 = Pps.PicInitQpMinus26;
    Params->num_ref_idx_l0_active_minus1 = Pps.NumRefIdxL0DefaultActiveMinus1;
    Params->num_ref_idx_l1_active_minus1 = Pps.NumRefIdxL1DefaultActiveMinus1;
    Params->frame_num = (USHORT)Header.FrameNum;
    Params->log2_max_frame_num_minus4 = Sps.Log2MaxFrameNumMinus4;
    Params->pic_order_cnt_type = Sps.PicOrderCntType;
    Params->log2_max_pic_order_cnt_lsb_minus4 = Sps.Log2MaxPicOrderCntLsbMinus4;
    Params->delta_pic_order_always_zero_flag = Sps.DeltaPicOrderAlwaysZero ? 1 : 0;
    Params->direct_8x8_inference_flag = Sps.Direct8x8Inference ? 1 : 0;
    Params->entropy_coding_mode_flag = Pps.EntropyCodingMode ? 1 : 0;
    Params->pic_order_present_flag = Pps.BottomFieldPicOrderInFramePresent ? 1 : 0;
    Params->deblocking_filter_control_present_flag = Pps.DeblockingFilterControlPresent ? 1 : 0;
    Params->redundant_pic_cnt_present_flag = Pps.RedundantPicCntPresent ? 1 : 0;
}

HRESULT
H264Decoder::Submit(
    _In_ const Picture& Current,
    _In_ const H264SliceHeader& Header,
    _In_ H264SliceType PictureType,
    _In_ const AccessUnit& Unit)
{
    static const UINT8 StartCode[3] = { 0, 0, 1 };
    std::vector<DXVA_Slice_H264_Short> SliceControl;
    D3D11_VIDEO_DECODER_BUFFER_DESC Buffers[4] = {};
    DXVA_PicParams_H264 PictureParams;
    DXVA_Qmatrix_H264 Matrix;
    DXVA_Slice_H264_Short Entry;
    UINT MacroblockCount = ActiveSps.WidthInMbs * ActiveSps.HeightInMbs;
    UINT8* Bitstream = nullptr;
    UINT Capacity = 0;
    UINT Written = 0;
    UINT Padding;
    UINT Retries = 0;
    UINT32 Bytes = 0;
    double Begin;
    double SubmitMs;
    HRESULT hr;

    FillPictureParameters(Current, Header, PictureType, &PictureParams);
    H264ResolveScalingLists(ActiveSps, PpsTable[Header.PpsId], Matrix.bScalingLists4x4, Matrix.bScalingLists8x8);

    Begin = NvdNowMs();
    hr = BeginFrame(Current.SurfaceIndex, &Retries);
    if (FAILED(hr))
        return hr;

    hr = CopyToDecoderBuffer(D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &PictureParams, sizeof(PictureParams));
    if (SUCCEEDED(hr))
        hr = CopyToDecoderBuffer(D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX, &Matrix, sizeof(Matrix));

    if (SUCCEEDED(hr))
    {
        hr = VideoContext->GetDecoderBuffer(Decoder.Get(),
                                            D3D11_VIDEO_DECODER_BUFFER_BITSTREAM,
                                            &Capacity,
                                            reinterpret_cast<VOID**>(&Bitstream));
    }

    if (SUCCEEDED(hr))
    {
        for (const SliceRef& Slice : Slices)
        {
            if (Written + sizeof(StartCode) + Slice.Size > Capacity)
            {
                hr = E_OUTOFMEMORY;
                break;
            }

            Entry.BSNALunitDataLocation = Written;
            Entry.SliceBytesInBuffer = (UINT)(sizeof(StartCode) + Slice.Size);
            Entry.wBadSliceChopping = 0;
            SliceControl.push_back(Entry);

            memcpy(Bitstream + Written, StartCode, sizeof(StartCode));
            memcpy(Bitstream + Written + sizeof(StartCode), &Unit.Data[Slice.Offset], Slice.Size);
            Written += Entry.SliceBytesInBuffer;
        }

        Bytes = Written;
        Padding = std::min<UINT>((BitstreamAlignment - (Written % BitstreamAlignment)) % BitstreamAlignment,
                                 Capacity - Written);
        memset(Bitstream + Written, 0, Padding);
        Written += Padding;
        if (!SliceControl.empty())
            SliceControl.back().SliceBytesInBuffer += Padding;

        VideoContext->ReleaseDecoderBuffer(Decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_BITSTREAM);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyToDecoderBuffer(D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL,
                                 SliceControl.data(),
                                 (UINT)(SliceControl.size() * sizeof(DXVA_Slice_H264_Short)));
    }

    if (SUCCEEDED(hr))
    {
        Buffers[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
        Buffers[0].DataSize = sizeof(PictureParams);
        Buffers[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_INVERSE_QUANTIZATION_MATRIX;
        Buffers[1].DataSize = sizeof(Matrix);
        Buffers[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
        Buffers[2].DataSize = (UINT)(SliceControl.size() * sizeof(DXVA_Slice_H264_Short));
        Buffers[2].NumMBsInBuffer = MacroblockCount;
        Buffers[3].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        Buffers[3].DataSize = Written;
        Buffers[3].NumMBsInBuffer = MacroblockCount;

        hr = VideoContext->SubmitDecoderBuffers(Decoder.Get(), 4, Buffers);
    }

    hr = EndFrame(hr, Begin);
    if (FAILED(hr))
        return hr;

    SubmitMs = NvdNowMs() - Begin;
    Stats.OnPictureDecoded(DecodeCount, PictureLetters[PictureType], Current.Poc, Bytes,
                           (UINT32)Slices.size(), SubmitMs, Retries);
    return S_OK;
}

HRESULT
H264Decoder::DecodePicture(
    _In_ const AccessUnit& Unit,
    _In_ const H264SliceHeader& Header,
    _In_ H264SliceType PictureType,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    const H264Sps& Sps = SpsTable[PpsTable[Header.PpsId].SpsId];
    BOOLEAN IsIdr = (Header.NalType == H264NalSliceIdr);
    BOOLEAN HadMmco5;
    Picture Current;
    HRESULT hr;

    if (Header.FieldPic)
    {
        wprintf(L"error: field coded pictures are not supported\n");
        return E_NOTIMPL;
    }

    if (!IsConfigured())
    {
        hr = Configure(Sps);
        if (FAILED(hr))
            return hr;
    }
    else if (Sps.CodedWidth != ActiveSps.CodedWidth || Sps.CodedHeight != ActiveSps.CodedHeight)
    {
        wprintf(L"error: resolution changes mid stream are not supported\n");
        return E_NOTIMPL;
    }
    else
    {
        /* Same frame size: pick up new VUI or reference counts without rebuilding */
        UINT SurfaceCount = (UINT)SurfaceRefs.size();
        ActiveSps = Sps;
        ActiveSps.MaxDpbFrames = std::min(ActiveSps.MaxDpbFrames, SurfaceCount - 2 - ConsumerSurfaces);
    }

    if (!HasSeenIdr && !IsIdr)
        return S_FALSE;

    if (IsIdr)
    {
        /* Earlier pictures are either shown first or thrown away, then references reset */
        if (Header.NoOutputOfPriorPics)
        {
            for (Picture& Entry : Dpb)
                Entry.NeedsOutput = FALSE;
        }
        while (std::any_of(Dpb.begin(), Dpb.end(), [](const Picture& Entry) { return Entry.NeedsOutput; }))
            OutputOne(Ready);

        for (Picture& Entry : Dpb)
        {
            Entry.IsShortTerm = FALSE;
            Entry.IsLongTerm = FALSE;
        }
        PruneDpb();

        HasSeenIdr = TRUE;
        PrevRefFrameNum = 0;
        PrevHadMmco5 = FALSE;
    }

    FillFrameNumGap(Header);

    Current.FrameNum = Header.FrameNum;
    Current.PresentationTime = Unit.PresentationTime;
    Current.Type = PictureType;
    ComputePoc(Header, Current);

    Current.SurfaceIndex = AcquireSurface(Ready);
    if (Current.SurfaceIndex < 0)
    {
        wprintf(L"error: every decode surface is in use\n");
        return E_OUTOFMEMORY;
    }

    hr = Submit(Current, Header, PictureType, Unit);
    if (FAILED(hr))
        return hr;
    DecodeCount++;

    MarkCurrent(Header, Current, &HadMmco5);
    PruneDpb();

    if (HadMmco5)
    {
        while (std::any_of(Dpb.begin(), Dpb.end(), [](const Picture& Entry) { return Entry.NeedsOutput; }))
            OutputOne(Ready);
    }

    if (Header.NalRefIdc != 0)
        PrevRefFrameNum = Current.FrameNum;
    PrevHadMmco5 = HadMmco5;

    Current.NeedsOutput = TRUE;
    SurfaceRefs[Current.SurfaceIndex]++;
    Dpb.push_back(Current);

    /* C.4.5.3 style bumping: keep at most the reorder depth waiting, and fit the DPB */
    for (;;)
    {
        UINT32 Waiting = (UINT32)std::count_if(Dpb.begin(), Dpb.end(),
                                               [](const Picture& Entry) { return Entry.NeedsOutput; });

        if (Waiting == 0)
            break;
        if (Waiting <= ActiveSps.ReorderFrames && Dpb.size() <= ActiveSps.MaxDpbFrames)
            break;

        OutputOne(Ready);
    }

    return S_OK;
}

HRESULT
H264Decoder::DecodeAccessUnit(
    _In_ const AccessUnit& Unit,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    std::vector<H264NalUnit> Units;
    H264SliceHeader First = {};
    H264SliceHeader Header = {};
    H264SliceType PictureType = H264SliceI;
    BOOLEAN HasPicture = FALSE;
    H264Sps Sps;
    H264Pps Pps;
    HRESULT hr;

    H264SplitAnnexB(Unit.Data.data(), Unit.Data.size(), Units);
    Slices.clear();

    for (const H264NalUnit& Nal : Units)
    {
        const UINT8* Payload = &Unit.Data[Nal.PayloadOffset];

        if (Nal.PayloadSize < 2)
            continue;

        switch (Nal.Type)
        {
            case H264NalSps:
                if (H264ParseSps(Payload, Nal.PayloadSize, &Sps))
                    SpsTable[Sps.SpsId] = Sps;
                else
                    wprintf(L"warning: unusable SPS skipped\n");
                break;

            case H264NalPps:
                if (H264ParsePps(Payload, Nal.PayloadSize, SpsTable.data(), &Pps))
                    PpsTable[Pps.PpsId] = Pps;
                else
                    wprintf(L"warning: unusable PPS skipped (FMO is not supported)\n");
                break;

            case H264NalSlice:
            case H264NalSliceIdr:
                if (!H264ParseSliceHeader(Payload, Nal.PayloadSize, SpsTable.data(), PpsTable.data(), &Header))
                {
                    wprintf(L"warning: slice header could not be parsed\n");
                    break;
                }

                /* A slice starting at macroblock 0 opens a new picture */
                if (HasPicture && Header.FirstMbInSlice == 0)
                {
                    hr = DecodePicture(Unit, First, PictureType, Ready);
                    if (FAILED(hr))
                        return hr;
                    Slices.clear();
                    HasPicture = FALSE;
                }

                if (!HasPicture)
                {
                    First = Header;
                    PictureType = H264SliceI;
                    HasPicture = TRUE;
                }

                if (Header.SliceType == H264SliceB)
                    PictureType = H264SliceB;
                else if (Header.SliceType == H264SliceP && PictureType != H264SliceB)
                    PictureType = H264SliceP;

                Slices.push_back({ Nal.PayloadOffset, Nal.PayloadSize });
                break;

            default:
                break;
        }
    }

    if (!HasPicture)
        return S_OK;

    return DecodePicture(Unit, First, PictureType, Ready);
}

VOID
H264Decoder::Drain(
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    while (std::any_of(Dpb.begin(), Dpb.end(), [](const Picture& Entry) { return Entry.NeedsOutput; }))
        OutputOne(Ready);
}

VOID
H264Decoder::Reset()
{
    for (const Picture& Entry : Dpb)
    {
        if (Entry.SurfaceIndex >= 0)
            SurfaceRefs[Entry.SurfaceIndex]--;
    }

    Dpb.clear();
    PrevPocMsb = 0;
    PrevPocLsb = 0;
    PrevFrameNum = 0;
    PrevFrameNumOffset = 0;
    PrevRefFrameNum = 0;
    FrameNumOffset = 0;
    PrevHadMmco5 = FALSE;
    MaxLongTermFrameIdx = -1;
    HasSeenIdr = FALSE;
}

VideoFormatInfo
H264Decoder::GetFormat() const
{
    VideoFormatInfo Info = {};

    Info.SurfaceWidth = ActiveSps.CodedWidth;
    Info.SurfaceHeight = ActiveSps.CodedHeight;
    Info.Format = DXGI_FORMAT_NV12;
    Info.BitDepth = 8;
    Info.Visible = ActiveSps.Crop;
    Info.SarWidth = ActiveSps.SarWidth;
    Info.SarHeight = ActiveSps.SarHeight;
    Info.IsFullRange = ActiveSps.VideoFullRange;
    Info.HasColourDescription = ActiveSps.ColourDescriptionPresent;
    Info.MatrixCoefficients = ActiveSps.MatrixCoefficients;
    if (ActiveSps.TimingInfoPresent && ActiveSps.NumUnitsInTick != 0)
        Info.FrameRate = (double)ActiveSps.TimeScale / (2.0 * ActiveSps.NumUnitsInTick);
    return Info;
}

std::wstring
H264Decoder::Describe() const
{
    const H264Sps& Sps = ActiveSps;
    std::wstring Text;
    WCHAR Line[256];

    swprintf_s(Line, L"codec          H.264 %S (%u), level %u.%u, constraint flags %02X\n",
               H264ProfileName(Sps.ProfileIdc), Sps.ProfileIdc, Sps.LevelIdc / 10, Sps.LevelIdc % 10,
               Sps.ConstraintFlags);
    Text += Line;
    swprintf_s(Line, L"coded size     %ux%u (%ux%u macroblocks)%s\n",
               Sps.CodedWidth, Sps.CodedHeight, Sps.WidthInMbs, Sps.HeightInMbs,
               Sps.FrameMbsOnly ? L"" : (Sps.MbAdaptiveFrameField ? L", MBAFF" : L", field coded"));
    Text += Line;
    swprintf_s(Line, L"display        %ldx%ld at (%ld,%ld), SAR %u:%u\n",
               Sps.Crop.right - Sps.Crop.left, Sps.Crop.bottom - Sps.Crop.top, Sps.Crop.left, Sps.Crop.top,
               Sps.SarWidth ? Sps.SarWidth : 1, Sps.SarHeight ? Sps.SarHeight : 1);
    Text += Line;
    swprintf_s(Line, L"references     %u, dpb %u frames, reorder %u, poc type %u\n",
               Sps.MaxNumRefFrames, Sps.MaxDpbFrames, Sps.ReorderFrames, Sps.PicOrderCntType);
    Text += Line;
    if (Sps.ColourDescriptionPresent)
    {
        swprintf_s(Line, L"colour         primaries %u, transfer %u, matrix %u, %s range\n",
                   Sps.ColourPrimaries, Sps.TransferCharacteristics, Sps.MatrixCoefficients,
                   Sps.VideoFullRange ? L"full" : L"limited");
        Text += Line;
    }
    swprintf_s(Line, L"decoder        H264_VLD_NOFGT, NV12, %u surfaces\n", GetSurfaceCount());
    Text += Line;
    swprintf_s(Line, L"config         #%u: bitstream raw %u, decoder specific 0x%X, min buffers %u\n",
               ConfigIndex, Config.ConfigBitstreamRaw, Config.ConfigDecoderSpecific,
               Config.ConfigMinRenderTargetBuffCount);
    Text += Line;
    return Text;
}
