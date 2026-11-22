/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Requests to the controller driver and the layouts each protocol version answers with
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xinput.hpp"

#define XI_IOCTL_GET_INFORMATION    0x80006000
#define XI_IOCTL_GET_SHARE_BUTTON   0x80006380
#define XI_IOCTL_SET_STATE          0x8000A010
#define XI_IOCTL_POWER_OFF          0x8000A01C
#define XI_IOCTL_GET_CAPABILITIES   0x8000E004
#define XI_IOCTL_GET_LED_STATE      0x8000E008
#define XI_IOCTL_GET_STATE          0x8000E00C
#define XI_IOCTL_WAIT_FOR_GUIDE     0x8000E014
#define XI_IOCTL_GET_BATTERY        0x8000E018
#define XI_IOCTL_GET_AUDIO          0x8000E020
#define XI_IOCTL_GET_BASE_BUS       0x8000E3FC

/* Capabilities the first protocol cannot report, so every such controller claims them */
static const XINPUT_CAPABILITIES XiFirstVersionCapabilities =
{
    XINPUT_DEVTYPE_GAMEPAD, XINPUT_DEVSUBTYPE_GAMEPAD, XINPUT_CAPS_VOICE_SUPPORTED,
    { 0xF3FF, 1, 1, 1, 1, 1, 1 },
    { 1, 1 },
};

#pragma pack(push, 1)

/* Most requests name the protocol revision and the controller on the interface */
typedef struct _XI_REQUEST
{
    USHORT Version;
    UCHAR Index;
} XI_REQUEST;

typedef struct _XI_STATE_V0
{
    UCHAR Status;
    UCHAR Reserved;
    UCHAR AudioInterface;
    DWORD PacketNumber;
    UCHAR Reserved2;
    WORD Buttons;
    UCHAR LeftTrigger;
    UCHAR RightTrigger;
    SHORT ThumbLX;
    SHORT ThumbLY;
    SHORT ThumbRX;
    SHORT ThumbRY;
} XI_STATE_V0;

typedef struct _XI_STATE_V1
{
    USHORT Version;
    UCHAR Status;
    UCHAR Reserved;
    UCHAR AudioInterface;
    DWORD PacketNumber;
    UCHAR Reserved2[2];
    UCHAR Buttons[2];
    UCHAR LeftTrigger;
    UCHAR RightTrigger;
    UCHAR Thumbs[8];
    UCHAR Reserved3[5];
    UCHAR Extra;
} XI_STATE_V1;

typedef struct _XI_CAPS_V1
{
    USHORT Version;
    UCHAR Type;
    UCHAR SubType;
    WORD Buttons;
    UCHAR LeftTrigger;
    UCHAR RightTrigger;
    SHORT ThumbLX;
    SHORT ThumbLY;
    SHORT ThumbRX;
    SHORT ThumbRY;
    UCHAR Reserved[6];
    UCHAR LeftMotor;
    UCHAR RightMotor;
} XI_CAPS_V1;

typedef struct _XI_CAPS_V2
{
    USHORT Version;
    UCHAR Type;
    UCHAR SubType;
    WORD Flags;
    WORD VendorId;
    WORD ProductId;
    WORD Revision;
    DWORD SerialNumber;
    WORD Buttons;
    UCHAR LeftTrigger;
    UCHAR RightTrigger;
    SHORT ThumbLX;
    SHORT ThumbLY;
    SHORT ThumbRX;
    SHORT ThumbRY;
    UCHAR Reserved[6];
    UCHAR LeftMotor;
    UCHAR RightMotor;
} XI_CAPS_V2;

typedef struct _XI_SET_STATE
{
    UCHAR Index;
    UCHAR Led;
    UCHAR LeftMotor;
    UCHAR RightMotor;
    UCHAR Which;        /* 1 the LEDs, 2 the motors */
} XI_SET_STATE;

typedef struct _XI_BASE_BUS_REQUEST
{
    USHORT Version;
    UCHAR Type;
    UCHAR Reserved[33];
} XI_BASE_BUS_REQUEST;

typedef struct _XI_BASE_BUS_ANSWER
{
    UCHAR Reserved[2];
    UCHAR Error;
    UCHAR Reserved2[20];
    WORD VendorId;
    WORD ProductId;
    WORD InputReportType;
    UCHAR HasSerial;
    UCHAR Serial[4];
    UCHAR Interface[3];
    UCHAR Reserved3[912];
} XI_BASE_BUS_ANSWER;

