/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Pixel shaders standing in for the fixed function texture stages
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ffpixel.hpp"

#include <new>
#include <string.h>

/* Register files as the tokens number them */
static constexpr BYTE D9RegTemp = 0;
static constexpr BYTE D9RegInput = 1;
static constexpr BYTE D9RegConst = 2;
static constexpr BYTE D9RegTexcoord = 3;
static constexpr BYTE D9RegColorOut = 8;
static constexpr BYTE D9RegSampler = 10;

/* Swizzles that replicate one component, and the identity */
static constexpr BYTE D9SwizzleX = 0x00;
static constexpr BYTE D9SwizzleY = 0x55;
static constexpr BYTE D9SwizzleZ = 0xAA;
static constexpr BYTE D9SwizzleW = 0xFF;
static constexpr BYTE D9SwizzleAll = 0xE4;

static constexpr BYTE D9MaskX = 0x1;
static constexpr BYTE D9MaskXY = 0x3;
static constexpr BYTE D9MaskXYZ = 0x7;
static constexpr BYTE D9MaskW = 0x8;
static constexpr BYTE D9MaskAll = 0xF;

/* Opcodes the converter writes */
static constexpr DWORD D9OpMov = 0x01;
static constexpr DWORD D9OpAdd = 0x02;
static constexpr DWORD D9OpMad = 0x04;
static constexpr DWORD D9OpMul = 0x05;
static constexpr DWORD D9OpRcp = 0x06;
static constexpr DWORD D9OpDp3 = 0x08;
static constexpr DWORD D9OpLrp = 0x12;
static constexpr DWORD D9OpM3x2 = 0x18;
static constexpr DWORD D9OpDcl = 0x1F;
static constexpr DWORD D9OpTexld = 0x42;
static constexpr DWORD D9OpTexldp = 0x10042;

/* The temporaries each part of a stage works in */
static constexpr BYTE D9TempPremodulate = 0;
static constexpr BYTE D9TempSample = 1;
static constexpr BYTE D9TempSaved = 2;
static constexpr BYTE D9TempCurrent = 3;
static constexpr BYTE D9TempBump = 4;
static constexpr BYTE D9TempResult = 5;
static constexpr BYTE D9TempArg0 = 6;
static constexpr BYTE D9TempArg1 = 7;
static constexpr BYTE D9TempArg2 = 8;
static constexpr BYTE D9TempLuminance = 9;
static constexpr BYTE D9TempLateSample = 10;
static constexpr BYTE D9TempOffset = 11;

/* What the scan marks an alpha op that stands for no op at all */
static constexpr DWORD D9NoAlphaOp = 0x7FFFFFFF;

typedef struct _D9_PS_SOURCE
{
    BYTE File;
    BYTE Number;
    BYTE Swizzle;
    BOOLEAN Negate;
} D9_PS_SOURCE;

typedef struct _D9_PS_TARGET
{
    BYTE File;
    BYTE Number;
    BYTE Mask;
    BOOLEAN Saturate;
} D9_PS_TARGET;

static
D9_PS_SOURCE
D9Src(
    _In_ BYTE File,
    _In_ BYTE Number,
    _In_ BYTE Swizzle)
{
    D9_PS_SOURCE Source = { File, Number, Swizzle, FALSE };

    return Source;
}

static
D9_PS_TARGET
D9Dst(
    _In_ BYTE File,
    _In_ BYTE Number,
    _In_ BYTE Mask,
    _In_ BOOLEAN Saturate)
{
    D9_PS_TARGET Target = { File, Number, Mask, Saturate };

    return Target;
}

static
D9_PS_SOURCE
D9Swizzled(
    _In_ D9_PS_SOURCE Source,
    _In_ BYTE Swizzle)
{
    Source.Swizzle = Swizzle;
    return Source;
}

static
D9_PS_SOURCE
D9Negated(
    _In_ D9_PS_SOURCE Source)
{
    Source.Negate = TRUE;
    return Source;
}

static
D9_PS_SOURCE
D9Temp(
    _In_ BYTE Number)
{
    return D9Src(D9RegTemp, Number, D9SwizzleAll);
}

static
D9_PS_TARGET
D9TempDst(
    _In_ BYTE Number,
    _In_ BYTE Mask)
{
    return D9Dst(D9RegTemp, Number, Mask, FALSE);
}

/* Two operands name the same register whatever their swizzles and modifiers */
static
BOOLEAN
D9SameRegister(
    _In_ const D9_PS_SOURCE& Left,
    _In_ const D9_PS_SOURCE& Right)
{
    return Left.File == Right.File && Left.Number == Right.Number;
}

static
DWORD
D9RegisterBits(
    _In_ BYTE File,
    _In_ BYTE Number)
{
    return 0x80000000 | (static_cast<DWORD>(File & 7) << 28) | (static_cast<DWORD>(File & 0x18) << 8) | Number;
}

/* Fields of a stage as the key packs them */
static
DWORD
D9ColorOp(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return Stage.Blend & 0x1F;
}

static
DWORD
D9ColorArg0(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Blend >> 10) & 0x3F;
}

static
DWORD
D9ColorArg1(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Blend >> 16) & 0x3F;
}

static
DWORD
D9ColorArg2(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Blend >> 22) & 0x3F;
}

static
DWORD
D9AlphaArg0(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return Stage.Inputs & 0x3F;
}

static
DWORD
D9AlphaArg1(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs >> 6) & 0x3F;
}

static
DWORD
D9AlphaArg2(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs >> 12) & 0x3F;
}

static
DWORD
D9ResultArg(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs >> 18) & 0x3F;
}

static
UINT
D9CoordIndex(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs >> 24) & 7;
}

static
DWORD
D9TexKind(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs >> 27) & 3;
}

static
BOOLEAN
D9Packed(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs & 0x20000000) != 0;
}

static
BOOLEAN
D9Projected(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    return (Stage.Inputs & 0x40000000) != 0;
}

static
BOOLEAN
D9SetIsWide(
    _In_ const D9_FIXED_PIXEL_STAGE& Entry)
{
    return (Entry.Inputs & 0x80000000) != 0;
}


static
DWORD
D9AlphaOp(
    _In_ const D9_FIXED_PIXEL_STAGE& Stage)
{
    DWORD Op = (Stage.Blend >> 5) & 0x1F;

    return Op == D9FixedAlphaUnset ? D9NoAlphaOp : Op;
}

static
BOOLEAN
D9IsBumpOp(
    _In_ DWORD Op)
{
    return Op == D3DTOP_BUMPENVMAP || Op == D3DTOP_BUMPENVMAPLUMINANCE;
}

/* Writes tokens and the instructions made of them */
class D9PsWriter
{
public:
    D9PsWriter(
        _Out_writes_(D9FixedPixelTokenLimit) DWORD* Buffer)
        : Tokens(Buffer)
    {
    }

    VOID
    Put(
        _In_ DWORD Token)
    {
        if (Count >= D9FixedPixelTokenLimit)
        {
            Full = TRUE;
            return;
        }
        Tokens[Count] = Token;
        Count += 1;
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ BOOLEAN Partial,
        _In_ const D9_PS_TARGET& Target,
        _In_reads_(SourceCount) const D9_PS_SOURCE* Sources,
        _In_ UINT SourceCount)
    {
        DWORD Bits = D9RegisterBits(Target.File, Target.Number) | (static_cast<DWORD>(Target.Mask) << 16);

        if (Target.Saturate)
            Bits |= 0x00100000;
        if (Partial)
            Bits |= 0x00200000;
        Put(Opcode | ((SourceCount + 1) << 24));
        Put(Bits);
        for (UINT Index = 0; Index < SourceCount; Index++)
        {
            const D9_PS_SOURCE& Source = Sources[Index];

            Put(D9RegisterBits(Source.File, Source.Number) | (static_cast<DWORD>(Source.Swizzle) << 16) |
                (Source.Negate ? 0x01000000 : 0));
        }
    }

    /* Partial precision instructions with one to three sources */
    VOID
    Pp(
        _In_ DWORD Opcode,
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& A)
    {
        Op(Opcode, TRUE, Target, &A, 1);
    }

