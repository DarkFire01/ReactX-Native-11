/*
 * PROJECT:     ReactX Basic tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Runs scenes on the system WARP and on ours and compares the results
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include <windows.h>
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>

/* What a scene produces: query results and pixels, compared byte for byte or within a tolerance */
struct TcResult
{
    std::vector<BYTE> Data;
    UINT Width = 0;
    UINT Height = 0;
    std::vector<BYTE> Pixels;
};

struct TcScene
{
    PCSTR Name;
    UINT Tolerance;
    HRESULT (*Run)(ID3D11Device *Device, ID3D11DeviceContext *Context, TcResult *Result);
};

static
VOID
TcAppend(
    _Inout_ TcResult *Result,
    _In_reads_bytes_(Size) const VOID *Data,
    _In_ SIZE_T Size)
{
    const BYTE *Bytes = static_cast<const BYTE *>(Data);

    Result->Data.insert(Result->Data.end(), Bytes, Bytes + Size);
}

static
HRESULT
TcReadBack(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Texture2D *Texture,
    _Inout_ TcResult *Result)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Texture2D *Staging = NULL;
    HRESULT hr;
    UINT Row;
    UINT Bytes;

    Texture->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    hr = Device->CreateTexture2D(&Desc, NULL, &Staging);
    if (FAILED(hr))
        return hr;

    Context->CopySubresourceRegion(Staging, 0, 0, 0, 0, Texture, 0, NULL);
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
    if (SUCCEEDED(hr))
    {
        Bytes = Desc.Width * 4;
        Result->Width = Desc.Width;
        Result->Height = Desc.Height;
        Result->Pixels.resize((SIZE_T)Bytes * Desc.Height);
        for (Row = 0; Row < Desc.Height; Row++)
            memcpy(&Result->Pixels[(SIZE_T)Row * Bytes], static_cast<BYTE *>(Map.pData) + (SIZE_T)Row * Map.RowPitch, Bytes);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return hr;
}

/* Feature level, feature support blocks, format support and multisample levels */
static
HRESULT
SceneCaps(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_FEATURE Features[] =
    {
        D3D11_FEATURE_THREADING, D3D11_FEATURE_DOUBLES, D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,
        D3D11_FEATURE_D3D11_OPTIONS, D3D11_FEATURE_ARCHITECTURE_INFO, D3D11_FEATURE_D3D9_OPTIONS,
        D3D11_FEATURE_SHADER_MIN_PRECISION_SUPPORT, D3D11_FEATURE_D3D9_SHADOW_SUPPORT,
        D3D11_FEATURE_D3D11_OPTIONS1, D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT,
        D3D11_FEATURE_MARKER_SUPPORT, D3D11_FEATURE_D3D9_OPTIONS1, D3D11_FEATURE_D3D11_OPTIONS2,
        D3D11_FEATURE_D3D11_OPTIONS3, D3D11_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, D3D11_FEATURE_D3D11_OPTIONS4,
        D3D11_FEATURE_SHADER_CACHE, D3D11_FEATURE_D3D11_OPTIONS5,
    };
    BYTE Block[256];
    D3D_FEATURE_LEVEL Level = Device->GetFeatureLevel();
    UINT Support;
    UINT Quality;
    UINT Format;
    UINT Count;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Context);
    TcAppend(Result, &Level, sizeof(Level));
    for (D3D11_FEATURE Feature : Features)
    {
        static const UINT Sizes[] = { 8, 4, 4, 56, 4, 4, 8, 4, 16, 4, 4, 12, 48, 4, 8, 4, 4, 4 };
        UINT Size = Sizes[&Feature - Features];

        ZeroMemory(Block, sizeof(Block));
        hr = Device->CheckFeatureSupport(Feature, Block, Size);
        TcAppend(Result, &hr, sizeof(hr));
        TcAppend(Result, Block, Size);
    }
    for (Format = 1; Format <= 132; Format++)
    {
        Support = 0;
        hr = Device->CheckFormatSupport(static_cast<DXGI_FORMAT>(Format), &Support);
        TcAppend(Result, &hr, sizeof(hr));
        TcAppend(Result, &Support, sizeof(Support));
        for (Count = 1; Count <= 32; Count *= 2)
        {
            Quality = 0;
            Device->CheckMultisampleQualityLevels(static_cast<DXGI_FORMAT>(Format), Count, &Quality);
            TcAppend(Result, &Quality, sizeof(Quality));
        }
    }
    return S_OK;
}

/* Clears of a few formats, read back through a staging copy */
static
HRESULT
SceneClear(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const DXGI_FORMAT Formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB };
    const FLOAT Color[4] = { 1.0f, 0.5f, 0.25f, 0.75f };
    D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_UNKNOWN, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    ID3D11Texture2D *Texture;
    ID3D11RenderTargetView *View;
    TcResult Image;
    D3D11_RECT Rect = { 8, 8, 40, 24 };
    HRESULT hr = S_OK;

    for (DXGI_FORMAT Format : Formats)
    {
        Desc.Format = Format;
        Texture = NULL;
        View = NULL;
        hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
        if (SUCCEEDED(hr))
            hr = Device->CreateRenderTargetView(Texture, NULL, &View);
        if (SUCCEEDED(hr))
        {
            const FLOAT Second[4] = { 0.0f, 0.2f, 0.4f, 1.0f };
            ID3D11DeviceContext1 *Context1 = NULL;

            Context->ClearRenderTargetView(View, Color);
            if (SUCCEEDED(Context->QueryInterface(IID_PPV_ARGS(&Context1))))
            {
                Context1->ClearView(View, Second, &Rect, 1);
                Context1->Release();
            }
            hr = TcReadBack(Device, Context, Texture, &Image);
            TcAppend(Result, Image.Pixels.data(), Image.Pixels.size());
            Result->Width = Image.Width;
            Result->Height = Image.Height;
            Result->Pixels = Image.Pixels;
        }
        if (View)
            View->Release();
        if (Texture)
            Texture->Release();
        if (FAILED(hr))
            break;
    }
    return hr;
}

/* Initial data, UpdateSubresource and region copies */
static
HRESULT
SceneCopy(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    D3D11_TEXTURE2D_DESC Desc = { 32, 32, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, 0, 0, 0 };
    std::vector<UINT> Source(32 * 32);
    D3D11_SUBRESOURCE_DATA Initial = { Source.data(), 32 * 4, 0 };
    ID3D11Texture2D *First = NULL;
    ID3D11Texture2D *Second = NULL;
    D3D11_BOX Box = { 4, 4, 0, 20, 12, 1 };
    UINT Patch[8 * 8];
    D3D11_BOX Target = { 10, 18, 0, 18, 26, 1 };
    HRESULT hr;
    UINT Index;

    for (Index = 0; Index < Source.size(); Index++)
        Source[Index] = 0xFF000000 | (Index * 2654435761u >> 8);
    for (Index = 0; Index < 64; Index++)
        Patch[Index] = 0xFF00FF00 | Index;

    hr = Device->CreateTexture2D(&Desc, &Initial, &First);
    if (SUCCEEDED(hr))
        hr = Device->CreateTexture2D(&Desc, NULL, &Second);
    if (SUCCEEDED(hr))
    {
        Context->CopyResource(Second, First);
        Context->CopySubresourceRegion(Second, 0, 12, 2, 0, First, 0, &Box);
        Context->UpdateSubresource(Second, 0, &Target, Patch, 8 * 4, 0);
        hr = TcReadBack(Device, Context, Second, Result);
        TcAppend(Result, Result->Pixels.data(), Result->Pixels.size());
    }
    if (Second)
        Second->Release();
    if (First)
        First->Release();
    return hr;
}

