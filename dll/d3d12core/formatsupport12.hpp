/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Format support answers built from the format rules and the driver's bits
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

constexpr UINT RxFormatRuleCount12 = 192;

/* Capabilities a format rule row describes, two bits each */
enum RxFormatCap12
{
    RxCapBuffer = 0,
    RxCapVertexBuffer,
    RxCapIndexBuffer,
    RxCapStreamOutput,
    RxCapTexture1D,
    RxCapTexture2D,
    RxCapTexture3D,
    RxCapTextureCube,
    RxCapShaderLoad,
    RxCapSample,
    RxCapSampleCompare,
    RxCapSampleMono,
    RxCapGather,
    RxCapGatherCompare,
    RxCapMips,
    RxCapRenderTarget,
    RxCapBlend,
    RxCapLogicOp,
    RxCapDepthStencil,
    RxCapMultisampleTarget,
    RxCapMultisampleLoad,
    RxCapMultisampleResolve,
    RxCapScanOut,
    RxCapCastInLayout,
    RxCapTypedUav,
    RxCapDecoderOutput,
    RxCapProcessorInput,
    RxCapProcessorOutput,
    RxCapCapture,
    RxCapEncoder
};

enum RxFormatRule12
{
    RxRuleNone = 0,
    RxRuleRequired = 1,
    RxRuleDriver = 2,
    RxRuleOpen = 3
};

extern const UINT64 RxFormatRules11_0[RxFormatRuleCount12];
extern const UINT64 RxFormatRules11_1[RxFormatRuleCount12];

/* Device facts the answers depend on */
struct RxFormatContext12
{
    D3D_FEATURE_LEVEL Level;
    BOOLEAN Video;
    BOOLEAN LogicOp;
    BOOLEAN Tiled;
    BOOLEAN SamplerFeedback;
    BOOLEAN TypedLoadExtra;
    BOOLEAN TypedLoadMinimal;
    BOOLEAN DisplayableDdi;
};

/* The driver's raw pfnCheckFormatSupport bits for a format */
typedef
UINT
(*RxFormatQuery12)(
    _In_ VOID* Context,
    _In_ DXGI_FORMAT Format);

/* FORMAT_SUPPORT1 and 2; FALSE when the format has no support at all */
BOOLEAN
RxCheckFormatSupport12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ RxFormatQuery12 Query,
    _In_ VOID* QueryContext,
    _Out_ UINT* Support1,
    _Out_ UINT* Support2);

/* Whether a format exists for the device's level */
BOOLEAN
RxFormatKnown12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format);

/* Whether tiled resources of this format and sample count can exist at all */
BOOLEAN
RxFormatTileable12(
    _In_ const RxFormatContext12& Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount);

/* What the level's rule row says about one capability of a format */
RxFormatRule12
RxFormatCapRule12(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format,
    _In_ RxFormatCap12 Cap);

/* Typed UAV loads the format promises (1), leaves to the driver (2) or lacks (0) */
RxFormatRule12
RxTypedLoadRule12(
    _In_ D3D_FEATURE_LEVEL Level,
    _In_ DXGI_FORMAT Format);
