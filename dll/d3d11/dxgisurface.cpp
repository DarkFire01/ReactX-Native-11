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

BOOLEAN
RxDxgiSurface::HasKeyedMutex()
{
    DdiResource* Resource = RxGetDdiResource(Owner);

    return Resource != nullptr && Resource->HasKeyedMutex();
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
    DdiResource* Resource = RxGetDdiResource(Owner);

    /* Swapchain buffers and NT handle textures have no global share handle; unshared ones get NULL */
    if (HasUsageOverride)
        return E_INVALIDARG;
    if (pSharedHandle == nullptr)
        return E_INVALIDARG;
    if (Resource != nullptr && Resource->GetShareKind() == RxShareKind::NtHandle)
        return E_INVALIDARG;

    *pSharedHandle = Resource ? (HANDLE)(ULONG_PTR)Resource->GetGlobalShare() : NULL;
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
    DdiResource* Resource = RxGetDdiResource(Owner);
    const KmtThunks* Kmt = RxGetKmtThunks();
    OBJECT_ATTRIBUTES Attributes;
    D3DKMT_HANDLE Objects[3];
    UINT ObjectCount = 1;
    NTSTATUS Status;

    if (pHandle == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    *pHandle = NULL;

    /* Only textures created or opened for NT handle sharing have NT handles to hand out */
    if (Resource == nullptr || Resource->GetShareKind() != RxShareKind::NtHandle || Resource->GetKmResource() == 0)
        return E_INVALIDARG;
    if (Kmt->ShareObjects == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* Named handles need the session's named object directory, which this runtime does not look up */
    if (lpName != nullptr)
    {
        RX_UNIMPLEMENTED();
        return E_NOTIMPL;
    }

    InitializeObjectAttributes(&Attributes, nullptr, (pAttributes && pAttributes->bInheritHandle) ? OBJ_INHERIT : 0,
                               nullptr, pAttributes ? pAttributes->lpSecurityDescriptor : nullptr);
    /* The keyed mutex and its fence go into the same handle, after the resource */
    Objects[0] = Resource->GetKmResource();
    if (Resource->HasKeyedMutex())
    {
        Objects[1] = Resource->GetRuntimeResource()->KeyedMutex;
        Objects[2] = Resource->GetRuntimeResource()->SyncObject;
        ObjectCount = 3;
    }

    Status = Kmt->ShareObjects(ObjectCount, Objects, &Attributes, dwAccess, pHandle);
    if (!NT_SUCCESS(Status))
    {
        RX_TRACE(L"D3DKMTShareObjects failed 0x%08lX", Status);
        return RxHresultFromStatus(Status);
    }

    return S_OK;
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

RxDxgiSurface::~RxDxgiSurface()
{
    FreeDc();
}

VOID
RxDxgiSurface::FreeDc(VOID)
{
    if (GdiDc != NULL)
    {
        SelectObject(GdiDc, GdiOldBitmap);
        DeleteDC(GdiDc);
    }
    if (GdiBitmap != NULL)
        DeleteObject(GdiBitmap);

    GdiDc = NULL;
    GdiBitmap = NULL;
    GdiOldBitmap = NULL;
    GdiBits = nullptr;
}

HRESULT
RxDxgiSurface::ReadIntoDc(VOID)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    ID3D11Texture2D* Texture;
    ID3D11Texture2D* Staging = nullptr;
    ID3D11DeviceContext* Context;
    HRESULT hr;
    UINT Row;

    hr = Owner->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Texture));
    if (FAILED(hr))
        return hr;

    Texture->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    hr = Device.CreateTexture2D(&Desc, nullptr, &Staging);
    if (FAILED(hr))
    {
        Texture->Release();
        return hr;
    }

    Device.GetImmediateContext(&Context);
    Context->CopyResource(Staging, Texture);
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        for (Row = 0; Row < GdiHeight; Row++)
        {
            memcpy(GdiBits + (SIZE_T)Row * GdiWidth * 4,
                   static_cast<BYTE*>(Mapped.pData) + (SIZE_T)Row * Mapped.RowPitch,
                   (SIZE_T)GdiWidth * 4);
        }
        Context->Unmap(Staging, 0);
    }

    Context->Release();
    Staging->Release();
    Texture->Release();
    return hr;
}

