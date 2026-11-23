/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel adapter and Direct3D 9 user mode driver behind a display
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* The adapter identity packet the display configuration returns for its private query */
static constexpr DISPLAYCONFIG_DEVICE_INFO_TYPE DdIdentityPacketType =
    static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(0xFFFFFFFE);
static constexpr UINT32 DdIdentityPacketSize = 0x808;
static constexpr SIZE_T DdIdentityVendorOffset = 0x344;
static constexpr SIZE_T DdIdentityDeviceOffset = 0x348;
static constexpr SIZE_T DdIdentitySubSysLowOffset = 0x34C;
static constexpr SIZE_T DdIdentitySubSysHighOffset = 0x350;
static constexpr SIZE_T DdIdentityRevisionOffset = 0x354;
static constexpr SIZE_T DdIdentityModelOffset = 0x358;

/* Driver models whose OpenAdapter wants the oldest runtime versions first */
static constexpr UINT DdLegacyModel = 1200;
static constexpr DWORD DdVendorAmd = 0x1002;
static constexpr DWORD DdVendorQualcomm = 0x4D4F4351;
static constexpr UINT DdFallbackModel = 1000;

/* DirectDraw opens the user mode driver at the DirectX 7 interface */
static constexpr UINT DdUmdInterface = 7;

/* Runtime versions tried newest first */
static const UINT DdUmdVersions[] =
{
    D3D_UMD_INTERFACE_VERSION_WDDM3_2_2, D3D_UMD_INTERFACE_VERSION_WDDM3_1_1, D3D_UMD_INTERFACE_VERSION_WDDM3_0_1,
    D3D_UMD_INTERFACE_VERSION_WDDM2_9_1, D3D_UMD_INTERFACE_VERSION_WDDM2_8_1, D3D_UMD_INTERFACE_VERSION_WDDM2_7_2,
    D3D_UMD_INTERFACE_VERSION_WDDM2_6_4, D3D_UMD_INTERFACE_VERSION_WDDM2_5_3, D3D_UMD_INTERFACE_VERSION_WDDM2_4_2,
    D3D_UMD_INTERFACE_VERSION_WDDM2_3_2, D3D_UMD_INTERFACE_VERSION_WDDM2_2_2, D3D_UMD_INTERFACE_VERSION_WDDM2_1_4,
    D3D_UMD_INTERFACE_VERSION_WDDM2_0_M2_2, D3D_UMD_INTERFACE_VERSION_WDDM2_0_M1_3, D3D_UMD_INTERFACE_VERSION_WDDM1_3,
    D3D_UMD_INTERFACE_VERSION_WIN8, D3D_UMD_INTERFACE_VERSION_WIN7,
};
static constexpr UINT DdLegacyFirstVersion = 15;

/* Below this much local memory the runtime borrows from the shared pool */
static constexpr ULONGLONG DdDefaultMinimumLocal = 0x2000000;

static
PFND3DKMT_OPENADAPTERFROMHDC
DdOpenFromHdcThunk(VOID)
{
    const KmtThunks* Kmt = RxGetKmtThunks();

    return Kmt != nullptr ? Kmt->OpenAdapterFromHdc : nullptr;
}

