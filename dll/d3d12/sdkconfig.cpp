/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12SDKConfiguration1, answered by the loader itself
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"

#include <d3d12.h>

#include "d3d12bridge.hpp"
#include "loader.hpp"

using PFN_RX_IS_DEVELOPER_MODE_ENABLED = HRESULT (WINAPI*)(BOOL*);

/* Developer mode as the app model unlock API reports it; FALSE where that API is missing */
static
BOOLEAN
RxIsDeveloperMode(VOID)
{
    static PFN_RX_IS_DEVELOPER_MODE_ENABLED Query = []()
    {
        HMODULE Module = LoadLibraryExW(L"api-ms-win-appmodel-unlock-l1-1-0.dll", NULL,
                                        LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (Module == NULL)
            return (PFN_RX_IS_DEVELOPER_MODE_ENABLED)nullptr;
        return reinterpret_cast<PFN_RX_IS_DEVELOPER_MODE_ENABLED>(GetProcAddress(Module, "IsDeveloperModeEnabled"));
    }();
    BOOL Enabled = FALSE;

    if (Query == nullptr || FAILED(Query(&Enabled)))
        return FALSE;
    return Enabled != FALSE;
}

/* A new object for every request, as Windows hands out */
class RxSdkConfiguration final : public ID3D12SDKConfiguration1
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

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12SDKConfiguration) ||
            riid == __uuidof(ID3D12SDKConfiguration1))
        {
            *ppvObject = static_cast<ID3D12SDKConfiguration1*>(this);
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

    /* Choosing another core needs developer mode; there is only the ReactX core to choose */
    HRESULT
    STDMETHODCALLTYPE
    SetSDKVersion(
        _In_ UINT SDKVersion,
        _In_z_ LPCSTR SDKPath) override
    {
        if (!RxIsDeveloperMode())
        {
            RX_TRACE(L"SetSDKVersion needs developer mode");
            return DXGI_ERROR_INVALID_CALL;
        }

        RX_TRACE(L"SetSDKVersion %u kept on the ReactX core", SDKVersion);
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    CreateDeviceFactory(
        _In_ UINT SDKVersion,
        _In_ LPCSTR SDKPath,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvFactory) override
    {
        const RxD3D12CoreExports* Core = RxGetCore();

        if (Core == nullptr)
            return D3D12_ERROR_INVALID_REDIST;

        return Core->GetInterface(CLSID_D3D12DeviceFactory, riid, ppvFactory);
    }

    VOID
    STDMETHODCALLTYPE
    FreeUnusedSDKs(VOID) override
    {
    }

private:
    ~RxSdkConfiguration() = default;

    LONG RefCount = 1;
};

HRESULT
RxGetSdkConfiguration(
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject)
{
    RxSdkConfiguration* Created = new (std::nothrow) RxSdkConfiguration();
    HRESULT hr = S_FALSE;

    if (Created == nullptr)
        return E_OUTOFMEMORY;

    if (ppvObject != nullptr)
        hr = Created->QueryInterface(riid, ppvObject);
    Created->Release();
    return hr;
}
