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
};
