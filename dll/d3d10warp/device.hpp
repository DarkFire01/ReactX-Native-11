/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device object and the driver side objects it creates
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "format.hpp"

class RxWarpAdapter;

/* One mip level of one array slice, inside the resource's memory */
typedef struct _RX_WARP_SUBRESOURCE
{
    SIZE_T Offset;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT RowPitch;
    UINT DepthPitch;
    SIZE_T Size;
} RX_WARP_SUBRESOURCE;

class RxWarpResource
{
public:
    ~RxWarpResource();

    HRESULT
    Initialize(
        _In_ const D3D11DDIARG_CREATERESOURCE *Args);

    RX_WARP_SUBRESOURCE *
    Subresource(
        _In_ UINT Index)
    {
        return (Index < SubresourceCount) ? &Subresources[Index] : nullptr;
    }

    BYTE *
    Data(
        _In_ UINT Index)
    {
        return Memory + Subresources[Index].Offset;
    }

    D3D10DDIRESOURCE_TYPE Dimension = D3D10DDIRESOURCE_BUFFER;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    UINT Usage = 0;
    UINT BindFlags = 0;
    UINT MapFlags = 0;
    UINT MiscFlags = 0;
    UINT MipLevels = 1;
    UINT ArraySize = 1;
    UINT SampleCount = 1;
    UINT ByteStride = 0;
    BYTE *Memory = nullptr;
    SIZE_T MemorySize = 0;
    UINT SubresourceCount = 0;
    RX_WARP_SUBRESOURCE *Subresources = nullptr;
};

/* Render target, depth stencil, shader resource and unordered access views share one shape */
class RxWarpView
{
public:
    RxWarpResource *Resource = nullptr;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    D3D10DDIRESOURCE_TYPE Dimension = D3D10DDIRESOURCE_BUFFER;
    UINT MipSlice = 0;
    UINT MipCount = 1;
    UINT FirstSlice = 0;
    UINT SliceCount = 1;
    UINT FirstElement = 0;
    UINT ElementCount = 0;
    UINT Flags = 0;
};

class RxWarpDevice
{
public:
    static
    HRESULT
    Create(
        _In_ RxWarpAdapter *Adapter,
        _Inout_ D3D10DDIARG_CREATEDEVICE *Args);

    /* Reports an error to the runtime the way the DDI asks: through pfnSetErrorCb */
    VOID
    SetError(
        _In_ HRESULT Error);

    RxWarpAdapter *Adapter = nullptr;
    D3D10DDI_HRTDEVICE RuntimeDevice = {};
    D3D10DDI_HRTCORELAYER RuntimeCoreLayer = {};
    const D3DDDI_DEVICECALLBACKS *KernelCallbacks = nullptr;
    const D3D11DDI_CORELAYER_DEVICECALLBACKS *CoreCallbacks = nullptr;
    UINT Interface = 0;
    UINT Version = 0;
};

/* DDI entry points, grouped by the file that implements them */
namespace RxWarpDdi
{
    /* resource.cpp */
    VOID APIENTRY CreateResource(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATERESOURCE *, D3D10DDI_HRESOURCE, D3D10DDI_HRTRESOURCE);
    VOID APIENTRY OpenResource(D3D10DDI_HDEVICE, const D3D10DDIARG_OPENRESOURCE *, D3D10DDI_HRESOURCE, D3D10DDI_HRTRESOURCE);
    VOID APIENTRY DestroyResource(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceMap(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, D3D10_DDI_MAP, UINT, D3D10DDI_MAPPED_SUBRESOURCE *);
    VOID APIENTRY ResourceUnmap(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT);
    BOOL APIENTRY ResourceIsStagingBusy(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceCopy(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, D3D10DDI_HRESOURCE);
    VOID APIENTRY ResourceCopyRegion(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, UINT, UINT, UINT, D3D10DDI_HRESOURCE, UINT, const D3D10_DDI_BOX *, UINT);
    VOID APIENTRY ResourceUpdateSubresourceUP(D3D10DDI_HDEVICE, D3D10DDI_HRESOURCE, UINT, const D3D10_DDI_BOX *, const VOID *, UINT, UINT, UINT);

    /* view.cpp */
    VOID APIENTRY CreateRenderTargetView(D3D10DDI_HDEVICE, const D3D10DDIARG_CREATERENDERTARGETVIEW *, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW);
    VOID APIENTRY CreateRenderTargetView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *, D3D10DDI_HRENDERTARGETVIEW, D3D10DDI_HRTRENDERTARGETVIEW);
    VOID APIENTRY DestroyRenderTargetView(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW);
    VOID APIENTRY CreateDepthStencilView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *, D3D10DDI_HDEPTHSTENCILVIEW, D3D10DDI_HRTDEPTHSTENCILVIEW);
    VOID APIENTRY DestroyDepthStencilView(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILVIEW);
    VOID APIENTRY CreateShaderResourceView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATESHADERRESOURCEVIEW *, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW);
    VOID APIENTRY CreateShaderResourceView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *, D3D10DDI_HSHADERRESOURCEVIEW, D3D10DDI_HRTSHADERRESOURCEVIEW);
    VOID APIENTRY DestroyShaderResourceView(D3D10DDI_HDEVICE, D3D10DDI_HSHADERRESOURCEVIEW);
    VOID APIENTRY CreateUnorderedAccessView(D3D10DDI_HDEVICE, const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW);
    VOID APIENTRY CreateUnorderedAccessView2(D3D10DDI_HDEVICE, const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *, D3D11DDI_HUNORDEREDACCESSVIEW, D3D11DDI_HRTUNORDEREDACCESSVIEW);
    VOID APIENTRY DestroyUnorderedAccessView(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW);

    /* clear.cpp */
    VOID APIENTRY ClearRenderTargetView(D3D10DDI_HDEVICE, D3D10DDI_HRENDERTARGETVIEW, FLOAT[4]);
    VOID APIENTRY ClearDepthStencilView(D3D10DDI_HDEVICE, D3D10DDI_HDEPTHSTENCILVIEW, UINT, FLOAT, UINT8);
    VOID APIENTRY ClearUnorderedAccessViewUint(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const UINT[4]);
    VOID APIENTRY ClearUnorderedAccessViewFloat(D3D10DDI_HDEVICE, D3D11DDI_HUNORDEREDACCESSVIEW, const FLOAT[4]);
    VOID APIENTRY ClearView(D3D10DDI_HDEVICE, D3D11DDI_HANDLETYPE, VOID *, const FLOAT[4], const D3D10_DDI_RECT *, UINT);
}
