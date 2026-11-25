/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The video side of the device: turning video on, its tables and the ID3D12VideoDevice face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"

/* Bytes of each video table the driver fills at a feature version */
static
SIZE_T
RxDeviceTableSize(
    _In_ UINT Version)
{
    if (Version < RxVideo0053)
        return offsetof(D3D12DDI_DEVICE_FUNCS_VIDEO_0110, pfnCalcPrivateVideoMotionEstimatorSize);
    if (Version < RxVideo0063)
        return offsetof(D3D12DDI_DEVICE_FUNCS_VIDEO_0110, pfnCalcPrivateVideoExtensionCommandSize);
    if (Version < RxVideo0076)
        return offsetof(D3D12DDI_DEVICE_FUNCS_VIDEO_0110, pfnExecuteDeviceVideoExtensionCommand);
    if (Version < RxVideo0080)
        return offsetof(D3D12DDI_DEVICE_FUNCS_VIDEO_0110, pfnCalcPrivateVideoEncoderSize);
    return sizeof(D3D12DDI_DEVICE_FUNCS_VIDEO_0110);
}

template <typename Table>
static
SIZE_T
RxFrameTableSize(
    _In_ UINT Version)
{
    if (Version < RxVideo0063)
        return offsetof(Table, pfnInitializeVideoExtensionCommand);
    if (Version < RxVideo0088)
        return offsetof(Table, pfnBarrier);
    return sizeof(Table);
}

static
SIZE_T
RxEncodeTableSize(
    _In_ UINT Version)
{
    if (Version < RxVideo0063)
        return offsetof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103, pfnInitializeVideoExtensionCommand);
    if (Version < RxVideo0080)
        return offsetof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103, pfnResolveEncoderOutputMetadata);
    if (Version < RxVideo0088)
        return offsetof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103, pfnBarrier);
    if (Version < RxVideo0103)
        return offsetof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103, pfnResolveInputParamLayout);
    return sizeof(D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103);
}

/* The entries every kind's list table shares; the barrier entry only once it takes the enhanced form */
template <typename Table>
static
VOID
RxTakeCommonEntries(
    _In_ const Table& Source,
    _In_ UINT Version,
    _Out_ RxVideoListTable12& Common)
{
    Common.pfnCloseCommandList = Source.pfnCloseCommandList;
    Common.pfnResetCommandList = Source.pfnResetCommandList;
    Common.pfnDiscardResource = Source.pfnDiscardResource;
    Common.pfnSetMarker = Source.pfnSetMarker;
    Common.pfnSetPredication = Source.pfnSetPredication;
    Common.pfnBeginQuery = Source.pfnBeginQuery;
    Common.pfnEndQuery = Source.pfnEndQuery;
    Common.pfnResolveQueryData = Source.pfnResolveQueryData;
    Common.pfnResourceBarrier = Source.pfnResourceBarrier;
    Common.pfnSetProtectedResourceSession = Source.pfnSetProtectedResourceSession;
    Common.pfnWriteBufferImmediate = Source.pfnWriteBufferImmediate;
    Common.pfnInitializeExtensionCommand = Source.pfnInitializeVideoExtensionCommand;
    Common.pfnExecuteExtensionCommand = Source.pfnExecuteVideoExtensionCommand;
    Common.pfnBarrier = Version >= RxVideo0094 ? Source.pfnBarrier : nullptr;
}

RxVideoState12::RxVideoState12(
    _In_ RxDevice12& Owner)
    : Device(Owner)
{
}

RxVideoState12::~RxVideoState12()
{
    UINT Index;

    for (Index = 0; Index < NodeCount; Index++)
        delete[] Nodes[Index].Profiles;
    delete[] Nodes;

    for (Index = 0; Index < ExtensionCount; Index++)
        delete[] Extensions[Index].Name;
    delete[] Extensions;

    delete VideoFace;
}

