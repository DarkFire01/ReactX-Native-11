/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     2D, cube and volume textures, their levels and the managed pool behind them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Usage bits each kind of texture accepts; 2D textures also take two the runtime keeps for itself */
static constexpr DWORD D9PlanarUsageAllowed = 0x50807E13;
static constexpr DWORD D9TextureUsageAllowed = 0x10807E13;
static constexpr DWORD D9VolumeUsageAllowed = 0x00803610;

/* The only usage a 2D scratch texture may carry */
static constexpr DWORD D9ScratchUsageAllowed = 0x40000000;

/* Usage that makes a texture whose levels all keep the top level's size, for video decoding */
static constexpr DWORD D9UsageVideoArray = 0x00400000;

/* Usage that only textures made to be shared may carry */
static constexpr DWORD D9TextureUsageShared = D3DUSAGE_NONSECURE | D3DUSAGE_RESTRICT_SHARED_RESOURCE |
                                              D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER;

/* Usage the format check hears about */
static constexpr DWORD D9TextureUsageChecked = D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_DYNAMIC |
                                               D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_DMAP;

/* Pools past scratch that video array textures fold into the default pool */
static constexpr D3DPOOL D9PoolVideoFirst = static_cast<D3DPOOL>(4);
static constexpr D3DPOOL D9PoolVideoLast = static_cast<D3DPOOL>(5);

/* The managed pool as the extended object spells it */
static constexpr D3DPOOL D9PoolManagedEx = static_cast<D3DPOOL>(6);

/* A whole texture's memory stays within two gigabytes */
static constexpr UINT64 D9TextureMemoryLimit = 0x80000000ULL;

/* D3D9 allows no more mip levels than this */
static constexpr UINT D9MaxTextureLevels = 32;

/* D3DFMT_A1 textures have size limits of their own */
static constexpr UINT D9A1MaxWidth = 8192;
static constexpr UINT D9A1MaxHeight = 2048;

/* A face whose dirty regions overflowed, or that was never uploaded, copies whole */
static constexpr UINT D9DirtyWhole = D9DirtyRegionLimit + 1;

