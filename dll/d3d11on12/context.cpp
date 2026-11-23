/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The D3D12 side of a device: heaps, null views and the graphics command list
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "context.hpp"

#include <dxcore.h>

/* CPU heap sizes of the descriptor pools */
constexpr UINT O12ViewPoolHeap = 1024;
constexpr UINT O12TargetPoolHeap = 64;

/* Highest level the hardware reaches, asked from the top down */
static
D3D_FEATURE_LEVEL
O12QueryHardwareLevel(
    _In_ ID3D12Device* Device)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_1_0_CORE,
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS Query = {};

    Query.NumFeatureLevels = _countof(Levels);
    Query.pFeatureLevelsRequested = Levels;
    Device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &Query, sizeof(Query));
    return Query.MaxSupportedFeatureLevel;
}

O12Context::O12Context(
    _In_ ID3D12Device* Device,
    _In_ UINT NodeIndex)
    : Device(Device),
      NodeIndex(NodeIndex),
      FeatureLevel(O12QueryHardwareLevel(Device)),
      SrvPool(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, O12ViewPoolHeap, 1u << NodeIndex),
      UavPool(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, O12ViewPoolHeap, 1u << NodeIndex),
      RtvPool(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, O12TargetPoolHeap, 1u << NodeIndex),
      DsvPool(Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, O12TargetPoolHeap, 1u << NodeIndex),
      SamplerPool(Device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, O12TargetPoolHeap, 1u << NodeIndex)
{
    Architecture.NodeIndex = NodeIndex;
    Device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &Architecture, sizeof(Architecture));
}

O12Context::~O12Context()
{
    if (WaitEvent != NULL)
        CloseHandle(WaitEvent);
}

/* The DXCore adapter matching the device, used for memory budgets */
static
VOID
O12OpenDxcoreAdapter(
    _In_ LUID Luid,
    _Inout_ RxComPtr<IUnknown>& Adapter)
{
    typedef HRESULT (WINAPI* PFN_DXCORE_CREATE_FACTORY)(REFIID, void**);
    PFN_DXCORE_CREATE_FACTORY Create;
    RxComPtr<IDXCoreAdapterFactory> Factory;
    HMODULE Module;

    Module = LoadLibraryExW(L"dxcore.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Module == NULL)
        return;

    Create = reinterpret_cast<PFN_DXCORE_CREATE_FACTORY>(GetProcAddress(Module, "DXCoreCreateAdapterFactory"));
    if (Create != nullptr && SUCCEEDED(Create(__uuidof(IDXCoreAdapterFactory), Factory.PutVoid())))
        Factory->GetAdapterByLuid(Luid, __uuidof(IDXCoreAdapter), Adapter.PutVoid());
}

HRESULT
O12Context::Initialize(
    _In_opt_ ID3D12CommandQueue* Queue,
    _In_ const O12ContextArgs& Arguments)
{
    D3D12_COMMAND_QUEUE_DESC SyncDesc = {};
    D3D12_DESCRIPTOR_HEAP_DESC HeapDesc = {};
    HRESULT hr;

    Args = Arguments;
    WaitEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

    /* A queue that only ever waits and signals */
    SyncDesc.Type = D3D12_COMMAND_LIST_TYPE_NONE;
    Device->CreateCommandQueue(&SyncDesc, __uuidof(ID3D12CommandQueue), SyncQueue.PutVoid());

    O12OpenDxcoreAdapter(Device->GetAdapterLuid(), DxcoreAdapter);

    hr = Device->QueryInterface(__uuidof(ID3D12Device3), Device3.PutVoid());
    if (FAILED(hr))
        return hr;
    hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), ResidencyFence.PutVoid());
    if (FAILED(hr))
        return hr;

    Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS13, &Options13, sizeof(Options13));

    HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    HeapDesc.NumDescriptors = O12ViewHeapSize;
    HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HeapDesc.NodeMask = GetNodeMask();
    hr = Device->CreateDescriptorHeap(&HeapDesc, __uuidof(ID3D12DescriptorHeap), ViewHeap.PutVoid());
    if (FAILED(hr))
        return hr;
    ViewIncrement = Device->GetDescriptorHandleIncrementSize(HeapDesc.Type);

    if (!Args.ComputeOnly)
    {
        HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        HeapDesc.NumDescriptors = O12SamplerHeapSize;
        hr = Device->CreateDescriptorHeap(&HeapDesc, __uuidof(ID3D12DescriptorHeap), SamplerHeap.PutVoid());
        if (FAILED(hr))
            return hr;
        SamplerIncrement = Device->GetDescriptorHandleIncrementSize(HeapDesc.Type);
    }

    hr = CreateNullViews();
    if (FAILED(hr))
        return hr;

    Device->QueryInterface(__uuidof(ID3D12Device1), Device1.PutVoid());
    Device->QueryInterface(__uuidof(ID3D12Device2), Device2.PutVoid());
    Device->QueryInterface(__uuidof(ID3D12CompatibilityDevice), Compatibility.PutVoid());

    return OpenGraphicsList(Queue);
}

