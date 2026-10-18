/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Matroska and WebM reader for the first AV1 or H.264 video track
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "source.hpp"

#include <algorithm>

enum MatroskaId : UINT32
{
    MkvEbml = 0x1A45DFA3,
    MkvSegment = 0x18538067,
    MkvSeekHead = 0x114D9B74,
    MkvSeek = 0x4DBB,
    MkvSeekId = 0x53AB,
    MkvSeekPosition = 0x53AC,
    MkvInfo = 0x1549A966,
    MkvTimecodeScale = 0x2AD7B1,
    MkvDuration = 0x4489,
    MkvTracks = 0x1654AE6B,
    MkvTrackEntry = 0xAE,
    MkvTrackNumber = 0xD7,
    MkvTrackType = 0x83,
    MkvCodecId = 0x86,
    MkvCodecPrivate = 0x63A2,
    MkvDefaultDuration = 0x23E383,
    MkvVideo = 0xE0,
    MkvPixelWidth = 0xB0,
    MkvPixelHeight = 0xBA,
    MkvCluster = 0x1F43B675,
    MkvTimecode = 0xE7,
    MkvSimpleBlock = 0xA3,
    MkvBlockGroup = 0xA0,
    MkvBlock = 0xA1,
    MkvReferenceBlock = 0xFB,
    MkvCues = 0x1C53BB6B,
    MkvCuePoint = 0xBB,
    MkvCueTime = 0xB3,
    MkvCueTrackPositions = 0xB7,
    MkvCueTrack = 0xF7,
    MkvCueClusterPosition = 0xF1,
};

constexpr UINT64 MkvUnknownSize = ~0ull;

static const UINT8 WebMDocType[4] = { 'w', 'e', 'b', 'm' };

struct MkvElement
{
    UINT32 Id;
    UINT64 Size;
    UINT64 DataOffset;
};

/* In-memory cursor over an element's payload */
class MkvReader
{
public:
    MkvReader(
        _In_reads_bytes_(Size) const UINT8* Data,
        _In_ SIZE_T Size)
        : Data(Data),
          Size(Size)
    {
    }

    BOOLEAN
    Next(
        _Out_ UINT32* Id,
        _Out_ const UINT8** Payload,
        _Out_ SIZE_T* PayloadSize)
    {
        UINT64 Length;

        if (!ReadId(Id) || !ReadSize(&Length) || Length == MkvUnknownSize || Length > Size - Offset)
            return FALSE;

        *Payload = Data + Offset;
        *PayloadSize = (SIZE_T)Length;
        Offset += (SIZE_T)Length;
        return TRUE;
    }

    BOOLEAN
    ReadId(
        _Out_ UINT32* Id)
    {
        UINT Length = 1;
        UINT Index;

        if (Offset >= Size || Data[Offset] == 0)
            return FALSE;
        while (!(Data[Offset] & (0x80 >> (Length - 1))) && Length < 4)
            Length++;
        if (Offset + Length > Size)
            return FALSE;

        *Id = 0;
        for (Index = 0; Index < Length; Index++)
            *Id = (*Id << 8) | Data[Offset + Index];
        Offset += Length;
        return TRUE;
    }

    BOOLEAN
    ReadSize(
        _Out_ UINT64* Value)
    {
        UINT Length = 1;
        UINT Index;
        BOOLEAN IsAllOnes;

        if (Offset >= Size || Data[Offset] == 0)
            return FALSE;
        while (!(Data[Offset] & (0x80 >> (Length - 1))))
            Length++;
        if (Offset + Length > Size)
            return FALSE;

        *Value = Data[Offset] & (0xFF >> Length);
        IsAllOnes = (*Value == (0xFFu >> Length));
        for (Index = 1; Index < Length; Index++)
        {
            *Value = (*Value << 8) | Data[Offset + Index];
            IsAllOnes = IsAllOnes && Data[Offset + Index] == 0xFF;
        }
        Offset += Length;

        if (IsAllOnes)
            *Value = MkvUnknownSize;
        return TRUE;
    }

    SIZE_T
    GetOffset() const
    {
        return Offset;
    }

private:
    const UINT8* Data;
    SIZE_T Size;
    SIZE_T Offset = 0;
};

static
UINT64
MkvUnsigned(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size)
{
    UINT64 Value = 0;
    SIZE_T Index;

    for (Index = 0; Index < Size && Index < 8; Index++)
        Value = (Value << 8) | Data[Index];
    return Value;
}

