/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Fences, the ID3D11Device3 to ID3D11Device5 methods and ID3D11DeviceContext3 and 4
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "fence.hpp"

/* Monitored fences are signaled by packets only, so they never get a GPU mapping */
static
D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS
RxFenceKernelFlags(
    _In_ D3D11_FENCE_FLAG Flags)
{
    D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS Kernel = {};
    BOOLEAN IsShared = (Flags & D3D11_FENCE_FLAG_SHARED) != 0;

    Kernel.Shared = IsShared;
    Kernel.NtSecuritySharing = IsShared;
    Kernel.CrossAdapter = (Flags & D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0;
    Kernel.NoGPUAccess = (Flags & D3D11_FENCE_FLAG_NON_MONITORED) == 0;
    return Kernel;
}

HRESULT
RxFence::Create(
    _In_ RxDevice& Device,
    _In_ UINT64 InitialValue,
    _In_ D3D11_FENCE_FLAG Flags,
    _Outptr_ RxFence** Fence)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Sync = {};
    RxFence* Created;
    NTSTATUS Status;

    *Fence = nullptr;
    if (Kmt->CreateSynchronizationObject2 == nullptr)
        return E_NOTIMPL;

    Created = new (std::nothrow) RxFence(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    Sync.hDevice = Device.GetKmDevice();
    Sync.Info.Flags = RxFenceKernelFlags(Flags);
    if (Flags & D3D11_FENCE_FLAG_NON_MONITORED)
    {
        Sync.Info.Type = D3DDDI_FENCE;
        Sync.Info.Fence.FenceValue = InitialValue;
    }
    else
    {
        Sync.Info.Type = D3DDDI_MONITORED_FENCE;
        Sync.Info.MonitoredFence.InitialFenceValue = InitialValue;
    }

    Status = Kmt->CreateSynchronizationObject2(&Sync);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTCreateSynchronizationObject2 for a fence failed 0x%08lX", Status);
        Created->Release();
        return RxHresultFromStatus(Status);
    }

    Created->SyncObject = Sync.hSyncObject;
    if (Sync.Info.Type == D3DDDI_MONITORED_FENCE)
        Created->CpuValue = static_cast<volatile UINT64*>(Sync.Info.MonitoredFence.FenceValueCPUVirtualAddress);
    Created->IsShareable = (Flags & D3D11_FENCE_FLAG_SHARED) != 0;

    *Fence = Created;
    return S_OK;
}

HRESULT
RxFence::Open(
    _In_ RxDevice& Device,
    _In_ HANDLE SharedHandle,
    _Outptr_ RxFence** Fence)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open = {};
    RxFence* Opened;
    NTSTATUS Status;

    *Fence = nullptr;
    if (Kmt->OpenSyncObjectFromNtHandle2 == nullptr)
        return E_NOTIMPL;

    Opened = new (std::nothrow) RxFence(Device);
    if (Opened == nullptr)
        return E_OUTOFMEMORY;

    Open.hNtHandle = SharedHandle;
    Open.hDevice = Device.GetKmDevice();
    Status = Kmt->OpenSyncObjectFromNtHandle2(&Open);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTOpenSyncObjectFromNtHandle2 failed 0x%08lX", Status);
        Opened->Release();
        return E_INVALIDARG;
    }

    Opened->SyncObject = Open.hSyncObject;
    Opened->CpuValue = static_cast<volatile UINT64*>(Open.MonitoredFence.FenceValueCPUVirtualAddress);
    Opened->IsShareable = TRUE;

    *Fence = Opened;
    return S_OK;
}

RxFence::~RxFence()
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy = {};

    if (SyncObject != 0)
    {
        Destroy.hSyncObject = SyncObject;
        RxGetKmtThunks()->DestroySynchronizationObject(&Destroy);
    }
}

