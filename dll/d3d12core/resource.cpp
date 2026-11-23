/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Committed resources, their sizes and their copyable footprints
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formats.hpp"
#include "formatlayout12.hpp"

constexpr UINT64 RxDefaultPlacement = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
constexpr UINT64 RxMsaaPlacement = D3D12_DEFAULT_MSAA_RESOURCE_PLACEMENT_ALIGNMENT;

static
UINT64
RxAlignUp(
    _In_ UINT64 Value,
    _In_ UINT64 Alignment)
{
    return Alignment ? (Value + Alignment - 1) / Alignment * Alignment : Value;
}

static
UINT
RxHighestBit(
    _In_ UINT64 Value)
{
    UINT Bit = 0;

    while (Value > 1)
    {
        Value >>= 1;
        Bit++;
    }

    return Bit;
}

BOOLEAN
RxFormatBlock(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* BlockWidth,
    _Out_ UINT* BlockHeight,
    _Out_ UINT* BlockBytes)
{
    UINT Bits = RxFormatBits(Format);

    *BlockWidth = 1;
    *BlockHeight = 1;
    *BlockBytes = 0;

    switch (Format)
    {
        /* Two texels share one element */
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
        case DXGI_FORMAT_YUY2:
            *BlockWidth = 2;
            *BlockBytes = 4;
            return TRUE;
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
            *BlockWidth = 2;
            *BlockBytes = 8;
            return TRUE;
        default:
            break;
    }

    if (Bits == 0 || Bits % 8 != 0)
        return FALSE;

    if (RxIsBlockCompressed(Format))
    {
        *BlockWidth = 4;
        *BlockHeight = 4;
    }

    *BlockBytes = Bits / 8;
    return TRUE;
}

VOID
RxFillResourceDefaults(
    _Inout_ D3D12_RESOURCE_DESC1& Desc)
{
    UINT64 Largest;

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        if (Desc.Alignment == 0)
            Desc.Alignment = RxDefaultPlacement;
        return;
    }

    Largest = Desc.Width;
    if (Desc.Height > Largest)
        Largest = Desc.Height;
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && Desc.DepthOrArraySize > Largest)
        Largest = Desc.DepthOrArraySize;

    if (Desc.MipLevels == 0)
        Desc.MipLevels = (UINT16)(RxHighestBit(Largest) + 1);
    if (Desc.Alignment == 0)
        Desc.Alignment = Desc.SampleDesc.Count > 1 ? RxMsaaPlacement : RxDefaultPlacement;
}

/* API resource states as the barrier layout the driver starts a resource in */
static
D3D12DDI_BARRIER_LAYOUT
RxInitialLayout(
    _In_ D3D12_RESOURCE_STATES State)
{
    constexpr UINT ReadStates = D3D12_RESOURCE_STATE_GENERIC_READ | D3D12_RESOURCE_STATE_DEPTH_READ |
                                D3D12_RESOURCE_STATE_RESOLVE_SOURCE | D3D12_RESOURCE_STATE_PREDICATION |
                                D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
    constexpr UINT ShaderStates = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    switch (State)
    {
        case D3D12_RESOURCE_STATE_COMMON:
            return (D3D12DDI_BARRIER_LAYOUT)0;
        case D3D12_RESOURCE_STATE_GENERIC_READ:
        case D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE:
            return (D3D12DDI_BARRIER_LAYOUT)31;
        case D3D12_RESOURCE_STATE_COPY_SOURCE:
            return (D3D12DDI_BARRIER_LAYOUT)0x80000000;
        case D3D12_RESOURCE_STATE_COPY_DEST:
            return (D3D12DDI_BARRIER_LAYOUT)0x80000001;
        case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
            return (D3D12DDI_BARRIER_LAYOUT)0x80000002;
        case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
            return (D3D12DDI_BARRIER_LAYOUT)0x80000003;
        case D3D12_RESOURCE_STATE_RENDER_TARGET:
            return (D3D12DDI_BARRIER_LAYOUT)2;
        case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
            return (D3D12DDI_BARRIER_LAYOUT)3;
        case D3D12_RESOURCE_STATE_DEPTH_WRITE:
            return (D3D12DDI_BARRIER_LAYOUT)4;
        case D3D12_RESOURCE_STATE_DEPTH_READ:
            return (D3D12DDI_BARRIER_LAYOUT)5;
        case D3D12_RESOURCE_STATE_RESOLVE_SOURCE:
            return (D3D12DDI_BARRIER_LAYOUT)9;
        case D3D12_RESOURCE_STATE_RESOLVE_DEST:
            return (D3D12DDI_BARRIER_LAYOUT)10;
        case D3D12_RESOURCE_STATE_STREAM_OUT:
        case D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE:
            return (D3D12DDI_BARRIER_LAYOUT)0xFFFFFFFF;
        default:
            break;
    }

    if ((State & ShaderStates) != 0)
        return (D3D12DDI_BARRIER_LAYOUT)31;
    if ((State & ~ReadStates) == 0)
        return (D3D12DDI_BARRIER_LAYOUT)19;
    return (D3D12DDI_BARRIER_LAYOUT)1;
}

