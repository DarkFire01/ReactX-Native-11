/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     State blocks: recording, the predefined kinds, capture and apply
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

/* Render states each predefined kind gathers, in the order the system replays them */
static const BYTE D9AllBlockStates[] =
{
    29, 7, 8, 9, 14, 15, 16, 19, 20, 22, 23, 24, 25, 26, 28, 33, 34, 35, 36, 37, 38, 40, 27, 195, 48, 52, 53,
    54, 55, 56, 57, 58, 59, 60, 128, 129, 130, 131, 132, 133, 134, 135, 198, 199, 200, 201, 202, 203, 204, 205,
    139, 141, 140, 136, 137, 143, 142, 148, 147, 145, 146, 151, 152, 154, 155, 156, 157, 158, 159, 160, 161, 162,
    163, 166, 167, 168, 170, 171, 172, 173, 174, 175, 176, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188,
    189, 190, 191, 192, 193, 194, 206, 207, 208, 209,
};

static const BYTE D9PixelBlockStates[] =
{
    7, 8, 9, 14, 15, 16, 19, 20, 23, 24, 25, 26, 33, 36, 37, 38, 40, 27, 195, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    128, 129, 130, 131, 132, 133, 134, 135, 198, 199, 200, 201, 202, 203, 204, 205, 168, 171, 174, 175, 176, 185,
    186, 187, 188, 189, 190, 191, 192, 193, 194, 206, 207, 208, 209,
};

static const BYTE D9VertexBlockStates[] =
{
    9, 29, 22, 28, 34, 35, 36, 37, 38, 48, 139, 141, 140, 136, 137, 143, 142, 148, 147, 145, 146, 151, 152, 154,
    155, 156, 157, 158, 159, 160, 161, 162, 163, 166, 167, 170, 172, 173, 178, 179, 180, 181, 182, 183, 184,
};

/* Texture stage states: everything a stage blends and transforms with, or only how it picks coordinates */
static const BYTE D9BlockStageStates[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 22, 23, 24, 26, 27, 28 };
static const BYTE D9VertexBlockStageStates[] = { D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS };

/* Sampler states run from the address modes to the displacement map offset, which only the full kind takes */
static constexpr UINT D9BlockSamplerStates = D3DSAMP_DMAPOFFSET;

/* Pixel and vertex samplers, and the number a block gives the first vertex sampler after the displacement map */
static constexpr UINT D9BlockPixelSamplers = 16;
static constexpr UINT D9BlockVertexSamplers = 4;

/* The constants of each kind go into a block in pieces of this many registers */
static constexpr UINT D9BlockConstantPiece = 128;

static
UINT
D9PaddedSize(
    _In_ UINT Size)
{
    return (Size + 7) & ~7u;
}

D9StateBlock::~D9StateBlock()
{
    for (D9BlockEntry* Entry = Next(nullptr); Entry != nullptr; Entry = Next(Entry))
        Hold(Entry, FALSE);
    delete[] Entries;
}

/* Textures and buffers stay alive while a block names them */
VOID
D9StateBlock::Hold(
    _In_ const D9BlockEntry* Entry,
    _In_ BOOLEAN Take)
{
    const VOID* Values = Entry + 1;

    switch (Entry->Op)
    {
    case D9BlockOp::Texture:
    {
        IDirect3DBaseTexture9* Texture = *static_cast<IDirect3DBaseTexture9* const*>(Values);

        if (Texture != nullptr)
            D9HoldTexture(Texture, Take);
        break;
    }
    case D9BlockOp::StreamSource:
    {
        IDirect3DVertexBuffer9* Buffer = static_cast<const D9BlockStream*>(Values)->Buffer;

        if (Buffer == nullptr)
            break;
        if (Take)
            static_cast<D9VertexBuffer*>(Buffer)->Bind();
        else
            static_cast<D9VertexBuffer*>(Buffer)->Unbind();
        break;
    }
    case D9BlockOp::Indices:
    {
        IDirect3DIndexBuffer9* Buffer = *static_cast<IDirect3DIndexBuffer9* const*>(Values);

        if (Buffer == nullptr)
            break;
        if (Take)
            static_cast<D9IndexBuffer*>(Buffer)->Bind();
        else
            static_cast<D9IndexBuffer*>(Buffer)->Unbind();
        break;
    }
    default:
        break;
    }
}

