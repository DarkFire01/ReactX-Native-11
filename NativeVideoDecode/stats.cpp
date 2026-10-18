/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Decode and presentation statistics
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "stats.hpp"

#include <algorithm>

double
NvdNowMs(VOID)
{
    static LARGE_INTEGER Frequency;
    LARGE_INTEGER Counter;

    if (Frequency.QuadPart == 0)
        QueryPerformanceFrequency(&Frequency);

    QueryPerformanceCounter(&Counter);
    return (double)Counter.QuadPart * 1000.0 / (double)Frequency.QuadPart;
}

static
double
NvdPercentile(
    _Inout_ std::vector<double>& Values,
    _In_ double Fraction)
{
    SIZE_T Index;

    if (Values.empty())
        return 0;

    Index = (SIZE_T)(Fraction * (double)(Values.size() - 1));
    std::nth_element(Values.begin(), Values.begin() + Index, Values.end());
    return Values[Index];
}

/* Folds per frame checksums so the stream value also depends on frame order */
static
UINT32
NvdCombineCrc(
    _In_ UINT32 Stream,
    _In_ UINT32 Frame)
{
    return (Stream << 5 | Stream >> 27) ^ Frame;
}

PlaybackStats::~PlaybackStats()
{
    if (Csv != nullptr)
        fclose(Csv);
}

VOID
PlaybackStats::Start()
{
    StartMs = NvdNowMs();
    IntervalStartMs = StartMs;
}

BOOLEAN
PlaybackStats::OpenCsv(
    _In_z_ PCWSTR Path)
{
    if (_wfopen_s(&Csv, Path, L"w") != 0 || Csv == nullptr)
        return FALSE;

    fprintf(Csv, "decode_index,type,poc,bytes,slices,submit_ms,pending_retries,time_ms\n");
    return TRUE;
}

VOID
PlaybackStats::OnPictureDecoded(
    _In_ UINT64 DecodeIndex,
    _In_ CHAR Type,
    _In_ INT32 Poc,
    _In_ UINT32 Bytes,
    _In_ UINT32 Slices,
    _In_ double Submit,
    _In_ UINT32 PendingRetries)
{
    Interval.Decoded++;
    Interval.Bytes += Bytes;
    Interval.PendingRetries += PendingRetries;
    Interval.SubmitTotalMs += Submit;
    Interval.SubmitMaxMs = std::max(Interval.SubmitMaxMs, Submit);

    TotalDecoded++;
    TotalBytes += Bytes;
    TotalPendingRetries += PendingRetries;
    if (Type >= 'A' && Type <= 'Z')
        TypeCounts[Type - 'A']++;
    SubmitMs.push_back(Submit);

    if (Csv != nullptr)
    {
        fprintf(Csv, "%llu,%c,%d,%u,%u,%.4f,%u,%.3f\n",
                DecodeIndex, Type, Poc, Bytes, Slices,
                Submit, PendingRetries, NvdNowMs() - StartMs);
    }
}

VOID
PlaybackStats::OnGpuComplete(
    _In_ double LatencyMs)
{
    Interval.GpuTotalMs += LatencyMs;
    Interval.GpuSamples++;
    Interval.GpuMaxMs = std::max(Interval.GpuMaxMs, LatencyMs);
    GpuMs.push_back(LatencyMs);
}

VOID
PlaybackStats::OnFramePresented(
    _In_ double Lateness)
{
    Interval.Presented++;
    TotalPresented++;
    LatenessMs.push_back(Lateness);
}

VOID
PlaybackStats::OnFrameDropped()
{
    Interval.Dropped++;
    TotalDropped++;
}

VOID
PlaybackStats::OnFrameChecksum(
    _In_ UINT64 DisplayIndex,
    _In_ UINT32 Crc)
{
    StreamCrc = NvdCombineCrc(StreamCrc, Crc);
    ChecksummedFrames++;
    if (DisplayIndex < 8)
        wprintf(L"  frame %llu crc %08X\n", DisplayIndex, Crc);
}

VOID
PlaybackStats::SetQueueState(
    _In_ UINT32 Queue,
    _In_ UINT32 Fill,
    _In_ UINT32 Size)
{
    OutputQueue = Queue;
    DpbFill = Fill;
    DpbSize = Size;
}