static
double
MkvFloat(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size)
{
    UINT64 Bits = MkvUnsigned(Data, Size);
    UINT32 Narrow;
    double Wide;
    float Single;

    if (Size == 4)
    {
        Narrow = (UINT32)Bits;
        memcpy(&Single, &Narrow, sizeof(Single));
        return Single;
    }

    memcpy(&Wide, &Bits, sizeof(Wide));
    return (Size == 8) ? Wide : 0;
}

static
SIZE
MkvPixelSize(
    _In_reads_bytes_(Size) const UINT8* Video,
    _In_ SIZE_T Size)
{
    MkvReader Fields(Video, Size);
    SIZE Result = {};
    const UINT8* Value;
    SIZE_T ValueSize;
    UINT32 Id;

    while (Fields.Next(&Id, &Value, &ValueSize))
    {
        if (Id == MkvPixelWidth)
            Result.cx = (LONG)MkvUnsigned(Value, ValueSize);
        else if (Id == MkvPixelHeight)
            Result.cy = (LONG)MkvUnsigned(Value, ValueSize);
    }

    return Result;
}

class MatroskaSource final : public VideoSource
{
public:
    ~MatroskaSource() override
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
        Position = FirstCluster;
        NeedsConfig = TRUE;
    }

    PCSTR
    GetContainerName() const override
    {
        return IsWebM ? "WebM" : "Matroska";
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
        return 0;
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
    struct CuePoint
    {
        INT64 Time;
        UINT64 ClusterOffset;
    };

    BOOLEAN
    ReadAt(
        _In_ UINT64 Offset,
        _Out_writes_bytes_(Size) VOID* Buffer,
        _In_ UINT32 Size);

    BOOLEAN
    ReadElementHeader(
        _In_ UINT64 Offset,
        _Out_ MkvElement* Element);

    BOOLEAN
    ReadPayload(
        _In_ const MkvElement& Element,
        _Out_ std::vector<UINT8>& Payload);

    VOID
    ParseInfo(
        _In_ const std::vector<UINT8>& Payload);

    BOOLEAN
    ParseTracks(
        _In_ const std::vector<UINT8>& Payload,
        _Out_ std::string& Error);

    VOID
    ParseSeekHead(
        _In_ const std::vector<UINT8>& Payload);

    VOID
    ParseCues(
        _In_ const std::vector<UINT8>& Payload);

    /* Fills Unit when the block belongs to the video track */
    BOOLEAN
    ParseBlock(
        _In_reads_bytes_(Size) const UINT8* Block,
        _In_ SIZE_T Size,
        _In_ BOOLEAN IsSimple,
        _In_ BOOLEAN HasReference,
        _Out_ AccessUnit& Unit);

    HANDLE File = INVALID_HANDLE_VALUE;
    UINT64 FileSize = 0;
    UINT64 SegmentStart = 0;
    UINT64 SegmentEnd = 0;
    UINT64 FirstCluster = 0;
    UINT64 Position = 0;
    UINT64 CuesOffset = 0;

    UINT64 TimecodeScale = 1000000;
    INT64 ClusterTime = 0;
    INT64 Duration = 0;
    double FrameRate = 0;
    BOOLEAN IsWebM = FALSE;

    UINT64 VideoTrack = 0;
    VideoCodec Codec = VideoCodec::Av1;
    std::vector<UINT8> ParameterSets;
    UINT32 LengthSize = 4;
    BOOLEAN NeedsConfig = TRUE;
    std::vector<CuePoint> Cues;
};

BOOLEAN
MatroskaSource::ReadAt(
    _In_ UINT64 Offset,
    _Out_writes_bytes_(Size) VOID* Buffer,
    _In_ UINT32 Size)
{
    OVERLAPPED At = {};
    DWORD Read = 0;

    At.Offset = (DWORD)Offset;
    At.OffsetHigh = (DWORD)(Offset >> 32);
    return ReadFile(File, Buffer, Size, &Read, &At) && Read == Size;
}

BOOLEAN
MatroskaSource::ReadElementHeader(
    _In_ UINT64 Offset,
    _Out_ MkvElement* Element)
{
    UINT8 Header[12] = {};
    UINT32 Available;

    if (Offset >= FileSize)
        return FALSE;

    Available = (UINT32)std::min<UINT64>(sizeof(Header), FileSize - Offset);
    if (!ReadAt(Offset, Header, Available))
        return FALSE;

    MkvReader Reader(Header, Available);
    if (!Reader.ReadId(&Element->Id) || !Reader.ReadSize(&Element->Size))
        return FALSE;

    Element->DataOffset = Offset + Reader.GetOffset();
    return TRUE;
}

