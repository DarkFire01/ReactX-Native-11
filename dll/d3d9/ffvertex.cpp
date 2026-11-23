/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shaders standing in for the fixed function vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ffvertex.hpp"

#include <math.h>
#include <new>
#include <string.h>

/* Register files as the tokens number them */
static constexpr BYTE D9VsTemp = 0;
static constexpr BYTE D9VsInput = 1;
static constexpr BYTE D9VsConst = 2;
static constexpr BYTE D9VsRasterOut = 4;
static constexpr BYTE D9VsColorOut = 5;
static constexpr BYTE D9VsTexcoordOut = 6;

/* Swizzles that replicate one component, and the identity */
static constexpr BYTE D9VsX = 0x00;
static constexpr BYTE D9VsY = 0x55;
static constexpr BYTE D9VsZ = 0xAA;
static constexpr BYTE D9VsW = 0xFF;
static constexpr BYTE D9VsAll = 0xE4;

/* Opcodes the converter writes */
static constexpr DWORD D9VsMov = 0x01;
static constexpr DWORD D9VsAdd = 0x02;
static constexpr DWORD D9VsMad = 0x04;
static constexpr DWORD D9VsMul = 0x05;
static constexpr DWORD D9VsRcp = 0x06;
static constexpr DWORD D9VsRsq = 0x07;
static constexpr DWORD D9VsDp3 = 0x08;
static constexpr DWORD D9VsM3x3 = 0x17;
static constexpr DWORD D9VsPow = 0x20;
static constexpr DWORD D9VsAbs = 0x23;
static constexpr DWORD D9VsNrm = 0x24;

/* Declaration usages */
static constexpr BYTE D9VsUsagePosition = 0;
static constexpr BYTE D9VsUsageWeight = 1;
static constexpr BYTE D9VsUsageIndices = 2;
static constexpr BYTE D9VsUsageNormal = 3;
static constexpr BYTE D9VsUsageSize = 4;
static constexpr BYTE D9VsUsageTexcoord = 5;
static constexpr BYTE D9VsUsageColor = 10;

/* Fixed registers: constants, zero one and the numbers, and the matrices */
static constexpr UINT D9VsConstZero = 0;
static constexpr UINT D9VsConstNumbers = 1;
static constexpr UINT D9VsConstWorldViewProj = 3;
static constexpr UINT D9VsConstAmbient = 7;
static constexpr UINT D9VsConstFogColor = 8;
static constexpr UINT D9VsConstFog = 9;
static constexpr UINT D9VsConstWorldView = 10;
static constexpr UINT D9VsConstNormal = 14;
static constexpr UINT D9VsConstView = 18;
static constexpr UINT D9VsConstViewInverse = 22;
static constexpr UINT D9VsConstProjection = 26;
static constexpr UINT D9VsConstTexture = 30;
static constexpr UINT D9VsConstPoint = 98;
static constexpr UINT D9VsConstPointScale = 99;
static constexpr UINT D9VsConstMaterial = 100;

/* Temporaries each part of the shader works in */
static constexpr BYTE D9VsTempEye = 0;
static constexpr BYTE D9VsTempNormal = 1;
static constexpr BYTE D9VsTempOut = 10;
static constexpr BYTE D9VsTempScratch = 11;

/* Inputs, modes and outputs as the key packs them */
static constexpr DWORD D9VsInPosition2 = 0x00000002;
static constexpr DWORD D9VsInNormal = 0x00000004;
static constexpr DWORD D9VsInNormal2 = 0x00000008;
static constexpr DWORD D9VsInDiffuse = 0x00000010;
static constexpr DWORD D9VsInSpecular = 0x00000020;
static constexpr DWORD D9VsInSize = 0x00000040;
static constexpr DWORD D9VsInWeights = 0x00000080;
static constexpr DWORD D9VsInIndices = 0x00010000;
static constexpr DWORD D9VsOutDiffuse = 0x00040000;
static constexpr DWORD D9VsOutSpecular = 0x00080000;
static constexpr DWORD D9VsOutSize = 0x00100000;
static constexpr DWORD D9VsOutFog = 0x00200000;
static constexpr DWORD D9VsBlending = 0x00400000;
static constexpr DWORD D9VsTweening = 0x00800000;
static constexpr DWORD D9VsLighting = 0x01000000;
static constexpr DWORD D9VsLocalViewer = 0x02000000;
static constexpr DWORD D9VsIndexed = 0x04000000;
static constexpr DWORD D9VsFog = 0x08000000;
static constexpr DWORD D9VsRangeFog = 0x10000000;
static constexpr DWORD D9VsSpecularOn = 0x20000000;
static constexpr DWORD D9VsColorVertex = 0x40000000;
static constexpr DWORD D9VsNormalize = 0x80000000;

static constexpr DWORD D9VsModePointScale = 0x00000010;
static constexpr DWORD D9VsModePointList = 0x00000020;
static constexpr DWORD D9VsModeFog = 0x00300000;
static constexpr DWORD D9VsModeTableFog = 0x00400000;

/* What the output description records beyond the stage count and coordinate sizes */
static constexpr DWORD D9VsWritesPosition = 0x0004;
static constexpr DWORD D9VsWritesSize = 0x0020;
static constexpr DWORD D9VsWritesDiffuse = 0x0040;
static constexpr DWORD D9VsWritesSpecular = 0x0080;
static constexpr DWORD D9VsWritesFog = 0x2000;

typedef struct _D9_VS_SOURCE
{
    BYTE File;
    BYTE Swizzle;
    USHORT Number;
    BOOLEAN Negate;
    BOOLEAN Relative;
    BOOLEAN Indexed;
} D9_VS_SOURCE;

typedef struct _D9_VS_TARGET
{
    BYTE File;
    BYTE Mask;
    USHORT Number;
} D9_VS_TARGET;

static
D9_VS_SOURCE
D9VsSrc(
    _In_ BYTE File,
    _In_ UINT Number,
    _In_ BYTE Swizzle)
{
    D9_VS_SOURCE Source = { File, Swizzle, static_cast<USHORT>(Number), FALSE, FALSE, FALSE };

    return Source;
}

static
D9_VS_TARGET
D9VsDst(
    _In_ BYTE File,
    _In_ UINT Number,
    _In_ BYTE Mask)
{
    D9_VS_TARGET Target = { File, Mask, static_cast<USHORT>(Number) };

    return Target;
}

static
D9_VS_SOURCE
D9VsSwizzled(
    _In_ D9_VS_SOURCE Source,
    _In_ BYTE Swizzle)
{
    Source.Swizzle = Swizzle;
    return Source;
}

static
D9_VS_SOURCE
D9VsNegated(
    _In_ D9_VS_SOURCE Source)
{
    Source.Negate = TRUE;
    return Source;
}

/* A constant read as the converter's register block lays it out: one component replicated, or a whole register */
static
D9_VS_SOURCE
D9VsConstant(
    _In_ UINT Register,
    _In_ UINT Component)
{
    static const BYTE Replicated[4] = { D9VsX, D9VsY, D9VsZ, D9VsW };

    return D9VsSrc(D9VsConst, Register, Replicated[Component & 3]);
}

static
D9_VS_SOURCE
D9VsWhole(
    _In_ UINT Register)
{
    return D9VsSrc(D9VsConst, Register, D9VsAll);
}

/* The first components of a target */
static
D9_VS_TARGET
D9VsFirst(
    _In_ D9_VS_TARGET Target,
    _In_ UINT Count)
{
    static const BYTE Masks[5] = { 0x1, 0x1, 0x3, 0x7, 0xF };

    Target.Mask = Masks[Count > 4 ? 1 : Count];
    return Target;
}

/* Collects the tokens of one shader */
class D9VsWriter
{
public:
    D9VsWriter(
        _Out_writes_(D9FixedVertexTokenLimit) DWORD* Tokens)
        : Tokens(Tokens)
    {
    }

    VOID
    Put(
        _In_ DWORD Token)
    {
        if (Count < D9FixedVertexTokenLimit)
            Tokens[Count] = Token;
        else
            Overflow = TRUE;
        Count += 1;
    }

    static
    DWORD
    Register(
        _In_ BYTE File,
        _In_ UINT Number)
    {
        return 0x80000000 | (static_cast<DWORD>(File & 7) << 28) | (static_cast<DWORD>(File & 0x18) << 8) | Number;
    }

    VOID
    Target(
        _In_ const D9_VS_TARGET& Target)
    {
        Put(Register(Target.File, Target.Number) | (static_cast<DWORD>(Target.Mask) << 16));
    }

    VOID
    Source(
        _In_ const D9_VS_SOURCE& Source)
    {
        Put(Register(Source.File, Source.Number) | (static_cast<DWORD>(Source.Swizzle) << 16) |
            (Source.Negate ? 0x01000000 : 0) | (Source.Relative || Source.Indexed ? 0x2000 : 0));
        if (Source.Relative)
            Put(0xF0E40800);
        else if (Source.Indexed)
            Put(0xB0000000);
    }

    /* An instruction's token counts the tokens after it, relative addresses included */
    VOID
    Begin(
        _In_ DWORD Opcode)
    {
        Start = Count;
        Put(Opcode);
    }

    VOID
    End(VOID)
    {
        if (Start < D9FixedVertexTokenLimit)
            Tokens[Start] |= (Count - Start - 1) << 24;
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First)
    {
        Begin(Opcode);
        Target(Out);
        Source(First);
        End();
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First,
        _In_ const D9_VS_SOURCE& Second)
    {
        Begin(Opcode);
        Target(Out);
        Source(First);
        Source(Second);
        End();
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First,
        _In_ const D9_VS_SOURCE& Second,
        _In_ const D9_VS_SOURCE& Third)
    {
        Begin(Opcode);
        Target(Out);
        Source(First);
        Source(Second);
        Source(Third);
        End();
    }

    VOID
    Declare(
        _In_ BYTE Usage,
        _In_ UINT Index,
        _In_ UINT Number)
    {
        Put(0x0200001F);
        Put(0x80000000 | Usage | (Index << 16));
        Put(0x900F0000 | Number);
    }

    DWORD* Tokens;
    UINT Count = 0;
    UINT Start = 0;
    BOOLEAN Overflow = FALSE;
};

/* Opens a block taken when the comparison holds */
static
VOID
D9VsIf(
    _Inout_ D9VsWriter* Out,
    _In_ DWORD Comparison,
    _In_ const D9_VS_SOURCE& Left,
    _In_ const D9_VS_SOURCE& Right)
{
    Out->Begin(0x29 | (Comparison << 16));
    Out->Source(Left);
    Out->Source(Right);
    Out->End();
}

/* Input registers number in the order the shader first declares them */
class D9VsInputs
{
public:
    UINT
    Take(
        _Inout_ INT* Slot)
    {
        if (*Slot < 0)
        {
            *Slot = static_cast<INT>(Next);
            Next += 1;
        }
        return static_cast<UINT>(*Slot);
    }

    INT Position = -1;
    INT Normal = -1;
    INT Size = -1;
    INT Weights = -1;
    INT Indices = -1;
    INT Diffuse = -1;
    INT Specular = -1;
    INT Position2 = -1;
    INT Normal2 = -1;
    INT Texcoords[D9VertexStages] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    UINT Next = 0;
};

/* Position times a matrix of four registers, through a temporary, into the target */
static
VOID
D9VsTransform(
    _Inout_ D9VsWriter* Out,
    _In_ const D9_VS_TARGET& Target,
    _In_ const D9_VS_SOURCE& Position,
    _In_ UINT Matrix,
    _In_ BYTE Scratch)
{
    D9_VS_TARGET Work = D9VsDst(D9VsTemp, Scratch, 0xF);
    D9_VS_SOURCE Partial = D9VsSrc(D9VsTemp, Scratch, D9VsAll);

    Out->Op(D9VsMul, Work, D9VsSwizzled(Position, D9VsX), D9VsWhole(Matrix));
    Out->Op(D9VsMad, Work, D9VsSwizzled(Position, D9VsY), D9VsWhole(Matrix + 1), Partial);
    Out->Op(D9VsMad, Work, D9VsSwizzled(Position, D9VsZ), D9VsWhole(Matrix + 2), Partial);
    Out->Op(D9VsAdd, Target, D9VsWhole(Matrix + 3), Partial);
}

