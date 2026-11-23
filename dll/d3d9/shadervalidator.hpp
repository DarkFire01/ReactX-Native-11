/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader token validation shared by shader creation and IDirect3DShaderValidator9
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d9.h>

/* Receives each message: the instruction's file and line, a level, the message number and its text */
typedef HRESULT (WINAPI* D9_SHADER_MESSAGE_SINK)(
    _In_opt_z_ PCSTR File,
    _In_ INT Line,
    _In_ DWORD Level,
    _In_ DWORD Id,
    _In_z_ PCSTR Text,
    _In_opt_ PVOID Context);

/* The device a shader is checked against; without one only the shader model limits apply */
struct D9_SHADER_CHECK_DEVICE
{
    const D3DCAPS9* Caps;
    DWORD Behavior;
    D3DDEVTYPE Type;
};

/* Checks that fail only for one kind of vertex processing */
static constexpr DWORD D9ShaderCheckHardware = 0x1;
static constexpr DWORD D9ShaderCheckSoftware = 0x2;

/* What a check leaves behind besides its verdict */
struct D9_SHADER_CHECK_RESULT
{
    BOOLEAN HardwareCapable;
    BOOLEAN SoftwareCapable;
    DWORD Outputs;
};

BOOLEAN
D9ValidateShader(
    _In_ const DWORD* Tokens,
    _In_ BOOLEAN Pixel,
    _In_opt_ const D9_SHADER_CHECK_DEVICE* Device,
    _In_ DWORD Flags,
    _In_opt_ D9_SHADER_MESSAGE_SINK Sink,
    _In_opt_ PVOID Context,
    _Out_ D9_SHADER_CHECK_RESULT* Result);

/* The object Direct3DShaderValidatorCreate9 returns */
void*
D9CreateShaderValidatorObject(VOID);
