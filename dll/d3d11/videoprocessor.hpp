/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video processor, its enumerator and its input and output views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "resource.hpp"

#include <array>
#include <vector>

/* The eight D3D11_VIDEO_PROCESSOR_FILTER values, brightness through anamorphic scaling */
constexpr UINT RxVideoFilterCount = 8;

class RxVideoProcessorEnumerator final : public RxDeviceChild<ID3D11VideoProcessorEnumerator>
{
public:
    static constexpr GUID PrivateId = { 0x8f1d2c47, 0x5a3e, 0x4b81, { 0xa9, 0x6c, 0x2e, 0x7f, 0x14, 0xd0, 0x93, 0x5b } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ const D3D11_VIDEO_PROCESSOR_CONTENT_DESC& Desc,
        _Outptr_ RxVideoProcessorEnumerator** Enumerator);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videoprocessorenumerator.inl"

    D3D11_1DDI_HVIDEOPROCESSORENUM
    GetHandle() const
    {
        return Handle;
    }

    /* Caps are read from the driver once, on first use */
    const D3D11_VIDEO_PROCESSOR_CAPS&
    GetCaps();

    UINT
    GetFormatSupport(
        _In_ DXGI_FORMAT Format);

private:
    explicit RxVideoProcessorEnumerator(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoProcessorEnumerator() override;

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC Desc = {};
    RxDriverMemory Memory;
    D3D11_1DDI_HVIDEOPROCESSORENUM Handle = {};
    BOOLEAN IsCreated = FALSE;
    BOOLEAN HasCaps = FALSE;
    D3D11_VIDEO_PROCESSOR_CAPS Caps = {};
};

class RxVideoProcessorInputView final : public RxDeviceChild<ID3D11VideoProcessorInputView>
{
public:
    static constexpr GUID PrivateId = { 0x4c0b9e61, 0xd27a, 0x4f3c, { 0x8b, 0x15, 0x6a, 0xe2, 0x39, 0x0d, 0xc7, 0x84 } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ ID3D11Resource* Resource,
        _In_ RxVideoProcessorEnumerator& Enumerator,
        _In_ const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC& Desc,
        _Outptr_ RxVideoProcessorInputView** View);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videoprocessorinputview.inl"

    D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW
    GetHandle() const
    {
        return Handle;
    }

    const D3D11_TEXTURE2D_DESC&
    GetTextureDesc() const
    {
        return TextureDesc;
    }

    /* Tells the driver about writes to the texture since this view last read it */
    VOID
    NoteRead();

private:
    explicit RxVideoProcessorInputView(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoProcessorInputView() override;

    ID3D11Resource* Resource = nullptr;
    DdiResource* Backing = nullptr;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC Desc = {};
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    RxDriverMemory Memory;
    D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW Handle = {};
    BOOLEAN IsCreated = FALSE;

    /* Starts out of step, so the first read reports whatever came before the view existed */
    UINT64 SeenWrites = ~0ull;
};

class RxVideoProcessorOutputView final : public RxDeviceChild<ID3D11VideoProcessorOutputView>
{
public:
    static constexpr GUID PrivateId = { 0xe6a35f08, 0x91bd, 0x4e27, { 0xb4, 0x72, 0x0c, 0x5d, 0xa8, 0x63, 0x1f, 0xe9 } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ ID3D11Resource* Resource,
        _In_ RxVideoProcessorEnumerator& Enumerator,
        _In_ const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC& Desc,
        _Outptr_ RxVideoProcessorOutputView** View);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videoprocessoroutputview.inl"

    D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW
    GetHandle() const
    {
        return Handle;
    }

    /* Width and height of the mip level the view writes */
    SIZE
    GetSize() const;

    UINT
    GetArraySize() const
    {
        return ArraySize;
    }

    DXGI_FORMAT
    GetFormat() const
    {
        return TextureDesc.Format;
    }

