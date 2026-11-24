/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Compares a Direct3D 9 on 12 driver against the system one through the system d3d9.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * d3d9on12check [-runtime system|reactx|<path>]
 *
 * The system d3d9.dll is loaded with 9on12 requested. For reactx the driver beside this program is
 * loaded first, so the runtime's load of d3d9on12.dll by name finds it. The runtime's GetProcAddress
 * import is pointed at a wrapper in this process only, which wraps the private open and the adapter
 * table and prints every answer. The device DDI table and the Direct3D 12 device and queue methods are
 * wrapped too, so the Direct3D 12 work behind each call shows. Run once per runtime in separate processes
 * and diff the output.
 */

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#include <winternl.h>

#include <d3d9.h>
#include <d3d9on12.h>
#include <d3dkmthk.h>
#include <d3dumddi.h>

#include <stdio.h>
#include <string>

#include "checks.hpp"

typedef HRESULT (APIENTRY* PFN_CK_OPEN_ADAPTER)(D3DDDIARG_OPENADAPTER*, const LUID*, VOID*);
typedef VOID (APIENTRY* PFN_CK_GET_TABLE)(VOID*);
typedef HRESULT (APIENTRY* PFN_CK_GET_TABLE_VERSIONED)(VOID*, UINT);
typedef IDirect3D9* (WINAPI* PFN_CK_CREATE_9ON12)(UINT, D3D9ON12_ARGS*, UINT);
typedef HRESULT (WINAPI* PFN_CK_CREATE_9ON12_EX)(UINT, D3D9ON12_ARGS*, UINT, IDirect3D9Ex**);

/* The private table entries this test wraps */
static constexpr UINT CkOpenSlot = 0;
static constexpr UINT CkOpen2Slot = 27;
static constexpr UINT CkFirstTableSlots = 27;

struct CkState
{
    FARPROC (WINAPI* GetProcAddress)(HMODULE, LPCSTR);
    PFN_CK_GET_TABLE GetTable;
    PFN_CK_GET_TABLE_VERSIONED GetTableVersioned;
    PFN_CK_OPEN_ADAPTER Open;
    PFN_CK_OPEN_ADAPTER Open2;
    PFND3DDDI_GETCAPS GetCaps;
    PFND3DDDI_CREATEDEVICE CreateDevice;
    PFND3DDDI_CLOSEADAPTER CloseAdapter;
    UINT Adapters;
};

static CkState Ck;

/* Keeps what was printed before a crash and names where it happened on stderr */
static
LONG
WINAPI
CkCrashFilter(
    _In_ EXCEPTION_POINTERS* Exception)
{
    const EXCEPTION_RECORD* Record = Exception->ExceptionRecord;
    WCHAR Name[MAX_PATH] = L"?";
    HMODULE Module = NULL;
    ULONG_PTR Offset = reinterpret_cast<ULONG_PTR>(Record->ExceptionAddress);

    fflush(stdout);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(Record->ExceptionAddress), &Module))
    {
        GetModuleFileNameW(Module, Name, _countof(Name));
        Offset -= reinterpret_cast<ULONG_PTR>(Module);
    }
    fwprintf(stderr, L"unhandled exception %08lX at %s+0x%Ix\n", Record->ExceptionCode, Name, Offset);
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static
VOID
CkDump(
    _In_z_ PCSTR Label,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ UINT Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    printf("  %s %u bytes\n", Label, Size);
    for (UINT Offset = 0; Offset < Size; Offset += 16)
    {
        printf("    %04X:", Offset);
        for (UINT Index = Offset; Index < Offset + 16 && Index < Size; Index += 4)
        {
            UINT Value = 0;
            UINT Left = Size - Index;

            CopyMemory(&Value, Bytes + Index, Left < 4 ? Left : 4);
            printf(" %08X", Value);
        }
        printf("\n");
    }
}

static
PCSTR
CkCapsName(
    _In_ D3DDDICAPS_TYPE Type)
{
    switch (Type)
    {
        case D3DDDICAPS_DDRAW: return "DDRAW";
        case D3DDDICAPS_DDRAW_MODE_SPECIFIC: return "DDRAW_MODE_SPECIFIC";
        case D3DDDICAPS_GETFORMATCOUNT: return "GETFORMATCOUNT";
        case D3DDDICAPS_GETFORMATDATA: return "GETFORMATDATA";
        case D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS: return "GETMULTISAMPLEQUALITYLEVELS";
        case D3DDDICAPS_GETD3DQUERYCOUNT: return "GETD3DQUERYCOUNT";
        case D3DDDICAPS_GETD3DQUERYDATA: return "GETD3DQUERYDATA";
        case D3DDDICAPS_GETD3D3CAPS: return "GETD3D3CAPS";
        case D3DDDICAPS_GETD3D7CAPS: return "GETD3D7CAPS";
        case D3DDDICAPS_GETD3D8CAPS: return "GETD3D8CAPS";
        case D3DDDICAPS_GETD3D9CAPS: return "GETD3D9CAPS";
        case D3DDDICAPS_GETGAMMARAMPCAPS: return "GETGAMMARAMPCAPS";
        case D3DDDICAPS_GETCONTENTPROTECTIONCAPS: return "GETCONTENTPROTECTIONCAPS";
        case D3DDDICAPS_GET_ARCHITECTURE_INFO: return "GET_ARCHITECTURE_INFO";
        default: return "other";
    }
}