static
BOOLEAN
D9IsPowerOfTwo(
    _In_ UINT Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

/* Levels a full mip chain down to one texel has */
static
UINT
D9FullChain(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Depth)
{
    UINT Largest = Width > Height ? Width : Height;
    UINT Count = 0;

    if (Depth > Largest)
        Largest = Depth;
    while (Largest != 0)
    {
        Count += 1;
        Largest >>= 1;
    }
    return Count;
}

/* Whether a chain of this many levels shrinks every edge to nothing before its last level */
static
BOOLEAN
D9TooManyLevels(
    _In_ UINT Levels,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Depth)
{
    UINT Shift = Levels - 1;

    return Levels > 1 && (Width >> Shift) == 0 && (Height >> Shift) == 0 && (Depth >> Shift) == 0;
}

/* The caps bit a texture filter needs for generating mip levels */
static
DWORD
D9FilterCap(
    _In_ D3DTEXTUREFILTERTYPE Filter)
{
    switch (Filter)
    {
    case D3DTEXF_POINT:
        return D3DPTFILTERCAPS_MINFPOINT;
    case D3DTEXF_LINEAR:
        return D3DPTFILTERCAPS_MINFLINEAR;
    case D3DTEXF_ANISOTROPIC:
        return D3DPTFILTERCAPS_MINFANISOTROPIC;
    case D3DTEXF_PYRAMIDALQUAD:
        return D3DPTFILTERCAPS_MINFPYRAMIDALQUAD;
    case D3DTEXF_GAUSSIANQUAD:
        return D3DPTFILTERCAPS_MINFGAUSSIANQUAD;
    default:
        return 0;
    }
}

/* The filter caps that apply to a kind of texture */
static
DWORD
D9KindFilterCaps(
    _In_ const D3DCAPS9* Caps,
    _In_ D9TextureKind Kind)
{
    switch (Kind)
    {
    case D9TextureKind::Cube:
        return Caps->CubeTextureFilterCaps;
    case D9TextureKind::Volume:
        return Caps->VolumeTextureFilterCaps;
    default:
        return Caps->TextureFilterCaps;
    }
}

/* Checks the parts of a request the three kinds share and settles whether the texture can be locked */
static
HRESULT
D9CheckTextureCommon(
    _In_ const D9TextureRequest* Request,
    _In_ BOOLEAN IsPlanar)
{
    D3DPOOL Pool = Request->Pool;
    DWORD Usage = Request->Usage;

    if (Request->Levels != 1 || (Usage & D3DUSAGE_AUTOGENMIPMAP) != 0)
    {
        if (Request->Format == D3DFMT_MULTI2_ARGB8)
        {
            D9_TRACE_ONCE(L"textures of several elements cannot have mip levels");
            return D3DERR_INVALIDCALL;
        }
        if ((Usage & D3DUSAGE_AUTOGENMIPMAP) != 0 && Pool == D3DPOOL_SYSTEMMEM)
            return D3DERR_INVALIDCALL;
    }

    if (Pool > D3DPOOL_SCRATCH)
    {
        D9_TRACE_ONCE(L"pool %u is not valid for a texture", Pool);
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_SCRATCH && (Usage & (IsPlanar ? ~D9ScratchUsageAllowed : ~0UL)) != 0)
    {
        D9_TRACE_ONCE(L"scratch textures take no usage");
        return D3DERR_INVALIDCALL;
    }
    if ((IsPlanar || Pool != D3DPOOL_SCRATCH) &&
        (Usage & ~(IsPlanar ? D9PlanarUsageAllowed : D9TextureUsageAllowed)) != 0)
    {
        D9_TRACE_ONCE(L"invalid texture usage 0x%08lX", Usage);
        return D3DERR_INVALIDCALL;
    }
    if ((Usage & D3DUSAGE_DYNAMIC) != 0 && Pool == D3DPOOL_MANAGED)
        return D3DERR_INVALIDCALL;

    if ((Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0)
    {
        if (Pool != D3DPOOL_DEFAULT)
            return D3DERR_INVALIDCALL;
    }
    else if (Pool != D3DPOOL_DEFAULT)
    {
        if (D9PixelStride(Request->Format) == 0 || D9IsHiddenDepth(Request->Format))
        {
            D9_TRACE_ONCE(L"format %u cannot live in runtime memory", Request->Format);
            return D3DERR_INVALIDCALL;
        }
    }
    return S_OK;
}

/* Default pool textures can only be locked when dynamic or of a format the runtime does not know the size of */
static
BOOLEAN
D9TextureLockable(
    _In_ const D9TextureRequest* Request)
{
    if (Request->Pool == D3DPOOL_SCRATCH)
        return TRUE;
    if (Request->Pool != D3DPOOL_DEFAULT && !D9IsHiddenDepth(Request->Format))
        return TRUE;
    return D9BytesPerPixel(Request->Format) == 0 || (Request->Usage & D3DUSAGE_DYNAMIC) != 0;
}

/* Every check CreateTexture, CreateCubeTexture and CreateVolumeTexture make, in the order the system makes them */
HRESULT
D9Device::CheckTextureRequest(
    _Inout_ D9TextureRequest* Request,
    _In_opt_ HANDLE* SharedHandle)
{
    static const D3DRESOURCETYPE Types[] = { D3DRTYPE_TEXTURE, D3DRTYPE_CUBETEXTURE, D3DRTYPE_VOLUMETEXTURE };
    BOOLEAN IsPlanar = Request->Kind == D9TextureKind::Planar;
    BOOLEAN IsVolume = Request->Kind == D9TextureKind::Volume;
    BOOLEAN VideoArray;
    BOOLEAN SkipLevelChecks = FALSE;
    D3DDISPLAYMODE Mode = {};
    HRESULT Format = S_OK;
    DWORD TextureCaps = Caps.TextureCaps;
    DWORD Mipped;
    HRESULT hr;

    if (SharedHandle != nullptr)
    {
        if (DeviceType != D3DDEVTYPE_NULLREF && ((Caps.Caps2 & D3DCAPS2_CANSHARERESOURCE) == 0 || !Enum->IsExtended()))
        {
            D9_TRACE_ONCE(L"this device cannot share textures");
            return D3DERR_INVALIDCALL;
        }
        if (Request->Pool != D3DPOOL_DEFAULT && (!IsPlanar || Request->Pool != D3DPOOL_SYSTEMMEM))
            return D3DERR_INVALIDCALL;
        if (Request->Pool == D3DPOOL_SYSTEMMEM && (*SharedHandle == NULL || Request->Levels != 1))
            return D3DERR_INVALIDCALL;
    }

    VideoArray = IsPlanar && (Request->Usage & D9UsageVideoArray) != 0;
    if (VideoArray && (Request->Pool == D9PoolVideoFirst || Request->Pool == D9PoolVideoLast))
        Request->Pool = D3DPOOL_DEFAULT;

    hr = D9CheckTextureCommon(Request, IsPlanar);
    if (FAILED(hr))
        return hr;
    if (Request->Pool != D3DPOOL_SCRATCH)
    {
        Enum->GetAdapterDisplayMode(Adapter->Ordinal, &Mode);
        Format = Enum->CheckDeviceFormat(Adapter->Ordinal, DeviceType, Mode.Format,
                                         Request->Usage & D9TextureUsageChecked,
                                         Types[static_cast<UINT>(Request->Kind)], Request->Format);
        if (FAILED(Format))
        {
            D9_TRACE_ONCE(L"format %u cannot make this texture", Request->Format);
            return D3DERR_INVALIDCALL;
        }
    }
    if (IsVolume && (Request->Usage & ~D9VolumeUsageAllowed) != 0)
        return D3DERR_INVALIDCALL;
    if ((Request->Usage & D9TextureUsageShared) != 0 && (SharedHandle == nullptr || *SharedHandle != NULL))
        return D3DERR_INVALIDCALL;

    Request->Lockable = D9TextureLockable(Request);
    Request->DriverAutogen = (Request->Usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
    if ((Request->Usage & D3DUSAGE_AUTOGENMIPMAP) != 0)
    {
        if (VideoArray || Request->Levels > 1)
            return D3DERR_INVALIDCALL;
        if ((Caps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP) == 0 || Format == D3DOK_NOAUTOGEN)
            Request->DriverAutogen = FALSE;
    }

    Mipped = IsPlanar ? D3DPTEXTURECAPS_MIPMAP : IsVolume ? D3DPTEXTURECAPS_MIPVOLUMEMAP : D3DPTEXTURECAPS_MIPCUBEMAP;
    if (Request->Levels == 0)
    {
        if ((Request->Pool != D3DPOOL_SCRATCH && (TextureCaps & Mipped) == 0) ||
            (Request->Usage & D3DUSAGE_AUTOGENMIPMAP) != 0 || (IsPlanar && Request->Format == D3DFMT_A1))
        {
            Request->Levels = 1;
            SkipLevelChecks = TRUE;
        }
        else
        {
            Request->Levels = D9FullChain(Request->Width, IsPlanar || IsVolume ? Request->Height : 0,
                                          IsVolume ? Request->Depth : 0);
        }
    }
    if (!SkipLevelChecks)
    {
        if (!VideoArray && D9TooManyLevels(Request->Levels, Request->Width, Request->Height, Request->Depth))
        {
            D9_TRACE_ONCE(L"%u levels are too many for a %ux%u texture", Request->Levels, Request->Width,
                          Request->Height);
            return D3DERR_INVALIDCALL;
        }
        if (Request->Levels > D9MaxTextureLevels)
            return D3DERR_INVALIDCALL;
    }

    Request->DriverFormat = Request->Format;
    if (Request->Pool != D3DPOOL_SCRATCH)
    {
        if (IsVolume)
        {
            if ((TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP_POW2) != 0 &&
                (!D9IsPowerOfTwo(Request->Width) || !D9IsPowerOfTwo(Request->Height) ||
                 !D9IsPowerOfTwo(Request->Depth)))
            {
                return D3DERR_INVALIDCALL;
            }
            if (Request->Width > Caps.MaxVolumeExtent || Request->Height > Caps.MaxVolumeExtent ||
                Request->Depth > Caps.MaxVolumeExtent)
            {
                return D3DERR_INVALIDCALL;
            }
            if ((TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) == 0)
                return D3DERR_INVALIDCALL;
        }
        else if (!IsPlanar)
        {
            if ((TextureCaps & D3DPTEXTURECAPS_CUBEMAP_POW2) != 0 && !D9IsPowerOfTwo(Request->Width))
                return D3DERR_INVALIDCALL;
            if (Request->Width > Caps.MaxTextureWidth || Request->Width > Caps.MaxTextureHeight)
                return D3DERR_INVALIDCALL;
            if ((TextureCaps & D3DPTEXTURECAPS_CUBEMAP) == 0)
                return D3DERR_INVALIDCALL;
        }
        else
        {
            if (Request->Levels > 1 && !VideoArray && (TextureCaps & D3DPTEXTURECAPS_MIPMAP) == 0)
                return D3DERR_INVALIDCALL;
            const UINT Edges[] = { Request->Width, Request->Height };

            for (UINT Edge : Edges)
            {
                if (D9IsPowerOfTwo(Edge) || (TextureCaps & D3DPTEXTURECAPS_POW2) == 0)
                    continue;
                if ((TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == 0 || Request->Levels > 1)
                    return D3DERR_INVALIDCALL;
            }
            if (Request->Width != Request->Height && (TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) != 0)
                return D3DERR_INVALIDCALL;
            if (Request->Format == D3DFMT_A1)
            {
                if (Request->Width > D9A1MaxWidth || Request->Height > D9A1MaxHeight ||
                    (Request->Width % 32) != 0 || Request->Levels != 1)
                {
                    return D3DERR_INVALIDCALL;
                }
            }
            else if (Request->Width > Caps.MaxTextureWidth || Request->Height > Caps.MaxTextureHeight)
            {
                D9_TRACE_ONCE(L"a %ux%u texture is larger than the device allows", Request->Width, Request->Height);
                return D3DERR_INVALIDCALL;
            }
        }
        if (!IsPlanar && Request->Levels > 1 && (TextureCaps & Mipped) == 0)
            return D3DERR_INVALIDCALL;
        if (!IsVolume && !(IsPlanar && Request->Format == D3DFMT_A1))
            Request->DriverFormat = D9DriverDepthFormat(Adapter, Request->Format);
        if (VideoArray && Request->Pool != D3DPOOL_DEFAULT)
            return D3DERR_INVALIDCALL;
    }

    if (D9IsDxtFormat(Request->Format) && ((Request->Width % 4) != 0 || (Request->Height % 4) != 0))
    {
        D9_TRACE_ONCE(L"block compressed textures need edges that are multiples of four");
        return D3DERR_INVALIDCALL;
    }
    if (Request->Width == 0 || Request->Height == 0 || Request->Depth == 0)
        return D3DERR_INVALIDCALL;
    if ((Request->Usage & D3DUSAGE_RESTRICTED_CONTENT) != 0)
        return CheckProtectedContent();
    return S_OK;
}

D9TextureCore::D9TextureCore(
    _In_ D9Device* Owner,
    _In_ const D9TextureRequest* Request)
    : D9Child(Owner),
      Kind(Request->Kind),
      Width(Request->Width),
      Height(Request->Height),
      Depth(Request->Depth),
      Levels(Request->Levels),
      Faces(Request->Kind == D9TextureKind::Cube ? 6 : 1),
      Usage(Request->Usage),
      Format(Request->Format),
      DriverFormat(Request->DriverFormat),
      Pool(Request->Pool),
      ReportedPool(Request->ReportedPool),
      Lockable(Request->Lockable),
      DriverAutogen(Request->DriverAutogen),
      Sharing(Request->Share),
      UserMemory(Request->UserMemory != nullptr)
{
    DWORD FilterCaps = D9KindFilterCaps(&Owner->DeviceCaps(), Kind);

    Filter = (FilterCaps & D3DPTFILTERCAPS_MINFLINEAR) != 0 ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    Memory = static_cast<BYTE*>(Request->UserMemory);
    for (UINT& Count : DirtyCount)
        Count = D9DirtyWhole;
    if (Pool == D3DPOOL_DEFAULT)
        Owner->NoteDefaultResource(TRUE);
}

D9TextureCore::~D9TextureCore()
{
    if (Pool == D3DPOOL_DEFAULT)
        Device->NoteDefaultResource(FALSE);
    DestroyLevelObjects();
    if (IsManaged())
        Device->ForgetManaged(this);
    if (Resource != nullptr)
    {
        Device->DestroyDriverSurface(Resource);
        delete Resource;
    }
    if (VideoResource != nullptr)
    {
        Device->DestroyDriverSurface(VideoResource);
        delete VideoResource;
    }
    delete[] Subresources;
    if (Memory != nullptr && !UserMemory)
        _aligned_free(Memory);
}

VOID
D9TextureCore::DestroyLevelObjects(VOID)
{
    if (LevelObjects == nullptr)
        return;

    for (UINT Index = 0; Index < Faces * Levels; Index++)
    {
        if (LevelObjects[Index] == nullptr)
            continue;
        if (Kind == D9TextureKind::Volume)
            delete static_cast<D9Volume*>(static_cast<IDirect3DVolume9*>(LevelObjects[Index]));
        else
            delete static_cast<D9Surface*>(static_cast<IDirect3DSurface9*>(LevelObjects[Index]));
    }
    delete[] LevelObjects;
    LevelObjects = nullptr;
}

/* Lays out every level and face, places memory textures in runtime memory and asks the driver for the resource */
HRESULT
D9TextureCore::Create(VOID)
{
    BOOLEAN InMemory = Pool != D3DPOOL_DEFAULT;
    BOOLEAN SameSize = (Usage & D9UsageVideoArray) != 0;
    UINT Count = Faces * Levels;
    D3DDDI_SURFACEINFO* Surfaces;
    UINT64 Total = 0;
    HRESULT hr;

    Subresources = new (std::nothrow) D9Subresource[Count]();
    LevelObjects = new (std::nothrow) IUnknown*[Count]();
    if (Subresources == nullptr || LevelObjects == nullptr)
        return E_OUTOFMEMORY;

    for (UINT Face = 0; Face < Faces; Face++)
    {
        for (UINT Level = 0; Level < Levels; Level++)
        {
            D9Subresource* Entry = &Subresources[SubresourceIndex(Face, Level)];
            UINT Shift = SameSize ? 0 : Level;
            UINT64 Pitch;
            UINT64 Slice;

            Entry->Width = (Width >> Shift) != 0 ? Width >> Shift : 1;
            Entry->Height = (Height >> Shift) != 0 ? Height >> Shift : 1;
            Entry->Depth = (Depth >> Shift) != 0 ? Depth >> Shift : 1;
            Pitch = D9MemoryPitch(Format, Entry->Width);
            Slice = Pitch * D9MemoryRows(Format, Entry->Height);
            if (Pitch > D9TextureMemoryLimit || Slice > D9TextureMemoryLimit)
                return D3DERR_INVALIDCALL;
            Entry->Pitch = static_cast<UINT>(Pitch);
            Entry->SlicePitch = static_cast<UINT>(Slice);
            Entry->Offset = static_cast<SIZE_T>(Total);
            Total += Slice * Entry->Depth;
        }
    }
    if (Total > D9TextureMemoryLimit)
    {
        D9_TRACE_ONCE(L"a texture of more than two gigabytes was refused");
        return D3DERR_INVALIDCALL;
    }

    if (InMemory && !UserMemory)
    {
        Memory = static_cast<BYTE*>(_aligned_malloc(static_cast<SIZE_T>(Total), 32));
        if (Memory == nullptr)
            return E_OUTOFMEMORY;
        ZeroMemory(Memory, static_cast<SIZE_T>(Total));
        if (Pool == D3DPOOL_SCRATCH)
            return S_OK;
    }

    Resource = new (std::nothrow) D9Resource();
    Surfaces = new (std::nothrow) D3DDDI_SURFACEINFO[Count]();
    if (Resource == nullptr || Surfaces == nullptr)
    {
        delete[] Surfaces;
        return E_OUTOFMEMORY;
    }

    Resource->Format = static_cast<D3DDDIFORMAT>(DriverFormat);
    Resource->Pool = InMemory ? D3DDDIPOOL_SYSTEMMEM : D3DDDIPOOL_VIDEOMEMORY;
    Resource->Flags.Texture = TRUE;
    Resource->Flags.CubeMap = Kind == D9TextureKind::Cube;
    Resource->Flags.Volume = Kind == D9TextureKind::Volume;
    Resource->Flags.RenderTarget = (Usage & D3DUSAGE_RENDERTARGET) != 0;
    Resource->Flags.ZBuffer = (Usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    Resource->Flags.Dynamic = (Usage & D3DUSAGE_DYNAMIC) != 0;
    Resource->Flags.TextApi = (Usage & D3DUSAGE_TEXTAPI) != 0;
    Resource->Flags.AutogenMipmap = DriverAutogen && !InMemory;
    Resource->Flags.NotLockable = !Lockable;
    Resource->Flags.Video = SameSize;
    Resource->Flags2.UserMemory = UserMemory;
    D9ApplyShare(Resource, &Sharing, Kind == D9TextureKind::Cube ? D3DRTYPE_CUBETEXTURE :
                                     Kind == D9TextureKind::Volume ? D3DRTYPE_VOLUMETEXTURE : D3DRTYPE_TEXTURE);
    Resource->LevelCount = Levels;
    Resource->Depth = Kind == D9TextureKind::Volume ? Depth : 0;

    /* Cube maps list each face's levels before the next face, as the driver expects */
    for (UINT Index = 0; Index < Count; Index++)
    {
        Surfaces[Index].Width = Subresources[Index].Width;
        Surfaces[Index].Height = Subresources[Index].Height;
        Surfaces[Index].Depth = Kind == D9TextureKind::Volume ? Subresources[Index].Depth : 0;
        if (Memory != nullptr)
        {
            Surfaces[Index].pSysMem = Memory + Subresources[Index].Offset;
            Surfaces[Index].SysMemPitch = Subresources[Index].Pitch;
            if (Kind == D9TextureKind::Volume)
                Surfaces[Index].SysMemSlicePitch = Subresources[Index].SlicePitch;
        }
    }
    hr = Device->CreateTextureResource(Surfaces, Count, Levels, Resource);
    delete[] Surfaces;
    if (FAILED(hr))
    {
        delete Resource;
        Resource = nullptr;
        return hr;
    }
    if (IsManaged())
        Device->TrackManaged(this);
    return S_OK;
}

ULONG
D9TextureCore::AddRefTexture(VOID)
{
    LONG Count = InterlockedIncrement(&RefCount);

    if (Count == 1)
    {
        Orphaned = FALSE;
        Device->AddRef();
    }
    return static_cast<ULONG>(Count);
}

/* The device outlives the texture it destroys; a bound texture waits for its stage to let go */
ULONG
D9TextureCore::ReleaseTexture(VOID)
{
    D9Device* Owner = Device;
    LONG Count = InterlockedDecrement(&RefCount);

    if (Count != 0)
        return static_cast<ULONG>(Count);

    if (BindCount > 0)
    {
        Orphaned = TRUE;
        Owner->Release();
        return 0;
    }
    delete this;
    Owner->Release();
    return 0;
}

VOID
D9TextureCore::Bind(VOID)
{
    BindCount += 1;
}

VOID
D9TextureCore::Unbind(VOID)
{
    BindCount -= 1;
    if (BindCount == 0 && Orphaned)
        delete this;
}

/* Hands out the persistent surface or volume of one level */
HRESULT
D9TextureCore::GetLevelSurface(
    _In_ UINT Face,
    _In_ UINT Level,
    _Outptr_ IUnknown** Result)
{
    UINT Index = SubresourceIndex(Face, Level);
    const D9Subresource* Entry = &Subresources[Index];

    if (LevelObjects[Index] == nullptr)
    {
        if (Kind == D9TextureKind::Volume)
        {
            D3DVOLUME_DESC Desc = {};

            Desc.Format = Format;
            Desc.Type = D3DRTYPE_VOLUME;
            Desc.Usage = Usage;
            Desc.Pool = ReportedPool;
            Desc.Width = Entry->Width;
            Desc.Height = Entry->Height;
            Desc.Depth = Entry->Depth;
            LevelObjects[Index] = new (std::nothrow) D9Volume(this, Index, &Desc);
        }
        else
        {
            D3DSURFACE_DESC Desc = {};

            DescribeLevel(Level, &Desc);
            LevelObjects[Index] = static_cast<IDirect3DSurface9*>(new (std::nothrow) D9Surface(this, Index, &Desc));
        }
        if (LevelObjects[Index] == nullptr)
            return E_OUTOFMEMORY;
    }

    AddRefTexture();
    *Result = LevelObjects[Index];
    return S_OK;
}

HRESULT
D9TextureCore::DescribeLevel(
    _In_ UINT Level,
    _Out_ D3DSURFACE_DESC* Desc) const
{
    if (Level >= Levels || Desc == nullptr)
        return D3DERR_INVALIDCALL;

    ZeroMemory(Desc, sizeof(*Desc));
    Desc->Format = Format;
    Desc->Type = D3DRTYPE_SURFACE;
    Desc->Usage = Usage;
    Desc->Pool = ReportedPool;
    Desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    Desc->Width = Subresources[Level].Width;
    Desc->Height = Subresources[Level].Height;
    return S_OK;
}

/* Where a box starts inside a level kept in memory; the products are signed, as the system computes them */
static
SSIZE_T
D9MemoryOffset(
    _In_ D3DFORMAT Format,
    _In_ const D9Subresource* Entry,
    _In_ const D3DBOX* Box)
{
    LONG Stride = D9PixelStride(Format);
    INT Left = static_cast<INT>(Box->Left);
    INT Top = static_cast<INT>(Box->Top);
    INT Row;
    INT Column;

    if (Stride < 0)
    {
        Row = (Top / 4) * static_cast<INT>(Entry->Pitch);
        Column = (Left / 4) * -Stride;
    }
    else if (Format == D3DFMT_A1)
    {
        Row = Top * static_cast<INT>(Entry->Pitch);
        Column = Left >> 3;
    }
    else
    {
        Row = Top * static_cast<INT>(Entry->Pitch);
        Column = Left * Stride;
    }
    return static_cast<SSIZE_T>(Row) + Column +
           static_cast<SSIZE_T>(static_cast<INT>(Box->Front) * static_cast<INT>(Entry->SlicePitch));
}

/* Memory textures lock without checking the box; the driver only hears of it for its system memory copy */
HRESULT
D9TextureCore::LockMemorySubresource(
    _In_ UINT Index,
    _In_opt_ const D3DBOX* Box,
    _In_ DWORD Flags,
    _Out_ D3DLOCKED_BOX* Locked)
{
    D9Subresource* Entry = &Subresources[Index];
    D3DLOCKED_BOX Driver = {};
    HRESULT hr;

    if (Entry->Locked)
        return D3DERR_INVALIDCALL;
    if (Resource != nullptr)
    {
        hr = Device->LockDriverSubresource(Resource, 0, nullptr, FALSE, Flags, &Driver, &Entry->LockedAsync);
        if (FAILED(hr))
            return hr;
    }

    Locked->pBits = Memory + Entry->Offset;
    if (Box != nullptr)
        Locked->pBits = static_cast<BYTE*>(Locked->pBits) + D9MemoryOffset(Format, Entry, Box);
    Locked->RowPitch = static_cast<INT>(Entry->Pitch);
    Locked->SlicePitch = static_cast<INT>(Entry->SlicePitch);
    Entry->Locked = TRUE;
    return S_OK;
}

/* A box inside a volume level that keeps the block layout of its format */
static
BOOLEAN
D9IsValidBox(
    _In_ D3DFORMAT Format,
    _In_ const D9Subresource* Entry,
    _In_ const D3DBOX* Box)
{
    RECT Area = { static_cast<LONG>(Box->Left), static_cast<LONG>(Box->Top), static_cast<LONG>(Box->Right),
                  static_cast<LONG>(Box->Bottom) };

    if (Box->Front >= Box->Back || Box->Back > Entry->Depth)
        return FALSE;
    return D9IsValidRect(Format, Entry->Width, Entry->Height, &Area);
}

HRESULT
D9TextureCore::LockSubresource(
    _In_ UINT Index,
    _In_opt_ const D3DBOX* Box,
    _In_ DWORD Flags,
    _In_ BOOLEAN FromLevel,
    _Out_ D3DLOCKED_BOX* Locked)
{
    D9Subresource* Entry = &Subresources[Index];
    BOOLEAN IsVolume = Kind == D9TextureKind::Volume;
    RECT Area;
    HRESULT hr;

    UNREFERENCED_PARAMETER(FromLevel);

    ZeroMemory(Locked, sizeof(*Locked));
    if (DcHeld)
    {
        D9_TRACE_ONCE(L"a texture lends a level to a device context and cannot lock");
        return D3DERR_INVALIDCALL;
    }
    if (Memory != nullptr)
    {
        hr = LockMemorySubresource(Index, Box, Flags, Locked);
    }
    else
    {
        if (Box != nullptr)
        {
            Area = { static_cast<LONG>(Box->Left), static_cast<LONG>(Box->Top), static_cast<LONG>(Box->Right),
                     static_cast<LONG>(Box->Bottom) };
            if (IsVolume ? !D9IsValidBox(Format, Entry, Box) : !D9IsValidRect(Format, Entry->Width, Entry->Height,
                                                                              &Area))
            {
                D9_TRACE_ONCE(L"a texture lock names a region outside its level");
                return D3DERR_INVALIDCALL;
            }
        }
        if (!D9LockFlagsAllowed(Flags))
            return D3DERR_INVALIDCALL;
        if (!Lockable || Entry->Locked)
        {
            D9_TRACE_ONCE(L"a texture level cannot be locked now");
            return D3DERR_INVALIDCALL;
        }
        hr = Device->LockDriverSubresource(Resource, Index, Box, IsVolume, Flags, Locked, &Entry->LockedAsync);
        if (SUCCEEDED(hr))
            Entry->Locked = TRUE;
    }
    if (FAILED(hr))
        return hr;

    /* Only a writing lock of a face's top level marks it dirty */
    if ((Index % Levels) == 0 && (Flags & (D3DLOCK_READONLY | D3DLOCK_NO_DIRTY_UPDATE)) == 0)
        AddDirtyRegion(Index / Levels, Box);
    return S_OK;
}

/* Unlocking a level that is not locked succeeds quietly, except through a driver level's surface or a volume */
HRESULT
D9TextureCore::UnlockSubresource(
    _In_ UINT Index,
    _In_ BOOLEAN FromLevel)
{
    D9Subresource* Entry = &Subresources[Index];
    HRESULT hr = S_OK;

    if (!Entry->Locked)
        return FromLevel && (Memory == nullptr || Kind == D9TextureKind::Volume) ? D3DERR_INVALIDCALL : S_OK;

    if (Memory != nullptr)
    {
        if (Resource != nullptr)
            hr = Device->UnlockDriverSubresource(Resource, 0, Entry->LockedAsync);
    }
    else if (Lockable)
    {
        hr = Device->UnlockDriverSubresource(Resource, Index, Entry->LockedAsync);
    }
    if (SUCCEEDED(hr))
        Entry->Locked = FALSE;
    return hr;
}

/* Remembers what part of a face's top level changed; too many regions, or the whole level, dirty it all */
HRESULT
D9TextureCore::AddDirtyRegion(
    _In_ UINT Face,
    _In_opt_ const D3DBOX* Box)
{
    UINT* Count = &DirtyCount[Face];

    if (*Count == D9DirtyWhole)
        return S_OK;
    if (Box == nullptr || *Count == D9DirtyRegionLimit ||
        (Box->Left == 0 && Box->Top == 0 && Box->Right == Width && Box->Bottom == Height &&
         (Kind != D9TextureKind::Volume || (Box->Front == 0 && Box->Back == Depth))))
    {
        *Count = D9DirtyWhole;
        return S_OK;
    }
    DirtyRegions[Face][*Count] = *Box;
    *Count += 1;
    return S_OK;
}

DWORD
D9TextureCore::SetPriorityCore(
    _In_ DWORD Priority)
{
    return Device->ChangePriority(Resource, Pool, &ResourcePriority, Priority);
}

DWORD
D9TextureCore::GetPriorityCore(VOID) const
{
    return Device->ReadPriority(Pool, ResourcePriority);
}

/* Only managed textures have a level of detail; changing it drops the video copy */
DWORD
D9TextureCore::SetLodCore(
    _In_ DWORD NewLod)
{
    DWORD Previous = Lod;
    UINT Limit = Levels;

    if (!IsManaged())
        return 0;

    if ((Usage & D3DUSAGE_AUTOGENMIPMAP) != 0)
        Limit = D9FullChain(Width, Height, 0);
    if (NewLod >= Limit)
        NewLod = Limit - 1;
    Lod = NewLod;
    Evict();
    return Previous;
}

DWORD
D9TextureCore::GetLodCore(VOID) const
{
    return IsManaged() ? Lod : 0;
}

HRESULT
D9TextureCore::SetFilterCore(
    _In_ D3DTEXTUREFILTERTYPE NewFilter)
{
    DWORD Needed = D9FilterCap(NewFilter);

    if (Needed == 0 || (D9KindFilterCaps(&Device->DeviceCaps(), Kind) & Needed) == 0)
    {
        D9_TRACE_ONCE(L"filter %u cannot generate mip levels here", NewFilter);
        return D3DERR_INVALIDCALL;
    }
    Filter = NewFilter;
    return S_OK;
}

VOID
D9TextureCore::GenerateMipsCore(VOID)
{
    if (DriverAutogen && Pool == D3DPOOL_DEFAULT)
        Device->DriverGenerateMips(Resource, Filter);
}


BOOLEAN
D9TextureCore::IsAnySubresourceLocked() const
{
    if (DcHeld)
        return TRUE;
    for (UINT Index = 0; Index < Faces * Levels; Index++)
    {
        if (Subresources[Index].Locked)
            return TRUE;
    }
    return FALSE;
}

/* The system hands the driver any two textures of one type, whatever their sizes, levels and formats */
HRESULT
D9TextureCore::CopyDirtyInto(
    _In_ D9TextureCore* Target)
{
    D3DBOX Whole = { 0, 0, Width, Height, 0, Depth };

    if (Kind != Target->Kind)
        return D3DERR_INVALIDCALL;

    for (UINT Remaining = Faces; Remaining > 0; Remaining -= 1)
    {
        UINT Face = Remaining - 1;
        UINT Regions = DirtyCount[Face];

        for (UINT Region = 0; Region < (Regions == D9DirtyWhole ? 1 : Regions); Region++)
        {
            const D3DBOX* Box = Regions == D9DirtyWhole ? &Whole : &DirtyRegions[Face][Region];

            if (Kind == D9TextureKind::Volume)
                Device->DriverVolBlt(Target->Resource, Resource, Box);
            else
                Device->DriverTexBlt(Target->Resource, Resource, Face, Box);
        }
        DirtyCount[Face] = 0;
    }
    return S_OK;
}

STDMETHODIMP
D9Device::UpdateTexture(
    _In_opt_ IDirect3DBaseTexture9* SourceTexture,
    _In_opt_ IDirect3DBaseTexture9* DestinationTexture)
{
    D9_DEVICE_LOCK();

    D9TextureCore* Source;
    D9TextureCore* Target;
    BOOLEAN SourceAutogen;
    BOOLEAN TargetAutogen;

    if (NotReset || SourceTexture == nullptr || DestinationTexture == nullptr)
        return D3DERR_INVALIDCALL;
    Source = D9CoreOf(SourceTexture);
    Target = D9CoreOf(DestinationTexture);
    if (Source->Device9() != this || Target->Device9() != this)
        return D3DERR_INVALIDCALL;

    /* Generated levels only copy into generated levels, which take a single level source otherwise */
    SourceAutogen = (Source->UsageOf() & D3DUSAGE_AUTOGENMIPMAP) != 0;
    TargetAutogen = (Target->UsageOf() & D3DUSAGE_AUTOGENMIPMAP) != 0;
    if (SourceAutogen ? !TargetAutogen : (TargetAutogen && SourceTexture->GetLevelCount() != 1))
    {
        D9_TRACE_ONCE(L"UpdateTexture: generated levels do not match");
        return D3DERR_INVALIDCALL;
    }
    if (Source->PoolOf() != D3DPOOL_SYSTEMMEM || Target->PoolOf() != D3DPOOL_DEFAULT)
    {
        D9_TRACE_ONCE(L"UpdateTexture: copies go from system memory to the default pool");
        return D3DERR_INVALIDCALL;
    }
    return Source->CopyDirtyInto(Target);
}

VOID
D9TextureCore::Evict(VOID)
{
    if (VideoResource == nullptr)
        return;

    Device->DestroyDriverSurface(VideoResource);
    delete VideoResource;
    VideoResource = nullptr;
    for (UINT& Count : DirtyCount)
        Count = D9DirtyWhole;
}

/* Makes the video copy of a managed texture from its level of detail on, then copies what changed, last face first */
VOID
D9TextureCore::Upload(VOID)
{
    UINT Count = Levels - Lod;
    D3DDDI_SURFACEINFO* Surfaces;
    D3DBOX Whole = { 0, 0, Width, Height, 0, Depth };
    HRESULT hr;

    if (!IsManaged())
        return;

    if (VideoResource == nullptr)
    {
        VideoResource = new (std::nothrow) D9Resource();
        Surfaces = new (std::nothrow) D3DDDI_SURFACEINFO[Faces * Count]();
        if (VideoResource == nullptr || Surfaces == nullptr)
        {
            delete VideoResource;
            VideoResource = nullptr;
            delete[] Surfaces;
            return;
        }
        for (UINT Face = 0; Face < Faces; Face++)
        {
            for (UINT Level = 0; Level < Count; Level++)
            {
                const D9Subresource* Entry = &Subresources[SubresourceIndex(Face, Lod + Level)];
                D3DDDI_SURFACEINFO* Surface = &Surfaces[Face * Count + Level];

                Surface->Width = Entry->Width;
                Surface->Height = Entry->Height;
                Surface->Depth = Kind == D9TextureKind::Volume ? Entry->Depth : 0;
            }
        }
        VideoResource->Format = static_cast<D3DDDIFORMAT>(DriverFormat);
        VideoResource->Pool = D3DDDIPOOL_VIDEOMEMORY;
        VideoResource->Flags.Value = Resource->Flags.Value;
        VideoResource->Flags.NotLockable = TRUE;
        VideoResource->Flags.AutogenMipmap = DriverAutogen;
        hr = Device->CreateTextureResource(Surfaces, Faces * Count, Count, VideoResource);
        delete[] Surfaces;
        if (FAILED(hr))
        {
            D9Trace(L"the video copy of a managed texture could not be made (0x%08lX)", hr);
            delete VideoResource;
            VideoResource = nullptr;
            return;
        }
        for (UINT& Dirty : DirtyCount)
            Dirty = D9DirtyWhole;
    }

    for (UINT Remaining = Faces; Remaining > 0; Remaining -= 1)
    {
        UINT Face = Remaining - 1;
        UINT Regions = DirtyCount[Face];

        for (UINT Region = 0; Region < (Regions == D9DirtyWhole ? 1 : Regions); Region++)
        {
            const D3DBOX* Box = Regions == D9DirtyWhole ? &Whole : &DirtyRegions[Face][Region];

            if (Kind == D9TextureKind::Volume)
                Device->DriverVolBlt(VideoResource, Resource, Box);
            else
                Device->DriverTexBlt(VideoResource, Resource, Face, Box);
        }
        DirtyCount[Face] = 0;
    }
}

D9Resource*
D9TextureCore::DrawResource(
    _Out_ BOOLEAN* Made)
{
    BOOLEAN Missing = VideoResource == nullptr;

    *Made = FALSE;
    if (!IsManaged())
        return Resource;
    Upload();
    *Made = Missing && VideoResource != nullptr;
    return VideoResource;
}

/* What the three texture interfaces share: identity, reference counts, priority, LOD and mip generation */
template <class Interface>
class D9TextureObject : public Interface, public D9TextureCore
{
public:
    D9TextureObject(
        _In_ D9Device* Owner,
        _In_ const D9TextureRequest* Request)
        : D9TextureCore(Owner, Request)
    {
    }

    IUnknown*
    Identity() override
    {
        return static_cast<Interface*>(this);
    }

    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override
    {
        if (Object == nullptr)
            return E_POINTER;

        *Object = nullptr;
        if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DResource9)) ||
            IsEqualGUID(Iid, __uuidof(IDirect3DBaseTexture9)) || IsEqualGUID(Iid, __uuidof(Interface)))
        {
            *Object = static_cast<Interface*>(this);
            AddRefTexture();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHOD_(ULONG, AddRef)() override
    {
        return AddRefTexture();
    }

    STDMETHOD_(ULONG, Release)() override
    {
        return ReleaseTexture();
    }

    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override
    {
        if (Result == nullptr)
            return D3DERR_INVALIDCALL;

        *Result = Device;
        Device->AddRef();
        return S_OK;
    }

    STDMETHOD(SetPrivateData)(
        _In_ REFGUID Guid,
        _In_reads_bytes_opt_(Size) const VOID* Data,
        _In_ DWORD Size,
        _In_ DWORD Flags) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return Private.Set(Guid, Data, Size, Flags);
    }

    STDMETHOD(GetPrivateData)(
        _In_ REFGUID Guid,
        _Out_writes_bytes_opt_(*Size) VOID* Data,
        _Inout_ DWORD* Size) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return Private.Get(Guid, Data, Size);
    }

    STDMETHOD(FreePrivateData)(
        _In_ REFGUID Guid) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return Private.Free(Guid);
    }

    STDMETHOD_(DWORD, SetPriority)(
        _In_ DWORD Priority) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return SetPriorityCore(Priority);
    }

    STDMETHOD_(DWORD, GetPriority)() override
    {
        return GetPriorityCore();
    }

    STDMETHOD_(VOID, PreLoad)() override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        Upload();
    }

    STDMETHOD_(DWORD, SetLOD)(
        _In_ DWORD NewLod) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return SetLodCore(NewLod);
    }

    STDMETHOD_(DWORD, GetLOD)() override
    {
        return GetLodCore();
    }

    STDMETHOD_(DWORD, GetLevelCount)() override
    {
        return Levels;
    }

    STDMETHOD(SetAutoGenFilterType)(
        _In_ D3DTEXTUREFILTERTYPE NewFilter) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return SetFilterCore(NewFilter);
    }

    STDMETHOD_(D3DTEXTUREFILTERTYPE, GetAutoGenFilterType)() override
    {
        return Filter;
    }

    STDMETHOD_(VOID, GenerateMipSubLevels)() override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        GenerateMipsCore();
    }
};

