/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The D3D12 side of a device: heaps, null views and the graphics command list
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "descriptors.hpp"

#include <functional>
#include <memory>

/* Shader visible heap sizes the layer starts with */
constexpr UINT O12ViewHeapSize = D3D12_MAX_SHADER_VISIBLE_DESCRIPTOR_HEAP_SIZE_TIER_1;
constexpr UINT O12SamplerHeapSize = D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE;

/* View dimensions in the order the null views are made */
enum O12NullView
{
    O12NullUnknown,
    O12NullBuffer,
    O12NullTexture1D,
    O12NullTexture2D,
    O12NullTexture2DMS,
    O12NullTexture3D,
    O12NullTextureCube,
    O12NullTexture1DArray,
    O12NullTexture2DArray,
    O12NullTexture2DMSArray,
    O12NullTextureCubeArray,
    O12NullCount,
};

struct O12ContextArgs
{
    BOOLEAN DisableGpuTimeout;
    BOOLEAN ComputeOnly;
};

class O12Context
{
public:
    O12Context(
        _In_ ID3D12Device* Device,
        _In_ UINT NodeIndex);

    ~O12Context();

    O12Context(const O12Context&) = delete;
    O12Context& operator=(const O12Context&) = delete;

    HRESULT
    Initialize(
        _In_opt_ ID3D12CommandQueue* Queue,
        _In_ const O12ContextArgs& Args);

    ID3D12CommandQueue*
    GetGraphicsQueue() const
    {
        return GraphicsQueue.Get();
    }

    UINT
    GetNodeMask() const
    {
        return 1u << NodeIndex;
    }

    D3D_FEATURE_LEVEL
    GetFeatureLevel() const
    {
        return FeatureLevel;
    }

    ID3D12Device*
    GetDevice() const
    {
        return Device;
    }

    O12DescriptorPool&
    GetSamplerPool()
    {
        return SamplerPool;
    }

    /* Context work waits in a queue and reaches the command list at the next flush point */
    VOID
    QueueBlendFactor(
        _In_ const FLOAT Factor[4]);

    VOID
    QueueStencilRef(
        _In_ UINT Reference);

    VOID
    RunQueued();

private:
    HRESULT
    CreateNullViews();

    HRESULT
    OpenGraphicsList(
        _In_opt_ ID3D12CommandQueue* Queue);

    VOID
    PrimeList();

    ID3D12Device* Device;
    UINT NodeIndex;
    O12ContextArgs Args = {};
    D3D_FEATURE_LEVEL FeatureLevel;
    O12DescriptorPool SrvPool;
    O12DescriptorPool UavPool;
    O12DescriptorPool RtvPool;
    O12DescriptorPool DsvPool;
    O12DescriptorPool SamplerPool;
    D3D12_FEATURE_DATA_ARCHITECTURE1 Architecture = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS13 Options13 = {};

    RxComPtr<ID3D12CommandQueue> SyncQueue;
    RxComPtr<IUnknown> DxcoreAdapter;
    RxComPtr<ID3D12Device3> Device3;
    RxComPtr<ID3D12Fence> ResidencyFence;

    RxComPtr<ID3D12DescriptorHeap> ViewHeap;
    RxComPtr<ID3D12DescriptorHeap> SamplerHeap;
    UINT ViewIncrement = 0;
    UINT SamplerIncrement = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE NullSrv[O12NullCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE NullUav[O12NullCount] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE NullRtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE NullSampler = {};

    RxComPtr<ID3D12Device1> Device1;
    RxComPtr<ID3D12Device2> Device2;
    RxComPtr<ID3D12CompatibilityDevice> Compatibility;

    RxComPtr<ID3D12CommandQueue> GraphicsQueue;
    RxComPtr<ID3D12Fence1> ListFence;
    RxComPtr<ID3D12CommandAllocator> Allocator;
    RxComPtr<ID3D12GraphicsCommandList> List;
    RxComPtr<IUnknown> SharingContract;
    HANDLE WaitEvent = NULL;
    std::vector<std::function<VOID(ID3D12GraphicsCommandList*)>> Queued;
    FLOAT BlendFactor[4] = {};
    UINT StencilRef = 0;
    UINT64 ListId = 1;
};
