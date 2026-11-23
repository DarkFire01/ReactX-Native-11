/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     D3D11On12CreateDevice, a Direct3D 11 device driven by d3d11on12.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "resource.hpp"

#include <dxgi1_4.h>

/* Used when the caller passes no list */
static constexpr D3D_FEATURE_LEVEL On12DefaultLevels[] =
{
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
};

/* The API level each pipeline level reaches */
static
D3D_FEATURE_LEVEL
RxOn12ApiLevel(
    _In_ D3D11DDI_3DPIPELINELEVEL Level)
{
    switch (Level)
    {
        case D3DWDDM2_0DDI_3DPIPELINELEVEL_12_1:
            return D3D_FEATURE_LEVEL_12_1;
        case D3DWDDM2_0DDI_3DPIPELINELEVEL_12_0:
            return D3D_FEATURE_LEVEL_12_0;
        case D3D11_1DDI_3DPIPELINELEVEL_11_1:
            return D3D_FEATURE_LEVEL_11_1;
        case D3D11DDI_3DPIPELINELEVEL_11_0:
            return D3D_FEATURE_LEVEL_11_0;
        case D3D11DDI_3DPIPELINELEVEL_10_1:
            return D3D_FEATURE_LEVEL_10_1;
        case D3D11DDI_3DPIPELINELEVEL_10_0:
            return D3D_FEATURE_LEVEL_10_0;
        case D3D11_1DDI_3DPIPELINELEVEL_9_3:
            return D3D_FEATURE_LEVEL_9_3;
        case D3D11_1DDI_3DPIPELINELEVEL_9_2:
            return D3D_FEATURE_LEVEL_9_2;
        default:
            return D3D_FEATURE_LEVEL_9_1;
    }
}

/* The layer asks for the API flags of each resource as it creates it */
static
D3D11_RESOURCE_FLAGS
CALLBACK
RxOn12GetResourceFlags(
    _In_ D3D10DDI_HRESOURCE Resource,
    _Out_ bool* AcquireOnWrite)
{
    *AcquireOnWrite = false;
    return DdiResource::FromLayerHandle(Resource).GetApiFlags();
}

/* Only resources opened through the runtime's shared path are noted, and none are yet */
static
bool
CALLBACK
RxOn12NotifySharedResourceCreation(
    _In_ HANDLE Resource,
    _In_ IUnknown* Object)
{
    return false;
}

/* Serializes layer opens, which share one adapter slot */
static SRWLOCK On12CreateLock = SRWLOCK_INIT;

/* The DXGI adapter the D3D12 device runs on, which the layer is handed */
static
HRESULT
RxOn12FindAdapter(
    _In_ const LUID& Luid,
    _In_ UINT Flags,
    _COM_Outptr_ IDXGIAdapter** Adapter)
{
    typedef HRESULT (WINAPI* PFN_CREATE_FACTORY2)(UINT, REFIID, void**);
    PFN_CREATE_FACTORY2 CreateFactory2;
    IDXGIFactory4* Factory = nullptr;
    HMODULE Dxgi;
    HRESULT hr;

    *Adapter = nullptr;
    Dxgi = LoadLibraryW(L"dxgi.dll");
    CreateFactory2 = Dxgi ? reinterpret_cast<PFN_CREATE_FACTORY2>(GetProcAddress(Dxgi, "CreateDXGIFactory2")) : nullptr;
    if (CreateFactory2 == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    hr = CreateFactory2((Flags & D3D11_CREATE_DEVICE_DEBUG) ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&Factory));
    if (FAILED(hr))
    {
        RX_TRACE(L"CreateDXGIFactory2 failed 0x%08lX", hr);
        return hr;
    }

    hr = Factory->EnumAdapterByLuid(Luid, IID_PPV_ARGS(Adapter));
    Factory->Release();
    if (FAILED(hr))
        RX_TRACE(L"no DXGI adapter for the D3D12 device (0x%08lX)", hr);
    return hr;
}

