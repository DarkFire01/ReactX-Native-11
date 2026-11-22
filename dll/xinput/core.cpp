/*
 * PROJECT:     ReactX XInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The request lock, the port and interface tables and device enumeration
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "xinput.hpp"

#include <strsafe.h>

static const GUID XiInterfaceClass = { 0xEC87F1E3, 0xC13B, 0x4100, { 0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB } };

/* Which LED pattern each port shows, and which port each pattern means */
static const UCHAR XiPortLed[4] = { 6, 7, 8, 9 };
static const UCHAR XiLedPort[16] = { 255, 255, 0, 1, 2, 3, 0, 1, 2, 3, 255, 255, 255, 255, 255, 0 };

/* An ACPI enumerated controller reports the local machine as its container */
static const GUID XiLocalContainer = { 0x00000000, 0x0000, 0x0000, { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } };

PXI_REQUEST_ROUTINE XiGetStateRoutine;
PXI_REQUEST_ROUTINE XiSetVibrationRoutine;
PXI_REQUEST_ROUTINE XiGetSystemButtonsRoutine;
UINT XiSettings;

static CRITICAL_SECTION XiLock;
static BOOLEAN XiReady;
static BOOLEAN XiEnabled;

/* Controllers by port, which grows past the four users the API knows */
static PXI_DEVICE* XiPorts;
static UINT XiPortCount;

/* The interfaces, one per driver device */
static PXI_DEVICE* XiBuses;

/* The port an embedded controller takes over, so it answers as user 0 */
static UINT XiEmbeddedPort = XI_NONE;

/* Interface detail shared by enumeration passes */
static PSP_DEVICE_INTERFACE_DETAIL_DATA_W XiDetail;
static DWORD XiDetailSize;

static
PVOID
XiAllocate(
    _In_ SIZE_T Size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size);
}

static
VOID
XiFree(
    _In_opt_ PVOID Memory)
{
    if (Memory != nullptr)
        HeapFree(GetProcessHeap(), 0, Memory);
}

/* The slot a port reads, with the embedded controller and port 0 trading places */
static
UINT
XiPortSlot(
    _In_ UINT Port)
{
    if (XiEmbeddedPort == XI_NONE)
        return Port;
    if (Port == 0)
        return XiEmbeddedPort;
    return (Port == XiEmbeddedPort) ? 0 : Port;
}

static
VOID
XiSetHandlers(
    _In_ BOOLEAN Enable);

BOOLEAN
XiInitialize(VOID)
{
    InitializeCriticalSection(&XiLock);
    XiSettings = XI_SETTING_ASSIGN_LEDS | XI_SETTING_STOP_VIBRATION;
    XiPortCount = 0;
    XiPorts = static_cast<PXI_DEVICE*>(XiAllocate(8 * sizeof(PXI_DEVICE)));
    if (XiPorts == nullptr)
    {
        XI_TRACE(L"no memory for the port table");
        return FALSE;
    }
    XiPortCount = 8;

    XiBuses = static_cast<PXI_DEVICE*>(XiAllocate(XI_BUS_COUNT * sizeof(PXI_DEVICE)));
    if (XiBuses == nullptr)
    {
        XI_TRACE(L"no memory for the interface table");
        XiPortCount = 0;
        XiFree(XiPorts);
        XiPorts = nullptr;
        return FALSE;
    }

    XiDetail = nullptr;
    XiDetailSize = 0;
    if (FAILED(XiLoadOverrides()))
    {
        XI_TRACE(L"app container without a usable XInputUap.dll, refusing to load");
        XiUnloadOverrides();
        return FALSE;
    }
    XiEnableCommunications(TRUE);
    XiReady = TRUE;
    return TRUE;
}

