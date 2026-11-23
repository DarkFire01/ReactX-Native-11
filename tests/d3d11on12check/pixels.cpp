/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pixel exact results of mip generation, resolves, tiled resources and video processing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * Both layers run on the same D3D12 driver, so the same D3D12 work gives the same bytes. The checks
 * print hashes of whole subresources and a few texels.
 */

#include "check.hpp"

#include <d3dcompiler.h>

#include <algorithm>

static
UINT
PxHash(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size,
    _In_ UINT Hash = 2166136261u)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    for (SIZE_T Index = 0; Index < Size; Index += 1)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619u;
    }

    return Hash;
}

static
HRESULT
PxOpen(
    _In_ OnCheck& Check,
    _Outptr_ ID3D11Device** Device,
    _Outptr_ ID3D11DeviceContext** Context)
{
    HRESULT hr;

    hr = Check.Make(D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, Device);
    Check.Report("open", "0x%08lX", hr);
    if (FAILED(hr))
        return hr;

    (*Device)->GetImmediateContext(Context);
    return S_OK;
}

/* Reads one subresource of a 2D texture back and reports a hash of its rows and its first texel */
static
VOID
PxReportSubresource(
    _In_ OnCheck& Check,
    _In_z_ PCSTR Key,
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Texture2D* Texture,
    _In_ UINT Mip,
    _In_ UINT RowBytesPerTexel,
    _In_ UINT Rows)
{
    ComPtr<ID3D11Texture2D> Staging;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    UINT Width;
    UINT Hash = 2166136261u;
    UINT First;
    HRESULT hr;

    Texture->GetDesc(&Desc);
    Width = (std::max)(Desc.Width >> Mip, 1u);
    Desc.Width = Width;
    Desc.Height = (std::max)(Desc.Height >> Mip, 1u);
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.SampleDesc.Count = 1;
    Desc.SampleDesc.Quality = 0;
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
    if (FAILED(hr))
    {
        Check.Report(Key, "staging 0x%08lX", hr);
        return;
    }

    Context->CopySubresourceRegion(Staging.Get(), 0, 0, 0, 0, Texture, Mip, nullptr);
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
    {
        Check.Report(Key, "map 0x%08lX", hr);
        return;
    }

    if (Rows == 0)
        Rows = Desc.Height;
    for (UINT Row = 0; Row < Rows; Row += 1)
        Hash = PxHash(static_cast<const BYTE*>(Mapped.pData) + Row * Mapped.RowPitch, Width * RowBytesPerTexel, Hash);
    First = *static_cast<const UINT*>(Mapped.pData);
    Context->Unmap(Staging.Get(), 0);

    Check.Report(Key, "%ux%u hash 0x%08X first 0x%08X", Width, Desc.Height, Hash, First);
}

/* A texture with a texel pattern that differs in every channel */
static
HRESULT
PxMakePattern(
    _In_ ID3D11Device* Device,
    _In_ const D3D11_TEXTURE2D_DESC& Desc,
    _In_ UINT BytesPerTexel,
    _Outptr_ ID3D11Texture2D** Texture)
{
    std::vector<BYTE> Texels(Desc.Width * Desc.Height * BytesPerTexel);
    std::vector<D3D11_SUBRESOURCE_DATA> Data(Desc.MipLevels);

    for (SIZE_T Index = 0; Index < Texels.size(); Index += 1)
        Texels[Index] = static_cast<BYTE>((Index * 37 + (Index / BytesPerTexel) * 11) & 0xFF);

    for (D3D11_SUBRESOURCE_DATA& Each : Data)
    {
        Each.pSysMem = Texels.data();
        Each.SysMemPitch = Desc.Width * BytesPerTexel;
    }

    return Device->CreateTexture2D(&Desc, Data.data(), Texture);
}

