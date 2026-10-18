/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device interface DirectComposition asks a Direct3D 11 device for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "partner.hpp"
#include "textures.hpp"

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxCompositionPartner::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxCompositionPartner::Release(VOID)
{
    return Device.Release();
}

/* Guard rects */

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::GuardOn(VOID)
{
    InterlockedIncrement(&GuardSupportCount);
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxCompositionPartner::GuardOff(VOID)
{
    InterlockedDecrement(&GuardSupportCount);
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::MakeGuardedTexture(
    _In_ const D3D11_TEXTURE2D_DESC* Desc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
    _COM_Outptr_ ID3D11Texture2D** Texture)
{
    RX_TRACE(L"partner MakeGuardedTexture %ux%u format %u bind %X misc %X", Desc ? Desc->Width : 0,
             Desc ? Desc->Height : 0, Desc ? Desc->Format : 0, Desc ? Desc->BindFlags : 0, Desc ? Desc->MiscFlags : 0);
    return Device.CreateTexture2D(Desc, InitialData, Texture);
}

VOID
STDMETHODCALLTYPE
RxCompositionPartner::PutGuardRect(
    _In_ ID3D11Texture2D* Texture,
    _In_ const RECT* Rect)
{
    DdiResource* Resource = RxGetDdiResource(Texture);

    if (Resource == nullptr || Rect == nullptr)
        return;

    Resource->Guard = RxGuard::Rect;
    Resource->GuardRect = *Rect;
}

VOID
STDMETHODCALLTYPE
RxCompositionPartner::ClearGuardRect(
    _In_ ID3D11Texture2D* Texture)
{
    DdiResource* Resource = RxGetDdiResource(Texture);

    if (Resource == nullptr)
        return;

    Resource->Guard = RxGuard::Empty;
    Resource->GuardRect = {};
}

VOID
STDMETHODCALLTYPE
RxCompositionPartner::DropGuard(
    _In_ ID3D11Texture2D* Texture)
{
    DdiResource* Resource = RxGetDdiResource(Texture);

    if (Resource != nullptr)
        Resource->Guard = RxGuard::None;
}

VOID
STDMETHODCALLTYPE
RxCompositionPartner::QueryGuardRect(
    _In_ ID3D11Texture2D* Texture,
    _Out_ BOOL* IsGuarded,
    _Out_ RECT* Rect)
{
    DdiResource* Resource = RxGetDdiResource(Texture);

    *IsGuarded = FALSE;
    *Rect = {};
    if (Resource == nullptr || Resource->Guard == RxGuard::None)
        return;

    *IsGuarded = TRUE;
    *Rect = Resource->GuardRect;
}

/* Memory offers made on behalf of the compositor */

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::OfferForPartner(
    _In_ UINT NumResources,
    _In_reads_(NumResources) IDXGIResource* const* Resources,
    _In_ DXGI_OFFER_RESOURCE_PRIORITY Priority,
    _In_ UINT Flags)
{
    IDXGIDevice2* Dxgi;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Flags);

    hr = Device.QueryInterface(IID_PPV_ARGS(&Dxgi));
    if (FAILED(hr))
        return hr;

    hr = Dxgi->OfferResources(NumResources, Resources, Priority);
    Dxgi->Release();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::ReclaimResourcesInternal(
    _In_ UINT NumResources,
    _In_reads_(NumResources) IDXGIResource* const* Resources,
    _Out_writes_opt_(NumResources) DXGI_RECLAIM_RESOURCE_RESULTS* Results)
{
    UINT Index;

    /* Offered memory is never taken away here, so everything comes back intact */
    for (Index = 0; Results != nullptr && Index < NumResources; Index++)
        Results[Index] = DXGI_RECLAIM_RESOURCE_RESULT_OK;

    return (NumResources != 0 && Resources != nullptr) ? S_OK : E_INVALIDARG;
}

UINT
STDMETHODCALLTYPE
RxCompositionPartner::GetPartnerCaps(VOID)
{
    /* What a Windows 11 device reports */
    return 1;
}

/* Not yet understood; each says when it is reached */

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::MakeCompositionBuffer(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Arg3,
    _In_ UINT Arg4,
    _In_ UINT Arg5,
    _In_ REFIID Riid,
    _Out_ VOID** Buffer,
    _Out_ VOID** Handle)
{
    UNREFERENCED_PARAMETER(Riid);

    RX_TRACE(L"partner MakeCompositionBuffer %ux%u format %u %u %X %X", Width, Height, Format, Arg3, Arg4, Arg5);
    *Buffer = nullptr;
    *Handle = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::ShowCompositionBuffers(
    _In_ VOID* Target,
    _In_reads_(Count) IUnknown* const* Buffers,
    _In_ UINT Count)
{
    UNREFERENCED_PARAMETER(Target);
    UNREFERENCED_PARAMETER(Buffers);

    RX_TRACE(L"partner ShowCompositionBuffers %u", Count);
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::OpenSyncChannel(
    _COM_Outptr_ IUnknown** Channel)
{
    RX_TRACE(L"partner OpenSyncChannel");
    *Channel = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::OpenSynchronizedChannel(
    _In_ HANDLE Shared,
    _COM_Outptr_ IUnknown** Channel)
{
    UNREFERENCED_PARAMETER(Shared);

    RX_TRACE(L"partner OpenSynchronizedChannel");
    *Channel = nullptr;
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::SubmitFlipToken(
    _In_ VOID* Manager,
    _In_ UINT64 Token,
    _In_ INT Arg2,
    _In_ UINT Arg3,
    _In_ UINT Arg4,
    _In_reads_(Count) IUnknown* const* Buffers,
    _In_ UINT Count)
{
    UNREFERENCED_PARAMETER(Manager);
    UNREFERENCED_PARAMETER(Buffers);

    RX_TRACE(L"partner SubmitFlipToken %I64X %d %u %u %u", Token, Arg2, Arg3, Arg4, Count);
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::CancelFlipManagerPresent(
    _In_ LUID Adapter,
    _In_ UINT64 Arg1,
    _In_ UINT64 Arg2)
{
    UNREFERENCED_PARAMETER(Adapter);

    RX_TRACE(L"partner CancelFlipManagerPresent %I64X %I64X", Arg1, Arg2);
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::AllowVariableRefresh(
    _In_ UINT64 Arg0,
    _In_ UINT64 Arg1)
{
    RX_TRACE(L"partner AllowVariableRefresh %I64X %I64X", Arg0, Arg1);
    return E_NOTIMPL;
}

HRESULT
STDMETHODCALLTYPE
RxCompositionPartner::RetireFinishedPresents(
    _In_ LUID Adapter,
    _In_ VOID* Arg1)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(Arg1);

    RX_TRACE(L"partner RetireFinishedPresents");
    return E_NOTIMPL;
}
