/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Memory layout facts of each format: cast family, unit size, planes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "formatlayout12.hpp"

/* Family, bits per unit, width and height alignment, flags; one row per DXGI_FORMAT */
static const RxFormatLayout12 RxLayouts[] =
{
    { 0, 0, 1, 1, 0 },  /* 0 UNKNOWN */
    { 1, 128, 1, 1, RxLayoutTypeless },  /* 1 R32G32B32A32_TYPELESS */
    { 1, 128, 1, 1, 0 },  /* 2 R32G32B32A32_FLOAT */
    { 1, 128, 1, 1, 0 },  /* 3 R32G32B32A32_UINT */
    { 1, 128, 1, 1, 0 },  /* 4 R32G32B32A32_SINT */
    { 5, 96, 1, 1, RxLayoutTypeless },  /* 5 R32G32B32_TYPELESS */
    { 5, 96, 1, 1, 0 },  /* 6 R32G32B32_FLOAT */
    { 5, 96, 1, 1, 0 },  /* 7 R32G32B32_UINT */
    { 5, 96, 1, 1, 0 },  /* 8 R32G32B32_SINT */
    { 9, 64, 1, 1, RxLayoutTypeless },  /* 9 R16G16B16A16_TYPELESS */
    { 9, 64, 1, 1, 0 },  /* 10 R16G16B16A16_FLOAT */
    { 9, 64, 1, 1, 0 },  /* 11 R16G16B16A16_UNORM */
    { 9, 64, 1, 1, 0 },  /* 12 R16G16B16A16_UINT */
    { 9, 64, 1, 1, 0 },  /* 13 R16G16B16A16_SNORM */
    { 9, 64, 1, 1, 0 },  /* 14 R16G16B16A16_SINT */
    { 15, 64, 1, 1, RxLayoutTypeless },  /* 15 R32G32_TYPELESS */
    { 15, 64, 1, 1, 0 },  /* 16 R32G32_FLOAT */
    { 15, 64, 1, 1, 0 },  /* 17 R32G32_UINT */
    { 15, 64, 1, 1, 0 },  /* 18 R32G32_SINT */
    { 19, 64, 1, 1, RxLayoutTypeless | RxLayoutPlanar },  /* 19 R32G8X24_TYPELESS */
    { 19, 64, 1, 1, RxLayoutPlanar },  /* 20 D32_FLOAT_S8X24_UINT */
    { 19, 64, 1, 1, RxLayoutPlanar },  /* 21 R32_FLOAT_X8X24_TYPELESS */
    { 19, 64, 1, 1, RxLayoutPlanar },  /* 22 X32_TYPELESS_G8X24_UINT */
    { 23, 32, 1, 1, RxLayoutTypeless },  /* 23 R10G10B10A2_TYPELESS */
    { 23, 32, 1, 1, 0 },  /* 24 R10G10B10A2_UNORM */
    { 23, 32, 1, 1, 0 },  /* 25 R10G10B10A2_UINT */
    { 26, 32, 1, 1, 0 },  /* 26 R11G11B10_FLOAT */
    { 27, 32, 1, 1, RxLayoutTypeless },  /* 27 R8G8B8A8_TYPELESS */
    { 27, 32, 1, 1, 0 },  /* 28 R8G8B8A8_UNORM */
    { 27, 32, 1, 1, 0 },  /* 29 R8G8B8A8_UNORM_SRGB */
    { 27, 32, 1, 1, 0 },  /* 30 R8G8B8A8_UINT */
    { 27, 32, 1, 1, 0 },  /* 31 R8G8B8A8_SNORM */
    { 27, 32, 1, 1, 0 },  /* 32 R8G8B8A8_SINT */
    { 33, 32, 1, 1, RxLayoutTypeless },  /* 33 R16G16_TYPELESS */
    { 33, 32, 1, 1, 0 },  /* 34 R16G16_FLOAT */
    { 33, 32, 1, 1, 0 },  /* 35 R16G16_UNORM */
    { 33, 32, 1, 1, 0 },  /* 36 R16G16_UINT */
    { 33, 32, 1, 1, 0 },  /* 37 R16G16_SNORM */
    { 33, 32, 1, 1, 0 },  /* 38 R16G16_SINT */
    { 39, 32, 1, 1, RxLayoutTypeless },  /* 39 R32_TYPELESS */
    { 39, 32, 1, 1, 0 },  /* 40 D32_FLOAT */
    { 39, 32, 1, 1, 0 },  /* 41 R32_FLOAT */
    { 39, 32, 1, 1, 0 },  /* 42 R32_UINT */
    { 39, 32, 1, 1, 0 },  /* 43 R32_SINT */
    { 44, 32, 1, 1, RxLayoutTypeless | RxLayoutPlanar },  /* 44 R24G8_TYPELESS */
    { 44, 32, 1, 1, RxLayoutPlanar },  /* 45 D24_UNORM_S8_UINT */
    { 44, 32, 1, 1, RxLayoutPlanar },  /* 46 R24_UNORM_X8_TYPELESS */
    { 44, 32, 1, 1, RxLayoutPlanar },  /* 47 X24_TYPELESS_G8_UINT */
    { 48, 16, 1, 1, RxLayoutTypeless },  /* 48 R8G8_TYPELESS */
    { 48, 16, 1, 1, 0 },  /* 49 R8G8_UNORM */
    { 48, 16, 1, 1, 0 },  /* 50 R8G8_UINT */
    { 48, 16, 1, 1, 0 },  /* 51 R8G8_SNORM */
    { 48, 16, 1, 1, 0 },  /* 52 R8G8_SINT */
    { 53, 16, 1, 1, RxLayoutTypeless },  /* 53 R16_TYPELESS */
    { 53, 16, 1, 1, 0 },  /* 54 R16_FLOAT */
    { 53, 16, 1, 1, 0 },  /* 55 D16_UNORM */
    { 53, 16, 1, 1, 0 },  /* 56 R16_UNORM */
    { 53, 16, 1, 1, 0 },  /* 57 R16_UINT */
    { 53, 16, 1, 1, 0 },  /* 58 R16_SNORM */
    { 53, 16, 1, 1, 0 },  /* 59 R16_SINT */
    { 60, 8, 1, 1, RxLayoutTypeless },  /* 60 R8_TYPELESS */
    { 60, 8, 1, 1, 0 },  /* 61 R8_UNORM */
    { 60, 8, 1, 1, 0 },  /* 62 R8_UINT */
    { 60, 8, 1, 1, 0 },  /* 63 R8_SNORM */
    { 60, 8, 1, 1, 0 },  /* 64 R8_SINT */
    { 65, 8, 1, 1, 0 },  /* 65 A8_UNORM */
    { 66, 1, 8, 1, 0 },  /* 66 R1_UNORM */
    { 67, 32, 1, 1, 0 },  /* 67 R9G9B9E5_SHAREDEXP */
    { 68, 16, 2, 1, 0 },  /* 68 R8G8_B8G8_UNORM */
    { 69, 16, 2, 1, 0 },  /* 69 G8R8_G8B8_UNORM */
    { 70, 64, 4, 4, RxLayoutTypeless },  /* 70 BC1_TYPELESS */
    { 70, 64, 4, 4, 0 },  /* 71 BC1_UNORM */
    { 70, 64, 4, 4, 0 },  /* 72 BC1_UNORM_SRGB */
    { 73, 128, 4, 4, RxLayoutTypeless },  /* 73 BC2_TYPELESS */
    { 73, 128, 4, 4, 0 },  /* 74 BC2_UNORM */
    { 73, 128, 4, 4, 0 },  /* 75 BC2_UNORM_SRGB */
    { 76, 128, 4, 4, RxLayoutTypeless },  /* 76 BC3_TYPELESS */
    { 76, 128, 4, 4, 0 },  /* 77 BC3_UNORM */
    { 76, 128, 4, 4, 0 },  /* 78 BC3_UNORM_SRGB */
    { 79, 64, 4, 4, RxLayoutTypeless },  /* 79 BC4_TYPELESS */
    { 79, 64, 4, 4, 0 },  /* 80 BC4_UNORM */
    { 79, 64, 4, 4, 0 },  /* 81 BC4_SNORM */
    { 82, 128, 4, 4, RxLayoutTypeless },  /* 82 BC5_TYPELESS */
    { 82, 128, 4, 4, 0 },  /* 83 BC5_UNORM */
    { 82, 128, 4, 4, 0 },  /* 84 BC5_SNORM */
    { 85, 16, 1, 1, 0 },  /* 85 B5G6R5_UNORM */
    { 86, 16, 1, 1, 0 },  /* 86 B5G5R5A1_UNORM */
    { 90, 32, 1, 1, 0 },  /* 87 B8G8R8A8_UNORM */
    { 92, 32, 1, 1, 0 },  /* 88 B8G8R8X8_UNORM */
    { 23, 32, 1, 1, 0 },  /* 89 R10G10B10_XR_BIAS_A2_UNORM */
    { 90, 32, 1, 1, RxLayoutTypeless },  /* 90 B8G8R8A8_TYPELESS */
    { 90, 32, 1, 1, 0 },  /* 91 B8G8R8A8_UNORM_SRGB */
    { 92, 32, 1, 1, RxLayoutTypeless },  /* 92 B8G8R8X8_TYPELESS */
    { 92, 32, 1, 1, 0 },  /* 93 B8G8R8X8_UNORM_SRGB */
    { 94, 128, 4, 4, RxLayoutTypeless },  /* 94 BC6H_TYPELESS */
    { 94, 128, 4, 4, 0 },  /* 95 BC6H_UF16 */
    { 94, 128, 4, 4, 0 },  /* 96 BC6H_SF16 */
    { 97, 128, 4, 4, RxLayoutTypeless },  /* 97 BC7_TYPELESS */
    { 97, 128, 4, 4, 0 },  /* 98 BC7_UNORM */
    { 97, 128, 4, 4, 0 },  /* 99 BC7_UNORM_SRGB */
    { 100, 32, 1, 1, RxLayoutYuv },  /* 100 AYUV */
    { 101, 32, 1, 1, RxLayoutYuv },  /* 101 Y410 */
    { 102, 64, 1, 1, RxLayoutYuv },  /* 102 Y416 */
    { 103, 8, 2, 2, RxLayoutPlanar | RxLayoutYuv },  /* 103 NV12 */
    { 104, 16, 2, 2, RxLayoutPlanar | RxLayoutYuv },  /* 104 P010 */
    { 105, 16, 2, 2, RxLayoutPlanar | RxLayoutYuv },  /* 105 P016 */
    { 106, 8, 2, 2, RxLayoutPlanar | RxLayoutYuv },  /* 106 420_OPAQUE */
    { 107, 16, 2, 1, RxLayoutYuv },  /* 107 YUY2 */
    { 108, 32, 2, 1, RxLayoutYuv },  /* 108 Y210 */
    { 109, 32, 2, 1, RxLayoutYuv },  /* 109 Y216 */
    { 110, 8, 4, 1, RxLayoutPlanar | RxLayoutYuv },  /* 110 NV11 */
    { 111, 8, 1, 1, RxLayoutYuv },  /* 111 AI44 */
    { 112, 8, 1, 1, RxLayoutYuv },  /* 112 IA44 */
    { 113, 8, 1, 1, RxLayoutYuv },  /* 113 P8 */
    { 114, 16, 1, 1, RxLayoutYuv },  /* 114 A8P8 */
    { 115, 16, 1, 1, 0 },  /* 115 B4G4R4A4_UNORM */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 116 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 117 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 118 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 119 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 120 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 121 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 122 */
    { 123, 8, 1, 1, RxLayoutHidden },  /* 123 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 124 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 125 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 126 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 127 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 128 */
    { 0, 0, 1, 1, RxLayoutHidden },  /* 129 */
    { 130, 8, 2, 1, RxLayoutPlanar | RxLayoutYuv },  /* 130 P208 */
    { 131, 8, 1, 2, RxLayoutPlanar | RxLayoutYuv },  /* 131 V208 */
    { 132, 8, 1, 1, RxLayoutPlanar | RxLayoutYuv },  /* 132 V408 */
    { 133, 128, 4, 4, RxLayoutTypeless | RxLayoutHidden },  /* 133 */
    { 133, 128, 4, 4, RxLayoutHidden },  /* 134 */
    { 133, 128, 4, 4, RxLayoutHidden },  /* 135 */
    { 133, 128, 4, 4, RxLayoutHidden },  /* 136 */
    { 137, 128, 5, 4, RxLayoutTypeless | RxLayoutHidden },  /* 137 */
    { 137, 128, 5, 4, RxLayoutHidden },  /* 138 */
    { 137, 128, 5, 4, RxLayoutHidden },  /* 139 */
    { 137, 128, 5, 4, RxLayoutHidden },  /* 140 */
    { 141, 128, 5, 5, RxLayoutTypeless | RxLayoutHidden },  /* 141 */
    { 141, 128, 5, 5, RxLayoutHidden },  /* 142 */
    { 141, 128, 5, 5, RxLayoutHidden },  /* 143 */
    { 141, 128, 5, 5, RxLayoutHidden },  /* 144 */
    { 145, 128, 6, 5, RxLayoutTypeless | RxLayoutHidden },  /* 145 */
    { 145, 128, 6, 5, RxLayoutHidden },  /* 146 */
    { 145, 128, 6, 5, RxLayoutHidden },  /* 147 */
    { 145, 128, 6, 5, RxLayoutHidden },  /* 148 */
    { 149, 128, 6, 6, RxLayoutTypeless | RxLayoutHidden },  /* 149 */
    { 149, 128, 6, 6, RxLayoutHidden },  /* 150 */
    { 149, 128, 6, 6, RxLayoutHidden },  /* 151 */
    { 149, 128, 6, 6, RxLayoutHidden },  /* 152 */
    { 153, 128, 8, 5, RxLayoutTypeless | RxLayoutHidden },  /* 153 */
    { 153, 128, 8, 5, RxLayoutHidden },  /* 154 */
    { 153, 128, 8, 5, RxLayoutHidden },  /* 155 */
    { 153, 128, 8, 5, RxLayoutHidden },  /* 156 */
    { 157, 128, 8, 6, RxLayoutTypeless | RxLayoutHidden },  /* 157 */
    { 157, 128, 8, 6, RxLayoutHidden },  /* 158 */
    { 157, 128, 8, 6, RxLayoutHidden },  /* 159 */
    { 157, 128, 8, 6, RxLayoutHidden },  /* 160 */
    { 161, 128, 8, 8, RxLayoutTypeless | RxLayoutHidden },  /* 161 */
    { 161, 128, 8, 8, RxLayoutHidden },  /* 162 */
    { 161, 128, 8, 8, RxLayoutHidden },  /* 163 */
    { 161, 128, 8, 8, RxLayoutHidden },  /* 164 */
    { 165, 128, 10, 5, RxLayoutTypeless | RxLayoutHidden },  /* 165 */
    { 165, 128, 10, 5, RxLayoutHidden },  /* 166 */
    { 165, 128, 10, 5, RxLayoutHidden },  /* 167 */
    { 165, 128, 10, 5, RxLayoutHidden },  /* 168 */
    { 169, 128, 10, 6, RxLayoutTypeless | RxLayoutHidden },  /* 169 */
    { 169, 128, 10, 6, RxLayoutHidden },  /* 170 */
    { 169, 128, 10, 6, RxLayoutHidden },  /* 171 */
    { 169, 128, 10, 6, RxLayoutHidden },  /* 172 */
    { 173, 128, 10, 8, RxLayoutTypeless | RxLayoutHidden },  /* 173 */
    { 173, 128, 10, 8, RxLayoutHidden },  /* 174 */
    { 173, 128, 10, 8, RxLayoutHidden },  /* 175 */
    { 173, 128, 10, 8, RxLayoutHidden },  /* 176 */
    { 177, 128, 10, 10, RxLayoutTypeless | RxLayoutHidden },  /* 177 */
    { 177, 128, 10, 10, RxLayoutHidden },  /* 178 */
    { 177, 128, 10, 10, RxLayoutHidden },  /* 179 */
    { 177, 128, 10, 10, RxLayoutHidden },  /* 180 */
    { 181, 128, 12, 10, RxLayoutTypeless | RxLayoutHidden },  /* 181 */
    { 181, 128, 12, 10, RxLayoutHidden },  /* 182 */
    { 181, 128, 12, 10, RxLayoutHidden },  /* 183 */
    { 181, 128, 12, 10, RxLayoutHidden },  /* 184 */
    { 185, 128, 12, 12, RxLayoutTypeless | RxLayoutHidden },  /* 185 */
    { 185, 128, 12, 12, RxLayoutHidden },  /* 186 */
    { 185, 128, 12, 12, RxLayoutHidden },  /* 187 */
    { 185, 128, 12, 12, RxLayoutHidden },  /* 188 */
    { 0, 8, 1, 1, 0 },  /* 189 SAMPLER_FEEDBACK_MIN_MIP_OPAQUE */
    { 0, 8, 1, 1, 0 },  /* 190 SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE */
    { 191, 16, 1, 1, 0 },  /* 191 A4B4G4R4_UNORM */
};

