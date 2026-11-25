/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D12VideoDevice::CheckFeatureSupport: the checks, the cached answers and the driver queries
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "video.hpp"

/* The driver's name for a bitstream that is not encrypted */
static constexpr GUID RxClearBitstream12 = {0x1b81bed0, 0xa0c7, 0x11d3, {0xb9, 0x84, 0x00, 0xc0, 0x4f, 0x2e, 0x73, 0xc5}};

VOID
RxToDdiConfiguration(
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& In,
    _Out_ D3D12DDI_VIDEO_DECODE_CONFIGURATION_0020* Out)
{
    Out->DecodeProfile = In.DecodeProfile;
    Out->BitstreamEncryption = RxClearBitstream12;
    Out->InterlaceType = (D3D12DDI_VIDEO_CODED_INTERLACE_TYPE_0020)In.InterlaceType;
}

/* The answer a driver gave is trusted no further than its call succeeding */
static
HRESULT
RxDriverAnswer(
    _In_ HRESULT hr)
{
    if (SUCCEEDED(hr))
        return S_OK;

    RX_TRACE(L"driver video caps query failed 0x%08lX", hr);
    return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}

/* An API node index in the driver's numbering; FALSE when the device has no such node */
static
BOOLEAN
RxNode(
    _In_ const RxVideoState12& Video,
    _In_ UINT NodeIndex,
    _Out_ UINT* Driver)
{
    return Video.DriverNode(NodeIndex, Driver);
}

/* Queries whose API and driver data are laid out alike, with the node index first when named */
template <typename Api, typename Ddi>
static
HRESULT
RxPassThrough(
    _In_ const RxVideoState12& Video,
    _In_ D3D12DDICAPS_TYPE_VIDEO_0020 Type,
    _Inout_ VOID* Data,
    _In_ UINT Size,
    _In_ BOOLEAN NodeFirst)
{
    static_assert(sizeof(Api) == sizeof(Ddi));
    Ddi Driver;
    UINT Node = 0;

    if (Size != sizeof(Api))
        return E_INVALIDARG;

    memcpy(&Driver, Data, sizeof(Driver));
    if (NodeFirst)
    {
        if (!RxNode(Video, *static_cast<const UINT*>(Data), &Node))
            return E_INVALIDARG;
        memcpy(&Driver, &Node, sizeof(Node));
    }

    if (FAILED(RxDriverAnswer(Video.GetCaps(Type, &Driver, sizeof(Driver)))))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;

    /* The node index goes back as the caller gave it */
    if (NodeFirst)
        memcpy(&Node, Data, sizeof(Node));
    memcpy(Data, &Driver, sizeof(Driver));
    if (NodeFirst)
        memcpy(Data, &Node, sizeof(Node));
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxVideoDevice12::CheckFeatureSupport(
    D3D12_FEATURE_VIDEO FeatureVideo,
    _Inout_updates_bytes_(FeatureSupportDataSize) void* pFeatureSupportData,
    UINT FeatureSupportDataSize)
{
    switch (FeatureVideo)
    {
        case D3D12_FEATURE_VIDEO_DECODE_SUPPORT:
        case D3D12_FEATURE_VIDEO_DECODE_PROFILES:
        case D3D12_FEATURE_VIDEO_DECODE_FORMATS:
        case D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT:
        case D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE:
        case D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT:
        case D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT:
        case D3D12_FEATURE_VIDEO_DECODE_HISTOGRAM:
        case D3D12_FEATURE_VIDEO_DECODE_PROTECTED_RESOURCES:
        case D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE1:
            return CheckDecodeFeature(FeatureVideo, pFeatureSupportData, FeatureSupportDataSize);

        case D3D12_FEATURE_VIDEO_PROCESS_SUPPORT:
        case D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS:
        case D3D12_FEATURE_VIDEO_PROCESS_REFERENCE_INFO:
        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE:
        case D3D12_FEATURE_VIDEO_PROCESS_PROTECTED_RESOURCES:
        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE1:
            return CheckProcessFeature(FeatureVideo, pFeatureSupportData, FeatureSupportDataSize);

        case D3D12_FEATURE_VIDEO_ARCHITECTURE:
        case D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT:
        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR:
        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_SIZE:
        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_COUNT:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMANDS:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_PARAMETERS:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_SUPPORT:
        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_SIZE:
            return CheckOtherFeature(FeatureVideo, pFeatureSupportData, FeatureSupportDataSize);

        default:
            if (FeatureVideo >= D3D12_FEATURE_VIDEO_ENCODER_CODEC &&
                FeatureVideo <= D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS)
            {
                return CheckEncodeFeature(FeatureVideo, pFeatureSupportData, FeatureSupportDataSize);
            }
            return E_INVALIDARG;
    }
}

/* Decode */

/* The format count the driver gives a configuration, the profile having been listed */
static
HRESULT
RxDecodeFormatCount(
    _In_ const RxVideoState12& Video,
    _In_ UINT Node,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Configuration,
    _Out_ UINT* Count)
{
    D3D12DDI_VIDEO_DECODE_FORMAT_COUNT_DATA_0032 Driver = {};

    *Count = 0;
    Driver.NodeIndex = Node;
    RxToDdiConfiguration(Configuration, &Driver.Configuration);
    if (FAILED(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODE_FORMAT_COUNT, &Driver, sizeof(Driver))))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;

    *Count = Driver.FormatCount;
    return S_OK;
}

