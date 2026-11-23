/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader token checks made before a shader is created
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

static constexpr USHORT D9OpDcl = 31;
static constexpr USHORT D9OpDefb = 47;
static constexpr USHORT D9OpDefi = 48;
static constexpr USHORT D9OpDef = 81;
static constexpr USHORT D9OpPhase = 0xFFFD;
static constexpr USHORT D9OpComment = 0xFFFE;
static constexpr USHORT D9OpEnd = 0xFFFF;

static
UINT
D9RegisterNumber(
    _In_ DWORD Token)
{
    return Token & 0x7FF;
}

/* Keeps a defined register in order, once */
static
VOID
D9AddDef(
    _Inout_updates_(D9MaxShaderDefs) USHORT* Defs,
    _Inout_ UINT* Count,
    _In_ UINT Number)
{
    UINT Place = 0;

    while (Place < *Count && Defs[Place] < Number)
        Place += 1;
    if ((Place < *Count && Defs[Place] == Number) || *Count >= D9MaxShaderDefs)
        return;
    MoveMemory(&Defs[Place + 1], &Defs[Place], (*Count - Place) * sizeof(*Defs));
    Defs[Place] = static_cast<USHORT>(Number);
    *Count += 1;
}


/* Sizes, definitions and instruction count of a shader the validator already accepted */
static
VOID
D9MeasureShader(
    _In_ const DWORD* Tokens,
    _Out_ D9ShaderLayout* Layout)
{
    UINT Version = Tokens[0] & 0xFFFF;
    const DWORD* Token = Tokens + 1;
    UINT Comments = 0;

    while (*Token != D9OpEnd)
    {
        DWORD Instruction = *Token;
        USHORT Opcode = static_cast<USHORT>(Instruction & 0xFFFF);
        UINT Length;

        if (Opcode == D9OpComment)
        {
            Length = (Instruction >> 16) & 0x7FFF;
            Comments += Length + 1;
            Token += Length + 1;
            continue;
        }
        if (Opcode == D9OpDef || Opcode == D9OpDefi)
            Length = 5;
        else if (Opcode == D9OpDefb || Opcode == D9OpDcl)
            Length = 2;
        else if (Version >= 0x200)
            Length = (Instruction >> 24) & 0xF;
        else
        {
            Length = 0;
            while ((Token[Length + 1] & 0x80000000) != 0)
                Length += 1;
        }
        if (Opcode == D9OpDef)
            D9AddDef(Layout->FloatDefs, &Layout->FloatDefCount, D9RegisterNumber(Token[1]));
        else if (Opcode == D9OpDefi)
            D9AddDef(Layout->IntDefs, &Layout->IntDefCount, D9RegisterNumber(Token[1]));
        else if (Opcode == D9OpDefb)
            D9AddDef(Layout->BoolDefs, &Layout->BoolDefCount, D9RegisterNumber(Token[1]));
        else if (Opcode != D9OpDcl && Opcode != D9OpPhase)
            Layout->Instructions += 1;
        Token += Length + 1;
    }
    Layout->Size = static_cast<UINT>(Token - Tokens + 1) * sizeof(DWORD);
    Layout->StrippedSize = Layout->Size - Comments * sizeof(DWORD);
}

BOOLEAN
D9CheckShaderTokens(
    _In_ const DWORD* Tokens,
    _In_ BOOLEAN Pixel,
    _In_ const D9_SHADER_CHECK_DEVICE* Device,
    _Out_ D9ShaderLayout* Layout)
{
    D9_SHADER_CHECK_RESULT Result;

    ZeroMemory(Layout, sizeof(*Layout));
    if (!D9ValidateShader(Tokens, Pixel, Device, 0, nullptr, nullptr, &Result))
        return FALSE;
    D9MeasureShader(Tokens, Layout);
    Layout->Outputs = Result.Outputs;
    Layout->HardwareCapable = Result.HardwareCapable;
    Layout->SoftwareCapable = Result.SoftwareCapable;
    return TRUE;
}
