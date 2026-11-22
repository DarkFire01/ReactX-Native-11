/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     DirectDraw answers for the entry points and objects, printed for comparing runtimes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * ddrawcheck [-runtime system|reactx|<path>] [check ...]
 */

#define INITGUID
#include <windows.h>
#include <ddraw.h>

#include <cstdio>
#include <string>
#include <vector>

typedef HRESULT (WINAPI* PDC_CREATE)(GUID*, LPDIRECTDRAW*, IUnknown*);
typedef HRESULT (WINAPI* PDC_CREATE_EX)(GUID*, LPVOID*, REFIID, IUnknown*);
typedef HRESULT (WINAPI* PDC_ENUM_A)(LPDDENUMCALLBACKA, LPVOID);
typedef HRESULT (WINAPI* PDC_ENUM_EX_A)(LPDDENUMCALLBACKEXA, LPVOID, DWORD);
typedef HRESULT (WINAPI* PDC_ENUM_W)(LPDDENUMCALLBACKW, LPVOID);
typedef HRESULT (WINAPI* PDC_ENUM_EX_W)(LPDDENUMCALLBACKEXW, LPVOID, DWORD);
typedef HRESULT (WINAPI* PDC_CREATE_CLIPPER)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*);
typedef HRESULT (WINAPI* PDC_GET_CLASS)(REFCLSID, REFIID, LPVOID*);
typedef HRESULT (WINAPI* PDC_CAN_UNLOAD)(VOID);

