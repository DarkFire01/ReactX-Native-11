/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Debugger trace for failures and state changes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static
inline
VOID
DiTraceV(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    _In_ va_list Arguments)
{
    WCHAR Buffer[512];
    int Length;

    Length = _snwprintf_s(Buffer, _countof(Buffer), _TRUNCATE, L"(dinput) ");
    if (Length < 0)
        return;

    if (_vsnwprintf_s(Buffer + Length, _countof(Buffer) - Length, _TRUNCATE, Format, Arguments) < 0)
        Buffer[_countof(Buffer) - 2] = L'\0';

    Length = static_cast<int>(wcslen(Buffer));
    if (Length < static_cast<int>(_countof(Buffer)) - 1)
    {
        Buffer[Length] = L'\n';
        Buffer[Length + 1] = L'\0';
    }

    OutputDebugStringW(Buffer);
}

static
inline
VOID
DiTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    va_list Arguments;

    va_start(Arguments, Format);
    DiTraceV(Format, Arguments);
    va_end(Arguments);
}

/* Traces once per call site, for rejected arguments that callers may repeat every frame */
#define DI_TRACE_ONCE(...)                                      \
    do                                                          \
    {                                                           \
        static LONG DiTraceOnceDone;                            \
        if (InterlockedExchange(&DiTraceOnceDone, 1) == 0)      \
            DiTrace(__VA_ARGS__);                               \
    } while (0)