HRESULT
RxVideoState12::Initialize(
    _In_ UINT FeatureVersion)
{
    HRESULT hr;

    Version = FeatureVersion;
    VideoFace = new (std::nothrow) RxVideoDevice12(*this);
    if (VideoFace == nullptr)
        return E_OUTOFMEMORY;

    hr = FillTables();
    if (FAILED(hr))
        return hr;

    hr = CacheCaps();
    if (FAILED(hr))
        return hr;

    return CacheExtensions();
}

HRESULT
RxVideoState12::FillTables()
{
    struct RxTableFill
    {
        D3D12DDI_TABLE_TYPE Type;
        VOID* Table;
        SIZE_T Size;
        VOID* Token;
    };
    const RxTableFill Fills[] =
    {
        { D3D12DDI_TABLE_TYPE_0020_DEVICE_VIDEO, &Functions, RxDeviceTableSize(Version), nullptr },
        { D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_DECODE, &Queues[RxVideoDecode], sizeof(Queues[0]), nullptr },
        { D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_DECODE, &DecodeList,
          RxFrameTableSize<D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0094>(Version), &Common[RxVideoDecode] },
        { D3D12DDI_TABLE_TYPE_0022_COMMAND_QUEUE_VIDEO_PROCESS, &Queues[RxVideoProcess], sizeof(Queues[0]), nullptr },
        { D3D12DDI_TABLE_TYPE_0022_COMMAND_LIST_VIDEO_PROCESS, &ProcessList,
          RxFrameTableSize<D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0106>(Version), &Common[RxVideoProcess] },
        { D3D12DDI_TABLE_TYPE_0053_COMMAND_LIST_VIDEO_ENCODE, &EncodeList, RxEncodeTableSize(Version),
          &Common[RxVideoEncode] },
        { D3D12DDI_TABLE_TYPE_0053_COMMAND_QUEUE_VIDEO_ENCODE, &Queues[RxVideoEncode], sizeof(Queues[0]), nullptr },
    };
    const RxAdapter12& Adapter = Device.GetAdapter();
    UINT Index;
    HRESULT hr;

    /* Encode tables exist from the version that brought motion estimation */
    for (Index = 0; Index < _countof(Fills); Index++)
    {
        D3D12DDI_HRTTABLE Token = {};

        if (Version < RxVideo0053 && (Fills[Index].Type == D3D12DDI_TABLE_TYPE_0053_COMMAND_LIST_VIDEO_ENCODE ||
                                      Fills[Index].Type == D3D12DDI_TABLE_TYPE_0053_COMMAND_QUEUE_VIDEO_ENCODE))
        {
            continue;
        }

        Token.handle = Fills[Index].Token;
        hr = Adapter.GetFunctions().pfnFillDDITable(Adapter.GetDriverHandle(), Fills[Index].Type, Fills[Index].Table,
                                                    Fills[Index].Size, 0, Token);
        if (FAILED(hr))
        {
            RX_TRACE(L"driver did not fill video table %u (0x%08lX)", Fills[Index].Type, hr);
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        }
    }

    RxTakeCommonEntries(DecodeList, Version, Common[RxVideoDecode]);
    RxTakeCommonEntries(ProcessList, Version, Common[RxVideoProcess]);
    RxTakeCommonEntries(EncodeList, Version, Common[RxVideoEncode]);

    if (Functions.pfnGetCaps == nullptr || DecodeList.pfnCloseCommandList == nullptr ||
        ProcessList.pfnCloseCommandList == nullptr)
    {
        RX_TRACE(L"driver video tables are incomplete");
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    return S_OK;
}

HRESULT
RxVideoState12::GetCaps(
    _In_ D3D12DDICAPS_TYPE_VIDEO_0020 Type,
    _Inout_updates_bytes_(Size) VOID* Data,
    _In_ UINT Size) const
{
    D3D12DDIARG_VIDEO_GETCAPS_0020 Args = {};

    Args.Type = Type;
    Args.pData = Data;
    Args.DataSize = Size;
    return Functions.pfnGetCaps(Device.Handle(), &Args);
}

BOOLEAN
RxVideoState12::DriverNode(
    _In_ UINT NodeIndex,
    _Out_ UINT* Driver) const
{
    *Driver = 0;
    if (NodeIndex >= Device.ApiNodeCount)
        return FALSE;

    *Driver = Device.ApiToPhysical[NodeIndex];
    return TRUE;
}

/* What each node decodes and processes, which CheckFeatureSupport answers from without the driver */
HRESULT
RxVideoState12::CacheCaps()
{
    UINT Node;
    HRESULT hr;

    NodeCount = Device.ApiNodeCount;
    Nodes = new (std::nothrow) RxVideoNodeCaps12[NodeCount ? NodeCount : 1]();
    if (Nodes == nullptr)
        return E_OUTOFMEMORY;

    for (Node = 0; Node < NodeCount; Node++)
    {
        D3D12DDI_VIDEO_PROCESS_MAX_INPUT_STREAMS_DATA_0020 Streams = {};
        D3D12DDI_VIDEO_DECODE_PROFILE_COUNT_DATA_0032 Count = {};
        D3D12DDI_VIDEO_DECODE_PROFILES_DATA_0020 Profiles = {};
        RxVideoNodeCaps12& Caps = Nodes[Node];

        Streams.NodeIndex = Device.ApiToPhysical[Node];
        hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_MAX_INPUT_STREAMS, &Streams, sizeof(Streams));
        if (FAILED(hr))
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Caps.MaxInputStreams = Streams.MaxInputStreams;

        /* Encoding is only offered where the driver says the node does it */
        if (Version >= RxVideo0053)
        {
            D3D12DDI_VIDEO_FEATURE_AREA_SUPPORT_DATA_0053 Areas = {};

            Areas.NodeIndex = Device.ApiToPhysical[Node];
            hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0053_FEATURE_AREA_SUPPORT, &Areas, sizeof(Areas));
            if (FAILED(hr))
                return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            Caps.EncodeSupported = Areas.VideoEncodeSupport != FALSE && Version >= RxVideoEncodeMinimum;
        }

        Count.NodeIndex = Device.ApiToPhysical[Node];
        hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODE_PROFILE_COUNT, &Count, sizeof(Count));
        if (FAILED(hr) || Count.ProfileCount > 0x1000)
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        if (Count.ProfileCount == 0)
            continue;

        Caps.Profiles = new (std::nothrow) GUID[Count.ProfileCount];
        if (Caps.Profiles == nullptr)
            return E_OUTOFMEMORY;

        Profiles.NodeIndex = Count.NodeIndex;
        Profiles.ProfileCount = Count.ProfileCount;
        Profiles.pProfiles = Caps.Profiles;
        hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_PROFILES, &Profiles, sizeof(Profiles));
        if (FAILED(hr))
            return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
        Caps.ProfileCount = Count.ProfileCount;
    }

    return S_OK;
}

