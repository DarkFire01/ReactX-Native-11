/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Small Direct3D 11 scenes, each exercising a little more of the pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scenes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <vector>

#include "ColorVS.h"
#include "ColorPS.h"
#include "PlacedVS.h"
#include "TexturedVS.h"
#include "TexturedPS.h"
#include "MeshVS.h"
#include "CubeVS.h"
#include "CubePS.h"
#include "BackdropVS.h"
#include "BackdropPS.h"
#include "InstancedVS.h"
#include "PatternCS.h"
#include "FullscreenVS.h"
#include "WrittenPS.h"
#include "PointVS.h"
#include "SpriteGS.h"
#include "ControlVS.h"
#include "TriangleHS.h"
#include "TriangleDS.h"
#include "SpinVS.h"
#include "CapturedVS.h"
#include "VertexColorPS.h"
#include "VolumePS.h"

constexpr float TdPi = 3.14159265f;

HRESULT
TdCheck(
    _In_ HRESULT Result,
    _In_z_ PCSTR What)
{
    if (FAILED(Result))
        printf("error: %s failed 0x%08lX\n", What, Result);
    return Result;
}

#define TD_TRY(Expression)                                 \
    do                                                     \
    {                                                      \
        HRESULT TdResult = TdCheck((Expression), #Expression); \
        if (FAILED(TdResult))                              \
            return TdResult;                               \
    } while (0)

/* Shared pieces */

struct ColorVertex
{
    float X, Y;
    float R, G, B, A;
};

static const D3D11_INPUT_ELEMENT_DESC ColorLayout[] =
{
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static
HRESULT
TdCreateBuffer(
    _In_ ID3D11Device* Device,
    _In_ UINT BindFlags,
    _In_ UINT Size,
    _In_opt_ const VOID* Data,
    _Outptr_ ID3D11Buffer** Buffer)
{
    D3D11_BUFFER_DESC Desc = {};
    D3D11_SUBRESOURCE_DATA Initial = {};

    Desc.ByteWidth = Size;
    Desc.Usage = Data ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
    Desc.BindFlags = BindFlags;
    Initial.pSysMem = Data;
    return Device->CreateBuffer(&Desc, Data ? &Initial : nullptr, Buffer);
}

static
VOID
TdSetViewport(
    _In_ const SceneTarget& Target)
{
    D3D11_VIEWPORT Viewport = { 0.0f, 0.0f, (float)Target.Width, (float)Target.Height, 0.0f, 1.0f };

    Target.Context->RSSetViewports(1, &Viewport);
}

/* clear: the back buffer and nothing else */

class ClearScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        UNREFERENCED_PARAMETER(Target);
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        const FLOAT Color[4] = { 0.1f, (Frame % 60) / 60.0f, 0.4f, 1.0f };

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Color);
    }
};

/* triangle: one vertex buffer, one input layout, the simplest shaders */

class TriangleScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const ColorVertex Vertices[] =
        {
            {  0.0f,  0.7f, 1.0f, 0.0f, 0.0f, 1.0f },
            {  0.6f, -0.6f, 0.0f, 1.0f, 0.0f, 1.0f },
            { -0.6f, -0.6f, 0.0f, 0.0f, 1.0f, 1.0f },
        };

        TD_TRY(Target.Device->CreateVertexShader(g_ColorVS, sizeof(g_ColorVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_ColorPS, sizeof(g_ColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(ColorLayout, _countof(ColorLayout), g_ColorVS, sizeof(g_ColorVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.05f, 0.05f, 0.08f, 1.0f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        UINT Stride = sizeof(ColorVertex);
        UINT Offset = 0;

        UNREFERENCED_PARAMETER(Frame);

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->Draw(3, 0);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
};

/* cbuffer: the same mesh drawn twice, placed by a dynamic and by a default constant buffer */

struct Placement
{
    float OffsetX, OffsetY;
    float Scale;
    float Angle;
    float Tint[4];
};

class ConstantBufferScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const ColorVertex Vertices[] =
        {
            {  0.0f,  0.5f, 1.0f, 1.0f, 1.0f, 1.0f },
            {  0.5f, -0.4f, 1.0f, 0.5f, 0.2f, 1.0f },
            { -0.5f, -0.4f, 0.2f, 0.5f, 1.0f, 1.0f },
        };
        D3D11_BUFFER_DESC Desc = {};

        TD_TRY(Target.Device->CreateVertexShader(g_PlacedVS, sizeof(g_PlacedVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_ColorPS, sizeof(g_ColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(ColorLayout, _countof(ColorLayout), g_PlacedVS, sizeof(g_PlacedVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));

        Desc.ByteWidth = sizeof(Placement);
        Desc.Usage = D3D11_USAGE_DYNAMIC;
        Desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        TD_TRY(Target.Device->CreateBuffer(&Desc, nullptr, &DynamicPlacement));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(Placement), nullptr, &DefaultPlacement));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.08f, 0.05f, 0.05f, 1.0f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        D3D11_MAPPED_SUBRESOURCE Mapped;
        Placement Left = { -0.45f, 0.0f, 0.8f, Frame * 0.05f, { 1.0f, 0.6f, 0.6f, 1.0f } };
        Placement Right = { 0.45f, 0.1f, 0.6f, -(Frame * 0.08f), { 0.6f, 1.0f, 0.6f, 1.0f } };
        UINT Stride = sizeof(ColorVertex);
        UINT Offset = 0;

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);

        if (SUCCEEDED(TdCheck(Target.Context->Map(DynamicPlacement.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped),
                              "Map(constant buffer)")))
        {
            memcpy(Mapped.pData, &Left, sizeof(Left));
            Target.Context->Unmap(DynamicPlacement.Get(), 0);
        }
        Target.Context->VSSetConstantBuffers(0, 1, DynamicPlacement.GetAddressOf());
        Target.Context->Draw(3, 0);

        Target.Context->UpdateSubresource(DefaultPlacement.Get(), 0, nullptr, &Right, 0, 0);
        Target.Context->VSSetConstantBuffers(0, 1, DefaultPlacement.GetAddressOf());
        Target.Context->Draw(3, 0);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> DynamicPlacement;
    ComPtr<ID3D11Buffer> DefaultPlacement;
};

/* texture: an indexed quad sampling a generated checkerboard */

struct TexturedVertex
{
    float X, Y;
    float U, V;
};

class TextureScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const TexturedVertex Vertices[] =
        {
            { -0.8f,  0.8f, 0.0f, 0.0f },
            {  0.8f,  0.8f, 2.0f, 0.0f },
            {  0.8f, -0.8f, 2.0f, 2.0f },
            { -0.8f, -0.8f, 0.0f, 2.0f },
        };
        static const UINT16 Indices[] = { 0, 1, 2, 0, 2, 3 };
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        constexpr UINT Size = 32;
        D3D11_TEXTURE2D_DESC TextureDesc = {};
        D3D11_SUBRESOURCE_DATA Initial = {};
        D3D11_SAMPLER_DESC SamplerDesc = {};
        UINT32 Pixels[Size * Size];
        UINT X;
        UINT Y;

        /* Checks with a gradient through them, so filtering and addressing both show */
        for (Y = 0; Y < Size; Y++)
        {
            for (X = 0; X < Size; X++)
            {
                BOOLEAN IsLight = ((X / 4) + (Y / 4)) % 2 == 0;
                UINT32 Red = IsLight ? 255 : X * 8;
                UINT32 Green = IsLight ? 255 : Y * 8;
                UINT32 Blue = IsLight ? 255 : 128;

                Pixels[Y * Size + X] = 0xFF000000 | (Blue << 16) | (Green << 8) | Red;
            }
        }

        TextureDesc.Width = Size;
        TextureDesc.Height = Size;
        TextureDesc.MipLevels = 1;
        TextureDesc.ArraySize = 1;
        TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        TextureDesc.SampleDesc.Count = 1;
        TextureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Initial.pSysMem = Pixels;
        Initial.SysMemPitch = Size * sizeof(UINT32);

        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

        TD_TRY(Target.Device->CreateVertexShader(g_TexturedVS, sizeof(g_TexturedVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_TexturedPS, sizeof(g_TexturedPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_TexturedVS, sizeof(g_TexturedVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_INDEX_BUFFER, sizeof(Indices), Indices, &IndexBuffer));
        TD_TRY(Target.Device->CreateTexture2D(&TextureDesc, &Initial, &Texture));
        TD_TRY(Target.Device->CreateShaderResourceView(Texture.Get(), nullptr, &TextureView));
        TD_TRY(Target.Device->CreateSamplerState(&SamplerDesc, &Sampler));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        UINT Stride = sizeof(TexturedVertex);
        UINT Offset = 0;

        UNREFERENCED_PARAMETER(Frame);

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->PSSetShaderResources(0, 1, TextureView.GetAddressOf());
        Target.Context->PSSetSamplers(0, 1, Sampler.GetAddressOf());
        Target.Context->DrawIndexed(6, 0, 0);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<ID3D11ShaderResourceView> TextureView;
    ComPtr<ID3D11SamplerState> Sampler;
};

/* depth: two intersecting cubes sorted by a depth buffer, culled by the rasterizer */

struct Matrix
{
    float M[4][4];
};

static
Matrix
TdMultiply(
    _In_ const Matrix& A,
    _In_ const Matrix& B)
{
    Matrix Result = {};
    UINT Row;
    UINT Column;
    UINT Index;

    for (Row = 0; Row < 4; Row++)
    {
        for (Column = 0; Column < 4; Column++)
        {
            for (Index = 0; Index < 4; Index++)
                Result.M[Row][Column] += A.M[Row][Index] * B.M[Index][Column];
        }
    }

    return Result;
}

/* Row vector convention: a point is transformed as point * matrix */
static
Matrix
TdTransform(
    _In_ float AngleY,
    _In_ float AngleX,
    _In_ float X,
    _In_ float Y,
    _In_ float Z)
{
    Matrix TurnY = { { { cosf(AngleY), 0, -sinf(AngleY), 0 }, { 0, 1, 0, 0 }, { sinf(AngleY), 0, cosf(AngleY), 0 }, { 0, 0, 0, 1 } } };
    Matrix TurnX = { { { 1, 0, 0, 0 }, { 0, cosf(AngleX), sinf(AngleX), 0 }, { 0, -sinf(AngleX), cosf(AngleX), 0 }, { 0, 0, 0, 1 } } };
    Matrix Move = { { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { X, Y, Z, 1 } } };

    return TdMultiply(TdMultiply(TurnY, TurnX), Move);
}

static
Matrix
TdPerspective(
    _In_ float FieldOfView,
    _In_ float Aspect,
    _In_ float Near,
    _In_ float Far)
{
    float Height = 1.0f / tanf(FieldOfView / 2.0f);
    Matrix Result = {};

    Result.M[0][0] = Height / Aspect;
    Result.M[1][1] = Height;
    Result.M[2][2] = Far / (Far - Near);
    Result.M[2][3] = 1.0f;
    Result.M[3][2] = -Near * Far / (Far - Near);
    return Result;
}

/* HLSL reads constant buffer matrices column by column */
static
Matrix
TdTranspose(
    _In_ const Matrix& Source)
{
    Matrix Result;
    UINT Row;
    UINT Column;

    for (Row = 0; Row < 4; Row++)
    {
        for (Column = 0; Column < 4; Column++)
            Result.M[Row][Column] = Source.M[Column][Row];
    }

    return Result;
}

class DepthScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        struct MeshVertex
        {
            float X, Y, Z;
            float R, G, B;
        };
        static const MeshVertex Vertices[] =
        {
            { -1, -1, -1, 1, 0, 0 }, { -1,  1, -1, 0, 1, 0 }, {  1,  1, -1, 0, 0, 1 }, {  1, -1, -1, 1, 1, 0 },
            { -1, -1,  1, 1, 0, 1 }, { -1,  1,  1, 0, 1, 1 }, {  1,  1,  1, 1, 1, 1 }, {  1, -1,  1, 0.2f, 0.2f, 0.2f },
        };
        static const UINT16 Indices[] =
        {
            0, 1, 2, 0, 2, 3,   4, 6, 5, 4, 7, 6,   4, 5, 1, 4, 1, 0,
            3, 2, 6, 3, 6, 7,   1, 5, 6, 1, 6, 2,   4, 0, 3, 4, 3, 7,
        };
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        D3D11_TEXTURE2D_DESC DepthDesc = {};
        D3D11_RASTERIZER_DESC RasterizerDesc = {};
        D3D11_DEPTH_STENCIL_DESC DepthStateDesc = {};

        DepthDesc.Width = Target.Width;
        DepthDesc.Height = Target.Height;
        DepthDesc.MipLevels = 1;
        DepthDesc.ArraySize = 1;
        DepthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        DepthDesc.SampleDesc.Count = 1;
        DepthDesc.Usage = D3D11_USAGE_DEFAULT;
        DepthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        RasterizerDesc.FillMode = D3D11_FILL_SOLID;
        RasterizerDesc.CullMode = D3D11_CULL_BACK;
        RasterizerDesc.DepthClipEnable = TRUE;

        DepthStateDesc.DepthEnable = TRUE;
        DepthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        DepthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;

        TD_TRY(Target.Device->CreateVertexShader(g_MeshVS, sizeof(g_MeshVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_ColorPS, sizeof(g_ColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_MeshVS, sizeof(g_MeshVS), &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_INDEX_BUFFER, sizeof(Indices), Indices, &IndexBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(Matrix), nullptr, &Camera));
        TD_TRY(Target.Device->CreateTexture2D(&DepthDesc, nullptr, &Depth));
        TD_TRY(Target.Device->CreateDepthStencilView(Depth.Get(), nullptr, &DepthView));
        TD_TRY(Target.Device->CreateRasterizerState(&RasterizerDesc, &Rasterizer));
        TD_TRY(Target.Device->CreateDepthStencilState(&DepthStateDesc, &DepthState));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        Matrix Projection = TdPerspective(TdPi / 3.0f, (float)Target.Width / Target.Height, 0.5f, 20.0f);
        Matrix WorldViewProjection;
        UINT Stride = 6 * sizeof(float);
        UINT Offset = 0;
        float Angle = Frame * 0.03f;

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->ClearDepthStencilView(DepthView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, DepthView.Get());
        Target.Context->OMSetDepthStencilState(DepthState.Get(), 0);
        Target.Context->RSSetState(Rasterizer.Get());
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->VSSetConstantBuffers(1, 1, Camera.GetAddressOf());

        WorldViewProjection = TdTranspose(TdMultiply(TdTransform(Angle, 0.5f, -0.6f, 0.0f, 6.0f), Projection));
        Target.Context->UpdateSubresource(Camera.Get(), 0, nullptr, &WorldViewProjection, 0, 0);
        Target.Context->DrawIndexed(36, 0, 0);

        WorldViewProjection = TdTranspose(TdMultiply(TdTransform(-Angle, 0.3f, 0.6f, 0.2f, 6.5f), Projection));
        Target.Context->UpdateSubresource(Camera.Get(), 0, nullptr, &WorldViewProjection, 0, 0);
        Target.Context->DrawIndexed(36, 0, 0);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    ComPtr<ID3D11Buffer> Camera;
    ComPtr<ID3D11Texture2D> Depth;
    ComPtr<ID3D11DepthStencilView> DepthView;
    ComPtr<ID3D11RasterizerState> Rasterizer;
    ComPtr<ID3D11DepthStencilState> DepthState;
};

/* blend: translucent quads over an opaque triangle */

class BlendScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const ColorVertex Vertices[] =
        {
            /* Opaque triangle */
            {  0.0f,  0.9f, 1.0f, 1.0f, 0.0f, 1.0f }, { 0.9f, -0.9f, 1.0f, 1.0f, 0.0f, 1.0f },
            { -0.9f, -0.9f, 1.0f, 1.0f, 0.0f, 1.0f },
            /* Red quad at half alpha */
            { -0.7f,  0.5f, 1.0f, 0.0f, 0.0f, 0.5f }, { 0.3f,  0.5f, 1.0f, 0.0f, 0.0f, 0.5f },
            {  0.3f, -0.5f, 1.0f, 0.0f, 0.0f, 0.5f }, { -0.7f,  0.5f, 1.0f, 0.0f, 0.0f, 0.5f },
            {  0.3f, -0.5f, 1.0f, 0.0f, 0.0f, 0.5f }, { -0.7f, -0.5f, 1.0f, 0.0f, 0.0f, 0.5f },
            /* Blue quad at a quarter alpha */
            { -0.3f,  0.7f, 0.0f, 0.2f, 1.0f, 0.25f }, { 0.7f,  0.7f, 0.0f, 0.2f, 1.0f, 0.25f },
            {  0.7f, -0.3f, 0.0f, 0.2f, 1.0f, 0.25f }, { -0.3f,  0.7f, 0.0f, 0.2f, 1.0f, 0.25f },
            {  0.7f, -0.3f, 0.0f, 0.2f, 1.0f, 0.25f }, { -0.3f, -0.3f, 0.0f, 0.2f, 1.0f, 0.25f },
        };
        D3D11_BLEND_DESC BlendDesc = {};

        BlendDesc.RenderTarget[0].BlendEnable = TRUE;
        BlendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        BlendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        BlendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        BlendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        BlendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        BlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        TD_TRY(Target.Device->CreateVertexShader(g_ColorVS, sizeof(g_ColorVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_ColorPS, sizeof(g_ColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(ColorLayout, _countof(ColorLayout), g_ColorVS, sizeof(g_ColorVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));
        TD_TRY(Target.Device->CreateBlendState(&BlendDesc, &Blend));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        UINT Stride = sizeof(ColorVertex);
        UINT Offset = 0;

        UNREFERENCED_PARAMETER(Frame);

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);

        Target.Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        Target.Context->Draw(3, 0);
        Target.Context->OMSetBlendState(Blend.Get(), nullptr, 0xFFFFFFFF);
        Target.Context->Draw(12, 3);
        Target.Context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11BlendState> Blend;
};

/* cube: a lit, textured, multisampled cube turning over a backdrop */

struct Vector3
{
    float X, Y, Z;
};

static
Vector3
TdCross(
    _In_ const Vector3& A,
    _In_ const Vector3& B)
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}

static
float
TdDot(
    _In_ const Vector3& A,
    _In_ const Vector3& B)
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

static
Vector3
TdNormalize(
    _In_ const Vector3& V)
{
    float Length = sqrtf(TdDot(V, V));

    return { V.X / Length, V.Y / Length, V.Z / Length };
}

/* Left handed view: the camera looks down +Z of its own space */
static
Matrix
TdLookAt(
    _In_ const Vector3& Eye,
    _In_ const Vector3& Target,
    _In_ const Vector3& Up)
{
    Vector3 Forward = TdNormalize({ Target.X - Eye.X, Target.Y - Eye.Y, Target.Z - Eye.Z });
    Vector3 Right = TdNormalize(TdCross(Up, Forward));
    Vector3 Above = TdCross(Forward, Right);

    return { { { Right.X, Above.X, Forward.X, 0 },
               { Right.Y, Above.Y, Forward.Y, 0 },
               { Right.Z, Above.Z, Forward.Z, 0 },
               { -TdDot(Right, Eye), -TdDot(Above, Eye), -TdDot(Forward, Eye), 1 } } };
}

static
float
TdSrgbToLinear(
    _In_ float Value)
{
    return (Value <= 0.04045f) ? Value / 12.92f : powf((Value + 0.055f) / 1.055f, 2.4f);
}

static
float
TdLinearToSrgb(
    _In_ float Value)
{
    return (Value <= 0.0031308f) ? Value * 12.92f : 1.055f * powf(Value, 1.0f / 2.4f) - 0.055f;
}

static
UINT32
TdHash(
    _In_ UINT32 Value)
{
    Value ^= Value >> 16;
    Value *= 0x7FEB352D;
    Value ^= Value >> 15;
    Value *= 0x846CA68B;
    return Value ^ (Value >> 16);
}

class CubeScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        D3D11_RASTERIZER_DESC RasterizerDesc = {};
        D3D11_DEPTH_STENCIL_DESC DepthStateDesc = {};
        D3D11_SAMPLER_DESC SamplerDesc = {};
        UINT Quality = 0;

        TD_TRY(Target.Device->CreateVertexShader(g_CubeVS, sizeof(g_CubeVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_CubePS, sizeof(g_CubePS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateVertexShader(g_BackdropVS, sizeof(g_BackdropVS), nullptr, &BackdropVertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_BackdropPS, sizeof(g_BackdropPS), nullptr, &BackdropPixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_CubeVS, sizeof(g_CubeVS), &Layout));
        TD_TRY(CreateMesh(Target.Device));
        TD_TRY(CreatePicture(Target.Device));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(FrameConstants), nullptr, &Frame));

        /* Four samples when the back buffer format allows them, otherwise draw straight to it */
        Samples = 1;
        if (SUCCEEDED(Target.Device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, 4, &Quality)) &&
            Quality > 0)
        {
            Samples = 4;
        }
        TD_TRY(CreateTargets(Target));

        RasterizerDesc.FillMode = D3D11_FILL_SOLID;
        RasterizerDesc.CullMode = D3D11_CULL_BACK;
        RasterizerDesc.DepthClipEnable = TRUE;
        RasterizerDesc.MultisampleEnable = Samples > 1;
        TD_TRY(Target.Device->CreateRasterizerState(&RasterizerDesc, &Rasterizer));

        DepthStateDesc.DepthEnable = TRUE;
        DepthStateDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        DepthStateDesc.DepthFunc = D3D11_COMPARISON_LESS;
        TD_TRY(Target.Device->CreateDepthStencilState(&DepthStateDesc, &DepthTest));
        DepthStateDesc.DepthEnable = FALSE;
        TD_TRY(Target.Device->CreateDepthStencilState(&DepthStateDesc, &NoDepth));

        SamplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
        SamplerDesc.MaxAnisotropy = 8;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        TD_TRY(Target.Device->CreateSamplerState(&SamplerDesc, &Sampler));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT FrameNumber) override
    {
        static const FLOAT Black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const Vector3 Eye = { 0.0f, 1.7f, -4.6f };
        ID3D11Buffer* Buffers[] = { VertexBuffer.Get() };
        ID3D11RenderTargetView* Color = (Samples > 1) ? ColorView.Get() : Target.BackBuffer;
        FrameConstants Constants = {};
        UINT Stride = sizeof(CubeVertex);
        UINT Offset = 0;

        /* A steady turn around two axes, tied to the frame so every runtime draws the same frame */
        Constants.World = TdTransform(FrameNumber * 0.015f + 0.6f, FrameNumber * 0.009f + 0.45f, 0.0f, 0.0f, 0.0f);
        Constants.ViewProjection = TdMultiply(TdLookAt(Eye, { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }),
                                              TdPerspective(TdPi / 3.2f, (float)Target.Width / Target.Height, 0.1f,
                                                            50.0f));
        Constants.LightDirection = TdNormalize({ 0.55f, -0.75f, 0.4f });
        Constants.CameraPosition = Eye;
        Target.Context->UpdateSubresource(Frame.Get(), 0, nullptr, &Constants, 0, 0);

        Target.Context->ClearRenderTargetView(Color, Black);
        Target.Context->ClearDepthStencilView(DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        Target.Context->OMSetRenderTargets(1, &Color, DepthView.Get());
        TdSetViewport(Target);
        Target.Context->RSSetState(Rasterizer.Get());
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        /* The backdrop needs no vertex data at all */
        Target.Context->OMSetDepthStencilState(NoDepth.Get(), 0);
        Target.Context->IASetInputLayout(nullptr);
        Target.Context->VSSetShader(BackdropVertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(BackdropPixelShader.Get(), nullptr, 0);
        Target.Context->Draw(3, 0);

        Target.Context->OMSetDepthStencilState(DepthTest.Get(), 0);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Buffers, &Stride, &Offset);
        Target.Context->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->VSSetConstantBuffers(2, 1, Frame.GetAddressOf());
        Target.Context->PSSetConstantBuffers(2, 1, Frame.GetAddressOf());
        Target.Context->PSSetShaderResources(0, 1, PictureView.GetAddressOf());
        Target.Context->PSSetSamplers(0, 1, Sampler.GetAddressOf());
        Target.Context->DrawIndexed(36, 0, 0);

        if (Samples > 1)
        {
            Target.Context->OMSetRenderTargets(0, nullptr, nullptr);
            Target.Context->ResolveSubresource(Target.BackBufferTexture, 0, ColorTexture.Get(), 0,
                                               DXGI_FORMAT_B8G8R8A8_UNORM);
        }
    }

private:
    struct CubeVertex
    {
        Vector3 Position;
        Vector3 Normal;
        float U, V;
        Vector3 Tint;
    };

    struct FrameConstants
    {
        Matrix World;
        Matrix ViewProjection;
        Vector3 LightDirection;
        float Padding0;
        Vector3 CameraPosition;
        float Padding1;
    };

    /* Four corners per face so every face keeps its own flat normal */
    HRESULT
    CreateMesh(
        _In_ ID3D11Device* Device)
    {
        static const Vector3 Normals[6] = { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 0, 1 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
        static const Vector3 Ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
        static const Vector3 Tints[6] = { { 1.00f, 0.55f, 0.32f }, { 0.36f, 0.62f, 1.00f }, { 0.45f, 0.86f, 0.48f },
                                          { 1.00f, 0.84f, 0.38f }, { 0.78f, 0.48f, 0.95f }, { 0.92f, 0.92f, 0.94f } };
        CubeVertex Vertices[24];
        UINT16 Indices[36];
        UINT Face;
        UINT Corner;

        for (Face = 0; Face < 6; Face++)
        {
            /* Seen from outside, Right runs left to right and Up bottom to top: clockwise is front facing */
            const Vector3& N = Normals[Face];
            const Vector3& Up = Ups[Face];
            Vector3 Right = TdCross(N, Up);
            static const float CornerSigns[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };

            for (Corner = 0; Corner < 4; Corner++)
            {
                float SideX = CornerSigns[Corner][0];
                float SideY = CornerSigns[Corner][1];
                CubeVertex& Vertex = Vertices[Face * 4 + Corner];

                Vertex.Position = { N.X + Right.X * SideX + Up.X * SideY, N.Y + Right.Y * SideX + Up.Y * SideY,
                                    N.Z + Right.Z * SideX + Up.Z * SideY };
                Vertex.Normal = N;
                Vertex.U = (SideX + 1.0f) * 0.5f;
                Vertex.V = (1.0f - SideY) * 0.5f;
                Vertex.Tint = Tints[Face];
            }

            Indices[Face * 6 + 0] = (UINT16)(Face * 4 + 0);
            Indices[Face * 6 + 1] = (UINT16)(Face * 4 + 1);
            Indices[Face * 6 + 2] = (UINT16)(Face * 4 + 2);
            Indices[Face * 6 + 3] = (UINT16)(Face * 4 + 0);
            Indices[Face * 6 + 4] = (UINT16)(Face * 4 + 2);
            Indices[Face * 6 + 5] = (UINT16)(Face * 4 + 3);
        }

        TD_TRY(TdCreateBuffer(Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &VertexBuffer));
        TD_TRY(TdCreateBuffer(Device, D3D11_BIND_INDEX_BUFFER, sizeof(Indices), Indices, &IndexBuffer));
        return S_OK;
    }

    /*
     * Light tiles with dark grout and a soft bevel, each tile a slightly different shade.
     * Mips are averaged in linear light, so distant faces do not darken.
     */
    HRESULT
    CreatePicture(
        _In_ ID3D11Device* Device)
    {
        constexpr UINT Size = 256;
        constexpr UINT Levels = 9;
        constexpr UINT Tile = 64;
        std::vector<float> Linear(Size * Size * 3);
        std::vector<std::vector<UINT32>> Pixels(Levels);
        D3D11_SUBRESOURCE_DATA Initial[Levels];
        D3D11_TEXTURE2D_DESC Desc = {};
        UINT Level;
        UINT Width;
        UINT X;
        UINT Y;

        for (Y = 0; Y < Size; Y++)
        {
            for (X = 0; X < Size; X++)
            {
                UINT LocalX = X % Tile;
                UINT LocalY = Y % Tile;
                UINT Edge = std::min(std::min(LocalX, Tile - 1 - LocalX), std::min(LocalY, Tile - 1 - LocalY));
                float Shade = 0.80f + 0.12f * ((TdHash((X / Tile) * 7 + (Y / Tile) * 131) & 0xFF) / 255.0f);
                float Grain = ((TdHash(X * 9277 + Y * 26699) & 0xFF) / 255.0f - 0.5f) * 0.04f;
                float Value;

                if (Edge < 3)
                    Value = 0.18f;
                else if (Edge < 7)
                    Value = Shade + ((LocalX < Tile / 2 && LocalY < Tile / 2) ? 0.06f : -0.08f);
                else
                    Value = Shade + Grain;

                Linear[(Y * Size + X) * 3 + 0] = TdSrgbToLinear(Value);
                Linear[(Y * Size + X) * 3 + 1] = TdSrgbToLinear(Value);
                Linear[(Y * Size + X) * 3 + 2] = TdSrgbToLinear(Value * 0.97f);
            }
        }

        for (Level = 0, Width = Size; Level < Levels; Level++, Width /= 2)
        {
            Pixels[Level].resize(Width * Width);
            for (Y = 0; Y < Width; Y++)
            {
                for (X = 0; X < Width; X++)
                {
                    const float* Texel = &Linear[(Y * Width + X) * 3];
                    UINT32 Red = (UINT32)(TdLinearToSrgb(Texel[0]) * 255.0f + 0.5f);
                    UINT32 Green = (UINT32)(TdLinearToSrgb(Texel[1]) * 255.0f + 0.5f);
                    UINT32 Blue = (UINT32)(TdLinearToSrgb(Texel[2]) * 255.0f + 0.5f);

                    Pixels[Level][Y * Width + X] = 0xFF000000 | (Blue << 16) | (Green << 8) | Red;
                }
            }

            Initial[Level].pSysMem = Pixels[Level].data();
            Initial[Level].SysMemPitch = Width * sizeof(UINT32);
            Initial[Level].SysMemSlicePitch = 0;

            /* Box filter the next level down, still in linear light */
            for (Y = 0; Width > 1 && Y < Width / 2; Y++)
            {
                for (X = 0; X < Width / 2; X++)
                {
                    for (UINT Channel = 0; Channel < 3; Channel++)
                    {
                        Linear[(Y * (Width / 2) + X) * 3 + Channel] =
                            (Linear[((2 * Y) * Width + 2 * X) * 3 + Channel] +
                             Linear[((2 * Y) * Width + 2 * X + 1) * 3 + Channel] +
                             Linear[((2 * Y + 1) * Width + 2 * X) * 3 + Channel] +
                             Linear[((2 * Y + 1) * Width + 2 * X + 1) * 3 + Channel]) * 0.25f;
                    }
                }
            }
        }

        Desc.Width = Size;
        Desc.Height = Size;
        Desc.MipLevels = Levels;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        Desc.SampleDesc.Count = 1;
        Desc.Usage = D3D11_USAGE_IMMUTABLE;
        Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        TD_TRY(Device->CreateTexture2D(&Desc, Initial, &Picture));
        TD_TRY(Device->CreateShaderResourceView(Picture.Get(), nullptr, &PictureView));
        return S_OK;
    }

    HRESULT
    CreateTargets(
        _In_ const SceneTarget& Target)
    {
        D3D11_TEXTURE2D_DESC Desc = {};

        Desc.Width = Target.Width;
        Desc.Height = Target.Height;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.SampleDesc.Count = Samples;
        Desc.Usage = D3D11_USAGE_DEFAULT;

        if (Samples > 1)
        {
            Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            Desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            TD_TRY(Target.Device->CreateTexture2D(&Desc, nullptr, &ColorTexture));
            TD_TRY(Target.Device->CreateRenderTargetView(ColorTexture.Get(), nullptr, &ColorView));
        }

        Desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        Desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        TD_TRY(Target.Device->CreateTexture2D(&Desc, nullptr, &Depth));
        TD_TRY(Target.Device->CreateDepthStencilView(Depth.Get(), nullptr, &DepthView));
        return S_OK;
    }

    UINT Samples = 1;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11VertexShader> BackdropVertexShader;
    ComPtr<ID3D11PixelShader> BackdropPixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    ComPtr<ID3D11Buffer> Frame;
    ComPtr<ID3D11Texture2D> Picture;
    ComPtr<ID3D11ShaderResourceView> PictureView;
    ComPtr<ID3D11SamplerState> Sampler;
    ComPtr<ID3D11Texture2D> ColorTexture;
    ComPtr<ID3D11RenderTargetView> ColorView;
    ComPtr<ID3D11Texture2D> Depth;
    ComPtr<ID3D11DepthStencilView> DepthView;
    ComPtr<ID3D11RasterizerState> Rasterizer;
    ComPtr<ID3D11DepthStencilState> DepthTest;
    ComPtr<ID3D11DepthStencilState> NoDepth;
};

/* instancing and indirect: the same grid of quads, with draw counts given directly or read from a buffer */

template <BOOLEAN IsIndirect>
class InstancingScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const float Corners[] = { -1, 1, 1, 1, 1, -1, -1, -1 };
        static const UINT16 Indices[] = { 0, 1, 2, 0, 2, 3 };
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
            { "TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 8, D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        };
        float Instances[InstanceCount][6];
        UINT Index;

        /* An 8 by 8 grid, colors walking around the hue circle */
        for (Index = 0; Index < InstanceCount; Index++)
        {
            float Hue = Index / (float)InstanceCount * 6.0f;

            Instances[Index][0] = -0.8f + (Index % 8) * (1.6f / 7.0f);
            Instances[Index][1] = 0.8f - (Index / 8) * (1.6f / 7.0f);
            Instances[Index][2] = std::clamp(fabsf(Hue - 3.0f) - 1.0f, 0.0f, 1.0f);
            Instances[Index][3] = std::clamp(2.0f - fabsf(Hue - 2.0f), 0.0f, 1.0f);
            Instances[Index][4] = std::clamp(2.0f - fabsf(Hue - 4.0f), 0.0f, 1.0f);
            Instances[Index][5] = 1.0f;
        }

        TD_TRY(Target.Device->CreateVertexShader(g_InstancedVS, sizeof(g_InstancedVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_ColorPS, sizeof(g_ColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_InstancedVS, sizeof(g_InstancedVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Corners), Corners, &CornerBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Instances), Instances, &InstanceBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_INDEX_BUFFER, sizeof(Indices), Indices, &IndexBuffer));

        if (IsIndirect)
        {
            D3D11_BUFFER_DESC Desc = {};

            /* Default usage, so the arguments arrive through UpdateSubresource like GPU written ones would */
            Desc.ByteWidth = 9 * sizeof(UINT);
            Desc.Usage = D3D11_USAGE_DEFAULT;
            Desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
            TD_TRY(Target.Device->CreateBuffer(&Desc, nullptr, &Arguments));
        }
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.06f, 0.06f, 0.06f, 1.0f };
        ID3D11Buffer* Buffers[] = { CornerBuffer.Get(), InstanceBuffer.Get() };
        UINT Strides[] = { 2 * sizeof(float), 6 * sizeof(float) };
        UINT Offsets[] = { 0, 0 };

        /* Indexed: the first 40 instances; plain: triangles for the last 24, starting at instance 40 */
        const UINT Counts[9] = { 6, 40, 0, 0, 0, 3, 24, 0, 40 };

        UNREFERENCED_PARAMETER(Frame);

        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 2, Buffers, Strides, Offsets);
        Target.Context->IASetIndexBuffer(IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);

        if (IsIndirect)
        {
            Target.Context->UpdateSubresource(Arguments.Get(), 0, nullptr, Counts, 0, 0);
            Target.Context->DrawIndexedInstancedIndirect(Arguments.Get(), 0);
            Target.Context->DrawInstancedIndirect(Arguments.Get(), 5 * sizeof(UINT));
        }
        else
        {
            Target.Context->DrawIndexedInstanced(Counts[0], Counts[1], Counts[2], (INT)Counts[3], Counts[4]);
            Target.Context->DrawInstanced(Counts[5], Counts[6], Counts[7], Counts[8]);
        }
    }

private:
    static constexpr UINT InstanceCount = 64;

    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> CornerBuffer;
    ComPtr<ID3D11Buffer> InstanceBuffer;
    ComPtr<ID3D11Buffer> IndexBuffer;
    ComPtr<ID3D11Buffer> Arguments;
};

/* Shared by the shader model 5 scenes: time, frame number and target size at b0 */

struct AnimationConstants
{
    float Time;
    UINT Frame;
    float Width;
    float Height;
};

static
VOID
TdUpdateAnimation(
    _In_ const SceneTarget& Target,
    _In_ ID3D11Buffer* Buffer,
    _In_ UINT Frame)
{
    AnimationConstants Constants = { Frame / 60.0f, Frame, (float)Target.Width, (float)Target.Height };

    Target.Context->UpdateSubresource(Buffer, 0, nullptr, &Constants, 0, 0);
}

/* compute: a compute shader writes the picture through a UAV, a pixel shader shows it */

class ComputeScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        D3D11_TEXTURE2D_DESC Desc = {};

        Desc.Width = Target.Width;
        Desc.Height = Target.Height;
        Desc.MipLevels = 1;
        Desc.ArraySize = 1;
        Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Desc.SampleDesc.Count = 1;
        Desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
        TD_TRY(Target.Device->CreateTexture2D(&Desc, nullptr, &Canvas));
        TD_TRY(Target.Device->CreateUnorderedAccessView(Canvas.Get(), nullptr, &CanvasWrite));
        TD_TRY(Target.Device->CreateShaderResourceView(Canvas.Get(), nullptr, &CanvasRead));
        TD_TRY(Target.Device->CreateComputeShader(g_PatternCS, sizeof(g_PatternCS), nullptr, &Pattern));
        TD_TRY(Target.Device->CreateVertexShader(g_FullscreenVS, sizeof(g_FullscreenVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_WrittenPS, sizeof(g_WrittenPS), nullptr, &PixelShader));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(AnimationConstants), nullptr,
                              &Animation));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        ID3D11UnorderedAccessView* NoView = nullptr;
        ID3D11ShaderResourceView* NoResource = nullptr;

        TdUpdateAnimation(Target, Animation.Get(), Frame);
        Target.Context->CSSetShader(Pattern.Get(), nullptr, 0);
        Target.Context->CSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->CSSetUnorderedAccessViews(0, 1, CanvasWrite.GetAddressOf(), nullptr);
        Target.Context->Dispatch((Target.Width + 7) / 8, (Target.Height + 7) / 8, 1);
        Target.Context->CSSetUnorderedAccessViews(0, 1, &NoView, nullptr);

        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(nullptr);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->PSSetShaderResources(0, 1, CanvasRead.GetAddressOf());
        Target.Context->Draw(3, 0);
        Target.Context->PSSetShaderResources(0, 1, &NoResource);
    }

private:
    ComPtr<ID3D11Texture2D> Canvas;
    ComPtr<ID3D11UnorderedAccessView> CanvasWrite;
    ComPtr<ID3D11ShaderResourceView> CanvasRead;
    ComPtr<ID3D11ComputeShader> Pattern;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11Buffer> Animation;
};

/* geometry: a point list expanded into quads by a geometry shader */

class GeometryScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "PSIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        float Points[PointCount][7];
        UINT Index;

        /* Two rings of sprites, the outer ones larger */
        for (Index = 0; Index < PointCount; Index++)
        {
            float Ring = (Index < PointCount / 2) ? 0.4f : 0.75f;
            float Angle = Index * (2.0f * TdPi / (PointCount / 2));
            float Hue = Index / (float)PointCount * 6.0f;

            Points[Index][0] = cosf(Angle) * Ring * Target.Height / Target.Width;
            Points[Index][1] = sinf(Angle) * Ring;
            Points[Index][2] = std::clamp(fabsf(Hue - 3.0f) - 1.0f, 0.0f, 1.0f);
            Points[Index][3] = std::clamp(2.0f - fabsf(Hue - 2.0f), 0.0f, 1.0f);
            Points[Index][4] = std::clamp(2.0f - fabsf(Hue - 4.0f), 0.0f, 1.0f);
            Points[Index][5] = 1.0f;
            Points[Index][6] = Ring * 0.12f;
        }

        TD_TRY(Target.Device->CreateVertexShader(g_PointVS, sizeof(g_PointVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreateGeometryShader(g_SpriteGS, sizeof(g_SpriteGS), nullptr, &GeometryShader));
        TD_TRY(Target.Device->CreatePixelShader(g_VertexColorPS, sizeof(g_VertexColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_PointVS, sizeof(g_PointVS), &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Points), Points, &VertexBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(AnimationConstants), nullptr,
                              &Animation));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.04f, 0.05f, 0.08f, 1.0f };
        UINT Stride = 7 * sizeof(float);
        UINT Offset = 0;

        TdUpdateAnimation(Target, Animation.Get(), Frame);
        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &Stride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->GSSetShader(GeometryShader.Get(), nullptr, 0);
        Target.Context->GSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->Draw(PointCount, 0);
        Target.Context->GSSetShader(nullptr, nullptr, 0);
    }

private:
    static constexpr UINT PointCount = 24;

    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11GeometryShader> GeometryShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> Animation;
};

/* tessellation: hull and domain shaders subdividing a triangle patch */

class TessellationScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const D3D11_INPUT_ELEMENT_DESC Elements[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        static const float Corners[] = { 0.0f, 0.8f, 0.7f, -0.6f, -0.7f, -0.6f };
        D3D11_RASTERIZER_DESC Wire = {};

        TD_TRY(Target.Device->CreateVertexShader(g_ControlVS, sizeof(g_ControlVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreateHullShader(g_TriangleHS, sizeof(g_TriangleHS), nullptr, &HullShader));
        TD_TRY(Target.Device->CreateDomainShader(g_TriangleDS, sizeof(g_TriangleDS), nullptr, &DomainShader));
        TD_TRY(Target.Device->CreatePixelShader(g_VertexColorPS, sizeof(g_VertexColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(Elements, _countof(Elements), g_ControlVS, sizeof(g_ControlVS),
                                                &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Corners), Corners, &VertexBuffer));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(AnimationConstants), nullptr,
                              &Animation));

        /* The right half shows the same patch as wireframe, so the subdivision itself is compared */
        Wire.FillMode = D3D11_FILL_WIREFRAME;
        Wire.CullMode = D3D11_CULL_NONE;
        Wire.DepthClipEnable = TRUE;
        TD_TRY(Target.Device->CreateRasterizerState(&Wire, &Wireframe));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
        float Half = Target.Width / 2.0f;
        D3D11_VIEWPORT Left = { 0.0f, 0.0f, Half, (float)Target.Height, 0.0f, 1.0f };
        D3D11_VIEWPORT Right = { Half, 0.0f, Half, (float)Target.Height, 0.0f, 1.0f };
        UINT Stride = 2 * sizeof(float);
        UINT Offset = 0;

        TdUpdateAnimation(Target, Animation.Get(), Frame);
        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, VertexBuffer.GetAddressOf(), &Stride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->HSSetShader(HullShader.Get(), nullptr, 0);
        Target.Context->HSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->DSSetShader(DomainShader.Get(), nullptr, 0);
        Target.Context->DSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);

        Target.Context->RSSetViewports(1, &Left);
        Target.Context->RSSetState(nullptr);
        Target.Context->Draw(3, 0);
        Target.Context->RSSetViewports(1, &Right);
        Target.Context->RSSetState(Wireframe.Get());
        Target.Context->Draw(3, 0);

        Target.Context->RSSetState(nullptr);
        Target.Context->HSSetShader(nullptr, nullptr, 0);
        Target.Context->DSSetShader(nullptr, nullptr, 0);
    }

private:
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11HullShader> HullShader;
    ComPtr<ID3D11DomainShader> DomainShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> VertexBuffer;
    ComPtr<ID3D11Buffer> Animation;
    ComPtr<ID3D11RasterizerState> Wireframe;
};

/* streamout: vertex shader output captured into a buffer, then drawn from it with DrawAuto */

class StreamOutputScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const D3D11_SO_DECLARATION_ENTRY Declaration[] =
        {
            { 0, "SV_Position", 0, 0, 4, 0 },
            { 0, "COLOR", 0, 0, 4, 0 },
        };
        static const D3D11_INPUT_ELEMENT_DESC CapturedLayout[] =
        {
            { "CAPTURED", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ColorVertex Vertices[VertexCount];
        UINT Stride = CapturedStride;
        UINT Index;

        /* Four triangles of different sizes, each turned at its own speed by the vertex shader */
        for (Index = 0; Index < VertexCount; Index++)
        {
            float Size = 0.25f + 0.17f * (Index / 3);
            float Angle = (Index / 3) * 0.4f - (Index % 3) * (2.0f * TdPi / 3.0f);

            Vertices[Index] = { cosf(Angle) * Size, sinf(Angle) * Size, (Index % 3) == 0 ? 1.0f : 0.2f,
                                (Index % 3) == 1 ? 1.0f : 0.2f, (Index % 3) == 2 ? 1.0f : 0.2f, 1.0f };
        }

        TD_TRY(Target.Device->CreateVertexShader(g_SpinVS, sizeof(g_SpinVS), nullptr, &SpinShader));
        TD_TRY(Target.Device->CreateGeometryShaderWithStreamOutput(g_SpinVS, sizeof(g_SpinVS), Declaration,
                                                                   _countof(Declaration), &Stride, 1,
                                                                   D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &Capture));
        TD_TRY(Target.Device->CreateVertexShader(g_CapturedVS, sizeof(g_CapturedVS), nullptr, &CapturedShader));
        TD_TRY(Target.Device->CreatePixelShader(g_VertexColorPS, sizeof(g_VertexColorPS), nullptr, &PixelShader));
        TD_TRY(Target.Device->CreateInputLayout(ColorLayout, _countof(ColorLayout), g_SpinVS, sizeof(g_SpinVS),
                                                &SourceLayout));
        TD_TRY(Target.Device->CreateInputLayout(CapturedLayout, _countof(CapturedLayout), g_CapturedVS,
                                                sizeof(g_CapturedVS), &Layout));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_VERTEX_BUFFER, sizeof(Vertices), Vertices, &Source));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER,
                              VertexCount * CapturedStride, nullptr, &Captured));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(AnimationConstants), nullptr,
                              &Animation));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        static const FLOAT Background[4] = { 0.08f, 0.06f, 0.05f, 1.0f };
        UINT SourceStride = sizeof(ColorVertex);
        UINT Stride = CapturedStride;
        UINT Offset = 0;

        /* Capture: nothing is rasterized, so no pixel shader or target is needed */
        TdUpdateAnimation(Target, Animation.Get(), Frame);
        Target.Context->IASetInputLayout(SourceLayout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Source.GetAddressOf(), &SourceStride, &Offset);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(SpinShader.Get(), nullptr, 0);
        Target.Context->VSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->GSSetShader(Capture.Get(), nullptr, 0);
        Target.Context->PSSetShader(nullptr, nullptr, 0);
        Target.Context->SOSetTargets(1, Captured.GetAddressOf(), &Offset);
        Target.Context->Draw(VertexCount, 0);
        Target.Context->SOSetTargets(0, nullptr, nullptr);
        Target.Context->GSSetShader(nullptr, nullptr, 0);

        /* Replay: the driver knows how many vertices landed in the buffer */
        Target.Context->ClearRenderTargetView(Target.BackBuffer, Background);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(Layout.Get());
        Target.Context->IASetVertexBuffers(0, 1, Captured.GetAddressOf(), &Stride, &Offset);
        Target.Context->VSSetShader(CapturedShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->DrawAuto();
    }

