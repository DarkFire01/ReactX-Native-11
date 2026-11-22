/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Force feedback effect files written and read back through the keyboard device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

static const GUID DcCustomEffect = { 0x0BADF00D, 0x1234, 0x5678, { 0x9A, 0xBC, 0xDE, 0xF0, 0x01, 0x02, 0x03, 0x04 } };

static
std::wstring
DcTempFile(
    _In_z_ PCWSTR Name)
{
    WCHAR Path[MAX_PATH];

    GetTempPathW(_countof(Path), Path);
    return std::wstring(Path) + Name;
}

static
VOID
DcDumpFile(
    _In_z_ PCWSTR Path)
{
    HANDLE File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    BYTE Data[2048];
    DWORD Read = 0;

    if (File == INVALID_HANDLE_VALUE)
    {
        printf("  file missing\n");
        return;
    }
    ReadFile(File, Data, sizeof(Data), &Read, nullptr);
    CloseHandle(File);
    printf("  file size %lu\n", Read);
    DcBytes("  file", Data, Read);
}

static
BOOL
CALLBACK
DcPrintFileEffect(
    _In_ LPCDIFILEEFFECT Effect,
    _In_ VOID* Context)
{
    const DIEFFECT* Params = Effect->lpDiEffect;
    ULONG* Count = static_cast<ULONG*>(Context);

    printf("  effect size %lu %s '%s'\n", Effect->dwSize, DcGuid(&Effect->GuidEffect).c_str(), Effect->szFriendlyName);
    if (Params != nullptr)
    {
        printf("    params size %lu flags %08lX duration %lu period %lu gain %lu trigger %08lX repeat %lu axes %lu cb %lu delay %lu\n",
               Params->dwSize, Params->dwFlags, Params->dwDuration, Params->dwSamplePeriod, Params->dwGain,
               Params->dwTriggerButton, Params->dwTriggerRepeatInterval, Params->cAxes, Params->cbTypeSpecificParams,
               Params->dwStartDelay);
        printf("    pointers axes %s direction %s envelope %s specific %s\n", Params->rgdwAxes ? "set" : "null",
               Params->rglDirection ? "set" : "null", Params->lpEnvelope ? "set" : "null",
               Params->lpvTypeSpecificParams ? "set" : "null");
        for (DWORD Axis = 0; Axis < Params->cAxes && Axis < 4; Axis++)
        {
            printf("    axis %lu %08lX direction %ld\n", Axis, Params->rgdwAxes ? Params->rgdwAxes[Axis] : 0,
                   Params->rglDirection ? Params->rglDirection[Axis] : 0);
        }
        if (Params->lpEnvelope != nullptr)
            DcBytes("    envelope", Params->lpEnvelope, sizeof(DIENVELOPE));
        if (Params->lpvTypeSpecificParams != nullptr && Params->cbTypeSpecificParams <= 64)
            DcBytes("    specific", Params->lpvTypeSpecificParams, Params->cbTypeSpecificParams);
    }
    (*Count)++;
    return Count[1] != 0 && *Count >= Count[1] ? DIENUM_STOP : DIENUM_CONTINUE;
}

static
VOID
DcEnumFile(
    _In_ IDirectInputDevice8W* Device,
    _In_z_ PCSTR Label,
    _In_z_ PCWSTR Path,
    _In_ DWORD Flags,
    _In_ ULONG StopAfter)
{
    ULONG Count[2] = { 0, StopAfter };
    HRESULT Result = Device->EnumEffectsInFile(Path, DcPrintFileEffect, Count, Flags);

    printf("  enum %s flags %#lx = %08lX count %lu\n", Label, Flags, Result, Count[0]);
}

