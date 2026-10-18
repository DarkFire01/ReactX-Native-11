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

    /* Shared and non monitored fences need sharing support this runtime does not have yet */
    if (Flags != D3D12_FENCE_FLAG_NONE)
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
RxFence::Initialize(
    _In_ UINT64 InitialValue,
    _In_ D3D12_FENCE_FLAGS FenceFlags)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 Create = {};
    D3D12DDIARG_CREATE_FENCE_0116 Args = {};
    D3D12DDI_HRTFENCE Runtime;
    NTSTATUS Status;
    HRESULT hr;

    Flags = FenceFlags;

    /* The CPU reads the value; the GPU only reaches the fence through kernel signals and waits */
    Create.hDevice = Device.KernelDevice();
    Create.Info.Type = D3DDDI_MONITORED_FENCE;
    Create.Info.Flags.NoGPUAccess = TRUE;
    Create.Info.MonitoredFence.InitialFenceValue = InitialValue;
    Status = RxGetKmtThunks()->CreateSynchronizationObject2(&Create);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"monitored fence creation failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }
    SyncObject = Create.hSyncObject;
    CompletedValue = static_cast<volatile UINT64*>(Create.Info.MonitoredFence.FenceValueCPUVirtualAddress);

    Args.FenceType = D3D12DDI_FENCE_TYPE_MONITORED;
    Args.MonitoredFenceArgs.FenceValue.BaseAddress = Create.Info.MonitoredFence.FenceValueGPUVirtualAddress;
    Args.MonitoredFenceArgs.Flags = D3D12DDI_FENCE_FLAG_BOTTOM_OF_PIPE;

    DriverFence.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateFenceSize(Device.Handle(), &Args));
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

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
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