#pragma pack(pop)

static_assert(sizeof(XI_STATE_V0) == 20, "first protocol state is 20 bytes");
static_assert(sizeof(XI_STATE_V1) == 29, "later protocol state is 29 bytes");
static_assert(sizeof(XI_CAPS_V1) == 24, "early capabilities are 24 bytes");
static_assert(sizeof(XI_CAPS_V2) == 36, "later capabilities are 36 bytes");
static_assert(sizeof(XI_BASE_BUS_ANSWER) == 949, "base bus answer is 949 bytes");

HRESULT
XiIoctl(
    _In_ HANDLE Handle,
    _In_ DWORD Code,
    _In_reads_bytes_opt_(InputSize) PVOID Input,
    _In_ DWORD InputSize,
    _Out_writes_bytes_opt_(OutputSize) PVOID Output,
    _In_ DWORD OutputSize,
    _Inout_opt_ LPOVERLAPPED Overlapped)
{
    DWORD Returned = 0;

    if (DeviceIoControl(Handle, Code, Input, InputSize, Output, OutputSize, &Returned, Overlapped))
        return S_OK;

    switch (GetLastError())
    {
        case ERROR_IO_PENDING:
            return (Overlapped != nullptr) ? E_PENDING : E_FAIL;
        case ERROR_BAD_COMMAND:
            return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
        default:
            return E_FAIL;
    }
}

HRESULT
XiQueryInformation(
    _In_ HANDLE Handle,
    _Out_ XI_INTERFACE_INFORMATION* Information)
{
    ZeroMemory(Information, sizeof(*Information));
    return XiIoctl(Handle, XI_IOCTL_GET_INFORMATION, nullptr, 0, Information, sizeof(*Information), nullptr);
}

/* The driver answers zero when the controller has a share button */
HRESULT
XiQueryShareButton(
    _In_ HANDLE Handle,
    _Out_ BOOL* Supported)
{
    DWORD Answer = 0;
    HRESULT hr;

    hr = XiIoctl(Handle, XI_IOCTL_GET_SHARE_BUTTON, nullptr, 0, &Answer, sizeof(Answer), nullptr);
    if (FAILED(hr))
    {
        *Supported = FALSE;
        return hr;
    }
    *Supported = (Answer == 0);
    return S_OK;
}

static
WORD
XiWord(
    _In_reads_(2) const UCHAR* Bytes)
{
    return (WORD)((Bytes[1] << 8) | Bytes[0]);
}

HRESULT
XiReadState(
    _Inout_ PXI_DEVICE Device)
{
    HRESULT hr;

    if (Device->Version == 0x100)
    {
        XI_STATE_V0 State = {};
        UCHAR Index = Device->Index;

        hr = XiIoctl(Device->Device, XI_IOCTL_GET_STATE, &Index, sizeof(Index), &State, sizeof(State), nullptr);
        if (FAILED(hr))
            return hr;

        Device->State.dwPacketNumber = State.PacketNumber;
        Device->State.Gamepad.wButtons = State.Buttons;
        Device->State.Gamepad.bLeftTrigger = State.LeftTrigger;
        Device->State.Gamepad.bRightTrigger = State.RightTrigger;
        Device->State.Gamepad.sThumbLX = State.ThumbLX;
        Device->State.Gamepad.sThumbLY = State.ThumbLY;
        Device->State.Gamepad.sThumbRX = State.ThumbRX;
        Device->State.Gamepad.sThumbRY = State.ThumbRY;
        if (State.Status == 1)
            Device->Flags |= XI_DEVICE_CONNECTED;
        else
            Device->Flags &= ~XI_DEVICE_CONNECTED;
        Device->AudioInterface = State.AudioInterface;
        return S_OK;
    }
    else
    {
        XI_STATE_V1 State = {};
        XI_REQUEST Request = { 0x101, Device->Index };

        hr = XiIoctl(Device->Device, XI_IOCTL_GET_STATE, &Request, sizeof(Request), &State, sizeof(State), nullptr);
        if (FAILED(hr))
            return hr;

        /* Bit 11 is not a button anyone may see */
        Device->State.dwPacketNumber = State.PacketNumber;
        Device->State.Gamepad.wButtons = XiWord(State.Buttons) & 0xF7FF;
        Device->State.Gamepad.bLeftTrigger = State.LeftTrigger;
        Device->State.Gamepad.bRightTrigger = State.RightTrigger;
        Device->State.Gamepad.sThumbLX = (SHORT)XiWord(&State.Thumbs[0]);
        Device->State.Gamepad.sThumbLY = (SHORT)XiWord(&State.Thumbs[2]);
        Device->State.Gamepad.sThumbRX = (SHORT)XiWord(&State.Thumbs[4]);
        Device->State.Gamepad.sThumbRY = (SHORT)XiWord(&State.Thumbs[6]);
        Device->SystemButtons.Guide = Device->State.Gamepad.wButtons & XI_GAMEPAD_GUIDE;
        if (Device->ShareButton)
            Device->SystemButtons.Share = State.Extra & 1;
        if (State.Status == 1)
            Device->Flags |= XI_DEVICE_CONNECTED;
        else
            Device->Flags &= ~XI_DEVICE_CONNECTED;
        Device->AudioInterface = State.AudioInterface;
        return S_OK;
    }
}

