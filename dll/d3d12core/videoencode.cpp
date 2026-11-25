/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video encode: encoders, encoder heaps, motion estimation and the encode list commands
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"
#include "commandlist_internal.hpp"

/* Blocks the encode arguments share with the driver word for word */
static_assert(sizeof(D3D12_VIDEO_ENCODER_PROFILE_DESC) == sizeof(D3D12DDI_VIDEO_ENCODER_PROFILE_DESC_0080_2));
static_assert(sizeof(D3D12_VIDEO_ENCODER_LEVEL_SETTING) == sizeof(D3D12DDI_VIDEO_ENCODER_LEVEL_SETTING_0080_2));
static_assert(sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION) == sizeof(D3D12DDI_VIDEO_ENCODER_CODEC_CONFIGURATION_0082_0));
static_assert(sizeof(D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC) == sizeof(D3D12DDI_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC_0082_0));
static_assert(sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA) ==
              sizeof(D3D12DDI_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_0108_0));
static_assert(sizeof(D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC) == sizeof(D3D12DDI_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC_0080));

/* The single API node a mask names, and whether video encoding runs there */
static
BOOLEAN
RxEncodeNode(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _Out_ UINT* NodeIndex)
{
    const RxVideoNodeCaps12* Caps;
    UINT Index = 0;

    *NodeIndex = 0;
    if ((NodeMask & (NodeMask - 1)) != 0)
        return FALSE;
    while (NodeMask != 0 && (NodeMask & (1u << Index)) == 0)
        Index++;

    Caps = Device.GetVideo()->NodeCaps(Index);
    *NodeIndex = Index;
    return Caps != nullptr && Caps->EncodeSupported;
}

/* Bytes the codec's block of a profile, level or configuration takes */
static
UINT
RxProfileSize(
    _In_ D3D12_VIDEO_ENCODER_CODEC Codec)
{
    switch (Codec)
    {
        case D3D12_VIDEO_ENCODER_CODEC_H264:
            return sizeof(D3D12_VIDEO_ENCODER_PROFILE_H264);
        case D3D12_VIDEO_ENCODER_CODEC_HEVC:
            return sizeof(D3D12_VIDEO_ENCODER_PROFILE_HEVC);
        case D3D12_VIDEO_ENCODER_CODEC_AV1:
            return sizeof(D3D12_VIDEO_ENCODER_AV1_PROFILE);
        default:
            return 0;
    }
}

static
UINT
RxLevelSize(
    _In_ D3D12_VIDEO_ENCODER_CODEC Codec)
{
    switch (Codec)
    {
        case D3D12_VIDEO_ENCODER_CODEC_H264:
            return sizeof(D3D12_VIDEO_ENCODER_LEVELS_H264);
        case D3D12_VIDEO_ENCODER_CODEC_HEVC:
            return sizeof(D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC);
        case D3D12_VIDEO_ENCODER_CODEC_AV1:
            return sizeof(D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS);
        default:
            return 0;
    }
}

static
UINT
RxConfigurationSize(
    _In_ D3D12_VIDEO_ENCODER_CODEC Codec)
{
    switch (Codec)
    {
        case D3D12_VIDEO_ENCODER_CODEC_H264:
            return sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264);
        case D3D12_VIDEO_ENCODER_CODEC_HEVC:
            return sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC);
        case D3D12_VIDEO_ENCODER_CODEC_AV1:
            return sizeof(D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION);
        default:
            return 0;
    }
}

/* A codec block given by size and pointer, copied out to a caller's block of the same size */
static
HRESULT
RxCopyCodecBlock(
    _In_ UINT Size,
    _In_reads_bytes_(Size) const VOID* Source,
    _In_ UINT DestinationSize,
    _Out_writes_bytes_opt_(DestinationSize) VOID* Destination)
{
    if (Destination == nullptr || DestinationSize != Size)
        return E_INVALIDARG;

    memcpy(Destination, Source, Size);
    return S_OK;
}

static
VOID
RxFindNodeOnly(
    _In_ UINT NodeMask,
    _Out_ UINT* NodeIndex)
{
    UINT Index = 0;

    while (NodeMask != 0 && (NodeMask & (1u << Index)) == 0)
        Index++;
    *NodeIndex = Index;
}

/* Motion estimators */

