/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Mip generation for shader resource views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

static
VOID
RxWarpReadFloat(
    _In_ DXGI_FORMAT Format,
    _In_ const BYTE *Texel,
    _In_ UINT Bytes,
    _Out_writes_(4) FLOAT *Color)
{
    BYTE Element[16] = {};
    UINT Raw[4];

    memcpy(Element, Texel, (Bytes <= 16) ? Bytes : 16);
    RxWarpDecodeTexel(Format, Element, Raw);
    memcpy(Color, Raw, sizeof(Raw));
}

/*
 * Picks the first of the two source texels for a destination texel along one axis. Even sizes
 * halve exactly; odd ones step a scaled coordinate so the last source texel is reached.
 */
static
UINT
RxWarpMipSource(
    _In_ UINT Index,
    _In_ UINT SourceSize,
    _In_ UINT TargetSize)
{
    FLOAT Step;
    FLOAT Position;
    UINT First;

    if ((SourceSize & 1) == 0)
        return Index * 2;

    Step = 1.0f / (FLOAT)TargetSize;
    if (SourceSize != 1)
        Step = ((FLOAT)(SourceSize - 1) / (FLOAT)SourceSize) * Step;
    Position = (FLOAT)Index * Step + Step * 0.25f;
    First = (UINT)floorf(Position * (FLOAT)SourceSize);
    return (First < SourceSize) ? First : SourceSize - 1;
}

/* Mip levels of half float formats round toward zero, so drop what a half cannot hold before encoding */
static
FLOAT
RxWarpTowardZeroHalf(
    _In_ FLOAT Value)
{
    UINT Bits;

    if (fabsf(Value) < 6.103515625e-05f)
        return truncf(Value * 16777216.0f) * (1.0f / 16777216.0f);
    memcpy(&Bits, &Value, sizeof(Bits));
    if (((Bits >> 23) & 0xFF) != 0xFF)
        Bits &= ~0x1FFFu;
    memcpy(&Value, &Bits, sizeof(Value));
    return Value;
}

/* One level from the one above it: the average of a 2x2 (or 2x2x2) box */
static
VOID
RxWarpMakeLevel(
    _In_ DXGI_FORMAT Format,
    _In_ UINT Bytes,
    _In_ BYTE *Memory,
    _In_ const RX_WARP_SUBRESOURCE *Source,
    _In_ const RX_WARP_SUBRESOURCE *Target)
{
    BOOLEAN HalfFloat = (Format == DXGI_FORMAT_R16G16B16A16_FLOAT || Format == DXGI_FORMAT_R16G16_FLOAT || Format == DXGI_FORMAT_R16_FLOAT);

    for (UINT Z = 0; Z < Target->Depth; Z++)
    {
        UINT Sz = (Source->Depth > 1) ? RxWarpMipSource(Z, Source->Depth, Target->Depth) : 0;
        UINT Dz = Z;

        for (UINT Y = 0; Y < Target->Height; Y++)
        {
            UINT Sy = RxWarpMipSource(Y, Source->Height, Target->Height);

            for (UINT X = 0; X < Target->Width; X++)
            {
                UINT Sx = RxWarpMipSource(X, Source->Width, Target->Width);
                FLOAT Sum[4] = {};
                UINT Taps = 0;
                BYTE Element[16];

                for (UINT Oz = 0; Oz < ((Source->Depth > 1) ? 2u : 1u); Oz++)
                {
                    for (UINT Oy = 0; Oy < ((Source->Height > 1) ? 2u : 1u); Oy++)
                    {
                        for (UINT Ox = 0; Ox < ((Source->Width > 1) ? 2u : 1u); Ox++)
                        {
                            UINT Tx = (Sx + Ox < Source->Width) ? Sx + Ox : Source->Width - 1;
                            UINT Ty = (Sy + Oy < Source->Height) ? Sy + Oy : Source->Height - 1;
                            UINT Tz = (Sz + Oz < Source->Depth) ? Sz + Oz : Source->Depth - 1;
                            FLOAT Color[4];

                            RxWarpReadFloat(Format, Memory + Source->Offset + (SIZE_T)Tz * Source->DepthPitch +
                                            (SIZE_T)Ty * Source->RowPitch + (SIZE_T)Tx * Bytes, Bytes, Color);
                            for (UINT Component = 0; Component < 4; Component++)
                                Sum[Component] += Color[Component];
                            Taps++;
                        }
                    }
                }

                for (UINT Component = 0; Component < 4; Component++)
                {
                    Sum[Component] = Sum[Component] * (1.0f / (FLOAT)Taps);
                    if (HalfFloat)
                        Sum[Component] = RxWarpTowardZeroHalf(Sum[Component]);
                }
                if (RxWarpEncodeColor(Format, Sum, Element))
                {
                    memcpy(Memory + Target->Offset + (SIZE_T)Dz * Target->DepthPitch + (SIZE_T)Y * Target->RowPitch +
                           (SIZE_T)X * Bytes, Element, Bytes);
                }
            }
        }
    }
}

VOID
APIENTRY
RxWarpDdi::GenMips(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HSHADERRESOURCEVIEW View)
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpObject<RxWarpView>(View);
    RxWarpResource *Resource;
    const RX_WARP_FORMAT_INFO *Info;
    UINT Levels;
    UINT Slices;

    if (RxWarpObject<RxWarpDevice>(Device)->Predicated())
        return;
    if (Object == nullptr || (Resource = Object->Resource) == nullptr || Resource->Memory == nullptr)
        return;
    Info = RxWarpFormatInfo(Object->Format);
    if (Info == nullptr || Info->Kind != RxKindPlain || Object->MipSlice >= Resource->MipLevels)
        return;

    Levels = Resource->MipLevels - Object->MipSlice;
    if (Object->MipCount != 0 && Object->MipCount < Levels)
        Levels = Object->MipCount;
    Slices = (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? 1 : (Object->SliceCount ? Object->SliceCount : 1);

    for (UINT Slice = 0; Slice < Slices; Slice++)
    {
        UINT ArraySlice = (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? 0 : Object->FirstSlice + Slice;

        for (UINT Level = 1; Level < Levels; Level++)
        {
            UINT Mip = Object->MipSlice + Level;
            RX_WARP_SUBRESOURCE *Source = Resource->Subresource(ArraySlice * Resource->MipLevels + Mip - 1);
            RX_WARP_SUBRESOURCE *Target = Resource->Subresource(ArraySlice * Resource->MipLevels + Mip);

            if (Source == nullptr || Target == nullptr)
                break;
            RxWarpMakeLevel(Object->Format, Info->Bytes, Resource->Memory, Source, Target);
        }
    }
}
