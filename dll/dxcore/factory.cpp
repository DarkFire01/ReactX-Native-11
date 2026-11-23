/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXCoreAdapterFactory2: lists, lookups and notification cookies
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

#include <cfgmgr32.h>

/* Kernel feature numbers the factory asks about */
static constexpr UINT DcKernelFeatureStatistics = 0x10000000;
static constexpr UINT DcKernelFeatureCarveout = 0x1000000B;

/* The kernel property block that reports reserved accelerator memory */
struct DC_GET_PROPERTIES
{
    UINT PropertyId;
    UINT DataSize;
    UINT64 Reserved0;
    void* Data;
    UINT64 Reserved1;
    UINT64 Reserved2;
};

static const GUID DcComputeDeviceArrival = {0x1024e4c9, 0x47c9, 0x48d3, {0xb4, 0xa8, 0xf9, 0xdf, 0x78, 0x52, 0x3b, 0x53}};

DcFactory::DcFactory(VOID)
{
    Watches[0] = { 0x41C61629A3BC3875ull, 0, nullptr, DXCoreNotificationType::AdapterBudgetChange };
    Watches[1] = { 0x41C61629A3BC4075ull, 0, nullptr, DXCoreNotificationType::AdapterHardwareContentProtectionTeardown };
}

DcFactory::~DcFactory(VOID)
{
    typedef CONFIGRET (WINAPI* PFN_DC_CM_UNREGISTER)(HCMNOTIFICATION);

    if (Monitor != nullptr)
    {
        PFN_DC_CM_UNREGISTER Unregister = reinterpret_cast<PFN_DC_CM_UNREGISTER>(
            GetProcAddress(GetModuleHandleW(L"cfgmgr32.dll"), "CM_Unregister_Notification"));

        if (Unregister != nullptr)
            Unregister(static_cast<HCMNOTIFICATION>(Monitor));
    }

    if (DcGlobalFactory == this)
        DcGlobalFactory = nullptr;

    /* Highest LUID first, the order the system tears its adapter map down in */
    for (size_t Remaining = Adapters.size(); Remaining > 0; Remaining -= 1)
    {
        size_t Index = Remaining - 1;

        Adapters[Index]->Release();
    }
}

static
BOOLEAN
DcAskKernelFeature(
    _In_ UINT FeatureId)
{
    D3DKMT_ISFEATUREENABLED Feature = {};

    Feature.FeatureId = static_cast<DXGK_FEATURE_ID>(FeatureId);
    return NT_SUCCESS(DcKmtCall(DcKmt()->IsFeatureEnabled, &Feature)) && Feature.Result.Enabled;
}

static
DWORD
CALLBACK
DcDeviceArrived(
    _In_ HCMNOTIFICATION Notification,
    _In_opt_ PVOID Context,
    _In_ CM_NOTIFY_ACTION Action,
    _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
    _In_ DWORD EventDataSize)
{
    static_cast<DcFactory*>(Context)->AdaptersChanged();
    return ERROR_SUCCESS;
}

