/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Codec independent decoder interface and the shared D3D11 video plumbing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "videodecoder.hpp"

constexpr UINT GpuQueryCount = 16;

HRESULT
VideoDecoder::Initialize(
    _In_ ID3D11Device* NewDevice,
    _In_ ID3D11DeviceContext* NewContext,
    _In_ UINT NewConsumerSurfaces,
    _In_ BOOLEAN UseGpuTiming)
{
    D3D11_QUERY_DESC QueryDesc = { D3D11_QUERY_EVENT, 0 };
    HRESULT hr;
    UINT Index;

    Device = NewDevice;
    Context = NewContext;
    ConsumerSurfaces = NewConsumerSurfaces;

    hr = Device.As(&VideoDevice);
    if (FAILED(hr))
    {
        wprintf(L"error: runtime has no ID3D11VideoDevice (0x%08lX)\n", hr);
        return hr;
    }

    hr = Context.As(&VideoContext);
    if (FAILED(hr))
    {
        wprintf(L"error: runtime has no ID3D11VideoContext (0x%08lX)\n", hr);
        return hr;
    }

    if (UseGpuTiming)
    {
        GpuQueries.resize(GpuQueryCount);
        for (Index = 0; Index < GpuQueryCount; Index++)
        {
            if (FAILED(Device->CreateQuery(&QueryDesc, &GpuQueries[Index].Query)))
            {
                wprintf(L"note: event queries unavailable, GPU completion timing is off\n");
                GpuQueries.clear();
                break;
            }
        }
    }

    return S_OK;
}

HRESULT
VideoDecoder::CreateDecoder(
    _In_ const GUID& Profile,
    _In_z_ PCWSTR ProfileName,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ DXGI_FORMAT Format,
    _In_ UINT DecoderSurfaces,
    _In_ BOOLEAN (*AcceptConfig)(const D3D11_VIDEO_DECODER_CONFIG&))
{
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC ViewDesc = {};
    D3D11_VIDEO_DECODER_DESC Desc = {};
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    D3D11_VIDEO_DECODER_CONFIG Candidate;
    BOOLEAN HasProfile = FALSE;
    BOOL IsSupported = FALSE;
    UINT ConfigCount = 0;
    UINT SurfaceCount;
    UINT Index;
    GUID Listed;
    HRESULT hr;

    for (Index = 0; Index < VideoDevice->GetVideoDecoderProfileCount(); Index++)
    {
        if (SUCCEEDED(VideoDevice->GetVideoDecoderProfile(Index, &Listed)) && Listed == Profile)
        {
            HasProfile = TRUE;
            break;
        }
    }

    hr = VideoDevice->CheckVideoDecoderFormat(&Profile, Format, &IsSupported);
    if (!HasProfile || FAILED(hr) || !IsSupported)
    {
        wprintf(L"error: adapter has no %s decoder for output format %u\n", ProfileName, Format);
        return E_NOTIMPL;
    }

    Desc.Guid = Profile;
    Desc.SampleWidth = Width;
    Desc.SampleHeight = Height;
    Desc.OutputFormat = Format;

    hr = VideoDevice->GetVideoDecoderConfigCount(&Desc, &ConfigCount);
    if (FAILED(hr))
        return hr;

    for (Index = 0; Index < ConfigCount; Index++)
    {
        if (SUCCEEDED(VideoDevice->GetVideoDecoderConfig(&Desc, Index, &Candidate)) && AcceptConfig(Candidate))
        {
            Config = Candidate;
            ConfigIndex = Index;
            break;
        }
    }

    if (Index == ConfigCount)
    {
        wprintf(L"error: none of the %u %s decoder configurations fit\n", ConfigCount, ProfileName);
        return E_NOTIMPL;
    }

    SurfaceCount = DecoderSurfaces + ConsumerSurfaces;

    TextureDesc.Width = Width;
    TextureDesc.Height = Height;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = SurfaceCount;
    TextureDesc.Format = Format;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DECODER;

    hr = Device->CreateTexture2D(&TextureDesc, nullptr, &Texture);
    if (FAILED(hr))
    {
        wprintf(L"error: CreateTexture2D(%ux%u x %u) failed 0x%08lX\n", Width, Height, SurfaceCount, hr);
        return hr;
    }

    OutputViews.resize(SurfaceCount);
    ViewDesc.DecodeProfile = Profile;
    ViewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
    for (Index = 0; Index < SurfaceCount; Index++)
    {
        ViewDesc.Texture2D.ArraySlice = Index;
        hr = VideoDevice->CreateVideoDecoderOutputView(Texture.Get(), &ViewDesc, &OutputViews[Index]);
        if (FAILED(hr))
        {
            wprintf(L"error: CreateVideoDecoderOutputView(%u) failed 0x%08lX\n", Index, hr);
            return hr;
        }
    }

    hr = VideoDevice->CreateVideoDecoder(&Desc, &Config, &Decoder);
    if (FAILED(hr))
    {
        wprintf(L"error: CreateVideoDecoder(%s) failed 0x%08lX\n", ProfileName, hr);
        return hr;
    }

    SurfaceRefs.assign(SurfaceCount, 0);
    return S_OK;
}

