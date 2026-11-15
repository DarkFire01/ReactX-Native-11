/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Rendering parameters
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "params.hpp"

static constexpr GUID RxParamsIid = { 0x3c1d77e2, 0x4b9a, 0x4f05, { 0x8a, 0x6e, 0x21, 0xd4, 0x9b, 0x70, 0x0e, 0x5c } };

static const WCHAR RxAvalonKey[] = L"Software\\Microsoft\\Avalon.Graphics";

FLOAT
RxCompatibleGamma(VOID)
{
    UINT Contrast = 1400;

    if (!SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &Contrast, 0))
        Contrast = 1400;
    return Contrast * 0.001f;
}

UINT32
RxCompatibleSmoothing(VOID)
{
    BOOL Smoothing = TRUE;
    UINT Type = FE_FONTSMOOTHINGCLEARTYPE;

    if (SystemParametersInfoW(SPI_GETFONTSMOOTHING, 0, &Smoothing, 0) && !Smoothing)
        return 1;
    if (SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE, 0, &Type, 0) && Type == FE_FONTSMOOTHINGSTANDARD)
        return 2;
    return 3;
}

static
BOOLEAN
RxReadDword(
    _In_opt_ HKEY Key,
    _In_z_ PCWSTR Name,
    _Out_ DWORD* Value)
{
    DWORD Size = sizeof(*Value);
    DWORD Type;

    if (Key == nullptr)
        return FALSE;
    return RegQueryValueExW(Key, Name, nullptr, &Type, reinterpret_cast<BYTE*>(Value), &Size) == ERROR_SUCCESS &&
           Type == REG_DWORD;
}

/* Drops a leading \\.\ so the name works as a key */
static
PCWSTR
RxSkipDevicePrefix(
    _In_z_ PCWSTR Name)
{
    while (*Name == L'\\')
        Name++;
    if (Name[0] == L'.' && Name[1] == L'\\')
        Name += 2;
    return Name;
}

static
HKEY
RxOpenDisplayKey(
    _In_opt_ HKEY Root,
    _In_opt_z_ PCWSTR Device)
{
    HKEY Key;

    if (Root == nullptr || Device == nullptr)
        return nullptr;
    if (RegOpenKeyExW(Root, Device, 0, KEY_READ, &Key) != ERROR_SUCCESS)
        return nullptr;
    return Key;
}

