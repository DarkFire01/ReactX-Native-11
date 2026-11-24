/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Hand written ID3D11DeviceContext methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "views.hpp"

#include <cmath>

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) ||
        riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11DeviceContext) ||
        riid == __uuidof(ID3D11DeviceContext1) ||
        riid == __uuidof(ID3D11DeviceContext2) ||
        riid == __uuidof(ID3D11DeviceContext3) ||
        riid == __uuidof(ID3D11DeviceContext4))
    {
        *ppvObject = static_cast<ID3D11DeviceContext4*>(this);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D11Multithread))
    {
        *ppvObject = static_cast<ID3D11Multithread*>(&Multithread);
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D11VideoContext) && SUCCEEDED(Device.EnsureVideoFuncs()))
    {
        *ppvObject = static_cast<ID3D11VideoContext*>(&VideoContext);
        AddRef();
        return S_OK;
    }

    if (RxIsTraceEnabled())
    {
        WCHAR Text[40];

        StringFromGUID2(riid, Text, _countof(Text));
        RxTrace(L"context QueryInterface %s not supported", Text);
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxImmediateContext::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxImmediateContext::Release(VOID)
{
    return Device.Release();
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::GetDevice(
    _Outptr_ ID3D11Device** ppDevice)
{
    RxContextGuard Guard(Device);
    Device.AddRef();
    *ppDevice = &Device;
}

D3D11_DEVICE_CONTEXT_TYPE
STDMETHODCALLTYPE
RxImmediateContext::GetType(VOID)
{
    RxContextGuard Guard(Device);
    return D3D11_DEVICE_CONTEXT_IMMEDIATE;
}

UINT
STDMETHODCALLTYPE
RxImmediateContext::GetContextFlags(VOID)
{
    RxContextGuard Guard(Device);
    return 0;
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::Flush(VOID)
{
    RxContextGuard Guard(Device);
    Device.FlushDriver(0);
}

HRESULT
STDMETHODCALLTYPE
RxImmediateContext::Map(
    _In_ ID3D11Resource* pResource,
    _In_ UINT Subresource,
    _In_ D3D11_MAP MapType,
    _In_ UINT MapFlags,
    _Out_opt_ D3D11_MAPPED_SUBRESOURCE* pMappedResource)
{
    RxContextGuard Guard(Device);
    D3D11_MAPPED_SUBRESOURCE Ignored;
    DdiResource* Resource = RxGetDdiResource(pResource);
    RxResourceInfo Info;
    BOOLEAN IsAllowed;

    if (pMappedResource != nullptr)
        *pMappedResource = {};
    if (Resource == nullptr || MapType < D3D11_MAP_READ || MapType > D3D11_MAP_WRITE_NO_OVERWRITE)
        return E_INVALIDARG;
    if ((MapFlags & ~D3D11_MAP_FLAG_DO_NOT_WAIT) != 0)
        return E_INVALIDARG;

    /* Dynamic resources are only ever rewritten; staging ones read and write as their CPU access says */
    RxDescribeResource(pResource, &Info);
    switch (Info.Usage)
    {
        case D3D11_USAGE_DYNAMIC:
            IsAllowed = (MapType == D3D11_MAP_WRITE_DISCARD || MapType == D3D11_MAP_WRITE_NO_OVERWRITE);
            break;

        case D3D11_USAGE_STAGING:
            IsAllowed = (MapType == D3D11_MAP_READ && (Info.CpuAccessFlags & D3D11_CPU_ACCESS_READ)) ||
                        (MapType == D3D11_MAP_WRITE && (Info.CpuAccessFlags & D3D11_CPU_ACCESS_WRITE)) ||
                        (MapType == D3D11_MAP_READ_WRITE &&
                         (Info.CpuAccessFlags & (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE)) ==
                             (D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE));
            break;

        default:
            IsAllowed = FALSE;
            break;
    }
    if (!IsAllowed)
        return E_INVALIDARG;

    return Resource->Map(Subresource, MapType, MapFlags, pMappedResource ? pMappedResource : &Ignored);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::Unmap(
    _In_ ID3D11Resource* pResource,
    _In_ UINT Subresource)
{
    RxContextGuard Guard(Device);
    DdiResource* Resource = RxGetDdiResource(pResource);

    if (Resource != nullptr)
        Resource->Unmap(Subresource);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CopySubresourceRegion(
    _In_ ID3D11Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ UINT DstX,
    _In_ UINT DstY,
    _In_ UINT DstZ,
    _In_ ID3D11Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_opt_ const D3D11_BOX* pSrcBox)
{
    RxContextGuard Guard(Device);
    CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox, 0);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CopySubresourceRegion1(
    _In_ ID3D11Resource* pDstResource,
    _In_ UINT DstSubresource,
    _In_ UINT DstX,
    _In_ UINT DstY,
    _In_ UINT DstZ,
    _In_ ID3D11Resource* pSrcResource,
    _In_ UINT SrcSubresource,
    _In_opt_ const D3D11_BOX* pSrcBox,
    _In_ UINT CopyFlags)
{
    RxContextGuard Guard(Device);
    DdiResource* Destination = RxGetDdiResource(pDstResource);
    DdiResource* Source = RxGetDdiResource(pSrcResource);

    static_assert(sizeof(D3D11_BOX) == sizeof(D3D10_DDI_BOX));

    if (Destination == nullptr || Source == nullptr)
        return;

    Device.GetDeviceFuncs().pfnResourceCopyRegion(Device.GetDriverDevice(),
                                                  Destination->GetHandle(),
                                                  DstSubresource,
                                                  DstX,
                                                  DstY,
                                                  DstZ,
                                                  Source->GetHandle(),
                                                  SrcSubresource,
                                                  reinterpret_cast<const D3D10_DDI_BOX*>(pSrcBox),
                                                  CopyFlags);
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::CopyResource(
    _In_ ID3D11Resource* pDstResource,
    _In_ ID3D11Resource* pSrcResource)
{
    RxContextGuard Guard(Device);
    DdiResource* Destination = RxGetDdiResource(pDstResource);
    DdiResource* Source = RxGetDdiResource(pSrcResource);

    if (Destination == nullptr || Source == nullptr)
        return;

    Device.GetDeviceFuncs().pfnResourceCopy(Device.GetDriverDevice(), Destination->GetHandle(), Source->GetHandle());
}

VOID
STDMETHODCALLTYPE
RxImmediateContext::ClearRenderTargetView(
    _In_ ID3D11RenderTargetView* pRenderTargetView,
    _In_ const FLOAT ColorRGBA[4])
{
    RxContextGuard Guard(Device);
    RxRenderTargetView* View = RxUnwrap<RxRenderTargetView>(pRenderTargetView);
    FLOAT Color[4];
    UINT Channel;

    if (View == nullptr || ColorRGBA == nullptr)
        return;

    /* Denormal channels clear as zero, the way the GPU would treat them anyway */
    for (Channel = 0; Channel < 4; Channel++)
        Color[Channel] = (std::fpclassify(ColorRGBA[Channel]) == FP_SUBNORMAL) ? 0.0f : ColorRGBA[Channel];

    Device.GetDeviceFuncs().pfnClearRenderTargetView(Device.GetDriverDevice(), View->GetHandle(), Color);
}
