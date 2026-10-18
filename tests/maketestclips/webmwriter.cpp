/*
 * PROJECT:     ReactX tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal AV1 WebM writer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "clips.hpp"

#include <stdio.h>

#include "../../NativeVideoDecode/av1syntax.hpp"

/* Everything is assembled in memory; element sizes always use the eight byte form */
class EbmlWriter
{
public:
    std::vector<UINT8> Data;

    VOID
    Id(
        _In_ UINT32 Value)
    {
        INT Shift;

        for (Shift = 24; Shift >= 0; Shift -= 8)
        {
            if ((Value >> Shift) != 0 || Shift == 0)
                Data.push_back((UINT8)(Value >> Shift));
        }
    }

    VOID
    Size(
        _In_ UINT64 Value)
    {
        INT Shift;

        Data.push_back(0x01);
        for (Shift = 48; Shift >= 0; Shift -= 8)
            Data.push_back((UINT8)(Value >> Shift));
    }

    VOID
    Unsigned(
        _In_ UINT32 ElementId,
        _In_ UINT64 Value)
    {
        INT Shift;

        Id(ElementId);
        Data.push_back(0x88);
        for (Shift = 56; Shift >= 0; Shift -= 8)
            Data.push_back((UINT8)(Value >> Shift));
    }

    VOID
    Float(
        _In_ UINT32 ElementId,
        _In_ double Value)
    {
        UINT64 Bits;

        memcpy(&Bits, &Value, sizeof(Bits));
        Unsigned(ElementId, Bits);
    }

    VOID
    Bytes(
        _In_ UINT32 ElementId,
        _In_reads_bytes_(Count) const VOID* Payload,
        _In_ SIZE_T Count)
    {
        Id(ElementId);
        Size(Count);
        Data.insert(Data.end(), (const UINT8*)Payload, (const UINT8*)Payload + Count);
    }

    VOID
    Child(
        _In_ UINT32 ElementId,
        _In_ const EbmlWriter& Inner)
    {
        Bytes(ElementId, Inner.Data.data(), Inner.Data.size());
    }
};

/* Header size of an element written by Bytes or Child: an id of Width bytes plus the size field */
static
UINT64
NvdElementOverhead(
    _In_ UINT IdWidth)
{
    return IdWidth + 8;
}

