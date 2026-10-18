/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXGIOutput6::GetDesc1: color depth, color space and the monitor's colorimetry
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

/* What GetDesc1 reports beyond the plain output desc */
struct RxOutputColor
{
    UINT BitsPerColor = 8;
    DXGI_COLOR_SPACE_TYPE ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

    /* sRGB until the monitor says otherwise */
    FLOAT Red[2] = { 0.64f, 0.33f };
    FLOAT Green[2] = { 0.30f, 0.60f };
    FLOAT Blue[2] = { 0.15f, 0.06f };
    FLOAT White[2] = { 0.3127f, 0.3290f };
    FLOAT MinLuminance = 0.0f;
    FLOAT MaxLuminance = 0.0f;
    FLOAT MaxFullFrameLuminance = 0.0f;
};

/* The active path whose source is this GDI device */
static
BOOLEAN
RxFindTarget(
    _In_z_ PCWSTR DeviceName,
    _Out_ DISPLAYCONFIG_PATH_TARGET_INFO* Target)
{
    std::vector<DISPLAYCONFIG_PATH_INFO> Paths;
    std::vector<DISPLAYCONFIG_MODE_INFO> Modes;
    DISPLAYCONFIG_SOURCE_DEVICE_NAME Source = {};
    const RxDisplayConfigRoutines* Config = RxGetDisplayConfig();
    UINT32 PathCount = 0;
    UINT32 ModeCount = 0;

    if (Config == nullptr || Config->GetBufferSizes(QDC_ONLY_ACTIVE_PATHS, &PathCount, &ModeCount) != ERROR_SUCCESS)
        return FALSE;

    Paths.resize(PathCount);
    Modes.resize(ModeCount);
    if (Config->Query(QDC_ONLY_ACTIVE_PATHS, &PathCount, Paths.data(), &ModeCount, Modes.data(), nullptr) !=
        ERROR_SUCCESS)
    {
        return FALSE;
    }

    for (UINT32 Index = 0; Index < PathCount; Index++)
    {
        Source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        Source.header.size = sizeof(Source);
        Source.header.adapterId = Paths[Index].sourceInfo.adapterId;
        Source.header.id = Paths[Index].sourceInfo.id;
        if (Config->GetDeviceInfo(&Source.header) == ERROR_SUCCESS &&
            _wcsicmp(Source.viewGdiDeviceName, DeviceName) == 0)
        {
            *Target = Paths[Index].targetInfo;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * The EDID Windows keeps for the monitor, found from its device path:
 * \\?\DISPLAY#<model>#<instance>#{guid} lives under Enum\DISPLAY\<model>\<instance>.
 */
static
BOOLEAN
RxReadEdid(
    _In_z_ PCWSTR DevicePath,
    _Out_ std::vector<BYTE>& Edid)
{
    WCHAR Key[512] = L"SYSTEM\\CurrentControlSet\\Enum\\";
    WCHAR Path[256];
    DWORD Size = 0;
    PWSTR Cursor;
    PWSTR Brace;
    HKEY Handle;
    LSTATUS Status;

    Edid.clear();
    if (wcsncmp(DevicePath, L"\\\\?\\", 4) != 0 || wcscpy_s(Path, DevicePath + 4) != 0)
        return FALSE;

    /* Drop the interface class and turn the separators into key separators */
    Brace = wcsstr(Path, L"#{");
    if (Brace == nullptr)
        return FALSE;
    *Brace = UNICODE_NULL;
    for (Cursor = Path; *Cursor != UNICODE_NULL; Cursor++)
    {
        if (*Cursor == L'#')
            *Cursor = L'\\';
    }

    if (wcscat_s(Key, Path) != 0 || wcscat_s(Key, L"\\Device Parameters") != 0)
        return FALSE;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, Key, 0, KEY_QUERY_VALUE, &Handle) != ERROR_SUCCESS)
        return FALSE;

    Status = RegQueryValueExW(Handle, L"EDID", nullptr, nullptr, nullptr, &Size);
    if (Status == ERROR_SUCCESS && Size >= 128)
    {
        Edid.resize(Size);
        Status = RegQueryValueExW(Handle, L"EDID", nullptr, nullptr, Edid.data(), &Size);
    }
    RegCloseKey(Handle);

    return Status == ERROR_SUCCESS && Edid.size() >= 128;
}

/* EDID chromaticity: 10 bit fractions, 8 high bits in their own byte and 2 low bits packed */
static
FLOAT
RxChromaticity(
    _In_ BYTE High,
    _In_ BYTE LowBits,
    _In_ UINT Shift)
{
    return (FLOAT)(((UINT)High << 2) | ((LowBits >> Shift) & 3)) / 1024.0f;
}

static
FLOAT
RxHalfToFloat(
    _In_ USHORT Half)
{
    INT Exponent = (Half >> 10) & 0x1F;
    FLOAT Mantissa = (Half & 0x3FF) / 1024.0f;

    if (Exponent == 0)
        return ldexpf(Mantissa, -14);
    if (Exponent == 0x1F)
        return 0.0f;

    return ldexpf(1.0f + Mantissa, Exponent - 15);
}

static
VOID
RxParseEdid(
    _In_ const std::vector<BYTE>& Edid,
    _Inout_ RxOutputColor* Color)
{
    const BYTE* Base = Edid.data();
    SIZE_T Block;
    UINT Offset;
    UINT End;

    Color->Red[0] = RxChromaticity(Base[27], Base[25], 6);
    Color->Red[1] = RxChromaticity(Base[28], Base[25], 4);
    Color->Green[0] = RxChromaticity(Base[29], Base[25], 2);
    Color->Green[1] = RxChromaticity(Base[30], Base[25], 0);
    Color->Blue[0] = RxChromaticity(Base[31], Base[26], 6);
    Color->Blue[1] = RxChromaticity(Base[32], Base[26], 4);
    Color->White[0] = RxChromaticity(Base[33], Base[26], 2);
    Color->White[1] = RxChromaticity(Base[34], Base[26], 0);

    /* CTA-861 extensions may carry an HDR static metadata block with the luminance range */
    for (Block = 128; Block + 128 <= Edid.size(); Block += 128)
    {
        const BYTE* Ext = Base + Block;

        /* DisplayID 2 extensions, as laptop panels use, keep it as half floats in display parameters */
        if (Ext[0] == 0x70 && Ext[1] >= 0x20)
        {
            End = std::min<UINT>(5 + Ext[2], 127);
            for (Offset = 5; Offset + 3 <= End;)
            {
                UINT Length = Ext[Offset + 2];

                if (Ext[Offset] == 0x21 && Length >= 27 && Offset + 3 + Length <= 128)
                {
                    const BYTE* Params = Ext + Offset + 3;

                    Color->MaxFullFrameLuminance = RxHalfToFloat((USHORT)(Params[21] | (Params[22] << 8)));
                    Color->MaxLuminance = RxHalfToFloat((USHORT)(Params[23] | (Params[24] << 8)));
                    Color->MinLuminance = RxHalfToFloat((USHORT)(Params[25] | (Params[26] << 8)));
                    return;
                }

                Offset += 3 + Length;
            }
            continue;
        }

        if (Ext[0] != 0x02)
            continue;

        End = Ext[2];
        for (Offset = 4; Offset < End && Offset < 127;)
        {
            UINT Tag = Ext[Offset] >> 5;
            UINT Length = Ext[Offset] & 0x1F;

            /* Extended tag 6 is the HDR static metadata data block */
            if (Tag == 7 && Length >= 3 && Ext[Offset + 1] == 6 && Offset + Length < 128)
            {
                UINT MaxCode = (Length >= 4) ? Ext[Offset + 4] : 0;
                UINT AverageCode = (Length >= 5) ? Ext[Offset + 5] : 0;
                UINT MinCode = (Length >= 6) ? Ext[Offset + 6] : 0;

                Color->MaxLuminance = MaxCode ? 50.0f * powf(2.0f, MaxCode / 32.0f) : 0.0f;
                Color->MaxFullFrameLuminance = AverageCode ? 50.0f * powf(2.0f, AverageCode / 32.0f) : 0.0f;
                Color->MinLuminance = MinCode ? Color->MaxLuminance * powf(MinCode / 255.0f, 2.0f) / 100.0f : 0.0f;
                return;
            }

            Offset += Length + 1;
        }
    }
}

static
VOID
RxQueryOutputColor(
    _In_z_ PCWSTR DeviceName,
    _Out_ RxOutputColor* Color)
{
    DISPLAYCONFIG_PATH_TARGET_INFO Target;
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO Advanced = {};
    DISPLAYCONFIG_TARGET_DEVICE_NAME Name = {};
    const RxDisplayConfigRoutines* Config;
    std::vector<BYTE> Edid;

    *Color = {};
    if (!RxFindTarget(DeviceName, &Target))
        return;

    /* RxFindTarget only succeeds with the calls present */
    Config = RxGetDisplayConfig();

    Advanced.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    Advanced.header.size = sizeof(Advanced);
    Advanced.header.adapterId = Target.adapterId;
    Advanced.header.id = Target.id;
    if (Config->GetDeviceInfo(&Advanced.header) == ERROR_SUCCESS)
    {
        if (Advanced.bitsPerColorChannel != 0)
            Color->BitsPerColor = Advanced.bitsPerColorChannel;
        if (Advanced.advancedColorEnabled)
            Color->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    }

    Name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    Name.header.size = sizeof(Name);
    Name.header.adapterId = Target.adapterId;
    Name.header.id = Target.id;
    if (Config->GetDeviceInfo(&Name.header) == ERROR_SUCCESS && RxReadEdid(Name.monitorDevicePath, Edid))
        RxParseEdid(Edid, Color);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiOutput::GetDesc1(
    _Out_ DXGI_OUTPUT_DESC1* pDesc)
{
    DXGI_OUTPUT_DESC Desc;
    RxOutputColor Color;
    HRESULT hr;

    if (pDesc == nullptr)
        return E_INVALIDARG;

    *pDesc = {};
    hr = GetDesc(&Desc);
    if (FAILED(hr))
        return hr;

    wcscpy_s(pDesc->DeviceName, Desc.DeviceName);
    pDesc->DesktopCoordinates = Desc.DesktopCoordinates;
    pDesc->AttachedToDesktop = Desc.AttachedToDesktop;
    pDesc->Rotation = Desc.Rotation;
    pDesc->Monitor = Desc.Monitor;

    RxQueryOutputColor(Desc.DeviceName, &Color);
    pDesc->BitsPerColor = Color.BitsPerColor;
    pDesc->ColorSpace = Color.ColorSpace;
    memcpy(pDesc->RedPrimary, Color.Red, sizeof(Color.Red));
    memcpy(pDesc->GreenPrimary, Color.Green, sizeof(Color.Green));
    memcpy(pDesc->BluePrimary, Color.Blue, sizeof(Color.Blue));
    memcpy(pDesc->WhitePoint, Color.White, sizeof(Color.White));
    pDesc->MinLuminance = Color.MinLuminance;
    pDesc->MaxLuminance = Color.MaxLuminance;
    pDesc->MaxFullFrameLuminance = Color.MaxFullFrameLuminance;
    return S_OK;
}
