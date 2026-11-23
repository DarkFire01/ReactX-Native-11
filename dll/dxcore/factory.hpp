/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The process wide adapter factory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* What a notification cookie points back to */
struct DcCookieOwner
{
    UINT32 Cookie;
    BOOLEAN ForAdapter;
    void* Owner;
};

/* A reference counted subscription to one kernel state name */
struct DcStateWatch
{
    ULONG64 StateName;
    ULONG Users;
    void* Subscription;
    DXCoreNotificationType Type;
};

class DcFactory final : public IDXCoreAdapterFactory2
{
public:
    DcFactory(VOID);

    /* Enumerates the adapters and starts watching for changes */
    HRESULT
    Initialize(VOID);

    /* IUnknown */
    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override;

    STDMETHODIMP_(ULONG) AddRef(VOID) override;
    STDMETHODIMP_(ULONG) Release(VOID) override;

    /* IDXCoreAdapterFactory */
    HRESULT
    STDMETHODCALLTYPE
    CreateAdapterList(
        _In_ uint32_t AttributeCount,
        _In_reads_(AttributeCount) const GUID* Attributes,
        _In_ REFIID Riid,
        _COM_Outptr_ void** List) override;

    HRESULT
    STDMETHODCALLTYPE
    GetAdapterByLuid(
        _In_ const LUID& AdapterLuid,
        _In_ REFIID Riid,
        _COM_Outptr_ void** Adapter) override;

    bool
    STDMETHODCALLTYPE
    IsNotificationTypeSupported(
        _In_ DXCoreNotificationType Type) override;

    HRESULT
    STDMETHODCALLTYPE
    RegisterEventNotification(
        _In_ IUnknown* Object,
        _In_ DXCoreNotificationType Type,
        _In_ PFN_DXCORE_NOTIFICATION_CALLBACK Callback,
        _In_opt_ void* Context,
        _Out_ uint32_t* Cookie) override;

    HRESULT
    STDMETHODCALLTYPE
    UnregisterEventNotification(
        _In_ uint32_t Cookie) override;

    /* IDXCoreAdapterFactory1 */
    HRESULT
    STDMETHODCALLTYPE
    CreateAdapterListByWorkload(
        _In_ DXCoreWorkload Workload,
        _In_ DXCoreRuntimeFilterFlags RuntimeFilter,
        _In_ DXCoreHardwareTypeFilterFlags HardwareTypeFilter,
        _In_ REFIID Riid,
        _COM_Outptr_ void** List) override;

    /* IDXCoreAdapterFactory2 */
    HRESULT
    STDMETHODCALLTYPE
    GetReservedAcceleratorMemoryInfo(
        _Out_ DXCoreReservedAcceleratorMemoryInfo* Info) override;

    /* Hands out the next free cookie and records who owns it */
    HRESULT
    TakeCookie(
        _In_ BOOLEAN ForAdapter,
        _In_ void* Owner,
        _Out_ UINT32* Cookie);

    VOID
    DropCookie(
        _In_ UINT32 Cookie);

    HRESULT
    WatchState(
        _In_ DXCoreNotificationType Type);

    VOID
    UnwatchState(
        _In_ DXCoreNotificationType Type);

    /* Sends a kernel state change to every adapter registered for it */
    VOID
    StateChanged(
        _In_ DXCoreNotificationType Type);

    /* Re-enumerates after a device arrival and marks the lists that changed */
    VOID
    AdaptersChanged(VOID);

    VOID
    ForgetList(
        _In_ DcAdapterList* List);

    ULONG Capabilities = 0;
    SRWLOCK Lock = SRWLOCK_INIT;

    /* Kept in ascending order of the LUID as one 64 bit number */
    std::vector<DcAdapter*> Adapters;
    std::vector<DcAdapterList*> Lists;

private:
    ~DcFactory(VOID);

    HRESULT
    Enumerate(
        _In_ BOOLEAN Initial);

    HRESULT
    ReadPhysicalAdapters(
        _In_ const D3DKMT_ADAPTERINFO& Info,
        _Inout_ DcAdapterTraits* Traits);

    VOID
    KeepAdapter(
        _In_ BOOLEAN Initial,
        _In_ const D3DKMT_ADAPTERINFO& Info,
        _In_ DcAdapterTraits& Traits,
        _In_ ULONG Flags,
        _In_ const DXCoreHardwareIDParts& HardwareId);

    HRESULT
    StartMonitoring(VOID);

    HRESULT
    BuildList(
        _In_ UINT AttributeCount,
        _In_reads_opt_(AttributeCount) const GUID* Attributes,
        _In_ REFIID Riid,
        _COM_Outptr_ void** List);

    VOID
    FillList(
        _Inout_ DcAdapterList* List,
        _In_ BOOLEAN NeedsEveryAttribute);

    LONG RefCount = 1;
    UINT32 NextCookie = 1;
    SRWLOCK CookieLock = SRWLOCK_INIT;
    std::vector<DcCookieOwner> Cookies;
    SRWLOCK WatchLock = SRWLOCK_INIT;
    DcStateWatch Watches[2];
    void* Monitor = nullptr;
};

/* Whether GetAdapterByLuid may hand out adapters that are neither render nor compute on this thread */
BOOLEAN
DcThreadAllowsAnyAdapter(VOID);

VOID
DcSetThreadAllowsAnyAdapter(
    _In_ BOOLEAN Allow);
