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

/* Whether other Direct3D versions can open the texture too, which is when the record's surface header is filled */
static
BOOLEAN
RxIsCrossApiShareable(
    _In_ const D3D11_TEXTURE2D_DESC& Desc)
{
    static const DXGI_FORMAT Formats[][2] =
    {
        { DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_SINT },
        { DXGI_FORMAT_R10G10B10A2_TYPELESS, DXGI_FORMAT_R11G11B10_FLOAT },
        { DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R32_SINT },
        { DXGI_FORMAT_R8G8_TYPELESS, DXGI_FORMAT_R8_SINT },
        { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8X8_UNORM },
        { DXGI_FORMAT_B8G8R8A8_TYPELESS, DXGI_FORMAT_B8G8R8X8_UNORM_SRGB },
        { DXGI_FORMAT_AYUV, DXGI_FORMAT_Y216 },
    };
    static const UINT MiscFlags[] =
    {
        D3D11_RESOURCE_MISC_SHARED,
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
        D3D11_RESOURCE_MISC_GDI_COMPATIBLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_GDI_COMPATIBLE,
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_GDI_COMPATIBLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_GDI_COMPATIBLE,
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE,
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
            D3D11_RESOURCE_MISC_GDI_COMPATIBLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_GDI_COMPATIBLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE |
            D3D11_RESOURCE_MISC_SHARED_EXCLUSIVE_WRITER,
        D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_GDI_COMPATIBLE |
            D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE | D3D11_RESOURCE_MISC_SHARED_EXCLUSIVE_WRITER,
    };
    BOOLEAN IsFormatShareable = FALSE;
    BOOLEAN IsMiscShareable = FALSE;

    for (const auto& Range : Formats)
        IsFormatShareable = IsFormatShareable || (Desc.Format >= Range[0] && Desc.Format <= Range[1]);
    for (UINT Misc : MiscFlags)
        IsMiscShareable = IsMiscShareable || Desc.MiscFlags == Misc;

    /* The other versions only know single sample surfaces with plain binds */
    return IsFormatShareable && IsMiscShareable && Desc.MipLevels <= 1 && Desc.Usage == D3D11_USAGE_DEFAULT &&
           Desc.CPUAccessFlags == 0 && Desc.SampleDesc.Count == 1 && Desc.SampleDesc.Quality == 0 &&
           (Desc.BindFlags & ~(D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS |
                               D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER)) == 0;
}

/* The record an opener rebuilds a shared 2D texture from, as Windows writes it */
static
RxSharedTextureRecord
RxMakeSharedRecord(
    _In_ const D3D11_TEXTURE2D_DESC& Desc)
{
    RxSharedTextureRecord Record = {};

    Record.Size = sizeof(Record);
    Record.Version = RxSharedRecordVersion;
    if (RxIsCrossApiShareable(Desc))
    {
        Record.Width = Desc.Width;
        Record.Height = Desc.Height;
        Record.Format = Desc.Format;
        Record.Constants[RxSharedRecordArraySizeIndex] = Desc.ArraySize;
    }

    /* The mutex and fence handles are filled in when the allocation callback creates them */
    if (Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        Record.Constants[RxSharedRecordKeyedMutexIndex] = 1;
    if (Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        Record.Constants[RxSharedRecordNtHandleIndex] = 1;
    if (Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE)
    {
        Record.Constants[RxSharedRecordDisplayableIndex] = 1;
        Record.Constants[RxSharedRecordDisplayableAgainIndex] = 1;
    }

    Record.Dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
    Record.Desc = Desc;
    return Record;
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
    _In_opt_ const RxSharedTextureRecord* SharedRecord,
    _In_opt_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc)
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
    {
        NewTexture->Resource.SetSharedRecord(*SharedRecord, RxShareKind::SwapChain);
    }
    else if constexpr (Traits::Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        if (Resolved.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
            NewTexture->Resource.SetSharedRecord(RxMakeSharedRecord(Resolved), RxShareKind::NtHandle);
        else if (Resolved.MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
            NewTexture->Resource.SetSharedRecord(RxMakeSharedRecord(Resolved), RxShareKind::GlobalHandle);
    }
    if (PrimaryDesc != nullptr)
        NewTexture->Resource.SetPrimaryDesc(*PrimaryDesc);
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
RxTexture<Traits>::Open(
    _In_ RxDevice& Device,
    _In_ HANDLE SharedHandle,
    _In_ BOOLEAN IsNtHandle,
    _Outptr_ RxTexture** Texture)
{
    *Texture = nullptr;

    if constexpr (Traits::Dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        RxSharedTextureRecord Record;
        RxTexture* NewTexture;
        HRESULT hr;

        NewTexture = new (std::nothrow) RxTexture(Device);
        if (NewTexture == nullptr)
            return E_OUTOFMEMORY;

        if (IsNtHandle)
            hr = NewTexture->Resource.OpenSharedNt(SharedHandle, &Record);
        else
            hr = NewTexture->Resource.OpenShared((D3DKMT_HANDLE)(ULONG_PTR)SharedHandle, &Record);

        /* Without a full record there is no description to give the texture */
        if (SUCCEEDED(hr) && (Record.Size < sizeof(Record) || Record.Version != RxSharedRecordVersion ||
                              Record.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D))
        {
            RX_TRACE(L"shared resource record size 0x%X version %u dimension %u is not supported", Record.Size,
                     Record.Version, Record.Dimension);
            hr = E_INVALIDARG;
        }

        if (FAILED(hr))
        {
            NewTexture->Release();
            return hr;
        }

        NewTexture->Description = Record.Desc;
        *Texture = NewTexture;
        return S_OK;
    }
    else
    {
        return E_INVALIDARG;
    }
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

    if (riid == IID_RxChildLifetime)
    {
        *ppvObject = static_cast<RxChildLifetime*>(this);
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
