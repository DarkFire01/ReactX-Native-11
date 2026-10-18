/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Drives an asynchronous hardware encoder MFT directly
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

/* Adapter scoped MFT enumeration needs Windows 10 RS1; this host only tool may assume it */
#undef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000002

#include "clips.hpp"

#include <codecapi.h>
#include <dxgi.h>
#include <mferror.h>
#include <strmif.h>

#include <algorithm>
#include <stdio.h>

static
HRESULT
NvdMakeInputSample(
    _In_ ID3D11Device* Device,
    _In_ const ClipSpec& Spec,
    _In_ UINT Index,
    _Out_ ComPtr<IMFSample>& Sample)
{
    D3D11_TEXTURE2D_DESC Desc = {};
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11Texture2D> Texture;
    ComPtr<IMFMediaBuffer> Buffer;
    ComPtr<IMF2DBuffer> Buffer2D;
    std::vector<UINT8> Frame;
    DWORD Length = 0;
    UINT64 Duration = 10000000ull / Spec.FrameRate;
    HRESULT hr;

    GeneratePattern(Frame, Spec.Width, Spec.Height, Index);

    Desc.Width = Spec.Width;
    Desc.Height = Spec.Height;
    Desc.MipLevels = 1;
    Desc.ArraySize = 1;
    Desc.Format = DXGI_FORMAT_NV12;
    Desc.SampleDesc.Count = 1;
    Desc.Usage = D3D11_USAGE_DEFAULT;
    Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    /* A fresh texture per frame: the encoder may still hold earlier ones */
    hr = Device->CreateTexture2D(&Desc, nullptr, &Texture);
    if (FAILED(hr))
        return hr;

    Device->GetImmediateContext(&Context);
    Context->UpdateSubresource(Texture.Get(), 0, nullptr, Frame.data(), Spec.Width, 0);

    hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), Texture.Get(), 0, FALSE, &Buffer);
    if (SUCCEEDED(hr) && SUCCEEDED(Buffer.As(&Buffer2D)) && SUCCEEDED(Buffer2D->GetContiguousLength(&Length)))
        hr = Buffer->SetCurrentLength(Length);
    if (SUCCEEDED(hr))
        hr = MFCreateSample(&Sample);
    if (SUCCEEDED(hr))
        hr = Sample->AddBuffer(Buffer.Get());
    if (SUCCEEDED(hr))
        hr = Sample->SetSampleTime(Index * Duration);
    if (SUCCEEDED(hr))
        hr = Sample->SetSampleDuration(Duration);
    return hr;
}

static
HRESULT
NvdCollectOutput(
    _In_ IMFTransform* Transform,
    _Inout_ std::vector<EncodedUnit>& Units)
{
    MFT_OUTPUT_STREAM_INFO StreamInfo = {};
    MFT_OUTPUT_DATA_BUFFER Output = {};
    ComPtr<IMFMediaBuffer> Contiguous;
    ComPtr<IMFMediaBuffer> Allocated;
    ComPtr<IMFMediaType> Available;
    ComPtr<IMFSample> Provided;
    EncodedUnit Unit;
    DWORD Status = 0;
    DWORD Length = 0;
    BYTE* Data = nullptr;
    LONGLONG Time = 0;
    HRESULT hr;

    /* Most hardware encoders hand out their own samples; the rest get one from us */
    Transform->GetOutputStreamInfo(0, &StreamInfo);
    if (!(StreamInfo.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)))
    {
        hr = MFCreateSample(&Provided);
        if (SUCCEEDED(hr))
            hr = MFCreateMemoryBuffer(std::max<DWORD>(StreamInfo.cbSize, 4 * 1024 * 1024), &Allocated);
        if (SUCCEEDED(hr))
            hr = Provided->AddBuffer(Allocated.Get());
        if (FAILED(hr))
            return hr;
        Output.pSample = Provided.Get();
    }

    hr = Transform->ProcessOutput(0, 1, &Output, &Status);
    if (Output.pEvents != nullptr)
        Output.pEvents->Release();

    if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
    {
        hr = Transform->GetOutputAvailableType(0, 0, &Available);
        if (SUCCEEDED(hr))
            hr = Transform->SetOutputType(0, Available.Get(), 0);
        return hr;
    }
    if (FAILED(hr))
        return hr;

    ComPtr<IMFSample> Sample = Output.pSample;
    if (Provided == nullptr && Output.pSample != nullptr)
        Output.pSample->Release();

    hr = Sample->ConvertToContiguousBuffer(&Contiguous);
    if (SUCCEEDED(hr))
        hr = Contiguous->Lock(&Data, nullptr, &Length);
    if (FAILED(hr))
        return hr;

    Unit.Data.assign(Data, Data + Length);
    Contiguous->Unlock();
    Sample->GetSampleTime(&Time);
    Unit.Time = Time;
    Unit.IsKeyFrame = MFGetAttributeUINT32(Sample.Get(), MFSampleExtension_CleanPoint, FALSE) != 0;
    Units.push_back(std::move(Unit));
    return S_OK;
}

