/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader converter: the geometry shader for point sprites, point fill, flat shading and wrapping
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

/* What the geometry shader does; the low two bits are the vertices per primitive */
static constexpr UINT ScGsVertexMask = 0x3;
static constexpr UINT ScGsPointFill = 0x4;
static constexpr UINT ScGsFlat = 0x8;
static constexpr UINT ScGsSprite = 0x10;
static constexpr UINT ScGsSpriteCoords = 0x20;
static constexpr UINT ScGsWrap = 0x40;
static constexpr UINT ScGsUnscaledSize = 0x80;
static constexpr UINT ScGsClipPlanes = 0x3F00;

/* Raster state words the shader is chosen from */
static constexpr UINT ScGsFillMask = 0xC000;
static constexpr UINT ScGsFillPoint = 0x4000;
static constexpr UINT ScGsShadeMask = 0x30000;
static constexpr UINT ScGsShadeFlat = 0x10000;
static constexpr UINT ScGsPrimitiveMask = 0x1C0000;
static constexpr UINT ScGsPrimitivePoints = 0x40000;
static constexpr UINT ScGsPrimitiveLines = 0x80000;
static constexpr UINT ScGsPrimitiveLineStrip = 0xC0000;
static constexpr UINT ScGsPointFillEnabled = 0x100000;
static constexpr UINT ScGsPointScale = 0x40000000;
static constexpr UINT ScGsPointSpriteEnable = 0x80000000;

/* Vertex output flags: a point size is written, texture coordinates are written */
static constexpr UINT ScGsOutputsPointSize = 0x100000;
static constexpr UINT ScGsOutputsTexcoords = 0x7FFF8;

static constexpr UINT ScGsUsagePosition = 0;
static constexpr UINT ScGsUsagePointSize = 4;
static constexpr UINT ScGsUsageTexcoord = 5;
static constexpr UINT ScGsUsagePositionT = 9;
static constexpr UINT ScGsUsageColor = 10;
static constexpr UINT ScGsUsageClip = 16;
static constexpr UINT ScGsUsageSpriteCoord = 17;

/* The sprite coordinate output register and the first temp of the vertex copies */
static constexpr UINT ScGsSpriteCoordRegister = 12;
static constexpr BYTE ScGsCopyTemp = 8;

/* Corner offsets of a sprite and its texture coordinates, in strip order */
static const float ScGsCorners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
static const float ScGsCornersMirrored[4][2] = { { -1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f } };
static const float ScGsSpriteCoords4[4][2] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };

/* One vertex shader output as the geometry shader reads it: register and four bit mask */
struct ScGsRegister
{
    UINT Register;
    UINT Mask;
};

static
ScGsRegister
ScGsRegisterOf(
    _In_ NineScOutput Output)
{
    ScGsRegister Result;

    Result.Register = (Output >> 9) & 0xF;
    Result.Mask = (Output >> 13) & 0xF;
    return Result;
}

/* Everything the shader is built from */
struct ScGsShader
{
    UINT Flags;
    UINT Options;
    NineScOutputs Inputs;
    NineScOutputs Outputs;
    BYTE Wrap[16];
};

