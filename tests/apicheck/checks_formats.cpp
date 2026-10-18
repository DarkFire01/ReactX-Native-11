/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Texture creation across every format, sizes and initial data
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "check.hpp"

#include <d3d11_4.h>
#include <vector>

/* Bytes per row of one mip, treating block compressed formats as rows of 4x4 blocks */
static
UINT
AcRowPitch(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _Out_ UINT* Rows,
    _In_ UINT Height)
{
    UINT BlockBytes = 0;

    switch (Format)
    {
        case DXGI_FORMAT_BC1_TYPELESS:
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_TYPELESS:
        case DXGI_FORMAT_BC4_UNORM:
        case DXGI_FORMAT_BC4_SNORM:
            BlockBytes = 8;
            break;
        case DXGI_FORMAT_BC2_TYPELESS:
        case DXGI_FORMAT_BC2_UNORM:
        case DXGI_FORMAT_BC2_UNORM_SRGB:
        case DXGI_FORMAT_BC3_TYPELESS:
        case DXGI_FORMAT_BC3_UNORM:
        case DXGI_FORMAT_BC3_UNORM_SRGB:
        case DXGI_FORMAT_BC5_TYPELESS:
        case DXGI_FORMAT_BC5_UNORM:
        case DXGI_FORMAT_BC5_SNORM:
        case DXGI_FORMAT_BC6H_TYPELESS:
        case DXGI_FORMAT_BC6H_UF16:
        case DXGI_FORMAT_BC6H_SF16:
        case DXGI_FORMAT_BC7_TYPELESS:
        case DXGI_FORMAT_BC7_UNORM:
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            BlockBytes = 16;
            break;
        default:
            break;
    }

    if (BlockBytes != 0)
    {
        *Rows = (Height + 3) / 4;
        return ((Width + 3) / 4) * BlockBytes;
    }

    /* Generous for every uncompressed format: 16 bytes a texel */
    *Rows = Height;
    return Width * 16;
}

/* A texture of one format with a full mip chain, filled from memory when asked */
static
HRESULT
AcCreateFormatTexture(
    _In_ CheckContext& Check,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ UINT BindFlags,
    _In_ BOOLEAN WithData)
{
    std::vector<D3D11_SUBRESOURCE_DATA> Initial;
    std::vector<std::vector<BYTE>> Storage;
    D3D11_TEXTURE2D_DESC Desc = {};
    ComPtr<ID3D11Texture2D> Texture;
    UINT Levels = 1;
    UINT Size = (Width > Height) ? Width : Height;
    UINT Level;
    UINT Rows;
    UINT Pitch;

    while (Size > 1)
    {
        Size >>= 1;
        Levels++;
    }

    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = Levels;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;

    if (WithData)
    {
        for (Level = 0; Level < Levels; Level++)
        {
            UINT LevelWidth = (Width >> Level) ? (Width >> Level) : 1;
            UINT LevelHeight = (Height >> Level) ? (Height >> Level) : 1;

            Pitch = AcRowPitch(Format, LevelWidth, &Rows, LevelHeight);
            Storage.emplace_back((SIZE_T)Pitch * Rows, (BYTE)0x5A);
            Initial.push_back({ Storage.back().data(), Pitch, Pitch * Rows });
        }
    }

    return Check.Device->CreateTexture2D(&Desc, WithData ? Initial.data() : nullptr, &Texture);
}

/* formats: every DXGI format as a sampled texture, with and without data, at awkward sizes */

static
VOID
AcCheckFormats(
    _In_ CheckContext& Check)
{
    char Key[64];
    UINT Support;
    UINT Format;
    HRESULT Plain;
    HRESULT Filled;
    HRESULT Odd;

    for (Format = 1; Format <= DXGI_FORMAT_B4G4R4A4_UNORM; Format++)
    {
        Support = 0;
        sprintf_s(Key, "support%u", Format);
        Plain = Check.Device->CheckFormatSupport((DXGI_FORMAT)Format, &Support);
        Check.Report(Key, "0x%08lX %08X", Plain, Support);

        /* A format the device does not know can crash the driver, as on Windows */
        if (FAILED(Plain))
            continue;

        Plain = AcCreateFormatTexture(Check, (DXGI_FORMAT)Format, 64, 64, D3D11_BIND_SHADER_RESOURCE, FALSE);
        Filled = AcCreateFormatTexture(Check, (DXGI_FORMAT)Format, 64, 64, D3D11_BIND_SHADER_RESOURCE, TRUE);
        Odd = AcCreateFormatTexture(Check, (DXGI_FORMAT)Format, 100, 36, D3D11_BIND_SHADER_RESOURCE, TRUE);
        sprintf_s(Key, "format%u", Format);
        Check.Report(Key, "plain 0x%08lX data 0x%08lX odd 0x%08lX", Plain, Filled, Odd);
    }
}