/* The coordinate a stage's generation byte names: a set index, or a generation mode */
static
DWORD
D9VsCoordinateSource(
    _In_ BYTE Stage)
{
    switch (Stage & 0xF)
    {
    case 8:
        return D3DTSS_TCI_CAMERASPACENORMAL;
    case 9:
        return D3DTSS_TCI_CAMERASPACEPOSITION;
    case 10:
        return D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR;
    case 11:
        return D3DTSS_TCI_SPHEREMAP;
    default:
        return Stage & 0xF;
    }
}

/* Coordinates made from the eye space position or normal; a reflection or sphere map also reads the eye
   vector, which a local viewer takes from the position */
static
D9_VS_SOURCE
D9VsGenerate(
    _Inout_ D9VsWriter* Out,
    _In_ DWORD Source,
    _In_ BOOLEAN HasNormal,
    _In_ BOOLEAN LocalViewer,
    _Out_ UINT* Last)
{
    D9_VS_SOURCE Eye = D9VsSrc(D9VsTemp, D9VsTempEye, D9VsAll);
    D9_VS_SOURCE Normal = D9VsSrc(D9VsTemp, D9VsTempNormal, D9VsAll);
    D9_VS_SOURCE View = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsAll);
    D9_VS_SOURCE Twice = D9VsSrc(D9VsTemp, 9, D9VsX);
    D9_VS_TARGET Result = D9VsDst(D9VsTemp, D9VsTempScratch, 0x7);
    D9_VS_SOURCE Made = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
    BOOLEAN Sphere = Source == D3DTSS_TCI_SPHEREMAP;
    D9_VS_TARGET Reflected = Sphere ? D9VsDst(D9VsTemp, 8, 0x7) : Result;

    *Last = 2;
    if (Source == D3DTSS_TCI_CAMERASPACENORMAL)
    {
        if (!HasNormal)
            Out->Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), D9VsConstant(D9VsConstZero, 0));
        return Normal;
    }
    if (Source == D3DTSS_TCI_CAMERASPACEPOSITION)
        return Eye;

    /* The reflection of the eye vector about the normal, the eye looking down z unless the viewer is local */
    if (LocalViewer)
    {
        Out->Op(D9VsNrm, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Eye);
        if (HasNormal)
        {
            Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 9, 0x1), Normal, View);
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 9, 0x1), Twice, Twice);
            Out->Op(D9VsMad, Reflected, D9VsNegated(Twice), Normal, View);
        }
        else
        {
            Out->Op(D9VsMov, Reflected, View);
        }
        if (Sphere)
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 8, 0x4), D9VsSrc(D9VsTemp, 8, D9VsZ),
                    D9VsNegated(D9VsConstant(D9VsConstZero, 3)));
    }
    else if (HasNormal)
    {
        Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 9, 0x1), D9VsSwizzled(Normal, D9VsZ), D9VsSwizzled(Normal, D9VsZ));
        Out->Op(D9VsMul, Reflected, D9VsNegated(Twice), Normal);
    }
    else if (Sphere)
    {
        Out->Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempScratch, 0x3), D9VsConstant(D9VsConstNumbers, 0));
        *Last = 1;
        return Made;
    }
    else
    {
        Out->Op(D9VsMov, Result, D9VsConstant(D9VsConstZero, 0));
    }
    if (!Sphere)
    {
        if (!LocalViewer)
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, D9VsTempScratch, 0x4), Made, D9VsConstant(D9VsConstZero, 3));
        return Made;
    }

    /* The sphere map halves the reflection over its length and moves it to the middle */
    Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 8, 0x4), D9VsSrc(D9VsTemp, 8, D9VsAll), D9VsSrc(D9VsTemp, 8, D9VsAll));
    Out->Op(D9VsRsq, D9VsDst(D9VsTemp, 8, 0x4), D9VsSrc(D9VsTemp, 8, D9VsZ));
    Out->Op(D9VsMul, D9VsDst(D9VsTemp, 8, 0x4), D9VsSrc(D9VsTemp, 8, D9VsZ), D9VsConstant(D9VsConstNumbers, 0));
    Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempScratch, 0x3), D9VsSrc(D9VsTemp, 8, D9VsAll),
            D9VsSrc(D9VsTemp, 8, D9VsZ), D9VsConstant(D9VsConstNumbers, 0));
    *Last = 1;
    return Made;
}

/* What the lighting part of a shader reads: the material colors, the light registers and the outputs it adds to */
typedef struct _D9_VS_LIGHTING
{
    D9_VS_SOURCE Ambient;
    D9_VS_SOURCE Emissive;
    D9_VS_SOURCE Diffuse;
    D9_VS_SOURCE Specular;
    BOOLEAN Single;
    BOOLEAN HasNormal;
    BOOLEAN Highlights;
    BOOLEAN LocalViewer;
    BOOLEAN FlowControl;
} D9_VS_LIGHTING;

/* One of a light's seven registers, through the loop counter when the lights loop */
static
D9_VS_SOURCE
D9VsLightRegister(
    _In_ const D9_VS_LIGHTING* Lighting,
    _In_ UINT Part)
{
    D9_VS_SOURCE Source = D9VsWhole(D9VertexFixedRegisters + Part);

    Source.Relative = !Lighting->Single;
    return Source;
}

/* The diffuse, specular and ambient terms of one light of a kind, added to r10 and r11 */
static
VOID
D9VsLightTerms(
    _Inout_ D9VsWriter* Out,
    _In_ const D9_VS_LIGHTING* Lighting,
    _In_ D3DLIGHTTYPE Kind)
{
    static const D9_VS_TARGET Toward = { D9VsTemp, 0x7, 5 };
    D9_VS_SOURCE Eye = D9VsSrc(D9VsTemp, D9VsTempEye, D9VsAll);
    D9_VS_SOURCE Normal = D9VsSrc(D9VsTemp, D9VsTempNormal, D9VsAll);
    D9_VS_SOURCE Facing = D9VsSrc(D9VsTemp, 9, D9VsX);
    D9_VS_SOURCE Distance = D9VsSrc(D9VsTemp, 9, D9VsY);
    D9_VS_SOURCE Strength = D9VsSrc(D9VsTemp, 9, D9VsZ);
    D9_VS_SOURCE Falloff = D9VsSrc(D9VsTemp, 9, D9VsW);
    D9_VS_SOURCE Work = D9VsSrc(D9VsTemp, 8, D9VsAll);
    D9_VS_SOURCE Half = D9VsSrc(D9VsTemp, 7, D9VsAll);
    D9_VS_SOURCE Color = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsAll);
    D9_VS_SOURCE Highlight = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
    D9_VS_SOURCE Zero = D9VsConstant(D9VsConstZero, 0);
    D9_VS_SOURCE Light;
    BOOLEAN Directional = Kind == D3DLIGHT_DIRECTIONAL;

    if (Directional)
    {
        Light = D9VsNegated(D9VsLightRegister(Lighting, 3));
    }
    else
    {
        D9_VS_SOURCE Cone = D9VsLightRegister(Lighting, 6);
        D9_VS_SOURCE Attenuation = D9VsLightRegister(Lighting, 5);

        /* The unit vector toward the light, its distance, then the attenuation within the range */
        Light = D9VsSrc(D9VsTemp, 5, D9VsAll);
        Out->Op(D9VsAdd, Toward, D9VsLightRegister(Lighting, 4), D9VsNegated(Eye));
        Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 9, 0x2), Light, Light);
        Out->Op(D9VsRsq, D9VsDst(D9VsTemp, 9, 0x2), Distance);
        Out->Op(D9VsMul, Toward, Light, Distance);
        Out->Op(D9VsRcp, D9VsDst(D9VsTemp, 9, 0x2), Distance);
        Out->Op(0x0D, D9VsDst(D9VsTemp, 9, 0x4), D9VsSwizzled(Cone, D9VsZ), Distance);
        Out->Op(D9VsMul, D9VsDst(D9VsTemp, 9, 0x8), D9VsSwizzled(Attenuation, D9VsZ), Distance);
        Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 9, 0x8), D9VsSwizzled(Attenuation, D9VsY), Falloff);
        Out->Op(D9VsMad, D9VsDst(D9VsTemp, 9, 0x8), Falloff, Distance, D9VsSwizzled(Attenuation, D9VsX));
        Out->Op(D9VsRcp, D9VsDst(D9VsTemp, 9, 0x8), Falloff);
        Out->Op(D9VsMul, D9VsDst(D9VsTemp, 9, 0x4), Strength, Falloff);

        /* A spot light fades from the inner cone to the outer one */
        if (Kind == D3DLIGHT_SPOT)
        {
            D9_VS_SOURCE Spot = D9VsSrc(D9VsTemp, 7, D9VsW);

            Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 9, 0x2), Light, D9VsNegated(D9VsLightRegister(Lighting, 3)));
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 7, 0x8), D9VsSwizzled(Cone, D9VsX),
                    D9VsNegated(D9VsSwizzled(Cone, D9VsY)));
            Out->Op(D9VsRcp, D9VsDst(D9VsTemp, 7, 0x8), Spot);
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 7, 0x1), Distance, D9VsNegated(D9VsSwizzled(Cone, D9VsY)));
            Out->Op(D9VsMul, D9VsDst(D9VsTemp, 7, 0x8), Spot, D9VsSrc(D9VsTemp, 7, D9VsX));
            Out->Op(D9VsPow, D9VsDst(D9VsTemp, 7, 0x8), Spot, D9VsSwizzled(Attenuation, D9VsW));
            Out->Op(0x0C, D9VsDst(D9VsTemp, 7, 0x2), D9VsSwizzled(Cone, D9VsX), Distance);
            Out->Op(0x0B, D9VsDst(D9VsTemp, 7, 0x8), Spot, D9VsSrc(D9VsTemp, 7, D9VsY));
            Out->Op(0x0C, D9VsDst(D9VsTemp, 7, 0x4), D9VsSwizzled(Cone, D9VsY), Distance);
            Out->Op(D9VsMul, D9VsDst(D9VsTemp, 7, 0x8), Spot, D9VsSrc(D9VsTemp, 7, D9VsZ));
            Out->Op(D9VsMul, D9VsDst(D9VsTemp, 9, 0x4), Strength, Spot);
        }
    }

    /* Diffuse and specular need a normal; facing away from the light adds nothing */
    if (Lighting->HasNormal)
    {
        Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 9, 0x1), Normal, Light);
        if (Lighting->FlowControl)
            D9VsIf(Out, 1, Facing, Zero);
        else
            Out->Op(0x0B, D9VsDst(D9VsTemp, 9, 0x1), Facing, Zero);
        Out->Op(D9VsMul, D9VsDst(D9VsTemp, 8, 0x7), Facing, Lighting->Diffuse);
        Out->Op(D9VsMul, D9VsDst(D9VsTemp, 8, 0x7), Work, D9VsLightRegister(Lighting, 1));
        if (Directional)
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Work, Color);
        else
            Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Work, Strength, Color);

        if (Lighting->Highlights)
        {
            if (Lighting->LocalViewer)
            {
                Out->Op(D9VsNrm, D9VsDst(D9VsTemp, 8, 0x7), D9VsNegated(Eye));
                Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 8, 0x7), Work, Light);
            }
            else
            {
                Out->Op(D9VsMov, D9VsDst(D9VsTemp, 8, 0x7), Light);
                Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 8, 0x4), Work, D9VsNegated(D9VsConstant(D9VsConstZero, 3)));
            }
            Out->Op(D9VsNrm, D9VsDst(D9VsTemp, 7, 0x7), Work);
            Out->Op(D9VsDp3, D9VsDst(D9VsTemp, 9, 0x2), Normal, Half);
            if (Lighting->FlowControl)
            {
                D9VsIf(Out, 1, Distance, Zero);
            }
            else
            {
                Out->Op(0x0B, D9VsDst(D9VsTemp, 9, 0x2), Distance, Zero);
                Out->Op(0x0C, D9VsDst(D9VsTemp, 9, 0x1), Zero, Facing);
                Out->Op(D9VsMul, D9VsDst(D9VsTemp, 9, 0x2), Distance, Facing);
            }
            Out->Op(D9VsPow, D9VsDst(D9VsTemp, 9, 0x2), Distance, D9VsConstant(D9VsConstMaterial + 4, 0));
            if (!Directional)
                Out->Op(D9VsMul, D9VsDst(D9VsTemp, 9, 0x2), Distance, Strength);
            if (Lighting->Single)
            {
                Out->Op(D9VsMul, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), Distance, D9VsLightRegister(Lighting, 2));
            }
            else
            {
                Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), Distance, D9VsLightRegister(Lighting, 2),
                        Highlight);
            }
            if (Lighting->FlowControl)
                Out->Put(0x0000002B);
        }
        if (Lighting->FlowControl)
            Out->Put(0x0000002B);
    }

    /* The light's ambient color, attenuated unless it is directional */
    if (!Directional)
    {
        Out->Op(D9VsMul, D9VsDst(D9VsTemp, 7, 0x7), Lighting->Ambient, Strength);
        Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Half, D9VsLightRegister(Lighting, 0), Color);
    }
    else if (Lighting->Ambient.File == D9VsConst)
    {
        Out->Op(D9VsMov, D9VsDst(D9VsTemp, 7, 0x7), Lighting->Ambient);
        Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Half, D9VsLightRegister(Lighting, 0), Color);
    }
    else
    {
        Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempOut, 0x7), Lighting->Ambient, D9VsLightRegister(Lighting, 0),
                Color);
    }
}

