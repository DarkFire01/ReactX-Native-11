/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12DescriptorHeap and the views written into it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

HRESULT
RxDescriptorHeap::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_DESCRIPTOR_HEAP_DESC& Desc,
    _Outptr_ RxDescriptorHeap** Heap)
{
    RxDescriptorHeap* Created;
    HRESULT hr;

    *Heap = nullptr;

    if (Desc.NodeMask > 1 || (Desc.Flags & ~D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0)
        return E_INVALIDARG;

    /* Only view and sampler heaps can be shader visible, and only up to the device's limit */
    if ((Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0)
    {
        UINT Limit;

        if (Desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV && Desc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
            return E_INVALIDARG;

        Limit = Device.GetShaderVisibleHeapLimit(Desc.Type);
        if (Limit != 0 && Desc.NumDescriptors > Limit)
            return E_INVALIDARG;
    }

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxDescriptorHeap(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxDescriptorHeap::Initialize(
    _In_ const D3D12_DESCRIPTOR_HEAP_DESC& HeapDesc)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATE_DESCRIPTOR_HEAP_0001 Args = {};
    UINT Index;
    HRESULT hr;

    Desc = HeapDesc;
    Desc.NodeMask = 1;

    Args.Type = (D3D12DDI_DESCRIPTOR_HEAP_TYPE)Desc.Type;
    Args.NumDescriptors = Desc.NumDescriptors;
    Args.Flags = D3D12DDI_DESCRIPTOR_HEAP_FLAG_CPU_VISIBLE;
    if ((Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0)
        Args.Flags |= D3D12DDI_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    Args.NodeMask = 1;

    DriverHeap.pDrvPrivate = Memory.Allocate(Ddi.pfnCalcPrivateDescriptorHeapSize(Device.Handle(), &Args));
    if (DriverHeap.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateDescriptorHeap(Device.Handle(), &Args, DriverHeap);
    if (FAILED(hr))
        return hr;
    IsDriverHeapCreated = TRUE;

    CpuStart = Ddi.pfnGetCPUDescriptorHandleForHeapStart(Device.Handle(), DriverHeap);
    GpuStart = Ddi.pfnGetGPUDescriptorHandleForHeapStart(Device.Handle(), DriverHeap);

    /* Render target views get runtime slots so command lists can find what each one names */
    if (Desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        RtvEntries = new (std::nothrow) RxRtvEntry[Desc.NumDescriptors]();
        if (RtvEntries == nullptr)
            return E_OUTOFMEMORY;

        for (Index = 0; Index < Desc.NumDescriptors; Index++)
            RtvEntries[Index].DriverHandle = CpuStart.ptr + Index * Device.DriverRtvIncrement();
    }

    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_DESCRIPTOR_HEAP);
}

RxDescriptorHeap::~RxDescriptorHeap()
{
    if (IsDriverHeapCreated)
        Device.Ddi().pfnDestroyDescriptorHeap(Device.Handle(), DriverHeap);

    delete[] RtvEntries;
}

HRESULT
STDMETHODCALLTYPE
RxDescriptorHeap::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12DescriptorHeap) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12DescriptorHeap*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

D3D12_DESCRIPTOR_HEAP_DESC
STDMETHODCALLTYPE
RxDescriptorHeap::GetDesc(VOID)
{
    return Desc;
}

D3D12_CPU_DESCRIPTOR_HANDLE
STDMETHODCALLTYPE
RxDescriptorHeap::GetCPUDescriptorHandleForHeapStart(VOID)
{
    D3D12_CPU_DESCRIPTOR_HANDLE Handle;

    Handle.ptr = RtvEntries != nullptr ? reinterpret_cast<SIZE_T>(RtvEntries) : CpuStart.ptr;
    return Handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
STDMETHODCALLTYPE
RxDescriptorHeap::GetGPUDescriptorHandleForHeapStart(VOID)
{
    D3D12_GPU_DESCRIPTOR_HANDLE Handle;

    Handle.ptr = GpuStart.ptr;
    return Handle;
}

/* Views */

/* The view a NULL desc stands for: the whole first mip of the resource in its own format */
static
VOID
RxDefaultRtvDesc(
    _In_ const D3D12_RESOURCE_DESC1& Resource,
    _Out_ D3D12_RENDER_TARGET_VIEW_DESC& View)
{
    ZeroMemory(&View, sizeof(View));
    View.Format = Resource.Format;

    switch (Resource.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            View.ViewDimension = D3D12_RTV_DIMENSION_BUFFER;
            View.Buffer.NumElements = (UINT)Resource.Width;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            View.ViewDimension = Resource.DepthOrArraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY
                                                               : D3D12_RTV_DIMENSION_TEXTURE1D;
            View.Texture1DArray.ArraySize = Resource.DepthOrArraySize;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (Resource.SampleDesc.Count > 1)
            {
                View.ViewDimension = Resource.DepthOrArraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY
                                                                   : D3D12_RTV_DIMENSION_TEXTURE2DMS;
                View.Texture2DMSArray.ArraySize = Resource.DepthOrArraySize;
            }
            else
            {
                View.ViewDimension = Resource.DepthOrArraySize > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY
                                                                   : D3D12_RTV_DIMENSION_TEXTURE2D;
                View.Texture2DArray.ArraySize = Resource.DepthOrArraySize;
            }
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            View.Texture3D.WSize = Resource.DepthOrArraySize;
            break;
        default:
            break;
    }
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateRenderTargetView(
    _In_opt_ ID3D12Resource* pResource,
    _In_opt_ const D3D12_RENDER_TARGET_VIEW_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    RxResource* Resource = RxUnwrap12<RxResource>(pResource);
    RxRtvEntry* Entry = reinterpret_cast<RxRtvEntry*>(DestDescriptor.ptr);
    D3D12DDIARG_CREATE_RENDER_TARGET_VIEW_0002 Args = {};
    D3D12_RENDER_TARGET_VIEW_DESC View;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;

    if (Entry == nullptr || (Resource == nullptr && pDesc == nullptr))
        return;

    if (pDesc != nullptr)
        View = *pDesc;
    else
        RxDefaultRtvDesc(Resource->GetApiDesc(), View);

    if (View.Format == DXGI_FORMAT_UNKNOWN && Resource != nullptr)
        View.Format = Resource->GetApiDesc().Format;

    Args.hDrvResource = Resource != nullptr ? Resource->GetDriverHandle() : D3D12DDI_HRESOURCE{};
    Args.Format = View.Format;

    switch (View.ViewDimension)
    {
        case D3D12_RTV_DIMENSION_BUFFER:
            Args.ResourceDimension = D3D12DDI_RD_BUFFER;
            Args.Buffer.FirstElement = View.Buffer.FirstElement;
            Args.Buffer.NumElements = View.Buffer.NumElements;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE1D:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args.Tex1D.MipSlice = View.Texture1D.MipSlice;
            Args.Tex1D.ArraySize = 1;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE1D;
            Args.Tex1D.MipSlice = View.Texture1DArray.MipSlice;
            Args.Tex1D.FirstArraySlice = View.Texture1DArray.FirstArraySlice;
            Args.Tex1D.ArraySize = View.Texture1DArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2D:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args.Tex2D.MipSlice = View.Texture2D.MipSlice;
            Args.Tex2D.ArraySize = 1;
            Args.Tex2D.PlaneSlice = View.Texture2D.PlaneSlice;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args.Tex2D.MipSlice = View.Texture2DArray.MipSlice;
            Args.Tex2D.FirstArraySlice = View.Texture2DArray.FirstArraySlice;
            Args.Tex2D.ArraySize = View.Texture2DArray.ArraySize;
            Args.Tex2D.PlaneSlice = View.Texture2DArray.PlaneSlice;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMS:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args.Tex2D.ArraySize = 1;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE2D;
            Args.Tex2D.FirstArraySlice = View.Texture2DMSArray.FirstArraySlice;
            Args.Tex2D.ArraySize = View.Texture2DMSArray.ArraySize;
            break;
        case D3D12_RTV_DIMENSION_TEXTURE3D:
            Args.ResourceDimension = D3D12DDI_RD_TEXTURE3D;
            Args.Tex3D.MipSlice = View.Texture3D.MipSlice;
            Args.Tex3D.FirstW = View.Texture3D.FirstWSlice;
            Args.Tex3D.WSize = View.Texture3D.WSize;
            break;
        default:
            RX_TRACE(L"render target view dimension %d not supported", View.ViewDimension);
            return;
    }

    Entry->Resource = Resource;
    Destination.ptr = Entry->DriverHandle;
    Ddi().pfnCreateRenderTargetView(DriverDevice, &Args, Destination);
}