/* Shared shaders for the draw scenes */
static const char TcShaderSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "Texture2D Tex : register(t0); SamplerState Smp : register(s0);\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n"
    "float4 ps(VO i) : SV_Target { return i.c; }\n"
    "float4 pstex(VO i) : SV_Target { return Tex.Sample(Smp, i.t); }\n"
    "float4 psload(VO i) : SV_Target { return Tex.Load(int3(i.p.xy, 0)); }\n"
    "float4 psfilter(VO i) : SV_Target { return Tex.Sample(Smp, i.p.xy / 32.0 - 0.5); }\n"
    "float4 psmath(VO i) : SV_Target { float4 v = i.c * 3.0 - 1.0; float d = dot(v.xyz, v.xyz);\n"
    "    float4 r = float4(sqrt(abs(v.x)), rsqrt(d + 1.0), frac(v.z * 7.3), 0.5);\n"
    "    [loop] for (int k = 0; k < 3; k++) { if (r.x > 0.6) break; r.x += 0.1; }\n"
    "    if (i.t.x > 0.5) discard; return saturate(r); }\n";

struct TcVertex
{
    FLOAT Position[4];
    FLOAT Color[4];
    FLOAT Texcoord[2];
};

static
HRESULT
TcCompile(
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _Out_ ID3DBlob **Blob)
{
    ID3DBlob *Errors = NULL;
    HRESULT hr;

    hr = D3DCompile(TcShaderSource, sizeof(TcShaderSource) - 1, "warpcheck", NULL, NULL, Entry, Profile, 0, 0, Blob, &Errors);
    if (Errors)
    {
        if (FAILED(hr))
            printf("%s", (const char *)Errors->GetBufferPointer());
        Errors->Release();
    }
    return hr;
}

