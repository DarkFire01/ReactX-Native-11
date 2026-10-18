/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDXGIDevice face of the device and the state behind it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <algorithm>

/* IDXGIDevice1 documents a default of three queued frames and a ceiling of sixteen */
static constexpr UINT RxDefaultFrameLatency = 3;
static constexpr UINT RxMaximumFrameLatency = 16;

typedef HRESULT (WINAPI* PFN_RX_CREATE_FACTORY)(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppFactory);

/* The ReactX dxgi.dll beside this module; swapchains and adapters come from there */
static
PFN_RX_CREATE_FACTORY
RxGetFactoryRoutine(VOID)
{
    static PFN_RX_CREATE_FACTORY Routine = []() -> PFN_RX_CREATE_FACTORY
    {
        WCHAR Path[MAX_PATH];
        HMODULE Self;
        HMODULE Dxgi;
        PWSTR Slash;

        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&RxGetFactoryRoutine), &Self) ||
            GetModuleFileNameW(Self, Path, _countof(Path)) == 0)
        {
            return nullptr;
        }

        Slash = wcsrchr(Path, L'\\');
        if (Slash == nullptr || wcscpy_s(Slash + 1, _countof(Path) - (Slash + 1 - Path), L"dxgi.dll") != 0)
            return nullptr;

        Dxgi = LoadLibraryExW(Path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (Dxgi == NULL)
        {
            RX_TRACE(L"no ReactX dxgi.dll at %s", Path);
            return nullptr;
        }

        return reinterpret_cast<PFN_RX_CREATE_FACTORY>(GetProcAddress(Dxgi, "CreateDXGIFactory1"));
    }();

    return Routine;
}

VOID
RxDevice::SetDxgiAdapter(
    _In_ IDXGIAdapter* Reported)
{
    IDXGIAdapter1* Adapter1;

    if (FAILED(Reported->QueryInterface(IID_PPV_ARGS(&Adapter1))))
        return;

    AcquireSRWLockExclusive(&DxgiLock);
    if (DxgiAdapter != nullptr)
        DxgiAdapter->Release();
    DxgiAdapter = Adapter1;
    ReleaseSRWLockExclusive(&DxgiLock);
}

HRESULT
RxDevice::GetDxgiAdapter(
    _COM_Outptr_ IDXGIAdapter1** Result)
{
    PFN_RX_CREATE_FACTORY CreateFactory;
    IDXGIFactory1* Factory = nullptr;
    IDXGIAdapter1* Candidate;
    DXGI_ADAPTER_DESC1 Desc;
    HRESULT hr = S_OK;
    UINT Index;

    *Result = nullptr;

    AcquireSRWLockExclusive(&DxgiLock);
    if (DxgiAdapter == nullptr)
    {
        /* A device made without an adapter reports the one it runs on, found by LUID */
        CreateFactory = RxGetFactoryRoutine();
        hr = CreateFactory ? CreateFactory(IID_PPV_ARGS(&Factory)) : DXGI_ERROR_UNSUPPORTED;
        for (Index = 0; SUCCEEDED(hr) && DxgiAdapter == nullptr; Index++)
        {
            hr = Factory->EnumAdapters1(Index, &Candidate);
            if (FAILED(hr))
                break;

            if (SUCCEEDED(Candidate->GetDesc1(&Desc)) &&
                Desc.AdapterLuid.LowPart == GetAdapter().GetLuid().LowPart &&
                Desc.AdapterLuid.HighPart == GetAdapter().GetLuid().HighPart)
            {
                DxgiAdapter = Candidate;
            }
            else
            {
                Candidate->Release();
            }
        }

        if (Factory != nullptr)
            Factory->Release();
        if (DxgiAdapter == nullptr && SUCCEEDED(hr))
            hr = DXGI_ERROR_NOT_FOUND;
    }

    if (DxgiAdapter != nullptr)
    {
        DxgiAdapter->AddRef();
        *Result = DxgiAdapter;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&DxgiLock);

    return hr;
}

VOID
RxDevice::TrackContext(
    _In_ D3DKMT_HANDLE KmContext)
{
    AcquireSRWLockExclusive(&ContextLock);
    KmContexts.push_back(KmContext);
    ReleaseSRWLockExclusive(&ContextLock);
}

VOID
RxDevice::UntrackContext(
    _In_ D3DKMT_HANDLE KmContext)
{
    AcquireSRWLockExclusive(&ContextLock);
    std::erase(KmContexts, KmContext);
    ReleaseSRWLockExclusive(&ContextLock);
}

