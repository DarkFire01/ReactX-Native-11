/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Action map checks, defaults and the state devices share about them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "dinput8.hpp"

/* Semantic device classes the system devices map */
#define DI_SEMANTIC_KEYBOARD        0x81000000
#define DI_SEMANTIC_MOUSE           0x82000000
#define DI_SEMANTIC_COMMS           0x83000000

/* Highest genre a format may name */
#define DI_GENRE_LAST               0x29

/* Semantics of the second priority */
#define DI_SEMANTIC_PRIORITY2       0x00004000

/* The how bits that say an action is mapped */
#define DI_HOW_MAPPED_MASK          0x3F

/* The sub types of a communications device control that map as one */
#define DI_COMMS_SELECTION          3
#define DI_COMMS_HARDWIRED          4

/* Device validation succeeded on a hardwired communications device */
#define DI_MAP_HARDWIRED            0x13

/* Map store answer that it has nothing for the device */
#define DI_MAP_NOT_STORED           2

/*
 * No device map store is installed, so each request to it fails as a missing one does.
 * The store would sit behind dimap.dll and the configuration UI behind diactfrm.dll.
 */
#define DI_MAP_STORE_MISSING        REGDB_E_CLASSNOTREG

/* Longest user name, in characters with the terminator, a map accepts */
#define DI_MAP_USER_LIMIT           257

/* Longest action name, in characters with the terminator */
#define DI_ACTION_NAME_LIMIT        256

/* The data format object types a semantic may land on */
BYTE
DiSemanticObjectType(
    _In_ DWORD Semantic);

/* Checks what every action format must have right; ANSI formats share the fields read */
HRESULT
DiCheckActionFormat(
    _In_opt_ const DIACTIONFORMATW* Format);

/* Checks the action names a caller supplies, read as wide or ANSI strings */
HRESULT
DiCheckActionNames(
    _In_ const DIACTIONFORMATW* Format,
    _In_ BOOLEAN Ansi);

HRESULT
DiCheckWideString(
    _In_opt_z_ PCWSTR Text,
    _In_ DWORD Limit);

HRESULT
DiCheckAnsiString(
    _In_opt_z_ PCSTR Text,
    _In_ DWORD Limit);

/* Checks semantics and clears or keeps prior mappings as the build flags ask */
HRESULT
DiValidateSemantics(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ DWORD MapFlags);

/* The checksum of a format and the actions it maps to one device */
DWORD
DiActionMapCrc(
    _In_ LPCDIACTIONFORMATW Format,
    _In_ REFGUID Device);

/* Resolves the user a map is for; frees with DiFreeMapUser */
HRESULT
DiResolveMapUser(
    _In_opt_z_ PCSTR Ansi,
    _In_opt_z_ PCWSTR Wide,
    _Outptr_ PWSTR* User);

VOID
DiFreeMapUser(
    _In_opt_ PWSTR User,
    _In_opt_z_ PCWSTR Wide);

/* Records or, with no name, forgets the user a device is set up for */
HRESULT
DiSetDeviceUser(
    _In_ REFGUID Device,
    _In_opt_z_ PCWSTR User);

/* Copies the user a device is set up for; S_FALSE and an empty name when there is none */
HRESULT
DiGetDeviceUser(
    _In_ REFGUID Device,
    _Out_writes_(MAX_PATH) PWSTR User);

/* Whether a device is recorded as set up for exactly this user */
BOOLEAN
DiIsDeviceUser(
    _In_ REFGUID Device,
    _In_z_ PCWSTR User);

/* Copies the ANSI fields an action format shares with its wide form */
VOID
DiActionHeaderFromAnsi(
    _Out_ LPDIACTIONFORMATW Wide,
    _In_ LPCDIACTIONFORMATA Ansi);