/* Reads a 2D texture of any plain format, BytesPerPixel wide */
static
HRESULT
TcReadBytes(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Texture2D *Texture,
    _In_ UINT BytesPerPixel,
    _Inout_ TcResult *Result)
{
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Texture2D *Staging = NULL;
    HRESULT hr;

    Texture->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    hr = Device->CreateTexture2D(&Desc, NULL, &Staging);
    if (FAILED(hr))
        return hr;
    Context->CopyResource(Staging, Texture);
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
    if (SUCCEEDED(hr))
    {
        for (UINT Row = 0; Row < Desc.Height; Row++)
            TcAppend(Result, static_cast<BYTE *>(Map.pData) + (SIZE_T)Row * Map.RowPitch, (SIZE_T)Desc.Width * BytesPerPixel);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return hr;
}

/* Options for one draw */
struct TcDrawSetup
{
    DXGI_FORMAT Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT BytesPerPixel = 4;
    UINT Size = 64;
    PCSTR PixelEntry = "ps";
    const TcVertex *Vertices = NULL;
    UINT VertexCount = 0;
    const UINT16 *Indices = NULL;
    UINT IndexCount = 0;
    D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    BOOL Depth = FALSE;
    BOOL Blend = FALSE;
    BOOL Texture = FALSE;
    D3D11_FILTER Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    D3D11_CULL_MODE Cull = D3D11_CULL_NONE;
    D3D11_VIEWPORT Viewport = { 0, 0, 0, 0, 0, 1 };
    ID3D11ShaderResourceView *View = NULL;
    ID3D11SamplerState *Sampler = NULL;
};

static
HRESULT
TcDrawScene(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ const TcDrawSetup &Setup,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    D3D11_TEXTURE2D_DESC Desc = { Setup.Size, Setup.Size, 1, 1, Setup.Format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_BUFFER_DESC BufferDesc = { Setup.VertexCount * (UINT)sizeof(TcVertex), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA Data = { Setup.Vertices, 0, 0 };
    D3D11_VIEWPORT Viewport = Setup.Viewport;
    ID3D11Texture2D *Target = NULL;
    ID3D11RenderTargetView *TargetView = NULL;
    ID3D11Texture2D *DepthTexture = NULL;
    ID3D11DepthStencilView *DepthView = NULL;
    ID3D11Buffer *VertexBuffer = NULL;
    ID3D11Buffer *IndexBuffer = NULL;
    ID3DBlob *VsBlob = NULL;
    ID3DBlob *PsBlob = NULL;
    ID3D11VertexShader *Vs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11RasterizerState *Raster = NULL;
    ID3D11BlendState *BlendState = NULL;
    ID3D11Texture2D *Image = NULL;
    ID3D11ShaderResourceView *ImageView = NULL;
    ID3D11SamplerState *Sampler = NULL;
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    HRESULT hr;

    hr = Device->CreateTexture2D(&Desc, NULL, &Target);
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target, NULL, &TargetView);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&BufferDesc, &Data, &VertexBuffer);
    if (SUCCEEDED(hr) && Setup.Indices != NULL)
    {
        D3D11_BUFFER_DESC IndexDesc = { Setup.IndexCount * 2u, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA IndexData = { Setup.Indices, 0, 0 };

        hr = Device->CreateBuffer(&IndexDesc, &IndexData, &IndexBuffer);
    }
    if (SUCCEEDED(hr))
        hr = TcCompile("vs", "vs_4_0", &VsBlob);
    if (SUCCEEDED(hr))
        hr = TcCompile(Setup.PixelEntry, "ps_4_0", &PsBlob);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, Setup.Cull, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (SUCCEEDED(hr) && Setup.Depth)
    {
        D3D11_TEXTURE2D_DESC DepthDesc = Desc;

        DepthDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = Device->CreateTexture2D(&DepthDesc, NULL, &DepthTexture);
        if (SUCCEEDED(hr))
            hr = Device->CreateDepthStencilView(DepthTexture, NULL, &DepthView);
    }
    if (SUCCEEDED(hr) && Setup.Blend)
    {
        D3D11_BLEND_DESC BlendDesc = {};

        BlendDesc.RenderTarget[0].BlendEnable = TRUE;
        BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = Device->CreateBlendState(&BlendDesc, &BlendState);
    }
    if (SUCCEEDED(hr) && Setup.View != NULL)
    {
        ImageView = Setup.View;
        ImageView->AddRef();
    }
    if (SUCCEEDED(hr) && Setup.Texture && Setup.View == NULL)
    {
        UINT Texels[16 * 16];
        D3D11_TEXTURE2D_DESC ImageDesc = { 16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA ImageData = { Texels, 16 * 4, 0 };
        D3D11_SAMPLER_DESC SamplerDesc = {};

        for (UINT Index = 0; Index < 256; Index++)
            Texels[Index] = 0xFF000000 | ((Index * 37) & 0xFF) | (((Index * 91) & 0xFF) << 8) | ((((Index % 16) ^ (Index / 16)) * 16) << 16);
        hr = Device->CreateTexture2D(&ImageDesc, &ImageData, &Image);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(Image, NULL, &ImageView);
        SamplerDesc.Filter = Setup.Filter;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        if (SUCCEEDED(hr))
            hr = Device->CreateSamplerState(&SamplerDesc, &Sampler);
    }

    if (SUCCEEDED(hr))
    {
        if (Viewport.Width == 0.0f)
        {
            Viewport.Width = (FLOAT)Setup.Size;
            Viewport.Height = (FLOAT)Setup.Size;
        }
        Context->ClearRenderTargetView(TargetView, Clear);
        if (DepthView)
            Context->ClearDepthStencilView(DepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
        Context->OMSetRenderTargets(1, &TargetView, DepthView);
        Context->OMSetBlendState(BlendState, NULL, 0xFFFFFFFF);
        Context->RSSetViewports(1, &Viewport);
        Context->RSSetState(Raster);
        Context->IASetInputLayout(Layout);
        Context->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
        Context->IASetPrimitiveTopology(Setup.Topology);
        Context->VSSetShader(Vs, NULL, 0);
        Context->PSSetShader(Ps, NULL, 0);
        if (ImageView)
        {
            Context->PSSetShaderResources(0, 1, &ImageView);
            if (Setup.Sampler != NULL)
                Context->PSSetSamplers(0, 1, &Setup.Sampler);
            else
                Context->PSSetSamplers(0, 1, &Sampler);
        }
        if (IndexBuffer)
        {
            Context->IASetIndexBuffer(IndexBuffer, DXGI_FORMAT_R16_UINT, 0);
            Context->DrawIndexed(Setup.IndexCount, 0, 0);
        }
        else
        {
            Context->Draw(Setup.VertexCount, 0);
        }
        hr = TcReadBytes(Device, Context, Target, Setup.BytesPerPixel, Result);

        /* Eight bit targets double as the picture */
        if (SUCCEEDED(hr) && Setup.BytesPerPixel == 4)
        {
            Result->Width = Setup.Size;
            Result->Height = Setup.Size;
            Result->Pixels.assign(Result->Data.end() - (SIZE_T)Setup.Size * Setup.Size * 4, Result->Data.end());
        }
    }

    ID3D11DeviceChild *Objects[] = { Target, TargetView, DepthTexture, DepthView, VertexBuffer, IndexBuffer, Vs, Ps, Layout,
                                     Raster, BlendState, Image, ImageView, Sampler };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    if (VsBlob)
        VsBlob->Release();
    if (PsBlob)
        PsBlob->Release();
    return hr;
}

/* A vertex given in normalized device coordinates, scaled by W so the divide brings it back */
static
TcVertex
TcMake(
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Z,
    _In_ FLOAT W,
    _In_ UINT Rgba,
    _In_ FLOAT U = 0.0f,
    _In_ FLOAT V = 0.0f)
{
    TcVertex Vertex = { { X * W, Y * W, Z * W, W },
                        { (Rgba >> 24) / 255.0f, ((Rgba >> 16) & 0xFF) / 255.0f, ((Rgba >> 8) & 0xFF) / 255.0f, (Rgba & 0xFF) / 255.0f },
                        { U, V } };

    return Vertex;
}

/* Flat colored triangles on awkward sub pixel positions: coverage and the fill rule */
static
HRESULT
SceneTriangles(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    std::vector<TcVertex> Vertices;
    TcDrawSetup Setup;
    UINT Seed = 12345;

    for (UINT Index = 0; Index < 40; Index++)
    {
        UINT Color;
        FLOAT Corner[6];

        for (FLOAT &Value : Corner)
        {
            Seed = Seed * 1103515245 + 12345;
            Value = ((Seed >> 8) % 2001) / 1000.0f - 1.0f;
        }
        Seed = Seed * 1103515245 + 12345;
        Color = (Seed & 0xFFFFFF00) | 0xFF;
        Vertices.push_back(TcMake(Corner[0], Corner[1], 0.5f, 1.0f, Color));
        Vertices.push_back(TcMake(Corner[2], Corner[3], 0.5f, 1.0f, Color));
        Vertices.push_back(TcMake(Corner[4], Corner[5], 0.5f, 1.0f, Color));
    }

    /* Two triangles sharing an edge on exact pixel boundaries */
    Vertices.push_back(TcMake(-0.5f, 0.5f, 0.5f, 1.0f, 0xFFFFFFFF));
    Vertices.push_back(TcMake(0.0f, 0.5f, 0.5f, 1.0f, 0xFFFFFFFF));
    Vertices.push_back(TcMake(-0.5f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFF));
    Vertices.push_back(TcMake(0.0f, 0.5f, 0.5f, 1.0f, 0xFF0000FF));
    Vertices.push_back(TcMake(0.0f, 0.0f, 0.5f, 1.0f, 0xFF0000FF));
    Vertices.push_back(TcMake(-0.5f, 0.0f, 0.5f, 1.0f, 0xFF0000FF));

    Setup.Vertices = Vertices.data();
    Setup.VertexCount = (UINT)Vertices.size();
    return TcDrawScene(Device, Context, Setup, Result);
}

static const TcVertex TcGradient[] =
{
    TcMake(-0.9f, -0.8f, 0.2f, 1.0f, 0xFF0000FF),
    TcMake(0.85f, -0.95f, 0.5f, 3.0f, 0x00FF00FF),
    TcMake(0.1f, 0.9f, 0.9f, 1.5f, 0x0000FF80),
};

/* Colors interpolated with perspective, into a float target so every bit shows */
static
HRESULT
SceneGradient(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    TcDrawSetup Setup;

    Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Setup.BytesPerPixel = 16;
    Setup.Size = 32;
    Setup.Vertices = TcGradient;
    Setup.VertexCount = ARRAYSIZE(TcGradient);
    return TcDrawScene(Device, Context, Setup, Result);
}

/* The same in eight bits: the float to unorm conversion of shader output */
static
HRESULT
SceneGradient8(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    TcDrawSetup Setup;

    Setup.Vertices = TcGradient;
    Setup.VertexCount = ARRAYSIZE(TcGradient);
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Overlapping triangles with a depth test and alpha blending */
static
HRESULT
SceneDepthBlend(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    const TcVertex Vertices[] =
    {
        TcMake(-0.9f, -0.9f, 0.1f, 1.0f, 0xFF000080), TcMake(0.9f, -0.9f, 0.9f, 1.0f, 0xFF000080), TcMake(0.0f, 0.9f, 0.5f, 1.0f, 0xFF000080),
        TcMake(-0.9f, 0.9f, 0.9f, 1.0f, 0x00FF00C0), TcMake(0.9f, 0.9f, 0.1f, 1.0f, 0x00FF00C0), TcMake(0.0f, -0.9f, 0.5f, 1.0f, 0x00FF00C0),
    };
    TcDrawSetup Setup;

    Setup.Vertices = Vertices;
    Setup.VertexCount = ARRAYSIZE(Vertices);
    Setup.Depth = TRUE;
    Setup.Blend = TRUE;
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Triangles crossing the near plane and the viewport edges, drawn with indices and back face culling */
static
HRESULT
SceneClip(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    const TcVertex Vertices[] =
    {
        TcMake(-1.6f, -0.5f, -0.4f, 1.0f, 0xFF8000FF),
        TcMake(1.4f, -1.3f, 0.6f, 2.0f, 0x2080FFFF),
        TcMake(0.2f, 1.7f, 1.3f, 1.0f, 0x80FF20FF),
        TcMake(0.5f, 0.5f, 0.5f, 1.0f, 0xFFFFFFFF),
    };
    static const UINT16 Indices[] = { 0, 2, 1, 0, 1, 3, 3, 1, 2 };
    TcDrawSetup Setup;
    D3D11_VIEWPORT Viewport = { 4.5f, 6.0f, 50.0f, 44.0f, 0.0f, 1.0f };

    Setup.Vertices = Vertices;
    Setup.VertexCount = ARRAYSIZE(Vertices);
    Setup.Indices = Indices;
    Setup.IndexCount = ARRAYSIZE(Indices);
    Setup.Cull = D3D11_CULL_BACK;
    Setup.Viewport = Viewport;
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Point then linear sampling with wrap and mirror addressing */
static
HRESULT
SceneTexture(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    const TcVertex Vertices[] =
    {
        TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0, -0.3f, -0.2f), TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0, 1.7f, -0.2f), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0, -0.3f, 1.9f),
        TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0, 1.7f, -0.2f), TcMake(1.0f, -1.0f, 0.5f, 2.0f, 0, 1.7f, 1.9f), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0, -0.3f, 1.9f),
    };
    TcDrawSetup Setup;
    HRESULT hr;

    Setup.Vertices = Vertices;
    Setup.VertexCount = ARRAYSIZE(Vertices);
    Setup.Texture = TRUE;
    Setup.PixelEntry = "pstex";
    hr = TcDrawScene(Device, Context, Setup, Result);
    if (SUCCEEDED(hr))
    {
        Setup.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        hr = TcDrawScene(Device, Context, Setup, Result);
    }
    if (SUCCEEDED(hr))
    {
        /* Linear again into a float target, so the filtered values show whole */
        Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        Setup.BytesPerPixel = 16;
        hr = TcDrawScene(Device, Context, Setup, Result);
    }
    return hr;
}

/* Arithmetic, a loop with a break, and discard in the pixel shader */
static
HRESULT
SceneMath(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    const TcVertex Vertices[] =
    {
        TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0x000000FF, 0.0f, 0.0f), TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0xFF00FFFF, 1.0f, 0.0f),
        TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0x00FF00FF, 0.0f, 1.0f), TcMake(1.0f, -1.0f, 0.5f, 1.0f, 0xFFFF80FF, 1.0f, 1.0f),
    };
    TcDrawSetup Setup;

    Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Setup.BytesPerPixel = 16;
    Setup.Size = 32;
    Setup.Vertices = Vertices;
    Setup.VertexCount = ARRAYSIZE(Vertices);
    Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    Setup.PixelEntry = "psmath";
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Aliased lines: random ones, then axis aligned and diagonal ones on exact pixel centers and edges */
static
HRESULT
SceneLines(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    std::vector<TcVertex> Vertices;
    TcDrawSetup Setup;
    UINT Seed = 777;

    for (UINT Index = 0; Index < 48; Index++)
    {
        FLOAT Corner[4];

        for (FLOAT &Value : Corner)
        {
            Seed = Seed * 1103515245 + 12345;
            Value = ((Seed >> 8) % 2201) / 1000.0f - 1.1f;
        }
        Vertices.push_back(TcMake(Corner[0], Corner[1], 0.5f, 1.0f, 0xFF4020FF));
        Vertices.push_back(TcMake(Corner[2], Corner[3], 0.5f, 1.0f, 0x20C0FFFF));
    }

    /* Pixel units: center is +0.5, so k / 32 - 1 lands on edges and (k + 0.5) / 32 - 1 on centers */
    for (UINT Index = 0; Index < 16; Index++)
    {
        FLOAT A = (FLOAT)(Index * 4) / 32.0f - 1.0f;
        FLOAT B = (FLOAT)(Index * 4 + 1) / 32.0f - 1.0f + ((Index & 1) ? 1.0f / 64.0f : 0.0f);
        FLOAT C = (FLOAT)(Index * 3 + 7) / 32.0f - 1.0f;

        Vertices.push_back(TcMake(A, B, 0.5f, 1.0f, 0xFFFFFFFF));
        Vertices.push_back(TcMake(C, B, 0.5f, 1.0f, 0xFFFF00FF));
        Vertices.push_back(TcMake(B, A, 0.5f, 1.0f, 0x00FFFFFF));
        Vertices.push_back(TcMake(B, C, 0.5f, 1.0f, 0xFF00FFFF));
        Vertices.push_back(TcMake(A, A, 0.5f, 1.0f, 0x80FF80FF));
        Vertices.push_back(TcMake(C, C + (FLOAT)(Index & 3) / 64.0f, 0.5f, 1.0f, 0x8080FFFF));
    }

    Setup.Vertices = Vertices.data();
    Setup.VertexCount = (UINT)Vertices.size();
    Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    return TcDrawScene(Device, Context, Setup, Result);
}

