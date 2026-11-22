/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The section every DirectInput 8 library in a session maps to coordinate devices
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

/* A device some process holds exclusively */
struct DI_EXCLUSIVE_OWNER
{
    GUID Device;
    HWND Window;
    DWORD ProcessId;
    DWORD CooperativeLevel;
};

#define DI_EXCLUSIVE_MAX        121
#define DI_JOYSTICK_SLOTS       16

/* Force feedback gains each joystick shares across processes */
struct DI_JOYSTICK_SLOT
{
    DWORD Tag;
    DWORD GlobalGain;
    DWORD DeviceGain;
};
#define DI_SHARED_SIZE          4096

/* The layout is shared with other processes, so fields keep their places */
struct DI_SHARED_STATE
{
    DWORD OwnerCount;
    DWORD Sequence;
    DWORD UuidCounter;
    DI_JOYSTICK_SLOT Joysticks[DI_JOYSTICK_SLOTS];
    DWORD ConfigChangedTime;
    DI_EXCLUSIVE_OWNER Owners[DI_EXCLUSIVE_MAX];
};

static_assert(FIELD_OFFSET(DI_SHARED_STATE, ConfigChangedTime) == 204, "shared layout");
static_assert(FIELD_OFFSET(DI_SHARED_STATE, Owners) == 208, "shared layout");
static_assert(sizeof(DI_SHARED_STATE) <= 4096, "shared layout");

extern HANDLE DiGlobalMutex;
extern HANDLE DiJoystickMutex;
extern DI_SHARED_STATE* DiShared;
