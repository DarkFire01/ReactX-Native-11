/*
 * PROJECT:     ReactX Direct2D
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shaders for filled primitives
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

cbuffer Viewport : register(b0)
{
    /* x: 2 / width, y: -2 / height */
    float4 Scale;
};

cbuffer Paint : register(b0)
{
    float4 Color;
    float4 MapU;
    float4 MapV;
    float4 Misc;
};

Texture2D Source : register(t0);
SamplerState Sampling : register(s0);

struct VertexIn
{
    float2 Position : POSITION;
    float Coverage : COVERAGE;
};

struct PixelIn
{
    float4 Position : SV_Position;
    float2 Device : TEXCOORD0;
    float Coverage : TEXCOORD1;
};

PixelIn
PrimitiveVS(VertexIn Input)
{
    PixelIn Output;

    Output.Position = float4(Input.Position.x * Scale.x - 1.0, Input.Position.y * Scale.y + 1.0, 0.0, 1.0);
    Output.Device = Input.Position;
    Output.Coverage = Input.Coverage;
    return Output;
}

float4
SolidPS(PixelIn Input) : SV_Target
{
    return Color * Input.Coverage;
}

float4
TexturePS(PixelIn Input) : SV_Target
{
    float3 Point = float3(Input.Device, 1.0);
    float2 Uv = float2(dot(MapU.xyz, Point), dot(MapV.xyz, Point));

    return Source.Sample(Sampling, Uv) * (Misc.x * Input.Coverage);
}
