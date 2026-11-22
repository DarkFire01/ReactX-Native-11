/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Controllers, their ports and the requests made of them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <setupapi.h>
#include <devpkey.h>
#include <xinput.h>

/* Controllers the API addresses by user index */
#define XI_USER_COUNT       4

/* Interfaces the driver exposes, each carrying one or more controllers */
#define XI_BUS_COUNT        16

/* Port numbers and indexes that name nothing */
#define XI_NONE             255

#define XI_DEVICE_CONNECTED 0x1
#define XI_DEVICE_BUS       0x2
#define XI_DEVICE_EMBEDDED  0x4

/* The guide button, which the classic API hides */
#define XI_GAMEPAD_GUIDE    0x0400

/* Settings a host can change through DllMain reasons */
#define XI_SETTING_ASSIGN_LEDS      0x1
#define XI_SETTING_CLEAR_LEDS       0x2
#define XI_SETTING_STOP_VIBRATION   0x4

typedef struct _XI_SYSTEM_BUTTONS
{
    DWORD Guide;
    DWORD Share;
    DWORD Reserved[6];
} XI_SYSTEM_BUTTONS;

/* Capabilities as the extended entry points return them */
typedef struct _XI_CAPABILITIES_EX
{
    XINPUT_CAPABILITIES Capabilities;
    WORD VendorId;
    WORD ProductId;
    WORD Revision;
    WORD Padding;
    DWORD SerialNumber;
} XI_CAPABILITIES_EX;

typedef struct _XI_BASE_BUS_INFORMATION
{
    WORD VendorId;
    WORD ProductId;
    WORD InputReportType;
    WORD Padding;
    DWORD SerialNumber;
    BYTE Interface[3];
    BYTE Reserved;
} XI_BASE_BUS_INFORMATION;

typedef struct _XI_GUIDE_RESULT
{
    DWORD Status;
    XINPUT_STATE State;
} XI_GUIDE_RESULT;

typedef struct _XI_DEVICE
{
    UINT Flags;
    HANDLE Device;
    HANDLE GuideWait;
    UCHAR Index;                /* controller number on its interface */
    PWSTR Path;
    SIZE_T PathLength;
    USHORT Version;             /* driver protocol, 0x100 for the first one */
    XINPUT_STATE State;
    XINPUT_VIBRATION Vibration;
    XI_SYSTEM_BUTTONS SystemButtons;
    USHORT VendorId;
    USHORT ProductId;
    UCHAR AudioInterface;
    UINT Bus;
    BOOL ShareButton;
    UCHAR TriggerKeys;
    USHORT ButtonKeys;
    USHORT LeftThumbKey;
    USHORT RightThumbKey;
} XI_DEVICE, *PXI_DEVICE;