HRESULT
XiQueryCapabilities(
    _In_ PXI_DEVICE Device,
    _Out_ XI_CAPABILITIES_EX* Capabilities)
{
    HRESULT hr;

    if (Device->Version == 0x100)
    {
        Capabilities->Capabilities = XiFirstVersionCapabilities;
        Capabilities->VendorId = Device->VendorId;
        Capabilities->ProductId = Device->ProductId;
        Capabilities->Revision = 0;
        Capabilities->SerialNumber = 0;
        return S_OK;
    }

    if (Device->Version == 0x101 || Device->Version == 0x102)
    {
        XI_CAPS_V1 Answer = {};
        XI_REQUEST Request = { 0x101, Device->Index };
        XINPUT_CAPABILITIES* Out = &Capabilities->Capabilities;

        hr = XiIoctl(Device->Device, XI_IOCTL_GET_CAPABILITIES, &Request, sizeof(Request), &Answer, sizeof(Answer), nullptr);
        if (FAILED(hr))
            return hr;

        Out->Type = Answer.Type ? Answer.Type : XINPUT_DEVTYPE_GAMEPAD;
        Out->SubType = Answer.SubType;
        Out->Flags = XINPUT_CAPS_VOICE_SUPPORTED | XINPUT_CAPS_PMD_SUPPORTED;
        Out->Gamepad.wButtons = Answer.Buttons & 0xF7FF;
        Out->Gamepad.bLeftTrigger = Answer.LeftTrigger;
        Out->Gamepad.bRightTrigger = Answer.RightTrigger;
        Out->Gamepad.sThumbLX = Answer.ThumbLX;
        Out->Gamepad.sThumbLY = Answer.ThumbLY;
        Out->Gamepad.sThumbRX = Answer.ThumbRX;
        Out->Gamepad.sThumbRY = Answer.ThumbRY;
        Out->Vibration.wLeftMotorSpeed = Answer.LeftMotor;
        Out->Vibration.wRightMotorSpeed = Answer.RightMotor;

        /* The wireless receiver itself hides its IDs and says it has no navigation */
        if (Device->VendorId == 0x045E && Device->ProductId == 0x0719)
        {
            Capabilities->VendorId = 0;
            Capabilities->ProductId = 0;
            Out->Flags = XINPUT_CAPS_VOICE_SUPPORTED | XINPUT_CAPS_PMD_SUPPORTED | XINPUT_CAPS_WIRELESS;
        }
        else
        {
            Capabilities->VendorId = Device->VendorId;
            Capabilities->ProductId = Device->ProductId;
        }
        Capabilities->Revision = 0;
        Capabilities->SerialNumber = 0;
        return S_OK;
    }
    else
    {
        XI_CAPS_V2 Answer = {};
        XI_REQUEST Request = { 0x102, Device->Index };
        XINPUT_CAPABILITIES* Out = &Capabilities->Capabilities;

        hr = XiIoctl(Device->Device, XI_IOCTL_GET_CAPABILITIES, &Request, sizeof(Request), &Answer, sizeof(Answer), nullptr);
        if (FAILED(hr))
            return hr;

        Out->Type = Answer.Type;
        Out->SubType = Answer.SubType;
        Out->Flags = Answer.Flags & 0x1F;
        Out->Gamepad.wButtons = Answer.Buttons & 0xF7FF;
        Out->Gamepad.bLeftTrigger = Answer.LeftTrigger;
        Out->Gamepad.bRightTrigger = Answer.RightTrigger;
        Out->Gamepad.sThumbLX = Answer.ThumbLX;
        Out->Gamepad.sThumbLY = Answer.ThumbLY;
        Out->Gamepad.sThumbRX = Answer.ThumbRX;
        Out->Gamepad.sThumbRY = Answer.ThumbRY;
        Out->Vibration.wLeftMotorSpeed = Answer.LeftMotor;
        Out->Vibration.wRightMotorSpeed = Answer.RightMotor;
        Capabilities->VendorId = Answer.VendorId;
        Capabilities->ProductId = Answer.ProductId;
        Capabilities->Revision = Answer.Revision;
        Capabilities->SerialNumber = Answer.SerialNumber;
        return S_OK;
    }
}

