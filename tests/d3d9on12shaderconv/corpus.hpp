/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 assembly shaders fed to both shader converters
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* A vertex element as the device describes it: usage, usage index, input register, conversion */
static constexpr NineScInput
ScInput(
    _In_ UINT Usage,
    _In_ UINT Index,
    _In_ UINT Register,
    _In_ UINT Conversion)
{
    return static_cast<NineScInput>(Usage | (Index << 4) | (Register << 8) | (Conversion << 13));
}

struct ScCase
{
    PCSTR Name;
    BOOL Pixel;
    UINT Flags;
    PCSTR Source;
    NineScInput Inputs[8];
    UINT InputCount;
};

static const ScCase ScCorpus[] =
{
    {
        "vs20_mov", FALSE, 0,
        "vs_2_0\n"
        "dcl_position v0\n"
        "mov oPos, v0\n",
        { ScInput(0, 0, 0, 0) }, 1
    },
    {
        "vs11_arith", FALSE, 0,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_color v2\n"
        "dcl_texcoord v3\n"
        "add r0, v0, c0\n"
        "sub r1, r0, -c1\n"
        "mad r2, r1, c2, v1\n"
        "mul r3, r2.yzxw, c3.x\n"
        "rcp r4.x, r3.y\n"
        "rsq r4.y, -r3.z\n"
        "dp3 r5.x, v1, c4\n"
        "dp4 r5.y, v0, c5\n"
        "mov r5.zw, r4.xxxy\n"
        "min r6, r5, c6\n"
        "max r7, r6, -c7\n"
        "slt r8, r7, v2\n"
        "sge r9, r8, v3\n"
        "frc r10.xy, r9\n"
        "mov oPos, r0\n"
        "mov oD0, r4.xyxy\n"
        "mov oD1, r5\n"
        "mov oT0, r6\n"
        "mov oT1.xy, r10\n"
        "mov oFog, r7.x\n"
        "mov oPts, r8.y\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0), ScInput(10, 0, 2, 2), ScInput(5, 0, 3, 0) }, 4
    },
    {
        "vs11_address", FALSE, 0,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_blendindices v1\n"
        "def c90, 1, 2, 3, 4\n"
        "mov a0.x, v1.x\n"
        "add r0, v0, c[a0.x + 3]\n"
        "mul r0, r0, c90\n"
        "mov oPos, r0\n",
        { ScInput(0, 0, 0, 0), ScInput(2, 0, 1, 1) }, 2
    },
    {
        "vs11_missing", FALSE, 0,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_color v1\n"
        "dcl_texcoord v2\n"
        "dcl_psize v3\n"
        "dcl_normal v4\n"
        "mov oPos, v0\n"
        "mov oD0, v1\n"
        "mov oT0, v2\n"
        "mov r0, v3\n"
        "add oT1, r0, v4\n",
        { ScInput(0, 0, 0, 0) }, 1
    },
    {
        "vs20_def", FALSE, 0,
        "vs_2_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "def c3, 0.5, -1, 2, 0\n"
        "defi i0, 4, 0, 1, 0\n"
        "defb b0, true\n"
        "mul r0, v0, c3\n"
        "dp4 oPos.x, r0, c0\n"
        "dp4 oPos.y, r0, c1\n"
        "dp4 oPos.z, r0, c2\n"
        "mov oPos.w, c3.y\n"
        "mov oD0, v1\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 3) }, 2
    },
    {
        "vs30_outputs", FALSE, 0,
        "vs_3_0\n"
        "dcl_position v0\n"
        "dcl_texcoord3 v1\n"
        "dcl_position o0\n"
        "dcl_texcoord2 o1.xy\n"
        "dcl_color o2\n"
        "dcl_fog o3.x\n"
        "abs r0, v1\n"
        "mov o0, v0\n"
        "mov o1.xy, r0\n"
        "mov o2, -r0.wzyx\n"
        "mov o3.x, v1.z\n",
        { ScInput(0, 0, 0, 0), ScInput(5, 3, 1, 0) }, 2
    },
    {
        "vs11_matrix", FALSE, 0,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_blendindices v2\n"
        "def c40, 1, 0, 0, 0\n"
        "def c41, 0, 1, 0, 0\n"
        "m4x4 oPos, v0, c0\n"
        "m4x3 r0.xyz, v0, c4\n"
        "m3x4 r1, -v1.zxyw, c8\n"
        "m3x3 r2.xyz, r1, c12\n"
        "m3x2 r3.xy, r2, c40\n"
        "mov a0.x, v2.x\n"
        "m4x4 r4, v0, c[a0.x + 20]\n"
        "m3x3 oD0.xyz, r0, c[a0.x + 30]\n"
        "mov oD0.w, r3.x\n"
        "mov oT0, r4\n"
        "mov oT1.xy, r3\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0), ScInput(2, 0, 2, 1) }, 3
    },
    {
        "vs20_matrix", FALSE, 0,
        "vs_2_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "m4x4 r0, v0, c0\n"
        "m4x3 r1.xyz, v0.wzyx, c4\n"
        "m3x3 r3.xyz, -r1, c8\n"
        "m3x2 r2.xy, v1, c11\n"
        "mov r2.zw, r3.xxxy\n"
        "m3x4 oPos, r0, c13\n"
        "mov oT0, r2\n"
        "mov oT1.xyz, r1\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0) }, 2
    },
    {
        "vs11_scalar", FALSE, 0,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "exp r0, v0.x\n"
        "log r1, -v0.y\n"
        "expp r2, v1.z\n"
        "logp r3, v1.w\n"
        "lit r4, v1\n"
        "dst r5, r0, r1\n"
        "exp r6.xy, -r5.w\n"
        "log r6.zw, r4.z\n"
        "expp r7.yz, r3.x\n"
        "logp r7.xw, -r2.y\n"
        "lit r8.yz, -r7.wzyx\n"
        "dst r8.xw, r6, -r7\n"
        "mov oPos, v0\n"
        "mov oD0, r8\n"
        "mov oD1, r5\n"
        "mov oT0, r4\n"
        "mov oT1, r6\n"
        "mov oT2, r7\n"
        "mov oT3, r3\n"
        "mov oT4, r2\n"
        "mov oT5, r1\n"
        "mov oT6, r0\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0) }, 2
    },
    {
        "vs20_scalar", FALSE, 0,
        "vs_2_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_texcoord v2\n"
        "def c0, -1.5574077, -0.020833334, -0.125, 1\n"
        "def c1, -0.0013888889, 0.0416666679, 0.5, 0.5\n"
        "expp r0, v0.x\n"
        "logp r1, v0.y\n"
        "expp r2.yz, -v1.w\n"
        "logp r3.x, -v1.z\n"
        "pow r4, v0.x, r0.y\n"
        "pow r4.yz, -r0.w, v2.x\n"
        "mov r11, v1\n"
        "crs r5.xyz, v0, r11\n"
        "crs r6.xz, -r5, r11\n"
        "sgn r7, v2, r8, r9\n"
        "sgn r7.yw, -r5.wzyx, r8, r9\n"
        "nrm r8, v1\n"
        "nrm r9.xyz, -r5\n"
        "sincos r10.xy, v0.z, c0, c1\n"
        "sincos r10.x, -r4.y, c0, c1\n"
        "sincos r10.y, r4.x, c0, c1\n"
        "lrp r11, v0, r11, r5\n"
        "lrp r11.zw, -r0.x, r1.yzxw, -r2\n"
        "mov oPos, v0\n"
        "mov oD0, r1\n"
        "mov oD1, r2\n"
        "mov oT0, r3\n"
        "mov oT1, r4\n"
        "mov oT2, r6\n"
        "mov oT3, r7\n"
        "mov oT4, r8\n"
        "mov oT5, r9\n"
        "mov oT6, r10\n"
        "mov oT7, r11\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0), ScInput(5, 0, 2, 0) }, 3
    },
    {
        "vs30_scalar", FALSE, 0,
        "vs_3_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_position o0\n"
        "dcl_texcoord o1\n"
        "dcl_texcoord1 o2\n"
        "dcl_texcoord2 o3\n"
        "sincos r0.xy, v0.x\n"
        "sincos r0.y, -v1.w\n"
        "pow r1, v0.y, r0.z\n"
        "nrm r2.xyz, v1\n"
        "lit r3, v1.zyxw\n"
        "expp r4, v0.z\n"
        "logp r4.y, v0.w\n"
        "mov o0, v0\n"
        "mov o1, r0\n"
        "mov_sat o2, r1\n"
        "add o3, r2, r3\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0) }, 2
    },
    {
        "vs20_flow", FALSE, 0,
        "vs_2_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "defi i0, 3, 0, 1, 0\n"
        "defb b0, true\n"
        "mov r0, v0\n"
        "mov r1, c0\n"
        "loop aL, i0\n"
        "add r1, r1, c[aL + 4]\n"
        "endloop\n"
        "rep i1\n"
        "mul r1, r1, c2\n"
        "endrep\n"
        "if b0\n"
        "add r0, r0, r1\n"
        "else\n"
        "add r0, r0, -r1\n"
        "endif\n"
        "if b1\n"
        "call l0\n"
        "endif\n"
        "callnz l1, b2\n"
        "callnz l1, !b0\n"
        "mov oPos, r0\n"
        "mov oD0, r1\n"
        "ret\n"
        "label l0\n"
        "add r0, r0, c3\n"
        "ret\n"
        "label l1\n"
        "mul r1, r1, c3\n"
        "ret\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0) }, 2
    },
    {
        "vs2x_flow", FALSE, 0,
        "vs_2_x\n"
        "dcl_position v0\n"
        "dcl_texcoord v1\n"
        "defi i0, 4, 2, 1, 0\n"
        "mov r0, v0\n"
        "mov r2, c0\n"
        "rep i0\n"
        "if_gt r0.x, c1.x\n"
        "add r0.x, r0.x, -c1.y\n"
        "break_lt r0.y, -c1.z\n"
        "else\n"
        "add r0.y, r0.y, c1.w\n"
        "endif\n"
        "setp_ge p0, r0, c2\n"
        "(p0) add r2, r2, c3\n"
        "(!p0.y) mov r2.xz, r0\n"
        "if p0.x\n"
        "break\n"
        "add r0, r0, r0\n"
        "endif\n"
        "if_le r2.w, r0.z\n"
        "break_ne r2.x, c4.y\n"
        "break_eq r2.y, c4.x\n"
        "break_ge r2.z, r0.w\n"
        "break\n"
        "endif\n"
        "breakp !p0.z\n"
        "setp_lt p0.xy, r2.wzyx, -c5\n"
        "setp_ne p0.z, r0, c5\n"
        "setp_eq p0.w, r0.y, c6\n"
        "setp_gt p0.zw, r2, c7\n"
        "setp_le p0.x, r0.x, c7.y\n"
        "endrep\n"
        "callnz l0, p0.w\n"
        "callnz l1, !p0.x\n"
        "if_eq r0.x, r0.y\n"
        "if_ne r0.z, r0.w\n"
        "if_lt r2.x, c8.x\n"
        "if_ge r2.y, c8.y\n"
        "mova a0.x, r2.y\n"
        "mova a0.yzw, -r2.wzyx\n"
        "add r0, r0, c[a0.x + 10]\n"
        "endif\n"
        "endif\n"
        "endif\n"
        "endif\n"
        "if !p0.y\n"
        "(p0.x) mov r0.y, c9.x\n"
        "endif\n"
        "mov oPos, r0\n"
        "mov oT0, r2\n"
        "ret\n"
        "label l0\n"
        "mov r0, v1\n"
        "ret\n"
        "label l1\n"
        "ret\n",
        { ScInput(0, 0, 0, 0), ScInput(5, 0, 1, 0) }, 2
    },
    {
        "vs30_flow", FALSE, 0,
        "vs_3_0\n"
        "dcl_position v0\n"
        "dcl_texcoord v1\n"
        "dcl_position o0\n"
        "dcl_texcoord o1\n"
        "dcl_texcoord1 o2\n"
        "dcl_texcoord2 o3\n"
        "defi i0, 2, 1, 1, 0\n"
        "defi i1, 3, 0, 2, 0\n"
        "mov r0, v0\n"
        "mov o0, v0\n"
        "loop aL, i0\n"
        "mov o[aL], r0\n"
        "loop aL, i1\n"
        "add r0, r0, c[aL]\n"
        "rep i2\n"
        "mul r0, r0, c10\n"
        "break_eq r0.x, c11.x\n"
        "loop aL, i3\n"
        "mad r0, r0, c[aL + 12], v1\n"
        "endloop\n"
        "endrep\n"
        "endloop\n"
        "mov o[aL + 1].xy, v1\n"
        "endloop\n"
        "mov o3, r0\n",
        { ScInput(0, 0, 0, 0), ScInput(5, 0, 1, 0) }, 2
    },
    {
        "vs30_texldl", FALSE, 0,
        "vs_3_0\n"
        "dcl_position v0\n"
        "dcl_texcoord v1\n"
        "dcl_2d s0\n"
        "dcl_cube s1\n"
        "dcl_volume s2\n"
        "dcl_2d s3\n"
        "dcl_position o0\n"
        "dcl_texcoord o1\n"
        "dcl_texcoord1 o2\n"
        "dcl_texcoord2 o3\n"
        "texldl r0, v1, s0\n"
        "texldl r1, v1.wzyx, s1\n"
        "texldl r2.xy, r0, s2\n"
        "texldl r3, r1, s3\n"
        "setp_gt p0, r0, c0\n"
        "(p0.y) texldl r3.yz, r2, s0\n"
        "mov o0, v0\n"
        "mov o1, r0\n"
        "add o2, r1, r2\n"
        "mov o3, r3\n",
        { ScInput(0, 0, 0, 0), ScInput(5, 0, 1, 0) }, 2
    },
    {
        "vs11_patch", FALSE, 1,
        "vs_1_1\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_blendindices v2\n"
        "def c20, 0, 1, 2, 3\n"
        "def c21, 2, 0, 0, 0\n"
        "mul r0, v0, c0\n"
        "mul r1, r0, r0\n"
        "mul r1, r1, c20\n"
        "mul r2, c21, v1\n"
        "mad r3, v0, -c1, r1\n"
        "mad r3, -r3, r2.yzxw, c20\n"
        "dp3 r4.x, v1, c2\n"
        "dp4 r4.y, r3, r3\n"
        "dp4 r4.z, -r3, r3\n"
        "mov a0.x, v2.x\n"
        "dp4 r4.w, v0, c[a0.x + 5]\n"
        "m4x4 r5, v0, c8\n"
        "m3x3 r6.xyz, v1, c12\n"
        "lit r7, r4\n"
        "dst r8, r4, r5\n"
        "mov oPos, r5\n"
        "mov oD0.xyz, r6\n"
        "mov oD0.w, r7.x\n"
        "mov oD1, r7\n"
        "mov oT0, r8\n"
        "mov oT1, r3\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0), ScInput(2, 0, 2, 1) }, 3
    },
    {
        "vs20_patch", FALSE, 1,
        "vs_2_0\n"
        "dcl_position v0\n"
        "dcl_normal v1\n"
        "dcl_texcoord v2\n"
        "defi i0, 2, 0, 1, 0\n"
        "mov r0, v0\n"
        "loop aL, i0\n"
        "mad r0, r0, c[aL], v2\n"
        "endloop\n"
        "nrm r1, v1\n"
        "crs r2.xyz, r1, r0\n"
        "pow r3, r0.x, r1.y\n"
        "lrp r4, r0, r1, r2\n"
        "sincos r5.xy, r0.x, c5, c6\n"
        "m3x2 r6.xy, r4, c7\n"
        "mul r6.zw, v2, r5.y\n"
        "mov oPos, r0\n"
        "mov oT0, r2\n"
        "mov oT1, r3\n"
        "mov oT2, r4\n"
        "mov oT3, r6\n",
        { ScInput(0, 0, 0, 0), ScInput(3, 0, 1, 0), ScInput(5, 0, 2, 1) }, 3
    },
    {
        "vs30_indexed", FALSE, 0,
        "vs_3_0\n"
        "dcl_position v0\n"
        "dcl_texcoord v1\n"
        "dcl_texcoord1 v2\n"
        "dcl_position o0\n"
        "dcl_texcoord o1\n"
        "dcl_texcoord1 o2.xy\n"
        "defi i0, 2, 1, 1, 0\n"
        "defb b0, true\n"
        "mov r0, c0\n"
        "loop aL, i0\n"
        "add r0, r0, v[aL]\n"
        "if_gt r0.x, c1.x\n"
        "break\n"
        "endif\n"
        "mova a0.x, r0.w\n"
        "mov o[aL + 1].xy, c[a0.x + 3]\n"
        "break\n"
        "endloop\n"
        "rep i1\n"
        "add r0, r0, c2\n"
        "break\n"
        "endrep\n"
        "mov o0, v0\n"
        "call l0\n"
        "ret\n"
        "label l0\n"
        "if b0\n"
        "mov o1, r0\n"
        "else\n"
        "mov o1, -r0\n"
        "endif\n"
        "ret\n",
        { ScInput(0, 0, 0, 0), ScInput(5, 0, 1, 0), ScInput(5, 1, 2, 0) }, 3
    },
    {
        "ps20_mov", TRUE, 0,
        "ps_2_0\n"
        "dcl v0\n"
        "mov oC0, v0\n",
        { }, 0
    },
    {
        "ps20_arith", TRUE, 0,
        "ps_2_0\n"
        "dcl v0\n"
        "dcl v1\n"
        "dcl t0\n"
        "dcl t1\n"
        "def c0, 0.5, 1, 0, 0.25\n"
        "add r0, v0, c0\n"
        "mul r1, r0, v1\n"
        "mad r2, r1, c1, -r0\n"
        "dp3 r3.x, r2, t0\n"
        "dp4 r3.y, r2, t1\n"
        "rcp r3.z, r3.x\n"
        "rsq r3.w, r3.y\n"
        "min r4, r3, c2\n"
        "max r5, r4, -c3\n"
        "cmp r6, r5, v0, -r1.wzyx\n"
        "lrp r7, v0.x, r6, r5\n"
        "dp2add r8, t1, c0, c0.w\n"
        "abs r9, r8\n"
        "frc r10, r9\n"
        "exp r11.x, r10.x\n"
        "log r11.y, r10.y\n"
        "pow r11.z, r10.z, c0.y\n"
        "mov_sat r11.w, r7.w\n"
        "nrm r9.xyz, r3\n"
        "sincos r8.xy, r5.x, c4, c5\n"
        "crs r6.xyz, r7, r9\n"
        "add r6, r6, r8.y\n"
        "mov_pp oC0, r11\n"
        "mov oC1, r6\n",
        { }, 0
    },
    {
        "ps20_tex", TRUE, 0,
        "ps_2_0\n"
        "dcl t0\n"
        "dcl t1\n"
        "dcl_pp t2.xy\n"
        "dcl_2d s0\n"
        "dcl_cube s1\n"
        "dcl_volume s2\n"
        "dcl_2d s3\n"
        "texld r0, t0, s0\n"
        "texldp r1, t1, s1\n"
        "texldb r2, t0, s2\n"
        "texld r3, r0, s3\n"
        "texld_pp r4, t2, s0\n"
        "texkill t1\n"
        "texkill r3\n"
        "mul r0, r0, r1\n"
        "add r0, r0, r2\n"
        "mad r0, r3, r4, r0\n"
        "mov oC0, r0\n",
        { }, 0
    },
    {
        "ps20_depth", TRUE, 0,
        "ps_2_0\n"
        "dcl v0\n"
        "dcl v1\n"
        "dcl t3\n"
        "dcl_2d s5\n"
        "texld r0, t3, s5\n"
        "mul r1, r0, v1\n"
        "mov oC0, v0\n"
        "mov oC1, r1\n"
        "mov oDepth, r0.z\n",
        { }, 0
    },
    {
        "ps2x_flow", TRUE, 0,
        "ps_2_x\n"
        "dcl v0\n"
        "dcl t0\n"
        "dcl_2d s0\n"
        "defi i0, 3, 0, 0, 0\n"
        "defb b0, true\n"
        "def c0, 0, 0.5, 1, 2\n"
        "mov r0, c0.x\n"
        "rep i0\n"
        "add r0, r0, v0\n"
        "endrep\n"
        "if b0\n"
        "mul r0, r0, c0.y\n"
        "else\n"
        "add r0, r0, c1\n"
        "endif\n"
        "if_gt r0.x, c0.y\n"
        "mov r0.y, c0.z\n"
        "endif\n"
        "setp_lt p0, r0, c0.zzzz\n"
        "(p0) mov r0, c2\n"
        "(!p0.x) add r0.x, r0.x, c0.w\n"
        "dsx r1, t0\n"
        "dsy r2, t0\n"
        "texldd r3, t0, s0, r1, r2\n"
        "if b1\n"
        "texld r4, t0, s0\n"
        "add r3, r3, r4\n"
        "endif\n"
        "add r0, r0, r3\n"
        "mov oC0, r0\n",
        { }, 0
    },
    {
        "ps30_inputs", TRUE, 0,
        "ps_3_0\n"
        "dcl_color v0\n"
        "dcl_color1_centroid v1.xyz\n"
        "dcl_texcoord v2\n"
        "dcl_texcoord3 v3.xy\n"
        "dcl_fog v4.x\n"
        "dcl vPos.xy\n"
        "dcl vFace\n"
        "dcl_2d s0\n"
        "dcl_cube s1\n"
        "def c0, 0, 0.5, 1, 2\n"
        "mov r0, v1.xyzx\n"
        "add r0, r0, v0\n"
        "mul r0.xy, vPos, c1\n"
        "if_lt vFace, c0.x\n"
        "mov r0.z, -c0.z\n"
        "endif\n"
        "texld r1, v2, s0\n"
        "texldl r2, v3.xyyy, s0\n"
        "texldb r3, v2, s1\n"
        "texldp r4, v2, s1\n"
        "add r1, r1, r2\n"
        "mad r1, r3, r4, r1\n"
        "mul r0.w, v4.x, r1.w\n"
        "mov oC0, r0\n"
        "mov oC1, r1\n"
        "mov oDepth, r1.x\n",
        { }, 0
    },
    {
        "ps30_loops", TRUE, 0,
        "ps_3_0\n"
        "dcl_texcoord v0\n"
        "dcl_texcoord1 v1\n"
        "dcl_texcoord2 v2\n"
        "dcl_texcoord3 v3\n"
        "dcl_2d s2\n"
        "defi i0, 3, 0, 1, 0\n"
        "def c0, 0, 0.5, 1, 2\n"
        "mov r0, c0.x\n"
        "loop aL, i0\n"
        "add r0, r0, v[aL]\n"
        "add r0, r0, c4\n"
        "break_ge r0.x, c0.w\n"
        "endloop\n"
        "rep i1\n"
        "texld r1, r0, s2\n"
        "add r0, r0, r1\n"
        "endrep\n"
        "call l0\n"
        "mov oC0, r0\n"
        "ret\n"
        "label l0\n"
        "texldl r2, v1, s2\n"
        "add r0, r0, r2\n"
        "ret\n",
        { }, 0
    },
    {
        "ps30_misc", TRUE, 0,
        "ps_3_0\n"
        "dcl_texcoord v0\n"
        "dcl_texcoord1_centroid v1\n"
        "dcl_color v2\n"
        "dcl_2d s0\n"
        "dcl_volume s3\n"
        "def c0, 0, 0.5, 1, 2\n"
        "defb b2, false\n"
        "defi i1, 2, 0, 1, 0\n"
        "sincos r0.xy, v0.x\n"
        "sincos r0.y, v0.y\n"
        "mov_sat r0.zw, r0.xxxy\n"
        "nrm_pp r1.xyz, v1\n"
        "mov r1.w, c0.z\n"
        "add r2, r1, -v2_abs\n"
        "texkill v1.xyw\n"
        "texkill r2.z\n"
        "setp_ge p0.xy, r2, c0.y\n"
        "(p0.y) texld r3, v0, s0\n"
        "(!p0.x) texldl r3, v1, s3\n"
        "callnz l1, b2\n"
        "callnz l1, !p0.y\n"
        "loop aL, i1\n"
        "breakp p0.x\n"
        "add r0, r0, v[aL]\n"
        "break_ne r0.x, c0.w\n"
        "endloop\n"
        "dsx_pp r4, r3\n"
        "dsy r5, r3.wzyx\n"
        "mov oC0, r0\n"
        "mov oC1, r1\n"
        "mov oC2, r3\n"
        "mad oC3, r4, r5, c1\n"
        "ret\n"
        "label l1\n"
        "lrp_sat r3, c0.y, r3, v2\n"
        "ret\n",
        { }, 0
    },
    {
        "ps20_centroid", TRUE, 0,
        "ps_2_0\n"
        "dcl_centroid t0\n"
        "dcl_centroid_pp t1.xy\n"
        "dcl v0.xyz\n"
        "dcl v1\n"
        "dcl_2d s0\n"
        "texld r0, t0, s0\n"
        "texld r1, t1, s0\n"
        "mad r0.xyz, r0, r1, v0\n"
        "mov r0.w, v1.w\n"
        "mov oC0, r0\n"
        "mov oC1, r1\n"
        "mov oC2, t0\n"
        "mov oC3, v1\n",
        { }, 0
    },

    {
        "ps11_arith", TRUE, 0,
        "ps_1_1\n"
        "def c0, 0.5, 0.25, 1, -0.5\n"
        "def c3, 2, -3, 0.75, 0\n"
        "tex t0\n"
        "tex t1\n"
        "texcoord t2\n"
        "texkill t3\n"
        "mul r0, t0, v0\n"
        "mad r1, t1, c0, v1_bias\n"
        "add_x2 r0, r0, -r1\n"
        "mul_sat r0.rgb, r0, c1\n"
        "+mov r0.a, t2.a\n"
        "sub_d2 r1, r0, c3\n"
        "dp3_x4 r1.rgb, t2_bx2, v1_bx2\n"
        "+add_sat r1.a, 1-v0, c2\n"
        "lrp r0, v0.a, r0, r1\n",
        { }, 0
    },
    {
        "ps12_arith", TRUE, 0,
        "ps_1_2\n"
        "def c2, 0.5, 0.25, 1, -0.5\n"
        "tex t0\n"
        "texcoord t1\n"
        "mov r0, t1_bx2\n"
        "cnd r1.rgb, r0.a, t0, c2\n"
        "+cnd r1.a, r0.a, c1, t0\n"
        "mad_x4 r0, r1, c4, -t0\n"
        "dp3_sat r1, r0, c5\n"
        "mul_d2 r0.rgb, r0, r1.a\n"
        "+sub_x2 r0.a, 1-r1, v1_bias\n"
        "add r0, r0, c6\n",
        { }, 0
    },
    {
        "ps14_shift", TRUE, 0,
        "ps_1_4\n"
        "def c0, 0.5, 0.25, 1, -0.5\n"
        "texcrd r0.rgb, t0\n"
        "texld r1, t1\n"
        "mul_d4 r2, r1, c0\n"
        "add_d8 r3.rgb, r2, r0_x2\n"
        "+mad_sat r3.a, r1.b, c1, -r2\n"
        "sub_x8 r4, r3, r1_bias\n"
        "lrp r0, r4, r1, 1-r3\n"
        "cnd r0.rgb, r4, r0, c2\n"
        "+mov_x4 r0.a, r4_bx2\n",
        { }, 0
    },


    {
        "ps11_bem", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texbem t1, t0\n"
        "texbeml t2, t0\n"
        "tex t3\n"
        "add r0, t1, t2\n"
        "mul r0, r0, t3\n",
        { }, 0
    },
    {
        "ps11_m3x2", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texm3x2pad t1, t0_bx2\n"
        "texm3x2tex t2, t0_bx2\n"
        "mov r0, t2\n",
        { }, 0
    },
    {
        "ps11_m3x3", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texm3x3pad t1, t0_bx2\n"
        "texm3x3pad t2, t0_bx2\n"
        "texm3x3tex t3, t0_bx2\n"
        "mov r0, t3\n",
        { }, 0
    },
    {
        "ps11_m3x3spec", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texm3x3pad t1, t0_bx2\n"
        "texm3x3pad t2, t0_bx2\n"
        "texm3x3spec t3, t0_bx2, c0\n"
        "mov r0, t3\n",
        { }, 0
    },
    {
        "ps11_m3x3vspec", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texm3x3pad t1, t0\n"
        "texm3x3pad t2, t0\n"
        "texm3x3vspec t3, t0\n"
        "mov r0, t3\n",
        { }, 0
    },
    {
        "ps11_reg2", TRUE, 0,
        "ps_1_1\n"
        "tex t0\n"
        "texreg2ar t1, t0\n"
        "texreg2gb t2, t0\n"
        "mul r0, t1, t2\n",
        { }, 0
    },
    {
        "ps12_ops", TRUE, 0,
        "ps_1_2\n"
        "tex t0\n"
        "texreg2rgb t1, t0\n"
        "texdp3tex t2, t0\n"
        "texdp3 t3, t0\n"
        "mul r0, t1, t2\n"
        "add r0, r0, t3\n",
        { }, 0
    },
    {
        "ps12_m3x3", TRUE, 0,
        "ps_1_2\n"
        "tex t0\n"
        "texm3x3pad t1, t0\n"
        "texm3x3pad t2, t0\n"
        "texm3x3 t3, t0\n"
        "mov r0, t3\n",
        { }, 0
    },
    {
        "ps13_depth", TRUE, 0,
        "ps_1_3\n"
        "tex t0\n"
        "texm3x2pad t1, t0\n"
        "texm3x2depth t2, t0\n"
        "cmp r0, t0, c0, c1\n",
        { }, 0
    },
    {
        "ps14_phases", TRUE, 0,
        "ps_1_4\n"
        "def c0, 0.5, 0.25, 1, -0.5\n"
        "texld r0, t0\n"
        "texcrd r1.rgb, t1\n"
        "texcrd r3.rgb, t3\n"
        "texcrd r2.rg, t2_dw.xyw\n"
        "texcrd r5.rgb, t5\n"
        "mul r1.rgb, r0, r1\n"
        "+mov r1.a, c0.b\n"
        "phase\n"
        "texld r4, r1\n"
        "texld r2, r3_dz\n"
        "texld r0, t4\n"
        "texdepth r5\n"
        "cmp r0, r0, r4, c1\n"

        "cnd r1, r0, r4, c0\n"
        "add_sat r0.rgb, r0, r1\n"
        "+mul r0.a, r2.a, c0.r\n",
        { }, 0
    },
    {
        "ps14_bem", TRUE, 0,
        "ps_1_4\n"
        "texld r1, t1\n"
        "texcrd r2.rgb, t0\n"
        "bem r2.rg, r2, r1\n"
        "phase\n"
        "texld r0, r2\n"
        "texkill t1\n"
        "mov r0, r0\n",
        { }, 0
    },
};
