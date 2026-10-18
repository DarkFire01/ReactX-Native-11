/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal shaders the API checks bind
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

struct SimpleVertex
{
    float2 Position : POSITION;
    float4 Color : COLOR;
};

struct SimplePixel
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

SimplePixel SimpleVS(SimpleVertex Input)
{
    SimplePixel Output;

    Output.Position = float4(Input.Position, 0.0f, 1.0f);
    Output.Color = Input.Color;
    return Output;
}

float4 SimplePS(SimplePixel Input) : SV_Target
{
    return Input.Color;
}
