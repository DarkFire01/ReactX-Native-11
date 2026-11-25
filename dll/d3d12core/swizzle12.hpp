/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CPU copies through parameterized swizzle patterns
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

/* A swizzle pattern turned into the address bits each coordinate bit flips */
struct RxSwizzle12
{
    /* Inside the tile, then in the bits the postamble rewrites after the tile offset is added */
    UINT32 TileFlips[3][32];
    UINT32 LateFlips[3][32];
    UINT32 Immediate;
    UINT TileLog2;
    UINT Shift[3];
    UINT Origin[3];
    /* Low address bits that follow the byte column unchanged, so runs of that size copy at once */
    UINT RunLog2;
    BOOLEAN HasLate;
    BOOLEAN StackSlices;
};

/* The region of one subresource a copy covers: X in bytes, Y in block rows, Z in slices */
struct RxSwizzleRegion12
{
    BYTE* Base;
    UINT RowPitch;
    UINT DepthPitch;
    UINT Start[3];
    UINT Size[3];
};

/* Row major texels, one element after another */
VOID
RxLinearSwizzlePattern12(
    _In_ UINT ElementLog2,
    _Out_ D3D12DDI_SWIZZLE_PATTERN_DESC_0022* Pattern);

/* The 64KB standard swizzle of 2D or 3D textures; empty for other element sizes */
VOID
RxStandardSwizzlePattern12(
    _In_ BOOLEAN Volume,
    _In_ UINT ElementLog2,
    _Out_ D3D12DDI_SWIZZLE_PATTERN_DESC_0022* Pattern);

/* Checks a pattern and its subresource's pre swizzle offsets; E_INVALIDARG when the CPU cannot use it */
HRESULT
RxPrepareSwizzle12(
    _In_ const D3D12DDI_SWIZZLE_PATTERN_DESC_0022& Pattern,
    _In_ UINT ElementLog2,
    _In_reads_(3) const UINT* Origin,
    _Out_ RxSwizzle12* Swizzle);

/* Moves the region between the swizzled subresource and linear memory with the given pitches */
VOID
RxSwizzleCopy12(
    _In_ const RxSwizzle12& Swizzle,
    _In_ const RxSwizzleRegion12& Region,
    _Inout_ BYTE* Linear,
    _In_ UINT LinearPitch,
    _In_ UINT LinearDepthPitch,
    _In_ BOOLEAN ToSwizzled);
