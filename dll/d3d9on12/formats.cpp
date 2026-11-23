/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 formats, their Direct3D 12 counterparts and the operations reported for them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d3d9on12.hpp"

/* Four character formats the runtime and applications use beyond the named ones */
static constexpr D3DDDIFORMAT NineFourCc(
    _In_ char A,
    _In_ char B,
    _In_ char C,
    _In_ char D)
{
    return static_cast<D3DDDIFORMAT>(static_cast<UINT>(static_cast<BYTE>(A)) |
                                     (static_cast<UINT>(static_cast<BYTE>(B)) << 8) |
                                     (static_cast<UINT>(static_cast<BYTE>(C)) << 16) |
                                     (static_cast<UINT>(static_cast<BYTE>(D)) << 24));
}

static constexpr D3DDDIFORMAT NineFormatNull = NineFourCc('N', 'U', 'L', 'L');
static constexpr D3DDDIFORMAT NineFormatResz = NineFourCc('R', 'E', 'S', 'Z');
static constexpr D3DDDIFORMAT NineFormatAtoc = NineFourCc('A', 'T', 'O', 'C');
static constexpr D3DDDIFORMAT NineFormatIntz = NineFourCc('I', 'N', 'T', 'Z');
static constexpr D3DDDIFORMAT NineFormatRawz = NineFourCc('R', 'A', 'W', 'Z');
static constexpr D3DDDIFORMAT NineFormatDf16 = NineFourCc('D', 'F', '1', '6');
static constexpr D3DDDIFORMAT NineFormatDf24 = NineFourCc('D', 'F', '2', '4');
static constexpr D3DDDIFORMAT NineFormatAti1 = NineFourCc('A', 'T', 'I', '1');
static constexpr D3DDDIFORMAT NineFormatAti2 = NineFourCc('A', 'T', 'I', '2');
static constexpr D3DDDIFORMAT NineFormatAyuv = NineFourCc('A', 'Y', 'U', 'V');
static constexpr D3DDDIFORMAT NineFormatYuy2 = NineFourCc('Y', 'U', 'Y', '2');
static constexpr D3DDDIFORMAT NineFormatNv11 = NineFourCc('N', 'V', '1', '1');
static constexpr D3DDDIFORMAT NineFormatNv12 = NineFourCc('N', 'V', '1', '2');
static constexpr D3DDDIFORMAT NineFormatYv12 = NineFourCc('Y', 'V', '1', '2');
static constexpr D3DDDIFORMAT NineFormatP010 = NineFourCc('P', '0', '1', '0');
static constexpr D3DDDIFORMAT NineFormatP016 = NineFourCc('P', '0', '1', '6');
static constexpr D3DDDIFORMAT NineFormatY210 = NineFourCc('Y', '2', '1', '0');
static constexpr D3DDDIFORMAT NineFormatY216 = NineFourCc('Y', '2', '1', '6');
static constexpr D3DDDIFORMAT NineFormatY410 = NineFourCc('Y', '4', '1', '0');
static constexpr D3DDDIFORMAT NineFormatY416 = NineFourCc('Y', '4', '1', '6');
static constexpr D3DDDIFORMAT NineFormatAi44 = NineFourCc('A', 'I', '4', '4');
static constexpr D3DDDIFORMAT NineFormatIa44 = NineFourCc('I', 'A', '4', '4');
static constexpr D3DDDIFORMAT NineFormatOpaque420 = NineFourCc('4', '2', '0', 'O');

/* The feature switch for alpha blending on float render targets; off unless configured on */
static constexpr UINT32 NineFloatBlendFeature = 61767681;

/* One reported format: its base operations and the multisample mask flips and blits share */
struct NineFormatEntry
{
    D3DDDIFORMAT Format;
    UINT Operations;
    USHORT MultisampleTypes;
    USHORT PrivateBitCount;
};

/* The multisample counts offered on formats that render */
static constexpr USHORT NineRenderSamples = 0x8B;