/* S_OK when the driver lists the format for the configuration, S_FALSE when it does not */
static
HRESULT
RxDecodeFormatListed(
    _In_ const RxVideoState12& Video,
    _In_ UINT Node,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Configuration,
    _In_ DXGI_FORMAT Format)
{
    D3D12DDI_VIDEO_DECODE_FORMATS_DATA_0020 Driver = {};
    DXGI_FORMAT Listed[32];
    UINT Count;
    UINT Index;
    HRESULT hr;

    hr = RxDecodeFormatCount(Video, Node, Configuration, &Count);
    if (FAILED(hr))
        return hr;
    if (Count > _countof(Listed))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;

    Driver.NodeIndex = Node;
    RxToDdiConfiguration(Configuration, &Driver.Configuration);
    Driver.FormatCount = Count;
    Driver.pOutputFormats = Listed;
    if (Count != 0 && FAILED(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_FORMATS, &Driver, sizeof(Driver))))
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;

    for (Index = 0; Index < Count; Index++)
    {
        if (Listed[Index] == Format)
            return S_OK;
    }
    return S_FALSE;
}

/* Whether a configuration names a listed profile with nothing the runtime does not know */
static
BOOLEAN
RxConfigurationKnown(
    _In_ const RxVideoState12& Video,
    _In_ UINT NodeIndex,
    _In_ const D3D12_VIDEO_DECODE_CONFIGURATION& Configuration)
{
    return Video.HasProfile(NodeIndex, Configuration.DecodeProfile) &&
           Configuration.BitstreamEncryption == D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE;
}

