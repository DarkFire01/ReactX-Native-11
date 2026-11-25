/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Reserved resources, their tiling and tile mappings
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

static_assert(sizeof(D3D12DDI_TILED_RESOURCE_COORDINATE) == sizeof(D3D12_TILED_RESOURCE_COORDINATE));
static_assert(sizeof(D3D12DDI_TILE_REGION_SIZE) == sizeof(D3D12_TILE_REGION_SIZE));

/* Tiles a buffer is cut into */
constexpr UINT RxTileBytes = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

/* The tile index a packed mip reports for the first array slice */
constexpr UINT RxPackedTile = D3D12_PACKED_TILE;

/* How a reserved resource divides into tiles */
struct RxTiling12
{
    D3D12_TILE_SHAPE Shape;
    UINT StandardMips;
    UINT PackedMips;
    UINT PackedTiles;
    UINT TilesPerSlice;
    UINT Slices;
    UINT Subresources;
};

/* The 64KB tile of a texture in texels: a square or 2:1 block of elements, narrower for more samples */
static
D3D12_TILE_SHAPE
RxStandardTileShape(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    static const UINT Flat[][2] = { { 256, 256 }, { 256, 128 }, { 128, 128 }, { 128, 64 }, { 64, 64 } };
    static const UINT Deep[][3] = { { 64, 32, 32 }, { 32, 32, 32 }, { 32, 32, 16 }, { 32, 16, 16 }, { 16, 16, 16 } };
    D3D12_TILE_SHAPE Shape = { RxTileBytes, 1, 1 };
    UINT BlockWidth = 1;
    UINT BlockHeight = 1;
    UINT BlockBytes = 1;
    UINT Size = 0;
    UINT Halving = 0;

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        return Shape;

    RxFormatBlock(Desc.Format, &BlockWidth, &BlockHeight, &BlockBytes);
    while (Size < 4 && (1u << Size) < BlockBytes)
        Size++;

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        Shape.WidthInTexels = Deep[Size][0] * BlockWidth;
        Shape.HeightInTexels = Deep[Size][1] * BlockHeight;
        Shape.DepthInTexels = Deep[Size][2];
        return Shape;
    }

    Shape.WidthInTexels = Flat[Size][0];
    Shape.HeightInTexels = Flat[Size][1];

    /* Each doubling of the samples halves the width, then the height, in turn */
    for (UINT Samples = 2; Samples <= Desc.SampleDesc.Count && Samples <= 16; Samples *= 2)
    {
        if ((Halving++ & 1) == 0)
            Shape.WidthInTexels /= 2;
        else
            Shape.HeightInTexels /= 2;
    }

    Shape.WidthInTexels *= BlockWidth;
    Shape.HeightInTexels *= BlockHeight;
    return Shape;
}

static
UINT
RxTilesAcross(
    _In_ UINT64 Extent,
    _In_ UINT Mip,
    _In_ UINT Tile)
{
    UINT64 Size = Extent >> Mip;

    if (Size == 0)
        Size = 1;
    return (UINT)((Size + Tile - 1) / Tile);
}

/* The tiles of one standard mip */
static
VOID
RxMipTiles(
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ const D3D12_TILE_SHAPE& Shape,
    _In_ UINT Mip,
    _Out_ D3D12_SUBRESOURCE_TILING* Tiling)
{
    BOOLEAN Volume = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;

    Tiling->WidthInTiles = RxTilesAcross(Desc.Width, Mip, Shape.WidthInTexels);
    Tiling->HeightInTiles = (UINT16)RxTilesAcross(Desc.Height, Mip, Shape.HeightInTexels);
    Tiling->DepthInTiles = (UINT16)(Volume ? RxTilesAcross(Desc.DepthOrArraySize, Mip, Shape.DepthInTexels) : 1);
    Tiling->StartTileIndexInOverallResource = 0;
}

