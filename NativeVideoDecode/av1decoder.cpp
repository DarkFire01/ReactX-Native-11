/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AV1 reference management on top of the D3D11 video decoder
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "av1decoder.hpp"

#include <dxva.h>

#include <algorithm>

/* Generous surface alignment; some drivers address AV1 surfaces in 128 pixel units */
constexpr UINT Av1SurfaceAlignment = 128;

static const CHAR FrameLetters[4] = { 'K', 'P', 'I', 'S' };

static
BOOLEAN
NvdAcceptAnyConfig(
    _In_ const D3D11_VIDEO_DECODER_CONFIG& Candidate)
{
    return TRUE;
}

static
UINT
NvdAlign(
    _In_ UINT Value,
    _In_ UINT Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

HRESULT
Av1Decoder::Configure()
{
    const GUID* Profile;
    PCWSTR ProfileName;
    HRESULT hr;

    if (Sequence.Profile != 0 || Sequence.MonoChrome || !Sequence.SubsamplingX || !Sequence.SubsamplingY)
    {
        wprintf(L"error: only AV1 Main profile 4:2:0 is supported (profile %u)\n", Sequence.Profile);
        return E_NOTIMPL;
    }

    if (Sequence.BitDepth == 8)
    {
        Format = DXGI_FORMAT_NV12;
        Profile = &D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0;
        ProfileName = L"AV1 Main 8 bit";
    }
    else if (Sequence.BitDepth == 10)
    {
        Format = DXGI_FORMAT_P010;
        Profile = &D3D11_DECODER_PROFILE_AV1_VLD_PROFILE0;
        ProfileName = L"AV1 Main 10 bit";
    }
    else
    {
        wprintf(L"error: %u bit AV1 is not supported\n", Sequence.BitDepth);
        return E_NOTIMPL;
    }

    SurfaceWidth = NvdAlign(Sequence.MaxFrameWidth, Av1SurfaceAlignment);
    SurfaceHeight = NvdAlign(Sequence.MaxFrameHeight, Av1SurfaceAlignment);

    /* Eight reference slots, the picture being decoded, and one spare */
    hr = CreateDecoder(*Profile, ProfileName, SurfaceWidth, SurfaceHeight, Format,
                       Av1NumRefFrames + 2, NvdAcceptAnyConfig);
    if (FAILED(hr))
        return hr;

    LastVisible = { 0, 0, (LONG)Sequence.MaxFrameWidth, (LONG)Sequence.MaxFrameHeight };
    return S_OK;
}

VOID
Av1Decoder::StoreReference(
    _In_ UINT SlotIndex,
    _In_ const Av1RefSlot& State,
    _In_ INT SurfaceIndex)
{
    Slot& Target = Slots[SlotIndex];

    /* Take the new reference first: the slot may already hold this very surface */
    if (SurfaceIndex >= 0)
        SurfaceRefs[SurfaceIndex]++;
    if (Target.SurfaceIndex >= 0)
        SurfaceRefs[Target.SurfaceIndex]--;

    Target.State = State;
    Target.SurfaceIndex = SurfaceIndex;
}

VOID
Av1Decoder::Output(
    _In_ UINT SurfaceIndex,
    _In_ const Av1RefSlot& State,
    _In_ CHAR Type,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    DecodedFrame Frame;

    /* Render size is what the stream wants shown; it never exceeds the decoded picture */
    LastVisible.left = 0;
    LastVisible.top = 0;
    LastVisible.right = (LONG)std::min(State.UpscaledWidth, State.RenderWidth ? State.RenderWidth
                                                                              : State.UpscaledWidth);
    LastVisible.bottom = (LONG)std::min(State.FrameHeight, State.RenderHeight ? State.RenderHeight
                                                                              : State.FrameHeight);

    Frame.SurfaceIndex = SurfaceIndex;
    Frame.PresentationTime = PresentationTime;
    Frame.DisplayIndex = DisplayCount++;
    Frame.Visible = LastVisible;
    Frame.Type = Type;
    SurfaceRefs[SurfaceIndex]++;
    Ready.push_back(Frame);

    /* A temporal unit carries one shown frame; anything after it has no timestamp of its own */
    PresentationTime = NvdNoTimestamp;
}

VOID
Av1Decoder::ShowExisting(
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    Slot Shown = Slots[Header.FrameToShowMapIdx];
    UINT Index;

    if (Shown.SurfaceIndex < 0)
        return;

    Output((UINT)Shown.SurfaceIndex, Shown.State, FrameLetters[Shown.State.FrameType], Ready);

    /* 7.21: showing a key frame loads its state and refreshes every slot with it */
    if (Shown.State.FrameType == Av1KeyFrame)
    {
        for (Index = 0; Index < Av1NumRefFrames; Index++)
            StoreReference(Index, Shown.State, Shown.SurfaceIndex);
        NeedsKeyFrame = FALSE;
    }
}

VOID
Av1Decoder::FillPictureParameters(
    _In_ UINT SurfaceIndex,
    _Out_ DXVA_PicParams_AV1* Params)
{
    const Av1FilmGrain& Grain = Header.FilmGrain;
    UINT SbShift = Sequence.Use128x128Superblock ? 5 : 4;
    UINT Index;
    UINT Feature;
    INT Ref;

    memset(Params, 0, sizeof(*Params));

    Params->width = Header.UpscaledWidth;
    Params->height = Header.FrameHeight;
    Params->max_width = Sequence.MaxFrameWidth;
    Params->max_height = Sequence.MaxFrameHeight;
    Params->CurrPicTextureIndex = (UCHAR)SurfaceIndex;
    Params->superres_denom = Header.UseSuperres ? Header.SuperresDenom : 8;
    Params->bitdepth = Sequence.BitDepth;
    Params->seq_profile = Sequence.Profile;

    /* Tile sizes go in superblocks; the last column and row may be partial */
    Params->tiles.cols = (UCHAR)Header.TileCols;
    Params->tiles.rows = (UCHAR)Header.TileRows;
    Params->tiles.context_update_id = (USHORT)Header.ContextUpdateTileId;
    for (Index = 0; Index < Header.TileCols; Index++)
    {
        Params->tiles.widths[Index] =
            (USHORT)((Header.MiColStarts[Index + 1] - Header.MiColStarts[Index] + (1u << SbShift) - 1) >> SbShift);
    }
    for (Index = 0; Index < Header.TileRows; Index++)
    {
        Params->tiles.heights[Index] =
            (USHORT)((Header.MiRowStarts[Index + 1] - Header.MiRowStarts[Index] + (1u << SbShift) - 1) >> SbShift);
    }

    Params->coding.use_128x128_superblock = Sequence.Use128x128Superblock;
    Params->coding.intra_edge_filter = Sequence.EnableIntraEdgeFilter;
    Params->coding.interintra_compound = Sequence.EnableInterintraCompound;
    Params->coding.masked_compound = Sequence.EnableMaskedCompound;
    Params->coding.warped_motion = Header.AllowWarpedMotion;
    Params->coding.dual_filter = Sequence.EnableDualFilter;
    Params->coding.jnt_comp = Sequence.EnableJntComp;
    Params->coding.screen_content_tools = Header.AllowScreenContentTools;
    Params->coding.integer_mv = Header.ForceIntegerMv;
    Params->coding.cdef = Sequence.EnableCdef;
    Params->coding.restoration = Sequence.EnableRestoration;
    Params->coding.film_grain = Sequence.FilmGrainParamsPresent;
    Params->coding.intrabc = Header.AllowIntrabc;
    Params->coding.high_precision_mv = Header.AllowHighPrecisionMv;
    Params->coding.switchable_motion_mode = Header.IsMotionModeSwitchable;
    Params->coding.filter_intra = Sequence.EnableFilterIntra;
    Params->coding.disable_frame_end_update_cdf = Header.DisableFrameEndUpdateCdf;
    Params->coding.disable_cdf_update = Header.DisableCdfUpdate;
    Params->coding.reference_mode = Header.ReferenceSelect;
    Params->coding.skip_mode = Header.SkipModePresent;
    Params->coding.reduced_tx_set = Header.ReducedTxSet;
    Params->coding.superres = Header.UseSuperres;
    Params->coding.tx_mode = Header.TxMode;
    Params->coding.use_ref_frame_mvs = Header.UseRefFrameMvs;
    Params->coding.enable_ref_frame_mvs = Sequence.EnableRefFrameMvs;
    Params->coding.reference_frame_update = 1;

    Params->format.frame_type = Header.FrameType;
    Params->format.show_frame = Header.ShowFrame;
    Params->format.showable_frame = Header.ShowableFrame;
    Params->format.subsampling_x = Sequence.SubsamplingX;
    Params->format.subsampling_y = Sequence.SubsamplingY;
    Params->format.mono_chrome = Sequence.MonoChrome;

    Params->primary_ref_frame = Header.PrimaryRefFrame;
    Params->order_hint = (UCHAR)Header.OrderHint;
    Params->order_hint_bits = Sequence.OrderHintBits;

    /* frame_refs name slots, while RefFrameMapTextureIndex maps slots to surfaces */
    for (Index = 0; Index < Av1RefsPerFrame; Index++)
    {
        Params->frame_refs[Index].Index = 0xFF;
        if (Header.FrameIsIntra)
            continue;

        Ref = Header.RefFrameIdx[Index];
        Params->frame_refs[Index].width = Slots[Ref].State.UpscaledWidth;
        Params->frame_refs[Index].height = Slots[Ref].State.FrameHeight;
        memcpy(Params->frame_refs[Index].wmmat, Header.GmParams[Index + 1], sizeof(Params->frame_refs[Index].wmmat));
        Params->frame_refs[Index].wminvalid = Header.GmInvalid[Index + 1];
        Params->frame_refs[Index].wmtype = Header.GmType[Index + 1];
        Params->frame_refs[Index].Index = (UCHAR)Ref;
    }

    for (Index = 0; Index < Av1NumRefFrames; Index++)
        Params->RefFrameMapTextureIndex[Index] = (Slots[Index].SurfaceIndex >= 0) ? (UCHAR)Slots[Index].SurfaceIndex : 0xFF;

    Params->loop_filter.filter_level[0] = Header.LoopFilterLevel[0];
    Params->loop_filter.filter_level[1] = Header.LoopFilterLevel[1];
    Params->loop_filter.filter_level_u = Header.LoopFilterLevel[2];
    Params->loop_filter.filter_level_v = Header.LoopFilterLevel[3];
    Params->loop_filter.sharpness_level = Header.LoopFilterSharpness;
    Params->loop_filter.mode_ref_delta_enabled = Header.LoopFilterDeltaEnabled;
    Params->loop_filter.mode_ref_delta_update = Header.LoopFilterDeltaUpdate;
    Params->loop_filter.delta_lf_multi = Header.DeltaLfMulti;
    Params->loop_filter.delta_lf_present = Header.DeltaLfPresent;
    memcpy(Params->loop_filter.ref_deltas, Header.LoopFilterRefDeltas, sizeof(Params->loop_filter.ref_deltas));
    memcpy(Params->loop_filter.mode_deltas, Header.LoopFilterModeDeltas, sizeof(Params->loop_filter.mode_deltas));
    Params->loop_filter.delta_lf_res = Header.DeltaLfRes;
    for (Index = 0; Index < 3; Index++)
    {
        Params->loop_filter.frame_restoration_type[Index] = Header.FrameRestorationType[Index];
        Params->loop_filter.log2_restoration_unit_size[Index] = Header.LoopRestorationSizeLog2[Index];
    }

    Params->quantization.delta_q_present = Header.DeltaQPresent;
    Params->quantization.delta_q_res = Header.DeltaQRes;
    Params->quantization.base_qindex = Header.BaseQIdx;
    Params->quantization.y_dc_delta_q = Header.DeltaQYDc;
    Params->quantization.u_dc_delta_q = Header.DeltaQUDc;
    Params->quantization.v_dc_delta_q = Header.DeltaQVDc;
    Params->quantization.u_ac_delta_q = Header.DeltaQUAc;
    Params->quantization.v_ac_delta_q = Header.DeltaQVAc;
    Params->quantization.qm_y = Header.UsingQmatrix ? Header.QmY : 0xFF;
    Params->quantization.qm_u = Header.UsingQmatrix ? Header.QmU : 0xFF;
    Params->quantization.qm_v = Header.UsingQmatrix ? Header.QmV : 0xFF;

    Params->cdef.damping = Header.CdefDampingMinus3;
    Params->cdef.bits = Header.CdefBits;
    for (Index = 0; Index < 8; Index++)
    {
        Params->cdef.y_strengths[Index].primary = Header.CdefYPriStrength[Index];
        Params->cdef.y_strengths[Index].secondary = Header.CdefYSecStrength[Index];
        Params->cdef.uv_strengths[Index].primary = Header.CdefUvPriStrength[Index];
        Params->cdef.uv_strengths[Index].secondary = Header.CdefUvSecStrength[Index];
    }

    Params->interp_filter = Header.InterpolationFilter;

    Params->segmentation.enabled = Header.SegmentationEnabled;
    Params->segmentation.update_map = Header.SegmentationUpdateMap;
    Params->segmentation.update_data = Header.SegmentationUpdateData;
    Params->segmentation.temporal_update = Header.SegmentationTemporalUpdate;
    for (Index = 0; Index < Av1MaxSegments; Index++)
    {
        for (Feature = 0; Feature < Av1SegLevels; Feature++)
        {
            if (Header.FeatureEnabled[Index][Feature])
                Params->segmentation.feature_mask[Index].mask |= (UCHAR)(1u << Feature);
            Params->segmentation.feature_data[Index][Feature] = Header.FeatureData[Index][Feature];
        }
    }

    if (Sequence.FilmGrainParamsPresent && Grain.ApplyGrain)
    {
        Params->film_grain.apply_grain = 1;
        Params->film_grain.scaling_shift_minus8 = Grain.GrainScalingMinus8;
        Params->film_grain.chroma_scaling_from_luma = Grain.ChromaScalingFromLuma;
        Params->film_grain.ar_coeff_lag = Grain.ArCoeffLag;
        Params->film_grain.ar_coeff_shift_minus6 = Grain.ArCoeffShiftMinus6;
        Params->film_grain.grain_scale_shift = Grain.GrainScaleShift;
        Params->film_grain.overlap_flag = Grain.OverlapFlag;
        Params->film_grain.clip_to_restricted_range = Grain.ClipToRestrictedRange;
        Params->film_grain.matrix_coeff_is_identity = (Sequence.MatrixCoefficients == 0);
        Params->film_grain.grain_seed = Grain.GrainSeed;

        Params->film_grain.num_y_points = Grain.NumYPoints;
        for (Index = 0; Index < Grain.NumYPoints; Index++)
        {
            Params->film_grain.scaling_points_y[Index][0] = Grain.PointYValue[Index];
            Params->film_grain.scaling_points_y[Index][1] = Grain.PointYScaling[Index];
        }
        Params->film_grain.num_cb_points = Grain.NumCbPoints;
        for (Index = 0; Index < Grain.NumCbPoints; Index++)
        {
            Params->film_grain.scaling_points_cb[Index][0] = Grain.PointCbValue[Index];
            Params->film_grain.scaling_points_cb[Index][1] = Grain.PointCbScaling[Index];
        }
        Params->film_grain.num_cr_points = Grain.NumCrPoints;
        for (Index = 0; Index < Grain.NumCrPoints; Index++)
        {
            Params->film_grain.scaling_points_cr[Index][0] = Grain.PointCrValue[Index];
            Params->film_grain.scaling_points_cr[Index][1] = Grain.PointCrScaling[Index];
        }

        memcpy(Params->film_grain.ar_coeffs_y, Grain.ArCoeffsYPlus128, sizeof(Params->film_grain.ar_coeffs_y));
        memcpy(Params->film_grain.ar_coeffs_cb, Grain.ArCoeffsCbPlus128, sizeof(Params->film_grain.ar_coeffs_cb));
        memcpy(Params->film_grain.ar_coeffs_cr, Grain.ArCoeffsCrPlus128, sizeof(Params->film_grain.ar_coeffs_cr));
        Params->film_grain.cb_mult = Grain.CbMult;
        Params->film_grain.cb_luma_mult = Grain.CbLumaMult;
        Params->film_grain.cr_mult = Grain.CrMult;
        Params->film_grain.cr_luma_mult = Grain.CrLumaMult;
        Params->film_grain.cb_offset = (SHORT)Grain.CbOffset;
        Params->film_grain.cr_offset = (SHORT)Grain.CrOffset;
    }

    Params->StatusReportFeedbackNumber = ++StatusFeedback;
}

HRESULT
Av1Decoder::DecodePicture(
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    std::vector<DXVA_Tile_AV1> TileControl;
    D3D11_VIDEO_DECODER_BUFFER_DESC Buffers[3] = {};
    DXVA_PicParams_AV1 Params;
    DXVA_Tile_AV1 Entry = {};
    Av1RefSlot Saved = {};
    UINT8* Bitstream = nullptr;
    UINT Capacity = 0;
    UINT Written = 0;
    UINT Retries;
    UINT Index;
    INT Surface;
    double Begin;
    HRESULT hr;

    Surface = FindFreeSurface();
    if (Surface < 0)
    {
        wprintf(L"error: every decode surface is in use\n");
        return E_OUTOFMEMORY;
    }

    FillPictureParameters((UINT)Surface, &Params);

    Begin = NvdNowMs();
    hr = BeginFrame((UINT)Surface, &Retries);
    if (FAILED(hr))
        return hr;

    hr = CopyToDecoderBuffer(D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &Params, sizeof(Params));

    if (SUCCEEDED(hr))
    {
        hr = VideoContext->GetDecoderBuffer(Decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, &Capacity,
                                            reinterpret_cast<VOID**>(&Bitstream));
    }

    if (SUCCEEDED(hr))
    {
        /* Tile data only: OBU headers, tile group headers and tile size fields stay behind */
        for (const TileData& Tile : Tiles)
        {
            if (Written + Tile.Size > Capacity)
            {
                hr = E_OUTOFMEMORY;
                break;
            }

            Entry.DataOffset = Written;
            Entry.DataSize = (UINT)Tile.Size;
            Entry.row = Tile.Row;
            Entry.column = Tile.Column;
            Entry.anchor_frame = 0xFF;
            TileControl.push_back(Entry);

            memcpy(Bitstream + Written, Tile.Data, Tile.Size);
            Written += (UINT)Tile.Size;
        }

        VideoContext->ReleaseDecoderBuffer(Decoder.Get(), D3D11_VIDEO_DECODER_BUFFER_BITSTREAM);
    }

    if (SUCCEEDED(hr))
    {
        hr = CopyToDecoderBuffer(D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, TileControl.data(),
                                 (UINT)(TileControl.size() * sizeof(DXVA_Tile_AV1)));
    }

    if (SUCCEEDED(hr))
    {
        Buffers[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
        Buffers[0].DataSize = sizeof(Params);
        Buffers[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
        Buffers[1].DataSize = (UINT)(TileControl.size() * sizeof(DXVA_Tile_AV1));
        Buffers[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
        Buffers[2].DataSize = Written;
        hr = VideoContext->SubmitDecoderBuffers(Decoder.Get(), 3, Buffers);
    }

    hr = EndFrame(hr, Begin);
    if (FAILED(hr))
        return hr;

    Stats.OnPictureDecoded(DecodeCount, FrameLetters[Header.FrameType], (INT32)Header.OrderHint, Written,
                           (UINT32)Tiles.size(), NvdNowMs() - Begin, Retries);
    DecodeCount++;

    /* 7.20 reference frame update: everything later frames may load from this one */
    Saved.IsValid = TRUE;
    Saved.FrameType = Header.FrameType;
    Saved.UpscaledWidth = Header.UpscaledWidth;
    Saved.FrameWidth = Header.FrameWidth;
    Saved.FrameHeight = Header.FrameHeight;
    Saved.RenderWidth = Header.RenderWidth;
    Saved.RenderHeight = Header.RenderHeight;
    Saved.OrderHint = Header.OrderHint;
    memcpy(Saved.GmParams, Header.GmParams, sizeof(Saved.GmParams));
    memcpy(Saved.LoopFilterRefDeltas, Header.LoopFilterRefDeltas, sizeof(Saved.LoopFilterRefDeltas));
    memcpy(Saved.LoopFilterModeDeltas, Header.LoopFilterModeDeltas, sizeof(Saved.LoopFilterModeDeltas));
    memcpy(Saved.FeatureEnabled, Header.FeatureEnabled, sizeof(Saved.FeatureEnabled));
    memcpy(Saved.FeatureData, Header.FeatureData, sizeof(Saved.FeatureData));
    Saved.FilmGrain = Header.FilmGrain;

    /* The picture holds itself while the slots and the output take their own counts */
    SurfaceRefs[Surface]++;
    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        if (Header.RefreshFrameFlags & (1u << Index))
            StoreReference(Index, Saved, Surface);
    }

    if (Header.ShowFrame)
        Output((UINT)Surface, Saved, FrameLetters[Header.FrameType], Ready);
    SurfaceRefs[Surface]--;

    return S_OK;
}

HRESULT
Av1Decoder::BeginPicture(
    _In_ const Av1Obu& Obu,
    _In_ const AccessUnit& Unit,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    Av1RefSlot Refs[Av1NumRefFrames];
    UINT Index;
    HRESULT hr;

    if (!Sequence.IsValid)
        return S_FALSE;

    if (!IsConfigured())
    {
        hr = Configure();
        if (FAILED(hr))
            return hr;
    }

    for (Index = 0; Index < Av1NumRefFrames; Index++)
        Refs[Index] = Slots[Index].State;

    if (!Av1ParseFrameHeader(Obu.Payload, Obu.Size, Sequence, Refs, Obu.TemporalId, Obu.SpatialId, &Header))
    {
        wprintf(L"warning: AV1 frame header could not be parsed, skipped\n");
        HasFrameHeader = FALSE;
        return S_FALSE;
    }

    if (Header.ShowExistingFrame)
    {
        if (!NeedsKeyFrame || Slots[Header.FrameToShowMapIdx].State.FrameType == Av1KeyFrame)
            ShowExisting(Ready);
        HasFrameHeader = FALSE;
        return S_OK;
    }

    /* After a reset only a key frame can start decoding again */
    if (NeedsKeyFrame)
    {
        if (Header.FrameType != Av1KeyFrame)
        {
            HasFrameHeader = FALSE;
            return S_FALSE;
        }
        NeedsKeyFrame = FALSE;
    }

    if (Header.UpscaledWidth > SurfaceWidth || Header.FrameHeight > SurfaceHeight)
    {
        wprintf(L"error: frame %ux%u exceeds the sequence maximum\n", Header.UpscaledWidth, Header.FrameHeight);
        return E_FAIL;
    }

    if (PresentationTime == NvdNoTimestamp)
        PresentationTime = Unit.PresentationTime;

    Tiles.clear();
    HasFrameHeader = TRUE;
    return S_OK;
}

HRESULT
Av1Decoder::AddTileGroup(
    _In_ const Av1Obu& Obu,
    _In_ SIZE_T Start,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    std::vector<Av1Tile> Group;
    BOOLEAN IsLast;

    if (!HasFrameHeader)
        return S_FALSE;

    if (!Av1ParseTileGroup(Obu.Payload, Obu.Size, Start, Header, Group, &IsLast))
    {
        wprintf(L"warning: AV1 tile group could not be parsed, frame dropped\n");
        HasFrameHeader = FALSE;
        return S_FALSE;
    }

    for (const Av1Tile& Tile : Group)
        Tiles.push_back({ Obu.Payload + Tile.Offset, Tile.Size, Tile.Row, Tile.Column });

    if (!IsLast)
        return S_OK;

    HasFrameHeader = FALSE;
    return DecodePicture(Ready);
}

HRESULT
Av1Decoder::DecodeAccessUnit(
    _In_ const AccessUnit& Unit,
    _Inout_ std::vector<DecodedFrame>& Ready)
{
    Av1SequenceHeader NewSequence;
    SIZE_T Offset = 0;
    Av1Obu Obu;
    HRESULT hr = S_OK;

    PresentationTime = Unit.PresentationTime;

    while (SUCCEEDED(hr) && Av1ReadObu(Unit.Data.data(), Unit.Data.size(), &Offset, &Obu))
    {
        switch (Obu.Type)
        {
            case Av1ObuSequenceHeader:
                if (!Av1ParseSequenceHeader(Obu.Payload, Obu.Size, &NewSequence))
                {
                    wprintf(L"warning: AV1 sequence header could not be parsed\n");
                    break;
                }

                if (IsConfigured() &&
                    (NewSequence.MaxFrameWidth > SurfaceWidth || NewSequence.MaxFrameHeight > SurfaceHeight ||
                     NewSequence.BitDepth != Sequence.BitDepth))
                {
                    wprintf(L"error: AV1 sequence changes size or bit depth mid stream, not supported\n");
                    return E_NOTIMPL;
                }
                Sequence = NewSequence;
                break;

            case Av1ObuTemporalDelimiter:
                HasFrameHeader = FALSE;
                break;

            case Av1ObuFrameHeader:
                hr = BeginPicture(Obu, Unit, Ready);
                break;

            case Av1ObuFrame:
                hr = BeginPicture(Obu, Unit, Ready);
                if (hr == S_OK && HasFrameHeader)
                    hr = AddTileGroup(Obu, Header.HeaderBytes, Ready);
                break;

            case Av1ObuTileGroup:
                hr = AddTileGroup(Obu, 0, Ready);
                break;

            default:
                break;
        }
    }

    return FAILED(hr) ? hr : S_OK;
}

VOID
Av1Decoder::Reset()
{
    UINT Index;

    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        if (Slots[Index].SurfaceIndex >= 0)
            SurfaceRefs[Slots[Index].SurfaceIndex]--;
        Slots[Index] = Slot();
    }

    Tiles.clear();
    HasFrameHeader = FALSE;
    NeedsKeyFrame = TRUE;
}

UINT
Av1Decoder::GetDpbFill() const
{
    UINT Count = 0;
    UINT Index;

    for (Index = 0; Index < Av1NumRefFrames; Index++)
    {
        if (Slots[Index].SurfaceIndex >= 0)
            Count++;
    }

    return Count;
}

VideoFormatInfo
Av1Decoder::GetFormat() const
{
    VideoFormatInfo Info = {};

    Info.SurfaceWidth = SurfaceWidth;
    Info.SurfaceHeight = SurfaceHeight;
    Info.Format = Format;
    Info.BitDepth = Sequence.BitDepth;
    Info.Visible = LastVisible;
    Info.SarWidth = 1;
    Info.SarHeight = 1;
    Info.IsFullRange = Sequence.ColorRange;
    Info.HasColourDescription = Sequence.ColorDescriptionPresent;
    Info.MatrixCoefficients = Sequence.MatrixCoefficients;
    if (Sequence.TimingInfoPresent && Sequence.NumUnitsInDisplayTick != 0)
        Info.FrameRate = (double)Sequence.TimeScale / Sequence.NumUnitsInDisplayTick;
    return Info;
}

std::wstring
Av1Decoder::Describe() const
{
    std::wstring Text;
    WCHAR Line[256];
    UINT Level = Sequence.SeqLevelIdx[0];

    swprintf_s(Line, L"codec          AV1 profile %u, level %u.%u%s, %u bit\n",
               Sequence.Profile, 2 + (Level >> 2), Level & 3, Sequence.SeqTier[0] ? L" high tier" : L"",
               Sequence.BitDepth);
    Text += Line;
    swprintf_s(Line, L"max size       %ux%u, %s superblocks%s\n",
               Sequence.MaxFrameWidth, Sequence.MaxFrameHeight,
               Sequence.Use128x128Superblock ? L"128x128" : L"64x64",
               Sequence.StillPicture ? L", still picture" : L"");
    Text += Line;
    swprintf_s(Line, L"tools          order hint %u bits, cdef %u, restoration %u, superres %u, film grain %u\n",
               Sequence.OrderHintBits, Sequence.EnableCdef, Sequence.EnableRestoration, Sequence.EnableSuperres,
               Sequence.FilmGrainParamsPresent);
    Text += Line;
    if (Sequence.ColorDescriptionPresent)
    {
        swprintf_s(Line, L"colour         primaries %u, transfer %u, matrix %u, %s range\n",
                   Sequence.ColorPrimaries, Sequence.TransferCharacteristics, Sequence.MatrixCoefficients,
                   Sequence.ColorRange ? L"full" : L"limited");
        Text += Line;
    }
    swprintf_s(Line, L"decoder        AV1_VLD_PROFILE0, %s, %ux%u, %u surfaces\n",
               (Format == DXGI_FORMAT_P010) ? L"P010" : L"NV12", SurfaceWidth, SurfaceHeight, GetSurfaceCount());
    Text += Line;
    swprintf_s(Line, L"config         #%u: bitstream raw %u, decoder specific 0x%X\n",
               ConfigIndex, Config.ConfigBitstreamRaw, Config.ConfigDecoderSpecific);
    Text += Line;
    return Text;
}
