/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DXBC program translation interface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <vector>

/* One element of an ISGN/OSGN style signature part */
typedef struct _DXC_SIGNATURE_ELEMENT
{
    const char* Name;
    ULONG SemanticIndex;
    ULONG SystemValue;
    ULONG ComponentType;
    ULONG Register;
    UCHAR Mask;
    UCHAR UsedMask;
    ULONG Stream;
    ULONG MinPrecision;
} DXC_SIGNATURE_ELEMENT;

/* Everything the container stage needs back from a translated program */
typedef struct _DXC_TRANSLATION
{
    ULONG ProgramKind;
    ULONG Major;
    ULONG Minor;
    std::vector<BYTE> Bitcode;
    BYTE StageInfo[16];
    ULONGLONG Features;
} DXC_TRANSLATION;

HRESULT
DxcReadSignature(
    _In_ const DXC_PART* Part,
    _In_ ULONG Stride,
    _Out_ std::vector<DXC_SIGNATURE_ELEMENT>& Elements);

HRESULT
DxcTranslateProgram(
    _In_reads_(TokenCount) const ULONG* Tokens,
    _In_ ULONG TokenCount,
    _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Inputs,
    _In_ const std::vector<DXC_SIGNATURE_ELEMENT>& Outputs,
    _Out_ DXC_TRANSLATION* Result);
