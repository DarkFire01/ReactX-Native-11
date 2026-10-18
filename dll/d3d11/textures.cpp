/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     One, two and three dimensional textures, and a common view of any resource
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textures.hpp"

BOOLEAN
RxDescribeResource(
    _In_opt_ ID3D11Resource* Resource,
    _Out_ RxResourceInfo* Info)
{
    *Info = {};
    if (Resource == nullptr || RxGetDdiResource(Resource) == nullptr)
        return FALSE;

    Resource->GetType(&Info->Dimension);
    switch (Info->Dimension)
    {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
        {
            D3D11_BUFFER_DESC Desc;

            static_cast<ID3D11Buffer*>(Resource)->GetDesc(&Desc);
            Info->Width = Desc.ByteWidth;
            Info->Usage = Desc.Usage;
            Info->CpuAccessFlags = Desc.CPUAccessFlags;
            Info->BindFlags = Desc.BindFlags;
            Info->MiscFlags = Desc.MiscFlags;
            Info->StructureByteStride = Desc.StructureByteStride;
            return TRUE;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        {
            D3D11_TEXTURE1D_DESC Desc;

            static_cast<ID3D11Texture1D*>(Resource)->GetDesc(&Desc);
            Info->Width = Desc.Width;
            Info->MipLevels = Desc.MipLevels;
            Info->ArraySize = Desc.ArraySize;
            Info->Format = Desc.Format;
            Info->Usage = Desc.Usage;
            Info->CpuAccessFlags = Desc.CPUAccessFlags;
            Info->BindFlags = Desc.BindFlags;
            Info->MiscFlags = Desc.MiscFlags;
            return TRUE;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE2D:
        {
            D3D11_TEXTURE2D_DESC Desc;

            static_cast<ID3D11Texture2D*>(Resource)->GetDesc(&Desc);
            Info->Width = Desc.Width;
            Info->Height = Desc.Height;
            Info->MipLevels = Desc.MipLevels;
            Info->ArraySize = Desc.ArraySize;
            Info->Format = Desc.Format;
            Info->SampleCount = Desc.SampleDesc.Count;
            Info->SampleQuality = Desc.SampleDesc.Quality;
            Info->Usage = Desc.Usage;
            Info->CpuAccessFlags = Desc.CPUAccessFlags;
            Info->BindFlags = Desc.BindFlags;
            Info->MiscFlags = Desc.MiscFlags;
            return TRUE;
        }

        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
        {
            D3D11_TEXTURE3D_DESC Desc;

            static_cast<ID3D11Texture3D*>(Resource)->GetDesc(&Desc);
            Info->Width = Desc.Width;
            Info->Height = Desc.Height;
            Info->Depth = Desc.Depth;
            Info->MipLevels = Desc.MipLevels;
            Info->Format = Desc.Format;
            Info->Usage = Desc.Usage;
            Info->CpuAccessFlags = Desc.CPUAccessFlags;
            Info->BindFlags = Desc.BindFlags;
            Info->MiscFlags = Desc.MiscFlags;
            return TRUE;
        }

        default:
            return FALSE;
    }
}

template <typename Traits>
HRESULT
RxTexture<Traits>::Create(
    _In_ RxDevice& Device,
    _In_ const Desc& Description,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
    _Outptr_ RxTexture** Texture,
    _In_ UINT ExtraDdiBindFlags,
    _In_ UINT ExtraDdiMiscFlags,
    _In_opt_ const RxSharedTextureRecord* SharedRecord)
{
    Desc Resolved = Description;
    RxTexture* NewTexture;
    UINT Largest;
    HRESULT hr;

    *Texture = nullptr;

    /* MipLevels of zero asks for the full chain down to one texel */
    if (Resolved.MipLevels == 0)
    {
        Largest = Traits::LargestSide(Resolved);
        Resolved.MipLevels = 1;
        while (Largest > 1)
        {
            Largest >>= 1;
            Resolved.MipLevels++;
        }
    }

    NewTexture = new (std::nothrow) RxTexture(Device);
    if (NewTexture == nullptr)
        return E_OUTOFMEMORY;

    NewTexture->Description = Resolved;
    if (SharedRecord != nullptr)
        NewTexture->Resource.SetSharedRecord(*SharedRecord);
    hr = NewTexture->Resource.CreateTexture(Traits::Shape(Resolved), InitialData, ExtraDdiBindFlags, ExtraDdiMiscFlags);
    if (FAILED(hr))
    {
        NewTexture->Release();
        return hr;
    }

    *Texture = NewTexture;
    return S_OK;
}

template <typename Traits>
HRESULT
STDMETHODCALLTYPE
RxTexture<Traits>::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == IID_RxDdiResource)
    {
        *ppvObject = &Resource;
        return S_OK;
    }

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11Resource) ||
        riid == __uuidof(Interface))
    {
        *ppvObject = static_cast<Interface*>(this);
        this->AddRef();
        return S_OK;
    }

    if (RxQueryDxgiSurface(DxgiSurface, riid, ppvObject))
        return S_OK;

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

template class RxTexture<RxTexture1DTraits>;
template class RxTexture<RxTexture2DTraits>;
template class RxTexture<RxTexture3DTraits>;
