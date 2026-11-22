/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry points, enumeration and the class factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"
#include "resource.h"

static const GUID DdClsidDirectDrawFactory2 = { 0xB9DC4790, 0x4AF1, 0x11D1, { 0x8C, 0x4C, 0x00, 0xC0, 0x4F, 0xD9, 0x30, 0xC5 } };

static const GUID DdDisplayGuid = { 0x67685559, 0x3106, 0x11D0, { 0xB9, 0x71, 0x00, 0xAA, 0x00, 0x34, 0x2F, 0x9F } };

static constexpr DWORD DdEnumFlagsMask = DDENUM_ATTACHEDSECONDARYDEVICES | DDENUM_DETACHEDSECONDARYDEVICES |
                                         DDENUM_NONDISPLAYDEVICES;

HRESULT
WINAPI
DirectDrawCreate(
    _In_opt_ GUID* Guid,
    _Out_ LPDIRECTDRAW* Draw,
    _In_opt_ IUnknown* Outer)
{
    DdInterface* Interface;
    HRESULT hr;

    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;

    DdState.ReloadRegistry = GetProfileIntA("DirectDraw", "reloadreg", 0) != 0;
    if (Draw == nullptr)
        return E_INVALIDARG;
    *Draw = nullptr;

    hr = DdCreateObject(Guid, &Interface, nullptr, 0);
    if (SUCCEEDED(hr))
        *Draw = reinterpret_cast<LPDIRECTDRAW>(Interface);
    return hr;
}

HRESULT
WINAPI
DirectDrawCreateEx(
    _In_opt_ GUID* Guid,
    _Out_ LPVOID* Draw,
    _In_ REFIID Iid,
    _In_opt_ IUnknown* Outer)
{
    DdInterface* Interface;
    HRESULT hr;

    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (!IsEqualGUID(Iid, IID_IDirectDraw7))
        return E_INVALIDARG;

    DdState.ReloadRegistry = GetProfileIntA("DirectDraw", "reloadreg", 0) != 0;
    if (Draw == nullptr)
        return E_INVALIDARG;

    hr = DdCreateObject(Guid, &Interface, nullptr, DDRAWILCL_DIRECTDRAW7);
    if (FAILED(hr))
    {
        *Draw = nullptr;
        return hr;
    }

    hr = DdQueryObject(Interface, &IID_IDirectDraw7, Draw);
    DdReleaseInterface(Interface);
    return hr;
}