HRESULT
STDMETHODCALLTYPE
RxFence::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11Fence))
    {
        *ppvObject = static_cast<ID3D11Fence*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxFence::CreateSharedHandle(
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    _In_ DWORD dwAccess,
    _In_opt_ LPCWSTR lpName,
    _Out_ HANDLE* pHandle)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name;
    D3DKMT_HANDLE Object = SyncObject;

    if (!IsShareable || pHandle == nullptr)
        return E_INVALIDARG;
    if (Kmt->ShareObjects == nullptr)
        return E_NOTIMPL;

    if (lpName != nullptr)
    {
        Name.Buffer = const_cast<PWSTR>(lpName);
        Name.Length = (USHORT)(wcslen(lpName) * sizeof(WCHAR));
        Name.MaximumLength = Name.Length + sizeof(WCHAR);
    }
    InitializeObjectAttributes(&Attributes, lpName != nullptr ? &Name : nullptr,
                               (pAttributes != nullptr && pAttributes->bInheritHandle) ? OBJ_INHERIT : 0, nullptr,
                               pAttributes != nullptr ? pAttributes->lpSecurityDescriptor : nullptr);
    return RxHresultFromStatus(Kmt->ShareObjects(1, &Object, &Attributes, dwAccess, pHandle));
}

UINT64
STDMETHODCALLTYPE
RxFence::GetCompletedValue(VOID)
{
    if (CpuValue == nullptr)
        return UINT64_MAX;

    MemoryBarrier();
    return *CpuValue;
}

HRESULT
STDMETHODCALLTYPE
RxFence::SetEventOnCompletion(
    _In_ UINT64 Value,
    _In_opt_ HANDLE hEvent)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};
    D3DKMT_HANDLE Object = SyncObject;

    if (Kmt->WaitForSynchronizationObjectFromCpu == nullptr)
        return E_NOTIMPL;

    /* No event means the caller waits right here */
    Wait.hDevice = Device.GetKmDevice();
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &Object;
    Wait.FenceValueArray = &Value;
    Wait.hAsyncEvent = hEvent;
    return RxHresultFromStatus(Kmt->WaitForSynchronizationObjectFromCpu(&Wait));
}

/* ID3D11Device5 */