HRESULT
DdHresultFromStatus(
    _In_ NTSTATUS Status)
{
    switch (static_cast<ULONG>(Status))
    {
    case STATUS_SUCCESS:
    case 0xC01E0006:
        return S_OK;
    case STATUS_PENDING:
        return E_PENDING;
    case STATUS_INVALID_PARAMETER:
    case STATUS_INVALID_PARAMETER_1:
    case STATUS_INVALID_PARAMETER_2:
    case STATUS_INVALID_PARAMETER_3:
    case STATUS_INVALID_PARAMETER_4:
    case STATUS_INVALID_PARAMETER_5:
    case STATUS_INVALID_PARAMETER_6:
    case STATUS_INVALID_PARAMETER_7:
    case STATUS_INVALID_PARAMETER_8:
    case STATUS_INVALID_PARAMETER_9:
    case STATUS_INVALID_PARAMETER_10:
    case STATUS_INVALID_PARAMETER_11:
    case STATUS_INVALID_PARAMETER_12:
        return E_INVALIDARG;
    case STATUS_NO_MEMORY:
    case STATUS_COMMITMENT_LIMIT:
        return E_OUTOFMEMORY;
    case STATUS_INVALID_USER_BUFFER:
        return 0x88760879;
    case STATUS_ACCESS_DENIED:
    case STATUS_INSUFFICIENT_RESOURCES:
        return 0x8876086A;
    case STATUS_PRIVILEGED_INSTRUCTION:
        return 0x88760871;
    case STATUS_ILLEGAL_INSTRUCTION:
        return 0x88760872;
    case STATUS_INVALID_HANDLE:
    case 0xC01E0112:
    case 0xC01E0113:
    case 0xC01E0114:
    case 0xC01E0115:
        return 0x88760873;
    case STATUS_DEVICE_REMOVED:
        return 0x88760870;
    case 0xC01E0102:
        return 0x8876021C;
    case 0xC01E0100:
        return 0x8876017C;
    case 0xC01E0109:
        return 0xC0262109;
    case 0xC01E0111:
        return 0xC0262111;
    case 0xC01E0009:
        OutputDebugStringW(L"User/kernel-mode driver mismatch");
        DebugBreak();
        return E_FAIL;
    default:
        return E_FAIL;
    }
}

static
NTSTATUS
DdQueryKernel(
    _In_ D3DKMT_HANDLE Adapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYADAPTERINFO Query = {};

    if (Kmt == nullptr || Kmt->QueryAdapterInfo == nullptr)
        return STATUS_NOT_SUPPORTED;

    Query.hAdapter = Adapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return Kmt->QueryAdapterInfo(&Query);
}

static
HRESULT
APIENTRY
DdQueryAdapterInfoCb(
    _In_ HANDLE hAdapter,
    _In_ const D3DDDICB_QUERYADAPTERINFO* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hAdapter);

    return DdHresultFromStatus(DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_UMDRIVERPRIVATE,
                                             Args->pPrivateDriverData, Args->PrivateDriverDataSize));
}

static
HRESULT
APIENTRY
DdQueryAdapterInfoCb2(
    _In_ HANDLE hAdapter,
    _In_ const D3DDDICB_QUERYADAPTERINFO2* Args)
{
    DdHardware* Hardware = static_cast<DdHardware*>(hAdapter);
    KMTQUERYADAPTERINFOTYPE Type;

    if (Args->QueryType == D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE)
        Type = KMTQAITYPE_UMDRIVERPRIVATE;
    else if (Args->QueryType == D3DDDI_QUERYADAPTERTYPE_QUERYREGISTRY)
        Type = KMTQAITYPE_QUERYREGISTRY;
    else
        return E_INVALIDARG;

    return DdHresultFromStatus(DdQueryKernel(Hardware->AdapterHandle, Type, Args->pPrivateDriverData,
                                             Args->PrivateDriverDataSize));
}

/* Reads the private adapter packet; the driver model field decides OpenAdapter's first version */
static
BOOLEAN
DdReadIdentityPacket(
    _In_ const LUID& AdapterLuid,
    _In_ UINT32 SourceId,
    _Out_writes_bytes_(DdIdentityPacketSize) BYTE* Packet)
{
    const RxDisplayConfigRoutines* Config = RxGetDisplayConfig();
    DISPLAYCONFIG_DEVICE_INFO_HEADER* Header = reinterpret_cast<DISPLAYCONFIG_DEVICE_INFO_HEADER*>(Packet);

    ZeroMemory(Packet, DdIdentityPacketSize);
    Header->type = DdIdentityPacketType;
    Header->size = DdIdentityPacketSize;
    Header->adapterId = AdapterLuid;
    Header->id = SourceId;

    if (Config == nullptr)
        return FALSE;
    return Config->GetDeviceInfo(Header) == ERROR_SUCCESS;
}

