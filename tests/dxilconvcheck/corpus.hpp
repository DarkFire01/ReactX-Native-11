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
    UINT Flags;
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

static const char DcVsInstanced[] =
    "cbuffer Instances : register(b0) { float4x4 Transforms[16]; float4 Tints[16]; };\n"
    "struct VsOut { float4 Position : SV_Position; float4 Color : COLOR0; uint Id : INSTANCEID; float Clip : SV_ClipDistance0; };\n"
    "VsOut main(float3 Position : POSITION, uint Vertex : SV_VertexID, uint Instance : SV_InstanceID)\n"
    "{\n"
    "    VsOut Out;\n"
    "    Out.Position = mul(float4(Position, 1.0f), Transforms[Instance & 15]);\n"
    "    Out.Color = Tints[Instance & 15] * ((Vertex & 1) ? 0.5f : 1.0f);\n"
    "    Out.Id = Instance;\n"
    "    Out.Clip = dot(Out.Position, float4(0.0f, 1.0f, 0.0f, 0.5f));\n"
    "    return Out;\n"
    "}\n";

static const char DcVsSkinned[] =
    "cbuffer Bones : register(b0) { float4x3 Palette[64]; };\n"
    "struct VsOut { float4 Position : SV_Position; float3 Normal : NORMAL; };\n"
    "VsOut main(float3 Position : POSITION, float3 Normal : NORMAL, uint4 Indices : BLENDINDICES, float4 Weights : BLENDWEIGHT)\n"
    "{\n"
    "    VsOut Out;\n"
    "    float3 Skinned = 0;\n"
    "    float3 Turned = 0;\n"
    "    [unroll] for (uint Index = 0; Index < 4; Index++)\n"
    "    {\n"
    "        Skinned += mul(float4(Position, 1.0f), Palette[Indices[Index]]) * Weights[Index];\n"
    "        Turned += mul(Normal, (float3x3)Palette[Indices[Index]]) * Weights[Index];\n"
    "    }\n"
    "    Out.Position = float4(Skinned, 1.0f);\n"
    "    Out.Normal = normalize(Turned);\n"
    "    return Out;\n"
    "}\n";

static const char DcVsIntegers[] =
    "cbuffer Params : register(b0) { int4 Signed; uint4 Unsigned; };\n"
    "struct VsOut { float4 Position : SV_Position; nointerpolation int4 A : TEXCOORD0; nointerpolation uint4 B : TEXCOORD1; };\n"
    "VsOut main(int4 In : POSITION, uint Vertex : SV_VertexID)\n"
    "{\n"
    "    VsOut Out;\n"
    "    Out.A = In * Signed + (In >> 2) - (In / max(Signed, 1)) + (In % 7);\n"
    "    Out.B = (Unsigned << (Vertex & 31)) ^ (Unsigned | Vertex) & ~Unsigned;\n"
    "    Out.B += Unsigned / max(Vertex, 1u) + Unsigned % 3u;\n"
    "    Out.A.w = min(In.x, In.y) + max(In.z, In.w) + abs(In.x);\n"
    "    Out.B.w = (Out.B.x > Out.B.y) ? Out.B.z : Out.B.w;\n"
    "    Out.Position = float4(In.xyz, 1.0f) * float4(asfloat(Unsigned.x), (float)Signed.y, (float)Unsigned.z, 1.0f);\n"
    "    return Out;\n"
    "}\n";

static const char DcVsMath[] =
    "cbuffer Params : register(b0) { float4 Value; };\n"
    "float4 main(float4 In : POSITION) : SV_Position\n"
    "{\n"
    "    float4 R = sin(In) + cos(In.yzwx) + exp2(In) + log2(abs(In) + 1.0f);\n"
    "    R += sqrt(abs(In)) + rsqrt(abs(In) + 1.0f) + rcp(In + 2.0f) + frac(In) + floor(In) + ceil(In) + round(In) + trunc(In);\n"
    "    R += saturate(In) + lerp(In, Value, 0.25f) + step(Value, In) + smoothstep(0.0f, 1.0f, In);\n"
    "    R += mad(In, Value, R) + pow(abs(In), Value) + fmod(In, Value + 3.0f) + sign(In);\n"
    "    R.x += dot(In.xyz, Value.xyz) + length(In) + distance(In, Value);\n"
    "    R.xyz += cross(In.xyz, Value.xyz) + reflect(In.xyz, normalize(Value.xyz));\n"
    "    R.w += atan2(In.y, In.x) + asin(saturate(In.z)) + acos(saturate(In.w)) + tan(In.x);\n"
    "    return R;\n"
    "}\n";

