/*
 * PROJECT:     ReactX Direct3D 9 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Render target and depth bindings, clears and color fills
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"

#include <algorithm>

/* Clear flag bits of D3DDDIARG_CLEAR */
static constexpr UINT NineClearTarget = 0x1;
static constexpr UINT NineClearDepth = 0x2;
static constexpr UINT NineClearStencil = 0x4;

static
NineResource*
NineHandleResource(
    _In_opt_ HANDLE hResource)
{
    if (hResource == reinterpret_cast<HANDLE>(1))
        return nullptr;
    return static_cast<NineResource*>(hResource);
}

/* One channel of a packed color; divided in double precision like the runtime's own conversion */
static
FLOAT
NineChannel(
    _In_ D3DCOLOR Color,
    _In_ UINT Shift)
{
    return static_cast<FLOAT>(static_cast<double>(static_cast<FLOAT>((Color >> Shift) & 0xFF)) / 255.0);
}

static
VOID
NineUnpackColor(
    _In_ const NineResource* Resource,
    _In_ D3DCOLOR Color,
    _Out_writes_(4) FLOAT* Rgba)
{
    Rgba[0] = NineChannel(Color, 16);
    Rgba[1] = NineChannel(Color, 8);
    Rgba[2] = NineChannel(Color, 0);
    Rgba[3] = NineChannel(Color, 24);

    /* The 10 bit format stores red where the others keep blue */
    if (Resource->Format == D3DDDIFMT_A2R10G10B10)
    {
        Rgba[0] = NineChannel(Color, 0);
        Rgba[2] = NineChannel(Color, 16);
    }
}

/* Clears one subresource of a color surface, through its render target view when it has one */
static
VOID
NineClearSurface(
    _Inout_ NineResource* Resource,
    _In_ UINT Sub,
    _In_reads_opt_(NumRects) const RECT* Rects,
    _In_ UINT NumRects,
    _In_ D3DCOLOR Color)
{
    NineDevice* Device = Resource->Device;
    const D3D12_RECT* Areas = reinterpret_cast<const D3D12_RECT*>(Rects);
    FLOAT Rgba[4];

    NineUnpackColor(Resource, Color, Rgba);
    if (Resource->Rtvs.empty())
    {
        Device->Context->ClearResourceWithNoRenderTarget(Resource->Underlying.get(), Rgba, NumRects, Areas, Sub, Sub,
                                                         Resource->DxgiFormat);
        return;
    }

    TL::TRTV* View = nullptr;

    if ((Resource->Desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0)
    {
        const BOOLEAN SrgbWrite = Device->State.RenderStates[D3DRS_SRGBWRITEENABLE] != 0;
        const DXGI_FORMAT* Cast = CD3D11FormatHelper::GetFormatCastSet(Resource->Desc.Format);
        BOOLEAN HasSrgb = FALSE;

        for (; *Cast != DXGI_FORMAT_UNKNOWN && !HasSrgb; Cast += 1)
            HasSrgb = CD3D11FormatHelper::IsSRGBFormat(*Cast);

        View = SrgbWrite && HasSrgb ? Resource->SrgbRtvs[Sub].get() : Resource->Rtvs[Sub].get();
    }
    if (View == nullptr)
    {
        NineTrace(L"clear of a surface without a render target view");
        return;
    }
    Device->Context->ClearRenderTargetView(View, Rgba, NumRects, Areas);
}

HRESULT
APIENTRY
NineSetRenderTarget(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETRENDERTARGET* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineDeviceState* State;
    NineResource* Resource;
    NineTargetSlot* Slot;
    UINT Sub = 0;

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    Resource = NineHandleResource(Args->hRenderTarget);
    if (Resource != nullptr)
        Sub = Resource->PlaneCount <= 1 ? NineSubresourceOf(Resource, Args->SubResourceIndex, 0, FALSE) : ~0U;

    State = &Device->State;
    Slot = &State->Targets[Args->RenderTargetIndex];
    if (Resource != nullptr)
        State->TargetMask |= 1U << Args->RenderTargetIndex;
    else
        State->TargetMask &= ~(1U << Args->RenderTargetIndex);

    if (Slot->Resource != Resource)
    {
        if (Slot->Resource != nullptr)
        {
            std::vector<UINT>& Bound = Slot->Resource->TargetSlots;
            auto Found = std::find(Bound.begin(), Bound.end(), Args->RenderTargetIndex);

            if (Found != Bound.end())
                Bound.erase(Found);
        }
        if (Resource != nullptr)
            Resource->TargetSlots.push_back(Args->RenderTargetIndex);
    }

    Slot->Resource = Resource;
    Slot->Sub = Sub;
    State->TargetsUsed = 0;
    for (UINT Mask = State->TargetMask; Mask != 0; Mask >>= 1)
        State->TargetsUsed += 1;
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineSetDepthStencil(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_SETDEPTHSTENCIL* Args)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);
    NineResource* Resource;

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    Resource = NineHandleResource(Args->hZBuffer);
    if (Device->State.DepthStencil != Resource)
    {
        if (Device->State.DepthStencil != nullptr)
            Device->State.DepthStencil->BoundAsDepth = FALSE;
        if (Resource != nullptr)
            Resource->BoundAsDepth = TRUE;
        Device->State.DepthStencil = Resource;
    }
    return NineDxgiResult(S_OK);
}

