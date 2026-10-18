/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ISO BMFF (MP4) reader for the first AVC video track
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "source.hpp"

#include <algorithm>

static
UINT32
NvdBe32(
    _In_reads_bytes_(4) const UINT8* Data)
{
    return ((UINT32)Data[0] << 24) | ((UINT32)Data[1] << 16) | ((UINT32)Data[2] << 8) | Data[3];
}

static
UINT64
NvdBe64(
    _In_reads_bytes_(8) const UINT8* Data)
{
    return ((UINT64)NvdBe32(Data) << 32) | NvdBe32(Data + 4);
}

static constexpr UINT32
NvdFourCc(
    _In_ char A,
    _In_ char B,
    _In_ char C,
    _In_ char D)
{
    return ((UINT32)(UINT8)A << 24) | ((UINT32)(UINT8)B << 16) | ((UINT32)(UINT8)C << 8) | (UINT8)D;
}

struct Mp4Box
{
    UINT32 Type;
    SIZE_T HeaderSize;
    SIZE_T Size;
};

/* Reads one box header out of an in-memory parent; FALSE at the end or on a bad size */
static
BOOLEAN
NvdReadBox(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _In_ SIZE_T Offset,
    _Out_ Mp4Box* Box)
{
    UINT64 BoxSize;

    if (Offset + 8 > Size)
        return FALSE;

    BoxSize = NvdBe32(Data + Offset);
    Box->Type = NvdBe32(Data + Offset + 4);
    Box->HeaderSize = 8;
    if (BoxSize == 1)
    {
        if (Offset + 16 > Size)
            return FALSE;
        BoxSize = NvdBe64(Data + Offset + 8);
        Box->HeaderSize = 16;
    }
    else if (BoxSize == 0)
    {
        BoxSize = Size - Offset;
    }

    if (BoxSize < Box->HeaderSize || BoxSize > Size - Offset)
        return FALSE;

    Box->Size = (SIZE_T)BoxSize;
    return TRUE;
}

struct Mp4Track
{
    UINT32 Handler = 0;
    UINT32 Timescale = 0;
    UINT32 SampleEntry = 0;
    UINT16 Width = 0;
    UINT16 Height = 0;
    UINT32 PresentationWidth = 0;
    UINT32 PresentationHeight = 0;
    std::vector<UINT8> CodecConfig;
    std::vector<std::pair<UINT32, UINT32>> TimeToSample;
    std::vector<std::pair<UINT32, INT32>> CompositionOffsets;
    std::vector<UINT32> SyncSamples;
    std::vector<UINT32> SampleSizes;
    UINT32 ConstantSampleSize = 0;
    UINT32 SampleCount = 0;
    std::vector<UINT64> ChunkOffsets;
    struct ChunkRun
    {
        UINT32 FirstChunk;
        UINT32 SamplesPerChunk;
    };
    std::vector<ChunkRun> SampleToChunk;
};

class Mp4Source final : public VideoSource
{
public:
    ~Mp4Source() override
    {
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
    }

    BOOLEAN
    Open(
        _In_z_ PCWSTR Path,
        _Out_ std::string& Error);

    BOOLEAN
    ReadAccessUnit(
        _Out_ AccessUnit& Unit) override;

    VOID
    Rewind() override
    {
        NextSample = 0;
        NeedsConfig = TRUE;
    }

    PCSTR
    GetContainerName() const override
    {
        return "MP4";
    }

    VideoCodec
    GetCodec() const override
    {
        return Codec;
    }

    double
    GetFrameRate() const override
    {
        return FrameRate;
    }

    UINT64
    GetFrameCount() const override
    {
        return Samples.size();
    }

    INT64
    GetDuration() const override
    {
        return Duration;
    }

    BOOLEAN
    SeekToKeyFrame(
        _In_ INT64 Time) override;

private:
    struct Sample
    {
        UINT64 Offset;
        UINT32 Size;
        INT64 PresentationTime;
        BOOLEAN IsKeyFrame;
    };

    BOOLEAN
    ReadAt(
        _In_ UINT64 Offset,
        _Out_writes_bytes_(Size) VOID* Buffer,
        _In_ UINT32 Size);

