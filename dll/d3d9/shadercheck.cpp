/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader token checks made before a shader is created
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Versions as major and minor in one number; zero means never */
#define D9V(Major, Minor) (((Major) << 8) | (Minor))
static constexpr UINT D9VersionAny = 0xFFFF;

/* The extended version 2 machines number themselves 2.1 */
static constexpr UINT D9Version2x = D9V(2, 1);

/* What one opcode takes, and the versions of each machine that have it */
struct D9OpcodeRule
{
    USHORT Opcode;
    BYTE Destination;
    BYTE Sources;
    USHORT VertexFirst;
    USHORT VertexLast;
    USHORT PixelFirst;
    USHORT PixelLast;
};

/* Opcodes whose parameters do not follow the common shape are checked on their own */
static constexpr USHORT D9OpDcl = 31;
static constexpr USHORT D9OpDefb = 47;
static constexpr USHORT D9OpDefi = 48;
static constexpr USHORT D9OpTexcoord = 64;
static constexpr USHORT D9OpTex = 66;
static constexpr USHORT D9OpSincos = 37;
static constexpr USHORT D9OpDef = 81;
static constexpr USHORT D9OpPhase = 0xFFFD;
static constexpr USHORT D9OpComment = 0xFFFE;
static constexpr USHORT D9OpEnd = 0xFFFF;

static const D9OpcodeRule D9OpcodeRules[] =
{
    { 0, 0, 0, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 1, 1, 1, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 2, 1, 2, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 3, 1, 2, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 4, 1, 3, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 5, 1, 2, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 6, 1, 1, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 7, 1, 1, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 8, 1, 2, D9V(1, 1), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 9, 1, 2, D9V(1, 1), D9VersionAny, D9V(1, 2), D9VersionAny },
    { 10, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 11, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 12, 1, 2, D9V(1, 1), D9VersionAny, 0, 0 },
    { 13, 1, 2, D9V(1, 1), D9VersionAny, 0, 0 },
    { 14, 1, 1, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 15, 1, 1, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 16, 1, 1, D9V(1, 1), D9VersionAny, 0, 0 },
    { 17, 1, 2, D9V(1, 1), D9VersionAny, 0, 0 },
    { 18, 1, 3, D9V(2, 0), D9VersionAny, D9V(1, 1), D9VersionAny },
    { 19, 1, 1, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 20, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 21, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 22, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 23, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 24, 1, 2, D9V(1, 1), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 25, 0, 1, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 26, 0, 2, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 27, 0, 2, D9V(2, 0), D9VersionAny, D9V(3, 0), D9VersionAny },
    { 28, 0, 0, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 29, 0, 0, D9V(2, 0), D9VersionAny, D9V(3, 0), D9VersionAny },
    { 30, 0, 1, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 32, 1, 2, D9V(2, 0), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 33, 1, 2, D9V(2, 0), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 34, 1, 3, D9V(2, 0), D9VersionAny, 0, 0 },
    { 35, 1, 1, D9V(2, 0), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 36, 1, 1, D9V(2, 0), D9VersionAny, D9V(2, 0), D9VersionAny },
    { 38, 0, 1, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 39, 0, 0, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 40, 0, 1, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 41, 0, 2, D9Version2x, D9VersionAny, D9Version2x, D9VersionAny },
    { 42, 0, 0, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 43, 0, 0, D9V(2, 0), D9VersionAny, D9Version2x, D9VersionAny },
    { 44, 0, 0, D9Version2x, D9VersionAny, D9Version2x, D9VersionAny },
    { 45, 0, 2, D9Version2x, D9VersionAny, D9Version2x, D9VersionAny },
    { 46, 1, 1, D9V(2, 0), D9VersionAny, 0, 0 },
    { 64, 1, 0, 0, 0, D9V(1, 1), D9V(1, 4) },
    { 65, 1, 0, 0, 0, D9V(1, 1), D9VersionAny },
    { 66, 1, 0, 0, 0, D9V(1, 1), D9VersionAny },
    { 67, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 68, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 69, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 70, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 71, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 72, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 73, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 74, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 76, 1, 2, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 77, 1, 1, 0, 0, D9V(1, 1), D9V(1, 3) },
    { 78, 1, 1, D9V(1, 1), D9VersionAny, 0, 0 },
    { 79, 1, 1, D9V(1, 1), D9VersionAny, 0, 0 },
    { 80, 1, 3, 0, 0, D9V(1, 1), D9V(1, 4) },
    { 82, 1, 1, 0, 0, D9V(1, 2), D9V(1, 3) },
    { 83, 1, 1, 0, 0, D9V(1, 2), D9V(1, 3) },
    { 84, 1, 1, 0, 0, D9V(1, 3), D9V(1, 3) },
    { 85, 1, 1, 0, 0, D9V(1, 2), D9V(1, 3) },
    { 86, 1, 1, 0, 0, D9V(1, 2), D9V(1, 3) },
    { 87, 1, 0, 0, 0, D9V(1, 4), D9V(1, 4) },
    { 88, 1, 3, 0, 0, D9V(1, 2), D9VersionAny },
    { 89, 1, 2, 0, 0, D9V(1, 4), D9V(1, 4) },
    { 90, 1, 3, 0, 0, D9V(2, 0), D9VersionAny },
    { 91, 1, 1, 0, 0, D9Version2x, D9VersionAny },
    { 92, 1, 1, 0, 0, D9Version2x, D9VersionAny },
    { 93, 1, 4, 0, 0, D9Version2x, D9VersionAny },
    { 94, 1, 2, D9Version2x, D9VersionAny, D9Version2x, D9VersionAny },
    { 95, 1, 2, D9V(3, 0), D9VersionAny, D9V(3, 0), D9VersionAny },
    { 96, 0, 1, D9Version2x, D9VersionAny, D9Version2x, D9VersionAny },
};