/* genmips: GenerateMips on a few formats and sizes */
VOID
OcCheckGenMips(
    _In_ OnCheck& Check)
{
    static const struct
    {
        DXGI_FORMAT Format;
        UINT Bytes;
        UINT Width;
        UINT Height;
    } Cases[] =
    {
        { DXGI_FORMAT_R8G8B8A8_UNORM, 4, 16, 16 },
        { DXGI_FORMAT_B8G8R8A8_UNORM, 4, 13, 7 },
        { DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, 16, 8 },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, 8, 8, 8 },
    };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    char Key[64];

    if (FAILED(PxOpen(Check, &Device, &Context)))
        return;

    for (UINT Case = 0; Case < _countof(Cases); Case += 1)
    {
        ComPtr<ID3D11Texture2D> Texture;
        ComPtr<ID3D11ShaderResourceView> View;
        D3D11_TEXTURE2D_DESC Desc = {};
        HRESULT hr;

        Desc.Width = Cases[Case].Width;
        Desc.Height = Cases[Case].Height;
        Desc.MipLevels = 0;
        Desc.ArraySize = 1;
        Desc.Format = Cases[Case].Format;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        Desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        hr = Device->CreateTexture2D(&Desc, nullptr, &Texture);
        sprintf_s(Key, "case%u.create", Case);
        Check.Report(Key, "0x%08lX", hr);
        if (FAILED(hr))
            continue;

        Texture->GetDesc(&Desc);
        {
            ComPtr<ID3D11Texture2D> Source;
            D3D11_TEXTURE2D_DESC SourceDesc = Desc;

            SourceDesc.MipLevels = 1;
            SourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            SourceDesc.MiscFlags = 0;
            PxMakePattern(Device.Get(), SourceDesc, Cases[Case].Bytes, &Source);
            Context->CopySubresourceRegion(Texture.Get(), 0, 0, 0, 0, Source.Get(), 0, nullptr);
        }

        Device->CreateShaderResourceView(Texture.Get(), nullptr, &View);
        Context->GenerateMips(View.Get());

        for (UINT Mip = 0; Mip < Desc.MipLevels; Mip += 1)
        {
            sprintf_s(Key, "case%u.mip%u", Case, Mip);
            PxReportSubresource(Check, Key, Device.Get(), Context.Get(), Texture.Get(), Mip, Cases[Case].Bytes, 0);
        }
    }
}

static const char PxDrawSource[] =
    "float4 vs(uint Id : SV_VertexID) : SV_Position\n"
    "{ float2 P[3] = { float2(-0.9, -0.7), float2(-0.2, 0.95), float2(0.85, -0.3) };\n"
    "  return float4(P[Id], 0, 1); }\n"
    "float4 ps(float4 Pos : SV_Position) : SV_Target\n"
    "{ return float4(frac(Pos.x / 7), frac(Pos.y / 5), 0.25, 1); }\n";

static
HRESULT
PxCompile(
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _Outptr_ ID3DBlob** Blob)
{
    ComPtr<ID3DBlob> Errors;

    return D3DCompile(PxDrawSource, sizeof(PxDrawSource) - 1, "pixels", nullptr, nullptr, Entry, Profile,
                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, Blob, &Errors);
}