HRESULT
D9StateBlock::Add(
    _In_ D9BlockOp Op,
    _In_ UINT Index,
    _In_ UINT Type,
    _In_reads_bytes_(Size) const VOID* Values,
    _In_ UINT Size)
{
    UINT Needed = sizeof(D9BlockEntry) + D9PaddedSize(Size);
    D9BlockEntry* Entry;

    if (Capacity - Used < Needed)
    {
        UINT Grown = Capacity + (Needed > 1024 ? Needed : 1024);
        BYTE* Larger = new (std::nothrow) BYTE[Grown];

        if (Larger == nullptr)
        {
            D9Trace(L"StateBlock: no memory for %u bytes", Grown);
            return E_OUTOFMEMORY;
        }
        if (Entries != nullptr)
            CopyMemory(Larger, Entries, Used);
        delete[] Entries;
        Entries = Larger;
        Capacity = Grown;
    }

    Entry = reinterpret_cast<D9BlockEntry*>(Entries + Used);
    Entry->Op = Op;
    Entry->Index = Index;
    Entry->Type = Type;
    Entry->Size = D9PaddedSize(Size);
    ZeroMemory(Entry + 1, Entry->Size);
    if (Size != 0)
        CopyMemory(Entry + 1, Values, Size);
    Used += Needed;
    Hold(Entry, TRUE);
    return S_OK;
}

_Ret_maybenull_
D9BlockEntry*
D9StateBlock::Next(
    _In_opt_ D9BlockEntry* Entry) const
{
    BYTE* Place = Entries;

    if (Entry != nullptr)
        Place = reinterpret_cast<BYTE*>(Entry + 1) + Entry->Size;
    if (Place == nullptr || Place >= Entries + Used)
        return nullptr;
    return reinterpret_cast<D9BlockEntry*>(Place);
}