VOID
RxToDdiResource(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _Out_ D3D12DDIARG_CREATERESOURCE_0111& Ddi)
{
    D3D12_RESOURCE_FLAGS Flags = Desc.Flags;
    UINT DdiFlags = 0;

    ZeroMemory(&Ddi, sizeof(Ddi));

    switch (Desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            Ddi.ResourceType = D3D12DDI_RT_BUFFER;
            Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            Ddi.ResourceType = D3D12DDI_RT_TEXTURE1D;
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            Ddi.ResourceType = D3D12DDI_RT_TEXTURE2D;
            break;
        default:
            Ddi.ResourceType = D3D12DDI_RT_TEXTURE3D;
            break;
    }

    Ddi.Width = Desc.Width;
    Ddi.Height = Desc.Height;
    Ddi.DepthOrArraySize = Desc.DepthOrArraySize;
    Ddi.MipLevels = Desc.MipLevels;
    Ddi.Format = Desc.Format;
    Ddi.SampleDesc = Desc.SampleDesc;
    Ddi.Layout = (D3D12DDI_TEXTURE_LAYOUT)Desc.Layout;
    Ddi.SamplerFeedbackMipRegion.Width = Desc.SamplerFeedbackMipRegion.Width;
    Ddi.SamplerFeedbackMipRegion.Height = Desc.SamplerFeedbackMipRegion.Height;
    Ddi.SamplerFeedbackMipRegion.Depth = Desc.SamplerFeedbackMipRegion.Depth;

    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0003_RENDER_TARGET;
    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0003_DEPTH_STENCIL;
    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0022_UNORDERED_ACCESS;
    if ((Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0003_SHADER_RESOURCE;
    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0003_CROSS_ADAPTER;
    if ((Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0003_SIMULTANEOUS_ACCESS;
    if ((Flags & D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0020_VIDEO_DECODE_REFERENCE_ONLY;
    if ((Flags & D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0080_VIDEO_ENCODE_REFERENCE_ONLY;
    if ((Flags & D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE) != 0)
        DdiFlags |= D3D12DDI_RESOURCE_FLAG_0088_RAYTRACING_ACCELERATION_STRUCTURE;
    Ddi.Flags = (D3D12DDI_RESOURCE_FLAGS_0003)DdiFlags;
}

/*
 * The optimizations a resource asks the driver for. Layouts must come out the same every time when the
 * caller only asks for sizes, and for buffers; a committed texture may get any layout the driver likes.
 */
static
UINT
RxOptimizationFlags(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ BOOLEAN SizeQuery,
    _In_ D3D12_HEAP_FLAGS HeapFlags)
{
    BOOLEAN IsBuffer = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    UINT Optimization = 0;

    if ((HeapFlags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) != 0)
        Optimization |= D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_PRIMARY;
    if (SizeQuery || IsBuffer)
        Optimization |= D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_DETERMINISTIC;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0)
        Optimization |= D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_SHADER_RESOURCE;
    if (Desc.SampleDesc.Count == 1 && (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) == 0 &&
        (IsBuffer || (Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0))
    {
        Optimization |= D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_UNORDERED_ACCESS;
    }

    return Optimization;
}

/* What the driver says a resource needs, in the API's form, and the layout it picked */
static
D3D12_RESOURCE_ALLOCATION_INFO
RxAllocationInfo(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const D3D12DDIARG_CREATERESOURCE_0111& Ddi,
    _In_ BOOLEAN SizeQuery,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _Out_opt_ D3D12DDI_TEXTURE_LAYOUT* DriverLayout)
{
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Info = {};
    D3D12_RESOURCE_ALLOCATION_INFO Result;
    UINT Optimization = RxOptimizationFlags(Desc, SizeQuery, HeapFlags);
    UINT64 Alignment;
    UINT64 Size;

    Device.Ddi().pfnCheckResourceAllocationInfo(Device.Handle(), &Ddi, (D3D12DDI_RESOURCE_OPTIMIZATION_FLAGS)Optimization,
                                                (UINT32)Desc.Alignment, 1, &Info);
    if (DriverLayout != nullptr)
        *DriverLayout = Info.Layout;

    Alignment = Desc.Alignment;
    if (Info.ResourceDataAlignment > Alignment)
        Alignment = Info.ResourceDataAlignment;
    if (Info.AdditionalDataHeaderAlignment > Alignment)
        Alignment = Info.AdditionalDataHeaderAlignment;
    if (Info.AdditionalDataAlignment > Alignment)
        Alignment = Info.AdditionalDataAlignment;

    Size = RxAlignUp(Info.ResourceDataSize, Info.ResourceDataAlignment);
    Size = RxAlignUp(RxAlignUp(Size, Info.AdditionalDataHeaderAlignment) + Info.AdditionalDataHeaderSize,
                     Info.AdditionalDataAlignment) + Info.AdditionalDataSize;

    Result.SizeInBytes = RxAlignUp(Size, Alignment);
    Result.Alignment = Alignment;
    return Result;
}

/* Sizes and alignments of several resources laid out one after another, as one heap would hold them */
static
D3D12_RESOURCE_ALLOCATION_INFO
RxAllocationInfoForDescs(
    _In_ RxDevice12& Device,
    _In_ UINT Count,
    _In_reads_(Count) const D3D12_RESOURCE_DESC1* Descs,
    _Out_writes_opt_(Count) D3D12_RESOURCE_ALLOCATION_INFO1* Each)
{
    D3D12_RESOURCE_ALLOCATION_INFO Total = { 0, 0 };
    UINT Index;

    /* One bad description fails the whole query before anything is written */
    for (Index = 0; Index < Count; Index++)
    {
        if (Count > 0x3333333 || FAILED(RxValidateResourceDesc(Device, Descs[Index], nullptr)))
        {
            Total.SizeInBytes = UINT64_MAX;
            Total.Alignment = RxDefaultPlacement;
            return Total;
        }
    }

    for (Index = 0; Index < Count; Index++)
    {
        D3D12_RESOURCE_DESC1 Desc = Descs[Index];
        D3D12DDIARG_CREATERESOURCE_0111 Ddi;
        D3D12_RESOURCE_ALLOCATION_INFO Info;
        UINT64 Offset;

        RxFillResourceDefaults(Desc);
        RxToDdiResource(Desc, Ddi);
        Info = RxAllocationInfo(Device, Desc, Ddi, TRUE, D3D12_HEAP_FLAG_NONE, nullptr);

        Offset = RxAlignUp(Total.SizeInBytes, Info.Alignment);
        Total.SizeInBytes = Offset + Info.SizeInBytes;
        if (Info.Alignment > Total.Alignment)
            Total.Alignment = Info.Alignment;

        if (Each != nullptr)
        {
            Each[Index].Offset = Offset;
            Each[Index].Alignment = Info.Alignment;
            Each[Index].SizeInBytes = Info.SizeInBytes;
        }
    }

    Total.SizeInBytes = RxAlignUp(Total.SizeInBytes, Total.Alignment);
    return Total;
}

/* Committed resources */

static
D3D12_HEAP_PROPERTIES
RxCustomProperties(
    _In_ const RxDevice12& Device,
    _In_ D3D12_HEAP_TYPE Type)
{
    D3D12_HEAP_PROPERTIES Properties = {};

    Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
    Properties.CreationNodeMask = 1;
    Properties.VisibleNodeMask = 1;

    switch (Type)
    {
        case D3D12_HEAP_TYPE_DEFAULT:
            Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
            Properties.MemoryPoolPreference = Device.IsUma() ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
        case D3D12_HEAP_TYPE_UPLOAD:
            Properties.CPUPageProperty = Device.IsCacheCoherentUma() ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                                                                     : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;
        case D3D12_HEAP_TYPE_READBACK:
            Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
            Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
            break;
        case D3D12_HEAP_TYPE_GPU_UPLOAD:
            Properties.CPUPageProperty = Device.IsCacheCoherentUma() ? D3D12_CPU_PAGE_PROPERTY_WRITE_BACK
                                                                     : D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE;
            Properties.MemoryPoolPreference = Device.IsUma() ? D3D12_MEMORY_POOL_L0 : D3D12_MEMORY_POOL_L1;
            break;
        default:
            Properties.Type = (D3D12_HEAP_TYPE)0;
            break;
    }

    return Properties;
}

/* The heap flags a committed resource's own heap carries: it holds only that kind of resource */
static
D3D12_HEAP_FLAGS
RxImplicitHeapFlags(
    _In_ D3D12_HEAP_FLAGS Flags,
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return Flags | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

    if ((Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0)
        return Flags | D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES;

    return Flags | D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES;
}

HRESULT
RxResource::CreateCommitted(
    _In_ RxDevice12& Device,
    _In_ const D3D12_HEAP_PROPERTIES& HeapProperties,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ D3D12_RESOURCE_STATES InitialState,
    _In_opt_ const D3D12_CLEAR_VALUE* ClearValue,
    _Outptr_ RxResource** Resource)
{
    D3D12_RESOURCE_DESC1 Normal = Desc;
    RxResource* Created;
    HRESULT hr;

    *Resource = nullptr;
    RxFillResourceDefaults(Normal);

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxResource(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(HeapProperties, HeapFlags, Normal, InitialState, ClearValue);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Resource = Created;
    return S_OK;
}

HRESULT
RxResource::Initialize(
    _In_ const D3D12_HEAP_PROPERTIES& HeapProperties,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC1& ResourceDesc,
    _In_ D3D12_RESOURCE_STATES InitialState,
    _In_opt_ const D3D12_CLEAR_VALUE* ClearValue)
{
    const D3D12DDI_DEVICE_FUNCS_CORE_0116& Ddi = Device.Ddi();
    D3D12DDIARG_CREATERESOURCE_0111 DdiResource;
    D3D12DDIARG_CREATEHEAP_0001 DdiHeap = {};
    D3D12DDIARG_ROW_MAJOR_RESOURCE_LAYOUT RowMajor = {};
    D3D12DDI_CLEAR_VALUES DdiClear = {};
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Existing = {};
    D3D12DDI_HEAP_AND_RESOURCE_SIZES Sizes;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    D3D12_HEAP_PROPERTIES Custom;
    D3D12DDI_HRTRESOURCE RuntimeHandle;
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 NoSession = {};
    UINT DdiHeapFlags = 0;
    UINT Index;
    HRESULT hr;

    Desc = ResourceDesc;
    Properties = HeapProperties;
    Properties.CreationNodeMask = 1;
    Properties.VisibleNodeMask = 1;
    Flags = RxImplicitHeapFlags(HeapFlags, Desc);

    RxToDdiResource(Desc, DdiResource);

    if (Desc.Layout == D3D12_TEXTURE_LAYOUT_ROW_MAJOR && Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        UINT BlockWidth;
        UINT BlockHeight;
        UINT BlockBytes;

        RxFormatBlock(Desc.Format, &BlockWidth, &BlockHeight, &BlockBytes);
        RowMajor.RowPitch = (UINT)RxAlignUp((Desc.Width + BlockWidth - 1) / BlockWidth * BlockBytes, 128);
        RowMajor.SlicePitch = RowMajor.RowPitch * (UINT)RxAlignUp((Desc.Height + BlockHeight - 1) / BlockHeight, 4);
        DdiResource.pRowMajorLayout = &RowMajor;
    }

    /* The size check sees no initial layout; creation then takes the layout the driver chose */
    Info = RxAllocationInfo(Device, Desc, DdiResource, FALSE, Flags, &DdiResource.Layout);
    if (Info.SizeInBytes >= 0xFFFFFFFFFFC00000ull)
        return E_OUTOFMEMORY;
    DdiResource.InitialBarrierLayout = RxInitialLayout(InitialState);

    Custom = Properties.Type == D3D12_HEAP_TYPE_CUSTOM ? Properties : RxCustomProperties(Device, Properties.Type);

    DdiHeap.ByteSize = Info.SizeInBytes;
    DdiHeap.Alignment = Info.Alignment;
    DdiHeap.CPUPageProperty = Custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE ? D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE
                              : Custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE ? D3D12DDI_CPU_PAGE_PROPERTY_WRITE_COMBINE
                                                                                                : D3D12DDI_CPU_PAGE_PROPERTY_WRITE_BACK;
    DdiHeap.MemoryPool = Custom.MemoryPoolPreference == D3D12_MEMORY_POOL_L0 ? D3D12DDI_MEMORY_POOL_L0 : D3D12DDI_MEMORY_POOL_L1;
    DdiHeap.CreationNodeMask = 1;
    DdiHeap.VisibleNodeMask = 1;

    /* Below tier 2 a heap says which kinds of resource it holds */
    if (Device.GetOptions().ResourceHeapTier >= D3D12DDI_RESOURCE_HEAP_TIER_2)
    {
        DdiHeapFlags = D3D12DDI_HEAP_FLAG_BUFFERS | D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES | D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES;
    }
    else
    {
        if ((Flags & D3D12_HEAP_FLAG_DENY_BUFFERS) == 0)
            DdiHeapFlags |= D3D12DDI_HEAP_FLAG_BUFFERS;
        if ((Flags & D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES) == 0)
            DdiHeapFlags |= D3D12DDI_HEAP_FLAG_RT_DS_TEXTURES;
        if ((Flags & D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES) == 0)
            DdiHeapFlags |= D3D12DDI_HEAP_FLAG_NON_RT_DS_TEXTURES;
    }
    if ((Flags & D3D12_HEAP_FLAG_ALLOW_DISPLAY) != 0)
        DdiHeapFlags |= D3D12DDI_HEAP_FLAG_PRIMARY;
    if ((Flags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) != 0 ||
        (DdiHeap.CPUPageProperty != D3D12DDI_CPU_PAGE_PROPERTY_NOT_AVAILABLE && !Device.IsCacheCoherentUma()))
    {
        DdiHeapFlags |= D3D12DDI_HEAP_FLAG_COHERENT_SYSTEMWIDE;
    }
    DdiHeapFlags |= D3D12DDI_HEAP_FLAG_0115_IMPLICIT;
    DdiHeap.Flags = (D3D12DDI_HEAP_FLAGS)DdiHeapFlags;

    if (ClearValue != nullptr)
    {
        DdiClear.Format = ClearValue->Format;
        if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        {
            for (Index = 0; Index < 4; Index++)
            {
                UINT Bits;

                memcpy(&Bits, &ClearValue->Color[Index], sizeof(Bits));
                if ((Bits & 0x7F800000) == 0)
                    Bits &= 0x80000000;
                memcpy(&DdiClear.Color[Index], &Bits, sizeof(Bits));
            }
        }
        else
        {
            DdiClear.DepthStencil.Depth = ClearValue->DepthStencil.Depth;
            DdiClear.DepthStencil.Stencil = ClearValue->DepthStencil.Stencil;
        }
    }

    /* Sized once for the heap object and once for the resource it carries, as the system core does */
    Sizes = Ddi.pfnCalcPrivateHeapAndResourceSizes(Device.Handle(), &DdiHeap, &DdiResource, NoSession);
    Sizes = Ddi.pfnCalcPrivateHeapAndResourceSizes(Device.Handle(), &DdiHeap, &DdiResource, NoSession);
    DriverHeap.pDrvPrivate = HeapMemory.Allocate(Sizes.Heap);
    DriverResource.pDrvPrivate = ResourceMemory.Allocate(Sizes.Resource);
    if (DriverHeap.pDrvPrivate == nullptr || DriverResource.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    RuntimeHandle.handle = &Runtime;
    hr = Ddi.pfnCreateHeapAndResource(Device.Handle(), &DdiHeap, DriverHeap, RuntimeHandle, &DdiResource,
                                      ClearValue != nullptr ? &DdiClear : nullptr, NoSession, DriverResource);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateHeapAndResource failed 0x%08lX", hr);
        return hr;
    }
    IsDriverResourceCreated = TRUE;

    Ddi.pfnCheckExistingResourceAllocationInfo(Device.Handle(), DriverResource, &Existing);

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        GpuAddress = Ddi.pfnCheckResourceVirtualAddress(Device.Handle(), DriverResource);

    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_HEAP);
}

RxResource::~RxResource()
{
    if (MapCount != 0)
        Device.Ddi().pfnUnmapHeap(Device.Handle(), DriverHeap);

    if (IsDriverResourceCreated)
        Device.Ddi().pfnDestroyHeapAndResource(Device.Handle(), DriverHeap, DriverResource);
}

HRESULT
STDMETHODCALLTYPE
RxResource::QueryInterface(
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
        riid == __uuidof(ID3D12Pageable) || riid == __uuidof(ID3D12Resource) || riid == __uuidof(ID3D12Resource1) ||
        riid == __uuidof(ID3D12Resource2) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12Resource2*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

UINT
RxResource::SubresourceCount() const
{
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return 1;
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        return Desc.MipLevels;
    return Desc.MipLevels * Desc.DepthOrArraySize;
}

HRESULT
STDMETHODCALLTYPE
RxResource::Map(
    UINT Subresource,
    _In_opt_ const D3D12_RANGE* pReadRange,
    _Outptr_opt_result_bytebuffer_(_Inexpressible_("Dependent on resource")) void** ppData)
{
    D3D12DDI_SUBRESOURCE_INFO Info = {};
    D3D12_HEAP_PROPERTIES Custom;
    HRESULT hr;

    if (ppData != nullptr)
        *ppData = nullptr;

    Custom = Properties.Type == D3D12_HEAP_TYPE_CUSTOM ? Properties : RxCustomProperties(Device, Properties.Type);
    if (Custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE || Subresource >= SubresourceCount())
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&MapLock);
    if (MapCount == 0)
    {
        hr = Device.Ddi().pfnMapHeap(Device.Handle(), DriverHeap, reinterpret_cast<void**>(&MappedBase));
        if (FAILED(hr))
        {
            ReleaseSRWLockExclusive(&MapLock);
            return hr;
        }
    }
    MapCount++;
    ReleaseSRWLockExclusive(&MapLock);

    if (ppData != nullptr)
    {
        Device.Ddi().pfnCheckSubresourceInfo(Device.Handle(), DriverResource, Subresource, &Info);
        *ppData = MappedBase + Info.Offset;
    }

    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxResource::Unmap(
    UINT Subresource,
    _In_opt_ const D3D12_RANGE* pWrittenRange)
{
    AcquireSRWLockExclusive(&MapLock);
    if (MapCount != 0)
    {
        MapCount -= 1;
        if (MapCount == 0)
        {
            Device.Ddi().pfnUnmapHeap(Device.Handle(), DriverHeap);
            MappedBase = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&MapLock);
}

D3D12_RESOURCE_DESC
STDMETHODCALLTYPE
RxResource::GetDesc(VOID)
{
    D3D12_RESOURCE_DESC Old;

    Old.Dimension = Desc.Dimension;
    Old.Alignment = Desc.Alignment;
    Old.Width = Desc.Width;
    Old.Height = Desc.Height;
    Old.DepthOrArraySize = Desc.DepthOrArraySize;
    Old.MipLevels = Desc.MipLevels;
    Old.Format = Desc.Format;
    Old.SampleDesc = Desc.SampleDesc;
    Old.Layout = Desc.Layout;
    Old.Flags = Desc.Flags;
    return Old;
}

D3D12_RESOURCE_DESC1
STDMETHODCALLTYPE
RxResource::GetDesc1(VOID)
{
    return Desc;
}

D3D12_GPU_VIRTUAL_ADDRESS
STDMETHODCALLTYPE
RxResource::GetGPUVirtualAddress(VOID)
{
    return GpuAddress;
}

HRESULT
STDMETHODCALLTYPE
RxResource::GetHeapProperties(
    _Out_opt_ D3D12_HEAP_PROPERTIES* pHeapProperties,
    _Out_opt_ D3D12_HEAP_FLAGS* pHeapFlags)
{
    if (pHeapProperties != nullptr)
        *pHeapProperties = Properties;
    if (pHeapFlags != nullptr)
        *pHeapFlags = Flags;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxResource::GetProtectedResourceSession(
    REFIID riid,
    _COM_Outptr_opt_ void** ppProtectedSession)
{
    if (ppProtectedSession != nullptr)
        *ppProtectedSession = nullptr;
    return DXGI_ERROR_NOT_FOUND;
}

/* Copyable footprints, worked out by the runtime alone */

BOOLEAN
RxCopyableFootprints(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ UINT FirstSubresource,
    _In_ UINT NumSubresources,
    _In_ UINT64 BaseOffset,
    _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT* Layouts,
    _Out_writes_opt_(NumSubresources) UINT* NumRows,
    _Out_writes_opt_(NumSubresources) UINT64* RowSizeInBytes,
    _Out_opt_ UINT64* TotalBytes)
{
    D3D12_RESOURCE_DESC1 Normal = Desc;
    BOOLEAN Buffer = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;
    UINT PerPlane;
    UINT Subresources;
    UINT64 Running = 0;
    UINT64 Total = 0;
    UINT Index;

    if (FAILED(RxValidateResourceDesc(Device, Desc, nullptr)))
        return FALSE;
    if (Buffer && Desc.Width >= 0xFFFFFFFF)
        return FALSE;

    RxFillResourceDefaults(Normal);
    PerPlane = Normal.MipLevels;
    if (Normal.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        PerPlane *= Normal.DepthOrArraySize;
    Subresources = PerPlane * (Buffer ? 1 : RxFormatPlaneCount(Normal.Format));

    if (NumSubresources != 0 && (FirstSubresource >= Subresources || NumSubresources > Subresources ||
                                 FirstSubresource + NumSubresources > Subresources))
    {
        return FALSE;
    }

    for (Index = 0; Index < NumSubresources; Index++)
    {
        UINT Subresource = FirstSubresource + Index;
        UINT Mip = Subresource % Normal.MipLevels;
        DXGI_FORMAT Format = Normal.Format;
        UINT64 Width = Normal.Width;
        UINT Height = Normal.Height;
        UINT Depth = 1;
        UINT BlockWidth = 1;
        UINT BlockHeight = 1;
        UINT BlockBytes = 1;
        UINT64 PitchWidth = Width;
        UINT64 Columns;
        UINT Rows;
        UINT64 RowSize;
        UINT64 RowPitch;
        UINT64 Offset;

        /* Planar formats copy each plane in a format of its own */
        if (!Buffer)
        {
            RxFormatPlane(Normal.Format, Subresource / PerPlane, Normal.Width, Normal.Height, &Format, &Width, &Height,
                          &PitchWidth);
            RxFormatBlock(Format, &BlockWidth, &BlockHeight, &BlockBytes);
        }

        Width = Width >> Mip ? Width >> Mip : 1;
        PitchWidth = PitchWidth >> Mip ? PitchWidth >> Mip : 1;
        Height = Height >> Mip ? Height >> Mip : 1;
        if (Normal.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
            Depth = Normal.DepthOrArraySize >> Mip ? Normal.DepthOrArraySize >> Mip : 1;

        Columns = (Width + BlockWidth - 1) / BlockWidth;
        Rows = (Height + BlockHeight - 1) / BlockHeight;
        RowSize = Columns * BlockBytes;
        /* Buffers report the copy pitch too, though their one row never steps by it; planar rows align further */
        RowPitch = RxAlignUp((PitchWidth + BlockWidth - 1) / BlockWidth * BlockBytes,
                             RxFormatIsPlanar(Normal.Format) ? D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT
                                                             : D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

        Offset = Buffer ? Running : RxAlignUp(Running, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

        if (Layouts != nullptr)
        {
            Layouts[Index].Offset = BaseOffset + Offset;
            Layouts[Index].Footprint.Format = Format;
            Layouts[Index].Footprint.Width = (UINT)(Columns * BlockWidth);
            Layouts[Index].Footprint.Height = Rows * BlockHeight;
            Layouts[Index].Footprint.Depth = Depth;
            Layouts[Index].Footprint.RowPitch = (UINT)RowPitch;
        }
        if (NumRows != nullptr)
            NumRows[Index] = Rows;
        if (RowSizeInBytes != nullptr)
            RowSizeInBytes[Index] = RowSize;

        Total = Offset + RowSize + RowPitch * ((UINT64)Rows * Depth - 1);
        Running = Offset + RowPitch * Rows * Depth;
    }

    if (TotalBytes != nullptr)
        *TotalBytes = Total;
    return TRUE;
}

/* Device methods */

static
D3D12_RESOURCE_DESC1
RxWidenDesc(
    _In_ const D3D12_RESOURCE_DESC& Desc)
{
    D3D12_RESOURCE_DESC1 Wide = {};

    Wide.Dimension = Desc.Dimension;
    Wide.Alignment = Desc.Alignment;
    Wide.Width = Desc.Width;
    Wide.Height = Desc.Height;
    Wide.DepthOrArraySize = Desc.DepthOrArraySize;
    Wide.MipLevels = Desc.MipLevels;
    Wide.Format = Desc.Format;
    Wide.SampleDesc = Desc.SampleDesc;
    Wide.Layout = Desc.Layout;
    Wide.Flags = Desc.Flags;
    return Wide;
}

static
HRESULT
RxCreateCommittedCommon(
    _In_ RxDevice12& Device,
    _In_opt_ const D3D12_HEAP_PROPERTIES* pHeapProperties,
    _In_ D3D12_HEAP_FLAGS HeapFlags,
    _In_opt_ const D3D12_RESOURCE_DESC1* pDesc,
    _In_ D3D12_RESOURCE_STATES InitialResourceState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    _In_ REFIID riidResource,
    _COM_Outptr_opt_ void** ppvResource)
{
    RxResource* Resource;
    HRESULT hr;

    if (ppvResource != nullptr)
        *ppvResource = nullptr;
    if (pHeapProperties == nullptr)
        return E_INVALIDARG;

    /* One node: the creation node validation reads is always the first */
    hr = RxValidateCommittedHeap(Device, *pHeapProperties, HeapFlags, 0);
    if (FAILED(hr))
        return hr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxValidateHeapPlacement(Device, pHeapProperties->Type, HeapFlags, *pDesc, pOptimizedClearValue);
    if (FAILED(hr))
        return hr;
    hr = RxValidateInitialState(Device, InitialResourceState, *pDesc, pHeapProperties->Type);
    if (FAILED(hr))
        return hr;

    if ((HeapFlags & D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER) != 0 &&
        (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || pDesc->Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR))
    {
        return E_INVALIDARG;
    }
    if ((HeapFlags & D3D12_HEAP_FLAG_ALLOW_SHADER_ATOMICS) != 0 &&
        (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) == 0)
    {
        return E_INVALIDARG;
    }

    /* Without an out pointer the call only answers whether creation would work */
    if (ppvResource == nullptr)
        return S_FALSE;
    if (pProtectedSession != nullptr)
        return E_NOTIMPL;

    hr = RxResource::CreateCommitted(Device, *pHeapProperties, HeapFlags, *pDesc, InitialResourceState,
                                     pOptimizedClearValue, &Resource);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Resource, riidResource, ppvResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommittedResource(
    _In_ const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riidResource,
    _COM_Outptr_opt_ void** ppvResource)
{
    D3D12_RESOURCE_DESC1 Wide;

    if (pDesc != nullptr)
        Wide = RxWidenDesc(*pDesc);

    return RxCreateCommittedCommon(*this, pHeapProperties, HeapFlags, pDesc != nullptr ? &Wide : nullptr,
                                   InitialResourceState, pOptimizedClearValue, nullptr, riidResource, ppvResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommittedResource1(
    _In_ const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    REFIID riidResource,
    _COM_Outptr_opt_ void** ppvResource)
{
    D3D12_RESOURCE_DESC1 Wide;

    if (pDesc != nullptr)
        Wide = RxWidenDesc(*pDesc);

    return RxCreateCommittedCommon(*this, pHeapProperties, HeapFlags, pDesc != nullptr ? &Wide : nullptr,
                                   InitialResourceState, pOptimizedClearValue, pProtectedSession, riidResource,
                                   ppvResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateCommittedResource2(
    _In_ const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags,
    _In_ const D3D12_RESOURCE_DESC1* pDesc,
    D3D12_RESOURCE_STATES InitialResourceState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    REFIID riidResource,
    _COM_Outptr_opt_ void** ppvResource)
{
    return RxCreateCommittedCommon(*this, pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                   pOptimizedClearValue, pProtectedSession, riidResource, ppvResource);
}

D3D12_RESOURCE_ALLOCATION_INFO
STDMETHODCALLTYPE
RxDevice12::GetResourceAllocationInfo(
    _In_ UINT visibleMask,
    _In_ UINT numResourceDescs,
    _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC* pResourceDescs)
{
    return GetResourceAllocationInfo1(visibleMask, numResourceDescs, pResourceDescs, nullptr);
}

D3D12_RESOURCE_ALLOCATION_INFO
STDMETHODCALLTYPE
RxDevice12::GetResourceAllocationInfo1(
    UINT visibleMask,
    UINT numResourceDescs,
    _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC* pResourceDescs,
    _Out_writes_opt_(numResourceDescs) D3D12_RESOURCE_ALLOCATION_INFO1* pResourceAllocationInfo1)
{
    D3D12_RESOURCE_DESC1 Stack[8];
    D3D12_RESOURCE_DESC1* Wide = Stack;
    D3D12_RESOURCE_ALLOCATION_INFO Result;
    UINT Index;

    if (numResourceDescs == 0 || pResourceDescs == nullptr)
        return { 0, 0 };

    if (numResourceDescs > _countof(Stack))
    {
        Wide = new (std::nothrow) D3D12_RESOURCE_DESC1[numResourceDescs];
        if (Wide == nullptr)
            return { UINT64_MAX, RxDefaultPlacement };
    }

    for (Index = 0; Index < numResourceDescs; Index++)
        Wide[Index] = RxWidenDesc(pResourceDescs[Index]);

    Result = RxAllocationInfoForDescs(*this, numResourceDescs, Wide, pResourceAllocationInfo1);

    if (Wide != Stack)
        delete[] Wide;
    return Result;
}

D3D12_RESOURCE_ALLOCATION_INFO
STDMETHODCALLTYPE
RxDevice12::GetResourceAllocationInfo2(
    UINT visibleMask,
    UINT numResourceDescs,
    _In_reads_(numResourceDescs) const D3D12_RESOURCE_DESC1* pResourceDescs,
    _Out_writes_opt_(numResourceDescs) D3D12_RESOURCE_ALLOCATION_INFO1* pResourceAllocationInfo1)
{
    if (numResourceDescs == 0 || pResourceDescs == nullptr)
        return { 0, 0 };

    return RxAllocationInfoForDescs(*this, numResourceDescs, pResourceDescs, pResourceAllocationInfo1);
}

D3D12_HEAP_PROPERTIES
STDMETHODCALLTYPE
RxDevice12::GetCustomHeapProperties(
    _In_ UINT nodeMask,
    D3D12_HEAP_TYPE heapType)
{
    return RxCustomProperties(*this, heapType);
}

static
VOID
RxFootprintsOrFill(
    _In_ RxDevice12& Device,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ UINT FirstSubresource,
    _In_ UINT NumSubresources,
    _In_ UINT64 BaseOffset,
    _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    _Out_writes_opt_(NumSubresources) UINT* pNumRows,
    _Out_writes_opt_(NumSubresources) UINT64* pRowSizeInBytes,
    _Out_opt_ UINT64* pTotalBytes)
{
    if (RxCopyableFootprints(Device, Desc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes,
                             pTotalBytes))
    {
        return;
    }

    /* A desc that can not exist gets every output filled with ones */
    if (pLayouts != nullptr)
        FillMemory(pLayouts, NumSubresources * sizeof(*pLayouts), 0xFF);
    if (pNumRows != nullptr)
        FillMemory(pNumRows, NumSubresources * sizeof(*pNumRows), 0xFF);
    if (pRowSizeInBytes != nullptr)
        FillMemory(pRowSizeInBytes, NumSubresources * sizeof(*pRowSizeInBytes), 0xFF);
    if (pTotalBytes != nullptr)
        *pTotalBytes = UINT64_MAX;
}

VOID
STDMETHODCALLTYPE
RxDevice12::GetCopyableFootprints(
    _In_ const D3D12_RESOURCE_DESC* pResourceDesc,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
    UINT64 BaseOffset,
    _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    _Out_writes_opt_(NumSubresources) UINT* pNumRows,
    _Out_writes_opt_(NumSubresources) UINT64* pRowSizeInBytes,
    _Out_opt_ UINT64* pTotalBytes)
{
    D3D12_RESOURCE_DESC1 Wide;

    if (pResourceDesc == nullptr)
        return;

    Wide = RxWidenDesc(*pResourceDesc);
    RxFootprintsOrFill(*this, Wide, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows, pRowSizeInBytes,
                       pTotalBytes);
}

VOID
STDMETHODCALLTYPE
RxDevice12::GetCopyableFootprints1(
    _In_ const D3D12_RESOURCE_DESC1* pResourceDesc,
    _In_range_(0, D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0, D3D12_REQ_SUBRESOURCES - FirstSubresource) UINT NumSubresources,
    UINT64 BaseOffset,
    _Out_writes_opt_(NumSubresources) D3D12_PLACED_SUBRESOURCE_FOOTPRINT* pLayouts,
    _Out_writes_opt_(NumSubresources) UINT* pNumRows,
    _Out_writes_opt_(NumSubresources) UINT64* pRowSizeInBytes,
    _Out_opt_ UINT64* pTotalBytes)
{
    if (pResourceDesc == nullptr)
        return;

    RxFootprintsOrFill(*this, *pResourceDesc, FirstSubresource, NumSubresources, BaseOffset, pLayouts, pNumRows,
                       pRowSizeInBytes, pTotalBytes);
}