    VOID
    Pp(
        _In_ DWORD Opcode,
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& A,
        _In_ const D9_PS_SOURCE& B)
    {
        const D9_PS_SOURCE Sources[2] = { A, B };

        Op(Opcode, TRUE, Target, Sources, 2);
    }

    VOID
    Pp(
        _In_ DWORD Opcode,
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& A,
        _In_ const D9_PS_SOURCE& B,
        _In_ const D9_PS_SOURCE& C)
    {
        const D9_PS_SOURCE Sources[3] = { A, B, C };

        Op(Opcode, TRUE, Target, Sources, 3);
    }

    /* Full precision instructions */
    VOID
    FullPrecision(
        _In_ DWORD Opcode,
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& A)
    {
        Op(Opcode, FALSE, Target, &A, 1);
    }

    VOID
    FullPrecision(
        _In_ DWORD Opcode,
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& A,
        _In_ const D9_PS_SOURCE& B)
    {
        const D9_PS_SOURCE Sources[2] = { A, B };

        Op(Opcode, FALSE, Target, Sources, 2);
    }

    VOID
    DeclareInput(
        _In_ BYTE File,
        _In_ BYTE Number)
    {
        Put(D9OpDcl | (2u << 24));
        Put(0x80000000);
        Put(D9RegisterBits(File, Number) | 0x000F0000);
    }

    VOID
    DeclareSampler(
        _In_ DWORD Kind,
        _In_ BYTE Number)
    {
        Put(D9OpDcl | (2u << 24));
        Put(Kind | 0x80000000);
        Put(D9RegisterBits(D9RegSampler, Number) | 0x000F0000);
    }

    DWORD* Tokens;
    UINT Count = 0;
    BOOLEAN Full = FALSE;
};

/* The state one shader build works through */
class D9PsBuild
{
public:
    D9PsBuild(
        _In_ const D9_FIXED_PIXEL_KEY* SourceKey,
        _In_ BOOLEAN Textured,
        _Out_writes_(D9FixedPixelTokenLimit) DWORD* Buffer)
        : Key(SourceKey),
          StageZeroTextured(Textured),
          Out(Buffer)
    {
    }

    VOID
    Build(VOID);

    const D9_FIXED_PIXEL_KEY* Key;
    BOOLEAN StageZeroTextured;
    D9PsWriter Out;

private:
    VOID
    Scan(
        _In_ UINT Count,
        _Inout_ BOOLEAN* DiffuseUsed,
        _Inout_ BOOLEAN* SpecularUsed,
        _Out_writes_(D9FixedStages) BOOLEAN* Samples,
        _Out_writes_(D9FixedStages) BOOLEAN* ColorSamples);

    VOID
    Separate(
        _In_ BYTE Temp,
        _Inout_ D9_PS_SOURCE* Operand,
        _In_ const D9_PS_SOURCE& Other);

    VOID
    Interpolate(
        _In_ const D9_PS_TARGET& Target,
        _In_ const D9_PS_SOURCE& Weight,
        _In_ const D9_PS_SOURCE& First,
        _In_ const D9_PS_SOURCE& Second);

    VOID
    Argument(
        _In_ DWORD Arg,
        _In_ BOOLEAN Alpha,
        _In_ BYTE Temp,
        _Inout_ D9_PS_SOURCE* Operand);

    VOID
    Sample(
        _In_ UINT Stage,
        _In_ D9_PS_SOURCE Coord,
        _In_ const D9_PS_TARGET& Target);

    VOID
    BlendColor(
        _In_ DWORD Op);

    VOID
    BlendAlpha(
        _In_ DWORD Op);

    const D9_FIXED_PIXEL_STAGE&
    StageOf(
        _In_ UINT Stage) const
    {
        return Key->Stages[Stage];
    }

    D9_PS_SOURCE Diffuse = {};
    D9_PS_SOURCE Specular = {};
    D9_PS_SOURCE Current = {};
    D9_PS_SOURCE StageCurrent = {};
    D9_PS_SOURCE Texture = {};
    D9_PS_SOURCE Coords[D9FixedStages] = {};
    UINT StageIndex = 0;
    BOOLEAN TempWritten = FALSE;
    D9_PS_TARGET ColorTarget = {};
    D9_PS_TARGET AlphaTarget = {};
};

/* Which inputs the active stages read and which stages sample. ColorSamples keeps what the color args alone
   ask for, which decides where an untextured stage cuts the chain */
VOID
D9PsBuild::Scan(
    _In_ UINT Count,
    _Inout_ BOOLEAN* DiffuseUsed,
    _Inout_ BOOLEAN* SpecularUsed,
    _Out_writes_(D9FixedStages) BOOLEAN* Samples,
    _Out_writes_(D9FixedStages) BOOLEAN* ColorSamples)
{
    BOOLEAN CurrentIsDiffuse = TRUE;
    BOOLEAN FollowsBump = FALSE;

    ZeroMemory(Samples, D9FixedStages * sizeof(*Samples));
    ZeroMemory(ColorSamples, D9FixedStages * sizeof(*ColorSamples));
    for (UINT Stage = 0; Stage < Count; Stage++)
    {
        const D9_FIXED_PIXEL_STAGE& Info = StageOf(Stage);
        DWORD ColorOp = D9ColorOp(Info);
        DWORD AlphaOp;
        auto Reads = [&](DWORD Arg)
        {
            switch (Arg & D3DTA_SELECTMASK)
            {
            case D3DTA_DIFFUSE:
                *DiffuseUsed = TRUE;
                break;
            case D3DTA_CURRENT:
                if (CurrentIsDiffuse)
                    *DiffuseUsed = TRUE;
                break;
            case D3DTA_TEXTURE:
                Samples[Stage] = TRUE;
                break;
            case D3DTA_SPECULAR:
                *SpecularUsed = TRUE;
                break;
            default:
                break;
            }
        };

        if (FollowsBump)
        {
            FollowsBump = FALSE;
            Samples[Stage] = TRUE;
            if (D9IsBumpOp(ColorOp))
                continue;
        }
        else if (D9IsBumpOp(ColorOp))
        {
            FollowsBump = TRUE;
            Samples[Stage] = TRUE;
            continue;
        }

        AlphaOp = D9AlphaOp(Info);
        if (Stage > 0 &&
            (D9ColorOp(StageOf(Stage - 1)) == D3DTOP_PREMODULATE || D9AlphaOp(StageOf(Stage - 1)) == D3DTOP_PREMODULATE))
        {
            Samples[Stage] = TRUE;
        }

        switch (ColorOp)
        {
        case D3DTOP_SELECTARG1:
        case D3DTOP_PREMODULATE:
            Reads(D9ColorArg1(Info));
            ColorSamples[Stage] = Samples[Stage];
            break;
        case D3DTOP_SELECTARG2:
            Reads(D9ColorArg2(Info));
            ColorSamples[Stage] = Samples[Stage];
            break;
        case D3DTOP_BLENDDIFFUSEALPHA:
            Reads(D9ColorArg1(Info));
            Reads(D9ColorArg2(Info));
            *DiffuseUsed = TRUE;
            ColorSamples[Stage] = Samples[Stage];
            break;
        case D3DTOP_BLENDTEXTUREALPHA:
        case D3DTOP_BLENDTEXTUREALPHAPM:
        {
            BOOLEAN Before;

            Reads(D9ColorArg1(Info));
            Reads(D9ColorArg2(Info));
            Before = Samples[Stage];
            Samples[Stage] = TRUE;
            ColorSamples[Stage] = Before;
            break;
        }
        case D3DTOP_BLENDCURRENTALPHA:
            Reads(D9ColorArg1(Info));
            Reads(D9ColorArg2(Info));
            ColorSamples[Stage] = Samples[Stage];
            if (Stage == 0)
                *DiffuseUsed = TRUE;
            break;
        case D3DTOP_DOTPRODUCT3:
            Reads(D9ColorArg1(Info));
            Reads(D9ColorArg2(Info));
            ColorSamples[Stage] = Samples[Stage];
            continue;
        case D3DTOP_MULTIPLYADD:
        case D3DTOP_LERP:
            Reads(D9ColorArg0(Info));
            Reads(D9ColorArg1(Info));
            Reads(D9ColorArg2(Info));
            ColorSamples[Stage] = Samples[Stage];
            break;
        default:
            if ((ColorOp >= D3DTOP_MODULATE && ColorOp <= D3DTOP_ADDSMOOTH) || ColorOp == D3DTOP_BLENDFACTORALPHA ||
                (ColorOp >= D3DTOP_MODULATEALPHA_ADDCOLOR && ColorOp <= D3DTOP_MODULATEINVCOLOR_ADDALPHA))
            {
                Reads(D9ColorArg1(Info));
                Reads(D9ColorArg2(Info));
                ColorSamples[Stage] = Samples[Stage];
            }
            break;
        }

        if (AlphaOp == D9NoAlphaOp)
        {
            Reads(StageZeroTextured ? D9AlphaArg2(Info) : D9AlphaArg1(Info));
        }
        else
        {
            switch (AlphaOp)
            {
            case D3DTOP_SELECTARG1:
            case D3DTOP_PREMODULATE:
                Reads(D9AlphaArg1(Info));
                break;
            case D3DTOP_SELECTARG2:
                Reads(D9AlphaArg2(Info));
                break;
            case D3DTOP_BLENDDIFFUSEALPHA:
                Reads(D9AlphaArg1(Info));
                Reads(D9AlphaArg2(Info));
                *DiffuseUsed = TRUE;
                break;
            case D3DTOP_BLENDTEXTUREALPHA:
                Reads(D9AlphaArg1(Info));
                Reads(D9AlphaArg2(Info));
                Samples[Stage] = TRUE;
                break;
            case D3DTOP_BLENDCURRENTALPHA:
                Reads(D9AlphaArg1(Info));
                Reads(D9AlphaArg2(Info));
                if (Stage == 0)
                    *DiffuseUsed = TRUE;
                break;
            case D3DTOP_MULTIPLYADD:
            case D3DTOP_LERP:
                Reads(D9AlphaArg0(Info));
                Reads(D9AlphaArg1(Info));
                Reads(D9AlphaArg2(Info));
                break;
            default:
                if ((AlphaOp >= D3DTOP_MODULATE && AlphaOp <= D3DTOP_ADDSMOOTH) || AlphaOp == D3DTOP_BLENDFACTORALPHA ||
                    AlphaOp == D3DTOP_BLENDTEXTUREALPHAPM)
                {
                    Reads(D9AlphaArg1(Info));
                    Reads(D9AlphaArg2(Info));
                }
                break;
            }
        }
        if (D9ResultArg(Info) != D3DTA_TEMP)
            CurrentIsDiffuse = FALSE;
    }
}