private:
    static constexpr UINT VertexCount = 12;
    static constexpr UINT CapturedStride = 8 * sizeof(float);

    ComPtr<ID3D11VertexShader> SpinShader;
    ComPtr<ID3D11GeometryShader> Capture;
    ComPtr<ID3D11VertexShader> CapturedShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> SourceLayout;
    ComPtr<ID3D11InputLayout> Layout;
    ComPtr<ID3D11Buffer> Source;
    ComPtr<ID3D11Buffer> Captured;
    ComPtr<ID3D11Buffer> Animation;
};

/* volume: Texture3D, Texture1D and a typed buffer, all read through shader resource views */

class VolumeScene final : public Scene
{
public:
    HRESULT
    Initialize(
        _In_ const SceneTarget& Target) override
    {
        static const float Tints[4][4] =
        {
            { 1.0f, 1.0f, 1.0f, 1.0f },
            { 1.0f, 0.8f, 0.6f, 1.0f },
            { 0.6f, 0.9f, 1.0f, 1.0f },
            { 0.8f, 1.0f, 0.7f, 1.0f },
        };
        std::vector<BYTE> Density(VolumeSize * VolumeSize * VolumeSize);
        std::vector<UINT32> Colors(PaletteSize);
        D3D11_TEXTURE3D_DESC VolumeDesc = {};
        D3D11_TEXTURE1D_DESC PaletteDesc = {};
        D3D11_BUFFER_DESC TintDesc = {};
        D3D11_SHADER_RESOURCE_VIEW_DESC TintView = {};
        D3D11_SAMPLER_DESC SamplerDesc = {};
        D3D11_SUBRESOURCE_DATA Data = {};
        UINT X;
        UINT Y;
        UINT Z;

        /* Overlapping blobs, so slices change shape as the depth moves */
        for (Z = 0; Z < VolumeSize; Z++)
        {
            for (Y = 0; Y < VolumeSize; Y++)
            {
                for (X = 0; X < VolumeSize; X++)
                {
                    float Value = sinf(X * 0.35f + Z * 0.2f) * cosf(Y * 0.3f - Z * 0.25f) * 0.5f + 0.5f;

                    Density[(Z * VolumeSize + Y) * VolumeSize + X] = (BYTE)(Value * 255.0f);
                }
            }
        }

        for (X = 0; X < PaletteSize; X++)
        {
            float T = X / (float)(PaletteSize - 1);
            UINT32 Red = (UINT32)(255.0f * std::clamp(T * 1.6f, 0.0f, 1.0f));
            UINT32 Green = (UINT32)(255.0f * std::clamp(T * 1.6f - 0.5f, 0.0f, 1.0f));
            UINT32 Blue = (UINT32)(255.0f * std::clamp(0.6f - T, 0.0f, 1.0f));

            Colors[X] = Red | (Green << 8) | (Blue << 16) | 0xFF000000;
        }

        VolumeDesc.Width = VolumeSize;
        VolumeDesc.Height = VolumeSize;
        VolumeDesc.Depth = VolumeSize;
        VolumeDesc.MipLevels = 1;
        VolumeDesc.Format = DXGI_FORMAT_R8_UNORM;
        VolumeDesc.Usage = D3D11_USAGE_IMMUTABLE;
        VolumeDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Data.pSysMem = Density.data();
        Data.SysMemPitch = VolumeSize;
        Data.SysMemSlicePitch = VolumeSize * VolumeSize;
        TD_TRY(Target.Device->CreateTexture3D(&VolumeDesc, &Data, &Volume));
        TD_TRY(Target.Device->CreateShaderResourceView(Volume.Get(), nullptr, &Views[0]));

        PaletteDesc.Width = PaletteSize;
        PaletteDesc.MipLevels = 1;
        PaletteDesc.ArraySize = 1;
        PaletteDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        PaletteDesc.Usage = D3D11_USAGE_IMMUTABLE;
        PaletteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Data.pSysMem = Colors.data();
        Data.SysMemPitch = 0;
        Data.SysMemSlicePitch = 0;
        TD_TRY(Target.Device->CreateTexture1D(&PaletteDesc, &Data, &Palette));
        TD_TRY(Target.Device->CreateShaderResourceView(Palette.Get(), nullptr, &Views[1]));

        TintDesc.ByteWidth = sizeof(Tints);
        TintDesc.Usage = D3D11_USAGE_IMMUTABLE;
        TintDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Data.pSysMem = Tints;
        TD_TRY(Target.Device->CreateBuffer(&TintDesc, &Data, &TintBuffer));
        TintView.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        TintView.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        TintView.Buffer.NumElements = 4;
        TD_TRY(Target.Device->CreateShaderResourceView(TintBuffer.Get(), &TintView, &Views[2]));

        SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        TD_TRY(Target.Device->CreateSamplerState(&SamplerDesc, &Sampler));

        TD_TRY(Target.Device->CreateVertexShader(g_FullscreenVS, sizeof(g_FullscreenVS), nullptr, &VertexShader));
        TD_TRY(Target.Device->CreatePixelShader(g_VolumePS, sizeof(g_VolumePS), nullptr, &PixelShader));
        TD_TRY(TdCreateBuffer(Target.Device, D3D11_BIND_CONSTANT_BUFFER, sizeof(AnimationConstants), nullptr,
                              &Animation));
        return S_OK;
    }

