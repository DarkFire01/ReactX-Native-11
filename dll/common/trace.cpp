/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Diagnostic output, enabled by REACTX_LOG or by reactx.ini next to the module
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"

#include <stdarg.h>
#include <stdio.h>

static INIT_ONCE TraceInitOnce = INIT_ONCE_STATIC_INIT;
static BOOLEAN TraceEnabled;
static HANDLE TraceFile = INVALID_HANDLE_VALUE;

/* The directory this module was loaded from, with a trailing backslash */
static
BOOLEAN
RxModuleDirectory(
    _Out_writes_(Size) PWSTR Directory,
    _In_ DWORD Size)
{
    HMODULE Self;
    PWSTR Slash;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RxModuleDirectory), &Self) ||
        GetModuleFileNameW(Self, Directory, Size) == 0)
    {
        return FALSE;
    }

    Slash = wcsrchr(Directory, L'\\');
    if (Slash == nullptr)
        return FALSE;

    *(Slash + 1) = UNICODE_NULL;
    return TRUE;
}

/* Names the build of this module once, so a log from another machine says what it ran */
static
VOID
RxTraceBuild(VOID)
{
    HMODULE Self;
    const IMAGE_DOS_HEADER* Dos;
    const IMAGE_NT_HEADERS* Nt;
    ULARGE_INTEGER Time;
    FILETIME FileTime;
    SYSTEMTIME Linked;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RxTraceBuild), &Self))
    {
        return;
    }

    Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Self);
    Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(Self) + Dos->e_lfanew);

    /* The link time counts seconds from 1970, a FILETIME 100 ns ticks from 1601 */
    Time.QuadPart = (ULONGLONG)Nt->FileHeader.TimeDateStamp * 10000000ull + 116444736000000000ull;
    FileTime.dwLowDateTime = Time.LowPart;
    FileTime.dwHighDateTime = Time.HighPart;
    if (!FileTimeToSystemTime(&FileTime, &Linked))
        return;

    RxTrace(L"ReactX %s built %04u-%02u-%02u %02u:%02u UTC", RxTraceModule, Linked.wYear, Linked.wMonth, Linked.wDay,
            Linked.wHour, Linked.wMinute);
}

/*
 * Logging is on when REACTX_LOG is set, or when reactx.ini beside the module says log=1.
 * REACTX_LOG_FILE, or logfile= in the ini, sends it to a file; games have no console.
 */
static
BOOL
CALLBACK
RxInitializeTrace(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Out_opt_ PVOID* Context)
{
    WCHAR Directory[MAX_PATH];
    WCHAR Settings[MAX_PATH];
    WCHAR LogPath[MAX_PATH] = {};
    WCHAR FullPath[MAX_PATH];

    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    TraceEnabled = GetEnvironmentVariableW(L"REACTX_LOG", NULL, 0) != 0;
    GetEnvironmentVariableW(L"REACTX_LOG_FILE", LogPath, _countof(LogPath));

    if (RxModuleDirectory(Directory, _countof(Directory)) &&
        swprintf_s(Settings, L"%sreactx.ini", Directory) > 0 &&
        GetFileAttributesW(Settings) != INVALID_FILE_ATTRIBUTES)
    {
        TraceEnabled = TraceEnabled || GetPrivateProfileIntW(L"reactx", L"log", 0, Settings) != 0;
        if (LogPath[0] == UNICODE_NULL)
            GetPrivateProfileStringW(L"reactx", L"logfile", L"", LogPath, _countof(LogPath), Settings);
    }

    if (!TraceEnabled || LogPath[0] == UNICODE_NULL)
        return TRUE;

    /* A bare file name lands next to the module */
    if (wcschr(LogPath, L'\\') == nullptr && wcschr(LogPath, L':') == nullptr &&
        swprintf_s(FullPath, L"%s%s", Directory, LogPath) > 0)
    {
        wcscpy_s(LogPath, FullPath);
    }

    TraceFile = CreateFileW(LogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    RxTraceBuild();
    return TRUE;
}

BOOLEAN
RxIsTraceEnabled(VOID)
{
    InitOnceExecuteOnce(&TraceInitOnce, RxInitializeTrace, nullptr, nullptr);
    return TraceEnabled;
}

VOID
RxTrace(
    _In_z_ _Printf_format_string_ PCWSTR Format,
    ...)
{
    WCHAR Line[1024];
    char Utf8[sizeof(Line) * 2];
    va_list Arguments;
    int Length;
    DWORD Written;
    HANDLE ErrorOutput;

    Length = _snwprintf_s(Line, _TRUNCATE, L"[%s %lu] ", RxTraceModule, GetCurrentThreadId());
    va_start(Arguments, Format);
    Length += _vsnwprintf_s(Line + Length, _countof(Line) - Length, _TRUNCATE, Format, Arguments);
    va_end(Arguments);
    if (Length < 0)
        Length = (int)wcslen(Line);
    if (Length < (int)_countof(Line) - 1)
    {
        Line[Length++] = L'\n';
        Line[Length] = UNICODE_NULL;
    }

    OutputDebugStringW(Line);

    Length = WideCharToMultiByte(CP_UTF8, 0, Line, Length, Utf8, sizeof(Utf8), NULL, NULL);
    if (Length <= 0)
        return;

    /* Appends from both modules and every thread land whole */
    if (TraceFile != INVALID_HANDLE_VALUE)
        WriteFile(TraceFile, Utf8, (DWORD)Length, &Written, NULL);

    ErrorOutput = GetStdHandle(STD_ERROR_HANDLE);
    if (ErrorOutput != NULL && ErrorOutput != INVALID_HANDLE_VALUE)
        WriteFile(ErrorOutput, Utf8, (DWORD)Length, &Written, NULL);
}
