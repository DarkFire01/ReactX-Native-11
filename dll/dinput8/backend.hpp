/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The interface every device class implements behind the generic device object
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <dinputd.h>

/* One device object as a backend describes it */
struct DI_OBJECT
{
    const GUID* Guid;
    DWORD Offset;
    DWORD Type;
    DWORD Flags;
};

/* The native layout a backend reports its state in */
struct DI_FORMAT
{
    DWORD DataSize;
    DWORD ObjectCount;
    DI_OBJECT* Objects;
};

struct DI_EM_INSTANCE;

/* A property request after the generic layer resolved the object it names */
struct DI_PROPERTY
{
    DWORD Id;
    DWORD Index;
    DWORD Type;
};

/* What a device class does behind the generic device object */
class DiBackend
{
public:
    virtual
    ~DiBackend()
    {
    }

    virtual
    const DI_FORMAT*
    Format(VOID) = 0;

    virtual
    HRESULT
    GetCapabilities(
        _Inout_ LPDIDEVCAPS Caps) = 0;

    virtual
    HRESULT
    GetDeviceInfo(
        _Inout_ LPDIDEVICEINSTANCEW Info) = 0;

    virtual
    HRESULT
    GetVersions(
        _Out_ LPDIDRIVERVERSIONS Versions)
    {
        UNREFERENCED_PARAMETER(Versions);
        return E_NOTIMPL;
    }

    /* The emulation instance the state comes from, or nullptr for a device read directly */
    virtual
    DI_EM_INSTANCE*
    Emulation(VOID)
    {
        return nullptr;
    }

    /* Fills the name and the fields past the common ones */
    virtual
    HRESULT
    GetObjectInfo(
        _In_ DWORD Index,
        _In_ DWORD Type,
        _Inout_ LPDIDEVICEOBJECTINSTANCEW Info)
    {
        UNREFERENCED_PARAMETER(Index);
        UNREFERENCED_PARAMETER(Type);
        UNREFERENCED_PARAMETER(Info);
        return S_OK;
    }

    virtual
    HRESULT
    GetProperty(
        _In_ const DI_PROPERTY* Property,
        _Inout_ LPDIPROPHEADER Header)
    {
        UNREFERENCED_PARAMETER(Property);
        UNREFERENCED_PARAMETER(Header);
        return E_NOTIMPL;
    }

    virtual
    HRESULT
    SetProperty(
        _In_ const DI_PROPERTY* Property,
        _In_ LPCDIPROPHEADER Header)
    {
        UNREFERENCED_PARAMETER(Property);
        UNREFERENCED_PARAMETER(Header);
        return E_NOTIMPL;
    }

    virtual
    HRESULT
    SetCooperativeLevel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags)
    {
        UNREFERENCED_PARAMETER(Window);
        UNREFERENCED_PARAMETER(Flags);
        return S_OK;
    }

    /* S_FALSE asks the generic layer to acquire the emulation instance */
    virtual
    HRESULT
    Acquire(VOID)
    {
        return S_FALSE;
    }

    virtual
    HRESULT
    Unacquire(VOID)
    {
        return S_FALSE;
    }

    /* Copies the native state */
    virtual
    HRESULT
    GetDeviceState(
        _Out_ VOID* State) = 0;

    virtual
    HRESULT
    Poll(VOID)
    {
        return S_FALSE;
    }

    virtual
    HRESULT
    MapUsage(
        _In_ DWORD Usage,
        _Out_ DWORD* Index)
    {
        UNREFERENCED_PARAMETER(Usage);
        *Index = 0;
        return E_NOTIMPL;
    }

    virtual
    DWORD
    GetUsage(
        _In_ DWORD Index)
    {
        UNREFERENCED_PARAMETER(Index);
        return 0;
    }

    virtual
    HRESULT
    RunControlPanel(
        _In_opt_ HWND Window,
        _In_ DWORD Flags) = 0;

    /* Maps the actions no stored map covered to the device's own idea of them */
    virtual
    HRESULT
    BuildDefaultActionMap(
        _Inout_ LPDIACTIONFORMATW Format,
        _In_ DWORD Flags,
        _In_ REFGUID Instance)
    {
        UNREFERENCED_PARAMETER(Format);
        UNREFERENCED_PARAMETER(Flags);
        UNREFERENCED_PARAMETER(Instance);
        return E_NOTIMPL;
    }
};

/* One object a game controller's default map can hand out, and what the map knows about it */
struct DI_MAP_OBJECT
{
    DWORD Type;
    DWORD Hints;
};

typedef
HRESULT
(*PDI_BACKEND_CREATE)(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend);

/* Maps a keyboard or mouse's objects to the semantics of its class */
HRESULT
DiBuildSystemDefaultMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ DWORD Class,
    _In_ REFGUID Device,
    _In_ const DI_FORMAT* Native,
    _In_ DWORD FirstButton);

/* Maps a game controller's axes, hats and buttons to the actions its semantics fit */
HRESULT
DiBuildDeviceDefaultMap(
    _Inout_ LPDIACTIONFORMATW Format,
    _In_ REFGUID Device,
    _Inout_updates_(Axes + Povs + Buttons) DI_MAP_OBJECT* Objects,
    _In_ DWORD Axes,
    _In_ DWORD Povs,
    _In_ DWORD Buttons);

/* Finds the HID device a GUID names, refreshing the device list once when it is not there */
HRESULT
DiFindHidDevice(
    _In_ REFGUID Guid,
    _Out_ PDI_BACKEND_CREATE* Create);

/* Instance GUIDs of the HID devices, rebuilding the list first; the caller frees the array */
HRESULT
DiListHidDevices(
    _Outptr_result_buffer_maybenull_(*Count) GUID** Guids,
    _Out_ DWORD* Count);