    VOID
    Render(
        _In_ const SceneTarget& Target,
        _In_ UINT Frame) override
    {
        ID3D11ShaderResourceView* Bound[] = { Views[0].Get(), Views[1].Get(), Views[2].Get() };

        TdUpdateAnimation(Target, Animation.Get(), Frame);
        Target.Context->OMSetRenderTargets(1, &Target.BackBuffer, nullptr);
        TdSetViewport(Target);
        Target.Context->IASetInputLayout(nullptr);
        Target.Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Target.Context->VSSetShader(VertexShader.Get(), nullptr, 0);
        Target.Context->PSSetShader(PixelShader.Get(), nullptr, 0);
        Target.Context->PSSetConstantBuffers(0, 1, Animation.GetAddressOf());
        Target.Context->PSSetShaderResources(0, _countof(Bound), Bound);
        Target.Context->PSSetSamplers(0, 1, Sampler.GetAddressOf());
        Target.Context->Draw(3, 0);
    }

private:
    static constexpr UINT VolumeSize = 32;
    static constexpr UINT PaletteSize = 256;

    ComPtr<ID3D11Texture3D> Volume;
    ComPtr<ID3D11Texture1D> Palette;
    ComPtr<ID3D11Buffer> TintBuffer;
    ComPtr<ID3D11ShaderResourceView> Views[3];
    ComPtr<ID3D11SamplerState> Sampler;
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11Buffer> Animation;
};