/* How the driver packs the smallest mips decides where the standard ones stop */
static
VOID
RxReadTiling(
    _In_ RxDevice12& Device,
    _In_ RxResource& Resource,
    _Out_ RxTiling12* Tiling)
{
    const D3D12_RESOURCE_DESC1& Desc = Resource.GetApiDesc();
    D3D12_SUBRESOURCE_TILING Mip;
    UINT Index;

    ZeroMemory(Tiling, sizeof(*Tiling));
    Tiling->Shape = RxStandardTileShape(Desc);

    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        Tiling->TilesPerSlice = (UINT)((Desc.Width + RxTileBytes - 1) / RxTileBytes);
        Tiling->Slices = 1;
        Tiling->Subresources = 1;
        return;
    }

    Device.Ddi().pfnGetMipPacking(Device.Handle(), Resource.GetDriverHandle(), &Tiling->PackedMips, &Tiling->PackedTiles);
    if (Tiling->PackedMips > Desc.MipLevels)
        Tiling->PackedMips = Desc.MipLevels;
    Tiling->StandardMips = Desc.MipLevels - Tiling->PackedMips;
    Tiling->Slices = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;
    Tiling->Subresources = Desc.MipLevels * Tiling->Slices;

    for (Index = 0; Index < Tiling->StandardMips; Index++)
    {
        RxMipTiles(Desc, Tiling->Shape, Index, &Mip);
        Tiling->TilesPerSlice += Mip.WidthInTiles * Mip.HeightInTiles * Mip.DepthInTiles;
    }
    Tiling->TilesPerSlice += Tiling->PackedTiles;
}

/* Reserved resources */

static
HRESULT
RxCreateReservedCommon(
    _In_ RxDevice12& Device,
    _In_opt_ const D3D12_RESOURCE_DESC1* pDesc,
    _In_ D3D12_RESOURCE_STATES InitialState,
    _In_opt_ const RxResourceStart12* Layout,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvResource)
{
    RxResourceStart12 Start = { RxLayoutForState(InitialState), 0, nullptr };
    D3D12_RESOURCE_STATES Checked = InitialState;
    D3D12_RESOURCE_DESC1 Normal;
    RxResource* Resource;
    HRESULT hr;

    if (ppvResource != nullptr)
        *ppvResource = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxValidateResourceDesc(Device, *pDesc, pOptimizedClearValue);
    if (FAILED(hr))
        return hr;

    if (Layout != nullptr)
    {
        hr = RxValidateInitialLayout(*pDesc, *Layout);
        Start = *Layout;
    }
    else
    {
        /* Buffers other than acceleration structures and simultaneous access textures are checked as common */
        if ((pDesc->Dimension == D3D12_RESOURCE_DIMENSION_BUFFER &&
             InitialState != D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE) ||
            (pDesc->Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS) != 0)
        {
            Checked = D3D12_RESOURCE_STATE_COMMON;
        }
        hr = RxValidateInitialState(Device, Checked, *pDesc, D3D12_HEAP_TYPE_DEFAULT);
    }
    if (FAILED(hr))
        return hr;

    if (Device.GetValidationCaps().TiledTier == 0)
        return E_INVALIDARG;
    if (pDesc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
        return E_INVALIDARG;
    if (pDesc->Dimension != D3D12_RESOURCE_DIMENSION_BUFFER && pDesc->Layout != D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE)
        return E_INVALIDARG;

    /* Without an out pointer the system fails once the checks pass */
    if (ppvResource == nullptr)
        return E_UNEXPECTED;
    if (pProtectedSession != nullptr)
        return E_NOTIMPL;

    Normal = *pDesc;
    RxFillResourceDefaults(Normal);

    hr = RxResource::CreateReserved(Device, Normal, Start, pOptimizedClearValue, &Resource);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Resource, riid, ppvResource);
}