/* Two constants or two inputs cannot be read by one instruction; the first goes through a temporary */
VOID
D9PsBuild::Separate(
    _In_ BYTE Temp,
    _Inout_ D9_PS_SOURCE* Operand,
    _In_ const D9_PS_SOURCE& Other)
{
    BOOLEAN BothConst = Operand->File == D9RegConst && Other.File == D9RegConst;
    BOOLEAN BothInput = Operand->File == D9RegInput && Other.File == D9RegInput;

    if ((BothConst || BothInput) && Operand->Number != Other.Number)
    {
        Out.Pp(D9OpMov, D9TempDst(Temp, D9MaskAll), *Operand);
        *Operand = D9Temp(Temp);
    }
}

/* A blend whose three operands are one register is a copy */
VOID
D9PsBuild::Interpolate(
    _In_ const D9_PS_TARGET& Target,
    _In_ const D9_PS_SOURCE& Weight,
    _In_ const D9_PS_SOURCE& First,
    _In_ const D9_PS_SOURCE& Second)
{
    if (D9SameRegister(Weight, First) && D9SameRegister(First, Second))
        Out.Pp(D9OpMov, Target, Weight);
    else
        Out.Pp(D9OpLrp, Target, Weight, First, Second);
}

/* The operand an arg names, with its complement or alpha replicate worked out in a temporary */
VOID
D9PsBuild::Argument(
    _In_ DWORD Arg,
    _In_ BOOLEAN Alpha,
    _In_ BYTE Temp,
    _Inout_ D9_PS_SOURCE* Operand)
{
    const D9_PS_SOURCE One = D9Src(D9RegConst, 1, D9SwizzleW);
    BOOLEAN Replicate = !Alpha && (Arg & D3DTA_ALPHAREPLICATE) != 0;

    switch (Arg & D3DTA_SELECTMASK)
    {
    case D3DTA_DIFFUSE:
        *Operand = Diffuse;
        break;
    case D3DTA_CURRENT:
        *Operand = StageCurrent;
        break;
    case D3DTA_TEXTURE:
        *Operand = Texture;
        break;
    case D3DTA_TFACTOR:
        *Operand = D9Src(D9RegConst, D9FixedConstFactor, D9SwizzleAll);
        break;
    case D3DTA_SPECULAR:
        *Operand = Specular;
        break;
    case D3DTA_TEMP:
        *Operand = TempWritten ? D9Temp(D9TempSaved) : D9Src(D9RegConst, 1, D9SwizzleX);
        break;
    case D3DTA_CONSTANT:
        *Operand = D9Src(D9RegConst, static_cast<BYTE>(D9FixedConstStage + StageIndex), D9SwizzleAll);
        break;
    default:
        break;
    }

    if ((Arg & D3DTA_COMPLEMENT) != 0)
    {
        Separate(Temp, Operand, One);
        if (Alpha)
        {
            Out.Pp(D9OpMov, D9TempDst(Temp, D9MaskXYZ), *Operand);
            Out.Pp(D9OpAdd, D9TempDst(Temp, D9MaskW), One, D9Negated(*Operand));
        }
        else if (Replicate)
        {
            D9_PS_SOURCE Alone = D9Swizzled(*Operand, D9SwizzleW);

            Alone.Negate = TRUE;
            Out.Pp(D9OpAdd, D9TempDst(Temp, D9MaskAll), One, Alone);
        }
        else
        {
            Out.Pp(D9OpAdd, D9TempDst(Temp, D9MaskAll), One, D9Negated(*Operand));
        }
        *Operand = D9Temp(Temp);
    }
    else if (Replicate)
    {
        Out.Pp(D9OpMov, D9TempDst(Temp, D9MaskAll), D9Swizzled(*Operand, D9SwizzleW));
        *Operand = D9Temp(Temp);
    }
}

/* Reads the stage's texture; a projected fetch of three floats divides by the third */
VOID
D9PsBuild::Sample(
    _In_ UINT Stage,
    _In_ D9_PS_SOURCE Coord,
    _In_ const D9_PS_TARGET& Target)
{
    const D9_FIXED_PIXEL_STAGE& Info = StageOf(Stage);
    D9_PS_SOURCE Sampler = D9Src(D9RegSampler, static_cast<BYTE>(Stage), D9SwizzleAll);
    DWORD Opcode = D9OpTexld;

    if (D9Projected(Info))
    {
        if (D9SetIsWide(StageOf(D9CoordIndex(Info))))
        {
            if (Coord.File != D9RegTemp)
            {
                Out.FullPrecision(D9OpMov, D9TempDst(D9TempSample, D9MaskAll), Coord);
                Coord = D9Temp(D9TempSample);
            }
            Out.FullPrecision(D9OpMov, D9Dst(Coord.File, Coord.Number, D9MaskW, FALSE), D9Swizzled(Coord, D9SwizzleZ));
        }
        Opcode = D9OpTexldp;
    }
    else if (Coord.File == D9RegConst)
    {
        Out.FullPrecision(D9OpMov, D9TempDst(D9TempSample, D9MaskAll), Coord);
        Coord = D9Temp(D9TempSample);
    }
    Out.Pp(Opcode, Target, Coord, Sampler);
    if (D9Packed(Info))
    {
        Out.Pp(D9OpMul, D9Dst(Target.File, Target.Number, D9MaskXYZ, FALSE), D9Src(Target.File, Target.Number, D9SwizzleAll),
               D9Src(D9RegConst, 0, D9SwizzleW));
    }
}