class D9Texture final : public D9TextureObject<IDirect3DTexture9>
{
public:
    using D9TextureObject::D9TextureObject;

    STDMETHOD_(D3DRESOURCETYPE, GetType)() override
    {
        return D3DRTYPE_TEXTURE;
    }

    STDMETHOD(GetLevelDesc)(
        _In_ UINT Level,
        _Out_ D3DSURFACE_DESC* Desc) override
    {
        return DescribeLevel(Level, Desc);
    }

    STDMETHOD(GetSurfaceLevel)(
        _In_ UINT Level,
        _Outptr_ IDirect3DSurface9** Surface) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
        IUnknown* Made = nullptr;
        HRESULT hr;

        if (Level >= Levels)
        {
            *Surface = nullptr;
            return D3DERR_INVALIDCALL;
        }
        hr = GetLevelSurface(0, Level, &Made);
        *Surface = static_cast<IDirect3DSurface9*>(Made);
        return hr;
    }

    STDMETHOD(LockRect)(
        _In_ UINT Level,
        _Out_ D3DLOCKED_RECT* Locked,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return LockLevel(0, Level, Locked, Rect, Flags);
    }

    STDMETHOD(UnlockRect)(
        _In_ UINT Level) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        if (Level >= Levels)
            return D3DERR_INVALIDCALL;
        return UnlockSubresource(Level, FALSE);
    }

    STDMETHOD(AddDirtyRect)(
        _In_opt_ const RECT* Rect) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        return AddDirtyFaceRect(0, Rect);
    }

    /* Shared with the cube map, which names a face as well */
    HRESULT
    LockLevel(
        _In_ UINT Face,
        _In_ UINT Level,
        _Out_ D3DLOCKED_RECT* Locked,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags)
    {
        D3DLOCKED_BOX Box = {};
        D3DBOX Area = {};
        HRESULT hr;

        if (Level >= Levels || Locked == nullptr)
            return D3DERR_INVALIDCALL;
        if (Rect != nullptr)
        {
            Area = { static_cast<UINT>(Rect->left), static_cast<UINT>(Rect->top), static_cast<UINT>(Rect->right),
                     static_cast<UINT>(Rect->bottom), 0, 1 };
        }
        hr = LockSubresource(SubresourceIndex(Face, Level), Rect != nullptr ? &Area : nullptr, Flags, FALSE, &Box);
        Locked->pBits = Box.pBits;
        Locked->Pitch = Box.RowPitch;
        return hr;
    }

    HRESULT
    AddDirtyFaceRect(
        _In_ UINT Face,
        _In_opt_ const RECT* Rect)
    {
        D3DBOX Area;

        if (Rect == nullptr)
            return AddDirtyRegion(Face, nullptr);
        if (!D9IsValidRect(Format, Width, Height, Rect))
            return D3DERR_INVALIDCALL;
        Area = { static_cast<UINT>(Rect->left), static_cast<UINT>(Rect->top), static_cast<UINT>(Rect->right),
                 static_cast<UINT>(Rect->bottom), 0, 1 };
        return AddDirtyRegion(Face, &Area);
    }
};