BOOLEAN
RxVideoState12::HasProfile(
    _In_ UINT NodeIndex,
    _In_ const GUID& Profile) const
{
    UINT Index;

    if (NodeIndex >= NodeCount)
        return FALSE;

    for (Index = 0; Index < Nodes[NodeIndex].ProfileCount; Index++)
    {
        if (IsEqualGUID(Nodes[NodeIndex].Profiles[Index], Profile))
            return TRUE;
    }
    return FALSE;
}

/* The list types an extension command records on, from the queues the driver named */
static
D3D12_COMMAND_LIST_SUPPORT_FLAGS
RxListSupportOf(
    _In_ UINT QueueFlags)
{
    UINT Support = D3D12_COMMAND_LIST_SUPPORT_FLAG_NONE;

    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_DECODE) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_DECODE;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0022_VIDEO_PROCESS) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_PROCESS;
    if ((QueueFlags & D3D12DDI_COMMAND_QUEUE_FLAG_0053_VIDEO_ENCODE) != 0)
        Support |= D3D12_COMMAND_LIST_SUPPORT_FLAG_VIDEO_ENCODE;
    return (D3D12_COMMAND_LIST_SUPPORT_FLAGS)Support;
}

/* The first node's extension commands, kept with their names so they outlive the query */
HRESULT
RxVideoState12::CacheExtensions()
{
    D3D12DDI_VIDEO_EXTENSION_COMMAND_COUNT_DATA_0063 Count = {};
    D3D12DDI_VIDEO_EXTENSION_COMMANDS_DATA_0063 Commands = {};
    D3D12DDI_VIDEO_EXTENSION_COMMAND_INFO_0063* Infos;
    UINT Index;
    HRESULT hr;

    if (Version < RxVideo0063 || NodeCount == 0)
        return S_OK;

    Count.NodeIndex = Device.ApiToPhysical[0];
    hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMAND_COUNT, &Count, sizeof(Count));
    if (FAILED(hr) || Count.CommandCount > 0x1000)
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    if (Count.CommandCount == 0)
        return S_OK;

    Infos = new (std::nothrow) D3D12DDI_VIDEO_EXTENSION_COMMAND_INFO_0063[Count.CommandCount]();
    Extensions = new (std::nothrow) RxVideoExtension12[Count.CommandCount]();
    if (Infos == nullptr || Extensions == nullptr)
    {
        delete[] Infos;
        return E_OUTOFMEMORY;
    }

    Commands.NodeIndex = Count.NodeIndex;
    Commands.CommandCount = Count.CommandCount;
    Commands.pCommandInfos = Infos;
    hr = GetCaps(D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMANDS, &Commands, sizeof(Commands));
    if (FAILED(hr))
    {
        delete[] Infos;
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    for (Index = 0; Index < Count.CommandCount; Index++)
    {
        RxVideoExtension12& Entry = Extensions[ExtensionCount];
        SIZE_T Length = Infos[Index].Name != nullptr ? wcslen(Infos[Index].Name) : 0;

        Entry.Name = new (std::nothrow) WCHAR[Length + 1];
        if (Entry.Name == nullptr)
        {
            delete[] Infos;
            return E_OUTOFMEMORY;
        }
        if (Length != 0)
            memcpy(Entry.Name, Infos[Index].Name, Length * sizeof(WCHAR));
        Entry.Name[Length] = L'\0';

        Entry.Info.CommandId = Infos[Index].CommandId;
        Entry.Info.Name = Entry.Name;
        Entry.Info.CommandListSupportFlags = RxListSupportOf(Infos[Index].CommandQueueFlags);
        ExtensionCount++;
    }

    delete[] Infos;
    return S_OK;
}

