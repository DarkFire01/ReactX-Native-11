/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel adapter plus the vendor UMD that drives it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "umdadapter.hpp"
#include "calltrap.hpp"

static const PCWSTR AdapterCallbacksName = L"D3DDDI_ADAPTERCALLBACKS";

/*
 * Tried in order. WDDM 2.0 comes last so drivers that also speak 1.3 stay on it;
 * drivers that start at 2.0, such as WARP, get it.
 */
static constexpr DdiRevision RuntimeRevisions[] =
{
    { D3DWDDM1_3_DDI_INTERFACE_VERSION, D3DWDDM1_3_DDI_BUILD_VERSION, 0x0603, L"WDDM 1.3" },
    { D3D11_1_DDI_INTERFACE_VERSION, D3D11_1_DDI_BUILD_VERSION, 0x0602, L"D3D11.1" },
    { D3D11_1_DDI_INTERFACE_VERSION_RC, D3D11_1_DDI_BUILD_VERSION_RC, 0x0602, L"D3D11.1 RC" },
    { D3DWDDM2_0_DDI_INTERFACE_VERSION, D3DWDDM2_0_DDI_BUILD_VERSION, 0x0A00, L"WDDM 2.0" },
};

static
BOOLEAN
RxIsRevisionAllowed(
    _In_ const DdiRevision& Revision)
{
    return Revision.MinimumWinnt <= _WIN32_WINNT;
}

static
UINT
RxMakeRuntimeVersion(
    _In_ const DdiRevision& Revision)
{
    return (Revision.Build << 16) | DXGI_OFFER_RECLAIM;
}

UmdAdapter::~UmdAdapter()
{
    D3DKMT_CLOSEADAPTER Close;

    CloseDriverAdapter();

    /* The UMD stays mapped: drivers start worker threads that outlive the adapter */
    Driver = NULL;

    if (KmtAdapter != 0)
    {
        Close.hAdapter = KmtAdapter;
        Kmt->CloseAdapter(&Close);
        KmtAdapter = 0;
    }
}