class D9CubeTexture final : public D9TextureObject<IDirect3DCubeTexture9>
{
public:
    using D9TextureObject::D9TextureObject;

    STDMETHOD_(D3DRESOURCETYPE, GetType)() override
    {
        return D3DRTYPE_CUBETEXTURE;
    }

    STDMETHOD(GetLevelDesc)(
        _In_ UINT Level,
        _Out_ D3DSURFACE_DESC* Desc) override
    {
        return DescribeLevel(Level, Desc);
    }

    STDMETHOD(GetCubeMapSurface)(
        _In_ D3DCUBEMAP_FACES Face,
        _In_ UINT Level,
        _Outptr_ IDirect3DSurface9** Surface) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
        IUnknown* Made = nullptr;
        HRESULT hr;

        if (static_cast<UINT>(Face) >= Faces || Level >= Levels)
        {
            *Surface = nullptr;
            return D3DERR_INVALIDCALL;
        }
        hr = GetLevelSurface(static_cast<UINT>(Face), Level, &Made);
        *Surface = static_cast<IDirect3DSurface9*>(Made);
        return hr;
    }

    STDMETHOD(LockRect)(
        _In_ D3DCUBEMAP_FACES Face,
        _In_ UINT Level,
        _Out_ D3DLOCKED_RECT* Locked,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
        D3DLOCKED_BOX Box = {};
        D3DBOX Area = {};
        HRESULT hr;

        if (static_cast<UINT>(Face) >= Faces || Level >= Levels || Locked == nullptr)
            return D3DERR_INVALIDCALL;
        if (Rect != nullptr)
        {
            Area = { static_cast<UINT>(Rect->left), static_cast<UINT>(Rect->top), static_cast<UINT>(Rect->right),
                     static_cast<UINT>(Rect->bottom), 0, 1 };
        }
        hr = LockSubresource(SubresourceIndex(static_cast<UINT>(Face), Level), Rect != nullptr ? &Area : nullptr,
                             Flags, FALSE, &Box);
        Locked->pBits = Box.pBits;
        Locked->Pitch = Box.RowPitch;
        return hr;
    }

    STDMETHOD(UnlockRect)(
        _In_ D3DCUBEMAP_FACES Face,
        _In_ UINT Level) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        if (static_cast<UINT>(Face) >= Faces || Level >= Levels)
            return D3DERR_INVALIDCALL;
        return UnlockSubresource(SubresourceIndex(static_cast<UINT>(Face), Level), FALSE);
    }

    STDMETHOD(AddDirtyRect)(
        _In_ D3DCUBEMAP_FACES Face,
        _In_opt_ const RECT* Rect) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
        D3DBOX Area;

        if (static_cast<UINT>(Face) >= Faces)
            return D3DERR_INVALIDCALL;
        if (Rect == nullptr)
            return AddDirtyRegion(static_cast<UINT>(Face), nullptr);
        if (!D9IsValidRect(Format, Width, Height, Rect))
            return D3DERR_INVALIDCALL;
        Area = { static_cast<UINT>(Rect->left), static_cast<UINT>(Rect->top), static_cast<UINT>(Rect->right),
                 static_cast<UINT>(Rect->bottom), 0, 1 };
        return AddDirtyRegion(static_cast<UINT>(Face), &Area);
    }
};