/* Fills a null shader resource view and unordered access view for one dimension */
static
VOID
O12DescribeNullView(
    _In_ O12NullView Kind,
    _In_ BOOLEAN ComputeOnly,
    _Out_ D3D12_SHADER_RESOURCE_VIEW_DESC* Srv,
    _Out_ D3D12_UNORDERED_ACCESS_VIEW_DESC* Uav)
{
    *Srv = {};
    *Uav = {};
    Srv->Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Srv->Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    Uav->Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

    if (ComputeOnly)
        Kind = O12NullBuffer;

    switch (Kind)
    {
        case O12NullUnknown:
        case O12NullBuffer:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            if (ComputeOnly)
            {
                Srv->Format = DXGI_FORMAT_R32_TYPELESS;
                Uav->Format = DXGI_FORMAT_R32_TYPELESS;
                Srv->Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                Uav->Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            break;

        case O12NullTexture1D:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
            Srv->Texture1D.MipLevels = 1;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
            break;

        case O12NullTexture2D:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            Srv->Texture2D.MipLevels = 1;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            break;

        case O12NullTexture2DMS:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
            break;

        case O12NullTexture3D:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            Srv->Texture3D.MipLevels = 1;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            Uav->Texture3D.WSize = 1;
            break;

        case O12NullTextureCube:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            Srv->TextureCube.MipLevels = 1;
            break;

        case O12NullTexture1DArray:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
            Srv->Texture1DArray.MipLevels = 1;
            Srv->Texture1DArray.ArraySize = 1;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
            Uav->Texture1DArray.ArraySize = 1;
            break;

        case O12NullTexture2DArray:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            Srv->Texture2DArray.MipLevels = 1;
            Srv->Texture2DArray.ArraySize = 1;
            Uav->ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            Uav->Texture2DArray.ArraySize = 1;
            break;

        case O12NullTexture2DMSArray:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
            Srv->Texture2DMSArray.ArraySize = 1;
            break;

        case O12NullTextureCubeArray:
            Srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
            Srv->TextureCubeArray.MipLevels = 1;
            Srv->TextureCubeArray.NumCubes = 1;
            break;

        default:
            break;
    }
}

HRESULT
O12Context::CreateNullViews()
{
    D3D12_SHADER_RESOURCE_VIEW_DESC Srv;
    D3D12_UNORDERED_ACCESS_VIEW_DESC Uav;
    D3D12_RENDER_TARGET_VIEW_DESC Rtv = {};
    D3D12_SAMPLER_DESC Sampler = {};
    HRESULT hr;

    for (UINT Kind = 0; Kind < O12NullCount; Kind++)
    {
        O12DescribeNullView(static_cast<O12NullView>(Kind), Args.ComputeOnly, &Srv, &Uav);

        if (Srv.ViewDimension != D3D12_SRV_DIMENSION_UNKNOWN)
        {
            hr = SrvPool.Allocate(&NullSrv[Kind]);
            if (FAILED(hr))
                return hr;
            Device->CreateShaderResourceView(nullptr, &Srv, NullSrv[Kind]);
        }

        if (Uav.ViewDimension != D3D12_UAV_DIMENSION_UNKNOWN)
        {
            hr = UavPool.Allocate(&NullUav[Kind]);
            if (FAILED(hr))
                return hr;
            Device->CreateUnorderedAccessView(nullptr, nullptr, &Uav, NullUav[Kind]);
        }
    }

    if (Args.ComputeOnly)
        return S_OK;

    hr = RtvPool.Allocate(&NullRtv);
    if (FAILED(hr))
        return hr;
    Rtv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    Device->CreateRenderTargetView(nullptr, &Rtv, NullRtv);

    hr = SamplerPool.Allocate(&NullSampler);
    if (FAILED(hr))
        return hr;
    Sampler.Filter = D3D12_FILTER_ANISOTROPIC;
    Sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    Sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    Sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    Sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    Device->CreateSampler(&Sampler, NullSampler);

    return S_OK;
}

HRESULT
O12Context::OpenGraphicsList(
    _In_opt_ ID3D12CommandQueue* Queue)
{
    D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
    RxComPtr<ID3D12Device9> Device9;
    RxComPtr<ID3D12CommandList> Created;
    HRESULT hr;

    if (Queue != nullptr)
    {
        GraphicsQueue = Queue;
    }
    else
    {
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.NodeMask = GetNodeMask();
        QueueDesc.Flags = Args.DisableGpuTimeout ? D3D12_COMMAND_QUEUE_FLAG_DISABLE_GPU_TIMEOUT :
                                                   D3D12_COMMAND_QUEUE_FLAG_NONE;
        if (SUCCEEDED(Device->QueryInterface(__uuidof(ID3D12Device9), Device9.PutVoid())))
        {
            hr = Device9->CreateCommandQueue1(&QueueDesc, __uuidof(D3D11On12CreatorID), __uuidof(ID3D12CommandQueue),
                                              GraphicsQueue.PutVoid());
        }
        else
        {
            hr = Device->CreateCommandQueue(&QueueDesc, __uuidof(ID3D12CommandQueue), GraphicsQueue.PutVoid());
        }
        if (FAILED(hr))
            return hr;
    }

    hr = Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence1), ListFence.PutVoid());
    if (FAILED(hr))
        return hr;

    hr = Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                        Allocator.PutVoid());
    if (FAILED(hr))
        return hr;

    hr = Device->CreateCommandList(GetNodeMask(), D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.Get(), nullptr,
                                   __uuidof(ID3D12CommandList), Created.PutVoid());
    if (FAILED(hr))
        return hr;
    hr = Created->QueryInterface(__uuidof(ID3D12GraphicsCommandList), List.PutVoid());
    if (FAILED(hr))
        return hr;

    PrimeList();

    GraphicsQueue->QueryInterface(__uuidof(ID3D12SharingContract), SharingContract.PutVoid());
    return S_OK;
}

