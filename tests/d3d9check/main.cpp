/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 runtime behavior, printed as comparable facts
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d9check [-runtime system|reactx|<path>] [check ...]
 */

#include <windows.h>
#include <d3d9.h>
#include <d3d9on12.h>
#include <dwmapi.h>

#include <cstdio>
#include <string>
#include <vector>

typedef HRESULT (WINAPI* PFN_NC_CREATE_EX)(UINT, IDirect3D9Ex**);
typedef IDirect3D9* (WINAPI* PFN_NC_CREATE)(UINT);

struct NineCheck
{
    HMODULE Module = NULL;
    PFN_NC_CREATE Create = nullptr;
    PFN_NC_CREATE_EX CreateEx = nullptr;
    IDirect3D9Ex* Ex = nullptr;
    IDirect3D9* Nine = nullptr;
};

typedef VOID (*PNC_CHECK)(_Inout_ NineCheck* Check);

struct NineCheckInfo
{
    PCSTR Name;
    PNC_CHECK Run;
};

/* Every format a Direct3D 9 application can ask about, FOURCC extensions included */
static const D3DFORMAT NcFormats[] =
{
    D3DFMT_R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A1R5G5B5,
    D3DFMT_A4R4G4B4, D3DFMT_R3G3B2, D3DFMT_A8, D3DFMT_A8R3G3B2, D3DFMT_X4R4G4B4, D3DFMT_A2B10G10R10,
    D3DFMT_A8B8G8R8, D3DFMT_X8B8G8R8, D3DFMT_G16R16, D3DFMT_A2R10G10B10, D3DFMT_A16B16G16R16, D3DFMT_A8P8,
    D3DFMT_P8, D3DFMT_L8, D3DFMT_A8L8, D3DFMT_A4L4, D3DFMT_V8U8, D3DFMT_L6V5U5, D3DFMT_X8L8V8U8, D3DFMT_Q8W8V8U8,
    D3DFMT_V16U16, D3DFMT_A2W10V10U10, D3DFMT_UYVY, D3DFMT_R8G8_B8G8, D3DFMT_YUY2, D3DFMT_G8R8_G8B8, D3DFMT_DXT1,
    D3DFMT_DXT2, D3DFMT_DXT3, D3DFMT_DXT4, D3DFMT_DXT5, D3DFMT_D16_LOCKABLE, D3DFMT_D32, D3DFMT_D15S1, D3DFMT_D24S8,
    D3DFMT_D24X8, D3DFMT_D24X4S4, D3DFMT_D16, D3DFMT_D32F_LOCKABLE, D3DFMT_D24FS8, D3DFMT_D32_LOCKABLE,
    D3DFMT_S8_LOCKABLE, D3DFMT_L16, D3DFMT_VERTEXDATA, D3DFMT_INDEX16, D3DFMT_INDEX32, D3DFMT_Q16W16V16U16,
    D3DFMT_MULTI2_ARGB8, D3DFMT_R16F, D3DFMT_G16R16F, D3DFMT_A16B16G16R16F, D3DFMT_R32F, D3DFMT_G32R32F,
    D3DFMT_A32B32G32R32F, D3DFMT_CxV8U8, D3DFMT_A1, D3DFMT_A2B10G10R10_XR_BIAS, D3DFMT_BINARYBUFFER,
    (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), (D3DFORMAT)MAKEFOURCC('D', 'F', '1', '6'),
    (D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4'), (D3DFORMAT)MAKEFOURCC('R', 'A', 'W', 'Z'),
    (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L'), (D3DFORMAT)MAKEFOURCC('A', 'T', 'I', '1'),
    (D3DFORMAT)MAKEFOURCC('A', 'T', 'I', '2'), (D3DFORMAT)MAKEFOURCC('R', 'E', 'S', 'Z'),
    (D3DFORMAT)MAKEFOURCC('A', 'T', 'O', 'C'), (D3DFORMAT)MAKEFOURCC('S', 'S', 'A', 'A'),
    (D3DFORMAT)MAKEFOURCC('N', 'V', 'D', 'B'), (D3DFORMAT)MAKEFOURCC('N', 'V', '1', '2'),
    (D3DFORMAT)MAKEFOURCC('Y', 'V', '1', '2'), (D3DFORMAT)MAKEFOURCC('A', 'Y', 'U', 'V'),
    (D3DFORMAT)MAKEFOURCC('A', '2', 'M', '1'), (D3DFORMAT)MAKEFOURCC('B', 'C', '4', 'U'),
    (D3DFORMAT)MAKEFOURCC('B', 'C', '5', 'U'),
};

static const D3DFORMAT NcDisplayFormats[] =
{
    D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A1R5G5B5, D3DFMT_A2R10G10B10,
};

static const D3DDEVTYPE NcDeviceTypes[] = { D3DDEVTYPE_HAL, D3DDEVTYPE_REF, D3DDEVTYPE_SW, D3DDEVTYPE_NULLREF };

static const DWORD NcUsages[] =
{
    0, D3DUSAGE_RENDERTARGET, D3DUSAGE_DEPTHSTENCIL, D3DUSAGE_DYNAMIC, D3DUSAGE_AUTOGENMIPMAP,
    D3DUSAGE_DMAP, D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DUSAGE_QUERY_SRGBREAD, D3DUSAGE_QUERY_FILTER,
    D3DUSAGE_QUERY_SRGBWRITE, D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DUSAGE_QUERY_VERTEXTEXTURE,
    D3DUSAGE_QUERY_WRAPANDMIP, D3DUSAGE_NONSECURE, D3DUSAGE_TEXTAPI,
};

static const D3DRESOURCETYPE NcResourceTypes[] =
{
    D3DRTYPE_SURFACE, D3DRTYPE_VOLUME, D3DRTYPE_TEXTURE, D3DRTYPE_VOLUMETEXTURE, D3DRTYPE_CUBETEXTURE,
    D3DRTYPE_VERTEXBUFFER, D3DRTYPE_INDEXBUFFER,
};

static
VOID
NcCheckIdentifier(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    printf("adapters = %u\n", Count);
    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        D3DADAPTER_IDENTIFIER9 Id = {};
        LUID Luid = {};
        HRESULT hr = Check->Nine->GetAdapterIdentifier(Adapter, 0, &Id);

        printf("identifier[%u] = %08lX driver '%s' description '%s' name '%s' version %08lX.%08lX "
               "ids %04lX:%04lX:%08lX:%02lX guid %08lX-%04X-%04X whql %08lX\n",
               Adapter, hr, Id.Driver, Id.Description, Id.DeviceName, Id.DriverVersion.HighPart,
               Id.DriverVersion.LowPart, Id.VendorId, Id.DeviceId, Id.SubSysId, Id.Revision,
               Id.DeviceIdentifier.Data1, Id.DeviceIdentifier.Data2, Id.DeviceIdentifier.Data3, Id.WHQLLevel);
        printf("identifier_guid[%u] = %02X%02X%02X%02X%02X%02X%02X%02X\n", Adapter, Id.DeviceIdentifier.Data4[0],
               Id.DeviceIdentifier.Data4[1], Id.DeviceIdentifier.Data4[2], Id.DeviceIdentifier.Data4[3],
               Id.DeviceIdentifier.Data4[4], Id.DeviceIdentifier.Data4[5], Id.DeviceIdentifier.Data4[6],
               Id.DeviceIdentifier.Data4[7]);
        hr = Check->Nine->GetAdapterIdentifier(Adapter, D3DENUM_WHQL_LEVEL, &Id);
        printf("identifier_whql[%u] = %08lX whql %08lX\n", Adapter, hr, Id.WHQLLevel);
        hr = Check->Nine->GetAdapterIdentifier(Adapter, D3DENUM_NO_DRIVERVERSION, &Id);
        printf("identifier_noversion[%u] = %08lX version %08lX.%08lX guid %08lX-%02X%02X%02X%02X%02X%02X%02X%02X\n",
               Adapter, hr, Id.DriverVersion.HighPart, Id.DriverVersion.LowPart, Id.DeviceIdentifier.Data1,
               Id.DeviceIdentifier.Data4[0], Id.DeviceIdentifier.Data4[1], Id.DeviceIdentifier.Data4[2],
               Id.DeviceIdentifier.Data4[3], Id.DeviceIdentifier.Data4[4], Id.DeviceIdentifier.Data4[5],
               Id.DeviceIdentifier.Data4[6], Id.DeviceIdentifier.Data4[7]);
        for (DWORD Flags : { 1UL, 8UL, 0x80000000UL, 6UL })
        {
            memset(&Id, 0xCC, sizeof(Id));
            hr = Check->Nine->GetAdapterIdentifier(Adapter, Flags, &Id);
            printf("identifier_flags[%u][%08lX] = %08lX vendor %08lX\n", Adapter, Flags, hr, Id.VendorId);
        }
        printf("monitor[%u] = %s\n", Adapter, Check->Nine->GetAdapterMonitor(Adapter) != NULL ? "set" : "null");
        if (Check->Ex != nullptr)
        {
            hr = Check->Ex->GetAdapterLUID(Adapter, &Luid);
            printf("luid[%u] = %08lX %s\n", Adapter, hr, (Luid.LowPart | Luid.HighPart) != 0 ? "set" : "zero");
        }
    }
    printf("identifier_bad = %08lX\n", Check->Nine->GetAdapterIdentifier(Count, 0, nullptr));
}

static
VOID
NcCheckModes(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        D3DDISPLAYMODE Mode = {};
        HRESULT hr = Check->Nine->GetAdapterDisplayMode(Adapter, &Mode);

        printf("display[%u] = %08lX %ux%u@%u fmt %u\n", Adapter, hr, Mode.Width, Mode.Height, Mode.RefreshRate, Mode.Format);
        for (D3DFORMAT Format : NcDisplayFormats)
        {
            UINT Modes = Check->Nine->GetAdapterModeCount(Adapter, Format);

            printf("modes[%u][%u] = %u\n", Adapter, Format, Modes);
            for (UINT Index = 0; Index < Modes; Index++)
            {
                hr = Check->Nine->EnumAdapterModes(Adapter, Format, Index, &Mode);
                printf("mode[%u][%u][%u] = %08lX %ux%u@%u fmt %u\n", Adapter, Format, Index, hr, Mode.Width,
                       Mode.Height, Mode.RefreshRate, Mode.Format);
            }
            hr = Check->Nine->EnumAdapterModes(Adapter, Format, Modes, &Mode);
            printf("mode_past[%u][%u] = %08lX\n", Adapter, Format, hr);
        }
    }
}

/* The same mode questions asked of an object from Direct3DCreate9, which walks GDI's mode list */
static
VOID
NcCheckModesPlain(
    _Inout_ NineCheck* Check)
{
    static const D3DFORMAT Formats[] =
    {
        D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A2R10G10B10, D3DFMT_P8,
        D3DFMT_R8G8B8, D3DFMT_A16B16G16R16F, D3DFMT_UNKNOWN,
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    UINT Count = Plain->GetAdapterCount();

    for (UINT Adapter = 0; Adapter <= Count; Adapter++)
    {
        D3DDISPLAYMODE Mode = {};
        HRESULT hr = Plain->GetAdapterDisplayMode(Adapter, &Mode);

        printf("plain_display[%u] = %08lX %ux%u@%u fmt %u\n", Adapter, hr, Mode.Width, Mode.Height, Mode.RefreshRate,
               Mode.Format);
        for (D3DFORMAT Format : Formats)
        {
            UINT Modes = Plain->GetAdapterModeCount(Adapter, Format);

            printf("plain_modes[%u][%u] = %u\n", Adapter, Format, Modes);
            for (UINT Index = 0; Index <= Modes; Index++)
            {
                memset(&Mode, 0xCC, sizeof(Mode));
                hr = Plain->EnumAdapterModes(Adapter, Format, Index, &Mode);
                printf("plain_mode[%u][%u][%u] = %08lX %ux%u@%u fmt %u\n", Adapter, Format, Index, hr, Mode.Width,
                       Mode.Height, Mode.RefreshRate, Mode.Format);
            }
        }
    }
    Plain->Release();
}

/* Filters, structure sizes and rotation of the extended mode methods */
static
VOID
NcCheckModesEx(
    _Inout_ NineCheck* Check)
{
    static const D3DFORMAT Formats[] =
    {
        D3DFMT_X8R8G8B8, D3DFMT_A2R10G10B10, D3DFMT_A16B16G16R16F, D3DFMT_A2B10G10R10_XR_BIAS, D3DFMT_R5G6B5,
        D3DFMT_A8R8G8B8, D3DFMT_UNKNOWN,
    };
    UINT Count;

    if (Check->Ex == nullptr)
        return;

    Count = Check->Ex->GetAdapterCount();
    for (UINT Adapter = 0; Adapter <= Count; Adapter++)
    {
        D3DDISPLAYMODEEX Mode = {};
        D3DDISPLAYROTATION Rotation = static_cast<D3DDISPLAYROTATION>(0xCC);
        HRESULT hr;

        Mode.Size = sizeof(Mode);
        hr = Check->Ex->GetAdapterDisplayModeEx(Adapter, &Mode, &Rotation);
        printf("ex_display[%u] = %08lX %ux%u@%u fmt %u order %u rotation %u\n", Adapter, hr, Mode.Width, Mode.Height,
               Mode.RefreshRate, Mode.Format, Mode.ScanLineOrdering, Rotation);
        Mode.Size = 12;
        hr = Check->Ex->GetAdapterDisplayModeEx(Adapter, &Mode, nullptr);
        printf("ex_display_size[%u] = %08lX\n", Adapter, hr);

        for (D3DFORMAT Format : Formats)
        {
            for (UINT Order = 0; Order <= 3; Order++)
            {
                D3DDISPLAYMODEFILTER Filter = { sizeof(Filter), Format, static_cast<D3DSCANLINEORDERING>(Order) };
                UINT Modes = Check->Ex->GetAdapterModeCountEx(Adapter, &Filter);

                printf("ex_modes[%u][%u][%u] = %u\n", Adapter, Format, Order, Modes);
                for (UINT Index = 0; Index <= Modes && Index < 4; Index++)
                {
                    memset(&Mode, 0xCC, sizeof(Mode));
                    Mode.Size = sizeof(Mode);
                    hr = Check->Ex->EnumAdapterModesEx(Adapter, &Filter, Index, &Mode);
                    printf("ex_mode[%u][%u][%u][%u] = %08lX %ux%u@%u fmt %u order %u\n", Adapter, Format, Order, Index,
                           hr, Mode.Width, Mode.Height, Mode.RefreshRate, Mode.Format, Mode.ScanLineOrdering);
                }
                hr = Check->Ex->EnumAdapterModesEx(Adapter, &Filter, Modes, &Mode);
                printf("ex_mode_past[%u][%u][%u] = %08lX\n", Adapter, Format, Order, hr);
            }
        }

        {
            D3DDISPLAYMODEFILTER Filter = { 8, D3DFMT_X8R8G8B8, D3DSCANLINEORDERING_UNKNOWN };

            Mode.Size = sizeof(Mode);
            printf("ex_filter_size[%u] = %u %08lX\n", Adapter, Check->Ex->GetAdapterModeCountEx(Adapter, &Filter),
                   Check->Ex->EnumAdapterModesEx(Adapter, &Filter, 0, &Mode));
            printf("ex_filter_null[%u] = %u %08lX\n", Adapter, Check->Ex->GetAdapterModeCountEx(Adapter, nullptr),
                   Check->Ex->EnumAdapterModesEx(Adapter, nullptr, 0, &Mode));
            Filter.Size = sizeof(Filter);
            Mode.Size = 16;
            printf("ex_mode_size[%u] = %08lX\n", Adapter, Check->Ex->EnumAdapterModesEx(Adapter, &Filter, 0, &Mode));
        }
    }
}

static
VOID
NcCheckCaps(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        for (D3DDEVTYPE Type : NcDeviceTypes)
        {
            D3DCAPS9 Caps = {};
            HRESULT hr = Check->Nine->GetDeviceCaps(Adapter, Type, &Caps);

            printf("caps[%u][%u] = %08lX", Adapter, Type, hr);
            for (UINT Index = 0; Index < sizeof(Caps) / sizeof(DWORD); Index++)
                printf(" %08lX", reinterpret_cast<const DWORD*>(&Caps)[Index]);
            printf("\n");
        }
    }
}

/* The plain object masks a few caps the extended one shows; bad types and adapters zero the structure */
static
VOID
NcCheckCapsPlain(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    UINT Count = Plain->GetAdapterCount();

    for (UINT Adapter = 0; Adapter <= Count; Adapter++)
    {
        for (UINT Type : { 0U, 1U, 5U, 0x7FFFFFFFU })
        {
            D3DCAPS9 Caps;
            HRESULT hr;

            memset(&Caps, 0xCC, sizeof(Caps));
            hr = Plain->GetDeviceCaps(Adapter, static_cast<D3DDEVTYPE>(Type), &Caps);
            printf("plain_caps[%u][%u] = %08lX", Adapter, Type, hr);
            for (UINT Index = 0; Index < sizeof(Caps) / sizeof(DWORD); Index++)
                printf(" %08lX", reinterpret_cast<const DWORD*>(&Caps)[Index]);
            printf("\n");
        }
    }
    Plain->Release();
}

static
VOID
NcCheckDeviceType(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        for (D3DDEVTYPE Type : NcDeviceTypes)
        {
            for (D3DFORMAT Display : NcDisplayFormats)
            {
                for (D3DFORMAT Back : NcDisplayFormats)
                {
                    printf("devtype[%u][%u][%u][%u] = %08lX %08lX\n", Adapter, Type, Display, Back,
                           Check->Nine->CheckDeviceType(Adapter, Type, Display, Back, TRUE),
                           Check->Nine->CheckDeviceType(Adapter, Type, Display, Back, FALSE));
                }
            }
        }
    }
}

static
VOID
NcCheckFormats(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        for (D3DDEVTYPE Type : { D3DDEVTYPE_HAL, D3DDEVTYPE_REF })
        {
            for (D3DRESOURCETYPE Resource : NcResourceTypes)
            {
                for (D3DFORMAT Format : NcFormats)
                {
                    printf("format[%u][%u][%u][%08X] =", Adapter, Type, Resource, Format);
                    for (DWORD Usage : NcUsages)
                        printf(" %08lX", Check->Nine->CheckDeviceFormat(Adapter, Type, D3DFMT_X8R8G8B8, Usage, Resource, Format));
                    printf("\n");
                }
            }
        }
    }
}

static
VOID
NcCheckMultisample(
    _Inout_ NineCheck* Check)
{
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        for (D3DFORMAT Format : NcFormats)
        {
            printf("multisample[%u][%08X] =", Adapter, Format);
            for (UINT Samples = D3DMULTISAMPLE_NONE; Samples <= D3DMULTISAMPLE_16_SAMPLES; Samples++)
            {
                DWORD Levels = 0;
                HRESULT hr = Check->Nine->CheckDeviceMultiSampleType(Adapter, D3DDEVTYPE_HAL, Format, TRUE,
                                                                     (D3DMULTISAMPLE_TYPE)Samples, &Levels);

                printf(" %08lX:%lu", hr, Levels);
            }
            printf("\n");
        }
    }
}

static
VOID
NcCheckDepthMatch(
    _Inout_ NineCheck* Check)
{
    static const D3DFORMAT Depths[] =
    {
        D3DFMT_D16_LOCKABLE, D3DFMT_D32, D3DFMT_D15S1, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D24X4S4, D3DFMT_D16,
        D3DFMT_D32F_LOCKABLE, D3DFMT_D24FS8, D3DFMT_D32_LOCKABLE, D3DFMT_S8_LOCKABLE,
        (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z'), (D3DFORMAT)MAKEFOURCC('D', 'F', '2', '4'),
    };
    UINT Count = Check->Nine->GetAdapterCount();

    for (UINT Adapter = 0; Adapter < Count; Adapter++)
    {
        for (D3DFORMAT Target : NcDisplayFormats)
        {
            printf("depthmatch[%u][%u] =", Adapter, Target);
            for (D3DFORMAT Depth : Depths)
                printf(" %08lX", Check->Nine->CheckDepthStencilMatch(Adapter, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, Target, Depth));
            printf("\n");
        }
        for (D3DFORMAT Source : NcFormats)
        {
            printf("conversion[%u][%08X] =", Adapter, Source);
            for (D3DFORMAT Target : NcDisplayFormats)
                printf(" %08lX", Check->Nine->CheckDeviceFormatConversion(Adapter, D3DDEVTYPE_HAL, Source, Target));
            printf("\n");
        }
    }
}

typedef IDirect3D9* (WINAPI* PFN_NC_CREATE_ON12)(UINT, D3D9ON12_ARGS*, UINT);
typedef HRESULT (WINAPI* PFN_NC_CREATE_ON12_EX)(UINT, D3D9ON12_ARGS*, UINT, IDirect3D9Ex**);
typedef int (WINAPI* PFN_NC_BEGIN)(D3DCOLOR, LPCWSTR);
typedef int (WINAPI* PFN_NC_END)(VOID);
typedef DWORD (WINAPI* PFN_NC_STATUS)(VOID);
typedef BOOL (WINAPI* PFN_NC_REPEAT)(VOID);

/* Prints whether a creation call produced an object, releasing it again */
static
VOID
NcPrintCreated(
    _In_z_ PCSTR Name,
    _In_ UINT Version,
    _In_opt_ IUnknown* Object)
{
    printf("%s[%08X] = %s\n", Name, Version, Object != nullptr ? "object" : "null");
    if (Object != nullptr)
        Object->Release();
}

static
VOID
NcCheckExports(
    _Inout_ NineCheck* Check)
{
    static const UINT Versions[] = { 0, 1, 30, 31, 32, 33, 100, 131, 132, 0x80000020, 0x80000021, 0x7FFFFFFF };
    PFN_NC_CREATE_ON12 CreateOn12 = reinterpret_cast<PFN_NC_CREATE_ON12>(GetProcAddress(Check->Module, "Direct3DCreate9On12"));
    PFN_NC_CREATE_ON12_EX CreateOn12Ex =
        reinterpret_cast<PFN_NC_CREATE_ON12_EX>(GetProcAddress(Check->Module, "Direct3DCreate9On12Ex"));
    PFN_NC_BEGIN Begin = reinterpret_cast<PFN_NC_BEGIN>(GetProcAddress(Check->Module, "D3DPERF_BeginEvent"));
    PFN_NC_END End = reinterpret_cast<PFN_NC_END>(GetProcAddress(Check->Module, "D3DPERF_EndEvent"));
    PFN_NC_STATUS Status = reinterpret_cast<PFN_NC_STATUS>(GetProcAddress(Check->Module, "D3DPERF_GetStatus"));
    PFN_NC_REPEAT Repeat = reinterpret_cast<PFN_NC_REPEAT>(GetProcAddress(Check->Module, "D3DPERF_QueryRepeatFrame"));
    static const PCSTR Names[] =
    {
        "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame",
        "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion", "DebugSetLevel", "DebugSetMute",
        "Direct3D9EnableMaximizedWindowedModeShim", "Direct3DCreate9", "Direct3DCreate9Ex", "Direct3DCreate9On12",
        "Direct3DCreate9On12Ex", "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture",
    };
    D3D9ON12_ARGS Args = {};
    IDirect3D9Ex* Ex = nullptr;
    HRESULT hr;

    for (PCSTR Name : Names)
        printf("export %s = %s\n", Name, GetProcAddress(Check->Module, Name) != nullptr ? "present" : "missing");
    for (WORD Ordinal = 16; Ordinal <= 38; Ordinal++)
        printf("ordinal %u = %s\n", Ordinal, GetProcAddress(Check->Module, MAKEINTRESOURCEA(Ordinal)) != nullptr ? "present" : "missing");

    for (UINT Version : Versions)
    {
        NcPrintCreated("create", Version, Check->Create(Version));
        Ex = nullptr;
        hr = Check->CreateEx(Version, &Ex);
        printf("create_ex[%08X] = %08lX %s\n", Version, hr, Ex != nullptr ? "object" : "null");
        if (Ex != nullptr)
            Ex->Release();
    }

    if (CreateOn12 != nullptr && CreateOn12Ex != nullptr)
    {
        NcPrintCreated("create_on12_none", D3D_SDK_VERSION, CreateOn12(D3D_SDK_VERSION, nullptr, 0));
        NcPrintCreated("create_on12_off", D3D_SDK_VERSION, CreateOn12(D3D_SDK_VERSION, &Args, 1));
        Ex = nullptr;
        hr = CreateOn12Ex(D3D_SDK_VERSION, &Args, 1, &Ex);
        printf("create_on12_ex_off = %08lX %s\n", hr, Ex != nullptr ? "object" : "null");
        if (Ex != nullptr)
        {
            printf("create_on12_ex_off adapters = %u\n", Ex->GetAdapterCount());
            Ex->Release();
        }
    }

    /* Nothing records the markers here, so these report the no tool answers */
    printf("perf_status = %lu\n", Status());
    printf("perf_begin = %d\n", Begin(0xFF00FF00, L"check"));
    printf("perf_end = %d\n", End());
    printf("perf_repeat = %d\n", Repeat());
}

/* QueryInterface answers of the creation objects */
static
VOID
NcCheckInterfaces(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        GUID Iid;
    }
    Interfaces[] =
    {
        { "IUnknown", __uuidof(IUnknown) },
        { "IDirect3D9", __uuidof(IDirect3D9) },
        { "IDirect3D9Ex", __uuidof(IDirect3D9Ex) },
        { "IDirect3DDevice9", __uuidof(IDirect3DDevice9) },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);

    for (const auto& Interface : Interfaces)
    {
        VOID* Object = nullptr;
        HRESULT hr = Check->Nine->QueryInterface(Interface.Iid, &Object);

        printf("qi_ex[%s] = %08lX %s\n", Interface.Name, hr, Object == Check->Nine ? "same" : (Object != nullptr ? "other" : "null"));
        if (Object != nullptr)
            static_cast<IUnknown*>(Object)->Release();

        Object = nullptr;
        hr = Plain->QueryInterface(Interface.Iid, &Object);
        printf("qi_plain[%s] = %08lX %s\n", Interface.Name, hr, Object == Plain ? "same" : (Object != nullptr ? "other" : "null"));
        if (Object != nullptr)
            static_cast<IUnknown*>(Object)->Release();
    }
    printf("addref = %lu\n", Plain->AddRef());
    printf("release = %lu\n", Plain->Release());
    printf("release_last = %lu\n", Plain->Release());
}

/* A small top level window the device checks render and present to */
static
HWND
NcCreateWindow(
    _In_ BOOLEAN Visible)
{
    static ATOM Class;
    WNDCLASSW Info = {};
    RECT Rect = { 0, 0, 64, 48 };
    HWND Window;

    if (Class == 0)
    {
        Info.lpfnWndProc = DefWindowProcW;
        Info.hInstance = GetModuleHandleW(nullptr);
        Info.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        Info.lpszClassName = L"NcWindow";
        Class = RegisterClassW(&Info);
    }

    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowExW(0, L"NcWindow", L"d3d9check", WS_OVERLAPPEDWINDOW, 40, 40, Rect.right - Rect.left,
                             Rect.bottom - Rect.top, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (Window != NULL && Visible)
    {
        ShowWindow(Window, SW_SHOWNOACTIVATE);
        UpdateWindow(Window);
    }
    return Window;
}

/* Which entry point a creation case goes through */
enum NcCreatePath
{
    NcPathPlain,
    NcPathEx,
    NcPathExEx,
};

struct NcCreateCase
{
    char Name[40];
    UINT Adapter;
    D3DDEVTYPE Type;
    DWORD Behavior;
    HWND Focus;
    D3DPRESENT_PARAMETERS Parameters;
    D3DDISPLAYMODEEX Mode;
    BOOLEAN PassMode;
};

/* Runs one creation case through one entry point and prints the code and what it left in the parameters */
static
VOID
NcRunCreateCase(
    _In_ NineCheck* Check,
    _In_ IDirect3D9* Plain,
    _In_ const NcCreateCase* Case,
    _In_ NcCreatePath Path)
{
    static const PCSTR PathNames[] = { "plain", "ex", "exex" };
    D3DPRESENT_PARAMETERS Parameters = Case->Parameters;
    D3DDISPLAYMODEEX Mode = Case->Mode;
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    if (Path == NcPathPlain)
    {
        hr = Plain->CreateDevice(Case->Adapter, Case->Type, Case->Focus, Case->Behavior, &Parameters, &Device);
    }
    else if (Path == NcPathEx)
    {
        hr = Check->Ex->CreateDevice(Case->Adapter, Case->Type, Case->Focus, Case->Behavior, &Parameters, &Device);
    }
    else
    {
        hr = Check->Ex->CreateDeviceEx(Case->Adapter, Case->Type, Case->Focus, Case->Behavior, &Parameters,
                                       Case->PassMode ? &Mode : nullptr, &DeviceEx);
        Device = DeviceEx;
    }

    printf("create[%s][%s] = %08lX %s bb %ux%u fmt %u count %u swap %u interval %08lX flags %08lX\n", Case->Name,
           PathNames[Path], hr, Device != nullptr ? "device" : "null", Parameters.BackBufferWidth,
           Parameters.BackBufferHeight, Parameters.BackBufferFormat, Parameters.BackBufferCount,
           Parameters.SwapEffect, Parameters.PresentationInterval, Parameters.Flags);
    if (Device != nullptr)
        printf("create_release[%s][%s] = %lu\n", Case->Name, PathNames[Path], Device->Release());
}

/* CreateDevice argument rules; only windowed cases may succeed, fullscreen ones fail in validation */
static
VOID
NcCheckCreate(
    _Inout_ NineCheck* Check)
{
    static const DWORD Hw = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(FALSE);
    HWND Other = NcCreateWindow(FALSE);
    HWND Gone = NcCreateWindow(FALSE);
    std::vector<NcCreateCase> Cases;
    NcCreateCase Base = {};

    DestroyWindow(Gone);
    Base.Type = D3DDEVTYPE_HAL;
    Base.Behavior = Hw;
    Base.Focus = Window;
    Base.Parameters.Windowed = TRUE;
    Base.Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Base.Parameters.hDeviceWindow = Window;

    auto Add = [&](PCSTR Name, auto Change)
    {
        NcCreateCase Case = Base;

        strcpy_s(Case.Name, Name);
        Change(Case);
        Cases.push_back(Case);
    };
    char Name[40];

    Add("base", [](NcCreateCase&) {});
    Add("adapter_bad", [&](NcCreateCase& C) { C.Adapter = Check->Nine->GetAdapterCount(); });
    Add("type_0", [](NcCreateCase& C) { C.Type = static_cast<D3DDEVTYPE>(0); });
    Add("type_sw", [](NcCreateCase& C) { C.Type = D3DDEVTYPE_SW; });
    Add("type_5", [](NcCreateCase& C) { C.Type = static_cast<D3DDEVTYPE>(5); });
    Add("vp_sw", [](NcCreateCase& C) { C.Behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING; });
    Add("vp_mixed", [](NcCreateCase& C) { C.Behavior = D3DCREATE_MIXED_VERTEXPROCESSING; });
    Add("vp_none", [](NcCreateCase& C) { C.Behavior = 0; });
    Add("vp_hw_sw", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_SOFTWARE_VERTEXPROCESSING; });
    Add("vp_all", [](NcCreateCase& C) { C.Behavior = 0xE0; });
    Add("pure_hw", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_PUREDEVICE; });
    Add("pure_sw", [](NcCreateCase& C) { C.Behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE; });
    Add("pure_mixed", [](NcCreateCase& C) { C.Behavior = D3DCREATE_MIXED_VERTEXPROCESSING | D3DCREATE_PUREDEVICE; });
    Add("pure_only", [](NcCreateCase& C) { C.Behavior = D3DCREATE_PUREDEVICE; });
    for (DWORD Bit : { 0x1UL, 0x8UL, 0x1000UL, 0x10000UL, 0x800000UL, 0x8000000UL, 0x20000000UL, 0x80000000UL })
    {
        sprintf_s(Name, "flag_%08lX", Bit);
        Add(Name, [Bit](NcCreateCase& C) { C.Behavior = Hw | Bit; });
    }
    Add("multithreaded", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE; });
    Add("nowindowchanges", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_NOWINDOWCHANGES; });
    Add("nodrivermanagement", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_DISABLE_DRIVER_MANAGEMENT; });
    Add("nodrivermanagement_ex", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_DISABLE_DRIVER_MANAGEMENT_EX; });
    Add("psgp_threading", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_DISABLE_PSGP_THREADING; });
    Add("presentstats", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_ENABLE_PRESENTSTATS; });
    Add("printscreen", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_DISABLE_PRINTSCREEN; });
    Add("screensaver", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_SCREENSAVER; });
    Add("adaptergroup", [](NcCreateCase& C) { C.Behavior = Hw | D3DCREATE_ADAPTERGROUP_DEVICE; });

    for (D3DFORMAT Format : { D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A1R5G5B5,
                              D3DFMT_A2R10G10B10, D3DFMT_A2B10G10R10, D3DFMT_A16B16G16R16F,
                              D3DFMT_A2B10G10R10_XR_BIAS, D3DFMT_A8B8G8R8, D3DFMT_D24S8,
                              static_cast<D3DFORMAT>(0x7FFFFFFF) })
    {
        sprintf_s(Name, "format_%u", Format);
        Add(Name, [Format](NcCreateCase& C) { C.Parameters.BackBufferFormat = Format; });
    }

    for (UINT Count : { 1U, 2U, 3U, 4U, 30U, 31U })
    {
        sprintf_s(Name, "count_%u", Count);
        Add(Name, [Count](NcCreateCase& C) { C.Parameters.BackBufferCount = Count; });
    }

    Add("swap_copy", [](NcCreateCase& C) { C.Parameters.SwapEffect = D3DSWAPEFFECT_COPY; });
    Add("swap_copy_2", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_COPY;
        C.Parameters.BackBufferCount = 2;
    });
    Add("swap_flip", [](NcCreateCase& C) { C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIP; });
    Add("swap_flip_2", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIP;
        C.Parameters.BackBufferCount = 2;
    });
    Add("swap_flipex", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIPEX;
        C.Parameters.BackBufferCount = 2;
    });
    Add("swap_flipex_565", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIPEX;
        C.Parameters.BackBufferFormat = D3DFMT_R5G6B5;
    });
    Add("swap_overlay", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_OVERLAY;
        C.Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    });
    Add("swap_nopresent", [](NcCreateCase& C) { C.Parameters.SwapEffect = static_cast<D3DSWAPEFFECT>(0x40000000); });
    Add("swap_0", [](NcCreateCase& C) { C.Parameters.SwapEffect = static_cast<D3DSWAPEFFECT>(0); });
    Add("swap_6", [](NcCreateCase& C) { C.Parameters.SwapEffect = static_cast<D3DSWAPEFFECT>(6); });

    Add("ms_2", [](NcCreateCase& C) { C.Parameters.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES; });
    Add("ms_4", [](NcCreateCase& C) { C.Parameters.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES; });
    Add("ms_4_copy", [](NcCreateCase& C)
    {
        C.Parameters.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
        C.Parameters.SwapEffect = D3DSWAPEFFECT_COPY;
    });
    Add("ms_4_lockable", [](NcCreateCase& C)
    {
        C.Parameters.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
        C.Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    });
    Add("ms_nonmaskable", [](NcCreateCase& C) { C.Parameters.MultiSampleType = D3DMULTISAMPLE_NONMASKABLE; });
    Add("ms_17", [](NcCreateCase& C) { C.Parameters.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(17); });
    Add("ms_quality_1", [](NcCreateCase& C) { C.Parameters.MultiSampleQuality = 1; });

    for (DWORD Flags : { 0x1UL, 0x2UL, 0x4UL, 0x8UL, 0x10UL, 0x20UL, 0x40UL, 0x80UL, 0x400UL, 0x800UL, 0x1000UL })
    {
        sprintf_s(Name, "pflags_%08lX", Flags);
        Add(Name, [Flags](NcCreateCase& C) { C.Parameters.Flags = Flags; });
    }
    Add("pflags_discard_depth", [](NcCreateCase& C)
    {
        C.Parameters.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
        C.Parameters.EnableAutoDepthStencil = TRUE;
        C.Parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    });
    Add("pflags_flipex_lock", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIPEX;
        C.Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    });
    Add("pflags_flipex_clip", [](NcCreateCase& C)
    {
        C.Parameters.SwapEffect = D3DSWAPEFFECT_FLIPEX;
        C.Parameters.Flags = D3DPRESENTFLAG_DEVICECLIP;
    });

    for (D3DFORMAT Format : { D3DFMT_D24S8, D3DFMT_D16, D3DFMT_D24X8, D3DFMT_D32, D3DFMT_D16_LOCKABLE,
                              D3DFMT_D32F_LOCKABLE, D3DFMT_X8R8G8B8, D3DFMT_UNKNOWN })
    {
        sprintf_s(Name, "depth_%u", Format);
        Add(Name, [Format](NcCreateCase& C)
        {
            C.Parameters.EnableAutoDepthStencil = TRUE;
            C.Parameters.AutoDepthStencilFormat = Format;
        });
    }

    Add("refresh_60", [](NcCreateCase& C) { C.Parameters.FullScreen_RefreshRateInHz = 60; });
    Add("interval_immediate", [](NcCreateCase& C) { C.Parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; });
    Add("interval_one", [](NcCreateCase& C) { C.Parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE; });
    Add("interval_two", [](NcCreateCase& C) { C.Parameters.PresentationInterval = D3DPRESENT_INTERVAL_TWO; });
    Add("interval_13", [](NcCreateCase& C) { C.Parameters.PresentationInterval = 0x13; });
    Add("mode_windowed", [](NcCreateCase& C)
    {
        C.PassMode = TRUE;
        C.Mode.Size = sizeof(C.Mode);
    });
    Add("size_set", [](NcCreateCase& C)
    {
        C.Parameters.BackBufferWidth = 32;
        C.Parameters.BackBufferHeight = 16;
    });
    Add("size_width", [](NcCreateCase& C) { C.Parameters.BackBufferWidth = 20; });

    Add("window_gone", [&](NcCreateCase& C) { C.Parameters.hDeviceWindow = Gone; });
    Add("focus_gone", [&](NcCreateCase& C) { C.Focus = Gone; });
    Add("focus_null", [](NcCreateCase& C) { C.Focus = NULL; });
    Add("window_null", [](NcCreateCase& C) { C.Parameters.hDeviceWindow = NULL; });
    Add("window_other", [&](NcCreateCase& C) { C.Parameters.hDeviceWindow = Other; });
    Add("windows_null", [](NcCreateCase& C)
    {
        C.Focus = NULL;
        C.Parameters.hDeviceWindow = NULL;
    });

    /* Fullscreen cases, each one refused by validation before any mode change */
    auto AddFull = [&](PCSTR FullName, auto Change)
    {
        Add(FullName, [&](NcCreateCase& C)
        {
            C.Parameters.Windowed = FALSE;
            C.Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
            Change(C);
        });
    };
    AddFull("full_flag_1", [](NcCreateCase& C) { C.Behavior |= 1; });
    AddFull("full_vp_none", [](NcCreateCase& C) { C.Behavior = 0; });
    AddFull("full_format_0", [](NcCreateCase& C) { C.Parameters.BackBufferFormat = D3DFMT_UNKNOWN; });
    AddFull("full_swap_6", [](NcCreateCase& C) { C.Parameters.SwapEffect = static_cast<D3DSWAPEFFECT>(6); });
    AddFull("full_nopresent", [](NcCreateCase& C) { C.Parameters.SwapEffect = static_cast<D3DSWAPEFFECT>(0x40000000); });
    AddFull("full_interval_13", [](NcCreateCase& C) { C.Parameters.PresentationInterval = 0x13; });
    AddFull("full_count_31", [](NcCreateCase& C) { C.Parameters.BackBufferCount = 31; });
    AddFull("full_ms_quality", [](NcCreateCase& C) { C.Parameters.MultiSampleQuality = 1; });
    AddFull("full_pflags_8", [](NcCreateCase& C) { C.Parameters.Flags = 8; });

    for (const NcCreateCase& Case : Cases)
    {
        NcRunCreateCase(Check, Plain, &Case, NcPathEx);
        NcRunCreateCase(Check, Plain, &Case, NcPathExEx);
        NcRunCreateCase(Check, Plain, &Case, NcPathPlain);
    }

    /* Fullscreen cases only some entry points refuse */
    {
        NcCreateCase Case = Base;

        Case.Parameters.Windowed = FALSE;
        Case.Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        Case.Parameters.BackBufferWidth = 640;
        Case.Parameters.BackBufferHeight = 480;

        strcpy_s(Case.Name, "full_mode_null");
        NcRunCreateCase(Check, Plain, &Case, NcPathExEx);

        strcpy_s(Case.Name, "full_mode_mismatch");
        Case.PassMode = TRUE;
        Case.Mode.Size = sizeof(Case.Mode);
        Case.Mode.Width = 800;
        Case.Mode.Height = 600;
        Case.Mode.Format = D3DFMT_X8R8G8B8;
        NcRunCreateCase(Check, Plain, &Case, NcPathExEx);

        strcpy_s(Case.Name, "full_mode_size");
        Case.Mode.Size = 0;
        Case.Mode.Width = 640;
        Case.Mode.Height = 480;
        NcRunCreateCase(Check, Plain, &Case, NcPathExEx);

        strcpy_s(Case.Name, "full_mode_scanline");
        Case.Mode.Size = sizeof(Case.Mode);
        Case.Mode.ScanLineOrdering = static_cast<D3DSCANLINEORDERING>(3);
        NcRunCreateCase(Check, Plain, &Case, NcPathExEx);

        strcpy_s(Case.Name, "full_noautorotate");
        Case.PassMode = FALSE;
        Case.Parameters.Flags = D3DPRESENTFLAG_NOAUTOROTATE;
        NcRunCreateCase(Check, Plain, &Case, NcPathPlain);

        strcpy_s(Case.Name, "full_focus_null");
        Case.Parameters.Flags = 0;
        Case.Focus = NULL;
        NcRunCreateCase(Check, Plain, &Case, NcPathPlain);
    }

    DestroyWindow(Other);
    DestroyWindow(Window);
    Plain->Release();
}

/* Prints a surface's description under a name */
static
VOID
NcPrintSurface(
    _In_z_ PCSTR Name,
    _In_opt_ IDirect3DSurface9* Surface)
{
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    if (Surface == nullptr)
    {
        printf("%s = null\n", Name);
        return;
    }
    hr = Surface->GetDesc(&Desc);
    printf("%s = %08lX fmt %u type %u usage %08lX pool %u ms %u:%lu size %ux%u\n", Name, hr, Desc.Format, Desc.Type,
           Desc.Usage, Desc.Pool, Desc.MultiSampleType, Desc.MultiSampleQuality, Desc.Width, Desc.Height);
}

static
VOID
NcPrintParameters(
    _In_z_ PCSTR Name,
    _In_ const D3DPRESENT_PARAMETERS* Parameters,
    _In_ HWND Window)
{
    printf("%s = bb %ux%u fmt %u count %u ms %u:%lu swap %u window %s windowed %d depth %d:%u flags %08lX "
           "refresh %u interval %08lX\n", Name, Parameters->BackBufferWidth, Parameters->BackBufferHeight,
           Parameters->BackBufferFormat, Parameters->BackBufferCount, Parameters->MultiSampleType,
           Parameters->MultiSampleQuality, Parameters->SwapEffect,
           Parameters->hDeviceWindow == Window ? "same" : (Parameters->hDeviceWindow == NULL ? "null" : "other"),
           Parameters->Windowed, Parameters->EnableAutoDepthStencil, Parameters->AutoDepthStencilFormat,
           Parameters->Flags, Parameters->FullScreen_RefreshRateInHz, Parameters->PresentationInterval);
}

/* Reads one pixel of a lockable surface */
static
VOID
NcPrintPixels(
    _In_z_ PCSTR Name,
    _In_ IDirect3DSurface9* Surface)
{
    D3DLOCKED_RECT Locked = {};
    HRESULT hr = Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY);

    if (FAILED(hr))
    {
        printf("%s = %08lX\n", Name, hr);
        return;
    }
    printf("%s = %08lX pitch %s first %08lX mid %08lX\n", Name, hr, Locked.Pitch >= 4 * 32 ? "wide" : "narrow",
           *static_cast<const DWORD*>(Locked.pBits),
           *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) + Locked.Pitch * 20 + 4 * 40));
    printf("%s_unlock = %08lX\n", Name, Surface->UnlockRect());
}

/* What the window shows after the compositor caught up */
static
VOID
NcPrintWindowPixel(
    _In_z_ PCSTR Name,
    _In_ HWND Window)
{
    HDC Dc;

    DwmFlush();
    DwmFlush();
    Dc = GetDC(Window);
    printf("%s = %06lX %06lX\n", Name, GetPixel(Dc, 1, 1), GetPixel(Dc, 40, 20));
    ReleaseDC(Window, Dc);
}

/* A windowed device: what creation leaves behind, its swap chain and surfaces, clears and presents */
static
VOID
NcCheckDevice(
    _Inout_ NineCheck* Check)
{
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DDEVICE_CREATION_PARAMETERS Creation = {};
    IDirect3DDevice9Ex* Device = nullptr;
    IDirect3DSwapChain9* Chain = nullptr;
    IDirect3DSwapChain9* Missing = nullptr;
    IDirect3DSwapChain9Ex* ChainEx = nullptr;
    IDirect3DSurface9* Back = nullptr;
    IDirect3DSurface9* Target = nullptr;
    IDirect3DSurface9* Depth = nullptr;
    IDirect3D9* Owner = nullptr;
    D3DDISPLAYMODEEX ModeEx = {};
    D3DDISPLAYROTATION Rotation = D3DDISPLAYROTATION_IDENTITY;
    D3DDISPLAYMODE Mode = {};
    D3DRASTER_STATUS Raster = {};
    D3DPRESENTSTATS Stats = {};
    D3DCAPS9 Caps = {};
    D3DCAPS9 EnumCaps = {};
    RECT Part = { 8, 8, 24, 16 };
    D3DRECT Area = { 8, 8, 24, 16 };
    INT Priority = 99;
    UINT Latency = 99;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    Parameters.EnableAutoDepthStencil = TRUE;
    Parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &Device);
    printf("device_create = %08lX %s\n", hr, Device != nullptr ? "device" : "null");
    NcPrintParameters("device_parameters", &Parameters, Window);
    if (Device == nullptr)
    {
        DestroyWindow(Window);
        return;
    }

    hr = Device->GetCreationParameters(&Creation);
    printf("device_creation = %08lX adapter %u type %u focus %s flags %08lX\n", hr, Creation.AdapterOrdinal,
           Creation.DeviceType, Creation.hFocusWindow == Window ? "same" : "other", Creation.BehaviorFlags);

    hr = Device->GetDirect3D(&Owner);
    printf("device_direct3d = %08lX %s\n", hr, Owner == Check->Nine ? "same" : "other");
    if (Owner != nullptr)
        printf("device_direct3d_release = %lu\n", Owner->Release());

    hr = Device->GetDeviceCaps(&Caps);
    Check->Nine->GetDeviceCaps(0, D3DDEVTYPE_HAL, &EnumCaps);
    printf("device_caps = %08lX %s\n", hr, memcmp(&Caps, &EnumCaps, sizeof(Caps)) == 0 ? "same" : "differs");
    for (UINT Index = 0; Index < sizeof(Caps) / sizeof(DWORD); Index++)
    {
        if (reinterpret_cast<const DWORD*>(&Caps)[Index] != reinterpret_cast<const DWORD*>(&EnumCaps)[Index])
            printf("device_caps_field[%u] = %08lX %08lX\n", Index, reinterpret_cast<const DWORD*>(&Caps)[Index],
                   reinterpret_cast<const DWORD*>(&EnumCaps)[Index]);
    }

    hr = Device->GetDisplayMode(0, &Mode);
    printf("device_mode = %08lX fmt %u %s\n", hr, Mode.Format, Mode.Width != 0 ? "sized" : "empty");
    printf("device_mode_1 = %08lX\n", Device->GetDisplayMode(1, &Mode));
    ModeEx.Size = sizeof(ModeEx);
    hr = Device->GetDisplayModeEx(0, &ModeEx, &Rotation);
    printf("device_mode_ex = %08lX fmt %u rotation %u\n", hr, ModeEx.Format, Rotation);

    printf("device_addref = %lu\n", Device->AddRef());
    printf("device_release = %lu\n", Device->Release());
    printf("device_swapchains = %u\n", Device->GetNumberOfSwapChains());
    hr = Device->GetSwapChain(0, &Chain);
    printf("device_swapchain = %08lX %s\n", hr, Chain != nullptr ? "object" : "null");
    printf("device_swapchain_1 = %08lX\n", Device->GetSwapChain(1, &Missing));
    if (Chain != nullptr)
    {
        D3DPRESENT_PARAMETERS Current = {};
        IDirect3DDevice9* Owner9 = nullptr;

        hr = Chain->GetPresentParameters(&Current);
        printf("chain_parameters_hr = %08lX\n", hr);
        NcPrintParameters("chain_parameters", &Current, Window);
        hr = Chain->GetDevice(&Owner9);
        printf("chain_device = %08lX %s\n", hr, Owner9 == Device ? "same" : "other");
        if (Owner9 != nullptr)
            printf("chain_device_release = %lu\n", Owner9->Release());
        hr = Chain->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), reinterpret_cast<VOID**>(&ChainEx));
        printf("chain_qi_ex = %08lX\n", hr);
        if (ChainEx != nullptr)
        {
            UINT Count = 99;

            printf("chain_stats = %08lX\n", ChainEx->GetPresentStats(&Stats));
            printf("chain_last_count = %08lX\n", ChainEx->GetLastPresentCount(&Count));
            printf("chain_ex_release = %lu\n", ChainEx->Release());
        }
        printf("chain_raster = %08lX\n", Chain->GetRasterStatus(&Raster));
        printf("chain_addref = %lu\n", Chain->AddRef());
        printf("chain_release = %lu\n", Chain->Release());
        printf("chain_release_last = %lu\n", Chain->Release());
        Chain = nullptr;
    }

    hr = Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Back);
    printf("device_backbuffer = %08lX\n", hr);
    NcPrintSurface("backbuffer_desc", Back);
    printf("device_backbuffer_1 = %08lX\n", Device->GetBackBuffer(0, 1, D3DBACKBUFFER_TYPE_MONO, &Target));
    printf("device_backbuffer_chain_1 = %08lX\n", Device->GetBackBuffer(1, 0, D3DBACKBUFFER_TYPE_MONO, &Target));
    printf("device_backbuffer_left = %08lX\n", Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_LEFT, &Target));
    if (Target != nullptr)
    {
        Target->Release();
        Target = nullptr;
    }
    hr = Device->GetRenderTarget(0, &Target);
    printf("device_rendertarget = %08lX %s\n", hr, Target == Back ? "backbuffer" : "other");
    if (Target != nullptr)
    {
        printf("rendertarget_release = %lu\n", Target->Release());
        Target = nullptr;
    }
    printf("device_rendertarget_1 = %08lX\n", Device->GetRenderTarget(1, &Target));
    hr = Device->GetDepthStencilSurface(&Depth);
    printf("device_depth = %08lX\n", hr);
    NcPrintSurface("depth_desc", Depth);

    if (Back != nullptr)
    {
        IDirect3DDevice9* Owner9 = nullptr;
        IUnknown* Container = nullptr;

        hr = Back->GetDevice(&Owner9);
        printf("backbuffer_device = %08lX %s\n", hr, Owner9 == Device ? "same" : "other");
        if (Owner9 != nullptr)
            Owner9->Release();
        hr = Back->GetContainer(__uuidof(IDirect3DSwapChain9), reinterpret_cast<VOID**>(&Container));
        printf("backbuffer_container = %08lX %s\n", hr, Container != nullptr ? "object" : "null");
        if (Container != nullptr)
            Container->Release();
        printf("backbuffer_addref = %lu\n", Back->AddRef());
        printf("backbuffer_release = %lu\n", Back->Release());
    }

    printf("device_coop = %08lX\n", Device->TestCooperativeLevel());
    printf("device_state = %08lX\n", Device->CheckDeviceState(Window));
    printf("device_state_null = %08lX\n", Device->CheckDeviceState(NULL));
    hr = Device->GetGPUThreadPriority(&Priority);
    printf("device_priority_get = %08lX %d\n", hr, Priority);
    printf("device_priority_set3 = %08lX\n", Device->SetGPUThreadPriority(3));
    hr = Device->GetGPUThreadPriority(&Priority);
    printf("device_priority_get3 = %08lX %d\n", hr, Priority);
    printf("device_priority_set8 = %08lX\n", Device->SetGPUThreadPriority(8));
    printf("device_priority_setm8 = %08lX\n", Device->SetGPUThreadPriority(-8));
    hr = Device->GetGPUThreadPriority(&Priority);
    printf("device_priority_get_last = %08lX %d\n", hr, Priority);
    hr = Device->GetMaximumFrameLatency(&Latency);
    printf("device_latency_get = %08lX %u\n", hr, Latency);
    printf("device_latency_set5 = %08lX\n", Device->SetMaximumFrameLatency(5));
    hr = Device->GetMaximumFrameLatency(&Latency);
    printf("device_latency_get5 = %08lX %u\n", hr, Latency);
    printf("device_latency_set17 = %08lX\n", Device->SetMaximumFrameLatency(17));
    printf("device_latency_set0 = %08lX\n", Device->SetMaximumFrameLatency(0));
    hr = Device->GetMaximumFrameLatency(&Latency);
    printf("device_latency_get0 = %08lX %u\n", hr, Latency);
    printf("device_latency_set30 = %08lX\n", Device->SetMaximumFrameLatency(30));
    printf("device_latency_set31 = %08lX\n", Device->SetMaximumFrameLatency(31));
    hr = Device->GetMaximumFrameLatency(&Latency);
    printf("device_latency_get30 = %08lX %u\n", hr, Latency);
    printf("device_priority_setm7 = %08lX\n", Device->SetGPUThreadPriority(-7));
    hr = Device->GetGPUThreadPriority(&Priority);
    printf("device_priority_getm7 = %08lX %d\n", hr, Priority);
    printf("device_raster = %08lX\n", Device->GetRasterStatus(0, &Raster));
    printf("device_raster_1 = %08lX\n", Device->GetRasterStatus(1, &Raster));

    for (DWORD State = 0; State < 256; State++)
    {
        DWORD Value = 0xCCCCCCCC;

        hr = Device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(State), &Value);
        if (SUCCEEDED(hr))
            printf("renderstate[%lu] = %08lX\n", State, Value);
        else if (State < 8)
            printf("renderstate[%lu] = %08lX\n", State, hr);
    }

    printf("clear = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF336699, 1.0f, 0));
    printf("clear_depth = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 0.5f, 7));
    printf("clear_rect = %08lX\n", Device->Clear(1, &Area, D3DCLEAR_TARGET, 0xFFCC0000, 1.0f, 0));
    printf("clear_rects_null = %08lX\n", Device->Clear(1, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0));
    printf("clear_flags_0 = %08lX\n", Device->Clear(0, nullptr, 0, 0, 1.0f, 0));
    printf("clear_flags_bad = %08lX\n", Device->Clear(0, nullptr, 0x10, 0, 1.0f, 0));
    printf("clear_z_range = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 2.0f, 0));
    if (Back != nullptr)
        NcPrintPixels("backbuffer_pixels", Back);

    printf("present = %08lX\n", Device->Present(nullptr, nullptr, nullptr, nullptr));
    NcPrintWindowPixel("window_pixels", Window);
    printf("present_rect = %08lX\n", Device->Present(&Part, &Part, nullptr, nullptr));
    printf("present_ex = %08lX\n", Device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0));
    printf("present_ex_flags = %08lX\n", Device->PresentEx(nullptr, nullptr, nullptr, nullptr, 0x80000000));
    printf("present_ex_donotwait = %08lX\n", Device->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTWAIT));
    printf("present_ex_donotflip = %08lX\n", Device->PresentEx(nullptr, nullptr, nullptr, nullptr, D3DPRESENT_DONOTFLIP));
    printf("present_window = %08lX\n", Device->Present(nullptr, nullptr, Window, nullptr));

    if (Back != nullptr)
    {
        printf("backbuffer_release_last = %lu\n", Back->Release());
        Back = nullptr;
    }
    if (Depth != nullptr)
    {
        printf("depth_release = %lu\n", Depth->Release());
        Depth = nullptr;
    }

    {
        D3DPRESENT_PARAMETERS Again = Parameters;

        Again.BackBufferWidth = 32;
        Again.BackBufferHeight = 24;
        hr = Device->Reset(&Again);
        printf("reset = %08lX\n", hr);
        NcPrintParameters("reset_parameters", &Again, Window);
        hr = Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Back);
        NcPrintSurface("reset_backbuffer", Back);
        if (Back != nullptr)
        {
            Back->Release();
            Back = nullptr;
        }
        Again = Parameters;
        Again.BackBufferWidth = 0;
        Again.BackBufferHeight = 0;
        Again.EnableAutoDepthStencil = FALSE;
        hr = Device->ResetEx(&Again, nullptr);
        printf("reset_ex = %08lX\n", hr);
        NcPrintParameters("reset_ex_parameters", &Again, Window);
        hr = Device->GetDepthStencilSurface(&Depth);
        printf("reset_ex_depth = %08lX %s\n", hr, Depth != nullptr ? "object" : "null");
        if (Depth != nullptr)
            Depth->Release();
        Again.SwapEffect = static_cast<D3DSWAPEFFECT>(9);
        printf("reset_bad = %08lX\n", Device->Reset(&Again));
        printf("reset_bad_coop = %08lX\n", Device->TestCooperativeLevel());
    }

    printf("device_release_last = %lu\n", Device->Release());
    DestroyWindow(Window);
}

/* A windowed device of the plain object: what it refuses that the extended device offers */
static
VOID
NcCheckDevicePlain(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DSwapChain9* Chain = nullptr;
    IDirect3DSwapChain9Ex* ChainEx = nullptr;
    IDirect3DSurface9* Back = nullptr;
    D3DCAPS9 Caps = {};
    D3DCAPS9 EnumCaps = {};
    DWORD Value = 0;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_COPY;
    Parameters.hDeviceWindow = Window;
    Parameters.BackBufferWidth = 40;
    Parameters.BackBufferHeight = 30;
    Parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window,
                             D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &Parameters, &Device);
    printf("plain_create = %08lX %s\n", hr, Device != nullptr ? "device" : "null");
    NcPrintParameters("plain_parameters", &Parameters, Window);
    if (Device == nullptr)
    {
        DestroyWindow(Window);
        Plain->Release();
        return;
    }

    hr = Device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<VOID**>(&DeviceEx));
    printf("plain_qi_ex = %08lX %s\n", hr, DeviceEx != nullptr ? "object" : "null");
    if (DeviceEx != nullptr)
        DeviceEx->Release();

    hr = Device->GetDeviceCaps(&Caps);
    Plain->GetDeviceCaps(0, D3DDEVTYPE_HAL, &EnumCaps);
    printf("plain_caps = %08lX %s\n", hr, memcmp(&Caps, &EnumCaps, sizeof(Caps)) == 0 ? "same" : "differs");
    for (UINT Index = 0; Index < sizeof(Caps) / sizeof(DWORD); Index++)
    {
        if (reinterpret_cast<const DWORD*>(&Caps)[Index] != reinterpret_cast<const DWORD*>(&EnumCaps)[Index])
            printf("plain_caps_field[%u] = %08lX %08lX\n", Index, reinterpret_cast<const DWORD*>(&Caps)[Index],
                   reinterpret_cast<const DWORD*>(&EnumCaps)[Index]);
    }

    hr = Device->GetSwapChain(0, &Chain);
    printf("plain_swapchain = %08lX\n", hr);
    if (Chain != nullptr)
    {
        hr = Chain->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), reinterpret_cast<VOID**>(&ChainEx));
        printf("plain_chain_qi_ex = %08lX %s\n", hr, ChainEx != nullptr ? "object" : "null");
        if (ChainEx != nullptr)
            ChainEx->Release();
        printf("plain_chain_release = %lu\n", Chain->Release());
    }

    printf("plain_coop = %08lX\n", Device->TestCooperativeLevel());
    hr = Device->GetRenderState(D3DRS_ZENABLE, &Value);
    printf("plain_zenable = %08lX %lu\n", hr, Value);
    hr = Device->GetRenderState(D3DRS_POINTSIZE_MAX, &Value);
    printf("plain_pointsize_max = %08lX %08lX\n", hr, Value);
    printf("plain_set_retired = %08lX\n", Device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(10), 1));
    printf("plain_set_cull = %08lX\n", Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    hr = Device->GetRenderState(D3DRS_CULLMODE, &Value);
    printf("plain_get_cull = %08lX %lu\n", hr, Value);
    printf("plain_clear = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF00FF00, 1.0f, 0));
    printf("plain_clear_depth = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0));
    hr = Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Back);
    NcPrintSurface("plain_backbuffer", Back);
    if (Back != nullptr)
    {
        D3DLOCKED_RECT Locked = {};
        RECT Bad = { 0, 0, 41, 30 };

        printf("plain_lock_bad = %08lX\n", Back->LockRect(&Locked, &Bad, 0));
        NcPrintPixels("plain_pixels", Back);
        printf("plain_backbuffer_release = %lu\n", Back->Release());
    }
    printf("plain_present = %08lX\n", Device->Present(nullptr, nullptr, nullptr, nullptr));
    Parameters.BackBufferWidth = 0;
    Parameters.BackBufferHeight = 0;
    Parameters.EnableAutoDepthStencil = TRUE;
    Parameters.AutoDepthStencilFormat = D3DFMT_D16;
    hr = Device->Reset(&Parameters);
    printf("plain_reset = %08lX\n", hr);
    NcPrintParameters("plain_reset_parameters", &Parameters, Window);
    hr = Device->GetRenderState(D3DRS_ZENABLE, &Value);
    printf("plain_reset_zenable = %08lX %lu\n", hr, Value);
    hr = Device->GetRenderState(D3DRS_CULLMODE, &Value);
    printf("plain_reset_cull = %08lX %lu\n", hr, Value);
    printf("plain_reset_clear_depth = %08lX\n", Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0));
    printf("plain_release_last = %lu\n", Device->Release());
    DestroyWindow(Window);
    Plain->Release();
}

/* Writes a byte pattern through one lock and reads it back through another */
static
VOID
NcCheckSurfaceBytes(
    _In_z_ PCSTR Name,
    _In_ IDirect3DSurface9* Surface)
{
    D3DLOCKED_RECT Locked = {};
    RECT Part = { 1, 1, 3, 3 };
    DWORD First = 0;
    DWORD Second = 0;
    HRESULT hr;

    hr = Surface->LockRect(&Locked, nullptr, 0);
    if (FAILED(hr))
    {
        printf("%s_bytes = %08lX\n", Name, hr);
        return;
    }
    static_cast<BYTE*>(Locked.pBits)[0] = 0x5A;
    static_cast<BYTE*>(Locked.pBits)[Locked.Pitch + 4] = 0xA5;
    Surface->UnlockRect();
    hr = Surface->LockRect(&Locked, &Part, D3DLOCK_READONLY);
    if (SUCCEEDED(hr))
    {
        First = *static_cast<const BYTE*>(Locked.pBits);
        Surface->UnlockRect();
    }
    hr = Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr))
    {
        Second = static_cast<const BYTE*>(Locked.pBits)[0];
        Surface->UnlockRect();
    }
    printf("%s_bytes = %08lX part %02lX first %02lX\n", Name, hr, First, Second);
}

/* Lock flags, rectangles and repeated locks against one surface */
static
VOID
NcCheckSurfaceLocks(
    _In_z_ PCSTR Name,
    _In_ IDirect3DSurface9* Surface)
{
    static const DWORD Flags[] =
    {
        0, D3DLOCK_READONLY, D3DLOCK_DISCARD, D3DLOCK_DISCARD | D3DLOCK_READONLY, D3DLOCK_NOOVERWRITE,
        D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE, D3DLOCK_NOSYSLOCK, D3DLOCK_DONOTWAIT, D3DLOCK_NO_DIRTY_UPDATE,
        D3DLOCK_NOOVERWRITE | D3DLOCK_READONLY, 0x00000001, 0x00040000,
    };
    static const RECT Rects[] =
    {
        { 0, 0, 1, 1 }, { 2, 2, 8, 6 }, { 3, 1, 5, 4 }, { 0, 0, 17, 8 }, { 4, 4, 4, 6 }, { -1, 0, 2, 2 },
        { 0, 0, 16, 8 }, { 4, 4, 16, 8 },
    };
    D3DLOCKED_RECT Locked = {};
    HRESULT hr;

    hr = Surface->LockRect(&Locked, nullptr, 0);
    printf("%s_lock = %08lX pitch %d\n", Name, hr, Locked.Pitch);
    printf("%s_lock_again = %08lX\n", Name, Surface->LockRect(&Locked, nullptr, 0));
    printf("%s_unlock = %08lX\n", Name, Surface->UnlockRect());
    printf("%s_unlock_again = %08lX\n", Name, Surface->UnlockRect());
    for (UINT Index = 0; Index < _countof(Flags); Index++)
    {
        hr = Surface->LockRect(&Locked, nullptr, Flags[Index]);
        printf("%s_flags[%08lX] = %08lX pitch %d\n", Name, Flags[Index], hr, Locked.Pitch);
        if (SUCCEEDED(hr))
            Surface->UnlockRect();
    }
    /* Drivers may hand out new memory on every lock, so the offset is taken against a lock just before */
    for (const RECT& Rect : Rects)
    {
        BYTE* Base = nullptr;

        if (SUCCEEDED(Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY)))
        {
            Base = static_cast<BYTE*>(Locked.pBits);
            Surface->UnlockRect();
        }
        hr = Surface->LockRect(&Locked, &Rect, D3DLOCK_READONLY);
        printf("%s_rect(%ld,%ld,%ld,%ld) = %08lX offset %lld\n", Name, Rect.left, Rect.top, Rect.right, Rect.bottom,
               hr, SUCCEEDED(hr) && Base != nullptr ? static_cast<long long>(static_cast<BYTE*>(Locked.pBits) - Base)
                                                    : -1LL);
        if (SUCCEEDED(hr))
            Surface->UnlockRect();
    }
}

/* Describes a surface creation; the surface goes back right away */
static
VOID
NcPrintSurfaceMade(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result,
    _In_opt_ IDirect3DSurface9* Surface)
{
    D3DSURFACE_DESC Desc = {};
    IDirect3DDevice9* Owner = nullptr;
    IUnknown* Container = nullptr;
    D3DLOCKED_RECT Locked = {};
    HRESULT Lock;

    if (Surface == nullptr)
    {
        printf("%s = %08lX null\n", Name, Result);
        return;
    }
    Surface->GetDesc(&Desc);
    Lock = Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(Lock))
        Surface->UnlockRect();
    Surface->GetDevice(&Owner);
    Surface->GetContainer(__uuidof(IDirect3DDevice9), reinterpret_cast<VOID**>(&Container));
    printf("%s = %08lX fmt %u type %u usage %08lX pool %u ms %u:%lu size %ux%u lock %08lX pitch %d rtype %u "
           "container %s\n", Name, Result, Desc.Format, Desc.Type, Desc.Usage, Desc.Pool, Desc.MultiSampleType,
           Desc.MultiSampleQuality, Desc.Width, Desc.Height, Lock, Locked.Pitch, Surface->GetType(),
           Container != nullptr && Container == Owner ? "device" : "other");
    if (Container != nullptr)
        Container->Release();
    if (Owner != nullptr)
        Owner->Release();
    printf("%s_release = %lu\n", Name, Surface->Release());
}

/* Every surface creation call a device offers, across formats, pools, sizes and sample types */
static
VOID
NcCheckSurfacesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_opt_ IDirect3DDevice9Ex* DeviceEx)
{
    static const D3DPOOL Pools[] = { D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH,
                                     static_cast<D3DPOOL>(4) };
    static const UINT Sizes[][2] = { { 16, 8 }, { 10, 6 }, { 0, 4 }, { 4, 0 }, { 1, 1 } };
    static const DWORD Usages[] =
    {
        0, D3DUSAGE_RENDERTARGET, D3DUSAGE_DEPTHSTENCIL, D3DUSAGE_DYNAMIC, D3DUSAGE_WRITEONLY,
        D3DUSAGE_RESTRICTED_CONTENT, D3DUSAGE_RESTRICT_SHARED_RESOURCE, D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER,
        D3DUSAGE_NONSECURE, D3DUSAGE_TEXTAPI, 0x00400000,
    };
    IDirect3DSurface9* Surface;
    HANDLE Shared;
    char Name[96];
    HRESULT hr;

    for (D3DFORMAT Format : NcFormats)
    {
        for (D3DPOOL Pool : Pools)
        {
            Surface = nullptr;
            hr = Device->CreateOffscreenPlainSurface(16, 8, Format, Pool, &Surface, nullptr);
            sprintf_s(Name, "%s_plain[%u,%u]", Prefix, Format, Pool);
            NcPrintSurfaceMade(Name, hr, Surface);
        }
        for (BOOL Lockable = FALSE; Lockable <= TRUE; Lockable++)
        {
            Surface = nullptr;
            hr = Device->CreateRenderTarget(16, 8, Format, D3DMULTISAMPLE_NONE, 0, Lockable, &Surface, nullptr);
            sprintf_s(Name, "%s_target[%u,%d]", Prefix, Format, Lockable);
            NcPrintSurfaceMade(Name, hr, Surface);
            Surface = nullptr;
            hr = Device->CreateDepthStencilSurface(16, 8, Format, D3DMULTISAMPLE_NONE, 0, Lockable, &Surface, nullptr);
            sprintf_s(Name, "%s_depth[%u,%d]", Prefix, Format, Lockable);
            NcPrintSurfaceMade(Name, hr, Surface);
        }
    }

    for (const auto& Size : Sizes)
    {
        static const D3DFORMAT Formats[] = { D3DFMT_A8R8G8B8, D3DFMT_DXT1, D3DFMT_DXT5, D3DFMT_YUY2, D3DFMT_R5G6B5 };

        for (D3DFORMAT Format : Formats)
        {
            Surface = nullptr;
            hr = Device->CreateOffscreenPlainSurface(Size[0], Size[1], Format, D3DPOOL_SCRATCH, &Surface, nullptr);
            sprintf_s(Name, "%s_size_scratch[%ux%u,%u]", Prefix, Size[0], Size[1], Format);
            NcPrintSurfaceMade(Name, hr, Surface);
            Surface = nullptr;
            hr = Device->CreateOffscreenPlainSurface(Size[0], Size[1], Format, D3DPOOL_SYSTEMMEM, &Surface, nullptr);
            sprintf_s(Name, "%s_size_sysmem[%ux%u,%u]", Prefix, Size[0], Size[1], Format);
            NcPrintSurfaceMade(Name, hr, Surface);
            Surface = nullptr;
            hr = Device->CreateRenderTarget(Size[0], Size[1], Format, D3DMULTISAMPLE_NONE, 0, TRUE, &Surface,
                                            nullptr);
            sprintf_s(Name, "%s_size_target[%ux%u,%u]", Prefix, Size[0], Size[1], Format);
            NcPrintSurfaceMade(Name, hr, Surface);
        }
    }

    for (UINT Type = 0; Type <= 16; Type++)
    {
        for (DWORD Quality = 0; Quality <= 64; Quality += 32)
        {
            Surface = nullptr;
            hr = Device->CreateRenderTarget(16, 8, D3DFMT_A8R8G8B8, static_cast<D3DMULTISAMPLE_TYPE>(Type), Quality,
                                            FALSE, &Surface, nullptr);
            sprintf_s(Name, "%s_ms_target[%u,%lu]", Prefix, Type, Quality);
            NcPrintSurfaceMade(Name, hr, Surface);
            Surface = nullptr;
            hr = Device->CreateDepthStencilSurface(16, 8, D3DFMT_D24S8, static_cast<D3DMULTISAMPLE_TYPE>(Type),
                                                   Quality, FALSE, &Surface, nullptr);
            sprintf_s(Name, "%s_ms_depth[%u,%lu]", Prefix, Type, Quality);
            NcPrintSurfaceMade(Name, hr, Surface);
        }
    }

    Shared = nullptr;
    Surface = nullptr;
    hr = Device->CreateRenderTarget(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, &Shared);
    sprintf_s(Name, "%s_target_shared", Prefix);
    NcPrintSurfaceMade(Name, hr, Surface);

    Surface = nullptr;
    hr = Device->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, nullptr);
    if (Surface != nullptr)
    {
        sprintf_s(Name, "%s_default", Prefix);
        NcCheckSurfaceLocks(Name, Surface);
        NcCheckSurfaceBytes(Name, Surface);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, nullptr);
    if (Surface != nullptr)
    {
        sprintf_s(Name, "%s_sysmem", Prefix);
        NcCheckSurfaceLocks(Name, Surface);
        NcCheckSurfaceBytes(Name, Surface);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateOffscreenPlainSurface(16, 8, D3DFMT_DXT1, D3DPOOL_SCRATCH, &Surface, nullptr);
    if (Surface != nullptr)
    {
        sprintf_s(Name, "%s_scratch_dxt1", Prefix);
        NcCheckSurfaceLocks(Name, Surface);
        Surface->Release();
    }
    Surface = nullptr;
    hr = Device->CreateRenderTarget(16, 8, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &Surface, nullptr);
    if (Surface != nullptr)
    {
        sprintf_s(Name, "%s_target", Prefix);
        NcCheckSurfaceLocks(Name, Surface);
        NcCheckSurfaceBytes(Name, Surface);
        Surface->Release();
    }

    if (DeviceEx == nullptr)
        return;
    for (DWORD Usage : Usages)
    {
        Surface = nullptr;
        hr = DeviceEx->CreateRenderTargetEx(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr,
                                            Usage);
        sprintf_s(Name, "%s_target_ex[%08lX]", Prefix, Usage);
        NcPrintSurfaceMade(Name, hr, Surface);
        Surface = nullptr;
        hr = DeviceEx->CreateDepthStencilSurfaceEx(16, 8, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface,
                                                   nullptr, Usage);
        sprintf_s(Name, "%s_depth_ex[%08lX]", Prefix, Usage);
        NcPrintSurfaceMade(Name, hr, Surface);
        Surface = nullptr;
        hr = DeviceEx->CreateOffscreenPlainSurfaceEx(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, nullptr,
                                                     Usage);
        sprintf_s(Name, "%s_plain_ex[%08lX]", Prefix, Usage);
        NcPrintSurfaceMade(Name, hr, Surface);
        Surface = nullptr;
        hr = DeviceEx->CreateOffscreenPlainSurfaceEx(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, nullptr,
                                                     Usage);
        sprintf_s(Name, "%s_plain_ex_sysmem[%08lX]", Prefix, Usage);
        NcPrintSurfaceMade(Name, hr, Surface);
    }
}

/* Standalone surfaces on a windowed extended device and on a device of the plain object */
static
VOID
NcCheckSurfaces(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &DeviceEx);
    printf("surfaces_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckSurfacesOn("surfaces_ex", DeviceEx, DeviceEx);
        printf("surfaces_ex_release = %lu\n", DeviceEx->Release());
    }

    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    printf("surfaces_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckSurfacesOn("surfaces_plain", Device, nullptr);
        printf("surfaces_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Nested locks, offsets against a whole lock, and bytes written through one lock and read through another */
template <class Buffer>
static
VOID
NcCheckBufferLocks(
    _In_z_ PCSTR Name,
    _In_ Buffer* Target)
{
    static const UINT Ranges[][2] = { { 0, 0 }, { 16, 0 }, { 16, 32 }, { 0, 64 }, { 200, 100 }, { 300, 0 } };
    static const DWORD Flags[] =
    {
        0, D3DLOCK_READONLY, D3DLOCK_DISCARD, D3DLOCK_NOOVERWRITE, D3DLOCK_DISCARD | D3DLOCK_READONLY,
        D3DLOCK_NOSYSLOCK, D3DLOCK_DONOTWAIT, 0x00000001, 0x00040000,
    };
    VOID* Whole = nullptr;
    VOID* Data = nullptr;
    VOID* Again = nullptr;
    HRESULT hr;

    hr = Target->Lock(0, 0, &Whole, 0);
    printf("%s_lock = %08lX %s\n", Name, hr, Whole != nullptr ? "mem" : "nomem");
    if (SUCCEEDED(hr))
    {
        static_cast<BYTE*>(Whole)[0] = 0x3C;
        static_cast<BYTE*>(Whole)[20] = 0xC3;
    }
    hr = Target->Lock(0, 0, &Again, 0);
    printf("%s_lock_nested = %08lX %s\n", Name, hr, Again == Whole ? "same" : "other");
    printf("%s_unlock = %08lX\n", Name, Target->Unlock());
    printf("%s_unlock_outer = %08lX\n", Name, Target->Unlock());

    for (const auto& Range : Ranges)
    {
        Whole = nullptr;
        if (SUCCEEDED(Target->Lock(0, 0, &Whole, D3DLOCK_READONLY)))
            Target->Unlock();
        Data = nullptr;
        hr = Target->Lock(Range[0], Range[1], &Data, D3DLOCK_READONLY);
        printf("%s_range(%u,%u) = %08lX offset %lld\n", Name, Range[0], Range[1], hr,
               SUCCEEDED(hr) && Whole != nullptr && Data != nullptr
                   ? static_cast<long long>(static_cast<BYTE*>(Data) - static_cast<BYTE*>(Whole)) : -1LL);
        if (SUCCEEDED(hr))
            Target->Unlock();
    }
    for (DWORD Flag : Flags)
    {
        Data = nullptr;
        hr = Target->Lock(0, 16, &Data, Flag);
        printf("%s_flags[%08lX] = %08lX %s\n", Name, Flag, hr, Data != nullptr ? "mem" : "nomem");
        if (SUCCEEDED(hr))
            Target->Unlock();
    }

    Data = nullptr;
    hr = Target->Lock(16, 16, &Data, D3DLOCK_READONLY);
    printf("%s_bytes = %08lX %02X\n", Name, hr, SUCCEEDED(hr) && Data != nullptr ? static_cast<BYTE*>(Data)[4] : 0);
    if (SUCCEEDED(hr))
        Target->Unlock();
}

/* Vertex and index buffer creation across pools, usages, formats and sizes, and their locks */
static
VOID
NcCheckBuffersOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const D3DPOOL Pools[] = { D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH,
                                     static_cast<D3DPOOL>(4), static_cast<D3DPOOL>(6) };
    static const DWORD Usages[] =
    {
        0, D3DUSAGE_RENDERTARGET, D3DUSAGE_DEPTHSTENCIL, D3DUSAGE_WRITEONLY, D3DUSAGE_SOFTWAREPROCESSING,
        D3DUSAGE_DONOTCLIP, D3DUSAGE_POINTS, D3DUSAGE_RTPATCHES, D3DUSAGE_NPATCHES, D3DUSAGE_DYNAMIC,
        D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DUSAGE_AUTOGENMIPMAP, D3DUSAGE_DMAP, D3DUSAGE_RESTRICTED_CONTENT,
        D3DUSAGE_RESTRICT_SHARED_RESOURCE, D3DUSAGE_NONSECURE, D3DUSAGE_TEXTAPI, 0x00000004, 0x00400000,
    };
    static const struct
    {
        UINT Length;
        DWORD Fvf;
    } Sizes[] =
    {
        { 0, 0 }, { 1, 0 }, { 11, D3DFVF_XYZ }, { 12, D3DFVF_XYZ }, { 27, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 },
        { 28, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1 }, { 39, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2 },
        { 40, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2 }, { 31, D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 },
        { 15, D3DFVF_XYZW }, { 16, D3DFVF_XYZW }, { 23, D3DFVF_XYZ | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0) },
        { 23, D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_SPECULAR | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE1(0) },
    };
    static const D3DFORMAT IndexFormats[] =
    {
        D3DFMT_INDEX16, D3DFMT_INDEX32, D3DFMT_UNKNOWN, D3DFMT_VERTEXDATA, D3DFMT_A8R8G8B8, D3DFMT_L16,
    };
    IDirect3DVertexBuffer9* Vertex;
    IDirect3DIndexBuffer9* Index;
    char Name[96];
    HRESULT hr;

    for (D3DPOOL Pool : Pools)
    {
        for (DWORD Usage : Usages)
        {
            D3DVERTEXBUFFER_DESC VertexDesc = {};
            D3DINDEXBUFFER_DESC IndexDesc = {};

            Vertex = nullptr;
            hr = Device->CreateVertexBuffer(64, Usage, 0, Pool, &Vertex, nullptr);
            if (Vertex != nullptr)
                Vertex->GetDesc(&VertexDesc);
            printf("%s_vb[%u,%08lX] = %08lX fmt %u type %u usage %08lX pool %u size %u fvf %08lX rtype %u\n", Prefix,
                   Pool, Usage, hr, VertexDesc.Format, VertexDesc.Type, VertexDesc.Usage, VertexDesc.Pool,
                   VertexDesc.Size, VertexDesc.FVF, Vertex != nullptr ? Vertex->GetType() : 0);
            if (Vertex != nullptr)
                Vertex->Release();

            Index = nullptr;
            hr = Device->CreateIndexBuffer(64, Usage, D3DFMT_INDEX16, Pool, &Index, nullptr);
            if (Index != nullptr)
                Index->GetDesc(&IndexDesc);
            printf("%s_ib[%u,%08lX] = %08lX fmt %u type %u usage %08lX pool %u size %u rtype %u\n", Prefix, Pool,
                   Usage, hr, IndexDesc.Format, IndexDesc.Type, IndexDesc.Usage, IndexDesc.Pool, IndexDesc.Size,
                   Index != nullptr ? Index->GetType() : 0);
            if (Index != nullptr)
                Index->Release();
        }
    }

    for (const auto& Size : Sizes)
    {
        Vertex = nullptr;
        hr = Device->CreateVertexBuffer(Size.Length, 0, Size.Fvf, D3DPOOL_DEFAULT, &Vertex, nullptr);
        printf("%s_vb_size[%u,%08lX] = %08lX\n", Prefix, Size.Length, Size.Fvf, hr);
        if (Vertex != nullptr)
            Vertex->Release();
    }
    for (D3DFORMAT Format : IndexFormats)
    {
        for (UINT Length = 1; Length <= 5; Length++)
        {
            Index = nullptr;
            hr = Device->CreateIndexBuffer(Length, 0, Format, D3DPOOL_DEFAULT, &Index, nullptr);
            printf("%s_ib_format[%u,%u] = %08lX\n", Prefix, Format, Length, hr);
            if (Index != nullptr)
                Index->Release();
        }
    }

    for (D3DPOOL Pool : Pools)
    {
        Vertex = nullptr;
        if (SUCCEEDED(Device->CreateVertexBuffer(256, 0, 0, Pool, &Vertex, nullptr)))
        {
            sprintf_s(Name, "%s_vb_locks[%u]", Prefix, Pool);
            NcCheckBufferLocks(Name, Vertex);
            Vertex->Release();
        }
        Index = nullptr;
        if (SUCCEEDED(Device->CreateIndexBuffer(256, 0, D3DFMT_INDEX32, Pool, &Index, nullptr)))
        {
            sprintf_s(Name, "%s_ib_locks[%u]", Prefix, Pool);
            NcCheckBufferLocks(Name, Index);
            Index->Release();
        }
    }
}

/* Buffers on an extended hardware device and on plain hardware, mixed and software devices */
static
VOID
NcCheckBuffers(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
    } PlainDevices[] =
    {
        { "buffers_hardware", D3DCREATE_HARDWARE_VERTEXPROCESSING },
        { "buffers_mixed", D3DCREATE_MIXED_VERTEXPROCESSING },
        { "buffers_software", D3DCREATE_SOFTWARE_VERTEXPROCESSING },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &DeviceEx);
    printf("buffers_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckBuffersOn("buffers_ex", DeviceEx);
        printf("buffers_ex_release = %lu\n", DeviceEx->Release());
    }

    for (const auto& Kind : PlainDevices)
    {
        IDirect3DDevice9* Device = nullptr;

        hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Kind.Behavior, &Parameters, &Device);
        printf("%s_create = %08lX\n", Kind.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckBuffersOn(Kind.Name, Device);
        printf("%s_release = %lu\n", Kind.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Describes a texture creation: level count, the first and last level and what a lock of the top level gives */
static
VOID
NcPrintTextureMade(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result,
    _In_opt_ IDirect3DTexture9* Texture)
{
    D3DSURFACE_DESC First = {};
    D3DSURFACE_DESC Last = {};
    D3DLOCKED_RECT Locked = {};
    DWORD Levels;
    HRESULT Lock;

    if (Texture == nullptr)
    {
        printf("%s = %08lX null\n", Name, Result);
        return;
    }
    Levels = Texture->GetLevelCount();
    Texture->GetLevelDesc(0, &First);
    Texture->GetLevelDesc(Levels - 1, &Last);
    Lock = Texture->LockRect(0, &Locked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(Lock))
        Texture->UnlockRect(0);
    printf("%s = %08lX levels %lu fmt %u type %u usage %08lX pool %u size %ux%u last %ux%u lock %08lX pitch %d "
           "lod %lu filter %u\n", Name, Result, Levels, First.Format, First.Type, First.Usage, First.Pool, First.Width,
           First.Height, Last.Width, Last.Height, Lock, Locked.Pitch, Texture->GetLOD(),
           Texture->GetAutoGenFilterType());
    printf("%s_release = %lu\n", Name, Texture->Release());
}

/* Reference counts, containers and descriptions of the surfaces a texture hands out */
static
VOID
NcCheckTextureLevels(
    _In_z_ PCSTR Name,
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DTexture9* Texture)
{
    IDirect3DSurface9* Level = nullptr;
    IDirect3DSurface9* Again = nullptr;
    IDirect3DTexture9* Container = nullptr;
    IDirect3DDevice9* Owner = nullptr;
    IUnknown* Unknown = nullptr;
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    printf("%s_addref = %lu\n", Name, Texture->AddRef());
    printf("%s_release = %lu\n", Name, Texture->Release());
    hr = Texture->GetSurfaceLevel(1, &Level);
    printf("%s_level = %08lX\n", Name, hr);
    if (Level == nullptr)
        return;
    printf("%s_level_texture_addref = %lu\n", Name, Texture->AddRef());
    printf("%s_level_texture_release = %lu\n", Name, Texture->Release());
    printf("%s_level_addref = %lu\n", Name, Level->AddRef());
    printf("%s_level_release = %lu\n", Name, Level->Release());
    hr = Texture->GetSurfaceLevel(1, &Again);
    printf("%s_level_again = %08lX same %d\n", Name, hr, Again == Level);
    if (Again != nullptr)
        printf("%s_level_again_release = %lu\n", Name, Again->Release());
    Level->GetDesc(&Desc);
    printf("%s_level_desc = fmt %u type %u usage %08lX pool %u ms %u:%lu size %ux%u rtype %u\n", Name, Desc.Format,
           Desc.Type, Desc.Usage, Desc.Pool, Desc.MultiSampleType, Desc.MultiSampleQuality, Desc.Width, Desc.Height,
           Level->GetType());
    hr = Level->GetContainer(__uuidof(IDirect3DTexture9), reinterpret_cast<VOID**>(&Container));
    printf("%s_level_container = %08lX same %d\n", Name, hr, Container == Texture);
    if (Container != nullptr)
        Container->Release();
    hr = Level->GetContainer(__uuidof(IDirect3DDevice9), reinterpret_cast<VOID**>(&Unknown));
    printf("%s_level_container_device = %08lX\n", Name, hr);
    if (Unknown != nullptr)
        Unknown->Release();
    hr = Level->GetDevice(&Owner);
    printf("%s_level_device = %08lX same %d\n", Name, hr, Owner == Device);
    if (Owner != nullptr)
        Owner->Release();
    printf("%s_level_priority = %lu %lu\n", Name, Level->SetPriority(3), Level->GetPriority());
    printf("%s_level_final_release = %lu\n", Name, Level->Release());
    printf("%s_level_bad = %08lX\n", Name, Texture->GetSurfaceLevel(Texture->GetLevelCount(), &Level));
    printf("%s_desc_bad = %08lX\n", Name, Texture->GetLevelDesc(Texture->GetLevelCount(), &Desc));
}

/* Bytes written through a texture lock and read through its level surface */
static
VOID
NcCheckTextureBytes(
    _In_z_ PCSTR Name,
    _In_ IDirect3DTexture9* Texture)
{
    static const RECT Rects[] = { { 0, 0, 1, 1 }, { 2, 1, 6, 3 }, { 0, 0, 9, 4 }, { 3, 3, 3, 4 }, { -1, 0, 2, 2 } };
    IDirect3DSurface9* Level = nullptr;
    D3DLOCKED_RECT Locked = {};
    D3DLOCKED_RECT Whole = {};
    HRESULT hr;

    hr = Texture->LockRect(1, &Locked, nullptr, 0);
    printf("%s_lock = %08lX pitch %d\n", Name, hr, Locked.Pitch);
    if (FAILED(hr))
        return;
    static_cast<BYTE*>(Locked.pBits)[0] = 0x3C;
    static_cast<BYTE*>(Locked.pBits)[Locked.Pitch + 4] = 0xC3;
    printf("%s_lock_again = %08lX\n", Name, Texture->LockRect(1, &Whole, nullptr, 0));
    printf("%s_unlock = %08lX\n", Name, Texture->UnlockRect(1));
    printf("%s_unlock_again = %08lX\n", Name, Texture->UnlockRect(1));
    printf("%s_unlock_other = %08lX\n", Name, Texture->UnlockRect(0));
    printf("%s_unlock_bad = %08lX\n", Name, Texture->UnlockRect(30));
    printf("%s_lock_bad = %08lX\n", Name, Texture->LockRect(30, &Whole, nullptr, 0));

    Texture->GetSurfaceLevel(1, &Level);
    if (Level != nullptr)
    {
        hr = Level->LockRect(&Whole, nullptr, D3DLOCK_READONLY);
        printf("%s_level_read = %08lX %02X %02X\n", Name, hr, static_cast<BYTE*>(Whole.pBits)[0],
               static_cast<BYTE*>(Whole.pBits)[Whole.Pitch + 4]);
        printf("%s_texture_unlock_while_level = %08lX\n", Name, Texture->UnlockRect(1));
        printf("%s_level_unlock = %08lX\n", Name, Level->UnlockRect());
        Level->Release();
    }
    for (const RECT& Rect : Rects)
    {
        BYTE* Base = nullptr;

        if (SUCCEEDED(Texture->LockRect(1, &Whole, nullptr, D3DLOCK_READONLY)))
        {
            Base = static_cast<BYTE*>(Whole.pBits);
            Texture->UnlockRect(1);
        }
        hr = Texture->LockRect(1, &Locked, &Rect, D3DLOCK_READONLY);
        printf("%s_rect(%ld,%ld,%ld,%ld) = %08lX offset %lld\n", Name, Rect.left, Rect.top, Rect.right, Rect.bottom,
               hr, SUCCEEDED(hr) && Base != nullptr ? static_cast<long long>(static_cast<BYTE*>(Locked.pBits) - Base)
                                                    : -1LL);
        if (SUCCEEDED(hr))
            Texture->UnlockRect(1);
        printf("%s_dirty(%ld,%ld,%ld,%ld) = %08lX\n", Name, Rect.left, Rect.top, Rect.right, Rect.bottom,
               Texture->AddDirtyRect(&Rect));
    }
    printf("%s_lod = %lu %lu %lu %lu\n", Name, Texture->SetLOD(2), Texture->GetLOD(), Texture->SetLOD(40),
           Texture->GetLOD());
    printf("%s_filter = %u %08lX %08lX %08lX %u\n", Name, Texture->GetAutoGenFilterType(),
           Texture->SetAutoGenFilterType(D3DTEXF_NONE), Texture->SetAutoGenFilterType(D3DTEXF_ANISOTROPIC),
           Texture->SetAutoGenFilterType(D3DTEXF_LINEAR), Texture->GetAutoGenFilterType());
    printf("%s_priority = %lu %lu %lu\n", Name, Texture->SetPriority(7), Texture->SetPriority(9),
           Texture->GetPriority());
}

/* Every texture creation call across formats, pools, usages, sizes and level counts */
static
VOID
NcCheckTexturesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const D3DPOOL Pools[] = { D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH,
                                     static_cast<D3DPOOL>(4), static_cast<D3DPOOL>(6) };
    static const DWORD Usages[] =
    {
        0, D3DUSAGE_RENDERTARGET, D3DUSAGE_DEPTHSTENCIL, D3DUSAGE_DYNAMIC, D3DUSAGE_AUTOGENMIPMAP, D3DUSAGE_DMAP,
        D3DUSAGE_WRITEONLY, D3DUSAGE_SOFTWAREPROCESSING, D3DUSAGE_DONOTCLIP, D3DUSAGE_RESTRICTED_CONTENT,
        D3DUSAGE_RESTRICT_SHARED_RESOURCE, D3DUSAGE_NONSECURE, D3DUSAGE_TEXTAPI, 0x00400000, 0x04000000,
        D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_RENDERTARGET, D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET,
        D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL,
    };
    static const struct
    {
        UINT Width;
        UINT Height;
        UINT Levels;
    } Shapes[] =
    {
        { 16, 8, 0 }, { 16, 8, 1 }, { 16, 8, 5 }, { 16, 8, 6 }, { 10, 6, 0 }, { 10, 6, 4 }, { 10, 6, 5 }, { 1, 1, 0 },
        { 1, 1, 2 }, { 0, 4, 1 }, { 4, 0, 1 }, { 4096, 4, 0 }, { 16384, 16, 1 }, { 32768, 1, 1 }, { 8, 8, 33 },
    };
    static const D3DFORMAT ShapeFormats[] = { D3DFMT_A8R8G8B8, D3DFMT_DXT1, D3DFMT_A1, D3DFMT_YUY2 };
    IDirect3DTexture9* Texture;
    HANDLE Shared;
    char Name[96];
    HRESULT hr;

    printf("%s_available = %u\n", Prefix, Device->GetAvailableTextureMem());
    for (D3DFORMAT Format : NcFormats)
    {
        for (D3DPOOL Pool : Pools)
        {
            Texture = nullptr;
            hr = Device->CreateTexture(16, 16, 0, 0, Format, Pool, &Texture, nullptr);
            sprintf_s(Name, "%s_format[%u,%u]", Prefix, Format, Pool);
            NcPrintTextureMade(Name, hr, Texture);
        }
        Texture = nullptr;
        hr = Device->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, Format, D3DPOOL_DEFAULT, &Texture, nullptr);
        sprintf_s(Name, "%s_format_target[%u]", Prefix, Format);
        NcPrintTextureMade(Name, hr, Texture);
        Texture = nullptr;
        hr = Device->CreateTexture(16, 16, 1, D3DUSAGE_DEPTHSTENCIL, Format, D3DPOOL_DEFAULT, &Texture, nullptr);
        sprintf_s(Name, "%s_format_depth[%u]", Prefix, Format);
        NcPrintTextureMade(Name, hr, Texture);
        Texture = nullptr;
        hr = Device->CreateTexture(16, 16, 0, D3DUSAGE_AUTOGENMIPMAP, Format, D3DPOOL_DEFAULT, &Texture, nullptr);
        sprintf_s(Name, "%s_format_autogen[%u]", Prefix, Format);
        NcPrintTextureMade(Name, hr, Texture);
    }
    for (DWORD Usage : Usages)
    {
        for (D3DPOOL Pool : Pools)
        {
            Texture = nullptr;
            hr = Device->CreateTexture(16, 8, 0, Usage, D3DFMT_A8R8G8B8, Pool, &Texture, nullptr);
            sprintf_s(Name, "%s_usage[%08lX,%u]", Prefix, Usage, Pool);
            NcPrintTextureMade(Name, hr, Texture);
        }
    }
    for (D3DFORMAT Format : ShapeFormats)
    {
        for (const auto& Shape : Shapes)
        {
            Texture = nullptr;
            hr = Device->CreateTexture(Shape.Width, Shape.Height, Shape.Levels, 0, Format, D3DPOOL_SYSTEMMEM, &Texture,
                                       nullptr);
            sprintf_s(Name, "%s_shape_sysmem[%u,%ux%u,%u]", Prefix, Format, Shape.Width, Shape.Height, Shape.Levels);
            NcPrintTextureMade(Name, hr, Texture);
            Texture = nullptr;
            hr = Device->CreateTexture(Shape.Width, Shape.Height, Shape.Levels, 0, Format, D3DPOOL_DEFAULT, &Texture,
                                       nullptr);
            sprintf_s(Name, "%s_shape_default[%u,%ux%u,%u]", Prefix, Format, Shape.Width, Shape.Height, Shape.Levels);
            NcPrintTextureMade(Name, hr, Texture);
        }
    }

    Texture = reinterpret_cast<IDirect3DTexture9*>(1);
    hr = Device->CreateTexture(16, 8, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, &Texture, nullptr);
    printf("%s_unknown_out = %08lX %s\n", Prefix, hr, Texture == nullptr ? "null" : "kept");
    Texture = reinterpret_cast<IDirect3DTexture9*>(1);
    hr = Device->CreateTexture(16, 8, 1, D3DUSAGE_WRITEONLY, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr);
    printf("%s_usage_out = %08lX %s\n", Prefix, hr, Texture == nullptr ? "null" : "kept");
    Texture = reinterpret_cast<IDirect3DTexture9*>(1);
    hr = Device->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture, nullptr);
    if (Texture == reinterpret_cast<IDirect3DTexture9*>(1))
    {
        printf("%s_managed_out = %08lX kept\n", Prefix, hr);
    }
    else
    {
        sprintf_s(Name, "%s_managed_out", Prefix);
        NcPrintTextureMade(Name, hr, Texture);
    }
    Shared = nullptr;
    Texture = nullptr;
    hr = Device->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, &Shared);
    sprintf_s(Name, "%s_shared", Prefix);
    NcPrintTextureMade(Name, hr, Texture);

    static const struct
    {
        PCSTR Name;
        DWORD Usage;
        D3DPOOL Pool;
    } Kinds[] =
    {
        { "default", 0, D3DPOOL_DEFAULT },
        { "dynamic", D3DUSAGE_DYNAMIC, D3DPOOL_DEFAULT },
        { "sysmem", 0, D3DPOOL_SYSTEMMEM },
        { "scratch", 0, D3DPOOL_SCRATCH },
        { "managed", 0, D3DPOOL_MANAGED },
        { "target", D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT },
    };
    for (const auto& Kind : Kinds)
    {
        Texture = nullptr;
        hr = Device->CreateTexture(16, 8, 0, Kind.Usage, D3DFMT_A8R8G8B8, Kind.Pool, &Texture, nullptr);
        if (Texture == nullptr)
            continue;
        sprintf_s(Name, "%s_%s", Prefix, Kind.Name);
        NcCheckTextureLevels(Name, Device, Texture);
        NcCheckTextureBytes(Name, Texture);
        printf("%s_final_release = %lu\n", Name, Texture->Release());
    }
}

/* Cube faces and volume levels: creation, descriptions, locks and bytes */
static
VOID
NcCheckCubesAndVolumesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const D3DPOOL Pools[] = { D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH };
    static const DWORD Usages[] = { 0, D3DUSAGE_RENDERTARGET, D3DUSAGE_DEPTHSTENCIL, D3DUSAGE_DYNAMIC,
                                    D3DUSAGE_AUTOGENMIPMAP, D3DUSAGE_WRITEONLY };
    static const UINT Edges[][2] = { { 8, 0 }, { 8, 4 }, { 8, 5 }, { 6, 0 }, { 0, 1 }, { 1, 0 } };
    static const UINT Boxes[][4] = { { 8, 8, 4, 0 }, { 8, 4, 2, 2 }, { 8, 8, 8, 5 }, { 6, 4, 2, 0 }, { 4, 4, 0, 1 },
                                     { 8, 8, 4, 6 } };
    IDirect3DCubeTexture9* Cube;
    IDirect3DVolumeTexture9* Volume;
    IDirect3DSurface9* Face;
    IDirect3DVolume9* Level;
    D3DSURFACE_DESC Desc;
    D3DVOLUME_DESC VolumeDesc;
    D3DLOCKED_RECT Locked;
    D3DLOCKED_BOX Box;
    char Name[96];
    HRESULT hr;

    for (D3DPOOL Pool : Pools)
    {
        for (DWORD Usage : Usages)
        {
            for (const auto& Edge : Edges)
            {
                Cube = nullptr;
                hr = Device->CreateCubeTexture(Edge[0], Edge[1], Usage, D3DFMT_A8R8G8B8, Pool, &Cube, nullptr);
                sprintf_s(Name, "%s_cube[%u,%08lX,%u,%u]", Prefix, Pool, Usage, Edge[0], Edge[1]);
                if (Cube == nullptr)
                {
                    printf("%s = %08lX null\n", Name, hr);
                    continue;
                }
                Desc = {};
                Cube->GetLevelDesc(Cube->GetLevelCount() - 1, &Desc);
                Locked = {};
                hr = Cube->LockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0, &Locked, nullptr, 0);
                printf("%s = levels %lu type %u usage %08lX pool %u last %ux%u lock %08lX pitch %d rtype %u\n", Name,
                       Cube->GetLevelCount(), Desc.Type, Desc.Usage, Desc.Pool, Desc.Width, Desc.Height, hr,
                       Locked.Pitch, Cube->GetType());
                if (SUCCEEDED(hr))
                {
                    static_cast<BYTE*>(Locked.pBits)[1] = 0x99;
                    Cube->UnlockRect(D3DCUBEMAP_FACE_NEGATIVE_X, 0);
                }
                Face = nullptr;
                hr = Cube->GetCubeMapSurface(D3DCUBEMAP_FACE_NEGATIVE_X, 0, &Face);
                if (Face != nullptr)
                {
                    Locked = {};
                    hr = Face->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
                    printf("%s_face = %08lX byte %02X\n", Name, hr,
                           SUCCEEDED(hr) ? static_cast<BYTE*>(Locked.pBits)[1] : 0);
                    if (SUCCEEDED(hr))
                        Face->UnlockRect();
                    printf("%s_face_release = %lu\n", Name, Face->Release());
                }
                printf("%s_face_bad = %08lX %08lX\n", Name,
                       Cube->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(6), 0, &Face),
                       Cube->GetCubeMapSurface(D3DCUBEMAP_FACE_POSITIVE_X, 9, &Face));
                printf("%s_release = %lu\n", Name, Cube->Release());
            }
            for (const auto& Shape : Boxes)
            {
                Volume = nullptr;
                hr = Device->CreateVolumeTexture(Shape[0], Shape[1], Shape[2], Shape[3], Usage, D3DFMT_A8R8G8B8,
                                                 Pool, &Volume, nullptr);
                sprintf_s(Name, "%s_volume[%u,%08lX,%ux%ux%u,%u]", Prefix, Pool, Usage, Shape[0], Shape[1],
                          Shape[2], Shape[3]);
                if (Volume == nullptr)
                {
                    printf("%s = %08lX null\n", Name, hr);
                    continue;
                }
                VolumeDesc = {};
                Volume->GetLevelDesc(Volume->GetLevelCount() - 1, &VolumeDesc);
                Box = {};
                hr = Volume->LockBox(0, &Box, nullptr, 0);
                printf("%s = levels %lu type %u usage %08lX pool %u last %ux%ux%u lock %08lX pitch %d %d rtype %u\n",
                       Name, Volume->GetLevelCount(), VolumeDesc.Type, VolumeDesc.Usage, VolumeDesc.Pool,
                       VolumeDesc.Width, VolumeDesc.Height, VolumeDesc.Depth, hr, Box.RowPitch, Box.SlicePitch,
                       Volume->GetType());
                if (SUCCEEDED(hr))
                {
                    static_cast<BYTE*>(Box.pBits)[Box.SlicePitch + 2] = 0x66;
                    Volume->UnlockBox(0);
                }
                Level = nullptr;
                hr = Volume->GetVolumeLevel(0, &Level);
                if (Level != nullptr)
                {
                    D3DBOX Part = { 1, 0, 3, 2, 1, 2 };
                    BYTE* Base = static_cast<BYTE*>(Box.pBits);
                    IDirect3DVolumeTexture9* Container = nullptr;

                    Box = {};
                    hr = Level->LockBox(&Box, nullptr, D3DLOCK_READONLY);
                    printf("%s_level = %08lX byte %02X\n", Name, hr,
                           SUCCEEDED(hr) ? static_cast<BYTE*>(Box.pBits)[Box.SlicePitch + 2] : 0);
                    if (SUCCEEDED(hr))
                    {
                        Base = static_cast<BYTE*>(Box.pBits);
                        Level->UnlockBox();
                    }
                    hr = Level->LockBox(&Box, &Part, D3DLOCK_READONLY);
                    printf("%s_level_part = %08lX offset %lld\n", Name, hr,
                           SUCCEEDED(hr) && Base != nullptr ? static_cast<long long>(static_cast<BYTE*>(Box.pBits) -
                                                                                     Base) : -1LL);
                    if (SUCCEEDED(hr))
                        Level->UnlockBox();
                    printf("%s_level_unlock_again = %08lX\n", Name, Level->UnlockBox());
                    Level->GetDesc(&VolumeDesc);
                    hr = Level->GetContainer(__uuidof(IDirect3DVolumeTexture9), reinterpret_cast<VOID**>(&Container));
                    printf("%s_level_desc = fmt %u type %u usage %08lX pool %u size %ux%ux%u container %08lX %d\n",
                           Name, VolumeDesc.Format, VolumeDesc.Type, VolumeDesc.Usage, VolumeDesc.Pool,
                           VolumeDesc.Width, VolumeDesc.Height, VolumeDesc.Depth, hr, Container == Volume);
                    if (Container != nullptr)
                        Container->Release();
                    printf("%s_level_release = %lu\n", Name, Level->Release());
                }
                printf("%s_dirty = %08lX\n", Name, Volume->AddDirtyBox(nullptr));
                printf("%s_release = %lu\n", Name, Volume->Release());
            }
        }
    }
}

/* Textures on a windowed extended device and on devices of the plain object */
static
VOID
NcCheckTextures(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &DeviceEx);
    printf("textures_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckTexturesOn("textures_ex", DeviceEx);
        NcCheckCubesAndVolumesOn("textures_ex", DeviceEx);
        printf("textures_ex_release = %lu\n", DeviceEx->Release());
    }

    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    printf("textures_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckTexturesOn("textures_plain", Device);
        NcCheckCubesAndVolumesOn("textures_plain", Device);
        printf("textures_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Private data on one resource: flags, sizes, replacing, interface pointers and freeing */
static
VOID
NcCheckPrivateData(
    _In_z_ PCSTR Name,
    _In_ IDirect3DResource9* Resource,
    _In_ IUnknown* Object)
{
    static const GUID First = { 0x1A2B3C4D, 0x1111, 0x2222, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    static const GUID Second = { 0x1A2B3C4D, 0x1111, 0x2222, { 8, 7, 6, 5, 4, 3, 2, 1 } };
    BYTE Bytes[12] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0, 0xB0, 0xC0 };
    BYTE Read[16] = {};
    IUnknown* Back = nullptr;
    DWORD Size = 16;
    ULONG Count;
    HRESULT hr;

    printf("%s_private_get_none = %08lX\n", Name, Resource->GetPrivateData(First, Read, &Size));
    printf("%s_private_free_none = %08lX\n", Name, Resource->FreePrivateData(First));
    printf("%s_private_bad_flags = %08lX\n", Name, Resource->SetPrivateData(First, Bytes, 4, 2));
    printf("%s_private_unknown_size = %08lX\n", Name, Resource->SetPrivateData(First, Object, 4, D3DSPD_IUNKNOWN));
    printf("%s_private_set = %08lX\n", Name, Resource->SetPrivateData(First, Bytes, sizeof(Bytes), 0));
    Size = 4;
    hr = Resource->GetPrivateData(First, Read, &Size);
    printf("%s_private_get_small = %08lX size %lu byte %02X\n", Name, hr, Size, Read[0]);
    Size = 0;
    hr = Resource->GetPrivateData(First, nullptr, &Size);
    printf("%s_private_get_size = %08lX size %lu\n", Name, hr, Size);
    Size = 16;
    hr = Resource->GetPrivateData(First, Read, &Size);
    printf("%s_private_get = %08lX size %lu bytes %02X %02X %02X\n", Name, hr, Size, Read[0], Read[11], Read[12]);
    printf("%s_private_replace = %08lX\n", Name, Resource->SetPrivateData(First, Bytes + 4, 2, 0));
    Size = 16;
    hr = Resource->GetPrivateData(First, Read, &Size);
    printf("%s_private_get_again = %08lX size %lu byte %02X %02X\n", Name, hr, Size, Read[0], Read[2]);
    printf("%s_private_zero = %08lX\n", Name, Resource->SetPrivateData(Second, Bytes, 0, 0));
    Size = 16;
    hr = Resource->GetPrivateData(Second, Read, &Size);
    printf("%s_private_get_zero = %08lX size %lu\n", Name, hr, Size);

    Object->AddRef();
    Count = Object->Release();
    hr = Resource->SetPrivateData(Second, Object, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    printf("%s_private_unknown = %08lX refs %lu", Name, hr, Count);
    Object->AddRef();
    printf(" %lu", Object->Release());
    Size = sizeof(Back);
    hr = Resource->GetPrivateData(Second, &Back, &Size);
    printf(" get %08lX size %lu same %d", hr, Size, Back == Object);
    if (Back != nullptr)
        Back->Release();
    printf(" free %08lX", Resource->FreePrivateData(Second));
    Object->AddRef();
    printf(" refs %lu\n", Object->Release());
    hr = Resource->FreePrivateData(First);
    printf("%s_private_free = %08lX %08lX\n", Name, hr, Resource->FreePrivateData(First));
    printf("%s_priority = %lu", Name, Resource->SetPriority(4));
    printf(" %lu", Resource->SetPriority(6));
    printf(" %lu type %u\n", Resource->GetPriority(), Resource->GetType());
    Resource->PreLoad();
}

/* Private data and priority across every kind of resource */
static
VOID
NcCheckResourcesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DTexture9* Other = nullptr;
    IDirect3DVolumeTexture9* Volume = nullptr;
    IDirect3DSurface9* Surface = nullptr;
    IDirect3DSurface9* Level = nullptr;
    IDirect3DVolume9* Slice = nullptr;
    IDirect3DVertexBuffer9* Vertices = nullptr;
    IDirect3DIndexBuffer9* Indices = nullptr;
    char Name[96];

    Device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Other, nullptr);
    if (Other == nullptr)
        return;
    for (D3DPOOL Pool : { D3DPOOL_DEFAULT, D3DPOOL_MANAGED, D3DPOOL_SYSTEMMEM, D3DPOOL_SCRATCH })
    {
        Texture = nullptr;
        if (SUCCEEDED(Device->CreateTexture(8, 8, 2, 0, D3DFMT_A8R8G8B8, Pool, &Texture, nullptr)))
        {
            sprintf_s(Name, "%s_texture[%u]", Prefix, Pool);
            NcCheckPrivateData(Name, Texture, Other);
            if (SUCCEEDED(Texture->GetSurfaceLevel(1, &Level)))
            {
                sprintf_s(Name, "%s_texture_level[%u]", Prefix, Pool);
                NcCheckPrivateData(Name, Level, Other);
                Level->Release();
            }
            Texture->Release();
        }
        Surface = nullptr;
        if (SUCCEEDED(Device->CreateOffscreenPlainSurface(8, 8, D3DFMT_A8R8G8B8, Pool, &Surface, nullptr)))
        {
            sprintf_s(Name, "%s_surface[%u]", Prefix, Pool);
            NcCheckPrivateData(Name, Surface, Other);
            Surface->Release();
        }
        Vertices = nullptr;
        if (SUCCEEDED(Device->CreateVertexBuffer(64, 0, 0, Pool, &Vertices, nullptr)))
        {
            sprintf_s(Name, "%s_vertices[%u]", Prefix, Pool);
            NcCheckPrivateData(Name, Vertices, Other);
            Vertices->Release();
        }
        Indices = nullptr;
        if (SUCCEEDED(Device->CreateIndexBuffer(64, 0, D3DFMT_INDEX16, Pool, &Indices, nullptr)))
        {
            sprintf_s(Name, "%s_indices[%u]", Prefix, Pool);
            NcCheckPrivateData(Name, Indices, Other);
            Indices->Release();
        }
        Volume = nullptr;
        if (SUCCEEDED(Device->CreateVolumeTexture(4, 4, 2, 1, 0, D3DFMT_A8R8G8B8, Pool, &Volume, nullptr)))
        {
            sprintf_s(Name, "%s_volume[%u]", Prefix, Pool);
            NcCheckPrivateData(Name, Volume, Other);
            if (SUCCEEDED(Volume->GetVolumeLevel(0, &Slice)))
            {
                static const GUID Tag = { 0x5E5E5E5E, 0x3333, 0x4444, { 9, 9, 9, 9, 9, 9, 9, 9 } };
                DWORD Value = 0x1234;
                DWORD Size = sizeof(Value);

                HRESULT hr;

                printf("%s_slice = %08lX", Name, Slice->SetPrivateData(Tag, &Value, sizeof(Value), 0));
                Value = 0;
                hr = Slice->GetPrivateData(Tag, &Value, &Size);
                printf(" %08lX %08lX", hr, Value);
                hr = Slice->FreePrivateData(Tag);
                printf(" %08lX %08lX\n", hr, Slice->FreePrivateData(Tag));
                Slice->Release();
            }
            Volume->Release();
        }
    }
    if (SUCCEEDED(Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Surface)))
    {
        sprintf_s(Name, "%s_backbuffer", Prefix);
        NcCheckPrivateData(Name, Surface, Other);
        Surface->Release();
    }
    if (SUCCEEDED(Device->CreateRenderTarget(8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr)))
    {
        sprintf_s(Name, "%s_target", Prefix);
        NcCheckPrivateData(Name, Surface, Other);
        Surface->Release();
    }
    printf("%s_other_release = %lu\n", Prefix, Other->Release());
}

/* Resources on a windowed extended device and on a device of the plain object */
static
VOID
NcCheckResources(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &DeviceEx);
    printf("resources_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckResourcesOn("resources_ex", DeviceEx);
        printf("resources_ex_release = %lu\n", DeviceEx->Release());
    }
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    printf("resources_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckResourcesOn("resources_plain", Device);
        printf("resources_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Makes one resource of a kind the reset checks hold across a reset */
static
IUnknown*
NcMakeResetResource(
    _In_ IDirect3DDevice9* Device,
    _In_ UINT Kind)
{
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DSurface9* Surface = nullptr;
    IDirect3DVertexBuffer9* Vertices = nullptr;
    IDirect3DIndexBuffer9* Indices = nullptr;
    IDirect3DCubeTexture9* Cube = nullptr;

    switch (Kind)
    {
    case 0:
        Device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr);
        return Texture;
    case 1:
        Device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Texture, nullptr);
        return Texture;
    case 2:
        Device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture, nullptr);
        return Texture;
    case 3:
        Device->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
        return Surface;
    case 4:
        Device->CreateOffscreenPlainSurface(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, nullptr);
        return Surface;
    case 5:
        Device->CreateOffscreenPlainSurface(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, nullptr);
        return Surface;
    case 6:
        Device->CreateVertexBuffer(64, 0, 0, D3DPOOL_DEFAULT, &Vertices, nullptr);
        return Vertices;
    case 7:
        Device->CreateIndexBuffer(64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &Indices,
                                  nullptr);
        return Indices;
    case 8:
        Device->CreateVertexBuffer(64, 0, 0, D3DPOOL_SYSTEMMEM, &Vertices, nullptr);
        return Vertices;
    case 9:
        Device->CreateCubeTexture(8, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Cube, nullptr);
        return Cube;
    case 10:
        Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Surface);
        return Surface;
    case 11:
        Device->GetDepthStencilSurface(&Surface);
        return Surface;
    default:
        Device->CreateDepthStencilSurface(16, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
        return Surface;
    }
}

/* Resets while resources of each kind live, what the device says afterwards, and a reset once they are gone */
static
VOID
NcCheckResetOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_opt_ IDirect3DDevice9Ex* DeviceEx,
    _In_ D3DPRESENT_PARAMETERS* Parameters)
{
    D3DPRESENT_PARAMETERS Again;
    IUnknown* Held;
    HRESULT hr;

    for (UINT Kind = 0; Kind <= 12; Kind++)
    {
        Held = NcMakeResetResource(Device, Kind);
        Again = *Parameters;
        hr = Device->Reset(&Again);
        printf("%s_reset[%u] = held %d reset %08lX", Prefix, Kind, Held != nullptr, hr);
        printf(" cooperative %08lX", Device->TestCooperativeLevel());
        if (DeviceEx != nullptr)
        {
            Again = *Parameters;
            hr = DeviceEx->ResetEx(&Again, nullptr);
            printf(" resetex %08lX state %08lX", hr, DeviceEx->CheckDeviceState(nullptr));
        }
        printf(" clear %08lX", Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0));
        if (Held != nullptr)
            printf(" release %lu", Held->Release());
        printf(" before %08lX", Device->TestCooperativeLevel());
        Again = *Parameters;
        hr = Device->Reset(&Again);
        printf(" after %08lX cooperative %08lX", hr, Device->TestCooperativeLevel());
        printf(" present %08lX\n", Device->Present(nullptr, nullptr, nullptr, nullptr));
        fflush(stdout);
    }
}

/* Device resets against held resources on an extended device and on a device of the plain object */
static
VOID
NcCheckReset(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DPRESENT_PARAMETERS Made;
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Parameters.EnableAutoDepthStencil = TRUE;
    Parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
    Made = Parameters;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, nullptr,
                                   &DeviceEx);
    printf("reset_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckResetOn("reset_ex", DeviceEx, DeviceEx, &Parameters);
        printf("reset_ex_release = %lu\n", DeviceEx->Release());
    }

    Made = Parameters;
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, &Device);
    printf("reset_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckResetOn("reset_plain", Device, nullptr, &Parameters);
        printf("reset_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* The surfaces the copy checks move pixels between */
struct NcCopyKind
{
    PCSTR Name;
    UINT Make;
    D3DFORMAT Format;
    D3DPOOL Pool;
    DWORD Usage;
    UINT Width;
    UINT Height;
    D3DMULTISAMPLE_TYPE Samples;
};

enum
{
    NcMakePlain,
    NcMakeTarget,
    NcMakeLockableTarget,
    NcMakeDepth,
    NcMakeTexture,
    NcMakeBackBuffer,
};

static const NcCopyKind NcCopyKinds[] =
{
    { "sysmem", NcMakePlain, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "default", NcMakePlain, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "scratch", NcMakePlain, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "target", NcMakeTarget, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "target_lock", NcMakeLockableTarget, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "target_ms", NcMakeTarget, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_4_SAMPLES },
    { "depth", NcMakeDepth, D3DFMT_D24S8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "depth_lock", NcMakeDepth, D3DFMT_D16_LOCKABLE, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "depth_ms", NcMakeDepth, D3DFMT_D24S8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_4_SAMPLES },
    { "depth_big", NcMakeDepth, D3DFMT_D24S8, D3DPOOL_DEFAULT, 0, 32, 16, D3DMULTISAMPLE_NONE },
    { "tex_default", NcMakeTexture, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "tex_target", NcMakeTexture, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET, 16, 8,
      D3DMULTISAMPLE_NONE },
    { "tex_sysmem", NcMakeTexture, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "tex_managed", NcMakeTexture, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "tex_dynamic", NcMakeTexture, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC, 16, 8, D3DMULTISAMPLE_NONE },
    { "tex_depth", NcMakeTexture, D3DFMT_D24S8, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, 16, 8, D3DMULTISAMPLE_NONE },
    { "sysmem_x8", NcMakePlain, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "target_x8", NcMakeTarget, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "sysmem_r5", NcMakePlain, D3DFMT_R5G6B5, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "target_r5", NcMakeTarget, D3DFMT_R5G6B5, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "sysmem_big", NcMakePlain, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 32, 16, D3DMULTISAMPLE_NONE },
    { "target_big", NcMakeLockableTarget, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0, 32, 16, D3DMULTISAMPLE_NONE },
    { "backbuffer", NcMakeBackBuffer, D3DFMT_UNKNOWN, D3DPOOL_DEFAULT, 0, 0, 0, D3DMULTISAMPLE_NONE },
    { "sysmem_dxt1", NcMakePlain, D3DFMT_DXT1, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "default_dxt1", NcMakePlain, D3DFMT_DXT1, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "sysmem_d16", NcMakePlain, D3DFMT_D16_LOCKABLE, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "sysmem_yuy2", NcMakePlain, D3DFMT_YUY2, D3DPOOL_SYSTEMMEM, 0, 16, 8, D3DMULTISAMPLE_NONE },
    { "default_yuy2", NcMakePlain, D3DFMT_YUY2, D3DPOOL_DEFAULT, 0, 16, 8, D3DMULTISAMPLE_NONE },
};

/* Makes one surface of a copy kind; texture levels keep their texture alive */
static
IDirect3DSurface9*
NcMakeCopySurface(
    _In_ IDirect3DDevice9* Device,
    _In_ const NcCopyKind* Kind)
{
    IDirect3DSurface9* Surface = nullptr;
    IDirect3DTexture9* Texture = nullptr;

    switch (Kind->Make)
    {
    case NcMakePlain:
        Device->CreateOffscreenPlainSurface(Kind->Width, Kind->Height, Kind->Format, Kind->Pool, &Surface, nullptr);
        break;
    case NcMakeTarget:
    case NcMakeLockableTarget:
        Device->CreateRenderTarget(Kind->Width, Kind->Height, Kind->Format, Kind->Samples, 0,
                                   Kind->Make == NcMakeLockableTarget, &Surface, nullptr);
        break;
    case NcMakeDepth:
        Device->CreateDepthStencilSurface(Kind->Width, Kind->Height, Kind->Format, Kind->Samples, 0, FALSE, &Surface,
                                          nullptr);
        break;
    case NcMakeTexture:
        Device->CreateTexture(Kind->Width, Kind->Height, 1, Kind->Usage, Kind->Format, Kind->Pool, &Texture, nullptr);
        if (Texture != nullptr)
        {
            Texture->GetSurfaceLevel(0, &Surface);
            Texture->Release();
        }
        break;
    case NcMakeBackBuffer:
        Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Surface);
        break;
    }
    return Surface;
}

/* A hash of a surface's rows, or why it could not be read */
static
VOID
NcPrintSurfaceHash(
    _In_z_ PCSTR Name,
    _In_ IDirect3DSurface9* Surface)
{
    D3DSURFACE_DESC Desc = {};
    D3DLOCKED_RECT Locked = {};
    UINT Bytes;
    UINT Rows;
    DWORD Hash = 2166136261UL;
    HRESULT hr;

    Surface->GetDesc(&Desc);
    hr = Surface->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        printf("%s = lock %08lX\n", Name, hr);
        return;
    }
    Bytes = Desc.Width * (Desc.Format == D3DFMT_R5G6B5 || Desc.Format == D3DFMT_D16_LOCKABLE ||
                          Desc.Format == D3DFMT_X1R5G5B5 || Desc.Format == D3DFMT_A1R5G5B5 ? 2 : 4);
    if (Desc.Format == D3DFMT_R8G8B8)
        Bytes = Desc.Width * 3;
    Rows = Desc.Height;
    if (Desc.Format == D3DFMT_DXT1)
    {
        Bytes = (Desc.Width + 3) / 4 * 8;
        Rows = (Desc.Height + 3) / 4;
    }
    for (UINT Row = 0; Row < Rows; Row++)
    {
        const BYTE* Line = static_cast<const BYTE*>(Locked.pBits) + static_cast<SIZE_T>(Row) * Locked.Pitch;

        for (UINT Index = 0; Index < Bytes; Index++)
            Hash = (Hash ^ Line[Index]) * 16777619UL;
    }
    printf("%s = %08lX first %08lX hash %08lX\n", Name, hr, *static_cast<const DWORD*>(Locked.pBits), Hash);
    Surface->UnlockRect();
}

/* Writes a pattern that numbers every byte of a lockable 32 bit surface */
static
VOID
NcFillPattern(
    _In_ IDirect3DSurface9* Surface,
    _In_ BYTE Seed)
{
    D3DSURFACE_DESC Desc = {};
    D3DLOCKED_RECT Locked = {};

    Surface->GetDesc(&Desc);
    if (FAILED(Surface->LockRect(&Locked, nullptr, 0)))
        return;
    for (UINT Row = 0; Row < Desc.Height; Row++)
    {
        BYTE* Line = static_cast<BYTE*>(Locked.pBits) + static_cast<SIZE_T>(Row) * Locked.Pitch;

        for (UINT Index = 0; Index < Desc.Width * 4; Index++)
            Line[Index] = static_cast<BYTE>(Seed + Row * 31 + Index);
    }
    Surface->UnlockRect();
}

/* TRUE for a surface that is a level of a texture or cube texture */
static
BOOLEAN
NcIsTextureLevel(
    _In_ IDirect3DSurface9* Surface)
{
    IUnknown* Container = nullptr;

    if (SUCCEEDED(Surface->GetContainer(__uuidof(IDirect3DBaseTexture9), reinterpret_cast<VOID**>(&Container))))
    {
        Container->Release();
        return TRUE;
    }
    return FALSE;
}

/* The system's whole surface StretchRect on an extended device reads a discard flag past the end of a texture level
   object when the other surface is a depth buffer, so its answer depends on heap contents */
static
BOOLEAN
NcStretchReadsPastLevel(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Source,
    _In_ IDirect3DSurface9* Target)
{
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    D3DSURFACE_DESC From = {};
    D3DSURFACE_DESC To = {};
    BOOLEAN FromLevel;
    BOOLEAN ToLevel;
    BOOLEAN FromDepth;
    BOOLEAN ToDepth;

    if (FAILED(Device->QueryInterface(__uuidof(IDirect3DDevice9Ex), reinterpret_cast<VOID**>(&DeviceEx))))
        return FALSE;
    DeviceEx->Release();
    if (Source == Target)
        return FALSE;
    Source->GetDesc(&From);
    Target->GetDesc(&To);
    if (From.Format != To.Format || From.Width != To.Width || From.Height != To.Height ||
        From.Pool != D3DPOOL_DEFAULT || To.Pool != D3DPOOL_DEFAULT)
    {
        return FALSE;
    }
    FromLevel = NcIsTextureLevel(Source);
    ToLevel = NcIsTextureLevel(Target);
    FromDepth = !FromLevel && (From.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    ToDepth = !ToLevel && (To.Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    return (FromLevel || ToLevel) && (FromDepth || ToDepth);
}

/* Depth buffer StretchRect with and without rectangles, inside a scene and with discardable buffers: whole surface
   copies on an extended device skip the depth rules */
static
VOID
NcCheckDepthStretches(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DSurface9* Depth = nullptr;
    IDirect3DSurface9* Other = nullptr;
    IDirect3DSurface9* Discard = nullptr;
    IDirect3DSurface9* Level = nullptr;
    IDirect3DTexture9* Texture = nullptr;
    RECT Whole = { 0, 0, 16, 8 };

    Device->CreateDepthStencilSurface(16, 8, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Depth, nullptr);
    Device->CreateDepthStencilSurface(16, 8, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Other, nullptr);
    Device->CreateDepthStencilSurface(16, 8, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &Discard, nullptr);
    Device->CreateTexture(16, 8, 1, D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, D3DPOOL_DEFAULT, &Texture, nullptr);
    if (Texture != nullptr)
        Texture->GetSurfaceLevel(0, &Level);
    if (Depth == nullptr || Other == nullptr || Discard == nullptr || Level == nullptr)
    {
        printf("%s_depth_stretch = surfaces missing\n", Prefix);
    }
    else
    {
        printf("%s_depth_stretch[whole] = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, Other, nullptr, D3DTEXF_NONE));
        printf("%s_depth_stretch[rects] = %08lX\n", Prefix,
               Device->StretchRect(Depth, &Whole, Other, &Whole, D3DTEXF_NONE));
        printf("%s_depth_stretch[point] = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, Other, nullptr, D3DTEXF_POINT));
        printf("%s_depth_stretch[discard_from] = %08lX\n", Prefix,
               Device->StretchRect(Discard, nullptr, Other, nullptr, D3DTEXF_NONE));
        printf("%s_depth_stretch[discard_to] = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, Discard, nullptr, D3DTEXF_NONE));
        printf("%s_depth_stretch[discard_rects] = %08lX\n", Prefix,
               Device->StretchRect(Discard, &Whole, Other, &Whole, D3DTEXF_NONE));
        printf("%s_depth_stretch[level_rects] = %08lX\n", Prefix,
               Device->StretchRect(Depth, &Whole, Level, &Whole, D3DTEXF_NONE));
        printf("%s_depth_stretch[discard_level] = %08lX\n", Prefix,
               Device->StretchRect(Discard, nullptr, Level, nullptr, D3DTEXF_NONE));
        Device->BeginScene();
        printf("%s_depth_stretch[scene_whole] = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, Other, nullptr, D3DTEXF_NONE));
        printf("%s_depth_stretch[scene_rects] = %08lX\n", Prefix,
               Device->StretchRect(Depth, &Whole, Other, &Whole, D3DTEXF_NONE));
        Device->EndScene();
    }
    if (Level != nullptr)
        Level->Release();
    if (Texture != nullptr)
        Texture->Release();
    if (Discard != nullptr)
        Discard->Release();
    if (Other != nullptr)
        Other->Release();
    if (Depth != nullptr)
        Depth->Release();
}

/* Every pairing of copy kinds through UpdateSurface, StretchRect and GetRenderTargetData */
static
VOID
NcCheckCopyPairs(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DSurface9* Surfaces[_countof(NcCopyKinds)] = {};
    char Name[128];

    for (UINT Index = 0; Index < _countof(NcCopyKinds); Index++)
    {
        Surfaces[Index] = NcMakeCopySurface(Device, &NcCopyKinds[Index]);
        printf("%s_kind[%s] = %s\n", Prefix, NcCopyKinds[Index].Name, Surfaces[Index] != nullptr ? "made" : "none");
    }
    for (UINT Source = 0; Source < _countof(NcCopyKinds); Source++)
    {
        for (UINT Target = 0; Target < _countof(NcCopyKinds); Target++)
        {
            HRESULT Update;
            HRESULT Stretch = S_OK;
            HRESULT Linear;
            HRESULT Readback;
            BOOLEAN Unstable;
            char StretchText[16];

            if (Surfaces[Source] == nullptr || Surfaces[Target] == nullptr)
                continue;
            Update = Device->UpdateSurface(Surfaces[Source], nullptr, Surfaces[Target], nullptr);
            Unstable = NcStretchReadsPastLevel(Device, Surfaces[Source], Surfaces[Target]);
            if (!Unstable)
                Stretch = Device->StretchRect(Surfaces[Source], nullptr, Surfaces[Target], nullptr, D3DTEXF_NONE);
            Linear = Device->StretchRect(Surfaces[Source], nullptr, Surfaces[Target], nullptr, D3DTEXF_LINEAR);
            Readback = Device->GetRenderTargetData(Surfaces[Source], Surfaces[Target]);
            if (Unstable)
                strcpy_s(StretchText, "skipped");
            else
                sprintf_s(StretchText, "%08lX", Stretch);
            sprintf_s(Name, "%s[%s,%s]", Prefix, NcCopyKinds[Source].Name, NcCopyKinds[Target].Name);
            printf("%s update %08lX stretch %s linear %08lX readback %08lX\n", Name, Update, StretchText, Linear,
                   Readback);
        }
    }
    for (UINT Index = 0; Index < _countof(NcCopyKinds); Index++)
    {
        HDC Dc = NULL;
        HRESULT Fill;
        HRESULT hr;
        HRESULT Release = 0;

        if (Surfaces[Index] == nullptr)
            continue;
        Fill = Device->ColorFill(Surfaces[Index], nullptr, 0xFF102030);
        hr = Surfaces[Index]->GetDC(&Dc);
        if (Dc != NULL)
            Release = Surfaces[Index]->ReleaseDC(Dc);
        printf("%s_fill[%s] = %08lX dc %08lX %s release %08lX\n", Prefix, NcCopyKinds[Index].Name, Fill, hr,
               Dc != NULL ? "set" : "null", Release);
        printf("%s_front[%s] = %08lX\n", Prefix, NcCopyKinds[Index].Name,
               Device->GetFrontBufferData(0, Surfaces[Index]));
        Surfaces[Index]->Release();
    }
    /* UpdateSurface and UpdateTexture are left out: the system reads through the missing objects and crashes */
    printf("%s_null = %08lX %08lX %08lX\n", Prefix, Device->StretchRect(nullptr, nullptr, nullptr, nullptr, D3DTEXF_NONE),
           Device->GetRenderTargetData(nullptr, nullptr), Device->ColorFill(nullptr, nullptr, 0));
}

/* Rectangles, points and filters the copies accept, and the bytes they leave */
static
VOID
NcCheckCopyRects(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const RECT Rects[] =
    {
        { 2, 1, 10, 5 }, { 0, 0, 16, 8 }, { 0, 0, 17, 8 }, { -1, 0, 4, 4 }, { 4, 4, 4, 6 }, { 6, 2, 2, 6 },
        { 12, 4, 16, 8 }, { 1, 1, 5, 5 }, { 4, 4, 8, 8 },
    };
    static const POINT Points[] = { { 0, 0 }, { 4, 2 }, { 12, 4 }, { 13, 0 }, { -1, 0 }, { 0, 7 } };
    static const DWORD Filters[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x100 };
    IDirect3DSurface9* Sysmem = NcMakeCopySurface(Device, &NcCopyKinds[0]);
    IDirect3DSurface9* Target = NcMakeCopySurface(Device, &NcCopyKinds[4]);
    IDirect3DSurface9* Big = NcMakeCopySurface(Device, &NcCopyKinds[21]);
    IDirect3DSurface9* BigMemory = NcMakeCopySurface(Device, &NcCopyKinds[20]);
    IDirect3DSurface9* Multi = NcMakeCopySurface(Device, &NcCopyKinds[5]);
    IDirect3DSurface9* DxtMemory = NcMakeCopySurface(Device, &NcCopyKinds[23]);
    IDirect3DSurface9* Dxt = NcMakeCopySurface(Device, &NcCopyKinds[24]);
    IDirect3DSurface9* Depth = NcMakeCopySurface(Device, &NcCopyKinds[6]);
    IDirect3DSurface9* DepthOther = NcMakeCopySurface(Device, &NcCopyKinds[6]);
    char Name[128];
    HRESULT hr;

    if (Sysmem == nullptr || Target == nullptr || Big == nullptr || BigMemory == nullptr)
    {
        printf("%s_rects = missing surfaces\n", Prefix);
        return;
    }

    for (UINT RectIndex = 0; RectIndex < _countof(Rects); RectIndex++)
    {
        for (UINT PointIndex = 0; PointIndex < _countof(Points); PointIndex++)
        {
            NcFillPattern(Sysmem, static_cast<BYTE>(RectIndex * 7 + PointIndex));
            Device->ColorFill(Target, nullptr, 0xFF405060);
            hr = Device->UpdateSurface(Sysmem, &Rects[RectIndex], Target, &Points[PointIndex]);
            sprintf_s(Name, "%s_update[%u,%u] %08lX", Prefix, RectIndex, PointIndex, hr);
            NcPrintSurfaceHash(Name, Target);
        }
        hr = Device->UpdateSurface(Sysmem, &Rects[RectIndex], Target, nullptr);
        printf("%s_update_nopoint[%u] = %08lX\n", Prefix, RectIndex, hr);
        hr = Device->UpdateSurface(Sysmem, nullptr, Target, &Points[RectIndex % _countof(Points)]);
        printf("%s_update_norect[%u] = %08lX\n", Prefix, RectIndex, hr);

        Device->ColorFill(Big, nullptr, 0xFF000000);
        hr = Device->ColorFill(Big, &Rects[RectIndex], 0x80FF8040);
        sprintf_s(Name, "%s_fill_rect[%u] %08lX", Prefix, RectIndex, hr);
        NcPrintSurfaceHash(Name, Big);
        hr = Device->GetRenderTargetData(Big, BigMemory);
        sprintf_s(Name, "%s_readback[%u] %08lX", Prefix, RectIndex, hr);
        NcPrintSurfaceHash(Name, BigMemory);

        for (UINT DestIndex = 0; DestIndex < _countof(Rects); DestIndex += 2)
        {
            NcFillPattern(Target, static_cast<BYTE>(RectIndex));
            Device->ColorFill(Big, nullptr, 0xFF000000);
            hr = Device->StretchRect(Target, &Rects[RectIndex], Big, &Rects[DestIndex], D3DTEXF_POINT);
            sprintf_s(Name, "%s_stretch[%u,%u] %08lX", Prefix, RectIndex, DestIndex, hr);
            NcPrintSurfaceHash(Name, Big);
        }
        printf("%s_stretch_nodest[%u] = %08lX\n", Prefix, RectIndex,
               Device->StretchRect(Target, &Rects[RectIndex], Big, nullptr, D3DTEXF_NONE));
        printf("%s_stretch_nosrc[%u] = %08lX\n", Prefix, RectIndex,
               Device->StretchRect(Big, nullptr, Target, &Rects[RectIndex], D3DTEXF_NONE));
        if (Dxt != nullptr && DxtMemory != nullptr)
        {
            HRESULT Update = Device->UpdateSurface(DxtMemory, &Rects[RectIndex], Dxt,
                                                   &Points[RectIndex % _countof(Points)]);
            HRESULT Fill = Device->ColorFill(Dxt, &Rects[RectIndex], 0);
            HRESULT Stretch = Device->StretchRect(Dxt, &Rects[RectIndex], Dxt, &Rects[RectIndex], D3DTEXF_NONE);

            printf("%s_dxt[%u] = update %08lX fill %08lX stretch %08lX\n", Prefix, RectIndex, Update, Fill, Stretch);
        }
    }

    for (DWORD Filter : Filters)
    {
        NcFillPattern(Target, 9);
        hr = Device->StretchRect(Target, nullptr, Big, nullptr, static_cast<D3DTEXTUREFILTERTYPE>(Filter));
        sprintf_s(Name, "%s_filter[%lu] %08lX", Prefix, Filter, hr);
        NcPrintSurfaceHash(Name, Big);
        printf("%s_filter_shrink[%lu] = %08lX\n", Prefix, Filter,
               Device->StretchRect(Big, nullptr, Target, nullptr, static_cast<D3DTEXTUREFILTERTYPE>(Filter)));
        printf("%s_filter_same[%lu] = %08lX\n", Prefix, Filter,
               Device->StretchRect(Target, nullptr, Target, nullptr, static_cast<D3DTEXTUREFILTERTYPE>(Filter)));
    }

    if (Multi != nullptr)
    {
        printf("%s_ms_fill = %08lX\n", Prefix, Device->ColorFill(Multi, nullptr, 0xFF203040));
        hr = Device->StretchRect(Multi, nullptr, Target, nullptr, D3DTEXF_NONE);
        sprintf_s(Name, "%s_ms_resolve %08lX", Prefix, hr);
        NcPrintSurfaceHash(Name, Target);
        hr = Device->StretchRect(Multi, &Rects[0], Target, &Rects[0], D3DTEXF_NONE);
        printf("%s_ms_resolve_rect = %08lX\n", Prefix, hr);
        hr = Device->StretchRect(Multi, &Rects[0], Big, &Rects[1], D3DTEXF_NONE);
        printf("%s_ms_resolve_stretch = %08lX\n", Prefix, hr);
        hr = Device->StretchRect(Target, nullptr, Multi, nullptr, D3DTEXF_NONE);
        printf("%s_ms_into = %08lX\n", Prefix, hr);
        printf("%s_ms_readback = %08lX\n", Prefix, Device->GetRenderTargetData(Multi, Sysmem));
    }

    if (Depth != nullptr && DepthOther != nullptr)
    {
        printf("%s_depth_copy = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, DepthOther, nullptr, D3DTEXF_NONE));
        printf("%s_depth_rect = %08lX\n", Prefix,
               Device->StretchRect(Depth, &Rects[0], DepthOther, &Rects[0], D3DTEXF_NONE));
        printf("%s_depth_filter = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, DepthOther, nullptr, D3DTEXF_LINEAR));
        Device->BeginScene();
        printf("%s_depth_scene = %08lX\n", Prefix,
               Device->StretchRect(Depth, nullptr, DepthOther, nullptr, D3DTEXF_NONE));
        printf("%s_scene_color = %08lX\n", Prefix, Device->StretchRect(Target, nullptr, Big, nullptr, D3DTEXF_NONE));
        printf("%s_scene_fill = %08lX\n", Prefix, Device->ColorFill(Target, nullptr, 0));
        printf("%s_scene_update = %08lX\n", Prefix, Device->UpdateSurface(Sysmem, nullptr, Target, nullptr));
        printf("%s_scene_readback = %08lX\n", Prefix, Device->GetRenderTargetData(Target, Sysmem));
        Device->EndScene();
    }

    {
        D3DLOCKED_RECT Locked = {};

        Sysmem->LockRect(&Locked, nullptr, 0);
        printf("%s_locked_src = %08lX\n", Prefix, Device->UpdateSurface(Sysmem, nullptr, Target, nullptr));
        printf("%s_locked_readback = %08lX\n", Prefix, Device->GetRenderTargetData(Target, Sysmem));
        Sysmem->UnlockRect();
        Target->LockRect(&Locked, nullptr, 0);
        printf("%s_locked_dst = %08lX\n", Prefix, Device->UpdateSurface(Sysmem, nullptr, Target, nullptr));
        printf("%s_locked_fill = %08lX\n", Prefix, Device->ColorFill(Target, nullptr, 0));
        printf("%s_locked_stretch = %08lX\n", Prefix,
               Device->StretchRect(Big, nullptr, Target, nullptr, D3DTEXF_NONE));
        Target->UnlockRect();
    }

    for (IDirect3DSurface9* Surface : { Sysmem, Target, Big, BigMemory, Multi, DxtMemory, Dxt, Depth, DepthOther })
    {
        if (Surface != nullptr)
            Surface->Release();
    }
}

/* Device contexts on surfaces of every format and pool, and what GDI drawing leaves in them */
static
VOID
NcCheckSurfaceDcs(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const D3DPOOL Pools[] = { D3DPOOL_SYSTEMMEM, D3DPOOL_DEFAULT, D3DPOOL_SCRATCH };
    IDirect3DSurface9* Surface;
    IDirect3DTexture9* Texture;
    char Name[128];
    HDC Dc;
    HDC Second;
    HRESULT hr;

    for (D3DFORMAT Format : NcFormats)
    {
        for (D3DPOOL Pool : Pools)
        {
            Surface = nullptr;
            Device->CreateOffscreenPlainSurface(16, 8, Format, Pool, &Surface, nullptr);
            if (Surface == nullptr)
                continue;
            Dc = NULL;
            hr = Surface->GetDC(&Dc);
            printf("%s_dc[%u,%u] = %08lX %s\n", Prefix, Format, Pool, hr, Dc != NULL ? "set" : "null");
            if (Dc != NULL)
            {
                RECT Area = { 2, 1, 6, 4 };
                HBRUSH Brush = CreateSolidBrush(RGB(0x10, 0x80, 0xF0));
                D3DLOCKED_RECT Locked = {};
                HDC Screen = GetDC(nullptr);
                HRESULT First;

                FillRect(Dc, &Area, Brush);
                DeleteObject(Brush);
                SetPixel(Dc, 0, 0, RGB(0xFF, 0x00, 0x7F));
                printf("%s_dc_pixel[%u,%u] = %06lX %06lX\n", Prefix, Format, Pool, GetPixel(Dc, 0, 0),
                       GetPixel(Dc, 3, 2));
                Second = NULL;
                printf("%s_dc_again[%u,%u] = %08lX\n", Prefix, Format, Pool, Surface->GetDC(&Second));
                printf("%s_dc_lock[%u,%u] = %08lX\n", Prefix, Format, Pool, Surface->LockRect(&Locked, nullptr, 0));
                printf("%s_dc_wrong[%u,%u] = %08lX\n", Prefix, Format, Pool, Surface->ReleaseDC(Screen));
                ReleaseDC(nullptr, Screen);
                First = Surface->ReleaseDC(Dc);
                printf("%s_dc_release[%u,%u] = %08lX again %08lX\n", Prefix, Format, Pool, First,
                       Surface->ReleaseDC(Dc));
                if (Pool != D3DPOOL_DEFAULT)
                {
                    sprintf_s(Name, "%s_dc_bytes[%u,%u]", Prefix, Format, Pool);
                    NcPrintSurfaceHash(Name, Surface);
                }
            }
            Surface->Release();
        }
    }

    for (UINT Index = 0; Index < _countof(NcCopyKinds); Index++)
    {
        Surface = NcMakeCopySurface(Device, &NcCopyKinds[Index]);
        if (Surface == nullptr)
            continue;
        Dc = NULL;
        hr = Surface->GetDC(&Dc);
        printf("%s_dc_kind[%s] = %08lX %s\n", Prefix, NcCopyKinds[Index].Name, hr, Dc != NULL ? "set" : "null");
        if (Dc != NULL)
        {
            SetPixel(Dc, 1, 1, RGB(0x20, 0x40, 0x60));
            printf("%s_dc_kind_release[%s] = %08lX\n", Prefix, NcCopyKinds[Index].Name, Surface->ReleaseDC(Dc));
        }
        printf("%s_dc_kind_unheld[%s] = %08lX\n", Prefix, NcCopyKinds[Index].Name, Surface->ReleaseDC(NULL));
        Surface->Release();
    }

    Texture = nullptr;
    Device->CreateTexture(16, 8, 2, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &Texture, nullptr);
    if (Texture != nullptr)
    {
        IDirect3DSurface9* Level = nullptr;
        D3DLOCKED_RECT Locked = {};

        Texture->GetSurfaceLevel(1, &Level);
        Dc = NULL;
        printf("%s_dc_level = %08lX\n", Prefix, Level->GetDC(&Dc));
        printf("%s_dc_level_lock = %08lX\n", Prefix, Texture->LockRect(1, &Locked, nullptr, 0));
        printf("%s_dc_level_lock0 = %08lX\n", Prefix, Texture->LockRect(0, &Locked, nullptr, 0));
        if (Dc != NULL)
            printf("%s_dc_level_release = %08lX\n", Prefix, Level->ReleaseDC(Dc));
        Texture->UnlockRect(0);
        Texture->LockRect(0, &Locked, nullptr, 0);
        Dc = NULL;
        printf("%s_dc_level_locked = %08lX\n", Prefix, Level->GetDC(&Dc));
        if (Dc != NULL)
            Level->ReleaseDC(Dc);
        Texture->UnlockRect(0);
        Level->Release();
        Texture->Release();
    }
}

/* Texture uploads: matching rules, managed and dirty regions, and the bytes that arrive */
static
VOID
NcCheckUpdateTextures(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const struct
    {
        PCSTR Name;
        UINT Width;
        UINT Height;
        UINT Levels;
        DWORD Usage;
        D3DFORMAT Format;
        D3DPOOL Pool;
    } Textures[] =
    {
        { "sys3", 16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM },
        { "sys1", 16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM },
        { "sysbig", 32, 16, 4, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM },
        { "sysx8", 16, 8, 3, 0, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM },
        { "dyn3", 16, 8, 3, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "def3", 16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "def1", 16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "def5", 16, 8, 5, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "defbig", 32, 16, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "defsmall", 8, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "defx8", 16, 8, 3, 0, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT },
        { "target", 16, 8, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "autogen", 16, 8, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT },
        { "managed", 16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED },
        { "scratch", 16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH },
        { "sysdxt", 16, 8, 3, 0, D3DFMT_DXT1, D3DPOOL_SYSTEMMEM },
        { "defdxt", 16, 8, 3, 0, D3DFMT_DXT1, D3DPOOL_DEFAULT },
    };
    IDirect3DBaseTexture9* Made[_countof(Textures) + 4] = {};
    IDirect3DCubeTexture9* Cube = nullptr;
    IDirect3DVolumeTexture9* Volume = nullptr;
    IDirect3DTexture9* Texture;
    IDirect3DTexture9* Memory = nullptr;
    IDirect3DTexture9* Dynamic = nullptr;
    D3DLOCKED_RECT Locked = {};
    RECT Dirty = { 4, 2, 8, 6 };
    char Name[128];
    HRESULT hr;

    for (UINT Index = 0; Index < _countof(Textures); Index++)
    {
        Texture = nullptr;
        hr = Device->CreateTexture(Textures[Index].Width, Textures[Index].Height, Textures[Index].Levels,
                                   Textures[Index].Usage, Textures[Index].Format, Textures[Index].Pool, &Texture,
                                   nullptr);
        printf("%s_tex[%s] = %08lX\n", Prefix, Textures[Index].Name, hr);
        Made[Index] = Texture;
    }
    Device->CreateCubeTexture(8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Cube, nullptr);
    Made[_countof(Textures)] = Cube;
    Cube = nullptr;
    Device->CreateCubeTexture(8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Cube, nullptr);
    Made[_countof(Textures) + 1] = Cube;
    Device->CreateVolumeTexture(8, 4, 2, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Volume, nullptr);
    Made[_countof(Textures) + 2] = Volume;
    Volume = nullptr;
    Device->CreateVolumeTexture(8, 4, 2, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Volume, nullptr);
    Made[_countof(Textures) + 3] = Volume;

    for (UINT Source = 0; Source < _countof(Made); Source++)
    {
        if (Made[Source] == nullptr)
            continue;
        for (UINT Target = 0; Target < _countof(Made); Target++)
        {
            /* Textures of different types are left out: the system copies without checking and crashes */
            if (Made[Target] == nullptr || Made[Target]->GetType() != Made[Source]->GetType())
                continue;
            printf("%s_update_clean[%u,%u] = %08lX\n", Prefix, Source, Target,
                   Device->UpdateTexture(Made[Source], Made[Target]));
            switch (Made[Source]->GetType())
            {
            case D3DRTYPE_CUBETEXTURE:
                static_cast<IDirect3DCubeTexture9*>(Made[Source])->AddDirtyRect(D3DCUBEMAP_FACE_POSITIVE_Y, nullptr);
                break;
            case D3DRTYPE_VOLUMETEXTURE:
                static_cast<IDirect3DVolumeTexture9*>(Made[Source])->AddDirtyBox(nullptr);
                break;
            default:
                static_cast<IDirect3DTexture9*>(Made[Source])->AddDirtyRect(nullptr);
                break;
            }
            printf("%s_update[%u,%u] = %08lX\n", Prefix, Source, Target,
                   Device->UpdateTexture(Made[Source], Made[Target]));
        }
    }
    for (IDirect3DBaseTexture9* Item : Made)
    {
        if (Item != nullptr)
            Item->Release();
    }

    /* Bytes through a dynamic texture the test can read back */
    Device->CreateTexture(16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Memory, nullptr);
    Device->CreateTexture(16, 8, 3, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Dynamic, nullptr);
    if (Memory == nullptr || Dynamic == nullptr)
    {
        printf("%s_bytes = missing textures\n", Prefix);
        if (Memory != nullptr)
            Memory->Release();
        if (Dynamic != nullptr)
            Dynamic->Release();
        return;
    }
    for (UINT Round = 0; Round < 6; Round++)
    {
        DWORD Flags = Round == 2 || Round == 3 ? D3DLOCK_NO_DIRTY_UPDATE : 0;
        const RECT* Area = Round == 1 || Round == 3 ? &Dirty : nullptr;

        for (UINT Index = 0; Index < 3; Index++)
        {
            UINT Rows = Index == 0 && Area != nullptr ? 4 : (8u >> Index);

            if (FAILED(Memory->LockRect(Index, &Locked, Index == 0 ? Area : nullptr, Flags)))
                continue;
            for (UINT Row = 0; Row < Rows; Row++)
            {
                memset(static_cast<BYTE*>(Locked.pBits) + Row * Locked.Pitch, 0x10 * (Round + 1) + Index,
                       Index == 0 && Area != nullptr ? 16 : (64u >> Index));
            }
            Memory->UnlockRect(Index);
        }
        if (Round == 4)
            printf("%s_bytes_dirty[%u] = %08lX\n", Prefix, Round, Memory->AddDirtyRect(&Dirty));
        if (Round == 5)
            printf("%s_bytes_dirty[%u] = %08lX\n", Prefix, Round, Memory->AddDirtyRect(nullptr));
        hr = Device->UpdateTexture(Memory, Dynamic);
        printf("%s_bytes_update[%u] = %08lX\n", Prefix, Round, hr);
        for (UINT Index = 0; Index < 3; Index++)
        {
            IDirect3DSurface9* Level = nullptr;

            Dynamic->GetSurfaceLevel(Index, &Level);
            sprintf_s(Name, "%s_bytes[%u,%u]", Prefix, Round, Index);
            NcPrintSurfaceHash(Name, Level);
            Level->Release();
        }
        printf("%s_bytes_again[%u] = %08lX\n", Prefix, Round, Device->UpdateTexture(Memory, Dynamic));
    }
    Memory->Release();
    Dynamic->Release();
}

/* Copies of what the screen shows into surfaces of the screen's size, which only memory A8R8G8B8 surfaces take */
static
VOID
NcCheckFrontBuffer(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const struct
    {
        PCSTR Name;
        D3DFORMAT Format;
        D3DPOOL Pool;
        UINT Grow;
    } Cases[] =
    {
        { "sysmem", D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0 },
        { "sysmem_larger", D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 8 },
        { "scratch", D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, 0 },
        { "default", D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 0 },
        { "x8", D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, 0 },
    };
    IDirect3DSwapChain9* Chain = nullptr;
    D3DDISPLAYMODE Mode = {};

    Device->GetDisplayMode(0, &Mode);
    Device->GetSwapChain(0, &Chain);
    for (const auto& Case : Cases)
    {
        IDirect3DSurface9* Surface = nullptr;
        HRESULT hr = Device->CreateOffscreenPlainSurface(Mode.Width + Case.Grow, Mode.Height, Case.Format, Case.Pool,
                                                         &Surface, nullptr);

        if (Surface == nullptr)
        {
            printf("%s_frontbuffer[%s] = %08lX null\n", Prefix, Case.Name, hr);
            continue;
        }
        printf("%s_frontbuffer[%s] = %08lX chain1 %08lX\n", Prefix, Case.Name, Device->GetFrontBufferData(0, Surface),
               Device->GetFrontBufferData(1, Surface));
        if (Chain != nullptr)
            printf("%s_frontbuffer_chain[%s] = %08lX\n", Prefix, Case.Name, Chain->GetFrontBufferData(Surface));
        Surface->Release();
    }
    if (Chain != nullptr)
        Chain->Release();
}

/* The copy calls on one device */
static
VOID
NcCheckCopiesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    D3DCAPS9 Caps = {};
    char Name[64];

    Device->GetDeviceCaps(&Caps);
    printf("%s_caps = stretch %08lX devcaps2 %08lX\n", Prefix, Caps.StretchRectFilterCaps, Caps.DevCaps2);
    sprintf_s(Name, "%s_pair", Prefix);
    NcCheckCopyPairs(Name, Device);
    NcCheckDepthStretches(Prefix, Device);
    NcCheckFrontBuffer(Prefix, Device);
    NcCheckCopyRects(Prefix, Device);
    NcCheckSurfaceDcs(Prefix, Device);
    NcCheckUpdateTextures(Prefix, Device);
}

/* Copies between surfaces and textures on a windowed extended device and a device of the plain object */
static
VOID
NcCheckCopies(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters,
                                   nullptr, &DeviceEx);
    printf("copies_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckCopiesOn("copies_ex", DeviceEx);
        printf("copies_ex_release = %lu\n", DeviceEx->Release());
    }

    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Parameters, &Device);
    printf("copies_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckCopiesOn("copies_plain", Device);
        printf("copies_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

static
VOID
NcPrintGamma(
    _In_z_ PCSTR Name,
    _In_ const D3DGAMMARAMP* Ramp)
{
    DWORD Hash = 2166136261UL;

    for (UINT Index = 0; Index < sizeof(*Ramp); Index++)
        Hash = (Hash ^ reinterpret_cast<const BYTE*>(Ramp)[Index]) * 16777619UL;
    printf("%s = red %04X %04X %04X green %04X blue %04X hash %08lX\n", Name, Ramp->red[0], Ramp->red[1],
           Ramp->red[255], Ramp->green[128], Ramp->blue[255], Hash);
}

/* Gamma ramps a windowed chain keeps; the display's own gamma never changes for them */
static
VOID
NcCheckGammaOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    D3DPRESENT_PARAMETERS Again = *Parameters;
    D3DGAMMARAMP Ramp;
    D3DGAMMARAMP Other;
    char Name[64];

    memset(&Ramp, 0xCC, sizeof(Ramp));
    Device->GetGammaRamp(0, &Ramp);
    sprintf_s(Name, "%s_initial", Prefix);
    NcPrintGamma(Name, &Ramp);

    for (UINT Index = 0; Index < 256; Index++)
    {
        Ramp.red[Index] = static_cast<WORD>(Index * 257);
        Ramp.green[Index] = static_cast<WORD>(65535 - Index * 257);
        Ramp.blue[Index] = static_cast<WORD>(Index * 100);
        Other.red[Index] = static_cast<WORD>(Index * 3);
        Other.green[Index] = static_cast<WORD>(Index * 5);
        Other.blue[Index] = static_cast<WORD>(Index * 7);
    }
    Device->SetGammaRamp(0, D3DSGR_NO_CALIBRATION, &Ramp);
    memset(&Ramp, 0, sizeof(Ramp));
    Device->GetGammaRamp(0, &Ramp);
    sprintf_s(Name, "%s_set", Prefix);
    NcPrintGamma(Name, &Ramp);

    Device->SetGammaRamp(0, D3DSGR_CALIBRATE, &Other);
    Device->GetGammaRamp(0, &Ramp);
    sprintf_s(Name, "%s_calibrate", Prefix);
    NcPrintGamma(Name, &Ramp);

    Device->SetGammaRamp(1, 0, &Ramp);
    Device->SetGammaRamp(0, 0, nullptr);
    Device->SetGammaRamp(0, 0x80, &Other);
    Device->GetGammaRamp(0, &Ramp);
    sprintf_s(Name, "%s_flags", Prefix);
    NcPrintGamma(Name, &Ramp);

    memset(&Ramp, 0xCC, sizeof(Ramp));
    Device->GetGammaRamp(1, &Ramp);
    sprintf_s(Name, "%s_chain1", Prefix);
    NcPrintGamma(Name, &Ramp);

    printf("%s_reset = %08lX\n", Prefix, Device->Reset(&Again));
    Device->GetGammaRamp(0, &Ramp);
    sprintf_s(Name, "%s_after_reset", Prefix);
    NcPrintGamma(Name, &Ramp);
}

static
VOID
NcCheckGamma(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DPRESENT_PARAMETERS Made;
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Made = Parameters;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, nullptr,
                                   &DeviceEx);
    printf("gamma_create_ex = %08lX\n", hr);
    if (DeviceEx != nullptr)
    {
        NcCheckGammaOn("gamma_ex", DeviceEx, &Parameters);
        printf("gamma_ex_release = %lu\n", DeviceEx->Release());
    }
    Made = Parameters;
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, &Device);
    printf("gamma_create_plain = %08lX\n", hr);
    if (Device != nullptr)
    {
        NcCheckGammaOn("gamma_plain", Device, &Parameters);
        printf("gamma_plain_release = %lu\n", Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* The resources the sharing checks make and open, each in one fixed description */
enum NcShareKind
{
    NcShareTarget,
    NcShareTargetLockable,
    NcShareTargetSmall,
    NcShareTargetFormat,
    NcShareDepth,
    NcSharePlain,
    NcShareTexture,
    NcShareTextureLevels,
    NcShareTextureTarget,
    NcShareCube,
    NcShareVolume,
    NcShareVertices,
    NcShareVerticesLong,
    NcShareIndices,
    NcShareCount,
};

static const PCSTR NcShareNames[] =
{
    "target", "target_lockable", "target_small", "target_format", "depth", "plain", "texture", "texture_levels",
    "texture_target", "cube", "volume", "vertices", "vertices_long", "indices",
};

static
HRESULT
NcMakeShared(
    _In_ IDirect3DDevice9* Device,
    _In_ NcShareKind Kind,
    _Inout_opt_ HANDLE* Shared,
    _Outptr_result_maybenull_ IDirect3DResource9** Made)
{
    IDirect3DSurface9* Surface = nullptr;
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DCubeTexture9* Cube = nullptr;
    IDirect3DVolumeTexture9* Volume = nullptr;
    IDirect3DVertexBuffer9* Vertices = nullptr;
    IDirect3DIndexBuffer9* Indices = nullptr;
    HRESULT hr = E_FAIL;

    *Made = nullptr;
    switch (Kind)
    {
    case NcShareTarget:
    case NcShareTargetLockable:
        hr = Device->CreateRenderTarget(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                        Kind == NcShareTargetLockable, &Surface, Shared);
        break;
    case NcShareTargetSmall:
        hr = Device->CreateRenderTarget(8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, Shared);
        break;
    case NcShareTargetFormat:
        hr = Device->CreateRenderTarget(16, 8, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, Shared);
        break;
    case NcShareDepth:
        hr = Device->CreateDepthStencilSurface(16, 8, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, Shared);
        break;
    case NcSharePlain:
        hr = Device->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, Shared);
        break;
    case NcShareTexture:
        hr = Device->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, Shared);
        break;
    case NcShareTextureLevels:
        hr = Device->CreateTexture(16, 8, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, Shared);
        break;
    case NcShareTextureTarget:
        hr = Device->CreateTexture(16, 8, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture,
                                   Shared);
        break;
    case NcShareCube:
        hr = Device->CreateCubeTexture(8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Cube, Shared);
        break;
    case NcShareVolume:
        hr = Device->CreateVolumeTexture(8, 8, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Volume, Shared);
        break;
    case NcShareVertices:
    case NcShareVerticesLong:
        hr = Device->CreateVertexBuffer(Kind == NcShareVertices ? 64 : 128, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                        &Vertices, Shared);
        break;
    case NcShareIndices:
        hr = Device->CreateIndexBuffer(64, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &Indices, Shared);
        break;
    default:
        break;
    }
    if (Surface != nullptr)
        *Made = Surface;
    else if (Texture != nullptr)
        *Made = Texture;
    else if (Cube != nullptr)
        *Made = Cube;
    else if (Volume != nullptr)
        *Made = Volume;
    else if (Vertices != nullptr)
        *Made = Vertices;
    else if (Indices != nullptr)
        *Made = Indices;
    return hr;
}

/* The result of a creation or an open, with what the resource reports about itself */
static
VOID
NcPrintShared(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result,
    _In_opt_ IDirect3DResource9* Resource,
    _In_opt_ const HANDLE* Shared)
{
    printf("%s = %08lX handle %s", Name, Result,
           Shared == nullptr ? "none" : (*Shared != NULL ? "set" : "null"));
    if (Resource == nullptr)
    {
        printf(" null\n");
        return;
    }
    switch (Resource->GetType())
    {
    case D3DRTYPE_SURFACE:
    {
        D3DSURFACE_DESC Desc = {};

        static_cast<IDirect3DSurface9*>(Resource)->GetDesc(&Desc);
        printf(" surface fmt %u usage %08lX pool %u size %ux%u", Desc.Format, Desc.Usage, Desc.Pool, Desc.Width,
               Desc.Height);
        break;
    }
    case D3DRTYPE_TEXTURE:
    {
        IDirect3DTexture9* Texture = static_cast<IDirect3DTexture9*>(Resource);
        D3DSURFACE_DESC Desc = {};

        Texture->GetLevelDesc(0, &Desc);
        printf(" texture levels %lu fmt %u usage %08lX pool %u size %ux%u", Texture->GetLevelCount(), Desc.Format,
               Desc.Usage, Desc.Pool, Desc.Width, Desc.Height);
        break;
    }
    case D3DRTYPE_CUBETEXTURE:
    {
        IDirect3DCubeTexture9* Cube = static_cast<IDirect3DCubeTexture9*>(Resource);
        D3DSURFACE_DESC Desc = {};

        Cube->GetLevelDesc(0, &Desc);
        printf(" cube levels %lu fmt %u size %u", Cube->GetLevelCount(), Desc.Format, Desc.Width);
        break;
    }
    case D3DRTYPE_VOLUMETEXTURE:
    {
        IDirect3DVolumeTexture9* Volume = static_cast<IDirect3DVolumeTexture9*>(Resource);
        D3DVOLUME_DESC Desc = {};

        Volume->GetLevelDesc(0, &Desc);
        printf(" volume levels %lu fmt %u size %ux%ux%u", Volume->GetLevelCount(), Desc.Format, Desc.Width,
               Desc.Height, Desc.Depth);
        break;
    }
    case D3DRTYPE_VERTEXBUFFER:
    {
        D3DVERTEXBUFFER_DESC Desc = {};

        static_cast<IDirect3DVertexBuffer9*>(Resource)->GetDesc(&Desc);
        printf(" vertices size %u usage %08lX pool %u fvf %08lX", Desc.Size, Desc.Usage, Desc.Pool, Desc.FVF);
        break;
    }
    case D3DRTYPE_INDEXBUFFER:
    {
        D3DINDEXBUFFER_DESC Desc = {};

        static_cast<IDirect3DIndexBuffer9*>(Resource)->GetDesc(&Desc);
        printf(" indices size %u usage %08lX pool %u fmt %u", Desc.Size, Desc.Usage, Desc.Pool, Desc.Format);
        break;
    }
    default:
        printf(" type %u", Resource->GetType());
        break;
    }
    printf("\n");
}

/* Pixels a render target holds, read back through a system memory surface of the same device */
static
VOID
NcPrintSharedPixels(
    _In_z_ PCSTR Name,
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Target)
{
    IDirect3DSurface9* Copy = nullptr;
    D3DLOCKED_RECT Locked = {};
    HRESULT hr;

    hr = Device->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Copy, nullptr);
    if (FAILED(hr))
    {
        printf("%s = %08lX\n", Name, hr);
        return;
    }
    hr = Device->GetRenderTargetData(Target, Copy);
    if (SUCCEEDED(hr))
        hr = Copy->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    if (SUCCEEDED(hr))
    {
        printf("%s = %08lX first %08lX last %08lX\n", Name, hr, *static_cast<const DWORD*>(Locked.pBits),
               *reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) + Locked.Pitch * 7 + 4 * 15));
        Copy->UnlockRect();
    }
    else
    {
        printf("%s = %08lX\n", Name, hr);
    }
    Copy->Release();
}

/* Shared resources made on one extended device and opened by handle on another */
static
VOID
NcCheckSharedOn(
    _In_ IDirect3DDevice9Ex* First,
    _In_ IDirect3DDevice9Ex* Second,
    _In_ IDirect3DDevice9* Plain)
{
    IDirect3DResource9* Made[NcShareCount] = {};
    HANDLE Handles[NcShareCount] = {};
    IDirect3DResource9* Opened;
    IDirect3DSurface9* Surface;
    IDirect3DTexture9* Texture;
    HANDLE Handle;
    char Name[96];
    HRESULT hr;

    for (UINT Kind = 0; Kind < NcShareCount; Kind++)
    {
        hr = NcMakeShared(First, static_cast<NcShareKind>(Kind), &Handles[Kind], &Made[Kind]);
        sprintf_s(Name, "shared_make[%s]", NcShareNames[Kind]);
        NcPrintShared(Name, hr, Made[Kind], &Handles[Kind]);
    }

    for (UINT Source = 0; Source < NcShareCount; Source++)
    {
        if (Handles[Source] == NULL)
            continue;
        for (UINT Kind = 0; Kind < NcShareCount; Kind++)
        {
            Handle = Handles[Source];
            hr = NcMakeShared(Second, static_cast<NcShareKind>(Kind), &Handle, &Opened);
            sprintf_s(Name, "shared_open[%s,%s]", NcShareNames[Source], NcShareNames[Kind]);
            NcPrintShared(Name, hr, Opened, nullptr);
            printf("%s_same = %d\n", Name, Handle == Handles[Source]);
            if (Opened != nullptr)
                printf("%s_release = %lu\n", Name, Opened->Release());
        }
        Handle = Handles[Source];
        hr = NcMakeShared(First, static_cast<NcShareKind>(Source), &Handle, &Opened);
        sprintf_s(Name, "shared_open_self[%s]", NcShareNames[Source]);
        NcPrintShared(Name, hr, Opened, nullptr);
        if (Opened != nullptr)
            printf("%s_release = %lu\n", Name, Opened->Release());
        Handle = Handles[Source];
        hr = NcMakeShared(Plain, static_cast<NcShareKind>(Source), &Handle, &Opened);
        sprintf_s(Name, "shared_open_plain[%s]", NcShareNames[Source]);
        NcPrintShared(Name, hr, Opened, nullptr);
        if (Opened != nullptr)
            printf("%s_release = %lu\n", Name, Opened->Release());
    }

    /* Handles that name nothing, and creations sharing cannot apply to */
    Handle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(0x40000002));
    hr = NcMakeShared(Second, NcShareTarget, &Handle, &Opened);
    NcPrintShared("shared_open_bogus", hr, Opened, &Handle);
    if (Opened != nullptr)
        Opened->Release();

    Handle = NULL;
    Surface = nullptr;
    hr = First->CreateRenderTarget(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &Surface, &Handle);
    NcPrintShared("shared_make_multisample", hr, Surface, &Handle);
    if (Surface != nullptr)
        Surface->Release();
    Handle = NULL;
    Surface = nullptr;
    hr = First->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, &Handle);
    NcPrintShared("shared_make_plain_sysmem", hr, Surface, &Handle);
    if (Surface != nullptr)
        Surface->Release();
    Handle = NULL;
    Surface = nullptr;
    hr = First->CreateOffscreenPlainSurface(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &Surface, &Handle);
    NcPrintShared("shared_make_plain_scratch", hr, Surface, &Handle);
    if (Surface != nullptr)
        Surface->Release();
    Handle = NULL;
    Texture = nullptr;
    hr = First->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &Texture, &Handle);
    NcPrintShared("shared_make_texture_managed", hr, Texture, &Handle);
    if (Texture != nullptr)
        Texture->Release();
    Handle = NULL;
    Texture = nullptr;
    hr = First->CreateTexture(16, 8, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, &Handle);
    NcPrintShared("shared_make_texture_dynamic", hr, Texture, &Handle);
    if (Texture != nullptr)
        Texture->Release();
    Handle = NULL;
    Texture = nullptr;
    hr = First->CreateTexture(16, 8, 0, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8,
                              D3DPOOL_DEFAULT, &Texture, &Handle);
    NcPrintShared("shared_make_texture_autogen", hr, Texture, &Handle);
    if (Texture != nullptr)
        Texture->Release();
    {
        static DWORD Pixels[16 * 8];

        for (UINT Index = 0; Index < _countof(Pixels); Index++)
            Pixels[Index] = 0xFF000000 | (Index * 0x10203);
        Handle = Pixels;
        Texture = nullptr;
        hr = First->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Texture, &Handle);
        NcPrintShared("shared_make_texture_memory", hr, Texture, &Handle);
        if (Texture != nullptr)
        {
            D3DLOCKED_RECT Locked = {};

            hr = Texture->LockRect(0, &Locked, nullptr, D3DLOCK_READONLY);
            printf("shared_make_texture_memory_lock = %08lX same %d pitch %d first %08lX\n", hr,
                   Locked.pBits == Pixels, Locked.Pitch,
                   SUCCEEDED(hr) ? *static_cast<const DWORD*>(Locked.pBits) : 0);
            if (SUCCEEDED(hr))
                Texture->UnlockRect(0);
            Texture->Release();
        }
        Handle = Pixels;
        Texture = nullptr;
        hr = First->CreateTexture(16, 8, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Texture, &Handle);
        NcPrintShared("shared_make_texture_memory_levels", hr, Texture, &Handle);
        if (Texture != nullptr)
            Texture->Release();
        Handle = NULL;
        Texture = nullptr;
        hr = First->CreateTexture(16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Texture, &Handle);
        NcPrintShared("shared_make_texture_memory_null", hr, Texture, &Handle);
        if (Texture != nullptr)
            Texture->Release();
    }
    {
        static const DWORD Usages[] =
        {
            D3DUSAGE_RESTRICTED_CONTENT, D3DUSAGE_RESTRICT_SHARED_RESOURCE, D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER,
            D3DUSAGE_NONSECURE,
        };

        for (DWORD Usage : Usages)
        {
            Handle = NULL;
            Surface = nullptr;
            hr = First->CreateRenderTargetEx(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, &Handle,
                                             Usage);
            sprintf_s(Name, "shared_make_target_ex[%08lX]", Usage);
            NcPrintShared(Name, hr, Surface, &Handle);
            if (Surface != nullptr)
            {
                IDirect3DSurface9* Other = nullptr;

                hr = Second->CreateRenderTargetEx(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Other,
                                                  &Handle, Usage);
                sprintf_s(Name, "shared_open_target_ex[%08lX]", Usage);
                NcPrintShared(Name, hr, Other, nullptr);
                if (Other != nullptr)
                    Other->Release();
                Other = nullptr;
                hr = Second->CreateRenderTargetEx(16, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Other,
                                                  &Handle, 0);
                sprintf_s(Name, "shared_open_target_ex_plain[%08lX]", Usage);
                NcPrintShared(Name, hr, Other, nullptr);
                if (Other != nullptr)
                    Other->Release();
                Surface->Release();
            }
            Handle = NULL;
            Surface = nullptr;
            hr = First->CreateOffscreenPlainSurfaceEx(16, 8, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Surface, &Handle,
                                                      Usage);
            sprintf_s(Name, "shared_make_plain_ex[%08lX]", Usage);
            NcPrintShared(Name, hr, Surface, &Handle);
            if (Surface != nullptr)
                Surface->Release();
            Handle = NULL;
            Texture = nullptr;
            hr = First->CreateTexture(16, 8, 1, Usage, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, &Handle);
            sprintf_s(Name, "shared_make_texture_usage[%08lX]", Usage);
            NcPrintShared(Name, hr, Texture, &Handle);
            if (Texture != nullptr)
                Texture->Release();
        }
    }

    /* Levels, usages and vertex formats the driver may or may not share */
    {
        static const struct
        {
            PCSTR Name;
            UINT Kind;
            UINT Levels;
            DWORD Usage;
            DWORD Fvf;
        } Cases[] =
        {
            { "texture_target_levels", 0, 2, D3DUSAGE_RENDERTARGET, 0 },
            { "texture_dynamic_levels", 0, 2, D3DUSAGE_DYNAMIC, 0 },
            { "cube_levels", 1, 2, 0, 0 },
            { "cube_target", 1, 1, D3DUSAGE_RENDERTARGET, 0 },
            { "cube_target_levels", 1, 2, D3DUSAGE_RENDERTARGET, 0 },
            { "cube_dynamic", 1, 1, D3DUSAGE_DYNAMIC, 0 },
            { "volume_levels", 2, 2, 0, 0 },
            { "volume_dynamic", 2, 1, D3DUSAGE_DYNAMIC, 0 },
            { "vertices_dynamic", 3, 0, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0 },
            { "vertices_fvf", 3, 0, D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE },
            { "vertices_dynamic_fvf", 3, 0, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE },
            { "vertices_plain", 3, 0, 0, 0 },
            { "vertices_dynamic_only", 3, 0, D3DUSAGE_DYNAMIC, 0 },
        };

        for (const auto& Case : Cases)
        {
            IDirect3DResource9* Resource = nullptr;
            IDirect3DTexture9* Planar = nullptr;
            IDirect3DCubeTexture9* Cube = nullptr;
            IDirect3DVolumeTexture9* Volume = nullptr;
            IDirect3DVertexBuffer9* Vertices = nullptr;

            Handle = NULL;
            if (Case.Kind == 0)
                hr = First->CreateTexture(16, 16, Case.Levels, Case.Usage, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Planar,
                                          &Handle);
            else if (Case.Kind == 1)
                hr = First->CreateCubeTexture(8, Case.Levels, Case.Usage, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Cube,
                                              &Handle);
            else if (Case.Kind == 2)
                hr = First->CreateVolumeTexture(8, 8, 4, Case.Levels, Case.Usage, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                                &Volume, &Handle);
            else
                hr = First->CreateVertexBuffer(96, Case.Usage, Case.Fvf, D3DPOOL_DEFAULT, &Vertices, &Handle);
            if (Planar != nullptr)
                Resource = Planar;
            else if (Cube != nullptr)
                Resource = Cube;
            else if (Volume != nullptr)
                Resource = Volume;
            else
                Resource = Vertices;
            sprintf_s(Name, "shared_make_case[%s]", Case.Name);
            NcPrintShared(Name, hr, Resource, &Handle);
            if (Resource != nullptr)
                Resource->Release();
        }
    }

    /* What one device writes the other reads */
    if (Made[NcShareTarget] != nullptr)
    {
        IDirect3DSurface9* Target = static_cast<IDirect3DSurface9*>(Made[NcShareTarget]);

        printf("shared_fill = %08lX\n", First->ColorFill(Target, nullptr, 0xFF336699));
        NcPrintSharedPixels("shared_pixels_first", First, Target);
        Handle = Handles[NcShareTarget];
        hr = NcMakeShared(Second, NcShareTarget, &Handle, &Opened);
        if (Opened != nullptr)
        {
            NcPrintSharedPixels("shared_pixels_second", Second, static_cast<IDirect3DSurface9*>(Opened));
            printf("shared_fill_second = %08lX\n",
                   Second->ColorFill(static_cast<IDirect3DSurface9*>(Opened), nullptr, 0xFF112233));
            NcPrintSharedPixels("shared_pixels_back", Second, static_cast<IDirect3DSurface9*>(Opened));
            NcPrintSharedPixels("shared_pixels_first_again", First, Target);
            printf("shared_opened_release = %lu\n", Opened->Release());
        }
    }

    for (UINT Kind = 0; Kind < NcShareCount; Kind++)
    {
        if (Made[Kind] != nullptr)
            printf("shared_release[%s] = %lu\n", NcShareNames[Kind], Made[Kind]->Release());
    }
}

static
VOID
NcCheckShared(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Nine = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    HWND Other = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DPRESENT_PARAMETERS Made;
    IDirect3DDevice9Ex* First = nullptr;
    IDirect3DDevice9Ex* Second = nullptr;
    IDirect3DDevice9* Plain = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Made = Parameters;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, nullptr,
                                   &First);
    printf("shared_create_first = %08lX\n", hr);
    Made = Parameters;
    Made.hDeviceWindow = Other;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Other, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, nullptr,
                                   &Second);
    printf("shared_create_second = %08lX\n", hr);
    Made = Parameters;
    Made.hDeviceWindow = Other;
    hr = Nine->CreateDevice(0, D3DDEVTYPE_HAL, Other, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, &Plain);
    printf("shared_create_plain = %08lX\n", hr);
    if (First != nullptr && Second != nullptr && Plain != nullptr)
        NcCheckSharedOn(First, Second, Plain);
    if (Plain != nullptr)
        printf("shared_plain_release = %lu\n", Plain->Release());
    if (Second != nullptr)
        printf("shared_second_release = %lu\n", Second->Release());
    if (First != nullptr)
        printf("shared_first_release = %lu\n", First->Release());
    DestroyWindow(Other);
    DestroyWindow(Window);
    Nine->Release();
}

/* Hashes the bits of a bitmap the cursor checks read back */
static
DWORD
NcHashBitmap(
    _In_opt_ HBITMAP Bitmap,
    _Out_ BITMAP* Info)
{
    std::vector<BYTE> Bits;
    DWORD Hash = 2166136261UL;

    ZeroMemory(Info, sizeof(*Info));
    if (Bitmap == NULL || GetObjectW(Bitmap, sizeof(*Info), Info) == 0)
        return 0;
    Bits.resize(static_cast<SIZE_T>(Info->bmWidthBytes) * Info->bmHeight);
    GetBitmapBits(Bitmap, static_cast<LONG>(Bits.size()), Bits.data());
    for (BYTE Value : Bits)
        Hash = (Hash ^ Value) * 16777619UL;
    return Hash;
}

/* Which cursor the thread shows: none, the one it had before, or a new one, and what that one looks like */
static
VOID
NcPrintCursor(
    _In_z_ PCSTR Name,
    _In_opt_ HCURSOR Before)
{
    HCURSOR Current = GetCursor();
    ICONINFO Info = {};
    BITMAP Mask;
    BITMAP Color;
    DWORD MaskHash;
    DWORD ColorHash;

    if (Current == NULL || Current == Before)
    {
        printf("%s = %s\n", Name, Current == NULL ? "none" : "before");
        return;
    }
    if (!GetIconInfo(Current, &Info))
    {
        printf("%s = new noinfo\n", Name);
        return;
    }
    MaskHash = NcHashBitmap(Info.hbmMask, &Mask);
    ColorHash = NcHashBitmap(Info.hbmColor, &Color);
    printf("%s = new icon %d hot %lu,%lu mask %ldx%ld bpp %u hash %08lX color %ldx%ld bpp %u hash %08lX\n", Name,
           Info.fIcon, Info.xHotspot, Info.yHotspot, Mask.bmWidth, Mask.bmHeight, Mask.bmBitsPixel, MaskHash,
           Color.bmWidth, Color.bmHeight, Color.bmBitsPixel, ColorHash);
    if (Info.hbmMask != NULL)
        DeleteObject(Info.hbmMask);
    if (Info.hbmColor != NULL)
        DeleteObject(Info.hbmColor);
}

/* Fills a 32 bit surface with a pattern whose alpha is opaque in most places */
static
VOID
NcFillCursor(
    _In_ IDirect3DSurface9* Surface,
    _In_ UINT Width,
    _In_ UINT Height)
{
    D3DLOCKED_RECT Locked = {};

    if (FAILED(Surface->LockRect(&Locked, nullptr, 0)))
        return;
    for (UINT Y = 0; Y < Height; Y++)
    {
        DWORD* Row = reinterpret_cast<DWORD*>(static_cast<BYTE*>(Locked.pBits) + Locked.Pitch * Y);

        for (UINT X = 0; X < Width; X++)
        {
            DWORD Alpha = ((X + Y) % 5) == 0 ? 0x40000000 : 0xFF000000;

            Row[X] = Alpha | ((X * 8) << 16) | ((Y * 8) << 8) | ((X ^ Y) == 3 ? 0 : 0x80);
        }
    }
    Surface->UnlockRect();
}

/* The device cursor on a windowed chain: properties, visibility and position; the real cursor goes back after */
static
VOID
NcCheckCursorOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DDevice9* Other,
    _In_ const D3DPRESENT_PARAMETERS* Parameters,
    _In_opt_ HCURSOR Before)
{
    static const struct
    {
        PCSTR Name;
        UINT Width;
        UINT Height;
        D3DFORMAT Format;
        D3DPOOL Pool;
        UINT HotX;
        UINT HotY;
    } Plains[] =
    {
        { "sysmem", 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 3, 5 },
        { "scratch", 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, 0, 0 },
        { "default", 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, 31, 31 },
        { "x8", 32, 32, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 0 },
        { "odd_width", 24, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 0 },
        { "odd_height", 32, 12, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 0 },
        { "hot_x", 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 32, 0 },
        { "hot_y", 32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 32 },
        { "small", 16, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 15, 7 },
        { "big", 64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 10, 20 },
        { "huge", 8192, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, 0, 0 },
    };
    D3DPRESENT_PARAMETERS Again = *Parameters;
    IDirect3DSurface9* Surface = nullptr;
    IDirect3DSurface9* Level = nullptr;
    IDirect3DTexture9* Texture = nullptr;
    POINT Point = {};
    char Name[96];
    HRESULT hr;

    printf("%s_show_early = %d\n", Prefix, Device->ShowCursor(TRUE));
    printf("%s_show_early_again = %d\n", Prefix, Device->ShowCursor(TRUE));
    printf("%s_hide_early = %d\n", Prefix, Device->ShowCursor(FALSE));
    Device->SetCursorPosition(10, 12, 0);
    sprintf_s(Name, "%s_cursor_early", Prefix);
    NcPrintCursor(Name, Before);
    printf("%s_props_null = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, nullptr));

    for (const auto& Plain : Plains)
    {
        Surface = nullptr;
        hr = Device->CreateOffscreenPlainSurface(Plain.Width, Plain.Height, Plain.Format, Plain.Pool, &Surface,
                                                 nullptr);
        if (Surface == nullptr)
        {
            printf("%s_props[%s] = none %08lX\n", Prefix, Plain.Name, hr);
            continue;
        }
        if (Plain.Format == D3DFMT_A8R8G8B8 && Plain.Pool != D3DPOOL_DEFAULT)
            NcFillCursor(Surface, Plain.Width, Plain.Height);
        hr = Device->SetCursorProperties(Plain.HotX, Plain.HotY, Surface);
        printf("%s_props[%s] = %08lX\n", Prefix, Plain.Name, hr);
        sprintf_s(Name, "%s_cursor[%s]", Prefix, Plain.Name);
        NcPrintCursor(Name, Before);
        printf("%s_props[%s]_release = %lu\n", Prefix, Plain.Name, Surface->Release());
    }

    Surface = nullptr;
    Device->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Surface, nullptr);
    if (Surface != nullptr)
    {
        printf("%s_props_target = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Surface));
        printf("%s_props_target_release = %lu\n", Prefix, Surface->Release());
    }
    Surface = nullptr;
    Device->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, &Surface, nullptr);
    if (Surface != nullptr)
    {
        printf("%s_props_target_lockable = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Surface));
        printf("%s_props_target_lockable_release = %lu\n", Prefix, Surface->Release());
    }
    Device->CreateTexture(32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr);
    if (Texture != nullptr)
    {
        Texture->GetSurfaceLevel(0, &Level);
        printf("%s_props_texture = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Level));
        Level->Release();
        printf("%s_props_texture_release = %lu\n", Prefix, Texture->Release());
        Texture = nullptr;
    }
    Device->CreateTexture(32, 32, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr);
    if (Texture != nullptr)
    {
        Texture->GetSurfaceLevel(0, &Level);
        printf("%s_props_texture_dynamic = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Level));
        Level->Release();
        printf("%s_props_texture_dynamic_release = %lu\n", Prefix, Texture->Release());
        Texture = nullptr;
    }
    Surface = nullptr;
    Other->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, nullptr);
    if (Surface != nullptr)
    {
        printf("%s_props_other = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Surface));
        printf("%s_props_other_release = %lu\n", Prefix, Surface->Release());
    }

    /* A cursor the device holds, shown, moved and hidden */
    Surface = nullptr;
    Device->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &Surface, nullptr);
    if (Surface == nullptr)
        return;
    NcFillCursor(Surface, 32, 32);
    printf("%s_hold_props = %08lX\n", Prefix, Device->SetCursorProperties(7, 9, Surface));
    Surface->AddRef();
    printf("%s_hold_refs = %lu\n", Prefix, Surface->Release());
    printf("%s_show = %d\n", Prefix, Device->ShowCursor(TRUE));
    sprintf_s(Name, "%s_cursor_shown", Prefix);
    NcPrintCursor(Name, Before);
    printf("%s_show_again = %d\n", Prefix, Device->ShowCursor(TRUE));
    Device->SetCursorPosition(100, 120, 0);
    GetCursorPos(&Point);
    printf("%s_position = %ld,%ld\n", Prefix, Point.x, Point.y);
    SetCursorPos(30, 40);
    Device->SetCursorPosition(100, 120, D3DCURSOR_IMMEDIATE_UPDATE);
    GetCursorPos(&Point);
    printf("%s_position_moved = %ld,%ld\n", Prefix, Point.x, Point.y);
    Device->SetCursorPosition(101, 122, D3DCURSOR_IMMEDIATE_UPDATE);
    GetCursorPos(&Point);
    printf("%s_position_next = %ld,%ld\n", Prefix, Point.x, Point.y);
    NcFillCursor(Surface, 16, 16);
    printf("%s_props_shown = %08lX\n", Prefix, Device->SetCursorProperties(1, 2, Surface));
    sprintf_s(Name, "%s_cursor_reprops", Prefix);
    NcPrintCursor(Name, Before);
    printf("%s_hide = %d\n", Prefix, Device->ShowCursor(FALSE));
    sprintf_s(Name, "%s_cursor_hidden", Prefix);
    NcPrintCursor(Name, Before);
    Device->SetCursorPosition(140, 150, 0);
    GetCursorPos(&Point);
    printf("%s_position_hidden = %ld,%ld\n", Prefix, Point.x, Point.y);
    printf("%s_hide_again = %d\n", Prefix, Device->ShowCursor(FALSE));
    printf("%s_props_hidden = %08lX\n", Prefix, Device->SetCursorProperties(0, 0, Surface));
    sprintf_s(Name, "%s_cursor_hidden_props", Prefix);
    NcPrintCursor(Name, Before);
    SetCursor(Before);
    printf("%s_show_after_set = %d\n", Prefix, Device->ShowCursor(TRUE));
    sprintf_s(Name, "%s_cursor_after_set", Prefix);
    NcPrintCursor(Name, Before);
    printf("%s_release_held = %lu\n", Prefix, Surface->Release());

    printf("%s_reset = %08lX\n", Prefix, Device->Reset(&Again));
    sprintf_s(Name, "%s_cursor_reset", Prefix);
    NcPrintCursor(Name, Before);
    printf("%s_show_reset = %d\n", Prefix, Device->ShowCursor(TRUE));
    sprintf_s(Name, "%s_cursor_show_reset", Prefix);
    NcPrintCursor(Name, Before);
}

static
VOID
NcCheckCursor(
    _Inout_ NineCheck* Check)
{
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);
    D3DPRESENT_PARAMETERS Parameters = {};
    D3DPRESENT_PARAMETERS Made;
    IDirect3DDevice9Ex* DeviceEx = nullptr;
    IDirect3DDevice9* Device = nullptr;
    HCURSOR Before = GetCursor();
    POINT Saved = {};
    HRESULT hr;

    GetCursorPos(&Saved);
    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.hDeviceWindow = Window;
    Made = Parameters;
    hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, nullptr,
                                   &DeviceEx);
    printf("cursor_create_ex = %08lX\n", hr);
    Made = Parameters;
    hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &Made, &Device);
    printf("cursor_create_plain = %08lX\n", hr);
    if (DeviceEx != nullptr && Device != nullptr)
    {
        NcCheckCursorOn("cursor_ex", DeviceEx, Device, &Parameters, Before);
        printf("cursor_ex_release = %lu\n", DeviceEx->Release());
        NcPrintCursor("cursor_ex_released", Before);
        DeviceEx = nullptr;
        SetCursor(Before);
        NcCheckCursorOn("cursor_plain", Device, Device, &Parameters, Before);
        printf("cursor_plain_release = %lu\n", Device->Release());
        NcPrintCursor("cursor_plain_released", Before);
        Device = nullptr;
    }
    if (DeviceEx != nullptr)
        DeviceEx->Release();
    if (Device != nullptr)
        Device->Release();
    SetCursor(Before);
    SetCursorPos(Saved.x, Saved.y);
    DestroyWindow(Window);
    Plain->Release();
}

/* Present statistics of a windowed chain; times vary from run to run, so only the counts print */
static
VOID
NcCheckPresentStatsOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DSwapChain9* Chain = nullptr;
    IDirect3DSwapChain9Ex* ChainEx = nullptr;
    D3DPRESENTSTATS Stats;
    UINT Count;
    HRESULT hr;

    Device->GetSwapChain(0, &Chain);
    if (Chain == nullptr)
        return;
    hr = Chain->QueryInterface(__uuidof(IDirect3DSwapChain9Ex), reinterpret_cast<VOID**>(&ChainEx));
    printf("%s_chain_ex = %08lX\n", Prefix, hr);
    Chain->Release();
    if (ChainEx == nullptr)
        return;
    for (UINT Round = 0; Round < 4; Round++)
    {
        Count = 0xCCCCCCCC;
        hr = ChainEx->GetLastPresentCount(&Count);
        printf("%s_last[%u] = %08lX %u\n", Prefix, Round, hr, Count);
        memset(&Stats, 0xCC, sizeof(Stats));
        hr = ChainEx->GetPresentStats(&Stats);
        printf("%s_stats[%u] = %08lX count %u %s\n", Prefix, Round, hr, Stats.PresentCount,
               Stats.PresentCount == 0xCCCCCCCC ? "untouched" : "written");
        Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
        printf("%s_present[%u] = %08lX\n", Prefix, Round, Device->Present(nullptr, nullptr, nullptr, nullptr));
    }
    /* GetLastPresentCount without an output pointer is left out: the system writes through it and crashes */
    ChainEx->Release();
}

static
VOID
NcCheckPresentStats(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        D3DSWAPEFFECT SwapEffect;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "stats_discard", D3DSWAPEFFECT_DISCARD, D3DCREATE_ENABLE_PRESENTSTATS, FALSE },
        { "stats_copy", D3DSWAPEFFECT_COPY, D3DCREATE_ENABLE_PRESENTSTATS, FALSE },
        { "stats_flipex", D3DSWAPEFFECT_FLIPEX, D3DCREATE_ENABLE_PRESENTSTATS, FALSE },
        { "stats_flipex_off", D3DSWAPEFFECT_FLIPEX, 0, FALSE },
        { "stats_discard_off", D3DSWAPEFFECT_DISCARD, 0, FALSE },
        { "stats_plain", D3DSWAPEFFECT_DISCARD, D3DCREATE_ENABLE_PRESENTSTATS, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        char Name[64];
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = Case.SwapEffect;
        Parameters.BackBufferCount = Case.SwapEffect == D3DSWAPEFFECT_FLIPEX ? 2 : 1;
        Parameters.hDeviceWindow = Window;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING | Case.Behavior,
                                     &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window,
                                           D3DCREATE_HARDWARE_VERTEXPROCESSING | Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        sprintf_s(Name, "%s", Case.Name);
        NcCheckPresentStatsOn(Name, Device);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* FNV-1a over a block of state, so a whole table prints as one number */
static
UINT
NcHashState(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 2166136261u;

    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619u;
    }
    return Hash;
}

/* Every render, stage and sampler state, transform, the viewport and scissor rectangle, material, clip planes,
   clip status, patch mode and vertex processing mode, each table as one hash */
static
VOID
NcPrintStateTables(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const DWORD Samplers[] = { 0, 15, D3DDMAPSAMPLER, D3DVERTEXTEXTURESAMPLER0, D3DVERTEXTEXTURESAMPLER3 };
    static const DWORD Transforms[] = { D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_TEXTURE0, D3DTS_TEXTURE7, D3DTS_WORLD,
                                        D3DTS_WORLDMATRIX(255) };
    DWORD Values[256];
    UINT Failed = 0;
    D3DMATRIX Matrix;
    D3DVIEWPORT9 Viewport = {};
    RECT Scissor = {};
    D3DMATERIAL9 Material = {};
    D3DCLIPSTATUS9 Status = {};
    FLOAT Plane[4];
    UINT Hash;
    DWORD Fvf = 0xDEADBEEF;

    for (UINT State = 0; State < 256; State++)
    {
        Values[State] = 0xDEADBEEF;
        if (FAILED(Device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(State), &Values[State])))
            Failed += 1;
    }
    printf("%s_render = %08X failed %u\n", Prefix, NcHashState(Values, sizeof(Values)), Failed);
    for (UINT Stage = 0; Stage < 8; Stage++)
    {
        for (UINT Type = 0; Type < 33; Type++)
        {
            Values[Type] = 0xDEADBEEF;
            Device->GetTextureStageState(Stage, static_cast<D3DTEXTURESTAGESTATETYPE>(Type), &Values[Type]);
        }
        printf("%s_stage[%u] = %08X\n", Prefix, Stage, NcHashState(Values, 33 * sizeof(DWORD)));
    }
    for (DWORD Sampler : Samplers)
    {
        for (UINT Type = 0; Type < 14; Type++)
        {
            Values[Type] = 0xDEADBEEF;
            Device->GetSamplerState(Sampler, static_cast<D3DSAMPLERSTATETYPE>(Type), &Values[Type]);
        }
        printf("%s_sampler[%lu] = %08X\n", Prefix, Sampler, NcHashState(Values, 14 * sizeof(DWORD)));
    }
    for (DWORD Type : Transforms)
    {
        HRESULT hr;

        memset(&Matrix, 0xCD, sizeof(Matrix));
        hr = Device->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(Type), &Matrix);
        printf("%s_transform[%lu] = %08lX %08X\n", Prefix, Type, hr, NcHashState(&Matrix, sizeof(Matrix)));
    }
    Device->GetViewport(&Viewport);
    printf("%s_viewport = %lu %lu %lu %lu %08X\n", Prefix, Viewport.X, Viewport.Y, Viewport.Width, Viewport.Height,
           NcHashState(&Viewport.MinZ, 2 * sizeof(FLOAT)));
    Device->GetScissorRect(&Scissor);
    printf("%s_scissor = %ld %ld %ld %ld\n", Prefix, Scissor.left, Scissor.top, Scissor.right, Scissor.bottom);
    Device->GetMaterial(&Material);
    printf("%s_material = %08X\n", Prefix, NcHashState(&Material, sizeof(Material)));
    Hash = 0;
    for (UINT Index = 0; Index < 8; Index++)
    {
        memset(Plane, 0xCD, sizeof(Plane));
        Device->GetClipPlane(Index, Plane);
        Hash = Hash * 31 + NcHashState(Plane, sizeof(Plane));
    }
    printf("%s_planes = %08X\n", Prefix, Hash);
    Device->GetClipStatus(&Status);
    printf("%s_clipstatus = %08lX %08lX\n", Prefix, Status.ClipUnion, Status.ClipIntersection);
    Device->GetFVF(&Fvf);
    printf("%s_fvf = %08lX\n", Prefix, Fvf);
    printf("%s_npatch = %.3f software %d\n", Prefix, Device->GetNPatchMode(), Device->GetSoftwareVertexProcessing());
}

/* Lights, their enables and what binding a buffer or texture does to the application's references */
static
VOID
NcCheckBindingsOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DVertexBuffer9* Buffer = nullptr;
    IDirect3DVertexBuffer9* Read = nullptr;
    IDirect3DIndexBuffer9* Indices = nullptr;
    IDirect3DIndexBuffer9* ReadIndices = nullptr;
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DBaseTexture9* ReadTexture = nullptr;
    D3DLIGHT9 Light = {};
    BOOL Enabled = 7;
    UINT Offset = 0;
    UINT Stride = 0;
    HRESULT hr;

    hr = Device->LightEnable(3, TRUE);
    printf("%s_light_enable = %08lX\n", Prefix, hr);
    memset(&Light, 0xCD, sizeof(Light));
    hr = Device->GetLight(3, &Light);
    printf("%s_light_get = %08lX type %u diffuse %.1f %.1f %.1f %.1f direction %.1f %.1f %.1f\n", Prefix, hr,
           Light.Type, Light.Diffuse.r, Light.Diffuse.g, Light.Diffuse.b, Light.Diffuse.a, Light.Direction.x,
           Light.Direction.y, Light.Direction.z);
    hr = Device->GetLightEnable(3, &Enabled);
    printf("%s_light_enabled = %08lX %d\n", Prefix, hr, Enabled);
    Enabled = 7;
    hr = Device->GetLightEnable(2, &Enabled);
    printf("%s_light_between = %08lX %d\n", Prefix, hr, Enabled);
    hr = Device->LightEnable(3, FALSE);
    printf("%s_light_disable = %08lX\n", Prefix, hr);
    Enabled = 7;
    hr = Device->GetLightEnable(3, &Enabled);
    printf("%s_light_disabled = %08lX %d\n", Prefix, hr, Enabled);

    Device->CreateVertexBuffer(64, 0, 0, D3DPOOL_DEFAULT, &Buffer, nullptr);
    Device->CreateIndexBuffer(64, 0, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &Indices, nullptr);
    Device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr);
    if (Buffer == nullptr || Indices == nullptr || Texture == nullptr)
    {
        printf("%s_binding_resources = missing\n", Prefix);
    }
    else
    {
        printf("%s_stream_set = %08lX\n", Prefix, Device->SetStreamSource(2, Buffer, 8, 4));
        printf("%s_indices_set = %08lX\n", Prefix, Device->SetIndices(Indices));
        printf("%s_texture_set = %08lX\n", Prefix, Device->SetTexture(3, Texture));
        Buffer->AddRef();
        printf("%s_stream_refs = %lu\n", Prefix, Buffer->Release());
        Texture->AddRef();
        printf("%s_texture_refs = %lu\n", Prefix, Texture->Release());

        /* The bindings outlive the application's last references */
        printf("%s_stream_release = %lu\n", Prefix, Buffer->Release());
        printf("%s_indices_release = %lu\n", Prefix, Indices->Release());
        printf("%s_texture_release = %lu\n", Prefix, Texture->Release());
        hr = Device->GetStreamSource(2, &Read, &Offset, &Stride);
        printf("%s_stream_get = %08lX %s %u %u\n", Prefix, hr, Read == Buffer ? "same" : "other", Offset, Stride);
        hr = Device->GetIndices(&ReadIndices);
        printf("%s_indices_get = %08lX %s\n", Prefix, hr, ReadIndices == Indices ? "same" : "other");
        hr = Device->GetTexture(3, &ReadTexture);
        printf("%s_texture_get = %08lX %s\n", Prefix, hr, ReadTexture == Texture ? "same" : "other");
        if (Read != nullptr)
            printf("%s_stream_get_release = %lu\n", Prefix, Read->Release());
        if (ReadIndices != nullptr)
            printf("%s_indices_get_release = %lu\n", Prefix, ReadIndices->Release());
        if (ReadTexture != nullptr)
            printf("%s_texture_get_release = %lu\n", Prefix, ReadTexture->Release());
        Device->SetStreamSource(2, nullptr, 0, 0);
        Device->SetIndices(nullptr);
        Device->SetTexture(3, nullptr);
        Buffer = nullptr;
        Indices = nullptr;
        Texture = nullptr;
    }
    if (Buffer != nullptr)
        Buffer->Release();
    if (Indices != nullptr)
        Indices->Release();
    if (Texture != nullptr)
        Texture->Release();
}

/* Changes a little of every kind of state, so a reset can show what it puts back */
static
VOID
NcChangeStates(
    _In_ IDirect3DDevice9* Device)
{
    D3DMATRIX Matrix = {};
    D3DVIEWPORT9 Viewport = { 1, 2, 3, 4, 0.25f, 0.75f };
    RECT Scissor = { 1, 1, 5, 5 };
    D3DMATERIAL9 Material = {};
    D3DCLIPSTATUS9 Status = { 3, 5 };
    D3DLIGHT9 Light = {};
    FLOAT Plane[4] = { 1.0f, 2.0f, 3.0f, 4.0f };

    Matrix._11 = 2.0f;
    Matrix._44 = 3.0f;
    Material.Power = 4.0f;
    Light.Type = D3DLIGHT_POINT;
    Light.Range = 5.0f;
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    Device->SetRenderState(D3DRS_FOGENABLE, TRUE);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADD);
    Device->SetTextureStageState(3, D3DTSS_CONSTANT, 0x12345678);
    Device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    Device->SetSamplerState(D3DVERTEXTEXTURESAMPLER0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    Device->SetTransform(D3DTS_VIEW, &Matrix);
    Device->SetTransform(D3DTS_WORLDMATRIX(255), &Matrix);
    Device->SetViewport(&Viewport);
    Device->SetScissorRect(&Scissor);
    Device->SetMaterial(&Material);
    Device->SetClipPlane(1, Plane);
    Device->SetClipStatus(&Status);
    Device->SetFVF(D3DFVF_XYZ);
    Device->SetNPatchMode(2.0f);
    Device->SetLight(1, &Light);
    Device->LightEnable(1, TRUE);
}

static
VOID
NcCheckStatesOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    D3DPRESENT_PARAMETERS Again = *Parameters;
    D3DLIGHT9 Light = {};
    BOOL Enabled = 7;
    char Name[64];
    HRESULT hr;

    sprintf_s(Name, "%s_initial", Prefix);
    NcPrintStateTables(Name, Device);
    NcChangeStates(Device);
    sprintf_s(Name, "%s_changed", Prefix);
    NcPrintStateTables(Name, Device);
    NcCheckBindingsOn(Prefix, Device);

    printf("%s_reset = %08lX\n", Prefix, Device->Reset(&Again));
    sprintf_s(Name, "%s_after_reset", Prefix);
    NcPrintStateTables(Name, Device);
    hr = Device->GetLight(1, &Light);
    printf("%s_reset_light = %08lX type %u\n", Prefix, hr, Light.Type);
    hr = Device->GetLightEnable(1, &Enabled);
    printf("%s_reset_light_enable = %08lX %d\n", Prefix, hr, Enabled);
}

/* Pipeline state on every kind of vertex processing a windowed device can have, before and after a reset */
static
VOID
NcCheckStates(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "states_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "states_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "states_sw", D3DCREATE_SOFTWARE_VERTEXPROCESSING, TRUE },
        { "states_mixed", D3DCREATE_MIXED_VERTEXPROCESSING, TRUE },
        { "states_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        D3DPRESENT_PARAMETERS Made;
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        Parameters.EnableAutoDepthStencil = TRUE;
        Parameters.AutoDepthStencilFormat = D3DFMT_D24S8;
        Made = Parameters;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Made, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Made, nullptr, &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckStatesOn(Case.Name, Device, &Parameters);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* The first register a kind of constant refuses, found by setting one register at a time */
static
UINT
NcConstantRegisters(
    _In_ IDirect3DDevice9* Device,
    _In_ UINT Kind)
{
    static const FLOAT Floats[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    static const INT Ints[4] = { 1, 2, 3, 4 };
    static const BOOL Bools[1] = { TRUE };
    UINT Low = 0;
    UINT High = 0x10000;

    while (Low < High)
    {
        UINT Middle = (Low + High) / 2;
        HRESULT hr = E_FAIL;

        switch (Kind)
        {
        case 0:
            hr = Device->SetVertexShaderConstantF(Middle, Floats, 1);
            break;
        case 1:
            hr = Device->SetVertexShaderConstantI(Middle, Ints, 1);
            break;
        case 2:
            hr = Device->SetVertexShaderConstantB(Middle, Bools, 1);
            break;
        case 3:
            hr = Device->SetPixelShaderConstantF(Middle, Floats, 1);
            break;
        case 4:
            hr = Device->SetPixelShaderConstantI(Middle, Ints, 1);
            break;
        default:
            hr = Device->SetPixelShaderConstantB(Middle, Bools, 1);
            break;
        }
        if (SUCCEEDED(hr))
            Low = Middle + 1;
        else
            High = Middle;
    }
    return Low;
}

/* Constant limits of each kind, values set and read back over ranges, and what a reset leaves */
static
VOID
NcCheckConstantsOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ const D3DPRESENT_PARAMETERS* Parameters)
{
    static const PCSTR Kinds[] = { "vf", "vi", "vb", "pf", "pi", "pb" };
    D3DPRESENT_PARAMETERS Again = *Parameters;
    FLOAT Floats[4 * 16];
    FLOAT Read[4 * 16];
    INT Ints[4 * 8];
    INT ReadInts[4 * 8];
    BOOL Bools[8];
    BOOL ReadBools[8];
    HRESULT hr;

    for (UINT Kind = 0; Kind < _countof(Kinds); Kind++)
        printf("%s_%s_registers = %u\n", Prefix, Kinds[Kind], NcConstantRegisters(Device, Kind));

    for (UINT Index = 0; Index < _countof(Floats); Index++)
        Floats[Index] = static_cast<FLOAT>(Index) * 0.25f - 3.0f;
    for (UINT Index = 0; Index < _countof(Ints); Index++)
        Ints[Index] = static_cast<INT>(Index * 7) - 50;
    for (UINT Index = 0; Index < _countof(Bools); Index++)
        Bools[Index] = static_cast<BOOL>(Index % 3);

    printf("%s_set_vf = %08lX\n", Prefix, Device->SetVertexShaderConstantF(4, Floats, 16));
    printf("%s_set_pf = %08lX\n", Prefix, Device->SetPixelShaderConstantF(2, Floats, 16));
    printf("%s_set_vi = %08lX\n", Prefix, Device->SetVertexShaderConstantI(3, Ints, 8));
    printf("%s_set_pi = %08lX\n", Prefix, Device->SetPixelShaderConstantI(5, Ints, 8));
    printf("%s_set_vb = %08lX\n", Prefix, Device->SetVertexShaderConstantB(1, Bools, 8));
    printf("%s_set_pb = %08lX\n", Prefix, Device->SetPixelShaderConstantB(6, Bools, 8));
    printf("%s_set_vf_overlap = %08lX\n", Prefix, Device->SetVertexShaderConstantF(10, Floats + 8, 2));

    for (UINT Start = 0; Start < 12; Start += 3)
    {
        memset(Read, 0xCC, sizeof(Read));
        hr = Device->GetVertexShaderConstantF(Start, Read, 16);
        printf("%s_get_vf[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(Read, sizeof(Read)));
        memset(Read, 0xCC, sizeof(Read));
        hr = Device->GetPixelShaderConstantF(Start, Read, 16);
        printf("%s_get_pf[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(Read, sizeof(Read)));
        memset(ReadInts, 0xCC, sizeof(ReadInts));
        hr = Device->GetVertexShaderConstantI(Start, ReadInts, 4);
        printf("%s_get_vi[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(ReadInts, sizeof(ReadInts)));
        memset(ReadInts, 0xCC, sizeof(ReadInts));
        hr = Device->GetPixelShaderConstantI(Start, ReadInts, 4);
        printf("%s_get_pi[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(ReadInts, sizeof(ReadInts)));
        memset(ReadBools, 0xCC, sizeof(ReadBools));
        hr = Device->GetVertexShaderConstantB(Start, ReadBools, 4);
        printf("%s_get_vb[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(ReadBools, sizeof(ReadBools)));
        memset(ReadBools, 0xCC, sizeof(ReadBools));
        hr = Device->GetPixelShaderConstantB(Start, ReadBools, 4);
        printf("%s_get_pb[%u] = %08lX %08X\n", Prefix, Start, hr, NcHashState(ReadBools, sizeof(ReadBools)));
    }

    printf("%s_reset = %08lX\n", Prefix, Device->Reset(&Again));
    memset(Read, 0xCC, sizeof(Read));
    hr = Device->GetVertexShaderConstantF(0, Read, 16);
    printf("%s_reset_vf = %08lX %08X\n", Prefix, hr, NcHashState(Read, sizeof(Read)));
    memset(Read, 0xCC, sizeof(Read));
    hr = Device->GetPixelShaderConstantF(0, Read, 16);
    printf("%s_reset_pf = %08lX %08X\n", Prefix, hr, NcHashState(Read, sizeof(Read)));
    memset(ReadInts, 0xCC, sizeof(ReadInts));
    hr = Device->GetVertexShaderConstantI(0, ReadInts, 8);
    printf("%s_reset_vi = %08lX %08X\n", Prefix, hr, NcHashState(ReadInts, sizeof(ReadInts)));
    memset(ReadBools, 0xCC, sizeof(ReadBools));
    hr = Device->GetPixelShaderConstantB(0, ReadBools, 8);
    printf("%s_reset_pb = %08lX %08X\n", Prefix, hr, NcHashState(ReadBools, sizeof(ReadBools)));
}

/* The state a block check sets and reads, as one hash */
static
UINT
NcHashBlockState(
    _In_ IDirect3DDevice9* Device)
{
    struct
    {
        DWORD States[4];
        DWORD Stage;
        DWORD Sampler;
        D3DMATRIX View;
        D3DVIEWPORT9 Viewport;
        FLOAT Constants[8];
        INT Ints[4];
        BOOL Bools[2];
        FLOAT Plane[4];
        UINT Frequency;
        DWORD Fvf;
    } Snapshot;

    memset(&Snapshot, 0, sizeof(Snapshot));
    Device->GetRenderState(D3DRS_CULLMODE, &Snapshot.States[0]);
    Device->GetRenderState(D3DRS_FOGCOLOR, &Snapshot.States[1]);
    Device->GetRenderState(D3DRS_ZENABLE, &Snapshot.States[2]);
    Device->GetRenderState(D3DRS_AMBIENT, &Snapshot.States[3]);
    Device->GetTextureStageState(0, D3DTSS_COLOROP, &Snapshot.Stage);
    Device->GetSamplerState(1, D3DSAMP_ADDRESSU, &Snapshot.Sampler);
    Device->GetTransform(D3DTS_VIEW, &Snapshot.View);
    Device->GetViewport(&Snapshot.Viewport);
    Device->GetVertexShaderConstantF(2, Snapshot.Constants, 2);
    Device->GetPixelShaderConstantI(1, Snapshot.Ints, 1);
    Device->GetVertexShaderConstantB(4, Snapshot.Bools, 2);
    Device->GetClipPlane(0, Snapshot.Plane);
    Device->GetStreamSourceFreq(2, &Snapshot.Frequency);
    Device->GetFVF(&Snapshot.Fvf);
    return NcHashState(&Snapshot, sizeof(Snapshot));
}

/* Sets the state a block check looks at from a seed */
static
VOID
NcSetBlockState(
    _In_ IDirect3DDevice9* Device,
    _In_ UINT Seed)
{
    D3DMATRIX View = {};
    D3DVIEWPORT9 Viewport = { Seed, 1, 16, 8 + Seed, 0.25f, 0.75f };
    FLOAT Constants[8];
    INT Ints[4] = { static_cast<INT>(Seed), 2, 3, 4 };
    BOOL Bools[2] = { static_cast<BOOL>(Seed % 2), TRUE };
    FLOAT Plane[4] = { 0.0f, 1.0f, static_cast<FLOAT>(Seed), 0.0f };

    for (UINT Index = 0; Index < _countof(Constants); Index++)
        Constants[Index] = static_cast<FLOAT>(Seed * 10 + Index);
    View._11 = static_cast<FLOAT>(Seed);
    View._44 = 1.0f;
    Device->SetRenderState(D3DRS_CULLMODE, Seed % 3 + 1);
    Device->SetRenderState(D3DRS_FOGCOLOR, 0x00010203 * Seed);
    Device->SetRenderState(D3DRS_ZENABLE, Seed % 2);
    Device->SetRenderState(D3DRS_AMBIENT, 0x11111111 * Seed);
    Device->SetTextureStageState(0, D3DTSS_COLOROP, Seed % 5 + 1);
    Device->SetSamplerState(1, D3DSAMP_ADDRESSU, Seed % 3 + 1);
    Device->SetTransform(D3DTS_VIEW, &View);
    Device->SetViewport(&Viewport);
    Device->SetVertexShaderConstantF(2, Constants, 2);
    Device->SetPixelShaderConstantI(1, Ints, 1);
    Device->SetVertexShaderConstantB(4, Bools, 2);
    Device->SetClipPlane(0, Plane);
    Device->SetStreamSourceFreq(2, Seed + 1);
    Device->SetFVF(Seed % 2 == 0 ? D3DFVF_XYZ | D3DFVF_DIFFUSE : D3DFVF_XYZRHW);
}

/* Recorded blocks and each predefined kind: what the device holds after recording, applying and capturing */
static
VOID
NcCheckBlocksOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const DWORD Types[] = { 0, D3DSBT_ALL, D3DSBT_PIXELSTATE, D3DSBT_VERTEXSTATE, 4, 0x7FFFFFFF };
    IDirect3DStateBlock9* Block = nullptr;
    IDirect3DStateBlock9* Other = nullptr;
    HRESULT hr;

    printf("%s_initial = %08X\n", Prefix, NcHashBlockState(Device));
    printf("%s_end_idle = %08lX\n", Prefix, Device->EndStateBlock(&Other));
    printf("%s_begin = %08lX\n", Prefix, Device->BeginStateBlock());
    printf("%s_begin_again = %08lX\n", Prefix, Device->BeginStateBlock());
    NcSetBlockState(Device, 1);
    printf("%s_create_recording = %08lX\n", Prefix, Device->CreateStateBlock(D3DSBT_ALL, &Other));
    printf("%s_recording = %08X\n", Prefix, NcHashBlockState(Device));
    hr = Device->EndStateBlock(&Block);
    printf("%s_end = %08lX\n", Prefix, hr);
    if (Block != nullptr)
    {
        printf("%s_apply = %08lX\n", Prefix, Block->Apply());
        printf("%s_applied = %08X\n", Prefix, NcHashBlockState(Device));
        NcSetBlockState(Device, 2);
        printf("%s_capture = %08lX\n", Prefix, Block->Capture());
        NcSetBlockState(Device, 3);
        printf("%s_apply_captured = %08lX\n", Prefix, Block->Apply());
        printf("%s_captured = %08X\n", Prefix, NcHashBlockState(Device));
        printf("%s_release = %lu\n", Prefix, Block->Release());
        Block = nullptr;
    }

    for (DWORD Type : Types)
    {
        NcSetBlockState(Device, 4);
        hr = Device->CreateStateBlock(static_cast<D3DSTATEBLOCKTYPE>(Type), &Block);
        printf("%s_create[%lX] = %08lX\n", Prefix, Type, hr);
        if (Block == nullptr)
            continue;
        NcSetBlockState(Device, 5);
        printf("%s_apply[%lX] = %08lX %08X\n", Prefix, Type, Block->Apply(), NcHashBlockState(Device));
        NcSetBlockState(Device, 6);
        printf("%s_capture[%lX] = %08lX\n", Prefix, Type, Block->Capture());
        NcSetBlockState(Device, 7);
        printf("%s_apply_captured[%lX] = %08lX %08X\n", Prefix, Type, Block->Apply(), NcHashBlockState(Device));
        printf("%s_release[%lX] = %lu\n", Prefix, Type, Block->Release());
        Block = nullptr;
    }
}

/* State blocks on every kind of vertex processing a windowed device can have */
static
VOID
NcCheckBlocks(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "blocks_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "blocks_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "blocks_sw", D3DCREATE_SOFTWARE_VERTEXPROCESSING, TRUE },
        { "blocks_mixed", D3DCREATE_MIXED_VERTEXPROCESSING, TRUE },
        { "blocks_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckBlocksOn(Case.Name, Device);
        printf("%s_device_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Shaders, declarations and the declarations formats stand for, with what binding them reads back */
static
VOID
NcCheckShadersOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const DWORD VertexCode[] = { 0xFFFE0200, 0x0001FFFE, 0xABCDEF01, 0x05000051, 0xA00F0001, 0x3F800000,
                                        0x3F800000, 0x3F800000, 0x3F800000, 0x0200001F, 0x80000000, 0x900F0000,
                                        0x03000005, 0xC00F0000, 0x90E40000, 0xA0E40001, 0x0000FFFF };
    static const DWORD PixelCode[] = { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB0030000, 0x02000001, 0x800F0800,
                                       0xB0E40000, 0x0000FFFF };
    static const DWORD BadPixel[] = { 0xFFFF0200, 0x02000001, 0x800F0800, 0xB0E40000, 0x0000FFFF };
    static const DWORD Lookup[] = { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB0030000, 0x0200001F, 0x90000000,
                                    0xA00F0800, 0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800, 0x02000001,
                                    0x800F0800, 0x80E40000, 0x0000FFFF };
    static const DWORD NarrowLookup[] = { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB0010000, 0x0200001F, 0x90000000,
                                          0xA00F0800, 0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800, 0x02000001,
                                          0x800F0800, 0x80E40000, 0x0000FFFF };
    static const DWORD PartialTemp[] = { 0xFFFF0200, 0x02000001, 0x80030000, 0xA0E40000, 0x02000001, 0x800F0800,
                                         0x80E40000, 0x0000FFFF };
    static const DWORD* const Checked[] = { BadPixel, Lookup, NarrowLookup, PartialTemp };
    static const D3DVERTEXELEMENT9 Elements[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 1 },
        { 0, 20, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 28, D3DDECLTYPE_UBYTE4N, 0, D3DDECLUSAGE_BINORMAL, 0 },
        D3DDECL_END(),
    };
    static const DWORD Fvfs[] =
    {
        D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE3(3),
        D3DFVF_XYZRHW | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE4(0), D3DFVF_XYZB3 | D3DFVF_PSIZE,
    };
    IDirect3DVertexShader9* Vertex = nullptr;
    IDirect3DPixelShader9* Pixel = nullptr;
    IDirect3DPixelShader9* Broken = nullptr;
    IDirect3DVertexDeclaration9* Declaration = nullptr;
    IDirect3DVertexDeclaration9* Bound = nullptr;
    D3DVERTEXELEMENT9 Read[MAXD3DDECLLENGTH + 1];
    DWORD Function[32];
    UINT Size = 0;
    DWORD Fvf;
    HRESULT hr;

    printf("%s_vs = %08lX\n", Prefix, Device->CreateVertexShader(VertexCode, &Vertex));
    printf("%s_ps = %08lX\n", Prefix, Device->CreatePixelShader(PixelCode, &Pixel));
    for (UINT Index = 0; Index < _countof(Checked); Index++)
    {
        printf("%s_ps_checked[%u] = %08lX\n", Prefix, Index, Device->CreatePixelShader(Checked[Index], &Broken));
        if (Broken != nullptr)
            Broken->Release();
        Broken = nullptr;
    }
    if (Vertex != nullptr)
    {
        memset(Function, 0xCC, sizeof(Function));
        hr = Vertex->GetFunction(Function, &Size);
        printf("%s_vs_function = %08lX %u %08X\n", Prefix, hr, Size, NcHashState(Function, sizeof(Function)));
        printf("%s_vs_set = %08lX\n", Prefix, Device->SetVertexShader(Vertex));
        printf("%s_vs_release_bound = %lu\n", Prefix, Vertex->Release());
        printf("%s_vs_unset = %08lX\n", Prefix, Device->SetVertexShader(nullptr));
    }
    if (Pixel != nullptr)
    {
        printf("%s_ps_set = %08lX\n", Prefix, Device->SetPixelShader(Pixel));
        printf("%s_ps_unset = %08lX\n", Prefix, Device->SetPixelShader(nullptr));
        printf("%s_ps_release = %lu\n", Prefix, Pixel->Release());
    }

    hr = Device->CreateVertexDeclaration(Elements, &Declaration);
    printf("%s_decl = %08lX\n", Prefix, hr);
    if (Declaration != nullptr)
    {
        Size = 0;
        memset(Read, 0xCC, sizeof(Read));
        hr = Declaration->GetDeclaration(Read, &Size);
        printf("%s_decl_read = %08lX %u %08X\n", Prefix, hr, Size, NcHashState(Read, sizeof(Read)));
        Device->SetVertexDeclaration(Declaration);
        Fvf = 0xCCCCCCCC;
        Device->GetFVF(&Fvf);
        printf("%s_decl_fvf = %08lX\n", Prefix, Fvf);
        printf("%s_decl_release = %lu\n", Prefix, Declaration->Release());
    }
    for (DWORD Format : Fvfs)
    {
        printf("%s_fvf[%08lX] = %08lX\n", Prefix, Format, Device->SetFVF(Format));
        hr = Device->GetVertexDeclaration(&Bound);
        if (Bound == nullptr)
        {
            printf("%s_fvf_decl[%08lX] = %08lX none\n", Prefix, Format, hr);
            continue;
        }
        Size = 0;
        memset(Read, 0xCC, sizeof(Read));
        Bound->GetDeclaration(Read, &Size);
        printf("%s_fvf_decl[%08lX] = %08lX %u %08X\n", Prefix, Format, hr, Size, NcHashState(Read, sizeof(Read)));
        printf("%s_fvf_decl_release[%08lX] = %lu\n", Prefix, Format, Bound->Release());
        Bound = nullptr;
    }
    Device->SetVertexDeclaration(nullptr);
}

/* Shaders and declarations on every kind of vertex processing a windowed device can have */
static
VOID
NcCheckShaders(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "shaders_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "shaders_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "shaders_sw", D3DCREATE_SOFTWARE_VERTEXPROCESSING, TRUE },
        { "shaders_mixed", D3DCREATE_MIXED_VERTEXPROCESSING, TRUE },
        { "shaders_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckShadersOn(Case.Name, Device);
        printf("%s_device_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Shader constants on every kind of vertex processing a windowed device can have */
static
VOID
NcCheckConstants(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "constants_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "constants_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "constants_sw", D3DCREATE_SOFTWARE_VERTEXPROCESSING, TRUE },
        { "constants_mixed", D3DCREATE_MIXED_VERTEXPROCESSING, TRUE },
        { "constants_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        D3DPRESENT_PARAMETERS Made;
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        Made = Parameters;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Made, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Made, nullptr, &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckConstantsOn(Case.Name, Device, &Parameters);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Prints every pixel of a target, read through a system memory copy or by locking it */
static
VOID
NcPrintPixels(
    _In_z_ PCSTR Prefix,
    _In_z_ PCSTR Name,
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Target,
    _In_ BOOLEAN Lock)
{
    IDirect3DSurface9* Copy = nullptr;
    IDirect3DSurface9* Read = Target;
    D3DSURFACE_DESC Desc;
    D3DLOCKED_RECT Locked = {};
    HRESULT hr = S_OK;

    Target->GetDesc(&Desc);
    if (!Lock)
    {
        hr = Device->CreateOffscreenPlainSurface(Desc.Width, Desc.Height, Desc.Format, D3DPOOL_SYSTEMMEM, &Copy,
                                                 nullptr);
        if (SUCCEEDED(hr))
            hr = Device->GetRenderTargetData(Target, Copy);
        Read = Copy;
    }
    if (SUCCEEDED(hr))
        hr = Read->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    printf("%s_%s_read = %08lX\n", Prefix, Name, hr);
    if (SUCCEEDED(hr))
    {
        for (UINT Row = 0; Row < Desc.Height; Row++)
        {
            const DWORD* Pixels = reinterpret_cast<const DWORD*>(static_cast<BYTE*>(Locked.pBits) + Row * Locked.Pitch);

            printf("%s_%s[%u] =", Prefix, Name, Row);
            for (UINT Column = 0; Column < Desc.Width; Column++)
                printf(" %06lX", Pixels[Column] & 0xFFFFFF);
            printf("\n");
        }
        Read->UnlockRect();
    }
    if (Copy != nullptr)
        Copy->Release();
}

/* Shaders, vertices and indices the draw checks draw with */
static const DWORD NcDrawVertexCode[] = { 0xFFFE0101, 0x0000001F, 0x80000000, 0x900F0000, 0x0000001F, 0x80000005,
                                          0x900F0001, 0x00000001, 0xC00F0000, 0x90E40000, 0x00000001, 0xE00F0000,
                                          0x90E40001, 0x0000FFFF };
static const DWORD NcDrawColorCode[] = { 0xFFFF0200, 0x02000001, 0x800F0800, 0xA0E40000, 0x0000FFFF };
static const DWORD NcDrawSampleCode[] = { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB0030000, 0x0200001F, 0x90000000,
                                          0xA00F0800, 0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800, 0x02000001,
                                          0x800F0800, 0x80E40000, 0x0000FFFF };
static const D3DVERTEXELEMENT9 NcDrawElements[] = { { 0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0 },
                                                    { 0, 16, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
                                                    D3DDECL_END() };
static const FLOAT NcDrawQuad[] = { -1.0f, -1.0f, 0.5f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f, 0.5f, 1.0f, 0.0f, 0.0f,
                                    1.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 0.0f,
                                    -1.0f, -1.0f, 0.5f, 1.0f, 0.0f, 1.0f, -1.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f,
                                    0.0f, -1.0f, 0.5f, 1.0f, 1.0f, 1.0f };
static const WORD NcDrawIndices16[] = { 0, 1, 2, 2, 1, 3, 4, 5, 6 };
static const DWORD NcDrawIndices32[] = { 0, 1, 2, 2, 1, 3, 4, 5, 6 };

/* The objects one device's draw checks make */
struct NcDrawObjects
{
    IDirect3DVertexShader9* Vertex;
    IDirect3DPixelShader9* Color;
    IDirect3DPixelShader9* Sample;
    IDirect3DVertexDeclaration9* Declaration;
    IDirect3DVertexBuffer9* Buffer;
    IDirect3DIndexBuffer9* Index16;
    IDirect3DIndexBuffer9* Index32;
    IDirect3DTexture9* Texture;
    IDirect3DSurface9* Target;
    IDirect3DSurface9* Lockable;
    IDirect3DSurface9* Original;
};

static
BOOLEAN
NcMakeDrawObjects(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ BOOLEAN Managed,
    _Out_ NcDrawObjects* Made)
{
    static const DWORD Texels[] = { 0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00 };
    D3DPOOL Pool = Managed ? D3DPOOL_MANAGED : D3DPOOL_DEFAULT;
    DWORD Usage = Managed ? 0 : D3DUSAGE_DYNAMIC;
    D3DLOCKED_RECT Locked = {};
    VOID* Data;

    ZeroMemory(Made, sizeof(*Made));
    printf("%s_vs = %08lX\n", Prefix, Device->CreateVertexShader(NcDrawVertexCode, &Made->Vertex));
    printf("%s_ps = %08lX\n", Prefix, Device->CreatePixelShader(NcDrawColorCode, &Made->Color));
    printf("%s_ps_sample = %08lX\n", Prefix, Device->CreatePixelShader(NcDrawSampleCode, &Made->Sample));
    printf("%s_decl = %08lX\n", Prefix, Device->CreateVertexDeclaration(NcDrawElements, &Made->Declaration));
    printf("%s_vb = %08lX\n", Prefix, Device->CreateVertexBuffer(sizeof(NcDrawQuad), Usage, 0, Pool, &Made->Buffer,
                                                                 nullptr));
    printf("%s_ib16 = %08lX\n", Prefix, Device->CreateIndexBuffer(sizeof(NcDrawIndices16), Usage, D3DFMT_INDEX16,
                                                                  Pool, &Made->Index16, nullptr));
    printf("%s_ib32 = %08lX\n", Prefix, Device->CreateIndexBuffer(sizeof(NcDrawIndices32), Usage, D3DFMT_INDEX32,
                                                                  Pool, &Made->Index32, nullptr));
    printf("%s_texture = %08lX\n", Prefix, Device->CreateTexture(2, 2, 1, Usage, D3DFMT_A8R8G8B8, Pool,
                                                                 &Made->Texture, nullptr));
    printf("%s_target = %08lX\n", Prefix, Device->CreateRenderTarget(8, 4, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                                                     FALSE, &Made->Target, nullptr));
    printf("%s_lockable = %08lX\n", Prefix, Device->CreateRenderTarget(8, 4, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE,
                                                                       0, TRUE, &Made->Lockable, nullptr));
    printf("%s_original = %08lX\n", Prefix, Device->GetRenderTarget(0, &Made->Original));
    if (Made->Vertex == nullptr || Made->Color == nullptr || Made->Sample == nullptr ||
        Made->Declaration == nullptr || Made->Buffer == nullptr || Made->Index16 == nullptr ||
        Made->Index32 == nullptr || Made->Texture == nullptr || Made->Target == nullptr ||
        Made->Lockable == nullptr || Made->Original == nullptr)
    {
        return FALSE;
    }
    if (SUCCEEDED(Made->Buffer->Lock(0, 0, &Data, 0)))
    {
        CopyMemory(Data, NcDrawQuad, sizeof(NcDrawQuad));
        Made->Buffer->Unlock();
    }
    if (SUCCEEDED(Made->Index16->Lock(0, 0, &Data, 0)))
    {
        CopyMemory(Data, NcDrawIndices16, sizeof(NcDrawIndices16));
        Made->Index16->Unlock();
    }
    if (SUCCEEDED(Made->Index32->Lock(0, 0, &Data, 0)))
    {
        CopyMemory(Data, NcDrawIndices32, sizeof(NcDrawIndices32));
        Made->Index32->Unlock();
    }
    if (SUCCEEDED(Made->Texture->LockRect(0, &Locked, nullptr, 0)))
    {
        CopyMemory(Locked.pBits, Texels, 8);
        CopyMemory(static_cast<BYTE*>(Locked.pBits) + Locked.Pitch, Texels + 2, 8);
        Made->Texture->UnlockRect(0);
    }
    return TRUE;
}

static
VOID
NcReleaseDrawObjects(
    _In_ IDirect3DDevice9* Device,
    _Inout_ NcDrawObjects* Made)
{
    IUnknown* Objects[] = { Made->Vertex, Made->Color, Made->Sample, Made->Declaration, Made->Buffer,
                            Made->Index16, Made->Index32, Made->Texture, Made->Target, Made->Lockable,
                            Made->Original };

    if (Made->Original != nullptr)
        Device->SetRenderTarget(0, Made->Original);
    Device->SetTexture(0, nullptr);
    Device->SetStreamSource(0, nullptr, 0, 0);
    Device->SetIndices(nullptr);
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    for (IUnknown* Object : Objects)
    {
        if (Object != nullptr)
            Object->Release();
    }
}

/* Draws through application shaders: the checks before the first draw, buffers, application memory, managed
   resources, textures and what stream 0 and the index binding hold afterwards */
static
VOID
NcCheckDrawsOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device,
    _In_ BOOLEAN Managed)
{
    static const FLOAT Red[] = { 1.0f, 0.0f, 0.0f, 1.0f };
    static const FLOAT Green[] = { 0.0f, 1.0f, 0.0f, 1.0f };
    static const FLOAT Blue[] = { 0.0f, 0.0f, 1.0f, 1.0f };
    NcDrawObjects Made;
    IDirect3DVertexBuffer9* Stream = nullptr;
    IDirect3DIndexBuffer9* Bound = nullptr;
    D3DLOCKED_RECT Locked = {};
    VOID* Data;
    UINT Offset;
    UINT Stride;
    HRESULT hr;

    if (!NcMakeDrawObjects(Prefix, Device, Managed, &Made))
    {
        NcReleaseDrawObjects(Device, &Made);
        return;
    }

    printf("%s_set_target_null = %08lX\n", Prefix, Device->SetRenderTarget(0, nullptr));
    printf("%s_set_target_index = %08lX\n", Prefix, Device->SetRenderTarget(4, Made.Target));
    printf("%s_set_target = %08lX\n", Prefix, Device->SetRenderTarget(0, Made.Target));
    printf("%s_set_target_twice = %08lX\n", Prefix, Device->SetRenderTarget(1, Made.Target));
    printf("%s_set_depth_target = %08lX\n", Prefix, Device->SetDepthStencilSurface(Made.Target));
    printf("%s_set_depth_null = %08lX\n", Prefix, Device->SetDepthStencilSurface(nullptr));
    printf("%s_clear = %08lX\n", Prefix, Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF204060, 1.0f, 0));
    Device->SetVertexShader(Made.Vertex);
    Device->SetPixelShader(Made.Color);
    Device->SetPixelShaderConstantF(0, Red, 1);

    printf("%s_draw_outside = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));
    Device->BeginScene();
    printf("%s_draw_no_decl = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));
    printf("%s_draw_up_no_decl = %08lX\n", Prefix, Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, NcDrawQuad, 24));
    Device->SetFVF(D3DFVF_XYZ);
    printf("%s_draw_fvf_no_stream = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));
    Device->SetVertexDeclaration(Made.Declaration);
    printf("%s_draw_indexed_none = %08lX\n", Prefix, Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 1));
    printf("%s_draw_up_stride = %08lX\n", Prefix, Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, NcDrawQuad, 0));
    printf("%s_draw_indexed_up_stride = %08lX\n", Prefix,
           Device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 1, NcDrawIndices16, D3DFMT_INDEX16, NcDrawQuad, 0));
    Device->SetStreamSource(0, Made.Buffer, 0, 24);
    Device->SetIndices(Made.Index16);
    printf("%s_draw_strip = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
    Device->EndScene();
    NcPrintPixels(Prefix, "pixels_strip", Device, Made.Target, FALSE);

    printf("%s_draw_after_scene = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1));
    Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    Device->BeginScene();
    Device->SetPixelShaderConstantF(0, Green, 1);
    printf("%s_draw_indexed = %08lX\n", Prefix, Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 7, 6, 1));
    Device->SetIndices(Made.Index32);
    Device->SetPixelShaderConstantF(0, Blue, 1);
    printf("%s_draw_indexed32 = %08lX\n", Prefix, Device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, -4, 4, 3, 0, 1));
    Device->EndScene();
    NcPrintPixels(Prefix, "pixels_indexed", Device, Made.Target, FALSE);

    Device->SetRenderTarget(0, Made.Lockable);
    Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
    Device->BeginScene();
    printf("%s_draw_up = %08lX\n", Prefix, Device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, NcDrawQuad + 24, 24));
    hr = Device->GetStreamSource(0, &Stream, &Offset, &Stride);
    printf("%s_stream_after_up = %08lX %d %u %u\n", Prefix, hr, Stream == nullptr, Offset, Stride);
    if (Stream != nullptr)
        Stream->Release();
    Device->SetStreamSource(0, Made.Buffer, 24, 24);
    Device->SetPixelShaderConstantF(0, Green, 1);
    printf("%s_draw_indexed_up = %08lX\n", Prefix,
           Device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 1, NcDrawIndices16 + 3, D3DFMT_INDEX16,
                                          NcDrawQuad, 24));
    hr = Device->GetIndices(&Bound);
    printf("%s_indices_after_up = %08lX %d\n", Prefix, hr, Bound == nullptr);
    if (Bound != nullptr)
        Bound->Release();
    hr = Device->GetStreamSource(0, &Stream, &Offset, &Stride);
    printf("%s_stream_after_indexed_up = %08lX %d %u %u\n", Prefix, hr, Stream == nullptr, Offset, Stride);
    if (Stream != nullptr)
        Stream->Release();
    Device->EndScene();
    NcPrintPixels(Prefix, "pixels_up", Device, Made.Lockable, TRUE);

    Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);
    Device->SetStreamSource(0, Made.Buffer, 0, 24);
    Device->SetPixelShader(Made.Sample);
    Device->SetTexture(0, Made.Texture);
    Device->BeginScene();
    printf("%s_draw_textured = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
    Device->EndScene();
    NcPrintPixels(Prefix, "pixels_textured", Device, Made.Lockable, TRUE);
    if (Managed && SUCCEEDED(Made.Texture->LockRect(0, &Locked, nullptr, 0)))
    {
        static_cast<DWORD*>(Locked.pBits)[0] = 0xFF808080;
        Made.Texture->UnlockRect(0);
    }
    if (Managed && SUCCEEDED(Made.Buffer->Lock(24, 24, &Data, 0)))
    {
        static_cast<FLOAT*>(Data)[1] = 0.0f;
        Made.Buffer->Unlock();
    }
    Device->BeginScene();
    printf("%s_draw_updated = %08lX\n", Prefix, Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2));
    Device->EndScene();
    NcPrintPixels(Prefix, "pixels_updated", Device, Made.Lockable, TRUE);
    NcReleaseDrawObjects(Device, &Made);
}

/* Draws on extended, hardware and pure devices */
static
VOID
NcCheckDraws(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "draws_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "draws_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "draws_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckDrawsOn(Case.Name, Device, Case.Plain);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Transformed corners of an 8 by 4 target with diffuse, specular and two coordinate sets */
struct NcFixedVertex
{
    FLOAT X;
    FLOAT Y;
    FLOAT Z;
    FLOAT Rhw;
    DWORD Diffuse;
    DWORD Specular;
    FLOAT U0;
    FLOAT V0;
    FLOAT U1;
    FLOAT V1;
};

static const NcFixedVertex NcFixedQuad[] =
{
    { 0.0f, 0.0f, 0.5f, 1.0f, 0xC0FF2040, 0x40102030, 0.0f, 0.0f, 0.0f, 0.0f },
    { 8.0f, 0.0f, 0.5f, 1.0f, 0x8040FF20, 0x20302010, 1.0f, 0.0f, 0.0f, 1.0f },
    { 0.0f, 4.0f, 0.5f, 1.0f, 0x402040FF, 0x60203040, 0.0f, 1.0f, 1.0f, 0.0f },
    { 8.0f, 4.0f, 0.5f, 1.0f, 0xFFFFFFFF, 0x10101010, 1.0f, 1.0f, 1.0f, 1.0f },
};

/* Stage states of one fixed function case: stage, type, value; stage 0xFF sets a render state */
struct NcFixedState
{
    DWORD Stage;
    DWORD Type;
    DWORD Value;
};

struct NcFixedCase
{
    PCSTR Name;
    NcFixedState States[6];
};

static const NcFixedCase NcFixedCases[] =
{
    { "default", {} },
    { "select_diffuse", { { 0, D3DTSS_COLOROP, D3DTOP_SELECTARG2 }, { 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 } } },
    { "modulate2x", { { 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X }, { 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE } } },
    { "modulate4x", { { 0, D3DTSS_COLOROP, D3DTOP_MODULATE4X } } },
    { "add", { { 0, D3DTSS_COLOROP, D3DTOP_ADD }, { 0, D3DTSS_ALPHAOP, D3DTOP_ADD } } },
    { "addsigned", { { 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED } } },
    { "addsigned2x", { { 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED2X } } },
    { "subtract", { { 0, D3DTSS_COLOROP, D3DTOP_SUBTRACT } } },
    { "addsmooth", { { 0, D3DTSS_COLOROP, D3DTOP_ADDSMOOTH } } },
    { "blend_diffuse", { { 0, D3DTSS_COLOROP, D3DTOP_BLENDDIFFUSEALPHA } } },
    { "blend_texture", { { 0, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHA } } },
    { "blend_factor", { { 0, D3DTSS_COLOROP, D3DTOP_BLENDFACTORALPHA }, { 0xFF, D3DRS_TEXTUREFACTOR, 0x40FF8000 } } },
    { "blend_texture_pm", { { 0, D3DTSS_COLOROP, D3DTOP_BLENDTEXTUREALPHAPM } } },
    { "modulate_alpha_add", { { 0, D3DTSS_COLOROP, D3DTOP_MODULATEALPHA_ADDCOLOR } } },
    { "modulate_color_add", { { 0, D3DTSS_COLOROP, D3DTOP_MODULATECOLOR_ADDALPHA } } },
    { "dot3", { { 0, D3DTSS_COLOROP, D3DTOP_DOTPRODUCT3 } } },
    { "multiplyadd", { { 0, D3DTSS_COLOROP, D3DTOP_MULTIPLYADD }, { 0, D3DTSS_COLORARG0, D3DTA_SPECULAR } } },
    { "lerp", { { 0, D3DTSS_COLOROP, D3DTOP_LERP }, { 0, D3DTSS_COLORARG0, D3DTA_SPECULAR } } },
    { "complement", { { 0, D3DTSS_COLORARG1, D3DTA_TEXTURE | D3DTA_COMPLEMENT },
                      { 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE | D3DTA_COMPLEMENT } } },
    { "replicate", { { 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE } } },
    { "constant", { { 0, D3DTSS_COLORARG2, D3DTA_CONSTANT }, { 0, D3DTSS_CONSTANT, 0xFF2080FF } } },
    { "specular", { { 0xFF, D3DRS_SPECULARENABLE, TRUE } } },
    { "two_stages", { { 1, D3DTSS_COLOROP, D3DTOP_ADD }, { 1, D3DTSS_TEXCOORDINDEX, 1 },
                      { 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2 } } },
    { "temp", { { 0, D3DTSS_RESULTARG, D3DTA_TEMP }, { 1, D3DTSS_COLOROP, D3DTOP_SUBTRACT },
                { 1, D3DTSS_COLORARG1, D3DTA_DIFFUSE }, { 1, D3DTSS_COLORARG2, D3DTA_TEMP },
                { 1, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 } } },
    { "disabled", { { 0, D3DTSS_COLOROP, D3DTOP_DISABLE } } },
};

/* The states every case starts from */
static const NcFixedState NcFixedBaseline[] =
{
    { 0, D3DTSS_COLOROP, D3DTOP_MODULATE }, { 0, D3DTSS_COLORARG1, D3DTA_TEXTURE }, { 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE },
    { 0, D3DTSS_COLORARG0, D3DTA_CURRENT }, { 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 },
    { 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE }, { 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE }, { 0, D3DTSS_RESULTARG, D3DTA_CURRENT },
    { 1, D3DTSS_COLOROP, D3DTOP_DISABLE }, { 1, D3DTSS_COLORARG1, D3DTA_TEXTURE }, { 1, D3DTSS_COLORARG2, D3DTA_CURRENT },
    { 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE }, { 1, D3DTSS_TEXCOORDINDEX, 1 }, { 0xFF, D3DRS_SPECULARENABLE, FALSE },
    { 0xFF, D3DRS_TEXTUREFACTOR, 0x80406080 },
};

static
VOID
NcFixedSet(
    _In_ IDirect3DDevice9* Device,
    _In_reads_(Count) const NcFixedState* States,
    _In_ UINT Count)
{
    for (UINT Index = 0; Index < Count && States[Index].Type != 0; Index++)
    {
        const NcFixedState& State = States[Index];

        if (State.Stage == 0xFF)
            Device->SetRenderState(static_cast<D3DRENDERSTATETYPE>(State.Type), State.Value);
        else
            Device->SetTextureStageState(State.Stage, static_cast<D3DTEXTURESTAGESTATETYPE>(State.Type), State.Value);
    }
}

/* Transformed vertices through the fixed function texture stages, every pixel read back for each setup */
static
VOID
NcCheckFixedPixelOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const DWORD Texels[2][4] =
    {
        { 0x80FF0000, 0xFF00FF00, 0x400000FF, 0xC0FFFFFF },
        { 0xFF808080, 0x40204080, 0xC0FF8000, 0x00000000 },
    };
    IDirect3DTexture9* Textures[2] = {};
    IDirect3DSurface9* Target = nullptr;
    IDirect3DSurface9* Original = nullptr;
    D3DLOCKED_RECT Locked = {};
    char Name[64];

    for (UINT Index = 0; Index < 2; Index++)
    {
        printf("%s_texture%u = %08lX\n", Prefix, Index,
               Device->CreateTexture(2, 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Textures[Index],
                                     nullptr));
        if (Textures[Index] != nullptr && SUCCEEDED(Textures[Index]->LockRect(0, &Locked, nullptr, 0)))
        {
            CopyMemory(Locked.pBits, Texels[Index], 8);
            CopyMemory(static_cast<BYTE*>(Locked.pBits) + Locked.Pitch, Texels[Index] + 2, 8);
            Textures[Index]->UnlockRect(0);
        }
    }
    printf("%s_target = %08lX\n", Prefix, Device->CreateRenderTarget(8, 4, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                                                     FALSE, &Target, nullptr));
    printf("%s_original = %08lX\n", Prefix, Device->GetRenderTarget(0, &Original));
    if (Textures[0] == nullptr || Textures[1] == nullptr || Target == nullptr || Original == nullptr)
        goto Done;

    Device->SetRenderTarget(0, Target);
    Device->SetTexture(0, Textures[0]);
    Device->SetTexture(1, Textures[1]);
    Device->SetRenderState(D3DRS_LIGHTING, FALSE);
    printf("%s_fvf = %08lX\n", Prefix,
           Device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2));
    for (const NcFixedCase& Case : NcFixedCases)
    {
        Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
        NcFixedSet(Device, NcFixedBaseline, _countof(NcFixedBaseline));
        NcFixedSet(Device, Case.States, _countof(Case.States));
        Device->BeginScene();
        printf("%s_%s_draw = %08lX\n", Prefix, Case.Name,
               Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, NcFixedQuad, sizeof(*NcFixedQuad)));
        Device->EndScene();
        sprintf_s(Name, "fixed_%s", Case.Name);
        NcPrintPixels(Prefix, Name, Device, Target, FALSE);
    }

Done:
    if (Original != nullptr)
    {
        Device->SetRenderTarget(0, Original);
        Original->Release();
    }
    Device->SetTexture(0, nullptr);
    Device->SetTexture(1, nullptr);
    for (IDirect3DTexture9* Texture : Textures)
    {
        if (Texture != nullptr)
            Texture->Release();
    }
    if (Target != nullptr)
        Target->Release();
}

static
VOID
NcCheckFixedPixel(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "ffpixel_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "ffpixel_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "ffpixel_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckFixedPixelOn(Case.Name, Device);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Untransformed vertices with a position, normal, colors and a coordinate set */
struct NcLitVertex
{
    FLOAT Position[3];
    FLOAT Normal[3];
    DWORD Diffuse;
    DWORD Specular;
    FLOAT Uv[2];
};

static const NcLitVertex NcLitQuad[4] =
{
    { { -0.8f, -0.6f, 0.25f }, { -0.3f, 0.4f, -0.8f }, 0xC0FF2040, 0x40102030, { 0.0f, 1.0f } },
    { { 0.8f, -0.6f, 0.375f }, { 0.3f, 0.4f, -0.8f }, 0x8040FF20, 0x20302010, { 1.0f, 1.0f } },
    { { -0.8f, 0.6f, 0.5f }, { -0.3f, -0.4f, -0.8f }, 0x402040FF, 0x60203040, { 0.0f, 0.0f } },
    { { 0.8f, 0.6f, 0.625f }, { 0.3f, -0.4f, -0.8f }, 0xFFFFFFFF, 0x10101010, { 1.0f, 0.0f } },
};

/* Vertex pipeline setups; stage 0xFE sets a matrix from the table and 0xFD turns the light on */
static const NcFixedCase NcVertexCases[] =
{
    { "unlit", {} },
    { "matrices", { { 0xFE, D3DTS_WORLD, 1 }, { 0xFE, D3DTS_VIEW, 2 }, { 0xFE, D3DTS_PROJECTION, 3 } } },
    { "lit", { { 0xFF, D3DRS_LIGHTING, TRUE }, { 0xFD, 1, 0 } } },
    { "lit_specular", { { 0xFF, D3DRS_LIGHTING, TRUE }, { 0xFD, 1, 0 }, { 0xFF, D3DRS_SPECULARENABLE, TRUE },
                        { 0xFE, D3DTS_VIEW, 2 } } },
    { "texgen", { { 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR },
                  { 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2 }, { 0xFE, D3DTS_TEXTURE0, 1 } } },
    { "sphere", { { 0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_SPHEREMAP } } },
    { "fog", { { 0xFF, D3DRS_FOGENABLE, TRUE }, { 0xFF, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR },
               { 0xFF, D3DRS_FOGSTART, 0x3E800000 }, { 0xFF, D3DRS_FOGEND, 0x3F400000 },
               { 0xFF, D3DRS_FOGCOLOR, 0x0000FF80 } } },
};

static const D3DMATRIX NcVertexMatrices[] =
{
    { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
    { 0.8f, 0.6f, 0.0f, 0.0f, -0.6f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.125f, -0.25f, 0.5f, 1.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.96f, 0.28f, 0.0f, 0.0f, -0.28f, 0.96f, 0.0f, 0.0f, 0.0f, 2.0f, 1.0f },
    { 1.5f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0101f, 1.0f, 0.0f, 0.0f, -0.10101f, 0.0f },
};

/* Untransformed vertices through the fixed function vertex pipeline, every pixel read back for each setup */
static
VOID
NcCheckFixedVertexOn(
    _In_z_ PCSTR Prefix,
    _In_ IDirect3DDevice9* Device)
{
    static const DWORD Texels[4] = { 0x80FF0000, 0xFF00FF00, 0x400000FF, 0xC0FFFFFF };
    D3DLIGHT9 Light = {};
    D3DMATERIAL9 Material = {};
    IDirect3DTexture9* Texture = nullptr;
    IDirect3DSurface9* Target = nullptr;
    IDirect3DSurface9* Original = nullptr;
    D3DLOCKED_RECT Locked = {};
    char Name[64];

    printf("%s_texture = %08lX\n", Prefix,
           Device->CreateTexture(2, 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &Texture, nullptr));
    if (Texture != nullptr && SUCCEEDED(Texture->LockRect(0, &Locked, nullptr, 0)))
    {
        CopyMemory(Locked.pBits, Texels, 8);
        CopyMemory(static_cast<BYTE*>(Locked.pBits) + Locked.Pitch, Texels + 2, 8);
        Texture->UnlockRect(0);
    }
    printf("%s_target = %08lX\n", Prefix, Device->CreateRenderTarget(8, 4, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                                                     FALSE, &Target, nullptr));
    printf("%s_original = %08lX\n", Prefix, Device->GetRenderTarget(0, &Original));
    if (Texture == nullptr || Target == nullptr || Original == nullptr)
        goto Done;

    Light.Type = D3DLIGHT_POINT;
    Light.Diffuse.r = Light.Diffuse.g = Light.Diffuse.b = 1.0f;
    Light.Specular.r = Light.Specular.g = 0.5f;
    Light.Position.z = -1.5f;
    Light.Range = 8.0f;
    Light.Attenuation0 = 0.5f;
    Light.Attenuation1 = 0.25f;
    Material.Diffuse.r = Material.Diffuse.g = Material.Diffuse.b = Material.Diffuse.a = 0.75f;
    Material.Ambient.r = 0.25f;
    Material.Specular.g = Material.Specular.b = 0.5f;
    Material.Power = 8.0f;
    Device->SetLight(0, &Light);
    Device->SetMaterial(&Material);
    Device->SetRenderTarget(0, Target);
    Device->SetTexture(0, Texture);
    Device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    printf("%s_fvf = %08lX\n", Prefix,
           Device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1));
    for (const NcFixedCase& Case : NcVertexCases)
    {
        Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
        Device->SetRenderState(D3DRS_LIGHTING, FALSE);
        Device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        Device->SetRenderState(D3DRS_FOGENABLE, FALSE);
        Device->LightEnable(0, FALSE);
        Device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
        Device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        Device->SetTransform(D3DTS_WORLD, &NcVertexMatrices[0]);
        Device->SetTransform(D3DTS_VIEW, &NcVertexMatrices[0]);
        Device->SetTransform(D3DTS_PROJECTION, &NcVertexMatrices[0]);
        Device->SetTransform(D3DTS_TEXTURE0, &NcVertexMatrices[0]);
        for (UINT Index = 0; Index < _countof(Case.States) && Case.States[Index].Type != 0; Index++)
        {
            const NcFixedState& State = Case.States[Index];

            if (State.Stage == 0xFE)
                Device->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(State.Type), &NcVertexMatrices[State.Value]);
            else if (State.Stage == 0xFD)
                Device->LightEnable(0, TRUE);
            else
                NcFixedSet(Device, &State, 1);
        }
        Device->BeginScene();
        printf("%s_%s_draw = %08lX\n", Prefix, Case.Name,
               Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, NcLitQuad, sizeof(*NcLitQuad)));
        Device->EndScene();
        sprintf_s(Name, "vertex_%s", Case.Name);
        NcPrintPixels(Prefix, Name, Device, Target, FALSE);
    }

Done:
    if (Original != nullptr)
    {
        Device->SetRenderTarget(0, Original);
        Original->Release();
    }
    Device->SetTexture(0, nullptr);
    if (Texture != nullptr)
        Texture->Release();
    if (Target != nullptr)
        Target->Release();
}

/* The fixed function vertex pipeline on an extended device and on hardware and pure plain devices */
static
VOID
NcCheckFixedVertex(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
        BOOLEAN Plain;
    } Cases[] =
    {
        { "ffvertex_ex", D3DCREATE_HARDWARE_VERTEXPROCESSING, FALSE },
        { "ffvertex_hw", D3DCREATE_HARDWARE_VERTEXPROCESSING, TRUE },
        { "ffvertex_pure", D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE, TRUE },
    };
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (const auto& Case : Cases)
    {
        D3DPRESENT_PARAMETERS Parameters = {};
        IDirect3DDevice9Ex* DeviceEx = nullptr;
        IDirect3DDevice9* Device = nullptr;
        HRESULT hr;

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 48;
        Parameters.BackBufferHeight = 32;
        if (Case.Plain)
        {
            hr = Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, &Device);
        }
        else
        {
            hr = Check->Ex->CreateDeviceEx(0, D3DDEVTYPE_HAL, Window, Case.Behavior, &Parameters, nullptr,
                                           &DeviceEx);
            Device = DeviceEx;
        }
        printf("%s_create = %08lX\n", Case.Name, hr);
        if (Device == nullptr)
            continue;
        NcCheckFixedVertexOn(Case.Name, Device);
        printf("%s_release = %lu\n", Case.Name, Device->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

/* Shader token streams covering the validator's rules: valid and invalid ones of every shader model */
struct NcShaderCase
{
    UINT Count;
    DWORD Tokens[62];
};

static const NcShaderCase NcValidatorCorpus[] =
{
    { 46, { 0xFFFE0201, 0x0200001F, 0x8008000D, 0x90040004, 0x0200001F, 0x80000005, 0x800F0002, 0x0200001F, 0x8002000A, 0x900F0000, 0x05000030, 0xF00F0001, 0x00000100, 0x00000100, 0x00000000, 0x3A59B4D7, 0x05000051, 0xA00F0001, 0xFFFFFFFF, 0x000000FF, 0x00000100, 0x00000100, 0x15000025, 0xC00F0001, 0xB0E41000, 0xF0E40802, 0xA0280800, 0xF0E40118, 0x04000022, 0xE0070000, 0xA0FF104E, 0xA0AA0000, 0xB0FF1114, 0x0200000B, 0x80E40000, 0xA0E40802, 0x04000021, 0xE00F0002, 0x800000D9, 0xF0E42002, 0xF0E40800, 0x13000044, 0xB0180000, 0xB1001000, 0xF0E40007, 0x0000FFFF } },
    { 57, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000009, 0x90080000, 0x0200001F, 0x800D0005, 0x90010001, 0x0200001F, 0x8000000A, 0xA008000A, 0x0200001F, 0x80000005, 0xE00F0003, 0x0200001F, 0x80010003, 0x900F0001, 0x01000026, 0xD0000000, 0x0200000F, 0xB0021000, 0xA0AA1002, 0x01000060, 0x9DE40031, 0x14000021, 0xC0CF0002, 0xB1E41000, 0x90AA0000, 0xB0FE1100, 0x0301005E, 0xE0240001, 0xA4E40002, 0xABE40002, 0x03000011, 0xC04F0002, 0x90AA0000, 0xFCE40074, 0x0200001A, 0xF0000800, 0x80E40001, 0x0000001E, 0x02000044, 0x8E4F0000, 0x80E4009B, 0x01000040, 0xE00F0002, 0x0300005F, 0xE0070004, 0xA15510B7, 0xBBAA1001, 0x0400005A, 0x80210001, 0x80000000, 0xF0E40000, 0xB0550000, 0x0000FFFF } },
    { 58, { 0xFFFE0201, 0x0200002F, 0xE00F0800, 0x90C6A11C, 0x05000030, 0xF00F0002, 0x8BA222C8, 0x00000000, 0x00000000, 0x7B8B67E9, 0x03000018, 0xB001000C, 0xA0550000, 0xA0550800, 0x04000015, 0xB00F3001, 0xF0E40800, 0x80FF0001, 0xFC550801, 0x0100001E, 0xB1AA1000, 0x03000002, 0xC0030002, 0xF0E40801, 0x90E40002, 0x04000021, 0xC0000000, 0xA0CF2000, 0xF0E40801, 0xA0E40801, 0x0200001A, 0xA0E41000, 0xA0FF0002, 0x03000011, 0xB0021003, 0xA0FF0001, 0x80AA009F, 0x03000008, 0xCF0F0001, 0x9EE40001, 0xA0980802, 0x0500000A, 0xE0042006, 0xB0E40000, 0xA0FF20BD, 0xB0E40000, 0xFC550006, 0x06000001, 0xBF010017, 0x80AA1001, 0x9DE40002, 0xAB553006, 0xE0AA0801, 0xF0E40002, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 54, { 0xFFFE0300, 0x0200001F, 0x90000000, 0xA01F0803, 0x0200001F, 0x80000005, 0x90020000, 0x0200001F, 0x80000005, 0xE0070003, 0x0200001F, 0x80000000, 0xA0230800, 0x05000030, 0xF00F0000, 0x00000001, 0x00000100, 0x000000FF, 0x000000FF, 0x0200002F, 0xE0020801, 0x9A4EF876, 0x0200002E, 0x80080001, 0xFDFF0000, 0x0300000A, 0xE0040002, 0xE0AA08EC, 0x8DE40001, 0x02000006, 0x80170000, 0xFCE40800, 0x04000009, 0x800F0800, 0xA0E43000, 0x90550801, 0xBCFF1001, 0x00000000, 0x0200001A, 0xB0001002, 0xA0550002, 0x03000005, 0xE0420002, 0x80550001, 0xF13E0802, 0x04000009, 0xE02F001B, 0xACE40000, 0xA0002002, 0xD0550800, 0x0200004F, 0xD02F0001, 0x90E40000, 0x0000FFFF } },
    { 48, { 0xFFFE0300, 0x0200001F, 0x8001000B, 0x90080000, 0x0100001E, 0xF0550000, 0x1406005E, 0x801F0000, 0xB0551000, 0x90080800, 0xA0E40002, 0x02000001, 0xC0110002, 0xA0000005, 0x0300005F, 0xB04F1001, 0x90FF0000, 0x90E40001, 0x03000002, 0xC00F0002, 0x9DAA00A9, 0xF0E40804, 0x0300000C, 0xB0170004, 0xBCFF1002, 0xA0E40001, 0x02000023, 0xC0010000, 0x803F0000, 0x04000018, 0xC0142002, 0xF0E40800, 0xE0FF0807, 0xBBAA1112, 0x03000059, 0x810F0000, 0xA0550805, 0xA0E40001, 0x03000021, 0xD0040800, 0x8000002A, 0xA0000002, 0x04000058, 0xC0270002, 0xF0AA0000, 0xA0E4091C, 0xA0990049, 0x0000FFFF } },
    { 42, { 0xFFFE0101, 0x0000002F, 0xE00F0801, 0xFFFFFFFF, 0x0000005F, 0xC0010001, 0x80DD0002, 0xA0000000, 0x00000015, 0xB0010002, 0x90E40001, 0x90550005, 0x0000002A, 0x0000000C, 0xC00F0000, 0xFDE42800, 0xE0E40800, 0x00000005, 0xE0030001, 0x80002002, 0xE0000801, 0x00000011, 0xB0440002, 0x8B000000, 0xA0E40800, 0x0000004A, 0x809F0007, 0x92D100FB, 0x00000059, 0x80040000, 0xB0AA1001, 0xA8000800, 0x0000005C, 0xE0140002, 0xA0E40003, 0x0000004A, 0xC0040000, 0xE0E40800, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 41, { 0xFFFE0201, 0x0200001F, 0x80000009, 0x800F0002, 0x0200001F, 0x8000000B, 0xE01F0003, 0x0200001F, 0x800C000D, 0x901D0000, 0x0200001F, 0x80020008, 0x902F0002, 0x05000051, 0xA00F0002, 0x00000001, 0x0B0436FB, 0x3F800000, 0x00000100, 0x05000030, 0xF00F0001, 0x00000100, 0xBEE108E3, 0x3F800000, 0x00000000, 0x0300000C, 0xA00F0800, 0x91550000, 0xBCE41002, 0x01000019, 0xA0E40000, 0x0200004F, 0xE0130001, 0xF0E40800, 0x0200001B, 0x90000002, 0x80000000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 37, { 0xFFFE0300, 0x0200001F, 0x80050000, 0x800F0003, 0x0200001F, 0x8000000D, 0xE0070000, 0x05000030, 0xF0010001, 0x3F800000, 0xFFFFFFFF, 0x000000FF, 0x00000000, 0x01000019, 0xA0FF1001, 0x01000028, 0xAD550000, 0x04000011, 0xE0040007, 0xB1591000, 0xADE420D0, 0xB0550000, 0x14000002, 0xD00F0000, 0xE0AA2800, 0xB0E40001, 0x9BFF0002, 0x0300000C, 0xD01F0001, 0xA0E40001, 0xAB280801, 0x0300005F, 0x80030000, 0xEBAA0802, 0x90550002, 0x0000002C, 0x0000FFFF } },
    { 55, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80020005, 0xE0060001, 0x0200001F, 0x8000000D, 0x90070002, 0x05000030, 0xF00F0002, 0x8516D433, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000001, 0x0200002F, 0xE00F0928, 0xFFFFFFFF, 0x01000019, 0xA0E40802, 0x0302005E, 0xE0020002, 0xF0AA0801, 0xA0E40000, 0x1405002D, 0xB0E41000, 0xA0550001, 0xA1272000, 0xB0550001, 0x0200004F, 0xB00F1000, 0x80001001, 0x1400000A, 0x8A070001, 0xBD551000, 0xA0550800, 0xFB550805, 0x01000024, 0xA0E40002, 0x05000058, 0xD00F0001, 0xABE40002, 0xA00008F9, 0xB3FF3004, 0x80001000, 0x04000025, 0x8F0F0002, 0xAB551002, 0x9D550002, 0x8C000002, 0x03000009, 0x804F0001, 0xADE40002, 0xEEE40806, 0x0000FFFF } },
    { 56, { 0xFFFE0300, 0x0200001F, 0x80000004, 0xE0140003, 0x0200001F, 0x90000000, 0xA0070801, 0x0200001F, 0x80000000, 0xA0010809, 0x0200001F, 0x8000000B, 0xE0210000, 0x0200001F, 0x8000000B, 0xE02F0003, 0x04000022, 0xC00F0002, 0xC1AA0800, 0xF5AA00F0, 0x90BE0002, 0x03000021, 0x80210002, 0xF1FF0859, 0x9D550000, 0x05000022, 0xB04F1000, 0xF1550804, 0xF0AA0001, 0xA1552000, 0xB0AA0001, 0x0200005C, 0x800F0002, 0xB0E41001, 0x03000002, 0xE002001B, 0xA0AA1000, 0xE0360802, 0x02000023, 0x80040001, 0xF5E40002, 0x02000053, 0xB0031002, 0xE0000000, 0x0300004C, 0xB1020001, 0xB0AA1002, 0x80E40000, 0x0200001A, 0xE0550802, 0xFFE40802, 0x0300000C, 0xBF171000, 0x90E4001B, 0xA0550801, 0x0000FFFF } },
    { 49, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80010009, 0x80040001, 0x0200001F, 0x8001000E, 0xE00F0002, 0x0200001F, 0x80000008, 0xE00F0001, 0x0200001F, 0x80000000, 0xA0010800, 0x0200001F, 0x80000008, 0x80140002, 0x05000030, 0xF00F0000, 0x00000100, 0x00000000, 0xFFFFFFFF, 0x00000001, 0x13000001, 0xE0030002, 0xB09E1000, 0xA1E40800, 0x03000017, 0xE0020000, 0xA0E40000, 0xABE40000, 0x04000012, 0xE0120005, 0xB0E41004, 0x80E40002, 0xAC000000, 0x0200001A, 0xA0000000, 0xFBAA0002, 0x0300000D, 0xD00F0000, 0x90000002, 0xF0000002, 0x03000005, 0x810F0000, 0xB0AA1000, 0xA0FF0001, 0x0000FFFF } },
    { 40, { 0xFFFE0201, 0x0200001F, 0x80000005, 0x900F000E, 0x0200001F, 0x80000005, 0x900F0000, 0x0200001F, 0x8001000A, 0x801F0003, 0x03000015, 0xD0030001, 0xA0E41000, 0x8BE40000, 0x01000028, 0x80E40000, 0x02000024, 0xB00F1005, 0x8D090000, 0x13000013, 0x90081000, 0xB1551000, 0xA0E40006, 0x0000002B, 0x03000042, 0xE04B0000, 0x9E9D0001, 0x90AA0000, 0x03000055, 0xDF040800, 0x80E12000, 0x80550000, 0x01000019, 0xA2FF0002, 0x04000020, 0xC0082002, 0xF0E40800, 0xF0E40002, 0xA7000000, 0x0000FFFF } },
    { 40, { 0xFFFE0101, 0x0000001F, 0x8000000D, 0x800F000C, 0x0000001F, 0x8002000A, 0x90030003, 0x00000009, 0xB0011000, 0xADFF20F1, 0xA0E40002, 0x00000015, 0xB00F0000, 0xA0000000, 0xA0AA2002, 0x00000048, 0x80540000, 0xA0551031, 0x00000009, 0xB0021014, 0x90E40002, 0xB0E43001, 0x00000001, 0x8E030001, 0x8CE420D8, 0x0000001A, 0x8FF20000, 0xF0E40802, 0x0000001B, 0x8B000001, 0xE055084F, 0x00000001, 0x80280001, 0xA0AA0000, 0x00000050, 0xC0080001, 0x80AA0002, 0x9B000000, 0xE0AA0801, 0x0000FFFF } },
    { 57, { 0xFFFE0300, 0x0200001F, 0x80000003, 0x80080001, 0x0200001F, 0x80000000, 0x902F0003, 0x0200000F, 0x80000000, 0xA001080F, 0x0200001F, 0x80000009, 0xE0040000, 0x05000051, 0xA0080000, 0x00000001, 0x000000FF, 0x000000FF, 0x3F800000, 0x0200002F, 0xE00F0802, 0x00000000, 0x03000008, 0xD0080802, 0x80E40002, 0xE0AA0000, 0x0400005A, 0xB00F1000, 0xA0000801, 0xB0AA1000, 0x80000000, 0x02000046, 0xE7010001, 0xB0F51002, 0x01000060, 0xA0AA0058, 0x04000016, 0x80030009, 0xA0E40804, 0x90E40000, 0xED550801, 0x01000028, 0xBDE41000, 0x05000018, 0xC02F0002, 0xF1E42000, 0x90551000, 0xF0ED2800, 0xB0550001, 0x03000054, 0xC11F0002, 0x90E4207F, 0xE0AA0800, 0x02000023, 0xE0070000, 0xABFF0000, 0x0000FFFF } },
    { 45, { 0xFFFE0200, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000009, 0xE00F0001, 0x05000051, 0xA00F0002, 0x00000001, 0x841807D8, 0x00000000, 0xFFFFFFFF, 0x01000060, 0xF0A30805, 0x03000020, 0xB00F1000, 0x90550002, 0xFDE40000, 0x03000002, 0x80080018, 0x90AB0000, 0x90FF0002, 0x01000028, 0xB0E41001, 0x03000008, 0x800F0001, 0x81000001, 0x90E40001, 0x04000022, 0x800E0001, 0x90E40001, 0xB1001004, 0xA0E40000, 0x01000019, 0xA0000000, 0x0100001E, 0xA05200E2, 0x0200004D, 0xEF020001, 0xF0FF0003, 0x00000027, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 55, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x800C0000, 0x90070001, 0x0200001F, 0x8002000B, 0x80020002, 0x0200001F, 0x80000000, 0x8003000A, 0x0200001F, 0x80020009, 0xE00F0003, 0x1500004F, 0xE00F0000, 0xBDE41000, 0x90550802, 0xD1E40815, 0xFDE40801, 0x03000015, 0xE0030001, 0xEDE40002, 0x80E40001, 0x05000012, 0xB04F1001, 0xADAA2000, 0xB0AA0800, 0xA1E40000, 0xACD10000, 0x11000027, 0xBD831000, 0x02000007, 0x800F001F, 0x90000001, 0x0200004D, 0x800F0002, 0x80550001, 0x03000005, 0x80020800, 0xA0E41000, 0xB0FE1122, 0x03000020, 0xE0010000, 0xB0E41000, 0x8BE40001, 0x0200001A, 0x90000801, 0x8D550001, 0x0000002A, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 35, { 0xFFFE0200, 0x05000051, 0xD0080802, 0x00000100, 0x000000FF, 0x00000100, 0x37800000, 0x0200002F, 0xE00F0801, 0x00000100, 0x02000010, 0x80030001, 0xA0550000, 0x03000018, 0xC00F0001, 0xA0E40000, 0xA1E40002, 0x14000025, 0xEF010000, 0xA5E40001, 0x8DAA0010, 0xA0E40801, 0x06000004, 0x80080003, 0xE0E42800, 0x80E41000, 0xA0E40001, 0xA1002000, 0xD0F20001, 0x01000057, 0xD00A0000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 21, { 0xFFFE0101, 0x0000001F, 0x80000009, 0x900F0002, 0x0000001F, 0x8000000B, 0x90070000, 0x0000001F, 0x80020005, 0x90060000, 0x0000001F, 0x80090005, 0x90080001, 0x0000001F, 0x8001000B, 0xE0070000, 0x0000000C, 0xC0010002, 0xABE41001, 0xB1AA1000, 0x0000FFFF } },
    { 60, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8002000B, 0x80080000, 0x0200001F, 0x80010008, 0x90040001, 0x0200002F, 0xE00F0843, 0x00000000, 0x05000030, 0xF00F011B, 0x00000001, 0x00000001, 0x00000100, 0x00000001, 0x0200002E, 0x800F0004, 0xF0720005, 0x13000023, 0xC00F0001, 0xB0A71000, 0x91FF0000, 0x0600004F, 0x80040000, 0xA9E40000, 0x90E40002, 0x80482002, 0x80540801, 0x8DFF0000, 0x0000002C, 0x0300000B, 0xD0070002, 0xA9550003, 0x80E40001, 0x02000023, 0xB1041006, 0xA0FF0011, 0x04000058, 0xB0021000, 0x806C0070, 0xA0FF0000, 0x80FF00AE, 0x0300000E, 0xC0022002, 0xB0E40000, 0xB0E41001, 0x03000001, 0xE80F0002, 0xFCE42802, 0xB0000001, 0x01000001, 0xDF000000, 0x03000005, 0xCF040000, 0xA1E40000, 0xA1E41000, 0x0000FFFF } },
    { 28, { 0xFFFE0201, 0x0200001F, 0x8002000B, 0x9001000F, 0x0200001F, 0x80000005, 0x80120001, 0x13000001, 0xB0020000, 0xB04E1000, 0xAB000800, 0x0200001A, 0xA05500B3, 0x90FF0005, 0x03000021, 0xB0021000, 0xADB90000, 0x80811001, 0x0200002E, 0xE0070002, 0xA0000802, 0x12000060, 0xBD551000, 0xACE40000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 54, { 0xFFFE0300, 0x0200001F, 0x8002000A, 0xC0010802, 0x0200001F, 0x80000008, 0xE00F0001, 0x0200001F, 0x80000005, 0x90070000, 0x0200002F, 0xE00F0802, 0x3F800000, 0x03000021, 0xB0030002, 0xB0551000, 0xF0E40000, 0x03000024, 0xB02F1000, 0xA1E42000, 0xF0E40800, 0x0200001B, 0xE0AA0800, 0xA0550888, 0x0000002A, 0x0000002C, 0x04000005, 0x80072002, 0xB0E40000, 0xE0000800, 0xAB000001, 0x12000019, 0xB0551000, 0xF0E40000, 0x03000014, 0xC06F0000, 0x908110DA, 0xACE41004, 0x02000013, 0xBF211000, 0xF0FF0800, 0x02000001, 0xBC1F001E, 0x80DB0000, 0x04000050, 0xC0070000, 0xA0E40043, 0xBB001002, 0xD1E40041, 0x03000016, 0xD0D80001, 0xB0001001, 0x90000001, 0x0000FFFF } },
    { 43, { 0xFFFE0300, 0x0200001F, 0x80000003, 0x80010003, 0x0200001F, 0x80020005, 0xE02F0001, 0x0200001F, 0x80010006, 0x80230002, 0x05000051, 0xA00F0000, 0x00000001, 0x00000001, 0x00000000, 0x00000001, 0x05000051, 0xA00F0000, 0x000000FF, 0xBC595562, 0xFFFFFFFF, 0x00000100, 0x03000005, 0xE0030000, 0x9D000001, 0xA0E40001, 0x04000014, 0xB0010004, 0x90E40001, 0x90AA2005, 0xB0000000, 0x03000043, 0x8E430001, 0xA0552002, 0xF0000800, 0x03000009, 0xBE281002, 0xEBE40801, 0xF0E40805, 0x02000010, 0xF07F0801, 0x80E40000, 0x0000FFFF } },
    { 23, { 0xFFFE0101, 0x0000001F, 0x800D000D, 0x900F0002, 0x0000001F, 0x8002000E, 0x900F0003, 0x0000004F, 0xE0080000, 0x90E40112, 0x0000005E, 0x80080003, 0x88E40022, 0xE0FF0000, 0x00000006, 0xE00F0002, 0x9DAA003F, 0x00000060, 0xB0AA1000, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 29, { 0xFFFE0300, 0x0200001F, 0x8000000A, 0x90000001, 0x0200001F, 0x8000000D, 0xB0131003, 0x0200001F, 0x80010003, 0xE00F0000, 0x05000030, 0xF00F0001, 0x000000FF, 0x3F800000, 0xFFFFFFFF, 0x00000100, 0x0200002F, 0xE00F0802, 0x00000000, 0x02000024, 0x80040000, 0x90000000, 0x0000002B, 0x01000041, 0x80020002, 0x02000044, 0xB0220000, 0xFDAA0002, 0x0000FFFF } },
    { 43, { 0xFFFE0200, 0x0200001F, 0x800E0003, 0x90020001, 0x0200001F, 0x8000000A, 0x80020002, 0x0200001F, 0x80010000, 0xE0080000, 0x0200001F, 0x80000003, 0x80070002, 0x0200001F, 0x80000005, 0x90070008, 0x05000030, 0xF00F0085, 0x00000001, 0x000000FF, 0x00000100, 0x00000100, 0x0200002F, 0xE00F0800, 0x000000FF, 0x0200000E, 0x80010001, 0xABE408E8, 0x0200001A, 0xA0550001, 0x90E40000, 0x01000041, 0x8E370021, 0x05000004, 0x8E0F0000, 0xB0E41001, 0xA0FF2001, 0x90E40800, 0xB0E41002, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 20, { 0xFFFE03FF, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80020005, 0xE0030002, 0x0200002F, 0xE00F0802, 0x00000100, 0x04000025, 0xC01F0001, 0x8DE40002, 0xA0AA0000, 0x90550000, 0x0000001C, 0x03000025, 0xE0282000, 0xF0E40800, 0xE0000801, 0x0000FFFF } },
    { 55, { 0xFFFE0201, 0x0200001F, 0x8002000B, 0x90040001, 0x0200001F, 0x80010008, 0x90070004, 0x0200002F, 0xE0080802, 0x687E594E, 0x05000030, 0xF0040002, 0x000000FF, 0x3B06C5B2, 0x3F800000, 0x00000000, 0x03000049, 0x80022001, 0xB0E40000, 0x9C550000, 0x01000040, 0x831F1006, 0x02000047, 0x8F010000, 0xAB000000, 0x03000006, 0x80030000, 0xB0FF3000, 0xF0000800, 0x03000043, 0x802F0002, 0xB1FF280B, 0xF0AA0801, 0x03000005, 0xCF080002, 0xA0E400E5, 0xE0000802, 0x03000015, 0xB10F0001, 0x8C3B0000, 0x80AA0007, 0x01000028, 0x90000001, 0x0300004E, 0x8E280002, 0xB1FF1001, 0xD0000005, 0x03000058, 0x8CFF0001, 0x91AA0003, 0xA0E40013, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 29, { 0xFFFE0201, 0x0200001F, 0x8007000A, 0x90040003, 0x0200001F, 0x80000009, 0x80180000, 0x0200001F, 0x80000008, 0x902A0003, 0x0200001F, 0x8000000D, 0x90070009, 0x02000024, 0xB0011000, 0xF0E40800, 0x04000025, 0xD00F0002, 0xABE40002, 0xB1001000, 0xE1550801, 0x0B000005, 0xE00F0801, 0xABAA0000, 0xFC550000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 18, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200002F, 0xE00F0801, 0x00000100, 0x0200002F, 0xE00F0800, 0xFFFFFFFF, 0x04000021, 0xD0210007, 0xAE552002, 0xF0E40801, 0xA0AA0002, 0x1100001D, 0xBDE71000, 0x0000002A, 0x0000FFFF } },
    { 43, { 0xFFFE0300, 0x0200001F, 0x8001000A, 0x902F0005, 0x05000030, 0xF00F0001, 0x000000FF, 0x00000000, 0xFFFFFFFF, 0x00000001, 0x05000030, 0xF00F0001, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000100, 0x03000011, 0x80170009, 0x90140002, 0xB0001002, 0x02000001, 0xB0021000, 0x903A0001, 0x00000027, 0x0300000A, 0xA0010000, 0xBDAA1002, 0xFCE40806, 0x04000002, 0xE0170000, 0xA0E43002, 0xF0AA0801, 0xA0E40800, 0x03000048, 0xB0FF3000, 0xF0E40800, 0xB055101D, 0x03000005, 0xC1480002, 0xB0E40848, 0xF1E40002, 0x0000002C, 0x0000FFFF } },
    { 36, { 0xFFFE0300, 0x0200001F, 0x80000005, 0x80081002, 0x0200001F, 0x90000000, 0xA01F0801, 0x05000051, 0xA00F0002, 0xFFFFFFFF, 0xFFFFFFFF, 0x000000FF, 0x000000FF, 0x05000022, 0xB00F1000, 0x90E42001, 0xF0E40800, 0x8DE40000, 0xA1FF1000, 0x04000025, 0xE00F0000, 0xF0160800, 0xA1AA08B2, 0xA0FF0000, 0x03000020, 0xC0010002, 0x90E40000, 0x9CE40100, 0x02000026, 0xA0003001, 0xF0550800, 0x0300000A, 0x8E020003, 0xB0E410B9, 0xA0E41000, 0x0000FFFF } },
    { 47, { 0xFFFE0201, 0x0200001F, 0x80060003, 0x80010003, 0x0200001F, 0x80000004, 0xE00F0001, 0x0200001F, 0x80000003, 0x90010003, 0x0200001F, 0x80010005, 0xE00F0003, 0x0200001F, 0x8000000A, 0x90030003, 0x05000051, 0xA00F0000, 0x000000FF, 0x000000FF, 0x000000FF, 0xD2CA293E, 0x05000051, 0xA00F0000, 0xFFFFFFFF, 0x00000100, 0x00000000, 0x3F800000, 0x16000022, 0x90081001, 0xB1001000, 0xA0AA1002, 0x8CFF0001, 0xA0E42000, 0xF0E40800, 0x0000001C, 0x04000011, 0xB00F3001, 0xF0E40800, 0xA0FF106A, 0xA0E40000, 0x0100000F, 0xADE40800, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 47, { 0xFFFE0101, 0x0000001F, 0x8000000C, 0x90010001, 0x00000051, 0xA00F0802, 0x00000100, 0x000000FF, 0x3F800000, 0x000000FF, 0x00000013, 0xC0010001, 0xA0AA085C, 0x0000004C, 0xCF070001, 0xABFF0000, 0xAEFF2000, 0x00000025, 0xB0041000, 0xF0E60000, 0xBBE41114, 0xABAA1000, 0x00000022, 0xD0B30004, 0x0000001D, 0x00000016, 0xE0470003, 0x90000001, 0xA0E40000, 0xA1E40002, 0x00000045, 0xB0011025, 0xA0000000, 0x00000048, 0xB0030000, 0xB0E41005, 0x00000009, 0xB0030000, 0x8B5500B9, 0xA0000000, 0x00000010, 0x9E040000, 0x8CFF0002, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 14, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x02000001, 0xC00F0000, 0x90E40000, 0x01000019, 0xA0E41001, 0x0000001C, 0x0100001E, 0xA0E41000, 0x0000001C, 0x0000FFFF } },
    { 41, { 0xFFFE0300, 0x0200001F, 0x800B000A, 0xC02F0003, 0x0200001F, 0x80020005, 0x90030001, 0x0200001F, 0x80020005, 0x900F0002, 0x0200001F, 0x80020000, 0x9003000E, 0x0200001F, 0x98000000, 0xA0020800, 0x05000051, 0xA00F0000, 0x00000100, 0x7B0BE6AB, 0x3F800000, 0xB4817F1E, 0x05000051, 0xA00F0002, 0xFFFFFFFF, 0xD89D82FE, 0xFFFFFFFF, 0xE64227B6, 0x02060029, 0x81550000, 0xA6AC0000, 0x04000014, 0xD0022002, 0xF0E40800, 0xA0FF0002, 0xA0E80000, 0x0200001A, 0xAEFF1002, 0xB0AA1106, 0x0000001C, 0x0000FFFF } },
    { 53, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80020009, 0x90020002, 0x0200001F, 0x8001000B, 0x900F0001, 0x0200001F, 0x80000008, 0x900F0001, 0x0200001F, 0x80010005, 0x90040000, 0x0200001F, 0x80010008, 0x90030002, 0x0200002F, 0xE00F080A, 0x00000001, 0x05000051, 0xA00F0037, 0x5DACB233, 0x00000100, 0x3F800000, 0x3F800000, 0x01000019, 0x90E40002, 0x03000020, 0x80010000, 0x8CE40057, 0xBB001000, 0x14000017, 0xC0010001, 0xB0001000, 0xA0E40000, 0xCDE40000, 0x01000028, 0xFCC6001B, 0x03000002, 0xC0280002, 0x8DAA0000, 0xA0AA0002, 0x02000010, 0xB00F1003, 0xEB9A0805, 0x0100001B, 0xA0E40000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 17, { 0xFFFE0101, 0x00000016, 0xC0030002, 0xA1E41000, 0xA1E40000, 0x00000043, 0xB0FF3000, 0x0000005D, 0x800F0006, 0xF0100000, 0xF0E40002, 0xADAA0002, 0xF0000802, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 25, { 0xFFFE0101, 0x0000001F, 0x80290008, 0x900F0000, 0x0000001C, 0x00000002, 0xE0040002, 0x00000025, 0x8F0F0001, 0xA0AA1000, 0xA0E40000, 0x90630000, 0x00000016, 0xC0040001, 0x8B002000, 0xF0E400F5, 0x00000008, 0xE03F0001, 0xA1E42001, 0xA0FF1031, 0x0000002A, 0x00000044, 0xB0070006, 0xABE40002, 0x0000FFFF } },
    { 32, { 0xFFFE0201, 0x0200001F, 0x8000000B, 0x9007000E, 0x0200001F, 0x80020003, 0xE0090003, 0x0200001F, 0x8000000A, 0x902F0003, 0x0200001F, 0x80020000, 0x90040003, 0x0200002F, 0xE00F0801, 0x00000000, 0x0200002F, 0xE00F0801, 0x3F800000, 0x01000026, 0x9BFF0002, 0x0000001C, 0x03000018, 0xB0481004, 0x8C550005, 0x8BAA0000, 0x04000008, 0x80080002, 0xA0E42000, 0xB0E40000, 0xA0E410C0, 0x0000FFFF } },
    { 26, { 0xFFFE0201, 0x0200001B, 0x80000000, 0x900F0001, 0x0200001F, 0x80000005, 0x900C0009, 0x0200001F, 0x80010008, 0x8007000E, 0x05000030, 0xF00F00FB, 0x00000100, 0xEDDC0ABC, 0xFFFFFFFF, 0xFFFFFFFF, 0x02000049, 0xE0070801, 0x80000107, 0x02000046, 0xB0040000, 0xA0E40000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 39, { 0xFFFE0300, 0x0200001F, 0x98000000, 0xA0070802, 0x05000051, 0xA00F0000, 0x00000100, 0x00000001, 0xEE1DA388, 0xCF67A8B0, 0x03000015, 0xB0471000, 0xADAA0001, 0xB1E40800, 0x0300005F, 0xB00F1006, 0x8BE40000, 0xA0E40802, 0x03000009, 0x8002000F, 0x9C0000E4, 0xE0E40800, 0x02060029, 0xE0E40802, 0x9DFF0001, 0x02000026, 0x810F0000, 0xA0000002, 0x0200000F, 0x800F0002, 0x80940001, 0x1100002B, 0xB0E41000, 0x04000004, 0x80EE0005, 0xE0000001, 0xF0AA0002, 0xFBE40001, 0x0000FFFF } },
    { 26, { 0xFFFE0300, 0x0200001F, 0x8000000A, 0xE00F0006, 0x0200001F, 0x8000000A, 0xE0080001, 0x0200001F, 0x8000000A, 0xE0010000, 0x0200001F, 0x8006000E, 0x900E0000, 0x0200001F, 0x80070005, 0xB0020003, 0x0200002F, 0xE00F0800, 0x000000FF, 0x0200002F, 0xE00F0800, 0x000000FF, 0x0200001B, 0xF9E40806, 0xA0C10000, 0x0000FFFF } },
    { 36, { 0xFFFE02FF, 0x0200001F, 0x8001000A, 0x90010003, 0x0200001F, 0x80000008, 0x80170001, 0x03000021, 0x801F0002, 0xF0000000, 0x80E40002, 0x0000002A, 0x02000007, 0x80020000, 0x97AA0000, 0x0300000C, 0x800E0004, 0xF0E40001, 0xA0E40801, 0x04000004, 0xE1040002, 0xA0FF1000, 0xA0550800, 0x90E40000, 0x01000056, 0xB0E41000, 0x02000047, 0xBD070001, 0xA0000003, 0x0200000E, 0xD0480015, 0x90FF0108, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 27, { 0xFFFE03FF, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8000000A, 0x900F0002, 0x0200001F, 0xA0000000, 0xA00F0801, 0x0200001F, 0x98000000, 0xA00F0801, 0x0200001F, 0x8002000D, 0x900F0002, 0x0200001F, 0x8000000D, 0x90070005, 0x0200002F, 0xE00F0801, 0x00000000, 0x01000060, 0xE1E40800, 0x02060029, 0x91E40001, 0xA0550000, 0x0000FFFF } },
    { 48, { 0xFFFE0201, 0x0200001F, 0x800A0009, 0x9006000A, 0x05000051, 0xA00F0000, 0x00000000, 0x00000100, 0xFDF17B7D, 0xFFFFFFFF, 0x03000002, 0xB00F0816, 0x80E40000, 0xE1AA0801, 0x03000014, 0xE01F0004, 0x90B30000, 0xABFF1012, 0x04000015, 0x800F2800, 0xF0E40800, 0xA1FF0000, 0x90E4000F, 0x02000053, 0x8E420002, 0xA0E40807, 0x01000006, 0xAEE41002, 0x0100001E, 0xE0E40800, 0x0400005F, 0xE1040001, 0xE0AA0872, 0x807A2000, 0xF0E40800, 0x05000052, 0x80020006, 0x90E40005, 0xA0FF00D2, 0x80E40000, 0xA0FF0001, 0x02000024, 0xB0340002, 0x80E40000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 23, { 0xFFFE0101, 0x0001FFFE, 0x12345678, 0x0000001F, 0x80000003, 0xE0010000, 0x0000001F, 0x8009000A, 0x900F0002, 0x0000001F, 0x80000003, 0x90080000, 0x00000008, 0xE0040002, 0xEBF42800, 0xF0000800, 0x00000010, 0xC0030002, 0xACDB1004, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 59, { 0xFFFE0300, 0x0200001F, 0x800B0008, 0xE0220004, 0x0200001F, 0x80000000, 0xE0080003, 0x0200001F, 0x80010002, 0x90010002, 0x0200001F, 0x800D0003, 0x80040002, 0x0200002F, 0xE00F0928, 0x3F800000, 0x05000051, 0xA00F0116, 0x3F800000, 0x000000FF, 0x000000FF, 0x00000001, 0x02000007, 0xC00F0001, 0x9C000000, 0x0300004E, 0xE0012006, 0xF0E40800, 0xA0000000, 0x04000005, 0x9F082802, 0xF0E40800, 0x90490002, 0xF0000000, 0x0000002C, 0x04000018, 0xE00F0026, 0x80AE2002, 0xB05F0000, 0xFBE40000, 0x0300005F, 0xC0040000, 0xAC640000, 0x80E40120, 0x03000008, 0x910F0800, 0x80AA0001, 0x80000006, 0x03000016, 0xC0130002, 0xF1E40800, 0xF0550002, 0x01000057, 0x800F0000, 0x13000044, 0x800F0000, 0xB1551000, 0x90550001, 0x0000FFFF } },
    { 26, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x05000030, 0xF00F0000, 0x00000003, 0x00000000, 0x00000001, 0x00000000, 0x02000001, 0x800F0000, 0x90E40000, 0x0200001B, 0xF0E40800, 0xF0E40000, 0x04000002, 0x800F0000, 0x80E40000, 0xA0E42000, 0xF0E40800, 0x0000001D, 0x02000001, 0xC00F0000, 0x80E40000, 0x0000FFFF } },
    { 22, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x05000030, 0xF00F0000, 0x00000003, 0x00000000, 0x00000001, 0x00000000, 0x0200001B, 0xF0E40800, 0xF0E40000, 0x0200001B, 0xF0E40800, 0xF0E40000, 0x0000001D, 0x0000001D, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 13, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x0200002F, 0xE00F0800, 0x00000001, 0x01000028, 0xE0E40800, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 48, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0x90010001, 0x0200001F, 0x80000009, 0x8017000E, 0x0200001F, 0x80020005, 0x90270004, 0x0200001F, 0x8001000A, 0xE0080001, 0x0200001F, 0x80020000, 0xE0110003, 0x05000030, 0xF00F0000, 0x00000001, 0x3F800000, 0x00000100, 0xAED7A633, 0x05000030, 0xF00F0000, 0x00000001, 0x3F800000, 0x000000FF, 0xA8ACA0E3, 0x03000021, 0xB0121000, 0x905500FF, 0x96000075, 0x02000044, 0xE00F0003, 0xA0000000, 0x05000004, 0xE5080002, 0x9C000000, 0x80002801, 0xF0AC0800, 0xF05508D9, 0x03200005, 0xB0080001, 0x8B2E0000, 0x90E40002, 0x0000FFFF } },
    { 37, { 0xFFFE4201, 0x0200001F, 0x80020005, 0xE00B0003, 0x0200001F, 0x8000000D, 0xE00F0002, 0x0200001F, 0x98000000, 0xA0080800, 0x0200001F, 0x80010004, 0x90020002, 0x03040042, 0xB0F21005, 0x90AA0001, 0x80E40000, 0x05000012, 0xBF0F1005, 0xFCFF0800, 0x9B000000, 0xA0003018, 0xF0550800, 0x01000001, 0xA0AA0805, 0x03000020, 0x80040000, 0xB1001001, 0xAB550004, 0x03000016, 0xB0470002, 0x90000001, 0xF0AA0802, 0x02000001, 0x800F0002, 0xA0E40002, 0x0000FFFF } },
    { 37, { 0xFFFE02DF, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80010005, 0xE0110001, 0x02000002, 0xB00F0000, 0xB0001000, 0xAB000802, 0x02000001, 0xE10C0002, 0xE0E40800, 0x04000058, 0x80060005, 0xAB00004D, 0xB0AA10F4, 0xB0E41000, 0x03000008, 0xD0080000, 0xA0E40000, 0xAB001002, 0x03000009, 0xC0080001, 0xA0550000, 0xAB401000, 0x03000005, 0xE0070016, 0x90D20000, 0x80670002, 0x02000007, 0x800F0001, 0xF05500EA, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 46, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8001000A, 0x900F0003, 0x0200001F, 0x80000008, 0x90030001, 0x0200001F, 0x8000000A, 0x90230002, 0x0200001F, 0x800B0005, 0x900F0001, 0x0200002F, 0xE00F092A, 0x00000001, 0x0000001D, 0x01000057, 0xE04F0002, 0x0400005A, 0xD0030801, 0xF0E40000, 0xF1FF0801, 0x8CFF0002, 0x0000002A, 0x02000053, 0x80120000, 0xE0730800, 0x02000048, 0xC0040002, 0xADFF0001, 0x01000041, 0xB00F0002, 0x02000055, 0x80020001, 0xA0E40000, 0x03000048, 0xC0022001, 0xF0E40800, 0xBBFF1001, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 49, { 0xFFFE0111, 0x0000001F, 0x8000000B, 0x9002000B, 0x0000001F, 0x80000008, 0x90070008, 0x0000001F, 0x80000000, 0x90030003, 0x0000001F, 0x80000005, 0x90270001, 0x0000001F, 0x8002000A, 0x800F0002, 0x00000030, 0xF00F00F1, 0x00000001, 0x00000001, 0x3F800000, 0x00000000, 0x0000002D, 0x8B000000, 0xA0000000, 0xFC550803, 0x80E40001, 0x00000005, 0xABAD1000, 0xF80000E6, 0x00000020, 0xCE0F0001, 0xF1FF0000, 0x90AA0000, 0x00000025, 0xDE020002, 0xBBE41002, 0xA15500EA, 0xA36F0800, 0x00000040, 0x80240007, 0x00000047, 0x800F0000, 0x80E42001, 0x0000002A, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 50, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80030008, 0x900F1001, 0x0200001F, 0x80020009, 0x902F0003, 0x05000051, 0xA00F0001, 0xFFFFFFFF, 0x17CADA22, 0x00000000, 0x00000000, 0x0100001E, 0xA00010DC, 0x03000009, 0x800F0002, 0x9BE40001, 0xA0FF00C1, 0x0300002E, 0xE0080000, 0x90E42000, 0xB0E40001, 0x00000000, 0x03000020, 0x80490009, 0xA0000007, 0xF0E40000, 0x0200004F, 0xB02F1001, 0xACE40028, 0x02000045, 0xE1220000, 0xABE40001, 0x02000001, 0xBE1F0000, 0xA0FF0000, 0x06000004, 0x800F0001, 0xA0FF2002, 0xF0E40800, 0xA0E42000, 0xF09B0801, 0xA0E40802, 0x0200004D, 0xBE6F0001, 0x90E40000, 0x0000FFFF } },
    { 45, { 0xFFFE0201, 0x0000001F, 0x80010005, 0x90040001, 0x0200001F, 0x80010003, 0x900F0000, 0x0200001F, 0x800A0004, 0x9004000C, 0x02000046, 0xB0020803, 0xF00000D7, 0x0300000B, 0xC0270001, 0xADFF0002, 0x90E40001, 0x02000024, 0xE0130002, 0xFDE40000, 0x0200001A, 0x90E40002, 0x90E40000, 0x01000047, 0xE02F0011, 0x01000060, 0x90AA0000, 0x02000001, 0x90140000, 0xB2E41000, 0x03000014, 0xD0C20005, 0x90E40001, 0xA0E40002, 0x0000002B, 0x0500005A, 0xEA020000, 0x97F50001, 0xA0E40001, 0x80552002, 0xB0AA0001, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 60, { 0xFFFE0100, 0x0000001F, 0x8000000A, 0x90040001, 0x0000001F, 0x8000000A, 0x90070003, 0x0000001F, 0x80020000, 0xE00F0002, 0x0000001F, 0x80000009, 0x90080000, 0x0000001F, 0x80010005, 0x80080000, 0x0000002F, 0xE00F0800, 0x000000FF, 0x00000052, 0xB00F0002, 0xA1740801, 0x00000015, 0x8E010000, 0xACAA0002, 0xE1E40800, 0xABFF0001, 0x0000004E, 0x800F0014, 0x90E40000, 0x0000005B, 0xC0020000, 0xEB550802, 0x0000000F, 0xB1070002, 0xA0E41000, 0x0000002B, 0x00000008, 0xE0110002, 0x80FF0111, 0xA1E40800, 0x0000005D, 0xC0210001, 0xACAA0001, 0x80E40085, 0xA0000000, 0xA0001001, 0x0000001B, 0x80AA0001, 0x90E40074, 0x0000004E, 0xA0070801, 0x8D630000, 0x00000052, 0x8E070000, 0xA1E40001, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 42, { 0xFFFE0101, 0x0000001F, 0x8000000A, 0x800F0002, 0x0000001F, 0x8000000A, 0x900F0002, 0x0000001F, 0x80000008, 0x90040000, 0x00000001, 0xB007001E, 0x9D000007, 0x00000021, 0xB0971001, 0xFB000002, 0xA0001000, 0x0000002E, 0xD0380023, 0xADE40000, 0x00000020, 0xC0030000, 0x80230002, 0xF0FF0002, 0x00000008, 0xBE270002, 0xF0552020, 0xAEFF0000, 0x00000009, 0xC2030002, 0xA05120C5, 0x00000040, 0xB00F1022, 0x00000004, 0xBC020000, 0xA0E40002, 0xF0AA0800, 0xA0E42001, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 40, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x05000051, 0xA00F0002, 0x000000FF, 0xFFFFFFFF, 0x00000000, 0x3EBAC862, 0x03000008, 0xC0480001, 0x90AA0000, 0xF0E40804, 0x03000001, 0xC02F0001, 0xFBE42001, 0x80AA4000, 0x02030029, 0xE0E40802, 0xA0FF0001, 0x0000001D, 0x03000008, 0xD0030004, 0xF0E40800, 0xA0E40002, 0x04040025, 0xEF070806, 0xB1FF1001, 0xFCE40801, 0xE0550800, 0x02000045, 0x80110000, 0x80AA0001, 0x05020029, 0x90AA004B, 0x82FF0001, 0xA0E41000, 0x80002033, 0xB0E40000, 0x0000FFFF } },
    { 29, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8000000B, 0x8004000A, 0x05000051, 0xA00F008F, 0x00000000, 0x8CFD49EE, 0x00000100, 0x00000000, 0x05000051, 0xE00F0801, 0x00000001, 0x5A717A8D, 0x00000000, 0x2E35F0B8, 0x03000014, 0xC0420002, 0x9C000002, 0x90550001, 0x02000023, 0xD00F0001, 0xA0AA1000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 58, { 0xFFFE0101, 0x00000051, 0xA00F0001, 0xFFFFFFFF, 0x5127BEE9, 0x00000100, 0x00000100, 0x00000051, 0xA00F0001, 0x00000000, 0x3F800000, 0x00000000, 0x00000100, 0x00000001, 0xD6130012, 0xA0000002, 0x00000009, 0xC1080001, 0xFEE4000C, 0x80E40002, 0x00000009, 0xD1030000, 0xA0000001, 0xA0E40003, 0x0000005B, 0x802F0002, 0xA0AA0800, 0x00000019, 0x9DA60000, 0x00000055, 0x8F030000, 0xF6000002, 0x0000000D, 0xE0420805, 0xA0550800, 0xA0AA010B, 0x00000044, 0xBE081000, 0x90FF0004, 0x00000042, 0xEF1C0002, 0x80E4011D, 0xABAA1000, 0x0000005F, 0xC0080000, 0x80AA0104, 0xB0E41076, 0x04000019, 0x805500E9, 0x00000058, 0xC1110001, 0xA0AA0800, 0xF0AA0800, 0xE0E40802, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 27, { 0xFFFE0300, 0x0200001F, 0x8001000B, 0xE00F0000, 0x0200001F, 0x80000004, 0x901F0002, 0x0200001F, 0x80000004, 0xE0080002, 0x0200001F, 0x8001000D, 0xA0210000, 0x0200001F, 0x80020009, 0x90270003, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x00000001, 0x00000100, 0x0300005F, 0xB00F1000, 0xBDE41001, 0xACE40801, 0x0000FFFF } },
    { 41, { 0xFFFE0200, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0x90010003, 0x0200001F, 0x80000008, 0x90020002, 0x0200001F, 0x80000004, 0x90070003, 0x0200001F, 0x8000000A, 0x900F0002, 0x0200001A, 0xA0E40001, 0xB0581002, 0x0200001A, 0xA0E40002, 0x8D000002, 0x04000012, 0xCF0F0000, 0xA0FF0801, 0xB0001000, 0xA0000000, 0x0300005F, 0xD03F0001, 0xA0510000, 0xA3550006, 0x0200004D, 0xEE8F0025, 0xF1E40800, 0x03000009, 0xE0430000, 0xB0E41000, 0xA0E40001, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 35, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80020004, 0x80120000, 0x0200001F, 0x80000008, 0x90080001, 0x0200001F, 0x80020004, 0x900F0000, 0x0200001F, 0x8001000D, 0x90270000, 0x05000051, 0xA00F0000, 0xFFFFFFFF, 0x3F800000, 0x000000FF, 0x00000100, 0x03000046, 0xD0062006, 0x80E42099, 0xB0550000, 0x0300000B, 0xE00F0001, 0xA0E40004, 0xA1550126, 0x04000004, 0xE0010001, 0x90E40120, 0xE0FF0801, 0x90000003, 0x0000FFFF } },
    { 42, { 0xFFFE0300, 0x0200001F, 0x8000000B, 0x900F000C, 0x0200001F, 0x80010005, 0x900F0002, 0x05000030, 0xF0010002, 0x00000000, 0x00000001, 0xFFFFFFFF, 0x000000FF, 0x05000051, 0xA00F0002, 0x000000FF, 0x00000000, 0xFFFFFFFF, 0x000000FF, 0x01000028, 0xB0E41002, 0x14000002, 0xCD080002, 0xB0551000, 0x90E40000, 0xACE40000, 0x03000016, 0xC0EF0000, 0x81E400B2, 0xA0AA0005, 0x0000002B, 0x02000007, 0x90170807, 0xA0EA0000, 0x03000001, 0xD10A2001, 0xB0E40000, 0x90E40000, 0x02000052, 0xB1040001, 0xAC550002, 0x0000FFFF } },
    { 57, { 0xFFFE0300, 0x05000051, 0xA00F0105, 0x00000100, 0xA2846F7C, 0xFFFFFFFF, 0x3F800000, 0x04000012, 0x80280002, 0x80FF0001, 0xFBE40000, 0x82E00002, 0x0302005E, 0xE0020000, 0xFBE408E3, 0xA0000000, 0x04000012, 0x80240000, 0xAAAA0000, 0xAAE41000, 0xA0551044, 0x0300000A, 0xBE030002, 0xCBE40880, 0x9BE40001, 0x0400004A, 0xB00F0000, 0x81E40002, 0x91E40001, 0x80E40000, 0x0300000C, 0x86170002, 0xBDAA1006, 0xA0000036, 0x0300004A, 0xE1020801, 0xB0553007, 0x80001000, 0x02000055, 0xEB6F0001, 0x9BE40001, 0x02000052, 0xE3010000, 0xA0E40001, 0x01000057, 0x800A0001, 0x0300004D, 0xE00F0010, 0xADE42000, 0x80551000, 0x02000007, 0xC0430001, 0xA0550801, 0x0200004A, 0xB0031000, 0xE10D0801, 0x0000FFFF } },
    { 34, { 0xFFFE0201, 0x0200001F, 0x00010009, 0x9002000F, 0x0200001F, 0x80010003, 0x80070003, 0x0200001F, 0x80000005, 0x90110001, 0x0200001F, 0x80000009, 0x90070000, 0x0200001F, 0x80000006, 0xE0080003, 0x05000030, 0xF00F0002, 0x97D83A52, 0x00000001, 0x3F800000, 0x000000FF, 0x0000002B, 0x0300000B, 0xB0431027, 0x90000000, 0xA0AA0006, 0x0200000F, 0xB00F1000, 0xA0E40000, 0x02000052, 0xC0030002, 0xB0551125, 0x0000FFFF } },
    { 18, { 0xFFFE0200, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80010005, 0x90010001, 0x01000028, 0xE07D0802, 0x02000008, 0xA0550003, 0xA0000904, 0x0200001E, 0xC0020801, 0x90FF0002, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 47, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200002F, 0xD00F00D1, 0x000000FF, 0x0200002F, 0xE00F0802, 0x000000FF, 0x14000016, 0xF02A0802, 0xB0001000, 0xA0FF0001, 0xFDFF0800, 0x0304000A, 0x81220020, 0xF0E40801, 0xAEE40800, 0x03000021, 0xE00F0000, 0x90000001, 0x81E40005, 0x01000028, 0xA0E40006, 0x03000018, 0xD00F0004, 0xA0FF0002, 0xA0550001, 0x01000041, 0x800F0000, 0x12000019, 0xB0001000, 0xA0000000, 0x03040029, 0xA0AA2804, 0xF0E40801, 0xA1E40804, 0x03000005, 0xC01F0000, 0xA9E40000, 0xB0551001, 0x04000021, 0xB00F0001, 0x9CE40001, 0xA0AF2801, 0xA0551000, 0x0000FFFF } },
    { 17, { 0xFFFE0101, 0x0000001F, 0x80000009, 0x900F0400, 0x0000001F, 0x80000009, 0xE00F000C, 0x0000001E, 0xB0FF1002, 0x0000005E, 0xB0210000, 0x80400006, 0xFBE40804, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 21, { 0xFFFE0101, 0x00000005, 0x80040021, 0xA0E4011B, 0xACE400D3, 0x00000027, 0x00000008, 0xB00F1001, 0x80E40002, 0xA0901002, 0x00000016, 0x800F0002, 0xF0550001, 0x80E40000, 0x00000009, 0xBE220002, 0xA0FF0001, 0xA0AA00E4, 0x0000000B, 0xC00F0001, 0x0000FFFF } },
    { 25, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x90050001, 0x05000030, 0xF00F00F7, 0x00000001, 0x3F800000, 0xFFFFFFFF, 0x3F800000, 0x05000051, 0xA00F0001, 0xCB830635, 0x3F800000, 0x3F800000, 0x3F800000, 0x04000059, 0xA0030800, 0xB0551002, 0xBBF5B006, 0xD0E40800, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 32, { 0xFFFE0200, 0x0200002F, 0xE00F0800, 0xFFFFFFFF, 0x04000012, 0xC00F0000, 0xEC550807, 0xA0551004, 0xADFF1028, 0x0100001E, 0xA0E41000, 0x02000048, 0x8E070002, 0xE0AA0801, 0x02000001, 0x800E0001, 0xB0AA0800, 0x02000054, 0xC0030000, 0xA0FF0000, 0x0200005C, 0xB0210000, 0x9CE40000, 0x0400000D, 0xD00F0800, 0x8EE40005, 0xA0FF3000, 0xE0480800, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 28, { 0xFFFE0300, 0x0200001F, 0x8000000A, 0x80070002, 0x0200001F, 0x8002000D, 0x901F0003, 0x0200001F, 0x8000000A, 0x80080003, 0x0200001F, 0x80020008, 0xE0080007, 0x0200001F, 0x800B0008, 0xE0010000, 0x0200005C, 0xE0E42000, 0xF0550800, 0x03000005, 0xC0430001, 0x83FF0000, 0xE7E40913, 0x02020029, 0xFDD70802, 0xE06B0800, 0x0000002A, 0x0000FFFF } },
    { 38, { 0xFFFE0201, 0x13000001, 0xC0060002, 0xA0E42001, 0xF0550800, 0x02000010, 0x80010000, 0x9C550002, 0x0400000A, 0xB0031000, 0xD2AA2004, 0xF0E40801, 0x8BAA0000, 0x0200005B, 0x81070000, 0xA0E41003, 0x01000019, 0xA0FF0001, 0x01000028, 0xFCD90800, 0x0300000A, 0xE00C001D, 0x90550002, 0xA2E40800, 0x0300004F, 0xE0080000, 0xDDE42805, 0xF0000800, 0x02000049, 0xDF030001, 0xA1FF0000, 0x02000043, 0xB00C0801, 0xA07B0001, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 30, { 0xFFFE0300, 0x0200001F, 0x8000000D, 0x8007000C, 0x0200001F, 0x800E000A, 0x800F0002, 0x0200001F, 0x80010009, 0x900F0001, 0x0200001F, 0x8001000D, 0xE0020002, 0x0200001F, 0x8800000D, 0x9004000F, 0x05000030, 0xF00F0001, 0x00000001, 0x00000100, 0x00000000, 0x000000FF, 0x03000014, 0x800F0010, 0xADAA008D, 0x90AA0002, 0x0200004A, 0x80070002, 0xA0E40002, 0x0000FFFF } },
    { 51, { 0xFFFE0201, 0x0200001F, 0x80000005, 0x900F000E, 0x0200001F, 0x8000000D, 0xE0040001, 0x05000051, 0xA00F0001, 0xFFFFFFFF, 0x00000001, 0x00000001, 0x3F800000, 0x04000025, 0xB0031001, 0xB0FF1001, 0x90AA0002, 0xA0AA0800, 0x03000005, 0xCE070001, 0x8CE40000, 0xA0700000, 0x04000008, 0x8F080002, 0xA0E40800, 0x9CAA2005, 0xB0550000, 0x1100001D, 0xB0CE1000, 0x04000059, 0xE0040000, 0xA83A0002, 0x80E42001, 0xB0AA0001, 0x0200004E, 0x8F0F0002, 0x90E40001, 0x03000014, 0xE0010014, 0x8000010A, 0xE0E40913, 0x01000028, 0xA0AA0800, 0x03000015, 0xC0130000, 0x91F50001, 0x90FF0002, 0x02000053, 0xA0710000, 0x80E40002, 0x0000FFFF } },
    { 57, { 0xFFFE0200, 0x0200001F, 0x80000005, 0x90080003, 0x0200001F, 0x8002000E, 0x90030002, 0x0200001F, 0x80000004, 0xE00F0001, 0x0200001F, 0x8000000A, 0x800F0000, 0x05000051, 0xA00F002F, 0x00000000, 0x3F800000, 0x3F800000, 0x3F800000, 0x0200002F, 0xE00F0802, 0xFFFFFFFF, 0x04000022, 0xA00F0800, 0x80000000, 0xB0E41007, 0x80E40000, 0x03000005, 0xC0080001, 0xA0000024, 0x90050000, 0x02000006, 0xE01F0000, 0x809E0001, 0x0300004C, 0xB10F0022, 0x80E40000, 0xA0AA0001, 0x04000002, 0x81020000, 0xADFF08D2, 0xF0002802, 0xF0AA0800, 0x0000002B, 0x01000060, 0xA0550004, 0x03000021, 0xD00F0000, 0x81AA0003, 0xF0E40003, 0x02000013, 0xE0010003, 0xA0870000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 59, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0x90040000, 0x05000051, 0xA00F0002, 0xAC47EF7C, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x05000030, 0xF00F0002, 0x3F800000, 0xB6A96742, 0x000000FF, 0x000000FF, 0x05000004, 0xD00D0002, 0x9D002000, 0xB0000001, 0x90E40001, 0x90550085, 0x0304005E, 0xD0080000, 0xA0180000, 0xFCE40000, 0x04000016, 0x8E0F0000, 0xADAA2000, 0xB09E0000, 0xA1E40001, 0x0300000D, 0xC00F0000, 0x90E40000, 0xABE41129, 0x04000017, 0x800F0000, 0xEDE40800, 0x9A552002, 0xF0AA0801, 0x02000044, 0xC0020001, 0x97E41000, 0x02000053, 0x81010000, 0xF0E40088, 0x0200005C, 0xE0E20002, 0x8EFF00DE, 0x0302005E, 0x800F0016, 0xA055003E, 0x80FF0005, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 55, { 0xFFFE0300, 0x0200001F, 0x80020005, 0x900F0000, 0x05000051, 0xA00F0000, 0x000000FF, 0x00000000, 0x51BACF44, 0x00000001, 0x04000009, 0xE00F0000, 0x9CE42002, 0x80AA0000, 0x90550000, 0x01000040, 0xB0070000, 0x02000044, 0x80020002, 0x90AE0002, 0x0400000D, 0xB0010005, 0x80FF1002, 0xEB192800, 0xB0AA0001, 0x05000004, 0xB0040001, 0xB0551000, 0x80660000, 0x80550115, 0x90E40001, 0x04000005, 0x80070001, 0x9DB10000, 0x90E400CE, 0xA0E40000, 0x03020042, 0xC00F0001, 0xA0FF0803, 0x81FF0075, 0x0200005B, 0xD0020002, 0xFC000802, 0x0202002D, 0xA0BE011C, 0x90E40000, 0x0B000015, 0xB00F0001, 0xBBE41001, 0xA500004A, 0x0300000F, 0xCF082001, 0xB0E40000, 0xA0E40000, 0x0000FFFF } },
    { 54, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000004, 0xE00F0002, 0x05000051, 0xA00F0000, 0x00000000, 0x000000FF, 0x00000000, 0x3F800000, 0x04000022, 0xE0470002, 0xE1E4091F, 0x80E40001, 0xA0551001, 0x02000052, 0xB02F0024, 0xB6001000, 0x02000013, 0xB1011000, 0xB1AA102D, 0x13000005, 0xB10F1001, 0xB0B31000, 0xA1AA1001, 0x02000004, 0xB0170002, 0x80550001, 0x0000002C, 0x03000011, 0x80210013, 0x80E40001, 0x9E550000, 0x04000008, 0xB9C90001, 0xBB5D1002, 0xF0002800, 0xF04B0800, 0x1100001C, 0xB03E1000, 0x02000046, 0xC0030000, 0xA0E40904, 0x14000009, 0xE0040000, 0xB0551000, 0x90E40002, 0xFCBC0002, 0x0200004E, 0xD0030017, 0x80E40001, 0x0000FFFF } },
    { 39, { 0xFFFE0300, 0x0200001F, 0x8000000A, 0x80030000, 0x0200001F, 0x8000000B, 0x900F000F, 0x05000051, 0xA00F0002, 0xEE844BF7, 0x3F800000, 0xA6CEB792, 0xFFFFFFFF, 0x0200001A, 0xA0550006, 0xB0E41002, 0x09000060, 0xACE40001, 0x03000018, 0xD0040000, 0x8C080000, 0xA0550000, 0x00000000, 0x02000048, 0x80630001, 0x8CE40001, 0x0301005E, 0xB11F1001, 0xA1E40001, 0xE0E40800, 0x0200001A, 0x905500F5, 0x8EE40001, 0x0000002A, 0x0302005E, 0x8F030000, 0x80550004, 0xA0AA0800, 0x0000FFFF } },
    { 39, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x05000051, 0xC0080002, 0xFFFFFFFF, 0x3F800000, 0x00000001, 0x00000100, 0x03000016, 0x804A0001, 0x80FF0001, 0xF0550000, 0x02000024, 0xF1670001, 0xF0550057, 0x03000005, 0x800F0001, 0xA0500801, 0x90550000, 0x03000014, 0x90171002, 0x90050805, 0xA0AA0001, 0x0300000D, 0xD0480000, 0x90AA0001, 0xA0550800, 0x0200004A, 0xD0210804, 0xBDE41002, 0x1320001E, 0xBD551000, 0xA0F42007, 0x90E40801, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 25, { 0xFFFE0300, 0x0200001F, 0x8000000D, 0x800F0003, 0x0200001F, 0x8000000A, 0x90080002, 0x0200001F, 0xD0000000, 0xA0010806, 0x0200001F, 0x90000000, 0xA0080803, 0x01000028, 0xE0000801, 0x02000023, 0xD1A80000, 0xA7E41000, 0x0500005A, 0xC10F0001, 0x8DFF0000, 0xF0002016, 0x90550800, 0x90E400CD, 0x0000FFFF } },
    { 40, { 0xFFFE0101, 0x00000001, 0xB0001002, 0xFC000800, 0x0000000F, 0xE0030001, 0xF0CF0000, 0x00000002, 0xD0030001, 0xAAE40800, 0xFDE400A5, 0x00000017, 0x800F0007, 0xA0FF0800, 0xA0E40801, 0x00000018, 0x8F0E0000, 0xF0770002, 0x90E40006, 0x0000002E, 0x8E0F0000, 0x80550000, 0x0000000E, 0xE0080000, 0x90000004, 0x00000045, 0x80020800, 0x91000000, 0x00000014, 0xE0030010, 0xA1550001, 0xA0E40001, 0x00000009, 0x80020002, 0xB0AA10FE, 0xACA120D8, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 42, { 0xFFFE0200, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000005, 0xE0080000, 0x0200001F, 0x80000005, 0xE0040001, 0x04000008, 0xB0071015, 0x90220111, 0x91AA2000, 0xB0AA0000, 0x03010042, 0xE00A0007, 0x90550007, 0xA0FF0801, 0x03000020, 0xAC0020F7, 0xE0000000, 0xACE40001, 0x04000025, 0x8E140000, 0xA0FF1001, 0xADE40000, 0xACE41006, 0x03000018, 0x80080002, 0xF0E40002, 0xA1B80001, 0x03000008, 0xD02F0002, 0xF0FF0001, 0xA0000000, 0x02000007, 0x8FBF0000, 0x80E40002, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 41, { 0xFFFE0300, 0x0200001F, 0x8000000D, 0xA0070002, 0x0200001F, 0x800B0009, 0xE0110003, 0x0200001F, 0x80010008, 0x800F0003, 0x0200001F, 0xA0000000, 0xA0030803, 0x0200001F, 0x80000000, 0xE00F0000, 0x05000051, 0xA00F0043, 0x00000100, 0x000000FF, 0x000000FF, 0x000000FF, 0x0200002F, 0xE00F0801, 0x7FE9F5A3, 0x03000017, 0x80440000, 0x80E40000, 0x908F0000, 0x03000005, 0xD0040001, 0xA0E40000, 0xA2E40000, 0x0300000B, 0xB00F1002, 0xABFF0000, 0x90550001, 0x02000011, 0xABE40805, 0xBCAA1000, 0x0000FFFF } },
    { 28, { 0xFFFE0300, 0x0200001F, 0x80020009, 0x800C1002, 0x0200001F, 0x80000004, 0x901F0001, 0x0200001F, 0x8000000A, 0x80040003, 0x0200001F, 0x8000000A, 0x90070001, 0x05000051, 0xA00F0000, 0x00000001, 0x3F800000, 0xFFFFFFFF, 0xFA7E605F, 0x0204002D, 0xA0E40800, 0x81550026, 0x04000021, 0xB00F1000, 0xE0E42802, 0xB0AA0000, 0xBD001000, 0x0000FFFF } },
    { 45, { 0xFFFE0101, 0x0000001F, 0x80000009, 0x800F000F, 0x0000001F, 0x800A0000, 0x80040002, 0x0000001F, 0x8001000A, 0xE002000C, 0x0000001F, 0x800A0004, 0xE0070000, 0x0000002F, 0xE00F0802, 0xFFFFFFFF, 0x00000017, 0x800F0000, 0x8F8F2000, 0xA0130801, 0x0000001C, 0x00000009, 0xEF030002, 0xA0E40898, 0xE0AA086E, 0x0000001A, 0xA3550000, 0xBC991000, 0x0000005D, 0x80050004, 0xB0FF105F, 0xACE40003, 0xA0250001, 0x80000002, 0x00000050, 0xC0010002, 0xADFF0000, 0x00000027, 0x00000001, 0xB0010027, 0xF0AA0000, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 16, { 0xFFFE0300, 0x0200001F, 0x8003000D, 0x900F0000, 0x0200002F, 0xE00F0802, 0x00000100, 0x04000020, 0x802F0000, 0xA0000007, 0x80E42000, 0x90000801, 0x02000013, 0x8F0F0003, 0xB5001007, 0x0000FFFF } },
    { 50, { 0xFFFE0200, 0x0001FFFE, 0x12345678, 0x05000051, 0xA00F0000, 0x095B576B, 0xE8A723E9, 0x00000100, 0x3F800000, 0x05000030, 0xF00F0001, 0x3F800000, 0xFFFFFFFF, 0x00000001, 0x000000FF, 0x02000024, 0xB0020000, 0xB0000000, 0x07000009, 0xB0170001, 0xA0550003, 0xA0000000, 0x03000048, 0xC0020001, 0xF1E42800, 0xF0720800, 0x04000012, 0xB00D0000, 0x800000CA, 0x8DE40000, 0x90550800, 0x02000024, 0x800F0000, 0xA0AA0004, 0x04000059, 0xAF121000, 0x8DE41000, 0xA0E42805, 0xF0C10800, 0x00000000, 0x02000056, 0x80031001, 0x9D550045, 0x02000001, 0xB0020000, 0x9DFF00CA, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 36, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000007, 0x90070003, 0x0200001F, 0x80010009, 0xC0120001, 0x0200001F, 0x80000004, 0xD0070003, 0x05000051, 0xA00F0001, 0x3F800000, 0x58B57C46, 0x41047EBE, 0x3F800000, 0x05000051, 0xA00300DC, 0x3F800000, 0x000000FF, 0x00000000, 0x00000001, 0x02000025, 0xC0280002, 0xA0000002, 0x03000042, 0xC0040002, 0x9C4E0000, 0xB0AA1001, 0x0300000E, 0xC00F0002, 0x81FF2000, 0xE0000001, 0x0000FFFF } },
    { 7, { 0xFFFE03FF, 0x0001FFFE, 0x12345678, 0x02000024, 0x80070000, 0x80AA0000, 0x0000FFFF } },
    { 30, { 0xFFFE0300, 0x05000051, 0x90071001, 0x00000000, 0xFFFFFFFF, 0x00000100, 0x00000100, 0x05000030, 0xF00F0000, 0x000000FF, 0x000000FF, 0x00000000, 0x00000100, 0x01000060, 0xBB101000, 0x0600005D, 0xD0040003, 0xB0001000, 0xF0E40000, 0xF000089C, 0xE1E40851, 0x914E0098, 0x03000026, 0xF00008D8, 0x04000025, 0xC0180000, 0x90FF0002, 0xF11E0002, 0x90BB0001, 0x0000FFFF } },
    { 43, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x800E0003, 0x90040002, 0x0200002F, 0xE0030802, 0x00000100, 0x05000051, 0xA00F0002, 0x00000001, 0x000000FF, 0x000000FF, 0x00000100, 0x0300000A, 0xB0470013, 0x90AA0000, 0xF0E42849, 0x01000041, 0xD0110002, 0x0200005B, 0x801F0009, 0x81B700F6, 0x0000002C, 0x1506005E, 0x90011000, 0xB0E41000, 0x8DE42001, 0xF0B60800, 0xE1FF0800, 0x01000026, 0xA0E41001, 0x0200000F, 0x89070000, 0x90000000, 0x0000001D, 0x0000002A, 0x0300005C, 0xB00F1001, 0x90F52004, 0xA0AA0800, 0x0000FFFF } },
    { 30, { 0xFFFE0101, 0x0000001F, 0x8000000B, 0x900F0004, 0x0000001F, 0x80010000, 0x90030000, 0x00000051, 0xA00F0001, 0x00000001, 0x3F800000, 0x00000100, 0x00000100, 0x00000051, 0xA00F0000, 0xB64C8961, 0xFFFFFFFF, 0x00000001, 0x00000001, 0x00000001, 0xC0020000, 0xFCFF0801, 0x00000012, 0x80041023, 0x80E40001, 0xA0550000, 0xA0550006, 0x0000001C, 0x0000001D, 0x0000FFFF } },
    { 16, { 0xFFFE0300, 0x0200001F, 0x800B000B, 0xE00F2001, 0x0200001A, 0x90170000, 0xA0AA10A0, 0x03000014, 0x8CAA2000, 0xF0E40801, 0xA0551036, 0x03000014, 0xC0240000, 0x80550001, 0xABE40107, 0x0000FFFF } },
    { 19, { 0xFFFE02FF, 0x0200002F, 0xE00F0801, 0x00000100, 0x0300000B, 0xC0410002, 0x90550002, 0xA0E41000, 0x01000019, 0xA0E4109D, 0x0000001C, 0x02000048, 0xCE020001, 0xFC000919, 0x13000001, 0x80070005, 0xB0891000, 0x90E40000, 0x0000FFFF } },
    { 23, { 0xFFFE03FF, 0x0001FFFE, 0x12345678, 0x05000051, 0xA00F0000, 0x00000000, 0x627B9A8C, 0x3F800000, 0x00000001, 0x0200002F, 0xE00F0802, 0x00000001, 0x0000001C, 0x04000022, 0xC0FF0002, 0x80000002, 0x8DAA0002, 0xA0E40002, 0x03000008, 0xB0041004, 0xAB5500CE, 0xA0AA00BA, 0x0000FFFF } },
    { 27, { 0xFFFE02FF, 0x0200001F, 0x8001000A, 0x80080009, 0x0200001F, 0x80000008, 0x900F0003, 0x05000051, 0xA0030001, 0x46BF773F, 0x3F800000, 0xA4C42A3D, 0x000000FF, 0x14000020, 0x80030001, 0xB0001000, 0xA1001000, 0xA05F0007, 0x04000025, 0x900F0801, 0xFBE40001, 0xA0550000, 0xA0360000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 50, { 0xFFFE02FF, 0x05000030, 0x900F0000, 0x00000100, 0x00000100, 0x00000001, 0xFFFFFFFF, 0x05000012, 0x80010000, 0xA0AA2035, 0xB0550000, 0xB0E41000, 0x81AA0000, 0x02000055, 0xE01B0000, 0xB1001002, 0x1300000E, 0x80010000, 0xB0E41000, 0xBC00089C, 0x03000005, 0xB0071000, 0xACAA0000, 0xA0550802, 0x03000008, 0xE0020000, 0xA0E41000, 0xA12B0000, 0x12000040, 0xB0431000, 0xB0171000, 0x01000040, 0x8D2F0019, 0x01000041, 0xE00F0008, 0x1300000E, 0xCF0A0000, 0xB0001000, 0xB0E41001, 0x05000025, 0xDE070008, 0xBC951001, 0xB05A1000, 0x80552051, 0xB0000000, 0x00000027, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 9, { 0xFFFE0200, 0x0200001F, 0x80000000, 0x900F0000, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000002A, 0x0000FFFF } },
    { 30, { 0xFFFE0101, 0x0001FFFE, 0x12345678, 0x0000001F, 0x80010005, 0x900F0001, 0x0000001F, 0x8009000A, 0x90040001, 0x0000002F, 0xE00F0801, 0x00000000, 0x00000051, 0xA00600A2, 0x00000001, 0x00000001, 0x000000FF, 0xFFFFFFFF, 0x0000000B, 0xEF270002, 0xA0AA0001, 0xA0550001, 0x00000008, 0xCE020001, 0xA0AA0002, 0xA0000002, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 22, { 0xFFFE23FF, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8002000A, 0x80010000, 0x05000051, 0xA0080000, 0xFFFFFFFF, 0xB377DE8A, 0x00000100, 0x00000001, 0x05000030, 0xF00F0002, 0x20A93480, 0xB570FC4A, 0x00000100, 0x00000000, 0x0200004D, 0xE10F0001, 0x9BF70001, 0x0000FFFF } },
    { 13, { 0xFFFE0300, 0x0200001F, 0x80020009, 0x800F0003, 0x0200001F, 0x80030009, 0xE0030000, 0x0000001C, 0x0301005E, 0x80030001, 0x97551001, 0xADE40802, 0x0000FFFF } },
    { 44, { 0xFFFE0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x800C0009, 0x90070000, 0x0200001F, 0x90000000, 0xA00F0802, 0x0200001F, 0x8000000B, 0xE02F0003, 0x0200001F, 0x98000000, 0xA01A0802, 0x0200001F, 0x8000000B, 0x80070001, 0x05000051, 0xB00F1000, 0x3F800000, 0x00000100, 0xFFFFFFFF, 0xFFFFFFFF, 0x05000051, 0xA00F1002, 0x00000000, 0x00000100, 0x000000FF, 0x00000000, 0x01000040, 0x802F0005, 0x03000017, 0xBF041019, 0xF0E40800, 0xADFF0000, 0x0300000B, 0xD0020002, 0xA1551002, 0xE100004F, 0x02000048, 0xC0030000, 0xF0550000, 0x0000FFFF } },
    { 43, { 0xFFFE0101, 0x0001FFFE, 0x12345678, 0x0000001F, 0x8000000D, 0x800F0000, 0x0000001F, 0x80000000, 0x90020003, 0x0000001F, 0x8000000A, 0x800F0000, 0x00000013, 0xE00F0000, 0x90FF0001, 0x0000004C, 0xC00F0001, 0xAB961000, 0xE0E40800, 0x00000021, 0xE0040000, 0x91FF20D2, 0xA0AA00DD, 0x0000002A, 0x00000008, 0x8F2F0001, 0xA0E42000, 0xA1E40001, 0x0000004C, 0xA9411027, 0x90AA0004, 0x00000010, 0x8F280001, 0xF0550917, 0x00000025, 0xE1220000, 0xAF551000, 0x9DFE0002, 0x81400001, 0x00000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    { 44, { 0xFFFE0300, 0x0200002F, 0xE00B0880, 0x00000001, 0x03000014, 0xB0481002, 0xF0AA0800, 0xA0AA00FD, 0x0300000A, 0x8F170001, 0xA0971000, 0xA1FF0000, 0x05000049, 0x80220000, 0x90E40000, 0xE0E40800, 0xB0E4107A, 0x80E40002, 0x04000004, 0x802F0001, 0x80EB0000, 0x90550021, 0x90E400DB, 0x04000005, 0x804F2000, 0xF0E40800, 0xF00000DE, 0xA1550002, 0x0200001A, 0xB1E41000, 0xA0FF0000, 0x02000054, 0x800B0000, 0xA0AE0000, 0x02000045, 0xBE031002, 0xA0550800, 0x1500000D, 0xC0012000, 0xF0E40800, 0xB0E41000, 0x90FF0000, 0xA0E40000, 0x0000FFFF } },
    { 29, { 0xFFFE0300, 0x0200001F, 0x80000005, 0xE00F0001, 0x0200001F, 0xA0000000, 0xA0220807, 0x0200001F, 0x8000000A, 0xE00F0001, 0x0200001F, 0x80000000, 0x90010000, 0x05000051, 0xA0030001, 0xFFFFFFFF, 0xF221CE4C, 0xFFFFFFFF, 0x00000000, 0x05000051, 0xA00F0050, 0x00000001, 0x00000100, 0xFFFFFFFF, 0x00000000, 0x02030029, 0xABE40802, 0xE01B0800, 0x0000FFFF } },
    { 49, { 0xFFFE0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8000000B, 0xE00F0000, 0x0200001F, 0x8000000B, 0x80010000, 0x0200001F, 0x80020009, 0xE01F0002, 0x0200001F, 0x80020000, 0x900F0000, 0x05000051, 0xA00F0053, 0x3F800000, 0x00000100, 0x00000001, 0x3F800000, 0x05000051, 0xA00F0002, 0x000000FF, 0x000000FF, 0x00000000, 0xD0EAF3FB, 0x03000021, 0x80020000, 0xB0001000, 0x80E40000, 0x05000025, 0x800F0001, 0xA0E41002, 0x805500F9, 0xEDFF2802, 0xF0000801, 0x0200002E, 0xF0230802, 0x81FF0007, 0x0304005E, 0xD00F0001, 0x80000000, 0xA0AA0002, 0x00000027, 0x02000001, 0xC00F0000, 0x90E40000, 0x0000FFFF } },
    /* Pixel shaders; streams that crash the system validator or make it read past its own object are left out */
    { 5, { 0xFFFF0000, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 31, { 0xFFFE0001, 0x00000051, 0xA01F0007, 0x5B363E34, 0x8AA9D742, 0x9EF424E9, 0xF595D5B6, 0x00000054, 0xB00A0003, 0xB0E40005, 0x00000002, 0x831F0001, 0x80E40001, 0x95F40000, 0x00000050, 0x8D080000, 0x84E40002, 0x90E41002, 0xA1E40003, 0x40000003, 0x800F0001, 0x80E40000, 0x90FD0000, 0x00000008, 0xB00F0000, 0xB0550001, 0x90F40000, 0x00000048, 0xB00F0000, 0xB7FF0002, 0x0000FFFF } },
    { 5, { 0xFFFF0100, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 24, { 0xFFFE0100, 0x00000051, 0xA00F0002, 0x368DC228, 0x0BE50857, 0xBBE4B2DC, 0x72AF0420, 0x00000012, 0x8E070000, 0xF6000801, 0xB0550000, 0xB0FF0003, 0x40000005, 0x80080000, 0x80000000, 0x80FF0000, 0x00000002, 0x8F030000, 0xF2A40001, 0x90FF0002, 0x00000001, 0x800F0000, 0xA0E40001, 0x0000FFFF } },
    { 6, { 0xFFFF0101, 0x00000000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 10, { 0xFFFE0101, 0x00000024, 0x800F0001, 0x90E40000, 0x83FF0002, 0x80540001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 11, { 0xFFFF0101, 0x00000001, 0x800F0000, 0xA0E40000, 0x00000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x0000FFFF } },
    { 11, { 0xFFFE0101, 0x00000051, 0xA00F0000, 0xD79D882E, 0xCCD63432, 0x125FE4C4, 0x0EAA65AB, 0x00000056, 0x800F0003, 0xA4A40001, 0x0000FFFF } },
    { 16, { 0xFFFE0101, 0x00000051, 0x800F1001, 0xE5AC2D60, 0xCC5AE262, 0xF9A245DB, 0xACE4D44E, 0x00000041, 0xA00F0001, 0x0000004D, 0xB00F0006, 0xB0E40000, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 18, { 0xFFFF0101, 0x00000003, 0x8F0E0000, 0x80AA0003, 0x90FF0002, 0x40000004, 0x8D070000, 0x8EA40000, 0xA55B0000, 0xA0B40000, 0x40000009, 0x8D080002, 0xA0A40517, 0xA0F40001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 19, { 0xFFFF0101, 0x00000051, 0xA00F0000, 0xC108EE83, 0x4CE6B0C2, 0x76743FC9, 0x53B86867, 0x00000051, 0xA00F0002, 0x494BCA66, 0x2145781D, 0x7C5484E3, 0x0836DC1B, 0x00000041, 0xB00F0003, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 20, { 0xFFFF0101, 0x0000004C, 0xB00F0000, 0xB4FF0002, 0x80E40002, 0x00000004, 0x80BF0001, 0xA0A40000, 0xB8A40000, 0x81A40000, 0x00000050, 0x81070003, 0x80D60000, 0xA7E40002, 0xA8C60000, 0x40000008, 0x8F080002, 0x98B70001, 0x8AE40000, 0x0000FFFF } },
    { 22, { 0xFFFF0101, 0x00000044, 0xB00F0000, 0xB0FF0001, 0x00000044, 0xA00F0003, 0xB4E40001, 0x00000047, 0xB00F0001, 0xB4E40003, 0x00000048, 0xB00F0002, 0xB0E40000, 0x00000004, 0x80070003, 0xA2B40000, 0xA4E40001, 0x97000002, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 24, { 0xFFFE0101, 0x00000000, 0x00000001, 0xB00F0003, 0xB0F40003, 0x0000000B, 0x80080000, 0xB0E40001, 0xA0E40001, 0x97E40000, 0x40000002, 0x80020001, 0xB7550001, 0x844706D2, 0x00000053, 0xB2170003, 0xA0E40000, 0xA2E40001, 0x90B40006, 0x86F40001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 26, { 0xFFFF0101, 0x0001FFFE, 0x12345678, 0x00000004, 0x80070003, 0x82F40002, 0x85A40001, 0x96550000, 0x00000002, 0x800F0000, 0x80E40003, 0x98AA0000, 0x00000003, 0x80070002, 0x83F40001, 0x80AA0000, 0x00000012, 0x80030001, 0x80E40000, 0x84E40001, 0xB3E50001, 0x40000000, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 31, { 0xFFFF0101, 0x0001FFFE, 0x12345678, 0x00000049, 0x800F0002, 0xB0260000, 0x00000004, 0x80080005, 0xA0E40002, 0xA0E40001, 0xB0FF0000, 0x00000005, 0x801F0000, 0x82FF0001, 0x80E40001, 0x00000003, 0x820F0003, 0x85E40000, 0x80A40001, 0x00000013, 0x83070002, 0x84E40002, 0x84E40000, 0x40000009, 0x80180000, 0xA0F40000, 0x80A40001, 0x00000001, 0x800F0000, 0x90E40001, 0x0000FFFF } },
    { 31, { 0xFFFE0101, 0x00000051, 0xB00F0007, 0x6DEEA41C, 0xA47EA5E2, 0xB24132D2, 0x51894AFF, 0x00000055, 0xB00F0004, 0xB0E40000, 0x00000047, 0xB00F0001, 0xB0E40001, 0x00000048, 0xB00F0002, 0xB0E40001, 0x00000043, 0xB00F0003, 0xB0F40001, 0x00000009, 0x80031003, 0x80A40003, 0xD5AA0002, 0x00000009, 0x80070001, 0x84E40003, 0xB0F40001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 32, { 0xFFFE0101, 0x00000009, 0x80070001, 0xA0E40000, 0xA9550003, 0x40000050, 0x80080001, 0x80FF0000, 0x80E40002, 0x80A40001, 0x00000002, 0x8F0F0001, 0xA4E40002, 0x80E40000, 0x00000005, 0x80670003, 0x80000003, 0xB0FF0003, 0x00000007, 0x80070000, 0x80B90002, 0x97A40001, 0x80A40001, 0x00000050, 0xA0180000, 0xD1FF0000, 0xC1000001, 0x90E40001, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 33, { 0xFFFF0101, 0x00000051, 0xA00F0007, 0x02F53E2E, 0x0EBCBFE8, 0x237634F4, 0x2455F455, 0x00000051, 0xA00F0002, 0xB919713E, 0x9CF6FA0D, 0x69912A72, 0xC8C6321E, 0x00000056, 0xB0040000, 0xB9A40000, 0x00000008, 0xA00F0001, 0xB1E40000, 0xB0FF0001, 0x40000002, 0x80180002, 0xB0550003, 0xBAF40002, 0x00000008, 0x800F0000, 0xB0000000, 0x8A000003, 0x00000059, 0x800F0002, 0x90E40001, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0101, 0x00000040, 0xB0030000, 0x40000049, 0xB00F0003, 0xB0E40001, 0x00000049, 0xB00F0002, 0xB4E40001, 0x00000049, 0xB00F0003, 0xB0E40001, 0x0000004C, 0xB00F0001, 0xB0E40001, 0xA0E40001, 0x00000005, 0x82170003, 0x80A40001, 0xA0E40001, 0x00000005, 0x80080001, 0x80F40001, 0xA8E40002, 0x00000012, 0xB01F0001, 0x84E40002, 0xA6F40003, 0xB0E40000, 0x00000008, 0x800F0001, 0x90720002, 0xA0000001, 0x40000012, 0xB01F0000, 0x89610001, 0xB7A40001, 0xA2660000, 0x0000FFFF } },
    { 39, { 0xFFFE0101, 0x00000009, 0x90080005, 0x80E40001, 0x88E40002, 0x00000008, 0x801F0001, 0x80F40003, 0xB3E40001, 0x40000001, 0x801F0000, 0x80A40001, 0x00000058, 0xE0080001, 0xA45B0001, 0xB0A40001, 0x80AA0000, 0x00000058, 0x80080001, 0x80E40001, 0xA7910001, 0xB7FF0000, 0x00000050, 0xD00F0801, 0x80FF0000, 0x8BE41003, 0xA3550001, 0x00000008, 0x82080002, 0xA0000002, 0x80AA0001, 0x00000002, 0xB00F0001, 0x80A40001, 0x86E40001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 45, { 0xFFFF0101, 0x00000051, 0xA00F0007, 0x06799B91, 0x566071CA, 0xC1FD0621, 0x02B72ABF, 0x00000051, 0xA00F000B, 0x2A1FCCE4, 0xB260DAD9, 0x9BE00A0A, 0x0A2A9057, 0x40000044, 0xB0070001, 0x80E40000, 0x00000044, 0xB0050002, 0x80E40000, 0x00000008, 0x830F0000, 0x80AA0005, 0xA2E40002, 0x00000004, 0x800F0001, 0xB0000002, 0xB0F40000, 0xB0AA0001, 0x00000004, 0xB1070001, 0x80F40000, 0xB0B40002, 0x80AA0002, 0x40000001, 0xA0070000, 0x94E40001, 0x40000050, 0x80080001, 0xA8E40002, 0xB0D00000, 0xA0AA0001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 54, { 0xFFFF0101, 0x00000053, 0xA0070001, 0xB9FF0000, 0x00000047, 0xB00F0001, 0xB0E40001, 0x00000048, 0xB00F0002, 0xB0E40001, 0x0000004D, 0xB00F0001, 0xB0E40000, 0x00000049, 0xB00F0002, 0xB0E40000, 0x00000049, 0xB00F0003, 0xB0E40000, 0x00000009, 0xBD030000, 0x80E40004, 0x88AA0001, 0x40000000, 0x40000008, 0x83080000, 0xB3A40006, 0xB0F41001, 0x00000059, 0x8F080003, 0x81F40001, 0x80E40001, 0x40000003, 0x8F0F0001, 0x90E40002, 0x97FF0001, 0x40000002, 0xB0080000, 0xB0000000, 0xAAA40002, 0x805B0003, 0x89000003, 0x00000005, 0xC00A0000, 0x89F40002, 0xB0A40002, 0x00000008, 0x80080000, 0x88E40003, 0x90E40003, 0x00000001, 0x800F0000, 0xA0E40001, 0x0000FFFF } },
    { 59, { 0xFFFF0101, 0x0000004C, 0xA00F0002, 0x80E40000, 0xBE560000, 0x0000004C, 0xB0030000, 0xB0F40003, 0xB9E40003, 0x00000040, 0xA00F0000, 0x00000049, 0xB0030000, 0xB0A40002, 0x00000042, 0xB00F0005, 0x00000008, 0xB0030003, 0x89AA0001, 0xB0E40002, 0x00000003, 0x80070003, 0x88FF0000, 0xA0B40003, 0x00000058, 0x800F0001, 0xA1E40000, 0x80E40001, 0xA7F4024E, 0x00000059, 0x80030001, 0xA3E40000, 0x95F40003, 0x00000045, 0x90180000, 0x80A40000, 0x00000008, 0xBBC70003, 0x80F40003, 0xB0F40000, 0x00000008, 0x800F0003, 0x80460001, 0x8AFF0002, 0x00000000, 0x00000012, 0x8F080000, 0x87E40002, 0x82000001, 0x8AE40001, 0x00000012, 0xB0080000, 0x89E40002, 0x85FF0000, 0xA8F40000, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 8, { 0xFFFF0102, 0x00000044, 0xB00F0001, 0xB0E40000, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 11, { 0xFFFF0102, 0x00000044, 0xB00F0000, 0xBBE40001, 0x00000045, 0x800F0001, 0xB4E40001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 21, { 0xFFFF0102, 0x00000047, 0xB00F0001, 0xB0E40000, 0x00000048, 0xB00F0002, 0xB0E40000, 0x00000044, 0xB00F0000, 0xB4A40000, 0x00000045, 0xB0070002, 0xB0E40002, 0x0000001E, 0x8E1F0003, 0x80A40000, 0x80E40000, 0x00000001, 0x800F0000, 0x90E40001, 0x0000FFFF } },
    { 22, { 0xFFFF0102, 0x00000009, 0x800F0000, 0xA0E40000, 0xA0E40001, 0x00000009, 0x800F0000, 0xA0E40000, 0xA0E40001, 0x00000009, 0x800F0000, 0xA0E40000, 0xA0E40001, 0x00000009, 0x800F0000, 0xA0E40000, 0xA0E40001, 0x00000009, 0x800F0000, 0xA0E40000, 0xA0E40001, 0x0000FFFF } },
    { 25, { 0xFFFF0102, 0x0001FFFE, 0x12345678, 0x00000051, 0xA00F0000, 0x99B43870, 0xBA716CE4, 0xD8C9C325, 0x93AF9135, 0x00000051, 0xA00F0000, 0x373F4D5C, 0x5C778424, 0x9A9827E1, 0x1F3202D1, 0x00000051, 0xA0180003, 0x8C344687, 0xA78767DD, 0x3C26AEF3, 0xEC9D20D9, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 28, { 0xFFFF0102, 0x00000051, 0xA00F0000, 0x9E75DFA6, 0xF6576AA7, 0xB5EEB0A0, 0xEC328F72, 0x00000051, 0xA00F0008, 0x3ADC99B6, 0xD3AB2266, 0xFE578CBF, 0xFF4D8B24, 0x00000051, 0xA00F0000, 0x299AEF86, 0x58634B36, 0x1FF8919B, 0x7C652171, 0x00000050, 0xAD080003, 0x80FF0000, 0x80E40002, 0x87E40002, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 28, { 0xFFFF0102, 0x00000045, 0xB00F0001, 0xBE710002, 0x00000043, 0xA00F0000, 0xB0E40000, 0x80000004, 0x800F0000, 0xA7FF0000, 0x80E40000, 0xA6FF0000, 0x00000005, 0x80070001, 0xA06B0001, 0xA9A40001, 0x00000046, 0xA2070000, 0x87FF0001, 0x40000012, 0xB0080001, 0xB8E40000, 0xB0E40001, 0xBAE40001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 33, { 0xFFFF0102, 0x00000047, 0xA01F0002, 0xB0E40005, 0x00000049, 0xB00F0001, 0xB0E40000, 0x00000049, 0xB00F0002, 0xB0E40000, 0x0000004A, 0xB00F0001, 0xB0E40000, 0x00000058, 0x80EF0002, 0xA3B40001, 0x90B40003, 0x98F40002, 0x00000008, 0xB0170005, 0x80E40000, 0x95E40000, 0x40000004, 0xB0080000, 0x90970003, 0x93F40001, 0x92FF0005, 0x00000012, 0x900F0000, 0x80A40000, 0x98B40001, 0x812B0003, 0x0000FFFF } },
    { 34, { 0xFFFF0102, 0x00000004, 0x801F0001, 0x80B40001, 0x80A40003, 0x9EE40000, 0x00000058, 0x8E070003, 0x81E40000, 0x82B40000, 0x86E40002, 0x00000058, 0x8E080003, 0x87A40000, 0x80E40003, 0xB1000002, 0x00000008, 0x8D870001, 0x80FF0003, 0x90A40003, 0x00000058, 0x8F0F0003, 0x88E41000, 0xA0AA0002, 0x80A40000, 0x00000058, 0xBD080003, 0xA04B0002, 0x88E40003, 0x80A40002, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0102, 0x00000053, 0xA0030000, 0xB0E40005, 0x00000005, 0xB0080001, 0xA0FF0001, 0xAAB40002, 0x00000050, 0x801F0002, 0x80FF0000, 0x80F40002, 0xB5E40000, 0x00000004, 0xB20F0000, 0xA0FF0000, 0xA8B40000, 0x98E40000, 0x00000008, 0x80070002, 0xB2E40000, 0x83AA0000, 0x40000008, 0xA0080000, 0xA0C20003, 0x80E40003, 0x40000009, 0x82080000, 0xB8E40000, 0xA2E40001, 0x00000000, 0x40000059, 0xA0180000, 0x92F40002, 0x84A40001, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 41, { 0xFFFF0102, 0x00000054, 0xB00F0002, 0xB4E40002, 0x00000012, 0x80740002, 0x85F40000, 0x80AA0004, 0x81AA0001, 0x00000004, 0x83030000, 0xA6000001, 0xA0E40000, 0xA5BA0002, 0x00000005, 0x81070003, 0x96EB0003, 0x80A40009, 0x00000058, 0x800F0000, 0x87980000, 0x84E40001, 0xA1F40000, 0x0000000F, 0x80070000, 0x00000004, 0xB0070002, 0xF0AA0001, 0xB0E40002, 0x82E40000, 0x0000004D, 0x800F0001, 0xA0E40002, 0x00000059, 0xB0080000, 0xA6A40000, 0x80E40000, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 42, { 0xFFFF0102, 0x00000042, 0xB00F0001, 0x00000049, 0xB00F0001, 0xB0E40001, 0x00000049, 0xB00F0002, 0xB0E40001, 0x0000004D, 0xB00F0003, 0xB0E40001, 0x00000047, 0x800F0000, 0x90B40001, 0x00000003, 0x80080001, 0xA0A40002, 0x90E40000, 0x00000008, 0x800F0000, 0xB0F40000, 0x86FF0001, 0x00000004, 0x83170001, 0x80FF0003, 0x80F40001, 0x9AA40001, 0x40000002, 0x90180000, 0x80550002, 0x80F40000, 0x00000000, 0x00800009, 0x8E030001, 0xB0F40000, 0x80FF0003, 0x00000059, 0x9F0F0000, 0x80FF0001, 0x84FF4000, 0x0000FFFF } },
    { 13, { 0xFFFF0103, 0x00000051, 0xA00F0001, 0xA3D785F4, 0xF0606A32, 0x648F9ABC, 0xCE3841A1, 0x00000040, 0xB00F0000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 16, { 0xFFFF0103, 0x00000052, 0x800F0003, 0xB4E40001, 0x0000001F, 0xAD180001, 0x89A40000, 0xA0E40001, 0x00000005, 0x8E0F038B, 0x82E40000, 0x80000003, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 18, { 0xFFFF0103, 0x00000043, 0xB10F00C9, 0xB9F40000, 0x00000000, 0x0000004C, 0xBE090000, 0xA0E40004, 0xA7E40000, 0x40000058, 0x80080701, 0xA0B40005, 0xAA550001, 0xA0E40001, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 25, { 0xFFFF0103, 0x40000056, 0xB00F0000, 0xB0E40000, 0x00000005, 0xA00F0001, 0x80FF0000, 0xB0AA0000, 0x40000058, 0xBF380001, 0x90FF0002, 0xA0D50002, 0xB9AA0802, 0x00000044, 0xB00F0000, 0xA0E40001, 0x0000004E, 0x820F0001, 0xB2E40001, 0xB0AA0002, 0x80A40005, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 30, { 0xFFFF0103, 0x00000051, 0xA02F0000, 0x51840862, 0x511D0427, 0xFD1475F8, 0xC9EF861C, 0x00000045, 0xB00F0001, 0xB4E40003, 0x00000005, 0x800F0004, 0x90E40002, 0xA0F40000, 0x00000005, 0x80080000, 0x90550001, 0x80F40002, 0x40000009, 0x8F0F0000, 0xA0E40000, 0xA0E40001, 0x00000059, 0x800F0002, 0x8AE400C4, 0xA8AA0003, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 62, { 0xFFFF0103, 0x00000054, 0xB00F0000, 0xB0F40294, 0x00000055, 0xA10F0000, 0xB0E40001, 0x00000042, 0x810F0002, 0x0000004A, 0x800F0001, 0xB0E40001, 0x00000056, 0xB00F0000, 0xB0A40000, 0x00000050, 0x91070000, 0x80FF0000, 0xA0E40001, 0xA8FF0000, 0x00000008, 0x800F0000, 0x88FF0001, 0x90A40001, 0x00000050, 0x80030001, 0x80FF0000, 0xBA000001, 0xB0E40002, 0x40000058, 0xB0030003, 0x80E40001, 0xA0E40002, 0x88F40000, 0x00000001, 0x83070001, 0x8AF40002, 0x40000058, 0x90080001, 0x80E40001, 0x84550800, 0x81E40001, 0x00000050, 0x80080000, 0x80A40000, 0x80FF0003, 0xA0E40002, 0x00000008, 0x800F0000, 0xB0E40000, 0xB8550001, 0x00000001, 0xB0180000, 0xA0B40001, 0x40000008, 0x800F0007, 0xB9A40003, 0x82FF0000, 0x00000001, 0x800F0000, 0xA0E40001, 0x0000FFFF } },
    { 5, { 0xFFFF0104, 0x40000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 6, { 0xFFFF0104, 0x0000FFFD, 0x40000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 8, { 0xFFFF0104, 0x0001FFFE, 0x12345678, 0x00000000, 0x00000001, 0x800F0000, 0x90E40001, 0x0000FFFF } },
    { 11, { 0xFFFF0104, 0x00000001, 0x800F0000, 0xA0E40000, 0x40000051, 0xA00F0001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0000FFFF } },
    { 12, { 0xFFFF0104, 0x00000042, 0x800F0002, 0xA0E40003, 0x0010005B, 0x80030006, 0xA0F40003, 0xA6E40000, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 14, { 0xFFFF0104, 0x00000040, 0x80070000, 0xB0E40000, 0x00000040, 0x80030000, 0xBAF40001, 0x00000001, 0x800F0001, 0x80E40000, 0x00000001, 0x80080000, 0xA0E40000, 0x0000FFFF } },
    { 14, { 0xFFFF0104, 0x00000051, 0xA00F0006, 0xD6347BA7, 0x4AA5DD72, 0x26F280CF, 0xAAE8BBAD, 0x00000042, 0x800F0001, 0xB0F40001, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 15, { 0xFFFF0104, 0x00000040, 0x80070000, 0xB0E40000, 0x00000001, 0x80080000, 0xA0E40000, 0x0000FFFD, 0x00000040, 0x80030000, 0xBAF40001, 0x00000001, 0x800F0001, 0x80E40000, 0x0000FFFF } },
    { 17, { 0xFFFF0104, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFD, 0x00000009, 0x800F0001, 0xA0E40000, 0xA0E40001, 0x40000009, 0x80080002, 0xA0E40000, 0xA0E40001, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 19, { 0xFFFF0104, 0x00000040, 0x800F0001, 0xB9FF0003, 0x00000057, 0x800F0001, 0x00000042, 0x80030000, 0xB0E40008, 0x00000057, 0xB01F0000, 0x00000005, 0x8F030000, 0x90690003, 0xA9E40001, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 24, { 0xFFFF0104, 0x00000050, 0x81080000, 0xA0FF0001, 0x80F40000, 0x80E40001, 0x00000012, 0x800F0003, 0xA0E40001, 0xA3FF0003, 0x83FF0000, 0x0000FFFD, 0x00000042, 0x800F0002, 0xB0E40005, 0x00000042, 0xA00F0000, 0xA0A40001, 0x00000004, 0x80030004, 0x90E40002, 0xB4E41001, 0x885F0003, 0x0000FFFF } },
    { 26, { 0xFFFF0104, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0001, 0xB0E40000, 0x00000042, 0x800F0002, 0xB0E40000, 0x00000042, 0x800F0003, 0xB0E40000, 0x00000042, 0x800F0004, 0xB0E40000, 0x00000042, 0x800F0005, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 27, { 0xFFFF0104, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0001, 0xB0E40000, 0x00000042, 0x800F0002, 0xB0E40000, 0x00000042, 0x800F0003, 0xB0E40000, 0x00000042, 0x800F0004, 0xB0E40000, 0x00000042, 0x800F0005, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x0000FFFD, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 27, { 0xFFFF0104, 0x00000057, 0xA0190001, 0x00000050, 0x98030001, 0xA0A40002, 0xB0A40803, 0xA8E40001, 0x00000009, 0x800F0002, 0x84E40000, 0x90FF0001, 0x00000059, 0xA2080000, 0xB0A40000, 0x8AF40000, 0x00000002, 0x8C080003, 0x80E40001, 0x87AA0000, 0x00000001, 0x800F0000, 0x96E40002, 0x00000001, 0x800F0000, 0x80E40000, 0x0000FFFF } },
    { 30, { 0xFFFF0104, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFD, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000042, 0x800F0000, 0xB0E40000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 32, { 0xFFFF0104, 0x00000057, 0x800F0001, 0x00000041, 0x8006040C, 0x00000042, 0xB0070000, 0xB0F00001, 0x00000008, 0x80080001, 0x00000009, 0x80170000, 0x85E40001, 0xB0FF0003, 0x00000004, 0x8ECF0004, 0x94E40001, 0xB0F20005, 0x80FF0003, 0x00000003, 0xB31F0001, 0x80550001, 0xA2E40000, 0x00000009, 0x80070003, 0x97FF0001, 0x8AA40000, 0x40000009, 0x80180005, 0xB0B40002, 0xB0B40003, 0x0000FFFF } },
    { 32, { 0xFFFF0104, 0x00000042, 0x800F0003, 0xB9E40001, 0x00000042, 0x800F0000, 0xB0E40000, 0x0000FFFD, 0x00000041, 0x800F0002, 0x00000057, 0xB00F0003, 0x00000057, 0xA0020001, 0x00000041, 0x800C0000, 0x00000058, 0x8E0F0000, 0x90AA0000, 0x92FF0002, 0x92E40002, 0x00000001, 0xA0070001, 0xA0A40002, 0x40000008, 0x80080000, 0x9AFF0001, 0xA7000001, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 32, { 0xFFFF0104, 0x00000057, 0x810F0003, 0x00000057, 0xA00F0000, 0x00000009, 0x9E030000, 0x8A0003C2, 0x88A40001, 0x00000050, 0x80030003, 0x90F40003, 0x90B40001, 0x90550000, 0x00000050, 0xA0070001, 0x80FF0000, 0x80E40001, 0x88B40000, 0x40000012, 0xBD080000, 0x80FF0002, 0x88550002, 0x82F40001, 0x00000008, 0x940F0000, 0xB0FF0000, 0x90A40002, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 34, { 0xFFFF0104, 0x00000051, 0xA00F0003, 0xAE4E8FBC, 0xDECF53A4, 0xA4CC52AE, 0xFA70BD5E, 0x00000040, 0x900F0003, 0xB0E40001, 0x00000000, 0x00000012, 0xAE030000, 0xB7AA0000, 0xA0F40800, 0x80F40002, 0x00000059, 0x80030001, 0x91AA0001, 0xB0A40001, 0x00000003, 0xB0130000, 0x89E40002, 0x84E40001, 0x4000FFFD, 0x00000041, 0x800F0000, 0x0000000D, 0xAE0F0003, 0xB7F40000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 36, { 0xFFFF0104, 0x00000042, 0xB0030000, 0xA2E40001, 0x00000057, 0x800F0001, 0x00000040, 0x900F0000, 0xB5E40001, 0x0000FFFD, 0x00000057, 0x80070000, 0xB1E40001, 0x00000040, 0xA00B0000, 0xB0F40001, 0x00000002, 0x80030000, 0xA4F40002, 0x80E40000, 0x00000012, 0xB0070002, 0x80E40000, 0x85E40002, 0x81640000, 0x00000000, 0x00000000, 0x00000004, 0x900F1002, 0xB0FF0002, 0x90E40002, 0x85F40000, 0x00000001, 0x800F0000, 0xB0E40001, 0x0000FFFF } },
    { 38, { 0xFFFF0104, 0x00000051, 0xA00F0003, 0xF90A58EF, 0xBE5ED732, 0xDFD6FA22, 0xE9A604E3, 0x00000051, 0xA00F0003, 0xEA42B97C, 0x8E75A583, 0xD4C3C83C, 0xD5C446B5, 0x00000051, 0xA0050003, 0xA84F3EDD, 0xEC5AEB6C, 0x9139DE8E, 0x2C8B2355, 0x00000059, 0xB00F0002, 0x80A40001, 0xB0FF0001, 0x40000000, 0x00000059, 0x80070002, 0x80E40003, 0x89E40001, 0x4000005F, 0x80070001, 0x40000008, 0x82080000, 0x80FF0002, 0x88FF0001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 43, { 0xFFFF0104, 0x00000057, 0x91070001, 0x00000057, 0x800F0006, 0x00000042, 0x900F0002, 0xA0E40000, 0x00000040, 0x814F0000, 0x88550000, 0x40000005, 0x90180001, 0xB1E40000, 0x90FF0002, 0x00000005, 0x80080001, 0xA0A40003, 0xA2FF0001, 0x0000FFFD, 0x00000057, 0x80070001, 0x40000040, 0x800F0002, 0x8AE40002, 0x00000041, 0x800F0001, 0x00000042, 0x800F0000, 0xB0E40002, 0x00000050, 0x8F070001, 0x80FF0000, 0x80FF0001, 0xB0AA0000, 0x0000FFFD, 0x00000040, 0x900F0000, 0xB0E40000, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 51, { 0xFFFF0104, 0x00000042, 0x800F0000, 0x89F40000, 0x00000058, 0x800F0000, 0x80F40000, 0xB6E40001, 0xA1F40000, 0x00000008, 0x800F0000, 0x86A40001, 0x903C0004, 0x40000001, 0x810F0000, 0x80E40001, 0x40000005, 0x90071001, 0xB0A40003, 0xB7A40001, 0x40000012, 0x80080001, 0x9AB40000, 0x80A20001, 0xA0E40003, 0x00000000, 0x00000009, 0x830F0003, 0x80E40000, 0x80F40001, 0x00000004, 0xB0030002, 0xA0E40000, 0xD0E40800, 0x83E40002, 0x0000FFFD, 0x00000005, 0x80080000, 0x80E40000, 0x8A550001, 0x00000040, 0x809F0000, 0x87E40001, 0x00000003, 0xD0000001, 0x80F40002, 0xB6550001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 51, { 0xFFFF0104, 0x00000051, 0x80050007, 0x1B8A6ED6, 0x9C2C6E74, 0x575337FF, 0x888240BA, 0x00000041, 0x800E0001, 0x00000059, 0x800F0000, 0x92E40001, 0xB0FC0001, 0x0000FFFD, 0x00000040, 0x900F0001, 0x89E40001, 0x00000042, 0x800F0002, 0xB0A40001, 0x00000040, 0x800F0002, 0x80E40001, 0x00000040, 0x800F0000, 0xB0E40001, 0x00000040, 0x900F0003, 0xA0E40003, 0x4000FFFD, 0x00000057, 0xB1030002, 0x00000042, 0x900F0000, 0x8AE40000, 0x00000005, 0xB00F0002, 0xA0E40001, 0x82FF0000, 0x00000005, 0x80170001, 0x89000000, 0x83000002, 0x40000059, 0x8D080001, 0xB0E40001, 0xB5A40002, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 53, { 0xFFFF0104, 0x00000042, 0xB00F0002, 0xA0E40000, 0x00000040, 0x800F0001, 0xB0E40003, 0x0000FFFD, 0x00000057, 0x800F0003, 0x00000042, 0xA01F0000, 0x80E40001, 0x00000041, 0xA00F0001, 0x00000041, 0x800F0003, 0x00000041, 0x800F058C, 0x00000001, 0x800F0005, 0x80210001, 0x00000004, 0x80070001, 0x80CD0001, 0x9A000000, 0x80F40000, 0x40000005, 0xB0070003, 0xB4000001, 0x80E40000, 0x40000023, 0x80180003, 0x00000005, 0x80171001, 0xA0E412E6, 0x95AA0001, 0x40000009, 0xF1080002, 0xB4A40001, 0xA0E40001, 0x00000050, 0x80170003, 0x85550003, 0x90FF0001, 0xB5AA0001, 0x00000001, 0x80080001, 0xA53A0001, 0x00000001, 0x800F0000, 0x90E40000, 0x0000FFFF } },
    { 56, { 0xFFFF0104, 0x00000051, 0xA00F0008, 0x8E0F1781, 0xD8FB0D38, 0xCBAF9BF7, 0x85AFDE6D, 0x00000051, 0xA00F0002, 0xFDD6B2A6, 0xDE52A128, 0x0951AB56, 0x8157579A, 0x00000051, 0xA00F0002, 0x78C326CA, 0xED3F5D14, 0xEA35E6FB, 0xA46C13B8, 0x00000042, 0xB0030000, 0xBA3B0000, 0x00000042, 0xA0030003, 0x84E40000, 0x00000005, 0x8E1F0001, 0x80FF0001, 0xA0B40000, 0x00000005, 0xAE171003, 0x86E40001, 0x80E40001, 0x00000058, 0x800F0002, 0xA0E40002, 0xA0A40000, 0xA5E40003, 0x00000008, 0x80030001, 0x80F40001, 0x80550001, 0x00000005, 0xB00F0000, 0x80AA0000, 0x82000000, 0x40000000, 0x00000002, 0x830F0002, 0xA6550006, 0x93550002, 0x00000005, 0x810F0005, 0x83E40001, 0xB9E40002, 0x0000FFFF } },
    { 57, { 0xFFFF0104, 0x00000041, 0x800F0001, 0x00000041, 0x800F0001, 0x40000041, 0xA00F0000, 0x00000001, 0x8B080001, 0x82E41002, 0x00000002, 0x80070001, 0x80AA0001, 0xB4E40000, 0x00000001, 0xB0E70000, 0xB0B40001, 0x40000050, 0x801F04DB, 0x80FF0000, 0x84A40003, 0x80A41006, 0x0000FFFD, 0x00000057, 0x80000002, 0x00000005, 0x8F0F00F3, 0xA8F40000, 0x80A40001, 0x40000000, 0x00000002, 0x81CF0002, 0xAAFF0006, 0xB6E40001, 0x00000058, 0x80070001, 0x90AA0000, 0xB2FF0802, 0x90E40000, 0x40000059, 0x92F80000, 0x8AE40000, 0xA0E40001, 0x00000059, 0x86020000, 0xA0000002, 0xB0A40003, 0x00000000, 0x00000004, 0x900F0003, 0x93E40801, 0x80A40000, 0x90B40000, 0x00000001, 0x800F0000, 0xA0E40000, 0x0000FFFF } },
    { 57, { 0xFFFF0104, 0x00000051, 0x800F0001, 0x9183173F, 0xF2BC8CA1, 0xF562ABE6, 0xF3AEE22C, 0x00000040, 0xA0070000, 0x805E0003, 0x00000058, 0x80070001, 0xA5E40000, 0x80AA0001, 0x80F40000, 0x40000009, 0x80080000, 0x82B40002, 0x80550001, 0x00000004, 0x80270000, 0x80550001, 0x80AA0003, 0xB9AA0001, 0x00000001, 0xB0A70001, 0x90FF0003, 0x00000050, 0x820F0000, 0x80FF0000, 0x80E40000, 0xB0A40001, 0x00000012, 0x80080001, 0x85E40001, 0x87A40001, 0x89E40000, 0x00000050, 0x801F0002, 0x80FF0000, 0x83550000, 0x80AA0003, 0x00000001, 0xAE081000, 0xA6E40001, 0x00000008, 0x8D070001, 0x98A40001, 0xB0AA0006, 0x40000008, 0x8E080001, 0x90FF0002, 0x90E40001, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 57, { 0xFFFF0104, 0x00000057, 0x80090003, 0x00000001, 0x903F0003, 0x88550003, 0x00000005, 0x84070000, 0x80E40000, 0x83F40003, 0x00000050, 0x801F0003, 0x80FF0000, 0xA9000003, 0x90550001, 0x00000002, 0x80070001, 0xA0F40000, 0x80F40000, 0x40000002, 0xB0180003, 0xB3FF0001, 0xA0550000, 0x00000050, 0xB00F0000, 0x80FF0000, 0x80FF0005, 0x89A40001, 0x00000002, 0x801F0001, 0x8AA40002, 0x80E40005, 0x00000059, 0x800F0000, 0xB2750001, 0x8AE40001, 0x00000002, 0x8F0F0000, 0x8AF40001, 0xA9F40001, 0x00000050, 0xB01F0003, 0x80FF0000, 0x86E40000, 0xA0000000, 0x0000FFFD, 0x00000041, 0xB0030001, 0x00000058, 0x808F0001, 0x81FF0001, 0x80A40000, 0x8AFF0001, 0x00000001, 0x800F0000, 0xB0E40000, 0x0000FFFF } },
    { 57, { 0xFFFF0104, 0x0001FFFE, 0x12345678, 0x00000042, 0x800F0003, 0x8AE40008, 0x00000041, 0x800F0001, 0x00000005, 0x80080003, 0xA0550000, 0xA0F40000, 0x0000FFFD, 0x00000040, 0x810F0001, 0x80E40002, 0x00000000, 0x4000001F, 0xEF080801, 0x00000059, 0x800F0001, 0x85E40001, 0x90A40001, 0x00000012, 0x8D080000, 0x80FF0000, 0x95B40003, 0x87A40001, 0x00000050, 0x90770000, 0x80FF0000, 0x85F40001, 0x80A40003, 0x40000002, 0xB0080000, 0xA0E41000, 0xB5B40001, 0x00000040, 0x800F0000, 0xB0000000, 0x40000050, 0xE0F80003, 0x84A40001, 0x88FF0000, 0xAAF40001, 0x00000003, 0x80080000, 0xA0F40002, 0x80AA0003, 0x00000008, 0x80070003, 0x90E40001, 0x80550004, 0x00000001, 0x800F0000, 0xB0E40001, 0x0000FFFF } },
    { 13, { 0xFFFE0111, 0x00000051, 0xA00F0007, 0x31E31E65, 0x908A20D1, 0x1E394EC0, 0xDC555963, 0x00000004, 0x830F0002, 0x8A550002, 0xA3F40003, 0xA9A40000, 0x0000FFFF } },
    { 5, { 0xFFFF01FF, 0x00000001, 0x800F0000, 0x80E40001, 0x0000FFFF } },
    { 8, { 0xFFFF0200, 0x0200001F, 0x80000002, 0x9001000B, 0x0200005C, 0x800F0802, 0x9AE41001, 0x0000FFFF } },
    { 11, { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB04D0000, 0x02000007, 0x800F0000, 0xF0E40002, 0x02000030, 0x810B000B, 0xE1E40802, 0x0000FFFF } },
    { 18, { 0xFFFF0200, 0x04000004, 0x80480002, 0xA0E40000, 0xA0E40815, 0x80550017, 0x01000001, 0xF0FF0800, 0x02000024, 0x800F0001, 0x80E40001, 0x02000024, 0x8F2F0002, 0x9CFF1000, 0x0200002E, 0xA00F1000, 0xA0FF0000, 0x0000FFFF } },
    { 23, { 0xFFFF0200, 0x05000030, 0xF00F0064, 0x00000080, 0x00000001, 0x0000007F, 0x00000100, 0x01000040, 0x800F0000, 0x03000009, 0x800F0001, 0x90E41001, 0x8CE40000, 0x0200000F, 0x804F0800, 0xA06D1000, 0x02000001, 0x800F0800, 0xA1E40002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 31, { 0xFFFF0200, 0x05000030, 0xF00F0025, 0x00000001, 0x00000080, 0x0000007F, 0x00000100, 0x03000021, 0x80280800, 0xE0E40802, 0x80990002, 0x02000001, 0x8F370801, 0x80000002, 0x03000009, 0x800F0002, 0xAC000000, 0x80AA0022, 0x03000001, 0x801F0802, 0xB0001000, 0x91C91000, 0x01000019, 0xA0E41175, 0x02000013, 0x800F0002, 0x801B0001, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 32, { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB02B0001, 0x05000051, 0xA00F0001, 0xFFFFFF80, 0x00000001, 0x00000000, 0x000000FF, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000080, 0x00000100, 0x6EB1D47E, 0x05000051, 0xA00F0000, 0x0000007F, 0x0000007F, 0x00000000, 0x00000080, 0x03000021, 0x8F0F001F, 0x8D000003, 0xF1AA0920, 0x0400005A, 0x800F0005, 0xA0E40000, 0xA0E400A4, 0xACE40088, 0x0000FFFF } },
    { 42, { 0xFFFF0200, 0x03000042, 0x80310000, 0x8B1B0001, 0xA0E40809, 0x0300000A, 0x800F0004, 0x90D20003, 0xB4000036, 0x0200001A, 0xA0E41002, 0xE0E40800, 0x03000002, 0xF04F0004, 0xABE4001E, 0xF0E40800, 0x03000017, 0x901F0000, 0x90AA0002, 0xB0E40000, 0x03000005, 0x810F0001, 0xA01B0002, 0xFCE40897, 0x01000001, 0xA8E40001, 0x44000025, 0xB011001C, 0x81E40000, 0xA0D51006, 0xB0E40000, 0x01000026, 0xF0E40001, 0x0200000F, 0x80030000, 0xE1E40800, 0x03000005, 0x904F0800, 0x80AA0001, 0x80E40000, 0x00000027, 0x0000FFFF } },
    { 44, { 0xFFFF0200, 0x0200001F, 0x80000000, 0x900F0000, 0x0200001F, 0x80000000, 0x900F0001, 0x0200001F, 0x90000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x03000042, 0x800F0000, 0xB0E40000, 0xA0E40800, 0x03000042, 0x800F0001, 0x80E40000, 0xA0E40800, 0x03000042, 0x800F0002, 0x80E40001, 0xA0E40800, 0x03000042, 0x800F0003, 0x80E40002, 0xA0E40800, 0x03000042, 0x800F0004, 0x80E40003, 0xA0E40800, 0x03000042, 0x800F0004, 0x80E40001, 0xA0E40800, 0x02000001, 0x800F0800, 0x80E40003, 0x0000FFFF } },
    { 47, { 0xFFFF0200, 0x0300000B, 0x804F0000, 0xA1E40001, 0xA0E4087A, 0x02000007, 0x802F0800, 0x80E40003, 0x01000028, 0xBDE41000, 0x0200005C, 0x8008001F, 0x8CFF0000, 0x0000002B, 0x03020042, 0x804E0000, 0xB0550001, 0xA1E40802, 0x03000016, 0x876F0000, 0x8CAA0000, 0x90550001, 0x04000025, 0x800F0008, 0x801B0010, 0xF1E40801, 0xA4C90000, 0x04000025, 0x80010000, 0x90E40000, 0x90E40002, 0xAC0000B1, 0x02000043, 0x806F0001, 0x90001002, 0x0200001C, 0xB0000000, 0x80D20002, 0x04000050, 0x803F0000, 0xF0D20802, 0x81FF0002, 0x81E40001, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 56, { 0xFFFF0200, 0x0200001F, 0x80000000, 0xB0630003, 0x0200002F, 0xE00F0834, 0x00000000, 0x05000051, 0xA00F0091, 0x00000080, 0x00000000, 0x000000FF, 0x000000FF, 0x05000030, 0xF00F0056, 0x00000100, 0x00000100, 0x000000FF, 0x00000080, 0x03000020, 0x900F0000, 0xADD21000, 0xB01B0001, 0x01000026, 0xF0E40000, 0x02000043, 0x800B0000, 0xA0E40000, 0x03000017, 0x90CF0802, 0x81E40002, 0x80AA0000, 0x02000001, 0x801F0802, 0x80E40006, 0x01000060, 0xB0001000, 0x03000059, 0xB01F0002, 0xFBE40001, 0x802D00F9, 0x00000027, 0x03000020, 0x8F1F0002, 0x8B55006B, 0x81E40002, 0x01000026, 0xF0E40001, 0x01000040, 0x800F0004, 0x00000000, 0x00000027, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 60, { 0xFFFF0200, 0x0200001F, 0x80000000, 0x90070001, 0x0200001F, 0xA0000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F080D, 0x0200001F, 0x90000000, 0xA00F0809, 0x0200001F, 0xA0000000, 0xA00F080E, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x04000058, 0x800F0007, 0x80E40007, 0x90E40001, 0x80E40007, 0x04000025, 0x80020007, 0xA0FF0000, 0xA0E40001, 0xA0E40002, 0x0200000F, 0x80010005, 0x90E40001, 0x02000001, 0x800F0800, 0x80E40007, 0x02000001, 0x800F0801, 0x80E40005, 0x02000001, 0x800F0802, 0x80E40005, 0x02000001, 0x800F0803, 0x80E40007, 0x02000001, 0x900F0800, 0x80FF0007, 0x02000001, 0x900F0800, 0xA0000000, 0x0000FFFF } },
    { 11, { 0xFFFF0201, 0x00000000, 0x04000025, 0x80110002, 0x80E40002, 0x80000002, 0x90E40001, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 15, { 0xFFFF0201, 0x0200001F, 0xA0000000, 0xA00F0800, 0x02000001, 0x8001001B, 0xA0E40000, 0x0200005C, 0x800F001B, 0x80E4001B, 0x03000008, 0x800F001B, 0xA0E40000, 0x81E4001B, 0x0000FFFF } },
    { 19, { 0xFFFF0201, 0x0200002F, 0xE00F0801, 0x00000000, 0x05000051, 0xA00F0001, 0x0000007F, 0x000000FF, 0x0000007F, 0x00000080, 0x00000027, 0x00040029, 0x0200005B, 0x800F1012, 0xB01B104C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 20, { 0xFFFF0201, 0x0600001F, 0x80000000, 0xB00F0003, 0x0200001F, 0x80000000, 0x80030002, 0x0200001F, 0x80000000, 0x90070002, 0x0200001F, 0x80000000, 0xB0030004, 0x0200002E, 0x800D8001, 0xF0E40000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 24, { 0xFFFF0201, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0x80000000, 0xB0030000, 0x0200001F, 0x80000000, 0xB00F000A, 0x0200002F, 0xE00F0800, 0x00000080, 0x05000051, 0xA00F0000, 0x0000007F, 0x00000100, 0x00000000, 0x704A4702, 0x02000023, 0x80F30802, 0x80FF0001, 0x4000002A, 0x0000FFFF } },
    { 26, { 0xFFFF0201, 0x03000015, 0xBC011017, 0xFCE40801, 0xA0E40002, 0x0300000B, 0x80120800, 0xA0E40000, 0x90E41005, 0x02000001, 0x800F0800, 0x80E40000, 0x01000019, 0xA0E4118C, 0x02020029, 0x80D20001, 0xF1C90807, 0x03000017, 0x89000802, 0xB0550000, 0x80C90007, 0x03000002, 0x800F0800, 0x90E40023, 0x81E40002, 0x0000FFFF } },
    { 27, { 0xFFFF0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x90000000, 0xA0010800, 0x0200001F, 0x90000000, 0xA00F0802, 0x0200001F, 0x80000000, 0x9073000C, 0x0200001F, 0x80000000, 0xB0010003, 0x0200001F, 0x80000000, 0x900F000D, 0x03000021, 0x801F000E, 0xA01B1000, 0xB0E4011A, 0x02000013, 0x80DF0027, 0xA11B0014, 0x00200000, 0x0000FFFF } },
    { 28, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0070007, 0x0200001F, 0x80000000, 0x90070001, 0x0200001F, 0x98000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x04000004, 0x8001000A, 0x80E4000A, 0x80E4000A, 0xB0E40007, 0x02000001, 0x800F0800, 0x80E4000A, 0x02000001, 0x800F0801, 0x80E4000A, 0x0000FFFF } },
    { 29, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0010000, 0x0200001F, 0x80000000, 0x90000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x0200002F, 0xE00F0800, 0x0000007F, 0x02000059, 0x80C90001, 0xFB550801, 0x04000022, 0xA07F1000, 0x90E41002, 0xFDFF087F, 0x8DB60072, 0x03000002, 0x800F0002, 0x90C90001, 0x81FF0006, 0x02000001, 0x84040000, 0x90E40827, 0x0000FFFF } },
    { 31, { 0xFFFF0201, 0x0400005A, 0x800F0000, 0x90D21005, 0xF0CF0802, 0x8CE40002, 0x02000024, 0x800F0000, 0xABFF0000, 0x03000021, 0x800F0803, 0xA0E40000, 0x9BFF1001, 0x0000001C, 0x0100001E, 0xA0E41001, 0x03000003, 0x803F0002, 0xABE40800, 0xA51B0000, 0x0100001E, 0xA0E41002, 0x13000013, 0xB00F0001, 0xBD001000, 0xE0E40000, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 31, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x900F0000, 0x0200001F, 0x80000000, 0x906F0000, 0x0200001F, 0x00000080, 0x9047000D, 0x0200001F, 0x80000000, 0xB0070001, 0x0200001F, 0x80000000, 0xB0010003, 0x0200002F, 0xE00F0801, 0x00000001, 0x1500005A, 0xB0070000, 0xB0001000, 0x90FF1001, 0x90E41002, 0xA0E4001D, 0x0400005A, 0x800F0826, 0xB0E41000, 0x8D550000, 0xF0C90800, 0x0000FFFF } },
    { 31, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0230002, 0x03000018, 0x80010001, 0x80960000, 0x90001001, 0x0000001C, 0x0100001E, 0xA0E41000, 0x01000026, 0xF0E40000, 0x0200000F, 0x803F0001, 0xECE40800, 0x0204002D, 0x80000000, 0xA0000000, 0x0200001A, 0xA0E41000, 0xBD001000, 0x0200004E, 0x9FDF0808, 0xA0E40802, 0x00000027, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 31, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0030003, 0x13000007, 0xA1211001, 0xB1AA1001, 0x80AA0001, 0x0000001C, 0x0100001E, 0xA0E41001, 0x01000001, 0xACC90016, 0x02000006, 0xA03F1002, 0x8DAA0004, 0x4200001E, 0x90E41001, 0xA0AA1116, 0x0100001E, 0xA0E41001, 0x04000022, 0xC01F0805, 0x90E41005, 0xEEE40003, 0xE0E40801, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 32, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0110002, 0x0200001F, 0x80000000, 0x901F0000, 0x0200005B, 0x80130000, 0x801B0000, 0x00000000, 0x0100001E, 0xEDE40806, 0x03000009, 0x870F0801, 0xA0E4081F, 0x9DE40000, 0x1200001D, 0xB0FF1001, 0xD0780800, 0x0600005B, 0xA00F1000, 0xA0D21000, 0x0400005A, 0xB04F0000, 0xA0FF1001, 0xFDE40001, 0xB0550000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 34, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB00F0000, 0x0200001F, 0x80000000, 0x900B000F, 0x0200001F, 0x80000000, 0x90AF0000, 0x0200001F, 0x80000000, 0x900F0003, 0x0200001F, 0x80000000, 0xB0130000, 0x0200002F, 0x800F1001, 0xFFFFFFFF, 0x03000042, 0x80310005, 0x91E40000, 0xAB1B0004, 0x0302005E, 0xB00F1024, 0xABE40001, 0x80C90803, 0x15000025, 0x9F0F0801, 0xB1E41000, 0x90E40001, 0xA0AA0035, 0x80C90000, 0x0000FFFF } },
    { 37, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x90040000, 0x0200001F, 0x80000000, 0x900F0000, 0x05000051, 0xA00F0002, 0x00000001, 0x00000080, 0xFFFFFF80, 0x00000000, 0x0200000F, 0xA00F1021, 0xB0E40000, 0x03000005, 0x804F0000, 0xB0AA0000, 0xA0D20000, 0x0200000E, 0xB00F1005, 0x93001000, 0x0200001A, 0xA0E41000, 0xBDE41000, 0x0000001C, 0x0100001E, 0xA0E41001, 0x0200001A, 0xA0E41000, 0xB0E40801, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 38, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB02B0000, 0x05000030, 0x900F0051, 0x000000FF, 0x00000080, 0x000000FF, 0x00000100, 0x03000003, 0x800F0002, 0xB0E40000, 0x80550000, 0x0400005A, 0x80080002, 0xB0E40001, 0x90E41000, 0x80D20001, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001A, 0xB0BE0005, 0xA1E40001, 0x14000042, 0xB02B0000, 0xB1551000, 0x80E40003, 0xAB550800, 0x0302005E, 0xB01F1002, 0xA1C91001, 0xB055101F, 0x0200002A, 0xB0001001, 0xA0E40801, 0x0000FFFF } },
    { 39, { 0xFFFF0201, 0x0200001F, 0xA0000000, 0xA02F080F, 0x05000030, 0xF00F0001, 0x00000100, 0x0000007F, 0xFFFFFF80, 0xFD4F624C, 0x0200000F, 0x8D03080A, 0x91E40001, 0x0000001C, 0x0100001E, 0xA0E41001, 0x04000050, 0x800F0000, 0xF0E40802, 0x80E40001, 0x8DE40001, 0x01000019, 0xA0E41000, 0x0000001C, 0x0100001E, 0xA0E41002, 0x44000050, 0x80080000, 0xBCAA0800, 0xA71B0000, 0x8B550001, 0x03000016, 0x8F0F0000, 0x90E40002, 0xBCE41000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 40, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x90430003, 0x05000030, 0xF00F0002, 0x00000001, 0x0000007F, 0x3F800000, 0x00000080, 0x02000001, 0x800F0800, 0x80E40000, 0x05000030, 0xF00F0002, 0x00000001, 0x3C12AAF5, 0xD1C85DAF, 0x00000000, 0x01000060, 0x90001001, 0x03000005, 0xB04F0000, 0x90551000, 0xFBE40802, 0x02000001, 0x800F0002, 0xA1E40001, 0x0200004E, 0x800F0001, 0x90E41001, 0x15000058, 0x800F0802, 0xB1551000, 0x90E41004, 0x80E40005, 0x80C90000, 0x01000041, 0x800F0002, 0x0000FFFF } },
    { 40, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB02F0006, 0x0200001F, 0x80000000, 0x900F0001, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F0802, 0x0200001F, 0x90000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x04000058, 0x800F001A, 0x80E4001A, 0x801B001A, 0x80E4001A, 0x04000058, 0x800F000F, 0x81E4001A, 0x80E4000F, 0x90E40001, 0x0300000A, 0x800F0006, 0x811B001A, 0xB0E40006, 0x02000001, 0x800F0800, 0x80E4001A, 0x0000FFFF } },
    { 42, { 0xFFFF0201, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x01000019, 0xA0E41001, 0x01000028, 0xE0E40800, 0x0000001C, 0x0000002B, 0x0100001E, 0xA0E41001, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 46, { 0xFFFF0201, 0x0200002F, 0xE00F0802, 0x00000100, 0x05000051, 0xA00F0001, 0x000000FF, 0x00000000, 0x00000080, 0x00000080, 0x02000001, 0x90110800, 0x81E40006, 0x0400005A, 0x801F0800, 0x90E40001, 0xA0E40811, 0x8B550000, 0x03000014, 0xC00F0801, 0x96D21005, 0x90AA1002, 0x03000021, 0x80020000, 0xB1AA1000, 0x81E40000, 0x03000005, 0x800F0004, 0x909F0000, 0xF0E40802, 0x0300000C, 0x90070002, 0xA0C910D8, 0x801B0018, 0x0300000A, 0xA00F1001, 0x891B0001, 0x90E40800, 0x02000043, 0x910F0801, 0xE0E40800, 0x0306005E, 0xB00F1000, 0x80D2000E, 0x80E40002, 0x0000FFFF } },
    { 48, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x901C0002, 0x0200001F, 0x80000000, 0x9041000C, 0x0200001F, 0x80000000, 0x80030001, 0x11000000, 0xB0C21000, 0x0500005D, 0xB00F0002, 0xADE40000, 0xACE40800, 0xA0E40800, 0xA0E41025, 0x00000000, 0x03010042, 0x80440802, 0xB0E40000, 0xA0E40801, 0x02000013, 0x80020001, 0xA0E41000, 0x03000016, 0x80030800, 0xECE40801, 0x80550006, 0x05000024, 0x800F0003, 0x80E40002, 0xACE40002, 0xBDFF000E, 0x80E40002, 0x02000007, 0x804F0002, 0xE1AA0800, 0x0300005F, 0xB0080002, 0x80E40000, 0xA0E408FB, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 50, { 0xFFFF0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0xC0040802, 0x0200001F, 0x80000000, 0x90030003, 0x0200002F, 0xE00F0802, 0xE3481A7D, 0x04000058, 0x80120001, 0x80E40001, 0x90D20006, 0xB0E40022, 0x04000025, 0x8002001B, 0x80D20001, 0xA0E40000, 0xADE40000, 0x0200001A, 0xA0E41002, 0xB0001000, 0x0400005D, 0xB0E40000, 0xACE40801, 0xA0FF0002, 0x91550000, 0x03000005, 0x800F0002, 0x8DFF0006, 0xA1AA0001, 0x03000042, 0x800F0002, 0x80E400BD, 0xA0E40800, 0x13000013, 0xB00F0000, 0xBDFF1000, 0xA0E40000, 0x0300000B, 0x80120002, 0x90E41002, 0x90C90000, 0x02000001, 0x804F0000, 0x80E40007, 0x0000FFFF } },
    { 51, { 0xFFFF0201, 0x0200001F, 0x90000000, 0xA02B080C, 0x0200001F, 0x80000000, 0xB0110000, 0x0200001F, 0x80000000, 0x90090002, 0x0200001F, 0x80000000, 0x900F0008, 0x05000051, 0xA00F00FB, 0x3F800000, 0x00000100, 0x3F800000, 0xFFFFFF80, 0x0200002F, 0xE00F0802, 0xFFFFFFFF, 0x0200002F, 0xE00F0800, 0x3F800000, 0x5600005D, 0xB0181000, 0xA0000000, 0x90AA00C8, 0xA0E40812, 0xB0E40078, 0xFDE40800, 0x03000016, 0x800F0001, 0x90C9000D, 0x81E40000, 0x03020042, 0x902F0802, 0xADE40802, 0xA0E48800, 0x0300000A, 0x806B0000, 0xA0E40802, 0xA0E40000, 0x0200005B, 0x800F0800, 0x81AA0000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 51, { 0xFFFF0201, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x0202002D, 0x80000000, 0xA0000000, 0x00000027, 0x00000027, 0x00000027, 0x00000027, 0x00000027, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 51, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0230003, 0x0200001F, 0x80000000, 0xB0430004, 0x0200001F, 0x80000000, 0xB00F0001, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F0803, 0x0200001F, 0xA0000000, 0xA00F080B, 0x0200001F, 0x90000000, 0xA00F0805, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x0300000A, 0x800F0007, 0xA0E40000, 0x80E40007, 0x00000000, 0x0301005E, 0xB00F1000, 0xA0E40000, 0xA0E40000, 0x0306005E, 0xB00F1000, 0xA0E40000, 0x81E40007, 0x02000001, 0x800F0800, 0x80E40007, 0x02000001, 0x800F0801, 0x80E40007, 0x02000001, 0x800F0802, 0x80E40007, 0x0000FFFF } },
    { 51, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB00F0000, 0x0200001F, 0x80000000, 0xB02F0004, 0x0200001F, 0x80000000, 0xB0010003, 0x0200001F, 0x80000000, 0xB0430001, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F080B, 0x0200001F, 0x98000000, 0xA00F0802, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0001, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x0500005D, 0x800F0014, 0xB0E40004, 0xA0E40802, 0x80E40014, 0x80E40014, 0x02000001, 0x800F000B, 0x8155000B, 0x03000009, 0x800F001B, 0x80FF001B, 0x8009001B, 0x0000FFFF } },
    { 52, { 0xFFFF0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0x90230002, 0x0200001F, 0x80000000, 0x900B0000, 0x05000030, 0xF00F0112, 0x0000007F, 0x3F800000, 0x3F800000, 0x00000100, 0x05000051, 0xA0230002, 0x00000001, 0x00000080, 0x0000007F, 0x3F800000, 0x05000030, 0xF0080002, 0x000000FF, 0x3441F7B9, 0x3F800000, 0x00000100, 0x0300005F, 0xB00F0002, 0x80E400AF, 0xA0E40811, 0x03000015, 0x800F0000, 0xB0E40800, 0xA0920000, 0x02000024, 0x8F320003, 0x80000000, 0x0200000E, 0x80044002, 0x80E40000, 0x0200000F, 0x900F0800, 0xACAA0015, 0x43000014, 0x80020002, 0x81E40000, 0x801B0006, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 52, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB00F0005, 0x0200001F, 0xA0000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F080E, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0001, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x0300000B, 0x8001000A, 0x80AA000A, 0x80E4000A, 0x03000042, 0x800F000D, 0x80E4000A, 0xA0E4080E, 0x02000001, 0x800F0800, 0x80E4000A, 0x02000001, 0x800F0801, 0x80E4000A, 0x02000001, 0x800F0802, 0x80E4000D, 0x02000001, 0x800F0803, 0x80E4000A, 0x0000FFFF } },
    { 55, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x900F0001, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F080E, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x42000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x0400005A, 0x80030018, 0x801B0018, 0x80E40018, 0xA0FF0002, 0x01000019, 0xA0E41000, 0x03000042, 0x800F001D, 0x80E40018, 0xA0E40800, 0x02000013, 0x80030004, 0x801B0004, 0x02000001, 0x800F0800, 0x80E40004, 0x02000001, 0x800F0801, 0x80E40004, 0x0000001C, 0x0100001E, 0xA0E41000, 0x00000000, 0x03010042, 0x800F0000, 0x80E40018, 0xA0E40800, 0x0200005B, 0x80110004, 0x80E40004, 0x0000001C, 0x0000FFFF } },
    { 55, { 0xFFFF0201, 0x0200001F, 0x80000000, 0x90030001, 0x0200001F, 0x80000000, 0x800F0001, 0x0200001F, 0x80000000, 0xB0030001, 0x05000051, 0xA01F0001, 0x00000100, 0xFFFFFFFF, 0x00000100, 0x00000100, 0x04000012, 0x800F0000, 0x80C90000, 0x90C90000, 0x90550002, 0x01000028, 0xB0E41000, 0x04000058, 0x800F0003, 0x8C1B0000, 0xA0FF0121, 0xF0E40801, 0x0000002B, 0x0200005B, 0x90170001, 0xA9E40088, 0x1500005A, 0x800F0005, 0xB1AA1000, 0xB0D20000, 0x90B41001, 0x8DE40001, 0x03000016, 0x810F0808, 0xE3AA0813, 0x90001000, 0x0200004E, 0x810F1000, 0xA0E41002, 0x01000040, 0x801F0801, 0x0200001A, 0xA0E41001, 0xB0E40000, 0x1300001A, 0x8D550000, 0xB0E40001, 0xE0E40879, 0x0000FFFF } },
    { 56, { 0xFFFF0201, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0xB0430001, 0x0200001F, 0x80000000, 0xB01F0001, 0x01000028, 0xE0E40800, 0x04000050, 0xA0011000, 0xF0E40827, 0x90E41000, 0x80FF0000, 0x03000016, 0x900F0002, 0x90E41005, 0xA0E40012, 0x02000001, 0xB00F0000, 0x9CB30000, 0x0000002B, 0x03000002, 0x803F0800, 0xB1551064, 0xA0550000, 0x03000021, 0x800F0000, 0x801B0000, 0x90E41000, 0x03000008, 0x800F0000, 0x81E40001, 0xF0000800, 0x01000026, 0xF0E40001, 0x12000026, 0xBD1F1000, 0x80550000, 0x00000027, 0x0200000E, 0x900F0800, 0xB5D20800, 0x01000040, 0x803F0000, 0x04000050, 0x80E6001E, 0x80000002, 0xACE40001, 0xB1E400C9, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 57, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0470006, 0x0200001F, 0x80000000, 0xB0470007, 0x0200001F, 0x80000000, 0xB02F0004, 0x0200001F, 0x80000000, 0xB0030001, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F080D, 0x0200001F, 0x90000000, 0xA00F0802, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0001, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x04000025, 0x80030009, 0xA0AA0000, 0xA0E40001, 0xA0E40002, 0x01000041, 0x800F0009, 0x02000001, 0x800F0800, 0x80E40009, 0x0000FFFF } },
    { 57, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0010007, 0x0200001F, 0x80000000, 0x90070000, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F080B, 0x0200001F, 0x98000000, 0xA00F0804, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0002, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x02000001, 0x800F0008, 0xA0E40000, 0x0302005E, 0xB00F1000, 0x80E40008, 0x80E40008, 0x02000001, 0x800F0800, 0x80E40008, 0x0000FFFF } },
    { 58, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB00F0004, 0x0200001F, 0x80000000, 0xB00F0002, 0x0200001F, 0x80000000, 0xB0010001, 0x0200001F, 0x80000000, 0x900F0000, 0x0200001F, 0x98000000, 0xA00F0800, 0x05000030, 0xF00F0000, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x00000000, 0x0400005A, 0x80080015, 0x90E40000, 0xA0E40000, 0xA0FF0000, 0x03000002, 0x800F001E, 0xA0E40001, 0x80E40015, 0x03000002, 0x8023001E, 0x80E4001E, 0xB0E40004, 0x03000017, 0x8007001E, 0x90E40000, 0xA0E40000, 0x01000019, 0xA0E41002, 0x02000001, 0x800F0800, 0x80E4001E, 0x0000001C, 0x0100001E, 0xA0E41002, 0x00000000, 0x00000000, 0x02000001, 0x800F0001, 0x80570001, 0x0000001C, 0x0000FFFF } },
    { 58, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0210007, 0x0200001F, 0x80000000, 0x90070000, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F080D, 0x0200001F, 0x90000000, 0xA00F0803, 0x0200001F, 0xA0000000, 0xA00F0805, 0x0200001F, 0x90000000, 0xA00F080F, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x03000008, 0x800F0006, 0x90E40000, 0xA1E40000, 0x0303005E, 0xB00F1000, 0xA0E40000, 0x80000006, 0x02000001, 0x800F0800, 0x80E40006, 0x02000001, 0x800F0801, 0x80E40006, 0x02000001, 0x800F0802, 0x80E40006, 0x0000FFFF } },
    { 61, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB02F0006, 0x0200001F, 0x80000000, 0xB02F0007, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0809, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x03020042, 0x800F000C, 0xB0E40006, 0xA0E40800, 0x03010042, 0x802F001E, 0x80E4000C, 0xA0E40809, 0x03000005, 0x8001000A, 0xA0E40000, 0x80E4000C, 0x01000028, 0xE0E40801, 0x04000012, 0x80010000, 0xA0E40000, 0xB0E40006, 0x802C000C, 0x0000002A, 0x03000042, 0x802F0001, 0xB0E40006, 0xA0E40800, 0x03000042, 0x800F0001, 0xB0E40006, 0xA0E40800, 0x0000002B, 0x02000001, 0x800F0800, 0x80E4000C, 0x02000001, 0x800F0801, 0x80E4000A, 0x0000FFFF } },
    { 62, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB0000001, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0xB0000000, 0xA0030803, 0x0200001F, 0x90000000, 0xA00B0803, 0x0200001F, 0x80000000, 0x800F0011, 0x05000030, 0xF00F0001, 0xFFFFFFFF, 0x00000000, 0x000000FF, 0x00000000, 0x0200002F, 0xE00F083B, 0xFFFFFFFF, 0x05000030, 0xF00F0002, 0xFFFFFFFF, 0x0000007F, 0x00000000, 0xFFFFFF80, 0x01000040, 0x903F0000, 0x03000021, 0x800B081C, 0x9BE4000D, 0xEBE40928, 0x04000050, 0x800B000A, 0x90601001, 0x801B0000, 0x801B0000, 0x03000042, 0xC04F0001, 0xB000009F, 0xA0FF0800, 0x0200000E, 0x806F0012, 0x80E40000, 0x01000040, 0x800F0000, 0x12000041, 0x80080000, 0xB1531000, 0x0400005A, 0x80080800, 0x81E40000, 0x90FF0002, 0x80E400FF, 0x01000019, 0xA0E41001, 0x0000FFFF } },
    { 62, { 0xFFFF0201, 0x0200001F, 0x80000000, 0xB00F0002, 0x0200001F, 0x80000000, 0x90070001, 0x0200001F, 0x80000000, 0x900F0000, 0x0200001F, 0xA0000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x02000001, 0x800F001C, 0xB0AA0002, 0x01000026, 0xF0E40003, 0x0305005E, 0xB00F1000, 0x80D7001C, 0x81E4001C, 0x03000009, 0x8007001C, 0x80E4001C, 0xB0FF0002, 0x0000002C, 0x02000001, 0x800F0011, 0x80E4001C, 0x0200000E, 0x800F0011, 0xA1550000, 0x00000027, 0x0200005B, 0x800F0011, 0x80E40011, 0x02000001, 0x800F0800, 0x80E40011, 0x0000FFFF } },
    { 12, { 0xFFFF02FE, 0x05000030, 0xF00F0002, 0xFFFFFFFF, 0x5137A11A, 0xFFFFFF80, 0x00000000, 0x00000000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 12, { 0xFFFF02FF, 0x0200001F, 0x80000000, 0x80470003, 0x00000027, 0x02000010, 0x814F0003, 0xA0550018, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 10, { 0xFFFF0300, 0x0200001F, 0xE0000000, 0xA09F0802, 0x0200001F, 0x80040005, 0x900B0000, 0x0100001E, 0xA0E4090F, 0x0000FFFF } },
    { 12, { 0xFFFF0300, 0x0200001F, 0x80110005, 0xB0130001, 0x03000020, 0xB00F1005, 0x80000013, 0xA0E41000, 0x0200000F, 0xC00F0800, 0x90C21001, 0x0000FFFF } },
    { 19, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA0010800, 0x0200001F, 0x98000000, 0xA00F0801, 0x0200001F, 0x98000000, 0xA00B0801, 0x0200001F, 0x80020009, 0x90030000, 0x02000007, 0xB06F0802, 0x80070002, 0x01000041, 0x800F0004, 0x0000FFFF } },
    { 21, { 0xFFFF0300, 0x05000051, 0xA00F0002, 0x00000001, 0x2909B729, 0xFFFFFF80, 0x00000100, 0x0200002F, 0xE00F0801, 0x00000001, 0x02000001, 0x800F0001, 0x80D2001C, 0x03000020, 0x8F0F0000, 0xA0E40000, 0x80E40000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 23, { 0xFFFF0300, 0x0200002F, 0xE00F0801, 0x000000FF, 0x05000051, 0xA00F004C, 0x00000001, 0xFFFFFF80, 0x0000007F, 0x000000FF, 0x02000001, 0x800F0001, 0x9DC91002, 0x01000026, 0xF0E40004, 0x0302005E, 0xB00F1002, 0xA0E40800, 0x906A0002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 24, { 0xFFFF0300, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0xC8000000, 0xA00B0800, 0x0200002F, 0xE00F0800, 0xFFFFFFFF, 0x0A00002F, 0xE00F0800, 0x3F800000, 0x05000030, 0xF0030002, 0x0000007F, 0x00000001, 0x00000000, 0x00000001, 0x03000020, 0x900F0002, 0xA6E40000, 0xE0E40801, 0x0000FFFF } },
    { 26, { 0xFFFF0300, 0x05000051, 0xA00F0000, 0x3F800000, 0x3F800000, 0x00000001, 0x00000100, 0x0200002F, 0xE0020802, 0x00000001, 0x03000009, 0x800F0801, 0xA0E40802, 0xB11B0000, 0x02000059, 0xE0E40802, 0x8CD20000, 0x1100001D, 0xB1881000, 0x02000001, 0x800F0000, 0xB1AA1018, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 26, { 0xFFFF0300, 0x0200001F, 0x8001000A, 0x902F0003, 0x0200001F, 0x80000000, 0x900F0001, 0x0200001F, 0x80000005, 0x901C1000, 0x0200002F, 0xE00F0801, 0xC1C1158F, 0x01000019, 0xA0E416D0, 0x0000001C, 0x0100001E, 0xA0E41002, 0x02000023, 0x80020001, 0x80E40000, 0x01000019, 0xA0E41002, 0x01000019, 0xA0E4117F, 0x0000FFFF } },
    { 27, { 0xFFFF0300, 0x0200001F, 0x8000000D, 0x90101002, 0x05000051, 0xA00F0000, 0x00000080, 0x000000FF, 0x3F800000, 0x273AFD21, 0x03020042, 0x80010000, 0xA01B0000, 0xA1E40801, 0x0200000E, 0x803F0000, 0xACE40002, 0x02020029, 0x80000001, 0xA0000000, 0x02000001, 0x8F6F0000, 0x80C90000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 27, { 0xFFFF0300, 0x01000028, 0x80C91001, 0x03000005, 0x800F000A, 0x81E40005, 0x901B1002, 0x0200001A, 0xA0E41001, 0xD1550801, 0x0000002A, 0x02000043, 0x800F0000, 0xA1550002, 0x0000002B, 0x03000059, 0x8F1F0802, 0xA0E41002, 0xB1551001, 0x03000050, 0x800F0000, 0x90550000, 0xB1001022, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 29, { 0xFFFF0300, 0x0200002F, 0xE0020802, 0x000000FF, 0x0400005A, 0x80010009, 0xA11B0000, 0x90AA1001, 0xA3E40001, 0x0300005F, 0xBF4F101A, 0x80AA0002, 0xA1E40800, 0x0000001C, 0x0100001E, 0xA0E41002, 0x01000041, 0x900F0001, 0x0000001C, 0x0100001E, 0xA0E41001, 0x02000013, 0x8F140002, 0x80FF0001, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 30, { 0xFFFF0300, 0x0200001F, 0x80010005, 0x904F0001, 0x0200001F, 0x80040005, 0x90270002, 0x0200001F, 0x90000000, 0xA00F0800, 0x05000030, 0xF00F0000, 0x00000010, 0x00000000, 0x0000007F, 0x00000000, 0x01000019, 0xA0E41001, 0x02000001, 0x800F0800, 0xA0E40000, 0x0000001C, 0x0100001E, 0xA0E41001, 0x03000009, 0x80180015, 0x8B1B0015, 0x8CE40015, 0x0000001C, 0x0000FFFF } },
    { 31, { 0xFFFF0300, 0x0200001F, 0x80000005, 0x900C1001, 0x0200001F, 0xA8000000, 0xA00F080C, 0x01000040, 0xB00F1003, 0x0000001C, 0x0100001E, 0xA0E41001, 0x03000003, 0x80620000, 0x9CE40004, 0x80E40001, 0x0000001C, 0x0100001E, 0xA0E41001, 0x04000058, 0x800F0001, 0xA11B0800, 0x8BE40000, 0xACE40014, 0x01000041, 0x910F0002, 0x0000001C, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 31, { 0xFFFF0300, 0x05000030, 0x800F00A9, 0x00000080, 0x00000001, 0x0000007F, 0xFFFFFF80, 0x05000051, 0xA00F00D0, 0x00000001, 0xFFFFFF80, 0x00000100, 0x00000000, 0x0200002F, 0xE00F0802, 0xFFFFFF80, 0x05000058, 0x802F0002, 0xA0E41002, 0xA1E40000, 0x90FF2004, 0xD0000000, 0x02000025, 0xB00F1001, 0xF0E40002, 0x02000001, 0xC00F0800, 0x80E40002, 0x0100002A, 0x81E40000, 0x0000FFFF } },
    { 32, { 0xFFFF0300, 0x05000030, 0xF00F0800, 0xFFFFFF80, 0x00000080, 0x3F800000, 0x00000001, 0x05000051, 0xA00F0000, 0x0000007F, 0xFFFFFFFF, 0x0000007F, 0x86BA839F, 0x13000025, 0xB00F1002, 0xB1FF1000, 0x9BFF1000, 0x03000005, 0x810F0000, 0x91AA1000, 0x9D1B1000, 0x0300000A, 0x80030002, 0xA0C91004, 0xF0E40002, 0x05000058, 0xCF0F0002, 0xA0550030, 0xA0E40002, 0x90002000, 0xB0000000, 0x0000FFFF } },
    { 32, { 0xFFFF0300, 0x0200001F, 0x80000005, 0x900F0002, 0x05000051, 0xA00F0110, 0x00000001, 0x000000FF, 0x0000007F, 0x00000000, 0x03000020, 0x81490803, 0xA8E41001, 0x90000002, 0x01000028, 0xE0E40802, 0x0303005E, 0x902F0802, 0x90E41002, 0x91D21005, 0x02000001, 0x800F0800, 0x80E40000, 0x03000017, 0x800F0001, 0x90551001, 0x90551001, 0x02000013, 0xB00F1001, 0x80E42000, 0x0000002B, 0x0000FFFF } },
    { 32, { 0xFFFF0300, 0x0001FFFE, 0x12345678, 0x04000015, 0xA02F1002, 0x91D22000, 0xF0E40801, 0x90E40002, 0x03000002, 0x800F001A, 0xB0550002, 0xA0D2108C, 0x0000001C, 0x0100001E, 0xA0E41001, 0x0200000F, 0x80640000, 0x805500EC, 0x02000001, 0x800F0002, 0xB000004D, 0x0500005D, 0x880F0022, 0x8CD200C9, 0x8CE40001, 0x80E48000, 0xFCE40007, 0x02000001, 0x800F0800, 0x80E40000, 0x0000001C, 0x0000FFFF } },
    { 33, { 0xFFFF0300, 0x0200001F, 0x80000008, 0x900F1001, 0x0200001F, 0xA0000000, 0xA0230800, 0x0200001F, 0x00120008, 0x900B0003, 0x05000051, 0xA00F0000, 0x00000100, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x0200005B, 0xB00F1002, 0x8000003B, 0x0400005A, 0x800F000C, 0xA0551001, 0xE2E40814, 0xA0E40003, 0x0200001A, 0xA0E41001, 0xC0550801, 0x01000042, 0x8F010000, 0x02000023, 0x800F0004, 0xA0C90002, 0x0000FFFF } },
    { 35, { 0xFFFF0300, 0x0200001F, 0x80020000, 0x90271001, 0x0200001F, 0x80010005, 0xE01C0000, 0x0200001F, 0x80000008, 0x900B0000, 0x0200001F, 0x80000009, 0x90170008, 0x0200001F, 0x80000009, 0xB00B0002, 0x0200001F, 0x800E000D, 0xD01F0803, 0x02000001, 0x800F0800, 0x80E40000, 0x04000012, 0x80040000, 0xA0FF1000, 0xA0E40802, 0x80550000, 0x02000024, 0xB00F100E, 0x90E41000, 0x0302005E, 0xB00F1002, 0x9C551001, 0xA0E40826, 0x0000FFFF } },
    { 36, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x02000025, 0x80030000, 0x80000000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 36, { 0xFFFF0300, 0x0200001F, 0x8001000A, 0x90030000, 0x0200001F, 0x80070005, 0x900F0009, 0x0200001F, 0x80020006, 0x90030003, 0x0200001F, 0x80000000, 0x90031000, 0x0200001F, 0xA0000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x801F0004, 0xA0E40000, 0x03000014, 0x800F0004, 0x90E40009, 0xA0E40000, 0x02000001, 0x800F0800, 0x80E40004, 0x0000FFFF } },
    { 37, { 0xFFFF0300, 0x0200001F, 0x80020003, 0x800F1003, 0x0200001F, 0x80010005, 0x900C0000, 0x0200001F, 0x8002000F, 0x90270003, 0x0200001F, 0x8001000B, 0x90110000, 0x0200001F, 0x80000000, 0x900F0000, 0x05000030, 0xF00F0107, 0xC1B34296, 0x00000000, 0x00000001, 0x0000007F, 0x0200002F, 0xC00F0002, 0x3F800000, 0x01000028, 0xB0E41000, 0x03000021, 0x800F0007, 0x806E0006, 0xA0E40917, 0x00000000, 0x0000002B, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 37, { 0xFFFF0300, 0x0200001F, 0x808A0004, 0x900C0003, 0x0200001F, 0x80010005, 0x90210012, 0x0200001F, 0x80000009, 0x902C0000, 0x0200001F, 0x90000000, 0xA0450801, 0x02000006, 0x8027001A, 0xA0D20001, 0x02000043, 0xD01F0811, 0x99C9008A, 0x00000027, 0x01000026, 0xF0E40001, 0x1300000E, 0x800F0800, 0xBDAA1000, 0x90C90000, 0x12000028, 0xB0551000, 0xA1E40002, 0x00000027, 0x0200005C, 0x804F0001, 0xF0E40007, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x80000004, 0x90430003, 0x0200001F, 0x80010004, 0xB0440003, 0x0200001F, 0x80000008, 0x900F1001, 0x0200001F, 0x8000000E, 0x900F1002, 0x0200001F, 0x80000004, 0x902B0000, 0x0200001F, 0x80000002, 0x90040802, 0x04000058, 0x800F0002, 0x801B0004, 0xB0AA1002, 0xA01B1000, 0x0000001C, 0x0100001E, 0xA0E41001, 0x13000024, 0x810F0000, 0xA0550000, 0x81C90000, 0x03000042, 0x800F0000, 0xAD5500A0, 0xA0E40800, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x01000026, 0xF0E40000, 0x0202002D, 0x80000000, 0xA0000000, 0x00000027, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x80030004, 0x900B0001, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0x80000008, 0x900C0000, 0x0200001F, 0x8003000B, 0xA04B0003, 0x0200001F, 0x98000000, 0xA061080A, 0x0200001F, 0x8001000D, 0x900C0001, 0x0200001F, 0x80000003, 0x90470002, 0x0200002F, 0x900F08CC, 0x00000001, 0x01000028, 0xE0550800, 0x03000016, 0xB10F1000, 0xBB550001, 0x90AA0001, 0x15000058, 0xB00F1001, 0xBD551000, 0x80D20002, 0xF0E40000, 0x81E40002, 0x0000002B, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x01000028, 0xE0E40800, 0x02000001, 0x800F0000, 0xA0E40000, 0x0000002B, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x0500005D, 0x800F0003, 0x80E40000, 0xA0E40800, 0x80E40001, 0x80E40002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 39, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F080B, 0x0200001F, 0x90000000, 0xA00F0808, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000001, 0x00000001, 0x0000007F, 0x00000000, 0x05000030, 0xF00F0001, 0x00000010, 0x000000FF, 0x0000007F, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x0304005E, 0xB00F1000, 0xA0E40000, 0xA0E40000, 0x02000001, 0x800F0800, 0xA0E40000, 0x0000FFFF } },
    { 40, { 0xFFFF0300, 0x05000051, 0xA0040002, 0x000000FF, 0x00000100, 0xFFFFFFFF, 0xFFFFFF80, 0x0200000F, 0xB00F1001, 0xACC90004, 0x02000001, 0x803F0015, 0xA0E41000, 0x01000060, 0xA0F500D3, 0x0200001A, 0xA0E41001, 0xBD001000, 0x03000009, 0xB00F0000, 0x81550000, 0x80FF0113, 0x04000015, 0x8F040002, 0x90E42005, 0xF0E40800, 0x887B0000, 0x04000021, 0x900F1000, 0x9C1B2001, 0xF0C80800, 0x80000002, 0x03000020, 0x8E0F0802, 0x80E40000, 0xA0FF1024, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 40, { 0xFFFF0300, 0x0001FFFE, 0x12345678, 0x0300001F, 0x80020008, 0x904F1001, 0x0200001F, 0x8000000D, 0x905F0003, 0x0200001F, 0x8004000A, 0x900F0000, 0x0200001F, 0x8000000A, 0x90110005, 0x0200001F, 0x80030005, 0x90440008, 0x0200001F, 0x80000005, 0xA02B1000, 0x05000051, 0xA00F0002, 0xFFFFFF80, 0x00000001, 0xFFFFFF80, 0x0000007F, 0x05000051, 0xA01B0002, 0x0000007F, 0x9F9206C2, 0x00000001, 0x00000001, 0x0200004E, 0x80080005, 0x80E40002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 40, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x0200001B, 0xF0E40800, 0xF0E40000, 0x02000001, 0x800F0000, 0xA0E40000, 0x0000001D, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 40, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x02010029, 0x80000000, 0xA0000000, 0x02000001, 0x800F0000, 0xA0E40000, 0x0000002B, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 41, { 0xFFFF0300, 0x0200001F, 0x80000009, 0x900B0002, 0x0200001F, 0x800A0009, 0x90011000, 0x0200001F, 0x80000004, 0x9003000C, 0x02000001, 0x814B0000, 0xB04B0007, 0x02000001, 0x902F0805, 0xCBAA084E, 0x0000001C, 0x0100001E, 0xA0E41001, 0x01000041, 0x900F001D, 0x01000019, 0xA0E414CD, 0x03000013, 0x900F1002, 0x91E42000, 0xF0E40800, 0x0100001E, 0xA0E41000, 0x03010042, 0xB06F0005, 0x9055002D, 0xA0E40897, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001E, 0xF0000801, 0x82000001, 0x0000001C, 0x0000FFFF } },
    { 42, { 0xFFFF0300, 0x0200001F, 0x80010000, 0x90020002, 0x0200001F, 0x80000005, 0x90640002, 0x0200001F, 0xD8000000, 0xA0530807, 0x05000051, 0xA00F0000, 0x0000007F, 0x0000007F, 0x39BE224C, 0x648A5064, 0x05000030, 0xF00F0001, 0x0000007F, 0x00000080, 0x3F800000, 0x00000000, 0x05000051, 0xA00F0000, 0x2F1AA0D9, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0x02000024, 0x80040002, 0x8B8B0002, 0x03020042, 0x80081022, 0xBDE40001, 0x80E40003, 0x02000001, 0x800F0000, 0x80E40001, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 42, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x02000025, 0x80030001, 0x80000000, 0x02000025, 0x80030001, 0x80000000, 0x02000025, 0x80030001, 0x80000000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 43, { 0xFFFF0300, 0x05000030, 0xF00F0001, 0x00000080, 0xFFFFFFFF, 0x0000007F, 0x3F800000, 0x05000030, 0xF00F0001, 0x3F800000, 0x00000100, 0x000000FF, 0x000000FF, 0x0200005C, 0x80171001, 0x801B0001, 0x04000014, 0x800B0001, 0x9B9000D8, 0x9C002000, 0xF0E40800, 0x0303005E, 0xB03F1001, 0xF0D20818, 0xA0FF0001, 0x03000009, 0x802F0001, 0xBCAA1001, 0xE6C90805, 0x03000042, 0x806F8000, 0xA11B0006, 0xA04C0802, 0x01000019, 0xA0E412F8, 0x03000005, 0x802F0802, 0x80C90000, 0xF0E40002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 43, { 0xFFFF0300, 0x0200001F, 0x800A0009, 0x900B0002, 0x0200001F, 0x80020004, 0x90030000, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0x8000000A, 0x902D0003, 0x05000030, 0xF00F0002, 0xFFFFFF80, 0x00000000, 0x00000001, 0x3F800000, 0x0200002F, 0xE02F0800, 0x00000001, 0x0200005B, 0x806F0007, 0xA0E40898, 0x03000059, 0xA00F1000, 0x90E40001, 0xB1E40002, 0x02000024, 0xB0001027, 0x81550001, 0x0500000C, 0x800F0003, 0x81E40800, 0xA0D2209F, 0x8DE40000, 0xA0E40014, 0x03000003, 0x9047081D, 0xA0E41002, 0x80E40020, 0x0000FFFF } },
    { 44, { 0xFFFF0300, 0x0200001F, 0x80030004, 0x90010003, 0x0200001F, 0x80030004, 0x900B0000, 0x0200001F, 0x8000000A, 0x90240003, 0x05000051, 0xA00F00BE, 0xEAF1DC51, 0x3F800000, 0x00000001, 0x0000007F, 0x02000001, 0x800F0800, 0x80E40000, 0x05000051, 0xA00F0000, 0x000000FF, 0x00000000, 0x000000FF, 0xFFFFFFFF, 0x05000051, 0xA0020055, 0x0000007F, 0xFFFFFF80, 0x00000100, 0x3F800000, 0x02000007, 0xA0331002, 0xAAE40001, 0x0120002E, 0x90E41005, 0x04000003, 0x820F0000, 0xA0E40801, 0x90D22002, 0xF00F0801, 0x01000019, 0xA0E41000, 0x0000FFFF } },
    { 45, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x80000008, 0x900F1001, 0x0200001F, 0x800D0009, 0x900F1001, 0x0200001F, 0x8003000C, 0x80640001, 0x0200001F, 0x80000000, 0xA04F080A, 0x03000017, 0x810F0800, 0x80FF0029, 0xE1E40858, 0x14000015, 0x902F0804, 0xB00F1000, 0xBDD20001, 0x80AA0000, 0x01000019, 0xA0E41001, 0x01000019, 0xA0E41002, 0x01000041, 0x806F0000, 0x03000042, 0x90080012, 0xA0C90007, 0xA0E40800, 0x01000019, 0x8CE40113, 0x02000016, 0xB0030002, 0x90E40000, 0x80E40000, 0x0200001A, 0xA0E41001, 0xE0E40800, 0x0000FFFF } },
    { 48, { 0xFFFF0300, 0x0200001F, 0x80000003, 0x900B0000, 0x0200001F, 0x80000005, 0x900F0001, 0x0200002F, 0xE00F0800, 0x00000001, 0x0200002F, 0xE00F0800, 0x3F800000, 0x0200002F, 0xE00F0800, 0x000000FF, 0x0400005A, 0x80020015, 0xF0E40002, 0xB1E40000, 0xB0E40001, 0x14000021, 0xB01F0000, 0xB1B61000, 0x84000000, 0x8DE40001, 0x0300005F, 0xB00B1002, 0xA0D20001, 0xA0E40816, 0x0200001A, 0xA0670000, 0x81E40000, 0x0300000C, 0xB00F1000, 0xAB1B1002, 0xE1E40800, 0x14000058, 0x9D0F0000, 0x901B0002, 0x90001000, 0xF0FF086C, 0x5400000B, 0x804F0002, 0xB0FF1000, 0xF10008DA, 0x800D0000, 0x0000FFFF } },
    { 51, { 0xFFFF0300, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x00000000, 0x00000000, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000001, 0x800F0000, 0xA0E40000, 0x0301005E, 0xB00F1000, 0x80E40000, 0xA0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x01000026, 0xF0E40000, 0x0202002D, 0x80000000, 0xA0000000, 0x00000027, 0x00000027, 0x00000027, 0x00000027, 0x00000027, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 51, { 0xFFFF0300, 0x0200001F, 0x80040005, 0x902F0002, 0x0200001F, 0x80000003, 0x900F0003, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F0806, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000010, 0x000000FF, 0x00000001, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x0200005C, 0x800F0002, 0xA0E40000, 0x04000058, 0x80010002, 0x80E40002, 0x8BE40002, 0x90E40002, 0x0300005F, 0x800F0013, 0x80E40002, 0xA0E40806, 0x03010042, 0x800F0017, 0x80E40013, 0xA0E40806, 0x02000001, 0x800F0800, 0x80E40013, 0x02000001, 0x800F0801, 0x80E40017, 0x0000FFFF } },
    { 51, { 0xFFFF0300, 0x0200001F, 0x80050005, 0x90410005, 0x0200001F, 0x80010006, 0x900F0002, 0x0200001F, 0x80030005, 0x90010000, 0x0200001F, 0x8006000A, 0x904F0003, 0x0200001F, 0x80000000, 0x90031000, 0x0200001F, 0x90000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000004, 0x000000FF, 0x00000001, 0x00000000, 0x05000030, 0xF00F0001, 0x00000010, 0x000000FF, 0xFFFFFFFF, 0x00000000, 0x04000004, 0x800F0007, 0x80E40007, 0x80E40007, 0x80E40007, 0x03000005, 0x80010007, 0x80E40007, 0x90E40000, 0x03000005, 0x80070009, 0x80E40009, 0x801B0009, 0x0000FFFF } },
    { 52, { 0xFFFF0300, 0x0200001F, 0x80040005, 0x90430009, 0x0200001F, 0x80060006, 0x90470002, 0x0200001F, 0x80020006, 0x902F0004, 0x0200001F, 0x98000000, 0xA00F0800, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000004, 0x000000FF, 0xFFFFFFFF, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x02000001, 0x8008000E, 0x801B000E, 0x0200005B, 0x800F0013, 0x80E4000E, 0x01000041, 0x800F0013, 0x02000001, 0x800F0800, 0x80E40013, 0x02000001, 0x800F0801, 0x80E4000E, 0x02000001, 0x800F0802, 0x80E40013, 0x0000FFFF } },
    { 53, { 0xFFFF0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x80000000, 0xB06F0002, 0x0200002F, 0xE00F0830, 0x8C8A58A8, 0x01000019, 0xA0E41040, 0x0100002C, 0xACFF1001, 0x0300000A, 0x800F0001, 0xAB000004, 0xACE4080D, 0x04000025, 0x80330000, 0x81FF0037, 0x8DE40001, 0xB0E400DC, 0x02010029, 0x80550001, 0x90E41001, 0x02000023, 0xB00F0002, 0xACE40802, 0x0200001B, 0xA100000D, 0xAC380001, 0x01000019, 0xA0E41001, 0x0000002B, 0x0200000F, 0x800F4000, 0xADE40002, 0x0500005D, 0xB06F1021, 0xA0000002, 0xA0FF0080, 0x80E40023, 0x90FF0002, 0x02000001, 0xA00F1000, 0x8B550002, 0x02000024, 0x801F0001, 0x83090000, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 55, { 0xFFFF0300, 0x0200001F, 0x8005000B, 0x900F0000, 0x0200001F, 0x8001000D, 0x90440002, 0x0200001F, 0x80040009, 0x900C0002, 0x0200002F, 0xF00F0000, 0x2C7C668B, 0x03000018, 0xA00F1001, 0xB1FF0000, 0x8BE40000, 0x14000009, 0xB10F1000, 0x801C0000, 0x971D1001, 0x90E40006, 0x03000016, 0x80030000, 0x80E40000, 0xB0E40000, 0x03120042, 0x806F0800, 0xA0E4081C, 0xACE40001, 0x02040029, 0x80000001, 0x90001000, 0x04000058, 0x80280000, 0xF0E400D2, 0x80550000, 0x90001000, 0x0000002B, 0x0300000A, 0xB01F0001, 0xABE41001, 0xB0551086, 0x0200002A, 0xB1AA1000, 0x80E40003, 0x13000024, 0x80010004, 0xBD001000, 0xB11B1012, 0x03000003, 0xB0010000, 0xA0E40800, 0xD0F20000, 0x0000FFFF } },
    { 55, { 0xFFFF0300, 0x0200001F, 0x80060006, 0x90410007, 0x0200001F, 0x80020005, 0x904F0004, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x90000000, 0xA00F080B, 0x0200001F, 0xA0000000, 0xA00F080D, 0x0200001F, 0x90000000, 0xA00F0801, 0x0200001F, 0x90000000, 0xA00F0808, 0x05000030, 0xF00F0000, 0x00000010, 0x00000001, 0x0000007F, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000000, 0x0300000A, 0x800F0019, 0x8BC60019, 0x8B1B0019, 0x0300000A, 0x8023001F, 0x90000004, 0x80E40019, 0x02000023, 0x8003001D, 0x80550019, 0x02000001, 0x800F0800, 0x80E4001F, 0x02000001, 0x800F0801, 0x80E4001D, 0x02000001, 0x800F0802, 0x80E4001D, 0x02000001, 0x800F0803, 0x80E4001D, 0x0000FFFF } },
    { 55, { 0xFFFF0300, 0x0200001F, 0x80060005, 0x902F0005, 0x0200001F, 0x8007000A, 0x90430002, 0x0200001F, 0x8004000A, 0x90430008, 0x0200001F, 0x80000000, 0x90031000, 0x0200001F, 0x80000000, 0x900F1001, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F080F, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000010, 0x000000FF, 0xFFFFFFFF, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x0303005E, 0xB00F1000, 0x90000002, 0xA0E40000, 0x02000001, 0x80010004, 0xA0550000, 0x03010042, 0x800F0017, 0x80E40004, 0xA0E40800, 0x0000FFFF } },
    { 56, { 0xFFFF0300, 0x0200001F, 0x80000000, 0x90010002, 0x0200001F, 0x90000000, 0xA02B0800, 0x05000030, 0xB00F1001, 0x0000007F, 0x00000080, 0x00000100, 0xCFC51144, 0x05000030, 0xF00F0001, 0xFFFFFF80, 0xFFFFFFFF, 0x00000100, 0x00000080, 0x03000021, 0x900F0002, 0x90C90000, 0xA0551000, 0x03000021, 0x800F0002, 0xA0E41115, 0x98E41005, 0x03000002, 0x904F0001, 0x90D200F6, 0xA0AA0000, 0x03000015, 0x90EF0800, 0x81AA00F1, 0x81E40002, 0x03020042, 0x800F0000, 0x9DFF0036, 0xA0550802, 0x03000042, 0xD00F0806, 0xA0E40800, 0xA0E40800, 0x0000001C, 0x0100001E, 0xA0E41001, 0x0300000A, 0xB34F1023, 0xA0E40807, 0x90E41001, 0x03000005, 0x900F0811, 0x90E41001, 0xF0D20801, 0x0000001C, 0x0000FFFF } },
    { 56, { 0xFFFF0300, 0x02000001, 0x800F0800, 0x80E40000, 0x0200002F, 0xE00F0800, 0xFFFFFFFF, 0x0200002F, 0xE00F0802, 0x000000FF, 0x05000030, 0xF00F0001, 0x00000100, 0x37321F37, 0x00000001, 0x3F800000, 0x0303005E, 0x800F0001, 0xFDE40001, 0xA0D21001, 0x02000006, 0x8B0F0800, 0x8CFF0004, 0x02000001, 0x80BF0005, 0x80E40002, 0x1300005C, 0x800F0802, 0xBD001000, 0x803D0006, 0x01000041, 0x803F0000, 0x0200001A, 0xA0E41000, 0xB0E41000, 0x0500005D, 0x80070000, 0xA0C90804, 0xADE40802, 0x80FF0123, 0x90E41002, 0x03000015, 0xB00F1002, 0xB1E40014, 0x8B1B0024, 0x03000016, 0x90340002, 0xC0550801, 0xA0E40000, 0x01000041, 0x800F0001, 0x03000003, 0x802F0000, 0xA0E40006, 0xA0E41013, 0x0000FFFF } },
    { 56, { 0xFFFF0300, 0x0200001F, 0x80000000, 0x910F1001, 0x0200001F, 0x90000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F0803, 0x0200001F, 0xA0000000, 0xA00F0804, 0x0200001F, 0x98000000, 0xA00F080F, 0x0200001F, 0xA0000000, 0xA00F0801, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x000000FF, 0x00000001, 0xFFFFFFFF, 0x00000000, 0x05000030, 0xF00F0001, 0x00000004, 0x000000FF, 0x0000007F, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x02000007, 0x800F001C, 0xAB550000, 0x02000001, 0x800F0800, 0x80E4001C, 0x02000001, 0x800F0801, 0x80E4001C, 0x02000001, 0x800F0802, 0x80E4001C, 0x02000001, 0x800F0803, 0x80E4001C, 0x0000FFFF } },
    { 57, { 0xFFFF0300, 0x01000028, 0xE0E4080C, 0x03000014, 0x800F0009, 0x80FF0002, 0xFDE40001, 0x03000021, 0x80020000, 0x80E40002, 0x8BE40018, 0x0000002A, 0x1100002A, 0xBD001000, 0x0000002B, 0x02000001, 0x800F0800, 0x80E40000, 0x0320000A, 0xA0131002, 0xA0E41000, 0xABE41001, 0x02000043, 0x80030000, 0xA01B0000, 0x05000022, 0x801F0000, 0x9BFF2021, 0xF0CD0801, 0xB0E41022, 0x80550000, 0x0305005E, 0xE00F0001, 0xA0E40001, 0x8CFF0001, 0x02000023, 0xB00F1001, 0x90000001, 0x02000001, 0x810F0801, 0x90E41000, 0x0200005C, 0xF00F000D, 0xA1E400E8, 0x0300000C, 0x90230800, 0xBCE40000, 0xB0E40002, 0x03000002, 0xB01F0003, 0xA0E40005, 0x8D0000D1, 0x03000023, 0x900F0804, 0x80E40001, 0x8BC90000, 0x0000FFFF } },
    { 58, { 0xFFFF0300, 0x0200001F, 0x80070005, 0x90210000, 0x0200001F, 0x80020003, 0x900F0008, 0x0200001F, 0x80000000, 0x90031000, 0x0200001F, 0xA0000000, 0xA00F0800, 0x0200001F, 0xA0000000, 0xA00F0808, 0x0200001F, 0x90000000, 0xA00F0803, 0x05000030, 0xF00F0000, 0x00000004, 0x00000000, 0x0000007F, 0x00000000, 0x00000000, 0x01000019, 0xA0E41003, 0x03000002, 0x800F0010, 0x80A00010, 0x90E40008, 0x01000041, 0x800F0010, 0x00000000, 0x03000042, 0x802F0017, 0x801B0010, 0xA0E40800, 0x02000001, 0x800F0800, 0x80E40017, 0x02000001, 0x800F0801, 0x80E40010, 0x02000001, 0x800F0802, 0x80E40010, 0x0000001C, 0x0100001E, 0xA0E41003, 0x0400005A, 0x80070017, 0x811E0010, 0xA1550000, 0xA0AA0000, 0x0000001C, 0x0000FFFF } },
    { 59, { 0xFFFF0300, 0x0200001F, 0x80000004, 0x900F1000, 0x0200001F, 0x80000000, 0xA00F0800, 0x02000001, 0x800F0800, 0x80E40000, 0x0200001F, 0x8003000A, 0x90110000, 0x0200001F, 0x90000000, 0xA0030800, 0x0200001F, 0x800F000A, 0x90030013, 0x0200001F, 0x80020007, 0x90011002, 0x03000016, 0xB00F0000, 0x80E40002, 0x91E40000, 0x01000028, 0xE0E40800, 0x0700005D, 0x800F0000, 0xA0E40002, 0x90E42000, 0xF0E40801, 0x80E40004, 0x95E4210A, 0xA0550800, 0x0000002B, 0x0305005E, 0xA00F1002, 0x80000014, 0x80000001, 0x03000005, 0x900F0802, 0xBB001002, 0xB0C90000, 0x54000017, 0xB01F1002, 0xA0550000, 0x9D1B0000, 0xEBC90800, 0x03000059, 0xB02F1000, 0x801B0002, 0xF1E40800, 0x03000016, 0x80010001, 0x90E40000, 0xF0E40002, 0x0000FFFF } },
    { 60, { 0xFFFF0300, 0x0001FFFE, 0x12345678, 0x0200001F, 0x8002000D, 0x90410003, 0x0200001F, 0x8000000A, 0x900F0002, 0x03000015, 0xB00F1000, 0xACE40002, 0xB1551000, 0x03010042, 0x800F0000, 0x8DFF0001, 0xA0E40800, 0x04000058, 0x806F0004, 0xBFE40080, 0xACE40000, 0x80550001, 0x0200001B, 0xF0E40800, 0xF0E40001, 0x01000040, 0x904F081D, 0x03000021, 0x806F0000, 0x80B80000, 0xAE000005, 0x03000020, 0x800F001D, 0xA0550001, 0xB0FF1000, 0x02000013, 0x8F010000, 0xB0E40020, 0x0000001C, 0x0100001E, 0xA0E41001, 0x0300004E, 0x800F0001, 0x90AA2002, 0xF0E90800, 0x0400005A, 0x801F0002, 0xB1D21001, 0x80E400C1, 0x8DE40002, 0x0200005B, 0x905B0801, 0xA0E40800, 0x0000001C, 0x0100001E, 0xA0E41001, 0x01000041, 0xB0011001, 0x0000001C, 0x0000FFFF } },
    { 60, { 0xFFFF0300, 0x0200001F, 0x80000005, 0x90010008, 0x0200001F, 0x80000000, 0x90031000, 0x0200001F, 0x98000000, 0xA00F0800, 0x0200001F, 0x98000000, 0xA00F080F, 0x0200001F, 0x90000000, 0xA00F0805, 0x0200001F, 0x98000000, 0xA00F080B, 0x05000051, 0xA00F0000, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000051, 0xA00F0001, 0x3F800000, 0x00000000, 0x3F000000, 0x40000000, 0x05000030, 0xF00F0000, 0x00000010, 0x00000001, 0xFFFFFFFF, 0x00000000, 0x0200002F, 0xE00F0800, 0x00000001, 0x00000000, 0x02000001, 0x800F0013, 0x8CE40013, 0x02000006, 0x8001000C, 0x90000008, 0x02000001, 0x800F0800, 0x80E4000C, 0x02000001, 0x800F0801, 0x80E4000C, 0x02000001, 0x800F0802, 0x80E40013, 0x02000001, 0x800F0803, 0x80E40013, 0x0000FFFF } },
    { 15, { 0xFFFF03FF, 0x0200001F, 0x800E0000, 0xB04C0002, 0x0200001F, 0x80000005, 0x900F1000, 0x0C000058, 0xA00F1000, 0xA0C91002, 0xA0550001, 0xA0E410CB, 0x01000041, 0x91018001, 0x0000FFFF } },
    { 48, { 0xFFFF03FF, 0x0200001F, 0xA0000000, 0xA04F0803, 0x0200001F, 0x80010005, 0x90011000, 0x0200001F, 0xC8000000, 0xA0040801, 0x0200001F, 0x800F000A, 0x90270003, 0x0200001B, 0xF0E40800, 0xF0E40005, 0x02000001, 0x803F0002, 0x80E40009, 0x04000025, 0x90010801, 0xB0E40000, 0x8C080000, 0x81E40000, 0x01000040, 0x800F0000, 0x03000002, 0x9F0F1011, 0xD0E40000, 0xBDC90003, 0x03000059, 0x970F0022, 0xB0E40000, 0x80E40002, 0x03000059, 0x800F0021, 0xF0550801, 0x90E4000A, 0x03000009, 0x800F0002, 0x80EE0056, 0xACE40000, 0x0100001E, 0x80AA0002, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
    { 10, { 0xFFFF0400, 0x0001FFFE, 0x12345678, 0x02000010, 0xA00F1002, 0xB1E40005, 0x02000001, 0x800F0800, 0x80E40000, 0x0000FFFF } },
};

/* IDirect3DShaderValidator9 as d3d9.dll exposes it */
typedef HRESULT (WINAPI* PFN_NC_VALIDATOR_SINK)(PCSTR, INT, DWORD, DWORD, PCSTR, PVOID);

struct NcShaderValidator;

struct NcShaderValidatorVtbl
{
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(NcShaderValidator*, REFIID, void**);
    ULONG (STDMETHODCALLTYPE* AddRef)(NcShaderValidator*);
    ULONG (STDMETHODCALLTYPE* Release)(NcShaderValidator*);
    HRESULT (STDMETHODCALLTYPE* Begin)(NcShaderValidator*, PFN_NC_VALIDATOR_SINK, PVOID, DWORD);
    HRESULT (STDMETHODCALLTYPE* Instruction)(NcShaderValidator*, PCSTR, INT, const DWORD*, DWORD);
    HRESULT (STDMETHODCALLTYPE* End)(NcShaderValidator*);
};

struct NcShaderValidator
{
    const NcShaderValidatorVtbl* Vtbl;
};

typedef NcShaderValidator* (WINAPI* PFN_NC_VALIDATOR_CREATE)(VOID);

/* Messages one validation produced: their numbers and levels, and a hash of where and what they said */
struct NcValidatorLog
{
    char Ids[1024];
    DWORD Hash;
};

static
HRESULT
WINAPI
NcValidatorSink(
    _In_opt_z_ PCSTR File,
    _In_ INT Line,
    _In_ DWORD Level,
    _In_ DWORD Id,
    _In_z_ PCSTR Text,
    _In_opt_ PVOID Context)
{
    NcValidatorLog* Log = static_cast<NcValidatorLog*>(Context);
    char Item[32];

    _snprintf_s(Item, sizeof(Item), _TRUNCATE, " %lu:%lX", Id, Level);
    strncat_s(Log->Ids, sizeof(Log->Ids), Item, _TRUNCATE);
    Log->Hash = NcHashState(Text, static_cast<UINT>(strlen(Text))) ^ (Log->Hash * 31);
    if (File != nullptr)
        Log->Hash ^= NcHashState(File, static_cast<UINT>(strlen(File))) + static_cast<DWORD>(Line);
    return S_OK;
}

/* Token count of the instruction starting at a position, as a caller handing them over one by one counts them */
static
UINT
NcInstructionLength(
    _In_ const DWORD* Token,
    _In_ DWORD Version,
    _In_ BOOLEAN First)
{
    DWORD Opcode = Token[0] & 0xFFFF;
    UINT Count = 1;

    if (First || Opcode == 0xFFFF)
        return 1;
    if (Opcode == 0xFFFE)
        return ((Token[0] >> 16) & 0x7FFF) + 1;
    if ((Version & 0xFFFF) >= 0x200)
        return ((Token[0] >> 24) & 0xF) + 1;
    while ((Token[Count] & 0x80000000) != 0)
        Count += 1;
    return Count;
}

/* Feeds a shader through the validator object one instruction at a time */
static
VOID
NcValidateOnce(
    _In_ NcShaderValidator* Validator,
    _In_ const NcShaderCase* Case,
    _In_ DWORD Flags,
    _Out_ NcValidatorLog* Log,
    _Out_writes_(256) PSTR Results)
{
    UINT Position = 0;
    char Item[32];
    HRESULT hr;

    ZeroMemory(Log, sizeof(*Log));
    hr = Validator->Vtbl->Begin(Validator, NcValidatorSink, Log, Flags);
    _snprintf_s(Results, 256, _TRUNCATE, "%08lX", hr);
    while (Position < Case->Count)
    {
        UINT Length = NcInstructionLength(&Case->Tokens[Position], Case->Tokens[0], Position == 0);

        if (Position + Length > Case->Count)
            Length = Case->Count - Position;
        hr = Validator->Vtbl->Instruction(Validator, "shader", static_cast<INT>(Position), &Case->Tokens[Position],
                                          Length);
        if (hr != S_OK)
        {
            _snprintf_s(Item, sizeof(Item), _TRUNCATE, " %u:%08lX", Position, hr);
            strncat_s(Results, 256, Item, _TRUNCATE);
        }
        if (Position != 0 && (Case->Tokens[Position] & 0xFFFF) == 0xFFFF)
            break;
        Position += Length;
    }
    hr = Validator->Vtbl->End(Validator);
    _snprintf_s(Item, sizeof(Item), _TRUNCATE, " end %08lX", hr);
    strncat_s(Results, 256, Item, _TRUNCATE);
}

/* The standalone validator object: its interface, the order its methods must come in, and every corpus shader with
   and without messages about the compile target */
static
VOID
NcCheckValidatorObject(
    _Inout_ NineCheck* Check)
{
    static const GUID Iid = { 0x814D5A8F, 0x5A68, 0x47F4, { 0xBC, 0xC6, 0xD4, 0xD9, 0x10, 0xB2, 0xA9, 0xE5 } };
    static const DWORD Version = 0xFFFE0200;
    static const DWORD Mov[] = { 0x02000001, 0xC00F0000, 0xA0E40000 };
    PFN_NC_VALIDATOR_CREATE Create =
        reinterpret_cast<PFN_NC_VALIDATOR_CREATE>(GetProcAddress(Check->Module, "Direct3DShaderValidatorCreate9"));
    NcShaderValidator* Validator;
    void* Other = nullptr;
    NcValidatorLog Log = {};
    char Results[256];

    printf("validator_create = %s\n", Create != nullptr ? "found" : "missing");
    if (Create == nullptr)
        return;
    Validator = Create();
    printf("validator_object = %s\n", Validator != nullptr ? "made" : "null");
    if (Validator == nullptr)
        return;

    printf("validator_qi_unknown = %08lX\n", Validator->Vtbl->QueryInterface(Validator, IID_IUnknown, &Other));
    if (Other != nullptr)
        printf("validator_qi_release = %lu\n", Validator->Vtbl->Release(Validator));
    Other = nullptr;
    printf("validator_qi_self = %08lX %u\n", Validator->Vtbl->QueryInterface(Validator, Iid, &Other),
           Other == Validator);
    if (Other != nullptr)
        printf("validator_qi_self_release = %lu\n", Validator->Vtbl->Release(Validator));
    Other = Validator;
    printf("validator_qi_device = %08lX %u\n",
           Validator->Vtbl->QueryInterface(Validator, __uuidof(IDirect3DDevice9), &Other), Other == nullptr);
    printf("validator_addref = %lu\n", Validator->Vtbl->AddRef(Validator));
    printf("validator_release = %lu\n", Validator->Vtbl->Release(Validator));

    /* Calls out of order, with the messages they leave */
    printf("validator_end_first = %08lX\n", Validator->Vtbl->End(Validator));
    printf("validator_instruction_first = %08lX\n", Validator->Vtbl->Instruction(Validator, nullptr, 0, &Version, 1));
    printf("validator_begin = %08lX\n", Validator->Vtbl->Begin(Validator, NcValidatorSink, &Log, 0));
    printf("validator_begin_again = %08lX\n", Validator->Vtbl->Begin(Validator, NcValidatorSink, &Log, 0));
    printf("validator_null_instruction = %08lX\n", Validator->Vtbl->Instruction(Validator, nullptr, 0, nullptr, 1));
    printf("validator_after_error = %08lX\n", Validator->Vtbl->Instruction(Validator, nullptr, 0, &Version, 1));
    printf("validator_end_error = %08lX\n", Validator->Vtbl->End(Validator));
    printf("validator_begin_after_error = %08lX\n", Validator->Vtbl->Begin(Validator, NcValidatorSink, &Log, 0));
    printf("validator_version = %08lX\n", Validator->Vtbl->Instruction(Validator, nullptr, 0, &Version, 2));
    printf("validator_mov = %08lX\n", Validator->Vtbl->Instruction(Validator, "f", 7, Mov, _countof(Mov)));
    printf("validator_end_missing = %08lX\n", Validator->Vtbl->End(Validator));
    printf("validator_order_log =%s %08lX\n", Log.Ids, Log.Hash);
    ZeroMemory(&Log, sizeof(Log));
    printf("validator_order_begin = %08lX\n", Validator->Vtbl->Begin(Validator, NcValidatorSink, &Log, 0));
    printf("validator_bad_version = %08lX\n", Validator->Vtbl->Instruction(Validator, nullptr, 0, Mov, 1));
    printf("validator_bad_version_end = %08lX\n", Validator->Vtbl->End(Validator));
    printf("validator_bad_version_log =%s %08lX\n", Log.Ids, Log.Hash);
    printf("validator_final_release = %lu\n", Validator->Vtbl->Release(Validator));

    for (UINT Index = 0; Index < _countof(NcValidatorCorpus); Index++)
    {
        for (DWORD Flags = 0; Flags < 2; Flags++)
        {
            Validator = Create();
            if (Validator == nullptr)
                return;
            NcValidateOnce(Validator, &NcValidatorCorpus[Index], Flags, &Log, Results);
            printf("validator[%u][%lu] = %s |%s %08lX\n", Index, Flags, Results, Log.Ids, Log.Hash);
            Validator->Vtbl->Release(Validator);
        }
    }
}

/* Shader creation on devices of each kind of vertex processing checks the corpus against the device's caps */
static
VOID
NcCheckValidatorDevices(
    _Inout_ NineCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        DWORD Behavior;
    } Cases[] =
    {
        { "hw", D3DCREATE_HARDWARE_VERTEXPROCESSING },
        { "sw", D3DCREATE_SOFTWARE_VERTEXPROCESSING },
        { "mixed", D3DCREATE_MIXED_VERTEXPROCESSING },
    };
    IDirect3DDevice9* Devices[_countof(Cases)] = {};
    IDirect3D9* Plain = Check->Create(D3D_SDK_VERSION);
    HWND Window = NcCreateWindow(TRUE);

    for (UINT Kind = 0; Kind < _countof(Cases); Kind++)
    {
        D3DPRESENT_PARAMETERS Parameters = {};

        Parameters.Windowed = TRUE;
        Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        Parameters.hDeviceWindow = Window;
        Parameters.BackBufferWidth = 16;
        Parameters.BackBufferHeight = 16;
        printf("validator_device_%s = %08lX\n", Cases[Kind].Name,
               Plain->CreateDevice(0, D3DDEVTYPE_HAL, Window, Cases[Kind].Behavior, &Parameters, &Devices[Kind]));
    }
    for (UINT Index = 0; Index < _countof(NcValidatorCorpus); Index++)
    {
        const NcShaderCase* Case = &NcValidatorCorpus[Index];
        BOOLEAN Pixel = (Case->Tokens[0] >> 16) == 0xFFFF;
        HRESULT Results[_countof(Cases)][2] = {};

        for (UINT Kind = 0; Kind < _countof(Cases); Kind++)
        {
            IDirect3DVertexShader9* Vertex = nullptr;
            IDirect3DPixelShader9* PixelShader = nullptr;

            if (Devices[Kind] == nullptr)
                continue;
            if (!Pixel)
                Results[Kind][0] = Devices[Kind]->CreateVertexShader(Case->Tokens, &Vertex);
            Results[Kind][1] = Devices[Kind]->CreatePixelShader(Case->Tokens, &PixelShader);
            if (Vertex != nullptr)
                Vertex->Release();
            if (PixelShader != nullptr)
                PixelShader->Release();
        }
        printf("validator_shader[%u] = %08lX %08lX %08lX %08lX %08lX %08lX\n", Index, Results[0][0], Results[0][1],
               Results[1][0], Results[1][1], Results[2][0], Results[2][1]);
    }
    for (UINT Kind = 0; Kind < _countof(Cases); Kind++)
    {
        if (Devices[Kind] != nullptr)
            printf("validator_device_%s_release = %lu\n", Cases[Kind].Name, Devices[Kind]->Release());
    }
    DestroyWindow(Window);
    Plain->Release();
}

static
VOID
NcCheckValidator(
    _Inout_ NineCheck* Check)
{
    NcCheckValidatorObject(Check);
    NcCheckValidatorDevices(Check);
}

static const NineCheckInfo NcChecks[] =
{
    { "exports", NcCheckExports },
    { "interfaces", NcCheckInterfaces },
    { "identifier", NcCheckIdentifier },
    { "modes", NcCheckModes },
    { "modes_plain", NcCheckModesPlain },
    { "modes_ex", NcCheckModesEx },
    { "caps", NcCheckCaps },
    { "caps_plain", NcCheckCapsPlain },
    { "devtype", NcCheckDeviceType },
    { "formats", NcCheckFormats },
    { "multisample", NcCheckMultisample },
    { "depthmatch", NcCheckDepthMatch },
    { "create", NcCheckCreate },
    { "device", NcCheckDevice },
    { "device_plain", NcCheckDevicePlain },
    { "surfaces", NcCheckSurfaces },
    { "buffers", NcCheckBuffers },
    { "textures", NcCheckTextures },
    { "reset", NcCheckReset },
    { "resources", NcCheckResources },
    { "copies", NcCheckCopies },
    { "gamma", NcCheckGamma },
    { "shared", NcCheckShared },
    { "cursor", NcCheckCursor },
    { "presentstats", NcCheckPresentStats },
    { "states", NcCheckStates },
    { "constants", NcCheckConstants },
    { "blocks", NcCheckBlocks },
    { "shaders", NcCheckShaders },
    { "draws", NcCheckDraws },
    { "ffpixel", NcCheckFixedPixel },
    { "ffvertex", NcCheckFixedVertex },
    { "validator", NcCheckValidator },
};

static
std::wstring
NcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"d3d9.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\d3d9.dll";
    }

    return Runtime;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Runtime = NcRuntimePath(L"system");
    std::vector<std::string> Wanted;
    NineCheck Check;
    char Name[64];
    HRESULT hr;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = NcRuntimePath(argv[++Index]);
            continue;
        }
        WideCharToMultiByte(CP_UTF8, 0, argv[Index], -1, Name, sizeof(Name), nullptr, nullptr);
        Wanted.push_back(Name);
    }

    Check.Module = LoadLibraryW(Runtime.c_str());
    if (Check.Module == NULL)
    {
        fprintf(stderr, "error: cannot load %ls (%lu)\n", Runtime.c_str(), GetLastError());
        return 1;
    }
    {
        WCHAR Loaded[MAX_PATH] = {};

        GetModuleFileNameW(Check.Module, Loaded, _countof(Loaded) - 1);
        fprintf(stderr, "runtime: %ls\n", Loaded);
    }
    Check.Create = reinterpret_cast<PFN_NC_CREATE>(GetProcAddress(Check.Module, "Direct3DCreate9"));
    Check.CreateEx = reinterpret_cast<PFN_NC_CREATE_EX>(GetProcAddress(Check.Module, "Direct3DCreate9Ex"));
    if (Check.CreateEx != nullptr)
    {
        hr = Check.CreateEx(D3D_SDK_VERSION, &Check.Ex);
        printf("create_ex = %08lX\n", hr);
        if (SUCCEEDED(hr))
            Check.Nine = Check.Ex;
    }
    if (Check.Nine == nullptr && Check.Create != nullptr)
        Check.Nine = Check.Create(D3D_SDK_VERSION);
    if (Check.Nine == nullptr)
    {
        fprintf(stderr, "error: no Direct3D 9 object\n");
        return 1;
    }

    for (const NineCheckInfo& Info : NcChecks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Item : Wanted)
            IsWanted = IsWanted || Item == Info.Name;
        if (IsWanted)
            Info.Run(&Check);
    }

    Check.Nine->Release();
    return 0;
}
