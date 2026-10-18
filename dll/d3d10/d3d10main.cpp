/*
 * PROJECT:     ReactX Direct3D 10 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device creation for d3d10.dll and d3d10_1.dll, on top of the ReactX d3d11.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d10runtime.hpp"

#ifdef RX_D3D10_1
extern const WCHAR RxTraceModule[] = L"d3d10_1";
#else
extern const WCHAR RxTraceModule[] = L"d3d10";
#endif

/* Asks the ReactX d3d11.dll for a device that also answers as Direct3D 10 */
constexpr UINT RxD3D10DeviceFlag = 0x40000000;

using PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN = decltype(&D3D11CreateDeviceAndSwapChain);

/* The d3d11.dll beside this module, so the whole runtime comes from one place */
_Ret_maybenull_
static
PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN
RxGetD3D11Create(VOID)
{
    static const PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN Create = []()
    {
        WCHAR Path[MAX_PATH];
        HMODULE Self = NULL;
        HMODULE D3d11;
        PWSTR Slash;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&RxGetD3D11Create), &Self) ||
            GetModuleFileNameW(Self, Path, _countof(Path)) == 0 || (Slash = wcsrchr(Path, L'\\')) == nullptr ||
            wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"d3d11.dll") != 0)
        {
            return (PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN)nullptr;
        }

        D3d11 = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (D3d11 == NULL)
            return (PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN)nullptr;

        return reinterpret_cast<PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN>(GetProcAddress(D3d11, "D3D11CreateDeviceAndSwapChain"));
    }();

    return Create;
}

/* Direct3D 10 driver types are the Direct3D 11 ones shifted by one, except WARP */
static
BOOLEAN
RxToD3D11DriverType(
    _In_ D3D10_DRIVER_TYPE DriverType,
    _Out_ D3D_DRIVER_TYPE* Result)
{
    switch (DriverType)
    {
        case D3D10_DRIVER_TYPE_HARDWARE:
            *Result = D3D_DRIVER_TYPE_HARDWARE;
            return TRUE;
        case D3D10_DRIVER_TYPE_REFERENCE:
            *Result = D3D_DRIVER_TYPE_REFERENCE;
            return TRUE;
        case D3D10_DRIVER_TYPE_NULL:
            *Result = D3D_DRIVER_TYPE_NULL;
            return TRUE;
        case D3D10_DRIVER_TYPE_SOFTWARE:
            *Result = D3D_DRIVER_TYPE_SOFTWARE;
            return TRUE;
        case D3D10_DRIVER_TYPE_WARP:
            *Result = D3D_DRIVER_TYPE_WARP;
            return TRUE;
        default:
            return FALSE;
    }
}

/* Creation flags in Direct3D 11 terms; most share their value */
static
UINT
RxToD3D11Flags(
    _In_ UINT Flags)
{
    UINT Result = Flags & (D3D10_CREATE_DEVICE_SINGLETHREADED | D3D10_CREATE_DEVICE_DEBUG |
                           D3D10_CREATE_DEVICE_SWITCH_TO_REF |
                           D3D10_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
                           D3D10_CREATE_DEVICE_BGRA_SUPPORT |
                           D3D10_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY);

    if (Flags & D3D10_CREATE_DEVICE_DEBUGGABLE)
        Result |= D3D11_CREATE_DEVICE_DEBUGGABLE;

    /* The top bit passes through so that Direct3D 11 refuses it as it would */
    return Result | RxD3D10DeviceFlag | (Flags & 0x80000000);
}

