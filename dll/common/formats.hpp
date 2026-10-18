/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Sizes of DXGI formats
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "rxcommon.hpp"

/* Bits in one element, or in one 4x4 block for block compressed formats; 0 when unknown */
UINT
RxFormatBits(
    _In_ DXGI_FORMAT Format);

BOOLEAN
RxIsBlockCompressed(
    _In_ DXGI_FORMAT Format);
