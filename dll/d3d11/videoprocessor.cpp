/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video processor, its enumerator and its input and output views
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videoprocessor.hpp"

#include <algorithm>

/* Known bits of each caps word; anything else the driver reports is dropped */
constexpr UINT RxDeviceCapsMask = 0x1F;
constexpr UINT RxFeatureCapsMask = 0xFFF;
constexpr UINT RxFilterCapsMask = 0xFF;
constexpr UINT RxInputFormatCapsMask = 0xF;
constexpr UINT RxAutoStreamCapsMask = 0xFF;
constexpr UINT RxStereoCapsMask = 0x1F;
constexpr UINT RxConversionCapsMask = 0x3F;
constexpr UINT RxTelecineCapsMask = 0x800001FF;

/* The color space bit fields line up with the DDI ones; only the low six bits mean anything */
constexpr UINT RxColorSpaceMask = 0x3F;

/* What the runtime pushes as the default stream and output color space: full range, BT.601 */
constexpr UINT RxDefaultColorSpace = 0x20;

static_assert(sizeof(D3D11_VIDEO_PROCESSOR_COLOR_SPACE) == sizeof(D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE));
static_assert(sizeof(D3D11_VIDEO_COLOR) == sizeof(D3D11_1DDI_VIDEO_COLOR));
static_assert(sizeof(D3D11_VIDEO_PROCESSOR_CUSTOM_RATE) == sizeof(D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE));
static_assert(sizeof(D3D11_VIDEO_PROCESSOR_FILTER_RANGE) == sizeof(D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE));

static
D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE
RxToDdiColorSpace(
    _In_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE& ColorSpace)
{
    D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE Ddi;
    UINT Bits;

    memcpy(&Bits, &ColorSpace, sizeof(Bits));
    Bits &= RxColorSpaceMask;
    memcpy(&Ddi, &Bits, sizeof(Ddi));
    return Ddi;
}

static
D3D11_VIDEO_PROCESSOR_COLOR_SPACE
RxColorSpaceFromBits(
    _In_ UINT Bits)
{
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace;

    memcpy(&ColorSpace, &Bits, sizeof(ColorSpace));
    return ColorSpace;
}

static
BOOLEAN
RxIsOrderedRect(
    _In_ const RECT& Rect)
{
    return Rect.left <= Rect.right && Rect.top <= Rect.bottom;
}

static
BOOLEAN
RxRectFits(
    _In_ const RECT& Rect,
    _In_ LONG Width,
    _In_ LONG Height)
{
    return Rect.left >= 0 && Rect.top >= 0 && Rect.right <= Width && Rect.bottom <= Height;
}

/* Chroma block of a video format: rects must start and end on it */
static
SIZE
RxChromaBlock(
    _In_ DXGI_FORMAT Format)
{
    switch (Format)
    {
        case DXGI_FORMAT_NV12:
        case DXGI_FORMAT_P010:
        case DXGI_FORMAT_P016:
        case DXGI_FORMAT_420_OPAQUE:
            return { 2, 2 };
        case DXGI_FORMAT_YUY2:
        case DXGI_FORMAT_Y210:
        case DXGI_FORMAT_Y216:
        case DXGI_FORMAT_P208:
        case DXGI_FORMAT_R8G8_B8G8_UNORM:
        case DXGI_FORMAT_G8R8_G8B8_UNORM:
            return { 2, 1 };
        case DXGI_FORMAT_NV11:
            return { 4, 1 };
        case DXGI_FORMAT_V208:
            return { 1, 2 };
        default:
            return { 1, 1 };
    }
}

static
LONG
RxAlignDown(
    _In_ LONG Value,
    _In_ LONG Block)
{
    return Value - (Value & (Block - 1));
}

static
LONG
RxAlignUp(
    _In_ LONG Value,
    _In_ LONG Block)
{
    return RxAlignDown(Value + Block - 1, Block);
}

/* Source rects shrink onto whole chroma blocks, destination rects grow onto them */
static
BOOLEAN
RxAlignRect(
    _Inout_ RECT& Rect,
    _In_ DXGI_FORMAT Format,
    _In_ BOOLEAN Grow)
{
    SIZE Block = RxChromaBlock(Format);
    RECT Aligned;

    if (Grow)
    {
        Aligned = { RxAlignDown(Rect.left, Block.cx), RxAlignDown(Rect.top, Block.cy),
                    RxAlignUp(Rect.right, Block.cx), RxAlignUp(Rect.bottom, Block.cy) };
    }
    else
    {
        Aligned = { RxAlignUp(Rect.left, Block.cx), RxAlignUp(Rect.top, Block.cy),
                    RxAlignDown(Rect.right, Block.cx), RxAlignDown(Rect.bottom, Block.cy) };
    }

    if (EqualRect(&Aligned, &Rect))
        return FALSE;

    Rect = Aligned;
    return TRUE;
}

/* Drivers on the first video DDI, Windows 8 era, get a few legacy behaviors */
static
BOOLEAN
RxIsFirstVideoDdi(
    _In_ RxDevice& Device)
{
    UINT Interface = Device.GetAdapter().GetRevision()->Interface;

    return Interface == D3D11_1_DDI_INTERFACE_VERSION || Interface == D3D11_1_DDI_INTERFACE_VERSION_RC;
}

/* Enumerator */

RxVideoProcessorEnumerator::~RxVideoProcessorEnumerator()
{
    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoProcessorEnum(Device.GetDriverDevice(), Handle);
}