/* State every new command list starts with */
VOID
O12Context::PrimeList()
{
    ID3D12DescriptorHeap* Heaps[2] = { ViewHeap.Get(), SamplerHeap.Get() };
    D3D12_RECT Scissors[D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];

    List->SetDescriptorHeaps(Args.ComputeOnly ? 1 : 2, Heaps);

    for (D3D12_RECT& Each : Scissors)
    {
        Each.left = D3D12_VIEWPORT_BOUNDS_MIN;
        Each.top = D3D12_VIEWPORT_BOUNDS_MIN;
        Each.right = D3D12_VIEWPORT_BOUNDS_MAX;
        Each.bottom = D3D12_VIEWPORT_BOUNDS_MAX;
    }
    List->RSSetScissorRects(_countof(Scissors), Scissors);
}

VOID
O12Context::QueueBlendFactor(
    _In_ const FLOAT Factor[4])
{
    FLOAT Copy[4] = { Factor[0], Factor[1], Factor[2], Factor[3] };

    Queued.push_back([this, Copy](ID3D12GraphicsCommandList* Target)
    {
        memcpy(BlendFactor, Copy, sizeof(BlendFactor));
        Target->OMSetBlendFactor(Copy);
    });
}

VOID
O12Context::QueueStencilRef(
    _In_ UINT Reference)
{
    Queued.push_back([this, Reference](ID3D12GraphicsCommandList* Target)
    {
        StencilRef = Reference;
        Target->OMSetStencilRef(Reference);
    });
}

VOID
O12Context::RunQueued()
{
    std::vector<std::function<VOID(ID3D12GraphicsCommandList*)>> Work;

    Work.swap(Queued);
    for (auto& Each : Work)
        Each(List.Get());
}