_Ret_maybenull_
const RxVideoExtension12*
RxVideoState12::FindExtension(
    _In_ const GUID& CommandId) const
{
    UINT Index;

    for (Index = 0; Index < ExtensionCount; Index++)
    {
        if (IsEqualGUID(Extensions[Index].Info.CommandId, CommandId))
            return &Extensions[Index];
    }
    return nullptr;
}

VOID
RxDestroyVideoState12(
    _In_opt_ RxVideoState12* State)
{
    delete State;
}

/* Video on the device */

BOOLEAN
RxDevice12::EnsureVideoSupported(VOID)
{
    RxVideoState12* Created;
    BOOLEAN Supported;
    HRESULT hr;

    AcquireSRWLockExclusive(&VideoLock);
    if (VideoEnableResult != S_FALSE)
    {
        Supported = Video != nullptr;
        ReleaseSRWLockExclusive(&VideoLock);
        return Supported;
    }

    /* Tried once; a driver below the first version with a video table offers no video */
    VideoEnableResult = S_OK;
    hr = ExtendedFunctions.pfnEnableExtendedFeature(DriverDevice, D3D12DDI_FEATURE_0020_VIDEO, VideoVersion);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver refused to enable video version %u: 0x%08lX", VideoVersion, hr);
        VideoEnableResult = hr;
    }
    else if (VideoVersion >= RxVideo0033)
    {
        Created = new (std::nothrow) RxVideoState12(*this);
        hr = Created != nullptr ? Created->Initialize(VideoVersion) : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
        {
            Video = Created;
        }
        else
        {
            RX_TRACE(L"video setup at version %u failed 0x%08lX", VideoVersion, hr);
            delete Created;
            VideoEnableResult = hr;
        }
    }

    RX_TRACE(L"video version %u %s", VideoVersion, Video != nullptr ? L"on" : L"off");
    Supported = Video != nullptr;
    ReleaseSRWLockExclusive(&VideoLock);
    return Supported;
}

