/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The last step of a shader conversion: signatures, the input layout and the signed container
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "shaderconv.hpp"

#include <d3d12.h>

/* One vertex declaration element, laid out as the runtime hands it over */
struct NineScElement
{
    USHORT Stream;
    USHORT Offset;
    BYTE Type;
    BYTE Method;
    BYTE Usage;
    BYTE UsageIndex;
};

static_assert(sizeof(NineScElement) == 8, "elements keep the runtime's layout");

/* Number of vertex streams a declaration can read */
static constexpr UINT NineScStreamCount = 16;

/* Bytes of a signature or a container */
typedef std::vector<BYTE> NineScBlob;

/* Signs a finished container in place; the runtime may hand one over */
typedef HRESULT (APIENTRY* PFN_NINE_SC_SIGN)(
    _Inout_updates_bytes_(Size) VOID* Container,
    _In_ UINT Size);

/*
 * Builds the input signature of a vertex shader and, when it has inputs and a declaration, the input
 * layout that feeds them. Frequencies holds the stream source frequency of each stream. The layout is
 * left as it was when there is nothing to build it from.
 */
VOID
NineScBuildInputSignature(
    _In_ const NineScInputs* Inputs,
    _In_reads_opt_(ElementCount) const NineScElement* Elements,
    _In_ UINT ElementCount,
    _In_reads_(NineScStreamCount) const UINT* Frequencies,
    _Inout_ std::vector<D3D12_INPUT_ELEMENT_DESC>* Layout,
    _Out_ NineScBlob* Signature);

/* Builds the output signature from the vertex shader outputs the converter reported */
VOID
NineScBuildOutputSignature(
    _In_ const NineScOutputs* Outputs,
    _Out_ NineScBlob* Signature);

/* Builds the output signature of a pixel shader: a target for each of bits 0 to 3, depth for bit 4 */
VOID
NineScBuildPixelOutputSignature(
    _In_ UINT Written,
    _Out_ NineScBlob* Signature);

/* Packs the signatures and the tokens into a container and signs it */
HRESULT
NineScPackageShader(
    _In_reads_bytes_(CodeSize) const VOID* Code,
    _In_ SIZE_T CodeSize,
    _In_ const NineScBlob* InputSignature,
    _In_ const NineScBlob* OutputSignature,
    _In_opt_ PFN_NINE_SC_SIGN Sign,
    _Out_ NineScBlob* Container);
