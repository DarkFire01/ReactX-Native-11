/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectDrawSurface through IDirectDrawSurface7
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

extern const VOID* const DdSurfaceVtbl1[];
extern const VOID* const DdSurfaceVtbl2[];
extern const VOID* const DdSurfaceVtbl3[];
extern const VOID* const DdSurfaceVtbl4[];
extern const VOID* const DdSurfaceVtbl7[];

/* Caps a surface may ask for that need a Direct3D runtime 64 bit Windows does not have */
static constexpr DWORD DdCapsNeedingDirect3D = DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_MIPMAP;

/* Surfaces wider or taller than this are refused */
static constexpr DWORD DdMaxSurfaceSide = 65535;

static constexpr DWORD DdDescFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT;

static
VOID
DdDropSurfaceDc(
    _Inout_ DdSurface* Surface);

static
VOID
DdDestroySurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version);

static const VOID*
DdSurfaceVtblFor(
    _In_ UCHAR Version)
{
    switch (Version)
    {
    case 1:
        return DdSurfaceVtbl1;
    case 2:
        return DdSurfaceVtbl2;
    case 3:
        return DdSurfaceVtbl3;
    case 4:
        return DdSurfaceVtbl4;
    default:
        return DdSurfaceVtbl7;
    }
}

static
DdSurfaceInterface*
DdInterfaceFromCom(
    _In_ VOID* Com)
{
    return static_cast<DdSurfaceInterface*>(Com);
}

_Ret_maybenull_
DdSurface*
DdSurfaceFromCom(
    _In_opt_ VOID* Com)
{
    DdSurfaceInterface* Interface = static_cast<DdSurfaceInterface*>(Com);

    if (Interface == nullptr || Interface->Surface == nullptr)
        return nullptr;
    return Interface->Surface;
}

BOOLEAN
DdSurfaceBusy(
    _In_ const DdSurface* Surface)
{
    return Surface->Locked || Surface->Dc != NULL;
}

_Ret_maybenull_
static
DdSurfaceInterface*
DdSurfaceInterfaceFor(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface;

    for (Interface = Surface->Interfaces; Interface != nullptr; Interface = Interface->Next)
    {
        if (Interface->Version == Version)
            return Interface;
    }

    Interface = new (std::nothrow) DdSurfaceInterface;
    if (Interface == nullptr)
        return nullptr;

    Interface->Vtbl = DdSurfaceVtblFor(Version);
    Interface->Surface = Surface;
    Interface->RefCount = 0;
    Interface->Version = Version;
    Interface->Next = Surface->Interfaces;
    Surface->Interfaces = Interface;
    return Interface;
}

UCHAR
DdSurfaceVersionOf(
    _In_ VOID* Com)
{
    return DdInterfaceFromCom(Com)->Version;
}

_Ret_maybenull_
static
DdSurfaceInterface*
DdExistingInterface(
    _In_ const DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface;

    for (Interface = Surface->Interfaces; Interface != nullptr; Interface = Interface->Next)
    {
        if (Interface->Version == Version)
            return Interface;
    }
    return nullptr;
}

ULONG
DdSurfaceRefsOn(
    _In_ const DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface = DdExistingInterface(Surface, Version);

    return Interface != nullptr ? Interface->RefCount : 0;
}

/* Takes a reference through one interface version, as an attachment or the runtime itself does */
VOID
DdHoldSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface;

    if ((Surface->ChainFlags & DdChainDying) != 0)
        return;
    Interface = DdSurfaceInterfaceFor(Surface, Version);
    if (Interface == nullptr)
        return;
    Interface->RefCount += 1;
    Surface->RefCount += 1;
}

/* Gives back one reference on an interface; the last one destroys the surface */
static
ULONG
DdReleaseThrough(
    _In_ DdSurface* Surface,
    _In_ DdSurfaceInterface* Interface)
{
    DdSurfaceInterface** Link;
    ULONG Remaining;

    if ((Surface->ChainFlags & DdChainDying) != 0 || Interface->RefCount == 0)
        return 0;

    /* A surface complex creation made lives as long as the surface that made it */
    if (Surface->RefCount == 1 && (Surface->ChainFlags & DdChainImplicit) != 0)
        return 0;

    Interface->RefCount -= 1;
    Remaining = Interface->RefCount;
    Surface->RefCount -= 1;

    if (Surface->RefCount == 0)
    {
        DdDestroySurface(Surface, Interface->Version);
        return 0;
    }

    if (Remaining == 0)
    {
        for (Link = &Surface->Interfaces; *Link != nullptr; Link = &(*Link)->Next)
        {
            if (*Link == Interface)
            {
                *Link = Interface->Next;
                break;
            }
        }
        Interface->Vtbl = nullptr;
        delete Interface;
    }
    return Remaining;
}

VOID
DdDropSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface = DdExistingInterface(Surface, Version);

    if (Interface != nullptr)
        DdReleaseThrough(Surface, Interface);
}

/* An interface of the given version with a reference for the caller */
_Ret_maybenull_
VOID*
DdHandOutSurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface = DdSurfaceInterfaceFor(Surface, Version);

    if (Interface == nullptr)
        return nullptr;
    Interface->RefCount += 1;
    Surface->RefCount += 1;
    return Interface;
}

/* Bytes per row the emulation layer uses: whole pixels rounded up to a DWORD */
static
LONG
DdSurfacePitch(
    _In_ DWORD Width,
    _In_ DWORD BitCount)
{
    return static_cast<LONG>(((static_cast<ULONGLONG>(Width) * BitCount + 31) / 32) * 4);
}

static
HRESULT
DdCheckPixelFormat(
    _In_ const DDPIXELFORMAT* Format)
{
    DWORD Flags = Format->dwFlags;

    if (Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;

    if ((Flags & DDPF_FOURCC) != 0)
        return DDERR_INVALIDPIXELFORMAT;

    if ((Flags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) != 0)
    {
        if (Format->dwRGBBitCount != 1 && Format->dwRGBBitCount != 2 && Format->dwRGBBitCount != 4 &&
            Format->dwRGBBitCount != 8)
        {
            return DDERR_INVALIDPIXELFORMAT;
        }
        return S_OK;
    }

    if ((Flags & DDPF_RGB) == 0)
        return DDERR_INVALIDPIXELFORMAT;
    if ((Flags & DDPF_ALPHAPIXELS) != 0 && Format->dwRGBAlphaBitMask == 0)
        return DDERR_INVALIDPIXELFORMAT;

    if (Format->dwRGBBitCount != 16 && Format->dwRGBBitCount != 24 && Format->dwRGBBitCount != 32)
        return DDERR_INVALIDPIXELFORMAT;

    return S_OK;
}

static
VOID
DdLinkSurface(
    _In_ DdSurface* Surface)
{
    Surface->Next = Surface->Driver->Surfaces;
    Surface->Driver->Surfaces = Surface;
}

static
VOID
DdUnlinkSurface(
    _In_ DdSurface* Surface)
{
    DdSurface** Link;

    for (Link = &Surface->Driver->Surfaces; *Link != nullptr; Link = &(*Link)->Next)
    {
        if (*Link == Surface)
        {
            *Link = Surface->Next;
            break;
        }
    }

    if (Surface->Driver->Primary == Surface)
        Surface->Driver->Primary = nullptr;
}

static
VOID
DdFreeSurfaceMemory(
    _Inout_ DdSurface* Surface)
{
    if (Surface->Resource != nullptr)
        DdDestroySurfaceResource(Surface->Driver->Hardware, Surface);
    if (Surface->SystemResource != nullptr)
        DdDestroySystemResource(Surface->Driver->Hardware, Surface);
    if (Surface->Bits != nullptr)
        UnmapViewOfFile(Surface->Bits);
    if (Surface->Section != NULL)
        CloseHandle(Surface->Section);
    Surface->Bits = nullptr;
    Surface->Section = NULL;
}

/* A section lets GDI wrap the same pixels when the application asks for a DC */
static
HRESULT
DdAllocateSystemBits(
    _Inout_ DdSurface* Surface)
{
    SIZE_T Bytes = static_cast<SIZE_T>(Surface->Pitch) * Surface->Height;

    Surface->Section = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                          static_cast<DWORD>(static_cast<ULONGLONG>(Bytes) >> 32),
                                          static_cast<DWORD>(Bytes), NULL);
    if (Surface->Section != NULL)
        Surface->Bits = static_cast<BYTE*>(MapViewOfFile(Surface->Section, FILE_MAP_ALL_ACCESS, 0, 0, Bytes));
    if (Surface->Bits == nullptr)
    {
        DdTrace(L"cannot allocate %Iu bytes for a %lux%lu surface", Bytes, Surface->Width, Surface->Height);
        DdFreeSurfaceMemory(Surface);
        return DDERR_OUTOFMEMORY;
    }
    return S_OK;
}

