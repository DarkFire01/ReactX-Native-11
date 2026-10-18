/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What each format can do on this device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "formatsupport.hpp"

HRESULT
RxDevice::GetFormatSupport(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Support)
{
    UINT Driver = 0;

    *Support = 0;

    /* Typeless buffers are the runtime's alone */
    if (Format == DXGI_FORMAT_UNKNOWN)
        return RxDeriveFormatSupport(Format, 0, Support) ? S_OK : E_FAIL;

    /* Formats the runtime does not know never reach the driver */
    if (!RxDeriveFormatSupport(Format, 0, Support))
        return E_FAIL;

    /* The driver is asked once per format; formats it disowns do not exist on this device */
    AcquireSRWLockExclusive(&FormatLock);
    if (!(FormatAsked[Format / 64] & (1ull << (Format % 64))))
    {
        DeviceFuncs.pfnCheckFormatSupport(DriverDevice, Format, &Driver);
        FormatDriverCaps[Format] = Driver;
        FormatAsked[Format / 64] |= 1ull << (Format % 64);
    }
    Driver = FormatDriverCaps[Format];
    ReleaseSRWLockExclusive(&FormatLock);

    if (Driver & D3D10_DDI_FORMAT_SUPPORT_NOT_SUPPORTED)
    {
        *Support = 0;
        return E_FAIL;
    }

    return RxDeriveFormatSupport(Format, Driver, Support) ? S_OK : E_FAIL;
}

HRESULT
RxDevice::GetFormatSupport2(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* Support)
{
    UINT First;
    UINT Driver;

    *Support = 0;

    /* The first query also asks the driver, and fails for formats this device does not have */
    if (FAILED(GetFormatSupport(Format, &First)))
        return E_FAIL;

    AcquireSRWLockShared(&FormatLock);
    Driver = FormatDriverCaps[Format];
    ReleaseSRWLockShared(&FormatLock);

    return RxDeriveFormatSupport2(Format, Driver, Support) ? S_OK : E_FAIL;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::CheckFormatSupport(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* pFormatSupport)
{
    if (pFormatSupport == nullptr)
        return E_INVALIDARG;

    return GetFormatSupport(Format, pFormatSupport);
}
