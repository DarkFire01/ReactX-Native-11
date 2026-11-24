/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What each format can do, from what the driver reports for it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "rxcommon.hpp"
#include "formatsupport.hpp"

#include <d3d11.h>
#include <d3d10umddi.h>

/*
 * Per format: what every feature level 11 device supports, and which capabilities the
 * driver decides. Formats missing here do not exist for Direct3D 11.
 */
struct RxFormatCaps
{
    DXGI_FORMAT Format;
    UINT Fixed;
    UINT DriverDecides;
};

static const RxFormatCaps RxFormatTable[] =
{
    { DXGI_FORMAT_UNKNOWN,                         0x00020001, 0x00000000 },
    { DXGI_FORMAT_R32G32B32A32_TYPELESS,           0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32A32_FLOAT,              0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32A32_UINT,               0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32A32_SINT,               0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32_TYPELESS,              0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32_FLOAT,                 0x005611FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32_UINT,                  0x005211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32B32_SINT,                  0x005211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_TYPELESS,           0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_FLOAT,              0x001A11F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_UNORM,              0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_UINT,               0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_SNORM,              0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16B16A16_SINT,               0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32_TYPELESS,                 0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32_FLOAT,                    0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32_UINT,                     0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G32_SINT,                     0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32G8X24_TYPELESS,               0x001210B0, 0x7AE4E200 },
    { DXGI_FORMAT_D32_FLOAT_S8X24_UINT,            0x001310B0, 0x7AE4E200 },
    { DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,        0x045215B0, 0x7AE4E200 },
    { DXGI_FORMAT_X32_TYPELESS_G8X24_UINT,         0x005211B0, 0x7AE4E200 },
    { DXGI_FORMAT_R10G10B10A2_TYPELESS,            0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R10G10B10A2_UNORM,               0x011A11F3, 0x7AE4E200 },
    { DXGI_FORMAT_R10G10B10A2_UINT,                0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R11G11B10_FLOAT,                 0x000211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_TYPELESS,               0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_UNORM,                  0x011A11F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,             0x011A11F0, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_UINT,                   0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_SNORM,                  0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8B8A8_SINT,                   0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_TYPELESS,                 0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_FLOAT,                    0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_UNORM,                    0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_UINT,                     0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_SNORM,                    0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16G16_SINT,                     0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R32_TYPELESS,                    0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_D32_FLOAT,                       0x001310B0, 0x7AE4E200 },
    { DXGI_FORMAT_R32_FLOAT,                       0x041215FB, 0x7AE4E200 },
    { DXGI_FORMAT_R32_UINT,                        0x001211FF, 0x7AE4E200 },
    { DXGI_FORMAT_R32_SINT,                        0x001211FB, 0x7AE4E200 },
    { DXGI_FORMAT_R24G8_TYPELESS,                  0x001210B0, 0x7AE4E200 },
    { DXGI_FORMAT_D24_UNORM_S8_UINT,               0x001310B0, 0x7AE4E200 },
    { DXGI_FORMAT_R24_UNORM_X8_TYPELESS,           0x045215B0, 0x7AE4E200 },
    { DXGI_FORMAT_X24_TYPELESS_G8_UINT,            0x005211B0, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8_TYPELESS,                   0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8_UNORM,                      0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8_UINT,                       0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8_SNORM,                      0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8G8_SINT,                       0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16_TYPELESS,                    0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R16_FLOAT,                       0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_D16_UNORM,                       0x001310B0, 0x7AE4E200 },
    { DXGI_FORMAT_R16_UNORM,                       0x041215F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16_UINT,                        0x001211F7, 0x7AE4E200 },
    { DXGI_FORMAT_R16_SNORM,                       0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R16_SINT,                        0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8_TYPELESS,                     0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_R8_UNORM,                        0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8_UINT,                         0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8_SNORM,                        0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R8_SINT,                         0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_A8_UNORM,                        0x000211F0, 0x7AE4E200 },
    { DXGI_FORMAT_R9G9B9E5_SHAREDEXP,              0x000211F0, 0x7AE4C200 },
    { DXGI_FORMAT_R8G8_B8G8_UNORM,                 0x000211F0, 0x7AE4E200 },
    { DXGI_FORMAT_G8R8_G8B8_UNORM,                 0x000211F0, 0x7AE4E200 },
    { DXGI_FORMAT_BC1_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC1_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC1_UNORM_SRGB,                  0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC2_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC2_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC2_UNORM_SRGB,                  0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC3_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC3_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC3_UNORM_SRGB,                  0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC4_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC4_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC4_SNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC5_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC5_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC5_SNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_B5G6R5_UNORM,                    0x000211F0, 0x7AE4E200 },
    { DXGI_FORMAT_B5G5R5A1_UNORM,                  0x000211F0, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8A8_UNORM,                  0x011A11F3, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8X8_UNORM,                  0x001211F3, 0x7AE4E200 },
    { DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,      0x011A0020, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8A8_TYPELESS,               0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,             0x011A11F0, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8X8_TYPELESS,               0x001210F0, 0x7AE4E200 },
    { DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,             0x001211F0, 0x7AE4E200 },
    { DXGI_FORMAT_BC6H_TYPELESS,                   0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC6H_UF16,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC6H_SF16,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC7_TYPELESS,                    0x001210E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC7_UNORM,                       0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_BC7_UNORM_SRGB,                  0x001211E0, 0x7AE4E200 },
    { DXGI_FORMAT_NV12,                            0x82028120, 0x7AE4C200 },
    { DXGI_FORMAT_P010,                            0x82028120, 0x7AE4C200 },
    { DXGI_FORMAT_P016,                            0x82028120, 0x7AE4C200 },
    { DXGI_FORMAT_420_OPAQUE,                      0x10000020, 0x7AE4E200 },
    { DXGI_FORMAT_YUY2,                            0x02020120, 0x7AE4E200 },
    { DXGI_FORMAT_B4G4R4A4_UNORM,                  0x000211F0, 0x7AE4E200 },
};