const RxFormatLayout12*
RxFormatLayout(
    _In_ DXGI_FORMAT Format)
{
    if ((UINT)Format >= _countof(RxLayouts))
        return nullptr;
    return &RxLayouts[Format];
}

BOOLEAN
RxFormatInHeader(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutHidden) == 0;
}

DXGI_FORMAT
RxFormatFamily(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr ? (DXGI_FORMAT)Layout->Family : DXGI_FORMAT_UNKNOWN;
}

BOOLEAN
RxFormatIsTypeless(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutTypeless) != 0;
}

BOOLEAN
RxFormatIsPlanar(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutPlanar) != 0;
}

BOOLEAN
RxFormatIsYuv(
    _In_ DXGI_FORMAT Format)
{
    const RxFormatLayout12* Layout = RxFormatLayout(Format);

    return Layout != nullptr && (Layout->Flags & RxLayoutYuv) != 0;
}

BOOLEAN
RxFormatCastsWhenTyped(
    _In_ DXGI_FORMAT Format,
    _In_ D3D_FEATURE_LEVEL Level)
{
    if (Level == D3D_FEATURE_LEVEL_1_0_CORE || Level == D3D_FEATURE_LEVEL_1_0_GENERIC)
        return FALSE;

    switch (Format)
    {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return TRUE;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
            return Level >= D3D_FEATURE_LEVEL_10_0;
        default:
            return FALSE;
    }
}

