/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 device: creation, format queries and the DDI tables
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device12.hpp"
#include "format.hpp"
#include <type_traits>

static constexpr UINT RxWarp12DevicePageSize = 4096;

/* Size of every driver object the runtime keeps a handle to: one pointer */
static constexpr SIZE_T RxWarp12ObjectSize = 8;

template <typename Function>
struct RxWarp12StubOf;

/* Entry points not written yet: they trace, fail with E_NOTIMPL or return zero */
template <typename Result, typename... Arguments>
struct RxWarp12StubOf<Result (APIENTRY *)(Arguments...)>
{
    static
    Result
    APIENTRY
    Run(
        _In_ Arguments...)
    {
        RX_WARP_TRACE("D3D12 DDI not implemented: %s\n", __FUNCSIG__);
        if constexpr (std::is_same_v<Result, HRESULT>)
            return E_NOTIMPL;
        else if constexpr (!std::is_void_v<Result>)
            return Result{};
    }
};

template <typename Function>
static
VOID
RxWarp12Stub(
    _Out_ Function *Slot)
{
    *Slot = &RxWarp12StubOf<Function>::Run;
}

/* Entry points that only ever answer one value */
template <typename Function, auto Value>
struct RxWarp12ConstantOf;

template <typename Result, typename... Arguments, auto Value>
struct RxWarp12ConstantOf<Result (APIENTRY *)(Arguments...), Value>
{
    static
    Result
    APIENTRY
    Run(
        _In_ Arguments...)
    {
        return (Result)Value;
    }
};

template <auto Value, typename Function>
static
VOID
RxWarp12Constant(
    _Out_ Function *Slot)
{
    *Slot = &RxWarp12ConstantOf<Function, Value>::Run;
}

/* Entry points that accept anything and do nothing */
template <typename Function>
struct RxWarp12NothingOf;

template <typename... Arguments>
struct RxWarp12NothingOf<VOID (APIENTRY *)(Arguments...)>
{
    static
    VOID
    APIENTRY
    Run(
        _In_ Arguments...)
    {
    }
};

template <typename Function>
static
VOID
RxWarp12Nothing(
    _Out_ Function *Slot)
{
    *Slot = &RxWarp12NothingOf<Function>::Run;
}

static
RxWarp12Device *
RxWarp12GetDevice(
    _In_ D3D12DDI_HDEVICE Device)
{
    return RxWarpObject<RxWarp12Device>(Device);
}

HRESULT
RxWarp12Device::Create(
    _In_ RxWarpAdapter *Adapter,
    _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args)
{
    RxWarp12Device *Device;

    Device = new (std::nothrow) RxWarp12Device();
    if (Device == nullptr)
        return E_OUTOFMEMORY;

    Device->Adapter = Adapter;
    Device->RuntimeDevice = Args->hRTDevice;
    Device->Interface = Args->Interface;
    Device->Version = Args->Version;
    Device->Flags = Args->Flags;
    Device->KernelCallbacks = Args->pKTCallbacks;
    Device->Callbacks = Args->p12UMCallbacks;
    RxWarpBind(Args->hDrvDevice, Device);

    /* Recorded work runs on a Direct3D 11 executor of the device's own */
    Device->Executor = new (std::nothrow) RxWarpDevice();
    if (Device->Executor == nullptr)
    {
        delete Device;
        RxWarpBind(Args->hDrvDevice, static_cast<RxWarp12Device *>(nullptr));
        return E_OUTOFMEMORY;
    }
    Device->Executor->Adapter = Adapter;

    /* A page of kernel memory the device keeps for its own bookkeeping */
    if (Args->p12UMCallbacks != nullptr)
    {
        RX_WARP_ALLOCATION_DATA Data = {};

        Data.Dimension = D3D10DDIRESOURCE_BUFFER;
        Data.Width = RxWarp12DevicePageSize;
        Data.Height = 1;
        Data.Depth = 1;
        Data.RowPitch = RxWarp12DevicePageSize;
        Data.Size = RxWarp12DevicePageSize;
        Data.ArraySize = 1;
        Data.MipLevels = 1;
        Data.Flags = RX_WARP_ALLOCATION_LINEAR;
        RxWarp12AllocateKernel(Device, &Data, nullptr, FALSE, &Device->PageAllocation, &Device->Page);
    }
    RX_WARP_TRACE("D3D12 device %p, interface %x version %x\n", Device, Args->Interface, Args->Version);
    return S_OK;
}

