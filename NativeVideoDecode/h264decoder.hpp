/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     H.264 picture management on top of the D3D11 video decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "h264syntax.hpp"
#include "videodecoder.hpp"

#include <dxva.h>

class H264Decoder final : public VideoDecoder
{
public:
    explicit H264Decoder(
        _In_ PlaybackStats& Stats)
        : VideoDecoder(Stats)
    {
    }

    HRESULT
    DecodeAccessUnit(
        _In_ const AccessUnit& Unit,
        _Inout_ std::vector<DecodedFrame>& Ready) override;

    VOID
    Drain(
        _Inout_ std::vector<DecodedFrame>& Ready) override;

    VOID
    Reset() override;

    VideoFormatInfo
    GetFormat() const override;

    std::wstring
    Describe() const override;

    UINT
    GetDpbFill() const override
    {
        return (UINT)Dpb.size();
    }

    UINT
    GetDpbSize() const override
    {
        return ActiveSps.MaxDpbFrames;
    }

private:
    struct Picture
    {
        INT SurfaceIndex = -1;
        INT32 TopPoc = 0;
        INT32 BottomPoc = 0;
        INT32 Poc = 0;
        UINT32 FrameNum = 0;
        INT32 FrameNumWrap = 0;
        UINT32 LongTermFrameIdx = 0;
        BOOLEAN IsShortTerm = FALSE;
        BOOLEAN IsLongTerm = FALSE;
        BOOLEAN IsNonExisting = FALSE;
        BOOLEAN NeedsOutput = FALSE;
        INT64 PresentationTime = NvdNoTimestamp;
        H264SliceType Type = H264SliceI;
    };

    struct SliceRef
    {
        SIZE_T Offset;
        SIZE_T Size;
    };

    HRESULT
    Configure(
        _In_ const H264Sps& Sps);

    HRESULT
    DecodePicture(
        _In_ const AccessUnit& Unit,
        _In_ const H264SliceHeader& Header,
        _In_ H264SliceType PictureType,
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    ComputePoc(
        _In_ const H264SliceHeader& Header,
        _Inout_ Picture& Current);

    VOID
    FillFrameNumGap(
        _In_ const H264SliceHeader& Header);

    VOID
    MarkCurrent(
        _In_ const H264SliceHeader& Header,
        _Inout_ Picture& Current,
        _Out_ BOOLEAN* HadMmco5);

    VOID
    UpdateFrameNumWrap(
        _In_ UINT32 CurrentFrameNum);

    VOID
    ApplySlidingWindow();

    VOID
    OutputOne(
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    PruneDpb();

    INT
    AcquireSurface(
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    FillPictureParameters(
        _In_ const Picture& Current,
        _In_ const H264SliceHeader& Header,
        _In_ H264SliceType PictureType,
        _Out_ DXVA_PicParams_H264* Params);

    HRESULT
    Submit(
        _In_ const Picture& Current,
        _In_ const H264SliceHeader& Header,
        _In_ H264SliceType PictureType,
        _In_ const AccessUnit& Unit);

    std::vector<H264Sps> SpsTable = std::vector<H264Sps>(H264MaxSpsCount);
    std::vector<H264Pps> PpsTable = std::vector<H264Pps>(H264MaxPpsCount);
    H264Sps ActiveSps = {};

    std::vector<SliceRef> Slices;
    std::vector<Picture> Dpb;

    INT32 PrevPocMsb = 0;
    INT32 PrevPocLsb = 0;
    UINT32 PrevFrameNum = 0;
    UINT32 PrevFrameNumOffset = 0;
    UINT32 PrevRefFrameNum = 0;
    UINT32 FrameNumOffset = 0;
    BOOLEAN PrevHadMmco5 = FALSE;
    INT32 MaxLongTermFrameIdx = -1;
    BOOLEAN HasSeenIdr = FALSE;
    BOOLEAN WarnedGap = FALSE;
};