/* Emissive and ambient light, then each kind of light, then the colors with the material's alphas */
static
VOID
D9VsWriteLighting(
    _Inout_ D9VsWriter* Out,
    _In_ const D9_FIXED_VERTEX_KEY* Key,
    _In_ const D9_VS_SOURCE& VertexDiffuse,
    _In_ const D9_VS_SOURCE& VertexSpecular,
    _In_ BOOLEAN FlowControl)
{
    static const D3DLIGHTTYPE Kinds[3] = { D3DLIGHT_DIRECTIONAL, D3DLIGHT_POINT, D3DLIGHT_SPOT };
    DWORD In = Key->Inputs;
    DWORD Modes = Key->Modes;
    D9_VS_LIGHTING Lighting;
    D9_VS_SOURCE Color = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsAll);
    D9_VS_TARGET Sum = D9VsDst(D9VsTemp, D9VsTempOut, 0x7);

    Lighting.Ambient = D9VsWhole(D9VsConstMaterial);
    Lighting.Emissive = D9VsWhole(D9VsConstMaterial + 1);
    Lighting.Diffuse = D9VsWhole(D9VsConstMaterial + 2);
    Lighting.Specular = D9VsWhole(D9VsConstMaterial + 3);
    if ((In & D9VsColorVertex) != 0)
    {
        if ((In & D9VsInDiffuse) != 0)
        {
            if ((Modes & 0xC00) == 0x400)
                Lighting.Diffuse = VertexDiffuse;
            if ((Modes & 0x3000) == 0x1000)
                Lighting.Specular = VertexDiffuse;
            if ((Modes & 0xC000) == 0x4000)
                Lighting.Ambient = VertexDiffuse;
            if ((Modes & 0x30000) == 0x10000)
                Lighting.Emissive = VertexDiffuse;
        }
        if ((In & D9VsInSpecular) != 0)
        {
            if ((Modes & 0xC00) == 0x800)
                Lighting.Diffuse = VertexSpecular;
            if ((Modes & 0x3000) == 0x2000)
                Lighting.Specular = VertexSpecular;
            if ((Modes & 0xC000) == 0x8000)
                Lighting.Ambient = VertexSpecular;
            if ((Modes & 0x30000) == 0x20000)
                Lighting.Emissive = VertexSpecular;
        }
    }
    Lighting.Single = (Modes & 0x8) != 0;
    Lighting.HasNormal = (In & D9VsInNormal) != 0;
    Lighting.Highlights = (In & D9VsSpecularOn) != 0 && (In & D9VsOutSpecular) != 0;
    Lighting.LocalViewer = (In & D9VsLocalViewer) != 0;
    Lighting.FlowControl = FlowControl;

    Out->Op(D9VsMov, Sum, Lighting.Ambient);
    Out->Op(D9VsMul, Sum, Color, D9VsWhole(D9VsConstAmbient));
    Out->Op(D9VsAdd, Sum, Color, Lighting.Emissive);
    if (Lighting.Highlights)
        Out->Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), D9VsConstant(D9VsConstZero, 0));

    for (UINT Kind = 0; Kind < 3; Kind++)
    {
        if ((Modes & (1u << Kind)) == 0)
            continue;
        if (!Lighting.Single)
        {
            Out->Put(0x0200001B);
            Out->Put(0xF0E40800);
            Out->Put(0xF0E40000 | Kind);
        }
        D9VsLightTerms(Out, &Lighting, Kinds[Kind]);
        if (!Lighting.Single)
            Out->Put(0x0000001D);
    }

    if ((In & D9VsOutDiffuse) != 0)
    {
        Out->Op(D9VsMov, D9VsDst(D9VsColorOut, 0, 0x7), Color);
        Out->Op(D9VsMov, D9VsDst(D9VsColorOut, 0, 0x8), D9VsSwizzled(Lighting.Diffuse, D9VsW));
    }
    if ((In & D9VsOutSpecular) != 0)
    {
        if ((In & D9VsSpecularOn) != 0)
        {
            Out->Op(D9VsMul, D9VsDst(D9VsColorOut, 1, 0x7), D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll),
                    Lighting.Specular);
            Out->Op(D9VsMov, D9VsDst(D9VsColorOut, 1, 0x8), D9VsSwizzled(Lighting.Specular, D9VsW));
        }
        else
        {
            Out->Op(D9VsMov, D9VsDst(D9VsColorOut, 1, 0xF), VertexSpecular);
        }
    }
}

/* The register of a blend matrix: the first one's fixed register, then a block for the rest */
static
UINT
D9VsBlendRegister(
    _In_ UINT Fixed,
    _In_ UINT Rest,
    _In_ UINT Index)
{
    return Index == 0 ? Fixed : Rest + 4 * (Index - 1);
}

/* A matrix of four registers read through the address register */
static
D9_VS_SOURCE
D9VsAddressed(
    _In_ UINT Register)
{
    D9_VS_SOURCE Source = D9VsWhole(Register);

    Source.Indexed = TRUE;
    return Source;
}

/* Position times a matrix whose registers the address register picks */
static
VOID
D9VsTransformIndexed(
    _Inout_ D9VsWriter* Out,
    _In_ const D9_VS_TARGET& Target,
    _In_ const D9_VS_SOURCE& Position,
    _In_ UINT Matrix,
    _In_ BOOLEAN Indexed,
    _In_ BYTE Scratch)
{
    D9_VS_TARGET Work = D9VsDst(D9VsTemp, Scratch, 0xF);
    D9_VS_SOURCE Partial = D9VsSrc(D9VsTemp, Scratch, D9VsAll);
    D9_VS_SOURCE Row[4];

    for (UINT Index = 0; Index < 4; Index++)
        Row[Index] = Indexed ? D9VsAddressed(Matrix + Index) : D9VsWhole(Matrix + Index);
    Out->Op(D9VsMul, Work, D9VsSwizzled(Position, D9VsX), Row[0]);
    Out->Op(D9VsMad, Work, D9VsSwizzled(Position, D9VsY), Row[1], Partial);
    Out->Op(D9VsMad, Work, D9VsSwizzled(Position, D9VsZ), Row[2], Partial);
    Out->Op(D9VsAdd, Target, Row[3], Partial);
}

/* Each blend matrix transforms the position, and the eye position and normal the shader needs, summed by
   weight; the last weight is what the others leave. Indexed blending picks each world matrix by the
   vertex's index and applies the view and projection after it */
static
VOID
D9VsBlend(
    _Inout_ D9VsWriter* Out,
    _In_ const D9_FIXED_VERTEX_KEY* Key,
    _In_ const D9_VS_SOURCE& Position,
    _In_ const D9_VS_SOURCE& Normal,
    _In_ const D9_VS_SOURCE& Weights,
    _In_ const D9_VS_SOURCE& Indices)
{
    static const BYTE Picks[4] = { D9VsX, D9VsY, D9VsZ, D9VsW };
    UINT Last = (Key->Modes >> 18) & 3;
    BOOLEAN Indexed = (Key->Inputs & D9VsIndexed) != 0;
    BOOLEAN NeedsEye = (Key->Needs & D9VertexNeedsPosition) != 0;
    BOOLEAN NeedsNormal = (Key->Needs & D9VertexNeedsNormal) != 0;
    BOOLEAN HasNormal = (Key->Inputs & D9VsInNormal) != 0;
    D9_VS_SOURCE Remaining = D9VsSrc(D9VsTemp, 8, D9VsX);
    D9_VS_SOURCE Moved = D9VsSrc(D9VsTemp, 9, D9VsAll);
    D9_VS_SOURCE Eye = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsAll);
    D9_VS_SOURCE Turned = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
    D9_VS_SOURCE Sum = D9VsSrc(D9VsTemp, 7, D9VsAll);

    Out->Op(D9VsMov, D9VsDst(D9VsTemp, 8, 0x1), D9VsConstant(D9VsConstZero, 3));
    Out->Op(D9VsMov, D9VsDst(D9VsTemp, 7, 0xF), D9VsConstant(D9VsConstZero, 0));
    if (NeedsNormal)
        Out->Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), D9VsConstant(D9VsConstZero, 0));
    if (NeedsEye)
        Out->Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempEye, 0xF), D9VsWhole(D9VsConstZero));

    for (UINT Index = 0; Index <= Last; Index++)
    {
        D9_VS_SOURCE Weight = Index == Last ? Remaining : D9VsSwizzled(Weights, Picks[Index]);

        if (Index != Last)
            Out->Op(D9VsAdd, D9VsDst(D9VsTemp, 8, 0x1), Remaining, D9VsNegated(Weight));

        if (Indexed)
        {
            D9_VS_TARGET Slot = D9VsDst(D9VsTemp, D9VsTempOut, 0x2);
            D9_VS_SOURCE Picked = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsY);

            Out->Op(D9VsMov, Slot, D9VsSwizzled(Indices, Picks[Index]));
            Out->Op(D9VsMul, Slot, Picked, D9VsConstant(D9VsConstNumbers, 1));
            Out->Op(D9VsAdd, Slot, Picked, D9VsConstant(D9VsConstNumbers, 3));
            Out->Op(0x2E, D9VsDst(3, 0, 0x1), Picked);
            D9VsTransformIndexed(Out, D9VsDst(D9VsTemp, 12, 0xF), Position, 0, TRUE, 6);
            D9VsTransformIndexed(Out, D9VsDst(D9VsTemp, D9VsTempOut, 0xF), D9VsSrc(D9VsTemp, 12, D9VsAll),
                                 D9VsConstView, FALSE, 6);
            D9VsTransformIndexed(Out, D9VsDst(D9VsTemp, 9, 0xF), Eye, D9VsConstProjection, FALSE, 6);
        }
        else
        {
            D9VsTransformIndexed(Out, D9VsDst(D9VsTemp, 9, 0xF), Position,
                                 D9VsBlendRegister(D9VsConstWorldViewProj, 62, Index), FALSE, 6);
        }
        if (Index == Last)
            Out->Op(D9VsMad, D9VsDst(D9VsRasterOut, 0, 0xF), Moved, Weight, Sum);
        else
            Out->Op(D9VsMad, D9VsDst(D9VsTemp, 7, 0xF), Moved, Weight, Sum);

        if (NeedsEye)
        {
            if (!Indexed)
            {
                D9VsTransformIndexed(Out, D9VsDst(D9VsTemp, D9VsTempOut, 0xF), Position,
                                     D9VsBlendRegister(D9VsConstWorldView, 74, Index), FALSE, 6);
            }
            Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempEye, 0x7), Eye, Weight,
                    D9VsSrc(D9VsTemp, D9VsTempEye, D9VsAll));
        }
        if (NeedsNormal && HasNormal)
        {
            if (Indexed)
            {
                Out->Op(D9VsM3x3, D9VsDst(D9VsTemp, 6, 0x7), Normal, D9VsWhole(D9VsConstViewInverse));
                Out->Op(D9VsM3x3, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), D9VsSrc(D9VsTemp, 6, D9VsAll),
                        D9VsAddressed(4));
            }
            else
            {
                Out->Op(D9VsM3x3, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), Normal,
                        D9VsWhole(D9VsBlendRegister(D9VsConstNormal, 86, Index)));
            }
            Out->Op(D9VsMad, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), Turned, Weight,
                    D9VsSrc(D9VsTemp, D9VsTempNormal, D9VsAll));
        }
    }
}