HRESULT
DcFactory::StartMonitoring(VOID)
{
    typedef CONFIGRET (WINAPI* PFN_DC_CM_REGISTER)(PCM_NOTIFY_FILTER, PVOID, PCM_NOTIFY_CALLBACK, PHCMNOTIFICATION);
    typedef DWORD (WINAPI* PFN_DC_CM_MAP)(CONFIGRET, DWORD);
    HMODULE Module = LoadLibraryExW(L"cfgmgr32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    PFN_DC_CM_REGISTER Register;
    PFN_DC_CM_MAP Map;
    CM_NOTIFY_FILTER Filter = {};
    HCMNOTIFICATION Handle = nullptr;
    CONFIGRET Result;
    DWORD Error;

    if (Module == NULL)
        return S_OK;

    Register = reinterpret_cast<PFN_DC_CM_REGISTER>(GetProcAddress(Module, "CM_Register_Notification"));
    Map = reinterpret_cast<PFN_DC_CM_MAP>(GetProcAddress(Module, "CM_MapCrToWin32Err"));
    if (Register == nullptr || Map == nullptr)
    {
        /* Releases without device interface notifications simply go unwatched */
        return S_OK;
    }

    Filter.cbSize = sizeof(Filter);
    Filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    Filter.u.DeviceInterface.ClassGuid = DcComputeDeviceArrival;
    Result = Register(&Filter, this, DcDeviceArrived, &Handle);
    Monitor = Handle;

    Error = Map(Result, ERROR_GEN_FAILURE);
    if (static_cast<LONG>(Error) > 0)
        return HRESULT_FROM_WIN32(Error);

    return static_cast<HRESULT>(Error);
}

HRESULT
DcFactory::Initialize(VOID)
{
    HRESULT hr;

    AcquireSRWLockExclusive(&Lock);

    if (DcAskKernelFeature(DcKernelFeatureStatistics))
        Capabilities |= DcFactoryEngineStatistics;
    if (DcIsSystemFeatureEnabled(DcFeatureCarveoutApi) && DcAskKernelFeature(DcKernelFeatureCarveout))
        Capabilities |= DcFactoryCarveout;

    hr = StartMonitoring();
    if (FAILED(hr))
    {
        BOOL InAppContainer = FALSE;
        DWORD Returned = 0;

        DcTrace(L"adapter arrival monitoring did not start (0x%08lX)", hr);

        /* Only app containers may go on without it */
        if (!DcIsSystemFeatureEnabled(DcFeatureNonFatalMonitoring) ||
            !GetTokenInformation(GetCurrentProcessToken(), TokenIsAppContainer, &InAppContainer,
                                 sizeof(InAppContainer), &Returned) ||
            !InAppContainer)
        {
            ReleaseSRWLockExclusive(&Lock);
            return hr;
        }
    }

    hr = Enumerate(TRUE);
    ReleaseSRWLockExclusive(&Lock);
    return hr;
}

STDMETHODIMP
DcFactory::QueryInterface(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    *Object = nullptr;
    if (DcIsEqualGuid(Riid, __uuidof(IUnknown)) || DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapterFactory2)) ||
        DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapterFactory1)) || DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapterFactory)))
    {
        *Object = static_cast<IDXCoreAdapterFactory2*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
DcFactory::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

STDMETHODIMP_(ULONG)
DcFactory::Release(VOID)
{
    ULONG Count;

    for (;;)
    {
        LONG Seen = ReadAcquire(&RefCount);

        if (Seen == 1)
            break;
        if (InterlockedCompareExchange(&RefCount, Seen - 1, Seen) == Seen)
            return Seen - 1;
    }

    /* The last reference goes under the global lock so a new factory cannot find this one */
    EnterCriticalSection(&DcGlobalLock);
    Count = InterlockedDecrement(&RefCount);
    if (Count == 0)
        delete this;
    LeaveCriticalSection(&DcGlobalLock);
    return Count;
}

VOID
DcFactory::FillList(
    _Inout_ DcAdapterList* List,
    _In_ BOOLEAN NeedsEveryAttribute)
{
    for (DcAdapter* Adapter : Adapters)
    {
        BOOLEAN Matches;

        if (!Adapter->IsValid() || (Adapter->Flags & (DcFlagRender | DcFlagComputeOnly)) == 0)
            continue;

        if (NeedsEveryAttribute)
        {
            Matches = TRUE;
            for (const GUID& Wanted : List->Required)
                Matches = Matches && DcHasGuid(Adapter->Attributes, Wanted);
        }
        else
        {
            Matches = FALSE;
            for (const GUID& Wanted : List->Required)
                Matches = Matches || DcHasGuid(Adapter->Attributes, Wanted);
        }

        if (Matches && !List->HardwareTypes.empty())
        {
            Matches = FALSE;
            for (const GUID& Wanted : List->HardwareTypes)
                Matches = Matches || DcHasGuid(Adapter->Attributes, Wanted);
        }

        if (!Matches)
            continue;

        Adapter->AddRef();
        List->Adapters.push_back(Adapter);
    }

    List->Count = static_cast<UINT>(List->Adapters.size());
}

HRESULT
DcFactory::BuildList(
    _In_ UINT AttributeCount,
    _In_reads_opt_(AttributeCount) const GUID* Attributes,
    _In_ REFIID Riid,
    _COM_Outptr_ void** List)
{
    DcAdapterList* Created;
    BOOLEAN ForGraphics = FALSE;
    UINT Preference = 0x80000000;
    UINT Index = 0xFFFFFFFF;
    UINT Factor = 0;
    HRESULT hr;

    if (List == nullptr)
        return E_POINTER;

    *List = nullptr;
    if (Attributes == nullptr || AttributeCount == 0)
        return E_INVALIDARG;

    Created = new (std::nothrow) DcAdapterList();
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Slot = 0; Slot < AttributeCount; Slot++)
    {
        Created->Required.push_back(Attributes[Slot]);
        if (DcIsEqualGuid(Attributes[Slot], DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS) ||
            DcIsEqualGuid(Attributes[Slot], DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS))
        {
            ForGraphics = TRUE;
        }
    }

    AcquireSRWLockExclusive(&Lock);
    AcquireSRWLockExclusive(&Created->Lock);

    FillList(Created, TRUE);
    if (!ForGraphics)
    {
        Created->SortFirstTime(FALSE);
    }
    else if (FAILED(Created->QuerySystemPreference(&Preference, &Index, &Factor)))
    {
        Created->SortFirstTime(TRUE);
    }
    else if (Preference != 0 && Index <= Created->Adapters.size())
    {
        if (Preference == 0x40000000)
            Created->SortForUserChosenAdapter(Index);
        else
            Created->SortForGpuPreference(Preference);
    }
    else
    {
        Created->SortForGpuPreference(2);
    }

    Lists.push_back(Created);
    AddRef();
    Created->Factory = this;

    hr = Created->QueryInterface(Riid, List);
    ReleaseSRWLockExclusive(&Created->Lock);
    ReleaseSRWLockExclusive(&Lock);
    Created->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DcFactory::CreateAdapterList(
    _In_ uint32_t AttributeCount,
    _In_reads_(AttributeCount) const GUID* Attributes,
    _In_ REFIID Riid,
    _COM_Outptr_ void** List)
{
    return BuildList(AttributeCount, Attributes, Riid, List);
}

HRESULT
STDMETHODCALLTYPE
DcFactory::CreateAdapterListByWorkload(
    _In_ DXCoreWorkload Workload,
    _In_ DXCoreRuntimeFilterFlags RuntimeFilter,
    _In_ DXCoreHardwareTypeFilterFlags HardwareTypeFilter,
    _In_ REFIID Riid,
    _COM_Outptr_ void** List)
{
    static const DXCoreAdapterPreference Order[] =
    {
        DXCoreAdapterPreference::Hardware,
        DXCoreAdapterPreference::HighPerformance,
    };
    ULONG Runtimes = static_cast<ULONG>(RuntimeFilter);
    ULONG Kinds = static_cast<ULONG>(HardwareTypeFilter);
    BOOLEAN WantsD3D12 = Runtimes == 0 || (Runtimes & static_cast<ULONG>(DXCoreRuntimeFilterFlags::D3D12)) != 0;
    DcAdapterList* Created;
    UINT Preference = 0x80000000;
    UINT Index = 0xFFFFFFFF;
    UINT Factor = 0;
    HRESULT hr;

    if (List == nullptr)
        return E_POINTER;

    *List = nullptr;
    Created = new (std::nothrow) DcAdapterList();
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    Created->ForWorkload = TRUE;
    Created->Workload = Workload;

    switch (Workload)
    {
        case DXCoreWorkload::Graphics:
            if (Runtimes == 0 || (Runtimes & static_cast<ULONG>(DXCoreRuntimeFilterFlags::D3D11)) != 0)
                Created->Required.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS);
            if (WantsD3D12)
                Created->Required.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
            break;

        case DXCoreWorkload::Compute:
            if (WantsD3D12)
                Created->Required.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
            break;

        case DXCoreWorkload::Media:
            if (WantsD3D12)
                Created->Required.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA);
            break;

        case DXCoreWorkload::MachineLearning:
            if (WantsD3D12)
                Created->Required.push_back(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML);
            break;

        default:
            Created->Release();
            return E_INVALIDARG;
    }

    if (Kinds & static_cast<ULONG>(DXCoreHardwareTypeFilterFlags::GPU))
        Created->HardwareTypes.push_back(DXCORE_HARDWARE_TYPE_ATTRIBUTE_GPU);
    if (Kinds & static_cast<ULONG>(DXCoreHardwareTypeFilterFlags::NPU))
        Created->HardwareTypes.push_back(DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU);
    if (Kinds & static_cast<ULONG>(DXCoreHardwareTypeFilterFlags::ComputeAccelerator))
        Created->HardwareTypes.push_back(DXCORE_HARDWARE_TYPE_ATTRIBUTE_COMPUTE_ACCELERATOR);
    if (Kinds & static_cast<ULONG>(DXCoreHardwareTypeFilterFlags::MediaAccelerator))
        Created->HardwareTypes.push_back(DXCORE_HARDWARE_TYPE_ATTRIBUTE_MEDIA_ACCELERATOR);

    AcquireSRWLockExclusive(&Lock);
    AcquireSRWLockExclusive(&Created->Lock);

    FillList(Created, FALSE);
    Created->SortForWorkload();

    if (Workload != DXCoreWorkload::Graphics)
    {
        Created->SortByPreferences(1, Order);
    }
    else if (SUCCEEDED(Created->QuerySystemPreference(&Preference, &Index, &Factor)) && Preference != 0 &&
             Index < Created->Adapters.size())
    {
        if (Preference == 0x40000000)
            Created->SortForUserChosenAdapter(Index);
        else
            Created->SortForGpuPreference(Preference);
    }
    else
    {
        Created->SortByPreferences(2, Order);
    }

    Lists.push_back(Created);
    AddRef();
    Created->Factory = this;

    hr = Created->QueryInterface(Riid, List);
    ReleaseSRWLockExclusive(&Created->Lock);
    ReleaseSRWLockExclusive(&Lock);
    Created->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DcFactory::GetAdapterByLuid(
    _In_ const LUID& AdapterLuid,
    _In_ REFIID Riid,
    _COM_Outptr_ void** Adapter)
{
    HRESULT hr = E_INVALIDARG;

    if (Adapter == nullptr)
        return E_POINTER;

    *Adapter = nullptr;
    AcquireSRWLockShared(&Lock);
    for (DcAdapter* Candidate : Adapters)
    {
        if (Candidate->Luid.LowPart != AdapterLuid.LowPart || Candidate->Luid.HighPart != AdapterLuid.HighPart)
            continue;

        if ((Candidate->Flags & (DcFlagRender | DcFlagComputeOnly)) == 0 && !DcThreadAllowsAnyAdapter())
            hr = E_INVALIDARG;
        else if (!Candidate->IsValid())
            hr = DXGI_ERROR_DEVICE_REMOVED;
        else
            hr = Candidate->QueryInterface(Riid, Adapter);
        break;
    }

    ReleaseSRWLockShared(&Lock);
    return hr;
}