HRESULT
RxDevice12::QueryVideoDevice(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    *ppvObject = nullptr;

    if (riid != __uuidof(ID3D12VideoDevice) && riid != __uuidof(ID3D12VideoDevice1) &&
        riid != __uuidof(ID3D12VideoDevice2) && riid != __uuidof(ID3D12VideoDevice3) &&
        riid != __uuidof(ID3D12VideoDevice4))
    {
        return E_NOINTERFACE;
    }

    if (TakesListType(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE) == FALSE || !EnsureVideoSupported())
        return E_NOINTERFACE;

    *ppvObject = static_cast<ID3D12VideoDevice4*>(&Video->Face());
    AddRef();
    return S_OK;
}

HRESULT
RxDevice12::CreateVideoCommandList(
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_opt_ RxCommandAllocator* Allocator,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppCommandList)
{
    if (!EnsureVideoSupported())
        return E_INVALIDARG;

    return RxCreateVideoList12(*this, Type, Allocator, riid, ppCommandList);
}

RxVideoKind12
RxVideoKindOf(
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    switch (Type)
    {
        case D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS:
            return RxVideoProcess;
        case D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE:
            return RxVideoEncode;
        default:
            return RxVideoDecode;
    }
}

PFND3D12DDI_EXECUTECOMMANDLISTS
RxExecuteEntry12(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type)
{
    RxVideoState12* Video = Device.GetVideo();

    if (Video != nullptr && (Type == D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE || Type == D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS ||
                             Type == D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE))
    {
        if (Video->ExecuteEntry(RxVideoKindOf(Type)) != nullptr)
            return Video->ExecuteEntry(RxVideoKindOf(Type));
    }

    return Device.QueueDdi().pfnExecuteCommandLists;
}

HRESULT
RxVideoSessionOut12(
    _In_opt_ RxProtectedResourceSession* Session,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppProtectedSession)
{
    /* Without a session the output is left as it was */
    if (ppProtectedSession == nullptr)
        return E_INVALIDARG;
    if (Session == nullptr)
        return DXGI_ERROR_NOT_FOUND;

    return Session->QueryInterface(riid, ppProtectedSession);
}

/* ID3D12VideoDevice */

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    return Device.QueryInterface(riid, ppvObject);
}

ULONG
STDMETHODCALLTYPE
RxVideoDevice12::AddRef(VOID)
{
    return Device.AddRef();
}

ULONG
STDMETHODCALLTYPE
RxVideoDevice12::Release(VOID)
{
    return Device.Release();
}

/* Video objects are only made to be handed out; a missing output fails once the desc has passed */
template <typename Object>
static
HRESULT
RxVideoHandOut(
    _In_ Object* Created,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject)
{
    if (ppvObject == nullptr)
    {
        Created->Release();
        return E_UNEXPECTED;
    }

    return RxHandOut12(Created, riid, ppvObject);
}

