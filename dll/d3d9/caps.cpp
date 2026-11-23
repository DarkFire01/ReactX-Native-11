/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GetDeviceCaps and the software device registration
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9.hpp"

/* Which reported bits survive into what applications see; the extended object keeps a few more */
static constexpr DWORD D9CapsShown = 0x02020000;
static constexpr DWORD D9CapsShownEx = 0x02020800;
static constexpr DWORD D9Caps2Shown = 0xF2020000;
static constexpr DWORD D9Caps3Shown = 0x000003A0;
static constexpr DWORD D9Caps3ShownEx = 0x000007A0;

/* Runtime private bits in the driver's Caps2 that decide the presentation intervals */
static constexpr DWORD D9Caps2QueuedFlips = 0x00200000;
static constexpr DWORD D9Caps2Presents = 0x00400000;

/* Device bits the runtime always clears, and the one it sets for hardware rasterizers */
static constexpr DWORD D9DevCapsCleared = 0x06000000;
static constexpr DWORD D9PrimitiveMiscCleared = 0x00002000;

/* A Direct3D 7 vertex fog cap the runtime moves into RasterCaps */
static constexpr DWORD D9VertexCapsFog = 0x00000004;

/* Copies the driver's caps through the runtime's masks into what GetDeviceCaps returns */
static
VOID
D9PresentCaps(
    _In_ const D9Adapter* Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _In_ BOOLEAN IsEx,
    _In_ BOOLEAN CanCalibrateGamma,
    _Out_ D3DCAPS9* Caps)
{
    const D3DCAPS9* Driver = &Adapter->DriverCaps;

    *Caps = *Driver;
    if (DeviceType == D3DDEVTYPE_HAL)
        Caps->DevCaps |= D3DDEVCAPS_HWRASTERIZATION;

    Caps->DeviceType = DeviceType;
    Caps->AdapterOrdinal = Adapter->Ordinal;
    Caps->MasterAdapterOrdinal = Adapter->MasterOrdinal;
    Caps->AdapterOrdinalInGroup = Adapter->OrdinalInGroup;
    Caps->NumberOfAdaptersInGroup = Adapter->AdaptersInGroup;

    Caps->Caps = Driver->Caps & (IsEx ? D9CapsShownEx : D9CapsShown);
    Caps->Caps2 = Driver->Caps2 & D9Caps2Shown;
    if (CanCalibrateGamma)
        Caps->Caps2 |= D3DCAPS2_CANCALIBRATEGAMMA;
    Caps->Caps3 = Driver->Caps3 & (IsEx ? D9Caps3ShownEx : D9Caps3Shown);

    Caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE;
    if ((Driver->Caps2 & D9Caps2QueuedFlips) != 0)
    {
        Caps->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_TWO | D3DPRESENT_INTERVAL_THREE |
                                      D3DPRESENT_INTERVAL_FOUR;
    }
    if ((Driver->Caps2 & D9Caps2Presents) != 0)
        Caps->PresentationIntervals |= D3DPRESENT_INTERVAL_IMMEDIATE;

    Caps->DevCaps &= ~D9DevCapsCleared;
    Caps->PrimitiveMiscCaps &= ~D9PrimitiveMiscCleared;
    if ((Caps->VertexProcessingCaps & D9VertexCapsFog) != 0)
    {
        Caps->RasterCaps |= D3DPRASTERCAPS_FOGVERTEX;
        Caps->VertexProcessingCaps &= ~D9VertexCapsFog;
    }

    if (Caps->MaxPointSize == 0.0f)
        Caps->MaxPointSize = 1.0f;
}

/* Hardware without a format it can render 3D to is not a Direct3D device */
static
BOOLEAN
D9HasAcceleratedFormat(
    _In_ const D9Adapter* Adapter)
{
    for (UINT Index = 0; Index < Adapter->FormatCount; Index++)
    {
        if ((Adapter->Formats[Index].Operations & FORMATOP_3DACCELERATION) != 0)
            return TRUE;
    }
    return FALSE;
}