STDMETHODIMP
D9StateBlock::QueryInterface(
    _In_ REFIID Iid,
    _COM_Outptr_ VOID** Object)
{
    if (Object == nullptr)
        return D3DERR_INVALIDCALL;

    *Object = nullptr;
    if (IsEqualGUID(Iid, __uuidof(IUnknown)) || IsEqualGUID(Iid, __uuidof(IDirect3DStateBlock9)))
    {
        *Object = static_cast<IDirect3DStateBlock9*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG)
D9StateBlock::AddRef()
{
    return AddRefChild();
}

/* The block lets go of what it holds before its device may go away */
STDMETHODIMP_(ULONG)
D9StateBlock::Release()
{
    LONG Count = InterlockedDecrement(&RefCount);
    D9Device* Owner = Device;

    if (Count != 0)
        return static_cast<ULONG>(Count);
    delete this;
    Owner->Release();
    return 0;
}

STDMETHODIMP
D9StateBlock::GetDevice(
    _Out_ IDirect3DDevice9** Result)
{
    if (Result == nullptr)
        return D3DERR_INVALIDCALL;

    *Result = Device;
    Device->AddRef();
    return S_OK;
}

STDMETHODIMP
D9StateBlock::Capture()
{
    return Device->CaptureBlock(this);
}

STDMETHODIMP
D9StateBlock::Apply()
{
    return Device->ApplyBlock(this);
}

/* Each entry goes back through the call that sets it, so the driver sees only what changes */
HRESULT
D9Device::ApplyBlock(
    _In_ D9StateBlock* Block)
{
    D9_DEVICE_LOCK();

    if (Recording != nullptr)
        return D3DERR_INVALIDCALL;

    for (D9BlockEntry* Entry = Block->Next(nullptr); Entry != nullptr; Entry = Block->Next(Entry))
    {
        VOID* Values = D9BlockValues(Entry);
        DWORD Value = *static_cast<DWORD*>(Values);

        switch (Entry->Op)
        {
        case D9BlockOp::RenderState:
            SetRenderState(static_cast<D3DRENDERSTATETYPE>(Entry->Index), Value);
            break;
        case D9BlockOp::StageState:
            SetTextureStageState(Entry->Index, static_cast<D3DTEXTURESTAGESTATETYPE>(Entry->Type), Value);
            break;
        case D9BlockOp::SamplerState:
            SetSamplerState(Entry->Index, static_cast<D3DSAMPLERSTATETYPE>(Entry->Type), Value);
            break;
        case D9BlockOp::Texture:
            SetTexture(Entry->Index, *static_cast<IDirect3DBaseTexture9**>(Values));
            break;
        case D9BlockOp::StreamSource:
        {
            D9BlockStream* Stream = static_cast<D9BlockStream*>(Values);

            SetStreamSource(Entry->Index, Stream->Buffer, Stream->Offset, Stream->Stride);
            break;
        }
        case D9BlockOp::StreamFrequency:
            SetStreamSourceFreq(Entry->Index, Value);
            break;
        case D9BlockOp::Indices:
            SetIndices(*static_cast<IDirect3DIndexBuffer9**>(Values));
            break;
        case D9BlockOp::NPatchMode:
            SetNPatchMode(*static_cast<FLOAT*>(Values));
            break;
        case D9BlockOp::Viewport:
            SetViewport(static_cast<D3DVIEWPORT9*>(Values));
            break;
        case D9BlockOp::Scissor:
            SetScissorRect(static_cast<RECT*>(Values));
            break;
        case D9BlockOp::Transform:
            SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(Entry->Index), static_cast<D3DMATRIX*>(Values));
            break;
        case D9BlockOp::ClipPlane:
            SetClipPlane(Entry->Index, static_cast<FLOAT*>(Values));
            break;
        case D9BlockOp::Material:
            SetMaterial(static_cast<D3DMATERIAL9*>(Values));
            break;
        case D9BlockOp::Light:
            SetLight(Entry->Index, static_cast<D3DLIGHT9*>(Values));
            break;
        case D9BlockOp::LightEnable:
            LightEnable(Entry->Index, Value != 0);
            break;
        case D9BlockOp::Fvf:
            SetFVF(Value);
            break;
        case D9BlockOp::VertexFloats:
            SetVertexShaderConstantF(Entry->Index, static_cast<FLOAT*>(Values), Entry->Type);
            break;
        case D9BlockOp::VertexInts:
            SetVertexShaderConstantI(Entry->Index, static_cast<INT*>(Values), Entry->Type);
            break;
        case D9BlockOp::VertexBools:
            SetVertexShaderConstantB(Entry->Index, static_cast<BOOL*>(Values), Entry->Type);
            break;
        case D9BlockOp::PixelFloats:
            SetPixelShaderConstantF(Entry->Index, static_cast<FLOAT*>(Values), Entry->Type);
            break;
        case D9BlockOp::PixelInts:
            SetPixelShaderConstantI(Entry->Index, static_cast<INT*>(Values), Entry->Type);
            break;
        case D9BlockOp::PixelBools:
            SetPixelShaderConstantB(Entry->Index, static_cast<BOOL*>(Values), Entry->Type);
            break;
        }
    }
    return S_OK;
}