static
HRESULT
APIENTRY
CkGetCaps(
    _In_ HANDLE hAdapter,
    _Inout_ const D3DDDIARG_GETCAPS* Args)
{
    HRESULT hr;

    printf("GetCaps %s (%u) size %u\n", CkCapsName(Args->Type), Args->Type, Args->DataSize);
    if (Args->Type == D3DDDICAPS_GETMULTISAMPLEQUALITYLEVELS && Args->pData != nullptr)
    {
        const DDIMULTISAMPLEQUALITYLEVELSDATA* Query = static_cast<const DDIMULTISAMPLEQUALITYLEVELSDATA*>(Args->pData);

        printf("  format %u flags %u samples %u\n", Query->Format, *(reinterpret_cast<const UINT*>(Query) + 1),
               Query->MsType);
    }

    hr = Ck.GetCaps(hAdapter, Args);
    printf("  hr %08lX\n", hr);
    if (FAILED(hr) || Args->pData == nullptr)
        return hr;

    if (Args->Type == D3DDDICAPS_GETFORMATDATA)
    {
        const FORMATOP* Ops = static_cast<const FORMATOP*>(Args->pData);

        for (UINT Index = 0; Index < Args->DataSize / sizeof(FORMATOP); Index++)
        {
            printf("  format %08X ops %08X flip %04X blt %04X bits %u\n", Ops[Index].Format, Ops[Index].Operations,
                   Ops[Index].FlipMsTypes, Ops[Index].BltMsTypes, Ops[Index].PrivateFormatBitCount);
        }
    }
    else
    {
        CkDump("data", Args->pData, Args->DataSize);
    }
    return hr;
}

static
HRESULT
APIENTRY
CkCreateDevice(
    _In_ HANDLE hAdapter,
    _Inout_ D3DDDIARG_CREATEDEVICE* Args)
{
    HRESULT hr;

    printf("CreateDevice interface %u version %08X flags %08X\n", Args->Interface, Args->Version, Args->Flags.Value);
    hr = Ck.CreateDevice(hAdapter, Args);
    printf("  hr %08lX\n", hr);
    if (SUCCEEDED(hr))
        CkWrapDeviceTable(Args->pDeviceFuncs);
    return hr;
}

static
HRESULT
APIENTRY
CkCloseAdapter(
    _In_ HANDLE hAdapter)
{
    HRESULT hr = Ck.CloseAdapter(hAdapter);

    printf("CloseAdapter hr %08lX\n", hr);
    return hr;
}

/* Prints the arguments and answers of a private open and wraps the adapter table it fills */
static
HRESULT
CkOpen(
    _In_z_ PCSTR Name,
    _In_ PFN_CK_OPEN_ADAPTER Open,
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* Luid,
    _Inout_ VOID* OpenArgs)
{
    const UINT_PTR* Fields = static_cast<const UINT_PTR*>(OpenArgs);
    const UINT* Counts = reinterpret_cast<const UINT*>(Fields + 2);
    HRESULT hr;

    printf("%s interface %u version %08X luid %s device %s queues %u node %08X\n", Name, Args->Interface,
           Args->Version, Luid != nullptr ? "set" : "none", Fields[0] != 0 ? "set" : "none", Counts[0], Counts[1]);

    hr = Open(Args, Luid, OpenArgs);
    printf("  hr %08lX driver version %08X\n", hr, Args->DriverVersion);
    if (FAILED(hr))
        return hr;

    Ck.Adapters += 1;
    printf("  funcs getcaps %s createdevice %s closeadapter %s\n", Args->pAdapterFuncs->pfnGetCaps ? "set" : "none",
           Args->pAdapterFuncs->pfnCreateDevice ? "set" : "none", Args->pAdapterFuncs->pfnCloseAdapter ? "set" : "none");
    Ck.GetCaps = Args->pAdapterFuncs->pfnGetCaps;
    Ck.CreateDevice = Args->pAdapterFuncs->pfnCreateDevice;
    Ck.CloseAdapter = Args->pAdapterFuncs->pfnCloseAdapter;
    Args->pAdapterFuncs->pfnGetCaps = CkGetCaps;
    Args->pAdapterFuncs->pfnCreateDevice = CkCreateDevice;
    Args->pAdapterFuncs->pfnCloseAdapter = CkCloseAdapter;
    return hr;
}

static
HRESULT
APIENTRY
CkOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* Luid,
    _Inout_ VOID* OpenArgs)
{
    return CkOpen("PrivateOpenAdapter", Ck.Open, Args, Luid, OpenArgs);
}

static
HRESULT
APIENTRY
CkOpenAdapter2(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* Luid,
    _Inout_ VOID* OpenArgs)
{
    return CkOpen("PrivateOpenAdapter2", Ck.Open2, Args, Luid, OpenArgs);
}

static
VOID
CkWrapTable(
    _Inout_ PVOID* Slots,
    _In_ UINT Count)
{
    UINT Missing = 0;

    for (UINT Slot = 0; Slot < Count; Slot++)
    {
        if (Slots[Slot] == nullptr)
            Missing += 1;
    }
    printf("  slots %u empty %u\n", Count, Missing);
    CkWrapPrivateTable(Slots, Count);

    Ck.Open = reinterpret_cast<PFN_CK_OPEN_ADAPTER>(Slots[CkOpenSlot]);
    Slots[CkOpenSlot] = reinterpret_cast<PVOID>(CkOpenAdapter);
    if (Count > CkOpen2Slot)
    {
        Ck.Open2 = reinterpret_cast<PFN_CK_OPEN_ADAPTER>(Slots[CkOpen2Slot]);
        Slots[CkOpen2Slot] = reinterpret_cast<PVOID>(CkOpenAdapter2);
    }
}