BOOLEAN
D9BuildFixedVertexShader(
    _Inout_ D9_FIXED_VERTEX_KEY* Key,
    _In_ BOOLEAN FlowControl,
    _In_ BOOLEAN Projected,
    _Out_writes_(D9FixedVertexTokenLimit) DWORD* Tokens,
    _Out_ UINT* Count)
{
    static const DWORD Comment[2] = { 0x54584554, 0x00464624 };
    D9VsWriter Out(Tokens);
    D9VsInputs Inputs;
    DWORD In = Key->Inputs;
    DWORD Modes = Key->Modes;
    DWORD Needs = Key->Needs;
    BOOLEAN DiffuseOut = (In & D9VsOutDiffuse) != 0;
    BOOLEAN SpecularOut = (In & D9VsOutSpecular) != 0;
    BOOLEAN HasNormal = (Needs & D9VertexNeedsNormal) != 0 && (In & D9VsInNormal) != 0;
    D9_VS_SOURCE Position;
    D9_VS_SOURCE Normal = {};
    D9_VS_SOURCE Diffuse = {};
    D9_VS_SOURCE Specular = {};
    D9_VS_SOURCE Size = {};
    D9_VS_SOURCE Weights = {};
    D9_VS_SOURCE Indices = {};
    D9_VS_SOURCE Position2 = {};
    D9_VS_SOURCE Normal2 = {};
    BOOLEAN TweenPosition = FALSE;
    BOOLEAN TweenNormal = FALSE;
    D9_VS_SOURCE Sets[D9VertexStages];
    DWORD Writes = D9VsWritesPosition;
    UINT Stages;

    *Count = 0;

    Position = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Position), D9VsAll);
    Out.Put(0xFFFE0200);
    Out.Put(0x0002FFFE);
    Out.Put(Comment[0]);
    Out.Put(Comment[1]);
    if (DiffuseOut)
        Writes |= D9VsWritesDiffuse;
    if (SpecularOut)
        Writes |= D9VsWritesSpecular;

    Out.Declare(D9VsUsagePosition, 0, Inputs.Take(&Inputs.Position));
    if (HasNormal)
    {
        Out.Declare(D9VsUsageNormal, 0, Inputs.Take(&Inputs.Normal));
        Normal = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Normal), D9VsAll);
    }
    /* Tweening reads a second position and normal */
    if ((In & D9VsTweening) != 0)
    {
        if ((In & D9VsInPosition2) != 0)
        {
            Out.Declare(D9VsUsagePosition, 1, Inputs.Take(&Inputs.Position2));
            Position2 = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Position2), D9VsAll);
            TweenPosition = TRUE;
        }
        if (HasNormal && (In & D9VsInNormal2) != 0)
        {
            Out.Declare(D9VsUsageNormal, 1, Inputs.Take(&Inputs.Normal2));
            Normal2 = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Normal2), D9VsAll);
            TweenNormal = TRUE;
        }
    }
    if (DiffuseOut)
    {
        if ((In & D9VsInDiffuse) != 0)
        {
            Out.Declare(D9VsUsageColor, 0, Inputs.Take(&Inputs.Diffuse));
            Diffuse = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Diffuse), D9VsAll);
        }
        else
        {
            Diffuse = D9VsConstant(D9VsConstZero, 3);
        }
    }
    if (SpecularOut)
    {
        if ((In & D9VsInSpecular) != 0)
        {
            Out.Declare(D9VsUsageColor, 1, Inputs.Take(&Inputs.Specular));
            Specular = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Specular), D9VsAll);
        }
        else
        {
            Specular = D9VsConstant(D9VsConstZero, 0);
        }
    }
    if ((Modes & D9VsModePointList) != 0)
    {
        if ((In & D9VsInSize) != 0)
        {
            Out.Declare(D9VsUsageSize, 0, Inputs.Take(&Inputs.Size));
            Size = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Size), D9VsX);
        }
        else
        {
            Size = D9VsConstant(D9VsConstPoint, 2);
        }
    }
    if ((Needs & D9VertexNeedsBlend) != 0)
    {
        if ((In & D9VsInWeights) != 0)
        {
            Out.Declare(D9VsUsageWeight, 0, Inputs.Take(&Inputs.Weights));
            Weights = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Weights), D9VsAll);
        }
        else
        {
            Weights = D9VsConstant(D9VsConstZero, 0);
        }
        if ((In & D9VsIndexed) != 0)
        {
            if ((In & D9VsInIndices) != 0)
            {
                Out.Declare(D9VsUsageIndices, 0, Inputs.Take(&Inputs.Indices));
                Indices = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Indices), D9VsAll);
            }
            else
            {
                Indices = D9VsWhole(2);
            }
        }
    }
    for (UINT Set = 0; Set < D9VertexStages; Set++)
    {
        if ((In & (0x100u << Set)) != 0)
        {
            Out.Declare(D9VsUsageTexcoord, Set, Inputs.Take(&Inputs.Texcoords[Set]));
            Sets[Set] = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Texcoords[Set]), D9VsAll);
        }
        else
        {
            Sets[Set] = D9VsWhole(D9VsConstZero);
        }
    }

    /* Tweening moves the position and normal toward the second ones by the tween factor */
    if (TweenPosition)
    {
        D9_VS_SOURCE Tweened = D9VsSrc(D9VsTemp, 2, D9VsAll);

        Out.Op(D9VsMov, D9VsDst(D9VsTemp, 2, 0xF), Position2);
        Out.Op(D9VsAdd, D9VsDst(D9VsTemp, 2, 0x7), Tweened, D9VsNegated(Position));
        Out.Op(D9VsMad, D9VsDst(D9VsTemp, 2, 0x7), D9VsConstant(D9VsConstFog, 3), Tweened, Position);
        Position = Tweened;
    }
    if (TweenNormal)
    {
        D9_VS_SOURCE Tweened = D9VsSrc(D9VsTemp, 3, D9VsAll);

        Out.Op(D9VsMov, D9VsDst(D9VsTemp, 3, 0x7), Normal2);
        Out.Op(D9VsAdd, D9VsDst(D9VsTemp, 3, 0x7), Tweened, D9VsNegated(Normal));
        Out.Op(D9VsMad, D9VsDst(D9VsTemp, 3, 0x7), D9VsConstant(D9VsConstFog, 3), Tweened, Normal);
        Normal = Tweened;
    }

    /* The eye space position and normal the later parts read, then the clip space position */
    if ((Needs & D9VertexNeedsBlend) != 0)
    {
        D9VsBlend(&Out, Key, Position, Normal, Weights, Indices);
    }
    else
    {
        if ((Needs & D9VertexNeedsPosition) != 0)
            D9VsTransform(&Out, D9VsDst(D9VsTemp, D9VsTempEye, 0xF), Position, D9VsConstWorldView, D9VsTempEye);
        if (HasNormal)
            Out.Op(D9VsM3x3, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), Normal, D9VsWhole(D9VsConstNormal));
        D9VsTransform(&Out, D9VsDst(D9VsRasterOut, 0, 0xF), Position, D9VsConstWorldViewProj, D9VsTempScratch);
    }
    if (HasNormal && (In & D9VsNormalize) != 0)
    {
        Out.Op(D9VsNrm, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), D9VsSrc(D9VsTemp, D9VsTempNormal, D9VsAll));
        Out.Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll));
    }

    /* Lighting makes the colors; without it the vertex colors pass through */
    if ((In & D9VsLighting) != 0)
    {
        D9VsWriteLighting(&Out, Key, Diffuse, Specular, FlowControl);
    }
    else
    {
        if (DiffuseOut)
            Out.Op(D9VsMov, D9VsDst(D9VsColorOut, 0, 0xF), Diffuse);
        if (SpecularOut)
            Out.Op(D9VsMov, D9VsDst(D9VsColorOut, 1, 0xF), Specular);
    }

    /* Vertex fog from the eye distance or depth, or the specular alpha when no mode is set */
    if ((In & (D9VsFog | D9VsOutFog)) == (D9VsFog | D9VsOutFog))
    {
        D9_VS_TARGET Fog = D9VsDst(D9VsRasterOut, 1, 0xF);
        D9_VS_TARGET Distance = D9VsDst(D9VsTemp, D9VsTempScratch, 0x1);
        D9_VS_SOURCE Measured = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsX);
        D9_VS_SOURCE Eye = D9VsSrc(D9VsTemp, D9VsTempEye, D9VsAll);
        DWORD Mode = Modes & D9VsModeFog;

        Writes |= D9VsWritesFog;
        if (Mode == 0)
        {
            Out.Op(D9VsMov, Fog, D9VsSwizzled(Specular, D9VsW));
        }
        else
        {
            if ((In & D9VsRangeFog) != 0)
            {
                Out.Op(D9VsDp3, Distance, Eye, Eye);
                Out.Op(D9VsRsq, Distance, Measured);
                Out.Op(D9VsRcp, Distance, Measured);
            }
            else
            {
                Out.Op(D9VsAbs, Distance, D9VsSwizzled(Eye, D9VsZ));
            }
            if (Mode == 0x00300000)
            {
                Out.Op(D9VsMul, Distance, Measured, D9VsConstant(D9VsConstFog, 0));
                Out.Op(D9VsAdd, Fog, Measured, D9VsConstant(D9VsConstFog, 1));
            }
            else
            {
                Out.Op(D9VsMul, Distance, Measured, D9VsConstant(D9VsConstFog, 2));
                if (Mode == 0x00200000)
                    Out.Op(D9VsMul, Distance, Measured, Measured);
                Out.Op(D9VsPow, D9VsDst(D9VsTemp, D9VsTempOut, 0xF), D9VsConstant(D9VsConstNumbers, 2),
                       D9VsNegated(Measured));
                Out.Op(D9VsMov, Fog, D9VsSrc(D9VsTemp, D9VsTempOut, D9VsX));
            }
        }
    }

    /* Point sizes, scaled by the eye distance when point scaling is on */
    if ((Modes & D9VsModePointList) != 0 && (In & D9VsOutSize) != 0)
    {
        D9_VS_TARGET Points = D9VsDst(D9VsRasterOut, 2, 0xF);

        Writes |= D9VsWritesSize;
        if ((Modes & D9VsModePointScale) != 0)
        {
            D9_VS_SOURCE Eye = D9VsSrc(D9VsTemp, D9VsTempEye, D9VsAll);
            D9_VS_TARGET Distance = D9VsDst(D9VsTemp, D9VsTempScratch, 0x1);
            D9_VS_SOURCE Measured = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsX);
            D9_VS_TARGET Sum = D9VsDst(D9VsTemp, D9VsTempOut, 0x1);
            D9_VS_SOURCE Summed = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsX);

            Out.Op(D9VsDp3, Distance, Eye, Eye);
            Out.Op(D9VsRsq, Distance, Measured);
            Out.Op(D9VsRcp, Distance, Measured);
            Out.Op(D9VsMov, Sum, D9VsConstant(D9VsConstPointScale, 1));
            Out.Op(D9VsMad, Sum, D9VsConstant(D9VsConstPointScale, 2), Measured, Summed);
            Out.Op(D9VsMad, Sum, Summed, Measured, D9VsConstant(D9VsConstPointScale, 0));
            if (FlowControl)
            {
                D9VsIf(&Out, 6, Summed, D9VsConstant(D9VsConstZero, 0));
                Out.Op(D9VsMov, Points, D9VsConstant(D9VsConstPoint, 0));
                Out.Put(0x0000002A);
                Out.Op(D9VsRsq, Sum, Summed);
                Out.Op(D9VsMul, Sum, Summed, D9VsConstant(D9VsConstPoint, 3));
                Out.Op(D9VsMul, Points, Size, Summed);
                Out.Put(0x0000002B);
            }
            else
            {
                Out.Op(0x0C, D9VsDst(D9VsTemp, D9VsTempOut, 0x2), Summed, D9VsConstant(D9VsConstZero, 0));
                Out.Op(D9VsMul, D9VsDst(D9VsTemp, D9VsTempOut, 0x4), D9VsSrc(D9VsTemp, D9VsTempOut, D9VsY),
                       D9VsConstant(D9VsConstPoint, 0));
                Out.Op(D9VsRsq, Sum, Summed);
                Out.Op(D9VsMul, Sum, Summed, D9VsConstant(D9VsConstPoint, 3));
                Out.Op(D9VsMad, Points, Size, Summed, D9VsSrc(D9VsTemp, D9VsTempOut, D9VsZ));
            }
        }
        else
        {
            Out.Op(D9VsMov, Points, Size);
        }
    }

    /* Each active stage gets the coordinates it reads in the output of its own number, through its transform
       and its projection */
    Stages = (Modes >> 6) & 0xF;
    Writes |= Stages << 8;
    for (UINT Stage = 0; Stage < Stages; Stage++)
    {
        static const BYTE Picks[4] = { D9VsX, D9VsY, D9VsZ, D9VsW };
        DWORD Source = D9VsCoordinateSource(Key->Stages[Stage]);
        DWORD Transform = (Key->Stages[Stage] >> 4) & 3;
        BOOLEAN Divide = ((Modes >> 24) & (1u << Stage)) != 0;
        D9_VS_SOURCE From;
        D9_VS_SOURCE Result;
        UINT Last;

        if ((Source & 0xFFFF0000) == 0)
        {
            From = Sets[Source & 7];
            Last = Key->Stages[Source & 7] >> 6;
        }
        else
        {
            From = D9VsGenerate(&Out, Source, (In & D9VsInNormal) != 0, (In & D9VsLocalViewer) != 0, &Last);
        }

        if (Transform != 0)
        {
            UINT Matrix = D9VsConstTexture + 4 * Stage;
            UINT Wanted = Transform + 1;
            D9_VS_TARGET Work = D9VsFirst(D9VsDst(D9VsTemp, D9VsTempOut, 0xF), Wanted);
            D9_VS_SOURCE Partial = D9VsSrc(D9VsTemp, D9VsTempOut, D9VsAll);

            /* A set the vertices lack reads zero through a temporary */
            if (From.File == D9VsConst)
            {
                Out.Op(D9VsMov, D9VsFirst(D9VsDst(D9VsTemp, D9VsTempScratch, 0xF), Last + 1), From);
                From = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
            }
            Out.Op(D9VsMul, Work, D9VsSwizzled(From, D9VsX), D9VsWhole(Matrix));
            for (UINT Part = 1; Part <= Last && Part < 4; Part++)
                Out.Op(D9VsMad, Work, D9VsSwizzled(From, Picks[Part]), D9VsWhole(Matrix + Part), Partial);
            if (Last < 3)
                Out.Op(D9VsAdd, Work, Partial, D9VsWhole(Matrix + Last + 1));
            Last = Wanted - 1;
            From = Partial;
        }

        Result = From;
        if (Divide)
        {
            if (Projected)
            {
                if (From.File == D9VsTemp)
                {
                    Out.Op(D9VsMov, D9VsDst(D9VsTemp, From.Number, 0x8), D9VsSwizzled(From, Picks[Last]));
                }
                else if (Last != 3)
                {
                    Out.Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), From);
                    Out.Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempScratch, 0x8), D9VsSwizzled(From, Picks[Last]));
                    Result = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
                }
                Last = 3;
            }
            else
            {
                Out.Op(D9VsRcp, D9VsDst(D9VsTemp, D9VsTempOut, 0x1), D9VsSwizzled(From, Picks[Last]));
                Out.Op(D9VsMul, D9VsFirst(D9VsDst(D9VsTemp, D9VsTempScratch, 0xF), Last + 1), From,
                       D9VsSrc(D9VsTemp, D9VsTempOut, D9VsX));
                Result = D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll);
            }
        }

        Out.Op(D9VsMov, D9VsFirst(D9VsDst(D9VsTexcoordOut, Stage, 0xF), Last + 1), Result);
        if (Last == 0)
            Writes |= 3u << (2 * Stage + 16);
        else if (Last == 2)
            Writes |= 1u << (2 * Stage + 16);
        else if (Last == 3)
            Writes |= 2u << (2 * Stage + 16);
    }
    Out.Put(0x0000FFFF);
    if (Out.Overflow)
        return FALSE;
    Key->Outputs = Writes;
    *Count = Out.Count;
    return TRUE;
}