static
DWORD
DdPacketValue(
    _In_reads_bytes_(DdIdentityPacketSize) const BYTE* Packet,
    _In_ SIZE_T Offset)
{
    DWORD Value;

    memcpy(&Value, Packet + Offset, sizeof(Value));
    return Value;
}

static
BOOLEAN
DdOpenDriverAdapter(
    _Inout_ DdHardware* Hardware)
{
    PFND3DDDI_OPENADAPTER OpenAdapter;
    D3DDDIARG_OPENADAPTER Args;
    BYTE* Packet;
    UINT Model = DdFallbackModel;
    UINT First = 0;
    UINT Index;
    DWORD Vendor;
    HRESULT hr = E_FAIL;

    OpenAdapter = reinterpret_cast<PFND3DDDI_OPENADAPTER>(GetProcAddress(Hardware->Module, "OpenAdapter"));
    if (OpenAdapter == nullptr)
    {
        DdTrace(L"UMD %s has no OpenAdapter", Hardware->UmdName);
        return FALSE;
    }

    Packet = new (std::nothrow) BYTE[DdIdentityPacketSize];
    if (Packet != nullptr)
    {
        if (DdReadIdentityPacket(Hardware->AdapterLuid, Hardware->VidPnSourceId, Packet))
        {
            Model = DdPacketValue(Packet, DdIdentityModelOffset);
            Vendor = DdPacketValue(Packet, DdIdentityVendorOffset);
            if (Model == DdLegacyModel && (Vendor == DdVendorAmd || Vendor == DdVendorQualcomm))
                First = DdLegacyFirstVersion;
        }
        delete[] Packet;
    }
    Hardware->IsOldDisplayModel = Model < DdLegacyModel;

    Hardware->Callbacks.pfnQueryAdapterInfoCb = DdQueryAdapterInfoCb;
    Hardware->Callbacks.pfnQueryAdapterInfoCb2 = DdQueryAdapterInfoCb2;

    for (Index = First; Index < _countof(DdUmdVersions); Index++)
    {
        ZeroMemory(&Args, sizeof(Args));
        Args.hAdapter = Hardware;
        Args.Interface = DdUmdInterface;
        Args.Version = DdUmdVersions[Index];
        Args.pAdapterCallbacks = &Hardware->Callbacks;
        Args.pAdapterFuncs = &Hardware->Functions;

        hr = OpenAdapter(&Args);
        if (SUCCEEDED(hr))
        {
            Hardware->DriverAdapter = Args.hAdapter;
            Hardware->RuntimeVersion = Args.Version;
            Hardware->DriverVersion = Args.DriverVersion;
            return TRUE;
        }
    }

    DdTrace(L"OpenAdapter on %s failed 0x%08lX for every runtime version", Hardware->UmdName, hr);
    return FALSE;
}

static
VOID
DdClampMemory(
    _Inout_ ULONGLONG* Local,
    _Inout_ ULONGLONG* NonLocal)
{
    ULONGLONG Total = *Local + *NonLocal;
    ULONGLONG Limit;
    ULONGLONG Excess;
    DWORD LowCap = 0;
    DWORD Size;
    HKEY Key;

    if (Total <= 0x7FFF0000)
        return;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", 0, KEY_QUERY_VALUE, &Key) == ERROR_SUCCESS)
    {
        Size = sizeof(LowCap);
        if (RegQueryValueExA(Key, "LowVidMemCap", NULL, NULL, reinterpret_cast<BYTE*>(&LowCap), &Size) != ERROR_SUCCESS)
            LowCap = 0;
        RegCloseKey(Key);
    }

    if (LowCap != 0)
    {
        Limit = 0x7FFF0000;
    }
    else
    {
        Limit = 0xFFFF0000;
        if (Total <= Limit)
            return;
    }

    /* Take the excess from the shared pool first, but leave it at most 1 GB less 64 KB */
    Excess = Total - Limit;
    if (*NonLocal > 0x3FFF0000)
    {
        if (*NonLocal - 0x3FFF0000 <= Excess)
        {
            Excess -= *NonLocal - 0x3FFF0000;
            *NonLocal = 0x3FFF0000;
        }
        else
        {
            *NonLocal -= Excess;
            Excess = 0;
        }
    }
    *Local -= Excess;
}

