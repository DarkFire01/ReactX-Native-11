/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 9 shader bytecode to Direct3D 10 tokens: analysis and the conversion entry
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "scprivate.hpp"

/* Direct3D 9 opcodes the analysis treats specially */
static constexpr UINT ScOpDcl = 0x1F;
static constexpr UINT ScOpDefB = 0x2F;
static constexpr UINT ScOpDefI = 0x30;
static constexpr UINT ScOpDef = 0x51;
static constexpr UINT ScOpComment = 0xFFFE;
static constexpr UINT ScOpEnd = 0xFFFF;

/* Vertex input and output usages the analysis adds outputs for */
static constexpr UINT ScUsagePosition = 0;
static constexpr UINT ScUsagePointSize = 4;
static constexpr UINT ScUsageTexcoord = 5;
static constexpr UINT ScUsagePositionT = 9;
static constexpr UINT ScUsageColor = 10;
static constexpr UINT ScUsageFog = 11;
static constexpr UINT ScUsageClip = 16;

/* Output register position, fog and point size share; fog is x and point size y */
static constexpr UINT ScFogRegister = 11;

class NineScTranslator
{
public:
    LONG References;
    UINT RuntimeVersion;
    ScAsm* Asm;
};

ScRegisterMap::ScRegisterMap()
{
    memset(SamplerKinds, ScNoRegister, sizeof(SamplerKinds));
    ZeroMemory(Inputs, sizeof(Inputs));
    memset(Temps, ScNoRegister, sizeof(Temps));
    memset(Special, ScNoRegister, 6);
    Special[6] = 0;
    Special[7] = 0;
}

ScShader::ScShader() :
    LoopCount(0),
    TempCount(0),
    Version(0),
    Flags(0),
    VertexFlags(0)
{
    for (UINT Buffer = 0; Buffer < NineScBufferCount; Buffer += 1)
    {
        RangeStart[Buffer] = ULONG_MAX;
        RangeEnd[Buffer] = 0;
        Relative[Buffer] = FALSE;
    }
    memset(OutputTemps, ScNoRegister, sizeof(OutputTemps));
    ZeroMemory(&Outputs, sizeof(Outputs));
    Inputs.UsageMask = 0;
    Inputs.Limit = 16;
}

UINT
ScRegType(
    _In_ DWORD Token)
{
    UINT Type = ((Token & 0x1800) | ((Token >> 20) & 0x7FF)) >> 8;

    /* The extra float constant banks are all constants */
    if (Type >= 11 && Type <= 13)
        return ScRegConst;
    return Type;
}

UINT
ScRegNumber(
    _In_ DWORD Token)
{
    UINT Type = ((Token & 0x1800) | ((Token >> 20) & 0x7FF)) >> 8;
    UINT Number = Token & 0x7FF;

    if (Type >= 11 && Type <= 13)
        Number |= (Type - 10) << 11;
    return Number;
}

UINT
ScTokenWriteMask(
    _In_ DWORD Token)
{
    return (Token >> 12) & ScMaskAll;
}

UINT
ScTokenSwizzle(
    _In_ DWORD Token,
    _In_ UINT Components)
{
    UINT Swizzle = 0;
    UINT Last = 0;
    UINT Component;

    for (Component = 0; Component < Components; Component += 1)
    {
        Last = (Token >> (16 + 2 * Component)) & 3;
        Swizzle |= 1 | (Last << (2 * Component + 1));
    }
    for (; Component < 4; Component += 1)
        Swizzle |= 1 | (Last << (2 * Component + 1));
    return Swizzle;
}