static const char DcVsIcb[] =
    "static const float4 Table[8] =\n"
    "{\n"
    "    float4(1, 0, 0, 1), float4(0, 1, 0, 1), float4(0, 0, 1, 1), float4(1, 1, 0, 1),\n"
    "    float4(1, 0, 1, 1), float4(0, 1, 1, 1), float4(1, 1, 1, 1), float4(0, 0, 0, 1),\n"
    "};\n"
    "struct VsOut { float4 Position : SV_Position; float4 Color : COLOR; };\n"
    "VsOut main(float4 Position : POSITION, uint Vertex : SV_VertexID)\n"
    "{\n"
    "    VsOut Out;\n"
    "    float4 Local[4];\n"
    "    for (uint Index = 0; Index < 4; Index++)\n"
    "        Local[Index] = Table[(Vertex + Index) & 7] * Position;\n"
    "    Out.Position = Position;\n"
    "    Out.Color = Local[Vertex & 3] + Table[Vertex & 7];\n"
    "    return Out;\n"
    "}\n";

static const char DcVsBuffers[] =
    "Buffer<float4> Positions : register(t0);\n"
    "StructuredBuffer<float3x3> Frames : register(t1);\n"
    "ByteAddressBuffer Raw : register(t2);\n"
    "tbuffer Extra : register(t3) { float4 Bias; };\n"
    "float4 main(uint Vertex : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float4 P = Positions.Load(Vertex);\n"
    "    uint Bits = Raw.Load(Vertex * 4);\n"
    "    uint2 Pair = Raw.Load2(Vertex * 8 + 16);\n"
    "    float3 Turned = mul(P.xyz, Frames[Vertex]);\n"
    "    return float4(Turned, 1.0f) + Bias + float4(asfloat(Bits), asfloat(Pair.x), asfloat(Pair.y), 0.0f);\n"
    "}\n";

static const char DcPsTextures[] =
    "Texture1D Line : register(t0);\n"
    "Texture2DArray Layers : register(t1);\n"
    "Texture3D Volume : register(t2);\n"
    "TextureCube Sky : register(t3);\n"
    "Texture2DMS<float4, 4> Multi : register(t4);\n"
    "SamplerState Linear : register(s0);\n"
    "SamplerComparisonState Shadow : register(s1);\n"
    "Texture2D<float> Depth : register(t5);\n"
    "float4 main(float4 Position : SV_Position, float3 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float4 C = Line.Sample(Linear, Uv.x);\n"
    "    C += Layers.SampleLevel(Linear, Uv, 2.0f);\n"
    "    C += Volume.SampleGrad(Linear, Uv, ddx(Uv), ddy(Uv));\n"
    "    C += Sky.SampleBias(Linear, Uv, 0.5f);\n"
    "    C += Multi.Load(int2(Position.xy), 2);\n"
    "    C += Layers.Load(int4(Position.xy, 1, 0), int2(1, -1));\n"
    "    C.x += Depth.SampleCmp(Shadow, Uv.xy, Uv.z);\n"
    "    C.y += Depth.SampleCmpLevelZero(Shadow, Uv.xy, Uv.z, int2(2, 2));\n"
    "    uint W;\n"
    "    uint H;\n"
    "    uint Levels;\n"
    "    Layers.GetDimensions(0, W, H, Levels, Levels);\n"
    "    C.z += W + H + Levels;\n"
    "    return C;\n"
    "}\n";

static const char DcPsGather[] =
    "Texture2D Color : register(t0);\n"
    "Texture2D<float> Depth : register(t1);\n"
    "TextureCubeArray Probes : register(t2);\n"
    "SamplerState Linear : register(s0);\n"
    "SamplerComparisonState Shadow : register(s1);\n"
    "float4 main(float4 Position : SV_Position, float4 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float4 C = Color.Gather(Linear, Uv.xy);\n"
    "    C += Color.Gather(Linear, Uv.xy, int2(1, 0));\n"
    "    C += Probes.Sample(Linear, Uv);\n"
    "    C.x += Color.CalculateLevelOfDetail(Linear, Uv.xy);\n"
    "    C.y += Color.CalculateLevelOfDetailUnclamped(Linear, Uv.xy);\n"
    "    C += Depth.GatherCmpRed(Shadow, Uv.xy, Uv.z);\n"
    "    C += Color.GatherGreen(Linear, Uv.xy, int2(-1, 1));\n"
    "    return C;\n"
    "}\n";

static const char DcPsDepth[] =
    "struct PsOut { float4 Color : SV_Target0; float4 Normal : SV_Target1; float Depth : SV_Depth; };\n"
    "PsOut main(float3 Normal : NORMAL, float4 Position : SV_Position, uint Primitive : SV_PrimitiveID, bool Front : SV_IsFrontFace)\n"
    "{\n"
    "    PsOut Out;\n"
    "    Out.Color = float4(Front ? 1.0f : 0.0f, (Primitive & 1) ? 0.5f : 0.25f, 0.0f, 1.0f);\n"
    "    Out.Normal = float4(normalize(Normal) * 0.5f + 0.5f, 0.0f);\n"
    "    Out.Depth = Position.z * 0.5f;\n"
    "    return Out;\n"
    "}\n";

