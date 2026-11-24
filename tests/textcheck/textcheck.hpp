/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared pieces of the DirectWrite comparison
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

struct TcSide
{
    PCWSTR Name;
    HMODULE Module;
    ComPtr<IDWriteFactory> Factory;
};

VOID
TcCheck(
    _In_ BOOLEAN Condition,
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...);

/* The en-us string, else the first */
std::wstring
TcName(
    _In_opt_ IDWriteLocalizedStrings* Names);

/* Face name, weight, stretch, style and simulations */
std::wstring
TcDescribeFont(
    _In_ IDWriteFont* Font);

VOID
TcCompareGdiInterop(
    _In_ TcSide& Want,
    _In_ TcSide& Got);
