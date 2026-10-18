/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Converting Direct3D 10 descriptions to and from their Direct3D 11 forms
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"

/* Misc flags: the low three agree; keyed mutex and GDI compatible moved in Direct3D 11 */
inline
UINT
RxToD3D11Misc(
    _In_ UINT Misc)
{
    UINT Result = Misc & (D3D10_RESOURCE_MISC_GENERATE_MIPS | D3D10_RESOURCE_MISC_SHARED |
                          D3D10_RESOURCE_MISC_TEXTURECUBE);

    if (Misc & D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        Result |= D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (Misc & D3D10_RESOURCE_MISC_GDI_COMPATIBLE)
        Result |= D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
    return Result;
}

inline
UINT
RxToD3D10Misc(
    _In_ UINT Misc)
{
    UINT Result = Misc & (D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_SHARED |
                          D3D11_RESOURCE_MISC_TEXTURECUBE);

    if (Misc & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX)
        Result |= D3D10_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    if (Misc & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)
        Result |= D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
    return Result;
}

/* Resources */

inline
D3D11_BUFFER_DESC
RxToD3D11Desc(
    _In_ const D3D10_BUFFER_DESC& Desc)
{
    return { Desc.ByteWidth, (D3D11_USAGE)Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags,
             RxToD3D11Misc(Desc.MiscFlags), 0 };
}

inline
D3D10_BUFFER_DESC
RxToD3D10Desc(
    _In_ const D3D11_BUFFER_DESC& Desc)
{
    return { Desc.ByteWidth, (D3D10_USAGE)Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags,
             RxToD3D10Misc(Desc.MiscFlags) };
}

inline
D3D11_TEXTURE1D_DESC
RxToD3D11Desc(
    _In_ const D3D10_TEXTURE1D_DESC& Desc)
{
    return { Desc.Width, Desc.MipLevels, Desc.ArraySize, Desc.Format, (D3D11_USAGE)Desc.Usage, Desc.BindFlags,
             Desc.CPUAccessFlags, RxToD3D11Misc(Desc.MiscFlags) };
}

inline
D3D10_TEXTURE1D_DESC
RxToD3D10Desc(
    _In_ const D3D11_TEXTURE1D_DESC& Desc)
{
    return { Desc.Width, Desc.MipLevels, Desc.ArraySize, Desc.Format, (D3D10_USAGE)Desc.Usage, Desc.BindFlags,
             Desc.CPUAccessFlags, RxToD3D10Misc(Desc.MiscFlags) };
}

inline
D3D11_TEXTURE2D_DESC
RxToD3D11Desc(
    _In_ const D3D10_TEXTURE2D_DESC& Desc)
{
    return { Desc.Width, Desc.Height, Desc.MipLevels, Desc.ArraySize, Desc.Format, Desc.SampleDesc,
             (D3D11_USAGE)Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, RxToD3D11Misc(Desc.MiscFlags) };
}

inline
D3D10_TEXTURE2D_DESC
RxToD3D10Desc(
    _In_ const D3D11_TEXTURE2D_DESC& Desc)
{
    return { Desc.Width, Desc.Height, Desc.MipLevels, Desc.ArraySize, Desc.Format, Desc.SampleDesc,
             (D3D10_USAGE)Desc.Usage, Desc.BindFlags, Desc.CPUAccessFlags, RxToD3D10Misc(Desc.MiscFlags) };
}

inline
D3D11_TEXTURE3D_DESC
RxToD3D11Desc(
    _In_ const D3D10_TEXTURE3D_DESC& Desc)
{
    return { Desc.Width, Desc.Height, Desc.Depth, Desc.MipLevels, Desc.Format, (D3D11_USAGE)Desc.Usage,
             Desc.BindFlags, Desc.CPUAccessFlags, RxToD3D11Misc(Desc.MiscFlags) };
}

inline
D3D10_TEXTURE3D_DESC
RxToD3D10Desc(
    _In_ const D3D11_TEXTURE3D_DESC& Desc)
{
    return { Desc.Width, Desc.Height, Desc.Depth, Desc.MipLevels, Desc.Format, (D3D10_USAGE)Desc.Usage,
             Desc.BindFlags, Desc.CPUAccessFlags, RxToD3D10Misc(Desc.MiscFlags) };
}

/* Views: shader resource and render target views keep the same layout, depth views gained Flags */

inline
D3D11_SHADER_RESOURCE_VIEW_DESC
RxToD3D11Desc(
    _In_ const D3D10_SHADER_RESOURCE_VIEW_DESC1& Desc)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC Result = {};

    static_assert(sizeof(D3D10_SHADER_RESOURCE_VIEW_DESC1) <= sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    memcpy(&Result, &Desc, sizeof(Desc));
    return Result;
}

inline
D3D10_SHADER_RESOURCE_VIEW_DESC1
RxToD3D10Desc(
    _In_ const D3D11_SHADER_RESOURCE_VIEW_DESC& Desc)
{
    D3D10_SHADER_RESOURCE_VIEW_DESC1 Result;

    memcpy(&Result, &Desc, sizeof(Result));
    return Result;
}

inline
D3D11_RENDER_TARGET_VIEW_DESC
RxToD3D11Desc(
    _In_ const D3D10_RENDER_TARGET_VIEW_DESC& Desc)
{
    D3D11_RENDER_TARGET_VIEW_DESC Result;

    static_assert(sizeof(D3D10_RENDER_TARGET_VIEW_DESC) == sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
    memcpy(&Result, &Desc, sizeof(Result));
    return Result;
}

inline
D3D10_RENDER_TARGET_VIEW_DESC
RxToD3D10Desc(
    _In_ const D3D11_RENDER_TARGET_VIEW_DESC& Desc)
{
    D3D10_RENDER_TARGET_VIEW_DESC Result;

    memcpy(&Result, &Desc, sizeof(Result));
    return Result;
}

inline
D3D11_DEPTH_STENCIL_VIEW_DESC
RxToD3D11Desc(
    _In_ const D3D10_DEPTH_STENCIL_VIEW_DESC& Desc)
{
    D3D11_DEPTH_STENCIL_VIEW_DESC Result = {};

    Result.Format = Desc.Format;
    Result.ViewDimension = (D3D11_DSV_DIMENSION)Desc.ViewDimension;
    static_assert(sizeof(Desc.Texture2DArray) <= sizeof(Result.Texture2DArray));
    memcpy(&Result.Texture2DArray, &Desc.Texture2DArray, sizeof(Desc.Texture2DArray));
    return Result;
}

inline
D3D10_DEPTH_STENCIL_VIEW_DESC
RxToD3D10Desc(
    _In_ const D3D11_DEPTH_STENCIL_VIEW_DESC& Desc)
{
    D3D10_DEPTH_STENCIL_VIEW_DESC Result = {};

    Result.Format = Desc.Format;
    Result.ViewDimension = (D3D10_DSV_DIMENSION)Desc.ViewDimension;
    memcpy(&Result.Texture2DArray, &Desc.Texture2DArray, sizeof(Result.Texture2DArray));
    return Result;
}

/* Blend: Direct3D 10 shares one blend function among targets that each switch it on or off */

inline
D3D11_BLEND_DESC
RxToD3D11Desc(
    _In_ const D3D10_BLEND_DESC& Desc)
{
    D3D11_BLEND_DESC Result = {};
    UINT Target;

    Result.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable;
    Result.IndependentBlendEnable = TRUE;
    for (Target = 0; Target < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        D3D11_RENDER_TARGET_BLEND_DESC& Out = Result.RenderTarget[Target];

        Out.BlendEnable = Desc.BlendEnable[Target];
        Out.SrcBlend = (D3D11_BLEND)Desc.SrcBlend;
        Out.DestBlend = (D3D11_BLEND)Desc.DestBlend;
        Out.BlendOp = (D3D11_BLEND_OP)Desc.BlendOp;
        Out.SrcBlendAlpha = (D3D11_BLEND)Desc.SrcBlendAlpha;
        Out.DestBlendAlpha = (D3D11_BLEND)Desc.DestBlendAlpha;
        Out.BlendOpAlpha = (D3D11_BLEND_OP)Desc.BlendOpAlpha;
        Out.RenderTargetWriteMask = Desc.RenderTargetWriteMask[Target];
    }

    return Result;
}

inline
D3D10_BLEND_DESC
RxToD3D10Desc(
    _In_ const D3D11_BLEND_DESC& Desc)
{
    const D3D11_RENDER_TARGET_BLEND_DESC* Shared = &Desc.RenderTarget[0];
    D3D10_BLEND_DESC Result = {};
    UINT Target;

    /* The shared function is whatever a blending target uses; targets that do not blend pinned theirs */
    for (Target = 0; Target < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        if (Desc.RenderTarget[Target].BlendEnable)
        {
            Shared = &Desc.RenderTarget[Target];
            break;
        }
    }

    const D3D11_RENDER_TARGET_BLEND_DESC& First = *Shared;

    Result.AlphaToCoverageEnable = Desc.AlphaToCoverageEnable;
    Result.SrcBlend = (D3D10_BLEND)First.SrcBlend;
    Result.DestBlend = (D3D10_BLEND)First.DestBlend;
    Result.BlendOp = (D3D10_BLEND_OP)First.BlendOp;
    Result.SrcBlendAlpha = (D3D10_BLEND)First.SrcBlendAlpha;
    Result.DestBlendAlpha = (D3D10_BLEND)First.DestBlendAlpha;
    Result.BlendOpAlpha = (D3D10_BLEND_OP)First.BlendOpAlpha;
    for (Target = 0; Target < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; Target++)
    {
        const D3D11_RENDER_TARGET_BLEND_DESC& Each = Desc.IndependentBlendEnable ? Desc.RenderTarget[Target]
                                                                                : Desc.RenderTarget[0];

        Result.BlendEnable[Target] = Each.BlendEnable;
        Result.RenderTargetWriteMask[Target] = Each.RenderTargetWriteMask;
    }

    return Result;
}

/* Direct3D 10 viewports are in whole pixels */

inline
D3D11_VIEWPORT
RxToD3D11Viewport(
    _In_ const D3D10_VIEWPORT& Viewport)
{
    return { (FLOAT)Viewport.TopLeftX, (FLOAT)Viewport.TopLeftY, (FLOAT)Viewport.Width, (FLOAT)Viewport.Height,
             Viewport.MinDepth, Viewport.MaxDepth };
}

inline
D3D10_VIEWPORT
RxToD3D10Viewport(
    _In_ const D3D11_VIEWPORT& Viewport)
{
    return { (INT)Viewport.TopLeftX, (INT)Viewport.TopLeftY, (UINT)Viewport.Width, (UINT)Viewport.Height,
             Viewport.MinDepth, Viewport.MaxDepth };
}

/* A Direct3D 11 pointer behind a Direct3D 10 one, without keeping a reference */
template <typename To, typename From>
inline
To*
RxTo11(
    _In_opt_ From* Object)
{
    To* Found = nullptr;

    if (Object != nullptr && SUCCEEDED(Object->QueryInterface(IID_PPV_ARGS(&Found))))
        Found->Release();
    return Found;
}

/* The Direct3D 10 face of a Direct3D 11 object, taking over the reference passed in */
template <typename To, typename From>
inline
To*
RxTo10(
    _In_opt_ From* Object)
{
    To* Found = nullptr;

    if (Object == nullptr)
        return nullptr;

    Object->QueryInterface(IID_PPV_ARGS(&Found));
    Object->Release();
    return Found;
}
