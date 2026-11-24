/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Draw time state: converted shaders, pipelines, constants, views and the draws themselves
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "scprivate.hpp"

#include <algorithm>

/* Constant slot sizes start at 1 MB and hand out 256 byte aligned pieces */
static constexpr UINT NineSlotCapacity = 0x100000;
static constexpr UINT NineSlotAlignment = 0x100;
static constexpr UINT NineConstantsBound = 256;

/* User memory vertices and indices go through a 32 MB buffer of 4 byte aligned pieces */
static constexpr UINT NineUserCapacity = 0x2000000;
static constexpr UINT NineUserAlignment = 4;

/* The 4 component mapping that keeps every channel */
static constexpr UINT NineIdentityMapping = 0x1688;

/* Register numbers of the extension constants */
static constexpr UINT NineExtPixelScale = 0;
static constexpr UINT NineExtPointSize = 1;
static constexpr UINT NineExtClipPlanes = 2;
static constexpr UINT NineExtScreenOffset = 8;
static constexpr UINT NineExtScreenScale = 9;

/* Raster state word 1 fields */
static constexpr UINT NineRasterPrimitiveShift = 18;
static constexpr UINT NineRasterPointLarge = 0x40000000;
static constexpr UINT NineRasterPointSprite = 0x80000000;
static constexpr UINT NineRasterAlphaFunc = 0x1E00000;
static constexpr UINT NineRasterAlphaTest = 0x20000000;
static constexpr UINT NineRasterClipPlanes = 0x3F00;

/* Vertex output usages the pixel stage treats on its own */
static constexpr UINT NineOutputClip = 16;
static constexpr UINT NineOutputPointSize = 0x100000;

/* Where a draw's vertices or indices start: a byte offset, a vertex or an element */
enum NineOffsetKind
{
    NineOffsetBytes,
    NineOffsetVertex,
    NineOffsetElement
};

static
FLOAT*
NineExtensionRegister(
    _Inout_ NineDevice* Device,
    _In_ UINT Register)
{
    return reinterpret_cast<FLOAT*>(Device->Extension + Register * 16);
}

static
VOID
NineInitSlot(
    _Inout_ NineConstantSlot* Slot,
    _In_ NineDevice* Device)
{
    Slot->Ring.Owner = Device;
    Slot->Ring.Capacity = NineSlotCapacity;
    Slot->Ring.Alignment = NineSlotAlignment;
    Slot->Ring.Cursor = NineSlotCapacity;
}

VOID
NineInitDrawState(
    _Inout_ NineDevice* Device)
{
    for (UINT Kind = 0; Kind < NineConstantKindCount; Kind += 1)
    {
        NineInitSlot(&Device->VertexSlots[Kind], Device);
        NineInitSlot(&Device->PixelSlots[Kind], Device);
        Device->VertexSlots[Kind].Current = Device->ZeroConstants;
        Device->PixelSlots[Kind].Current = Device->ZeroConstants;
    }
    NineInitSlot(&Device->VertexExtension, Device);
    NineInitSlot(&Device->GeometryExtension, Device);
    NineInitSlot(&Device->PixelFog, Device);
    NineInitSlot(&Device->PixelBump, Device);
    Device->UserRing.Owner = Device;
    Device->UserRing.Capacity = NineUserCapacity;
    Device->UserRing.Alignment = NineUserAlignment;
    Device->UserRing.Cursor = NineUserCapacity;
    Device->FanRing.Owner = Device;
    Device->FanRing.Alignment = NineSlotAlignment;

    Device->TransformedVertex.Stage = NineVertexStage;
    Device->PointGeometry.Stage = NineVertexStage;
    for (UINT Index = 0; Index < 16; Index += 1)
        Device->State.TextureKinds[Index] = 1;
    Device->State.ExtensionDirty = TRUE;
}

/* Takes every pipeline built from one converted shader out of the cache */
static
VOID
NineDropPipelinesOf(
    _Inout_ NineDevice* Device,
    _In_ const NineConverted* Converted)
{
    auto& Pipelines = Device->Pipelines;

    for (auto Entry = Pipelines.begin(); Entry != Pipelines.end();)
    {
        const NinePipelineKey& Key = (*Entry)->Key;

        if (Key.Vertex == Converted || Key.Pixel == Converted || Key.Geometry == Converted)
        {
            if (Device->State.Pipeline == (*Entry)->State.get())
                Device->State.Pipeline = nullptr;
            Entry = Pipelines.erase(Entry);
        }
        else
        {
            ++Entry;
        }
    }
}

VOID
NineDropConverted(
    _Inout_ NineDevice* Device,
    _Inout_ NineShader* Shader)
{
    NineDeviceState* State = &Device->State;

    for (const std::unique_ptr<NineConverted>& Converted : Shader->Converted)
    {
        NineDropPipelinesOf(Device, Converted.get());
        if (State->Vertex == Converted.get())
        {
            State->Vertex = nullptr;
            State->VertexKeyDirty = TRUE;
        }
        if (State->Pixel == Converted.get())
            State->Pixel = nullptr;
        if (State->Geometry == Converted.get())
            State->Geometry = nullptr;
    }
    Shader->Converted.clear();
}

VOID
NineFreeDrawState(
    _Inout_ NineDevice* Device)
{
    Device->Pipelines.clear();
    for (UINT Stage = 0; Stage < NineShaderStageCount; Stage += 1)
    {
        for (auto& Entry : Device->ShaderCache[Stage])
            Entry.second->Converted.clear();
    }
    Device->TransformedVertex.Converted.clear();
    Device->PointGeometry.Converted.clear();
    Device->Samplers.clear();
    NineScDestroyTranslator(Device->Translator);
    Device->Translator = nullptr;

    for (UINT Kind = 0; Kind < NineConstantKindCount; Kind += 1)
    {
        Device->VertexSlots[Kind].Ring.Buffer.reset();
        Device->PixelSlots[Kind].Ring.Buffer.reset();
    }
    Device->VertexExtension.Ring.Buffer.reset();
    Device->GeometryExtension.Ring.Buffer.reset();
    Device->PixelFog.Ring.Buffer.reset();
    Device->PixelBump.Ring.Buffer.reset();
    Device->UserRing.Buffer.reset();
    Device->FanRing.Buffer.reset();
    Device->UserFanList.reset();
}

/* The 36 bytes of pipeline state the converter keys its output on */
static
VOID
NineBuildRaster(
    _In_ const NineDevice* Device,
    _Out_ NineScRasterStates* Raster)
{
    const NineDeviceState* State = &Device->State;
    const DWORD* Rs = State->RenderStates;
    UINT Fill = Rs[D3DDDIRS_FILLMODE];
    UINT Words1;
    UINT Words2;

    ZeroMemory(Raster, sizeof(*Raster));
    for (UINT Sampler = 0; Sampler < 16; Sampler += 1)
    {
        DWORD Wrap = Sampler < 8 ? Rs[D3DDDIRS_WRAP0 + Sampler] : Rs[D3DDDIRS_WRAP8 + Sampler - 8];

        Raster->Samplers[Sampler] = static_cast<BYTE>((State->TextureKinds[Sampler] & 3) | ((Wrap & 0xF) << 2));
    }

    Words1 = 0;
    Words2 = 0;
    for (UINT Stage = 0; Stage < 8; Stage += 1)
    {
        Raster->Words[0] |= (State->StageStates[Stage][D3DDDITSS_TEXCOORDINDEX] & 0xF) << (4 * Stage);
        if (State->StageStates[Stage][D3DDDITSS_TEXTURETRANSFORMFLAGS] & 0x100)
            Words1 |= 1u << Stage;
        if (State->StageStates[Stage][D3DDDITSS_DISABLETEXTURECOLORKEY] != 0)
            Words2 |= 4u << Stage;
    }

    if (Fill < 1 || Fill > 3)
        Fill = 3;
    Words1 |= (Rs[D3DDDIRS_CLIPPLANEENABLE] & 0x3F) << 8;
    Words1 |= Fill << 14;
    Words1 |= (Rs[D3DDDIRS_SHADEMODE] & 3) << 16;
    Words1 |= (State->Primitive & 7) << NineRasterPrimitiveShift;
    Words1 |= (Rs[D3DDDIRS_ALPHAFUNC] & 0xF) << 21;
    if (Rs[D3DDDIRS_FOGENABLE] != 0)
        Words1 |= 0x2000000;
    Words1 |= (Rs[D3DDDIRS_FOGTABLEMODE] & 3) << 26;
    if (State->WInfo.WNear != 1.0f || State->WInfo.WFar != 1.0f)
        Words1 |= 0x10000000;
    if (Rs[D3DDDIRS_ALPHATESTENABLE] != 0)
        Words1 |= NineRasterAlphaTest;
    if (State->PointSizeLarge)
        Words1 |= NineRasterPointLarge;
    if (Rs[D3DDDIRS_POINTSPRITEENABLE] != 0)
        Words1 |= NineRasterPointSprite;
    Raster->Words[1] = Words1;

    if (Rs[D3DDDIRS_COLORKEYENABLE] != 0)
        Words2 |= 1;
    if (Rs[D3DDDIRS_COLORKEYBLENDENABLE] != 0)
        Words2 |= 2;
    Words2 |= State->ShadowSamplers;
    Raster->Words[2] = Words2;
    Raster->Words[3] = State->SwappedTargets;
}

/* Compares the pixel part of a raster key with the one seen last; the point size bit marks nothing */
static
VOID
NineComparePixelKey(
    _Inout_ NineDeviceState* State,
    _In_ const NineScRasterStates* Raster)
{
    NineScRasterStates Seen = *Raster;

    Seen.Words[1] &= ~NineRasterPointLarge;
    if (!State->HavePixelKey || memcmp(&Seen, &State->LastPixelKey, sizeof(Seen)) != 0)
        State->PixelDirty = TRUE;
    State->LastPixelKey = Seen;
    State->HavePixelKey = TRUE;
}

VOID
NineNotePixelKey(
    _Inout_ NineDevice* Device)
{
    NineScRasterStates Raster;

    NineBuildRaster(Device, &Raster);
    NineComparePixelKey(&Device->State, &Raster);
}