/* Every controller goes, after its LEDs and motors are put back as settings ask */
static
VOID
XiCloseDevices(VOID)
{
    if (XiPortCount == 0 || XiPorts == nullptr || XiBuses == nullptr)
        return;

    for (UINT Index = 0; Index < XI_BUS_COUNT; Index++)
    {
        XiDestroyDevice(XiBuses[Index]);
        XiBuses[Index] = nullptr;
    }

    for (UINT Index = 0; Index < XiPortCount; Index++)
    {
        PXI_DEVICE Device = XiPorts[Index];

        if (Device == nullptr)
            continue;
        if (XiSettings & XI_SETTING_CLEAR_LEDS)
            XiSendLedState(Device, 0);
        if (XiSettings & XI_SETTING_STOP_VIBRATION)
        {
            Device->Vibration.wLeftMotorSpeed = 0;
            Device->Vibration.wRightMotorSpeed = 0;
            XiSendVibration(Device);
        }
        XiDestroyDevice(Device);
        XiPorts[Index] = nullptr;
    }

    XiFree(XiPorts);
    XiFree(XiBuses);
    XiPorts = nullptr;
    XiBuses = nullptr;
    XiPortCount = 0;
}

VOID
XiClose(
    _In_ BOOLEAN ProcessExit)
{
    UNREFERENCED_PARAMETER(ProcessExit);

    XiReady = FALSE;
    XiEnableCommunications(FALSE);
    XiCloseDevices();
    XiFree(XiDetail);
    XiDetail = nullptr;
    XiDetailSize = 0;
    XiUnloadOverrides();
    DeleteCriticalSection(&XiLock);
}

static
VOID
XiSetHandlers(
    _In_ BOOLEAN Enable)
{
    XiGetStateRoutine = Enable ? XiEnabledGetState : XiDisabledGetState;
    XiSetVibrationRoutine = Enable ? XiEnabledSetVibration : XiDisabledSetVibration;
    XiGetSystemButtonsRoutine = Enable ? XiEnabledGetSystemButtons : XiDisabledGetSystemButtons;
}

/* Disabling silences the motors and freezes the state reported; enabling brings the motors back */
VOID
XiEnableCommunications(
    _In_ BOOLEAN Enable)
{
    if (XiEnabled == Enable && XiGetStateRoutine != nullptr)
        return;
    if (XiGetStateRoutine != nullptr)
        XI_TRACE(L"communications %s", Enable ? L"enabled" : L"disabled, motors stopped and state frozen");
    XiEnabled = Enable;
    XiSetHandlers(Enable);

    for (UINT Port = 0; Port < XI_USER_COUNT; Port++)
    {
        PXI_DEVICE Device = nullptr;

        if (SUCCEEDED(XiGetDeviceOnPort(Port, &Device, 0)) && Device != nullptr)
            XiSetVibrationRoutine(Device, nullptr);
    }
}

HRESULT
XiLeave(VOID)
{
    if (!XiReady)
        return E_FAIL;
    LeaveCriticalSection(&XiLock);
    return S_OK;
}

DWORD
XiReturnCode(
    _In_ HRESULT Result)
{
    if (FAILED(Result))
    {
        switch (Result)
        {
            case E_NOTIMPL:
                return ERROR_CALL_NOT_IMPLEMENTED;
            case E_OUTOFMEMORY:
                return ERROR_OUTOFMEMORY;
            case E_INVALIDARG:
                return ERROR_BAD_ARGUMENTS;
            default:
                return (DWORD)Result;
        }
    }
    return (Result == S_FALSE) ? ERROR_DEVICE_NOT_CONNECTED : ERROR_SUCCESS;
}

static
HRESULT
XiSetDeviceOnPort(
    _In_ UINT Port,
    _In_ PXI_DEVICE Device)
{
    if (Port >= XiPortCount)
    {
        PXI_DEVICE* Grown = static_cast<PXI_DEVICE*>(XiAllocate((Port + 1) * sizeof(PXI_DEVICE)));

        if (Grown == nullptr)
            return E_OUTOFMEMORY;
        memcpy(Grown, XiPorts, XiPortCount * sizeof(PXI_DEVICE));
        XiFree(XiPorts);
        XiPorts = Grown;
        XiPortCount = Port + 1;
    }
    if (XiPorts[Port] != nullptr)
        return E_FAIL;

    if ((Device->Flags & XI_DEVICE_EMBEDDED) && XiEmbeddedPort == XI_NONE)
        XiEmbeddedPort = Port;
    XiPorts[Port] = Device;
    return S_OK;
}

