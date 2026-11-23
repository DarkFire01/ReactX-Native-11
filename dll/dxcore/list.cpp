/*
 * PROJECT:     ReactX DXCore
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXCoreAdapterList and the orders it can be sorted in
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"

/* Settings record naming the adapter the user picked by hardware id */
struct DC_USER_ADAPTER
{
    UINT VendorId;
    UINT DeviceId;
    UINT SubSystem;
};

static constexpr UINT DcPreferenceUserChosen = 0x40000000;
static constexpr UINT DcSettingUserAdapter = 7;

DcAdapterList::DcAdapterList(VOID)
{
}

DcAdapterList::~DcAdapterList(VOID)
{
    if (Factory != nullptr)
    {
        for (const DcNotification& Entry : Notifications)
            Factory->DropCookie(Entry.Cookie);

        Factory->ForgetList(this);
        Factory->Release();
    }

    for (DcAdapter* Adapter : Adapters)
        Adapter->Release();
}

STDMETHODIMP
DcAdapterList::QueryInterface(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    *Object = nullptr;
    if (DcIsEqualGuid(Riid, __uuidof(IUnknown)) || DcIsEqualGuid(Riid, __uuidof(IDXCoreAdapterList)))
    {
        *Object = static_cast<IDXCoreAdapterList*>(this);
        AddRef();
        return S_OK;
    }

    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
DcAdapterList::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

STDMETHODIMP_(ULONG)
DcAdapterList::Release(VOID)
{
    ULONG Remaining = InterlockedDecrement(&RefCount);

    if (Remaining == 0)
        delete this;

    return Remaining;
}

HRESULT
STDMETHODCALLTYPE
DcAdapterList::GetAdapter(
    _In_ uint32_t Index,
    _In_ REFIID Riid,
    _COM_Outptr_ void** Adapter)
{
    HRESULT hr;

    if (Adapter == nullptr)
        return E_POINTER;

    *Adapter = nullptr;
    AcquireSRWLockShared(&Lock);
    if (Index >= Count)
    {
        ReleaseSRWLockShared(&Lock);
        return E_INVALIDARG;
    }

    if (!Fresh)
    {
        ReleaseSRWLockShared(&Lock);
        return DXGI_ERROR_DEVICE_REMOVED;
    }

    hr = Adapters[Index]->QueryInterface(Riid, Adapter);
    ReleaseSRWLockShared(&Lock);
    return hr;
}

uint32_t
STDMETHODCALLTYPE
DcAdapterList::GetAdapterCount(VOID)
{
    return Count;
}

bool
STDMETHODCALLTYPE
DcAdapterList::IsStale(VOID)
{
    BOOLEAN Stale;

    AcquireSRWLockShared(&Lock);
    Stale = !Fresh;
    ReleaseSRWLockShared(&Lock);
    return Stale != FALSE;
}

HRESULT
STDMETHODCALLTYPE
DcAdapterList::GetFactory(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    HRESULT hr = E_UNEXPECTED;

    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    EnterCriticalSection(&DcGlobalLock);
    if (DcGlobalFactory != nullptr)
        hr = DcGlobalFactory->QueryInterface(Riid, Object);
    LeaveCriticalSection(&DcGlobalLock);
    return hr;
}

bool
STDMETHODCALLTYPE
DcAdapterList::IsAdapterPreferenceSupported(
    _In_ DXCoreAdapterPreference Preference)
{
    return static_cast<ULONG>(Preference) <= static_cast<ULONG>(DXCoreAdapterPreference::HighPerformance);
}

/* Hardware ahead of software, and compute only adapters last among hardware */
static
BOOLEAN
DcIsPlainHardware(
    _In_ const DcAdapter* Adapter)
{
    return Adapter->Traits.IsHardware && (Adapter->Flags & DcFlagComputeOnly) == 0;
}

/* A discrete or detachable GPU, which machine learning lists want first */
static
BOOLEAN
DcIsStrongGpu(
    _In_ const DcAdapter* Adapter)
{
    return Adapter->Traits.Kind == DcKindGpu && (Adapter->Flags & (DcFlagDiscrete | DcFlagDetachable)) != 0;
}

/* Gives -1, 0 or 1 for the side a flag favors, with HasLeft first when Wanted is set */
static
int
DcFavor(
    _In_ BOOLEAN Left,
    _In_ BOOLEAN Right,
    _In_ BOOLEAN Wanted)
{
    if (Left == Right)
        return 0;

    return (Left == Wanted) ? 1 : -1;
}

/* The strict weak order the preferences define; ties fall back to the unique adapter GUID */
class DcPreferenceOrder
{
public:
    DcPreferenceOrder(
        _In_ const DcAdapterList* List,
        _In_ UINT Count,
        _In_reads_(Count) const DXCoreAdapterPreference* Preferences) :
        Owner(List),
        PreferenceCount(Count),
        Wanted(Preferences)
    {
    }

    bool
    operator()(
        _In_ const DcAdapter* Left,
        _In_ const DcAdapter* Right) const
    {
        BOOLEAN Workload = Owner->ForWorkload;
        BOOLEAN UseGuid = !Workload;
        BOOLEAN LeftCompute = (Left->Flags & DcFlagComputeOnly) != 0;
        BOOLEAN RightCompute = (Right->Flags & DcFlagComputeOnly) != 0;
        BOOLEAN SameKind = Left->Traits.Kind == Right->Traits.Kind;
        int Verdict;

        for (UINT Slot = 0; Slot < PreferenceCount; Slot++)
        {
            switch (Wanted[Slot])
            {
                case DXCoreAdapterPreference::Hardware:
                    Verdict = DcFavor(Left->Traits.IsHardware, Right->Traits.IsHardware, TRUE);
                    if (Verdict != 0)
                        return Verdict > 0;
                    if (Workload || !Left->Traits.IsHardware)
                        break;

                    Verdict = DcFavor(LeftCompute, RightCompute, FALSE);
                    if (Verdict != 0)
                        return Verdict > 0;
                    break;

                case DXCoreAdapterPreference::MinimumPower:
                    if (!Workload)
                    {
                        Verdict = DcFavor(LeftCompute, RightCompute, FALSE);
                        if (Verdict != 0)
                            return Verdict > 0;
                    }

                    if ((LeftCompute && RightCompute) || (Workload && !SameKind))
                        break;

                    Verdict = DcFavor((Left->Flags & DcFlagIntegrated) != 0, (Right->Flags & DcFlagIntegrated) != 0, TRUE);
                    if (Verdict != 0)
                        return Verdict > 0;

                    UseGuid = TRUE;
                    break;

                case DXCoreAdapterPreference::HighPerformance:
                    if (!Workload)
                    {
                        Verdict = DcFavor(LeftCompute, RightCompute, FALSE);
                        if (Verdict != 0)
                            return Verdict > 0;
                        if (LeftCompute)
                            break;
                    }

                    if (LeftCompute && RightCompute)
                        break;

                    if (Owner->ForWorkload && Owner->Workload == DXCoreWorkload::MachineLearning)
                    {
                        Verdict = DcFavor(DcIsStrongGpu(Left), DcIsStrongGpu(Right), TRUE);
                        if (Verdict != 0)
                            return Verdict > 0;
                    }

                    if (Workload && !SameKind)
                        break;

                    Verdict = DcFavor((Left->Flags & DcFlagDetachable) != 0, (Right->Flags & DcFlagDetachable) != 0, TRUE);
                    if (Verdict != 0)
                        return Verdict > 0;

                    Verdict = DcFavor((Left->Flags & DcFlagDiscrete) != 0, (Right->Flags & DcFlagDiscrete) != 0, TRUE);
                    if (Verdict != 0)
                        return Verdict > 0;

                    Verdict = DcFavor((Left->Flags & DcFlagIntegrated) != 0, (Right->Flags & DcFlagIntegrated) != 0, FALSE);
                    if (Verdict != 0)
                        return Verdict > 0;

                    UseGuid = TRUE;
                    break;

                default:
                    break;
            }
        }

        if (!UseGuid)
            return false;

        return memcmp(Left->UniqueGuid, Right->UniqueGuid, sizeof(Left->UniqueGuid)) < 0;
    }

private:
    const DcAdapterList* Owner;
    UINT PreferenceCount;
    const DXCoreAdapterPreference* Wanted;
};

VOID
DcAdapterList::SortByPreferences(
    _In_ UINT PreferenceCount,
    _In_reads_(PreferenceCount) const DXCoreAdapterPreference* Preferences)
{
    std::sort(Adapters.begin(), Adapters.end(), DcPreferenceOrder(this, PreferenceCount, Preferences));
}

VOID
DcAdapterList::SortFirstTime(
    _In_ BOOLEAN ForGraphics)
{
    static const DXCoreAdapterPreference Order[] =
    {
        DXCoreAdapterPreference::Hardware,
        DXCoreAdapterPreference::HighPerformance,
    };

    if (Count == 0)
        return;

    SortByPreferences(ForGraphics ? 2 : 1, Order);
    std::sort(Adapters.begin() + 1, Adapters.end(), [](const DcAdapter* Left, const DcAdapter* Right)
    {
        return memcmp(Left->UniqueGuid, Right->UniqueGuid, sizeof(Left->UniqueGuid)) < 0;
    });
    std::stable_partition(Adapters.begin(), Adapters.end(), DcIsPlainHardware);
}

VOID
DcAdapterList::SortForGpuPreference(
    _In_ UINT Preference)
{
    DXCoreAdapterPreference Order[2] = { DXCoreAdapterPreference::Hardware, DXCoreAdapterPreference::Hardware };
    UINT Used = 1;

    if (Preference == 2)
    {
        Order[1] = DXCoreAdapterPreference::HighPerformance;
        Used = 2;
    }
    else if (Preference == 1)
    {
        Order[1] = DXCoreAdapterPreference::MinimumPower;
        Used = 2;
    }

    SortByPreferences(Used, Order);
    std::stable_partition(Adapters.begin(), Adapters.end(), DcIsPlainHardware);
}

VOID
DcAdapterList::SortForUserChosenAdapter(
    _In_ UINT Index)
{
    DC_USER_ADAPTER Chosen = {};

    SortForGpuPreference(2);
    if (Index >= Adapters.size() || FAILED(DcQueryUserSetting(DcSettingUserAdapter, &Chosen, sizeof(Chosen))))
        return;

    for (UINT Slot = 1; Slot < Adapters.size(); Slot++)
    {
        DXCoreHardwareIDParts Parts = {};

        if (FAILED(Adapters[Slot]->GetProperty(DXCoreAdapterProperty::HardwareIDParts, sizeof(Parts), &Parts)))
            continue;

        if (Parts.vendorID != Chosen.VendorId || Parts.deviceID != Chosen.DeviceId ||
            (Parts.subVendorID | (Parts.subSystemID << 16)) != Chosen.SubSystem)
        {
            continue;
        }

        /* Moves the chosen adapter to the front, keeping the rest in order */
        if (Slot < Count)
            std::rotate(Adapters.begin(), Adapters.begin() + Slot, Adapters.begin() + Slot + 1);
        return;
    }
}

/* Rank of each hardware kind for each workload; lower goes first */
static const UINT DcWorkloadRank[4][5] =
{
    { 0xFFFFFFFF, 0, 1, 0xFFFFFFFF, 0xFFFFFFFF },
    { 0xFFFFFFFF, 1, 0, 2, 0xFFFFFFFF },
    { 0xFFFFFFFF, 2, 1, 0xFFFFFFFF, 0 },
    { 0xFFFFFFFF, 2, 1, 0, 0xFFFFFFFF },
};

static const GUID* const DcRuntimeAttributes[] =
{
    &DXCORE_ADAPTER_ATTRIBUTE_D3D11_GRAPHICS,
    &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS,
    &DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE,
    &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML,
    &DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_MEDIA,
};

static
UINT
DcCountRuntimes(
    _In_ DcAdapter* Adapter)
{
    UINT Total = 0;

    for (const GUID* Attribute : DcRuntimeAttributes)
    {
        if (Adapter->IsAttributeSupported(*Attribute))
            Total += 1;
    }

    return Total;
}

VOID
DcAdapterList::SortForWorkload(VOID)
{
    UINT Row = static_cast<UINT>(Workload);

    if (!ForWorkload)
        return;

    std::sort(Adapters.begin(), Adapters.end(), [Row](DcAdapter* Left, DcAdapter* Right)
    {
        UINT LeftRank = DcWorkloadRank[Row][Left->Traits.Kind];
        UINT RightRank = DcWorkloadRank[Row][Right->Traits.Kind];
        UINT LeftCount = DcCountRuntimes(Left);
        UINT RightCount = DcCountRuntimes(Right);

        if (LeftRank != RightRank)
            return LeftRank < RightRank;
        if (LeftCount != RightCount)
            return LeftCount < RightCount;

        return memcmp(Left->UniqueGuid, Right->UniqueGuid, sizeof(Left->UniqueGuid)) < 0;
    });
}

HRESULT
DcAdapterList::QuerySystemPreference(
    _Out_ UINT* Preference,
    _Out_ UINT* Index,
    _Out_ UINT* Factor)
{
    return DcQueryGpuPreference(Adapters.data(), Count, Preference, Index, Factor);
}

HRESULT
STDMETHODCALLTYPE
DcAdapterList::Sort(
    _In_ uint32_t PreferenceCount,
    _In_reads_(PreferenceCount) const DXCoreAdapterPreference* Preferences)
{
    BOOLEAN AskSystem = TRUE;
    UINT Preference = 0;
    UINT Index = 0xFFFFFFFF;
    UINT Factor = 0;

    if (PreferenceCount == 0 || Preferences == nullptr)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);

    /* Only lists sorted by hardware alone follow the system GPU preference */
    for (UINT Slot = 0; Slot < PreferenceCount; Slot++)
    {
        if (Preferences[Slot] == DXCoreAdapterPreference::MinimumPower ||
            Preferences[Slot] == DXCoreAdapterPreference::HighPerformance)
        {
            AskSystem = FALSE;
            break;
        }
    }

    if (AskSystem && SUCCEEDED(QuerySystemPreference(&Preference, &Index, &Factor)) && Preference != 0 && Factor <= 4)
    {
        if (Preference == DcPreferenceUserChosen)
            SortForUserChosenAdapter(Index);
        else
            SortForGpuPreference(Preference);
    }
    else
    {
        SortByPreferences(PreferenceCount, Preferences);
    }

    ReleaseSRWLockExclusive(&Lock);
    return S_OK;
}

