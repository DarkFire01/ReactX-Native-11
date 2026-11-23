/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     A device on the D3D11 on 12 layer and the D3D12 objects behind it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* D3D12 device interface the runtime keeps next to the device for private queries */
static const GUID RxD3D12PrivateQueryIid = { 0x8f1c0e3c, 0xfae3, 0x4a82, { 0xb0, 0x98, 0xbf, 0xe1, 0x70, 0x82, 0x07, 0xff } };

/* Private data a D3D12 device keeps its kernel device handle under */
static const GUID RxD3D12KernelDeviceKey = { 0x1a20c5df, 0x8f65, 0x405a, { 0x99, 0x82, 0x20, 0x58, 0x83, 0x9f, 0x7b, 0x27 } };

/* D3D12 device interface asked for when the device finishes construction */
static const GUID RxD3D12ConstructionIid = { 0xde18ef3a, 0x2089, 0x4936, { 0xa3, 0xf3, 0xec, 0x78, 0x7a, 0xc6, 0xa4, 0x0d } };

/* The formats a new device caches support for, in the order it asks */
static constexpr DXGI_FORMAT RxCachedFormats[] =
{
    DXGI_FORMAT_R32G32B32A32_TYPELESS, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT,
    DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R32G32B32_TYPELESS, DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R16G16B16A16_TYPELESS,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UINT,
    DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R32G32_TYPELESS,
    DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G8X24_TYPELESS,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,
    DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT,
    DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS,
    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UINT,
    DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R16G16_TYPELESS, DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_SINT,
    DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT,
    DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT, DXGI_FORMAT_R8G8_TYPELESS,
    DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R8G8_SINT,
    DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM,
    DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R8_TYPELESS,
    DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_R8_SINT, DXGI_FORMAT_BC1_TYPELESS,
    DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC2_TYPELESS, DXGI_FORMAT_BC2_UNORM,
    DXGI_FORMAT_BC2_UNORM_SRGB, DXGI_FORMAT_BC3_TYPELESS, DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_BC3_UNORM_SRGB,
    DXGI_FORMAT_BC4_TYPELESS, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC5_TYPELESS,
    DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM, DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8A8_UNORM,
    DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8X8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM,
    DXGI_FORMAT_B8G8R8X8_UNORM_SRGB, DXGI_FORMAT_BC6H_TYPELESS, DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_SF16,
    DXGI_FORMAT_BC7_TYPELESS, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB,
};

VOID
RxDevice::WarmFormatCaches()
{
    UINT Quality;
    UINT Count;

    for (DXGI_FORMAT Format : RxCachedFormats)
    {
        GetDriverFormatCaps(Format);

        for (Count = 2; Count < D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; Count++)
        {
            Quality = 0;
            DeviceFuncs.pfnCheckMultisampleQualityLevels(DriverDevice, Format, Count, 0, &Quality);
        }
    }
}

HRESULT
RxDevice::AttachLayer()
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options = {};
    D3D12_FEATURE_DATA_SHADER_MODEL Model = { D3D_SHADER_MODEL_6_2 };
    ID3D12Device* Base = nullptr;
    IUnknown* Construction = nullptr;
    D3DKMT_HANDLE KernelDevice = 0;
    UINT Size = sizeof(KernelDevice);
    HRESULT hr;

    /* The layer's device starts with the layer interface */
    LayerDevice = static_cast<IOn12DdiDevice*>(DriverDevice.pDrvPrivate);

    hr = LayerDevice->GetD3D12Device(IID_PPV_ARGS(&LayerDevice12));
    if (FAILED(hr))
    {
        RX_TRACE(L"the layer has no ID3D12Device5 (0x%08lX)", hr);
        return hr;
    }

    hr = LayerDevice12->QueryInterface(RxD3D12PrivateQueryIid, reinterpret_cast<void**>(&LayerPrivate));
    if (FAILED(hr))
    {
        RX_TRACE(L"the D3D12 device has no private query interface (0x%08lX)", hr);
        return hr;
    }

    hr = LayerDevice12->GetPrivateData(RxD3D12KernelDeviceKey, &Size, &KernelDevice);
    if (FAILED(hr))
    {
        RX_TRACE(L"the D3D12 device has no kernel device (0x%08lX)", hr);
        return hr;
    }

    hr = LayerDevice->GetGraphicsQueue(IID_PPV_ARGS(&LayerQueue));
    if (FAILED(hr))
    {
        RX_TRACE(L"the layer has no graphics queue (0x%08lX)", hr);
        return hr;
    }

    hr = LayerDevice12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &Options, sizeof(Options));
    if (FAILED(hr))
        return hr;

    WarmFormatCaches();

    if (SUCCEEDED(LayerDevice12->QueryInterface(RxD3D12ConstructionIid, reinterpret_cast<void**>(&Construction))))
        Construction->Release();

    if (SUCCEEDED(LayerDevice12->QueryInterface(IID_PPV_ARGS(&Base))))
    {
        Base->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &Model, sizeof(Model));
        Base->Release();
    }

    return S_OK;
}