BOOLEAN
MatroskaSource::ReadPayload(
    _In_ const MkvElement& Element,
    _Out_ std::vector<UINT8>& Payload)
{
    if (Element.Size == MkvUnknownSize || Element.Size > 256ull * 1024 * 1024 ||
        Element.DataOffset + Element.Size > FileSize)
    {
        return FALSE;
    }

    Payload.resize((SIZE_T)Element.Size);
    return Payload.empty() || ReadAt(Element.DataOffset, Payload.data(), (UINT32)Payload.size());
}

VOID
MatroskaSource::ParseInfo(
    _In_ const std::vector<UINT8>& Payload)
{
    MkvReader Reader(Payload.data(), Payload.size());
    const UINT8* Value;
    double Length = 0;
    SIZE_T Size;
    UINT32 Id;

    while (Reader.Next(&Id, &Value, &Size))
    {
        if (Id == MkvTimecodeScale)
            TimecodeScale = MkvUnsigned(Value, Size);
        else if (Id == MkvDuration)
            Length = MkvFloat(Value, Size);
    }

    if (TimecodeScale == 0)
        TimecodeScale = 1000000;
    Duration = (INT64)(Length * (double)TimecodeScale / 100.0);
}

BOOLEAN
MatroskaSource::ParseTracks(
    _In_ const std::vector<UINT8>& Payload,
    _Out_ std::string& Error)
{
    MkvReader Tracks(Payload.data(), Payload.size());
    std::string CodecId;
    std::vector<UINT8> Private;
    const UINT8* Entry;
    const UINT8* Value;
    UINT64 Number;
    UINT64 Type;
    UINT64 DefaultDuration;
    SIZE PictureSize;
    SIZE_T EntrySize;
    SIZE_T Size;
    UINT32 Id;

    while (Tracks.Next(&Id, &Entry, &EntrySize))
    {
        if (Id != MkvTrackEntry)
            continue;

        MkvReader Fields(Entry, EntrySize);
        Number = 0;
        Type = 0;
        DefaultDuration = 0;
        PictureSize = {};
        CodecId.clear();
        Private.clear();

        while (Fields.Next(&Id, &Value, &Size))
        {
            switch (Id)
            {
                case MkvTrackNumber:
                    Number = MkvUnsigned(Value, Size);
                    break;
                case MkvTrackType:
                    Type = MkvUnsigned(Value, Size);
                    break;
                case MkvCodecId:
                    CodecId.assign((const char*)Value, Size);
                    break;
                case MkvCodecPrivate:
                    Private.assign(Value, Value + Size);
                    break;
                case MkvDefaultDuration:
                    DefaultDuration = MkvUnsigned(Value, Size);
                    break;
                case MkvVideo:
                    PictureSize = MkvPixelSize(Value, Size);
                    break;
            }
        }

        /* Track type 1 is video; the first one wins */
        if (Type != 1)
            continue;

        if (CodecId.compare(0, 5, "V_AV1") == 0)
        {
            Codec = VideoCodec::Av1;
            if (!Private.empty() && !NvdParseAv1Config(Private.data(), Private.size(), ParameterSets, Error))
                return FALSE;
        }
        else if (CodecId == "V_MPEG4/ISO/AVC")
        {
            Codec = VideoCodec::H264;
            if (!NvdParseAvcConfig(Private.data(), Private.size(), ParameterSets, &LengthSize, Error))
                return FALSE;
        }
        else
        {
            Error = "video track codec " + CodecId + " is not supported (AV1 and H.264 are)";
            return FALSE;
        }

        VideoTrack = Number;
        DisplaySize = PictureSize;
        if (DefaultDuration != 0)
            FrameRate = 1e9 / (double)DefaultDuration;
        return TRUE;
    }

    Error = "no video track";
    return FALSE;
}

VOID
MatroskaSource::ParseSeekHead(
    _In_ const std::vector<UINT8>& Payload)
{
    MkvReader Entries(Payload.data(), Payload.size());
    const UINT8* Entry;
    const UINT8* Value;
    UINT64 SeekId;
    UINT64 SeekPosition;
    SIZE_T EntrySize;
    SIZE_T Size;
    UINT32 Id;

    while (Entries.Next(&Id, &Entry, &EntrySize))
    {
        if (Id != MkvSeek)
            continue;

        MkvReader Fields(Entry, EntrySize);
        SeekId = 0;
        SeekPosition = 0;
        while (Fields.Next(&Id, &Value, &Size))
        {
            if (Id == MkvSeekId)
                SeekId = MkvUnsigned(Value, Size);
            else if (Id == MkvSeekPosition)
                SeekPosition = MkvUnsigned(Value, Size);
        }

        if (SeekId == MkvCues)
            CuesOffset = SegmentStart + SeekPosition;
    }
}