HRESULT
RxVideoDevice12::CheckDecodeFeature(
    _In_ D3D12_FEATURE_VIDEO Feature,
    _Inout_ VOID* Data,
    _In_ UINT Size)
{
    UINT Node;
    HRESULT hr;

    switch (Feature)
    {
        case D3D12_FEATURE_VIDEO_DECODE_PROFILE_COUNT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT* Count = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILE_COUNT*>(Data);

            if (Size != sizeof(*Count) || Video.NodeCaps(Count->NodeIndex) == nullptr)
                return E_INVALIDARG;
            Count->ProfileCount = Video.NodeCaps(Count->NodeIndex)->ProfileCount;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_DECODE_PROFILES:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES* Profiles = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_PROFILES*>(Data);
            const RxVideoNodeCaps12* Caps;

            if (Size != sizeof(*Profiles))
                return E_INVALIDARG;
            Caps = Video.NodeCaps(Profiles->NodeIndex);
            if (Caps == nullptr || Profiles->ProfileCount != Caps->ProfileCount || Profiles->pProfiles == nullptr)
                return E_INVALIDARG;
            memcpy(Profiles->pProfiles, Caps->Profiles, Caps->ProfileCount * sizeof(GUID));
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_DECODE_FORMAT_COUNT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT* Count = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_FORMAT_COUNT*>(Data);

            if (Size != sizeof(*Count) || !RxNode(Video, Count->NodeIndex, &Node))
                return E_INVALIDARG;
            Count->FormatCount = 0;
            if (!RxConfigurationKnown(Video, Count->NodeIndex, Count->Configuration))
                return S_OK;
            return RxDecodeFormatCount(Video, Node, Count->Configuration, &Count->FormatCount);
        }

        case D3D12_FEATURE_VIDEO_DECODE_FORMATS:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS* Formats = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_FORMATS*>(Data);
            D3D12DDI_VIDEO_DECODE_FORMATS_DATA_0020 Driver = {};
            UINT Count;

            if (Size != sizeof(*Formats) || !RxNode(Video, Formats->NodeIndex, &Node) ||
                !RxConfigurationKnown(Video, Formats->NodeIndex, Formats->Configuration))
            {
                return E_INVALIDARG;
            }

            /* The array must be exactly as long as the count the driver gives */
            hr = RxDecodeFormatCount(Video, Node, Formats->Configuration, &Count);
            if (FAILED(hr))
                return hr;
            if (Formats->FormatCount != Count || Formats->pOutputFormats == nullptr)
                return E_INVALIDARG;

            Driver.NodeIndex = Node;
            RxToDdiConfiguration(Formats->Configuration, &Driver.Configuration);
            Driver.FormatCount = Count;
            Driver.pOutputFormats = Formats->pOutputFormats;
            return RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_FORMATS, &Driver, sizeof(Driver)));
        }

        case D3D12_FEATURE_VIDEO_DECODE_SUPPORT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT* Support = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT*>(Data);
            D3D12DDI_VIDEO_DECODE_SUPPORT_DATA_0020 Driver = {};

            if (Size != sizeof(*Support) || !RxNode(Video, Support->NodeIndex, &Node) || Support->Width == 0 ||
                Support->Height == 0 ||
                (UINT)Support->Configuration.InterlaceType > D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED)
            {
                return E_INVALIDARG;
            }

            /* A profile the driver never listed is simply not supported */
            Support->SupportFlags = D3D12_VIDEO_DECODE_SUPPORT_FLAG_NONE;
            Support->ConfigurationFlags = D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_NONE;
            Support->DecodeTier = D3D12_VIDEO_DECODE_TIER_NOT_SUPPORTED;
            if (!RxConfigurationKnown(Video, Support->NodeIndex, Support->Configuration))
                return S_OK;

            /* Nor is a format the driver does not list for the configuration */
            hr = RxDecodeFormatListed(Video, Node, Support->Configuration, Support->DecodeFormat);
            if (hr != S_OK)
                return SUCCEEDED(hr) ? S_OK : hr;

            Driver.NodeIndex = Node;
            RxToDdiConfiguration(Support->Configuration, &Driver.Configuration);
            Driver.Width = Support->Width;
            Driver.Height = Support->Height;
            Driver.DecodeFormat = Support->DecodeFormat;
            Driver.FrameRate = Support->FrameRate;
            Driver.BitRate = Support->BitRate;
            hr = RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_SUPPORT, &Driver, sizeof(Driver)));
            if (FAILED(hr))
                return hr;

            Support->SupportFlags = (D3D12_VIDEO_DECODE_SUPPORT_FLAGS)Driver.SupportFlags;
            Support->ConfigurationFlags = (D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS)Driver.ConfigurationFlags;
            Support->DecodeTier = (D3D12_VIDEO_DECODE_TIER)Driver.DecodeTier;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_DECODE_CONVERSION_SUPPORT:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_CONVERSION_SUPPORT* Conversion =
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_CONVERSION_SUPPORT*>(Data);
            D3D12DDI_VIDEO_DECODE_CONVERSION_SUPPORT_DATA_0032 Driver = {};

            static_assert(sizeof(Conversion->ScaleSupport) == sizeof(Driver.ScaleSupport));
            if (Size != sizeof(*Conversion) || !RxNode(Video, Conversion->NodeIndex, &Node))
                return E_INVALIDARG;

            Conversion->SupportFlags = D3D12_VIDEO_DECODE_CONVERSION_SUPPORT_FLAG_NONE;
            ZeroMemory(&Conversion->ScaleSupport, sizeof(Conversion->ScaleSupport));
            if (!RxConfigurationKnown(Video, Conversion->NodeIndex, Conversion->Configuration))
                return S_OK;

            Driver.NodeIndex = Node;
            RxToDdiConfiguration(Conversion->Configuration, &Driver.Configuration);
            Driver.DecodeSample.Width = Conversion->DecodeSample.Width;
            Driver.DecodeSample.Height = Conversion->DecodeSample.Height;
            Driver.DecodeSample.Format.Format = Conversion->DecodeSample.Format.Format;
            Driver.DecodeSample.Format.ColorSpace = Conversion->DecodeSample.Format.ColorSpace;
            Driver.OutputFormat.Format = Conversion->OutputFormat.Format;
            Driver.OutputFormat.ColorSpace = Conversion->OutputFormat.ColorSpace;
            Driver.FrameRate = Conversion->FrameRate;
            Driver.BitRate = Conversion->BitRate;
            hr = RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_DECODE_CONVERSION_SUPPORT, &Driver,
                                              sizeof(Driver)));
            if (FAILED(hr))
                return hr;

            /* Scaling is only reported for a conversion that is supported */
            Conversion->SupportFlags = (D3D12_VIDEO_DECODE_CONVERSION_SUPPORT_FLAGS)Driver.SupportFlags;
            if ((Driver.SupportFlags & D3D12DDI_VIDEO_DECODE_CONVERSION_SUPPORT_FLAG_0020_SUPPORTED) != 0)
                memcpy(&Conversion->ScaleSupport, &Driver.ScaleSupport, sizeof(Conversion->ScaleSupport));
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE:
        case D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE1:
        {
            const BOOLEAN Newer = Feature == D3D12_FEATURE_VIDEO_DECODER_HEAP_SIZE1;
            D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE1 Request = {};
            D3D12DDI_VIDEO_DECODER_HEAP_SIZE_DATA_0072 Driver = {};
            const D3D12_VIDEO_DECODER_HEAP_DESC* Desc;

            if (Size != (Newer ? sizeof(D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE1)
                               : sizeof(D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE)))
            {
                return E_INVALIDARG;
            }

            if (Newer)
                memcpy(&Request, Data, sizeof(Request));
            else
                Request.VideoDecoderHeapDesc = static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE*>(Data)->VideoDecoderHeapDesc;
            Desc = &Request.VideoDecoderHeapDesc;
            if ((Desc->NodeMask & (Desc->NodeMask - 1)) != 0 || (Request.Protected && Video.GetVersion() < RxVideo0072))
                return E_INVALIDARG;

            hr = Device.TranslateNodeMask(Desc->NodeMask, TRUE, &Driver.NodeMask);
            if (FAILED(hr))
                return hr;
            RxToDdiConfiguration(Desc->Configuration, &Driver.Configuration);
            Driver.DecodeWidth = Desc->DecodeWidth;
            Driver.DecodeHeight = Desc->DecodeHeight;
            Driver.Format = Desc->Format;
            Driver.FrameRate = Desc->FrameRate;
            Driver.BitRate = Desc->BitRate;
            Driver.MaxDecodePictureBufferCount = Desc->MaxDecodePictureBufferCount;
            Driver.Protected = Request.Protected;

            /* Before protected resources the sizes follow the desc directly */
            if (Video.GetVersion() >= RxVideo0072)
            {
                hr = Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODER_HEAP_SIZE, &Driver, sizeof(Driver));
            }
            else
            {
                D3D12DDI_VIDEO_DECODER_HEAP_SIZE_DATA_0033 Older = {};

                memcpy(&Older.VideoDecoderHeapDesc, &Driver, sizeof(Older.VideoDecoderHeapDesc));
                hr = Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_DECODER_HEAP_SIZE, &Older, sizeof(Older));
                Driver.MemoryPoolL0Size = Older.MemoryPoolL0Size;
                Driver.MemoryPoolL1Size = Older.MemoryPoolL1Size;
            }
            hr = RxDriverAnswer(hr);
            if (FAILED(hr))
                return hr;

            if (Newer)
            {
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE1*>(Data)->MemoryPoolL0Size = Driver.MemoryPoolL0Size;
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE1*>(Data)->MemoryPoolL1Size = Driver.MemoryPoolL1Size;
            }
            else
            {
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE*>(Data)->MemoryPoolL0Size = Driver.MemoryPoolL0Size;
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODER_HEAP_SIZE*>(Data)->MemoryPoolL1Size = Driver.MemoryPoolL1Size;
            }
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_DECODE_HISTOGRAM:
            if (Video.GetVersion() < RxVideo0041)
                return E_INVALIDARG;
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_DECODE_HISTOGRAM, D3D12DDI_VIDEO_DECODE_HISTOGRAM_DATA_0041>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0040_DECODE_HISTOGRAM, Data, Size, TRUE);

        case D3D12_FEATURE_VIDEO_DECODE_PROTECTED_RESOURCES:
        {
            D3D12_FEATURE_DATA_VIDEO_DECODE_PROTECTED_RESOURCES* Protected =
                static_cast<D3D12_FEATURE_DATA_VIDEO_DECODE_PROTECTED_RESOURCES*>(Data);
            D3D12DDI_VIDEO_DECODE_PROTECTED_RESOURCES_DATA_0072 Driver = {};

            if (Size != sizeof(*Protected) || !RxNode(Video, Protected->NodeIndex, &Node))
                return E_INVALIDARG;

            Protected->SupportFlags = D3D12_VIDEO_PROTECTED_RESOURCE_SUPPORT_FLAG_NONE;
            if (Video.GetVersion() < RxVideo0072 || !RxConfigurationKnown(Video, Protected->NodeIndex, Protected->Configuration))
                return S_OK;

            Driver.NodeIndex = Node;
            RxToDdiConfiguration(Protected->Configuration, &Driver.Configuration);
            hr = RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0072_DECODE_PROTECTED_RESOURCES, &Driver,
                                              sizeof(Driver)));
            if (FAILED(hr))
                return hr;
            Protected->SupportFlags = (D3D12_VIDEO_PROTECTED_RESOURCE_SUPPORT_FLAGS)Driver.SupportFlags;
            return S_OK;
        }

        default:
            return E_INVALIDARG;
    }
}

