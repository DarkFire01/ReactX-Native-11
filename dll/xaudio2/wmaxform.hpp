/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     xWMA subframe transform interface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

/* Smallest and largest subframe sizes the transform accepts */
#define XA_WMA_XFORM_MIN_SIZE   16
#define XA_WMA_XFORM_MAX_SIZE   16384

struct XaWmaTransform;

/**
 * @brief Builds the tables for every power of two subframe size from MinSize to MaxSize.
 */
HRESULT
XaWmaCreateTransform(
    _In_ UINT32 MinSize,
    _In_ UINT32 MaxSize,
    _Outptr_ XaWmaTransform** Transform);

VOID
XaWmaFreeTransform(
    _In_opt_ XaWmaTransform* Transform);

/**
 * @brief Runs the scaled DCT-IV in place on one subframe of Size coefficients.
 *
 * Size is a power of two of at least 16. Sizes the transform was not created for come out
 * as zeros run through the post rotation.
 */
VOID
XaWmaInverseTransform(
    _In_ XaWmaTransform* Transform,
    _Inout_updates_(Size) float* Coefs,
    _In_ UINT32 Size,
    _In_ float Scale);