/* Older protocols cannot report batteries: a wired controller, full, and a headset only if it has audio */
HRESULT
XiQueryBattery(
    _In_ PXI_DEVICE Device,
    _In_ BYTE DeviceType,
    _Out_ XINPUT_BATTERY_INFORMATION* Battery)
{
    XINPUT_BATTERY_INFORMATION Result;

    if (Device->Version >= 0x102)
    {
#pragma pack(push, 1)
        struct
        {
            USHORT Version;
            UCHAR Index;
            UCHAR DeviceType;
        } Request = { 0x102, Device->Index, DeviceType };
#pragma pack(pop)
        BYTE Answer[4] = {};
        HRESULT hr;

        hr = XiIoctl(Device->Device, XI_IOCTL_GET_BATTERY, &Request, sizeof(Request), Answer, sizeof(Answer), nullptr);
        if (FAILED(hr))
            return hr;
        Result.BatteryType = Answer[2];
        Result.BatteryLevel = Answer[3];
    }
    else
    {
        Result.BatteryType = BATTERY_TYPE_WIRED;
        Result.BatteryLevel = BATTERY_LEVEL_FULL;
        if (DeviceType == BATTERY_DEVTYPE_HEADSET)
        {
            UINT RenderCount = 0;
            XI_AUDIO_REQUEST Audio = { nullptr, &RenderCount, nullptr, nullptr };

            if (FAILED(XiGetAudioDevice(Device, &Audio)) || RenderCount == 0)
            {
                Result.BatteryType = BATTERY_TYPE_DISCONNECTED;
                Result.BatteryLevel = BATTERY_LEVEL_EMPTY;
            }
        }
    }

    *Battery = Result;
    return S_OK;
}

HRESULT
XiQueryAudioInformation(
    _In_ PXI_DEVICE Device,
    _Out_ PUSHORT VendorId,
    _Out_ PUSHORT ProductId,
    _Out_ PUCHAR Interface)
{
    XI_REQUEST Request = { 0x102, Device->Index };
    BYTE Answer[7] = {};
    HRESULT hr;

    if (Device->Version < 0x102)
        return E_FAIL;

    hr = XiIoctl(Device->Device, XI_IOCTL_GET_AUDIO, &Request, sizeof(Request), Answer, sizeof(Answer), nullptr);
    if (FAILED(hr))
        return hr;
    *VendorId = XiWord(&Answer[2]);
    *ProductId = XiWord(&Answer[4]);
    *Interface = Answer[6];
    return S_OK;
}