static
VOID
APIENTRY
CkGetTable(
    _Out_ VOID* Table)
{
    printf("GetPrivateDDITable\n");
    Ck.GetTable(Table);
    CkWrapTable(static_cast<PVOID*>(Table), CkFirstTableSlots);
}

static
HRESULT
APIENTRY
CkGetTableVersioned(
    _Out_writes_bytes_(Size) VOID* Table,
    _In_ UINT Size)
{
    HRESULT hr;

    printf("GetPrivateDDITableVersioned size %u\n", Size);
    hr = Ck.GetTableVersioned(Table, Size);
    printf("  hr %08lX\n", hr);
    if (SUCCEEDED(hr))
        CkWrapTable(static_cast<PVOID*>(Table), Size / sizeof(PVOID));
    return hr;
}

static
FARPROC
WINAPI
CkGetProcAddress(
    _In_ HMODULE Module,
    _In_ LPCSTR Name)
{
    FARPROC Found = Ck.GetProcAddress(Module, Name);
    WCHAR Path[MAX_PATH];

    if (Found == nullptr || IS_INTRESOURCE(Name))
        return Found;

    if (strcmp(Name, "GetPrivateDDITable") == 0 || strcmp(Name, "GetPrivateDDITableVersioned") == 0)
    {
        if (GetModuleFileNameW(Module, Path, _countof(Path)) != 0)
            fwprintf(stderr, L"driver module %s\n", Path);
    }

    if (strcmp(Name, "GetPrivateDDITable") == 0)
    {
        Ck.GetTable = reinterpret_cast<PFN_CK_GET_TABLE>(Found);
        return reinterpret_cast<FARPROC>(CkGetTable);
    }
    if (strcmp(Name, "GetPrivateDDITableVersioned") == 0)
    {
        Ck.GetTableVersioned = reinterpret_cast<PFN_CK_GET_TABLE_VERSIONED>(Found);
        return reinterpret_cast<FARPROC>(CkGetTableVersioned);
    }
    return Found;
}

/* Points the runtime's GetProcAddress import at the wrapper */
static
BOOLEAN
CkPatchImports(
    _In_ HMODULE Module)
{
    BYTE* Base = reinterpret_cast<BYTE*>(Module);
    const IMAGE_DOS_HEADER* Dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(Base);
    const IMAGE_NT_HEADERS* Nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(Base + Dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY* Directory = &Nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    for (const IMAGE_IMPORT_DESCRIPTOR* Import =
             reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(Base + Directory->VirtualAddress);
         Import->Name != 0; Import++)
    {
        const IMAGE_THUNK_DATA* Names;
        IMAGE_THUNK_DATA* Addresses;

        if (Import->OriginalFirstThunk == 0)
            continue;

        Names = reinterpret_cast<const IMAGE_THUNK_DATA*>(Base + Import->OriginalFirstThunk);
        Addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(Base + Import->FirstThunk);
        for (; Names->u1.AddressOfData != 0; Names++, Addresses++)
        {
            const IMAGE_IMPORT_BY_NAME* ByName;
            DWORD Old;

            if (IMAGE_SNAP_BY_ORDINAL(Names->u1.Ordinal))
                continue;
            ByName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(Base + Names->u1.AddressOfData);
            if (strcmp(ByName->Name, "GetProcAddress") != 0)
                continue;

            Ck.GetProcAddress = reinterpret_cast<decltype(Ck.GetProcAddress)>(Addresses->u1.Function);
            if (!VirtualProtect(&Addresses->u1.Function, sizeof(Addresses->u1.Function), PAGE_READWRITE, &Old))
                return FALSE;
            Addresses->u1.Function = reinterpret_cast<ULONG_PTR>(CkGetProcAddress);
            VirtualProtect(&Addresses->u1.Function, sizeof(Addresses->u1.Function), Old, &Old);
            return TRUE;
        }
    }
    return FALSE;
}

static
HWND
CkCreateWindow(VOID)
{
    WNDCLASSW Class = {};
    RECT Rect = { 0, 0, 64, 48 };

    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"d3d9on12check";
    RegisterClassW(&Class);
    AdjustWindowRect(&Rect, WS_OVERLAPPEDWINDOW, FALSE);
    return CreateWindowW(Class.lpszClassName, L"d3d9on12check", WS_OVERLAPPEDWINDOW, 0, 0, Rect.right - Rect.left,
                         Rect.bottom - Rect.top, NULL, NULL, Class.hInstance, NULL);
}

static
VOID
CkPrintIdentifier(
    _In_ IDirect3D9* D3D)
{
    D3DADAPTER_IDENTIFIER9 Id = {};
    HRESULT hr = D3D->GetAdapterIdentifier(0, 0, &Id);

    printf("GetAdapterIdentifier hr %08lX\n", hr);
    if (FAILED(hr))
        return;
    printf("  driver %s\n  description %s\n  device %s\n", Id.Driver, Id.Description, Id.DeviceName);
    printf("  version %08lX%08lX vendor %04lX device %04lX subsys %08lX revision %lu whql %lu\n",
           Id.DriverVersion.HighPart, Id.DriverVersion.LowPart, Id.VendorId, Id.DeviceId, Id.SubSysId, Id.Revision,
           Id.WHQLLevel);
}

static const D3DFORMAT CkFormats[] =
{
    D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4,
    D3DFMT_X4R4G4B4, D3DFMT_A8B8G8R8, D3DFMT_X8B8G8R8, D3DFMT_A2R10G10B10, D3DFMT_A2B10G10R10, D3DFMT_G16R16,
    D3DFMT_A16B16G16R16, D3DFMT_A8, D3DFMT_L8, D3DFMT_A8L8, D3DFMT_L16, D3DFMT_V8U8, D3DFMT_Q8W8V8U8, D3DFMT_V16U16,
    D3DFMT_Q16W16V16U16, D3DFMT_R16F, D3DFMT_G16R16F, D3DFMT_A16B16G16R16F, D3DFMT_R32F, D3DFMT_G32R32F,
    D3DFMT_A32B32G32R32F, D3DFMT_DXT1, D3DFMT_DXT2, D3DFMT_DXT3, D3DFMT_DXT4, D3DFMT_DXT5, D3DFMT_D16, D3DFMT_D24S8,
    D3DFMT_D24X8, D3DFMT_D24FS8, D3DFMT_D32, D3DFMT_D16_LOCKABLE, D3DFMT_D32F_LOCKABLE, D3DFMT_P8, D3DFMT_A8P8,
    D3DFMT_YUY2, D3DFMT_UYVY, static_cast<D3DFORMAT>(MAKEFOURCC('N', 'V', '1', '2')),
    static_cast<D3DFORMAT>(MAKEFOURCC('Y', 'V', '1', '2')), static_cast<D3DFORMAT>(MAKEFOURCC('A', 'Y', 'U', 'V')),
    static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')), static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '1', '6')),
    static_cast<D3DFORMAT>(MAKEFOURCC('D', 'F', '2', '4')), static_cast<D3DFORMAT>(MAKEFOURCC('N', 'U', 'L', 'L')),
    static_cast<D3DFORMAT>(MAKEFOURCC('R', 'E', 'S', 'Z')), static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'O', 'C')),
    static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '1')), static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '2')),
};