UINT
RxFormatPlaneCount(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_V208:
        case DXGI_FORMAT_V408:
            return 3;
        default:
            return RxFormatIsPlanar(Format) ? 2 : 1;
    }
}

VOID
RxFormatPlane(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Plane,
    _In_ UINT64 Width,
    _In_ UINT Height,
    _Out_ DXGI_FORMAT* PlaneFormat,
    _Out_ UINT64* PlaneWidth,
    _Out_ UINT* PlaneHeight,
    _Out_ UINT64* PitchWidth)
{
    BOOLEAN Chroma = Plane != 0;

    *PlaneFormat = Format;
    *PlaneWidth = Width;
    *PlaneHeight = Height;

    switch (RxFormatFamily(Format))
    {
        /* Depth in a 32 bit plane, stencil in an 8 bit one */
        case DXGI_FORMAT_R32G8X24_TYPELESS:
        case DXGI_FORMAT_R24G8_TYPELESS:
            *PlaneFormat = Chroma ? DXGI_FORMAT_R8_TYPELESS : DXGI_FORMAT_R32_TYPELESS;
            break;
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_420_OPAQUE:
            *PlaneFormat = Chroma ? DXGI_FORMAT_R8G8_TYPELESS : DXGI_FORMAT_R8_TYPELESS;
            if (Chroma)
            {
                *PlaneWidth = (Width + 1) / 2;
                *PlaneHeight = (Height + 1) / 2;
            }
            break;
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
            *PlaneFormat = Chroma ? DXGI_FORMAT_R16G16_TYPELESS : DXGI_FORMAT_R16_TYPELESS;
            if (Chroma)
            {
                *PlaneWidth = (Width + 1) / 2;
                *PlaneHeight = (Height + 1) / 2;
            }
            break;
        case DXGI_FORMAT_NV11:
            *PlaneFormat = Chroma ? DXGI_FORMAT_R8G8_TYPELESS : DXGI_FORMAT_R8_TYPELESS;
            if (Chroma)
                *PlaneWidth = (Width + 3) / 4;
            break;
        case DXGI_FORMAT_P208:
            *PlaneFormat = Chroma ? DXGI_FORMAT_R8G8_TYPELESS : DXGI_FORMAT_R8_TYPELESS;
            if (Chroma)
                *PlaneWidth = (Width + 1) / 2;
            break;
        case DXGI_FORMAT_V208:
            *PlaneFormat = DXGI_FORMAT_R8_TYPELESS;
            if (Chroma)
                *PlaneHeight = (Height + 1) / 2;
            break;
        case DXGI_FORMAT_V408:
            *PlaneFormat = DXGI_FORMAT_R8_TYPELESS;
            break;
        default:
            break;
    }

    /* The 4:1:1 chroma plane keeps the pitch of a 4:2:2 one */
    *PitchWidth = *PlaneWidth;
    if (RxFormatFamily(Format) == DXGI_FORMAT_NV11 && Chroma)
        *PitchWidth = (Width + 1) / 2;
}