VOID
APIENTRY
RxWarp12Device::Destroy(
    _In_ D3D12DDI_HDEVICE Device)
{
    RxWarpLock Lock;

    RxWarp12Device *This = RxWarp12GetDevice(Device);

    if (This->PageAllocation != 0)
        RxWarp12FreeKernel(This, nullptr, This->PageAllocation, This->Page != nullptr);
    delete This->Executor;
    delete This;
}

static
VOID
APIENTRY
RxWarp12CheckFormatSupport(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _Out_ UINT *Support)
{
    *Support = RxWarpFormatSupport12(Format, RxWarp12GetDevice(Device)->Interface);
}

static
VOID
APIENTRY
RxWarp12CheckMultisampleQualityLevels(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT SampleCount,
    _In_ D3D12DDI_MULTISAMPLE_QUALITY_LEVEL_FLAGS Flags,
    _Out_ UINT *Levels)
{
    UNREFERENCED_PARAMETER(Device);
    *Levels = RxWarpMultisampleQuality(Format, SampleCount, (UINT)Flags);
}

static
HRESULT
APIENTRY
RxWarp12CreateCommandPool(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_COMMAND_POOL_0040 *Args,
    _In_ D3D12DDI_HCOMMANDPOOL_0040 Pool)
{
    RxWarpLock Lock;
    RxWarp12CommandPool *Object;

    UNREFERENCED_PARAMETER(Args);
    Object = new (std::nothrow) RxWarp12CommandPool();
    RxWarpBind(Pool, Object);
    if (Object == nullptr)
    {
        RX_WARP_TRACE("D3D12 command pool allocation failed\n");
        return E_OUTOFMEMORY;
    }
    Object->Device = RxWarp12GetDevice(Device);
    return S_OK;
}

static
VOID
APIENTRY
RxWarp12DestroyCommandPool(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDPOOL_0040 Pool)
{
    RxWarpLock Lock;

    RxWarp12CommandPool *Object = RxWarpObject<RxWarp12CommandPool>(Pool);

    UNREFERENCED_PARAMETER(Device);
    if (Object != nullptr && Object->Allocation != 0)
        RxWarp12FreeKernel(Object->Device, nullptr, Object->Allocation, Object->Memory != nullptr);
    delete Object;
}

static
VOID
APIENTRY
RxWarp12ResetCommandPool(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_HCOMMANDPOOL_0040 Pool)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Pool);
}

/* One node: physical adapter i is node i */
static
VOID
APIENTRY
RxWarp12QueryNodeMap(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ UINT Count,
    _Out_writes_(Count) UINT *Map)
{
    UNREFERENCED_PARAMETER(Device);
    for (UINT Index = 0; Index < Count; Index++)
        Map[Index] = Index;
}

/* The reference keeps its root signature inside the runtime's memory; this many bytes */
static
SIZE_T
APIENTRY
RxWarp12CalcPrivateRootSignatureSize(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ const D3D12DDIARG_CREATE_ROOT_SIGNATURE_0100 *Args)
{
    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Args);
    return 0x90;
}

/* Video and content protection are the only extended features, and both are off */
static
HRESULT
APIENTRY
RxWarp12GetSupportedExtendedFeatures(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_FEATURE_0020 Highest,
    _Inout_ UINT32 *Count,
    _Out_writes_opt_(*Count) D3D12DDI_FEATURE_0020 *Features)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Highest);
    if (Features == nullptr)
        *Count = 0;
    return S_OK;
}

