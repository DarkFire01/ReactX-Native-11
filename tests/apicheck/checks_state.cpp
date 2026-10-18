/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pipeline state checks: defaults, getters, ClearState, desc normalization, lifetime
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "check.hpp"

#include "SimpleVS.h"
#include "SimplePS.h"

static
ComPtr<ID3D11Buffer>
AcBuffer(
    _In_ CheckContext& Check,
    _In_ UINT BindFlags,
    _In_ UINT Size)
{
    D3D11_BUFFER_DESC Desc = {};
    ComPtr<ID3D11Buffer> Buffer;

    Desc.ByteWidth = Size;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;
    Check.Expect(SUCCEEDED(Check.Device->CreateBuffer(&Desc, nullptr, &Buffer)), "CreateBuffer");
    return Buffer;
}

static
ComPtr<ID3D11Texture2D>
AcTexture(
    _In_ CheckContext& Check,
    _In_ DXGI_FORMAT Format,
    _In_ UINT BindFlags)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    ComPtr<ID3D11Texture2D> Texture;

    Desc.Width = 64;
    Desc.Height = 64;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = Format;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;
    Check.Expect(SUCCEEDED(Check.Device->CreateTexture2D(&Desc, nullptr, &Texture)), "CreateTexture2D");
    return Texture;
}

/* Prints whether a getter handed back the bound object, and that it took a reference */
template <typename Interface>
static
VOID
AcSameObject(
    _In_ CheckContext& Check,
    _In_z_ PCSTR Key,
    _In_opt_ Interface* Bound,
    _In_opt_ Interface* Returned,
    _In_ ULONG CountBefore)
{
    Check.Report(Key, "%s", (Bound == Returned) ? "same" : (Returned ? "other" : "null"));
    if (Returned != nullptr)
    {
        Check.Expect(AcRefCount(Returned) == CountBefore + 1, "getter adds a reference");
        Returned->Release();
    }
}

static
VOID
AcCheckDefaults(
    _In_ CheckContext& Check)
{
    ID3D11DeviceContext* Context = Check.Context;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11BlendState> Blend;
    ComPtr<ID3D11DepthStencilState> DepthStencil;
    ComPtr<ID3D11RasterizerState> Rasterizer;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11SamplerState> Sampler;
    ComPtr<ID3D11RenderTargetView> RenderTarget;
    ComPtr<ID3D11DepthStencilView> DepthView;
    D3D11_PRIMITIVE_TOPOLOGY Topology;
    FLOAT Factor[4] = {};
    UINT Instances = 7;
    UINT Mask = 0;
    UINT Reference = 9;
    UINT Count = 5;

    Context->IAGetPrimitiveTopology(&Topology);
    Check.Report("topology", "%d", Topology);
    Context->IAGetInputLayout(&Layout);
    Check.Report("input_layout", "%s", Layout ? "set" : "null");
    Context->VSGetShader(&VertexShader, nullptr, &Instances);
    Check.Report("vertex_shader", "%s instances %u", VertexShader ? "set" : "null", Instances);
    Context->OMGetBlendState(&Blend, Factor, &Mask);
    Check.Report("blend", "%s factor %.2f %.2f %.2f %.2f mask %08X", Blend ? "set" : "null", Factor[0], Factor[1],
                 Factor[2], Factor[3], Mask);
    Context->OMGetDepthStencilState(&DepthStencil, &Reference);
    Check.Report("depth_stencil", "%s ref %u", DepthStencil ? "set" : "null", Reference);
    Context->RSGetState(&Rasterizer);
    Check.Report("rasterizer", "%s", Rasterizer ? "set" : "null");
    Context->RSGetViewports(&Count, nullptr);
    Check.Report("viewport_count", "%u", Count);
    Count = 5;
    Context->RSGetScissorRects(&Count, nullptr);
    Check.Report("scissor_count", "%u", Count);
    Context->PSGetSamplers(0, 1, &Sampler);
    Check.Report("sampler0", "%s", Sampler ? "set" : "null");
    Context->OMGetRenderTargets(1, &RenderTarget, &DepthView);
    Check.Report("targets", "%s %s", RenderTarget ? "set" : "null", DepthView ? "set" : "null");
}