template <typename SceneType>
static
std::unique_ptr<Scene>
TdMakeScene()
{
    return std::unique_ptr<Scene>(new (std::nothrow) SceneType());
}

static const SceneInfo Scenes[] =
{
    { L"clear", L"ClearRenderTargetView only", TdMakeScene<ClearScene> },
    { L"triangle", L"vertex buffer, input layout, VS and PS, viewport, Draw", TdMakeScene<TriangleScene> },
    { L"cbuffer", L"dynamic and default constant buffers, Map and UpdateSubresource", TdMakeScene<ConstantBufferScene> },
    { L"texture", L"index buffer, texture with initial data, SRV, sampler", TdMakeScene<TextureScene> },
    { L"depth", L"depth buffer, depth stencil state, rasterizer culling", TdMakeScene<DepthScene> },
    { L"blend", L"alpha blending over an opaque draw", TdMakeScene<BlendScene> },
    { L"cube", L"lit, textured, mipmapped, multisampled spinning cube", TdMakeScene<CubeScene> },
    { L"instancing", L"per instance data, DrawInstanced and DrawIndexedInstanced",
      TdMakeScene<InstancingScene<FALSE>> },
    { L"indirect", L"the instancing scene with counts read from a buffer", TdMakeScene<InstancingScene<TRUE>> },
    { L"compute", L"compute shader writing a texture through a UAV, then sampled", TdMakeScene<ComputeScene> },
    { L"geometry", L"geometry shader growing points into quads", TdMakeScene<GeometryScene> },
    { L"tessellation", L"hull and domain shaders, solid and wireframe", TdMakeScene<TessellationScene> },
    { L"streamout", L"stream output from vertex shader bytecode, DrawAuto", TdMakeScene<StreamOutputScene> },
    { L"volume", L"Texture3D, Texture1D and buffer shader resource views", TdMakeScene<VolumeScene> },
};

const SceneInfo*
TdFindScene(
    _In_z_ PCWSTR Name)
{
    for (const SceneInfo& Info : Scenes)
    {
        if (_wcsicmp(Info.Name, Name) == 0)
            return &Info;
    }

    return nullptr;
}

VOID
TdListScenes(VOID)
{
    for (const SceneInfo& Info : Scenes)
        wprintf(L"  %-10s %s\n", Info.Name, Info.Purpose);
}
