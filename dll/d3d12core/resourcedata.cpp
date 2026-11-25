/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     CPU writes and reads of mapped texture subresources
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"
#include "formatlayout12.hpp"
#include "swizzle12.hpp"

constexpr UINT RxNoAddressing = 0xFF;

/* Layout patterns: six mip groups for each of three plane groups */
constexpr UINT RxLayoutMipGroups = 6;
constexpr UINT RxLayoutPatterns = 18;

static
BOOLEAN
RxIsBlockCompressed12(
    _In_ DXGI_FORMAT Format)
{
    return (Format >= DXGI_FORMAT_BC1_TYPELESS && Format <= DXGI_FORMAT_BC5_SNORM) ||
           (Format >= DXGI_FORMAT_BC6H_TYPELESS && Format <= DXGI_FORMAT_BC7_UNORM_SRGB);
}

/* log2 of the bytes one addressed unit of the format takes, when that is 1 to 16 bytes */
static
UINT
RxAddressingLog2(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);
    UINT Bytes;

    if (Layout == nullptr)
        return RxNoAddressing;

    if (RxIsBlockCompressed12(Format))
        Bytes = Layout->BitsPerUnit / 8;
    else
        Bytes = Layout->BitsPerUnit * Layout->WidthAlign * Layout->HeightAlign / 8;

    switch (Bytes)
    {
        case 1:
            return 0;
        case 2:
            return 1;
        case 4:
            return 2;
        case 8:
            return 3;
        case 16:
            return 4;
        default:
            return RxNoAddressing;
    }
}

static
UINT
RxAlignTo(
    _In_ UINT Value,
    _In_ UINT Alignment)
{
    return (Value + Alignment - 1) / Alignment * Alignment;
}

static
UINT
RxMipExtent(
    _In_ UINT Extent,
    _In_ UINT Mip)
{
    return (Extent >> Mip) != 0 ? Extent >> Mip : 1;
}

/* The swizzle pattern the driver uses for one subresource; FALSE when it names none the runtime can read */
static
BOOLEAN
RxSubresourcePattern(
    _In_ RxDevice12& Device,
    _In_ const D3D12DDI_RESOURCE_ALLOCATION_INFO_0022& Allocation,
    _In_ const D3D12_RESOURCE_DESC1& Desc,
    _In_ UINT Subresource,
    _In_ const D3D12DDI_SUBRESOURCE_INFO& Info,
    _In_ UINT ElementLog2,
    _Out_ D3D12DDI_SWIZZLE_PATTERN_DESC_0022* Pattern)
{
    const D3D12DDI_TEXTURE_LAYOUT_CAPS_0026& Caps = Device.GetTextureLayoutCaps();
    D3D12DDI_SWIZZLE_PATTERN Patterns[RxLayoutPatterns] = {};
    D3D12DDI_SWIZZLE_PATTERN Chosen;
    UINT Array = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;
    UINT Index;

    if (Allocation.Layout == D3D12DDI_TL_ROW_MAJOR)
    {
        Chosen = D3D12DDI_SP_ROW_MAJOR;
    }
    else if (Allocation.Layout == D3D12DDI_TL_64KB_TILE_STANDARD_SWIZZLE)
    {
        Chosen = D3D12DDI_SP_64KB_STANDARD_SWIZZLE;
    }
    else
    {
        Index = (UINT)Allocation.Layout - D3D12DDI_TL_DEVICE_DEPENDENT_SWIZZLE_0;
        if (Index >= Caps.DeviceDependentLayoutCount ||
            FAILED(Device.GetAdapter().GetCaps(D3D12DDICAPS_TYPE_0022_TEXTURE_LAYOUT, &Index, Patterns, sizeof(Patterns))))
        {
            return FALSE;
        }

        if (Caps.IndexableSwizzlePatterns)
        {
            /* Such drivers name the pattern of each subresource themselves */
            if (Info.ColumnPreSwizzleOffset >= RxLayoutPatterns)
            {
                RX_TRACE(L"driver named swizzle pattern %u of a layout", Info.ColumnPreSwizzleOffset);
                return FALSE;
            }
            Chosen = Patterns[Info.ColumnPreSwizzleOffset];
        }
        else
        {
            UINT Mip = Subresource % Desc.MipLevels;
            UINT Plane = Subresource / (Array * Desc.MipLevels);
            UINT MipGroup = 0;
            UINT PlaneGroup = 0;

            while (MipGroup < _countof(Allocation.MipLevelSwizzleTransition) &&
                   Mip >= Allocation.MipLevelSwizzleTransition[MipGroup])
            {
                MipGroup++;
            }
            while (PlaneGroup < _countof(Allocation.PlaneSliceSwizzleTransition) &&
                   Plane >= Allocation.PlaneSliceSwizzleTransition[PlaneGroup])
            {
                PlaneGroup++;
            }
            Chosen = Patterns[PlaneGroup * RxLayoutMipGroups + MipGroup];
        }
    }

    /* One dimensional textures have no standard swizzle and are laid out linearly */
    if (Chosen == D3D12DDI_SP_ROW_MAJOR || Chosen == D3D12DDI_SP_64KB_STANDARD_SWIZZLE)
    {
        if (Chosen == D3D12DDI_SP_ROW_MAJOR || Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D)
            RxLinearSwizzlePattern12(ElementLog2, Pattern);
        else
            RxStandardSwizzlePattern12(Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D, ElementLog2, Pattern);
        return TRUE;
    }

    Index = (UINT)Chosen - D3D12DDI_SP_DEVICE_DEPENDENT_0;
    return Index < Caps.DeviceDependentSwizzleCount &&
           SUCCEEDED(Device.GetAdapter().GetCaps(D3D12DDICAPS_TYPE_0022_SWIZZLE_PATTERN, &Index, Pattern, sizeof(*Pattern)));
}