static
HRESULT
APIENTRY
RxWarp12GetSupportedExtendedFeatureVersions(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_FEATURE_0020 Feature,
    _Inout_ UINT32 *Count,
    _Out_writes_opt_(*Count) UINT32 *Versions)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Feature);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Versions);
    return E_INVALIDARG;
}

static
HRESULT
APIENTRY
RxWarp12EnableExtendedFeature(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_FEATURE_0020 Feature,
    _In_ UINT32 Version)
{
    RxWarpLock Lock;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Feature);
    UNREFERENCED_PARAMETER(Version);
    return E_INVALIDARG;
}

static
HRESULT
APIENTRY
RxWarp12SetExtendedFeatureCallbacks(
    _In_ D3D12DDI_HDEVICE Device,
    _In_ D3D12DDI_TABLE_TYPE Table,
    _In_reads_(Size) const VOID *Callbacks,
    _In_ SIZE_T Size)
{
    RxWarpLock Lock;

    if (Table == D3D12DDI_TABLE_TYPE_0030_CONTENT_PROTECTION_CALLBACKS && Size == sizeof(VOID *))
        RxWarp12GetDevice(Device)->ContentProtectionCallback = *static_cast<VOID *const *>(Callbacks);
    return S_OK;
}

static
D3D12DDI_DEVICE_FUNCS_CORE_0116
RxWarp12BuildDeviceTable(VOID)
{
    D3D12DDI_DEVICE_FUNCS_CORE_0116 Device = {};

    RxWarp12Stub(&Device.pfnCheckFormatSupport);
    RxWarp12Stub(&Device.pfnCheckMultisampleQualityLevels);
    RxWarp12Stub(&Device.pfnGetMipPacking);
    RxWarp12Stub(&Device.pfnCalcPrivateElementLayoutSize);
    RxWarp12Stub(&Device.pfnCreateElementLayout);
    RxWarp12Stub(&Device.pfnDestroyElementLayout);
    RxWarp12Stub(&Device.pfnCalcPrivateBlendStateSize);
    RxWarp12Stub(&Device.pfnCreateBlendState);
    RxWarp12Stub(&Device.pfnDestroyBlendState);
    RxWarp12Stub(&Device.pfnCalcPrivateDepthStencilStateSize);
    RxWarp12Stub(&Device.pfnCreateDepthStencilState);
    RxWarp12Stub(&Device.pfnDestroyDepthStencilState);
    RxWarp12Stub(&Device.pfnCalcPrivateRasterizerStateSize);
    RxWarp12Stub(&Device.pfnCreateRasterizerState);
    RxWarp12Stub(&Device.pfnDestroyRasterizerState);
    RxWarp12Stub(&Device.pfnCalcPrivateShaderSize);
    RxWarp12Stub(&Device.pfnCreateVertexShader);
    RxWarp12Stub(&Device.pfnCreatePixelShader);
    RxWarp12Stub(&Device.pfnCreateGeometryShader);
    RxWarp12Stub(&Device.pfnCreateComputeShader);
    RxWarp12Stub(&Device.pfnCalcPrivateGeometryShaderWithStreamOutput);
    RxWarp12Stub(&Device.pfnCreateGeometryShaderWithStreamOutput);
    RxWarp12Stub(&Device.pfnCalcPrivateTessellationShaderSize);
    RxWarp12Stub(&Device.pfnCreateHullShader);
    RxWarp12Stub(&Device.pfnCreateDomainShader);
    RxWarp12Stub(&Device.pfnDestroyShader);
    RxWarp12Stub(&Device.pfnCalcPrivateCommandQueueSize);
    RxWarp12Stub(&Device.pfnCreateCommandQueue);
    RxWarp12Stub(&Device.pfnDestroyCommandQueue);
    RxWarp12Stub(&Device.pfnCalcPrivateCommandPoolSize);
    RxWarp12Stub(&Device.pfnCreateCommandPool);
    RxWarp12Stub(&Device.pfnDestroyCommandPool);
    RxWarp12Stub(&Device.pfnResetCommandPool);
    RxWarp12Stub(&Device.pfnCalcPrivatePipelineStateSize);
    RxWarp12Stub(&Device.pfnCreatePipelineState);
    RxWarp12Stub(&Device.pfnDestroyPipelineState);
    RxWarp12Stub(&Device.pfnCalcPrivateCommandListSize);
    RxWarp12Stub(&Device.pfnCreateCommandList);
    RxWarp12Stub(&Device.pfnDestroyCommandList);
    RxWarp12Stub(&Device.pfnCalcPrivateFenceSize);
    RxWarp12Stub(&Device.pfnCreateFence);
    RxWarp12Stub(&Device.pfnDestroyFence);
    RxWarp12Stub(&Device.pfnCalcPrivateDescriptorHeapSize);
    RxWarp12Stub(&Device.pfnCreateDescriptorHeap);
    RxWarp12Stub(&Device.pfnDestroyDescriptorHeap);
    RxWarp12Stub(&Device.pfnGetDescriptorSizeInBytes);
    RxWarp12Stub(&Device.pfnGetCPUDescriptorHandleForHeapStart);
    RxWarp12Stub(&Device.pfnGetGPUDescriptorHandleForHeapStart);
    RxWarp12Stub(&Device.pfnCreateShaderResourceView);
    RxWarp12Stub(&Device.pfnCreateConstantBufferView);
    RxWarp12Stub(&Device.pfnCreateSampler);
    RxWarp12Stub(&Device.pfnCreateUnorderedAccessView);
    RxWarp12Stub(&Device.pfnCreateRenderTargetView);
    RxWarp12Stub(&Device.pfnCreateDepthStencilView);
    RxWarp12Stub(&Device.pfnCalcPrivateRootSignatureSize);
    RxWarp12Stub(&Device.pfnCreateRootSignature);
    RxWarp12Stub(&Device.pfnDestroyRootSignature);
    RxWarp12Stub(&Device.pfnMapHeap);
    RxWarp12Stub(&Device.pfnUnmapHeap);
    RxWarp12Stub(&Device.pfnCalcPrivateHeapAndResourceSizes);
    RxWarp12Stub(&Device.pfnCreateHeapAndResource);
    RxWarp12Stub(&Device.pfnDestroyHeapAndResource);
    RxWarp12Stub(&Device.pfnMakeResident);
    RxWarp12Stub(&Device.pfnEvict);
    RxWarp12Stub(&Device.pfnCalcPrivateOpenedHeapAndResourceSizes);
    RxWarp12Stub(&Device.pfnOpenHeapAndResource);
    RxWarp12Stub(&Device.pfnCopyDescriptors);
    RxWarp12Stub(&Device.pfnCopyDescriptorsSimple);
    RxWarp12Stub(&Device.pfnCalcPrivateQueryHeapSize);
    RxWarp12Stub(&Device.pfnCreateQueryHeap);
    RxWarp12Stub(&Device.pfnDestroyQueryHeap);
    RxWarp12Stub(&Device.pfnCalcPrivateCommandSignatureSize);
    RxWarp12Stub(&Device.pfnCreateCommandSignature);
    RxWarp12Stub(&Device.pfnDestroyCommandSignature);
    RxWarp12Stub(&Device.pfnCheckResourceVirtualAddress);
    RxWarp12Stub(&Device.pfnCheckResourceAllocationInfo);
    RxWarp12Stub(&Device.pfnCheckSubresourceInfo);
    RxWarp12Stub(&Device.pfnCheckExistingResourceAllocationInfo);
    RxWarp12Stub(&Device.pfnOfferResources);
    RxWarp12Stub(&Device.pfnReclaimResources);
    RxWarp12Stub(&Device.pfnGetImplicitPhysicalAdapterMask);
    RxWarp12Stub(&Device.pfnGetPresentPrivateDriverDataSize);
    RxWarp12Stub(&Device.pfnQueryNodeMap);
    RxWarp12Stub(&Device.pfnRetrieveShaderComment);
    RxWarp12Stub(&Device.pfnCheckResourceAllocationHandle);
    RxWarp12Stub(&Device.pfnCalcPrivatePipelineLibrarySize);
    RxWarp12Stub(&Device.pfnCreatePipelineLibrary);
    RxWarp12Stub(&Device.pfnDestroyPipelineLibrary);
    RxWarp12Stub(&Device.pfnAddPipelineStateToLibrary);
    RxWarp12Stub(&Device.pfnCalcSerializedLibrarySize);
    RxWarp12Stub(&Device.pfnSerializeLibrary);
    RxWarp12Stub(&Device.pfnGetDebugAllocationInfo);
    RxWarp12Stub(&Device.pfnCalcPrivateCommandRecorderSize);
    RxWarp12Stub(&Device.pfnCreateCommandRecorder);
    RxWarp12Stub(&Device.pfnDestroyCommandRecorder);
    RxWarp12Stub(&Device.pfnCommandRecorderSetCommandPoolAsTarget);
    RxWarp12Stub(&Device.pfnCalcPrivateSchedulingGroupSize);
    RxWarp12Stub(&Device.pfnCreateSchedulingGroup);
    RxWarp12Stub(&Device.pfnDestroySchedulingGroup);
    RxWarp12Stub(&Device.pfnEnumerateMetaCommands);
    RxWarp12Stub(&Device.pfnEnumerateMetaCommandParameters);
    RxWarp12Stub(&Device.pfnCalcPrivateMetaCommandSize);
    RxWarp12Stub(&Device.pfnCreateMetaCommand);
    RxWarp12Stub(&Device.pfnDestroyMetaCommand);
    RxWarp12Stub(&Device.pfnGetMetaCommandRequiredParameterInfo);
    RxWarp12Stub(&Device.pfnCalcPrivateStateObjectSize);
    RxWarp12Stub(&Device.pfnCreateStateObject);
    RxWarp12Stub(&Device.pfnDestroyStateObject);
    RxWarp12Stub(&Device.pfnGetRaytracingAccelerationStructurePrebuildInfo);
    RxWarp12Stub(&Device.pfnCheckDriverMatchingIdentifier);
    RxWarp12Stub(&Device.pfnGetShaderIdentifier);
    RxWarp12Stub(&Device.pfnGetShaderStackSize);
    RxWarp12Stub(&Device.pfnGetPipelineStackSize);
    RxWarp12Stub(&Device.pfnSetPipelineStackSize);
    RxWarp12Stub(&Device.pfnSetBackgroundProcessingMode);
    RxWarp12Stub(&Device.pfnCalcPrivateAddToStateObjectSize);
    RxWarp12Stub(&Device.pfnAddToStateObject);
    RxWarp12Stub(&Device.pfnCreateSamplerFeedbackUnorderedAccessView);
    RxWarp12Stub(&Device.pfnCreateAmplificationShader);
    RxWarp12Stub(&Device.pfnCreateMeshShader);
    RxWarp12Stub(&Device.pfnCalcPrivateMeshShaderSize);
    RxWarp12Stub(&Device.pfnGetProgramIdentifier);
    RxWarp12Stub(&Device.pfnGetWorkGraphMemoryRequirements);
    RxWarp12Stub(&Device.pfnGetTextureLayouts);
    RxWarp12Stub(&Device.pfnGetApplicationSpecificDriverState);
    RxWarp12Stub(&Device.pfnGetApplicationSpecificDriverBlobStatus);
    RxWarp12Stub(&Device.pfnGetApplicationSpecificDriverStateBlobSize);
    /* Every object that is a single pointer to us */
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateElementLayoutSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateBlendStateSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateDepthStencilStateSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateRasterizerStateSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateShaderSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateGeometryShaderWithStreamOutput);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateTessellationShaderSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateCommandQueueSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateCommandPoolSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivatePipelineStateSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateCommandListSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateFenceSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateDescriptorHeapSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateQueryHeapSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateCommandSignatureSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivatePipelineLibrarySize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateCommandRecorderSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateSchedulingGroupSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateStateObjectSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateAddToStateObjectSize);
    RxWarp12Constant<RxWarp12ObjectSize>(&Device.pfnCalcPrivateMeshShaderSize);

    /* Queries with a fixed answer */
    RxWarp12Constant<0>(&Device.pfnGetPresentPrivateDriverDataSize);
    RxWarp12Constant<0>(&Device.pfnGetShaderStackSize);
    RxWarp12Constant<0>(&Device.pfnGetApplicationSpecificDriverState);
    RxWarp12Constant<1>(&Device.pfnGetApplicationSpecificDriverBlobStatus);
    RxWarp12Constant<0>(&Device.pfnGetApplicationSpecificDriverStateBlobSize);

    Device.pfnCheckFormatSupport = RxWarp12CheckFormatSupport;
    Device.pfnCheckMultisampleQualityLevels = RxWarp12CheckMultisampleQualityLevels;
    Device.pfnCreateCommandPool = RxWarp12CreateCommandPool;
    Device.pfnDestroyCommandPool = RxWarp12DestroyCommandPool;
    Device.pfnResetCommandPool = RxWarp12ResetCommandPool;
    Device.pfnQueryNodeMap = RxWarp12QueryNodeMap;
    Device.pfnCalcPrivateRootSignatureSize = RxWarp12CalcPrivateRootSignatureSize;
    RxWarp12FillResourceTable(&Device);
    RxWarp12FillPipelineTable(&Device);
    RxWarp12FillDescriptorTable(&Device);
    RxWarp12FillListTable(&Device, nullptr);
    RxWarp12FillQueueTables(&Device, nullptr, nullptr);
    return Device;
}

