/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Copies and stretches between surfaces, and read back into system memory
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <math.h>

/* One copy between two resources as the blit entry describes it */
struct NineCopy
{
    NineResource* Source;
    NineResource* Dest;
    UINT SourceSub;
    UINT DestSub;
    UINT DestX;
    UINT DestY;
    UINT DestZ;
    D3D12_BOX SourceBox;
    BOOLEAN Whole;
    BOOLEAN Stretch;
    RECT SourceRect;
    RECT DestRect;
};

static
NineResource*
NineBltResource(
    _In_opt_ HANDLE hResource)
{
    if (hResource == reinterpret_cast<HANDLE>(1))
        return nullptr;
    return static_cast<NineResource*>(hResource);
}

static
BOOLEAN
NineIsYuv(
    _In_ DXGI_FORMAT Format)
{
    return CD3D11FormatHelper::YUV(Format) != FALSE;
}

/* Only an all zero rect stands for the whole subresource; a degenerate one is kept as it is */
static
BOOLEAN
NineRectEmpty(
    _In_ const RECT* Rect)
{
    return Rect->left == 0 && Rect->top == 0 && Rect->right == 0 && Rect->bottom == 0;
}

/* Copies keep to whole blocks of block compressed formats and to pixel pairs of packed video formats */
static
VOID
NineAlignCopy(
    _Inout_ NineCopy* Copy)
{
    const DXGI_FORMAT Format = Copy->Source->Typed.Format;
    const BOOLEAN Blocks = (Format >= DXGI_FORMAT_BC1_TYPELESS && Format <= DXGI_FORMAT_BC5_SNORM) ||
                           (Format >= DXGI_FORMAT_BC6H_TYPELESS && Format <= DXGI_FORMAT_BC7_UNORM_SRGB);

    if (Blocks)
    {
        Copy->DestX &= ~3U;
        Copy->DestY &= ~3U;
        Copy->SourceBox.left &= ~3U;
        Copy->SourceBox.top &= ~3U;
        Copy->SourceBox.right = (Copy->SourceBox.right + 3) & ~3U;
        Copy->SourceBox.bottom = (Copy->SourceBox.bottom + 3) & ~3U;
    }
    else if (NineIsYuv(Format))
    {
        Copy->DestX &= ~1U;
        Copy->SourceBox.left &= ~1U;
        Copy->SourceBox.right = (Copy->SourceBox.right + 1) & ~1U;
    }
}

/* Shrinks a rect to a bound, moving the matching rect of the other surface by the same share */
static
VOID
NineClipToBound(
    _Inout_ RECT* Rect,
    _Inout_ RECT* Other,
    _Inout_ RECT* Bound)
{
    const FLOAT ScaleX = static_cast<FLOAT>(static_cast<UINT>(Other->right - Other->left)) /
                         static_cast<FLOAT>(static_cast<UINT>(Rect->right - Rect->left));
    const FLOAT ScaleY = static_cast<FLOAT>(static_cast<UINT>(Other->bottom - Other->top)) /
                         static_cast<FLOAT>(static_cast<UINT>(Rect->bottom - Rect->top));

    if (Bound->left < 0)
        Bound->left = 0;
    if (Bound->top < 0)
        Bound->top = 0;

    if (Rect->left < Bound->left)
    {
        Other->left += static_cast<LONG>(ceilf(static_cast<FLOAT>(Bound->left - Rect->left) * ScaleX));
        Rect->left = Bound->left;
    }
    if (Rect->right > Bound->right)
    {
        Other->right -= static_cast<LONG>(ceilf(static_cast<FLOAT>(Rect->right - Bound->right) * ScaleX));
        Rect->right = Bound->right;
    }
    if (Rect->top < Bound->top)
    {
        Other->top += static_cast<LONG>(ceilf(static_cast<FLOAT>(Bound->top - Rect->top) * ScaleY));
        Rect->top = Bound->top;
    }
    if (Rect->bottom > Bound->bottom)
    {
        Other->bottom -= static_cast<LONG>(ceilf(static_cast<FLOAT>(Rect->bottom - Bound->bottom) * ScaleY));
        Rect->bottom = Bound->bottom;
    }
}