/* Drops an interface; the controllers on it stay in their ports, marked gone */
static
VOID
XiRemoveBus(
    _In_ UINT Bus)
{
    if (Bus >= XI_BUS_COUNT || XiBuses[Bus] == nullptr)
        return;

    XI_TRACE(L"interface %u removed", Bus);
    for (UINT Index = 0; Index < XiPortCount; Index++)
    {
        PXI_DEVICE Device = XiPorts[Index];

        if (Device != nullptr && Device->Bus == Bus)
        {
            Device->Bus = XI_NONE;
            Device->Flags &= ~XI_DEVICE_CONNECTED;
        }
    }
    XiDestroyDevice(XiBuses[Bus]);
    XiBuses[Bus] = nullptr;
}

static
VOID
XiRemovePort(
    _In_ UINT Port)
{
    UINT Slot;
    PXI_DEVICE Device;

    if (Port >= XiPortCount)
        return;
    Slot = XiPortSlot(Port);
    Device = XiPorts[Slot];
    if (Device == nullptr)
        return;
    XI_TRACE(L"port %u: controller %u of %s gone", Port, Device->Index, Device->Path);

    /* The embedded controller leaving hands port 0 to another embedded one, if any */
    if (Slot == XiEmbeddedPort)
    {
        XiEmbeddedPort = XI_NONE;
        for (UINT Index = 0; Index < XiPortCount; Index++)
        {
            if (Index != Slot && XiPorts[Index] != nullptr && (XiPorts[Index]->Flags & XI_DEVICE_EMBEDDED))
            {
                XiEmbeddedPort = Index;
                break;
            }
        }
        XI_TRACE(L"embedded controller left; port %u now answers as user 0", XiEmbeddedPort);
    }
    XiDestroyDevice(Device);
    XiPorts[Slot] = nullptr;
}

/* A controller found on an interface takes the port its LEDs show, else the first free one */
static
HRESULT
XiPlaceDevice(
    _Inout_ PXI_DEVICE Device)
{
    UCHAR Led = 0;
    UINT Port;
    HRESULT hr;

    if (!(Device->Flags & XI_DEVICE_CONNECTED))
        return S_FALSE;
    if (Device->Flags & XI_DEVICE_BUS)
        return E_NOTIMPL;

    for (UINT Index = 0; Index < XiPortCount; Index++)
    {
        PXI_DEVICE Other = XiPorts[Index];

        if (Other != nullptr && Other->Index == Device->Index && Other->Path != nullptr && Device->Path != nullptr &&
            wcscmp(Other->Path, Device->Path) == 0)
        {
            return S_FALSE;
        }
    }

    Port = XI_NONE;
    if (Device->Version < 0x101 || (SUCCEEDED(XiQueryLedState(Device, &Led)) && Led < 15))
    {
        Port = XiLedPort[Led];
        if (Port != XI_NONE && Port < XiPortCount && XiPorts[Port] != nullptr)
        {
            XI_TRACE(L"controller %u LEDs (%u) point at port %u, which is taken", Device->Index, Led, Port);
            Port = XI_NONE;
            XiSendLedState(Device, 13);
        }
    }

    if (Device->Flags & XI_DEVICE_EMBEDDED)
        Port = XI_NONE;
    for (UINT Index = 0; Index < XI_USER_COUNT && Port == XI_NONE; Index++)
    {
        if (Index == 0 && (Device->Flags & XI_DEVICE_EMBEDDED))
            continue;
        if (Index >= XiPortCount || XiPorts[Index] == nullptr)
            Port = Index;
    }
    if (Port >= XI_USER_COUNT)
    {
        XI_TRACE(L"controller %u of %s found no free port", Device->Index, Device->Path);
        return S_FALSE;
    }

    hr = XiSetDeviceOnPort(Port, Device);
    if (FAILED(hr))
    {
        XI_TRACE(L"port %u refused controller %u (0x%08lX)", Port, Device->Index, hr);
        return hr;
    }
    XI_TRACE(L"port %u: controller %u of %s, VID %04X PID %04X, protocol %X%s", Port, Device->Index, Device->Path,
             Device->VendorId, Device->ProductId, Device->Version,
             (Device->Flags & XI_DEVICE_EMBEDDED) ? L", embedded" : L"");
    if ((XiSettings & XI_SETTING_ASSIGN_LEDS) && !(Device->Flags & XI_DEVICE_EMBEDDED))
        XiSendLedState(Device, XiPortLed[Port & 3]);
    return S_OK;
}

