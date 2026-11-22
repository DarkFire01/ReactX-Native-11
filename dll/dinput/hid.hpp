/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HID game controllers, their list and the joystick configuration
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dinput.hpp"

/* What enumeration needs to know about one HID device */
typedef struct _RX_DI_HID_SUMMARY
{
    GUID Instance;
    UCHAR DevType;
} RX_DI_HID_SUMMARY;

typedef struct _RX_DI_HID_SNAPSHOT
{
    ULONG Count;
    RX_DI_HID_SUMMARY Entries[ANYSIZE_ARRAY];
} RX_DI_HID_SNAPSHOT;

VOID
DiRefreshHidList(
    _In_ BOOLEAN Force);

HRESULT
DiSnapshotHidList(
    _Outptr_result_maybenull_ RX_DI_HID_SNAPSHOT** Snapshot);

HRESULT
DiFindHidByPath(
    _In_z_ PCWSTR Path,
    _Out_ LPGUID Instance);

HRESULT
DiFindHidBackend(
    _In_ REFGUID Instance,
    _Out_ PRX_DI_BACKEND_CREATE* Create);

ULONG
DiHidMouseAxes(VOID);

BOOLEAN
DiHidParseIds(
    _In_z_ PCWSTR Text,
    _Out_ PUSHORT VendorId,
    _Out_ PUSHORT ProductId);

HRESULT
DiCreateJoyConfig(
    _In_ IUnknown* OuterUnknown,
    _Outptr_ IUnknown** Inner);