static
D3D12DDI_COMMAND_LIST_FUNCS_3D_0114
RxWarp12BuildCommandListTable(VOID)
{
    D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 List = {};

    RxWarp12Stub(&List.pfnCloseCommandList);
    RxWarp12Stub(&List.pfnResetCommandList);
    RxWarp12Stub(&List.pfnDrawInstanced);
    RxWarp12Stub(&List.pfnDrawIndexedInstanced);
    RxWarp12Stub(&List.pfnDispatch);
    RxWarp12Stub(&List.pfnClearUnorderedAccessViewUint);
    RxWarp12Stub(&List.pfnClearUnorderedAccessViewFloat);
    RxWarp12Stub(&List.pfnClearRenderTargetView);
    RxWarp12Stub(&List.pfnClearDepthStencilView);
    RxWarp12Stub(&List.pfnDiscardResource);
    RxWarp12Stub(&List.pfnCopyTextureRegion);
    RxWarp12Stub(&List.pfnResourceCopy);
    RxWarp12Stub(&List.pfnCopyTiles);
    RxWarp12Stub(&List.pfnCopyBufferRegion);
    RxWarp12Stub(&List.pfnResourceResolveSubresource);
    RxWarp12Stub(&List.pfnExecuteBundle);
    RxWarp12Stub(&List.pfnExecuteIndirect);
    RxWarp12Stub(&List.pfnResourceBarrier);
    RxWarp12Stub(&List.pfnBlt);
    RxWarp12Stub(&List.pfnPresent);
    RxWarp12Stub(&List.pfnBeginQuery);
    RxWarp12Stub(&List.pfnEndQuery);
    RxWarp12Stub(&List.pfnResolveQueryData);
    RxWarp12Stub(&List.pfnSetPredication);
    RxWarp12Stub(&List.pfnIaSetTopology);
    RxWarp12Stub(&List.pfnRsSetViewports);
    RxWarp12Stub(&List.pfnRsSetScissorRects);
    RxWarp12Stub(&List.pfnOmSetBlendFactor);
    RxWarp12Stub(&List.pfnOmSetStencilRef);
    RxWarp12Stub(&List.pfnSetPipelineState);
    RxWarp12Stub(&List.pfnSetDescriptorHeaps);
    RxWarp12Stub(&List.pfnSetComputeRootSignature);
    RxWarp12Stub(&List.pfnSetGraphicsRootSignature);
    RxWarp12Stub(&List.pfnSetComputeRootDescriptorTable);
    RxWarp12Stub(&List.pfnSetGraphicsRootDescriptorTable);
    RxWarp12Stub(&List.pfnSetComputeRoot32BitConstant);
    RxWarp12Stub(&List.pfnSetGraphicsRoot32BitConstant);
    RxWarp12Stub(&List.pfnSetComputeRoot32BitConstants);
    RxWarp12Stub(&List.pfnSetGraphicsRoot32BitConstants);
    RxWarp12Stub(&List.pfnSetComputeRootConstantBufferView);
    RxWarp12Stub(&List.pfnSetGraphicsRootConstantBufferView);
    RxWarp12Stub(&List.pfnSetComputeRootShaderResourceView);
    RxWarp12Stub(&List.pfnSetGraphicsRootShaderResourceView);
    RxWarp12Stub(&List.pfnSetComputeRootUnorderedAccessView);
    RxWarp12Stub(&List.pfnSetGraphicsRootUnorderedAccessView);
    RxWarp12Stub(&List.pfnIASetIndexBuffer);
    RxWarp12Stub(&List.pfnIASetVertexBuffers);
    RxWarp12Stub(&List.pfnSOSetTargets);
    RxWarp12Stub(&List.pfnOMSetRenderTargets);
    RxWarp12Stub(&List.pfnSetMarker);
    RxWarp12Stub(&List.pfnClearRootArguments);
    RxWarp12Stub(&List.pfnAtomicCopyBufferRegion);
    RxWarp12Stub(&List.pfnOMSetDepthBounds);
    RxWarp12Stub(&List.pfnSetSamplePositions);
    RxWarp12Stub(&List.pfnResourceResolveSubresourceRegion);
    RxWarp12Stub(&List.pfnSetProtectedResourceSession);
    RxWarp12Stub(&List.pfnWriteBufferImmediate);
    RxWarp12Stub(&List.pfnSetViewInstanceMask);
    RxWarp12Stub(&List.pfnInitializeMetaCommand);
    RxWarp12Stub(&List.pfnExecuteMetaCommand);
    RxWarp12Stub(&List.pfnBuildRaytracingAccelerationStructure);
    RxWarp12Stub(&List.pfnEmitRaytracingAccelerationStructurePostbuildInfo);
    RxWarp12Stub(&List.pfnCopyRaytracingAccelerationStructure);
    RxWarp12Stub(&List.pfnSetPipelineState1);
    RxWarp12Stub(&List.pfnDispatchRays);
    RxWarp12Stub(&List.pfnRSSetShadingRate);
    RxWarp12Stub(&List.pfnRSSetShadingRateImage);
    RxWarp12Stub(&List.pfnDispatchMesh);
    RxWarp12Stub(&List.pfnBarrier);
    RxWarp12Stub(&List.pfnOmSetFrontAndBackStencilRef);
    RxWarp12Stub(&List.pfnRSSetDepthBias);
    RxWarp12Stub(&List.pfnIASetIndexBufferStripCutValue);
    RxWarp12Stub(&List.pfnSetProgram);
    RxWarp12Stub(&List.pfnDispatchGraph);
    RxWarp12Nothing(&List.pfnDiscardResource);
    RxWarp12Nothing(&List.pfnSetProtectedResourceSession);
    RxWarp12Nothing(&List.pfnInitializeMetaCommand);
    RxWarp12FillListTable(nullptr, &List);
    RxWarp12FillQueueTables(nullptr, nullptr, &List);
    return List;
}

