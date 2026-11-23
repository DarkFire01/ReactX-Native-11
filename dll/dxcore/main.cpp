/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     dxcore.dll entry points and process wide state
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

CRITICAL_SECTION DcGlobalLock;
DcFactory* DcGlobalFactory;

static DWORD DcThreadSlot = TLS_OUT_OF_INDEXES;

/* The feature configuration record ntdll hands back */
struct DC_FEATURE_RECORD
{
    ULONG FeatureId;
    ULONG Settings;
    ULONG Payload;
};

typedef NTSTATUS (NTAPI* PFN_DC_QUERY_FEATURE)(ULONG, ULONG, PULONGLONG, DC_FEATURE_RECORD*);

BOOLEAN
DcIsSystemFeatureEnabled(
    _In_ UINT32 FeatureId)
{
    static PFN_DC_QUERY_FEATURE Query = reinterpret_cast<PFN_DC_QUERY_FEATURE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlQueryFeatureConfiguration"));
    DC_FEATURE_RECORD Record = {};
    ULONGLONG Stamp = 0;
    ULONG Kind;

    if (Query == nullptr)
        return TRUE;

    /* The carveout API only exists while the carveout itself is switched on */
    if (FeatureId == DcFeatureCarveoutApi && !DcIsSystemFeatureEnabled(DcFeatureCarveout))
        return FALSE;

    /* The hybrid mode switch follows the running configuration, the rest the boot one */
    Kind = (FeatureId == DcFeatureSingleAdapterHybridMode) ? 1 : 0;
    if (!NT_SUCCESS(Query(FeatureId, Kind, &Stamp, &Record)))
        return TRUE;

    return ((Record.Settings >> 4) & 0x3) != 1;
}

BOOLEAN
DcThreadAllowsAnyAdapter(VOID)
{
    if (DcThreadSlot == TLS_OUT_OF_INDEXES)
        return FALSE;

    return TlsGetValue(DcThreadSlot) != nullptr;
}

VOID
DcSetThreadAllowsAnyAdapter(
    _In_ BOOLEAN Allow)
{
    if (DcThreadSlot != TLS_OUT_OF_INDEXES)
        TlsSetValue(DcThreadSlot, Allow ? reinterpret_cast<void*>(1) : nullptr);
}

static const GUID DcPrivateFactoryId = {0xb2831a01, 0xdcfc, 0x410d, {0x91, 0x80, 0x7e, 0xcd, 0xd8, 0xc2, 0x56, 0x19}};
static const GUID DcThreadOptionsId = {0x0d82d832, 0xe474, 0x4da9, {0xa1, 0x62, 0xc3, 0xdf, 0xfd, 0xa0, 0xfa, 0x6d}};

static BOOLEAN DcPrivateFactoryAsked;

/* The per thread switch object DXCoreCreateInternalFactory hands out */
class DcThreadOptions final : public IUnknown
{
public:
    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override
    {
        if (Object == nullptr)
            return E_POINTER;

        *Object = nullptr;
        if (!DcIsEqualGuid(Riid, __uuidof(IUnknown)) && !DcIsEqualGuid(Riid, DcThreadOptionsId))
            return E_NOINTERFACE;

        *Object = this;
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG)
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    STDMETHODIMP_(ULONG) Release(VOID) override;

    /* Only switch zero exists: GetAdapterByLuid on adapters without render or compute */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    SetInternalFeatureStateForThread(
        _In_ UINT32 Feature,
        _In_ BOOL Enable)
    {
        if (Feature != 0)
            return E_INVALIDARG;

        DcSetThreadAllowsAnyAdapter(Enable != FALSE);
        return S_OK;
    }

private:
    LONG RefCount = 1;
};

static DcThreadOptions* DcGlobalThreadOptions;

STDMETHODIMP_(ULONG)
DcThreadOptions::Release(VOID)
{
    ULONG Count;

    EnterCriticalSection(&DcGlobalLock);
    Count = InterlockedDecrement(&RefCount);
    if (Count == 0)
    {
        if (DcGlobalThreadOptions == this)
            DcGlobalThreadOptions = nullptr;
        delete this;
    }
    LeaveCriticalSection(&DcGlobalLock);
    return Count;
}

EXTERN_C
HRESULT
WINAPI
DXCoreCreateAdapterFactory(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Factory)
{
    DcFactory* Created;
    HRESULT hr;

    if (Factory == nullptr)
        return E_POINTER;

    *Factory = nullptr;
    EnterCriticalSection(&DcGlobalLock);

    if (DcIsEqualGuid(Riid, DcPrivateFactoryId))
        DcPrivateFactoryAsked = TRUE;

    if (DcGlobalFactory != nullptr)
    {
        hr = DcGlobalFactory->QueryInterface(Riid, Factory);
        LeaveCriticalSection(&DcGlobalLock);
        return hr;
    }

    Created = new (std::nothrow) DcFactory();
    if (Created == nullptr)
    {
        LeaveCriticalSection(&DcGlobalLock);
        return E_OUTOFMEMORY;
    }

    hr = Created->Initialize();
    if (FAILED(hr))
    {
        DcTrace(L"the adapter factory did not start (0x%08lX)", hr);
        Created->Release();
        LeaveCriticalSection(&DcGlobalLock);
        return hr;
    }

    DcGlobalFactory = Created;
    hr = Created->QueryInterface(Riid, Factory);
    Created->Release();
    LeaveCriticalSection(&DcGlobalLock);
    return hr;
}

EXTERN_C
HRESULT
WINAPI
DXCoreCreateInternalFactory(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Factory)
{
    DcThreadOptions* Created;
    HRESULT hr;

    if (Factory == nullptr)
        return E_POINTER;

    *Factory = nullptr;
    EnterCriticalSection(&DcGlobalLock);

    if (DcGlobalThreadOptions != nullptr)
    {
        hr = DcGlobalThreadOptions->QueryInterface(Riid, Factory);
        LeaveCriticalSection(&DcGlobalLock);
        return FAILED(hr) ? hr : S_OK;
    }

    Created = new (std::nothrow) DcThreadOptions();
    if (Created == nullptr)
    {
        LeaveCriticalSection(&DcGlobalLock);
        return E_OUTOFMEMORY;
    }

    DcGlobalThreadOptions = Created;
    hr = Created->QueryInterface(Riid, Factory);
    Created->Release();
    LeaveCriticalSection(&DcGlobalLock);
    return FAILED(hr) ? hr : S_OK;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        InitializeCriticalSection(&DcGlobalLock);
        DcThreadSlot = TlsAlloc();
    }
    else if (Reason == DLL_PROCESS_DETACH && Reserved == nullptr)
    {
        if (DcThreadSlot != TLS_OUT_OF_INDEXES)
            TlsFree(DcThreadSlot);
        DeleteCriticalSection(&DcGlobalLock);
    }

    return TRUE;
}
