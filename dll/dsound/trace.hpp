/*
 * PROJECT:     ReactX DirectSound
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Failure and state change messages for the debugger
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static
inline
VOID
DsTraceV(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    _In_ va_list Arguments)
{
    WCHAR Line[512];
    int Length;

    Length = _snwprintf_s(Line, _countof(Line), _TRUNCATE, L"(dsound) ");
    if (Length < 0)
        return;
    if (_vsnwprintf_s(Line + Length, _countof(Line) - Length - 1, _TRUNCATE, Format, Arguments) < 0)
        Line[_countof(Line) - 2] = L'\0';
    wcscat_s(Line, _countof(Line), L"\n");
    OutputDebugStringW(Line);
}

static
inline
VOID
DsTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    DsTraceV(Format, Arguments);
    va_end(Arguments);
}

/* Reports a rejected argument the first time a call site sees one */
#define DS_TRACE_ONCE(...) \
    do \
    { \
        static LONG DsTraceSeen; \
        if (InterlockedExchange(&DsTraceSeen, 1) == 0) \
            DsTrace(__VA_ARGS__); \
    } while (0)