static
D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001
RxWarp12BuildCommandQueueTable(VOID)
{
    D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 Queue = {};

    RxWarp12Stub(&Queue.pfnExecuteCommandLists);
    RxWarp12Stub(&Queue.pfnUpdateTileMappings);
    RxWarp12Stub(&Queue.pfnCopyTileMappings);
    RxWarp12Stub(&Queue.pfnSignalFence);
    RxWarp12Stub(&Queue.pfnWaitForFence);
    RxWarp12FillQueueTables(nullptr, &Queue, nullptr);
    return Queue;
}

static
D3D12DDI_EXTENDED_FEATURES_FUNCS_0096
RxWarp12BuildExtendedFeaturesTable(VOID)
{
    D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 Features = {};

    Features.pfnGetSupportedExtendedFeatures = RxWarp12GetSupportedExtendedFeatures;
    Features.pfnGetSupportedExtendedFeatureVersions = RxWarp12GetSupportedExtendedFeatureVersions;
    Features.pfnEnableExtendedFeature = RxWarp12EnableExtendedFeature;
    Features.pfnSetExtendedFeatureCallbacks = RxWarp12SetExtendedFeatureCallbacks;
    return Features;
}

/* Present and the residency calls; overlay planes and the old present stay empty like the reference */
static
DXGI1_4_DDI_BASE_FUNCTIONS
RxWarp12BuildDxgiTable(VOID)
{
    DXGI1_4_DDI_BASE_FUNCTIONS Dxgi = {};

    RxWarp12Stub(&Dxgi.pfnGetGammaCaps);
    RxWarp12Stub(&Dxgi.pfnSetDisplayMode);
    RxWarp12Stub(&Dxgi.pfnSetResourcePriority);
    RxWarp12Stub(&Dxgi.pfnQueryResourceResidency);
    RxWarp12Stub(&Dxgi.pfnRotateResourceIdentities);
    RxWarp12Stub(&Dxgi.pfnBlt);
    RxWarp12Stub(&Dxgi.pfnResolveSharedResource);
    RxWarp12Stub(&Dxgi.pfnBlt1);
    RxWarp12Stub(&Dxgi.pfnOfferResources);
    RxWarp12Stub(&Dxgi.pfnReclaimResources);
    RxWarp12Stub(&Dxgi.pfnPresent1);
    RxWarp12Stub(&Dxgi.pfnTrimResidencySet);
    return Dxgi;
}

const D3D12DDI_DEVICE_FUNCS_CORE_0116 *
RxWarp12Device::DeviceTable()
{
    static const D3D12DDI_DEVICE_FUNCS_CORE_0116 Table = RxWarp12BuildDeviceTable();

    return &Table;
}

const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *
RxWarp12Device::CommandListTable()
{
    static const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 Table = RxWarp12BuildCommandListTable();

    return &Table;
}

const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *
RxWarp12Device::CommandQueueTable()
{
    static const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 Table = RxWarp12BuildCommandQueueTable();

    return &Table;
}

const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 *
RxWarp12Device::ExtendedFeaturesTable()
{
    static const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 Table = RxWarp12BuildExtendedFeaturesTable();

    return &Table;
}

const DXGI1_4_DDI_BASE_FUNCTIONS *
RxWarp12Device::DxgiTable()
{
    static const DXGI1_4_DDI_BASE_FUNCTIONS Table = RxWarp12BuildDxgiTable();

    return &Table;
}