VOID
MatroskaSource::ParseCues(
    _In_ const std::vector<UINT8>& Payload)
{
    MkvReader Points(Payload.data(), Payload.size());
    const UINT8* Point;
    const UINT8* Value;
    const UINT8* Track;
    CuePoint Cue;
    UINT64 CueTrack;
    UINT64 ClusterPosition;
    SIZE_T PointSize;
    SIZE_T TrackSize;
    SIZE_T Size;
    UINT32 Id;
    UINT32 TrackId;

    Cues.clear();
    while (Points.Next(&Id, &Point, &PointSize))
    {
        if (Id != MkvCuePoint)
            continue;

        MkvReader Fields(Point, PointSize);
        Cue = {};
        while (Fields.Next(&Id, &Value, &Size))
        {
            if (Id == MkvCueTime)
            {
                Cue.Time = (INT64)(MkvUnsigned(Value, Size) * TimecodeScale / 100);
                continue;
            }
            if (Id != MkvCueTrackPositions)
                continue;

            MkvReader Positions(Value, Size);
            CueTrack = 0;
            ClusterPosition = 0;
            while (Positions.Next(&TrackId, &Track, &TrackSize))
            {
                if (TrackId == MkvCueTrack)
                    CueTrack = MkvUnsigned(Track, TrackSize);
                else if (TrackId == MkvCueClusterPosition)
                    ClusterPosition = MkvUnsigned(Track, TrackSize);
            }

            if (CueTrack == VideoTrack)
            {
                Cue.ClusterOffset = SegmentStart + ClusterPosition;
                Cues.push_back(Cue);
            }
        }
    }
}

BOOLEAN
MatroskaSource::Open(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    std::vector<UINT8> Payload;
    LARGE_INTEGER Size;
    MkvElement Element;
    UINT64 Offset;
    BOOLEAN HasTracks = FALSE;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error = "cannot open file";
        return FALSE;
    }
    GetFileSizeEx(File, &Size);
    FileSize = (UINT64)Size.QuadPart;

    if (!ReadElementHeader(0, &Element) || Element.Id != MkvEbml || !ReadPayload(Element, Payload))
    {
        Error = "not an EBML file";
        return FALSE;
    }
    IsWebM = std::search(Payload.begin(), Payload.end(), std::begin(WebMDocType), std::end(WebMDocType)) !=
             Payload.end();

    Offset = Element.DataOffset + Element.Size;
    if (!ReadElementHeader(Offset, &Element) || Element.Id != MkvSegment)
    {
        Error = "no Segment element";
        return FALSE;
    }
    SegmentStart = Element.DataOffset;
    SegmentEnd = (Element.Size == MkvUnknownSize) ? FileSize : std::min(FileSize, SegmentStart + Element.Size);

    /* Everything needed for playback sits ahead of the first cluster, except Cues sometimes */
    for (Offset = SegmentStart; Offset < SegmentEnd && ReadElementHeader(Offset, &Element);)
    {
        if (Element.Id == MkvCluster)
        {
            FirstCluster = Offset;
            break;
        }
        if (Element.Size == MkvUnknownSize)
            break;

        switch (Element.Id)
        {
            case MkvInfo:
                if (ReadPayload(Element, Payload))
                    ParseInfo(Payload);
                break;

            case MkvTracks:
                if (!ReadPayload(Element, Payload) || !ParseTracks(Payload, Error))
                {
                    if (Error.empty())
                        Error = "unreadable Tracks element";
                    return FALSE;
                }
                HasTracks = TRUE;
                break;

            case MkvSeekHead:
                if (ReadPayload(Element, Payload))
                    ParseSeekHead(Payload);
                break;

            case MkvCues:
                CuesOffset = Offset;
                break;
        }

        Offset = Element.DataOffset + Element.Size;
    }

    if (!HasTracks || FirstCluster == 0)
    {
        Error = HasTracks ? "no clusters" : "no Tracks element";
        return FALSE;
    }

    if (CuesOffset != 0 && ReadElementHeader(CuesOffset, &Element) && Element.Id == MkvCues &&
        ReadPayload(Element, Payload))
    {
        ParseCues(Payload);
    }

    Position = FirstCluster;
    return TRUE;
}