/* A line strip and a triangle list with adjacency, which drops the adjacent vertices */
static
HRESULT
SceneStrips(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    std::vector<TcVertex> Vertices;
    TcDrawSetup Setup;
    HRESULT hr;

    for (UINT Index = 0; Index < 24; Index++)
    {
        FLOAT Angle = (FLOAT)Index * 0.55f;

        Vertices.push_back(TcMake(cosf(Angle) * (0.2f + Index * 0.03f), sinf(Angle) * (0.25f + Index * 0.028f), 0.5f, 1.0f,
                                  0x10101000 | (Index * 0x0A0B0C00) | 0xFF));
    }
    Setup.Vertices = Vertices.data();
    Setup.VertexCount = (UINT)Vertices.size();
    Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    hr = TcDrawScene(Device, Context, Setup, Result);
    if (SUCCEEDED(hr))
    {
        Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
        hr = TcDrawScene(Device, Context, Setup, Result);
    }
    if (SUCCEEDED(hr))
    {
        Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
        hr = TcDrawScene(Device, Context, Setup, Result);
    }
    return hr;
}

/* Points on whole, half and quarter pixel positions */
static
HRESULT
ScenePoints(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    std::vector<TcVertex> Vertices;
    TcDrawSetup Setup;

    for (UINT Y = 0; Y < 12; Y++)
    {
        for (UINT X = 0; X < 12; X++)
        {
            FLOAT Px = ((FLOAT)X * 5.0f + (FLOAT)(Y % 4) * 0.25f + 2.0f) / 32.0f - 1.0f;
            FLOAT Py = ((FLOAT)Y * 5.0f + (FLOAT)(X % 4) * 0.25f + 2.0f) / 32.0f - 1.0f;

            Vertices.push_back(TcMake(Px, Py, 0.5f, 1.0f + (FLOAT)(X & 1), 0xFF000000 | (X * 20) << 16 | (Y * 20) << 8 | 0xFF));
        }
    }
    Setup.Vertices = Vertices.data();
    Setup.VertexCount = (UINT)Vertices.size();
    Setup.Topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Occlusion, pipeline statistics and predication around the depth scene */
static
HRESULT
SceneQueries(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    D3D11_QUERY_DESC Desc[4] = { { D3D11_QUERY_OCCLUSION, 0 }, { D3D11_QUERY_PIPELINE_STATISTICS, 0 },
                                 { D3D11_QUERY_OCCLUSION_PREDICATE, 0 }, { D3D11_QUERY_EVENT, 0 } };
    ID3D11Query *Queries[4] = {};
    ID3D11Predicate *Predicate = NULL;
    D3D11_QUERY_DATA_PIPELINE_STATISTICS Statistics = {};
    UINT64 Samples = 0;
    BOOL Visible = FALSE;
    BOOL Event = FALSE;
    TcResult Scratch;
    HRESULT hr = S_OK;

    for (UINT Index = 0; Index < 4 && SUCCEEDED(hr); Index++)
        hr = Device->CreateQuery(&Desc[Index], &Queries[Index]);
    if (FAILED(hr))
        goto Exit;
    Queries[2]->QueryInterface(IID_PPV_ARGS(&Predicate));

    Context->Begin(Queries[0]);
    Context->Begin(Queries[1]);
    Context->Begin(Queries[2]);
    hr = SceneDepthBlend(Device, Context, &Scratch);
    Context->End(Queries[2]);
    Context->End(Queries[1]);
    Context->End(Queries[0]);
    Context->End(Queries[3]);
    if (FAILED(hr))
        goto Exit;

    while (Context->GetData(Queries[0], &Samples, sizeof(Samples), 0) == S_FALSE)
        ;
    while (Context->GetData(Queries[1], &Statistics, sizeof(Statistics), 0) == S_FALSE)
        ;
    while (Context->GetData(Queries[2], &Visible, sizeof(Visible), 0) == S_FALSE)
        ;
    while (Context->GetData(Queries[3], &Event, sizeof(Event), 0) == S_FALSE)
        ;
    TcAppend(Result, &Samples, sizeof(Samples));
    TcAppend(Result, &Statistics, sizeof(Statistics));
    TcAppend(Result, &Visible, sizeof(Visible));
    TcAppend(Result, &Event, sizeof(Event));

    /* Something was drawn, so predicating on TRUE skips this draw and FALSE keeps it */
    Context->SetPredication(Predicate, TRUE);
    hr = SceneTriangles(Device, Context, Result);
    Context->SetPredication(Predicate, FALSE);
    if (SUCCEEDED(hr))
        hr = SceneTriangles(Device, Context, Result);
    Context->SetPredication(NULL, FALSE);

Exit:
    if (Predicate)
        Predicate->Release();
    for (ID3D11Query *Query : Queries)
    {
        if (Query)
            Query->Release();
    }
    return hr;
}

/* GenerateMips on even and odd sizes, eight bit, sRGB and half float */
static
HRESULT
SceneMips(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const struct
    {
        UINT Width;
        UINT Height;
        DXGI_FORMAT Format;
        UINT Bytes;
    } Cases[] =
    {
        { 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, 4 },
        { 21, 13, DXGI_FORMAT_R8G8B8A8_UNORM, 4 },
        { 16, 9, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4 },
        { 12, 7, DXGI_FORMAT_R16G16B16A16_FLOAT, 8 },
    };
    HRESULT hr = S_OK;

    for (const auto &Case : Cases)
    {
        D3D11_TEXTURE2D_DESC Desc = { Case.Width, Case.Height, 0, 1, Case.Format, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                      D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, D3D11_RESOURCE_MISC_GENERATE_MIPS };
        std::vector<BYTE> Texels((SIZE_T)Case.Width * Case.Height * Case.Bytes);
        ID3D11Texture2D *Texture = NULL;
        ID3D11Texture2D *Staging = NULL;
        ID3D11ShaderResourceView *View = NULL;
        UINT Seed = Case.Width * 31 + Case.Height;

        for (BYTE &Value : Texels)
        {
            Seed = Seed * 1103515245 + 12345;
            Value = (BYTE)(Seed >> 16);
        }
        /* Keep half floats finite: clear the top exponent bit */
        if (Case.Bytes == 8)
        {
            for (SIZE_T Index = 1; Index < Texels.size(); Index += 2)
                Texels[Index] &= 0x3F;
        }

        hr = Device->CreateTexture2D(&Desc, NULL, &Texture);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(Texture, NULL, &View);
        if (SUCCEEDED(hr))
        {
            D3D11_TEXTURE2D_DESC Read;

            Context->UpdateSubresource(Texture, 0, NULL, Texels.data(), Case.Width * Case.Bytes, 0);
            Context->GenerateMips(View);
            Texture->GetDesc(&Read);
            Read.Usage = D3D11_USAGE_STAGING;
            Read.BindFlags = 0;
            Read.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            Read.MiscFlags = 0;
            hr = Device->CreateTexture2D(&Read, NULL, &Staging);
            if (SUCCEEDED(hr))
            {
                Context->CopyResource(Staging, Texture);
                for (UINT Mip = 0; Mip < Read.MipLevels && SUCCEEDED(hr); Mip++)
                {
                    D3D11_MAPPED_SUBRESOURCE Map;
                    UINT Width = (Case.Width >> Mip) ? (Case.Width >> Mip) : 1;
                    UINT Height = (Case.Height >> Mip) ? (Case.Height >> Mip) : 1;

                    hr = Context->Map(Staging, Mip, D3D11_MAP_READ, 0, &Map);
                    if (FAILED(hr))
                        break;
                    for (UINT Row = 0; Row < Height; Row++)
                        TcAppend(Result, static_cast<BYTE *>(Map.pData) + (SIZE_T)Row * Map.RowPitch, (SIZE_T)Width * Case.Bytes);
                    Context->Unmap(Staging, Mip);
                }
            }
        }
        if (Staging)
            Staging->Release();
        if (View)
            View->Release();
        if (Texture)
            Texture->Release();
        if (FAILED(hr))
            break;
    }
    return hr;
}

static const char TcComputeSource[] =
    "RWStructuredBuffer<uint4> Out : register(u0);\n"
    "RWByteAddressBuffer Counts : register(u1);\n"
    "RWTexture2D<float4> Image : register(u2);\n"
    "AppendStructuredBuffer<uint2> Picked : register(u3);\n"
    "groupshared uint Sum[64];\n"
    "[numthreads(8, 8, 1)]\n"
    "void cs(uint3 id : SV_DispatchThreadID, uint3 gid : SV_GroupID, uint3 lid : SV_GroupThreadID, uint li : SV_GroupIndex)\n"
    "{\n"
    "    Sum[li] = id.x * 3 + id.y * 7 + gid.x;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    [loop] for (uint s = 32; s > 0; s >>= 1) { if (li < s) Sum[li] += Sum[li + s]; GroupMemoryBarrierWithGroupSync(); }\n"
    "    uint flat = id.y * 32 + id.x;\n"
    "    Out[flat] = uint4(id.x, id.y, lid.x + lid.y * 10, Sum[0]);\n"
    "    uint old;\n"
    "    Counts.InterlockedAdd((id.x & 3) * 4, 1, old);\n"
    "    Counts.InterlockedMax(16, flat * 13 % 97);\n"
    "    Image[id.xy] = float4(id.x / 32.0, id.y / 16.0, sqrt(li / 64.0), 1);\n"
    "    if ((flat % 7) == 0) Picked.Append(uint2(flat, 0));\n"
    "}\n";

/* Fills a buffer from the GPU and reads it back */
static
HRESULT
TcReadBuffer(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Buffer *Buffer,
    _Inout_ TcResult *Result)
{
    D3D11_BUFFER_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Map;
    ID3D11Buffer *Staging = NULL;
    HRESULT hr;

    Buffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Desc.StructureByteStride = 0;
    hr = Device->CreateBuffer(&Desc, NULL, &Staging);
    if (FAILED(hr))
        return hr;
    Context->CopyResource(Staging, Buffer);
    hr = Context->Map(Staging, 0, D3D11_MAP_READ, 0, &Map);
    if (SUCCEEDED(hr))
    {
        TcAppend(Result, Map.pData, Desc.ByteWidth);
        Context->Unmap(Staging, 0);
    }
    Staging->Release();
    return hr;
}

/* Thread ids, a shared memory reduction across barriers, atomics, a typed UAV and an append buffer */
static
HRESULT
SceneCompute(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    D3D11_BUFFER_DESC OutDesc = { 512 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16 };
    D3D11_BUFFER_DESC CountDesc = { 32, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
    D3D11_BUFFER_DESC PickDesc = { 128 * 8, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 8 };
    D3D11_BUFFER_DESC TallyDesc = { 16, D3D11_USAGE_DEFAULT, 0, 0, 0, 0 };
    D3D11_TEXTURE2D_DESC ImageDesc = { 32, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0 };
    D3D11_UNORDERED_ACCESS_VIEW_DESC RawDesc = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC AppendDesc = {};
    ID3D11Buffer *Out = NULL;
    ID3D11Buffer *Counts = NULL;
    ID3D11Buffer *Picked = NULL;
    ID3D11Buffer *Tally = NULL;
    ID3D11Texture2D *Image = NULL;
    ID3D11UnorderedAccessView *Views[4] = {};
    ID3D11ComputeShader *Shader = NULL;
    ID3DBlob *Blob = NULL;
    ID3DBlob *Errors = NULL;
    const UINT Initial[4] = { 0, 0, 0, 0 };
    HRESULT hr;

    hr = D3DCompile(TcComputeSource, sizeof(TcComputeSource) - 1, "compute", NULL, NULL, "cs", "cs_5_0", 0, 0, &Blob, &Errors);
    if (Errors)
    {
        if (FAILED(hr))
            printf("%s", (const char *)Errors->GetBufferPointer());
        Errors->Release();
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(Blob->GetBufferPointer(), Blob->GetBufferSize(), NULL, &Shader);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&CountDesc, NULL, &Counts);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&PickDesc, NULL, &Picked);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&TallyDesc, NULL, &Tally);
    if (SUCCEEDED(hr))
        hr = Device->CreateTexture2D(&ImageDesc, NULL, &Image);
    if (SUCCEEDED(hr))
        hr = Device->CreateUnorderedAccessView(Out, NULL, &Views[0]);
    if (SUCCEEDED(hr))
    {
        RawDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        RawDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        RawDesc.Buffer.NumElements = 8;
        RawDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        hr = Device->CreateUnorderedAccessView(Counts, &RawDesc, &Views[1]);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateUnorderedAccessView(Image, NULL, &Views[2]);
    if (SUCCEEDED(hr))
    {
        AppendDesc.Format = DXGI_FORMAT_UNKNOWN;
        AppendDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        AppendDesc.Buffer.NumElements = 128;
        AppendDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
        hr = Device->CreateUnorderedAccessView(Picked, &AppendDesc, &Views[3]);
    }
    if (SUCCEEDED(hr))
    {
        const UINT Zero[4] = {};

        Context->ClearUnorderedAccessViewUint(Views[1], Zero);
        Context->ClearUnorderedAccessViewUint(Views[3], Zero);
        Context->CSSetShader(Shader, NULL, 0);
        Context->CSSetUnorderedAccessViews(0, 4, Views, Initial);
        Context->Dispatch(4, 2, 1);
        Context->CopyStructureCount(Tally, 0, Views[3]);
        hr = TcReadBuffer(Device, Context, Out, Result);
        if (SUCCEEDED(hr))
            hr = TcReadBuffer(Device, Context, Counts, Result);
        if (SUCCEEDED(hr))
            hr = TcReadBuffer(Device, Context, Tally, Result);
        if (SUCCEEDED(hr))
            hr = TcReadBytes(Device, Context, Image, 4, Result);
    }

    for (ID3D11UnorderedAccessView *View : Views)
    {
        if (View)
            View->Release();
    }
    ID3D11DeviceChild *Objects[] = { Out, Counts, Picked, Tally, Image, Shader };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    if (Blob)
        Blob->Release();
    return hr;
}

/* Every BC1 to BC5 variant, random blocks, loaded texel by texel into a float target */
static
HRESULT
SceneBlocks(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const DXGI_FORMAT Formats[] =
    {
        DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_BC1_UNORM_SRGB, DXGI_FORMAT_BC2_UNORM, DXGI_FORMAT_BC3_UNORM,
        DXGI_FORMAT_BC3_UNORM_SRGB, DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_BC4_SNORM, DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_BC5_SNORM,
    };
    const TcVertex Vertices[] =
    {
        TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
        TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, -1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
    };
    HRESULT hr = S_OK;

    for (DXGI_FORMAT Format : Formats)
    {
        BOOLEAN Small = (Format == DXGI_FORMAT_BC1_UNORM || Format == DXGI_FORMAT_BC1_UNORM_SRGB ||
                         Format == DXGI_FORMAT_BC4_UNORM || Format == DXGI_FORMAT_BC4_SNORM);
        UINT BlockBytes = Small ? 8 : 16;
        D3D11_TEXTURE2D_DESC Desc = { 16, 16, 1, 1, Format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        std::vector<BYTE> Blocks(16 * BlockBytes);
        D3D11_SUBRESOURCE_DATA Data = { Blocks.data(), 4 * BlockBytes, 0 };
        ID3D11Texture2D *Texture = NULL;
        ID3D11ShaderResourceView *View = NULL;
        UINT Seed = (UINT)Format * 7919;
        TcDrawSetup Setup;

        for (SIZE_T Index = 0; Index < Blocks.size(); Index++)
        {
            Seed = Seed * 1103515245 + 12345;
            Blocks[Index] = (BYTE)(Seed >> 16);
        }
        hr = Device->CreateTexture2D(&Desc, &Data, &Texture);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(Texture, NULL, &View);
        if (SUCCEEDED(hr))
        {
            Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            Setup.BytesPerPixel = 16;
            Setup.Size = 16;
            Setup.Vertices = Vertices;
            Setup.VertexCount = ARRAYSIZE(Vertices);
            Setup.PixelEntry = "psload";
            Setup.View = View;
            hr = TcDrawScene(Device, Context, Setup, Result);
        }
        if (View)
            View->Release();
        if (Texture)
            Texture->Release();
        if (FAILED(hr))
            break;
    }
    return hr;
}

/* A large target covered several times with filtered texturing and blending: mostly a speed check */
static
HRESULT
SceneFill(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    std::vector<TcVertex> Vertices;
    TcDrawSetup Setup;

    for (UINT Layer = 0; Layer < 4; Layer++)
    {
        FLOAT Shift = (FLOAT)Layer * 0.37f;

        Vertices.push_back(TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0, Shift, 0.0f));
        Vertices.push_back(TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0, Shift + 3.0f, 0.0f));
        Vertices.push_back(TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0, Shift, 3.0f));
        Vertices.push_back(TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0, Shift + 3.0f, 0.0f));
        Vertices.push_back(TcMake(1.0f, -1.0f, 0.5f, 1.0f, 0, Shift + 3.0f, 3.0f));
        Vertices.push_back(TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0, Shift, 3.0f));
    }
    Setup.Size = 512;
    Setup.Vertices = Vertices.data();
    Setup.VertexCount = (UINT)Vertices.size();
    Setup.Texture = TRUE;
    Setup.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Setup.PixelEntry = "pstex";
    Setup.Blend = TRUE;
    return TcDrawScene(Device, Context, Setup, Result);
}

