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

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) ||
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

VOID
STDMETHODCALLTYPE
RxDevice12::CreateConstantBufferView(
    _In_opt_ const D3D12_CONSTANT_BUFFER_VIEW_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    static const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC NullView = {};
    const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC* View = &NullView;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;

    static_assert(sizeof(D3D12_CONSTANT_BUFFER_VIEW_DESC) == sizeof(D3D12DDI_CONSTANT_BUFFER_VIEW_DESC));

    if (pDesc != nullptr)
    {
        /* Location and size go by 256 bytes, and a view covers at most 64KB */
        if ((pDesc->BufferLocation & 0xFF) != 0 || (pDesc->SizeInBytes & 0xFF) != 0 ||
            pDesc->SizeInBytes > D3D12_REQ_CONSTANT_BUFFER_ELEMENT_COUNT * 16)
        {
            RemoveOnBadView();
            return;
        }

        View = reinterpret_cast<const D3D12DDI_CONSTANT_BUFFER_VIEW_DESC*>(pDesc);
    }

    Destination.ptr = DestDescriptor.ptr;
    Ddi().pfnCreateConstantBufferView(DriverDevice, View, Destination);
}

/* Filters by reduction: plain, comparison, and minimum or maximum */
static
BOOLEAN
RxSamplerFilterKnown(
    _In_ UINT Filter,
    _Out_ BOOLEAN* Reducing,
    _Out_ BOOLEAN* Comparing,
    _Out_ BOOLEAN* PointMipAniso)
{
    const UINT Reduction = (Filter >> D3D12_FILTER_REDUCTION_TYPE_SHIFT) & D3D12_FILTER_REDUCTION_TYPE_MASK;
    const UINT Basic = Filter & ~(D3D12_FILTER_REDUCTION_TYPE_MASK << D3D12_FILTER_REDUCTION_TYPE_SHIFT);

    *Reducing = Reduction >= D3D12_FILTER_REDUCTION_TYPE_MINIMUM;
    *Comparing = Reduction == D3D12_FILTER_REDUCTION_TYPE_COMPARISON;
    *PointMipAniso = Basic == D3D12_FILTER_MIN_MAG_ANISOTROPIC_MIP_POINT;

    switch (Basic)
    {
        case D3D12_FILTER_MIN_MAG_MIP_POINT:
        case D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR:
        case D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT:
        case D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR:
        case D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT:
        case D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR:
        case D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT:
        case D3D12_FILTER_MIN_MAG_MIP_LINEAR:
        case D3D12_FILTER_MIN_MAG_ANISOTROPIC_MIP_POINT:
        case D3D12_FILTER_ANISOTROPIC:
            return TRUE;
        default:
            return FALSE;
    }
}

static
BOOLEAN
RxAddressModeKnown(
    _In_ D3D12_TEXTURE_ADDRESS_MODE Mode)
{
    return Mode >= D3D12_TEXTURE_ADDRESS_MODE_WRAP && Mode <= D3D12_TEXTURE_ADDRESS_MODE_MIRROR_ONCE;
}

static
BOOLEAN
RxAddressModeClamps(
    _In_ D3D12_TEXTURE_ADDRESS_MODE Mode)
{
    return Mode == D3D12_TEXTURE_ADDRESS_MODE_CLAMP || Mode == D3D12_TEXTURE_ADDRESS_MODE_BORDER;
}

