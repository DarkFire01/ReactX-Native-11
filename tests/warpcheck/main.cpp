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

/* Fills a buffer from the GPU and reads it back, up to Limit bytes */
static
HRESULT
TcReadBuffer(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _In_ ID3D11Buffer *Buffer,
    _Inout_ TcResult *Result,
    _In_ UINT Limit = UINT_MAX)
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
        TcAppend(Result, Map.pData, (Limit < Desc.ByteWidth) ? Limit : Desc.ByteWidth);
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
        DXGI_FORMAT_BC6H_UF16, DXGI_FORMAT_BC6H_SF16, DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC7_UNORM_SRGB,
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
        BOOLEAN Modern = (Format >= DXGI_FORMAT_BC6H_TYPELESS);
        UINT BlockBytes = Small ? 8 : 16;
        UINT Size = Modern ? 64 : 16;
        D3D11_TEXTURE2D_DESC Desc = { Size, Size, 1, 1, Format, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        std::vector<BYTE> Blocks((SIZE_T)(Size / 4) * (Size / 4) * BlockBytes);
        D3D11_SUBRESOURCE_DATA Data = { Blocks.data(), (Size / 4) * BlockBytes, 0 };
        ID3D11Texture2D *Texture = NULL;
        ID3D11ShaderResourceView *View = NULL;
        UINT Seed = (UINT)Format * 7919;
        TcDrawSetup Setup;

        for (SIZE_T Index = 0; Index < Blocks.size(); Index++)
        {
            Seed = Seed * 1103515245 + 12345;
            Blocks[Index] = (BYTE)(Seed >> 16);
        }

        /* BC7 picks its mode from the lowest set bit: walk through all eight and the reserved one */
        if (Format >= DXGI_FORMAT_BC7_TYPELESS)
        {
            for (SIZE_T Block = 0; Block < Blocks.size() / 16; Block++)
            {
                UINT Mode = (UINT)(Block % 9);

                Blocks[Block * 16] = (Mode == 8) ? 0 : (BYTE)((Blocks[Block * 16] & ~((2u << Mode) - 1)) | (1u << Mode));
            }
        }
        hr = Device->CreateTexture2D(&Desc, &Data, &Texture);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(Texture, NULL, &View);
        if (SUCCEEDED(hr))
        {
            Setup.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            Setup.BytesPerPixel = 16;
            Setup.Size = Size;
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

static const char TcGeometrySource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct AI { float4 p : POSITION; float4 c : COLOR; };\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n"
    "VO vsauto(AI i) { VO o; o.p = i.p; o.c = i.c.bgra; o.t = 0; return o; }\n"
    "float4 ps(VO i) : SV_Target { return i.c; }\n"
    "[maxvertexcount(4)] [instance(2)]\n"
    "void gs(point VO i[1], uint id : SV_GSInstanceID, inout TriangleStream<VO> s)\n"
    "{\n"
    "    float2 o = float2(0.06, 0.05) * (1 + id);\n"
    "    VO v = i[0]; v.c.a = 0.5 + 0.25 * id;\n"
    "    v.p = i[0].p + float4(-o.x, o.y, 0, 0); s.Append(v);\n"
    "    v.p = i[0].p + float4(o.x, o.y, 0, 0); s.Append(v);\n"
    "    v.p = i[0].p + float4(-o.x, -o.y, 0, 0); s.Append(v);\n"
    "    v.p = i[0].p + float4(o.x, -o.y, 0, 0); s.Append(v);\n"
    "}\n";

static
HRESULT
TcCompileFrom(
    _In_z_ PCSTR Source,
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _Out_ ID3DBlob **Blob)
{
    ID3DBlob *Errors = NULL;
    HRESULT hr = D3DCompile(Source, strlen(Source), "warpcheck", NULL, NULL, Entry, Profile, 0, 0, Blob, &Errors);

    if (Errors)
    {
        if (FAILED(hr))
            printf("%s", (const char *)Errors->GetBufferPointer());
        Errors->Release();
    }
    return hr;
}

static
HRESULT
TcCompileWith(
    _In_z_ PCSTR Source,
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _In_ const D3D_SHADER_MACRO *Defines,
    _Out_ ID3DBlob **Blob)
{
    ID3DBlob *Errors = NULL;
    HRESULT hr = D3DCompile(Source, strlen(Source), "warpcheck", Defines, NULL, Entry, Profile, 0, 0, Blob, &Errors);

    if (Errors)
    {
        if (FAILED(hr))
            printf("%s", (const char *)Errors->GetBufferPointer());
        Errors->Release();
    }
    return hr;
}

/* Geometry shader expansion with instancing, stream output with statistics, DrawAuto and pass through capture */
static
HRESULT
SceneGeometry(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const D3D11_INPUT_ELEMENT_DESC AutoElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const D3D11_SO_DECLARATION_ENTRY Declaration[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
        { 0, "COLOR", 0, 0, 4, 0 },
    };
    const UINT Stride = 32;
    std::vector<TcVertex> Points;
    D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_BUFFER_DESC CaptureDesc = { 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_QUERY_DESC StatsDesc = { D3D11_QUERY_SO_STATISTICS, 0 };
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    ID3DBlob *VsBlob = NULL;
    ID3DBlob *AutoBlob = NULL;
    ID3DBlob *GsBlob = NULL;
    ID3DBlob *PsBlob = NULL;
    ID3D11VertexShader *Vs = NULL;
    ID3D11VertexShader *AutoVs = NULL;
    ID3D11GeometryShader *Gs = NULL;
    ID3D11GeometryShader *Capture = NULL;
    ID3D11GeometryShader *PassThrough = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11InputLayout *AutoLayout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Captured = NULL;
    ID3D11Texture2D *Target = NULL;
    ID3D11RenderTargetView *TargetView = NULL;
    ID3D11RasterizerState *Raster = NULL;
    ID3D11Query *Stats = NULL;
    D3D11_QUERY_DATA_SO_STATISTICS StatsData = {};
    UINT Offset = 0;
    HRESULT hr;

    for (UINT Index = 0; Index < 9; Index++)
    {
        Points.push_back(TcMake(((FLOAT)(Index % 3) - 1.0f) * 0.6f, ((FLOAT)(Index / 3) - 1.0f) * 0.6f, 0.5f, 1.0f,
                                0x20408000 | (Index * 0x1C000000) | 0xFF));
    }

    hr = TcCompileFrom(TcGeometrySource, "vs", "vs_5_0", &VsBlob);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcGeometrySource, "vsauto", "vs_5_0", &AutoBlob);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcGeometrySource, "gs", "gs_5_0", &GsBlob);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcGeometrySource, "ps", "ps_5_0", &PsBlob);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(AutoBlob->GetBufferPointer(), AutoBlob->GetBufferSize(), NULL, &AutoVs);
    if (SUCCEEDED(hr))
        hr = Device->CreateGeometryShader(GsBlob->GetBufferPointer(), GsBlob->GetBufferSize(), NULL, &Gs);
    if (SUCCEEDED(hr))
        hr = Device->CreateGeometryShaderWithStreamOutput(GsBlob->GetBufferPointer(), GsBlob->GetBufferSize(), Declaration,
                                                          ARRAYSIZE(Declaration), &Stride, 1, 0, NULL, &Capture);
    if (SUCCEEDED(hr))
        hr = Device->CreateGeometryShaderWithStreamOutput(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), Declaration,
                                                          ARRAYSIZE(Declaration), &Stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, NULL,
                                                          &PassThrough);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(AutoElements, ARRAYSIZE(AutoElements), AutoBlob->GetBufferPointer(), AutoBlob->GetBufferSize(), &AutoLayout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Points.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Points.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&CaptureDesc, NULL, &Captured);
    if (SUCCEEDED(hr))
        hr = Device->CreateTexture2D(&Desc, NULL, &Target);
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target, NULL, &TargetView);
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateQuery(&StatsDesc, &Stats);
    if (FAILED(hr))
        goto Exit;

    {
        UINT VertexStride = sizeof(TcVertex);

        /* Expansion straight to the screen, capturing what it emits */
        Context->ClearRenderTargetView(TargetView, Clear);
        Context->OMSetRenderTargets(1, &TargetView, NULL);
        Context->RSSetViewports(1, &Viewport);
        Context->RSSetState(Raster);
        Context->IASetInputLayout(Layout);
        Context->IASetVertexBuffers(0, 1, &Input, &VertexStride, &Offset);
        Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        Context->VSSetShader(Vs, NULL, 0);
        Context->GSSetShader(Capture, NULL, 0);
        Context->PSSetShader(Ps, NULL, 0);
        Context->SOSetTargets(1, &Captured, &Offset);
        Context->Begin(Stats);
        Context->Draw((UINT)Points.size(), 0);
        Context->End(Stats);
        while (Context->GetData(Stats, &StatsData, sizeof(StatsData), 0) == S_FALSE)
            ;
        TcAppend(Result, &StatsData, sizeof(StatsData));
        hr = TcReadBytes(Device, Context, Target, 4, Result);

        /* The captured triangles again, through DrawAuto, with their colors swapped around */
        if (SUCCEEDED(hr))
        {
            ID3D11Buffer *None = NULL;

            Context->SOSetTargets(1, &None, &Offset);
            Context->ClearRenderTargetView(TargetView, Clear);
            Context->IASetInputLayout(AutoLayout);
            Context->IASetVertexBuffers(0, 1, &Captured, &Stride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->VSSetShader(AutoVs, NULL, 0);
            Context->GSSetShader(NULL, NULL, 0);
            Context->DrawAuto();
            hr = TcReadBytes(Device, Context, Target, 4, Result);
        }

        /* The vertex shader's own output captured with nothing drawn */
        if (SUCCEEDED(hr))
        {
            ID3D11Buffer *None = NULL;
            UINT Zero = 0;

            Context->IASetInputLayout(Layout);
            Context->IASetVertexBuffers(0, 1, &Input, &VertexStride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
            Context->VSSetShader(Vs, NULL, 0);
            Context->GSSetShader(PassThrough, NULL, 0);
            Context->SOSetTargets(1, &Captured, &Zero);
            Context->Draw((UINT)Points.size(), 0);
            Context->SOSetTargets(1, &None, &Offset);
            Context->GSSetShader(NULL, NULL, 0);
            hr = TcReadBuffer(Device, Context, Captured, Result);
        }
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, AutoVs, Gs, Capture, PassThrough, Ps, Layout, AutoLayout, Input, Captured, Target, TargetView, Raster, Stats };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    ID3DBlob *Blobs[] = { VsBlob, AutoBlob, GsBlob, PsBlob };
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

static const char TcTessellationSource[] =
    "cbuffer F : register(b0) { float4 E; float4 I; };\n"
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct CP { float4 p : POS; };\n"
    "CP vs(VI i) { CP o; o.p = i.p; return o; }\n"
    "#if DOMAIN_QUAD\n"
    "#define NPTS 4\n"
    "#define LOC float2\n"
    "#define LOCZ 0\n"
    "struct PC { float e[4] : SV_TessFactor; float i[2] : SV_InsideTessFactor; };\n"
    "PC pc(InputPatch<CP, NPTS> p) { PC o; o.e[0] = E.x; o.e[1] = E.y; o.e[2] = E.z; o.e[3] = E.w; o.i[0] = I.x; o.i[1] = I.y; return o; }\n"
    "float4 place(const OutputPatch<CP, NPTS> p, LOC l) { return lerp(lerp(p[0].p, p[1].p, l.x), lerp(p[3].p, p[2].p, l.x), l.y); }\n"
    "#elif DOMAIN_TRI\n"
    "#define NPTS 3\n"
    "#define LOC float3\n"
    "#define LOCZ l.z\n"
    "struct PC { float e[3] : SV_TessFactor; float i : SV_InsideTessFactor; };\n"
    "PC pc(InputPatch<CP, NPTS> p) { PC o; o.e[0] = E.x; o.e[1] = E.y; o.e[2] = E.z; o.i = I.x; return o; }\n"
    "float4 place(const OutputPatch<CP, NPTS> p, LOC l) { return p[0].p * l.x + p[1].p * l.y + p[2].p * l.z; }\n"
    "#else\n"
    "#define NPTS 2\n"
    "#define LOC float2\n"
    "#define LOCZ 0\n"
    "struct PC { float e[2] : SV_TessFactor; };\n"
    "PC pc(InputPatch<CP, NPTS> p) { PC o; o.e[0] = E.x; o.e[1] = E.y; return o; }\n"
    "float4 place(const OutputPatch<CP, NPTS> p, LOC l) { return lerp(p[0].p, p[1].p, l.x) + float4(0, l.y * 0.5, 0, 0); }\n"
    "#endif\n"
    "#ifdef DOMAIN\n"
    "[domain(DOMAIN)] [partitioning(PART)] [outputtopology(TOPO)] [outputcontrolpoints(NPTS)] [patchconstantfunc(\"pc\")]\n"
    "CP hs(InputPatch<CP, NPTS> p, uint i : SV_OutputControlPointID) { CP o; o.p = p[i].p + float4(0, 0, 0.01 * i, 0); return o; }\n"
    "struct DO { float4 p : SV_Position; float4 d : LOCATION; };\n"
    "[domain(DOMAIN)] DO ds(PC c, LOC l : SV_DomainLocation, const OutputPatch<CP, NPTS> p, uint id : SV_PrimitiveID)\n"
    "{ DO o; o.p = place(p, l); o.d = float4(l.xy, LOCZ, c.e[0] + id * 100); return o; }\n"
    "float4 ps(DO i) : SV_Target { return float4(i.d.xyz, 1); }\n"
    "#endif\n";

/* Every domain, partitioning and output primitive over a set of tess factors, captured from the domain shader */
static
HRESULT
SceneTessellation(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const D3D11_SO_DECLARATION_ENTRY Declaration[] =
    {
        { 0, "SV_POSITION", 0, 0, 4, 0 },
        { 0, "LOCATION", 0, 0, 4, 0 },
    };
    static const PCSTR Domains[3] = { "\"quad\"", "\"tri\"", "\"isoline\"" };
    static const PCSTR DomainMacros[3] = { "DOMAIN_QUAD", "DOMAIN_TRI", "DOMAIN_LINE" };
    static const UINT PatchPoints[3] = { 4, 3, 2 };
    static const PCSTR Partitions[4] = { "\"integer\"", "\"pow2\"", "\"fractional_odd\"", "\"fractional_even\"" };
    static const PCSTR Topologies[4] = { "\"triangle_cw\"", "\"triangle_ccw\"", "\"point\"", "\"line\"" };
    static const FLOAT Factors[][8] =
    {
        { 1, 1, 1, 1, 1, 1 },
        { 2.5f, 3.2f, 1.7f, 4.9f, 3.3f, 2.1f },
        { 7, 7, 7, 7, 6, 5 },
        { 64, 1, 33.3f, 12, 17.5f, 9.9f },
        { 0.5f, 1.2f, 2, 3, 0.7f, 1.0f },
        { 1, 1, 1, 1, 2.2f, 1 },
        { 0, 2, 2, 2, 2, 2 },
        { 3, 4, 5, 6, 1.0f, 8 },
        { 1.00001f, 1, 1, 1, 1, 1 },
        { 100, 2.0001f, 5.5f, 6.5f, 2.9999f, 3.0001f },
        { 2, 2, 2, 2, 2, 2 },
        { 1, 1, 1, 1, 9, 4 },
    };
    const UINT Stride = 32;
    std::vector<TcVertex> Points;
    D3D11_BUFFER_DESC CaptureDesc = { 1 << 20, D3D11_USAGE_DEFAULT, D3D11_BIND_STREAM_OUTPUT, 0, 0, 0 };
    D3D11_BUFFER_DESC ConstantDesc = { 32, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
    D3D11_QUERY_DESC StatsDesc = { D3D11_QUERY_SO_STATISTICS, 0 };
    D3D11_QUERY_DESC PipeDesc = { D3D11_QUERY_PIPELINE_STATISTICS, 0 };
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    ID3DBlob *VsBlob = NULL;
    ID3D11VertexShader *Vs = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Captured = NULL;
    ID3D11Buffer *Constants = NULL;
    ID3D11Query *Stats = NULL;
    ID3D11Query *Pipe = NULL;
    ID3D11Texture2D *Target = NULL;
    ID3D11RenderTargetView *TargetView = NULL;
    ID3D11RasterizerState *Raster = NULL;
    UINT Offset = 0;
    UINT VertexStride = sizeof(TcVertex);
    HRESULT hr;

    /* Two patches worth of points for every domain */
    for (UINT Patch = 0; Patch < 2; Patch++)
    {
        FLOAT X = Patch ? 0.1f : -0.9f;

        Points.push_back(TcMake(X, -0.8f, 0.2f, 1.0f, 0xFF));
        Points.push_back(TcMake(X + 0.8f, -0.7f, 0.3f, 1.0f, 0xFF));
        Points.push_back(TcMake(X + 0.7f, 0.8f, 0.4f, 1.0f, 0xFF));
        Points.push_back(TcMake(X - 0.05f, 0.6f, 0.5f, 1.0f, 0xFF));
    }

    hr = TcCompileFrom(TcTessellationSource, "vs", "vs_5_0", &VsBlob);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Points.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Points.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&CaptureDesc, NULL, &Captured);
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&ConstantDesc, NULL, &Constants);
    if (SUCCEEDED(hr))
        hr = Device->CreateQuery(&StatsDesc, &Stats);
    if (SUCCEEDED(hr))
        hr = Device->CreateQuery(&PipeDesc, &Pipe);
    if (SUCCEEDED(hr))
        hr = Device->CreateTexture2D(&Desc, NULL, &Target);
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target, NULL, &TargetView);
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (FAILED(hr))
        goto Exit;

    Context->IASetInputLayout(Layout);
    Context->VSSetShader(Vs, NULL, 0);
    Context->HSSetConstantBuffers(0, 1, &Constants);
    Context->RSSetViewports(1, &Viewport);
    Context->RSSetState(Raster);

    for (UINT Domain = 0; Domain < 3 && SUCCEEDED(hr); Domain++)
    {
        D3D_SHADER_MACRO DomainDefines[] = { { DomainMacros[Domain], "1" }, { "DOMAIN", Domains[Domain] }, { "PART", Partitions[0] },
                                             { "TOPO", Topologies[(Domain == 2) ? 3 : 0] }, { NULL, NULL } };
        ID3DBlob *DsBlob = NULL;
        ID3D11DomainShader *Ds = NULL;
        ID3D11GeometryShader *Capture = NULL;
        ID3D11Buffer *Packed = NULL;
        std::vector<TcVertex> Patches;

        /* Each domain's patches are packed from the shared corner list */
        for (UINT Patch = 0; Patch < 2; Patch++)
        {
            for (UINT Point = 0; Point < PatchPoints[Domain]; Point++)
                Patches.push_back(Points[Patch * 4 + Point]);
        }
        {
            D3D11_BUFFER_DESC PackedDesc = { (UINT)(Patches.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
            D3D11_SUBRESOURCE_DATA Data = { Patches.data(), 0, 0 };

            hr = Device->CreateBuffer(&PackedDesc, &Data, &Packed);
        }
        if (SUCCEEDED(hr))
            hr = TcCompileWith(TcTessellationSource, "ds", "ds_5_0", DomainDefines, &DsBlob);
        if (SUCCEEDED(hr))
            hr = Device->CreateDomainShader(DsBlob->GetBufferPointer(), DsBlob->GetBufferSize(), NULL, &Ds);
        if (SUCCEEDED(hr))
            hr = Device->CreateGeometryShaderWithStreamOutput(DsBlob->GetBufferPointer(), DsBlob->GetBufferSize(), Declaration,
                                                              ARRAYSIZE(Declaration), &Stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, NULL,
                                                              &Capture);

        for (UINT Partition = 0; Partition < 4 && SUCCEEDED(hr); Partition++)
        {
            for (UINT Topology = 0; Topology < 4 && SUCCEEDED(hr); Topology++)
            {
                D3D_SHADER_MACRO Defines[] = { { DomainMacros[Domain], "1" }, { "DOMAIN", Domains[Domain] },
                                               { "PART", Partitions[Partition] }, { "TOPO", Topologies[Topology] }, { NULL, NULL } };
                ID3DBlob *HsBlob = NULL;
                ID3D11HullShader *Hs = NULL;
                BOOLEAN Lines = (Domain == 2);

                /* Lines only come out of isolines, triangles only out of the others */
                if ((Lines && Topology < 2) || (!Lines && Topology == 3))
                    continue;
                hr = TcCompileWith(TcTessellationSource, "hs", "hs_5_0", Defines, &HsBlob);
                if (SUCCEEDED(hr))
                    hr = Device->CreateHullShader(HsBlob->GetBufferPointer(), HsBlob->GetBufferSize(), NULL, &Hs);

                for (UINT Case = 0; Case < ARRAYSIZE(Factors) && SUCCEEDED(hr); Case++)
                {
                    D3D11_QUERY_DATA_SO_STATISTICS StatsData = {};
                    D3D11_QUERY_DATA_PIPELINE_STATISTICS PipeData = {};
                    ID3D11Buffer *None = NULL;
                    UINT Header[4] = { Domain, Partition, Topology, Case };
                    UINT Corners = (Topology == 2) ? 1 : (Lines ? 2 : 3);

                    Context->UpdateSubresource(Constants, 0, NULL, Factors[Case], 0, 0);
                    Context->IASetVertexBuffers(0, 1, &Packed, &VertexStride, &Offset);
                    Context->IASetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY)(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + PatchPoints[Domain] - 1));
                    Context->HSSetShader(Hs, NULL, 0);
                    Context->DSSetShader(Ds, NULL, 0);
                    Context->GSSetShader(Capture, NULL, 0);
                    Context->PSSetShader(NULL, NULL, 0);
                    Context->SOSetTargets(1, &Captured, &Offset);
                    Context->Begin(Pipe);
                    Context->Begin(Stats);
                    Context->Draw(PatchPoints[Domain] * 2, 0);
                    Context->End(Stats);
                    Context->End(Pipe);
                    Context->SOSetTargets(1, &None, &Offset);
                    while (Context->GetData(Stats, &StatsData, sizeof(StatsData), 0) == S_FALSE)
                        ;
                    while (Context->GetData(Pipe, &PipeData, sizeof(PipeData), 0) == S_FALSE)
                        ;
                    TcAppend(Result, Header, sizeof(Header));
                    TcAppend(Result, &StatsData, sizeof(StatsData));
                    TcAppend(Result, &PipeData.HSInvocations, sizeof(PipeData.HSInvocations));
                    TcAppend(Result, &PipeData.DSInvocations, sizeof(PipeData.DSInvocations));
                    hr = TcReadBuffer(Device, Context, Captured, Result, (UINT)(StatsData.NumPrimitivesWritten * Corners * Stride));
                }
                if (Hs)
                    Hs->Release();
                if (HsBlob)
                    HsBlob->Release();
            }
        }

        /* One quad and one triangle patch drawn on screen */
        if (SUCCEEDED(hr) && Domain < 2)
        {
            D3D_SHADER_MACRO Defines[] = { { DomainMacros[Domain], "1" }, { "DOMAIN", Domains[Domain] },
                                           { "PART", Partitions[3] }, { "TOPO", Topologies[0] }, { NULL, NULL } };
            ID3DBlob *HsBlob = NULL;
            ID3DBlob *PsBlob = NULL;
            ID3D11HullShader *Hs = NULL;
            ID3D11PixelShader *Ps = NULL;

            hr = TcCompileWith(TcTessellationSource, "hs", "hs_5_0", Defines, &HsBlob);
            if (SUCCEEDED(hr))
                hr = Device->CreateHullShader(HsBlob->GetBufferPointer(), HsBlob->GetBufferSize(), NULL, &Hs);
            if (SUCCEEDED(hr))
                hr = TcCompileWith(TcTessellationSource, "ps", "ps_5_0", DomainDefines, &PsBlob);
            if (SUCCEEDED(hr))
                hr = Device->CreatePixelShader(PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), NULL, &Ps);
            if (SUCCEEDED(hr))
            {
                Context->UpdateSubresource(Constants, 0, NULL, Factors[1], 0, 0);
                Context->ClearRenderTargetView(TargetView, Clear);
                Context->OMSetRenderTargets(1, &TargetView, NULL);
                Context->IASetVertexBuffers(0, 1, &Packed, &VertexStride, &Offset);
                Context->IASetPrimitiveTopology((D3D11_PRIMITIVE_TOPOLOGY)(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + PatchPoints[Domain] - 1));
                Context->HSSetShader(Hs, NULL, 0);
                Context->DSSetShader(Ds, NULL, 0);
                Context->GSSetShader(NULL, NULL, 0);
                Context->PSSetShader(Ps, NULL, 0);
                Context->Draw(PatchPoints[Domain] * 2, 0);
                hr = TcReadBytes(Device, Context, Target, 4, Result);
            }
            ID3D11DeviceChild *Objects[] = { Hs, Ps };
            for (ID3D11DeviceChild *Object : Objects)
            {
                if (Object)
                    Object->Release();
            }
            if (HsBlob)
                HsBlob->Release();
            if (PsBlob)
                PsBlob->Release();
        }

        ID3D11DeviceChild *Objects[] = { Ds, Capture, Packed };
        for (ID3D11DeviceChild *Object : Objects)
        {
            if (Object)
                Object->Release();
        }
        if (DsBlob)
            DsBlob->Release();
    }
    Context->HSSetShader(NULL, NULL, 0);
    Context->DSSetShader(NULL, NULL, 0);
    Context->GSSetShader(NULL, NULL, 0);

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, Layout, Input, Captured, Constants, Stats, Pipe, Target, TargetView, Raster };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    if (VsBlob)
        VsBlob->Release();
    return hr;
}

