/*
 * PROJECT:     ReactX DXGI runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXGIAdapter built from kernel adapter queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxgiobjects.hpp"

#include <d3d10_1.h>
#include <strsafe.h>

static
NTSTATUS
RxQueryAdapter(
    _In_ D3DKMT_HANDLE KmtAdapter,
    _In_ KMTQUERYADAPTERINFOTYPE Type,
    _Out_writes_bytes_(Size) PVOID Data,
    _In_ UINT Size)
{
    D3DKMT_QUERYADAPTERINFO Query = {};

    Query.hAdapter = KmtAdapter;
    Query.Type = Type;
    Query.pPrivateDriverData = Data;
    Query.PrivateDriverDataSize = Size;
    return RxGetKmtThunks()->QueryAdapterInfo(&Query);
}

/* Older kernels have no version query; the UMD file carries the same number */
static
BOOLEAN
RxReadUmdFileVersion(
    _In_ D3DKMT_HANDLE KmtAdapter,
    _Out_ LARGE_INTEGER* Version)
{
    D3DKMT_UMDFILENAMEINFO Name = {};
    VS_FIXEDFILEINFO* Fixed;
    std::vector<BYTE> Block;
    DWORD Size;
    UINT FixedSize;

    Name.Version = KMTUMDVERSION_DX11;
    if (!NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_UMDRIVERNAME, &Name, sizeof(Name))))
        return FALSE;

    Size = GetFileVersionInfoSizeW(Name.UmdFileName, nullptr);
    if (Size == 0)
        return FALSE;

    Block.resize(Size);
    if (!GetFileVersionInfoW(Name.UmdFileName, 0, Size, Block.data()) ||
        !VerQueryValueW(Block.data(), L"\\", reinterpret_cast<PVOID*>(&Fixed), &FixedSize) ||
        FixedSize < sizeof(*Fixed))
    {
        return FALSE;
    }

    Version->HighPart = (LONG)Fixed->dwFileVersionMS;
    Version->LowPart = Fixed->dwFileVersionLS;
    return TRUE;
}

RxDxgiAdapter::RxDxgiAdapter(
    _In_ RxDxgiFactory* Owner)
    : Factory(Owner)
{
    Factory->AddRef();
}

RxDxgiAdapter::~RxDxgiAdapter()
{
    Factory->Release();
}