/* The format, block size, precision and size must be ones the driver estimates motion on */
static
BOOLEAN
RxEstimationIsSupported(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_ DXGI_FORMAT Format,
    _In_ D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE BlockSize,
    _In_ D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION Precision,
    _In_ const D3D12_VIDEO_SIZE_RANGE& SizeRange)
{
    D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR Support = {};
    HRESULT hr;

    if ((NodeMask & (NodeMask - 1)) != 0 || (UINT)BlockSize >= 32 || (UINT)Precision >= 32)
        return FALSE;

    RxFindNodeOnly(NodeMask, &Support.NodeIndex);
    Support.InputFormat = Format;
    hr = Device.GetVideo()->Face().CheckFeatureSupport(D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &Support, sizeof(Support));
    return SUCCEEDED(hr) && ((UINT)Support.BlockSizeFlags & (1u << BlockSize)) != 0 &&
           ((UINT)Support.PrecisionFlags & (1u << Precision)) != 0 && SizeRange.MaxWidth <= Support.SizeRange.MaxWidth &&
           SizeRange.MaxHeight <= Support.SizeRange.MaxHeight && SizeRange.MinWidth >= Support.SizeRange.MinWidth &&
           SizeRange.MinHeight >= Support.SizeRange.MinHeight;
}

HRESULT
RxVideoMotionEstimator12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC& Desc,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoMotionEstimator12** Estimator)
{
    RxVideoMotionEstimator12* Created;
    HRESULT hr;

    *Estimator = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;
    if (Device.GetVideo()->GetVersion() < RxVideoMotionMinimum)
        return E_INVALIDARG;

    Created = new (std::nothrow) RxVideoMotionEstimator12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, Session);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Estimator = Created;
    return S_OK;
}

HRESULT
RxVideoMotionEstimator12::Initialize(
    _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC& EstimatorDesc,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Device.GetVideo()->Ddi();
    D3D12DDIARG_CREATE_VIDEO_MOTION_ESTIMATOR_0060 Args = {};
    SIZE_T Size;
    HRESULT hr;

    if (!RxEstimationIsSupported(Device, EstimatorDesc.NodeMask, EstimatorDesc.InputFormat, EstimatorDesc.BlockSize,
                                 EstimatorDesc.Precision, EstimatorDesc.SizeRange))
    {
        return E_INVALIDARG;
    }

    Desc = EstimatorDesc;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    hr = Device.TranslateNodeMask(EstimatorDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    Args.InputFormat = EstimatorDesc.InputFormat;
    Args.BlockSize = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053)EstimatorDesc.BlockSize;
    Args.Precision = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_0053)EstimatorDesc.Precision;
    memcpy(&Args.SizeRange, &EstimatorDesc.SizeRange, sizeof(Args.SizeRange));
    if (Session != nullptr)
        Args.hDrvProtectedResourceSession = Session->GetDriverHandle();

    Size = Ddi.pfnCalcPrivateVideoMotionEstimatorSize(Device.Handle(), &Args);
    DriverEstimator.pDrvPrivate = Memory.Allocate(Size);
    if (DriverEstimator.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoMotionEstimator(Device.Handle(), &Args, DriverEstimator);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoMotionEstimator failed 0x%08lX", hr);
        return hr;
    }
    IsDriverEstimatorCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverEstimator.pDrvPrivate, D3D12DDI_HT_0053_VIDEO_MOTION_ESTIMATOR);
}

RxVideoMotionEstimator12::~RxVideoMotionEstimator12()
{
    if (IsDriverEstimatorCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoMotionEstimator(Device.Handle(), DriverEstimator);
    if (Session != nullptr)
        Session->Release();
}

HRESULT
STDMETHODCALLTYPE
RxVideoMotionEstimator12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoMotionEstimator) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoMotionEstimator*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Motion vector heaps */