/* The sampler checks of the system runtime; every failure is counted before giving up */
static
BOOLEAN
RxSamplerValid(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SAMPLER_DESC2& Desc)
{
    const D3D_FEATURE_LEVEL Level = Device.GetFeatureLevel();
    UINT TiledTier = Device.GetOptions().TiledResourcesTier;
    UINT Problems = 0;
    BOOLEAN Reducing = FALSE;
    BOOLEAN Comparing = FALSE;
    BOOLEAN PointMipAniso = FALSE;

    if (Level < D3D_FEATURE_LEVEL_9_1)
        Problems += 1;

    if (Level < D3D_FEATURE_LEVEL_11_1 && TiledTier > 1)
        TiledTier = 1;

    if (!RxSamplerFilterKnown(Desc.Filter, &Reducing, &Comparing, &PointMipAniso))
    {
        Problems += 1;
        Reducing = FALSE;
        Comparing = FALSE;
        PointMipAniso = FALSE;
    }
    if (Reducing && TiledTier < D3D12_TILED_RESOURCES_TIER_2)
        Problems += 1;
    if (PointMipAniso && Level < D3D_FEATURE_LEVEL_11_0)
        Problems += 1;

    if (!RxAddressModeKnown(Desc.AddressU))
        Problems += 1;
    if (!RxAddressModeKnown(Desc.AddressV))
        Problems += 1;
    if (!RxAddressModeKnown(Desc.AddressW))
        Problems += 1;
    if (Desc.MipLODBias < D3D12_MIP_LOD_BIAS_MIN || Desc.MipLODBias > D3D12_MIP_LOD_BIAS_MAX)
        Problems += 1;
    if (Desc.MaxAnisotropy > D3D12_MAX_MAXANISOTROPY)
        Problems += 1;
    if (Comparing && (UINT)Desc.ComparisonFunc > D3D12_COMPARISON_FUNC_ALWAYS)
        Problems += 1;
    if ((Desc.Flags & ~(D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR | D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES)) != 0)
        Problems += 1;

    if (Desc.Flags != D3D12_SAMPLER_FLAG_NONE)
    {
        if (Device.GetHighestShaderModel() < D3D_SHADER_MODEL_6_7)
        {
            Problems += 1;
        }
        else if ((Desc.Flags & D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES) != 0)
        {
            /* Only point or linear filters without comparison, no LOD, and clamped U and V */
            if (Device.GetDdiBuild() < 60)
                Problems += 1;
            if (Desc.Filter != D3D12_FILTER_MIN_MAG_MIP_POINT && Desc.Filter != D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT &&
                Desc.Filter != D3D12_FILTER_MINIMUM_MIN_MAG_MIP_POINT &&
                Desc.Filter != D3D12_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT &&
                Desc.Filter != D3D12_FILTER_MAXIMUM_MIN_MAG_MIP_POINT &&
                Desc.Filter != D3D12_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT)
            {
                Problems += 1;
            }
            if (Desc.MinLOD != 0.0f || Desc.MaxLOD != 0.0f || Desc.MipLODBias != 0.0f)
                Problems += 1;
            if (!RxAddressModeClamps(Desc.AddressU))
                Problems += 1;
            if (!RxAddressModeClamps(Desc.AddressV))
                Problems += 1;
        }
    }

    return Problems == 0;
}

/* Writes a checked sampler, leaving out what its filter and address modes never read */
static
VOID
RxWriteSampler(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SAMPLER_DESC2& Desc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    D3D12DDI_SAMPLER_DESC_0096 Sampler = {};
    D3D12DDIARG_CREATE_SAMPLER_0096 Args;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Destination;
    const UINT Reduction = (Desc.Filter >> D3D12_FILTER_REDUCTION_TYPE_SHIFT) & D3D12_FILTER_REDUCTION_TYPE_MASK;

    if (!RxSamplerValid(Device, Desc))
    {
        Device.RemoveOnBadView();
        return;
    }

    Sampler.Filter = (D3D12DDI_FILTER)Desc.Filter;
    Sampler.AddressU = (D3D12DDI_TEXTURE_ADDRESS_MODE)Desc.AddressU;
    Sampler.AddressV = (D3D12DDI_TEXTURE_ADDRESS_MODE)Desc.AddressV;
    Sampler.AddressW = (D3D12DDI_TEXTURE_ADDRESS_MODE)Desc.AddressW;
    Sampler.MipLODBias = Desc.MipLODBias;
    if ((Desc.Filter & D3D12_ANISOTROPIC_FILTERING_BIT) != 0)
        Sampler.MaxAnisotropy = Desc.MaxAnisotropy;

    /* Without a comparison filter the function reads as NEVER, and NONE does too */
    Sampler.ComparisonFunc = D3D12DDI_COMPARISON_FUNC_NEVER;
    if (Reduction == D3D12_FILTER_REDUCTION_TYPE_COMPARISON && (UINT)Desc.ComparisonFunc > D3D12_COMPARISON_FUNC_NONE)
        Sampler.ComparisonFunc = (D3D12DDI_COMPARISON_FUNC)Desc.ComparisonFunc;

    if (Desc.AddressU == D3D12_TEXTURE_ADDRESS_MODE_BORDER || Desc.AddressV == D3D12_TEXTURE_ADDRESS_MODE_BORDER ||
        Desc.AddressW == D3D12_TEXTURE_ADDRESS_MODE_BORDER)
    {
        memcpy(Sampler.UintBorderColor, Desc.UintBorderColor, sizeof(Sampler.UintBorderColor));
    }

    Sampler.MinLOD = Desc.MinLOD;
    Sampler.MaxLOD = Desc.MaxLOD;
    Sampler.Flags = (D3D12DDI_SAMPLER_FLAGS_0096)(Desc.Flags & 0xFF);

    Args.pSamplerDesc = &Sampler;
    Destination.ptr = DestDescriptor.ptr;
    Device.Ddi().pfnCreateSampler(Device.Handle(), &Args, Destination);
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateSampler(
    _In_ const D3D12_SAMPLER_DESC* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    D3D12_SAMPLER_DESC2 Desc = {};

    /* The older description is the newer one without flags */
    memcpy(&Desc, pDesc, sizeof(*pDesc));
    RxWriteSampler(*this, Desc, DestDescriptor);
}

VOID
STDMETHODCALLTYPE
RxDevice12::CreateSampler2(
    _In_ const D3D12_SAMPLER_DESC2* pDesc,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pDesc == nullptr)
    {
        RemoveOnBadView();
        return;
    }

    RxWriteSampler(*this, *pDesc, DestDescriptor);
}