static const char DcPsDepthGe[] =
    "[earlydepthstencil]\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    return float4(ddx_fine(Uv), ddy_coarse(Uv));\n"
    "}\n";

static const char DcPsConservativeDepth[] =
    "struct PsOut { float4 Color : SV_Target; float Depth : SV_DepthGreaterEqual; uint Coverage : SV_Coverage; };\n"
    "PsOut main(float4 Position : SV_Position, uint Mask : SV_Coverage, uint Sample : SV_SampleIndex)\n"
    "{\n"
    "    PsOut Out;\n"
    "    Out.Color = float4(Sample, Mask, 0.0f, 1.0f);\n"
    "    Out.Depth = Position.z + 0.01f;\n"
    "    Out.Coverage = Mask & 5;\n"
    "    return Out;\n"
    "}\n";

static const char DcPsInterpolation[] =
    "float4 main(float4 Position : SV_Position,\n"
    "            nointerpolation float4 Flat : TEXCOORD0,\n"
    "            centroid float4 Center : TEXCOORD1,\n"
    "            noperspective float4 Screen : TEXCOORD2,\n"
    "            sample float4 PerSample : TEXCOORD3,\n"
    "            linear centroid noperspective float4 Mixed : TEXCOORD4) : SV_Target\n"
    "{\n"
    "    return Flat + Center + Screen + PerSample + Mixed;\n"
    "}\n";

static const char DcPsEvaluate[] =
    "float4 main(float4 Position : SV_Position, float4 Color : COLOR0, float2 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float4 C = EvaluateAttributeAtSample(Color, 1);\n"
    "    C += EvaluateAttributeCentroid(Color);\n"
    "    C.xy += EvaluateAttributeSnapped(Uv, int2(1, -2));\n"
    "    return C;\n"
    "}\n";

static const char DcPsUav[] =
    "RWTexture2D<float4> Canvas : register(u1);\n"
    "RWStructuredBuffer<uint> Counts : register(u2);\n"
    "float4 main(float4 Position : SV_Position) : SV_Target\n"
    "{\n"
    "    uint Previous;\n"
    "    Canvas[uint2(Position.xy)] = float4(1.0f, 0.0f, 0.0f, 1.0f);\n"
    "    InterlockedAdd(Counts[0], 1, Previous);\n"
    "    return float4(Previous, 0.0f, 0.0f, 1.0f);\n"
    "}\n";

static const char DcPsFlow[] =
    "cbuffer Params : register(b0) { uint Mode; float4 Colors[4]; };\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    float4 C = 0;\n"
    "    [branch] switch (Mode)\n"
    "    {\n"
    "    case 0:\n"
    "        C = Colors[0];\n"
    "        break;\n"
    "    case 1:\n"
    "    case 2:\n"
    "        C = Colors[Mode] * Uv.x;\n"
    "        break;\n"
    "    default:\n"
    "        C = Colors[3];\n"
    "        break;\n"
    "    }\n"
    "    [loop] for (uint Index = 0; Index < Mode; Index++)\n"
    "    {\n"
    "        if (C.x > 4.0f)\n"
    "            break;\n"
    "        if ((Index & 1) == 0)\n"
    "            continue;\n"
    "        C += Colors[Index & 3];\n"
    "    }\n"
    "    uint Count = 0;\n"
    "    [loop] while (C.y < 1.0f && Count < 16)\n"
    "    {\n"
    "        C.y += Uv.y + 0.1f;\n"
    "        Count++;\n"
    "    }\n"
    "    [flatten] if (Uv.x > 0.5f)\n"
    "        C.z = Uv.y;\n"
    "    else\n"
    "        C.z = -Uv.y;\n"
    "    if (any(C < 0.0f))\n"
    "        discard;\n"
    "    return C;\n"
    "}\n";

static const char DcPsBits[] =
    "cbuffer Params : register(b0) { uint4 Value; int4 Signed; };\n"
    "uint4 main(float4 Position : SV_Position) : SV_Target\n"
    "{\n"
    "    uint4 R = countbits(Value) + firstbithigh(Value) + firstbitlow(Value) + reversebits(Value);\n"
    "    R += (uint4)firstbithigh(Signed);\n"
    "    R.x += f32tof16(Position.x) + asuint(f16tof32(Value.y));\n"
    "    R.y += (uint)(Position.y) * Value.z;\n"
    "    R.zw += Value.zw >> (Value.xy & 7);\n"
    "    return R;\n"
    "}\n";

