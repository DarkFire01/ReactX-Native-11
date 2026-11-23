/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXIL programs lowered to the tokenized program the executor runs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* The DXIL program part starts with this; the bitcode sits at BitcodeOffset from Magic */
static constexpr UINT RxWarpDxilMagic = 0x4C495844;     /* "DXIL" */

BOOLEAN
RxWarpIsDxil(
    _In_ const UINT *Code);

/* Runs once per process, beside DllMain */
BOOLEAN
RxWarpDxilAttach(VOID);

VOID
RxWarpDxilDetach(VOID);

/* A tokenized program for RxWarpShader::Initialize, or null when the program uses something not lowered */
UINT *
RxWarpLowerDxil(
    _In_ const UINT *Code);

VOID
RxWarpFreeLowered(
    _In_opt_ UINT *Tokens);