/* Each entry takes the value the device holds now; a light past the device's light table fails the capture */
HRESULT
D9Device::CaptureBlock(
    _In_ D9StateBlock* Block)
{
    D9_DEVICE_LOCK();

    if (Recording != nullptr)
        return D3DERR_INVALIDCALL;

    for (D9BlockEntry* Entry = Block->Next(nullptr); Entry != nullptr; Entry = Block->Next(Entry))
    {
        VOID* Values = D9BlockValues(Entry);
        DWORD* Value = static_cast<DWORD*>(Values);

        switch (Entry->Op)
        {
        case D9BlockOp::RenderState:
            *Value = RenderStates[Entry->Index];
            break;
        case D9BlockOp::StageState:
            GetTextureStageState(Entry->Index, static_cast<D3DTEXTURESTAGESTATETYPE>(Entry->Type), Value);
            break;
        case D9BlockOp::SamplerState:
            GetSamplerState(Entry->Index, static_cast<D3DSAMPLERSTATETYPE>(Entry->Type), Value);
            break;
        case D9BlockOp::Texture:
        {
            IDirect3DBaseTexture9* Texture = nullptr;

            D9StateBlock::Hold(Entry, FALSE);
            GetTexture(Entry->Index, &Texture);
            *static_cast<IDirect3DBaseTexture9**>(Values) = Texture;
            D9StateBlock::Hold(Entry, TRUE);
            if (Texture != nullptr)
                Texture->Release();
            break;
        }
        case D9BlockOp::StreamSource:
        {
            D9BlockStream* Stream = static_cast<D9BlockStream*>(Values);
            IDirect3DVertexBuffer9* Buffer = nullptr;

            D9StateBlock::Hold(Entry, FALSE);
            GetStreamSource(Entry->Index, &Buffer, &Stream->Offset, &Stream->Stride);
            Stream->Buffer = Buffer;
            D9StateBlock::Hold(Entry, TRUE);
            if (Buffer != nullptr)
                Buffer->Release();
            break;
        }
        case D9BlockOp::StreamFrequency:
            GetStreamSourceFreq(Entry->Index, reinterpret_cast<UINT*>(Value));
            break;
        case D9BlockOp::Indices:
        {
            IDirect3DIndexBuffer9* Buffer = nullptr;

            D9StateBlock::Hold(Entry, FALSE);
            GetIndices(&Buffer);
            *static_cast<IDirect3DIndexBuffer9**>(Values) = Buffer;
            D9StateBlock::Hold(Entry, TRUE);
            if (Buffer != nullptr)
                Buffer->Release();
            break;
        }
        case D9BlockOp::NPatchMode:
            *static_cast<FLOAT*>(Values) = PatchSegments;
            break;
        case D9BlockOp::Viewport:
            *static_cast<D3DVIEWPORT9*>(Values) = CurrentViewport;
            break;
        case D9BlockOp::Scissor:
            *static_cast<RECT*>(Values) = ScissorRect;
            break;
        case D9BlockOp::Transform:
            GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(Entry->Index), static_cast<D3DMATRIX*>(Values));
            break;
        case D9BlockOp::ClipPlane:
            GetClipPlane(Entry->Index, static_cast<FLOAT*>(Values));
            break;
        case D9BlockOp::Material:
            *static_cast<D3DMATERIAL9*>(Values) = CurrentMaterial;
            break;
        case D9BlockOp::Light:
            if (Entry->Index >= LightCapacity)
                return D3DERR_INVALIDCALL;
            *static_cast<D3DLIGHT9*>(Values) = Lights[Entry->Index].Light;
            break;
        case D9BlockOp::LightEnable:
            if (Entry->Index >= LightCapacity)
                return D3DERR_INVALIDCALL;
            *Value = Lights[Entry->Index].Enabled;
            break;
        case D9BlockOp::Fvf:
            *Value = CurrentFvf;
            break;
        case D9BlockOp::VertexFloats:
            CopyMemory(Values, VertexFloats[Entry->Index], Entry->Type * sizeof(*VertexFloats));
            break;
        case D9BlockOp::VertexInts:
            CopyMemory(Values, VertexInts[Entry->Index], Entry->Type * sizeof(*VertexInts));
            break;
        case D9BlockOp::VertexBools:
            CopyMemory(Values, &VertexBools[Entry->Index], Entry->Type * sizeof(*VertexBools));
            break;
        case D9BlockOp::PixelFloats:
            CopyMemory(Values, PixelFloats[Entry->Index], Entry->Type * sizeof(*PixelFloats));
            break;
        case D9BlockOp::PixelInts:
            CopyMemory(Values, PixelInts[Entry->Index], Entry->Type * sizeof(*PixelInts));
            break;
        case D9BlockOp::PixelBools:
            CopyMemory(Values, &PixelBools[Entry->Index], Entry->Type * sizeof(*PixelBools));
            break;
        }
    }
    return S_OK;
}

/* Constant ranges a block keeps as one entry each */
HRESULT
D9Device::RecordConstants(
    _In_ D9BlockOp Op,
    _In_ UINT Start,
    _In_ UINT Count,
    _In_reads_bytes_opt_(Count * Stride) const VOID* Data,
    _In_ UINT Stride)
{
    return Recording->Add(Op, Start, Count, Data, Count * Stride);
}

