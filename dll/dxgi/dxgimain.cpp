/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry points
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"

extern const WCHAR RxTraceModule[] = L"dxgi";

/* CreateDXGIFactory2 turns this interface away instead of handing out a factory for it */
static constexpr GUID RxRefusedFactoryInterface =
    { 0x50c7f8e9, 0xa63f, 0x4035, { 0x98, 0x01, 0xb9, 0x6e, 0x73, 0x33, 0x47, 0xde } };

/* Creates a factory with the given creation flags and asks it for riid */
static
HRESULT
RxCreateFactory(
    _In_ UINT Flags,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppFactory)
{
    RxDxgiFactory* Factory;
    HRESULT hr;

    if (ppFactory != nullptr)
        *ppFactory = nullptr;

    hr = RxDxgiFactory::Create(Flags, &Factory);
    if (FAILED(hr))
        return hr;

    hr = Factory->QueryInterface(riid, ppFactory);
    Factory->Release();
    return hr;
}

/* The debug layer ships with the Windows SDK as System32\dxgidebug.dll */
static
BOOLEAN
RxIsDebugLayerInstalled(VOID)
{
    WCHAR Path[MAX_PATH];
    UINT Length;

    Length = GetSystemDirectoryW(Path, _countof(Path));
    if (Length == 0 || Length >= _countof(Path) || wcscat_s(Path, L"\\dxgidebug.dll") != 0)
        return FALSE;

    return GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
}

extern "C"
HRESULT
WINAPI
CreateDXGIFactory(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppFactory)
{
    return RxCreateFactory(RxDxgiFactory::LegacyFactory, riid, ppFactory);
}

extern "C"
HRESULT
WINAPI
CreateDXGIFactory1(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppFactory)
{
    return RxCreateFactory(0, riid, ppFactory);
}

extern "C"
HRESULT
WINAPI
CreateDXGIFactory2(
    _In_ UINT Flags,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppFactory)
{
    if (riid == RxRefusedFactoryInterface)
    {
        if (ppFactory != nullptr)
            *ppFactory = nullptr;
        return E_NOINTERFACE;
    }

    /* The debug flag needs the SDK debug layer to be installed, as on Windows */
    if ((Flags & DXGI_CREATE_FACTORY_DEBUG) && !RxIsDebugLayerInstalled())
        return DXGI_ERROR_INVALID_CALL;

    return RxCreateFactory(Flags & ~RxDxgiFactory::LegacyFactory, riid, ppFactory);
}

extern "C"
HRESULT
WINAPI
DXGIGetDebugInterface1(
    _In_ UINT Flags,
    _In_ REFIID riid,
    _COM_Outptr_ void** pDebug)
{
    UNREFERENCED_PARAMETER(Flags);
    UNREFERENCED_PARAMETER(riid);

    if (pDebug == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pDebug = nullptr;
    return DXGI_ERROR_SDK_COMPONENT_MISSING;
}

/* gdi32 exports this without an SDK declaration; it fills in one BOOLEAN */
typedef NTSTATUS (APIENTRY* PFN_RX_SET_REMOVAL_SUPPORT)(
    _In_ BOOLEAN* Supported);

extern "C"
HRESULT
WINAPI
DXGIDeclareAdapterRemovalSupport(VOID)
{
    static PFN_RX_SET_REMOVAL_SUPPORT SetSupport = []()
    {
        HMODULE Gdi32 = LoadLibraryExW(L"gdi32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

        return Gdi32 ? reinterpret_cast<PFN_RX_SET_REMOVAL_SUPPORT>(
                           GetProcAddress(Gdi32, "D3DKMTSetProcessDeviceRemovalSupport"))
                     : nullptr;
    }();
    BOOLEAN Supported = TRUE;
    NTSTATUS Status;

    if (SetSupport == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Status = SetSupport(&Supported);
    switch (Status)
    {
        /* A second declaration, or one after a device already exists */
        case STATUS_ALREADY_INITIALIZED:
            return DXGI_ERROR_ALREADY_EXISTS;
        case STATUS_UNSUCCESSFUL:
            return DXGI_ERROR_INVALID_CALL;
        default:
            return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
    }
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_ LPVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