VOID
PlaybackStats::PrintInterval(
    _In_ double Now)
{
    double Seconds = (Now - IntervalStartMs) / 1000.0;

    if (Seconds <= 0)
        return;

    wprintf(L"[%7.2fs] decode %6.1f fps | present %6.1f fps | submit avg %.3f max %.3f ms",
            (Now - StartMs) / 1000.0,
            Interval.Decoded / Seconds,
            Interval.Presented / Seconds,
            Interval.Decoded ? Interval.SubmitTotalMs / Interval.Decoded : 0.0,
            Interval.SubmitMaxMs);
    if (Interval.GpuSamples > 0)
    {
        wprintf(L" | gpu avg %.3f max %.3f ms",
                Interval.GpuTotalMs / Interval.GpuSamples,
                Interval.GpuMaxMs);
    }
    wprintf(L" | %.2f Mbps | queue %u dpb %u/%u | drops %llu | busy %llu\n",
            Interval.Bytes * 8.0 / Seconds / 1e6,
            OutputQueue,
            DpbFill,
            DpbSize,
            Interval.Dropped,
            Interval.PendingRetries);

    LastInterval = Interval;
    LastIntervalSeconds = Seconds;
    Interval = IntervalCounters();
    IntervalStartMs = Now;
}

VOID
PlaybackStats::ReportIfDue()
{
    double Now = NvdNowMs();

    if (Now - IntervalStartMs >= 1000.0)
        PrintInterval(Now);
}

VOID
PlaybackStats::PrintSummary()
{
    double Seconds = (NvdNowMs() - StartMs) / 1000.0;

    wprintf(L"\n== summary ==\n");
    wprintf(L"  wall time        %.2f s\n", Seconds);
    wprintf(L"  pictures decoded %llu (%s)\n", TotalDecoded, TypeSummary().c_str());
    wprintf(L"  frames presented %llu, dropped %llu\n", TotalPresented, TotalDropped);
    wprintf(L"  average decode   %.1f fps\n", Seconds > 0 ? TotalDecoded / Seconds : 0.0);
    wprintf(L"  bitstream        %.2f MB, %.2f Mbps average\n",
            TotalBytes / 1e6, Seconds > 0 ? TotalBytes * 8.0 / Seconds / 1e6 : 0.0);
    wprintf(L"  submit (CPU)     p50 %.3f  p95 %.3f  p99 %.3f  max %.3f ms\n",
            NvdPercentile(SubmitMs, 0.50), NvdPercentile(SubmitMs, 0.95),
            NvdPercentile(SubmitMs, 0.99), NvdPercentile(SubmitMs, 1.0));
    if (!GpuMs.empty())
    {
        wprintf(L"  submit to done   p50 %.3f  p95 %.3f  p99 %.3f  max %.3f ms\n",
                NvdPercentile(GpuMs, 0.50), NvdPercentile(GpuMs, 0.95),
                NvdPercentile(GpuMs, 0.99), NvdPercentile(GpuMs, 1.0));
    }
    if (!LatenessMs.empty())
    {
        wprintf(L"  present lateness p50 %.3f  p95 %.3f  max %.3f ms\n",
                NvdPercentile(LatenessMs, 0.50), NvdPercentile(LatenessMs, 0.95),
                NvdPercentile(LatenessMs, 1.0));
    }
    wprintf(L"  decoder busy     %llu BeginFrame retries\n", TotalPendingRetries);
    if (ChecksummedFrames > 0)
        wprintf(L"  stream checksum  %08X over %llu frames\n", StreamCrc, ChecksummedFrames);
}

std::wstring
PlaybackStats::OverlayText() const
{
    WCHAR Text[512];
    double Seconds = LastIntervalSeconds > 0 ? LastIntervalSeconds : 1;

    swprintf_s(Text,
               L"decode %.1f fps   present %.1f fps   drops %llu\n"
               L"submit avg %.3f ms   gpu avg %.3f ms\n"
               L"%.2f Mbps   queue %u   dpb %u/%u\n"
               L"frames %llu   %s",
               LastInterval.Decoded / Seconds,
               LastInterval.Presented / Seconds,
               TotalDropped,
               LastInterval.Decoded ? LastInterval.SubmitTotalMs / LastInterval.Decoded : 0.0,
               LastInterval.GpuSamples ? LastInterval.GpuTotalMs / LastInterval.GpuSamples : 0.0,
               LastInterval.Bytes * 8.0 / Seconds / 1e6,
               OutputQueue,
               DpbFill,
               DpbSize,
               TotalDecoded,
               TypeSummary().c_str());
    return Text;
}

std::wstring
PlaybackStats::TypeSummary() const
{
    std::wstring Text;
    WCHAR Part[32];
    UINT Index;

    for (Index = 0; Index < 26; Index++)
    {
        if (TypeCounts[Index] == 0)
            continue;

        swprintf_s(Part, L"%s%c %llu", Text.empty() ? L"" : L", ", L'A' + Index, TypeCounts[Index]);
        Text += Part;
    }

    return Text;
}