/**
 * @brief
 * Hands out a GDI DC drawing into the texture. Only single subresource BGRA
 * textures made GDI compatible have one, and only one at a time.
 */
HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::GetDC(
    _In_ BOOL Discard,
    _Out_ HDC* phdc)
{
    BITMAPINFO BitmapInfo = {};
    RxResourceInfo Info;
    void* Bits = nullptr;
    HRESULT hr;

    if (phdc == nullptr)
        return E_INVALIDARG;
    *phdc = NULL;

    if (GdiDc != NULL ||
        !RxDescribeResource(Owner, &Info) ||
        Info.Dimension != D3D11_RESOURCE_DIMENSION_TEXTURE2D ||
        !(Info.MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE) ||
        Info.MipLevels != 1 || Info.ArraySize != 1 || Info.SampleCount != 1 ||
        (Info.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         Info.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB &&
         Info.Format != DXGI_FORMAT_B8G8R8A8_TYPELESS))
    {
        return DXGI_ERROR_INVALID_CALL;
    }

    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = (LONG)Info.Width;
    BitmapInfo.bmiHeader.biHeight = -(LONG)Info.Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    GdiBitmap = CreateDIBSection(NULL, &BitmapInfo, DIB_RGB_COLORS, &Bits, NULL, 0);
    GdiDc = CreateCompatibleDC(NULL);
    if (GdiBitmap == NULL || GdiDc == NULL || Bits == nullptr)
    {
        FreeDc();
        return E_OUTOFMEMORY;
    }

    GdiOldBitmap = SelectObject(GdiDc, GdiBitmap);
    GdiBits = static_cast<BYTE*>(Bits);
    GdiWidth = Info.Width;
    GdiHeight = Info.Height;

    /* Without discard the DC starts out with what the texture holds */
    if (!Discard)
    {
        hr = ReadIntoDc();
        if (FAILED(hr))
        {
            FreeDc();
            return hr;
        }
    }

    *phdc = GdiDc;
    return S_OK;
}

/**
 * @brief
 * Gives the DC back and copies what GDI drew into the texture, all of it when
 * no dirty rectangle is given.
 */
HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::ReleaseDC(
    _In_opt_ RECT* pDirtyRect)
{
    ID3D11DeviceContext* Context;
    RECT Dirty = { 0, 0, (LONG)GdiWidth, (LONG)GdiHeight };
    RECT Whole = Dirty;
    D3D11_BOX Box;

    if (GdiDc == NULL)
        return DXGI_ERROR_INVALID_CALL;

    GdiFlush();

    if (pDirtyRect != nullptr && !IntersectRect(&Dirty, pDirtyRect, &Whole))
        SetRectEmpty(&Dirty);

    if (!IsRectEmpty(&Dirty))
    {
        Box.left = (UINT)Dirty.left;
        Box.top = (UINT)Dirty.top;
        Box.front = 0;
        Box.right = (UINT)Dirty.right;
        Box.bottom = (UINT)Dirty.bottom;
        Box.back = 1;

        Device.GetImmediateContext(&Context);
        Context->UpdateSubresource(Owner,
                                   0,
                                   &Box,
                                   GdiBits + ((SIZE_T)Dirty.top * GdiWidth + Dirty.left) * 4,
                                   GdiWidth * 4,
                                   0);
        Context->Release();
    }

    FreeDc();
    return S_OK;
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

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::AcquireSync(
    _In_ UINT64 Key,
    _In_ DWORD dwMilliseconds)
{
    DdiResource* Resource = RxGetDdiResource(Owner);

    if (Resource == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    return Resource->AcquireSync(Key, dwMilliseconds);
}

HRESULT
STDMETHODCALLTYPE
RxDxgiSurface::ReleaseSync(
    _In_ UINT64 Key)
{
    DdiResource* Resource = RxGetDdiResource(Owner);

    if (Resource == nullptr)
        return DXGI_ERROR_INVALID_CALL;
    return Resource->ReleaseSync(Key);
}