/* The declaration part of a vertex shader key */
static
VOID
NineAppendDecl(
    _Inout_ std::vector<BYTE>* Key,
    _In_opt_ const NineVertexDecl* Decl)
{
    UINT Count = Decl != nullptr ? static_cast<UINT>(Decl->Elements.size()) : 0;
    const BYTE* Bytes = reinterpret_cast<const BYTE*>(&Count);

    Key->insert(Key->end(), Bytes, Bytes + sizeof(Count));
    if (Decl == nullptr)
        return;
    Bytes = reinterpret_cast<const BYTE*>(Decl->Elements.data());
    Key->insert(Key->end(), Bytes, Bytes + Count * sizeof(Decl->Elements[0]));
    Bytes = reinterpret_cast<const BYTE*>(Decl->Inputs.Inputs.data());
    Key->insert(Key->end(), Bytes, Bytes + Decl->Inputs.Inputs.size() * sizeof(Decl->Inputs.Inputs[0]));
}

static
VOID
NineAppendBytes(
    _Inout_ std::vector<BYTE>* Key,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ SIZE_T Size)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Data);

    Key->insert(Key->end(), Bytes, Bytes + Size);
}

/* The outputs a key or the next stage sees: flags, indexed mask and each used output */
static
VOID
NineAppendOutputs(
    _Inout_ std::vector<BYTE>* Key,
    _In_ const NineScOutputs* Outputs)
{
    NineAppendBytes(Key, &Outputs->Flags, sizeof(Outputs->Flags));
    NineAppendBytes(Key, &Outputs->Indexed, sizeof(Outputs->Indexed));
    NineAppendBytes(Key, &Outputs->Count, sizeof(Outputs->Count));
    NineAppendBytes(Key, Outputs->Outputs, Outputs->Count * sizeof(Outputs->Outputs[0]));
}

static
NineConverted*
NineFindConverted(
    _In_ NineShader* Shader,
    _In_ const std::vector<BYTE>& Key)
{
    for (const std::unique_ptr<NineConverted>& Converted : Shader->Converted)
    {
        if (Converted->Key == Key)
            return Converted.get();
    }
    return nullptr;
}

static
PFN_NINE_SC_SIGN
NineSigner(
    _In_ const NineDevice* Device)
{
    if (!Device->Adapter->HasExtra[1])
        return nullptr;
    return reinterpret_cast<PFN_NINE_SC_SIGN>(Device->Adapter->Extras.Values[1]);
}

/* Packs tokens and signatures into a signed container and makes the translation layer shader of it */
static
HRESULT
NineFinishShader(
    _In_ NineDevice* Device,
    _Inout_ NineConverted* Converted,
    _In_reads_bytes_(TokenSize) const VOID* Tokens,
    _In_ SIZE_T TokenSize,
    _In_ const NineScBlob* InputSignature,
    _In_ const NineScBlob* OutputSignature)
{
    std::unique_ptr<BYTE[]> Copy;
    HRESULT hr;

    hr = NineScPackageShader(Tokens, TokenSize, InputSignature, OutputSignature, NineSigner(Device), &Converted->Container);
    if (FAILED(hr) || Converted->Container.empty())
        return hr;

    Converted->OutputSignature = *OutputSignature;
    Copy.reset(new BYTE[Converted->Container.size()]);
    memcpy(Copy.get(), Converted->Container.data(), Converted->Container.size());
    Converted->Shader.reset(new TL::Shader(&*Device->Context, std::move(Copy), Converted->Container.size()));
    return S_OK;
}

/* The vertex shader of the bound declaration and shader, converted when its key is new */
static
HRESULT
NineResolveVertexShader(
    _Inout_ NineDevice* Device,
    _In_ const NineScRasterStates* Raster)
{
    NineDeviceState* State = &Device->State;
    const NineVertexDecl* Decl = State->VertexDecl;
    BOOLEAN Transformed = Decl != nullptr && Decl->Pretransformed;
    NineShader* Owner = Transformed ? &Device->TransformedVertex : State->Shaders[NineVertexStage];
    std::unique_ptr<NineConverted> Made;
    std::vector<BYTE> Key;
    NineConverted* Converted;
    NineScInputs Inputs;
    NineScBlob InputSignature;
    NineScBlob OutputSignature;
    PVOID Tokens = nullptr;
    SIZE_T TokenSize = 0;
    HRESULT hr;

    if (Owner == nullptr)
        return E_FAIL;

    NineAppendBytes(&Key, Raster, sizeof(*Raster));
    NineAppendBytes(&Key, State->Frequencies, sizeof(State->Frequencies));
    NineAppendDecl(&Key, Decl);
    Converted = NineFindConverted(Owner, Key);
    if (Converted == nullptr)
    {
        Made = std::make_unique<NineConverted>();
        Made->Key = std::move(Key);
        ZeroMemory(&Made->Outputs, sizeof(Made->Outputs));
        ZeroMemory(Made->Used, sizeof(Made->Used));
        if (Decl != nullptr)
            Inputs = Decl->Inputs;

        if (Transformed)
        {
            NineScTlArgs Args = {};

            Args.Version = Device->RuntimeVersion;
            Args.Inputs = &Inputs;
            Args.Outputs = &Made->Outputs;
            hr = NineScConvertTlShader(&Device->Translator, &Args);
            Tokens = Args.Tokens;
            TokenSize = Args.TokenSize;
        }
        else
        {
            NineScArgs Args = {};

            Args.Version = Device->RuntimeVersion;
            Args.Raster = Raster;
            Args.VertexInputs = &Inputs;
            Args.Code = reinterpret_cast<const DWORD*>(Owner->Code.data());
            Args.CodeSize = Owner->Code.size();
            Args.VertexOutputs = &Made->Outputs;
            hr = NineScConvertShader(&Device->Translator, &Args);
            Tokens = Args.Tokens;
            TokenSize = Args.TokenSize;
            for (UINT Kind = 0; Kind < NineScBufferCount; Kind += 1)
            {
                Made->Defines[Kind] = std::move(Args.Constants[Kind]);
                Made->Used[Kind] = Args.BufferSizes[Kind];
            }
        }

        if (SUCCEEDED(hr) && Tokens != nullptr)
        {
            NineScBuildInputSignature(&Inputs, Decl != nullptr ? Decl->Elements.data() : nullptr,
                                      Decl != nullptr ? static_cast<UINT>(Decl->Elements.size()) : 0, State->Frequencies,
                                      &Made->Layout, &InputSignature);
            NineScBuildOutputSignature(&Made->Outputs, &OutputSignature);
            hr = NineFinishShader(Device, Made.get(), Tokens, TokenSize, &InputSignature, &OutputSignature);
            if (FAILED(hr))
                NineTrace(L"vertex shader container failed (0x%08lX)", hr);
        }
        else if (FAILED(hr))
        {
            NineTrace(L"vertex shader conversion failed (0x%08lX)", hr);
        }
        free(Tokens);
        Converted = Made.get();
        Owner->Converted.push_back(std::move(Made));
    }

    /* Constants the shader defines itself land in the application's registers */
    if (!Transformed)
    {
        static const UINT Strides[NineScBufferCount] = { 16, 16, 4 };
        BYTE* Shadows[NineScBufferCount] = { State->VertexConstants.Float, State->VertexConstants.Integer,
                                              State->VertexConstants.Boolean };

        for (UINT Kind = 0; Kind < NineScBufferCount; Kind += 1)
        {
            for (const NineScConstant& Constant : Converted->Defines[Kind])
            {
                memcpy(Shadows[Kind] + (Constant.Offset >> 2) * Strides[Kind], Constant.Value, Strides[Kind]);
                State->VertexConstants.Dirty[Kind] = TRUE;
            }
        }
    }

    State->Vertex = Converted;
    State->VertexKeyDirty = FALSE;
    return Converted->Shader != nullptr ? S_OK : E_FAIL;
}

/* Whether point sprites, point fill or texture coordinate wrapping need the geometry stage */
static
BOOLEAN
NineNeedsGeometry(
    _In_ const NineScRasterStates* Raster,
    _In_ const NineScOutputs* Outputs)
{
    UINT Words1 = Raster->Words[1];
    UINT Texcoords = (Outputs->Flags >> 3) & 0xFFFF;

    if ((Words1 & 0xC000) == 0x4000 && (Words1 & 0x100000) != 0)
        return TRUE;
    if ((Words1 & 0x1C0000) == 0x40000 &&
        ((Words1 & (NineRasterPointLarge | NineRasterPointSprite)) != 0 || (Outputs->Flags & NineOutputPointSize) != 0))
    {
        return TRUE;
    }
    for (UINT Index = 0; Index < 16 && Texcoords != 0; Index += 1, Texcoords >>= 1)
    {
        if ((Texcoords & 1) != 0 && (Raster->Samplers[Index] & 0x3C) != 0)
            return TRUE;
    }
    return FALSE;
}

static
BOOLEAN
NineSameOutputs(
    _In_ const NineScOutputs* Left,
    _In_ const NineScOutputs* Right)
{
    return Left->Flags == Right->Flags && (Left->Indexed & ((1ull << 45) - 1)) == (Right->Indexed & ((1ull << 45) - 1)) &&
           Left->Count == Right->Count &&
           memcmp(Left->Outputs, Right->Outputs, Left->Count * sizeof(Left->Outputs[0])) == 0;
}

static
HRESULT
NineResolveGeometryShader(
    _Inout_ NineDevice* Device,
    _In_ const NineScRasterStates* Raster)
{
    NineDeviceState* State = &Device->State;
    NineConverted* Vertex = State->Vertex;
    std::unique_ptr<NineConverted> Made;
    std::vector<BYTE> Key;
    NineConverted* Converted;
    NineScGsArgs Args = {};
    HRESULT hr;

    NineAppendBytes(&Key, Raster, sizeof(*Raster));
    NineAppendOutputs(&Key, &Vertex->Outputs);
    Converted = NineFindConverted(&Device->PointGeometry, Key);
    if (Converted == nullptr)
    {
        Made = std::make_unique<NineConverted>();
        Made->Key = std::move(Key);
        ZeroMemory(&Made->Outputs, sizeof(Made->Outputs));
        ZeroMemory(Made->Used, sizeof(Made->Used));

        Args.Version = Device->RuntimeVersion;
        Args.VertexOutputs = &Vertex->Outputs;
        Args.Outputs = &Made->Outputs;
        Args.Raster = Raster;
        hr = NineScCreateGeometryShader(Device->Translator, &Args);
        if (SUCCEEDED(hr))
        {
            NineScBlob OutputSignature = Vertex->OutputSignature;

            if (!NineSameOutputs(&Vertex->Outputs, &Made->Outputs))
                NineScBuildOutputSignature(&Made->Outputs, &OutputSignature);
            hr = NineFinishShader(Device, Made.get(), Args.Tokens, Args.TokenSize, &Vertex->OutputSignature,
                                  &OutputSignature);
        }
        if (FAILED(hr))
            NineTrace(L"geometry shader conversion failed (0x%08lX)", hr);
        free(Args.Tokens);
        Converted = Made.get();
        Device->PointGeometry.Converted.push_back(std::move(Made));
    }
    return Converted->Shader != nullptr ? (State->Geometry = Converted, S_OK) : E_FAIL;
}