static
VOID
ScGsDescribe(
    _In_ const NineScGsArgs* Args,
    _Out_ ScGsShader* Shader)
{
    const NineScOutputs* Vertex = Args->VertexOutputs;
    UINT Words1 = Args->Raster->Words[1];
    UINT Primitive = Words1 & ScGsPrimitiveMask;
    UINT Flags = 0;
    BOOL Expand;

    ZeroMemory(Shader, sizeof(*Shader));
    Shader->Options = Args->Flags;

    if ((Words1 & ScGsFillMask) == ScGsFillPoint && (Words1 & ScGsPointFillEnabled))
    {
        Flags = ScGsPointFill;
        if (Args->Version < 9 && (Words1 & ScGsShadeMask) == ScGsShadeFlat)
            Flags |= ScGsFlat;
        Expand = TRUE;
    }
    else
    {
        Expand = Primitive == ScGsPrimitivePoints;
    }

    if (Expand)
    {
        if (Words1 & ScGsPointSpriteEnable)
            Flags |= ScGsSpriteCoords;
        if ((Flags & (ScGsPointFill | ScGsSpriteCoords)) || (Vertex->Flags & ScGsOutputsPointSize) ||
            (Words1 & ScGsPointScale))
        {
            Flags |= ScGsSprite;
        }
    }

    if ((Vertex->Flags & ScGsOutputsTexcoords) && !(Flags & ScGsSpriteCoords) && Primitive != ScGsPrimitivePoints)
    {
        UINT Coords = (Vertex->Flags >> 3) & 0xFFFF;

        for (UINT Index = 0; Coords != 0; Index += 1, Coords >>= 1)
        {
            BYTE Sampler = Args->Raster->Samplers[Index];

            if ((Coords & 1) && (Sampler & 0x3C))
            {
                Shader->Wrap[Index] = (Sampler >> 2) & 0xF;
                Flags |= ScGsWrap;
            }
        }
    }

    if (Flags)
    {
        UINT Vertices;

        if (Primitive == ScGsPrimitivePoints)
            Vertices = 1;
        else if (Primitive == ScGsPrimitiveLines || Primitive == ScGsPrimitiveLineStrip)
            Vertices = 2;
        else
            Vertices = 3;

        Flags = (Flags & ~ScGsVertexMask & 0x7F) | Vertices;
        Shader->Flags = (Words1 & ScGsClipPlanes) | (((Args->Raster->Words[2] >> 10) & 1) << 7) | Flags;
        Shader->Inputs.Flags = Vertex->Flags;
        Shader->Inputs.Indexed = Vertex->Indexed & ((1ull << 45) - 1);
        memcpy(Shader->Inputs.Outputs, Vertex->Outputs, Vertex->Count * sizeof(NineScOutput));
        Shader->Inputs.Count = Vertex->Count;
        Shader->Outputs = Shader->Inputs;
    }

    if (Words1 & ScGsPointSpriteEnable)
        ScAddOutput(&Shader->Outputs, ScGsUsageSpriteCoord, 0, ScGsSpriteCoordRegister, 0xF0000, FALSE);
}

/* Writes the shader; vertex registers copied to a temp are read from there afterwards */
class ScGsWriter
{
public:
    ScGsWriter(
        _In_ ScAsm* Writer,
        _In_ const ScGsShader* Info);

    HRESULT
    WriteDeclarations();

    VOID
    WriteOutputs();

private:
    ScAsm* Asm;
    const ScGsShader* Shader;
    UINT Vertices;
    NineScOutput Position;
    NineScOutput PointSize;

    /*
     * Temps holding vertex registers, twelve per vertex. The byte after them is the temp a copy takes,
     * so a register past eleven on the last vertex reads it.
     */
    BYTE Copies[40];

    SB::COperandBase
    Input(
        _In_ UINT Vertex,
        _In_ const ScGsRegister& Source,
        _In_ UINT Swizzle) const;

    SB::COperandBase
    CopyDst(
        _In_ UINT Vertex,
        _In_ UINT Register,
        _In_ UINT Mask);

    VOID
    Wrap(
        _In_ UINT Vertex,
        _In_ UINT Coordinate,
        _In_ const ScGsRegister& Source);

    VOID
    Facing();

    VOID
    Sprites();

    VOID
    Primitives();
};

ScGsWriter::ScGsWriter(
    _In_ ScAsm* Writer,
    _In_ const ScGsShader* Info) :
    Asm(Writer),
    Shader(Info),
    Vertices(Info->Flags & ScGsVertexMask),
    Position(0),
    PointSize(0)
{
    for (UINT Slot = 0; Slot < Info->Inputs.Count; Slot += 1)
    {
        NineScOutput Output = Info->Inputs.Outputs[Slot];
        UINT Usage = Output & 0x1F;
        BOOL First = (Output & 0x1E0) == 0;

        if ((Usage == ScGsUsagePosition || Usage == ScGsUsagePositionT) && First)
            Position = Output;
        else if (Usage == ScGsUsagePointSize && First)
            PointSize = Output;
    }

    memset(Copies, ScNoRegister, sizeof(Copies));
    Copies[36] = ScGsCopyTemp;
}