HRESULT
XiQueryBaseBus(
    _In_ PXI_DEVICE Device,
    _Out_ XI_BASE_BUS_INFORMATION* Information)
{
    XI_BASE_BUS_REQUEST Request = {};
    XI_BASE_BUS_ANSWER* Answer;
    HRESULT hr;

    ZeroMemory(Information, sizeof(*Information));
    Information->VendorId = Device->VendorId;
    Information->ProductId = Device->ProductId;
    if (Device->Version < 0x103)
        return S_OK;

    Answer = static_cast<XI_BASE_BUS_ANSWER*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Answer)));
    if (Answer == nullptr)
        return E_OUTOFMEMORY;

    Request.Version = 0x104;
    Request.Type = 2;
    hr = XiIoctl(Device->Device, XI_IOCTL_GET_BASE_BUS, &Request, sizeof(Request), Answer, sizeof(*Answer), nullptr);
    if (SUCCEEDED(hr))
    {
        if (Answer->Error != 0)
        {
            hr = E_NOTIMPL;
        }
        else
        {
            Information->VendorId = Answer->VendorId;
            Information->ProductId = Answer->ProductId;
            Information->InputReportType = Answer->InputReportType;
            if (Answer->HasSerial)
            {
                Information->SerialNumber = ((DWORD)Answer->Serial[0] << 24) | ((DWORD)Answer->Serial[1] << 16) |
                                            ((DWORD)Answer->Serial[2] << 8) | Answer->Serial[3];
                memcpy(Information->Interface, Answer->Interface, sizeof(Information->Interface));
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, Answer);
    return hr;
}

HRESULT
XiQueryLedState(
    _In_ PXI_DEVICE Device,
    _Out_ PUCHAR State)
{
    XI_REQUEST Request = { 0x101, Device->Index };
    BYTE Answer[3] = {};
    HRESULT hr;

    hr = XiIoctl(Device->Device, XI_IOCTL_GET_LED_STATE, &Request, sizeof(Request), Answer, sizeof(Answer), nullptr);
    if (SUCCEEDED(hr))
        *State = Answer[1];
    return hr;
}

/* The motors take the high byte of each speed */
HRESULT
XiSendVibration(
    _In_ PXI_DEVICE Device)
{
    XI_SET_STATE Request = {};

    Request.Index = Device->Index;
    Request.LeftMotor = HIBYTE(Device->Vibration.wLeftMotorSpeed);
    Request.RightMotor = HIBYTE(Device->Vibration.wRightMotorSpeed);
    Request.Which = 2;
    return XiIoctl(Device->Device, XI_IOCTL_SET_STATE, &Request, sizeof(Request), nullptr, 0, nullptr);
}

HRESULT
XiSendLedState(
    _In_ PXI_DEVICE Device,
    _In_ UCHAR State)
{
    XI_SET_STATE Request = {};

    Request.Index = Device->Index;
    Request.Led = State;
    Request.Which = 1;
    return XiIoctl(Device->Device, XI_IOCTL_SET_STATE, &Request, sizeof(Request), nullptr, 0, nullptr);
}

HRESULT
XiSendPowerOff(
    _In_ PXI_DEVICE Device)
{
    XI_REQUEST Request = { 0x102, Device->Index };

    return XiIoctl(Device->Device, XI_IOCTL_POWER_OFF, &Request, sizeof(Request), nullptr, 0, nullptr);
}

/* Blocks until the guide button is pressed; Result carries the error and the state at that moment */
HRESULT
XiWaitForGuide(
    _In_ HANDLE Handle,
    _In_ UCHAR Index,
    _Out_ XI_GUIDE_RESULT* Result)
{
    XI_REQUEST Request = { 0x102, Index };
    XI_STATE_V1 State = {};
    OVERLAPPED Overlapped = {};
    DWORD Returned = 0;
    HRESULT hr;

    Overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (Overlapped.hEvent == nullptr)
        return E_OUTOFMEMORY;

    State.Status = 5;
    hr = XiIoctl(Handle, XI_IOCTL_WAIT_FOR_GUIDE, &Request, sizeof(Request), &State, sizeof(State), &Overlapped);
    if (hr != E_PENDING && FAILED(hr))
    {
        CloseHandle(Overlapped.hEvent);
        Result->Status = GetLastError();
        return hr;
    }

    Result->Status = ERROR_IO_PENDING;
    if (!GetOverlappedResult(Handle, &Overlapped, &Returned, TRUE))
    {
        DWORD Error = GetLastError();

        Result->Status = (Error == ERROR_OPERATION_ABORTED) ? ERROR_CANCELLED : GetLastError();
    }
    else if (Returned != sizeof(State))
    {
        Result->Status = ERROR_CANCELLED;
    }
    else if (State.Status != 1)
    {
        Result->Status = ERROR_DEVICE_NOT_CONNECTED;
    }
    else
    {
        Result->Status = ERROR_SUCCESS;
        Result->State.dwPacketNumber = State.PacketNumber;
        Result->State.Gamepad.bLeftTrigger = State.LeftTrigger;
        Result->State.Gamepad.bRightTrigger = State.RightTrigger;
        Result->State.Gamepad.sThumbLX = (SHORT)XiWord(&State.Thumbs[0]);
        Result->State.Gamepad.sThumbLY = (SHORT)XiWord(&State.Thumbs[2]);
        Result->State.Gamepad.sThumbRX = (SHORT)XiWord(&State.Thumbs[4]);
        Result->State.Gamepad.sThumbRY = (SHORT)XiWord(&State.Thumbs[6]);
        Result->State.Gamepad.wButtons = XiWord(State.Buttons);
    }
    CloseHandle(Overlapped.hEvent);
    return S_OK;
}
