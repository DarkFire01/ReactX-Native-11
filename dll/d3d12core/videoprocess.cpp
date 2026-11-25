/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Video processing: ID3D12VideoProcessor and ProcessFrames
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"
#include "commandlist_internal.hpp"

static_assert(sizeof(D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC) == sizeof(D3D12DDI_VIDEO_PROCESS_OUTPUT_STREAM_DESC_0032));
static_assert(sizeof(D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC) == sizeof(D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0032));

/* One API input stream desc in the form a driver at this version takes */
static
VOID
RxToDdiInputDesc(
    _In_ const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC& In,
    _Out_ D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0106& Out)
{
    ZeroMemory(&Out, sizeof(Out));
    Out.Format = In.Format;
    Out.ColorSpace = In.ColorSpace;
    Out.SourceAspectRatio = In.SourceAspectRatio;
    Out.DestinationAspectRatio = In.DestinationAspectRatio;
    Out.FrameRate = In.FrameRate;
    Out.SourceSizeRange.MaxWidth = In.SourceSizeRange.MaxWidth;
    Out.SourceSizeRange.MaxHeight = In.SourceSizeRange.MaxHeight;
    Out.SourceSizeRange.MinWidth = In.SourceSizeRange.MinWidth;
    Out.SourceSizeRange.MinHeight = In.SourceSizeRange.MinHeight;
    Out.DestinationSizeRange.MaxWidth = In.DestinationSizeRange.MaxWidth;
    Out.DestinationSizeRange.MaxHeight = In.DestinationSizeRange.MaxHeight;
    Out.DestinationSizeRange.MinWidth = In.DestinationSizeRange.MinWidth;
    Out.DestinationSizeRange.MinHeight = In.DestinationSizeRange.MinHeight;
    Out.EnableOrientation = In.EnableOrientation;
    Out.FilterFlags = (D3D12DDI_VIDEO_PROCESS_FILTER_FLAGS_0020)In.FilterFlags;
    Out.StereoFormat = (D3D12DDI_VIDEO_FRAME_STEREO_FORMAT_0020)In.StereoFormat;
    Out.DeinterlaceMode = (D3D12DDI_VIDEO_PROCESS_DEINTERLACE_FLAGS_0020)In.DeinterlaceMode;
    Out.EnableAlphaBlending = In.EnableAlphaBlending;
    Out.LumaKey.Enable = In.LumaKey.Enable;
    Out.LumaKey.Lower = In.LumaKey.Lower;
    Out.LumaKey.Upper = In.LumaKey.Upper;
    Out.NumPastFrames = In.NumPastFrames;
    Out.NumFutureFrames = In.NumFutureFrames;
    Out.EnableAutoProcessing = In.EnableAutoProcessing;
}

/* Each stream must be one the driver processes into this output */
static
BOOLEAN
RxStreamIsSupported(
    _In_ RxDevice12& Device,
    _In_ UINT NodeIndex,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
    _In_ const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC& Input)
{
    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT Data = {};
    HRESULT hr;

    Data.NodeIndex = NodeIndex;
    Data.InputSample.Width = Input.SourceSizeRange.MaxWidth;
    Data.InputSample.Height = Input.SourceSizeRange.MaxHeight;
    Data.InputSample.Format.Format = Input.Format;
    Data.InputSample.Format.ColorSpace = Input.ColorSpace;
    Data.InputFieldType = Input.FieldType;
    Data.InputStereoFormat = Input.StereoFormat;
    Data.InputFrameRate = Input.FrameRate;
    Data.OutputFormat.Format = Output.Format;
    Data.OutputFormat.ColorSpace = Output.ColorSpace;
    Data.OutputStereoFormat = Output.EnableStereo ? D3D12_VIDEO_FRAME_STEREO_FORMAT_HORIZONTAL
                                                   : D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
    Data.OutputFrameRate = Output.FrameRate;
    hr = Device.GetVideo()->Face().CheckFeatureSupport(D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &Data, sizeof(Data));
    return SUCCEEDED(hr) && (Data.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) != 0;
}