/* Converter state */

VOID
D9VertexConverter::Touch(
    _In_ UINT Range,
    _In_ UINT First,
    _In_ UINT End)
{
    if (Ranges[Range].First > First)
        Ranges[Range].First = First;
    if (Ranges[Range].End < End)
        Ranges[Range].End = End;
}

/* Matrices go in as the application gave them, one row per register */
VOID
D9VertexConverter::WriteMatrix(
    _In_ UINT Register,
    _In_ const D3DMATRIX* Matrix)
{
    if (Mark < Register + 4)
        Mark = Register + 4;
    Touch(0, Register, Register + 4);
    CopyMemory(Registers[Register], Matrix, sizeof(*Matrix));
}

VOID
D9VertexConverter::Start(
    _In_ UINT DriverRegisters,
    _In_ BOOLEAN CanBranch,
    _In_ BOOLEAN CanProject)
{
    static const FLOAT Numbers[3][4] =
    {
        { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.5f, 8.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 2.0f, 3.0f },
    };
    static const DWORD Euler = 0x402DF84D;
    UINT Spare = DriverRegisters - D9VertexFixedRegisters;

    ZeroMemory(Registers, sizeof(Registers));
    CopyMemory(Registers, Numbers, sizeof(Numbers));
    CopyMemory(&Registers[D9VsConstNumbers][2], &Euler, sizeof(Euler));
    LightLimit = (Spare >> 1) / 7;
    if (LightLimit > 255)
        LightLimit = 255;
    MatrixLimit = Spare >> 4;
    if (MatrixLimit > 256)
        MatrixLimit = 256;
    Registers[D9VsConstNumbers][3] = static_cast<FLOAT>(7 * LightLimit + D9VertexFixedRegisters);
    FlowControl = CanBranch;
    Projected = CanProject;
    for (D9_VERTEX_RANGE& Range : Ranges)
    {
        Range.First = 0xFFFFFFFF;
        Range.End = 0;
    }
    Mark = 0;
    ZeroMemory(Loops, sizeof(Loops));
    LightCount = 0;
    EnabledCount = 0;
    ActiveCount = 0;
    ZeroMemory(&Key, sizeof(Key));
    ZeroMemory(&UsedKey, sizeof(UsedKey));
    UsedKey.Size = 0xFFFFFFFF;
    UsedShader = NULL;
    Dirty = TRUE;
    Stale = 0xFFFFFFFF;
    Updated = 0;
    DisabledStages = 0xFF;
    Lighting = FALSE;
    ColorVertex = FALSE;
    ZeroMemory(Sources, sizeof(Sources));
    FogStart = 0.0f;
    FogEnd = 0.0f;
    ZeroMemory(&View, sizeof(View));
    View._11 = View._22 = View._33 = View._44 = 1.0f;
    Projection = View;
    for (UINT Index = 0; Index < 256; Index++)
    {
        Worlds[Index] = View;
        WorldStale[Index] = Index != 0 && Index < MatrixLimit;
        InverseStale[Index] = WorldStale[Index];
    }
    ZeroMemory(WorldViews, sizeof(WorldViews));

    /* The block of numbers is the first range the driver gets */
    Ranges[0].First = 0;
    Ranges[0].End = 3;
}

/* Lighting with vertex colors takes each material color from where its source state points */
VOID
D9VertexConverter::ApplyMaterialSources(VOID)
{
    if (!Lighting || !ColorVertex)
    {
        Key.Inputs &= ~D9VsColorVertex;
        Key.Modes &= ~0x0003FC00u;
        return;
    }
    Key.Inputs |= D9VsColorVertex;
    for (UINT Index = 0; Index < 4; Index++)
        Key.Modes = (Key.Modes & ~(3u << (10 + 2 * Index))) | ((Sources[Index] << (10 + 2 * Index)) & (3u << (10 + 2 * Index)));
}

static
FLOAT
D9VsChannel(
    _In_ DWORD Color,
    _In_ UINT Shift)
{
    return static_cast<FLOAT>(static_cast<FLOAT>((Color >> Shift) & 0xFF) / 255.0);
}

BOOLEAN
D9VertexConverter::SetRenderState(
    _In_ UINT State,
    _In_ DWORD Value)
{
    FLOAT* Fixed;

    switch (State)
    {
    case D3DRS_FOGENABLE:
        Key.Inputs = (Key.Inputs & ~D9VsFog) | (Value != 0 ? D9VsFog : 0);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_SPECULARENABLE:
        Key.Inputs = (Key.Inputs & ~D9VsSpecularOn) | (Value != 0 ? D9VsSpecularOn : 0);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_FOGCOLOR:
        Fixed = Registers[D9VsConstFogColor];
        Fixed[0] = D9VsChannel(Value, 16);
        Fixed[1] = D9VsChannel(Value, 8);
        Fixed[2] = D9VsChannel(Value, 0);
        Touch(0, D9VsConstFogColor, D9VsConstFogColor + 1);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_FOGTABLEMODE:
        Key.Modes = (Key.Modes & ~D9VsModeTableFog) | (Value != 0 ? D9VsModeTableFog : 0);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_FOGSTART:
        CopyMemory(&FogStart, &Value, sizeof(FogStart));
        Stale |= 0x200000;
        Dirty = TRUE;
        return FALSE;
    case D3DRS_FOGEND:
        CopyMemory(&FogEnd, &Value, sizeof(FogEnd));
        Stale |= 0x200000;
        Dirty = TRUE;
        return FALSE;
    case D3DRS_FOGDENSITY:
        CopyMemory(&Registers[D9VsConstFog][2], &Value, sizeof(Value));
        Touch(0, D9VsConstFog, D9VsConstFog + 1);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_RANGEFOGENABLE:
        Key.Inputs = (Key.Inputs & ~D9VsRangeFog) | (Value != 0 ? D9VsRangeFog : 0);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_LIGHTING:
        if (Value == 0)
        {
            Lighting = FALSE;
            Key.Inputs &= ~(D9VsLighting | D9VsColorVertex);
            Key.Modes &= 0xFFFC03F0;
        }
        else if (!Lighting)
        {
            Lighting = TRUE;
            Stale |= 0x400000;
            Key.Inputs |= D9VsLighting;
            ApplyMaterialSources();
        }
        Dirty = TRUE;
        return TRUE;
    case D3DRS_AMBIENT:
        Fixed = Registers[D9VsConstAmbient];
        Fixed[0] = D9VsChannel(Value, 16);
        Fixed[1] = D9VsChannel(Value, 8);
        Fixed[2] = D9VsChannel(Value, 0);
        Touch(0, D9VsConstAmbient, D9VsConstAmbient + 1);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_FOGVERTEXMODE:
        Key.Modes = (Key.Modes & ~D9VsModeFog) | ((Value << 20) & D9VsModeFog);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_COLORVERTEX:
        ColorVertex = Value != 0;
        if (Lighting)
            ApplyMaterialSources();
        Dirty = TRUE;
        return TRUE;
    case D3DRS_LOCALVIEWER:
        Key.Inputs = (Key.Inputs & ~D9VsLocalViewer) | (Value != 0 ? D9VsLocalViewer : 0);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_NORMALIZENORMALS:
        Key.Inputs = (Key.Inputs & ~D9VsNormalize) | (Value != 0 ? D9VsNormalize : 0);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_DIFFUSEMATERIALSOURCE:
    case D3DRS_SPECULARMATERIALSOURCE:
    case D3DRS_AMBIENTMATERIALSOURCE:
    case D3DRS_EMISSIVEMATERIALSOURCE:
        Sources[State - D3DRS_DIFFUSEMATERIALSOURCE] = Value;
        if (Lighting && ColorVertex)
            ApplyMaterialSources();
        Dirty = TRUE;
        return TRUE;
    case D3DRS_VERTEXBLEND:
        Key.Inputs = (Key.Inputs & ~(D9VsTweening | D9VsBlending)) | (Value == D3DVBF_TWEENING ? D9VsTweening : 0) |
                     (Value != 0 ? D9VsBlending : 0);
        Key.Modes = (Key.Modes & ~0x000C0000u) | ((Value >= 1 && Value <= 3 ? Value : 0) << 18);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_POINTSIZE:
        CopyMemory(&Registers[D9VsConstPoint][2], &Value, sizeof(Value));
        Touch(0, D9VsConstPoint, D9VsConstPoint + 1);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_POINTSIZE_MIN:
        CopyMemory(&Registers[D9VsConstPoint][1], &Value, sizeof(Value));
        Touch(0, D9VsConstPoint, D9VsConstPoint + 1);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_POINTSIZE_MAX:
        CopyMemory(&Registers[D9VsConstPoint][0], &Value, sizeof(Value));
        Touch(0, D9VsConstPoint, D9VsConstPoint + 1);
        Dirty = TRUE;
        return FALSE;
    case D3DRS_POINTSCALEENABLE:
        Key.Modes = (Key.Modes & ~D9VsModePointScale) | (Value != 0 ? D9VsModePointScale : 0);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_POINTSCALE_A:
    case D3DRS_POINTSCALE_B:
    case D3DRS_POINTSCALE_C:
        CopyMemory(&Registers[D9VsConstPointScale][State - D3DRS_POINTSCALE_A], &Value, sizeof(Value));
        Touch(0, D9VsConstPointScale, D9VsConstPointScale + 1);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_INDEXEDVERTEXBLENDENABLE:
        Key.Inputs = (Key.Inputs & ~D9VsIndexed) | (Value != 0 ? D9VsIndexed : 0);
        Dirty = TRUE;
        return TRUE;
    case D3DRS_TWEENFACTOR:
        CopyMemory(&Registers[D9VsConstFog][3], &Value, sizeof(Value));
        Touch(0, D9VsConstFog, D9VsConstFog + 1);
        Dirty = TRUE;
        return TRUE;
    default:
        return FALSE;
    }
}