/* Formats the runtime converts to ARGB and groups with it */
static constexpr UINT NineArgbOps = FORMATOP_CONVERT_TO_ARGB | FORMATOP_MEMBEROFGROUP_ARGB;
static constexpr UINT NineSrgbOps = FORMATOP_SRGBREAD | FORMATOP_SRGBWRITE;
static constexpr UINT NineDisplayOps = FORMATOP_DISPLAYMODE | FORMATOP_3DACCELERATION;
static constexpr UINT NineVideoOps = FORMATOP_PIXELSIZE | FORMATOP_CONVERT_TO_ARGB | FORMATOP_OFFSCREENPLAIN;

/* Every format the adapter may report, in the order it reports them */
static const NineFormatEntry NineFormats[] =
{
    { D3DDDIFMT_X8R8G8B8, NineDisplayOps | NineArgbOps | NineSrgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_R5G6B5, NineDisplayOps | NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A8R8G8B8, NineArgbOps | NineSrgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A8B8G8R8, NineArgbOps | NineSrgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A1R5G5B5, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_X1R5G5B5, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A4R4G4B4, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_X4R4G4B4, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_A16B16G16R16, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_G16R16, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_A2B10G10R10, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A2R10G10B10, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_A16B16G16R16F, NineArgbOps, NineRenderSamples, 0 },
    { D3DDDIFMT_R16F, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_G16R16F, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_R32F, 0, 0, 0 },
    { D3DDDIFMT_G32R32F, 0, 0, 0 },
    { D3DDDIFMT_A32B32G32R32F, 0, 0, 0 },
    { D3DDDIFMT_V8U8, FORMATOP_BUMPMAP, NineRenderSamples, 0 },
    { D3DDDIFMT_Q8W8V8U8, FORMATOP_BUMPMAP, NineRenderSamples, 0 },
    { D3DDDIFMT_V16U16, FORMATOP_BUMPMAP, NineRenderSamples, 0 },
    { D3DDDIFMT_Q16W16V16U16, FORMATOP_BUMPMAP, NineRenderSamples, 0 },
    { D3DDDIFMT_L8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_L16, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_A8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_A8L8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_DXT1, FORMATOP_SRGBREAD, 0, 0 },
    { D3DDDIFMT_DXT2, FORMATOP_SRGBREAD, 0, 0 },
    { D3DDDIFMT_DXT3, FORMATOP_SRGBREAD, 0, 0 },
    { D3DDDIFMT_DXT4, FORMATOP_SRGBREAD, 0, 0 },
    { D3DDDIFMT_DXT5, FORMATOP_SRGBREAD, 0, 0 },
    { NineFormatAti1, FORMATOP_PIXELSIZE | FORMATOP_SRGBREAD, 0, 8 },
    { NineFormatAti2, FORMATOP_PIXELSIZE | FORMATOP_SRGBREAD, 0, 8 },
    { D3DDDIFMT_D24S8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_D24FS8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_D24X8, 0, NineRenderSamples, 0 },
    { D3DDDIFMT_D16, 0, NineRenderSamples, 0 },
    { NineFormatAyuv, NineVideoOps, 0, 32 },
    { NineFormatYuy2, NineVideoOps, 0, 16 },
    { NineFormatNv12, NineVideoOps, 0, 16 },
    { NineFormatYv12, NineVideoOps, 0, 16 },
    { NineFormatP010, NineVideoOps, 0, 32 },
    { NineFormatIntz, 0, NineRenderSamples, 0 },
    { NineFormatDf16, 0, NineRenderSamples, 0 },
    { NineFormatDf24, 0, NineRenderSamples, 0 },
    { NineFormatNull, FORMATOP_TEXTURE | FORMATOP_VOLUMETEXTURE | FORMATOP_CUBETEXTURE | FORMATOP_OFFSCREEN_RENDERTARGET |
                      FORMATOP_SAME_FORMAT_RENDERTARGET | FORMATOP_OFFSCREENPLAIN | FORMATOP_BUMPMAP | FORMATOP_DMAP |
                      FORMATOP_VERTEXTEXTURE, NineRenderSamples, 0 },
    { NineFormatResz, 0, 0, 0 },
    { NineFormatAtoc, FORMATOP_OFFSCREENPLAIN, 0, 0 },
};