SB::COperandBase
ScGsWriter::Input(
    _In_ UINT Vertex,
    _In_ const ScGsRegister& Source,
    _In_ UINT Swizzle) const
{
    BYTE Copy = Copies[12 * Vertex + Source.Register];
    SB::COperandBase Operand;

    if (!Swizzle)
        Swizzle = ScSwizzleFromMask(Source.Mask << 4);
    if (Copy == ScNoRegister)
        Operand = SB::COperand2D(D3D10_SB_OPERAND_TYPE_INPUT, Vertex, Source.Register);
    else
        Operand = ScTempOperand(Copy);
    ScSetSwizzle(&Operand, Swizzle);
    return Operand;
}

SB::COperandBase
ScGsWriter::CopyDst(
    _In_ UINT Vertex,
    _In_ UINT Register,
    _In_ UINT Mask)
{
    BYTE* Copy = &Copies[12 * Vertex + Register];

    if (*Copy == ScNoRegister)
        *Copy = Copies[36];
    return ScTempDst(*Copy, Mask);
}

HRESULT
ScGsWriter::WriteDeclarations()
{
    BOOL Sprite = (Shader->Flags & ScGsSprite) != 0;
    D3D10_SB_PRIMITIVE Primitive;
    D3D10_SB_PRIMITIVE_TOPOLOGY Topology;

    if (Vertices == 1)
    {
        Primitive = D3D10_SB_PRIMITIVE_POINT;
        Topology = D3D10_SB_PRIMITIVE_TOPOLOGY_POINTLIST;
    }
    else if (Vertices == 2)
    {
        Primitive = D3D10_SB_PRIMITIVE_LINE;
        Topology = D3D10_SB_PRIMITIVE_TOPOLOGY_LINESTRIP;
    }
    else
    {
        Primitive = D3D10_SB_PRIMITIVE_TRIANGLE;
        Topology = D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    }
    if (Sprite)
        Topology = D3D10_SB_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    Asm->EmitGSMaxOutputVertexCountDecl(Sprite ? 4 * Vertices : Vertices);
    Asm->EmitTempsDecl(Copies[36] + Shader->Outputs.Count);
    Asm->EmitConstantBufferDecl(3, 10, D3D10_SB_CONSTANT_BUFFER_IMMEDIATE_INDEXED);
    Asm->EmitGSInputPrimitiveDecl(Primitive);
    Asm->EmitGSOutputTopologyDecl(Topology);

    for (UINT Slot = 0; Slot < Shader->Outputs.Count; Slot += 1)
    {
        NineScOutput Output = Shader->Outputs.Outputs[Slot];
        UINT Usage = Output & 0x1F;
        BOOL First = (Output & 0x1E0) == 0;
        UINT Register = (Output >> 9) & 0xF;
        UINT Mask = ((Output >> 13) & 0xF) << 4;

        if (Usage == ScGsUsagePositionT && First)
            return E_FAIL;

        if (Usage == ScGsUsagePosition && First)
        {
            Asm->EmitInputSystemInterpretedValueDecl2D(Vertices, Register, Mask, D3D10_SB_NAME_POSITION);
            Asm->EmitOutputSystemInterpretedValueDecl(Register, Mask, D3D10_SB_NAME_POSITION);
        }
        else if (Usage == ScGsUsageClip)
        {
            Asm->EmitInputSystemInterpretedValueDecl2D(Vertices, Register, Mask, D3D10_SB_NAME_CLIP_DISTANCE);
            Asm->EmitOutputSystemInterpretedValueDecl(Register, Mask, D3D10_SB_NAME_CLIP_DISTANCE);
        }
        else if (Usage == ScGsUsageSpriteCoord)
        {
            Asm->EmitOutputDecl(Register, Mask);
        }
        else
        {
            Asm->EmitInputDecl2D(D3D10_SB_OPERAND_TYPE_INPUT, Vertices, Register, Mask);
            if (Usage != ScGsUsagePointSize)
                Asm->EmitOutputDecl(Register, Mask);
        }
    }
    return S_OK;
}