static const char TcBiasSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
    "struct CO { float4 p : SV_Position; float4 c : COLOR; float2 d : SV_ClipDistance0; float e : SV_CullDistance0; };\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; return o; }\n"
    "CO vsclip(VI i)\n"
    "{\n"
    "    CO o; o.p = i.p; o.c = i.c;\n"
    "    o.d = float2(i.p.x + 0.35 * i.p.w, 0.45 * i.p.w - i.p.y + i.t.x);\n"
    "    o.e = i.t.y;\n"
    "    return o;\n"
    "}\n"
    "float4 ps(VO i) : SV_Target { return i.c; }\n";

/* Depth bias on each depth format, then clip and cull distances */
static
HRESULT
SceneBias(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const DXGI_FORMAT Formats[3] = { DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D32_FLOAT };
    static const UINT FormatBytes[3] = { 2, 4, 4 };
    static const FLOAT Biases[][3] =
    {
        { 0, 0, 0 },
        { 1, 0, 0 },
        { -1, 0, 0 },
        { 5, 0, 0 },
        { 100000, 0.001f, 0 },
        { 0, 0, 2.5f },
        { 3, 0, -1.5f },
        { -1000, -0.002f, 0 },
        { 2, 0.0001f, 7 },
    };
    std::vector<TcVertex> Vertices;
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    ID3DBlob *VsBlob = NULL;
    ID3DBlob *ClipBlob = NULL;
    ID3DBlob *PsBlob = NULL;
    ID3D11VertexShader *Vs = NULL;
    ID3D11VertexShader *ClipVs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Texture2D *Target = NULL;
    ID3D11RenderTargetView *TargetView = NULL;
    ID3D11DepthStencilState *Always = NULL;
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    HRESULT hr;

    /* A sloped triangle, a flat one, then the clip distance triangles */
    Vertices.push_back(TcMake(-0.8f, -0.8f, 0.1f, 1.0f, 0xFF0000FF));
    Vertices.push_back(TcMake(0.8f, -0.6f, 0.7f, 1.0f, 0x00FF00FF));
    Vertices.push_back(TcMake(0.0f, 0.8f, 0.4f, 1.0f, 0x0000FFFF));
    Vertices.push_back(TcMake(-0.9f, 0.5f, 0.25f, 1.0f, 0xFFFF00FF));
    Vertices.push_back(TcMake(-0.3f, 0.9f, 0.25f, 1.0f, 0xFFFF00FF));
    Vertices.push_back(TcMake(-0.9f, 0.9f, 0.25f, 1.0f, 0xFFFF00FF));
    Vertices.push_back(TcMake(-0.9f, -0.9f, 0.5f, 1.0f, 0xFF8000FF, 0.0f, 1.0f));
    Vertices.push_back(TcMake(0.9f, -0.7f, 0.5f, 2.0f, 0x80FF00FF, -0.2f, 1.0f));
    Vertices.push_back(TcMake(0.1f, 0.9f, 0.5f, 1.0f, 0x0080FFFF, 0.3f, -1.0f));
    Vertices.push_back(TcMake(0.2f, -0.2f, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, -1.0f));
    Vertices.push_back(TcMake(0.9f, -0.2f, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, -0.5f));
    Vertices.push_back(TcMake(0.5f, 0.6f, 0.5f, 1.0f, 0xFFFFFFFF, 0.0f, -2.0f));

    hr = TcCompileFrom(TcBiasSource, "vs", "vs_5_0", &VsBlob);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcBiasSource, "vsclip", "vs_5_0", &ClipBlob);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcBiasSource, "ps", "ps_5_0", &PsBlob);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(ClipBlob->GetBufferPointer(), ClipBlob->GetBufferSize(), NULL, &ClipVs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(PsBlob->GetBufferPointer(), PsBlob->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), VsBlob->GetBufferPointer(), VsBlob->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Vertices.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Vertices.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC Desc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };

        hr = Device->CreateTexture2D(&Desc, NULL, &Target);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateRenderTargetView(Target, NULL, &TargetView);
    if (SUCCEEDED(hr))
    {
        D3D11_DEPTH_STENCIL_DESC DepthDesc = {};

        DepthDesc.DepthEnable = TRUE;
        DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        DepthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
        hr = Device->CreateDepthStencilState(&DepthDesc, &Always);
    }
    if (FAILED(hr))
        goto Exit;

    Context->IASetInputLayout(Layout);
    Context->IASetVertexBuffers(0, 1, &Input, &Stride, &Offset);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->VSSetShader(Vs, NULL, 0);
    Context->PSSetShader(Ps, NULL, 0);
    Context->RSSetViewports(1, &Viewport);
    Context->OMSetDepthStencilState(Always, 0);

    for (UINT Format = 0; Format < 3 && SUCCEEDED(hr); Format++)
    {
        D3D11_TEXTURE2D_DESC DepthDesc = { 64, 64, 1, 1, Formats[Format], { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_DEPTH_STENCIL, 0, 0 };
        ID3D11Texture2D *Depth = NULL;
        ID3D11DepthStencilView *DepthView = NULL;

        hr = Device->CreateTexture2D(&DepthDesc, NULL, &Depth);
        if (SUCCEEDED(hr))
            hr = Device->CreateDepthStencilView(Depth, NULL, &DepthView);
        for (UINT Case = 0; Case < ARRAYSIZE(Biases) && SUCCEEDED(hr); Case++)
        {
            D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, (INT)Biases[Case][0], Biases[Case][1], Biases[Case][2],
                                                 TRUE, FALSE, FALSE, FALSE };
            ID3D11RasterizerState *Raster = NULL;

            hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
            if (FAILED(hr))
                break;
            Context->RSSetState(Raster);
            Context->OMSetRenderTargets(1, &TargetView, DepthView);
            Context->ClearDepthStencilView(DepthView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
            Context->Draw(6, 0);
            hr = TcReadBytes(Device, Context, Depth, FormatBytes[Format], Result);
            Raster->Release();
        }
        Context->OMSetRenderTargets(1, &TargetView, NULL);
        if (DepthView)
            DepthView->Release();
        if (Depth)
            Depth->Release();
    }

    /* Clip distances cut the triangles; the second one is culled whole */
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, FALSE, FALSE };
        ID3D11RasterizerState *Raster = NULL;

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
        if (SUCCEEDED(hr))
        {
            Context->RSSetState(Raster);
            Context->ClearRenderTargetView(TargetView, Clear);
            Context->OMSetRenderTargets(1, &TargetView, NULL);
            Context->VSSetShader(ClipVs, NULL, 0);
            Context->Draw(6, 6);
            hr = TcReadBytes(Device, Context, Target, 4, Result);
            Raster->Release();
        }
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, ClipVs, Ps, Layout, Input, Target, TargetView, Always };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    ID3DBlob *Blobs[] = { VsBlob, ClipBlob, PsBlob };
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

