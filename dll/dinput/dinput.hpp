/*
 * PROJECT:     ReactX DirectInput
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Objects, devices and the input emulation shared by the runtime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0700
#endif

#include <windows.h>
#include <objbase.h>
#include <dinput.h>
#include <dinputd.h>

#include "trace.hpp"

#ifndef MAXULONG
#define MAXULONG                0xFFFFFFFFUL
#endif

/* DIDFT_OPTIONAL, which the DirectInput 7 headers leave out */
#define DI_TYPE_OPTIONAL        0x80000000

/* String resources */
#define IDS_MOUSE               16
#define IDS_KEYBOARD            17
#define IDS_JOYSTICK            18
#define IDS_DIRECTINPUT         19
#define IDS_DIRECTINPUTDEVICE   20
#define IDS_MOUSE_OBJECT        256
#define IDS_KEY_OBJECT          512

/* Keyboard scan code tables in RT_RCDATA */
#define IDR_KEYS_IDENTITY       6

/* Device type bits that carry data in a state buffer */
#define DI_TYPE_HAS_DWORD       (DIDFT_RELAXIS | DIDFT_ABSAXIS | DIDFT_POV)
#define DI_TYPE_INSTANCE_MASK   0x00FFFF00
#define DI_TYPE_ATTRIBUTE_MASK  0x1F000000

/* Private property ids, below the range the headers define */
#define DI_PROP_ENABLE_OBJECT   0xFFFB
#define DI_PROP_TYPE_MAP_FILE   0xFFFD
#define DI_PROP_MAX_BUFFER      0xFFFF

/* RX_DI_SINK flags */
#define DI_SINK_EXCLUSIVE       0x00000001
#define DI_SINK_ACQUIRED        0x00000002
#define DI_SINK_RELATIVE        0x00000004
#define DI_SINK_EMULATED        0x00000008
#define DI_SINK_NOWINKEY        0x00000020
#define DI_SINK_NATIVE_AXES     0x00000040
#define DI_SINK_BUFFER_ALWAYS   0x00000100
#define DI_SINK_LL_MOUSE        0x00010000
#define DI_SINK_LL_KEYBOARD     0x00020000
#define DI_SINK_HOOK_KEYBOARD   0x00080000
#define DI_SINK_SUBCLASS_MOUSE  0x00100000

/* Emulation modes, as the Emulation registry value and the device variants name them */
#define DI_EMULATE_LL_MOUSE     0x01
#define DI_EMULATE_LL_KEYBOARD  0x02
#define DI_EMULATE_HOOK_KBD     0x08
#define DI_EMULATE_SUBCLASS     0x10

/* RxDiDevice state flags */
#define DI_DEV_ACQUIRED         0x00000001
#define DI_DEV_SINK_ACQUIRED    0x00000002
#define DI_DEV_LOCK_READY       0x00000004
#define DI_DEV_COOKS_DATA       0x00000008
#define DI_DEV_POLLED_FORMAT    0x00000010
#define DI_DEV_WANTS_ACQUIRE    0x00000020
#define DI_DEV_LOST             0x00000040
#define DI_DEV_ICONIC_LOST      0x00000080

class RxDiDevice;
struct RX_DI_SOURCE;

/* Sink flags change result for a sink the emulation layer does not drive */
#define DIERR_UNSUPPORTED_SINK  HRESULT_FROM_WIN32(ERROR_INVALID_LEVEL)

/* GetDeviceData flag that reads what is left after the device was lost */
#define DI_GDD_RESIDUAL         0x00000002

/* How RxDiDevice::ConvertObjects reads and writes objects, combined with the wanted type bits */
#define DI_CONVERT_FROM_ID      0x00000100
#define DI_CONVERT_FROM_OFFSET  0x00000200
#define DI_CONVERT_TO_ID        0x00001000
#define DI_CONVERT_TO_OFFSET    0x00002000

