/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Surfaces, textures and buffers on translation layer resources, and locking them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <algorithm>

/* Resource flag bits of D3DDDI_RESOURCEFLAGS and D3DDDI_RESOURCEFLAGS2 */
static constexpr UINT NineFlagRenderTarget = 0x1;
static constexpr UINT NineFlagZBuffer = 0x2;
static constexpr UINT NineFlagHintStatic = 0x8;
static constexpr UINT NineFlagAutogenMipmap = 0x10;
static constexpr UINT NineFlagWriteOnly = 0x40;
static constexpr UINT NineFlagNotLockable = 0x80;
static constexpr UINT NineFlagShared = 0x800;
static constexpr UINT NineFlagPrimary = 0x8000;
static constexpr UINT NineFlagTexture = 0x10000;
static constexpr UINT NineFlagCubeMap = 0x20000;
static constexpr UINT NineFlagVolume = 0x40000;
static constexpr UINT NineFlagVertexBuffer = 0x80000;
static constexpr UINT NineFlagIndexBuffer = 0x100000;
static constexpr UINT NineFlagDecodeTarget = 0x200000;
static constexpr UINT NineFlagDecodeBuffer = 0x400000;
static constexpr UINT NineFlagProcessTarget = 0x800000;
static constexpr UINT NineFlag2CrossAdapter = 0x4;
static constexpr UINT NineFlag2Displayable = 0x8;

/* Lock flag bits of D3DDDI_LOCKFLAGS */
static constexpr UINT NineLockReadOnly = 0x1;
static constexpr UINT NineLockNoOverwrite = 0x4;
static constexpr UINT NineLockDiscard = 0x8;
static constexpr UINT NineLockRangeValid = 0x10;
static constexpr UINT NineLockAreaValid = 0x20;
static constexpr UINT NineLockBoxValid = 0x40;
static constexpr UINT NineLockRegionMask = NineLockRangeValid | NineLockAreaValid | NineLockBoxValid;
static constexpr UINT NineLockNotifyOnly = 0x80;
static constexpr UINT NineLockMightDraw = 0x100;
static constexpr UINT NineLockDoNotWait = 0x200;

/* Textures wider or taller than this are refused */
static constexpr UINT NineMaxTextureSize = 0x4000;

static constexpr UINT NineUnlockNotifyOnly = 0x1;

static volatile LONG64 NineResourceSerial;

static constexpr D3DDDIFORMAT
NineFourCcFormat(
    _In_ char A,
    _In_ char B,
    _In_ char C,
    _In_ char D)
{
    return static_cast<D3DDDIFORMAT>(static_cast<UINT>(static_cast<BYTE>(A)) |
                                     (static_cast<UINT>(static_cast<BYTE>(B)) << 8) |
                                     (static_cast<UINT>(static_cast<BYTE>(C)) << 16) |
                                     (static_cast<UINT>(static_cast<BYTE>(D)) << 24));
}

VOID
NineUnderlyingDeleter::operator()(
    _In_opt_ TL::Resource* Resource) const
{
    if (Resource == nullptr)
        return;
    Resource->ClearInputBindings();
    Resource->ClearOutputBindings();
    Resource->Release();
}

/* Pools that are only ever touched by the CPU get no views */
static
BOOLEAN
NineGpuPool(
    _In_ D3DDDI_POOL Pool)
{
    return Pool != D3DDDIPOOL_SYSTEMMEM && Pool != D3DDDIPOOL_STAGINGMEM;
}

static
BOOLEAN
NineWantsRenderTarget(
    _In_ UINT Flags,
    _In_ D3DDDI_POOL Pool)
{
    return NineGpuPool(Pool) && (Flags & (NineFlagRenderTarget | NineFlagAutogenMipmap)) != 0;
}

static
BOOLEAN
NineWantsDepthStencil(
    _In_ UINT Flags,
    _In_ D3DDDI_POOL Pool)
{
    return NineGpuPool(Pool) && (Flags & NineFlagZBuffer) != 0;
}

static
BOOLEAN
NineWantsShaderView(
    _In_ UINT Flags,
    _In_ D3DDDI_POOL Pool)
{
    if (!NineGpuPool(Pool) || (Flags & (NineFlagDecodeTarget | NineFlagProcessTarget)) != 0)
        return FALSE;
    return (Flags & (NineFlagTexture | NineFlagCubeMap | NineFlagVolume)) != 0;
}

/* The first sRGB format a format can be cast to, or unknown */
static
DXGI_FORMAT
NineSrgbFormat(
    _In_ DXGI_FORMAT Format)
{
    const DXGI_FORMAT* Cast = CD3D11FormatHelper::GetFormatCastSet(Format);

    for (; *Cast != DXGI_FORMAT_UNKNOWN; Cast += 1)
    {
        if (CD3D11FormatHelper::IsSRGBFormat(*Cast))
            return *Cast;
    }
    return DXGI_FORMAT_UNKNOWN;
}

/* Depth formats are locked only in their lockable variants */
static
BOOLEAN
NineFormatLockable(
    _In_ D3DDDIFORMAT Format)
{
    if (Format == D3DDDIFMT_D16_LOCKABLE || Format == D3DDDIFMT_D32F_LOCKABLE || NineIsIhvFormat(Format))
        return TRUE;
    return CD3D11FormatHelper::GetComponentName(NineDxgiFormat(Format), 0) != D3D11FCN_D;
}

/* The color format of a depth format's family that reads the depth the same way */
static
DXGI_FORMAT
NineDepthCompanion(
    _In_ DXGI_FORMAT Format)
{
    const D3D11_FORMAT_COMPONENT_INTERPRETATION Wanted = CD3D11FormatHelper::GetFormatComponentInterpretation(Format, 0);
    const DXGI_FORMAT* Cast = CD3D11FormatHelper::GetFormatCastSet(Format);

    for (; *Cast != DXGI_FORMAT_UNKNOWN; Cast += 1)
    {
        if (CD3D11FormatHelper::GetTypeLevel(*Cast) != D3D11FTL_FULL_TYPE ||
            CD3D11FormatHelper::GetComponentName(*Cast, 0) != D3D11FCN_R)
        {
            continue;
        }
        if (CD3D11FormatHelper::GetFormatComponentInterpretation(*Cast, 0) == Wanted)
            return *Cast;
    }
    return DXGI_FORMAT_UNKNOWN;
}

/* Number of bits needed to hold the value, which is the full mip chain length for that size */
static
UINT16
NineMipChainLength(
    _In_ UINT Largest)
{
    UINT16 Length = 0;

    while (Largest != 0)
    {
        Length += 1;
        Largest >>= 1;
    }
    return Length;
}

static
UINT
NineAtLeastOne(
    _In_ UINT Value)
{
    return Value != 0 ? Value : 1;
}

/* Sample count and quality of a Direct3D 9 multisample type */
static
VOID
NineSampleDesc(
    _In_ const D3DDDIARG_CREATERESOURCE2* Args,
    _Out_ DXGI_SAMPLE_DESC* Samples)
{
    if (Args->MultisampleType == D3DDDIMULTISAMPLE_NONE)
    {
        Samples->Count = 1;
        Samples->Quality = 0;
    }
    else if (Args->MultisampleType == D3DDDIMULTISAMPLE_NONMASKABLE)
    {
        Samples->Count = 2U << Args->MultisampleQuality;
        Samples->Quality = 0;
    }
    else
    {
        Samples->Count = Args->MultisampleType;
        Samples->Quality = Args->MultisampleQuality;
    }
}