UINT
ScInstrLength(
    _In_ DWORD Token,
    _In_ DWORD Version)
{
    UINT Opcode = Token & 0xFFFF;

    if (Opcode == ScOpComment)
        return ((Token >> 16) & 0x7FFF) + 1;
    if (Opcode == ScOpDef || Opcode == ScOpDefI)
        return 6;
    if (Opcode == ScOpDefB)
        return 3;

    /* From vs_2_0 and ps_2_0 on, and in every pixel shader, the token carries the length */
    if ((Version & 0xFFFF0000) == 0xFFFF0000 || Version >= 0xFFFE0200)
        return ((Token >> 24) & 0xF) + 1;

    switch (Opcode)
    {
        case 1:
        case 6:
        case 7:
        case 14:
        case 15:
        case 16:
        case 19:
        case ScOpDcl:
        case 78:
        case 79:
            return 3;
        case 2:
        case 4:
        case 5:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 17:
        case 20:
        case 21:
        case 22:
        case 23:
        case 24:
            return 4;
        default:
            return 1;
    }
}

UINT
ScInputIndex(
    _In_ DWORD Version,
    _In_ UINT Register)
{
    /* Below ps_3_0 color inputs come after the position the pixel shader gets first */
    if ((Version & 0xFFFF0000) == 0xFFFF0000 && Version < 0xFFFF0300)
        return Register + 1;
    return Register;
}

/* Constant registers a matrix instruction reads from its source */
static
UINT
ScConstantSpan(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
        case 20:
        case 22:
            return 4;
        case 21:
        case 23:
            return 3;
        case 24:
            return 2;
        default:
            return 1;
    }
}

static
UINT
ScTextureKind(
    _In_ DWORD Token)
{
    switch (Token & 0x78000000)
    {
        case 0x10000000:
            return 1;
        case 0x18000000:
            return 2;
        case 0x20000000:
            return 3;
        default:
            return 0;
    }
}

static
BOOL
ScFindConstant(
    _In_ const ScShader* Shader,
    _In_ UINT Buffer,
    _In_ UINT Offset,
    _Out_opt_ NineScConstant* Found)
{
    for (const NineScConstant& Constant : Shader->Constants[Buffer])
    {
        if (Constant.Offset == Offset)
        {
            if (Found)
                *Found = Constant;
            return TRUE;
        }
    }
    return FALSE;
}

static
const NineScInput*
ScFindInput(
    _In_ const NineScInputs* Inputs,
    _In_ UINT Usage,
    _In_ UINT Index)
{
    for (const NineScInput& Input : Inputs->Inputs)
    {
        if (NineScInputUsage(Input) == Usage && NineScInputIndex(Input) == Index)
            return &Input;
    }
    return NULL;
}

static
VOID
ScAddInput(
    _Inout_ NineScInputs* Inputs,
    _In_ UINT Usage,
    _In_ UINT Index,
    _In_ UINT Register,
    _In_ UINT Flag,
    _In_ UINT Conversion)
{
    if (Inputs->Inputs.size() + 1 > Inputs->Limit)
        __fastfail(FAST_FAIL_INVALID_ARG);

    Inputs->Inputs.push_back(static_cast<NineScInput>((Usage & 0xF) | ((Index & 0xF) << 4) | ((Register & 0xF) << 8) |
                                                      ((Flag & 1) << 12) | ((Conversion & 3) << 13)));
    Inputs->UsageMask |= 1u << Usage;
}

