/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Resources: the UMD side of a resource and the API objects built on it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <initguid.h>

#include "resource.hpp"

#include <algorithm>
#include <vector>
#include <malloc.h>

UINT
RxDdiBindFlags(
    _In_ UINT BindFlags)
{
    UINT Ddi = 0;

    /* Everything but unordered access sits at the same bit in both encodings */
    Ddi |= BindFlags & (D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER | D3D11_BIND_CONSTANT_BUFFER |
                        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_RENDER_TARGET |
                        D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER);
    if (BindFlags & D3D11_BIND_UNORDERED_ACCESS)
        Ddi |= D3D11_DDI_BIND_UNORDERED_ACCESS;

    return Ddi;
}

UINT
RxDdiCpuAccessFlags(
    _In_ UINT CpuAccessFlags)
{
    UINT Ddi = 0;

    if (CpuAccessFlags & D3D11_CPU_ACCESS_WRITE)
        Ddi |= D3D10_DDI_CPU_ACCESS_WRITE;
    if (CpuAccessFlags & D3D11_CPU_ACCESS_READ)
        Ddi |= D3D10_DDI_CPU_ACCESS_READ;

    return Ddi;
}

/* The low misc bits mean the same in both encodings; cube maps travel as a dimension instead */
UINT
RxDdiMiscFlags(
    _In_ UINT MiscFlags)
{
    return MiscFlags & (D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_SHARED |
                        D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS |
                        D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_RESOURCE_CLAMP);
}

HRESULT
RxApiResult(
    _In_ HRESULT DriverResult)
{
    switch (DriverResult)
    {
        case DXGI_DDI_ERR_WASSTILLDRAWING:
            return DXGI_ERROR_WAS_STILL_DRAWING;
        case DXGI_DDI_ERR_UNSUPPORTED:
            return DXGI_ERROR_UNSUPPORTED;
        default:
            return DriverResult;
    }
}

DdiResource::~DdiResource()
{
    D3DKMT_DESTROYALLOCATION Destroy = {};

    if (IsCreated)
        Device.GetDeviceFuncs().pfnDestroyResource(Device.GetDriverDevice(), Handle);

    if (Memory != nullptr)
        _aligned_free(Memory);

    if (Runtime == nullptr)
        return;

    /* An opened resource has no allocations of ours, so the runtime closes it */
    if (Runtime->IsOpened && Runtime->KmResource != 0)
    {
        Destroy.hDevice = Device.GetKmDevice();
        Destroy.hResource = Runtime->KmResource;
        RxGetKmtThunks()->DestroyAllocation(&Destroy);
        Runtime->KmResource = 0;
    }

    /* Created ones may still be deallocated by the driver later; the device keeps the record until then */
    Device.RetireRuntimeResource(Runtime);
}

HRESULT
DdiResource::Create(
    _In_ const D3D11DDIARG_CREATERESOURCE& Args)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    D3D10DDI_HRTRESOURCE RuntimeHandle;
    SIZE_T Size;
    HRESULT hr;

    if (Runtime == nullptr)
        return E_OUTOFMEMORY;

    Size = Funcs.pfnCalcPrivateResourceSize(Device.GetDriverDevice(), &Args);
    Memory = _aligned_malloc(Size, 16);
    if (Memory == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Memory, Size);

    Handle.pDrvPrivate = Memory;
    RuntimeHandle.handle = Runtime;
    Funcs.pfnCreateResource(Device.GetDriverDevice(), &Args, Handle, RuntimeHandle);

    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateResource failed 0x%08lX: dimension %u format %u %ux%u mips %u array %u samples %u usage %u bind %X cpu %X misc %X",
                 hr, Args.ResourceDimension, Args.Format, Args.pMipInfoList ? Args.pMipInfoList[0].TexelWidth : 0,
                 Args.pMipInfoList ? Args.pMipInfoList[0].TexelHeight : 0, Args.MipLevels, Args.ArraySize,
                 Args.SampleDesc.Count, Args.Usage, Args.BindFlags, Args.MapFlags, Args.MiscFlags);
        return hr;
    }

    IsCreated = TRUE;
    return S_OK;
}