    VOID
    NoteWrite()
    {
        Backing->NoteWrite();
    }

private:
    explicit RxVideoProcessorOutputView(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoProcessorOutputView() override;

    ID3D11Resource* Resource = nullptr;
    DdiResource* Backing = nullptr;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC Desc = {};
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    UINT ArraySize = 1;
    RxDriverMemory Memory;
    D3D11_1DDI_HVIDEOPROCESSOROUTPUTVIEW Handle = {};
    BOOLEAN IsCreated = FALSE;
};

class RxVideoProcessor final : public RxDeviceChild<ID3D11VideoProcessor>
{
public:
    static constexpr GUID PrivateId = { 0x27d94b3a, 0x6c18, 0x4d05, { 0x9f, 0xe3, 0x41, 0xb8, 0x0a, 0x76, 0xc5, 0x2d } };

    /* Everything the Get methods hand back; the driver never has to be asked */
    struct OutputState
    {
        BOOL TargetRectEnabled = FALSE;
        RECT TargetRect = {};
        BOOL BackgroundYCbCr = FALSE;
        D3D11_VIDEO_COLOR Background = {};
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace = {};
        D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE AlphaFillMode = D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE;
        UINT AlphaFillStream = 0;
        BOOL ConstrictionEnabled = FALSE;
        SIZE ConstrictionSize = {};
        BOOL StereoMode = FALSE;
    };

    struct FilterState
    {
        BOOL Enabled = FALSE;
        INT Level = 0;
    };

    struct StreamState
    {
        D3D11_VIDEO_FRAME_FORMAT FrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace = {};
        D3D11_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate = D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL;
        BOOL RepeatFrame = FALSE;
        DXGI_RATIONAL CustomRate = {};
        BOOL SourceRectEnabled = FALSE;
        RECT SourceRect = {};
        BOOL DestRectEnabled = FALSE;
        RECT DestRect = {};
        BOOL AlphaEnabled = FALSE;
        FLOAT Alpha = 1.0f;
        std::vector<UINT> Palette;
        BOOL AspectRatioEnabled = FALSE;
        DXGI_RATIONAL SourceAspectRatio = {};
        DXGI_RATIONAL DestAspectRatio = {};
        BOOL LumaKeyEnabled = FALSE;
        FLOAT LumaKeyLower = 0.0f;
        FLOAT LumaKeyUpper = 0.0f;
        BOOL StereoEnabled = FALSE;
        D3D11_VIDEO_PROCESSOR_STEREO_FORMAT StereoFormat = D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO;
        BOOL LeftViewFrame0 = FALSE;
        BOOL BaseViewFrame0 = FALSE;
        D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode = D3D11_VIDEO_PROCESSOR_STEREO_FLIP_NONE;
        INT MonoOffset = 0;
        BOOL AutoProcessing = TRUE;
        std::array<FilterState, RxVideoFilterCount> Filters = {};
        BOOL RotationEnabled = FALSE;
        D3D11_VIDEO_PROCESSOR_ROTATION Rotation = D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY;
    };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ RxVideoProcessorEnumerator& Enumerator,
        _In_ UINT RateConversionIndex,
        _Outptr_ RxVideoProcessor** Processor);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

#include "videoprocessor.inl"

    /* State is pushed to the driver on first touch, through the same setters the API uses */
    const OutputState&
    GetOutput();

    _Ret_maybenull_
    const StreamState*
    GetStream(
        _In_ UINT Stream);

    VOID
    SetOutputTargetRect(
        _In_ BOOL Enable,
        _In_opt_ const RECT* Rect);

    VOID
    SetOutputBackgroundColor(
        _In_ BOOL YCbCr,
        _In_opt_ const D3D11_VIDEO_COLOR* Color);

    VOID
    SetOutputColorSpace(
        _In_opt_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* ColorSpace);

    VOID
    SetOutputAlphaFillMode(
        _In_ D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE Mode,
        _In_ UINT StreamIndex);

    VOID
    SetOutputConstriction(
        _In_ BOOL Enable,
        _In_ SIZE Size);

    VOID
    SetOutputStereoMode(
        _In_ BOOL Enable);

    HRESULT
    SetOutputExtension(
        _In_ const GUID* Extension,
        _In_ UINT DataSize,
        _In_ PVOID Data);