STDMETHODIMP
D9Enum::GetDeviceCaps(
    _In_ UINT Adapter,
    _In_ D3DDEVTYPE DeviceType,
    _Out_ D3DCAPS9* Caps)
{
    D9LockScope Scope(&Lock);
    const D9Adapter* Table;

    /* The null reference device answers even when there is no display at all */
    if (Adapter >= AdapterCount && !(AdapterCount == 0 && DeviceType == D3DDEVTYPE_NULLREF && Adapter == 0))
    {
        D9_TRACE_ONCE(L"GetDeviceCaps: adapter %u does not exist", Adapter);
        return D3DERR_INVALIDCALL;
    }

    switch (DeviceType)
    {
    case D3DDEVTYPE_HAL:
        if (SoftwareOnly)
        {
            ZeroMemory(Caps, sizeof(*Caps));
            return D3DERR_NOTAVAILABLE;
        }
        break;

    case D3DDEVTYPE_REF:
    case D3DDEVTYPE_NULLREF:
        Table = Adapter < AdapterCount ? ReferenceTable(Adapter) : nullptr;
        if (Table == nullptr || !D9HasAcceleratedFormat(Table))
        {
            ZeroMemory(Caps, sizeof(*Caps));
            return D3DERR_NOTAVAILABLE;
        }
        D9PresentCaps(Table, DeviceType, IsEx, CanCalibrateGamma, Caps);
        return S_OK;

    case D3DDEVTYPE_SW:
        ZeroMemory(Caps, sizeof(*Caps));
        return D3DERR_NOTAVAILABLE;

    default:
        D9_TRACE_ONCE(L"GetDeviceCaps: invalid device type %u", DeviceType);
        ZeroMemory(Caps, sizeof(*Caps));
        return D3DERR_INVALIDDEVICE;
    }

    if (!D9HasAcceleratedFormat(&Adapters[Adapter]))
    {
        ZeroMemory(Caps, sizeof(*Caps));
        return D3DERR_NOTAVAILABLE;
    }

    D9PresentCaps(&Adapters[Adapter], DeviceType, IsEx, CanCalibrateGamma, Caps);
    return S_OK;
}

/* Software rasterizer plug-ins are refused until the software pipeline exists */
STDMETHODIMP
D9Enum::RegisterSoftwareDevice(
    _In_ VOID* InitializeFunction)
{
    D9LockScope Scope(&Lock);

    if (InitializeFunction == nullptr)
    {
        D9_TRACE_ONCE(L"RegisterSoftwareDevice: no initialization function");
        return D3DERR_INVALIDCALL;
    }
    if (AdapterCount == 0)
        return D3DERR_NOTAVAILABLE;

    D9_TRACE_ONCE(L"RegisterSoftwareDevice: software devices are not supported yet");
    return D3DERR_NOTAVAILABLE;
}

/* Displays on the same adapter form a group led by the first of them */
VOID
D9Enum::GroupAdapters(VOID)
{
    static constexpr UINT Ungrouped = D9MaxAdapters;

    for (UINT Index = 0; Index < AdapterCount; Index++)
        Adapters[Index].MasterOrdinal = Ungrouped;

    for (UINT Master = 0; Master < AdapterCount; Master++)
    {
        D9Adapter* Lead = &Adapters[Master];
        UINT Members = 0;

        if (Lead->MasterOrdinal != Ungrouped)
            continue;

        Lead->MasterOrdinal = Master;
        Lead->OrdinalInGroup = 0;
        for (UINT Index = 0; Index < AdapterCount; Index++)
        {
            D9Adapter* Member = &Adapters[Index];

            if (Member->MasterOrdinal != Ungrouped || Member->GroupLuid.LowPart != Lead->GroupLuid.LowPart ||
                Member->GroupLuid.HighPart != Lead->GroupLuid.HighPart)
            {
                continue;
            }
            Members += 1;
            Member->MasterOrdinal = Master;
            Member->OrdinalInGroup = Members;
        }
        Lead->AdaptersInGroup = Members + 1;
    }
}

/* Registry switches read once per object */
VOID
D9Enum::ReadSwitches(VOID)
{
    CHAR Path[MAX_PATH + 1] = {};
    DWORD Size = MAX_PATH;
    DWORD Value = 0;
    DWORD Type = 0;
    HKEY Key;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\DirectDraw\\GammaCalibrator", 0, KEY_QUERY_VALUE,
                      &Key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExA(Key, "Path", nullptr, &Type, reinterpret_cast<BYTE*>(Path), &Size) == ERROR_SUCCESS)
        {
            Path[Size < MAX_PATH ? Size : MAX_PATH] = '\0';
            CanCalibrateGamma = Type == REG_SZ && Path[0] != '\0';
        }
        RegCloseKey(Key);
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D\\Drivers", 0, KEY_QUERY_VALUE, &Key) ==
        ERROR_SUCCESS)
    {
        Size = sizeof(Value);
        if (RegQueryValueExA(Key, "SoftwareOnly", nullptr, &Type, reinterpret_cast<BYTE*>(&Value), &Size) ==
                ERROR_SUCCESS &&
            Type == REG_DWORD && Value != 0)
        {
            SoftwareOnly = TRUE;
            D9Trace(L"SoftwareOnly is set; the hardware device is not offered");
        }
        RegCloseKey(Key);
    }
}