/* Process */

/* The processor desc a size query names, in the form the driver at this version takes */
static
HRESULT
RxProcessorSize(
    _In_ RxDevice12& Device,
    _In_ const RxVideoState12& Video,
    _In_ UINT NodeMask,
    _In_ const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC& Output,
    _In_ UINT NumInputs,
    _In_reads_(NumInputs) const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC* Inputs,
    _In_ BOOL Protected,
    _Out_ UINT64* L0,
    _Out_ UINT64* L1)
{
    D3D12DDI_VIDEO_PROCESSOR_SIZE_DATA_0072 Driver = {};
    D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043* Streams;
    UINT Index;
    HRESULT hr;

    *L0 = 0;
    *L1 = 0;
    if ((NodeMask & (NodeMask - 1)) != 0 || NumInputs == 0 || Inputs == nullptr ||
        (Protected && Video.GetVersion() < RxVideo0072))
    {
        return E_INVALIDARG;
    }

    hr = Device.TranslateNodeMask(NodeMask, TRUE, &Driver.NodeMask);
    if (FAILED(hr))
        return hr;

    Streams = new (std::nothrow) D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0043[NumInputs]();
    if (Streams == nullptr)
        return E_OUTOFMEMORY;

    /* The 0043 stream desc is the API one without its field type */
    for (Index = 0; Index < NumInputs; Index++)
    {
        const BYTE* Source = reinterpret_cast<const BYTE*>(&Inputs[Index]);
        BYTE* Target = reinterpret_cast<BYTE*>(&Streams[Index]);
        const SIZE_T Split = offsetof(D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC, FieldType);

        memcpy(Target, Source, Split);
        memcpy(Target + Split, Source + Split + sizeof(Inputs[Index].FieldType), sizeof(Streams[Index]) - Split);
    }

    memcpy(&Driver.OutputStream, &Output, sizeof(Driver.OutputStream));
    Driver.pInputStreams = Streams;
    Driver.NumInputStreams = NumInputs;
    Driver.Protected = Protected;

    if (Video.GetVersion() >= RxVideo0072)
    {
        hr = Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_PROCESSOR_SIZE, &Driver, sizeof(Driver));
    }
    else if (Video.GetVersion() >= RxVideo0043)
    {
        D3D12DDI_VIDEO_PROCESSOR_SIZE_DATA_0043 Older = {};

        memcpy(&Older.VideoProcessorDesc, &Driver, sizeof(Older.VideoProcessorDesc));
        hr = Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_PROCESSOR_SIZE, &Older, sizeof(Older));
        Driver.MemoryPoolL0Size = Older.MemoryPoolL0Size;
        Driver.MemoryPoolL1Size = Older.MemoryPoolL1Size;
    }
    else
    {
        D3D12DDI_VIDEO_PROCESSOR_SIZE_DATA_0032 Oldest = {};

        Oldest.VideoProcessorDesc.NodeMask = Driver.NodeMask;
        Oldest.VideoProcessorDesc.OutputStream = Driver.OutputStream;
        Oldest.VideoProcessorDesc.pInputStreams =
            reinterpret_cast<D3D12DDI_VIDEO_PROCESSOR_INPUT_STREAM_DESC_0032*>(const_cast<D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC*>(Inputs));
        Oldest.VideoProcessorDesc.NumInputStreams = NumInputs;
        hr = Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0032_PROCESSOR_SIZE, &Oldest, sizeof(Oldest));
        Driver.MemoryPoolL0Size = Oldest.MemoryPoolL0Size;
        Driver.MemoryPoolL1Size = Oldest.MemoryPoolL1Size;
    }

    delete[] Streams;
    hr = RxDriverAnswer(hr);
    if (FAILED(hr))
        return hr;

    *L0 = Driver.MemoryPoolL0Size;
    *L1 = Driver.MemoryPoolL1Size;
    return S_OK;
}

