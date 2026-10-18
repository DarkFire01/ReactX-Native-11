/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11VideoContext video processor methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videoprocessor.hpp"

/* The setters go straight through; every getter answers from the processor's cached state */

static
RxVideoProcessor*
RxProcessorFrom(
    _In_opt_ ID3D11VideoProcessor* Processor)
{
    return RxUnwrap<RxVideoProcessor>(Processor);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputTargetRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ BOOL Enable,
    _In_opt_ const RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputTargetRect(Enable, pRect);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputBackgroundColor(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ BOOL YCbCr,
    _In_ const D3D11_VIDEO_COLOR* pColor)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputBackgroundColor(YCbCr, pColor);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputColorSpace(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* pColorSpace)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputColorSpace(pColorSpace);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputAlphaFillMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE AlphaFillMode,
    _In_ UINT StreamIndex)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputAlphaFillMode(AlphaFillMode, StreamIndex);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputConstriction(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ BOOL Enable,
    _In_ SIZE Size)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputConstriction(Enable, Size);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputStereoMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ BOOL Enable)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetOutputStereoMode(Enable);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetOutputExtension(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ const GUID* pExtensionGuid,
    _In_ UINT DataSize,
    _In_ void* pData)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr)
        return E_INVALIDARG;

    return Processor->SetOutputExtension(pExtensionGuid, DataSize, pData);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputTargetRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ BOOL* Enabled,
    _Out_ RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr || Enabled == nullptr || pRect == nullptr)
        return;

    *Enabled = Processor->GetOutput().TargetRectEnabled;
    *pRect = Processor->GetOutput().TargetRect;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputBackgroundColor(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ BOOL* pYCbCr,
    _Out_ D3D11_VIDEO_COLOR* pColor)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr || pYCbCr == nullptr || pColor == nullptr)
        return;

    *pYCbCr = Processor->GetOutput().BackgroundYCbCr;
    *pColor = Processor->GetOutput().Background;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputColorSpace(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ D3D11_VIDEO_PROCESSOR_COLOR_SPACE* pColorSpace)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr && pColorSpace != nullptr)
        *pColorSpace = Processor->GetOutput().ColorSpace;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputAlphaFillMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE* pAlphaFillMode,
    _Out_ UINT* pStreamIndex)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr || pAlphaFillMode == nullptr || pStreamIndex == nullptr)
        return;

    *pAlphaFillMode = Processor->GetOutput().AlphaFillMode;
    *pStreamIndex = Processor->GetOutput().AlphaFillStream;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputConstriction(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ BOOL* pEnabled,
    _Out_ SIZE* pSize)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr || pEnabled == nullptr || pSize == nullptr)
        return;

    *pEnabled = Processor->GetOutput().ConstrictionEnabled;
    *pSize = Processor->GetOutput().ConstrictionSize;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputStereoMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _Out_ BOOL* pEnabled)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr && pEnabled != nullptr)
        *pEnabled = Processor->GetOutput().StereoMode;
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetOutputExtension(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ const GUID* pExtensionGuid,
    _In_ UINT DataSize,
    _Out_writes_bytes_(DataSize) void* pData)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr)
        return E_INVALIDARG;

    return Processor->GetOutputExtension(pExtensionGuid, DataSize, pData);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamFrameFormat(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ D3D11_VIDEO_FRAME_FORMAT FrameFormat)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamFrameFormat(StreamIndex, FrameFormat);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamColorSpace(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* pColorSpace)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamColorSpace(StreamIndex, pColorSpace);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamOutputRate(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ D3D11_VIDEO_PROCESSOR_OUTPUT_RATE OutputRate,
    _In_ BOOL RepeatFrame,
    _In_opt_ const DXGI_RATIONAL* pCustomRate)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamOutputRate(StreamIndex, OutputRate, RepeatFrame, pCustomRate);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamSourceRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_opt_ const RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamSourceRect(StreamIndex, Enable, pRect);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamDestRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_opt_ const RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamDestRect(StreamIndex, Enable, pRect);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamAlpha(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_ FLOAT Alpha)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamAlpha(StreamIndex, Enable, Alpha);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamPalette(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ UINT Count,
    _In_reads_opt_(Count) const UINT* pEntries)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamPalette(StreamIndex, Count, pEntries);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamPixelAspectRatio(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_opt_ const DXGI_RATIONAL* pSourceAspectRatio,
    _In_opt_ const DXGI_RATIONAL* pDestinationAspectRatio)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamPixelAspectRatio(StreamIndex, Enable, pSourceAspectRatio, pDestinationAspectRatio);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamLumaKey(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_ FLOAT Lower,
    _In_ FLOAT Upper)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamLumaKey(StreamIndex, Enable, Lower, Upper);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamStereoFormat(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_ D3D11_VIDEO_PROCESSOR_STEREO_FORMAT Format,
    _In_ BOOL LeftViewFrame0,
    _In_ BOOL BaseViewFrame0,
    _In_ D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode,
    _In_ int MonoOffset)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamStereoFormat(StreamIndex, Enable, Format, LeftViewFrame0, BaseViewFrame0, FlipMode, MonoOffset);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamAutoProcessingMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamAutoProcessingMode(StreamIndex, Enable);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamFilter(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ D3D11_VIDEO_PROCESSOR_FILTER Filter,
    _In_ BOOL Enable,
    _In_ int Level)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamFilter(StreamIndex, Filter, Enable, Level);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamExtension(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ const GUID* pExtensionGuid,
    _In_ UINT DataSize,
    _In_ void* pData)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr)
        return E_INVALIDARG;

    return Processor->SetStreamExtension(StreamIndex, pExtensionGuid, DataSize, pData);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamFrameFormat(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ D3D11_VIDEO_FRAME_FORMAT* pFrameFormat)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State != nullptr && pFrameFormat != nullptr)
        *pFrameFormat = State->FrameFormat;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamColorSpace(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ D3D11_VIDEO_PROCESSOR_COLOR_SPACE* pColorSpace)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State != nullptr && pColorSpace != nullptr)
        *pColorSpace = State->ColorSpace;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamOutputRate(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ D3D11_VIDEO_PROCESSOR_OUTPUT_RATE* pOutputRate,
    _Out_ BOOL* pRepeatFrame,
    _Out_ DXGI_RATIONAL* pCustomRate)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pOutputRate == nullptr || pRepeatFrame == nullptr || pCustomRate == nullptr)
        return;

    *pOutputRate = State->OutputRate;
    *pRepeatFrame = State->RepeatFrame;
    *pCustomRate = State->CustomRate;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamSourceRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled,
    _Out_ RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnabled == nullptr || pRect == nullptr)
        return;

    *pEnabled = State->SourceRectEnabled;
    *pRect = State->SourceRect;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamDestRect(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled,
    _Out_ RECT* pRect)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnabled == nullptr || pRect == nullptr)
        return;

    *pEnabled = State->DestRectEnabled;
    *pRect = State->DestRect;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamAlpha(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled,
    _Out_ FLOAT* pAlpha)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnabled == nullptr || pAlpha == nullptr)
        return;

    *pEnabled = State->AlphaEnabled;
    *pAlpha = State->Alpha;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamPalette(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ UINT Count,
    _Out_writes_(Count) UINT* pEntries)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;
    UINT Entry;

    if (State == nullptr || pEntries == nullptr)
        return;

    /* Entries the app never set read back as zero */
    for (Entry = 0; Entry < Count; Entry++)
        pEntries[Entry] = (Entry < State->Palette.size()) ? State->Palette[Entry] : 0;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamPixelAspectRatio(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled,
    _Out_ DXGI_RATIONAL* pSourceAspectRatio,
    _Out_ DXGI_RATIONAL* pDestinationAspectRatio)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnabled == nullptr || pSourceAspectRatio == nullptr || pDestinationAspectRatio == nullptr)
        return;

    *pEnabled = State->AspectRatioEnabled;
    *pSourceAspectRatio = State->SourceAspectRatio;
    *pDestinationAspectRatio = State->DestAspectRatio;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamLumaKey(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled,
    _Out_ FLOAT* pLower,
    _Out_ FLOAT* pUpper)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnabled == nullptr || pLower == nullptr || pUpper == nullptr)
        return;

    *pEnabled = State->LumaKeyEnabled;
    *pLower = State->LumaKeyLower;
    *pUpper = State->LumaKeyUpper;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamStereoFormat(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnable,
    _Out_ D3D11_VIDEO_PROCESSOR_STEREO_FORMAT* pFormat,
    _Out_ BOOL* pLeftViewFrame0,
    _Out_ BOOL* pBaseViewFrame0,
    _Out_ D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE* pFlipMode,
    _Out_ int* MonoOffset)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnable == nullptr || pFormat == nullptr || pLeftViewFrame0 == nullptr ||
        pBaseViewFrame0 == nullptr || pFlipMode == nullptr || MonoOffset == nullptr)
    {
        return;
    }

    *pEnable = State->StereoEnabled;
    *pFormat = State->StereoFormat;
    *pLeftViewFrame0 = State->LeftViewFrame0;
    *pBaseViewFrame0 = State->BaseViewFrame0;
    *pFlipMode = State->FlipMode;
    *MonoOffset = State->MonoOffset;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamAutoProcessingMode(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnabled)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State != nullptr && pEnabled != nullptr)
        *pEnabled = State->AutoProcessing;
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamFilter(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ D3D11_VIDEO_PROCESSOR_FILTER Filter,
    _Out_ BOOL* pEnabled,
    _Out_ int* pLevel)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || (UINT)Filter >= RxVideoFilterCount || pEnabled == nullptr || pLevel == nullptr)
        return;

    *pEnabled = State->Filters[Filter].Enabled;
    *pLevel = State->Filters[Filter].Level;
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamExtension(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ const GUID* pExtensionGuid,
    _In_ UINT DataSize,
    _Out_writes_bytes_(DataSize) void* pData)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr)
        return E_INVALIDARG;

    return Processor->GetStreamExtension(StreamIndex, pExtensionGuid, DataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorBlt(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ ID3D11VideoProcessorOutputView* pView,
    _In_ UINT OutputFrame,
    _In_ UINT StreamCount,
    _In_reads_(StreamCount) const D3D11_VIDEO_PROCESSOR_STREAM* pStreams)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor == nullptr)
        return E_INVALIDARG;

    return Processor->Blt(pView, OutputFrame, StreamCount, pStreams);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorSetStreamRotation(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _In_ BOOL Enable,
    _In_ D3D11_VIDEO_PROCESSOR_ROTATION Rotation)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);

    if (Processor != nullptr)
        Processor->SetStreamRotation(StreamIndex, Enable, Rotation);
}

VOID
STDMETHODCALLTYPE
RxVideoContext::VideoProcessorGetStreamRotation(
    _In_ ID3D11VideoProcessor* pVideoProcessor,
    _In_ UINT StreamIndex,
    _Out_ BOOL* pEnable,
    _Out_ D3D11_VIDEO_PROCESSOR_ROTATION* pRotation)
{
    RxVideoProcessor* Processor = RxProcessorFrom(pVideoProcessor);
    const RxVideoProcessor::StreamState* State = Processor ? Processor->GetStream(StreamIndex) : nullptr;

    if (State == nullptr || pEnable == nullptr || pRotation == nullptr)
        return;

    *pEnable = State->RotationEnabled;
    *pRotation = State->Rotation;
}