HRESULT
WriteAv1WebM(
    _In_z_ PCWSTR Path,
    _In_ const ClipSpec& Spec,
    _In_ const std::vector<EncodedUnit>& Units)
{
    std::vector<std::vector<UINT8>> Clusters;
    std::vector<INT64> ClusterTimes;
    std::vector<UINT8> CodecPrivate;
    std::vector<UINT8> Block;
    Av1SequenceHeader Sequence = {};
    EbmlWriter Header;
    EbmlWriter Info;
    EbmlWriter Tracks;
    EbmlWriter Entry;
    EbmlWriter Video;
    EbmlWriter Cues;
    EbmlWriter Segment;
    EbmlWriter Cluster;
    EbmlWriter Whole;
    INT64 ClusterStart = -1;
    INT64 Milliseconds;
    UINT64 Position;
    SIZE_T Offset;
    SIZE_T Start;
    SIZE_T Index;
    UINT Pass;
    Av1Obu Obu;
    FILE* File;

    for (const EncodedUnit& Unit : Units)
    {
        /* Matroska drops temporal delimiters and keeps every other OBU whole */
        Block.clear();
        Offset = 0;
        for (Start = 0; Av1ReadObu(Unit.Data.data(), Unit.Data.size(), &Offset, &Obu); Start = Offset)
        {
            if (Obu.Type == Av1ObuTemporalDelimiter)
                continue;

            if (Obu.Type == Av1ObuSequenceHeader && CodecPrivate.empty() &&
                Av1ParseSequenceHeader(Obu.Payload, Obu.Size, &Sequence))
            {
                CodecPrivate = { 0x81,
                                 (UINT8)((Sequence.Profile << 5) | Sequence.SeqLevelIdx[0]),
                                 (UINT8)((Sequence.SeqTier[0] << 7) | ((Sequence.BitDepth > 8) << 6) |
                                         ((Sequence.BitDepth == 12) << 5) | (Sequence.MonoChrome << 4) |
                                         (Sequence.SubsamplingX << 3) | (Sequence.SubsamplingY << 2) |
                                         Sequence.ChromaSamplePosition),
                                 0 };
                CodecPrivate.insert(CodecPrivate.end(), Unit.Data.begin() + Start, Unit.Data.begin() + Offset);
            }

            Block.insert(Block.end(), Unit.Data.begin() + Start, Unit.Data.begin() + Offset);
        }

        Milliseconds = Unit.Time / 10000;
        if (Unit.IsKeyFrame || ClusterStart < 0 || Milliseconds - ClusterStart > 30000)
        {
            if (ClusterStart >= 0)
                Clusters.push_back(Cluster.Data);
            Cluster = EbmlWriter();
            ClusterStart = Milliseconds;
            ClusterTimes.push_back(Milliseconds);
            Cluster.Unsigned(0xE7, (UINT64)Milliseconds);
        }

        /* SimpleBlock: track 1, int16 time relative to the cluster, key frame flag */
        Cluster.Id(0xA3);
        Cluster.Size(4 + Block.size());
        Cluster.Data.push_back(0x81);
        Cluster.Data.push_back((UINT8)((Milliseconds - ClusterStart) >> 8));
        Cluster.Data.push_back((UINT8)(Milliseconds - ClusterStart));
        Cluster.Data.push_back(Unit.IsKeyFrame ? 0x80 : 0x00);
        Cluster.Data.insert(Cluster.Data.end(), Block.begin(), Block.end());
    }
    if (ClusterStart >= 0)
        Clusters.push_back(Cluster.Data);

    if (CodecPrivate.empty())
    {
        wprintf(L"  webm: the stream carries no sequence header\n");
        return E_FAIL;
    }

    Header.Unsigned(0x4286, 1);
    Header.Unsigned(0x42F7, 1);
    Header.Unsigned(0x42F2, 4);
    Header.Unsigned(0x42F3, 8);
    Header.Bytes(0x4282, "webm", 4);
    Header.Unsigned(0x4287, 4);
    Header.Unsigned(0x4285, 2);

    Info.Unsigned(0x2AD7B1, 1000000);
    Info.Float(0x4489, Spec.Frames * 1000.0 / Spec.FrameRate);
    Info.Bytes(0x4D80, "maketestclips", 13);
    Info.Bytes(0x5741, "maketestclips", 13);

    Video.Unsigned(0xB0, Spec.Width);
    Video.Unsigned(0xBA, Spec.Height);
    Entry.Unsigned(0xD7, 1);
    Entry.Unsigned(0x73C5, 1);
    Entry.Unsigned(0x83, 1);
    Entry.Bytes(0x86, "V_AV1", 5);
    Entry.Bytes(0x63A2, CodecPrivate.data(), CodecPrivate.size());
    Entry.Unsigned(0x23E383, 1000000000ull / Spec.FrameRate);
    Entry.Child(0xE0, Video);
    Tracks.Child(0xAE, Entry);

    Segment.Child(0x1549A966, Info);
    Segment.Child(0x1654AE6B, Tracks);

    /*
     * Cues sit ahead of the clusters. Their numbers are fixed width, so a first pass with
     * placeholder positions has the same size as the real one.
     */
    for (Pass = 0; Pass < 2; Pass++)
    {
        Position = Segment.Data.size() + NvdElementOverhead(4) + Cues.Data.size();
        Cues = EbmlWriter();
        for (Index = 0; Index < Clusters.size(); Index++)
        {
            EbmlWriter Point;
            EbmlWriter Positions;

            Positions.Unsigned(0xF7, 1);
            Positions.Unsigned(0xF1, (Pass == 0) ? 0 : Position);
            Point.Unsigned(0xB3, (UINT64)ClusterTimes[Index]);
            Point.Child(0xB7, Positions);
            Cues.Child(0xBB, Point);
            Position += NvdElementOverhead(4) + Clusters[Index].size();
        }
    }

    Segment.Child(0x1C53BB6B, Cues);
    for (const std::vector<UINT8>& Each : Clusters)
        Segment.Bytes(0x1F43B675, Each.data(), Each.size());

    Whole.Child(0x1A45DFA3, Header);
    Whole.Child(0x18538067, Segment);

    if (_wfopen_s(&File, Path, L"wb") != 0 || File == nullptr)
        return E_FAIL;
    fwrite(Whole.Data.data(), 1, Whole.Data.size(), File);
    fclose(File);

    wprintf(L"  webm  %zu clusters -> %s\n", Clusters.size(), Path);
    return S_OK;
}
