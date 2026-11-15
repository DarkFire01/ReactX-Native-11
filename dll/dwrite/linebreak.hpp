/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Line break opportunities, UAX #14 pair table style
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

/* Breakpoint byte, laid out like DWRITE_LINE_BREAKPOINT */
constexpr BYTE RxBreakConditionMask = 0x3;
constexpr BYTE RxBreakIsWhitespace = 0x10;
constexpr BYTE RxBreakIsSoftHyphen = 0x20;

inline
BYTE
RxBreakBefore(
    _In_ BYTE Breakpoint)
{
    return Breakpoint & RxBreakConditionMask;
}

inline
BYTE
RxBreakAfter(
    _In_ BYTE Breakpoint)
{
    return (Breakpoint >> 2) & RxBreakConditionMask;
}

inline
BOOLEAN
RxCanBreakAfter(
    _In_ BYTE Breakpoint)
{
    BYTE After = RxBreakAfter(Breakpoint);

    return After == DWRITE_BREAK_CONDITION_CAN_BREAK || After == DWRITE_BREAK_CONDITION_MUST_BREAK;
}

/* Line break classes, in the order the pair table uses */
enum RxBreakClass : BYTE
{
    RxLbAI, RxLbAK, RxLbAL, RxLbAP, RxLbAS, RxLbB2, RxLbBA, RxLbBB, RxLbBK, RxLbCB, RxLbCJ, RxLbCL, RxLbCM, RxLbCP,
    RxLbCR, RxLbEX, RxLbGL, RxLbH2, RxLbH3, RxLbHL, RxLbHY, RxLbID, RxLbIN, RxLbIS, RxLbJL, RxLbJT, RxLbJV, RxLbLF,
    RxLbNL, RxLbNS, RxLbNU, RxLbOP, RxLbPO, RxLbPR, RxLbQU, RxLbRI, RxLbSA, RxLbSG, RxLbSP, RxLbSY, RxLbVF, RxLbVI,
    RxLbWJ, RxLbXX, RxLbZW,
    /* The second half of a surrogate pair, or an absorbed variation selector */
    RxLbContinue
};

RxBreakClass
RxLineBreakClass(
    _In_ UINT32 CodePoint);

BOOLEAN
RxIsBreakWhitespace(
    _In_ UINT32 CodePoint);

/* One breakpoint byte per UTF-16 code unit */
VOID
RxAnalyzeBreakpoints(
    _In_reads_(Length) PCWSTR Text,
    _In_ UINT32 Length,
    _Out_writes_(Length) BYTE* Breakpoints,
    _Out_writes_(Length) RxBreakClass* Classes);