/* SetForceFeedbackState command the runtime sends when a device is acquired */
#define DI_FF_ACQUIRE           0x80000000

/* What the device hands the effect shepherd to name itself */
typedef struct _RX_DI_FF_HANDLE
{
    DWORD Id;
    DWORD Tag;
} RX_DI_FF_HANDLE;

/* Force feedback state each joystick shares across processes */
#define DI_JOY_SLOTS            16

typedef struct _RX_DI_JOY_SLOT
{
    DWORD Tag;
    DWORD GlobalGain;
    DWORD DeviceGain;
} RX_DI_JOY_SLOT;

/* Per device go between for the force feedback driver */
class RxDiShepherd
{
public:
    virtual ~RxDiShepherd() {}

    virtual HRESULT DeviceID(_In_ DWORD ExternalId, _In_ DWORD Begin, _In_ PVOID Info) = 0;
    virtual HRESULT GetVersions(_Inout_ LPDIDRIVERVERSIONS Versions) = 0;
    virtual HRESULT Escape(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIEFFESCAPE Request) = 0;
    virtual HRESULT DeviceEscape(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIEFFESCAPE Request) = 0;
    virtual HRESULT SetGain(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Gain) = 0;
    virtual HRESULT SetForceFeedbackState(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Command) = 0;
    virtual HRESULT GetForceFeedbackState(_In_ RX_DI_FF_HANDLE* Handle, _Inout_ LPDIDEVICESTATE State) = 0;
    virtual HRESULT DownloadEffect(_Inout_ RX_DI_FF_HANDLE* Handle, _In_ DWORD EffectId, _In_ LPCDIEFFECT Params, _In_ DWORD Flags) = 0;
    virtual HRESULT DestroyEffect(_Inout_ RX_DI_FF_HANDLE* Handle) = 0;
    virtual HRESULT StartEffect(_In_ RX_DI_FF_HANDLE* Handle, _In_ DWORD Mode, _In_ DWORD Count) = 0;
    virtual HRESULT StopEffect(_In_ RX_DI_FF_HANDLE* Handle) = 0;
    virtual HRESULT GetEffectStatus(_In_ RX_DI_FF_HANDLE* Handle, _Out_ LPDWORD Status) = 0;
    virtual HRESULT SetGlobalGain(_In_ DWORD Gain) = 0;
};

/* One device instance's view of an emulated input class */
typedef struct _RX_DI_SINK
{
    ULONG Flags;
    PBYTE State;
    DIDEVICEOBJECTDATA_DX3* Buffer;
    DIDEVICEOBJECTDATA_DX3* BufferEnd;
    DIDEVICEOBJECTDATA_DX3* Head;
    DIDEVICEOBJECTDATA_DX3* Tail;
    LONG Overflow;
    RxDiDevice* Device;
    HWND Window;
    struct _RX_DI_SINK* Next;
    PULONG OffsetMap;
    ULONG_PTR Extra;
    RX_DI_SOURCE* Source;
    LONG AcquireCount;
    LONG RefCount;
    BOOL UsesWorker;
    BOOL CursorHidden;
} RX_DI_SINK, *PRX_DI_SINK;

typedef
HRESULT
(*PRX_DI_SOURCE_ACQUIRE)(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOL Acquire);

/* Shared state of an input class, the system keyboard or mouse */
struct RX_DI_SOURCE
{
    PBYTE State;
    PULONG Types;
    PRX_DI_SOURCE_ACQUIRE Acquire;
    LONG AcquireCount;
    ULONG StateSize;
    LONG RefCount;
};

/* What a backend hands the emulation layer when it opens a sink */
typedef struct _RX_DI_SINK_PARAMS
{
    ULONG StateSize;
    ULONG ObjectCount;
    const DIOBJECTDATAFORMAT* Objects;
    ULONG_PTR Extra;
    ULONG Mode;
} RX_DI_SINK_PARAMS;

