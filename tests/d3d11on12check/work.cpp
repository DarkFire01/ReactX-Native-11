/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The translation work of Direct3D 11 on 12: resources, views, shaders, draws and more
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 *
 * Each check records the D3D12 calls of one step and flushes inside the recording, so the trace holds
 * the command list the step produced. GPU waits and readbacks stay outside the recordings because
 * whether they block depends on timing.
 */

#include "check.hpp"
#include "trace12.hpp"

#include <d3dcompiler.h>

static const char OcShaderSource[] =
    "cbuffer Tint : register(b0) { float4 Scale; };\n"
    "struct VsIn { float2 Pos : POSITION; float4 Color : COLOR; };\n"
    "struct VsOut { float4 Pos : SV_Position; float4 Color : COLOR; };\n"
    "VsOut vs(VsIn In) { VsOut Out; Out.Pos = float4(In.Pos, 0, 1); Out.Color = In.Color; return Out; }\n"
    "float4 ps(VsOut In) : SV_Target { return In.Color * Scale; }\n"
    "[maxvertexcount(3)]\n"
    "void gs(triangle VsOut In[3], inout TriangleStream<VsOut> Stream)\n"
    "{ for (uint i = 0; i < 3; i++) Stream.Append(In[i]); }\n"
    "RWStructuredBuffer<uint> Result : register(u0);\n"
    "[numthreads(8, 1, 1)]\n"
    "void cs(uint3 Id : SV_DispatchThreadID) { Result[Id.x] = Id.x * 3 + 1; }\n";

struct OcVertex
{
    float Position[2];
    float Color[4];
};

