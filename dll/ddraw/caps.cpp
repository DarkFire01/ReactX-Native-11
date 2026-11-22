/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Driver and emulation capabilities, built the way the system runtime reports them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Caps every WDDM driver reports before the user mode driver adds its own */
static constexpr DWORD DdHalBaseCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTQUEUE | DDCAPS_BLTFOURCC |
                                       DDCAPS_BLTSTRETCH | DDCAPS_GDI | DDCAPS_ALPHA | DDCAPS_BLTCOLORFILL |
                                       DDCAPS_CANBLTSYSMEM;
static constexpr DWORD DdHalBaseCaps2 = DDCAPS2_NONLOCALVIDMEM | DDCAPS2_WIDESURFACES | DDCAPS2_CANFLIPODDEVEN |
                                        DDCAPS2_COPYFOURCC | DDCAPS2_FLIPNOVSYNC | DDCAPS2_CANSHARERESOURCE;
static constexpr DWORD DdHalBaseFxCaps = DDFXCAPS_BLTARITHSTRETCHY | DDFXCAPS_BLTSHRINKX | DDFXCAPS_BLTSHRINKY |
                                         DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHY;
static constexpr DWORD DdHalSurfaceCaps = DDSCAPS_BACKBUFFER | DDSCAPS_COMPLEX | DDSCAPS_FLIP |
                                          DDSCAPS_FRONTBUFFER | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_PRIMARYSURFACE |
                                          DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY |
                                          DDSCAPS_ZBUFFER | DDSCAPS_OWNDC | DDSCAPS_MIPMAP | DDSCAPS_RESERVED2 |
                                          DDSCAPS_LOCALVIDMEM | DDSCAPS_NONLOCALVIDMEM;

/* Emulation layer caps when it draws through GDI */
static constexpr DWORD DdHelCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_PALETTE | DDCAPS_COLORKEY |
                                   DDCAPS_ALPHA | DDCAPS_BLTCOLORFILL | DDCAPS_BLTDEPTHFILL | DDCAPS_CANCLIP |
                                   DDCAPS_CANCLIPSTRETCHED | DDCAPS_CANBLTSYSMEM;

/* Bit 1 has no public name; the system emulation layer sets it */
static constexpr DWORD DdHelFxCaps = DDFXCAPS_BLTALPHA | 0x00000002 | DDFXCAPS_BLTARITHSTRETCHY |
                                     DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN | DDFXCAPS_BLTSHRINKX |
                                     DDFXCAPS_BLTSHRINKXN | DDFXCAPS_BLTSHRINKY | DDFXCAPS_BLTSHRINKYN |
                                     DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHXN | DDFXCAPS_BLTSTRETCHY |
                                     DDFXCAPS_BLTSTRETCHYN;
static constexpr DWORD DdHelPalCaps = DDPCAPS_4BIT | DDPCAPS_8BITENTRIES | DDPCAPS_8BIT | DDPCAPS_ALLOW256 |
                                      DDPCAPS_1BIT | DDPCAPS_2BIT;
static constexpr DWORD DdHelPlainSurfaceCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_TEXTURE | DDSCAPS_ZBUFFER |
                                               DDSCAPS_MIPMAP | DDSCAPS_RESERVED2;
static constexpr DWORD DdHelGdiSurfaceCaps = DdHelPlainSurfaceCaps | DDSCAPS_FLIP | DDSCAPS_PALETTE |
                                             DDSCAPS_PRIMARYSURFACE;

/* Mode flag for 15 bit modes reported as 16 bit */
static constexpr WORD DdModeFlag555 = 0x0100;

/* Raster operations as bits of the 256 entry table, indexed by the high byte of the ROP */
static
VOID
DdSetRop(
    _Inout_updates_(DD_ROP_SPACE) DWORD* Rops,
    _In_ DWORD Rop)
{
    DWORD Index = (Rop >> 16) & 0xFF;

    Rops[Index / 32] |= 1u << (Index % 32);
}