/* Object a property or object request resolved to */
typedef struct _RX_DI_PROP_TARGET
{
    ULONG_PTR Property;
    ULONG Index;
    ULONG Type;
} RX_DI_PROP_TARGET;

/* Values HKLM\...\DirectInput\Compatibility can set for an application */
typedef struct _RX_DI_APP_HACKS
{
    ULONG ReAcquire;
    ULONG NoSubClass;
    ULONG NativeAxisOnly;
    ULONG NoPollUnacquire;
    ULONG SucceedAcquire;
    ULONG MaxDeviceNameLength;
    UCHAR DataFormatHint;
    UCHAR Reserved[3];
} RX_DI_APP_HACKS;

/* Flag GetAxisTraits adds for axes that report absolute positions */
#define DI_AXIS_ABSOLUTE 0x100

/* The device specific half of a device object */
class RxDiBackend
{
public:
    virtual ~RxDiBackend() {}

    virtual PRX_DI_SINK GetSink(VOID) { return nullptr; }
    virtual HRESULT GetVersions(_Inout_ LPDIDRIVERVERSIONS Versions) { return E_NOTIMPL; }
    virtual HRESULT GetDataFormat(_Outptr_ const DIDATAFORMAT** Format) = 0;
    virtual HRESULT GetObjectInfo(_In_ const RX_DI_PROP_TARGET* Target, _Inout_ LPDIDEVICEOBJECTINSTANCEW Info) = 0;
    virtual HRESULT GetCapabilities(_Inout_ LPDIDEVCAPS Caps) = 0;
    virtual HRESULT Acquire(VOID) { return S_FALSE; }
    virtual HRESULT Unacquire(VOID) { return S_FALSE; }
    virtual HRESULT GetDeviceState(_Out_ PVOID State) = 0;
    virtual HRESULT GetDeviceInfo(_Inout_ LPDIDEVICEINSTANCEW Info) = 0;
    virtual HRESULT GetProperty(_In_ const RX_DI_PROP_TARGET* Target, _Inout_ LPDIPROPHEADER Header) { return E_NOTIMPL; }
    virtual HRESULT SetProperty(_In_ const RX_DI_PROP_TARGET* Target, _In_ LPCDIPROPHEADER Header) { return E_NOTIMPL; }
    virtual HRESULT SetEventNotification(_In_opt_ HANDLE Event) { return S_FALSE; }
    virtual HRESULT SetCooperativeLevel(_In_opt_ HWND Window, _In_ DWORD Flags) = 0;
    virtual HRESULT RunControlPanel(_In_opt_ HWND Owner, _In_ DWORD Flags) { return E_NOTIMPL; }
    virtual HRESULT CookDeviceData(_In_ ULONG Count, _Inout_opt_ DIDEVICEOBJECTDATA_DX3* Data) { return E_NOTIMPL; }
    virtual HRESULT CreateEffectDriver(_Outptr_ class RxDiShepherd** Driver) { return E_NOTIMPL; }
    virtual HRESULT GetFFConfigKey(_In_ REGSAM Access, _Out_ HKEY* Key) { return E_NOTIMPL; }
    virtual HRESULT SendDeviceData(_In_reads_(*Count) LPCDIDEVICEOBJECTDATA Data, _Inout_ LPDWORD Count, _In_ DWORD Flags) { *Count = 0; return E_NOTIMPL; }
    virtual HRESULT Poll(VOID) { return S_FALSE; }
    virtual ULONG GetUsage(_In_ ULONG Index) { return 0; }
    virtual HRESULT MapUsage(_In_ ULONG Usage, _Out_ PULONG Index) { return E_NOTIMPL; }
    /* The action map axis kind of an object in the low byte and DI_AXIS_ABSOLUTE for absolute axes */
    virtual ULONG GetAxisTraits(_In_ ULONG Index) { return 0; }
    virtual HRESULT SetDIData(_In_ DWORD Version, _In_ RX_DI_APP_HACKS* Hacks) { return E_NOTIMPL; }
};

