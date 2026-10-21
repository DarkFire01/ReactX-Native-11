/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     One, two and three dimensional textures, and a common view of any resource
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "resource.hpp"

/* Any resource described the same way, so views can be built against all of them alike */
struct RxResourceInfo
{
    D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    UINT Width = 0;
    UINT Height = 1;
    UINT Depth = 1;
    UINT MipLevels = 1;
    UINT ArraySize = 1;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    UINT SampleCount = 1;
    UINT SampleQuality = 0;
    D3D11_USAGE Usage = D3D11_USAGE_DEFAULT;
    UINT CpuAccessFlags = 0;
    UINT BindFlags = 0;
    UINT MiscFlags = 0;
    UINT StructureByteStride = 0;

    BOOLEAN
    IsCube() const
    {
        return (MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) != 0;
    }

    /* Size of a mip level along one axis */
    static
    UINT
    MipSize(
        _In_ UINT Size,
        _In_ UINT Mip)
    {
        return (Size >> Mip) ? (Size >> Mip) : 1;
    }
};

/* Fills Info for a resource of this runtime; FALSE for anything else */
BOOLEAN
RxDescribeResource(
    _In_opt_ ID3D11Resource* Resource,
    _Out_ RxResourceInfo* Info);

struct RxTexture1DTraits
{
    using Interface = ID3D11Texture1D;
    using Desc = D3D11_TEXTURE1D_DESC;
    static constexpr D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
    static constexpr GUID PrivateId = { 0x3a91c0e5, 0x6d24, 0x4b7f, { 0x8e, 0x13, 0x52, 0xaf, 0x09, 0xd6, 0x7c, 0x41 } };

    static
    RxTextureShape
    Shape(
        _In_ const Desc& Source)
    {
        return { D3D10DDIRESOURCE_TEXTURE1D, Source.Width, 1, 1, Source.MipLevels, Source.ArraySize, Source.Format,
                 { 1, 0 }, Source.Usage, Source.BindFlags, Source.CPUAccessFlags, Source.MiscFlags };
    }

    static
    UINT
    LargestSide(
        _In_ const Desc& Source)
    {
        return Source.Width;
    }
};

struct RxTexture2DTraits
{
    using Interface = ID3D11Texture2D;
    using Desc = D3D11_TEXTURE2D_DESC;
    static constexpr D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    static constexpr GUID PrivateId = { 0x7e52b8a1, 0x0c69, 0x4d3e, { 0xa1, 0x77, 0xd4, 0x3b, 0x5e, 0x80, 0x29, 0x96 } };

    static
    RxTextureShape
    Shape(
        _In_ const Desc& Source)
    {
        return { (Source.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) ? D3D10DDIRESOURCE_TEXTURECUBE
                                                                       : D3D10DDIRESOURCE_TEXTURE2D,
                 Source.Width, Source.Height, 1, Source.MipLevels, Source.ArraySize, Source.Format, Source.SampleDesc,
                 Source.Usage, Source.BindFlags, Source.CPUAccessFlags, Source.MiscFlags };
    }

    static
    UINT
    LargestSide(
        _In_ const Desc& Source)
    {
        return (Source.Width > Source.Height) ? Source.Width : Source.Height;
    }
};

struct RxTexture3DTraits
{
    using Interface = ID3D11Texture3D;
    using Desc = D3D11_TEXTURE3D_DESC;
    static constexpr D3D11_RESOURCE_DIMENSION Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
    static constexpr GUID PrivateId = { 0xb4d06f72, 0x93e8, 0x41a5, { 0x9b, 0x2c, 0x6e, 0x17, 0xf0, 0x48, 0xd5, 0x3a } };

    static
    RxTextureShape
    Shape(
        _In_ const Desc& Source)
    {
        return { D3D10DDIRESOURCE_TEXTURE3D, Source.Width, Source.Height, Source.Depth, Source.MipLevels, 1,
                 Source.Format, { 1, 0 }, Source.Usage, Source.BindFlags, Source.CPUAccessFlags, Source.MiscFlags };
    }

    static
    UINT
    LargestSide(
        _In_ const Desc& Source)
    {
        UINT Side = (Source.Width > Source.Height) ? Source.Width : Source.Height;

        return (Side > Source.Depth) ? Side : Source.Depth;
    }
};

template <typename Traits>
class RxTexture final : public RxDeviceChild<typename Traits::Interface>
{
public:
    using Interface = typename Traits::Interface;
    using Desc = typename Traits::Desc;
    static constexpr GUID PrivateId = Traits::PrivateId;

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const Desc& Description,
        _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
        _Outptr_ RxTexture** Texture,
        _In_ UINT ExtraDdiBindFlags = 0,
        _In_ UINT ExtraDdiMiscFlags = 0,
        _In_opt_ const RxSharedTextureRecord* SharedRecord = nullptr,
        _In_opt_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc = nullptr);

    /* Opens a texture another device shared; only 2D textures can be opened */
    static
    HRESULT
    Open(
        _In_ RxDevice& Device,
        _In_ HANDLE SharedHandle,
        _In_ BOOLEAN IsNtHandle,
        _Outptr_ RxTexture** Texture);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    VOID
    STDMETHODCALLTYPE
    GetType(
        _Out_ D3D11_RESOURCE_DIMENSION* pResourceDimension) override
    {
        *pResourceDimension = Traits::Dimension;
    }

    VOID
    STDMETHODCALLTYPE
    SetEvictionPriority(
        _In_ UINT EvictionPriority) override
    {
        Priority = EvictionPriority;
    }

    UINT
    STDMETHODCALLTYPE
    GetEvictionPriority(VOID) override
    {
        return Priority;
    }

    VOID
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ Desc* pDesc) override
    {
        *pDesc = Description;
    }

    DdiResource&
    GetDdiResource()
    {
        return Resource;
    }

    RxDxgiSurface&
    GetDxgiSurface()
    {
        return DxgiSurface;
    }

private:
    explicit RxTexture(
        _In_ RxDevice& Device)
        : RxDeviceChild<Interface>(Device),
          Resource(Device),
          DxgiSurface(this, Device)
    {
    }

    DdiResource Resource;
    RxDxgiSurface DxgiSurface;
    Desc Description = {};
    UINT Priority = DXGI_RESOURCE_PRIORITY_NORMAL;
};

using RxTexture1D = RxTexture<RxTexture1DTraits>;
using RxTexture2D = RxTexture<RxTexture2DTraits>;
using RxTexture3D = RxTexture<RxTexture3DTraits>;