VOID
D9PsBuild::BlendColor(
    _In_ DWORD Op)
{
    const D9_FIXED_PIXEL_STAGE& Info = StageOf(StageIndex);
    const D9_PS_SOURCE Half = D9Src(D9RegConst, 0, D9SwizzleX);
    const D9_PS_SOURCE Two = D9Src(D9RegConst, 0, D9SwizzleY);
    const D9_PS_SOURCE Four = D9Src(D9RegConst, 0, D9SwizzleZ);
    const D9_PS_SOURCE One = D9Src(D9RegConst, 1, D9SwizzleW);
    const D9_PS_SOURCE Factor = D9Src(D9RegConst, D9FixedConstFactor, D9SwizzleAll);
    const D9_PS_TARGET Work = D9TempDst(D9TempResult, D9MaskXYZ);
    const D9_PS_SOURCE Result = D9Temp(D9TempResult);
    D9_PS_SOURCE A0 = {};
    D9_PS_SOURCE A1 = {};
    D9_PS_SOURCE A2 = {};

    switch (Op)
    {
    case D3DTOP_SELECTARG1:
    case D3DTOP_PREMODULATE:
        Argument(D9ColorArg1(Info), FALSE, D9TempArg1, &A1);
        Out.Pp(D9OpMov, ColorTarget, A1);
        return;
    case D3DTOP_SELECTARG2:
        Argument(D9ColorArg2(Info), FALSE, D9TempArg2, &A2);
        Out.Pp(D9OpMov, ColorTarget, A2);
        return;
    case D3DTOP_MULTIPLYADD:
    case D3DTOP_LERP:
        Argument(D9ColorArg0(Info), FALSE, D9TempArg0, &A0);
        Argument(D9ColorArg1(Info), FALSE, D9TempArg1, &A1);
        Argument(D9ColorArg2(Info), FALSE, D9TempArg2, &A2);
        Separate(D9TempArg0, &A0, A1);
        Separate(D9TempArg0, &A0, A2);
        Separate(D9TempArg1, &A1, A2);
        if (Op == D3DTOP_MULTIPLYADD)
            Out.Pp(D9OpMad, ColorTarget, A1, A2, A0);
        else
            Interpolate(ColorTarget, A0, A1, A2);
        return;
    default:
        break;
    }

    if (Op < D3DTOP_MODULATE || Op > D3DTOP_DOTPRODUCT3 || Op == D3DTOP_PREMODULATE)
        return;
    Argument(D9ColorArg1(Info), FALSE, D9TempArg1, &A1);
    Argument(D9ColorArg2(Info), FALSE, D9TempArg2, &A2);

    switch (Op)
    {
    case D3DTOP_BLENDDIFFUSEALPHA:
    case D3DTOP_BLENDFACTORALPHA:
    case D3DTOP_BLENDCURRENTALPHA:
    {
        D9_PS_SOURCE Weight = Op == D3DTOP_BLENDDIFFUSEALPHA ? Diffuse
                                                             : Op == D3DTOP_BLENDFACTORALPHA ? Factor : StageCurrent;

        Separate(D9TempArg2, &A2, A1);
        Separate(D9TempArg2, &A2, Weight);
        Out.Pp(D9OpAdd, Work, A1, D9Negated(A2));
        Out.Pp(D9OpMad, ColorTarget, Result, D9Swizzled(Weight, D9SwizzleW), A2);
        return;
    }
    case D3DTOP_ADDSMOOTH:
        Out.Pp(D9OpAdd, Work, One, D9Negated(A1));
        Out.Pp(D9OpMad, ColorTarget, Result, A2, A1);
        return;
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR:
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA:
        Separate(D9TempArg1, &A1, One);
        Separate(D9TempArg1, &A1, A2);
        if (Op == D3DTOP_MODULATEINVALPHA_ADDCOLOR)
        {
            Out.Pp(D9OpAdd, D9TempDst(D9TempResult, D9MaskW), One, D9Negated(D9Swizzled(A1, D9SwizzleW)));
            Out.Pp(D9OpMad, ColorTarget, D9Swizzled(Result, D9SwizzleW), A2, A1);
        }
        else
        {
            Out.Pp(D9OpAdd, Work, One, D9Negated(A1));
            Out.Pp(D9OpMad, ColorTarget, Result, A2, D9Swizzled(A1, D9SwizzleW));
        }
        return;
    case D3DTOP_DOTPRODUCT3:
        Separate(D9TempArg1, &A1, Half);
        Separate(D9TempArg2, &A2, Half);
        Out.Pp(D9OpAdd, D9TempDst(D9TempArg1, D9MaskXYZ), A1, D9Negated(Half));
        Out.Pp(D9OpMul, D9TempDst(D9TempArg1, D9MaskXYZ), D9Temp(D9TempArg1), Four);
        Out.Pp(D9OpAdd, D9TempDst(D9TempArg2, D9MaskXYZ), A2, D9Negated(Half));
        Out.Pp(D9OpDp3, D9Dst(D9RegTemp, D9TempResult, D9MaskAll, TRUE), D9Temp(D9TempArg1), D9Temp(D9TempArg2));
        return;
    default:
        break;
    }

    Separate(D9TempArg1, &A1, A2);
    switch (Op)
    {
    case D3DTOP_MODULATE:
        Out.Pp(D9OpMul, ColorTarget, A1, A2);
        break;
    case D3DTOP_MODULATE2X:
        Out.Pp(D9OpMul, Work, A1, A2);
        Out.Pp(D9OpAdd, ColorTarget, Result, Result);
        break;
    case D3DTOP_MODULATE4X:
        Out.Pp(D9OpMul, Work, A1, A2);
        Out.Pp(D9OpMul, ColorTarget, Result, Four);
        break;
    case D3DTOP_ADD:
        Out.Pp(D9OpAdd, ColorTarget, A1, A2);
        break;
    case D3DTOP_ADDSIGNED:
        Out.Pp(D9OpAdd, Work, A1, A2);
        Out.Pp(D9OpAdd, ColorTarget, Result, D9Negated(Half));
        break;
    case D3DTOP_ADDSIGNED2X:
        Out.Pp(D9OpAdd, Work, A1, A2);
        Out.Pp(D9OpAdd, Work, Result, D9Negated(Half));
        Out.Pp(D9OpMul, ColorTarget, Result, Two);
        break;
    case D3DTOP_SUBTRACT:
        Out.Pp(D9OpAdd, ColorTarget, A1, D9Negated(A2));
        break;
    case D3DTOP_BLENDTEXTUREALPHA:
        Out.Pp(D9OpAdd, Work, A1, D9Negated(A2));
        Out.Pp(D9OpMad, ColorTarget, Result, D9Swizzled(Texture, D9SwizzleW), A2);
        break;
    case D3DTOP_BLENDTEXTUREALPHAPM:
        Out.Pp(D9OpAdd, D9TempDst(D9TempResult, D9MaskW), One, D9Negated(D9Swizzled(Texture, D9SwizzleW)));
        Out.Pp(D9OpMad, ColorTarget, D9Swizzled(Result, D9SwizzleW), A2, A1);
        break;
    case D3DTOP_MODULATEALPHA_ADDCOLOR:
        Out.Pp(D9OpMad, ColorTarget, D9Swizzled(A1, D9SwizzleW), A2, A1);
        break;
    case D3DTOP_MODULATECOLOR_ADDALPHA:
        Out.Pp(D9OpMad, ColorTarget, A2, A1, D9Swizzled(A1, D9SwizzleW));
        break;
    default:
        break;
    }
}