typedef
HRESULT
(*PRX_DI_BACKEND_CREATE)(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend);

/* One effect a force feedback driver lists in the registry */
struct RX_DI_EFFECT_ENTRY
{
    DWORD Id;
    DWORD EffectType;
    DWORD StaticParams;
    DWORD DynamicParams;
    DWORD Coordinates;
    GUID Guid;
    WCHAR Name[MAX_PATH];
};

static_assert(sizeof(RX_DI_EFFECT_ENTRY) == 556, "registry effect record");

/* Reference counting shared by the COM objects, with aggregation and holds */
class RxDiUnknown
{
public:
    VOID
    Hold(VOID);

    VOID
    Unhold(VOID);

    IUnknown*
    InnerUnknown(VOID)
    {
        return &Inner;
    }

protected:
    explicit
    RxDiUnknown(
        _In_opt_ IUnknown* OuterUnknown);

    virtual ~RxDiUnknown();

    /* The interface IUnknown queries hand out */
    virtual PVOID PrimaryInterface(VOID) = 0;

    /* Returns the interface for an IID without a reference, or nullptr */
    virtual PVOID QueryOwn(_In_ REFIID Iid) = 0;

    /* Runs when the last application reference goes away */
    virtual VOID OnFinalRelease(VOID) {}

    /* Lets an object answer IIDs that need work, like an aggregated helper */
    virtual HRESULT QuerySlow(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object)
    {
        *Object = nullptr;
        return E_NOINTERFACE;
    }

    IUnknown* Outer;

private:
    class RxInner final : public IUnknown
    {
    public:
        RxDiUnknown* Owner;

        HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override;
        ULONG STDMETHODCALLTYPE AddRef(VOID) override;
        ULONG STDMETHODCALLTYPE Release(VOID) override;
    };

    RxInner Inner;
    LONG RefCount;
    LONG HoldCount;
};

/* IDirectInput, IDirectInput2 and IDirectInput7 in both character sets */
class RxDirectInput final : public RxDiUnknown, public IDirectInput7W, public IDirectInput7A
{
public:
    static
    HRESULT
    Create(
        _In_opt_ IUnknown* OuterUnknown,
        _In_ REFIID Iid,
        _COM_Outptr_ PVOID* Object);

    DWORD
    Version(VOID) const
    {
        return CreatedVersion;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override;
    ULONG STDMETHODCALLTYPE AddRef(VOID) override;
    ULONG STDMETHODCALLTYPE Release(VOID) override;

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID Guid, LPDIRECTINPUTDEVICEW* Device, LPUNKNOWN OuterUnknown) override;
    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID Guid, LPDIRECTINPUTDEVICEA* Device, LPUNKNOWN OuterUnknown) override;
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD DevType, LPDIENUMDEVICESCALLBACKW Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD DevType, LPDIENUMDEVICESCALLBACKA Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID Guid) override;
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND Owner, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE Instance, DWORD Version) override;
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID Class, LPCWSTR Name, LPGUID Instance) override;
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID Class, LPCSTR Name, LPGUID Instance) override;
    HRESULT STDMETHODCALLTYPE CreateDeviceEx(REFGUID Guid, REFIID Iid, LPVOID* Device, LPUNKNOWN OuterUnknown) override;

private:
    explicit
    RxDirectInput(
        _In_opt_ IUnknown* OuterUnknown);

    ~RxDirectInput() override;

    PVOID PrimaryInterface(VOID) override { return static_cast<IDirectInput7W*>(this); }
    PVOID QueryOwn(_In_ REFIID Iid) override;
    HRESULT QuerySlow(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override;

    HRESULT
    CreateDeviceCommon(
        _In_ REFGUID Guid,
        _In_ REFIID Iid,
        _Outptr_ PVOID* Device,
        _In_opt_ IUnknown* OuterUnknown);

    DWORD CreatedVersion;
    IUnknown* JoyConfig;
    CRITICAL_SECTION Lock;
    BOOL LockReady;
};

