/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     One open file: source, decoder, frame queue, clock and statistics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "player.hpp"
#include "av1decoder.hpp"
#include "h264decoder.hpp"

#include <shlobj.h>

#include <algorithm>

constexpr SIZE_T QueueDepth = 4;

static
UINT32
NvdCrc32(
    _In_ UINT32 Crc,
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size)
{
    static UINT32 Table[256];
    UINT32 Value;
    UINT Index;
    UINT Bit;

    if (Table[1] == 0)
    {
        for (Index = 0; Index < 256; Index++)
        {
            Value = Index;
            for (Bit = 0; Bit < 8; Bit++)
                Value = (Value & 1) ? (Value >> 1) ^ 0xEDB88320 : Value >> 1;
            Table[Index] = Value;
        }
    }

    Crc = ~Crc;
    while (Size-- > 0)
        Crc = Table[(Crc ^ *Data++) & 0xFF] ^ (Crc >> 8);
    return ~Crc;
}

Player::Player(
    _In_ ID3D11Device* NewDevice,
    _In_ ID3D11DeviceContext* NewContext,
    _In_ const PlayerOptions& NewOptions)
    : Device(NewDevice),
      Context(NewContext),
      Options(NewOptions)
{
}

Player::~Player()
{
    ReleaseQueue();
    if (HasDisplayed && Decoder != nullptr)
        Decoder->ReleaseFrame(Displayed.SurfaceIndex);
}

VOID
Player::ReleaseQueue()
{
    for (const DecodedFrame& Frame : Queue)
        Decoder->ReleaseFrame(Frame.SurfaceIndex);
    Queue.clear();
}

HRESULT
Player::Open(
    _In_z_ PCWSTR NewPath)
{
    std::string Error;
    HRESULT hr;

    Path = NewPath;
    Source = NvdOpenSource(NewPath, Error);
    if (Source == nullptr)
    {
        wprintf(L"error: %s: %S\n", NewPath, Error.c_str());
        return E_FAIL;
    }

    Stats = std::make_unique<PlaybackStats>();
    if (Source->GetCodec() == VideoCodec::Av1)
        Decoder = std::make_unique<Av1Decoder>(*Stats);
    else
        Decoder = std::make_unique<H264Decoder>(*Stats);

    hr = Decoder->Initialize(Device.Get(), Context.Get(), Options.ConsumerSurfaces, Options.GpuTiming);
    if (FAILED(hr))
        return hr;

    if (!Options.CsvPath.empty() && !Stats->OpenCsv(Options.CsvPath.c_str()))
        wprintf(L"warning: cannot write %s\n", Options.CsvPath.c_str());
    if (!Options.DumpDirectory.empty())
        SHCreateDirectoryExW(NULL, Options.DumpDirectory.c_str(), NULL);

    Stats->Start();

    /* Prime until the first picture tells us the size, so a window can be built for it */
    while (!Decoder->IsConfigured() && !IsEndOfStream)
    {
        hr = FillQueue();
        if (FAILED(hr))
            return hr;
    }

    if (!Decoder->IsConfigured())
    {
        wprintf(L"error: %s has no decodable pictures\n", NewPath);
        return E_FAIL;
    }

    if (Options.FrameRate > 0)
        FrameRate = Options.FrameRate;
    else if (Source->GetFrameRate() > 0)
        FrameRate = Source->GetFrameRate();
    else if (Decoder->GetFormat().FrameRate > 0)
        FrameRate = Decoder->GetFormat().FrameRate;

    wprintf(L"%s\n", DescribeStream().c_str());
    return S_OK;
}

HRESULT
Player::FillQueue()
{
    HRESULT hr;

    while (!IsEndOfStream && Queue.size() < QueueDepth)
    {
        Ready.clear();
        if (!Source->ReadAccessUnit(Unit))
        {
            Decoder->Drain(Ready);
            IsEndOfStream = TRUE;
        }
        else
        {
            hr = Decoder->DecodeAccessUnit(Unit, Ready);
            if (FAILED(hr))
                return hr;
        }

        for (DecodedFrame& Frame : Ready)
            ClampToDisplaySize(Frame.Visible);
        Queue.insert(Queue.end(), Ready.begin(), Ready.end());
        if (!Decoder->IsConfigured())
            continue;
        if (!Ready.empty())
            break;
    }

    return S_OK;
}

