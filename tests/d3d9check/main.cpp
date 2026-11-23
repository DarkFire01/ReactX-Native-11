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
            HRESULT Stretch;
            HRESULT Linear;
            HRESULT Readback;

            if (Surfaces[Source] == nullptr || Surfaces[Target] == nullptr)
                continue;
            Update = Device->UpdateSurface(Surfaces[Source], nullptr, Surfaces[Target], nullptr);
            Stretch = Device->StretchRect(Surfaces[Source], nullptr, Surfaces[Target], nullptr, D3DTEXF_NONE);
            Linear = Device->StretchRect(Surfaces[Source], nullptr, Surfaces[Target], nullptr, D3DTEXF_LINEAR);
            Readback = Device->GetRenderTargetData(Surfaces[Source], Surfaces[Target]);
            sprintf_s(Name, "%s[%s,%s]", Prefix, NcCopyKinds[Source].Name, NcCopyKinds[Target].Name);
            printf("%s update %08lX stretch %08lX linear %08lX readback %08lX\n", Name, Update, Stretch, Linear,
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
