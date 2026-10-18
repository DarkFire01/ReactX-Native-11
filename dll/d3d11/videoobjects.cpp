/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video decoder and decoder output view objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videoobjects.hpp"

#include <malloc.h>

static_assert(sizeof(D3D11_1DDI_VIDEO_DECODER_DESC) == sizeof(D3D11_VIDEO_DECODER_DESC));
static_assert(sizeof(D3D11_1DDI_VIDEO_DECODER_CONFIG) == sizeof(D3D11_VIDEO_DECODER_CONFIG));

RxVideoDecoder::~RxVideoDecoder()
{
    UINT Index;

    for (Index = 0; Index < RxMaxDecoderBufferTypes; Index++)
    {
        if (Buffers[Index].IsMapped)
            Buffers[Index].Resource->Unmap(0);
        Buffers[Index].Resource.reset();
    }

    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoDecoder(Device.GetDriverDevice(), Handle);

    if (Memory != nullptr)
        _aligned_free(Memory);
}

HRESULT
RxVideoDecoder::Create(
    _In_ RxDevice& Device,
    _In_ const D3D11_VIDEO_DECODER_DESC& Desc,
    _In_ const D3D11_VIDEO_DECODER_CONFIG& Config,
    _Outptr_ RxVideoDecoder** Decoder)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEODECODER Args;
    D3D11_1DDI_HRTDECODE RuntimeHandle;
    RxVideoDecoder* NewDecoder;
    SIZE_T Size;
    HRESULT hr;

    *Decoder = nullptr;

    NewDecoder = new (std::nothrow) RxVideoDecoder(Device);
    if (NewDecoder == nullptr)
        return E_OUTOFMEMORY;

    NewDecoder->Desc = Desc;
    NewDecoder->Config = Config;
    memcpy(&Args.Desc, &Desc, sizeof(Args.Desc));
    memcpy(&Args.Config, &Config, sizeof(Args.Config));

    Size = Funcs.pfnCalcPrivateVideoDecoderSize(Device.GetDriverDevice(), &Args);
    NewDecoder->Memory = _aligned_malloc(Size, 16);
    if (NewDecoder->Memory == nullptr)
    {
        NewDecoder->Release();
        return E_OUTOFMEMORY;
    }
    ZeroMemory(NewDecoder->Memory, Size);

    NewDecoder->Handle.pDrvPrivate = NewDecoder->Memory;
    RuntimeHandle.handle = static_cast<ID3D11VideoDecoder*>(NewDecoder);
    hr = Funcs.pfnCreateVideoDecoder(Device.GetDriverDevice(), &Args, NewDecoder->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoDecoder failed 0x%08lX", hr);
        NewDecoder->Release();
        return hr;
    }
    NewDecoder->IsCreated = TRUE;

    hr = NewDecoder->CreateBuffers();
    if (FAILED(hr))
    {
        NewDecoder->Release();
        return hr;
    }

    *Decoder = NewDecoder;
    return S_OK;
}

HRESULT
RxVideoDecoder::CreateBuffers()
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDI_VIDEO_DECODER_BUFFER_INFO Info;
    D3D11_1DDI_VIDEO_DECODER_DESC DdiDesc;
    UINT Count = 0;
    UINT Index;
    UINT ApiType;
    HRESULT hr;

    memcpy(&DdiDesc, &Desc, sizeof(DdiDesc));
    Funcs.pfnGetVideoDecoderBufferTypeCount(Device.GetDriverDevice(), &DdiDesc, &Count);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
        return hr;

    /* The driver says which buffers it wants and how big; each gets one resource */
    for (Index = 0; Index < Count; Index++)
    {
        Info = {};
        Funcs.pfnGetVideoDecoderBufferInfo(Device.GetDriverDevice(), &DdiDesc, Index, &Info);
        hr = Device.TakeDriverError();
        if (FAILED(hr))
            return hr;

        ApiType = (UINT)Info.Type - 1;
        if (Info.Type == D3D11_1DDI_VIDEO_DECODER_BUFFER_UNKNOWN || ApiType >= RxMaxDecoderBufferTypes)
            continue;

        Buffers[ApiType].Resource = std::make_unique<DdiResource>(Device);
        hr = Buffers[ApiType].Resource->CreateDecoderBuffer(Info);
        if (FAILED(hr))
        {
            RX_TRACE(L"decoder buffer type %u (%u bytes) failed 0x%08lX", Info.Type, Info.Size, hr);
            Buffers[ApiType].Resource.reset();
            return hr;
        }

        Buffers[ApiType].Size = Info.Size;
        Buffers[ApiType].IsDynamic = (Info.Usage == D3D10_DDI_USAGE_DYNAMIC);
        RX_TRACE(L"decoder buffer type %u: %u bytes, usage %u", Info.Type, Info.Size, Info.Usage);
    }

    return S_OK;
}