HRESULT
RxVideoProcessorEnumerator::Create(
    _In_ RxDevice& Device,
    _In_ const D3D11_VIDEO_PROCESSOR_CONTENT_DESC& Desc,
    _Outptr_ RxVideoProcessorEnumerator** Enumerator)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEOPROCESSORENUM Args = {};
    D3D11_1DDI_HRTVIDEOPROCESSORENUM RuntimeHandle;
    RxVideoProcessorEnumerator* NewEnumerator;
    HRESULT hr;

    *Enumerator = nullptr;

    if ((UINT)Desc.InputFrameFormat > D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST ||
        (UINT)Desc.Usage > D3D11_VIDEO_USAGE_OPTIMAL_QUALITY ||
        (Desc.InputFrameRate.Numerator != 0 && Desc.InputFrameRate.Denominator == 0) ||
        (Desc.OutputFrameRate.Numerator != 0 && Desc.OutputFrameRate.Denominator == 0) ||
        Desc.InputWidth == 0 || Desc.InputHeight == 0 || Desc.OutputWidth == 0 || Desc.OutputHeight == 0)
    {
        return E_INVALIDARG;
    }

    NewEnumerator = new (std::nothrow) RxVideoProcessorEnumerator(Device);
    if (NewEnumerator == nullptr)
        return E_OUTOFMEMORY;
    NewEnumerator->Desc = Desc;

    /* Field for field the same; the two enums only take their three defined values */
    Args.Desc.InputFrameFormat = (D3D11_1DDI_VIDEO_FRAME_FORMAT)Desc.InputFrameFormat;
    Args.Desc.InputFrameRate = Desc.InputFrameRate;
    Args.Desc.InputWidth = Desc.InputWidth;
    Args.Desc.InputHeight = Desc.InputHeight;
    Args.Desc.OutputFrameRate = Desc.OutputFrameRate;
    Args.Desc.OutputWidth = Desc.OutputWidth;
    Args.Desc.OutputHeight = Desc.OutputHeight;
    Args.Desc.Usage = (D3D11_1DDI_VIDEO_USAGE)Desc.Usage;

    NewEnumerator->Handle.pDrvPrivate =
        NewEnumerator->Memory.Allocate(Funcs.pfnCalcPrivateVideoProcessorEnumSize(Device.GetDriverDevice(), &Args));
    if (NewEnumerator->Handle.pDrvPrivate == nullptr)
    {
        NewEnumerator->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<ID3D11VideoProcessorEnumerator*>(NewEnumerator);
    hr = Funcs.pfnCreateVideoProcessorEnum(Device.GetDriverDevice(), &Args, NewEnumerator->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoProcessorEnum failed 0x%08lX", hr);
        NewEnumerator->Release();
        return hr;
    }

    NewEnumerator->IsCreated = TRUE;
    *Enumerator = NewEnumerator;
    return S_OK;
}

const D3D11_VIDEO_PROCESSOR_CAPS&
RxVideoProcessorEnumerator::GetCaps()
{
    D3D11_1DDI_VIDEO_PROCESSOR_CAPS Ddi = {};

    if (HasCaps)
        return Caps;

    Device.GetVideoFuncs().pfnGetVideoProcessorCaps(Device.GetDriverDevice(), Handle, &Ddi);
    if (FAILED(Device.TakeDriverError()))
        Ddi = {};

    Caps.DeviceCaps = Ddi.DeviceCaps & RxDeviceCapsMask;
    Caps.FeatureCaps = Ddi.FeatureCaps & RxFeatureCapsMask;
    Caps.FilterCaps = Ddi.FilterCaps & RxFilterCapsMask;
    Caps.InputFormatCaps = Ddi.InputFormatCaps & RxInputFormatCapsMask;
    Caps.AutoStreamCaps = Ddi.AutoStreamCaps & RxAutoStreamCapsMask;
    Caps.StereoCaps = (Caps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO) ? (Ddi.StereoCaps & RxStereoCapsMask)
                                                                                    : 0;
    Caps.RateConversionCapsCount = Ddi.RateConversionCapsCount;
    Caps.MaxInputStreams = Ddi.MaxInputStreams;
    Caps.MaxStreamStates = Ddi.MaxStreamStates;

    /* The legacy feature bit only means something to drivers at the first video DDI */
    if (!RxIsFirstVideoDdi(Device))
        Caps.FeatureCaps &= ~D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_LEGACY;

    HasCaps = TRUE;
    return Caps;
}