HRESULT
DdiResource::CreateTexture(
    _In_ const RxTextureShape& Shape,
    _In_reads_opt_(Shape.MipLevels * Shape.ArraySize) const D3D11_SUBRESOURCE_DATA* InitialData,
    _In_ UINT ExtraDdiBindFlags,
    _In_ UINT ExtraDdiMiscFlags)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mips[D3D11_REQ_MIP_LEVELS];
    UINT Level;

    if (Shape.MipLevels == 0 || Shape.MipLevels > D3D11_REQ_MIP_LEVELS)
        return E_INVALIDARG;

    for (Level = 0; Level < Shape.MipLevels; Level++)
    {
        Mips[Level].TexelWidth = std::max(Shape.Width >> Level, 1u);
        Mips[Level].TexelHeight = std::max(Shape.Height >> Level, 1u);
        Mips[Level].TexelDepth = std::max(Shape.Depth >> Level, 1u);
        Mips[Level].PhysicalWidth = Mips[Level].TexelWidth;
        Mips[Level].PhysicalHeight = Mips[Level].TexelHeight;
        Mips[Level].PhysicalDepth = Mips[Level].TexelDepth;
    }

    /* D3D11_SUBRESOURCE_DATA and the DDI's upload description share one layout */
    static_assert(sizeof(D3D11_SUBRESOURCE_DATA) == sizeof(D3D10_DDIARG_SUBRESOURCE_UP));

    Args.pMipInfoList = Mips;
    Args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP*>(InitialData);
    Args.ResourceDimension = Shape.Dimension;
    Args.Usage = Shape.Usage;
    Args.BindFlags = RxDdiBindFlags(Shape.BindFlags) | ExtraDdiBindFlags;
    Args.MapFlags = RxDdiCpuAccessFlags(Shape.CPUAccessFlags);
    Args.MiscFlags = RxDdiMiscFlags(Shape.MiscFlags) | ExtraDdiMiscFlags;
    Args.Format = Shape.Format;
    Args.SampleDesc = Shape.SampleDesc;
    Args.MipLevels = Shape.MipLevels;
    Args.ArraySize = Shape.ArraySize;
    Args.DecoderBufferType = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;

    return Create(Args);
}

HRESULT
DdiResource::OpenShared(
    _In_ D3DKMT_HANDLE GlobalShare)
{
    const D3DWDDM1_3DDI_DEVICEFUNCS& Funcs = Device.GetDeviceFuncs();
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_QUERYRESOURCEINFO Query = {};
    D3DKMT_OPENRESOURCE Open = {};
    D3D10DDIARG_OPENRESOURCE Args = {};
    D3D10DDI_HRTRESOURCE RuntimeHandle;
    std::vector<D3DDDI_OPENALLOCATIONINFO> Allocations;
    std::vector<BYTE> RuntimeData;
    std::vector<BYTE> ResourceData;
    std::vector<BYTE> AllocationData;
    NTSTATUS Status;
    SIZE_T Size;
    HRESULT hr;

    if (Kmt->QueryResourceInfo == nullptr || Kmt->OpenResource == nullptr)
        return DXGI_ERROR_UNSUPPORTED;

    /* The first query only reports how big everything is */
    Query.hDevice = Device.GetKmDevice();
    Query.hGlobalShare = GlobalShare;
    Status = Kmt->QueryResourceInfo(&Query);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Allocations.resize(Query.NumAllocations);
    RuntimeData.resize(std::max(Query.PrivateRuntimeDataSize, 1u));
    ResourceData.resize(std::max(Query.ResourcePrivateDriverDataSize, 1u));
    AllocationData.resize(std::max(Query.TotalPrivateDriverDataSize, 1u));

    Open.hDevice = Device.GetKmDevice();
    Open.hGlobalShare = GlobalShare;
    Open.NumAllocations = Query.NumAllocations;
    Open.pOpenAllocationInfo = Allocations.data();
    Open.pPrivateRuntimeData = RuntimeData.data();
    Open.PrivateRuntimeDataSize = Query.PrivateRuntimeDataSize;
    Open.pResourcePrivateDriverData = ResourceData.data();
    Open.ResourcePrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;
    Open.pTotalPrivateDriverDataBuffer = AllocationData.data();
    Open.TotalPrivateDriverDataBufferSize = Query.TotalPrivateDriverDataSize;
    Status = Kmt->OpenResource(&Open);
    if (!NT_SUCCESS(Status))
        return RxHresultFromStatus(Status);

    Runtime->KmResource = Open.hResource;
    Runtime->IsOpened = TRUE;

    /* The runtime data belongs to whoever created the surface; the UMD only needs its own */
    Args.NumAllocations = Open.NumAllocations;
    Args.pOpenAllocationInfo = Allocations.data();
    Args.hKMResource.handle = Open.hResource;
    Args.pPrivateDriverData = ResourceData.data();
    Args.PrivateDriverDataSize = Query.ResourcePrivateDriverDataSize;

    Size = Funcs.pfnCalcPrivateOpenedResourceSize(Device.GetDriverDevice(), &Args);
    Memory = _aligned_malloc(Size ? Size : 1, 16);
    if (Memory == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Memory, Size);

    Handle.pDrvPrivate = Memory;
    RuntimeHandle.handle = Runtime;
    Funcs.pfnOpenResource(Device.GetDriverDevice(), &Args, Handle, RuntimeHandle);

    hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD OpenResource failed 0x%08lX", hr);
        return hr;
    }

    IsCreated = TRUE;
    return S_OK;
}

