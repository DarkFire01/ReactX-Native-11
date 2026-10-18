/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Argument checks for texture creation, done before the driver sees anything
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"

/* Every texture desc flattened into one shape; Depth is 1 below 3D */
struct RxTextureRequest
{
    D3D11_RESOURCE_DIMENSION Dimension;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D11_USAGE Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
};

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE1D_DESC& Desc);

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE2D_DESC& Desc);

RxTextureRequest
RxMakeTextureRequest(
    _In_ const D3D11_TEXTURE3D_DESC& Desc);

/* E_INVALIDARG for anything the API would refuse, S_OK otherwise */
HRESULT
RxCheckTextureRequest(
    _In_ RxDevice& Device,
    _In_ const RxTextureRequest& Request,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData);