    VOID
    ParseContainer(
        _In_reads_bytes_(Size) const UINT8* Data,
        _In_ SIZE_T Size,
        _Inout_ Mp4Track& Track,
        _Inout_ std::vector<Mp4Track>& Tracks);

    VOID
    ParseLeaf(
        _In_ UINT32 Type,
        _In_reads_bytes_(Size) const UINT8* Data,
        _In_ SIZE_T Size,
        _Inout_ Mp4Track& Track);

    BOOLEAN
    BuildSamples(
        _In_ const Mp4Track& Track,
        _Out_ std::string& Error);

    HANDLE File = INVALID_HANDLE_VALUE;
    std::vector<Sample> Samples;
    VideoCodec Codec = VideoCodec::H264;

    /* H.264 parameter sets as Annex B, or AV1 configOBUs, sent ahead of the next unit read */
    std::vector<UINT8> ParameterSets;
    BOOLEAN NeedsConfig = TRUE;
    UINT32 LengthSize = 4;
    SIZE_T NextSample = 0;
    double FrameRate = 0;
    INT64 Duration = 0;
};

BOOLEAN
Mp4Source::SeekToKeyFrame(
    _In_ INT64 Time)
{
    SIZE_T Best = 0;
    SIZE_T Index;

    /* Decode order: the last key frame whose picture is due no later than Time */
    for (Index = 0; Index < Samples.size(); Index++)
    {
        if (Samples[Index].IsKeyFrame && Samples[Index].PresentationTime <= Time)
            Best = Index;
    }

    NextSample = Best;
    NeedsConfig = TRUE;
    return TRUE;
}

BOOLEAN
Mp4Source::ReadAt(
    _In_ UINT64 Offset,
    _Out_writes_bytes_(Size) VOID* Buffer,
    _In_ UINT32 Size)
{
    OVERLAPPED Position = {};
    DWORD Read = 0;

    Position.Offset = (DWORD)Offset;
    Position.OffsetHigh = (DWORD)(Offset >> 32);
    return ReadFile(File, Buffer, Size, &Read, &Position) && Read == Size;
}