/* IDirectInputDevice, 2 and 7 in both character sets */
class RxDiDevice final : public RxDiUnknown, public IDirectInputDevice7W, public IDirectInputDevice7A
{
    friend class RxDiEffect;

public:
    static
    HRESULT
    Create(
        _In_opt_ IUnknown* OuterUnknown,
        _In_ REFIID Iid,
        _COM_Outptr_ PVOID* Object);

    VOID Enter(VOID);
    VOID Leave(VOID);

    /* Called by the emulation layer when the system takes the device away */
    VOID
    MarkLost(VOID);

    HANDLE
    NotifyEvent(VOID) const
    {
        return Event;
    }

    HRESULT
    InternalUnacquire(VOID);

    DWORD
    Version(VOID) const
    {
        return CreatedVersion;
    }

    /* Turns object ids or data format offsets into ids or offsets of objects with the wanted type */
    HRESULT
    ConvertObjects(
        _In_ ULONG Count,
        _Inout_updates_(Count) PDWORD Values,
        _In_ DWORD How);

    /* Fits an effect read from a file to this device's actuators */
    HRESULT
    ModifyEffectParams(
        _Inout_ LPDIEFFECT Params,
        _In_ REFGUID Guid);

    HRESULT STDMETHODCALLTYPE QueryInterface(_In_ REFIID Iid, _COM_Outptr_ PVOID* Object) override;
    ULONG STDMETHODCALLTYPE AddRef(VOID) override;
    ULONG STDMETHODCALLTYPE Release(VOID) override;

    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS Caps) override;
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKW Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE EnumObjects(LPDIENUMDEVICEOBJECTSCALLBACKA Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID Property, LPDIPROPHEADER Header) override;
    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID Property, LPCDIPROPHEADER Header) override;
    HRESULT STDMETHODCALLTYPE Acquire(VOID) override;
    HRESULT STDMETHODCALLTYPE Unacquire(VOID) override;
    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD Size, LPVOID State) override;
    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD ObjectSize, LPDIDEVICEOBJECTDATA Data, LPDWORD Count, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT Format) override;
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE NewEvent) override;
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND Window, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEW Info, DWORD Object, DWORD How) override;
    HRESULT STDMETHODCALLTYPE GetObjectInfo(LPDIDEVICEOBJECTINSTANCEA Info, DWORD Object, DWORD How) override;
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEW Info) override;
    HRESULT STDMETHODCALLTYPE GetDeviceInfo(LPDIDEVICEINSTANCEA Info) override;
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND Owner, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE Instance, DWORD Version, REFGUID Guid) override;
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID Guid, LPCDIEFFECT Params, LPDIRECTINPUTEFFECT* Effect, LPUNKNOWN OuterUnknown) override;
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKW Callback, LPVOID Context, DWORD Type) override;
    HRESULT STDMETHODCALLTYPE EnumEffects(LPDIENUMEFFECTSCALLBACKA Callback, LPVOID Context, DWORD Type) override;
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOW Info, REFGUID Guid) override;
    HRESULT STDMETHODCALLTYPE GetEffectInfo(LPDIEFFECTINFOA Info, REFGUID Guid) override;
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD State) override;
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD Command) override;
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE Escape) override;
    HRESULT STDMETHODCALLTYPE Poll(VOID) override;
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD ObjectSize, LPCDIDEVICEOBJECTDATA Data, LPDWORD Count, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCWSTR Name, LPDIENUMEFFECTSINFILECALLBACK Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(LPCSTR Name, LPDIENUMEFFECTSINFILECALLBACK Callback, LPVOID Context, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCWSTR Name, DWORD Count, LPDIFILEEFFECT Effects, DWORD Flags) override;
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(LPCSTR Name, DWORD Count, LPDIFILEEFFECT Effects, DWORD Flags) override;

