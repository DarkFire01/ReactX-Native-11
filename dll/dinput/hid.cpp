/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HID game controllers, their list and the joystick configuration
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "hid.hpp"

VOID
DiRefreshHidList(
    _In_ BOOLEAN Force)
{
    UNREFERENCED_PARAMETER(Force);
}

HRESULT
DiSnapshotHidList(
    _Outptr_result_maybenull_ RX_DI_HID_SNAPSHOT** Snapshot)
{
    *Snapshot = nullptr;
    return S_OK;
}

HRESULT
DiFindHidByPath(
    _In_z_ PCWSTR Path,
    _Out_ LPGUID Instance)
{
    UNREFERENCED_PARAMETER(Path);
    UNREFERENCED_PARAMETER(Instance);
    return DIERR_DEVICENOTREG;
}

HRESULT
DiFindHidBackend(
    _In_ REFGUID Instance,
    _Out_ PRX_DI_BACKEND_CREATE* Create)
{
    UNREFERENCED_PARAMETER(Instance);
    *Create = nullptr;
    return DIERR_DEVICENOTREG;
}

ULONG
DiHidMouseAxes(VOID)
{
    return 2;
}

HRESULT
DiCreateJoyConfig(
    _In_ IUnknown* OuterUnknown,
    _Outptr_ IUnknown** Inner)
{
    UNREFERENCED_PARAMETER(OuterUnknown);
    *Inner = nullptr;
    return E_NOINTERFACE;
}