VOID
D9PsBuild::BlendAlpha(
    _In_ DWORD Op)
{
    const D9_FIXED_PIXEL_STAGE& Info = StageOf(StageIndex);
    const D9_PS_SOURCE Half = D9Src(D9RegConst, 0, D9SwizzleX);
    const D9_PS_SOURCE Two = D9Src(D9RegConst, 0, D9SwizzleY);
    const D9_PS_SOURCE Four = D9Src(D9RegConst, 0, D9SwizzleZ);
    const D9_PS_SOURCE One = D9Src(D9RegConst, 1, D9SwizzleW);
    const D9_PS_SOURCE Factor = D9Src(D9RegConst, D9FixedConstFactor, D9SwizzleAll);
    const D9_PS_TARGET Work = D9TempDst(D9TempResult, D9MaskW);
    const D9_PS_SOURCE Result = D9Swizzled(D9Temp(D9TempResult), D9SwizzleW);
    D9_PS_SOURCE B0 = {};
    D9_PS_SOURCE B1 = {};
    D9_PS_SOURCE B2 = {};

    if (Op == D9NoAlphaOp)
    {
        if (StageZeroTextured)
        {
            Argument(D9AlphaArg2(Info), TRUE, D9TempArg2, &B2);
            Out.Pp(D9OpMov, AlphaTarget, B2);
        }
        else
        {
            Argument(D9AlphaArg1(Info), TRUE, D9TempArg1, &B1);
            Out.Pp(D9OpMov, AlphaTarget, B1);
        }
        return;
    }

    switch (Op)
    {
    case D3DTOP_SELECTARG1:
    case D3DTOP_PREMODULATE:
        Argument(D9AlphaArg1(Info), TRUE, D9TempArg1, &B1);
        Out.Pp(D9OpMov, AlphaTarget, B1);
        return;
    case D3DTOP_SELECTARG2:
        Argument(D9AlphaArg2(Info), TRUE, D9TempArg2, &B2);
        Out.Pp(D9OpMov, AlphaTarget, B2);
        return;
    case D3DTOP_MULTIPLYADD:
    case D3DTOP_LERP:
        Argument(D9AlphaArg0(Info), TRUE, D9TempArg0, &B0);
        Argument(D9AlphaArg1(Info), TRUE, D9TempArg1, &B1);
        Argument(D9AlphaArg2(Info), TRUE, D9TempArg2, &B2);
        Separate(D9TempArg0, &B0, B1);
        Separate(D9TempArg0, &B0, B2);
        Separate(D9TempArg1, &B1, B2);
        if (Op == D3DTOP_MULTIPLYADD)
            Out.Pp(D9OpMad, AlphaTarget, B1, B2, B0);
        else
            Interpolate(AlphaTarget, B0, B1, B2);
        return;
    default:
        break;
    }

    /* Ops with no alpha meaning keep the current alpha */
    if (Op < D3DTOP_MODULATE || Op > D3DTOP_BLENDCURRENTALPHA || Op == D3DTOP_PREMODULATE)
    {
        Out.Pp(D9OpMov, AlphaTarget, D9Swizzled(StageCurrent, D9SwizzleW));
        return;
    }
    Argument(D9AlphaArg1(Info), TRUE, D9TempArg1, &B1);
    Argument(D9AlphaArg2(Info), TRUE, D9TempArg2, &B2);

    switch (Op)
    {
    case D3DTOP_ADDSMOOTH:
        Separate(D9TempArg1, &B1, One);
        Separate(D9TempArg2, &B2, One);
        Interpolate(AlphaTarget, B1, One, B2);
        return;
    case D3DTOP_BLENDDIFFUSEALPHA:
    case D3DTOP_BLENDFACTORALPHA:
    case D3DTOP_BLENDCURRENTALPHA:
    {
        D9_PS_SOURCE Weight = Op == D3DTOP_BLENDDIFFUSEALPHA ? Diffuse
                                                             : Op == D3DTOP_BLENDFACTORALPHA ? Factor : StageCurrent;

        Separate(D9TempArg2, &B2, B1);
        Separate(D9TempArg2, &B2, Weight);
        Out.Pp(D9OpAdd, Work, B1, D9Negated(B2));
        Out.Pp(D9OpMad, AlphaTarget, Result, Weight, B2);
        return;
    }
    default:
        break;
    }

    Separate(D9TempArg1, &B1, B2);
    switch (Op)
    {
    case D3DTOP_MODULATE:
        Out.Pp(D9OpMul, AlphaTarget, B1, B2);
        break;
    case D3DTOP_MODULATE2X:
        Out.Pp(D9OpMul, Work, B1, B2);
        Out.Pp(D9OpAdd, AlphaTarget, Result, Result);
        break;
    case D3DTOP_MODULATE4X:
        Out.Pp(D9OpMul, Work, B1, B2);
        Out.Pp(D9OpMul, AlphaTarget, Result, Four);
        break;
    case D3DTOP_ADD:
        Out.Pp(D9OpAdd, AlphaTarget, B1, B2);
        break;
    case D3DTOP_ADDSIGNED:
        Out.Pp(D9OpAdd, Work, B1, B2);
        Out.Pp(D9OpAdd, AlphaTarget, Result, D9Negated(Half));
        break;
    case D3DTOP_ADDSIGNED2X:
        Out.Pp(D9OpAdd, Work, B1, B2);
        Out.Pp(D9OpAdd, Work, Result, D9Negated(Half));
        Out.Pp(D9OpMul, AlphaTarget, Result, Two);
        break;
    case D3DTOP_SUBTRACT:
        Out.Pp(D9OpAdd, AlphaTarget, B1, D9Negated(B2));
        break;
    case D3DTOP_BLENDTEXTUREALPHA:
        Out.Pp(D9OpAdd, Work, B1, D9Negated(B2));
        Out.Pp(D9OpMad, AlphaTarget, Result, Texture, B2);
        break;
    case D3DTOP_BLENDTEXTUREALPHAPM:
        Out.Pp(D9OpAdd, Work, One, D9Negated(Texture));
        Out.Pp(D9OpMad, AlphaTarget, Result, B2, B1);
        break;
    default:
        break;
    }
}

