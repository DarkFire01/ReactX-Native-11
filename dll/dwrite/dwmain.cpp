/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exported entry point
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"

extern const WCHAR RxTraceModule[] = L"dwrite";

/* The shared factory lives until the process ends */
static RxWriteFactory* RxSharedFactory;
static INIT_ONCE RxSharedFactoryOnce = INIT_ONCE_STATIC_INIT;

static
BOOL
CALLBACK
RxCreateSharedFactory(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ PVOID* Context)
{
    return SUCCEEDED(RxWriteFactory::Create(DWRITE_FACTORY_TYPE_SHARED, &RxSharedFactory));
}

extern "C"
HRESULT
WINAPI
DWriteCreateFactory(
    _In_ DWRITE_FACTORY_TYPE factoryType,
    _In_ REFIID iid,
    _COM_Outptr_ IUnknown** factory) WIN_NOEXCEPT
{
    RxWriteFactory* Factory;
    HRESULT hr;

    if (factory == nullptr)
        return E_INVALIDARG;
    *factory = nullptr;

    if (factoryType == DWRITE_FACTORY_TYPE_SHARED)
    {
        if (!InitOnceExecuteOnce(&RxSharedFactoryOnce, RxCreateSharedFactory, nullptr, nullptr))
            return E_OUTOFMEMORY;
        return RxSharedFactory->QueryInterface(iid, reinterpret_cast<void**>(factory));
    }

    if (factoryType != DWRITE_FACTORY_TYPE_ISOLATED)
        return E_INVALIDARG;

    hr = RxWriteFactory::Create(factoryType, &Factory);
    if (FAILED(hr))
        return hr;

    hr = Factory->QueryInterface(iid, reinterpret_cast<void**>(factory));
    Factory->Release();
    return hr;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);
    return TRUE;
}
