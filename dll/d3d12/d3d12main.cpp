/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     d3d12.dll, the loader that hands every call to d3d12core.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"

#include <d3d12.h>

#include "d3d12bridge.hpp"

extern const WCHAR RxTraceModule[] = L"d3d12";

using PFN_RX_D3D12_GET_INTERFACE = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);

/* The d3d12core.dll beside this module and the table it hands back */
_Ret_maybenull_
static
const RxD3D12CoreExports*
RxGetCore(VOID)
{
    static const RxD3D12CoreExports* const Exports = []()
    {
        WCHAR Path[MAX_PATH];
        HMODULE Self = NULL;
        HMODULE Core;
        PWSTR Slash;
        PFN_RX_D3D12_GET_INTERFACE GetInterface;
        const RxD3D12CoreExports* Table = nullptr;
        HRESULT hr;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&RxGetCore), &Self) ||
            GetModuleFileNameW(Self, Path, _countof(Path)) == 0 || (Slash = wcsrchr(Path, L'\\')) == nullptr ||
            wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"d3d12core.dll") != 0)
        {
            return (const RxD3D12CoreExports*)nullptr;
        }

        Core = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (Core == NULL)
        {
            RX_TRACE(L"d3d12core.dll did not load (%lu)", GetLastError());
            return (const RxD3D12CoreExports*)nullptr;
        }

        GetInterface = reinterpret_cast<PFN_RX_D3D12_GET_INTERFACE>(GetProcAddress(Core, "D3D12GetInterface"));
        if (GetInterface == nullptr)
            return (const RxD3D12CoreExports*)nullptr;

        hr = GetInterface(CLSID_RxD3D12CoreExports, CLSID_RxD3D12CoreExports, (void**)&Table);
        if (FAILED(hr) || Table == nullptr || Table->Version != RxD3D12CoreExportsVersion)
        {
            RX_TRACE(L"d3d12core.dll is not a ReactX core (0x%08lX)", hr);
            return (const RxD3D12CoreExports*)nullptr;
        }

        return Table;
    }();

    return Exports;
}

/* An application asking for its own D3D12Core.dll still gets this one */
static
VOID
RxNoteAgilitySdk(VOID)
{
    static LONG Noted;
    const UINT* SdkVersion;

    if (InterlockedExchange(&Noted, 1) != 0)
        return;

    SdkVersion = reinterpret_cast<const UINT*>(GetProcAddress(GetModuleHandleW(NULL), "D3D12SDKVersion"));
    if (SdkVersion != nullptr)
        RX_TRACE(L"the application asks for SDK %u, using the ReactX core", *SdkVersion);
}

extern "C"
HRESULT
WINAPI
D3D12CreateDevice(
    _In_opt_ IUnknown* pAdapter,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppDevice)
{
    const RxD3D12CoreExports* Core;

    RxNoteAgilitySdk();

    Core = RxGetCore();
    if (Core == nullptr)
    {
        if (ppDevice != nullptr)
            *ppDevice = nullptr;
        return E_FAIL;
    }

    return Core->CreateDevice(pAdapter, MinimumFeatureLevel, riid, ppDevice);
}

extern "C"
HRESULT
WINAPI
D3D12GetInterface(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvDebug)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
    {
        if (ppvDebug != nullptr)
            *ppvDebug = nullptr;
        return E_FAIL;
    }

    return Core->GetInterface(rclsid, riid, ppvDebug);
}

/* The debug interfaces are core objects, each found by the class that makes it */
extern "C"
HRESULT
WINAPI
D3D12GetDebugInterface(
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvDebug)
{
    const CLSID* Class = &CLSID_D3D12Debug;

    if (ppvDebug != nullptr)
        *ppvDebug = nullptr;

    if (riid == __uuidof(ID3D12Tools) || riid == __uuidof(ID3D12Tools1) || riid == __uuidof(ID3D12Tools2))
        Class = &CLSID_D3D12Tools;
    else if (riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings) ||
             riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings1) ||
             riid == __uuidof(ID3D12DeviceRemovedExtendedDataSettings2))
    {
        Class = &CLSID_D3D12DeviceRemovedExtendedData;
    }

    return D3D12GetInterface(*Class, riid, ppvDebug);
}

extern "C"
HRESULT
WINAPI
D3D12SerializeRootSignature(
    _In_ const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
        return E_FAIL;

    return Core->SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
}

extern "C"
HRESULT
WINAPI
D3D12SerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
        return E_FAIL;

    return Core->SerializeVersionedRootSignature(pRootSignature, ppBlob, ppErrorBlob);
}

extern "C"
HRESULT
WINAPI
D3D12CreateRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
        return E_FAIL;

    return Core->CreateRootSignatureDeserializer(pSrcData, SrcDataSizeInBytes, pRootSignatureDeserializerInterface,
                                                 ppRootSignatureDeserializer);
}

extern "C"
HRESULT
WINAPI
D3D12CreateVersionedRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
        return E_FAIL;

    return Core->CreateVersionedRootSignatureDeserializer(pSrcData, SrcDataSizeInBytes,
                                                          pRootSignatureDeserializerInterface,
                                                          ppRootSignatureDeserializer);
}

extern "C"
HRESULT
WINAPI
D3D12EnableExperimentalFeatures(
    _In_ UINT NumFeatures,
    _In_count_(NumFeatures) const IID* pIIDs,
    _In_opt_count_(NumFeatures) void* pConfigurationStructs,
    _In_opt_count_(NumFeatures) UINT* pConfigurationStructSizes)
{
    const RxD3D12CoreExports* Core = RxGetCore();

    if (Core == nullptr)
        return E_FAIL;

    return Core->EnableExperimentalFeatures(NumFeatures, pIIDs, pConfigurationStructs, pConfigurationStructSizes);
}
