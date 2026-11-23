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
        hr = Check->Nine->GetAdapterIdentifier(Adapter, D3DENUM_WHQL_LEVEL, &Id);
        printf("identifier_whql[%u] = %08lX whql %08lX\n", Adapter, hr, Id.WHQLLevel);
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

static const NineCheckInfo NcChecks[] =
{
    { "exports", NcCheckExports },
    { "interfaces", NcCheckInterfaces },
    { "identifier", NcCheckIdentifier },
    { "modes", NcCheckModes },
    { "caps", NcCheckCaps },
    { "devtype", NcCheckDeviceType },
    { "formats", NcCheckFormats },
    { "multisample", NcCheckMultisample },
    { "depthmatch", NcCheckDepthMatch },
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
