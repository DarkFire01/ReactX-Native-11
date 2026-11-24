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
    hr = S_OK;

    for (UINT Number = 0; Number < VertexCount; Number++, Vertex += Stride)
    {
        D9VmRegisters* Registers = &Vm->Registers;

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
            D9PvFetch(Data, Element->Type, Declared->Mask, Registers->Inputs[Declared->Register]);
        }
        if (FAILED(hr))
            break;
        Vm->Run(Program);

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
            if ((Written & D9VmWritesPointSize) != 0 && (Parts & D3DFVF_PSIZE) != 0)
                CopyMemory(Vertex + Layout.PointSize, &Registers->PointSize[0], sizeof(Registers->PointSize[0]));
            if ((Parts & D3DFVF_DIFFUSE) != 0)
            {
                DWORD Packed = 0xFFFFFFFF;

                if ((Written & D9VmWritesDiffuse) != 0)
                    Packed = D9PvPackColor(Registers->Colors[0]);
                if ((Written & D9VmWritesDiffuse) != 0 || !Keep)
                    CopyMemory(Vertex + Layout.Diffuse, &Packed, sizeof(Packed));
            }
            Specular = (Written & (D9VmWritesSpecular | D9VmWritesFog)) != 0;
            if ((Parts & D3DFVF_SPECULAR) != 0 && (Specular || !Keep))
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
    delete Fixed;
    return hr;
}
