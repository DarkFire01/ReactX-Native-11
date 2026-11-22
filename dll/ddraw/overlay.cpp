/*
 * PROJECT:     ReactX DirectDraw
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Overlay surface methods; 64 bit Windows creates no overlays, so these answer for everything else
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ddraw.hpp"

/* Flags UpdateOverlay accepts */
static constexpr DWORD DdOverlayFlags = 0x07FFFFFF;

/* Flags UpdateOverlayDisplay accepts */
static constexpr DWORD DdOverlayDisplayFlags = 0x00030000;

/* System memory overlay updates belong to the emulation layer, which has none to offer */
static
BOOLEAN
DdEmulatedOnly(
    _In_ const DdSurface* Surface)
{
    return (Surface->Caps.dwCaps & DDSCAPS_SYSTEMMEMORY) != 0;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlay(
    _In_ VOID* Com,
    _In_opt_ LPRECT SourceRect,
    _In_opt_ VOID* Target,
    _In_opt_ LPRECT TargetRect,
    _In_ DWORD Flags,
    _In_opt_ LPDDOVERLAYFX Fx)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);
    DdSurface* Destination = DdSurfaceFromCom(Target);

    DdLockScope Lock;

    if (Surface->Lost || (Destination != nullptr && Destination->Lost))
        return DDERR_SURFACELOST;
    if ((Flags & ~DdOverlayFlags) != 0 || Destination == nullptr)
        return E_INVALIDARG;
    if ((Flags & DDOVER_DDFX) != 0 && (Fx == nullptr || Fx->dwSize != sizeof(*Fx)))
        return E_INVALIDARG;
    if ((Surface->Caps.dwCaps & DDSCAPS_OVERLAY) == 0)
        return DDERR_NOTAOVERLAYSURFACE;

    DD_TRACE_ONCE(L"overlays are not implemented");
    return DDERR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceSetOverlayPosition(
    _In_ VOID* Com,
    _In_ LONG X,
    _In_ LONG Y)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Surface->Lost)
        return DDERR_SURFACELOST;
    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DDERR_NOTONMIPMAPSUBLEVEL;
    if ((Surface->Caps.dwCaps & DDSCAPS_OVERLAY) == 0)
        return DDERR_NOTAOVERLAYSURFACE;
    return DDERR_OVERLAYNOTVISIBLE;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceGetOverlayPosition(
    _In_ VOID* Com,
    _Out_opt_ LPLONG X,
    _Out_opt_ LPLONG Y)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if (Surface->Lost)
        return DDERR_SURFACELOST;
    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DDERR_NOTONMIPMAPSUBLEVEL;
    if ((Surface->Caps.dwCaps & DDSCAPS_OVERLAY) == 0)
        return DDERR_NOTAOVERLAYSURFACE;
    return DDERR_OVERLAYNOTVISIBLE;
}

/* With no overlays there is nothing in the Z order to report */
HRESULT
STDMETHODCALLTYPE
DdSurfaceEnumOverlayZOrders(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Context,
    _In_opt_ VOID* Callback)
{
    DdLockScope Lock;

    if (Callback == nullptr || (Flags & ~DDENUMOVERLAYZ_FRONTTOBACK) != 0)
        return E_INVALIDARG;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlayZOrder(
    _In_ VOID* Com,
    _In_ DWORD Flags,
    _In_opt_ VOID* Reference)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DDERR_NOTONMIPMAPSUBLEVEL;
    if ((Surface->Caps.dwCaps & DDSCAPS_OVERLAY) == 0)
        return DDERR_NOTAOVERLAYSURFACE;
    return DDERR_UNSUPPORTED;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceAddOverlayDirtyRect(
    _In_ VOID* Com,
    _In_opt_ LPRECT Rect)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if ((Surface->Caps.dwCaps & DDSCAPS_RESERVED2) != 0)
        return DDERR_INVALIDSURFACETYPE;
    if (Rect == nullptr || Rect->left < 0 || Rect->top < 0 || Rect->right < Rect->left || Rect->bottom < Rect->top)
        return E_INVALIDARG;
    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DDERR_NOTONMIPMAPSUBLEVEL;
    return DdEmulatedOnly(Surface) ? DDERR_UNSUPPORTED : S_OK;
}

HRESULT
STDMETHODCALLTYPE
DdSurfaceUpdateOverlayDisplay(
    _In_ VOID* Com,
    _In_ DWORD Flags)
{
    DdSurface* Surface = DdSurfaceFromCom(Com);

    DdLockScope Lock;

    if ((Flags & ~DdOverlayDisplayFlags) != 0)
        return E_INVALIDARG;
    if ((Surface->Caps.dwCaps & DDSCAPS_RESERVED2) != 0)
        return DDERR_INVALIDSURFACETYPE;
    if ((Surface->Caps.dwCaps & DDSCAPS_OPTIMIZED) != 0)
        return DDERR_NOTONMIPMAPSUBLEVEL;
    return DdEmulatedOnly(Surface) ? DDERR_UNSUPPORTED : S_OK;
}