/* The pixel shader keyed on the raster state and the outputs of the stage before it */
static
HRESULT
NineResolvePixelShader(
    _Inout_ NineDevice* Device,
    _In_ const NineScRasterStates* Live)
{
    NineDeviceState* State = &Device->State;
    NineShader* Owner = State->Shaders[NinePixelStage];
    const NineConverted* Before = State->Geometry != nullptr ? State->Geometry : State->Vertex;
    NineScRasterStates Raster = *Live;
    NineScOutputs Linked = {};
    std::unique_ptr<NineConverted> Made;
    std::vector<BYTE> Key;
    NineConverted* Converted;
    BOOLEAN DroppedClip = FALSE;

    State->PixelDirty = FALSE;
    if (Owner == nullptr)
        return E_FAIL;

    /* The alpha test the key carries is the one kept for it, which alpha to coverage replaces */
    Raster.Words[1] = (Raster.Words[1] & ~(NineRasterAlphaFunc | NineRasterAlphaTest)) | State->PixelAlpha;

    for (UINT Index = 0; Index < Before->Outputs.Count; Index += 1)
    {
        NineScOutput Output = Before->Outputs.Outputs[Index];

        if ((Output & 0x1F) == NineOutputClip && Index + 1 == Before->Outputs.Count)
        {
            DroppedClip = TRUE;
            Raster.Words[1] &= ~NineRasterClipPlanes;
            continue;
        }
        ScAddOutput(&Linked, Output & 0x1F, (Output >> 5) & 0xF, (Output >> 9) & 0xF, ((Output >> 13) & 0xF) << 16,
                    FALSE);
    }

    NineAppendBytes(&Key, &Raster, sizeof(Raster));
    NineAppendOutputs(&Key, &Linked);
    Converted = NineFindConverted(Owner, Key);
    if (Converted == nullptr)
    {
        NineScArgs Args = {};
        NineScInputs NoInputs = {};
        HRESULT hr;

        Made = std::make_unique<NineConverted>();
        Made->Key = std::move(Key);
        ZeroMemory(&Made->Outputs, sizeof(Made->Outputs));
        ZeroMemory(Made->Used, sizeof(Made->Used));

        NoInputs.Limit = 16;
        Args.PixelShader = TRUE;
        Args.Version = Device->RuntimeVersion;
        Args.Raster = &Raster;
        Args.PixelInputs = &Linked;
        Args.VertexInputs = &NoInputs;
        Args.Code = reinterpret_cast<const DWORD*>(Owner->Code.data());
        Args.CodeSize = Owner->Code.size();
        hr = NineScConvertShader(&Device->Translator, &Args);
        for (UINT Kind = 0; Kind < NineScBufferCount; Kind += 1)
        {
            Made->Defines[Kind] = Args.Constants[Kind];
            Made->Used[Kind] = Args.BufferSizes[Kind];
        }
        Made->PixelOutputs = Args.PixelFlag;

        if (SUCCEEDED(hr) && Args.Tokens != nullptr)
        {
            NineScBlob InputSignature = Before->OutputSignature;
            NineScBlob OutputSignature;

            if (DroppedClip || !Args.Extra.empty())
            {
                NineScOutputs Widened = Linked;

                for (UINT Extra : Args.Extra)
                    ScAddOutput(&Widened, Extra & 0x1F, (Extra >> 5) & 0xF, (Extra >> 9) & 0xF,
                                ((Extra >> 13) & 0xF) << 16, FALSE);
                NineScBuildOutputSignature(&Widened, &InputSignature);
            }
            NineScBuildPixelOutputSignature(Args.PixelFlag, &OutputSignature);
            hr = NineFinishShader(Device, Made.get(), Args.Tokens, Args.TokenSize, &InputSignature, &OutputSignature);
        }
        if (FAILED(hr))
            NineTrace(L"pixel shader conversion failed (0x%08lX)", hr);
        free(Args.Tokens);
        Converted = Made.get();
        Owner->Converted.push_back(std::move(Made));
    }

    {
        static const UINT Strides[NineScBufferCount] = { 16, 16, 4 };
        BYTE* Shadows[NineScBufferCount] = { State->PixelConstants.Float, State->PixelConstants.Integer,
                                              State->PixelConstants.Boolean };

        for (UINT Kind = 0; Kind < NineScBufferCount; Kind += 1)
        {
            for (const NineScConstant& Constant : Converted->Defines[Kind])
            {
                memcpy(Shadows[Kind] + (Constant.Offset >> 2) * Strides[Kind], Constant.Value, Strides[Kind]);
                State->PixelConstants.Dirty[Kind] = TRUE;
            }
        }
    }

    State->Pixel = Converted;
    return Converted->Shader != nullptr ? S_OK : E_FAIL;
}

/* Copies data into a new piece of a slot's upload buffer and makes it the one bound */
static
VOID
NineVersionSlot(
    _Inout_ NineConstantSlot* Slot,
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ UINT Size)
{
    Slot->Current = NineAllocateUpload(&Slot->Ring, Size);
    if (Data != nullptr)
        memcpy(Slot->Current.Cpu, Data, Size);
}

template <TL::EShaderStage Stage>
static
VOID
NineBindSlot(
    _Inout_ NineDevice* Device,
    _In_ const NineConstantSlot* Slot,
    _In_ UINT Register)
{
    TL::Resource* Buffer = Slot->Current.Buffer;
    UINT First = Slot->Current.Offset >> 4;
    UINT Count = NineConstantsBound;

    Device->Context->SetConstantBuffers<Stage>(Register, 1, &Buffer, &First, &Count);
}

/* Uploads the application's constants a stage reads when they changed or more of them are read */
template <TL::EShaderStage Stage>
static
VOID
NineUploadStage(
    _Inout_ NineDevice* Device,
    _Inout_ NineStageConstants* Shadow,
    _Inout_updates_(NineConstantKindCount) NineConstantSlot* Slots,
    _In_ const NineConverted* Converted)
{
    const BYTE* Data[NineConstantKindCount] = { Shadow->Float, Shadow->Integer, Shadow->Boolean };

    for (UINT Kind = 0; Kind < NineConstantKindCount; Kind += 1)
    {
        UINT Used = Converted->Used[Kind];
        UINT Count = Kind == NineConstantBoolean ? Used : Used >> 2;
        UINT Size;

        if (Count == 0 || (Kind != NineConstantBoolean && Used < 4))
            continue;
        if (!Shadow->Dirty[Kind] && Slots[Kind].Uploaded >= Count)
            continue;

        Size = std::min(NineConstantRegisters[Kind] * NineConstantStride[Kind], NineConstantStride[Kind] * Count);
        NineVersionSlot(&Slots[Kind], Data[Kind], Size);
        Shadow->Dirty[Kind] = FALSE;
        Slots[Kind].Uploaded = Count;
        NineBindSlot<Stage>(Device, &Slots[Kind], Kind);
    }
}

/* The D3D12 blend factor of a Direct3D 9 one */
static
D3D12_BLEND
NineBlendFactor(
    _In_ UINT Factor)
{
    if ((Factor >= 1 && Factor <= 11) || (Factor >= 14 && Factor <= 17))
        return static_cast<D3D12_BLEND>(Factor);
    return D3D12_BLEND_ZERO;
}

/* The alpha factor a color factor turns into when alpha is not blended on its own */
static
D3D12_BLEND
NineAlphaFactor(
    _In_ UINT Factor)
{
    switch (Factor)
    {
        case D3D12_BLEND_SRC_COLOR:
            return D3D12_BLEND_SRC_ALPHA;
        case D3D12_BLEND_INV_SRC_COLOR:
            return D3D12_BLEND_INV_SRC_ALPHA;
        case D3D12_BLEND_DEST_COLOR:
            return D3D12_BLEND_DEST_ALPHA;
        case D3D12_BLEND_INV_DEST_COLOR:
            return D3D12_BLEND_INV_DEST_ALPHA;
        case D3D12_BLEND_SRC1_COLOR:
            return D3D12_BLEND_SRC1_ALPHA;
        case D3D12_BLEND_INV_SRC1_COLOR:
            return D3D12_BLEND_INV_SRC1_ALPHA;
        case D3D12_BLEND_ZERO:
        case D3D12_BLEND_ONE:
        case D3D12_BLEND_SRC_ALPHA:
        case D3D12_BLEND_INV_SRC_ALPHA:
        case D3D12_BLEND_DEST_ALPHA:
        case D3D12_BLEND_INV_DEST_ALPHA:
        case D3D12_BLEND_SRC_ALPHA_SAT:
        case D3D12_BLEND_BLEND_FACTOR:
        case D3D12_BLEND_INV_BLEND_FACTOR:
            return static_cast<D3D12_BLEND>(Factor);
        default:
            return D3D12_BLEND_ZERO;
    }
}

static
D3D12_BLEND_OP
NineBlendOp(
    _In_ UINT Op)
{
    if (Op >= 2 && Op <= 5)
        return static_cast<D3D12_BLEND_OP>(Op);
    return D3D12_BLEND_OP_ADD;
}

/* Clamps a blend state value the way it is stored */
static
UINT
NineClampBlend(
    _In_ DWORD Value,
    _In_ UINT Limit,
    _In_ UINT Fallback)
{
    return Value >= Limit ? Fallback : Value;
}