DEFINE_GUID(DcClsidFactory2, 0xb9dc4790, 0x4af1, 0x11d1, 0x8c, 0x4c, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5);
DEFINE_GUID(DcIidFactory2, 0x89b2c488, 0x4af4, 0x11d1, 0x8c, 0x4c, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5);
DEFINE_GUID(DcIidDirect3D, 0x3bba0080, 0x2421, 0x11cf, 0xa3, 0x1a, 0x00, 0xaa, 0x00, 0xb9, 0x33, 0x56);
DEFINE_GUID(DcIidDirect3D2, 0x6aae1ec1, 0x662a, 0x11d0, 0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a);
DEFINE_GUID(DcIidDirect3D3, 0xbb223240, 0xe72b, 0x11d0, 0xa9, 0xb4, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e);
DEFINE_GUID(DcIidDirect3D7, 0xf5049e77, 0x4861, 0x11d2, 0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8);
DEFINE_GUID(DcIidUnknownThing, 0x12345678, 0x1234, 0x1234, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34);
DEFINE_GUID(DcIidClassFactory, 0x00000001, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(DcNullGuid, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
DEFINE_GUID(DcIidUnknown,0x00000000, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);

struct DrawCheck
{
    HMODULE Module = NULL;
    PDC_CREATE Create = nullptr;
    PDC_CREATE_EX CreateEx = nullptr;
    HWND Window = NULL;
};

typedef VOID (*PDC_CHECK)(_Inout_ DrawCheck* Check);

struct DrawCheckInfo
{
    PCSTR Name;
    PDC_CHECK Run;
};

static
VOID
DcBytes(
    _In_z_ PCSTR Name,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    printf("%s =", Name);
    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        if (Index != 0 && (Index % 32) == 0)
            printf("\n   ");
        printf(" %02X", static_cast<const BYTE*>(Data)[Index]);
    }
    printf("\n");
}

static
VOID
DcGuid(
    _In_z_ PCSTR Name,
    _In_opt_ const GUID* Guid)
{
    if (Guid == nullptr)
    {
        printf("%s = null\n", Name);
        return;
    }

    printf("%s = {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n", Name, Guid->Data1, Guid->Data2,
           Guid->Data3, Guid->Data4[0], Guid->Data4[1], Guid->Data4[2], Guid->Data4[3], Guid->Data4[4],
           Guid->Data4[5], Guid->Data4[6], Guid->Data4[7]);
}

static
ULONG
DcRefCount(
    _In_ IUnknown* Object)
{
    Object->AddRef();
    return Object->Release();
}

static
LPDIRECTDRAW7
DcCreate7(
    _In_ DrawCheck* Check)
{
    LPDIRECTDRAW7 Draw = nullptr;

    if (FAILED(Check->CreateEx(nullptr, reinterpret_cast<LPVOID*>(&Draw), IID_IDirectDraw7, nullptr)))
        return nullptr;
    return Draw;
}

static
VOID
DcCheckExports(
    _Inout_ DrawCheck* Check)
{
    static const PCSTR Names[] =
    {
        "AcquireDDThreadLock", "CompleteCreateSysmemSurface", "D3DParseUnknownCommand", "DDGetAttachedSurfaceLcl",
        "DDInternalLock", "DDInternalUnlock", "DSoundHelp", "DirectDrawCreate", "DirectDrawCreateClipper",
        "DirectDrawCreateEx", "DirectDrawEnumerateA", "DirectDrawEnumerateExA", "DirectDrawEnumerateExW",
        "DirectDrawEnumerateW", "DllCanUnloadNow", "DllGetClassObject", "GetDDSurfaceLocal", "GetOLEThunkData",
        "GetSurfaceFromDC", "RegisterSpecialCase", "ReleaseDDThreadLock", "SetAppCompatData",
    };

    for (ULONG Index = 0; Index < _countof(Names); Index++)
    {
        FARPROC ByName = GetProcAddress(Check->Module, Names[Index]);
        FARPROC ByOrdinal = GetProcAddress(Check->Module, MAKEINTRESOURCEA(Index + 1));

        printf("export %lu %s = %s %s\n", Index + 1, Names[Index], ByName != nullptr ? "found" : "missing",
               ByName != nullptr && ByName == ByOrdinal ? "ordinal" : "no-ordinal");
    }
}

static
BOOL
WINAPI
DcEnumA(
    _In_opt_ GUID* Guid,
    _In_ LPSTR Description,
    _In_ LPSTR Name,
    _In_opt_ LPVOID Context)
{
    printf("  enum_a desc=\"%s\" name=\"%s\" ctx=%p\n", Description, Name, Context);
    DcGuid("    guid", Guid);
    return Context != reinterpret_cast<LPVOID>(2);
}

static
BOOL
WINAPI
DcEnumExA(
    _In_opt_ GUID* Guid,
    _In_ LPSTR Description,
    _In_ LPSTR Name,
    _In_opt_ LPVOID Context,
    _In_opt_ HMONITOR Monitor)
{
    MONITORINFOEXA Info = {};

    printf("  enum_ex_a desc=\"%s\" name=\"%s\" ctx=%p monitor=%s\n", Description, Name, Context,
           Monitor != NULL ? "set" : "null");
    DcGuid("    guid", Guid);
    if (Monitor != NULL)
    {
        Info.cbSize = sizeof(Info);
        GetMonitorInfoA(Monitor, &Info);
        printf("    monitor_device=\"%s\"\n", Info.szDevice);
    }
    return Context != reinterpret_cast<LPVOID>(2);
}

static
BOOL
WINAPI
DcEnumW(
    _In_opt_ GUID* Guid,
    _In_ LPWSTR Description,
    _In_ LPWSTR Name,
    _In_opt_ LPVOID Context)
{
    printf("  enum_w desc=\"%ls\" name=\"%ls\"\n", Description, Name);
    return TRUE;
}

static
BOOL
WINAPI
DcEnumExW(
    _In_opt_ GUID* Guid,
    _In_ LPWSTR Description,
    _In_ LPWSTR Name,
    _In_opt_ LPVOID Context,
    _In_opt_ HMONITOR Monitor)
{
    printf("  enum_ex_w desc=\"%ls\" name=\"%ls\"\n", Description, Name);
    return TRUE;
}

static
VOID
DcCheckEnumerate(
    _Inout_ DrawCheck* Check)
{
    PDC_ENUM_A EnumA = reinterpret_cast<PDC_ENUM_A>(GetProcAddress(Check->Module, "DirectDrawEnumerateA"));
    PDC_ENUM_EX_A EnumExA = reinterpret_cast<PDC_ENUM_EX_A>(GetProcAddress(Check->Module, "DirectDrawEnumerateExA"));
    PDC_ENUM_W EnumW = reinterpret_cast<PDC_ENUM_W>(GetProcAddress(Check->Module, "DirectDrawEnumerateW"));
    PDC_ENUM_EX_W EnumExW = reinterpret_cast<PDC_ENUM_EX_W>(GetProcAddress(Check->Module, "DirectDrawEnumerateExW"));

    printf("enum_a(null) = %08lX\n", EnumA(nullptr, nullptr));
    printf("enum_a = %08lX\n", EnumA(DcEnumA, reinterpret_cast<LPVOID>(1)));
    printf("enum_a_stop = %08lX\n", EnumA(DcEnumA, reinterpret_cast<LPVOID>(2)));
    for (DWORD Flags = 0; Flags <= 8; Flags++)
        printf("enum_ex_a[%lu] = %08lX\n", Flags, EnumExA(DcEnumExA, reinterpret_cast<LPVOID>(1), Flags));
    printf("enum_ex_a_high = %08lX\n", EnumExA(DcEnumExA, nullptr, 0x80000000));
    printf("enum_ex_a(null) = %08lX\n", EnumExA(nullptr, nullptr, 0));
    printf("enum_ex_a_stop = %08lX\n", EnumExA(DcEnumExA, reinterpret_cast<LPVOID>(2), 7));
    printf("enum_w(null) = %08lX\n", EnumW(nullptr, nullptr));
    printf("enum_w = %08lX\n", EnumW(DcEnumW, nullptr));
    printf("enum_ex_w = %08lX\n", EnumExW(DcEnumExW, nullptr, 0));
    printf("enum_ex_w(null) = %08lX\n", EnumExW(nullptr, nullptr, 0));
    printf("enum_ex_w_bad = %08lX\n", EnumExW(DcEnumExW, nullptr, 0x10));
}

static
VOID
DcCheckCreate(
    _Inout_ DrawCheck* Check)
{
    static GUID DisplayGuid = { 0x67685559, 0x3106, 0x11d0, { 0xb9, 0x71, 0x00, 0xaa, 0x00, 0x34, 0x2f, 0x9f } };
    static GUID BadDisplayGuid = { 0x67685559 + 31, 0x3106, 0x11d0, { 0xb9, 0x71, 0x00, 0xaa, 0x00, 0x34, 0x2f, 0x9f } };
    GUID* Guids[] =
    {
        nullptr, reinterpret_cast<GUID*>(DDCREATE_HARDWAREONLY), reinterpret_cast<GUID*>(DDCREATE_EMULATIONONLY),
        const_cast<GUID*>(&DcNullGuid), &DisplayGuid, &BadDisplayGuid, const_cast<GUID*>(&DcIidUnknownThing),
    };
    LPDIRECTDRAW Draw;
    LPVOID Object;
    IUnknown* Outer = reinterpret_cast<IUnknown*>(1);
    HRESULT hr;

    for (ULONG Index = 0; Index < _countof(Guids); Index++)
    {
        Draw = reinterpret_cast<LPDIRECTDRAW>(0x1);
        hr = Check->Create(Guids[Index], &Draw, nullptr);
        printf("create[%lu] = %08lX out=%s\n", Index, hr, Draw == nullptr ? "null" : "set");
        if (SUCCEEDED(hr))
        {
            printf("  refs = %lu\n", DcRefCount(Draw));
            printf("  release = %lu\n", Draw->Release());
        }

        Object = reinterpret_cast<LPVOID>(0x1);
        hr = Check->CreateEx(Guids[Index], &Object, IID_IDirectDraw7, nullptr);
        printf("create_ex[%lu] = %08lX out=%s\n", Index, hr, Object == nullptr ? "null" : "set");
        if (SUCCEEDED(hr))
            printf("  release = %lu\n", static_cast<IUnknown*>(Object)->Release());
    }

    Draw = reinterpret_cast<LPDIRECTDRAW>(0x1);
    hr = Check->Create(nullptr, &Draw, Outer);
    printf("create_outer = %08lX out=%s\n", hr, Draw == nullptr ? "null" : "set");
    printf("create_null = %08lX\n", Check->Create(nullptr, nullptr, nullptr));
    Object = reinterpret_cast<LPVOID>(0x1);
    hr = Check->CreateEx(nullptr, &Object, IID_IDirectDraw7, Outer);
    printf("create_ex_outer = %08lX out=%s\n", hr, Object == nullptr ? "null" : "set");
    Object = reinterpret_cast<LPVOID>(0x1);
    hr = Check->CreateEx(nullptr, &Object, IID_IDirectDraw, nullptr);
    printf("create_ex_iid1 = %08lX out=%s\n", hr, Object == nullptr ? "null" : "set");
    Object = reinterpret_cast<LPVOID>(0x1);
    hr = Check->CreateEx(nullptr, &Object, IID_IDirectDraw4, nullptr);
    printf("create_ex_iid4 = %08lX out=%s\n", hr, Object == nullptr ? "null" : "set");
}

/* Objects made without hardware or with hardware only answer from different caps */
static
VOID
DcCheckEmulated(
    _Inout_ DrawCheck* Check)
{
    static GUID* Guids[] =
    {
        reinterpret_cast<GUID*>(DDCREATE_EMULATIONONLY), reinterpret_cast<GUID*>(DDCREATE_HARDWAREONLY),
    };
    LPDIRECTDRAW7 Draw;
    DDSURFACEDESC2 Desc;
    DDSCAPS2 Caps;
    BYTE Hal[512];
    BYTE Hel[512];
    DWORD Codes[64];
    DWORD Count;
    DWORD Total;
    DWORD Free;
    HRESULT hr;

    for (ULONG Index = 0; Index < _countof(Guids); Index++)
    {
        Draw = nullptr;
        hr = Check->CreateEx(Guids[Index], reinterpret_cast<LPVOID*>(&Draw), IID_IDirectDraw7, nullptr);
        printf("special[%lu] = %08lX\n", Index, hr);
        if (FAILED(hr))
            continue;

        memset(Hal, 0xCC, sizeof(Hal));
        memset(Hel, 0xCC, sizeof(Hel));
        *reinterpret_cast<DWORD*>(Hal) = sizeof(DDCAPS_DX7);
        *reinterpret_cast<DWORD*>(Hel) = sizeof(DDCAPS_DX7);
        printf("  caps = %08lX\n", Draw->GetCaps(reinterpret_cast<LPDDCAPS>(Hal), reinterpret_cast<LPDDCAPS>(Hel)));
        DcBytes("  hal", Hal, sizeof(DDCAPS_DX7));
        DcBytes("  hel", Hel, sizeof(DDCAPS_DX7));

        memset(&Desc, 0xCC, sizeof(Desc));
        Desc.dwSize = sizeof(Desc);
        printf("  display_mode = %08lX\n", Draw->GetDisplayMode(&Desc));
        DcBytes("  desc", &Desc, sizeof(Desc));

        Count = _countof(Codes);
        hr = Draw->GetFourCCCodes(&Count, Codes);
        printf("  fourcc = %08lX count=%lu\n", hr, Count);

        memset(&Caps, 0, sizeof(Caps));
        Caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        Total = 0xCCCCCCCC;
        Free = 0xCCCCCCCC;
        hr = Draw->GetAvailableVidMem(&Caps, &Total, &Free);
        printf("  vidmem = %08lX total=%lu\n", hr, Total);
        Caps.dwCaps = 0;
        hr = Draw->GetAvailableVidMem(&Caps, &Total, &Free);
        printf("  vidmem0 = %08lX total=%lu\n", hr, Total);
        printf("  release = %lu\n", Draw->Release());
    }
}

static
VOID
DcCheckInterfaces(
    _Inout_ DrawCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        const GUID* Iid;
    } Iids[] =
    {
        { "IUnknown", &DcIidUnknown }, { "IDirectDraw", &IID_IDirectDraw }, { "IDirectDraw2", &IID_IDirectDraw2 },
        { "IDirectDraw4", &IID_IDirectDraw4 }, { "IDirectDraw7", &IID_IDirectDraw7 },
        { "IDirectDrawSurface", &IID_IDirectDrawSurface }, { "IDirectDrawClipper", &IID_IDirectDrawClipper },
        { "IDirectDrawPalette", &IID_IDirectDrawPalette }, { "IClassFactory", &DcIidClassFactory },
        { "Unknown", &DcIidUnknownThing }, { "IDirectDrawFactory2", &DcIidFactory2 },
        { "IDirect3D", &DcIidDirect3D }, { "IDirect3D2", &DcIidDirect3D2 }, { "IDirect3D3", &DcIidDirect3D3 },
        { "IDirect3D7", &DcIidDirect3D7 },
    };
    LPDIRECTDRAW Draw = nullptr;
    IUnknown* Interfaces[_countof(Iids)] = {};
    IUnknown* Object;
    HRESULT hr;

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;

    for (ULONG Index = 0; Index < _countof(Iids); Index++)
    {
        Object = reinterpret_cast<IUnknown*>(0x1);
        hr = Draw->QueryInterface(*Iids[Index].Iid, reinterpret_cast<LPVOID*>(&Object));
        printf("qi %s = %08lX out=%s same=%d refs=%lu\n", Iids[Index].Name, hr, Object == nullptr ? "null" : "set",
               Object == Draw, DcRefCount(Draw));
        if (SUCCEEDED(hr))
        {
            Interfaces[Index] = Object;
            printf("  own_refs = %lu\n", DcRefCount(Object));
        }
    }
    printf("qi_null = %08lX\n", Draw->QueryInterface(IID_IDirectDraw7, nullptr));

    /* Each interface keeps its own count but they share one object */
    for (ULONG Index = 0; Index < _countof(Iids); Index++)
    {
        if (Interfaces[Index] == nullptr)
            continue;

        Object = nullptr;
        hr = Interfaces[Index]->QueryInterface(IID_IDirectDraw, reinterpret_cast<LPVOID*>(&Object));
        printf("back %s = %08lX same=%d\n", Iids[Index].Name, hr, Object == Draw);
        if (Object != nullptr)
            Object->Release();
        printf("  release = %lu\n", Interfaces[Index]->Release());
    }
    printf("release = %lu\n", Draw->Release());
}