/* One path for every creation export: validate, create through Direct3D 11, face the result as Direct3D 10 */
static
HRESULT
RxCreateD3D10Device(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D10_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_ D3D_FEATURE_LEVEL FeatureLevel,
    _In_opt_ DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    _Out_opt_ IDXGISwapChain** ppSwapChain,
    _In_ REFIID DeviceInterface,
    _Out_ void** ppDevice)
{
    PFN_RX_CREATE_DEVICE_AND_SWAP_CHAIN Create = RxGetD3D11Create();
    ID3D11Device* Device11 = nullptr;
    ID3D10Multithread* Multithread;
    ID3D10Device* Device10;
    D3D_DRIVER_TYPE Type;
    HRESULT hr;

    if (ppSwapChain != nullptr)
        *ppSwapChain = nullptr;
    if (ppDevice == nullptr)
        return E_INVALIDARG;
    *ppDevice = nullptr;

    /* An explicit adapter only goes with the hardware driver type, and becomes UNKNOWN */
    if (!RxToD3D11DriverType(DriverType, &Type) || (pAdapter != nullptr && DriverType != D3D10_DRIVER_TYPE_HARDWARE))
        return E_INVALIDARG;
    if (pAdapter != nullptr)
        Type = D3D_DRIVER_TYPE_UNKNOWN;

    if (Create == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    hr = Create(pAdapter, Type, Software, RxToD3D11Flags(Flags), &FeatureLevel, 1, D3D11_SDK_VERSION,
                pSwapChainDesc, ppSwapChain, &Device11, nullptr, nullptr);
    if (SUCCEEDED(hr))
    {
        hr = Device11->QueryInterface(DeviceInterface, ppDevice);
        Device11->Release();
    }

    if (FAILED(hr))
    {
        if (ppSwapChain != nullptr && *ppSwapChain != nullptr)
        {
            (*ppSwapChain)->Release();
            *ppSwapChain = nullptr;
        }
        return hr;
    }

    /* Direct3D 10 devices are protected for use from several threads unless asked not to be */
    Device10 = static_cast<ID3D10Device*>(*ppDevice);
    if (!(Device10->GetCreationFlags() & D3D10_CREATE_DEVICE_SINGLETHREADED) &&
        SUCCEEDED(Device10->QueryInterface(IID_PPV_ARGS(&Multithread))))
    {
        Multithread->SetMultithreadProtected(TRUE);
        Multithread->Release();
    }

    return hr;
}

#ifdef RX_D3D10_1

/* Direct3D 10.1 runs at 10.0, 10.1 or one of the 9 levels */
static
BOOLEAN
RxIsD3D10_1Level(
    _In_ D3D10_FEATURE_LEVEL1 Level)
{
    return Level == D3D10_FEATURE_LEVEL_10_0 || Level == D3D10_FEATURE_LEVEL_10_1 || (Level & 0xF000) == 0x9000;
}

extern "C"
HRESULT
WINAPI
D3D10CreateDeviceAndSwapChain1(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D10_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_ D3D10_FEATURE_LEVEL1 HardwareLevel,
    _In_ UINT SDKVersion,
    _In_opt_ DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    _Out_opt_ IDXGISwapChain** ppSwapChain,
    _Out_opt_ ID3D10Device1** ppDevice)
{
    if (ppSwapChain != nullptr)
        *ppSwapChain = nullptr;
    if (ppDevice != nullptr)
        *ppDevice = nullptr;

    if (SDKVersion != D3D10_1_SDK_VERSION)
    {
        RX_TRACE(L"D3D10CreateDevice1 with SDK version %u, %u expected", SDKVersion, D3D10_1_SDK_VERSION);
        return E_INVALIDARG;
    }
    if (!RxIsD3D10_1Level(HardwareLevel))
        return E_INVALIDARG;

    return RxCreateD3D10Device(pAdapter, DriverType, Software, Flags, (D3D_FEATURE_LEVEL)HardwareLevel,
                               pSwapChainDesc, ppSwapChain, __uuidof(ID3D10Device1),
                               reinterpret_cast<void**>(ppDevice));
}

extern "C"
HRESULT
WINAPI
D3D10CreateDevice1(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D10_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_ D3D10_FEATURE_LEVEL1 HardwareLevel,
    _In_ UINT SDKVersion,
    _Out_opt_ ID3D10Device1** ppDevice)
{
    return D3D10CreateDeviceAndSwapChain1(pAdapter, DriverType, Software, Flags, HardwareLevel, SDKVersion, nullptr,
                                          nullptr, ppDevice);
}

#else

extern "C"
HRESULT
WINAPI
D3D10CreateDeviceAndSwapChain(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D10_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_ UINT SDKVersion,
    _In_opt_ DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    _Out_opt_ IDXGISwapChain** ppSwapChain,
    _Out_opt_ ID3D10Device** ppDevice)
{
    if (ppSwapChain != nullptr)
        *ppSwapChain = nullptr;
    if (ppDevice != nullptr)
        *ppDevice = nullptr;

    if (SDKVersion != D3D10_SDK_VERSION)
    {
        RX_TRACE(L"D3D10CreateDevice with SDK version %u, %u expected", SDKVersion, D3D10_SDK_VERSION);
        return E_INVALIDARG;
    }

    return RxCreateD3D10Device(pAdapter, DriverType, Software, Flags, D3D_FEATURE_LEVEL_10_0, pSwapChainDesc,
                               ppSwapChain, __uuidof(ID3D10Device), reinterpret_cast<void**>(ppDevice));
}

extern "C"
HRESULT
WINAPI
D3D10CreateDevice(
    _In_opt_ IDXGIAdapter* pAdapter,
    _In_ D3D10_DRIVER_TYPE DriverType,
    _In_opt_ HMODULE Software,
    _In_ UINT Flags,
    _In_ UINT SDKVersion,
    _Out_opt_ ID3D10Device** ppDevice)
{
    return D3D10CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, SDKVersion, nullptr, nullptr,
                                         ppDevice);
}

#endif