HRESULT
STDMETHODCALLTYPE
RxDevice::CreateFence(
    _In_ UINT64 InitialValue,
    _In_ D3D11_FENCE_FLAG Flags,
    _In_ REFIID ReturnedInterface,
    _COM_Outptr_ void** ppFence)
{
    constexpr UINT KnownFlags = D3D11_FENCE_FLAG_SHARED | D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER |
                                D3D11_FENCE_FLAG_NON_MONITORED;
    RxFence* Fence;
    HRESULT hr;

    if (ppFence != nullptr)
        *ppFence = nullptr;

    /* Only monitored fences exist on this runtime; a cross adapter fence must also be shared */
    if ((Flags & ~KnownFlags) != 0 || (Flags & D3D11_FENCE_FLAG_NON_MONITORED) ||
        ((Flags & D3D11_FENCE_FLAG_SHARED_CROSS_ADAPTER) && !(Flags & D3D11_FENCE_FLAG_SHARED)))
    {
        RX_TRACE(L"CreateFence: flags 0x%X are not supported", Flags);
        return E_INVALIDARG;
    }

    if (ppFence == nullptr)
        return S_FALSE;

    hr = RxFence::Create(*this, InitialValue, Flags, &Fence);
    if (FAILED(hr))
        return hr;

    hr = Fence->QueryInterface(ReturnedInterface, ppFence);
    Fence->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice::OpenSharedFence(
    _In_ HANDLE hFence,
    _In_ REFIID ReturnedInterface,
    _COM_Outptr_ void** ppFence)
{
    RxFence* Fence;
    HRESULT hr;

    *ppFence = nullptr;
    hr = RxFence::Open(*this, hFence, &Fence);
    if (FAILED(hr))
        return hr;

    hr = Fence->QueryInterface(ReturnedInterface, ppFence);
    Fence->Release();
    return hr;
}

/* ID3D11Device4 */

HRESULT
STDMETHODCALLTYPE
RxDevice::RegisterDeviceRemovedEvent(
    _In_ HANDLE hEvent,
    _Out_ DWORD* pdwCookie)
{
    HANDLE Copy;

    *pdwCookie = 0;
    if (hEvent == NULL)
        return E_INVALIDARG;

    if (!DuplicateHandle(GetCurrentProcess(), hEvent, GetCurrentProcess(), &Copy, EVENT_MODIFY_STATE, FALSE, 0))
        return HRESULT_FROM_WIN32(GetLastError());

    AcquireSRWLockExclusive(&RemovedLock);
    RemovedCookie += 1;
    RemovedEvents.push_back({ RemovedCookie, Copy });
    *pdwCookie = RemovedCookie;
    ReleaseSRWLockExclusive(&RemovedLock);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxDevice::UnregisterDeviceRemoved(
    _In_ DWORD dwCookie)
{
    AcquireSRWLockExclusive(&RemovedLock);
    for (auto Each = RemovedEvents.begin(); Each != RemovedEvents.end(); ++Each)
    {
        if (Each->Cookie == dwCookie)
        {
            CloseHandle(Each->Event);
            RemovedEvents.erase(Each);
            break;
        }
    }
    ReleaseSRWLockExclusive(&RemovedLock);
}

/* ID3D11Device3 */

VOID
STDMETHODCALLTYPE
RxDevice::GetImmediateContext3(
    _Outptr_ ID3D11DeviceContext3** ppImmediateContext)
{
    Context.AddRef();
    *ppImmediateContext = &Context;
}

/* ID3D11DeviceContext3 */

VOID
STDMETHODCALLTYPE
RxImmediateContext::Flush1(
    _In_ D3D11_CONTEXT_TYPE ContextType,
    _In_opt_ HANDLE hEvent)
{
    RxFence* Fence = nullptr;
    UINT64 Value = 1;

    UNREFERENCED_PARAMETER(ContextType);

    Device.FlushDriver(0);
    if (hEvent == NULL)
        return;

    /* The event fires once the GPU is past everything flushed so far */
    if (FAILED(RxFence::Create(Device, 0, D3D11_FENCE_FLAG_NONE, &Fence)))
        return;

    if (Device.SignalFenceOnGpu(Fence->GetSyncObject(), Value) == S_FALSE)
        SetEvent(hEvent);
    else
        Fence->SetEventOnCompletion(Value, hEvent);
    Fence->Release();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::SetHardwareProtectionState(
    _In_ BOOL HwProtectionEnable)
{
    IsHardwareProtected = HwProtectionEnable ? TRUE : FALSE;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GetHardwareProtectionState(
    _Out_ BOOL* pHwProtectionEnable)
{
    *pHwProtectionEnable = IsHardwareProtected;
}

/* ID3D11DeviceContext4 */

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::Signal(
    _In_ ID3D11Fence* pFence,
    _In_ UINT64 Value)
{
    RxFence* Fence = RxUnwrap<RxFence>(pFence);
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU CpuSignal = {};
    D3DKMT_HANDLE Object;
    HRESULT hr;

    if (Fence == nullptr)
        return E_INVALIDARG;

    /* Work recorded so far goes to the GPU first, so the signal lands after it */
    Device.FlushDriver(0);
    Object = Fence->GetSyncObject();
    hr = Device.SignalFenceOnGpu(Object, Value);
    if (hr != S_FALSE)
        return hr;

    /* Nothing was ever submitted, so nothing is pending either */
    CpuSignal.hDevice = Device.GetKmDevice();
    CpuSignal.ObjectCount = 1;
    CpuSignal.ObjectHandleArray = &Object;
    CpuSignal.FenceValueArray = &Value;
    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObjectFromCpu(&CpuSignal));
}

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::Wait(
    _In_ ID3D11Fence* pFence,
    _In_ UINT64 Value)
{
    RxFence* Fence = RxUnwrap<RxFence>(pFence);

    if (Fence == nullptr)
        return E_INVALIDARG;

    Device.FlushDriver(0);
    return Device.WaitForFenceOnGpu(Fence->GetSyncObject(), Value);
}