static
VOID
DcCheckCaps(
    _Inout_ DrawCheck* Check)
{
    static const DWORD Sizes[] =
    {
        0, sizeof(DDCAPS_DX1), sizeof(DDCAPS_DX3), sizeof(DDCAPS_DX5), sizeof(DDCAPS_DX6), sizeof(DDCAPS_DX7),
        sizeof(DDCAPS_DX7) + 4, 100,
    };
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    BYTE Hal[512];
    BYTE Hel[512];
    HRESULT hr;

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;

    for (DWORD Size : Sizes)
    {
        memset(Hal, 0xCC, sizeof(Hal));
        memset(Hel, 0xCC, sizeof(Hel));
        *reinterpret_cast<DWORD*>(Hal) = Size;
        *reinterpret_cast<DWORD*>(Hel) = Size;
        hr = Draw->GetCaps(reinterpret_cast<LPDDCAPS>(Hal), reinterpret_cast<LPDDCAPS>(Hel));
        printf("caps[%lu] = %08lX\n", Size, hr);
        DcBytes("  hal", Hal, Size > 0 && Size < 400 ? Size + 4 : 8);
        DcBytes("  hel", Hel, Size > 0 && Size < 400 ? Size + 4 : 8);

        memset(Hal, 0xCC, sizeof(Hal));
        *reinterpret_cast<DWORD*>(Hal) = Size;
        printf("caps_hal_only[%lu] = %08lX\n", Size, Draw->GetCaps(reinterpret_cast<LPDDCAPS>(Hal), nullptr));
        memset(Hel, 0xCC, sizeof(Hel));
        *reinterpret_cast<DWORD*>(Hel) = Size;
        printf("caps_hel_only[%lu] = %08lX\n", Size, Draw->GetCaps(nullptr, reinterpret_cast<LPDDCAPS>(Hel)));
    }
    printf("caps_none = %08lX\n", Draw->GetCaps(nullptr, nullptr));
    Draw->Release();

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    memset(Hal, 0xCC, sizeof(Hal));
    memset(Hel, 0xCC, sizeof(Hel));
    *reinterpret_cast<DWORD*>(Hal) = sizeof(DDCAPS_DX7);
    *reinterpret_cast<DWORD*>(Hel) = sizeof(DDCAPS_DX7);
    printf("caps7 = %08lX\n", Draw7->GetCaps(reinterpret_cast<LPDDCAPS>(Hal), reinterpret_cast<LPDDCAPS>(Hel)));
    DcBytes("  hal", Hal, sizeof(DDCAPS_DX7));
    DcBytes("  hel", Hel, sizeof(DDCAPS_DX7));
    *reinterpret_cast<DWORD*>(Hal) = sizeof(DDCAPS_DX6);
    printf("caps7_dx6 = %08lX\n", Draw7->GetCaps(reinterpret_cast<LPDDCAPS>(Hal), nullptr));
    Draw7->Release();
}

static
HRESULT
WINAPI
DcEnumMode(
    _In_ LPDDSURFACEDESC Desc,
    _In_ LPVOID Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    printf("  mode %lux%lu bpp=%lu hz=%lu pitch=%ld flags=%08lX pf=%08lX/%08lX %08lX %08lX %08lX caps=%08lX\n",
           Desc->dwWidth, Desc->dwHeight, Desc->ddpfPixelFormat.dwRGBBitCount, Desc->dwRefreshRate, Desc->lPitch,
           Desc->dwFlags, Desc->ddpfPixelFormat.dwFlags, Desc->ddpfPixelFormat.dwFourCC,
           Desc->ddpfPixelFormat.dwRBitMask, Desc->ddpfPixelFormat.dwGBitMask, Desc->ddpfPixelFormat.dwBBitMask,
           Desc->ddsCaps.dwCaps);
    (*Count)++;
    return *Count >= 400 ? DDENUMRET_CANCEL : DDENUMRET_OK;
}

static
HRESULT
WINAPI
DcEnumMode2(
    _In_ LPDDSURFACEDESC2 Desc,
    _In_ LPVOID Context)
{
    ULONG* Count = static_cast<ULONG*>(Context);

    if (*Count < 3)
        DcBytes("  mode2", Desc, sizeof(*Desc));
    (*Count)++;
    return DDENUMRET_OK;
}