static
ULONGLONG
DdMinimumLocalMemory(VOID)
{
    ULONGLONG Minimum = DdDefaultMinimumLocal;
    DWORD Size = sizeof(Minimum);
    DWORD Type;
    HKEY Key;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D", 0, KEY_QUERY_VALUE, &Key) != ERROR_SUCCESS)
        return DdDefaultMinimumLocal;

    if (RegQueryValueExA(Key, "MinimumLocalVidMem", NULL, &Type, reinterpret_cast<BYTE*>(&Minimum), &Size) != ERROR_SUCCESS ||
        Type != REG_QWORD || Size != sizeof(Minimum))
    {
        Minimum = DdDefaultMinimumLocal;
    }
    RegCloseKey(Key);
    return Minimum;
}

static
BOOLEAN
DdQueryAdapterState(
    _Inout_ DdHardware* Hardware)
{
    D3DKMT_SEGMENTSIZEINFO Segments = {};
    ULONGLONG Minimum;
    ULONGLONG Borrow;
    NTSTATUS Status;

    Status = DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_GETSEGMENTSIZE, &Segments, sizeof(Segments));
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"segment size query failed 0x%08lX", Status);
        return FALSE;
    }

    Hardware->LocalMemory = Segments.DedicatedVideoMemorySize + Segments.DedicatedSystemMemorySize;
    Hardware->NonLocalMemory = Segments.SharedSystemMemorySize;
    DdClampMemory(&Hardware->LocalMemory, &Hardware->NonLocalMemory);

    Status = DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_FLIPQUEUEINFO, &Hardware->FlipQueue,
                           sizeof(Hardware->FlipQueue));
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"flip queue query failed 0x%08lX", Status);
        return FALSE;
    }

    /* Adapters with too little local memory get some of the shared pool counted as local */
    Minimum = DdMinimumLocalMemory();
    if (Hardware->LocalMemory < Minimum)
    {
        Borrow = Minimum - Hardware->LocalMemory;
        if (Hardware->NonLocalMemory <= Borrow)
        {
            Hardware->LocalMemory += Hardware->NonLocalMemory;
            Hardware->NonLocalMemory = 0;
        }
        else
        {
            Hardware->LocalMemory += Borrow;
            Hardware->NonLocalMemory -= Borrow;
        }
    }
    Hardware->LocalMemoryFree = Hardware->LocalMemory;
    Hardware->NonLocalMemoryFree = Hardware->NonLocalMemory;

    Status = DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_ADAPTERTYPE, &Hardware->AdapterType,
                           sizeof(Hardware->AdapterType));
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"adapter type query failed 0x%08lX", Status);
        return FALSE;
    }

    Status = DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_WDDM_1_2_CAPS, &Hardware->Wddm12Caps,
                           sizeof(Hardware->Wddm12Caps));
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"WDDM 1.2 caps query failed 0x%08lX", Status);
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
DdLoadUserModeDriver(
    _Inout_ DdHardware* Hardware)
{
    D3DKMT_UMDFILENAMEINFO FileName = {};
    NTSTATUS Status;

    FileName.Version = KMTUMDVERSION_DX9;
    Status = DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_UMDRIVERNAME, &FileName, sizeof(FileName));
    if (!NT_SUCCESS(Status) || FileName.UmdFileName[0] == UNICODE_NULL)
    {
        DdTrace(L"adapter has no Direct3D 9 UMD (0x%08lX), running without hardware", Status);
        return FALSE;
    }

    wcsncpy_s(Hardware->UmdName, FileName.UmdFileName, _TRUNCATE);
    Hardware->Module = LoadLibraryExW(Hardware->UmdName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Hardware->Module == NULL && GetLastError() == ERROR_INVALID_PARAMETER)
        Hardware->Module = LoadLibraryW(Hardware->UmdName);
    if (Hardware->Module == NULL)
    {
        DdTrace(L"cannot load UMD %s (%lu)", Hardware->UmdName, GetLastError());
        return FALSE;
    }

    return TRUE;
}