/* Every queue must be a direct one on the chosen node, at most one of them, and match the timeout flag */
static
HRESULT
RxOn12PickQueue(
    _In_reads_opt_(QueueCount) IUnknown* const* Queues,
    _In_ UINT QueueCount,
    _In_ UINT NodeMask,
    _In_ UINT Flags,
    _Outptr_result_maybenull_ ID3D12CommandQueue** Picked)
{
    ID3D12CommandQueue* Queue;
    D3D12_COMMAND_QUEUE_DESC Desc;
    UINT Index;

    *Picked = nullptr;
    for (Index = 0; Index < QueueCount; Index++)
    {
        Queue = nullptr;
        if (Queues[Index] == nullptr || FAILED(Queues[Index]->QueryInterface(IID_PPV_ARGS(&Queue))))
        {
            RX_TRACE(L"D3D11On12CreateDevice: queue %u is not a D3D12 command queue", Index);
            goto Invalid;
        }

        Desc = Queue->GetDesc();
        if ((Flags & D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT) &&
            (Desc.Flags & D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT) == 0)
        {
            RX_TRACE(L"D3D11On12CreateDevice: queue %u does not disable the GPU timeout", Index);
            goto InvalidQueue;
        }

        if ((Desc.NodeMask & NodeMask) == 0)
        {
            RX_TRACE(L"D3D11On12CreateDevice: queue %u is on another node", Index);
            goto InvalidQueue;
        }

        if (Desc.Type != D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
            RX_TRACE(L"D3D11On12CreateDevice: queue %u is not a direct queue", Index);
            goto InvalidQueue;
        }

        if (*Picked != nullptr)
        {
            RX_TRACE(L"D3D11On12CreateDevice: more than one direct queue");
            goto InvalidQueue;
        }

        *Picked = Queue;
    }

    return S_OK;

InvalidQueue:
    Queue->Release();
Invalid:
    if (*Picked != nullptr)
    {
        (*Picked)->Release();
        *Picked = nullptr;
    }
    return E_INVALIDARG;
}

/* Opens the layer and creates the device at the first requested level the layer reaches */
static
HRESULT
RxOn12CreateDevice(
    _In_ ID3D12Device1* Device12,
    _In_opt_ ID3D12CommandQueue* Queue,
    _In_ UINT NodeIndex,
    _In_ UINT Flags,
    _In_reads_(LevelCount) const D3D_FEATURE_LEVEL* Levels,
    _In_ UINT LevelCount,
    _Outptr_opt_ RxDevice** Created,
    _Out_opt_ D3D_FEATURE_LEVEL* Chosen)
{
    On12Callbacks2 Callbacks2 = {};
    On12OpenArgs On12 = {};
    IDXGIAdapter* Adapter = nullptr;
    D3D_FEATURE_LEVEL Strongest;
    D3D_FEATURE_LEVEL Level = (D3D_FEATURE_LEVEL)0;
    UmdAdapter* Umd;
    RxDevice* Device;
    UINT Index;
    HRESULT hr;

    hr = RxOn12FindAdapter(Device12->GetAdapterLuid(), Flags, &Adapter);
    if (FAILED(hr))
        return DXGI_ERROR_UNSUPPORTED;

    On12.Device = Device12;
    On12.GraphicsQueue = Queue;
    On12.Adapter = Adapter;
    On12.NodeIndex = NodeIndex;
    On12.Callbacks.GetResourceFlags = RxOn12GetResourceFlags;
    On12.Callbacks.NotifySharedResourceCreation = RxOn12NotifySharedResourceCreation;
    On12.DisableGpuTimeout = true;
    On12.DeferredContexts = true;
    On12.ContractVersion = On12ContractVersion;
    On12.Callbacks2 = &Callbacks2;

    Umd = new (std::nothrow) UmdAdapter();
    if (Umd == nullptr)
    {
        Adapter->Release();
        return E_OUTOFMEMORY;
    }

    hr = Umd->OpenLayer(Device12->GetAdapterLuid(), &On12);
    if (FAILED(hr))
    {
        delete Umd;
        Adapter->Release();
        return DXGI_ERROR_UNSUPPORTED;
    }

    /* The first requested level the layer reaches, whatever it is called */
    Strongest = RxOn12ApiLevel(Umd->GetHighestPipelineLevel());
    for (Index = 0; Index < LevelCount; Index++)
    {
        if (Levels[Index] <= Strongest)
        {
            Level = Levels[Index];
            break;
        }
    }

    if (Level == 0)
    {
        delete Umd;
        Adapter->Release();
        return DXGI_ERROR_UNSUPPORTED;
    }

    RX_TRACE(L"D3D11 on 12 feature level 0x%04X", Level);
    if (Chosen != nullptr)
        *Chosen = Level;

    if (Created == nullptr)
    {
        delete Umd;
        Adapter->Release();
        return S_FALSE;
    }

    /* The device owns the adapter from here on, even when creation fails */
    hr = RxDevice::Create(Umd, Level, Umd->GetHighestPipelineLevel(), Flags, &Device);
    if (SUCCEEDED(hr))
    {
        Device->SetDxgiAdapter(Adapter);
        *Created = Device;
    }

    Adapter->Release();
    return hr;
}