bool
STDMETHODCALLTYPE
DcFactory::IsNotificationTypeSupported(
    _In_ DXCoreNotificationType Type)
{
    return static_cast<ULONG>(Type) <= static_cast<ULONG>(DXCoreNotificationType::AdapterHardwareContentProtectionTeardown);
}

HRESULT
STDMETHODCALLTYPE
DcFactory::RegisterEventNotification(
    _In_ IUnknown* Object,
    _In_ DXCoreNotificationType Type,
    _In_ PFN_DXCORE_NOTIFICATION_CALLBACK Callback,
    _In_opt_ void* Context,
    _Out_ uint32_t* Cookie)
{
    IDXCoreAdapterList* List = nullptr;
    IDXCoreAdapter* Adapter = nullptr;
    HRESULT hr;

    if (Cookie == nullptr)
        return E_POINTER;

    *Cookie = 0;
    if (Object == nullptr)
        return E_INVALIDARG;
    if (Callback == nullptr)
        return E_POINTER;
    if (!IsNotificationTypeSupported(Type))
        return DXGI_ERROR_INVALID_CALL;

    if (SUCCEEDED(Object->QueryInterface(__uuidof(IDXCoreAdapterList), reinterpret_cast<void**>(&List))))
    {
        hr = static_cast<DcAdapterList*>(List)->AddNotification(Type, Callback, Context, Cookie);
        List->Release();
        return hr;
    }

    if (SUCCEEDED(Object->QueryInterface(__uuidof(IDXCoreAdapter), reinterpret_cast<void**>(&Adapter))))
    {
        hr = static_cast<DcAdapter*>(static_cast<IDXCoreAdapter1*>(Adapter))->AddNotification(Type, Callback,
                                                                                               Context, Cookie);
        Adapter->Release();
        return hr;
    }

    return E_INVALIDARG;
}