static const char DcPsDouble[] =
    "cbuffer Params : register(b0) { double2 Scale; };\n"
    "float4 main(float4 Position : SV_Position) : SV_Target\n"
    "{\n"
    "    double X = Position.x;\n"
    "    double Y = Position.y;\n"
    "    double R = X * Scale.x + Y / Scale.y;\n"
    "    R = min(R, Scale.x) + max(R, Scale.y);\n"
    "    uint Low;\n"
    "    uint High;\n"
    "    asuint(R, Low, High);\n"
    "    return float4((float)R, (float)(X < Y), Low, High);\n"
    "}\n";

static const char DcPsMinPrecision[] =
    "min16float4 main(float4 Position : SV_Position, min16float4 Color : COLOR0, min16int2 Index : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    min16float4 C = Color * (min16float)0.5f;\n"
    "    C.x += (min16float)Index.x;\n"
    "    C.y -= (min16float)Index.y;\n"
    "    return C;\n"
    "}\n";

static const char DcGsPoints[] =
    "struct GsIn { float4 Position : SV_Position; float Size : PSIZE; };\n"
    "struct GsOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; uint Layer : SV_RenderTargetArrayIndex; };\n"
    "[maxvertexcount(4)]\n"
    "void main(point GsIn Input[1], uint Primitive : SV_PrimitiveID, inout TriangleStream<GsOut> Stream)\n"
    "{\n"
    "    GsOut Out;\n"
    "    Out.Layer = Primitive & 3;\n"
    "    [unroll] for (uint Index = 0; Index < 4; Index++)\n"
    "    {\n"
    "        Out.Uv = float2(Index & 1, Index >> 1);\n"
    "        Out.Position = Input[0].Position + float4((Out.Uv * 2.0f - 1.0f) * Input[0].Size, 0.0f, 0.0f);\n"
    "        Stream.Append(Out);\n"
    "    }\n"
    "}\n";

static const char DcGsLines[] =
    "struct GsIn { float4 Position : SV_Position; };\n"
    "struct GsOut { float4 Position : SV_Position; uint Viewport : SV_ViewportArrayIndex; };\n"
    "[maxvertexcount(6)]\n"
    "void main(lineadj GsIn Input[4], inout LineStream<GsOut> Stream)\n"
    "{\n"
    "    GsOut Out;\n"
    "    for (uint Index = 0; Index < 4; Index++)\n"
    "    {\n"
    "        Out.Position = Input[Index].Position;\n"
    "        Out.Viewport = Index;\n"
    "        Stream.Append(Out);\n"
    "        if (Index == 1)\n"
    "            Stream.RestartStrip();\n"
    "    }\n"
    "}\n";

static const char DcGsStreams[] =
    "struct GsIn { float4 Position : SV_Position; float3 Normal : NORMAL; };\n"
    "struct First { float4 Position : SV_Position; float3 Normal : NORMAL; };\n"
    "struct Second { float4 Data : TEXCOORD0; };\n"
    "[maxvertexcount(3)]\n"
    "void main(triangle GsIn Input[3], inout PointStream<First> A, inout PointStream<Second> B)\n"
    "{\n"
    "    for (uint Index = 0; Index < 3; Index++)\n"
    "    {\n"
    "        First F;\n"
    "        Second S;\n"
    "        F.Position = Input[Index].Position;\n"
    "        F.Normal = Input[Index].Normal;\n"
    "        A.Append(F);\n"
    "        S.Data = float4(Input[Index].Normal, Index);\n"
    "        B.Append(S);\n"
    "    }\n"
    "}\n";

static const char DcGsInstanced[] =
    "struct GsIn { float4 Position : SV_Position; };\n"
    "struct GsOut { float4 Position : SV_Position; uint Layer : SV_RenderTargetArrayIndex; };\n"
    "[instance(6)]\n"
    "[maxvertexcount(3)]\n"
    "void main(triangle GsIn Input[3], uint Instance : SV_GSInstanceID, inout TriangleStream<GsOut> Stream)\n"
    "{\n"
    "    GsOut Out;\n"
    "    Out.Layer = Instance;\n"
    "    for (uint Index = 0; Index < 3; Index++)\n"
    "    {\n"
    "        Out.Position = Input[Index].Position * (Instance + 1);\n"
    "        Stream.Append(Out);\n"
    "    }\n"
    "}\n";