static
D3D12_RESOURCE_DESC1
RxReservedDesc(
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

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateReservedResource(
    _In_ const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvResource)
{
    D3D12_RESOURCE_DESC1 Wide;

    if (pDesc != nullptr)
        Wide = RxReservedDesc(*pDesc);

    return RxCreateReservedCommon(*this, pDesc != nullptr ? &Wide : nullptr, InitialState, nullptr, pOptimizedClearValue,
                                  nullptr, riid, ppvResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateReservedResource1(
    _In_ const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialState,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvResource)
{
    D3D12_RESOURCE_DESC1 Wide;

    if (pDesc != nullptr)
        Wide = RxReservedDesc(*pDesc);

    return RxCreateReservedCommon(*this, pDesc != nullptr ? &Wide : nullptr, InitialState, nullptr, pOptimizedClearValue,
                                  pProtectedSession, riid, ppvResource);
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateReservedResource2(
    _In_ const D3D12_RESOURCE_DESC* pDesc,
    D3D12_BARRIER_LAYOUT InitialLayout,
    _In_opt_ const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedSession,
    UINT32 NumCastableFormats,
    _In_opt_count_(NumCastableFormats) const DXGI_FORMAT* pCastableFormats,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvResource)
{
    RxResourceStart12 Start = { (D3D12DDI_BARRIER_LAYOUT)InitialLayout, NumCastableFormats, pCastableFormats };
    D3D12_RESOURCE_DESC1 Wide;

    if (pDesc != nullptr)
        Wide = RxReservedDesc(*pDesc);

    return RxCreateReservedCommon(*this, pDesc != nullptr ? &Wide : nullptr, D3D12_RESOURCE_STATE_COMMON, &Start,
                                  pOptimizedClearValue, pProtectedSession, riid, ppvResource);
}

/*
 * Tiling of a reserved resource. Each array slice holds its standard mips and then its packed ones;
 * the slice offset is added to every entry, packed ones included, as the system runtime reports them.
 */
VOID
STDMETHODCALLTYPE
RxDevice12::GetResourceTiling(
    _In_ ID3D12Resource* pTiledResource,
    _Out_opt_ UINT* pNumTilesForEntireResource,
    _Out_opt_ D3D12_PACKED_MIP_INFO* pPackedMipDesc,
    _Out_opt_ D3D12_TILE_SHAPE* pStandardTileShapeForNonPackedMips,
    _Inout_opt_ UINT* pNumSubresourceTilings,
    _In_ UINT FirstSubresourceTilingToGet,
    _Out_writes_(*pNumSubresourceTilings) D3D12_SUBRESOURCE_TILING* pSubresourceTilingsForNonPackedMips)
{
    RxResource* Resource = RxUnwrap12<RxResource>(pTiledResource);
    D3D12_SUBRESOURCE_TILING Mip;
    RxTiling12 Tiling;
    UINT StandardTiles = 0;
    UINT Count;
    UINT Index;

    if (Resource == nullptr || !Resource->IsReserved())
    {
        if (pNumTilesForEntireResource != nullptr)
            *pNumTilesForEntireResource = 0;
        if (pPackedMipDesc != nullptr)
            ZeroMemory(pPackedMipDesc, sizeof(*pPackedMipDesc));
        if (pStandardTileShapeForNonPackedMips != nullptr)
            ZeroMemory(pStandardTileShapeForNonPackedMips, sizeof(*pStandardTileShapeForNonPackedMips));
        if (pNumSubresourceTilings != nullptr)
            *pNumSubresourceTilings = 0;
        return;
    }

    const D3D12_RESOURCE_DESC1& Desc = Resource->GetApiDesc();

    RxReadTiling(*this, *Resource, &Tiling);
    for (Index = 0; Index < Tiling.StandardMips; Index++)
    {
        RxMipTiles(Desc, Tiling.Shape, Index, &Mip);
        StandardTiles += Mip.WidthInTiles * Mip.HeightInTiles * Mip.DepthInTiles;
    }

    if (pPackedMipDesc != nullptr)
    {
        pPackedMipDesc->NumStandardMips = (UINT8)Tiling.StandardMips;
        pPackedMipDesc->NumPackedMips = (UINT8)Tiling.PackedMips;
        pPackedMipDesc->NumTilesForPackedMips = Tiling.PackedTiles;
        pPackedMipDesc->StartTileIndexInOverallResource = Tiling.PackedMips != 0 ? StandardTiles : 0;
    }
    if (pNumTilesForEntireResource != nullptr)
        *pNumTilesForEntireResource = Tiling.TilesPerSlice * Tiling.Slices;
    if (pStandardTileShapeForNonPackedMips != nullptr)
        *pStandardTileShapeForNonPackedMips = Tiling.Shape;
    if (pNumSubresourceTilings == nullptr)
        return;

    Count = Tiling.Subresources - FirstSubresourceTilingToGet;
    if (Count > *pNumSubresourceTilings)
        Count = *pNumSubresourceTilings;

    for (Index = 0; Index < Count && pSubresourceTilingsForNonPackedMips != nullptr; Index++)
    {
        UINT Subresource = FirstSubresourceTilingToGet + Index;
        UINT MipLevel = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? 0 : Subresource % Desc.MipLevels;
        UINT Slice = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ? Subresource : Subresource / Desc.MipLevels;
        D3D12_SUBRESOURCE_TILING& Out = pSubresourceTilingsForNonPackedMips[Index];
        UINT Start = 0;
        UINT Earlier;

        if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            Out.WidthInTiles = Tiling.TilesPerSlice;
            Out.HeightInTiles = 1;
            Out.DepthInTiles = 1;
            Out.StartTileIndexInOverallResource = Slice * Tiling.TilesPerSlice;
            continue;
        }

        if (MipLevel >= Tiling.StandardMips)
        {
            Out.WidthInTiles = 0;
            Out.HeightInTiles = 0;
            Out.DepthInTiles = 0;
            Out.StartTileIndexInOverallResource = RxPackedTile + Slice * Tiling.TilesPerSlice;
            continue;
        }

        for (Earlier = 0; Earlier < MipLevel; Earlier++)
        {
            RxMipTiles(Desc, Tiling.Shape, Earlier, &Mip);
            Start += Mip.WidthInTiles * Mip.HeightInTiles * Mip.DepthInTiles;
        }
        RxMipTiles(Desc, Tiling.Shape, MipLevel, &Out);
        Out.StartTileIndexInOverallResource = Start + Slice * Tiling.TilesPerSlice;
    }

    *pNumSubresourceTilings = Count;
}

/* Tile mappings */

/* The heap flags that keep a resource's kind of data out of a heap */
static
UINT
RxHeapFlagsDenying(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    switch (Desc.Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_BUFFER:
            return D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if ((Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0)
                return D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
            return D3D12_HEAP_FLAG_DENY_NON_RT_DS_TEXTURES | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        default:
            return D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
    }
}

VOID
STDMETHODCALLTYPE
RxCommandQueue::UpdateTileMappings(
    _In_ ID3D12Resource* pResource,
    UINT NumResourceRegions,
    _In_reads_opt_(NumResourceRegions) const D3D12_TILED_RESOURCE_COORDINATE* pResourceRegionStartCoordinates,
    _In_reads_opt_(NumResourceRegions) const D3D12_TILE_REGION_SIZE* pResourceRegionSizes,
    _In_opt_ ID3D12Heap* pHeap,
    UINT NumRanges,
    _In_reads_opt_(NumRanges) const D3D12_TILE_RANGE_FLAGS* pRangeFlags,
    _In_reads_opt_(NumRanges) const UINT* pHeapRangeStartOffsets,
    _In_reads_opt_(NumRanges) const UINT* pRangeTileCounts,
    D3D12_TILE_MAPPING_FLAGS Flags)
{
    static const D3D12_TILED_RESOURCE_COORDINATE Origin = {};
    RxResource* Resource = RxUnwrap12<RxResource>(pResource);
    RxHeap* Heap = RxUnwrap12<RxHeap>(pHeap);
    const D3D12_TILED_RESOURCE_COORDINATE* Coordinates = pResourceRegionStartCoordinates;
    const D3D12_TILE_REGION_SIZE* Sizes = pResourceRegionSizes;
    const UINT* Counts = pRangeTileCounts;
    D3D12_TILE_REGION_SIZE Single = {};
    D3D12DDI_HHEAP DriverHeap = {};
    UINT Sum = 0;
    UINT Index;

    if (Resource == nullptr)
        return;

    if (Heap != nullptr)
    {
        if ((Heap->GetApiDesc().Flags & RxHeapFlagsDenying(Resource->GetApiDesc())) != 0)
        {
            RX_TRACE(L"tiles mapped into a heap that denies the resource's kind");
            Device.MarkRemoved(DXGI_ERROR_INVALID_CALL);
            return;
        }
        DriverHeap = Heap->GetDriverHeap();
    }

    /* Without coordinates one region covers the whole resource; otherwise a missing size is one tile */
    Single.NumTiles = 1;
    if (Coordinates == nullptr)
    {
        RxTiling12 Tiling;

        RxReadTiling(Device, *Resource, &Tiling);
        Single.NumTiles = Tiling.TilesPerSlice * Tiling.Slices;
        Coordinates = &Origin;
        if (Sizes == nullptr)
            Sizes = &Single;
    }

    /* One range without counts takes as many tiles as the regions hold together */
    if (NumRanges == 1 && Counts == nullptr)
    {
        for (Index = 0; Index < NumResourceRegions; Index++)
            Sum += pResourceRegionSizes != nullptr ? pResourceRegionSizes[Index].NumTiles : Single.NumTiles;
        Counts = &Sum;
    }

    AcquireSRWLockExclusive(&Lock);
    Device.QueueDdi().pfnUpdateTileMappings(DriverQueue, Resource->GetDriverHandle(), NumResourceRegions,
                                            reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE*>(Coordinates),
                                            reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE*>(Sizes), DriverHeap,
                                            NumRanges, reinterpret_cast<const D3D12DDI_TILE_RANGE_FLAGS*>(pRangeFlags),
                                            pHeapRangeStartOffsets, Counts, (D3D12DDI_TILE_MAPPING_FLAGS)Flags);
    ReleaseSRWLockExclusive(&Lock);
}

VOID
STDMETHODCALLTYPE
RxCommandQueue::CopyTileMappings(
    _In_ ID3D12Resource* pDstResource,
    _In_ const D3D12_TILED_RESOURCE_COORDINATE* pDstRegionStartCoordinate,
    _In_ ID3D12Resource* pSrcResource,
    _In_ const D3D12_TILED_RESOURCE_COORDINATE* pSrcRegionStartCoordinate,
    _In_ const D3D12_TILE_REGION_SIZE* pRegionSize,
    D3D12_TILE_MAPPING_FLAGS Flags)
{
    RxResource* Destination = RxUnwrap12<RxResource>(pDstResource);
    RxResource* Source = RxUnwrap12<RxResource>(pSrcResource);

    if (Destination == nullptr || Source == nullptr)
        return;

    AcquireSRWLockExclusive(&Lock);
    Device.QueueDdi().pfnCopyTileMappings(DriverQueue, Destination->GetDriverHandle(),
                                          reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE*>(pDstRegionStartCoordinate),
                                          Source->GetDriverHandle(),
                                          reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE*>(pSrcRegionStartCoordinate),
                                          reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE*>(pRegionSize),
                                          (D3D12DDI_TILE_MAPPING_FLAGS)Flags);
    ReleaseSRWLockExclusive(&Lock);
}

VOID
STDMETHODCALLTYPE
RxCommandList::CopyTiles(
    _In_ ID3D12Resource* pTiledResource,
    _In_ const D3D12_TILED_RESOURCE_COORDINATE* pTileRegionStartCoordinate,
    _In_ const D3D12_TILE_REGION_SIZE* pTileRegionSize,
    _In_ ID3D12Resource* pBuffer,
    UINT64 BufferStartOffsetInBytes,
    D3D12_TILE_COPY_FLAGS Flags)
{
    RxResource* Tiled = RxUnwrap12<RxResource>(pTiledResource);
    RxResource* Buffer = RxUnwrap12<RxResource>(pBuffer);
    D3D12DDI_HRESOURCE None = {};

    if (!Records())
        return;

    DriverTable->pfnCopyTiles(DriverList, Tiled != nullptr ? Tiled->GetDriverHandle() : None,
                              reinterpret_cast<const D3D12DDI_TILED_RESOURCE_COORDINATE*>(pTileRegionStartCoordinate),
                              reinterpret_cast<const D3D12DDI_TILE_REGION_SIZE*>(pTileRegionSize),
                              Buffer != nullptr ? Buffer->GetDriverHandle() : None, BufferStartOffsetInBytes,
                              (D3D12DDI_TILE_COPY_FLAGS)Flags);
}
