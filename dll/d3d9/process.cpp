/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ProcessVertices through the software vertex pipeline
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Interpreter registers of every device; the system's software bank is this wide whatever the device */
static constexpr UINT D9PvFloatRegisters = 8192;
static constexpr UINT D9PvIntRegisters = 2048;

/* The reciprocal of w the screen transform takes when w is zero */
static constexpr DWORD D9PvZeroWReciprocal = 0x4A000000;

/* The left, right, top and bottom outcodes, which a guard band lets the rasterizer clip */
static constexpr DWORD D9PvSidePlanes = 0xF;

/* D3DPV_DONOTCOPYDATA */
static constexpr DWORD D9PvKeepData = 0x1;

/* The converter's stale light list, and the vertex fog part of the format its shaders write */
static constexpr DWORD D9PvLightsStale = 0x400000;
static constexpr DWORD D9PvWritesFog = 0x2000;

/* The lock flags of the caller's the destination lock passes on */
static constexpr DWORD D9PvLockFlags = D3DLOCK_READONLY | D3DLOCK_NOOVERWRITE | D3DLOCK_DISCARD |
                                       D3DLOCK_NO_DIRTY_UPDATE;

/* Where each part of a vertex in a flexible vertex format destination goes */
struct D9PvLayout
{
    UINT Stride;
    UINT PointSize;
    UINT Diffuse;
    UINT Specular;
    UINT Fog;
    UINT Coordinates[8];
    UINT CoordinateSizes[8];
    UINT CoordinateCount;
};

/* One shader output a declaration destination takes */
struct D9PvTarget
{
    UINT Register;
    UINT Mask;
    UINT Offset;
    BYTE Type;
    BYTE Usage;
    BYTE Index;
};

/* One pass of the pipeline over a range of vertices: the program, where its inputs come from and how its
   outputs are laid out */
struct D9PvRun
{
    D9VmProgram* Program;
    D9VertexMachine* Vm;
    D9PvLayout Layout;
    const D9PvTarget* Targets;
    UINT TargetCount;
    DWORD Parts;
    DWORD Position;
    UINT Stride;
    BOOLEAN Keep;
    BOOLEAN Clipping;
    BOOLEAN Drawing;
    BOOLEAN ClampColors;
    FLOAT Scale[3];
    FLOAT Offset[3];
    const BYTE* Streams[D9StreamCount];
    UINT Strides[D9StreamCount];
    FLOAT Planes[8][4];
    UINT PlaneCount;
    BOOLEAN GuardBand;
    FLOAT BandScale[2];
    FLOAT BandOffset[2];
    DWORD ClipUnion;
    DWORD ClipIntersection;
};