HRESULT
DdiResource::CreateBuffer(
    _In_ const D3D11_BUFFER_DESC& Desc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mip = {};

    Mip.TexelWidth = Desc.ByteWidth;
    Mip.TexelHeight = 1;
    Mip.TexelDepth = 1;
    Mip.PhysicalWidth = Desc.ByteWidth;
    Mip.PhysicalHeight = 1;
    Mip.PhysicalDepth = 1;

    Args.pMipInfoList = &Mip;
    Args.pInitialDataUP = reinterpret_cast<const D3D10_DDIARG_SUBRESOURCE_UP*>(InitialData);
    Args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    Args.Usage = Desc.Usage;
    Args.BindFlags = RxDdiBindFlags(Desc.BindFlags);
    Args.MapFlags = RxDdiCpuAccessFlags(Desc.CPUAccessFlags);
    Args.MiscFlags = RxDdiMiscFlags(Desc.MiscFlags);
    Args.Format = DXGI_FORMAT_UNKNOWN;
    Args.SampleDesc.Count = 1;
    Args.MipLevels = 1;
    Args.ArraySize = 1;
    Args.ByteStride = (Desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) ? Desc.StructureByteStride : 0;
    Args.DecoderBufferType = D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;

    return Create(Args);
}

HRESULT
DdiResource::CreateDecoderBuffer(
    _In_ const D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO& Info)
{
    D3D11DDIARG_CREATERESOURCE Args = {};
    D3D10DDI_MIPINFO Mip = {};

    Mip.TexelWidth = Info.Size;
    Mip.TexelHeight = 1;
    Mip.TexelDepth = 1;
    Mip.PhysicalWidth = Info.Size;
    Mip.PhysicalHeight = 1;
    Mip.PhysicalDepth = 1;

    /* Dynamic buffers only need writing; others are read back by some drivers too */
    Args.pMipInfoList = &Mip;
    Args.ResourceDimension = D3D10DDIRESOURCE_BUFFER;
    Args.Usage = Info.Usage;
    Args.BindFlags = D3D11_DDI_BIND_DECODER;
    Args.MapFlags = (Info.Usage == D3D10_DDI_USAGE_DYNAMIC) ? D3D10_DDI_CPU_ACCESS_WRITE
                                                            : (D3D10_DDI_CPU_ACCESS_WRITE | D3D10_DDI_CPU_ACCESS_READ);
    Args.Format = DXGI_FORMAT_UNKNOWN;
    Args.SampleDesc.Count = 1;
    Args.MipLevels = 1;
    Args.ArraySize = 1;
    Args.DecoderBufferType = Info.Type;
    Args.TextureLayout = D3DWDDM2_0DDI_TL_UNDEFINED;

    return Create(Args);
}

HRESULT
DdiResource::Map(
    _In_ UINT Subresource,
    _In_ D3D11_MAP MapType,
    _In_ UINT MapFlags,
    _Out_ D3D11_MAPPED_SUBRESOURCE* Mapped)
{
    D3D10DDI_MAPPED_SUBRESOURCE Result = {};
    HRESULT hr;

    /* D3D11_MAP and the DO_NOT_WAIT flag share their values with the DDI */
    Device.GetDeviceFuncs().pfnResourceMap(Device.GetDriverDevice(), Handle, Subresource, (D3D10_DDI_MAP)MapType,
                                           MapFlags, &Result);
    hr = RxApiResult(Device.TakeDriverError());
    if (FAILED(hr))
    {
        Mapped->pData = nullptr;
        return hr;
    }

    Mapped->pData = Result.pData;
    Mapped->RowPitch = Result.RowPitch;
    Mapped->DepthPitch = Result.DepthPitch;
    return S_OK;
}