class D9VolumeTexture final : public D9TextureObject<IDirect3DVolumeTexture9>
{
public:
    using D9TextureObject::D9TextureObject;

    STDMETHOD_(D3DRESOURCETYPE, GetType)() override
    {
        return D3DRTYPE_VOLUMETEXTURE;
    }

    STDMETHOD(GetLevelDesc)(
        _In_ UINT Level,
        _Out_ D3DVOLUME_DESC* Desc) override
    {
        if (Level >= Levels || Desc == nullptr)
            return D3DERR_INVALIDCALL;

        ZeroMemory(Desc, sizeof(*Desc));
        Desc->Format = Format;
        Desc->Type = D3DRTYPE_VOLUME;
        Desc->Usage = Usage;
        Desc->Pool = ReportedPool;
        Desc->Width = Subresources[Level].Width;
        Desc->Height = Subresources[Level].Height;
        Desc->Depth = Subresources[Level].Depth;
        return S_OK;
    }

    STDMETHOD(GetVolumeLevel)(
        _In_ UINT Level,
        _Outptr_ IDirect3DVolume9** Volume) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());
        IUnknown* Made = nullptr;
        HRESULT hr;

        if (Level >= Levels)
        {
            *Volume = nullptr;
            return D3DERR_INVALIDCALL;
        }
        hr = GetLevelSurface(0, Level, &Made);
        *Volume = static_cast<IDirect3DVolume9*>(Made);
        return hr;
    }

    STDMETHOD(LockBox)(
        _In_ UINT Level,
        _Out_ D3DLOCKED_BOX* Locked,
        _In_opt_ const D3DBOX* Box,
        _In_ DWORD Flags) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        if (Level >= Levels || Locked == nullptr)
            return D3DERR_INVALIDCALL;
        return LockSubresource(Level, Box, Flags, FALSE, Locked);
    }

    STDMETHOD(UnlockBox)(
        _In_ UINT Level) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        if (Level >= Levels)
            return D3DERR_INVALIDCALL;
        return UnlockSubresource(Level, FALSE);
    }

    STDMETHOD(AddDirtyBox)(
        _In_opt_ const D3DBOX* Box) override
    {
        D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

        if (Box != nullptr && !D9IsValidBox(Format, &Subresources[0], Box))
            return D3DERR_INVALIDCALL;
        return AddDirtyRegion(0, Box);
    }
};

