/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader model 5 stages for the tinyd3d scenes: compute, geometry, tessellation, stream output
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

struct ColorPixel
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

cbuffer Animation : register(b0)
{
    float Time;
    uint Frame;
    float2 Size;
};

float3 Hue(float Value)
{
    float H = frac(Value) * 6.0f;

    return saturate(float3(abs(H - 3.0f) - 1.0f, 2.0f - abs(H - 2.0f), 2.0f - abs(H - 4.0f)));
}

/* compute: a moving interference pattern written straight into a texture */

RWTexture2D<float4> Canvas : register(u0);

[numthreads(8, 8, 1)]
void PatternCS(uint3 Thread : SV_DispatchThreadID)
{
    float2 Point = Thread.xy / Size - 0.5f;
    float Waves = sin(length(Point - float2(0.2f, 0.1f)) * 40.0f - Time * 3.0f) +
                  sin(length(Point + float2(0.25f, -0.15f)) * 33.0f + Time * 2.0f);

    if (Thread.x >= (uint)Size.x || Thread.y >= (uint)Size.y)
        return;

    Canvas[Thread.xy] = float4(Hue(Waves * 0.12f + Time * 0.05f) * (0.55f + 0.2f * Waves), 1.0f);
}

/* A full screen triangle that shows the texture the compute pass wrote, texel for pixel */

Texture2D<float4> Written : register(t0);

float4 FullscreenVS(uint Vertex : SV_VertexID) : SV_Position
{
    float2 Corner = float2((Vertex << 1) & 2, Vertex & 2);

    return float4(Corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4 WrittenPS(float4 Position : SV_Position) : SV_Target
{
    return Written.Load(int3(Position.xy, 0));
}

/* geometry: each point grows into a spinning quad */

struct PointVertex
{
    float2 Center : POSITION;
    float4 Color : COLOR;
    float Size : PSIZE;
};

PointVertex PointVS(PointVertex Input)
{
    return Input;
}

[maxvertexcount(4)]
void SpriteGS(point PointVertex Input[1], inout TriangleStream<ColorPixel> Output)
{
    static const float2 Corners[4] = { float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1) };
    ColorPixel Vertex;
    float Sine;
    float Cosine;
    uint Index;

    sincos(Time + Input[0].Center.x * 3.0f, Sine, Cosine);
    for (Index = 0; Index < 4; Index++)
    {
        float2 Turned = float2(Corners[Index].x * Cosine - Corners[Index].y * Sine,
                               Corners[Index].x * Sine + Corners[Index].y * Cosine);

        Vertex.Position = float4(Input[0].Center + Turned * Input[0].Size * float2(Size.y / Size.x, 1.0f), 0.0f, 1.0f);
        Vertex.Color = Input[0].Color * (0.7f + 0.3f * (Index & 1));
        Output.Append(Vertex);
    }
}

/* tessellation: one triangle patch, subdivided and bulged by the domain shader */

struct ControlPoint
{
    float2 Position : POSITION;
};

struct PatchFactors
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

ControlPoint ControlVS(ControlPoint Input)
{
    return Input;
}

PatchFactors PatchConstants(InputPatch<ControlPoint, 3> Patch)
{
    PatchFactors Factors;
    float Level = 3.0f + (Frame % 120) / 20.0f;

    Factors.Edges[0] = Level;
    Factors.Edges[1] = Level + 1.0f;
    Factors.Edges[2] = Level + 2.0f;
    Factors.Inside = Level;
    return Factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstants")]
ControlPoint TriangleHS(InputPatch<ControlPoint, 3> Patch, uint Index : SV_OutputControlPointID)
{
    return Patch[Index];
}

[domain("tri")]
ColorPixel TriangleDS(PatchFactors Factors, float3 Weights : SV_DomainLocation,
                      const OutputPatch<ControlPoint, 3> Patch)
{
    ColorPixel Output;
    float2 Position = Patch[0].Position * Weights.x + Patch[1].Position * Weights.y + Patch[2].Position * Weights.z;
    float Bulge = Weights.x * Weights.y * Weights.z * 6.0f;

    Position += normalize(Position + 0.0001f) * Bulge * (0.6f + 0.4f * sin(Time));
    Output.Position = float4(Position, 0.0f, 1.0f);
    Output.Color = float4(Hue(dot(Weights, float3(0.0f, 0.33f, 0.66f))) * (0.6f + Bulge), 1.0f);
    return Output;
}

/* streamout: the vertex shader output is captured, then drawn back with DrawAuto */

struct StreamVertex
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

StreamVertex SpinVS(float2 Position : POSITION, float4 Color : COLOR, uint Vertex : SV_VertexID)
{
    StreamVertex Output;
    float Sine;
    float Cosine;

    sincos(Time * (1.0f + (Vertex / 3) * 0.3f), Sine, Cosine);
    Output.Position = float4(Position.x * Cosine - Position.y * Sine, Position.x * Sine + Position.y * Cosine, 0.0f, 1.0f);
    Output.Position.xy *= float2(Size.y / Size.x, 1.0f);
    Output.Color = Color;
    return Output;
}

/* Reads the captured vertices back; SV_Position was stored as a plain float4 */
ColorPixel CapturedVS(float4 Position : CAPTURED, float4 Color : COLOR)
{
    ColorPixel Output;

    Output.Position = Position;
    Output.Color = Color;
    return Output;
}

float4 VertexColorPS(ColorPixel Input) : SV_Target
{
    return Input.Color;
}

/* volume: a 3D texture sliced over time, colored through a 1D palette and tinted from a buffer */

Texture3D<float> Volume : register(t0);
Texture1D<float4> Palette : register(t1);
Buffer<float4> Tints : register(t2);
SamplerState Linear : register(s0);

float4 VolumePS(float4 Position : SV_Position) : SV_Target
{
    float3 Point = float3(Position.xy / Size, frac(Time * 0.1f));
    float Density = Volume.SampleLevel(Linear, Point, 0);
    float4 Color = Palette.SampleLevel(Linear, Density, 0);

    return Color * Tints[(uint)(Position.x / Size.x * 4.0f)];
}
