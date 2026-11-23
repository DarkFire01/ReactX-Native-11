/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12DeviceFactory and ID3D12DeviceConfiguration: device creation with its own settings
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "core.hpp"

extern "C"
HRESULT
WINAPI
D3D12GetInterface(
    _In_ REFCLSID rclsid,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvDebug);

extern "C" const UINT D3D12SDKVersion;

constexpr UINT RxKnownFactoryFlags = D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE |
                                     D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE |
                                     D3D12_DEVICE_FACTORY_FLAG_DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON;

/* One object answers as the factory and as the configuration its devices are made with */
class RxDeviceFactory12 final : public ID3D12DeviceConfiguration1, public ID3D12DeviceFactory
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

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12DeviceConfiguration) ||
            riid == __uuidof(ID3D12DeviceConfiguration1))
        {
            *ppvObject = static_cast<ID3D12DeviceConfiguration1*>(this);
        }
        else if (riid == __uuidof(ID3D12DeviceFactory))
        {
            *ppvObject = static_cast<ID3D12DeviceFactory*>(this);
        }
        else
        {
            /* Windows leaves the output alone when it has no such interface */
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
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

    /* ID3D12DeviceFactory */

    HRESULT
    STDMETHODCALLTYPE
    InitializeFromGlobalState(VOID) override
    {
        AcquireSRWLockExclusive(&Lock);
        Dred = RxProcessDred;
        ReleaseSRWLockExclusive(&Lock);
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    ApplyToGlobalState(VOID) override
    {
        AcquireSRWLockExclusive(&Lock);
        RxProcessDred = Dred;
        ReleaseSRWLockExclusive(&Lock);
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    SetFlags(
        D3D12_DEVICE_FACTORY_FLAGS flags) override
    {
        UINT Wanted = flags;

        /* Returning incompatible devices only makes sense once existing ones may be returned */
        if ((Wanted & ~RxKnownFactoryFlags) != 0 ||
            (Wanted & (D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_EXISTING_DEVICE |
                       D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE)) ==
                D3D12_DEVICE_FACTORY_FLAG_ALLOW_RETURNING_INCOMPATIBLE_EXISTING_DEVICE)
        {
            return E_INVALIDARG;
        }

        AcquireSRWLockExclusive(&Lock);
        Flags = Wanted;
        ReleaseSRWLockExclusive(&Lock);
        return S_OK;
    }

    D3D12_DEVICE_FACTORY_FLAGS
    STDMETHODCALLTYPE
    GetFlags(VOID) override
    {
        D3D12_DEVICE_FACTORY_FLAGS Current;

        AcquireSRWLockShared(&Lock);
        Current = (D3D12_DEVICE_FACTORY_FLAGS)Flags;
        ReleaseSRWLockShared(&Lock);
        return Current;
    }

    /* Settings objects asked for here change this factory, not the process */
    HRESULT
    STDMETHODCALLTYPE
    GetConfigurationInterface(
        REFCLSID clsid,
        REFIID iid,
        _COM_Outptr_ void** ppv) override
    {
        if (clsid == CLSID_D3D12DeviceRemovedExtendedData)
            return RxMakeDredSettings(&Dred, iid, ppv);

        return D3D12GetInterface(clsid, iid, ppv);
    }

    /* Experimental features need developer mode, which this runtime leaves off */
    HRESULT
    STDMETHODCALLTYPE
    EnableExperimentalFeatures(
        UINT NumFeatures,
        _In_reads_(NumFeatures) const IID* pIIDs,
        _In_reads_opt_(NumFeatures) void* pConfigurationStructs,
        _In_reads_opt_(NumFeatures) UINT* pConfigurationStructSizes) override
    {
        if (NumFeatures != 0)
        {
            RX_TRACE(L"device factory refused %u experimental features", NumFeatures);
            return E_NOINTERFACE;
        }

        return S_OK;
    }

    /*
     * Windows hands each factory request a device of its own here, whatever the flags say about
     * existing devices, and leaves the adapter's D3D12CreateDevice singleton as it was.
     */
    HRESULT
    STDMETHODCALLTYPE
    CreateDevice(
        _In_opt_ IUnknown* adapter,
        D3D_FEATURE_LEVEL FeatureLevel,
        REFIID riid,
        _COM_Outptr_opt_ void** ppvDevice) override
    {
        return RxCreateSharedDevice(adapter, FeatureLevel, 0, riid, ppvDevice);
    }

    /* ID3D12DeviceConfiguration */

    D3D12_DEVICE_CONFIGURATION_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        D3D12_DEVICE_CONFIGURATION_DESC Desc = {};
        UINT DeviceFlags = 0;

        AcquireSRWLockShared(&Lock);
        if (Dred.AutoBreadcrumbs == D3D12_DRED_ENABLEMENT_FORCED_ON)
            DeviceFlags |= D3D12_DEVICE_FLAG_DRED_AUTO_BREADCRUMBS_ENABLED;
        if (Dred.PageFault == D3D12_DRED_ENABLEMENT_FORCED_ON)
            DeviceFlags |= D3D12_DEVICE_FLAG_DRED_PAGE_FAULT_REPORTING_ENABLED;
        if (Dred.WatsonDump == D3D12_DRED_ENABLEMENT_FORCED_ON)
            DeviceFlags |= D3D12_DEVICE_FLAG_DRED_WATSON_REPORTING_ENABLED;
        if (Dred.BreadcrumbContext == D3D12_DRED_ENABLEMENT_FORCED_ON)
            DeviceFlags |= D3D12_DEVICE_FLAG_DRED_BREADCRUMB_CONTEXT_ENABLED;
        if (Dred.MarkersOnlyBreadcrumbs)
            DeviceFlags |= D3D12_DEVICE_FLAG_DRED_USE_MARKERS_ONLY_BREADCRUMBS;
        ReleaseSRWLockShared(&Lock);

        Desc.Flags = (D3D12_DEVICE_FLAGS)DeviceFlags;
        Desc.SDKVersion = D3D12SDKVersion;
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetEnabledExperimentalFeatures(
        _Out_writes_(NumGuids) GUID* pGuids,
        UINT NumGuids) override
    {
        /* No feature is ever enabled, so any buffer is big enough */
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    SerializeVersionedRootSignature(
        _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pDesc,
        _COM_Outptr_ ID3DBlob** ppResult,
        _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppError) override
    {
        return RxSerializeVersionedRootSignature(pDesc, ppResult, ppError);
    }

    HRESULT
    STDMETHODCALLTYPE
    CreateVersionedRootSignatureDeserializer(
        _In_reads_bytes_(Size) const void* pBlob,
        SIZE_T Size,
        REFIID riid,
        _COM_Outptr_ void** ppvDeserializer) override
    {
        return RxCreateVersionedRootSignatureDeserializer(pBlob, Size, riid, ppvDeserializer);
    }

    /* ID3D12DeviceConfiguration1 */

    HRESULT
    STDMETHODCALLTYPE
    CreateVersionedRootSignatureDeserializerFromSubobjectInLibrary(
        _In_reads_bytes_(Size) const void* pLibraryBlob,
        SIZE_T Size,
        LPCWSTR RootSignatureSubobjectName,
        REFIID riid,
        _COM_Outptr_ void** ppvDeserializer) override
    {
        if (ppvDeserializer != nullptr)
            *ppvDeserializer = nullptr;
        RX_UNIMPLEMENTED();
        return E_NOTIMPL;
    }

private:
    ~RxDeviceFactory12() = default;

    LONG RefCount = 1;
    SRWLOCK Lock = SRWLOCK_INIT;
    UINT Flags = 0;
    RxDredState Dred = {};
};

HRESULT
RxMakeDeviceFactory(
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject)
{
    RxDeviceFactory12* Factory;
    HRESULT hr;

    /* Without an output the caller only learns that factories exist */
    if (ppvObject == nullptr)
        return S_FALSE;

    Factory = new (std::nothrow) RxDeviceFactory12();
    if (Factory == nullptr)
        return E_OUTOFMEMORY;

    hr = Factory->QueryInterface(riid, ppvObject);
    Factory->Release();
    return hr;
}
