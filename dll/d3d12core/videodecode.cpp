/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video decode: ID3D12VideoDecoder, ID3D12VideoDecoderHeap and DecodeFrame
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"
#include "commandlist_internal.hpp"

/* The API node a node mask names, which must be one node at most */
static
BOOLEAN
RxSingleNode(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _Out_ UINT* NodeIndex)
{
    UINT Mask = NodeMask != 0 ? NodeMask : 1;
    UINT Index = 0;

    *NodeIndex = 0;
    if ((NodeMask & (NodeMask - 1)) != 0)
        return FALSE;

    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        Index++;
    }

    *NodeIndex = Index;
    return Index < Device.GetNodeCount();
}

/* The rules a decoder and its heaps share: one node, a profile the node lists, no unknown encryption */
static
BOOLEAN
RxDecodeConfigurationIsValid(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Configuration,
    _Out_ UINT* NodeIndex)
{
    if (!RxSingleNode(Device, NodeMask, NodeIndex))
        return FALSE;

    return Device.GetVideo()->HasProfile(*NodeIndex, Configuration.DecodeProfile) &&
           Configuration.BitstreamEncryption == D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE &&
           (UINT)Configuration.InterlaceType <= D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED;
}

/* A protected session asks the driver whether this decode configuration takes protected resources */
static
BOOLEAN
RxDecodeTakesSession(
    _In_ RxDevice12& Device,
    _In_ UINT NodeIndex,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Configuration)
{
    D3D12_FEATURE_DATA_VIDEO_DECODE_PROTECTED_RESOURCES Data = {};
    HRESULT hr;

    Data.NodeIndex = NodeIndex;
    Data.Configuration = Configuration;
    hr = Device.GetVideo()->Face().CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_PROTECTED_RESOURCES, &Data,
                                                        sizeof(Data));
    return SUCCEEDED(hr) && (Data.SupportFlags & D3D12_VIDEO_PROTECTED_RESOURCE_SUPPORT_FLAG_SUPPORTED) != 0;
}

/* Decoder heaps */

HRESULT
RxVideoDecoderHeap12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_DECODER_HEAP_DESC& Desc,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoDecoderHeap12** Heap)
{
    RxVideoDecoderHeap12* Created;
    HRESULT hr;

    *Heap = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoDecoderHeap12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, Session);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxVideoDecoderHeap12::Initialize(
    _In_ const D3D12_VIDEO_DECODER_HEAP_DESC& HeapDesc,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    RxVideoState12& Video = *Device.GetVideo();
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Video.Ddi();
    D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT Support = {};
    D3D12DDIARG_CREATE_VIDEO_DECODER_HEAP_0072 Args = {};
    UINT NodeIndex;
    SIZE_T Size;
    HRESULT hr;

    /* The heap must be one the driver says it decodes into */
    if (!RxSingleNode(Device, HeapDesc.NodeMask, &NodeIndex))
        return E_INVALIDARG;

    Support.NodeIndex = NodeIndex;
    Support.Configuration = HeapDesc.Configuration;
    Support.Width = HeapDesc.DecodeWidth;
    Support.Height = HeapDesc.DecodeHeight;
    Support.DecodeFormat = HeapDesc.Format;
    Support.FrameRate = HeapDesc.FrameRate;
    Support.BitRate = HeapDesc.BitRate;
    hr = Video.Face().CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT, &Support, sizeof(Support));
    if (FAILED(hr) || (Support.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED) == 0)
        return E_INVALIDARG;
    if (ProtectedSession != nullptr && !RxDecodeTakesSession(Device, NodeIndex, HeapDesc.Configuration))
        return E_INVALIDARG;

    Desc = HeapDesc;
    ConfigurationFlags = Support.ConfigurationFlags;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    hr = Device.TranslateNodeMask(HeapDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    RxToDdiConfiguration(HeapDesc.Configuration, &Args.Configuration);
    Args.DecodeWidth = HeapDesc.DecodeWidth;
    Args.DecodeHeight = HeapDesc.DecodeHeight;
    Args.Format = HeapDesc.Format;
    Args.FrameRate = HeapDesc.FrameRate;
    Args.BitRate = HeapDesc.BitRate;
    Args.MaxDecodePictureBufferCount = HeapDesc.MaxDecodePictureBufferCount;
    if (Session != nullptr)
        Args.hDrvProtectedResourceSession = Session->GetDriverHandle();

    /* Before protected sessions the driver takes the same leading fields without the session */
    Size = Ddi.pfnCalcPrivateVideoDecoderHeapSize(Device.Handle(), &Args);
    DriverHeap.pDrvPrivate = Memory.Allocate(Size);
    if (DriverHeap.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoDecoderHeap(Device.Handle(), &Args, DriverHeap);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoDecoderHeap failed 0x%08lX", hr);
        return hr;
    }
    IsDriverHeapCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_0032_VIDEO_DECODER_HEAP);
}