INT
VideoDecoder::FindFreeSurface() const
{
    UINT Index;

    for (Index = 0; Index < SurfaceRefs.size(); Index++)
    {
        if (SurfaceRefs[Index] == 0)
            return (INT)Index;
    }

    return -1;
}

HRESULT
VideoDecoder::BeginFrame(
    _In_ UINT SurfaceIndex,
    _Out_ UINT* Retries)
{
    HRESULT hr;

    /* E_PENDING means the decoder still owns the previous frame; real players spin briefly too */
    *Retries = 0;
    for (;;)
    {
        hr = VideoContext->DecoderBeginFrame(Decoder.Get(), OutputViews[SurfaceIndex].Get(), 0, nullptr);
        if (hr != E_PENDING || *Retries >= 1000)
            break;
        (*Retries)++;
        Sleep(1);
    }

    if (FAILED(hr))
        wprintf(L"error: DecoderBeginFrame failed 0x%08lX\n", hr);

    return hr;
}

HRESULT
VideoDecoder::CopyToDecoderBuffer(
    _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type,
    _In_reads_bytes_(Size) const VOID* Data,
    _In_ UINT Size)
{
    UINT Capacity = 0;
    VOID* Buffer = nullptr;
    HRESULT hr;

    hr = VideoContext->GetDecoderBuffer(Decoder.Get(), Type, &Capacity, &Buffer);
    if (FAILED(hr))
        return hr;

    if (Capacity < Size)
    {
        VideoContext->ReleaseDecoderBuffer(Decoder.Get(), Type);
        return E_OUTOFMEMORY;
    }

    memcpy(Buffer, Data, Size);
    return VideoContext->ReleaseDecoderBuffer(Decoder.Get(), Type);
}

HRESULT
VideoDecoder::EndFrame(
    _In_ HRESULT Status,
    _In_ double BeginMs)
{
    /* EndFrame always runs so the decoder is not left mid-frame */
    if (FAILED(VideoContext->DecoderEndFrame(Decoder.Get())) && SUCCEEDED(Status))
        Status = E_FAIL;

    if (FAILED(Status))
    {
        wprintf(L"error: submitting picture %llu failed 0x%08lX\n", DecodeCount, Status);
        return Status;
    }

    if (!GpuQueries.empty() && !GpuQueries[NextGpuQuery].IsPending)
    {
        Context->End(GpuQueries[NextGpuQuery].Query.Get());
        GpuQueries[NextGpuQuery].SubmitMs = BeginMs;
        GpuQueries[NextGpuQuery].IsPending = TRUE;
        NextGpuQuery = (NextGpuQuery + 1) % GpuQueryCount;
    }

    return S_OK;
}

VOID
VideoDecoder::PollGpu()
{
    BOOL Done;

    for (GpuQuery& Query : GpuQueries)
    {
        if (!Query.IsPending)
            continue;

        if (Context->GetData(Query.Query.Get(), &Done, sizeof(Done), 0) == S_OK && Done)
        {
            Stats.OnGpuComplete(NvdNowMs() - Query.SubmitMs);
            Query.IsPending = FALSE;
        }
    }
}

VOID
VideoDecoder::ReleaseFrame(
    _In_ UINT SurfaceIndex)
{
    if (SurfaceIndex < SurfaceRefs.size() && SurfaceRefs[SurfaceIndex] > 0)
        SurfaceRefs[SurfaceIndex]--;
}
