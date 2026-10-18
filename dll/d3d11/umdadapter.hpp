/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Kernel adapter plus the vendor UMD that drives it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* One DDI revision this runtime can drive, in the order umdadapter.cpp tries them */
struct DdiRevision
{
    UINT Interface;
    UINT Build;
    UINT MinimumWinnt;
    PCWSTR Name;
};

class UmdAdapter
{
public:
    UmdAdapter() = default;
    ~UmdAdapter();

    UmdAdapter(const UmdAdapter&) = delete;
    UmdAdapter& operator=(const UmdAdapter&) = delete;

    HRESULT
    Open(
        _In_opt_ const LUID* AdapterLuid);

    BOOLEAN
    SupportsPipelineLevel(
        _In_ D3D11DDI_3DPIPELINELEVEL Level) const;

    const DdiRevision*
    GetRevision() const
    {
        return Revision;
    }

    UINT
    GetRuntimeVersion() const
    {
        return RuntimeVersion;
    }

    const LUID&
    GetLuid() const
    {
        return Luid;
    }

    D3DKMT_HANDLE
    GetKmtHandle() const
    {
        return KmtAdapter;
    }

    D3D10DDI_HADAPTER
    GetDriverHandle() const
    {
        return DriverAdapter;
    }

    const D3D10_2DDI_ADAPTERFUNCS&
    GetFunctions() const
    {
        return Functions;
    }

    /* One pfnGetCaps answer; fails when the driver has no answer for that type */
    HRESULT
    GetCaps(
        _In_ D3D10_2DDICAPS_TYPE Type,
        _Out_writes_bytes_(Size) VOID* Data,
        _In_ UINT Size) const;

    /* One D3DKMTQueryAdapterInfo answer from the kernel driver */
    HRESULT
    QueryKernel(
        _In_ KMTQUERYADAPTERINFOTYPE Type,
        _Inout_updates_bytes_(Size) VOID* Data,
        _In_ UINT Size) const;

    /* The WDDM version the kernel driver reports, 0 when it can not be read */
    D3DKMT_DRIVERVERSION
    GetWddmVersion() const;

private:
    HRESULT
    OpenKernelAdapter(
        _In_opt_ const LUID* AdapterLuid);

    HRESULT
    LoadDriver();

    HRESULT
    OpenDriverAdapter(
        _In_ UINT Interface,
        _In_ UINT Version);

    VOID
    CloseDriverAdapter();

    HRESULT
    NegotiateRevision();

    HRESULT
    QueryCaps();

    static
    HRESULT
    APIENTRY
    QueryAdapterInfoCb(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ const D3DDDICB_QUERYADAPTERINFO* Query);

    static
    HRESULT
    APIENTRY
    QueryAdapterInfoCb2(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ const D3DDDICB_QUERYADAPTERINFO2* Query);

    static
    HRESULT
    APIENTRY
    GetMultisampleMethodListCb(
        _In_ HANDLE RuntimeAdapter,
        _Inout_ D3DDDICB_GETMULTISAMPLEMETHODLIST* Query);

    const KmtThunks* Kmt = nullptr;
    D3DKMT_HANDLE KmtAdapter = 0;
    LUID Luid = {};
    HMODULE Driver = NULL;
    PFND3D10DDI_OPENADAPTER OpenAdapter = nullptr;
    D3DDDI_ADAPTERCALLBACKS Callbacks = {};
    D3D10_2DDI_ADAPTERFUNCS Functions = {};
    D3D10DDI_HADAPTER DriverAdapter = {};
    BOOLEAN IsDriverAdapterOpen = FALSE;
    const DdiRevision* Revision = nullptr;
    UINT RuntimeVersion = 0;
    UINT PipelineCaps = 0;
};
