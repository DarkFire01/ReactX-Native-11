/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IDirectDraw through IDirectDraw7 methods and their tables
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

#include <vector>

static const GUID DdIidVideoPortContainer = { 0x6C142760, 0xA733, 0x11CE, { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 } };

/* Surface caps that never name a memory pool */
static constexpr DWORD DdCapsNotMemory = 0x0005893C;

/* Surface caps the memory query refuses outright */
static constexpr DWORD DdCapsBadForMemory = 0x03000000;

/* Driver flag that marks a ModeX desktop */
static constexpr ULONG DdDriverModeX = DDRAWI_MODEX;

/* The fixed ModeX modes offered when a 640x480 palettized mode exists */
static constexpr DWORD DdModeXRefresh = 60;

static
HRESULT
DdCheckExclusive(
    _In_ DdLocal* Local,
    _Out_ BOOLEAN* Owned,
    _Out_ BOOLEAN* Mine)
{
    DdLocal* Owner = Local->Driver != nullptr ? Local->Driver->ExclusiveOwner : nullptr;

    *Owned = Owner != nullptr;
    *Mine = Owner == Local;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdQueryInterface(
    _In_ VOID* Com,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object)
{
    return DdQueryObject(DdFromCom(Com), Iid, Object);
}

HRESULT
DdQueryObject(
    _In_ DdInterface* This,
    _In_opt_ const IID* Iid,
    _Outptr_ VOID** Object)
{
    DdInterface* Found;
    DdKind Kind;

    DdLockScope Lock;

    if (Iid == nullptr || Object == nullptr)
        return E_INVALIDARG;
    *Object = nullptr;

    if (IsEqualGUID(*Iid, IID_IUnknown) || IsEqualGUID(*Iid, IID_IDirectDraw))
    {
        Kind = DdKind::Draw1;
    }
    else if (IsEqualGUID(*Iid, IID_IDirectDraw2))
    {
        Kind = DdKind::Draw2;
    }
    else if (IsEqualGUID(*Iid, IID_IDirectDraw4))
    {
        Kind = DdKind::Draw4;
    }
    else if (IsEqualGUID(*Iid, IID_IDirectDraw7))
    {
        Kind = DdKind::Draw7;
    }
    else
    {
        /* No legacy Direct3D runtime ships for 64 bit processes, so Direct3D interfaces are not offered */
        if (IsEqualGUID(*Iid, DdIidVideoPortContainer))
            DD_TRACE_ONCE(L"video port container interface is not implemented");
        return E_NOINTERFACE;
    }

    /* The interface asked for keeps an uninitialized table only when it is this one */
    if (This->Kind == Kind)
        Found = This;
    else
        Found = DdFindInterface(This->Local, Kind, TRUE);

    if (Found == nullptr)
        return E_NOINTERFACE;

    DdAddRefInterface(Found);
    *Object = Found;
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
DdAddRef(
    _In_ VOID* Com)
{
    return DdAddRefInterface(DdFromCom(Com));
}

static
ULONG
STDMETHODCALLTYPE
DdRelease(
    _In_ VOID* Com)
{
    return DdReleaseInterface(DdFromCom(Com));
}

static
HRESULT
STDMETHODCALLTYPE
DdCompact(
    _In_ VOID* Com)
{
    DdInterface* This = DdFromCom(Com);

    DdLockScope Lock;

    if (This->Local->Driver->ExclusiveOwner != This->Local)
        return DDERR_NOEXCLUSIVEMODE;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdCreateClipper(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _Out_ LPDIRECTDRAWCLIPPER* Clipper,
    _In_opt_ IUnknown* Outer)
{
    DdInterface* This = DdFromCom(Com);

    DdLockScope Lock;

    return DdNewClipper(This->Local->Driver, Flags, Clipper, Outer, TRUE, This);
}

static
HRESULT
STDMETHODCALLTYPE
DdCreatePalette(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_ LPPALETTEENTRY Entries,
    _Out_ LPDIRECTDRAWPALETTE* Palette,
    _In_opt_ IUnknown* Outer)
{
    return DdNewPalette(DdFromCom(Com), Flags, Entries, Palette, Outer);
}

static
HRESULT
STDMETHODCALLTYPE
DdCreateSurface(
    _In_ VOID* Com,
    _In_ VOID* Desc,
    _Out_ VOID** Surface,
    _In_opt_ IUnknown* Outer)
{
    DdLockScope Lock;

    return DdCreateSurfaceObject(DdFromCom(Com), static_cast<const DDSURFACEDESC2*>(Desc), Surface, Outer, TRUE);
}

static
HRESULT
STDMETHODCALLTYPE
DdDuplicateSurface(
    _In_ VOID* Com,
    _In_ VOID* Source,
    _Out_ VOID** Duplicate)
{
    DD_TRACE_ONCE(L"DuplicateSurface is not implemented yet");
    return DDERR_UNSUPPORTED;
}

/* Caps bits with no public meaning that EnumSurfaces refuses in a description */
static constexpr DWORD DdEnumUnnamedCaps = 0x03000000;

/* The DDSCAPS2 and DDSCAPS3 bits a description may not ask for */
static constexpr DWORD DdEnumBadCaps2 = 0x00000001;
static constexpr DWORD DdEnumBadCaps3 = 0xFFFFE000;

static constexpr DWORD DdEnumKnownFlags = DDENUMSURFACES_ALL | DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH |
                                          DDENUMSURFACES_CANBECREATED | DDENUMSURFACES_DOESEXIST;

static constexpr DWORD DdDescColorKeys = DDSD_CKDESTOVERLAY | DDSD_CKDESTBLT | DDSD_CKSRCOVERLAY | DDSD_CKSRCBLT;

static
BOOLEAN
DdSameKey(
    _In_ const DDCOLORKEY* Mine,
    _In_ const DDCOLORKEY* Wanted)
{
    return memcmp(Mine, Wanted, sizeof(*Mine)) == 0;
}

/* Whether a surface agrees with every field the description names */
static
BOOLEAN
DdSurfaceMatches(
    _In_ const DdSurface* Surface,
    _In_ const DDSURFACEDESC2* Desc)
{
    DWORD Flags = Desc->dwFlags;

    if ((Flags & DDSD_CAPS) != 0 && memcmp(&Surface->Caps, &Desc->ddsCaps, sizeof(Surface->Caps)) != 0)
        return FALSE;
    if ((Flags & DDSD_HEIGHT) != 0 && Surface->Height != Desc->dwHeight)
        return FALSE;
    if ((Flags & DDSD_WIDTH) != 0 && Surface->Width != Desc->dwWidth)
        return FALSE;
    if ((Flags & DDSD_CKDESTBLT) != 0 && !DdSameKey(&Surface->DestKey, &Desc->ddckCKDestBlt))
        return FALSE;
    if ((Flags & DDSD_CKSRCBLT) != 0 && !DdSameKey(&Surface->SourceKey, &Desc->ddckCKSrcBlt))
        return FALSE;
    if ((Flags & DDSD_BACKBUFFERCOUNT) != 0 && Surface->BackBufferCount != Desc->dwBackBufferCount)
        return FALSE;
    if ((Flags & DDSD_MIPMAPCOUNT) != 0 && Desc->dwMipMapCount != 0)
        return FALSE;
    if ((Flags & DDSD_PIXELFORMAT) != 0 &&
        (!Surface->KeepsFormat || memcmp(&Surface->Format, &Desc->ddpfPixelFormat, sizeof(Surface->Format)) != 0))
    {
        return FALSE;
    }

    /* Overlay keys only exist on overlays */
    if ((Surface->Caps.dwCaps & DDSCAPS_OVERLAY) == 0)
        return (Flags & (DDSD_CKDESTOVERLAY | DDSD_CKSRCOVERLAY)) == 0;
    if ((Flags & DDSD_CKDESTOVERLAY) != 0 && !DdSameKey(&Surface->DestOverlayKey, &Desc->ddckCKDestOverlay))
        return FALSE;
    if ((Flags & DDSD_CKSRCOVERLAY) != 0 && !DdSameKey(&Surface->SourceOverlayKey, &Desc->ddckCKSrcOverlay))
        return FALSE;
    return TRUE;
}

/* The live surface interface this object made that was created just before the given stamp */
_Ret_maybenull_
static
DdSurfaceInterface*
DdOlderSurfaceInterface(
    _In_ DdLocal* Local,
    _In_ ULONG Below)
{
    DdSurfaceInterface* Best = nullptr;

    for (DdSurface* Surface = Local->Driver->Surfaces; Surface != nullptr; Surface = Surface->Next)
    {
        if (Surface->Creator != Local)
            continue;
        for (DdSurfaceInterface* Interface = Surface->Interfaces; Interface != nullptr; Interface = Interface->Next)
        {
            if (Interface->RefCount == 0 || Interface->Stamp >= Below)
                continue;
            if (Best == nullptr || Interface->Stamp > Best->Stamp)
                Best = Interface;
        }
    }
    return Best;
}

static
HRESULT
DdEnumExistingSurfaces(
    _In_ DdInterface* This,
    _In_ DWORD Flags,
    _In_opt_ const DDSURFACEDESC2* Desc,
    _In_opt_ VOID* Context,
    _In_ LPDDENUMSURFACESCALLBACK7 Callback)
{
    UCHAR Version = DdIsOldDraw(This) ? 1 : This->Kind == DdKind::Draw4 ? 4 : 7;
    DdSurfaceInterface* Interface;
    DDSURFACEDESC2 Found;
    BOOLEAN Matches;
    VOID* Handed;
    ULONG Below = ULONG_MAX;

    for (;;)
    {
        Interface = DdOlderSurfaceInterface(This->Local, Below);
        if (Interface == nullptr)
            return S_OK;
        Below = Interface->Stamp;

        if ((Interface->Surface->Caps.dwCaps & DDSCAPS_RESERVED2) != 0)
            continue;
        if ((Flags & DDENUMSURFACES_ALL) == 0)
        {
            Matches = DdSurfaceMatches(Interface->Surface, Desc);
            if (Matches == ((Flags & DDENUMSURFACES_NOMATCH) != 0))
                continue;
        }

        DdDescribeSurface(Interface->Surface, &Found);
        if (Version == 1)
            Found.dwSize = sizeof(DDSURFACEDESC);
        Handed = DdHandOutSurface(Interface->Surface, Version);
        if (Callback(static_cast<LPDIRECTDRAWSURFACE7>(Handed), &Found, Context) == DDENUMRET_CANCEL)
            return S_OK;
    }
}

/* One size and one pixel format a creatable surface is tried with */
struct DdTrySize
{
    DWORD Width;
    DWORD Height;
};

struct DdTryFormat
{
    BOOLEAN IsFourCC;
    BOOLEAN IsRgb;
    DWORD FourCC;
    DWORD BitCount;
    DWORD Masks[4];
};

/* The RGB layout a display depth stands for when surfaces are tried at it */
static
VOID
DdDepthFormat(
    _In_ DWORD BitCount,
    _Out_ DdTryFormat* Format)
{
    static const DWORD Masks8[] = { 0xE0, 0x1C, 0x03, 0 };
    static const DWORD Masks16[] = { 0xF800, 0x07E0, 0x1F, 0 };
    static const DWORD Masks32[] = { 0xFF0000, 0xFF00, 0xFF, 0 };
    const DWORD* Masks = BitCount == 8 ? Masks8 : BitCount == 16 ? Masks16 : Masks32;

    ZeroMemory(Format, sizeof(*Format));
    Format->IsRgb = TRUE;
    Format->BitCount = BitCount;
    memcpy(Format->Masks, Masks, sizeof(Format->Masks));
}

static
VOID
DdAddTrySize(
    _Inout_ std::vector<DdTrySize>& Sizes,
    _In_ DWORD Width,
    _In_ DWORD Height)
{
    for (const DdTrySize& Size : Sizes)
    {
        if (Size.Width == Width && Size.Height == Height)
            return;
    }
    Sizes.push_back({ Width, Height });
}

/* Tries the description at every display size and depth it leaves open and reports each surface that could be made */
static
HRESULT
DdEnumCreatableSurfaces(
    _In_ DdInterface* This,
    _Inout_ DDSURFACEDESC2* Desc,
    _In_opt_ VOID* Context,
    _In_ LPDDENUMSURFACESCALLBACK7 Callback)
{
    DdDriver* Driver = This->Local->Driver;
    PCSTR Name = _stricmp(Driver->Name, "DISPLAY") == 0 ? nullptr : Driver->Name;
    BOOLEAN Primary = (Desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    BOOLEAN EverySize = (Desc->dwFlags & DDSD_HEIGHT) == 0 && !Primary;
    BOOLEAN WantRgb = FALSE;
    BOOLEAN WantFourCC = FALSE;
    std::vector<DdTrySize> Sizes;
    std::vector<DdTryFormat> Formats;
    DdTryFormat Format;
    DDSURFACEDESC2 Found;
    DEVMODEA Mode;
    VOID* Created;

    if ((Desc->dwFlags & DDSD_PIXELFORMAT) != 0)
    {
        WantFourCC = (Desc->ddpfPixelFormat.dwFlags & DDPF_YUV) != 0;
        WantRgb = (Desc->ddpfPixelFormat.dwFlags & DDPF_RGB) != 0;
    }

    /* Every mode offers its size at each of the four depths; the depths are tried once per mode, repeats included */
    if (EverySize || WantRgb)
    {
        for (DWORD Index = 0;; Index += 1)
        {
            ZeroMemory(&Mode, sizeof(Mode));
            Mode.dmSize = sizeof(Mode);
            if (!EnumDisplaySettingsA(Name, Index, &Mode))
                break;
            if (EverySize)
                DdAddTrySize(Sizes, Mode.dmPelsWidth, Mode.dmPelsHeight);
            for (DWORD Depth = 8; WantRgb && Depth <= 32; Depth += 8)
            {
                DdDepthFormat(Depth, &Format);
                Formats.push_back(Format);
            }
        }
    }
    if (!EverySize)
        Sizes.push_back({ Desc->dwWidth, Desc->dwHeight });

    for (DWORD Index = 0; WantFourCC && Index < Driver->FourCCCount; Index += 1)
    {
        ZeroMemory(&Format, sizeof(Format));
        Format.IsFourCC = TRUE;
        Format.FourCC = Driver->FourCC[Index];
        Formats.push_back(Format);
    }

    /* Without a format to try the description's own is used */
    if (Formats.empty())
    {
        ZeroMemory(&Format, sizeof(Format));
        Formats.push_back(Format);
    }

    if (!Primary)
        Desc->dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;

    for (const DdTrySize& Size : Sizes)
    {
        Desc->dwWidth = Size.Width;
        Desc->dwHeight = Size.Height;
        for (const DdTryFormat& Try : Formats)
        {
            if (Try.IsFourCC)
            {
                Desc->ddpfPixelFormat.dwFlags = DDPF_YUV;
                Desc->ddpfPixelFormat.dwFourCC = Try.FourCC;
            }
            else if (Try.IsRgb)
            {
                Desc->ddpfPixelFormat.dwFlags = Try.BitCount == 8 ? DDPF_RGB | DDPF_PALETTEINDEXED8 : DDPF_RGB;
                Desc->ddpfPixelFormat.dwRGBBitCount = Try.BitCount;
                Desc->ddpfPixelFormat.dwRBitMask = Try.Masks[0];
                Desc->ddpfPixelFormat.dwGBitMask = Try.Masks[1];
                Desc->ddpfPixelFormat.dwBBitMask = Try.Masks[2];
                Desc->ddpfPixelFormat.dwRGBAlphaBitMask = Try.Masks[3];
            }

            /* The old interfaces create from the short description they were given */
            if (DdIsOldDraw(This))
                Desc->dwSize = sizeof(DDSURFACEDESC);
            Created = nullptr;
            HRESULT hr = DdCreateSurfaceObject(This, Desc, &Created, nullptr, FALSE);
            Desc->dwSize = sizeof(*Desc);
            if (FAILED(hr))
                continue;

            DdDescribeSurface(DdSurfaceFromCom(Created), &Found);
            if (DdIsOldDraw(This))
                Found.dwSize = sizeof(DDSURFACEDESC);
            Callback(nullptr, &Found, Context);
            static_cast<IUnknown*>(Created)->Release();
        }
    }
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdEnumSurfaces(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Desc,
    _In_opt_ VOID* Context,
    _In_opt_ VOID* Callback)
{
    DdInterface* This = DdFromCom(Com);
    const DDSURFACEDESC2* Wanted = nullptr;
    DDSURFACEDESC2 Widened;

    DdLockScope Lock;

    /* The old interfaces widen their short description into the long one */
    if (Desc != nullptr)
    {
        if (DdIsOldDraw(This))
        {
            if (static_cast<const DDSURFACEDESC*>(Desc)->dwSize != sizeof(DDSURFACEDESC))
                return E_INVALIDARG;
            ZeroMemory(&Widened, sizeof(Widened));
            memcpy(&Widened, Desc, sizeof(DDSURFACEDESC));
            Widened.dwSize = sizeof(Widened);
            Wanted = &Widened;
        }
        else
        {
            Wanted = static_cast<const DDSURFACEDESC2*>(Desc);
        }
    }

    if (Callback == nullptr || (Flags & ~DdEnumKnownFlags) != 0)
        return E_INVALIDARG;
    if (Wanted != nullptr && Wanted->dwSize != sizeof(DDSURFACEDESC2))
        return E_INVALIDARG;

    if ((Flags & DDENUMSURFACES_ALL) != 0)
    {
        if ((Flags & (DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH)) != 0)
            return E_INVALIDARG;
    }
    else if (Wanted == nullptr ||
             (Flags & (DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH)) == (DDENUMSURFACES_MATCH | DDENUMSURFACES_NOMATCH))
    {
        return E_INVALIDARG;
    }
    if ((Flags & DDENUMSURFACES_CANBECREATED) != 0 &&
        ((Flags & DDENUMSURFACES_MATCH) == 0 || (Flags & (DDENUMSURFACES_ALL | DDENUMSURFACES_NOMATCH)) != 0))
    {
        return E_INVALIDARG;
    }

    if (Wanted != nullptr)
    {
        if ((Wanted->dwFlags & DDSD_LPSURFACE) != 0 || (Wanted->ddsCaps.dwCaps & DdEnumUnnamedCaps) != 0)
            return E_INVALIDARG;
        if ((Wanted->ddsCaps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
            return S_OK;
        if ((Wanted->ddsCaps.dwCaps2 & DdEnumBadCaps2) != 0 || (Wanted->ddsCaps.dwCaps3 & DdEnumBadCaps3) != 0 ||
            Wanted->ddsCaps.dwCaps4 != 0)
        {
            return DDERR_INVALIDCAPS;
        }
        if ((Wanted->ddsCaps.dwCaps & DDSCAPS_RESERVED2) != 0)
            return E_INVALIDARG;
        if (((Wanted->dwFlags & DDSD_HEIGHT) != 0) != ((Wanted->dwFlags & DDSD_WIDTH) != 0))
            return E_INVALIDARG;
        if ((Flags & DDENUMSURFACES_CANBECREATED) != 0 && (Wanted->dwFlags & DdDescColorKeys) != 0)
            return E_INVALIDARG;
    }

    /* The tries write their sizes and formats into the description the caller passed */
    if ((Flags & DDENUMSURFACES_CANBECREATED) != 0)
    {
        return DdEnumCreatableSurfaces(This, const_cast<DDSURFACEDESC2*>(Wanted), Context,
                                       reinterpret_cast<LPDDENUMSURFACESCALLBACK7>(Callback));
    }

    if ((Flags & DDENUMSURFACES_DOESEXIST) == 0)
        return E_INVALIDARG;
    return DdEnumExistingSurfaces(This, Flags, Wanted, Context, reinterpret_cast<LPDDENUMSURFACESCALLBACK7>(Callback));
}

static
HRESULT
STDMETHODCALLTYPE
DdFlipToGdiSurface(
    _In_ VOID* Com)
{
    DdLockScope Lock;

    /* Without a primary surface there is nothing to flip back */
    return DDERR_NOTFOUND;
}

static
VOID
DdBuildCaps(
    _In_ const DDCORECAPS* Core,
    _In_ const DDNONLOCALVIDMEMCAPS* NonLocal,
    _In_ const DDSCAPSEX* More,
    _Out_ DDCAPS_DX7* Caps)
{
    ZeroMemory(Caps, sizeof(*Caps));
    memcpy(Caps, Core, sizeof(*Core));
    Caps->dwNLVBCaps = NonLocal->dwNLVBCaps;
    Caps->dwNLVBCaps2 = NonLocal->dwNLVBCaps2;
    Caps->dwNLVBCKeyCaps = NonLocal->dwNLVBCKeyCaps;
    Caps->dwNLVBFXCaps = NonLocal->dwNLVBFXCaps;
    memcpy(Caps->dwNLVBRops, NonLocal->dwNLVBRops, sizeof(Caps->dwNLVBRops));
    Caps->ddsCaps.dwCaps = Core->ddsCaps.dwCaps;
    Caps->ddsCaps.dwCaps2 = More->dwCaps2;
    Caps->ddsCaps.dwCaps3 = More->dwCaps3;
    Caps->ddsCaps.dwCaps4 = More->dwCaps4;
    Caps->dwSize = sizeof(*Caps);
}

static
BOOLEAN
DdIsCapsSize(
    _In_ DWORD Size)
{
    return Size == sizeof(DDCAPS_DX1) || Size == sizeof(DDCAPS_DX3) || Size == sizeof(DDCAPS_DX5) ||
           Size == sizeof(DDCAPS_DX7);
}

static
VOID
DdCopyCaps(
    _In_ const DDCAPS_DX7* Source,
    _Inout_ LPDDCAPS Target)
{
    DWORD Size = Target->dwSize;

    ZeroMemory(Target, Size);
    memcpy(Target, Source, Size);
    Target->dwSize = Size;

    /* The private surface bit never reaches applications */
    if (Size >= sizeof(DDCAPS_DX7))
        reinterpret_cast<DDCAPS_DX7*>(Target)->ddsCaps.dwCaps &= ~DDSCAPS_RESERVED2;
    reinterpret_cast<DDCAPS_DX1*>(Target)->ddsCaps.dwCaps &= ~DDSCAPS_RESERVED2;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetCaps(
    _In_ VOID* Com,
    _Inout_opt_ LPDDCAPS DriverCaps,
    _Inout_opt_ LPDDCAPS HelCaps)
{
    DdInterface* This = DdFromCom(Com);
    DdDriver* Driver;
    DDCAPS_DX7 Caps;
    DDSCAPS2 Memory = {};

    DdLockScope Lock;

    Driver = This->Local->Driver;
    if (DriverCaps == nullptr && HelCaps == nullptr)
        return E_INVALIDARG;
    if (DriverCaps != nullptr && !DdIsCapsSize(DriverCaps->dwSize))
        return E_INVALIDARG;
    if (HelCaps != nullptr && !DdIsCapsSize(HelCaps->dwSize))
        return E_INVALIDARG;

    if (DriverCaps != nullptr)
    {
        DdBuildCaps(&Driver->HalCaps, &Driver->HalNonLocalCaps, &Driver->HalMoreCaps, &Caps);
        DdCopyCaps(&Caps, DriverCaps);

        /* This goes through the IDirectDraw2 rules, which report nothing for an emulated driver */
        Memory.dwCaps = DDSCAPS_VIDEOMEMORY;
        if ((Driver->Flags & DDRAWI_NOHARDWARE) == 0)
            DdGetAvailableMemory(This->Local, &Memory, &DriverCaps->dwVidMemTotal, &DriverCaps->dwVidMemFree);

        if (!DdState.EnableOverlays)
        {
            DriverCaps->dwCaps &= ~DDCAPS_OVERLAY;
            DriverCaps->dwCaps2 &= ~DDCAPS2_COLORCONTROLOVERLAY;
        }
    }

    if (HelCaps != nullptr)
    {
        DdBuildCaps(&Driver->HelCaps, &Driver->HelNonLocalCaps, &Driver->HelMoreCaps, &Caps);
        DdCopyCaps(&Caps, HelCaps);
        HelCaps->dwVidMemFree = 0;
    }

    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetDisplayMode(
    _In_ VOID* Com,
    _Inout_ VOID* Out)
{
    DdInterface* This = DdFromCom(Com);
    DDSURFACEDESC2* Desc = static_cast<DDSURFACEDESC2*>(Out);
    DdDriver* Driver;
    HDC Dc;

    DdLockScope Lock;

    if (Desc == nullptr || (Desc->dwSize != sizeof(DDSURFACEDESC2) && Desc->dwSize != sizeof(DDSURFACEDESC)))
        return E_INVALIDARG;

    Driver = This->Local->Driver;
    ZeroMemory(Desc, Desc->dwSize);
    Desc->dwSize = DdIsOldDraw(This) ? sizeof(DDSURFACEDESC) : sizeof(DDSURFACEDESC2);
    Desc->dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    Desc->dwWidth = Driver->Mode.Width;
    Desc->dwHeight = Driver->Mode.Height;
    Desc->dwRefreshRate = Driver->Mode.RefreshRate;

    Dc = DdCreateDisplayDc(Driver->Name);
    DdFillPixelFormat(&Desc->ddpfPixelFormat, Dc, 0);
    if (Dc != NULL)
        DeleteDC(Dc);
    Desc->lPitch = (Desc->ddpfPixelFormat.dwRGBBitCount * Desc->dwWidth) >> 3;
    return S_OK;
}

struct DdModeEntry
{
    WORD Width;
    WORD Height;
    BYTE BitCount;
    WORD RefreshRate;
};

static
BOOLEAN
DdReportMode(
    _In_ DdInterface* This,
    _In_ LPDDENUMMODESCALLBACK2 Callback,
    _In_opt_ VOID* Context,
    _In_ const DdModeEntry& Entry,
    _In_ DWORD Flags,
    _In_ BOOLEAN ModeX)
{
    DDSURFACEDESC2 Desc = {};

    Desc.dwSize = DdIsOldDraw(This) ? sizeof(DDSURFACEDESC) : sizeof(DDSURFACEDESC2);
    Desc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    Desc.dwHeight = Entry.Height;
    Desc.dwWidth = Entry.Width;
    Desc.lPitch = (Entry.BitCount * Entry.Width) >> 3;
    DdFillPixelFormat(&Desc.ddpfPixelFormat, NULL, Entry.BitCount);
    Desc.dwRefreshRate = (Flags & DDEDM_REFRESHRATES) != 0 ? Entry.RefreshRate : 0;
    if (ModeX)
        Desc.ddsCaps.dwCaps |= DDSCAPS_MODEX;

    /* Any answer but an error keeps the enumeration going, as the system runtime does */
    return Callback(&Desc, Context) >= 0;
}

static
HRESULT
DdEnumModes(
    _In_ DdInterface* This,
    _In_ DWORD Flags,
    _In_opt_ const DDSURFACEDESC2* Filter,
    _In_opt_ VOID* Context,
    _In_opt_ LPDDENUMMODESCALLBACK2 Callback)
{
    DdModeEntry* Entries;
    DdModeEntry* Grown;
    DdModeEntry Entry;
    DEVMODEA Mode;
    PCSTR Name;
    ULONG Capacity = 256;
    ULONG Count = 0;
    ULONG Index;
    DWORD Number;
    DWORD BitCount;
    BOOLEAN Has320x200 = FALSE;
    BOOLEAN Has320x240 = FALSE;
    BOOLEAN Has640x480 = FALSE;
    BOOLEAN Duplicate;

    DdLockScope Lock;

    if ((Filter != nullptr && Filter->dwSize != sizeof(DDSURFACEDESC2)) || (Flags & ~(DDEDM_REFRESHRATES | DDEDM_STANDARDVGAMODES)) != 0 ||
        Callback == nullptr)
    {
        return E_INVALIDARG;
    }

    Entries = static_cast<DdModeEntry*>(LocalAlloc(LMEM_FIXED, Capacity * sizeof(*Entries)));
    if (Entries == nullptr)
        return E_FAIL;

    Name = _stricmp(This->Local->Driver->Name, "DISPLAY") == 0 ? nullptr : This->Local->Driver->Name;
    for (Number = 0;; Number++)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsA(Name, Number, &Mode))
            break;

        if (_stricmp(reinterpret_cast<PCSTR>(Mode.dmDeviceName), "MODEX") == 0 ||
            _stricmp(reinterpret_cast<PCSTR>(Mode.dmDeviceName), "VGA") == 0 || Mode.dmBitsPerPel < 8)
        {
            continue;
        }

        if (Mode.dmBitsPerPel == 8)
        {
            if (Mode.dmPelsWidth == 320 && Mode.dmPelsHeight == 200)
                Has320x200 = TRUE;
            if (Mode.dmPelsWidth == 320 && Mode.dmPelsHeight == 240)
                Has320x240 = TRUE;
            if (Mode.dmPelsWidth == 640 && Mode.dmPelsHeight == 480)
                Has640x480 = TRUE;
        }

        BitCount = Mode.dmBitsPerPel == 15 ? 16 : Mode.dmBitsPerPel;
        if (Filter != nullptr)
        {
            if (((Filter->dwFlags & DDSD_WIDTH) != 0 && Mode.dmPelsWidth != Filter->dwWidth) ||
                ((Filter->dwFlags & DDSD_HEIGHT) != 0 && Mode.dmPelsHeight != Filter->dwHeight) ||
                ((Filter->dwFlags & DDSD_PIXELFORMAT) != 0 && BitCount != Filter->ddpfPixelFormat.dwRGBBitCount) ||
                ((Filter->dwFlags & DDSD_REFRESHRATE) != 0 && Mode.dmDisplayFrequency != Filter->dwRefreshRate))
            {
                continue;
            }
        }

        Duplicate = FALSE;
        for (Index = 0; Index < Count && !Duplicate; Index++)
        {
            Duplicate = Entries[Index].Width == Mode.dmPelsWidth && Entries[Index].Height == Mode.dmPelsHeight &&
                        Entries[Index].BitCount == BitCount &&
                        ((Flags & DDEDM_REFRESHRATES) == 0 || Entries[Index].RefreshRate == Mode.dmDisplayFrequency);
        }
        if (Duplicate)
            continue;

        Entries[Count].Width = static_cast<WORD>(Mode.dmPelsWidth);
        Entries[Count].Height = static_cast<WORD>(Mode.dmPelsHeight);
        Entries[Count].BitCount = static_cast<BYTE>(BitCount);
        Entries[Count].RefreshRate = (Flags & DDEDM_REFRESHRATES) != 0 ? static_cast<WORD>(Mode.dmDisplayFrequency) : 0;
        if (++Count < Capacity)
            continue;

        if (Capacity >= 0x2000)
        {
            LocalFree(Entries);
            return E_FAIL;
        }
        Grown = static_cast<DdModeEntry*>(LocalAlloc(LMEM_FIXED, 2 * Capacity * sizeof(*Entries)));
        if (Grown == nullptr)
        {
            LocalFree(Entries);
            return E_FAIL;
        }
        memcpy(Grown, Entries, Count * sizeof(*Entries));
        LocalFree(Entries);
        Entries = Grown;
        Capacity *= 2;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (!DdReportMode(This, Callback, Context, Entries[Index], Flags, FALSE))
            break;
    }

    /* Applications that asked for ModeX also see the low resolution palettized modes */
    if ((This->Local->Flags & DDRAWILCL_ALLOWMODEX) != 0 && Has640x480)
    {
        Entry = { 320, 200, 8, DdModeXRefresh };
        if (Has320x200 || DdReportMode(This, Callback, Context, Entry, Flags, TRUE))
        {
            Entry.Height = 240;
            if (!Has320x240)
                DdReportMode(This, Callback, Context, Entry, Flags, TRUE);
        }
    }

    LocalFree(Entries);
    return S_OK;
}

struct DdOldModeContext
{
    LPDDENUMMODESCALLBACK Callback;
    VOID* Context;
};

static
HRESULT
WINAPI
DdForwardOldMode(
    _In_ LPDDSURFACEDESC2 Desc,
    _In_opt_ VOID* Context)
{
    DdOldModeContext* Old = static_cast<DdOldModeContext*>(Context);

    return Old->Callback(reinterpret_cast<LPDDSURFACEDESC>(Desc), Old->Context);
}

static
HRESULT
STDMETHODCALLTYPE
DdEnumDisplayModes(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ LPDDSURFACEDESC Filter,
    _In_opt_ VOID* Context,
    _In_opt_ LPDDENUMMODESCALLBACK Callback)
{
    DdOldModeContext Old = { Callback, Context };
    DDSURFACEDESC2 Wide = {};

    if (Filter != nullptr)
    {
        if (Filter->dwSize != sizeof(DDSURFACEDESC))
            return E_INVALIDARG;
        memcpy(&Wide, Filter, sizeof(*Filter));
        Wide.dwSize = sizeof(Wide);
    }

    return DdEnumModes(DdFromCom(Com), Flags, Filter != nullptr ? &Wide : nullptr, &Old,
                       Callback != nullptr ? DdForwardOldMode : nullptr);
}

static
HRESULT
STDMETHODCALLTYPE
DdEnumDisplayModes4(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ LPDDSURFACEDESC2 Filter,
    _In_opt_ VOID* Context,
    _In_opt_ LPDDENUMMODESCALLBACK2 Callback)
{
    return DdEnumModes(DdFromCom(Com), Flags, Filter, Context, Callback);
}

static
HRESULT
STDMETHODCALLTYPE
DdGetFourCCCodes(
    _In_ VOID* Com,
    _Inout_ DWORD* Count,
    _Out_writes_opt_(*Count) DWORD* Codes)
{
    DdInterface* This = DdFromCom(Com);
    DdDriver* Driver;
    DWORD Available;
    DWORD Index;

    DdLockScope Lock;

    if (Count == nullptr)
        return E_INVALIDARG;

    Driver = This->Local->Driver;
    Available = Driver->FourCCCount;
    if (Codes != nullptr)
    {
        if (*Count < Available)
            Available = *Count;
        *Count = Available;
        for (Index = 0; Index < Available; Index++)
            Codes[Index] = Driver->FourCC[Index];
    }
    else
    {
        *Count = Available;
    }

    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetGdiSurface(
    _In_ VOID* Com,
    _Out_ VOID** Surface)
{
    DdLockScope Lock;

    if (Surface == nullptr)
        return E_INVALIDARG;
    *Surface = nullptr;
    return DDERR_NOTFOUND;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetMonitorFrequency(
    _In_ VOID* Com,
    _Out_ DWORD* Frequency)
{
    DdInterface* This = DdFromCom(Com);
    DWORD Value;

    DdLockScope Lock;

    if (Frequency == nullptr)
        return E_INVALIDARG;

    Value = This->Local->Driver->MonitorFrequency;
    if (Value == 0)
        return E_NOTIMPL;

    *Frequency = Value;
    return S_OK;
}

/* Reads the scan line; a vertical blank reports line 0 with its own error */
static
HRESULT
DdReadScanLine(
    _In_ DdDriver* Driver,
    _Out_ DWORD* Line)
{
    const KmtThunks* Kmt = RxGetKmtThunks();
    D3DKMT_GETSCANLINE Scan = {};
    NTSTATUS Status;

    *Line = 0;
    if (Driver->Hardware == nullptr || Kmt == nullptr)
        return E_NOTIMPL;

    Scan.hAdapter = Driver->Hardware->AdapterHandle;
    Scan.VidPnSourceId = Driver->Hardware->VidPnSourceId;
    Status = D3DKMTGetScanLine(&Scan);
    if (Status == STATUS_DEVICE_REMOVED)
    {
        DdTrace(L"device removed while reading the scan line");
        return DDERR_SURFACELOST;
    }
    if (!NT_SUCCESS(Status))
        return DdHresultFromStatus(Status);

    if (Scan.InVerticalBlank)
        return DDERR_VERTICALBLANKINPROGRESS;

    *Line = Scan.ScanLine;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetScanLine(
    _In_ VOID* Com,
    _Out_ DWORD* Line)
{
    DdInterface* This = DdFromCom(Com);

    DdLockScope Lock;

    return DdReadScanLine(This->Local->Driver, Line);
}

static
HRESULT
STDMETHODCALLTYPE
DdGetVerticalBlankStatus(
    _In_ VOID* Com,
    _Out_ BOOL* InBlank)
{
    DdInterface* This = DdFromCom(Com);
    DWORD Line;
    HRESULT hr;

    DdLockScope Lock;

    hr = DdReadScanLine(This->Local->Driver, &Line);
    if (hr == DDERR_VERTICALBLANKINPROGRESS)
    {
        *InBlank = TRUE;
    }
    else if (FAILED(hr))
    {
        /* Without a scan line the answer alternates so polling loops still finish */
        *InBlank = DdState.BlankToggle;
        DdState.BlankToggle = !DdState.BlankToggle;
    }
    else
    {
        *InBlank = FALSE;
    }

    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdInitialize(
    _In_ VOID* Com,
    _In_opt_ GUID* Guid)
{
    DdInterface* This = DdFromCom(Com);
    DdInterface* Created;
    BOOLEAN WasInitialized;
    HRESULT hr;

    DdLockScope Lock;

    if (This->Local->Driver != nullptr)
        return DDERR_ALREADYINITIALIZED;

    WasInitialized = This->Initialized;
    This->Initialized = TRUE;
    This->Vtbl = DdVtblFor(This->Kind, TRUE);

    hr = DdCreateObject(Guid, &Created, This, This->Kind == DdKind::Draw7 ? DDRAWILCL_DIRECTDRAW7 : 0);
    if (FAILED(hr) && !WasInitialized)
    {
        This->Initialized = FALSE;
        This->Vtbl = DdVtblFor(This->Kind, FALSE);
    }

    return hr;
}

static
HRESULT
DdChangeMode(
    _In_ DdLocal* Local,
    _In_opt_ const DEVMODEA* Mode)
{
    DdDriver* Driver = Local->Driver;
    HANDLE Transition;
    PCSTR Name;
    LONG Result;

    Name = _stricmp(Driver->Name, "DISPLAY") == 0 ? nullptr : Driver->Name;

    /* The compositor signals this event when it is ready for the new mode */
    Transition = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, L"DWM_DX_FULLSCREEN_TRANSITION_EVENT");
    if (Transition != NULL && !ResetEvent(Transition))
    {
        CloseHandle(Transition);
        Transition = NULL;
    }

    Result = ChangeDisplaySettingsExA(Name, const_cast<DEVMODEA*>(Mode), NULL, CDS_FULLSCREEN, NULL);
    if (Transition != NULL)
    {
        WaitForSingleObject(Transition, 3000);
        CloseHandle(Transition);
    }

    if (Result != DISP_CHANGE_SUCCESSFUL)
        DdTrace(L"ChangeDisplaySettingsEx failed %ld", Result);
    return Result == DISP_CHANGE_SUCCESSFUL ? S_OK : E_NOTIMPL;
}

static
BOOLEAN
DdModeHasRefresh(
    _In_ DdDriver* Driver,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BitCount,
    _In_ DWORD Refresh)
{
    DEVMODEA Mode;
    DWORD Number;

    for (Number = 0;; Number++)
    {
        ZeroMemory(&Mode, sizeof(Mode));
        Mode.dmSize = sizeof(Mode);
        if (!EnumDisplaySettingsA(Driver->DeviceName, Number, &Mode))
            return FALSE;

        if (Mode.dmPelsWidth == Width && Mode.dmPelsHeight == Height && Mode.dmBitsPerPel == BitCount &&
            Mode.dmDisplayFrequency == Refresh)
        {
            return TRUE;
        }
    }
}

static
DWORD
DdPickRefresh(
    _In_ DdDriver* Driver,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD Wanted,
    _In_ DWORD BitCount)
{
    DEVMODEA Registry = {};

    if ((DdState.RegFlags & DdRegForceRefreshRate) != 0 &&
        DdModeHasRefresh(Driver, Width, Height, BitCount, DdState.ForcedRefreshRate))
    {
        return DdState.ForcedRefreshRate;
    }

    if (Wanted != 0)
        return Wanted;

    /* Keep the rate the desktop was set up with when it fits, then try the common ones */
    Registry.dmSize = sizeof(Registry);
    EnumDisplaySettingsA(Driver->DeviceName, ENUM_REGISTRY_SETTINGS, &Registry);
    if (Width <= Registry.dmPelsWidth && Height <= Registry.dmPelsHeight &&
        DdModeHasRefresh(Driver, Width, Height, BitCount, Registry.dmDisplayFrequency) && Registry.dmDisplayFrequency != 0)
    {
        return Registry.dmDisplayFrequency;
    }

    if (DdModeHasRefresh(Driver, Width, Height, BitCount, 75))
        return 75;
    if (DdModeHasRefresh(Driver, Width, Height, BitCount, 60))
        return 60;
    return 0;
}

static
HRESULT
DdSetMode(
    _In_ DdLocal* Local,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BitCount,
    _In_ DWORD Refresh)
{
    DdDriver* Driver = Local->Driver;
    DEVMODEA Mode = {};
    DWORD Picked;
    HRESULT hr;

    Local->Flags |= DDRAWILCL_MODEHASBEENCHANGED;

    Mode.dmSize = sizeof(Mode);
    Mode.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
    Mode.dmBitsPerPel = BitCount;
    if (BitCount == 16 && (Driver->Mode.Flags & 0x0100) != 0)
        Mode.dmBitsPerPel = 15;
    Mode.dmPelsWidth = Width;
    Mode.dmPelsHeight = Height;

    Picked = DdPickRefresh(Driver, Width, Height, Refresh, Mode.dmBitsPerPel);
    if (Picked != 0)
    {
        Mode.dmDisplayFrequency = Picked;
        Mode.dmFields |= DM_DISPLAYFREQUENCY;
    }

    Driver->Flags &= ~DDRAWI_MODEX;
    hr = DdChangeMode(Local, &Mode);
    if (FAILED(hr))
    {
        /* ModeX requests fall back to 640x480 when the low resolution mode is missing */
        if ((Local->Flags & DDRAWILCL_ALLOWMODEX) == 0 || BitCount != 8 || Width != 320 || (Height != 240 && Height != 200))
            return E_NOTIMPL;

        Mode.dmFields &= ~DM_DISPLAYFREQUENCY;
        Mode.dmPelsWidth = 640;
        Mode.dmPelsHeight = 480;
        if (FAILED(DdChangeMode(Local, &Mode)))
            return E_NOTIMPL;
        Driver->Flags |= DdDriverModeX;
    }

    DdState.DisplayUniqueness = DdQueryDisplayUniqueness();
    DdResetAllObjects(FALSE);
    Local->Flags |= DDRAWILCL_MODEHASBEENCHANGED | DDRAWILCL_DIRTYDC;
    DdTrace(L"display mode set to %lux%lu at %lu bits", Width, Height, BitCount);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdSetDisplayMode2(
    _In_ VOID* Com,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BitCount,
    _In_ DWORD Refresh,
    _In_ DWORD Flags)
{
    DdInterface* This = DdFromCom(Com);
    DdLocal* Local = This->Local;
    BOOLEAN Owned;
    BOOLEAN Mine;
    HRESULT hr;

    DdLockScope Lock;

    if ((Flags & ~DDSDM_STANDARDVGAMODE) != 0)
        return E_INVALIDARG;

    DdCheckExclusive(Local, &Owned, &Mine);
    if (Owned && !Mine)
        return DDERR_NOEXCLUSIVEMODE;

    hr = DdSetMode(Local, static_cast<WORD>(Width), static_cast<WORD>(Height), static_cast<BYTE>(BitCount),
                   static_cast<WORD>(Refresh));
    if (SUCCEEDED(hr))
        Local->SavedMode = Local->Driver->ModeKey;
    return hr;
}

static
HRESULT
STDMETHODCALLTYPE
DdSetDisplayMode(
    _In_ VOID* Com,
    _In_ DWORD Width,
    _In_ DWORD Height,
    _In_ DWORD BitCount)
{
    return DdSetDisplayMode2(Com, Width, Height, BitCount, 0, 0);
}

/* Puts the registry mode back once SetDisplayMode was tried, even when that attempt failed */
HRESULT
DdRestoreMode(
    _In_ DdLocal* Local)
{
    Local->Driver->Flags &= ~DdDriverModeX;
    if ((Local->Flags & DDRAWILCL_MODEHASBEENCHANGED) == 0)
        return S_OK;

    if (FAILED(DdChangeMode(Local, nullptr)))
        return E_NOTIMPL;

    DdState.DisplayUniqueness = DdQueryDisplayUniqueness();
    DdResetAllObjects(FALSE);
    Local->Flags = (Local->Flags & ~DDRAWILCL_MODEHASBEENCHANGED) | DDRAWILCL_DIRTYDC;
    DdTrace(L"display mode restored");
    RedrawWindow(NULL, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdRestoreDisplayMode(
    _In_ VOID* Com)
{
    DdInterface* This = DdFromCom(Com);
    BOOLEAN Owned;
    BOOLEAN Mine;

    DdLockScope Lock;

    DdCheckExclusive(This->Local, &Owned, &Mine);
    if (Owned && !Mine)
        return DDERR_NOEXCLUSIVEMODE;
    return DdRestoreMode(This->Local);
}

static
HRESULT
STDMETHODCALLTYPE
DdSetCooperativeLevel(
    _In_ VOID* Com,
    _In_opt_ HWND Window,
    _In_ DWORD Flags)
{
    DdInterface* This = DdFromCom(Com);
    DdLocal* Local = This->Local;
    DdDriver* Driver = Local->Driver;
    BOOLEAN MaxWindowed;
    BOOLEAN Owned = FALSE;
    BOOLEAN Mine = FALSE;
    HWND Previous;
    LONG Style;

    MaxWindowed = (Flags & (DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)) != 0 && !DdState.DisableMaxWindowedMode;

    DdLockScope Lock;

    if ((Flags & 0xFFFF0000) != 0 || (Flags & (DDSCL_FPUSETUP | DDSCL_FPUPRESERVE)) == (DDSCL_FPUSETUP | DDSCL_FPUPRESERVE))
        return E_INVALIDARG;

    /* Displays outside the desktop never move the application's windows */
    if ((Driver->Flags & DdDriverAttached) == 0)
        Flags |= DDSCL_NOWINDOWCHANGES;

    if ((Flags & (DDSCL_NORMAL | DDSCL_EXCLUSIVE | DDSCL_SETFOCUSWINDOW)) == 0 ||
        ((Flags & DDSCL_EXCLUSIVE) != 0 && (Flags & DDSCL_FULLSCREEN) == 0) ||
        ((Flags & DDSCL_ALLOWMODEX) != 0 && (Flags & DDSCL_FULLSCREEN) == 0) ||
        (Window != NULL && !IsWindow(Window)) || (Flags & (DDSCL_EXCLUSIVE | 0x20)) == (DDSCL_EXCLUSIVE | 0x20))
    {
        return E_INVALIDARG;
    }

    if ((Flags & DDSCL_SETFOCUSWINDOW) != 0)
    {
        if ((Flags & DDSCL_CREATEDEVICEWINDOW) == 0 &&
            (Flags & ~(DDSCL_SETFOCUSWINDOW | DDSCL_ALLOWMODEX | DDSCL_NOWINDOWCHANGES | 0x2000)) != 0)
        {
            return E_INVALIDARG;
        }
        if ((Local->Flags & DDRAWILCL_HASEXCLUSIVEMODE) != 0)
            return DDERR_HWNDALREADYSET;
    }

    if ((Flags & DDSCL_SETDEVICEWINDOW) != 0)
    {
        if ((Flags & (DDSCL_CREATEDEVICEWINDOW | DDSCL_SETFOCUSWINDOW)) != 0)
            return E_INVALIDARG;
        if (Window != NULL && Local->FocusWindow == NULL)
            return DDERR_NOFOCUSWINDOW;
    }

    if ((Flags & DDSCL_CREATEDEVICEWINDOW) != 0)
    {
        if ((Local->Window != NULL && (Local->Flags & DDRAWILCL_CREATEDWINDOW) == 0) || (Flags & DDSCL_EXCLUSIVE) == 0)
            return E_INVALIDARG;

        if (Local->Window == NULL && (Flags & DDSCL_SETFOCUSWINDOW) == 0)
        {
            if (Window != NULL)
                return E_INVALIDARG;
            if (Local->FocusWindow == NULL)
                return DDERR_NOFOCUSWINDOW;
        }
    }

    if ((Flags & DDSCL_EXCLUSIVE) != 0 && (Flags & DDSCL_CREATEDEVICEWINDOW) == 0)
    {
        if (Window == NULL || (GetWindowLongA(Window, GWL_STYLE) & WS_CHILD) != 0)
            return E_INVALIDARG;
    }

    if (This->Kind == DdKind::Draw1)
        Local->Flags |= DDRAWILCL_V1SCLBEHAVIOUR;

    if (Window != NULL && (Flags & (DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW)) == 0)
    {
        Style = GetWindowLongA(Window, GWL_STYLE);
        if ((Style & WS_CAPTION) == WS_DLGFRAME)
            Flags |= DDSCL_NOWINDOWCHANGES;
    }

    if ((Flags & DDSCL_EXCLUSIVE) != 0)
    {
        DdCheckExclusive(Local, &Owned, &Mine);
        if (Owned && !Mine)
            return DDERR_EXCLUSIVEMODEALREADYSET;
    }

    if ((Flags & DDSCL_SETFOCUSWINDOW) != 0)
    {
        Local->FocusWindow = Window;
        if ((Flags & DDSCL_CREATEDEVICEWINDOW) == 0)
        {
            if ((Flags & DDSCL_MULTITHREADED) != 0)
                Local->Flags |= DDRAWILCL_MULTITHREADED;
            if ((Flags & DDSCL_FPUSETUP) != 0)
                Local->Flags |= DDRAWILCL_FPUSETUP;
            if ((Flags & DDSCL_FPUPRESERVE) != 0)
                Local->Flags |= DDRAWILCL_FPUPRESERVE;
            return S_OK;
        }
    }

    if ((Flags & DDSCL_CREATEDEVICEWINDOW) != 0 && Local->Window == NULL)
    {
        Window = CreateWindowExA(0, "DirectDrawDeviceWnd", "DirectDrawDeviceWnd", WS_POPUP | WS_VISIBLE,
                                 Driver->DeviceRect.left, Driver->DeviceRect.top,
                                 Driver->DeviceRect.right - Driver->DeviceRect.left,
                                 Driver->DeviceRect.bottom - Driver->DeviceRect.top, Local->FocusWindow, NULL,
                                 DdState.Module, NULL);
        if (Window == NULL)
        {
            DdTrace(L"cannot create the device window (%lu)", GetLastError());
            return E_INVALIDARG;
        }
    }

    if (Window != Local->Window && IsWindow(Local->Window) && (Local->Flags & DDRAWILCL_CREATEDWINDOW) != 0)
    {
        DestroyWindow(Local->Window);
        Local->Flags &= ~DDRAWILCL_CREATEDWINDOW;
    }

    Previous = Local->Window;
    Local->Window = Window;
    if ((Flags & (DDSCL_SETDEVICEWINDOW | DDSCL_CREATEDEVICEWINDOW)) != 0)
    {
        if ((Flags & DDSCL_CREATEDEVICEWINDOW) != 0)
            Local->Flags |= DDRAWILCL_CREATEDWINDOW;
    }
    else
    {
        Local->FocusWindow = Window;
    }

    if ((Flags & DDSCL_ALLOWMODEX) != 0 && (Driver->Flags & DDRAWI_MODEXILLEGAL) == 0)
        Local->Flags |= DDRAWILCL_ALLOWMODEX;
    else
        Local->Flags &= ~DDRAWILCL_ALLOWMODEX;
    Local->MaxWindowedMode = MaxWindowed;

    if ((Flags & DDSCL_EXCLUSIVE) != 0)
    {
        if ((Flags & DDSCL_FULLSCREEN) != 0)
        {
            Driver->Flags |= DDRAWI_FULLSCREEN;
            Local->Flags |= DDRAWILCL_ISFULLSCREEN;
        }

        DD_TRACE_ONCE(L"exclusive mode does not hook the focus window yet");
        if (!Mine)
        {
            Driver->ExclusiveOwner = Local;
            Local->Flags = (Local->Flags | DDRAWILCL_ACTIVEYES) & ~DDRAWILCL_ACTIVENO;
            if (Window != NULL)
                SetForegroundWindow(Window);
            Local->Flags |= DDRAWILCL_HASEXCLUSIVEMODE;
            ClipCursor(&Driver->DeviceRect);
            DdTrace(L"exclusive mode taken by window %p", Window);
        }
    }
    else
    {
        if ((Local->Flags & DDRAWILCL_HASEXCLUSIVEMODE) != 0)
        {
            /* The first interface keeps its mode when it leaves exclusive mode */
            Driver->ExclusiveOwner = nullptr;
            if ((Local->Flags & DDRAWILCL_V1SCLBEHAVIOUR) == 0)
                DdRestoreMode(Local);
            ClipCursor(NULL);
            Local->Flags &= ~(DDRAWILCL_HASEXCLUSIVEMODE | DDRAWILCL_ISFULLSCREEN | DDRAWILCL_ALLOWMODEX);
            if ((Flags & DDSCL_NOWINDOWCHANGES) == 0 && IsWindow(Previous))
                SetWindowPos(Previous, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            DdTrace(L"exclusive mode released");
        }

        if (Driver->MonitorCount > 1 && (Driver->Flags & DDRAWI_DISPLAYDRV) != 0)
        {
            Driver->Flags |= DDRAWI_VIRTUALDESKTOP;
            DdUpdateDeviceRect(Driver);
        }
    }

    Local->Flags |= DDRAWILCL_SETCOOPCALLED;
    if ((Flags & DDSCL_MULTITHREADED) != 0)
        Local->Flags |= DDRAWILCL_MULTITHREADED;
    if ((Flags & DDSCL_FPUSETUP) != 0)
        Local->Flags |= DDRAWILCL_FPUSETUP;
    if ((Flags & DDSCL_FPUPRESERVE) != 0)
        Local->Flags |= DDRAWILCL_FPUPRESERVE;
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdWaitForVerticalBlank(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ HANDLE Event)
{
    DdInterface* This = DdFromCom(Com);
    DdDriver* Driver = This->Local->Driver;
    DWORD Line;
    HRESULT hr;

    DdLockScope Lock;

    if ((Flags & DDWAITVB_BLOCKBEGINEVENT) != 0 || Event != NULL)
        return E_NOTIMPL;
    if (Flags != DDWAITVB_BLOCKBEGIN && Flags != DDWAITVB_BLOCKEND)
        return E_INVALIDARG;
    if ((Driver->Flags & DdDriverModeX) != 0 || Driver->Hardware == nullptr)
        return E_NOTIMPL;

    /* Spin on the scan line until the blank starts, and for BLOCKEND until it ends */
    do
    {
        hr = DdReadScanLine(Driver, &Line);
    } while (hr == S_OK);

    if (hr == DDERR_VERTICALBLANKINPROGRESS && Flags == DDWAITVB_BLOCKEND)
    {
        do
        {
            hr = DdReadScanLine(Driver, &Line);
        } while (hr == DDERR_VERTICALBLANKINPROGRESS);
    }

    return S_OK;
}

HRESULT
DdGetAvailableMemory(
    _In_ DdLocal* Local,
    _In_ const DDSCAPS2* Caps,
    _Out_opt_ DWORD* Total,
    _Out_opt_ DWORD* Free)
{
    DdDriver* Driver = Local->Driver;
    DdHardware* Hardware = Driver->Hardware;
    DWORD LocalTotal = 0;
    DWORD LocalFree = 0;
    DWORD NonLocalTotal = 0;
    DWORD NonLocalFree = 0;
    DWORD FoundTotal = 0;
    DWORD FoundFree = 0;
    DWORD Primary;
    BOOLEAN AddShared;
    DEVMODEA Mode = {};

    if ((Caps->dwCaps & DdCapsBadForMemory) != 0 || (Caps->dwCaps2 & DDSCAPS2_HARDWAREDEINTERLACE) != 0 ||
        (Caps->dwCaps3 & 0xFFFFE000) != 0 || Caps->dwCaps4 != 0)
    {
        return DDERR_INVALIDCAPS;
    }

    if ((Caps->dwCaps & DdCapsNotMemory) != 0 || (Total == nullptr && Free == nullptr))
        return E_INVALIDARG;

    if (Total != nullptr)
        *Total = 0;
    if (Free != nullptr)
        *Free = 0;

    if (Hardware != nullptr &&
        ((Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) == 0 || (Driver->HalCaps.dwCaps2 & DDCAPS2_NONLOCALVIDMEM) != 0))
    {
        AddShared = (Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) != 0;
        if ((Caps->dwCaps & DDSCAPS_LOCALVIDMEM) == 0 && (Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) == 0 &&
            (Caps->dwCaps & (DDSCAPS_TEXTURE | DDSCAPS_RESERVED2 | DDSCAPS_VIDEOMEMORY)) != 0)
        {
            AddShared = TRUE;
        }

        if ((Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) == 0 || (Caps->dwCaps & DDSCAPS_LOCALVIDMEM) != 0)
        {
            FoundTotal = static_cast<DWORD>(Hardware->LocalMemory);
            FoundFree = static_cast<DWORD>(Hardware->LocalMemoryFree);

            /* Until something is allocated the desktop is the only user of local memory */
            if (FoundFree == FoundTotal)
            {
                Mode.dmSize = sizeof(Mode);
                if (EnumDisplaySettingsA(Driver->DeviceName, ENUM_CURRENT_SETTINGS, &Mode))
                {
                    Primary = Mode.dmPelsWidth * Mode.dmPelsHeight;
                    if (Mode.dmBitsPerPel == 32)
                        FoundFree -= 4 * Primary;
                    else if (Mode.dmBitsPerPel == 15 || Mode.dmBitsPerPel == 16)
                        FoundFree -= 2 * Primary;
                    else if (Mode.dmBitsPerPel == 24)
                        FoundFree -= 3 * Primary;
                    else
                        FoundFree -= Primary;
                }
            }
        }

        if (AddShared)
        {
            FoundTotal += static_cast<DWORD>(Hardware->NonLocalMemory);
            FoundFree += static_cast<DWORD>(Hardware->NonLocalMemoryFree);
        }

        if ((Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) != 0)
        {
            NonLocalTotal = FoundTotal;
            NonLocalFree = FoundFree;
        }
        else
        {
            LocalTotal = FoundTotal;
            LocalFree = FoundFree;
        }
    }
    else if (Hardware == nullptr && (Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) == 0)
    {
        DdQueryEmulatedMemory(Driver->AdapterLuid, Caps->dwCaps, &LocalTotal, &LocalFree);
    }

    if (Free != nullptr)
    {
        *Free = LocalFree;
        if ((Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) != 0 || (Driver->HalCaps.dwCaps2 & DDCAPS2_NONLOCALVIDMEMCAPS) == 0)
            *Free = NonLocalFree + LocalFree;
    }

    if (Total != nullptr)
    {
        *Total = LocalTotal;
        if ((Caps->dwCaps & DDSCAPS_NONLOCALVIDMEM) != 0 || (Driver->HalCaps.dwCaps2 & DDCAPS2_NONLOCALVIDMEMCAPS) == 0)
            *Total = NonLocalTotal + LocalTotal;
    }

    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetAvailableVidMem(
    _In_ VOID* Com,
    _In_ LPDDSCAPS Caps,
    _Out_opt_ DWORD* Total,
    _Out_opt_ DWORD* Free)
{
    DdInterface* This = DdFromCom(Com);
    DDSCAPS2 Wide = {};

    DdLockScope Lock;

    if ((This->Local->Driver->Flags & DDRAWI_NOHARDWARE) != 0)
        return DDERR_NODIRECTDRAWHW;
    if (Caps == nullptr)
        return E_INVALIDARG;

    Wide.dwCaps = Caps->dwCaps;
    return DdGetAvailableMemory(This->Local, &Wide, Total, Free);
}

static
HRESULT
STDMETHODCALLTYPE
DdGetAvailableVidMem4(
    _In_ VOID* Com,
    _In_ LPDDSCAPS2 Caps,
    _Out_opt_ DWORD* Total,
    _Out_opt_ DWORD* Free)
{
    DdInterface* This = DdFromCom(Com);

    DdLockScope Lock;

    if (Caps == nullptr)
        return E_INVALIDARG;
    return DdGetAvailableMemory(This->Local, Caps, Total, Free);
}

static
HRESULT
STDMETHODCALLTYPE
DdGetSurfaceFromDC(
    _In_ VOID* Com,
    _In_opt_ HDC Dc,
    _Out_ VOID** Surface)
{
    DdLockScope Lock;

    if (Dc == NULL || Surface == nullptr)
        return E_INVALIDARG;
    *Surface = nullptr;
    return DDERR_NOTFOUND;
}

static
HRESULT
STDMETHODCALLTYPE
DdRestoreAllSurfaces(
    _In_ VOID* Com)
{
    DdLockScope Lock;

    return DdRestoreLocalSurfaces(DdFromCom(Com)->Local);
}

/* True when the input desktop is the one this thread draws on */
static
BOOLEAN
DdDesktopIsVisible(VOID)
{
    WCHAR InputName[MAX_PATH] = {};
    WCHAR ThreadName[MAX_PATH] = {};
    HDESK Input;
    DWORD Needed;
    BOOLEAN Visible;

    Input = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (Input == NULL)
        return FALSE;

    Visible = GetUserObjectInformationW(Input, UOI_NAME, InputName, sizeof(InputName), &Needed) &&
              GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, ThreadName,
                                        sizeof(ThreadName), &Needed) &&
              _wcsicmp(InputName, ThreadName) == 0;
    CloseDesktop(Input);
    return Visible;
}

static
HRESULT
STDMETHODCALLTYPE
DdTestCooperativeLevel(
    _In_ VOID* Com)
{
    DdInterface* This = DdFromCom(Com);
    DdLocal* Local = This->Local;
    DdDriver* Driver = Local->Driver;
    BOOLEAN Owned;
    BOOLEAN Mine;

    DdLockScope Lock;

    if ((Driver->FakeFlags & 0x10) != 0)
        return (Local->Flags & DDRAWILCL_HASEXCLUSIVEMODE) != 0 ? DDERR_NOEXCLUSIVEMODE : DDERR_EXCLUSIVEMODEALREADYSET;

    DdCheckExclusive(Local, &Owned, &Mine);
    if ((Local->Flags & DDRAWILCL_HASEXCLUSIVEMODE) != 0)
        return Mine && DdDesktopIsVisible() ? S_OK : DDERR_NOEXCLUSIVEMODE;

    if (Owned || !DdDesktopIsVisible())
        return DDERR_EXCLUSIVEMODEALREADYSET;

    if (memcmp(&Driver->ModeKey, &Local->SavedMode, sizeof(Driver->ModeKey)) != 0)
        return DDERR_WRONGMODE;
    return S_OK;
}

/* Fills the identifier both GetDeviceIdentifier versions start from */
static
HRESULT
DdFillIdentifier(
    _In_ DdLocal* Local,
    _In_ DWORD Flags,
    _Out_ DDDEVICEIDENTIFIER2* Ident)
{
    static const GUID DirectDrawClass = { 0xD7B70EE0, 0x4340, 0x11CF, { 0xB0, 0x63, 0x00, 0x20, 0xAF, 0xC2, 0xCD, 0x35 } };
    DdDriver* Driver = Local->Driver;
    DISPLAY_DEVICEA Device;
    ULONGLONG Ids;
    ULONGLONG More;
    ULONGLONG Version;
    ULONGLONG Half;
    DWORD Index;
    HRESULT hr = DDERR_NOTFOUND;

    ZeroMemory(Ident, sizeof(*Ident));
    if ((Flags & ~DDGDI_GETHOSTIDENTIFIER) != 0)
    {
        hr = E_INVALIDARG;
    }
    else if ((Driver->Flags & DDRAWI_DISPLAYDRV) == 0)
    {
        hr = E_FAIL;
    }
    else
    {
        for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
        {
            if (_stricmp(Device.DeviceName, Driver->Name) != 0 &&
                ((Device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) == 0 || _stricmp(Driver->Name, "DISPLAY") != 0))
            {
                continue;
            }

            strncpy_s(Ident->szDescription, Device.DeviceString, 127);
            DdReadAdapterIdentity(Device.DeviceName, &Ident->dwVendorId, &Ident->dwDeviceId, &Ident->dwSubSysId,
                                  &Ident->dwRevision, Ident->szDriver, sizeof(Ident->szDriver));
            hr = S_OK;
            break;
        }

        /* The identifier GUID is the DirectDraw class mixed with the adapter ids */
        if (SUCCEEDED(hr))
        {
            memcpy(&Version, &Ident->liDriverVersion, sizeof(Version));
            Ids = Ident->dwVendorId | (static_cast<ULONGLONG>(Ident->dwDeviceId) << 32);
            More = Ident->dwSubSysId | (static_cast<ULONGLONG>(Ident->dwRevision) << 32);
            memcpy(&Half, &DirectDrawClass, sizeof(Half));
            Half ^= Ids;
            memcpy(&Ident->guidDeviceIdentifier, &Half, sizeof(Half));
            memcpy(&Half, DirectDrawClass.Data4, sizeof(Half));
            Half ^= More ^ Version;
            memcpy(Ident->guidDeviceIdentifier.Data4, &Half, sizeof(Half));
        }
    }

    Ident->dwWHQLLevel = 1;
    return hr;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetDeviceIdentifier(
    _In_ VOID* Com,
    _Out_ LPDDDEVICEIDENTIFIER Ident,
    _In_ DWORD Flags)
{
    DDDEVICEIDENTIFIER2 Full;
    HRESULT hr;

    {
        DdLockScope Lock;

        hr = DdFillIdentifier(DdFromCom(Com)->Local, Flags, &Full);
    }

    if (Ident == nullptr)
        return E_INVALIDARG;
    memcpy(Ident, &Full, sizeof(*Ident));
    return hr;
}

static
HRESULT
STDMETHODCALLTYPE
DdGetDeviceIdentifier7(
    _In_ VOID* Com,
    _Out_ LPDDDEVICEIDENTIFIER2 Ident,
    _In_ DWORD Flags)
{
    DDDEVICEIDENTIFIER2 Full;
    HRESULT hr;

    DdLockScope Lock;

    if (Ident == nullptr)
        return E_INVALIDARG;

    /* The padding after the WHQL level is left as the caller had it */
    hr = DdFillIdentifier(DdFromCom(Com)->Local, Flags, &Full);
    memcpy(Ident, &Full, FIELD_OFFSET(DDDEVICEIDENTIFIER2, dwWHQLLevel) + sizeof(Full.dwWHQLLevel));
    return hr;
}

static
HRESULT
STDMETHODCALLTYPE
DdStartModeTest(
    _In_ VOID* Com,
    _In_opt_ LPSIZE Modes,
    _In_ DWORD Count,
    _In_ DWORD Flags)
{
    return DDERR_TESTFINISHED;
}

static
HRESULT
STDMETHODCALLTYPE
DdEvaluateMode(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _Out_opt_ DWORD* Timeout)
{
    return E_INVALIDARG;
}

/* Every method of an object made by the class factory refuses until Initialize */
static
HRESULT
STDMETHODCALLTYPE
DdUninit0(
    _In_opt_ VOID* Unused0)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdUninit1(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdUninit2(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdUninit3(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2,
    _In_opt_ VOID* Unused3)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdUninit4(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2,
    _In_opt_ VOID* Unused3,
    _In_opt_ VOID* Unused4)
{
    return CO_E_NOTINITIALIZED;
}

static
HRESULT
STDMETHODCALLTYPE
DdUninit5(
    _In_opt_ VOID* Unused0,
    _In_opt_ VOID* Unused1,
    _In_opt_ VOID* Unused2,
    _In_opt_ VOID* Unused3,
    _In_opt_ VOID* Unused4,
    _In_opt_ VOID* Unused5)
{
    return CO_E_NOTINITIALIZED;
}

#define DD_SLOT(Function) reinterpret_cast<const VOID*>(&Function)

/* Slots shared by every version up to WaitForVerticalBlank, with the version specific ones passed in */
#define DD_COMMON_SLOTS(CreateSurfaceSlot, EnumModesSlot, SetModeSlot) \
    DD_SLOT(DdQueryInterface), DD_SLOT(DdAddRef), DD_SLOT(DdRelease), DD_SLOT(DdCompact), \
    DD_SLOT(DdCreateClipper), DD_SLOT(DdCreatePalette), DD_SLOT(CreateSurfaceSlot), DD_SLOT(DdDuplicateSurface), \
    DD_SLOT(EnumModesSlot), DD_SLOT(DdEnumSurfaces), DD_SLOT(DdFlipToGdiSurface), DD_SLOT(DdGetCaps), \
    DD_SLOT(DdGetDisplayMode), DD_SLOT(DdGetFourCCCodes), DD_SLOT(DdGetGdiSurface), \
    DD_SLOT(DdGetMonitorFrequency), DD_SLOT(DdGetScanLine), DD_SLOT(DdGetVerticalBlankStatus), \
    DD_SLOT(DdInitialize), DD_SLOT(DdRestoreDisplayMode), DD_SLOT(DdSetCooperativeLevel), DD_SLOT(SetModeSlot), \
    DD_SLOT(DdWaitForVerticalBlank)

#define DD_UNINIT_SLOTS(SetModeSlot) \
    DD_SLOT(DdQueryInterface), DD_SLOT(DdAddRef), DD_SLOT(DdRelease), DD_SLOT(DdUninit0), DD_SLOT(DdUninit3), \
    DD_SLOT(DdUninit4), DD_SLOT(DdUninit3), DD_SLOT(DdUninit2), DD_SLOT(DdUninit4), DD_SLOT(DdUninit4), \
    DD_SLOT(DdUninit0), DD_SLOT(DdUninit2), DD_SLOT(DdUninit1), DD_SLOT(DdUninit2), DD_SLOT(DdUninit1), \
    DD_SLOT(DdUninit1), DD_SLOT(DdUninit1), DD_SLOT(DdUninit1), DD_SLOT(DdInitialize), DD_SLOT(DdUninit0), \
    DD_SLOT(DdUninit2), DD_SLOT(SetModeSlot), DD_SLOT(DdUninit2)

static const VOID* const DdVtbl1[] =
{
    DD_COMMON_SLOTS(DdCreateSurface, DdEnumDisplayModes, DdSetDisplayMode),
};

static const VOID* const DdVtbl2[] =
{
    DD_COMMON_SLOTS(DdCreateSurface, DdEnumDisplayModes, DdSetDisplayMode2),
    DD_SLOT(DdGetAvailableVidMem),
};

static const VOID* const DdVtbl4[] =
{
    DD_COMMON_SLOTS(DdCreateSurface, DdEnumDisplayModes4, DdSetDisplayMode2),
    DD_SLOT(DdGetAvailableVidMem4), DD_SLOT(DdGetSurfaceFromDC), DD_SLOT(DdRestoreAllSurfaces),
    DD_SLOT(DdTestCooperativeLevel), DD_SLOT(DdGetDeviceIdentifier),
};

static const VOID* const DdVtbl7[] =
{
    DD_COMMON_SLOTS(DdCreateSurface, DdEnumDisplayModes4, DdSetDisplayMode2),
    DD_SLOT(DdGetAvailableVidMem4), DD_SLOT(DdGetSurfaceFromDC), DD_SLOT(DdRestoreAllSurfaces),
    DD_SLOT(DdTestCooperativeLevel), DD_SLOT(DdGetDeviceIdentifier7), DD_SLOT(DdStartModeTest),
    DD_SLOT(DdEvaluateMode),
};

static const VOID* const DdUninitVtbl1[] =
{
    DD_UNINIT_SLOTS(DdUninit3),
};

static const VOID* const DdUninitVtbl2[] =
{
    DD_UNINIT_SLOTS(DdUninit5),
    DD_SLOT(DdUninit3),
};

static const VOID* const DdUninitVtbl4[] =
{
    DD_UNINIT_SLOTS(DdUninit5),
    DD_SLOT(DdUninit3), DD_SLOT(DdUninit2), DD_SLOT(DdUninit0), DD_SLOT(DdUninit0), DD_SLOT(DdUninit2),
};

static const VOID* const DdUninitVtbl7[] =
{
    DD_UNINIT_SLOTS(DdUninit5),
    DD_SLOT(DdUninit3), DD_SLOT(DdUninit2), DD_SLOT(DdUninit0), DD_SLOT(DdUninit0), DD_SLOT(DdUninit2),
    DD_SLOT(DdUninit3), DD_SLOT(DdUninit2),
};

static_assert(_countof(DdVtbl1) == 23 && _countof(DdVtbl2) == 24 && _countof(DdVtbl4) == 28 && _countof(DdVtbl7) == 30);
static_assert(_countof(DdUninitVtbl1) == 23 && _countof(DdUninitVtbl2) == 24 && _countof(DdUninitVtbl4) == 28 &&
              _countof(DdUninitVtbl7) == 30);

const VOID*
DdVtblFor(
    _In_ DdKind Kind,
    _In_ BOOLEAN Initialized)
{
    switch (Kind)
    {
    case DdKind::Draw2:
        return Initialized ? DdVtbl2 : DdUninitVtbl2;
    case DdKind::Draw4:
        return Initialized ? DdVtbl4 : DdUninitVtbl4;
    case DdKind::Draw7:
        return Initialized ? DdVtbl7 : DdUninitVtbl7;
    default:
        return Initialized ? DdVtbl1 : DdUninitVtbl1;
    }
}