VOID
D9VertexConverter::SetStage(
    _In_ UINT Stage,
    _In_ UINT Type,
    _In_ DWORD Value)
{
    BYTE* Byte = &Key.Stages[Stage];
    DWORD Count;

    switch (Type)
    {
    case D3DTSS_COLOROP:
        if ((Value == D3DTOP_DISABLE) == ((DisabledStages & (1u << Stage)) != 0))
            return;
        DisabledStages ^= 1u << Stage;
        break;
    case D3DTSS_TEXCOORDINDEX:
        switch (Value & 0xFFFF0000)
        {
        case 0:
            *Byte = static_cast<BYTE>((*Byte & 0xF0) | (Value & 7));
            break;
        case D3DTSS_TCI_CAMERASPACENORMAL:
            *Byte = static_cast<BYTE>((*Byte & 0xF0) | 8);
            break;
        case D3DTSS_TCI_CAMERASPACEPOSITION:
            *Byte = static_cast<BYTE>((*Byte & 0xF0) | 9);
            break;
        case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
            *Byte = static_cast<BYTE>((*Byte & 0xF0) | 10);
            break;
        case D3DTSS_TCI_SPHEREMAP:
            *Byte = static_cast<BYTE>((*Byte & 0xF0) | 11);
            break;
        default:
            *Byte = static_cast<BYTE>(*Byte & 0xF0);
            break;
        }
        break;
    case D3DTSS_TEXTURETRANSFORMFLAGS:
        if ((Value & D3DTTFF_PROJECTED) != 0)
            Key.Modes |= 1u << (24 + Stage);
        else
            Key.Modes &= ~(1u << (24 + Stage));
        Count = Value & ~D3DTTFF_PROJECTED;
        Count = Count == 2 ? 1 : Count == 3 ? 2 : Count == 4 ? 3 : 0;
        *Byte = static_cast<BYTE>((*Byte & 0xCF) | (Count << 4));
        break;
    default:
        return;
    }
    Dirty = TRUE;
}

VOID
D9VertexConverter::SetTransform(
    _In_ DWORD Type,
    _In_ const D3DMATRIX* Matrix)
{
    if (Type == D3DTS_WORLDMATRIX(0))
    {
        Stale |= 0xC031111;
        Worlds[0] = *Matrix;
        return;
    }
    if (Type > D3DTS_WORLDMATRIX(0) && Type < D3DTS_WORLDMATRIX(0) + MatrixLimit)
    {
        UINT Index = Type - D3DTS_WORLDMATRIX(0);

        Worlds[Index] = *Matrix;
        WorldStale[Index] = TRUE;
        InverseStale[Index] = TRUE;
        Stale |= 0xC000000;
        if (Index < 4)
            Stale |= 0x1111u << Index;
        return;
    }
    if (Type == D3DTS_VIEW)
    {
        View = *Matrix;
        Stale |= 0x198FFFF;
        return;
    }
    if (Type == D3DTS_PROJECTION)
    {
        Projection = *Matrix;
        Stale |= 0x103FF0F;
        return;
    }
    if (Type >= D3DTS_TEXTURE0 && Type <= D3DTS_TEXTURE7)
        WriteMatrix(D9VsConstTexture + 4 * (Type - D3DTS_TEXTURE0), Matrix);
}

/* A format's parts and the float count of each coordinate set */
VOID
D9VertexConverter::SetFvf(
    _In_ DWORD Fvf)
{
    static const BYTE Floats[4] = { 2, 3, 4, 1 };
    DWORD In = (Key.Inputs & ~0x0001FFFFu) | 1;
    UINT Weights;
    UINT Sets = (Fvf >> 8) & 0xF;
    DWORD Sizes = Fvf >> 16;

    if ((Fvf & D3DFVF_NORMAL) != 0)
        In |= D9VsInNormal;
    if ((Fvf & D3DFVF_NORMAL) != 0 && (Fvf & 0x20) != 0)
        In |= 0x40;
    else if ((Fvf & 0x20) != 0)
        In |= 0x40;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
        In |= D9VsInDiffuse;
    if ((Fvf & D3DFVF_SPECULAR) != 0)
        In |= D9VsInSpecular;
    switch (Fvf & 0x400E)
    {
    case D3DFVF_XYZB1: Weights = 1; break;
    case D3DFVF_XYZB2: Weights = 2; break;
    case D3DFVF_XYZB3: Weights = 3; break;
    case D3DFVF_XYZB4: Weights = 4; break;
    case D3DFVF_XYZB5: Weights = 5; break;
    default: Weights = 0; break;
    }
    if ((Fvf & D3DFVF_LASTBETA_UBYTE4) != 0 && Weights != 0)
    {
        In |= D9VsInIndices;
        Weights -= 1;
    }
    if ((Fvf & D3DFVF_LASTBETA_UBYTE4) == 0 || Weights != 0 || (In & D9VsInIndices) != 0)
    {
        if (Weights != 0)
            In |= D9VsInWeights;
    }
    for (UINT Set = 0; Set < Sets; Set++)
    {
        Key.Stages[Set] = static_cast<BYTE>((Key.Stages[Set] & 0x3F) | ((Floats[Sizes & 3] - 1) << 6));
        Sizes >>= 2;
    }
    In = (In & ~0xFF00u) | ((((1u << Sets) - 1) & 0xFF) << 8);
    Key.Inputs = In;
    Dirty = TRUE;
}

