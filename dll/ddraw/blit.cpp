/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Emulation layer blits, fills and presenting to the primary through GDI
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Blt flags the emulation layer knows; anything else is refused */
static constexpr DWORD DdBltKnownFlags = DDBLT_ASYNC | DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_DDROPS | DDBLT_KEYDEST |
                                         DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE | DDBLT_ROP |
                                         DDBLT_ROTATIONANGLE | DDBLT_WAIT | DDBLT_DONOTWAIT | DDBLT_DEPTHFILL |
                                         DDBLT_ALPHADEST | DDBLT_ALPHASRC | DDBLT_ZBUFFER;

struct DdBlitJob
{
    DdSurface* Target;
    RECT To;
    DdSurface* Source;
    RECT From;
    BOOLEAN Keyed;
    DDCOLORKEY Key;
    DWORD KeyMask;
    BOOLEAN MirrorX;
    BOOLEAN MirrorY;
};

static
DWORD
DdBytesPerPixel(
    _In_ const DdSurface* Surface)
{
    return Surface->Format.dwRGBBitCount / 8;
}

static
DWORD
DdReadPixel(
    _In_reads_bytes_(Size) const BYTE* Pixel,
    _In_ DWORD Size)
{
    DWORD Value = 0;

    memcpy(&Value, Pixel, Size);
    return Value;
}

static
VOID
DdWritePixel(
    _Out_writes_bytes_(Size) BYTE* Pixel,
    _In_ DWORD Size,
    _In_ DWORD Value)
{
    memcpy(Pixel, &Value, Size);
}

static
BOOLEAN
DdRectInside(
    _In_ const RECT* Rect,
    _In_ const DdSurface* Surface)
{
    return Rect->left >= 0 && Rect->top >= 0 && Rect->right <= static_cast<LONG>(Surface->Width) &&
           Rect->bottom <= static_cast<LONG>(Surface->Height) && Rect->left < Rect->right && Rect->top < Rect->bottom;
}

/* Bits a color key compares: the color channels, or the whole index for palettized pixels */
static
DWORD
DdKeyMask(
    _In_ const DdSurface* Surface)
{
    const DDPIXELFORMAT* Format = &Surface->Format;
    DWORD Mask = Format->dwRBitMask | Format->dwGBitMask | Format->dwBBitMask;

    if (Mask == 0)
        Mask = Format->dwRGBBitCount >= 32 ? 0xFFFFFFFF : (1u << Format->dwRGBBitCount) - 1;
    return Mask;
}

static
VOID
DdFillRect(
    _In_ DdSurface* Target,
    _In_ const RECT* To,
    _In_ DWORD Value)
{
    DWORD Size = DdBytesPerPixel(Target);
    BYTE* Row;
    LONG X;
    LONG Y;

    for (Y = To->top; Y < To->bottom; Y++)
    {
        Row = Target->Bits + Y * Target->Pitch + To->left * Size;
        for (X = To->left; X < To->right; X++, Row += Size)
            DdWritePixel(Row, Size, Value);
    }
}

/* Nearest pixel sampling from the middle of each target pixel, mirrored when asked */
static
VOID
DdCopyRect(
    _In_ const DdBlitJob& Job)
{
    DWORD Size = DdBytesPerPixel(Job.Target);
    LONG TargetWidth = Job.To.right - Job.To.left;
    LONG TargetHeight = Job.To.bottom - Job.To.top;
    LONG SourceWidth = Job.From.right - Job.From.left;
    LONG SourceHeight = Job.From.bottom - Job.From.top;
    BYTE* Copy = nullptr;
    const BYTE* SourceBits = Job.Source->Bits;
    LONG SourcePitch = Job.Source->Pitch;
    const BYTE* SourceRow;
    BYTE* TargetRow;
    DWORD Pixel;
    LONG SourceX;
    LONG SourceY;
    LONG X;
    LONG Y;

    /* Blits within one surface read from a copy so overlapping rectangles behave like a move */
    if (Job.Source == Job.Target)
    {
        Copy = static_cast<BYTE*>(LocalAlloc(LMEM_FIXED, static_cast<SIZE_T>(Job.Source->Pitch) * Job.Source->Height));
        if (Copy == nullptr)
            return;
        memcpy(Copy, Job.Source->Bits, static_cast<SIZE_T>(Job.Source->Pitch) * Job.Source->Height);
        SourceBits = Copy;
    }

    for (Y = 0; Y < TargetHeight; Y++)
    {
        SourceY = static_cast<LONG>((static_cast<LONGLONG>(2 * Y + 1) * SourceHeight) / (2 * TargetHeight));
        if (Job.MirrorY)
            SourceY = SourceHeight - 1 - SourceY;
        SourceRow = SourceBits + (Job.From.top + SourceY) * SourcePitch;
        TargetRow = Job.Target->Bits + (Job.To.top + Y) * Job.Target->Pitch;

        for (X = 0; X < TargetWidth; X++)
        {
            SourceX = static_cast<LONG>((static_cast<LONGLONG>(2 * X + 1) * SourceWidth) / (2 * TargetWidth));
            if (Job.MirrorX)
                SourceX = SourceWidth - 1 - SourceX;

            Pixel = DdReadPixel(SourceRow + (Job.From.left + SourceX) * Size, Size);
            if (Job.Keyed && (Pixel & Job.KeyMask) >= (Job.Key.dwColorSpaceLowValue & Job.KeyMask) &&
                (Pixel & Job.KeyMask) <= (Job.Key.dwColorSpaceHighValue & Job.KeyMask))
            {
                continue;
            }
            DdWritePixel(TargetRow + (Job.To.left + X) * Size, Size, Pixel);
        }
    }

    if (Copy != nullptr)
        LocalFree(Copy);
}