static const char DcHsQuad[] =
    "cbuffer Params : register(b0) { float4 Factors; };\n"
    "struct CpIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };\n"
    "struct CpOut { float3 Position : POSITION; float2 Uv : TEXCOORD0; };\n"
    "struct Constants { float Edges[4] : SV_TessFactor; float Inside[2] : SV_InsideTessFactor; float3 Center : CENTER; };\n"
    "Constants Patch(InputPatch<CpIn, 4> Input, uint Primitive : SV_PrimitiveID)\n"
    "{\n"
    "    Constants Out;\n"
    "    Out.Center = 0;\n"
    "    [unroll] for (uint Index = 0; Index < 4; Index++)\n"
    "    {\n"
    "        Out.Edges[Index] = Factors[Index] + (Primitive & 1);\n"
    "        Out.Center += Input[Index].Position * 0.25f;\n"
    "    }\n"
    "    Out.Inside[0] = max(Out.Edges[0], Out.Edges[2]);\n"
    "    Out.Inside[1] = max(Out.Edges[1], Out.Edges[3]);\n"
    "    return Out;\n"
    "}\n"
    "[domain(\"quad\")]\n"
    "[partitioning(\"fractional_odd\")]\n"
    "[outputtopology(\"triangle_ccw\")]\n"
    "[outputcontrolpoints(4)]\n"
    "[patchconstantfunc(\"Patch\")]\n"
    "[maxtessfactor(32.0)]\n"
    "CpOut main(InputPatch<CpIn, 4> Input, uint Id : SV_OutputControlPointID, uint Primitive : SV_PrimitiveID)\n"
    "{\n"
    "    CpOut Out;\n"
    "    Out.Position = Input[Id].Position;\n"
    "    Out.Uv = Input[Id].Uv + (Primitive & 3);\n"
    "    return Out;\n"
    "}\n";

static const char DcDsQuad[] =
    "struct CpIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };\n"
    "struct Constants { float Edges[4] : SV_TessFactor; float Inside[2] : SV_InsideTessFactor; float3 Center : CENTER; };\n"
    "struct DsOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; };\n"
    "[domain(\"quad\")]\n"
    "DsOut main(Constants Patch, float2 Location : SV_DomainLocation, const OutputPatch<CpIn, 4> Points, uint Primitive : SV_PrimitiveID)\n"
    "{\n"
    "    DsOut Out;\n"
    "    float3 Top = lerp(Points[0].Position, Points[1].Position, Location.x);\n"
    "    float3 Bottom = lerp(Points[3].Position, Points[2].Position, Location.x);\n"
    "    Out.Position = float4(lerp(Top, Bottom, Location.y) + Patch.Center * Patch.Inside[0], 1.0f);\n"
    "    Out.Uv = lerp(Points[0].Uv, Points[2].Uv, Location) + Primitive;\n"
    "    return Out;\n"
    "}\n";

static const char DcHsLine[] =
    "struct CpIn { float4 Position : POSITION; };\n"
    "struct Constants { float Factors[2] : SV_TessFactor; };\n"
    "Constants Patch(InputPatch<CpIn, 2> Input)\n"
    "{\n"
    "    Constants Out;\n"
    "    Out.Factors[0] = 8.0f;\n"
    "    Out.Factors[1] = length(Input[0].Position - Input[1].Position);\n"
    "    return Out;\n"
    "}\n"
    "[domain(\"isoline\")]\n"
    "[partitioning(\"pow2\")]\n"
    "[outputtopology(\"line\")]\n"
    "[outputcontrolpoints(2)]\n"
    "[patchconstantfunc(\"Patch\")]\n"
    "CpIn main(InputPatch<CpIn, 2> Input, uint Id : SV_OutputControlPointID)\n"
    "{\n"
    "    return Input[Id];\n"
    "}\n";

static const char DcDsLine[] =
    "struct CpIn { float4 Position : POSITION; };\n"
    "struct Constants { float Factors[2] : SV_TessFactor; };\n"
    "[domain(\"isoline\")]\n"
    "float4 main(Constants Patch, float2 Location : SV_DomainLocation, const OutputPatch<CpIn, 2> Points) : SV_Position\n"
    "{\n"
    "    return lerp(Points[0].Position, Points[1].Position, Location.x) + Location.y * Patch.Factors[1];\n"
    "}\n";

static const char DcHsTriEven[] =
    "struct CpIn { float4 Position : POSITION; float3 Normal : NORMAL; };\n"
    "struct Constants { float Edges[3] : SV_TessFactor; float Inside : SV_InsideTessFactor; };\n"
    "Constants Patch(InputPatch<CpIn, 3> Input, const OutputPatch<CpIn, 3> Output)\n"
    "{\n"
    "    Constants Out;\n"
    "    for (uint Index = 0; Index < 3; Index++)\n"
    "        Out.Edges[Index] = 1.0f + length(Output[Index].Normal - Input[Index].Normal);\n"
    "    Out.Inside = (Out.Edges[0] + Out.Edges[1] + Out.Edges[2]) / 3.0f;\n"
    "    return Out;\n"
    "}\n"
    "[domain(\"tri\")]\n"
    "[partitioning(\"fractional_even\")]\n"
    "[outputtopology(\"triangle_cw\")]\n"
    "[outputcontrolpoints(3)]\n"
    "[patchconstantfunc(\"Patch\")]\n"
    "CpIn main(InputPatch<CpIn, 3> Input, uint Id : SV_OutputControlPointID)\n"
    "{\n"
    "    CpIn Out = Input[Id];\n"
    "    Out.Normal = normalize(Out.Normal);\n"
    "    return Out;\n"
    "}\n";