static
VOID
AcCheckGetters(
    _In_ CheckContext& Check)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D11Device* Device = Check.Device;
    ID3D11DeviceContext* Context = Check.Context;
    ComPtr<ID3D11Buffer> Vertices = AcBuffer(Check, D3D11_BIND_VERTEX_BUFFER, 256);
    ComPtr<ID3D11Buffer> Indices = AcBuffer(Check, D3D11_BIND_INDEX_BUFFER, 64);
    ComPtr<ID3D11Buffer> Constants = AcBuffer(Check, D3D11_BIND_CONSTANT_BUFFER, 64);
    ComPtr<ID3D11Texture2D> Color = AcTexture(Check, DXGI_FORMAT_R8G8B8A8_UNORM,
                                              D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11Texture2D> Depth = AcTexture(Check, DXGI_FORMAT_D24_UNORM_S8_UINT, D3D11_BIND_DEPTH_STENCIL);
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11RenderTargetView> RenderTarget;
    ComPtr<ID3D11DepthStencilView> DepthView;
    ComPtr<ID3D11ShaderResourceView> ShaderView;
    ComPtr<ID3D11BlendState> Blend;
    ComPtr<ID3D11DepthStencilState> DepthState;
    ComPtr<ID3D11RasterizerState> Rasterizer;
    ComPtr<ID3D11SamplerState> Sampler;
    D3D11_BLEND_DESC BlendDesc = {};
    D3D11_DEPTH_STENCIL_DESC DepthDesc = {};
    D3D11_RASTERIZER_DESC RasterDesc = {};
    D3D11_SAMPLER_DESC SamplerDesc = {};
    D3D11_VIEWPORT Viewports[2] = { { 0, 0, 64, 32, 0, 1 }, { 8, 8, 16, 16, 0.25f, 0.75f } };
    D3D11_VIEWPORT BadViewport = { 0, 0, 10, 10, 0.9f, 0.1f };
    D3D11_RECT Rects[1] = { { 1, 2, 30, 40 } };
    const FLOAT Factor[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    UINT Strides[2] = { 24, 12 };
    UINT Offsets[2] = { 4, 8 };
    ID3D11Buffer* VertexBuffers[2] = { Vertices.Get(), Vertices.Get() };

    BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    BlendDesc.RenderTarget[0].RenderTargetWriteMask = 0xF;
    DepthDesc.DepthFunc = D3D11_COMPARISON_LESS;
    DepthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    RasterDesc.FillMode = D3D11_FILL_WIREFRAME;
    RasterDesc.CullMode = D3D11_CULL_NONE;
    SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    SamplerDesc.AddressU = SamplerDesc.AddressV = SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    Check.Expect(SUCCEEDED(Device->CreateVertexShader(g_SimpleVS, sizeof(g_SimpleVS), nullptr, &VertexShader)), "VS");
    Check.Expect(SUCCEEDED(Device->CreatePixelShader(g_SimplePS, sizeof(g_SimplePS), nullptr, &PixelShader)), "PS");
    Check.Expect(SUCCEEDED(Device->CreateInputLayout(Elements, 2, g_SimpleVS, sizeof(g_SimpleVS), &Layout)), "layout");
    Check.Expect(SUCCEEDED(Device->CreateRenderTargetView(Color.Get(), nullptr, &RenderTarget)), "RTV");
    Check.Expect(SUCCEEDED(Device->CreateDepthStencilView(Depth.Get(), nullptr, &DepthView)), "DSV");
    Check.Expect(SUCCEEDED(Device->CreateShaderResourceView(Color.Get(), nullptr, &ShaderView)), "SRV");
    Check.Expect(SUCCEEDED(Device->CreateBlendState(&BlendDesc, &Blend)), "blend");
    Check.Expect(SUCCEEDED(Device->CreateDepthStencilState(&DepthDesc, &DepthState)), "depth state");
    Check.Expect(SUCCEEDED(Device->CreateRasterizerState(&RasterDesc, &Rasterizer)), "rasterizer");
    Check.Expect(SUCCEEDED(Device->CreateSamplerState(&SamplerDesc, &Sampler)), "sampler");
    if (Check.Failures != 0)
        return;

    Context->VSSetShader(VertexShader.Get(), nullptr, 0);
    Context->PSSetShader(PixelShader.Get(), nullptr, 0);
    Context->IASetInputLayout(Layout.Get());
    Context->IASetVertexBuffers(3, 2, VertexBuffers, Strides, Offsets);
    Context->IASetIndexBuffer(Indices.Get(), DXGI_FORMAT_R32_UINT, 12);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
    Context->VSSetConstantBuffers(5, 1, Constants.GetAddressOf());
    Context->GSSetConstantBuffers(2, 1, Constants.GetAddressOf());
    Context->PSSetShaderResources(7, 1, ShaderView.GetAddressOf());
    Context->CSSetSamplers(3, 1, Sampler.GetAddressOf());
    Context->OMSetBlendState(Blend.Get(), Factor, 0x00FF00FF);
    Context->OMSetDepthStencilState(DepthState.Get(), 0x1FF);
    Context->RSSetState(Rasterizer.Get());
    Context->RSSetViewports(2, Viewports);
    Context->RSSetViewports(1, &BadViewport);
    Context->RSSetScissorRects(1, Rects);

    {
        ComPtr<ID3D11VertexShader> GotVertex;
        ComPtr<ID3D11PixelShader> GotPixel;
        ComPtr<ID3D11InputLayout> GotLayout;
        ID3D11Buffer* GotBuffers[3] = {};
        UINT GotStrides[3] = {};
        UINT GotOffsets[3] = {};
        ComPtr<ID3D11Buffer> GotIndex;
        DXGI_FORMAT IndexFormat;
        UINT IndexOffset;
        D3D11_PRIMITIVE_TOPOLOGY Topology;
        ULONG Before = AcRefCount(VertexShader.Get());
        UINT Instances = 3;

        Context->VSGetShader(&GotVertex, nullptr, &Instances);
        AcSameObject(Check, "vs", VertexShader.Get(), GotVertex.Get(), Before);
        GotVertex.Detach();
        Check.Report("vs_instances", "%u", Instances);
        Before = AcRefCount(PixelShader.Get());
        Context->PSGetShader(&GotPixel, nullptr, nullptr);
        AcSameObject(Check, "ps", PixelShader.Get(), GotPixel.Get(), Before);
        GotPixel.Detach();
        Before = AcRefCount(Layout.Get());
        Context->IAGetInputLayout(&GotLayout);
        AcSameObject(Check, "layout", Layout.Get(), GotLayout.Get(), Before);
        GotLayout.Detach();

        Context->IAGetVertexBuffers(2, 3, GotBuffers, GotStrides, GotOffsets);
        Check.Report("vertex_buffers", "%s %s %s strides %u %u %u offsets %u %u %u", GotBuffers[0] ? "set" : "null",
                     GotBuffers[1] == Vertices.Get() ? "same" : "other", GotBuffers[2] == Vertices.Get() ? "same" : "other",
                     GotStrides[0], GotStrides[1], GotStrides[2], GotOffsets[0], GotOffsets[1], GotOffsets[2]);
        for (ID3D11Buffer* Buffer : GotBuffers)
        {
            if (Buffer != nullptr)
                Buffer->Release();
        }

        Context->IAGetIndexBuffer(&GotIndex, &IndexFormat, &IndexOffset);
        Check.Report("index_buffer", "%s format %d offset %u", GotIndex.Get() == Indices.Get() ? "same" : "other",
                     IndexFormat, IndexOffset);
        Context->IAGetPrimitiveTopology(&Topology);
        Check.Report("topology", "%d", Topology);
    }

    {
        ComPtr<ID3D11Buffer> Got;
        ComPtr<ID3D11Buffer> Empty;
        ComPtr<ID3D11ShaderResourceView> GotView;
        ComPtr<ID3D11SamplerState> GotSampler;

        Context->VSGetConstantBuffers(5, 1, &Got);
        Check.Report("vs_cb5", "%s", Got.Get() == Constants.Get() ? "same" : "other");
        Context->VSGetConstantBuffers(4, 1, &Empty);
        Check.Report("vs_cb4", "%s", Empty ? "set" : "null");
        Got.Reset();
        Context->GSGetConstantBuffers(2, 1, &Got);
        Check.Report("gs_cb2", "%s", Got.Get() == Constants.Get() ? "same" : "other");
        Context->PSGetShaderResources(7, 1, &GotView);
        Check.Report("ps_srv7", "%s", GotView.Get() == ShaderView.Get() ? "same" : "other");
        Context->CSGetSamplers(3, 1, &GotSampler);
        Check.Report("cs_sampler3", "%s", GotSampler.Get() == Sampler.Get() ? "same" : "other");
        GotSampler.Reset();
        Context->CSGetSamplers(2, 1, &GotSampler);
        Check.Report("cs_sampler2", "%s", GotSampler ? "set" : "null");
    }

    {
        ComPtr<ID3D11BlendState> GotBlend;
        ComPtr<ID3D11DepthStencilState> GotDepth;
        ComPtr<ID3D11RasterizerState> GotRaster;
        FLOAT GotFactor[4] = {};
        UINT Mask = 0;
        UINT Reference = 0;

        Context->OMGetBlendState(&GotBlend, GotFactor, &Mask);
        Check.Report("blend", "%s factor %.2f %.2f %.2f %.2f mask %08X", GotBlend.Get() == Blend.Get() ? "same" : "other",
                     GotFactor[0], GotFactor[1], GotFactor[2], GotFactor[3], Mask);
        Context->OMGetDepthStencilState(&GotDepth, &Reference);
        Check.Report("depth_stencil", "%s ref %u", GotDepth.Get() == DepthState.Get() ? "same" : "other", Reference);
        Context->RSGetState(&GotRaster);
        Check.Report("rasterizer", "%s", GotRaster.Get() == Rasterizer.Get() ? "same" : "other");
    }

    {
        D3D11_VIEWPORT GotViewports[3] = {};
        D3D11_RECT GotRects[2] = {};
        UINT Count = 3;

        Context->RSGetViewports(&Count, GotViewports);
        Check.Report("viewports", "%u: %.0f %.0f %.0f %.0f %.2f %.2f / %.0f %.0f %.0f %.0f %.2f %.2f / %.0f", Count,
                     GotViewports[0].TopLeftX, GotViewports[0].TopLeftY, GotViewports[0].Width, GotViewports[0].Height,
                     GotViewports[0].MinDepth, GotViewports[0].MaxDepth, GotViewports[1].TopLeftX,
                     GotViewports[1].TopLeftY, GotViewports[1].Width, GotViewports[1].Height, GotViewports[1].MinDepth,
                     GotViewports[1].MaxDepth, GotViewports[2].Width);
        Count = 2;
        Context->RSGetScissorRects(&Count, GotRects);
        Check.Report("scissors", "%u: %ld %ld %ld %ld / %ld", Count, GotRects[0].left, GotRects[0].top,
                     GotRects[0].right, GotRects[0].bottom, GotRects[1].right);
    }

    {
        ComPtr<ID3D11RenderTargetView> GotTarget;
        ComPtr<ID3D11DepthStencilView> GotDepthView;

        Context->OMSetRenderTargets(1, RenderTarget.GetAddressOf(), DepthView.Get());
        Context->OMGetRenderTargets(1, &GotTarget, &GotDepthView);
        Check.Report("targets", "%s %s", GotTarget.Get() == RenderTarget.Get() ? "same" : "other",
                     GotDepthView.Get() == DepthView.Get() ? "same" : "other");

        /* The texture is bound for output now, so the read binding of it must be gone */
        ComPtr<ID3D11ShaderResourceView> GotView;
        Context->PSGetShaderResources(7, 1, &GotView);
        Check.Report("ps_srv7_after_rtv", "%s", GotView ? "set" : "null");
    }
}

static
VOID
AcCheckClearState(
    _In_ CheckContext& Check)
{
    ID3D11DeviceContext* Context = Check.Context;
    ComPtr<ID3D11Buffer> Constants = AcBuffer(Check, D3D11_BIND_CONSTANT_BUFFER, 64);
    ComPtr<ID3D11Buffer> Got;
    D3D11_VIEWPORT Viewport = { 0, 0, 8, 8, 0, 1 };
    D3D11_PRIMITIVE_TOPOLOGY Topology;
    UINT Count = 4;
    UINT Reference = 1;

    Context->PSSetConstantBuffers(1, 1, Constants.GetAddressOf());
    Context->RSSetViewports(1, &Viewport);
    Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    Context->OMSetDepthStencilState(nullptr, 5);
    Context->ClearState();

    Context->PSGetConstantBuffers(1, 1, &Got);
    Check.Report("ps_cb1", "%s", Got ? "set" : "null");
    Context->RSGetViewports(&Count, nullptr);
    Check.Report("viewport_count", "%u", Count);
    Context->IAGetPrimitiveTopology(&Topology);
    Check.Report("topology", "%d", Topology);
    Context->OMGetDepthStencilState(nullptr, &Reference);
    Check.Report("stencil_ref", "%u", Reference);
}

static
VOID
AcCheckDescs(
    _In_ CheckContext& Check)
{
    ID3D11Device* Device = Check.Device;
    D3D11_BLEND_DESC Blend = {};
    D3D11_DEPTH_STENCIL_DESC Depth = {};
    D3D11_SAMPLER_DESC Sampler = {};
    ComPtr<ID3D11BlendState> BlendState;
    ComPtr<ID3D11DepthStencilState> DepthState;
    ComPtr<ID3D11SamplerState> SamplerState;

    /* Independent blending off: target 1 must come back as a copy of target 0 */
    Blend.RenderTarget[0].BlendEnable = TRUE;
    Blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    Blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    Blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    Blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].RenderTargetWriteMask = 0x7;
    Blend.RenderTarget[1].SrcBlend = D3D11_BLEND_DEST_COLOR;
    if (Check.Expect(SUCCEEDED(Device->CreateBlendState(&Blend, &BlendState)), "CreateBlendState"))
    {
        BlendState->GetDesc(&Blend);
        Check.Report("blend_rt1", "enable %d src %d dest %d mask %X", Blend.RenderTarget[1].BlendEnable,
                     Blend.RenderTarget[1].SrcBlend, Blend.RenderTarget[1].DestBlend,
                     Blend.RenderTarget[1].RenderTargetWriteMask);
    }

    /* Depth off and stencil off: the fields they ignore come back pinned */
    Depth.DepthEnable = FALSE;
    Depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    Depth.DepthFunc = D3D11_COMPARISON_GREATER;
    Depth.StencilReadMask = 0x12;
    Depth.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    if (Check.Expect(SUCCEEDED(Device->CreateDepthStencilState(&Depth, &DepthState)), "CreateDepthStencilState"))
    {
        DepthState->GetDesc(&Depth);
        Check.Report("depth", "write %d func %d read %X front func %d", Depth.DepthWriteMask, Depth.DepthFunc,
                     Depth.StencilReadMask, Depth.FrontFace.StencilFunc);
    }

    /* Point filtering with clamp: anisotropy, comparison and border color are not read */
    Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    Sampler.MaxAnisotropy = 9;
    Sampler.ComparisonFunc = D3D11_COMPARISON_LESS;
    Sampler.BorderColor[0] = 0.5f;
    Sampler.MaxLOD = 4.0f;
    if (Check.Expect(SUCCEEDED(Device->CreateSamplerState(&Sampler, &SamplerState)), "CreateSamplerState"))
    {
        SamplerState->GetDesc(&Sampler);
        Check.Report("sampler", "aniso %u compare %d border %.2f maxlod %.1f", Sampler.MaxAnisotropy,
                     Sampler.ComparisonFunc, Sampler.BorderColor[0], Sampler.MaxLOD);
    }
}