NTSTATUS
RxDevice::SetContextPriority(
    _In_ INT Priority)
{
    D3DKMT_SETCONTEXTSCHEDULINGPRIORITY Set;
    NTSTATUS Status = STATUS_SUCCESS;

    if (RxGetKmtThunks()->SetContextSchedulingPriority == nullptr)
        return STATUS_NOT_SUPPORTED;

    AcquireSRWLockShared(&ContextLock);
    for (D3DKMT_HANDLE KmContext : KmContexts)
    {
        Set.hContext = KmContext;
        Set.Priority = Priority;
        Status = RxGetKmtThunks()->SetContextSchedulingPriority(&Set);

        /* A lost device keeps the priority it had; the rest of the contexts still get it */
        if (!NT_SUCCESS(Status) && RxHresultFromStatus(Status) != D3DDDIERR_DEVICEREMOVED)
            break;
        Status = STATUS_SUCCESS;
    }
    ReleaseSRWLockShared(&ContextLock);

    return Status;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxDxgiDevice::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxDxgiDevice::Release(VOID)
{
    return Device.Release();
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return Device.GetPrivateDataStore().Set(Name, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return Device.GetPrivateDataStore().Set(Name, 0, nullptr, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return Device.GetPrivateDataStore().Get(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    IDXGIAdapter1* Adapter;
    HRESULT hr;

    if (ppParent == nullptr)
        return E_INVALIDARG;
    *ppParent = nullptr;

    hr = Device.GetDxgiAdapter(&Adapter);
    if (FAILED(hr))
        return hr;

    hr = Adapter->QueryInterface(riid, ppParent);
    Adapter->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::GetAdapter(
    _COM_Outptr_ IDXGIAdapter** pAdapter)
{
    IDXGIAdapter1* Adapter;
    HRESULT hr;

    if (pAdapter == nullptr)
        return E_INVALIDARG;
    *pAdapter = nullptr;

    hr = Device.GetDxgiAdapter(&Adapter);
    if (FAILED(hr))
        return DXGI_ERROR_INVALID_CALL;

    *pAdapter = Adapter;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::SetGPUThreadPriority(
    _In_ INT Priority)
{
    NTSTATUS Status;

    if (Priority < -7 || Priority > 7)
        return E_INVALIDARG;

    Status = Device.SetContextPriority(Priority);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        return RxHresultFromStatus(Status);

    GpuThreadPriority = Priority;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::GetGPUThreadPriority(
    _Out_ INT* pPriority)
{
    if (pPriority == nullptr)
        return E_INVALIDARG;

    *pPriority = GpuThreadPriority;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::SetMaximumFrameLatency(
    _In_ UINT MaxLatency)
{
    if (MaxLatency > RxMaximumFrameLatency)
        return DXGI_ERROR_INVALID_CALL;

    /* Zero restores the default */
    MaximumFrameLatency = (MaxLatency == 0) ? RxDefaultFrameLatency : MaxLatency;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::GetMaximumFrameLatency(
    _Out_ UINT* pMaxLatency)
{
    if (pMaxLatency == nullptr)
        return DXGI_ERROR_INVALID_CALL;

    *pMaxLatency = MaximumFrameLatency;
    return S_OK;
}

/* IDXGIDevice1 to 3 */

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::QueryResourceResidency(
    _In_reads_(NumResources) IUnknown* const* ppResources,
    _Out_writes_(NumResources) DXGI_RESIDENCY* pResidencyStatus,
    UINT NumResources)
{
    UINT Index;

    if (ppResources == nullptr || pResidencyStatus == nullptr)
        return E_INVALIDARG;

    /* Video memory manager residency is not tracked per resource here; everything counts as resident */
    for (Index = 0; Index < NumResources; Index++)
    {
        if (ppResources[Index] == nullptr)
            return E_INVALIDARG;
        pResidencyStatus[Index] = DXGI_RESIDENCY_FULLY_RESIDENT;
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::OfferResources(
    _In_ UINT NumResources,
    _In_reads_(NumResources) IDXGIResource* const* ppResources,
    _In_ DXGI_OFFER_RESOURCE_PRIORITY Priority)
{
    UINT Index;

    if (NumResources == 0 || ppResources == nullptr || Priority < DXGI_OFFER_RESOURCE_PRIORITY_LOW ||
        Priority > DXGI_OFFER_RESOURCE_PRIORITY_HIGH)
    {
        return E_INVALIDARG;
    }

    for (Index = 0; Index < NumResources; Index++)
    {
        if (ppResources[Index] == nullptr)
            return E_INVALIDARG;
    }

    /* Keeping offered memory is always allowed, and then nothing is lost when it is reclaimed */
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::ReclaimResources(
    _In_ UINT NumResources,
    _In_reads_(NumResources) IDXGIResource* const* ppResources,
    _Out_writes_all_opt_(NumResources) BOOL* pDiscarded)
{
    UINT Index;

    if (NumResources == 0 || ppResources == nullptr)
        return E_INVALIDARG;

    for (Index = 0; Index < NumResources; Index++)
    {
        if (ppResources[Index] == nullptr)
            return E_INVALIDARG;
        if (pDiscarded != nullptr)
            pDiscarded[Index] = FALSE;
    }

    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiDevice::EnqueueSetEvent(
    _In_ HANDLE hEvent)
{
    D3D11_QUERY_DESC Desc = { D3D11_QUERY_EVENT, 0 };
    ID3D11DeviceContext* Context;
    ID3D11Query* Query;
    BOOL Done = FALSE;
    HRESULT hr;

    if (hEvent == NULL)
        return E_INVALIDARG;

    hr = Device.CreateQuery(&Desc, &Query);
    if (FAILED(hr))
        return hr;

    /* The event goes up once everything submitted so far has finished on the GPU */
    Device.GetImmediateContext(&Context);
    Context->End(Query);
    while ((hr = Context->GetData(Query, &Done, sizeof(Done), 0)) == S_FALSE)
        SwitchToThread();

    Context->Release();
    Query->Release();
    if (FAILED(hr))
        return hr;

    return SetEvent(hEvent) ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

VOID
STDMETHODCALLTYPE
RxDxgiDevice::Trim(VOID)
{
    ID3D11DeviceContext* Context;

    /* Temporary driver memory is released by the driver as work drains; the runtime keeps none */
    Device.GetImmediateContext(&Context);
    Context->Flush();
    Context->Release();
}