static const char DcCsAtomics[] =
    "RWByteAddressBuffer Raw : register(u0);\n"
    "RWTexture2D<uint> Image : register(u1);\n"
    "RWStructuredBuffer<int> Signed : register(u2);\n"
    "groupshared uint Shared[32];\n"
    "[numthreads(32, 1, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID, uint Local : SV_GroupIndex)\n"
    "{\n"
    "    uint Old;\n"
    "    int SignedOld;\n"
    "    Shared[Local] = 0;\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    InterlockedAdd(Shared[Local & 7], Thread.x, Old);\n"
    "    InterlockedMin(Shared[1], Thread.x);\n"
    "    InterlockedMax(Shared[2], Thread.x);\n"
    "    InterlockedAnd(Shared[3], Thread.x);\n"
    "    InterlockedOr(Shared[4], Thread.x);\n"
    "    InterlockedXor(Shared[5], Thread.x);\n"
    "    InterlockedExchange(Shared[6], Thread.x, Old);\n"
    "    InterlockedCompareExchange(Shared[7], 0, Thread.x, Old);\n"
    "    InterlockedCompareStore(Shared[8], 1, Thread.x);\n"
    "    Raw.InterlockedAdd(Thread.x * 4, 1, Old);\n"
    "    Raw.InterlockedMax(0, Thread.x);\n"
    "    Raw.InterlockedCompareExchange(4, Old, Thread.x, Old);\n"
    "    InterlockedAdd(Image[Thread.xy], 2, Old);\n"
    "    InterlockedMin(Image[uint2(0, 0)], Thread.y);\n"
    "    InterlockedMin(Signed[0], -(int)Thread.x, SignedOld);\n"
    "    InterlockedMax(Signed[1], (int)Thread.x);\n"
    "    AllMemoryBarrierWithGroupSync();\n"
    "    Raw.Store(Thread.x * 4 + 128, Shared[Local] + Old + SignedOld);\n"
    "}\n";

static const char DcCsCounters[] =
    "struct Item { float4 Value; uint Key; };\n"
    "AppendStructuredBuffer<Item> Output : register(u0);\n"
    "ConsumeStructuredBuffer<Item> Input : register(u1);\n"
    "RWStructuredBuffer<Item> Counted : register(u2);\n"
    "[numthreads(64, 1, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID)\n"
    "{\n"
    "    Item I = Input.Consume();\n"
    "    I.Key += Thread.x;\n"
    "    Output.Append(I);\n"
    "    uint Slot = Counted.IncrementCounter();\n"
    "    Counted[Slot] = I;\n"
    "}\n";

static const char DcCsTyped[] =
    "RWBuffer<float4> Typed : register(u0);\n"
    "RWTexture3D<float> Volume : register(u1);\n"
    "RWTexture2DArray<uint2> Layers : register(u2);\n"
    "Texture2D<float4> Source : register(t0);\n"
    "[numthreads(8, 8, 4)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID, uint3 Group : SV_GroupID, uint3 Inside : SV_GroupThreadID)\n"
    "{\n"
    "    float4 V = Source.Load(int3(Thread.xy, 0));\n"
    "    Typed[Thread.x + Thread.y * 64] = V;\n"
    "    Volume[Thread] = V.x + Volume[Thread + uint3(1, 0, 0)];\n"
    "    Layers[uint3(Thread.xy, Group.z)] = uint2(Inside.x, Inside.y);\n"
    "    uint W;\n"
    "    uint H;\n"
    "    uint D;\n"
    "    Volume.GetDimensions(W, H, D);\n"
    "    DeviceMemoryBarrier();\n"
    "    Typed[0] = float4(W, H, D, 0.0f);\n"
    "}\n";

static const char DcCs4Structured[] =
    "struct Pair { float A; uint B; };\n"
    "RWStructuredBuffer<Pair> Output : register(u0);\n"
    "StructuredBuffer<Pair> Input : register(t0);\n"
    "groupshared Pair Shared[256];\n"
    "[numthreads(256, 1, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID, uint Local : SV_GroupIndex)\n"
    "{\n"
    "    Shared[Local] = Input[Thread.x];\n"
    "    GroupMemoryBarrierWithGroupSync();\n"
    "    Pair P = Shared[255 - Local];\n"
    "    P.A *= 2.0f;\n"
    "    Output[Thread.x] = P;\n"
    "}\n";

