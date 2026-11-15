<#
.SYNOPSIS
    Regenerate the COM declaration and stub files under dll/d3d11.

.DESCRIPTION
    Each entry lists the methods implemented by hand; everything else gets a stub.
    Run this after moving a method from a stub file into real code.
#>
[CmdletBinding()]
param(
    [string] $SdkVersion = '10.0.28000.0',

    # Regenerate only the entries whose Dir is listed, such as dll\dwrite
    [string[]] $Only
)

$ErrorActionPreference = 'Stop'

$Root = Split-Path -Parent $PSScriptRoot
$HeaderDir = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include\$SdkVersion"
$Generator = Join-Path $PSScriptRoot 'gencom.py'
$DefaultDir = 'dll\d3d11'

$Classes = @(
    @{
        Class       = 'RxDevice'
        Interfaces  = @('ID3D11Device', 'ID3D11Device1', 'ID3D11Device2')
        Headers     = @('um\d3d11.h', 'um\d3d11_1.h', 'um\d3d11_2.h')
        Name        = 'device'
        Purpose     = 'ID3D11Device methods'
        Implemented = @(
            'GetFeatureLevel', 'GetCreationFlags', 'GetDeviceRemovedReason', 'GetImmediateContext',
            'SetExceptionMode', 'GetExceptionMode', 'CheckMultisampleQualityLevels', 'CreateTexture2D',
            'CreateRenderTargetView', 'CreateBuffer', 'CreateVertexShader', 'CreatePixelShader',
            'CreateInputLayout', 'CreateShaderResourceView', 'CreateDepthStencilView', 'CreateBlendState',
            'CreateDepthStencilState', 'CreateRasterizerState', 'CreateSamplerState', 'GetPrivateData',
            'SetPrivateData', 'SetPrivateDataInterface', 'CreateTexture1D', 'CreateTexture3D',
            'CreateUnorderedAccessView', 'CreateGeometryShader', 'CreateGeometryShaderWithStreamOutput',
            'CreateHullShader', 'CreateDomainShader', 'CreateComputeShader', 'CheckFormatSupport',
            'CreateQuery', 'CreatePredicate', 'CheckFeatureSupport', 'GetImmediateContext1', 'CreateBlendState1',
            'CreateRasterizerState1', 'GetImmediateContext2', 'GetResourceTiling', 'CheckMultisampleQualityLevels1',
            'CreateDeviceContextState', 'OpenSharedResource', 'OpenSharedResource1'
        )
    },
    @{
        Class       = 'RxImmediateContext'
        Interfaces  = @('ID3D11DeviceChild', 'ID3D11DeviceContext', 'ID3D11DeviceContext1', 'ID3D11DeviceContext2')
        Headers     = @('um\d3d11.h', 'um\d3d11_1.h', 'um\d3d11_2.h')
        Name        = 'context'
        Purpose     = 'ID3D11DeviceContext methods'
        Implemented = @(
            'GetDevice', 'GetType', 'GetContextFlags', 'Flush', 'ClearState', 'Map', 'Unmap',
            'CopySubresourceRegion', 'CopyResource', 'ClearRenderTargetView', 'VSSetConstantBuffers',
            'PSSetShaderResources', 'PSSetShader', 'PSSetSamplers', 'VSSetShader', 'DrawIndexed', 'Draw',
            'PSSetConstantBuffers', 'IASetInputLayout', 'IASetVertexBuffers', 'IASetIndexBuffer',
            'IASetPrimitiveTopology', 'VSSetShaderResources', 'VSSetSamplers', 'OMSetRenderTargets',
            'OMSetBlendState', 'OMSetDepthStencilState', 'RSSetState', 'RSSetViewports', 'RSSetScissorRects',
            'ClearDepthStencilView', 'UpdateSubresource', 'ResolveSubresource', 'DrawInstanced',
            'DrawIndexedInstanced', 'DrawInstancedIndirect', 'DrawIndexedInstancedIndirect', 'IAGetInputLayout',
            'IAGetVertexBuffers', 'IAGetIndexBuffer', 'IAGetPrimitiveTopology', 'RSGetState', 'RSGetViewports',
            'RSGetScissorRects', 'OMGetRenderTargets', 'OMGetBlendState', 'OMGetDepthStencilState',
            'VSGetConstantBuffers', 'VSGetShaderResources', 'VSGetSamplers', 'HSSetConstantBuffers',
            'HSSetShaderResources', 'HSSetSamplers', 'HSGetConstantBuffers', 'HSGetShaderResources',
            'HSGetSamplers', 'DSSetConstantBuffers', 'DSSetShaderResources', 'DSSetSamplers',
            'DSGetConstantBuffers', 'DSGetShaderResources', 'DSGetSamplers', 'GSSetConstantBuffers',
            'GSSetShaderResources', 'GSSetSamplers', 'GSGetConstantBuffers', 'GSGetShaderResources',
            'GSGetSamplers', 'PSGetConstantBuffers', 'PSGetShaderResources', 'PSGetSamplers',
            'CSSetConstantBuffers', 'CSSetShaderResources', 'CSSetSamplers', 'CSGetConstantBuffers',
            'CSGetShaderResources', 'CSGetSamplers', 'VSGetShader', 'HSGetShader', 'DSGetShader', 'GSGetShader',
            'PSGetShader', 'CSGetShader', 'GSSetShader', 'HSSetShader', 'DSSetShader', 'CSSetShader',
            'CSSetUnorderedAccessViews', 'CSGetUnorderedAccessViews', 'OMSetRenderTargetsAndUnorderedAccessViews',
            'OMGetRenderTargetsAndUnorderedAccessViews', 'Dispatch', 'DispatchIndirect',
            'ClearUnorderedAccessViewUint', 'ClearUnorderedAccessViewFloat', 'CopyStructureCount', 'SOSetTargets',
            'SOGetTargets', 'DrawAuto', 'Begin', 'End', 'GetData', 'SetPredication', 'GetPredication',
            'GenerateMips', 'SetResourceMinLOD', 'GetResourceMinLOD', 'CopySubresourceRegion1', 'UpdateSubresource1',
            'DiscardResource', 'DiscardView', 'VSSetConstantBuffers1', 'HSSetConstantBuffers1', 'DSSetConstantBuffers1',
            'GSSetConstantBuffers1', 'PSSetConstantBuffers1', 'CSSetConstantBuffers1', 'VSGetConstantBuffers1',
            'HSGetConstantBuffers1', 'DSGetConstantBuffers1', 'GSGetConstantBuffers1', 'PSGetConstantBuffers1',
            'CSGetConstantBuffers1', 'ClearView', 'DiscardView1', 'IsAnnotationEnabled', 'SetMarkerInt',
            'BeginEventInt', 'EndEvent', 'SwapDeviceContextState', 'GetPrivateData', 'SetPrivateData',
            'SetPrivateDataInterface'
        )
    },
    @{
        Class       = 'RxVideoDevice'
        Interfaces  = @('ID3D11VideoDevice')
        Name        = 'videodevice'
        Purpose     = 'ID3D11VideoDevice methods'
        Implemented = @(
            'GetVideoDecoderProfileCount', 'GetVideoDecoderProfile', 'CheckVideoDecoderFormat',
            'GetVideoDecoderConfigCount', 'GetVideoDecoderConfig', 'CreateVideoDecoder',
            'CreateVideoDecoderOutputView', 'CreateVideoProcessor', 'CreateVideoProcessorInputView',
            'CreateVideoProcessorOutputView', 'CreateVideoProcessorEnumerator'
        )
    },
    @{
        Class       = 'RxVideoContext'
        Interfaces  = @('ID3D11DeviceChild', 'ID3D11VideoContext')
        Name        = 'videocontext'
        Purpose     = 'ID3D11VideoContext methods'
        Implemented = @(
            'GetDevice', 'GetPrivateData', 'SetPrivateData', 'SetPrivateDataInterface',
            'GetDecoderBuffer', 'ReleaseDecoderBuffer', 'DecoderBeginFrame', 'DecoderEndFrame',
            'SubmitDecoderBuffers',
            'VideoProcessorSetOutputTargetRect', 'VideoProcessorSetOutputBackgroundColor',
            'VideoProcessorSetOutputColorSpace', 'VideoProcessorSetOutputAlphaFillMode',
            'VideoProcessorSetOutputConstriction', 'VideoProcessorSetOutputStereoMode',
            'VideoProcessorSetOutputExtension', 'VideoProcessorGetOutputTargetRect',
            'VideoProcessorGetOutputBackgroundColor', 'VideoProcessorGetOutputColorSpace',
            'VideoProcessorGetOutputAlphaFillMode', 'VideoProcessorGetOutputConstriction',
            'VideoProcessorGetOutputStereoMode', 'VideoProcessorGetOutputExtension',
            'VideoProcessorSetStreamFrameFormat', 'VideoProcessorSetStreamColorSpace',
            'VideoProcessorSetStreamOutputRate', 'VideoProcessorSetStreamSourceRect',
            'VideoProcessorSetStreamDestRect', 'VideoProcessorSetStreamAlpha', 'VideoProcessorSetStreamPalette',
            'VideoProcessorSetStreamPixelAspectRatio', 'VideoProcessorSetStreamLumaKey',
            'VideoProcessorSetStreamStereoFormat', 'VideoProcessorSetStreamAutoProcessingMode',
            'VideoProcessorSetStreamFilter', 'VideoProcessorSetStreamExtension',
            'VideoProcessorGetStreamFrameFormat', 'VideoProcessorGetStreamColorSpace',
            'VideoProcessorGetStreamOutputRate', 'VideoProcessorGetStreamSourceRect',
            'VideoProcessorGetStreamDestRect', 'VideoProcessorGetStreamAlpha', 'VideoProcessorGetStreamPalette',
            'VideoProcessorGetStreamPixelAspectRatio', 'VideoProcessorGetStreamLumaKey',
            'VideoProcessorGetStreamStereoFormat', 'VideoProcessorGetStreamAutoProcessingMode',
            'VideoProcessorGetStreamFilter', 'VideoProcessorGetStreamExtension', 'VideoProcessorBlt',
            'VideoProcessorSetStreamRotation', 'VideoProcessorGetStreamRotation'
        )
    },
    @{
        Class       = 'RxBuffer'
        Interfaces  = @('ID3D11Resource', 'ID3D11Buffer')
        Include     = 'resource.hpp'
        Name        = 'buffer'
        Purpose     = 'ID3D11Buffer methods'
        Implemented = @('GetType', 'SetEvictionPriority', 'GetEvictionPriority', 'GetDesc')
    },
    @{
        Class       = 'RxVideoDecoder'
        Interfaces  = @('ID3D11VideoDecoder')
        Name        = 'videodecoder'
        Purpose     = 'ID3D11VideoDecoder methods'
        Implemented = @('GetCreationParameters', 'GetDriverHandle')
    },
    @{
        Class       = 'RxVideoDecoderOutputView'
        Interfaces  = @('ID3D11View', 'ID3D11VideoDecoderOutputView')
        Name        = 'videodecoderoutputview'
        Purpose     = 'ID3D11VideoDecoderOutputView methods'
        Implemented = @('GetResource', 'GetDesc')
    },
    @{
        Class       = 'RxVideoProcessorEnumerator'
        Interfaces  = @('ID3D11VideoProcessorEnumerator')
        Include     = 'videoprocessor.hpp'
        Name        = 'videoprocessorenumerator'
        Purpose     = 'ID3D11VideoProcessorEnumerator methods'
        Implemented = @(
            'GetVideoProcessorContentDesc', 'CheckVideoProcessorFormat', 'GetVideoProcessorCaps',
            'GetVideoProcessorRateConversionCaps', 'GetVideoProcessorCustomRate', 'GetVideoProcessorFilterRange'
        )
    },
    @{
        Class       = 'RxVideoProcessor'
        Interfaces  = @('ID3D11VideoProcessor')
        Include     = 'videoprocessor.hpp'
        Name        = 'videoprocessor'
        Purpose     = 'ID3D11VideoProcessor methods'
        Implemented = @('GetContentDesc', 'GetRateConversionCaps')
    },
    @{
        Class       = 'RxVideoProcessorInputView'
        Interfaces  = @('ID3D11View', 'ID3D11VideoProcessorInputView')
        Include     = 'videoprocessor.hpp'
        Name        = 'videoprocessorinputview'
        Purpose     = 'ID3D11VideoProcessorInputView methods'
        Implemented = @('GetResource', 'GetDesc')
    },
    @{
        Class       = 'RxVideoProcessorOutputView'
        Interfaces  = @('ID3D11View', 'ID3D11VideoProcessorOutputView')
        Include     = 'videoprocessor.hpp'
        Name        = 'videoprocessoroutputview'
        Purpose     = 'ID3D11VideoProcessorOutputView methods'
        Implemented = @('GetResource', 'GetDesc')
    },
    @{
        Class       = 'RxDxgiDevice'
        Interfaces  = @('IDXGIObject', 'IDXGIDevice', 'IDXGIDevice1', 'IDXGIDevice2', 'IDXGIDevice3')
        Headers     = @('shared\dxgi.h', 'shared\dxgi1_2.h', 'shared\dxgi1_3.h')
        Include     = 'device.hpp'
        Name        = 'dxgidevice'
        Purpose     = 'IDXGIDevice methods'
        Implemented = @(
            'SetPrivateData', 'SetPrivateDataInterface', 'GetPrivateData', 'GetParent', 'GetAdapter',
            'SetGPUThreadPriority', 'GetGPUThreadPriority', 'SetMaximumFrameLatency', 'GetMaximumFrameLatency',
            'QueryResourceResidency', 'OfferResources', 'ReclaimResources', 'EnqueueSetEvent', 'Trim'
        )
    },
    @{
        Class       = 'RxD3D10Device'
        Interfaces  = @('ID3D10Device', 'ID3D10Device1')
        Headers     = @('um\d3d10.h', 'um\d3d10_1.h')
        Include     = 'd3d10face.hpp'
        Name        = 'd3d10device'
        Purpose     = 'ID3D10Device1 methods'
        Implemented = @(
            'VSSetConstantBuffers', 'PSSetShaderResources', 'PSSetShader', 'PSSetSamplers', 'VSSetShader', 'DrawIndexed',
            'Draw', 'PSSetConstantBuffers', 'IASetInputLayout', 'IASetVertexBuffers', 'IASetIndexBuffer', 'DrawIndexedInstanced',
            'DrawInstanced', 'GSSetConstantBuffers', 'GSSetShader', 'IASetPrimitiveTopology', 'VSSetShaderResources', 'VSSetSamplers',
            'SetPredication', 'GSSetShaderResources', 'GSSetSamplers', 'OMSetRenderTargets', 'OMSetBlendState', 'OMSetDepthStencilState',
            'SOSetTargets', 'DrawAuto', 'RSSetState', 'RSSetViewports', 'RSSetScissorRects', 'CopySubresourceRegion',
            'CopyResource', 'UpdateSubresource', 'ClearRenderTargetView', 'ClearDepthStencilView', 'GenerateMips', 'ResolveSubresource',
            'VSGetConstantBuffers', 'PSGetShaderResources', 'PSGetShader', 'PSGetSamplers', 'VSGetShader', 'PSGetConstantBuffers',
            'IAGetInputLayout', 'IAGetVertexBuffers', 'IAGetIndexBuffer', 'GSGetConstantBuffers', 'GSGetShader', 'IAGetPrimitiveTopology',
            'VSGetShaderResources', 'VSGetSamplers', 'GetPredication', 'GSGetShaderResources', 'GSGetSamplers', 'OMGetRenderTargets',
            'OMGetBlendState', 'OMGetDepthStencilState', 'SOGetTargets', 'RSGetState', 'RSGetViewports', 'RSGetScissorRects',
            'GetDeviceRemovedReason', 'SetExceptionMode', 'GetExceptionMode', 'GetPrivateData', 'SetPrivateData', 'SetPrivateDataInterface',
            'ClearState', 'Flush', 'CreateBuffer', 'CreateTexture1D', 'CreateTexture2D', 'CreateTexture3D',
            'CreateShaderResourceView', 'CreateRenderTargetView', 'CreateDepthStencilView', 'CreateInputLayout', 'CreateVertexShader', 'CreateGeometryShader',
            'CreateGeometryShaderWithStreamOutput', 'CreatePixelShader', 'CreateBlendState', 'CreateDepthStencilState', 'CreateRasterizerState', 'CreateSamplerState',
            'CreateQuery', 'CreatePredicate', 'CreateCounter', 'CheckFormatSupport', 'CheckMultisampleQualityLevels', 'CheckCounterInfo',
            'CheckCounter', 'GetCreationFlags', 'OpenSharedResource', 'SetTextFilterSize', 'GetTextFilterSize', 'CreateShaderResourceView1',
            'CreateBlendState1', 'GetFeatureLevel'
        )
    },
    @{
        Class       = 'RxDevice12'
        Interfaces  = @('ID3D12Object', 'ID3D12Device', 'ID3D12Device1', 'ID3D12Device2', 'ID3D12Device3', 'ID3D12Device4', 'ID3D12Device5', 'ID3D12Device6', 'ID3D12Device7', 'ID3D12Device8', 'ID3D12Device9', 'ID3D12Device10', 'ID3D12Device11', 'ID3D12Device12', 'ID3D12Device13', 'ID3D12Device14')
        Headers     = @('um\d3d12.h')
        Include     = 'device.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'device'
        Purpose     = 'ID3D12Device methods'
        Implemented = @(
            'GetPrivateData', 'SetPrivateData', 'SetPrivateDataInterface', 'SetName', 'GetNodeCount',
            'CreateCommandQueue', 'CreateCommandAllocator', 'CreateCommandList', 'CheckFeatureSupport',
            'CreateDescriptorHeap', 'GetDescriptorHandleIncrementSize', 'CreateRenderTargetView',
            'GetResourceAllocationInfo', 'GetCustomHeapProperties', 'CreateCommittedResource', 'CreateFence',
            'GetDeviceRemovedReason', 'GetCopyableFootprints', 'GetAdapterLuid', 'CreateCommandList1',
            'CreateCommittedResource1', 'GetResourceAllocationInfo1', 'CreateCommittedResource2',
            'GetResourceAllocationInfo2', 'GetCopyableFootprints1'
        )
    },
    @{
        Class       = 'RxCommandQueue'
        Interfaces  = @('ID3D12CommandQueue')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'queue'
        Purpose     = 'ID3D12CommandQueue methods'
        Implemented = @(
            'ExecuteCommandLists', 'SetMarker', 'BeginEvent', 'EndEvent', 'Signal', 'Wait', 'GetTimestampFrequency',
            'GetClockCalibration', 'GetDesc'
        )
    },
    @{
        Class       = 'RxCommandAllocator'
        Interfaces  = @('ID3D12CommandAllocator')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'allocator'
        Purpose     = 'ID3D12CommandAllocator methods'
        Implemented = @(
            'Reset'
        )
    },
    @{
        Class       = 'RxCommandList'
        Interfaces  = @('ID3D12CommandList', 'ID3D12GraphicsCommandList', 'ID3D12GraphicsCommandList1', 'ID3D12GraphicsCommandList2', 'ID3D12GraphicsCommandList3', 'ID3D12GraphicsCommandList4', 'ID3D12GraphicsCommandList5', 'ID3D12GraphicsCommandList6', 'ID3D12GraphicsCommandList7', 'ID3D12GraphicsCommandList8', 'ID3D12GraphicsCommandList9', 'ID3D12GraphicsCommandList10')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'commandlist'
        Purpose     = 'ID3D12GraphicsCommandList methods'
        Implemented = @(
            'GetType', 'Close', 'Reset', 'CopyBufferRegion', 'CopyTextureRegion', 'CopyResource', 'ResourceBarrier',
            'ClearRenderTargetView', 'OMSetRenderTargets', 'SetDescriptorHeaps', 'IASetPrimitiveTopology',
            'RSSetViewports', 'RSSetScissorRects', 'OMSetBlendFactor', 'OMSetStencilRef', 'SetMarker', 'BeginEvent',
            'EndEvent'
        )
    },
    @{
        Class       = 'RxFence'
        Interfaces  = @('ID3D12Fence', 'ID3D12Fence1')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'fence'
        Purpose     = 'ID3D12Fence methods'
        Implemented = @(
            'GetCompletedValue', 'SetEventOnCompletion', 'Signal', 'GetCreationFlags'
        )
    },
    @{
        Class       = 'RxDescriptorHeap'
        Interfaces  = @('ID3D12DescriptorHeap')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'descriptorheap'
        Purpose     = 'ID3D12DescriptorHeap methods'
        Implemented = @(
            'GetDesc', 'GetCPUDescriptorHandleForHeapStart', 'GetGPUDescriptorHandleForHeapStart'
        )
    },
    @{
        Class       = 'RxResource'
        Interfaces  = @('ID3D12Resource', 'ID3D12Resource1', 'ID3D12Resource2')
        Headers     = @('um\d3d12.h')
        Include     = 'objects.hpp'
        Dir         = 'dll\d3d12core'
        Name        = 'resource'
        Purpose     = 'ID3D12Resource methods'
        Implemented = @(
            'Map', 'Unmap', 'GetDesc', 'GetGPUVirtualAddress', 'GetHeapProperties', 'GetProtectedResourceSession',
            'GetDesc1'
        )
    },
    @{
        Class       = 'RxDxgiAdapter'
        Interfaces  = @('IDXGIObject', 'IDXGIAdapter', 'IDXGIAdapter1', 'IDXGIAdapter2', 'IDXGIAdapter3', 'IDXGIAdapter4')
        Headers     = @('shared\dxgi.h', 'shared\dxgi1_2.h', 'shared\dxgi1_4.h', 'shared\dxgi1_6.h')
        Include     = 'dxgiobjects.hpp'
        Dir         = 'dll\dxgi'
        Name        = 'dxgiadapter'
        Purpose     = 'IDXGIAdapter methods'
        Implemented = @(
            'SetPrivateData', 'SetPrivateDataInterface', 'GetPrivateData', 'GetParent', 'EnumOutputs', 'GetDesc',
            'CheckInterfaceSupport', 'GetDesc1', 'GetDesc2', 'GetDesc3', 'QueryVideoMemoryInfo',
            'SetVideoMemoryReservation', 'RegisterVideoMemoryBudgetChangeNotificationEvent',
            'UnregisterVideoMemoryBudgetChangeNotification',
            'RegisterHardwareContentProtectionTeardownStatusEvent',
            'UnregisterHardwareContentProtectionTeardownStatus'
        )
    },
    @{
        Class       = 'RxDxgiOutput'
        Interfaces  = @('IDXGIObject', 'IDXGIOutput', 'IDXGIOutput1', 'IDXGIOutput2', 'IDXGIOutput3', 'IDXGIOutput4',
                        'IDXGIOutput5', 'IDXGIOutput6')
        Headers     = @('shared\dxgi.h', 'shared\dxgi1_2.h', 'shared\dxgi1_3.h', 'shared\dxgi1_4.h', 'shared\dxgi1_5.h',
                        'shared\dxgi1_6.h')
        Include     = 'dxgiobjects.hpp'
        Dir         = 'dll\dxgi'
        Name        = 'dxgioutput'
        Purpose     = 'IDXGIOutput methods'
        Implemented = @(
            'SetPrivateData', 'SetPrivateDataInterface', 'GetPrivateData', 'GetParent', 'GetDesc',
            'GetDisplayModeList', 'FindClosestMatchingMode', 'WaitForVBlank', 'GetDisplayModeList1',
            'FindClosestMatchingMode1', 'GetDesc1'
        )
    },
    @{
        Class       = 'RxDxgiFactory'
        Interfaces  = @('IDXGIObject', 'IDXGIFactory', 'IDXGIFactory1', 'IDXGIFactory2', 'IDXGIFactory3', 'IDXGIFactory4',
                        'IDXGIFactory5', 'IDXGIFactory6', 'IDXGIFactory7')
        Headers     = @('shared\dxgi.h', 'shared\dxgi1_2.h', 'shared\dxgi1_3.h', 'shared\dxgi1_4.h', 'shared\dxgi1_5.h',
                        'shared\dxgi1_6.h')
        Include     = 'dxgiobjects.hpp'
        Dir         = 'dll\dxgi'
        Name        = 'dxgifactory'
        Purpose     = 'IDXGIFactory methods'
        Implemented = @(
            'SetPrivateData', 'SetPrivateDataInterface', 'GetPrivateData', 'GetParent', 'EnumAdapters',
            'MakeWindowAssociation', 'GetWindowAssociation', 'CreateSwapChain', 'EnumAdapters1', 'IsCurrent',
            'IsWindowedStereoEnabled', 'CreateSwapChainForHwnd', 'GetCreationFlags', 'EnumAdapterByLuid',
            'CreateSoftwareAdapter', 'CreateSwapChainForCoreWindow', 'GetSharedResourceAdapterLuid',
            'RegisterStereoStatusWindow', 'RegisterStereoStatusEvent', 'UnregisterStereoStatus',
            'RegisterOcclusionStatusWindow', 'RegisterOcclusionStatusEvent', 'UnregisterOcclusionStatus',
            'CreateSwapChainForComposition', 'EnumWarpAdapter', 'CheckFeatureSupport',
            'EnumAdapterByGpuPreference', 'RegisterAdaptersChangedEvent', 'UnregisterAdaptersChangedEvent'
        )
    },
    @{
        Class       = 'RxSwapChain'
        Interfaces  = @('IDXGIObject', 'IDXGIDeviceSubObject', 'IDXGISwapChain', 'IDXGISwapChain1', 'IDXGISwapChain2',
                        'IDXGISwapChain3', 'IDXGISwapChain4')
        Headers     = @('shared\dxgi.h', 'shared\dxgi1_2.h', 'shared\dxgi1_3.h', 'shared\dxgi1_4.h', 'shared\dxgi1_5.h')
        Include     = 'swapchain.hpp'
        Dir         = 'dll\dxgi'
        Name        = 'swapchain'
        Purpose     = 'IDXGISwapChain methods'
        Implemented = @(
            'SetPrivateData', 'SetPrivateDataInterface', 'GetPrivateData', 'GetParent', 'GetDevice', 'Present',
            'GetBuffer', 'SetFullscreenState', 'GetFullscreenState', 'GetDesc', 'ResizeBuffers',
            'GetContainingOutput', 'GetLastPresentCount', 'GetDesc1', 'GetFullscreenDesc', 'GetHwnd',
            'GetCoreWindow', 'Present1', 'IsTemporaryMonoSupported', 'GetRestrictToOutput', 'SetBackgroundColor',
            'GetBackgroundColor', 'SetRotation', 'GetRotation', 'SetMaximumFrameLatency', 'GetMaximumFrameLatency',
            'GetFrameLatencyWaitableObject', 'GetCurrentBackBufferIndex', 'ResizeTarget', 'GetFrameStatistics',
            'SetSourceSize', 'GetSourceSize', 'SetMatrixTransform', 'GetMatrixTransform', 'CheckColorSpaceSupport',
            'SetColorSpace1', 'ResizeBuffers1', 'SetHDRMetaData'
        )
    },
    @{
        Class       = 'RxWriteFactory'
        Interfaces  = @('IDWriteFactory', 'IDWriteFactory1', 'IDWriteFactory2', 'IDWriteFactory3', 'IDWriteFactory4',
                        'IDWriteFactory5', 'IDWriteFactory6', 'IDWriteFactory7')
        Headers     = @('um\dwrite.h', 'um\dwrite_1.h', 'um\dwrite_2.h', 'um\dwrite_3.h')
        Include     = 'factory.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'factory'
        Purpose     = 'IDWriteFactory methods'
        Implemented = @(
            'CreateFontFileReference', 'CreateCustomFontFileReference', 'RegisterFontFileLoader',
            'UnregisterFontFileLoader', 'RegisterFontCollectionLoader', 'UnregisterFontCollectionLoader', 'CreateFontFace', 'CreateRenderingParams',
            'CreateMonitorRenderingParams', 'CreateCustomRenderingParams', 'CreateGlyphRunAnalysis',
            'GetSystemFontCollection', 'CreateTextFormat',
            'CreateTextLayout', 'CreateGdiCompatibleTextLayout', 'CreateEllipsisTrimmingSign', 'CreateTypography'
        )
    },
    @{
        Class       = 'RxFontFace'
        Interfaces  = @('IDWriteFontFace', 'IDWriteFontFace1', 'IDWriteFontFace2', 'IDWriteFontFace3', 'IDWriteFontFace4',
                        'IDWriteFontFace5')
        Headers     = @('um\dwrite.h', 'um\dwrite_1.h', 'um\dwrite_2.h', 'um\dwrite_3.h')
        Include     = 'fontface.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'fontface'
        Purpose     = 'IDWriteFontFace methods'
        Implemented = @(
            'GetType', 'GetFiles', 'GetIndex', 'GetSimulations', 'IsSymbolFont', 'GetMetrics', 'GetGlyphCount',
            'GetDesignGlyphMetrics', 'GetGlyphIndices', 'TryGetFontTable', 'ReleaseFontTable', 'IsMonospacedFont',
            'GetDesignGlyphAdvances', 'GetCaretMetrics', 'HasKerningPairs', 'GetKerningPairAdjustments',
            'IsColorFont', 'GetColorPaletteCount', 'GetPaletteEntryCount', 'GetWeight', 'GetStretch', 'GetStyle',
            'GetPanose', 'IsCharacterLocal', 'HasCharacter', 'GetGlyphImageFormats', 'IsGlyphLocal', 'AreGlyphsLocal',
            'AreCharactersLocal', 'GetFontAxisValueCount', 'HasVariations', 'Equals', 'GetRecommendedRenderingMode'
        )
    },
    @{
        Class       = 'RxRenderingParams'
        Interfaces  = @('IDWriteRenderingParams', 'IDWriteRenderingParams1', 'IDWriteRenderingParams2',
                        'IDWriteRenderingParams3')
        Headers     = @('um\dwrite.h', 'um\dwrite_1.h', 'um\dwrite_2.h', 'um\dwrite_3.h')
        Include     = 'params.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'params'
        Purpose     = 'IDWriteRenderingParams methods'
        Implemented = @(
            'GetGamma', 'GetEnhancedContrast', 'GetClearTypeLevel', 'GetPixelGeometry', 'GetRenderingMode',
            'GetGrayscaleEnhancedContrast', 'GetGridFitMode', 'GetRenderingMode1'
        )
    },
    @{
        Class       = 'RxFontCollection'
        Interfaces  = @('IDWriteFontCollection')
        Headers     = @('um\dwrite.h')
        Include     = 'collection.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'collection'
        Purpose     = 'IDWriteFontCollection methods'
        Implemented = @('GetFontFamilyCount', 'GetFontFamily', 'FindFamilyName', 'GetFontFromFontFace')
    },
    @{
        Class       = 'RxFontFamily'
        Interfaces  = @('IDWriteFontList', 'IDWriteFontFamily')
        Headers     = @('um\dwrite.h')
        Include     = 'collection.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'family'
        Purpose     = 'IDWriteFontFamily methods'
        Implemented = @('GetFontCollection', 'GetFontCount', 'GetFont', 'GetFamilyNames', 'GetFirstMatchingFont',
                        'GetMatchingFonts')
    },
    @{
        Class       = 'RxFont'
        Interfaces  = @('IDWriteFont')
        Headers     = @('um\dwrite.h')
        Include     = 'collection.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'font'
        Purpose     = 'IDWriteFont methods'
        Implemented = @('GetFontFamily', 'GetWeight', 'GetStretch', 'GetStyle', 'IsSymbolFont', 'GetFaceNames',
                        'GetInformationalStrings', 'GetSimulations', 'GetMetrics', 'HasCharacter', 'CreateFontFace')
    },
    @{
        Class       = 'RxTextFormat'
        Interfaces  = @('IDWriteTextFormat', 'IDWriteTextFormat1')
        Headers     = @('um\dwrite.h', 'um\dwrite_1.h', 'um\dwrite_2.h')
        Include     = 'textformat.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'textformat'
        Purpose     = 'IDWriteTextFormat methods'
        Implemented = @(
            'SetTextAlignment', 'SetParagraphAlignment', 'SetWordWrapping', 'SetReadingDirection', 'SetFlowDirection',
            'SetIncrementalTabStop', 'SetTrimming', 'SetLineSpacing', 'GetTextAlignment', 'GetParagraphAlignment',
            'GetWordWrapping', 'GetReadingDirection', 'GetFlowDirection', 'GetIncrementalTabStop', 'GetTrimming',
            'GetLineSpacing', 'GetFontCollection', 'GetFontFamilyNameLength', 'GetFontFamilyName', 'GetFontWeight',
            'GetFontStyle', 'GetFontStretch', 'GetFontSize', 'GetLocaleNameLength', 'GetLocaleName',
            'SetVerticalGlyphOrientation', 'GetVerticalGlyphOrientation', 'SetLastLineWrapping', 'GetLastLineWrapping',
            'SetOpticalAlignment', 'GetOpticalAlignment', 'SetFontFallback', 'GetFontFallback'
        )
    },
    @{
        Class       = 'RxTextLayout'
        Interfaces  = @('IDWriteTextFormat', 'IDWriteTextLayout', 'IDWriteTextLayout1', 'IDWriteTextLayout2')
        Headers     = @('um\dwrite.h', 'um\dwrite_1.h', 'um\dwrite_2.h')
        Include     = 'textlayout.hpp'
        Dir         = 'dll\dwrite'
        Name        = 'textlayout'
        Purpose     = 'IDWriteTextLayout methods'
        Implemented = @(
            'SetTextAlignment', 'SetParagraphAlignment', 'SetWordWrapping', 'SetReadingDirection', 'SetFlowDirection',
            'SetIncrementalTabStop', 'SetTrimming', 'SetLineSpacing', 'GetTextAlignment', 'GetParagraphAlignment',
            'GetWordWrapping', 'GetReadingDirection', 'GetFlowDirection', 'GetIncrementalTabStop', 'GetTrimming',
            'GetLineSpacing', 'GetFontCollection', 'GetFontFamilyNameLength', 'GetFontFamilyName', 'GetFontWeight',
            'GetFontStyle', 'GetFontStretch', 'GetFontSize', 'GetLocaleNameLength', 'GetLocaleName',
            'SetVerticalGlyphOrientation', 'GetVerticalGlyphOrientation', 'SetLastLineWrapping', 'GetLastLineWrapping',
            'SetOpticalAlignment', 'GetOpticalAlignment', 'SetFontFallback', 'GetFontFallback',
            'SetMaxWidth', 'SetMaxHeight', 'SetFontCollection', 'SetFontFamilyName', 'SetFontWeight', 'SetFontStyle',
            'SetFontStretch', 'SetFontSize', 'SetUnderline', 'SetStrikethrough', 'SetDrawingEffect', 'SetInlineObject',
            'SetTypography', 'SetLocaleName', 'GetMaxWidth', 'GetMaxHeight', 'GetUnderline', 'GetStrikethrough',
            'GetDrawingEffect', 'GetInlineObject', 'GetTypography', 'Draw', 'GetLineMetrics', 'GetMetrics',
            'GetOverhangMetrics', 'GetClusterMetrics', 'DetermineMinWidth', 'HitTestPoint', 'HitTestTextPosition',
            'HitTestTextRange', 'SetPairKerning', 'GetPairKerning', 'SetCharacterSpacing', 'GetCharacterSpacing'
        )
    }
)

