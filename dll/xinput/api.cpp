/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The exported API: argument checks, then a request to the controller on the port
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xinput.hpp"

/* XInputEnable is marked deprecated by the header that declares it */
#pragma warning(disable: 4995)

typedef DWORD (WINAPI* PXI_GET_STATE)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI* PXI_SET_STATE)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI* PXI_GET_CAPABILITIES)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef VOID (WINAPI* PXI_ENABLE)(BOOL);
typedef DWORD (WINAPI* PXI_GET_AUDIO_IDS)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);
typedef DWORD (WINAPI* PXI_GET_BATTERY)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD (WINAPI* PXI_GET_KEYSTROKE)(DWORD, DWORD, PXINPUT_KEYSTROKE);

/* An app container reaches controllers through the brokered implementation */
static HMODULE XiUapModule;
static PXI_GET_STATE XiUapGetState;
static PXI_SET_STATE XiUapSetState;
static PXI_GET_CAPABILITIES XiUapGetCapabilities;
static PXI_ENABLE XiUapEnable;
static PXI_GET_AUDIO_IDS XiUapGetAudioDeviceIds;
static PXI_GET_BATTERY XiUapGetBatteryInformation;
static PXI_GET_KEYSTROKE XiUapGetKeystroke;

/* DllMain reasons a host uses to change what this library does to controllers */
#define XI_REASON_SETTINGS_BASE     0xBEEF0000