HRESULT
RxVideoDevice12::CheckProcessFeature(
    _In_ D3D12_FEATURE_VIDEO Feature,
    _Inout_ VOID* Data,
    _In_ UINT Size)
{
    UINT Node;
    HRESULT hr;

    switch (Feature)
    {
        case D3D12_FEATURE_VIDEO_PROCESS_MAX_INPUT_STREAMS:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS* Streams =
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_MAX_INPUT_STREAMS*>(Data);

            if (Size != sizeof(*Streams) || Video.NodeCaps(Streams->NodeIndex) == nullptr)
                return E_INVALIDARG;
            Streams->MaxInputStreams = Video.NodeCaps(Streams->NodeIndex)->MaxInputStreams;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_PROCESS_SUPPORT:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT* Support = static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT*>(Data);
            D3D12DDI_VIDEO_PROCESS_SUPPORT_DATA_0032 Driver = {};
            const UINT Outputs = offsetof(D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT, SupportFlags);

            static_assert(sizeof(*Support) == sizeof(Driver));
            if (Size != sizeof(*Support) || !RxNode(Video, Support->NodeIndex, &Node))
                return E_INVALIDARG;

            /* The outputs start cleared and are only filled from an answer the driver gave */
            ZeroMemory(reinterpret_cast<BYTE*>(Support) + Outputs, sizeof(*Support) - Outputs);
            memcpy(&Driver, Support, Outputs);
            Driver.NodeIndex = Node;
            hr = RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_SUPPORT, &Driver, sizeof(Driver)));
            if (FAILED(hr))
                return hr;

            memcpy(reinterpret_cast<BYTE*>(Support) + Outputs, reinterpret_cast<const BYTE*>(&Driver) + Outputs,
                   sizeof(*Support) - Outputs);
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_PROCESS_REFERENCE_INFO:
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_PROCESS_REFERENCE_INFO,
                                 D3D12DDI_VIDEO_PROCESS_REFERENCE_INFO_DATA_0020>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0020_PROCESS_REFERENCE_INFO, Data, Size, TRUE);

        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE* Query = static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE*>(Data);

            if (Size != sizeof(*Query) || Query->pOutputStreamDesc == nullptr)
                return E_INVALIDARG;
            return RxProcessorSize(Device, Video, Query->NodeMask, *Query->pOutputStreamDesc, Query->NumInputStreamDescs,
                                   Query->pInputStreamDescs, FALSE, &Query->MemoryPoolL0Size, &Query->MemoryPoolL1Size);
        }

        case D3D12_FEATURE_VIDEO_PROCESSOR_SIZE1:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1* Query = static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESSOR_SIZE1*>(Data);

            if (Size != sizeof(*Query) || Query->pOutputStreamDesc == nullptr)
                return E_INVALIDARG;
            return RxProcessorSize(Device, Video, Query->NodeMask, *Query->pOutputStreamDesc, Query->NumInputStreamDescs,
                                   Query->pInputStreamDescs, Query->Protected, &Query->MemoryPoolL0Size,
                                   &Query->MemoryPoolL1Size);
        }

        case D3D12_FEATURE_VIDEO_PROCESS_PROTECTED_RESOURCES:
        {
            D3D12_FEATURE_DATA_VIDEO_PROCESS_PROTECTED_RESOURCES* Protected =
                static_cast<D3D12_FEATURE_DATA_VIDEO_PROCESS_PROTECTED_RESOURCES*>(Data);

            if (Size != sizeof(*Protected) || !RxNode(Video, Protected->NodeIndex, &Node))
                return E_INVALIDARG;
            Protected->SupportFlags = D3D12_VIDEO_PROTECTED_RESOURCE_SUPPORT_FLAG_NONE;
            if (Video.GetVersion() < RxVideo0072)
                return S_OK;
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_PROCESS_PROTECTED_RESOURCES,
                                 D3D12DDI_VIDEO_PROCESS_PROTECTED_RESOURCES_DATA_0072>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0072_PROCESS_PROTECTED_RESOURCES, Data, Size, TRUE);
        }

        default:
            return E_INVALIDARG;
    }
}

/* Motion estimation, extension commands and what the hardware is */