/* The API capability each driver capability turns on */
static const struct
{
    UINT Driver;
    UINT Api;
} RxDriverCaps[] =
{
    { D3D10_DDI_FORMAT_SUPPORT_SHADER_SAMPLE, D3D11_FORMAT_SUPPORT_SHADER_SAMPLE },
    { D3D10_DDI_FORMAT_SUPPORT_RENDERTARGET, D3D11_FORMAT_SUPPORT_RENDER_TARGET },
    { D3D10_DDI_FORMAT_SUPPORT_BLENDABLE, D3D11_FORMAT_SUPPORT_BLENDABLE },
    { D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET, D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET },
    { D3D10_DDI_FORMAT_SUPPORT_MULTISAMPLE_LOAD, D3D11_FORMAT_SUPPORT_MULTISAMPLE_LOAD },
    { D3D11_1DDI_FORMAT_SUPPORT_UAV_WRITES, D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW },
    { D3D11_1DDI_FORMAT_SUPPORT_DECODER_OUTPUT, D3D11_FORMAT_SUPPORT_DECODER_OUTPUT },
    { D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT, D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT },
    { D3D11_1DDI_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT, D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT },
    { D3D11_1DDI_FORMAT_SUPPORT_VIDEO_ENCODER, D3D11_FORMAT_SUPPORT_VIDEO_ENCODER },
};

static
const RxFormatCaps*
RxFindFormatCaps(
    _In_ DXGI_FORMAT Format)
{
    for (const RxFormatCaps& Caps : RxFormatTable)
    {
        if (Caps.Format == Format)
            return &Caps;
    }

    return nullptr;
}

BOOLEAN
RxDeriveFormatSupport(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Driver,
    _Out_ UINT* Support)
{
    const RxFormatCaps* Caps = RxFindFormatCaps(Format);
    UINT Allowed;
    UINT Bits;

    *Support = 0;
    if (Caps == nullptr)
        return FALSE;

    Allowed = Caps->DriverDecides;
    Bits = Caps->Fixed;
    for (const auto& Entry : RxDriverCaps)
    {
        if ((Driver & Entry.Driver) && (Allowed & Entry.Api))
            Bits |= Entry.Api;
    }

    /* Gather comes with filtering, and mips can be generated when the format renders and blends */
    if ((Bits & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) && (Allowed & D3D11_FORMAT_SUPPORT_SHADER_GATHER))
        Bits |= D3D11_FORMAT_SUPPORT_SHADER_GATHER;
    if ((Bits & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) && (Bits & D3D11_FORMAT_SUPPORT_RENDER_TARGET) &&
        (Bits & D3D11_FORMAT_SUPPORT_BLENDABLE) && (Allowed & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN))
    {
        Bits |= D3D11_FORMAT_SUPPORT_MIP_AUTOGEN;
    }
    if ((Bits & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) && (Bits & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) &&
        (Allowed & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE))
    {
        Bits |= D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE;
    }

    *Support = Bits;
    return TRUE;
}

/*
 * The FORMAT_SUPPORT2 answers: what is fixed for each format and what the driver decides.
 * A format with nothing to report fails the query.
 */
