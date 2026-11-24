/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DXGI face of buffers and textures: IDXGIResource, IDXGISurface and IDXGIKeyedMutex
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

class RxDevice;

/*
 * Lives inside the resource it describes and shares its lifetime and identity:
 * reference counting and QueryInterface go to the D3D11 object.
 */
class RxDxgiSurface final : public IDXGISurface2, public IDXGIResource1, public IDXGIKeyedMutex
{
public:
    RxDxgiSurface(
        _In_ ID3D11Resource* Owner,
        _In_ RxDevice& Device)
        : Owner(Owner),
          Device(Device)
    {
    }

    /* Buffers and single subresource textures can be used as surfaces */
    BOOLEAN
    IsSurface();

    /* Textures made or opened with a keyed mutex answer IDXGIKeyedMutex */
    BOOLEAN
    HasKeyedMutex();

    /* Swapchain buffers report the usage the swapchain was made with */
    VOID
    SetUsage(
        _In_ DXGI_USAGE Usage)
    {
        UsageOverride = Usage;
        HasUsageOverride = TRUE;
    }

    /* What GetParent reports instead of the device; not referenced, the owner detaches it */
    VOID
    SetParent(
        _In_opt_ IUnknown* NewParent)
    {
        Parent = NewParent;
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
    SetPrivateData(
        _In_ REFGUID Name,
        _In_ UINT DataSize,
        _In_reads_bytes_(DataSize) const void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID Name,
        _In_opt_ const IUnknown* pUnknown) override;

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID Name,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_(*pDataSize) void* pData) override;

    HRESULT
    STDMETHODCALLTYPE
    GetParent(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppParent) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDevice(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppDevice) override;

    HRESULT
    STDMETHODCALLTYPE
    GetSharedHandle(
        _Out_ HANDLE* pSharedHandle) override;

    HRESULT
    STDMETHODCALLTYPE
    GetUsage(
        _Out_ DXGI_USAGE* pUsage) override;

    HRESULT
    STDMETHODCALLTYPE
    SetEvictionPriority(
        _In_ UINT EvictionPriority) override;

    HRESULT
    STDMETHODCALLTYPE
    GetEvictionPriority(
        _Out_ UINT* pEvictionPriority) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateSubresourceSurface(
        _In_ UINT index,
        _COM_Outptr_ IDXGISurface2** ppSurface) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateSharedHandle(
        _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
        _In_ DWORD dwAccess,
        _In_opt_ LPCWSTR lpName,
        _Out_ HANDLE* pHandle) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDesc(
        _Out_ DXGI_SURFACE_DESC* pDesc) override;

    HRESULT
    STDMETHODCALLTYPE
    Map(
        _Out_ DXGI_MAPPED_RECT* pLockedRect,
        _In_ UINT MapFlags) override;

    HRESULT
    STDMETHODCALLTYPE
    Unmap(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetDC(
        _In_ BOOL Discard,
        _Out_ HDC* phdc) override;

    HRESULT
    STDMETHODCALLTYPE
    ReleaseDC(
        _In_opt_ RECT* pDirtyRect) override;

    HRESULT
    STDMETHODCALLTYPE
    GetResource(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppParentResource,
        _Out_ UINT* pSubresourceIndex) override;

    HRESULT
    STDMETHODCALLTYPE
    AcquireSync(
        _In_ UINT64 Key,
        _In_ DWORD dwMilliseconds) override;

    HRESULT
    STDMETHODCALLTYPE
    ReleaseSync(
        _In_ UINT64 Key) override;

    ~RxDxgiSurface();

    /* TRUE between GetDC and ReleaseDC */
    BOOLEAN
    IsDcHeld() const
    {
        return GdiDc != NULL;
    }

private:
    /* Copies the texture into the DIB behind the DC */
    HRESULT
    ReadIntoDc(VOID);

    VOID
    FreeDc(VOID);

    ID3D11Resource* Owner;
    RxDevice& Device;
    IUnknown* Parent = nullptr;
    DXGI_USAGE UsageOverride = 0;
    BOOLEAN HasUsageOverride = FALSE;

    /* The DC GetDC handed out, drawn into a DIB that ReleaseDC copies back */
    HDC GdiDc = NULL;
    HBITMAP GdiBitmap = NULL;
    HGDIOBJ GdiOldBitmap = NULL;
    BYTE* GdiBits = nullptr;
    UINT GdiWidth = 0;
    UINT GdiHeight = 0;
};
