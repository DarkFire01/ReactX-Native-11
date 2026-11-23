/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Opening and closing an adapter on top of a Direct3D 12 device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

typedef HRESULT (WINAPI* PFN_NINE_CREATE_DXCORE_FACTORY)(REFIID, void**);

/* DXCore is optional: without it the adapter is found through DXGI */
static
HRESULT
NineCreateDxCoreFactory(
    _COM_Outptr_ IDXCoreAdapterFactory** Factory)
{
    static const PFN_NINE_CREATE_DXCORE_FACTORY Create = []()
    {
        HMODULE Module = LoadLibraryExW(L"dxcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (Module == NULL)
            return static_cast<PFN_NINE_CREATE_DXCORE_FACTORY>(nullptr);
        return reinterpret_cast<PFN_NINE_CREATE_DXCORE_FACTORY>(GetProcAddress(Module, "DXCoreCreateAdapterFactory"));
    }();

    *Factory = nullptr;
    if (Create == nullptr)
        return E_NOINTERFACE;
    return Create(IID_PPV_ARGS(Factory));
}

/* Finds the adapter through DXCore and reads its hardware IDs and driver version */
static
HRESULT
NineFindDxCoreAdapter(
    _Inout_ NineAdapter* Adapter,
    _In_ const LUID* AdapterLuid,
    _Outptr_result_maybenull_ IUnknown** Found)
{
    IDXCoreAdapterFactory* Factory;
    IDXCoreAdapter* Core = nullptr;
    HRESULT hr;

    *Found = nullptr;
    if (SUCCEEDED(NineCreateDxCoreFactory(&Factory)))
    {
        Factory->GetAdapterByLuid(*AdapterLuid, IID_PPV_ARGS(Found));
        Factory->Release();
    }
    if (*Found == nullptr)
        return S_OK;

    (*Found)->QueryInterface(IID_PPV_ARGS(&Core));
    hr = Core->GetProperty(DXCoreAdapterProperty::HardwareID, sizeof(Adapter->HardwareId), &Adapter->HardwareId);
    if (SUCCEEDED(hr))
    {
        hr = Core->GetProperty(DXCoreAdapterProperty::DriverVersion, sizeof(Adapter->DriverVersion),
                               &Adapter->DriverVersion);
    }
    Core->Release();
    return hr;
}

/* The DXGI way to the same facts, for systems without DXCore */
static
HRESULT
NineFindDxgiAdapter(
    _Inout_ NineAdapter* Adapter,
    _In_ const LUID* AdapterLuid,
    _Outptr_result_maybenull_ IUnknown** Found)
{
    IDXGIFactory4* Factory = nullptr;
    IDXGIAdapter* Dxgi = nullptr;
    DXGI_ADAPTER_DESC Desc = {};
    HRESULT hr;

    *Found = nullptr;
    hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory));
    if (FAILED(hr))
        return hr;

    hr = Factory->EnumAdapterByLuid(*AdapterLuid, IID_PPV_ARGS(Found));
    Factory->Release();
    if (FAILED(hr))
        return hr;

    if (*Found != nullptr)
        (*Found)->QueryInterface(IID_PPV_ARGS(&Dxgi));

    hr = Dxgi->GetDesc(&Desc);
    if (SUCCEEDED(hr))
    {
        Adapter->HardwareId.vendorID = Desc.VendorId;
        Adapter->HardwareId.deviceID = Desc.DeviceId;
        Adapter->HardwareId.subSysID = Desc.SubSysId;
        Adapter->HardwareId.revision = Desc.Revision;
        Adapter->DriverVersion.QuadPart = 0;
        hr = Dxgi->CheckInterfaceSupport(__uuidof(IDXGIDevice), &Adapter->DriverVersion);
    }
    Dxgi->Release();
    return hr;
}

/* Takes the application's device, or makes one on the adapter */
static
HRESULT
NineBindDevice(
    _Inout_ NineAdapter* Adapter,
    _In_ const LUID* AdapterLuid,
    _In_ const NineOpenArgs* Open,
    _In_opt_ IUnknown* Found)
{
    LUID DeviceLuid;
    HRESULT hr;

    if (Open->Device == nullptr)
        return D3D12CreateDevice(Found, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Adapter->Device));

    hr = Open->Device->QueryInterface(IID_PPV_ARGS(&Adapter->Device));
    if (FAILED(hr))
        return hr;

    DeviceLuid = Adapter->Device->GetAdapterLuid();
    if (memcmp(&DeviceLuid, AdapterLuid, sizeof(DeviceLuid)) != 0)
    {
        NineTrace(L"the application's Direct3D 12 device is on another adapter");
        return E_FAIL;
    }
    return S_OK;
}