VOID
DdFillPixelFormat(
    _Out_ DDPIXELFORMAT* Format,
    _In_opt_ HDC Dc,
    _In_ DWORD BitCount)
{
    BITMAPINFO* Info;
    DWORD* Masks;
    HBITMAP Bitmap;

    ZeroMemory(Format, sizeof(*Format));
    Format->dwSize = sizeof(*Format);
    Format->dwFlags = DDPF_RGB;
    if (Dc != NULL)
        BitCount = GetDeviceCaps(Dc, BITSPIXEL);
    Format->dwRGBBitCount = BitCount;

    if (BitCount == 8)
    {
        Format->dwFlags |= DDPF_PALETTEINDEXED8;
        return;
    }

    if (BitCount == 24)
    {
        Format->dwRBitMask = 0xFF0000;
        Format->dwGBitMask = 0x00FF00;
        Format->dwBBitMask = 0x0000FF;
        return;
    }

    if (Dc == NULL)
    {
        if (BitCount == 15)
        {
            Format->dwRBitMask = 0x7C00;
            Format->dwGBitMask = 0x03E0;
            Format->dwBBitMask = 0x001F;
        }
        else if (BitCount == 16)
        {
            Format->dwRBitMask = 0xF800;
            Format->dwGBitMask = 0x07E0;
            Format->dwBBitMask = 0x001F;
        }
        else if (BitCount == 32)
        {
            Format->dwRBitMask = 0xFF0000;
            Format->dwGBitMask = 0x00FF00;
            Format->dwBBitMask = 0x0000FF;
        }
        return;
    }

    /* Ask GDI for the masks of a bitmap compatible with the screen */
    Info = static_cast<BITMAPINFO*>(LocalAlloc(LPTR, sizeof(BITMAPINFOHEADER) + 4 * sizeof(DWORD)));
    if (Info == nullptr)
        return;

    Bitmap = CreateCompatibleBitmap(Dc, 1, 1);
    if (Bitmap != NULL)
    {
        Info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        if (GetDIBits(Dc, Bitmap, 0, 0, NULL, Info, DIB_RGB_COLORS) && Info->bmiHeader.biCompression == BI_BITFIELDS)
        {
            GetDIBits(Dc, Bitmap, 0, Info->bmiHeader.biHeight, NULL, Info, DIB_RGB_COLORS);
            Masks = reinterpret_cast<DWORD*>(Info->bmiColors);
            Format->dwRBitMask = Masks[0];
            Format->dwGBitMask = Masks[1];
            Format->dwBBitMask = Masks[2];
            Format->dwRGBAlphaBitMask = 0;
        }
        DeleteObject(Bitmap);
    }
    LocalFree(Info);
}