HRESULT
RxVideoMotionVectorHeap12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC& Desc,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoMotionVectorHeap12** Heap)
{
    RxVideoMotionVectorHeap12* Created;
    HRESULT hr;

    *Heap = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;
    if (Device.GetVideo()->GetVersion() < RxVideoMotionMinimum)
        return E_INVALIDARG;

    Created = new (std::nothrow) RxVideoMotionVectorHeap12(Device);
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
RxVideoMotionVectorHeap12::Initialize(
    _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC& HeapDesc,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Device.GetVideo()->Ddi();
    D3D12DDIARG_CREATE_VIDEO_MOTION_VECTOR_HEAP_0060 Args = {};
    SIZE_T Size;
    HRESULT hr;

    if (!RxEstimationIsSupported(Device, HeapDesc.NodeMask, HeapDesc.InputFormat, HeapDesc.BlockSize, HeapDesc.Precision,
                                 HeapDesc.SizeRange))
    {
        return E_INVALIDARG;
    }

    Desc = HeapDesc;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    hr = Device.TranslateNodeMask(HeapDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    Args.InputFormat = HeapDesc.InputFormat;
    Args.BlockSize = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053)HeapDesc.BlockSize;
    Args.Precision = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_0053)HeapDesc.Precision;
    memcpy(&Args.SizeRange, &HeapDesc.SizeRange, sizeof(Args.SizeRange));
    if (Session != nullptr)
        Args.hDrvProtectedResourceSession = Session->GetDriverHandle();

    Size = Ddi.pfnCalcPrivateVideoMotionVectorHeapSize(Device.Handle(), &Args);
    DriverHeap.pDrvPrivate = Memory.Allocate(Size);
    if (DriverHeap.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoMotionVectorHeap(Device.Handle(), &Args, DriverHeap);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoMotionVectorHeap failed 0x%08lX", hr);
        return hr;
    }
    IsDriverHeapCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_0053_VIDEO_MOTION_VECTOR_HEAP);
}

RxVideoMotionVectorHeap12::~RxVideoMotionVectorHeap12()
{
    if (IsDriverHeapCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoMotionVectorHeap(Device.Handle(), DriverHeap);
    if (Session != nullptr)
        Session->Release();
}

HRESULT
STDMETHODCALLTYPE
RxVideoMotionVectorHeap12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoMotionVectorHeap) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoMotionVectorHeap*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Extension commands */

HRESULT
RxVideoExtensionCommand12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC& Desc,
    _In_reads_bytes_(ParametersSize) const VOID* Parameters,
    _In_ SIZE_T ParametersSize,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoExtensionCommand12** Command)
{
    RxVideoExtensionCommand12* Created;
    HRESULT hr;

    *Command = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoExtensionCommand12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc, Parameters, ParametersSize, Session);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Command = Created;
    return S_OK;
}

HRESULT
RxVideoExtensionCommand12::Initialize(
    _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC& CommandDesc,
    _In_reads_bytes_(ParametersSize) const VOID* Parameters,
    _In_ SIZE_T ParametersSize,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    RxVideoState12& Video = *Device.GetVideo();
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Video.Ddi();
    const RxVideoExtension12* Known = Video.FindExtension(CommandDesc.CommandId);
    D3D12DDIARG_CREATE_VIDEO_EXTENSION_COMMAND_0063 Args = {};
    UINT Flags = 0;
    SIZE_T Size;
    HRESULT hr;

    /* Only commands the driver listed are made */
    if (Known == nullptr || Ddi.pfnCreateVideoExtensionCommand == nullptr ||
        (CommandDesc.NodeMask & (CommandDesc.NodeMask - 1)) != 0)
    {
        return E_INVALIDARG;
    }

    if ((Known->Info.CommandListSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_DECODE) != 0)
        Flags |= D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE;
    if ((Known->Info.CommandListSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_PROCESS) != 0)
        Flags |= D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS;
    if ((Known->Info.CommandListSupportFlags & D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_ENCODE) != 0)
        Flags |= D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE;
    QueueFlags = (D3D12DDI_COMMAND_QUEUE_FLAGS)Flags;

    Desc = CommandDesc;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    hr = Device.TranslateNodeMask(CommandDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    Args.CommandId = CommandDesc.CommandId;
    Args.pCreationParameters = Parameters;
    Args.CreationParametersDataSizeInBytes = ParametersSize;
    if (Session != nullptr)
        Args.hDrvProtectedResourceSession = Session->GetDriverHandle();

    Size = Ddi.pfnCalcPrivateVideoExtensionCommandSize(Device.Handle(), &Args);
    DriverCommand.pDrvPrivate = Memory.Allocate(Size);
    if (DriverCommand.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoExtensionCommand(Device.Handle(), &Args, DriverCommand);
    if (FAILED(hr))
        return hr;
    IsDriverCommandCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverCommand.pDrvPrivate, D3D12DDI_HT_0061_VIDEO_EXTENSION_COMMAND);
}

RxVideoExtensionCommand12::~RxVideoExtensionCommand12()
{
    if (IsDriverCommandCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoExtensionCommand(Device.Handle(), DriverCommand);
    if (Session != nullptr)
        Session->Release();
}

HRESULT
STDMETHODCALLTYPE
RxVideoExtensionCommand12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoExtensionCommand) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoExtensionCommand*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Encoders */

HRESULT
RxVideoEncoder12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_ENCODER_DESC& Desc,
    _Outptr_ RxVideoEncoder12** Encoder)
{
    RxVideoEncoder12* Created;
    HRESULT hr;

    *Encoder = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoEncoder12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Encoder = Created;
    return S_OK;
}