VOID
D9PsBuild::Build(VOID)
{
    static const DWORD SamplerKinds[4] = { 0x10000000, 0x10000000, 0x18000000, 0x20000000 };
    const DWORD Flags = Key->Flags;
    const UINT SetCount = (Flags >> 16) & 0xF;
    const BOOLEAN AddSpecular = (Flags & (D9FixedSpecularAdd | D9FixedHasSpecular)) ==
                                (D9FixedSpecularAdd | D9FixedHasSpecular);
    const BOOLEAN Sprites = (Flags & D9FixedPointSprite) != 0;
    const D9_PS_SOURCE ZeroOne = D9Src(D9RegConst, 1, D9SwizzleAll);
    BOOLEAN Samples[D9FixedStages];
    BOOLEAN ColorSamples[D9FixedStages];
    BOOLEAN DiffuseUsed = FALSE;
    BOOLEAN SpecularUsed = FALSE;
    BOOLEAN SharedCoordDeclared = FALSE;
    BOOLEAN FollowsBump = FALSE;
    BOOLEAN LuminancePending = FALSE;
    UINT Active = (Flags >> 20) & 0xF;
    UINT Count;

    /* The chain ends at the first stage that reads a texture it lacks */
    if (Active > D9FixedStages)
        Active = D9FixedStages;
    Scan(Active, &DiffuseUsed, &SpecularUsed, Samples, ColorSamples);
    for (Count = 0; Count < Active; Count++)
    {
        if (D9TexKind(StageOf(Count)) == D9FixedTexNone && ColorSamples[Count])
            break;
    }
    DiffuseUsed = FALSE;
    SpecularUsed = AddSpecular;
    Scan(Count, &DiffuseUsed, &SpecularUsed, Samples, ColorSamples);
    if (Count == 0)
        DiffuseUsed = (Flags & D9FixedHasDiffuse) != 0;

    /* ps_2_0 and the comment that marks the converter's shaders */
    Out.Put(0xFFFF0200);
    Out.Put(0x0002FFFE);
    Out.Put(0x54584554);
    Out.Put(0x00464624);

    if ((Flags & D9FixedHasDiffuse) != 0 && DiffuseUsed)
    {
        Out.DeclareInput(D9RegInput, 0);
        Diffuse = D9Src(D9RegInput, 0, D9SwizzleAll);
    }
    else
    {
        Diffuse = D9Src(D9RegConst, 1, D9SwizzleW);
    }
    if ((Flags & D9FixedHasSpecular) != 0 && SpecularUsed)
    {
        Out.DeclareInput(D9RegInput, 1);
        Specular = D9Src(D9RegInput, 1, D9SwizzleAll);
    }
    else
    {
        Specular = D9Src(D9RegConst, 1, D9SwizzleX);
    }

    for (UINT Stage = 0; Stage < Count; Stage++)
    {
        if (!Samples[Stage])
        {
            Coords[Stage] = ZeroOne;
        }
        else if (Sprites)
        {
            if (!SharedCoordDeclared)
            {
                SharedCoordDeclared = TRUE;
                Out.DeclareInput(D9RegTexcoord, 0);
            }
            Coords[Stage] = D9Src(D9RegTexcoord, 0, D9SwizzleAll);
        }
        else if (D9CoordIndex(StageOf(Stage)) >= SetCount)
        {
            Coords[Stage] = ZeroOne;
        }
        else
        {
            Out.DeclareInput(D9RegTexcoord, static_cast<BYTE>(Stage));
            Coords[Stage] = D9Src(D9RegTexcoord, static_cast<BYTE>(Stage), D9SwizzleAll);
        }
    }
    for (UINT Stage = 0; Stage < Count; Stage++)
    {
        if (Samples[Stage])
            Out.DeclareSampler(SamplerKinds[D9TexKind(StageOf(Stage))], static_cast<BYTE>(Stage));
    }

    TempWritten = FALSE;
    Current = Diffuse;
    if (Count == 0)
    {
        if (AddSpecular)
        {
            Out.Pp(D9OpMov, D9TempDst(D9TempArg2, D9MaskAll), Diffuse);
            Out.Pp(D9OpAdd, D9Dst(D9RegTemp, D9TempPremodulate, D9MaskXYZ, TRUE), D9Temp(D9TempArg2), Specular);
            Out.Pp(D9OpMov, D9Dst(D9RegTemp, D9TempPremodulate, D9MaskW, TRUE), D9Swizzled(Diffuse, D9SwizzleW));
        }
        else
        {
            Out.Pp(D9OpMov, D9Dst(D9RegTemp, D9TempPremodulate, D9MaskAll, TRUE), Diffuse);
        }
        Out.Pp(D9OpMov, D9TempDst(D9TempSample, D9MaskAll), D9Temp(D9TempPremodulate));
    }

    for (UINT Stage = 0; Stage < Count; Stage++)
    {
        const D9_FIXED_PIXEL_STAGE& Info = StageOf(Stage);
        const UINT Set = D9CoordIndex(Info);
        const DWORD Kind = D9TexKind(Info);
        const DWORD ColorOp = D9ColorOp(Info);
        const D9_PS_SOURCE Sampler = D9Src(D9RegSampler, static_cast<BYTE>(Stage), D9SwizzleAll);
        D9_PS_SOURCE Coord = Coords[Stage];
        BYTE TextureTemp = Stage < 4 ? D9TempSample : D9TempLateSample;
        D9_PS_TARGET TextureTarget = D9TempDst(TextureTemp, D9MaskAll);

        StageIndex = Stage;
        Texture = D9Temp(TextureTemp);

        /* Coordinates shorter than the fetch reads are padded with zeros and a one */
        if (Coord.File != D9RegConst && Kind != D9FixedTexNone)
        {
            UINT Have = ((Flags >> (2 * Set)) & 3) + 1;
            UINT Need;

            if (Have < 2 && Sprites)
                Have = 4;
            if (D9Projected(Info))
                Need = D9SetIsWide(StageOf(Set)) ? 3 : 4;
            else
                Need = Kind == D9FixedTex2D ? 2 : 3;
            if (Need > Have)
            {
                static const BYTE Masks[4] = { 0, D9MaskX, D9MaskXY, D9MaskXYZ };

                Out.FullPrecision(D9OpMov, D9TempDst(D9TempResult, D9MaskAll), ZeroOne);
                Out.FullPrecision(D9OpMov, D9TempDst(D9TempResult, Masks[Have]), Coord);
                Coord = D9Temp(D9TempResult);
            }
        }

        if (FollowsBump)
        {
            if (D9Projected(Info))
            {
                if (D9SetIsWide(StageOf(Set)))
                    Out.FullPrecision(D9OpRcp, D9TempDst(D9TempSample, D9MaskAll), D9Swizzled(Coord, D9SwizzleZ));
                else
                    Out.FullPrecision(D9OpRcp, D9TempDst(D9TempSample, D9MaskX), D9Swizzled(Coord, D9SwizzleW));
                Out.FullPrecision(D9OpMul, D9TempDst(D9TempSample, D9MaskAll), Coord,
                           D9Src(D9RegTemp, D9TempSample, D9SwizzleX));
                Coord = D9Temp(D9TempSample);
            }
            Out.FullPrecision(D9OpAdd, D9TempDst(D9TempBump, D9MaskAll), Coord, D9Temp(D9TempOffset));
            Out.Pp(D9OpTexld, TextureTarget, D9Temp(D9TempBump), Sampler);
            if (LuminancePending)
            {
                Out.Pp(D9OpMul, D9TempDst(TextureTemp, D9MaskXYZ), Texture,
                       D9Src(D9RegTemp, D9TempLuminance, D9SwizzleX));
                LuminancePending = FALSE;
            }
            FollowsBump = FALSE;
        }
        else if (D9IsBumpOp(ColorOp))
        {
            Sample(Stage, Coord, D9TempDst(D9TempBump, D9MaskAll));
            if (ColorOp == D3DTOP_BUMPENVMAPLUMINANCE)
            {
                BYTE Luminance = static_cast<BYTE>(D9FixedConstLuminance + Stage / 2);
                BOOLEAN Odd = (Stage & 1) != 0;

                Out.Pp(D9OpMad, D9Dst(D9RegTemp, D9TempLuminance, D9MaskX, TRUE),
                       D9Src(D9RegTemp, D9TempBump, D9SwizzleZ),
                       D9Src(D9RegConst, Luminance, Odd ? D9SwizzleZ : D9SwizzleX),
                       D9Src(D9RegConst, Luminance, Odd ? D9SwizzleW : D9SwizzleY));
                LuminancePending = TRUE;
            }
            Out.Pp(D9OpMov, D9TempDst(D9TempOffset, D9MaskAll), D9Src(D9RegConst, 1, D9SwizzleX));
            Out.Pp(D9OpM3x2, D9TempDst(D9TempOffset, D9MaskXY), D9Temp(D9TempBump),
                   D9Src(D9RegConst, static_cast<BYTE>(D9FixedConstBump + 2 * Stage), D9SwizzleAll));
            FollowsBump = TRUE;
        }
        else if (Samples[Stage])
        {
            Sample(Stage, Coord, TextureTarget);
        }

        /* A bump stage only perturbs the next stage's coordinates */
        if (D9IsBumpOp(ColorOp))
            continue;

        StageCurrent = Current;
        ColorTarget = D9TempDst(D9TempResult, D9MaskXYZ);
        ColorTarget.Saturate = !AddSpecular;
        AlphaTarget = D9TempDst(D9TempResult, D9MaskW);
        AlphaTarget.Saturate = !AddSpecular;

        /* The stage after a premodulate one starts from the product with its texture */
        if (Stage > 0)
        {
            BOOLEAN ColorPremodulated = D9ColorOp(StageOf(Stage - 1)) == D3DTOP_PREMODULATE;
            BOOLEAN AlphaPremodulated = D9AlphaOp(StageOf(Stage - 1)) == D3DTOP_PREMODULATE;

            if (ColorPremodulated && AlphaPremodulated)
            {
                Out.Pp(D9OpMul, D9TempDst(D9TempPremodulate, D9MaskAll), StageCurrent, Texture);
            }
            else if (ColorPremodulated || AlphaPremodulated)
            {
                Out.Pp(D9OpMov, D9TempDst(D9TempPremodulate, D9MaskAll), Texture);
                if (ColorPremodulated)
                    Out.Pp(D9OpMul, D9TempDst(D9TempPremodulate, D9MaskXYZ), StageCurrent, Texture);
                if (AlphaPremodulated)
                    Out.Pp(D9OpMul, D9TempDst(D9TempPremodulate, D9MaskW), StageCurrent, Texture);
            }
            if (ColorPremodulated || AlphaPremodulated)
                StageCurrent = D9Temp(D9TempPremodulate);
        }

        BlendColor(ColorOp);
        if (ColorOp != D3DTOP_DOTPRODUCT3)
            BlendAlpha(D9AlphaOp(Info));

        if (Stage == Count - 1)
        {
            if (AddSpecular)
            {
                Out.Pp(D9OpAdd, D9Dst(D9RegTemp, D9TempResult, D9MaskXYZ, TRUE), D9Temp(D9TempResult), Specular);
                Out.Pp(D9OpMov, D9Dst(D9RegTemp, D9TempResult, D9MaskW, TRUE),
                       D9Src(D9RegTemp, D9TempResult, D9SwizzleW));
            }
            Out.Pp(D9OpMov, D9TempDst(D9TempSample, D9MaskAll), D9Temp(D9TempResult));
        }
        else if (D9ResultArg(Info) != D3DTA_TEMP)
        {
            Out.Pp(D9OpMov, D9TempDst(D9TempCurrent, D9MaskAll), D9Temp(D9TempResult));
            Current = D9Temp(D9TempCurrent);
        }
        else
        {
            Out.Pp(D9OpMov, D9TempDst(D9TempSaved, D9MaskAll), D9Temp(D9TempResult));
            TempWritten = TRUE;
        }
    }

    Out.Pp(D9OpMov, D9Dst(D9RegColorOut, 0, D9MaskAll, FALSE), D9Temp(D9TempSample));
    Out.Put(0x0000FFFF);
}