_Ret_maybenull_
DdHardware*
DdOpenHardware(
    _In_ HDC Dc)
{
    PFND3DKMT_OPENADAPTERFROMHDC OpenFromHdc = DdOpenFromHdcThunk();
    D3DKMT_OPENADAPTERFROMHDC Open = {};
    BOOLEAN NeedsUpdate = FALSE;
    DdHardware* Hardware;
    NTSTATUS Status;

    if (OpenFromHdc == nullptr || RxGetKmtThunks() == nullptr)
    {
        DdTrace(L"no D3DKMT thunks, running without hardware");
        return nullptr;
    }

    Open.hDc = Dc;
    Status = OpenFromHdc(&Open);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTOpenAdapterFromHdc failed 0x%08lX", Status);
        return nullptr;
    }

    Hardware = new (std::nothrow) DdHardware;
    if (Hardware == nullptr)
    {
        D3DKMT_CLOSEADAPTER Close = { Open.hAdapter };

        RxGetKmtThunks()->CloseAdapter(&Close);
        return nullptr;
    }

    Hardware->AdapterHandle = Open.hAdapter;
    Hardware->AdapterLuid = Open.AdapterLuid;
    Hardware->VidPnSourceId = Open.VidPnSourceId;

    /* A driver waiting on an update reboot is treated as absent */
    if (NT_SUCCESS(DdQueryKernel(Hardware->AdapterHandle, KMTQAITYPE_CHECKDRIVERUPDATESTATUS, &NeedsUpdate,
                                 sizeof(NeedsUpdate))) && NeedsUpdate)
    {
        DdTrace(L"display driver has a pending update, running without hardware");
        DdCloseHardware(Hardware);
        return nullptr;
    }

    if (!DdLoadUserModeDriver(Hardware) || !DdCreateKernelDevice(Hardware) || !DdOpenDriverAdapter(Hardware) ||
        !DdCreateDriverDevice(Hardware) || !DdQueryAdapterState(Hardware))
    {
        DdCloseHardware(Hardware);
        return nullptr;
    }

    return Hardware;
}

VOID
DdCloseHardware(
    _In_opt_ DdHardware* Hardware)
{
    D3DKMT_CLOSEADAPTER Close;

    if (Hardware == nullptr)
        return;

    DdDestroyDriverDevice(Hardware);
    if (Hardware->DriverAdapter != NULL && Hardware->Functions.pfnCloseAdapter != nullptr)
        Hardware->Functions.pfnCloseAdapter(Hardware->DriverAdapter);
    DdDestroyKernelDevice(Hardware);

    /* The UMD stays mapped: drivers start worker threads that outlive the adapter */
    if (Hardware->AdapterHandle != 0)
    {
        Close.hAdapter = Hardware->AdapterHandle;
        RxGetKmtThunks()->CloseAdapter(&Close);
    }

    delete[] Hardware->Formats;
    delete Hardware;
}

HRESULT
DdHardwareGetCaps(
    _In_ DdHardware* Hardware,
    _In_ D3DDDICAPS_TYPE Type,
    _In_opt_ VOID* Info,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    D3DDDIARG_GETCAPS Caps;

    Caps.Type = Type;
    Caps.pInfo = Info;
    Caps.pData = Data;
    Caps.DataSize = Size;
    return Hardware->Functions.pfnGetCaps(Hardware->DriverAdapter, &Caps);
}

