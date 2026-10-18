/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shaders for the tinyd3d scenes, compiled to headers by fxc at build time
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

struct ColorVertex
{
    float2 Position : POSITION;
    float4 Color : COLOR;
};

struct ColorPixel
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

cbuffer Placement : register(b0)
{
    float2 Offset;
    float Scale;
    float Angle;
    float4 Tint;
};

cbuffer Camera : register(b1)
{
    float4x4 WorldViewProjection;
};

Texture2D Picture : register(t0);
SamplerState PictureSampler : register(s0);

ColorPixel ColorVS(ColorVertex Input)
{
    ColorPixel Output;

    Output.Position = float4(Input.Position, 0.0f, 1.0f);
    Output.Color = Input.Color;
    return Output;
}

float4 ColorPS(ColorPixel Input) : SV_Target
{
    return Input.Color;
}

ColorPixel PlacedVS(ColorVertex Input)
{
    ColorPixel Output;
    float Sine;
    float Cosine;
    float2 Turned;

    sincos(Angle, Sine, Cosine);
    Turned = float2(Input.Position.x * Cosine - Input.Position.y * Sine,
                    Input.Position.x * Sine + Input.Position.y * Cosine);
    Output.Position = float4(Turned * Scale + Offset, 0.0f, 1.0f);
    Output.Color = Input.Color * Tint;
    return Output;
}

struct TexturedVertex
{
    float2 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct TexturedPixel
{
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

TexturedPixel TexturedVS(TexturedVertex Input)
{
    TexturedPixel Output;

    Output.Position = float4(Input.Position, 0.0f, 1.0f);
    Output.TexCoord = Input.TexCoord;
    return Output;
}

float4 TexturedPS(TexturedPixel Input) : SV_Target
{
    return Picture.Sample(PictureSampler, Input.TexCoord);
}

struct MeshVertex
{
    float3 Position : POSITION;
    float3 Color : COLOR;
};

ColorPixel MeshVS(MeshVertex Input)
{
    ColorPixel Output;

    Output.Position = mul(float4(Input.Position, 1.0f), WorldViewProjection);
    Output.Color = float4(Input.Color, 1.0f);
    return Output;
}

/* Lit, textured cube; lighting happens in linear space and is encoded to sRGB at the end */

cbuffer CubeFrame : register(b2)
{
    row_major float4x4 World;
    row_major float4x4 ViewProjection;
    float3 LightDirection;
    float Padding0;
    float3 CameraPosition;
    float Padding1;
};

struct CubeVertex
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float3 Tint : COLOR;
};

struct CubePixel
{
    float4 Position : SV_Position;
    float3 WorldPosition : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float3 Tint : COLOR;
};

float3 LinearToSrgb(float3 Color)
{
    float3 Low = Color * 12.92f;
    float3 High = 1.055f * pow(max(Color, 0.0f), 1.0f / 2.4f) - 0.055f;

    return lerp(High, Low, step(Color, 0.0031308f));
}

CubePixel CubeVS(CubeVertex Input)
{
    CubePixel Output;
    float4 WorldPosition = mul(float4(Input.Position, 1.0f), World);

    Output.Position = mul(WorldPosition, ViewProjection);
    Output.WorldPosition = WorldPosition.xyz;
    Output.Normal = mul(Input.Normal, (float3x3)World);
    Output.TexCoord = Input.TexCoord;
    Output.Tint = Input.Tint;
    return Output;
}

float4 CubePS(CubePixel Input) : SV_Target
{
    const float3 LightColor = float3(1.0f, 0.95f, 0.88f) * 1.7f;
    const float3 Sky = float3(0.32f, 0.40f, 0.55f);
    const float3 Ground = float3(0.20f, 0.17f, 0.15f);
    float3 Normal = normalize(Input.Normal);
    float3 ToLight = normalize(-LightDirection);
    float3 ToCamera = normalize(CameraPosition - Input.WorldPosition);
    float3 Halfway = normalize(ToLight + ToCamera);
    float3 Albedo = Picture.Sample(PictureSampler, Input.TexCoord).rgb * Input.Tint;
    float3 Ambient = lerp(Ground, Sky, Normal.y * 0.5f + 0.5f) * 0.6f;
    float Diffuse = saturate(dot(Normal, ToLight));
    float Specular = pow(saturate(dot(Normal, Halfway)), 64.0f) * 0.5f * (Diffuse > 0.0f);
    float3 Color = Albedo * (Ambient + Diffuse * LightColor) + Specular * LightColor;

    /* A gentle curve keeps highlights from clipping hard before the sRGB encode */
    Color = Color / (1.0f + Color * 0.15f);
    return float4(LinearToSrgb(Color), 1.0f);
}

/* A full screen triangle from the vertex id alone, with no buffers or layout */

struct BackdropPixel
{
    float4 Position : SV_Position;
    float2 Screen : TEXCOORD0;
};

BackdropPixel BackdropVS(uint Vertex : SV_VertexID)
{
    BackdropPixel Output;
    float2 Corner = float2((Vertex << 1) & 2, Vertex & 2);

    Output.Position = float4(Corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 1.0f, 1.0f);
    Output.Screen = Corner;
    return Output;
}

float4 BackdropPS(BackdropPixel Input) : SV_Target
{
    float3 Top = float3(0.05f, 0.07f, 0.12f);
    float3 Bottom = float3(0.010f, 0.010f, 0.016f);
    float Vignette = saturate(1.0f - dot(Input.Screen - 0.5f, Input.Screen - 0.5f) * 0.9f);

    return float4(LinearToSrgb(lerp(Top, Bottom, Input.Screen.y) * Vignette), 1.0f);
}

/* Instanced quads: per vertex corners, per instance placement and color */

struct InstancedVertex
{
    float2 Corner : POSITION;
    float2 Offset : TEXCOORD1;
    float4 Color : TEXCOORD2;
};

ColorPixel InstancedVS(InstancedVertex Input, uint Instance : SV_InstanceID)
{
    ColorPixel Output;
    float Scale = 0.06f + 0.004f * (Instance % 8);

    Output.Position = float4(Input.Corner * Scale + Input.Offset, 0.0f, 1.0f);
    Output.Color = Input.Color;
    return Output;
}
