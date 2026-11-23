/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12CommandAllocator: a driver command pool plus the recorder that writes into it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

HRESULT
RxCommandAllocator::Create(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _Outptr_ RxCommandAllocator** Allocator)
{
    RxCommandAllocator* Created;
    HRESULT hr;

    *Allocator = nullptr;

    if (RxQueueFlagsOf(Type) == D3D12DDI_COMMAND_QUEUE_FLAG_NONE)
        return E_INVALIDARG;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxCommandAllocator(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Type);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Allocator = Created;
    return S_OK;
}

HRESULT
RxCommandAllocator::Initialize(
    _In_ D3D12_COMMAND_LIST_TYPE ListType)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_COMMAND_POOL_0040 PoolArgs = {};
    D3D12DDIARG_CREATE_COMMAND_RECORDER_0040 RecorderArgs = {};
    HRESULT hr;

    Type = ListType;

    Pool.pDrvPrivate = PoolMemory.Allocate(Ddi.pfnCalcPrivateCommandPoolSize(Device.Handle(), &PoolArgs));
    if (Pool.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateCommandPool(Device.Handle(), &PoolArgs, Pool);
    if (FAILED(hr))
        return hr;
    IsPoolCreated = TRUE;

    /* One recorder serves every list type but video, so it can record whatever list uses it */
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            RecorderArgs.QueueFlags = RxQueueFlagsOf(Type);
            break;
        default:
            RecorderArgs.QueueFlags = RxQueueFlagsOf(D3D12_COMMAND_LIST_TYPE_DIRECT);
            break;
    }

    Recorder.pDrvPrivate = RecorderMemory.Allocate(Ddi.pfnCalcPrivateCommandRecorderSize(Device.Handle(), &RecorderArgs));
    if (Recorder.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateCommandRecorder(Device.Handle(), &RecorderArgs, Recorder);
    if (FAILED(hr))
        return hr;
    IsRecorderCreated = TRUE;

    Ddi.pfnCommandRecorderSetCommandPoolAsTarget(Device.Handle(), Recorder, Pool);
    return S_OK;
}

RxCommandAllocator::~RxCommandAllocator()
{
    if (IsRecorderCreated)
        Device.Ddi().pfnDestroyCommandRecorder(Device.Handle(), Recorder);
    if (IsPoolCreated)
        Device.Ddi().pfnDestroyCommandPool(Device.Handle(), Pool);
}

HRESULT
STDMETHODCALLTYPE
RxCommandAllocator::QueryInterface(
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
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12CommandAllocator) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12CommandAllocator*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxCommandAllocator::Reset(VOID)
{
    /* A list still recording into the pool keeps it */
    if (!Acquire())
        return E_FAIL;

    Device.Ddi().pfnResetCommandPool(Device.Handle(), Pool);
    Unhold();
    return S_OK;
}