DXGI_FORMAT
NineDxgiFormat(
    _In_ D3DDDIFORMAT Format)
{
    switch (static_cast<UINT>(Format))
    {
        case D3DDDIFMT_A8R8G8B8: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case D3DDDIFMT_X8R8G8B8: return DXGI_FORMAT_B8G8R8X8_UNORM;
        case D3DDDIFMT_R5G6B5: return DXGI_FORMAT_B5G6R5_UNORM;
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_A1R5G5B5: return DXGI_FORMAT_B5G5R5A1_UNORM;
        case D3DDDIFMT_A4R4G4B4:
        case D3DDDIFMT_X4R4G4B4: return DXGI_FORMAT_B4G4R4A4_UNORM;
        case D3DDDIFMT_A8: return DXGI_FORMAT_A8_UNORM;
        case D3DDDIFMT_A2B10G10R10:
        case D3DDDIFMT_A2R10G10B10: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case D3DDDIFMT_A8B8G8R8: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case D3DDDIFMT_G16R16: return DXGI_FORMAT_R16G16_UNORM;
        case D3DDDIFMT_A16B16G16R16: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case D3DDDIFMT_A8P8: return DXGI_FORMAT_A8P8;
        case D3DDDIFMT_P8: return DXGI_FORMAT_P8;
        case D3DDDIFMT_L8: return DXGI_FORMAT_R8_UNORM;
        case D3DDDIFMT_A8L8: return DXGI_FORMAT_R8G8_UNORM;
        case D3DDDIFMT_V8U8: return DXGI_FORMAT_R8G8_SNORM;
        case D3DDDIFMT_Q8W8V8U8: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case D3DDDIFMT_V16U16: return DXGI_FORMAT_R16G16_SNORM;
        case D3DDDIFMT_D16_LOCKABLE:
        case D3DDDIFMT_D16: return DXGI_FORMAT_D16_UNORM;
        case D3DDDIFMT_S8D24:
        case D3DDDIFMT_D24S8:
        case D3DDDIFMT_X8D24:
        case D3DDDIFMT_D24X8:
        case D3DDDIFMT_D24FS8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case D3DDDIFMT_L16: return DXGI_FORMAT_R16_UNORM;
        case D3DDDIFMT_D32F_LOCKABLE: return DXGI_FORMAT_D32_FLOAT;
        case D3DDDIFMT_Q16W16V16U16: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case D3DDDIFMT_R16F: return DXGI_FORMAT_R16_FLOAT;
        case D3DDDIFMT_G16R16F: return DXGI_FORMAT_R16G16_FLOAT;
        case D3DDDIFMT_A16B16G16R16F: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case D3DDDIFMT_R32F: return DXGI_FORMAT_R32_FLOAT;
        case D3DDDIFMT_G32R32F: return DXGI_FORMAT_R32G32_FLOAT;
        case D3DDDIFMT_A32B32G32R32F: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case D3DDDIFMT_DXT1: return DXGI_FORMAT_BC1_UNORM;
        case D3DDDIFMT_DXT2:
        case D3DDDIFMT_DXT3: return DXGI_FORMAT_BC2_UNORM;
        case D3DDDIFMT_DXT4:
        case D3DDDIFMT_DXT5: return DXGI_FORMAT_BC3_UNORM;
        default: break;
    }

    if (Format == NineFormatIntz || Format == NineFormatDf24)
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    if (Format == NineFormatDf16 || Format == NineFormatRawz)
        return DXGI_FORMAT_D16_UNORM;
    if (Format == NineFormatAti1)
        return DXGI_FORMAT_BC4_UNORM;
    if (Format == NineFormatAti2)
        return DXGI_FORMAT_BC5_UNORM;
    if (Format == NineFormatAyuv)
        return DXGI_FORMAT_AYUV;
    if (Format == NineFormatYuy2)
        return DXGI_FORMAT_YUY2;
    if (Format == NineFormatNv11)
        return DXGI_FORMAT_NV11;
    if (Format == NineFormatNv12 || Format == NineFormatYv12)
        return DXGI_FORMAT_NV12;
    if (Format == NineFormatP010)
        return DXGI_FORMAT_P010;
    if (Format == NineFormatP016)
        return DXGI_FORMAT_P016;
    if (Format == NineFormatY210)
        return DXGI_FORMAT_Y210;
    if (Format == NineFormatY216)
        return DXGI_FORMAT_Y216;
    if (Format == NineFormatY410)
        return DXGI_FORMAT_Y410;
    if (Format == NineFormatY416)
        return DXGI_FORMAT_Y416;
    if (Format == NineFormatAi44)
        return DXGI_FORMAT_AI44;
    if (Format == NineFormatIa44)
        return DXGI_FORMAT_IA44;
    if (Format == NineFormatOpaque420)
        return DXGI_FORMAT_420_OPAQUE;
    return DXGI_FORMAT_UNKNOWN;
}