/* Wraps texture coordinates across the primitive: each one moves to within half of vertex 0's */
VOID
ScGsWriter::Wrap(
    _In_ UINT Vertex,
    _In_ UINT Coordinate,
    _In_ const ScGsRegister& Source)
{
    UINT Written = Source.Mask << 4;
    UINT Modes = Shader->Wrap[Coordinate];
    UINT Mask = 0;
    UINT Swizzle = 0;

    if ((Written & ScMaskX) && (Modes & 1))
    {
        Mask = ScMaskX;
        Swizzle = ScSwz(0, 0, 0, 0);
    }
    if ((Written & ScMaskY) && (Modes & 2))
    {
        Mask |= ScMaskY;
        Swizzle = ScSwz(0, 1, 0, 0);
    }
    if ((Written & ScMaskZ) && (Modes & 4))
    {
        Mask |= ScMaskZ;
        Swizzle |= ScSwz(0, 0, 2, 0);
    }
    if ((Written & ScMaskW) && (Modes & 8))
    {
        Mask |= ScMaskW;
        Swizzle |= ScSwz(0, 0, 0, 3);
    }

    if (Mask)
    {
        SB::COperandBase Base = Input(0, Source, Swizzle);
        SB::COperandBase Back = ScNegated(Base);
        SB::COperandBase Here = Input(Vertex, Source, Swizzle);
        SB::COperandBase Delta = ScSwizzled(ScTempOperand(0), Swizzle);
        SB::COperandBase Up = ScSwizzled(ScTempOperand(1), Swizzle);
        SB::COperandBase Down = ScSwizzled(ScTempOperand(2), Swizzle);
        SB::COperandBase Below = ScSwizzled(ScTempOperand(3), Swizzle);
        SB::COperandBase DeltaSize = ScAbsolute(Delta);
        SB::COperandBase UpSize = ScAbsolute(Up);
        SB::COperand One(1.0f);
        SB::COperand MinusOne(-1.0f);
        SB::COperand Zero(0.0f);
        SB::COperandBase DeltaDst = ScTempDst(0, Mask);
        SB::COperandBase UpDst = ScTempDst(1, Mask);
        SB::COperandBase DownDst = ScTempDst(2, Mask);
        SB::COperandBase BelowDst = ScTempDst(3, Mask);
        SB::COperandBase Result = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Source.Register, Mask);

        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &DeltaDst, &Here, &Back);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &UpDst, &Delta, &One);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &DownDst, &Delta, &MinusOne);
        ScEmit(Asm, D3D10_SB_OPCODE_LT, 3, &BelowDst, &Delta, &Zero);
        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &UpDst, &Below, &Up, &Down);
        ScEmit(Asm, D3D10_SB_OPCODE_LT, 3, &BelowDst, &DeltaSize, &UpSize);
        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &DeltaDst, &Below, &Delta, &Up);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &Result, &Delta, &Base);
    }
    if (Written != Mask)
    {
        SB::COperandBase Value = Input(Vertex, Source, 0);
        SB::COperandBase Rest = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Source.Register, Written ^ Mask);

        ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Rest, &Value);
    }
}

/*
 * For point fill: r0.x is set when the triangle's signed area is negative, and r4 to r7 get the
 * sprite corner offsets in the order that keeps the strip's winding.
 */
