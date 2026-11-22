/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectDraw object creation, the shared driver records and their lifetime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Monitors past the primary are named by this GUID with the display index added to Data1 */
static const GUID DdDisplayGuidBase = { 0x67685559, 0x3106, 0x11D0, { 0xB9, 0x71, 0x00, 0xAA, 0x00, 0x34, 0x2F, 0x9F } };
static constexpr DWORD DdMaxDisplayIndex = 32;

/* The internal DirectDraw version the system runtime stamps on every driver */
static constexpr DWORD DdRuntimeInternal = 0x0902;

/* Driver flags the matching rules look at besides the public ones */
static constexpr ULONG DdDriverRetired = 0x40000000;

static
VOID
DdLinkLocal(
    _In_ DdLocal* Local)
{
    Local->Next = DdState.Locals;
    DdState.Locals = Local;
}

static
VOID
DdUnlinkLocal(
    _In_ DdLocal* Local)
{
    DdLocal** Link;

    for (Link = &DdState.Locals; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Local)
        {
            *Link = Local->Next;
            return;
        }
    }
}

static
VOID
DdUnlinkInterface(
    _In_ DdInterface* Interface)
{
    DdInterface** Link;

    for (Link = &DdState.Interfaces; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Interface)
        {
            *Link = Interface->Next;
            return;
        }
    }
}

static
VOID
DdUnlinkDriver(
    _In_ DdDriver* Driver)
{
    DdDriver** Link;

    for (Link = &DdState.Drivers; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Driver)
        {
            *Link = Driver->Next;
            return;
        }
    }
}

_Ret_maybenull_
DdInterface*
DdFindInterface(
    _In_ DdLocal* Local,
    _In_ DdKind Kind,
    _In_ BOOLEAN Initialized)
{
    DdInterface* Interface;

    for (Interface = DdState.Interfaces; Interface != nullptr; Interface = Interface->Next)
    {
        if (Interface->Local == Local && Interface->Kind == Kind && Interface->Initialized == Initialized)
            return Interface;
    }

    Interface = new (std::nothrow) DdInterface;
    if (Interface == nullptr)
        return nullptr;

    Interface->Vtbl = DdVtblFor(Kind, Initialized);
    Interface->Local = Local;
    Interface->RefCount = 0;
    Interface->Kind = Kind;
    Interface->Initialized = Initialized;
    Interface->Next = DdState.Interfaces;
    DdState.Interfaces = Interface;
    return Interface;
}

/* A first interface on a new local object, counted once */
_Ret_maybenull_
static
DdInterface*
DdNewObject(
    _In_opt_ DdDriver* Driver,
    _In_ DdKind Kind,
    _In_ BOOLEAN Initialized)
{
    DdInterface* Interface;
    DdLocal* Local;

    Local = new (std::nothrow) DdLocal;
    if (Local == nullptr)
        return nullptr;

    ZeroMemory(Local, sizeof(*Local));
    Interface = new (std::nothrow) DdInterface;
    if (Interface == nullptr)
    {
        delete Local;
        return nullptr;
    }

    Local->Driver = Driver;
    Local->RefCount = 1;
    Local->ProcessId = GetCurrentProcessId();
    if (Driver != nullptr)
    {
        Driver->RefCount++;
        Local->SavedMode = Driver->ModeKey;
    }
    DdLinkLocal(Local);

    Interface->Vtbl = DdVtblFor(Kind, Initialized);
    Interface->Local = Local;
    Interface->RefCount = 1;
    Interface->Kind = Kind;
    Interface->Initialized = Initialized;
    Interface->Next = DdState.Interfaces;
    DdState.Interfaces = Interface;
    return Interface;
}

_Ret_maybenull_
DdInterface*
DdNewUninitializedObject(
    _In_ DdKind Kind)
{
    return DdNewObject(nullptr, Kind, FALSE);
}

VOID
DdFreeUninitializedObject(
    _In_ DdInterface* Interface)
{
    DdUnlinkLocal(Interface->Local);
    DdUnlinkInterface(Interface);
    delete Interface->Local;
    delete Interface;
}

