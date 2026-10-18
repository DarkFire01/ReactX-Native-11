/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Raw H.264 Annex B elementary stream reader
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "source.hpp"
#include "h264syntax.hpp"
#include "bitreader.hpp"

class AnnexBSource final : public VideoSource
{
public:
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
        NextUnit = 0;
    }

    PCSTR
    GetContainerName() const override
    {
        return "H.264 Annex B";
    }

    VideoCodec
    GetCodec() const override
    {
        return VideoCodec::H264;
    }

    double
    GetFrameRate() const override
    {
        return 0;
    }

    UINT64
    GetFrameCount() const override
    {
        return 0;
    }

    INT64
    GetDuration() const override
    {
        return 0;
    }

    /* Without timestamps only the start of the stream is a known seek point */
    BOOLEAN
    SeekToKeyFrame(
        _In_ INT64 Time) override
    {
        if (Time > 0)
            return FALSE;

        Rewind();
        return TRUE;
    }

private:
    static
    BOOLEAN
    IsVcl(
        _In_ UINT8 Type)
    {
        return Type >= H264NalSlice && Type <= H264NalSliceIdr;
    }

    std::vector<UINT8> Stream;
    std::vector<H264NalUnit> Units;
    SIZE_T NextUnit = 0;
};

BOOLEAN
AnnexBSource::Open(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    LARGE_INTEGER Size;
    HANDLE File;
    DWORD Read = 0;
    BOOL IsRead;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error = "cannot open file";
        return FALSE;
    }

    GetFileSizeEx(File, &Size);
    if (Size.QuadPart > 2ll * 1024 * 1024 * 1024)
    {
        CloseHandle(File);
        Error = "elementary streams over 2 GB are not supported";
        return FALSE;
    }

    Stream.resize((SIZE_T)Size.QuadPart);
    IsRead = ReadFile(File, Stream.data(), (DWORD)Stream.size(), &Read, NULL);
    CloseHandle(File);
    if (!IsRead || Read != Stream.size())
    {
        Error = "read failed";
        return FALSE;
    }

    H264SplitAnnexB(Stream.data(), Stream.size(), Units);
    if (Units.empty())
    {
        Error = "no start codes found";
        return FALSE;
    }

    return TRUE;
}

BOOLEAN
AnnexBSource::ReadAccessUnit(
    _Out_ AccessUnit& Unit)
{
    static const UINT8 StartCode[3] = { 0, 0, 1 };
    BOOLEAN HasSlice = FALSE;
    UINT32 FirstMb;

    Unit.Data.clear();
    Unit.PresentationTime = NvdNoTimestamp;
    Unit.IsKeyFrame = FALSE;

    while (NextUnit < Units.size())
    {
        const H264NalUnit& Nal = Units[NextUnit];
        const UINT8* Payload = &Stream[Nal.PayloadOffset];

        if (Nal.PayloadSize == 0)
        {
            NextUnit++;
            continue;
        }

        /* Anything other than a further slice of the same picture starts the next unit */
        if (HasSlice)
        {
            if (!IsVcl(Nal.Type))
                break;

            BitReader Reader(Payload + 1, Nal.PayloadSize - 1);
            FirstMb = Reader.ReadUe();
            if (FirstMb == 0)
                break;
        }

        Unit.Data.insert(Unit.Data.end(), StartCode, StartCode + 3);
        Unit.Data.insert(Unit.Data.end(), Payload, Payload + Nal.PayloadSize);
        if (IsVcl(Nal.Type))
            HasSlice = TRUE;
        if (Nal.Type == H264NalSliceIdr)
            Unit.IsKeyFrame = TRUE;
        NextUnit++;
    }

    return !Unit.Data.empty();
}

std::unique_ptr<VideoSource>
NvdOpenAnnexBSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    std::unique_ptr<AnnexBSource> Source = std::make_unique<AnnexBSource>();

    if (!Source->Open(Path, Error))
        return nullptr;

    return Source;
}