static
VOID
AcCheckNullOutput(
    _In_ CheckContext& Check)
{
    D3D11_BUFFER_DESC Desc = {};
    D3D11_SAMPLER_DESC Sampler = {};

    Desc.ByteWidth = 64;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    Sampler.AddressU = Sampler.AddressV = Sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

    /* A null output pointer only validates */
    Check.Report("buffer", "%08X", Check.Device->CreateBuffer(&Desc, nullptr, nullptr));
    Check.Report("sampler", "%08X", Check.Device->CreateSamplerState(&Sampler, nullptr));
    Check.Report("vertex_shader", "%08X", Check.Device->CreateVertexShader(g_SimpleVS, sizeof(g_SimpleVS), nullptr,
                                                                         nullptr));
    Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    Desc.ByteWidth = 20;
    Check.Report("unaligned_constant_buffer", "%08X", Check.Device->CreateBuffer(&Desc, nullptr, nullptr));
}

static
VOID
AcCheckLifetime(
    _In_ CheckContext& Check)
{
    ComPtr<ID3D11Buffer> Constants = AcBuffer(Check, D3D11_BIND_CONSTANT_BUFFER, 64);
    ComPtr<ID3D11Buffer> Got;
    ID3D11Buffer* Raw = Constants.Get();

    /* Bound objects outlive the caller's references; a getter brings one back */
    Check.Context->VSSetConstantBuffers(0, 1, &Raw);
    Constants.Reset();
    Check.Context->VSGetConstantBuffers(0, 1, &Got);
    Check.Report("bound_after_release", "%s", Got ? "alive" : "gone");
    if (Got)
    {
        D3D11_BUFFER_DESC Desc;

        Got->GetDesc(&Desc);
        Check.Report("bound_desc", "%u %X", Desc.ByteWidth, Desc.BindFlags);
    }
}

static const CheckInfo StateChecks[] =
{
    { "defaults", AcCheckDefaults },
    { "getters", AcCheckGetters },
    { "clearstate", AcCheckClearState },
    { "descs", AcCheckDescs },
    { "nulloutput", AcCheckNullOutput },
    { "lifetime", AcCheckLifetime },
};

const CheckInfo*
AcStateChecks(
    _Out_ UINT* Count)
{
    *Count = _countof(StateChecks);
    return StateChecks;
}