/* Whether the CPU can address the texels of a resource of this kind at all */
static
BOOLEAN
RxCpuAddressable(
    _In_ const D3D12_RESOURCE_DESC1& Desc)
{
    constexpr UINT Opaque = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY |
                            D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY;

    if (Desc.SampleDesc.Count > 1 || (Desc.Flags & Opaque) != 0 || RxAddressingLog2(Desc.Format) == RxNoAddressing)
        return FALSE;

    /* Volumes with mips only have a known layout when the app chose a standard one */
    if ((Desc.Layout == D3D12_TEXTURE_LAYOUT_UNKNOWN || Desc.Layout == D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE) &&
        Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D && Desc.MipLevels != 1)
    {
        return FALSE;
    }

    return Desc.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE &&
           Desc.Format != DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
}

HRESULT
RxResource::CopySubresourceData(
    _In_ UINT Subresource,
    _In_opt_ const D3D12_BOX* Box,
    _Inout_ BYTE* Linear,
    _In_ UINT LinearPitch,
    _In_ UINT LinearDepthPitch,
    _In_ BOOLEAN Write)
{
    D3D12DDI_RESOURCE_ALLOCATION_INFO_0022 Allocation;
    D3D12DDI_SWIZZLE_PATTERN_DESC_0022 Pattern;
    D3D12DDI_SUBRESOURCE_INFO Info = {};
    D3D12_HEAP_PROPERTIES Custom;
    RxSwizzleRegion12 Region;
    RxSwizzle12 Swizzle;
    const RxFormatLayout12* Block;
    DXGI_FORMAT PlaneFormat;
    UINT64 PlaneWidth;
    UINT64 PitchWidth;
    UINT PlaneHeight;
    UINT Array = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;
    UINT Planes = RxFormatPlaneCount(Desc.Format);
    UINT Mip;
    UINT Plane;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT Origin[3];
    UINT Log2;
    D3D12_BOX Extent;
    BYTE* Base;
    HRESULT hr;

    if (IsReservedResource)
    {
        RX_TRACE(L"reserved resources have no CPU view of their tiles");
        return E_INVALIDARG;
    }

    Custom = Properties.Type == D3D12_HEAP_TYPE_CUSTOM ? Properties : RxCustomProperties(Device, Properties.Type);
    if (Custom.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
        return E_INVALIDARG;
    if (Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER || !RxCpuAddressable(Desc))
        return E_INVALIDARG;
    if (Subresource >= Desc.MipLevels * Array * Planes)
        return E_INVALIDARG;

    /* The subresource in whole blocks of the format, then split into its plane */
    Block = RxFormatLayout(Desc.Format);
    Mip = Subresource % Desc.MipLevels;
    Plane = Subresource / (Array * Desc.MipLevels);
    Width = RxAlignTo(RxMipExtent((UINT)Desc.Width, Mip), Block->WidthAlign);
    Height = RxAlignTo(RxMipExtent(Desc.Height, Mip), Block->HeightAlign);
    Depth = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? RxMipExtent(Desc.DepthOrArraySize, Mip) : 1;
    RxFormatPlane(Desc.Format, Plane, Width, Height, &PlaneFormat, &PlaneWidth, &PlaneHeight, &PitchWidth);
    Width = (UINT)PlaneWidth;
    Height = PlaneHeight;
    Block = RxFormatLayout(PlaneFormat);

    if (Box != nullptr)
    {
        if (((Box->left | Box->right) % Block->WidthAlign) != 0 || ((Box->top | Box->bottom) % Block->HeightAlign) != 0)
            return E_INVALIDARG;
        if (Box->right > Width || Box->bottom > Height || Box->back > Depth)
            return E_INVALIDARG;
        Extent = *Box;
    }
    else
    {
        Extent = { 0, 0, 0, Width, Height, Depth };
    }

    /* An empty box copies nothing */
    if (Extent.left >= Extent.right || Extent.top >= Extent.bottom || Extent.front >= Extent.back)
        return S_OK;

    hr = MapMemory(&Base);
    if (FAILED(hr))
        return hr;

    Device.Ddi().pfnCheckSubresourceInfo(Device.Handle(), DriverResource, Subresource, &Info);
    Origin[0] = Info.RowBytePreSwizzleOffset;
    Origin[1] = Info.ColumnPreSwizzleOffset;
    Origin[2] = Info.DepthPreSwizzleOffset;

    /* Opened resources have no creation record and ask the driver instead */
    Allocation = CreationLayout;
    if (!HasCreationLayout)
        Device.Ddi().pfnCheckExistingResourceAllocationInfo(Device.Handle(), DriverResource, &Allocation);

    /* A pattern the driver does not describe, or the runtime cannot follow, leaves the texels as they are */
    Log2 = RxAddressingLog2(Desc.Format);
    if (RxSubresourcePattern(Device, Allocation, Desc, Subresource, Info, Log2, &Pattern) &&
        SUCCEEDED(RxPrepareSwizzle12(Pattern, Log2, Origin, &Swizzle)))
    {
        Log2 = RxAddressingLog2(PlaneFormat);
        Region.Base = Base + Info.Offset;
        Region.RowPitch = (UINT)Info.RowStride;
        Region.DepthPitch = (UINT)Info.DepthStride;
        Region.Start[0] = Extent.left / Block->WidthAlign << Log2;
        Region.Start[1] = Extent.top / Block->HeightAlign;
        Region.Start[2] = Extent.front;
        Region.Size[0] = (Extent.right / Block->WidthAlign - Extent.left / Block->WidthAlign) << Log2;
        Region.Size[1] = Extent.bottom / Block->HeightAlign - Extent.top / Block->HeightAlign;
        Region.Size[2] = Extent.back - Extent.front;
        RxSwizzleCopy12(Swizzle, Region, Linear, LinearPitch, LinearDepthPitch, Write);
    }
    else
    {
        RX_TRACE(L"no usable swizzle pattern for subresource %u", Subresource);
    }

    UnmapMemory();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxResource::WriteToSubresource(
    UINT DstSubresource,
    _In_opt_ const D3D12_BOX* pDstBox,
    _In_ const void* pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch)
{
    return CopySubresourceData(DstSubresource, pDstBox, static_cast<BYTE*>(const_cast<void*>(pSrcData)), SrcRowPitch,
                               SrcDepthPitch, TRUE);
}

HRESULT
STDMETHODCALLTYPE
RxResource::ReadFromSubresource(
    _Out_ void* pDstData,
    UINT DstRowPitch,
    UINT DstDepthPitch,
    UINT SrcSubresource,
    _In_opt_ const D3D12_BOX* pSrcBox)
{
    return CopySubresourceData(SrcSubresource, pSrcBox, static_cast<BYTE*>(pDstData), DstRowPitch, DstDepthPitch, FALSE);
}