static
VOID
NineBuildBlend(
    _In_ const NineDevice* Device,
    _Out_ D3D12_BLEND_DESC* Blend)
{
    const NineDeviceState* State = &Device->State;
    const DWORD* Rs = State->RenderStates;
    static const UINT WriteStates[NineTargetCount] = { D3DDDIRS_COLORWRITEENABLE, D3DDDIRS_COLORWRITEENABLE1,
                                                      D3DDDIRS_COLORWRITEENABLE2, D3DDDIRS_COLORWRITEENABLE3 };
    D3D12_RENDER_TARGET_BLEND_DESC* First = &Blend->RenderTarget[0];
    UINT Op;

    ZeroMemory(Blend, sizeof(*Blend));
    Blend->AlphaToCoverageEnable = State->AlphaToCoverage;
    Blend->IndependentBlendEnable = TRUE;
    First->BlendEnable = NineClampBlend(Rs[D3DDDIRS_ALPHABLENDENABLE], 2, 1) != 0;

    if (First->BlendEnable)
    {
        UINT Source = NineClampBlend(Rs[D3DDDIRS_SRCBLEND], 32, 2);
        UINT Dest = NineClampBlend(Rs[D3DDDIRS_DESTBLEND], 32, 1);

        Op = NineClampBlend(Rs[D3DDDIRS_BLENDOP], 8, 1);
        if (Source == D3DBLEND_BOTHSRCALPHA)
        {
            First->SrcBlend = D3D12_BLEND_SRC_ALPHA;
            First->DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        }
        else if (Source == D3DBLEND_BOTHINVSRCALPHA)
        {
            First->SrcBlend = D3D12_BLEND_INV_SRC_ALPHA;
            First->DestBlend = D3D12_BLEND_SRC_ALPHA;
        }
        else
        {
            First->SrcBlend = NineBlendFactor(Source);
            First->DestBlend = NineBlendFactor(Dest);
        }
        First->BlendOp = NineBlendOp(Op);

        if (NineClampBlend(Rs[D3DDDIRS_SEPARATEALPHABLENDENABLE], 2, 1) != 0)
        {
            UINT AlphaOp = Rs[D3DDDIRS_BLENDOPALPHA];

            First->SrcBlendAlpha = NineAlphaFactor(NineBlendFactor(NineClampBlend(Rs[D3DDDIRS_SRCBLENDALPHA], 32, 2)));
            First->DestBlendAlpha = NineAlphaFactor(NineBlendFactor(NineClampBlend(Rs[D3DDDIRS_DESTBLENDALPHA], 32, 1)));
            First->BlendOpAlpha = AlphaOp >= 1 && AlphaOp <= 5 ? static_cast<D3D12_BLEND_OP>(AlphaOp) : D3D12_BLEND_OP_ADD;
        }
        else
        {
            First->SrcBlendAlpha = NineAlphaFactor(First->SrcBlend);
            First->DestBlendAlpha = NineAlphaFactor(First->DestBlend);
            First->BlendOpAlpha = First->BlendOp;
        }
    }

    for (UINT Slot = 0; Slot < NineTargetCount; Slot += 1)
    {
        D3D12_RENDER_TARGET_BLEND_DESC* Target = &Blend->RenderTarget[Slot];
        const NineResource* Resource = State->Targets[Slot].Resource;
        UINT Enable = Rs[WriteStates[Slot]] & 0xFF;
        UINT Mapping = Resource != nullptr ? NineViewSwizzle(Resource->Format, FALSE) : NineIdentityMapping;
        UINT Mask = 0;

        if (Slot != 0)
            *Target = *First;

        if (Mapping == NineIdentityMapping)
        {
            Mask = Enable & 0xF;
        }
        else
        {
            for (UINT Channel = 0; Channel < 4; Channel += 1)
            {
                UINT Select = (Mapping >> (3 * Channel)) & 7;

                if (Select <= 3)
                    Mask |= ((Enable >> Channel) & 1) << Select;
            }
        }
        Target->RenderTargetWriteMask = static_cast<UINT8>(Mask);

        /* A promoted primary has no alpha to read back */
        if (Resource != nullptr && Resource->PromotedPrimary)
        {
            if (Target->SrcBlend == D3D12_BLEND_DEST_ALPHA)
                Target->SrcBlend = D3D12_BLEND_ONE;
            else if (Target->SrcBlend == D3D12_BLEND_INV_DEST_ALPHA)
                Target->SrcBlend = D3D12_BLEND_ZERO;
            if (Target->DestBlend == D3D12_BLEND_DEST_ALPHA)
                Target->DestBlend = D3D12_BLEND_ONE;
            else if (Target->DestBlend == D3D12_BLEND_INV_DEST_ALPHA)
                Target->DestBlend = D3D12_BLEND_ZERO;
        }
    }
}

static
D3D12_STENCIL_OP
NineStencilOp(
    _In_ DWORD Value)
{
    return static_cast<D3D12_STENCIL_OP>(((Value - 1) & 7) + 1);
}

static
D3D12_COMPARISON_FUNC
NineCompare(
    _In_ DWORD Value)
{
    return static_cast<D3D12_COMPARISON_FUNC>(((Value - 1) & 7) + 1);
}

static
VOID
NineBuildDepth(
    _In_ const NineDevice* Device,
    _In_ BOOLEAN HasDepth,
    _Out_ D3D12_DEPTH_STENCIL_DESC* Depth)
{
    const DWORD* Rs = Device->State.RenderStates;

    Depth->DepthEnable = HasDepth && (Rs[D3DDDIRS_ZENABLE] & 1) != 0;
    Depth->DepthWriteMask = (Rs[D3DDDIRS_ZWRITEENABLE] & 1) != 0 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    Depth->DepthFunc = NineCompare(Rs[D3DDDIRS_ZFUNC]);
    Depth->StencilEnable = (Rs[D3DDDIRS_STENCILENABLE] & 1) != 0;
    Depth->StencilReadMask = static_cast<UINT8>(Rs[D3DDDIRS_STENCILMASK]);
    Depth->StencilWriteMask = static_cast<UINT8>(Rs[D3DDDIRS_STENCILWRITEMASK]);
    Depth->FrontFace.StencilFailOp = NineStencilOp(Rs[D3DDDIRS_STENCILFAIL]);
    Depth->FrontFace.StencilDepthFailOp = NineStencilOp(Rs[D3DDDIRS_STENCILZFAIL]);
    Depth->FrontFace.StencilPassOp = NineStencilOp(Rs[D3DDDIRS_STENCILPASS]);
    Depth->FrontFace.StencilFunc = NineCompare(Rs[D3DDDIRS_STENCILFUNC]);
    if ((Rs[D3DDDIRS_TWOSIDEDSTENCILMODE] & 1) != 0)
    {
        Depth->BackFace.StencilFailOp = NineStencilOp(Rs[D3DDDIRS_CCW_STENCILFAIL]);
        Depth->BackFace.StencilDepthFailOp = NineStencilOp(Rs[D3DDDIRS_CCW_STENCILZFAIL]);
        Depth->BackFace.StencilPassOp = NineStencilOp(Rs[D3DDDIRS_CCW_STENCILPASS]);
        Depth->BackFace.StencilFunc = NineCompare(Rs[D3DDDIRS_CCW_STENCILFUNC]);
    }
    else
    {
        Depth->BackFace = Depth->FrontFace;
    }
}

/* How many depth units one unit of D3D9 depth bias is for a depth format */
static
FLOAT
NineBiasScale(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_D16_UNORM:
            return 65536.0f;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return 16777216.0f;
        case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return 8388608.0f;
        default:
            return 0.0f;
    }
}

static
VOID
NineBuildRasterizer(
    _In_ const NineDevice* Device,
    _In_ BOOLEAN DepthTested,
    _In_ DXGI_FORMAT DepthFormat,
    _In_ BOOLEAN Multisampled,
    _Out_ D3D12_RASTERIZER_DESC* Desc)
{
    const NineDeviceState* State = &Device->State;
    const DWORD* Rs = State->RenderStates;
    BOOLEAN Triangles = State->Primitive >= D3DPT_TRIANGLELIST;
    BOOLEAN Antialias = NineClampBlend(Rs[D3DDDIRS_MULTISAMPLEANTIALIAS], 2, 1) != 0;
    BOOLEAN Clipping = NineClampBlend(Rs[D3DDDIRS_CLIPPING], 2, 1) != 0;
    DWORD Cull = Rs[D3DDDIRS_CULLMODE];
    DWORD Fill = Rs[D3DDDIRS_FILLMODE];
    FLOAT Bias;

    if (Cull < 1 || Cull > 3)
        Cull = 1;
    if (Fill < 1 || Fill > 3)
        Fill = 3;

    ZeroMemory(Desc, sizeof(*Desc));
    Desc->FillMode = Triangles && Fill == D3DFILL_WIREFRAME ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    Desc->CullMode = D3D12_CULL_MODE_NONE;
    if (Triangles && Cull == D3DCULL_CW)
        Desc->CullMode = D3D12_CULL_MODE_FRONT;
    else if (Triangles && Cull == D3DCULL_CCW)
        Desc->CullMode = D3D12_CULL_MODE_BACK;

    if (DepthTested && Triangles)
    {
        memcpy(&Bias, &Rs[D3DDDIRS_DEPTHBIAS], sizeof(Bias));
        Desc->DepthBias = static_cast<INT>(NineBiasScale(DepthFormat) * Bias);
        memcpy(&Desc->SlopeScaledDepthBias, &Rs[D3DDDIRS_SLOPESCALEDEPTHBIAS], sizeof(FLOAT));
    }
    Desc->DepthClipEnable = State->Pretransformed && !DepthTested ? FALSE : Clipping;
    Desc->MultisampleEnable = Multisampled && Antialias;
    Desc->AntialiasedLineEnable = NineClampBlend(Rs[D3DDDIRS_ANTIALIASEDLINEENABLE], 2, 1) != 0;
    Desc->ForcedSampleCount = Multisampled && !Antialias && !DepthTested ? 1 : 0;
}

/* The sRGB member of a format's cast family, or unknown when it has none */
static
DXGI_FORMAT
NineSrgbOf(
    _In_ DXGI_FORMAT Format)
{
    for (const DXGI_FORMAT* Cast = CD3D11FormatHelper::GetFormatCastSet(Format); *Cast != DXGI_FORMAT_UNKNOWN; Cast += 1)
    {
        if (CD3D11FormatHelper::IsSRGBFormat(*Cast))
            return *Cast;
    }
    return DXGI_FORMAT_UNKNOWN;
}