BOOLEAN
DdReadDisplayMode(
    _Inout_ DdDriver* Driver)
{
    DEVMODEA Mode = {};
    DDPIXELFORMAT Format;
    DISPLAY_DEVICEA Device;
    PCSTR Name;
    DWORD Monitors = 0;
    DWORD Index;
    HDC Dc;

    Mode.dmSize = sizeof(Mode);
    Name = _stricmp(Driver->Name, "display") == 0 ? nullptr : Driver->Name;
    if (!EnumDisplaySettingsA(Name, ENUM_CURRENT_SETTINGS, &Mode))
    {
        DdTrace(L"cannot read the current display mode of %S", Driver->Name);
        return FALSE;
    }

    ZeroMemory(&Driver->Mode, sizeof(Driver->Mode));
    Driver->Mode.Width = Mode.dmPelsWidth;
    Driver->Mode.Height = Mode.dmPelsHeight;
    Driver->Mode.BitCount = Mode.dmBitsPerPel;
    Driver->Mode.RefreshRate = static_cast<WORD>(Mode.dmDisplayFrequency);
    Driver->Mode.Pitch = (Mode.dmBitsPerPel * Mode.dmPelsWidth) >> 3;

    if (Mode.dmBitsPerPel <= 8)
    {
        Driver->Mode.Flags = DDMODEINFO_PALETTIZED;
    }
    else
    {
        Dc = DdCreateDisplayDc(Driver->Name);
        DdFillPixelFormat(&Format, Dc, Mode.dmBitsPerPel);
        if (Dc != NULL)
            DeleteDC(Dc);

        if (Driver->Mode.BitCount == 15)
        {
            Driver->Mode.BitCount = 16;
            Driver->Mode.Flags = DdModeFlag555;
        }
        Driver->Mode.RedMask = Format.dwRBitMask;
        Driver->Mode.GreenMask = Format.dwGBitMask;
        Driver->Mode.BlueMask = Format.dwBBitMask;
        Driver->Mode.AlphaMask = Format.dwRGBAlphaBitMask;
    }

    Driver->MonitorFrequency = Driver->Mode.RefreshRate;

    for (Index = 0; DdEnumDisplayDevice(Index, &Device); Index++)
    {
        if ((Device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0)
            Monitors++;
    }

    Driver->ModeKey.Width = static_cast<WORD>(Driver->Mode.Width);
    Driver->ModeKey.Height = static_cast<WORD>(Driver->Mode.Height);
    Driver->ModeKey.BitCount = static_cast<BYTE>(Driver->Mode.BitCount);
    if (Driver->Mode.BitCount == 16 && (Driver->Mode.Flags & DdModeFlag555) != 0)
        Driver->ModeKey.BitCount = 15;
    Driver->ModeKey.MonitorCount = static_cast<BYTE>(Monitors);
    Driver->ModeKey.RefreshRate = Driver->Mode.RefreshRate;
    return TRUE;
}

VOID
DdUpdateDeviceRect(
    _Inout_ DdDriver* Driver)
{
    MONITORINFO Info = {};
    HMONITOR Monitor;

    Driver->MonitorCount = GetSystemMetrics(SM_CMONITORS);
    SetRect(&Driver->DeviceRect, 0, 0, Driver->Mode.Width, Driver->Mode.Height);

    if (Driver->MonitorCount > 1 && _stricmp(Driver->Name, "display") != 0)
    {
        Monitor = DdMonitorFromDevice(Driver->Name);
        Info.cbSize = sizeof(Info);
        if (Monitor != NULL && GetMonitorInfoW(Monitor, &Info) && !IsRectEmpty(&Info.rcMonitor))
            Driver->DeviceRect = Info.rcMonitor;
    }

    /* A virtual desktop driver attached to the desktop covers all of it */
    if ((Driver->Flags & (DDRAWI_VIRTUALDESKTOP | DdDriverAttached)) == (DDRAWI_VIRTUALDESKTOP | DdDriverAttached))
    {
        SetRect(&Driver->DesktopRect, GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN));
    }
    else
    {
        Driver->DesktopRect = Driver->DeviceRect;
    }
}

/* Gamma ramps work when the kernel says so, or when the adapter only displays and its WDDM caps say so */
static
BOOLEAN
DdHardwareHasGamma(
    _In_ DdHardware* Hardware)
{
    DWORD Gamma = 0;

    if (Hardware->AdapterType.RenderSupported)
    {
        if (FAILED(DdHardwareGetCaps(Hardware, D3DDDICAPS_GETGAMMARAMPCAPS, nullptr, &Gamma, sizeof(Gamma))))
            return FALSE;
        return (Gamma & 1) != 0;
    }

    return Hardware->Wddm12Caps.SupportGammaRamp != 0;
}

static
VOID
DdCollectFourCC(
    _Inout_ DdDriver* Driver)
{
    DdHardware* Hardware = Driver->Hardware;
    DWORD Count = 0;
    UINT Index;

    delete[] Driver->FourCC;
    Driver->FourCC = nullptr;
    Driver->FourCCCount = 0;
    if (!DdHardwareQueryFormats(Hardware))
        return;

    Driver->FourCC = new (std::nothrow) DWORD[Hardware->FormatCount];
    if (Driver->FourCC == nullptr)
        return;

    /* Plain offscreen formats whose code is a FOURCC rather than a small D3DFORMAT number */
    for (Index = 0; Index < Hardware->FormatCount; Index++)
    {
        const FORMATOP& Op = Hardware->Formats[Index];

        if ((static_cast<DWORD>(Op.Format) & 0xFF00) != 0 && (Op.Operations & FORMATOP_OFFSCREENPLAIN) != 0)
            Driver->FourCC[Count++] = static_cast<DWORD>(Op.Format);
    }
    Driver->FourCCCount = Count;
}