struct CkUse
{
    DWORD Usage;
    D3DRESOURCETYPE Type;
};

static const CkUse CkUses[] =
{
    { 0, D3DRTYPE_TEXTURE }, { 0, D3DRTYPE_CUBETEXTURE }, { 0, D3DRTYPE_VOLUMETEXTURE }, { 0, D3DRTYPE_SURFACE },
    { D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE }, { D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE },
    { D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE }, { D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE },
    { D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE }, { D3DUSAGE_DMAP, D3DRTYPE_TEXTURE },
    { D3DUSAGE_QUERY_VERTEXTEXTURE, D3DRTYPE_TEXTURE }, { D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE },
    { D3DUSAGE_QUERY_SRGBWRITE, D3DRTYPE_TEXTURE }, { D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE },
    { D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, D3DRTYPE_TEXTURE }, { D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DRTYPE_TEXTURE },
    { D3DUSAGE_QUERY_WRAPANDMIP, D3DRTYPE_TEXTURE },
};

/* The runtime's answers built from the driver's caps and formats */
static
VOID
CkQueryAdapter(
    _In_ IDirect3D9* D3D)
{
    D3DCAPS9 Caps = {};
    HRESULT hr;

    printf("GetAdapterCount %u\n", D3D->GetAdapterCount());
    CkPrintIdentifier(D3D);

    hr = D3D->GetDeviceCaps(0, D3DDEVTYPE_HAL, &Caps);
    printf("GetDeviceCaps hr %08lX\n", hr);
    if (SUCCEEDED(hr))
        CkDump("caps", &Caps, sizeof(Caps));

    for (const D3DFORMAT Format : CkFormats)
    {
        std::string Line;
        char Item[16];

        for (const CkUse& Use : CkUses)
        {
            hr = D3D->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, Use.Usage, Use.Type, Format);
            sprintf_s(Item, " %08lX", hr);
            Line += Item;
        }
        printf("CheckDeviceFormat %08X%s\n", Format, Line.c_str());
    }

    for (const D3DFORMAT Format : CkFormats)
    {
        std::string Line;
        char Item[24];

        for (UINT Samples = 0; Samples <= 16; Samples++)
        {
            DWORD Levels = 0;

            hr = D3D->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, Format, TRUE,
                                                 static_cast<D3DMULTISAMPLE_TYPE>(Samples), &Levels);
            sprintf_s(Item, " %lX/%lu", static_cast<ULONG>(hr & 0xFFFF), Levels);
            Line += Item;
        }
        printf("CheckDeviceMultiSampleType %08X%s\n", Format, Line.c_str());
    }

    for (const D3DFORMAT Depth : { D3DFMT_D16, D3DFMT_D24S8, D3DFMT_D24X8, D3DFMT_D32 })
    {
        printf("CheckDepthStencilMatch %08X %08lX %08lX\n", Depth,
               D3D->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, Depth),
               D3D->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, Depth));
    }

    for (const D3DFORMAT Target : { D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A2R10G10B10 })
    {
        printf("CheckDeviceType %08X %08lX %08lX\n", Target,
               D3D->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, Target, TRUE),
               D3D->CheckDeviceType(0, D3DDEVTYPE_HAL, Target, Target, FALSE));
    }
}

/*
 * The runtime hands its batch to the driver on a lock only when the locked handle hashes onto a slot
 * marked by the current batch, so a plain readback flushes at a point that depends on handle values.
 * Locking a surface that was just filled always flushes, which empties the batch at a fixed point.
 */