/* Interpolation alone: one triangle without perspective, then the same one with it */
static
HRESULT
SceneInterpolate(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    const TcVertex Flat[] =
    {
        TcMake(-0.9f, -0.8f, 0.2f, 1.0f, 0xFF0000FF, 0.0f, 0.0f),
        TcMake(0.85f, -0.95f, 0.5f, 1.0f, 0x00FF00FF, 1.0f, 0.0f),
        TcMake(0.1f, 0.9f, 0.9f, 1.0f, 0x0000FF80, 0.0f, 1.0f),
    };
    const TcVertex Deep[] =
    {
        TcMake(-0.9f, -0.8f, 0.2f, 1.0f, 0xFF0000FF, 0.0f, 0.0f),
        TcMake(0.85f, -0.95f, 0.5f, 3.0f, 0x00FF00FF, 1.0f, 0.0f),
        TcMake(0.1f, 0.9f, 0.9f, 1.5f, 0x0000FF80, 0.0f, 1.0f),
    };
    TcDrawSetup Setup;
    HRESULT hr;

    Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    Setup.BytesPerPixel = 16;
    Setup.Size = 32;
    Setup.Vertices = Flat;
    Setup.VertexCount = ARRAYSIZE(Flat);
    hr = TcDrawScene(Device, Context, Setup, Result);
    if (SUCCEEDED(hr))
    {
        Setup.Vertices = Deep;
        hr = TcDrawScene(Device, Context, Setup, Result);
    }
    return hr;
}

