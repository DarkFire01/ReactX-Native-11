/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Wrappers that print the device DDI calls and the Direct3D 12 calls a driver makes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Wraps every filled entry of a device function table the driver returned */
VOID
CkWrapDeviceTable(
    _Inout_ D3DDDI_DEVICEFUNCS* Functions);

/* Points the Direct3D 12 device and queue methods of this process at printing wrappers */
BOOLEAN
CkHookD3D12(VOID);

/* Prints what other threads did since the last flush, one thread after another */
VOID
CkFlushWorkers(VOID);
