/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel adapter plus the Direct3D 12 user mode driver that drives it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* What the runtime says when the adapter or its driver can not be used */
constexpr HRESULT RxDriverInternalError = DXGI_ERROR_DRIVER_INTERNAL_ERROR;

/* The answer for adapter queries about data the runtime does not hold */
constexpr HRESULT RxNotAvailable = MAKE_HRESULT(SEVERITY_ERROR, 0x876, 2154);

RxAdapter12::~RxAdapter12()
{
    D3DKMT_CLOSEADAPTER Close;

    if (Functions.pfnCloseAdapter != nullptr && DriverAdapter.pDrvPrivate != nullptr)
        Functions.pfnCloseAdapter(DriverAdapter);

    /* The UMD stays mapped: drivers start worker threads that outlive the adapter */
    Driver = NULL;

    if (KmtAdapter != 0)
    {
        Close.hAdapter = KmtAdapter;
        Kmt->CloseAdapter(&Close);
    }
}

HRESULT
RxAdapter12::Open(
    _In_ const LUID& AdapterLuid)
{
    D3DKMT_OPENADAPTERFROMLUID FromLuid = {};
    D3DKMT_UMDFILENAMEINFO FileName = {};
    D3D12DDIARG_OPENADAPTER OpenArgs = {};
    D3D12DDI_3DPIPELINESUPPORT1_DATA_0081 Pipeline = {};
    D3DKMT_ADAPTERTYPE Type = {};
    D3D12DDI_3DPIPELINELEVEL Level;
    PFND3D12DDI_OPENADAPTER OpenAdapter;
    NTSTATUS Status;
    HRESULT hr;

    Kmt = RxGetKmtThunks();
    if (Kmt == nullptr || Kmt->OpenAdapterFromLuid == nullptr || Kmt->CreatePagingQueue == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    FromLuid.AdapterLuid = AdapterLuid;
    Status = Kmt->OpenAdapterFromLuid(&FromLuid);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTOpenAdapterFromLuid failed 0x%08lX", Status);
        return DXGI_ERROR_NOT_FOUND;
    }
    KmtAdapter = FromLuid.hAdapter;
    Luid = AdapterLuid;

    FileName.Version = KMTUMDVERSION_DX12;
    hr = QueryKernel(KMTQAITYPE_UMDRIVERNAME, &FileName, sizeof(FileName));
    if (FAILED(hr) || FileName.UmdFileName[0] == UNICODE_NULL)
    {
        RX_TRACE(L"adapter has no Direct3D 12 driver (0x%08lX)", hr);
        return DXGI_ERROR_UNSUPPORTED;
    }

    Driver = LoadLibraryExW(FileName.UmdFileName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Driver == NULL)
    {
        RX_TRACE(L"%s did not load (%lu)", FileName.UmdFileName, GetLastError());
        return DXGI_ERROR_UNSUPPORTED;
    }

    OpenAdapter = reinterpret_cast<PFND3D12DDI_OPENADAPTER>(GetProcAddress(Driver, "OpenAdapter12"));
    if (OpenAdapter == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* Only a software adapter is asked for the WARP export */
    if (SUCCEEDED(QueryKernel(KMTQAITYPE_ADAPTERTYPE, &Type, sizeof(Type))) && Type.SoftwareDevice)
        WarpExtension = GetProcAddress(Driver, MAKEINTRESOURCEA(199)) != nullptr;

    Callbacks.pfnQueryAdapterInfoCb = QueryAdapterInfoCb;
    Callbacks.pfnGetMultisampleMethodListCb = GetMultisampleMethodListCb;
    Callbacks.pfnQueryAdapterInfoCb2 = QueryAdapterInfoCb2;

    OpenArgs.hRTAdapter.handle = this;
    OpenArgs.pAdapterCallbacks = &Callbacks;
    OpenArgs.pAdapterFuncs = reinterpret_cast<D3D12DDI_ADAPTERFUNCS*>(&Functions);
    static_assert(sizeof(D3D12DDI_ADAPTERFUNCS) == sizeof(D3D12DDI_ADAPTERFUNCS_0109));

    hr = OpenAdapter(&OpenArgs);
    if (FAILED(hr))
    {
        RX_TRACE(L"OpenAdapter12 failed 0x%08lX", hr);
        return hr;
    }
    DriverAdapter = OpenArgs.hAdapter;

    if (Functions.pfnCloseAdapter == nullptr || Functions.pfnGetSupportedVersions == nullptr ||
        Functions.pfnGetCaps == nullptr || Functions.pfnGetOptionalDDITables == nullptr ||
        Functions.pfnFillDDITable == nullptr || Functions.pfnDestroyDevice == nullptr ||
        Functions.pfnCalcPrivateDeviceSize == nullptr || Functions.pfnCreateDevice == nullptr)
    {
        RX_TRACE(L"driver adapter table is incomplete");
        return RxDriverInternalError;
    }

    /* The highest level the driver offers, asked as a runtime that knows 12_2 */
    Pipeline.HighestRuntimeSupportedFeatureLevel = D3D12DDI_3DPIPELINELEVEL_12_2;
    if (SUCCEEDED(GetCaps(D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1, nullptr, &Pipeline, sizeof(Pipeline))))
        Level = Pipeline.MaximumDriverSupportedFeatureLevel;
    else if (FAILED(GetCaps(D3D12DDICAPS_TYPE_3DPIPELINESUPPORT, nullptr, &Level, sizeof(Level))))
        Level = (D3D12DDI_3DPIPELINELEVEL)0;

    switch (Level)
    {
        case D3D12DDI_3DPIPELINELEVEL_1_0_GENERIC:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_1_0_GENERIC;
            break;
        case D3D12DDI_3DPIPELINELEVEL_1_0_CORE:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_1_0_CORE;
            break;
        case D3D12DDI_3DPIPELINELEVEL_11_0:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_11_0;
            break;
        case D3D12DDI_3DPIPELINELEVEL_11_1:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_11_1;
            break;
        case D3D12DDI_3DPIPELINELEVEL_12_0:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_12_0;
            break;
        case D3D12DDI_3DPIPELINELEVEL_12_1:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_12_1;
            break;
        case D3D12DDI_3DPIPELINELEVEL_12_2:
            MaxFeatureLevel = D3D_FEATURE_LEVEL_12_2;
            break;
        default:
            RX_TRACE(L"driver reports no usable pipeline level (%d)", Level);
            break;
    }

    return NegotiateVersion();
}

/* The driver lists every revision it speaks; this runtime speaks exactly one */
HRESULT
RxAdapter12::NegotiateVersion()
{
    UINT64* Versions;
    UINT32 Count = 0;
    UINT32 Index;
    HRESULT hr;

    hr = Functions.pfnGetSupportedVersions(DriverAdapter, &Count, nullptr);
    if (FAILED(hr) || Count == 0)
        return RxDriverInternalError;

    Versions = new (std::nothrow) UINT64[Count];
    if (Versions == nullptr)
        return E_OUTOFMEMORY;

    hr = Functions.pfnGetSupportedVersions(DriverAdapter, &Count, Versions);
    for (Index = 0; SUCCEEDED(hr) && Index < Count; Index++)
    {
        if ((UINT)(Versions[Index] >> 32) == RxDdiInterface12 &&
            (UINT)((Versions[Index] >> 16) & 0xFFFF) == RxDdiBuild12)
        {
            Version = Versions[Index];
            break;
        }
    }
    delete[] Versions;

    if (Version == 0)
    {
        RX_TRACE(L"driver does not speak DDI build %u", RxDdiBuild12);
        return RxDriverInternalError;
    }

    return S_OK;
}

HRESULT
RxAdapter12::GetCaps(
    _In_ D3D12DDICAPS_TYPE Type,
    _In_opt_ VOID* Info,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size) const
{
    D3D12DDIARG_GETCAPS Args = {};

    Args.Type = Type;
    Args.pInfo = Info;
    Args.pData = Data;
    Args.DataSize = Size;
    return Functions.pfnGetCaps(DriverAdapter, &Args);
}

HRESULT
RxAdapter12::QueryKernel(
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

HRESULT
APIENTRY
RxAdapter12::QueryAdapterInfoCb(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ const D3DDDICB_QUERYADAPTERINFO* Query)
{
    RxAdapter12* Self = static_cast<RxAdapter12*>(RuntimeAdapter);

    return Self->QueryKernel(KMTQAITYPE_UMDRIVERPRIVATE, Query->pPrivateDriverData, Query->PrivateDriverDataSize);
}

HRESULT
APIENTRY
RxAdapter12::QueryAdapterInfoCb2(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ const D3DDDICB_QUERYADAPTERINFO2* Query)
{
    RxAdapter12* Self = static_cast<RxAdapter12*>(RuntimeAdapter);

    switch (Query->QueryType)
    {
        case D3DDDI_QUERYADAPTERTYPE_DRIVERPRIVATE:
            return Self->QueryKernel(KMTQAITYPE_UMDRIVERPRIVATE, Query->pPrivateDriverData,
                                     Query->PrivateDriverDataSize);
        case D3DDDI_QUERYADAPTERTYPE_QUERYREGISTRY:
            return Self->QueryKernel(KMTQAITYPE_QUERYREGISTRY, Query->pPrivateDriverData,
                                     Query->PrivateDriverDataSize);

        /* Only tools hand a driver state blob to the runtime, so there is never one to give */
        case D3DDDI_QUERYADAPTERTYPE_APPLICATIONSPECIFICDRIVERBLOB:
            return RxNotAvailable;

        /* The description comes from a shader cache application registration, which this runtime never has */
        case D3DDDI_QUERYADAPTERTYPE_APPLICATIONDESC:
            if (Query->PrivateDriverDataSize < sizeof(D3D12DDI_APPLICATION_DESC))
                return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            if (Query->pPrivateDriverData == nullptr)
                return E_INVALIDARG;
            return RxNotAvailable;

        default:
            RX_TRACE(L"QueryAdapterInfoCb2 type %d not handled", Query->QueryType);
            return E_INVALIDARG;
    }
}

HRESULT
APIENTRY
RxAdapter12::GetMultisampleMethodListCb(
    _In_ HANDLE RuntimeAdapter,
    _Inout_ D3DDDICB_GETMULTISAMPLEMETHODLIST* Query)
{
    return E_NOTIMPL;
}