VOID
NineUnbindOutputs(
    _Inout_ NineResource* Resource)
{
    NineDeviceState* State = &Resource->Device->State;

    while (!Resource->TargetSlots.empty())
    {
        D3DDDIARG_SETRENDERTARGET Unbind = {};

        Unbind.RenderTargetIndex = Resource->TargetSlots.front();
        NineSetRenderTarget(Resource->Device, &Unbind);
    }
    if (Resource->BoundAsDepth && State->DepthStencil == Resource)
    {
        Resource->BoundAsDepth = FALSE;
        State->DepthStencil = nullptr;
    }
}

HRESULT
APIENTRY
NineClear(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_CLEAR* Args,
    _In_ UINT NumRects,
    _In_reads_opt_(NumRects) const RECT* Rects)
{
    NineDevice* Device = static_cast<NineDevice*>(hDevice);

    if (Device == nullptr || Args == nullptr)
        return E_INVALIDARG;

    try
    {
        if ((Args->Flags & NineClearTarget) != 0)
        {
            for (UINT Index = 0; Index < NineTargetCount; Index += 1)
            {
                NineTargetSlot* Slot = &Device->State.Targets[Index];

                if (Slot->Resource == nullptr)
                    continue;
                if (Slot->Resource->Device != Device)
                    return E_INVALIDARG;
                NineClearSurface(Slot->Resource, Slot->Sub, Rects, NumRects, Args->FillColor);
            }
        }

        if ((Args->Flags & (NineClearDepth | NineClearStencil)) != 0)
        {
            NineResource* Depth = Device->State.DepthStencil;
            UINT Planes = 0;

            if (Depth == nullptr || Depth->Device != Device)
                return E_INVALIDARG;

            if ((Args->Flags & NineClearDepth) != 0)
                Planes |= D3D12_CLEAR_FLAG_DEPTH;
            if ((Args->Flags & NineClearStencil) != 0)
                Planes |= D3D12_CLEAR_FLAG_STENCIL;
            Device->Context->ClearDepthStencilView(Depth->Dsvs[0].get(), Planes, Args->FillDepth,
                                                   static_cast<UINT8>(Args->FillStencil), NumRects,
                                                   reinterpret_cast<const D3D12_RECT*>(Rects));
        }
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}

HRESULT
APIENTRY
NineColorFill(
    _In_ HANDLE hDevice,
    _In_ const D3DDDIARG_COLORFILL* Args)
{
    NineResource* Resource;

    if (hDevice == NULL || Args == nullptr)
        return E_INVALIDARG;

    Resource = NineHandleResource(Args->hResource);
    if (Resource == nullptr)
        return S_OK;

    try
    {
        if (Resource->PlaneCount == 1)
            NineClearSurface(Resource, NineSubresourceOf(Resource, Args->SubResourceIndex, 0, FALSE), &Args->DstRect, 1,
                             Args->Color);
        else
            NINE_TRACE_ONCE(L"color fills of planar video surfaces are not done yet");
    }
    catch (_com_error& Error)
    {
        return NineDxgiResult(Error.Error());
    }
    catch (std::bad_alloc&)
    {
        return E_OUTOFMEMORY;
    }
    return NineDxgiResult(S_OK);
}
