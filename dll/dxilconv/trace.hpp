/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Debugger output for failures and state changes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <stdarg.h>
#include <stdio.h>

/* Formats "(dxilconv) message" and hands it to the debugger */
static
inline
VOID
DxcTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    WCHAR Line[512];
    va_list Arguments;
    int Prefix;
    int Length;

    Prefix = _snwprintf_s(Line, _countof(Line), _TRUNCATE, L"(dxilconv) ");
    if (Prefix < 0)
        return;

    va_start(Arguments, Format);
    Length = _vsnwprintf_s(Line + Prefix, _countof(Line) - Prefix, _TRUNCATE, Format, Arguments);
    va_end(Arguments);

    if (Length < 0)
        Length = static_cast<int>(wcslen(Line + Prefix));

    if (Prefix + Length + 1 < static_cast<int>(_countof(Line)))
    {
        Line[Prefix + Length] = L'\n';
        Line[Prefix + Length + 1] = UNICODE_NULL;
    }
    else
    {
        Line[_countof(Line) - 2] = L'\n';
        Line[_countof(Line) - 1] = UNICODE_NULL;
    }

    OutputDebugStringW(Line);
}