private:
    /* How GetDeviceState moves data from the device layout to the application layout */
    enum class StateCopy
    {
        Slow,
        Matched,
        Direct,
        Equal
    };

    explicit
    RxDiDevice(
        _In_opt_ IUnknown* OuterUnknown);

    ~RxDiDevice() override;

    PVOID PrimaryInterface(VOID) override { return static_cast<IDirectInputDevice7W*>(this); }
    PVOID QueryOwn(_In_ REFIID Iid) override;
    VOID OnFinalRelease(VOID) override;

    HRESULT ResetState(VOID);
    HRESULT LoadDeviceFormat(VOID);
    HRESULT ParseFormat(_In_ LPCDIDATAFORMAT Format);
    HRESULT ParseLegacyJoystickFormat(VOID);
    VOID OptimizeFormat(VOID);
    HRESULT ReadNative(_Out_ PVOID State);
    HRESULT ReadState(_Out_ PVOID State);
    ULONG FindObject(_In_ const DIOBJECTDATAFORMAT* Wanted, _In_ const ULONG* Used);
    HRESULT MapHow(_In_ DWORD Object, _In_ DWORD How, _Inout_ RX_DI_PROP_TARGET* Target);
    HRESULT ValidateProperty(_In_ ULONG_PTR Property, _In_ LPCDIPROPHEADER Header, _Inout_ RX_DI_PROP_TARGET* Target);
    HRESULT CheckDefaultProperty(_In_ const RX_DI_PROP_TARGET* Target, _In_ BOOLEAN Set);
    HRESULT DefaultGetProperty(_In_ const RX_DI_PROP_TARGET* Target, _Inout_ LPDIPROPHEADER Header);
    HRESULT DefaultSetProperty(_In_ const RX_DI_PROP_TARGET* Target, _In_ LPCDIPROPHEADER Header);
    HRESULT ApplyProperty(_In_ ULONG_PTR Property, _In_ LPCDIPROPHEADER Header);
    HRESULT FillObjectInfo(_In_ const RX_DI_PROP_TARGET* Target, _Inout_ LPDIDEVICEOBJECTINSTANCEW Info);
    HRESULT CanAcquire(VOID);
    HRESULT WatchForeground(VOID);
    HRESULT ReleaseBackendAcquire(VOID);
    VOID RetryAcquire(VOID);
    HRESULT CreateEffectDriver(VOID);
    HRESULT AcquireForceFeedback(VOID);
    VOID RefreshGain(VOID);
    HRESULT InitForceFeedback(VOID);

    RxDiBackend* Backend;
    ULONG State;
    HWND Window;
    DWORD CoopFlags;
    HANDLE Event;
    StateCopy CopyMode;
    BOOLEAN RelativeAxes;

    /* Application data format */
    PULONG ObjectToApp;
    PULONG AppToObject;
    ULONG AppDataSize;
    PULONG LegacyObjectToApp;
    PULONG LegacyAppToObject;
    ULONG LegacyDataSize;

    /* Device data format */
    DIDATAFORMAT Native;
    LONG CopyDelta;
    ULONG CopyStart;
    ULONG CopyLength;
    PBYTE Scratch;
    PBYTE Baseline;
    ULONG RelativeAxisCount;
    PULONG RelativeAxisOffsets;
    PRX_DI_SINK Sink;

    ULONG PolledFlag;
    HRESULT LostResult;
    ULONG BufferSize;
    PULONG PovFill;
    ULONG PovFillCount;
    struct RX_DI_EFFECT_ENTRY* EffectList;
    ULONG EffectCount;
    ULONG ForceFeedbackCaps;
    DIFFDEVICEATTRIBUTES FFAttributes;
    ULONG MaxBufferSize;
    RxDiShepherd* EffectDriver;
    RX_DI_FF_HANDLE DriverId;
    DWORD CreatedVersion;
    PVOID* CreatedEffects;
    ULONG CreatedEffectCount;
    ULONG Gain;
    ULONG AutoCenter;
    ULONG InstanceMask;
    LONG LockDepth;
    DWORD LockOwner;
    CRITICAL_SECTION Lock;
    GUID InstanceGuid;
    RX_DI_APP_HACKS Hacks;
};