VOID
ScGsWriter::Facing()
{
    ScGsRegister Corner = ScGsRegisterOf(Position);
    SB::COperandBase R0X = ScSwizzled(ScTempOperand(0), ScSwzXXXX);
    SB::COperandBase R0Y = ScSwizzled(ScTempOperand(0), ScSwzYYYY);
    SB::COperandBase R0Z = ScSwizzled(ScTempOperand(0), ScSwzZZZZ);
    SB::COperandBase R0W = ScSwizzled(ScTempOperand(0), ScSwzWWWW);
    SB::COperandBase R0XDst = ScTempDst(0, ScMaskX);
    SB::COperandBase R0YDst = ScTempDst(0, ScMaskY);
    SB::COperandBase R0ZDst = ScTempDst(0, ScMaskZ);
    SB::COperandBase R0WDst = ScTempDst(0, ScMaskW);
    SB::COperand Zero(0.0f);

    if (Shader->Flags & ScGsUnscaledSize)
    {
        /* Screen space: area from the edge vectors */
        SB::COperandBase X0 = ScNegated(Input(0, Corner, ScSwzXXXX));
        SB::COperandBase Y0 = ScNegated(Input(0, Corner, ScSwzYYYY));
        SB::COperandBase X1 = Input(1, Corner, ScSwzXXXX);
        SB::COperandBase Y1 = Input(1, Corner, ScSwzYYYY);
        SB::COperandBase X2 = Input(2, Corner, ScSwzXXXX);
        SB::COperandBase Y2 = Input(2, Corner, ScSwzYYYY);
        SB::COperandBase NegZ = ScNegated(R0Z);

        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R0XDst, &X1, &X0);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R0YDst, &Y2, &Y0);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0ZDst, &R0X, &R0Y);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R0XDst, &X2, &X0);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R0YDst, &Y1, &Y0);
        ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &R0XDst, &R0X, &R0Y, &NegZ);
    }
    else
    {
        /* Clip space: the determinant of x, y and w over the three vertices */
        SB::COperandBase X[3];
        SB::COperandBase Y[3];
        SB::COperandBase W[3];
        SB::COperandBase R1X = ScSwizzled(ScTempOperand(1), ScSwzXXXX);
        SB::COperandBase R1Y = ScSwizzled(ScTempOperand(1), ScSwzYYYY);
        SB::COperandBase R1Z = ScSwizzled(ScTempOperand(1), ScSwzZZZZ);
        SB::COperandBase R2X = ScSwizzled(ScTempOperand(2), ScSwzXXXX);
        SB::COperandBase R2Y = ScSwizzled(ScTempOperand(2), ScSwzYYYY);
        SB::COperandBase R2Z = ScSwizzled(ScTempOperand(2), ScSwzZZZZ);
        SB::COperandBase R1XDst = ScTempDst(1, ScMaskX);
        SB::COperandBase R1YDst = ScTempDst(1, ScMaskY);
        SB::COperandBase R1ZDst = ScTempDst(1, ScMaskZ);
        SB::COperandBase R2XDst = ScTempDst(2, ScMaskX);
        SB::COperandBase R2YDst = ScTempDst(2, ScMaskY);
        SB::COperandBase R2ZDst = ScTempDst(2, ScMaskZ);
        SB::COperandBase NegR0Y = ScNegated(R0Y);
        SB::COperandBase NegR1Y = ScNegated(R1Y);
        SB::COperandBase NegR2Y = ScNegated(R2Y);
        SB::COperandBase NegR0W = ScNegated(R0W);

        for (UINT Vertex = 0; Vertex < 3; Vertex += 1)
        {
            X[Vertex] = Input(Vertex, Corner, ScSwzXXXX);
            Y[Vertex] = Input(Vertex, Corner, ScSwzYYYY);
            W[Vertex] = Input(Vertex, Corner, ScSwzWWWW);
        }

        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0XDst, &X[1], &Y[2]);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0YDst, &X[2], &Y[1]);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R1XDst, &X[0], &Y[2]);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R1YDst, &X[2], &Y[0]);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R2XDst, &X[0], &Y[1]);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R2YDst, &X[1], &Y[0]);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R0ZDst, &R0X, &NegR0Y);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R1ZDst, &R1X, &NegR1Y);
        ScEmit(Asm, D3D10_SB_OPCODE_ADD, 3, &R2ZDst, &R2X, &NegR2Y);
        ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0WDst, &W[1], &R1Z);
        ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &R0WDst, &W[0], &R0Z, &NegR0W);
        ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &R0XDst, &W[2], &R2Z, &R0W);
    }

    ScEmit(Asm, D3D10_SB_OPCODE_LT, 3, &R0XDst, &R0X, &Zero);
    for (UINT Corner4 = 0; Corner4 < 4; Corner4 += 1)
    {
        SB::COperand Negative(ScGsCorners[Corner4][0], ScGsCorners[Corner4][1], 0.0f, 0.0f);
        SB::COperand Positive(ScGsCornersMirrored[Corner4][0], ScGsCornersMirrored[Corner4][1], 0.0f, 0.0f);
        SB::COperandBase Dst = ScTempDst(4 + Corner4, ScMaskX | ScMaskY);

        ScEmit(Asm, D3D10_SB_OPCODE_MOVC, 4, &Dst, &R0X, &Negative, &Positive);
    }
}

