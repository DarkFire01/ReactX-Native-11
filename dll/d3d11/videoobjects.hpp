/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video decoder and decoder output view objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "resource.hpp"

#include <memory>

/* API buffer types run 0 to 8; the DDI numbers them one higher */
constexpr UINT RxMaxDecoderBufferTypes = 16;

class RxVideoDecoder final : public RxDeviceChild<ID3D11VideoDecoder>
{
public:
    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const D3D11_VIDEO_DECODER_DESC& Desc,
        _In_ const D3D11_VIDEO_DECODER_CONFIG& Config,
        _Outptr_ RxVideoDecoder** Decoder);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videodecoder.inl"

    D3D11_1DDI_HDECODE
    GetHandle() const
    {
        return Handle;
    }

    /* The per-type buffer behind GetDecoderBuffer; one map at a time, as the API requires */
    HRESULT
    MapBuffer(
        _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type,
        _Out_ UINT* Size,
        _Out_ VOID** Data);

    HRESULT
    UnmapBuffer(
        _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type);

    _Ret_maybenull_
    DdiResource*
    GetBuffer(
        _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type) const
    {
        return ((UINT)Type < RxMaxDecoderBufferTypes) ? Buffers[Type].Resource.get() : nullptr;
    }

private:
    struct DecoderBuffer
    {
        std::unique_ptr<DdiResource> Resource;
        UINT Size = 0;
        BOOLEAN IsDynamic = FALSE;
        BOOLEAN IsMapped = FALSE;
    };

    explicit RxVideoDecoder(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoDecoder() override;

    HRESULT
    CreateBuffers();

    D3D11_VIDEO_DECODER_DESC Desc = {};
    D3D11_VIDEO_DECODER_CONFIG Config = {};
    PVOID Memory = nullptr;
    D3D11_1DDI_HDECODE Handle = {};
    BOOLEAN IsCreated = FALSE;
    DecoderBuffer Buffers[RxMaxDecoderBufferTypes];
};

class RxVideoDecoderOutputView final : public RxDeviceChild<ID3D11VideoDecoderOutputView>
{
public:
    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ ID3D11Resource* Resource,
        _In_ const D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC& Desc,
        _Outptr_ RxVideoDecoderOutputView** View);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videodecoderoutputview.inl"

    D3D11_1DDI_HVIDEODECODEROUTPUTVIEW
    GetHandle() const
    {
        return Handle;
    }

    VOID
    NoteWrite()
    {
        Backing->NoteWrite();
    }

private:
    explicit RxVideoDecoderOutputView(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoDecoderOutputView() override;

    ID3D11Resource* Resource = nullptr;
    DdiResource* Backing = nullptr;
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC Desc = {};
    PVOID Memory = nullptr;
    D3D11_1DDI_HVIDEODECODEROUTPUTVIEW Handle = {};
    BOOLEAN IsCreated = FALSE;
};
