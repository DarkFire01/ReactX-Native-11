/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AV1 reference management on top of the D3D11 video decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "av1syntax.hpp"
#include "videodecoder.hpp"

class Av1Decoder final : public VideoDecoder
{
public:
    explicit Av1Decoder(
        _In_ PlaybackStats& Stats)
        : VideoDecoder(Stats)
    {
    }

    HRESULT
    DecodeAccessUnit(
        _In_ const AccessUnit& Unit,
        _Inout_ std::vector<DecodedFrame>& Ready) override;

    /* AV1 shows frames in decode order, so nothing ever waits */
    VOID
    Drain(
        _Inout_ std::vector<DecodedFrame>& Ready) override
    {
    }

    VOID
    Reset() override;

    VideoFormatInfo
    GetFormat() const override;

    std::wstring
    Describe() const override;

    UINT
    GetDpbFill() const override;

    UINT
    GetDpbSize() const override
    {
        return Av1NumRefFrames;
    }

private:
    struct Slot
    {
        Av1RefSlot State;
        INT SurfaceIndex = -1;
    };

    struct TileData
    {
        const UINT8* Data;
        SIZE_T Size;
        UINT16 Row;
        UINT16 Column;
    };

    HRESULT
    Configure();

    HRESULT
    BeginPicture(
        _In_ const Av1Obu& Obu,
        _In_ const AccessUnit& Unit,
        _Inout_ std::vector<DecodedFrame>& Ready);

    HRESULT
    AddTileGroup(
        _In_ const Av1Obu& Obu,
        _In_ SIZE_T Start,
        _Inout_ std::vector<DecodedFrame>& Ready);

    HRESULT
    DecodePicture(
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    ShowExisting(
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    FillPictureParameters(
        _In_ UINT SurfaceIndex,
        _Out_ struct _DXVA_PicParams_AV1* Params);

    VOID
    Output(
        _In_ UINT SurfaceIndex,
        _In_ const Av1RefSlot& State,
        _In_ CHAR Type,
        _Inout_ std::vector<DecodedFrame>& Ready);

    VOID
    StoreReference(
        _In_ UINT SlotIndex,
        _In_ const Av1RefSlot& State,
        _In_ INT SurfaceIndex);

    Av1SequenceHeader Sequence = {};
    Av1FrameHeader Header = {};
    Slot Slots[Av1NumRefFrames];
    std::vector<TileData> Tiles;
    BOOLEAN HasFrameHeader = FALSE;
    BOOLEAN NeedsKeyFrame = TRUE;
    INT64 PresentationTime = NvdNoTimestamp;
    RECT LastVisible = {};
    UINT SurfaceWidth = 0;
    UINT SurfaceHeight = 0;
    DXGI_FORMAT Format = DXGI_FORMAT_NV12;
};