/* Register types a parameter names */
static constexpr UINT D9RegTemp = 0;
static constexpr UINT D9RegInput = 1;
static constexpr UINT D9RegTexture = 3;
static constexpr UINT D9RegRastOut = 4;
static constexpr UINT D9RegOutput = 6;
static constexpr UINT D9RegColorOut = 8;
static constexpr UINT D9RegDepthOut = 9;
static constexpr UINT D9RegSampler = 10;

/* Registers a check tracks of each kind */
static constexpr UINT D9TrackedRegisters = 32;

struct D9ShaderWalk
{
    BOOLEAN Pixel;
    UINT Version;
    BYTE Declared[11][D9TrackedRegisters];
    BYTE Written[11][D9TrackedRegisters];
    BYTE SamplerTypes[D9TrackedRegisters];
    BOOLEAN PositionWritten;
};

static
UINT
D9RegisterType(
    _In_ DWORD Token)
{
    return ((Token >> 28) & 7) | ((Token >> 8) & 0x18);
}

static
UINT
D9RegisterNumber(
    _In_ DWORD Token)
{
    return Token & 0x7FF;
}

/* Relative addressing adds a token after a parameter from version 2 on */
static
BOOLEAN
D9HasAddressToken(
    _In_ const D9ShaderWalk* Walk,
    _In_ DWORD Token)
{
    return Walk->Version >= D9V(2, 0) && (Token & (1u << 13)) != 0;
}

/* The components of a register a walk has seen declared or written */
static
BYTE
D9RegisterMask(
    _In_ const BYTE (*Masks)[D9TrackedRegisters],
    _In_ UINT Type,
    _In_ UINT Number)
{
    if (Type >= 11 || Number >= D9TrackedRegisters)
        return 0;
    return Masks[Type][Number];
}

static
VOID
D9AddRegisterMask(
    _Inout_ BYTE (*Masks)[D9TrackedRegisters],
    _In_ UINT Type,
    _In_ UINT Number,
    _In_ BYTE Mask)
{
    if (Type < 11 && Number < D9TrackedRegisters)
        Masks[Type][Number] |= Mask;
}