VOID
CkFlushBatch(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Flush,
    _In_z_ PCSTR Label)
{
    D3DLOCKED_RECT Locked = {};
    HRESULT hr;

    hr = Device->ColorFill(Flush, nullptr, 0);
    printf("%s flush ColorFill hr %08lX\n", Label, hr);
    hr = Flush->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    printf("%s flush LockRect hr %08lX\n", Label, hr);
    if (SUCCEEDED(hr))
        Flush->UnlockRect();
}

/* Reads a render target back through a system memory surface and prints a few pixels and a sum */
static
VOID
CkReadBack(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Target,
    _In_opt_ IDirect3DSurface9* Flush,
    _In_z_ PCSTR Label)
{
    static const POINT Probes[] = { { 0, 0 }, { 10, 10 }, { 30, 20 }, { 63, 47 } };
    IDirect3DSurface9* Copy = nullptr;
    D3DLOCKED_RECT Locked = {};
    D3DSURFACE_DESC Desc = {};
    HRESULT hr;

    if (Flush != nullptr)
        CkFlushBatch(Device, Flush, Label);

    Target->GetDesc(&Desc);
    hr = Device->CreateOffscreenPlainSurface(Desc.Width, Desc.Height, Desc.Format, D3DPOOL_SYSTEMMEM, &Copy, nullptr);
    printf("%s CreateOffscreenPlainSurface hr %08lX\n", Label, hr);
    if (FAILED(hr))
        return;

    hr = Device->GetRenderTargetData(Target, Copy);
    printf("%s GetRenderTargetData hr %08lX\n", Label, hr);
    hr = Copy->LockRect(&Locked, nullptr, D3DLOCK_READONLY);
    printf("%s LockRect hr %08lX\n", Label, hr);
    if (SUCCEEDED(hr))
    {
        ULONG Sum = 0;

        for (UINT Row = 0; Row < Desc.Height; Row++)
        {
            const DWORD* Line = reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) + Row * Locked.Pitch);

            for (UINT Column = 0; Column < Desc.Width; Column++)
                Sum = Sum * 31 + Line[Column];
        }
        printf("%s pixels", Label);
        for (const POINT& Probe : Probes)
        {
            const DWORD* Line =
                reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(Locked.pBits) + Probe.y * Locked.Pitch);

            printf(" %08lX", Line[Probe.x]);
        }
        printf(" sum %08lX\n", Sum);
        Copy->UnlockRect();
    }
    Copy->Release();
}

/* Copies out of a multisampled target: the whole surface resolves, a part of it is drawn */
static
VOID
CkMultisample(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Target,
    _In_ IDirect3DSurface9* Flush)
{
    IDirect3DSurface9* Samples = nullptr;
    RECT Inner = { 8, 8, 40, 32 };
    RECT Part = { 4, 4, 36, 28 };
    RECT Moved = { 20, 12, 52, 36 };
    HRESULT hr;

    printf("== multisample\n");
    hr = Device->CreateRenderTarget(64, 48, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, 0, FALSE, &Samples, nullptr);
    printf("multisampled CreateRenderTarget hr %08lX\n", hr);
    if (Samples == nullptr)
        return;

    hr = Device->ColorFill(Samples, nullptr, 0xFF204060);
    printf("ColorFill hr %08lX\n", hr);
    hr = Device->ColorFill(Samples, &Inner, 0xFFC08040);
    printf("inner ColorFill hr %08lX\n", hr);

    hr = Device->StretchRect(Samples, nullptr, Target, nullptr, D3DTEXF_NONE);
    printf("whole StretchRect hr %08lX\n", hr);
    CkReadBack(Device, Target, Flush, "resolved");

    hr = Device->ColorFill(Target, nullptr, 0xFF000000);
    printf("clear ColorFill hr %08lX\n", hr);
    hr = Device->StretchRect(Samples, &Part, Target, &Moved, D3DTEXF_NONE);
    printf("part StretchRect hr %08lX\n", hr);
    CkReadBack(Device, Target, Flush, "part");
    Samples->Release();
}

/* Reaches the Direct3D 12 device and a surface's resource through IDirect3DDevice9On12, then hands it back */
static
VOID
CkInterop(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Surface,
    _In_ IDirect3DSurface9* Flush)
{
    IDirect3DDevice9On12* On12 = nullptr;
    IDirect3DDevice9Ex* Ex = nullptr;
    ID3D12Device* Device12 = nullptr;
    ID3D12CommandQueue* Queue = nullptr;
    ID3D12Resource* Resource = nullptr;
    ID3D12Fence* Fence = nullptr;
    UINT64 Value = 1;
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    HRESULT hr;

    printf("== interop\n");
    if (SUCCEEDED(Device->QueryInterface(IID_PPV_ARGS(&Ex))))
    {
        IDirect3DSurface9* Shared = nullptr;
        HANDLE Handle = NULL;

        hr = Ex->SetMaximumFrameLatency(2);
        printf("SetMaximumFrameLatency hr %08lX\n", hr);

        /* A shared surface hands out a handle other devices can open */
        hr = Ex->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Shared, &Handle);
        printf("shared CreateRenderTarget hr %08lX handle %s\n", hr, Handle != NULL ? "set" : "none");
        if (Shared != nullptr)
            Shared->Release();
        Ex->Release();
    }

    hr = Device->QueryInterface(IID_PPV_ARGS(&On12));
    printf("QueryInterface IDirect3DDevice9On12 hr %08lX\n", hr);
    if (FAILED(hr))
        return;

    hr = On12->GetD3D12Device(IID_PPV_ARGS(&Device12));
    printf("GetD3D12Device hr %08lX\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = Device12->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(&Queue));
        printf("CreateCommandQueue hr %08lX\n", hr);
    }

    if (Queue != nullptr)
    {
        hr = On12->UnwrapUnderlyingResource(Surface, Queue, IID_PPV_ARGS(&Resource));
        printf("UnwrapUnderlyingResource hr %08lX\n", hr);
        if (Resource != nullptr)
        {
            D3D12_RESOURCE_DESC Desc = Resource->GetDesc();

            printf("  resource dim %u size %llux%u format %u flags %X\n", Desc.Dimension, Desc.Width, Desc.Height,
                   Desc.Format, Desc.Flags);
            Resource->Release();
            hr = Device12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence));
            printf("CreateFence hr %08lX\n", hr);
            if (Fence != nullptr)
            {
                hr = Queue->Signal(Fence, 1);
                printf("Signal hr %08lX\n", hr);
                hr = On12->ReturnUnderlyingResource(Surface, 1, &Value, &Fence);
                printf("ReturnUnderlyingResource hr %08lX\n", hr);
                fflush(stdout);
                CkReadBack(Device, Surface, Flush, "returned");
                Fence->Release();
            }
        }
        Queue->Release();
    }

    if (Device12 != nullptr)
        Device12->Release();
    On12->Release();
}