/* features: every CheckFeatureSupport answer, raw, plus a wrong size for each */

static
VOID
AcReportFeature(
    _In_ CheckContext& Check,
    _In_ D3D11_FEATURE Feature,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    char Key[64];
    char Text[512];
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    HRESULT hr;
    UINT Index;
    int Used;

    hr = Check.Device->CheckFeatureSupport(Feature, Data, Size);
    Used = sprintf_s(Text, "0x%08lX", hr);
    for (Index = 0; Index < Size && SUCCEEDED(hr); Index += 4)
        Used += sprintf_s(Text + Used, sizeof(Text) - Used, " %08X", *reinterpret_cast<const UINT*>(Bytes + Index));

    sprintf_s(Key, "feature%u", (UINT)Feature);
    Check.Report(Key, "%s", Text);
}

static
VOID
AcCheckFeatures(
    _In_ CheckContext& Check)
{
    BYTE Data[256];
    char Key[64];
    UINT Format;
    UINT Feature;
    static const UINT Sizes[] =
    {
        sizeof(D3D11_FEATURE_DATA_THREADING), sizeof(D3D11_FEATURE_DATA_DOUBLES),
        sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT), sizeof(D3D11_FEATURE_DATA_FORMAT_SUPPORT2),
        sizeof(D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS), sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS),
        sizeof(D3D11_FEATURE_DATA_ARCHITECTURE_INFO), sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS),
        sizeof(D3D11_FEATURE_DATA_SHADER_MIN_PRECISION_SUPPORT), sizeof(D3D11_FEATURE_DATA_D3D9_SHADOW_SUPPORT),
        sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS1), sizeof(D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT),
        sizeof(D3D11_FEATURE_DATA_MARKER_SUPPORT), sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS1),
        sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS2), sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS3),
        sizeof(D3D11_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT), sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS4),
        sizeof(D3D11_FEATURE_DATA_SHADER_CACHE), sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS5),
        sizeof(D3D11_FEATURE_DATA_DISPLAYABLE), sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS6),
    };

    for (Feature = 0; Feature < _countof(Sizes); Feature++)
    {
        /* The two format queries take a format in, so they are covered below */
        if (Feature == D3D11_FEATURE_FORMAT_SUPPORT || Feature == D3D11_FEATURE_FORMAT_SUPPORT2)
            continue;

        memset(Data, 0xCD, sizeof(Data));
        AcReportFeature(Check, (D3D11_FEATURE)Feature, Data, Sizes[Feature]);

        sprintf_s(Key, "feature%u.badsize", Feature);
        Check.Report(Key, "0x%08lX", Check.Device->CheckFeatureSupport((D3D11_FEATURE)Feature, Data, Sizes[Feature] + 4));
    }

    sprintf_s(Key, "feature.unknown");
    Check.Report(Key, "0x%08lX", Check.Device->CheckFeatureSupport((D3D11_FEATURE)200, Data, 4));

    for (Format = 0; Format <= DXGI_FORMAT_B4G4R4A4_UNORM; Format++)
    {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT Support = { (DXGI_FORMAT)Format, 0xCDCDCDCD };
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 Support2 = { (DXGI_FORMAT)Format, 0xCDCDCDCD };
        HRESULT hr;
        HRESULT hr2;

        hr = Check.Device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support));
        hr2 = Check.Device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &Support2, sizeof(Support2));
        sprintf_s(Key, "featureformat%u", Format);
        Check.Report(Key, "0x%08lX %08X 0x%08lX %08X", hr, Support.OutFormatSupport, hr2, Support2.OutFormatSupport2);
    }
}

static const CheckInfo FormatChecks[] =
{
    { "formats", AcCheckFormats },
    { "features", AcCheckFeatures },
};

const CheckInfo*
AcFormatChecks(
    _Out_ UINT* Count)
{
    *Count = _countof(FormatChecks);
    return FormatChecks;
}
