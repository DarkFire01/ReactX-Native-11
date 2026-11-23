/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The adapter identity the display configuration hands the Direct3D 9 family of runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"
#include "adapteridentity.hpp"

/* The private display configuration packet and where its fields sit */
static constexpr DISPLAYCONFIG_DEVICE_INFO_TYPE RxIdentityPacketType =
    static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFE);
static constexpr UINT32 RxIdentityPacketSize = 0x808;
static constexpr SIZE_T RxIdentityVendorOffset = 0x344;
static constexpr SIZE_T RxIdentityDeviceOffset = 0x348;
static constexpr SIZE_T RxIdentitySubSysLowOffset = 0x34C;
static constexpr SIZE_T RxIdentitySubSysHighOffset = 0x350;
static constexpr SIZE_T RxIdentityRevisionOffset = 0x354;
static constexpr SIZE_T RxIdentityModelOffset = 0x358;
static constexpr SIZE_T RxIdentityDescriptionOffset = 0x35C;
static constexpr SIZE_T RxIdentityDeviceNameOffset = 0x67C;

static
DWORD
RxPacketValue(
    _In_reads_bytes_(RxIdentityPacketSize) const BYTE* Packet,
    _In_ SIZE_T Offset)
{
    DWORD Value;

    memcpy(&Value, Packet + Offset, sizeof(Value));
    return Value;
}

static
VOID
RxPacketString(
    _In_reads_bytes_(RxIdentityPacketSize) const BYTE* Packet,
    _In_ SIZE_T Offset,
    _Out_writes_z_(Count) WCHAR* Text,
    _In_ SIZE_T Count)
{
    memcpy(Text, Packet + Offset, Count * sizeof(WCHAR));
    Text[Count - 1] = UNICODE_NULL;
}

BOOLEAN
RxReadAdapterIdentity(
    _In_ const LUID& AdapterLuid,
    _In_ UINT32 SourceId,
    _Out_ RxAdapterIdentity* Identity)
{
    const RxDisplayConfigRoutines* Config = RxGetDisplayConfig();
    DISPLAYCONFIG_DEVICE_INFO_HEADER* Header;
    BYTE* Packet;
    BOOLEAN Read;

    ZeroMemory(Identity, sizeof(*Identity));
    Identity->DriverModel = RxDefaultDriverModel;
    if (Config == nullptr)
        return FALSE;

    Packet = new (std::nothrow) BYTE[RxIdentityPacketSize]();
    if (Packet == nullptr)
        return FALSE;

    Header = reinterpret_cast<DISPLAYCONFIG_DEVICE_INFO_HEADER*>(Packet);
    Header->type = RxIdentityPacketType;
    Header->size = RxIdentityPacketSize;
    Header->adapterId = AdapterLuid;
    Header->id = SourceId;

    Read = Config->GetDeviceInfo(Header) == ERROR_SUCCESS;
    if (Read)
    {
        Identity->VendorId = RxPacketValue(Packet, RxIdentityVendorOffset);
        Identity->DeviceId = RxPacketValue(Packet, RxIdentityDeviceOffset);
        Identity->SubSysId = RxPacketValue(Packet, RxIdentitySubSysLowOffset) |
                             (RxPacketValue(Packet, RxIdentitySubSysHighOffset) << 16);
        Identity->Revision = RxPacketValue(Packet, RxIdentityRevisionOffset);
        Identity->DriverModel = RxPacketValue(Packet, RxIdentityModelOffset);
        RxPacketString(Packet, RxIdentityDescriptionOffset, Identity->Description, _countof(Identity->Description));
        RxPacketString(Packet, RxIdentityDeviceNameOffset, Identity->DeviceName, _countof(Identity->DeviceName));
    }

    delete[] Packet;
    return Read;
}