VOID
RxRenderingParams::SystemValues(
    _In_opt_z_ PCWSTR DeviceName,
    _Out_ RxParamValues* Values)
{
    HKEY Machine = nullptr;
    HKEY User = nullptr;
    HKEY MachineDisplay = nullptr;
    HKEY UserDisplay = nullptr;
    BOOLEAN GeometryFromRegistry = FALSE;
    DWORD Value;

    Values->Gamma = 1.8f;
    Values->EnhancedContrast = 0.5f;
    Values->GrayscaleEnhancedContrast = 1.0f;
    Values->ClearTypeLevel = 1.0f;
    Values->PixelGeometry = DWRITE_PIXEL_GEOMETRY_RGB;
    Values->RenderingMode = DWRITE_RENDERING_MODE1_DEFAULT;
    Values->GridFitMode = DWRITE_GRID_FIT_MODE_DEFAULT;
    Values->IsModern = TRUE;
    Values->CompatibleGamma = RxCompatibleGamma();
    Values->CompatibleSmoothing = RxCompatibleSmoothing();

    RegOpenKeyExW(HKEY_LOCAL_MACHINE, RxAvalonKey, 0, KEY_READ, &Machine);
    RegOpenKeyExW(HKEY_CURRENT_USER, RxAvalonKey, 0, KEY_READ, &User);

    if (Machine != nullptr || User != nullptr)
    {
        PCWSTR Device = DeviceName != nullptr ? RxSkipDevicePrefix(DeviceName) : nullptr;

        MachineDisplay = RxOpenDisplayKey(Machine, Device);
        UserDisplay = RxOpenDisplayKey(User, Device);

        /* Fall back to the settings of the primary display */
        if ((Machine != nullptr && MachineDisplay == nullptr) || (User != nullptr && UserDisplay == nullptr))
        {
            DISPLAY_DEVICEW Display = { sizeof(Display) };

            for (DWORD Index = 0; EnumDisplayDevicesW(nullptr, Index, &Display, 0); Index++)
            {
                if (Display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
                {
                    PCWSTR Primary = RxSkipDevicePrefix(Display.DeviceName);

                    if (Device == nullptr || _wcsicmp(Device, Primary) != 0)
                    {
                        if (MachineDisplay == nullptr)
                            MachineDisplay = RxOpenDisplayKey(Machine, Primary);
                        if (UserDisplay == nullptr)
                            UserDisplay = RxOpenDisplayKey(User, Primary);
                    }
                    break;
                }
                Display.cb = sizeof(Display);
            }
        }

        if ((RxReadDword(UserDisplay, L"GammaLevel", &Value) || RxReadDword(MachineDisplay, L"GammaLevel", &Value)) &&
            Value >= 1000 && Value <= 2200)
            Values->Gamma = Value * 0.001f;

        if ((RxReadDword(UserDisplay, L"PixelStructure", &Value) ||
             RxReadDword(MachineDisplay, L"PixelStructure", &Value)) && Value <= 2)
        {
            Values->PixelGeometry = static_cast<DWRITE_PIXEL_GEOMETRY>(Value);
            GeometryFromRegistry = TRUE;
        }

        if (RxReadDword(UserDisplay, L"ClearTypeLevel", &Value) && Value <= 100)
            Values->ClearTypeLevel = Value * 0.01f;
        if (RxReadDword(UserDisplay, L"EnhancedContrastLevel", &Value) && Value <= 400)
            Values->EnhancedContrast = Value * 0.01f;
        if (RxReadDword(UserDisplay, L"GrayscaleEnhancedContrastLevel", &Value) && Value <= 400)
            Values->GrayscaleEnhancedContrast = Value * 0.01f;
    }

    if (!GeometryFromRegistry)
    {
        UINT Orientation;

        if (SystemParametersInfoW(SPI_GETFONTSMOOTHINGORIENTATION, 0, &Orientation, 0) &&
            Orientation == FE_FONTSMOOTHINGORIENTATIONBGR)
            Values->PixelGeometry = DWRITE_PIXEL_GEOMETRY_BGR;
    }

    if (UserDisplay != nullptr)
        RegCloseKey(UserDisplay);
    if (MachineDisplay != nullptr)
        RegCloseKey(MachineDisplay);
    if (User != nullptr)
        RegCloseKey(User);
    if (Machine != nullptr)
        RegCloseKey(Machine);
}

VOID
RxRenderingParams::ReadValues(
    _In_opt_ IDWriteRenderingParams* Params,
    _Out_ RxParamValues* Values)
{
    RxRenderingParams* Ours = nullptr;
    RxComPtr<IDWriteRenderingParams1> Params1;
    RxComPtr<IDWriteRenderingParams2> Params2;
    RxComPtr<IDWriteRenderingParams3> Params3;

    if (Params == nullptr)
    {
        SystemValues(nullptr, Values);
        return;
    }

    if (SUCCEEDED(Params->QueryInterface(RxParamsIid, reinterpret_cast<void**>(&Ours))))
    {
        *Values = Ours->Fields;
        Ours->Release();
        return;
    }

    Values->Gamma = Params->GetGamma();
    Values->EnhancedContrast = Params->GetEnhancedContrast();
    Values->ClearTypeLevel = Params->GetClearTypeLevel();
    Values->PixelGeometry = Params->GetPixelGeometry();
    Values->RenderingMode = static_cast<DWRITE_RENDERING_MODE1>(Params->GetRenderingMode());
    Values->GrayscaleEnhancedContrast = 1.0f;
    Values->GridFitMode = DWRITE_GRID_FIT_MODE_DEFAULT;
    Values->CompatibleGamma = RxCompatibleGamma();
    Values->CompatibleSmoothing = RxCompatibleSmoothing();
    Values->IsModern = FALSE;

    if (SUCCEEDED(Params->QueryInterface(__uuidof(IDWriteRenderingParams1), Params1.PutVoid())))
        Values->GrayscaleEnhancedContrast = Params1->GetGrayscaleEnhancedContrast();
    if (SUCCEEDED(Params->QueryInterface(__uuidof(IDWriteRenderingParams2), Params2.PutVoid())))
        Values->GridFitMode = Params2->GetGridFitMode();
    if (SUCCEEDED(Params->QueryInterface(__uuidof(IDWriteRenderingParams3), Params3.PutVoid())))
    {
        Values->RenderingMode = Params3->GetRenderingMode1();
        Values->IsModern = TRUE;
    }
}

HRESULT
RxRenderingParams::Create(
    _In_ const RxParamValues& Values,
    _Outptr_ RxRenderingParams** Params)
{
    *Params = new (std::nothrow) RxRenderingParams();
    if (*Params == nullptr)
        return E_OUTOFMEMORY;
    (*Params)->Fields = Values;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxRenderingParams::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxParamsIid)
    {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteRenderingParams) ||
        riid == __uuidof(IDWriteRenderingParams1) || riid == __uuidof(IDWriteRenderingParams2) ||
        riid == __uuidof(IDWriteRenderingParams3))
    {
        *ppvObject = static_cast<IDWriteRenderingParams3*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxRenderingParams)

FLOAT
STDMETHODCALLTYPE
RxRenderingParams::GetGamma(VOID)
{
    return Fields.Gamma;
}

FLOAT
STDMETHODCALLTYPE
RxRenderingParams::GetEnhancedContrast(VOID)
{
    return Fields.EnhancedContrast;
}

FLOAT
STDMETHODCALLTYPE
RxRenderingParams::GetClearTypeLevel(VOID)
{
    return Fields.ClearTypeLevel;
}

DWRITE_PIXEL_GEOMETRY
STDMETHODCALLTYPE
RxRenderingParams::GetPixelGeometry(VOID)
{
    return Fields.PixelGeometry;
}

/* The legacy interface has no downsampled symmetric mode */
DWRITE_RENDERING_MODE
STDMETHODCALLTYPE
RxRenderingParams::GetRenderingMode(VOID)
{
    if (Fields.RenderingMode == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED)
        return DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    return static_cast<DWRITE_RENDERING_MODE>(Fields.RenderingMode);
}

FLOAT
STDMETHODCALLTYPE
RxRenderingParams::GetGrayscaleEnhancedContrast(VOID)
{
    return Fields.GrayscaleEnhancedContrast;
}

DWRITE_GRID_FIT_MODE
STDMETHODCALLTYPE
RxRenderingParams::GetGridFitMode(VOID)
{
    return Fields.GridFitMode;
}

DWRITE_RENDERING_MODE1
STDMETHODCALLTYPE
RxRenderingParams::GetRenderingMode1(VOID)
{
    return Fields.RenderingMode;
}