/* effect.cpp */

HRESULT
DiReadAttributes(
    _In_ HKEY Key,
    _Out_ LPDIFFDEVICEATTRIBUTES Attributes);

BOOLEAN
DiReadEffectEntry(
    _In_ HKEY Effects,
    _In_ DWORD Index,
    _Inout_ RX_DI_EFFECT_ENTRY* Entry);

HRESULT
DiCreateEffect(
    _In_ RxDiDevice* Device,
    _In_ REFGUID Guid,
    _In_opt_ LPCDIEFFECT Params,
    _Outptr_ LPDIRECTINPUTEFFECT* Effect,
    _In_opt_ IUnknown* OuterUnknown);

LPDIRECTINPUTEFFECT
DiHoldEffect(
    _In_ PVOID Effect);

VOID
DiUnholdEffect(
    _In_ LPDIRECTINPUTEFFECT Effect);

VOID
DiReleaseCreatedEffects(
    _In_reads_opt_(Count) PVOID* Effects,
    _In_ ULONG Count);

HRESULT
DiEnumEffectsInFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_opt_ LPDIENUMEFFECTSINFILECALLBACK Callback,
    _In_opt_ LPVOID Context,
    _In_ DWORD Flags);

HRESULT
DiWriteEffectToFile(
    _In_ RxDiDevice* Device,
    _In_opt_z_ LPCSTR Name,
    _In_ DWORD Count,
    _In_reads_opt_(Count) LPDIFILEEFFECT Effects,
    _In_ DWORD Flags);

/* shepherd.cpp */

HRESULT
DiCreateShepherd(
    _In_opt_ HKEY Config,
    _Outptr_ RxDiShepherd** Shepherd);

DWORD
DiReadJoystickGain(
    _In_ ULONG Id);

/* main.cpp */

HRESULT
DiReadValue(
    _In_ HKEY Key,
    _In_z_ PCWSTR Name,
    _Out_writes_bytes_(Size) PVOID Buffer,
    _In_ ULONG Size);


extern HINSTANCE DiModule;
extern LONG DiModuleRefs;
extern CRITICAL_SECTION DiLock;
extern BOOL DiLockReady;
extern BOOL DiLowLevelHooks;
extern ULONG DiEmulation;
extern HANDLE DiGlobalMutex;
extern volatile LONG* DiSequence;

HRESULT
DiCheckVersion(
    _In_opt_ HINSTANCE Instance,
    _In_ DWORD Version);

HRESULT
DiStartup(VOID);

ULONG
DiNextGuidSequence(VOID);

HRESULT
DiOpenKey(
    _In_ HKEY Root,
    _In_z_ PCWSTR Path,
    _In_ REGSAM Access,
    _In_ DWORD Options,
    _Out_ HKEY* Key);

BOOLEAN
DiReadDword(
    _In_ HKEY Key,
    _In_z_ PCWSTR Name,
    _Out_ PULONG Value);

LONG
DiReadString(
    _In_ HKEY Key,
    _In_opt_z_ PCWSTR Name,
    _Out_writes_(Count) PWSTR Buffer,
    _In_ ULONG Count);

BOOLEAN
DiParseGuid(
    _In_z_ PCWSTR Text,
    _Out_ LPGUID Guid);

VOID
DiLockJoysticks(VOID);

VOID
DiUnlockJoysticks(VOID);

RX_DI_JOY_SLOT*
DiJoystickSlot(
    _In_ ULONG Id);

BOOLEAN
DiGetApplicationId(
    _Out_writes_(276) PWSTR Id,
    _Out_writes_opt_(MAX_PATH) PWSTR Name);