/* Inside an app container every entry point must come from XInputUap, or the library does not load */
HRESULT
XiLoadOverrides(VOID)
{
    static const PCSTR Names[] =
    {
        "XInputGetState", "XInputSetState", "XInputGetCapabilities", "XInputEnable", "XInputGetAudioDeviceIds",
        "XInputGetBatteryInformation", "XInputGetKeystroke",
    };
    FARPROC Found[ARRAYSIZE(Names)] = {};
    DWORD AppContainer = 0;
    DWORD Size = 0;

    if (!GetTokenInformation(GetCurrentProcessToken(), TokenIsAppContainer, &AppContainer, sizeof(AppContainer), &Size) ||
        AppContainer == 0)
    {
        return S_OK;
    }

    XiUapModule = LoadLibraryExW(L"XInputUap.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (XiUapModule == nullptr)
        return HRESULT_FROM_WIN32(GetLastError());
    for (UINT Index = 0; Index < ARRAYSIZE(Names); Index++)
    {
        Found[Index] = GetProcAddress(XiUapModule, Names[Index]);
        if (Found[Index] == nullptr)
            return HRESULT_FROM_WIN32(GetLastError());
    }

    XiUapGetState = reinterpret_cast<PXI_GET_STATE>(Found[0]);
    XiUapSetState = reinterpret_cast<PXI_SET_STATE>(Found[1]);
    XiUapGetCapabilities = reinterpret_cast<PXI_GET_CAPABILITIES>(Found[2]);
    XiUapEnable = reinterpret_cast<PXI_ENABLE>(Found[3]);
    XiUapGetAudioDeviceIds = reinterpret_cast<PXI_GET_AUDIO_IDS>(Found[4]);
    XiUapGetBatteryInformation = reinterpret_cast<PXI_GET_BATTERY>(Found[5]);
    XiUapGetKeystroke = reinterpret_cast<PXI_GET_KEYSTROKE>(Found[6]);
    return S_OK;
}

VOID
XiUnloadOverrides(VOID)
{
    XiUapGetState = nullptr;
    XiUapSetState = nullptr;
    XiUapGetCapabilities = nullptr;
    XiUapEnable = nullptr;
    XiUapGetAudioDeviceIds = nullptr;
    XiUapGetBatteryInformation = nullptr;
    XiUapGetKeystroke = nullptr;
    if (XiUapModule != nullptr)
    {
        FreeLibrary(XiUapModule);
        XiUapModule = nullptr;
    }
}

static
BOOL
XiApplySetting(
    _In_ DWORD Reason,
    _In_opt_ PVOID Value)
{
    UINT Bit;

    switch (Reason)
    {
        case XI_REASON_SETTINGS_BASE:
            return TRUE;
        case XI_REASON_SETTINGS_BASE + 1:
            Bit = XI_SETTING_ASSIGN_LEDS;
            break;
        case XI_REASON_SETTINGS_BASE + 2:
            Bit = XI_SETTING_CLEAR_LEDS;
            break;
        case XI_REASON_SETTINGS_BASE + 3:
            Bit = XI_SETTING_STOP_VIBRATION;
            break;
        default:
            return FALSE;
    }
    if (Value != nullptr)
        XiSettings |= Bit;
    else
        XiSettings &= ~Bit;
    return TRUE;
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

    switch (Reason)
    {
        case DLL_PROCESS_ATTACH:
            return XiInitialize();
        case DLL_PROCESS_DETACH:
            XiClose(Reserved != nullptr);
            return TRUE;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            return TRUE;
        default:
            return XiApplySetting(Reason, Reserved);
    }
}

/* The state, guide button included */
static
DWORD
XiStateRequest(
    _In_ DWORD UserIndex,
    _Out_ XINPUT_STATE* State)
{
    return XiReturnCode(XiProcessRequest(UserIndex, XiGetStateRoutine, State, FALSE));
}

extern "C"
DWORD
WINAPI
XInputGetState(
    _In_ DWORD dwUserIndex,
    _Out_ XINPUT_STATE* pState) WIN_NOEXCEPT
{
    DWORD Result;

    if (dwUserIndex >= XI_USER_COUNT || pState == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetState != nullptr)
        return XiUapGetState(dwUserIndex, pState);

    Result = XiStateRequest(dwUserIndex, pState);
    if (Result == ERROR_SUCCESS)
        pState->Gamepad.wButtons &= ~XI_GAMEPAD_GUIDE;
    return Result;
}

extern "C"
DWORD
WINAPI
XInputGetStateEx(
    _In_ DWORD dwUserIndex,
    _Out_ XINPUT_STATE* pState)
{
    if (dwUserIndex >= XI_USER_COUNT || pState == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetState != nullptr)
        return XiUapGetState(dwUserIndex, pState);
    return XiStateRequest(dwUserIndex, pState);
}

extern "C"
DWORD
WINAPI
XInputSetState(
    _In_ DWORD dwUserIndex,
    _In_ XINPUT_VIBRATION* pVibration) WIN_NOEXCEPT
{
    if (dwUserIndex >= XI_USER_COUNT || pVibration == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapSetState != nullptr)
        return XiUapSetState(dwUserIndex, pVibration);
    return XiReturnCode(XiProcessRequest(dwUserIndex, XiSetVibrationRoutine, pVibration, FALSE));
}

/* Only revision 1 of the extended capabilities exists */
extern "C"
DWORD
WINAPI
XInputGetCapabilitiesEx(
    _In_ DWORD dwRevision,
    _In_ DWORD dwUserIndex,
    _In_ DWORD dwFlags,
    _Out_ XI_CAPABILITIES_EX* pCapabilities)
{
    DWORD Result;

    if (dwUserIndex >= XI_USER_COUNT || dwFlags > XINPUT_FLAG_GAMEPAD || pCapabilities == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (dwRevision != 1)
        return ERROR_INVALID_DATA;

    Result = XiReturnCode(XiProcessRequest(dwUserIndex, XiGetCapabilities, pCapabilities, FALSE));
    if (Result == ERROR_SUCCESS)
        pCapabilities->Capabilities.Gamepad.wButtons &= ~XI_GAMEPAD_GUIDE;
    return Result;
}

extern "C"
DWORD
WINAPI
XInputGetCapabilities(
    _In_ DWORD dwUserIndex,
    _In_ DWORD dwFlags,
    _Out_ XINPUT_CAPABILITIES* pCapabilities) WIN_NOEXCEPT
{
    XI_CAPABILITIES_EX Capabilities = {};
    DWORD Result;

    if (pCapabilities == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetCapabilities != nullptr)
        return XiUapGetCapabilities(dwUserIndex, dwFlags, pCapabilities);

    Result = XInputGetCapabilitiesEx(1, dwUserIndex, dwFlags, &Capabilities);
    if (Result == ERROR_SUCCESS)
        *pCapabilities = Capabilities.Capabilities;
    return Result;
}

extern "C"
void
WINAPI
XInputEnable(
    _In_ BOOL enable) WIN_NOEXCEPT
{
    if (XiUapEnable != nullptr)
        XiUapEnable(enable);
    else
        XiEnableCommunications(enable != FALSE);
}

/* Any user asks each port in turn and stops at the first one with a keystroke waiting */
extern "C"
DWORD
WINAPI
XInputGetKeystroke(
    _In_ DWORD dwUserIndex,
    _Reserved_ DWORD dwReserved,
    _Out_ PXINPUT_KEYSTROKE pKeystroke) WIN_NOEXCEPT
{
    XI_KEYSTROKE_REQUEST Request = { dwReserved, pKeystroke, 0 };
    HRESULT hr;

    if ((dwUserIndex != XUSER_INDEX_ANY && dwUserIndex >= XI_USER_COUNT) || pKeystroke == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetKeystroke != nullptr)
        return XiUapGetKeystroke(dwUserIndex, dwReserved, pKeystroke);

    if (dwUserIndex == XUSER_INDEX_ANY)
    {
        Request.Result = ERROR_EMPTY;
        for (UINT Port = 0; Port < XI_USER_COUNT; Port++)
        {
            XiProcessRequest(Port, XiGetKeystroke, &Request, FALSE);
            if (Request.Result == ERROR_SUCCESS)
            {
                pKeystroke->UserIndex = (BYTE)Port;
                return ERROR_SUCCESS;
            }
        }
        return ERROR_EMPTY;
    }

    hr = XiProcessRequest(dwUserIndex, XiGetKeystroke, &Request, FALSE);
    if (FAILED(hr))
        return XiReturnCode(hr);
    if (hr == S_FALSE)
        return ERROR_DEVICE_NOT_CONNECTED;
    pKeystroke->UserIndex = (BYTE)dwUserIndex;
    return Request.Result;
}

/*
 * A headset battery is read straight from whichever interface answers for the user index,
 * before the controller on the port is asked.
 */
extern "C"
DWORD
WINAPI
XInputGetBatteryInformation(
    _In_ DWORD dwUserIndex,
    _In_ BYTE devType,
    _Out_ XINPUT_BATTERY_INFORMATION* pBatteryInformation) WIN_NOEXCEPT
{
    XI_BATTERY_REQUEST Request = { devType, pBatteryInformation };

    if (dwUserIndex >= XI_USER_COUNT || pBatteryInformation == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetBatteryInformation != nullptr)
        return XiUapGetBatteryInformation(dwUserIndex, devType, pBatteryInformation);

    pBatteryInformation->BatteryType = BATTERY_TYPE_DISCONNECTED;
    pBatteryInformation->BatteryLevel = BATTERY_LEVEL_EMPTY;
    if (devType == BATTERY_DEVTYPE_HEADSET)
    {
        XiInterfaceWalk Walk;

        if (Walk.Restart())
        {
            HANDLE Handle;

            while (Walk.Next(&Handle))
            {
                XI_DEVICE Device;
                BOOLEAN Done = FALSE;

                if (Handle == INVALID_HANDLE_VALUE)
                    continue;
                if (XiFillFromInterface(Handle, &Device))
                {
                    Device.Index = (UCHAR)dwUserIndex;
                    Done = SUCCEEDED(XiQueryBattery(&Device, BATTERY_DEVTYPE_HEADSET, pBatteryInformation));
                }
                CloseHandle(Handle);
                if (Done)
                    return ERROR_SUCCESS;
            }
        }
    }

    return XiReturnCode(XiProcessRequest(dwUserIndex, XiGetBattery, &Request, FALSE));
}

extern "C"
DWORD
WINAPI
XInputGetAudioDeviceIds(
    _In_ DWORD dwUserIndex,
    _Out_writes_opt_(*pRenderCount) LPWSTR pRenderDeviceId,
    _Inout_opt_ UINT* pRenderCount,
    _Out_writes_opt_(*pCaptureCount) LPWSTR pCaptureDeviceId,
    _Inout_opt_ UINT* pCaptureCount) WIN_NOEXCEPT
{
    XI_AUDIO_REQUEST Request = { pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount };
    OSVERSIONINFOW Version = {};
    XiInterfaceWalk Walk;

    if (dwUserIndex >= XI_USER_COUNT || (pRenderCount == nullptr && pCaptureCount == nullptr))
        return ERROR_BAD_ARGUMENTS;
    if ((pRenderDeviceId != nullptr && pRenderCount == nullptr) || (pCaptureDeviceId != nullptr && pCaptureCount == nullptr))
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetAudioDeviceIds != nullptr)
        return XiUapGetAudioDeviceIds(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount);

    Version.dwOSVersionInfoSize = sizeof(Version);
#pragma warning(suppress: 4996)
    if (GetVersionExW(&Version) && Version.dwMajorVersion < 6)
        return ERROR_NOT_SUPPORTED;

    if (Walk.Restart())
    {
        HANDLE Handle;

        while (Walk.Next(&Handle))
        {
            XI_DEVICE Device;
            BOOLEAN Done = FALSE;

            if (Handle == INVALID_HANDLE_VALUE)
                continue;
            if (XiFillFromInterface(Handle, &Device) && Device.Version >= 0x102)
            {
                Device.Index = (UCHAR)dwUserIndex;
                Done = SUCCEEDED(XiGetAudioDevice(&Device, &Request));
            }
            CloseHandle(Handle);
            if (Done)
                return ERROR_SUCCESS;
        }
    }

    return XiReturnCode(XiProcessRequest(dwUserIndex, XiGetAudioDevice, &Request, FALSE));
}

/* Interfaces rather than users: an index names one of the sixteen interface slots */
extern "C"
DWORD
WINAPI
XInputGetBaseBusInformation(
    _In_ DWORD dwBusIndex,
    _Out_ XI_BASE_BUS_INFORMATION* pInformation)
{
    if (dwBusIndex >= XI_BUS_COUNT || pInformation == nullptr)
        return ERROR_BAD_ARGUMENTS;
    return XiReturnCode(XiProcessRequest(dwBusIndex, XiGetBaseBus, pInformation, TRUE));
}

extern "C"
DWORD
WINAPI
XInputPowerOffController(
    _In_ DWORD dwUserIndex)
{
    if (dwUserIndex >= XI_USER_COUNT)
        return ERROR_BAD_ARGUMENTS;
    return XiReturnCode(XiProcessRequest(dwUserIndex, XiPowerOff, nullptr, FALSE));
}

extern "C"
DWORD
WINAPI
XInputWaitForGuideButton(
    _In_ DWORD dwUserIndex,
    _In_opt_ HANDLE hEvent,
    _Out_ XI_GUIDE_RESULT* pResult)
{
    XI_GUIDE_REQUEST Request = { hEvent, pResult };

    if (dwUserIndex >= XI_USER_COUNT || pResult == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetState != nullptr)
        return ERROR_ACCESS_DENIED;
    if (hEvent == INVALID_HANDLE_VALUE)
        Request.Event = nullptr;
    return XiReturnCode(XiProcessRequest(dwUserIndex, XiStartGuideWait, &Request, FALSE));
}

extern "C"
DWORD
WINAPI
XInputCancelGuideButtonWait(
    _In_ DWORD dwUserIndex)
{
    if (dwUserIndex >= XI_USER_COUNT)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetState != nullptr)
        return ERROR_ACCESS_DENIED;
    return XiReturnCode(XiProcessRequest(dwUserIndex, XiCancelGuideWait, nullptr, FALSE));
}

extern "C"
DWORD
WINAPI
XInputGetSystemButtons(
    _In_ DWORD dwUserIndex,
    _Out_ XI_SYSTEM_BUTTONS* pButtons)
{
    if (dwUserIndex >= XI_USER_COUNT || pButtons == nullptr)
        return ERROR_BAD_ARGUMENTS;
    if (XiUapGetState != nullptr)
        return ERROR_ACCESS_DENIED;
    return XiReturnCode(XiProcessRequest(dwUserIndex, XiGetSystemButtonsRoutine, pButtons, FALSE));
}
