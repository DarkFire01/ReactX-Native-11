/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 11 renderer behind a Direct2D device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "d2dobjects.hpp"

#include "PrimitiveVS.h"
#include "SolidPS.h"
#include "TexturePS.h"

constexpr UINT32 RxInitialVertices = 4096;

struct RxPaintConstants
{
    FLOAT Color[4];
    FLOAT MapU[4];
    FLOAT MapV[4];
    FLOAT Misc[4];
};

RxRenderer::RxRenderer() : ScratchWidth(0), ScratchHeight(0), VertexCapacity(0), VertexOffset(0), Swapped(FALSE)
{
    InitializeSRWLock(&Lock);
}

RxRenderer::~RxRenderer()
{
}

HRESULT
RxRenderer::Initialize(
    _In_ ID3D11Device1* Device)
{
    static const D3D11_INPUT_ELEMENT_DESC Elements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COVERAGE", 0, DXGI_FORMAT_R32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    static const D3D_FEATURE_LEVEL Levels[] =
    {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1,
    };
    D3D11_BUFFER_DESC Buffer = {};
    D3D11_BLEND_DESC Blend = {};
    D3D11_RASTERIZER_DESC Raster = {};
    ID3D11DeviceContext1* Immediate;
    HRESULT hr;

    D3DDevice = RxComPtr<ID3D11Device1>(Device);
    Device->GetImmediateContext1(&Immediate);
    D3DContext.Attach(Immediate);

    /* Our own pipeline state, swapped in while we draw, so the application's bindings survive */
    {
        D3D_FEATURE_LEVEL Chosen;
        UINT Flags = (Device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED) ?
                         D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0;

        hr = Device->CreateDeviceContextState(Flags, Levels, _countof(Levels), D3D11_SDK_VERSION,
                                              __uuidof(ID3D11Device1), &Chosen, OwnState.Put());
        if (FAILED(hr))
        {
            RX_TRACE(L"CreateDeviceContextState failed 0x%08lX, drawing in the caller's state", hr);
            OwnState.Reset();
        }
    }

    hr = Device->CreateVertexShader(g_PrimitiveVS, sizeof(g_PrimitiveVS), nullptr, VertexShader.Put());
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(g_SolidPS, sizeof(g_SolidPS), nullptr, SolidShader.Put());
    if (SUCCEEDED(hr))
        hr = Device->CreatePixelShader(g_TexturePS, sizeof(g_TexturePS), nullptr, TextureShader.Put());
    if (SUCCEEDED(hr))
        hr = Device->CreateInputLayout(Elements, _countof(Elements), g_PrimitiveVS, sizeof(g_PrimitiveVS), Layout.Put());
    if (FAILED(hr))
        return hr;

    Buffer.ByteWidth = RxInitialVertices * sizeof(RxVertex);
    Buffer.Usage = D3D11_USAGE_DYNAMIC;
    Buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    Buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = Device->CreateBuffer(&Buffer, nullptr, Vertices.Put());
    if (FAILED(hr))
        return hr;
    VertexCapacity = RxInitialVertices;

    Buffer.ByteWidth = 16;
    Buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = Device->CreateBuffer(&Buffer, nullptr, VertexConstants.Put());
    if (FAILED(hr))
        return hr;
    Buffer.ByteWidth = sizeof(RxPaintConstants);
    hr = Device->CreateBuffer(&Buffer, nullptr, PixelConstants.Put());
    if (FAILED(hr))
        return hr;

    /* Premultiplied source over, and a straight copy for clears inside clips */
    Blend.RenderTarget[0].BlendEnable = TRUE;
    Blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    Blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    Blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    Blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    Blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    Blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = Device->CreateBlendState(&Blend, SourceOver.Put());
    if (FAILED(hr))
        return hr;
    Blend.RenderTarget[0].BlendEnable = FALSE;
    hr = Device->CreateBlendState(&Blend, CopyBlend.Put());
    if (FAILED(hr))
        return hr;

    Raster.FillMode = D3D11_FILL_SOLID;
    Raster.CullMode = D3D11_CULL_NONE;
    Raster.ScissorEnable = TRUE;
    Raster.DepthClipEnable = TRUE;
    hr = Device->CreateRasterizerState(&Raster, Rasterizer.Put());
    if (FAILED(hr))
        return hr;

    {
        static const struct
        {
            D3D11_FILTER Filter;
            D3D11_TEXTURE_ADDRESS_MODE Address;
        } Modes[RxSamplerCount] =
        {
            { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP },
            { D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP },
            { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP },
            { D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR },
        };

        for (UINT32 Index = 0; Index < RxSamplerCount; Index++)
        {
            D3D11_SAMPLER_DESC Sampler = {};

            Sampler.Filter = Modes[Index].Filter;
            Sampler.AddressU = Modes[Index].Address;
            Sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            Sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            Sampler.MaxLOD = D3D11_FLOAT32_MAX;
            Sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
            hr = Device->CreateSamplerState(&Sampler, Samplers[Index].Put());
            if (FAILED(hr))
                return hr;
        }
    }
    return S_OK;
}