static const char TcOpsSource[] =
    "RWByteAddressBuffer Out : register(u0);\n"
    "Texture2D<float4> T : register(t0);\n"
    "Texture2D<float> D : register(t1);\n"
    "SamplerState S : register(s0);\n"
    "SamplerComparisonState Z : register(s1);\n"
    "cbuffer C : register(b0) { float4 F; uint4 U; int4 O; };\n"
    "void put(uint at, double v) { uint lo, hi; asuint(v, lo, hi); Out.Store2(at, uint2(lo, hi)); }\n"
    "[numthreads(8, 1, 1)]\n"
    "void cs(uint i : SV_DispatchThreadID)\n"
    "{\n"
    "    uint at = i * 256;\n"
    "    double a = (double)F.x * (i + 1) + 1.0e-10 * i;\n"
    "    double b = (double)F.y - i;\n"
    "    double c = a * b + a / b;\n"
    "    double d = fma(a, b, c);\n"
    "    double e = rcp(b);\n"
    "    double m = max(a, b) - min(a, -b);\n"
    "    put(at + 0, a); put(at + 8, b); put(at + 16, c); put(at + 24, d); put(at + 32, e); put(at + 40, m);\n"
    "    put(at + 48, (double)(int)(U.w - i * 7)); put(at + 56, (double)(U.z + i));\n"
    "    put(at + 64, abs(-c) + (double)F.z);\n"
    "    put(at + 72, (i & 1) ? a : b);\n"
    "    Out.Store4(at + 80, uint4(a < b, a >= b, a == c, a != d));\n"
    "    Out.Store4(at + 96, uint4(asuint((float)c), (uint)(int)(d * 1000.0), (uint)(e * 1.0e6), asuint((float)m)));\n"
    "    Out.Store4(at + 112, msad4(U.x ^ (i * 0x01000100), uint2(U.y + i * 77, U.z * (i + 3)), uint4(i, 2, 3, 4)));\n"
    "    int2 o = O.xy + int2(i, -i);\n"
    "    float2 uv = float2(0.1 + 0.07 * i, 0.3 + 0.05 * i);\n"
    "    Out.Store4(at + 128, asuint(T.GatherRed(S, uv, o)));\n"
    "    Out.Store4(at + 144, asuint(T.GatherGreen(S, uv, o + int2(40, -40))));\n"
    "    Out.Store4(at + 160, asuint(D.GatherCmp(Z, uv, 0.5, o)));\n"
    "    Out.Store4(at + 176, asuint(T.GatherAlpha(S, uv, O.zw)));\n"
    "}\n";