HRESULT
RxVideoDecoder::MapBuffer(
    _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type,
    _Out_ UINT* Size,
    _Out_ VOID** Data)
{
    D3D11_MAPPED_SUBRESOURCE Mapped;
    DecoderBuffer* Buffer;
    HRESULT hr;

    if ((UINT)Type >= RxMaxDecoderBufferTypes || Buffers[Type].Resource == nullptr)
        return E_INVALIDARG;

    Buffer = &Buffers[Type];
    if (Buffer->IsMapped)
        return E_INVALIDARG;

    /* Discard lets the driver rename the buffer while the previous frame still reads it */
    hr = Buffer->Resource->Map(0, Buffer->IsDynamic ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE, 0, &Mapped);
    if (FAILED(hr))
        return hr;

    Buffer->IsMapped = TRUE;
    *Data = Mapped.pData;
    *Size = Buffer->Size;
    return S_OK;
}

HRESULT
RxVideoDecoder::UnmapBuffer(
    _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type)
{
    if ((UINT)Type >= RxMaxDecoderBufferTypes || !Buffers[Type].IsMapped)
        return E_INVALIDARG;

    Buffers[Type].Resource->Unmap(0);
    Buffers[Type].IsMapped = FALSE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoder::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11VideoDecoder))
    {
        *ppvObject = static_cast<ID3D11VideoDecoder*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoder::GetCreationParameters(
    _Out_ D3D11_VIDEO_DECODER_DESC* pVideoDesc,
    _Out_ D3D11_VIDEO_DECODER_CONFIG* pConfig)
{
    if (pVideoDesc == nullptr || pConfig == nullptr)
        return E_INVALIDARG;

    *pVideoDesc = Desc;
    *pConfig = Config;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoder::GetDriverHandle(
    _Out_ HANDLE* pDriverHandle)
{
    if (pDriverHandle == nullptr)
        return E_INVALIDARG;

    *pDriverHandle = NULL;
    Device.GetVideoFuncs().pfnVideoDecoderGetHandle(Device.GetDriverDevice(), Handle, pDriverHandle);
    return Device.TakeDriverError();
}

RxVideoDecoderOutputView::~RxVideoDecoderOutputView()
{
    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoDecoderOutputView(Device.GetDriverDevice(), Handle);

    if (Memory != nullptr)
        _aligned_free(Memory);

    if (Resource != nullptr)
        Resource->Release();
}

HRESULT
RxVideoDecoderOutputView::Create(
    _In_ RxDevice& Device,
    _In_ ID3D11Resource* Resource,
    _In_ const D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC& Desc,
    _Outptr_ RxVideoDecoderOutputView** View)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEODECODEROUTPUTVIEW Args = {};
    D3D11_1DDI_HRTVIDEODECODEROUTPUTVIEW RuntimeHandle;
    RxVideoDecoderOutputView* NewView;
    DdiResource* Backing;
    SIZE_T Size;
    HRESULT hr;

    *View = nullptr;

    Backing = RxGetDdiResource(Resource);
    if (Backing == nullptr || Desc.ViewDimension != D3D11_VDOV_DIMENSION_TEXTURE2D)
        return E_INVALIDARG;

    NewView = new (std::nothrow) RxVideoDecoderOutputView(Device);
    if (NewView == nullptr)
        return E_OUTOFMEMORY;

    Resource->AddRef();
    NewView->Resource = Resource;
    NewView->Backing = Backing;
    NewView->Desc = Desc;

    Args.hDrvResource = Backing->GetHandle();
    Args.DecodeProfile = Desc.DecodeProfile;
    Args.MipSlice = 0;
    Args.FirstArraySlice = Desc.Texture2D.ArraySlice;
    Args.ArraySize = 1;

    Size = Funcs.pfnCalcPrivateVideoDecoderOutputViewSize(Device.GetDriverDevice(), &Args);
    NewView->Memory = _aligned_malloc(Size, 16);
    if (NewView->Memory == nullptr)
    {
        NewView->Release();
        return E_OUTOFMEMORY;
    }
    ZeroMemory(NewView->Memory, Size);

    NewView->Handle.pDrvPrivate = NewView->Memory;
    RuntimeHandle.handle = static_cast<ID3D11VideoDecoderOutputView*>(NewView);
    hr = Funcs.pfnCreateVideoDecoderOutputView(Device.GetDriverDevice(), &Args, NewView->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoDecoderOutputView failed 0x%08lX", hr);
        NewView->Release();
        return hr;
    }

    NewView->IsCreated = TRUE;
    *View = NewView;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoderOutputView::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11View) ||
        riid == __uuidof(ID3D11VideoDecoderOutputView))
    {
        *ppvObject = static_cast<ID3D11VideoDecoderOutputView*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

VOID
STDMETHODCALLTYPE
RxVideoDecoderOutputView::GetResource(
    _Outptr_ ID3D11Resource** ppResource)
{
    Resource->AddRef();
    *ppResource = Resource;
}

VOID
STDMETHODCALLTYPE
RxVideoDecoderOutputView::GetDesc(
    _Out_ D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC* pDesc)
{
    *pDesc = Desc;
}