/* Issues each query type and waits for its answer; timestamps only print whether they were filled */
static
VOID
CkQueries(
    _In_ IDirect3DDevice9* Device)
{
    static const D3DQUERYTYPE Types[] =
    {
        D3DQUERYTYPE_VCACHE, D3DQUERYTYPE_EVENT, D3DQUERYTYPE_OCCLUSION, D3DQUERYTYPE_TIMESTAMP,
        D3DQUERYTYPE_TIMESTAMPDISJOINT, D3DQUERYTYPE_TIMESTAMPFREQ, D3DQUERYTYPE_PIPELINETIMINGS,
    };
    HRESULT hr;

    printf("== queries\n");
    for (const D3DQUERYTYPE Type : Types)
    {
        IDirect3DQuery9* Query = nullptr;
        BYTE Data[16] = {};
        DWORD Size;
        UINT Polls;

        hr = Device->CreateQuery(Type, &Query);
        printf("CreateQuery type %u hr %08lX\n", Type, hr);
        if (FAILED(hr))
            continue;

        Size = Query->GetDataSize();
        if (Type == D3DQUERYTYPE_OCCLUSION || Type == D3DQUERYTYPE_TIMESTAMPDISJOINT)
        {
            hr = Query->Issue(D3DISSUE_BEGIN);
            printf("Issue begin hr %08lX\n", hr);
            hr = Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030, 1.0f, 0);
            printf("Clear hr %08lX\n", hr);
        }
        hr = Query->Issue(D3DISSUE_END);
        printf("Issue end hr %08lX\n", hr);

        CkSetPolling(TRUE);
        for (Polls = 0; Polls < 100000; Polls++)
        {
            hr = Query->GetData(Data, Size, D3DGETDATA_FLUSH);
            if (hr != S_FALSE)
                break;
            Sleep(0);
        }
        CkSetPolling(FALSE);

        printf("GetData size %lu hr %08lX", Size, hr);
        if (Type == D3DQUERYTYPE_TIMESTAMP)
        {
            UINT64 Value = 0;

            CopyMemory(&Value, Data, sizeof(Value));
            printf(" filled %s", static_cast<DWORD>(Value) != 0 ? "yes" : "no");
        }
        else
        {
            for (DWORD Index = 0; Index < Size && Index < sizeof(Data); Index += 4)
            {
                DWORD Value = 0;

                CopyMemory(&Value, Data + Index, Size - Index < 4 ? Size - Index : 4);
                printf(" %08lX", Value);
            }
        }
        printf("\n");
        Query->Release();
    }
}

/* Clears, fills, copies and presents the back buffer, reading the results back */
/* vs_2_0: position and diffuse color passed through */
static const DWORD CkVertexCode[] =
{
    0xFFFE0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x0200001F, 0x8000000A, 0x900F0001,
    0x02000001, 0xC00F0000, 0x90E40000,
    0x02000001, 0xD00F0000, 0x90E40001,
    0x0000FFFF,
};

/* ps_2_0: the interpolated color */
static const DWORD CkPixelCode[] =
{
    0xFFFF0200,
    0x0200001F, 0x80000000, 0x900F0000,
    0x02000001, 0x800F0800, 0x90E40000,
    0x0000FFFF,
};