BOOLEAN
D9BuildFixedPixelShader(
    _In_ const D9_FIXED_PIXEL_KEY* Key,
    _In_ BOOLEAN StageZeroTextured,
    _Out_writes_(D9FixedPixelTokenLimit) DWORD* Tokens,
    _Out_ UINT* Count)
{
    D9PsBuild Build(Key, StageZeroTextured, Tokens);

    Build.Build();
    *Count = Build.Out.Count;
    return !Build.Out.Full;
}

/* The key bytes that pick a shader: its size, its flags and the active stages */
static
BOOLEAN
D9SameKey(
    _In_ const D9_FIXED_PIXEL_KEY* Left,
    _In_ const D9_FIXED_PIXEL_KEY* Right)
{
    return memcmp(Left, Right, Left->Size) == 0;
}

VOID
D9PixelConverter::Start(
    _In_reads_(D9FixedStages * 33) const DWORD* Stages,
    _In_ DWORD Factor)
{
    static const FLOAT Scales[2][4] = { { 0.5f, 2.0f, 4.0f, 1.0f / 255.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } };

    ZeroMemory(&Key, sizeof(Key));
    Key.Size = sizeof(DWORD) * 2;
    ZeroMemory(&UsedKey, sizeof(UsedKey));
    UsedKey.Size = sizeof(DWORD) * 2;
    UsedKey.Flags = D9FixedNeverMatches;
    UsedShader = NULL;
    Dirty = TRUE;
    StageCount = 0;
    Fvf = 0;
    StageZeroTextured = FALSE;
    ZeroMemory(TextureKinds, sizeof(TextureKinds));
    ZeroMemory(TextureFormats, sizeof(TextureFormats));
    ZeroMemory(Constants, sizeof(Constants));
    CopyMemory(Constants, Scales, sizeof(Scales));
    ConstantMark = 0;
    SetFactor(Factor);
    for (UINT Stage = 0; Stage < D9FixedStages; Stage++)
    {
        for (UINT Type = D3DTSS_COLOROP; Type <= D3DTSS_CONSTANT; Type++)
            SetStage(Stage, Type, Stages[Stage * 33 + Type]);
    }
}

VOID
D9PixelConverter::KeepConstant(
    _In_ UINT Register)
{
    if (ConstantMark < Register + 1)
        ConstantMark = Register + 1;
}

/* Colors reach the shaders as four floats of zero to one, red first */
static
VOID
D9ColorFloats(
    _In_ DWORD Color,
    _Out_writes_(4) FLOAT* Values)
{
    Values[0] = static_cast<FLOAT>(((Color >> 16) & 0xFF) / 255.0);
    Values[1] = static_cast<FLOAT>(((Color >> 8) & 0xFF) / 255.0);
    Values[2] = static_cast<FLOAT>((Color & 0xFF) / 255.0);
    Values[3] = static_cast<FLOAT>((Color >> 24) / 255.0);
}

VOID
D9PixelConverter::SetFactor(
    _In_ DWORD Color)
{
    D9ColorFloats(Color, Constants[D9FixedConstFactor]);
    KeepConstant(D9FixedConstFactor);
}

BOOLEAN
D9PixelConverter::SetStage(
    _In_ UINT Stage,
    _In_ UINT Type,
    _In_ DWORD Value)
{
    D9_FIXED_PIXEL_STAGE* Info = &Key.Stages[Stage];
    BOOLEAN Recounted = FALSE;
    UINT Register;

    switch (Type)
    {
    case D3DTSS_COLOROP:
        Info->Blend = (Info->Blend & ~0x1Fu) | (Value & 0x1F);
        if (Value == D3DTOP_DISABLE ? Stage < StageCount : Stage == StageCount)
        {
            /* The active stages run up to the first disabled one */
            if (Value == D3DTOP_DISABLE)
            {
                StageCount = Stage;
            }
            else
            {
                StageCount = Stage + 1;
                while (StageCount < D9FixedStages && (Key.Stages[StageCount].Blend & 0x1F) != D3DTOP_DISABLE)
                    StageCount += 1;
            }
            Key.Size = sizeof(DWORD) * 2 + StageCount * sizeof(D9_FIXED_PIXEL_STAGE);
            Key.Flags = (Key.Flags & ~0x00F00000u) | (StageCount << 20);
            Recounted = TRUE;
        }
        break;
    case D3DTSS_COLORARG1:
        Info->Blend = (Info->Blend & ~0x003F0000u) | ((Value << 16) & 0x003F0000);
        break;
    case D3DTSS_COLORARG2:
        Info->Blend = (Info->Blend & ~0x0FC00000u) | ((Value << 22) & 0x0FC00000);
        break;
    case D3DTSS_ALPHAOP:
        if (Value == 0x7FFFFFFF)
            Value = D9FixedAlphaUnset;
        Info->Blend = (Info->Blend & ~0x3E0u) | ((Value << 5) & 0x3E0);
        break;
    case D3DTSS_ALPHAARG1:
        Info->Inputs = (Info->Inputs & ~0xFC0u) | ((Value << 6) & 0xFC0);
        break;
    case D3DTSS_ALPHAARG2:
        Info->Inputs = (Info->Inputs & ~0x3F000u) | ((Value << 12) & 0x3F000);
        break;
    case D3DTSS_COLORARG0:
        Info->Blend = (Info->Blend & ~0xFC00u) | ((Value << 10) & 0xFC00);
        break;
    case D3DTSS_ALPHAARG0:
        Info->Inputs = (Info->Inputs & ~0x3Fu) | (Value & 0x3F);
        break;
    case D3DTSS_RESULTARG:
        Info->Inputs = (Info->Inputs & ~0xFC0000u) | ((Value << 18) & 0xFC0000);
        break;
    case D3DTSS_TEXCOORDINDEX:
        /* A generated coordinate comes from the stage's own set */
        Info->Inputs = (Info->Inputs & ~0x07000000u) | ((Value <= 7 ? Value : Stage) << 24);
        break;
    case D3DTSS_TEXTURETRANSFORMFLAGS:
        Info->Inputs = (Info->Inputs & ~0x40000000u) | ((Value & D3DTTFF_PROJECTED) != 0 ? 0x40000000 : 0);
        break;
    case D3DTSS_BUMPENVMAT00:
    case D3DTSS_BUMPENVMAT10:
    case D3DTSS_BUMPENVMAT01:
    case D3DTSS_BUMPENVMAT11:
        Register = D9FixedConstBump + Stage * 2 +
                   ((Type == D3DTSS_BUMPENVMAT01 || Type == D3DTSS_BUMPENVMAT11) ? 1 : 0);
        CopyMemory(&Constants[Register][(Type == D3DTSS_BUMPENVMAT10 || Type == D3DTSS_BUMPENVMAT11) ? 1 : 0], &Value,
                   sizeof(Value));
        KeepConstant(Register);
        return FALSE;
    case D3DTSS_BUMPENVLSCALE:
    case D3DTSS_BUMPENVLOFFSET:
        Register = D9FixedConstLuminance + Stage / 2;
        CopyMemory(&Constants[Register][(Stage & 1) * 2 + (Type == D3DTSS_BUMPENVLOFFSET ? 1 : 0)], &Value,
                   sizeof(Value));
        KeepConstant(Register);
        return FALSE;
    case D3DTSS_CONSTANT:
        D9ColorFloats(Value, Constants[D9FixedConstStage + Stage]);
        KeepConstant(D9FixedConstStage + Stage);
        return FALSE;
    default:
        return FALSE;
    }
    Dirty = TRUE;
    return Recounted;
}

