/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Encode synthetic test clips and score decoded dumps against the source pattern
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

/*
 * maketestclips [-out <dir>]
 *     Writes synthetic-<codec>-<w>x<h>-<fps>.mp4 with the system's hardware encoders,
 *     and a WebM copy of every AV1 stream.
 *
 * maketestclips -verify <dump dir>
 *     Scores NativeVideoDecode -dump output (8 bit NV12) against the regenerated pattern.
 */

#include "clips.hpp"

#include <codecapi.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <strmif.h>

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string>

static
HRESULT
NvdSetupWriter(
    _In_ IMFDXGIDeviceManager* Manager,
    _In_z_ PCWSTR Path,
    _In_ IMFMediaType* Output,
    _In_ IMFMediaType* Input,
    _Out_ ComPtr<IMFSinkWriter>& Writer,
    _Out_ DWORD* Stream)
{
    ComPtr<IMFAttributes> Attributes;
    HRESULT hr;

    hr = MFCreateAttributes(&Attributes, 2);
    if (SUCCEEDED(hr))
        hr = Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (SUCCEEDED(hr))
        hr = Attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, Manager);
    if (SUCCEEDED(hr))
        hr = MFCreateSinkWriterFromURL(Path, nullptr, Attributes.Get(), &Writer);
    if (SUCCEEDED(hr))
        hr = Writer->AddStream(Output, Stream);
    if (SUCCEEDED(hr))
        hr = Writer->SetInputMediaType(*Stream, Input, nullptr);
    return hr;
}

/* H.264: the sink writer finds and drives the encoder on its own */
static
HRESULT
EncodeWithSinkWriter(
    _In_ IMFDXGIDeviceManager* Manager,
    _In_ const ClipSpec& Spec,
    _In_z_ PCWSTR Path)
{
    ComPtr<IMFSinkWriter> Writer;
    ComPtr<IMFMediaType> Output;
    ComPtr<IMFMediaType> Input;
    ComPtr<ICodecAPI> Codec;
    std::vector<UINT8> Frame;
    VARIANT Value;
    DWORD Stream = 0;
    UINT64 Duration = 10000000ull / Spec.FrameRate;
    UINT Index;
    HRESULT hr;

    hr = MFCreateMediaType(&Output);
    if (SUCCEEDED(hr))
        hr = MFCreateMediaType(&Input);
    if (FAILED(hr))
        return hr;

    Output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    Output->SetGUID(MF_MT_SUBTYPE, Spec.Subtype);
    Output->SetUINT32(MF_MT_AVG_BITRATE, Spec.Bitrate);
    Output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    Output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    MFSetAttributeSize(Output.Get(), MF_MT_FRAME_SIZE, Spec.Width, Spec.Height);
    MFSetAttributeRatio(Output.Get(), MF_MT_FRAME_RATE, Spec.FrameRate, 1);
    MFSetAttributeRatio(Output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    Input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    Input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    Input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(Input.Get(), MF_MT_FRAME_SIZE, Spec.Width, Spec.Height);
    MFSetAttributeRatio(Input.Get(), MF_MT_FRAME_RATE, Spec.FrameRate, 1);
    MFSetAttributeRatio(Input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);

    hr = NvdSetupWriter(Manager, Path, Output.Get(), Input.Get(), Writer, &Stream);
    if (FAILED(hr))
    {
        wprintf(L"  %s: sink writer setup failed 0x%08lX\n", Spec.CodecName, hr);
        return hr;
    }

    /* A key frame every two seconds gives seeking something to land on */
    if (SUCCEEDED(Writer->GetServiceForStream(Stream, GUID_NULL, IID_PPV_ARGS(&Codec))))
    {
        VariantInit(&Value);
        Value.vt = VT_UI4;
        Value.ulVal = Spec.FrameRate * 2;
        Codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &Value);
    }

    hr = Writer->BeginWriting();
    for (Index = 0; SUCCEEDED(hr) && Index < Spec.Frames; Index++)
    {
        ComPtr<IMFMediaBuffer> Buffer;
        ComPtr<IMFSample> Sample;
        BYTE* Data = nullptr;

        GeneratePattern(Frame, Spec.Width, Spec.Height, Index);
        hr = MFCreateMemoryBuffer((DWORD)Frame.size(), &Buffer);
        if (SUCCEEDED(hr))
            hr = Buffer->Lock(&Data, nullptr, nullptr);
        if (SUCCEEDED(hr))
        {
            memcpy(Data, Frame.data(), Frame.size());
            Buffer->Unlock();
            hr = Buffer->SetCurrentLength((DWORD)Frame.size());
        }
        if (SUCCEEDED(hr))
            hr = MFCreateSample(&Sample);
        if (SUCCEEDED(hr))
            hr = Sample->AddBuffer(Buffer.Get());
        if (SUCCEEDED(hr))
            hr = Sample->SetSampleTime(Index * Duration);
        if (SUCCEEDED(hr))
            hr = Sample->SetSampleDuration(Duration);
        if (SUCCEEDED(hr))
            hr = Writer->WriteSample(Stream, Sample.Get());
    }

    if (SUCCEEDED(hr))
        hr = Writer->Finalize();

    wprintf(L"  %-5s %ux%u %u fps %u frames -> %s (0x%08lX)\n",
            Spec.CodecName, Spec.Width, Spec.Height, Spec.FrameRate, Spec.Frames, Path, hr);
    return hr;
}