/* Every register of one constant kind, a piece at a time */
static
HRESULT
D9AddConstants(
    _Inout_ D9StateBlock* Block,
    _In_ D9BlockOp Op,
    _In_reads_bytes_(Registers * Stride) const BYTE* Data,
    _In_ UINT Registers,
    _In_ UINT Stride)
{
    HRESULT hr = S_OK;

    for (UINT Start = 0; Start < Registers && SUCCEEDED(hr); Start += D9BlockConstantPiece)
    {
        UINT Count = Registers - Start < D9BlockConstantPiece ? Registers - Start : D9BlockConstantPiece;

        hr = Block->Add(Op, Start, Count, Data + Start * Stride, Count * Stride);
    }
    return hr;
}

/* Gathers what a predefined kind of block covers, in the order the system replays it */
HRESULT
D9Device::FillBlock(
    _Inout_ D9StateBlock* Block,
    _In_ D3DSTATEBLOCKTYPE Type)
{
    const BYTE* States = nullptr;
    UINT StateCount = 0;
    const BYTE* StageTypes = D9BlockStageStates;
    UINT StageTypeCount = _countof(D9BlockStageStates);
    UINT SamplerTypes = D9BlockSamplerStates;
    UINT Stages = Caps.MaxTextureBlendStages < D9StageCount ? Caps.MaxTextureBlendStages : D9StageCount;
    UINT StreamCount = Caps.MaxStreams < D9StreamCount ? Caps.MaxStreams : D9StreamCount;
    BOOLEAN Displacement = (Caps.DevCaps2 & (D3DDEVCAPS2_DMAPNPATCH | D3DDEVCAPS2_PRESAMPLEDDMAPNPATCH)) != 0;
    UINT Samplers[D9BlockPixelSamplers + 1 + D9BlockVertexSamplers];
    UINT SamplerCount = 0;
    HRESULT hr = S_OK;

    switch (Type)
    {
    case D3DSBT_ALL:
        States = D9AllBlockStates;
        StateCount = _countof(D9AllBlockStates);
        break;
    case D3DSBT_PIXELSTATE:
        States = D9PixelBlockStates;
        StateCount = _countof(D9PixelBlockStates);
        SamplerTypes = D9BlockSamplerStates - 1;
        break;
    case D3DSBT_VERTEXSTATE:
        States = D9VertexBlockStates;
        StateCount = _countof(D9VertexBlockStates);
        StageTypes = D9VertexBlockStageStates;
        StageTypeCount = _countof(D9VertexBlockStageStates);
        break;
    default:
        /* The system takes a kind of zero as a block with nothing in it */
        return Type == 0 ? S_OK : D3DERR_INVALIDCALL;
    }

    for (UINT Index = 0; Index < StateCount && SUCCEEDED(hr); Index++)
        hr = Block->Add(D9BlockOp::RenderState, States[Index], 0, &RenderStates[States[Index]], sizeof(DWORD));
    for (UINT Stage = 0; Stage < Stages && SUCCEEDED(hr); Stage++)
    {
        for (UINT Index = 0; Index < StageTypeCount && SUCCEEDED(hr); Index++)
            hr = Block->Add(D9BlockOp::StageState, Stage, StageTypes[Index], &StageStates[Stage][StageTypes[Index]],
                            sizeof(DWORD));
    }

    for (UINT Sampler = 0; Sampler < D9BlockPixelSamplers; Sampler++)
        Samplers[SamplerCount++] = Sampler;
    if (Displacement)
        Samplers[SamplerCount++] = D3DDMAPSAMPLER;
    for (UINT Sampler = 0; Sampler < D9BlockVertexSamplers; Sampler++)
        Samplers[SamplerCount++] = D3DVERTEXTEXTURESAMPLER0 + Sampler;

    if (Type == D3DSBT_VERTEXSTATE)
    {
        D3DLIGHT9 Light;
        BOOL Enabled;

        if (SUCCEEDED(hr))
            hr = Block->Add(D9BlockOp::NPatchMode, 0, 0, &PatchSegments, sizeof(PatchSegments));
        for (UINT Index = 0; Index < LightCapacity && SUCCEEDED(hr); Index++)
        {
            if (FAILED(GetLight(Index, &Light)))
                continue;
            GetLightEnable(Index, &Enabled);
            Enabled = Enabled != FALSE;
            hr = Block->Add(D9BlockOp::Light, Index, 0, &Light, sizeof(Light));
            if (SUCCEEDED(hr))
                hr = Block->Add(D9BlockOp::LightEnable, Index, 0, &Enabled, sizeof(Enabled));
        }
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::VertexFloats, reinterpret_cast<BYTE*>(VertexFloats),
                                Limits.VertexFloats, sizeof(*VertexFloats));
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::VertexInts, reinterpret_cast<BYTE*>(VertexInts), Limits.VertexInts,
                                sizeof(*VertexInts));
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::VertexBools, reinterpret_cast<BYTE*>(VertexBools),
                                Limits.VertexBools, sizeof(*VertexBools));
        for (UINT Stream = 0; Stream < StreamCount && SUCCEEDED(hr); Stream++)
            hr = Block->Add(D9BlockOp::StreamFrequency, Stream, 0, &Streams[Stream].Frequency, sizeof(UINT));
        return hr;
    }

    for (UINT Index = 0; Index < SamplerCount && SUCCEEDED(hr); Index++)
    {
        for (UINT Kind = D3DSAMP_ADDRESSU; Kind <= SamplerTypes && SUCCEEDED(hr); Kind++)
        {
            DWORD Value = 0;

            GetSamplerState(Samplers[Index], static_cast<D3DSAMPLERSTATETYPE>(Kind), &Value);
            hr = Block->Add(D9BlockOp::SamplerState, Samplers[Index], Kind, &Value, sizeof(Value));
        }
    }

    if (Type == D3DSBT_PIXELSTATE)
    {
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::PixelFloats, reinterpret_cast<BYTE*>(PixelFloats),
                                Limits.PixelFloats, sizeof(*PixelFloats));
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::PixelInts, reinterpret_cast<BYTE*>(PixelInts), Limits.PixelInts,
                                sizeof(*PixelInts));
        if (SUCCEEDED(hr))
            hr = D9AddConstants(Block, D9BlockOp::PixelBools, reinterpret_cast<BYTE*>(PixelBools), Limits.PixelBools,
                                sizeof(*PixelBools));
        return hr;
    }

    for (UINT Index = 0; Index < SamplerCount && SUCCEEDED(hr); Index++)
    {
        IDirect3DBaseTexture9* Texture = nullptr;

        GetTexture(Samplers[Index], &Texture);
        hr = Block->Add(D9BlockOp::Texture, Samplers[Index], 0, &Texture, sizeof(Texture));
        if (Texture != nullptr)
            Texture->Release();
    }
    for (UINT Stream = 0; Stream < StreamCount && SUCCEEDED(hr); Stream++)
    {
        D9BlockStream Bound = {};

        GetStreamSource(Stream, &Bound.Buffer, &Bound.Offset, &Bound.Stride);
        hr = Block->Add(D9BlockOp::StreamSource, Stream, 0, &Bound, sizeof(Bound));
        if (Bound.Buffer != nullptr)
            Bound.Buffer->Release();
        if (SUCCEEDED(hr))
            hr = Block->Add(D9BlockOp::StreamFrequency, Stream, 0, &Streams[Stream].Frequency, sizeof(UINT));
    }
    if (SUCCEEDED(hr))
        hr = Block->Add(D9BlockOp::Indices, 0, 0, &Indices, sizeof(Indices));
    if (SUCCEEDED(hr))
        hr = Block->Add(D9BlockOp::NPatchMode, 0, 0, &PatchSegments, sizeof(PatchSegments));
    if (SUCCEEDED(hr))
        hr = Block->Add(D9BlockOp::Viewport, 0, 0, &CurrentViewport, sizeof(CurrentViewport));
    if (SUCCEEDED(hr))
        hr = Block->Add(D9BlockOp::Scissor, 0, 0, &ScissorRect, sizeof(ScissorRect));

    /* The world matrices, then the view and projection, then one texture matrix per stage */
    for (UINT Index = 0; Index < 256 + 2 + Stages && SUCCEEDED(hr); Index++)
    {
        UINT Kind = Index < 256 ? D3DTS_WORLDMATRIX(Index) : Index == 256 ? D3DTS_VIEW :
                    Index == 257 ? D3DTS_PROJECTION : D3DTS_TEXTURE0 + (Index - 258);
        D3DMATRIX Matrix = {};

        GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(Kind), &Matrix);
        hr = Block->Add(D9BlockOp::Transform, Kind, 0, &Matrix, sizeof(Matrix));
    }
    for (UINT Plane = 0; Plane < Caps.MaxUserClipPlanes && SUCCEEDED(hr); Plane++)
        hr = Block->Add(D9BlockOp::ClipPlane, Plane, 0, ClipPlanes[Plane], sizeof(ClipPlanes[Plane]));
    if (SUCCEEDED(hr))
        hr = Block->Add(D9BlockOp::Material, 0, 0, &CurrentMaterial, sizeof(CurrentMaterial));
    for (UINT Index = 0; Index < LightCapacity && SUCCEEDED(hr); Index++)
    {
        D3DLIGHT9 Light;
        BOOL Enabled = FALSE;

        if (FAILED(GetLight(Index, &Light)))
            continue;
        GetLightEnable(Index, &Enabled);
        Enabled = Enabled != FALSE;
        hr = Block->Add(D9BlockOp::Light, Index, 0, &Light, sizeof(Light));
        if (SUCCEEDED(hr))
            hr = Block->Add(D9BlockOp::LightEnable, Index, 0, &Enabled, sizeof(Enabled));
    }

    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::VertexFloats, reinterpret_cast<BYTE*>(VertexFloats),
                            Limits.VertexFloats, sizeof(*VertexFloats));
    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::VertexInts, reinterpret_cast<BYTE*>(VertexInts), Limits.VertexInts,
                            sizeof(*VertexInts));
    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::VertexBools, reinterpret_cast<BYTE*>(VertexBools), Limits.VertexBools,
                            sizeof(*VertexBools));
    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::PixelFloats, reinterpret_cast<BYTE*>(PixelFloats), Limits.PixelFloats,
                            sizeof(*PixelFloats));
    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::PixelInts, reinterpret_cast<BYTE*>(PixelInts), Limits.PixelInts,
                            sizeof(*PixelInts));
    if (SUCCEEDED(hr))
        hr = D9AddConstants(Block, D9BlockOp::PixelBools, reinterpret_cast<BYTE*>(PixelBools), Limits.PixelBools,
                            sizeof(*PixelBools));
    return hr;
}

