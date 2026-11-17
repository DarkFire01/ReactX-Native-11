/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Render target, depth stencil, shader resource and unordered access views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

template <typename Handle>
static
RxWarpView *
RxWarpNewView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ Handle View,
    _In_ D3D10DDI_HRESOURCE Resource,
    _In_ DXGI_FORMAT Format,
    _In_ D3D10DDIRESOURCE_TYPE Dimension)
{
    RxWarpView *Object = new (std::nothrow) RxWarpView();

    if (Object == nullptr)
    {
        RxWarpObject<RxWarpDevice>(Device)->SetError(E_OUTOFMEMORY);
    }
    else
    {
        Object->Resource = RxWarpObject<RxWarpResource>(Resource);
        Object->Format = Format;
        Object->Dimension = Dimension;
    }
    RxWarpBind(View, Object);
    return Object;
}

/* Render target and unordered access views: one mip level and a range of slices */
template <typename Arguments, typename Handle>
static
VOID
RxWarpTargetView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const Arguments *Args,
    _In_ Handle View)
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpNewView(Device, View, Args->hDrvResource, Args->Format, Args->ResourceDimension);

    if (Object == nullptr)
        return;

    switch (Args->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            Object->FirstElement = Args->Buffer.FirstElement;
            Object->ElementCount = Args->Buffer.NumElements;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            Object->MipSlice = Args->Tex1D.MipSlice;
            Object->FirstSlice = Args->Tex1D.FirstArraySlice;
            Object->SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            Object->MipSlice = Args->Tex3D.MipSlice;
            Object->FirstSlice = Args->Tex3D.FirstW;
            Object->SliceCount = Args->Tex3D.WSize;
            break;
        default:
            Object->MipSlice = Args->Tex2D.MipSlice;
            Object->FirstSlice = Args->Tex2D.FirstArraySlice;
            Object->SliceCount = Args->Tex2D.ArraySize;
            break;
    }
}

template <typename Arguments>
static
VOID
RxWarpShaderView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const Arguments *Args,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW View)
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpNewView(Device, View, Args->hDrvResource, Args->Format, Args->ResourceDimension);

    if (Object == nullptr)
        return;

    switch (Args->ResourceDimension)
    {
        case D3D10DDIRESOURCE_BUFFER:
            Object->FirstElement = Args->Buffer.FirstElement;
            Object->ElementCount = Args->Buffer.NumElements;
            break;
        case D3D11DDIRESOURCE_BUFFEREX:
            Object->FirstElement = Args->BufferEx.FirstElement;
            Object->ElementCount = Args->BufferEx.NumElements;
            Object->Flags = Args->BufferEx.Flags;
            break;
        case D3D10DDIRESOURCE_TEXTURE1D:
            Object->MipSlice = Args->Tex1D.MostDetailedMip;
            Object->MipCount = Args->Tex1D.MipLevels;
            Object->FirstSlice = Args->Tex1D.FirstArraySlice;
            Object->SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            Object->MipSlice = Args->Tex3D.MostDetailedMip;
            Object->MipCount = Args->Tex3D.MipLevels;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            Object->MipSlice = Args->TexCube.MostDetailedMip;
            Object->MipCount = Args->TexCube.MipLevels;
            Object->FirstSlice = Args->TexCube.First2DArrayFace;
            Object->SliceCount = Args->TexCube.NumCubes * 6;
            break;
        default:
            Object->MipSlice = Args->Tex2D.MostDetailedMip;
            Object->MipCount = Args->Tex2D.MipLevels;
            Object->FirstSlice = Args->Tex2D.FirstArraySlice;
            Object->SliceCount = Args->Tex2D.ArraySize;
            break;
    }
}

template <typename Handle>
static
VOID
RxWarpDestroyView(
    _In_ Handle View)
{
    RxWarpLock Lock;

    delete RxWarpObject<RxWarpView>(View);
    RxWarpBind(View, (RxWarpView *)nullptr);
}

VOID
APIENTRY
RxWarpDdi::CreateRenderTargetView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D10DDIARG_CREATERENDERTARGETVIEW *Args,
    _In_ D3D10DDI_HRENDERTARGETVIEW View,
    _In_ D3D10DDI_HRTRENDERTARGETVIEW RuntimeView)
{
    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpTargetView(Device, Args, View);
}