VOID
Mp4Source::ParseLeaf(
    _In_ UINT32 Type,
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ Mp4Track& Track)
{
    UINT32 Count;
    UINT32 Index;
    SIZE_T EntryOffset;
    Mp4Box Entry;
    Mp4Box Child;
    BOOLEAN IsLarge;

    /* Every leaf parsed here is a full box: version and flags come first */
    if (Size < 8)
        return;
    Count = NvdBe32(Data + 4);

    switch (Type)
    {
        case NvdFourCc('h', 'd', 'l', 'r'):
            if (Size >= 12)
                Track.Handler = NvdBe32(Data + 8);
            break;

        case NvdFourCc('t', 'k', 'h', 'd'):
            /* 16.16 presentation size after the matrix; version 1 widens the time fields */
            EntryOffset = (Data[0] == 1) ? 88 : 76;
            if (Size >= EntryOffset + 8)
            {
                Track.PresentationWidth = NvdBe32(Data + EntryOffset) >> 16;
                Track.PresentationHeight = NvdBe32(Data + EntryOffset + 4) >> 16;
            }
            break;

        case NvdFourCc('m', 'd', 'h', 'd'):
            if (Data[0] == 1 && Size >= 24)
                Track.Timescale = NvdBe32(Data + 20);
            else if (Size >= 16)
                Track.Timescale = NvdBe32(Data + 12);
            break;

        case NvdFourCc('s', 't', 's', 'd'):
            /* Visual sample entries carry 78 bytes of fields before their child boxes */
            if (!NvdReadBox(Data, Size, 8, &Entry))
                break;
            Track.SampleEntry = Entry.Type;

            /* VisualSampleEntry: width and height follow 24 bytes of reserved and predefined fields */
            if (Entry.Size >= Entry.HeaderSize + 28)
            {
                const UINT8* Fields = Data + 8 + Entry.HeaderSize;
                Track.Width = (UINT16)((Fields[24] << 8) | Fields[25]);
                Track.Height = (UINT16)((Fields[26] << 8) | Fields[27]);
            }
            EntryOffset = 8 + Entry.HeaderSize + 78;
            while (NvdReadBox(Data, 8 + Entry.Size, EntryOffset, &Child))
            {
                if (Child.Type == NvdFourCc('a', 'v', 'c', 'C') || Child.Type == NvdFourCc('a', 'v', '1', 'C'))
                {
                    Track.CodecConfig.assign(Data + EntryOffset + Child.HeaderSize,
                                           Data + EntryOffset + Child.Size);
                }
                EntryOffset += Child.Size;
            }
            break;

        case NvdFourCc('s', 't', 't', 's'):
            for (Index = 0; Index < Count && 8 + Index * 8 + 8 <= Size; Index++)
            {
                Track.TimeToSample.emplace_back(NvdBe32(Data + 8 + Index * 8),
                                                NvdBe32(Data + 12 + Index * 8));
            }
            break;

        case NvdFourCc('c', 't', 't', 's'):
            for (Index = 0; Index < Count && 8 + Index * 8 + 8 <= Size; Index++)
            {
                Track.CompositionOffsets.emplace_back(NvdBe32(Data + 8 + Index * 8),
                                                      (INT32)NvdBe32(Data + 12 + Index * 8));
            }
            break;

        case NvdFourCc('s', 't', 's', 's'):
            for (Index = 0; Index < Count && 8 + Index * 4 + 4 <= Size; Index++)
                Track.SyncSamples.push_back(NvdBe32(Data + 8 + Index * 4));
            break;

        case NvdFourCc('s', 't', 's', 'z'):
            if (Size < 12)
                break;
            Track.ConstantSampleSize = NvdBe32(Data + 4);
            Track.SampleCount = NvdBe32(Data + 8);
            if (Track.ConstantSampleSize == 0)
            {
                for (Index = 0; Index < Track.SampleCount && 12 + Index * 4 + 4 <= Size; Index++)
                    Track.SampleSizes.push_back(NvdBe32(Data + 12 + Index * 4));
            }
            break;

        case NvdFourCc('s', 't', 's', 'c'):
            for (Index = 0; Index < Count && 8 + Index * 12 + 12 <= Size; Index++)
            {
                Track.SampleToChunk.push_back({ NvdBe32(Data + 8 + Index * 12),
                                                NvdBe32(Data + 12 + Index * 12) });
            }
            break;

        case NvdFourCc('s', 't', 'c', 'o'):
        case NvdFourCc('c', 'o', '6', '4'):
            IsLarge = (Type == NvdFourCc('c', 'o', '6', '4'));
            for (Index = 0; Index < Count; Index++)
            {
                EntryOffset = 8 + (SIZE_T)Index * (IsLarge ? 8 : 4);
                if (EntryOffset + (IsLarge ? 8 : 4) > Size)
                    break;
                Track.ChunkOffsets.push_back(IsLarge ? NvdBe64(Data + EntryOffset)
                                                     : NvdBe32(Data + EntryOffset));
            }
            break;
    }
}

VOID
Mp4Source::ParseContainer(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _Inout_ Mp4Track& Track,
    _Inout_ std::vector<Mp4Track>& Tracks)
{
    SIZE_T Offset = 0;
    Mp4Box Box;
    Mp4Track NewTrack;

    while (NvdReadBox(Data, Size, Offset, &Box))
    {
        const UINT8* Body = Data + Offset + Box.HeaderSize;
        SIZE_T BodySize = Box.Size - Box.HeaderSize;

        switch (Box.Type)
        {
            case NvdFourCc('t', 'r', 'a', 'k'):
                NewTrack = Mp4Track();
                ParseContainer(Body, BodySize, NewTrack, Tracks);
                Tracks.push_back(NewTrack);
                break;

            case NvdFourCc('m', 'd', 'i', 'a'):
            case NvdFourCc('m', 'i', 'n', 'f'):
            case NvdFourCc('s', 't', 'b', 'l'):
                ParseContainer(Body, BodySize, Track, Tracks);
                break;

            default:
                ParseLeaf(Box.Type, Body, BodySize, Track);
                break;
        }

        Offset += Box.Size;
    }
}