/* Adds an output or widens the mask of the one already there */
static
VOID
ScAddOutput(
    _Inout_ NineScOutputs* Outputs,
    _In_ UINT Usage,
    _In_ UINT Index,
    _In_ UINT Register,
    _In_ UINT Mask,
    _In_ BOOL Indexed)
{
    UINT Components = (Mask >> 16) & 0xF;
    UINT Slot;

    if (Outputs->Count >= NineScMaxOutputs)
        __fastfail(FAST_FAIL_INVALID_ARG);

    for (Slot = 0; Slot < Outputs->Count; Slot += 1)
    {
        NineScOutput* Output = &Outputs->Outputs[Slot];

        if ((*Output & 0x1F) == Usage && ((*Output >> 5) & 0xF) == Index)
        {
            *Output |= Components << 13;
            return;
        }
    }

    Outputs->Outputs[Slot] = (Outputs->Outputs[Slot] & ~0x1FFFFu) | (Usage & 0x1F) | ((Index & 0xF) << 5) |
                             ((Register & 0xF) << 9) | (Components << 13);
    Outputs->Count = Slot + 1;
    if (Indexed)
        Outputs->Indexed = (Outputs->Indexed | (1ull << Slot)) & ((1ull << 45) - 1);

    switch (Usage)
    {
        case ScUsageColor:
            if (Index <= 1)
                Outputs->Flags |= 2u << Index;
            break;
        case ScUsageFog:
            if (Index == 0)
                Outputs->Flags |= 0x80000;
            break;
        case 14:
            Outputs->Flags |= 0x200000;
            break;
        case 15:
            Outputs->Flags |= 0x400000;
            break;
        case ScUsagePosition:
        case ScUsagePositionT:
            if (Index == 0)
                Outputs->Flags |= 1;
            break;
        case ScUsageTexcoord:
            Outputs->Flags = (Outputs->Flags & 0xFFF80007) | ((Outputs->Flags | (8u << Index)) & 0x7FFF8);
            break;
        case ScUsagePointSize:
            if (Index == 0)
                Outputs->Flags |= 0x100000;
            break;
    }
}

/* Clip distances go to one or two outputs after the shader's own */
static
VOID
ScAddClipOutputs(
    _Inout_ NineScOutputs* Outputs,
    _In_ UINT Planes)
{
    UINT Count = 0;
    UINT Mask;
    UINT Register = 13;
    UINT Index = 0;

    for (UINT Bits = Planes; Bits != 0; Bits >>= 1)
    {
        if (Bits & 1)
            Count += 1;
    }

    switch (Count & 3)
    {
        case 0:
            Mask = 0xF0000;
            break;
        case 1:
            Mask = 0x10000;
            break;
        case 2:
            Mask = 0x30000;
            break;
        default:
            Mask = 0x70000;
            break;
    }

    if (Count > 4)
    {
        ScAddOutput(Outputs, ScUsageClip, 0, 13, 0xF0000, FALSE);
        Index = 1;
        Register = 14;
    }
    ScAddOutput(Outputs, ScUsageClip, Index, Register, Mask, FALSE);
}

/* State the vertex shader walk keeps between instructions */
struct ScVertexWalk
{
    ScShader* Shader;
    const NineScInputs* Declared;
    UINT RuntimeVersion;
    BYTE NextTemp;
    BOOLEAN Relative;
    UINT Opcode;
};

