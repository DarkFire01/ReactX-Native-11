/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Clears of render target, depth stencil and unordered access views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "device.hpp"

/*
 * Writes one element value over a view, or over the given rectangles of it. Mask selects the
 * bits that change, so depth only and stencil only clears keep the other part.
 */
static
VOID
RxWarpFillView(
    _In_ RxWarpView *View,
    _In_reads_bytes_(16) const BYTE *Element,
    _In_reads_bytes_(16) const BYTE *Mask,
    _In_reads_opt_(RectCount) const D3D10_DDI_RECT *Rects,
    _In_ UINT RectCount)
{
    RxWarpResource *Resource = View->Resource;
    const RX_WARP_FORMAT_INFO *Info = RxWarpFormatInfo(View->Format);
    UINT Bytes;
    BOOLEAN Partial = FALSE;

    if (Resource == nullptr)
        return;

    for (UINT Index = 0; Index < 16; Index++)
        Partial |= (Mask[Index] != 0xFF);

    /* Raw buffer views are DWORDs; structured ones use the resource's stride */
    if (Info == nullptr || Info->Kind == RxKindBuffer)
        Bytes = Resource->ByteStride ? Resource->ByteStride : 4;
    else
        Bytes = Info->Bytes;
    if (View->Format == DXGI_FORMAT_R32_TYPELESS)
        Bytes = 4;

    if (Resource->Dimension == D3D10DDIRESOURCE_BUFFER)
    {
        BYTE *Data = Resource->Data(0) + (SIZE_T)View->FirstElement * Bytes;

        for (UINT Item = 0; Item < View->ElementCount; Item++)
        {
            for (UINT Byte = 0; Byte < Bytes; Byte++)
            {
                BYTE *Target = Data + (SIZE_T)Item * Bytes + Byte;

                *Target = (BYTE)((*Target & ~Mask[Byte % 16]) | (Element[Byte % 16] & Mask[Byte % 16]));
            }
        }
        return;
    }

    for (UINT Slice = 0; Slice < View->SliceCount; Slice++)
    {
        UINT Index = (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? View->MipSlice :
                     (View->FirstSlice + Slice) * Resource->MipLevels + View->MipSlice;
        RX_WARP_SUBRESOURCE *Sub = Resource->Subresource(Index);
        UINT Depth = (Resource->Dimension == D3D10DDIRESOURCE_TEXTURE3D) ? View->FirstSlice + Slice : 0;
        D3D10_DDI_RECT Whole;
        const D3D10_DDI_RECT *List;
        UINT Count;

        if (Sub == nullptr || Depth >= Sub->Depth)
            continue;

        Whole = { 0, 0, (LONG)Sub->Width, (LONG)Sub->Height };
        List = (Rects != nullptr && RectCount != 0) ? Rects : &Whole;
        Count = (Rects != nullptr && RectCount != 0) ? RectCount : 1;

        for (UINT Rect = 0; Rect < Count; Rect++)
        {
            LONG Left = (List[Rect].left > 0) ? List[Rect].left : 0;
            LONG Top = (List[Rect].top > 0) ? List[Rect].top : 0;
            LONG Right = (List[Rect].right < (LONG)Sub->Width) ? List[Rect].right : (LONG)Sub->Width;
            LONG Bottom = (List[Rect].bottom < (LONG)Sub->Height) ? List[Rect].bottom : (LONG)Sub->Height;

            for (LONG Y = Top; Y < Bottom; Y++)
            {
                BYTE *Row = Resource->Data(Index) + (SIZE_T)Depth * Sub->DepthPitch + (SIZE_T)Y * Sub->RowPitch;

                for (LONG X = Left; X < Right; X++)
                {
                    for (UINT Sample = 0; Sample < Resource->SampleCount; Sample++)
                    {
                        BYTE *Target = Row + ((SIZE_T)X * Resource->SampleCount + Sample) * Bytes;

                        if (!Partial)
                        {
                            memcpy(Target, Element, Bytes);
                            continue;
                        }
                        for (UINT Byte = 0; Byte < Bytes; Byte++)
                            Target[Byte] = (BYTE)((Target[Byte] & ~Mask[Byte]) | (Element[Byte] & Mask[Byte]));
                    }
                }
            }
        }
    }
}

static const BYTE RxWarpAllBits[16] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

VOID
APIENTRY
RxWarpDdi::ClearRenderTargetView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HRENDERTARGETVIEW View,
    _In_ FLOAT Color[4])
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpObject<RxWarpView>(View);
    BYTE Element[16];

    UNREFERENCED_PARAMETER(Device);
    if (Object != nullptr && RxWarpEncodeColor(Object->Format, Color, Element))
        RxWarpFillView(Object, Element, RxWarpAllBits, nullptr, 0);
}