/* Fills empty rects with the whole subresource and keeps both rects inside their surfaces */
static
VOID
NineFitRects(
    _In_ const NineCopy* Copy,
    _Inout_ RECT* Source,
    _Inout_ RECT* Dest)
{
    RECT Bound;

    if (NineRectEmpty(Source))
    {
        const D3D12_SUBRESOURCE_FOOTPRINT& Whole = Copy->Source->Footprints[Copy->SourceSub].Footprint;

        *Source = { 0, 0, static_cast<LONG>(Whole.Width), static_cast<LONG>(Whole.Height) };
    }
    if (NineRectEmpty(Dest))
    {
        const D3D12_SUBRESOURCE_FOOTPRINT& Whole = Copy->Dest->Footprints[Copy->DestSub].Footprint;

        *Dest = { 0, 0, static_cast<LONG>(Whole.Width), static_cast<LONG>(Whole.Height) };
    }

    Bound = { 0, 0, static_cast<LONG>(Copy->Dest->Desc.Width), static_cast<LONG>(Copy->Dest->Desc.Height) };
    NineClipToBound(Dest, Source, &Bound);
    Bound = { 0, 0, static_cast<LONG>(Copy->Source->Desc.Width), static_cast<LONG>(Copy->Source->Desc.Height) };
    NineClipToBound(Source, Dest, &Bound);
}

/* A copy of a resource the CPU cannot read, in a staging resource it can */
TL::unique_comptr<TL::Resource>
NineStagingCopy(
    _In_ NineResource* Resource)
{
    NineDevice* Device = Resource->Device;
    TL::ResourceCreationArgs Create = *Resource->Underlying->Parent();
    TL::unique_comptr<TL::Resource> Staging;

    Create.m_heapDesc.Properties = {};
    Create.m_heapDesc.Properties.Type = D3D12_HEAP_TYPE_READBACK;
    Create.m_heapDesc.Properties.CreationNodeMask = 1;
    Create.m_heapDesc.Properties.VisibleNodeMask = 1;
    Create.m_appDesc.m_usage = TL::RESOURCE_USAGE_STAGING;
    Create.m_appDesc.m_cpuAcess = TL::RESOURCE_CPU_ACCESS_READ;
    Create.m_appDesc.m_bindFlags = TL::RESOURCE_BIND_NONE;

    Staging = TL::Resource::CreateResource(&*Device->Context, Create,
                                           TL::ResourceAllocationContext::ImmediateContextThreadLongLived);
    Device->Context->ResourceCopy(Staging.get(), Resource->Underlying.get());
    return Staging;
}