HRESULT
RxVideoProcessor12::Create(
    _In_ RxDevice12& Device,
    _In_ UINT NodeMask,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
    _In_ UINT NumInputs,
    _In_reads_(NumInputs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* Inputs,
    _In_opt_ RxProtectedResourceSession* Session,
    _Outptr_ RxVideoProcessor12** Processor)
{
    RxVideoProcessor12* Created;
    HRESULT hr;

    *Processor = nullptr;

    hr = Device.CheckUsable();
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxVideoProcessor12(Device);
    if (Created == nullptr)
        return E_OUTOFMEMORY;

    hr = Created->Initialize(NodeMask, Output, NumInputs, Inputs, Session);
    if (FAILED(hr))
    {
        Created->Release();
        return hr;
    }

    *Processor = Created;
    return S_OK;
}

HRESULT
RxVideoProcessor12::Initialize(
    _In_ UINT NodeMask,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
    _In_ UINT NumInputs,
    _In_reads_(NumInputs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* Inputs,
    _In_opt_ RxProtectedResourceSession* ProtectedSession)
{
    RxVideoState12& Video = *Device.GetVideo();
    const D3D12DDI_DEVICE_FUNCS_VIDEO_0110& Ddi = Video.Ddi();
    const RxVideoNodeCaps12* Caps;
    D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0106 Args = {};
    D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0106* Driver = nullptr;
    D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043* Middle = nullptr;
    UINT NodeIndex = 0;
    SIZE_T Size;
    UINT Index;
    HRESULT hr;

    /* One node, no more streams than the node blends, each one the driver can process */
    if ((NodeMask & (NodeMask - 1)) != 0)
        return E_INVALIDARG;
    while (NodeMask != 0 && (NodeMask & (1u << NodeIndex)) == 0)
        NodeIndex++;
    Caps = Video.NodeCaps(NodeIndex);
    if (Caps == nullptr || NumInputs > Caps->MaxInputStreams)
        return E_INVALIDARG;
    for (Index = 0; Index < NumInputs; Index++)
    {
        if (!RxStreamIsSupported(Device, NodeIndex, Output, Inputs[Index]))
            return E_INVALIDARG;
    }

    Mask = NodeMask;
    OutputDesc = Output;
    InputDescs = new (std::nothrow) D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC[NumInputs];
    Driver = new (std::nothrow) D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0106[NumInputs];
    if (InputDescs == nullptr || Driver == nullptr)
    {
        delete[] Driver;
        return E_OUTOFMEMORY;
    }
    memcpy(InputDescs, Inputs, NumInputs * sizeof(*InputDescs));
    InputCount = NumInputs;
    Session = ProtectedSession;
    if (Session != nullptr)
        Session->AddRef();

    for (Index = 0; Index < NumInputs; Index++)
        RxToDdiInputDesc(Inputs[Index], Driver[Index]);

    hr = Device.TranslateNodeMask(NodeMask, TRUE, &Args.NodeMask);
    if (SUCCEEDED(hr))
    {
        memcpy(&Args.OutputStream, &Output, sizeof(Output));
        Args.NumInputStreams = NumInputs;
        Args.pInputStreams = Driver;
        if (Session != nullptr)
            Args.hDrvProtectedResourceSession = Session->GetDriverHandle();
    }

    /* Field types moved out of the stream desc after the first video versions, and LUTs came later */
    if (SUCCEEDED(hr) && Video.GetVersion() < RxVideo0043)
    {
        D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0032 Oldest = {};

        static_assert(sizeof(Inputs[0]) == sizeof(D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0032));
        Oldest.NodeMask = Args.NodeMask;
        Oldest.OutputStream = Args.OutputStream;
        Oldest.pInputStreams = reinterpret_cast<D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0032*>(InputDescs);
        Oldest.NumInputStreams = NumInputs;
        Size = reinterpret_cast<PFND3D12DDI_CALCPRIVATEVIDEOPROCESSORSIZE_0032>(Ddi.pfnCalcPrivateVideoProcessorSize)(
            Device.Handle(), &Oldest);
        DriverProcessor.pDrvPrivate = Memory.Allocate(Size);
        hr = DriverProcessor.pDrvPrivate == nullptr
                 ? E_OUTOFMEMORY
                 : reinterpret_cast<PFND3D12DDI_CREATEVIDEOPROCESSOR_0032>(Ddi.pfnCreateVideoProcessor)(
                       Device.Handle(), &Oldest, DriverProcessor);
    }
    else if (SUCCEEDED(hr) && Video.GetVersion() < RxVideo0106)
    {
        D3D12DDIARG_CREATE_VIDEO_PROCESSOR_0072 Older = {};

        Middle = new (std::nothrow) D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043[NumInputs];
        if (Middle == nullptr)
        {
            delete[] Driver;
            return E_OUTOFMEMORY;
        }
        for (Index = 0; Index < NumInputs; Index++)
            memcpy(&Middle[Index], &Driver[Index], sizeof(Middle[Index]));

        /* The 0043 shape is the 0072 one without its trailing session */
        Older.NodeMask = Args.NodeMask;
        Older.OutputStream = Args.OutputStream;
        Older.pInputStreams = Middle;
        Older.NumInputStreams = NumInputs;
        Older.hDrvProtectedResourceSession = Args.hDrvProtectedResourceSession;
        Size = reinterpret_cast<PFND3D12DDI_CALCPRIVATEVIDEOPROCESSORSIZE_0072>(Ddi.pfnCalcPrivateVideoProcessorSize)(
            Device.Handle(), &Older);
        DriverProcessor.pDrvPrivate = Memory.Allocate(Size);
        hr = DriverProcessor.pDrvPrivate == nullptr
                 ? E_OUTOFMEMORY
                 : reinterpret_cast<PFND3D12DDI_CREATEVIDEOPROCESSOR_0072>(Ddi.pfnCreateVideoProcessor)(
                       Device.Handle(), &Older, DriverProcessor);
    }
    else if (SUCCEEDED(hr))
    {
        Size = Ddi.pfnCalcPrivateVideoProcessorSize(Device.Handle(), &Args);
        DriverProcessor.pDrvPrivate = Memory.Allocate(Size);
        hr = DriverProcessor.pDrvPrivate == nullptr ? E_OUTOFMEMORY
                                                    : Ddi.pfnCreateVideoProcessor(Device.Handle(), &Args, DriverProcessor);
    }

    delete[] Middle;
    delete[] Driver;
    if (FAILED(hr))
    {
        RX_TRACE(L"driver CreateVideoProcessor failed 0x%08lX", hr);
        return hr;
    }

    IsDriverProcessorCreated = TRUE;

    /* New pageables start out resident, as on Windows */
    return Device.MakePageableResident(DriverProcessor.pDrvPrivate, D3D12DDI_HT_0020_VIDEO_PROCESSOR);
}

RxVideoProcessor12::~RxVideoProcessor12()
{
    if (IsDriverProcessorCreated)
        Device.GetVideo()->Ddi().pfnDestroyVideoProcessor(Device.Handle(), DriverProcessor);
    if (Session != nullptr)
        Session->Release();
    delete[] InputDescs;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessor12::QueryInterface(
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
        riid == __uuidof(ID3D12VideoProcessor) || riid == __uuidof(ID3D12VideoProcessor1) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12VideoProcessor1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxVideoProcessor12::GetInputStreamDescs(
    UINT NumInputStreamDescs,
    _Out_writes_(NumInputStreamDescs) D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* pInputStreamDescs)
{
    if (NumInputStreamDescs > InputCount || pInputStreamDescs == nullptr)
        return E_INVALIDARG;

    memcpy(pInputStreamDescs, InputDescs, NumInputStreamDescs * sizeof(*pInputStreamDescs));
    return S_OK;
}

/* ProcessFrames */

VOID
STDMETHODCALLTYPE
RxVideoProcessList12::ProcessFrames(
    _In_ ID3D12VideoProcessor* pVideoProcessor,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
    UINT NumInputStreams,
    _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS* pInputArguments)
{
    if (!Records())
        return;

    if (pOutputArguments == nullptr || (NumInputStreams != 0 && pInputArguments == nullptr))
    {
        Fail(E_INVALIDARG);
        return;
    }

    RecordProcess(pVideoProcessor, *pOutputArguments, NumInputStreams, pInputArguments, nullptr);
}

VOID
STDMETHODCALLTYPE
RxVideoProcessList12::ProcessFrames1(
    _In_ ID3D12VideoProcessor* pVideoProcessor,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS* pOutputArguments,
    UINT NumInputStreams,
    _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS1* pInputArguments)
{
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS* Inputs;
    D3D12_VIDEO_FIELD_TYPE* Fields;
    UINT Index;

    if (!Records())
        return;

    if (pOutputArguments == nullptr || (NumInputStreams != 0 && pInputArguments == nullptr))
    {
        Fail(E_INVALIDARG);
        return;
    }

    /* The newer stream arguments are the older ones with a field type after them */
    Inputs = static_cast<D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS*>(Scratch(NumInputStreams * sizeof(*Inputs) + 1));
    Fields = static_cast<D3D12_VIDEO_FIELD_TYPE*>(Scratch(NumInputStreams * sizeof(*Fields) + 1));
    if (Inputs == nullptr || Fields == nullptr)
        return;

    for (Index = 0; Index < NumInputStreams; Index++)
    {
        memcpy(&Inputs[Index], &pInputArguments[Index], sizeof(Inputs[Index]));
        Fields[Index] = pInputArguments[Index].FieldType;
    }

    RecordProcess(pVideoProcessor, *pOutputArguments, NumInputStreams, Inputs, Fields);
}

/* Driver handles for one stream's past and future frames */
static
BOOLEAN
RxConvertReferences(
    _In_ const D3D12_VIDEO_PROCESS_REFERENCE_SET& In,
    _Out_ D3D12DDI_VIDEO_PROCESS_REFERENCES_INFO_0020& Out,
    _In_reads_(In.NumPastFrames + In.NumFutureFrames) D3D12DDI_HRESOURCE* Handles)
{
    UINT Index;

    if ((In.NumPastFrames != 0 && In.ppPastFrames == nullptr) || (In.NumFutureFrames != 0 && In.ppFutureFrames == nullptr))
        return FALSE;

    for (Index = 0; Index < In.NumPastFrames; Index++)
        Handles[Index] = RxDriverResource(In.ppPastFrames[Index]);
    for (Index = 0; Index < In.NumFutureFrames; Index++)
        Handles[In.NumPastFrames + Index] = RxDriverResource(In.ppFutureFrames[Index]);

    Out.NumPastFrames = In.NumPastFrames;
    Out.hDrvPastFrames = In.NumPastFrames != 0 ? Handles : nullptr;
    Out.pPastSubresources = In.pPastSubresources;
    Out.NumFutureFrames = In.NumFutureFrames;
    Out.hDrvFutureFrames = In.NumFutureFrames != 0 ? Handles + In.NumPastFrames : nullptr;
    Out.pFutureSubresources = In.pFutureSubresources;
    return TRUE;
}

VOID
RxVideoProcessList12::RecordProcess(
    _In_ ID3D12VideoProcessor* pVideoProcessor,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS& Output,
    _In_ UINT NumInputStreams,
    _In_reads_(NumInputStreams) const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS* Inputs,
    _In_reads_opt_(NumInputStreams) const D3D12_VIDEO_FIELD_TYPE* FieldTypes)
{
    RxVideoState12& State = Video();
    RxVideoProcessor12* Processor = RxUnwrap12<RxVideoProcessor12>(pVideoProcessor);
    D3D12DDIARG_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS_0032 DriverOutput = {};
    D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0106* DriverInputs;
    UINT Stream;
    UINT View;

    static_assert(_countof(Inputs[0].FilterLevels) == _countof(DriverInputs[0].FilterLevels));

    if (Processor == nullptr || NumInputStreams == 0 || NumInputStreams > Processor->GetNumInputStreamDescs() ||
        RxUnwrap12<RxResource>(Output.OutputStream[0].pTexture2D) == nullptr || !SessionMatches(Processor->GetSession()))
    {
        Fail(E_INVALIDARG);
        return;
    }

    DriverInputs = static_cast<D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0106*>(
        Scratch(NumInputStreams * sizeof(*DriverInputs)));
    if (DriverInputs == nullptr)
        return;

    for (Stream = 0; Stream < NumInputStreams; Stream++)
    {
        const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS& In = Inputs[Stream];
        D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0106& Out = DriverInputs[Stream];

        if (RxUnwrap12<RxResource>(In.InputStream[0].pTexture2D) == nullptr)
        {
            Fail(E_INVALIDARG);
            return;
        }

        /* Stereo streams carry a second view */
        for (View = 0; View < 2; View++)
        {
            const D3D12_VIDEO_PROCESS_REFERENCE_SET& References = In.InputStream[View].ReferenceSet;
            D3D12DDI_HRESOURCE* Handles =
                static_cast<D3D12DDI_HRESOURCE*>(Scratch((References.NumPastFrames + References.NumFutureFrames + 1) *
                                                         sizeof(D3D12DDI_HRESOURCE)));

            if (Handles == nullptr)
                return;
            if (!RxConvertReferences(References, Out.InputStream[View].ReferenceInfo, Handles))
            {
                Fail(E_INVALIDARG);
                return;
            }

            Out.InputStream[View].hDrvInputTexture = RxDriverResource(In.InputStream[View].pTexture2D);
            Out.InputStream[View].Subresource = In.InputStream[View].Subresource;
        }

        memcpy(&Out.Transform.SourceRectangle, &In.Transform.SourceRectangle, sizeof(Out.Transform.SourceRectangle));
        memcpy(&Out.Transform.DestinationRectangle, &In.Transform.DestinationRectangle,
               sizeof(Out.Transform.DestinationRectangle));
        Out.Transform.Orientation = (D3D12DDI_VIDEO_PROCESS_ORIENTATION_0020)In.Transform.Orientation;
        Out.Flags = (D3D12DDI_VIDEO_PROCESS_INPUT_STREAM_FLAGS_0020)In.Flags;
        Out.RateInfo.OutputIndex = In.RateInfo.OutputIndex;
        Out.RateInfo.InputFrameOrField = In.RateInfo.InputFrameOrField;
        memcpy(Out.FilterLevels, In.FilterLevels, sizeof(Out.FilterLevels));
        Out.AlphaBlending.Enable = In.AlphaBlending.Enable;
        Out.AlphaBlending.Alpha = In.AlphaBlending.Alpha;

        /* Without a field type per frame the one the processor was made with stands */
        Out.FieldType = (D3D12DDI_VIDEO_FIELD_TYPE_0020)(FieldTypes != nullptr ? FieldTypes[Stream]
                                                                                : Processor->GetInput(Stream).FieldType);
    }

    for (View = 0; View < 2; View++)
    {
        DriverOutput.OutputStream[View].hDrvTexture2D = RxDriverResource(Output.OutputStream[View].pTexture2D);
        DriverOutput.OutputStream[View].Subresource = Output.OutputStream[View].Subresource;
    }
    memcpy(&DriverOutput.TargetRectangle, &Output.TargetRectangle, sizeof(DriverOutput.TargetRectangle));

    if (State.GetVersion() >= RxVideo0106)
    {
        State.ProcessDdi().pfnProcessFrame(DriverList, Processor->GetDriverHandle(), &DriverOutput, DriverInputs,
                                           NumInputStreams);
        return;
    }

    /* Older drivers take the streams without the lookup tables, and the first ones without a field type */
    if (State.GetVersion() >= RxVideo0043)
    {
        D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0043* Older =
            static_cast<D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0043*>(Scratch(NumInputStreams * sizeof(*Older)));

        if (Older == nullptr)
            return;
        for (Stream = 0; Stream < NumInputStreams; Stream++)
            memcpy(&Older[Stream], &DriverInputs[Stream], sizeof(Older[Stream]));
        reinterpret_cast<PFND3D12DDI_VIDEO_PROCESS_FRAME_0043>(State.ProcessDdi().pfnProcessFrame)(
            DriverList, Processor->GetDriverHandle(), &DriverOutput, Older, NumInputStreams);
        return;
    }

    {
        D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0032* Oldest =
            static_cast<D3D12DDIARG_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS_0032*>(Scratch(NumInputStreams * sizeof(*Oldest)));

        if (Oldest == nullptr)
            return;
        for (Stream = 0; Stream < NumInputStreams; Stream++)
            memcpy(&Oldest[Stream], &DriverInputs[Stream], sizeof(Oldest[Stream]));
        reinterpret_cast<PFND3D12DDI_VIDEO_PROCESS_FRAME_0032>(State.ProcessDdi().pfnProcessFrame)(
            DriverList, Processor->GetDriverHandle(), &DriverOutput, Oldest, NumInputStreams);
    }
}