/* Shaders, declarations, streams and indices: creating, sharing, binding and letting go */
static
VOID
CkShaders(
    _In_ IDirect3DDevice9* Device,
    _In_ IDirect3DSurface9* Flush)
{
    static const D3DVERTEXELEMENT9 Plain[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        D3DDECL_END(),
    };
    static const D3DVERTEXELEMENT9 Transformed[] =
    {
        { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITIONT, 0 },
        { 0, 16, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
        { 0, 20, D3DDECLTYPE_SHORT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END(),
    };
    IDirect3DVertexShader9* Vertex[2] = {};
    IDirect3DPixelShader9* Pixel = nullptr;
    IDirect3DVertexDeclaration9* Decls[2] = {};
    IDirect3DVertexBuffer9* Buffer = nullptr;
    IDirect3DIndexBuffer9* Indices = nullptr;
    HRESULT hr;

    printf("== shaders\n");
    for (UINT Index = 0; Index < _countof(Vertex); Index += 1)
    {
        hr = Device->CreateVertexShader(CkVertexCode, &Vertex[Index]);
        printf("CreateVertexShader %u hr %08lX\n", Index, hr);
    }
    hr = Device->CreatePixelShader(CkPixelCode, &Pixel);
    printf("CreatePixelShader hr %08lX\n", hr);
    hr = Device->CreateVertexDeclaration(Plain, &Decls[0]);
    printf("CreateVertexDeclaration plain hr %08lX\n", hr);
    hr = Device->CreateVertexDeclaration(Transformed, &Decls[1]);
    printf("CreateVertexDeclaration transformed hr %08lX\n", hr);
    hr = Device->CreateVertexBuffer(256, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &Buffer, nullptr);
    printf("CreateVertexBuffer hr %08lX\n", hr);
    hr = Device->CreateIndexBuffer(64, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &Indices, nullptr);
    printf("CreateIndexBuffer hr %08lX\n", hr);

    for (UINT Index = 0; Index < _countof(Decls); Index += 1)
    {
        hr = Device->SetVertexDeclaration(Decls[Index]);
        printf("SetVertexDeclaration %u hr %08lX\n", Index, hr);
    }
    hr = Device->SetVertexShader(Vertex[1]);
    printf("SetVertexShader hr %08lX\n", hr);
    hr = Device->SetPixelShader(Pixel);
    printf("SetPixelShader hr %08lX\n", hr);
    hr = Device->SetStreamSource(0, Buffer, 16, 16);
    printf("SetStreamSource 0 hr %08lX\n", hr);
    hr = Device->SetStreamSource(3, Buffer, 0, 8);
    printf("SetStreamSource 3 hr %08lX\n", hr);
    hr = Device->SetStreamSourceFreq(0, D3DSTREAMSOURCE_INDEXEDDATA | 2);
    printf("SetStreamSourceFreq 0 hr %08lX\n", hr);
    hr = Device->SetStreamSourceFreq(3, D3DSTREAMSOURCE_INSTANCEDATA | 1);
    printf("SetStreamSourceFreq 3 hr %08lX\n", hr);
    hr = Device->SetIndices(Indices);
    printf("SetIndices hr %08lX\n", hr);
    CkFlushBatch(Device, Flush, "shaders bound");
    CkFlushState();

    Device->SetStreamSourceFreq(0, 1);
    Device->SetStreamSourceFreq(3, 1);
    Device->SetStreamSource(3, nullptr, 0, 0);
    if (Buffer != nullptr)
        printf("vertex buffer released %lu\n", Buffer->Release());
    if (Indices != nullptr)
        printf("index buffer released %lu\n", Indices->Release());
    Device->SetVertexShader(nullptr);
    Device->SetPixelShader(nullptr);
    Device->SetVertexDeclaration(nullptr);
    for (UINT Index = 0; Index < _countof(Vertex); Index += 1)
    {
        if (Vertex[Index] != nullptr)
            printf("vertex shader %u released %lu\n", Index, Vertex[Index]->Release());
    }
    if (Pixel != nullptr)
        printf("pixel shader released %lu\n", Pixel->Release());
    for (UINT Index = 0; Index < _countof(Decls); Index += 1)
    {
        if (Decls[Index] != nullptr)
            printf("declaration %u released %lu\n", Index, Decls[Index]->Release());
    }
    CkFlushBatch(Device, Flush, "shaders released");
    CkFlushState();
}

static
VOID
CkScene(
    _In_ IDirect3DDevice9* Device)
{
    IDirect3DSurface9* Back = nullptr;
    IDirect3DSurface9* Target = nullptr;
    IDirect3DSurface9* Depth = nullptr;
    IDirect3DSurface9* Flush = nullptr;
    RECT Fill = { 8, 8, 24, 24 };
    RECT Source = { 0, 0, 32, 24 };
    RECT Dest = { 0, 0, 64, 48 };
    HRESULT hr;

    printf("== scene\n");
    hr = Device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF336699, 1.0f, 0);
    printf("Clear hr %08lX\n", hr);

    hr = Device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &Back);
    printf("GetBackBuffer hr %08lX\n", hr);
    if (FAILED(hr))
        return;

    hr = Device->ColorFill(Back, &Fill, 0xFF00FF00);
    printf("ColorFill hr %08lX\n", hr);

    hr = Device->CreateOffscreenPlainSurface(4, 4, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &Flush, nullptr);
    printf("flush CreateOffscreenPlainSurface hr %08lX\n", hr);
    CkReadBack(Device, Back, Flush, "back");

    hr = Device->CreateDepthStencilSurface(64, 48, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, TRUE, &Depth, nullptr);
    printf("CreateDepthStencilSurface hr %08lX\n", hr);
    if (Depth != nullptr)
    {
        hr = Device->SetDepthStencilSurface(Depth);
        printf("SetDepthStencilSurface hr %08lX\n", hr);
        hr = Device->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 0.5f, 3);
        printf("Clear depth hr %08lX\n", hr);
    }

    hr = Device->CreateRenderTarget(64, 48, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &Target, nullptr);
    printf("CreateRenderTarget hr %08lX\n", hr);
    if (Target != nullptr)
    {
        hr = Device->StretchRect(Back, &Source, Target, &Dest, D3DTEXF_POINT);
        printf("StretchRect hr %08lX\n", hr);
        CkReadBack(Device, Target, Flush, "target");
        CkMultisample(Device, Target, Flush);
    }

    hr = Device->Present(nullptr, nullptr, nullptr, nullptr);
    printf("Present hr %08lX\n", hr);

    if (Flush != nullptr)
        CkShaders(Device, Flush);
    CkDraws(Device, Back, Flush);
    CkQueries(Device);

    if (Target != nullptr)
    {
        CkInterop(Device, Target, Flush);
        Target->Release();
    }
    if (Flush != nullptr)
        Flush->Release();
    if (Depth != nullptr)
    {
        Device->SetDepthStencilSurface(nullptr);
        Depth->Release();
    }
    Back->Release();
}

