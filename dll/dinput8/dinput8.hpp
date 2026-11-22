/*
 * PROJECT:     ReactX DirectInput 8
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects, devices and the state they share inside the library
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "backend.hpp"
#include <dinputd.h>
#include <new>

#include "trace.hpp"

/* A format object the device need not have */
#define DI_OBJECT_OPTIONAL          0x80000000

/* The one interface version this library accepts */
#define DI_VERSION                  0x0800

/* Device flags reported in DIDEVCAPS that the generic layer adds */
#define DI_CAPS_FLAGS_MASK          0xFF0000

/* String resources */
#define IDS_MOUSE                   16
#define IDS_KEYBOARD                17
#define IDS_JOYSTICK                18
#define IDS_DIRECTINPUT8            19
#define IDS_DIRECTINPUTDEVICE8      20
#define IDS_BUTTON                  28
#define IDS_AXIS                    29
#define IDS_POV                     30
#define IDS_COLLECTION              31
#define IDS_MOUSE_OBJECT            256
#define IDS_KEY_UNKNOWN             511
#define IDS_KEY_FIRST               512

/* Emulation levels named by the alias device GUIDs */
#define DI_EMULATE_MOUSE            0x01
#define DI_EMULATE_KEYBOARD         0x02
#define DI_EMULATE_KEYBOARD_HOOK    0x08
#define DI_EMULATE_MOUSE_HOOK       0x10

extern HINSTANCE DiInstance;
extern LONG DiModuleRefs;
extern CRITICAL_SECTION DiLock;
extern BOOLEAN DiLockReady;
extern UINT DiNotifyMessage;
extern DWORD DiEmulation;

/* Per application compatibility settings */
struct DI_APP_HACKS
{
    DWORD ReAcquire;
    DWORD NoSubClass;
    DWORD MaxNameLength;
};

extern DI_APP_HACKS DiAppHacks;

VOID
DiModuleAddRef(VOID);

VOID
DiModuleRelease(VOID);

HRESULT
DiCheckVersion(
    _In_opt_ HINSTANCE Instance,
    _In_ DWORD Version);

HRESULT
DiSharedInitialize(VOID);

VOID
DiSharedClose(VOID);

VOID
DiRegisterApplication(
    _In_ DWORD Version,
    _In_ BOOLEAN UsesMapper);

HRESULT
DiLaunchControlPanel(
    _In_z_ PCWSTR Applet);

/* Records an exclusive owner in the section every process shares */
HRESULT
DiAcquireExclusive(
    _In_ const GUID* Device,
    _In_opt_ HWND Window,
    _In_ DWORD Cooperation);

VOID
DiReleaseExclusive(
    _In_ const GUID* Device,
    _In_opt_ HWND Window,
    _In_ DWORD Cooperation);

BOOLEAN
DiIsUnknown(
    _In_ REFIID Riid);

/* Common lifetime for every object, with the inner unknown an aggregating owner talks to */
class DiObject
{
public:
    DiObject(
        _In_opt_ IUnknown* Outer);

    virtual
    ~DiObject();

    /* Returns the interface for an IID, without a reference, or nullptr */
    virtual
    VOID*
    FindInterface(
        _In_ REFIID Riid) = 0;

    IUnknown*
    InnerUnknown(VOID)
    {
        return &Inner;
    }

    HRESULT
    OuterQuery(
        _In_ REFIID Riid,
        _Outptr_ VOID** Object)
    {
        return Controller->QueryInterface(Riid, Object);
    }

    ULONG
    OuterAddRef(VOID)
    {
        return Controller->AddRef();
    }

    ULONG
    OuterRelease(VOID)
    {
        return Controller->Release();
    }

protected:
    class Unknown : public IUnknown
    {
    public:
        DiObject* Owner;

        STDMETHODIMP
        QueryInterface(
            _In_ REFIID Riid,
            _Outptr_ VOID** Object) override;

        STDMETHODIMP_(ULONG)
        AddRef() override;

        STDMETHODIMP_(ULONG)
        Release() override;
    };

    Unknown Inner;
    IUnknown* Controller;
    LONG Refs;

    /* An IID no static face answers, for objects created on demand */
    virtual
    HRESULT
    QueryExtra(
        _In_ REFIID Riid,
        _Outptr_ VOID** Object)
    {
        UNREFERENCED_PARAMETER(Riid);
        *Object = nullptr;
        return E_NOINTERFACE;
    }
};

/* Hands a newly made object to the caller through the IID asked for */
HRESULT
DiFinishCreate(
    _In_ DiObject* Object,
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result);

/* The top level IDirectInput8 object */
HRESULT
DiCreateInput(
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result);

/* An uninitialized IDirectInputDevice8 object */
HRESULT
DiCreateDevice(
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Riid,
    _Outptr_ VOID** Result);

HRESULT
DiCreateKeyboard(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend);

HRESULT
DiCreateMouse(
    _In_ REFGUID Guid,
    _Outptr_ DiBackend** Backend);

/* Devices the library knows without a hardware list, in enumeration order */
struct DI_STATIC_DEVICE
{
    const GUID* Guid;
    BYTE Type;
    PDI_BACKEND_CREATE Create;
};

#define DI_STATIC_DEVICE_COUNT      6

extern const DI_STATIC_DEVICE DiStaticDevices[DI_STATIC_DEVICE_COUNT];

/* Fills an ANSI device instance from the wide one, keeping the caller's size */
VOID
DiInstanceToAnsi(
    _Inout_ LPDIDEVICEINSTANCEA Ansi,
    _In_ LPCDIDEVICEINSTANCEW Wide);

extern const DIDATAFORMAT DiJoystickFormat;
