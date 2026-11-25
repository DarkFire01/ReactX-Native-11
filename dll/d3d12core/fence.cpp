/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12Fence over a kernel monitored fence
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

constexpr D3D12_FENCE_FLAGS RxKnownFenceFlags = D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER |
                                                D3D12_FENCE_FLAG_NON_MONITORED;

HRESULT
RxFence::Create(
    _In_ RxDevice12& Device,
    _In_ UINT64 InitialValue,
    _In_ D3D12_FENCE_FLAGS Flags,
    _Outptr_ RxFence** Fence)
{
    RxFence* Created;
    HRESULT hr;

    *Fence = nullptr;

    if ((Flags & ~RxKnownFenceFlags) != 0)
        return E_INVALIDARG;
    if ((Flags & D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER) != 0 && (Flags & D3D12_FENCE_FLAG_SHARED) == 0)
        return E_INVALIDARG;

    /* Cross adapter and non monitored fences need support this runtime does not have yet */
    if ((Flags & ~D3D12_FENCE_FLAG_SHARED) != 0)
    {
        RX_TRACE(L"fence flags 0x%X not supported", Flags);
        return E_NOTIMPL;
    }

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxFence(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(InitialValue, Flags);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Fence = Created;
    return S_OK;
}

HRESULT
RxFence::Open(
    _In_ RxDevice12& Device,
    _In_ HANDLE SharedHandle,
    _Outptr_ RxFence** Fence)
{
    RxFence* Opened;
    HRESULT hr;

    *Fence = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Opened = new (std::nothrow) RxFence(Device);
    if (Opened == nullptr)
        return E_OUTOFMEMORY;

    hr = Opened->InitializeOpened(SharedHandle);
    if (FAILED(hr))
    {
        Opened->Release();
        return hr;
    }

    *Fence = Opened;
    return S_OK;
}

HRESULT
RxFence::Initialize(
    _In_ UINT64 InitialValue,
    _In_ D3D12_FENCE_FLAGS FenceFlags)
{
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    NTSTATUS Status;

    Flags = FenceFlags;

    /* The CPU reads the value; the GPU only reaches the fence through kernel signals and waits */
    Create.hDevice = Device.KernelDevice();
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = TRUE;
    Create.Info.MonitoredFence.InitialFenceValue = InitialValue;

    /* A shared fence gets NT handles through D3DKMTShareObjects */
    if ((FenceFlags & D3D12_FENCE_FLAG_SHARED) != 0)
    {
        Create.Info.Flags.Shared = TRUE;
        Create.Info.Flags.NtSecuritySharing = TRUE;
    }
    Status = RxGetKmtThunks()->CreateSynchronizationObject2(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"monitored fence creation failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }
    SyncObject = Create.hSyncObject;
    CompletedValue = static_cast<volatile UINT64*>(Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);

    return CreateDriverFence(Create.Info.MonitoredFence.FenceValueGPUVirtualAddress);
}

/* An opened fence is a monitored fence mapped into this process, flagged shared like its creator */
HRESULT
RxFence::InitializeOpened(
    _In_ HANDLE SharedHandle)
{
    D3DKMT_OPENSYNCOBJECTFROMNTHANDLE2 Open = {};
    const KmtThunks* Kmt = RxGetKmtThunks();
    NTSTATUS Status;

    Flags = D3D12_FENCE_FLAG_SHARED;
    if (Kmt->OpenSyncObjectFromNtHandle2 == nullptr)
        return E_NOTIMPL;

    Open.hNtHandle = SharedHandle;
    Open.hDevice = Device.KernelDevice();
    Open.Flags.NoGPUAccess = TRUE;
    Status = Kmt->OpenSyncObjectFromNtHandle2(&Open);
    if (!NT_SUCCESS(Status))
        return RxApiResult12(Device, Status, "D3DKMTOpenSyncObjectFromNtHandle2");

    SyncObject = Open.hSyncObject;
    CompletedValue = static_cast<volatile UINT64*>(Open.MonitoredFence.FenceValueCPUVirtualAddress);
    if (CompletedValue == nullptr)
        return E_INVALIDARG;

    return CreateDriverFence(Open.MonitoredFence.FenceValueGPUVirtualAddress);
}

HRESULT
RxFence::CreateDriverFence(
    _In_ D3D12_GPU_VIRTUAL_ADDRESS FenceAddress)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_FENCE_0116 Args = {};
    D3D12DDIARG_CREATE_FENCE_0116 Sizing = {};
    D3D12DDI_HRTFENCE Runtime;
    HRESULT hr;

    Args.FenceType = D3D12DDI_FENCE_TYPE_MONITORED;
    Args.MonitoredFenceArgs.FenceValue.BaseAddress = FenceAddress;
    Args.MonitoredFenceArgs.Flags = D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE;

    /* Sizing only names the fence type */
    Sizing.FenceType = Args.FenceType;
    DriverFence.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateFenceSize(Device.Handle(), &Sizing));
    if (DriverFence.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    Runtime.handle = this;
    hr = Ddi.pfnCreateFence(Device.Handle(), DriverFence, Runtime, &Args);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateFence failed 0x%08lX", hr);
        return hr;
    }
    IsDriverFenceCreated = TRUE;

    return S_OK;
}

RxFence::~RxFence()
{
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT Destroy;

    if (IsDriverFenceCreated)
        Device.Ddi().pfnDestroyFence(Device.Handle(), DriverFence);

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

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Fence) || riid == __uuidof(ID3D12Fence1) ||
        riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12Fence1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

UINT64
STDMETHODCALLTYPE
RxFence::GetCompletedValue(VOID)
{
    MemoryBarrier();
    return *CompletedValue;
}

HRESULT
STDMETHODCALLTYPE
RxFence::SetEventOnCompletion(
    UINT64 Value,
    HANDLE hEvent)
{
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECTFROMCPU Wait = {};

    /* With no event the kernel wait blocks until the value is reached */
    Wait.hDevice = Device.KernelDevice();
    Wait.ObjectCount = 1;
    Wait.ObjectHandleArray = &SyncObject;
    Wait.FenceValueArray = &Value;
    Wait.hAsyncEvent = hEvent;
    return RxHresultFromStatus(RxGetKmtThunks()->WaitForSynchronizationObjectFromCpu(&Wait));
}

HRESULT
STDMETHODCALLTYPE
RxFence::Signal(
    UINT64 Value)
{
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECTFROMCPU Signal = {};

    Signal.hDevice = Device.KernelDevice();
    Signal.ObjectCount = 1;
    Signal.ObjectHandleArray = &SyncObject;
    Signal.FenceValueArray = &Value;
    Signal.Flags.AllowFenceRewind = TRUE;
    return RxHresultFromStatus(RxGetKmtThunks()->SignalSynchronizationObjectFromCpu(&Signal));
}

D3D12_FENCE_FLAGS
STDMETHODCALLTYPE
RxFence::GetCreationFlags(VOID)
{
    return Flags;
}
