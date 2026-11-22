/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What each request does to the controller it reaches
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xinput.hpp"

HRESULT
XiEnabledGetState(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    HRESULT hr;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    hr = XiReadState(Device);
    if (SUCCEEDED(hr))
        memcpy(Context, &Device->State, sizeof(Device->State));
    return hr;
}

/* A disabled controller reads as idle, one packet past the last one seen */
HRESULT
XiDisabledGetState(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XINPUT_STATE* State = static_cast<XINPUT_STATE*>(Context);

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    ZeroMemory(State, sizeof(*State));
    State->dwPacketNumber = Device->State.dwPacketNumber + 1;
    return S_OK;
}

/* No new speeds just sends the ones kept */
HRESULT
XiEnabledSetVibration(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    if (Context != nullptr)
        memcpy(&Device->Vibration, Context, sizeof(Device->Vibration));
    return XiSendVibration(Device);
}

/* While disabled new speeds are only kept, and the motors are stopped */
HRESULT
XiDisabledSetVibration(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XI_DEVICE Silent;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    if (Context != nullptr)
    {
        memcpy(&Device->Vibration, Context, sizeof(Device->Vibration));
        return S_OK;
    }

    Silent = *Device;
    Silent.Vibration.wLeftMotorSpeed = 0;
    Silent.Vibration.wRightMotorSpeed = 0;
    return XiSendVibration(&Silent);
}

HRESULT
XiEnabledGetSystemButtons(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    HRESULT hr;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    hr = XiReadState(Device);
    if (SUCCEEDED(hr))
        memcpy(Context, &Device->SystemButtons, sizeof(Device->SystemButtons));
    return hr;
}

HRESULT
XiDisabledGetSystemButtons(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    ZeroMemory(Context, sizeof(XI_SYSTEM_BUTTONS));
    return S_OK;
}

HRESULT
XiGetCapabilities(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    return XiQueryCapabilities(Device, static_cast<XI_CAPABILITIES_EX*>(Context));
}

HRESULT
XiGetBattery(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XI_BATTERY_REQUEST* Request = static_cast<XI_BATTERY_REQUEST*>(Context);

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    return XiQueryBattery(Device, Request->DeviceType, Request->Battery);
}

HRESULT
XiGetKeystroke(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XI_KEYSTROKE_REQUEST* Request = static_cast<XI_KEYSTROKE_REQUEST*>(Context);

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return E_FAIL;
    Request->Result = XiNextKeystroke(Device, Request->Keystroke);
    return S_OK;
}

/* Only interfaces answer this one */
HRESULT
XiGetBaseBus(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    if (!(Device->Flags & XI_DEVICE_CONNECTED) || !(Device->Flags & XI_DEVICE_BUS))
        return E_FAIL;
    return XiQueryBaseBus(Device, static_cast<XI_BASE_BUS_INFORMATION*>(Context));
}

HRESULT
XiPowerOff(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    if (!(Device->Flags & XI_DEVICE_CONNECTED) || Device->Version < 0x102)
        return E_FAIL;
    return XiSendPowerOff(Device);
}

typedef struct _XI_GUIDE_WAIT
{
    HANDLE Handle;
    HANDLE Event;
    UCHAR Index;
    XI_GUIDE_RESULT* Result;
} XI_GUIDE_WAIT;

static
DWORD
WINAPI
XiGuideWaitThread(
    _In_ LPVOID Parameter)
{
    XI_GUIDE_WAIT* Wait = static_cast<XI_GUIDE_WAIT*>(Parameter);

    if (SUCCEEDED(XiWaitForGuide(Wait->Handle, Wait->Index, Wait->Result)) && Wait->Event != nullptr)
        SetEvent(Wait->Event);
    HeapFree(GetProcessHeap(), 0, Wait);
    return 0;
}

/*
 * The wait runs on its own overlapped handle. With an event it runs on a thread and the call
 * returns at once; without one the lock is let go first and the call blocks, so S_FALSE tells
 * the request loop the lock is already gone.
 */
HRESULT
XiStartGuideWait(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    XI_GUIDE_REQUEST* Request = static_cast<XI_GUIDE_REQUEST*>(Context);
    XI_GUIDE_WAIT* Wait;
    HRESULT hr;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
    {
        Request->Result->Status = ERROR_DEVICE_NOT_CONNECTED;
        return E_FAIL;
    }
    if (Device->GuideWait == INVALID_HANDLE_VALUE)
    {
        Device->GuideWait = XiOpenDevice(Device->Path, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL);
        if (Device->GuideWait == INVALID_HANDLE_VALUE)
            return E_FAIL;
    }

    Wait = static_cast<XI_GUIDE_WAIT*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Wait)));
    if (Wait == nullptr)
    {
        Request->Result->Status = ERROR_OUTOFMEMORY;
        return E_OUTOFMEMORY;
    }
    Wait->Handle = Device->GuideWait;
    Wait->Event = Request->Event;
    Wait->Index = Device->Index;
    Wait->Result = Request->Result;

    if (Request->Event != nullptr)
    {
        if (CreateThread(nullptr, 0, XiGuideWaitThread, Wait, 0, nullptr) == nullptr)
        {
            HeapFree(GetProcessHeap(), 0, Wait);
            Request->Result->Status = GetLastError();
            return E_FAIL;
        }
        return S_OK;
    }

    hr = XiLeave();
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, Wait);
        return hr;
    }
    XiGuideWaitThread(Wait);
    return S_FALSE;
}

HRESULT
XiCancelGuideWait(
    _Inout_ PXI_DEVICE Device,
    _Inout_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);

    if (Device->GuideWait != INVALID_HANDLE_VALUE)
    {
        CloseHandle(Device->GuideWait);
        Device->GuideWait = INVALID_HANDLE_VALUE;
    }
    return S_OK;
}
