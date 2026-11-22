/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device information and capabilities of the system devices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dinput8check.hpp"

static const GUID* DcSystemDevices[] =
{
    &GUID_SysKeyboard, &GUID_SysMouse, &GUID_SysKeyboardEm, &GUID_SysKeyboardEm2, &GUID_SysMouseEm, &GUID_SysMouseEm2,
};

static
VOID
DcPrintCaps(
    _In_ const DIDEVCAPS* Caps)
{
    printf("  caps size %lu flags %08lX type %08lX axes %lu buttons %lu povs %lu period %lu res %lu fw %lu hw %lu ffdrv %lu\n",
           Caps->dwSize, Caps->dwFlags, Caps->dwDevType, Caps->dwAxes, Caps->dwButtons, Caps->dwPOVs,
           Caps->dwFFSamplePeriod, Caps->dwFFMinTimeResolution, Caps->dwFirmwareRevision, Caps->dwHardwareRevision,
           Caps->dwFFDriverVersion);
}

VOID
DcCheckDeviceInfo(VOID)
{
    IDirectInput8W* Wide = DcCreateWide();
    IDirectInput8A* Ansi = DcCreateAnsi();

    if (Wide == nullptr || Ansi == nullptr)
        return;
    for (const GUID* Guid : DcSystemDevices)
    {
        IDirectInputDevice8W* Device = nullptr;
        IDirectInputDevice8A* DeviceA = nullptr;

        printf("device %s\n", DcGuid(Guid).c_str());
        if (FAILED(Wide->CreateDevice(*Guid, &Device, nullptr)) || FAILED(Ansi->CreateDevice(*Guid, &DeviceA, nullptr)))
        {
            printf("  create failed\n");
            continue;
        }
        for (DWORD Size : { static_cast<DWORD>(sizeof(DIDEVCAPS)), static_cast<DWORD>(sizeof(DIDEVCAPS_DX3)), 0ul, 48ul })
        {
            DIDEVCAPS Caps;

            memset(&Caps, 0xCC, sizeof(Caps));
            Caps.dwSize = Size;
            printf("  getcaps %lu = %08lX\n", Size, Device->GetCapabilities(&Caps));
            DcPrintCaps(&Caps);
        }
        printf("  getcaps null = %08lX\n", Device->GetCapabilities(nullptr));
        for (DWORD Size : { static_cast<DWORD>(sizeof(DIDEVICEINSTANCEW)), static_cast<DWORD>(sizeof(DIDEVICEINSTANCE_DX3W)),
                            0ul, 1104ul })
        {
            DIDEVICEINSTANCEW Info;
            HRESULT Result;

            memset(&Info, 0xCC, sizeof(Info));
            Info.dwSize = Size;
            Result = Device->GetDeviceInfo(&Info);
            printf("  info %lu = %08lX type %08lX inst %s prod %s ff %s page %04X usage %04X\n", Size, Result,
                   Info.dwDevType, DcGuid(&Info.guidInstance).c_str(), DcGuid(&Info.guidProduct).c_str(),
                   DcGuid(&Info.guidFFDriver).c_str(), Info.wUsagePage, Info.wUsage);
            if (SUCCEEDED(Result))
            {
                printf("    name '%s' product '%s'\n", DcWide(Info.tszInstanceName).c_str(),
                       DcWide(Info.tszProductName).c_str());
            }
        }
        for (DWORD Size : { static_cast<DWORD>(sizeof(DIDEVICEINSTANCEA)), static_cast<DWORD>(sizeof(DIDEVICEINSTANCE_DX3A)),
                            static_cast<DWORD>(sizeof(DIDEVICEINSTANCEW)) })
        {
            DIDEVICEINSTANCEA Info;
            HRESULT Result;

            memset(&Info, 0xCC, sizeof(Info));
            Info.dwSize = Size;
            Result = DeviceA->GetDeviceInfo(&Info);
            printf("  infoa %lu = %08lX type %08lX inst %s\n", Size, Result, Info.dwDevType,
                   DcGuid(&Info.guidInstance).c_str());
            if (SUCCEEDED(Result))
            {
                printf("    name '%s' product '%s'\n", Info.tszInstanceName, Info.tszProductName);
                DcBytes("    tail", reinterpret_cast<BYTE*>(&Info) + 556, sizeof(Info) - 556);
            }
        }
        DeviceA->Release();
        Device->Release();
    }
    Ansi->Release();
    Wide->Release();
}