static
VOID
D9PvMakeLayout(
    _In_ DWORD Fvf,
    _Out_ D9PvLayout* Layout)
{
    static const BYTE PositionBytes[] = { 0, 12, 16, 16, 20, 24, 28, 32 };
    static const BYTE CoordinateBytes[] = { 8, 12, 16, 4 };
    UINT Offset = PositionBytes[(Fvf & D3DFVF_XYZB5) >> 1];

    ZeroMemory(Layout, sizeof(*Layout));
    if ((Fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZW)
        Offset = 16;

    /* The point size lands right after the position, where a normal would sit, as on the system */
    Layout->PointSize = Offset;
    if ((Fvf & D3DFVF_NORMAL) != 0)
        Offset += 12;
    if ((Fvf & D3DFVF_PSIZE) != 0)
        Offset += 4;
    Layout->Diffuse = Offset;
    if ((Fvf & D3DFVF_DIFFUSE) != 0)
        Offset += 4;
    Layout->Specular = Offset;
    if ((Fvf & D3DFVF_SPECULAR) != 0)
        Offset += 4;
    Layout->Fog = Offset;
    if ((Fvf & D9VmFogFormat) != 0)
        Offset += 4;
    Layout->CoordinateCount = (Fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (Layout->CoordinateCount > 8)
        Layout->CoordinateCount = 8;
    for (UINT Set = 0; Set < Layout->CoordinateCount; Set++)
    {
        Layout->Coordinates[Set] = Offset;
        Layout->CoordinateSizes[Set] = CoordinateBytes[(Fvf >> (16 + Set * 2)) & 3];
        Offset += Layout->CoordinateSizes[Set];
    }
    Layout->Stride = Offset;
}

/* Bytes a declaration's vertex takes: the end of its furthest element */
static
UINT
D9PvDeclarationStride(
    _In_ const D9Declaration* Declaration)
{
    static const BYTE TypeBytes[] = { 4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8 };
    UINT Stride = 0;

    for (UINT Index = 0; Index < Declaration->ElementCount; Index++)
    {
        const D3DVERTEXELEMENT9* Element = &Declaration->Elements[Index];
        UINT End = Element->Offset + (Element->Type < _countof(TypeBytes) ? TypeBytes[Element->Type] : 0);

        if (End > Stride)
            Stride = End;
    }
    return Stride;
}

static
const D3DVERTEXELEMENT9*
D9PvFindElement(
    _In_ const D9Declaration* Declaration,
    _In_ UINT Usage,
    _In_ UINT Index)
{
    for (UINT Entry = 0; Entry < Declaration->ElementCount; Entry++)
    {
        const D3DVERTEXELEMENT9* Element = &Declaration->Elements[Entry];

        if (Element->Usage == Usage && Element->UsageIndex == Index)
            return Element;
    }
    return nullptr;
}

/* A color channel from a float, rounded the way the system packs vertex colors */
static
DWORD
D9PvChannel(
    _In_ FLOAT Value)
{
    FLOAT Scaled;

    if (Value < 0.0f)
        return 0;
    if (Value > 1.0f)
        return 255;
    Scaled = Value * 255.0f;
    if (Scaled >= 0.0f)
        return static_cast<DWORD>(static_cast<INT>(Scaled + 0.5f));

    /* Only NaN gets here, which the system's conversion turns into zero */
    return 0;
}

static
DWORD
D9PvPackColor(
    _In_reads_(4) const D9VmValue* Color)
{
    return (D9PvChannel(Color[3].Float) << 24) | (D9PvChannel(Color[0].Float) << 16) |
           (D9PvChannel(Color[1].Float) << 8) | D9PvChannel(Color[2].Float);
}

/* A float from its bits */
static
FLOAT
D9PvBits(
    _In_ DWORD Bits)
{
    FLOAT Value;

    CopyMemory(&Value, &Bits, sizeof(Value));
    return Value;
}

/* A half float widened the way the system does it: the largest exponent stays a number */
static
DWORD
D9PvWidenHalf(
    _In_ WORD Half)
{
    DWORD Sign = static_cast<DWORD>(Half & 0x8000) << 16;
    DWORD Exponent = (Half >> 10) & 0x1F;
    DWORD Fraction = Half & 0x3FF;
    INT Scale = -14;

    if (Exponent != 0)
        return Sign | ((Exponent + 112) << 23) | (Fraction << 13);
    if (Fraction == 0)
        return Sign;

    /* Denormals shift up until the leading bit reaches the hidden position */
    while ((Fraction & 0x400) == 0)
    {
        Fraction <<= 1;
        Scale -= 1;
    }
    return Sign | (static_cast<DWORD>(Scale + 127) << 23) | ((Fraction & 0x3FF) << 13);
}

/* Reads one element of a vertex into the components of an input register its declaration names, in order;
   names past what the type holds read 0, or 1 for w */
static
VOID
D9PvFetch(
    _In_ const BYTE* Data,
    _In_ BYTE Type,
    _In_ UINT Mask,
    _Inout_updates_(4) D9VmValue* Register)
{
    static const DWORD UnitByte = 0x3B808081;
    static const DWORD UnitShort = 0x38000100;
    static const DWORD UnitWord = 0x37800080;
    static const DWORD UnitTen = 0x3B004020;
    DWORD Values[4] = {};
    UINT Held = 0;
    UINT Taken = 0;

    switch (Type)
    {
    case D3DDECLTYPE_FLOAT1:
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_FLOAT3:
    case D3DDECLTYPE_FLOAT4:
        Held = Type + 1;
        CopyMemory(Values, Data, Held * sizeof(*Values));
        break;
    case D3DDECLTYPE_D3DCOLOR:
    {
        static const BYTE Order[4] = { 2, 1, 0, 3 };

        Held = 4;
        for (UINT Part = 0; Part < 4; Part++)
        {
            FLOAT Value = static_cast<FLOAT>(Data[Order[Part]]) * D9PvBits(UnitByte);

            CopyMemory(&Values[Part], &Value, sizeof(Value));
        }
        break;
    }
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4N:
        Held = 4;
        for (UINT Part = 0; Part < 4; Part++)
        {
            FLOAT Value = static_cast<FLOAT>(Data[Part]);

            if (Type == D3DDECLTYPE_UBYTE4N)
                Value *= D9PvBits(UnitByte);
            CopyMemory(&Values[Part], &Value, sizeof(Value));
        }
        break;
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT4:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_SHORT4N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_USHORT4N:
        Held = (Type == D3DDECLTYPE_SHORT2 || Type == D3DDECLTYPE_SHORT2N || Type == D3DDECLTYPE_USHORT2N) ? 2 : 4;
        for (UINT Part = 0; Part < Held; Part++)
        {
            WORD Raw;
            FLOAT Value;

            CopyMemory(&Raw, Data + Part * sizeof(Raw), sizeof(Raw));
            if (Type == D3DDECLTYPE_USHORT2N || Type == D3DDECLTYPE_USHORT4N)
                Value = static_cast<FLOAT>(Raw) * D9PvBits(UnitWord);
            else
                Value = static_cast<FLOAT>(static_cast<SHORT>(Raw));
            if (Type == D3DDECLTYPE_SHORT2N || Type == D3DDECLTYPE_SHORT4N)
                Value *= D9PvBits(UnitShort);
            CopyMemory(&Values[Part], &Value, sizeof(Value));
        }
        break;
    case D3DDECLTYPE_UDEC3:
    case D3DDECLTYPE_DEC3N:
    {
        DWORD Packed;

        CopyMemory(&Packed, Data, sizeof(Packed));
        Held = 3;
        for (UINT Part = 0; Part < 3; Part++)
        {
            DWORD Field = (Packed >> (Part * 10)) & 0x3FF;
            FLOAT Value;

            if (Type == D3DDECLTYPE_UDEC3)
            {
                Value = static_cast<FLOAT>(Field);
            }
            else
            {
                INT Signed = (Field & 0x200) != 0 ? static_cast<INT>(Field) - 0x400 : static_cast<INT>(Field);

                Value = static_cast<FLOAT>(Signed) * D9PvBits(UnitTen);
            }
            CopyMemory(&Values[Part], &Value, sizeof(Value));
        }
        break;
    }
    case D3DDECLTYPE_FLOAT16_2:
    case D3DDECLTYPE_FLOAT16_4:
        Held = Type == D3DDECLTYPE_FLOAT16_2 ? 2 : 4;
        for (UINT Part = 0; Part < Held; Part++)
        {
            WORD Raw;

            CopyMemory(&Raw, Data + Part * sizeof(Raw), sizeof(Raw));
            Values[Part] = D9PvWidenHalf(Raw);
        }
        break;
    default:
        return;
    }

    for (UINT Part = 0; Part < 4; Part++)
    {
        if ((Mask & (1u << Part)) == 0)
            continue;
        if (Taken < Held)
            Register[Part].Bits = Values[Taken];
        else
            Register[Part].Bits = Part == 3 ? 0x3F800000 : 0;
        Taken += 1;
    }
}

/* Whether any of the six frustum tests of a position in the half space form fails, by sign bit like the
   system tests them */
static
BOOLEAN
D9PvOutside(
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Z,
    _In_ FLOAT W)
{
    const FLOAT Tests[] = { X, W - X, Y, W - Y, Z, W - Z };
    DWORD Signs = 0;

    for (FLOAT Test : Tests)
    {
        DWORD Bits;

        CopyMemory(&Bits, &Test, sizeof(Bits));
        Signs |= Bits;
    }
    return (Signs & 0x80000000) != 0;
}

static
FLOAT
D9PvReciprocal(
    _In_ FLOAT W)
{
    FLOAT Large;

    if (W != 0.0f)
        return 1.0f / W;
    CopyMemory(&Large, &D9PvZeroWReciprocal, sizeof(Large));
    return Large;
}

/* The frustum outcodes of a position in the half space form, from the sign bits of the six tests like the
   system takes them, then a bit for each enabled user clip plane the position is behind */
static
DWORD
D9PvClipCode(
    _In_ const D9PvRun* Run,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT Z,
    _In_ FLOAT W)
{
    const FLOAT Tests[] = { X, W - X, W - Y, Y, Z, W - Z };
    DWORD Code = 0;

    for (UINT Index = 0; Index < _countof(Tests); Index++)
    {
        DWORD Bits;

        CopyMemory(&Bits, &Tests[Index], sizeof(Bits));
        Code |= (Bits >> 31) << Index;
    }
    for (UINT Plane = 0; Plane < Run->PlaneCount; Plane++)
    {
        const FLOAT* Given = Run->Planes[Plane];

        if ((((Given[0] * X) + (Given[1] * Y)) + (Given[2] * Z)) + (Given[3] * W) < 0.0f)
            Code |= D3DCS_PLANE0 << Plane;
    }
    return Code;
}

/* The transformed vertex a version 3 shader's declared outputs make, packed in the order they are declared:
   the first position becomes the transformed one, a first point size takes one float and every other output
   as many floats as its highest written component */
static
UINT
D9PvModelThreeLayout(
    _In_ const D9VmProgram* Program,
    _Out_writes_(D9VmDeclaredLimit + 1) D3DVERTEXELEMENT9* Elements,
    _Out_writes_(D9VmDeclaredLimit) D9PvTarget* Targets,
    _Out_ UINT* Stride)
{
    static const D3DVERTEXELEMENT9 End = D3DDECL_END();
    UINT Offset = 0;
    UINT Count = 0;

    for (UINT Index = 0; Index < Program->OutputCount; Index++)
    {
        const D9VmDeclared* Declared = &Program->Outputs[Index];
        BYTE Usage = Declared->Usage & 0xF;
        UINT Width = (Declared->Mask & 8) != 0 ? 4 : (Declared->Mask & 4) != 0 ? 3 : (Declared->Mask & 2) != 0 ? 2 :
                     (Declared->Mask & 1) != 0 ? 1 : 0;
        D3DVERTEXELEMENT9* Element = &Elements[Count];
        D9PvTarget* Target = &Targets[Count];

        if (Usage == D3DDECLUSAGE_PSIZE && Declared->Index == 0)
            Width = 1;
        else if (Usage == D3DDECLUSAGE_POSITION && Declared->Index == 0)
            Usage = D3DDECLUSAGE_POSITIONT;
        if (Width == 0)
            Width = 1;
        Element->Stream = 0;
        Element->Offset = static_cast<WORD>(Offset);
        Element->Type = static_cast<BYTE>(D3DDECLTYPE_FLOAT1 + Width - 1);
        Element->Method = D3DDECLMETHOD_DEFAULT;
        Element->Usage = Usage;
        Element->UsageIndex = Declared->Index;
        Target->Register = Declared->Register;
        Target->Mask = (1u << Width) - 1;
        Target->Offset = Offset;
        Target->Type = Element->Type;
        Target->Usage = Usage;
        Target->Index = Declared->Index;
        Offset += Width * sizeof(FLOAT);
        Count += 1;
    }
    Elements[Count] = End;
    *Stride = Offset;
    return Count;
}

/* The guard band outcodes of a position the frustum test put outside, by sign bits like the frustum's */
static
DWORD
D9PvBandCode(
    _In_ const D9PvRun* Run,
    _In_ FLOAT X,
    _In_ FLOAT Y,
    _In_ FLOAT W)
{
    const FLOAT Across = (Run->BandOffset[0] * W) + (Run->BandScale[0] * X);
    const FLOAT Down = (Run->BandOffset[1] * W) + (Run->BandScale[1] * Y);
    const FLOAT Tests[] = { Across, W - Across, W - Down, Down };
    DWORD Code = 0;

    for (UINT Index = 0; Index < _countof(Tests); Index++)
    {
        DWORD Bits;

        CopyMemory(&Bits, &Tests[Index], sizeof(Bits));
        Code |= (Bits >> 31) << (12 + Index);
    }
    return Code;
}

/* Runs the shader over vertices of the bound streams and writes what it outputs where the run's layout or
   declaration puts it; a draw keeps clipped positions in clip space and collects their outcodes */
static
HRESULT
D9PvRunVertices(
    _Inout_ D9PvRun* Run,
    _In_opt_ const D9Declaration* Input,
    _In_ UINT First,
    _In_ UINT Count,
    _Out_writes_bytes_(Count * Run->Stride) BYTE* Vertex,
    _Out_writes_opt_(Count) WORD* Codes)
{
    D9VmProgram* Program = Run->Program;
    const D9PvLayout* Layout = &Run->Layout;
    D9VmRegisters* Registers = &Run->Vm->Registers;

    for (UINT Number = 0; Number < Count; Number++, Vertex += Run->Stride)
    {
        for (UINT Index = 0; Index < Program->InputCount; Index++)
        {
            const D9VmDeclared* Declared = &Program->Inputs[Index];
            const D3DVERTEXELEMENT9* Element = nullptr;
            const BYTE* Data;

            if (Input != nullptr)
                Element = D9PvFindElement(Input, Declared->Usage, Declared->Index);
            if (Element == nullptr)
            {
                if (Program->Version >= D3DVS_VERSION(2, 0))
                    continue;
                D9Trace(L"shader input usage %u index %u is not in the declaration", Declared->Usage,
                        Declared->Index);
                return D3DERR_INVALIDCALL;
            }
            if (Run->Streams[Element->Stream] == nullptr || Declared->Register >= D9VmInputCount)
                continue;
            Data = Run->Streams[Element->Stream] + static_cast<SIZE_T>(Run->Strides[Element->Stream]) *
                                                   (First + Number) + Element->Offset;
            D9PvFetch(Data, Element->Type, Declared->Mask, Registers->Inputs[Declared->Register]);
        }
        Run->Vm->Run(Program);

        if (Program->Version >= D3DVS_VERSION(3, 0))
        {
            UINT Outputs = Run->Targets != nullptr ? Run->TargetCount : Program->OutputCount;

            for (UINT Index = 0; Index < Outputs; Index++)
            {
                const D9PvTarget* Target = Run->Targets != nullptr ? &Run->Targets[Index] : nullptr;
                UINT Register = Target != nullptr ? Target->Register : Program->Outputs[Index].Register;
                UINT Usage = Target != nullptr ? Target->Usage : Program->Outputs[Index].Usage;
                UINT UsageIndex = Target != nullptr ? Target->Index : Program->Outputs[Index].Index;
                const D9VmValue* Raw = Registers->Outputs[Register < D9VmOutputCount ? Register : 0];
                D9VmValue Values[4];
                BOOLEAN IsPosition = Target != nullptr ? (Usage == D3DDECLUSAGE_POSITIONT && UsageIndex == 0) :
                                                         (Usage == D3DDECLUSAGE_POSITION && UsageIndex == 0);

                CopyMemory(Values, Raw, sizeof(Values));
                if (IsPosition)
                {
                    FLOAT W = Raw[3].Float;
                    FLOAT HalfX = (W + Raw[0].Float) * 0.5f;
                    FLOAT HalfY = (W + Raw[1].Float) * 0.5f;
                    FLOAT Reciprocal = D9PvReciprocal(W);
                    DWORD Code = 0;

                    /* A draw keeps what the clipper takes in clip space */
                    if (Run->Drawing && Run->Clipping)
                    {
                        Code = D9PvClipCode(Run, HalfX, HalfY, Raw[2].Float, W);
                        if (Code != 0 && Run->GuardBand)
                            Code |= D9PvBandCode(Run, HalfX, HalfY, W);
                        Codes[Number] = static_cast<WORD>(Code);
                        Run->ClipIntersection &= Code;
                        Run->ClipUnion |= Code;
                        if (Run->GuardBand)
                            Code &= ~D9PvSidePlanes;
                    }
                    if (Code != 0)
                    {
                        Values[0].Float = HalfX;
                        Values[1].Float = HalfY;
                    }
                    else
                    {
                        Values[0].Float = ((Reciprocal * HalfX) * Run->Scale[0]) + Run->Offset[0];
                        Values[1].Float = ((Reciprocal * HalfY) * Run->Scale[1]) + Run->Offset[1];
                        Values[2].Float = ((Raw[2].Float * Reciprocal) * Run->Scale[2]) + Run->Offset[2];
                        Values[3].Float = Reciprocal;
                    }
                }
                else if (Run->ClampColors && Usage == D3DDECLUSAGE_COLOR && UsageIndex < 2)
                {
                    /* Colors reach the fixed function pixel shaders within the unit range */
                    for (UINT Part = 0; Part < 4; Part++)
                    {
                        FLOAT Value = Values[Part].Float <= 1.0f ? Values[Part].Float : 1.0f;

                        Values[Part].Float = Value < 0.0f ? 0.0f : Value;
                    }
                }

                if (Target != nullptr)
                {
                    BYTE* Field = Vertex + Target->Offset;

                    if (Target->Type == D3DDECLTYPE_D3DCOLOR)
                    {
                        DWORD Packed = D9PvPackColor(Values);

                        CopyMemory(Field, &Packed, sizeof(Packed));
                        continue;
                    }
                    for (UINT Part = 0; Part < 4; Part++)
                    {
                        if ((Target->Mask & (1 << Part)) == 0)
                            continue;
                        CopyMemory(Field, &Values[Part], sizeof(Values[Part]));
                        Field += sizeof(Values[Part]);
                    }
                    continue;
                }

                switch (Usage)
                {
                case D3DDECLUSAGE_PSIZE:
                    CopyMemory(Vertex + Layout->PointSize, &Raw[0], sizeof(Raw[0]));
                    break;
                case D3DDECLUSAGE_TEXCOORD:
                    if (UsageIndex < Layout->CoordinateCount)
                        CopyMemory(Vertex + Layout->Coordinates[UsageIndex], Raw, Layout->CoordinateSizes[UsageIndex]);
                    break;
                case D3DDECLUSAGE_POSITION:
                    if (UsageIndex != 0)
                        break;
                    if (Run->Position == D3DFVF_XYZRHW)
                        CopyMemory(Vertex, Values, 4 * sizeof(*Values));
                    else if (Run->Position == D3DFVF_XYZ)
                        CopyMemory(Vertex, Raw, 3 * sizeof(*Raw));
                    else if (Run->Position == D3DFVF_XYZW)
                        CopyMemory(Vertex, Raw, 4 * sizeof(*Raw));
                    break;
                case D3DDECLUSAGE_COLOR:
                {
                    DWORD Packed = D9PvPackColor(Raw);

                    CopyMemory(Vertex + (UsageIndex != 0 ? Layout->Specular : Layout->Diffuse), &Packed,
                               sizeof(Packed));
                    break;
                }
                case D3DDECLUSAGE_FOG:
                    Vertex[Layout->Specular + 3] = static_cast<BYTE>(D9PvChannel(Raw[0].Float));
                    break;
                default:
                    break;
                }
            }
            continue;
        }

        /* Version 1 and 2 shaders: the rasterizer, color and coordinate outputs by the destination format */
        {
            const D9VmValue* Raw = Registers->Position;
            DWORD Written = Program->Written;
            DWORD Parts = Run->Parts;
            BOOLEAN Specular;
            BOOLEAN OwnFog = (Parts & D9VmFogFormat) != 0 && (Written & D9VmWritesFog) != 0;

            if (Run->Position == D3DFVF_XYZ || Run->Position == D3DFVF_XYZW)
            {
                CopyMemory(Vertex, Raw, (Run->Position == D3DFVF_XYZW ? 4 : 3) * sizeof(*Raw));
            }
            else
            {
                FLOAT W = Raw[3].Float;
                FLOAT Z = Raw[2].Float;
                FLOAT SumX = Raw[0].Float + W;
                FLOAT SumY = Raw[1].Float + W;
                FLOAT Reciprocal;
                FLOAT Screen[4];
                BOOLEAN InClipSpace = FALSE;

                if (Run->Drawing)
                {
                    /* A draw tests every vertex unless clipping is off; one the clipper takes stays in clip space */
                    if (!Run->Clipping)
                    {
                        Reciprocal = D9PvReciprocal(W);
                    }
                    else
                    {
                        DWORD Code = D9PvClipCode(Run, SumX * 0.5f, SumY * 0.5f, Z, W);

                        /* Inside the guard band only the side planes failing, the rasterizer clips it */
                        if (Code != 0 && Run->GuardBand)
                            Code |= D9PvBandCode(Run, SumX * 0.5f, SumY * 0.5f, W);
                        Codes[Number] = static_cast<WORD>(Code);
                        Run->ClipIntersection &= Code;
                        Run->ClipUnion |= Code;
                        InClipSpace = Run->GuardBand ? (Code & ~D9PvSidePlanes) != 0 : Code != 0;
                        Reciprocal = 1.0f / W;
                    }
                }
                else if (Run->Clipping && !D9PvOutside(SumX * 0.5f, SumY * 0.5f, Z, W))
                {
                    /* Inside the frustum the system divides without looking at w */
                    Reciprocal = 1.0f / W;
                }
                else
                {
                    Reciprocal = D9PvReciprocal(W);
                }
                if (InClipSpace)
                {
                    Screen[0] = SumX * 0.5f;
                    Screen[1] = SumY * 0.5f;
                    Screen[2] = Z;
                    Screen[3] = W;
                }
                else
                {
                    Screen[0] = (((SumX * 0.5f) * Reciprocal) * Run->Scale[0]) + Run->Offset[0];
                    Screen[1] = (((SumY * 0.5f) * Reciprocal) * Run->Scale[1]) + Run->Offset[1];
                    Screen[2] = ((Run->Scale[2] * Z) * Reciprocal) + Run->Offset[2];
                    Screen[3] = Reciprocal;
                }
                CopyMemory(Vertex, Screen, sizeof(Screen));
            }
            if ((Written & D9VmWritesPointSize) != 0 && (Parts & D3DFVF_PSIZE) != 0)
                CopyMemory(Vertex + Layout->PointSize, &Registers->PointSize[0], sizeof(Registers->PointSize[0]));
            if ((Parts & D3DFVF_DIFFUSE) != 0)
            {
                DWORD Packed = 0xFFFFFFFF;

                if ((Written & D9VmWritesDiffuse) != 0)
                    Packed = D9PvPackColor(Registers->Colors[0]);
                if ((Written & D9VmWritesDiffuse) != 0 || !Run->Keep)
                    CopyMemory(Vertex + Layout->Diffuse, &Packed, sizeof(Packed));
            }
            Specular = (Written & (D9VmWritesSpecular | D9VmWritesFog)) != 0;
            if ((Parts & D3DFVF_SPECULAR) != 0 && (Specular || !Run->Keep))
            {
                DWORD Packed = 0;

                if ((Written & D9VmWritesSpecular) != 0)
                    Packed = D9PvPackColor(Registers->Colors[1]);
                if ((Written & D9VmWritesFog) != 0 && !OwnFog)
                    Packed = (Packed & 0x00FFFFFF) | (D9PvChannel(Registers->Fog[0].Float) << 24);
                CopyMemory(Vertex + Layout->Specular, &Packed, sizeof(Packed));
            }
            if (OwnFog)
                CopyMemory(Vertex + Layout->Fog, &Registers->Fog[0], sizeof(Registers->Fog[0]));
            for (UINT Set = 0; Set < Layout->CoordinateCount; Set++)
                CopyMemory(Vertex + Layout->Coordinates[Set], Registers->Outputs[Set], Layout->CoordinateSizes[Set]);
        }
    }
    return S_OK;
}

HRESULT
D9Device::PrepareFixedProcessing(
    _Outptr_ D9VmProgram** Program,
    _Out_ DWORD* Outputs)
{
    BOOLEAN Dirty = VertexShaders.Dirty;
    DWORD Waiting = VertexShaders.Stale & D9PvLightsStale;
    D9_FIXED_VERTEX_KEY Key;
    D9VmProgram* Made;
    DWORD* Tokens;
    UINT Count = 0;
    BYTE* Saved = nullptr;
    HRESULT hr;

    *Program = nullptr;
    *Outputs = 0;

    /* A hardware device's own pipeline runs this, so its converter is left as it was */
    if (!SoftwareVertices)
    {
        Saved = new (std::nothrow) BYTE[sizeof(VertexShaders)];
        if (Saved == nullptr)
            return E_OUTOFMEMORY;
        CopyMemory(Saved, &VertexShaders, sizeof(VertexShaders));
    }

    /* The converter takes the current input and states; the draws still see them as waiting */
    if (Declaration != nullptr && !Declaration->Transformed)
    {
        if (Declaration->ForFormat)
            VertexShaders.SetFvf(Declaration->Fvf);
        else
            VertexShaders.SetElements(Declaration->Elements);
    }
    if (Waiting != 0)
        VertexShaders.GatherLights();
    VertexShaders.RefreshKey();
    VertexShaders.UpdateRegisters();
    VertexShaders.Dirty = Dirty;
    VertexShaders.Stale |= Waiting;

    Key = VertexShaders.Key;

    /* Only the converter's registers and light loops reach the bank */
    ZeroMemory(FixedMachine->FloatConstants, D9PvFloatRegisters * sizeof(*FixedMachine->FloatConstants));
    ZeroMemory(FixedMachine->IntConstants, D9PvIntRegisters * sizeof(*FixedMachine->IntConstants));
    ZeroMemory(FixedMachine->BoolConstants, D9PvIntRegisters * sizeof(*FixedMachine->BoolConstants));
    CopyMemory(FixedMachine->FloatConstants, VertexShaders.Registers, sizeof(VertexShaders.Registers));
    CopyMemory(FixedMachine->IntConstants, VertexShaders.Loops, sizeof(VertexShaders.Loops));
    if (Saved != nullptr)
    {
        CopyMemory(static_cast<VOID*>(&VertexShaders), Saved, sizeof(VertexShaders));
        delete[] Saved;
    }

    Tokens = new (std::nothrow) DWORD[D9FixedVertexTokenLimit];
    if (Tokens == nullptr)
        return E_OUTOFMEMORY;
    if (!D9BuildFixedVertexShader(&Key, VertexShaders.FlowControl, VertexShaders.Projected, Tokens, &Count))
    {
        D9Trace(L"ProcessVertices: the fixed function vertex shader could not be made");
        delete[] Tokens;
        return D3DERR_INVALIDCALL;
    }
    Made = new (std::nothrow) D9VmProgram();
    if (Made == nullptr)
    {
        delete[] Tokens;
        return E_OUTOFMEMORY;
    }
    hr = Made->Compile(Tokens, Count);
    delete[] Tokens;
    if (FAILED(hr))
    {
        D9Trace(L"ProcessVertices: the interpreter cannot run the fixed function shader, %08lX", hr);
        delete Made;
        return hr;
    }
    *Program = Made;
    *Outputs = Key.Outputs;
    return S_OK;
}

/* Whether DONOTCOPYDATA still lets a hardware device's fixed function pipeline write its colors and coordinates */
VOID
D9Device::FixedKeptParts(
    _In_ DWORD Outputs,
    _Inout_ DWORD* Parts,
    _Inout_ UINT* CoordinateCount)
{
    BOOLEAN Lighting = RenderStates[D3DRS_LIGHTING] != FALSE;
    BOOLEAN Generated = FALSE;

    if (!Lighting)
        *Parts &= ~D3DFVF_DIFFUSE;
    if (!(Lighting && RenderStates[D3DRS_SPECULARENABLE] != FALSE) && (Outputs & D9PvWritesFog) == 0)
        *Parts &= ~D3DFVF_SPECULAR;
    for (UINT Stage = 0; Stage < D9VertexStages; Stage++)
    {
        if ((Switch.ApiCoordIndex[Stage] & 0xFFFF0000) != 0 || Switch.ApiTransformFlags[Stage] != 0)
            Generated = TRUE;
    }
    if (!Generated)
        *CoordinateCount = 0;
}

STDMETHODIMP
D9Device::ProcessVertices(
    _In_ UINT SrcStartIndex,
    _In_ UINT DestIndex,
    _In_ UINT VertexCount,
    _In_opt_ IDirect3DVertexBuffer9* DestBuffer,
    _In_opt_ IDirect3DVertexDeclaration9* VertexDecl,
    _In_ DWORD Flags)
{
    D9_DEVICE_LOCK();
    D9VertexBuffer* Dest = static_cast<D9VertexBuffer*>(DestBuffer);
    D9Declaration* Output = static_cast<D9Declaration*>(VertexDecl);
    ULONGLONG End = static_cast<ULONGLONG>(DestIndex) + VertexCount;
    D3DVERTEXBUFFER_DESC Desc;
    D9PvLayout Layout;
    D9PvTarget Targets[D9VmDeclaredLimit];
    UINT TargetCount = 0;
    UINT Stride;
    D9VmProgram* Program;
    D9VmProgram* Fixed = nullptr;
    D9VertexMachine** Slot;
    D9VertexMachine* Vm;
    DWORD FixedOutputs = 0;
    DWORD Parts;
    BOOLEAN Keep = (Flags & D9PvKeepData) != 0;
    BYTE* Base;
    BYTE* Vertex;
    FLOAT Scale[3];
    FLOAT Offset[3];
    DWORD Position;
    BOOLEAN Clipping = RenderStates[D3DRS_CLIPPING] != FALSE;
    D9PvRun Run = {};
    HRESULT hr;

    if (Dest == nullptr)
        return D3DERR_INVALIDCALL;
    Dest->GetDesc(&Desc);
    D9PvMakeLayout(Desc.FVF, &Layout);
    Parts = Desc.FVF;
    if (Output != nullptr)
    {
        Stride = D9PvDeclarationStride(Output);
        if (Stride * End > Desc.Size)
        {
            D9Trace(L"ProcessVertices: %u vertices from %u do not fit the destination", VertexCount, DestIndex);
            return D3DERR_INVALIDCALL;
        }
    }
    else
    {
        Stride = Layout.Stride;
        if (End > (Stride != 0 ? Desc.Size / Stride : 0))
        {
            D9Trace(L"ProcessVertices: %u vertices from %u do not fit the destination", VertexCount, DestIndex);
            return D3DERR_INVALIDCALL;
        }
    }

    /* The fixed function pipeline has no shader to write a declaration's outputs */
    if (VertexShader == nullptr && Output != nullptr)
    {
        D9Trace(L"ProcessVertices: a declaration destination needs a vertex shader");
        return D3DERR_INVALIDCALL;
    }
    /* The fixed function shaders run on an interpreter of their own, whose registers the application's
       shaders never touch */
    Slot = VertexShader == nullptr ? &FixedMachine : &Machine;
    if (*Slot == nullptr)
    {
        *Slot = new (std::nothrow) D9VertexMachine();
        if (*Slot == nullptr)
            return E_OUTOFMEMORY;
        hr = (*Slot)->Initialize(D9PvFloatRegisters, D9PvIntRegisters, D9PvIntRegisters);
        if (FAILED(hr))
        {
            delete *Slot;
            *Slot = nullptr;
            return hr;
        }
    }
    Vm = *Slot;
    if (VertexShader == nullptr)
    {
        hr = PrepareFixedProcessing(&Fixed, &FixedOutputs);
        if (FAILED(hr))
            return hr;
        Program = Fixed;

        /* A hardware device's pipeline writes the coordinates it computes, which the destination must hold */
        if (!SoftwareVertices)
        {
            static const BYTE CoordinateBytes[] = { 8, 12, 16, 4 };
            UINT Sets = (FixedOutputs & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;

            if (Sets > Layout.CoordinateCount ||
                (FixedOutputs & Desc.FVF & 0xFFFF0000) != (FixedOutputs & 0xFFFF0000))
            {
                D9Trace(L"ProcessVertices: the destination format %08lX lacks the coordinates of %08lX", Desc.FVF,
                        FixedOutputs);
                delete Fixed;
                return D3DERR_INVALIDCALL;
            }
            Layout.CoordinateCount = Sets;
            for (UINT Set = 0; Set < Sets; Set++)
            {
                Layout.CoordinateSizes[Set] = CoordinateBytes[(FixedOutputs >> (16 + Set * 2)) & 3];
                if (Set != 0)
                    Layout.Coordinates[Set] = Layout.Coordinates[Set - 1] + Layout.CoordinateSizes[Set - 1];
            }
        }

        /* Without copying, a software device writes only positions */
        if ((Flags & D9PvKeepData) != 0)
        {
            if (SoftwareVertices)
            {
                Parts &= ~(D3DFVF_DIFFUSE | D3DFVF_SPECULAR);
                Layout.CoordinateCount = 0;
            }
            else
            {
                FixedKeptParts(FixedOutputs, &Parts, &Layout.CoordinateCount);
            }
            Keep = FALSE;
        }
    }
    else
    {
        Program = VertexShader->Program;
        if (Program == nullptr)
        {
            Program = new (std::nothrow) D9VmProgram();
            if (Program == nullptr)
                return E_OUTOFMEMORY;
            hr = Program->Compile(VertexShader->Tokens(), VertexShader->Shape()->Size / sizeof(DWORD));
            if (FAILED(hr))
            {
                D9Trace(L"ProcessVertices: the interpreter cannot run the shader, %08lX", hr);
                delete Program;
                return hr;
            }
            VertexShader->Program = Program;
        }
    }

    /* A declaration destination takes the declared outputs it has room for, and only floats and colors */
    if (Output != nullptr)
    {
        if (Program->Version < D3DVS_VERSION(3, 0))
        {
            D9Trace(L"ProcessVertices: a declaration destination needs declared shader outputs");
            return D3DERR_INVALIDCALL;
        }
        for (UINT Index = 0; Index < Program->OutputCount; Index++)
        {
            const D9VmDeclared* Declared = &Program->Outputs[Index];
            const D3DVERTEXELEMENT9* Element;
            UINT Mask = Declared->Mask;
            INT Left;

            if (Declared->Usage == D3DDECLUSAGE_POSITION && Declared->Index == 0)
            {
                Element = D9PvFindElement(Output, D3DDECLUSAGE_POSITION, 0);
                if (Element == nullptr)
                    Element = D9PvFindElement(Output, D3DDECLUSAGE_POSITIONT, 0);
            }
            else
            {
                Element = D9PvFindElement(Output, Declared->Usage, Declared->Index);
            }
            if (Element == nullptr)
                continue;
            if (Element->Type <= D3DDECLTYPE_FLOAT4)
            {
                /* The mask keeps as many of its first components as the element has room for */
                Left = Element->Type + 1;
                if ((Mask & 1) != 0)
                {
                    Left -= 1;
                    if (Left == 0)
                        Mask &= 1;
                }
                if ((Mask & 2) != 0)
                {
                    Left -= 1;
                    if (Left == 0)
                        Mask &= 3;
                }
                if ((Mask & 4) != 0 && Left == 1)
                    Mask &= 7;
            }
            else if (Element->Type == D3DDECLTYPE_D3DCOLOR)
            {
                if (Mask != 0xF)
                    continue;
            }
            else
            {
                D9Trace(L"ProcessVertices: destination element type %u is neither float nor color", Element->Type);
                return D3DERR_INVALIDCALL;
            }
            Targets[TargetCount].Register = Declared->Register;
            Targets[TargetCount].Mask = Mask;
            Targets[TargetCount].Offset = Element->Offset;
            Targets[TargetCount].Type = Element->Type;
            Targets[TargetCount].Usage = Element->Usage;
            Targets[TargetCount].Index = Element->UsageIndex;
            TargetCount += 1;
        }
        if (TargetCount == 0)
            return S_OK;
    }

    /* The software bank: a hardware device never fills it, so its shaders read zeros */
    if (Fixed == nullptr)
    {
        ZeroMemory(Vm->FloatConstants, D9PvFloatRegisters * sizeof(*Vm->FloatConstants));
        ZeroMemory(Vm->IntConstants, D9PvIntRegisters * sizeof(*Vm->IntConstants));
        ZeroMemory(Vm->BoolConstants, D9PvIntRegisters * sizeof(*Vm->BoolConstants));
    }
    if (Fixed == nullptr &&
        (BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0)
    {
        /* A mixed or software device's bank is never wider than the interpreter's */
        CopyMemory(Vm->FloatConstants, VertexFloats, Limits.VertexFloats * sizeof(*VertexFloats));
        CopyMemory(Vm->IntConstants, VertexInts, Limits.VertexInts * sizeof(*VertexInts));
        CopyMemory(Vm->BoolConstants, VertexBools, Limits.VertexBools * sizeof(*VertexBools));
    }
    for (UINT Index = 0; Index < Program->DefinitionCount; Index++)
    {
        const DWORD* Tokens = &Program->Code[Program->Definitions[Index]];
        UINT Register = Tokens[1] & 0x7FF;

        if (LOWORD(Tokens[0]) == D3DSIO_DEF && Register < D9PvFloatRegisters)
            CopyMemory(Vm->FloatConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFI && Register < D9PvIntRegisters)
            CopyMemory(Vm->IntConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFB && Register < D9PvIntRegisters)
            Vm->BoolConstants[Register].Bits = Tokens[2];
    }

    /* The screen transform of the half space position, offset a little like the system's viewport cache */
    Scale[0] = static_cast<FLOAT>(CurrentViewport.Width);
    Scale[1] = -static_cast<FLOAT>(CurrentViewport.Height);
    Scale[2] = CurrentViewport.MaxZ - CurrentViewport.MinZ;
    Offset[0] = static_cast<FLOAT>(CurrentViewport.X) + 0.000001f;
    Offset[1] = (static_cast<FLOAT>(CurrentViewport.Y) + static_cast<FLOAT>(CurrentViewport.Height)) + 0.000001f;
    Offset[2] = CurrentViewport.MinZ;
    Position = Desc.FVF & D3DFVF_POSITION_MASK;

    hr = Dest->Lock(0, Desc.Size, reinterpret_cast<VOID**>(&Base), (Flags & D9PvLockFlags) | D3DLOCK_NOSYSLOCK);
    if (FAILED(hr))
    {
        D9Trace(L"ProcessVertices: cannot lock the destination, %08lX", hr);
        delete Fixed;
        return D3DERR_INVALIDCALL;
    }

    /* Under hardware processing a declaration destination starts where the last format destination's vertex
       size puts it; the software pipeline steps by the declaration's own size */
    if (Output == nullptr)
        ProcessedStride = Stride;
    Vertex = Base + static_cast<SIZE_T>(SoftwareVertices ? Stride : ProcessedStride) * DestIndex;

    Run.Program = Program;
    Run.Vm = Vm;
    Run.Layout = Layout;
    Run.Targets = Output != nullptr ? Targets : nullptr;
    Run.TargetCount = TargetCount;
    Run.Parts = Parts;
    Run.Position = Position;
    Run.Stride = Stride;
    Run.Keep = Keep;
    Run.Clipping = Clipping;
    CopyMemory(Run.Scale, Scale, sizeof(Scale));
    CopyMemory(Run.Offset, Offset, sizeof(Offset));
    for (UINT Index = 0; Index < D9StreamCount; Index++)
    {
        D9VertexBuffer* Source = static_cast<D9VertexBuffer*>(Streams[Index].Buffer);

        if (Source != nullptr && Source->Bytes() != nullptr)
            Run.Streams[Index] = Source->Bytes() + Streams[Index].Offset;
        Run.Strides[Index] = Streams[Index].Stride;
    }
    hr = D9PvRunVertices(&Run, Declaration, SrcStartIndex, VertexCount, Vertex, nullptr);

    Dest->Unlock();
    delete Fixed;
    return hr;
}

/* The interpreter of the bound shader, or of the fixed function one the converter makes, with the constants
   the shader reads loaded */
HRESULT
D9Device::PrepareSoftwareProgram(
    _Outptr_ D9VmProgram** Program,
    _Outptr_result_maybenull_ D9VmProgram** Owned,
    _Outptr_ D9VertexMachine** Vm)
{
    D9VertexMachine** Slot = VertexShader == nullptr ? &FixedMachine : &Machine;
    D9VmProgram* Running;
    DWORD Outputs;
    HRESULT hr;

    *Program = nullptr;
    *Owned = nullptr;
    *Vm = nullptr;
    if (*Slot == nullptr)
    {
        *Slot = new (std::nothrow) D9VertexMachine();
        if (*Slot == nullptr)
            return E_OUTOFMEMORY;
        hr = (*Slot)->Initialize(D9PvFloatRegisters, D9PvIntRegisters, D9PvIntRegisters);
        if (FAILED(hr))
        {
            delete *Slot;
            *Slot = nullptr;
            return hr;
        }
    }

    if (VertexShader == nullptr)
    {
        hr = PrepareFixedProcessing(&Running, &Outputs);
        if (FAILED(hr))
            return hr;
        *Owned = Running;
    }
    else
    {
        Running = VertexShader->Program;
        if (Running == nullptr)
        {
            Running = new (std::nothrow) D9VmProgram();
            if (Running == nullptr)
                return E_OUTOFMEMORY;
            hr = Running->Compile(VertexShader->Tokens(), VertexShader->Shape()->Size / sizeof(DWORD));
            if (FAILED(hr))
            {
                D9Trace(L"the interpreter cannot run a vertex shader, %08lX", hr);
                delete Running;
                return hr;
            }
            VertexShader->Program = Running;
        }

        /* The software bank holds the application's constants, then what the shader defines */
        ZeroMemory((*Slot)->FloatConstants, D9PvFloatRegisters * sizeof(*(*Slot)->FloatConstants));
        ZeroMemory((*Slot)->IntConstants, D9PvIntRegisters * sizeof(*(*Slot)->IntConstants));
        ZeroMemory((*Slot)->BoolConstants, D9PvIntRegisters * sizeof(*(*Slot)->BoolConstants));
        CopyMemory((*Slot)->FloatConstants, VertexFloats, Limits.VertexFloats * sizeof(*VertexFloats));
        CopyMemory((*Slot)->IntConstants, VertexInts, Limits.VertexInts * sizeof(*VertexInts));
        CopyMemory((*Slot)->BoolConstants, VertexBools, Limits.VertexBools * sizeof(*VertexBools));
    }
    for (UINT Index = 0; Index < Running->DefinitionCount; Index++)
    {
        const DWORD* Tokens = &Running->Code[Running->Definitions[Index]];
        UINT Register = Tokens[1] & 0x7FF;

        if (LOWORD(Tokens[0]) == D3DSIO_DEF && Register < D9PvFloatRegisters)
            CopyMemory((*Slot)->FloatConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFI && Register < D9PvIntRegisters)
            CopyMemory((*Slot)->IntConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFB && Register < D9PvIntRegisters)
            (*Slot)->BoolConstants[Register].Bits = Tokens[2];
    }
    *Program = Running;
    *Vm = *Slot;
    return S_OK;
}

/* Room for a draw's vertices in a transformed vertex buffer: after the last draw's without overwriting
   them while they fit, from the start of a discarded buffer otherwise, which a draw too large for it
   replaces with one of its own size first */
HRESULT
D9Device::LockTransformed(
    _In_ UINT Buffer,
    _In_ UINT Size,
    _Outptr_ BYTE** Data)
{
    D9Resource* Resource = &StagingBuffers[Buffer];
    DWORD Flags = D3DLOCK_NOOVERWRITE | D3DLOCK_NOSYSLOCK;
    UINT* Used = &TransformedUsed[Buffer];
    VOID* Mapped = nullptr;
    HRESULT hr;

    *Data = nullptr;
    if (Size > Resource->Width - *Used || *Used == 0)
    {
        if (Size > Resource->Width)
        {
            D9Resource Larger;

            hr = CreateDriverBuffer(Resource->Format, Size, Resource->Flags.Value, &Larger);
            if (FAILED(hr))
            {
                D9Trace(L"a transformed vertex buffer of %u bytes cannot be made, %08lX", Size, hr);
                return hr;
            }
            DestroyDriverSurface(Resource);
            *Resource = Larger;
        }
        Flags = D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK;
        *Used = 0;
    }
    TransformedAsync = TRUE;
    hr = LockDriverBuffer(Resource, 0, Resource->Width, TRUE, Flags, 0, &TransformedAsync, &Mapped);
    if (FAILED(hr))
    {
        D9Trace(L"the transformed vertex buffer cannot be locked, %08lX", hr);
        return hr;
    }
    TransformedBuffer = Buffer;
    TransformedStart = *Used;
    *Data = static_cast<BYTE*>(Mapped) + *Used;
    return S_OK;
}

/* Stream 0 reads the transformed vertex buffer unless the driver has it there already; the application's
   stream goes back at the next draw the hardware processes */
VOID
D9Device::BindTransformed(VOID)
{
    D3DDDIARG_SETSTREAMSOURCE Args = {};

    HANDLE Resource = StagingBuffers[TransformedBuffer].DriverResource;

    if (DriverStreamZero == Resource && DriverStrideZero == TransformedStride)
        return;
    Args.Stream = 0;
    Args.hVertexBuffer = Resource;
    Args.Stride = TransformedStride;
    DdiStreamSource(&Args);
    StreamDirty |= 1;
    DrawDirty |= D9DrawStreams;
}

/* The flags a clip vertex keeps besides its outcodes: an edge of the original triangle, and the plane that made
   the vertex, which the screen transform clamps it to */
static constexpr DWORD D9ClipEdge = 0x40000;
static constexpr DWORD D9ClipMadeFront = 0x10000;
static constexpr DWORD D9ClipMadeBack = 0x20000;
static constexpr DWORD D9ClipMadeLeft = 0x1000;
static constexpr DWORD D9ClipMadeRight = 0x2000;
static constexpr DWORD D9ClipMadeTop = 0x4000;
static constexpr DWORD D9ClipMadeBottom = 0x8000;
static constexpr DWORD D9ClipMadePlane = 0x80000;

/* The guard band outcodes, which the clipper tests in place of the side planes when the driver has a band */
static constexpr DWORD D9ClipBandLeft = 0x1000;
static constexpr DWORD D9ClipBandRight = 0x2000;
static constexpr DWORD D9ClipBandTop = 0x4000;
static constexpr DWORD D9ClipBandBottom = 0x8000;

/* Vertices one triangle may grow to while the clipper cuts it */
static constexpr UINT D9ClipPoolSize = 160;
static constexpr UINT D9ClipListSize = 32;

/* A vertex while the clipper works on it: the half space position, its flags, the colors, the screen position,
   the separate fog and the coordinate sets as the transformed vertex holds them */
struct D9ClipVertex
{
    FLOAT Clip[4];
    DWORD Flags;
    DWORD Diffuse;
    DWORD Specular;
    FLOAT Screen[4];
    FLOAT Fog;
    BYTE Coordinates[8 * 4 * sizeof(FLOAT)];
};

/* The planes the clipper cuts against */
enum class D9ClipPlane
{
    Front,
    Back,
    Left,
    Right,
    Top,
    Bottom,
    BandLeft,
    BandRight,
    BandTop,
    BandBottom,
    User,
};

/* What one clipped draw needs besides the run: the viewport and band edges, where the coordinates sit and
   the vertices the planes make */
struct D9Clipper
{
    const D9PvRun* Run;
    UINT CoordinateBytes;
    UINT CoordinateSets;
    DWORD Wraps[8];
    BOOLEAN Flat;
    FLOAT Inverse[3];
    FLOAT Low[2];
    FLOAT High[2];
    FLOAT Band[4];
    D9ClipVertex Pool[D9ClipPoolSize];
    UINT PoolUsed;
    D9ClipVertex* Lists[2][D9ClipListSize];
};

/* A color channel by channel along an edge, rounded half away from zero like the system */
static
DWORD
D9ClipBlendColor(
    _In_ DWORD From,
    _In_ DWORD To,
    _In_ FLOAT Along)
{
    DWORD Result = 0;

    for (UINT Shift = 0; Shift < 32; Shift += 8)
    {
        FLOAT Start = static_cast<FLOAT>((From >> Shift) & 0xFF);
        FLOAT Value = ((static_cast<FLOAT>((To >> Shift) & 0xFF) - Start) * Along) + Start;
        INT Rounded = static_cast<INT>(Value >= 0.0f ? Value + 0.5f : Value - 0.5f);

        Result |= (static_cast<DWORD>(Rounded) & 0xFF) << Shift;
    }
    return Result;
}

/* The vertex where an edge from an inside vertex to an outside one crosses a plane */
static
D9ClipVertex*
D9ClipCross(
    _Inout_ D9Clipper* Clipper,
    _In_ const D9ClipVertex* Inside,
    _In_ const D9ClipVertex* Outside,
    _In_ DWORD Made,
    _In_ FLOAT Near,
    _In_ FLOAT Span)
{
    D9ClipVertex* Made_;
    FLOAT Along = Near / Span;
    const FLOAT* From = reinterpret_cast<const FLOAT*>(Inside->Coordinates);
    const FLOAT* To = reinterpret_cast<const FLOAT*>(Outside->Coordinates);
    FLOAT* Into;

    if (Clipper->PoolUsed == D9ClipPoolSize)
        return nullptr;
    Made_ = &Clipper->Pool[Clipper->PoolUsed];
    Clipper->PoolUsed += 1;
    Made_->Flags = (Inside->Flags & Outside->Flags & ~D9ClipEdge) | Made;
    for (UINT Part = 0; Part < 4; Part++)
        Made_->Clip[Part] = ((Outside->Clip[Part] - Inside->Clip[Part]) * Along) + Inside->Clip[Part];

    if ((Clipper->Run->Parts & D3DFVF_DIFFUSE) != 0)
    {
        Made_->Diffuse = Clipper->Flat ? Inside->Diffuse : D9ClipBlendColor(Inside->Diffuse, Outside->Diffuse, Along);
    }
    if ((Clipper->Run->Parts & D3DFVF_SPECULAR) != 0)
    {
        if (Clipper->Flat)
        {
            FLOAT Start = static_cast<FLOAT>(Inside->Specular >> 24);
            FLOAT Value = ((static_cast<FLOAT>(Outside->Specular >> 24) - Start) * Along) + Start;
            INT Rounded = static_cast<INT>(Value >= 0.0f ? Value + 0.5f : Value - 0.5f);

            Made_->Specular = (Inside->Specular & 0x00FFFFFF) | ((static_cast<DWORD>(Rounded) & 0xFF) << 24);
        }
        else
        {
            Made_->Specular = D9ClipBlendColor(Inside->Specular, Outside->Specular, Along);
        }
    }
    if ((Clipper->Run->Parts & D9VmFogFormat) != 0)
        Made_->Fog = ((Outside->Fog - Inside->Fog) * Along) + Inside->Fog;

    /* Coordinates go straight along the edge; a wrapped one would take the shorter way, which no draw here uses */
    Into = reinterpret_cast<FLOAT*>(Made_->Coordinates);
    for (UINT Set = 0; Set < Clipper->CoordinateSets; Set++)
    {
        UINT Parts = Clipper->Run->Layout.CoordinateSizes[Set] / sizeof(FLOAT);

        for (UINT Part = 0; Part < Parts; Part++)
        {
            *Into = ((*To - *From) * Along) + *From;
            Into += 1;
            From += 1;
            To += 1;
        }
    }
    return Made_;
}

/* How far inside a plane a vertex is, each plane computed the way the system orders its arithmetic */
static
FLOAT
D9ClipDistance(
    _In_ const D9Clipper* Clipper,
    _In_ D9ClipPlane Plane,
    _In_ UINT User,
    _In_ const D9ClipVertex* Vertex)
{
    const FLOAT* At = Vertex->Clip;

    switch (Plane)
    {
    case D9ClipPlane::Front:
        return At[2];
    case D9ClipPlane::Back:
        return At[3] - At[2];
    case D9ClipPlane::Left:
        return At[0];
    case D9ClipPlane::Right:
        return At[3] - At[0];
    case D9ClipPlane::Top:
        return At[3] - At[1];
    case D9ClipPlane::Bottom:
        return At[1];
    case D9ClipPlane::BandLeft:
        return At[0] - (Clipper->Band[0] * At[3]);
    case D9ClipPlane::BandRight:
        return (Clipper->Band[2] * At[3]) - At[0];
    case D9ClipPlane::BandTop:
        return (Clipper->Band[3] * At[3]) - At[1];
    case D9ClipPlane::BandBottom:
        return At[1] - (Clipper->Band[1] * At[3]);
    default:
    {
        const FLOAT* Given = Clipper->Run->Planes[User];

        return (((At[1] * Given[1]) + (At[0] * Given[0])) + (At[2] * Given[2])) + (At[3] * Given[3]);
    }
    }
}

/* One plane of the polygon clipper: the vertices inside stay, and each edge across the plane adds where it
   crosses */
static
UINT
D9ClipAgainst(
    _Inout_ D9Clipper* Clipper,
    _In_ D9ClipPlane Plane,
    _In_ UINT User,
    _In_ DWORD Made,
    _In_reads_(Count) D9ClipVertex* const* In,
    _In_ UINT Count,
    _Out_writes_(D9ClipListSize) D9ClipVertex** Out)
{
    const D9ClipVertex* Previous = In[Count - 1];
    FLOAT Before = D9ClipDistance(Clipper, Plane, User, Previous);
    UINT Kept = 0;

    for (UINT Index = 0; Index < Count && Kept + 2 <= D9ClipListSize; Index++)
    {
        D9ClipVertex* Current = In[Index];
        FLOAT Now = D9ClipDistance(Clipper, Plane, User, Current);

        if (Before >= 0.0f)
        {
            Out[Kept++] = const_cast<D9ClipVertex*>(Previous);
            if (Now < 0.0f)
                Out[Kept++] = D9ClipCross(Clipper, Previous, Current, Made, Before, Before - Now);
        }
        else if (Now >= 0.0f)
        {
            Out[Kept++] = D9ClipCross(Clipper, Current, Previous, (Previous->Flags & D9ClipEdge) | Made, Now,
                                      Now - Before);
        }
        if (Kept != 0 && Out[Kept - 1] == nullptr)
            return 0;
        Previous = Current;
        Before = Now;
    }
    return Kept;
}

/* The outcodes of a vertex the front plane made, kept with its edge and front flags */
static
DWORD
D9ClipRecode(
    _In_ const D9Clipper* Clipper,
    _Inout_ D9ClipVertex* Vertex)
{
    const FLOAT* At = Vertex->Clip;
    DWORD Code = D9PvClipCode(Clipper->Run, At[0], At[1], At[2], At[3]);

    if (Code != 0 && Clipper->Run->GuardBand)
        Code |= D9PvBandCode(Clipper->Run, At[0], At[1], At[3]);
    Vertex->Flags = (Vertex->Flags & (D9ClipEdge | D9ClipMadeFront)) | Code;
    return Code;
}

/* Cuts a triangle against every plane it crosses and puts the clipped vertices on the screen; vertices a
   side plane made sit on its edge */
static
UINT
D9ClipTriangle(
    _Inout_ D9Clipper* Clipper,
    _In_ D9ClipVertex* First,
    _In_ D9ClipVertex* Second,
    _In_ D9ClipVertex* Third,
    _Out_ D9ClipVertex*** Result)
{
    static const struct
    {
        DWORD Code;
        D9ClipPlane Plane;
        DWORD Made;
    } Sides[2][5] =
    {
        {
            { D3DCS_BACK, D9ClipPlane::Back, D9ClipMadeBack },
            { D3DCS_LEFT, D9ClipPlane::Left, D9ClipMadeLeft },
            { D3DCS_RIGHT, D9ClipPlane::Right, D9ClipMadeRight },
            { D3DCS_BOTTOM, D9ClipPlane::Bottom, D9ClipMadeBottom },
            { D3DCS_TOP, D9ClipPlane::Top, D9ClipMadeTop },
        },
        {
            { D3DCS_BACK, D9ClipPlane::Back, D9ClipMadeBack },
            { D9ClipBandLeft, D9ClipPlane::BandLeft, D9ClipMadeLeft },
            { D9ClipBandRight, D9ClipPlane::BandRight, D9ClipMadeRight },
            { D9ClipBandBottom, D9ClipPlane::BandBottom, D9ClipMadeBottom },
            { D9ClipBandTop, D9ClipPlane::BandTop, D9ClipMadeTop },
        },
    };
    D9ClipVertex** In = Clipper->Lists[0];
    D9ClipVertex** Out = Clipper->Lists[1];
    DWORD Code = First->Flags | Second->Flags | Third->Flags;
    UINT Count = 3;
    UINT Band = Clipper->Run->GuardBand ? 1 : 0;

    Clipper->PoolUsed = 0;
    In[0] = First;
    In[1] = Second;
    In[2] = Third;
    if ((Code & D3DCS_FRONT) != 0)
    {
        Count = D9ClipAgainst(Clipper, D9ClipPlane::Front, 0, D9ClipMadeFront, In, Count, Out);
        if (Count < 3)
            return 0;
        D9ClipVertex** Swap = In;
        In = Out;
        Out = Swap;

        /* The vertices the front plane made are tested against the others again */
        Code = 0;
        for (UINT Index = 0; Index < Count; Index++)
        {
            DWORD Flags = In[Index]->Flags;

            if ((Flags & D9ClipMadeFront) != 0)
                Flags = D9ClipRecode(Clipper, In[Index]);
            Code |= Flags;
        }
    }
    for (UINT Side = 0; Side < 5; Side++)
    {
        if ((Code & Sides[Band][Side].Code) == 0)
            continue;
        Count = D9ClipAgainst(Clipper, Sides[Band][Side].Plane, 0, Sides[Band][Side].Made, In, Count, Out);
        if (Count < 3)
            return 0;
        D9ClipVertex** Swap = In;
        In = Out;
        Out = Swap;
    }
    for (UINT Plane = 0; Plane < Clipper->Run->PlaneCount; Plane++)
    {
        if ((Code & (D3DCS_PLANE0 << Plane)) == 0)
            continue;
        Count = D9ClipAgainst(Clipper, D9ClipPlane::User, Plane, D9ClipMadePlane << Plane, In, Count, Out);
        if (Count < 3)
            return 0;
        D9ClipVertex** Swap = In;
        In = Out;
        Out = Swap;
    }

    /* Clipped vertices go on the screen; one a side plane made lands on that side's edge */
    for (UINT Index = 0; Index < Count; Index++)
    {
        D9ClipVertex* Vertex = In[Index];
        DWORD Flags = Vertex->Flags;
        FLOAT Reciprocal;

        if ((Flags & ~D9ClipEdge) == 0)
            continue;
        Reciprocal = 1.0f / Vertex->Clip[3];
        for (UINT Axis = 0; Axis < 2; Axis++)
        {
            DWORD Sides_ = (Flags >> (12 + Axis * 2)) & 3;
            FLOAT Value;

            if (Sides_ == 1)
            {
                Value = Clipper->Low[Axis];
            }
            else if (Sides_ == 2)
            {
                Value = Clipper->High[Axis];
            }
            else
            {
                Value = ((Clipper->Run->Scale[Axis] * Vertex->Clip[Axis]) * Reciprocal) + Clipper->Run->Offset[Axis];
                if (Value < Clipper->Low[Axis])
                    Value = Clipper->Low[Axis];
                if (Value > Clipper->High[Axis])
                    Value = Clipper->High[Axis];
            }
            Vertex->Screen[Axis] = Value;
        }
        Vertex->Screen[2] = ((Vertex->Clip[2] * Reciprocal) * Clipper->Run->Scale[2]) + Clipper->Run->Offset[2];
        Vertex->Screen[3] = Reciprocal;
    }
    *Result = In;
    return Count;
}

/* A clip vertex from a transformed one: one the vertex pipeline put on the screen goes back to the half
   space, one it left there takes its position as it is */
static
VOID
D9ClipFromTransformed(
    _In_ const D9Clipper* Clipper,
    _In_ const BYTE* Transformed,
    _In_ DWORD Code,
    _Out_ D9ClipVertex* Vertex)
{
    const D9PvRun* Run = Clipper->Run;
    const BYTE* Next = Transformed + 4 * sizeof(FLOAT);
    DWORD Mask = Run->GuardBand ? ~D9PvSidePlanes : 0xFFFFFFFF;

    ZeroMemory(Vertex, sizeof(*Vertex));
    if ((Code & Mask) == 0)
    {
        FLOAT W;

        CopyMemory(Vertex->Screen, Transformed, sizeof(Vertex->Screen));
        W = 1.0f / Vertex->Screen[3];
        Vertex->Clip[3] = W;
        for (UINT Axis = 0; Axis < 3; Axis++)
            Vertex->Clip[Axis] = ((Vertex->Screen[Axis] - Run->Offset[Axis]) * W) * Clipper->Inverse[Axis];
    }
    else
    {
        CopyMemory(Vertex->Clip, Transformed, sizeof(Vertex->Clip));
    }
    Vertex->Flags = Code;

    /* The colors, fog and coordinates follow the position in this order */
    if ((Run->Parts & D3DFVF_DIFFUSE) != 0)
    {
        CopyMemory(&Vertex->Diffuse, Next, sizeof(Vertex->Diffuse));
        Next += sizeof(DWORD);
    }
    if ((Run->Parts & D3DFVF_SPECULAR) != 0)
    {
        CopyMemory(&Vertex->Specular, Next, sizeof(Vertex->Specular));
        Next += sizeof(DWORD);
    }
    if ((Run->Parts & D9VmFogFormat) != 0)
    {
        CopyMemory(&Vertex->Fog, Next, sizeof(Vertex->Fog));
        Next += sizeof(DWORD);
    }
    CopyMemory(Vertex->Coordinates, Next, Clipper->CoordinateBytes);
}

/* The transformed vertex the driver draws for a clip vertex */
static
VOID
D9ClipToTransformed(
    _In_ const D9Clipper* Clipper,
    _In_ const D9ClipVertex* Vertex,
    _Out_ BYTE* Transformed)
{
    const D9PvLayout* Layout = &Clipper->Run->Layout;

    CopyMemory(Transformed, Vertex->Screen, sizeof(Vertex->Screen));
    if ((Clipper->Run->Parts & D3DFVF_DIFFUSE) != 0)
        CopyMemory(Transformed + Layout->Diffuse, &Vertex->Diffuse, sizeof(Vertex->Diffuse));
    if ((Clipper->Run->Parts & D3DFVF_SPECULAR) != 0)
        CopyMemory(Transformed + Layout->Specular, &Vertex->Specular, sizeof(Vertex->Specular));
    if ((Clipper->Run->Parts & D9VmFogFormat) != 0)
        CopyMemory(Transformed + Layout->Fog, &Vertex->Fog, sizeof(Vertex->Fog));
    if (Layout->CoordinateCount != 0)
        CopyMemory(Transformed + Layout->Coordinates[0], Vertex->Coordinates, Clipper->CoordinateBytes);
}

/* Draws the triangles a strip of transformed vertices makes, from where the draw's vertices start, and moves
   the start past them */
VOID
D9Device::DrawTransformedRun(
    _In_ D3DPRIMITIVETYPE Type,
    _In_ UINT Vertices,
    _In_ UINT Primitives)
{
    D3DDDIARG_DRAWPRIMITIVE2 Draw = {};

    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    BindTransformed();
    SendTargets();
    SettleSwitch(Type, FALSE);
    Draw.PrimitiveType = Type;
    Draw.FirstVertexOffset = TransformedStart;
    Draw.PrimitiveCount = Primitives;
    DdiDrawPrimitive2(&Draw);
    TransformedStart += Vertices * TransformedStride;
}

/* Draws a run of whole indexed triangles from the transformed vertex buffer with the indices where they lie */
VOID
D9Device::DrawIndexedRun(
    _In_ const VOID* IndexData,
    _In_ UINT IndexSize,
    _In_ UINT MinIndex,
    _In_ UINT VertexCount,
    _In_ UINT Primitives)
{
    D3DDDIARG_DRAWINDEXEDPRIMITIVE2 Draw = {};

    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    BindTransformed();
    SendTargets();
    SettleSwitch(D3DPT_TRIANGLELIST, TRUE);
    Draw.PrimitiveType = D3DPT_TRIANGLELIST;
    Draw.BaseVertexOffset = static_cast<INT>(TransformedStart) - static_cast<INT>(MinIndex * TransformedStride);
    Draw.MinIndex = MinIndex;
    Draw.NumVertices = VertexCount;
    Draw.PrimitiveCount = Primitives;
    DdiDrawIndexedPrimitive2(&Draw, IndexSize, IndexData);
}

/* Draws vertices the clipper made from memory of its own; stream 0 holds nothing the next draw can count on */
VOID
D9Device::DrawClippedVertices(
    _In_ D3DPRIMITIVETYPE Type,
    _In_reads_bytes_(Vertices * TransformedStride) const BYTE* Data,
    _In_ UINT Vertices,
    _In_ UINT Primitives)
{
    D3DDDIARG_SETSTREAMSOURCEUM Memory = {};
    D3DDDIARG_DRAWPRIMITIVE Draw = {};

    UNREFERENCED_PARAMETER(Vertices);
    if ((DrawDirty & D9DrawTextures) != 0)
        FlushTextures();
    SendTargets();
    SettleSwitch(Type, FALSE);
    DriverStreamZero = NULL;
    DriverStrideZero = TransformedStride;
    Draw.PrimitiveType = Type;
    Draw.PrimitiveCount = Primitives;
    if (Batch != nullptr)
    {
        Batch->DrawMemory(&Draw, TransformedStride, Data);
        return;
    }
    Memory.Stream = 0;
    Memory.Stride = TransformedStride;
    DeviceFunctions.pfnSetStreamSourceUm(DriverDevice, &Memory, Data);
    DeviceFunctions.pfnDrawPrimitive(DriverDevice, &Draw, nullptr);
}

/* Clips a triangle and draws what is left as a fan; flat shading gives every vertex the first one's colors
   while the clipper runs */
HRESULT
D9Device::DrawClippedTriangle(
    _Inout_ D9Clipper* Clipper,
    _Inout_ D9ClipVertex* First,
    _Inout_ D9ClipVertex* Second,
    _Inout_ D9ClipVertex* Third)
{
    D9ClipVertex** Polygon = nullptr;
    DWORD Kept[4] = { Second->Diffuse, Second->Specular, Third->Diffuse, Third->Specular };
    BYTE* Vertices;
    UINT Count;

    First->Flags |= D9ClipEdge;
    Second->Flags |= D9ClipEdge;
    Third->Flags |= D9ClipEdge;
    if (Clipper->Flat)
    {
        Second->Diffuse = First->Diffuse;
        Third->Diffuse = First->Diffuse;
        Second->Specular = (Second->Specular & 0xFF000000) | (First->Specular & 0x00FFFFFF);
        Third->Specular = (Third->Specular & 0xFF000000) | (First->Specular & 0x00FFFFFF);
    }
    Count = D9ClipTriangle(Clipper, First, Second, Third, &Polygon);
    if (Count != 0)
    {
        Vertices = new (std::nothrow) BYTE[Count * TransformedStride];
        if (Vertices == nullptr)
            return E_OUTOFMEMORY;
        for (UINT Index = 0; Index < Count; Index++)
            D9ClipToTransformed(Clipper, Polygon[Index], Vertices + Index * TransformedStride);
        DrawClippedVertices(D3DPT_TRIANGLEFAN, Vertices, Count, Count - 2);
        delete[] Vertices;
    }
    First->Flags &= ~D9ClipEdge;
    Second->Flags &= ~D9ClipEdge;
    Third->Flags &= ~D9ClipEdge;
    if (Clipper->Flat)
    {
        Second->Diffuse = Kept[0];
        Second->Specular = Kept[1];
        Third->Diffuse = Kept[2];
        Third->Specular = Kept[3];
    }
    return S_OK;
}

/* A triangle draw some of whose triangles cross the frustum: runs of whole triangles draw from the transformed
   vertex buffer, triangles outside one plane are dropped and the others clipped */
HRESULT
D9Device::DrawClipped(
    _In_ const D9PvRun* Run,
    _In_ D3DPRIMITIVETYPE Type,
    _In_ UINT PrimitiveCount,
    _In_reads_bytes_(VertexCount * Run->Stride) const BYTE* Data,
    _In_ UINT VertexCount,
    _In_reads_(VertexCount) const WORD* Codes,
    _In_opt_ const VOID* IndexData,
    _In_ UINT IndexSize,
    _In_ UINT MinIndex)
{
    DWORD Mask = Run->GuardBand ? ~D9PvSidePlanes : 0xFFFFFFFF;
    UINT Stride = Run->Stride;
    D9Clipper* Clipper;
    D9ClipVertex Corners[3];
    UINT Pending = 0;
    UINT RunFirst = 0;
    HRESULT hr = S_OK;

    Clipper = new (std::nothrow) D9Clipper;
    if (Clipper == nullptr)
        return E_OUTOFMEMORY;
    ZeroMemory(Clipper, sizeof(*Clipper));
    Clipper->Run = Run;
    Clipper->CoordinateSets = Run->Layout.CoordinateCount;
    for (UINT Set = 0; Set < Run->Layout.CoordinateCount; Set++)
        Clipper->CoordinateBytes += Run->Layout.CoordinateSizes[Set];
    Clipper->Flat = RenderStates[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    Clipper->Inverse[0] = 1.0f / Run->Scale[0];
    Clipper->Inverse[1] = 1.0f / Run->Scale[1];
    Clipper->Inverse[2] = 1.0f / Run->Scale[2];
    if (Run->GuardBand)
    {
        FLOAT X = static_cast<FLOAT>(CurrentViewport.X);
        FLOAT Y = static_cast<FLOAT>(CurrentViewport.Y);
        FLOAT Width = static_cast<FLOAT>(CurrentViewport.Width);
        FLOAT Height = static_cast<FLOAT>(CurrentViewport.Height);
        FLOAT Left = 1.0f - ((Caps.GuardBandLeft - X) * (2.0f / Width));
        FLOAT Right = ((Caps.GuardBandRight - X) * (2.0f / Width)) - 1.0f;
        FLOAT Bottom = ((Caps.GuardBandBottom - Y) * (2.0f / Height)) - 1.0f;
        FLOAT Top = 1.0f - ((Caps.GuardBandTop - Y) * (2.0f / Height));

        Clipper->Low[0] = Caps.GuardBandLeft;
        Clipper->Low[1] = Caps.GuardBandTop;
        Clipper->High[0] = Caps.GuardBandRight;
        Clipper->High[1] = Caps.GuardBandBottom;
        Clipper->Band[0] = (1.0f - Left) * 0.5f;
        Clipper->Band[1] = (1.0f - Bottom) * 0.5f;
        Clipper->Band[2] = (Right + 1.0f) * 0.5f;
        Clipper->Band[3] = (Top + 1.0f) * 0.5f;
    }
    else
    {
        Clipper->Low[0] = static_cast<FLOAT>(CurrentViewport.X);
        Clipper->Low[1] = static_cast<FLOAT>(CurrentViewport.Y);
        Clipper->High[0] = static_cast<FLOAT>(CurrentViewport.X) + static_cast<FLOAT>(CurrentViewport.Width);
        Clipper->High[1] = static_cast<FLOAT>(CurrentViewport.Y) + static_cast<FLOAT>(CurrentViewport.Height);
    }

    if (IndexData != nullptr)
    {
        /* Indices pick the vertices; runs of whole triangles draw with the indices where they lie */
        const BYTE* RunIndices = static_cast<const BYTE*>(IndexData);

        for (UINT Triangle = 0; Triangle < PrimitiveCount && SUCCEEDED(hr); Triangle++)
        {
            const BYTE* At = static_cast<const BYTE*>(IndexData) + Triangle * 3 * IndexSize;
            UINT Picked[3];
            WORD Three[3];

            for (UINT Corner = 0; Corner < 3; Corner++)
            {
                if (IndexSize == 2)
                    Picked[Corner] = reinterpret_cast<const WORD*>(At)[Corner];
                else
                    Picked[Corner] = reinterpret_cast<const DWORD*>(At)[Corner];
                Picked[Corner] -= MinIndex;
                Three[Corner] = Codes[Picked[Corner]];
            }
            if ((Three[0] & Three[1] & Three[2]) == 0 && ((Three[0] | Three[1] | Three[2]) & Mask) == 0)
            {
                Pending += 1;
                continue;
            }
            if (Pending != 0)
                DrawIndexedRun(RunIndices, IndexSize, MinIndex, VertexCount, Pending);
            Pending = 0;
            RunIndices = At + 3 * IndexSize;
            if ((Three[0] & Three[1] & Three[2]) != 0)
                continue;
            for (UINT Corner = 0; Corner < 3; Corner++)
                D9ClipFromTransformed(Clipper, Data + Picked[Corner] * Stride, Three[Corner], &Corners[Corner]);
            hr = DrawClippedTriangle(Clipper, &Corners[0], &Corners[1], &Corners[2]);
        }
        if (SUCCEEDED(hr) && Pending != 0)
            DrawIndexedRun(RunIndices, IndexSize, MinIndex, VertexCount, Pending);
    }
    else if (Type == D3DPT_TRIANGLELIST)
    {
        for (UINT Triangle = 0; Triangle < PrimitiveCount && SUCCEEDED(hr); Triangle++)
        {
            const WORD* Three = &Codes[Triangle * 3];
            const BYTE* At = Data + Triangle * 3 * Stride;

            if ((Three[0] & Three[1] & Three[2]) == 0 && ((Three[0] | Three[1] | Three[2]) & Mask) == 0)
            {
                Pending += 1;
                continue;
            }
            if (Pending != 0)
                DrawTransformedRun(D3DPT_TRIANGLELIST, Pending * 3, Pending);
            Pending = 0;
            TransformedStart += 3 * Stride;
            if ((Three[0] & Three[1] & Three[2]) != 0)
                continue;
            for (UINT Corner = 0; Corner < 3; Corner++)
                D9ClipFromTransformed(Clipper, At + Corner * Stride, Three[Corner], &Corners[Corner]);
            hr = DrawClippedTriangle(Clipper, &Corners[0], &Corners[1], &Corners[2]);
        }
        if (SUCCEEDED(hr) && Pending != 0)
            DrawTransformedRun(D3DPT_TRIANGLELIST, Pending * 3, Pending);
    }
    else if (Type == D3DPT_TRIANGLESTRIP)
    {
        for (UINT Triangle = 0; Triangle < PrimitiveCount && SUCCEEDED(hr); Triangle++)
        {
            const WORD* Three = &Codes[Triangle];
            const BYTE* At = Data + Triangle * Stride;
            BOOLEAN Odd = (Triangle & 1) != 0;

            if ((Three[0] & Three[1] & Three[2]) == 0 && ((Three[0] | Three[1] | Three[2]) & Mask) == 0)
            {
                if (Pending != 0 || !Odd)
                {
                    Pending += 1;
                    continue;
                }

                /* A run may not start on an odd triangle, which draws alone with its winding put right */
                BYTE* Alone = new (std::nothrow) BYTE[3 * Stride];

                if (Alone == nullptr)
                {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                CopyMemory(Alone, At + Stride, Stride);
                CopyMemory(Alone + Stride, At, Stride);
                CopyMemory(Alone + 2 * Stride, At + 2 * Stride, Stride);
                DrawClippedVertices(D3DPT_TRIANGLEFAN, Alone, 3, 1);
                delete[] Alone;
                TransformedStart += Stride;
                continue;
            }
            if (Pending != 0)
            {
                DrawTransformedRun(D3DPT_TRIANGLESTRIP, Pending + 2, Pending);
                TransformedStart -= Stride;
            }
            else
            {
                TransformedStart += Stride;
            }
            Pending = 0;
            if ((Three[0] & Three[1] & Three[2]) != 0)
                continue;
            D9ClipFromTransformed(Clipper, At, Three[0], &Corners[0]);
            D9ClipFromTransformed(Clipper, At + (Odd ? 2 : 1) * Stride, Three[Odd ? 2 : 1], &Corners[1]);
            D9ClipFromTransformed(Clipper, At + (Odd ? 1 : 2) * Stride, Three[Odd ? 1 : 2], &Corners[2]);
            hr = DrawClippedTriangle(Clipper, &Corners[0], &Corners[1], &Corners[2]);
        }
        if (SUCCEEDED(hr) && Pending != 0)
            DrawTransformedRun(D3DPT_TRIANGLESTRIP, Pending + 2, Pending);
    }
    else
    {
        /* A fan keeps its first vertex; a run that does not start next to it borrows the vertex before it */
        D9ClipVertex Hub;

        D9ClipFromTransformed(Clipper, Data, Codes[0], &Hub);
        for (UINT Triangle = 0; Triangle <= PrimitiveCount && SUCCEEDED(hr); Triangle++)
        {
            BOOLEAN Last = Triangle == PrimitiveCount;
            WORD One = 0;
            WORD Two = 0;
            BOOLEAN Culled = FALSE;

            if (!Last)
            {
                One = Codes[Triangle + 1];
                Two = Codes[Triangle + 2];
                Culled = (Codes[0] & One & Two) != 0;
                if (!Culled && ((Codes[0] | One | Two) & Mask) == 0)
                {
                    Pending += 1;
                    continue;
                }
            }
            if (Pending != 0)
            {
                if (RunFirst == 0)
                {
                    DrawTransformedRun(D3DPT_TRIANGLEFAN, Pending + 2, Pending);
                }
                else
                {
                    BYTE* Borrowed = new (std::nothrow) BYTE[(Pending + 2) * Stride];

                    if (Borrowed == nullptr)
                    {
                        hr = E_OUTOFMEMORY;
                        break;
                    }
                    CopyMemory(Borrowed, Data, Stride);
                    CopyMemory(Borrowed + Stride, Data + RunFirst * Stride, (Pending + 1) * Stride);
                    DrawClippedVertices(D3DPT_TRIANGLEFAN, Borrowed, Pending + 2, Pending);
                    delete[] Borrowed;
                }
            }
            Pending = 0;
            if (Last)
                break;
            RunFirst = Triangle + 2;
            if (Culled)
                continue;
            D9ClipFromTransformed(Clipper, Data + (Triangle + 1) * Stride, One, &Corners[0]);
            D9ClipFromTransformed(Clipper, Data + (Triangle + 2) * Stride, Two, &Corners[1]);
            hr = DrawClippedTriangle(Clipper, &Corners[0], &Corners[1], &Hub);
        }
    }
    delete Clipper;
    return hr;
}

HRESULT
D9Device::DrawSoftware(
    _In_ D3DPRIMITIVETYPE Type,
    _In_ UINT PrimitiveCount,
    _In_ UINT First,
    _In_ UINT VertexCount,
    _In_ UINT MinIndex,
    _In_opt_ const VOID* IndexData,
    _In_ UINT IndexSize)
{
    D9VmProgram* Program;
    D9VmProgram* Owned;
    D9VertexMachine* Vm;
    D9Declaration* Output;
    D9PvRun Run = {};
    D9PvTarget Targets[D9VmDeclaredLimit];
    DWORD Format;
    WORD* Codes = nullptr;
    BYTE* Data;
    HRESULT hr;

    hr = PrepareSoftwareProgram(&Program, &Owned, &Vm);
    if (FAILED(hr))
        return hr;

    /* A version 3 shader's declared outputs lay out a declaration of the shader's own, the others a format */
    if (Program->Version >= D3DVS_VERSION(3, 0))
    {
        D3DVERTEXELEMENT9 Elements[D9VmDeclaredLimit + 1];
        UINT Count = D9PvModelThreeLayout(Program, Elements, Targets, &Run.Stride);

        Run.Targets = Targets;
        Run.TargetCount = Count;
        Output = VertexShader->Transformed;
        hr = S_OK;
        if (Output == nullptr)
        {
            Output = new (std::nothrow) D9Declaration(this);
            if (Output == nullptr)
                return E_OUTOFMEMORY;
            hr = Output->Initialize(Elements, Count, 0);
            if (FAILED(hr))
            {
                delete Output;
                return hr;
            }
            Output->Bind();
            Output->Orphaned = TRUE;
            VertexShader->Transformed = Output;
        }
    }
    else
    {
        Format = Program->OutputFormat();
        if (Format == 0)
        {
            D9_TRACE_ONCE(L"a vertex shader without a position drew under software vertex processing");
            delete Owned;
            return D3DERR_INVALIDCALL;
        }
        D9PvMakeLayout(Format, &Run.Layout);
        Run.Parts = Format;
        Run.Position = D3DFVF_XYZRHW;
        Run.Stride = Run.Layout.Stride;
        hr = KeepFvfDeclaration(Format, &Output);
    }

    /* The declaration of the transformed vertices reaches the driver when it changes */
    if (SUCCEEDED(hr) && Output->DriverHandle == NULL)
        hr = DriverDeclaration(Output, &Output->DriverHandle);
    if (FAILED(hr))
    {
        delete Owned;
        return hr;
    }
    if ((DrawDirty & D9DrawTransformedInput) != 0 || Switch.InputDeclaration != Output->DriverHandle)
        SwitchDeclaration(Output);
    DrawDirty &= ~D9DrawTransformedInput;

    Run.Program = Program;
    Run.Vm = Vm;
    Run.ClampColors = PixelShader == nullptr;
    Run.Clipping = RenderStates[D3DRS_CLIPPING] != FALSE;
    Run.Drawing = TRUE;
    Run.ClipIntersection = 0xFFFFFFFF;
    Run.Scale[0] = static_cast<FLOAT>(CurrentViewport.Width);
    Run.Scale[1] = -static_cast<FLOAT>(CurrentViewport.Height);
    Run.Scale[2] = CurrentViewport.MaxZ - CurrentViewport.MinZ;
    Run.Offset[0] = static_cast<FLOAT>(CurrentViewport.X) + 0.000001f;
    Run.Offset[1] = (static_cast<FLOAT>(CurrentViewport.Y) + static_cast<FLOAT>(CurrentViewport.Height)) + 0.000001f;
    Run.Offset[2] = CurrentViewport.MinZ;

    /* The guard band the driver offers, as a scale and offset of the half space position */
    Run.GuardBand = Caps.GuardBandLeft != 0.0f || Caps.GuardBandRight != 0.0f || Caps.GuardBandTop != 0.0f ||
                    Caps.GuardBandBottom != 0.0f;
    if (Run.GuardBand)
    {
        FLOAT X = static_cast<FLOAT>(CurrentViewport.X);
        FLOAT Y = static_cast<FLOAT>(CurrentViewport.Y);
        FLOAT Width = static_cast<FLOAT>(CurrentViewport.Width);
        FLOAT Height = static_cast<FLOAT>(CurrentViewport.Height);
        FLOAT Left = 1.0f - ((Caps.GuardBandLeft - X) * (2.0f / Width));
        FLOAT Right = ((Caps.GuardBandRight - X) * (2.0f / Width)) - 1.0f;
        FLOAT Bottom = ((Caps.GuardBandBottom - Y) * (2.0f / Height)) - 1.0f;
        FLOAT Top = 1.0f - ((Caps.GuardBandTop - Y) * (2.0f / Height));

        Run.BandScale[0] = 2.0f / (Right + Left);
        Run.BandOffset[0] = ((Left - 1.0f) * Run.BandScale[0]) * 0.5f;
        Run.BandScale[1] = 2.0f / (Top + Bottom);
        Run.BandOffset[1] = ((Bottom - 1.0f) * Run.BandScale[1]) * 0.5f;
    }
    for (UINT Index = 0; Index < D9StreamCount; Index++)
    {
        D9VertexBuffer* Source = static_cast<D9VertexBuffer*>(Streams[Index].Buffer);

        if (Source != nullptr && Source->Bytes() != nullptr)
            Run.Streams[Index] = Source->Bytes() + Streams[Index].Offset;
        Run.Strides[Index] = Streams[Index].Stride;
    }
    if (MemoryVertices != nullptr)
        Run.Streams[0] = static_cast<const BYTE*>(MemoryVertices);

    if (Run.Clipping)
    {
        Codes = new (std::nothrow) WORD[VertexCount];
        if (Codes == nullptr)
        {
            delete Owned;
            return E_OUTOFMEMORY;
        }
    }
    TransformedStride = Run.Stride;
    hr = LockTransformed(Run.Clipping ? 0 : 1, VertexCount * Run.Stride, &Data);
    if (SUCCEEDED(hr))
    {
        hr = D9PvRunVertices(&Run, Declaration, First, VertexCount, Data, Codes);

        /* A primitive wholly outside one plane draws nothing */
        if (SUCCEEDED(hr) && Run.Clipping && Run.ClipIntersection == 0 && Run.Targets == nullptr &&
            (Run.ClipUnion & (Run.GuardBand ? ~D9PvSidePlanes : 0xFFFFFFFF)) != 0 &&
            (Type == D3DPT_TRIANGLELIST ||
             (IndexData == nullptr && (Type == D3DPT_TRIANGLESTRIP || Type == D3DPT_TRIANGLEFAN))))
        {
            /* Some triangles cross the frustum and go through the clipper */
            hr = DrawClipped(&Run, Type, PrimitiveCount, Data, VertexCount, Codes, IndexData, IndexSize, MinIndex);
            if (IndexData != nullptr)
                TransformedUsed[TransformedBuffer] = TransformedStart + VertexCount * Run.Stride;
            else
                TransformedUsed[TransformedBuffer] = TransformedStart;
        }
        else if (SUCCEEDED(hr) && (Run.ClipIntersection == 0 || !Run.Clipping))
        {
            if ((DrawDirty & D9DrawTextures) != 0)
                FlushTextures();
            BindTransformed();
            SendTargets();
            SettleSwitch(Type, IndexData != nullptr);
            if (IndexData == nullptr)
            {
                D3DDDIARG_DRAWPRIMITIVE2 Draw = {};

                Draw.PrimitiveType = Type;
                Draw.FirstVertexOffset = TransformedStart;
                Draw.PrimitiveCount = PrimitiveCount;
                DdiDrawPrimitive2(&Draw);
            }
            else
            {
                D3DDDIARG_DRAWINDEXEDPRIMITIVE2 Draw = {};

                Draw.PrimitiveType = Type;
                Draw.BaseVertexOffset = static_cast<INT>(TransformedStart) - static_cast<INT>(MinIndex * Run.Stride);
                Draw.MinIndex = MinIndex;
                Draw.NumVertices = VertexCount;
                Draw.PrimitiveCount = PrimitiveCount;
                DdiDrawIndexedPrimitive2(&Draw, IndexSize, IndexData);
            }
            TransformedUsed[TransformedBuffer] = TransformedStart + VertexCount * Run.Stride;
        }
        UnlockDriverBuffer(&StagingBuffers[TransformedBuffer], TransformedAsync);
    }
    delete[] Codes;
    delete Owned;
    return hr;
}