/* The interface itself goes in the bus table, then every controller it carries gets a port */
static
HRESULT
XiEnumerateInterface(
    _In_ HANDLE Handle,
    _In_z_ PCWSTR Path,
    _In_ HDEVINFO List,
    _In_ PSP_DEVINFO_DATA DeviceData)
{
    XI_INTERFACE_INFORMATION Information = {};
    PXI_DEVICE Bus;
    PXI_DEVICE Device = nullptr;
    UINT Slot = 0;
    UINT Added = XI_NONE;
    HRESULT hr;

    hr = XiQueryInformation(Handle, &Information);
    if (FAILED(hr))
    {
        XI_TRACE(L"%s does not answer the information request (0x%08lX)", Path, hr);
        goto Exit;
    }
    XI_TRACE(L"interface %s: protocol %X, %u controllers, VID %04X PID %04X", Path, Information.Version,
             Information.DeviceCount, Information.VendorId, Information.ProductId);
    if (Information.DeviceCount == 0)
        goto Exit;

    Bus = XiCreateDevice(Handle, Path, List, DeviceData);
    if (Bus == nullptr)
    {
        hr = E_OUTOFMEMORY;
        goto Exit;
    }
    Bus->Flags |= XI_DEVICE_CONNECTED | XI_DEVICE_BUS;
    Bus->VendorId = Information.VendorId;
    Bus->ProductId = Information.ProductId;
    Bus->Version = Information.Version;
    if (FAILED(XiQueryShareButton(Handle, &Bus->ShareButton)))
        Bus->ShareButton = FALSE;

    for (Slot = 0; Slot < XI_BUS_COUNT; Slot++)
    {
        PXI_DEVICE Known = XiBuses[Slot];

        if (Known != nullptr && Known->Path != nullptr && Bus->Path != nullptr && wcscmp(Known->Path, Bus->Path) == 0)
            break;
    }
    if (Slot < XI_BUS_COUNT)
    {
        XiDestroyDevice(Bus);
        hr = S_OK;
    }
    else
    {
        for (Slot = 0; Slot < XI_BUS_COUNT && XiBuses[Slot] != nullptr; Slot++);
        if (Slot == XI_BUS_COUNT)
        {
            XI_TRACE(L"all %u interface slots are taken, ignoring %s", XI_BUS_COUNT, Path);
            XiDestroyDevice(Bus);
            hr = E_FAIL;
            goto Exit;
        }
        XiBuses[Slot] = Bus;
        Added = Slot;
        hr = S_OK;
    }

    for (UCHAR Index = 0; Index < Information.DeviceCount; Index++)
    {
        /* A controller that found no port is reused for the next index */
        if (Device != nullptr)
        {
            HANDLE Kept = Device->Device;
            PWSTR KeptPath = Device->Path;

            ZeroMemory(Device, sizeof(*Device));
            Device->Device = Kept;
            Device->GuideWait = INVALID_HANDLE_VALUE;
            Device->Path = KeptPath;
            Device->Bus = XI_NONE;
        }
        else
        {
            Device = XiCreateDevice(Handle, Path, List, DeviceData);
            if (Device == nullptr)
            {
                hr = E_OUTOFMEMORY;
                goto Exit;
            }
        }

        Device->Index = Index;
        Device->VendorId = Information.VendorId;
        Device->ProductId = Information.ProductId;
        Device->Bus = Slot;
        Device->Version = Information.Version;
        if (FAILED(XiQueryShareButton(Handle, &Device->ShareButton)))
            Device->ShareButton = FALSE;

        hr = XiReadState(Device);
        if (SUCCEEDED(hr))
        {
            hr = XiPlaceDevice(Device);
            if (hr == S_OK)
                Device = nullptr;
        }
    }
    XiDestroyDevice(Device);

Exit:
    if (Added != XI_NONE && FAILED(hr))
        XiRemoveBus(Added);
    return hr;
}