/* The components a source's swizzle picks for the positions an instruction reads */
static
BYTE
D9ComponentsRead(
    _In_ DWORD Token,
    _In_ BYTE Positions)
{
    BYTE Read = 0;

    for (UINT Position = 0; Position < 4; Position++)
    {
        if ((Positions & (1u << Position)) != 0)
            Read |= static_cast<BYTE>(1u << ((Token >> (16 + Position * 2)) & 3));
    }
    return Read;
}

/* A source must name components the shader declared or wrote before */
static
BOOLEAN
D9SourceReady(
    _In_ const D9ShaderWalk* Walk,
    _In_ DWORD Token,
    _In_ BYTE Positions)
{
    UINT Type = D9RegisterType(Token);
    UINT Number = D9RegisterNumber(Token);
    BYTE Needed = D9ComponentsRead(Token, Positions);

    switch (Type)
    {
    case D9RegTemp:
        return (D9RegisterMask(Walk->Written, Type, Number) & Needed) == Needed;
    case D9RegInput:
        if (Walk->Pixel && Walk->Version < D9V(2, 0))
            return TRUE;
        return (D9RegisterMask(Walk->Declared, Type, Number) & Needed) == Needed;
    case D9RegTexture:
        if (!Walk->Pixel)
            return TRUE;
        if (Walk->Version >= D9V(2, 0))
            return (D9RegisterMask(Walk->Declared, Type, Number) & Needed) == Needed;
        return Walk->Version == D9V(1, 4) || D9RegisterMask(Walk->Written, Type, Number) != 0;
    case D9RegSampler:
        return D9RegisterMask(Walk->Declared, Type, Number) != 0;
    default:
        return TRUE;
    }
}

/* Notes what an instruction writes */
static
VOID
D9NoteWrite(
    _Inout_ D9ShaderWalk* Walk,
    _In_ DWORD Token)
{
    UINT Type = D9RegisterType(Token);
    UINT Number = D9RegisterNumber(Token);

    D9AddRegisterMask(Walk->Written, Type, Number, static_cast<BYTE>((Token >> 16) & 0xF));
    if (Walk->Pixel)
        return;
    if (Type == D9RegRastOut && Number == 0)
        Walk->PositionWritten = TRUE;
    if (Type == D9RegOutput && Walk->Version >= D9V(3, 0) && D9RegisterMask(Walk->Declared, Type, Number) != 0)
        Walk->PositionWritten = TRUE;
}

/* Which positions of a source an instruction reads: the destination's components for most, fixed ones for dot
   products and matrices, and the coordinates a sampler's kind of texture needs for a lookup */
