/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Debugger output for failures and state changes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <stdarg.h>
#include <stdio.h>

static inline
VOID
DiTraceV(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    _In_ va_list Arguments)
{
    WCHAR Buffer[512];
    int Length;

    Length = _snwprintf_s(Buffer, _countof(Buffer), _TRUNCATE, L"(dinput8) ");
    if (Length < 0)
        return;
    _vsnwprintf_s(Buffer + Length, _countof(Buffer) - Length, _TRUNCATE, Format, Arguments);
    Length = static_cast<int>(wcslen(Buffer));
    if (Length < static_cast<int>(_countof(Buffer)) - 1)
    {
        Buffer[Length] = L'\n';
        Buffer[Length + 1] = L'\0';
    }
    else
    {
        Buffer[_countof(Buffer) - 2] = L'\n';
    }
    OutputDebugStringW(Buffer);
}

static inline
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

/* Reports a rejected argument the first time a call site sees it */
#define DI_TRACE_ONCE(...)                                      \
    do                                                          \
    {                                                           \
        static LONG DiTraceSeen;                                \
        if (InterlockedExchange(&DiTraceSeen, 1) == 0)          \
            DiTrace(__VA_ARGS__);                               \
    } while (0)
