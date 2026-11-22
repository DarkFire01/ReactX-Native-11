/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The original XInput API, answered by XInput 1.4
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <xinput.h>

typedef DWORD (WINAPI* PX9_GET_STATE)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* PX9_SET_STATE)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI* PX9_GET_CAPABILITIES)(DWORD, DWORD, XINPUT_CAPABILITIES*);

static HMODULE X9Module;
static PX9_GET_STATE X9GetState;
static PX9_SET_STATE X9SetState;
static PX9_GET_CAPABILITIES X9GetCapabilities;

/* This API always described a plain wired gamepad, whatever is plugged in */
static const XINPUT_CAPABILITIES X9Capabilities =
{
    XINPUT_DEVTYPE_GAMEPAD, XINPUT_DEVSUBTYPE_GAMEPAD, XINPUT_CAPS_VOICE_SUPPORTED,
    { 0xF3FF, 1, 1, 1, 1, 1, 1 },
    { 1, 1 },
};

/* Loads XInput 1.4 the first time; the error codes are what a failed load leaves */
static
DWORD
X9Load(VOID)
{
    if (X9Module != nullptr)
        return ERROR_SUCCESS;

    X9Module = LoadLibraryExW(L"XInput1_4.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (X9Module == nullptr)
        return ERROR_MOD_NOT_FOUND;

    X9GetCapabilities = reinterpret_cast<PX9_GET_CAPABILITIES>(GetProcAddress(X9Module, "XInputGetCapabilities"));
    X9GetState = reinterpret_cast<PX9_GET_STATE>(GetProcAddress(X9Module, "XInputGetState"));
    X9SetState = reinterpret_cast<PX9_SET_STATE>(GetProcAddress(X9Module, "XInputSetState"));
    if (X9GetCapabilities != nullptr && X9GetState != nullptr && X9SetState != nullptr)
        return ERROR_SUCCESS;

    X9GetCapabilities = nullptr;
    X9GetState = nullptr;
    X9SetState = nullptr;
    FreeLibrary(X9Module);
    X9Module = nullptr;
    return ERROR_PROC_NOT_FOUND;
}

extern "C"
BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Instance);
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
    {
        X9Module = nullptr;
    }
    else if (Reason == DLL_PROCESS_DETACH && X9Module != nullptr)
    {
        FreeLibrary(X9Module);
        X9Module = nullptr;
    }
    return TRUE;
}

extern "C"
DWORD
WINAPI
XInputGetState(
    _In_ DWORD dwUserIndex,
    _Out_ XINPUT_STATE* pState) WIN_NOEXCEPT
{
    DWORD Result = X9Load();

    if (Result != ERROR_SUCCESS)
        return Result;
    return X9GetState(dwUserIndex, pState);
}

extern "C"
DWORD
WINAPI
XInputSetState(
    _In_ DWORD dwUserIndex,
    _In_ XINPUT_VIBRATION* pVibration) WIN_NOEXCEPT
{
    DWORD Result = X9Load();

    if (Result != ERROR_SUCCESS)
        return Result;
    return X9SetState(dwUserIndex, pVibration);
}

extern "C"
DWORD
WINAPI
XInputGetCapabilities(
    _In_ DWORD dwUserIndex,
    _In_ DWORD dwFlags,
    _Out_ XINPUT_CAPABILITIES* pCapabilities) WIN_NOEXCEPT
{
    DWORD Result = X9Load();

    if (Result != ERROR_SUCCESS)
        return Result;
    Result = X9GetCapabilities(dwUserIndex, dwFlags, pCapabilities);
    if (Result == ERROR_SUCCESS)
        *pCapabilities = X9Capabilities;
    return Result;
}

/* DirectSound GUIDs were never filled in; the call only reports whether the controller is there */
extern "C"
DWORD
WINAPI
XInputGetDSoundAudioDeviceGuids(
    _In_ DWORD dwUserIndex,
    _Out_ GUID* pDSoundRenderGuid,
    _Out_ GUID* pDSoundCaptureGuid)
{
    XINPUT_STATE State = {};
    DWORD Result;

    if (pDSoundRenderGuid == nullptr || pDSoundCaptureGuid == nullptr)
        return ERROR_BAD_ARGUMENTS;
    *pDSoundRenderGuid = {};
    *pDSoundCaptureGuid = {};

    Result = X9Load();
    if (Result != ERROR_SUCCESS)
        return Result;
    return X9GetState(dwUserIndex, &State);
}