VOID
APIENTRY
RxWarpDdi::CreateRenderTargetView2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDIARG_CREATERENDERTARGETVIEW *Args,
    _In_ D3D10DDI_HRENDERTARGETVIEW View,
    _In_ D3D10DDI_HRTRENDERTARGETVIEW RuntimeView)
{
    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpTargetView(Device, Args, View);
}

VOID
APIENTRY
RxWarpDdi::DestroyRenderTargetView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRENDERTARGETVIEW View)
{
    UNREFERENCED_PARAMETER(Device);
    RxWarpDestroyView(View);
}

VOID
APIENTRY
RxWarpDdi::CreateDepthStencilView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11DDIARG_CREATEDEPTHSTENCILVIEW *Args,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW View,
    _In_ D3D10DDI_HRTDEPTHSTENCILVIEW RuntimeView)
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpNewView(Device, View, Args->hDrvResource, Args->Format, Args->ResourceDimension);

    UNREFERENCED_PARAMETER(RuntimeView);
    if (Object == nullptr)
        return;

    Object->Flags = Args->Flags;
    switch (Args->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            Object->MipSlice = Args->Tex1D.MipSlice;
            Object->FirstSlice = Args->Tex1D.FirstArraySlice;
            Object->SliceCount = Args->Tex1D.ArraySize;
            break;
        case D3D10DDIRESOURCE_TEXTURECUBE:
            Object->MipSlice = Args->TexCube.MipSlice;
            Object->FirstSlice = Args->TexCube.FirstArraySlice;
            Object->SliceCount = Args->TexCube.ArraySize;
            break;
        default:
            Object->MipSlice = Args->Tex2D.MipSlice;
            Object->FirstSlice = Args->Tex2D.FirstArraySlice;
            Object->SliceCount = Args->Tex2D.ArraySize;
            break;
    }
}

VOID
APIENTRY
RxWarpDdi::DestroyDepthStencilView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW View)
{
    UNREFERENCED_PARAMETER(Device);
    RxWarpDestroyView(View);
}

VOID
APIENTRY
RxWarpDdi::CreateShaderResourceView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11DDIARG_CREATESHADERRESOURCEVIEW *Args,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW View,
    _In_ D3D10DDI_HRTSHADERRESOURCEVIEW RuntimeView)
{
    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpShaderView(Device, Args, View);
}

VOID
APIENTRY
RxWarpDdi::CreateShaderResourceView2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDIARG_CREATESHADERRESOURCEVIEW *Args,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW View,
    _In_ D3D10DDI_HRTSHADERRESOURCEVIEW RuntimeView)
{
    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpShaderView(Device, Args, View);
}

VOID
APIENTRY
RxWarpDdi::DestroyShaderResourceView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW View)
{
    UNREFERENCED_PARAMETER(Device);
    RxWarpDestroyView(View);
}

VOID
APIENTRY
RxWarpDdi::CreateUnorderedAccessView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3D11DDIARG_CREATEUNORDEREDACCESSVIEW *Args,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW View,
    _In_ D3D11DDI_HRTUNORDEREDACCESSVIEW RuntimeView)
{
    RxWarpLock Lock;
    RxWarpView *Object;

    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpTargetView(Device, Args, View);
    Object = RxWarpObject<RxWarpView>(View);
    if (Object != nullptr && Args->ResourceDimension == D3D10DDIRESOURCE_BUFFER)
        Object->Flags = Args->Buffer.Flags;
}

VOID
APIENTRY
RxWarpDdi::CreateUnorderedAccessView2(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ const D3DWDDM2_0DDIARG_CREATEUNORDEREDACCESSVIEW *Args,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW View,
    _In_ D3D11DDI_HRTUNORDEREDACCESSVIEW RuntimeView)
{
    RxWarpLock Lock;
    RxWarpView *Object;

    UNREFERENCED_PARAMETER(RuntimeView);
    RxWarpTargetView(Device, Args, View);
    Object = RxWarpObject<RxWarpView>(View);
    if (Object != nullptr && Args->ResourceDimension == D3D10DDIRESOURCE_BUFFER)
        Object->Flags = Args->Buffer.Flags;
}

VOID
APIENTRY
RxWarpDdi::DestroyUnorderedAccessView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW View)
{
    UNREFERENCED_PARAMETER(Device);
    RxWarpDestroyView(View);
}