HRESULT
RxVideoDevice12::CheckOtherFeature(
    _In_ D3D12_FEATURE_VIDEO Feature,
    _Inout_ VOID* Data,
    _In_ UINT Size)
{
    UINT Node;
    HRESULT hr;

    switch (Feature)
    {
        case D3D12_FEATURE_VIDEO_ARCHITECTURE:
        {
            D3D12_FEATURE_DATA_VIDEO_ARCHITECTURE* Architecture = static_cast<D3D12_FEATURE_DATA_VIDEO_ARCHITECTURE*>(Data);
            D3D12DDICAPS_TYPE_VIDEO_ARCHITECTURE_DATA_0098 Driver = {};
            SYSTEM_INFO System;

            if (Size != sizeof(*Architecture))
                return E_INVALIDARG;

            /* x86 and x64 video engines are coherent with the CPU; on ARM only the driver can say */
            GetNativeSystemInfo(&System);
            Architecture->IOCoherent = TRUE;
            if (System.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_ARM &&
                System.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_ARM64)
            {
                return S_OK;
            }

            Architecture->IOCoherent = FALSE;
            if (Video.GetVersion() >= RxVideo0098 &&
                SUCCEEDED(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0098_ARCHITECTURE, &Driver, sizeof(Driver))))
            {
                Architecture->IOCoherent = Driver.IOCoherent;
            }
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_FEATURE_AREA_SUPPORT:
        {
            D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT* Areas = static_cast<D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT*>(Data);

            if (Size != sizeof(*Areas) || Video.NodeCaps(Areas->NodeIndex) == nullptr)
                return E_INVALIDARG;
            if (Video.GetVersion() < RxVideo0053)
            {
                Areas->VideoDecodeSupport = TRUE;
                Areas->VideoProcessSupport = TRUE;
                Areas->VideoEncodeSupport = FALSE;
                return S_OK;
            }

            hr = RxPassThrough<D3D12_FEATURE_DATA_VIDEO_FEATURE_AREA_SUPPORT, D3D12DDI_VIDEO_FEATURE_AREA_SUPPORT_DATA_0053>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0053_FEATURE_AREA_SUPPORT, Data, Size, TRUE);
            if (SUCCEEDED(hr) && !Video.NodeCaps(Areas->NodeIndex)->EncodeSupported)
                Areas->VideoEncodeSupport = FALSE;
            return hr;
        }

        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR:
            if (Video.GetVersion() < RxVideoMotionMinimum)
                return E_INVALIDARG;
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR, D3D12DDI_VIDEO_MOTION_ESTIMATOR_DATA_0060>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0053_MOTION_ESTIMATOR, Data, Size, TRUE);

        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_SIZE:
        {
            D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_SIZE* Query = static_cast<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_SIZE*>(Data);
            D3D12DDI_VIDEO_MOTION_ESTIMATOR_SIZE_DATA_0060 Driver = {};

            if (Size != sizeof(*Query) || Video.GetVersion() < RxVideoMotionMinimum || !RxNode(Video, Query->NodeIndex, &Node))
                return E_INVALIDARG;

            /* The driver names the node by mask here */
            Driver.NodeMask = 1u << Node;
            Driver.InputFormat = Query->InputFormat;
            Driver.BlockSize = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_0053)Query->BlockSize;
            Driver.Precision = (D3D12DDI_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_0053)Query->Precision;
            memcpy(&Driver.SizeRange, &Query->SizeRange, sizeof(Driver.SizeRange));
            Driver.Protected = Query->Protected;
            hr = RxDriverAnswer(Video.GetCaps(D3D12DDICAPS_TYPE_VIDEO_0053_MOTION_ESTIMATOR_SIZE, &Driver, sizeof(Driver)));
            if (FAILED(hr))
                return hr;

            Query->MotionVectorHeapMemoryPoolL0Size = Driver.MotionVectorHeapMemoryPoolL0Size;
            Query->MotionVectorHeapMemoryPoolL1Size = Driver.MotionVectorHeapMemoryPoolL1Size;
            Query->MotionEstimatorMemoryPoolL0Size = Driver.MotionEstimatorMemoryPoolL0Size;
            Query->MotionEstimatorMemoryPoolL1Size = Driver.MotionEstimatorMemoryPoolL1Size;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES:
        {
            D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES* Protected =
                static_cast<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES*>(Data);

            if (Size != sizeof(*Protected) || !RxNode(Video, Protected->NodeIndex, &Node))
                return E_INVALIDARG;
            Protected->SupportFlags = D3D12_VIDEO_PROTECTED_RESOURCE_SUPPORT_FLAG_NONE;
            if (Video.GetVersion() < RxVideo0072)
                return S_OK;
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES,
                                 D3D12DDI_VIDEO_MOTION_ESTIMATOR_PROTECTED_RESOURCES_DATA_0072>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0072_MOTION_ESTIMATOR_PROTECTED_RESOURCES, Data, Size, TRUE);
        }

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_COUNT:
        {
            D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_COUNT* Count =
                static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_COUNT*>(Data);

            if (Size != sizeof(*Count) || !RxNode(Video, Count->NodeIndex, &Node))
                return E_INVALIDARG;
            Count->CommandCount = Count->NodeIndex == 0 ? Video.GetExtensionCount() : 0;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMANDS:
        {
            D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMANDS* Commands = static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMANDS*>(Data);
            UINT Index;

            if (Size != sizeof(*Commands) || !RxNode(Video, Commands->NodeIndex, &Node) ||
                Commands->CommandCount != (Commands->NodeIndex == 0 ? Video.GetExtensionCount() : 0) ||
                (Commands->CommandCount != 0 && Commands->pCommandInfos == nullptr))
            {
                return E_INVALIDARG;
            }

            for (Index = 0; Index < Commands->CommandCount; Index++)
                Commands->pCommandInfos[Index] = Video.GetExtension(Index)->Info;
            return S_OK;
        }

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT:
            if (Size != sizeof(D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT) ||
                Video.FindExtension(static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT*>(Data)->CommandId) ==
                    nullptr)
            {
                return E_INVALIDARG;
            }
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT,
                                 D3D12DDI_VIDEO_EXTENSION_COMMAND_PARAMETER_COUNT_DATA_0063>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMAND_PARAMETER_COUNT, Data, Size, FALSE);

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_PARAMETERS:
            if (Size != sizeof(D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETERS) ||
                Video.FindExtension(static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETERS*>(Data)->CommandId) ==
                    nullptr)
            {
                return E_INVALIDARG;
            }
            static_assert(sizeof(D3D12_VIDEO_EXTENSION_COMMAND_PARAMETER_INFO) ==
                          sizeof(D3D12DDI_VIDEO_EXTENSION_COMMAND_PARAMETER_INFO_0063));
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_PARAMETERS,
                                 D3D12DDI_VIDEO_EXTENSION_COMMAND_PARAMETERS_DATA_0063>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMAND_PARAMETERS, Data, Size, FALSE);

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_SUPPORT:
            if (Size != sizeof(D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SUPPORT) ||
                Video.FindExtension(static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SUPPORT*>(Data)->CommandId) == nullptr)
            {
                return E_INVALIDARG;
            }
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SUPPORT,
                                 D3D12DDI_VIDEO_EXTENSION_COMMAND_CAPS_DATA_0063>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMAND_CAPS, Data, Size, TRUE);

        case D3D12_FEATURE_VIDEO_EXTENSION_COMMAND_SIZE:
            if (Size != sizeof(D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SIZE) ||
                Video.FindExtension(static_cast<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SIZE*>(Data)->CommandId) == nullptr)
            {
                return E_INVALIDARG;
            }
            return RxPassThrough<D3D12_FEATURE_DATA_VIDEO_EXTENSION_COMMAND_SIZE,
                                 D3D12DDI_VIDEO_EXTENSION_COMMAND_SIZE_DATA_0063>(
                Video, D3D12DDICAPS_TYPE_VIDEO_0063_EXTENSION_COMMAND_SIZE, Data, Size, TRUE);

        default:
            return E_INVALIDARG;
    }
}

/* Encode */

/*
 * One encode query: the API data size, its driver caps type, the support field to clear on a refusal and
 * where the codec is. Heap size queries name their node by mask; every other one leads with a node index.
 */
struct RxEncodeQuery
{
    D3D12_FEATURE_VIDEO Feature;
    UINT Size;
    D3D12DDICAPS_TYPE_VIDEO_0020 Type;
    UINT MinimumVersion;
    UINT SupportedOffset;
    UINT CodecOffset;
    BOOLEAN NodeByMask;
};

