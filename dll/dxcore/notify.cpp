/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Adapter notifications and the kernel state names behind them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

typedef NTSTATUS (NTAPI* PFN_DC_STATE_CALLBACK)(ULONG64, ULONG, void*, void*, const void*, ULONG);
typedef NTSTATUS (NTAPI* PFN_DC_QUERY_STATE)(ULONG*, ULONG64, PFN_DC_STATE_CALLBACK, void*, void*);
typedef NTSTATUS (NTAPI* PFN_DC_SUBSCRIBE_STATE)(void**, ULONG64, ULONG, PFN_DC_STATE_CALLBACK, void*, void*, ULONG,
                                                 ULONG);
typedef NTSTATUS (NTAPI* PFN_DC_UNSUBSCRIBE_STATE)(void*);

/* One callback waiting for a worker thread */
struct DcPendingNotification
{
    IUnknown* Object;
    DcNotification Notification;
};

static
VOID
CALLBACK
DcDeliverNotification(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID Context)
{
    DcPendingNotification* Pending = static_cast<DcPendingNotification*>(Context);

    Pending->Notification.Callback(Pending->Notification.Type, Pending->Object, Pending->Notification.Context);
    Pending->Object->Release();
    delete Pending;
}

VOID
DcPostNotification(
    _In_ IUnknown* Object,
    _In_ const DcNotification& Notification)
{
    DcPendingNotification* Pending = new (std::nothrow) DcPendingNotification;

    if (Pending == nullptr)
        return;

    Pending->Object = Object;
    Pending->Notification = Notification;
    Object->AddRef();
    if (!TrySubmitThreadpoolCallback(DcDeliverNotification, Pending, nullptr))
    {
        DcTrace(L"a notification could not be queued (%lu)", GetLastError());
        Object->Release();
        delete Pending;
    }
}

HRESULT
DcAdapter::AddNotification(
    _In_ DXCoreNotificationType Type,
    _In_ PFN_DXCORE_NOTIFICATION_CALLBACK Callback,
    _In_opt_ void* Context,
    _Out_ UINT32* Cookie)
{
    DcFactory* Factory = DcGlobalFactory;
    DcNotification Entry;
    HRESULT hr;

    hr = Factory->TakeCookie(TRUE, this, &Entry.Cookie);
    if (FAILED(hr))
        return hr;

    Entry.Type = Type;
    Entry.Callback = Callback;
    Entry.Context = Context;

    if (Type == DXCoreNotificationType::AdapterNoLongerValid)
    {
        AcquireSRWLockExclusive(&NotifyLock);
        Notifications.push_back(Entry);
        *Cookie = Entry.Cookie;
        if (!IsValid())
            DcPostNotification(static_cast<IDXCoreAdapter1*>(this), Entry);
        ReleaseSRWLockExclusive(&NotifyLock);
        return S_OK;
    }

    /* The cookie stays taken even when the type is refused */
    if (Type != DXCoreNotificationType::AdapterBudgetChange &&
        Type != DXCoreNotificationType::AdapterHardwareContentProtectionTeardown)
    {
        return E_INVALIDARG;
    }

    hr = Factory->WatchState(Type);
    if (FAILED(hr))
    {
        Factory->DropCookie(Entry.Cookie);
        return hr;
    }

    AcquireSRWLockExclusive(&NotifyLock);
    Notifications.push_back(Entry);
    *Cookie = Entry.Cookie;

    /* A new budget or teardown watcher hears the current state straight away */
    DcPostNotification(static_cast<IDXCoreAdapter1*>(this), Entry);
    ReleaseSRWLockExclusive(&NotifyLock);
    return S_OK;
}

HRESULT
DcAdapter::RemoveNotification(
    _In_ UINT32 Cookie)
{
    DXCoreNotificationType Type;

    AcquireSRWLockExclusive(&NotifyLock);
    for (size_t Slot = 0; Slot < Notifications.size(); Slot++)
    {
        if (Notifications[Slot].Cookie != Cookie)
            continue;

        Type = Notifications[Slot].Type;
        Notifications.erase(Notifications.begin() + Slot);
        ReleaseSRWLockExclusive(&NotifyLock);

        if (Type != DXCoreNotificationType::AdapterNoLongerValid && DcGlobalFactory != nullptr)
            DcGlobalFactory->UnwatchState(Type);
        return S_OK;
    }

    ReleaseSRWLockExclusive(&NotifyLock);
    return E_INVALIDARG;
}

