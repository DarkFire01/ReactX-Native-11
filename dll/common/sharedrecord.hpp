/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The runtime record a shared texture carries for the process that opens it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d11.h>

/*
 * What a D3D11 runtime records with a shared texture so another process can open it,
 * laid out as Windows writes it for a single sample swapchain buffer. Direct3D 12
 * swapchain buffers carry the same record so a D3D11 compositor can open them.
 */
struct RxSharedTextureRecord
{
    UINT Size;
    UINT Version;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT Constants[9];          /* The first is the array size; the rest are fixed for swapchain buffers */
    D3D11_RESOURCE_DIMENSION Dimension;
    D3D11_TEXTURE2D_DESC Desc;
};

static_assert(sizeof(RxSharedTextureRecord) == 0x68);

/* The only record version this runtime writes and reads */
constexpr UINT RxSharedRecordVersion = 4;

/* Record constants: the array size, the keyed mutex with its fence, then NT handle and displayable flags */
constexpr UINT RxSharedRecordArraySizeIndex = 0;
constexpr UINT RxSharedRecordKeyedMutexIndex = 2;
constexpr UINT RxSharedRecordKeyedMutexShareIndex = 3;
constexpr UINT RxSharedRecordSyncObjectShareIndex = 4;
constexpr UINT RxSharedRecordNtHandleIndex = 5;
constexpr UINT RxSharedRecordDisplayableIndex = 6;
constexpr UINT RxSharedRecordDisplayableAgainIndex = 8;

/* The record's constants as Windows writes them for a single sample swapchain buffer */
constexpr UINT RxSharedRecordConstants[9] = { 1, 0, 0, 0, 0, 1, 1, 0, 1 };