/* Video formats have no shader view of their own and stay out of the render target rules */
static
BOOLEAN
NineIsVideoFormat(
    _In_ DXGI_FORMAT Format)
{
    return Format >= DXGI_FORMAT_AYUV && Format <= DXGI_FORMAT_A8P8;
}

/* The color format a depth format is sampled through */
static
DXGI_FORMAT
NineDepthSampleFormat(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
    }
}

/* The texture kinds a format can be sampled as, from its Direct3D 12 support */
static
UINT
NineSampleOps(
    _In_ D3D12_FORMAT_SUPPORT1 Support)
{
    UINT Ops = 0;
    BOOLEAN Flat = (Support & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0;

    if (Flat)
        Ops = FORMATOP_TEXTURE | FORMATOP_DMAP | FORMATOP_VERTEXTEXTURE;
    if ((Support & D3D12_FORMAT_SUPPORT1_TEXTURE3D) != 0)
        Ops = Flat ? (Ops | FORMATOP_VOLUMETEXTURE) : FORMATOP_VOLUMETEXTURE;
    if ((Support & D3D12_FORMAT_SUPPORT1_TEXTURECUBE) != 0)
        Ops |= FORMATOP_CUBETEXTURE;
    return Ops;
}

/* Float formats that cannot blend while the float blend switch is off */
static
BOOLEAN
NineIsUnblendedFloat(
    _In_ D3DDDIFORMAT Format)
{
    return Format == D3DDDIFMT_G16R16F || Format == D3DDDIFMT_R32F || Format == D3DDDIFMT_G32R32F ||
           Format == D3DDDIFMT_A32B32G32R32F;
}

static
BOOLEAN
NineFloatBlendEnabled(VOID)
{
    static const BOOLEAN Enabled = []()
    {
        typedef FEATURE_ENABLED_STATE (WINAPI* PFN_NINE_FEATURE_STATE)(UINT32, FEATURE_CHANGE_TIME);
        HMODULE Module = LoadLibraryExW(L"api-ms-win-core-featurestaging-l1-1-0.dll", NULL,
                                        LOAD_LIBRARY_SEARCH_SYSTEM32);
        PFN_NINE_FEATURE_STATE Query;
        UINT State;

        if (Module == NULL)
            return static_cast<BOOLEAN>(FALSE);
        Query = reinterpret_cast<PFN_NINE_FEATURE_STATE>(GetProcAddress(Module, "GetFeatureEnabledState"));
        if (Query == nullptr)
            return static_cast<BOOLEAN>(FALSE);
        State = static_cast<UINT>(Query(NineFloatBlendFeature, FEATURE_CHANGE_TIME_MODULE_RELOAD));
        return static_cast<BOOLEAN>((State & ~0xC0u) == FEATURE_ENABLED_STATE_ENABLED);
    }();

    return Enabled;
}

/* The operations one entry gets from what Direct3D 12 supports; FALSE when its support is unknown */
static
BOOLEAN
NineEntryOps(
    _In_ const NineAdapter* Adapter,
    _In_ const NineFormatEntry* Entry,
    _In_ BOOLEAN FloatBlend,
    _Inout_ UINT* Ops,
    _Out_ HRESULT* Result)
{
    const UINT Rendered = FORMATOP_OFFSCREEN_RENDERTARGET | FORMATOP_SAME_FORMAT_RENDERTARGET | FORMATOP_OFFSCREENPLAIN;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT Support = {};
    DXGI_FORMAT SampleFormat;
    BOOLEAN Video;

    *Result = S_OK;
    Support.Format = NineDxgiFormat(Entry->Format);
    if (FAILED(Adapter->Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Support, sizeof(Support))))
        return FALSE;

    Video = NineIsVideoFormat(Support.Format);
    if (!Video && (Support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0 &&
        (Entry->Operations & FORMATOP_BUMPMAP) == 0)
    {
        *Ops |= Rendered;
    }

    if (FloatBlend && (Support.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) == 0)
        *Ops |= FORMATOP_NOALPHABLEND;
    if ((Support.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL) != 0)
        *Ops |= FORMATOP_ZSTENCIL | FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH;

    SampleFormat = NineDepthSampleFormat(Support.Format);
    if (SampleFormat != DXGI_FORMAT_UNKNOWN)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT Sampled = {};

        Sampled.Format = SampleFormat;
        *Result = Adapter->Device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &Sampled, sizeof(Sampled));
        if (FAILED(*Result))
            return FALSE;
        *Ops |= NineSampleOps(Sampled.Support1);
    }
    else if (!Video)
    {
        *Ops |= NineSampleOps(Support.Support1);
    }

    if ((*Ops & Rendered) == Rendered && (Support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RENDERTARGET) != 0 &&
        (Support.Support1 & D3D12_FORMAT_SUPPORT1_MULTISAMPLE_RESOLVE) != 0)
    {
        *Ops |= FORMATOP_TEXTURE | FORMATOP_VOLUMETEXTURE | FORMATOP_OFFSCREEN_RENDERTARGET |
                FORMATOP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH;
    }

    if ((Support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0 &&
        (Support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0 &&
        (Support.Support1 & D3D12_FORMAT_SUPPORT1_MIP) != 0 && Entry->Format != D3DDDIFMT_A2R10G10B10)
    {
        *Ops |= FORMATOP_AUTOGENMIPMAP;
    }
    return TRUE;
}

/* Counts the formats, or writes them; a writer stops once it has written the entry at Limit */
UINT
NineCollectFormatOps(
    _In_ const NineAdapter* Adapter,
    _Out_writes_opt_(Limit + 1) FORMATOP* Formats,
    _In_ UINT Limit,
    _Out_ HRESULT* Result)
{
    BOOLEAN FloatBlend = NineFloatBlendEnabled();
    UINT Count = 0;

    *Result = S_OK;
    for (const NineFormatEntry& Entry : NineFormats)
    {
        UINT Ops = Entry.Operations;
        BOOLEAN Placeholder = Entry.Format == NineFormatNull || Entry.Format == NineFormatResz ||
                              Entry.Format == NineFormatAtoc;

        if (!FloatBlend && NineIsUnblendedFloat(Entry.Format))
            Ops |= FORMATOP_NOALPHABLEND;

        if (!Placeholder && !NineEntryOps(Adapter, &Entry, FloatBlend, &Ops, Result))
        {
            if (FAILED(*Result))
            {
                NineTrace(L"the sampled form of depth format %u has no support (0x%08lX)", Entry.Format, *Result);
                return Count;
            }
            continue;
        }

        if (Formats != nullptr)
        {
            Formats[Count].Format = Entry.Format;
            Formats[Count].Operations = Ops;
            Formats[Count].FlipMsTypes = Entry.MultisampleTypes;
            Formats[Count].BltMsTypes = Entry.MultisampleTypes;
            Formats[Count].PrivateFormatBitCount = Entry.PrivateBitCount;
            if (Count == Limit)
                return Count;
        }
        Count += 1;
    }
    return Count;
}

BOOLEAN
NineIsIhvFormat(
    _In_ D3DDDIFORMAT Format)
{
    return Format == NineFormatAti1 || Format == NineFormatAti2 || Format == NineFormatDf24 ||
           Format == NineFormatDf16 || Format == NineFormatNull || Format == NineFormatIntz ||
           Format == NineFormatRawz;
}

/* Shader view swizzles, with 5 standing for a forced one and 4 for a forced zero */
static constexpr UINT NineSwizzleRgba = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 2, 3);
static constexpr UINT NineSwizzleRgb1 = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 2, 5);
static constexpr UINT NineSwizzleAlpha = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(4, 4, 4, 3);
static constexpr UINT NineSwizzleLuma = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 5);
static constexpr UINT NineSwizzleLumaAlpha = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 1);
static constexpr UINT NineSwizzleRed = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 5, 5, 5);
static constexpr UINT NineSwizzleRedGreen = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 5, 5);
static constexpr UINT NineSwizzleGreenRed = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(1, 0, 5, 5);
static constexpr UINT NineSwizzleChroma = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 4, 5);
static constexpr UINT NineSwizzleSplat = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0);