extern "C"
HRESULT
WINAPI
D3D11On12CreateDevice(
    _In_ IUnknown* pDevice,
    _In_ UINT Flags,
    _In_reads_opt_(FeatureLevels) const D3D_FEATURE_LEVEL* pFeatureLevels,
    _In_ UINT FeatureLevels,
    _In_reads_opt_(NumQueues) IUnknown* const* ppCommandQueues,
    _In_ UINT NumQueues,
    _In_ UINT NodeMask,
    _COM_Outptr_opt_ ID3D11Device** ppDevice,
    _COM_Outptr_opt_ ID3D11DeviceContext** ppImmediateContext,
    _Out_opt_ D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    ID3D12Device1* Device12 = nullptr;
    ID3D12CommandQueue* Queue = nullptr;
    RxDevice* Device = nullptr;
    BOOLEAN WantsDevice;
    ULONG NodeIndex;
    HRESULT hr;

    if (ppDevice != nullptr)
        *ppDevice = nullptr;
    if (ppImmediateContext != nullptr)
        *ppImmediateContext = nullptr;
    WantsDevice = ppDevice != nullptr || ppImmediateContext != nullptr;

    if (pDevice == nullptr)
    {
        RX_TRACE(L"D3D11On12CreateDevice: no D3D12 device");
        return E_INVALIDARG;
    }

    if (FAILED(pDevice->QueryInterface(IID_PPV_ARGS(&Device12))) || Device12 == nullptr)
    {
        RX_TRACE(L"D3D11On12CreateDevice: not a D3D12 device");
        return E_INVALIDARG;
    }

    if (pFeatureLevels != nullptr && pChosenFeatureLevel != nullptr &&
        pChosenFeatureLevel >= pFeatureLevels && pChosenFeatureLevel < pFeatureLevels + FeatureLevels)
    {
        RX_TRACE(L"D3D11On12CreateDevice: the chosen level overlaps the requested ones");
        Device12->Release();
        return E_INVALIDARG;
    }

    if (pChosenFeatureLevel != nullptr)
        *pChosenFeatureLevel = (D3D_FEATURE_LEVEL)0;

    if (pFeatureLevels == nullptr || FeatureLevels == 0)
    {
        pFeatureLevels = On12DefaultLevels;
        FeatureLevels = _countof(On12DefaultLevels);
    }

    if (NodeMask == 0)
        NodeMask = 1;
    if ((NodeMask & (NodeMask - 1)) != 0)
    {
        RX_TRACE(L"D3D11On12CreateDevice: node mask 0x%X has more than one node", NodeMask);
        Device12->Release();
        return E_INVALIDARG;
    }
    _BitScanForward(&NodeIndex, NodeMask);

    if ((NumQueues != 0) != (ppCommandQueues != nullptr))
    {
        RX_TRACE(L"D3D11On12CreateDevice: queue count and queue list disagree");
        Device12->Release();
        return E_INVALIDARG;
    }

    if ((Flags & ~RxKnownCreateFlags) != 0)
    {
        Device12->Release();
        return E_INVALIDARG;
    }

    hr = RxOn12PickQueue(ppCommandQueues, NumQueues, NodeMask, Flags, &Queue);
    if (FAILED(hr))
    {
        Device12->Release();
        return hr;
    }

    AcquireSRWLockExclusive(&On12CreateLock);
    hr = RxOn12CreateDevice(Device12, Queue, NodeIndex, Flags, pFeatureLevels, FeatureLevels,
                            WantsDevice ? &Device : nullptr, pChosenFeatureLevel);
    ReleaseSRWLockExclusive(&On12CreateLock);

    if (FAILED(hr))
    {
        if (pChosenFeatureLevel != nullptr)
            *pChosenFeatureLevel = (D3D_FEATURE_LEVEL)0;
    }
    else if (Device != nullptr)
    {
        if (ppImmediateContext != nullptr)
            Device->GetImmediateContext(ppImmediateContext);

        if (ppDevice != nullptr)
            *ppDevice = Device;
        else
            Device->Release();
    }

    if (Queue != nullptr)
        Queue->Release();
    Device12->Release();
    return hr;
}