BOOLEAN
DdBuildHardwareCaps(
    _Inout_ DdDriver* Driver)
{
    DdHardware* Hardware = Driver->Hardware;
    DDRAW_MODE_SPECIFIC_CAPS ModeCaps = {};
    DDRAW_CAPS Caps = {};
    DDCORECAPS* Hal = &Driver->HalCaps;
    HRESULT hr;

    hr = DdHardwareGetCaps(Hardware, D3DDDICAPS_DDRAW, nullptr, &Caps, sizeof(Caps));
    if (FAILED(hr))
    {
        DD_TRACE_ONCE(L"GetCaps(DDRAW) failed 0x%08lX", hr);
        ZeroMemory(&Caps, sizeof(Caps));
    }

    hr = DdHardwareGetCaps(Hardware, D3DDDICAPS_DDRAW_MODE_SPECIFIC, nullptr, &ModeCaps, sizeof(ModeCaps));
    if (FAILED(hr))
    {
        DD_TRACE_ONCE(L"GetCaps(DDRAW_MODE_SPECIFIC) failed 0x%08lX", hr);
        ZeroMemory(&ModeCaps, sizeof(ModeCaps));
    }

    ZeroMemory(Hal, sizeof(*Hal));
    Hal->dwSize = sizeof(DDCAPS_DX7);
    Hal->dwCaps = DdHalBaseCaps;
    Hal->dwCaps2 = DdHalBaseCaps2;
    if (Hardware->FlipQueue.MaxSoftwareFlipQueueLength != 0 || Hardware->FlipQueue.FlipFlags.FlipInterval != 0)
        Hal->dwCaps2 |= DDCAPS2_FLIPINTERVAL;
    Hal->dwFXCaps = DdHalBaseFxCaps;
    Hal->ddsCaps.dwCaps = DdHalSurfaceCaps;

    if ((Caps.Caps & DDRAW_CAPS_ZBLTS) != 0)
        Hal->dwCaps |= DDCAPS_ZBLTS;
    if ((Caps.Caps & DDRAW_CAPS_COLORKEY) != 0)
        Hal->dwCaps |= DDCAPS_COLORKEY | DDCAPS_COLORKEYHWASSIST;
    if ((Caps.Caps & DDRAW_CAPS_BLTDEPTHFILL) != 0)
        Hal->dwCaps |= DDCAPS_BLTDEPTHFILL;
    if ((ModeCaps.Caps & MODE_CAPS_READSCANLINE) != 0)
        Hal->dwCaps |= DDCAPS_READSCANLINE;

    if ((Caps.Caps2 & DDRAW_CAPS2_CANDROPZ16BIT) != 0)
        Hal->dwCaps2 |= DDCAPS2_CANDROPZ16BIT;
    if ((Caps.Caps2 & DDRAW_CAPS2_DYNAMICTEXTURES) != 0)
        Hal->dwCaps2 |= DDCAPS2_DYNAMICTEXTURES;
    if (DdHardwareHasGamma(Hardware))
        Hal->dwCaps2 |= DDCAPS2_PRIMARYGAMMA;

    if ((Caps.CKeyCaps & DDRAW_CKEYCAPS_SRCBLT) != 0)
        Hal->dwCKeyCaps |= DDCKEYCAPS_SRCBLT;
    if ((Caps.CKeyCaps & DDRAW_CKEYCAPS_DESTBLT) != 0)
        Hal->dwCKeyCaps |= DDCKEYCAPS_DESTBLT;

    if ((Caps.FxCaps & DDRAW_FXCAPS_BLTMIRRORLEFTRIGHT) != 0)
        Hal->dwFXCaps |= DDFXCAPS_BLTMIRRORLEFTRIGHT;
    if ((Caps.FxCaps & DDRAW_FXCAPS_BLTMIRRORUPDOWN) != 0)
        Hal->dwFXCaps |= DDFXCAPS_BLTMIRRORUPDOWN;

    /* Overlays only count when the mode reports a visible one */
    if (ModeCaps.MaxVisibleOverlays != 0 && (ModeCaps.Caps & MODE_CAPS_OVERLAY) != 0)
    {
        Hal->ddsCaps.dwCaps |= DDSCAPS_OVERLAY;
        Hal->dwCaps |= DDCAPS_OVERLAY | DDCAPS_OVERLAYCANTCLIP | DDCAPS_OVERLAYFOURCC;
        if ((ModeCaps.Caps & MODE_CAPS_OVERLAYSTRETCH) != 0)
            Hal->dwCaps |= DDCAPS_OVERLAYSTRETCH;
        if ((ModeCaps.Caps & MODE_CAPS_CANBOBINTERLEAVED) != 0)
            Hal->dwCaps2 |= DDCAPS2_CANBOBINTERLEAVED;
        if ((ModeCaps.Caps & MODE_CAPS_CANBOBNONINTERLEAVED) != 0)
            Hal->dwCaps2 |= DDCAPS2_CANBOBNONINTERLEAVED;
        if ((ModeCaps.Caps & MODE_CAPS_CANFLIPODDEVEN) != 0)
            Hal->dwCaps2 |= DDCAPS2_CANFLIPODDEVEN;
        if ((ModeCaps.Caps & MODE_CAPS_COLORCONTROLOVERLAY) != 0)
            Hal->dwCaps2 |= DDCAPS2_COLORCONTROLOVERLAY;

        if ((Caps.Caps & DDRAW_CAPS_COLORKEY) != 0)
        {
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_DESTOVERLAY) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_DESTOVERLAY;
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_DESTOVERLAYYUV) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_DESTOVERLAYYUV;
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_SRCOVERLAY) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_SRCOVERLAY;
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_SRCOVERLAYCLRSPACE) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_SRCOVERLAYCLRSPACE;
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_SRCOVERLAYCLRSPACEYUV) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_SRCOVERLAYCLRSPACEYUV;
            if ((ModeCaps.CKeyCaps & MODE_CKEYCAPS_SRCOVERLAYYUV) != 0)
                Hal->dwCKeyCaps |= DDCKEYCAPS_SRCOVERLAYYUV;
        }

        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYSHRINKX) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYSHRINKX;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYSHRINKY) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYSHRINKY;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYSTRETCHX) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYSTRETCHX;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYSTRETCHY) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYSTRETCHY | DDFXCAPS_OVERLAYARITHSTRETCHY;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYMIRRORLEFTRIGHT) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYMIRRORLEFTRIGHT;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYMIRRORUPDOWN) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYMIRRORUPDOWN;
        if ((ModeCaps.FxCaps & MODE_FXCAPS_OVERLAYDEINTERLACE) != 0)
            Hal->dwFXCaps |= DDFXCAPS_OVERLAYDEINTERLACE;

        Hal->dwMaxVisibleOverlays = ModeCaps.MaxVisibleOverlays;
        Hal->dwMinOverlayStretch = ModeCaps.MinOverlayStretch;
        Hal->dwMaxOverlayStretch = ModeCaps.MaxOverlayStretch;
    }

    DdCollectFourCC(Driver);
    Hal->dwNumFourCCCodes = Driver->FourCCCount;

    /* Blits only promise SRCCOPY, in video memory and both ways across the bus */
    DdSetRop(Hal->dwRops, SRCCOPY);
    Hal->dwSVBCaps |= DDCAPS_BLT;
    DdSetRop(Hal->dwSVBRops, SRCCOPY);
    Hal->dwVSBCaps |= DDCAPS_BLT;
    DdSetRop(Hal->dwVSBRops, SRCCOPY);

    /* An 8 bit desktop has no GDI blits of its own */
    if (Driver->Mode.BitCount == 8)
        Hal->dwCaps &= ~DDCAPS_GDI;

    /* Non local video memory blits with the same caps as local ones */
    Driver->HasNonLocalCaps = (Hal->dwCaps2 & DDCAPS2_NONLOCALVIDMEM) != 0 &&
                              (Hal->dwCaps2 & DDCAPS2_NONLOCALVIDMEMCAPS) == 0;
    ZeroMemory(&Driver->HalNonLocalCaps, sizeof(Driver->HalNonLocalCaps));
    if (Driver->HasNonLocalCaps)
    {
        Driver->HalNonLocalCaps.dwSize = sizeof(Driver->HalNonLocalCaps);
        Driver->HalNonLocalCaps.dwNLVBCaps = Hal->dwCaps;
        Driver->HalNonLocalCaps.dwNLVBCaps2 = Hal->dwCaps2;
        Driver->HalNonLocalCaps.dwNLVBCKeyCaps = Hal->dwCKeyCaps;
        Driver->HalNonLocalCaps.dwNLVBFXCaps = Hal->dwFXCaps;
        memcpy(Driver->HalNonLocalCaps.dwNLVBRops, Hal->dwRops, sizeof(Hal->dwRops));
    }

    ZeroMemory(&Driver->HalMoreCaps, sizeof(Driver->HalMoreCaps));
    Driver->HalMoreCaps.dwCaps2 = DDSCAPS2_CUBEMAP;

    /* Display drivers can always render to a window; no gamma calibrator is installed */
    Hal->dwCaps2 |= DDCAPS2_CANRENDERWINDOWED;
    Hal->dwCaps2 &= ~DDCAPS2_CANCALIBRATEGAMMA;
    return TRUE;
}