/* Double precision math, msad4 and gathers with offsets from registers, in a compute shader */
static
HRESULT
SceneOps(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    struct
    {
        FLOAT F[4];
        UINT U[4];
        INT O[4];
    } Constants = { { 3.25f, 7.5f, 0.125f, 0.0f }, { 0x00FF8040, 0x80402010, 0x10305070, 1000 }, { 1, -2, 33, -35 } };
    D3D11_BUFFER_DESC ConstantDesc = { sizeof(Constants), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    D3D11_BUFFER_DESC OutDesc = { 8 * 256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
    D3D11_UNORDERED_ACCESS_VIEW_DESC OutView = {};
    D3D11_SAMPLER_DESC SamplerDesc = {};
    D3D11_SAMPLER_DESC CompareDesc = {};
    UINT Texels[64];
    FLOAT Depths[64];
    ID3DBlob *CsBlob = NULL;
    ID3D11ComputeShader *Cs = NULL;
    ID3D11Buffer *Buffer = NULL;
    ID3D11Buffer *Out = NULL;
    ID3D11UnorderedAccessView *Uav = NULL;
    ID3D11Texture2D *Color = NULL;
    ID3D11Texture2D *Depth = NULL;
    ID3D11ShaderResourceView *Views[2] = {};
    ID3D11SamplerState *Samplers[2] = {};
    HRESULT hr;

    for (UINT Index = 0; Index < 64; Index++)
    {
        Texels[Index] = Index * 0x04030201u + 0x10203040u;
        Depths[Index] = (FLOAT)((Index * 37) % 64) / 63.0f;
    }

    hr = TcCompileFrom(TcOpsSource, "cs", "cs_5_0", &CsBlob);
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(CsBlob->GetBufferPointer(), CsBlob->GetBufferSize(), NULL, &Cs);
    if (SUCCEEDED(hr))
    {
        D3D11_SUBRESOURCE_DATA Data = { &Constants, 0, 0 };

        hr = Device->CreateBuffer(&ConstantDesc, &Data, &Buffer);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
    if (SUCCEEDED(hr))
    {
        OutView.Format = DXGI_FORMAT_R32_TYPELESS;
        OutView.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        OutView.Buffer.NumElements = OutDesc.ByteWidth / 4;
        OutView.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        hr = Device->CreateUnorderedAccessView(Out, &OutView, &Uav);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC Desc = { 8, 8, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Texels, 32, 0 };

        hr = Device->CreateTexture2D(&Desc, &Data, &Color);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_TEXTURE2D_DESC Desc = { 8, 8, 1, 1, DXGI_FORMAT_R32_FLOAT, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Depths, 32, 0 };

        hr = Device->CreateTexture2D(&Desc, &Data, &Depth);
    }
    if (SUCCEEDED(hr))
        hr = Device->CreateShaderResourceView(Color, NULL, &Views[0]);
    if (SUCCEEDED(hr))
        hr = Device->CreateShaderResourceView(Depth, NULL, &Views[1]);
    if (SUCCEEDED(hr))
    {
        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = Device->CreateSamplerState(&SamplerDesc, &Samplers[0]);
    }
    if (SUCCEEDED(hr))
    {
        CompareDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
        CompareDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        CompareDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        CompareDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        CompareDesc.ComparisonFunc = D3D11_COMPARISON_LESS;
        CompareDesc.MaxLOD = D3D11_FLOAT32_MAX;
        hr = Device->CreateSamplerState(&CompareDesc, &Samplers[1]);
    }
    if (FAILED(hr))
        goto Exit;

    Context->CSSetShader(Cs, NULL, 0);
    Context->CSSetConstantBuffers(0, 1, &Buffer);
    Context->CSSetShaderResources(0, 2, Views);
    Context->CSSetSamplers(0, 2, Samplers);
    Context->CSSetUnorderedAccessViews(0, 1, &Uav, NULL);
    Context->Dispatch(1, 1, 1);
    hr = TcReadBuffer(Device, Context, Out, Result);
    {
        ID3D11UnorderedAccessView *None = NULL;

        Context->CSSetUnorderedAccessViews(0, 1, &None, NULL);
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Cs, Buffer, Out, Uav, Color, Depth, Views[0], Views[1], Samplers[0], Samplers[1] };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    if (CsBlob)
        CsBlob->Release();
    return hr;
}

static const char TcMsaaSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; centroid float2 t : TEXCOORD; };\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n"
    "float4 ps(VO i, uint m : SV_Coverage) : SV_Target { return float4(i.c.rg, i.t.x, (countbits(m) + 1) / 17.0); }\n"
    "struct PO { float4 c : SV_Target; uint m : SV_Coverage; };\n"
    "PO psmask(VO i, uint m : SV_Coverage) { PO o; o.c = float4(i.t, i.c.b, 1); o.m = (m & 0x5555) | (uint)(i.t.y * 4) << 1; return o; }\n"
    "Texture2DMS<float4> T : register(t0);\n"
    "Texture2DMS<float> D : register(t1);\n"
    "RWByteAddressBuffer Out : register(u0);\n"
    "cbuffer C : register(b0) { uint Samples; };\n"
    "[numthreads(8, 8, 1)]\n"
    "void cs(uint3 i : SV_DispatchThreadID)\n"
    "{\n"
    "    uint at = ((i.y * 64 + i.x) * Samples) * 20;\n"
    "    for (uint s = 0; s < Samples; s++)\n"
    "    {\n"
    "        Out.Store4(at + s * 20, asuint(T.Load(i.xy, s)));\n"
    "        Out.Store(at + s * 20 + 16, asuint(D.Load(i.xy, s)));\n"
    "    }\n"
    "    if (i.x == 0 && i.y == 0)\n"
    "    {\n"
    "        uint w, h, n;\n"
    "        T.GetDimensions(w, h, n);\n"
    "        Out.Store(64 * 64 * Samples * 20, n);\n"
    "        for (uint s = 0; s < 17; s++)\n"
    "            Out.Store2(64 * 64 * Samples * 20 + 4 + s * 8, asuint(T.GetSamplePosition(s)));\n"
    "    }\n"
    "}\n";

/* Multisampled triangles with centroid inputs, coverage in and out, a sample mask and depth, read back sample by sample and resolved */
static
HRESULT
SceneMsaa(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const UINT Counts[4] = { 2, 4, 8, 16 };
    std::vector<TcVertex> Vertices;
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = { 0.1f, 0.2f, 0.3f, 1.0f };
    ID3DBlob *Blobs[4] = {};
    ID3D11VertexShader *Vs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11PixelShader *PsMask = NULL;
    ID3D11ComputeShader *Cs = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Constants = NULL;
    ID3D11DepthStencilState *Depth = NULL;
    ID3D11RasterizerState *Raster = NULL;
    ID3D11BlendState *Blend = NULL;
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    HRESULT hr;

    Vertices.push_back(TcMake(-0.93f, -0.81f, 0.2f, 1.0f, 0xFF2000FF, 0.0f, 0.0f));
    Vertices.push_back(TcMake(0.87f, -0.62f, 0.8f, 1.0f, 0x20FF00FF, 1.0f, 0.3f));
    Vertices.push_back(TcMake(-0.11f, 0.91f, 0.5f, 1.0f, 0x0040FFFF, 0.5f, 1.0f));
    Vertices.push_back(TcMake(-0.95f, 0.33f, 0.9f, 1.0f, 0xFFFF00FF, 0.2f, 0.9f));
    Vertices.push_back(TcMake(0.91f, 0.47f, 0.1f, 1.0f, 0x00FFFFFF, 0.8f, 0.1f));
    Vertices.push_back(TcMake(0.23f, -0.93f, 0.45f, 2.0f, 0xFF00FFFF, 0.4f, 0.6f));
    Vertices.push_back(TcMake(-0.47f, -0.27f, 0.05f, 1.0f, 0xFFFFFFFF, 0.1f, 0.2f));
    Vertices.push_back(TcMake(0.53f, -0.11f, 0.05f, 1.0f, 0x808080FF, 0.9f, 0.7f));
    Vertices.push_back(TcMake(0.07f, 0.61f, 0.05f, 1.0f, 0x404040FF, 0.3f, 0.95f));

    hr = TcCompileFrom(TcMsaaSource, "vs", "vs_5_0", &Blobs[0]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcMsaaSource, "ps", "ps_5_0", &Blobs[1]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcMsaaSource, "psmask", "ps_5_0", &Blobs[2]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcMsaaSource, "cs", "cs_5_0", &Blobs[3]);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(Blobs[1]->GetBufferPointer(), Blobs[1]->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(Blobs[2]->GetBufferPointer(), Blobs[2]->GetBufferSize(), NULL, &PsMask);
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(Blobs[3]->GetBufferPointer(), Blobs[3]->GetBufferSize(), NULL, &Cs);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Vertices.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Vertices.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC ConstantDesc = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        hr = Device->CreateBuffer(&ConstantDesc, NULL, &Constants);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_DEPTH_STENCIL_DESC DepthDesc = {};

        DepthDesc.DepthEnable = TRUE;
        DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        DepthDesc.DepthFunc = D3D11_COMPARISON_LESS;
        hr = Device->CreateDepthStencilState(&DepthDesc, &Depth);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, TRUE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BLEND_DESC BlendDesc = {};

        BlendDesc.RenderTarget[0].BlendEnable = TRUE;
        BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = Device->CreateBlendState(&BlendDesc, &Blend);
    }
    if (FAILED(hr))
        goto Exit;

    for (UINT Count : Counts)
    {
        D3D11_TEXTURE2D_DESC ColorDesc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { Count, 0 }, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_TEXTURE2D_DESC DepthDesc = { 64, 64, 1, 1, DXGI_FORMAT_R32_TYPELESS, { Count, 0 }, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_TEXTURE2D_DESC ResolvedDesc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0 };
        D3D11_DEPTH_STENCIL_VIEW_DESC DsvDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC DepthSrv = {};
        UINT Bytes = 64 * 64 * Count * 20 + 4 + 17 * 8;
        D3D11_BUFFER_DESC OutDesc = { (Bytes + 15) & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
        D3D11_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        UINT Levels = 0;
        UINT CountData[4] = { Count, 0, 0, 0 };
        ID3D11Texture2D *Color = NULL;
        ID3D11Texture2D *DepthTexture = NULL;
        ID3D11Texture2D *Resolved = NULL;
        ID3D11RenderTargetView *ColorView = NULL;
        ID3D11DepthStencilView *DepthView = NULL;
        ID3D11ShaderResourceView *Views[2] = {};
        ID3D11Buffer *Out = NULL;
        ID3D11UnorderedAccessView *Uav = NULL;

        if (FAILED(Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, Count, &Levels)) || Levels == 0)
            continue;

        DsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        DsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        DepthSrv.Format = DXGI_FORMAT_R32_FLOAT;
        DepthSrv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
        UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        UavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UavDesc.Buffer.NumElements = OutDesc.ByteWidth / 4;
        UavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

        hr = Device->CreateTexture2D(&ColorDesc, NULL, &Color);
        if (SUCCEEDED(hr))
            hr = Device->CreateTexture2D(&DepthDesc, NULL, &DepthTexture);
        if (SUCCEEDED(hr))
            hr = Device->CreateTexture2D(&ResolvedDesc, NULL, &Resolved);
        if (SUCCEEDED(hr))
            hr = Device->CreateRenderTargetView(Color, NULL, &ColorView);
        if (SUCCEEDED(hr))
            hr = Device->CreateDepthStencilView(DepthTexture, &DsvDesc, &DepthView);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(Color, NULL, &Views[0]);
        if (SUCCEEDED(hr))
            hr = Device->CreateShaderResourceView(DepthTexture, &DepthSrv, &Views[1]);
        if (SUCCEEDED(hr))
            hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
        if (SUCCEEDED(hr))
            hr = Device->CreateUnorderedAccessView(Out, &UavDesc, &Uav);

        if (SUCCEEDED(hr))
        {
            ID3D11ShaderResourceView *NoViews[2] = {};
            ID3D11UnorderedAccessView *NoUav = NULL;
            ID3D11RenderTargetView *NoTarget = NULL;

            Context->ClearRenderTargetView(ColorView, Clear);
            Context->ClearDepthStencilView(DepthView, D3D11_CLEAR_DEPTH, 1.0f, 0);
            Context->OMSetRenderTargets(1, &ColorView, DepthView);
            Context->OMSetDepthStencilState(Depth, 0);
            Context->OMSetBlendState(Blend, NULL, 0xFFFFFFFF);
            Context->RSSetViewports(1, &Viewport);
            Context->RSSetState(Raster);
            Context->IASetInputLayout(Layout);
            Context->IASetVertexBuffers(0, 1, &Input, &Stride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->VSSetShader(Vs, NULL, 0);
            Context->PSSetShader(Ps, NULL, 0);
            Context->Draw(6, 0);

            /* The last triangle writes its own coverage under a sample mask */
            Context->OMSetBlendState(NULL, NULL, 0xFFFFFFF7);
            Context->PSSetShader(PsMask, NULL, 0);
            Context->Draw(3, 6);
            Context->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
            Context->OMSetRenderTargets(1, &NoTarget, NULL);

            Context->UpdateSubresource(Constants, 0, NULL, CountData, 0, 0);
            Context->CSSetShader(Cs, NULL, 0);
            Context->CSSetConstantBuffers(0, 1, &Constants);
            Context->CSSetShaderResources(0, 2, Views);
            Context->CSSetUnorderedAccessViews(0, 1, &Uav, NULL);
            Context->Dispatch(8, 8, 1);
            Context->CSSetShaderResources(0, 2, NoViews);
            Context->CSSetUnorderedAccessViews(0, 1, &NoUav, NULL);
            hr = TcReadBuffer(Device, Context, Out, Result, Bytes);

            if (SUCCEEDED(hr))
            {
                Context->ResolveSubresource(Resolved, 0, Color, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                hr = TcReadBytes(Device, Context, Resolved, 4, Result);
            }
        }

        ID3D11DeviceChild *Objects[] = { Color, DepthTexture, Resolved, ColorView, DepthView, Views[0], Views[1], Out, Uav };
        for (ID3D11DeviceChild *Object : Objects)
        {
            if (Object)
                Object->Release();
        }
        if (FAILED(hr))
            break;
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, Ps, PsMask, Cs, Layout, Input, Constants, Depth, Raster, Blend };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

static const char TcCoverageSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "float4 vs(VI i) : SV_Position { return i.p; }\n"
    "cbuffer C : register(b0) { uint Samples; float Scale; float Bias; };\n"
    "float4 ps(float4 p : SV_Position) : SV_Target { return float4(1, 0.5, 0.25, (floor(p.x) + floor(p.y) * 64 + 0.5) * Scale + Bias); }\n"
    "Texture2DMS<float4> T : register(t0);\n"
    "RWByteAddressBuffer Out : register(u0);\n"
    "[numthreads(8, 8, 1)]\n"
    "void cs(uint3 i : SV_DispatchThreadID)\n"
    "{\n"
    "    uint mask = 0;\n"
    "    for (uint s = 0; s < Samples; s++)\n"
    "        mask |= (T.Load(i.xy, s).x > 0.5) ? (1u << s) : 0;\n"
    "    Out.Store((i.y * 64 + i.x) * 4, mask);\n"
    "}\n";

/* Alpha to coverage: which samples each alpha keeps, at every sample count and dither position */
static
HRESULT
SceneAlphaCoverage(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const UINT Counts[5] = { 1, 2, 4, 8, 16 };
    static const FLOAT Ranges[2][2] = { { 1.0f / 4096.0f, 0.0f }, { 1.0f / (4096.0f * 16.0f), 0.40625f } };
    const TcVertex Vertices[] =
    {
        TcMake(-1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
        TcMake(1.0f, 1.0f, 0.5f, 1.0f, 0), TcMake(1.0f, -1.0f, 0.5f, 1.0f, 0), TcMake(-1.0f, -1.0f, 0.5f, 1.0f, 0),
    };
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = {};
    ID3DBlob *Blobs[3] = {};
    ID3D11VertexShader *Vs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11ComputeShader *Cs = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Constants = NULL;
    ID3D11BlendState *Blend = NULL;
    ID3D11RasterizerState *Raster = NULL;
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    HRESULT hr;

    hr = TcCompileFrom(TcCoverageSource, "vs", "vs_5_0", &Blobs[0]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcCoverageSource, "ps", "ps_5_0", &Blobs[1]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcCoverageSource, "cs", "cs_5_0", &Blobs[2]);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(Blobs[1]->GetBufferPointer(), Blobs[1]->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(Blobs[2]->GetBufferPointer(), Blobs[2]->GetBufferSize(), NULL, &Cs);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { sizeof(Vertices), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Vertices, 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC ConstantDesc = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        hr = Device->CreateBuffer(&ConstantDesc, NULL, &Constants);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BLEND_DESC BlendDesc = {};

        BlendDesc.AlphaToCoverageEnable = TRUE;
        BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = Device->CreateBlendState(&BlendDesc, &Blend);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, TRUE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (FAILED(hr))
        goto Exit;

    for (UINT Count : Counts)
    {
        D3D11_TEXTURE2D_DESC ColorDesc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { Count, 0 }, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_BUFFER_DESC OutDesc = { 64 * 64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
        D3D11_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
        UINT Levels = 0;
        ID3D11Texture2D *Color = NULL;
        ID3D11RenderTargetView *ColorView = NULL;
        ID3D11ShaderResourceView *View = NULL;
        ID3D11Buffer *Out = NULL;
        ID3D11UnorderedAccessView *Uav = NULL;

        if (Count > 1 && (FAILED(Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, Count, &Levels)) || Levels == 0))
            continue;
        UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        UavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UavDesc.Buffer.NumElements = OutDesc.ByteWidth / 4;
        UavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;

        hr = Device->CreateTexture2D(&ColorDesc, NULL, &Color);
        if (SUCCEEDED(hr))
            hr = Device->CreateRenderTargetView(Color, NULL, &ColorView);
        if (SUCCEEDED(hr) && Count > 1)
            hr = Device->CreateShaderResourceView(Color, &SrvDesc, &View);
        if (SUCCEEDED(hr))
            hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
        if (SUCCEEDED(hr))
            hr = Device->CreateUnorderedAccessView(Out, &UavDesc, &Uav);

        /* Every pixel a different alpha, over the whole range and then finely around one value */
        for (UINT Range = 0; Range < 2 && SUCCEEDED(hr); Range++)
        {
            struct { UINT Samples; FLOAT Scale; FLOAT Bias; UINT Pad; } Data = { Count, Ranges[Range][0], Ranges[Range][1], 0 };
            ID3D11ShaderResourceView *NoView = NULL;
            ID3D11UnorderedAccessView *NoUav = NULL;
            ID3D11RenderTargetView *NoTarget = NULL;

            Context->UpdateSubresource(Constants, 0, NULL, &Data, 0, 0);
            Context->ClearRenderTargetView(ColorView, Clear);
            Context->OMSetRenderTargets(1, &ColorView, NULL);
            Context->OMSetBlendState(Blend, NULL, 0xFFFFFFFF);
            Context->OMSetDepthStencilState(NULL, 0);
            Context->RSSetViewports(1, &Viewport);
            Context->RSSetState(Raster);
            Context->IASetInputLayout(Layout);
            Context->IASetVertexBuffers(0, 1, &Input, &Stride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->VSSetShader(Vs, NULL, 0);
            Context->PSSetShader(Ps, NULL, 0);
            Context->PSSetConstantBuffers(0, 1, &Constants);
            Context->Draw(6, 0);
            Context->OMSetRenderTargets(1, &NoTarget, NULL);
            Context->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
            if (Count == 1)
            {
                hr = TcReadBytes(Device, Context, Color, 4, Result);
                continue;
            }

            Context->CSSetShader(Cs, NULL, 0);
            Context->CSSetConstantBuffers(0, 1, &Constants);
            Context->CSSetShaderResources(0, 1, &View);
            Context->CSSetUnorderedAccessViews(0, 1, &Uav, NULL);
            Context->Dispatch(8, 8, 1);
            Context->CSSetShaderResources(0, 1, &NoView);
            Context->CSSetUnorderedAccessViews(0, 1, &NoUav, NULL);
            hr = TcReadBuffer(Device, Context, Out, Result);
        }

        ID3D11DeviceChild *Objects[] = { Color, ColorView, View, Out, Uav };
        for (ID3D11DeviceChild *Object : Objects)
        {
            if (Object)
                Object->Release();
        }
        if (FAILED(hr))
            break;
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, Ps, Cs, Layout, Input, Constants, Blend, Raster };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

static const char TcLineModeSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; return o; }\n"
    "float4 ps(VO i) : SV_Target { return i.c; }\n"
    "cbuffer C : register(b0) { uint Samples; };\n"
    "Texture2DMS<float4> T : register(t0);\n"
    "RWByteAddressBuffer Out : register(u0);\n"
    "[numthreads(8, 8, 1)]\n"
    "void cs(uint3 i : SV_DispatchThreadID)\n"
    "{\n"
    "    for (uint s = 0; s < Samples; s++)\n"
    "    {\n"
    "        uint4 v = (uint4)round(saturate(T.Load(i.xy, s)) * 255);\n"
    "        Out.Store(((i.y * 64 + i.x) * Samples + s) * 4, v.x | (v.y << 8) | (v.z << 16) | (v.w << 24));\n"
    "    }\n"
    "}\n";

/* Lines under every multisample and antialiased line setting, read back per sample */
static
HRESULT
SceneLineModes(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const UINT Counts[3] = { 1, 4, 8 };
    std::vector<TcVertex> Vertices;
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = {};
    ID3DBlob *Blobs[3] = {};
    ID3D11VertexShader *Vs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11ComputeShader *Cs = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Constants = NULL;
    ID3D11RasterizerState *Raster[4] = {};
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    UINT Seed = 4242;
    HRESULT hr;

    for (UINT Index = 0; Index < 24; Index++)
    {
        FLOAT Corner[4];

        for (FLOAT &Value : Corner)
        {
            Seed = Seed * 1103515245 + 12345;
            Value = ((Seed >> 8) % 2201) / 1000.0f - 1.1f;
        }
        Vertices.push_back(TcMake(Corner[0], Corner[1], 0.5f, 1.0f, 0xFF4020FF));
        Vertices.push_back(TcMake(Corner[2], Corner[3], 0.5f, 1.0f, 0x20C0FF80));
    }
    for (UINT Index = 0; Index < 8; Index++)
    {
        FLOAT A = (FLOAT)(Index * 8) / 32.0f - 1.0f;
        FLOAT B = (FLOAT)(Index * 8 + 3) / 32.0f - 1.0f + ((Index & 1) ? 1.0f / 64.0f : 0.0f);
        FLOAT C = (FLOAT)(Index * 6 + 7) / 32.0f - 1.0f;

        Vertices.push_back(TcMake(A, B, 0.5f, 1.0f, 0xFFFFFFFF));
        Vertices.push_back(TcMake(C, B, 0.5f, 1.0f, 0xFFFF00FF));
        Vertices.push_back(TcMake(B, A, 0.5f, 1.0f, 0x00FFFFFF));
        Vertices.push_back(TcMake(B, C, 0.5f, 1.0f, 0xFF00FFFF));
    }

    hr = TcCompileFrom(TcLineModeSource, "vs", "vs_5_0", &Blobs[0]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcLineModeSource, "ps", "ps_5_0", &Blobs[1]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcLineModeSource, "cs", "cs_5_0", &Blobs[2]);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(Blobs[1]->GetBufferPointer(), Blobs[1]->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(Blobs[2]->GetBufferPointer(), Blobs[2]->GetBufferSize(), NULL, &Cs);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Vertices.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Vertices.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC ConstantDesc = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        hr = Device->CreateBuffer(&ConstantDesc, NULL, &Constants);
    }
    for (UINT Mode = 0; Mode < 4 && SUCCEEDED(hr); Mode++)
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, (Mode & 1) != 0, (Mode & 2) != 0 };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster[Mode]);
    }
    if (FAILED(hr))
        goto Exit;

    for (UINT Count : Counts)
    {
        D3D11_TEXTURE2D_DESC ColorDesc = { 64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { Count, 0 }, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_BUFFER_DESC OutDesc = { 64 * 64 * 4 * Count, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
        D3D11_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
        UINT CountData[4] = { Count, 0, 0, 0 };
        UINT Levels = 0;
        ID3D11Texture2D *Color = NULL;
        ID3D11RenderTargetView *ColorView = NULL;
        ID3D11ShaderResourceView *View = NULL;
        ID3D11Buffer *Out = NULL;
        ID3D11UnorderedAccessView *Uav = NULL;

        if (Count > 1 && (FAILED(Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, Count, &Levels)) || Levels == 0))
            continue;
        UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        UavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UavDesc.Buffer.NumElements = OutDesc.ByteWidth / 4;
        UavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;

        hr = Device->CreateTexture2D(&ColorDesc, NULL, &Color);
        if (SUCCEEDED(hr))
            hr = Device->CreateRenderTargetView(Color, NULL, &ColorView);
        if (SUCCEEDED(hr) && Count > 1)
            hr = Device->CreateShaderResourceView(Color, &SrvDesc, &View);
        if (SUCCEEDED(hr))
            hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
        if (SUCCEEDED(hr))
            hr = Device->CreateUnorderedAccessView(Out, &UavDesc, &Uav);

        for (UINT Mode = 0; Mode < 4 && SUCCEEDED(hr); Mode++)
        {
            ID3D11ShaderResourceView *NoView = NULL;
            ID3D11UnorderedAccessView *NoUav = NULL;
            ID3D11RenderTargetView *NoTarget = NULL;

            Context->ClearRenderTargetView(ColorView, Clear);
            Context->OMSetRenderTargets(1, &ColorView, NULL);
            Context->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
            Context->OMSetDepthStencilState(NULL, 0);
            Context->RSSetViewports(1, &Viewport);
            Context->RSSetState(Raster[Mode]);
            Context->IASetInputLayout(Layout);
            Context->IASetVertexBuffers(0, 1, &Input, &Stride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            Context->VSSetShader(Vs, NULL, 0);
            Context->PSSetShader(Ps, NULL, 0);
            Context->Draw((UINT)Vertices.size(), 0);
            Context->OMSetRenderTargets(1, &NoTarget, NULL);
            if (Count == 1)
            {
                hr = TcReadBytes(Device, Context, Color, 4, Result);
                continue;
            }

            Context->UpdateSubresource(Constants, 0, NULL, CountData, 0, 0);
            Context->CSSetShader(Cs, NULL, 0);
            Context->CSSetConstantBuffers(0, 1, &Constants);
            Context->CSSetShaderResources(0, 1, &View);
            Context->CSSetUnorderedAccessViews(0, 1, &Uav, NULL);
            Context->Dispatch(8, 8, 1);
            Context->CSSetShaderResources(0, 1, &NoView);
            Context->CSSetUnorderedAccessViews(0, 1, &NoUav, NULL);
            hr = TcReadBuffer(Device, Context, Out, Result);
        }

        ID3D11DeviceChild *Objects[] = { Color, ColorView, View, Out, Uav };
        for (ID3D11DeviceChild *Object : Objects)
        {
            if (Object)
                Object->Release();
        }
        if (FAILED(hr))
            break;
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, Ps, Cs, Layout, Input, Constants, Raster[0], Raster[1], Raster[2], Raster[3] };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

static const char TcRateSource[] =
    "struct VI { float4 p : POSITION; float4 c : COLOR; float2 t : TEXCOORD; };\n"
    "struct VO { float4 p : SV_Position; float4 c : COLOR; sample float2 t : TEXCOORD; noperspective float2 n : NORMAL; };\n"
    "VO vs(VI i) { VO o; o.p = i.p; o.c = i.c; o.t = i.t; o.n = i.t * 3 - i.c.xy; return o; }\n"
    "struct PO { float4 a : SV_Target0; float4 b : SV_Target1; };\n"
    "PO ps(VO i, uint s : SV_SampleIndex)\n"
    "{\n"
    "    PO o;\n"
    "    uint n = GetRenderTargetSampleCount();\n"
    "    o.a = float4(i.t.x, EvaluateAttributeSnapped(i.c, int2(3, -5)).g, EvaluateAttributeAtSample(i.t, (s + 1) % n).y, EvaluateAttributeCentroid(i.c).b);\n"
    "    o.b = float4(EvaluateAttributeAtSample(i.t, 1).y, EvaluateAttributeAtSample(i.t, s).y, EvaluateAttributeAtSample(i.t, (uint)i.p.x % n).y,EvaluateAttributeSnapped(i.n, int2(-7, 6)).y);\n"
    "    return o;\n"
    "}\n"
    "Texture2DMS<float4> T0 : register(t0);\n"
    "Texture2DMS<float4> T1 : register(t1);\n"
    "RWByteAddressBuffer Out : register(u0);\n"
    "cbuffer C : register(b0) { uint Samples; };\n"
    "[numthreads(8, 8, 1)]\n"
    "void cs(uint3 i : SV_DispatchThreadID)\n"
    "{\n"
    "    for (uint s = 0; s < Samples; s++)\n"
    "    {\n"
    "        uint at = ((i.y * 64 + i.x) * Samples + s) * 32;\n"
    "        Out.Store4(at, asuint(T0.Load(i.xy, s)));\n"
    "        Out.Store4(at + 16, asuint(T1.Load(i.xy, s)));\n"
    "    }\n"
    "}\n";

/* Sample rate shading with inputs evaluated at sample positions, snapped offsets and the centroid, in float targets */
static
HRESULT
SceneSampleRate(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const UINT Counts[4] = { 2, 4, 8, 16 };
    std::vector<TcVertex> Vertices;
    UINT Seed = 9001;
    D3D11_VIEWPORT Viewport = { 0, 0, 64, 64, 0, 1 };
    const FLOAT Clear[4] = {};
    ID3DBlob *Blobs[3] = {};
    ID3D11VertexShader *Vs = NULL;
    ID3D11PixelShader *Ps = NULL;
    ID3D11ComputeShader *Cs = NULL;
    ID3D11InputLayout *Layout = NULL;
    ID3D11Buffer *Input = NULL;
    ID3D11Buffer *Constants = NULL;
    ID3D11RasterizerState *Raster = NULL;
    UINT Stride = sizeof(TcVertex);
    UINT Offset = 0;
    HRESULT hr;

    /* One triangle per 16 pixel cell, random corners, depth and w, so both windings and every corner order show up */
    for (UINT Cell = 0; Cell < 16; Cell++)
    {
        for (UINT Index = 0; Index < 3; Index++)
        {
            FLOAT Value[6];

            for (FLOAT &Next : Value)
            {
                Seed = Seed * 1103515245 + 12345;
                Next = (FLOAT)((Seed >> 8) % 1000) / 1000.0f;
            }
            Vertices.push_back(TcMake((FLOAT)(Cell % 4) * 0.5f - 1.0f + 0.02f + Value[0] * 0.46f,
                                      (FLOAT)(Cell / 4) * 0.5f - 1.0f + 0.02f + Value[1] * 0.46f,
                                      Value[2], 0.5f + Value[3] * 2.0f, (UINT)(Value[4] * 255.0f) << 16 | 0xFF000000 | 0x40 << 8 | 0xFF,
                                      Value[5], Value[4]));
        }
    }

    hr = TcCompileFrom(TcRateSource, "vs", "vs_5_0", &Blobs[0]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcRateSource, "ps", "ps_5_0", &Blobs[1]);
    if (SUCCEEDED(hr))
        hr = TcCompileFrom(TcRateSource, "cs", "cs_5_0", &Blobs[2]);
    if (SUCCEEDED(hr))
        hr = Device->CreateVertexShader(Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), NULL, &Vs);
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(Blobs[1]->GetBufferPointer(), Blobs[1]->GetBufferSize(), NULL, &Ps);
    if (SUCCEEDED(hr))
        hr = Device->CreateComputeShader(Blobs[2]->GetBufferPointer(), Blobs[2]->GetBufferSize(), NULL, &Cs);
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, ARRAYSIZE(Elements), Blobs[0]->GetBufferPointer(), Blobs[0]->GetBufferSize(), &Layout);
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC InputDesc = { (UINT)(Vertices.size() * sizeof(TcVertex)), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA Data = { Vertices.data(), 0, 0 };

        hr = Device->CreateBuffer(&InputDesc, &Data, &Input);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_BUFFER_DESC ConstantDesc = { 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

        hr = Device->CreateBuffer(&ConstantDesc, NULL, &Constants);
    }
    if (SUCCEEDED(hr))
    {
        D3D11_RASTERIZER_DESC RasterDesc = { D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, FALSE, TRUE, FALSE };

        hr = Device->CreateRasterizerState(&RasterDesc, &Raster);
    }
    if (FAILED(hr))
        goto Exit;

    for (UINT Count : Counts)
    {
        D3D11_TEXTURE2D_DESC ColorDesc = { 64, 64, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, { Count, 0 }, D3D11_USAGE_DEFAULT,
                                           D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
        D3D11_BUFFER_DESC OutDesc = { 64 * 64 * 32 * Count, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0 };
        D3D11_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
        UINT CountData[4] = { Count, 0, 0, 0 };
        UINT Levels = 0;
        ID3D11Texture2D *Color[2] = {};
        ID3D11RenderTargetView *ColorViews[2] = {};
        ID3D11ShaderResourceView *Views[2] = {};
        ID3D11Buffer *Out = NULL;
        ID3D11UnorderedAccessView *Uav = NULL;

        if (FAILED(Device->CheckMultisampleQualityLevels(DXGI_FORMAT_R32G32B32A32_FLOAT, Count, &Levels)) || Levels == 0)
            continue;
        UavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        UavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        UavDesc.Buffer.NumElements = OutDesc.ByteWidth / 4;
        UavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

        for (UINT Index = 0; Index < 2 && SUCCEEDED(hr); Index++)
        {
            hr = Device->CreateTexture2D(&ColorDesc, NULL, &Color[Index]);
            if (SUCCEEDED(hr))
                hr = Device->CreateRenderTargetView(Color[Index], NULL, &ColorViews[Index]);
            if (SUCCEEDED(hr))
                hr = Device->CreateShaderResourceView(Color[Index], NULL, &Views[Index]);
        }
        if (SUCCEEDED(hr))
            hr = Device->CreateBuffer(&OutDesc, NULL, &Out);
        if (SUCCEEDED(hr))
            hr = Device->CreateUnorderedAccessView(Out, &UavDesc, &Uav);

        if (SUCCEEDED(hr))
        {
            ID3D11ShaderResourceView *NoViews[2] = {};
            ID3D11UnorderedAccessView *NoUav = NULL;
            ID3D11RenderTargetView *NoTargets[2] = {};

            Context->ClearRenderTargetView(ColorViews[0], Clear);
            Context->ClearRenderTargetView(ColorViews[1], Clear);
            Context->OMSetRenderTargets(2, ColorViews, NULL);
            Context->OMSetBlendState(NULL, NULL, 0xFFFFFFFF);
            Context->OMSetDepthStencilState(NULL, 0);
            Context->RSSetViewports(1, &Viewport);
            Context->RSSetState(Raster);
            Context->IASetInputLayout(Layout);
            Context->IASetVertexBuffers(0, 1, &Input, &Stride, &Offset);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->VSSetShader(Vs, NULL, 0);
            Context->PSSetShader(Ps, NULL, 0);
            Context->Draw((UINT)Vertices.size(), 0);
            Context->OMSetRenderTargets(2, NoTargets, NULL);

            Context->UpdateSubresource(Constants, 0, NULL, CountData, 0, 0);
            Context->CSSetShader(Cs, NULL, 0);
            Context->CSSetConstantBuffers(0, 1, &Constants);
            Context->CSSetShaderResources(0, 2, Views);
            Context->CSSetUnorderedAccessViews(0, 1, &Uav, NULL);
            Context->Dispatch(8, 8, 1);
            Context->CSSetShaderResources(0, 2, NoViews);
            Context->CSSetUnorderedAccessViews(0, 1, &NoUav, NULL);
            hr = TcReadBuffer(Device, Context, Out, Result);
        }

        ID3D11DeviceChild *Objects[] = { Color[0], Color[1], ColorViews[0], ColorViews[1], Views[0], Views[1], Out, Uav };
        for (ID3D11DeviceChild *Object : Objects)
        {
            if (Object)
                Object->Release();
        }
        if (FAILED(hr))
            break;
    }

Exit:
    ID3D11DeviceChild *Objects[] = { Vs, Ps, Cs, Layout, Input, Constants, Raster };
    for (ID3D11DeviceChild *Object : Objects)
    {
        if (Object)
            Object->Release();
    }
    for (ID3DBlob *Blob : Blobs)
    {
        if (Blob)
            Blob->Release();
    }
    return hr;
}

/* A test pattern in any of the presented formats, one texel at a time */
static
VOID
TcPresentTexel(
    _In_ DXGI_FORMAT Format,
    _In_ UINT X,
    _In_ UINT Y,
    _Out_writes_bytes_(8) BYTE *Texel)
{
    FLOAT Color[4] = { (FLOAT)X / 63.0f, (FLOAT)Y / 63.0f, (FLOAT)((X * 7 + Y * 13) % 64) / 63.0f, 1.0f };

    switch (Format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            Texel[0] = (BYTE)(Color[2] * 255.0f + 0.5f);
            Texel[1] = (BYTE)(Color[1] * 255.0f + 0.5f);
            Texel[2] = (BYTE)(Color[0] * 255.0f + 0.5f);
            Texel[3] = 255;
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            for (UINT Index = 0; Index < 4; Index++)
                Texel[Index] = (BYTE)(Color[Index] * 255.0f + 0.5f);
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        {
            UINT Packed = (UINT)(Color[0] * 1023.0f + 0.5f) | (UINT)(Color[1] * 1023.0f + 0.5f) << 10 |
                          (UINT)(Color[2] * 1023.0f + 0.5f) << 20 | 3u << 30;

            memcpy(Texel, &Packed, 4);
            break;
        }
        default:
            for (UINT Index = 0; Index < 4; Index++)
            {
                /* Halves of k / 63 with k < 64 are exact enough to build by hand: sign 0, then exponent and mantissa */
                FLOAT Value = Color[Index];
                UINT Bits;
                USHORT Half;

                memcpy(&Bits, &Value, 4);
                Half = (Value == 0.0f) ? 0 : (USHORT)((((Bits >> 23) & 0xFF) - 112) << 10 | ((Bits >> 13) & 0x3FF));
                memcpy(Texel + Index * 2, &Half, 2);
            }
            break;
    }
}

/* Swap chains on a window, read back from the window through GDI: format conversion and stretching on present */
static
HRESULT
ScenePresent(
    _In_ ID3D11Device *Device,
    _In_ ID3D11DeviceContext *Context,
    _Inout_ TcResult *Result)
{
    static const struct { DXGI_FORMAT Format; UINT Width; UINT Height; UINT Bytes; } Cases[] =
    {
        { DXGI_FORMAT_B8G8R8A8_UNORM, 64, 64, 4 },
        { DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 4 },
        { DXGI_FORMAT_R10G10B10A2_UNORM, 64, 64, 4 },
        { DXGI_FORMAT_R16G16B16A16_FLOAT, 64, 64, 8 },
        { DXGI_FORMAT_B8G8R8A8_UNORM, 40, 24, 4 },
        { DXGI_FORMAT_R8G8B8A8_UNORM, 96, 80, 4 },
    };
    WNDCLASSW Class = {};
    IDXGIDevice *DxgiDevice = NULL;
    IDXGIAdapter *Adapter = NULL;
    IDXGIFactory *Factory = NULL;
    HWND Window = NULL;
    HRESULT hr;

    UNREFERENCED_PARAMETER(Context);
    Class.lpfnWndProc = DefWindowProcW;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"WarpCheckPresent";
    RegisterClassW(&Class);
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, Class.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 0, 0, 64, 64,
                             NULL, NULL, Class.hInstance, NULL);
    if (Window == NULL)
        return HRESULT_FROM_WIN32(GetLastError());

    hr = Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice));
    if (SUCCEEDED(hr))
        hr = DxgiDevice->GetAdapter(&Adapter);
    if (SUCCEEDED(hr))
        hr = Adapter->GetParent(IID_PPV_ARGS(&Factory));

    for (UINT Case = 0; Case < ARRAYSIZE(Cases) && SUCCEEDED(hr); Case++)
    {
        DXGI_SWAP_CHAIN_DESC Desc = {};
        IDXGISwapChain *Chain = NULL;
        ID3D11Texture2D *Back = NULL;
        std::vector<BYTE> Pattern(Cases[Case].Width * Cases[Case].Height * Cases[Case].Bytes);
        BITMAPINFO Info = {};
        VOID *Bits = NULL;
        HBITMAP Bitmap = NULL;
        HDC WindowDc = NULL;
        HDC MemoryDc = NULL;
        MSG Message;

        Desc.BufferDesc.Width = Cases[Case].Width;
        Desc.BufferDesc.Height = Cases[Case].Height;
        Desc.BufferDesc.Format = Cases[Case].Format;
        Desc.SampleDesc.Count = 1;
        Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Desc.BufferCount = 1;
        Desc.OutputWindow = Window;
        Desc.Windowed = TRUE;
        Desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        hr = Factory->CreateSwapChain(Device, &Desc, &Chain);
        if (SUCCEEDED(hr))
            hr = Chain->GetBuffer(0, IID_PPV_ARGS(&Back));
        if (FAILED(hr))
        {
            printf("present case %u: %08lx\n", Case, hr);
            if (Chain)
                Chain->Release();
            break;
        }

        for (UINT Y = 0; Y < Cases[Case].Height; Y++)
        {
            for (UINT X = 0; X < Cases[Case].Width; X++)
                TcPresentTexel(Cases[Case].Format, X * 63 / (Cases[Case].Width - 1), Y * 63 / (Cases[Case].Height - 1),
                               &Pattern[(Y * Cases[Case].Width + X) * Cases[Case].Bytes]);
        }
        Context->UpdateSubresource(Back, 0, NULL, Pattern.data(), Cases[Case].Width * Cases[Case].Bytes, 0);
        hr = Chain->Present(0, 0);
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
            DispatchMessageW(&Message);

        Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
        Info.bmiHeader.biWidth = 64;
        Info.bmiHeader.biHeight = -64;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        WindowDc = GetDC(Window);
        MemoryDc = CreateCompatibleDC(WindowDc);
        Bitmap = CreateDIBSection(MemoryDc, &Info, DIB_RGB_COLORS, &Bits, NULL, 0);
        if (Bitmap != NULL)
        {
            HGDIOBJ Previous = SelectObject(MemoryDc, Bitmap);

            BitBlt(MemoryDc, 0, 0, 64, 64, WindowDc, 0, 0, SRCCOPY);
            GdiFlush();
            for (UINT Index = 0; Index < 64 * 64; Index++)
            {
                BYTE *Pixel = (BYTE *)Bits + Index * 4;

                Result->Data.insert(Result->Data.end(), Pixel, Pixel + 3);
            }
            SelectObject(MemoryDc, Previous);
            DeleteObject(Bitmap);
        }
        DeleteDC(MemoryDc);
        ReleaseDC(Window, WindowDc);
        Back->Release();
        Chain->Release();
    }

    if (Factory)
        Factory->Release();
    if (Adapter)
        Adapter->Release();
    if (DxgiDevice)
        DxgiDevice->Release();
    DestroyWindow(Window);
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
    { "geometry", 0, SceneGeometry },
    { "tess", 0, SceneTessellation },
    { "bias", 0, SceneBias },
    { "ops", 0, SceneOps },
    { "msaa", 0, SceneMsaa },
    { "a2c", 0, SceneAlphaCoverage },
    { "linemodes", 0, SceneLineModes },
    { "rate", 0, SceneSampleRate },
    { "present", 0, ScenePresent },
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