VOID
D9PixelConverter::SetSpecular(
    _In_ BOOLEAN Enable)
{
    if (((Key.Flags & D9FixedSpecularAdd) != 0) == (Enable != FALSE))
        return;
    Key.Flags ^= D9FixedSpecularAdd;
    Dirty = TRUE;
}

VOID
D9PixelConverter::SetPointSprite(
    _In_ BOOLEAN Enable)
{
    if (((Key.Flags & D9FixedPointSprite) != 0) == (Enable != FALSE))
        return;
    Key.Flags ^= D9FixedPointSprite;
    Dirty = TRUE;
}

/* Each coordinate set of a format records its float count; three float sets are marked on the stage of the
   same number */
VOID
D9PixelConverter::SetFvf(
    _In_ DWORD Format)
{
    static const BYTE Codes[4] = { 1, 2, 3, 0 };
    UINT Sets = (Format & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    DWORD Sizes = Format >> 16;

    if (Format == Fvf)
        return;
    Fvf = Format;
    Dirty = TRUE;
    Key.Flags &= ~(D9FixedHasDiffuse | D9FixedHasSpecular | 0x000F0000u | 0xFFFFu);
    if ((Format & D3DFVF_DIFFUSE) != 0)
        Key.Flags |= D9FixedHasDiffuse;
    if ((Format & D3DFVF_SPECULAR) != 0)
        Key.Flags |= D9FixedHasSpecular;
    Key.Flags |= Sets << 16;
    for (UINT Set = 0; Set < Sets; Set++)
    {
        UINT Size = Sizes & 3;

        Key.Stages[Set].Inputs &= 0x7FFFFFFF;
        if (Size == D3DFVF_TEXTUREFORMAT3)
            Key.Stages[Set].Inputs |= 0x80000000;
        Key.Flags |= static_cast<DWORD>(Codes[Size]) << (2 * Set);
        Sizes >>= 2;
    }
}

/* A declaration's coordinate sets record their float counts by usage index; colors by index 0 and 1 */
VOID
D9PixelConverter::SetElements(
    _In_ const D3DVERTEXELEMENT9* Elements)
{
    UINT Sets = 0;

    Key.Flags &= ~(D9FixedHasDiffuse | D9FixedHasSpecular | 0xFFFFu);
    Dirty = TRUE;
    Fvf = 0;
    for (UINT Index = 0; Index <= 101 && Elements[Index].Stream != 0xFF; Index++)
    {
        const D3DVERTEXELEMENT9& Element = Elements[Index];
        UINT Usage = Element.UsageIndex;

        if (Element.Usage == D3DDECLUSAGE_TEXCOORD)
        {
            D9_FIXED_PIXEL_STAGE* Entry = &Key.Stages[Usage & 7];

            Entry->Inputs &= 0x7FFFFFFF;
            if (Element.Type == D3DDECLTYPE_FLOAT3)
                Entry->Inputs |= 0x80000000;
            if (Sets <= Usage + 1)
                Sets = Usage + 1;
            /* The sizes live in the low word, which a wide type spills over into the next set */
            Key.Flags = (Key.Flags & ~0xFFFFu) |
                        (((Key.Flags & ~(3u << (2 * Usage))) | (static_cast<DWORD>(Element.Type) << (2 * Usage))) &
                         0xFFFF);
        }
        else if (Element.Usage == D3DDECLUSAGE_COLOR)
        {
            if (Usage == 0)
                Key.Flags |= D9FixedHasDiffuse;
            else if (Usage == 1)
                Key.Flags |= D9FixedHasSpecular;
        }
    }
    Key.Flags = (Key.Flags & ~0x000F0000u) | ((Sets & 0xF) << 16);
}

VOID
D9PixelConverter::SetTexture(
    _In_ UINT Stage,
    _In_ DWORD Kind,
    _In_ DWORD Format)
{
    D9_FIXED_PIXEL_STAGE* Info = &Key.Stages[Stage];

    if (TextureKinds[Stage] == Kind && TextureFormats[Stage] == Format)
        return;
    TextureKinds[Stage] = Kind;
    TextureFormats[Stage] = Format;
    Info->Inputs = (Info->Inputs & ~0x18000000u) | (Kind << 27);
    if (Stage == 0)
    {
        StageZeroTextured = Kind != D9FixedTexNone;
        Key.Flags &= ~0x10000000u;
    }
    if (Kind != D9FixedTexNone)
    {
        BOOLEAN Packed = Format == D3DFMT_R8G8_B8G8 || Format == D3DFMT_G8R8_G8B8;

        Info->Inputs = (Info->Inputs & ~0x20000000u) | (Packed ? 0x20000000 : 0);
    }
    Dirty = TRUE;
}

BOOLEAN
D9PixelConverter::Find(
    _Out_ HANDLE* Shader)
{
    *Shader = NULL;
    if (D9SameKey(&UsedKey, &Key))
    {
        *Shader = UsedShader;
        return TRUE;
    }
    for (UINT Index = 0; Index < CacheCount; Index++)
    {
        const D9_FIXED_PIXEL_ENTRY& Entry = Cache[(CacheFirst + Index) % D9FixedCacheLimit];

        if (D9SameKey(&Key, &Entry.Key) && Entry.Shader != NULL)
        {
            *Shader = Entry.Shader;
            UsedKey = Entry.Key;
            UsedShader = Entry.Shader;
            return TRUE;
        }
    }
    return FALSE;
}

BOOLEAN
D9PixelConverter::Keep(
    _In_ HANDLE Shader,
    _Out_ HANDLE* Evicted)
{
    D9_FIXED_PIXEL_ENTRY* Entry;

    *Evicted = NULL;
    if (Cache == nullptr)
    {
        Cache = new (std::nothrow) D9_FIXED_PIXEL_ENTRY[D9FixedCacheLimit];
        if (Cache == nullptr)
            return FALSE;
    }
    if (CacheCount == D9FixedCacheLimit)
    {
        *Evicted = Cache[CacheFirst].Shader;
        CacheFirst = (CacheFirst + 1) % D9FixedCacheLimit;
        CacheCount -= 1;
    }
    Entry = &Cache[(CacheFirst + CacheCount) % D9FixedCacheLimit];
    ZeroMemory(&Entry->Key, sizeof(Entry->Key));
    CopyMemory(&Entry->Key, &Key, Key.Size);
    Entry->Shader = Shader;
    CacheCount += 1;
    UsedKey = Entry->Key;
    UsedShader = Shader;
    return TRUE;
}

BOOLEAN
D9PixelConverter::TakeOldest(
    _Out_ HANDLE* Shader)
{
    *Shader = NULL;
    if (CacheCount == 0)
        return FALSE;
    *Shader = Cache[CacheFirst].Shader;
    CacheFirst = (CacheFirst + 1) % D9FixedCacheLimit;
    CacheCount -= 1;
    return TRUE;
}