static
VOID
DcCheckModes(
    _Inout_ DrawCheck* Check)
{
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    DDSURFACEDESC Desc;
    DDSURFACEDESC2 Desc2;
    ULONG Count;
    DWORD Frequency;
    DWORD Line;
    BOOL Blank;
    HRESULT hr;

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;

    memset(&Desc, 0xCC, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    printf("display_mode = %08lX\n", Draw->GetDisplayMode(&Desc));
    DcBytes("  desc", &Desc, sizeof(Desc));
    memset(&Desc, 0xCC, sizeof(Desc));
    Desc.dwSize = 0;
    printf("display_mode_size0 = %08lX\n", Draw->GetDisplayMode(&Desc));
    printf("display_mode_null = %08lX\n", Draw->GetDisplayMode(nullptr));

    for (DWORD Flags = 0; Flags < 4; Flags++)
    {
        Count = 0;
        printf("enum_modes[%lu] = %08lX\n", Flags, Draw->EnumDisplayModes(Flags, nullptr, &Count, DcEnumMode));
        printf("  count = %lu\n", Count);
    }
    printf("enum_modes_null = %08lX\n", Draw->EnumDisplayModes(0, nullptr, nullptr, nullptr));
    printf("enum_modes_bad = %08lX\n", Draw->EnumDisplayModes(0x10, nullptr, nullptr, DcEnumMode));

    memset(&Desc, 0, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DDSD_WIDTH | DDSD_HEIGHT;
    Desc.dwWidth = 1024;
    Desc.dwHeight = 768;
    Count = 0;
    printf("enum_modes_filter = %08lX\n", Draw->EnumDisplayModes(0, &Desc, &Count, DcEnumMode));
    memset(&Desc, 0, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    Desc.dwFlags = DDSD_PIXELFORMAT;
    Desc.ddpfPixelFormat.dwSize = sizeof(Desc.ddpfPixelFormat);
    Desc.ddpfPixelFormat.dwRGBBitCount = 16;
    Count = 0;
    printf("enum_modes_bpp = %08lX\n", Draw->EnumDisplayModes(0, &Desc, &Count, DcEnumMode));
    Desc.dwSize = 3;
    printf("enum_modes_badsize = %08lX\n", Draw->EnumDisplayModes(0, &Desc, &Count, DcEnumMode));

    Frequency = 0xCCCCCCCC;
    hr = Draw->GetMonitorFrequency(&Frequency);
    printf("frequency = %08lX value=%lu\n", hr, Frequency);
    printf("frequency_null = %08lX\n", Draw->GetMonitorFrequency(nullptr));
    Line = 0xCCCCCCCC;
    printf("scanline = %08lX\n", Draw->GetScanLine(&Line));
    printf("vblank = %08lX\n", Draw->GetVerticalBlankStatus(&Blank));
    printf("wait_vb = %08lX\n", Draw->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL));
    printf("wait_vb_bad = %08lX\n", Draw->WaitForVerticalBlank(0x10, NULL));
    printf("wait_vb_event = %08lX\n", Draw->WaitForVerticalBlank(DDWAITVB_BLOCKBEGINEVENT, NULL));
    printf("wait_vb_end = %08lX\n", Draw->WaitForVerticalBlank(DDWAITVB_BLOCKEND, NULL));
    printf("restore_mode = %08lX\n", Draw->RestoreDisplayMode());
    printf("set_mode_bad = %08lX\n", Draw->SetDisplayMode(1, 1, 7));
    printf("set_mode_zero = %08lX\n", Draw->SetDisplayMode(0, 0, 0));
    Draw->Release();

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    memset(&Desc2, 0xCC, sizeof(Desc2));
    Desc2.dwSize = sizeof(Desc2);
    printf("display_mode7 = %08lX\n", Draw7->GetDisplayMode(&Desc2));
    DcBytes("  desc2", &Desc2, sizeof(Desc2));
    memset(&Desc2, 0xCC, sizeof(Desc2));
    Desc2.dwSize = sizeof(DDSURFACEDESC);
    printf("display_mode7_old = %08lX\n", Draw7->GetDisplayMode(&Desc2));
    DcBytes("  desc2", &Desc2, sizeof(Desc2));
    Count = 0;
    printf("enum_modes7 = %08lX\n", Draw7->EnumDisplayModes(DDEDM_REFRESHRATES, nullptr, &Count, DcEnumMode2));
    printf("  count = %lu\n", Count);
    printf("set_mode7_bad = %08lX\n", Draw7->SetDisplayMode(1, 1, 7, 0, 0));
    printf("set_mode7_flags = %08lX\n", Draw7->SetDisplayMode(640, 480, 32, 0, 0x10));
    printf("evaluate = %08lX\n", Draw7->EvaluateMode(0, nullptr));
    printf("start_test = %08lX\n", Draw7->StartModeTest(nullptr, 0, 0));
    Draw7->Release();
}

static
VOID
DcCheckFourCC(
    _Inout_ DrawCheck* Check)
{
    LPDIRECTDRAW Draw = nullptr;
    DWORD Codes[256];
    DWORD Count;
    HRESULT hr;

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;

    Count = 0xCCCC;
    hr = Draw->GetFourCCCodes(&Count, nullptr);
    printf("fourcc_count = %08lX count=%lu\n", hr, Count);
    memset(Codes, 0xCC, sizeof(Codes));
    Count = 1;
    hr = Draw->GetFourCCCodes(&Count, Codes);
    printf("fourcc_one = %08lX count=%lu first=%08lX second=%08lX\n", hr, Count, Codes[0], Codes[1]);
    memset(Codes, 0xCC, sizeof(Codes));
    Count = _countof(Codes);
    hr = Draw->GetFourCCCodes(&Count, Codes);
    printf("fourcc = %08lX count=%lu\n", hr, Count);
    for (DWORD Index = 0; SUCCEEDED(hr) && Index < Count && Index < _countof(Codes); Index++)
        printf("  code %08lX %.4s\n", Codes[Index], reinterpret_cast<PCSTR>(&Codes[Index]));
    printf("fourcc_null = %08lX\n", Draw->GetFourCCCodes(nullptr, nullptr));
    Draw->Release();
}

static
VOID
DcCheckMemory(
    _Inout_ DrawCheck* Check)
{
    static const DWORD CapsList[] =
    {
        0, DDSCAPS_VIDEOMEMORY, DDSCAPS_TEXTURE, DDSCAPS_LOCALVIDMEM, DDSCAPS_NONLOCALVIDMEM,
        DDSCAPS_OFFSCREENPLAIN, DDSCAPS_PRIMARYSURFACE, DDSCAPS_SYSTEMMEMORY, DDSCAPS_3DDEVICE,
        DDSCAPS_ZBUFFER, DDSCAPS_TEXTURE | DDSCAPS_LOCALVIDMEM, DDSCAPS_TEXTURE | DDSCAPS_NONLOCALVIDMEM,
        DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM, DDSCAPS_OVERLAY, DDSCAPS_FLIP, 0x80000000,
    };
    LPDIRECTDRAW2 Draw2 = nullptr;
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    DDSCAPS Caps;
    DDSCAPS2 Caps2;
    DWORD Total;
    DWORD Free;
    HRESULT hr;

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;
    Draw->QueryInterface(IID_IDirectDraw2, reinterpret_cast<LPVOID*>(&Draw2));
    Draw->Release();
    if (Draw2 == nullptr)
        return;

    for (DWORD Value : CapsList)
    {
        Caps.dwCaps = Value;
        Total = 0xCCCCCCCC;
        Free = 0xCCCCCCCC;
        hr = Draw2->GetAvailableVidMem(&Caps, &Total, &Free);
        printf("vidmem2[%08lX] = %08lX total=%lu free_nonzero=%d\n", Value, hr, Total, Free != 0 && Free != 0xCCCCCCCC);
    }
    Caps.dwCaps = DDSCAPS_VIDEOMEMORY;
    printf("vidmem2_null = %08lX\n", Draw2->GetAvailableVidMem(nullptr, &Total, &Free));
    printf("vidmem2_nulls = %08lX\n", Draw2->GetAvailableVidMem(&Caps, nullptr, nullptr));
    Draw2->Release();

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    for (DWORD Value : CapsList)
    {
        memset(&Caps2, 0, sizeof(Caps2));
        Caps2.dwCaps = Value;
        Total = 0xCCCCCCCC;
        Free = 0xCCCCCCCC;
        hr = Draw7->GetAvailableVidMem(&Caps2, &Total, &Free);
        printf("vidmem7[%08lX] = %08lX total=%lu free_nonzero=%d\n", Value, hr, Total, Free != 0 && Free != 0xCCCCCCCC);
    }
    memset(&Caps2, 0, sizeof(Caps2));
    Caps2.dwCaps = DDSCAPS_TEXTURE;
    Caps2.dwCaps2 = DDSCAPS2_TEXTUREMANAGE;
    printf("vidmem7_managed = %08lX\n", Draw7->GetAvailableVidMem(&Caps2, &Total, &Free));
    Caps2.dwCaps2 = 0;
    Caps2.dwCaps4 = 1;
    printf("vidmem7_caps4 = %08lX\n", Draw7->GetAvailableVidMem(&Caps2, &Total, &Free));
    Draw7->Release();
}

static
VOID
DcCheckIdentifier(
    _Inout_ DrawCheck* Check)
{
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW4 Draw4 = nullptr;
    LPDIRECTDRAW7 Draw7;
    DDDEVICEIDENTIFIER Ident;
    DDDEVICEIDENTIFIER2 Ident2;

    if (SUCCEEDED(Check->Create(nullptr, &Draw, nullptr)))
    {
        Draw->QueryInterface(IID_IDirectDraw4, reinterpret_cast<LPVOID*>(&Draw4));
        Draw->Release();
    }
    if (Draw4 != nullptr)
    {
        for (DWORD Flags = 0; Flags < 3; Flags++)
        {
            memset(&Ident, 0xCC, sizeof(Ident));
            printf("ident4[%lu] = %08lX\n", Flags, Draw4->GetDeviceIdentifier(&Ident, Flags));
            printf("  driver=\"%.512s\" desc=\"%.512s\"\n", Ident.szDriver, Ident.szDescription);
            DcBytes("  tail", &Ident.liDriverVersion, sizeof(Ident) - FIELD_OFFSET(DDDEVICEIDENTIFIER, liDriverVersion));
        }
        printf("ident4_null = %08lX\n", Draw4->GetDeviceIdentifier(nullptr, 0));
        Draw4->Release();
    }

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    for (DWORD Flags = 0; Flags < 3; Flags++)
    {
        memset(&Ident2, 0xCC, sizeof(Ident2));
        printf("ident7[%lu] = %08lX\n", Flags, Draw7->GetDeviceIdentifier(&Ident2, Flags));
        printf("  driver=\"%.512s\" desc=\"%.512s\"\n", Ident2.szDriver, Ident2.szDescription);
        DcBytes("  tail", &Ident2.liDriverVersion,
                sizeof(Ident2) - FIELD_OFFSET(DDDEVICEIDENTIFIER2, liDriverVersion));
    }
    printf("ident7_null = %08lX\n", Draw7->GetDeviceIdentifier(nullptr, 0));
    Draw7->Release();
}

static
VOID
DcCheckCooperative(
    _Inout_ DrawCheck* Check)
{
    static const DWORD FlagsList[] =
    {
        0, DDSCL_NORMAL, DDSCL_NORMAL | DDSCL_FULLSCREEN, DDSCL_EXCLUSIVE, DDSCL_FULLSCREEN,
        DDSCL_NORMAL | DDSCL_EXCLUSIVE, DDSCL_NORMAL | DDSCL_MULTITHREADED, DDSCL_NORMAL | DDSCL_FPUSETUP,
        DDSCL_NORMAL | DDSCL_FPUPRESERVE, DDSCL_NORMAL | DDSCL_FPUSETUP | DDSCL_FPUPRESERVE,
        DDSCL_NORMAL | DDSCL_ALLOWMODEX, DDSCL_SETFOCUSWINDOW, DDSCL_SETDEVICEWINDOW,
        DDSCL_SETFOCUSWINDOW | DDSCL_NORMAL, DDSCL_CREATEDEVICEWINDOW, DDSCL_NORMAL | DDSCL_NOWINDOWCHANGES,
        DDSCL_NORMAL | DDSCL_ALLOWREBOOT, 0x8000, 0x10000,
    };
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    HWND Windows[] = { NULL, Check->Window, reinterpret_cast<HWND>(0x1234) };

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;
    for (ULONG Index = 0; Index < _countof(Windows); Index++)
    {
        for (DWORD Flags : FlagsList)
        {
            printf("coop[%lu][%08lX] = %08lX\n", Index, Flags, Draw->SetCooperativeLevel(Windows[Index], Flags));
            Draw->SetCooperativeLevel(NULL, DDSCL_NORMAL);
        }
    }
    printf("compact = %08lX\n", Draw->Compact());
    printf("initialize = %08lX\n", Draw->Initialize(nullptr));
    printf("flip_gdi = %08lX\n", Draw->FlipToGDISurface());
    printf("gdi_surface_null = %08lX\n", Draw->GetGDISurface(nullptr));
    Draw->Release();

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    printf("test_coop7 = %08lX\n", Draw7->TestCooperativeLevel());
    printf("coop7_normal = %08lX\n", Draw7->SetCooperativeLevel(Check->Window, DDSCL_NORMAL));
    printf("test_coop7_normal = %08lX\n", Draw7->TestCooperativeLevel());
    printf("restore_all7 = %08lX\n", Draw7->RestoreAllSurfaces());
    printf("surface_from_dc_null = %08lX\n", Draw7->GetSurfaceFromDC(NULL, nullptr));
    printf("compact7 = %08lX\n", Draw7->Compact());
    printf("initialize7 = %08lX\n", Draw7->Initialize(nullptr));
    Draw7->Release();
}

static
VOID
DcCheckFactory(
    _Inout_ DrawCheck* Check)
{
    static const struct
    {
        PCSTR Name;
        const GUID* Clsid;
    } Classes[] =
    {
        { "DirectDraw", &CLSID_DirectDraw }, { "DirectDraw7", &CLSID_DirectDraw7 },
        { "DirectDrawClipper", &CLSID_DirectDrawClipper }, { "Factory2", &DcClsidFactory2 },
        { "Unknown", &DcIidUnknownThing },
    };
    PDC_GET_CLASS GetClass = reinterpret_cast<PDC_GET_CLASS>(GetProcAddress(Check->Module, "DllGetClassObject"));
    PDC_CAN_UNLOAD CanUnload = reinterpret_cast<PDC_CAN_UNLOAD>(GetProcAddress(Check->Module, "DllCanUnloadNow"));
    IClassFactory* Factory;
    IUnknown* Object;
    HRESULT hr;

    printf("can_unload = %08lX\n", CanUnload());
    printf("class_null = %08lX\n", GetClass(CLSID_DirectDraw, IID_IClassFactory, nullptr));
    for (const auto& Class : Classes)
    {
        Factory = reinterpret_cast<IClassFactory*>(0x1);
        hr = GetClass(*Class.Clsid, IID_IClassFactory, reinterpret_cast<LPVOID*>(&Factory));
        printf("class %s = %08lX out=%s\n", Class.Name, hr, Factory == nullptr ? "null" : "set");
        Object = reinterpret_cast<IUnknown*>(0x1);
        hr = GetClass(*Class.Clsid, IID_IDirectDraw, reinterpret_cast<LPVOID*>(&Object));
        printf("class_iid %s = %08lX out=%s\n", Class.Name, hr, Object == nullptr ? "null" : "set");
        if (FAILED(GetClass(*Class.Clsid, IID_IClassFactory, reinterpret_cast<LPVOID*>(&Factory))))
            continue;

        printf("  can_unload = %08lX\n", CanUnload());
        printf("  lock = %08lX\n", Factory->LockServer(TRUE));
        printf("  unlock = %08lX\n", Factory->LockServer(FALSE));
        Object = reinterpret_cast<IUnknown*>(0x1);
        hr = Factory->CreateInstance(reinterpret_cast<IUnknown*>(1), IID_IUnknown, reinterpret_cast<LPVOID*>(&Object));
        printf("  create_outer = %08lX out=%s\n", hr, Object == nullptr ? "null" : "set");

        static const struct
        {
            PCSTR Name;
            const GUID* Iid;
        } Wants[] =
        {
            { "IUnknown", &DcIidUnknown }, { "IDirectDraw", &IID_IDirectDraw }, { "IDirectDraw2", &IID_IDirectDraw2 },
            { "IDirectDraw4", &IID_IDirectDraw4 }, { "IDirectDraw7", &IID_IDirectDraw7 },
            { "IDirectDrawClipper", &IID_IDirectDrawClipper }, { "Unknown", &DcIidUnknownThing },
        };

        for (const auto& Want : Wants)
        {
            Object = reinterpret_cast<IUnknown*>(0x1);
            hr = Factory->CreateInstance(nullptr, *Want.Iid, reinterpret_cast<LPVOID*>(&Object));
            printf("  create %s = %08lX out=%s\n", Want.Name, hr, Object == nullptr ? "null" : "set");
            if (FAILED(hr) || Object == nullptr)
                continue;

            if (*Want.Iid == IID_IDirectDraw || *Want.Iid == IID_IDirectDraw2 || *Want.Iid == IID_IDirectDraw4)
            {
                DDCAPS Caps = {};

                Caps.dwSize = sizeof(DDCAPS_DX1);
                printf("    caps_before = %08lX\n", static_cast<IDirectDraw*>(Object)->GetCaps(&Caps, nullptr));
                printf("    coop_before = %08lX\n", static_cast<IDirectDraw*>(Object)->SetCooperativeLevel(NULL, DDSCL_NORMAL));
                printf("    init = %08lX\n", static_cast<IDirectDraw*>(Object)->Initialize(nullptr));
                printf("    init_again = %08lX\n", static_cast<IDirectDraw*>(Object)->Initialize(nullptr));
                printf("    caps_after = %08lX\n", static_cast<IDirectDraw*>(Object)->GetCaps(&Caps, nullptr));
            }
            else if (*Want.Iid == IID_IDirectDraw7)
            {
                DDCAPS Caps = {};

                Caps.dwSize = sizeof(Caps);
                printf("    caps_before = %08lX\n", static_cast<IDirectDraw7*>(Object)->GetCaps(&Caps, nullptr));
                printf("    init_bad = %08lX\n", static_cast<IDirectDraw7*>(Object)->Initialize(const_cast<GUID*>(&DcIidUnknownThing)));
                printf("    init = %08lX\n", static_cast<IDirectDraw7*>(Object)->Initialize(nullptr));
                printf("    init_again = %08lX\n", static_cast<IDirectDraw7*>(Object)->Initialize(nullptr));
                printf("    caps_after = %08lX\n", static_cast<IDirectDraw7*>(Object)->GetCaps(&Caps, nullptr));
            }
            else if (*Want.Iid == IID_IDirectDrawClipper)
            {
                HWND Window = reinterpret_cast<HWND>(0x1);

                printf("    clip_hwnd_before = %08lX\n", static_cast<IDirectDrawClipper*>(Object)->GetHWnd(&Window));
                printf("    clip_init = %08lX\n", static_cast<IDirectDrawClipper*>(Object)->Initialize(nullptr, 0));
                printf("    clip_init_again = %08lX\n", static_cast<IDirectDrawClipper*>(Object)->Initialize(nullptr, 0));
            }
            printf("    can_unload = %08lX\n", CanUnload());
            printf("    release = %lu\n", Object->Release());
        }
        printf("  release = %lu\n", Factory->Release());
    }
    printf("can_unload_end = %08lX\n", CanUnload());
}

static
VOID
DcPrintRegion(
    _In_z_ PCSTR Name,
    _In_ HRESULT Result,
    _In_ DWORD Size,
    _In_ const RGNDATA* Region)
{
    const RECT* Rects = reinterpret_cast<const RECT*>(Region->Buffer);

    printf("%s = %08lX size=%lu count=%lu\n", Name, Result, Size, SUCCEEDED(Result) ? Region->rdh.nCount : 0);
    for (DWORD Index = 0; SUCCEEDED(Result) && Index < Region->rdh.nCount && Index < 8; Index++)
        printf("  rect %ld %ld %ld %ld\n", Rects[Index].left, Rects[Index].top, Rects[Index].right, Rects[Index].bottom);
}

static
VOID
DcCheckClipper(
    _Inout_ DrawCheck* Check)
{
    PDC_CREATE_CLIPPER CreateClipper =
        reinterpret_cast<PDC_CREATE_CLIPPER>(GetProcAddress(Check->Module, "DirectDrawCreateClipper"));
    LPDIRECTDRAWCLIPPER Clipper = nullptr;
    LPDIRECTDRAWCLIPPER Second = nullptr;
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    IUnknown* Object;
    struct
    {
        RGNDATAHEADER Header;
        RECT Rects[8];
    } List;
    BYTE Buffer[512];
    RGNDATA* Region = reinterpret_cast<RGNDATA*>(Buffer);
    RECT Bound = { 5, 5, 25, 25 };
    HWND Window;
    BOOL Changed;
    DWORD Size;
    HRESULT hr;

    printf("create_clipper_flags = %08lX\n", CreateClipper(1, &Clipper, nullptr));
    printf("create_clipper_outer = %08lX\n", CreateClipper(0, &Clipper, reinterpret_cast<IUnknown*>(1)));
    hr = CreateClipper(0, &Clipper, nullptr);
    printf("create_clipper = %08lX\n", hr);
    if (FAILED(hr))
        return;

    printf("  refs = %lu\n", DcRefCount(Clipper));
    Object = nullptr;
    hr = Clipper->QueryInterface(IID_IUnknown, reinterpret_cast<LPVOID*>(&Object));
    printf("  qi_unknown = %08lX same=%d\n", hr, Object == Clipper);
    if (Object != nullptr)
        Object->Release();
    printf("  qi_surface = %08lX\n", Clipper->QueryInterface(IID_IDirectDrawSurface, reinterpret_cast<LPVOID*>(&Object)));
    printf("  initialize = %08lX\n", Clipper->Initialize(nullptr, 0));
    Window = reinterpret_cast<HWND>(0x1);
    hr = Clipper->GetHWnd(&Window);
    printf("  get_hwnd = %08lX null=%d\n", hr, Window == NULL);
    Changed = 7;
    hr = Clipper->IsClipListChanged(&Changed);
    printf("  changed = %08lX value=%d\n", hr, Changed);

    Size = 0x1234;
    hr = Clipper->GetClipList(nullptr, nullptr, &Size);
    printf("  get_list_none = %08lX size=%lu\n", hr, Size);
    printf("  set_list_flags = %08lX\n", Clipper->SetClipList(nullptr, 1));

    memset(&List, 0, sizeof(List));
    List.Header.dwSize = sizeof(List.Header);
    List.Header.iType = RDH_RECTANGLES;
    List.Header.nCount = 0;
    printf("  set_list_empty = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
    List.Header.nCount = 3;
    SetRect(&List.Rects[0], 0, 0, 10, 10);
    SetRect(&List.Rects[1], 20, 0, 30, 10);
    SetRect(&List.Rects[2], 0, 40, 10, 50);
    SetRect(&List.Header.rcBound, 0, 0, 30, 50);
    List.Header.iType = 7;
    printf("  set_list_type = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
    List.Header.iType = RDH_RECTANGLES;
    List.Header.dwSize = 8;
    printf("  set_list_size = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
    List.Header.dwSize = sizeof(List.Header);
    printf("  set_list = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));

    Size = 0;
    hr = Clipper->GetClipList(nullptr, nullptr, &Size);
    printf("  get_list_size = %08lX size=%lu\n", hr, Size);
    Size = 40;
    memset(Buffer, 0xCC, sizeof(Buffer));
    hr = Clipper->GetClipList(nullptr, Region, &Size);
    printf("  get_list_small = %08lX size=%lu count=%lu\n", hr, Size, Region->rdh.nCount);
    Size = sizeof(Buffer);
    memset(Buffer, 0xCC, sizeof(Buffer));
    hr = Clipper->GetClipList(nullptr, Region, &Size);
    DcPrintRegion("  get_list", hr, Size, Region);
    DcBytes("  header", Region, sizeof(RGNDATAHEADER));
    Size = sizeof(Buffer);
    memset(Buffer, 0xCC, sizeof(Buffer));
    hr = Clipper->GetClipList(&Bound, Region, &Size);
    DcPrintRegion("  get_list_bound", hr, Size, Region);

    printf("  set_hwnd_flags = %08lX\n", Clipper->SetHWnd(1, Check->Window));
    printf("  set_hwnd_bad = %08lX\n", Clipper->SetHWnd(0, reinterpret_cast<HWND>(0x1234)));
    printf("  set_hwnd = %08lX\n", Clipper->SetHWnd(0, Check->Window));
    hr = Clipper->GetHWnd(&Window);
    printf("  get_hwnd_set = %08lX same=%d\n", hr, Window == Check->Window);
    printf("  set_list_hwnd = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
    printf("  set_hwnd_null = %08lX\n", Clipper->SetHWnd(0, NULL));
    printf("  clear_list = %08lX\n", Clipper->SetClipList(nullptr, 0));
    hr = Clipper->GetClipList(nullptr, nullptr, &Size);
    printf("  get_list_cleared = %08lX\n", hr);
    printf("  release = %lu\n", Clipper->Release());

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;
    printf("dd_clipper_flags = %08lX\n", Draw->CreateClipper(2, &Clipper, nullptr));
    hr = Draw->CreateClipper(0, &Clipper, nullptr);
    printf("dd_clipper = %08lX\n", hr);
    printf("  dd_refs = %lu\n", DcRefCount(Draw));
    if (SUCCEEDED(hr))
    {
        printf("  refs = %lu\n", DcRefCount(Clipper));
        List.Header.iType = 7;
        printf("  set_list_v1_type = %08lX\n", Clipper->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
    }

    /* Releasing the object also releases the clippers it made */
    printf("dd_release = %lu\n", Draw->Release());

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    hr = Draw7->CreateClipper(0, &Second, nullptr);
    printf("dd7_clipper = %08lX\n", hr);
    printf("  dd_refs = %lu\n", DcRefCount(Draw7));
    if (SUCCEEDED(hr))
    {
        List.Header.iType = 7;
        printf("  set_list_v7_type = %08lX\n", Second->SetClipList(reinterpret_cast<LPRGNDATA>(&List), 0));
        printf("  release = %lu\n", Second->Release());
        printf("  dd_refs = %lu\n", DcRefCount(Draw7));
    }
    printf("dd7_release = %lu\n", Draw7->Release());
}

static
VOID
DcCheckPalette(
    _Inout_ DrawCheck* Check)
{
    static const DWORD FlagsList[] =
    {
        DDPCAPS_8BIT, DDPCAPS_4BIT, DDPCAPS_2BIT, DDPCAPS_1BIT, DDPCAPS_8BIT | DDPCAPS_ALLOW256,
        DDPCAPS_4BIT | DDPCAPS_8BITENTRIES, DDPCAPS_1BIT | DDPCAPS_8BITENTRIES, DDPCAPS_8BIT | DDPCAPS_8BITENTRIES,
        DDPCAPS_4BIT | DDPCAPS_ALLOW256, DDPCAPS_8BIT | DDPCAPS_ALPHA, DDPCAPS_4BIT | DDPCAPS_ALPHA | DDPCAPS_8BITENTRIES,
        DDPCAPS_8BIT | DDPCAPS_4BIT, 0, DDPCAPS_8BIT | DDPCAPS_PRIMARYSURFACE, DDPCAPS_8BIT | DDPCAPS_VSYNC,
        DDPCAPS_8BIT | DDPCAPS_INITIALIZE, DDPCAPS_8BIT | 0x800,
    };
    PALETTEENTRY Entries[256];
    PALETTEENTRY Read[256];
    LPDIRECTDRAWPALETTE Palette;
    LPDIRECTDRAW Draw = nullptr;
    LPDIRECTDRAW7 Draw7;
    IUnknown* Object;
    DWORD Caps;
    HRESULT hr;

    for (DWORD Index = 0; Index < 256; Index++)
    {
        Entries[Index].peRed = static_cast<BYTE>(Index);
        Entries[Index].peGreen = static_cast<BYTE>(255 - Index);
        Entries[Index].peBlue = static_cast<BYTE>(Index * 3);
        Entries[Index].peFlags = static_cast<BYTE>(Index & 7);
    }

    if (FAILED(Check->Create(nullptr, &Draw, nullptr)))
        return;
    printf("palette_nocoop = %08lX\n", Draw->CreatePalette(DDPCAPS_8BIT, Entries, &Palette, nullptr));
    Draw->SetCooperativeLevel(Check->Window, DDSCL_NORMAL);
    printf("palette_outer = %08lX\n", Draw->CreatePalette(DDPCAPS_8BIT, Entries, &Palette, reinterpret_cast<IUnknown*>(1)));

    for (DWORD Flags : FlagsList)
    {
        Palette = nullptr;
        hr = Draw->CreatePalette(Flags, Entries, &Palette, nullptr);
        printf("palette[%08lX] = %08lX\n", Flags, hr);
        if (FAILED(hr))
            continue;

        Caps = 0xCCCCCCCC;
        hr = Palette->GetCaps(&Caps);
        printf("  caps = %08lX value=%08lX\n", hr, Caps);
        memset(Read, 0xCC, sizeof(Read));
        hr = Palette->GetEntries(0, 0, 256, Read);
        printf("  get_all = %08lX\n", hr);
        memset(Read, 0xCC, sizeof(Read));
        hr = Palette->GetEntries(0, 0, 2, Read);
        printf("  get_two = %08lX\n", hr);
        DcBytes("  first", Read, 8);
        memset(Read, 0xCC, sizeof(Read));
        if (SUCCEEDED(Palette->GetEntries(0, 0, (Caps & DDPCAPS_8BIT) ? 256 : (Caps & DDPCAPS_4BIT) ? 16 :
                                                (Caps & DDPCAPS_2BIT) ? 4 : 2, Read)))
            DcBytes("  entries", Read, 1024);
        printf("  get_flags = %08lX\n", Palette->GetEntries(1, 0, 1, Read));
        printf("  get_zero = %08lX\n", Palette->GetEntries(0, 0, 0, Read));
        printf("  get_start = %08lX\n", Palette->GetEntries(0, 255, 2, Read));
        printf("  set_some = %08lX\n", Palette->SetEntries(0, 1, 2, &Entries[100]));
        printf("  set_first = %08lX\n", Palette->SetEntries(0, 0, 1, &Entries[50]));
        printf("  set_last = %08lX\n", Palette->SetEntries(0, 255, 1, &Entries[60]));
        printf("  set_bad = %08lX\n", Palette->SetEntries(0, 1, 300, Entries));
        memset(Read, 0xCC, sizeof(Read));
        if (SUCCEEDED(Palette->GetEntries(0, 0, 4, Read)))
            DcBytes("  after_set", Read, 16);
        memset(Read, 0xCC, sizeof(Read));
        if (SUCCEEDED(Palette->GetEntries(0, 252, 4, Read)))
            DcBytes("  after_set_end", Read, 16);
        printf("  initialize = %08lX\n", Palette->Initialize(nullptr, 0, nullptr));
        Object = nullptr;
        hr = Palette->QueryInterface(IID_IDirectDrawPalette, reinterpret_cast<LPVOID*>(&Object));
        printf("  qi = %08lX same=%d\n", hr, Object == Palette);
        if (Object != nullptr)
            Object->Release();
        printf("  qi_bad = %08lX\n", Palette->QueryInterface(IID_IDirectDrawSurface, reinterpret_cast<LPVOID*>(&Object)));
        printf("  refs = %lu\n", DcRefCount(Palette));
        printf("  dd_refs = %lu\n", DcRefCount(Draw));
        printf("  release = %lu\n", Palette->Release());
    }
    Draw->Release();

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    Draw7->SetCooperativeLevel(Check->Window, DDSCL_NORMAL);
    hr = Draw7->CreatePalette(DDPCAPS_8BIT, Entries, &Palette, nullptr);
    printf("palette7 = %08lX\n", hr);
    printf("  dd_refs = %lu\n", DcRefCount(Draw7));
    if (SUCCEEDED(hr))
    {
        printf("  release = %lu\n", Palette->Release());
        printf("  dd_refs = %lu\n", DcRefCount(Draw7));
    }
    Draw7->Release();
}

/* Surface description bytes with the surface pointer blanked, it differs every run */
static
VOID
DcSurfaceDesc(
    _In_z_ PCSTR Name,
    _In_ LPDIRECTDRAWSURFACE7 Surface)
{
    DDSURFACEDESC2 Desc;
    HRESULT hr;

    memset(&Desc, 0xCC, sizeof(Desc));
    Desc.dwSize = sizeof(Desc);
    hr = Surface->GetSurfaceDesc(&Desc);
    printf("%s = %08lX\n", Name, hr);
    Desc.lpSurface = nullptr;
    DcBytes("  desc", &Desc, sizeof(Desc));
}

struct DcSurfaceShape
{
    PCSTR Name;
    DWORD Flags;
    DWORD Caps;
    DWORD Width;
    DWORD Height;
    DWORD BitCount;
    DWORD PixelFlags;
    DWORD Red;
    DWORD Green;
    DWORD Blue;
    DWORD Alpha;
};

static
VOID
DcFillDesc(
    _In_ const DcSurfaceShape& Shape,
    _Out_ DDSURFACEDESC2* Desc)
{
    memset(Desc, 0, sizeof(*Desc));
    Desc->dwSize = sizeof(*Desc);
    Desc->dwFlags = Shape.Flags;
    Desc->ddsCaps.dwCaps = Shape.Caps;
    Desc->dwWidth = Shape.Width;
    Desc->dwHeight = Shape.Height;
    if (Shape.PixelFlags != 0)
    {
        Desc->dwFlags |= DDSD_PIXELFORMAT;
        Desc->ddpfPixelFormat.dwSize = sizeof(Desc->ddpfPixelFormat);
        Desc->ddpfPixelFormat.dwFlags = Shape.PixelFlags;
        Desc->ddpfPixelFormat.dwRGBBitCount = Shape.BitCount;
        Desc->ddpfPixelFormat.dwRBitMask = Shape.Red;
        Desc->ddpfPixelFormat.dwGBitMask = Shape.Green;
        Desc->ddpfPixelFormat.dwBBitMask = Shape.Blue;
        Desc->ddpfPixelFormat.dwRGBAlphaBitMask = Shape.Alpha;
    }
}

static
VOID
DcCheckSurfaceCreate(
    _Inout_ DrawCheck* Check)
{
    static const DWORD Size = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    static const DWORD Plain = DDSCAPS_OFFSCREENPLAIN;
    static const DWORD Sys = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    static const DcSurfaceShape Shapes[] =
    {
        { "sys_default", Size, Sys, 64, 32 },
        { "video_default", Size, Plain, 64, 32 },
        { "plain_default", Size, Plain | DDSCAPS_VIDEOMEMORY, 64, 32 },
        { "sys_odd", Size, Sys, 13, 7 },
        { "sys_565", Size, Sys, 13, 7, 16, DDPF_RGB, 0xF800, 0x07E0, 0x001F },
        { "sys_555", Size, Sys, 13, 7, 16, DDPF_RGB, 0x7C00, 0x03E0, 0x001F },
        { "sys_1555", Size, Sys, 13, 7, 16, DDPF_RGB | DDPF_ALPHAPIXELS, 0x7C00, 0x03E0, 0x001F, 0x8000 },
        { "sys_24", Size, Sys, 13, 7, 24, DDPF_RGB, 0xFF0000, 0xFF00, 0xFF },
        { "sys_32", Size, Sys, 13, 7, 32, DDPF_RGB, 0xFF0000, 0xFF00, 0xFF },
        { "sys_8888", Size, Sys, 13, 7, 32, DDPF_RGB | DDPF_ALPHAPIXELS, 0xFF0000, 0xFF00, 0xFF, 0xFF000000 },
        { "sys_bgr", Size, Sys, 13, 7, 32, DDPF_RGB, 0xFF, 0xFF00, 0xFF0000 },
        { "sys_p8", Size, Sys, 13, 7, 8, DDPF_RGB | DDPF_PALETTEINDEXED8 },
        { "sys_p4", Size, Sys, 13, 7, 4, DDPF_RGB | DDPF_PALETTEINDEXED4 },
        { "sys_332", Size, Sys, 13, 7, 8, DDPF_RGB, 0xE0, 0x1C, 0x03 },
        { "video_565", Size, Plain, 13, 7, 16, DDPF_RGB, 0xF800, 0x07E0, 0x001F },
        { "video_p8", Size, Plain, 13, 7, 8, DDPF_RGB | DDPF_PALETTEINDEXED8 },
        { "no_caps", DDSD_WIDTH | DDSD_HEIGHT, 0, 64, 32 },
        { "caps_zero", Size, 0, 64, 32 },
        { "no_size", DDSD_CAPS, Sys, 0, 0 },
        { "zero_width", Size, Sys, 0, 32 },
        { "sys_and_video", Size, Sys | DDSCAPS_VIDEOMEMORY, 64, 32 },
        { "texture_sys", Size, DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY, 64, 32 },
        { "primary", DDSD_CAPS, DDSCAPS_PRIMARYSURFACE },
        { "primary_size", Size, DDSCAPS_PRIMARYSURFACE, 64, 32 },
        { "backbuffer_alone", Size, DDSCAPS_BACKBUFFER | DDSCAPS_SYSTEMMEMORY, 64, 32 },
        { "huge", Size, Sys, 70000, 2 },
        { "fourcc_yuy2", Size, Sys, 16, 8, 0, DDPF_FOURCC },
    };
    LPDIRECTDRAWSURFACE7 Surface;
    LPDIRECTDRAW7 Draw7;
    DDSURFACEDESC2 Desc;
    HRESULT hr;

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;

    DcFillDesc(Shapes[0], &Desc);
    printf("surface_nocoop = %08lX\n", Draw7->CreateSurface(&Desc, &Surface, nullptr));
    Draw7->SetCooperativeLevel(Check->Window, DDSCL_NORMAL);
    printf("surface_outer = %08lX\n", Draw7->CreateSurface(&Desc, &Surface, reinterpret_cast<IUnknown*>(1)));
    printf("surface_null_desc = %08lX\n", Draw7->CreateSurface(nullptr, &Surface, nullptr));
    Desc.dwSize = sizeof(DDSURFACEDESC);
    printf("surface_old_size = %08lX\n", Draw7->CreateSurface(&Desc, &Surface, nullptr));

    for (const DcSurfaceShape& Shape : Shapes)
    {
        DcFillDesc(Shape, &Desc);
        if (Shape.PixelFlags == DDPF_FOURCC)
            Desc.ddpfPixelFormat.dwFourCC = MAKEFOURCC('Y', 'U', 'Y', '2');
        Surface = nullptr;
        hr = Draw7->CreateSurface(&Desc, &Surface, nullptr);
        printf("surface %s = %08lX\n", Shape.Name, hr);
        if (FAILED(hr))
            continue;

        DcSurfaceDesc("  get_desc", Surface);
        memset(&Desc, 0, sizeof(Desc));
        Desc.dwSize = sizeof(Desc);
        hr = Surface->Lock(nullptr, &Desc, DDLOCK_WAIT, NULL);
        printf("  lock = %08lX pitch=%ld flags=%08lX\n", hr, Desc.lPitch, Desc.dwFlags);
        if (SUCCEEDED(hr))
        {
            Desc.lpSurface = nullptr;
            DcBytes("  lock_desc", &Desc, sizeof(Desc));
            printf("  unlock = %08lX\n", Surface->Unlock(nullptr));
        }
        printf("  unlock_again = %08lX\n", Surface->Unlock(nullptr));
        printf("  release = %lu\n", Surface->Release());
    }

    Draw7->Release();
}

static
LPDIRECTDRAWSURFACE7
DcMakeSurface(
    _In_ LPDIRECTDRAW7 Draw7,
    _In_ DWORD Caps,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BitCount)
{
    DcSurfaceShape Shape = { "", DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT, Caps, Width, Height };
    LPDIRECTDRAWSURFACE7 Surface = nullptr;
    DDSURFACEDESC2 Desc;

    if (BitCount == 16)
    {
        Shape.BitCount = 16;
        Shape.PixelFlags = DDPF_RGB;
        Shape.Red = 0xF800;
        Shape.Green = 0x07E0;
        Shape.Blue = 0x001F;
    }
    else if (BitCount == 32)
    {
        Shape.BitCount = 32;
        Shape.PixelFlags = DDPF_RGB;
        Shape.Red = 0xFF0000;
        Shape.Green = 0xFF00;
        Shape.Blue = 0xFF;
    }
    DcFillDesc(Shape, &Desc);
    if (FAILED(Draw7->CreateSurface(&Desc, &Surface, nullptr)))
        return nullptr;
    return Surface;
}

/* Writes a known pattern into every row, then reads the surface back as bytes */
static
VOID
DcPattern(
    _In_ LPDIRECTDRAWSURFACE7 Surface,
    _In_ DWORD Seed)
{
    DDSURFACEDESC2 Desc = {};
    BYTE* Row;

    Desc.dwSize = sizeof(Desc);
    if (FAILED(Surface->Lock(nullptr, &Desc, DDLOCK_WAIT, NULL)))
        return;
    for (DWORD Y = 0; Y < Desc.dwHeight; Y++)
    {
        Row = static_cast<BYTE*>(Desc.lpSurface) + Y * Desc.lPitch;
        for (DWORD X = 0; X < Desc.dwWidth * (Desc.ddpfPixelFormat.dwRGBBitCount / 8); X++)
            Row[X] = static_cast<BYTE>(Seed + X * 7 + Y * 13);
    }
    Surface->Unlock(nullptr);
}

static
VOID
DcDump(
    _In_z_ PCSTR Name,
    _In_ LPDIRECTDRAWSURFACE7 Surface)
{
    DDSURFACEDESC2 Desc = {};
    HRESULT hr;

    Desc.dwSize = sizeof(Desc);
    hr = Surface->Lock(nullptr, &Desc, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    printf("%s = %08lX\n", Name, hr);
    if (FAILED(hr))
        return;
    for (DWORD Y = 0; Y < Desc.dwHeight; Y++)
    {
        char Label[32];

        sprintf_s(Label, "  row%lu", Y);
        DcBytes(Label, static_cast<BYTE*>(Desc.lpSurface) + Y * Desc.lPitch,
                Desc.dwWidth * (Desc.ddpfPixelFormat.dwRGBBitCount / 8));
    }
    Surface->Unlock(nullptr);
}

static
VOID
DcCheckSurfaceOps(
    _Inout_ DrawCheck* Check)
{
    static const DWORD Sys = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    LPDIRECTDRAWSURFACE7 Target;
    LPDIRECTDRAWSURFACE7 Source;
    LPDIRECTDRAWSURFACE7 Wide;
    LPDIRECTDRAW7 Draw7;
    DDSURFACEDESC2 Desc;
    DDPIXELFORMAT Format;
    DDCOLORKEY Key;
    DDSCAPS2 Caps;
    DDBLTFX Fx;
    RECT From;
    RECT To;
    IUnknown* Object;
    HDC Dc;
    HRESULT hr;

    Draw7 = DcCreate7(Check);
    if (Draw7 == nullptr)
        return;
    Draw7->SetCooperativeLevel(Check->Window, DDSCL_NORMAL);

    for (DWORD BitCount : { 32u, 16u })
    {
        printf("[depth %lu]\n", BitCount);
        Target = DcMakeSurface(Draw7, Sys, 6, 4, BitCount);
        Source = DcMakeSurface(Draw7, Sys, 6, 4, BitCount);
        Wide = DcMakeSurface(Draw7, Sys, 3, 2, BitCount);
        if (Target == nullptr || Source == nullptr || Wide == nullptr)
        {
            printf("create failed\n");
            continue;
        }

        memset(&Caps, 0xCC, sizeof(Caps));
        hr = Target->GetCaps(&Caps);
        printf("get_caps = %08lX\n", hr);
        DcBytes("  caps", &Caps, sizeof(Caps));
        memset(&Format, 0xCC, sizeof(Format));
        Format.dwSize = sizeof(Format);
        hr = Target->GetPixelFormat(&Format);
        printf("get_format = %08lX\n", hr);
        DcBytes("  format", &Format, sizeof(Format));
        printf("is_lost = %08lX\n", Target->IsLost());
        printf("restore = %08lX\n", Target->Restore());
        Object = nullptr;
        hr = Target->QueryInterface(IID_IDirectDrawSurface, reinterpret_cast<LPVOID*>(&Object));
        printf("qi_surface1 = %08lX same=%d\n", hr, Object == Target);
        if (Object != nullptr)
            Object->Release();
        hr = Target->QueryInterface(IID_IDirectDrawSurface7, reinterpret_cast<LPVOID*>(&Object));
        printf("qi_surface7 = %08lX same=%d\n", hr, Object == Target);
        if (SUCCEEDED(hr))
            Object->Release();
        printf("qi_draw = %08lX\n", Target->QueryInterface(IID_IDirectDraw7, reinterpret_cast<LPVOID*>(&Object)));

        DcPattern(Source, 1);
        DcPattern(Target, 100);
        DcPattern(Wide, 200);

        memset(&Fx, 0, sizeof(Fx));
        Fx.dwSize = sizeof(Fx);
        Fx.dwFillColor = BitCount == 32 ? 0x00112233 : 0x1234;
        SetRect(&To, 1, 1, 4, 3);
        printf("fill = %08lX\n", Target->Blt(&To, nullptr, nullptr, DDBLT_COLORFILL | DDBLT_WAIT, &Fx));
        DcDump("  after_fill", Target);
        printf("fill_nofx = %08lX\n", Target->Blt(&To, nullptr, nullptr, DDBLT_COLORFILL, nullptr));
        Fx.dwSize = 3;
        printf("fill_badfx = %08lX\n", Target->Blt(&To, nullptr, nullptr, DDBLT_COLORFILL, &Fx));
        Fx.dwSize = sizeof(Fx);
        SetRect(&To, 4, 2, 9, 5);
        printf("fill_outside = %08lX\n", Target->Blt(&To, nullptr, nullptr, DDBLT_COLORFILL, &Fx));

        printf("copy = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_WAIT, nullptr));
        DcDump("  after_copy", Target);

        DcPattern(Target, 100);
        SetRect(&From, 0, 0, 3, 2);
        printf("fast = %08lX\n", Target->BltFast(2, 1, Source, &From, DDBLTFAST_NOCOLORKEY | DDBLTFAST_WAIT));
        DcDump("  after_fast", Target);
        printf("fast_clip = %08lX\n", Target->BltFast(5, 3, Source, &From, DDBLTFAST_WAIT));
        printf("fast_self = %08lX\n", Target->BltFast(1, 0, Target, &From, DDBLTFAST_WAIT));
        DcDump("  after_self", Target);

        DcPattern(Target, 100);
        SetRect(&To, 0, 0, 6, 4);
        printf("stretch = %08lX\n", Target->Blt(&To, Wide, nullptr, DDBLT_WAIT, nullptr));
        DcDump("  after_stretch", Target);
        SetRect(&To, 0, 0, 2, 1);
        SetRect(&From, 0, 0, 6, 4);
        DcPattern(Target, 100);
        printf("shrink = %08lX\n", Target->Blt(&To, Source, &From, DDBLT_WAIT, nullptr));
        DcDump("  after_shrink", Target);

        Key.dwColorSpaceLowValue = BitCount == 32 ? 0x000F0801 : 0x0801;
        Key.dwColorSpaceHighValue = Key.dwColorSpaceLowValue;
        printf("set_key = %08lX\n", Source->SetColorKey(DDCKEY_SRCBLT, &Key));
        memset(&Key, 0xCC, sizeof(Key));
        hr = Source->GetColorKey(DDCKEY_SRCBLT, &Key);
        printf("get_key = %08lX low=%08lX high=%08lX\n", hr, Key.dwColorSpaceLowValue, Key.dwColorSpaceHighValue);
        printf("get_key_dest = %08lX\n", Source->GetColorKey(DDCKEY_DESTBLT, &Key));
        printf("set_key_bad = %08lX\n", Source->SetColorKey(DDCKEY_SRCBLT | DDCKEY_DESTBLT, &Key));
        DcPattern(Target, 100);
        printf("keyed = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_KEYSRC | DDBLT_WAIT, nullptr));
        DcDump("  after_keyed", Target);
        DcPattern(Target, 100);
        printf("fast_keyed = %08lX\n", Target->BltFast(0, 0, Source, nullptr, DDBLTFAST_SRCCOLORKEY));
        DcDump("  after_fast_keyed", Target);
        printf("clear_key = %08lX\n", Source->SetColorKey(DDCKEY_SRCBLT, nullptr));
        printf("get_key_cleared = %08lX\n", Source->GetColorKey(DDCKEY_SRCBLT, &Key));

        memset(&Fx, 0, sizeof(Fx));
        Fx.dwSize = sizeof(Fx);
        Fx.dwROP = BLACKNESS;
        printf("rop_black = %08lX\n", Target->Blt(nullptr, nullptr, nullptr, DDBLT_ROP, &Fx));
        DcDump("  after_black", Target);
        Fx.dwROP = SRCINVERT;
        printf("rop_xor = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_ROP, &Fx));
        Fx.dwROP = SRCCOPY;
        DcPattern(Target, 100);
        printf("rop_copy = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_ROP, &Fx));
        DcDump("  after_rop_copy", Target);
        Fx.dwDDFX = DDBLTFX_MIRRORLEFTRIGHT;
        DcPattern(Target, 100);
        printf("mirror = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_DDFX, &Fx));
        DcDump("  after_mirror", Target);

        memset(&Desc, 0, sizeof(Desc));
        Desc.dwSize = sizeof(Desc);
        SetRect(&To, 2, 1, 4, 3);
        hr = Target->Lock(&To, &Desc, DDLOCK_WAIT, NULL);
        printf("lock_rect = %08lX pitch=%ld\n", hr, Desc.lPitch);
        printf("lock_twice = %08lX\n", Target->Lock(&To, &Desc, DDLOCK_WAIT, NULL));
        printf("blt_locked = %08lX\n", Target->Blt(nullptr, Source, nullptr, DDBLT_WAIT, nullptr));
        printf("unlock_wrong = %08lX\n", Target->Unlock(&From));
        printf("unlock = %08lX\n", Target->Unlock(&To));
        SetRect(&To, 2, 1, 9, 3);
        printf("lock_bad_rect = %08lX\n", Target->Lock(&To, &Desc, DDLOCK_WAIT, NULL));
        printf("lock_null_desc = %08lX\n", Target->Lock(nullptr, nullptr, DDLOCK_WAIT, NULL));

        hr = Target->GetDC(&Dc);
        printf("get_dc = %08lX\n", hr);
        if (SUCCEEDED(hr))
        {
            printf("  get_dc_twice = %08lX\n", Target->GetDC(&Dc));
            printf("  lock_with_dc = %08lX\n", Target->Lock(nullptr, &Desc, DDLOCK_WAIT, NULL));
            SetPixel(Dc, 0, 0, RGB(0x10, 0x20, 0x30));
            printf("  release_dc = %08lX\n", Target->ReleaseDC(Dc));
            DcDump("  after_dc", Target);
        }
        printf("release_dc_again = %08lX\n", Target->ReleaseDC(NULL));

        printf("release_wide = %lu\n", Wide->Release());
        printf("release_source = %lu\n", Source->Release());
        printf("release_target = %lu\n", Target->Release());
    }

    Draw7->Release();
}

static const DrawCheckInfo DcChecks[] =
{
    { "exports", DcCheckExports },
    { "enumerate", DcCheckEnumerate },
    { "create", DcCheckCreate },
    { "interfaces", DcCheckInterfaces },
    { "caps", DcCheckCaps },
    { "emulated", DcCheckEmulated },
    { "modes", DcCheckModes },
    { "fourcc", DcCheckFourCC },
    { "memory", DcCheckMemory },
    { "identifier", DcCheckIdentifier },
    { "cooperative", DcCheckCooperative },
    { "factory", DcCheckFactory },
    { "clipper", DcCheckClipper },
    { "palette", DcCheckPalette },
    { "surface_create", DcCheckSurfaceCreate },
    { "surface_ops", DcCheckSurfaceOps },
};

static
std::wstring
DcRuntimePath(
    _In_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return L"ddraw.dll";

    if (_wcsicmp(Runtime, L"reactx") == 0)
    {
        GetModuleFileNameW(NULL, Module, _countof(Module));
        Slash = wcsrchr(Module, L'\\');
        if (Slash != nullptr)
            *(Slash + 1) = L'\0';
        return std::wstring(Module) + L"reactx\\ddraw.dll";
    }

    return Runtime;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    std::wstring Runtime = DcRuntimePath(L"system");
    std::vector<std::string> Wanted;
    DrawCheck Check;
    WCHAR Loaded[MAX_PATH];
    char Name[64];

    /* Unbuffered so a crash still shows the last check that ran */
    setvbuf(stdout, nullptr, _IONBF, 0);

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
        {
            Runtime = DcRuntimePath(argv[++Index]);
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
    GetModuleFileNameW(Check.Module, Loaded, _countof(Loaded));
    fprintf(stderr, "loaded %ls\n", Loaded);

    Check.Create = reinterpret_cast<PDC_CREATE>(GetProcAddress(Check.Module, "DirectDrawCreate"));
    Check.CreateEx = reinterpret_cast<PDC_CREATE_EX>(GetProcAddress(Check.Module, "DirectDrawCreateEx"));
    if (Check.Create == nullptr || Check.CreateEx == nullptr)
    {
        fprintf(stderr, "error: no DirectDrawCreate\n");
        return 1;
    }
    Check.Window = CreateWindowExW(0, L"STATIC", L"ddrawcheck", WS_OVERLAPPEDWINDOW, 0, 0, 320, 240, NULL, NULL,
                                   GetModuleHandleW(NULL), nullptr);

    for (const DrawCheckInfo& Info : DcChecks)
    {
        BOOLEAN IsWanted = Wanted.empty();

        for (const std::string& Item : Wanted)
            IsWanted = IsWanted || Item == Info.Name;
        if (IsWanted)
        {
            printf("[%s]\n", Info.Name);
            Info.Run(&Check);
        }
    }

    DestroyWindow(Check.Window);
    return 0;
}
