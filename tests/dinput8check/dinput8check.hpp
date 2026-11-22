/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared helpers for the DirectInput 8 checks
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

#include <cstdio>
#include <string>
#include <vector>

typedef HRESULT (WINAPI* PDC_CREATE)(HINSTANCE, DWORD, REFIID, VOID**, IUnknown*);

/* A value no runtime writes, to tell an untouched out parameter from a cleared one */
#define DC_SENTINEL reinterpret_cast<VOID*>(static_cast<ULONG_PTR>(0x5A5A5A5A))

extern HMODULE DcModule;
extern PDC_CREATE DcCreate;
extern HWND DcWindow;

std::string
DcGuid(
    _In_opt_ const GUID* Guid);

std::string
DcWide(
    _In_z_ PCWSTR Text);

VOID
DcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size);

PCSTR
DcPointer(
    _In_opt_ const VOID* Value);

IDirectInput8W*
DcCreateWide(VOID);

IDirectInput8A*
DcCreateAnsi(VOID);

VOID
DcCheckDeviceInfo(VOID);

VOID
DcInitFormats(VOID);

extern DIDATAFORMAT DcKeyboardFormat;
extern DIDATAFORMAT DcMouseFormat;
extern DIDATAFORMAT DcMouse2Format;
extern DIDATAFORMAT DcJoystickFormat;

/* Objects a format may ask for that the device need not have */
#define DC_OPTIONAL         0x80000000

VOID
DcCheckEffectFiles(VOID);

VOID
DcCheckActions(VOID);

VOID
DcCheckEffectFileRead(
    _In_z_ PCWSTR Path);

VOID
DcRunDataChecks(
    _In_z_ PCSTR Name);

VOID
DcCheckJoyConfig(VOID);