/* Records one source register: temps get a Direct3D 10 temp, constants widen their buffer range */
static
HRESULT
ScWalkVertexSource(
    _Inout_ ScVertexWalk* Walk,
    _In_ DWORD Token)
{
    ScShader* Shader = Walk->Shader;
    UINT Number = ScRegNumber(Token);
    BYTE* Slot = NULL;

    switch (ScRegType(Token))
    {
        case ScRegTemp:
            Slot = &Shader->Map.Temps[Number];
            break;

        case ScRegInput:
        case ScRegSampler:
        case ScRegLabel:
            return S_OK;

        case ScRegConst:
        {
            UINT End = Number + ScConstantSpan(Walk->Opcode);

            if (Token & 0x2000)
                Walk->Relative = TRUE;

            for (UINT Register = Number; Register < End; Register += 1)
            {
                if (!Walk->Relative && Walk->RuntimeVersion >= 9 &&
                    ScFindConstant(Shader, NineScFloatBuffer, 4 * Register, NULL))
                {
                    continue;
                }
                if (Register < Shader->RangeStart[NineScFloatBuffer])
                    Shader->RangeStart[NineScFloatBuffer] = Register;
                if (Register + 1 > Shader->RangeEnd[NineScFloatBuffer])
                    Shader->RangeEnd[NineScFloatBuffer] = Register + 1;
            }
            return S_OK;
        }

        case ScRegAddress:
            Slot = &Shader->Map.Special[ScSlotAddress];
            break;

        case ScRegConstInt:
            if (Walk->RuntimeVersion < 9 || !ScFindConstant(Shader, NineScIntegerBuffer, 4 * Number, NULL))
            {
                if (Number < Shader->RangeStart[NineScIntegerBuffer])
                    Shader->RangeStart[NineScIntegerBuffer] = Number;
                if (Number + 1 > Shader->RangeEnd[NineScIntegerBuffer])
                    Shader->RangeEnd[NineScIntegerBuffer] = Number + 1;
            }
            return S_OK;

        case ScRegConstBool:
            if (Walk->RuntimeVersion < 9 || !ScFindConstant(Shader, NineScBooleanBuffer, Number, NULL))
            {
                if (Number < Shader->RangeStart[NineScBooleanBuffer])
                    Shader->RangeStart[NineScBooleanBuffer] = Number;
                if (Number + 1 > Shader->RangeEnd[NineScBooleanBuffer])
                    Shader->RangeEnd[NineScBooleanBuffer] = Number + 1;
            }
            return S_OK;

        case ScRegLoop:
            Slot = &Shader->Map.Special[ScSlotLoop];
            break;

        case ScRegPredicate:
            Slot = &Shader->Map.Special[ScSlotPredicate];
            break;

        default:
            return E_FAIL;
    }

    if (*Slot == ScNoRegister)
    {
        *Slot = Walk->NextTemp;
        Walk->NextTemp += 1;
    }
    return S_OK;
}

/* Records one destination register and the output it implies */
static
HRESULT
ScWalkVertexDestination(
    _Inout_ ScVertexWalk* Walk,
    _In_ DWORD Token)
{
    ScShader* Shader = Walk->Shader;
    NineScOutputs* Outputs = &Shader->Outputs;
    UINT Number = Token & 0x7FF;
    BYTE* Slot;

    switch (((Token & 0x1800) | ((Token >> 20) & 0x7FF)) >> 8)
    {
        case ScRegTemp:
            Slot = &Shader->Map.Temps[Number];
            break;

        case ScRegAddress:
            Slot = &Shader->Map.Special[ScSlotAddress];
            break;

        case ScRegRasterOut:
            if (Number == 0)
            {
                if (!(Outputs->Flags & 1))
                    ScAddOutput(Outputs, ScUsagePosition, 0, 0, 0xF0000, FALSE);
                Slot = &Shader->OutputTemps[0];
            }
            else if (Number == 1)
            {
                if (!(Outputs->Flags & 0x80000))
                    ScAddOutput(Outputs, ScUsageFog, 0, ScFogRegister, 0x10000, FALSE);
                Slot = &Shader->OutputTemps[ScFogRegister];
            }
            else if (Number == 2)
            {
                if (!(Outputs->Flags & 0x100000))
                    ScAddOutput(Outputs, ScUsagePointSize, 0, ScFogRegister, 0x20000, FALSE);
                Slot = &Shader->OutputTemps[ScFogRegister];
            }
            else
            {
                return E_FAIL;
            }
            break;

        case ScRegAttributeOut:
            if (!((1u << Number) & (Outputs->Flags >> 1) & 3))
                ScAddOutput(Outputs, ScUsageColor, Number, Number + 1, 0xF0000, FALSE);
            Slot = &Shader->OutputTemps[Number + 1];
            break;

        case ScRegOutput:
            if (Shader->Version >= 0xFFFE0300)
            {
                if (Token & 0x2000)
                    Shader->VertexFlags |= ScVertexIndexedOutputs;
                return S_OK;
            }
            ScAddOutput(Outputs, ScUsageTexcoord, Number, Number + 3, Token & 0xF0000, FALSE);
            Slot = &Shader->OutputTemps[Number + 3];
            break;

        case ScRegPredicate:
            Slot = &Shader->Map.Special[ScSlotPredicate];
            break;

        default:
            return E_FAIL;
    }

    if (*Slot == ScNoRegister)
    {
        *Slot = Walk->NextTemp;
        Walk->NextTemp += 1;
    }
    return S_OK;
}