/* Render target slots carry runtime state beside the driver's descriptor, so both move */
static
VOID
RxCopyRtvRun(
    _In_ RxDevice12& Device,
    _In_ UINT Count,
    _In_ SIZE_T Dest,
    _In_ SIZE_T Source)
{
    RxRtvEntry* To = reinterpret_cast<RxRtvEntry*>(Dest);
    const RxRtvEntry* From = reinterpret_cast<const RxRtvEntry*>(Source);
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DriverDest;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE DriverSource;
    UINT Index;

    for (Index = 0; Index < Count; Index++)
    {
        To[Index].Resource = From[Index].Resource;
        To[Index].Reserved = From[Index].Reserved;
    }

    DriverDest.ptr = To->DriverHandle;
    DriverSource.ptr = From->DriverHandle;
    Device.Ddi().pfnCopyDescriptorsSimple(Device.Handle(), Count, DriverDest, DriverSource,
                                          D3D12DDI_DESCRIPTOR_HEAP_TYPE_RTV);
}

VOID
STDMETHODCALLTYPE
RxDevice12::CopyDescriptors(
    _In_ UINT NumDestDescriptorRanges,
    _In_reads_(NumDestDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE* pDestDescriptorRangeStarts,
    _In_reads_opt_(NumDestDescriptorRanges) const UINT* pDestDescriptorRangeSizes,
    _In_ UINT NumSrcDescriptorRanges,
    _In_reads_(NumSrcDescriptorRanges) const D3D12_CPU_DESCRIPTOR_HANDLE* pSrcDescriptorRangeStarts,
    _In_reads_opt_(NumSrcDescriptorRanges) const UINT* pSrcDescriptorRangeSizes,
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    UINT DestRange = 0;
    UINT DestDone = 0;
    UINT SourceRange;

    if (DescriptorHeapsType != D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        Ddi().pfnCopyDescriptors(DriverDevice, NumDestDescriptorRanges,
                                 reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE*>(pDestDescriptorRangeStarts),
                                 pDestDescriptorRangeSizes, NumSrcDescriptorRanges,
                                 reinterpret_cast<const D3D12DDI_CPU_DESCRIPTOR_HANDLE*>(pSrcDescriptorRangeStarts),
                                 pSrcDescriptorRangeSizes, (D3D12DDI_DESCRIPTOR_HEAP_TYPE)DescriptorHeapsType);
        return;
    }

    /*
     * Render targets go run by run. Like the system runtime, a source range split over two destination
     * ranges starts again from its first slot for the second part.
     */
    for (SourceRange = 0; SourceRange < NumSrcDescriptorRanges; SourceRange++)
    {
        const UINT SourceSize = pSrcDescriptorRangeSizes != nullptr ? pSrcDescriptorRangeSizes[SourceRange] : 1;
        UINT SourceDone = 0;

        while (DestRange < NumDestDescriptorRanges)
        {
            const UINT DestSize = pDestDescriptorRangeSizes != nullptr ? pDestDescriptorRangeSizes[DestRange] : 1;
            const UINT DestLeft = DestSize - DestDone;
            const UINT SourceLeft = SourceSize - SourceDone;
            const UINT Run = DestLeft < SourceLeft ? DestLeft : SourceLeft;

            RxCopyRtvRun(*this, Run, pDestDescriptorRangeStarts[DestRange].ptr + DestDone * sizeof(RxRtvEntry),
                         pSrcDescriptorRangeStarts[SourceRange].ptr);
            DestDone += Run;
            if (Run != DestLeft && Run == SourceLeft)
                break;

            SourceDone += Run;
            DestRange += 1;
            if (Run == DestLeft)
                DestDone = 0;
        }
    }
}

VOID
STDMETHODCALLTYPE
RxDevice12::CopyDescriptorsSimple(
    _In_ UINT NumDescriptors,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptorRangeStart,
    _In_ D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptorRangeStart,
    _In_ D3D12_DESCRIPTOR_HEAP_TYPE DescriptorHeapsType)
{
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Dest;
    D3D12DDI_CPU_DESCRIPTOR_HANDLE Source;

    if (DescriptorHeapsType == D3D12_DESCRIPTOR_HEAP_TYPE_RTV)
    {
        RxCopyRtvRun(*this, NumDescriptors, DestDescriptorRangeStart.ptr, SrcDescriptorRangeStart.ptr);
        return;
    }

    Dest.ptr = DestDescriptorRangeStart.ptr;
    Source.ptr = SrcDescriptorRangeStart.ptr;
    Ddi().pfnCopyDescriptorsSimple(DriverDevice, NumDescriptors, Dest, Source,
                                   (D3D12DDI_DESCRIPTOR_HEAP_TYPE)DescriptorHeapsType);
}

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
