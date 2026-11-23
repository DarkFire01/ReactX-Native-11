/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The filtered, sorted adapter list a factory hands out
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

class DcAdapterList final : public IDXCoreAdapterList
{
public:
    DcAdapterList(VOID);

    /* IUnknown */
    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override;

    STDMETHODIMP_(ULONG) AddRef(VOID) override;
    STDMETHODIMP_(ULONG) Release(VOID) override;

    /* IDXCoreAdapterList */
    HRESULT
    STDMETHODCALLTYPE
    GetAdapter(
        _In_ uint32_t Index,
        _In_ REFIID Riid,
        _COM_Outptr_ void** Adapter) override;

    uint32_t STDMETHODCALLTYPE GetAdapterCount(VOID) override;
    bool STDMETHODCALLTYPE IsStale(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFactory(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Factory) override;

    HRESULT
    STDMETHODCALLTYPE
    Sort(
        _In_ uint32_t PreferenceCount,
        _In_reads_(PreferenceCount) const DXCoreAdapterPreference* Preferences) override;

    bool
    STDMETHODCALLTYPE
    IsAdapterPreferenceSupported(
        _In_ DXCoreAdapterPreference Preference) override;

    /* Orders the list by the preferences in turn */
    VOID
    SortByPreferences(
        _In_ UINT PreferenceCount,
        _In_reads_(PreferenceCount) const DXCoreAdapterPreference* Preferences);

    /* The order a list starts in when no system GPU preference applies */
    VOID
    SortFirstTime(
        _In_ BOOLEAN ForGraphics);

    VOID
    SortForGpuPreference(
        _In_ UINT Preference);

    VOID
    SortForUserChosenAdapter(
        _In_ UINT Index);

    VOID
    SortForWorkload(VOID);

    /* Asks the shared DirectX settings which adapter the system prefers */
    HRESULT
    QuerySystemPreference(
        _Out_ UINT* Preference,
        _Out_ UINT* Index,
        _Out_ UINT* Factor);

    HRESULT
    AddNotification(
        _In_ DXCoreNotificationType Type,
        _In_ PFN_DXCORE_NOTIFICATION_CALLBACK Callback,
        _In_opt_ void* Context,
        _Out_ UINT32* Cookie);

    HRESULT
    RemoveNotification(
        _In_ UINT32 Cookie);

    VOID
    Notify(VOID);

    std::vector<DcAdapter*> Adapters;
    UINT Count = 0;
    BOOLEAN Fresh = TRUE;
    BOOLEAN NeedsEveryAttribute = TRUE;
    BOOLEAN ForWorkload = FALSE;
    DXCoreWorkload Workload = DXCoreWorkload::Graphics;
    std::vector<GUID> Required;
    std::vector<GUID> HardwareTypes;
    DcFactory* Factory = nullptr;
    SRWLOCK Lock = SRWLOCK_INIT;

private:
    ~DcAdapterList(VOID);

    LONG RefCount = 1;
    std::vector<DcNotification> Notifications;
};