/* Instructions whose first parameter is a source, not a destination */
static
BOOL
ScHasNoDestination(
    _In_ UINT Opcode)
{
    switch (Opcode)
    {
        case 25:
        case 26:
        case 27:
        case 30:
        case 38:
        case 40:
        case 41:
        case 44:
        case 45:
        case 65:
        case 96:
            return TRUE;
        default:
            return FALSE;
    }
}

static
HRESULT
ScAnalyzeVertexShader(
    _In_ NineScTranslator* Translator,
    _In_reads_(Count) const DWORD* Code,
    _In_ UINT Count,
    _In_ UINT Flags,
    _In_ const NineScRasterStates* Raster,
    _In_ const NineScInputs* Declared,
    _Out_ ScShader** Result)
{
    ScVertexWalk Walk;
    ScShader* Shader;
    NineScInputs Inputs;
    const DWORD* Token;
    DWORD Version;
    HRESULT hr = S_OK;

    if (!Code || !Result || !Count)
        return E_INVALIDARG;

    Version = Code[0];
    switch (Version)
    {
        case 0xFFFE0100:
        case 0xFFFE0101:
        case 0xFFFE0200:
        case 0xFFFE0201:
        case 0xFFFE02FF:
        case 0xFFFE0300:
            break;
        default:
            return E_INVALIDARG;
    }

    Shader = new (std::nothrow) ScShader();
    if (!Shader)
        return E_OUTOFMEMORY;
    Shader->Version = Version;
    Inputs.UsageMask = 0;
    Inputs.Limit = 16;

    Walk.Shader = Shader;
    Walk.Declared = Declared;
    Walk.RuntimeVersion = Translator->RuntimeVersion;
    Walk.NextTemp = ScFirstShaderTemp;
    Walk.Relative = FALSE;

    Shader->Code.reserve(Count);
    Shader->Code.push_back(Version);

    Token = Code + 1;
    while (*Token != ScOpEnd)
    {
        UINT Length;
        UINT Opcode;

        if (static_cast<UINT>(reinterpret_cast<const BYTE*>(Token) - reinterpret_cast<const BYTE*>(Code)) > Count)
        {
            hr = E_FAIL;
            break;
        }

        Length = ScInstrLength(*Token, Version);
        Opcode = *Token & 0xFFFF;
        if (Opcode == 0)
        {
            Token += Length;
            continue;
        }

        if (Opcode == ScOpDcl)
        {
            DWORD Usage = Token[1];
            DWORD Register = Token[2];
            UINT Number = Register & 0x7FF;

            switch (((Register & 0x1800) | ((Register >> 20) & 0x7FF)) >> 8)
            {
                case ScRegInput:
                {
                    const NineScInput* Match = ScFindInput(Declared, Usage & 0xF, (Usage >> 16) & 0xF);
                    UINT Flag = Match ? (*Match >> 12) & 1 : 0;
                    UINT Conversion = Match ? NineScInputConversion(*Match) : 0;

                    ScAddInput(&Inputs, Usage & 0xF, (Usage >> 16) & 0xF, Number, Flag, Conversion);
                    break;
                }

                case ScRegOutput:
                    ScAddOutput(&Shader->Outputs, Usage & 0xF, (Usage >> 16) & 0xF, Number, Register & 0xF0000, FALSE);
                    if (Shader->OutputTemps[Number] == ScNoRegister)
                    {
                        Shader->OutputTemps[Number] = Walk.NextTemp;
                        Walk.NextTemp += 1;
                    }
                    break;

                case ScRegSampler:
                    Shader->Map.SamplerKinds[Number] = static_cast<BYTE>(ScTextureKind(Usage));
                    break;

                default:
                    hr = E_FAIL;
                    break;
            }
            if (FAILED(hr))
                break;
            Token += Length;
            continue;
        }

        if (Opcode == ScOpDefB)
        {
            NineScConstant Constant;

            /* Only the register and the value are set; the rest stays as the stack left it */
            ZeroMemory(&Constant, sizeof(Constant));
            Constant.Offset = ScRegNumber(Token[1]);
            Constant.Value[0] = Token[2];
            Shader->Constants[NineScBooleanBuffer].push_back(Constant);
            Token += Length;
            continue;
        }

        if (Opcode == ScOpDefI || Opcode == ScOpDef)
        {
            NineScConstant Constant;

            Constant.Offset = 4 * ScRegNumber(Token[1]);
            memcpy(Constant.Value, &Token[2], sizeof(Constant.Value));
            Shader->Constants[Opcode == ScOpDef ? NineScFloatBuffer : NineScIntegerBuffer].push_back(Constant);
            Token += Length;
            continue;
        }

        if (Opcode == ScOpComment)
        {
            Token += Length;
            continue;
        }

        Walk.Opcode = Opcode;
        Shader->Code.push_back(*Token);
        Token += 1;

        if (*Token & 0x80000000)
        {
            DWORD OpcodeToken = *(Token - 1);

            if (ScHasNoDestination(Opcode))
            {
                if (Opcode == 27 || Opcode == 38)
                    Shader->LoopCount += 1;
            }
            else
            {
                Shader->Code.push_back(*Token);
                hr = ScWalkVertexDestination(&Walk, *Token);
                if (FAILED(hr))
                    break;
                Token += 1;
            }

            /* The predicate token is copied as it is */
            if (OpcodeToken & 0x10000000)
            {
                Shader->Code.push_back(*Token);
                Token += 1;
            }

            while (*Token & 0x80000000)
            {
                Shader->Code.push_back(*Token);
                hr = ScWalkVertexSource(&Walk, *Token);
                if (FAILED(hr))
                    break;
                Token += 1;
            }
            if (FAILED(hr))
                break;
        }
    }

    if (SUCCEEDED(hr))
    {
        Shader->Code.push_back(ScOpEnd);

        if (Shader->VertexFlags & ScVertexIndexedOutputs)
            memset(Shader->OutputTemps, ScNoRegister, sizeof(Shader->OutputTemps));

        for (const NineScInput& Input : Inputs.Inputs)
        {
            ScInputRegister* Entry;

            if (!ScFindInput(Declared, NineScInputUsage(Input), NineScInputIndex(Input)))
                continue;

            Entry = &Shader->Map.Inputs[NineScInputRegister(Input)];
            if (NineScInputConversion(Input))
            {
                Entry->Kind = ScInputTemp;
                Entry->Register = Walk.NextTemp;
                Walk.NextTemp += 1;
            }
            else
            {
                Entry->Kind = ScInputDirect;
                Entry->Register = static_cast<BYTE>(NineScInputRegister(Input));
            }
            Entry->Mask = 0xF;
        }

        if (Raster->Words[1] & 0x3F00)
            ScAddClipOutputs(&Shader->Outputs, (Raster->Words[1] >> 8) & 0x3F);

        Shader->Inputs = Inputs;
        Shader->RangeStart[NineScFloatBuffer] *= 4;
        Shader->RangeStart[NineScIntegerBuffer] *= 4;
        Shader->RangeEnd[NineScFloatBuffer] *= 4;
        Shader->RangeEnd[NineScIntegerBuffer] *= 4;
        Shader->Relative[NineScFloatBuffer] = Walk.Relative;
        Shader->TempCount = Walk.NextTemp;
        Shader->Flags = Flags;
        *Result = Shader;
        return S_OK;
    }

    delete Shader;
    return hr;
}