D9Volume::D9Volume(
    _In_ D9TextureCore* Texture,
    _In_ UINT Subresource,
    _In_ const D3DVOLUME_DESC* Desc)
    : Texture(Texture),
      Subresource(Subresource),
      Desc(*Desc)
{
}

STDMETHODIMP
D9Volume::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DVolume9)))
    {
        *Object = static_cast<IDirect3DVolume9*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9Volume::AddRef()
{
    return Texture->AddRefTexture();
}

STDMETHODIMP_(ULONG)
D9Volume::Release()
{
    return Texture->ReleaseTexture();
}

STDMETHODIMP
D9Volume::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Texture->Device9();
    (*Result)->AddRef();
    return S_OK;
}

STDMETHODIMP
D9Volume::SetPrivateData(
    _In_ REFGUID Guid,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    D9Device* Device = Texture->Device9();
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Set(Guid, Data, Size, Flags);
}

STDMETHODIMP
D9Volume::GetPrivateData(
    _In_ REFGUID Guid,
    _Out_writes_bytes_opt_(*Size) VOID* Data,
    _Inout_ DWORD* Size)
{
    D9Device* Device = Texture->Device9();
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Get(Guid, Data, Size);
}

STDMETHODIMP
D9Volume::FreePrivateData(
    _In_ REFGUID Guid)
{
    D9Device* Device = Texture->Device9();
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Private.Free(Guid);
}