HRESULT
RxVideoEncoder12::Initialize(
    _In_ const D3D12_VIDEO_ENCODER_DESC& EncoderDesc)
{
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Device.GetVideo()->Ddi();
    D3D12DDIARG_CREATE_VIDEO_ENCODER_0082_0 Args = {};
    UINT NodeIndex;
    SIZE_T Size;
    HRESULT hr;

    /* The codec's own blocks come with the size of that codec's block */
    if (!RxEncodeNode(Device, EncoderDesc.NodeMask, &NodeIndex) || RxProfileSize(EncoderDesc.EncodeCodec) == 0 ||
        EncoderDesc.EncodeProfile.DataSize != RxProfileSize(EncoderDesc.EncodeCodec) ||
        EncoderDesc.EncodeProfile.pH264Profile == nullptr ||
        EncoderDesc.CodecConfiguration.DataSize != RxConfigurationSize(EncoderDesc.EncodeCodec) ||
        EncoderDesc.CodecConfiguration.pH264Config == nullptr)
    {
        return E_INVALIDARG;
    }

    Desc = EncoderDesc;
    memcpy(&Profile, EncoderDesc.EncodeProfile.pH264Profile, EncoderDesc.EncodeProfile.DataSize);
    memcpy(&Configuration, EncoderDesc.CodecConfiguration.pH264Config, EncoderDesc.CodecConfiguration.DataSize);
    Desc.EncodeProfile.pH264Profile = &Profile.H264;
    Desc.CodecConfiguration.pH264Config = &Configuration.H264;

    hr = Device.TranslateNodeMask(EncoderDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    Args.Flags = (D3D12DDI_VIDEO_ENCODER_FLAGS_0080)Desc.Flags;
    Args.EncodeCodec = (D3D12DDI_VIDEO_ENCODER_CODEC_0080)Desc.EncodeCodec;
    memcpy(&Args.EncodeProfile, &Desc.EncodeProfile, sizeof(Args.EncodeProfile));
    Args.InputFormat = Desc.InputFormat;
    memcpy(&Args.CodecConfiguration, &Desc.CodecConfiguration, sizeof(Args.CodecConfiguration));
    Args.MaxMotionEstimationPrecision = (D3D12DDI_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_0080)
                                            Desc.MaxMotionEstimationPrecision;

    Size = Ddi.pfnCalcPrivateVideoEncoderSize(Device.Handle(), &Args);
    DriverEncoder.pDrvPrivate = Memory.Allocate(Size);
    if (DriverEncoder.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoEncoder(Device.Handle(), &Args, DriverEncoder);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoEncoder failed 0x%08lX", hr);
        return hr;
    }
    IsDriverEncoderCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverEncoder.pDrvPrivate, D3D12DDI_HT_0080_VIDEO_ENCODER);
}

