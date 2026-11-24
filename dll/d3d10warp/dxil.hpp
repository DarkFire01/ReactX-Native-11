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
 * wave dest, value, lane, kind. The kind immediate holds the operation, its variant at bit 8,
 * the value type at bit 16 and RxWarpWaveWide for 64 bit values. The lanes of a wave are one quad.
 */
static constexpr UINT RxWarpOpcodeWave = 0x400;
static constexpr UINT RxWarpWaveLanes = 4;
static constexpr UINT RxWarpWaveWide = 0x1000000;

/*
 * Descriptor heap prefix: heap index, class and slot pairs. The instruction after it runs once
 * per set of lanes with equal indices, with each class (resource class) and slot (low 16 bits)
 * bound to that heap entry.
 */
static constexpr UINT RxWarpOpcodeHeap = 0x401;

/* Float to half and back, rounding toward zero: dest, value */
static constexpr UINT RxWarpOpcodeHalfTowardZero = 0x402;

/*
 * 64 bit operation: dest, a, b, c, kind. Wide sources read as a low and high pair from
 * their first two components; wide results go to x and z low, y and w high.
 */
static constexpr UINT RxWarpOpcodeLong = 0x403;

/*
 * 64 bit store to a computed local array element: low cell, high cell, value. When the lanes
 * name different elements, the reference stores lanes 2 and 3's values for lanes 0 and 1 and
 * nothing for lanes 2 and 3.
 */
static constexpr UINT RxWarpOpcodePairScatter = 0x404;

typedef enum _RX_WARP_LONG_KIND
{
    RxWarpLongAdd,
    RxWarpLongSubtract,
    RxWarpLongMultiply,
    RxWarpLongDivide,
    RxWarpLongRemainder,
    RxWarpLongSignedDivide,
    RxWarpLongSignedRemainder,
    RxWarpLongShiftLeft,
    RxWarpLongShiftRight,
    RxWarpLongSignedShiftRight,
    RxWarpLongMax,
    RxWarpLongMin,
    RxWarpLongSignedMax,
    RxWarpLongSignedMin,
    RxWarpLongZeroExtend,       /* from 32 bits */
    RxWarpLongSignExtend,
    RxWarpLongToFloat,          /* unsigned, then signed, to float and to double */
    RxWarpLongSignedToFloat,
    RxWarpLongToDouble,
    RxWarpLongSignedToDouble,
    RxWarpLongFromFloat,        /* float and double to unsigned, then signed */
    RxWarpLongSignedFromFloat,
    RxWarpLongFromDouble,
    RxWarpLongSignedFromDouble,
    RxWarpLongSaturateDouble,
    RxWarpLongFusedDouble,      /* a * b + c, rounded once */
    RxWarpLongFirstNarrow,      /* the kinds from here give 32 bit results */
    RxWarpLongEqual = RxWarpLongFirstNarrow,
    RxWarpLongNotEqual,
    RxWarpLongLess,
    RxWarpLongGreaterEqual,
    RxWarpLongSignedLess,
    RxWarpLongSignedGreaterEqual,
    RxWarpLongCountBits,
    RxWarpLongFirstHigh,
    RxWarpLongFirstLow,
    RxWarpLongSignedFirstHigh,
} RX_WARP_LONG_KIND;

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
    RxWarpWaveMatch,
    RxWarpWaveMultiPrefix,      /* lane operand: the partition mask; variant: sum, and, or, xor, product */
    RxWarpWaveMultiCount,
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