/* Exact inputs for the filter: a 4x4 texture sampled at positions known to the bit, into a float target */
static
HRESULT
SceneFilter(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const UINT Texels[16] =
    {
        0xFF000000, 0x80FF2010, 0x00113377, 0xFFFFFFFF,
        0x7F7F7F7F, 0x01020304, 0xFEFDFCFB, 0x12345678,
        0x9ABCDEF0, 0x00FF00FF, 0xFF00FF00, 0x55AA55AA,
        0x0F1E2D3C, 0xC3D2E1F0, 0x80808080, 0x7F808180,
    };
    static const D3D11_TEXTURE_ADDRESS_MODE Modes[] =
    {
        D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_BORDER,
    };
    const TcVertex Vertices[] =
    {
        TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
        TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, -1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
    };
    D3D11_TEXTURE2D_DESC Desc = { 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    D3D11_SUBRESOURCE_DATA Data = { Texels, 16, 0 };
    ID3D11Texture2D *Texture = NULL;
    ID3D11ShaderResourceView *View = NULL;
    HRESULT hr;

    hr = Device->CreateTexture2D(&Desc, &Data, &Texture);
    if (SUCCEEDED(hr))
        hr = Device->CreateShaderResourceView(Texture, NULL, &View);
    for (UINT Index = 0; Index < ARRAYSIZE(Modes) * 2 && SUCCEEDED(hr); Index++)
    {
        ID3D11SamplerState *Sampler = NULL;
        D3D11_SAMPLER_DESC SamplerDesc = {};
        TcDrawSetup Setup;

        SamplerDesc.Filter = (Index & 1) ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerDesc.AddressU = Modes[Index / 2];
        SamplerDesc.AddressV = Modes[Index / 2];
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.BorderColor[0] = 0.25f;
        SamplerDesc.BorderColor[3] = 1.0f;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = Device->CreateSamplerState(&SamplerDesc, &Sampler);
        if (FAILED(hr))
            break;
        Context->PSSetSamplers(0, 1, &Sampler);
        Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        Setup.BytesPerPixel = 16;
        Setup.Size = 64;
        Setup.Vertices = Vertices;
        Setup.VertexCount = ARRAYSIZE(Vertices);
        Setup.PixelEntry = "psfilter";
        Setup.View = View;
        Setup.Sampler = Sampler;
        hr = TcDrawScene(Device, Context, Setup, Result);
        Sampler->Release();
    }
    if (View)
        View->Release();
    if (Texture)
        Texture->Release();
    return hr;
}

static const TcScene Scenes[] =
{
    { "caps", 0, SceneCaps },
    { "clear", 0, SceneClear },
    { "copy", 0, SceneCopy },
    { "tri", 0, SceneTriangles },
    { "grad", 0, SceneGradient },
    { "grad8", 0, SceneGradient8 },
    { "depth", 0, SceneDepthBlend },
    { "clip", 0, SceneClip },
    { "tex", 0, SceneTexture },
    { "math", 0, SceneMath },
    { "lines", 0, SceneLines },
    { "strips", 0, SceneStrips },
    { "points", 0, ScenePoints },
    { "queries", 0, SceneQueries },
    { "mips", 0, SceneMips },
    { "compute", 0, SceneCompute },
    { "bc", 0, SceneBlocks },
    { "fill", 0, SceneFill },
    { "filter", 0, SceneFilter },
    { "interp", 0, SceneInterpolate },
};

static
HRESULT
TcRun(
    _In_ HMODULE Module,
    _In_ const TcScene &Scene,
    _Out_ TcResult *Result)
{
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };
    ID3D11Device *Device = NULL;
    ID3D11DeviceContext *Context = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_SOFTWARE, Module, 0, Levels, ARRAYSIZE(Levels),
                           D3D11_SDK_VERSION, &Device, NULL, &Context);
    if (FAILED(hr))
        return hr;

    hr = Scene.Run(Device, Context, Result);
    Context->Release();
    Device->Release();
    return hr;
}

