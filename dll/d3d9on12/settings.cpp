/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Tuning values read from the Direct3D9on12 registry key
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9on12.hpp"

static const CHAR NineSettingsKey[] = "Software\\Microsoft\\Direct3D\\Direct3D9on12";

/* A DWORD from the key, or Missing when the key or the value is absent */
static
ULONG
NineReadSetting(
    _In_opt_ HKEY Key,
    _In_z_ PCSTR Name,
    _In_ ULONG Missing)
{
    DWORD Value = 0;
    DWORD Size = sizeof(Value);

    if (Key == NULL)
        return Missing;
    if (RegQueryValueExA(Key, Name, NULL, NULL, reinterpret_cast<BYTE*>(&Value), &Size) != ERROR_SUCCESS)
        return Missing;
    return Value;
}

static
NineSettings
NineLoadSettings(VOID)
{
    NineSettings Settings;
    HKEY Key = NULL;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, NineSettingsKey, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        Key = NULL;

    Settings.SingleThread = NineReadSetting(Key, "Singlethread", 0) != 0;
    Settings.MaxUploadPerCommandList = NineReadSetting(Key, "MaxAllocatedUploadHeapSpacePerCommandList", ULONG_MAX);
    Settings.MaxSrvHeapSize = NineReadSetting(Key, "MaxSRVHeapSize", ULONG_MAX);
    Settings.BufferPoolTrimThreshold = NineReadSetting(Key, "BufferPoolTrimThreshold", ULONG_MAX);

    if (Key != NULL)
        RegCloseKey(Key);
    return Settings;
}

const NineSettings&
NineGetSettings(VOID)
{
    static const NineSettings Settings = NineLoadSettings();

    return Settings;
}