VOID
DiGetAppHacks(
    _Out_ RX_DI_APP_HACKS* Hacks);

HRESULT
DiRunControlPanel(
    _In_z_ PCWSTR Arguments);

HRESULT
DiCheckString(
    _In_opt_z_ PCWSTR Text,
    _In_ ULONG Limit);

HRESULT
DiCheckStringA(
    _In_opt_z_ PCSTR Text,
    _In_ ULONG Limit);

HRESULT
DiAllocate(
    _In_ SIZE_T Size,
    _Inout_ PVOID* Block);

VOID
DiFree(
    _Inout_ PVOID* Block);

PVOID
DiFindTable(
    _In_ ULONG Id);

BOOL
DiIsWindowOurs(
    _In_opt_ HWND Window);

DWORD
DiWindowProcess(
    _In_opt_ HWND Window);

HRESULT
DiAcquireExclusive(
    _In_ const GUID* Instance,
    _In_ HWND Window,
    _In_ DWORD Flags);

VOID
DiReleaseExclusive(
    _In_ const GUID* Instance,
    _In_ HWND Window,
    _In_ DWORD Flags);

/* devices.cpp */

typedef struct _RX_DI_STATIC_DEVICE
{
    const GUID* Instance;
    UCHAR Type;
    PRX_DI_BACKEND_CREATE Create;
} RX_DI_STATIC_DEVICE;

extern const RX_DI_STATIC_DEVICE DiStaticDevices[6];

HRESULT
DiFindBackend(
    _In_ REFGUID Instance,
    _Out_ PRX_DI_BACKEND_CREATE* Create);

ULONG
DiGetKeyboardType(
    _In_ INT Which);

HRESULT
DiCreateKeyboard(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend);

HRESULT
DiCreateMouse(
    _In_ REFGUID Instance,
    _Outptr_ RxDiBackend** Backend);

/* emulation.cpp */

extern RX_DI_SOURCE DiKeyboardSource;
extern RX_DI_SOURCE DiMouseSource;

HRESULT
DiOpenSink(
    _In_ const RX_DI_SINK_PARAMS* Params,
    _In_ RX_DI_SOURCE* Source,
    _Outptr_ PRX_DI_SINK* Sink);

VOID
DiCloseSink(
    _In_opt_ PRX_DI_SINK Sink);

HRESULT
DiOpenKeyboardSink(
    _Inout_ RX_DI_SINK_PARAMS* Params,
    _Outptr_ PRX_DI_SINK* Sink);

HRESULT
DiOpenMouseSink(
    _Inout_ RX_DI_SINK_PARAMS* Params,
    _Outptr_ PRX_DI_SINK* Sink);

HRESULT
DiAcquireSink(
    _Inout_ PRX_DI_SINK Sink);

HRESULT
DiUnacquireSink(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG KeepFlags);

HRESULT
DiSetSinkFormat(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG Size,
    _In_reads_(Size) const ULONG* Map);

HRESULT
DiSetSinkBuffer(
    _Inout_ PRX_DI_SINK Sink,
    _In_ ULONG Count);

/* What a backend may read and change on the sink it opened */
ULONG
DiSinkFlags(
    _In_ PRX_DI_SINK Sink);

VOID
DiSetSinkExclusive(
    _Inout_ PRX_DI_SINK Sink,
    _In_ BOOLEAN Exclusive);

ULONG_PTR
DiSinkContext(
    _In_ PRX_DI_SINK Sink);

VOID
DiPrimeKeyboard(
    _In_ ULONG Toggles);

VOID
DiSeedMouse(
    _In_ ULONG Buttons);

VOID
DiPostState(
    _In_ RX_DI_SOURCE* Source,
    _In_reads_bytes_(Source->StateSize) const BYTE* Values,
    _In_ DWORD Time);

VOID
DiProcessShutdown(VOID);