BOOLEAN
Mp4Source::BuildSamples(
    _In_ const Mp4Track& Track,
    _Out_ std::string& Error)
{
    UINT32 Run = 0;
    UINT32 Chunk;
    UINT32 InChunk;
    UINT32 PerChunk;
    UINT32 SampleIndex = 0;
    UINT32 Count;
    UINT64 Offset;
    UINT64 DecodeTime = 0;
    UINT64 TotalDuration = 0;
    INT64 Earliest = INT64_MAX;
    SIZE_T Index;
    SIZE_T Entry;
    SIZE_T Remaining;

    Count = Track.ConstantSampleSize ? Track.SampleCount : (UINT32)Track.SampleSizes.size();
    if (Count == 0 || Track.ChunkOffsets.empty() || Track.SampleToChunk.empty() || Track.Timescale == 0)
    {
        Error = "video track has no usable sample table (fragmented MP4 is not supported)";
        return FALSE;
    }

    Samples.resize(Count);

    /* Walk chunks in order, using the stsc run that covers each one */
    for (Chunk = 1; Chunk <= Track.ChunkOffsets.size() && SampleIndex < Count; Chunk++)
    {
        while (Run + 1 < Track.SampleToChunk.size() && Track.SampleToChunk[Run + 1].FirstChunk <= Chunk)
            Run++;

        PerChunk = Track.SampleToChunk[Run].SamplesPerChunk;
        Offset = Track.ChunkOffsets[Chunk - 1];
        for (InChunk = 0; InChunk < PerChunk && SampleIndex < Count; InChunk++, SampleIndex++)
        {
            Samples[SampleIndex].Offset = Offset;
            Samples[SampleIndex].Size = Track.ConstantSampleSize ? Track.ConstantSampleSize
                                                            : Track.SampleSizes[SampleIndex];
            Offset += Samples[SampleIndex].Size;
        }
    }
    Samples.resize(SampleIndex);

    Index = 0;
    for (const auto& Delta : Track.TimeToSample)
    {
        for (Remaining = Delta.first; Remaining > 0 && Index < Samples.size(); Remaining--, Index++)
        {
            Samples[Index].PresentationTime = (INT64)DecodeTime;
            DecodeTime += Delta.second;
            TotalDuration += Delta.second;
        }
    }

    Index = 0;
    for (const auto& Shift : Track.CompositionOffsets)
    {
        for (Remaining = Shift.first; Remaining > 0 && Index < Samples.size(); Remaining--, Index++)
            Samples[Index].PresentationTime += Shift.second;
    }

    for (const Sample& Each : Samples)
        Earliest = std::min(Earliest, Each.PresentationTime);

    /* Edit lists are ignored, so start the clock at the earliest presentation time */
    for (Sample& Each : Samples)
    {
        Each.PresentationTime = (Each.PresentationTime - Earliest) * 10000000 / Track.Timescale;
        Each.IsKeyFrame = Track.SyncSamples.empty();
    }

    for (UINT32 SyncSample : Track.SyncSamples)
    {
        Entry = SyncSample - 1;
        if (Entry < Samples.size())
            Samples[Entry].IsKeyFrame = TRUE;
    }

    if (TotalDuration > 0)
    {
        FrameRate = (double)Samples.size() * Track.Timescale / (double)TotalDuration;
        Duration = (INT64)(TotalDuration * 10000000 / Track.Timescale);
    }

    return TRUE;
}

