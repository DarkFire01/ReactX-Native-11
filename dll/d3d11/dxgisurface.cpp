/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DXGI face of buffers and textures: IDXGIResource and IDXGISurface
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "textures.hpp"

BOOLEAN
RxDxgiSurface::IsSurface()
{
    RxResourceInfo Info;

    if (!RxDescribeResource(Owner, &Info))
        return FALSE;

    switch (Info.Dimension)
    {
        case D3D11_RESOURCE_DIMENSION_BUFFER:
            return TRUE;
        case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
            return Info.MipLevels == 1 && Info.Depth == 1;
        default:
            return Info.MipLevels == 1 && Info.ArraySize == 1;
    }
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Owner->QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxDxgiSurface::AddRef(VOID)
{
    return Owner->AddRef();
}

ULONG
STDMETHODCALLTYPE
RxDxgiSurface::Release(VOID)
{
    return Owner->Release();
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::SetPrivateData(
    _In_ REFGUID Name,
    _In_ UINT DataSize,
    _In_reads_bytes_(DataSize) const void* pData)
{
    return Owner->SetPrivateData(Name, DataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::SetPrivateDataInterface(
    _In_ REFGUID Name,
    _In_opt_ const IUnknown* pUnknown)
{
    return Owner->SetPrivateDataInterface(Name, pUnknown);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetPrivateData(
    _In_ REFGUID Name,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_(*pDataSize) void* pData)
{
    return Owner->GetPrivateData(Name, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetParent(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParent)
{
    /* Swapchain buffers belong to the swapchain, everything else to the device */
    if (Parent != nullptr)
        return Parent->QueryInterface(riid, ppParent);
    return Device.QueryInterface(riid, ppParent);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetDevice(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppDevice)
{
    return Device.QueryInterface(riid, ppDevice);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetSharedHandle(
    _Out_ HANDLE* pSharedHandle)
{
    /* Swapchain buffers can never be shared; other resources just have no handle yet */
    if (HasUsageOverride)
        return E_INVALIDARG;
    if (pSharedHandle == nullptr)
        return E_INVALIDARG;

    *pSharedHandle = NULL;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetUsage(
    _Out_ DXGI_USAGE* pUsage)
{
    RxResourceInfo Info;
    DXGI_USAGE Usage = 0;

    if (pUsage == nullptr)
        return E_INVALIDARG;

    if (HasUsageOverride)
    {
        *pUsage = UsageOverride;
        return S_OK;
    }

    /* CPU access first, then one bit per kind of GPU binding */
    RxDescribeResource(Owner, &Info);
    if (Info.Usage == D3D11_USAGE_DYNAMIC)
        Usage = DXGI_CPU_ACCESS_DYNAMIC;
    else if (Info.Usage == D3D11_USAGE_STAGING)
        Usage = DXGI_CPU_ACCESS_SCRATCH;

    if (Info.BindFlags & D3D11_BIND_SHADER_RESOURCE)
        Usage |= DXGI_USAGE_SHADER_INPUT;
    if (Info.BindFlags & D3D11_BIND_RENDER_TARGET)
        Usage |= DXGI_USAGE_RENDER_TARGET_OUTPUT;
    if (Info.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        Usage |= DXGI_USAGE_UNORDERED_ACCESS;

    *pUsage = Usage;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::SetEvictionPriority(
    _In_ UINT EvictionPriority)
{
    Owner->SetEvictionPriority(EvictionPriority);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetEvictionPriority(
    _Out_ UINT* pEvictionPriority)
{
    if (pEvictionPriority == nullptr)
        return E_INVALIDARG;

    *pEvictionPriority = Owner->GetEvictionPriority();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::CreateSubresourceSurface(
    _In_ UINT index,
    _COM_Outptr_ IDXGISurface2** ppSurface)
{
    UNREFERENCED_PARAMETER(index);

    if (ppSurface != nullptr)
        *ppSurface = nullptr;
    return DXGI_ERROR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::CreateSharedHandle(
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    _In_ DWORD dwAccess,
    _In_opt_ LPCWSTR lpName,
    _Out_ HANDLE* pHandle)
{
    UNREFERENCED_PARAMETER(pAttributes);
    UNREFERENCED_PARAMETER(dwAccess);
    UNREFERENCED_PARAMETER(lpName);

    /* Only resources created shared have NT handles to hand out */
    if (pHandle != nullptr)
        *pHandle = NULL;
    return E_INVALIDARG;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetDesc(
    _Out_ DXGI_SURFACE_DESC* pDesc)
{
    RxResourceInfo Info;

    if (pDesc == nullptr || !IsSurface() || !RxDescribeResource(Owner, &Info))
        return E_INVALIDARG;

    /* A buffer is a row of bytes with no format */
    pDesc->Width = Info.Width;
    pDesc->Height = Info.Height;
    pDesc->Format = (Info.Dimension == D3D11_RESOURCE_DIMENSION_BUFFER) ? DXGI_FORMAT_UNKNOWN : Info.Format;
    pDesc->SampleDesc = { Info.SampleCount, Info.SampleQuality };
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::Map(
    _Out_ DXGI_MAPPED_RECT* pLockedRect,
    _In_ UINT MapFlags)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ID3D11DeviceContext* Context;
    D3D11_MAP Type;
    DXGI_USAGE Usage;
    UINT CpuAccess;
    HRESULT hr;

    if (pLockedRect == nullptr || !IsSurface())
        return E_INVALIDARG;

    /* Discard means write everything, and only dynamic resources can do it */
    GetUsage(&Usage);
    CpuAccess = (UINT)(Usage & DXGI_CPU_ACCESS_FIELD);
    if (MapFlags == 0 || (MapFlags & (DXGI_MAP_DISCARD | DXGI_MAP_READ)) == (DXGI_MAP_DISCARD | DXGI_MAP_READ) ||
        ((MapFlags & DXGI_MAP_DISCARD) && !(MapFlags & DXGI_MAP_WRITE)) || CpuAccess == 0 ||
        (CpuAccess == DXGI_CPU_ACCESS_DYNAMIC && (MapFlags & DXGI_MAP_READ)) ||
        (CpuAccess != DXGI_CPU_ACCESS_DYNAMIC && (MapFlags & DXGI_MAP_DISCARD)))
    {
        return E_INVALIDARG;
    }

    if (MapFlags & DXGI_MAP_DISCARD)
        Type = D3D11_MAP_WRITE_DISCARD;
    else if ((MapFlags & (DXGI_MAP_READ | DXGI_MAP_WRITE)) == (DXGI_MAP_READ | DXGI_MAP_WRITE))
        Type = D3D11_MAP_READ_WRITE;
    else if (MapFlags & DXGI_MAP_READ)
        Type = D3D11_MAP_READ;
    else
        Type = D3D11_MAP_WRITE;

    Device.GetImmediateContext(&Context);
    hr = Context->Map(Owner, 0, Type, 0, &Mapped);
    Context->Release();
    if (FAILED(hr))
        return hr;

    pLockedRect->pBits = static_cast<BYTE*>(Mapped.pData);
    pLockedRect->Pitch = (INT)Mapped.RowPitch;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::Unmap(VOID)
{
    ID3D11DeviceContext* Context;

    Device.GetImmediateContext(&Context);
    Context->Unmap(Owner, 0);
    Context->Release();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetDC(
    _In_ BOOL Discard,
    _Out_ HDC* phdc)
{
    UNREFERENCED_PARAMETER(Discard);

    /* GDI interop needs GDI compatible resources, which this runtime does not make yet */
    if (phdc != nullptr)
        *phdc = NULL;
    return DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::ReleaseDC(
    _In_opt_ RECT* pDirtyRect)
{
    UNREFERENCED_PARAMETER(pDirtyRect);
    return DXGI_ERROR_INVALID_CALL;
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetResource(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppParentResource,
    _Out_ UINT* pSubresourceIndex)
{
    HRESULT hr;

    hr = Owner->QueryInterface(riid, ppParentResource);
    if (SUCCEEDED(hr) && pSubresourceIndex != nullptr)
        *pSubresourceIndex = 0;
    return hr;
}