/* Already compressed units into MP4: input and output types match, so no encoder is involved */
static
HRESULT
WriteMp4Passthrough(
    _In_ IMFDXGIDeviceManager* Manager,
    _In_ const ClipSpec& Spec,
    _In_ IMFMediaType* Type,
    _In_ const std::vector<EncodedUnit>& Units,
    _In_z_ PCWSTR Path)
{
    ComPtr<IMFSinkWriter> Writer;
    ComPtr<IMFMediaType> Declared;
    DWORD Stream = 0;
    UINT64 Duration = 10000000ull / Spec.FrameRate;
    HRESULT hr;

    /* Some encoders report their padded coded size; the container should carry the real picture size */
    hr = MFCreateMediaType(&Declared);
    if (SUCCEEDED(hr))
        hr = Type->CopyAllItems(Declared.Get());
    if (SUCCEEDED(hr))
        hr = MFSetAttributeSize(Declared.Get(), MF_MT_FRAME_SIZE, Spec.Width, Spec.Height);
    if (SUCCEEDED(hr))
        hr = NvdSetupWriter(Manager, Path, Declared.Get(), Declared.Get(), Writer, &Stream);
    if (SUCCEEDED(hr))
        hr = Writer->BeginWriting();

    for (const EncodedUnit& Unit : Units)
    {
        ComPtr<IMFMediaBuffer> Buffer;
        ComPtr<IMFSample> Sample;
        BYTE* Data = nullptr;

        if (FAILED(hr))
            break;

        hr = MFCreateMemoryBuffer((DWORD)Unit.Data.size(), &Buffer);
        if (SUCCEEDED(hr))
            hr = Buffer->Lock(&Data, nullptr, nullptr);
        if (SUCCEEDED(hr))
        {
            memcpy(Data, Unit.Data.data(), Unit.Data.size());
            Buffer->Unlock();
            hr = Buffer->SetCurrentLength((DWORD)Unit.Data.size());
        }
        if (SUCCEEDED(hr))
            hr = MFCreateSample(&Sample);
        if (SUCCEEDED(hr))
            hr = Sample->AddBuffer(Buffer.Get());
        if (SUCCEEDED(hr))
            hr = Sample->SetSampleTime(Unit.Time);
        if (SUCCEEDED(hr))
            hr = Sample->SetSampleDuration(Duration);
        if (SUCCEEDED(hr) && Unit.IsKeyFrame)
            hr = Sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
        if (SUCCEEDED(hr))
            hr = Writer->WriteSample(Stream, Sample.Get());
    }

    if (SUCCEEDED(hr))
        hr = Writer->Finalize();

    wprintf(L"  %-5s %ux%u %u fps %zu units -> %s (0x%08lX)\n",
            Spec.CodecName, Spec.Width, Spec.Height, Spec.FrameRate, Units.size(), Path, hr);
    return hr;
}

static
int
VerifyDumps(
    _In_z_ PCWSTR Directory)
{
    WIN32_FIND_DATAW Found;
    std::vector<UINT8> Expected;
    std::vector<UINT8> Actual;
    std::wstring Pattern = std::wstring(Directory) + L"\\frame_*.nv12";
    double Worst = 1e9;
    double Total = 0;
    UINT Frames = 0;
    UINT Index;
    UINT Width;
    UINT Height;
    HANDLE Search;
    FILE* File;

    Search = FindFirstFileW(Pattern.c_str(), &Found);
    if (Search == INVALID_HANDLE_VALUE)
    {
        wprintf(L"no frame_*.nv12 dumps in %s\n", Directory);
        return 1;
    }

    do
    {
        double Error = 0;
        double Difference;
        double Psnr;
        SIZE_T Sample;
        SIZE_T LumaSize;

        if (swscanf_s(Found.cFileName, L"frame_%u_%ux%u.nv12", &Index, &Width, &Height) != 3)
            continue;

        std::wstring Path = std::wstring(Directory) + L"\\" + Found.cFileName;
        if (_wfopen_s(&File, Path.c_str(), L"rb") != 0 || File == nullptr)
            continue;
        Actual.resize((SIZE_T)Width * Height * 3 / 2);
        fread(Actual.data(), 1, Actual.size(), File);
        fclose(File);

        /* Luma only: encoder chroma loss says little about decoder correctness */
        GeneratePattern(Expected, Width, Height, Index);
        LumaSize = (SIZE_T)Width * Height;
        for (Sample = 0; Sample < LumaSize; Sample++)
        {
            Difference = (double)Expected[Sample] - Actual[Sample];
            Error += Difference * Difference;
        }
        Error /= (double)LumaSize;
        Psnr = (Error == 0) ? 99.0 : 10.0 * log10(255.0 * 255.0 / Error);

        /* For the first frame, show which 16 row bands carry the error */
        if (Frames == 0 && Psnr < 45.0)
        {
            UINT Band;
            UINT Row;
            UINT Column;

            for (Band = 0; Band < (Height + 15) / 16; Band++)
            {
                double BandError = 0;
                UINT Samples = 0;

                for (Row = Band * 16; Row < std::min(Height, Band * 16 + 16); Row++)
                {
                    for (Column = 0; Column < Width; Column++)
                    {
                        Difference = (double)Expected[(SIZE_T)Row * Width + Column] -
                                     Actual[(SIZE_T)Row * Width + Column];
                        BandError += Difference * Difference;
                        Samples++;
                    }
                }

                BandError /= Samples;
                if (BandError > 4.0)
                    wprintf(L"    rows %4u-%4u  mean squared error %.1f\n", Band * 16, Band * 16 + 15, BandError);
            }
        }

        wprintf(L"  frame %5u  %ux%u  Y PSNR %6.2f dB%s\n", Index, Width, Height, Psnr,
                (Psnr < 30.0) ? L"  << LOW" : L"");
        Worst = std::min(Worst, Psnr);
        Total += Psnr;
        Frames++;
    } while (FindNextFileW(Search, &Found));
    FindClose(Search);

    if (Frames == 0)
        return 1;

    wprintf(L"%u frames, average %.2f dB, worst %.2f dB: %s\n", Frames, Total / Frames, Worst,
            (Worst >= 30.0) ? L"PASS" : L"FAIL");
    return (Worst >= 30.0) ? 0 : 1;
}

