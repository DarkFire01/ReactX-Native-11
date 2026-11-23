/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11On12Device2 of a device on the D3D11 on 12 layer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "resource.hpp"
#include "textures.hpp"

/* Usage and flags every wrapped description shares */
template <typename TDesc>
static
VOID
RxOn12FillCommon(
    _In_ const On12ResourceInfo& Info,
    _In_ UINT MiscFlags,
    _Inout_ TDesc& Desc)
{
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = Info.Resource.Flags11.BindFlags;
    Desc.CPUAccessFlags = Info.Resource.Flags11.CPUAccessFlags;
    Desc.MiscFlags = MiscFlags;
}

/* Builds the Direct3D 11 object for a resource the layer wrapped, described as the layer reported it */
static
HRESULT
RxOn12WrapObject(
    _In_ RxDevice& Device,
    _In_ const On12ResourceInfo& Info,
    _In_ const D3D11_RESOURCE_FLAGS& Overrides,
    _In_ D3DKMT_HANDLE KernelHandle,
    _In_ const std::vector<BYTE>& Data,
    _Outptr_result_maybenull_ ID3D11Resource** Wrapped)
{
    const D3D12_RESOURCE_DESC& Source = Info.Resource.Desc;
    const D3D11_RESOURCE_FLAGS& Flags = Info.Resource.Flags11;
    UINT MiscFlags = Flags.MiscFlags | Overrides.MiscFlags;
    UINT Size = (UINT)Data.size();
    HRESULT hr = E_INVALIDARG;

    *Wrapped = nullptr;
    if (Source.Width > MAXUINT32 || Source.Dimension < D3D12_RESOURCE_DIMENSION_BUFFER ||
        Source.Dimension > D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        return E_INVALIDARG;
    }

    switch (Source.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
        {
            D3D11_BUFFER_DESC Desc = {};
            RxBuffer* Buffer;

            Desc.ByteWidth = (UINT)Source.Width;
            Desc.StructureByteStride = Flags.StructureByteStride;
            RxOn12FillCommon(Info, MiscFlags, Desc);
            hr = RxBuffer::Wrap(Device, Desc, KernelHandle, Data.data(), Size, Flags, &Buffer);
            if (SUCCEEDED(hr))
                *Wrapped = Buffer;
            break;
        }

        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        {
            D3D11_TEXTURE1D_DESC Desc = {};
            RxTexture1D* Texture;

            Desc.Width = (UINT)Source.Width;
            Desc.MipLevels = Source.MipLevels;
            Desc.ArraySize = Source.DepthOrArraySize;
            Desc.Format = Source.Format;
            RxOn12FillCommon(Info, MiscFlags, Desc);
            RxCheckLayerTextureFormat(Device, Desc.Format);
            hr = RxTexture1D::Wrap(Device, Desc, KernelHandle, Data.data(), Size, Flags, &Texture);
            if (SUCCEEDED(hr))
                *Wrapped = Texture;
            break;
        }

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        {
            D3D11_TEXTURE2D_DESC Desc = {};
            RxTexture2D* Texture;

            Desc.Width = (UINT)Source.Width;
            Desc.Height = Source.Height;
            Desc.MipLevels = Source.MipLevels;
            Desc.ArraySize = Source.DepthOrArraySize;
            Desc.Format = Source.Format;
            Desc.SampleDesc = Source.SampleDesc;
            RxOn12FillCommon(Info, MiscFlags, Desc);
            RxCheckLayerTextureFormat(Device, Desc.Format);
            hr = RxTexture2D::Wrap(Device, Desc, KernelHandle, Data.data(), Size, Flags, &Texture);
            if (SUCCEEDED(hr))
                *Wrapped = Texture;
            break;
        }

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
        {
            D3D11_TEXTURE3D_DESC Desc = {};
            RxTexture3D* Texture;

            Desc.Width = (UINT)Source.Width;
            Desc.Height = Source.Height;
            Desc.Depth = Source.DepthOrArraySize;
            Desc.MipLevels = Source.MipLevels;
            Desc.Format = Source.Format;
            RxOn12FillCommon(Info, MiscFlags, Desc);
            RxCheckLayerTextureFormat(Device, Desc.Format);
            hr = RxTexture3D::Wrap(Device, Desc, KernelHandle, Data.data(), Size, Flags, &Texture);
            if (SUCCEEDED(hr))
                *Wrapped = Texture;
            break;
        }

        default:
            break;
    }

    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxOn12Face::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxOn12Face::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxOn12Face::Release(VOID)
{
    return Device.Release();
}

HRESULT
STDMETHODCALLTYPE
RxOn12Face::CreateWrappedResource(
    _In_ IUnknown* pResource12,
    _In_ const D3D11_RESOURCE_FLAGS* pFlags11,
    _In_ D3D12_RESOURCE_STATES InState,
    _In_ D3D12_RESOURCE_STATES OutState,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppResource11)
{
    IOn12DdiDevice* Layer = Device.GetLayerDevice();
    ID3D11Resource* Wrapped = nullptr;
    D3DKMT_HANDLE KernelHandle = 0;
    On12ResourceInfo Info = {};
    std::vector<BYTE> Data;
    DdiResource* Backing;
    HRESULT hr;

    if (ppResource11 != nullptr)
        *ppResource11 = nullptr;
    if (pFlags11 == nullptr)
        return E_INVALIDARG;

    /* The wrapping handle takes over a reference of its own on the D3D12 resource once it is described */
    if (pResource12 != nullptr)
        pResource12->AddRef();

    Data.resize(Layer->GetResourcePrivateDataSize());
    hr = Layer->CreateWrappingHandle(pResource12, On12WrapReason::CreateWrappedResource, Data.data(),
                                     (UINT)Data.size(), &KernelHandle);
    if (SUCCEEDED(hr))
        hr = Layer->FillResourceInfo(KernelHandle, pFlags11, &Info);
    if (FAILED(hr))
    {
        RX_TRACE(L"CreateWrappedResource: the layer could not wrap the resource (0x%08lX)", hr);
        if (pResource12 != nullptr)
            pResource12->Release();
        return hr;
    }

    if (Info.Kind != On12ResourceInfoResource)
        return E_INVALIDARG;

    /* The wrapping handle stays with the layer whatever happens, as with the system runtime */
    hr = RxOn12WrapObject(Device, Info, *pFlags11, KernelHandle, Data, &Wrapped);
    if (FAILED(hr))
    {
        RX_TRACE(L"CreateWrappedResource failed 0x%08lX", hr);
        return hr;
    }

    if (ppResource11 == nullptr)
    {
        Wrapped->Release();
        return S_OK;
    }

    hr = Wrapped->QueryInterface(riid, ppResource11);
    if (SUCCEEDED(hr))
    {
        Backing = RxGetDdiResource(Wrapped);
        Backing->LayerInState = InState;
        Backing->LayerOutState = OutState;
        Backing->IsLayerWrapped = TRUE;
        Backing->GetLayerResource()->SetGraphicsCurrentState(InState, On12StateReason::Create);
    }

    Wrapped->Release();
    return hr;
}

VOID
STDMETHODCALLTYPE
RxOn12Face::ReleaseWrappedResources(
    _In_reads_(NumResources) ID3D11Resource* const* ppResources,
    _In_ UINT NumResources)
{
    IOn12DdiDevice* Layer = Device.GetLayerDevice();
    DdiResource* Backing;
    UINT Index;

    for (Index = 0; Index < NumResources; Index++)
    {
        Backing = RxGetDdiResource(ppResources[Index]);
        if (Backing == nullptr || !Backing->IsLayerWrapped ||
            Backing->LayerOwner != DdiResource::On12Owner::Direct3D11)
        {
            continue;
        }

        Layer->TransitionResourceForRelease(Backing->GetLayerResource(), Backing->LayerOutState);
        Backing->LayerOwner = DdiResource::On12Owner::Released;
    }

    Layer->ApplyAllResourceTransitions();
}

VOID
STDMETHODCALLTYPE
RxOn12Face::AcquireWrappedResources(
    _In_reads_(NumResources) ID3D11Resource* const* ppResources,
    _In_ UINT NumResources)
{
    DdiResource* Backing;
    UINT Index;

    for (Index = 0; Index < NumResources; Index++)
    {
        Backing = RxGetDdiResource(ppResources[Index]);
        if (Backing == nullptr || !Backing->IsLayerWrapped ||
            Backing->LayerOwner != DdiResource::On12Owner::Released)
        {
            continue;
        }

        Backing->GetLayerResource()->SetGraphicsCurrentState(Backing->LayerInState, On12StateReason::Acquire);
        Backing->LayerOwner = DdiResource::On12Owner::Direct3D11;
    }
}

HRESULT
STDMETHODCALLTYPE
RxOn12Face::GetD3D12Device(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvDevice)
{
    return Device.GetLayerDevice12()->QueryInterface(riid, ppvDevice);
}

HRESULT
STDMETHODCALLTYPE
RxOn12Face::UnwrapUnderlyingResource(
    _In_ ID3D11Resource* pResource11,
    _In_ ID3D12CommandQueue* pCommandQueue,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvResource12)
{
    IOn12DdiDevice* Layer = Device.GetLayerDevice();
    ID3D12Resource* Underlying;
    IOn12DdiResource* Inner;
    DdiResource* Backing;
    void* Handed = nullptr;
    HRESULT hr;

    if (Device.GetAdapter().GetLayerContractVersion() < 3)
        return E_NOTIMPL;

    Backing = RxGetDdiResource(pResource11);
    if (Backing == nullptr || Backing->LayerOwner != DdiResource::On12Owner::Direct3D11)
        return E_INVALIDARG;

    Inner = Backing->GetLayerResource();
    Underlying = Inner != nullptr ? Inner->GetUnderlyingResource() : nullptr;
    if (Underlying == nullptr || Underlying->GetDesc().Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return E_INVALIDARG;

    /* Keyed mutex and GDI compatible textures stay with Direct3D 11 */
    if (Backing->GetApiFlags().MiscFlags & (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_GDI_COMPATIBLE))
        return E_INVALIDARG;

    hr = Underlying->QueryInterface(riid, &Handed);
    if (FAILED(hr))
        return hr;

    Layer->TransitionResourceForRelease(Inner, D3D12_RESOURCE_STATE_COMMON);
    Layer->ApplyAllResourceTransitions();
    hr = Layer->AddResourceWaitsToQueue(Inner, pCommandQueue);
    if (FAILED(hr))
    {
        static_cast<IUnknown*>(Handed)->Release();
        return hr;
    }

    Backing->LayerOwner = DdiResource::On12Owner::Unwrapped;
    *ppvResource12 = Handed;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxOn12Face::ReturnUnderlyingResource(
    _In_ ID3D11Resource* pResource11,
    _In_ UINT NumSync,
    _In_reads_(NumSync) UINT64* pSignalValues,
    _In_reads_(NumSync) ID3D12Fence** ppFences)
{
    DdiResource* Backing;
    HRESULT hr;

    if (Device.GetAdapter().GetLayerContractVersion() < 3)
        return E_NOTIMPL;

    Backing = RxGetDdiResource(pResource11);
    if (Backing == nullptr || Backing->LayerOwner != DdiResource::On12Owner::Unwrapped)
        return E_INVALIDARG;

    Backing->GetLayerResource()->SetGraphicsCurrentState(D3D12_RESOURCE_STATE_COMMON, On12StateReason::Create);
    hr = Device.GetLayerDevice()->AddDeferredWaitsToResource(Backing->GetLayerResource(), NumSync, pSignalValues,
                                                             ppFences);
    if (SUCCEEDED(hr))
        Backing->LayerOwner = DdiResource::On12Owner::Direct3D11;
    return hr;
}