RxVideoEncoder12::~RxVideoEncoder12()
{
    if (IsDriverEncoderCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoEncoder(Device.Handle(), DriverEncoder);
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoder12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoEncoder) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoEncoder*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoder12::GetCodecProfile(
    _Inout_ D3D12_VIDEO_ENCODER_PROFILE_DESC dstProfile)
{
    return RxCopyCodecBlock(Desc.EncodeProfile.DataSize, &Profile, dstProfile.DataSize, dstProfile.pH264Profile);
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoder12::GetCodecConfiguration(
    _Inout_ D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION dstCodecConfig)
{
    return RxCopyCodecBlock(Desc.CodecConfiguration.DataSize, &Configuration, dstCodecConfig.DataSize,
                            dstCodecConfig.pH264Config);
}

/* Encoder heaps */

HRESULT
RxVideoEncoderHeap12::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1& Desc,
    _Outptr_ RxVideoEncoderHeap12** Heap)
{
    RxVideoEncoderHeap12* Created;
    HRESULT hr;

    *Heap = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoEncoderHeap12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(Desc);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Heap = Created;
    return S_OK;
}

HRESULT
RxVideoEncoderHeap12::Initialize(
    _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1& HeapDesc)
{
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Device.GetVideo()->Ddi();
    D3D12DDIARG_CREATE_VIDEO_ENCODER_HEAP_0110_0 Args = {};
    UINT NodeIndex;
    SIZE_T Size;
    HRESULT hr;

    if (!RxEncodeNode(Device, HeapDesc.NodeMask, &NodeIndex) || RxProfileSize(HeapDesc.EncodeCodec) == 0 ||
        HeapDesc.EncodeProfile.DataSize != RxProfileSize(HeapDesc.EncodeCodec) ||
        HeapDesc.EncodeProfile.pH264Profile == nullptr ||
        HeapDesc.EncodeLevel.DataSize != RxLevelSize(HeapDesc.EncodeCodec) ||
        HeapDesc.EncodeLevel.pH264LevelSetting == nullptr || HeapDesc.ResolutionsListCount == 0 ||
        HeapDesc.pResolutionList == nullptr)
    {
        return E_INVALIDARG;
    }

    Resolutions = new (std::nothrow) D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC[HeapDesc.ResolutionsListCount];
    if (Resolutions == nullptr)
        return E_OUTOFMEMORY;

    Desc.NodeMask = HeapDesc.NodeMask;
    Desc.Flags = HeapDesc.Flags;
    Desc.EncodeCodec = HeapDesc.EncodeCodec;
    Desc.EncodeProfile = HeapDesc.EncodeProfile;
    Desc.EncodeLevel = HeapDesc.EncodeLevel;
    Desc.ResolutionsListCount = HeapDesc.ResolutionsListCount;
    DownscaleFactor = HeapDesc.Pow2DownscaleFactor;
    memcpy(&Profile, HeapDesc.EncodeProfile.pH264Profile, HeapDesc.EncodeProfile.DataSize);
    memcpy(&Level, HeapDesc.EncodeLevel.pH264LevelSetting, HeapDesc.EncodeLevel.DataSize);
    memcpy(Resolutions, HeapDesc.pResolutionList, HeapDesc.ResolutionsListCount * sizeof(*Resolutions));
    Desc.EncodeProfile.pH264Profile = &Profile.H264;
    Desc.EncodeLevel.pH264LevelSetting = &Level.H264;
    Desc.pResolutionList = Resolutions;

    hr = Device.TranslateNodeMask(HeapDesc.NodeMask, TRUE, &Args.NodeMask);
    if (FAILED(hr))
        return hr;
    Args.Flags = (D3D12DDI_VIDEO_ENCODER_HEAP_FLAGS_0080)Desc.Flags;
    Args.EncodeCodec = (D3D12DDI_VIDEO_ENCODER_CODEC_0080)Desc.EncodeCodec;
    memcpy(&Args.EncodeProfile, &Desc.EncodeProfile, sizeof(Args.EncodeProfile));
    memcpy(&Args.EncodeLevel, &Desc.EncodeLevel, sizeof(Args.EncodeLevel));
    Args.ResolutionsListCount = Desc.ResolutionsListCount;
    Args.pResolutionList = reinterpret_cast<const D3D12DDI_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC_0080*>(Resolutions);
    Args.Pow2DownscaleFactor = DownscaleFactor;

    Size = Ddi.pfnCalcPrivateVideoEncoderHeapSize(Device.Handle(), &Args);
    DriverHeap.pDrvPrivate = Memory.Allocate(Size);
    if (DriverHeap.pDrvPrivate == nullptr)
        return E_OUTOFMEMORY;

    hr = Ddi.pfnCreateVideoEncoderHeap(Device.Handle(), &Args, DriverHeap);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoEncoderHeap failed 0x%08lX", hr);
        return hr;
    }
    IsDriverHeapCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverHeap.pDrvPrivate, D3D12DDI_HT_0080_VIDEO_ENCODER_HEAP);
}

