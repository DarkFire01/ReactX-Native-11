/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The adapter object a factory hands out
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

class DcAdapter final : public IDXCoreAdapter1
{
public:
    DcAdapter(VOID);

    /* IUnknown */
    STDMETHODIMP
    QueryInterface(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override;

    STDMETHODIMP_(ULONG) AddRef(VOID) override;
    STDMETHODIMP_(ULONG) Release(VOID) override;

    /* IDXCoreAdapter */
    bool STDMETHODCALLTYPE IsValid(VOID) override;

    bool
    STDMETHODCALLTYPE
    IsAttributeSupported(
        _In_ REFGUID Attribute) override;

    bool
    STDMETHODCALLTYPE
    IsPropertySupported(
        _In_ DXCoreAdapterProperty Property) override;

    HRESULT
    STDMETHODCALLTYPE
    GetProperty(
        _In_ DXCoreAdapterProperty Property,
        _In_ size_t BufferSize,
        _Out_writes_bytes_(BufferSize) void* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPropertySize(
        _In_ DXCoreAdapterProperty Property,
        _Out_ size_t* BufferSize) override;

    bool
    STDMETHODCALLTYPE
    IsQueryStateSupported(
        _In_ DXCoreAdapterState State) override;

    HRESULT
    STDMETHODCALLTYPE
    QueryState(
        _In_ DXCoreAdapterState State,
        _In_ size_t InputSize,
        _In_reads_bytes_opt_(InputSize) const void* Input,
        _In_ size_t OutputSize,
        _Out_writes_bytes_(OutputSize) void* Output) override;

    bool
    STDMETHODCALLTYPE
    IsSetStateSupported(
        _In_ DXCoreAdapterState State) override;

    HRESULT
    STDMETHODCALLTYPE
    SetState(
        _In_ DXCoreAdapterState State,
        _In_ size_t InputSize,
        _In_reads_bytes_opt_(InputSize) const void* Input,
        _In_ size_t DataSize,
        _In_reads_bytes_opt_(DataSize) const void* Data) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFactory(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Factory) override;

    /* IDXCoreAdapter1 */
    HRESULT
    STDMETHODCALLTYPE
    GetPropertyWithInput(
        _In_ DXCoreAdapterProperty Property,
        _In_ size_t InputSize,
        _In_reads_bytes_opt_(InputSize) const void* Input,
        _In_ size_t OutputSize,
        _Out_writes_bytes_(OutputSize) void* Output) override;

    /* Fills what the enumeration does not, straight from the kernel */
    VOID
    ReadDriverDetails(
        _In_ ULONG FactoryCapabilities);

    /* Reads a REG_MULTI_SZ value from the adapter's driver key */
    HRESULT
    ReadDriverStrings(
        _In_z_ PCWSTR ValueName,
        _Inout_ std::vector<std::wstring>* Strings);

    LONG
    GetLifeState(VOID) const
    {
        return ReadAcquire(&LifeState);
    }

    VOID
    SetLifeState(
        _In_ LONG NewState)
    {
        WriteRelease(&LifeState, NewState);
    }

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
    Notify(
        _In_ DXCoreNotificationType Type);

    D3DKMT_HANDLE Handle = 0;
    LUID Luid = {};
    DXCoreHardwareIDParts HardwareId = {};
    ULONG Flags = 0;
    std::vector<GUID> Attributes;
    DcAdapterTraits Traits;
    UINT64 DriverVersion = 0;
    UINT ComputePreemption = 0;
    UINT GraphicsPreemption = 0;
    UINT64 DedicatedAdapterMemory = 0;
    UINT64 DedicatedSystemMemory = 0;
    UINT64 SharedSystemMemory = 0;
    UINT KmdModelVersion = 0;
    std::string Description;
    BYTE UniqueGuid[DcUniqueGuidSize] = {};
    UINT HybridMode = 0;
    ULONG FactoryCapabilities = 0;

private:
    ~DcAdapter(VOID);

    /* Known gives whether the property exists, Present whether this adapter has it */
    VOID
    ClassifyProperty(
        _In_ ULONG Property,
        _Out_ BOOLEAN* Known,
        _Out_ BOOLEAN* Present);

    VOID
    ClassifyQueryState(
        _In_ ULONG State,
        _Out_ BOOLEAN* Known,
        _Out_ BOOLEAN* Present);

    VOID
    ClassifySetState(
        _In_ ULONG State,
        _Out_ BOOLEAN* Known,
        _Out_ BOOLEAN* Present);

    HRESULT
    ReadState(
        _In_ ULONG State,
        _In_ size_t InputSize,
        _In_reads_bytes_opt_(InputSize) const void* Input,
        _In_ size_t OutputSize,
        _Out_writes_bytes_(OutputSize) void* Output);

    LONG RefCount = 1;
    LONG LifeState = DcStateNew;
    SRWLOCK NotifyLock = SRWLOCK_INIT;
    std::vector<DcNotification> Notifications;
};

/* The adapter attribute GUIDs the factory knows how to filter on */
BOOLEAN
DcIsEqualGuid(
    _In_ const GUID& Left,
    _In_ const GUID& Right);

BOOLEAN
DcHasGuid(
    _In_ const std::vector<GUID>& List,
    _In_ const GUID& Item);