/* A triangle that covers the whole target, with a color per corner */
static const OcVertex OcFullTriangle[] =
{
    { { -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
    { { -1.0f, 3.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
    { { 3.0f, -1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
};

/* The objects a drawing check needs */
struct OcScene
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> Vertices;
    ComPtr<ID3D11Buffer> Constants;
    ComPtr<ID3D11Texture2D> Target;
    ComPtr<ID3D11RenderTargetView> TargetView;
};

static
HRESULT
OcCompile(
    _In_z_ PCSTR Entry,
    _In_z_ PCSTR Profile,
    _Outptr_ ID3DBlob** Blob)
{
    ComPtr<ID3DBlob> Errors;

    return D3DCompile(OcShaderSource, sizeof(OcShaderSource) - 1, "d3d11on12check", nullptr, nullptr, Entry,
                      Profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, Blob, &Errors);
}

/* A fresh device with no internal worker threads, so the trace order is the call order */
static
HRESULT
OcOpen(
    _In_ OnCheck& Check,
    _Outptr_ ID3D11Device** Device,
    _Outptr_ ID3D11DeviceContext** Context)
{
    HRESULT hr;

    Tr12Reset();
    hr = Check.Make(D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS, Device);
    Check.Report("open", "0x%08lX", hr);
    if (FAILED(hr))
        return hr;

    (*Device)->GetImmediateContext(Context);
    return S_OK;
}

/* Waits until the GPU finished all earlier work, so upload space is reused the same way every run */
static
VOID
OcIdle(
    _In_ ID3D11DeviceContext* Context)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11Query> Event;
    D3D11_QUERY_DESC Desc = { D3D11_QUERY_EVENT, 0 };
    BOOL Done = FALSE;

    Context->GetDevice(&Device);
    if (FAILED(Device->CreateQuery(&Desc, &Event)))
        return;

    Context->End(Event.Get());
    while (Context->GetData(Event.Get(), &Done, sizeof(Done), 0) == S_FALSE)
        Sleep(0);
}

static
VOID
OcBegin(
    _In_ ID3D11DeviceContext* Context)
{
    OcIdle(Context);
    Tr12Begin();
}

/* Flushes inside the recording and prints what the step sent to D3D12 */
static
VOID
OcEnd(
    _In_ OnCheck& Check,
    _In_ ID3D11DeviceContext* Context,
    _In_z_ PCSTR Key)
{
    Context->Flush();
    OcPrintTrace(Check, Key, Tr12End());
}

static
UINT
OcHash(
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);
    UINT Hash = 2166136261u;

    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 16777619u;
    }

    return Hash;
}

/* Copies a 2D subresource to a staging texture and reports a hash and a few texels */
static
VOID
OcReportTexture(
    _In_ OnCheck& Check,
    _In_z_ PCSTR Key,
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Texture2D* Texture)
{
    ComPtr<ID3D11Texture2D> Staging;
    D3D11_TEXTURE2D_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    std::vector<UINT> Texels;
    HRESULT hr;

    Texture->GetDesc(&Desc);
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
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

    Context->CopySubresourceRegion(Staging.Get(), 0, 0, 0, 0, Texture, 0, nullptr);
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
    {
        Check.Report(Key, "map 0x%08lX", hr);
        return;
    }

    for (UINT Row = 0; Row < Desc.Height; Row++)
    {
        const UINT* Line = reinterpret_cast<const UINT*>(static_cast<const BYTE*>(Mapped.pData) +
                                                         Row * Mapped.RowPitch);

        Texels.insert(Texels.end(), Line, Line + Desc.Width);
    }
    Context->Unmap(Staging.Get(), 0);

    Check.Report(Key, "hash 0x%08X first 0x%08X middle 0x%08X last 0x%08X", OcHash(Texels.data(),
                 Texels.size() * sizeof(UINT)), Texels.front(), Texels[Texels.size() / 2 + Desc.Width / 2],
                 Texels.back());
}

/* Copies a buffer to a staging buffer and reports its first words */
static
VOID
OcReportBuffer(
    _In_ OnCheck& Check,
    _In_z_ PCSTR Key,
    _In_ ID3D11Device* Device,
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Buffer* Buffer)
{
    ComPtr<ID3D11Buffer> Staging;
    D3D11_BUFFER_DESC Desc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    const UINT* Words;
    HRESULT hr;

    Buffer->GetDesc(&Desc);
    Desc.Usage = D3D11_USAGE_STAGING;
    Desc.BindFlags = 0;
    Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Desc.MiscFlags = 0;
    Desc.StructureByteStride = 0;
    hr = Device->CreateBuffer(&Desc, nullptr, &Staging);
    if (FAILED(hr))
    {
        Check.Report(Key, "staging 0x%08lX", hr);
        return;
    }

    Context->CopyResource(Staging.Get(), Buffer);
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
    {
        Check.Report(Key, "map 0x%08lX", hr);
        return;
    }

    Words = static_cast<const UINT*>(Mapped.pData);
    Check.Report(Key, "hash 0x%08X words %u %u %u %u", OcHash(Mapped.pData, Desc.ByteWidth), Words[0], Words[1],
                 Words[2], Words[7]);
    Context->Unmap(Staging.Get(), 0);
}

static
D3D11_TEXTURE2D_DESC
OcTextureDesc(
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT Format,
    _In_ UINT BindFlags)
{
    D3D11_TEXTURE2D_DESC Desc = {};

    Desc.Width = Width;
    Desc.Height = Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;
    return Desc;
}

static
D3D11_BUFFER_DESC
OcBufferDesc(
    _In_ UINT Size,
    _In_ D3D11_USAGE Usage,
    _In_ UINT BindFlags,
    _In_ UINT CpuFlags)
{
    D3D11_BUFFER_DESC Desc = {};

    Desc.ByteWidth = Size;
    Desc.Usage = Usage;
    Desc.BindFlags = BindFlags;
    Desc.CPUAccessFlags = CpuFlags;
    return Desc;
}

/* Shaders, input layout, buffers and a 16x16 target for the drawing checks */
static
HRESULT
OcBuildScene(
    _In_ OnCheck& Check,
    _Inout_ OcScene& Scene)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const float Scale[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    ComPtr<ID3DBlob> VsCode;
    ComPtr<ID3DBlob> PsCode;
    D3D11_TEXTURE2D_DESC TargetDesc;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_SUBRESOURCE_DATA Data = {};
    HRESULT hr;

    hr = OcOpen(Check, &Scene.Device, &Scene.Context);
    if (FAILED(hr))
        return hr;

    if (FAILED(OcCompile("vs", "vs_5_0", &VsCode)) || FAILED(OcCompile("ps", "ps_5_0", &PsCode)))
    {
        Check.Report("compile", "failed");
        return E_FAIL;
    }

    hr = Scene.Device->CreateVertexShader(VsCode->GetBufferPointer(), VsCode->GetBufferSize(), nullptr,
                                          &Scene.VertexShader);
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreatePixelShader(PsCode->GetBufferPointer(), PsCode->GetBufferSize(), nullptr,
                                             &Scene.PixelShader);
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreateInputLayout(Elements, _countof(Elements), VsCode->GetBufferPointer(),
                                             VsCode->GetBufferSize(), &Scene.Layout);

    BufferDesc = OcBufferDesc(sizeof(OcFullTriangle), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0);
    Data.pSysMem = OcFullTriangle;
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreateBuffer(&BufferDesc, &Data, &Scene.Vertices);

    BufferDesc = OcBufferDesc(16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0);
    Data.pSysMem = Scale;
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreateBuffer(&BufferDesc, &Data, &Scene.Constants);

    TargetDesc = OcTextureDesc(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreateTexture2D(&TargetDesc, nullptr, &Scene.Target);
    if (SUCCEEDED(hr))
        hr = Scene.Device->CreateRenderTargetView(Scene.Target.Get(), nullptr, &Scene.TargetView);

    Check.Report("scene", "0x%08lX", hr);
    Scene.Context->Flush();
    return hr;
}

/* Binds the scene's pipeline on a context */
static
VOID
OcBindScene(
    _In_ OcScene& Scene,
    _In_ ID3D11DeviceContext* Context)
{
    D3D11_VIEWPORT Viewport = { 0.0f, 0.0f, 16.0f, 16.0f, 0.0f, 1.0f };
    UINT Stride = sizeof(OcVertex);
    UINT Offset = 0;

    Context->IASetInputLayout(Scene.Layout.Get());
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Context->IASetVertexBuffers(0, 1, Scene.Vertices.GetAddressOf(), &Stride, &Offset);
    Context->VSSetShader(Scene.VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(Scene.PixelShader.Get(), nullptr, 0);
    Context->PSSetConstantBuffers(0, 1, Scene.Constants.GetAddressOf());
    Context->RSSetViewports(1, &Viewport);
    Context->OMSetRenderTargets(1, Scene.TargetView.GetAddressOf(), nullptr);
}

/* resources: the D3D12 objects behind each kind of Direct3D 11 resource */
VOID
OcCheckResources(
    _In_ OnCheck& Check)
{
    static const UINT Pixels[8 * 8] = { 0x11223344, 0x55667788 };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Buffer> Buffers[4];
    ComPtr<ID3D11Texture2D> Textures[4];
    ComPtr<ID3D11Texture3D> Volume;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_TEXTURE3D_DESC VolumeDesc = {};
    D3D11_SUBRESOURCE_DATA Data = { Pixels, 8 * sizeof(UINT), 0 };
    HRESULT hr;

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    OcBegin(Context.Get());
    BufferDesc = OcBufferDesc(1024, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER, 0);
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Buffers[0]);
    OcEnd(Check, Context.Get(), "vertex_buffer");
    Check.Report("vertex_buffer_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE);
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Buffers[1]);
    OcEnd(Check, Context.Get(), "dynamic_constants");
    Check.Report("dynamic_constants_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    BufferDesc = OcBufferDesc(4096, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Buffers[2]);
    OcEnd(Check, Context.Get(), "staging_buffer");
    Check.Report("staging_buffer_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0);
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    BufferDesc.StructureByteStride = 4;
    hr = Device->CreateBuffer(&BufferDesc, nullptr, &Buffers[3]);
    OcEnd(Check, Context.Get(), "structured_buffer");
    Check.Report("structured_buffer_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    TextureDesc = OcTextureDesc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM,
                                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    TextureDesc.MipLevels = 0;
    hr = Device->CreateTexture2D(&TextureDesc, nullptr, &Textures[0]);
    OcEnd(Check, Context.Get(), "mipped_target");
    Check.Report("mipped_target_hr", "0x%08lX", hr);
    if (SUCCEEDED(hr))
    {
        Textures[0]->GetDesc(&TextureDesc);
        Check.Report("mipped_target_mips", "%u", TextureDesc.MipLevels);
    }

    OcBegin(Context.Get());
    TextureDesc = OcTextureDesc(8, 8, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    TextureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    hr = Device->CreateTexture2D(&TextureDesc, &Data, &Textures[1]);
    OcEnd(Check, Context.Get(), "immutable_texture");
    Check.Report("immutable_texture_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    TextureDesc = OcTextureDesc(64, 64, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
    hr = Device->CreateTexture2D(&TextureDesc, nullptr, &Textures[2]);
    OcEnd(Check, Context.Get(), "depth");
    Check.Report("depth_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    TextureDesc = OcTextureDesc(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET);
    TextureDesc.SampleDesc.Count = 4;
    hr = Device->CreateTexture2D(&TextureDesc, nullptr, &Textures[3]);
    OcEnd(Check, Context.Get(), "msaa_target");
    Check.Report("msaa_target_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    VolumeDesc.Width = 8;
    VolumeDesc.Height = 8;
    VolumeDesc.Depth = 8;
    VolumeDesc.MipLevels = 1;
    VolumeDesc.Format = DXGI_FORMAT_R16_FLOAT;
    VolumeDesc.Usage = D3D11_USAGE_DEFAULT;
    VolumeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    hr = Device->CreateTexture3D(&VolumeDesc, nullptr, &Volume);
    OcEnd(Check, Context.Get(), "volume");
    Check.Report("volume_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    for (ComPtr<ID3D11Buffer>& Each : Buffers)
        Each.Reset();
    for (ComPtr<ID3D11Texture2D>& Each : Textures)
        Each.Reset();
    Volume.Reset();
    OcEnd(Check, Context.Get(), "destroy");
}

/* views: descriptors made for shader resource, target, depth and unordered access views */
VOID
OcCheckViews(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Texture2D> Color;
    ComPtr<ID3D11Texture2D> Depth;
    ComPtr<ID3D11Texture2D> Array;
    ComPtr<ID3D11Buffer> Structured;
    ComPtr<ID3D11ShaderResourceView> Srv;
    ComPtr<ID3D11ShaderResourceView> MipSrv;
    ComPtr<ID3D11RenderTargetView> Rtv;
    ComPtr<ID3D11RenderTargetView> SliceRtv;
    ComPtr<ID3D11DepthStencilView> Dsv;
    ComPtr<ID3D11UnorderedAccessView> Uav;
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
    D3D11_RENDER_TARGET_VIEW_DESC RtvDesc = {};
    D3D11_UNORDERED_ACCESS_VIEW_DESC UavDesc = {};
    HRESULT hr;

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    TextureDesc = OcTextureDesc(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM,
                                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    TextureDesc.MipLevels = 3;
    Device->CreateTexture2D(&TextureDesc, nullptr, &Color);
    TextureDesc = OcTextureDesc(32, 32, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL);
    Device->CreateTexture2D(&TextureDesc, nullptr, &Depth);
    TextureDesc = OcTextureDesc(16, 16, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_RENDER_TARGET);
    TextureDesc.ArraySize = 4;
    Device->CreateTexture2D(&TextureDesc, nullptr, &Array);
    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0);
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    BufferDesc.StructureByteStride = 4;
    Device->CreateBuffer(&BufferDesc, nullptr, &Structured);
    Context->Flush();

    OcBegin(Context.Get());
    hr = Device->CreateShaderResourceView(Color.Get(), nullptr, &Srv);
    OcEnd(Check, Context.Get(), "srv");
    Check.Report("srv_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    SrvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SrvDesc.Texture2D.MostDetailedMip = 1;
    SrvDesc.Texture2D.MipLevels = 2;
    hr = Device->CreateShaderResourceView(Color.Get(), &SrvDesc, &MipSrv);
    OcEnd(Check, Context.Get(), "srv_mip");
    Check.Report("srv_mip_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    hr = Device->CreateRenderTargetView(Color.Get(), nullptr, &Rtv);
    OcEnd(Check, Context.Get(), "rtv");
    Check.Report("rtv_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    RtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    RtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
    RtvDesc.Texture2DArray.FirstArraySlice = 2;
    RtvDesc.Texture2DArray.ArraySize = 1;
    hr = Device->CreateRenderTargetView(Array.Get(), &RtvDesc, &SliceRtv);
    OcEnd(Check, Context.Get(), "rtv_slice");
    Check.Report("rtv_slice_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    hr = Device->CreateDepthStencilView(Depth.Get(), nullptr, &Dsv);
    OcEnd(Check, Context.Get(), "dsv");
    Check.Report("dsv_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    UavDesc.Format = DXGI_FORMAT_UNKNOWN;
    UavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    UavDesc.Buffer.NumElements = 64;
    hr = Device->CreateUnorderedAccessView(Structured.Get(), &UavDesc, &Uav);
    OcEnd(Check, Context.Get(), "uav");
    Check.Report("uav_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    Srv.Reset();
    MipSrv.Reset();
    Rtv.Reset();
    SliceRtv.Reset();
    Dsv.Reset();
    Uav.Reset();
    OcEnd(Check, Context.Get(), "destroy");
}

/* shaders: creating each shader stage, which hands DXBC to dxilconv */
VOID
OcCheckShaders(
    _In_ OnCheck& Check)
{
    static const D3D11_SO_DECLARATION_ENTRY Declaration[] =
    {
        { 0, "SV_Position", 0, 0, 4, 0 },
    };
    static const struct
    {
        PCSTR Entry;
        PCSTR Profile;
    } Stages[] =
    {
        { "vs", "vs_5_0" }, { "ps", "ps_5_0" }, { "gs", "gs_5_0" }, { "cs", "cs_5_0" }, { "vs", "vs_4_0" },
    };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11DeviceChild> Shaders[_countof(Stages) + 1];
    ComPtr<ID3DBlob> Code;
    char Key[64];
    HRESULT hr;

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    for (UINT Index = 0; Index < _countof(Stages); Index++)
    {
        if (FAILED(OcCompile(Stages[Index].Entry, Stages[Index].Profile, &Code)))
        {
            Check.Report("compile", "%s failed", Stages[Index].Profile);
            continue;
        }

        sprintf_s(Key, "%s_%s", Stages[Index].Entry, Stages[Index].Profile);
        OcBegin(Context.Get());
        switch (Stages[Index].Entry[0])
        {
            case 'v':
                hr = Device->CreateVertexShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr,
                                                reinterpret_cast<ID3D11VertexShader**>(Shaders[Index].GetAddressOf()));
                break;

            case 'p':
                hr = Device->CreatePixelShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr,
                                               reinterpret_cast<ID3D11PixelShader**>(Shaders[Index].GetAddressOf()));
                break;

            case 'g':
                hr = Device->CreateGeometryShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr,
                                                  reinterpret_cast<ID3D11GeometryShader**>(
                                                      Shaders[Index].GetAddressOf()));
                break;

            default:
                hr = Device->CreateComputeShader(Code->GetBufferPointer(), Code->GetBufferSize(), nullptr,
                                                 reinterpret_cast<ID3D11ComputeShader**>(
                                                     Shaders[Index].GetAddressOf()));
                break;
        }
        OcEnd(Check, Context.Get(), Key);
        Check.Report(Key, "0x%08lX", hr);
    }

    if (SUCCEEDED(OcCompile("vs", "vs_5_0", &Code)))
    {
        OcBegin(Context.Get());
        hr = Device->CreateGeometryShaderWithStreamOutput(Code->GetBufferPointer(), Code->GetBufferSize(),
                                                          Declaration, _countof(Declaration), nullptr, 0,
                                                          D3D11_SO_NO_RASTERIZED_STREAM, nullptr,
                                                          reinterpret_cast<ID3D11GeometryShader**>(
                                                              Shaders[_countof(Stages)].GetAddressOf()));
        OcEnd(Check, Context.Get(), "stream_out");
        Check.Report("stream_out", "0x%08lX", hr);
    }

    OcBegin(Context.Get());
    for (ComPtr<ID3D11DeviceChild>& Each : Shaders)
        Each.Reset();
    OcEnd(Check, Context.Get(), "destroy");
}

/* draw: clearing and drawing a triangle, the pipeline state it needs and the pixels, then a dispatch */
VOID
OcCheckDraw(
    _In_ OnCheck& Check)
{
    static const float Clear[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    OcScene Scene;
    ComPtr<ID3DBlob> CsCode;
    ComPtr<ID3D11ComputeShader> Compute;
    ComPtr<ID3D11Buffer> Results;
    ComPtr<ID3D11UnorderedAccessView> ResultView;
    D3D11_BUFFER_DESC BufferDesc;

    if (FAILED(OcBuildScene(Check, Scene)))
        return;

    OcBegin(Scene.Context.Get());
    Scene.Context->ClearRenderTargetView(Scene.TargetView.Get(), Clear);
    OcEnd(Check, Scene.Context.Get(), "clear");
    OcReportTexture(Check, "clear_pixels", Scene.Device.Get(), Scene.Context.Get(), Scene.Target.Get());

    OcBegin(Scene.Context.Get());
    OcBindScene(Scene, Scene.Context.Get());
    Scene.Context->Draw(3, 0);
    OcEnd(Check, Scene.Context.Get(), "draw");
    OcReportTexture(Check, "draw_pixels", Scene.Device.Get(), Scene.Context.Get(), Scene.Target.Get());

    OcBegin(Scene.Context.Get());
    Scene.Context->Draw(3, 0);
    OcEnd(Check, Scene.Context.Get(), "redraw");

    if (FAILED(OcCompile("cs", "cs_5_0", &CsCode)))
        return;

    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0);
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    BufferDesc.StructureByteStride = 4;
    Scene.Device->CreateBuffer(&BufferDesc, nullptr, &Results);
    Scene.Device->CreateUnorderedAccessView(Results.Get(), nullptr, &ResultView);
    Scene.Device->CreateComputeShader(CsCode->GetBufferPointer(), CsCode->GetBufferSize(), nullptr, &Compute);
    Scene.Context->Flush();

    OcBegin(Scene.Context.Get());
    Scene.Context->CSSetShader(Compute.Get(), nullptr, 0);
    Scene.Context->CSSetUnorderedAccessViews(0, 1, ResultView.GetAddressOf(), nullptr);
    Scene.Context->Dispatch(8, 1, 1);
    OcEnd(Check, Scene.Context.Get(), "dispatch");
    OcReportBuffer(Check, "dispatch_words", Scene.Device.Get(), Scene.Context.Get(), Results.Get());
}

/* copies: UpdateSubresource, CopyResource and CopySubresourceRegion, with the texels they leave */
VOID
OcCheckCopies(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Texture2D> Source;
    ComPtr<ID3D11Texture2D> Whole;
    ComPtr<ID3D11Texture2D> Part;
    ComPtr<ID3D11Buffer> BufferSource;
    ComPtr<ID3D11Buffer> BufferDest;
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_BOX Box = { 2, 3, 0, 10, 9, 1 };
    D3D11_BOX BufferBox = { 16, 0, 0, 80, 1, 1 };
    UINT Texels[16 * 16];
    UINT Words[64];

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    for (UINT Index = 0; Index < _countof(Texels); Index++)
        Texels[Index] = 0xFF000000 | (Index * 0x010307);
    for (UINT Index = 0; Index < _countof(Words); Index++)
        Words[Index] = Index * 7 + 5;

    TextureDesc = OcTextureDesc(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    Device->CreateTexture2D(&TextureDesc, nullptr, &Source);
    Device->CreateTexture2D(&TextureDesc, nullptr, &Whole);
    Device->CreateTexture2D(&TextureDesc, nullptr, &Part);
    BufferDesc = OcBufferDesc(sizeof(Words), D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0);
    BufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    Device->CreateBuffer(&BufferDesc, nullptr, &BufferSource);
    Device->CreateBuffer(&BufferDesc, nullptr, &BufferDest);
    Context->Flush();

    OcBegin(Context.Get());
    Context->UpdateSubresource(Source.Get(), 0, nullptr, Texels, 16 * sizeof(UINT), 0);
    OcEnd(Check, Context.Get(), "update_texture");

    OcBegin(Context.Get());
    Context->UpdateSubresource(BufferSource.Get(), 0, nullptr, Words, sizeof(Words), 0);
    OcEnd(Check, Context.Get(), "update_buffer");

    OcBegin(Context.Get());
    Context->CopyResource(Whole.Get(), Source.Get());
    OcEnd(Check, Context.Get(), "copy_resource");

    OcBegin(Context.Get());
    Context->CopySubresourceRegion(Part.Get(), 0, 5, 4, 0, Source.Get(), 0, &Box);
    OcEnd(Check, Context.Get(), "copy_region");

    OcBegin(Context.Get());
    Context->CopySubresourceRegion(BufferDest.Get(), 0, 32, 0, 0, BufferSource.Get(), 0, &BufferBox);
    OcEnd(Check, Context.Get(), "copy_buffer_region");

    OcReportTexture(Check, "source_pixels", Device.Get(), Context.Get(), Source.Get());
    OcReportTexture(Check, "whole_pixels", Device.Get(), Context.Get(), Whole.Get());
    OcReportTexture(Check, "part_pixels", Device.Get(), Context.Get(), Part.Get());
    OcReportBuffer(Check, "buffer_words", Device.Get(), Context.Get(), BufferDest.Get());
}

/* map: dynamic buffers renamed by discard and appended to by no overwrite */
VOID
OcCheckMap(
    _In_ OnCheck& Check)
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Buffer> Dynamic;
    ComPtr<ID3D11Buffer> Copy;
    ComPtr<ID3D11Texture2D> DynamicTexture;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_TEXTURE2D_DESC TextureDesc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    HRESULT hr;

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE);
    Device->CreateBuffer(&BufferDesc, nullptr, &Dynamic);
    BufferDesc = OcBufferDesc(256, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0);
    Device->CreateBuffer(&BufferDesc, nullptr, &Copy);
    TextureDesc = OcTextureDesc(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    TextureDesc.Usage = D3D11_USAGE_DYNAMIC;
    TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    Device->CreateTexture2D(&TextureDesc, nullptr, &DynamicTexture);
    Context->Flush();

    OcBegin(Context.Get());
    hr = Context->Map(Dynamic.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        memset(Mapped.pData, 0x5A, 128);
        Context->Unmap(Dynamic.Get(), 0);
    }
    Context->CopyResource(Copy.Get(), Dynamic.Get());
    OcEnd(Check, Context.Get(), "discard");
    Check.Report("discard_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    hr = Context->Map(Dynamic.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        memset(Mapped.pData, 0x3C, 256);
        Context->Unmap(Dynamic.Get(), 0);
    }
    OcEnd(Check, Context.Get(), "discard_again");
    Check.Report("discard_again_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    hr = Context->Map(Dynamic.Get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        memset(static_cast<BYTE*>(Mapped.pData) + 128, 0x77, 128);
        Context->Unmap(Dynamic.Get(), 0);
    }
    Context->CopyResource(Copy.Get(), Dynamic.Get());
    OcEnd(Check, Context.Get(), "no_overwrite");
    Check.Report("no_overwrite_hr", "0x%08lX", hr);

    OcBegin(Context.Get());
    hr = Context->Map(DynamicTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        Check.Report("texture_pitch", "%u", Mapped.RowPitch);
        for (UINT Row = 0; Row < 8; Row++)
            memset(static_cast<BYTE*>(Mapped.pData) + Row * Mapped.RowPitch, Row * 16, 8 * sizeof(UINT));
        Context->Unmap(DynamicTexture.Get(), 0);
    }
    OcEnd(Check, Context.Get(), "texture_discard");
    Check.Report("texture_discard_hr", "0x%08lX", hr);

    hr = Context->Map(Copy.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    Check.Report("map_default", "0x%08lX", hr);

    OcReportBuffer(Check, "copy_words", Device.Get(), Context.Get(), Copy.Get());
    OcReportTexture(Check, "texture_pixels", Device.Get(), Context.Get(), DynamicTexture.Get());
}

/* Polls a query outside any recording; how often it is polled depends on timing */
static
HRESULT
OcWaitQuery(
    _In_ ID3D11DeviceContext* Context,
    _In_ ID3D11Asynchronous* Query,
    _Out_writes_bytes_(Size) VOID* Data,
    _In_ UINT Size)
{
    HRESULT hr;

    for (UINT Attempt = 0; Attempt < 100000; Attempt++)
    {
        hr = Context->GetData(Query, Data, Size, 0);
        if (hr != S_FALSE)
            return hr;
        Sleep(0);
    }

    return hr;
}

/* queries: event, occlusion, pipeline statistics and timestamp disjoint queries around a draw */
VOID
OcCheckQueries(
    _In_ OnCheck& Check)
{
    OcScene Scene;
    ComPtr<ID3D11Query> Event;
    ComPtr<ID3D11Query> Occlusion;
    ComPtr<ID3D11Query> Statistics;
    ComPtr<ID3D11Query> Disjoint;
    ComPtr<ID3D11Predicate> Predicate;
    D3D11_QUERY_DESC Desc = {};
    D3D11_QUERY_DATA_PIPELINE_STATISTICS Counts = {};
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT Timing = {};
    UINT64 Samples = 0;
    BOOL Done = FALSE;
    HRESULT hr;

    if (FAILED(OcBuildScene(Check, Scene)))
        return;

    OcBegin(Scene.Context.Get());
    Desc.Query = D3D11_QUERY_EVENT;
    hr = Scene.Device->CreateQuery(&Desc, &Event);
    Check.Report("event_hr", "0x%08lX", hr);
    Desc.Query = D3D11_QUERY_OCCLUSION;
    hr = Scene.Device->CreateQuery(&Desc, &Occlusion);
    Check.Report("occlusion_hr", "0x%08lX", hr);
    Desc.Query = D3D11_QUERY_PIPELINE_STATISTICS;
    hr = Scene.Device->CreateQuery(&Desc, &Statistics);
    Check.Report("statistics_hr", "0x%08lX", hr);
    Desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    hr = Scene.Device->CreateQuery(&Desc, &Disjoint);
    Check.Report("disjoint_hr", "0x%08lX", hr);
    Desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
    hr = Scene.Device->CreatePredicate(&Desc, &Predicate);
    Check.Report("predicate_hr", "0x%08lX", hr);
    OcEnd(Check, Scene.Context.Get(), "create");

    if (Event == nullptr || Occlusion == nullptr || Statistics == nullptr || Disjoint == nullptr ||
        Predicate == nullptr)
    {
        return;
    }

    OcBegin(Scene.Context.Get());
    OcBindScene(Scene, Scene.Context.Get());
    Scene.Context->Begin(Disjoint.Get());
    Scene.Context->Begin(Occlusion.Get());
    Scene.Context->Begin(Statistics.Get());
    Scene.Context->Begin(Predicate.Get());
    Scene.Context->Draw(3, 0);
    Scene.Context->End(Predicate.Get());
    Scene.Context->End(Statistics.Get());
    Scene.Context->End(Occlusion.Get());
    Scene.Context->End(Disjoint.Get());
    Scene.Context->End(Event.Get());
    OcEnd(Check, Scene.Context.Get(), "queries");

    hr = OcWaitQuery(Scene.Context.Get(), Event.Get(), &Done, sizeof(Done));
    Check.Report("event", "0x%08lX %d", hr, Done);
    hr = OcWaitQuery(Scene.Context.Get(), Occlusion.Get(), &Samples, sizeof(Samples));
    Check.Report("occlusion", "0x%08lX %llu", hr, Samples);
    hr = OcWaitQuery(Scene.Context.Get(), Statistics.Get(), &Counts, sizeof(Counts));
    Check.Report("statistics", "0x%08lX ia %llu %llu vs %llu c %llu %llu ps %llu", hr, Counts.IAVertices,
                 Counts.IAPrimitives, Counts.VSInvocations, Counts.CInvocations, Counts.CPrimitives,
                 Counts.PSInvocations);
    hr = OcWaitQuery(Scene.Context.Get(), Disjoint.Get(), &Timing, sizeof(Timing));
    Check.Report("disjoint", "0x%08lX %d", hr, Timing.Disjoint);
    hr = OcWaitQuery(Scene.Context.Get(), Predicate.Get(), &Done, sizeof(Done));
    Check.Report("predicate", "0x%08lX %d", hr, Done);

    OcBegin(Scene.Context.Get());
    Scene.Context->SetPredication(Predicate.Get(), FALSE);
    Scene.Context->Draw(3, 0);
    Scene.Context->SetPredication(nullptr, FALSE);
    OcEnd(Check, Scene.Context.Get(), "predicated");
}

/* deferred: a command list recorded on a deferred context and run on the immediate one */
VOID
OcCheckDeferred(
    _In_ OnCheck& Check)
{
    static const float Clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    static const float Half[4] = { 0.5f, 1.0f, 1.0f, 1.0f };
    OcScene Scene;
    ComPtr<ID3D11DeviceContext> Deferred;
    ComPtr<ID3D11CommandList> List;
    ComPtr<ID3D11Buffer> Constants;
    D3D11_BUFFER_DESC BufferDesc;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    HRESULT hr;

    if (FAILED(OcBuildScene(Check, Scene)))
        return;

    BufferDesc = OcBufferDesc(16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE);
    Scene.Device->CreateBuffer(&BufferDesc, nullptr, &Constants);

    OcBegin(Scene.Context.Get());
    hr = Scene.Device->CreateDeferredContext(0, &Deferred);
    OcEnd(Check, Scene.Context.Get(), "create");
    Check.Report("create_hr", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    OcBegin(Scene.Context.Get());
    Deferred->ClearRenderTargetView(Scene.TargetView.Get(), Clear);
    OcBindScene(Scene, Deferred.Get());
    hr = Deferred->Map(Constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped);
    if (SUCCEEDED(hr))
    {
        memcpy(Mapped.pData, Half, sizeof(Half));
        Deferred->Unmap(Constants.Get(), 0);
    }
    Deferred->PSSetConstantBuffers(0, 1, Constants.GetAddressOf());
    Deferred->Draw(3, 0);
    hr = Deferred->FinishCommandList(FALSE, &List);
    OcEnd(Check, Scene.Context.Get(), "record");
    Check.Report("record_hr", "0x%08lX", hr);
    if (FAILED(hr))
        return;

    OcBegin(Scene.Context.Get());
    Scene.Context->ExecuteCommandList(List.Get(), FALSE);
    OcEnd(Check, Scene.Context.Get(), "execute");
    OcReportTexture(Check, "pixels", Scene.Device.Get(), Scene.Context.Get(), Scene.Target.Get());

    OcBegin(Scene.Context.Get());
    List.Reset();
    Deferred.Reset();
    OcEnd(Check, Scene.Context.Get(), "destroy");
}

static
LRESULT
CALLBACK
OcWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    return DefWindowProcW(Window, Message, WParam, LParam);
}

/* present: a windowed flip swap chain on a hidden window, its creation traced, then presented twice */
VOID
OcCheckPresent(
    _In_ OnCheck& Check)
{
    static const float Colors[2][4] = { { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } };
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    ComPtr<IDXGIFactory2> Factory;
    ComPtr<IDXGISwapChain1> SwapChain;
    DXGI_SWAP_CHAIN_DESC1 Desc = {};
    WNDCLASSW Class = {};
    HWND Window;
    HRESULT hr;

    if (FAILED(OcOpen(Check, &Device, &Context)))
        return;

    Class.lpfnWndProc = OcWindowProc;
    Class.hInstance = GetModuleHandleW(nullptr);
    Class.lpszClassName = L"d3d11on12check";
    RegisterClassW(&Class);
    Window = CreateWindowExW(0, Class.lpszClassName, L"d3d11on12check", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                             nullptr, Class.hInstance, nullptr);
    if (Window == nullptr)
    {
        Check.Report("window", "failed");
        return;
    }

    Device.As(&DxgiDevice);
    DxgiDevice->GetAdapter(&Adapter);
    Adapter->GetParent(IID_PPV_ARGS(&Factory));

    Desc.Width = 32;
    Desc.Height = 32;
    Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    Desc.SampleDesc.Count = 1;
    Desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    Desc.BufferCount = 2;
    Desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    OcBegin(Context.Get());
    hr = Factory->CreateSwapChainForHwnd(Device.Get(), Window, &Desc, nullptr, nullptr, &SwapChain);
    OcEnd(Check, Context.Get(), "create");
    Check.Report("create_hr", "0x%08lX", hr);

    for (UINT Frame = 0; SwapChain != nullptr && Frame < 2; Frame++)
    {
        ComPtr<ID3D11Texture2D> Buffer;
        ComPtr<ID3D11RenderTargetView> View;
        char Key[32];

        SwapChain->GetBuffer(0, IID_PPV_ARGS(&Buffer));
        Device->CreateRenderTargetView(Buffer.Get(), nullptr, &View);
        Context->Flush();

        /* Which allocators a present reuses depends on GPU progress, so the frames print results only */
        sprintf_s(Key, "frame%u", Frame);
        Context->ClearRenderTargetView(View.Get(), Colors[Frame]);
        hr = SwapChain->Present(0, 0);
        Check.Report(Key, "0x%08lX", hr);
    }

    SwapChain.Reset();
    Context->Flush();
    DestroyWindow(Window);
}