static
VOID
DdFreePrivateEntry(
    _In_ DdPrivateData* Entry)
{
    if ((Entry->Flags & DDSPD_IUNKNOWNPOINTER) != 0)
        Entry->Object->Release();
    else
        delete[] Entry->Bytes;
    delete Entry;
}

static
VOID
DdFreeAllPrivateData(
    _Inout_ DdSurface* Surface)
{
    DdPrivateData* Entry;

    while ((Entry = Surface->PrivateData) != nullptr)
    {
        Surface->PrivateData = Entry->Next;
        DdFreePrivateEntry(Entry);
    }
}

static
VOID
DdDestroySurface(
    _In_ DdSurface* Surface,
    _In_ UCHAR Version)
{
    DdSurfaceInterface* Interface;
    DdInterface* Owner = Surface->Owner;

    Surface->ChainFlags |= DdChainDying;
    DdDetachForDestroy(Surface, Version);
    DdUnlinkSurface(Surface);
    if (Surface->Dc != NULL)
        DdDropSurfaceDc(Surface);
    if (Surface->Palette != nullptr)
        DdReleasePalette(Surface->Palette);
    if (Surface->Clipper != nullptr)
        DdReleaseClipper(Surface->Clipper);
    DdFreeSurfaceMemory(Surface);
    DdFreeAllPrivateData(Surface);

    while ((Interface = Surface->Interfaces) != nullptr)
    {
        Surface->Interfaces = Interface->Next;
        Interface->Vtbl = nullptr;
        Interface->Surface = nullptr;
        delete Interface;
    }
    delete Surface;

    if (Owner != nullptr)
        DdReleaseInterface(Owner);
}

/* The combinations of caps the runtime refuses before it looks at anything else */
static
HRESULT
DdCheckCapsCombination(
    _In_ const DdInterface* This,
    _In_ const DDSURFACEDESC2* Desc,
    _In_ const DDSCAPS2* Requested)
{
    DWORD Caps = Requested->dwCaps;
    DWORD Hints = Requested->dwCaps2;
    BOOLEAN Counted = (Desc->dwFlags & DDSD_BACKBUFFERCOUNT) != 0;
    BOOLEAN Mipped = (Desc->dwFlags & DDSD_MIPMAPCOUNT) != 0;

    /* Counts only mean something on complex surfaces, and a flip chain needs one */
    if ((Caps & DDSCAPS_COMPLEX) != 0)
    {
        if (Counted && Mipped)
            return E_INVALIDARG;
    }
    else if (Counted || Mipped)
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Caps & DDSCAPS_FLIP) != 0)
    {
        if ((Caps & DDSCAPS_COMPLEX) == 0 || !Counted || Desc->dwBackBufferCount == 0)
            return DDERR_INVALIDCAPS;
        if (Mipped)
            return E_NOTIMPL;
    }

    if ((Caps & (DDSCAPS_VISIBLE | DDSCAPS_PALETTE | DDSCAPS_OPTIMIZED)) != 0)
        return DDERR_INVALIDCAPS;
    if ((Caps & DDSCAPS_WRITEONLY) != 0 && (Caps & DDSCAPS_RESERVED2) == 0 &&
        (Hints & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)) == 0)
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Hints & (DDSCAPS2_HINTDYNAMIC | DDSCAPS2_HINTSTATIC)) == (DDSCAPS2_HINTDYNAMIC | DDSCAPS2_HINTSTATIC) ||
        (Hints & (DDSCAPS2_HINTANTIALIASING | DDSCAPS2_HINTDYNAMIC)) == (DDSCAPS2_HINTANTIALIASING | DDSCAPS2_HINTDYNAMIC) ||
        (Hints & (DDSCAPS2_HINTANTIALIASING | DDSCAPS2_HINTSTATIC)) == (DDSCAPS2_HINTANTIALIASING | DDSCAPS2_HINTSTATIC))
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Caps & DDSCAPS_LOCALVIDMEM) != 0 && (Caps & DDSCAPS_NONLOCALVIDMEM) != 0)
        return DDERR_INVALIDCAPS;
    if ((Caps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) != 0 && (Caps & DDSCAPS_SYSTEMMEMORY) != 0)
        return DDERR_INVALIDCAPS;

    /* Only the first DirectDraw interface may name front and back buffers itself */
    if (This->Kind != DdKind::Draw1 && (Caps & (DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER)) != 0)
        return DDERR_INVALIDCAPS;

    if ((Caps & DDSCAPS_COMPLEX) != 0)
    {
        if ((Caps & DDSCAPS_FRONTBUFFER) != 0)
            return DDERR_INVALIDCAPS;
        if ((Caps & DDSCAPS_BACKBUFFER) != 0 &&
            ((Caps & (DDSCAPS_ZBUFFER | DDSCAPS_ALPHA)) == 0 || (Caps & DDSCAPS_FLIP) != 0))
        {
            return DDERR_INVALIDCAPS;
        }
        if ((Caps & (DDSCAPS_TEXTURE | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_OVERLAY | DDSCAPS_PRIMARYSURFACE |
                     DDSCAPS_BACKBUFFER)) == 0)
        {
            return DDERR_INVALIDCAPS;
        }
        if ((Caps & (DDSCAPS_MIPMAP | DDSCAPS_ZBUFFER | DDSCAPS_FLIP | DDSCAPS_ALPHA)) == 0 &&
            (Hints & DDSCAPS2_CUBEMAP) == 0)
        {
            return DDERR_INVALIDCAPS;
        }
    }
    if ((Caps & DDSCAPS_BACKBUFFER) != 0 && (Caps & (DDSCAPS_FRONTBUFFER | DDSCAPS_ALPHA)) != 0)
        return DDERR_INVALIDCAPS;
    if ((Caps & DDSCAPS_FLIP) != 0 && (Caps & DDSCAPS_COMPLEX) == 0)
        return DDERR_INVALIDCAPS;
    if ((Caps & DDSCAPS_PRIMARYSURFACE) != 0 &&
        ((Caps & (DDSCAPS_TEXTURE | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_OVERLAY | DDSCAPS_BACKBUFFER)) != 0 ||
         ((Caps & DDSCAPS_ZBUFFER) != 0 && (Caps & DDSCAPS_FLIP) == 0)))
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Caps & DDSCAPS_OFFSCREENPLAIN) != 0 && (Caps & (DDSCAPS_TEXTURE | DDSCAPS_OVERLAY | DDSCAPS_BACKBUFFER)) != 0)
        return DDERR_INVALIDCAPS;
    if ((Caps & DDSCAPS_OVERLAY) != 0 &&
        ((Caps & (DDSCAPS_TEXTURE | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_BACKBUFFER)) != 0 ||
         ((Caps & DDSCAPS_ZBUFFER) != 0 && (Caps & DDSCAPS_FLIP) == 0)))
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Caps & DDSCAPS_MIPMAP) != 0)
    {
        if ((Caps & DDSCAPS_TEXTURE) == 0 || (Caps & DDSCAPS_ZBUFFER) != 0)
            return DDERR_INVALIDCAPS;
    }
    else if ((Caps & DDSCAPS_ZBUFFER) != 0 && (Caps & DDSCAPS_BACKBUFFER) != 0 && (Caps & DDSCAPS_COMPLEX) == 0)
    {
        return DDERR_INVALIDCAPS;
    }
    if ((Caps & DDSCAPS_ALLOCONLOAD) != 0 && (Caps & DDSCAPS_TEXTURE) == 0)
        return DDERR_INVALIDCAPS;
    return S_OK;
}