#define RX_ENCODE_QUERY(Feature, Api, Ddi, Type, Version, Supported, Codec, NodeByMask)                              \
    { Feature, sizeof(Api), Type, Version, offsetof(Api, Supported), offsetof(Api, Codec), NodeByMask }

/* The driver data of every query the table passes through is laid out as the API's */
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_CODEC_DATA_0080));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_PROFILE_LEVEL) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_PROFILE_LEVEL_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_OUTPUT_RESOLUTION_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_INPUT_FORMAT) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_INPUT_FORMAT_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_RATE_CONTROL_MODE_DATA_0080));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_INTRA_REFRESH_MODE) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_INTRA_REFRESH_MODE_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_HEAP_SIZE) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_HEAP_SIZE_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_DATA_0083_0));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_SUPPORT_DATA_0083_0));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_RESOURCE_REQUIREMENTS_DATA_0080_2));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG) ==
              sizeof(D3D12DDI_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG_0095));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_SUPPORT1_DATA_0096));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1_DATA_0102));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_SUPPORT2_DATA_0107));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_HEAP_SIZE1) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_HEAP_SIZE_DATA_0110_0));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_0103));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_QPMAP_INPUT) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_QPMAP_INPUT_0103));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_DIRTY_REGIONS) == sizeof(D3D12DDICAPS_ENCODER_DIRTY_REGIONS_0103));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_MOTION_SEARCH) == sizeof(D3D12DDICAPS_VIDEO_ENCODER_MOTION_SEARCH_0107));
static_assert(sizeof(D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS) ==
              sizeof(D3D12DDICAPS_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS_DATA_0110_0));

static const RxEncodeQuery RxEncodeQueries[] =
{
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_CODEC, D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC,
                    D3D12DDICAPS_VIDEO_ENCODER_CODEC_DATA_0080, D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_CODEC,
                    RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_PROFILE_LEVEL, D3D12_FEATURE_DATA_VIDEO_ENCODER_PROFILE_LEVEL,
                    D3D12DDICAPS_VIDEO_ENCODER_PROFILE_LEVEL_DATA_0080_2, D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_PROFILE_LEVEL,
                    RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT,
                    D3D12DDICAPS_VIDEO_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_OUTPUT_RESOLUTION_RATIOS_COUNT, RxVideoEncodeMinimum,
                    ResolutionRatiosCount, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION, D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION,
                    D3D12DDICAPS_VIDEO_ENCODER_OUTPUT_RESOLUTION_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_OUTPUT_RESOLUTION, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_INPUT_FORMAT, D3D12_FEATURE_DATA_VIDEO_ENCODER_INPUT_FORMAT,
                    D3D12DDICAPS_VIDEO_ENCODER_INPUT_FORMAT_DATA_0080_2, D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_INPUT_FORMAT,
                    RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_MODE, D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_MODE,
                    D3D12DDICAPS_VIDEO_ENCODER_RATE_CONTROL_MODE_DATA_0080,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_RATE_CONTROL_MODE, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_INTRA_REFRESH_MODE, D3D12_FEATURE_DATA_VIDEO_ENCODER_INTRA_REFRESH_MODE,
                    D3D12DDICAPS_VIDEO_ENCODER_INTRA_REFRESH_MODE_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_INTRA_REFRESH_MODE, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE,
                    D3D12DDICAPS_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_FRAME_SUBREGION_LAYOUT_MODE, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_HEAP_SIZE, D3D12_FEATURE_DATA_VIDEO_ENCODER_HEAP_SIZE,
                    D3D12DDICAPS_VIDEO_ENCODER_HEAP_SIZE_DATA_0080_2, D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_HEAP_SIZE,
                    RxVideoEncodeMinimum, IsSupported, HeapDesc.EncodeCodec, TRUE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT,
                    D3D12DDICAPS_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_DATA_0083_0,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_CODEC_CONFIGURATION_SUPPORT, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT,
                    D3D12DDICAPS_VIDEO_ENCODER_SUPPORT_DATA_0083_0, D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_SUPPORT,
                    RxVideoEncodeMinimum, SupportFlags, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT,
                    D3D12DDICAPS_VIDEO_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_CODEC_PICTURE_CONTROL_SUPPORT, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS, D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS,
                    D3D12DDICAPS_VIDEO_ENCODER_RESOURCE_REQUIREMENTS_DATA_0080_2,
                    D3D12DDICAPS_TYPE_VIDEO_0080_ENCODER_RESOURCE_REQUIREMENTS, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG,
                    D3D12DDI_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG_0095,
                    D3D12DDICAPS_TYPE_VIDEO_0095_ENCODER_FRAME_SUBREGION_LAYOUT_CONFIG, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1,
                    D3D12DDICAPS_VIDEO_ENCODER_SUPPORT1_DATA_0096, D3D12DDICAPS_TYPE_VIDEO_0096_ENCODER_SUPPORT1,
                    RxVideoEncodeMinimum, SupportFlags, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1,
                    D3D12DDICAPS_VIDEO_ENCODER_RESOURCE_REQUIREMENTS1_DATA_0102,
                    D3D12DDICAPS_TYPE_VIDEO_0102_ENCODER_RESOURCE_REQUIREMENTS1, RxVideoEncodeMinimum, IsSupported, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT2, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT2,
                    D3D12DDICAPS_VIDEO_ENCODER_SUPPORT2_DATA_0107, D3D12DDICAPS_TYPE_VIDEO_0107_ENCODER_SUPPORT2,
                    RxVideoEncodeMinimum, SupportFlags, Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_HEAP_SIZE1, D3D12_FEATURE_DATA_VIDEO_ENCODER_HEAP_SIZE1,
                    D3D12DDICAPS_VIDEO_ENCODER_HEAP_SIZE_DATA_0110_0, D3D12DDICAPS_TYPE_VIDEO_0110_ENCODER_HEAP_SIZE1,
                    RxVideoEncodeMinimum, IsSupported, HeapDesc.EncodeCodec, TRUE),
RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT,
                    D3D12DDICAPS_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT_0103,
                    D3D12DDICAPS_TYPE_VIDEO_0103_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT, RxVideo0103, IsSupported,
                    SessionInfo.Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_QPMAP_INPUT, D3D12_FEATURE_DATA_VIDEO_ENCODER_QPMAP_INPUT,
                    D3D12DDICAPS_VIDEO_ENCODER_QPMAP_INPUT_0103, D3D12DDICAPS_TYPE_VIDEO_0103_ENCODER_QPMAP_INPUT, RxVideo0103,
                    IsSupported, SessionInfo.Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_DIRTY_REGIONS, D3D12_FEATURE_DATA_VIDEO_ENCODER_DIRTY_REGIONS,
                    D3D12DDICAPS_ENCODER_DIRTY_REGIONS_0103, D3D12DDICAPS_TYPE_VIDEO_0103_ENCODER_DIRTY_REGIONS, RxVideo0103,
                    SupportFlags, SessionInfo.Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_MOTION_SEARCH, D3D12_FEATURE_DATA_VIDEO_ENCODER_MOTION_SEARCH,
                    D3D12DDICAPS_VIDEO_ENCODER_MOTION_SEARCH_0107, D3D12DDICAPS_TYPE_VIDEO_0107_ENCODER_MOTION_SEARCH,
                    D3D12DDI_FEATURE_VERSION_VIDEO_0107_0, SupportFlags, SessionInfo.Codec, FALSE),
    RX_ENCODE_QUERY(D3D12_FEATURE_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS,
                    D3D12_FEATURE_DATA_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS,
                    D3D12DDICAPS_VIDEO_ENCODER_RATE_CONTROL_FRAME_ANALYSIS_DATA_0110_0,
                    D3D12DDICAPS_TYPE_VIDEO_0110_ENCODER_RATE_CONTROL_FRAME_ANALYSIS, RxVideo0110, SupportFlags, Codec, FALSE),
};
#undef RX_ENCODE_QUERY

HRESULT
RxVideoDevice12::CheckEncodeFeature(
    _In_ D3D12_FEATURE_VIDEO Feature,
    _Inout_ VOID* Data,
    _In_ UINT Size)
{
    const RxEncodeQuery* Query = nullptr;
    const RxVideoNodeCaps12* Caps;
    D3D12_VIDEO_ENCODER_CODEC Codec;
    BYTE Driver[0x400];
    UINT NodeIndex;
    UINT Node;
    UINT Index;
    HRESULT hr;

    for (Index = 0; Index < _countof(RxEncodeQueries); Index++)
    {
        if (RxEncodeQueries[Index].Feature == Feature)
            Query = &RxEncodeQueries[Index];
    }

    if (Query == nullptr || Size != Query->Size || Size > sizeof(Driver))
        return E_INVALIDARG;

    memcpy(&NodeIndex, Data, sizeof(NodeIndex));
    memcpy(&Codec, static_cast<const BYTE*>(Data) + Query->CodecOffset, sizeof(Codec));

    /* A heap on a node the device does not have is not supported; a bad node index is a bad query */
    ZeroMemory(static_cast<BYTE*>(Data) + Query->SupportedOffset, sizeof(UINT));
    if (Query->NodeByMask)
    {
        UINT Mask = NodeIndex;

        if (FAILED(Device.TranslateNodeMask(Mask, TRUE, &Node)))
            return S_OK;
        for (NodeIndex = 0; Mask > 1; Mask >>= 1)
            NodeIndex++;
    }
    else if (!RxNode(Video, NodeIndex, &Node))
    {
        return E_INVALIDARG;
    }

    /* A node without encoding, or a codec the runtime does not know, is answered as unsupported */
    Caps = Video.NodeCaps(NodeIndex);
    if (Caps == nullptr)
        return S_OK;
    if (!Caps->EncodeSupported || Video.GetVersion() < Query->MinimumVersion ||
        (UINT)Codec > D3D12_VIDEO_ENCODER_CODEC_AV1)
    {
        return S_OK;
    }

    /* A count of ratios is answered for the first query and checked against for the second */
    if (Feature == D3D12_FEATURE_VIDEO_ENCODER_OUTPUT_RESOLUTION)
    {
        const D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION* Resolution =
            static_cast<const D3D12_FEATURE_DATA_VIDEO_ENCODER_OUTPUT_RESOLUTION*>(Data);

        if (Resolution->ResolutionRatiosCount == 0 || Resolution->pResolutionRatios == nullptr)
            return E_INVALIDARG;
    }

    memcpy(Driver, Data, Size);
    memcpy(Driver, &Node, sizeof(Node));
    hr = Video.GetCaps(Query->Type, Driver, Size);
    if (FAILED(hr))
    {
        RX_TRACE(L"driver encode query %d failed 0x%08lX", Feature, hr);
        return DXGI_ERROR_DRIVER_INTERNAL_ERROR;
    }

    /* The node goes back as the caller named it */
    memcpy(&Node, Data, sizeof(Node));
    memcpy(Data, Driver, Size);
    memcpy(Data, &Node, sizeof(Node));

    /* Input maps are not offered yet: the driver's other answers stand, its support does not */
    if (Feature == D3D12_FEATURE_VIDEO_ENCODER_QPMAP_INPUT || Feature == D3D12_FEATURE_VIDEO_ENCODER_DIRTY_REGIONS ||
        Feature == D3D12_FEATURE_VIDEO_ENCODER_RESOLVE_INPUT_PARAM_LAYOUT)
    {
        ZeroMemory(static_cast<BYTE*>(Data) + Query->SupportedOffset, sizeof(UINT));
    }
    return S_OK;
}