RxVideoDecoderHeap12::~RxVideoDecoderHeap12()
{
    if (IsDriverHeapCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoDecoderHeap(Device.Handle(), DriverHeap);
    if (Session != nullptr)
        Session->Release();
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoderHeap12::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12VideoDecoderHeap) || riid == __uuidof(ID3D12VideoDecoderHeap1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoDecoderHeap1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Decoders */

HRESULT
RxVideoDecoder12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_DECODER_DESC& Desc,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoDecoder12** Decoder)
{
    RxVideoDecoder12* Created;
    HRESULT hr;

    *Decoder = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoDecoder12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, Session);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Decoder = Created;
    return S_OK;
}

HRESULT
RxVideoDecoder12::Initialize(
    _In_ const D3D12_VIDEO_DECODER_DESC& DecoderDesc,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Device.GetVideo()->Ddi();
    D3D12DDIARG_CREATE_VIDEO_DECODER_0072 Args = {};
    UINT NodeIndex;
    SIZE_T Size;
    HRESULT hr;

    if (!RxDecodeConfigurationIsValid(Device, DecoderDesc.NodeMask, DecoderDesc.Configuration, &NodeIndex))
        return E_INVALIDARG;
    if (ProtectedSession != nullptr && !RxDecodeTakesSession(Device, NodeIndex, DecoderDesc.Configuration))
        return E_INVALIDARG;

    Desc = DecoderDesc;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    hr = Device.TranslateNodeMask(DecoderDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    RxToDdiConfiguration(DecoderDesc.Configuration, &Args.Configuration);
    if (Session != nullptr)
        Args.hDrvProtectedResourceSession = Session->GetDriverHandle();

    Size = Ddi.pfnCalcPrivateVideoDecoderSize(Device.Handle(), &Args);
    DriverDecoder.pDrvPrivate = Memory.Allocate(Size);
    if (DriverDecoder.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoDecoder(Device.Handle(), &Args, DriverDecoder);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoDecoder failed 0x%08lX", hr);
        return hr;
    }
    IsDriverDecoderCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverDecoder.pDrvPrivate, D3D12DDI_HT_0020_VIDEO_DECODER);
}

