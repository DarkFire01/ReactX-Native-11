/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Direct3D 12 video: the video device, its objects and the video command lists
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "objects.hpp"
#include "session.hpp"

#include <d3d12video.h>

/* Video feature versions whose driver interfaces change shape */
constexpr UINT RxVideo0033 = D3D12DDI_FEATURE_VERSION_VIDEO_0033_0;
constexpr UINT RxVideo0041 = D3D12DDI_FEATURE_VERSION_VIDEO_0041_0;
constexpr UINT RxVideo0041b = D3D12DDI_FEATURE_VERSION_VIDEO_0041_1;
constexpr UINT RxVideo0043 = D3D12DDI_FEATURE_VERSION_VIDEO_0043_0;
constexpr UINT RxVideo0053 = D3D12DDI_FEATURE_VERSION_VIDEO_0053_0;
constexpr UINT RxVideo0060 = D3D12DDI_FEATURE_VERSION_VIDEO_0060_0;
constexpr UINT RxVideo0063 = D3D12DDI_FEATURE_VERSION_VIDEO_0063_0;
constexpr UINT RxVideo0072 = D3D12DDI_FEATURE_VERSION_VIDEO_0072_0;
constexpr UINT RxVideo0076 = D3D12DDI_FEATURE_VERSION_VIDEO_0076_0;
constexpr UINT RxVideo0080 = D3D12DDI_FEATURE_VERSION_VIDEO_0080_2;
constexpr UINT RxVideo0088 = D3D12DDI_FEATURE_VERSION_VIDEO_0088_0;
constexpr UINT RxVideo0094 = D3D12DDI_FEATURE_VERSION_VIDEO_0094_0;
constexpr UINT RxVideo0098 = D3D12DDI_FEATURE_VERSION_VIDEO_0098_0;
constexpr UINT RxVideo0103 = D3D12DDI_FEATURE_VERSION_VIDEO_0103_0;
constexpr UINT RxVideo0106 = D3D12DDI_FEATURE_VERSION_VIDEO_0106_0;
constexpr UINT RxVideo0110 = D3D12DDI_FEATURE_VERSION_VIDEO_0110_0;

/* The oldest versions whose encode and motion estimation entries this runtime drives */
constexpr UINT RxVideoEncodeMinimum = RxVideo0110;
constexpr UINT RxVideoMotionMinimum = RxVideo0060;

/* The three kinds of video list, in the order their tables are kept */
enum RxVideoKind12
{
    RxVideoDecode,
    RxVideoProcess,
    RxVideoEncode,
    RxVideoKinds,
};

/* What every video list table has, wherever the kind's table keeps it */
struct RxVideoListTable12
{
    PFND3D12DDI_CLOSECOMMANDLIST pfnCloseCommandList;
    PFND3D12DDI_RESETCOMMANDLIST_0040 pfnResetCommandList;
    PFND3D12DDI_DISCARD_RESOURCE_0003 pfnDiscardResource;
    PFND3D12DDI_SET_MARKER pfnSetMarker;
    PFND3D12DDI_SET_PREDICATION pfnSetPredication;
    PFND3D12DDI_BEGIN_END_QUERY_0003 pfnBeginQuery;
    PFND3D12DDI_BEGIN_END_QUERY_0003 pfnEndQuery;
    PFND3D12DDI_RESOLVE_QUERY_DATA pfnResolveQueryData;
    PFND3D12DDI_RESOURCEBARRIER_0022 pfnResourceBarrier;
    PFND3D12DDI_SETPROTECTEDRESOURCESESSION_0030 pfnSetProtectedResourceSession;
    PFND3D12DDI_WRITEBUFFERIMMEDIATE_0032 pfnWriteBufferImmediate;
    PFND3D12DDI_INITIALIZE_VIDEO_EXTENSION_COMMAND_0063 pfnInitializeExtensionCommand;
    PFND3D12DDI_EXECUTE_VIDEO_EXTENSION_COMMAND_0063 pfnExecuteExtensionCommand;
    PFND3D12DDI_BARRIER_0094 pfnBarrier;
};

/* One extension command the driver offers, as CheckFeatureSupport reports it */
struct RxVideoExtension12
{
    D3D12_VIDEO_EXTENSION_COMMAND_INFO Info;
    WCHAR* Name;
};

/* The decode caps the runtime keeps per node, read once video is turned on */
struct RxVideoNodeCaps12
{
    UINT MaxInputStreams;
    UINT ProfileCount;
    GUID* Profiles;
    BOOLEAN EncodeSupported;
};