static
VOID
CkCreateDevice(
    _In_ IDirect3D9* D3D,
    _In_ HWND Window,
    _In_ DWORD Behavior)
{
    D3DPRESENT_PARAMETERS Parameters = {};
    IDirect3DDevice9* Device = nullptr;
    HRESULT hr;

    Parameters.Windowed = TRUE;
    Parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    Parameters.BackBufferWidth = 64;
    Parameters.BackBufferHeight = 48;
    Parameters.hDeviceWindow = Window;
    Parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    hr = D3D->CreateDevice(0, D3DDEVTYPE_HAL, Window, Behavior, &Parameters, &Device);
    printf("IDirect3D9::CreateDevice behavior %08lX hr %08lX\n", Behavior, hr);
    if (Device != nullptr)
    {
        CkScene(Device);
        printf("  device released %lu\n", Device->Release());
    }
    CkFlushState();
}

static
std::wstring
CkDriverPath(
    _In_z_ PCWSTR Runtime)
{
    WCHAR Module[MAX_PATH];
    PWSTR Slash;

    if (_wcsicmp(Runtime, L"system") == 0)
        return std::wstring();
    if (_wcsicmp(Runtime, L"reactx") != 0)
        return Runtime;

    GetModuleFileNameW(NULL, Module, _countof(Module));
    Slash = wcsrchr(Module, L'\\');
    if (Slash != nullptr)
        Slash[1] = UNICODE_NULL;
    return std::wstring(Module) + L"reactx\\d3d9on12.dll";
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) WCHAR** argv)
{
    PCWSTR Runtime = L"system";
    std::wstring Driver;
    WCHAR System[MAX_PATH];
    D3D9ON12_ARGS Args = {};
    PFN_CK_CREATE_9ON12 Create;
    PFN_CK_CREATE_9ON12_EX CreateEx;
    IDirect3D9* D3D;
    IDirect3D9Ex* D3DEx = nullptr;
    HMODULE Module;
    HWND Window;
    HRESULT hr;

    for (int Index = 1; Index < argc; Index++)
    {
        if (_wcsicmp(argv[Index], L"-runtime") == 0 && Index + 1 < argc)
            Runtime = argv[++Index];
    }

    setvbuf(stdout, nullptr, _IOFBF, 1 << 16);
    SetUnhandledExceptionFilter(CkCrashFilter);
    Driver = CkDriverPath(Runtime);
    if (!Driver.empty() && LoadLibraryW(Driver.c_str()) == NULL)
    {
        fwprintf(stderr, L"cannot load %s (%lu)\n", Driver.c_str(), GetLastError());
        return 1;
    }

    GetSystemDirectoryW(System, _countof(System));
    Module = LoadLibraryW((std::wstring(System) + L"\\d3d9.dll").c_str());
    if (Module == NULL || !CkPatchImports(Module))
    {
        fwprintf(stderr, L"cannot load and patch the system d3d9.dll\n");
        return 1;
    }

    Create = reinterpret_cast<PFN_CK_CREATE_9ON12>(GetProcAddress(Module, "Direct3DCreate9On12"));
    CreateEx = reinterpret_cast<PFN_CK_CREATE_9ON12_EX>(GetProcAddress(Module, "Direct3DCreate9On12Ex"));
    if (Create == nullptr || CreateEx == nullptr)
        return 1;

    if (!CkHookD3D12())
        fwprintf(stderr, L"the Direct3D 12 methods are not traced\n");

    Window = CkCreateWindow();

    /* A hidden window counts as occluded and its presents never reach the driver */
    ShowWindow(Window, SW_SHOWNOACTIVATE);
    UpdateWindow(Window);
    Args.Enable9On12 = TRUE;

    printf("== Direct3DCreate9On12\n");
    D3D = Create(D3D_SDK_VERSION, &Args, 1);
    printf("Direct3DCreate9On12 %s\n", D3D != nullptr ? "object" : "null");
    if (D3D != nullptr)
    {
        CkQueryAdapter(D3D);
        CkCreateDevice(D3D, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING);
        CkCreateDevice(D3D, Window, D3DCREATE_SOFTWARE_VERTEXPROCESSING);
        printf("released %lu\n", D3D->Release());
    }

    printf("== Direct3DCreate9On12Ex\n");
    hr = CreateEx(D3D_SDK_VERSION, &Args, 1, &D3DEx);
    printf("Direct3DCreate9On12Ex hr %08lX\n", hr);
    if (D3DEx != nullptr)
    {
        CkQueryAdapter(D3DEx);
        CkCreateDevice(D3DEx, Window, D3DCREATE_HARDWARE_VERTEXPROCESSING);
        printf("released %lu\n", D3DEx->Release());
    }

    printf("adapters opened %u\n", Ck.Adapters);
    fflush(stdout);
    DestroyWindow(Window);
    return 0;
}
