/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Debugger output for failures and state changes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <stdarg.h>
#include <stdio.h>

/* Formats "(d3d11on12) message" and hands it to the debugger */
static
inline
VOID
On12Trace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    WCHAR Line[512];
    va_list Arguments;
    int Prefix;
    int Length;

    Prefix = _snwprintf_s(Line, _countof(Line), _TRUNCATE, L"(d3d11on12) ");
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
        Line[Prefix + Length + 1] = L'\0';
    }
    else
    {
        Line[_countof(Line) - 2] = L'\n';
        Line[_countof(Line) - 1] = L'\0';
    }

    OutputDebugStringW(Line);
}

/* Reports a message once per call site so callers that retry do not flood the debugger */
#define ON12_TRACE_ONCE(...) \
    do \
    { \
        static LONG Reported; \
        if (InterlockedExchange(&Reported, 1) == 0) \
            On12Trace(__VA_ARGS__); \
    } while (0)