class RxVideoDevice12;

/* The device's video side, made the first time video is asked for */
class RxVideoState12
{
public:
    explicit RxVideoState12(
        _In_ RxDevice12& Owner);

    ~RxVideoState12();

    RxVideoState12(const RxVideoState12&) = delete;
    RxVideoState12& operator=(const RxVideoState12&) = delete;

    /* Fills the tables for the chosen version and caches the caps Windows caches */
    HRESULT
    Initialize(
        _In_ UINT FeatureVersion);

    UINT
    GetVersion() const
    {
        return Version;
    }

    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110&
    Ddi() const
    {
        return Functions;
    }

    const D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0094&
    DecodeDdi() const
    {
        return DecodeList;
    }

    const D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0106&
    ProcessDdi() const
    {
        return ProcessList;
    }

    const D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103&
    EncodeDdi() const
    {
        return EncodeList;
    }

    /* The common entries of a kind's list table, which is also the token the driver hands back */
    RxVideoListTable12*
    ListTable(
        _In_ RxVideoKind12 Kind)
    {
        return &Common[Kind];
    }

    PFND3D12DDI_EXECUTECOMMANDLISTS
    ExecuteEntry(
        _In_ RxVideoKind12 Kind) const
    {
        return Queues[Kind].pfnExecuteCommandLists;
    }

    RxVideoDevice12&
    Face()
    {
        return *VideoFace;
    }

    /* One pfnGetCaps answer from the video table */
    HRESULT
    GetCaps(
        _In_ D3D12DDICAPS_TYPE_VIDEO_0020 Type,
        _Inout_updates_bytes_(Size) VOID* Data,
        _In_ UINT Size) const;

    /* The driver node an API node index stands for; FALSE for a node the device does not have */
    BOOLEAN
    DriverNode(
        _In_ UINT NodeIndex,
        _Out_ UINT* Driver) const;

    const RxVideoNodeCaps12*
    NodeCaps(
        _In_ UINT NodeIndex) const
    {
        return NodeIndex < NodeCount ? &Nodes[NodeIndex] : nullptr;
    }

    /* Whether a decode profile is one the driver listed for this node */
    BOOLEAN
    HasProfile(
        _In_ UINT NodeIndex,
        _In_ const GUID& Profile) const;

    UINT
    GetExtensionCount() const
    {
        return ExtensionCount;
    }

    _Ret_maybenull_
    const RxVideoExtension12*
    FindExtension(
        _In_ const GUID& CommandId) const;

    const RxVideoExtension12*
    GetExtension(
        _In_ UINT Index) const
    {
        return &Extensions[Index];
    }

    RxDevice12& Device;

private:
    HRESULT
    FillTables();

    HRESULT
    CacheCaps();

    HRESULT
    CacheExtensions();

    UINT Version = 0;
    RxVideoDevice12* VideoFace = nullptr;
    D3D12DDI_DEVICE_FUNCS_VIDEO_0110 Functions = {};
    D3D12DDI_COMMAND_QUEUE_FUNCS_VIDEO_0020 Queues[RxVideoKinds] = {};
    D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_DECODE_0094 DecodeList = {};
    D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_PROCESS_0106 ProcessList = {};
    D3D12DDI_COMMAND_LIST_FUNCS_VIDEO_ENCODE_0103 EncodeList = {};
    RxVideoListTable12 Common[RxVideoKinds] = {};
    UINT NodeCount = 0;
    RxVideoNodeCaps12* Nodes = nullptr;
    UINT ExtensionCount = 0;
    RxVideoExtension12* Extensions = nullptr;
};