int
wmain(
    _In_ int argc,
    _In_reads_(argc) wchar_t** argv)
{
    static const ClipSpec Clips[] =
    {
        { MFVideoFormat_AV1, L"av1", 1280, 720, 30, 300, 4000000 },
        { MFVideoFormat_AV1, L"av1", 1920, 1080, 60, 300, 8000000 },
        { MFVideoFormat_H264, L"h264", 1280, 720, 30, 300, 4000000 },
    };
    std::vector<EncodedUnit> Units;
    ComPtr<IMFDXGIDeviceManager> Manager;
    ComPtr<ID3D10Multithread> Multithread;
    ComPtr<IMFMediaType> OutputType;
    ComPtr<ID3D11Device> Device;
    std::wstring OutDirectory = L"C:\\DevEnv\\PRs\\SampleVideos";
    WCHAR Path[MAX_PATH];
    UINT ResetToken = 0;
    HRESULT hr;
    int Failures = 0;

    if (argc == 3 && _wcsicmp(argv[1], L"-verify") == 0)
        return VerifyDumps(argv[2]);
    if (argc == 3 && _wcsicmp(argv[1], L"-out") == 0)
    {
        OutDirectory = argv[2];
    }
    else if (argc != 1)
    {
        fwprintf(stderr, L"usage: maketestclips [-out <dir>] | -verify <dump dir>\n");
        return 2;
    }

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    /* Hardware encoders want a D3D11 device shared through a DXGI device manager */
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                           D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                           nullptr, 0, D3D11_SDK_VERSION, &Device, nullptr, nullptr);
    if (SUCCEEDED(hr) && SUCCEEDED(Device.As(&Multithread)))
        Multithread->SetMultithreadProtected(TRUE);
    if (SUCCEEDED(hr))
        hr = MFCreateDXGIDeviceManager(&ResetToken, &Manager);
    if (SUCCEEDED(hr))
        hr = Manager->ResetDevice(Device.Get(), ResetToken);
    if (FAILED(hr))
    {
        fwprintf(stderr, L"error: D3D11 device for the encoders failed 0x%08lX\n", hr);
        return 1;
    }

    wprintf(L"writing synthetic clips to %s\n", OutDirectory.c_str());
    for (const ClipSpec& Spec : Clips)
    {
        swprintf_s(Path, L"%s\\synthetic-%s-%ux%u-%u.mp4", OutDirectory.c_str(), Spec.CodecName,
                   Spec.Width, Spec.Height, Spec.FrameRate);

        if (Spec.Subtype != MFVideoFormat_AV1)
        {
            if (FAILED(EncodeWithSinkWriter(Manager.Get(), Spec, Path)))
                Failures++;
            continue;
        }

        if (FAILED(EncodeWithMft(Device.Get(), Manager.Get(), Spec, Units, OutputType)))
        {
            Failures++;
            continue;
        }

        if (FAILED(WriteMp4Passthrough(Manager.Get(), Spec, OutputType.Get(), Units, Path)))
        {
            DeleteFileW(Path);
            Failures++;
        }

        swprintf_s(Path, L"%s\\synthetic-%s-%ux%u-%u.webm", OutDirectory.c_str(), Spec.CodecName,
                   Spec.Width, Spec.Height, Spec.FrameRate);
        if (FAILED(WriteAv1WebM(Path, Spec, Units)))
            Failures++;
    }

    MFShutdown();
    CoUninitialize();
    return Failures;
}