HRESULT
STDMETHODCALLTYPE
DcFactory::UnregisterEventNotification(
    _In_ uint32_t Cookie)
{
    HRESULT hr = E_INVALIDARG;

    AcquireSRWLockExclusive(&CookieLock);
    for (size_t Slot = 0; Slot < Cookies.size(); Slot++)
    {
        DcCookieOwner Entry = Cookies[Slot];

        if (Entry.Cookie != Cookie)
            continue;

        if (Entry.ForAdapter)
            hr = static_cast<DcAdapter*>(Entry.Owner)->RemoveNotification(Cookie);
        else
            hr = static_cast<DcAdapterList*>(Entry.Owner)->RemoveNotification(Cookie);

        Cookies.erase(Cookies.begin() + Slot);
        break;
    }

    ReleaseSRWLockExclusive(&CookieLock);
    return hr;
}

HRESULT
STDMETHODCALLTYPE
DcFactory::GetReservedAcceleratorMemoryInfo(
    _Out_ DXCoreReservedAcceleratorMemoryInfo* Info)
{
    DC_GET_PROPERTIES Request = {};
    UINT64 Data[6] = {};
    NTSTATUS Status;

    if (!DcIsSystemFeatureEnabled(DcFeatureCarveoutApi))
        return E_NOTIMPL;
    if ((Capabilities & DcFactoryCarveout) == 0)
        return DXGI_ERROR_UNSUPPORTED;
    if (Info == nullptr)
        return E_POINTER;

    Request.PropertyId = 6;
    Request.DataSize = sizeof(Data);
    Request.Data = Data;
    Status = DcKmtCall(DcKmt()->GetProperties, &Request);
    if (!NT_SUCCESS(Status))
        return DcStatusHresult(Status);

    Info->maximumSize = Data[0];
    Info->recommendedSize = Data[1];
    Info->currentSize = Data[2];
    Info->userConfiguredSize = Data[3];
    Info->currentCacheSize = Data[4];
    Info->flags = (Data[5] & 1) ? DXCoreReservedAcceleratorMemoryFlags::UserConfigured
                                : DXCoreReservedAcceleratorMemoryFlags::None;
    return S_OK;
}