/* A Z buffer's format comes from the pixel format, or from the depth field of the short description */
static
HRESULT
DdTakeDepthFormat(
    _In_ const DDSURFACEDESC2* Desc,
    _In_ BOOLEAN ZBuffer,
    _Out_ DDPIXELFORMAT* Format)
{
    const DDPIXELFORMAT* Given = &Desc->ddpfPixelFormat;
    BOOLEAN Short = Desc->dwSize < sizeof(DDSURFACEDESC2);
    BOOLEAN HasFormat = (Desc->dwFlags & DDSD_PIXELFORMAT) != 0;
    DWORD Depth;

    if (!ZBuffer)
    {
        if ((Desc->dwFlags & DDSD_ZBUFFERBITDEPTH) != 0)
            return E_INVALIDARG;
        if (HasFormat && (Given->dwFlags & (DDPF_ZBUFFER | DDPF_STENCILBUFFER)) != 0)
            return E_INVALIDARG;
        return S_FALSE;
    }

    if (!Short && (Desc->dwFlags & DDSD_ZBUFFERBITDEPTH) != 0)
        return E_INVALIDARG;

    if (HasFormat)
    {
        if (Given->dwSize != sizeof(*Given) || (Given->dwFlags & DDPF_ZBUFFER) == 0)
            return E_INVALIDARG;
        if (Short && (Given->dwFlags & DDPF_STENCILBUFFER) != 0)
            return E_INVALIDARG;
        if (!Short && Given->dwZBitMask == 0)
            return E_INVALIDARG;
        if ((Given->dwFlags & DDPF_STENCILBUFFER) != 0 && (Given->dwStencilBitDepth == 0 || Given->dwStencilBitMask == 0))
            return E_INVALIDARG;
        *Format = *Given;
    }
    else
    {
        if (!Short || (Desc->dwFlags & DDSD_ZBUFFERBITDEPTH) == 0)
            return E_INVALIDARG;
        Depth = Desc->dwMipMapCount;
        ZeroMemory(Format, sizeof(*Format));
        Format->dwSize = sizeof(*Format);
        Format->dwFlags = DDPF_ZBUFFER;
        Format->dwZBufferBitDepth = Depth;
        Format->dwZBitMask = Depth >= 32 ? 0xFFFFFFFF : (1u << Depth) - 1;
    }

    Depth = Format->dwZBufferBitDepth;
    if (Depth < 8)
        return E_INVALIDARG;
    if (Depth != 8 && Depth != 16 && Depth != 24 && Depth != 32)
        return DDERR_INVALIDPIXELFORMAT;
    return S_OK;
}

/* One surface with its memory, its first interface and a reference on it */
static
HRESULT
DdNewSurface(
    _In_ DdInterface* Maker,
    _In_ const DDSCAPS2* Caps,
    _In_ const DDPIXELFORMAT* Format,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ UCHAR Version,
    _Outptr_ DdSurface** Result)
{
    DdLocal* Local = Maker->Local;
    DdDriver* Driver = Local->Driver;
    DdSurfaceInterface* Interface;
    DdSurface* Surface;
    HRESULT hr;

    Surface = new (std::nothrow) DdSurface;
    if (Surface == nullptr)
        return E_OUTOFMEMORY;

    ZeroMemory(Surface, sizeof(*Surface));
    Surface->Driver = Driver;
    Surface->Creator = Local;
    Surface->MakerKind = Maker->Kind;
    Surface->ProcessId = GetCurrentProcessId();
    Surface->Caps = *Caps;
    Surface->Format = *Format;
    Surface->Width = Width;
    Surface->Height = Height;
    Surface->Pitch = DdSurfacePitch(Width, Format->dwRGBBitCount);
    Surface->Uniqueness = 1;

    if ((Caps->dwCaps & DDSCAPS_PRIMARYSURFACE) == 0 && (Caps->dwCaps & DDSCAPS_VIDEOMEMORY) != 0)
        hr = DdCreateSurfaceResource(Driver->Hardware, Surface);
    else
        hr = DdAllocateSystemBits(Surface);
    if (FAILED(hr))
    {
        delete Surface;
        return hr;
    }

    /* The pool the memory was charged to becomes part of the caps */
    if (Surface->Resource != nullptr && (Surface->Caps.dwCaps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) == 0)
        Surface->Caps.dwCaps |= Surface->Resource->ChargedShared ? DDSCAPS_NONLOCALVIDMEM : DDSCAPS_LOCALVIDMEM;

    Interface = DdSurfaceInterfaceFor(Surface, Version);
    if (Interface == nullptr)
    {
        DdFreeSurfaceMemory(Surface);
        delete Surface;
        return E_OUTOFMEMORY;
    }

    Interface->RefCount = 1;
    Surface->RefCount = 1;
    DdLinkSurface(Surface);
    *Result = Surface;
    return S_OK;
}

/* Builds a front buffer and its back buffers, each pointing at the next and the last at the front */
static
HRESULT
DdNewFlipChain(
    _In_ DdInterface* Maker,
    _In_ const DDSCAPS2* Caps,
    _In_ const DDPIXELFORMAT* Format,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BackCount,
    _In_ UCHAR Version,
    _Outptr_ DdSurface** Result)
{
    DdSurface* Front = nullptr;
    DdSurface* Previous = nullptr;
    DdSurface* Member;
    HRESULT hr;

    for (DWORD Index = 0; Index <= BackCount; Index++)
    {
        hr = DdNewSurface(Maker, Caps, Format, Width, Height, Version, &Member);
        if (SUCCEEDED(hr) && Previous != nullptr)
            hr = DdAddLink(Previous, Version, Member, Version, TRUE);
        if (FAILED(hr))
        {
            if (Front != nullptr)
            {
                Front->ChainFlags &= ~DdChainImplicit;
                DdDestroySurface(Front, Version);
            }
            return hr;
        }

        if (Front == nullptr)
        {
            Front = Member;
            Member->Caps.dwCaps |= DDSCAPS_FRONTBUFFER;
            Member->ChainFlags = DdChainRoot | DdChainWasFront;
            Member->BackBufferCount = BackCount;
        }
        else
        {
            Member->ChainFlags = DdChainImplicit;
            if (Index == 1)
            {
                Member->Caps.dwCaps |= DDSCAPS_BACKBUFFER;
                Member->ChainFlags |= DdChainWasBack;
            }
        }
        Previous = Member;
    }

    hr = DdAddLink(Previous, Version, Front, Version, TRUE);
    if (FAILED(hr))
    {
        DdDestroySurface(Front, Version);
        return hr;
    }
    *Result = Front;
    return S_OK;
}