HRESULT
RxOpenKernelAdapter(
    _In_ const LUID& Luid,
    _Out_ D3DKMT_HANDLE* Adapter)
{
    static SRWLOCK Lock = SRWLOCK_INIT;
    static std::vector<std::pair<LUID, D3DKMT_HANDLE>> Opened;
    D3DKMT_OPENADAPTERFROMLUID Open = {};
    const KmtThunks* Kmt = RxGetKmtThunks();
    NTSTATUS Status = STATUS_SUCCESS;

    *Adapter = 0;
    if (Kmt == nullptr || Kmt->OpenAdapterFromLuid == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    AcquireSRWLockExclusive(&Lock);
    for (const auto& Each : Opened)
    {
        if (Each.first.LowPart == Luid.LowPart && Each.first.HighPart == Luid.HighPart)
            *Adapter = Each.second;
    }

    if (*Adapter == 0)
    {
        Open.AdapterLuid = Luid;
        Status = Kmt->OpenAdapterFromLuid(&Open);
        if (NT_SUCCESS(Status))
        {
            Opened.push_back({ Luid, Open.hAdapter });
            *Adapter = Open.hAdapter;
        }
    }
    ReleaseSRWLockExclusive(&Lock);

    return NT_SUCCESS(Status) ? S_OK : RxHresultFromStatus(Status);
}

/* Kernel preemption levels count in hundreds from none; DXGI starts at the DMA buffer boundary */
static
UINT
RxPreemptionLevel(
    _In_ UINT Kernel)
{
    return (Kernel <= 100) ? 0 : (Kernel / 100) - 1;
}

HRESULT
RxDxgiAdapter::Create(
    _In_ RxDxgiFactory* Factory,
    _In_ const RxAdapterInfo& Info,
    _Outptr_ RxDxgiAdapter** Adapter)
{
    RxDxgiAdapter* NewAdapter;
    HRESULT hr;

    *Adapter = nullptr;

    NewAdapter = new (std::nothrow) RxDxgiAdapter(Factory);
    if (NewAdapter == nullptr)
        return E_OUTOFMEMORY;

    /* The adapter uses the process wide kernel handle for memory queries */
    hr = RxOpenKernelAdapter(Info.Luid, &NewAdapter->KmtAdapter);
    if (FAILED(hr))
    {
        NewAdapter->Release();
        return hr;
    }

    NewAdapter->Desc.AdapterLuid = Info.Luid;
    NewAdapter->Outputs = Info.Outputs;
    hr = NewAdapter->Describe(Info);
    if (FAILED(hr))
    {
        NewAdapter->Release();
        return hr;
    }

    *Adapter = NewAdapter;
    return S_OK;
}

HRESULT
RxDxgiAdapter::Describe(
    _In_ const RxAdapterInfo& Info)
{
    D3DKMT_ADAPTERREGISTRYINFO Registry;
    D3DKMT_SEGMENTSIZEINFO Segments = {};
    D3DKMT_QUERY_DEVICE_IDS Ids = {};
    D3DKMT_UMD_DRIVER_VERSION Version = {};
    D3DKMT_WDDM_1_2_CAPS Caps = {};
    D3DKMT_DRIVERVERSION Wddm = {};
    D3DKMT_DRIVER_DESCRIPTION* Text;
    NTSTATUS Status;

    /* The memory split is the one query DXGI cannot do without */
    Status = RxQueryAdapter(KmtAdapter, KMTQAITYPE_GETSEGMENTSIZE, &Segments, sizeof(Segments));
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Desc.DedicatedVideoMemory = (SIZE_T)Segments.DedicatedVideoMemorySize;
    Desc.DedicatedSystemMemory = (SIZE_T)Segments.DedicatedSystemMemorySize;
    Desc.SharedSystemMemory = (SIZE_T)Segments.SharedSystemMemorySize;

    /* The description query is newer than the registry strings it replaces */
    Text = new (std::nothrow) D3DKMT_DRIVER_DESCRIPTION;
    if (Text != nullptr &&
        NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_DRIVER_DESCRIPTION, Text, sizeof(*Text))))
    {
        StringCchCopyW(Desc.Description, _countof(Desc.Description), Text->DriverDescription);
    }
    else if (NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_ADAPTERREGISTRYINFO, &Registry, sizeof(Registry))))
    {
        StringCchCopyW(Desc.Description, _countof(Desc.Description), Registry.AdapterString);
    }
    delete Text;

    if (NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Ids, sizeof(Ids))))
    {
        Desc.VendorId = Ids.DeviceIds.VendorID;
        Desc.DeviceId = Ids.DeviceIds.DeviceID;
        Desc.SubSysId = (Ids.DeviceIds.SubSystemID << 16) | Ids.DeviceIds.SubVendorID;
        Desc.Revision = Ids.DeviceIds.RevisionID;
    }

    /* Software means Basic Render that renders without driving a display */
    if (Info.IsBasicRender && Info.Type.RenderSupported && !Info.Type.DisplaySupported)
        Desc.Flags |= DXGI_ADAPTER_FLAG_SOFTWARE;
    if (Info.Type.ACGSupported)
        ExtraFlags |= DXGI_ADAPTER_FLAG3_ACG_COMPATIBLE;

    if (NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_DRIVERVERSION, &Wddm, sizeof(Wddm))))
    {
        ExtraFlags |= (Wddm >= KMT_DRIVERVERSION_WDDM_2_0) ? DXGI_ADAPTER_FLAG3_SUPPORT_MONITORED_FENCES
                                                           : DXGI_ADAPTER_FLAG3_SUPPORT_NON_MONITORED_FENCES;
        if (Wddm >= KMT_DRIVERVERSION_WDDM_1_3)
            ExtraFlags |= DXGI_ADAPTER_FLAG3_KEYED_MUTEX_CONFORMANCE;
    }

    if (NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_WDDM_1_2_CAPS, &Caps, sizeof(Caps))))
    {
        GraphicsPreemption = (DXGI_GRAPHICS_PREEMPTION_GRANULARITY)RxPreemptionLevel(
            Caps.PreemptionCaps.GraphicsPreemptionGranularity);
        ComputePreemption = (DXGI_COMPUTE_PREEMPTION_GRANULARITY)RxPreemptionLevel(
            Caps.PreemptionCaps.ComputePreemptionGranularity);
    }

    if (NT_SUCCESS(RxQueryAdapter(KmtAdapter, KMTQAITYPE_UMD_DRIVER_VERSION, &Version, sizeof(Version))))
    {
        UmdVersion = Version.DriverVersion;
        HasUmdVersion = TRUE;
    }
    else
    {
        HasUmdVersion = RxReadUmdFileVersion(KmtAdapter, &UmdVersion);
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIAdapter) ||
        riid == __uuidof(IDXGIAdapter1) || riid == __uuidof(IDXGIAdapter2) || riid == __uuidof(IDXGIAdapter3) ||
        riid == __uuidof(IDXGIAdapter4))
    {
        *ppvObject = static_cast<IDXGIAdapter4*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxDxgiAdapter::AddRef(VOID)
{
    return InterlockedIncrement(&RefCount);
}

ULONG
STDMETHODCALLTYPE
RxDxgiAdapter::Release(VOID)
{
    ULONG Count = InterlockedDecrement(&RefCount);

    if (Count == 0)
        delete this;
    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return PrivateData.Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return PrivateData.Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return PrivateData.Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    if (ppParent == nullptr)
        return E_INVALIDARG;

    return Factory->QueryInterface(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::EnumOutputs(
    _In_ UINT Output,
    _COM_Outptr_ IDXGIOutput** ppOutput)
{
    RxDxgiOutput* Created;
    HRESULT hr;

    if (ppOutput == nullptr)
        return E_INVALIDARG;
    *ppOutput = nullptr;

    if (Output >= Outputs.size())
        return DXGI_ERROR_NOT_FOUND;

    hr = RxDxgiOutput::Create(this, Outputs[Output], &Created);
    if (SUCCEEDED(hr))
        *ppOutput = Created;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetDesc(
    _Out_ DXGI_ADAPTER_DESC* pDesc)
{
    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* The first desc is the second one without its flags */
    static_assert(offsetof(DXGI_ADAPTER_DESC1, Flags) == sizeof(DXGI_ADAPTER_DESC));
    memcpy(pDesc, &Desc, sizeof(*pDesc));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::CheckInterfaceSupport(
    _In_ REFGUID InterfaceName,
    _Out_ LARGE_INTEGER* pUMDVersion)
{
    /* Only the device interfaces DXGI itself can create count */
    if (InterfaceName != __uuidof(IDXGIDevice) && InterfaceName != __uuidof(ID3D10Device) &&
        InterfaceName != __uuidof(ID3D10Device1))
    {
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (!HasUmdVersion)
        return DXGI_ERROR_UNSUPPORTED;

    if (pUMDVersion != nullptr)
        *pUMDVersion = UmdVersion;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetDesc1(
    _Out_ DXGI_ADAPTER_DESC1* pDesc)
{
    if (pDesc == nullptr)
        return E_INVALIDARG;

    *pDesc = Desc;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetDesc3(
    _Out_ DXGI_ADAPTER_DESC3* pDesc)
{
    if (pDesc == nullptr)
        return E_INVALIDARG;

    /* The second and third descs lay the first one's fields out the same way */
    *pDesc = {};
    memcpy(pDesc, &Desc, offsetof(DXGI_ADAPTER_DESC1, Flags));
    pDesc->Flags = (DXGI_ADAPTER_FLAG3)(Desc.Flags | ExtraFlags);
    pDesc->GraphicsPreemptionGranularity = GraphicsPreemption;
    pDesc->ComputePreemptionGranularity = ComputePreemption;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::GetDesc2(
    _Out_ DXGI_ADAPTER_DESC2* pDesc)
{
    DXGI_ADAPTER_DESC3 Full;

    if (pDesc == nullptr)
        return E_INVALIDARG;

    static_assert(offsetof(DXGI_ADAPTER_DESC2, Flags) == offsetof(DXGI_ADAPTER_DESC3, Flags));
    GetDesc3(&Full);
    memcpy(pDesc, &Full, offsetof(DXGI_ADAPTER_DESC2, Flags));

    /* Only the remote and software bits existed when this desc was defined */
    pDesc->Flags = Full.Flags & (DXGI_ADAPTER_FLAG_REMOTE | DXGI_ADAPTER_FLAG_SOFTWARE);
    pDesc->GraphicsPreemptionGranularity = Full.GraphicsPreemptionGranularity;
    pDesc->ComputePreemptionGranularity = Full.ComputePreemptionGranularity;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::QueryVideoMemoryInfo(
    _In_ UINT NodeIndex,
    _In_ DXGI_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
    _Out_ DXGI_QUERY_VIDEO_MEMORY_INFO* pVideoMemoryInfo)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYVIDEOMEMORYINFO Query = {};
    NTSTATUS Status;

    if (Kmt->QueryVideoMemoryInfo == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The kernel checks the node and group; DXGI passes them straight through */
    Query.hAdapter = KmtAdapter;
    Query.MemorySegmentGroup = (D3DKMT_MEMORY_SEGMENT_GROUP)MemorySegmentGroup;
    Query.PhysicalAdapterIndex = NodeIndex;
    Status = Kmt->QueryVideoMemoryInfo(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    pVideoMemoryInfo->Budget = Query.Budget;
    pVideoMemoryInfo->CurrentUsage = Query.CurrentUsage;
    pVideoMemoryInfo->AvailableForReservation = Query.AvailableForReservation;
    pVideoMemoryInfo->CurrentReservation = Query.CurrentReservation;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::SetVideoMemoryReservation(
    _In_ UINT NodeIndex,
    _In_ DXGI_MEMORY_SEGMENT_GROUP MemorySegmentGroup,
    _In_ UINT64 Reservation)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CHANGEVIDEOMEMORYRESERVATION Change = {};

    if (Kmt->ChangeVideoMemoryReservation == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    Change.hAdapter = KmtAdapter;
    Change.MemorySegmentGroup = (D3DKMT_MEMORY_SEGMENT_GROUP)MemorySegmentGroup;
    Change.Reservation = Reservation;
    Change.PhysicalAdapterIndex = NodeIndex;
    return RxHresultFromStatus(Kmt->ChangeVideoMemoryReservation(&Change));
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::RegisterVideoMemoryBudgetChangeNotificationEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    return BudgetEvents.AddEvent(hEvent, pdwCookie, TRUE);
}

VOID
STDMETHODCALLTYPE
RxDxgiAdapter::UnregisterVideoMemoryBudgetChangeNotification(
    _In_ DWORD dwCookie)
{
    BudgetEvents.Remove(dwCookie);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiAdapter::RegisterHardwareContentProtectionTeardownStatusEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    return TeardownEvents.AddEvent(hEvent, pdwCookie, FALSE);
}

VOID
STDMETHODCALLTYPE
RxDxgiAdapter::UnregisterHardwareContentProtectionTeardownStatus(
    _In_ DWORD dwCookie)
{
    TeardownEvents.Remove(dwCookie);
}