/* Fetches an interface's path into the shared detail buffer, growing it once if it is short */
static
BOOLEAN
XiFetchDetail(
    _In_ HDEVINFO List,
    _In_ PSP_DEVICE_INTERFACE_DATA Interface,
    _Inout_ PSP_DEVICE_INTERFACE_DETAIL_DATA_W* Detail,
    _Inout_ DWORD* DetailSize)
{
    DWORD Required = *DetailSize;

    if (*Detail != nullptr)
    {
        ZeroMemory(*Detail, *DetailSize);
        (*Detail)->cbSize = sizeof(**Detail);
    }
    if (SetupDiGetDeviceInterfaceDetailW(List, Interface, *Detail, *DetailSize, &Required, nullptr))
        return TRUE;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;

    XiFree(*Detail);
    *Detail = nullptr;
    *DetailSize = 0;
    *Detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(XiAllocate(Required ? Required : 1));
    if (*Detail == nullptr)
        return FALSE;
    (*Detail)->cbSize = sizeof(**Detail);
    *DetailSize = Required;
    return SetupDiGetDeviceInterfaceDetailW(List, Interface, *Detail, *DetailSize, nullptr, nullptr) != FALSE;
}

static
HRESULT
XiEnumerateDevices(VOID)
{
    HDEVINFO List;

    List = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (List == INVALID_HANDLE_VALUE || List == nullptr)
    {
        XI_TRACE(L"SetupDiCreateDeviceInfoList failed (%lu)", GetLastError());
        return E_FAIL;
    }
    if (SetupDiGetClassDevsExW(&XiInterfaceClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, List, nullptr,
                               nullptr) == INVALID_HANDLE_VALUE)
    {
        XI_TRACE(L"no controller interfaces could be listed (%lu)", GetLastError());
        SetupDiDestroyDeviceInfoList(List);
        return E_FAIL;
    }

    for (DWORD Index = 0;; Index++)
    {
        SP_DEVICE_INTERFACE_DATA Interface = {};
        SP_DEVINFO_DATA DeviceData = {};
        HANDLE Handle;

        Interface.cbSize = sizeof(Interface);
        if (!SetupDiEnumDeviceInterfaces(List, nullptr, &XiInterfaceClass, Index, &Interface))
            break;
        if (!XiFetchDetail(List, &Interface, &XiDetail, &XiDetailSize))
        {
            XI_TRACE(L"no path for controller interface %lu (%lu)", Index, GetLastError());
            continue;
        }

        Handle = XiOpenDevice(XiDetail->DevicePath, FILE_ATTRIBUTE_NORMAL);
        if (Handle == INVALID_HANDLE_VALUE)
        {
            XI_TRACE(L"cannot open %s (%lu)", XiDetail->DevicePath, GetLastError());
            continue;
        }

        /* The device that goes with the interface is looked up by the same index */
        DeviceData.cbSize = sizeof(DeviceData);
        if (SetupDiEnumDeviceInfo(List, Index, &DeviceData))
            XiEnumerateInterface(Handle, XiDetail->DevicePath, List, &DeviceData);
        else
            XI_TRACE(L"no device for interface %lu, skipping it (%lu)", Index, GetLastError());
        CloseHandle(Handle);
    }

    SetupDiDestroyDeviceInfoList(List);
    return S_OK;
}

HRESULT
XiGetDeviceOnPort(
    _In_ UINT Port,
    _Out_ PXI_DEVICE* Device,
    _In_ UCHAR Retries)
{
    *Device = (Port < XiPortCount) ? XiPorts[XiPortSlot(Port)] : nullptr;

    /* An empty port is looked for again as many times as asked */
    while (*Device == nullptr && Retries != 0)
    {
        HRESULT hr = XiEnumerateDevices();

        if (FAILED(hr))
            return hr;
        *Device = (Port < XiPortCount) ? XiPorts[XiPortSlot(Port)] : nullptr;
        Retries -= 1;
    }
    return S_OK;
}

/*
 * Runs one request under the lock. A controller that fails or goes missing is dropped and
 * looked for once more; S_FALSE means none answered. A routine returning S_FALSE has left the lock.
 */