VOID
APIENTRY
RxWarpDdi::ClearDepthStencilView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D10DDI_HDEPTHSTENCILVIEW View,
    _In_ UINT Flags,
    _In_ FLOAT Depth,
    _In_ UINT8 Stencil)
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpObject<RxWarpView>(View);
    const RX_WARP_FORMAT_INFO *Info;
    const FLOAT Values[4] = { Depth, (FLOAT)Stencil, 0.0f, 0.0f };
    BYTE Element[16];
    BYTE Mask[16] = {};
    UINT Offset = 0;

    UNREFERENCED_PARAMETER(Device);
    if (Object == nullptr || (Info = RxWarpFormatInfo(Object->Format)) == nullptr)
        return;

    /* Build the value with depth in channel 4 and stencil in channel 5, then mask per flag */
    RxWarpEncodeColor(Object->Format, Values, Element);
    for (UINT Index = 0; Index < Info->ChannelCount; Index++)
    {
        const RX_WARP_CHANNEL &Channel = Info->Channels[Index];
        BOOLEAN Wanted = (Channel.Source == 4 && (Flags & D3D10_DDI_CLEAR_DEPTH)) ||
                         (Channel.Source == 5 && (Flags & D3D10_DDI_CLEAR_STENCIL));

        for (UINT Bit = 0; Bit < Channel.Bits && Wanted; Bit++)
            Mask[(Offset + Bit) / 8] |= (BYTE)(1u << ((Offset + Bit) % 8));
        Offset += Channel.Bits;
    }
    RxWarpFillView(Object, Element, Mask, nullptr, 0);
}

VOID
APIENTRY
RxWarpDdi::ClearUnorderedAccessViewUint(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW View,
    _In_ const UINT Values[4])
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpObject<RxWarpView>(View);
    BYTE Element[16];

    UNREFERENCED_PARAMETER(Device);
    if (Object == nullptr)
        return;

    /* Raw and structured buffers repeat the first value in every DWORD */
    if (Object->Format == DXGI_FORMAT_UNKNOWN || Object->Format == DXGI_FORMAT_R32_TYPELESS)
    {
        for (UINT Index = 0; Index < 4; Index++)
            memcpy(Element + Index * 4, &Values[0], 4);
    }
    else if (!RxWarpEncodeUint(Object->Format, Values, Element))
    {
        return;
    }
    RxWarpFillView(Object, Element, RxWarpAllBits, nullptr, 0);
}

VOID
APIENTRY
RxWarpDdi::ClearUnorderedAccessViewFloat(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D11DDI_HUNORDEREDACCESSVIEW View,
    _In_ const FLOAT Values[4])
{
    RxWarpLock Lock;
    RxWarpView *Object = RxWarpObject<RxWarpView>(View);
    BYTE Element[16];

    UNREFERENCED_PARAMETER(Device);
    if (Object != nullptr && RxWarpEncodeColor(Object->Format, Values, Element))
        RxWarpFillView(Object, Element, RxWarpAllBits, nullptr, 0);
}

VOID
APIENTRY
RxWarpDdi::ClearView(
    _In_ D3D10DDI_HDEVICE Device,
    _In_ D3D11DDI_HANDLETYPE Type,
    _In_ VOID *View,
    _In_ const FLOAT Color[4],
    _In_reads_opt_(RectCount) const D3D10_DDI_RECT *Rects,
    _In_ UINT RectCount)
{
    RxWarpLock Lock;
    RxWarpView *Object;
    BYTE Element[16];

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Type);

    /* The handle is the view's driver private pointer whatever its type */
    Object = *static_cast<RxWarpView **>(View);
    if (Object != nullptr && RxWarpEncodeColor(Object->Format, Color, Element))
        RxWarpFillView(Object, Element, RxWarpAllBits, Rects, RectCount);
}