HRESULT
DcAdapterList::AddNotification(
    _In_ DXCoreNotificationType Type,
    _In_ PFN_DXCORE_NOTIFICATION_CALLBACK Callback,
    _In_opt_ void* Context,
    _Out_ UINT32* Cookie)
{
    DcNotification Entry;
    HRESULT hr;

    hr = Factory->TakeCookie(FALSE, this, &Entry.Cookie);
    if (FAILED(hr))
        return hr;

    /* The cookie stays taken even when the type is refused */
    if (Type != DXCoreNotificationType::AdapterListStale)
        return E_INVALIDARG;

    Entry.Type = Type;
    Entry.Callback = Callback;
    Entry.Context = Context;

    AcquireSRWLockExclusive(&Lock);
    Notifications.push_back(Entry);
    *Cookie = Entry.Cookie;
    if (!Fresh)
        DcPostNotification(static_cast<IDXCoreAdapterList*>(this), Entry);
    ReleaseSRWLockExclusive(&Lock);
    return S_OK;
}

HRESULT
DcAdapterList::RemoveNotification(
    _In_ UINT32 Cookie)
{
    AcquireSRWLockExclusive(&Lock);
    for (size_t Slot = 0; Slot < Notifications.size(); Slot++)
    {
        if (Notifications[Slot].Cookie == Cookie)
        {
            Notifications.erase(Notifications.begin() + Slot);
            ReleaseSRWLockExclusive(&Lock);
            return S_OK;
        }
    }

    ReleaseSRWLockExclusive(&Lock);
    return E_INVALIDARG;
}

VOID
DcAdapterList::Notify(VOID)
{
    for (const DcNotification& Entry : Notifications)
        DcPostNotification(static_cast<IDXCoreAdapterList*>(this), Entry);
}
