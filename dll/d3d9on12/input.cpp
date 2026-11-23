/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Vertex declarations, vertex streams and the index buffer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "scprivate.hpp"

/* Inputs one declaration can make */
static constexpr UINT NineDeclInputLimit = 64;

/* D3DDECLUSAGE values the declaration looks for */
static constexpr BYTE NineUsagePosition = 0;
static constexpr BYTE NineUsagePointSize = 4;
static constexpr BYTE NineUsagePositionT = 9;

/* The handle d3d9.dll passes for a stream or index buffer it cleared */
static const HANDLE NineClearedBuffer = reinterpret_cast<HANDLE>(1);

/* How the converter widens a D3DDECLTYPE when it reads it */
static
UINT
NineElementConversion(
    _In_ BYTE Type)
{
    switch (Type)
    {
        case D3DDECLTYPE_UDEC3:
            return 2;
        case D3DDECLTYPE_DEC3N:
            return 3;
        case D3DDECLTYPE_UBYTE4:
        case D3DDECLTYPE_SHORT2:
        case D3DDECLTYPE_SHORT4:
            return 1;
        default:
            return 0;
    }
}

HRESULT
APIENTRY
NineCreateVertexShaderDecl(
    _In_ HANDLE hDevice,
    _Inout_ D3DDDIARG_CREATEVERTEXSHADERDECL* Args,
    _In_reads_(Args->NumVertexElements) const D3DDDIVERTEXELEMENT* Elements)
{
    std::unique_ptr<NineVertexDecl> Decl;

    if (hDevice == nullptr || Args == nullptr)
        return E_INVALIDARG;
    if (Args->NumVertexElements != 0 && Elements == nullptr)
        return E_INVALIDARG;

    Decl = std::make_unique<NineVertexDecl>();
    Decl->Inputs.Limit = NineDeclInputLimit;
    Decl->Elements.resize(Args->NumVertexElements);
    for (UINT Index = 0; Index < Args->NumVertexElements; Index += 1)
    {
        NineScElement* Element = &Decl->Elements[Index];
        BOOL ScreenSpace = FALSE;

        static_assert(sizeof(*Element) == sizeof(Elements[Index]), "elements are copied as the runtime lays them out");
        memcpy(Element, &Elements[Index], sizeof(*Element));

        /* The first POSITIONT is read as a position the vertex shader leaves in screen space */
        if (Element->Usage == NineUsagePositionT && Element->UsageIndex == 0)
        {
            Element->Usage = NineUsagePosition;
            ScreenSpace = TRUE;
            Decl->Pretransformed = TRUE;
        }
        if (Element->Usage == NineUsagePointSize)
            Decl->PointSize = TRUE;

        ScAddInput(&Decl->Inputs, Element->Usage, Element->UsageIndex, Index, ScreenSpace,
                   NineElementConversion(Element->Type));
        Decl->StreamMask |= 1u << (Element->Stream & 31);
    }

    Args->ShaderHandle = Decl.release();
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetVertexShaderDecl(
    _In_ HANDLE hDevice,
    _In_ HANDLE hDecl)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineVertexDecl* Decl = static_cast<NineVertexDecl*>(hDecl);
    NineDeviceState* State;

    if (Device == nullptr)
        return E_INVALIDARG;

    State = &Device->State;
    if (State->VertexDecl != Decl)
    {
        State->VertexDecl = Decl;
        State->Dirty |= NineDirtyInputLayout;
        if (Decl != nullptr && State->Pretransformed != Decl->Pretransformed)
        {
            State->Pretransformed = Decl->Pretransformed;
            State->Dirty |= NineDirtyPretransformed;
        }
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineDeleteVertexShaderDecl(
    _In_ HANDLE hDevice,
    _In_ HANDLE hDecl)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineVertexDecl* Decl = static_cast<NineVertexDecl*>(hDecl);

    if (Device == nullptr || Decl == nullptr)
        return E_INVALIDARG;

    if (Device->State.VertexDecl == Decl)
        Device->State.VertexDecl = nullptr;
    delete Decl;
    return NineDxgiResult(S_OK);
}

static
VOID
NineBindStream(
    _Inout_ NineDevice* Device,
    _In_ UINT Stream,
    _In_opt_ NineResource* Buffer,
    _In_ UINT Offset,
    _In_ UINT Stride)
{
    NineDeviceState* State = &Device->State;
    NineStreamSlot* Slot = &State->Streams[Stream];

    if (Slot->Buffer == Buffer && Slot->Stride == Stride && Slot->Offset == Offset)
        return;

    if (Slot->Buffer != nullptr)
    {
        std::vector<UINT>& Bound = Slot->Buffer->StreamSlots;

        for (auto Entry = Bound.begin(); Entry != Bound.end(); ++Entry)
        {
            if (*Entry == Stream)
            {
                Bound.erase(Entry);
                break;
            }
        }
    }

    /* Streams in use end at the highest one with a buffer behind it */
    if (Buffer != nullptr)
    {
        Buffer->StreamSlots.push_back(Stream);
        if (State->StreamsUsed < Stream + 1)
            State->StreamsUsed = Stream + 1;
    }
    else if (State->StreamsUsed == Stream + 1)
    {
        do
        {
            State->StreamsUsed -= 1;
        } while (State->StreamsUsed > 0 && State->Streams[State->StreamsUsed - 1].Buffer == nullptr);
    }

    Slot->Buffer = Buffer;
    Slot->Offset = Offset;
    Slot->Stride = Stride;
    State->Dirty |= NineDirtyVertexBuffers;
}

static
VOID
NineBindIndices(
    _Inout_ NineDevice* Device,
    _In_opt_ NineResource* Buffer,
    _In_ UINT Stride)
{
    NineDeviceState* State = &Device->State;

    if (State->IndexBuffer == Buffer && State->IndexStride == Stride)
        return;

    if (State->IndexBuffer != nullptr)
        State->IndexBuffer->BoundAsIndices = FALSE;
    if (Buffer != nullptr)
        Buffer->BoundAsIndices = TRUE;
    State->IndexBuffer = Buffer;
    State->IndexStride = Stride;
    State->Dirty |= NineDirtyIndexBuffer;
}

HRESULT
APIENTRY
NineSetStreamSource(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCE* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Buffer;

    if (Device == nullptr || Args == nullptr || Args->Stream >= NineScStreamCount)
        return E_INVALIDARG;

    Buffer = Args->hVertexBuffer == NineClearedBuffer ? nullptr : static_cast<NineResource*>(Args->hVertexBuffer);
    NineBindStream(Device, Args->Stream, Buffer, Args->Offset, Args->Stride);
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetStreamSourceFreq(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETSTREAMSOURCEFREQ* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineDeviceState* State;

    if (Device == nullptr || Args == nullptr || Args->Stream >= NineScStreamCount)
        return E_INVALIDARG;

    State = &Device->State;
    if (State->Frequencies[Args->Stream] != Args->Divider)
        State->Dirty |= NineDirtyInputLayout;
    State->Frequencies[Args->Stream] = Args->Divider;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetIndices(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETINDICES* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Buffer;

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    Buffer = Args->hIndexBuffer == NineClearedBuffer ? nullptr : static_cast<NineResource*>(Args->hIndexBuffer);
    NineBindIndices(Device, Buffer, Args->Stride);
    return NineDxgiResult(S_OK);
}

VOID
NineUnbindInputs(
    _Inout_ NineResource* Resource)
{
    while (!Resource->StreamSlots.empty())
        NineBindStream(Resource->Device, Resource->StreamSlots.front(), nullptr, 0, 0);
    if (Resource->BoundAsIndices)
        NineBindIndices(Resource->Device, nullptr, 0);
}