HRESULT
UmdAdapter::Open(
    _In_opt_ const LUID* AdapterLuid)
{
    D3DKMT_ADAPTERTYPE Type = {};
    HRESULT hr;

    Kmt = RxGetKmtThunks();
    if (Kmt == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Callbacks.pfnQueryAdapterInfoCb = QueryAdapterInfoCb;
    Callbacks.pfnGetMultisampleMethodListCb = GetMultisampleMethodListCb;
    Callbacks.pfnQueryAdapterInfoCb2 = QueryAdapterInfoCb2;
    RxFillMissingCallbacks<&AdapterCallbacksName>(Callbacks);

    hr = OpenKernelAdapter(AdapterLuid);
    if (FAILED(hr))
        return hr;

    if (SUCCEEDED(QueryKernel(KMTQAITYPE_ADAPTERTYPE, &Type, sizeof(Type))))
        IsSoftwareAdapter = Type.SoftwareDevice != 0;

    hr = LoadDriver();
    if (FAILED(hr))
        return hr;

    hr = NegotiateRevision();
    if (FAILED(hr))
        return hr;

    return QueryCaps();
}

BOOLEAN
UmdAdapter::SupportsPipelineLevel(
    _In_ D3D11DDI_3DPIPELINELEVEL Level) const
{
    /* Feature level 12 devices only exist on the WDDM 2.0 DDI */
    if ((Level == D3DWDDM2_0DDI_3DPIPELINELEVEL_12_0 || Level == D3DWDDM2_0DDI_3DPIPELINELEVEL_12_1) &&
        Revision->Interface < D3DWDDM2_0_DDI_INTERFACE_VERSION)
    {
        return FALSE;
    }

    return (PipelineCaps & D3D11DDI_ENCODE_3DPIPELINESUPPORT_CAP(Level)) != 0;
}

HRESULT
UmdAdapter::OpenKernelAdapter(
    _In_opt_ const LUID* AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMLUID FromLuid = {};
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME FromDisplay = {};
    D3DKMT_ENUMADAPTERS Enum = {};
    D3DKMT_CLOSEADAPTER Close;
    DISPLAY_DEVICEW Display;
    DWORD Index;
    NTSTATUS Status;

    if (AdapterLuid != nullptr)
    {
        if (Kmt->OpenAdapterFromLuid == nullptr)
            return DXGI_ERROR_UNSUPPORTED;

        FromLuid.AdapterLuid = *AdapterLuid;
        Status = Kmt->OpenAdapterFromLuid(&FromLuid);
        if (!NT_SUCCESS(Status))
        {
            RX_TRACE(L"D3DKMTOpenAdapterFromLuid failed 0x%08lX", Status);
            return RxHresultFromStatus(Status);
        }

        KmtAdapter = FromLuid.hAdapter;
        Luid = *AdapterLuid;
        return S_OK;
    }

    /* The default adapter is the one driving the primary display */
    if (Kmt->OpenAdapterFromGdiDisplayName != nullptr)
    {
        Display.cb = sizeof(Display);
        for (Index = 0; EnumDisplayDevicesW(NULL, Index, &Display, 0); Index++)
        {
            if ((Display.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) == 0)
                continue;

            wcscpy_s(FromDisplay.DeviceName, Display.DeviceName);
            Status = Kmt->OpenAdapterFromGdiDisplayName(&FromDisplay);
            if (NT_SUCCESS(Status))
            {
                KmtAdapter = FromDisplay.hAdapter;
                Luid = FromDisplay.AdapterLuid;
                return S_OK;
            }
            break;
        }
    }

    if (Kmt->EnumAdapters == nullptr)
        return DXGI_ERROR_NOT_FOUND;

    Status = Kmt->EnumAdapters(&Enum);
    if (!NT_SUCCESS(Status) || Enum.NumAdapters == 0)
        return DXGI_ERROR_NOT_FOUND;

    KmtAdapter = Enum.Adapters[0].hAdapter;
    Luid = Enum.Adapters[0].AdapterLuid;
    for (Index = 1; Index < Enum.NumAdapters; Index++)
    {
        Close.hAdapter = Enum.Adapters[Index].hAdapter;
        Kmt->CloseAdapter(&Close);
    }

    return S_OK;
}

HRESULT
UmdAdapter::LoadDriver()
{
    static constexpr KMTUMDVERSION Versions[] = { KMTUMDVERSION_DX11, KMTUMDVERSION_DX10 };
    D3DKMT_UMDFILENAMEINFO FileName = {};
    D3DKMT_QUERYADAPTERINFO Query = {};
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    DWORD LoadFlags;

    for (KMTUMDVERSION Version : Versions)
    {
        FileName.Version = Version;
        Query.hAdapter = KmtAdapter;
        Query.Type = KMTQAITYPE_UMDRIVERNAME;
        Query.pPrivateDriverData = &FileName;
        Query.PrivateDriverDataSize = sizeof(FileName);

        Status = Kmt->QueryAdapterInfo(&Query);
        if (NT_SUCCESS(Status) && FileName.UmdFileName[0] != UNICODE_NULL)
            break;
    }

    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"adapter has no D3D10/11 UMD (0x%08lX)", Status);
        return DXGI_ERROR_UNSUPPORTED;
    }

    /* DriverStore UMDs come back as full paths and load their siblings from there */
    if (wcschr(FileName.UmdFileName, L'\\') != nullptr)
        LoadFlags = LOAD_WITH_ALTERED_SEARCH_PATH;
    else
        LoadFlags = LOAD_LIBRARY_SEARCH_SYSTEM32;

    Driver = LoadLibraryExW(FileName.UmdFileName, NULL, LoadFlags);
    if (Driver == NULL)
    {
        RX_TRACE(L"failed to load UMD %s (%lu)", FileName.UmdFileName, GetLastError());
        return DXGI_ERROR_UNSUPPORTED;
    }

    OpenAdapter = reinterpret_cast<PFND3D10DDI_OPENADAPTER>(
        GetProcAddress(Driver, "OpenAdapter10_2"));
    if (OpenAdapter == nullptr)
    {
        RX_TRACE(L"UMD %s has no OpenAdapter10_2", FileName.UmdFileName);
        return DXGI_ERROR_UNSUPPORTED;
    }

    RX_TRACE(L"UMD %s", FileName.UmdFileName);
    return S_OK;
}