VOID
DdBuildEmulatedCaps(
    _Inout_ DdDriver* Driver)
{
    ZeroMemory(&Driver->HalCaps, sizeof(Driver->HalCaps));
    Driver->HalCaps.dwSize = sizeof(DDCAPS_DX7);
    Driver->HalCaps.dwCaps = DDCAPS_NOHARDWARE;
    Driver->HalCaps.dwCaps2 = DDCAPS2_CERTIFIED | DDCAPS2_CANRENDERWINDOWED;
    Driver->HasNonLocalCaps = FALSE;
    ZeroMemory(&Driver->HalNonLocalCaps, sizeof(Driver->HalNonLocalCaps));
    ZeroMemory(&Driver->HalMoreCaps, sizeof(Driver->HalMoreCaps));
    delete[] Driver->FourCC;
    Driver->FourCC = nullptr;
    Driver->FourCCCount = 0;
}

VOID
DdBuildHelCaps(
    _Inout_ DdDriver* Driver)
{
    DDCORECAPS* Hel = &Driver->HelCaps;
    BOOLEAN Gdi = (Driver->Flags & DdDriverDoesGdi) != 0;

    ZeroMemory(Hel, sizeof(*Hel));
    Hel->dwCaps2 = DDCAPS2_CERTIFIED;
    Hel->dwZBufferBitDepths = DDBD_16;
    Hel->ddsCaps.dwCaps = Gdi ? DdHelGdiSurfaceCaps : DdHelPlainSurfaceCaps;
    ZeroMemory(&Driver->HelMoreCaps, sizeof(Driver->HelMoreCaps));
    Driver->HelMoreCaps.dwCaps2 = DDSCAPS2_CUBEMAP;

    if (Gdi)
    {
        Hel->dwCaps = DdHelCaps;
        Hel->dwCKeyCaps = DDCKEYCAPS_SRCBLT;
        Hel->dwFXCaps = DdHelFxCaps;
        Hel->dwPalCaps = DdHelPalCaps;

        /* Every ROP GDI knows, though only BLACKNESS, SRCCOPY and WHITENESS are fast */
        DdSetRop(Hel->dwRops, BLACKNESS);
        DdSetRop(Hel->dwRops, SRCCOPY);
        DdSetRop(Hel->dwRops, WHITENESS);

        Hel->dwSVBCaps = Hel->dwCaps;
        Hel->dwSVBCKeyCaps = Hel->dwCKeyCaps;
        Hel->dwSVBFXCaps = Hel->dwFXCaps;
        memcpy(Hel->dwSVBRops, Hel->dwRops, sizeof(Hel->dwRops));
        Hel->dwVSBCaps = Hel->dwCaps;
        Hel->dwVSBCKeyCaps = Hel->dwCKeyCaps;
        Hel->dwVSBFXCaps = Hel->dwFXCaps;
        memcpy(Hel->dwVSBRops, Hel->dwRops, sizeof(Hel->dwRops));
        Hel->dwSSBCaps = Hel->dwCaps;
        Hel->dwSSBCKeyCaps = Hel->dwCKeyCaps;
        Hel->dwSSBFXCaps = Hel->dwFXCaps;
        memcpy(Hel->dwSSBRops, Hel->dwRops, sizeof(Hel->dwRops));
    }

    ZeroMemory(&Driver->HelNonLocalCaps, sizeof(Driver->HelNonLocalCaps));
    if (Driver->HasNonLocalCaps)
    {
        Driver->HelNonLocalCaps.dwSize = sizeof(Driver->HelNonLocalCaps);
        Driver->HelNonLocalCaps.dwNLVBCaps = Hel->dwCaps;
        Driver->HelNonLocalCaps.dwNLVBCaps2 = Hel->dwCaps2;
        Driver->HelNonLocalCaps.dwNLVBCKeyCaps = Hel->dwCKeyCaps;
        Driver->HelNonLocalCaps.dwNLVBFXCaps = Hel->dwFXCaps;
        memcpy(Driver->HelNonLocalCaps.dwNLVBRops, Hel->dwRops, sizeof(Hel->dwRops));
    }

    Driver->Flags |= DdDriverHelCaps;
}