/* resolve: ResolveSubresource of an edge heavy multisampled draw, for each sample count the device has */
VOID
OcCheckResolve(
    _In_ OnCheck& Check)
{
    static const DXGI_FORMAT Formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT };
    static const float Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3DBlob> VsCode;
    ComPtr<ID3DBlob> PsCode;
    ComPtr<ID3D11VertexShader> Vs;
    ComPtr<ID3D11PixelShader> Ps;
    D3D11_VIEWPORT Viewport = { 0.0f, 0.0f, 32.0f, 24.0f, 0.0f, 1.0f };
    char Key[64];

    if (FAILED(PxOpen(Check, &Device, &Context)))
        return;

    PxCompile("vs", "vs_5_0", &VsCode);
    PxCompile("ps", "ps_5_0", &PsCode);
    Device->CreateVertexShader(VsCode->GetBufferPointer(), VsCode->GetBufferSize(), nullptr, &Vs);
    Device->CreatePixelShader(PsCode->GetBufferPointer(), PsCode->GetBufferSize(), nullptr, &Ps);

    for (UINT FormatIndex = 0; FormatIndex < _countof(Formats); FormatIndex += 1)
    {
        for (UINT Samples = 2; Samples <= 8; Samples *= 2)
        {
            ComPtr<ID3D11Texture2D> Multi;
            ComPtr<ID3D11Texture2D> Single;
            ComPtr<ID3D11RenderTargetView> View;
            D3D11_TEXTURE2D_DESC Desc = {};
            UINT Quality = 0;
            HRESULT hr;

            sprintf_s(Key, "fmt%u.ms%u", Formats[FormatIndex], Samples);
            Device->CheckMultisampleQualityLevels(Formats[FormatIndex], Samples, &Quality);
            if (Quality == 0)
            {
                Check.Report(Key, "unsupported");
                continue;
            }

            Desc.Width = 32;
            Desc.Height = 24;
            Desc.MipLevels = 1;
            Desc.ArraySize = 1;
            Desc.Format = Formats[FormatIndex];
            Desc.SampleDesc.Count = Samples;
            Desc.Usage = D3D11_USAGE_DEFAULT;
            Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            hr = Device->CreateTexture2D(&Desc, nullptr, &Multi);
            Desc.SampleDesc.Count = 1;
            Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (SUCCEEDED(hr))
                hr = Device->CreateTexture2D(&Desc, nullptr, &Single);
            if (SUCCEEDED(hr))
                hr = Device->CreateRenderTargetView(Multi.Get(), nullptr, &View);
            if (FAILED(hr))
            {
                Check.Report(Key, "create 0x%08lX", hr);
                continue;
            }

            Context->ClearRenderTargetView(View.Get(), Clear);
            Context->OMSetRenderTargets(1, View.GetAddressOf(), nullptr);
            Context->RSSetViewports(1, &Viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->IASetInputLayout(nullptr);
            Context->VSSetShader(Vs.Get(), nullptr, 0);
            Context->PSSetShader(Ps.Get(), nullptr, 0);
            Context->Draw(3, 0);
            Context->OMSetRenderTargets(0, nullptr, nullptr);
            Context->ResolveSubresource(Single.Get(), 0, Multi.Get(), 0, Formats[FormatIndex]);

            PxReportSubresource(Check, Key, Device.Get(), Context.Get(), Single.Get(), 0,
                                Formats[FormatIndex] == DXGI_FORMAT_R8G8B8A8_UNORM ? 4 : 8, 0);
        }
    }
}

