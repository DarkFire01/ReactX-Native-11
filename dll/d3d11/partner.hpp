/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device interface DirectComposition asks a Direct3D 11 device for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

class RxDevice;

/*
 * dcomp.dll queries the device for this by IID and calls it by slot, so the IIDs and
 * the method order follow what it expects. The two older IIDs reach the same table.
 */
DEFINE_GUID(IID_RxCompositionPartner, 0x26c5dc23, 0xe49c, 0x4b0a, 0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32);
DEFINE_GUID(IID_RxCompositionPartner1, 0x9ea90e78, 0x15ac, 0x48b4, 0x83, 0xc9, 0x66, 0x4c, 0xab, 0x2f, 0x71, 0xcd);
DEFINE_GUID(IID_RxCompositionPartner0, 0x7334140e, 0xd155, 0x4c70, 0x96, 0xf5, 0xb6, 0xdb, 0xcc, 0x27, 0x3f, 0x88);

struct IRxCompositionPartner : public IUnknown
{
    /* Guarded textures: rendering to one stays inside its guard rect while support is on */
    virtual
    HRESULT
    STDMETHODCALLTYPE
    GuardOn(VOID) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    GuardOff(VOID) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    MakeGuardedTexture(
        _In_ const D3D11_TEXTURE2D_DESC* Desc,
        _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
        _COM_Outptr_ ID3D11Texture2D** Texture) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    PutGuardRect(
        _In_ ID3D11Texture2D* Texture,
        _In_ const RECT* Rect) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    ClearGuardRect(
        _In_ ID3D11Texture2D* Texture) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    DropGuard(
        _In_ ID3D11Texture2D* Texture) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    OfferForPartner(
        _In_ UINT NumResources,
        _In_reads_(NumResources) IDXGIResource* const* Resources,
        _In_ DXGI_OFFER_RESOURCE_PRIORITY Priority,
        _In_ UINT Flags) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    ReclaimResourcesInternal(
        _In_ UINT NumResources,
        _In_reads_(NumResources) IDXGIResource* const* Resources,
        _Out_writes_opt_(NumResources) DXGI_RECLAIM_RESOURCE_RESULTS* Results) = 0;

    virtual
    UINT
    STDMETHODCALLTYPE
    GetPartnerCaps(VOID) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    MakeCompositionBuffer(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ DXGI_FORMAT Format,
        _In_ UINT Arg3,
        _In_ UINT Arg4,
        _In_ UINT Arg5,
        _In_ REFIID Riid,
        _Out_ VOID** Buffer,
        _Out_ VOID** Handle) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    ShowCompositionBuffers(
        _In_ VOID* Target,
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) = 0;

    virtual
    VOID
    STDMETHODCALLTYPE
    QueryGuardRect(
        _In_ ID3D11Texture2D* Texture,
        _Out_ BOOL* IsGuarded,
        _Out_ RECT* Rect) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    OpenSyncChannel(
        _COM_Outptr_ IUnknown** Channel) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    OpenSynchronizedChannel(
        _In_ HANDLE Shared,
        _COM_Outptr_ IUnknown** Channel) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    SubmitFlipToken(
        _In_ VOID* Manager,
        _In_ UINT64 Token,
        _In_ INT Arg2,
        _In_ UINT Arg3,
        _In_ UINT Arg4,
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    CancelFlipManagerPresent(
        _In_ LUID Adapter,
        _In_ UINT64 Arg1,
        _In_ UINT64 Arg2) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    AllowVariableRefresh(
        _In_ UINT64 Arg0,
        _In_ UINT64 Arg1) = 0;

    virtual
    HRESULT
    STDMETHODCALLTYPE
    RetireFinishedPresents(
        _In_ LUID Adapter,
        _In_ VOID* Arg1) = 0;
};

/* A tear-off of RxDevice: same lifetime, QueryInterface goes back to the device */
class RxCompositionPartner final : public IRxCompositionPartner
{
public:
    explicit RxCompositionPartner(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GuardOn(VOID) override;

    VOID
    STDMETHODCALLTYPE
    GuardOff(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    MakeGuardedTexture(
        _In_ const D3D11_TEXTURE2D_DESC* Desc,
        _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
        _COM_Outptr_ ID3D11Texture2D** Texture) override;

    VOID
    STDMETHODCALLTYPE
    PutGuardRect(
        _In_ ID3D11Texture2D* Texture,
        _In_ const RECT* Rect) override;

    VOID
    STDMETHODCALLTYPE
    ClearGuardRect(
        _In_ ID3D11Texture2D* Texture) override;

    VOID
    STDMETHODCALLTYPE
    DropGuard(
        _In_ ID3D11Texture2D* Texture) override;

    HRESULT
    STDMETHODCALLTYPE
    OfferForPartner(
        _In_ UINT NumResources,
        _In_reads_(NumResources) IDXGIResource* const* Resources,
        _In_ DXGI_OFFER_RESOURCE_PRIORITY Priority,
        _In_ UINT Flags) override;

    HRESULT
    STDMETHODCALLTYPE
    ReclaimResourcesInternal(
        _In_ UINT NumResources,
        _In_reads_(NumResources) IDXGIResource* const* Resources,
        _Out_writes_opt_(NumResources) DXGI_RECLAIM_RESOURCE_RESULTS* Results) override;

    UINT
    STDMETHODCALLTYPE
    GetPartnerCaps(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    MakeCompositionBuffer(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ DXGI_FORMAT Format,
        _In_ UINT Arg3,
        _In_ UINT Arg4,
        _In_ UINT Arg5,
        _In_ REFIID Riid,
        _Out_ VOID** Buffer,
        _Out_ VOID** Handle) override;

    HRESULT
    STDMETHODCALLTYPE
    ShowCompositionBuffers(
        _In_ VOID* Target,
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) override;

    VOID
    STDMETHODCALLTYPE
    QueryGuardRect(
        _In_ ID3D11Texture2D* Texture,
        _Out_ BOOL* IsGuarded,
        _Out_ RECT* Rect) override;

    HRESULT
    STDMETHODCALLTYPE
    OpenSyncChannel(
        _COM_Outptr_ IUnknown** Channel) override;

    HRESULT
    STDMETHODCALLTYPE
    OpenSynchronizedChannel(
        _In_ HANDLE Shared,
        _COM_Outptr_ IUnknown** Channel) override;

    HRESULT
    STDMETHODCALLTYPE
    SubmitFlipToken(
        _In_ VOID* Manager,
        _In_ UINT64 Token,
        _In_ INT Arg2,
        _In_ UINT Arg3,
        _In_ UINT Arg4,
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) override;

    HRESULT
    STDMETHODCALLTYPE
    CancelFlipManagerPresent(
        _In_ LUID Adapter,
        _In_ UINT64 Arg1,
        _In_ UINT64 Arg2) override;

    HRESULT
    STDMETHODCALLTYPE
    AllowVariableRefresh(
        _In_ UINT64 Arg0,
        _In_ UINT64 Arg1) override;

    HRESULT
    STDMETHODCALLTYPE
    RetireFinishedPresents(
        _In_ LUID Adapter,
        _In_ VOID* Arg1) override;

private:
    RxDevice& Device;
    LONG GuardSupportCount = 0;
};