foreach ($Entry in $Classes) {
    if ($Only -and $Only -notcontains $Entry.Dir) { continue }
    $Headers = if ($Entry.Headers) { $Entry.Headers } else { @('um\d3d11.h') }
    $Include = if ($Entry.Include) { $Entry.Include } else { 'device.hpp' }
    $Out = Join-Path $Root $(if ($Entry.Dir) { $Entry.Dir } else { $DefaultDir })
    $Arguments = @(
        '-I', $Generator,
        '--class', $Entry.Class,
        '--decl', (Join-Path $Out "$($Entry.Name).inl"),
        '--stubs', (Join-Path $Out "$($Entry.Name)_stubs.cpp"),
        '--include', $Include,
        '--purpose', $Entry.Purpose,
        '--project', $(switch ($Entry.Dir) { 'dll\dxgi' { 'ReactX DXGI runtime' } 'dll\d3d12core' { 'ReactX Direct3D 12 runtime' } 'dll\dwrite' { 'ReactX DirectWrite' } 'dll\d2d1' { 'ReactX Direct2D' } default { 'ReactX Direct3D 11 runtime' } })
    )
    if ($Entry.Implemented.Count -gt 0) {
        $Arguments += @('--implemented', ($Entry.Implemented -join ','))
    }
    foreach ($Header in $Headers) {
        $Arguments += @('--header', (Join-Path $HeaderDir $Header))
    }
    foreach ($Interface in $Entry.Interfaces) {
        $Arguments += @('--interface', $Interface)
    }

    & python @Arguments
    if ($LASTEXITCODE -ne 0) { throw "gencom.py failed for $($Entry.Class)" }
}