static
VOID
DdDestroyDriver(
    _In_ DdDriver* Driver)
{
    DdUnlinkDriver(Driver);
    DdOrphanClippers(Driver);
    DdCloseHardware(Driver->Hardware);
    delete[] Driver->FourCC;
    delete Driver;
}

/* Resolves the GUID an application passed to the GDI name of its display */
static
HRESULT
DdDisplayFromGuid(
    _In_ const GUID* Guid,
    _Out_writes_z_(Size) PSTR Name,
    _In_ SIZE_T Size,
    _Out_ BOOLEAN* Explicit)
{
    DISPLAY_DEVICEA Device;
    DWORD Index;

    *Explicit = FALSE;
    if (IsEqualGUID(*Guid, GUID_NULL))
    {
        strcpy_s(Name, Size, "display");
        return S_OK;
    }

    Index = Guid->Data1 - DdDisplayGuidBase.Data1;
    if (Index > DdMaxDisplayIndex ||
        memcmp(&Guid->Data2, &DdDisplayGuidBase.Data2, sizeof(GUID) - sizeof(Guid->Data1)) != 0)
    {
        return DDERR_INVALIDDIRECTDRAWGUID;
    }

    if (!DdEnumDisplayDevice(Index, &Device) || (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
        return DDERR_INVALIDDIRECTDRAWGUID;

    *Explicit = TRUE;
    strncpy_s(Name, Size, Device.DeviceName, _TRUNCATE);
    if (_stricmp(Name, DdState.PrimaryDisplay) == 0)
        strcpy_s(Name, Size, "display");
    return S_OK;
}

static
BOOLEAN
DdIsAttachedToDesktop(
    _In_z_ PCSTR Name)
{
    DISPLAY_DEVICEA Device;
    DWORD Index;

    if (_stricmp(Name, "display") == 0)
        return TRUE;

    for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
    {
        if (_stricmp(Device.DeviceName, Name) == 0)
            return (Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
    }

    return FALSE;
}

_Ret_maybenull_
static
DdDriver*
DdNewDriver(
    _In_z_ PCSTR Name,
    _In_ HDC Dc,
    _In_ ULONG CreateFlags)
{
    DdDriver* Driver;

    Driver = new (std::nothrow) DdDriver;
    if (Driver == nullptr)
        return nullptr;

    ZeroMemory(Driver, sizeof(*Driver));
    strncpy_s(Driver->Name, Name, _TRUNCATE);
    if (_stricmp(Name, "display") == 0)
        strcpy_s(Driver->DeviceName, DdState.PrimaryDisplay);
    else
        strncpy_s(Driver->DeviceName, Name, _TRUNCATE);
    Driver->Internal1 = DdRuntimeInternal;

    if (!DdReadDisplayMode(Driver))
    {
        delete Driver;
        return nullptr;
    }

    if ((CreateFlags & DdCreateEmulationOnly) == 0)
        Driver->Hardware = DdOpenHardware(Dc);

    Driver->Flags = DDRAWI_DISPLAYDRV | DdDriverDoesGdi;
    if (Driver->Hardware == nullptr || !DdBuildHardwareCaps(Driver))
    {
        if (Driver->Hardware != nullptr)
        {
            DdCloseHardware(Driver->Hardware);
            Driver->Hardware = nullptr;
        }
        Driver->Flags |= DDRAWI_NOHARDWARE;
        DdBuildEmulatedCaps(Driver);
        DdAdapterLuidFromDc(Dc, &Driver->AdapterLuid);
    }

    DdUpdateDeviceRect(Driver);

    if ((CreateFlags & DdCreateHardwareOnly) == 0)
        DdBuildHelCaps(Driver);
    else
        Driver->Flags |= DdDriverEmulatedCaps;

    Driver->Next = DdState.Drivers;
    DdState.Drivers = Driver;
    return Driver;
}

/* An existing driver for the display that this kind of request may share */
_Ret_maybenull_
static
DdDriver*
DdFindDriver(
    _In_z_ PCSTR Name,
    _In_ ULONG CreateFlags)
{
    DdLocal* Local;
    DdDriver* Driver;

    for (Local = DdState.Locals; Local != nullptr; Local = Local->Next)
    {
        Driver = Local->Driver;
        if (Driver == nullptr || _stricmp(Driver->Name, Name) != 0)
            continue;

        if ((CreateFlags & DdCreateEmulationOnly) != 0)
        {
            if ((Driver->Flags & DDRAWI_NOHARDWARE) != 0 && (Driver->Flags & DdDriverRetired) == 0)
                return Driver;
        }
        else if ((Driver->Flags & (DDRAWI_NOHARDWARE | DdDriverRetired)) == 0)
        {
            return Driver;
        }

        /* With one monitor any live driver for the display will do */
        if (Driver->MonitorCount <= 1 && (Driver->Flags & DdDriverRetired) == 0)
            return Driver;
    }

    return nullptr;
}

HRESULT
DdCreateObject(
    _In_opt_ GUID* Guid,
    _Out_ DdInterface** Result,
    _In_opt_ DdInterface* Existing,
    _In_ ULONG LocalFlags)
{
    CHAR Name[32];
    ULONG CreateFlags = 0;
    BOOLEAN Explicit;
    BOOLEAN EmulateOnly = FALSE;
    DdDriver* Driver;
    DdInterface* Interface;
    DdLocal* Local;
    HDC Dc;
    HRESULT hr;

    *Result = nullptr;

    /* Palettes below 8 bits per pixel are not supported */
    Dc = GetDC(NULL);
    if (GetDeviceCaps(Dc, PLANES) * GetDeviceCaps(Dc, BITSPIXEL) < 8)
    {
        ReleaseDC(NULL, Dc);
        return DDERR_NODIRECTDRAWSUPPORT;
    }
    ReleaseDC(NULL, Dc);

    DdLockScope Lock;

    DdReadRegistry();
    DdRefreshPrimaryName();
    if ((DdState.RegFlags & DdRegEmulationOnly) != 0)
    {
        CreateFlags = DdCreateEmulationOnly;
        EmulateOnly = GetSystemMetrics(SM_CMONITORS) <= 1;
    }

    if (Guid == reinterpret_cast<GUID*>(DDCREATE_EMULATIONONLY))
    {
        CreateFlags = DdCreateEmulationOnly;
        Guid = nullptr;
    }
    else if (Guid == reinterpret_cast<GUID*>(DDCREATE_HARDWAREONLY))
    {
        CreateFlags |= DdCreateHardwareOnly;
        Guid = nullptr;
    }
    else if (EmulateOnly)
    {
        Guid = nullptr;
    }

    hr = DdDisplayFromGuid(Guid != nullptr ? Guid : &GUID_NULL, Name, sizeof(Name), &Explicit);
    if (FAILED(hr))
        return hr;

    Driver = DdFindDriver(Name, CreateFlags);
    if (Driver != nullptr && (CreateFlags & DdCreateEmulationOnly) != 0 && (Driver->Flags & DDRAWI_NOHARDWARE) == 0)
        return E_FAIL;

    Dc = DdCreateDisplayDc(Name);
    if (Dc == NULL)
    {
        DdTrace(L"cannot create a DC for %S", Name);
        return DDERR_CANTCREATEDC;
    }

    if (Driver == nullptr)
    {
        if ((CreateFlags & (DdCreateEmulationOnly | DdCreateHardwareOnly)) ==
            (DdCreateEmulationOnly | DdCreateHardwareOnly))
        {
            DeleteDC(Dc);
            return DDERR_NODIRECTDRAWHW;
        }

        DdState.DisplayUniqueness = DdQueryDisplayUniqueness();
        Driver = DdNewDriver(Name, Dc, CreateFlags);
        if (Driver == nullptr)
        {
            DeleteDC(Dc);
            return E_FAIL;
        }
    }
    else if ((Driver->Flags & (DdDriverHelCaps | DdDriverEmulatedCaps)) == 0)
    {
        if ((CreateFlags & DdCreateHardwareOnly) == 0)
            DdBuildHelCaps(Driver);
        else
            Driver->Flags |= DdDriverEmulatedCaps;
    }

    if (DdIsAttachedToDesktop(Name))
        Driver->Flags |= DdDriverAttached;

    if (Existing != nullptr)
    {
        /* Initialize on a class factory object fills in its local record */
        Interface = Existing;
        Local = Interface->Local;
        Local->Driver = Driver;
        Local->SavedMode = Driver->ModeKey;
        Driver->RefCount += Local->RefCount;
    }
    else
    {
        Interface = DdNewObject(Driver, DdKind::Draw1, TRUE);
        if (Interface == nullptr)
        {
            if (Driver->RefCount == 0)
                DdDestroyDriver(Driver);
            DeleteDC(Dc);
            return E_OUTOFMEMORY;
        }
        Local = Interface->Local;
    }

    if (Explicit)
        Local->Flags |= DDRAWILCL_EXPLICITMONITOR;
    Local->Flags |= LocalFlags;
    Local->Dc = Dc;

    *Result = Interface;
    return S_OK;
}

ULONG
DdAddRefInterface(
    _In_ DdInterface* Interface)
{
    DdLockScope Lock;

    if (Interface->Local->Driver != nullptr)
        Interface->Local->Driver->RefCount++;
    Interface->Local->RefCount++;
    Interface->RefCount++;
    return Interface->RefCount;
}

/* Undoes what a local object did to the display when the application lets go of it */
static
VOID
DdRetireLocal(
    _In_ DdLocal* Local)
{
    DdDriver* Driver = Local->Driver;

    if (Driver != nullptr)
    {
        DdReleaseLocalSurfaces(Local);
        DdReleaseLocalPalettes(Local);
        DdReleaseLocalClippers(Local);
        if (Driver->ExclusiveOwner == Local)
            Driver->ExclusiveOwner = nullptr;
    }

    if ((Local->Flags & DDRAWILCL_CREATEDWINDOW) != 0 && IsWindow(Local->Window))
    {
        DestroyWindow(Local->Window);
        Local->Window = NULL;
        Local->Flags &= ~DDRAWILCL_CREATEDWINDOW;
    }
}

ULONG
DdReleaseInterface(
    _In_ DdInterface* Interface)
{
    DdLocal* Local;
    DdLocal* Other;
    DdDriver* Driver;
    ULONG Remaining;
    BOOLEAN SharedDc;

    DdLockScope Lock;

    if (Interface->RefCount == 0)
        return 0;

    Local = Interface->Local;
    Driver = Local->Driver;
    Interface->RefCount -= 1;
    Remaining = Interface->RefCount;
    Local->RefCount -= 1;
    if (Driver != nullptr)
        Driver->RefCount -= 1;

    if (Local->RefCount == 0)
        DdRetireLocal(Local);

    if (Driver != nullptr && Driver->RefCount == 0)
        DdDestroyDriver(Driver);

    if (Local->RefCount == 0)
    {
        SharedDc = FALSE;
        for (Other = DdState.Locals; Other != nullptr; Other = Other->Next)
        {
            if (Other != Local && Other->Dc == Local->Dc)
                SharedDc = TRUE;
        }
        if (Local->Dc != NULL && !SharedDc)
            DeleteDC(Local->Dc);

        DdUnlinkLocal(Local);
        delete Local;
    }

    if (Remaining == 0)
    {
        DdUnlinkInterface(Interface);
        Interface->Vtbl = nullptr;
        delete Interface;
    }

    return Remaining;
}

VOID
DdResetAllObjects(
    _In_ BOOLEAN Restore)
{
    DdDriver* Driver;

    UNREFERENCED_PARAMETER(Restore);

    /* A mode change reads the new mode; the caps that depend on it follow */
    for (Driver = DdState.Drivers; Driver != nullptr; Driver = Driver->Next)
    {
        if (!DdReadDisplayMode(Driver))
        {
            DdState.RestoreFailed = TRUE;
            continue;
        }

        if (Driver->Hardware != nullptr)
            DdBuildHardwareCaps(Driver);
        DdUpdateDeviceRect(Driver);
        DdInvalidateSurfaces(Driver);
        DdTrace(L"display %S changed to %lux%lu at %lu bits", Driver->Name, Driver->Mode.Width, Driver->Mode.Height,
                Driver->Mode.BitCount);
    }
}
