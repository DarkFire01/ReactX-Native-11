/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Memory layout facts of each format: cast family, unit size, planes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

enum RxLayoutFlag12
{
    RxLayoutTypeless = 0x01,
    RxLayoutPlanar = 0x02,
    RxLayoutYuv = 0x04,
    RxLayoutHidden = 0x08
};

struct RxFormatLayout12
{
    UINT8 Family;
    UINT8 BitsPerUnit;
    UINT8 WidthAlign;
    UINT8 HeightAlign;
    UINT8 Flags;
};

/* The row for a format, nullptr past the end of the table */
const RxFormatLayout12*
RxFormatLayout(
    _In_ DXGI_FORMAT Format);

/* Whether the API headers name this format */
BOOLEAN
RxFormatInHeader(
    _In_ DXGI_FORMAT Format);

/* The typeless format heading the format's cast family */
DXGI_FORMAT
RxFormatFamily(
    _In_ DXGI_FORMAT Format);

BOOLEAN
RxFormatIsTypeless(
    _In_ DXGI_FORMAT Format);

BOOLEAN
RxFormatIsPlanar(
    _In_ DXGI_FORMAT Format);

BOOLEAN
RxFormatIsYuv(
    _In_ DXGI_FORMAT Format);

/* Fully typed formats whose views may still change type at this level */
BOOLEAN
RxFormatCastsWhenTyped(
    _In_ DXGI_FORMAT Format,
    _In_ D3D_FEATURE_LEVEL Level);

/* Planes a resource of this format is split into for copies */
UINT
RxFormatPlaneCount(
    _In_ DXGI_FORMAT Format);

/* The copy format and size of one plane of a texture, and the width its row pitch covers */
VOID
RxFormatPlane(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Plane,
    _In_ UINT64 Width,
    _In_ UINT Height,
    _Out_ DXGI_FORMAT* PlaneFormat,
    _Out_ UINT64* PlaneWidth,
    _Out_ UINT* PlaneHeight,
    _Out_ UINT64* PitchWidth);
