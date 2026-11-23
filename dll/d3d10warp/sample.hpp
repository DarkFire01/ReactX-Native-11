/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Texture sampling, texel loads and resource queries for shaders
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

typedef enum _RX_WARP_SAMPLE_MODE
{
    RxWarpSampleAuto,       /* level from the derivatives */
    RxWarpSampleLevel       /* level given by the shader */
} RX_WARP_SAMPLE_MODE;

typedef struct _RX_WARP_SAMPLE_REQUEST
{
    RxWarpView *View;
    const RX_WARP_SAMPLER *Sampler;
    UINT Dimension;         /* D3D10_SB_RESOURCE_DIMENSION */
    INT Offset[3];
    UINT Mode;
    FLOAT Bias;
    FLOAT Level;
    BOOLEAN Compare;
    FLOAT Reference;
    BOOLEAN Gather;
    UINT GatherComponent;
    UINT Sample;            /* ld_ms */
    BOOLEAN Clamped;        /* the shader gave a lowest level of detail */
    FLOAT LodClamp;
} RX_WARP_SAMPLE_REQUEST;

VOID
RxWarpSample(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(4) const FLOAT *Coordinate,
    _In_reads_opt_(3) const FLOAT *Dx,
    _In_reads_opt_(3) const FLOAT *Dy,
    _Out_writes_(4) FLOAT *Result);

/* The fixed point result of the last eight bit filter, as 8.7 per component; FALSE when the last sample used another path */
/* The float an 8.7 filtered value reads as */
FLOAT
RxWarpByteResult(
    _In_ INT Fixed);

BOOLEAN
RxWarpLastByteFilter(
    _Out_writes_(4) INT *Fixed);

VOID
RxWarpLoad(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(4) const INT *Address,
    _Out_writes_(4) UINT *Result);

/* lod: clamped level in x, unclamped in y */
VOID
RxWarpComputeLod(
    _In_ const RX_WARP_SAMPLE_REQUEST *Request,
    _In_reads_(3) const FLOAT *Dx,
    _In_reads_(3) const FLOAT *Dy,
    _Out_writes_(4) FLOAT *Result);

/* Position of a sample from the pixel center, in pixels; zero for a bad index */
VOID
RxWarpSamplePosition(
    _In_ UINT Count,
    _In_ UINT Index,
    _Out_writes_(2) FLOAT *Position);

VOID
RxWarpResourceInfo(
    _In_opt_ RxWarpView *View,
    _In_ UINT Mip,
    _In_ UINT ReturnType,
    _Out_writes_(4) UINT *Result);
