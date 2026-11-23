/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex shaders standing in for the fixed function vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "ffvertex.hpp"

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
    D9_VS_SOURCE Source = { File, Swizzle, static_cast<USHORT>(Number), FALSE };

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
            (Source.Negate ? 0x01000000 : 0));
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First)
    {
        Put(Opcode | (2u << 24));
        Target(Out);
        Source(First);
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First,
        _In_ const D9_VS_SOURCE& Second)
    {
        Put(Opcode | (3u << 24));
        Target(Out);
        Source(First);
        Source(Second);
    }

    VOID
    Op(
        _In_ DWORD Opcode,
        _In_ const D9_VS_TARGET& Out,
        _In_ const D9_VS_SOURCE& First,
        _In_ const D9_VS_SOURCE& Second,
        _In_ const D9_VS_SOURCE& Third)
    {
        Put(Opcode | (4u << 24));
        Target(Out);
        Source(First);
        Source(Second);
        Source(Third);
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
    BOOLEAN Overflow = FALSE;
};

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
    D9_VS_SOURCE Sets[D9VertexStages];
    DWORD Writes = D9VsWritesPosition;
    UINT Stages;

    UNREFERENCED_PARAMETER(FlowControl);
    *Count = 0;

    /* Parts not converted yet */
    if ((In & (D9VsTweening | D9VsLighting)) != 0 || (Needs & D9VertexNeedsBlend) != 0)
        return FALSE;
    if ((Modes & D9VsModePointList) != 0 && (In & D9VsOutSize) != 0)
        return FALSE;
    if ((In & (D9VsFog | D9VsOutFog)) == (D9VsFog | D9VsOutFog))
        return FALSE;

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
            Size = D9VsSrc(D9VsInput, Inputs.Take(&Inputs.Size), D9VsAll);
        }
        else
        {
            Size = D9VsConstant(D9VsConstPoint, 2);
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

    /* The eye space position and normal the later parts read, then the clip space position */
    if ((Needs & D9VertexNeedsPosition) != 0)
        D9VsTransform(&Out, D9VsDst(D9VsTemp, D9VsTempEye, 0xF), Position, D9VsConstWorldView, D9VsTempEye);
    if (HasNormal)
        Out.Op(D9VsM3x3, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), Normal, D9VsWhole(D9VsConstNormal));
    D9VsTransform(&Out, D9VsDst(D9VsRasterOut, 0, 0xF), Position, D9VsConstWorldViewProj, D9VsTempScratch);
    if (HasNormal && (In & D9VsNormalize) != 0)
    {
        Out.Op(D9VsNrm, D9VsDst(D9VsTemp, D9VsTempScratch, 0x7), D9VsSrc(D9VsTemp, D9VsTempNormal, D9VsAll));
        Out.Op(D9VsMov, D9VsDst(D9VsTemp, D9VsTempNormal, 0x7), D9VsSrc(D9VsTemp, D9VsTempScratch, D9VsAll));
    }

    /* Without lighting the vertex colors pass through */
    if (DiffuseOut)
        Out.Op(D9VsMov, D9VsDst(D9VsColorOut, 0, 0xF), Diffuse);
    if (SpecularOut)
        Out.Op(D9VsMov, D9VsDst(D9VsColorOut, 1, 0xF), Specular);

    if ((Modes & D9VsModePointList) != 0 && (In & D9VsOutSize) != 0)
    {
        Writes |= D9VsWritesSize;
        Out.Op(D9VsMov, D9VsDst(D9VsRasterOut, 2, 0xF), Size);
    }

    /* Each active stage gets the coordinates it reads in the output of its own number */
    Stages = (Modes >> 6) & 0xF;
    Writes |= Stages << 8;
    for (UINT Stage = 0; Stage < Stages; Stage++)
    {
        DWORD Source = D9VsCoordinateSource(Key->Stages[Stage]);
        DWORD Transform = (Key->Stages[Stage] >> 4) & 3;
        BOOLEAN Divide = ((Modes >> 24) & (1u << Stage)) != 0;
        D9_VS_SOURCE From;
        UINT Floats;

        if ((Source & 0xFFFF0000) != 0 || Transform != 0 || Divide)
            return FALSE;
        UNREFERENCED_PARAMETER(Projected);
        From = Sets[Source & 7];
        Floats = Key->Stages[Source & 7] >> 6;
        Out.Op(D9VsMov, D9VsFirst(D9VsDst(D9VsTexcoordOut, Stage, 0xF), Floats + 1), From);
        if (Floats == 0)
            Writes |= 3u << (2 * Stage + 16);
        else if (Floats == 2)
            Writes |= 1u << (2 * Stage + 16);
        else if (Floats == 3)
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
    World = View;
    ZeroMemory(&WorldView, sizeof(WorldView));

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
        World = *Matrix;
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

    if ((Key.Needs & D9VertexNeedsBlend) == 0)
    {
        if ((Flags & 0x10) != 0)
        {
            for (UINT Row = 0; Row < 4; Row++)
                D9VsProductRow(World.m[Row], &View, WorldView.m[Row]);
            Flags &= ~0x10u;
        }
        if ((Flags & 0x1) != 0)
        {
            D3DMATRIX Product;

            for (UINT Row = 0; Row < 4; Row++)
                D9VsProductRow(WorldView.m[Row], &Projection, Product.m[Row]);
            if (Mark < D9VsConstWorldViewProj + 4)
                Mark = D9VsConstWorldViewProj + 4;
            Touch(0, D9VsConstWorldViewProj, D9VsConstWorldViewProj + 4);
            CopyMemory(Registers[D9VsConstWorldViewProj], &Product, sizeof(Product));
            Flags &= ~0x1u;
        }
        if ((Key.Needs & D9VertexNeedsPosition) != 0 && (Flags & 0x1000) != 0)
        {
            WriteMatrix(D9VsConstWorldView, &WorldView);
            Flags &= ~0x1000u;
        }
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
