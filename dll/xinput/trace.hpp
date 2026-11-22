/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Diagnostic lines for the debugger
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

/* Each module names itself in the line prefix */
extern const WCHAR XiTraceModule[];

/* One line to the debugger, always on, for failures and state changes only */
inline
VOID
XiTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    WCHAR Line[512];
    va_list Arguments;
    int Length;

    Length = _snwprintf_s(Line, _TRUNCATE, L"(%s) ", XiTraceModule);
    if (Length < 0)
        Length = 0;
    va_start(Arguments, Format);
    _vsnwprintf_s(Line + Length, ARRAYSIZE(Line) - Length, _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    Length = (int)wcslen(Line);
    if (Length < (int)ARRAYSIZE(Line) - 1)
    {
        Line[Length++] = L'\n';
        Line[Length] = UNICODE_NULL;
    }
    OutputDebugStringW(Line);
}

#define XI_TRACE(...) XiTrace(__VA_ARGS__)

/* Names a rejected argument once per entry point, so a polling game does not flood the debugger */
#define XI_TRACE_ONCE(...) \
    do \
    { \
        static LONG Reported; \
        if (InterlockedExchange(&Reported, 1) == 0) \
            XiTrace(__VA_ARGS__); \
    } while (0)
