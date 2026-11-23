/*
 * PROJECT:     ReactX XAudio2
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     XAudio2 2.8, whose exports all forward to XAudio2 2.9
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