/* Reads one subresource of a CPU readable resource row by row into the destination's system memory */
static
VOID
NineReadIntoSystemMemory(
    _In_ const NineCopy* Copy,
    _In_ TL::Resource* Readable)
{
    NineDevice* Device = Copy->Source->Device;
    const NineSysMemSource& Target = Copy->Dest->SysMem[Copy->DestSub];
    TL::MappedSubresource Mapped = {};
    const BYTE* From;
    BYTE* To;
    UINT64 RowBytes;
    UINT Rows;

    switch (Readable->AppDesc()->Usage())
    {
        case TL::RESOURCE_USAGE_DEFAULT:
            Device->Context->MapDefault(Readable, Copy->SourceSub, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
            break;
        case TL::RESOURCE_USAGE_DYNAMIC:
            if (Readable->Parent()->m_heapDesc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
                Device->Context->MapDynamicTexture(Readable, Copy->SourceSub, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
            else
                Device->Context->MapUnderlyingSynchronize(Readable, Copy->SourceSub, TL::MAP_TYPE_READ, false, nullptr,
                                                          &Mapped);
            break;
        case TL::RESOURCE_USAGE_STAGING:
            Device->Context->MapUnderlyingSynchronize(Readable, Copy->SourceSub, TL::MAP_TYPE_READ, false, nullptr,
                                                      &Mapped);
            break;
        default:
            break;
    }

    From = static_cast<const BYTE*>(Mapped.pData);
    if (Copy->Whole)
    {
        To = const_cast<BYTE*>(Target.Data);
        RowBytes = Target.RowPitch;
        Rows = Copy->Source->RowCounts[Copy->SourceSub];
    }
    else
    {
        const DXGI_FORMAT Format = Copy->Source->Typed.Format;
        const UINT Bytes = Format != DXGI_FORMAT_UNKNOWN ? CD3D11FormatHelper::GetBitsPerUnit(Format) >> 3 : 1;

        To = const_cast<BYTE*>(Target.Data) + Copy->DestZ * Target.SlicePitch + Copy->DestX * Bytes +
             Copy->DestY * Target.RowPitch;
        From += Copy->SourceBox.front * Mapped.DepthPitch + Bytes * Copy->SourceBox.left +
                Copy->SourceBox.top * Mapped.RowPitch;
        RowBytes = (Copy->SourceBox.right - Copy->SourceBox.left) * Bytes;
        Rows = Copy->SourceBox.bottom - Copy->SourceBox.top;
    }

    for (UINT Row = 0; Row < Rows; Row += 1)
        CopyMemory(To + Row * Target.RowPitch, From + static_cast<UINT64>(Row) * Mapped.RowPitch, RowBytes);

    switch (Readable->AppDesc()->Usage())
    {
        case TL::RESOURCE_USAGE_DEFAULT:
            Device->Context->UnmapDefault(Readable, Copy->SourceSub, nullptr);
            break;
        case TL::RESOURCE_USAGE_DYNAMIC:
            if (Readable->Parent()->m_heapDesc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
                Device->Context->UnmapDynamicTexture(Readable, Copy->SourceSub, nullptr, false);
            else
                Device->Context->UnmapUnderlyingSimple(Readable, Copy->SourceSub, nullptr);
            break;
        case TL::RESOURCE_USAGE_STAGING:
            Device->Context->UnmapUnderlyingStaging(Readable, Copy->SourceSub, nullptr);
            break;
        default:
            break;
    }
}

/* The source itself when the CPU can read it, otherwise a staging copy kept in Staging */
static
TL::Resource*
NineReadableSource(
    _In_ NineResource* Source,
    _Inout_ TL::unique_comptr<TL::Resource>& Staging)
{
    TL::Resource* Readable = Source->Underlying.get();

    if ((Readable->AppDesc()->CPUAccessFlags() & TL::RESOURCE_CPU_ACCESS_READ) != 0)
        return Readable;
    Staging = NineStagingCopy(Source);
    return Staging.get();
}

/* Reads a GPU subresource row by row into the runtime's system memory copy of the destination */
static
VOID
NineCopyToSystemMemory(
    _In_ const NineCopy* Copy)
{
    TL::unique_comptr<TL::Resource> Staging;

    NineReadIntoSystemMemory(Copy, NineReadableSource(Copy->Source, Staging));
}

/* Copies plane by plane, or draws the copy when the formats, sample counts or sizes call for it */
static
HRESULT
NineCopySubresources(
    _Inout_ NineCopy* Copy)
{
    NineDevice* Device = Copy->Source->Device;
    NineResource* Source = Copy->Source;
    NineResource* Dest = Copy->Dest;
    UINT SourceSubs[3];
    UINT DestSubs[3];
    const UINT SourceIndex = Copy->SourceSub;
    const UINT DestIndex = Copy->DestSub;

    for (UINT Plane = 0; Plane < Source->PlaneCount; Plane += 1)
        SourceSubs[Plane] = NineSubresourceOf(Source, SourceIndex, Plane, FALSE);
    for (UINT Plane = 0; Plane < Dest->PlaneCount; Plane += 1)
        DestSubs[Plane] = NineSubresourceOf(Dest, DestIndex, Plane, FALSE);

    if (Source->PlaneCount != 1 || Dest->PlaneCount != 1)
    {
        NINE_TRACE_ONCE(L"copies between planar surfaces are not done yet");
        return E_NOTIMPL;
    }

    Copy->SourceSub = SourceSubs[0];
    Copy->DestSub = DestSubs[0];

    if (Dest->SystemMemory)
    {
        NineCopyToSystemMemory(Copy);
        return S_OK;
    }
    if (Source->SystemMemory)
    {
        NINE_TRACE_ONCE(L"copies from system memory surfaces are not done yet");
        return E_NOTIMPL;
    }

    const UINT SourceSamples = Source->Desc.SampleDesc.Count;
    const UINT DestSamples = Dest->Desc.SampleDesc.Count;
    const BOOLEAN SourceWide = Source->Format == D3DDDIFMT_A2R10G10B10;
    const BOOLEAN DestWide = Dest->Format == D3DDDIFMT_A2R10G10B10;
    BOOLEAN OtherFamily = FALSE;

    if (Source->DxgiFormat != DXGI_FORMAT_UNKNOWN && Dest->DxgiFormat != DXGI_FORMAT_UNKNOWN)
    {
        OtherFamily = CD3D11FormatHelper::GetParentFormat(Source->DxgiFormat) !=
                      CD3D11FormatHelper::GetParentFormat(Dest->DxgiFormat);
    }

    /* Only a whole, unstretched copy within one format family resolves; anything else is drawn */
    if (SourceSamples > 1 && DestSamples == 1 && !OtherFamily && !Copy->Stretch && Copy->Whole &&
        SourceWide == DestWide)
    {
        Device->Context->ResourceResolveSubresource(Dest->Underlying.get(), Copy->DestSub, Source->Underlying.get(),
                                                    Copy->SourceSub, Dest->DxgiFormat);
        return S_OK;
    }

    if (!OtherFamily && !Copy->Stretch && SourceSamples == DestSamples && SourceWide == DestWide)
    {
        Device->Context->ResourceCopyRegion(Dest->Underlying.get(), Copy->DestSub, Copy->DestX, Copy->DestY, Copy->DestZ,
                                            Source->Underlying.get(), Copy->SourceSub,
                                            Copy->Whole ? nullptr : &Copy->SourceBox);
        return S_OK;
    }

    RECT From;
    RECT To;

    if (Copy->Stretch)
    {
        From = Copy->SourceRect;
        To = Copy->DestRect;
    }
    else
    {
        From = { static_cast<LONG>(Copy->SourceBox.left), static_cast<LONG>(Copy->SourceBox.top),
                 static_cast<LONG>(Copy->SourceBox.right), static_cast<LONG>(Copy->SourceBox.bottom) };
        To = { static_cast<LONG>(Copy->DestX), static_cast<LONG>(Copy->DestY),
               static_cast<LONG>(Copy->DestX + Copy->SourceBox.right - Copy->SourceBox.left),
               static_cast<LONG>(Copy->DestY + Copy->SourceBox.bottom - Copy->SourceBox.top) };
    }
    NineFitRects(Copy, &From, &To);

    Device->Context->m_BlitHelper.Blit(Source->Underlying.get(), &Copy->SourceSub, 1, From, Dest->Underlying.get(),
                                       &Copy->DestSub, 1, To, false, SourceWide != DestWide);
    return S_OK;
}

HRESULT
APIENTRY
NineBlt(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_BLT* Args)
{
    NineResource* Source;
    NineResource* Dest;
    NineCopy Copy = {};
    HRESULT hr;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    Source = NineBltResource(Args->hSrcResource);
    Dest = NineBltResource(Args->hDstResource);
    if (Source == nullptr || Dest == nullptr)
        return E_INVALIDARG;
    if (NineIsYuv(Source->DxgiFormat) != NineIsYuv(Dest->DxgiFormat) && NineIsYuv(Dest->DxgiFormat))
        return E_INVALIDARG;

    Copy.Source = Source;
    Copy.Dest = Dest;
    Copy.SourceSub = Args->SrcSubResourceIndex;
    Copy.DestSub = Args->DstSubResourceIndex;

    const LONG SourceWidth = Args->SrcRect.right - Args->SrcRect.left;
    const LONG SourceHeight = Args->SrcRect.bottom - Args->SrcRect.top;
    const LONG DestWidth = Args->DstRect.right - Args->DstRect.left;
    const LONG DestHeight = Args->DstRect.bottom - Args->DstRect.top;

    if (SourceWidth == DestWidth && SourceHeight == DestHeight)
    {
        if (Source->Typed.Width == static_cast<UINT64>(static_cast<UINT>(SourceWidth)) &&
            Source->Typed.Height == static_cast<UINT>(SourceHeight) &&
            Dest->Typed.Width == static_cast<UINT64>(static_cast<UINT>(DestWidth)) &&
            Dest->Typed.Height == static_cast<UINT>(DestHeight))
        {
            Copy.Whole = TRUE;
        }
        else
        {
            Copy.DestX = Args->DstRect.left;
            Copy.DestY = Args->DstRect.top;
            Copy.SourceBox = { static_cast<UINT>(Args->SrcRect.left), static_cast<UINT>(Args->SrcRect.top), 0,
                               static_cast<UINT>(Args->SrcRect.right), static_cast<UINT>(Args->SrcRect.bottom), 1 };
        }
        NineAlignCopy(&Copy);
    }
    else
    {
        Copy.Stretch = TRUE;
        Copy.SourceRect = Args->SrcRect;
        Copy.DestRect = Args->DstRect;
    }

    try
    {
        hr = NineCopySubresources(&Copy);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }
    return NineDxgiResult(hr);
}

/* Uploads one subresource of a system memory resource into a GPU one */
static
VOID
NineUploadFromSystemMemory(
    _In_ NineResource* Source,
    _In_ UINT SourceSub,
    _Inout_ NineResource* Dest,
    _In_ UINT DestSub,
    _In_ const NineCopy* Copy)
{
    NineDevice* Device = Dest->Device;
    const NineSysMemSource& From = Source->SysMem[SourceSub];
    const BOOLEAN Blocks = CD3D11FormatHelper::IsBlockCompressFormat(Source->DxgiFormat);
    UINT Bits = Source->DxgiFormat != DXGI_FORMAT_UNKNOWN ? CD3D11FormatHelper::GetBitsPerUnit(Source->DxgiFormat) : 8;
    TL::Resource* Target = Dest->Underlying.get();
    const UINT Mips = Target->AppDesc()->MipLevels();
    const UINT Slices = Target->AppDesc()->ArraySize();
    D3D11_SUBRESOURCE_DATA Data;
    D3D12_BOX Box;
    UINT Flags = 0;
    const BYTE* Bytes = From.Data;

    if (!Copy->Whole)
    {
        const D3D12_BOX& Area = Copy->SourceBox;

        Bytes += static_cast<SIZE_T>(Blocks ? Area.left / 4 : Area.left) * (Bits / 8) +
                 From.RowPitch * (Blocks ? Area.top / 4 : Area.top) + From.SlicePitch * Area.front;
        Box = { Copy->DestX, Copy->DestY, Copy->DestZ, Copy->DestX + (Area.right - Area.left),
                Copy->DestY + (Area.bottom - Area.top), Copy->DestZ + (Area.back - Area.front) };
    }
    Data.pSysMem = Bytes;
    Data.SysMemPitch = static_cast<UINT>(From.RowPitch);
    Data.SysMemSlicePitch = static_cast<UINT>(From.SlicePitch);

    if ((Source->Format == D3DDDIFMT_A2R10G10B10) != (Dest->Format == D3DDDIFMT_A2R10G10B10))
        Flags = static_cast<UINT>(TL::ImmediateContext::UpdateSubresourcesFlags::ChannelSwapR10G10B10A2);

    TL::CSubresourceSubset Subset(1, 1, static_cast<UINT8>(Target->AppDesc()->NonOpaquePlaneCount()),
                                  static_cast<UINT8>(DestSub % Mips), static_cast<UINT16>((DestSub / Mips) % Slices),
                                  static_cast<UINT8>(DestSub / (Mips * Slices)));
    Device->Context->UpdateSubresources(Target, Subset, &Data, Copy->Whole ? nullptr : &Box,
                                        static_cast<TL::ImmediateContext::UpdateSubresourcesFlags>(Flags));
}

/* Keeps a box within a subresource's footprint; the height limit is taken from the left edge as the reference does */
static
VOID
NineLimitBox(
    _Inout_ D3D12_BOX* Box,
    _In_ const D3D12_SUBRESOURCE_FOOTPRINT* Footprint)
{
    if (Box->right - Box->left > Footprint->Width)
        Box->right = Box->left + Footprint->Width;
    if (Box->bottom - Box->top > Footprint->Height)
        Box->bottom = Box->left + Footprint->Height;
    if (Box->back - Box->front > Footprint->Depth)
        Box->back = Box->front + Footprint->Depth;
}

/* Moves a partial copy to one source and one destination level */
static
VOID
NineCopyAtLevels(
    _Inout_ NineCopy* Part,
    _In_ UINT SourceLevel,
    _In_ UINT DestLevel)
{
    const D3D12_SUBRESOURCE_FOOTPRINT& From = Part->Source->Footprints[SourceLevel].Footprint;
    const D3D12_SUBRESOURCE_FOOTPRINT& To = Part->Dest->Footprints[DestLevel].Footprint;
    D3D12_BOX* Box = &Part->SourceBox;

    if (Part->Whole)
        return;

    Part->DestX >>= DestLevel;
    Part->DestY >>= DestLevel;
    Part->DestZ >>= DestLevel;
    if (SourceLevel != 0)
    {
        const UINT Round = (1u << SourceLevel) - 1;

        Box->left >>= SourceLevel;
        Box->top >>= SourceLevel;
        Box->front >>= SourceLevel;
        Box->right = (Box->right >> SourceLevel) + ((Box->right & Round) != 0 ? 1 : 0);
        Box->bottom = (Box->bottom >> SourceLevel) + ((Box->bottom & Round) != 0 ? 1 : 0);
        Box->back = (Box->back >> SourceLevel) + ((Box->back & Round) != 0 ? 1 : 0);

        NineLimitBox(Box, &From);
        NineLimitBox(Box, &To);
        Box->right = std::min(Box->right, From.Width);
        Box->bottom = std::min(Box->bottom, From.Height);
        Box->back = std::min(Box->back, From.Depth);
        if (Box->left >= Box->right)
            Box->left = Box->right - 1;
        if (Box->top >= Box->bottom)
            Box->top = Box->bottom - 1;
        if (Box->front >= Box->back)
            Box->front = Box->back - 1;
        if (Part->DestZ == To.Depth)
            Part->DestZ = To.Depth - 1;
    }
    NineAlignCopy(Part);
}

/* How many top levels of one texture are larger than the other in both directions */
static
UINT
NineLevelsAbove(
    _In_ const NineResource* Larger,
    _In_ const NineResource* Other)
{
    const UINT Levels = Larger->AutogenMips ? 1 : Larger->Typed.MipLevels;
    UINT Skipped = 0;

    while (Skipped < Levels)
    {
        if ((Larger->Typed.Width >> Skipped) <= Other->Typed.Width || (Larger->Typed.Height >> Skipped) <= Other->Typed.Height)
            break;
        Skipped += 1;
    }
    return Skipped;
}

/*
 * Copies every slice and every level both textures have, one subresource at a time. Textures with
 * different level counts line up by size: each skips its levels larger than the other's top level.
 */
static
VOID
NineCopyEachSubresource(
    _Inout_ NineCopy* Copy,
    _In_ UINT Face)
{
    NineResource* Source = Copy->Source;
    NineResource* Dest = Copy->Dest;
    const UINT Slices = Dest->Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Dest->ArraySize;
    const UINT First = Face == ~0u ? 0 : Face;
    const UINT Last = Face == ~0u ? Slices : Face + 1;
    const UINT SourceMips = Source->Typed.MipLevels;
    const UINT DestMips = Dest->Typed.MipLevels;
    UINT SourceSkip = 0;
    UINT DestSkip = 0;
    UINT Levels = DestMips;

    if (Source->Subresources != Dest->Subresources)
    {
        SourceSkip = NineLevelsAbove(Source, Dest);
        DestSkip = NineLevelsAbove(Dest, Source);
        Levels = std::min(DestMips - DestSkip, SourceMips - SourceSkip);
    }

    for (UINT Slice = First; Slice < Last; Slice += 1)
    {
        for (UINT Level = 0; Level < Levels; Level += 1)
        {
            NineCopy Part = *Copy;
            const UINT SourceLevel = Level + SourceSkip;
            const UINT DestLevel = Level + DestSkip;
            const UINT SourceIndex = SourceLevel + SourceMips * Slice;
            const UINT DestIndex = DestLevel + DestMips * Slice;

            NineCopyAtLevels(&Part, SourceLevel, DestLevel);
            if (Source->SystemMemory)
            {
                NineUploadFromSystemMemory(Source, SourceIndex, Dest, NineSubresourceOf(Dest, DestIndex, 0, TRUE), &Part);
            }
            else
            {
                Part.SourceSub = SourceIndex;
                Part.DestSub = DestIndex;
                NineCopySubresources(&Part);
            }
        }
    }
}

/* Reads every subresource into a system memory texture through one staging copy, all with the same area */
static
VOID
NineCopyAllToSystemMemory(
    _Inout_ NineCopy* Copy)
{
    TL::unique_comptr<TL::Resource> Staging;
    TL::Resource* Readable = NineReadableSource(Copy->Source, Staging);
    const UINT Count = std::max(Copy->Source->Subresources, Copy->Dest->Subresources);

    for (UINT Index = 0; Index < Count; Index += 1)
    {
        NineCopy Part = *Copy;

        Part.SourceSub = Index;
        Part.DestSub = Index;
        NineReadIntoSystemMemory(&Part, Readable);
    }
}

HRESULT
APIENTRY
NineTexBlt1(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_TEXBLT1* Args)
{
    NineResource* Source;
    NineResource* Dest;
    NineCopy Copy = {};
    HRESULT Result;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;
    Source = NineBltResource(Args->hSrcResource);
    Dest = NineBltResource(Args->hDstResource);
    if (Source == nullptr || Dest == nullptr)
        return E_INVALIDARG;

    /* The reference reports a copy onto itself as not done but makes it anyway */
    Result = Source == Dest ? E_NOTIMPL : S_OK;

    Copy.Source = Source;
    Copy.Dest = Dest;
    Copy.DestX = Args->DstPoint.x;
    Copy.DestY = Args->DstPoint.y;
    Copy.SourceBox = { static_cast<UINT>(Args->SrcRect.left), static_cast<UINT>(Args->SrcRect.top), 0,
                       static_cast<UINT>(Args->SrcRect.right), static_cast<UINT>(Args->SrcRect.bottom), 1 };
    if (static_cast<UINT64>(Args->SrcRect.right - Args->SrcRect.left) == Dest->Typed.Width &&
        static_cast<UINT>(Args->SrcRect.bottom - Args->SrcRect.top) == Dest->Typed.Height)
    {
        Copy.Whole = TRUE;
    }
    else
    {
        NineAlignCopy(&Copy);
    }

    try
    {
        if (Source->Subresources == Dest->Subresources && Dest->SystemMemory)
        {
            if (Source->SystemMemory)
                NINE_TRACE_ONCE(L"texture blits between system memory textures are not done yet");
            else
                NineCopyAllToSystemMemory(&Copy);
        }
        else if (Source->Subresources == Dest->Subresources && Copy.Whole && !Source->SystemMemory && Source->Format == Dest->Format &&
                 !(Source->Typed.SampleDesc.Count > 1 && Dest->Typed.SampleDesc.Count == 1))
            Dest->Device->Context->ResourceCopy(Dest->Underlying.get(), Source->Underlying.get());
        else
            NineCopyEachSubresource(&Copy, Args->CubeMapFace);
        Dest->FanStart = 0;
        Dest->FanValid = FALSE;
    }
    catch (_com_error& Error)
    {
        Result = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        Result = E_OUTOFMEMORY;
    }
    return NineDxgiResult(Result);
}