/* A color in the primary's format, as GDI wants it */
static
COLORREF
DdPixelToColor(
    _In_ const DDPIXELFORMAT* Format,
    _In_ DWORD Pixel)
{
    DWORD Channel[3] = { Format->dwRBitMask, Format->dwGBitMask, Format->dwBBitMask };
    BYTE Value[3];
    DWORD Index;
    DWORD Shift;
    DWORD Bits;

    for (Index = 0; Index < 3; Index++)
    {
        if (Channel[Index] == 0)
        {
            Value[Index] = 0;
            continue;
        }
        for (Shift = 0; ((Channel[Index] >> Shift) & 1) == 0; Shift++)
            ;
        Bits = Channel[Index] >> Shift;
        Value[Index] = static_cast<BYTE>((((Pixel & Channel[Index]) >> Shift) * 255) / Bits);
    }

    return RGB(Value[0], Value[1], Value[2]);
}

/* The primary belongs to the desktop compositor, so drawing on it goes through GDI */
static
HRESULT
DdPresentToPrimary(
    _In_ DdSurface* Primary,
    _In_ const RECT* To,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* From,
    _In_ BOOLEAN Fill,
    _In_ DWORD FillValue)
{
    HWND Window = Primary->Clipper != nullptr ? Primary->Clipper->Window : NULL;
    struct
    {
        BITMAPINFOHEADER Header;
        DWORD Masks[3];
    } Info = {};
    POINT Origin = { 0, 0 };
    HBRUSH Brush;
    RECT Local = *To;
    HDC Dc;
    int Lines;

    Dc = GetDC(Window);
    if (Dc == NULL)
        return DDERR_CANTCREATEDC;

    /* The rectangle is in screen space; a window clipper draws through that window's DC */
    if (Window != NULL)
    {
        ClientToScreen(Window, &Origin);
        OffsetRect(&Local, -Origin.x, -Origin.y);
    }

    if (Fill)
    {
        Brush = CreateSolidBrush(DdPixelToColor(&Primary->Format, FillValue));
        FillRect(Dc, &Local, Brush);
        DeleteObject(Brush);
        ReleaseDC(Window, Dc);
        return S_OK;
    }

    Info.Header.biSize = sizeof(Info.Header);
    Info.Header.biWidth = static_cast<LONG>(Source->Pitch * 8 / Source->Format.dwRGBBitCount);
    Info.Header.biHeight = -static_cast<LONG>(Source->Height);
    Info.Header.biPlanes = 1;
    Info.Header.biBitCount = static_cast<WORD>(Source->Format.dwRGBBitCount);
    Info.Header.biCompression = BI_RGB;
    if (Source->Format.dwRGBBitCount == 16 || Source->Format.dwRGBBitCount == 32)
    {
        Info.Header.biCompression = BI_BITFIELDS;
        Info.Masks[0] = Source->Format.dwRBitMask;
        Info.Masks[1] = Source->Format.dwGBitMask;
        Info.Masks[2] = Source->Format.dwBBitMask;
    }

    SetStretchBltMode(Dc, COLORONCOLOR);
    Lines = StretchDIBits(Dc, Local.left, Local.top, Local.right - Local.left, Local.bottom - Local.top, From->left,
                          From->top, From->right - From->left, From->bottom - From->top, Source->Bits,
                          reinterpret_cast<BITMAPINFO*>(&Info), DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(Window, Dc);
    if (Lines == 0)
    {
        DD_TRACE_ONCE(L"StretchDIBits to the primary failed (%lu)", GetLastError());
        return DDERR_GENERIC;
    }
    return S_OK;
}

HRESULT
DdBlt(
    _In_ DdSurface* Target,
    _In_opt_ const RECT* TargetRect,
    _In_opt_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags,
    _In_opt_ const DDBLTFX* Fx)
{
    BOOLEAN Primary = (Target->Caps.dwCaps & DDSCAPS_PRIMARYSURFACE) != 0;
    DdBlitJob Job = {};
    DWORD Rop = SRCCOPY;

    if (Target == nullptr || (Flags & ~DdBltKnownFlags) != 0)
        return E_INVALIDARG;
    if ((Flags & (DDBLT_COLORFILL | DDBLT_DDFX | DDBLT_DDROPS | DDBLT_KEYDESTOVERRIDE | DDBLT_KEYSRCOVERRIDE | DDBLT_ROP |
                  DDBLT_ROTATIONANGLE | DDBLT_DEPTHFILL)) != 0 &&
        (Fx == nullptr || Fx->dwSize != sizeof(*Fx)))
    {
        return E_INVALIDARG;
    }

    Job.Target = Target;
    if (TargetRect != nullptr)
        Job.To = *TargetRect;
    else
        SetRect(&Job.To, 0, 0, Target->Width, Target->Height);
    if (!DdRectInside(&Job.To, Target))
        return DDERR_INVALIDRECT;

    if (DdSurfaceBusy(Target) || (Source != nullptr && DdSurfaceBusy(Source)))
        return DDERR_SURFACEBUSY;

    if ((Flags & DDBLT_COLORFILL) != 0)
    {
        if (Primary)
            return DdPresentToPrimary(Target, &Job.To, nullptr, nullptr, TRUE, Fx->dwFillColor);
        DdFillRect(Target, &Job.To, Fx->dwFillColor);
        return S_OK;
    }

    if ((Flags & DDBLT_ROP) != 0)
    {
        Rop = Fx->dwROP;
        if (Rop == BLACKNESS || Rop == WHITENESS)
        {
            if (Primary)
                return DdPresentToPrimary(Target, &Job.To, nullptr, nullptr, TRUE, Rop == BLACKNESS ? 0 : 0xFFFFFFFF);
            DdFillRect(Target, &Job.To, Rop == BLACKNESS ? 0 : 0xFFFFFFFF);
            return S_OK;
        }
        if (Rop != SRCCOPY)
            return DDERR_NORASTEROPHW;
    }

    if (Source == nullptr)
        return E_INVALIDARG;

    Job.Source = Source;
    if (SourceRect != nullptr)
        Job.From = *SourceRect;
    else
        SetRect(&Job.From, 0, 0, Source->Width, Source->Height);
    if (!DdRectInside(&Job.From, Source))
        return DDERR_INVALIDRECT;

    if (Primary)
        return DdPresentToPrimary(Target, &Job.To, Source, &Job.From, FALSE, 0);

    if (Source->Format.dwRGBBitCount != Target->Format.dwRGBBitCount || Target->Format.dwRGBBitCount < 8)
    {
        DD_TRACE_ONCE(L"blits that convert pixel formats are not implemented yet");
        return DDERR_UNSUPPORTED;
    }

    if ((Flags & DDBLT_KEYDEST) != 0 || (Flags & DDBLT_KEYDESTOVERRIDE) != 0)
    {
        DD_TRACE_ONCE(L"destination color keyed blits are not implemented yet");
        return DDERR_NOCOLORKEYHW;
    }

    if ((Flags & DDBLT_KEYSRCOVERRIDE) != 0)
    {
        Job.Keyed = TRUE;
        Job.Key = Fx->ddckSrcColorkey;
    }
    else if ((Flags & DDBLT_KEYSRC) != 0)
    {
        if ((Source->KeyFlags & DDCKEY_SRCBLT) == 0)
            return DDERR_NOCOLORKEY;
        Job.Keyed = TRUE;
        Job.Key = Source->SourceKey;
    }
    Job.KeyMask = DdKeyMask(Source);

    if ((Flags & DDBLT_DDFX) != 0)
    {
        Job.MirrorX = (Fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT) != 0;
        Job.MirrorY = (Fx->dwDDFX & DDBLTFX_MIRRORUPDOWN) != 0;
    }

    DdCopyRect(Job);
    Target->Uniqueness += 1;
    return S_OK;
}

HRESULT
DdBltFast(
    _In_ DdSurface* Target,
    _In_ DWORD X,
    _In_ DWORD Y,
    _In_ DdSurface* Source,
    _In_opt_ const RECT* SourceRect,
    _In_ DWORD Flags)
{
    DWORD BltFlags = DDBLT_WAIT;
    RECT From;
    RECT To;

    if ((Flags & ~(DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY | DDBLTFAST_WAIT | DDBLTFAST_DONOTWAIT)) != 0 ||
        (Flags & (DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY)) == (DDBLTFAST_SRCCOLORKEY | DDBLTFAST_DESTCOLORKEY))
    {
        return E_INVALIDARG;
    }

    if (Target->Clipper != nullptr)
        return DDERR_BLTFASTCANTCLIP;

    if (SourceRect != nullptr)
        From = *SourceRect;
    else
        SetRect(&From, 0, 0, Source->Width, Source->Height);
    SetRect(&To, X, Y, X + (From.right - From.left), Y + (From.bottom - From.top));

    if ((Flags & DDBLTFAST_SRCCOLORKEY) != 0)
        BltFlags |= DDBLT_KEYSRC;
    if ((Flags & DDBLTFAST_DESTCOLORKEY) != 0)
        BltFlags |= DDBLT_KEYDEST;

    return DdBlt(Target, &To, Source, &From, BltFlags, nullptr);
}
