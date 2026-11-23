/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     HLSL sources for the DXBC shaders fed to the converter
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

typedef struct _DC_SHADER
{
    PCSTR Name;
    PCSTR Profile;
    PCSTR Entry;
    PCSTR Source;
} DC_SHADER;

static const char DcVsPassthrough[] =
    "float4 main(float4 Position : POSITION) : SV_Position\n"
    "{\n"
    "    return Position;\n"
    "}\n";

static const char DcPsColor[] =
    "float4 main() : SV_Target\n"
    "{\n"
    "    return float4(1.0f, 0.5f, 0.25f, 1.0f);\n"
    "}\n";

static const char DcVsTransform[] =
    "cbuffer Camera : register(b0)\n"
    "{\n"
    "    float4x4 World;\n"
    "    float4x4 ViewProjection;\n"
    "};\n"
    "struct VsOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; float3 Normal : NORMAL; };\n"
    "VsOut main(float3 Position : POSITION, float2 Uv : TEXCOORD0, float3 Normal : NORMAL)\n"
    "{\n"
    "    VsOut Out;\n"
    "    float4 WorldPosition = mul(float4(Position, 1.0f), World);\n"
    "    Out.Position = mul(WorldPosition, ViewProjection);\n"
    "    Out.Uv = Uv;\n"
    "    Out.Normal = normalize(mul(Normal, (float3x3)World));\n"
    "    return Out;\n"
    "}\n";

static const char DcPsTexture[] =
    "Texture2D Diffuse : register(t0);\n"
    "SamplerState Linear : register(s0);\n"
    "cbuffer Material : register(b1) { float4 Tint; float Cutoff; };\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0, float3 Normal : NORMAL) : SV_Target\n"
    "{\n"
    "    float4 Color = Diffuse.Sample(Linear, Uv) * Tint;\n"
    "    if (Color.a < Cutoff)\n"
    "        discard;\n"
    "    return Color * saturate(dot(Normal, float3(0.0f, 0.0f, 1.0f)));\n"
    "}\n";

static const char DcPsLoop[] =
    "cbuffer Lights : register(b0) { float4 Colors[8]; uint Count; };\n"
    "float4 main(float4 Position : SV_Position) : SV_Target\n"
    "{\n"
    "    float4 Sum = 0;\n"
    "    [loop] for (uint Index = 0; Index < Count; Index++)\n"
    "        Sum += Colors[Index] * (Position.x > 10.0f ? 2.0f : 0.5f);\n"
    "    return Sum;\n"
    "}\n";

static const char DcGsTriangle[] =
    "struct GsIn { float4 Position : SV_Position; };\n"
    "[maxvertexcount(3)]\n"
    "void main(triangle GsIn Input[3], inout TriangleStream<GsIn> Stream)\n"
    "{\n"
    "    for (uint Index = 0; Index < 3; Index++)\n"
    "        Stream.Append(Input[Index]);\n"
    "    Stream.RestartStrip();\n"
    "}\n";

static const char DcHsTriangle[] =
    "struct CpIn { float4 Position : POSITION; };\n"
    "struct Constants { float Edges[3] : SV_TessFactor; float Inside : SV_InsideTessFactor; };\n"
    "Constants Patch(InputPatch<CpIn, 3> Input)\n"
    "{\n"
    "    Constants Out;\n"
    "    Out.Edges[0] = Out.Edges[1] = Out.Edges[2] = 4.0f;\n"
    "    Out.Inside = 4.0f;\n"
    "    return Out;\n"
    "}\n"
    "[domain(\"tri\")]\n"
    "[partitioning(\"integer\")]\n"
    "[outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(3)]\n"
    "[patchconstantfunc(\"Patch\")]\n"
    "CpIn main(InputPatch<CpIn, 3> Input, uint Id : SV_OutputControlPointID)\n"
    "{\n"
    "    return Input[Id];\n"
    "}\n";

static const char DcDsTriangle[] =
    "struct CpIn { float4 Position : POSITION; };\n"
    "struct Constants { float Edges[3] : SV_TessFactor; float Inside : SV_InsideTessFactor; };\n"
    "[domain(\"tri\")]\n"
    "float4 main(Constants Patch, float3 Location : SV_DomainLocation, const OutputPatch<CpIn, 3> Points) : SV_Position\n"
    "{\n"
    "    return Points[0].Position * Location.x + Points[1].Position * Location.y + Points[2].Position * Location.z;\n"
    "}\n";

static const char DcCsBuffer[] =
    "RWStructuredBuffer<uint> Output : register(u0);\n"
    "StructuredBuffer<uint> Input : register(t0);\n"
    "groupshared uint Shared[64];\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID, uint Local : SV_GroupIndex)\n"
    "{\n"
    "    Shared[Local] = Input[Thread.x];\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    Output[Thread.x] = Shared[63 - Local] + Shared[Local];\n"
    "}\n";

static const char DcCsRaw[] =
    "RWByteAddressBuffer Output : register(u0);\n"
    "[numthreads(8, 8, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID)\n"
    "{\n"
    "    Output.Store((Thread.y * 8 + Thread.x) * 4, Thread.x ^ Thread.y);\n"
    "}\n";

static const char DcPsSpaces[] =
    "Texture2D Maps[4] : register(t0, space1);\n"
    "SamplerState Point : register(s2, space3);\n"
    "cbuffer Frame : register(b0, space2) { uint Which; };\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    return Maps[Which].Sample(Point, Uv);\n"
    "}\n";

static const DC_SHADER DcCorpus[] =
{
    { "vs40_passthrough", "vs_4_0", "main", DcVsPassthrough },
    { "ps40_color", "ps_4_0", "main", DcPsColor },
    { "vs40_transform", "vs_4_0", "main", DcVsTransform },
    { "ps40_texture", "ps_4_0", "main", DcPsTexture },
    { "vs41_passthrough", "vs_4_1", "main", DcVsPassthrough },
    { "ps41_loop", "ps_4_1", "main", DcPsLoop },
    { "gs40_triangle", "gs_4_0", "main", DcGsTriangle },
    { "cs40_raw", "cs_4_0", "main", DcCsRaw },
    { "vs50_passthrough", "vs_5_0", "main", DcVsPassthrough },
    { "ps50_color", "ps_5_0", "main", DcPsColor },
    { "vs50_transform", "vs_5_0", "main", DcVsTransform },
    { "ps50_texture", "ps_5_0", "main", DcPsTexture },
    { "ps50_loop", "ps_5_0", "main", DcPsLoop },
    { "gs50_triangle", "gs_5_0", "main", DcGsTriangle },
    { "hs50_triangle", "hs_5_0", "main", DcHsTriangle },
    { "ds50_triangle", "ds_5_0", "main", DcDsTriangle },
    { "cs50_buffer", "cs_5_0", "main", DcCsBuffer },
    { "cs50_raw", "cs_5_0", "main", DcCsRaw },
    { "vs51_transform", "vs_5_1", "main", DcVsTransform },
    { "ps51_spaces", "ps_5_1", "main", DcPsSpaces },
    { "cs51_buffer", "cs_5_1", "main", DcCsBuffer },
};