HRESULT
XiProcessRequest(
    _In_ UINT Port,
    _In_ PXI_REQUEST_ROUTINE Routine,
    _Inout_opt_ PVOID Context,
    _In_ BOOLEAN ByBus)
{
    BOOLEAN Retried = FALSE;
    HRESULT hr;

    if (!XiReady)
        return E_FAIL;

    EnterCriticalSection(&XiLock);
    for (;;)
    {
        PXI_DEVICE Device = nullptr;

        if (ByBus)
        {
            Device = (Port < XI_BUS_COUNT) ? XiBuses[Port] : nullptr;
        }
        else
        {
            hr = XiGetDeviceOnPort(Port, &Device, XiEnabled ? 1 : 0);
            if (FAILED(hr))
                break;
        }
        if (Device == nullptr)
        {
            hr = S_FALSE;
            break;
        }

        hr = Routine(Device, Context);
        if (hr == S_FALSE)
            return S_OK;
        if (FAILED(hr))
        {
            XI_TRACE(L"%s %u failed a request (0x%08lX), dropping it", ByBus ? L"interface" : L"port", Port, hr);
            Device->Flags &= ~XI_DEVICE_CONNECTED;
            if (!ByBus)
                XiRemoveBus(Device->Bus);
        }
        if (Device->Flags & XI_DEVICE_CONNECTED)
            break;

        if (ByBus)
        {
            XiRemoveBus(Port);
        }
        else
        {
            XiRemovePort(Port);
            hr = S_OK;
        }
        if (Retried)
        {
            hr = S_FALSE;
            break;
        }
        XI_TRACE(L"%s %u disconnected, looking for it once more", ByBus ? L"interface" : L"port", Port);
        Retried = TRUE;
    }

    XiLeave();
    return hr;
}

PXI_DEVICE
XiCreateDevice(
    _In_ HANDLE Handle,
    _In_z_ PCWSTR Path,
    _In_opt_ HDEVINFO List,
    _In_opt_ PSP_DEVINFO_DATA DeviceData)
{
    PXI_DEVICE Device = static_cast<PXI_DEVICE>(XiAllocate(sizeof(*Device)));
    HANDLE Process = GetCurrentProcess();

    if (Device == nullptr)
        return nullptr;
    Device->Device = INVALID_HANDLE_VALUE;
    Device->GuideWait = INVALID_HANDLE_VALUE;
    Device->Bus = XI_NONE;
    if (!DuplicateHandle(Process, Handle, Process, &Device->Device, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        XI_TRACE(L"cannot duplicate the handle of %s (%lu)", Path, GetLastError());
        goto Fail;
    }

    Device->PathLength = wcslen(Path) + 1;
    Device->Path = static_cast<PWSTR>(XiAllocate(Device->PathLength * sizeof(WCHAR)));
    if (Device->Path == nullptr || FAILED(StringCchCopyW(Device->Path, Device->PathLength, Path)))
        goto Fail;

    if (XiIsEmbedded(List, DeviceData))
        Device->Flags |= XI_DEVICE_EMBEDDED;
    return Device;

Fail:
    XiFree(Device->Path);
    if (Device->Device != INVALID_HANDLE_VALUE)
        CloseHandle(Device->Device);
    XiFree(Device);
    return nullptr;
}

VOID
XiDestroyDevice(
    _In_opt_ PXI_DEVICE Device)
{
    if (Device == nullptr)
        return;
    if (Device->GuideWait != INVALID_HANDLE_VALUE)
        CloseHandle(Device->GuideWait);
    if (Device->Device != INVALID_HANDLE_VALUE)
        CloseHandle(Device->Device);
    XiFree(Device->Path);
    XiFree(Device);
}

/* Just enough of a controller to address the interface it was opened on directly */
BOOLEAN
XiFillFromInterface(
    _In_ HANDLE Handle,
    _Out_ PXI_DEVICE Device)
{
    XI_INTERFACE_INFORMATION Information = {};

    ZeroMemory(Device, sizeof(*Device));
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;
    if (FAILED(XiQueryInformation(Handle, &Information)) || (Information.Flags & 0x80))
        return FALSE;

    Device->Flags = XI_DEVICE_CONNECTED;
    Device->Device = Handle;
    Device->GuideWait = INVALID_HANDLE_VALUE;
    Device->Version = Information.Version;
    Device->VendorId = Information.VendorId;
    Device->ProductId = Information.ProductId;
    return TRUE;
}

HANDLE
XiOpenDevice(
    _In_z_ PCWSTR Path,
    _In_ DWORD Attributes)
{
    return CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       Attributes, nullptr);
}