/* ID3D12VideoDevice on the device: lifetime and QueryInterface stay with the device */
class RxVideoDevice12 final : public ID3D12VideoDevice4
{
public:
    explicit RxVideoDevice12(
        _In_ RxVideoState12& Owner)
        : Video(Owner),
          Device(Owner.Device)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    CheckFeatureSupport(
        D3D12_FEATURE_VIDEO FeatureVideo,
        _Inout_updates_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
        UINT FeatureSupportDataSize) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoDecoder(
        _In_ const D3D12_VIDEO_DECODER_DESC* pDesc,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoDecoder) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoDecoderHeap(
        _In_ const D3D12_VIDEO_DECODER_HEAP_DESC* pVideoDecoderHeapDesc,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoDecoderHeap) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoProcessor(
        UINT NodeMask,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC* pOutputStreamDesc,
        UINT NumInputStreamDescs,
        _In_reads_(NumInputStreamDescs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoProcessor) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoMotionEstimator(
        _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC* pDesc,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoMotionEstimator) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoMotionVectorHeap(
        _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC* pDesc,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoMotionVectorHeap) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoDecoder1(
        _In_ const D3D12_VIDEO_DECODER_DESC* pDesc,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoDecoder) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoDecoderHeap1(
        _In_ const D3D12_VIDEO_DECODER_HEAP_DESC* pVideoDecoderHeapDesc,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoDecoderHeap) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoProcessor1(
        UINT NodeMask,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC* pOutputStreamDesc,
        UINT NumInputStreamDescs,
        _In_reads_(NumInputStreamDescs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoProcessor) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoExtensionCommand(
        _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC* pDesc,
        _In_reads_bytes_(CreationParametersDataSizeInBytes) const void* pCreationParameters,
        SIZE_T CreationParametersDataSizeInBytes,
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoExtensionCommand) override;

    HRESULT
    STDMETHODCALLTYPE
    ExecuteExtensionCommand(
        _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
        _In_reads_bytes_(ExecutionParametersSizeInBytes) const void* pExecutionParameters,
        SIZE_T ExecutionParametersSizeInBytes,
        _Out_writes_bytes_(OutputDataSizeInBytes) void* pOutputData,
        SIZE_T OutputDataSizeInBytes) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoEncoder(
        _In_ const D3D12_VIDEO_ENCODER_DESC* pDesc,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoEncoder) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoEncoderHeap(
        _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC* pDesc,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoEncoderHeap) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateVideoEncoderHeap1(
        _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1* pDesc,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppVideoEncoderHeap) override;

private:
    /* The checks and the driver call behind each feature, see videocaps.cpp */
    HRESULT
    CheckDecodeFeature(
        _In_ D3D12_FEATURE_VIDEO Feature,
        _Inout_ VOID* Data,
        _In_ UINT Size);

    HRESULT
    CheckProcessFeature(
        _In_ D3D12_FEATURE_VIDEO Feature,
        _Inout_ VOID* Data,
        _In_ UINT Size);

    HRESULT
    CheckEncodeFeature(
        _In_ D3D12_FEATURE_VIDEO Feature,
        _Inout_ VOID* Data,
        _In_ UINT Size);

    HRESULT
    CheckOtherFeature(
        _In_ D3D12_FEATURE_VIDEO Feature,
        _Inout_ VOID* Data,
        _In_ UINT Size);

    RxVideoState12& Video;
    RxDevice12& Device;
};

/* A video object's protected session, as GetProtectedResourceSession hands it out */
HRESULT
RxVideoSessionOut12(
    _In_opt_ RxProtectedResourceSession* Session,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppProtectedSession);

class RxVideoDecoderHeap12 final : public RxDeviceChild12<ID3D12VideoDecoderHeap1>
{
public:
    static constexpr GUID PrivateId = {0x0b5e7d21, 0x93c4, 0x4a8f, {0xb2, 0x16, 0x5d, 0x7e, 0x0c, 0x93, 0xa4, 0x18}};

    /* The desc is checked */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_DECODER_HEAP_DESC& Desc,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoDecoderHeap12** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_VIDEO_DECODER_HEAP_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEODECODERHEAP_0032
    GetDriverHandle() const
    {
        return DriverHeap;
    }

    const D3D12_VIDEO_DECODER_HEAP_DESC&
    GetApiDesc() const
    {
        return Desc;
    }

    /* The flags the decode support check gave for this configuration */
    D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS
    GetConfigurationFlags() const
    {
        return ConfigurationFlags;
    }

    RxProtectedResourceSession*
    GetSession() const
    {
        return Session;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoDecoderHeap12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_DECODER_HEAP_DESC& HeapDesc,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    D3D12_VIDEO_DECODER_HEAP_DESC Desc = {};
    D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS ConfigurationFlags = D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_NONE;
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEODECODERHEAP_0032 DriverHeap = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
};

class RxVideoDecoder12 final : public RxDeviceChild12<ID3D12VideoDecoder1>
{
public:
    static constexpr GUID PrivateId = {0x6c21f0a8, 0x2d57, 0x4be3, {0x8e, 0x49, 0x13, 0xa6, 0xd0, 0x75, 0x2f, 0xc4}};

    /* The desc is checked */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_DECODER_DESC& Desc,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoDecoder12** Decoder);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_VIDEO_DECODER_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEODECODER_0020
    GetDriverHandle() const
    {
        return DriverDecoder;
    }

    const D3D12_VIDEO_DECODER_DESC&
    GetApiDesc() const
    {
        return Desc;
    }

    RxProtectedResourceSession*
    GetSession() const
    {
        return Session;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoDecoder12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_DECODER_DESC& DecoderDesc,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    D3D12_VIDEO_DECODER_DESC Desc = {};
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEODECODER_0020 DriverDecoder = {};
    BOOLEAN IsDriverDecoderCreated = FALSE;
};

class RxVideoProcessor12 final : public RxDeviceChild12<ID3D12VideoProcessor1>
{
public:
    static constexpr GUID PrivateId = {0xa39d6f14, 0x7b08, 0x4c52, {0x96, 0xe1, 0x24, 0x8f, 0xb3, 0x5a, 0x0d, 0x67}};

    /* The streams are checked; the node mask is in API numbering */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ UINT NodeMask,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
        _In_ UINT NumInputs,
        _In_reads_(NumInputs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* Inputs,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoProcessor12** Processor);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    UINT
    STDMETHODCALLTYPE
    GetNodeMask(VOID) override
    {
        return Mask;
    }

    UINT
    STDMETHODCALLTYPE
    GetNumInputStreamDescs(VOID) override
    {
        return InputCount;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetInputStreamDescs(
        UINT NumInputStreamDescs,
        _Out_writes_(NumInputStreamDescs) D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs) override;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC
    STDMETHODCALLTYPE
    GetOutputStreamDesc(VOID) override
    {
        return OutputDesc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEOPROCESSOR_0020
    GetDriverHandle() const
    {
        return DriverProcessor;
    }

    const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC&
    GetInput(
        _In_ UINT Index) const
    {
        return InputDescs[Index];
    }

    RxProtectedResourceSession*
    GetSession() const
    {
        return Session;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoProcessor12() override;

    HRESULT
    Initialize(
        _In_ UINT NodeMask,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
        _In_ UINT NumInputs,
        _In_reads_(NumInputs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* Inputs,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    UINT Mask = 0;
    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC OutputDesc = {};
    UINT InputCount = 0;
    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* InputDescs = nullptr;
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOPROCESSOR_0020 DriverProcessor = {};
    BOOLEAN IsDriverProcessorCreated = FALSE;
};

class RxVideoMotionEstimator12 final : public RxDeviceChild12<ID3D12VideoMotionEstimator>
{
public:
    static constexpr GUID PrivateId = {0x52e8b0c7, 0x1f6a, 0x4d93, {0xa4, 0x2c, 0x89, 0x0b, 0x7e, 0x16, 0xd5, 0x3f}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC& Desc,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoMotionEstimator12** Estimator);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_VIDEO_MOTION_ESTIMATOR_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEOMOTIONESTIMATOR_0053
    GetDriverHandle() const
    {
        return DriverEstimator;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoMotionEstimator12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_DESC& EstimatorDesc,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    D3D12_VIDEO_MOTION_ESTIMATOR_DESC Desc = {};
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOMOTIONESTIMATOR_0053 DriverEstimator = {};
    BOOLEAN IsDriverEstimatorCreated = FALSE;
};

class RxVideoMotionVectorHeap12 final : public RxDeviceChild12<ID3D12VideoMotionVectorHeap>
{
public:
    static constexpr GUID PrivateId = {0xd04a73e9, 0x5c18, 0x4f26, {0x8b, 0x7d, 0x31, 0xe0, 0x9a, 0x64, 0xc2, 0x85}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC& Desc,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoMotionVectorHeap12** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEOMOTIONVECTORHEAP_0053
    GetDriverHandle() const
    {
        return DriverHeap;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoMotionVectorHeap12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC& HeapDesc,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC Desc = {};
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOMOTIONVECTORHEAP_0053 DriverHeap = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
};

class RxVideoExtensionCommand12 final : public RxDeviceChild12<ID3D12VideoExtensionCommand>
{
public:
    static constexpr GUID PrivateId = {0x7f3c925b, 0xe04d, 0x4a61, {0x9f, 0x8a, 0x46, 0xd2, 0x1b, 0x0e, 0x73, 0xc9}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC& Desc,
        _In_reads_bytes_(ParametersSize) const VOID* Parameters,
        _In_ SIZE_T ParametersSize,
        _In_opt_ RxProtectedResourceSession* Session,
        _Outptr_ RxVideoExtensionCommand12** Command);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_VIDEO_EXTENSION_COMMAND_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override
    {
        return Desc;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetProtectedResourceSession(
        REFIID riid,
        _COM_Outptr_opt_ void** ppProtectedSession) override
    {
        return RxVideoSessionOut12(Session, riid, ppProtectedSession);
    }

    D3D12DDI_HVIDEOEXTENSIONCOMMAND_0063
    GetDriverHandle() const
    {
        return DriverCommand;
    }

    /* The queues the driver said the command runs on */
    D3D12DDI_COMMAND_QUEUE_FLAGS
    GetQueueFlags() const
    {
        return QueueFlags;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoExtensionCommand12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_EXTENSION_COMMAND_DESC& CommandDesc,
        _In_reads_bytes_(ParametersSize) const VOID* Parameters,
        _In_ SIZE_T ParametersSize,
        _In_opt_ RxProtectedResourceSession* ProtectedSession);

    D3D12_VIDEO_EXTENSION_COMMAND_DESC Desc = {};
    D3D12DDI_COMMAND_QUEUE_FLAGS QueueFlags = D3D12DDI_COMMAND_QUEUE_FLAG_NONE;
    RxProtectedResourceSession* Session = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOEXTENSIONCOMMAND_0063 DriverCommand = {};
    BOOLEAN IsDriverCommandCreated = FALSE;
};

/* Codec specific blocks the encoder objects keep a copy of, sized by the codec */
union RxEncoderProfile12
{
    D3D12_VIDEO_ENCODER_PROFILE_H264 H264;
    D3D12_VIDEO_ENCODER_PROFILE_HEVC Hevc;
    D3D12_VIDEO_ENCODER_AV1_PROFILE Av1;
};

union RxEncoderLevel12
{
    D3D12_VIDEO_ENCODER_LEVELS_H264 H264;
    D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC Hevc;
    D3D12_VIDEO_ENCODER_AV1_LEVEL_TIER_CONSTRAINTS Av1;
};

union RxEncoderConfiguration12
{
    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 H264;
    D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC Hevc;
    D3D12_VIDEO_ENCODER_AV1_CODEC_CONFIGURATION Av1;
};

class RxVideoEncoder12 final : public RxDeviceChild12<ID3D12VideoEncoder>
{
public:
    static constexpr GUID PrivateId = {0x39b6e04f, 0x8a2d, 0x4c71, {0xbe, 0x05, 0x6f, 0x93, 0x2c, 0x18, 0xe7, 0xa0}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_ENCODER_DESC& Desc,
        _Outptr_ RxVideoEncoder12** Encoder);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    UINT
    STDMETHODCALLTYPE
    GetNodeMask(VOID) override
    {
        return Desc.NodeMask;
    }

    D3D12_VIDEO_ENCODER_FLAGS
    STDMETHODCALLTYPE
    GetEncoderFlags(VOID) override
    {
        return Desc.Flags;
    }

    D3D12_VIDEO_ENCODER_CODEC
    STDMETHODCALLTYPE
    GetCodec(VOID) override
    {
        return Desc.EncodeCodec;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetCodecProfile(
        _Inout_ D3D12_VIDEO_ENCODER_PROFILE_DESC dstProfile) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCodecConfiguration(
        _Inout_ D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION dstCodecConfig) override;

    DXGI_FORMAT
    STDMETHODCALLTYPE
    GetInputFormat(VOID) override
    {
        return Desc.InputFormat;
    }

    D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE
    STDMETHODCALLTYPE
    GetMaxMotionEstimationPrecision(VOID) override
    {
        return Desc.MaxMotionEstimationPrecision;
    }

    D3D12DDI_HVIDEOENCODER_0080
    GetDriverHandle() const
    {
        return DriverEncoder;
    }

    const D3D12_VIDEO_ENCODER_DESC&
    GetApiDesc() const
    {
        return Desc;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoEncoder12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_ENCODER_DESC& EncoderDesc);

    D3D12_VIDEO_ENCODER_DESC Desc = {};
    RxEncoderProfile12 Profile = {};
    RxEncoderConfiguration12 Configuration = {};
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOENCODER_0080 DriverEncoder = {};
    BOOLEAN IsDriverEncoderCreated = FALSE;
};

class RxVideoEncoderHeap12 final : public RxDeviceChild12<ID3D12VideoEncoderHeap1>
{
public:
    static constexpr GUID PrivateId = {0xe6a1d83c, 0x4f97, 0x4b0e, {0x81, 0x3a, 0xc5, 0x2d, 0x96, 0x07, 0x4b, 0xd1}};

    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1& Desc,
        _Outptr_ RxVideoEncoderHeap12** Heap);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    UINT
    STDMETHODCALLTYPE
    GetNodeMask(VOID) override
    {
        return Desc.NodeMask;
    }

    D3D12_VIDEO_ENCODER_HEAP_FLAGS
    STDMETHODCALLTYPE
    GetEncoderHeapFlags(VOID) override
    {
        return Desc.Flags;
    }

    D3D12_VIDEO_ENCODER_CODEC
    STDMETHODCALLTYPE
    GetCodec(VOID) override
    {
        return Desc.EncodeCodec;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetCodecProfile(
        _Inout_ D3D12_VIDEO_ENCODER_PROFILE_DESC dstProfile) override;

    HRESULT
    STDMETHODCALLTYPE
    GetCodecLevel(
        _Inout_ D3D12_VIDEO_ENCODER_LEVEL_SETTING dstLevel) override;

    UINT
    STDMETHODCALLTYPE
    GetResolutionListCount(VOID) override
    {
        return Desc.ResolutionsListCount;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetResolutionList(
        const UINT ResolutionsListCount,
        _Out_writes_(ResolutionsListCount) D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC* pResolutionList) override;

    UINT
    STDMETHODCALLTYPE
    GetPow2DownscaleFactor(VOID) override
    {
        return DownscaleFactor;
    }

    D3D12DDI_HVIDEOENCODERHEAP_0080
    GetDriverHandle() const
    {
        return DriverHeap;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxVideoEncoderHeap12() override;

    HRESULT
    Initialize(
        _In_ const D3D12_VIDEO_ENCODER_HEAP_DESC1& HeapDesc);

    D3D12_VIDEO_ENCODER_HEAP_DESC Desc = {};
    UINT DownscaleFactor = 0;
    RxEncoderProfile12 Profile = {};
    RxEncoderLevel12 Level = {};
    D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC* Resolutions = nullptr;
    RxDriverMemory12 Memory;
    D3D12DDI_HVIDEOENCODERHEAP_0080 DriverHeap = {};
    BOOLEAN IsDriverHeapCreated = FALSE;
};

/* What the queue needs of a video list it is asked to run */
class RxVideoListBase12 : public RxListCallbacks12
{
public:
    static constexpr GUID PrivateId = {0x1d94c6e2, 0xa73b, 0x4f05, {0x8c, 0x61, 0x2e, 0xb7, 0x50, 0x9d, 0x14, 0xfa}};

    /* A list that failed loses the device when it is run, as Windows does */
    virtual
    BOOLEAN
    IsReadyFor(
        _In_ D3D12_COMMAND_LIST_TYPE QueueType) = 0;

    virtual
    D3D12DDI_HCOMMANDLIST
    GetDriverList() const = 0;
};

/* The commands every video list records, shared by the decode, process and encode lists */
template <typename Interface>
class RxVideoList12 : public RxDeviceChild12<Interface>, public RxVideoListBase12
{
public:
    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    D3D12_COMMAND_LIST_TYPE
    STDMETHODCALLTYPE
    GetType(VOID) override
    {
        return Type;
    }

    HRESULT
    STDMETHODCALLTYPE
    Close(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    Reset(
        _In_ ID3D12CommandAllocator* pAllocator) override;

    VOID
    STDMETHODCALLTYPE
    ClearState(VOID) override;

    VOID
    STDMETHODCALLTYPE
    ResourceBarrier(
        _In_ UINT NumBarriers,
        _In_reads_(NumBarriers) const D3D12_RESOURCE_BARRIER* pBarriers) override;

    VOID
    STDMETHODCALLTYPE
    DiscardResource(
        _In_ ID3D12Resource* pResource,
        _In_opt_ const D3D12_DISCARD_REGION* pRegion) override;

    VOID
    STDMETHODCALLTYPE
    BeginQuery(
        _In_ ID3D12QueryHeap* pQueryHeap,
        _In_ D3D12_QUERY_TYPE QueryType,
        _In_ UINT Index) override;

    VOID
    STDMETHODCALLTYPE
    EndQuery(
        _In_ ID3D12QueryHeap* pQueryHeap,
        _In_ D3D12_QUERY_TYPE QueryType,
        _In_ UINT Index) override;

    VOID
    STDMETHODCALLTYPE
    ResolveQueryData(
        _In_ ID3D12QueryHeap* pQueryHeap,
        _In_ D3D12_QUERY_TYPE QueryType,
        _In_ UINT StartIndex,
        _In_ UINT NumQueries,
        _In_ ID3D12Resource* pDestinationBuffer,
        _In_ UINT64 AlignedDestinationBufferOffset) override;

    VOID
    STDMETHODCALLTYPE
    SetPredication(
        _In_opt_ ID3D12Resource* pBuffer,
        _In_ UINT64 AlignedBufferOffset,
        _In_ D3D12_PREDICATION_OP Operation) override;

    VOID
    STDMETHODCALLTYPE
    SetMarker(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void* pData,
        UINT Size) override;

    VOID
    STDMETHODCALLTYPE
    BeginEvent(
        UINT Metadata,
        _In_reads_bytes_opt_(Size) const void* pData,
        UINT Size) override;

    VOID
    STDMETHODCALLTYPE
    EndEvent(VOID) override;

    VOID
    STDMETHODCALLTYPE
    WriteBufferImmediate(
        UINT Count,
        _In_reads_(Count) const D3D12_WRITEBUFFERIMMEDIATE_PARAMETER* pParams,
        _In_reads_opt_(Count) const D3D12_WRITEBUFFERIMMEDIATE_MODE* pModes) override;

    VOID
    STDMETHODCALLTYPE
    SetProtectedResourceSession(
        _In_opt_ ID3D12ProtectedResourceSession* pProtectedResourceSession) override;

    VOID
    STDMETHODCALLTYPE
    InitializeExtensionCommand(
        _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
        _In_reads_bytes_(InitializationParametersSizeInBytes) const void* pInitializationParameters,
        SIZE_T InitializationParametersSizeInBytes) override;

    VOID
    STDMETHODCALLTYPE
    ExecuteExtensionCommand(
        _In_ ID3D12VideoExtensionCommand* pExtensionCommand,
        _In_reads_bytes_(ExecutionParametersSizeInBytes) const void* pExecutionParameters,
        SIZE_T ExecutionParametersSizeInBytes) override;

    VOID
    STDMETHODCALLTYPE
    Barrier(
        UINT32 NumBarrierGroups,
        _In_reads_(NumBarrierGroups) const D3D12_BARRIER_GROUP* pBarrierGroups) override;

    BOOLEAN
    IsReadyFor(
        _In_ D3D12_COMMAND_LIST_TYPE QueueType) override
    {
        if (!Recording && FAILED(Error))
        {
            this->Device.MarkRemoved(DXGI_ERROR_INVALID_CALL);
            return FALSE;
        }
        return !Recording && QueueType == Type;
    }

    D3D12DDI_HCOMMANDLIST
    GetDriverList() const override
    {
        return DriverList;
    }

    VOID
    OnDriverError(
        _In_ HRESULT hr) override;

    VOID
    OnDriverTable(
        _In_ D3D12DDI_HRTTABLE Table) override;

    /* Makes the driver list, recording into the allocator when one is given */
    HRESULT
    Initialize(
        _In_ D3D12_COMMAND_LIST_TYPE ListType,
        _In_opt_ RxCommandAllocator* FirstAllocator);

protected:
    explicit RxVideoList12(
        _In_ RxDevice12& Owner)
        : RxDeviceChild12<Interface>(Owner)
    {
    }

    ~RxVideoList12() override;

    /* Open and not failed; anything else drops what is recorded */
    BOOLEAN
    Records() const
    {
        return Recording && SUCCEEDED(Error);
    }

    /* The first failure is what Close returns */
    VOID
    Fail(
        _In_ HRESULT hr);

    /* A session the list's work and an object's protection must agree on */
    BOOLEAN
    SessionMatches(
        _In_opt_ RxProtectedResourceSession* ObjectSession) const
    {
        return ObjectSession == Session;
    }

    RxVideoState12&
    Video() const
    {
        return *this->Device.GetVideo();
    }

    /* Temporary room for driver handle arrays, kept until the next reset */
    _Ret_maybenull_
    VOID*
    Scratch(
        _In_ SIZE_T Size);

    D3D12_COMMAND_LIST_TYPE Type = D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE;
    D3D12DDI_HCOMMANDLIST DriverList = {};
    const RxVideoListTable12* Table = nullptr;

private:
    HRESULT
    Claim(
        _In_ RxCommandAllocator* NewAllocator);

    VOID
    Begin();

    VOID
    ReleaseScratch();

    RxDriverMemory12 Memory;
    BOOLEAN IsDriverListCreated = FALSE;
    BOOLEAN Recording = FALSE;
    BOOLEAN TakesSessions = FALSE;
    HRESULT Error = S_OK;
    RxCommandAllocator* Allocator = nullptr;
    UINT64 ListId = UINT64_MAX;
    INT OpenQueries = 0;
    RxProtectedResourceSession* Session = nullptr;
    struct RxScratchBlock* ScratchBlocks = nullptr;
};

class RxVideoDecodeList12 final : public RxVideoList12<ID3D12VideoDecodeCommandList3>
{
public:
    explicit RxVideoDecodeList12(
        _In_ RxDevice12& Owner)
        : RxVideoList12(Owner)
    {
    }

    VOID
    STDMETHODCALLTYPE
    DecodeFrame(
        _In_ ID3D12VideoDecoder* pDecoder,
        _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
        _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS* pInputArguments) override;

    VOID
    STDMETHODCALLTYPE
    DecodeFrame1(
        _In_ ID3D12VideoDecoder* pDecoder,
        _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1* pOutputArguments,
        _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS* pInputArguments) override;

private:
    VOID
    RecordDecode(
        _In_ ID3D12VideoDecoder* pDecoder,
        _In_ const D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1& Output,
        _In_ const D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS& Input);
};

class RxVideoProcessList12 final : public RxVideoList12<ID3D12VideoProcessCommandList3>
{
public:
    explicit RxVideoProcessList12(
        _In_ RxDevice12& Owner)
        : RxVideoList12(Owner)
    {
    }

    VOID
    STDMETHODCALLTYPE
    ProcessFrames(
        _In_ ID3D12VideoProcessor* pVideoProcessor,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
        UINT NumInputStreams,
        _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS* pInputArguments) override;

    VOID
    STDMETHODCALLTYPE
    ProcessFrames1(
        _In_ ID3D12VideoProcessor* pVideoProcessor,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
        UINT NumInputStreams,
        _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1* pInputArguments) override;

private:
    VOID
    RecordProcess(
        _In_ ID3D12VideoProcessor* pVideoProcessor,
        _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS& Output,
        _In_ UINT NumInputStreams,
        _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS* Inputs,
        _In_reads_opt_(NumInputStreams) const D3D12_VIDEO_FIELD_TYPE* FieldTypes);
};

class RxVideoEncodeList12 final : public RxVideoList12<ID3D12VideoEncodeCommandList3>
{
public:
    explicit RxVideoEncodeList12(
        _In_ RxDevice12& Owner)
        : RxVideoList12(Owner)
    {
    }

    VOID
    STDMETHODCALLTYPE
    EstimateMotion(
        _In_ ID3D12VideoMotionEstimator* pMotionEstimator,
        _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT* pOutputArguments,
        _In_ const D3D12_VIDEO_MOTION_ESTIMATOR_INPUT* pInputArguments) override;

    VOID
    STDMETHODCALLTYPE
    ResolveMotionVectorHeap(
        const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT* pOutputArguments,
        const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT* pInputArguments) override;

    VOID
    STDMETHODCALLTYPE
    EncodeFrame(
        _In_ ID3D12VideoEncoder* pEncoder,
        _In_ ID3D12VideoEncoderHeap* pHeap,
        _In_ const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS* pInputArguments,
        _In_ const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS* pOutputArguments) override;

    VOID
    STDMETHODCALLTYPE
    ResolveEncoderOutputMetadata(
        _In_ const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS* pInputArguments,
        _In_ const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS* pOutputArguments) override;
};

/* A new video list of the type; closed unless an allocator is given */
HRESULT
RxCreateVideoList12(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type,
    _In_opt_ RxCommandAllocator* Allocator,
    _In_ REFIID riid,
    _COM_Outptr_ void** ppCommandList);

/* The driver list of a closed video list that may run on a queue of this type */
BOOLEAN
RxVideoListForQueue12(
    _In_opt_ ID3D12CommandList* List,
    _In_ D3D12_COMMAND_LIST_TYPE QueueType,
    _Out_ D3D12DDI_HCOMMANDLIST* Handle);

/* The entry a queue of this type submits lists through */
PFND3D12DDI_EXECUTECOMMANDLISTS
RxExecuteEntry12(
    _In_ RxDevice12& Device,
    _In_ D3D12_COMMAND_LIST_TYPE Type);

/* An API decode configuration in driver form; the only encryption the API names is none */
VOID
RxToDdiConfiguration(
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& In,
    _Out_ D3D12DDI_VIDEO_DECODE_CONFIGURATION_0020* Out);

/* The video list kind of an API list type */
RxVideoKind12
RxVideoKindOf(
    _In_ D3D12_COMMAND_LIST_TYPE Type);