UINT
NineViewSwizzle(
    _In_ D3DDDIFORMAT Format,
    _In_ BOOLEAN AlphaIsOne)
{
    const UINT WithAlpha = AlphaIsOne ? NineSwizzleRgb1 : NineSwizzleRgba;

    switch (static_cast<UINT>(Format))
    {
        case D3DDDIFMT_R8G8B8:
        case D3DDDIFMT_X8R8G8B8:
        case D3DDDIFMT_R5G6B5:
        case D3DDDIFMT_X1R5G5B5:
        case D3DDDIFMT_R3G3B2:
        case D3DDDIFMT_X4R4G4B4:
        case D3DDDIFMT_X8B8G8R8:
        case D3DDDIFMT_L6V5U5:
        case D3DDDIFMT_X8L8V8U8:
            return NineSwizzleRgb1;
        case D3DDDIFMT_A8R8G8B8:
        case D3DDDIFMT_A1R5G5B5:
        case D3DDDIFMT_A4R4G4B4:
        case D3DDDIFMT_A8R3G3B2:
        case D3DDDIFMT_A2B10G10R10:
        case D3DDDIFMT_A8B8G8R8:
        case D3DDDIFMT_A16B16G16R16:
        case D3DDDIFMT_Q8W8V8U8:
        case D3DDDIFMT_A2W10V10U10:
        case D3DDDIFMT_Q16W16V16U16:
        case D3DDDIFMT_A16B16G16R16F:
        case D3DDDIFMT_A32B32G32R32F:
        case D3DDDIFMT_A2B10G10R10_XR_BIAS:
            return WithAlpha;
        case D3DDDIFMT_A8:
            return NineSwizzleAlpha;
        case D3DDDIFMT_G16R16:
        case D3DDDIFMT_V8U8:
        case D3DDDIFMT_V16U16:
        case D3DDDIFMT_G16R16F:
        case D3DDDIFMT_G32R32F:
        case D3DDDIFMT_CxV8U8:
            return NineSwizzleRedGreen;
        case D3DDDIFMT_A2R10G10B10:
            return D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(2, 1, 0, AlphaIsOne ? 5 : 3);
        case D3DDDIFMT_L8:
        case D3DDDIFMT_L16:
            return NineSwizzleLuma;
        case D3DDDIFMT_A8L8:
        case D3DDDIFMT_A4L4:
            return NineSwizzleLumaAlpha;
        case D3DDDIFMT_D32:
        case D3DDDIFMT_D24S8:
        case 76:
        case D3DDDIFMT_D24X8:
        case D3DDDIFMT_D16:
        case D3DDDIFMT_R16F:
        case D3DDDIFMT_R32F:
            return NineSwizzleRed;
        case D3DDDIFMT_A1:
            return NineSwizzleSplat;
        default:
            break;
    }

    if (Format == D3DDDIFMT_DXT1 || Format == D3DDDIFMT_DXT2 || Format == D3DDDIFMT_DXT3 ||
        Format == D3DDDIFMT_DXT4 || Format == D3DDDIFMT_DXT5 || Format == NineFourCc('M', 'E', 'T', '1'))
    {
        return WithAlpha;
    }
    if (Format == NineFormatDf24 || Format == NineFormatDf16 || Format == NineFormatIntz ||
        Format == NineFormatRawz || Format == NineFormatAti1)
    {
        return NineSwizzleRed;
    }
    if (Format == NineFormatAti2)
        return NineSwizzleGreenRed;
    if (Format == NineFourCc('G', 'R', 'G', 'B') || Format == NineFourCc('R', 'G', 'B', 'G') ||
        Format == NineFourCc('U', 'Y', 'V', 'Y'))
    {
        return NineSwizzleRgb1;
    }
    if (Format == NineFormatNv12 || Format == NineFormatNv11 || Format == NineFormatYuy2 ||
        Format == NineFormatP010 || Format == NineFormatP016 || Format == NineFormatY210 ||
        Format == NineFormatY216 || Format == NineFormatY410 || Format == NineFormatY416 ||
        Format == NineFormatIa44 || Format == NineFormatAi44)
    {
        return NineSwizzleChroma;
    }
    return ~0U;
}
