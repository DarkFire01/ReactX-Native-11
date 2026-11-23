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
static
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

/* Reads a GPU subresource row by row into the runtime's system memory copy of the destination */
static
VOID
NineCopyToSystemMemory(
    _In_ const NineCopy* Copy)
{
    NineDevice* Device = Copy->Source->Device;
    TL::unique_comptr<TL::Resource> Staging;
    TL::Resource* Readable = Copy->Source->Underlying.get();
    const NineSysMemSource& Target = Copy->Dest->SysMem[Copy->DestSub];
    TL::MappedSubresource Mapped = {};
    const BYTE* From;
    BYTE* To;
    UINT64 RowBytes;
    UINT Rows;

    if ((Readable->AppDesc()->CPUAccessFlags() & TL::RESOURCE_CPU_ACCESS_READ) == 0)
    {
        Staging = NineStagingCopy(Copy->Source);
        Readable = Staging.get();
    }

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