static
BYTE
D9SourcePositions(
    _In_ const D9ShaderWalk* Walk,
    _In_ DWORD Instruction,
    _In_ BYTE Written,
    _In_ UINT Source,
    _In_opt_ const DWORD* Sampler)
{
    switch (Instruction & 0xFFFF)
    {
    case 8:
    case 22:
    case 23:
    case 24:
    case 33:
    case 36:
        return 0x7;
    case 9:
    case 20:
    case 21:
        return 0xF;
    case 90:
        return Source < 2 ? 0x3 : 0x1;
    case 66:
        if (Walk->Version >= D9V(2, 0) && Source == 0 && Sampler != nullptr)
        {
            UINT Number = D9RegisterNumber(*Sampler);
            BYTE Kind = Number < D9TrackedRegisters ? Walk->SamplerTypes[Number] : 0;
            BYTE Coordinates = Kind == (D3DSTT_2D >> D3DSP_TEXTURETYPE_SHIFT) ? 0x3 : 0x7;

            if (((Instruction >> 16) & 0xFF) != 0)
                Coordinates |= 0x8;
            return Coordinates;
        }
        return Written != 0 ? Written : 0xF;
    default:
        return Written != 0 ? Written : 0xF;
    }
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

static
const D9OpcodeRule*
D9FindRule(
    _In_ USHORT Opcode)
{
    for (const D9OpcodeRule& Rule : D9OpcodeRules)
    {
        if (Rule.Opcode == Opcode)
            return &Rule;
    }
    return nullptr;
}

/* The version tokens each kind of machine accepts, up to what the device reports */
static
BOOLEAN
D9VersionAccepted(
    _In_ DWORD Token,
    _In_ BOOLEAN Pixel,
    _In_ DWORD Supported)
{
    UINT Version = Token & 0xFFFF;

    if ((Token & 0xFFFF0000) != (Pixel ? 0xFFFF0000 : 0xFFFE0000))
        return FALSE;
    if (Version > (Supported & 0xFFFF))
        return FALSE;
    if (Pixel)
        return (Version >= D9V(1, 1) && Version <= D9V(1, 4)) || Version == D9V(2, 0) || Version == D9Version2x ||
               Version == D9V(3, 0);
    return Version == D9V(1, 1) || Version == D9V(2, 0) || Version == D9Version2x || Version == D9V(3, 0);
}

/* How many parameter tokens an opcode takes in a version 1 machine, or zero for none it has */
static
UINT
D9FirstVersionParameters(
    _In_ const D9ShaderWalk* Walk,
    _In_ USHORT Opcode,
    _In_ const D9OpcodeRule* Rule)
{
    if (Opcode == D9OpTex || Opcode == D9OpTexcoord)
        return Walk->Version == D9V(1, 4) ? 2 : 1;
    return Rule->Destination + Rule->Sources;
}

/* Walks the tokens and checks each instruction; fills in the sizes and the registers DEF instructions fix */
BOOLEAN
D9CheckShaderTokens(
    _In_ const DWORD* Tokens,
    _In_ BOOLEAN Pixel,
    _In_ DWORD Supported,
    _Out_ D9ShaderLayout* Layout)
{
    D9ShaderWalk Walk = {};
    const DWORD* Token = Tokens + 1;
    UINT Comments = 0;

    ZeroMemory(Layout, sizeof(*Layout));
    if (!D9VersionAccepted(Tokens[0], Pixel, Supported))
        return FALSE;
    Walk.Pixel = Pixel;
    Walk.Version = Tokens[0] & 0xFFFF;

    for (;;)
    {
        DWORD Instruction = *Token;
        USHORT Opcode = static_cast<USHORT>(Instruction & 0xFFFF);
        const D9OpcodeRule* Rule;
        UINT Parameters;
        UINT First;
        UINT Last;

        if (Opcode == D9OpEnd)
            break;
        if (Opcode == D9OpComment)
        {
            UINT Length = (Instruction >> 16) & 0x7FFF;

            Comments += Length + 1;
            Token += Length + 1;
            continue;
        }
        if (Opcode == D9OpPhase)
        {
            if (!Pixel || Walk.Version != D9V(1, 4))
                return FALSE;
            Token += 1;
            continue;
        }

        /* Declarations and definitions have shapes of their own */
        if (Opcode == D9OpDcl || Opcode == D9OpDef || Opcode == D9OpDefi || Opcode == D9OpDefb)
        {
            UINT Expected = Opcode == D9OpDcl ? 2 : Opcode == D9OpDefb ? 2 : 5;
            DWORD Target = Token[Opcode == D9OpDcl ? 2 : 1];

            if (Opcode == D9OpDcl && Pixel && Walk.Version < D9V(2, 0))
                return FALSE;
            if ((Opcode == D9OpDefi || Opcode == D9OpDefb) && Walk.Version < D9V(2, 0))
                return FALSE;
            if (Walk.Version >= D9V(2, 0) && ((Instruction >> 24) & 0xF) != Expected)
                return FALSE;
            if (Opcode == D9OpDcl)
            {
                UINT Type = D9RegisterType(Target);
                UINT Number = D9RegisterNumber(Target);

                D9AddRegisterMask(Walk.Declared, Type, Number, static_cast<BYTE>((Target >> 16) & 0xF));
                if (Type == D9RegSampler && Number < D9TrackedRegisters)
                    Walk.SamplerTypes[Number] = static_cast<BYTE>((Token[1] >> D3DSP_TEXTURETYPE_SHIFT) & 0xF);
            }
            else if (Opcode == D9OpDef)
                D9AddDef(Layout->FloatDefs, &Layout->FloatDefCount, D9RegisterNumber(Target));
            else if (Opcode == D9OpDefi)
                D9AddDef(Layout->IntDefs, &Layout->IntDefCount, D9RegisterNumber(Target));
            else
                D9AddDef(Layout->BoolDefs, &Layout->BoolDefCount, D9RegisterNumber(Target));
            Token += Expected + 1;
            continue;
        }

        Rule = D9FindRule(Opcode);
        if (Rule == nullptr)
            return FALSE;
        First = Pixel ? Rule->PixelFirst : Rule->VertexFirst;
        Last = Pixel ? Rule->PixelLast : Rule->VertexLast;
        if (First == 0 || Walk.Version < First || Walk.Version > Last)
            return FALSE;

        /* Count the parameters each version's encoding gives the instruction */
        if (Walk.Version >= D9V(2, 0))
        {
            UINT Sources = Rule->Sources;
            UINT Count = 0;
            UINT Index = 1;

            if (Opcode == D9OpSincos && Walk.Version < D9V(3, 0))
                Sources = 3;
            if (Opcode == D9OpTex)
                Sources = 2;
            Parameters = (Instruction >> 24) & 0xF;
            while (Index <= Parameters)
            {
                DWORD Param = Token[Index];

                Count += 1;
                Index += D9HasAddressToken(&Walk, Param) ? 2 : 1;
            }
            if (Count != Rule->Destination + Sources)
                return FALSE;
        }
        else
        {
            Parameters = 0;
            while ((Token[Parameters + 1] & 0x80000000) != 0)
                Parameters += 1;
            if (Parameters != D9FirstVersionParameters(&Walk, Opcode, Rule))
                return FALSE;
        }

        /* Sources must be ready before the destination counts as written */
        {
            UINT Index = 1;
            UINT Source = 0;
            BOOLEAN HasDestination = Rule->Destination != 0 || Opcode == D9OpTex || Opcode == D9OpTexcoord;
            DWORD Destination = HasDestination ? Token[1] : 0;
            BYTE Written = static_cast<BYTE>((Destination >> 16) & 0xF);
            const DWORD* Sampler = nullptr;

            if (HasDestination)
                Index += D9HasAddressToken(&Walk, Token[1]) ? 2 : 1;
            if (Opcode == D9OpTex && Walk.Version >= D9V(2, 0) && Parameters >= 3)
                Sampler = &Token[Parameters];
            while (Index <= Parameters)
            {
                DWORD Param = Token[Index];

                if (!D9SourceReady(&Walk, Param, D9SourcePositions(&Walk, Instruction, Written, Source, Sampler)))
                    return FALSE;
                Index += D9HasAddressToken(&Walk, Param) ? 2 : 1;
                Source += 1;
            }
            if (HasDestination)
                D9NoteWrite(&Walk, Destination);
        }
        Layout->Instructions += 1;
        Token += Parameters + 1;
    }

    /* What each machine must leave written */
    if (Pixel)
    {
        if (Walk.Version < D9V(2, 0) && D9RegisterMask(Walk.Written, D9RegTemp, 0) == 0)
            return FALSE;
        if (Walk.Version >= D9V(2, 0) && D9RegisterMask(Walk.Written, D9RegColorOut, 0) == 0 &&
            D9RegisterMask(Walk.Written, D9RegDepthOut, 0) == 0)
            return FALSE;
    }
    else if (!Walk.PositionWritten)
    {
        return FALSE;
    }

    Layout->Size = static_cast<UINT>(Token - Tokens + 1) * sizeof(DWORD);
    Layout->StrippedSize = Layout->Size - Comments * sizeof(DWORD);
    return TRUE;
}