/* A session handed to a video creation method: none, or one of this runtime's */
static
BOOLEAN
RxTakeSession(
    _In_opt_ ID3D12ProtectedResourceSession* Given,
    _Out_ RxProtectedResourceSession** Session)
{
    *Session = RxUnwrap12<RxProtectedResourceSession>(Given);
    return Given == nullptr || *Session != nullptr;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoDecoder(
    _In_ const D3D12_VIDEO_DECODER_DESC* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoDecoder)
{
    return CreateVideoDecoder1(pDesc, nullptr, riid, ppVideoDecoder);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoDecoder1(
    _In_ const D3D12_VIDEO_DECODER_DESC* pDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoDecoder)
{
    RxProtectedResourceSession* Session;
    RxVideoDecoder12* Decoder;
    HRESULT hr;

    if (ppVideoDecoder != nullptr)
        *ppVideoDecoder = nullptr;
    if (pDesc == nullptr || !RxTakeSession(pProtectedResourceSession, &Session))
        return E_INVALIDARG;

    hr = RxVideoDecoder12::Create(Device, *pDesc, Session, &Decoder);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Decoder, riid, ppVideoDecoder);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoDecoderHeap(
    _In_ const D3D12_VIDEO_DECODER_HEAP_DESC* pVideoDecoderHeapDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoDecoderHeap)
{
    return CreateVideoDecoderHeap1(pVideoDecoderHeapDesc, nullptr, riid, ppVideoDecoderHeap);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoDecoderHeap1(
    _In_ const D3D12_VIDEO_DECODER_HEAP_DESC* pVideoDecoderHeapDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoDecoderHeap)
{
    RxProtectedResourceSession* Session;
    RxVideoDecoderHeap12* Heap;
    HRESULT hr;

    if (ppVideoDecoderHeap != nullptr)
        *ppVideoDecoderHeap = nullptr;
    if (pVideoDecoderHeapDesc == nullptr || !RxTakeSession(pProtectedResourceSession, &Session))
        return E_INVALIDARG;

    hr = RxVideoDecoderHeap12::Create(Device, *pVideoDecoderHeapDesc, Session, &Heap);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Heap, riid, ppVideoDecoderHeap);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoProcessor(
    UINT NodeMask,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC* pOutputStreamDesc,
    UINT NumInputStreamDescs,
    _In_reads_(NumInputStreamDescs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoProcessor)
{
    return CreateVideoProcessor1(NodeMask, pOutputStreamDesc, NumInputStreamDescs, pInputStreamDescs, nullptr, riid,
                                 ppVideoProcessor);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoProcessor1(
    UINT NodeMask,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC* pOutputStreamDesc,
    UINT NumInputStreamDescs,
    _In_reads_(NumInputStreamDescs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoProcessor)
{
    RxProtectedResourceSession* Session;
    RxVideoProcessor12* Processor;
    HRESULT hr;

    if (ppVideoProcessor != nullptr)
        *ppVideoProcessor = nullptr;
    if ((NodeMask & (NodeMask - 1)) != 0)
        return E_INVALIDARG;
    if (ppVideoProcessor == nullptr)
        return E_UNEXPECTED;
    if (pOutputStreamDesc == nullptr || NumInputStreamDescs == 0 || pInputStreamDescs == nullptr ||
        !RxTakeSession(pProtectedResourceSession, &Session))
    {
        return E_INVALIDARG;
    }

    hr = RxVideoProcessor12::Create(Device, NodeMask, *pOutputStreamDesc, NumInputStreamDescs, pInputStreamDescs,
                                    Session, &Processor);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Processor, riid, ppVideoProcessor);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoMotionEstimator(
    _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC* pDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoMotionEstimator)
{
    RxProtectedResourceSession* Session;
    RxVideoMotionEstimator12* Estimator;
    HRESULT hr;

    if (ppVideoMotionEstimator != nullptr)
        *ppVideoMotionEstimator = nullptr;
    if (pDesc == nullptr || !RxTakeSession(pProtectedResourceSession, &Session))
        return E_INVALIDARG;

    hr = RxVideoMotionEstimator12::Create(Device, *pDesc, Session, &Estimator);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Estimator, riid, ppVideoMotionEstimator);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoMotionVectorHeap(
    _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC* pDesc,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoMotionVectorHeap)
{
    RxProtectedResourceSession* Session;
    RxVideoMotionVectorHeap12* Heap;
    HRESULT hr;

    if (ppVideoMotionVectorHeap != nullptr)
        *ppVideoMotionVectorHeap = nullptr;
    if (pDesc == nullptr || !RxTakeSession(pProtectedResourceSession, &Session))
        return E_INVALIDARG;

    hr = RxVideoMotionVectorHeap12::Create(Device, *pDesc, Session, &Heap);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Heap, riid, ppVideoMotionVectorHeap);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoExtensionCommand(
    _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC* pDesc,
    _In_reads_bytes_(CreationParametersDataSizeInBytes) const void* pCreationParameters,
    SIZE_T CreationParametersDataSizeInBytes,
    _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoExtensionCommand)
{
    RxProtectedResourceSession* Session;
    RxVideoExtensionCommand12* Command;
    HRESULT hr;

    if (ppVideoExtensionCommand != nullptr)
        *ppVideoExtensionCommand = nullptr;
    if (pDesc == nullptr || !RxTakeSession(pProtectedResourceSession, &Session))
        return E_INVALIDARG;

    hr = RxVideoExtensionCommand12::Create(Device, *pDesc, pCreationParameters, CreationParametersDataSizeInBytes,
                                           Session, &Command);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Command, riid, ppVideoExtensionCommand);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::ExecuteExtensionCommand(
    _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
    _In_reads_bytes_(ExecutionParametersSizeInBytes) const void* pExecutionParameters,
    SIZE_T ExecutionParametersSizeInBytes,
    _Out_writes_bytes_(OutputDataSizeInBytes) void* pOutputData,
    SIZE_T OutputDataSizeInBytes)
{
    RxVideoExtensionCommand12* Command = RxUnwrap12<RxVideoExtensionCommand12>(pExtensionCommand);
    HRESULT hr;

    if (Command == nullptr || Video.Ddi().pfnExecuteDeviceVideoExtensionCommand == nullptr)
        return E_INVALIDARG;

    hr = Video.Ddi().pfnExecuteDeviceVideoExtensionCommand(Device.Handle(), Command->GetDriverHandle(),
                                                           pExecutionParameters, ExecutionParametersSizeInBytes,
                                                           pOutputData, OutputDataSizeInBytes);
    Device.CheckDriverError();
    return hr;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoEncoder(
    _In_ const D3D12_VIDEO_ENCODER_DESC* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoEncoder)
{
    RxVideoEncoder12* Encoder;
    HRESULT hr;

    if (ppVideoEncoder != nullptr)
        *ppVideoEncoder = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoEncoder12::Create(Device, *pDesc, &Encoder);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Encoder, riid, ppVideoEncoder);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoEncoderHeap(
    _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoEncoderHeap)
{
    D3D12_VIDEO_ENCODER_HEAP_DESC1 Desc = {};

    if (pDesc == nullptr)
    {
        if (ppVideoEncoderHeap != nullptr)
            *ppVideoEncoderHeap = nullptr;
        return E_INVALIDARG;
    }

    /* The first heap desc is the newer one without downscaling */
    Desc.NodeMask = pDesc->NodeMask;
    Desc.Flags = pDesc->Flags;
    Desc.EncodeCodec = pDesc->EncodeCodec;
    Desc.EncodeProfile = pDesc->EncodeProfile;
    Desc.EncodeLevel = pDesc->EncodeLevel;
    Desc.ResolutionsListCount = pDesc->ResolutionsListCount;
    Desc.pResolutionList = pDesc->pResolutionList;
    return CreateVideoEncoderHeap1(&Desc, riid, ppVideoEncoderHeap);
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CreateVideoEncoderHeap1(
    _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1* pDesc,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppVideoEncoderHeap)
{
    RxVideoEncoderHeap12* Heap;
    HRESULT hr;

    if (ppVideoEncoderHeap != nullptr)
        *ppVideoEncoderHeap = nullptr;
    if (pDesc == nullptr)
        return E_INVALIDARG;

    hr = RxVideoEncoderHeap12::Create(Device, *pDesc, &Heap);
    if (FAILED(hr))
        return hr;

    return RxVideoHandOut(Heap, riid, ppVideoEncoderHeap);
}