static
VOID
TcWritePpm(
    _In_z_ PCSTR Path,
    _In_ const TcResult &Result)
{
    FILE *File;

    if (Result.Pixels.empty() || fopen_s(&File, Path, "wb") != 0)
        return;
    fprintf(File, "P6\n%u %u\n255\n", Result.Width, Result.Height);
    for (SIZE_T Index = 0; Index + 3 < Result.Pixels.size(); Index += 4)
        fwrite(&Result.Pixels[Index], 1, 3, File);
    fclose(File);
}

/*
 * Results cross processes as files: the Direct3D runtime finds the software rasterizer by its
 * module name, so two modules called d3d10warp.dll in one process would end up as one.
 */
static
BOOLEAN
TcSaveResult(
    _In_z_ PCWSTR Path,
    _In_ HRESULT Status,
    _In_ double Milliseconds,
    _In_ const TcResult &Result)
{
    FILE *File;
    UINT64 Sizes[2] = { Result.Data.size(), Result.Pixels.size() };

    if (_wfopen_s(&File, Path, L"wb") != 0)
        return FALSE;
    fwrite(&Status, sizeof(Status), 1, File);
    fwrite(&Milliseconds, sizeof(Milliseconds), 1, File);
    fwrite(&Result.Width, sizeof(Result.Width), 1, File);
    fwrite(&Result.Height, sizeof(Result.Height), 1, File);
    fwrite(Sizes, sizeof(Sizes), 1, File);
    fwrite(Result.Data.data(), 1, Result.Data.size(), File);
    fwrite(Result.Pixels.data(), 1, Result.Pixels.size(), File);
    fclose(File);
    return TRUE;
}

