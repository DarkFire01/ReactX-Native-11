/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Codec independent decoder interface and the shared D3D11 video plumbing
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "source.hpp"
#include "stats.hpp"

using Microsoft::WRL::ComPtr;

/* A picture ready for display; the surface stays valid until ReleaseFrame */
struct DecodedFrame
{
    UINT SurfaceIndex;
    INT64 PresentationTime;
    UINT64 DisplayIndex;

    /* The visible part of the surface for this picture; AV1 frame sizes can change */
    RECT Visible;
    CHAR Type;
};

/* What the presenter and readback need to know about the decoded pictures */
struct VideoFormatInfo
{
    UINT SurfaceWidth;
    UINT SurfaceHeight;
    DXGI_FORMAT Format;
    UINT BitDepth;
    RECT Visible;
    UINT SarWidth;
    UINT SarHeight;
    BOOLEAN IsFullRange;
    BOOLEAN HasColourDescription;
    UINT MatrixCoefficients;
    double FrameRate;
};

class VideoDecoder
{
public:
    explicit VideoDecoder(
        _In_ PlaybackStats& Stats)
        : Stats(Stats)
    {
    }

    virtual ~VideoDecoder() = default;

    HRESULT
    Initialize(
        _In_ ID3D11Device* Device,
        _In_ ID3D11DeviceContext* Context,
        _In_ UINT ConsumerSurfaces,
        _In_ BOOLEAN UseGpuTiming);

    virtual
    HRESULT
    DecodeAccessUnit(
        _In_ const AccessUnit& Unit,
        _Inout_ std::vector<DecodedFrame>& Ready) = 0;

    /* Outputs every picture still waiting for display, at end of stream */
    virtual
    VOID
    Drain(
        _Inout_ std::vector<DecodedFrame>& Ready) = 0;

    /* Forgets every reference after a seek; decoding resumes at the next key frame */
    virtual
    VOID
    Reset() = 0;

    virtual
    VideoFormatInfo
    GetFormat() const = 0;

    virtual
    std::wstring
    Describe() const = 0;

    virtual
    UINT
    GetDpbFill() const = 0;

    virtual
    UINT
    GetDpbSize() const = 0;

    VOID
    ReleaseFrame(
        _In_ UINT SurfaceIndex);

    /* Collects completed GPU timing queries without blocking */
    VOID
    PollGpu();

    BOOLEAN
    IsConfigured() const
    {
        return Decoder != nullptr;
    }

    ID3D11Texture2D*
    GetTexture() const
    {
        return Texture.Get();
    }

    UINT
    GetSurfaceCount() const
    {
        return (UINT)SurfaceRefs.size();
    }

protected:
    /* Creates the decoder, its surface array and one output view per surface */
    HRESULT
    CreateDecoder(
        _In_ const GUID& Profile,
        _In_z_ PCWSTR ProfileName,
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ DXGI_FORMAT Format,
        _In_ UINT DecoderSurfaces,
        _In_ BOOLEAN (*AcceptConfig)(const D3D11_VIDEO_DECODER_CONFIG&));

    INT
    FindFreeSurface() const;

    HRESULT
    BeginFrame(
        _In_ UINT SurfaceIndex,
        _Out_ UINT* Retries);

    HRESULT
    CopyToDecoderBuffer(
        _In_ D3D11_VIDEO_DECODER_BUFFER_TYPE Type,
        _In_reads_bytes_(Size) const VOID* Data,
        _In_ UINT Size);

    /* Ends the frame and records timing; Status is the result of the buffer steps */
    HRESULT
    EndFrame(
        _In_ HRESULT Status,
        _In_ double BeginMs);

    PlaybackStats& Stats;

    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    ComPtr<ID3D11VideoDevice> VideoDevice;
    ComPtr<ID3D11VideoContext> VideoContext;
    ComPtr<ID3D11VideoDecoder> Decoder;
    ComPtr<ID3D11Texture2D> Texture;
    std::vector<ComPtr<ID3D11VideoDecoderOutputView>> OutputViews;
    D3D11_VIDEO_DECODER_CONFIG Config = {};
    UINT ConfigIndex = 0;
    UINT ConsumerSurfaces = 0;

    /* One count per holder: the reference structures and every frame handed out */
    std::vector<UINT> SurfaceRefs;

    UINT64 DecodeCount = 0;
    UINT64 DisplayCount = 0;
    UINT StatusFeedback = 0;

private:
    struct GpuQuery
    {
        ComPtr<ID3D11Query> Query;
        double SubmitMs = 0;
        BOOLEAN IsPending = FALSE;
    };
    std::vector<GpuQuery> GpuQueries;
    UINT NextGpuQuery = 0;
};