HRESULT
UmdAdapter::OpenDriverAdapter(
    _In_ UINT Interface,
    _In_ UINT Version)
{
    D3D10DDIARG_OPENADAPTER Args = {};
    HRESULT hr;

    Functions = {};

    Args.hRTAdapter.handle = this;
    Args.Interface = Interface;
    Args.Version = Version;
    Args.pAdapterCallbacks = &Callbacks;
    Args.pAdapterFuncs_2 = &Functions;

    hr = OpenAdapter(&Args);
    if (FAILED(hr))
    {
        RX_TRACE(L"OpenAdapter10_2(0x%08X, 0x%08X) failed 0x%08lX", Interface, Version, hr);
        return hr;
    }

    DriverAdapter = Args.hAdapter;
    IsDriverAdapterOpen = TRUE;
    return S_OK;
}

VOID
UmdAdapter::CloseDriverAdapter()
{
    if (!IsDriverAdapterOpen)
        return;

    if (Functions.pfnCloseAdapter != nullptr)
        Functions.pfnCloseAdapter(DriverAdapter);

    DriverAdapter = {};
    IsDriverAdapterOpen = FALSE;
}

HRESULT
UmdAdapter::NegotiateRevision()
{
    const DdiRevision* Probe = nullptr;
    UINT64* Supported;
    UINT32 Count = 0;
    UINT32 Index;
    HRESULT hr;

    for (const DdiRevision& Candidate : RuntimeRevisions)
    {
        if (RxIsRevisionAllowed(Candidate))
        {
            Probe = &Candidate;
            break;
        }
    }

    if (Probe == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* Open once at our newest revision only to learn what the driver speaks */
    hr = OpenDriverAdapter(Probe->Interface, RxMakeRuntimeVersion(*Probe));
    if (FAILED(hr))
        return hr;

    if (Functions.pfnGetSupportedVersions == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    hr = Functions.pfnGetSupportedVersions(DriverAdapter, &Count, nullptr);
    if (FAILED(hr) || Count == 0)
        return FAILED(hr) ? hr : DXGI_ERROR_UNSUPPORTED;

    Supported = new (std::nothrow) UINT64[Count];
    if (Supported == nullptr)
        return E_OUTOFMEMORY;

    hr = Functions.pfnGetSupportedVersions(DriverAdapter, &Count, Supported);
    if (FAILED(hr))
    {
        delete[] Supported;
        return hr;
    }

    for (Index = 0; Index < Count; Index++)
    {
        RX_TRACE(L"driver speaks interface %u.%u build %u",
                 (UINT)(Supported[Index] >> 48),
                 (UINT)((Supported[Index] >> 32) & 0xFFFF),
                 (UINT)((Supported[Index] >> 16) & 0xFFFF));

        if ((UINT)(Supported[Index] >> 48) == D3D11_DDI_MAJOR_VERSION &&
            (UINT)(Supported[Index] >> 32) > NewestDriverInterface)
        {
            NewestDriverInterface = (UINT)(Supported[Index] >> 32);
        }
    }

    /* Pick our newest revision whose interface the driver lists at a build we cover */
    for (const DdiRevision& Candidate : RuntimeRevisions)
    {
        if (!RxIsRevisionAllowed(Candidate))
            continue;

        for (Index = 0; Index < Count; Index++)
        {
            if ((UINT)(Supported[Index] >> 32) == Candidate.Interface &&
                (UINT)((Supported[Index] >> 16) & 0xFFFF) <= Candidate.Build)
            {
                Revision = &Candidate;
                break;
            }
        }

        if (Revision != nullptr)
            break;
    }

    delete[] Supported;

    if (Revision == nullptr)
    {
        RX_TRACE(L"no DDI revision in common with the driver");
        return DXGI_ERROR_UNSUPPORTED;
    }

    RuntimeVersion = RxMakeRuntimeVersion(*Revision);
    RX_TRACE(L"using %s (interface 0x%08X version 0x%08X)",
             Revision->Name, Revision->Interface, RuntimeVersion);

    if (Revision == Probe)
        return S_OK;

    CloseDriverAdapter();
    return OpenDriverAdapter(Revision->Interface, RuntimeVersion);
}

HRESULT
UmdAdapter::QueryCaps()
{
    D3D11DDI_3DPIPELINESUPPORT_CAPS Pipeline = {};
    D3D10_2DDIARG_GETCAPS Caps = {};
    HRESULT hr;

    if (Functions.pfnGetCaps == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Caps.Type = D3D11DDICAPS_3DPIPELINESUPPORT;
    Caps.pData = &Pipeline;
    Caps.DataSize = sizeof(Pipeline);

    hr = Functions.pfnGetCaps(DriverAdapter, &Caps);
    if (FAILED(hr))
    {
        RX_TRACE(L"GetCaps(3DPIPELINESUPPORT) failed 0x%08lX", hr);
        return hr;
    }

    PipelineCaps = Pipeline.Caps;
    RX_TRACE(L"pipeline levels 0x%03X", PipelineCaps);
    return S_OK;
}

HRESULT
UmdAdapter::GetCaps(
    _In_ D3D10_2DDICAPS_TYPE Type,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size) const
{
    D3D10_2DDIARG_GETCAPS Caps = {};

    ZeroMemory(Data, Size);
    if (Functions.pfnGetCaps == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Caps.Type = Type;
    Caps.pData = Data;
    Caps.DataSize = Size;
    return Functions.pfnGetCaps(DriverAdapter, &Caps);
}

HRESULT
UmdAdapter::QueryKernel(
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size) const
{
    D3DKMT_QUERYADAPTERINFO Query = {};

    Query.hAdapter = KmtAdapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return RxHresultFromStatus(Kmt->QueryAdapterInfo(&Query));
}

D3DKMT_DRIVERVERSION
UmdAdapter::GetWddmVersion() const
{
    D3DKMT_DRIVERVERSION Version = (D3DKMT_DRIVERVERSION)0;

    if (FAILED(QueryKernel(KMTQAITYPE_DRIVERVERSION, &Version, sizeof(Version))))
        return (D3DKMT_DRIVERVERSION)0;

    return Version;
}

HRESULT
APIENTRY
UmdAdapter::QueryAdapterInfoCb(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ const D3DDDICB_QUERYADAPTERINFO* Query)
{
    UmdAdapter* Self = static_cast<UmdAdapter*>(RuntimeAdapter);
    D3DKMT_QUERYADAPTERINFO KmtQuery = {};

    KmtQuery.hAdapter = Self->KmtAdapter;
    KmtQuery.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    KmtQuery.pPrivateDriverData = Query->pPrivateDriverData;
    KmtQuery.PrivateDriverDataSize = Query->PrivateDriverDataSize;

    return RxHresultFromStatus(Self->Kmt->QueryAdapterInfo(&KmtQuery));
}

HRESULT
APIENTRY
UmdAdapter::QueryAdapterInfoCb2(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ const D3DDDICB_QUERYADAPTERINFO2* Query)
{
    UmdAdapter* Self = static_cast<UmdAdapter*>(RuntimeAdapter);
    D3DKMT_QUERYADAPTERINFO KmtQuery = {};

    switch (Query->QueryType)
    {
        case D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE:
            KmtQuery.Type = KMTQAITYPE_UMDRIVERPRIVATE;
            break;

        case D3DDDI_QUERYADAPTERTYPE_QUERYREGISTRY:
            KmtQuery.Type = KMTQAITYPE_QUERYREGISTRY;
            break;

        /* Drivers ask for the rest, such as per application profiles, and carry on without them */
        default:
            RX_TRACE(L"QueryAdapterInfoCb2 type %d not handled", Query->QueryType);
            return E_INVALIDARG;
    }

    KmtQuery.hAdapter = Self->KmtAdapter;
    KmtQuery.pPrivateDriverData = Query->pPrivateDriverData;
    KmtQuery.PrivateDriverDataSize = Query->PrivateDriverDataSize;

    return RxHresultFromStatus(Self->Kmt->QueryAdapterInfo(&KmtQuery));
}

HRESULT
APIENTRY
UmdAdapter::GetMultisampleMethodListCb(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ D3DDDICB_GETMULTISAMPLEMETHODLIST* Query)
{
    /* Only the D3D9 runtime routes MSAA method lists through here */
    return E_NOTIMPL;
}