static
HRESULT
ScTranslateVertexShader(
    _In_ NineScTranslator* Translator,
    _In_ ScShader* Shader,
    _In_ const NineScRasterStates* Raster,
    _Out_ std::vector<DWORD>* Tokens)
{
    ScAsm* Asm = Translator->Asm;
    ScVertexContext* Context;
    HRESULT hr;

    Context = new (std::nothrow) ScVertexContext(Translator->RuntimeVersion, Asm, Raster, Shader);
    if (!Context)
        return E_OUTOFMEMORY;

    Asm->Begin(D3D10_SB_VERTEX_SHADER, Shader->Flags);
    hr = Context->WriteDeclarations();
    if (SUCCEEDED(hr))
        hr = Context->TranslateInstructions();
    if (FAILED(hr))
    {
        delete Context;
        return hr;
    }

    Context->WriteOutputs();
    delete Context;

    Asm->EndShader();
    Tokens->assign(Asm->GetShader(), Asm->GetShader() + Asm->ShaderSizeInDWORDs());
    return S_OK;
}

HRESULT
NineScConvertShader(
    _Inout_ NineScTranslator** Translator,
    _Inout_ NineScArgs* Args)
{
    NineScTranslator* Owner = *Translator;
    std::vector<DWORD> Tokens;
    ScShader* Shader = NULL;
    BOOL Produced = FALSE;
    HRESULT hr = Args->Code ? S_OK : E_INVALIDARG;

    if (!Owner)
    {
        Owner = new (std::nothrow) NineScTranslator();
        if (!Owner)
            return E_OUTOFMEMORY;
        Owner->References = 1;
        Owner->RuntimeVersion = Args->Version;
        Owner->Asm = new (std::nothrow) ScAsm();
        if (!Owner->Asm)
        {
            delete Owner;
            return E_OUTOFMEMORY;
        }
        *Translator = Owner;
    }
    if (FAILED(hr))
        return hr;

    if (Args->PixelShader)
    {
        hr = E_NOTIMPL;
    }
    else
    {
        hr = ScAnalyzeVertexShader(Owner, Args->Code, static_cast<UINT>(Args->CodeSize), Args->Flags, Args->Raster,
                                   Args->VertexInputs, &Shader);
        if (SUCCEEDED(hr))
        {
            Args->BufferSizes[NineScFloatBuffer] =
                Shader->Relative[NineScFloatBuffer] ? 1024 : Shader->RangeEnd[NineScFloatBuffer];
            Args->BufferSizes[NineScIntegerBuffer] = Shader->RangeEnd[NineScIntegerBuffer];
            Args->BufferSizes[NineScBooleanBuffer] = Shader->RangeEnd[NineScBooleanBuffer];
            *Args->VertexInputs = Shader->Inputs;
            *Args->VertexOutputs = Shader->Outputs;

            hr = ScTranslateVertexShader(Owner, Shader, Args->Raster, &Tokens);
            Produced = SUCCEEDED(hr);
            for (UINT Buffer = 0; Buffer < NineScBufferCount; Buffer += 1)
                Args->Constants[Buffer] = Shader->Constants[Buffer];
            delete Shader;
        }
    }

    Args->Emitted = Owner->Asm->Emitted;
    Args->Patched = Owner->Asm->Patched;
    if (FAILED(hr) || !Produced)
        return hr;

    Args->Tokens = malloc(Tokens.size() * sizeof(DWORD));
    if (!Args->Tokens)
    {
        Args->TokenSize = 0;
        return E_OUTOFMEMORY;
    }
    memcpy(Args->Tokens, Tokens.data(), Tokens.size() * sizeof(DWORD));
    Args->TokenSize = Tokens.size() * sizeof(DWORD);
    return S_OK;
}

VOID
NineScDestroyTranslator(
    _In_opt_ NineScTranslator* Translator)
{
    if (!Translator)
        return;
    delete Translator->Asm;
    delete Translator;
}