STDMETHODIMP
D9Volume::GetContainer(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = nullptr;
    return Texture->Identity()->QueryInterface(Iid, Result);
}

STDMETHODIMP
D9Volume::GetDesc(
    _Out_ D3DVOLUME_DESC* Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Desc;
    return S_OK;
}

STDMETHODIMP
D9Volume::LockBox(
    _Out_ D3DLOCKED_BOX* Locked,
    _In_opt_ const D3DBOX* Box,
    _In_ DWORD Flags)
{
    D9Device* Device = Texture->Device9();
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    if (Locked == nullptr)
        return D3DERR_INVALIDCALL;
    return Texture->LockSubresource(Subresource, Box, Flags, TRUE, Locked);
}

STDMETHODIMP
D9Volume::UnlockBox()
{
    D9Device* Device = Texture->Device9();
    D9DeviceLock Scope(Device->LockSection(), Device->IsMultithreaded());

    return Texture->UnlockSubresource(Subresource, TRUE);
}

/* Makes the texture object of a checked request */
template <class Object>
static
HRESULT
D9MakeTexture(
    _In_ D9Device* Device,
    _In_ const D9TextureRequest* Request,
    _Outptr_ Object** Made)
{
    Object* Texture = new (std::nothrow) Object(Device, Request);
    HRESULT hr;

    if (Texture == nullptr)
        return E_OUTOFMEMORY;

    hr = Texture->Create();
    if (FAILED(hr))
    {
        D9Trace(L"a texture of format %u could not be made (0x%08lX)", Request->Format, hr);
        delete Texture;
        return hr;
    }
    Texture->AddRef();
    *Made = Texture;
    return S_OK;
}

/* A handle on a system memory texture names the memory of its only level; on others it shares */
static
VOID
D9StartTextureShare(
    _Inout_ D9TextureRequest* Request,
    _In_opt_ const HANDLE* SharedHandle)
{
    if (SharedHandle != nullptr && Request->Pool == D3DPOOL_SYSTEMMEM)
    {
        Request->UserMemory = *SharedHandle;
        D9StartShare(nullptr, 0, &Request->Share);
        return;
    }
    D9StartShare(SharedHandle, Request->Usage | D9ShareUsageModern | (Request->Lockable ? D9ShareUsageLockable : 0),
                 &Request->Share);
}

/* Hands back the handle a texture made to share got from the kernel */
static
VOID
D9FinishTextureShare(
    _In_ const D9TextureRequest* Request,
    _In_ const D9TextureCore* Texture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    if (Request->Share.Mode == D9ShareMode::Create)
        *SharedHandle = reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(Texture->DriverResource()->ShareHandle));
}

/* Fills the request every creation call starts from; the extended object's managed pool keeps its spelling */
static
VOID
D9StartRequest(
    _Out_ D9TextureRequest* Request,
    _In_ D9TextureKind Kind,
    _In_ D3DPOOL Pool,
    _In_ BOOLEAN ExtendedObject)
{
    ZeroMemory(Request, sizeof(*Request));
    Request->Kind = Kind;
    Request->Pool = Pool == D9PoolManagedEx ? D3DPOOL_MANAGED : Pool;
    Request->ReportedPool = Request->Pool == D3DPOOL_MANAGED && ExtendedObject ? D9PoolManagedEx : Request->Pool;
}

STDMETHODIMP
D9Device::CreateTexture(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DTexture9** Texture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    D9TextureRequest Request;
    D9Texture* Made = nullptr;
    HRESULT hr;

    D9_DEVICE_LOCK();

    if (Format == D3DFMT_UNKNOWN)
    {
        *Texture = nullptr;
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_MANAGED && Enum->IsExtended())
    {
        D9_TRACE_ONCE(L"CreateTexture: the extended object has no managed pool");
        return D3DERR_INVALIDCALL;
    }

    D9StartRequest(&Request, D9TextureKind::Planar, Pool, Enum->IsExtended());
    Request.Width = Width;
    Request.Height = Height;
    Request.Depth = 1;
    Request.Levels = Levels;
    Request.Usage = Usage;
    Request.Format = Format;
    hr = CheckTextureRequest(&Request, SharedHandle);
    if (SUCCEEDED(hr))
    {
        D9StartTextureShare(&Request, SharedHandle);
        if (Request.Share.Mode != D9ShareMode::None &&
            (Request.Usage & D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER) != 0)
        {
            hr = CheckDriverRestriction();
        }
    }
    if (SUCCEEDED(hr))
        hr = D9MakeTexture(this, &Request, &Made);
    if (SUCCEEDED(hr))
        D9FinishTextureShare(&Request, Made, SharedHandle);
    *Texture = SUCCEEDED(hr) ? Made : nullptr;
    return hr;
}