/* tiled: tile mappings, tile updates and tile copies of a tiled texture and buffer */
VOID
OcCheckTiled(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Device2> Device2;
    ComPtr<ID3D11DeviceContext2> Context2;
    ComPtr<ID3D11Buffer> Pool;
    ComPtr<ID3D11Texture2D> Tiled;
    ComPtr<ID3D11Buffer> TiledBuffer;
    ComPtr<ID3D11Buffer> Readback;
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 Options = {};
    D3D11_BUFFER_DESC BufferDesc = {};
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_PACKED_MIP_DESC Packed = {};
    D3D11_TILE_SHAPE Shape = {};
    D3D11_SUBRESOURCE_TILING Tilings[8] = {};
    D3D11_TILED_RESOURCE_COORDINATE Coordinate = {};
    D3D11_TILE_REGION_SIZE Region = {};
    D3D11_MAPPED_SUBRESOURCE Mapped;
    std::vector<BYTE> Data(65536 * 2);
    UINT TileCount = 0;
    UINT TilingCount = _countof(Tilings);
    UINT RangeFlags = 0;
    UINT PoolStart = 1;
    UINT RangeTiles = 2;
    HRESULT hr;

    if (FAILED(PxOpen(Check, &Device, &Context)))
        return;

    Device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &Options, sizeof(Options));
    Check.Report("tier", "%d", Options.TiledResourcesTier);
    if (Options.TiledResourcesTier == D3D11_TILED_RESOURCES_NOT_SUPPORTED)
        return;

    Device.As(&Device2);
    Context.As(&Context2);

    BufferDesc.ByteWidth = 65536 * 4;
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_TILE_POOL;
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Pool);
    Check.Report("pool", "0x%08lX", hr);

    Desc.Width = 256;
    Desc.Height = 256;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    Desc.MiscFlags = D3D11_RESOURCE_MISC_TILED;
    hr = Device->CreateTexture2D(&Desc, nullptr, &Tiled);
    Check.Report("texture", "0x%08lX", hr);

    BufferDesc.ByteWidth = 65536 * 2;
    BufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_TILED | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &TiledBuffer);
    Check.Report("buffer", "0x%08lX", hr);
    if (Pool == nullptr || Tiled == nullptr || TiledBuffer == nullptr)
        return;

    Device2->GetResourceTiling(Tiled.Get(), &TileCount, &Packed, &Shape, &TilingCount, 0, Tilings);
    Check.Report("tiling", "tiles %u packed %u/%u shape %ux%ux%u sub %u: %ux%ux%u start %u", TileCount,
                 Packed.NumStandardMips, Packed.NumPackedMips, Shape.WidthInTexels, Shape.HeightInTexels,
                 Shape.DepthInTexels, TilingCount, Tilings[0].WidthInTiles, Tilings[0].HeightInTiles,
                 Tilings[0].DepthInTiles, Tilings[0].StartTileIndexInOverallResource);

    /* Two tiles of the buffer map to pool tiles 1 and 2 */
    Region.NumTiles = 2;
    hr = Context2->UpdateTileMappings(TiledBuffer.Get(), 1, &Coordinate, &Region, Pool.Get(), 1, &RangeFlags,
                                      &PoolStart, &RangeTiles, 0);
    Check.Report("map_buffer", "0x%08lX", hr);

    /* The texture's first tile has a pool tile of its own */
    Region.NumTiles = 1;
    RangeTiles = 1;
    PoolStart = 3;
    hr = Context2->UpdateTileMappings(Tiled.Get(), 1, &Coordinate, &Region, Pool.Get(), 1, &RangeFlags,
                                      &PoolStart, &RangeTiles, 0);
    Check.Report("map_texture", "0x%08lX", hr);

    for (SIZE_T Index = 0; Index < Data.size(); Index += 1)
        Data[Index] = static_cast<BYTE>((Index * 13 + Index / 251) & 0xFF);

    Region.NumTiles = 2;
    Context2->UpdateTiles(TiledBuffer.Get(), &Coordinate, &Region, Data.data(), 0);
    Region.NumTiles = 1;
    Context2->UpdateTiles(Tiled.Get(), &Coordinate, &Region, Data.data() + 65536, 0);

    BufferDesc.ByteWidth = 65536 * 3;
    BufferDesc.Usage = D3D11_USAGE_STAGING;
    BufferDesc.BindFlags = 0;
    BufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    BufferDesc.MiscFlags = 0;
    Device->CreateBuffer(&BufferDesc, nullptr, &Readback);

    /* Reads the texture's tile back as linear tile data, then the buffer's tiles after it */
    Region.NumTiles = 1;
    Context2->CopyTiles(Tiled.Get(), &Coordinate, &Region, Readback.Get(), 0,
                        D3D11_TILE_COPY_SWIZZLED_TILED_RESOURCE_TO_LINEAR_BUFFER);
    Context->CopySubresourceRegion(Readback.Get(), 0, 65536, 0, 0, TiledBuffer.Get(), 0, nullptr);
    hr = Context->Map(Readback.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
    {
        Check.Report("readback", "map 0x%08lX", hr);
        return;
    }

    Check.Report("texture_tile", "hash 0x%08X match %d", PxHash(Mapped.pData, 65536),
                 memcmp(Mapped.pData, Data.data() + 65536, 65536) == 0);
    Check.Report("buffer_tiles", "hash 0x%08X match %d", PxHash(static_cast<BYTE*>(Mapped.pData) + 65536, 65536 * 2),
                 memcmp(static_cast<BYTE*>(Mapped.pData) + 65536, Data.data(), 65536 * 2) == 0);
    Context->Unmap(Readback.Get(), 0);
}

