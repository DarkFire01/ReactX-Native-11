/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXIL programs lowered to the tokenized program the executor runs
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* The DXIL program part starts with this; the bitcode sits at BitcodeOffset from Magic */
static constexpr UINT RxWarpDxilMagic = 0x4C495844;     /* "DXIL" */

/*
 * Cross lane operation lowered from DXIL, an opcode past the tokenized format's own:
 * wave dest, value, lane, kind. The kind immediate holds the operation, its variant at bit 8
 * and the value type at bit 16. The lanes of a wave are one quad.
 */
static constexpr UINT RxWarpOpcodeWave = 0x400;
static constexpr UINT RxWarpWaveLanes = 4;

typedef enum _RX_WARP_WAVE_KIND
{
    RxWarpWaveLaneIndex,
    RxWarpWaveFirstLane,
    RxWarpWaveAnyTrue,
    RxWarpWaveAllTrue,
    RxWarpWaveAllEqual,
    RxWarpWaveBallot,
    RxWarpWaveReadLane,
    RxWarpWaveReadFirst,
    RxWarpWaveReduce,           /* variant: sum, product, min, max */
    RxWarpWaveBits,             /* variant: and, or, xor */
    RxWarpWavePrefix,           /* variant: sum, product */
    RxWarpWaveCount,
    RxWarpWavePrefixCount,
    RxWarpWaveQuadRead,
    RxWarpWaveQuadSwap,         /* variant: across x, across y, diagonal */
} RX_WARP_WAVE_KIND;

typedef enum _RX_WARP_WAVE_TYPE
{
    RxWarpWaveFloat,
    RxWarpWaveSigned,
    RxWarpWaveUnsigned,
} RX_WARP_WAVE_TYPE;

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