VOID
DdiResource::Unmap(
    _In_ UINT Subresource)
{
    Device.GetDeviceFuncs().pfnResourceUnmap(Device.GetDriverDevice(), Handle, Subresource);
}

_Ret_maybenull_
DdiResource*
RxGetDdiResource(
    _In_opt_ ID3D11Resource* Resource)
{
    DdiResource* Found = nullptr;

    /* The private id hands back a raw pointer and takes no reference */
    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_RxDdiResource, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

_Ret_maybenull_
RxChildLifetime*
RxGetLifetime(
    _In_opt_ ID3D11Resource* Resource)
{
    RxChildLifetime* Found = nullptr;

    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_RxChildLifetime, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

HRESULT
RxGetTexture2DDesc(
    _In_ ID3D11Resource* Resource,
    _Out_ D3D11_TEXTURE2D_DESC* Desc)
{
    ID3D11Texture2D* Texture;

    if (Resource == nullptr || FAILED(Resource->QueryInterface(IID_PPV_ARGS(&Texture))))
        return E_INVALIDARG;

    Texture->GetDesc(Desc);
    Texture->Release();
    return S_OK;
}

HRESULT
RxBuffer::Create(
    _In_ RxDevice& Device,
    _In_ const D3D11_BUFFER_DESC& Desc,
    _In_opt_ const D3D11_SUBRESOURCE_DATA* InitialData,
    _Outptr_ RxBuffer** Buffer)
{
    RxBuffer* NewBuffer;
    HRESULT hr;

    *Buffer = nullptr;

    NewBuffer = new (std::nothrow) RxBuffer(Device);
    if (NewBuffer == nullptr)
        return E_OUTOFMEMORY;

    NewBuffer->Desc = Desc;
    hr = NewBuffer->Resource.CreateBuffer(Desc, InitialData);
    if (FAILED(hr))
    {
        NewBuffer->Release();
        return hr;
    }

    *Buffer = NewBuffer;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBuffer::QueryInterface(
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

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11Resource) || riid == __uuidof(ID3D11Buffer))
    {
        *ppvObject = static_cast<ID3D11Buffer*>(this);
        AddRef();
        return S_OK;
    }

    if (RxQueryDxgiSurface(DxgiSurface, riid, ppvObject))
        return S_OK;

    return RxQueryD3D10Face(*this, this, this->Device, riid, ppvObject);
}

VOID
STDMETHODCALLTYPE
RxBuffer::GetType(
    _Out_ D3D11_RESOURCE_DIMENSION* pResourceDimension)
{
    *pResourceDimension = D3D11_RESOURCE_DIMENSION_BUFFER;
}

VOID
STDMETHODCALLTYPE
RxBuffer::SetEvictionPriority(
    _In_ UINT EvictionPriority)
{
    Priority = EvictionPriority;
}

UINT
STDMETHODCALLTYPE
RxBuffer::GetEvictionPriority(VOID)
{
    return Priority;
}

VOID
STDMETHODCALLTYPE
RxBuffer::GetDesc(
    _Out_ D3D11_BUFFER_DESC* pDesc)
{
    *pDesc = Desc;
}

BOOLEAN
RxQueryDxgiSurface(
    _In_ RxDxgiSurface& Surface,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) || riid == __uuidof(IDXGIResource) ||
        riid == __uuidof(IDXGIResource1))
    {
        *ppvObject = static_cast<IDXGIResource1*>(&Surface);
        Surface.AddRef();
        return TRUE;
    }

    if ((riid == __uuidof(IDXGISurface) || riid == __uuidof(IDXGISurface1) || riid == __uuidof(IDXGISurface2)) &&
        Surface.IsSurface())
    {
        *ppvObject = static_cast<IDXGISurface2*>(&Surface);
        Surface.AddRef();
        return TRUE;
    }

    return FALSE;
}