/* A video processor input texture filled with a pattern */
static
HRESULT
PxMakeVideoInput(
    _In_ ID3D11Device* Device,
    _In_ DXGI_FORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _Outptr_ ID3D11Texture2D** Texture)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Data = {};
    std::vector<BYTE> Texels(Width * Height * 4);

    for (SIZE_T Index = 0; Index < Texels.size(); Index += 1)
        Texels[Index] = static_cast<BYTE>((Index * 7 + (Index / (Width * 4)) * 29) & 0xFF);

    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    Data.pSysMem = Texels.data();
    Data.SysMemPitch = Format == DXGI_FORMAT_NV12 ? Width : Width * 4;

    return Device->CreateTexture2D(&Desc, &Data, Texture);
}

/* video: video processor caps, formats, HDR metadata and the pixels of a few blts */
VOID
OcCheckVideo(
    _In_ OnCheck& Check)
{
    static const DXGI_FORMAT Formats[] =
    {
        DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_NV12, DXGI_FORMAT_P010,
        DXGI_FORMAT_YUY2, DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
    };
    static const DXGI_FORMAT Inputs[] = { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_NV12 };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11VideoDevice> Video;
    ComPtr<ID3D11VideoContext> VideoContext;
    ComPtr<ID3D11VideoContext2> VideoContext2;
    ComPtr<ID3D11VideoProcessorEnumerator> Enumerator;
    ComPtr<ID3D11VideoProcessor> Processor;
    ComPtr<ID3D11Texture2D> Output;
    ComPtr<ID3D11VideoProcessorOutputView> OutputView;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Content = {};
    D3D11_VIDEO_PROCESSOR_CAPS Caps = {};
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC OutputViewDesc = {};
    D3D11_TEXTURE2D_DESC Desc = {};
    DXGI_HDR_METADATA_HDR10 Hdr10 = { { 34000, 16000 }, { 13250, 34500 }, { 7500, 3000 }, { 15635, 16450 },
                                      10000000, 50, 1000, 400 };
    DXGI_HDR_METADATA_HDR10 HdrBack = {};
    DXGI_HDR_METADATA_TYPE HdrType = DXGI_HDR_METADATA_TYPE_NONE;
    char Key[64];
    HRESULT hr;

    if (FAILED(PxOpen(Check, &Device, &Context)))
        return;

    Device.As(&Video);
    Context.As(&VideoContext);
    Context.As(&VideoContext2);
    if (Video == nullptr || VideoContext == nullptr)
    {
        Check.Report("video", "none");
        return;
    }

    Content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    Content.InputWidth = 64;
    Content.InputHeight = 48;
    Content.OutputWidth = 96;
    Content.OutputHeight = 64;
    Content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    hr = Video->CreateVideoProcessorEnumerator(&Content, &Enumerator);
    Check.Report("enumerator", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    hr = Enumerator->GetVideoProcessorCaps(&Caps);
    Check.Report("caps", "0x%08lX device 0x%X feature 0x%X filter 0x%X input 0x%X auto 0x%X stereo 0x%X rates %u "
                 "streams %u states %u", hr, Caps.DeviceCaps, Caps.FeatureCaps, Caps.FilterCaps,
                 Caps.InputFormatCaps, Caps.AutoStreamCaps, Caps.StereoCaps, Caps.RateConversionCapsCount,
                 Caps.MaxInputStreams, Caps.MaxStreamStates);

    for (DXGI_FORMAT Format : Formats)
    {
        UINT Support = 0;

        hr = Enumerator->CheckVideoProcessorFormat(Format, &Support);
        sprintf_s(Key, "format_%u", Format);
        Check.Report(Key, "0x%08lX 0x%X", hr, Support);
    }

    hr = Video->CreateVideoProcessor(Enumerator.Get(), 0, &Processor);
    Check.Report("processor", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    Desc.Width = 96;
    Desc.Height = 64;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    Device->CreateTexture2D(&Desc, nullptr, &Output);
    OutputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    hr = Video->CreateVideoProcessorOutputView(Output.Get(), Enumerator.Get(), &OutputViewDesc, &OutputView);
    Check.Report("output_view", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    for (DXGI_FORMAT Format : Inputs)
    {
        ComPtr<ID3D11Texture2D> Input;
        ComPtr<ID3D11VideoProcessorInputView> InputView;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC InputViewDesc = {};
        D3D11_VIDEO_PROCESSOR_STREAM Stream = {};
        RECT Source = { 4, 2, 60, 46 };
        RECT Target = { 8, 4, 88, 60 };
        D3D11_VIDEO_COLOR Background = {};

        hr = PxMakeVideoInput(Device.Get(), Format, 64, 48, &Input);
        InputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        if (SUCCEEDED(hr))
            hr = Video->CreateVideoProcessorInputView(Input.Get(), Enumerator.Get(), &InputViewDesc, &InputView);
        sprintf_s(Key, "input_%u", Format);
        if (FAILED(hr))
        {
            Check.Report(Key, "0x%08lX", hr);
            continue;
        }

        Background.RGBA.R = 0.25f;
        Background.RGBA.G = 0.5f;
        Background.RGBA.B = 0.75f;
        Background.RGBA.A = 1.0f;
        VideoContext->VideoProcessorSetOutputBackgroundColor(Processor.Get(), FALSE, &Background);
        VideoContext->VideoProcessorSetStreamSourceRect(Processor.Get(), 0, TRUE, &Source);
        VideoContext->VideoProcessorSetStreamDestRect(Processor.Get(), 0, TRUE, &Target);
        VideoContext->VideoProcessorSetStreamFrameFormat(Processor.Get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);

        Stream.Enable = TRUE;
        Stream.pInputSurface = InputView.Get();
        hr = VideoContext->VideoProcessorBlt(Processor.Get(), OutputView.Get(), 0, 1, &Stream);
        Check.Report(Key, "blt 0x%08lX", hr);

        sprintf_s(Key, "output_%u", Format);
        PxReportSubresource(Check, Key, Device.Get(), Context.Get(), Output.Get(), 0, 4, 0);
    }

    if (VideoContext2 != nullptr)
    {
        VideoContext2->VideoProcessorSetOutputHDRMetaData(Processor.Get(), DXGI_HDR_METADATA_TYPE_HDR10,
                                                          sizeof(Hdr10), &Hdr10);
        VideoContext2->VideoProcessorGetOutputHDRMetaData(Processor.Get(), &HdrType, 0, nullptr);
        VideoContext2->VideoProcessorGetOutputHDRMetaData(Processor.Get(), &HdrType, sizeof(HdrBack), &HdrBack);
        Check.Report("output_hdr", "type %d same %d", HdrType,
                     memcmp(&Hdr10, &HdrBack, sizeof(Hdr10)) == 0);

        VideoContext2->VideoProcessorSetStreamHDRMetaData(Processor.Get(), 0, DXGI_HDR_METADATA_TYPE_HDR10,
                                                          sizeof(Hdr10), &Hdr10);
        HdrType = DXGI_HDR_METADATA_TYPE_NONE;
        VideoContext2->VideoProcessorGetStreamHDRMetaData(Processor.Get(), 0, &HdrType, sizeof(HdrBack), &HdrBack);
        Check.Report("stream_hdr", "type %d", HdrType);
    }
}
