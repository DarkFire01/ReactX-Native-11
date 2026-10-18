/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d12core.dll entry points and the table the d3d12.dll loader asks for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "core.hpp"

extern const WCHAR RxTraceModule[] = L"d3d12core";

/* The SDK this core implements, read by the loader the same way an application's is */
extern "C" const UINT D3D12SDKVersion = 616;

static
HRESULT
WINAPI
RxEnableExperimentalFeatures(
    _In_ UINT NumFeatures,
    _In_count_(NumFeatures) const IID* pIIDs,
    _In_opt_count_(NumFeatures) void* pConfigurationStructs,
    _In_opt_count_(NumFeatures) UINT* pConfigurationStructSizes)
{
    RX_UNIMPLEMENTED();
    return E_NOINTERFACE;
}

extern "C"
HRESULT
WINAPI
D3D12GetInterface(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvDebug);

/* Removed device data settings: kept per process and read by nothing yet, since no breadcrumbs are recorded */
class RxDredSettings final : public ID3D12DeviceRemovedExtendedDataSettings2
{
public:
    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings) ||
            riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings1) ||
            riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings2))
        {
            *ppvObject = static_cast<ID3D12DeviceRemovedExtendedDataSettings2*>(this);
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    /* One object for the whole process, never freed */
    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return 2;
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return 1;
    }

    VOID
    STDMETHODCALLTYPE
    SetAutoBreadcrumbsEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        AutoBreadcrumbs = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetPageFaultEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        PageFault = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetWatsonDumpEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        WatsonDump = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetBreadcrumbContextEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        BreadcrumbContext = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    UseMarkersOnlyAutoBreadcrumbs(
        BOOL MarkersOnly) override
    {
        MarkersOnlyBreadcrumbs = MarkersOnly;
    }

private:
    D3D12_DRED_ENABLEMENT AutoBreadcrumbs = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    D3D12_DRED_ENABLEMENT PageFault = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    D3D12_DRED_ENABLEMENT WatsonDump = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    D3D12_DRED_ENABLEMENT BreadcrumbContext = D3D12_DRED_ENABLEMENT_SYSTEM_CONTROLLED;
    BOOL MarkersOnlyBreadcrumbs = FALSE;
};

static RxDredSettings RxProcessDredSettings;

static const RxD3D12CoreExports RxCoreExports =
{
    RxD3D12CoreExportsVersion,
    RxCreateDevice,
    RxSerializeRootSignature,
    RxCreateRootSignatureDeserializer,
    RxSerializeVersionedRootSignature,
    RxCreateVersionedRootSignatureDeserializer,
    RxEnableExperimentalFeatures,
    D3D12GetInterface,
};

extern "C"
HRESULT
WINAPI
D3D12GetInterface(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvDebug)
{
    if (rclsid == CLSID_RxD3D12CoreExports)
    {
        if (ppvDebug == nullptr)
            return E_POINTER;
        *ppvDebug = const_cast<RxD3D12CoreExports*>(&RxCoreExports);
        return S_OK;
    }

    if (ppvDebug != nullptr)
        *ppvDebug = nullptr;

    if (rclsid == CLSID_D3D12DeviceRemovedExtendedData)
    {
        if (ppvDebug == nullptr)
            return S_FALSE;
        return RxProcessDredSettings.QueryInterface(riid, ppvDebug);
    }

    /* The debug layer is a separate install on Windows too, and this runtime has none */
    if (rclsid == CLSID_D3D12Debug)
        return DXGI_ERROR_SDK_COMPONENT_MISSING;

    return E_NOINTERFACE;
}