VOID
DcAdapter::Notify(
    _In_ DXCoreNotificationType Type)
{
    AcquireSRWLockShared(&NotifyLock);
    for (const DcNotification& Entry : Notifications)
    {
        if (Entry.Type == Type)
            DcPostNotification(static_cast<IDXCoreAdapter1*>(this), Entry);
    }
    ReleaseSRWLockShared(&NotifyLock);
}

static
NTSTATUS
NTAPI
DcIgnoreStateData(
    _In_ ULONG64 StateName,
    _In_ ULONG ChangeStamp,
    _In_opt_ void* TypeId,
    _In_opt_ void* Context,
    _In_reads_bytes_opt_(Size) const void* Data,
    _In_ ULONG Size)
{
    return STATUS_SUCCESS;
}

/* Kernel state names report budget changes and content protection teardowns */
static
NTSTATUS
NTAPI
DcStateNameChanged(
    _In_ ULONG64 StateName,
    _In_ ULONG ChangeStamp,
    _In_opt_ void* TypeId,
    _In_opt_ void* Context,
    _In_reads_bytes_opt_(Size) const void* Data,
    _In_ ULONG Size)
{
    DcFactory* Factory = static_cast<DcFactory*>(Context);

    if (StateName == 0x41C61629A3BC3875ull)
        Factory->StateChanged(DXCoreNotificationType::AdapterBudgetChange);
    else
        Factory->StateChanged(DXCoreNotificationType::AdapterHardwareContentProtectionTeardown);

    return STATUS_SUCCESS;
}

HRESULT
DcFactory::WatchState(
    _In_ DXCoreNotificationType Type)
{
    static PFN_DC_QUERY_STATE Query = reinterpret_cast<PFN_DC_QUERY_STATE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlQueryWnfStateData"));
    static PFN_DC_SUBSCRIBE_STATE Subscribe = reinterpret_cast<PFN_DC_SUBSCRIBE_STATE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlSubscribeWnfStateChangeNotification"));
    DcStateWatch* Watch = (Type == DXCoreNotificationType::AdapterBudgetChange) ? &Watches[0] : &Watches[1];
    ULONG Stamp = 0;
    NTSTATUS Status;

    AcquireSRWLockExclusive(&WatchLock);
    if (Watch->Users == 0)
    {
        if (Query == nullptr || Subscribe == nullptr)
        {
            ReleaseSRWLockExclusive(&WatchLock);
            return E_OUTOFMEMORY;
        }

        Status = Query(&Stamp, Watch->StateName, DcIgnoreStateData, nullptr, nullptr);
        if (NT_SUCCESS(Status))
            Status = Subscribe(&Watch->Subscription, Watch->StateName, Stamp, DcStateNameChanged, this, nullptr, 0, 0);

        if (!NT_SUCCESS(Status))
        {
            DcTrace(L"state name subscription failed (0x%08lX)", Status);
            Watch->Subscription = nullptr;
            ReleaseSRWLockExclusive(&WatchLock);
            return E_OUTOFMEMORY;
        }
    }

    Watch->Users += 1;
    ReleaseSRWLockExclusive(&WatchLock);
    return S_OK;
}

VOID
DcFactory::UnwatchState(
    _In_ DXCoreNotificationType Type)
{
    static PFN_DC_UNSUBSCRIBE_STATE Unsubscribe = reinterpret_cast<PFN_DC_UNSUBSCRIBE_STATE>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlUnsubscribeWnfStateChangeNotification"));
    DcStateWatch* Watch = (Type == DXCoreNotificationType::AdapterBudgetChange) ? &Watches[0] : &Watches[1];
    void* Subscription = nullptr;

    AcquireSRWLockExclusive(&WatchLock);
    if (Watch->Users != 0)
    {
        Watch->Users -= 1;
        if (Watch->Users == 0)
        {
            Subscription = Watch->Subscription;
            Watch->Subscription = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&WatchLock);

    if (Subscription != nullptr && Unsubscribe != nullptr)
        Unsubscribe(Subscription);
}

VOID
DcFactory::StateChanged(
    _In_ DXCoreNotificationType Type)
{
    AcquireSRWLockShared(&Lock);
    for (DcAdapter* Adapter : Adapters)
        Adapter->Notify(Type);
    ReleaseSRWLockShared(&Lock);
}