HRESULT
RxRenderer::Begin(
    _In_ ID3D11RenderTargetView* Target,
    _In_ UINT32 Width,
    _In_ UINT32 Height)
{
    D3D11_VIEWPORT Viewport = { 0.0f, 0.0f, static_cast<FLOAT>(Width), static_cast<FLOAT>(Height), 0.0f, 1.0f };
    D3D11_MAPPED_SUBRESOURCE Mapped;
    UINT Stride = sizeof(RxVertex);
    UINT Offset = 0;
    ID3D11Buffer* Buffer = Vertices.Get();
    ID3D11Buffer* VsConstants = VertexConstants.Get();
    ID3D11Buffer* PsConstants = PixelConstants.Get();

    if (OwnState.Get() && !Swapped)
    {
        D3DContext->SwapDeviceContextState(OwnState.Get(), SavedState.Put());
        Swapped = TRUE;
    }

    if (SUCCEEDED(D3DContext->Map(VertexConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        FLOAT* Scale = static_cast<FLOAT*>(Mapped.pData);

        Scale[0] = 2.0f / Width;
        Scale[1] = -2.0f / Height;
        Scale[2] = 0.0f;
        Scale[3] = 0.0f;
        D3DContext->Unmap(VertexConstants.Get(), 0);
    }

    D3DContext->OMSetRenderTargets(1, &Target, nullptr);
    D3DContext->RSSetViewports(1, &Viewport);
    D3DContext->RSSetState(Rasterizer.Get());
    D3DContext->IASetInputLayout(Layout.Get());
    D3DContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3DContext->IASetVertexBuffers(0, 1, &Buffer, &Stride, &Offset);
    D3DContext->VSSetShader(VertexShader.Get(), nullptr, 0);
    D3DContext->VSSetConstantBuffers(0, 1, &VsConstants);
    D3DContext->PSSetConstantBuffers(0, 1, &PsConstants);
    D3DContext->OMSetBlendState(SourceOver.Get(), nullptr, MAXUINT32);

    {
        RECT Full = { 0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height) };

        SetClip(Full);
    }
    return S_OK;
}

VOID
RxRenderer::End(VOID)
{
    ID3D11RenderTargetView* None = nullptr;

    D3DContext->OMSetRenderTargets(1, &None, nullptr);
    if (Swapped)
    {
        D3DContext->SwapDeviceContextState(SavedState.Get(), nullptr);
        SavedState.Reset();
        Swapped = FALSE;
    }
}

VOID
RxRenderer::SetClip(
    _In_ const RECT& Clip)
{
    D3D11_RECT Rect = Clip;

    D3DContext->RSSetScissorRects(1, &Rect);
}

VOID
RxRenderer::Draw(
    _In_reads_(Count) const RxVertex* Source,
    _In_ UINT32 Count,
    _In_ const RxPaint& Paint,
    _In_ BOOLEAN Copy)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    D3D11_MAP Mode = D3D11_MAP_WRITE_NO_OVERWRITE;

    if (Count == 0)
        return;

    /* Grow the buffer for large batches */
    if (Count > VertexCapacity)
    {
        D3D11_BUFFER_DESC Buffer = {};
        UINT Stride = sizeof(RxVertex);
        UINT Offset = 0;
        ID3D11Buffer* Bound;

        Buffer.ByteWidth = Count * sizeof(RxVertex);
        Buffer.Usage = D3D11_USAGE_DYNAMIC;
        Buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        Buffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(D3DDevice->CreateBuffer(&Buffer, nullptr, Vertices.Put())))
            return;
        VertexCapacity = Count;
        VertexOffset = 0;
        Bound = Vertices.Get();
        D3DContext->IASetVertexBuffers(0, 1, &Bound, &Stride, &Offset);
    }

    if (VertexOffset + Count > VertexCapacity)
    {
        VertexOffset = 0;
        Mode = D3D11_MAP_WRITE_DISCARD;
    }
    if (VertexOffset == 0)
        Mode = D3D11_MAP_WRITE_DISCARD;

    if (FAILED(D3DContext->Map(Vertices.Get(), 0, Mode, 0, &Mapped)))
        return;
    memcpy(static_cast<RxVertex*>(Mapped.pData) + VertexOffset, Source, Count * sizeof(RxVertex));
    D3DContext->Unmap(Vertices.Get(), 0);

    if (SUCCEEDED(D3DContext->Map(PixelConstants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
    {
        RxPaintConstants* Constants = static_cast<RxPaintConstants*>(Mapped.pData);

        memcpy(Constants->Color, Paint.Color, sizeof(Constants->Color));
        Constants->MapU[0] = Paint.Uv[0];
        Constants->MapU[1] = Paint.Uv[1];
        Constants->MapU[2] = Paint.Uv[2];
        Constants->MapU[3] = 0.0f;
        Constants->MapV[0] = Paint.Uv[3];
        Constants->MapV[1] = Paint.Uv[4];
        Constants->MapV[2] = Paint.Uv[5];
        Constants->MapV[3] = 0.0f;
        Constants->Misc[0] = Paint.Opacity;
        Constants->Misc[1] = Constants->Misc[2] = Constants->Misc[3] = 0.0f;
        D3DContext->Unmap(PixelConstants.Get(), 0);
    }

    if (Paint.Texture != nullptr)
    {
        ID3D11ShaderResourceView* View = Paint.Texture;
        ID3D11SamplerState* Sampler = Samplers[Paint.Sampler < RxSamplerCount ? Paint.Sampler : 0].Get();

        D3DContext->PSSetShader(TextureShader.Get(), nullptr, 0);
        D3DContext->PSSetShaderResources(0, 1, &View);
        D3DContext->PSSetSamplers(0, 1, &Sampler);
    }
    else
    {
        D3DContext->PSSetShader(SolidShader.Get(), nullptr, 0);
    }

    D3DContext->OMSetBlendState(Copy ? CopyBlend.Get() : SourceOver.Get(), nullptr, MAXUINT32);
    D3DContext->Draw(Count, VertexOffset);
    VertexOffset += Count;

    if (Paint.Texture != nullptr)
    {
        ID3D11ShaderResourceView* None = nullptr;

        D3DContext->PSSetShaderResources(0, 1, &None);
    }
}

VOID
RxRenderer::ClearTarget(
    _In_reads_(4) const FLOAT* Color)
{
    ID3D11RenderTargetView* Target = nullptr;

    D3DContext->OMGetRenderTargets(1, &Target, nullptr);
    if (Target != nullptr)
    {
        D3DContext->ClearRenderTargetView(Target, Color);
        Target->Release();
    }
}

ID3D11ShaderResourceView*
RxRenderer::Upload(
    _In_reads_(Width * Height) const UINT32* Pixels,
    _In_ UINT32 Width,
    _In_ UINT32 Height,
    _Out_ UINT32* TextureWidth,
    _Out_ UINT32* TextureHeight)
{
    D3D11_BOX Box = { 0, 0, 0, Width, Height, 1 };

    *TextureWidth = 0;
    *TextureHeight = 0;
    if (Width == 0 || Height == 0)
        return nullptr;

    if (Width > ScratchWidth || Height > ScratchHeight)
    {
        D3D11_TEXTURE2D_DESC Desc = {};

        Desc.Width = std::max(Width, std::max<UINT32>(ScratchWidth, 512));
        Desc.Height = std::max(Height, std::max<UINT32>(ScratchHeight, 128));
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_DEFAULT;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        ScratchView.Reset();
        Scratch.Reset();
        if (FAILED(D3DDevice->CreateTexture2D(&Desc, nullptr, Scratch.Put())) ||
            FAILED(D3DDevice->CreateShaderResourceView(Scratch.Get(), nullptr, ScratchView.Put())))
        {
            ScratchWidth = ScratchHeight = 0;
            return nullptr;
        }
        ScratchWidth = Desc.Width;
        ScratchHeight = Desc.Height;
    }

    D3DContext->UpdateSubresource(Scratch.Get(), 0, &Box, Pixels, Width * 4, 0);
    *TextureWidth = ScratchWidth;
    *TextureHeight = ScratchHeight;
    return ScratchView.Get();
}