static const char DcPsArrays51[] =
    "Texture2D Maps[] : register(t0, space1);\n"
    "SamplerState Samplers[4] : register(s0);\n"
    "struct Material { float4 Tint; uint Map; uint Sampler; };\n"
    "ConstantBuffer<Material> Materials[8] : register(b0, space2);\n"
    "cbuffer Frame : register(b0) { uint Which; };\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0, nointerpolation uint Id : MATERIAL) : SV_Target\n"
    "{\n"
    "    Material M = Materials[Which & 7];\n"
    "    float4 C = Maps[NonUniformResourceIndex(Id)].Sample(Samplers[M.Sampler & 3], Uv);\n"
    "    return C * M.Tint + Maps[M.Map].Load(int3(Position.xy, 0));\n"
    "}\n";

static const char DcCsArrays51[] =
    "RWStructuredBuffer<uint> Outputs[4] : register(u0, space1);\n"
    "ByteAddressBuffer Inputs[] : register(t0, space3);\n"
    "[numthreads(16, 1, 1)]\n"
    "void main(uint3 Thread : SV_DispatchThreadID)\n"
    "{\n"
    "    uint Value = Inputs[Thread.x & 7].Load(Thread.x * 4);\n"
    "    InterlockedAdd(Outputs[Thread.y & 3][0], Value);\n"
    "    Outputs[NonUniformResourceIndex(Value & 3)][Thread.x] = Value;\n"
    "}\n";

static const char DcPsInterfaces[] =
    "interface ILight { float3 Shade(float3 Normal); };\n"
    "class Ambient : ILight { float3 Color; float3 Shade(float3 Normal) { return Color; } };\n"
    "class Directional : ILight { float3 Direction; float3 Color; float3 Shade(float3 Normal) { return Color * saturate(dot(Normal, Direction)); } };\n"
    "cbuffer Lights : register(b0) { Ambient Sky; Directional Sun; };\n"
    "ILight Active;\n"
    "float4 main(float4 Position : SV_Position, float3 Normal : NORMAL) : SV_Target\n"
    "{\n"
    "    return float4(Active.Shade(normalize(Normal)), 1.0f);\n"
    "}\n";

static const char DcVsLevel9[] =
    "float4x4 Transform;\n"
    "struct VsOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; };\n"
    "VsOut main(float4 Position : POSITION, float2 Uv : TEXCOORD0)\n"
    "{\n"
    "    VsOut Out;\n"
    "    Out.Position = mul(Position, Transform);\n"
    "    Out.Uv = Uv;\n"
    "    return Out;\n"
    "}\n";

static const char DcPsLevel9[] =
    "Texture2D Diffuse : register(t0);\n"
    "SamplerState Linear : register(s0);\n"
    "float4 main(float4 Position : SV_Position, float2 Uv : TEXCOORD0) : SV_Target\n"
    "{\n"
    "    return Diffuse.Sample(Linear, Uv) * 0.5f;\n"
    "}\n";

static const char DcPsStencil[] =
    "struct PsOut { float4 Color : SV_Target; uint Stencil : SV_StencilRef; };\n"
    "PsOut main(float4 Position : SV_Position, uint Inner : SV_InnerCoverage)\n"
    "{\n"
    "    PsOut Out;\n"
    "    Out.Color = float4(Inner, 0.0f, 0.0f, 1.0f);\n"
    "    Out.Stencil = (uint)Position.x & 0xFF;\n"
    "    return Out;\n"
    "}\n";