STDMETHODIMP
D9Device::CreateCubeTexture(
    _In_ UINT EdgeLength,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DCubeTexture9** CubeTexture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    D9TextureRequest Request;
    D9CubeTexture* Made = nullptr;
    HRESULT hr;

    D9_DEVICE_LOCK();

    if (Format == D3DFMT_UNKNOWN)
    {
        *CubeTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_MANAGED && Enum->IsExtended())
    {
        D9_TRACE_ONCE(L"CreateCubeTexture: the extended object has no managed pool");
        return D3DERR_INVALIDCALL;
    }

    D9StartRequest(&Request, D9TextureKind::Cube, Pool, Enum->IsExtended());
    Request.Width = EdgeLength;
    Request.Height = EdgeLength;
    Request.Depth = 1;
    Request.Levels = Levels;
    Request.Usage = Usage;
    Request.Format = Format;
    hr = CheckTextureRequest(&Request, SharedHandle);
    if (SUCCEEDED(hr))
    {
        D9StartTextureShare(&Request, SharedHandle);
        if (Request.Share.Mode != D9ShareMode::None &&
            (Request.Usage & D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER) != 0)
        {
            hr = CheckDriverRestriction();
        }
    }
    if (SUCCEEDED(hr))
        hr = D9MakeTexture(this, &Request, &Made);
    if (SUCCEEDED(hr))
        D9FinishTextureShare(&Request, Made, SharedHandle);
    *CubeTexture = SUCCEEDED(hr) ? Made : nullptr;
    return hr;
}

STDMETHODIMP
D9Device::CreateVolumeTexture(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT Depth,
    _In_ UINT Levels,
    _In_ DWORD Usage,
    _In_ D3DFORMAT Format,
    _In_ D3DPOOL Pool,
    _Out_ IDirect3DVolumeTexture9** VolumeTexture,
    _Inout_opt_ HANDLE* SharedHandle)
{
    D9TextureRequest Request;
    D9VolumeTexture* Made = nullptr;
    HRESULT hr;

    D9_DEVICE_LOCK();

    if (Format == D3DFMT_UNKNOWN)
    {
        *VolumeTexture = nullptr;
        return D3DERR_INVALIDCALL;
    }
    if (Pool == D3DPOOL_MANAGED && Enum->IsExtended())
    {
        D9_TRACE_ONCE(L"CreateVolumeTexture: the extended object has no managed pool");
        return D3DERR_INVALIDCALL;
    }

    D9StartRequest(&Request, D9TextureKind::Volume, Pool, Enum->IsExtended());
    Request.Width = Width;
    Request.Height = Height;
    Request.Depth = Depth;
    Request.Levels = Levels;
    Request.Usage = Usage;
    Request.Format = Format;
    hr = CheckTextureRequest(&Request, SharedHandle);
    if (SUCCEEDED(hr))
    {
        D9StartTextureShare(&Request, SharedHandle);
        if (Request.Share.Mode != D9ShareMode::None &&
            (Request.Usage & D3DUSAGE_RESTRICT_SHARED_RESOURCE_DRIVER) != 0)
        {
            hr = CheckDriverRestriction();
        }
    }
    if (SUCCEEDED(hr))
        hr = D9MakeTexture(this, &Request, &Made);
    if (SUCCEEDED(hr))
        D9FinishTextureShare(&Request, Made, SharedHandle);
    *VolumeTexture = SUCCEEDED(hr) ? Made : nullptr;
    return hr;
}

/* The texture object behind an interface this runtime handed out */
D9TextureCore*
D9CoreOf(
    _In_ IDirect3DBaseTexture9* Texture)
{
    switch (Texture->GetType())
    {
    case D3DRTYPE_CUBETEXTURE:
        return static_cast<D9CubeTexture*>(static_cast<IDirect3DCubeTexture9*>(Texture));
    case D3DRTYPE_VOLUMETEXTURE:
        return static_cast<D9VolumeTexture*>(static_cast<IDirect3DVolumeTexture9*>(Texture));
    default:
        return static_cast<D9Texture*>(static_cast<IDirect3DTexture9*>(Texture));
    }
}

/* Pixel samplers come first, then the displacement map sampler and the vertex samplers; samplers 16 to 20 name
   the same slots as the displacement map and vertex samplers */
static
BOOLEAN
D9TextureSlot(
    _In_ DWORD Stage,
    _Out_ UINT* Slot)
{
    *Slot = Stage >= D3DDMAPSAMPLER ? Stage - (D3DDMAPSAMPLER - 16) : Stage;
    return *Slot < D9TextureSlotCount;
}

STDMETHODIMP
D9Device::SetTexture(
    _In_ DWORD Stage,
    _In_opt_ IDirect3DBaseTexture9* Texture)
{
    IDirect3DBaseTexture9* Previous;
    UINT Slot;

    D9_DEVICE_LOCK();

    if (!D9TextureSlot(Stage, &Slot))
        return D3DERR_INVALIDCALL;
    if (Recording != nullptr)
        return Recording->Add(D9BlockOp::Texture, Stage, 0, &Texture, sizeof(Texture));

    Previous = Textures[Slot];
    if (Previous == Texture)
        return S_OK;
    if (Texture != nullptr)
        D9CoreOf(Texture)->Bind();
    Textures[Slot] = Texture;

    /* A scratch texture never reaches the driver, so binding one changes nothing it sees */
    if (Texture == nullptr || D9CoreOf(Texture)->PoolOf() != D3DPOOL_SCRATCH)
    {
        TextureDirty |= 1u << Slot;
        DrawDirty |= D9DrawTextures;
    }
    if (Previous != nullptr)
        D9CoreOf(Previous)->Unbind();
    return S_OK;
}

STDMETHODIMP
D9Device::GetTexture(
    _In_ DWORD Stage,
    _Out_ IDirect3DBaseTexture9** Texture)
{
    UINT Slot;

    D9_DEVICE_LOCK();

    if (Texture == nullptr)
        return D3DERR_INVALIDCALL;
    if (!D9TextureSlot(Stage, &Slot))
    {
        *Texture = nullptr;
        return D3DERR_INVALIDCALL;
    }
    *Texture = Textures[Slot];
    if (*Texture != nullptr)
        (*Texture)->AddRef();
    return S_OK;
}

/* A state block holds a texture as a binding would */
VOID
D9HoldTexture(
    _In_ IDirect3DBaseTexture9* Texture,
    _In_ BOOLEAN Hold)
{
    if (Hold)
        D9CoreOf(Texture)->Bind();
    else
        D9CoreOf(Texture)->Unbind();
}

/* Lets go of every bound texture, destroying those the application already released */
VOID
D9Device::UnbindTextures(VOID)
{
    for (IDirect3DBaseTexture9*& Bound : Textures)
    {
        IDirect3DBaseTexture9* Texture = Bound;

        if (Texture == nullptr)
            continue;
        Bound = nullptr;
        D9CoreOf(Texture)->Unbind();
    }
}

VOID
D9Device::TrackManaged(
    _In_ D9ManagedResource* Resource)
{
    Resource->NextManaged = ManagedResources;
    ManagedResources = Resource;
}

VOID
D9Device::ForgetManaged(
    _In_ D9ManagedResource* Resource)
{
    for (D9ManagedResource** Link = &ManagedResources; *Link != nullptr; Link = &(*Link)->NextManaged)
    {
        if (*Link != Resource)
            continue;
        *Link = Resource->NextManaged;
        return;
    }
}

STDMETHODIMP
D9Device::EvictManagedResources()
{
    D9_DEVICE_LOCK();

    for (D9ManagedResource* Resource = ManagedResources; Resource != nullptr; Resource = Resource->NextManaged)
        Resource->Evict();
    return S_OK;
}
