/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline views over buffers and textures of every dimension
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "views.hpp"
#include "formats.hpp"

#include <type_traits>

BOOLEAN
RxIsTypelessFormat(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_R32G32B32A32_TYPELESS:
        case DXGI_FORMAT_R32G32B32_TYPELESS:
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        case DXGI_FORMAT_R32G32_TYPELESS:
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R16G16_TYPELESS:
        case DXGI_FORMAT_R32_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:
        case DXGI_FORMAT_R8G8_TYPELESS:
        case DXGI_FORMAT_R16_TYPELESS:
        case DXGI_FORMAT_R8_TYPELESS:
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_TYPELESS:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC7_TYPELESS:
            return TRUE;
        default:
            return FALSE;
    }
}

/* Counts of -1 mean everything from the first one on */
static
UINT
RxRemaining(
    _In_ UINT Requested,
    _In_ UINT First,
    _In_ UINT Available)
{
    if (Requested != (UINT)-1)
        return Requested;
    return (First < Available) ? Available - First : 0;
}

static
BOOLEAN
RxRangeFits(
    _In_ UINT First,
    _In_ UINT Count,
    _In_ UINT Available)
{
    return Count != 0 && First < Available && Count <= Available - First;
}

/* Elements a buffer view covers when none are named: whole structures */
static
UINT
RxStructureCount(
    _In_ const RxResourceInfo& Resource)
{
    return Resource.StructureByteStride ? Resource.Width / Resource.StructureByteStride : 0;
}

/* Render target views */