HRESULT
DdCreateSurfaceObject(
    _In_ DdInterface* This,
    _In_ const DDSURFACEDESC2* Desc,
    _Out_ VOID** Result,
    _In_opt_ IUnknown* Outer)
{
    DdLocal* Local = This->Local;
    DdDriver* Driver = Local->Driver;
    DdSurface* Surface;
    DDSCAPS2 Caps = {};
    DDSCAPS2 Checked;
    DDPIXELFORMAT Format;
    BOOLEAN Primary;
    BOOLEAN Chain;
    BOOLEAN Depth;
    UCHAR Version;
    DWORD Width;
    DWORD Height;
    HRESULT hr;

    if (Outer != nullptr)
        return CLASS_E_NOAGGREGATION;
    if (Result == nullptr || Desc == nullptr)
        return E_INVALIDARG;
    *Result = nullptr;
    if (Desc->dwSize != (DdIsOldDraw(This) ? sizeof(DDSURFACEDESC) : sizeof(DDSURFACEDESC2)) &&
        !(This->Kind != DdKind::Draw4 && This->Kind != DdKind::Draw7 && Desc->dwSize == sizeof(DDSURFACEDESC)))
    {
        return E_INVALIDARG;
    }
    if ((Local->Flags & DDRAWILCL_SETCOOPCALLED) == 0)
        return DDERR_NOCOOPERATIVELEVELSET;

    if ((Desc->dwFlags & DDSD_CAPS) != 0)
        Caps = Desc->ddsCaps;
    if (Desc->dwSize < sizeof(DDSURFACEDESC2))
        Caps.dwCaps2 = Caps.dwCaps3 = Caps.dwCaps4 = 0;

    if ((Caps.dwCaps & DDSCAPS_SYSTEMMEMORY) != 0 && (Caps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0)
        return DDERR_INVALIDCAPS;
    if ((Caps.dwCaps & (DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM)) != 0 && (Caps.dwCaps & DDSCAPS_VIDEOMEMORY) == 0)
        return DDERR_INVALIDCAPS;

    /* The checks see the caps with the memory the first attempt asks for */
    Checked = Caps;
    if ((Checked.dwCaps & DDSCAPS_SYSTEMMEMORY) == 0)
        Checked.dwCaps |= DDSCAPS_VIDEOMEMORY;
    hr = DdCheckCapsCombination(This, Desc, &Checked);
    if (FAILED(hr))
        return hr;

    hr = DdTakeDepthFormat(Desc, (Caps.dwCaps & DDSCAPS_ZBUFFER) != 0, &Format);
    if (FAILED(hr))
        return hr;
    Depth = hr == S_OK;

    if ((Caps.dwCaps & DdCapsNeedingDirect3D) != 0)
    {
        DD_TRACE_ONCE(L"surfaces for Direct3D are not offered, as on 64 bit Windows");
        return E_NOINTERFACE;
    }
    if ((Caps.dwCaps & DDSCAPS_OVERLAY) != 0 && (Driver->HalCaps.dwCaps & DDCAPS_OVERLAY) == 0)
        return DDERR_NOOVERLAYHW;

    Chain = (Caps.dwCaps & DDSCAPS_FLIP) != 0;
    Primary = (Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    if (Primary)
    {
        if ((Desc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT)) != 0)
            return E_INVALIDARG;
        if (Chain)
        {
            if (Driver->ExclusiveOwner != Local)
                return DDERR_NOEXCLUSIVEMODE;
            DD_TRACE_ONCE(L"flipping primary chains are not implemented yet");
            return DDERR_UNSUPPORTED;
        }
        if (Driver->Primary != nullptr)
            return DDERR_PRIMARYSURFACEALREADYEXISTS;

        Width = Driver->Mode.Width;
        Height = Driver->Mode.Height;
    }
    else
    {
        if ((Desc->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT)) != (DDSD_WIDTH | DDSD_HEIGHT) || Desc->dwWidth == 0 ||
            Desc->dwHeight == 0 || Desc->dwWidth > DdMaxSurfaceSide || Desc->dwHeight > DdMaxSurfaceSide)
        {
            return E_INVALIDARG;
        }
        Width = Desc->dwWidth;
        Height = Desc->dwHeight;
    }

    if (Depth)
    {
        /* The Z buffer format was taken above */
    }
    else if (!Primary && (Desc->dwFlags & DDSD_PIXELFORMAT) != 0)
    {
        if ((Desc->dwFlags & DDSD_CAPS) == 0)
            return DDERR_INVALIDPIXELFORMAT;
        hr = DdCheckPixelFormat(&Desc->ddpfPixelFormat);
        if (FAILED(hr))
            return hr;
        Format = Desc->ddpfPixelFormat;
    }
    else
    {
        HDC Dc = DdCreateDisplayDc(Driver->Name);

        DdFillPixelFormat(&Format, Dc, 0);
        if (Dc != NULL)
            DeleteDC(Dc);
    }

    /* Where the surface says it lives; the primary is still kept in system memory */
    if (Primary)
    {
        Caps.dwCaps |= DDSCAPS_VISIBLE | DDSCAPS_VIDEOMEMORY | DDSCAPS_LOCALVIDMEM;
        Caps.dwCaps &= ~DDSCAPS_OFFSCREENPLAIN;
    }
    else if ((Caps.dwCaps & DDSCAPS_SYSTEMMEMORY) == 0)
    {
        /* What the driver cannot hold falls back to system memory unless video memory was demanded */
        if (Driver->Hardware != nullptr &&
            ((Desc->dwFlags & DDSD_CAPS) == 0 || DdDriverCanCreate(Driver->Hardware, &Caps, &Format)))
        {
            Caps.dwCaps |= DDSCAPS_VIDEOMEMORY;
        }
        else if (Driver->Hardware != nullptr && (Caps.dwCaps & DDSCAPS_VIDEOMEMORY) != 0)
        {
            return DDERR_UNSUPPORTED;
        }
        else
        {
            Caps.dwCaps |= DDSCAPS_SYSTEMMEMORY;
        }
    }

    Version = This->Kind == DdKind::Draw7 ? 7 : This->Kind == DdKind::Draw4 ? 4 : 1;
    if (Chain)
        hr = DdNewFlipChain(This, &Caps, &Format, Width, Height, Desc->dwBackBufferCount, Version, &Surface);
    else
        hr = DdNewSurface(This, &Caps, &Format, Width, Height, Version, &Surface);
    if (FAILED(hr))
        return hr;

    if (Primary)
        Driver->Primary = Surface;

    /* Only the surface handed back keeps the DirectDraw object alive */
    if (!DdIsOldDraw(This))
    {
        DdAddRefInterface(This);
        Surface->Owner = This;
    }

    *Result = DdExistingInterface(Surface, Version);
    return S_OK;
}