/* Each vertex becomes a strip of four corners around it */
VOID
ScGsWriter::Sprites()
{
    ScGsRegister Corner = ScGsRegisterOf(Position);
    ScGsRegister Size = ScGsRegisterOf(PointSize);
    UINT SizeSwizzle = ScSwizzleFromMask(Size.Mask << 4);
    UINT Planes = (Shader->Flags >> 8) & 0x3F;
    SB::COperandBase R0X = ScSwizzled(ScTempOperand(0), ScSwzXXXX);
    SB::COperandBase R0XY = ScSwizzled(ScTempOperand(0), ScSwzXYYY);
    SB::COperandBase R0XDst = ScTempDst(0, ScMaskX);
    SB::COperandBase R0XYDst = ScTempDst(0, ScMaskX | ScMaskY);

    if (Shader->Flags & ScGsPointFill)
        Facing();

    for (UINT Vertex = 0; Vertex < Vertices; Vertex += 1)
    {
        SB::COperandBase Limits = SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 1);

        /* r0.x is the point size, clamped when the vertex writes it */
        if (Shader->Outputs.Flags & ScGsOutputsPointSize)
        {
            SB::COperandBase Written = Input(Vertex, Size, SizeSwizzle);
            SB::COperandBase Smallest = ScSwizzled(Limits, ScSwzYYYY);
            SB::COperandBase Largest = ScSwizzled(Limits, ScSwzZZZZ);

            ScEmit(Asm, D3D10_SB_OPCODE_MAX, 3, &R0XDst, &Written, &Smallest);
            ScEmit(Asm, D3D10_SB_OPCODE_MIN, 3, &R0XDst, &R0X, &Largest);
        }
        else
        {
            SB::COperandBase Fixed = ScSwizzled(Limits, ScSwzXXXX);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &R0XDst, &Fixed);
        }

        /* r0.xy becomes the half extent, in clip space unless the size is already in screen space */
        if (Shader->Flags & ScGsUnscaledSize)
        {
            SB::COperand Half(0.5f);

            ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0XYDst, &R0X, &Half);
        }
        else
        {
            SB::COperandBase W = Input(Vertex, Corner, ScSwzWWWW);
            SB::COperandBase Pixel = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_CONSTANT_BUFFER, 3, 0), ScSwzXYYY);

            ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0XDst, &R0X, &W);
            ScEmit(Asm, D3D10_SB_OPCODE_MUL, 3, &R0XYDst, &R0X, &Pixel);
        }

        for (UINT Corner4 = 0; Corner4 < 4; Corner4 += 1)
        {
            SB::COperandBase Center = ScSwizzled(SB::COperand2D(D3D10_SB_OPERAND_TYPE_INPUT, Vertex, Corner.Register),
                                                 ScSwzXYYY);
            SB::COperandBase Extent = ScSwizzled(ScTempOperand(0), ScSwzXYYY);
            SB::COperandBase Moved = CopyDst(Vertex, Corner.Register, ScMaskX | ScMaskY);
            SB::COperandBase Rest = SB::COperand2D(D3D10_SB_OPERAND_TYPE_INPUT, Vertex, Corner.Register);

            if (Shader->Flags & ScGsPointFill)
            {
                SB::COperandBase Offset = ScSwizzled(ScTempOperand(4 + Corner4), ScSwzXYYY);

                ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Moved, &Extent, &Offset, &Center);
            }
            else
            {
                SB::COperand Offset(ScGsCorners[Corner4][0], ScGsCorners[Corner4][1], 0.0f, 0.0f);

                ScEmit(Asm, D3D10_SB_OPCODE_MAD, 4, &Moved, &Extent, &Offset, &Center);
            }

            SB::COperandBase Depth = CopyDst(Vertex, Corner.Register, ScMaskZ | ScMaskW);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Depth, &Rest);
            if (Shader->Flags & ScGsClipPlanes)
                ScWriteClipPlanes(Asm, Planes, Input(Vertex, Corner, 0));

            for (UINT Slot = 0; Slot < Shader->Outputs.Count; Slot += 1)
            {
                NineScOutput Output = Shader->Outputs.Outputs[Slot];
                UINT Usage = Output & 0x1F;
                ScGsRegister Source = ScGsRegisterOf(Output);
                SB::COperandBase Dst = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Source.Register, Source.Mask << 4);

                if ((Shader->Flags & ScGsFlat) && Usage == ScGsUsageColor)
                {
                    SB::COperandBase Value = Input(0, Source, 0);

                    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
                }
                else if (Usage == ScGsUsageSpriteCoord)
                {
                    SB::COperand Value(ScGsSpriteCoords4[Corner4][0], ScGsSpriteCoords4[Corner4][1], 0.0f, 1.0f);
                    SB::COperandBase Coord = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, ScGsSpriteCoordRegister,
                                                             ScMaskAll);

                    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Coord, &Value);
                }
                else if (Usage != ScGsUsageClip && Usage != ScGsUsagePointSize)
                {
                    SB::COperandBase Value = Input(Vertex, Source, 0);

                    ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
                }
            }
            Asm->AddBare(D3D10_SB_OPCODE_EMIT);
        }
        Asm->AddBare(D3D10_SB_OPCODE_CUT);
    }
}