/* A declaration's elements by usage; coordinate sets record their float count by usage index */
VOID
D9VertexConverter::SetElements(
    _In_ const D3DVERTEXELEMENT9* Elements)
{
    DWORD In = Key.Inputs & ~0x0001FFFFu;
    DWORD Sets = 0;

    for (UINT Index = 0; Index <= 1000 && Elements[Index].Stream != 0xFF; Index++)
    {
        const D3DVERTEXELEMENT9& Element = Elements[Index];
        UINT Usage = Element.UsageIndex;
        BYTE* Byte;

        switch (Element.Usage)
        {
        case D3DDECLUSAGE_POSITION:
            if (Usage == 0)
                In |= 1;
            else if (Usage == 1)
                In |= D9VsInPosition2;
            break;
        case D3DDECLUSAGE_NORMAL:
            if (Usage == 0)
                In |= D9VsInNormal;
            else if (Usage == 1)
                In |= D9VsInNormal2;
            break;
        case D3DDECLUSAGE_BLENDWEIGHT:
            In |= D9VsInWeights;
            break;
        case D3DDECLUSAGE_BLENDINDICES:
            In |= D9VsInIndices;
            break;
        case D3DDECLUSAGE_PSIZE:
            In |= D9VsInSize;
            break;
        case D3DDECLUSAGE_COLOR:
            if (Usage == 0)
                In |= D9VsInDiffuse;
            else if (Usage == 1)
                In |= D9VsInSpecular;
            break;
        case D3DDECLUSAGE_TEXCOORD:
            if (Usage >= D9VertexStages)
                break;
            Sets |= 1u << Usage;
            Byte = &Key.Stages[Usage];
            switch (Element.Type)
            {
            case D3DDECLTYPE_FLOAT1:
                *Byte = static_cast<BYTE>(*Byte & 0x3F);
                break;
            case D3DDECLTYPE_FLOAT2:
                *Byte = static_cast<BYTE>((*Byte & 0x3F) | 0x40);
                break;
            case D3DDECLTYPE_FLOAT3:
                *Byte = static_cast<BYTE>((*Byte & 0x3F) | 0x80);
                break;
            case D3DDECLTYPE_FLOAT4:
                *Byte = static_cast<BYTE>(*Byte | 0xC0);
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    Key.Inputs = (In & ~0xFF00u) | (Sets << 8);
    Dirty = TRUE;
}

VOID
D9VertexConverter::SetPointList(
    _In_ BOOLEAN Points)
{
    Key.Modes = (Key.Modes & ~D9VsModePointList) | (Points ? D9VsModePointList : 0);
    Dirty = TRUE;
}

/* The material's colors and its power, the power kept within what the shaders raise to */
VOID
D9VertexConverter::SetMaterial(
    _In_ const D3DMATERIAL9* Material)
{
    FLOAT Power = Material->Power;
    FLOAT Kept;

    CopyMemory(Registers[D9VsConstMaterial], &Material->Ambient, sizeof(Material->Ambient));
    CopyMemory(Registers[D9VsConstMaterial + 1], &Material->Emissive, sizeof(Material->Emissive));
    CopyMemory(Registers[D9VsConstMaterial + 2], &Material->Diffuse, sizeof(Material->Diffuse));
    CopyMemory(Registers[D9VsConstMaterial + 3], &Material->Specular, sizeof(Material->Specular));
    if (Power > 127.0f)
        Kept = 127.0f;
    else if (Power == 0.0f)
        Kept = 0.0001f;
    else if (Power < -127.0f)
        Kept = -127.0f;
    else
        Kept = Power;
    Registers[D9VsConstMaterial + 4][0] = Kept;
    Registers[D9VsConstMaterial + 4][1] = Power;
    Touch(1, D9VsConstMaterial, D9VsConstMaterial + 5);
}

/* Point scaling reads the shorter side of the viewport */
VOID
D9VertexConverter::SetViewportSize(
    _In_ DWORD Width,
    _In_ DWORD Height)
{
    Registers[D9VsConstPoint][3] = static_cast<FLOAT>(Height >= Width ? Width : Height);
    Touch(0, D9VsConstPoint, D9VsConstPoint + 1);
}

/* The outputs each draw needs follow from the inputs and states; then what the shader works out first */
BOOLEAN
D9VertexConverter::RefreshKey(VOID)
{
    DWORD In = (Key.Inputs & 0xFFC1FFFF) | 0x00020000;
    DWORD Modes = Key.Modes;
    DWORD Needs = 0;
    UINT Stages = 0;

    if ((In & D9VsLighting) != 0 || (In & D9VsInDiffuse) != 0)
        In |= D9VsOutDiffuse;
    if (((In & D9VsSpecularOn) != 0 && (In & D9VsLighting) != 0) || (In & D9VsInSpecular) != 0)
        In |= D9VsOutSpecular;
    if ((In & D9VsInSize) != 0 || (Modes & (D9VsModePointScale | D9VsModePointList)) ==
                                  (D9VsModePointScale | D9VsModePointList))
        In |= D9VsOutSize;
    if ((In & D9VsFog) != 0 &&
        ((Modes & D9VsModeFog) != 0 || ((Modes & D9VsModeTableFog) == 0 && (In & D9VsInSpecular) != 0)))
        In |= D9VsOutFog;

    while (Stages < D9VertexStages && (DisabledStages & (1u << Stages)) == 0)
        Stages += 1;
    Modes = (Modes & ~0x3C0u) | (Stages << 6);

    if ((Modes & (D9VsModePointScale | D9VsModePointList)) == (D9VsModePointScale | D9VsModePointList) ||
        ((In & D9VsFog) != 0 && (Modes & D9VsModeFog) != 0))
        Needs = D9VertexNeedsPosition;
    if ((In & D9VsLighting) != 0)
    {
        if ((Modes & 7) != 0)
            Needs |= D9VertexNeedsNormal;
        if (((Modes & 1) != 0 && (In & D9VsLocalViewer) != 0) || (Modes & 6) != 0)
            Needs |= D9VertexNeedsPosition;
    }
    if (Needs != (D9VertexNeedsPosition | D9VertexNeedsNormal))
    {
        for (UINT Stage = 0; Stage < Stages; Stage++)
        {
            switch (D9VsCoordinateSource(Key.Stages[Stage]))
            {
            case D3DTSS_TCI_CAMERASPACENORMAL:
                Needs |= D9VertexNeedsNormal;
                break;
            case D3DTSS_TCI_CAMERASPACEPOSITION:
                Needs |= D9VertexNeedsPosition;
                break;
            case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
            case D3DTSS_TCI_SPHEREMAP:
                Needs |= D9VertexNeedsNormal;
                if ((In & D9VsLocalViewer) != 0)
                    Needs |= D9VertexNeedsPosition;
                break;
            default:
                break;
            }
        }
    }
    if ((In & D9VsTweening) == 0 && (In & D9VsBlending) != 0)
    {
        if ((In & (D9VsIndexed | D9VsInWeights)) != 0)
            Needs |= D9VertexNeedsBlend;
        if ((In & D9VsIndexed) != 0)
            Needs |= D9VertexNeedsIndices;
    }

    Key.Inputs = In;
    Key.Modes = Modes;
    Key.Size = 12 + Stages;
    Key.Needs = Needs;
    Dirty = FALSE;
    return memcmp(&UsedKey, &Key, Key.Size) != 0;
}

BOOLEAN
D9VertexConverter::Find(
    _Out_ HANDLE* Shader)
{
    *Shader = NULL;
    for (UINT Index = 0; Index < CacheCount; Index++)
    {
        const D9_FIXED_VERTEX_ENTRY& Entry = Cache[(CacheFirst + Index) % D9VertexCacheLimit];

        if (Entry.Key.Size == Key.Size && memcmp(&Entry.Key, &Key, Key.Size) == 0 && Entry.Shader != NULL)
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
D9VertexConverter::Keep(
    _In_ HANDLE Shader,
    _Out_ HANDLE* Evicted)
{
    D9_FIXED_VERTEX_ENTRY* Entry;

    *Evicted = NULL;
    if (Cache == nullptr)
    {
        Cache = new (std::nothrow) D9_FIXED_VERTEX_ENTRY[D9VertexCacheLimit];
        if (Cache == nullptr)
            return FALSE;
    }
    if (CacheCount == D9VertexCacheLimit)
    {
        *Evicted = Cache[CacheFirst].Shader;
        CacheFirst = (CacheFirst + 1) % D9VertexCacheLimit;
        CacheCount -= 1;
    }
    Entry = &Cache[(CacheFirst + CacheCount) % D9VertexCacheLimit];
    Entry->Key = Key;
    Entry->Shader = Shader;
    CacheCount += 1;
    UsedKey = Key;
    UsedShader = Shader;
    return TRUE;
}

BOOLEAN
D9VertexConverter::TakeOldest(
    _Out_ HANDLE* Shader)
{
    *Shader = NULL;
    if (CacheCount == 0)
        return FALSE;
    *Shader = Cache[CacheFirst].Shader;
    CacheFirst = (CacheFirst + 1) % D9VertexCacheLimit;
    CacheCount -= 1;
    return TRUE;
}

/* Lights */

D9_VS_LIGHT*
D9VertexConverter::FindLight(
    _In_ DWORD Index)
{
    for (UINT Slot = 0; Slot < LightCount; Slot++)
    {
        if (Lights[Slot].Index == Index)
            return &Lights[Slot];
    }
    return nullptr;
}

/* A new light waits for the list of enabled lights; a change to an enabled one moves or recolors it */
BOOLEAN
D9VertexConverter::SetLight(
    _In_ DWORD Index,
    _In_ const D3DLIGHT9* Light)
{
    D9_VS_LIGHT* Kept = FindLight(Index);

    if (Kept == nullptr)
    {
        if (Lights == nullptr)
        {
            Lights = new (std::nothrow) D9_VS_LIGHT[D9VertexLightCapacity];
            if (Lights == nullptr)
                return FALSE;
        }
        if (LightCount == D9VertexLightCapacity)
            return FALSE;
        Kept = &Lights[LightCount];
        LightCount += 1;
        Kept->Index = Index;
        Kept->Enabled = FALSE;
        Kept->Light = *Light;
        Stale |= 0xC00000;
        Dirty = TRUE;
    }
    if (Kept->Enabled)
    {
        if (Light->Type != Kept->Light.Type)
        {
            Stale |= 0x400000;
            Dirty = TRUE;
        }
        if (Kept->Light.Direction.x != Light->Direction.x || Kept->Light.Direction.y != Light->Direction.y ||
            Kept->Light.Direction.z != Light->Direction.z || Kept->Light.Position.x != Light->Position.x ||
            Kept->Light.Position.y != Light->Position.y || Kept->Light.Position.z != Light->Position.z)
        {
            Stale |= 0x800000;
            Dirty = TRUE;
        }
        if ((Stale & 0x400000) == 0)
            Stale |= 0x2000000;
    }
    Kept->Light = *Light;
    return TRUE;
}

/* Enabling puts a light first among the enabled ones; a light never set starts as the default one */
BOOLEAN
D9VertexConverter::EnableLight(
    _In_ DWORD Index,
    _In_ BOOLEAN Enable)
{
    D9_VS_LIGHT* Kept = FindLight(Index);
    UINT Slot;

    if (Kept == nullptr)
    {
        D3DLIGHT9 Default = {};

        Default.Type = D3DLIGHT_DIRECTIONAL;
        Default.Diffuse.r = Default.Diffuse.g = Default.Diffuse.b = 1.0f;
        Default.Direction.z = 1.0f;
        if (!SetLight(Index, &Default))
            return FALSE;
        Kept = FindLight(Index);
    }
    if ((Kept->Enabled != FALSE) == (Enable != FALSE))
        return TRUE;

    Slot = static_cast<UINT>(Kept - Lights);
    for (UINT Place = 0; Place < EnabledCount; Place++)
    {
        if (Enabled[Place] != Slot)
            continue;
        MoveMemory(&Enabled[Place], &Enabled[Place + 1], (EnabledCount - Place - 1) * sizeof(Enabled[0]));
        EnabledCount -= 1;
        break;
    }
    if (Enable)
    {
        MoveMemory(&Enabled[1], &Enabled[0], EnabledCount * sizeof(Enabled[0]));
        Enabled[0] = Slot;
        EnabledCount += 1;
    }
    Kept->Enabled = Enable;
    Stale |= 0x400000;
    Dirty = TRUE;
    return TRUE;
}

/* Directional lights take the first registers, then point and spot lights, each kind looping by seven */
BOOLEAN
D9VertexConverter::GatherLights(VOID)
{
    UINT Counts[3] = {};
    UINT Taken[3] = {};

    Stale = (Stale | 0x2C00000) & ~0x400000u;
    Dirty = TRUE;
    Key.Modes &= ~0x7u;
    ActiveCount = 0;
    for (UINT Place = 0; Place < EnabledCount && ActiveCount < LightLimit; Place++)
    {
        const D9_VS_LIGHT* Light = &Lights[Enabled[Place]];

        Active[ActiveCount] = Light;
        ActiveCount += 1;
        if (Light->Light.Type == D3DLIGHT_DIRECTIONAL)
            Key.Modes |= 1;
        else if (Light->Light.Type == D3DLIGHT_POINT)
            Key.Modes |= 2;
        else
            Key.Modes |= 4;
    }
    for (UINT Index = 0; Index < ActiveCount; Index++)
    {
        D3DLIGHTTYPE Type = Active[Index]->Light.Type;

        Counts[Type == D3DLIGHT_DIRECTIONAL ? 0 : Type == D3DLIGHT_POINT ? 1 : 2] += 1;
    }
    for (UINT Index = 0; Index < ActiveCount; Index++)
    {
        D3DLIGHTTYPE Type = Active[Index]->Light.Type;
        UINT Kind = Type == D3DLIGHT_DIRECTIONAL ? 0 : Type == D3DLIGHT_POINT ? 1 : 2;
        UINT Before = Kind == 0 ? 0 : Kind == 1 ? Counts[0] : Counts[0] + Counts[1];

        ActiveSlots[Index] = Before + Taken[Kind];
        Taken[Kind] += 1;
    }
    for (UINT Kind = 0; Kind < 3; Kind++)
    {
        UINT Before = Kind == 0 ? 0 : Kind == 1 ? Counts[0] : Counts[0] + Counts[1];

        Loops[Kind][0] = static_cast<INT>(Counts[Kind]);
        Loops[Kind][1] = static_cast<INT>(7 * Before);
        Loops[Kind][2] = 7;
        Loops[Kind][3] = 0;
    }
    if (ActiveCount == 1)
    {
        Key.Modes |= 0x8;
        return FALSE;
    }
    Key.Modes &= ~0x8u;
    return TRUE;
}

/* A light's colors, attenuation and cone in its seven registers */
VOID
D9VertexConverter::WriteLight(
    _In_ UINT Slot,
    _In_ const D3DLIGHT9* Light)
{
    UINT Base = D9VertexFixedRegisters + 7 * Slot;
    FLOAT* Block = Registers[Base];

    CopyMemory(Block, &Light->Ambient, sizeof(Light->Ambient));
    CopyMemory(Block + 4, &Light->Diffuse, sizeof(Light->Diffuse));
    CopyMemory(Block + 8, &Light->Specular, sizeof(Light->Specular));
    Block[20] = Light->Attenuation0;
    Block[21] = Light->Attenuation1;
    Block[22] = Light->Attenuation2;
    Block[23] = Light->Falloff;
    Block[24] = cosf(Light->Theta * 0.5f);
    Block[25] = cosf(Light->Phi * 0.5f);
    Block[26] = Light->Range;
    Touch(1, Base, Base + 7);
}

/* Directions and positions move into eye space; directions keep unit length */
VOID
D9VertexConverter::TransformLights(VOID)
{
    for (UINT Index = 0; Index < ActiveCount; Index++)
    {
        const D3DLIGHT9* Light = &Active[Index]->Light;
        UINT Base = D9VertexFixedRegisters + 7 * ActiveSlots[Index];

        if (Light->Type == D3DLIGHT_SPOT || Light->Type == D3DLIGHT_DIRECTIONAL)
        {
            FLOAT* Direction = Registers[Base + 3];
            FLOAT X = (Light->Direction.x * View._11 + Light->Direction.y * View._21) + Light->Direction.z * View._31;
            FLOAT Y = (Light->Direction.x * View._12 + Light->Direction.y * View._22) + Light->Direction.z * View._32;
            FLOAT Z = (Light->Direction.x * View._13 + Light->Direction.y * View._23) + Light->Direction.z * View._33;
            FLOAT Length = sqrtf((X * X + Y * Y) + Z * Z);

            Direction[0] = X;
            Direction[1] = Y;
            Direction[2] = Z;
            Direction[3] = 1.0f;
            if (Length != 0.0f)
            {
                FLOAT Inverse = 1.0f / Length;

                Direction[0] = X * Inverse;
                Direction[1] = Y * Inverse;
                Direction[2] = Z * Inverse;
            }
            Touch(1, Base + 3, Base + 4);
        }
        if (Light->Type == D3DLIGHT_POINT || Light->Type == D3DLIGHT_SPOT)
        {
            FLOAT* Position = Registers[Base + 4];

            Position[0] = ((Light->Position.x * View._11 + Light->Position.y * View._21) +
                           Light->Position.z * View._31) + View._41;
            Position[1] = ((Light->Position.x * View._12 + Light->Position.y * View._22) +
                           Light->Position.z * View._32) + View._42;
            Position[2] = ((Light->Position.x * View._13 + Light->Position.y * View._23) +
                           Light->Position.z * View._33) + View._43;
            Position[3] = 1.0f;
            Touch(1, Base + 4, Base + 5);
        }
    }
}

/* One row of the product of two matrices, each sum taken in row order */
static
VOID
D9VsProductRow(
    _In_reads_(4) const FLOAT* Row,
    _In_ const D3DMATRIX* Right,
    _Out_writes_(4) FLOAT* Result)
{
    for (UINT Column = 0; Column < 4; Column++)
    {
        Result[Column] = ((Row[0] * Right->m[0][Column] + Row[1] * Right->m[1][Column]) +
                          Row[2] * Right->m[2][Column]) + Row[3] * Right->m[3][Column];
    }
}

/* The inverse the normals transform by, worked in doubles with each product and sum in the system's order; a
   matrix without one gives the identity */
static
VOID
D9VsInverse(
    _In_ const D3DMATRIX* Matrix,
    _Out_ D3DMATRIX* Inverse)
{
    const FLOAT* M = &Matrix->_11;
    FLOAT* R = &Inverse->_11;
    double A0 = M[0], A1 = M[1], A2 = M[2], A3 = M[3];
    double B0 = M[4], B1 = M[5], B2 = M[6], B3 = M[7];
    double C0 = M[8], C1 = M[9], C2 = M[10], C3 = M[11];
    double D0 = M[12], D1 = M[13], D2 = M[14], D3 = M[15];
    double P23 = B3 * A2 - B2 * A3;
    double Q23 = C3 * A2 - C2 * A3;
    double R23 = D3 * A2 - D2 * A3;
    double S23 = C3 * B2 - C2 * B3;
    double T23 = D3 * B2 - D2 * B3;
    double U23 = D3 * C2 - D2 * C3;
    double K0 = B1 * Q23 - C1 * P23 - A1 * S23;
    double K1 = A1 * T23 - B1 * R23 + D1 * P23;
    double K2 = C1 * R23 - D1 * Q23 - A1 * U23;
    double K3 = B1 * U23 - C1 * T23 + D1 * S23;
    double Det = D0 * K0 + C0 * K1 + B0 * K2 + A0 * K3;
    double P01;
    double Q01;
    double R01;
    double S01;
    double T01;
    double U01;
    double Scale;

    if (Det == 0.0)
    {
        ZeroMemory(Inverse, sizeof(*Inverse));
        Inverse->_11 = Inverse->_22 = Inverse->_33 = Inverse->_44 = 1.0f;
        return;
    }
    P01 = B1 * A0 - B0 * A1;
    Q01 = C1 * A0 - C0 * A1;
    R01 = D1 * A0 - D0 * A1;
    S01 = C1 * B0 - C0 * B1;
    T01 = D1 * B0 - D0 * B1;
    U01 = D1 * C0 - D0 * C1;
    Scale = 1.0 / Det;
    R[0] = static_cast<FLOAT>(Scale * K3);
    R[1] = static_cast<FLOAT>(Scale * K2);
    R[2] = static_cast<FLOAT>(Scale * K1);
    R[3] = static_cast<FLOAT>(K0 * Scale);
    R[4] = static_cast<FLOAT>((C0 * T23 - D0 * S23 - B0 * U23) * Scale);
    R[5] = static_cast<FLOAT>((A0 * U23 - C0 * R23 + D0 * Q23) * Scale);
    R[6] = static_cast<FLOAT>((B0 * R23 - D0 * P23 - A0 * T23) * Scale);
    R[7] = static_cast<FLOAT>((A0 * S23 - B0 * Q23 + C0 * P23) * Scale);
    R[8] = static_cast<FLOAT>((U01 * B3 - T01 * C3 + S01 * D3) * Scale);
    R[9] = static_cast<FLOAT>((R01 * C3 - Q01 * D3 - U01 * A3) * Scale);
    R[10] = static_cast<FLOAT>((T01 * A3 - R01 * B3 + P01 * D3) * Scale);
    R[11] = static_cast<FLOAT>((Q01 * B3 - P01 * C3 - S01 * A3) * Scale);
    R[12] = static_cast<FLOAT>((T01 * C2 - S01 * D2 - U01 * B2) * Scale);
    R[13] = static_cast<FLOAT>((U01 * A2 - R01 * C2 + Q01 * D2) * Scale);
    R[14] = static_cast<FLOAT>((R01 * B2 - P01 * D2 - T01 * A2) * Scale);
    R[15] = static_cast<FLOAT>((S01 * A2 - Q01 * B2 + P01 * C2) * Scale);
}

/* Indexed world matrices and their inverses live after the lights, eight registers each */
VOID
D9VertexConverter::WriteBlendMatrix(
    _In_ UINT Register,
    _In_ const D3DMATRIX* Matrix)
{
    if (Mark < Register + 4)
        Mark = Register + 4;
    Touch(2, Register, Register + 4);
    CopyMemory(Registers[Register], Matrix, sizeof(*Matrix));
}

/* The matrices blending reads: each blend matrix's products for plain blending, or the view, projection and
   indexed world matrices for indexed blending */
VOID
D9VertexConverter::UpdateBlendRegisters(
    _Inout_ DWORD* Flags,
    _In_ UINT Last)
{
    UINT Base = D9VertexFixedRegisters + 7 * LightLimit;
    D3DMATRIX Inverse;

    if ((Key.Needs & D9VertexNeedsIndices) != 0)
    {
        if ((*Flags & 0x80000) != 0)
        {
            WriteMatrix(D9VsConstView, &View);
            *Flags &= ~0x80000u;
        }
        if ((*Flags & 0x100000) != 0 && (Key.Needs & D9VertexNeedsNormal) != 0)
        {
            D9VsInverse(&View, &Inverse);
            WriteMatrix(D9VsConstViewInverse, &Inverse);
            *Flags &= ~0x100000u;
        }
        if ((*Flags & 0x40000) != 0)
        {
            WriteMatrix(D9VsConstProjection, &Projection);
            *Flags &= ~0x40000u;
        }
        if ((*Flags & 0x10000) != 0)
        {
            WriteBlendMatrix(Base, &Worlds[0]);
            *Flags &= ~0x10000u;
        }
        for (UINT Index = 1; Index < 256; Index++)
        {
            if (!WorldStale[Index])
                continue;
            if (Index < MatrixLimit)
                WriteBlendMatrix(Base + 8 * Index, &Worlds[Index]);
            WorldStale[Index] = FALSE;
        }
        *Flags &= ~0x4000000u;

        /* The first world matrix's inverse is written again each time */
        if ((Key.Needs & D9VertexNeedsNormal) != 0)
        {
            if ((*Flags & 0x20000) != 0)
            {
                D9VsInverse(&Worlds[0], &Inverse);
                WriteBlendMatrix(Base + 4, &Inverse);
            }
            for (UINT Index = 1; Index < 256; Index++)
            {
                if (!InverseStale[Index])
                    continue;
                D9VsInverse(&Worlds[Index], &Inverse);
                if (Index < MatrixLimit)
                    WriteBlendMatrix(Base + 8 * Index + 4, &Inverse);
                InverseStale[Index] = FALSE;
            }
            *Flags &= ~0x8000000u;
        }
        return;
    }

    for (UINT Index = 0; Index <= Last; Index++)
    {
        if ((*Flags & (0x10u << Index)) != 0)
        {
            for (UINT Row = 0; Row < 4; Row++)
                D9VsProductRow(Worlds[Index].m[Row], &View, WorldViews[Index].m[Row]);
            *Flags &= ~(0x10u << Index);
        }
        if ((*Flags & (0x1u << Index)) != 0)
        {
            D3DMATRIX Product;

            for (UINT Row = 0; Row < 4; Row++)
                D9VsProductRow(WorldViews[Index].m[Row], &Projection, Product.m[Row]);
            WriteMatrix(D9VsBlendRegister(D9VsConstWorldViewProj, 62, Index), &Product);
            *Flags &= ~(0x1u << Index);
        }
        if ((Key.Needs & D9VertexNeedsPosition) != 0 && (*Flags & (0x1000u << Index)) != 0)
        {
            WriteMatrix(D9VsBlendRegister(D9VsConstWorldView, 74, Index), &WorldViews[Index]);
            *Flags &= ~(0x1000u << Index);
        }
        if ((Key.Needs & D9VertexNeedsNormal) != 0 && (*Flags & (0x100u << Index)) != 0)
        {
            D9VsInverse(&WorldViews[Index], &Inverse);
            WriteMatrix(D9VsBlendRegister(D9VsConstNormal, 86, Index), &Inverse);
            *Flags &= ~(0x100u << Index);
        }
    }
}

VOID
D9VertexConverter::UpdateRegisters(VOID)
{
    DWORD Flags = Stale;

    if ((Flags & 0x200000) != 0)
    {
        FLOAT Scale = 0.0f;
        FLOAT Offset = 0.0f;

        if (FogStart != FogEnd)
        {
            Scale = -1.0f / (FogEnd - FogStart);
            Offset = -FogEnd * Scale;
        }
        Registers[D9VsConstFog][0] = Scale;
        Registers[D9VsConstFog][1] = Offset;
        if (Mark < D9VsConstFog + 1)
            Mark = D9VsConstFog + 1;
        Touch(0, D9VsConstFog, D9VsConstFog + 1);
        Flags &= ~0x200000u;
    }

    /* Plain draws take the first blend matrix's registers */
    UpdateBlendRegisters(&Flags, (Key.Needs & D9VertexNeedsBlend) != 0 ? (Key.Modes >> 18) & 3 : 0);
    if ((Flags & 0x800000) != 0)
    {
        TransformLights();
        Flags &= ~0x800000u;
    }
    if ((Flags & 0x2000000) != 0)
    {
        for (UINT Index = 0; Index < ActiveCount; Index++)
            WriteLight(ActiveSlots[Index], &Active[Index]->Light);
        Flags &= ~0x2000000u;
    }
    Updated = Flags;
    Stale = Flags;
}

BOOLEAN
D9VertexConverter::TakeRange(
    _Out_ UINT* First,
    _Out_ UINT* Count)
{
    static const UINT Order[3] = { 0, 2, 1 };

    for (UINT Index : Order)
    {
        D9_VERTEX_RANGE* Range = &Ranges[Index];

        if (Range->First >= Range->End)
            continue;
        *First = Range->First;
        *Count = Range->End - Range->First;
        if (Mark < Range->End)
            Mark = Range->End;
        Range->First = 0xFFFFFFFF;
        Range->End = 0;
        return TRUE;
    }
    return FALSE;
}