BOOLEAN
MatroskaSource::ParseBlock(
    _In_reads_bytes_(Size) const UINT8* Block,
    _In_ SIZE_T Size,
    _In_ BOOLEAN IsSimple,
    _In_ BOOLEAN HasReference,
    _Out_ AccessUnit& Unit)
{
    MkvReader Reader(Block, Size);
    UINT64 Track;
    INT16 RelativeTime;
    UINT8 Flags;
    SIZE_T Offset;

    if (!Reader.ReadSize(&Track) || Track != VideoTrack)
        return FALSE;

    Offset = Reader.GetOffset();
    if (Offset + 3 > Size)
        return FALSE;

    RelativeTime = (INT16)((Block[Offset] << 8) | Block[Offset + 1]);
    Flags = Block[Offset + 2];
    Offset += 3;

    if ((Flags & 0x06) != 0)
    {
        wprintf(L"warning: laced video blocks are not supported, skipped\n");
        return FALSE;
    }

    Unit.Data.clear();
    if (NeedsConfig)
    {
        Unit.Data = ParameterSets;
        NeedsConfig = FALSE;
    }

    if (Codec == VideoCodec::Av1)
        Unit.Data.insert(Unit.Data.end(), Block + Offset, Block + Size);
    else
        NvdAppendAnnexB(Block + Offset, Size - Offset, LengthSize, Unit.Data);

    Unit.PresentationTime = (INT64)((ClusterTime + RelativeTime) * (INT64)TimecodeScale / 100);
    Unit.IsKeyFrame = IsSimple ? (Flags & 0x80) != 0 : !HasReference;
    return TRUE;
}

BOOLEAN
MatroskaSource::ReadAccessUnit(
    _Out_ AccessUnit& Unit)
{
    std::vector<UINT8> Payload;
    const UINT8* Value;
    const UINT8* Block;
    MkvElement Element;
    BOOLEAN HasReference;
    SIZE_T BlockSize;
    SIZE_T Size;
    UINT32 Id;

    /*
     * Clusters are entered rather than skipped, so their children read as a flat run;
     * that also copes with clusters of unknown size.
     */
    while (Position < SegmentEnd && ReadElementHeader(Position, &Element))
    {
        if (Element.Id == MkvCluster)
        {
            Position = Element.DataOffset;
            continue;
        }

        if (Element.Size == MkvUnknownSize)
            return FALSE;
        Position = Element.DataOffset + Element.Size;

        switch (Element.Id)
        {
            case MkvTimecode:
                if (ReadPayload(Element, Payload))
                    ClusterTime = (INT64)MkvUnsigned(Payload.data(), Payload.size());
                break;

            case MkvSimpleBlock:
                if (ReadPayload(Element, Payload) && ParseBlock(Payload.data(), Payload.size(), TRUE, FALSE, Unit))
                    return TRUE;
                break;

            case MkvBlockGroup:
                if (!ReadPayload(Element, Payload))
                    break;

                Block = nullptr;
                BlockSize = 0;
                HasReference = FALSE;
                {
                    MkvReader Group(Payload.data(), Payload.size());
                    while (Group.Next(&Id, &Value, &Size))
                    {
                        if (Id == MkvBlock)
                        {
                            Block = Value;
                            BlockSize = Size;
                        }
                        else if (Id == MkvReferenceBlock)
                        {
                            HasReference = TRUE;
                        }
                    }
                }

                if (Block != nullptr && ParseBlock(Block, BlockSize, FALSE, HasReference, Unit))
                    return TRUE;
                break;
        }
    }

    return FALSE;
}

BOOLEAN
MatroskaSource::SeekToKeyFrame(
    _In_ INT64 Time)
{
    UINT64 Target = FirstCluster;

    /* Cue points name clusters that start with a key frame of the track */
    for (const CuePoint& Cue : Cues)
    {
        if (Cue.Time <= Time)
            Target = Cue.ClusterOffset;
    }

    Position = Target;
    NeedsConfig = TRUE;
    return TRUE;
}

std::unique_ptr<VideoSource>
NvdOpenMatroskaSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    std::unique_ptr<MatroskaSource> Source = std::make_unique<MatroskaSource>();

    if (!Source->Open(Path, Error))
        return nullptr;

    return Source;
}