static const RxFormatCaps RxFormatTable2[] =
{
    { DXGI_FORMAT_R32G32B32A32_TYPELESS,           0x00000600, 0x00014340 },
    { DXGI_FORMAT_R32G32B32A32_FLOAT,              0x00000680, 0x00014340 },
    { DXGI_FORMAT_R32G32B32A32_UINT,               0x00000680, 0x00014340 },
    { DXGI_FORMAT_R32G32B32A32_SINT,               0x00000680, 0x00014240 },
    { DXGI_FORMAT_R32G32B32_UINT,                  0x00000100, 0x00014140 },
    { DXGI_FORMAT_R16G16B16A16_TYPELESS,           0x00000600, 0x00014340 },
    { DXGI_FORMAT_R16G16B16A16_FLOAT,              0x00010480, 0x00014340 },
    { DXGI_FORMAT_R16G16B16A16_UNORM,              0x00000480, 0x00010340 },
    { DXGI_FORMAT_R16G16B16A16_UINT,               0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16G16B16A16_SNORM,              0x00000480, 0x00010340 },
    { DXGI_FORMAT_R16G16B16A16_SINT,               0x00000480, 0x00014240 },
    { DXGI_FORMAT_R32G32_TYPELESS,                 0x00000200, 0x00014340 },
    { DXGI_FORMAT_R32G32_FLOAT,                    0x00000080, 0x00014340 },
    { DXGI_FORMAT_R32G32_UINT,                     0x00000080, 0x00014340 },
    { DXGI_FORMAT_R32G32_SINT,                     0x00000080, 0x00014240 },
    { DXGI_FORMAT_R10G10B10A2_TYPELESS,            0x00000600, 0x00014340 },
    { DXGI_FORMAT_R10G10B10A2_UNORM,               0x00010480, 0x00014340 },
    { DXGI_FORMAT_R10G10B10A2_UINT,                0x00000480, 0x00014340 },
    { DXGI_FORMAT_R11G11B10_FLOAT,                 0x00000480, 0x00010340 },
    { DXGI_FORMAT_R8G8B8A8_TYPELESS,               0x00000600, 0x00014340 },
    { DXGI_FORMAT_R8G8B8A8_UNORM,                  0x00010480, 0x00014340 },
    { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,             0x00000400, 0x00010340 },
    { DXGI_FORMAT_R8G8B8A8_UINT,                   0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8G8B8A8_SNORM,                  0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8G8B8A8_SINT,                   0x00000480, 0x00014240 },
    { DXGI_FORMAT_R16G16_TYPELESS,                 0x00000600, 0x00014340 },
    { DXGI_FORMAT_R16G16_FLOAT,                    0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16G16_UNORM,                    0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16G16_UINT,                     0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16G16_SNORM,                    0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16G16_SINT,                     0x00000480, 0x00014240 },
    { DXGI_FORMAT_R32_TYPELESS,                    0x00000600, 0x00014340 },
    { DXGI_FORMAT_D32_FLOAT,                       0x00000400, 0x00014340 },
    { DXGI_FORMAT_R32_FLOAT,                       0x00000488, 0x00014340 },
    { DXGI_FORMAT_R32_UINT,                        0x000004BF, 0x00014340 },
    { DXGI_FORMAT_R32_SINT,                        0x000004BF, 0x00014240 },
    { DXGI_FORMAT_R8G8_TYPELESS,                   0x00000600, 0x00014340 },
    { DXGI_FORMAT_R8G8_UNORM,                      0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8G8_UINT,                       0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8G8_SNORM,                      0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8G8_SINT,                       0x00000480, 0x00014240 },
    { DXGI_FORMAT_R16_TYPELESS,                    0x00000600, 0x00014340 },
    { DXGI_FORMAT_R16_FLOAT,                       0x00000480, 0x00014340 },
    { DXGI_FORMAT_D16_UNORM,                       0x00000400, 0x00014340 },
    { DXGI_FORMAT_R16_UNORM,                       0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16_UINT,                        0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16_SNORM,                       0x00000480, 0x00014340 },
    { DXGI_FORMAT_R16_SINT,                        0x00000480, 0x00014240 },
    { DXGI_FORMAT_R8_TYPELESS,                     0x00000600, 0x00014340 },
    { DXGI_FORMAT_R8_UNORM,                        0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8_UINT,                         0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8_SNORM,                        0x00000480, 0x00014340 },
    { DXGI_FORMAT_R8_SINT,                         0x00000480, 0x00014240 },
    { DXGI_FORMAT_A8_UNORM,                        0x00000480, 0x00014340 },
    { DXGI_FORMAT_R9G9B9E5_SHAREDEXP,              0x00000480, 0x00014340 },
    { DXGI_FORMAT_BC1_TYPELESS,                    0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC1_UNORM,                       0x00000400, 0x00014340 },
    { DXGI_FORMAT_BC1_UNORM_SRGB,                  0x00000400, 0x00014340 },
    { DXGI_FORMAT_BC2_TYPELESS,                    0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC2_UNORM,                       0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC2_UNORM_SRGB,                  0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC3_TYPELESS,                    0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC3_UNORM,                       0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC3_UNORM_SRGB,                  0x00000600, 0x00014340 },
    { DXGI_FORMAT_BC4_TYPELESS,                    0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC4_UNORM,                       0x00000000, 0x00014340 },
    { DXGI_FORMAT_BC4_SNORM,                       0x00000000, 0x00014340 },
    { DXGI_FORMAT_BC5_TYPELESS,                    0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC5_UNORM,                       0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC5_SNORM,                       0x00000200, 0x00014340 },
    { DXGI_FORMAT_B5G6R5_UNORM,                    0x00000080, 0x00014340 },
    { DXGI_FORMAT_B5G5R5A1_UNORM,                  0x00000080, 0x00010340 },
    { DXGI_FORMAT_B8G8R8A8_UNORM,                  0x00010480, 0x00014340 },
    { DXGI_FORMAT_B8G8R8X8_UNORM,                  0x00000400, 0x00010340 },
    { DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM,      0x00000400, 0x00010340 },
    { DXGI_FORMAT_B8G8R8A8_TYPELESS,               0x00000600, 0x00014340 },
    { DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,             0x00000400, 0x00014340 },
    { DXGI_FORMAT_B8G8R8X8_TYPELESS,               0x00000600, 0x00014340 },
    { DXGI_FORMAT_B8G8R8X8_UNORM_SRGB,             0x00000400, 0x00014340 },
    { DXGI_FORMAT_BC6H_TYPELESS,                   0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC6H_UF16,                       0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC6H_SF16,                       0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC7_TYPELESS,                    0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC7_UNORM,                       0x00000200, 0x00014340 },
    { DXGI_FORMAT_BC7_UNORM_SRGB,                  0x00000200, 0x00014340 },
    /* Video formats are shareable and never tiled */
    { DXGI_FORMAT_AYUV,                            0x00000480, 0x00010140 },
    { DXGI_FORMAT_Y410,                            0x00000480, 0x00010140 },
    { DXGI_FORMAT_Y416,                            0x00000480, 0x00010140 },
    { DXGI_FORMAT_NV12,                            0x00010480, 0x00014140 },
    { DXGI_FORMAT_P010,                            0x00000480, 0x00014140 },
    { DXGI_FORMAT_P016,                            0x00000480, 0x00014140 },
    { DXGI_FORMAT_420_OPAQUE,                      0x00000400, 0x00014140 },
    { DXGI_FORMAT_YUY2,                            0x00010480, 0x00014140 },
    { DXGI_FORMAT_Y210,                            0x00000480, 0x00010140 },
    { DXGI_FORMAT_Y216,                            0x00000480, 0x00010140 },
    { DXGI_FORMAT_B4G4R4A4_UNORM,                  0x00000080, 0x00014340 },
};