typedef
HRESULT
(*PXI_REQUEST_ROUTINE)(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

/* What the protocol answers about an interface */
typedef struct _XI_INTERFACE_INFORMATION
{
    USHORT Version;
    UCHAR DeviceCount;
    UCHAR Reserved;
    UCHAR Flags;
    UCHAR Reserved2[3];
    USHORT VendorId;
    USHORT ProductId;
} XI_INTERFACE_INFORMATION;

/* core.cpp */

extern PXI_REQUEST_ROUTINE XiGetStateRoutine;
extern PXI_REQUEST_ROUTINE XiSetVibrationRoutine;
extern PXI_REQUEST_ROUTINE XiGetSystemButtonsRoutine;
extern UINT XiSettings;

BOOLEAN
XiInitialize(VOID);

VOID
XiClose(
    _In_ BOOLEAN ProcessExit);

VOID
XiEnableCommunications(
    _In_ BOOLEAN Enable);

HRESULT
XiLeave(VOID);

HRESULT
XiProcessRequest(
    _In_ UINT Port,
    _In_ PXI_REQUEST_ROUTINE Routine,
    _Inout_opt_ PVOID Context,
    _In_ BOOLEAN ByBus);

HRESULT
XiGetDeviceOnPort(
    _In_ UINT Port,
    _Out_ PXI_DEVICE* Device,
    _In_ UCHAR Retries);

DWORD
XiReturnCode(
    _In_ HRESULT Result);

PXI_DEVICE
XiCreateDevice(
    _In_ HANDLE Handle,
    _In_z_ PCWSTR Path,
    _In_opt_ HDEVINFO List,
    _In_opt_ PSP_DEVINFO_DATA DeviceData);

VOID
XiDestroyDevice(
    _In_opt_ PXI_DEVICE Device);

BOOLEAN
XiFillFromInterface(
    _In_ HANDLE Handle,
    _Out_ PXI_DEVICE Device);

HANDLE
XiOpenDevice(
    _In_z_ PCWSTR Path,
    _In_ DWORD Attributes);

/* Walks every interface of the class, handing out an open handle for each */
class XiInterfaceWalk
{
public:
    XiInterfaceWalk() = default;
    ~XiInterfaceWalk();

    BOOLEAN
    Restart(VOID);

    BOOLEAN
    Next(
        _Out_ PHANDLE Handle);

private:
    HDEVINFO List = INVALID_HANDLE_VALUE;
    DWORD Index = 0;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail = nullptr;
    DWORD DetailSize = 0;
};

BOOLEAN
XiIsEmbedded(
    _In_opt_ HDEVINFO List,
    _In_opt_ PSP_DEVINFO_DATA DeviceData);

PVOID
XiDeviceProperty(
    _In_ HDEVINFO List,
    _In_ PSP_DEVINFO_DATA DeviceData,
    _In_ const DEVPROPKEY* Key);

VOID
XiFreeProperty(
    _In_opt_ PVOID Value);

/* device.cpp: what each request does to one controller */

HRESULT
XiEnabledGetState(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiDisabledGetState(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiEnabledSetVibration(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiDisabledSetVibration(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiEnabledGetSystemButtons(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiDisabledGetSystemButtons(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiGetCapabilities(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

typedef struct _XI_BATTERY_REQUEST
{
    BYTE DeviceType;
    XINPUT_BATTERY_INFORMATION* Battery;
} XI_BATTERY_REQUEST;

HRESULT
XiGetBattery(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

typedef struct _XI_KEYSTROKE_REQUEST
{
    DWORD Reserved;
    PXINPUT_KEYSTROKE Keystroke;
    DWORD Result;
} XI_KEYSTROKE_REQUEST;

HRESULT
XiGetKeystroke(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiGetBaseBus(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiPowerOff(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

typedef struct _XI_GUIDE_REQUEST
{
    HANDLE Event;
    XI_GUIDE_RESULT* Result;
} XI_GUIDE_REQUEST;

HRESULT
XiStartGuideWait(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

HRESULT
XiCancelGuideWait(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);

/* api.cpp: an app container takes its controllers from XInputUap instead */

HRESULT
XiLoadOverrides(VOID);

VOID
XiUnloadOverrides(VOID);

/* driver.cpp */

HRESULT
XiIoctl(
    _In_ HANDLE Handle,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InputSize) PVOID Input,
    _In_ DWORD InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID Output,
    _In_ DWORD OutputSize,
    _Inout_opt_ LPOVERLAPPED Overlapped);

HRESULT
XiQueryInformation(
    _In_ HANDLE Handle,
    _Out_ XI_INTERFACE_INFORMATION* Information);

HRESULT
XiQueryShareButton(
    _In_ HANDLE Handle,
    _Out_ BOOL* Supported);

HRESULT
XiReadState(
    _Inout_ PXI_DEVICE Device);

HRESULT
XiQueryCapabilities(
    _In_ PXI_DEVICE Device,
    _Out_ XI_CAPABILITIES_EX* Capabilities);

HRESULT
XiQueryBattery(
    _In_ PXI_DEVICE Device,
    _In_ BYTE DeviceType,
    _Out_ XINPUT_BATTERY_INFORMATION* Battery);

HRESULT
XiQueryAudioInformation(
    _In_ PXI_DEVICE Device,
    _Out_ PUSHORT VendorId,
    _Out_ PUSHORT ProductId,
    _Out_ PUCHAR Interface);

HRESULT
XiQueryBaseBus(
    _In_ PXI_DEVICE Device,
    _Out_ XI_BASE_BUS_INFORMATION* Information);

HRESULT
XiQueryLedState(
    _In_ PXI_DEVICE Device,
    _Out_ PUCHAR State);

HRESULT
XiSendVibration(
    _In_ PXI_DEVICE Device);

HRESULT
XiSendLedState(
    _In_ PXI_DEVICE Device,
    _In_ UCHAR State);

HRESULT
XiSendPowerOff(
    _In_ PXI_DEVICE Device);

HRESULT
XiWaitForGuide(
    _In_ HANDLE Handle,
    _In_ UCHAR Index,
    _Out_ XI_GUIDE_RESULT* Result);

/* keystroke.cpp */

DWORD
XiNextKeystroke(
    _Inout_ PXI_DEVICE Device,
    _Inout_ PXINPUT_KEYSTROKE Keystroke);

/* audio.cpp */

typedef struct _XI_AUDIO_REQUEST
{
    LPWSTR RenderId;
    UINT* RenderCount;
    LPWSTR CaptureId;
    UINT* CaptureCount;
} XI_AUDIO_REQUEST;

HRESULT
XiGetAudioDevice(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context);
