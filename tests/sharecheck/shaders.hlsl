/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shaders that copy a shared texture into a render target by sampling it
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

Texture2D<float4> Source : register(t0);

/* One triangle that covers the whole target */
float4
FullScreenVS(
    uint Id : SV_VertexID) : SV_Position
{
    float2 Corner = float2((Id << 1) & 2, Id & 2);

    return float4(Corner * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}

float4
CopyPS(
    float4 Position : SV_Position) : SV_Target
{
    return Source.Load(int3(Position.xy, 0));
}