/* Binds render targets and the depth buffer and fills their part of the pipeline description */
static
VOID
NineApplyTargets(
    _Inout_ NineDevice* Device,
    _In_ BOOLEAN HasDepth)
{
    NineDeviceState* State = &Device->State;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc = &State->Desc;
    BOOLEAN Srgb = State->RenderStates[D3DDDIRS_SRGBWRITEENABLE] != 0;
    TL::RTV* Views[8] = {};
    TL::DSV* Depth = nullptr;
    UINT Count = 0;

    for (UINT Slot = 0; Slot < NineTargetCount; Slot += 1)
    {
        if (State->TargetMask & (1u << Slot))
            Count = Slot + 1;
    }

    for (UINT Slot = 0; Slot < Count; Slot += 1)
    {
        NineResource* Resource = State->Targets[Slot].Resource;
        UINT Sub = State->Targets[Slot].Sub;

        if (Resource == nullptr)
            continue;
        if ((Resource->Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && Sub < Resource->Rtvs.size())
        {
            BOOLEAN UseSrgb = Srgb && NineSrgbOf(Resource->Desc.Format) != DXGI_FORMAT_UNKNOWN &&
                              Sub < Resource->SrgbRtvs.size();

            Views[Slot] = UseSrgb ? Resource->SrgbRtvs[Sub].get() : Resource->Rtvs[Sub].get();
        }
        Desc->RTVFormats[Slot] = Srgb ? NineSrgbOf(Resource->DxgiFormat) : Resource->DxgiFormat;
    }
    for (UINT Slot = Count; Slot < 8; Slot += 1)
        Desc->RTVFormats[Slot] = DXGI_FORMAT_UNKNOWN;
    Desc->NumRenderTargets = Count;

    if (HasDepth)
    {
        NineResource* Buffer = State->DepthStencil;
        UINT Kind = 0;

        if ((State->RenderStates[D3DDDIRS_ZWRITEENABLE] & 1) == 0)
            Kind |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        if (static_cast<BYTE>(State->RenderStates[D3DDDIRS_STENCILWRITEMASK]) == 0 &&
            CD3D11FormatHelper::GetComponentName(Buffer->DxgiFormat, 1) == D3D11FCN_S)
        {
            Kind |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
        }
        Depth = Buffer->Dsvs[Kind].get();
    }
    Device->Context->OMSetRenderTargets(Views, Count, Depth);
}

/* The words of one stage's sampler, as the sampler cache keys it */
static
std::array<UINT, 4>
NineSamplerWords(
    _In_ const NineDeviceState* State,
    _In_ UINT Stage)
{
    const DWORD* Tss = State->StageStates[Stage];
    std::array<UINT, 4> Words = {};
    UINT Address[3] = { Tss[D3DDDITSS_ADDRESSU], Tss[D3DDDITSS_ADDRESSV], Tss[D3DDDITSS_ADDRESSW] };
    UINT Anisotropy = Tss[D3DDDITSS_MAXANISOTROPY];
    UINT Bias = Tss[D3DDDITSS_MIPMAPLODBIAS];

    for (UINT Axis = 0; Axis < 3; Axis += 1)
    {
        if (Address[Axis] > 5)
            Address[Axis] = 1;
    }
    if (Anisotropy > 16)
        Anisotropy = 1;
    if ((Bias & 0x7F800000) == 0x7F800000 && (Bias & 0x7FFFFF) != 0)
        Bias = 0;

    Words[0] = (Tss[D3DDDITSS_MAGFILTER] & 0xF) | ((Tss[D3DDDITSS_MINFILTER] & 0xF) << 4) |
               ((Tss[D3DDDITSS_MIPFILTER] & 0xF) << 8) | ((Address[0] & 7) << 12) | ((Address[1] & 7) << 15) |
               ((Address[2] & 7) << 18) | ((Anisotropy & 0xFF) << 21);
    if (State->Textures[Stage].Shadow)
        Words[0] |= 1u << 29;
    if (State->Textures[Stage].SwappedRedBlue)
        Words[0] |= 1u << 30;
    Words[1] = Tss[D3DDDITSS_BORDERCOLOR];
    Words[2] = Tss[D3DDDITSS_MAXMIPLEVEL];
    Words[3] = Bias;
    return Words;
}

static
D3D12_FILTER
NineFilter(
    _In_ UINT Mag,
    _In_ UINT Min,
    _In_ UINT Mip,
    _In_ BOOLEAN AnisotropicPointMip)
{
    UINT Filter;

    if (Min == D3DTEXF_ANISOTROPIC || Mag == D3DTEXF_ANISOTROPIC)
    {
        if (Mip == D3DTEXF_POINT && AnisotropicPointMip)
            return D3D12_FILTER_MIN_MAG_ANISOTROPIC_MIP_POINT;
        return D3D12_FILTER_ANISOTROPIC;
    }
    if ((Min != D3DTEXF_POINT && Min != D3DTEXF_LINEAR) || (Mag != D3DTEXF_POINT && Mag != D3DTEXF_LINEAR))
        return D3D12_FILTER_MIN_MAG_MIP_POINT;

    Filter = Mip == D3DTEXF_POINT ? 0 : 1;
    if (Mag == D3DTEXF_LINEAR)
        Filter |= 4;
    if (Min == D3DTEXF_LINEAR)
        Filter |= 16;
    return static_cast<D3D12_FILTER>(Filter);
}

static
FLOAT
NineColorChannel(
    _In_ UINT Color,
    _In_ UINT Shift)
{
    return static_cast<FLOAT>(static_cast<double>((Color >> Shift) & 0xFF) / 255.0);
}

static
TL::Sampler*
NineFindSampler(
    _Inout_ NineDevice* Device,
    _In_ const std::array<UINT, 4>& Words)
{
    D3D12_SAMPLER_DESC Desc = {};
    UINT Word0 = Words[0];
    TL::Sampler* Made;

    for (const auto& Entry : Device->Samplers)
    {
        if (Entry.first == Words)
            return Entry.second.get();
    }

    Desc.Filter = NineFilter(Word0 & 0xF, (Word0 >> 4) & 0xF, (Word0 >> 8) & 0xF,
                             Device->Options19.AnisoFilterWithPointMipSupported != FALSE);
    if (Word0 & (1u << 29))
        Desc.Filter = static_cast<D3D12_FILTER>(Desc.Filter | 0x80);
    Desc.AddressU = static_cast<D3D12_TEXTURE_ADDRESS_MODE>((Word0 >> 12) & 7);
    Desc.AddressV = static_cast<D3D12_TEXTURE_ADDRESS_MODE>((Word0 >> 15) & 7);
    Desc.AddressW = static_cast<D3D12_TEXTURE_ADDRESS_MODE>((Word0 >> 18) & 7);
    memcpy(&Desc.MipLODBias, &Words[3], sizeof(FLOAT));
    Desc.MaxAnisotropy = (Word0 >> 21) & 0xFF;
    Desc.ComparisonFunc = (Word0 & (1u << 29)) ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_NEVER;
    Desc.BorderColor[0] = NineColorChannel(Words[1], (Word0 & (1u << 30)) ? 0 : 16);
    Desc.BorderColor[1] = NineColorChannel(Words[1], 8);
    Desc.BorderColor[2] = NineColorChannel(Words[1], (Word0 & (1u << 30)) ? 16 : 0);
    Desc.BorderColor[3] = NineColorChannel(Words[1], 24);
    Desc.MinLOD = static_cast<FLOAT>(Words[2]);
    Desc.MaxLOD = D3D12_FLOAT32_MAX;

    Made = new TL::Sampler(&*Device->Context, Desc);
    Device->Samplers.emplace_back(Words, std::unique_ptr<TL::Sampler>(Made));
    return Made;
}

/* Samplers of the 16 pixel and the 4 vertex stages */
static
VOID
NineApplySamplers(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;

    for (UINT Stage = 0; Stage < NineStageCount; Stage += 1)
    {
        TL::Sampler* Sampler;

        if (Stage == 16)
            continue;
        Sampler = NineFindSampler(Device, NineSamplerWords(State, Stage));
        if (Stage < 16)
            Device->Context->SetSamplers<TL::e_PS>(Stage, 1, &Sampler);
        else
            Device->Context->SetSamplers<TL::e_VS>(Stage - 17, 1, &Sampler);
    }
}

/* Shader views of the 16 pixel and the 4 vertex stages */
static
VOID
NineApplyTextures(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;

    for (UINT Stage = 0; Stage < NineStageCount; Stage += 1)
    {
        NineResource* Texture = State->Textures[Stage].Texture;
        TL::SRV* View = nullptr;
        UINT Kind;

        if (Stage == 16)
            continue;

        if (Texture != nullptr && Texture->TargetSlots.empty() &&
            !(Texture->BoundAsDepth && (State->RenderStates[D3DDDIRS_ZWRITEENABLE] & 1) != 0))
        {
            Kind = (State->StageStates[Stage][D3DDDITSS_MIPFILTER] & 0xF) != 0 ? 0 : 2;
            if (State->StageStates[Stage][D3DDDITSS_SRGBTEXTURE] != 0)
                Kind |= 1;
            View = NineShaderViewOf(Texture, Kind);
        }

        if (Stage < 16)
            Device->Context->SetShaderResources<TL::e_PS>(Stage, 1, &View);
        else
            Device->Context->SetShaderResources<TL::e_VS>(Stage - 17, 1, &View);
    }
}

/* The viewport goes to the translation layer as given; the extension constants map screen to clip space */
static
VOID
NineApplyViewport(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;
    const FLOAT* Rect = State->Viewport;
    D3D12_VIEWPORT Viewport;
    FLOAT* Scale;
    FLOAT* Offset;
    FLOAT* Pixel;

    if (!State->ViewportDirty)
        return;

    Pixel = NineExtensionRegister(Device, NineExtPixelScale);
    Pixel[0] = 1.0f / Rect[2];
    Pixel[1] = -1.0f / Rect[3];
    Offset = NineExtensionRegister(Device, NineExtScreenOffset);
    Offset[0] = 0.5f - (Rect[0] + Rect[2] * 0.5f);
    Offset[1] = 0.5f - (Rect[1] + Rect[3] * 0.5f);
    Offset[2] = -State->ZRange.MinZ;
    Offset[3] = 0.0f;
    Scale = NineExtensionRegister(Device, NineExtScreenScale);
    Scale[0] = 2.0f / Rect[2];
    Scale[1] = -2.0f / Rect[3];
    Scale[2] = 1.0f / (State->ZRange.MaxZ - State->ZRange.MinZ);
    Scale[3] = 1.0f;

    Viewport.TopLeftX = Rect[0];
    Viewport.TopLeftY = Rect[1];
    Viewport.Width = Rect[2];
    Viewport.Height = Rect[3];
    Viewport.MinDepth = State->ZRange.MinZ;
    Viewport.MaxDepth = State->ZRange.MaxZ;
    Device->Context->SetViewport(0, &Viewport);
    State->ViewportDirty = FALSE;
    State->ExtensionDirty = TRUE;
}

static
VOID
NineApplyScissor(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;

    if (State->RenderStates[D3DDDIRS_SCISSORTESTENABLE] != 0)
    {
        Device->Context->SetScissorRect(0, reinterpret_cast<const D3D12_RECT*>(&State->Scissor));
        Device->Context->SetScissorRectEnable(TRUE);
    }
    else
    {
        Device->Context->SetScissorRectEnable(FALSE);
    }
}

/* The point size is clamped once and kept clamped */
static
VOID
NineApplyPointSize(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;
    FLOAT* Point = NineExtensionRegister(Device, NineExtPointSize);

    if (!State->PointSizeDirty)
        return;
    Point[0] = std::min(std::max(Point[0], Point[1]), Point[2]);
    State->PointSizeLarge = Point[0] > 1.0f;
    State->PointSizeDirty = FALSE;
}

static
D3D_PRIMITIVE_TOPOLOGY
NineTopology(
    _In_ UINT Primitive)
{
    return Primitive == D3DPT_TRIANGLEFAN ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST
                                          : static_cast<D3D_PRIMITIVE_TOPOLOGY>(Primitive);
}

static
D3D12_PRIMITIVE_TOPOLOGY_TYPE
NineTopologyType(
    _In_ UINT Primitive)
{
    if (Primitive == D3DPT_POINTLIST)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    if (Primitive <= D3DPT_LINESTRIP)
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

/* The pipeline for the converted shaders and the state around them, built when it is new */
static
TL::PipelineState*
NineFindPipeline(
    _Inout_ NineDevice* Device,
    _In_ const NinePipelineKey* Key)
{
    TL::GRAPHICS_PIPELINE_STATE_DESC Desc = {};
    std::unique_ptr<NinePipeline> Made;

    for (const std::unique_ptr<NinePipeline>& Pipeline : Device->Pipelines)
    {
        if (memcmp(&Pipeline->Key, Key, sizeof(*Key)) == 0)
            return Pipeline->State.get();
    }

    Desc.pVertexShader = Key->Vertex->Shader.get();
    Desc.pPixelShader = Key->Pixel->Shader.get();
    Desc.pGeometryShader = Key->Geometry != nullptr ? Key->Geometry->Shader.get() : nullptr;
    Desc.BlendState = Key->Blend;
    Desc.SampleMask = Key->SampleMask;
    Desc.RasterizerState = Key->Raster;
    Desc.DepthStencilState = Key->Depth;
    Desc.InputLayout.pInputElementDescs = Key->Vertex->Layout.data();
    Desc.InputLayout.NumElements = static_cast<UINT>(Key->Vertex->Layout.size());
    Desc.PrimitiveTopologyType = Key->Topology;
    Desc.NumRenderTargets = Key->TargetCount;
    memcpy(Desc.RTVFormats, Key->TargetFormats, sizeof(Desc.RTVFormats));
    Desc.DSVFormat = Key->DepthFormat;
    Desc.SampleDesc = Key->Samples;

    Made = std::make_unique<NinePipeline>();
    Made->Key = *Key;
    Made->State.reset(new TL::PipelineState(&*Device->Context, Desc));
    Device->Pipelines.push_back(std::move(Made));
    return Device->Pipelines.back()->State.get();
}

/* The first bound target or the depth buffer, whose sample count the pipeline takes */
static
const NineResource*
NineFirstOutput(
    _In_ const NineDeviceState* State)
{
    for (UINT Slot = 0; Slot < NineTargetCount; Slot += 1)
    {
        if (State->Targets[Slot].Resource != nullptr)
            return State->Targets[Slot].Resource;
    }
    return State->DepthStencil;
}

/* Where the vertices and indices of a draw come from */
struct NineDrawSource
{
    NineOffsetKind VertexKind;
    UINT VertexValue;
    UINT VertexCount;
    NineOffsetKind IndexKind;
    UINT IndexValue;
    UINT IndexCount;

    /* The indices are a list the device made, bound by the draw itself */
    BOOLEAN MadeIndices;
};

/* Copies user memory streams into the upload buffer for this draw's vertices */
static
HRESULT
NineBindStreams(
    _Inout_ NineDevice* Device,
    _In_ const NineDrawSource* Source)
{
    NineDeviceState* State = &Device->State;
    TL::Resource* Buffers[NineScStreamCount] = {};
    UINT Strides[NineScStreamCount] = {};
    UINT Offsets[NineScStreamCount] = {};
    UINT Count = State->StreamsUsed != 0 ? State->StreamsUsed : NineScStreamCount;

    for (UINT Stream = 0; Stream < Count; Stream += 1)
    {
        NineStreamSlot* Slot = &State->Streams[Stream];

        Strides[Stream] = Slot->Stride;
        if (Slot->User != nullptr)
        {
            UINT Start = 0;
            UINT Size;
            NineUploadSlice Slice;

            if (State->VertexDecl == nullptr || (State->VertexDecl->StreamMask & (1u << Stream)) == 0)
                continue;
            if (Source->VertexKind != NineOffsetBytes)
                Start = Slot->Stride * Source->VertexValue;
            else if (Stream == 0)
                Start = Source->VertexValue;
            if (Start >= Slot->Limit)
                return E_INVALIDARG;
            Size = Slot->Stride * Source->VertexCount;
            Slice = NineAllocateUpload(&Device->UserRing, Start + Size);
            memcpy(Slice.Cpu + Start, Slot->User + Slot->Offset + Start, std::min(Slot->Limit - Start, Size));
            Buffers[Stream] = Slice.Buffer;
            Offsets[Stream] = Slice.Offset;
            if (Stream == 0 && Source->VertexKind == NineOffsetBytes)
                Offsets[Stream] += Source->VertexValue;
            continue;
        }

        if (Slot->Buffer != nullptr)
            Buffers[Stream] = Slot->Buffer->Underlying.get();
        Offsets[Stream] = Slot->Offset;
        if (Stream == 0 && Source->VertexKind == NineOffsetBytes)
            Offsets[Stream] += Source->VertexValue;
    }
    Device->Context->IaSetVertexBuffers(0, Count, Buffers, Strides, Offsets);
    return S_OK;
}

static
VOID
NineBindIndexBuffer(
    _Inout_ NineDevice* Device,
    _In_ const NineDrawSource* Source)
{
    NineDeviceState* State = &Device->State;
    TL::Resource* Buffer = nullptr;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    UINT Offset = 0;

    if (State->IndexStride == 2)
        Format = DXGI_FORMAT_R16_UINT;
    else if (State->IndexStride == 4)
        Format = DXGI_FORMAT_R32_UINT;

    if (State->UserIndices != nullptr && Source->IndexCount != 0)
    {
        UINT Start = Source->IndexKind == NineOffsetBytes ? Source->IndexValue : State->IndexStride * Source->IndexValue;
        UINT Size = State->IndexStride * Source->IndexCount;
        NineUploadSlice Slice = NineAllocateUpload(&Device->UserRing, Start + Size);

        memcpy(Slice.Cpu + Start, State->UserIndices + Start, std::min(State->UserIndexLimit - std::min(Start, State->UserIndexLimit), Size));
        Buffer = Slice.Buffer;
        Offset = Slice.Offset;
    }
    else if (State->IndexBuffer != nullptr)
    {
        Buffer = State->IndexBuffer->Underlying.get();
    }
    if (Source->IndexKind == NineOffsetBytes)
        Offset += Source->IndexValue;
    Device->Context->IaSetIndexBuffer(Buffer, Format, Offset);
}

/* The fog and bump constants of the pixel stage, uploaded when their states changed */
static
VOID
NineApplyPixelExtensions(
    _Inout_ NineDevice* Device)
{
    NineDeviceState* State = &Device->State;

    if (State->FogDirty)
    {
        const DWORD* Rs = State->RenderStates;
        FLOAT Fog[8];

        Fog[0] = NineColorChannel(Rs[D3DDDIRS_FOGCOLOR], 16);
        Fog[1] = NineColorChannel(Rs[D3DDDIRS_FOGCOLOR], 8);
        Fog[2] = NineColorChannel(Rs[D3DDDIRS_FOGCOLOR], 0);
        Fog[3] = NineColorChannel(Rs[D3DDDIRS_ALPHAREF], 0);
        memcpy(&Fog[4], &Rs[D3DDDIRS_FOGSTART], sizeof(FLOAT));
        memcpy(&Fog[5], &Rs[D3DDDIRS_FOGEND], sizeof(FLOAT));
        Fog[6] = Fog[5] - Fog[4] == 0.0f ? 0.0f : 1.0f / (Fog[5] - Fog[4]);
        memcpy(&Fog[7], &Rs[D3DDDIRS_FOGDENSITY], sizeof(FLOAT));
        NineVersionSlot(&Device->PixelFog, Fog, sizeof(Fog));
        Device->PixelFog.Dirty = TRUE;
        State->FogDirty = FALSE;
    }
    if (State->BumpDirty)
    {
        NineVersionSlot(&Device->PixelBump, Device->Bump, sizeof(Device->Bump));
        Device->PixelFog.Dirty = TRUE;
        State->BumpDirty = FALSE;
    }
}

/* Binds everything a draw needs; Skip says the reference drops the draw without an error */
static
HRESULT
NinePrepareDraw(
    _Inout_ NineDevice* Device,
    _In_ UINT Primitive,
    _In_ const NineDrawSource* Source,
    _Out_ BOOLEAN* Skip)
{
    NineDeviceState* State = &Device->State;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC* Desc = &State->Desc;
    const NineResource* Output;
    NineScRasterStates Raster;
    NinePipelineKey Key = {};
    NineConverted* Geometry;
    BOOLEAN HasDepth;
    BOOLEAN Multisampled;
    HRESULT hr;

    *Skip = TRUE;
    if ((State->TargetMask == 0 && State->DepthStencil == nullptr) || Primitive < D3DPT_POINTLIST ||
        Primitive > D3DPT_TRIANGLEFAN || Source->VertexCount == 0)
    {
        return S_OK;
    }
    *Skip = FALSE;

    if (State->Primitive != Primitive || State->Desc.PrimitiveTopologyType == 0)
        Device->Context->IaSetTopology(NineTopology(Primitive));
    State->Primitive = Primitive;

    hr = NineBindStreams(Device, Source);
    if (FAILED(hr))
        return hr;
    if (!Source->MadeIndices)
        NineBindIndexBuffer(Device, Source);
    NineApplyViewport(Device);
    NineApplyPointSize(Device);
    NineApplyScissor(Device);
    if (State->ExtensionDirty)
    {
        NineVersionSlot(&Device->VertexExtension, Device->Extension, NineExtensionSize);
        Device->VertexExtension.Dirty = TRUE;
        State->ExtensionDirty = FALSE;
    }
    NineBuildRaster(Device, &Raster);
    NineComparePixelKey(State, &Raster);

    if (State->VertexKeyDirty || State->Vertex == nullptr || (State->Dirty & (NineDirtyInputLayout | NineDirtyPretransformed)))
    {
        State->Dirty &= ~(NineDirtyInputLayout | NineDirtyPretransformed);
        State->PixelDirty = TRUE;
        hr = NineResolveVertexShader(Device, &Raster);
        if (FAILED(hr))
            return hr;
    }

    Geometry = State->Geometry;
    State->Geometry = nullptr;
    if (NineNeedsGeometry(&Raster, &State->Vertex->Outputs))
    {
        hr = NineResolveGeometryShader(Device, &Raster);
        if (FAILED(hr))
            return hr;
        NineVersionSlot(&Device->GeometryExtension, Device->Extension, NineExtensionSize);
        Device->GeometryExtension.Dirty = TRUE;
    }
    if (State->Geometry != Geometry)
        State->PixelDirty = TRUE;

    NineApplyPixelExtensions(Device);
    NineApplySamplers(Device);

    /* A blend change looks again at the alpha test the pixel shader was keyed with */
    if (State->PixelDirty || State->BlendDirty)
    {
        UINT Alpha = Raster.Words[1] & (NineRasterAlphaFunc | NineRasterAlphaTest);

        if ((Alpha & NineRasterAlphaTest) != 0 && State->AlphaToCoverage)
        {
            State->PixelAlpha = 8 << 21;
            State->PixelDirty = TRUE;
        }
        else if (Alpha != State->PixelAlpha)
        {
            State->PixelAlpha = Alpha;
            State->PixelDirty = TRUE;
        }
    }
    State->BlendDirty = FALSE;
    if (State->PixelDirty || State->Pixel == nullptr || State->Pixel->Shader == nullptr)
    {
        hr = NineResolvePixelShader(Device, &Raster);
        if (FAILED(hr))
            return hr;
    }
    NineApplyTextures(Device);

    Output = NineFirstOutput(State);
    HasDepth = State->DepthStencil != nullptr &&
               ((State->RenderStates[D3DDDIRS_ZENABLE] & 1) != 0 || (State->RenderStates[D3DDDIRS_STENCILENABLE] & 1) != 0);
    Desc->SampleDesc = Output != nullptr ? Output->Typed.SampleDesc : DXGI_SAMPLE_DESC{ 1, 0 };
    Multisampled = Desc->SampleDesc.Count > 1;
    Desc->DSVFormat = HasDepth ? State->DepthStencil->DxgiFormat : DXGI_FORMAT_UNKNOWN;
    NineApplyTargets(Device, HasDepth);
    NineBuildBlend(Device, &Desc->BlendState);
    Desc->SampleMask = State->RenderStates[D3DDDIRS_MULTISAMPLEMASK];
    if (Desc->SampleDesc.Count == 1 && Desc->SampleMask == 0)
        Desc->SampleMask = 0xFFFFFFFF;
    NineBuildDepth(Device, HasDepth, &Desc->DepthStencilState);
    NineBuildRasterizer(Device, HasDepth && Desc->DepthStencilState.DepthEnable, Desc->DSVFormat, Multisampled,
                        &Desc->RasterizerState);
    Desc->PrimitiveTopologyType = NineTopologyType(Primitive);

    Key.Vertex = State->Vertex;
    Key.Pixel = State->Pixel;
    Key.Geometry = State->Geometry;
    Key.Blend = Desc->BlendState;
    Key.SampleMask = Desc->SampleMask;
    Key.Raster = Desc->RasterizerState;
    Key.Depth = Desc->DepthStencilState;
    Key.Topology = Desc->PrimitiveTopologyType;
    Key.TargetCount = Desc->NumRenderTargets;
    memcpy(Key.TargetFormats, Desc->RTVFormats, sizeof(Key.TargetFormats));
    Key.DepthFormat = Desc->DSVFormat;
    Key.Samples = Desc->SampleDesc;
    State->Pipeline = NineFindPipeline(Device, &Key);
    Device->Context->SetPipelineState(State->Pipeline);

    Device->Context->OMSetStencilRef(static_cast<BYTE>(State->RenderStates[D3DDDIRS_STENCILREF]));
    {
        DWORD Factor = State->RenderStates[D3DDDIRS_BLENDFACTOR];
        FLOAT Rgba[4] = { NineColorChannel(Factor, 16), NineColorChannel(Factor, 8), NineColorChannel(Factor, 0),
                          NineColorChannel(Factor, 24) };

        Device->Context->OMSetBlendFactor(Rgba);
    }

    NineUploadStage<TL::e_VS>(Device, &State->VertexConstants, Device->VertexSlots, State->Vertex);
    NineUploadStage<TL::e_PS>(Device, &State->PixelConstants, Device->PixelSlots, State->Pixel);
    if (Device->VertexExtension.Dirty)
    {
        NineBindSlot<TL::e_VS>(Device, &Device->VertexExtension, NineExtensionSlot);
        Device->VertexExtension.Dirty = FALSE;
    }
    if (Device->GeometryExtension.Dirty)
    {
        NineBindSlot<TL::e_GS>(Device, &Device->GeometryExtension, NineExtensionSlot);
        Device->GeometryExtension.Dirty = FALSE;
    }
    if (Device->PixelFog.Dirty)
    {
        NineBindSlot<TL::e_PS>(Device, &Device->PixelFog, NineExtensionSlot);
        NineBindSlot<TL::e_PS>(Device, &Device->PixelBump, NineBumpSlot);
        Device->PixelFog.Dirty = FALSE;
    }
    return S_OK;
}

/* Vertices a primitive count covers */
static
UINT
NineVertexCount(
    _In_ UINT Primitive,
    _In_ UINT Count)
{
    switch (Primitive)
    {
        case D3DPT_POINTLIST:
            return Count;
        case D3DPT_LINELIST:
            return Count * 2;
        case D3DPT_LINESTRIP:
            return Count + 1;
        case D3DPT_TRIANGLELIST:
            return Count * 3;
        case D3DPT_TRIANGLESTRIP:
        case D3DPT_TRIANGLEFAN:
            return Count + 2;
        default:
            return 0;
    }
}

static
UINT
NineInstanceCount(
    _In_ const NineDeviceState* State)
{
    if (State->Frequencies[0] & D3DSTREAMSOURCE_INDEXEDDATA)
        return State->Frequencies[0] & ~D3DSTREAMSOURCE_INDEXEDDATA;
    return 1;
}

/* Runs a draw, turning translation layer exceptions into results */
template <typename Body>
static
HRESULT
NineGuardedDraw(
    _In_ Body Draw)
{
    try
    {
        return Draw();
    }
    catch (_com_error& Error)
    {
        return Error.Error();
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
}

/* A fan drawn as a list through indices {k + 1, k + 2, 0} the device keeps mapped */
static
HRESULT
NineDrawFan(
    _Inout_ NineDevice* Device,
    _In_ const NineDrawSource* Vertices,
    _In_ UINT Primitives)
{
    NineDeviceState* State = &Device->State;
    NineDrawSource Source = *Vertices;
    NineUploadSlice Slice;
    UINT* Indices;
    BOOLEAN Skip;
    HRESULT hr;

    if (Device->FanRing.Buffer.get() == nullptr || Primitives > Device->FanCapacity)
    {
        Device->FanRing.Capacity = 4 * ((3 * Primitives) & 0x3FFFFFFF);
        Device->FanRing.Cursor = Device->FanRing.Capacity;
        Slice = NineAllocateUpload(&Device->FanRing, Device->FanRing.Capacity);
        Indices = reinterpret_cast<UINT*>(Slice.Cpu);
        for (UINT Index = 0; Index < Primitives; Index += 1)
        {
            Indices[3 * Index] = Index + 1;
            Indices[3 * Index + 1] = Index + 2;
            Indices[3 * Index + 2] = 0;
        }
        Device->FanCapacity = Primitives;
    }

    Source.VertexCount = Primitives + 2;
    Source.MadeIndices = TRUE;
    Source.IndexKind = NineOffsetElement;
    Source.IndexValue = 0;
    Source.IndexCount = 3 * Primitives;
    hr = NinePrepareDraw(Device, D3DPT_TRIANGLEFAN, &Source, &Skip);
    if (FAILED(hr) || Skip)
        return hr;
    Device->Context->IaSetIndexBuffer(Device->FanRing.Buffer.get(), DXGI_FORMAT_R32_UINT, 0);
    Device->Context->DrawIndexedInstanced(3 * Primitives, NineInstanceCount(State), 0,
                                          Vertices->VertexKind == NineOffsetVertex ? Vertices->VertexValue : 0, 0);
    return S_OK;
}

HRESULT
APIENTRY
NineDrawPrimitive(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWPRIMITIVE* Args,
    _In_opt_ const UINT* FlagBuffer)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    UNREFERENCED_PARAMETER(FlagBuffer);
    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    return NineDxgiResult(NineGuardedDraw([&]() -> HRESULT
    {
        NineDrawSource Source = { NineOffsetVertex, Args->VStart, 0, NineOffsetElement, 0, 0 };
        BOOLEAN Skip;
        HRESULT hr;

        if (Args->PrimitiveType == D3DPT_TRIANGLEFAN)
            return NineDrawFan(Device, &Source, Args->PrimitiveCount);
        Source.VertexCount = NineVertexCount(Args->PrimitiveType, Args->PrimitiveCount);
        hr = NinePrepareDraw(Device, Args->PrimitiveType, &Source, &Skip);
        if (SUCCEEDED(hr) && !Skip)
            Device->Context->DrawInstanced(Source.VertexCount, NineInstanceCount(&Device->State), Args->VStart, 0);
        return hr;
    }));
}

HRESULT
APIENTRY
NineDrawPrimitive2(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWPRIMITIVE2* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    return NineDxgiResult(NineGuardedDraw([&]() -> HRESULT
    {
        NineDrawSource Source = { NineOffsetBytes, Args->FirstVertexOffset, 0, NineOffsetElement, 0, 0 };
        BOOLEAN Skip;
        HRESULT hr;

        if (Args->PrimitiveType == D3DPT_TRIANGLEFAN)
            return NineDrawFan(Device, &Source, Args->PrimitiveCount);
        Source.VertexCount = NineVertexCount(Args->PrimitiveType, Args->PrimitiveCount);
        hr = NinePrepareDraw(Device, Args->PrimitiveType, &Source, &Skip);
        if (SUCCEEDED(hr) && !Skip)
            Device->Context->DrawInstanced(Source.VertexCount, NineInstanceCount(&Device->State), 0, 0);
        return hr;
    }));
}

/* A mapped buffer of list indices {k + 1, k + 2, 0} picked out of fan indices */
static
TL::unique_comptr<TL::Resource>
NineMakeFanList(
    _Inout_ NineDevice* Device,
    _In_ const BYTE* Fan,
    _In_ UINT IndexSize,
    _In_ UINT Primitives)
{
    NineUploadRing Ring = {};
    NineUploadSlice Slice;
    UINT* List;

    Ring.Owner = Device;
    Ring.Alignment = NineSlotAlignment;
    Ring.Capacity = 4 * ((3 * Primitives) & 0x3FFFFFFF);
    Ring.Cursor = Ring.Capacity;
    Slice = NineAllocateUpload(&Ring, Ring.Capacity);
    List = reinterpret_cast<UINT*>(Slice.Cpu);
    for (UINT Index = 0; Index < Primitives; Index += 1)
    {
        UINT Picks[3] = { Index + 1, Index + 2, 0 };

        for (UINT Corner = 0; Corner < 3; Corner += 1)
        {
            List[3 * Index + Corner] = IndexSize == 2 ? reinterpret_cast<const USHORT*>(Fan)[Picks[Corner]]
                                                      : reinterpret_cast<const UINT*>(Fan)[Picks[Corner]];
        }
    }
    return std::move(Ring.Buffer);
}

/* The fan indices of an index buffer as a list, kept with the buffer until it is written or the start moves */
static
TL::Resource*
NineBufferFanList(
    _Inout_ NineDevice* Device,
    _Inout_ NineResource* Buffer,
    _In_ UINT Start,
    _In_ UINT Primitives)
{
    TL::unique_comptr<TL::Resource> Staging;
    TL::Resource* Readable = Buffer->Underlying.get();
    TL::MappedSubresource Mapped = {};

    if (Buffer->FanValid && Buffer->FanStart == Start)
        return Buffer->FanList.get();

    if ((Readable->AppDesc()->CPUAccessFlags() & TL::RESOURCE_CPU_ACCESS_READ) == 0)
    {
        Staging = NineStagingCopy(Buffer);
        Readable = Staging.get();
    }
    switch (Readable->AppDesc()->Usage())
    {
        case TL::RESOURCE_USAGE_DEFAULT:
            Device->Context->MapDefault(Readable, 0, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
            break;
        case TL::RESOURCE_USAGE_DYNAMIC:
            if (Readable->Parent()->m_heapDesc.Properties.CPUPageProperty == D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE)
            {
                Device->Context->MapDynamicTexture(Readable, 0, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
                break;
            }
            Device->Context->MapUnderlyingSynchronize(Readable, 0, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
            break;
        default:
            Device->Context->MapUnderlyingSynchronize(Readable, 0, TL::MAP_TYPE_READ, false, nullptr, &Mapped);
            break;
    }

    Buffer->FanList = NineMakeFanList(Device, static_cast<const BYTE*>(Mapped.pData) + Start, Device->State.IndexStride,
                                      Primitives);
    Buffer->FanStart = Start;
    Buffer->FanValid = TRUE;
    Device->Context->Unmap(Readable, 0, TL::MAP_TYPE_READ, nullptr);
    return Buffer->FanList.get();
}

/* An indexed fan drawn as a list through indices made from the fan's own */
static
HRESULT
NineDrawIndexedFan(
    _Inout_ NineDevice* Device,
    _In_ const NineDrawSource* Vertices,
    _In_ UINT Start,
    _In_ UINT Primitives)
{
    NineDeviceState* State = &Device->State;
    NineDrawSource Source = *Vertices;
    TL::Resource* List;
    BOOLEAN Skip;
    HRESULT hr;

    if (State->UserIndices != nullptr)
    {
        Device->UserFanList = NineMakeFanList(Device, State->UserIndices + Start, State->IndexStride, Primitives);
        List = Device->UserFanList.get();
    }
    else if (State->IndexBuffer != nullptr)
    {
        List = NineBufferFanList(Device, State->IndexBuffer, Start, Primitives);
    }
    else
    {
        return S_OK;
    }

    Source.IndexCount = 3 * Primitives;
    Source.MadeIndices = TRUE;
    hr = NinePrepareDraw(Device, D3DPT_TRIANGLEFAN, &Source, &Skip);
    if (FAILED(hr) || Skip)
        return hr;
    Device->Context->IaSetIndexBuffer(List, DXGI_FORMAT_R32_UINT, Source.IndexKind == NineOffsetBytes ? Source.IndexValue : 0);
    Device->Context->DrawIndexedInstanced(3 * Primitives, NineInstanceCount(State), 0,
                                          Vertices->VertexKind == NineOffsetVertex ? Vertices->VertexValue : 0, 0);
    return S_OK;
}

HRESULT
APIENTRY
NineDrawIndexedPrimitive(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    return NineDxgiResult(NineGuardedDraw([&]() -> HRESULT
    {
        UINT Count = NineVertexCount(Args->PrimitiveType, Args->PrimitiveCount);
        NineDrawSource Source = { NineOffsetVertex, static_cast<UINT>(Args->BaseVertexIndex),
                                  Args->MinIndex + Args->NumVertices, NineOffsetElement, Args->StartIndex, Count };
        BOOLEAN Skip;
        HRESULT hr;

        if (Args->PrimitiveType == D3DPT_TRIANGLEFAN)
            return NineDrawIndexedFan(Device, &Source, Args->StartIndex * Device->State.IndexStride,
                                      Args->PrimitiveCount);
        hr = NinePrepareDraw(Device, Args->PrimitiveType, &Source, &Skip);
        if (SUCCEEDED(hr) && !Skip)
        {
            Device->Context->DrawIndexedInstanced(Count, NineInstanceCount(&Device->State), Args->StartIndex,
                                                  Args->BaseVertexIndex, 0);
        }
        return hr;
    }));
}

HRESULT
APIENTRY
NineDrawIndexedPrimitive2(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_DRAWINDEXEDPRIMITIVE2* Args,
    _In_ UINT IndicesSize,
    _In_ const VOID* Indices,
    _In_opt_ const UINT* FlagBuffer)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;
    if (FlagBuffer != nullptr)
        return E_NOTIMPL;

    return NineDxgiResult(NineGuardedDraw([&]() -> HRESULT
    {
        NineDeviceState* State = &Device->State;
        UINT Count = NineVertexCount(Args->PrimitiveType, Args->PrimitiveCount);
        NineDrawSource Source = { NineOffsetBytes, 0, Args->NumVertices, NineOffsetBytes, Args->StartIndexOffset, Count };
        INT BaseVertex = 0;
        BOOLEAN Skip;
        HRESULT hr;

        if (Args->BaseVertexOffset >= 0)
        {
            Source.VertexValue = static_cast<UINT>(Args->BaseVertexOffset);
        }
        else
        {
            if (State->Streams[0].Stride == 0)
                __fastfail(FAST_FAIL_INVALID_ARG);
            BaseVertex = Args->BaseVertexOffset / static_cast<INT>(State->Streams[0].Stride);
        }

        NineSetIndicesUm(Device, IndicesSize, Indices);
        if (Args->PrimitiveType == D3DPT_TRIANGLEFAN)
            return NineDrawIndexedFan(Device, &Source, Args->StartIndexOffset, Args->PrimitiveCount);
        hr = NinePrepareDraw(Device, Args->PrimitiveType, &Source, &Skip);
        if (SUCCEEDED(hr) && !Skip)
            Device->Context->DrawIndexedInstanced(Count, NineInstanceCount(State), 0, BaseVertex, 0);
        return hr;
    }));
}

HRESULT
APIENTRY
NineSetStreamSourceUm(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCEUM* Args,
    _In_ const VOID* Data)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    D3DDDIARG_SETSTREAMSOURCE Unbind = {};
    NineStreamSlot* Slot;

    if (Device == nullptr || Args == nullptr || Data == nullptr || Args->Stream >= NineScStreamCount)
        return E_INVALIDARG;

    Unbind.Stream = Args->Stream;
    NineSetStreamSource(Device, &Unbind);
    Slot = &Device->State.Streams[Args->Stream];
    Slot->User = static_cast<const BYTE*>(Data);
    Slot->Limit = ~0u;
    Slot->Stride = Args->Stride;
    Slot->Offset = 0;
    if (Device->State.StreamsUsed < Args->Stream + 1)
        Device->State.StreamsUsed = Args->Stream + 1;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetIndicesUm(
    _In_ HANDLE hDevice,
    _In_ UINT IndexSize,
    _In_ const VOID* Data)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    D3DDDIARG_SETINDICES Unbind = {};

    if (Device == nullptr)
        return E_INVALIDARG;

    NineSetIndices(Device, &Unbind);
    Device->State.UserIndices = static_cast<const BYTE*>(Data);
    Device->State.UserIndexLimit = ~0u;
    Device->State.IndexStride = IndexSize;
    return NineDxgiResult(S_OK);
}

/* Depth formats a sampler compares against instead of reading */
static
BOOLEAN
NineIsShadowFormat(
    _In_ D3DDDIFORMAT Format)
{
    return Format == D3DDDIFMT_D24S8 || Format == D3DDDIFMT_D24X8 || Format == D3DDDIFMT_D16;
}

HRESULT
APIENTRY
NineSetTexture(
    _In_ HANDLE hDevice,
    _In_ UINT Stage,
    _In_ HANDLE hTexture)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Texture = hTexture == reinterpret_cast<HANDLE>(1) ? nullptr : static_cast<NineResource*>(hTexture);
    NineDeviceState* State;
    NineStageTexture* Slot;
    UINT Index;

    if (Device == nullptr)
        return E_INVALIDARG;
    if (Stage == 256 || Stage == 16)
        return NineDxgiResult(S_OK);
    Index = Stage >= 257 && Stage <= 260 ? Stage - 240 : Stage;
    if (Index >= NineStageCount)
        return NineDxgiResult(S_OK);

    State = &Device->State;
    Slot = &State->Textures[Index];
    if (Texture != nullptr)
    {
        BOOLEAN Shadow = NineIsShadowFormat(Texture->Format);

        if (Shadow != Slot->Shadow)
        {
            UINT Bit = Index < 16 ? 1u << (11 + Index) : 1u << (27 + Index - 17);

            Slot->Shadow = Shadow;
            if (Shadow)
                State->ShadowSamplers |= Bit;
            else
                State->ShadowSamplers &= ~Bit;
            if (Index >= 17)
                State->VertexKeyDirty = TRUE;
        }
        Slot->SwappedRedBlue = Texture->Format == D3DDDIFMT_A2R10G10B10;
    }

    if (Slot->Texture != Texture)
    {
        Slot->Texture = Texture;
        if (Index < 16)
            State->TextureKinds[Index] = static_cast<BYTE>(Texture != nullptr ? Texture->TextureType : 1);
    }
    NineNotePixelKey(Device);
    return NineDxgiResult(S_OK);
}