HRESULT
EncodeWithMft(
    _In_ ID3D11Device* Device,
    _In_ IMFDXGIDeviceManager* Manager,
    _In_ const ClipSpec& Spec,
    _Out_ std::vector<EncodedUnit>& Units,
    _Out_ ComPtr<IMFMediaType>& OutputType)
{
    MFT_REGISTER_TYPE_INFO InputInfo = { MFMediaType_Video, MFVideoFormat_NV12 };
    MFT_REGISTER_TYPE_INFO OutputInfo = { MFMediaType_Video, Spec.Subtype };
    ComPtr<IMFMediaEventGenerator> Events;
    ComPtr<IMFAttributes> EnumAttributes;
    ComPtr<IMFAttributes> Attributes;
    ComPtr<IDXGIDevice> DxgiDevice;
    ComPtr<IDXGIAdapter> Adapter;
    DXGI_ADAPTER_DESC AdapterDesc;
    ComPtr<IMFTransform> Transform;
    ComPtr<IMFMediaType> Output;
    ComPtr<IMFMediaType> Input;
    ComPtr<ICodecAPI> Codec;
    IMFActivate** Activates = nullptr;
    WCHAR* Name = nullptr;
    UINT32 Count = 0;
    UINT32 Length = 0;
    UINT Next = 0;
    BOOLEAN IsDraining = FALSE;
    BOOLEAN IsDrained = FALSE;
    VARIANT Value;
    HRESULT hr;

    Units.clear();

    /* Only encoders on the adapter that owns our device can use its textures */
    hr = Device->QueryInterface(IID_PPV_ARGS(&DxgiDevice));
    if (SUCCEEDED(hr))
        hr = DxgiDevice->GetAdapter(&Adapter);
    if (SUCCEEDED(hr))
        hr = Adapter->GetDesc(&AdapterDesc);
    if (SUCCEEDED(hr))
        hr = MFCreateAttributes(&EnumAttributes, 1);
    if (SUCCEEDED(hr))
    {
        hr = EnumAttributes->SetBlob(MFT_ENUM_ADAPTER_LUID, (const UINT8*)&AdapterDesc.AdapterLuid,
                                     sizeof(AdapterDesc.AdapterLuid));
    }
    if (SUCCEEDED(hr))
    {
        hr = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER, MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
                      &InputInfo, &OutputInfo, EnumAttributes.Get(), &Activates, &Count);
    }
    if (FAILED(hr) || Count == 0)
    {
        wprintf(L"  %s: no hardware encoder MFT (0x%08lX)\n", Spec.CodecName, hr);
        CoTaskMemFree(Activates);
        return FAILED(hr) ? hr : MF_E_TOPO_CODEC_NOT_FOUND;
    }

    Activates[0]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &Name, &Length);
    wprintf(L"  %s: using %s\n", Spec.CodecName, Name ? Name : L"(unnamed encoder)");
    CoTaskMemFree(Name);

    hr = Activates[0]->ActivateObject(IID_PPV_ARGS(&Transform));
    for (UINT32 Index = 0; Index < Count; Index++)
    {
        if (FAILED(hr) || Index > 0)
            Activates[Index]->Release();
    }
    if (FAILED(hr))
    {
        wprintf(L"  %s: ActivateObject failed 0x%08lX\n", Spec.CodecName, hr);
        CoTaskMemFree(Activates);
        return hr;
    }

    /* Asynchronous MFTs stay locked until the caller says it understands the event model */
    hr = Transform->GetAttributes(&Attributes);
    if (SUCCEEDED(hr) && !MFGetAttributeUINT32(Attributes.Get(), MF_TRANSFORM_ASYNC, FALSE))
    {
        wprintf(L"  %s: encoder is synchronous, which this tool does not drive\n", Spec.CodecName);
        hr = E_NOTIMPL;
    }
    if (SUCCEEDED(hr))
        hr = Attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    if (SUCCEEDED(hr))
    {
        hr = Transform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, (ULONG_PTR)Manager);
        if (FAILED(hr))
            wprintf(L"  %s: MFT_MESSAGE_SET_D3D_MANAGER failed 0x%08lX\n", Spec.CodecName, hr);
    }

    if (SUCCEEDED(hr))
        hr = MFCreateMediaType(&Output);
    if (SUCCEEDED(hr))
    {
        Output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        Output->SetGUID(MF_MT_SUBTYPE, Spec.Subtype);
        Output->SetUINT32(MF_MT_AVG_BITRATE, Spec.Bitrate);
        Output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(Output.Get(), MF_MT_FRAME_SIZE, Spec.Width, Spec.Height);
        MFSetAttributeRatio(Output.Get(), MF_MT_FRAME_RATE, Spec.FrameRate, 1);
        MFSetAttributeRatio(Output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = Transform->SetOutputType(0, Output.Get(), 0);
        if (FAILED(hr))
            wprintf(L"  %s: SetOutputType failed 0x%08lX\n", Spec.CodecName, hr);
    }

    if (SUCCEEDED(hr))
        hr = MFCreateMediaType(&Input);
    if (SUCCEEDED(hr))
    {
        Input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        Input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
        Input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(Input.Get(), MF_MT_FRAME_SIZE, Spec.Width, Spec.Height);
        MFSetAttributeRatio(Input.Get(), MF_MT_FRAME_RATE, Spec.FrameRate, 1);
        MFSetAttributeRatio(Input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = Transform->SetInputType(0, Input.Get(), 0);
        if (FAILED(hr))
            wprintf(L"  %s: SetInputType failed 0x%08lX\n", Spec.CodecName, hr);
    }

    /* A key frame every two seconds gives seeking something to land on */
    if (SUCCEEDED(hr) && SUCCEEDED(Transform.As(&Codec)))
    {
        VariantInit(&Value);
        Value.vt = VT_UI4;
        Value.ulVal = Spec.FrameRate * 2;
        Codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &Value);
    }

    if (SUCCEEDED(hr))
        hr = Transform.As(&Events);
    if (SUCCEEDED(hr))
        hr = Transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    if (SUCCEEDED(hr))
        hr = Transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

    while (SUCCEEDED(hr) && !IsDrained)
    {
        ComPtr<IMFMediaEvent> Event;
        MediaEventType Type;
        HRESULT EventStatus = S_OK;

        hr = Events->GetEvent(0, &Event);
        if (FAILED(hr))
            break;
        Event->GetType(&Type);
        Event->GetStatus(&EventStatus);

        switch (Type)
        {
            case METransformNeedInput:
                if (Next < Spec.Frames)
                {
                    ComPtr<IMFSample> Sample;

                    hr = NvdMakeInputSample(Device, Spec, Next, Sample);
                    if (SUCCEEDED(hr))
                        hr = Transform->ProcessInput(0, Sample.Get(), 0);
                    Next++;
                }
                else if (!IsDraining)
                {
                    Transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
                    hr = Transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
                    IsDraining = TRUE;
                }
                break;

            case METransformHaveOutput:
                hr = NvdCollectOutput(Transform.Get(), Units);
                break;

            case METransformDrainComplete:
                IsDrained = TRUE;
                break;

            case MEError:
                hr = FAILED(EventStatus) ? EventStatus : E_FAIL;
                break;

            default:
                break;
        }
    }

    Transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    if (SUCCEEDED(hr))
        hr = Transform->GetOutputCurrentType(0, &OutputType);

    Activates[0]->ShutdownObject();
    Activates[0]->Release();
    CoTaskMemFree(Activates);

    wprintf(L"  %s: %u frames in, %zu units out (0x%08lX)\n", Spec.CodecName, Next, Units.size(), hr);
    return hr;
}