static
BOOLEAN
TcLoadResult(
    _In_z_ PCWSTR Path,
    _Out_ HRESULT *Status,
    _Out_ double *Milliseconds,
    _Out_ TcResult *Result)
{
    FILE *File;
    UINT64 Sizes[2] = {};
    BOOLEAN Complete;

    *Status = E_FAIL;
    *Milliseconds = 0.0;
    if (_wfopen_s(&File, Path, L"rb") != 0)
        return FALSE;
    Complete = fread(Status, sizeof(*Status), 1, File) == 1 && fread(Milliseconds, sizeof(*Milliseconds), 1, File) == 1 &&
               fread(&Result->Width, sizeof(Result->Width), 1, File) == 1 &&
               fread(&Result->Height, sizeof(Result->Height), 1, File) == 1 && fread(Sizes, sizeof(Sizes), 1, File) == 1;
    if (Complete)
    {
        Result->Data.resize((SIZE_T)Sizes[0]);
        Result->Pixels.resize((SIZE_T)Sizes[1]);
        Complete = fread(Result->Data.data(), 1, Result->Data.size(), File) == Result->Data.size() &&
                   fread(Result->Pixels.data(), 1, Result->Pixels.size(), File) == Result->Pixels.size();
    }
    fclose(File);
    return Complete;
}

/* Child: loads exactly one WARP, runs one scene and writes what it produced */
static
int
TcChild(
    _In_z_ PCWSTR ModulePath,
    _In_z_ PCWSTR SceneName,
    _In_z_ PCWSTR OutputPath)
{
    std::string Name(SceneName, SceneName + wcslen(SceneName));
    HMODULE Module = LoadLibraryExW(ModulePath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;

    if (Module == NULL)
        return 3;
    QueryPerformanceFrequency(&Frequency);
    for (const TcScene &Scene : Scenes)
    {
        TcResult Result;
        HRESULT Status;

        if (_stricmp(Scene.Name, Name.c_str()) != 0)
            continue;
        QueryPerformanceCounter(&Start);
        Status = TcRun(Module, Scene, &Result);
        QueryPerformanceCounter(&End);
        return TcSaveResult(OutputPath, Status, (End.QuadPart - Start.QuadPart) * 1000.0 / Frequency.QuadPart, Result) ? 0 : 4;
    }
    return 5;
}

/* Parent: runs one scene in a fresh copy of this program and reads the result back */
static
BOOLEAN
TcRunIsolated(
    _In_z_ PCWSTR ModulePath,
    _In_z_ PCSTR SceneName,
    _In_z_ PCWSTR OutputPath,
    _Out_ HRESULT *Status,
    _Out_ double *Milliseconds,
    _Out_ TcResult *Result)
{
    WCHAR Self[MAX_PATH];
    std::wstring Command;
    STARTUPINFOW Startup = { sizeof(Startup) };
    PROCESS_INFORMATION Process = {};
    std::wstring Scene(SceneName, SceneName + strlen(SceneName));

    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    DeleteFileW(OutputPath);
    Command = L"\"" + std::wstring(Self) + L"\" --child \"" + ModulePath + L"\" " + Scene + L" \"" + OutputPath + L"\"";
    if (!CreateProcessW(NULL, &Command[0], NULL, NULL, FALSE, 0, NULL, NULL, &Startup, &Process))
        return FALSE;
    WaitForSingleObject(Process.hProcess, INFINITE);
    CloseHandle(Process.hThread);
    CloseHandle(Process.hProcess);
    return TcLoadResult(OutputPath, Status, Milliseconds, Result);
}

int
__cdecl
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t **argv)
{
    WCHAR SystemPath[MAX_PATH];
    WCHAR OursPath[MAX_PATH];
    UINT Failures = 0;

    if (argc == 5 && wcscmp(argv[1], L"--child") == 0)
        return TcChild(argv[2], argv[3], argv[4]);

    if (argc < 2)
    {
        wprintf(L"usage: warpcheck <our d3d10warp.dll> [scene]\n");
        return 2;
    }

    GetSystemDirectoryW(SystemPath, ARRAYSIZE(SystemPath));
    wcscat_s(SystemPath, L"\\d3d10warp.dll");
    if (GetFullPathNameW(argv[1], ARRAYSIZE(OursPath), OursPath, NULL) == 0 || _wcsicmp(SystemPath, OursPath) == 0)
    {
        wprintf(L"give the path of our WARP, not the system one\n");
        return 2;
    }

    for (const TcScene &Scene : Scenes)
    {
        TcResult Expected;
        TcResult Actual;
        HRESULT SystemHr;
        HRESULT OurHr;
        double SystemTime;
        double OurTime;
        SIZE_T Differ = 0;
        UINT Worst = 0;
        char Name[96];

        if (argc > 2 && _stricmp(Scene.Name, std::string(argv[2], argv[2] + wcslen(argv[2])).c_str()) != 0)
            continue;

        if (!TcRunIsolated(SystemPath, Scene.Name, L"warpcheck_system.out", &SystemHr, &SystemTime, &Expected) ||
            !TcRunIsolated(OursPath, Scene.Name, L"warpcheck_ours.out", &OurHr, &OurTime, &Actual))
        {
            printf("%-8s FAIL a child process did not report back\n", Scene.Name);
            Failures++;
            continue;
        }
        if (SystemHr != OurHr || Expected.Data.size() != Actual.Data.size())
        {
            printf("%-8s FAIL hr %08lX vs %08lX, %zu vs %zu bytes\n", Scene.Name, SystemHr, OurHr,
                   Expected.Data.size(), Actual.Data.size());
            Failures++;
            continue;
        }

        for (SIZE_T Index = 0; Index < Expected.Data.size(); Index++)
        {
            UINT Delta = (UINT)abs((int)Expected.Data[Index] - (int)Actual.Data[Index]);

            if (Delta > Scene.Tolerance)
            {
                if (Differ < 8)
                    printf("  %s byte %zu: %u vs %u\n", Scene.Name, Index, Expected.Data[Index], Actual.Data[Index]);
                Differ++;
            }
            if (Delta > Worst)
                Worst = Delta;
        }

        printf("%-8s %s (%zu bytes, %zu differ, worst %u; %.1f ms system, %.1f ms ours)\n", Scene.Name, Differ ? "FAIL" : "ok",
               Expected.Data.size(), Differ, Worst, SystemTime, OurTime);
        if (Differ)
        {
            Failures++;

            /* Small results are counters: show them whole */
            if (Expected.Data.size() <= 256)
            {
                for (SIZE_T Index = 0; Index + 8 <= Expected.Data.size(); Index += 8)
                {
                    UINT64 Want;
                    UINT64 Got;

                    memcpy(&Want, &Expected.Data[Index], sizeof(Want));
                    memcpy(&Got, &Actual.Data[Index], sizeof(Got));
                    if (Want != Got)
                        printf("  %s qword %zu: %llu vs %llu\n", Scene.Name, Index / 8, Want, Got);
                }
            }
        }

        sprintf_s(Name, "%s_system.ppm", Scene.Name);
        TcWritePpm(Name, Expected);
        sprintf_s(Name, "%s_ours.ppm", Scene.Name);
        TcWritePpm(Name, Actual);

        /* Raw results too, for scenes that produce data rather than a picture */
        {
            FILE *File;

            sprintf_s(Name, "%s_ours.bin", Scene.Name);
            if (fopen_s(&File, Name, "wb") == 0)
            {
                fwrite(Actual.Data.data(), 1, Actual.Data.size(), File);
                fclose(File);
            }
            sprintf_s(Name, "%s_system.bin", Scene.Name);
            if (fopen_s(&File, Name, "wb") == 0)
            {
                fwrite(Expected.Data.data(), 1, Expected.Data.size(), File);
                fclose(File);
            }
        }
    }
    return Failures ? 1 : 0;
}