STDMETHODIMP
D9Device::CreateStateBlock(
    _In_ D3DSTATEBLOCKTYPE Type,
    _Out_ IDirect3DStateBlock9** SB)
{
    D9_DEVICE_LOCK();
    D9StateBlock* Block;
    HRESULT hr;

    if (SB == nullptr || Recording != nullptr)
        return D3DERR_INVALIDCALL;

    Block = new (std::nothrow) D9StateBlock(this);
    if (Block == nullptr)
        return E_OUTOFMEMORY;
    hr = FillBlock(Block, Type);
    if (FAILED(hr))
    {
        delete Block;
        return hr;
    }
    Block->AddRef();
    *SB = Block;
    return S_OK;
}

STDMETHODIMP
D9Device::BeginStateBlock()
{
    D9_DEVICE_LOCK();

    if (Recording != nullptr)
        return D3DERR_INVALIDCALL;
    Recording = new (std::nothrow) D9StateBlock(this);
    if (Recording == nullptr)
        return E_OUTOFMEMORY;
    return S_OK;
}

STDMETHODIMP
D9Device::EndStateBlock(
    _In_ IDirect3DStateBlock9** SB)
{
    D9_DEVICE_LOCK();

    if (SB == nullptr || Recording == nullptr)
        return D3DERR_INVALIDCALL;
    Recording->AddRef();
    *SB = Recording;
    Recording = nullptr;
    return S_OK;
}
