/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXGI format layouts, capabilities and color encoding
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <dxgiformat.h>

typedef enum _RX_WARP_CHANNEL_TYPE
{
    RxChannelNone,
    RxChannelUnorm,
    RxChannelSnorm,
    RxChannelUint,
    RxChannelSint,
    RxChannelFloat,
    RxChannelSrgb,
    RxChannelTypeless,
    RxChannelUnused
} RX_WARP_CHANNEL_TYPE;

typedef enum _RX_WARP_FORMAT_KIND
{
    RxKindPlain,        /* bit fields, least significant first */
    RxKindR11G11B10,
    RxKindR9G9B9E5,
    RxKindBlock,        /* block compressed, 4x4 texels per block */
    RxKindPacked422,    /* two texels per element, like R8G8_B8G8 */
    RxKindPlanar,       /* video formats, not encodable */
    RxKindBuffer        /* DXGI_FORMAT_UNKNOWN: raw and structured buffers */
} RX_WARP_FORMAT_KIND;

typedef struct _RX_WARP_CHANNEL
{
    UINT8 Bits;
    UINT8 Type;         /* RX_WARP_CHANNEL_TYPE */
    UINT8 Source;       /* 0 R, 1 G, 2 B, 3 A, 4 depth, 5 stencil */
} RX_WARP_CHANNEL;

typedef struct _RX_WARP_FORMAT_INFO
{
    UINT8 Kind;         /* RX_WARP_FORMAT_KIND */
    UINT8 Bytes;        /* per element, or per block */
    UINT8 ChannelCount;
    RX_WARP_CHANNEL Channels[4];
} RX_WARP_FORMAT_INFO;

/* Builds the layout table; called once when the DLL loads */
VOID
RxWarpInitFormats(VOID);

const RX_WARP_FORMAT_INFO *
RxWarpFormatInfo(
    _In_ DXGI_FORMAT Format);

/* Bytes per row of Width texels, before the driver's row alignment */
UINT
RxWarpFormatRowBytes(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width);

/* Rows of storage for Height texels: block formats store one row per four texels */
UINT
RxWarpFormatRows(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Height);

UINT
RxWarpFormatBlockWidth(
    _In_ DXGI_FORMAT Format);

/* D3D10_DDI_FORMAT_SUPPORT bits for the device's interface version */
UINT
RxWarpFormatSupport(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Interface);

/* The same for a Direct3D 12 device */
UINT
RxWarpFormatSupport12(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Interface);

UINT
RxWarpMultisampleQuality(
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ UINT Flags);

/*
 * Encodes an RGBA color (or depth and stencil) into one element of Format.
 * Returns FALSE for formats that cannot be written this way.
 */
BOOLEAN
RxWarpEncodeColor(
    _In_ DXGI_FORMAT Format,
    _In_reads_(4) const FLOAT *Color,
    _Out_writes_bytes_(16) BYTE *Element);

BOOLEAN
RxWarpEncodeUint(
    _In_ DXGI_FORMAT Format,
    _In_reads_(4) const UINT *Values,
    _Out_writes_bytes_(16) BYTE *Element);

/* One element to its four components as raw bits: floats for float and normalized formats, integers otherwise */
BOOLEAN
RxWarpDecodeTexel(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(16) const BYTE *Element,
    _Out_writes_(4) UINT *Value);

UINT
RxWarpLinearToSrgbByte(
    _In_ FLOAT Value);

BOOLEAN
RxWarpFormatIsInteger(
    _In_ DXGI_FORMAT Format);

FLOAT
RxWarpHalfToFloat(
    _In_ UINT Half);

/* Normalized integers of the given width to float, bit for bit with the reference */
FLOAT
RxWarpUnormToFloat(
    _In_ UINT Value,
    _In_ UINT Bits);

FLOAT
RxWarpSnormToFloat(
    _In_ INT Value,
    _In_ UINT Bits);

/* Eight bit sRGB to linear, by the reference table */
FLOAT
RxWarpSrgbByteToLinear(
    _In_ UINT Byte);

UINT
RxWarpFloatToHalf(
    _In_ FLOAT Value);

/* One texel of a 4x4 block at (X, Y) within the texture, as raw component bits */
BOOLEAN
RxWarpDecodeBlockTexel(
    _In_ DXGI_FORMAT Format,
    _In_reads_bytes_(16) const BYTE *Block,
    _In_ UINT X,
    _In_ UINT Y,
    _Out_writes_(4) UINT *Value);