/* The translation layer bind flags carry the Direct3D 11 bind bits unchanged */
static
UINT
NineLegacyBindFlags(
    _In_ UINT Bind)
{
    return Bind & (TL::RESOURCE_BIND_VERTEX_BUFFER | TL::RESOURCE_BIND_INDEX_BUFFER |
                   TL::RESOURCE_BIND_CONSTANT_BUFFER | TL::RESOURCE_BIND_SHADER_RESOURCE |
                   TL::RESOURCE_BIND_STREAM_OUTPUT | TL::RESOURCE_BIND_RENDER_TARGET |
                   TL::RESOURCE_BIND_DEPTH_STENCIL | TL::RESOURCE_BIND_UNORDERED_ACCESS |
                   TL::RESOURCE_BIND_DECODER | TL::RESOURCE_BIND_VIDEO_ENCODER);
}

/* One render target view per array slice and mip, plus sRGB twins when asked for */
static
VOID
NineMakeRenderTargetViews(
    _Inout_ NineResource* Resource,
    _In_ DXGI_FORMAT Format,
    _In_ BOOLEAN WithSrgb)
{
    const DXGI_FORMAT Srgb = NineSrgbFormat(Format);
    const BOOLEAN Twins = WithSrgb && Srgb != DXGI_FORMAT_UNKNOWN;
    const D3D12_RESOURCE_DESC& Typed = Resource->Typed;
    const UINT Slices = Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Resource->ArraySize;
    TL::ImmediateContext* Context = &*Resource->Device->Context;
    D3D12_RENDER_TARGET_VIEW_DESC View = {};
    UINT Next = 0;

    Resource->Rtvs.clear();
    Resource->Rtvs.resize(Resource->Subresources);
    if (Twins)
    {
        Resource->SrgbRtvs.clear();
        Resource->SrgbRtvs.resize(Resource->Subresources);
    }

    /* A view keeps the fields an earlier one set when its own layout does not use them */
    for (UINT Slice = 0; Slice < Slices; Slice += 1)
    {
        for (UINT Mip = 0; Mip < Typed.MipLevels; Mip += 1)
        {
            if (Typed.DepthOrArraySize > 1 && Typed.SampleDesc.Count > 1)
            {
                View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
                View.Texture2DArray.MipSlice = Slice;
                View.Texture2DArray.FirstArraySlice = 1;
            }
            else if (Typed.DepthOrArraySize > 1)
            {
                View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                View.Texture2DArray.MipSlice = Mip;
                View.Texture2DArray.FirstArraySlice = Slice;
                View.Texture2DArray.ArraySize = 1;
                View.Texture2DArray.PlaneSlice = 0;
            }
            else if (Typed.SampleDesc.Count > 1)
            {
                View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                View.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                View.Texture2D.MipSlice = Mip;
                View.Texture2D.PlaneSlice = 0;
            }

            View.Format = Format;
            Resource->Rtvs[Next].reset(TL::TRTV::CreateView(Context, View, *Resource->Underlying));
            if (Twins)
            {
                View.Format = Srgb;
                Resource->SrgbRtvs[Next].reset(TL::TRTV::CreateView(Context, View, *Resource->Underlying));
            }
            Next += 1;
        }
    }
}

/* Plain, read only depth, read only stencil and fully read only depth views */
static
VOID
NineMakeDepthStencilViews(
    _Inout_ NineResource* Resource)
{
    TL::ImmediateContext* Context = &*Resource->Device->Context;

    Resource->DepthViewFormat = Resource->DxgiFormat;
    for (UINT Kind = 0; Kind < _countof(Resource->Dsvs); Kind += 1)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC View = {};
        const D3D12_DSV_FLAGS Flags = static_cast<D3D12_DSV_FLAGS>(Kind);

        if ((Flags & D3D12_DSV_FLAG_READ_ONLY_STENCIL) != 0 &&
            CD3D11FormatHelper::GetComponentName(Resource->DxgiFormat, 1) != D3D11FCN_S)
        {
            continue;
        }

        View.Format = Resource->DxgiFormat;
        View.Flags = Flags;
        if (Resource->Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (Resource->Typed.SampleDesc.Count > 1)
            {
                View.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
            }
            else
            {
                View.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                View.Texture2D.MipSlice = 0;
            }
        }
        Resource->Dsvs[Kind].reset(TL::TDSV::CreateView(Context, View, *Resource->Underlying));
    }
}

/* View options: read through the sRGB format, and expose only the top mip */
static constexpr UINT NineViewSrgb = 0x1;
static constexpr UINT NineViewTopMip = 0x2;

static
TL::TSRV*
NineMakeShaderView(
    _Inout_ NineResource* Resource,
    _In_ UINT Options)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC View = {};
    DXGI_FORMAT Format = NineDxgiFormat(Resource->Format);

    View.Format = Format;
    if (CD3D11FormatHelper::GetComponentName(Format, 0) == D3D11FCN_D)
        View.Format = NineDepthCompanion(Format);
    View.Shader4ComponentMapping = NineViewSwizzle(Resource->Format, Resource->PromotedPrimary);

    if (Resource->Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        View.Texture3D.MipLevels = Resource->Typed.MipLevels;
    }
    else if (Resource->Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
    {
        if (Resource->ArraySize == 6)
        {
            View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            View.TextureCube.MipLevels = Resource->Typed.MipLevels;
        }
        else if (Resource->Typed.SampleDesc.Count > 1)
        {
            View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        }
        else
        {
            View.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            View.Texture2D.MipLevels = Resource->Typed.MipLevels;
        }
    }

    if ((Options & NineViewSrgb) != 0 && NineSrgbFormat(View.Format) != DXGI_FORMAT_UNKNOWN)
        View.Format = NineSrgbFormat(View.Format);

    if ((Options & NineViewTopMip) != 0 &&
        (View.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D || View.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2DARRAY ||
         View.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE3D || View.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE))
    {
        View.Texture2D.MostDetailedMip = 0;
        View.Texture2D.MipLevels = 1;
    }

    return TL::TSRV::CreateView(&*Resource->Device->Context, View, *Resource->Underlying);
}

/* The texture kind a sampler sees through a shader view */
static
UINT
NineTextureKind(
    _In_ D3D12_SRV_DIMENSION Dimension)
{
    switch (Dimension)
    {
        case D3D12_SRV_DIMENSION_TEXTURE2D:
        case D3D12_SRV_DIMENSION_TEXTURE2DMS:
            return 1;
        case D3D12_SRV_DIMENSION_TEXTURECUBE:
            return 2;
        case D3D12_SRV_DIMENSION_TEXTURE3D:
            return 3;
        default:
            return 0;
    }
}

