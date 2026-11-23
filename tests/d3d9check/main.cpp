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