RxVideoDecoder12::~RxVideoDecoder12()
{
    if (IsDriverDecoderCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoDecoder(Device.Handle(), DriverDecoder);
    if (Session != nullptr)
        Session->Release();
}

HRESULT
STDMETHODCALLTYPE
RxVideoDecoder12::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12Object) || riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12Pageable) ||
        riid == __uuidof(ID3D12VideoDecoder) || riid == __uuidof(ID3D12VideoDecoder1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoDecoder1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* DecodeFrame */

static
BOOLEAN
RxSameConfiguration(
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& First,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Second)
{
    return IsEqualGUID(First.DecodeProfile, Second.DecodeProfile) &&
           First.BitstreamEncryption == Second.BitstreamEncryption && First.InterlaceType == Second.InterlaceType;
}

VOID
STDMETHODCALLTYPE
RxVideoDecodeList12::DecodeFrame(
    _In_ ID3D12VideoDecoder* pDecoder,
    _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
    _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS* pInputArguments)
{
    D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1 Output = {};

    if (!Records())
        return;

    if (pOutputArguments == nullptr || pInputArguments == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    Output.pOutputTexture2D = pOutputArguments->pOutputTexture2D;
    Output.OutputSubresource = pOutputArguments->OutputSubresource;
    Output.ConversionArguments.Enable = pOutputArguments->ConversionArguments.Enable;
    Output.ConversionArguments.pReferenceTexture2D = pOutputArguments->ConversionArguments.pReferenceTexture2D;
    Output.ConversionArguments.ReferenceSubresource = pOutputArguments->ConversionArguments.ReferenceSubresource;
    Output.ConversionArguments.OutputColorSpace = pOutputArguments->ConversionArguments.OutputColorSpace;
    Output.ConversionArguments.DecodeColorSpace = pOutputArguments->ConversionArguments.DecodeColorSpace;
    RecordDecode(pDecoder, Output, *pInputArguments);
}

VOID
STDMETHODCALLTYPE
RxVideoDecodeList12::DecodeFrame1(
    _In_ ID3D12VideoDecoder* pDecoder,
    _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1* pOutputArguments,
    _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS* pInputArguments)
{
    if (!Records())
        return;

    if (pOutputArguments == nullptr || pInputArguments == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    RecordDecode(pDecoder, *pOutputArguments, *pInputArguments);
}

VOID
RxVideoDecodeList12::RecordDecode(
    _In_ ID3D12VideoDecoder* pDecoder,
    _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1& Output,
    _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS& Input)
{
    RxVideoState12& State = Video();
    RxVideoDecoder12* Decoder = RxUnwrap12<RxVideoDecoder12>(pDecoder);
    RxVideoDecoderHeap12* Heap = RxUnwrap12<RxVideoDecoderHeap12>(Input.pHeap);
    const D3D12_VIDEO_DECODE_REFERENCE_FRAMES& References = Input.ReferenceFrames;
    D3D12DDI_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS_0032 DriverInput = {};
    D3D12DDI_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS_0041_1 DriverOutput = {};
    D3D12DDI_VIDEO_DECODE_CONVERSION_ARGUMENTS_0041& Conversion = DriverOutput.ConversionArguments;
    UINT Index;

    static_assert(_countof(Input.FrameArguments) == _countof(DriverInput.FrameArguments));
    static_assert(_countof(Output.Histograms) == _countof(DriverOutput.Histograms));

    /* The decoder, its heap and the buffers a decode reads and writes must all be there */
    if (Decoder == nullptr || Heap == nullptr || RxUnwrap12<RxResource>(Output.pOutputTexture2D) == nullptr ||
        RxUnwrap12<RxResource>(Input.CompressedBitstream.pBuffer) == nullptr ||
        Input.NumFrameArguments > _countof(Input.FrameArguments) ||
        !RxSameConfiguration(Decoder->GetApiDesc().Configuration, Heap->GetApiDesc().Configuration) ||
        (Output.ConversionArguments.Enable && RxUnwrap12<RxResource>(Output.ConversionArguments.pReferenceTexture2D) == nullptr) ||
        (References.NumTexture2Ds != 0 && References.ppTexture2Ds == nullptr))
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* Protected work only mixes with objects of the same session */
    if (!SessionMatches(Decoder->GetSession()) || !SessionMatches(Heap->GetSession()))
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* A heap that needs reference only allocations takes no other kind as a reference */
    if ((Heap->GetConfigurationFlags() & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED) != 0)
    {
        for (Index = 0; Index < References.NumTexture2Ds; Index++)
        {
            RxResource* Reference = RxUnwrap12<RxResource>(References.ppTexture2Ds[Index]);

            if (Reference != nullptr &&
                (Reference->GetApiDesc().Flags & D3D12_RESOURCE_FLAG_VIDEO_DECODE_REFERENCE_ONLY) == 0)
            {
                RX_TRACE(L"DecodeFrame: reference %u is not a reference only allocation", Index);
                Fail(E_INVALIDARG);
                return;
            }
        }
    }

    for (Index = 0; Index < Input.NumFrameArguments; Index++)
    {
        DriverInput.FrameArguments[Index].Type = (D3D12DDI_VIDEO_DECODE_PARAMETER_TYPE_0020)Input.FrameArguments[Index].Type;
        DriverInput.FrameArguments[Index].pData = Input.FrameArguments[Index].pData;
        DriverInput.FrameArguments[Index].Size = Input.FrameArguments[Index].Size;
    }
    DriverInput.NumFrameArguments = Input.NumFrameArguments;

    if (References.NumTexture2Ds != 0)
    {
        DriverInput.ReferenceFrames.hDrvTexture2Ds =
            static_cast<D3D12DDI_HRESOURCE*>(Scratch(References.NumTexture2Ds * sizeof(D3D12DDI_HRESOURCE)));
        DriverInput.ReferenceFrames.hDrvVideoDecoderHeaps = static_cast<D3D12DDI_HVIDEODECODERHEAP_0032*>(
            Scratch(References.NumTexture2Ds * sizeof(D3D12DDI_HVIDEODECODERHEAP_0032)));
        if (DriverInput.ReferenceFrames.hDrvTexture2Ds == nullptr ||
            DriverInput.ReferenceFrames.hDrvVideoDecoderHeaps == nullptr)
        {
            return;
        }

        for (Index = 0; Index < References.NumTexture2Ds; Index++)
        {
            RxVideoDecoderHeap12* Reference =
                References.ppHeaps != nullptr ? RxUnwrap12<RxVideoDecoderHeap12>(References.ppHeaps[Index]) : nullptr;

            DriverInput.ReferenceFrames.hDrvTexture2Ds[Index] = RxDriverResource(References.ppTexture2Ds[Index]);
            if (Reference != nullptr)
                DriverInput.ReferenceFrames.hDrvVideoDecoderHeaps[Index] = Reference->GetDriverHandle();
        }
        DriverInput.ReferenceFrames.pSubresources = References.pSubresources;
        DriverInput.ReferenceFrames.NumTexture2Ds = References.NumTexture2Ds;
    }

    DriverInput.CompressedBitstream.hDrvBuffer = RxDriverResource(Input.CompressedBitstream.pBuffer);
    DriverInput.CompressedBitstream.Offset = Input.CompressedBitstream.Offset;
    DriverInput.CompressedBitstream.Size = Input.CompressedBitstream.Size;
    DriverInput.hDrvVideoDecoderHeap = Heap->GetDriverHandle();

    DriverOutput.hDrvOutputTexture2D = RxDriverResource(Output.pOutputTexture2D);
    DriverOutput.OutputSubresource = Output.OutputSubresource;
    Conversion.Enable = Output.ConversionArguments.Enable;
    Conversion.hDrvReferenceTexture2D = RxDriverResource(Output.ConversionArguments.pReferenceTexture2D);
    Conversion.ReferenceSubresource = Output.ConversionArguments.ReferenceSubresource;
    Conversion.OutputColorSpace = Output.ConversionArguments.OutputColorSpace;
    Conversion.DecodeColorSpace = Output.ConversionArguments.DecodeColorSpace;
    Conversion.OutputWidth = Output.ConversionArguments.OutputWidth;
    Conversion.OutputHeight = Output.ConversionArguments.OutputHeight;
    for (Index = 0; Index < _countof(Output.Histograms); Index++)
    {
        DriverOutput.Histograms[Index].Offset = Output.Histograms[Index].Offset;
        DriverOutput.Histograms[Index].hDrvBuffer = RxDriverResource(Output.Histograms[Index].pBuffer);
    }

    /* Older drivers take the output arguments before the conversion size and histograms were added */
    if (State.GetVersion() >= RxVideo0041b)
    {
        State.DecodeDdi().pfnDecodeFrame(DriverList, Decoder->GetDriverHandle(), &DriverOutput, &DriverInput);
    }
    else if (State.GetVersion() >= RxVideo0041)
    {
        D3D12DDI_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS_0041 Older = {};

        Older.hDrvOutputTexture2D = DriverOutput.hDrvOutputTexture2D;
        Older.OutputSubresource = DriverOutput.OutputSubresource;
        Older.ConversionArguments.Enable = Conversion.Enable;
        Older.ConversionArguments.hDrvReferenceTexture2D = Conversion.hDrvReferenceTexture2D;
        Older.ConversionArguments.ReferenceSubresource = Conversion.ReferenceSubresource;
        Older.ConversionArguments.OutputColorSpace = Conversion.OutputColorSpace;
        Older.ConversionArguments.DecodeColorSpace = Conversion.DecodeColorSpace;
        memcpy(Older.Histograms, DriverOutput.Histograms, sizeof(Older.Histograms));
        reinterpret_cast<PFND3D12DDI_VIDEO_DECODE_FRAME_0041>(State.DecodeDdi().pfnDecodeFrame)(
            DriverList, Decoder->GetDriverHandle(), &Older, &DriverInput);
    }
    else
    {
        D3D12DDI_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS_0021 Oldest = {};

        Oldest.hDrvOutputTexture2D = DriverOutput.hDrvOutputTexture2D;
        Oldest.OutputSubresource = DriverOutput.OutputSubresource;
        Oldest.ConversionArguments.Enable = Conversion.Enable;
        Oldest.ConversionArguments.hDrvReferenceTexture2D = Conversion.hDrvReferenceTexture2D;
        Oldest.ConversionArguments.ReferenceSubresource = Conversion.ReferenceSubresource;
        Oldest.ConversionArguments.OutputColorSpace = Conversion.OutputColorSpace;
        Oldest.ConversionArguments.DecodeColorSpace = Conversion.DecodeColorSpace;
        reinterpret_cast<PFND3D12DDI_VIDEO_DECODE_FRAME_0032>(State.DecodeDdi().pfnDecodeFrame)(
            DriverList, Decoder->GetDriverHandle(), &Oldest, &DriverInput);
    }
}