static const char DcPsCalls[] =
    "cbuffer Params : register(b0) { float4 Values[16]; uint Count; };\n"
    "float4 Accumulate(uint Start, uint Stop)\n"
    "{\n"
    "    float4 Sum = 0;\n"
    "    [loop] for (uint Index = Start; Index < Stop; Index++)\n"
    "        Sum += Values[Index & 15];\n"
    "    return Sum;\n"
    "}\n"
    "float4 main(float4 Position : SV_Position) : SV_Target\n"
    "{\n"
    "    float4 A = Accumulate(0, Count);\n"
    "    float4 B = Accumulate(Count, 16);\n"
    "    [branch] if (A.x > B.x)\n"
    "        return A - B;\n"
    "    precise float4 R = A * B + Position;\n"
    "    return R;\n"
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
    { "vs40_instanced", "vs_4_0", "main", DcVsInstanced, 0 },
    { "vs50_skinned", "vs_5_0", "main", DcVsSkinned, 0 },
    { "vs40_integers", "vs_4_0", "main", DcVsIntegers, 0 },
    { "vs50_integers", "vs_5_0", "main", DcVsIntegers, 0 },
    { "vs40_math", "vs_4_0", "main", DcVsMath, 0 },
    { "vs50_icb", "vs_5_0", "main", DcVsIcb, 0 },
    { "vs40_icb", "vs_4_0", "main", DcVsIcb, 0 },
    { "vs50_buffers", "vs_5_0", "main", DcVsBuffers, 0 },
    { "ps41_textures", "ps_4_1", "main", DcPsTextures, 0 },
    { "ps50_textures", "ps_5_0", "main", DcPsTextures, 0 },
    { "ps50_gather", "ps_5_0", "main", DcPsGather, 0 },
    { "ps40_depth", "ps_4_0", "main", DcPsDepth, 0 },
    { "ps50_depth", "ps_5_0", "main", DcPsDepth, 0 },
    { "ps50_early_depth", "ps_5_0", "main", DcPsDepthGe, 0 },
    { "ps50_conservative", "ps_5_0", "main", DcPsConservativeDepth, 0 },
    { "ps41_interpolation", "ps_4_1", "main", DcPsInterpolation, 0 },
    { "ps50_evaluate", "ps_5_0", "main", DcPsEvaluate, 0 },
    { "ps50_uav", "ps_5_0", "main", DcPsUav, 0 },
    { "ps40_flow", "ps_4_0", "main", DcPsFlow, 0 },
    { "ps50_flow", "ps_5_0", "main", DcPsFlow, 0 },
    { "ps50_bits", "ps_5_0", "main", DcPsBits, 0 },
    { "ps50_double", "ps_5_0", "main", DcPsDouble, 0 },
    { "ps50_minprecision", "ps_5_0", "main", DcPsMinPrecision, 0 },
    { "ps50_stencil", "ps_5_0", "main", DcPsStencil, 0 },
    { "ps50_calls", "ps_5_0", "main", DcPsCalls, 0 },
    { "ps50_interfaces", "ps_5_0", "main", DcPsInterfaces, 0 },
    { "gs40_points", "gs_4_0", "main", DcGsPoints, 0 },
    { "gs40_lines", "gs_4_0", "main", DcGsLines, 0 },
    { "gs50_streams", "gs_5_0", "main", DcGsStreams, 0 },
    { "gs50_instanced", "gs_5_0", "main", DcGsInstanced, 0 },
    { "hs50_quad", "hs_5_0", "main", DcHsQuad, 0 },
    { "ds50_quad", "ds_5_0", "main", DcDsQuad, 0 },
    { "hs50_line", "hs_5_0", "main", DcHsLine, 0 },
    { "ds50_line", "ds_5_0", "main", DcDsLine, 0 },
    { "hs50_tri_even", "hs_5_0", "main", DcHsTriEven, 0 },
    { "cs40_structured", "cs_4_0", "main", DcCs4Structured, 0 },
    { "cs41_structured", "cs_4_1", "main", DcCs4Structured, 0 },
    { "cs50_atomics", "cs_5_0", "main", DcCsAtomics, 0 },
    { "cs50_counters", "cs_5_0", "main", DcCsCounters, 0 },
    { "cs50_typed", "cs_5_0", "main", DcCsTyped, 0 },
    { "ps51_arrays", "ps_5_1", "main", DcPsArrays51, D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES },
    { "cs51_arrays", "cs_5_1", "main", DcCsArrays51, D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES },
    { "cs51_atomics", "cs_5_1", "main", DcCsAtomics, 0 },
    { "hs51_quad", "hs_5_1", "main", DcHsQuad, 0 },
    { "ds51_quad", "ds_5_1", "main", DcDsQuad, 0 },
    { "gs51_streams", "gs_5_1", "main", DcGsStreams, 0 },
    { "vs40_level9", "vs_4_0_level_9_1", "main", DcVsLevel9, 0 },
    { "ps40_level9", "ps_4_0_level_9_3", "main", DcPsLevel9, 0 },
    { "vs50_transform_noopt", "vs_5_0", "main", DcVsTransform, D3DCOMPILE_SKIP_OPTIMIZATION },
    { "vs50_transform_rowmajor", "vs_5_0", "main", DcVsTransform, D3DCOMPILE_PACK_MATRIX_ROW_MAJOR },
    { "ps50_texture_debug", "ps_5_0", "main", DcPsTexture, D3DCOMPILE_DEBUG },
    { "ps50_flow_debug_noopt", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION },
    { "ps50_flow_ieee", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_IEEE_STRICTNESS },
    { "ps50_flow_avoid", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_AVOID_FLOW_CONTROL },
    { "ps50_flow_prefer", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_PREFER_FLOW_CONTROL },
    { "ps50_flow_partial", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_PARTIAL_PRECISION },
    { "ps50_flow_o3", "ps_5_0", "main", DcPsFlow, D3DCOMPILE_OPTIMIZATION_LEVEL3 },
    { "ps51_spaces_bound", "ps_5_1", "main", DcPsSpaces, D3DCOMPILE_ALL_RESOURCES_BOUND },
    { "cs51_arrays_alias", "cs_5_1", "main", DcCsArrays51, D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES | D3DCOMPILE_RESOURCES_MAY_ALIAS },
};