VOID
Player::ReadBack(
    _In_ const DecodedFrame& Frame)
{
    const RECT& Crop = Frame.Visible;
    D3D11_MAPPED_SUBRESOURCE Mapped;
    D3D11_TEXTURE2D_DESC Desc;
    WCHAR DumpPath[MAX_PATH];
    const UINT8* Plane;
    BOOLEAN IsDumping;
    FILE* Dump = nullptr;
    UINT32 Crc = 0;
    LONG ChromaLeft = Crop.left & ~1;
    SIZE_T Sample;
    SIZE_T LumaBytes;
    SIZE_T ChromaBytes;
    LONG Row;
    HRESULT hr;

    IsDumping = !Options.DumpDirectory.empty() && Frame.DisplayIndex < Options.DumpCount;
    if (!Options.Checksum && !IsDumping)
        return;

    /* Each frame is read once, even if the caller asks again while it waits */
    if (Frame.DisplayIndex < NextReadBack)
        return;
    NextReadBack = Frame.DisplayIndex + 1;

    Decoder->GetTexture()->GetDesc(&Desc);
    if (Staging == nullptr)
    {
        Desc.ArraySize = 1;
        Desc.Usage = D3D11_USAGE_STAGING;
        Desc.BindFlags = 0;
        Desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = Device->CreateTexture2D(&Desc, nullptr, &Staging);
        if (FAILED(hr))
        {
            wprintf(L"warning: staging texture failed 0x%08lX, no checksums\n", hr);
            Options.Checksum = FALSE;
            Options.DumpDirectory.clear();
            return;
        }
    }

    Context->CopySubresourceRegion(Staging.Get(), 0, 0, 0, 0, Decoder->GetTexture(),
                                   D3D11CalcSubresource(0, Frame.SurfaceIndex, 1), nullptr);
    hr = Context->Map(Staging.Get(), 0, D3D11_MAP_READ, 0, &Mapped);
    if (FAILED(hr))
    {
        wprintf(L"warning: frame readback failed 0x%08lX (removed reason 0x%08lX)\n", hr,
                Device->GetDeviceRemovedReason());
        return;
    }

    /* P010 keeps the NV12 layout with two bytes per sample */
    Sample = (Desc.Format == DXGI_FORMAT_P010) ? 2 : 1;
    LumaBytes = (Crop.right - Crop.left) * Sample;
    ChromaBytes = (Crop.right - ChromaLeft) * Sample;

    if (IsDumping)
    {
        swprintf_s(DumpPath, L"%s\\frame_%05llu_%ldx%ld.%s", Options.DumpDirectory.c_str(),
                   Frame.DisplayIndex, Crop.right - Crop.left, Crop.bottom - Crop.top,
                   (Sample == 2) ? L"p010" : L"nv12");
        _wfopen_s(&Dump, DumpPath, L"wb");
    }

    /* Luma rows, then interleaved chroma at half height after the full luma plane */
    for (Row = Crop.top; Row < Crop.bottom; Row++)
    {
        Plane = (const UINT8*)Mapped.pData + (SIZE_T)Row * Mapped.RowPitch + Crop.left * Sample;
        Crc = NvdCrc32(Crc, Plane, LumaBytes);
        if (Dump != nullptr)
            fwrite(Plane, 1, LumaBytes, Dump);
    }
    for (Row = Crop.top / 2; Row < Crop.bottom / 2; Row++)
    {
        Plane = (const UINT8*)Mapped.pData + (SIZE_T)(Desc.Height + Row) * Mapped.RowPitch + ChromaLeft * Sample;
        Crc = NvdCrc32(Crc, Plane, ChromaBytes);
        if (Dump != nullptr)
            fwrite(Plane, 1, ChromaBytes, Dump);
    }

    if (Dump != nullptr)
        fclose(Dump);
    Context->Unmap(Staging.Get(), 0);

    if (Options.Checksum)
        Stats->OnFrameChecksum(Frame.DisplayIndex, Crc);
}

