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

/* D3DPV_DONOTCOPYDATA */
static constexpr DWORD D9PvKeepData = 0x1;

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

/* Reads one element of a vertex into an input register; parts the type lacks read as (0, 0, 0, 1) */
static
VOID
D9PvFetch(
    _In_ const BYTE* Data,
    _In_ BYTE Type,
    _Out_writes_(4) D9VmValue* Register)
{
    FLOAT Values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    UINT Count = 0;

    switch (Type)
    {
    case D3DDECLTYPE_FLOAT1:
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_FLOAT3:
    case D3DDECLTYPE_FLOAT4:
        Count = Type + 1;
        CopyMemory(Values, Data, Count * sizeof(*Values));
        break;
    case D3DDECLTYPE_D3DCOLOR:
    {
        static const DWORD Scale = 0x3B808081;
        FLOAT Step;

        CopyMemory(&Step, &Scale, sizeof(Step));
        Values[0] = static_cast<FLOAT>(Data[2]) * Step;
        Values[1] = static_cast<FLOAT>(Data[1]) * Step;
        Values[2] = static_cast<FLOAT>(Data[0]) * Step;
        Values[3] = static_cast<FLOAT>(Data[3]) * Step;
        Count = 4;
        break;
    }
    case D3DDECLTYPE_UBYTE4:
        for (UINT Part = 0; Part < 4; Part++)
            Values[Part] = static_cast<FLOAT>(Data[Part]);
        Count = 4;
        break;
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT4:
        Count = Type == D3DDECLTYPE_SHORT2 ? 2 : 4;
        for (UINT Part = 0; Part < Count; Part++)
        {
            SHORT Value;

            CopyMemory(&Value, Data + Part * sizeof(Value), sizeof(Value));
            Values[Part] = static_cast<FLOAT>(Value);
        }
        break;
    default:
        return;
    }
    for (UINT Part = 0; Part < 4; Part++)
        Register[Part].Float = Values[Part];
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
    BYTE* Base;
    BYTE* Vertex;
    FLOAT Scale[3];
    FLOAT Offset[3];
    DWORD Position;
    BOOLEAN Clipping = RenderStates[D3DRS_CLIPPING] != FALSE;
    HRESULT hr;

    if (Dest == nullptr)
        return D3DERR_INVALIDCALL;
    Dest->GetDesc(&Desc);
    D9PvMakeLayout(Desc.FVF, &Layout);
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

    /* The fixed function pipeline has no interpreted form yet */
    if (VertexShader == nullptr)
    {
        D9Trace(L"ProcessVertices: no vertex shader");
        return D3DERR_INVALIDCALL;
    }
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
    if (Machine == nullptr)
    {
        Machine = new (std::nothrow) D9VertexMachine();
        if (Machine == nullptr)
            return E_OUTOFMEMORY;
        hr = Machine->Initialize(D9PvFloatRegisters, D9PvIntRegisters, D9PvIntRegisters);
        if (FAILED(hr))
        {
            delete Machine;
            Machine = nullptr;
            return hr;
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
    ZeroMemory(Machine->FloatConstants, D9PvFloatRegisters * sizeof(*Machine->FloatConstants));
    ZeroMemory(Machine->IntConstants, D9PvIntRegisters * sizeof(*Machine->IntConstants));
    ZeroMemory(Machine->BoolConstants, D9PvIntRegisters * sizeof(*Machine->BoolConstants));
    if ((BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0)
    {
        /* A mixed or software device's bank is never wider than the interpreter's */
        CopyMemory(Machine->FloatConstants, VertexFloats, Limits.VertexFloats * sizeof(*VertexFloats));
        CopyMemory(Machine->IntConstants, VertexInts, Limits.VertexInts * sizeof(*VertexInts));
        CopyMemory(Machine->BoolConstants, VertexBools, Limits.VertexBools * sizeof(*VertexBools));
    }
    for (UINT Index = 0; Index < Program->DefinitionCount; Index++)
    {
        const DWORD* Tokens = &Program->Code[Program->Definitions[Index]];
        UINT Register = Tokens[1] & 0x7FF;

        if (LOWORD(Tokens[0]) == D3DSIO_DEF && Register < D9PvFloatRegisters)
            CopyMemory(Machine->FloatConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFI && Register < D9PvIntRegisters)
            CopyMemory(Machine->IntConstants[Register], &Tokens[2], 4 * sizeof(DWORD));
        else if (LOWORD(Tokens[0]) == D3DSIO_DEFB && Register < D9PvIntRegisters)
            Machine->BoolConstants[Register].Bits = Tokens[2];
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
        return D3DERR_INVALIDCALL;
    }

    /* A declaration destination starts where the last format destination's vertex size puts it */
    if (Output == nullptr)
        ProcessedStride = Stride;
    Vertex = Base + static_cast<SIZE_T>(ProcessedStride) * DestIndex;
    hr = S_OK;

    for (UINT Number = 0; Number < VertexCount; Number++, Vertex += Stride)
    {
        D9VmRegisters* Registers = &Machine->Registers;

        for (UINT Index = 0; Index < Program->InputCount; Index++)
        {
            const D9VmDeclared* Declared = &Program->Inputs[Index];
            const D3DVERTEXELEMENT9* Element = nullptr;
            const D9Stream* Stream;
            D9VertexBuffer* Source;
            const BYTE* Data;

            if (Declaration != nullptr)
                Element = D9PvFindElement(Declaration, Declared->Usage, Declared->Index);
            if (Element == nullptr)
            {
                if (Program->Version >= D3DVS_VERSION(2, 0))
                    continue;
                D9Trace(L"ProcessVertices: shader input usage %u index %u is not in the declaration", Declared->Usage,
                        Declared->Index);
                hr = D3DERR_INVALIDCALL;
                break;
            }
            Stream = &Streams[Element->Stream];
            Source = static_cast<D9VertexBuffer*>(Stream->Buffer);
            if (Source == nullptr || Source->Bytes() == nullptr || Declared->Register >= D9VmInputCount)
                continue;
            Data = Source->Bytes() + Stream->Offset + static_cast<SIZE_T>(Stream->Stride) * (SrcStartIndex + Number) +
                   Element->Offset;
            D9PvFetch(Data, Element->Type, Registers->Inputs[Declared->Register]);
        }
        if (FAILED(hr))
            break;
        Machine->Run(Program);

        if (Program->Version >= D3DVS_VERSION(3, 0))
        {
            UINT Count = Output != nullptr ? TargetCount : Program->OutputCount;

            for (UINT Index = 0; Index < Count; Index++)
            {
                UINT Register = Output != nullptr ? Targets[Index].Register : Program->Outputs[Index].Register;
                UINT Usage = Output != nullptr ? Targets[Index].Usage : Program->Outputs[Index].Usage;
                UINT UsageIndex = Output != nullptr ? Targets[Index].Index : Program->Outputs[Index].Index;
                const D9VmValue* Raw = Registers->Outputs[Register < D9VmOutputCount ? Register : 0];
                D9VmValue Values[4];
                BOOLEAN IsPosition = Output != nullptr ? (Usage == D3DDECLUSAGE_POSITIONT && UsageIndex == 0) :
                                                         (Usage == D3DDECLUSAGE_POSITION && UsageIndex == 0);

                CopyMemory(Values, Raw, sizeof(Values));
                if (IsPosition)
                {
                    FLOAT W = Raw[3].Float;
                    FLOAT HalfX = (W + Raw[0].Float) * 0.5f;
                    FLOAT HalfY = (W + Raw[1].Float) * 0.5f;
                    FLOAT Reciprocal = D9PvReciprocal(W);

                    Values[0].Float = ((Reciprocal * HalfX) * Scale[0]) + Offset[0];
                    Values[1].Float = ((Reciprocal * HalfY) * Scale[1]) + Offset[1];
                    Values[2].Float = ((Raw[2].Float * Reciprocal) * Scale[2]) + Offset[2];
                    Values[3].Float = Reciprocal;
                }

                if (Output != nullptr)
                {
                    BYTE* Field = Vertex + Targets[Index].Offset;

                    if (Targets[Index].Type == D3DDECLTYPE_D3DCOLOR)
                    {
                        DWORD Packed = D9PvPackColor(Values);

                        CopyMemory(Field, &Packed, sizeof(Packed));
                        continue;
                    }
                    for (UINT Part = 0; Part < 4; Part++)
                    {
                        if ((Targets[Index].Mask & (1 << Part)) == 0)
                            continue;
                        CopyMemory(Field, &Values[Part], sizeof(Values[Part]));
                        Field += sizeof(Values[Part]);
                    }
                    continue;
                }

                switch (Usage)
                {
                case D3DDECLUSAGE_PSIZE:
                    CopyMemory(Vertex + Layout.PointSize, &Raw[0], sizeof(Raw[0]));
                    break;
                case D3DDECLUSAGE_TEXCOORD:
                    if (UsageIndex < Layout.CoordinateCount)
                        CopyMemory(Vertex + Layout.Coordinates[UsageIndex], Raw, Layout.CoordinateSizes[UsageIndex]);
                    break;
                case D3DDECLUSAGE_POSITION:
                    if (UsageIndex != 0)
                        break;
                    if (Position == D3DFVF_XYZRHW)
                        CopyMemory(Vertex, Values, 4 * sizeof(*Values));
                    else if (Position == D3DFVF_XYZ)
                        CopyMemory(Vertex, Raw, 3 * sizeof(*Raw));
                    else if (Position == D3DFVF_XYZW)
                        CopyMemory(Vertex, Raw, 4 * sizeof(*Raw));
                    break;
                case D3DDECLUSAGE_COLOR:
                {
                    DWORD Packed = D9PvPackColor(Raw);

                    CopyMemory(Vertex + (UsageIndex != 0 ? Layout.Specular : Layout.Diffuse), &Packed, sizeof(Packed));
                    break;
                }
                case D3DDECLUSAGE_FOG:
                    Vertex[Layout.Specular + 3] = static_cast<BYTE>(D9PvChannel(Raw[0].Float));
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
            BOOLEAN Specular;

            if (Position == D3DFVF_XYZ || Position == D3DFVF_XYZW)
            {
                CopyMemory(Vertex, Raw, (Position == D3DFVF_XYZW ? 4 : 3) * sizeof(*Raw));
            }
            else
            {
                FLOAT W = Raw[3].Float;
                FLOAT Z = Raw[2].Float;
                FLOAT SumX = Raw[0].Float + W;
                FLOAT SumY = Raw[1].Float + W;
                FLOAT Reciprocal;
                FLOAT Screen[4];

                /* Inside the frustum the system divides without looking at w */
                if (Clipping && !D9PvOutside(SumX * 0.5f, SumY * 0.5f, Z, W))
                    Reciprocal = 1.0f / W;
                else
                    Reciprocal = D9PvReciprocal(W);
                Screen[0] = (((SumX * 0.5f) * Reciprocal) * Scale[0]) + Offset[0];
                Screen[1] = (((SumY * 0.5f) * Reciprocal) * Scale[1]) + Offset[1];
                Screen[2] = ((Scale[2] * Z) * Reciprocal) + Offset[2];
                Screen[3] = Reciprocal;
                CopyMemory(Vertex, Screen, sizeof(Screen));
            }
            if ((Written & D9VmWritesPointSize) != 0 && (Desc.FVF & D3DFVF_PSIZE) != 0)
                CopyMemory(Vertex + Layout.PointSize, &Registers->PointSize[0], sizeof(Registers->PointSize[0]));
            if ((Desc.FVF & D3DFVF_DIFFUSE) != 0)
            {
                DWORD Packed = 0xFFFFFFFF;

                if ((Written & D9VmWritesDiffuse) != 0)
                    Packed = D9PvPackColor(Registers->Colors[0]);
                if ((Written & D9VmWritesDiffuse) != 0 || (Flags & D9PvKeepData) == 0)
                    CopyMemory(Vertex + Layout.Diffuse, &Packed, sizeof(Packed));
            }
            Specular = (Written & (D9VmWritesSpecular | D9VmWritesFog)) != 0;
            if ((Desc.FVF & D3DFVF_SPECULAR) != 0 && (Specular || (Flags & D9PvKeepData) == 0))
            {
                DWORD Packed = 0;

                if ((Written & D9VmWritesSpecular) != 0)
                    Packed = D9PvPackColor(Registers->Colors[1]);
                if ((Written & D9VmWritesFog) != 0)
                    Packed = (Packed & 0x00FFFFFF) | (D9PvChannel(Registers->Fog[0].Float) << 24);
                CopyMemory(Vertex + Layout.Specular, &Packed, sizeof(Packed));
            }
            for (UINT Set = 0; Set < Layout.CoordinateCount; Set++)
                CopyMemory(Vertex + Layout.Coordinates[Set], Registers->Outputs[Set], Layout.CoordinateSizes[Set]);
        }
    }

    Dest->Unlock();
    return hr;
}
