/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Hand written ID3D11VideoContext methods
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videoobjects.hpp"

#include <vector>

/* Every decoder and view reaching this context was created by this runtime */
static
RxVideoDecoder*
RxDecoderFrom(
    _In_ ID3D11VideoDecoder* Decoder)
{
    return static_cast<RxVideoDecoder*>(Decoder);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(ID3D11VideoContext))
    {
        *ppvObject = static_cast<ID3D11VideoContext*>(this);
        AddRef();
        return S_OK;
    }

    return Device.GetContext().QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxVideoContext::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxVideoContext::Release(VOID)
{
    return Device.Release();
}

VOID
STDMETHODCALLTYPE
RxVideoContext::GetDevice(
    _Outptr_ ID3D11Device** ppDevice)
{
    Device.AddRef();
    *ppDevice = &Device;
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::GetPrivateData(
    _In_ REFGUID guid,
    _Inout_ UINT* pDataSize,
    _Out_writes_bytes_opt_(*pDataSize) void* pData)
{
    return PrivateData.Get(guid, pDataSize, pData);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::SetPrivateData(
    _In_ REFGUID guid,
    _In_ UINT DataSize,
    _In_reads_bytes_opt_(DataSize) const void* pData)
{
    return PrivateData.Set(guid, DataSize, pData, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::SetPrivateDataInterface(
    _In_ REFGUID guid,
    _In_opt_ const IUnknown* pData)
{
    return PrivateData.Set(guid, 0, nullptr, pData);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::GetDecoderBuffer(
    _In_ ID3D11VideoDecoder* pDecoder,
    _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type,
    _Out_ UINT* pBufferSize,
    _Outptr_result_bytebuffer_(*pBufferSize) void** ppBuffer)
{
    if (pDecoder == nullptr || pBufferSize == nullptr || ppBuffer == nullptr)
        return E_INVALIDARG;

    return RxDecoderFrom(pDecoder)->MapBuffer(Type, pBufferSize, ppBuffer);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::ReleaseDecoderBuffer(
    _In_ ID3D11VideoDecoder* pDecoder,
    _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type)
{
    if (pDecoder == nullptr)
        return E_INVALIDARG;

    return RxDecoderFrom(pDecoder)->UnmapBuffer(Type);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::DecoderBeginFrame(
    _In_ ID3D11VideoDecoder* pDecoder,
    _In_ ID3D11VideoDecoderOutputView* pView,
    _In_ UINT ContentKeySize,
    _In_reads_bytes_opt_(ContentKeySize) const void* pContentKey)
{
    D3D11_1DDIARG_VIDEODECODERBEGINFRAME Args = {};
    HRESULT hr;

    if (pDecoder == nullptr || pView == nullptr)
        return E_INVALIDARG;

    Args.hOutputView = static_cast<RxVideoDecoderOutputView*>(pView)->GetHandle();
    Args.pContentKey = pContentKey;
    Args.ContentKeySize = ContentKeySize;

    hr = Device.GetVideoFuncs().pfnVideoDecoderBeginFrame(Device.GetDriverDevice(),
                                                          RxDecoderFrom(pDecoder)->GetHandle(),
                                                          &Args);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();

    /* The frame is a write to the output texture; video processor reads check for it */
    if (SUCCEEDED(hr))
        static_cast<RxVideoDecoderOutputView*>(pView)->NoteWrite();

    /* A busy decoder is reported as still drawing; the API calls that E_PENDING */
    if (hr == DXGI_DDI_ERR_WASSTILLDRAWING)
        return E_PENDING;
    return RxApiResult(hr);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::DecoderEndFrame(
    _In_ ID3D11VideoDecoder* pDecoder)
{
    if (pDecoder == nullptr)
        return E_INVALIDARG;

    Device.GetVideoFuncs().pfnVideoDecoderEndFrame(Device.GetDriverDevice(), RxDecoderFrom(pDecoder)->GetHandle());
    return RxApiResult(Device.TakeDriverError());
}

/* Hands the driver a private call on a decoder; drivers also use it as their own escape */
APP_DEPRECATED_HRESULT
STDMETHODCALLTYPE
RxVideoContext::DecoderExtension(
    _In_ ID3D11VideoDecoder* pDecoder,
    _In_ const D3D11_VIDEO_DECODER_EXTENSION* pExtensionData)
{
    D3D11_1DDIARG_VIDEODECODEREXTENSION Args = {};
    std::vector<D3D10DDI_HRESOURCE> Resources;
    DdiResource* Backing;
    UINT Index;
    HRESULT hr;

    if (pDecoder == nullptr || pExtensionData == nullptr)
        return E_INVALIDARG;

    /* A count goes with a list and no count with none */
    if ((pExtensionData->ResourceCount != 0) != (pExtensionData->ppResourceList != nullptr))
        return E_INVALIDARG;

    Resources.resize(pExtensionData->ResourceCount);
    for (Index = 0; Index < pExtensionData->ResourceCount; Index++)
    {
        Backing = RxGetDdiResource(pExtensionData->ppResourceList[Index]);
        if (Backing == nullptr)
            return E_INVALIDARG;
        Resources[Index] = Backing->GetHandle();
    }

    Args.Function = pExtensionData->Function;
    Args.pPrivateInputData = pExtensionData->pPrivateInputData;
    Args.PrivateInputDataSize = pExtensionData->PrivateInputDataSize;
    Args.pPrivateOutputData = pExtensionData->pPrivateOutputData;
    Args.PrivateOutputDataSize = pExtensionData->PrivateOutputDataSize;
    Args.ResourceCount = pExtensionData->ResourceCount;
    Args.pResourceList = Resources.empty() ? nullptr : Resources.data();

    hr = Device.GetVideoFuncs().pfnVideoDecoderExtension(Device.GetDriverDevice(),
                                                         RxDecoderFrom(pDecoder)->GetHandle(),
                                                         &Args);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
        RX_TRACE(L"DecoderExtension function 0x%X failed 0x%08lX", pExtensionData->Function, hr);
    return RxApiResult(hr);
}

HRESULT
STDMETHODCALLTYPE
RxVideoContext::SubmitDecoderBuffers(
    _In_ ID3D11VideoDecoder* pDecoder,
    _In_ UINT NumBuffers,
    _In_reads_(NumBuffers) const D3D11_VIDEO_DECODER_BUFFER_DESC* pBufferDesc)
{
    std::vector<D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC> Descs;
    RxVideoDecoder* Decoder;
    DdiResource* Buffer;
    UINT Index;
    HRESULT hr;

    if (pDecoder == nullptr || (NumBuffers != 0 && pBufferDesc == nullptr))
        return E_INVALIDARG;

    Decoder = RxDecoderFrom(pDecoder);
    Descs.resize(NumBuffers);
    for (Index = 0; Index < NumBuffers; Index++)
    {
        const D3D11_VIDEO_DECODER_BUFFER_DESC& Source = pBufferDesc[Index];
        D3D11_1DDI_VIDEO_DECODER_BUFFER_DESC& Target = Descs[Index];

        Buffer = Decoder->GetBuffer(Source.BufferType);
        if (Buffer == nullptr)
            return E_INVALIDARG;

        Target = {};
        Target.hResource = Buffer->GetHandle();
        Target.BufferType = (D3D11_1DDI_VIDEO_DECODER_BUFFER_TYPE)(Source.BufferType + 1);
        Target.BufferIndex = Source.BufferIndex;
        Target.DataOffset = Source.DataOffset;
        Target.DataSize = Source.DataSize;
        Target.FirstMBaddress = Source.FirstMBaddress;
        Target.NumMBsInBuffer = Source.NumMBsInBuffer;
        Target.Width = Source.Width;
        Target.Height = Source.Height;
        Target.Stride = Source.Stride;
        Target.ReservedBits = Source.ReservedBits;
        Target.pIV = Source.pIV;
        Target.IVSize = Source.IVSize;
        Target.PartialEncryption = Source.PartialEncryption;
        Target.EncryptedBlockInfo.NumEncryptedBytesAtBeginning = Source.EncryptedBlockInfo.NumEncryptedBytesAtBeginning;
        Target.EncryptedBlockInfo.NumBytesInSkipPattern = Source.EncryptedBlockInfo.NumBytesInSkipPattern;
        Target.EncryptedBlockInfo.NumBytesInEncryptPattern = Source.EncryptedBlockInfo.NumBytesInEncryptPattern;
    }

    hr = Device.GetVideoFuncs().pfnVideoDecoderSubmitBuffers(Device.GetDriverDevice(), Decoder->GetHandle(),
                                                             NumBuffers, Descs.data());
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    return RxApiResult(hr);
}