    HRESULT
    GetOutputExtension(
        _In_ const GUID* Extension,
        _In_ UINT DataSize,
        _Out_writes_bytes_(DataSize) PVOID Data);

    VOID
    SetStreamFrameFormat(
        _In_ UINT Stream,
        _In_ D3D11_VIDEO_FRAME_FORMAT Format);

    VOID
    SetStreamColorSpace(
        _In_ UINT Stream,
        _In_opt_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* ColorSpace);

    VOID
    SetStreamOutputRate(
        _In_ UINT Stream,
        _In_ D3D11_VIDEO_PROCESSOR_OUTPUT_RATE Rate,
        _In_ BOOL RepeatFrame,
        _In_opt_ const DXGI_RATIONAL* CustomRate);

    VOID
    SetStreamSourceRect(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_opt_ const RECT* Rect);

    VOID
    SetStreamDestRect(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_opt_ const RECT* Rect);

    VOID
    SetStreamAlpha(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_ FLOAT Alpha);

    VOID
    SetStreamPalette(
        _In_ UINT Stream,
        _In_ UINT Count,
        _In_reads_opt_(Count) const UINT* Entries);

    VOID
    SetStreamPixelAspectRatio(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_opt_ const DXGI_RATIONAL* SourceRatio,
        _In_opt_ const DXGI_RATIONAL* DestRatio);

    VOID
    SetStreamLumaKey(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_ FLOAT Lower,
        _In_ FLOAT Upper);

    VOID
    SetStreamStereoFormat(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_ D3D11_VIDEO_PROCESSOR_STEREO_FORMAT Format,
        _In_ BOOL LeftViewFrame0,
        _In_ BOOL BaseViewFrame0,
        _In_ D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode,
        _In_ INT MonoOffset);

    VOID
    SetStreamAutoProcessingMode(
        _In_ UINT Stream,
        _In_ BOOL Enable);

    VOID
    SetStreamFilter(
        _In_ UINT Stream,
        _In_ D3D11_VIDEO_PROCESSOR_FILTER Filter,
        _In_ BOOL Enable,
        _In_ INT Level);

    HRESULT
    SetStreamExtension(
        _In_ UINT Stream,
        _In_ const GUID* Extension,
        _In_ UINT DataSize,
        _In_ PVOID Data);

    HRESULT
    GetStreamExtension(
        _In_ UINT Stream,
        _In_ const GUID* Extension,
        _In_ UINT DataSize,
        _Out_writes_bytes_(DataSize) PVOID Data);

    VOID
    SetStreamRotation(
        _In_ UINT Stream,
        _In_ BOOL Enable,
        _In_ D3D11_VIDEO_PROCESSOR_ROTATION Rotation);

    HRESULT
    Blt(
        _In_ ID3D11VideoProcessorOutputView* OutputView,
        _In_ UINT OutputFrame,
        _In_ UINT StreamCount,
        _In_reads_(StreamCount) const D3D11_VIDEO_PROCESSOR_STREAM* InputStreams);

private:
    explicit RxVideoProcessor(
        _In_ RxDevice& Device)
        : RxDeviceChild(Device)
    {
    }

    ~RxVideoProcessor() override;

    VOID
    EnsureState();

    BOOLEAN
    IsValidStream(
        _In_ UINT Stream) const
    {
        return Stream < Streams.size();
    }

    HRESULT
    ValidateStream(
        _In_ UINT Index,
        _In_ const D3D11_VIDEO_PROCESSOR_STREAM& Stream,
        _In_ const RxVideoProcessorOutputView& Target);

    RxVideoProcessorEnumerator* Enumerator = nullptr;
    UINT RateConversionIndex = 0;
    D3D11_VIDEO_PROCESSOR_CAPS Caps = {};
    D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS RateCaps = {};
    std::array<D3D11_VIDEO_PROCESSOR_FILTER_RANGE, RxVideoFilterCount> FilterRanges = {};
    RxDriverMemory Memory;
    D3D11_1DDI_HVIDEOPROCESSOR Handle = {};
    BOOLEAN IsCreated = FALSE;
    BOOLEAN HasState = FALSE;
    OutputState Output;
    std::vector<StreamState> Streams;
};