HRESULT
RxRenderTargetTraits::Resolve(
    _In_ const RxResourceInfo& Resource,
    _In_opt_ const ApiDesc* Requested,
    _Out_ ApiDesc* Resolved,
    _Out_ DdiArgs* Args)
{
    ApiDesc Desc = {};
    UINT Mip = 0;
    UINT First = 0;
    UINT Count = 1;

    *Args = {};
    if (Requested != nullptr)
    {
        Desc = *Requested;
    }
    else
    {
        /* Without a desc the view is mip 0 of everything, in the resource's format */
        Desc.Format = Resource.Format;
        switch (Resource.Dimension)
        {
            case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY
                                                              : D3D11_RTV_DIMENSION_TEXTURE1D;
                Desc.Texture1DArray.ArraySize = Resource.ArraySize;
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
                if (Resource.SampleCount > 1)
                    Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY
                                                                  : D3D11_RTV_DIMENSION_TEXTURE2DMS;
                else
                    Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY
                                                                  : D3D11_RTV_DIMENSION_TEXTURE2D;
                Desc.Texture2DArray.ArraySize = Resource.ArraySize;
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
                Desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
                Desc.Texture3D.WSize = (UINT)-1;
                break;
            default:
                return E_INVALIDARG;
        }
    }

    if (Desc.Format == DXGI_FORMAT_UNKNOWN)
        Desc.Format = Resource.Format;
    if (Desc.Format == DXGI_FORMAT_UNKNOWN || RxIsTypelessFormat(Desc.Format))
        return E_INVALIDARG;

    Args->Format = Desc.Format;
    switch (Desc.ViewDimension)
    {
        case D3D11_RTV_DIMENSION_BUFFER:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
                return E_INVALIDARG;
            Args->ResourceDimension = D3D10DDIRESOURCE_BUFFER;
            Args->Buffer.FirstElement = Desc.Buffer.FirstElement;
            Args->Buffer.NumElements = Desc.Buffer.NumElements;
            *Resolved = Desc;
            return S_OK;

        case D3D11_RTV_DIMENSION_TEXTURE1D:
        case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE1D)
                return E_INVALIDARG;
            Mip = Desc.Texture1DArray.MipSlice;
            if (Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE1DARRAY)
            {
                First = Desc.Texture1DArray.FirstArraySlice;
                Count = Desc.Texture1DArray.ArraySize = RxRemaining(Desc.Texture1DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE1D;
            Args->Tex1D = { Mip, First, Count };
            break;

        case D3D11_RTV_DIMENSION_TEXTURE2D:
        case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        case D3D11_RTV_DIMENSION_TEXTURE2DMS:
        case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                return E_INVALIDARG;
            if ((Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMS ||
                 Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY) != (Resource.SampleCount > 1))
            {
                return E_INVALIDARG;
            }

            if (Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2D)
            {
                Mip = Desc.Texture2D.MipSlice;
            }
            else if (Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DARRAY)
            {
                Mip = Desc.Texture2DArray.MipSlice;
                First = Desc.Texture2DArray.FirstArraySlice;
                Count = Desc.Texture2DArray.ArraySize = RxRemaining(Desc.Texture2DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            else if (Desc.ViewDimension == D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY)
            {
                First = Desc.Texture2DMSArray.FirstArraySlice;
                Count = Desc.Texture2DMSArray.ArraySize = RxRemaining(Desc.Texture2DMSArray.ArraySize, First,
                                                                      Resource.ArraySize);
            }

            /* Multisampled views are never cubes to the driver */
            Args->ResourceDimension = (Resource.IsCube() && Resource.SampleCount == 1) ? D3D10DDIRESOURCE_TEXTURECUBE
                                                                                      : D3D10DDIRESOURCE_TEXTURE2D;
            Args->Tex2D = { Mip, First, Count };
            break;

        case D3D11_RTV_DIMENSION_TEXTURE3D:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE3D)
                return E_INVALIDARG;
            Mip = Desc.Texture3D.MipSlice;
            First = Desc.Texture3D.FirstWSlice;
            Count = Desc.Texture3D.WSize = RxRemaining(Desc.Texture3D.WSize, First,
                                                       RxResourceInfo::MipSize(Resource.Depth, Mip));
            if (!RxRangeFits(First, Count, RxResourceInfo::MipSize(Resource.Depth, Mip)) || Mip >= Resource.MipLevels)
                return E_INVALIDARG;
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
            Args->Tex3D = { Mip, First, Count };
            *Resolved = Desc;
            return S_OK;

        default:
            return E_INVALIDARG;
    }

    if (Mip >= Resource.MipLevels || !RxRangeFits(First, Count, Resource.ArraySize))
        return E_INVALIDARG;

    *Resolved = Desc;
    return S_OK;
}

/* Shader resource views */

HRESULT
RxShaderResourceTraits::Resolve(
    _In_ const RxResourceInfo& Resource,
    _In_opt_ const ApiDesc* Requested,
    _Out_ ApiDesc* Resolved,
    _Out_ DdiArgs* Args)
{
    ApiDesc Desc = {};
    UINT* Mips = nullptr;
    UINT Detailed = 0;
    UINT First = 0;
    UINT Count = 1;

    *Args = {};
    if (Requested != nullptr)
    {
        Desc = *Requested;
    }
    else
    {
        /* Without a desc the view is every mip and slice, in the resource's format */
        Desc.Format = Resource.Format;
        switch (Resource.Dimension)
        {
            case D3D11_RESOURCE_DIMENSION_BUFFER:
                /* Only structured buffers know their element size */
                if (Resource.StructureByteStride == 0)
                    return E_INVALIDARG;
                Desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                Desc.Buffer.NumElements = RxStructureCount(Resource);
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY
                                                              : D3D11_SRV_DIMENSION_TEXTURE1D;
                Desc.Texture1DArray.MipLevels = (UINT)-1;
                Desc.Texture1DArray.ArraySize = Resource.ArraySize;
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
                if (Resource.IsCube())
                {
                    Desc.ViewDimension = (Resource.ArraySize > 6) ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY
                                                                  : D3D11_SRV_DIMENSION_TEXTURECUBE;
                    Desc.TextureCubeArray.MipLevels = (UINT)-1;
                    Desc.TextureCubeArray.NumCubes = Resource.ArraySize / 6;
                }
                else if (Resource.SampleCount > 1)
                {
                    Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY
                                                                  : D3D11_SRV_DIMENSION_TEXTURE2DMS;
                    Desc.Texture2DMSArray.ArraySize = Resource.ArraySize;
                }
                else
                {
                    Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY
                                                                  : D3D11_SRV_DIMENSION_TEXTURE2D;
                    Desc.Texture2DArray.MipLevels = (UINT)-1;
                    Desc.Texture2DArray.ArraySize = Resource.ArraySize;
                }
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
                Desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
                Desc.Texture3D.MipLevels = (UINT)-1;
                break;
            default:
                return E_INVALIDARG;
        }
    }

    if (Desc.Format == DXGI_FORMAT_UNKNOWN && Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
        Desc.Format = Resource.Format;

    /* Raw buffer views are the one place a typeless format is the right one */
    if (RxIsTypelessFormat(Desc.Format) &&
        !(Desc.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (Desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW)))
    {
        return E_INVALIDARG;
    }

    Args->Format = Desc.Format;
    switch (Desc.ViewDimension)
    {
        case D3D11_SRV_DIMENSION_BUFFER:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
                return E_INVALIDARG;
            Args->ResourceDimension = D3D10DDIRESOURCE_BUFFER;
            Args->Buffer.FirstElement = Desc.Buffer.FirstElement;
            Args->Buffer.NumElements = Desc.Buffer.NumElements;
            *Resolved = Desc;
            return S_OK;

        case D3D11_SRV_DIMENSION_BUFFEREX:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
                return E_INVALIDARG;

            /* Raw views read 32 bit words and need a buffer that allows them */
            if ((Desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW) &&
                ((Resource.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) == 0 ||
                 Desc.Format != DXGI_FORMAT_R32_TYPELESS))
            {
                return E_INVALIDARG;
            }
            Args->ResourceDimension = D3D11DDIRESOURCE_BUFFEREX;
            Args->BufferEx.FirstElement = Desc.BufferEx.FirstElement;
            Args->BufferEx.NumElements = Desc.BufferEx.NumElements;
            Args->BufferEx.Flags = (Desc.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW) ? D3D11_DDI_BUFFEREX_SRV_FLAG_RAW
                                                                                        : 0;
            *Resolved = Desc;
            return S_OK;

        case D3D11_SRV_DIMENSION_TEXTURE1D:
        case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE1D)
                return E_INVALIDARG;
            Detailed = Desc.Texture1DArray.MostDetailedMip;
            Mips = &Desc.Texture1DArray.MipLevels;
            if (Desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE1DARRAY)
            {
                First = Desc.Texture1DArray.FirstArraySlice;
                Count = Desc.Texture1DArray.ArraySize = RxRemaining(Desc.Texture1DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE1D;
            break;

        case D3D11_SRV_DIMENSION_TEXTURE2D:
        case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || Resource.SampleCount > 1)
                return E_INVALIDARG;
            Detailed = Desc.Texture2DArray.MostDetailedMip;
            Mips = &Desc.Texture2DArray.MipLevels;
            if (Desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY)
            {
                First = Desc.Texture2DArray.FirstArraySlice;
                Count = Desc.Texture2DArray.ArraySize = RxRemaining(Desc.Texture2DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            break;

        case D3D11_SRV_DIMENSION_TEXTURE2DMS:
        case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || Resource.SampleCount == 1)
                return E_INVALIDARG;
            if (Desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY)
            {
                First = Desc.Texture2DMSArray.FirstArraySlice;
                Count = Desc.Texture2DMSArray.ArraySize = RxRemaining(Desc.Texture2DMSArray.ArraySize, First,
                                                                      Resource.ArraySize);
            }
            if (!RxRangeFits(First, Count, Resource.ArraySize))
                return E_INVALIDARG;
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            Args->Tex2D = { 0, First, 1, Count };
            *Resolved = Desc;
            return S_OK;

        case D3D11_SRV_DIMENSION_TEXTURE3D:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE3D)
                return E_INVALIDARG;
            Detailed = Desc.Texture3D.MostDetailedMip;
            Mips = &Desc.Texture3D.MipLevels;
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
            break;

        case D3D11_SRV_DIMENSION_TEXTURECUBE:
        case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || !Resource.IsCube())
                return E_INVALIDARG;
            Detailed = Desc.TextureCubeArray.MostDetailedMip;
            Mips = &Desc.TextureCubeArray.MipLevels;
            if (Desc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBEARRAY)
            {
                First = Desc.TextureCubeArray.First2DArrayFace;
                Count = Desc.TextureCubeArray.NumCubes * 6;
            }
            else
            {
                Count = 6;
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURECUBE;
            break;

        default:
            return E_INVALIDARG;
    }

    /* A mip count of -1 means every level from the most detailed one down */
    if (Detailed >= Resource.MipLevels)
        return E_INVALIDARG;
    *Mips = RxRemaining(*Mips, Detailed, Resource.MipLevels);
    if (!RxRangeFits(Detailed, *Mips, Resource.MipLevels) || !RxRangeFits(First, Count, Resource.ArraySize))
        return E_INVALIDARG;

    switch (Args->ResourceDimension)
    {
        case D3D10DDIRESOURCE_TEXTURE1D:
            Args->Tex1D = { Detailed, First, *Mips, Count };
            break;
        case D3D10DDIRESOURCE_TEXTURE2D:
            Args->Tex2D = { Detailed, First, *Mips, Count };
            break;
        case D3D10DDIRESOURCE_TEXTURE3D:
            Args->Tex3D = { Detailed, *Mips };
            break;
        default:
            Args->TexCube = { Detailed, *Mips, First, Count / 6 };
            break;
    }

    *Resolved = Desc;
    return S_OK;
}

/* Depth stencil views */

HRESULT
RxDepthStencilViewTraits::Resolve(
    _In_ const RxResourceInfo& Resource,
    _In_opt_ const ApiDesc* Requested,
    _Out_ ApiDesc* Resolved,
    _Out_ DdiArgs* Args)
{
    ApiDesc Desc = {};
    UINT Mip = 0;
    UINT First = 0;
    UINT Count = 1;

    *Args = {};
    if (Requested != nullptr)
    {
        Desc = *Requested;
    }
    else
    {
        Desc.Format = Resource.Format;
        if (Resource.Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE1D)
        {
            Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY
                                                          : D3D11_DSV_DIMENSION_TEXTURE1D;
            Desc.Texture1DArray.ArraySize = Resource.ArraySize;
        }
        else if (Resource.Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (Resource.SampleCount > 1)
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY
                                                              : D3D11_DSV_DIMENSION_TEXTURE2DMS;
            else
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY
                                                              : D3D11_DSV_DIMENSION_TEXTURE2D;
            Desc.Texture2DArray.ArraySize = Resource.ArraySize;
        }
        else
        {
            return E_INVALIDARG;
        }
    }

    if (Desc.Format == DXGI_FORMAT_UNKNOWN)
        Desc.Format = Resource.Format;
    if (RxIsTypelessFormat(Desc.Format))
        return E_INVALIDARG;

    Args->Format = Desc.Format;
    Args->Flags = Desc.Flags;
    switch (Desc.ViewDimension)
    {
        case D3D11_DSV_DIMENSION_TEXTURE1D:
        case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE1D)
                return E_INVALIDARG;
            Mip = Desc.Texture1DArray.MipSlice;
            if (Desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE1DARRAY)
            {
                First = Desc.Texture1DArray.FirstArraySlice;
                Count = Desc.Texture1DArray.ArraySize = RxRemaining(Desc.Texture1DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE1D;
            Args->Tex1D = { Mip, First, Count };
            break;

        case D3D11_DSV_DIMENSION_TEXTURE2D:
        case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        case D3D11_DSV_DIMENSION_TEXTURE2DMS:
        case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                return E_INVALIDARG;
            if (Desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2D)
            {
                Mip = Desc.Texture2D.MipSlice;
            }
            else if (Desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DARRAY)
            {
                Mip = Desc.Texture2DArray.MipSlice;
                First = Desc.Texture2DArray.FirstArraySlice;
                Count = Desc.Texture2DArray.ArraySize = RxRemaining(Desc.Texture2DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            else if (Desc.ViewDimension == D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY)
            {
                First = Desc.Texture2DMSArray.FirstArraySlice;
                Count = Desc.Texture2DMSArray.ArraySize = RxRemaining(Desc.Texture2DMSArray.ArraySize, First,
                                                                      Resource.ArraySize);
            }
            Args->ResourceDimension = (Resource.IsCube() && Resource.SampleCount == 1) ? D3D10DDIRESOURCE_TEXTURECUBE
                                                                                      : D3D10DDIRESOURCE_TEXTURE2D;
            Args->Tex2D = { Mip, First, Count };
            break;

        default:
            return E_INVALIDARG;
    }

    if (Mip >= Resource.MipLevels || !RxRangeFits(First, Count, Resource.ArraySize))
        return E_INVALIDARG;

    *Resolved = Desc;
    return S_OK;
}

/* Unordered access views */

HRESULT
RxUnorderedAccessTraits::Resolve(
    _In_ const RxResourceInfo& Resource,
    _In_opt_ const ApiDesc* Requested,
    _Out_ ApiDesc* Resolved,
    _Out_ DdiArgs* Args)
{
    ApiDesc Desc = {};
    UINT Mip = 0;
    UINT First = 0;
    UINT Count = 1;

    *Args = {};
    if (Requested != nullptr)
    {
        Desc = *Requested;
    }
    else
    {
        Desc.Format = Resource.Format;
        switch (Resource.Dimension)
        {
            case D3D11_RESOURCE_DIMENSION_BUFFER:
                if (Resource.StructureByteStride == 0)
                    return E_INVALIDARG;
                Desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                Desc.Buffer.NumElements = RxStructureCount(Resource);
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY
                                                              : D3D11_UAV_DIMENSION_TEXTURE1D;
                Desc.Texture1DArray.ArraySize = Resource.ArraySize;
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
                Desc.ViewDimension = (Resource.ArraySize > 1) ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY
                                                              : D3D11_UAV_DIMENSION_TEXTURE2D;
                Desc.Texture2DArray.ArraySize = Resource.ArraySize;
                break;
            case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
                Desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                Desc.Texture3D.WSize = (UINT)-1;
                break;
            default:
                return E_INVALIDARG;
        }
    }

    if (Desc.Format == DXGI_FORMAT_UNKNOWN && Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
        Desc.Format = Resource.Format;

    Args->Format = Desc.Format;
    switch (Desc.ViewDimension)
    {
        case D3D11_UAV_DIMENSION_BUFFER:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_BUFFER)
                return E_INVALIDARG;
            if ((Desc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) &&
                ((Resource.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS) == 0 ||
                 Desc.Format != DXGI_FORMAT_R32_TYPELESS))
            {
                return E_INVALIDARG;
            }

            /* Append and counter views keep a hidden count, which only structured buffers have room for */
            if ((Desc.Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)) &&
                (Resource.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0)
            {
                return E_INVALIDARG;
            }

            Args->ResourceDimension = D3D10DDIRESOURCE_BUFFER;
            Args->Buffer.FirstElement = Desc.Buffer.FirstElement;
            Args->Buffer.NumElements = Desc.Buffer.NumElements;
            Args->Buffer.Flags = ((Desc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) ? D3D11_DDI_BUFFER_UAV_FLAG_RAW : 0) |
                                 ((Desc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_APPEND) ? D3D11_DDI_BUFFER_UAV_FLAG_APPEND : 0) |
                                 ((Desc.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_COUNTER) ? D3D11_DDI_BUFFER_UAV_FLAG_COUNTER : 0);
            *Resolved = Desc;
            return S_OK;

        case D3D11_UAV_DIMENSION_TEXTURE1D:
        case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE1D)
                return E_INVALIDARG;
            Mip = Desc.Texture1DArray.MipSlice;
            if (Desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE1DARRAY)
            {
                First = Desc.Texture1DArray.FirstArraySlice;
                Count = Desc.Texture1DArray.ArraySize = RxRemaining(Desc.Texture1DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE1D;
            Args->Tex1D = { Mip, First, Count };
            break;

        case D3D11_UAV_DIMENSION_TEXTURE2D:
        case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D || Resource.SampleCount > 1)
                return E_INVALIDARG;
            Mip = Desc.Texture2DArray.MipSlice;
            if (Desc.ViewDimension == D3D11_UAV_DIMENSION_TEXTURE2DARRAY)
            {
                First = Desc.Texture2DArray.FirstArraySlice;
                Count = Desc.Texture2DArray.ArraySize = RxRemaining(Desc.Texture2DArray.ArraySize, First,
                                                                    Resource.ArraySize);
            }
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
            Args->Tex2D = { Mip, First, Count };
            break;

        case D3D11_UAV_DIMENSION_TEXTURE3D:
            if (Resource.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE3D)
                return E_INVALIDARG;
            Mip = Desc.Texture3D.MipSlice;
            First = Desc.Texture3D.FirstWSlice;
            Count = Desc.Texture3D.WSize = RxRemaining(Desc.Texture3D.WSize, First,
                                                       RxResourceInfo::MipSize(Resource.Depth, Mip));
            if (Mip >= Resource.MipLevels || !RxRangeFits(First, Count, RxResourceInfo::MipSize(Resource.Depth, Mip)))
                return E_INVALIDARG;
            Args->ResourceDimension = D3D10DDIRESOURCE_TEXTURE3D;
            Args->Tex3D = { Mip, First, Count };
            *Resolved = Desc;
            return S_OK;

        default:
            return E_INVALIDARG;
    }

    if (RxIsTypelessFormat(Desc.Format) || Mip >= Resource.MipLevels || !RxRangeFits(First, Count, Resource.ArraySize))
        return E_INVALIDARG;

    *Resolved = Desc;
    return S_OK;
}

/* The shared view object */

template <typename Traits>
RxView<Traits>::~RxView()
{
    if (IsCreated)
        (this->Device.GetDeviceFuncs().*Traits::Destroy)(this->Device.GetDriverDevice(), Handle);

    /* Views keep their resource alive internally, so app references alone decide when it can go */
    if (Resource != nullptr)
        RxGetLifetime(Resource)->ReleaseInternalRef();
}

template <typename Traits>
HRESULT
RxView<Traits>::Create(
    _In_ RxDevice& Device,
    _In_ ID3D11Resource* Resource,
    _In_opt_ const ApiDesc* Desc,
    _Outptr_ RxView** View)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    typename Traits::DdiArgs Args;
    typename Traits::RuntimeHandle RuntimeHandle;
    RxResourceInfo Info;
    ApiDesc Resolved;
    RxView* NewView;
    HRESULT hr;

    *View = nullptr;

    if (!RxDescribeResource(Resource, &Info) || (Info.BindFlags & Traits::RequiredBind) == 0)
    {
        RX_TRACE(L"view refused: resource bind %X lacks %X", Info.BindFlags, Traits::RequiredBind);
        return E_INVALIDARG;
    }

    hr = Traits::Resolve(Info, Desc, &Resolved, &Args);
    if (FAILED(hr))
    {
        RX_TRACE(L"view refused: resource dimension %u format %u mips %u array %u misc %X, view format %u dimension %u",
                 Info.Dimension, Info.Format, Info.MipLevels, Info.ArraySize, Info.MiscFlags,
                 Desc ? Desc->Format : 0, Desc ? Desc->ViewDimension : 0);
        return hr;
    }

    NewView = new (std::nothrow) RxView(Device);
    if (NewView == nullptr)
        return E_OUTOFMEMORY;

    RxGetLifetime(Resource)->AddInternalRef();
    NewView->Resource = Resource;
    NewView->Backing = RxGetDdiResource(Resource);
    NewView->Desc = Resolved;
    NewView->SeenWrites = NewView->Backing->GetWriteCount();
    Args.hDrvResource = NewView->Backing->GetHandle();

    NewView->Handle.pDrvPrivate = NewView->Memory.Allocate((Funcs.*Traits::CalcSize)(Device.GetDriverDevice(), &Args));
    if (NewView->Handle.pDrvPrivate == nullptr)
    {
        NewView->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<Interface*>(NewView);
    (Funcs.*Traits::Create)(Device.GetDriverDevice(), &Args, NewView->Handle, RuntimeHandle);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD view create failed 0x%08lX", hr);
        NewView->Release();
        return hr;
    }

    NewView->IsCreated = TRUE;
    *View = NewView;
    return S_OK;
}

template <typename Traits>
VOID
RxView<Traits>::NoteRead()
{
    UINT64 Writes = Backing->GetWriteCount();

    if (Writes == SeenWrites)
        return;

    SeenWrites = Writes;
    if constexpr (std::is_same_v<Traits, RxShaderResourceTraits>)
    {
        this->Device.GetDeviceFuncs().pfnShaderResourceViewReadAfterWriteHazard(this->Device.GetDriverDevice(), Handle,
                                                                                 Backing->GetHandle());
    }
}

template <typename Traits>
HRESULT
STDMETHODCALLTYPE
RxView<Traits>::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11View) ||
        riid == __uuidof(Interface))
    {
        *ppvObject = static_cast<Interface*>(this);
        this->AddRef();
        return S_OK;
    }

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

template class RxView<RxRenderTargetTraits>;
template class RxView<RxShaderResourceTraits>;
template class RxView<RxDepthStencilViewTraits>;
template class RxView<RxUnorderedAccessTraits>;
