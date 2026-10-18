/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Hand written ID3D11VideoDevice methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videoobjects.hpp"
#include "videoprocessor.hpp"

/* The DDI decoder description and configuration mirror the API structures */
static_assert(sizeof(D3D11_1DDI_VIDEO_DECODER_DESC) == sizeof(D3D11_VIDEO_DECODER_DESC));
static_assert(sizeof(D3D11_1DDI_VIDEO_DECODER_CONFIG) == sizeof(D3D11_VIDEO_DECODER_CONFIG));

static
D3D11_1DDI_VIDEO_DECODER_DESC
RxToDdiDecoderDesc(
    _In_ const D3D11_VIDEO_DECODER_DESC& Desc)
{
    D3D11_1DDI_VIDEO_DECODER_DESC DdiDesc;

    DdiDesc.Guid = Desc.Guid;
    DdiDesc.SampleWidth = Desc.SampleWidth;
    DdiDesc.SampleHeight = Desc.SampleHeight;
    DdiDesc.OutputFormat = Desc.OutputFormat;
    return DdiDesc;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxVideoDevice::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxVideoDevice::Release(VOID)
{
    return Device.Release();
}

UINT
STDMETHODCALLTYPE
RxVideoDevice::GetVideoDecoderProfileCount(VOID)
{
    UINT Count = 0;

    Device.GetVideoFuncs().pfnGetVideoDecoderProfileCount(Device.GetDriverDevice(), &Count);
    if (FAILED(Device.TakeDriverError()))
        return 0;

    return Count;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::GetVideoDecoderProfile(
    _In_ UINT Index,
    _Out_ GUID* pDecoderProfile)
{
    if (pDecoderProfile == nullptr)
        return E_INVALIDARG;
    if (Index >= GetVideoDecoderProfileCount())
        return E_INVALIDARG;

    Device.GetVideoFuncs().pfnGetVideoDecoderProfile(Device.GetDriverDevice(), Index, pDecoderProfile);
    return Device.TakeDriverError();
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CheckVideoDecoderFormat(
    _In_ const GUID* pDecoderProfile,
    _In_ DXGI_FORMAT Format,
    _Out_ BOOL* pSupported)
{
    if (pDecoderProfile == nullptr || pSupported == nullptr)
        return E_INVALIDARG;

    *pSupported = FALSE;
    Device.GetVideoFuncs().pfnCheckVideoDecoderFormat(Device.GetDriverDevice(),
                                                      pDecoderProfile,
                                                      Format,
                                                      pSupported);
    return Device.TakeDriverError();
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::GetVideoDecoderConfigCount(
    _In_ const D3D11_VIDEO_DECODER_DESC* pDesc,
    _Out_ UINT* pCount)
{
    D3D11_1DDI_VIDEO_DECODER_DESC DdiDesc;

    if (pDesc == nullptr || pCount == nullptr)
        return E_INVALIDARG;

    *pCount = 0;
    DdiDesc = RxToDdiDecoderDesc(*pDesc);
    Device.GetVideoFuncs().pfnGetVideoDecoderConfigCount(Device.GetDriverDevice(), &DdiDesc, pCount);
    return Device.TakeDriverError();
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::GetVideoDecoderConfig(
    _In_ const D3D11_VIDEO_DECODER_DESC* pDesc,
    _In_ UINT Index,
    _Out_ D3D11_VIDEO_DECODER_CONFIG* pConfig)
{
    D3D11_1DDI_VIDEO_DECODER_CONFIG DdiConfig = {};
    D3D11_1DDI_VIDEO_DECODER_DESC DdiDesc;
    HRESULT hr;

    if (pDesc == nullptr || pConfig == nullptr)
        return E_INVALIDARG;

    DdiDesc = RxToDdiDecoderDesc(*pDesc);
    Device.GetVideoFuncs().pfnGetVideoDecoderConfig(Device.GetDriverDevice(), &DdiDesc, Index, &DdiConfig);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
        return hr;

    memcpy(pConfig, &DdiConfig, sizeof(*pConfig));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoDecoder(
    _In_ const D3D11_VIDEO_DECODER_DESC* pVideoDesc,
    _In_ const D3D11_VIDEO_DECODER_CONFIG* pConfig,
    _COM_Outptr_ ID3D11VideoDecoder** ppDecoder)
{
    RxVideoDecoder* Decoder;
    HRESULT hr;

    if (ppDecoder == nullptr)
        return E_POINTER;
    *ppDecoder = nullptr;
    if (pVideoDesc == nullptr || pConfig == nullptr)
        return E_INVALIDARG;

    hr = RxVideoDecoder::Create(Device, *pVideoDesc, *pConfig, &Decoder);
    if (SUCCEEDED(hr))
        *ppDecoder = Decoder;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoDecoderOutputView(
    _In_ ID3D11Resource* pResource,
    _In_ const D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11VideoDecoderOutputView** ppVDOVView)
{
    RxVideoDecoderOutputView* View;
    HRESULT hr;

    if (ppVDOVView != nullptr)
        *ppVDOVView = nullptr;
    if (pResource == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoDecoderOutputView::Create(Device, pResource, *pDesc, &View);
    if (FAILED(hr))
        return hr;

    /* A null output pointer only validates the arguments */
    if (ppVDOVView == nullptr)
    {
        View->Release();
        return S_FALSE;
    }

    *ppVDOVView = View;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoProcessorEnumerator(
    _In_ const D3D11_VIDEO_PROCESSOR_CONTENT_DESC* pDesc,
    _COM_Outptr_ ID3D11VideoProcessorEnumerator** ppEnum)
{
    RxVideoProcessorEnumerator* Enumerator;
    HRESULT hr;

    if (ppEnum == nullptr)
        return E_INVALIDARG;
    *ppEnum = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoProcessorEnumerator::Create(Device, *pDesc, &Enumerator);
    if (SUCCEEDED(hr))
        *ppEnum = Enumerator;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoProcessor(
    _In_ ID3D11VideoProcessorEnumerator* pEnum,
    _In_ UINT RateConversionIndex,
    _COM_Outptr_ ID3D11VideoProcessor** ppVideoProcessor)
{
    RxVideoProcessorEnumerator* Enumerator = RxUnwrap<RxVideoProcessorEnumerator>(pEnum);
    RxVideoProcessor* Processor;
    HRESULT hr;

    if (ppVideoProcessor == nullptr)
        return E_INVALIDARG;
    *ppVideoProcessor = nullptr;
    if (Enumerator == nullptr)
        return E_INVALIDARG;

    hr = RxVideoProcessor::Create(Device, *Enumerator, RateConversionIndex, &Processor);
    if (SUCCEEDED(hr))
        *ppVideoProcessor = Processor;
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoProcessorInputView(
    _In_ ID3D11Resource* pResource,
    _In_ ID3D11VideoProcessorEnumerator* pEnum,
    _In_ const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11VideoProcessorInputView** ppVPIView)
{
    RxVideoProcessorEnumerator* Enumerator = RxUnwrap<RxVideoProcessorEnumerator>(pEnum);
    RxVideoProcessorInputView* View;
    HRESULT hr;

    if (ppVPIView != nullptr)
        *ppVPIView = nullptr;
    if (pResource == nullptr || Enumerator == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoProcessorInputView::Create(Device, pResource, *Enumerator, *pDesc, &View);
    if (FAILED(hr))
        return hr;

    /* A null output pointer only validates the arguments */
    if (ppVPIView == nullptr)
    {
        View->Release();
        return S_FALSE;
    }

    *ppVPIView = View;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice::CreateVideoProcessorOutputView(
    _In_ ID3D11Resource* pResource,
    _In_ ID3D11VideoProcessorEnumerator* pEnum,
    _In_ const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC* pDesc,
    _COM_Outptr_opt_ ID3D11VideoProcessorOutputView** ppVPOView)
{
    RxVideoProcessorEnumerator* Enumerator = RxUnwrap<RxVideoProcessorEnumerator>(pEnum);
    RxVideoProcessorOutputView* View;
    HRESULT hr;

    if (ppVPOView != nullptr)
        *ppVPOView = nullptr;
    if (pResource == nullptr || Enumerator == nullptr || pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoProcessorOutputView::Create(Device, pResource, *Enumerator, *pDesc, &View);
    if (FAILED(hr))
        return hr;

    if (ppVPOView == nullptr)
    {
        View->Release();
        return S_FALSE;
    }

    *ppVPOView = View;
    return S_OK;
}