BOOLEAN
Mp4Source::Open(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    std::vector<Mp4Track> Tracks;
    std::vector<UINT8> Movie;
    LARGE_INTEGER FileSize;
    Mp4Track Unused;
    UINT8 Header[16];
    UINT64 Offset = 0;
    UINT64 BoxSize;
    UINT32 HeaderSize;
    UINT32 Type;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error = "cannot open file";
        return FALSE;
    }
    GetFileSizeEx(File, &FileSize);

    /* Only moov is read into memory; mdat stays on disk */
    while (Offset + 8 <= (UINT64)FileSize.QuadPart)
    {
        if (!ReadAt(Offset, Header, 16 <= FileSize.QuadPart - Offset ? 16 : 8))
            break;

        BoxSize = NvdBe32(Header);
        Type = NvdBe32(Header + 4);
        HeaderSize = 8;
        if (BoxSize == 1)
        {
            BoxSize = NvdBe64(Header + 8);
            HeaderSize = 16;
        }
        else if (BoxSize == 0)
        {
            BoxSize = FileSize.QuadPart - Offset;
        }
        if (BoxSize < HeaderSize)
            break;

        if (Type == NvdFourCc('m', 'o', 'o', 'v'))
        {
            if (BoxSize > 512 * 1024 * 1024)
                break;
            Movie.resize((SIZE_T)(BoxSize - HeaderSize));
            if (!ReadAt(Offset + HeaderSize, Movie.data(), (UINT32)Movie.size()))
                Movie.clear();
            break;
        }

        Offset += BoxSize;
    }

    if (Movie.empty())
    {
        Error = "no moov box";
        return FALSE;
    }

    ParseContainer(Movie.data(), Movie.size(), Unused, Tracks);

    for (const Mp4Track& Track : Tracks)
    {
        if (Track.Handler != NvdFourCc('v', 'i', 'd', 'e'))
            continue;

        switch (Track.SampleEntry)
        {
            case NvdFourCc('a', 'v', 'c', '1'):
            case NvdFourCc('a', 'v', 'c', '3'):
                Codec = VideoCodec::H264;
                if (!NvdParseAvcConfig(Track.CodecConfig.data(), Track.CodecConfig.size(),
                                       ParameterSets, &LengthSize, Error))
                {
                    return FALSE;
                }
                break;

            case NvdFourCc('a', 'v', '0', '1'):
                Codec = VideoCodec::Av1;
                if (!NvdParseAv1Config(Track.CodecConfig.data(), Track.CodecConfig.size(), ParameterSets, Error))
                    return FALSE;
                break;

            default:
                Error = "first video track is neither H.264 nor AV1";
                return FALSE;
        }

        /* The track header is the presentation size; the sample entry may carry the coded one */
        if (Track.PresentationWidth != 0 && Track.PresentationHeight != 0)
            DisplaySize = { (LONG)Track.PresentationWidth, (LONG)Track.PresentationHeight };
        else
            DisplaySize = { (LONG)Track.Width, (LONG)Track.Height };
        return BuildSamples(Track, Error);
    }

    Error = "no video track";
    return FALSE;
}

BOOLEAN
Mp4Source::ReadAccessUnit(
    _Out_ AccessUnit& Unit)
{
    std::vector<UINT8> Payload;
    const Sample* Current;

    if (NextSample >= Samples.size())
        return FALSE;

    Current = &Samples[NextSample];
    Payload.resize(Current->Size);
    if (!ReadAt(Current->Offset, Payload.data(), Current->Size))
        return FALSE;

    Unit.Data.clear();
    Unit.Data.reserve(Payload.size() + ParameterSets.size() + 64);

    /* The first unit after open, rewind or seek carries the configuration record's contents */
    if (NeedsConfig)
    {
        Unit.Data = ParameterSets;
        NeedsConfig = FALSE;
    }

    /* AV1 samples are already sized OBUs; H.264 ones need start codes */
    if (Codec == VideoCodec::Av1)
        Unit.Data.insert(Unit.Data.end(), Payload.begin(), Payload.end());
    else
        NvdAppendAnnexB(Payload.data(), Payload.size(), LengthSize, Unit.Data);

    Unit.PresentationTime = Current->PresentationTime;
    Unit.IsKeyFrame = Current->IsKeyFrame;
    NextSample++;
    return TRUE;
}

std::unique_ptr<VideoSource>
NvdOpenMp4Source(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    std::unique_ptr<Mp4Source> Source = std::make_unique<Mp4Source>();

    if (!Source->Open(Path, Error))
        return nullptr;

    return Source;
}