RxVideoEncoderHeap12::~RxVideoEncoderHeap12()
{
    if (IsDriverHeapCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoEncoderHeap(Device.Handle(), DriverHeap);
    delete[] Resolutions;
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoderHeap12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoEncoderHeap) || riid == __uuidof(ID3D12VideoEncoderHeap1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoEncoderHeap1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoderHeap12::GetCodecProfile(
    _Inout_ D3D12_VIDEO_ENCODER_PROFILE_DESC dstProfile)
{
    return RxCopyCodecBlock(Desc.EncodeProfile.DataSize, &Profile, dstProfile.DataSize, dstProfile.pH264Profile);
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoderHeap12::GetCodecLevel(
    _Inout_ D3D12_VIDEO_ENCODER_LEVEL_SETTING dstLevel)
{
    return RxCopyCodecBlock(Desc.EncodeLevel.DataSize, &Level, dstLevel.DataSize, dstLevel.pH264LevelSetting);
}

HRESULT
STDMETHODCALLTYPE
RxVideoEncoderHeap12::GetResolutionList(
    const UINT ResolutionsListCount,
    _Out_writes_(ResolutionsListCount) D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC* pResolutionList)
{
    if (ResolutionsListCount != Desc.ResolutionsListCount || pResolutionList == nullptr)
        return E_INVALIDARG;

    memcpy(pResolutionList, Resolutions, ResolutionsListCount * sizeof(*pResolutionList));
    return S_OK;
}

/* Encode list commands */

VOID
STDMETHODCALLTYPE
RxVideoEncodeList12::EstimateMotion(
    _In_ ID3D12VideoMotionEstimator* pMotionEstimator,
    _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT* pOutputArguments,
    _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_INPUT* pInputArguments)
{
    RxVideoMotionEstimator12* Estimator = RxUnwrap12<RxVideoMotionEstimator12>(pMotionEstimator);
    RxVideoMotionVectorHeap12* Output;
    RxVideoMotionVectorHeap12* Hint;
    D3D12DDI_VIDEO_MOTION_ESTIMATOR_OUTPUT_0053 DriverOutput = {};
    D3D12DDI_VIDEO_MOTION_ESTIMATOR_INPUT_0053 DriverInput = {};

    if (!Records())
        return;

    if (Estimator == nullptr || pOutputArguments == nullptr || pInputArguments == nullptr ||
        Video().EncodeDdi().pfnEstimateMotion == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    Output = RxUnwrap12<RxVideoMotionVectorHeap12>(pOutputArguments->pMotionVectorHeap);
    Hint = RxUnwrap12<RxVideoMotionVectorHeap12>(pInputArguments->pHintMotionVectorHeap);
    if (Output == nullptr || RxUnwrap12<RxResource>(pInputArguments->pInputTexture2D) == nullptr ||
        RxUnwrap12<RxResource>(pInputArguments->pReferenceTexture2D) == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverOutput.hDrvMotionVectorHeap = Output->GetDriverHandle();
    DriverInput.hDrvInputTexture2D = RxDriverResource(pInputArguments->pInputTexture2D);
    DriverInput.InputSubresourceIndex = pInputArguments->InputSubresourceIndex;
    DriverInput.hDrvReferenceTexture2D = RxDriverResource(pInputArguments->pReferenceTexture2D);
    DriverInput.ReferenceSubresourceIndex = pInputArguments->ReferenceSubresourceIndex;
    if (Hint != nullptr)
        DriverInput.hDrvPreviousMotionVectorHeap = Hint->GetDriverHandle();

    Video().EncodeDdi().pfnEstimateMotion(DriverList, Estimator->GetDriverHandle(), &DriverOutput, &DriverInput);
}

VOID
STDMETHODCALLTYPE
RxVideoEncodeList12::ResolveMotionVectorHeap(
    const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT* pOutputArguments,
    const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT* pInputArguments)
{
    RxVideoMotionVectorHeap12* Heap;
    D3D12DDI_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT_0060 DriverOutput = {};
    D3D12DDI_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT_0053 DriverInput = {};

    if (!Records())
        return;

    if (pOutputArguments == nullptr || pInputArguments == nullptr ||
        Video().EncodeDdi().pfnResolveVideoMotionVectorHeap == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    Heap = RxUnwrap12<RxVideoMotionVectorHeap12>(pInputArguments->pMotionVectorHeap);
    if (Heap == nullptr || RxUnwrap12<RxResource>(pOutputArguments->pMotionVectorTexture2D) == nullptr)
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverOutput.hDrvMotionVectorTexture2D = RxDriverResource(pOutputArguments->pMotionVectorTexture2D);
    DriverOutput.MotionVectorCoordinate.X = pOutputArguments->MotionVectorCoordinate.X;
    DriverOutput.MotionVectorCoordinate.Y = pOutputArguments->MotionVectorCoordinate.Y;
    DriverOutput.MotionVectorCoordinate.Z = pOutputArguments->MotionVectorCoordinate.Z;
    DriverOutput.MotionVectorCoordinate.SubresourceIndex = pOutputArguments->MotionVectorCoordinate.SubresourceIndex;
    DriverInput.hDrvMotionVectorHeap = Heap->GetDriverHandle();
    DriverInput.PixelWidth = pInputArguments->PixelWidth;
    DriverInput.PixelHeight = pInputArguments->PixelHeight;

    Video().EncodeDdi().pfnResolveVideoMotionVectorHeap(DriverList, &DriverOutput, &DriverInput);
}

VOID
STDMETHODCALLTYPE
RxVideoEncodeList12::EncodeFrame(
    _In_ ID3D12VideoEncoder* pEncoder,
    _In_ ID3D12VideoEncoderHeap* pHeap,
    _In_ const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS* pInputArguments,
    _In_ const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS* pOutputArguments)
{
    RxVideoEncoder12* Encoder = RxUnwrap12<RxVideoEncoder12>(pEncoder);
    RxVideoEncoderHeap12* Heap = RxUnwrap12<RxVideoEncoderHeap12>(pHeap);
    D3D12DDI_VIDEO_ENCODER_ENCODEFRAME_INPUT_STREAM_ARGUMENTS_0103_0 DriverInput = {};
    D3D12DDI_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_STREAM_ARGUMENTS_0102_0 DriverOutput = {};
    D3D12DDI_VIDEO_ENCODER_PICTURE_CONTROL_DESC_0103_0& Picture = DriverInput.PictureControlDesc;
    const D3D12_VIDEO_ENCODE_REFERENCE_FRAMES* References;
    UINT Index;

    if (!Records())
        return;

    if (Encoder == nullptr || Heap == nullptr || pInputArguments == nullptr || pOutputArguments == nullptr ||
        Video().EncodeDdi().pfnEncodeFrame == nullptr || Video().GetVersion() < RxVideoEncodeMinimum)
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* The encoder and heap are of one codec, and the frame, bitstream and metadata buffers are all given */
    References = &pInputArguments->PictureControlDesc.ReferenceFrames;
    if (Encoder->GetCodec() != Heap->GetCodec() || RxUnwrap12<RxResource>(pInputArguments->pInputFrame) == nullptr ||
        RxUnwrap12<RxResource>(pOutputArguments->Bitstream.pBuffer) == nullptr ||
        RxUnwrap12<RxResource>(pOutputArguments->EncoderOutputMetadata.pBuffer) == nullptr ||
        (References->NumTexture2Ds != 0 && References->ppTexture2Ds == nullptr))
    {
        Fail(E_INVALIDARG);
        return;
    }

    memcpy(&DriverInput.SequenceControlDesc, &pInputArguments->SequenceControlDesc, sizeof(DriverInput.SequenceControlDesc));
    Picture.IntraRefreshFrameIndex = pInputArguments->PictureControlDesc.IntraRefreshFrameIndex;
    Picture.Flags = (D3D12DDI_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS_0080)pInputArguments->PictureControlDesc.Flags;
    memcpy(&Picture.PictureControlCodecData, &pInputArguments->PictureControlDesc.PictureControlCodecData,
           sizeof(Picture.PictureControlCodecData));

    if (References->NumTexture2Ds != 0)
    {
        Picture.ReferenceFrames.hDrvTexture2Ds =
            static_cast<D3D12DDI_HRESOURCE*>(Scratch(References->NumTexture2Ds * sizeof(D3D12DDI_HRESOURCE)));
        if (Picture.ReferenceFrames.hDrvTexture2Ds == nullptr)
            return;
        for (Index = 0; Index < References->NumTexture2Ds; Index++)
            Picture.ReferenceFrames.hDrvTexture2Ds[Index] = RxDriverResource(References->ppTexture2Ds[Index]);
        Picture.ReferenceFrames.NumTexture2Ds = References->NumTexture2Ds;
        Picture.ReferenceFrames.pSubresources = References->pSubresources;
    }

    DriverInput.hDrvInputTexture2D = RxDriverResource(pInputArguments->pInputFrame);
    DriverInput.InputFrameSubresource = pInputArguments->InputFrameSubresource;
    DriverInput.CurrentFrameBitstreamMetadataSize = pInputArguments->CurrentFrameBitstreamMetadataSize;

    DriverOutput.CompressedBitstream.FrameOutputBuffer.hDrvBuffer = RxDriverResource(pOutputArguments->Bitstream.pBuffer);
    DriverOutput.CompressedBitstream.FrameOutputBuffer.Offset = pOutputArguments->Bitstream.FrameStartOffset;
    DriverOutput.ReconstructedPicture.hDrvTexture2D =
        RxDriverResource(pOutputArguments->ReconstructedPicture.pReconstructedPicture);
    DriverOutput.ReconstructedPicture.InputFrameSubresource =
        pOutputArguments->ReconstructedPicture.ReconstructedPictureSubresource;
    DriverOutput.HWLayoutEncoderMetadataDrvBuffer.hDrvBuffer =
        RxDriverResource(pOutputArguments->EncoderOutputMetadata.pBuffer);
    DriverOutput.HWLayoutEncoderMetadataDrvBuffer.Offset = pOutputArguments->EncoderOutputMetadata.Offset;

    Video().EncodeDdi().pfnEncodeFrame(DriverList, Encoder->GetDriverHandle(), Heap->GetDriverHandle(), &DriverInput,
                                       &DriverOutput);
}

VOID
STDMETHODCALLTYPE
RxVideoEncodeList12::ResolveEncoderOutputMetadata(
    _In_ const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS* pInputArguments,
    _In_ const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS* pOutputArguments)
{
    D3D12DDI_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS_0102_0 DriverInput = {};
    D3D12DDI_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS_0102_0 DriverOutput = {};

    if (!Records())
        return;

    if (pInputArguments == nullptr || pOutputArguments == nullptr ||
        Video().EncodeDdi().pfnResolveEncoderOutputMetadata == nullptr ||
        RxUnwrap12<RxResource>(pInputArguments->HWLayoutMetadata.pBuffer) == nullptr ||
        RxUnwrap12<RxResource>(pOutputArguments->ResolvedLayoutMetadata.pBuffer) == nullptr ||
        RxProfileSize(pInputArguments->EncoderCodec) == 0)
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverInput.EncoderCodec = (D3D12DDI_VIDEO_ENCODER_CODEC_0080)pInputArguments->EncoderCodec;
    memcpy(&DriverInput.EncoderProfile, &pInputArguments->EncoderProfile, sizeof(DriverInput.EncoderProfile));
    DriverInput.EncoderInputFormat = pInputArguments->EncoderInputFormat;
    memcpy(&DriverInput.EncodedPictureEffectiveResolution, &pInputArguments->EncodedPictureEffectiveResolution,
           sizeof(DriverInput.EncodedPictureEffectiveResolution));
    DriverInput.HWLayoutEncoderMetadataDrvBuffer.hDrvBuffer = RxDriverResource(pInputArguments->HWLayoutMetadata.pBuffer);
    DriverInput.HWLayoutEncoderMetadataDrvBuffer.Offset = pInputArguments->HWLayoutMetadata.Offset;
    DriverOutput.ResolvedLayoutEncoderMetadataDrvBuffer.hDrvBuffer =
        RxDriverResource(pOutputArguments->ResolvedLayoutMetadata.pBuffer);
    DriverOutput.ResolvedLayoutEncoderMetadataDrvBuffer.Offset = pOutputArguments->ResolvedLayoutMetadata.Offset;

    Video().EncodeDdi().pfnResolveEncoderOutputMetadata(DriverList, &DriverInput, &DriverOutput);
}
