/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     System keyboard and mouse input gathered on a worker thread for every acquired device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "backend.hpp"

/* Instance flags */
#define DI_EM_EXCLUSIVE         0x00000001
#define DI_EM_ACQUIRED          0x00000002
#define DI_EM_RELATIVE          0x00000004
#define DI_EM_ACTIVE            0x00000008
#define DI_EM_NOWINKEY          0x00000020

/* How an instance gets its input, kept in the high word like the emulation levels */
#define DI_EM_MOUSE_RAW         0x00010000
#define DI_EM_KEYBOARD_RAW      0x00020000
#define DI_EM_KEYBOARD_HOOK     0x00080000
#define DI_EM_MOUSE_SUBCLASS    0x00100000

/* Starts or stops a device that gathers its own input */
typedef
HRESULT
(*PDI_EM_SWITCH)(
    _In_ VOID* Context,
    _In_ BOOLEAN On);

/* What one kind of device looks like to every instance of it */
struct DI_EM_CLASS
{
    BOOLEAN Keyboard;
    DWORD DataSize;
    BYTE* State;
    DWORD* ObjectTypes;
    BOOLEAN TypesReady;
    LONG Acquired;
    LONG Exclusive;

    /* Set for a device with its own reader, nullptr for the system keyboard and mouse */
    PDI_EM_SWITCH Switch;
    VOID* Context;
};

/* One buffered change, with the object index the device resolves to an application offset */
struct DI_EM_EVENT
{
    DWORD Object;
    DWORD Data;
    DWORD TimeStamp;
    DWORD Sequence;
};

/* The emulation state one device object owns */
struct DI_EM_INSTANCE
{
    DWORD Flags;
    DI_EM_CLASS* Class;
    DI_EM_INSTANCE* Next;
    LONG Refs;
    LONG AcquireCount;
    BOOLEAN HoldsThread;
    BOOLEAN CursorHidden;
    HWND Window;
    HANDLE Event;
    CRITICAL_SECTION Lock;

    /* Native offset to device object index, -1 where the data format has no object */
    LONG* ObjectMap;
    DWORD ObjectMapSize;

    /* Ring of buffered changes */
    DI_EM_EVENT* Buffer;
    DWORD Capacity;
    DWORD Head;
    DWORD Tail;
    BOOLEAN Overflow;
};

extern DI_EM_CLASS DiKeyboardClass;
extern DI_EM_CLASS DiMouseClass;

HRESULT
DiEmCreate(
    _In_ DI_EM_CLASS* Class,
    _In_ const DI_FORMAT* Format,
    _In_ DWORD Mode,
    _Outptr_ DI_EM_INSTANCE** Instance);

/* Hands a changed native state to the acquired instances of a class */
VOID
DiEmPostState(
    _In_ DI_EM_CLASS* Class,
    _In_reads_bytes_(Class->DataSize) const BYTE* State,
    _In_ DWORD Time);

VOID
DiEmRelease(
    _In_ DI_EM_INSTANCE* Instance);

HRESULT
DiEmAcquire(
    _In_ DI_EM_INSTANCE* Instance);

HRESULT
DiEmUnacquire(
    _In_ DI_EM_INSTANCE* Instance);

HRESULT
DiEmSetBufferSize(
    _In_ DI_EM_INSTANCE* Instance,
    _In_ DWORD Count);

HRESULT
DiEmSetObjectMap(
    _In_ DI_EM_INSTANCE* Instance,
    _In_reads_(Count) const LONG* Map,
    _In_ DWORD Count);

VOID
DiEmSetEvent(
    _In_ DI_EM_INSTANCE* Instance,
    _In_opt_ HANDLE Event);

/* Seeds the mouse buttons from their current state when a mouse is acquired */
VOID
DiEmSeedMouseButtons(
    _In_ BYTE Buttons[3]);