HRESULT
DcFactory::TakeCookie(
    _In_ BOOLEAN ForAdapter,
    _In_ void* Owner,
    _Out_ UINT32* Cookie)
{
    UINT32 Candidate = 0;
    BOOLEAN Found = FALSE;

    AcquireSRWLockExclusive(&CookieLock);

    /* Search up from the last cookie handed out, then wrap around to one */
    for (UINT Pass = 0; Pass < 2 && !Found; Pass++)
    {
        for (Candidate = (Pass == 0) ? NextCookie : 1; Candidate != 0xFFFFFFFF; Candidate++)
        {
            BOOLEAN Taken = FALSE;

            for (const DcCookieOwner& Entry : Cookies)
                Taken = Taken || Entry.Cookie == Candidate;

            if (!Taken)
            {
                Found = TRUE;
                break;
            }
        }
    }

    if (!Found)
    {
        ReleaseSRWLockExclusive(&CookieLock);
        return E_OUTOFMEMORY;
    }

    Cookies.push_back({ Candidate, ForAdapter, Owner });
    *Cookie = Candidate;
    NextCookie = (Candidate == 0xFFFFFFFE) ? 1 : Candidate + 1;
    ReleaseSRWLockExclusive(&CookieLock);
    return S_OK;
}

VOID
DcFactory::DropCookie(
    _In_ UINT32 Cookie)
{
    AcquireSRWLockExclusive(&CookieLock);
    for (size_t Slot = 0; Slot < Cookies.size(); Slot++)
    {
        if (Cookies[Slot].Cookie == Cookie)
        {
            Cookies.erase(Cookies.begin() + Slot);
            break;
        }
    }
    ReleaseSRWLockExclusive(&CookieLock);
}