static const struct
{
    UINT Driver;
    UINT Api;
} RxDriverCaps2[] =
{
    { D3D11_1DDI_FORMAT_SUPPORT_OUTPUT_MERGER_LOGIC_OP, D3D11_FORMAT_SUPPORT2_OUTPUT_MERGER_LOGIC_OP },
    { D3DWDDM1_3DDI_FORMAT_SUPPORT_TILED, D3D11_FORMAT_SUPPORT2_TILED },
    { D3DWDDM2_0DDI_FORMAT_SUPPORT_UAV_READS, D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD },
    { D3D11_1DDI_FORMAT_SUPPORT_MULTIPLANE_OVERLAY, D3D11_FORMAT_SUPPORT2_MULTIPLANE_OVERLAY },
    { D3DWDDM3_0DDI_FORMAT_SUPPORT_DISPLAYABLE, D3D11_FORMAT_SUPPORT2_DISPLAYABLE },
};

BOOLEAN
RxDeriveFormatSupport2(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Driver,
    _Out_ UINT* Support)
{
    const RxFormatCaps* Caps = nullptr;
    UINT Bits;

    *Support = 0;
    for (const RxFormatCaps& Entry : RxFormatTable2)
    {
        if (Entry.Format == Format)
            Caps = &Entry;
    }

    if (Caps == nullptr)
        return FALSE;

    Bits = Caps->Fixed;
    for (const auto& Entry : RxDriverCaps2)
    {
        if ((Driver & Entry.Driver) && (Caps->DriverDecides & Entry.Api))
            Bits |= Entry.Api;
    }

    *Support = Bits;
    return Bits != 0;
}
