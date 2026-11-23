/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The adapter identity the display configuration hands the Direct3D 9 family of runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* What the display configuration knows about the adapter behind a video present source */
struct RxAdapterIdentity
{
    DWORD VendorId;
    DWORD DeviceId;
    DWORD SubSysId;
    DWORD Revision;
    UINT DriverModel;
    WCHAR Description[400];
    WCHAR DeviceName[198];
};

/* Driver model reported when the identity cannot be read */
static constexpr UINT RxDefaultDriverModel = 1000;

/* Pass as the source to ask about the adapter without naming a display */
static constexpr UINT32 RxAnySource = 0xFFFFFFFF;

/* Zeroes Identity and fills it when the display configuration answers */
BOOLEAN
RxReadAdapterIdentity(
    _In_ const LUID& AdapterLuid,
    _In_ UINT32 SourceId,
    _Out_ RxAdapterIdentity* Identity);
