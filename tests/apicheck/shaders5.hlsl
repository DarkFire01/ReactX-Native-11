/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader model 5 stages the compute and stream output checks bind
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

cbuffer Parameters : register(b0)
{
    uint Add;
    uint Every;
    uint2 Unused;
};

RWStructuredBuffer<uint> Squares : register(u0);
AppendStructuredBuffer<uint> Picked : register(u1);

/* Each thread writes its index squared plus Add */
[numthreads(64, 1, 1)]
void SquaresCS(uint3 Thread : SV_DispatchThreadID)
{
    Squares[Thread.x] = Thread.x * Thread.x + Add;
}

/* Threads whose index is a multiple of Every append it */
[numthreads(64, 1, 1)]
void AppendCS(uint3 Thread : SV_DispatchThreadID)
{
    if (Thread.x % Every == 0)
        Picked.Append(Thread.x);
}

struct PassVertex
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

[maxvertexcount(3)]
void PassGS(triangle PassVertex Input[3], inout TriangleStream<PassVertex> Output)
{
    Output.Append(Input[0]);
    Output.Append(Input[1]);
    Output.Append(Input[2]);
}

struct PatchPoint
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

struct PatchFactors
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

PatchFactors PassConstants(InputPatch<PatchPoint, 3> Patch)
{
    PatchFactors Factors;

    Factors.Edges[0] = 1.0f;
    Factors.Edges[1] = 1.0f;
    Factors.Edges[2] = 1.0f;
    Factors.Inside = 1.0f;
    return Factors;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PassConstants")]
PatchPoint PassHS(InputPatch<PatchPoint, 3> Patch, uint Index : SV_OutputControlPointID)
{
    return Patch[Index];
}

[domain("tri")]
PatchPoint PassDS(PatchFactors Factors, float3 Weights : SV_DomainLocation, const OutputPatch<PatchPoint, 3> Patch)
{
    PatchPoint Output;

    Output.Position = Patch[0].Position * Weights.x + Patch[1].Position * Weights.y + Patch[2].Position * Weights.z;
    Output.Color = Patch[0].Color;
    return Output;
}
