/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3D12CreateDevice
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "core.hpp"

#include <dxcore_interface.h>

static
BOOLEAN
RxIsCreatableLevel(
    _In_ D3D_FEATURE_LEVEL Level)
{
    switch (Level)
    {
        case D3D_FEATURE_LEVEL_1_0_GENERIC:
        case D3D_FEATURE_LEVEL_1_0_CORE:
        case D3D_FEATURE_LEVEL_11_0:
        case D3D_FEATURE_LEVEL_11_1:
        case D3D_FEATURE_LEVEL_12_0:
        case D3D_FEATURE_LEVEL_12_1:
        case D3D_FEATURE_LEVEL_12_2:
            return TRUE;
        default:
            return FALSE;
    }
}

/* The first adapter of the dxgi.dll beside this module, which is the one Windows picks too */
static
HRESULT
RxDefaultAdapterLuid(
    _Out_ LUID* Luid)
{
    WCHAR Path[MAX_PATH];
    HMODULE Self = NULL;
    HMODULE Dxgi;
    PWSTR Slash;
    decltype(&CreateDXGIFactory1) CreateFactory;
    IDXGIFactory1* Factory;
    IDXGIAdapter1* Adapter;
    DXGI_ADAPTER_DESC1 Desc;
    HRESULT hr;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&RxDefaultAdapterLuid), &Self) ||
        GetModuleFileNameW(Self, Path, _countof(Path)) == 0 || (Slash = wcsrchr(Path, L'\\')) == nullptr ||
        wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"dxgi.dll") != 0)
    {
        return E_FAIL;
    }

    Dxgi = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (Dxgi == NULL)
        Dxgi = LoadLibraryExW(L"dxgi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Dxgi == NULL)
        return DXGI_ERROR_UNSUPPORTED;

    CreateFactory = reinterpret_cast<decltype(&CreateDXGIFactory1)>(GetProcAddress(Dxgi, "CreateDXGIFactory1"));
    if (CreateFactory == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    hr = CreateFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory));
    if (FAILED(hr))
        return hr;

    hr = Factory->EnumAdapters1(0, &Adapter);
    Factory->Release();
    if (FAILED(hr))
        return hr;

    hr = Adapter->GetDesc1(&Desc);
    Adapter->Release();
    if (FAILED(hr))
        return hr;

    *Luid = Desc.AdapterLuid;
    return S_OK;
}

/* An adapter from DXGI or from DXCore */
static
HRESULT
RxAdapterLuid(
    _In_ IUnknown* Unknown,
    _Out_ LUID* Luid)
{
    IDXGIAdapter* DxgiAdapter;
    IDXCoreAdapter* CoreAdapter;
    DXGI_ADAPTER_DESC Desc;
    HRESULT hr;

    if (SUCCEEDED(Unknown->QueryInterface(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&DxgiAdapter))))
    {
        hr = DxgiAdapter->GetDesc(&Desc);
        DxgiAdapter->Release();
        if (SUCCEEDED(hr))
            *Luid = Desc.AdapterLuid;
        return hr;
    }

    if (SUCCEEDED(Unknown->QueryInterface(__uuidof(IDXCoreAdapter), reinterpret_cast<void**>(&CoreAdapter))))
    {
        hr = CoreAdapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(*Luid), Luid);
        CoreAdapter->Release();
        return hr;
    }

    return E_INVALIDARG;
}

HRESULT
WINAPI
RxCreateSharedDevice(
    _In_opt_ IUnknown* pAdapter,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ UINT Sharing,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppDevice)
{
    RxDevice12* Device;
    LUID Luid;
    HRESULT hr;

    if (ppDevice != nullptr)
        *ppDevice = nullptr;

    if (!RxIsCreatableLevel(MinimumFeatureLevel))
        return E_INVALIDARG;

    hr = pAdapter != nullptr ? RxAdapterLuid(pAdapter, &Luid) : RxDefaultAdapterLuid(&Luid);
    if (FAILED(hr))
    {
        RX_TRACE(L"no adapter to make a device on (0x%08lX)", hr);
        return hr;
    }

    /* Without an output only the question of whether a device could be made is answered */
    if (ppDevice == nullptr)
    {
        hr = RxDevice12::CheckLevel(Luid, MinimumFeatureLevel);
        return SUCCEEDED(hr) ? S_FALSE : hr;
    }

    hr = RxDevice12::Get(Luid, MinimumFeatureLevel, Sharing, &Device);
    if (FAILED(hr))
        return hr;

    hr = Device->QueryInterface(riid, ppDevice);
    Device->Release();
    return hr;
}

HRESULT
WINAPI
RxCreateDevice(
    _In_opt_ IUnknown* pAdapter,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppDevice)
{
    return RxCreateSharedDevice(pAdapter, MinimumFeatureLevel, RxDeviceReuseSingleton | RxDeviceStoreSingleton, riid,
                                ppDevice);
}