static
BOOLEAN
DdEnumNonDisplayDrivers(
    _In_ LPDDENUMCALLBACKEXA Callback,
    _In_opt_ VOID* Context)
{
    CHAR KeyName[256];
    CHAR Description[256];
    CHAR DriverName[MAX_PATH];
    DWORD NameSize;
    DWORD Size;
    DWORD Type;
    DWORD Index;
    HKEY Drivers;
    HKEY Driver;
    GUID Guid;
    WCHAR WideName[64];
    HDC Dc;
    BOOL Continue;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Hardware\\DirectDrawDrivers", 0, KEY_READ, &Drivers) != ERROR_SUCCESS)
        return TRUE;

    for (Index = 0;; Index++)
    {
        NameSize = sizeof(KeyName);
        if (RegEnumKeyExA(Drivers, Index, KeyName, &NameSize, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
            break;

        /* Key names are GUIDs without braces */
        swprintf_s(WideName, L"{%S}", KeyName);
        if (FAILED(IIDFromString(WideName, &Guid)))
            continue;
        if (RegOpenKeyExA(Drivers, KeyName, 0, KEY_READ, &Driver) != ERROR_SUCCESS)
            continue;

        Size = sizeof(Description) - 1;
        if (RegQueryValueExA(Driver, "Description", NULL, &Type, reinterpret_cast<BYTE*>(Description), &Size) ==
                ERROR_SUCCESS &&
            Type == REG_SZ)
        {
            Description[Size] = '\0';
            Size = sizeof(DriverName) - 1;
            if (RegQueryValueExA(Driver, "DriverName", NULL, &Type, reinterpret_cast<BYTE*>(DriverName), &Size) ==
                ERROR_SUCCESS)
            {
                DriverName[Size] = '\0';
                Dc = Type == REG_SZ ? DdCreateDisplayDc(DriverName) : NULL;
                if (Dc != NULL)
                {
                    Continue = Callback(&Guid, Description, DriverName, Context, NULL);
                    DeleteDC(Dc);
                    if (!Continue)
                    {
                        RegCloseKey(Driver);
                        RegCloseKey(Drivers);
                        return FALSE;
                    }
                }
            }
        }
        RegCloseKey(Driver);
    }

    RegCloseKey(Drivers);
    return TRUE;
}

HRESULT
WINAPI
DirectDrawEnumerateExA(
    _In_opt_ LPDDENUMCALLBACKEXA Callback,
    _In_opt_ LPVOID Context,
    _In_ DWORD Flags)
{
    CHAR Description[256];
    DISPLAY_DEVICEA Device;
    BOOLEAN Secondaries = FALSE;
    DWORD Value = 0;
    DWORD Size = sizeof(Value);
    DWORD Index;
    HKEY Key;
    GUID Guid;

    DdState.RegFlags &= ~DdRegEnumAttachedSecondaries;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\DirectDraw", 0, KEY_READ, &Key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(Key, "EnumerateAttachedSecondaries", NULL, NULL, reinterpret_cast<BYTE*>(&Value), &Size) ==
                ERROR_SUCCESS &&
            Value != 0)
        {
            Secondaries = TRUE;
            DdState.RegFlags |= DdRegEnumAttachedSecondaries;
        }
        RegCloseKey(Key);
    }

    if (Callback == nullptr || (Flags & ~DdEnumFlagsMask) != 0)
        return E_INVALIDARG;

    LoadStringA(DdState.Module, IDS_PRIMARY_DISPLAY, Description, sizeof(Description));
    if (!Callback(NULL, Description, const_cast<PSTR>("display"), Context, NULL))
        return S_OK;

    if ((Flags & DDENUM_NONDISPLAYDEVICES) != 0 && !DdEnumNonDisplayDrivers(Callback, Context))
        return S_OK;

    if (!DdIsMultiMonitor())
        return S_OK;

    for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
    {
        if ((Device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;

        /* Detached displays are never listed; attached ones only when asked for */
        if ((Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;
        if ((Flags & DDENUM_ATTACHEDSECONDARYDEVICES) == 0 &&
            (!Secondaries || (Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0))
        {
            continue;
        }

        Guid = DdDisplayGuid;
        Guid.Data1 += Index;
        if (!Callback(&Guid, Device.DeviceString, Device.DeviceName, Context, DdMonitorFromDevice(Device.DeviceName)))
            break;
    }

    return S_OK;
}

struct DdEnumTranslate
{
    LPDDENUMCALLBACKA Callback;
    LPVOID Context;
};

static
BOOL
WINAPI
DdTranslateEnum(
    _In_opt_ GUID* Guid,
    _In_ LPSTR Description,
    _In_ LPSTR Name,
    _In_opt_ LPVOID Context,
    _In_opt_ HMONITOR Monitor)
{
    DdEnumTranslate* Translate = static_cast<DdEnumTranslate*>(Context);

    return Translate->Callback(Guid, Description, Name, Translate->Context);
}

HRESULT
WINAPI
DirectDrawEnumerateA(
    _In_opt_ LPDDENUMCALLBACKA Callback,
    _In_opt_ LPVOID Context)
{
    DdEnumTranslate Translate = { Callback, Context };

    if (Callback == nullptr)
        return E_INVALIDARG;
    return DirectDrawEnumerateExA(DdTranslateEnum, &Translate, DDENUM_NONDISPLAYDEVICES);
}

HRESULT
WINAPI
DirectDrawEnumerateExW(
    _In_opt_ LPDDENUMCALLBACKEXW Callback,
    _In_opt_ LPVOID Context,
    _In_ DWORD Flags)
{
    return E_NOTIMPL;
}

HRESULT
WINAPI
DirectDrawEnumerateW(
    _In_opt_ LPDDENUMCALLBACKW Callback,
    _In_opt_ LPVOID Context)
{
    if (Callback == nullptr)
        return E_INVALIDARG;
    return E_NOTIMPL;
}

HRESULT
WINAPI
DirectDrawCreateClipper(
    _In_ DWORD Flags,
    _Out_ LPDIRECTDRAWCLIPPER* Clipper,
    _In_opt_ IUnknown* Outer)
{
    DD_TRACE_ONCE(L"DirectDrawCreateClipper is not implemented yet");
    return DDERR_UNSUPPORTED;
}

/* The class factory DllGetClassObject hands out */
struct DdFactory
{
    const VOID* Vtbl;
    ULONG RefCount;
    CLSID Clsid;
};

static HRESULT STDMETHODCALLTYPE DdFactoryQueryInterface(_In_ VOID* Com, _In_ REFIID Iid, _Outptr_ VOID** Object);
static ULONG STDMETHODCALLTYPE DdFactoryAddRef(_In_ VOID* Com);
static ULONG STDMETHODCALLTYPE DdFactoryRelease(_In_ VOID* Com);
static HRESULT STDMETHODCALLTYPE DdFactoryCreateInstance(_In_ VOID* Com, _In_opt_ IUnknown* Outer, _In_ REFIID Iid,
                                                         _Outptr_ VOID** Object);
static HRESULT STDMETHODCALLTYPE DdFactoryLockServer(_In_ VOID* Com, _In_ BOOL Lock);

static const VOID* const DdFactoryVtbl[] =
{
    reinterpret_cast<const VOID*>(&DdFactoryQueryInterface),
    reinterpret_cast<const VOID*>(&DdFactoryAddRef),
    reinterpret_cast<const VOID*>(&DdFactoryRelease),
    reinterpret_cast<const VOID*>(&DdFactoryCreateInstance),
    reinterpret_cast<const VOID*>(&DdFactoryLockServer),
};

static
DdFactory*
DdFactoryFromCom(
    _In_opt_ VOID* Com)
{
    DdFactory* Factory = static_cast<DdFactory*>(Com);

    if (Factory == nullptr || Factory->Vtbl != DdFactoryVtbl)
        return nullptr;
    return Factory;
}

static
HRESULT
STDMETHODCALLTYPE
DdFactoryQueryInterface(
    _In_ VOID* Com,
    _In_ REFIID Iid,
    _Outptr_ VOID** Object)
{
    DdFactory* Factory;

    DdLockScope Lock;

    Factory = DdFactoryFromCom(Com);
    if (Factory == nullptr)
        return E_FAIL;

    *Object = nullptr;
    if (!IsEqualGUID(Iid, IID_IClassFactory) && !IsEqualGUID(Iid, IID_IUnknown))
        return E_NOINTERFACE;

    Factory->RefCount++;
    *Object = Factory;
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
DdFactoryAddRef(
    _In_ VOID* Com)
{
    DdFactory* Factory;

    DdLockScope Lock;

    Factory = DdFactoryFromCom(Com);
    if (Factory == nullptr)
        return static_cast<ULONG>(E_FAIL);
    return ++Factory->RefCount;
}

static
ULONG
STDMETHODCALLTYPE
DdFactoryRelease(
    _In_ VOID* Com)
{
    DdFactory* Factory;

    DdLockScope Lock;

    Factory = DdFactoryFromCom(Com);
    if (Factory == nullptr)
        return static_cast<ULONG>(E_FAIL);

    Factory->RefCount -= 1;
    if (Factory->RefCount != 0)
        return Factory->RefCount;

    DdState.LockCount -= 1;
    Factory->Vtbl = nullptr;
    LocalFree(Factory);
    return 0;
}

static
HRESULT
STDMETHODCALLTYPE
DdFactoryCreateInstance(
    _In_ VOID* Com,
    _In_opt_ IUnknown* Outer,
    _In_ REFIID Iid,
    _Outptr_ VOID** Object)
{
    DdFactory* Factory;
    DdInterface* Interface;
    DdKind Kind;
    HDC Dc;
    int Depth;
    HRESULT hr;

    if (Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;

    Factory = DdFactoryFromCom(Com);
    if (Factory == nullptr || &Iid == nullptr)
        return E_INVALIDARG;
    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;

    Dc = GetDC(NULL);
    Depth = GetDeviceCaps(Dc, PLANES) * GetDeviceCaps(Dc, BITSPIXEL);
    ReleaseDC(NULL, Dc);
    if (Depth < 8)
        return E_FAIL;

    DdLockScope Lock;

    if (IsEqualGUID(Factory->Clsid, CLSID_DirectDrawClipper))
    {
        DD_TRACE_ONCE(L"clipper objects are not implemented yet");
        return E_NOINTERFACE;
    }

    /* The second factory class has no objects of its own */
    if (!IsEqualGUID(Factory->Clsid, CLSID_DirectDraw) && !IsEqualGUID(Factory->Clsid, CLSID_DirectDraw7))
        return S_OK;

    if (IsEqualGUID(Iid, IID_IUnknown) || IsEqualGUID(Iid, IID_IDirectDraw))
        Kind = DdKind::Draw1;
    else if (IsEqualGUID(Iid, IID_IDirectDraw2))
        Kind = DdKind::Draw2;
    else if (IsEqualGUID(Iid, IID_IDirectDraw4))
        Kind = DdKind::Draw4;
    else if (IsEqualGUID(Iid, IID_IDirectDraw7))
        Kind = DdKind::Draw7;
    else
        return E_NOINTERFACE;

    Interface = DdNewUninitializedObject(Kind);
    if (Interface == nullptr)
        return E_OUTOFMEMORY;

    /* The query below takes the only reference */
    Interface->RefCount -= 1;
    Interface->Local->RefCount -= 1;
    hr = DdQueryObject(Interface, &Iid, Object);
    if (FAILED(hr))
        DdFreeUninitializedObject(Interface);
    return hr;
}

static
HRESULT
STDMETHODCALLTYPE
DdFactoryLockServer(
    _In_ VOID* Com,
    _In_ BOOL Lock)
{
    typedef HRESULT (WINAPI* PDD_LOCK_EXTERNAL)(IUnknown*, BOOL, BOOL);
    PDD_LOCK_EXTERNAL LockExternal;
    HMODULE Ole;
    HRESULT hr = E_UNEXPECTED;

    if (DdFactoryFromCom(Com) == nullptr)
        return E_INVALIDARG;

    Ole = LoadLibraryExA("OLE32.DLL", NULL, 0);
    if (Ole == NULL)
        return hr;

    LockExternal = reinterpret_cast<PDD_LOCK_EXTERNAL>(GetProcAddress(Ole, "CoLockObjectExternal"));
    if (LockExternal != nullptr)
    {
        hr = LockExternal(static_cast<IUnknown*>(Com), Lock, TRUE);
        if (SUCCEEDED(hr))
        {
            DdLockScope Scope;

            if (Lock)
                DdState.LockCount++;
            else
                DdState.LockCount -= 1;
        }
    }

    return hr;
}


_Check_return_
STDAPI
DllGetClassObject(
    _In_ REFCLSID Clsid,
    _In_ REFIID Iid,
    _Outptr_ LPVOID* Object)
{
    DdFactory* Factory;
    HRESULT hr;

    if (Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;
    if (&Clsid == nullptr || &Iid == nullptr)
        return E_INVALIDARG;

    if (!IsEqualGUID(Clsid, CLSID_DirectDraw) && !IsEqualGUID(Clsid, CLSID_DirectDraw7) &&
        !IsEqualGUID(Clsid, CLSID_DirectDrawClipper) && !IsEqualGUID(Clsid, DdClsidDirectDrawFactory2))
    {
        return E_FAIL;
    }
    if (!IsEqualGUID(Iid, IID_IUnknown) && !IsEqualGUID(Iid, IID_IClassFactory))
        return E_NOINTERFACE;

    DdLockScope Lock;

    Factory = static_cast<DdFactory*>(LocalAlloc(LPTR, sizeof(*Factory)));
    if (Factory == nullptr)
        return E_OUTOFMEMORY;

    Factory->Vtbl = DdFactoryVtbl;
    Factory->RefCount = 0;
    Factory->Clsid = Clsid;
    hr = DdFactoryQueryInterface(Factory, Iid, Object);
    if (FAILED(hr))
    {
        LocalFree(Factory);
        *Object = nullptr;
        return hr;
    }

    DdState.LockCount++;
    return hr;
}

__control_entrypoint(DllExport)
STDAPI
DllCanUnloadNow(VOID)
{
    DdLockScope Lock;

    if (DdState.Interfaces != nullptr || DdState.Locals != nullptr || DdState.Clippers != nullptr)
        return S_FALSE;
    return DdState.LockCount != 0 ? S_FALSE : S_OK;
}

/* The helpers below serve other system components rather than applications */

VOID
WINAPI
AcquireDDThreadLock(VOID)
{
    DdEnterLock();
}

VOID
WINAPI
ReleaseDDThreadLock(VOID)
{
    DdLeaveLock();
}

/* Steps over a DrawPrimitives2 command the driver did not know */
HRESULT
WINAPI
D3DParseUnknownCommand(
    _In_ LPVOID Command,
    _Out_ LPVOID* Next)
{
    const BYTE* Header = static_cast<const BYTE*>(Command);
    WORD Count;

    *Next = Command;
    Count = *reinterpret_cast<const WORD*>(Header + 2);

    switch (Header[0])
    {
    case 4:
    case 5:
    case 6:
    case 7:
    case 9:
    case 10:
    case 11:
    case 12:
    case 14:
        return MAKE_DDHRESULT(3000);
    case 13:
        *Next = const_cast<BYTE*>(Header + 4 + static_cast<SIZE_T>(Count) * Header[1]);
        return S_OK;
    case 28:
        *Next = const_cast<BYTE*>(Header + 4 + 16 * static_cast<SIZE_T>(Count));
        return S_OK;
    case 29:
        *Next = const_cast<BYTE*>(Header + 4 + 8 * static_cast<SIZE_T>(Count));
        return S_OK;
    default:
        return E_FAIL;
    }
}

VOID
WINAPI
SetAppCompatData(
    _In_ DWORD Type,
    _In_ DWORD Value)
{
    switch (Type)
    {
    case 1:
        DdState.DwmOffForPrimaryLock = FALSE;
        break;
    case 2:
        DdState.DwmOffForPrimaryBlt = FALSE;
        break;
    case 3:
        DdState.ForceFullscreenSprite = TRUE;
        break;
    case 4:
        DdState.ForceBltToPrimary = TRUE;
        break;
    case 5:
        DdState.ShadowBufferColor = Value;
        break;
    case 7:
        DdState.DwmOffForPrimaryLock = TRUE;
        break;
    case 8:
        DdState.EnableOverlays = TRUE;
        break;
    case 9:
        DdState.DisableSurfaceLock = TRUE;
        break;
    case 10:
        DdState.RedirectPrimaryBlts = TRUE;
        break;
    case 11:
        DdState.StripMaxWindowBorder = TRUE;
        break;
    case 12:
        DdState.DisableMaxWindowedMode = TRUE;
        break;
    default:
        break;
    }
}

LPVOID
WINAPI
GetOLEThunkData(
    _In_ ULONG_PTR Index)
{
    switch (Index)
    {
    case 2:
        return DdState.Interfaces;
    case 1:
    case 3:
    case 4:
    case 6:
        return nullptr;
    default:
        DD_TRACE_ONCE(L"GetOLEThunkData(%Iu) is not implemented", Index);
        return nullptr;
    }
}

HRESULT
WINAPI
DSoundHelp(
    _In_opt_ HWND Window,
    _In_opt_ WNDPROC Procedure,
    _In_ DWORD ProcessId)
{
    DD_TRACE_ONCE(L"DSoundHelp window tracking is not implemented");
    return S_OK;
}

HRESULT
WINAPI
DDGetAttachedSurfaceLcl(
    _In_ LPVOID Surface,
    _In_ LPDDSCAPS2 Caps,
    _Out_ LPVOID* Attached)
{
    *Attached = nullptr;
    return DDERR_NOTFOUND;
}

HRESULT
WINAPI
DDInternalLock(
    _In_ LPVOID Surface,
    _Out_ LPVOID* Bits)
{
    DD_TRACE_ONCE(L"DDInternalLock is not implemented yet");
    return DDERR_UNSUPPORTED;
}

HRESULT
WINAPI
DDInternalUnlock(
    _In_ LPVOID Surface)
{
    DD_TRACE_ONCE(L"DDInternalUnlock is not implemented yet");
    return DDERR_UNSUPPORTED;
}

BOOL
WINAPI
CompleteCreateSysmemSurface(
    _In_ LPVOID Surface)
{
    return TRUE;
}

LPVOID
WINAPI
GetDDSurfaceLocal(
    _In_ LPVOID Draw,
    _In_ DWORD Handle,
    _Inout_ BOOL* Created)
{
    DD_TRACE_ONCE(L"GetDDSurfaceLocal is not implemented yet");
    return nullptr;
}

HRESULT
WINAPI
GetSurfaceFromDC(
    _In_ HDC Dc,
    _Out_ LPVOID* Surface,
    _Out_opt_ LPVOID* Local)
{
    *Surface = nullptr;
    return DDERR_NOTFOUND;
}

HRESULT
WINAPI
RegisterSpecialCase(
    _In_ LPVOID SourceFormat,
    _In_ LPVOID TargetFormat,
    _In_ LPVOID Junction,
    _In_ DWORD Count)
{
    return DDERR_NOTFOUND;
}