BOOLEAN
DdHardwareQueryFormats(
    _Inout_ DdHardware* Hardware)
{
    UINT Count = 0;
    HRESULT hr;

    if (Hardware->FormatsQueried)
        return Hardware->Formats != nullptr;

    Hardware->FormatsQueried = TRUE;
    hr = DdHardwareGetCaps(Hardware, D3DDDICAPS_GETFORMATCOUNT, nullptr, &Count, sizeof(Count));
    if (FAILED(hr) || Count == 0)
    {
        if (FAILED(hr))
            DdTrace(L"GetCaps(GETFORMATCOUNT) failed 0x%08lX", hr);
        return FALSE;
    }

    Hardware->Formats = new (std::nothrow) FORMATOP[Count];
    if (Hardware->Formats == nullptr)
        return FALSE;

    hr = DdHardwareGetCaps(Hardware, D3DDDICAPS_GETFORMATDATA, nullptr, Hardware->Formats, Count * sizeof(FORMATOP));
    if (FAILED(hr))
    {
        DdTrace(L"GetCaps(GETFORMATDATA) failed 0x%08lX", hr);
        delete[] Hardware->Formats;
        Hardware->Formats = nullptr;
        return FALSE;
    }

    Hardware->FormatCount = Count;
    return TRUE;
}

BOOLEAN
DdReadAdapterIdentity(
    _In_z_ PCSTR DeviceName,
    _Out_ DWORD* VendorId,
    _Out_ DWORD* DeviceId,
    _Out_ DWORD* SubSysId,
    _Out_ DWORD* Revision,
    _Out_writes_z_(DriverSize) PSTR Driver,
    _In_ SIZE_T DriverSize)
{
    PFND3DKMT_OPENADAPTERFROMHDC OpenFromHdc = DdOpenFromHdcThunk();
    D3DKMT_OPENADAPTERFROMHDC Open = {};
    D3DKMT_UMDFILENAMEINFO FileName = {};
    D3DKMT_CLOSEADAPTER Close;
    BYTE* Packet;
    PCWSTR Base;
    HDC Dc;
    NTSTATUS Status;

    *VendorId = 0;
    *DeviceId = 0;
    *SubSysId = 0;
    *Revision = 0;
    if (OpenFromHdc == nullptr)
        return FALSE;

    Dc = DdCreateDisplayDc(DeviceName);
    if (Dc == NULL)
        return FALSE;

    Open.hDc = Dc;
    Status = OpenFromHdc(&Open);
    DeleteDC(Dc);
    if (!NT_SUCCESS(Status))
        return FALSE;

    /* Without a UMD name the runtime reports the 9on12 mapping layer */
    FileName.Version = KMTUMDVERSION_DX9;
    if (!NT_SUCCESS(DdQueryKernel(Open.hAdapter, KMTQAITYPE_UMDRIVERNAME, &FileName, sizeof(FileName))))
        wcscpy_s(FileName.UmdFileName, L"D3D9on12.dll");
    FileName.UmdFileName[_countof(FileName.UmdFileName) - 1] = UNICODE_NULL;

    Packet = new (std::nothrow) BYTE[DdIdentityPacketSize];
    if (Packet != nullptr)
    {
        DdReadIdentityPacket(Open.AdapterLuid, Open.VidPnSourceId, Packet);
        *VendorId = DdPacketValue(Packet, DdIdentityVendorOffset);
        *DeviceId = DdPacketValue(Packet, DdIdentityDeviceOffset);
        *SubSysId = DdPacketValue(Packet, DdIdentitySubSysLowOffset) |
                    (DdPacketValue(Packet, DdIdentitySubSysHighOffset) << 16);
        *Revision = DdPacketValue(Packet, DdIdentityRevisionOffset);
        delete[] Packet;
    }

    Close.hAdapter = Open.hAdapter;
    RxGetKmtThunks()->CloseAdapter(&Close);

    Base = wcsrchr(FileName.UmdFileName, L'\\');
    Base = Base != nullptr ? Base + 1 : FileName.UmdFileName;
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, Base, -1, Driver, static_cast<int>(DriverSize), NULL, NULL);
    Driver[DriverSize - 1] = '\0';
    return TRUE;
}