TickResult
Player::Tick(
    _In_ BOOLEAN IsUnpaced,
    _In_ BOOLEAN IsStep,
    _Out_ DecodedFrame* Frame,
    _Out_ DWORD* WaitMs)
{
    double FrameMs = 1000.0 / FrameRate;
    double TargetMs;
    double Now;
    HRESULT hr;

    *WaitMs = 0;

    for (;;)
    {
        hr = FillQueue();
        if (FAILED(hr))
            return TickResult::Failed;

        Decoder->PollGpu();
        Stats->SetQueueState((UINT32)Queue.size(), Decoder->GetDpbFill(), Decoder->GetDpbSize());
        Stats->ReportIfDue();

        if (Queue.empty())
        {
            if (!IsEndOfStream)
                continue;
            if (!Looping || FAILED(SeekTo(0)))
                return TickResult::Finished;
            continue;
        }

        *Frame = Queue.front();
        ReadBack(*Frame);

        /* Pictures between the key frame and a seek target are decoded but never shown */
        if (SkipUntil != NvdNoTimestamp && Frame->PresentationTime != NvdNoTimestamp &&
            Frame->PresentationTime < SkipUntil)
        {
            Queue.pop_front();
            Decoder->ReleaseFrame(Frame->SurfaceIndex);
            continue;
        }
        SkipUntil = NvdNoTimestamp;

        if (IsUnpaced || IsStep)
        {
            Queue.pop_front();
            ClockBaseTime = NvdNoTimestamp;
            return TickResult::Show;
        }

        Now = NvdNowMs();
        if (ClockBaseTime == NvdNoTimestamp)
        {
            ClockBaseTime = (Frame->PresentationTime != NvdNoTimestamp) ? Frame->PresentationTime
                                                                       : (INT64)(ShownCount * FrameMs * 10000);
            ClockBaseMs = Now;
        }

        if (Frame->PresentationTime != NvdNoTimestamp)
            TargetMs = ClockBaseMs + (Frame->PresentationTime - ClockBaseTime) / 10000.0;
        else
            TargetMs = ClockBaseMs + ShownCount * FrameMs - ClockBaseTime / 10000.0;

        if (Now < TargetMs - 1.0)
        {
            *WaitMs = (DWORD)(TargetMs - Now);
            return TickResult::Wait;
        }

        /* More than two frames late with more queued: skip this one */
        if (Now > TargetMs + 2 * FrameMs && Queue.size() > 1)
        {
            Queue.pop_front();
            Decoder->ReleaseFrame(Frame->SurfaceIndex);
            Stats->OnFrameDropped();
            ShownCount++;
            continue;
        }

        Queue.pop_front();
        Stats->OnFramePresented(Now - TargetMs);
        ShownCount++;
        return TickResult::Show;
    }
}

VOID
Player::FrameShown(
    _In_ const DecodedFrame& Frame)
{
    if (HasDisplayed)
        Decoder->ReleaseFrame(Displayed.SurfaceIndex);

    Displayed = Frame;
    HasDisplayed = TRUE;
}

VOID
Player::SetPaused(
    _In_ BOOLEAN IsPaused)
{
    if (IsPaused)
    {
        PausedAtMs = NvdNowMs();
        return;
    }

    /* The clock stands still while paused */
    if (PausedAtMs != 0)
        ClockBaseMs += NvdNowMs() - PausedAtMs;
    PausedAtMs = 0;
}

HRESULT
Player::SeekTo(
    _In_ INT64 Time)
{
    Time = std::max<INT64>(Time, 0);
    if (Source->GetDuration() > 0)
        Time = std::min(Time, Source->GetDuration());

    if (!Source->SeekToKeyFrame(Time))
        return E_NOTIMPL;

    ReleaseQueue();
    Decoder->Reset();
    IsEndOfStream = FALSE;
    ClockBaseTime = NvdNoTimestamp;
    PausedAtMs = (PausedAtMs != 0) ? NvdNowMs() : 0;
    SkipUntil = (Time > 0) ? Time : NvdNoTimestamp;
    return S_OK;
}

HRESULT
Player::SeekRelative(
    _In_ double Seconds)
{
    return SeekTo(GetPosition() + (INT64)(Seconds * 10000000));
}

INT64
Player::GetPosition() const
{
    if (!HasDisplayed)
        return 0;
    if (Displayed.PresentationTime != NvdNoTimestamp)
        return Displayed.PresentationTime;
    return (INT64)(Displayed.DisplayIndex * 10000000 / FrameRate);
}

VOID
Player::ClampToDisplaySize(
    _Inout_ RECT& Visible) const
{
    SIZE Declared = Source->GetDisplaySize();

    /* Some encoders pad the coded size without signalling a render size; the container knows better */
    if (Declared.cx > 0 && Declared.cy > 0)
    {
        Visible.right = std::min(Visible.right, Visible.left + Declared.cx);
        Visible.bottom = std::min(Visible.bottom, Visible.top + Declared.cy);
    }
}

VideoFormatInfo
Player::GetVideoFormat() const
{
    VideoFormatInfo Format = Decoder->GetFormat();

    ClampToDisplaySize(Format.Visible);
    return Format;
}

std::wstring
Player::DescribeStream() const
{
    std::wstring Text;
    WCHAR Line[MAX_PATH + 64];

    swprintf_s(Line, L"file           %s\n", Path.c_str());
    Text += Line;
    swprintf_s(Line, L"container      %S, %llu frames, %.2f s, %.3f fps\n",
               Source->GetContainerName(), Source->GetFrameCount(), Source->GetDuration() / 1e7, FrameRate);
    Text += Line;
    return Text + Decoder->Describe();
}