/* A property of a device, in a buffer of its own; nullptr when it has none */
PVOID
XiDeviceProperty(
    _In_ HDEVINFO List,
    _In_ PSP_DEVINFO_DATA DeviceData,
    _In_ const DEVPROPKEY* Key)
{
    DEVPROPTYPE Type = 0;
    DWORD Size = 0;
    PVOID Value;

    SetupDiGetDevicePropertyW(List, DeviceData, Key, &Type, nullptr, 0, &Size, 0);
    Value = XiAllocate(Size ? Size : 1);
    if (Value == nullptr)
        return nullptr;
    if (SetupDiGetDevicePropertyW(List, DeviceData, Key, &Type, static_cast<PBYTE>(Value), Size, nullptr, 0))
        return Value;
    XiFree(Value);
    return nullptr;
}

VOID
XiFreeProperty(
    _In_opt_ PVOID Value)
{
    XiFree(Value);
}

/* Built in controllers sit in the machine's own container or name themselves in their hardware ID */
BOOLEAN
XiIsEmbedded(
    _In_opt_ HDEVINFO List,
    _In_opt_ PSP_DEVINFO_DATA DeviceData)
{
    PVOID Value;
    BOOLEAN Embedded;

    if (List == nullptr || DeviceData == nullptr)
        return FALSE;

    Value = XiDeviceProperty(List, DeviceData, &DEVPKEY_Device_ContainerId);
    if (Value != nullptr)
    {
        Embedded = memcmp(Value, &XiLocalContainer, sizeof(GUID)) == 0;
        XiFree(Value);
        if (Embedded)
            return TRUE;
    }

    Value = XiDeviceProperty(List, DeviceData, &DEVPKEY_Device_HardwareIds);
    if (Value == nullptr)
        return FALSE;
    Embedded = wcsstr(static_cast<PCWSTR>(Value), L"XINPUT_EMBEDDED_DEVICE") != nullptr;
    XiFree(Value);
    return Embedded;
}

XiInterfaceWalk::~XiInterfaceWalk()
{
    if (List != INVALID_HANDLE_VALUE)
        SetupDiDestroyDeviceInfoList(List);
    XiFree(Detail);
}

BOOLEAN
XiInterfaceWalk::Restart(VOID)
{
    if (List != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(List);
        List = INVALID_HANDLE_VALUE;
    }
    Index = 0;
    if (Detail == nullptr)
    {
        DetailSize = 264;
        Detail = static_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(XiAllocate(DetailSize));
        if (Detail == nullptr)
        {
            DetailSize = 0;
            return FALSE;
        }
    }

    List = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (List == INVALID_HANDLE_VALUE || List == nullptr)
    {
        List = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    return SetupDiGetClassDevsExW(&XiInterfaceClass, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE, List,
                                  nullptr, nullptr) != INVALID_HANDLE_VALUE;
}

BOOLEAN
XiInterfaceWalk::Next(
    _Out_ PHANDLE Handle)
{
    SP_DEVICE_INTERFACE_DATA Interface = {};

    *Handle = INVALID_HANDLE_VALUE;
    if (List == INVALID_HANDLE_VALUE || Detail == nullptr)
        return FALSE;

    Interface.cbSize = sizeof(Interface);
    if (!SetupDiEnumDeviceInterfaces(List, nullptr, &XiInterfaceClass, Index, &Interface))
        return FALSE;
    Index++;
    if (!XiFetchDetail(List, &Interface, &Detail, &DetailSize))
        return FALSE;

    *Handle = XiOpenDevice(Detail->DevicePath, FILE_ATTRIBUTE_NORMAL);
    return TRUE;
}