BOOLEAN
DdAdapterLuidFromDc(
    _In_ HDC Dc,
    _Out_ LUID* AdapterLuid)
{
    PFND3DKMT_OPENADAPTERFROMHDC OpenFromHdc = DdOpenFromHdcThunk();
    D3DKMT_OPENADAPTERFROMHDC Open = {};
    D3DKMT_CLOSEADAPTER Close;

    ZeroMemory(AdapterLuid, sizeof(*AdapterLuid));
    if (OpenFromHdc == nullptr || RxGetKmtThunks() == nullptr)
        return FALSE;

    Open.hDc = Dc;
    if (!NT_SUCCESS(OpenFromHdc(&Open)))
        return FALSE;

    *AdapterLuid = Open.AdapterLuid;
    Close.hAdapter = Open.hAdapter;
    RxGetKmtThunks()->CloseAdapter(&Close);
    return TRUE;
}

/* Adds one DXGI segment group's budget to the totals, and what is left of it to the free count */
static
HRESULT
DdAddSegmentGroup(
    _In_ IDXGIAdapter3* Adapter,
    _In_ DXGI_MEMORY_SEGMENT_GROUP Group,
    _Inout_ DWORD* Total,
    _Inout_ DWORD* Free)
{
    DXGI_QUERY_VIDEO_MEMORY_INFO Info = {};
    HRESULT hr;

    hr = Adapter->QueryVideoMemoryInfo(0, Group, &Info);
    if (FAILED(hr))
        return hr;

    *Total += static_cast<DWORD>(Info.Budget);
    if (Info.Budget > Info.CurrentUsage)
        *Free += static_cast<DWORD>(Info.Budget - Info.CurrentUsage);
    return S_OK;
}

HRESULT
DdQueryEmulatedMemory(
    _In_ const LUID& AdapterLuid,
    _In_ DWORD Caps,
    _Out_ DWORD* Total,
    _Out_ DWORD* Free)
{
    typedef HRESULT (WINAPI* PDD_CREATE_FACTORY)(REFIID, VOID**);
    static PDD_CREATE_FACTORY CreateFactory = []()
    {
        HMODULE Dxgi = LoadLibraryW(L"dxgi.dll");

        if (Dxgi == NULL)
            return static_cast<PDD_CREATE_FACTORY>(nullptr);
        return reinterpret_cast<PDD_CREATE_FACTORY>(GetProcAddress(Dxgi, "CreateDXGIFactory1"));
    }();
    IDXGIFactory4* Factory = nullptr;
    IDXGIAdapter3* Adapter = nullptr;
    BOOLEAN Local = (Caps & DDSCAPS_LOCALVIDMEM) != 0;
    BOOLEAN NonLocal = (Caps & DDSCAPS_NONLOCALVIDMEM) != 0;
    HRESULT hr = E_FAIL;

    *Total = 0;
    *Free = 0;

    /* Without a pool named, local memory counts and textures or video memory add the shared pool */
    if (!Local && !NonLocal)
    {
        Local = TRUE;
        NonLocal = (Caps & (DDSCAPS_TEXTURE | DDSCAPS_RESERVED2 | DDSCAPS_VIDEOMEMORY)) != 0;
    }

    if (CreateFactory != nullptr)
        hr = CreateFactory(__uuidof(IDXGIFactory4), reinterpret_cast<VOID**>(&Factory));
    if (SUCCEEDED(hr))
        hr = Factory->EnumAdapterByLuid(AdapterLuid, __uuidof(IDXGIAdapter3), reinterpret_cast<VOID**>(&Adapter));

    if (SUCCEEDED(hr) && Local)
        hr = DdAddSegmentGroup(Adapter, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, Total, Free);
    if (SUCCEEDED(hr) && NonLocal)
        hr = DdAddSegmentGroup(Adapter, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, Total, Free);

    if (Adapter != nullptr)
        Adapter->Release();
    if (Factory != nullptr)
        Factory->Release();

    if (FAILED(hr))
        DD_TRACE_ONCE(L"cannot read emulated device memory from DXGI (0x%08lX)", hr);
    return hr;
}
