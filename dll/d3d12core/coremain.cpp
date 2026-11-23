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
extern "C" const UINT D3D12SDKVersion = 617;

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

RxDredState RxProcessDred;

/* Each request gets its own settings object; all of them write the state they were made for */
class RxDredSettings final : public ID3D12DeviceRemovedExtendedDataSettings2
{
public:
    explicit
    RxDredSettings(
        _In_ RxDredState* State)
        : Target(State)
    {
    }

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
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            delete this;
        return Count;
    }

    VOID
    STDMETHODCALLTYPE
    SetAutoBreadcrumbsEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        Target->AutoBreadcrumbs = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetPageFaultEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        Target->PageFault = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetWatsonDumpEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        Target->WatsonDump = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    SetBreadcrumbContextEnablement(
        D3D12_DRED_ENABLEMENT Enablement) override
    {
        Target->BreadcrumbContext = Enablement;
    }

    VOID
    STDMETHODCALLTYPE
    UseMarkersOnlyAutoBreadcrumbs(
        BOOL MarkersOnly) override
    {
        Target->MarkersOnlyBreadcrumbs = MarkersOnly;
    }

private:
    ~RxDredSettings() = default;

    LONG RefCount = 1;
    RxDredState* Target;
};

HRESULT
RxMakeDredSettings(
    _In_ RxDredState* Target,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject)
{
    RxDredSettings* Settings;
    HRESULT hr;

    if (ppvObject == nullptr)
        return S_FALSE;

    Settings = new (std::nothrow) RxDredSettings(Target);
    if (Settings == nullptr)
        return E_OUTOFMEMORY;
    hr = Settings->QueryInterface(riid, ppvObject);
    Settings->Release();
    return hr;
}

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

/* The driver verifier class, which has no public header */
static constexpr CLSID RxDriverVerifierClsid = {0xb7b44efa, 0x03ed, 0x4af2, {0xab, 0xc8, 0x51, 0x30, 0x04, 0x51, 0x9d, 0xcb}};
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

    if (rclsid == CLSID_D3D12DeviceRemovedExtendedData)
        return RxMakeDredSettings(&RxProcessDred, riid, ppvDebug);

    if (rclsid == CLSID_D3D12DeviceFactory)
        return RxMakeDeviceFactory(riid, ppvDebug);

    /* The debug layer and driver verifier are separate installs on Windows too; the output is left alone */
    if (rclsid == CLSID_D3D12Debug || rclsid == RxDriverVerifierClsid)
        return DXGI_ERROR_SDK_COMPONENT_MISSING;

    return E_NOINTERFACE;
}