VOID
DdReleaseLocalSurfaces(
    _In_ DdLocal* Local)
{
    DdSurface* Surface;

    /* Destroying one surface of a chain takes others with it, so start over each time */
    for (;;)
    {
        for (Surface = Local->Driver->Surfaces; Surface != nullptr; Surface = Surface->Next)
        {
            if (Surface->Creator == Local && Surface->ProcessId == Local->ProcessId &&
                (Surface->ChainFlags & DdChainDying) == 0)
            {
                break;
            }
        }
        if (Surface == nullptr)
            break;
        Surface->Owner = nullptr;
        DdDestroySurface(Surface, Surface->Interfaces != nullptr ? Surface->Interfaces->Version : 7);
    }
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceQueryInterface(
    _In_ VOID* Com,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdSurfaceInterface* Found;
    UCHAR Version;

    DdLockScope Lock;

    if (Iid == nullptr || Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;

    if (IsEqualGUID(*Iid, IID_IUnknown) || IsEqualGUID(*Iid, IID_IDirectDrawSurface))
        Version = 1;
    else if (IsEqualGUID(*Iid, IID_IDirectDrawSurface2))
        Version = 2;
    else if (IsEqualGUID(*Iid, IID_IDirectDrawSurface3))
        Version = 3;
    else if (IsEqualGUID(*Iid, IID_IDirectDrawSurface4))
        Version = 4;
    else if (IsEqualGUID(*Iid, IID_IDirectDrawSurface7))
        Version = 7;
    else
        return E_NOINTERFACE;

    Found = DdSurfaceInterfaceFor(Surface, Version);
    if (Found == nullptr)
        return E_OUTOFMEMORY;

    Found->RefCount += 1;
    Surface->RefCount += 1;
    *Object = Found;
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
DdSurfaceAddRef(
    _In_ VOID* Com)
{
    DdSurfaceInterface* Interface = DdInterfaceFromCom(Com);

    DdLockScope Lock;

    Interface->RefCount += 1;
    Interface->Surface->RefCount += 1;
    return Interface->RefCount;
}

static
ULONG
STDMETHODCALLTYPE
DdSurfaceRelease(
    _In_ VOID* Com)
{
    DdSurfaceInterface* Interface = DdInterfaceFromCom(Com);

    DdLockScope Lock;

    if (Interface == nullptr || Interface->Surface == nullptr)
        return 0;
    return DdReleaseThrough(Interface->Surface, Interface);
}

static
VOID
DdDescribe(
    _In_ const DdSurface* Surface,
    _Inout_ DDSURFACEDESC2* Desc,
    _In_ DWORD Size)
{
    ZeroMemory(Desc, Size);
    Desc->dwSize = Size;
    Desc->dwFlags = DdDescFlags;
    Desc->dwWidth = Surface->Width;
    Desc->dwHeight = Surface->Height;
    Desc->lPitch = Surface->Pitch;
    Desc->ddpfPixelFormat = Surface->Format;

    /* The short description carries a Z buffer's depth in its own field */
    if ((Surface->Caps.dwCaps & DDSCAPS_ZBUFFER) != 0 && Size < sizeof(DDSURFACEDESC2))
    {
        Desc->dwFlags = (Desc->dwFlags & ~DDSD_PIXELFORMAT) | DDSD_ZBUFFERBITDEPTH;
        Desc->dwMipMapCount = Surface->Format.dwZBufferBitDepth;
    }
    if ((Surface->ChainFlags & DdChainWasFront) != 0)
    {
        Desc->dwFlags |= DDSD_BACKBUFFERCOUNT;
        Desc->dwBackBufferCount = Surface->BackBufferCount;
    }
    Desc->ddsCaps.dwCaps = Surface->Caps.dwCaps;
    if (Size >= sizeof(DDSURFACEDESC2))
    {
        Desc->ddsCaps.dwCaps2 = Surface->Caps.dwCaps2;
        Desc->ddsCaps.dwCaps3 = Surface->Caps.dwCaps3;
        Desc->ddsCaps.dwCaps4 = Surface->Caps.dwCaps4;
    }

    if ((Surface->KeyFlags & DDCKEY_SRCBLT) != 0)
    {
        Desc->dwFlags |= DDSD_CKSRCBLT;
        Desc->ddckCKSrcBlt = Surface->SourceKey;
    }
    if ((Surface->KeyFlags & DDCKEY_DESTBLT) != 0)
    {
        Desc->dwFlags |= DDSD_CKDESTBLT;
        Desc->ddckCKDestBlt = Surface->DestKey;
    }
}

/* The driver decides a resource's pitch; the first lock tells */
static
VOID
DdLearnPitch(
    _Inout_ DdSurface* Surface)
{
    DdHardware* Hardware = Surface->Driver->Hardware;
    BYTE* Bits;

    if (Surface->Pitch != 0 || Surface->Resource == nullptr)
        return;
    if (SUCCEEDED(DdLockSurfaceResource(Hardware, Surface, nullptr, 0, &Bits)))
        DdUnlockSurfaceResource(Hardware, Surface);
}

/* The full description an enumeration hands out; a pitch not yet learned stays zero */
VOID
DdDescribeSurface(
    _In_ DdSurface* Surface,
    _Out_ DDSURFACEDESC2* Desc)
{
    DdDescribe(Surface, Desc, sizeof(*Desc));
}

static
DWORD
DdDescSizeFor(
    _In_ const DdSurfaceInterface* Interface)
{
    return Interface->Version >= 4 ? sizeof(DDSURFACEDESC2) : sizeof(DDSURFACEDESC);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetSurfaceDesc(
    _In_ VOID* Com,
    _Inout_ DDSURFACEDESC2* Desc)
{
    DdSurfaceInterface* Interface = DdInterfaceFromCom(Com);

    DdLockScope Lock;

    if (Desc == nullptr || Desc->dwSize != DdDescSizeFor(Interface))
        return E_INVALIDARG;
    DdLearnPitch(Interface->Surface);
    DdDescribe(Interface->Surface, Desc, Desc->dwSize);
    return S_OK;
}

static
HRESULT
DdLockSurface(
    _In_ DdSurfaceInterface* Interface,
    _In_opt_ const RECT* Rect,
    _Inout_ DDSURFACEDESC2* Desc,
    _In_ DWORD Flags)
{
    DdSurface* Surface = Interface->Surface;
    const RECT* Rows;
    BYTE* Bits;
    RECT Whole;
    HRESULT hr;

    DdLockScope Lock;

    if (Desc == nullptr || Desc->dwSize != DdDescSizeFor(Interface))
        return E_INVALIDARG;

    /* Primary surfaces are composed by the desktop and cannot be locked */
    if ((Surface->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0)
        return E_NOTIMPL;

    SetRect(&Whole, 0, 0, Surface->Width, Surface->Height);
    if (Rect != nullptr &&
        (Rect->left < 0 || Rect->top < 0 || Rect->right > Whole.right || Rect->bottom > Whole.bottom ||
         Rect->left >= Rect->right || Rect->top >= Rect->bottom))
    {
        return E_INVALIDARG;
    }

    if (DdSurfaceBusy(Surface))
        return DDERR_SURFACEBUSY;

    if (Surface->Resource != nullptr)
    {
        hr = DdLockSurfaceResource(Surface->Driver->Hardware, Surface, Rect, Flags, &Bits);
        if (FAILED(hr))
            return hr;
    }
    else
    {
        DdWaitForDriver(Surface->Driver->Hardware, Surface);
        Rows = Rect != nullptr ? Rect : &Whole;
        Bits = Surface->Bits + Rows->top * Surface->Pitch + (Rows->left * Surface->Format.dwRGBBitCount) / 8;
    }

    Surface->Locked = TRUE;
    Surface->LockedReadOnly = (Flags & DDLOCK_READONLY) != 0;
    Surface->LockRect = Rect != nullptr ? *Rect : Whole;
    DdDescribe(Surface, Desc, Desc->dwSize);
    Desc->lpSurface = Bits;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceLock(
    _In_ VOID* Com,
    _In_opt_ LPRECT Rect,
    _Inout_ DDSURFACEDESC2* Desc,
    _In_ DWORD Flags,
    _In_opt_ HANDLE Event)
{
    return DdLockSurface(DdInterfaceFromCom(Com), Rect, Desc, Flags);
}

static
HRESULT
DdUnlockSurface(
    _In_ DdSurface* Surface,
    _In_opt_ const RECT* Rect)
{
    DdLockScope Lock;

    if (!Surface->Locked)
        return DDERR_NOTLOCKED;
    if (Rect != nullptr && !EqualRect(Rect, &Surface->LockRect))
        return DDERR_NOTLOCKED;

    Surface->Locked = FALSE;
    if (!Surface->LockedReadOnly)
        DdTouchSurface(Surface);
    if (Surface->Resource != nullptr)
        return DdUnlockSurfaceResource(Surface->Driver->Hardware, Surface);
    return S_OK;
}

HRESULT
DdMapSurface(
    _Inout_ DdSurface* Surface,
    _In_ BOOLEAN ReadOnly,
    _Outptr_ BYTE** Bits)
{
    if (Surface->Resource == nullptr)
    {
        DdWaitForDriver(Surface->Driver->Hardware, Surface);
        *Bits = Surface->Bits;
        return S_OK;
    }
    return DdLockSurfaceResource(Surface->Driver->Hardware, Surface, nullptr, ReadOnly ? DDLOCK_READONLY : 0, Bits);
}

VOID
DdUnmapSurface(
    _Inout_ DdSurface* Surface)
{
    if (Surface->Resource != nullptr)
        DdUnlockSurfaceResource(Surface->Driver->Hardware, Surface);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceUnlock4(
    _In_ VOID* Com,
    _In_opt_ LPRECT Rect)
{
    return DdUnlockSurface(DdSurfaceFromCom(Com), Rect);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceUnlock(
    _In_ VOID* Com,
    _In_opt_ LPVOID Bits)
{
    return DdUnlockSurface(DdSurfaceFromCom(Com), nullptr);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetCaps(
    _In_ VOID* Com,
    _Out_ DDSCAPS2* Caps)
{
    DdSurfaceInterface* Interface = DdInterfaceFromCom(Com);

    DdLockScope Lock;

    if (Caps == nullptr)
        return E_INVALIDARG;
    if (Interface->Version >= 4)
        *Caps = Interface->Surface->Caps;
    else
        Caps->dwCaps = Interface->Surface->Caps.dwCaps;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetPixelFormat(
    _In_ VOID* Com,
    _Out_ LPDDPIXELFORMAT Format)
{
    DdLockScope Lock;

    if (Format == nullptr || Format->dwSize != sizeof(*Format))
        return E_INVALIDARG;
    *Format = DdSurfaceFromCom(Com)->Format;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceIsLost(
    _In_ VOID* Com)
{
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceRestore(
    _In_ VOID* Com)
{
    return S_OK;
}

static
DDCOLORKEY*
DdKeySlot(
    _In_ DdSurface* Surface,
    _In_ DWORD Which)
{
    switch (Which)
    {
    case DDCKEY_SRCBLT:
        return &Surface->SourceKey;
    case DDCKEY_DESTBLT:
        return &Surface->DestKey;
    case DDCKEY_SRCOVERLAY:
        return &Surface->SourceOverlayKey;
    case DDCKEY_DESTOVERLAY:
        return &Surface->DestOverlayKey;
    default:
        return nullptr;
    }
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetColorKey(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _Out_ LPDDCOLORKEY Key)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DDCOLORKEY* Slot;

    DdLockScope Lock;

    Slot = DdKeySlot(Surface, Flags);
    if (Slot == nullptr || Key == nullptr)
        return E_INVALIDARG;
    if ((Surface->KeyFlags & Flags) == 0)
        return DDERR_NOCOLORKEY;
    *Key = *Slot;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceSetColorKey(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ LPDDCOLORKEY Key)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DDCOLORKEY* Slot;
    DWORD Which = Flags & ~DDCKEY_COLORSPACE;

    DdLockScope Lock;

    Slot = DdKeySlot(Surface, Which);
    if (Slot == nullptr)
        return E_INVALIDARG;

    if (Key == nullptr)
    {
        Surface->KeyFlags &= ~Which;
        return S_OK;
    }

    *Slot = *Key;
    if ((Flags & DDCKEY_COLORSPACE) == 0)
        Slot->dwColorSpaceHighValue = Slot->dwColorSpaceLowValue;
    Surface->KeyFlags |= Which;
    return S_OK;
}

/* Colors of a palettized surface: its palette, or GDI's system palette without one */
static
VOID
DdSurfaceColors(
    _In_ const DdSurface* Surface,
    _In_ DWORD Count,
    _Out_writes_(256) PALETTEENTRY* Entries)
{
    const DdPalette* Palette = Surface->Palette;
    DWORD Held;

    ZeroMemory(Entries, 256 * sizeof(*Entries));
    if (Palette != nullptr && (Palette->Flags & 0x1000) == 0)
    {
        Held = (Palette->Flags & 0x800) != 0 ? 2 : (Palette->Flags & 0x400) != 0 ? 4 : (Palette->Flags & 0x2) != 0 ? 16 : 256;
        memcpy(Entries, Palette->Entries, (Held < Count ? Held : Count) * sizeof(PALETTEENTRY));
    }
    else
    {
        GetSystemPaletteEntries(NULL, 0, Count, Entries);
    }
}

/* A DIB section over the surface pixels, so GDI and the application see the same memory */
static
HRESULT
DdCreateSectionDc(
    _Inout_ DdSurface* Surface)
{
    struct
    {
        BITMAPINFOHEADER Header;
        RGBQUAD Colors[256];
    } Info = {};
    PALETTEENTRY Entries[256];
    DWORD* Masks = reinterpret_cast<DWORD*>(Info.Colors);
    VOID* Bits;
    DWORD Count;
    DWORD Index;
    HDC Dc;

    Info.Header.biSize = sizeof(Info.Header);
    Info.Header.biWidth = static_cast<LONG>(Surface->Pitch * 8 / Surface->Format.dwRGBBitCount);
    Info.Header.biHeight = -static_cast<LONG>(Surface->Height);
    Info.Header.biPlanes = 1;
    Info.Header.biBitCount = static_cast<WORD>(Surface->Format.dwRGBBitCount);
    Info.Header.biCompression = BI_RGB;
    if (Surface->Format.dwRGBBitCount == 16 || Surface->Format.dwRGBBitCount == 32)
    {
        Info.Header.biCompression = BI_BITFIELDS;
        Masks[0] = Surface->Format.dwRBitMask;
        Masks[1] = Surface->Format.dwGBitMask;
        Masks[2] = Surface->Format.dwBBitMask;
    }
    else if (Surface->Format.dwRGBBitCount <= 8)
    {
        Count = 1u << Surface->Format.dwRGBBitCount;
        Info.Header.biClrUsed = Count;
        DdSurfaceColors(Surface, Count, Entries);
        for (Index = 0; Index < Count; Index++)
        {
            Info.Colors[Index].rgbRed = Entries[Index].peRed;
            Info.Colors[Index].rgbGreen = Entries[Index].peGreen;
            Info.Colors[Index].rgbBlue = Entries[Index].peBlue;
        }
    }

    Dc = CreateCompatibleDC(NULL);
    if (Dc == NULL)
        return DDERR_CANTCREATEDC;

    Surface->Bitmap = CreateDIBSection(Dc, reinterpret_cast<BITMAPINFO*>(&Info), DIB_RGB_COLORS, &Bits,
                                       Surface->Section, 0);
    if (Surface->Bitmap == NULL)
    {
        DdTrace(L"CreateDIBSection over a surface failed (%lu)", GetLastError());
        DeleteDC(Dc);
        return DDERR_CANTCREATEDC;
    }

    Surface->OldBitmap = SelectObject(Dc, Surface->Bitmap);
    Surface->Dc = Dc;
    return S_OK;
}

/* GDI only builds DCs over the formats a display could scan out */
static
BOOLEAN
DdGdiCanWrap(
    _In_ const DdSurface* Surface)
{
    switch (DdDriverFormat(&Surface->Format))
    {
    case D3DDDIFMT_P8:
    case D3DDDIFMT_R8G8B8:
    case D3DDDIFMT_A8R8G8B8:
    case D3DDDIFMT_X8R8G8B8:
    case D3DDDIFMT_R5G6B5:
    case D3DDDIFMT_X1R5G5B5:
    case D3DDDIFMT_A1R5G5B5:
        return TRUE;
    default:
        return FALSE;
    }
}

typedef NTSTATUS (APIENTRY* PDD_CREATE_DC_FROM_MEMORY)(D3DKMT_CREATEDCFROMMEMORY*);
typedef NTSTATUS (APIENTRY* PDD_DESTROY_DC_FROM_MEMORY)(const D3DKMT_DESTROYDCFROMMEMORY*);

static
FARPROC
DdGdiExport(
    _In_z_ PCSTR Name)
{
    HMODULE Gdi = GetModuleHandleW(L"gdi32.dll");

    return Gdi != NULL ? GetProcAddress(Gdi, Name) : nullptr;
}

/* A DC the kernel builds over the locked resource, kept until ReleaseDC */
static
HRESULT
DdCreateResourceDc(
    _Inout_ DdSurface* Surface)
{
    static PDD_CREATE_DC_FROM_MEMORY CreateDc =
        reinterpret_cast<PDD_CREATE_DC_FROM_MEMORY>(DdGdiExport("D3DKMTCreateDCFromMemory"));
    DdHardware* Hardware = Surface->Driver->Hardware;
    D3DKMT_CREATEDCFROMMEMORY Create = {};
    PALETTEENTRY Entries[256];
    BYTE* Bits;
    NTSTATUS Status;
    HDC Screen;
    HRESULT hr;

    if (CreateDc == nullptr)
        return DDERR_CANTCREATEDC;

    hr = DdLockSurfaceResource(Hardware, Surface, nullptr, 0, &Bits);
    if (FAILED(hr))
        return hr;

    if (Surface->Resource->Format == D3DDDIFMT_P8)
    {
        DdSurfaceColors(Surface, 256, Entries);
        Create.pColorTable = Entries;
    }

    Screen = GetDC(NULL);
    Create.pMemory = Bits;
    Create.Format = Surface->Resource->Format;
    Create.Width = Surface->Width;
    Create.Height = Surface->Height;
    Create.Pitch = static_cast<UINT>(Surface->Pitch);
    Create.hDeviceDc = Screen;
    Status = CreateDc(&Create);
    ReleaseDC(NULL, Screen);
    if (!NT_SUCCESS(Status))
    {
        DdTrace(L"D3DKMTCreateDCFromMemory failed 0x%08lX for format %u", Status, Surface->Resource->Format);
        DdUnlockSurfaceResource(Hardware, Surface);
        return DDERR_CANTCREATEDC;
    }

    Surface->Dc = Create.hDc;
    Surface->Bitmap = static_cast<HBITMAP>(Create.hBitmap);
    return S_OK;
}

static
VOID
DdDropSurfaceDc(
    _Inout_ DdSurface* Surface)
{
    static PDD_DESTROY_DC_FROM_MEMORY DestroyDc =
        reinterpret_cast<PDD_DESTROY_DC_FROM_MEMORY>(DdGdiExport("D3DKMTDestroyDCFromMemory"));
    D3DKMT_DESTROYDCFROMMEMORY Destroy;

    GdiFlush();
    if (Surface->Resource != nullptr)
    {
        Destroy.hDc = Surface->Dc;
        Destroy.hBitmap = Surface->Bitmap;
        if (DestroyDc != nullptr)
            DestroyDc(&Destroy);
        DdUnlockSurfaceResource(Surface->Driver->Hardware, Surface);
    }
    else
    {
        SelectObject(Surface->Dc, Surface->OldBitmap);
        DeleteDC(Surface->Dc);
        DeleteObject(Surface->Bitmap);
    }
    Surface->Dc = NULL;
    Surface->Bitmap = NULL;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetDC(
    _In_ VOID* Com,
    _Out_ HDC* Result)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    HRESULT hr;

    DdLockScope Lock;

    if (Result == nullptr)
        return E_INVALIDARG;
    if (Surface->Dc != NULL)
        return DDERR_DCALREADYCREATED;
    if (Surface->Locked)
        return DDERR_SURFACEBUSY;
    /* The composed primary cannot be locked, so GDI cannot wrap it either */
    if ((Surface->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0)
        return DDERR_CANTCREATEDC;
    if (!DdGdiCanWrap(Surface))
        return DDERR_CANTCREATEDC;

    if (Surface->Resource != nullptr)
    {
        hr = DdCreateResourceDc(Surface);
    }
    else
    {
        DdWaitForDriver(Surface->Driver->Hardware, Surface);
        hr = DdCreateSectionDc(Surface);
    }
    if (FAILED(hr))
        return hr;

    *Result = Surface->Dc;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceReleaseDC(
    _In_ VOID* Com,
    _In_opt_ HDC Dc)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Surface->Dc == NULL)
        return DDERR_NODC;

    DdDropSurfaceDc(Surface);
    DdTouchSurface(Surface);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceBlt(
    _In_ VOID* Com,
    _In_opt_ LPRECT TargetRect,
    _In_opt_ VOID* Source,
    _In_opt_ LPRECT SourceRect,
    _In_ DWORD Flags,
    _In_opt_ LPDDBLTFX Fx)
{
    DdLockScope Lock;

    return DdBlt(DdSurfaceFromCom(Com), TargetRect, DdSurfaceFromCom(Source), SourceRect, Flags, Fx);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceBltFast(
    _In_ VOID* Com,
    _In_ DWORD X,
    _In_ DWORD Y,
    _In_opt_ VOID* Source,
    _In_opt_ LPRECT SourceRect,
    _In_ DWORD Flags)
{
    DdSurface* From = DdSurfaceFromCom(Source);

    DdLockScope Lock;

    if (From == nullptr)
        return E_INVALIDARG;
    return DdBltFast(DdSurfaceFromCom(Com), X, Y, From, SourceRect, Flags);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetPalette(
    _In_ VOID* Com,
    _Out_ LPDIRECTDRAWPALETTE* Palette)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Palette == nullptr)
        return E_INVALIDARG;
    *Palette = nullptr;
    if (Surface->Palette == nullptr)
        return DDERR_NOPALETTEATTACHED;

    Surface->Palette->RefCount += 1;
    *Palette = reinterpret_cast<LPDIRECTDRAWPALETTE>(Surface->Palette);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceSetPalette(
    _In_ VOID* Com,
    _In_opt_ LPDIRECTDRAWPALETTE Palette)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdPalette* New = reinterpret_cast<DdPalette*>(Palette);

    DdLockScope Lock;

    if (New != nullptr && (Surface->Format.dwFlags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 |
                                                       DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXED8)) == 0 &&
        (Surface->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) == 0)
    {
        return DDERR_INVALIDPIXELFORMAT;
    }

    if (New != nullptr)
        New->RefCount += 1;
    if (Surface->Palette != nullptr)
    {
        Surface->Palette->OnPrimary = FALSE;
        DdReleasePalette(Surface->Palette);
    }
    Surface->Palette = New;
    if (New != nullptr)
        New->OnPrimary = (Surface->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetClipper(
    _In_ VOID* Com,
    _Out_ LPDIRECTDRAWCLIPPER* Clipper)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Clipper == nullptr)
        return E_INVALIDARG;
    *Clipper = nullptr;
    if (Surface->Clipper == nullptr)
        return DDERR_NOCLIPPERATTACHED;

    Surface->Clipper->RefCount += 1;
    *Clipper = reinterpret_cast<LPDIRECTDRAWCLIPPER>(Surface->Clipper);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceSetClipper(
    _In_ VOID* Com,
    _In_opt_ LPDIRECTDRAWCLIPPER Clipper)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdClipper* New = reinterpret_cast<DdClipper*>(Clipper);

    DdLockScope Lock;

    if (New == nullptr && Surface->Clipper == nullptr)
        return DDERR_NOCLIPPERATTACHED;

    if (New != nullptr)
        New->RefCount += 1;
    if (Surface->Clipper != nullptr)
        DdReleaseClipper(Surface->Clipper);
    Surface->Clipper = New;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetDDInterface(
    _In_ VOID* Com,
    _Out_ LPVOID* Draw)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdInterface* Interface;

    DdLockScope Lock;

    if (Draw == nullptr)
        return E_INVALIDARG;
    /* The interface of the kind that created the surface */
    Interface = DdFindInterface(Surface->Creator, Surface->MakerKind, TRUE);
    if (Interface == nullptr)
        return E_OUTOFMEMORY;
    DdAddRefInterface(Interface);
    *Draw = Interface;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceStatus(
    _In_ VOID* Com,
    _In_ DWORD Flags)
{
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfacePageLock(
    _In_ VOID* Com,
    _In_ DWORD Flags)
{
    return S_OK;
}

/* Moves the contents stamp on; a stamp of zero never moves, and wrapping skips zero */
VOID
DdTouchSurface(
    _Inout_ DdSurface* Surface)
{
    if (Surface->Uniqueness == 0)
        return;
    Surface->Uniqueness += 1;
    if (Surface->Uniqueness == 0)
        Surface->Uniqueness = 1;
}

_Ret_maybenull_
static
DdPrivateData*
DdFindPrivateData(
    _In_ DdSurface* Surface,
    _In_ REFGUID Tag,
    _Out_opt_ DdPrivateData*** Link)
{
    DdPrivateData** Slot;

    for (Slot = &Surface->PrivateData; *Slot != nullptr; Slot = &(*Slot)->Next)
    {
        if (IsEqualGUID((*Slot)->Tag, Tag))
        {
            if (Link != nullptr)
                *Link = Slot;
            return *Slot;
        }
    }
    return nullptr;
}

static
HRESULT
DdDropPrivateData(
    _In_ DdSurface* Surface,
    _In_ REFGUID Tag)
{
    DdPrivateData** Link;
    DdPrivateData* Entry;

    Entry = DdFindPrivateData(Surface, Tag, &Link);
    if (Entry == nullptr)
        return DDERR_NOTFOUND;
    *Link = Entry->Next;
    DdFreePrivateEntry(Entry);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceSetPrivateData(
    _In_ VOID* Com,
    _In_ REFGUID Tag,
    _In_ LPVOID Data,
    _In_ DWORD Size,
    _In_ DWORD Flags)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdPrivateData* Entry;

    DdLockScope Lock;

    if (Size == 0 || (Flags & ~(DDSPD_IUNKNOWNPOINTER | DDSPD_VOLATILE)) != 0)
        return E_INVALIDARG;

    /* Whatever was stored under the tag goes, even when the new data is refused */
    DdDropPrivateData(Surface, Tag);
    if ((Flags & DDSPD_IUNKNOWNPOINTER) != 0 && Size != sizeof(IUnknown*))
        return E_INVALIDARG;

    Entry = new (std::nothrow) DdPrivateData;
    if (Entry == nullptr)
        return E_OUTOFMEMORY;

    if ((Flags & DDSPD_IUNKNOWNPOINTER) != 0)
    {
        Entry->Object = static_cast<IUnknown*>(Data);
        Entry->Object->AddRef();
    }
    else
    {
        Entry->Bytes = new (std::nothrow) BYTE[Size];
        if (Entry->Bytes == nullptr)
        {
            delete Entry;
            return E_OUTOFMEMORY;
        }
        memcpy(Entry->Bytes, Data, Size);
    }

    Entry->Tag = Tag;
    Entry->Size = Size;
    Entry->Flags = Flags;
    Entry->Stamp = Surface->Uniqueness;
    Entry->Next = Surface->PrivateData;
    Surface->PrivateData = Entry;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetPrivateData(
    _In_ VOID* Com,
    _In_ REFGUID Tag,
    _Out_writes_bytes_opt_(*Size) LPVOID Data,
    _Inout_ LPDWORD Size)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdPrivateData* Entry;

    DdLockScope Lock;

    Entry = DdFindPrivateData(Surface, Tag, nullptr);
    if (Entry == nullptr)
        return DDERR_NOTFOUND;

    /* Volatile data lasts only while the contents stay as they were when it was set */
    if ((Entry->Flags & DDSPD_VOLATILE) != 0 && (Surface->Uniqueness == 0 || Entry->Stamp != Surface->Uniqueness))
    {
        *Size = 0;
        return DDERR_EXPIRED;
    }

    if (*Size < Entry->Size)
    {
        *Size = Entry->Size;
        return DDERR_MOREDATA;
    }

    if ((Entry->Flags & DDSPD_IUNKNOWNPOINTER) != 0)
        memcpy(Data, &Entry->Object, Entry->Size);
    else
        memcpy(Data, Entry->Bytes, Entry->Size);
    *Size = Entry->Size;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceFreePrivateData(
    _In_ VOID* Com,
    _In_ REFGUID Tag)
{
    DdLockScope Lock;

    return DdDropPrivateData(DdSurfaceFromCom(Com), Tag);
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetUniqueness(
    _In_ VOID* Com,
    _Out_ LPDWORD Value)
{
    DdLockScope Lock;

    if (Value == nullptr)
        return E_INVALIDARG;
    *Value = DdSurfaceFromCom(Com)->Uniqueness;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceChangeUniqueness(
    _In_ VOID* Com)
{
    DdLockScope Lock;

    DdTouchSurface(DdSurfaceFromCom(Com));
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceSetPriority(
    _In_ VOID* Com,
    _In_ DWORD Priority)
{
    DdSurfaceFromCom(Com)->Priority = Priority;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceGetPriority(
    _In_ VOID* Com,
    _Out_ LPDWORD Priority)
{
    if (Priority == nullptr)
        return E_INVALIDARG;
    *Priority = DdSurfaceFromCom(Com)->Priority;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceInitialize(
    _In_ VOID* Com,
    _In_opt_ VOID* Draw,
    _In_opt_ VOID* Desc)
{
    return DDERR_ALREADYINITIALIZED;
}

/* Flipping and overlays are not implemented yet */
static
HRESULT
STDMETHODCALLTYPE
DdSurfaceMissing1(
    _In_ VOID* Com,
    _In_opt_ VOID* First)
{
    DD_TRACE_ONCE(L"a surface method that is not implemented yet was called");
    return DDERR_UNSUPPORTED;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceMissing2(
    _In_ VOID* Com,
    _In_opt_ VOID* First,
    _In_opt_ VOID* Second)
{
    DD_TRACE_ONCE(L"a surface method that is not implemented yet was called");
    return DDERR_UNSUPPORTED;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceMissing3(
    _In_ VOID* Com,
    _In_opt_ VOID* First,
    _In_opt_ VOID* Second,
    _In_opt_ VOID* Third)
{
    DD_TRACE_ONCE(L"a surface method that is not implemented yet was called");
    return DDERR_UNSUPPORTED;
}

static
HRESULT
STDMETHODCALLTYPE
DdSurfaceMissing5(
    _In_ VOID* Com,
    _In_opt_ VOID* First,
    _In_opt_ VOID* Second,
    _In_opt_ VOID* Third,
    _In_opt_ VOID* Fourth,
    _In_opt_ VOID* Fifth)
{
    DD_TRACE_ONCE(L"a surface method that is not implemented yet was called");
    return DDERR_UNSUPPORTED;
}

#define DD_SURFACE_SLOT(Function) reinterpret_cast<const VOID*>(&Function)

/* The IDirectDrawSurface slots every version starts with, Unlock passed in */
#define DD_SURFACE_SLOTS(UnlockSlot) \
    DD_SURFACE_SLOT(DdSurfaceQueryInterface), DD_SURFACE_SLOT(DdSurfaceAddRef), DD_SURFACE_SLOT(DdSurfaceRelease), \
    DD_SURFACE_SLOT(DdSurfaceAddAttached), DD_SURFACE_SLOT(DdSurfaceMissing1), DD_SURFACE_SLOT(DdSurfaceBlt), \
    DD_SURFACE_SLOT(DdSurfaceMissing3), DD_SURFACE_SLOT(DdSurfaceBltFast), DD_SURFACE_SLOT(DdSurfaceDeleteAttached), \
    DD_SURFACE_SLOT(DdSurfaceEnumAttached), DD_SURFACE_SLOT(DdSurfaceMissing3), DD_SURFACE_SLOT(DdSurfaceMissing2), \
    DD_SURFACE_SLOT(DdSurfaceGetAttached), DD_SURFACE_SLOT(DdSurfaceStatus), DD_SURFACE_SLOT(DdSurfaceGetCaps), \
    DD_SURFACE_SLOT(DdSurfaceGetClipper), DD_SURFACE_SLOT(DdSurfaceGetColorKey), DD_SURFACE_SLOT(DdSurfaceGetDC), \
    DD_SURFACE_SLOT(DdSurfaceStatus), DD_SURFACE_SLOT(DdSurfaceMissing2), DD_SURFACE_SLOT(DdSurfaceGetPalette), \
    DD_SURFACE_SLOT(DdSurfaceGetPixelFormat), DD_SURFACE_SLOT(DdSurfaceGetSurfaceDesc), \
    DD_SURFACE_SLOT(DdSurfaceInitialize), DD_SURFACE_SLOT(DdSurfaceIsLost), DD_SURFACE_SLOT(DdSurfaceLock), \
    DD_SURFACE_SLOT(DdSurfaceReleaseDC), DD_SURFACE_SLOT(DdSurfaceRestore), DD_SURFACE_SLOT(DdSurfaceSetClipper), \
    DD_SURFACE_SLOT(DdSurfaceSetColorKey), DD_SURFACE_SLOT(DdSurfaceMissing2), DD_SURFACE_SLOT(DdSurfaceSetPalette), \
    DD_SURFACE_SLOT(UnlockSlot), DD_SURFACE_SLOT(DdSurfaceMissing5), DD_SURFACE_SLOT(DdSurfaceMissing1), \
    DD_SURFACE_SLOT(DdSurfaceMissing2)

#define DD_SURFACE2_SLOTS \
    DD_SURFACE_SLOT(DdSurfaceGetDDInterface), DD_SURFACE_SLOT(DdSurfacePageLock), DD_SURFACE_SLOT(DdSurfacePageLock)

#define DD_SURFACE4_SLOTS \
    DD_SURFACE_SLOT(DdSurfaceSetPrivateData), DD_SURFACE_SLOT(DdSurfaceGetPrivateData), \
    DD_SURFACE_SLOT(DdSurfaceFreePrivateData), DD_SURFACE_SLOT(DdSurfaceGetUniqueness), \
    DD_SURFACE_SLOT(DdSurfaceChangeUniqueness)

const VOID* const DdSurfaceVtbl1[] =
{
    DD_SURFACE_SLOTS(DdSurfaceUnlock),
};

const VOID* const DdSurfaceVtbl2[] =
{
    DD_SURFACE_SLOTS(DdSurfaceUnlock), DD_SURFACE2_SLOTS,
};

const VOID* const DdSurfaceVtbl3[] =
{
    DD_SURFACE_SLOTS(DdSurfaceUnlock), DD_SURFACE2_SLOTS, DD_SURFACE_SLOT(DdSurfaceMissing2),
};

const VOID* const DdSurfaceVtbl4[] =
{
    DD_SURFACE_SLOTS(DdSurfaceUnlock4), DD_SURFACE2_SLOTS, DD_SURFACE_SLOT(DdSurfaceMissing2), DD_SURFACE4_SLOTS,
};

const VOID* const DdSurfaceVtbl7[] =
{
    DD_SURFACE_SLOTS(DdSurfaceUnlock4), DD_SURFACE2_SLOTS, DD_SURFACE_SLOT(DdSurfaceMissing2), DD_SURFACE4_SLOTS,
    DD_SURFACE_SLOT(DdSurfaceSetPriority), DD_SURFACE_SLOT(DdSurfaceGetPriority), DD_SURFACE_SLOT(DdSurfaceSetPriority),
    DD_SURFACE_SLOT(DdSurfaceGetPriority),
};

static_assert(_countof(DdSurfaceVtbl1) == 36 && _countof(DdSurfaceVtbl2) == 39 && _countof(DdSurfaceVtbl3) == 40 &&
              _countof(DdSurfaceVtbl4) == 45 && _countof(DdSurfaceVtbl7) == 49);