/* Each vertex is passed on, with clip distances and wrapped coordinates */
VOID
ScGsWriter::Primitives()
{
    ScGsRegister Corner = ScGsRegisterOf(Position);
    UINT Planes = (Shader->Flags >> 8) & 0x3F;

    for (UINT Vertex = 0; Vertex < Vertices; Vertex += 1)
    {
        if (Shader->Flags & ScGsClipPlanes)
            ScWriteClipPlanes(Asm, Planes, Input(Vertex, Corner, 0));

        for (UINT Slot = 0; Slot < Shader->Outputs.Count; Slot += 1)
        {
            NineScOutput Output = Shader->Outputs.Outputs[Slot];
            UINT Usage = Output & 0x1F;
            ScGsRegister Source = ScGsRegisterOf(Output);

            if (Usage == ScGsUsagePointSize)
                continue;
            if (Usage == ScGsUsageTexcoord && (Shader->Flags & ScGsWrap) && Vertex != 0)
            {
                Wrap(Vertex, (Output >> 5) & 0xF, Source);
                continue;
            }

            SB::COperandBase Dst = SB::COperandDst(D3D10_SB_OPERAND_TYPE_OUTPUT, Source.Register, Source.Mask << 4);
            SB::COperandBase Value = Input(Vertex, Source, 0);

            ScEmit(Asm, D3D10_SB_OPCODE_MOV, 2, &Dst, &Value);
        }
        Asm->AddBare(D3D10_SB_OPCODE_EMIT);
    }
}

VOID
ScGsWriter::WriteOutputs()
{
    if (Shader->Flags & ScGsSprite)
        Sprites();
    else
        Primitives();
}

HRESULT
NineScCreateGeometryShader(
    _In_ NineScTranslator* Translator,
    _Inout_ NineScGsArgs* Args)
{
    ScGsShader* Shader;
    ScGsWriter* Writer;
    ScAsm* Asm = Translator->Asm;
    HRESULT hr;

    Shader = new (std::nothrow) ScGsShader();
    if (!Shader)
        return E_OUTOFMEMORY;
    ScGsDescribe(Args, Shader);
    if (!Shader->Flags)
    {
        delete Shader;
        return E_INVALIDARG;
    }

    Writer = new (std::nothrow) ScGsWriter(Asm, Shader);
    if (!Writer)
    {
        delete Shader;
        return E_OUTOFMEMORY;
    }

    Asm->Begin(D3D10_SB_GEOMETRY_SHADER, Shader->Options, 4);
    hr = Writer->WriteDeclarations();
    if (SUCCEEDED(hr))
    {
        Writer->WriteOutputs();
        Asm->EndShader();

        Args->TokenSize = Asm->ShaderSizeInDWORDs() * sizeof(DWORD);
        Args->Tokens = malloc(Args->TokenSize);
        if (!Args->Tokens)
        {
            Args->TokenSize = 0;
            hr = E_OUTOFMEMORY;
        }
        else
        {
            NineScOutputs* Outputs = Args->Outputs;

            memcpy(Args->Tokens, Asm->GetShader(), Args->TokenSize);
            Outputs->Flags = Shader->Outputs.Flags;
            Outputs->Indexed = (Outputs->Indexed & ~((1ull << 45) - 1)) | Shader->Outputs.Indexed;
            memcpy(Outputs->Outputs, Shader->Outputs.Outputs, Shader->Outputs.Count * sizeof(NineScOutput));
            Outputs->Count = Shader->Outputs.Count;
        }
    }

    delete Writer;
    delete Shader;
    return hr;
}