UINT
RxVideoProcessorEnumerator::GetFormatSupport(
    _In_ DXGI_FORMAT Format)
{
    UINT Flags = 0;

    if (Format == DXGI_FORMAT_UNKNOWN)
        return 0;

    Device.GetVideoFuncs().pfnCheckVideoProcessorFormat(Device.GetDriverDevice(), Handle, Format, &Flags);
    if (FAILED(Device.TakeDriverError()))
        return 0;

    return Flags & (D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT | D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT);
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) ||
        riid == __uuidof(ID3D11VideoProcessorEnumerator))
    {
        *ppvObject = static_cast<ID3D11VideoProcessorEnumerator*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::GetVideoProcessorContentDesc(
    _Out_ D3D11_VIDEO_PROCESSOR_CONTENT_DESC* pContentDesc)
{
    if (pContentDesc == nullptr)
        return E_INVALIDARG;

    *pContentDesc = Desc;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::CheckVideoProcessorFormat(
    _In_ DXGI_FORMAT Format,
    _Out_ UINT* pFlags)
{
    if (pFlags == nullptr)
        return E_INVALIDARG;

    *pFlags = GetFormatSupport(Format);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::GetVideoProcessorCaps(
    _Out_ D3D11_VIDEO_PROCESSOR_CAPS* pCaps)
{
    if (pCaps == nullptr)
        return E_INVALIDARG;

    *pCaps = GetCaps();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::GetVideoProcessorRateConversionCaps(
    _In_ UINT TypeIndex,
    _Out_ D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS* pCaps)
{
    D3D11_1DDI_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS Ddi = {};
    HRESULT hr;

    if (pCaps == nullptr || TypeIndex >= GetCaps().RateConversionCapsCount)
        return E_INVALIDARG;

    Device.GetVideoFuncs().pfnGetVideoProcessorRateConversionCaps(Device.GetDriverDevice(), Handle, TypeIndex, &Ddi);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
        return hr;

    pCaps->PastFrames = Ddi.PastFrames;
    pCaps->FutureFrames = Ddi.FutureFrames;
    pCaps->ProcessorCaps = Ddi.ConversionCaps & RxConversionCapsMask;
    pCaps->ITelecineCaps = Ddi.ITelecineCaps & RxTelecineCapsMask;
    pCaps->CustomRateCount = Ddi.CustomRateCount;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::GetVideoProcessorCustomRate(
    _In_ UINT TypeIndex,
    _In_ UINT CustomRateIndex,
    _Out_ D3D11_VIDEO_PROCESSOR_CUSTOM_RATE* pRate)
{
    D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS RateCaps;
    D3D11_1DDI_VIDEO_PROCESSOR_CUSTOM_RATE Ddi = {};
    HRESULT hr;

    if (pRate == nullptr)
        return E_INVALIDARG;

    hr = GetVideoProcessorRateConversionCaps(TypeIndex, &RateCaps);
    if (FAILED(hr))
        return hr;
    if (CustomRateIndex >= RateCaps.CustomRateCount)
        return E_INVALIDARG;

    Device.GetVideoFuncs().pfnGetVideoProcessorCustomRate(Device.GetDriverDevice(), Handle, TypeIndex,
                                                         CustomRateIndex, &Ddi);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
        return hr;

    memcpy(pRate, &Ddi, sizeof(*pRate));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorEnumerator::GetVideoProcessorFilterRange(
    _In_ D3D11_VIDEO_PROCESSOR_FILTER Filter,
    _Out_ D3D11_VIDEO_PROCESSOR_FILTER_RANGE* pRange)
{
    D3D11_1DDI_VIDEO_PROCESSOR_FILTER_RANGE Ddi = {};
    HRESULT hr;

    if (pRange == nullptr || (UINT)Filter >= RxVideoFilterCount || (GetCaps().FilterCaps & (1u << Filter)) == 0)
        return E_INVALIDARG;

    Device.GetVideoFuncs().pfnGetVideoProcessorFilterRange(Device.GetDriverDevice(), Handle,
                                                          (D3D11_1DDI_VIDEO_PROCESSOR_FILTER)Filter, &Ddi);
    hr = Device.TakeDriverError();
    if (FAILED(hr))
        return hr;

    memcpy(pRange, &Ddi, sizeof(*pRange));
    return S_OK;
}

/* Input view */

RxVideoProcessorInputView::~RxVideoProcessorInputView()
{
    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoProcessorInputView(Device.GetDriverDevice(), Handle);

    if (Resource != nullptr)
        Resource->Release();
}

HRESULT
RxVideoProcessorInputView::Create(
    _In_ RxDevice& Device,
    _In_ ID3D11Resource* Resource,
    _In_ RxVideoProcessorEnumerator& Enumerator,
    _In_ const D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC& Desc,
    _Outptr_ RxVideoProcessorInputView** View)
{
    constexpr UINT ReadableBinds = D3D11_BIND_DECODER | D3D11_BIND_VIDEO_ENCODER | D3D11_BIND_RENDER_TARGET |
                                   D3D11_BIND_UNORDERED_ACCESS | 0x800;
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEOPROCESSORINPUTVIEW Args = {};
    D3D11_1DDI_HRTVIDEOPROCESSORINPUTVIEW RuntimeHandle;
    D3D11_TEXTURE2D_DESC TextureDesc;
    RxVideoProcessorInputView* NewView;
    DdiResource* Backing;
    HRESULT hr;

    *View = nullptr;

    Backing = RxGetDdiResource(Resource);
    if (Backing == nullptr || FAILED(RxGetTexture2DDesc(Resource, &TextureDesc)))
        return E_INVALIDARG;

    /* Any texture the GPU can write to, or one with no binds at all, can be read */
    if (TextureDesc.BindFlags != 0 && (TextureDesc.BindFlags & ReadableBinds) == 0 &&
        (TextureDesc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE) == 0)
    {
        return E_INVALIDARG;
    }

    if ((TextureDesc.MiscFlags & D3D11_RESOURCE_MISC_GUARDED) != 0 || TextureDesc.Usage == D3D11_USAGE_STAGING ||
        TextureDesc.SampleDesc.Count > 1 ||
        (Enumerator.GetFormatSupport(TextureDesc.Format) & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 ||
        Desc.ViewDimension != D3D11_VPIV_DIMENSION_TEXTURE2D || Desc.Texture2D.ArraySlice >= TextureDesc.ArraySize)
    {
        return E_INVALIDARG;
    }

    NewView = new (std::nothrow) RxVideoProcessorInputView(Device);
    if (NewView == nullptr)
        return E_OUTOFMEMORY;

    Resource->AddRef();
    NewView->Resource = Resource;
    NewView->Backing = Backing;
    NewView->Desc = Desc;
    NewView->TextureDesc = TextureDesc;

    Args.hDrvResource = Backing->GetHandle();
    Args.hDrvVideoProcessorEnum = Enumerator.GetHandle();
    Args.FourCC = Desc.FourCC;
    Args.MipSlice = Desc.Texture2D.MipSlice;
    Args.FirstArraySlice = Desc.Texture2D.ArraySlice;
    Args.ArraySize = 1;

    NewView->Handle.pDrvPrivate =
        NewView->Memory.Allocate(Funcs.pfnCalcPrivateVideoProcessorInputViewSize(Device.GetDriverDevice(), &Args));
    if (NewView->Handle.pDrvPrivate == nullptr)
    {
        NewView->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<ID3D11VideoProcessorInputView*>(NewView);
    hr = Funcs.pfnCreateVideoProcessorInputView(Device.GetDriverDevice(), &Args, NewView->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoProcessorInputView failed 0x%08lX", hr);
        NewView->Release();
        return hr;
    }

    NewView->IsCreated = TRUE;
    *View = NewView;
    return S_OK;
}

VOID
RxVideoProcessorInputView::NoteRead()
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    UINT64 Writes = Backing->GetWriteCount();

    if (Writes == SeenWrites)
        return;

    SeenWrites = Writes;
    if (Funcs.pfnVideoProcessorInputViewReadAfterWriteHazard != nullptr)
        Funcs.pfnVideoProcessorInputViewReadAfterWriteHazard(Device.GetDriverDevice(), Handle, Backing->GetHandle());
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorInputView::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11View) ||
        riid == __uuidof(ID3D11VideoProcessorInputView))
    {
        *ppvObject = static_cast<ID3D11VideoProcessorInputView*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

VOID
STDMETHODCALLTYPE
RxVideoProcessorInputView::GetResource(
    _Outptr_ ID3D11Resource** ppResource)
{
    Resource->AddRef();
    *ppResource = Resource;
}

VOID
STDMETHODCALLTYPE
RxVideoProcessorInputView::GetDesc(
    _Out_ D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC* pDesc)
{
    *pDesc = Desc;
}

/* Output view */

RxVideoProcessorOutputView::~RxVideoProcessorOutputView()
{
    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoProcessorOutputView(Device.GetDriverDevice(), Handle);

    if (Resource != nullptr)
        Resource->Release();
}

HRESULT
RxVideoProcessorOutputView::Create(
    _In_ RxDevice& Device,
    _In_ ID3D11Resource* Resource,
    _In_ RxVideoProcessorEnumerator& Enumerator,
    _In_ const D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC& Desc,
    _Outptr_ RxVideoProcessorOutputView** View)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEOPROCESSOROUTPUTVIEW Args = {};
    D3D11_1DDI_HRTVIDEOPROCESSOROUTPUTVIEW RuntimeHandle;
    D3D11_TEXTURE2D_DESC TextureDesc;
    RxVideoProcessorOutputView* NewView;
    DdiResource* Backing;
    BOOLEAN IsFirstDdi;
    UINT ArraySize;
    HRESULT hr;

    *View = nullptr;

    Backing = RxGetDdiResource(Resource);
    if (Backing == nullptr || FAILED(RxGetTexture2DDesc(Resource, &TextureDesc)))
        return E_INVALIDARG;

    if ((TextureDesc.BindFlags & D3D11_BIND_RENDER_TARGET) == 0 || TextureDesc.SampleDesc.Count > 1 ||
        (Enumerator.GetFormatSupport(TextureDesc.Format) & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0)
    {
        return E_INVALIDARG;
    }

    IsFirstDdi = RxIsFirstVideoDdi(Device);
    Args.hDrvResource = Backing->GetHandle();
    Args.hDrvVideoProcessorEnum = Enumerator.GetHandle();

    switch (Desc.ViewDimension)
    {
        case D3D11_VPOV_DIMENSION_TEXTURE2D:
            if (Desc.Texture2D.MipSlice >= TextureDesc.MipLevels || (IsFirstDdi && Desc.Texture2D.MipSlice != 0))
                return E_INVALIDARG;

            Args.MipSlice = Desc.Texture2D.MipSlice;
            Args.FirstArraySlice = 0;
            Args.ArraySize = 1;
            break;

        case D3D11_VPOV_DIMENSION_TEXTURE2DARRAY:
            /* All remaining slices when asked for -1; two slices only make sense for stereo */
            ArraySize = (Desc.Texture2DArray.ArraySize == (UINT)-1) ? TextureDesc.ArraySize - Desc.Texture2DArray.FirstArraySlice
                                                                   : Desc.Texture2DArray.ArraySize;
            if (Desc.Texture2DArray.MipSlice >= TextureDesc.MipLevels ||
                Desc.Texture2DArray.FirstArraySlice >= TextureDesc.ArraySize || ArraySize < 1 || ArraySize > 2 ||
                Desc.Texture2DArray.FirstArraySlice + ArraySize > TextureDesc.ArraySize ||
                (ArraySize > 1 && (Enumerator.GetCaps().FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO) == 0))
            {
                return E_INVALIDARG;
            }

            Args.MipSlice = Desc.Texture2DArray.MipSlice;
            Args.FirstArraySlice = Desc.Texture2DArray.FirstArraySlice;
            Args.ArraySize = ArraySize;
            break;

        default:
            return E_INVALIDARG;
    }

    NewView = new (std::nothrow) RxVideoProcessorOutputView(Device);
    if (NewView == nullptr)
        return E_OUTOFMEMORY;

    Resource->AddRef();
    NewView->Resource = Resource;
    NewView->Backing = Backing;
    NewView->Desc = Desc;
    NewView->TextureDesc = TextureDesc;
    NewView->ArraySize = Args.ArraySize;

    NewView->Handle.pDrvPrivate =
        NewView->Memory.Allocate(Funcs.pfnCalcPrivateVideoProcessorOutputViewSize(Device.GetDriverDevice(), &Args));
    if (NewView->Handle.pDrvPrivate == nullptr)
    {
        NewView->Release();
        return E_OUTOFMEMORY;
    }

    RuntimeHandle.handle = static_cast<ID3D11VideoProcessorOutputView*>(NewView);
    hr = Funcs.pfnCreateVideoProcessorOutputView(Device.GetDriverDevice(), &Args, NewView->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoProcessorOutputView failed 0x%08lX", hr);
        NewView->Release();
        return hr;
    }

    NewView->IsCreated = TRUE;
    *View = NewView;
    return S_OK;
}

SIZE
RxVideoProcessorOutputView::GetSize() const
{
    UINT Mip = (Desc.ViewDimension == D3D11_VPOV_DIMENSION_TEXTURE2D) ? Desc.Texture2D.MipSlice
                                                                     : Desc.Texture2DArray.MipSlice;

    return { (LONG)std::max(TextureDesc.Width >> Mip, 1u), (LONG)std::max(TextureDesc.Height >> Mip, 1u) };
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessorOutputView::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11View) ||
        riid == __uuidof(ID3D11VideoProcessorOutputView))
    {
        *ppvObject = static_cast<ID3D11VideoProcessorOutputView*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

VOID
STDMETHODCALLTYPE
RxVideoProcessorOutputView::GetResource(
    _Outptr_ ID3D11Resource** ppResource)
{
    Resource->AddRef();
    *ppResource = Resource;
}

VOID
STDMETHODCALLTYPE
RxVideoProcessorOutputView::GetDesc(
    _Out_ D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC* pDesc)
{
    *pDesc = Desc;
}

/* Processor */

RxVideoProcessor::~RxVideoProcessor()
{
    if (IsCreated)
        Device.GetVideoFuncs().pfnDestroyVideoProcessor(Device.GetDriverDevice(), Handle);

    if (Enumerator != nullptr)
        Enumerator->Release();
}

HRESULT
RxVideoProcessor::Create(
    _In_ RxDevice& Device,
    _In_ RxVideoProcessorEnumerator& Enumerator,
    _In_ UINT RateConversionIndex,
    _Outptr_ RxVideoProcessor** Processor)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();
    D3D11_1DDIARG_CREATEVIDEOPROCESSOR Args = {};
    D3D11_1DDI_HRTVIDEOPROCESSOR RuntimeHandle;
    RxVideoProcessor* NewProcessor;
    UINT Filter;
    HRESULT hr;

    *Processor = nullptr;

    NewProcessor = new (std::nothrow) RxVideoProcessor(Device);
    if (NewProcessor == nullptr)
        return E_OUTOFMEMORY;

    Enumerator.AddRef();
    NewProcessor->Enumerator = &Enumerator;
    NewProcessor->RateConversionIndex = RateConversionIndex;
    NewProcessor->Caps = Enumerator.GetCaps();

    /* An index past the driver's list fails here, before the driver sees it */
    hr = Enumerator.GetVideoProcessorRateConversionCaps(RateConversionIndex, &NewProcessor->RateCaps);
    if (FAILED(hr))
    {
        NewProcessor->Release();
        return hr;
    }

    for (Filter = 0; Filter < RxVideoFilterCount; Filter++)
    {
        if ((NewProcessor->Caps.FilterCaps & (1u << Filter)) != 0 &&
            FAILED(Enumerator.GetVideoProcessorFilterRange((D3D11_VIDEO_PROCESSOR_FILTER)Filter,
                                                           &NewProcessor->FilterRanges[Filter])))
        {
            NewProcessor->FilterRanges[Filter] = {};
        }
    }

    NewProcessor->Streams.resize(NewProcessor->Caps.MaxInputStreams);

    /* The size query sees no enumerator, the create call does */
    Args.RateConversionCapsIndex = RateConversionIndex;
    NewProcessor->Handle.pDrvPrivate =
        NewProcessor->Memory.Allocate(Funcs.pfnCalcPrivateVideoProcessorSize(Device.GetDriverDevice(), &Args));
    if (NewProcessor->Handle.pDrvPrivate == nullptr)
    {
        NewProcessor->Release();
        return E_OUTOFMEMORY;
    }

    Args.hVideoProcessorEnum = Enumerator.GetHandle();
    RuntimeHandle.handle = static_cast<ID3D11VideoProcessor*>(NewProcessor);
    hr = Funcs.pfnCreateVideoProcessor(Device.GetDriverDevice(), &Args, NewProcessor->Handle, RuntimeHandle);
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    if (FAILED(hr))
    {
        RX_TRACE(L"UMD CreateVideoProcessor failed 0x%08lX", hr);
        NewProcessor->Release();
        return hr;
    }

    NewProcessor->IsCreated = TRUE;
    *Processor = NewProcessor;
    return S_OK;
}

VOID
RxVideoProcessor::EnsureState()
{
    D3D11_VIDEO_COLOR Black = {};
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE ColorSpace = RxColorSpaceFromBits(RxDefaultColorSpace);
    UINT Stream;
    UINT Filter;

    if (HasState)
        return;

    /* Set before the defaults go out, so the setters below do not recurse */
    HasState = TRUE;

    Black.RGBA.A = 1.0f;
    SetOutputTargetRect(FALSE, nullptr);
    SetOutputBackgroundColor(FALSE, &Black);
    SetOutputColorSpace(&ColorSpace);
    SetOutputAlphaFillMode(D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE, 0);
    SetOutputConstriction(FALSE, { 1, 1 });
    SetOutputStereoMode(FALSE);

    for (Stream = 0; Stream < Streams.size(); Stream++)
    {
        SetStreamFrameFormat(Stream, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        SetStreamColorSpace(Stream, &ColorSpace);
        SetStreamOutputRate(Stream, D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL, FALSE, nullptr);
        SetStreamSourceRect(Stream, FALSE, nullptr);
        SetStreamDestRect(Stream, FALSE, nullptr);
        SetStreamAlpha(Stream, FALSE, 1.0f);
        SetStreamPalette(Stream, 0, nullptr);
        SetStreamPixelAspectRatio(Stream, FALSE, nullptr, nullptr);
        SetStreamLumaKey(Stream, FALSE, 0.0f, 0.0f);
        SetStreamStereoFormat(Stream, FALSE, D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO, FALSE, FALSE,
                              D3D11_VIDEO_PROCESSOR_STEREO_FLIP_NONE, 0);
        SetStreamRotation(Stream, FALSE, D3D11_VIDEO_PROCESSOR_ROTATION_IDENTITY);
        SetStreamAutoProcessingMode(Stream, TRUE);
        for (Filter = 0; Filter < RxVideoFilterCount; Filter++)
            SetStreamFilter(Stream, (D3D11_VIDEO_PROCESSOR_FILTER)Filter, FALSE, 0);
    }
}

const RxVideoProcessor::OutputState&
RxVideoProcessor::GetOutput()
{
    EnsureState();
    return Output;
}

_Ret_maybenull_
const RxVideoProcessor::StreamState*
RxVideoProcessor::GetStream(
    _In_ UINT Stream)
{
    EnsureState();
    return IsValidStream(Stream) ? &Streams[Stream] : nullptr;
}

VOID
RxVideoProcessor::SetOutputTargetRect(
    _In_ BOOL Enable,
    _In_opt_ const RECT* Rect)
{
    EnsureState();
    if (Enable && (Rect == nullptr || !RxIsOrderedRect(*Rect)))
        return;

    /* A disabled rect reaches the driver as zeros, never as NULL */
    Output.TargetRectEnabled = Enable;
    Output.TargetRect = Enable ? *Rect : RECT{};
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputTargetRect(Device.GetDriverDevice(), Handle, Enable,
                                                               &Output.TargetRect);
}

VOID
RxVideoProcessor::SetOutputBackgroundColor(
    _In_ BOOL YCbCr,
    _In_opt_ const D3D11_VIDEO_COLOR* Color)
{
    D3D11_1DDI_VIDEO_COLOR Ddi;

    EnsureState();
    if (Color == nullptr || Color->RGBA.A < 0.0f || Color->RGBA.A > 1.0f)
        return;

    Output.BackgroundYCbCr = YCbCr;
    Output.Background = *Color;
    memcpy(&Ddi, Color, sizeof(Ddi));
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputBackgroundColor(Device.GetDriverDevice(), Handle, YCbCr, &Ddi);
}

VOID
RxVideoProcessor::SetOutputColorSpace(
    _In_opt_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* ColorSpace)
{
    D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE Ddi;

    EnsureState();
    if (ColorSpace == nullptr)
        return;

    Output.ColorSpace = *ColorSpace;
    Ddi = RxToDdiColorSpace(*ColorSpace);
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputColorSpace(Device.GetDriverDevice(), Handle, &Ddi);
}

VOID
RxVideoProcessor::SetOutputAlphaFillMode(
    _In_ D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE Mode,
    _In_ UINT StreamIndex)
{
    EnsureState();
    if ((UINT)Mode > D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_SOURCE_STREAM ||
        (Mode == D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_SOURCE_STREAM && !IsValidStream(StreamIndex)))
    {
        return;
    }

    Output.AlphaFillMode = Mode;
    Output.AlphaFillStream = StreamIndex;
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputAlphaFillMode(Device.GetDriverDevice(), Handle,
                                                                  (D3D11_1DDI_VIDEO_PROCESSOR_ALPHA_FILL_MODE)Mode,
                                                                  StreamIndex);
}

VOID
RxVideoProcessor::SetOutputConstriction(
    _In_ BOOL Enable,
    _In_ SIZE Size)
{
    EnsureState();
    Output.ConstrictionEnabled = Enable;
    Output.ConstrictionSize = Size;
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputConstriction(Device.GetDriverDevice(), Handle, Enable, Size);
}

VOID
RxVideoProcessor::SetOutputStereoMode(
    _In_ BOOL Enable)
{
    EnsureState();
    if (Enable && (Caps.FeatureCaps & D3D11_VIDEO_PROCESSOR_FEATURE_CAPS_STEREO) == 0)
        return;

    Output.StereoMode = Enable;
    Device.GetVideoFuncs().pfnVideoProcessorSetOutputStereoMode(Device.GetDriverDevice(), Handle, Enable);
}

HRESULT
RxVideoProcessor::SetOutputExtension(
    _In_ const GUID* Extension,
    _In_ UINT DataSize,
    _In_ PVOID Data)
{
    EnsureState();
    if (Extension == nullptr)
        return E_INVALIDARG;

    return Device.GetVideoFuncs().pfnVideoProcessorSetOutputExtension(Device.GetDriverDevice(), Handle, Extension,
                                                                     DataSize, Data);
}

HRESULT
RxVideoProcessor::GetOutputExtension(
    _In_ const GUID* Extension,
    _In_ UINT DataSize,
    _Out_writes_bytes_(DataSize) PVOID Data)
{
    EnsureState();
    if (Extension == nullptr)
        return E_INVALIDARG;

    return Device.GetVideoFuncs().pfnVideoProcessorGetOutputExtension(Device.GetDriverDevice(), Handle, Extension,
                                                                     DataSize, Data);
}

VOID
RxVideoProcessor::SetStreamFrameFormat(
    _In_ UINT Stream,
    _In_ D3D11_VIDEO_FRAME_FORMAT Format)
{
    EnsureState();
    if (!IsValidStream(Stream) || (UINT)Format > D3D11_VIDEO_FRAME_FORMAT_INTERLACED_BOTTOM_FIELD_FIRST)
        return;

    Streams[Stream].FrameFormat = Format;
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamFrameFormat(Device.GetDriverDevice(), Handle, Stream,
                                                                (D3D11_1DDI_VIDEO_FRAME_FORMAT)Format);
}

VOID
RxVideoProcessor::SetStreamColorSpace(
    _In_ UINT Stream,
    _In_opt_ const D3D11_VIDEO_PROCESSOR_COLOR_SPACE* ColorSpace)
{
    D3D11_1DDI_VIDEO_PROCESSOR_COLOR_SPACE Ddi;

    EnsureState();
    if (!IsValidStream(Stream) || ColorSpace == nullptr)
        return;

    Streams[Stream].ColorSpace = *ColorSpace;
    Ddi = RxToDdiColorSpace(*ColorSpace);
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamColorSpace(Device.GetDriverDevice(), Handle, Stream, &Ddi);
}

VOID
RxVideoProcessor::SetStreamOutputRate(
    _In_ UINT Stream,
    _In_ D3D11_VIDEO_PROCESSOR_OUTPUT_RATE Rate,
    _In_ BOOL RepeatFrame,
    _In_opt_ const DXGI_RATIONAL* CustomRate)
{
    StreamState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (UINT)Rate > D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_CUSTOM ||
        (Rate == D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_CUSTOM && CustomRate == nullptr))
    {
        return;
    }

    State = &Streams[Stream];
    State->OutputRate = Rate;
    State->RepeatFrame = RepeatFrame;
    State->CustomRate = (Rate == D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_CUSTOM) ? *CustomRate : DXGI_RATIONAL{};
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamOutputRate(Device.GetDriverDevice(), Handle, Stream,
                                                               (D3D11_1DDI_VIDEO_PROCESSOR_OUTPUT_RATE)Rate,
                                                               RepeatFrame, &State->CustomRate);
}

VOID
RxVideoProcessor::SetStreamSourceRect(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_opt_ const RECT* Rect)
{
    StreamState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (Enable && (Rect == nullptr || !RxIsOrderedRect(*Rect))))
        return;

    State = &Streams[Stream];
    State->SourceRectEnabled = Enable;
    State->SourceRect = Enable ? *Rect : RECT{};
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamSourceRect(Device.GetDriverDevice(), Handle, Stream, Enable,
                                                               &State->SourceRect);
}

VOID
RxVideoProcessor::SetStreamDestRect(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_opt_ const RECT* Rect)
{
    StreamState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (Enable && (Rect == nullptr || !RxIsOrderedRect(*Rect))))
        return;

    State = &Streams[Stream];
    State->DestRectEnabled = Enable;
    State->DestRect = Enable ? *Rect : RECT{};
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamDestRect(Device.GetDriverDevice(), Handle, Stream, Enable,
                                                             &State->DestRect);
}

VOID
RxVideoProcessor::SetStreamAlpha(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_ FLOAT Alpha)
{
    EnsureState();
    if (!IsValidStream(Stream) || (Enable && (Alpha < 0.0f || Alpha > 1.0f)))
        return;

    Streams[Stream].AlphaEnabled = Enable;
    Streams[Stream].Alpha = Alpha;
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamAlpha(Device.GetDriverDevice(), Handle, Stream, Enable, Alpha);
}

VOID
RxVideoProcessor::SetStreamPalette(
    _In_ UINT Stream,
    _In_ UINT Count,
    _In_reads_opt_(Count) const UINT* Entries)
{
    EnsureState();
    if (!IsValidStream(Stream) || (Count != 0 && Entries == nullptr))
        return;

    Streams[Stream].Palette.assign(Entries, Entries + Count);
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamPalette(Device.GetDriverDevice(), Handle, Stream, Count,
                                                            Streams[Stream].Palette.data());
}

VOID
RxVideoProcessor::SetStreamPixelAspectRatio(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_opt_ const DXGI_RATIONAL* SourceRatio,
    _In_opt_ const DXGI_RATIONAL* DestRatio)
{
    StreamState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (Enable && (SourceRatio == nullptr || DestRatio == nullptr)))
        return;

    State = &Streams[Stream];
    State->AspectRatioEnabled = Enable;
    State->SourceAspectRatio = Enable ? *SourceRatio : DXGI_RATIONAL{};
    State->DestAspectRatio = Enable ? *DestRatio : DXGI_RATIONAL{};
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamPixelAspectRatio(Device.GetDriverDevice(), Handle, Stream, Enable,
                                                                     &State->SourceAspectRatio,
                                                                     &State->DestAspectRatio);
}

VOID
RxVideoProcessor::SetStreamLumaKey(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_ FLOAT Lower,
    _In_ FLOAT Upper)
{
    EnsureState();
    if (!IsValidStream(Stream) || (Enable && (Lower < 0.0f || Upper > 1.0f || Lower > Upper)))
        return;

    Streams[Stream].LumaKeyEnabled = Enable;
    Streams[Stream].LumaKeyLower = Lower;
    Streams[Stream].LumaKeyUpper = Upper;
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamLumaKey(Device.GetDriverDevice(), Handle, Stream, Enable, Lower,
                                                            Upper);
}

VOID
RxVideoProcessor::SetStreamStereoFormat(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_ D3D11_VIDEO_PROCESSOR_STEREO_FORMAT Format,
    _In_ BOOL LeftViewFrame0,
    _In_ BOOL BaseViewFrame0,
    _In_ D3D11_VIDEO_PROCESSOR_STEREO_FLIP_MODE FlipMode,
    _In_ INT MonoOffset)
{
    StreamState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (UINT)Format > D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_CHECKERBOARD ||
        (UINT)FlipMode > D3D11_VIDEO_PROCESSOR_STEREO_FLIP_FRAME1)
    {
        return;
    }

    State = &Streams[Stream];
    State->StereoEnabled = Enable;
    State->StereoFormat = Format;
    State->LeftViewFrame0 = LeftViewFrame0;
    State->BaseViewFrame0 = BaseViewFrame0;
    State->FlipMode = FlipMode;
    State->MonoOffset = MonoOffset;
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamStereoFormat(Device.GetDriverDevice(), Handle, Stream, Enable,
                                                                 (D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FORMAT)Format,
                                                                 LeftViewFrame0, BaseViewFrame0,
                                                                 (D3D11_1DDI_VIDEO_PROCESSOR_STEREO_FLIP_MODE)FlipMode,
                                                                 MonoOffset);
}

VOID
RxVideoProcessor::SetStreamAutoProcessingMode(
    _In_ UINT Stream,
    _In_ BOOL Enable)
{
    EnsureState();
    if (!IsValidStream(Stream))
        return;

    Streams[Stream].AutoProcessing = Enable;
    Device.GetVideoFuncs().pfnVideoProcessorSetStreamAutoProcessingMode(Device.GetDriverDevice(), Handle, Stream,
                                                                       Enable);
}

VOID
RxVideoProcessor::SetStreamFilter(
    _In_ UINT Stream,
    _In_ D3D11_VIDEO_PROCESSOR_FILTER Filter,
    _In_ BOOL Enable,
    _In_ INT Level)
{
    BOOLEAN IsSupported;
    FilterState* State;

    EnsureState();
    if (!IsValidStream(Stream) || (UINT)Filter >= RxVideoFilterCount)
        return;

    IsSupported = (Caps.FilterCaps & (1u << Filter)) != 0;
    if (Enable && (!IsSupported || Level < FilterRanges[Filter].Minimum || Level > FilterRanges[Filter].Maximum))
        return;

    /* Unsupported filters are only remembered; the driver never hears about them */
    State = &Streams[Stream].Filters[Filter];
    State->Enabled = Enable;
    State->Level = Enable ? Level : 0;
    if (IsSupported)
    {
        Device.GetVideoFuncs().pfnVideoProcessorSetStreamFilter(Device.GetDriverDevice(), Handle, Stream,
                                                               (D3D11_1DDI_VIDEO_PROCESSOR_FILTER)Filter, Enable,
                                                               State->Level);
    }
}

HRESULT
RxVideoProcessor::SetStreamExtension(
    _In_ UINT Stream,
    _In_ const GUID* Extension,
    _In_ UINT DataSize,
    _In_ PVOID Data)
{
    EnsureState();
    if (!IsValidStream(Stream) || Extension == nullptr)
        return E_INVALIDARG;

    return Device.GetVideoFuncs().pfnVideoProcessorSetStreamExtension(Device.GetDriverDevice(), Handle, Stream,
                                                                     Extension, DataSize, Data);
}

HRESULT
RxVideoProcessor::GetStreamExtension(
    _In_ UINT Stream,
    _In_ const GUID* Extension,
    _In_ UINT DataSize,
    _Out_writes_bytes_(DataSize) PVOID Data)
{
    EnsureState();
    if (!IsValidStream(Stream) || Extension == nullptr)
        return E_INVALIDARG;

    return Device.GetVideoFuncs().pfnVideoProcessorGetStreamExtension(Device.GetDriverDevice(), Handle, Stream,
                                                                     Extension, DataSize, Data);
}

VOID
RxVideoProcessor::SetStreamRotation(
    _In_ UINT Stream,
    _In_ BOOL Enable,
    _In_ D3D11_VIDEO_PROCESSOR_ROTATION Rotation)
{
    const D3D11_1DDI_VIDEODEVICEFUNCS& Funcs = Device.GetVideoFuncs();

    EnsureState();
    if (!IsValidStream(Stream) || (UINT)Rotation > D3D11_VIDEO_PROCESSOR_ROTATION_270)
        return;

    Streams[Stream].RotationEnabled = Enable;
    Streams[Stream].Rotation = Rotation;
    if (Funcs.pfnVideoProcessorSetStreamRotation != nullptr)
    {
        Funcs.pfnVideoProcessorSetStreamRotation(Device.GetDriverDevice(), Handle, Stream, Enable,
                                                 (D3D11_1DDI_VIDEO_PROCESSOR_ROTATION)Rotation);
    }
}

HRESULT
RxVideoProcessor::ValidateStream(
    _In_ UINT Index,
    _In_ const D3D11_VIDEO_PROCESSOR_STREAM& Stream,
    _In_ const RxVideoProcessorOutputView& Target)
{
    const StreamState& State = Streams[Index];
    RxVideoProcessorInputView* Input;
    BOOLEAN IsSeparate;
    SIZE OutputSize = Target.GetSize();
    UINT OutputFrames;
    UINT Frame;
    RECT Aligned;

    Input = RxUnwrap<RxVideoProcessorInputView>(Stream.pInputSurface);
    if (Input == nullptr)
        return E_INVALIDARG;

    /* Interlaced input at the normal rate yields a frame per field */
    OutputFrames = (State.FrameFormat != D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE &&
                    State.OutputRate == D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_NORMAL) ? 2 : 1;
    if (State.OutputRate != D3D11_VIDEO_PROCESSOR_OUTPUT_RATE_CUSTOM && Stream.OutputIndex >= OutputFrames)
        return E_INVALIDARG;

    IsSeparate = State.StereoFormat == D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_SEPARATE;
    if (IsSeparate != (Stream.pInputSurfaceRight != nullptr))
        return E_INVALIDARG;
    if (State.StereoFormat == D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_MONO_OFFSET && !Output.StereoMode)
        return E_INVALIDARG;

    if (Stream.PastFrames > RateCaps.PastFrames || Stream.FutureFrames > RateCaps.FutureFrames ||
        (Stream.PastFrames != 0 && Stream.ppPastSurfaces == nullptr) ||
        (Stream.FutureFrames != 0 && Stream.ppFutureSurfaces == nullptr))
    {
        return E_INVALIDARG;
    }

    /* Every reference frame has to look like the current one */
    for (Frame = 0; Frame < Stream.PastFrames + Stream.FutureFrames; Frame++)
    {
        RxVideoProcessorInputView* Other =
            RxUnwrap<RxVideoProcessorInputView>((Frame < Stream.PastFrames) ? Stream.ppPastSurfaces[Frame]
                                                                            : Stream.ppFutureSurfaces[Frame - Stream.PastFrames]);
        if (Other == nullptr || Other->GetTextureDesc().Width != Input->GetTextureDesc().Width ||
            Other->GetTextureDesc().Height != Input->GetTextureDesc().Height ||
            Other->GetTextureDesc().Format != Input->GetTextureDesc().Format)
        {
            return E_INVALIDARG;
        }
    }

    if (State.SourceRectEnabled)
    {
        if (!RxRectFits(State.SourceRect, Input->GetTextureDesc().Width, Input->GetTextureDesc().Height))
            return E_INVALIDARG;

        Aligned = State.SourceRect;
        if (RxAlignRect(Aligned, Input->GetTextureDesc().Format, FALSE))
            SetStreamSourceRect(Index, TRUE, &Aligned);
    }

    if (State.DestRectEnabled)
    {
        if (!RxRectFits(State.DestRect, OutputSize.cx, OutputSize.cy))
            return E_INVALIDARG;

        Aligned = State.DestRect;
        if (RxAlignRect(Aligned, Target.GetFormat(), TRUE))
            SetStreamDestRect(Index, TRUE, &Aligned);
    }

    return S_OK;
}

HRESULT
RxVideoProcessor::Blt(
    _In_ ID3D11VideoProcessorOutputView* OutputView,
    _In_ UINT OutputFrame,
    _In_ UINT StreamCount,
    _In_reads_(StreamCount) const D3D11_VIDEO_PROCESSOR_STREAM* InputStreams)
{
    std::vector<D3D11_1DDI_VIDEO_PROCESSOR_STREAM> DdiStreams;
    std::vector<D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW> References;
    RxVideoProcessorOutputView* Target;
    RxVideoProcessorInputView* View;
    D3D11_1DDI_VIDEO_PROCESSOR_STREAM* Ddi;
    SIZE OutputSize;
    UINT Enabled = 0;
    SIZE_T Reserved;
    UINT Index;
    RECT Aligned;
    HRESULT hr;

    EnsureState();

    Target = RxUnwrap<RxVideoProcessorOutputView>(OutputView);
    if (Target == nullptr || StreamCount > Caps.MaxStreamStates || (StreamCount != 0 && InputStreams == nullptr))
        return E_INVALIDARG;

    OutputSize = Target->GetSize();
    if (Output.TargetRectEnabled)
    {
        if (!RxRectFits(Output.TargetRect, OutputSize.cx, OutputSize.cy))
            return E_INVALIDARG;

        Aligned = Output.TargetRect;
        if (RxAlignRect(Aligned, Target->GetFormat(), TRUE))
            SetOutputTargetRect(TRUE, &Aligned);
    }

    /* Stereo output writes both slices of a two slice view, mono output exactly one */
    if (Target->GetArraySize() != (Output.StereoMode ? 2u : 1u))
        return E_INVALIDARG;

    for (Index = 0; Index < StreamCount; Index++)
    {
        if (!InputStreams[Index].Enable)
            continue;

        if (Index >= Streams.size())
            return E_INVALIDARG;

        hr = ValidateStream(Index, InputStreams[Index], *Target);
        if (FAILED(hr))
            return hr;
        Enabled++;
    }

    if (Enabled > Caps.MaxInputStreams)
        return E_INVALIDARG;

    /* Reference handles live in one array; reserve it all up front so pointers into it stay put */
    Reserved = 0;
    for (Index = 0; Index < StreamCount; Index++)
    {
        if (InputStreams[Index].Enable)
            Reserved += 2 * (InputStreams[Index].PastFrames + InputStreams[Index].FutureFrames);
    }
    References.reserve(Reserved);

    DdiStreams.resize(StreamCount);
    for (Index = 0; Index < StreamCount; Index++)
    {
        const D3D11_VIDEO_PROCESSOR_STREAM& Stream = InputStreams[Index];
        const BOOLEAN IsSeparate = Streams[Index].StereoFormat == D3D11_VIDEO_PROCESSOR_STEREO_FORMAT_SEPARATE;

        if (!Stream.Enable)
            continue;

        Ddi = &DdiStreams[Index];
        Ddi->Enable = TRUE;
        Ddi->OutputIndex = Stream.OutputIndex;
        Ddi->InputFrameOrField = Stream.InputFrameOrField;
        Ddi->PastFrames = Stream.PastFrames;
        Ddi->FutureFrames = Stream.FutureFrames;

        View = RxUnwrap<RxVideoProcessorInputView>(Stream.pInputSurface);
        View->NoteRead();
        Ddi->hInputSurface = View->GetHandle();

        View = RxUnwrap<RxVideoProcessorInputView>(Stream.pInputSurfaceRight);
        if (View != nullptr)
        {
            View->NoteRead();
            Ddi->hInputSurfaceRight = View->GetHandle();
        }

        auto AppendFrames = [&References](_In_reads_opt_(Count) ID3D11VideoProcessorInputView* const* Surfaces,
                                          _In_ UINT Count) -> D3D11_1DDI_HVIDEOPROCESSORINPUTVIEW*
        {
            SIZE_T First = References.size();
            RxVideoProcessorInputView* Reference;

            if (Count == 0 || Surfaces == nullptr)
                return nullptr;

            for (UINT Each = 0; Each < Count; Each++)
            {
                Reference = RxUnwrap<RxVideoProcessorInputView>(Surfaces[Each]);
                Reference->NoteRead();
                References.push_back(Reference->GetHandle());
            }
            return &References[First];
        };

        Ddi->pPastSurfaces = AppendFrames(Stream.ppPastSurfaces, Stream.PastFrames);
        Ddi->pFutureSurfaces = AppendFrames(Stream.ppFutureSurfaces, Stream.FutureFrames);
        if (IsSeparate)
        {
            Ddi->pPastSurfacesRight = AppendFrames(Stream.ppPastSurfacesRight, Stream.PastFrames);
            Ddi->pFutureSurfacesRight = AppendFrames(Stream.ppFutureSurfacesRight, Stream.FutureFrames);
        }
    }

    /* What the processor writes counts as a write for whoever reads the output next */
    Target->NoteWrite();

    hr = Device.GetVideoFuncs().pfnVideoProcessorBlt(Device.GetDriverDevice(), Handle, Target->GetHandle(),
                                                     OutputFrame, StreamCount, DdiStreams.data());
    if (SUCCEEDED(hr))
        hr = Device.TakeDriverError();
    return RxApiResult(hr);
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessor::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == PrivateId)
    {
        *ppvObject = this;
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11VideoProcessor))
    {
        *ppvObject = static_cast<ID3D11VideoProcessor*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

VOID
STDMETHODCALLTYPE
RxVideoProcessor::GetContentDesc(
    _Out_ D3D11_VIDEO_PROCESSOR_CONTENT_DESC* pDesc)
{
    Enumerator->GetVideoProcessorContentDesc(pDesc);
}

VOID
STDMETHODCALLTYPE
RxVideoProcessor::GetRateConversionCaps(
    _Out_ D3D11_VIDEO_PROCESSOR_RATE_CONVERSION_CAPS* pCaps)
{
    *pCaps = RateCaps;
}