VOID
DcFactory::ForgetList(
    _In_ DcAdapterList* List)
{
    AcquireSRWLockExclusive(&Lock);
    for (size_t Slot = 0; Slot < Lists.size(); Slot++)
    {
        if (Lists[Slot] == List)
        {
            Lists.erase(Lists.begin() + Slot);
            break;
        }
    }
    ReleaseSRWLockExclusive(&Lock);
}

VOID
DcFactory::AdaptersChanged(VOID)
{
    std::vector<DcAdapter*> Gone;
    std::vector<DcAdapter*> Arrived;

    AcquireSRWLockExclusive(&Lock);

    for (DcAdapter* Adapter : Adapters)
        Adapter->SetLifeState(DcStateRecheck);

    Enumerate(FALSE);

    for (DcAdapter* Adapter : Adapters)
    {
        if (Adapter->GetLifeState() == DcStateRecheck)
        {
            Adapter->SetLifeState(DcStateRemoved);
            Gone.push_back(Adapter);
            DcTrace(L"adapter %08lX:%08lX went away", Adapter->Luid.HighPart, Adapter->Luid.LowPart);
        }
        else if (Adapter->GetLifeState() == DcStateNew)
        {
            Adapter->SetLifeState(DcStateValid);
            Arrived.push_back(Adapter);
            DcTrace(L"adapter %08lX:%08lX arrived", Adapter->Luid.HighPart, Adapter->Luid.LowPart);
        }
    }

    for (DcAdapterList* List : Lists)
    {
        BOOLEAN Stale = FALSE;

        AcquireSRWLockExclusive(&List->Lock);
        if (!List->Fresh)
        {
            ReleaseSRWLockExclusive(&List->Lock);
            continue;
        }

        for (DcAdapter* Adapter : Arrived)
        {
            BOOLEAN Matches = List->NeedsEveryAttribute;
            BOOLEAN KindMatches = List->HardwareTypes.empty();

            for (const GUID& Wanted : List->Required)
            {
                if (List->NeedsEveryAttribute)
                    Matches = Matches && DcHasGuid(Adapter->Attributes, Wanted);
                else
                    Matches = Matches || DcHasGuid(Adapter->Attributes, Wanted);
            }

            for (const GUID& Wanted : List->HardwareTypes)
                KindMatches = KindMatches || DcHasGuid(Adapter->Attributes, Wanted);

            if (Matches && KindMatches)
            {
                Stale = TRUE;
                break;
            }
        }

        for (size_t Slot = 0; !Stale && Slot < List->Adapters.size(); Slot++)
            Stale = List->Adapters[Slot]->GetLifeState() == DcStateRemoved;

        if (Stale)
        {
            List->Fresh = FALSE;
            List->Notify();
        }
        ReleaseSRWLockExclusive(&List->Lock);
    }

    for (DcAdapter* Adapter : Gone)
        Adapter->Notify(DXCoreNotificationType::AdapterNoLongerValid);

    ReleaseSRWLockExclusive(&Lock);
}