VOID
DcCheckEffectFiles(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();
    IDirectInputDevice8W* Device = nullptr;
    IDirectInputDevice8A* DeviceA = nullptr;
    std::wstring Path = DcTempFile(L"dinput8check.ffe");
    std::wstring Junk = DcTempFile(L"dinput8check.bad");
    DWORD Axes[2] = { DIJOFS_X, DIJOFS_Y };
    LONG Direction[2] = { 18000, 0 };
    DIENVELOPE Envelope = { sizeof(Envelope), 1000, 20000, 3000, 40000 };
    DIPERIODIC Periodic = { 5000, 100, 9000, 250000 };
    DICONSTANTFORCE Constant = { 7000 };
    DIEFFECT Params[3] = {};
    DIFILEEFFECT Effects[3] = {};

    if (Wide == nullptr || Ansi == nullptr || FAILED(Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr)) ||
        FAILED(Ansi->CreateDevice(GUID_SysKeyboard, &DeviceA, nullptr)))
    {
        return;
    }

    for (UINT Index = 0; Index < 3; Index++)
    {
        Params[Index].dwSize = sizeof(DIEFFECT);
        Params[Index].dwFlags = DIEFF_OBJECTOFFSETS | DIEFF_POLAR;
        Params[Index].dwDuration = 1000000 + Index;
        Params[Index].dwSamplePeriod = 10 + Index;
        Params[Index].dwGain = 9000 + Index;
        Params[Index].dwTriggerButton = DIEB_NOTRIGGER;
        Params[Index].dwTriggerRepeatInterval = 20 + Index;
        Params[Index].cAxes = 2;
        Params[Index].rgdwAxes = Axes;
        Params[Index].rglDirection = Direction;
        Params[Index].dwStartDelay = 30 + Index;
        Effects[Index].dwSize = sizeof(DIFILEEFFECT);
        Effects[Index].lpDiEffect = &Params[Index];
    }
    Effects[0].GuidEffect = GUID_Sine;
    Params[0].lpEnvelope = &Envelope;
    Params[0].cbTypeSpecificParams = sizeof(Periodic);
    Params[0].lpvTypeSpecificParams = &Periodic;
    strcpy_s(Effects[0].szFriendlyName, "Sine wave");
    Effects[1].GuidEffect = DcCustomEffect;
    strcpy_s(Effects[1].szFriendlyName, "Custom one");
    Effects[2].GuidEffect = GUID_ConstantForce;
    Params[2].cbTypeSpecificParams = sizeof(Constant);
    Params[2].lpvTypeSpecificParams = &Constant;
    strcpy_s(Effects[2].szFriendlyName, "Constant push");

    DeleteFileW(Path.c_str());
    DcEnumFile(Device, "missing", Path.c_str(), 0, 0);
    printf("  enum null callback = %08lX\n", Device->EnumEffectsInFile(Path.c_str(), nullptr, nullptr, 0));
    printf("  enum bad flags = %08lX\n", Device->EnumEffectsInFile(Path.c_str(), DcPrintFileEffect, nullptr, 2));
    printf("  write bad flags = %08lX\n", Device->WriteEffectToFile(Path.c_str(), 1, Effects, 2));
    printf("  write null effects = %08lX\n", Device->WriteEffectToFile(Path.c_str(), 1, nullptr, 0));

    printf("  write none = %08lX\n", Device->WriteEffectToFile(Path.c_str(), 0, Effects, 0));
    DcDumpFile(Path.c_str());
    DcEnumFile(Device, "none", Path.c_str(), 0, 0);

    printf("  write standard = %08lX\n", Device->WriteEffectToFile(Path.c_str(), 3, Effects, 0));
    DcDumpFile(Path.c_str());
    DcEnumFile(Device, "standard", Path.c_str(), DIFEF_INCLUDENONSTANDARD, 0);

    printf("  write all = %08lX\n", Device->WriteEffectToFile(Path.c_str(), 3, Effects, DIFEF_INCLUDENONSTANDARD));
    DcDumpFile(Path.c_str());
    DcEnumFile(Device, "all", Path.c_str(), 0, 0);
    DcEnumFile(Device, "all", Path.c_str(), DIFEF_INCLUDENONSTANDARD, 0);
    DcEnumFile(Device, "all", Path.c_str(), DIFEF_INCLUDENONSTANDARD | DIFEF_MODIFYIFNEEDED, 0);
    DcEnumFile(Device, "stop", Path.c_str(), DIFEF_INCLUDENONSTANDARD, 1);
    {
        ULONG Count[2] = { 0, 0 };
        CHAR Narrow[MAX_PATH];

        WideCharToMultiByte(CP_ACP, 0, Path.c_str(), -1, Narrow, _countof(Narrow), nullptr, nullptr);
        printf("  enum ansi = %08lX", DeviceA->EnumEffectsInFile(Narrow, DcPrintFileEffect, Count, DIFEF_INCLUDENONSTANDARD));
        printf(" count %lu\n", Count[0]);
    }

    /* A file that is not an effect file */
    {
        HANDLE File = CreateFileW(Junk.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        static const char Text[] = "RIFF\x10\x00\x00\x00WAVEjunkjunkjunk";
        DWORD Written;

        WriteFile(File, Text, sizeof(Text) - 1, &Written, nullptr);
        CloseHandle(File);
        DcEnumFile(Device, "junk", Junk.c_str(), 0, 0);
        DeleteFileW(Junk.c_str());
    }

    /* Keep a copy for the other runtime to read with -read */
    CopyFileW(Path.c_str(), DcTempFile(L"dinput8check_keep.ffe").c_str(), FALSE);
    DeleteFileW(Path.c_str());
    DeviceA->Release();
    Device->Release();
    Ansi->Release();
    Wide->Release();
}

/* Reads a file the other runtime wrote, to check both read each other's output */
VOID
DcCheckEffectFileRead(
    _In_z_ PCWSTR Path)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInputDevice8W* Device = nullptr;

    if (Wide == nullptr || FAILED(Wide->CreateDevice(GUID_SysKeyboard, &Device, nullptr)))
        return;
    DcEnumFile(Device, "foreign", Path, DIFEF_INCLUDENONSTANDARD, 0);
    Device->Release();
    Wide->Release();
}