/* Builds the Direct3D 12 description and the footprints every lock and upload use */
static
HRESULT
NineDescribeResource(
    _Inout_ NineResource* Resource,
    _In_ const D3DDDIARG_CREATERESOURCE2* Args,
    _In_ BOOLEAN FromExisting)
{
    NineDevice* Device = Resource->Device;
    const D3DDDI_SURFACEINFO* First = Args->pSurfList;
    const UINT Flags = Args->Flags.Value;
    D3D12_RESOURCE_DESC& Desc = Resource->Desc;
    D3D12_RESOURCE_DIMENSION Dimension;
    D3DDDIFORMAT Format;
    UINT16 Mips;
    BOOLEAN WantsSrgb;
    BOOLEAN SampledDepth;

    if (First == nullptr)
        return E_INVALIDARG;

    if (Args->Format == D3DDDIFMT_D24S8 || Args->Format == D3DDDIFMT_D24X8 || Args->Format == D3DDDIFMT_D16)
        Resource->SampledDepth = TRUE;
    Resource->SystemMemory = Args->Pool == D3DDDIPOOL_SYSTEMMEM;

    if ((Flags & (NineFlagVertexBuffer | NineFlagIndexBuffer | NineFlagDecodeBuffer)) != 0)
        Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    else if (First->Depth > 1)
        Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    else if (First->Width > NineMaxTextureSize || First->Height > NineMaxTextureSize)
        return E_INVALIDARG;
    else
        Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    /* Unformatted buffers read as 32 bit floats */
    Format = Args->Format;
    if (Format == D3DDDIFMT_UNKNOWN && (Flags & (NineFlagVertexBuffer | NineFlagIndexBuffer)) != 0)
        Format = D3DDDIFMT_R32F;
    Resource->Format = Format;

    if ((Flags & NineFlagPrimary) != 0)
    {
        if (Format == D3DDDIFMT_X8R8G8B8)
        {
            Format = D3DDDIFMT_A8R8G8B8;
            Resource->Format = Format;
            Resource->PromotedPrimary = TRUE;
        }
        Resource->VidPnSourceId = Args->VidPnSourceId;
    }
    else
    {
        Resource->VidPnSourceId = ~0U;
    }

    Resource->DxgiFormat = NineDxgiFormat(Format);
    Resource->Lockable = NineFormatLockable(Format) && (Flags & NineFlagNotLockable) == 0;
    Resource->PlaneCount = static_cast<UINT8>(CD3D11FormatHelper::NonOpaquePlaneCount(Resource->DxgiFormat));
    Resource->CubeMap = (Flags & NineFlagCubeMap) != 0;

    Mips = static_cast<UINT16>(NineAtLeastOne(Args->MipLevels));
    Resource->ArraySize = static_cast<UINT8>(Args->SurfCount / Mips);
    Resource->AutogenMips = (Flags & NineFlagAutogenMipmap) != 0;
    if (Resource->AutogenMips)
        Mips = NineMipChainLength((std::max)((std::max)(First->Width, First->Height), First->Depth));

    Desc = {};
    Desc.Dimension = Dimension;
    Desc.Width = NineAtLeastOne(First->Width);
    Desc.Height = NineAtLeastOne(First->Height);
    Desc.DepthOrArraySize = static_cast<UINT16>(Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                                    ? NineAtLeastOne(First->Depth)
                                                    : Resource->ArraySize);
    Desc.MipLevels = Mips;
    Desc.Format = Resource->DxgiFormat;
    NineSampleDesc(Args, &Desc.SampleDesc);
    if (Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
        ((Args->Flags2.Value & NineFlag2CrossAdapter) != 0 && Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D))
    {
        Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    }

    if (NineWantsRenderTarget(Flags, Args->Pool))
        Desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (NineWantsDepthStencil(Flags, Args->Pool))
    {
        Desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if ((Flags & NineFlagTexture) == 0)
            Desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    if ((Args->Flags2.Value & NineFlag2CrossAdapter) != 0)
        Desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;

    Resource->Subresources = Resource->PlaneCount * Resource->ArraySize * Mips;
    Resource->Locks.assign(Resource->Subresources, NineLockState());
    Resource->SysMem.assign(Resource->Subresources, NineSysMemSource());
    Resource->Footprints.assign(Resource->Subresources, D3D12_PLACED_SUBRESOURCE_FOOTPRINT());
    Resource->RowSizes.assign(Resource->Subresources, 0);
    Resource->RowCounts.assign(Resource->Subresources, 0);
    Resource->TotalSize = 0;
    Resource->Typed = Desc;

    /* Formats viewed as sRGB or sampled as depth are created typeless unless typed casts are allowed */
    WantsSrgb = NineSrgbFormat(Resource->DxgiFormat) != DXGI_FORMAT_UNKNOWN && !FromExisting;
    SampledDepth = NineWantsDepthStencil(Flags, Args->Pool) && NineWantsShaderView(Flags, Args->Pool);
    if (!Device->Adapter->TypedCasting && (WantsSrgb || SampledDepth))
        Desc.Format = CD3D11FormatHelper::GetParentFormat(Resource->DxgiFormat);

    Device->Device12->GetCopyableFootprints(&Desc, 0, Resource->Subresources, 0, Resource->Footprints.data(),
                                            Resource->RowCounts.data(), Resource->RowSizes.data(), &Resource->TotalSize);

    /* With unrestricted copy pitches the rows are packed as tightly as the format allows */
    if (Device->Context->GetOptions13().UnrestrictedBufferTextureCopyPitchSupported && Resource->PlaneCount == 1)
    {
        Resource->TotalSize = 0;
        for (UINT Sub = 0; Sub < Resource->Subresources; Sub += 1)
        {
            D3D12_SUBRESOURCE_FOOTPRINT& Footprint = Resource->Footprints[Sub].Footprint;
            UINT Pitch = 0;

            CD3D11FormatHelper::CalculateMinimumRowMajorRowPitch(Footprint.Format, Footprint.Width, Pitch);
            Footprint.RowPitch = Pitch;
            Resource->RowSizes[Sub] = Pitch;
            Resource->Footprints[Sub].Offset = Resource->TotalSize;
            Pitch = 0;
            CD3D11FormatHelper::CalculateMinimumRowMajorSlicePitch(Footprint.Format, Footprint.RowPitch,
                                                                    Footprint.Height, Pitch);
            Resource->TotalSize += static_cast<UINT64>(Footprint.Depth) * Pitch;
        }
    }

    /* Where the runtime keeps each surface's initial contents, plane by plane */
    if (!FromExisting)
    {
        for (UINT Plane = 0; Plane < Resource->PlaneCount; Plane += 1)
        {
            for (UINT Surface = 0; Surface < Args->SurfCount; Surface += 1)
            {
                const D3DDDI_SURFACEINFO* Info = &Args->pSurfList[Surface];
                NineSysMemSource* Source = &Resource->SysMem[Surface + Args->SurfCount * Plane];
                UINT Skip = 0;

                if (Plane != 0)
                {
                    UINT Horizontal = 0;
                    UINT Vertical = 0;

                    CD3D11FormatHelper::GetYCbCrChromaSubsampling(Resource->DxgiFormat, Horizontal, Vertical);
                    Skip = Info->Height * Info->SysMemPitch + (Info->Height / Vertical) * (Plane - 1);
                }

                Source->Data = static_cast<const BYTE*>(Info->pSysMem) + Skip;
                Source->RowPitch = Info->SysMemPitch;
                Source->SlicePitch = Info->SysMemSlicePitch;

                /* Vendor block formats give their pitch in blocks of four rows */
                if (NineIsIhvFormat(Args->Format) && CD3D11FormatHelper::IsBlockCompressFormat(Desc.Format))
                    Source->RowPitch *= (CD3D11FormatHelper::GetBitsPerUnit(Desc.Format) >> 3) >> 2;
            }
        }
    }

    if (Resource->SystemMemory && Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
    {
        Desc = {};
        Desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        Desc.Width = Resource->TotalSize;
        Desc.Height = 1;
        Desc.DepthOrArraySize = 1;
        Desc.MipLevels = 1;
        Desc.SampleDesc.Count = 1;
        Desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    }

    if ((Flags & NineFlagDecodeBuffer) != 0)
        Resource->DecodeBuffer = TRUE;
    return S_OK;
}

/* Fills the translation layer's creation arguments the way the reference describes its resources */
static
VOID
NineDescribeCreation(
    _Inout_ NineResource* Resource,
    _In_ const D3DDDIARG_CREATERESOURCE2* Args,
    _Out_ TL::ResourceCreationArgs* Create)
{
    NineDevice* Device = Resource->Device;
    const UINT Flags = Args->Flags.Value;
    const UINT Flags2 = Args->Flags2.Value;
    const D3D12_RESOURCE_DESC& Desc = Resource->Desc;
    D3D12_RESOURCE_ALLOCATION_INFO Info;
    TL::RESOURCE_USAGE Usage;
    D3D12_HEAP_TYPE HeapType;
    UINT Bind = 0;
    UINT Planes;
    UINT ArraySize;
    UINT Depth;

    if ((Flags & (NineFlagDecodeTarget | NineFlagDecodeBuffer)) != 0)
        Bind |= TL::RESOURCE_BIND_DECODER;
    if (NineWantsRenderTarget(Flags, Args->Pool))
        Bind |= TL::RESOURCE_BIND_RENDER_TARGET;
    if (NineWantsDepthStencil(Flags, Args->Pool))
        Bind |= TL::RESOURCE_BIND_DEPTH_STENCIL;
    if (NineWantsShaderView(Flags, Args->Pool) || Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        Bind |= TL::RESOURCE_BIND_SHADER_RESOURCE;
    if ((Flags & NineFlagVertexBuffer) != 0)
        Bind |= TL::RESOURCE_BIND_VERTEX_BUFFER;
    if ((Flags & NineFlagIndexBuffer) != 0)
        Bind |= TL::RESOURCE_BIND_INDEX_BUFFER;

    /* Lockable resources are written by the CPU, and read back too unless they only feed the GPU */
    if (Resource->Lockable)
    {
        Resource->CpuAccess |= TL::RESOURCE_CPU_ACCESS_WRITE;
        if ((Flags & (NineFlagVertexBuffer | NineFlagIndexBuffer | NineFlagWriteOnly)) == 0)
            Resource->CpuAccess |= TL::RESOURCE_CPU_ACCESS_READ;
    }

    if ((Flags & NineFlagDecodeBuffer) != 0 ||
        (Resource->CpuAccess & (TL::RESOURCE_CPU_ACCESS_WRITE | TL::RESOURCE_CPU_ACCESS_READ)) != 0)
    {
        Usage = TL::RESOURCE_USAGE_DYNAMIC;
    }
    else
    {
        Usage = TL::RESOURCE_USAGE_DEFAULT;
    }

    /* Static buffers live in GPU memory on adapters with their own memory */
    HeapType = TL::Resource::GetD3D12HeapType(Usage, Resource->CpuAccess);
    if (!Device->Context->m_architecture.isUMA && HeapType == D3D12_HEAP_TYPE_UPLOAD &&
        (Flags & (NineFlagVertexBuffer | NineFlagIndexBuffer)) != 0 && (Flags & NineFlagHintStatic) != 0)
    {
        HeapType = D3D12_HEAP_TYPE_DEFAULT;
    }

    Create->m_desc12 = Desc;

    Planes = CD3D11FormatHelper::NonOpaquePlaneCount(Desc.Format);
    ArraySize = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;
    Depth = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? Desc.DepthOrArraySize : 1;
    Create->m_appDesc = TL::AppResourceDesc(Desc.MipLevels * ArraySize, static_cast<UINT8>(Planes),
                                            Planes * Desc.MipLevels * ArraySize, static_cast<UINT8>(Desc.MipLevels),
                                            static_cast<UINT16>(ArraySize), Depth, static_cast<UINT>(Desc.Width),
                                            Desc.Height, Desc.Format, Desc.SampleDesc.Count, Desc.SampleDesc.Quality,
                                            Usage, static_cast<TL::RESOURCE_CPU_ACCESS>(Resource->CpuAccess),
                                            static_cast<TL::RESOURCE_BIND_FLAGS>(Bind), Desc.Dimension);

    Info = Device->Device12->GetResourceAllocationInfo(0, 1, &Desc);
    Create->m_heapDesc = {};
    Create->m_heapDesc.SizeInBytes = Info.SizeInBytes;
    Create->m_heapDesc.Properties.Type = HeapType;
    Create->m_heapDesc.Properties.CreationNodeMask = 1;
    Create->m_heapDesc.Properties.VisibleNodeMask = 1;
    Create->m_heapDesc.Alignment = Info.Alignment;
    Create->m_heapDesc.Flags = (Flags & NineFlagShared) != 0 ? D3D12_HEAP_FLAG_SHARED : D3D12_HEAP_FLAG_CREATE_NOT_RESIDENT;

    Create->m_isPlacedTexture = false;
    Create->m_bBoundForStreamOut = false;
    Create->m_OffsetToStreamOutputSuffix = 0;
    Create->m_bManageResidency = true;
    Create->m_bIsD3D9on12Resource = true;

    if ((Flags & NineFlagPrimary) != 0)
    {
        Create->m_heapDesc.Flags |= D3D12_HEAP_FLAG_ALLOW_DISPLAY;
        Create->m_bManageResidency = false;
    }

    /* Cross adapter surfaces sit in linear system memory both adapters can reach */
    if ((Flags2 & NineFlag2CrossAdapter) != 0)
    {
        Create->m_heapDesc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        Create->m_heapDesc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
        Create->m_heapDesc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        Create->m_heapDesc.Flags |= D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        Create->m_desc12.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Create->m_desc12.Flags |= D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
    }

    Create->m_flags11.BindFlags = NineLegacyBindFlags(Bind);
    if ((Flags & NineFlagShared) != 0)
    {
        Create->m_flags11.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
        if ((Flags2 & (NineFlag2CrossAdapter | NineFlag2Displayable)) != 0)
            Create->m_flags11.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        if ((Flags2 & NineFlag2Displayable) != 0)
            Create->m_bTriggerDeferredWaits = true;
    }
    if ((Flags & NineFlagDecodeTarget) != 0)
        Create->m_desc12.Flags |= D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    if (Resource->Format == NineFourCcFormat('Y', 'V', '1', '2'))
        Create->m_FormatEmulation = TL::FormatEmulation::YV12;
}

/*
 * Creates the translation layer resource, or takes over an opened one, and the views the resource's
 * use calls for.
 */
static
HRESULT
NineInitResource(
    _Inout_ NineResource* Resource,
    _In_ const D3DDDIARG_CREATERESOURCE2* Args,
    _Inout_opt_ TL::unique_comptr<TL::Resource>* Existing)
{
    const UINT Flags = Args->Flags.Value;
    TL::ResourceCreationArgs Create = {};
    BOOLEAN WantsSrgb;
    HRESULT hr;

    hr = NineDescribeResource(Resource, Args, Existing != nullptr);
    if (FAILED(hr))
        return hr;

    /* System memory resources stay with the runtime's own copy */
    if (Resource->SystemMemory)
    {
        Resource->Underlying.reset();
        return S_OK;
    }

    NineDescribeCreation(Resource, Args, &Create);
    if (Existing != nullptr && *Existing)
    {
        Resource->Underlying.reset(Existing->release());
    }
    else
    {
        Resource->Underlying.reset(TL::Resource::CreateResource(&*Resource->Device->Context, Create,
                                                                TL::ResourceAllocationContext::ImmediateContextThreadLongLived)
                                       .release());
    }

    /* Render targets always get their sRGB twins; an opened resource gets no sRGB shader view */
    WantsSrgb = NineSrgbFormat(Resource->DxgiFormat) != DXGI_FORMAT_UNKNOWN;
    if (NineWantsRenderTarget(Flags, Args->Pool))
        NineMakeRenderTargetViews(Resource, Resource->DxgiFormat, WantsSrgb);
    WantsSrgb = WantsSrgb && Existing == nullptr;
    if (NineWantsDepthStencil(Flags, Args->Pool))
        NineMakeDepthStencilViews(Resource);
    if (NineWantsShaderView(Flags, Args->Pool))
    {
        Resource->Srv.reset(NineMakeShaderView(Resource, 0));
        Resource->TextureType = NineTextureKind(Resource->Srv->GetDesc12().ViewDimension);
        if (WantsSrgb)
            Resource->SrgbSrv.reset(NineMakeShaderView(Resource, NineViewSrgb));
    }

    if (NineGpuPool(Args->Pool) && (Flags & NineFlagDecodeTarget) != 0)
        NINE_TRACE_ONCE(L"decoder output views are not created yet");
    if (NineGpuPool(Args->Pool) && (Flags & NineFlagProcessTarget) != 0)
        NINE_TRACE_ONCE(L"video processor output views are not created yet");
    return S_OK;
}

/* Drops the views, the lock history and the translation layer resource */
static
VOID
NineFreeResource(
    _In_ _Post_invalid_ NineResource* Resource)
{
    NineDevice* Device = Resource->Device;

    if (Resource->Underlying)
    {
        Resource->Underlying->ClearInputBindings();
        Resource->Underlying->ClearOutputBindings();
    }
    NineUnbindInputs(Resource);
    NineUnbindOutputs(Resource);
    for (std::unique_ptr<TL::TDSV>& View : Resource->Dsvs)
        View.reset();

    {
        std::lock_guard<std::mutex> Guard(Device->LockRangesLock);
        Device->LockRanges.erase(Resource);
    }

    Resource->Underlying.reset();
    Resource->SrgbSrv.reset();
    Resource->TopMipSrv[0].reset();
    Resource->TopMipSrv[1].reset();
    Resource->Srv.reset();
    Resource->SrgbRtvs.clear();
    Resource->Rtvs.clear();
    delete Resource;
}

HRESULT
APIENTRY
NineCreateResource2(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATERESOURCE2* Args)
{
    NineResource* Resource;
    HRESULT hr;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    /* The NULL format stands for a render target nothing is ever written to */
    if (Args->Format == NineFourCcFormat('N', 'U', 'L', 'L'))
    {
        Args->hResource = reinterpret_cast<HANDLE>(1);
        return S_OK;
    }

    Resource = new (std::nothrow) NineResource();
    if (Resource == nullptr)
        return E_OUTOFMEMORY;

    Resource->Device = static_cast<NineDevice*>(hDevice);
    Resource->RuntimeResource = Args->hResource;
    Resource->UniqueId = static_cast<UINT64>(InterlockedIncrement64(&NineResourceSerial));
    Resource->CpuDirty = TRUE;

    try
    {
        hr = NineInitResource(Resource, Args, nullptr);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    if (FAILED(hr))
    {
        NineTrace(L"resource format %u flags 0x%X is not created (0x%08lX)", Args->Format, Args->Flags.Value, hr);
        NineFreeResource(Resource);
        return NineDxgiResult(hr);
    }

    Args->hResource = Resource;
    return NineDxgiResult(hr);
}

HRESULT
APIENTRY
NineDestroyResource(
    _In_ HANDLE hDevice,
    _In_ HANDLE hResource)
{
    NineResource* Resource = hResource == reinterpret_cast<HANDLE>(1) ? nullptr : static_cast<NineResource*>(hResource);

    if (Resource == nullptr)
        return S_OK;
    if (hDevice == NULL)
        return E_INVALIDARG;

    NineFreeResource(Resource);
    return NineDxgiResult(S_OK);
}

/* Creation arguments for a resource another device made, described from what Direct3D 12 reports */
static
VOID
NineDescribeShared(
    _In_ ID3D12Device* Device,
    _In_ const TL::ResourceInfo* Info,
    _Out_ TL::ResourceCreationArgs* Create)
{
    D3D12_RESOURCE_DESC Desc = Info->Resource.m_ResourceDesc;
    const BOOLEAN Volume = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    const UINT ArraySize = Volume ? 1 : Desc.DepthOrArraySize;
    const UINT Depth = Volume ? Desc.DepthOrArraySize : 1;
    D3D12_RESOURCE_ALLOCATION_INFO Allocation;
    UINT Planes;
    UINT Bind = 0;

    /* Targets placed at the small alignment are asked for at the default one */
    if ((Desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) != 0 &&
        Desc.Alignment == D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT)
    {
        Desc.Alignment = 0;
    }

    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        Bind |= TL::RESOURCE_BIND_RENDER_TARGET;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0)
        Bind |= TL::RESOURCE_BIND_DEPTH_STENCIL;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) == 0)
        Bind |= TL::RESOURCE_BIND_SHADER_RESOURCE;

    Planes = CD3D11FormatHelper::NonOpaquePlaneCount(Desc.Format);
    Create->m_desc12 = Desc;
    Create->m_appDesc = TL::AppResourceDesc(ArraySize * Desc.MipLevels, static_cast<UINT8>(Planes),
                                            Planes * ArraySize * Desc.MipLevels, static_cast<UINT8>(Desc.MipLevels),
                                            static_cast<UINT16>(ArraySize), Depth, static_cast<UINT>(Desc.Width),
                                            Desc.Height, Desc.Format, Desc.SampleDesc.Count, Desc.SampleDesc.Quality,
                                            TL::RESOURCE_USAGE_DEFAULT, static_cast<TL::RESOURCE_CPU_ACCESS>(0),
                                            static_cast<TL::RESOURCE_BIND_FLAGS>(Bind), Desc.Dimension);

    Allocation = Device->GetResourceAllocationInfo(0, 1, &Desc);
    Create->m_heapDesc = {};
    Create->m_heapDesc.SizeInBytes = Allocation.SizeInBytes;
    Create->m_heapDesc.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    Create->m_heapDesc.Properties.CreationNodeMask = 1;
    Create->m_heapDesc.Properties.VisibleNodeMask = 1;
    Create->m_heapDesc.Alignment = Allocation.Alignment;
}

/* Takes over the resource behind a shared handle and describes it the way a created one would be */
static
HRESULT
NineOpenShared(
    _Inout_ NineResource* Resource,
    _In_ UINT Kmt,
    _In_reads_bytes_(TL::SharedResourceHelpers::cPrivateResourceDriverDataSize) VOID* Data,
    _In_ D3DDDIFORMAT Format)
{
    NineDevice* Device = Resource->Device;
    TL::ResourceCreationArgs Create = {};
    TL::ResourceInfo Info = {};
    D3DDDIARG_CREATERESOURCE2 Args = {};
    D3DDDI_SURFACEINFO Surface = {};
    TL::unique_comptr<TL::Resource> Opened;
    UINT ArraySize;

    Device->Sharing->QueryResourceInfoFromKMTHandle(Kmt, nullptr, &Info);
    NineDescribeShared(Device->Device12, &Info, &Create);

    /* The opened resource starts out in the common state, as a freshly shared one is */
    Opened = Device->Sharing->OpenResourceFromKmtHandle(Create, Kmt, Data,
                                                        TL::SharedResourceHelpers::cPrivateResourceDriverDataSize,
                                                        D3D12_RESOURCE_STATE_COMMON);

    const D3D12_RESOURCE_DESC& Desc = Opened->Parent()->m_desc12;
    const BOOLEAN Buffer = Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER;

    Surface.Width = static_cast<UINT>(Desc.Width);
    Surface.Height = Desc.Height;
    Surface.Depth = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? Desc.DepthOrArraySize : 1;
    ArraySize = Desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Desc.DepthOrArraySize;

    /* Buffers come back as index buffers and planar video formats are never plain textures */
    Args.Format = Format;
    Args.Pool = D3DDDIPOOL_LOCALVIDMEM;
    Args.pSurfList = &Surface;
    Args.SurfCount = Desc.MipLevels * ArraySize;
    Args.MipLevels = Desc.MipLevels;
    if (Buffer)
        Args.Flags.Value |= NineFlagIndexBuffer;
    else if (!CD3D11FormatHelper::YUV(Desc.Format))
        Args.Flags.Value |= NineFlagTexture;
    if ((Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
        Args.Flags.Value |= NineFlagRenderTarget;

    return NineInitResource(Resource, &Args, std::addressof(Opened));
}

HRESULT
APIENTRY
NineOpenResource(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_OPENRESOURCE* Args,
    _In_ const NineOpenResourceArgs* OpenArgs)
{
    NineResource* Resource;
    HRESULT hr;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;
    if (Args->PrivateDriverDataSize != TL::SharedResourceHelpers::cPrivateResourceDriverDataSize ||
        Args->pPrivateDriverData == nullptr || OpenArgs == nullptr)
    {
        return E_INVALIDARG;
    }

    Resource = new (std::nothrow) NineResource();
    if (Resource == nullptr)
        return NineDxgiResult(E_OUTOFMEMORY);

    Resource->Device = static_cast<NineDevice*>(hDevice);
    Resource->RuntimeResource = Args->hResource;
    Resource->UniqueId = static_cast<UINT64>(InterlockedIncrement64(&NineResourceSerial));
    Resource->CpuDirty = TRUE;

    try
    {
        hr = NineOpenShared(Resource, Args->hKMResource, Args->pPrivateDriverData, OpenArgs->Format);
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    if (FAILED(hr))
    {
        NineTrace(L"shared resource %u is not opened (0x%08lX)", Args->hKMResource, hr);
        NineFreeResource(Resource);
        return NineDxgiResult(hr);
    }

    Args->hResource = Resource;
    return NineDxgiResult(hr);
}

/* Maps a runtime subresource and plane to the translation layer's subresource */
UINT
NineSubresourceOf(
    _In_ const NineResource* Resource,
    _In_ UINT Index,
    _In_ UINT Plane,
    _In_ BOOLEAN Expanded)
{
    const UINT Mips = Resource->Typed.MipLevels;
    const UINT Slices = Resource->Typed.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1 : Resource->ArraySize;
    UINT Slice;

    /* The runtime counts autogen textures by their top level only */
    if (Resource->AutogenMips && !Expanded)
        Index *= Mips;

    Slice = static_cast<UINT8>((Index / Mips) % Slices);
    return Index % Mips + (Plane * Slices + Slice) * Mips;
}

/* The translation layer map type for a lock; a lock without CPU write access maps for read or write only */
static
TL::MAP_TYPE
NineMapType(
    _In_ UINT LockFlags,
    _In_ BOOLEAN DecodeBuffer,
    _In_ UINT CpuAccess)
{
    const BOOLEAN Discard = (LockFlags & NineLockDiscard) != 0;

    if (Discard && DecodeBuffer)
        return TL::MAP_TYPE_WRITE_DISCARD;

    if ((LockFlags & NineLockReadOnly) != 0 || (CpuAccess & TL::RESOURCE_CPU_ACCESS_WRITE) == 0)
        return (CpuAccess & TL::RESOURCE_CPU_ACCESS_READ) != 0 ? TL::MAP_TYPE_READ : TL::MAP_TYPE_WRITE;
    if ((CpuAccess & TL::RESOURCE_CPU_ACCESS_READ) != 0)
        return TL::MAP_TYPE_READWRITE;
    if (Discard)
        return TL::MAP_TYPE_WRITE_DISCARD;
    if ((LockFlags & NineLockNoOverwrite) != 0)
        return TL::MAP_TYPE_WRITE_NOOVERWRITE;
    return TL::MAP_TYPE_WRITE;
}

/* Whether a registry setting turns off merging adjacent discards; on unless set to zero */
static
BOOLEAN
NineLockDiscardMerging(VOID)
{
    static const BOOLEAN Enabled = []() -> BOOLEAN
    {
        HKEY Key;
        DWORD Value = 0;
        DWORD Size = sizeof(Value);
        LSTATUS Status;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Direct3D\\Direct3D9on12", 0, KEY_READ, &Key) !=
            ERROR_SUCCESS)
        {
            return TRUE;
        }
        Status = RegQueryValueExA(Key, "LockDiscardOptimization", NULL, NULL, reinterpret_cast<BYTE*>(&Value), &Size);
        RegCloseKey(Key);
        return Status != ERROR_SUCCESS || Value != 0;
    }();

    return Enabled;
}

/*
 * A discard that only touches bytes next to the ones discarded before keeps the buffer and writes
 * without overwrite; one that overlaps starts the history over.
 */
static
TL::MAP_TYPE
NineMergeDiscard(
    _Inout_ NineResource* Resource,
    _In_ NineLockRange Range)
{
    NineDevice* Device = Resource->Device;
    std::lock_guard<std::mutex> Guard(Device->LockRangesLock);
    const UINT End = Range.Offset + Range.Size;
    auto Found = Device->LockRanges.find(Resource);

    if (Found == Device->LockRanges.end())
    {
        Device->LockRanges.emplace(Resource, std::vector<NineLockRange>{ Range });
        return TL::MAP_TYPE_WRITE_DISCARD;
    }

    std::vector<NineLockRange>& Known = Found->second;
    for (NineLockRange& Old : Known)
    {
        const UINT OldEnd = Old.Offset + Old.Size;
        const BOOLEAN StartsInside = Range.Offset >= Old.Offset && Range.Offset < OldEnd;
        const BOOLEAN EndsInside = End >= Old.Offset && End < OldEnd;

        if (StartsInside || EndsInside)
        {
            Known.clear();
            Known.push_back(Range);
            return TL::MAP_TYPE_WRITE_DISCARD;
        }
        if (Range.Offset == OldEnd)
        {
            Old.Size += Range.Size;
            return TL::MAP_TYPE_WRITE_NOOVERWRITE;
        }
        if (End == Old.Offset)
        {
            Old.Offset = Range.Offset;
            Old.Size += Range.Size;
            return TL::MAP_TYPE_WRITE_NOOVERWRITE;
        }
    }

    Known.push_back(Range);
    return TL::MAP_TYPE_WRITE_NOOVERWRITE;
}

/* Locks one translation layer subresource; Region is the runtime's range, area or box as given */
static
HRESULT
NineLockSubresource(
    _Inout_ NineResource* Resource,
    _In_ UINT Sub,
    _In_ UINT LockFlags,
    _In_reads_(6) const UINT* Region,
    _Out_ VOID** Data,
    _Out_ UINT* Pitch,
    _Out_ UINT* SlicePitch,
    _In_ BOOLEAN Async,
    _Out_opt_ HANDLE* Cookie)
{
    NineDevice* Device = Resource->Device;
    TL::Resource* RenameCookie = nullptr;
    NineLockState* Lock;
    TL::MappedSubresource Mapped = {};
    TL::Resource* Underlying;
    const D3D12_BOX* Box = nullptr;
    TL::MAP_TYPE MapType;
    BOOLEAN DoNotWait;
    bool Done = false;

    if ((LockFlags & NineLockMightDraw) != 0 && (LockFlags & NineLockDiscard) != 0)
        NINE_TRACE_ONCE(L"a lock that discards and may draw from the locked data is treated as a plain discard");
    if ((LockFlags & NineLockNotifyOnly) != 0)
        return S_OK;

    /* Nested locks keep the flags of the first one */
    Lock = &Resource->Locks[Sub];
    if (Lock->Count != 0)
        LockFlags = Lock->Flags;
    else
        Lock->Flags = LockFlags;

    /* Asynchronous locks only rename or append to write only buffers */
    if (Async)
    {
        if ((LockFlags & (NineLockNoOverwrite | NineLockDiscard)) == 0 ||
            (Resource->CpuAccess & TL::RESOURCE_CPU_ACCESS_READ) != 0 ||
            Resource->Typed.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return E_NOTIMPL;
        }
        if ((LockFlags & NineLockDiscard) != 0)
        {
            if (Lock->Count == 0)
            {
                RenameCookie = Device->Context->CreateRenameCookie(Resource->Underlying.get(),
                                                                   TL::ResourceAllocationContext::FreeThread);
                *Cookie = RenameCookie;
                Resource->Renamed = RenameCookie->GetIdentity()->m_suballocation;
            }
        }
        else if (!Resource->Renamed.IsInitialized())
        {
            return E_NOTIMPL;
        }
    }

    if ((LockFlags & NineLockRegionMask) != 0)
    {
        D3D12_BOX& Kept = Lock->Box;

        if ((LockFlags & NineLockRangeValid) != 0)
        {
            Kept.left = Region[0];
            Kept.top = 0;
            Kept.front = 0;
            Kept.right = Region[0] + Region[1];
            Kept.bottom = 1;
            Kept.back = 1;
        }
        else if ((LockFlags & NineLockAreaValid) != 0)
        {
            Kept.left = Region[0];
            Kept.top = Region[1];
            Kept.front = static_cast<LONG>(Region[1]) < 0 ? ~0U : 0;
            Kept.right = Region[2];
            Kept.bottom = Region[3];
            Kept.back = 1;
        }
        else
        {
            Kept.left = Region[0];
            Kept.top = Region[1];
            Kept.front = Region[4];
            Kept.right = Region[2];
            Kept.bottom = Region[3];
            Kept.back = Region[5];
        }

        /* Clamped to the top level of the resource as a whole */
        Kept.left = (std::min)(Kept.left, static_cast<UINT>(Resource->Typed.Width));
        Kept.right = (std::min)(Kept.right, static_cast<UINT>(Resource->Typed.Width));
        Kept.top = (std::min)(Kept.top, Resource->Typed.Height);
        Kept.bottom = (std::min)(Kept.bottom, Resource->Typed.Height);
        Kept.front = (std::min)(Kept.front, static_cast<UINT>(Resource->Typed.DepthOrArraySize));
        Kept.back = (std::min)(Kept.back, static_cast<UINT>(Resource->Typed.DepthOrArraySize));
        Box = &Kept;
    }

    Underlying = Resource->Underlying.get();
    if (Underlying == nullptr)
    {
        /* The reference dereferences the missing resource here */
        NineTrace(L"system memory resource locked without notify only (flags 0x%X)", LockFlags);
        return E_INVALIDARG;
    }

    MapType = NineMapType(LockFlags, Resource->DecodeBuffer, Underlying->AppDesc()->CPUAccessFlags());
    DoNotWait = (LockFlags & NineLockDoNotWait) != 0;

    if (MapType == TL::MAP_TYPE_WRITE_DISCARD && NineLockDiscardMerging())
    {
        NineLockRange Range;

        if ((LockFlags & NineLockRangeValid) != 0)
        {
            Range.Offset = Region[0];
            Range.Size = Region[1];
        }
        else
        {
            Range.Offset = 0;
            Range.Size = static_cast<UINT>(Resource->TotalSize);
        }
        MapType = NineMergeDiscard(Resource, Range);
    }

    if (Async)
    {
        const D3D12_RANGE NothingRead = {};
        const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& Placement = Underlying->GetSubresourcePlacement(Sub);
        BYTE* Base = nullptr;
        UINT Slice = 0;

        Resource->Renamed.GetResource()->Map(0, &NothingRead, reinterpret_cast<VOID**>(&Base));
        if (Base != nullptr)
            Base += Resource->Renamed.GetOffset();
        *Data = Base + (Box != nullptr ? Box->left : 0);
        *Pitch = Placement.Footprint.RowPitch;
        CD3D11FormatHelper::CalculateMinimumRowMajorSlicePitch(Placement.Footprint.Format, Placement.Footprint.RowPitch,
                                                                Placement.Footprint.Height, Slice);
        *SlicePitch = Slice;
        Done = true;
    }
    else switch (Underlying->AppDesc()->Usage())
    {
        case TL::RESOURCE_USAGE_DEFAULT:
            Done = Device->Context->MapDefault(Underlying, Sub, MapType, DoNotWait, Box, &Mapped);
            break;
        case TL::RESOURCE_USAGE_DYNAMIC:
        {
            const BOOLEAN GpuOnly =
                Underlying->Parent()->m_heapDesc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;

            if (MapType == TL::MAP_TYPE_WRITE_NOOVERWRITE)
                Done = Device->Context->MapUnderlying(Underlying, Sub, MapType, Box, &Mapped);
            else if (GpuOnly)
                Done = Device->Context->MapDynamicTexture(Underlying, Sub, MapType, DoNotWait, Box, &Mapped);
            else if (MapType == TL::MAP_TYPE_WRITE_DISCARD)
                Done = Device->Context->MapDiscardBuffer(Underlying, Sub, MapType, DoNotWait, Box, &Mapped);
            else
                Done = Device->Context->MapUnderlyingSynchronize(Underlying, Sub, MapType, DoNotWait, Box, &Mapped);
            break;
        }
        case TL::RESOURCE_USAGE_STAGING:
            Done = Device->Context->MapUnderlyingSynchronize(Underlying, Sub, MapType, DoNotWait, Box, &Mapped);
            break;
        default:
            break;
    }

    if (!Done)
    {
        if (RenameCookie != nullptr)
            Device->Context->DeleteRenameCookie(RenameCookie);
        return D3DERR_WASSTILLDRAWING;
    }

    if (!Async)
    {
        *Data = Mapped.pData;
        *Pitch = Mapped.RowPitch;
        *SlicePitch = Mapped.DepthPitch;
    }
    if (MapType != TL::MAP_TYPE_READ)
    {
        Resource->GpuWrites = 0;
        Resource->CpuDirty = TRUE;
    }
    Lock->Count += 1;
    return S_OK;
}

static
VOID
NineUnlockSubresource(
    _Inout_ NineResource* Resource,
    _In_ UINT Sub,
    _In_ UINT UnlockFlags,
    _In_ BOOLEAN Async)
{
    NineDevice* Device = Resource->Device;
    NineLockState* Lock;
    TL::Resource* Underlying;
    const D3D12_BOX* Box;
    TL::MAP_TYPE MapType;

    if ((UnlockFlags & NineUnlockNotifyOnly) != 0)
        return;

    Lock = &Resource->Locks[Sub];
    Lock->Count -= 1;
    Box = (Lock->Flags & NineLockRegionMask) != 0 ? &Lock->Box : nullptr;

    /* A renamed buffer was written through its own mapping */
    if (Async && Resource->Renamed.IsInitialized())
    {
        D3D12_RANGE Written = {};

        if (Resource->Desc.Width != 0)
        {
            Written.Begin = Resource->Renamed.GetOffset();
            Written.End = Resource->Renamed.GetOffset() + Resource->Desc.Width;
        }
        Resource->Renamed.GetResource()->Unmap(0, &Written);
        return;
    }

    Underlying = Resource->Underlying.get();
    if (Underlying == nullptr)
        return;

    MapType = NineMapType(Lock->Flags, Resource->DecodeBuffer, Underlying->AppDesc()->CPUAccessFlags());
    switch (Underlying->AppDesc()->Usage())
    {
        case TL::RESOURCE_USAGE_DEFAULT:
            Device->Context->UnmapDefault(Underlying, Sub, Box);
            break;
        case TL::RESOURCE_USAGE_DYNAMIC:
            if (Underlying->Parent()->m_heapDesc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
                Device->Context->UnmapDynamicTexture(Underlying, Sub, Box, MapType != TL::MAP_TYPE_READ);
            else
                Device->Context->UnmapUnderlyingSimple(Underlying, Sub, Box);
            break;
        case TL::RESOURCE_USAGE_STAGING:
            Device->Context->UnmapUnderlyingStaging(Underlying, Sub, Box);
            break;
        default:
            break;
    }
}

static
NineResource*
NineResourceFrom(
    _In_opt_ HANDLE hResource)
{
    if (hResource == reinterpret_cast<HANDLE>(1))
        return nullptr;
    return static_cast<NineResource*>(hResource);
}

HRESULT
APIENTRY
NineLock(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_LOCK* Args)
{
    NineResource* Resource = NineResourceFrom(Args->hResource);
    VOID* Data[3] = {};
    UINT Pitch[3] = {};
    UINT SlicePitch[3] = {};
    UINT Region[6];
    HRESULT hr = S_OK;

    if (hDevice == NULL)
        return E_INVALIDARG;
    if (Resource == nullptr)
        return S_OK;

    CopyMemory(Region, &Args->Box, sizeof(Region));
    try
    {
        for (UINT Plane = 0; Plane < Resource->PlaneCount && SUCCEEDED(hr); Plane += 1)
        {
            UINT Sub = NineSubresourceOf(Resource, Args->SubResourceIndex, Plane, FALSE);

            hr = NineLockSubresource(Resource, Sub, Args->Flags.Value, Region, &Data[Plane], &Pitch[Plane],
                                     &SlicePitch[Plane], FALSE, nullptr);
        }
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    Args->pSurfData = Data[0];
    Args->Pitch = Pitch[0];
    Args->SlicePitch = SlicePitch[0];
    return NineDxgiResult(hr);
}

HRESULT
APIENTRY
NineUnlock(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_UNLOCK* Args)
{
    NineResource* Resource = NineResourceFrom(Args->hResource);

    if (hDevice == NULL)
        return E_INVALIDARG;
    if (Resource == nullptr)
        return S_OK;
    if (Resource->Device != hDevice)
        return E_INVALIDARG;

    try
    {
        for (UINT Plane = 0; Plane < Resource->PlaneCount; Plane += 1)
            NineUnlockSubresource(Resource, NineSubresourceOf(Resource, Args->SubResourceIndex, Plane, FALSE),
                                  Args->Flags.Value, FALSE);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}

/* Asynchronous lock flags in the bit order of the regular lock flags; asynchronous locks always write */
static
UINT
NineAsyncLockFlags(
    _In_ UINT Flags)
{
    UINT Lock = (Flags & 0x1F) << 2;

    if ((Flags & 0x40) != 0)
        Lock |= NineLockNotifyOnly;
    return Lock | 0x2;
}

HRESULT
APIENTRY
NineLockAsync(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_LOCKASYNC* Args)
{
    NineResource* Resource;
    HANDLE Cookie = nullptr;
    UINT Region[6];
    HRESULT hr = S_OK;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    Resource = NineResourceFrom(Args->hResource);
    if (Resource == nullptr)
    {
        NineTrace(L"asynchronous lock of a null resource");
        return E_INVALIDARG;
    }

    CopyMemory(Region, &Args->Box, sizeof(Region));
    try
    {
        for (UINT Plane = 0; Plane < Resource->PlaneCount && SUCCEEDED(hr); Plane += 1)
        {
            UINT Sub = NineSubresourceOf(Resource, Args->SubResourceIndex, Plane, FALSE);

            hr = NineLockSubresource(Resource, Sub, NineAsyncLockFlags(Args->Flags.Value), Region, &Args->pSurfData,
                                     &Args->Pitch, &Args->SlicePitch, TRUE,
                                     Resource->PlaneCount > 1 ? nullptr : &Cookie);
        }
    }
    catch (_com_error& Error)
    {
        hr = Error.Error();
    }
    catch (std::bad_alloc&)
    {
        hr = E_OUTOFMEMORY;
    }

    Args->hCookie = Cookie;
    return NineDxgiResult(hr);
}

HRESULT
APIENTRY
NineUnlockAsync(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_UNLOCKASYNC* Args)
{
    NineResource* Resource;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    Resource = NineResourceFrom(Args->hResource);
    if (Resource == nullptr)
    {
        NineTrace(L"asynchronous unlock of a null resource");
        return E_INVALIDARG;
    }

    try
    {
        for (UINT Plane = 0; Plane < Resource->PlaneCount; Plane += 1)
            NineUnlockSubresource(Resource, NineSubresourceOf(Resource, Args->SubResourceIndex, Plane, FALSE),
                                  Args->Flags.Value & NineUnlockNotifyOnly, TRUE);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}

TL::TSRV*
NineShaderViewOf(
    _Inout_ NineResource* Resource,
    _In_ UINT Kind)
{
    std::unique_ptr<TL::TSRV>* Slot;

    if (Resource->Srv == nullptr)
        return nullptr;
    if (Resource->SrgbSrv == nullptr)
        Kind &= ~NineViewSrgb;
    if (Resource->Typed.MipLevels == 1)
        Kind &= ~NineViewTopMip;

    switch (Kind)
    {
        case 0:
            return Resource->Srv.get();
        case NineViewSrgb:
            return Resource->SrgbSrv.get();
        default:
            Slot = &Resource->TopMipSrv[Kind & NineViewSrgb];
            if (*Slot == nullptr)
                Slot->reset(NineMakeShaderView(Resource, Kind));
            return Slot->get();
    }
}

HRESULT
APIENTRY
NineRename(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_RENAME* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Resource;
    TL::Resource* Cookie;

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    Resource = Args->hResource == reinterpret_cast<HANDLE>(1) ? nullptr : static_cast<NineResource*>(Args->hResource);
    Cookie = static_cast<TL::Resource*>(Args->hCookie);
    try
    {
        Device->Context->Rename(Resource->Underlying.get(), Cookie);
        Device->Context->DeleteRenameCookie(Cookie);
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    return NineDxgiResult(S_OK);
}