/* Takes the application's first queue, which must be a direct queue, or makes one */
static
HRESULT
NineBindQueue(
    _Inout_ NineAdapter* Adapter,
    _In_ const NineOpenArgs* Open)
{
    D3D12_COMMAND_QUEUE_DESC Desc = {};
    HRESULT hr;

    if (Open->QueueCount == 0)
        return Adapter->Device->CreateCommandQueue(&Desc, IID_PPV_ARGS(&Adapter->Queue));

    hr = Open->Queues[0]->QueryInterface(IID_PPV_ARGS(&Adapter->Queue));
    if (FAILED(hr))
        return hr;

    if (Adapter->Queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        NineTrace(L"the application's queue is not a direct queue");
        return E_FAIL;
    }
    return S_OK;
}

static
VOID
NineFreeAdapter(
    _In_ _Post_invalid_ NineAdapter* Adapter)
{
    if (Adapter->Queue != nullptr)
        Adapter->Queue->Release();
    if (Adapter->Device != nullptr)
        Adapter->Device->Release();
    delete Adapter;
}

static
HRESULT
NineBuildAdapter(
    _Inout_ NineAdapter* Adapter,
    _In_ const D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _In_ const NineOpenArgs* Open)
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS3 Options = {};
    IUnknown* Found = nullptr;
    HRESULT FeatureResult;
    HRESULT hr;

    Adapter->Callbacks = Args->pAdapterCallbacks;
    if (Open->Version >= 2)
    {
        Adapter->Extras = *Open->Extras;
        Adapter->HasExtra[0] = Adapter->Extras.Values[0] != nullptr;
        Adapter->HasExtra[1] = Adapter->Extras.Values[1] != nullptr;
    }

    if (AdapterLuid == nullptr)
        return E_FAIL;

    hr = NineFindDxCoreAdapter(Adapter, AdapterLuid, &Found);
    if (SUCCEEDED(hr) && Found == nullptr)
        hr = NineFindDxgiAdapter(Adapter, AdapterLuid, &Found);
    if (FAILED(hr))
    {
        NineTrace(L"the adapter of the runtime is not found (0x%08lX)", hr);
        if (Found != nullptr)
            Found->Release();
        return hr;
    }

    hr = NineBindDevice(Adapter, AdapterLuid, Open, Found);
    if (FAILED(hr))
    {
        NineTrace(L"no Direct3D 12 device (0x%08lX)", hr);
        if (Found != nullptr)
            Found->Release();
        return hr;
    }

    FeatureResult = Adapter->Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &Options, sizeof(Options));
    if (FAILED(FeatureResult))
    {
        if (Found != nullptr)
            Found->Release();
        return FeatureResult;
    }
    Adapter->TypedCasting = Options.CastingFullyTypedFormatSupported != FALSE;

    hr = NineBindQueue(Adapter, Open);
    if (Found != nullptr)
        Found->Release();
    if (FAILED(hr))
        NineTrace(L"no Direct3D 12 queue (0x%08lX)", hr);
    return hr;
}

static
HRESULT
APIENTRY
NineCloseAdapter(
    _In_ HANDLE hAdapter)
{
    if (hAdapter == NULL)
        return E_INVALIDARG;

    NineFreeAdapter(static_cast<NineAdapter*>(hAdapter));
    return NineDxgiResult(S_OK);
}

/* Builds the adapter and hands its table back; the second form's version is raised to two */
static
HRESULT
NineOpen(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ NineOpenArgs* Open)
{
    NineAdapter* Adapter;
    HRESULT hr;

    Adapter = new (std::nothrow) NineAdapter();
    if (Adapter == nullptr)
        return E_OUTOFMEMORY;

    hr = NineBuildAdapter(Adapter, Args, AdapterLuid, Open);
    if (FAILED(hr))
    {
        NineFreeAdapter(Adapter);
        return NineDxgiResult(hr);
    }

    Args->hAdapter = Adapter;
    Args->pAdapterFuncs->pfnGetCaps = NineGetCaps;
    Args->pAdapterFuncs->pfnCreateDevice = NineCreateDevice;
    Args->pAdapterFuncs->pfnCloseAdapter = NineCloseAdapter;
    Args->DriverVersion = Args->Version < NineNewestRuntimeVersion ? Args->Version : NineNewestRuntimeVersion;
    if (Open->Version < 2)
        Open->Version = 2;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineOpenAdapter(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ VOID* OpenArgs)
{
    const NineOpenArgsV1* First = static_cast<const NineOpenArgsV1*>(OpenArgs);
    NineOpenArgs Open = {};

    if (Args == nullptr)
        return E_INVALIDARG;

    Open.Device = First->Device;
    Open.Queues = First->Queues;
    Open.QueueCount = First->QueueCount;
    Open.NodeMask = First->NodeMask;
    Open.Version = 1;
    return NineOpen(Args, AdapterLuid, &Open);
}

HRESULT
APIENTRY
NineOpenAdapter2(
    _Inout_ D3DDDIARG_OPENADAPTER* Args,
    _In_opt_ const LUID* AdapterLuid,
    _Inout_ VOID* OpenArgs)
{
    if (Args == nullptr)
        return E_INVALIDARG;

    return NineOpen(Args, AdapterLuid, static_cast<NineOpenArgs*>(OpenArgs));
}

