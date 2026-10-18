/*
 * PROJECT:     ReactX NativeVideoDecode
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Codec configuration records shared by the container readers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "source.hpp"

BOOLEAN
NvdParseAvcConfig(
    _In_reads_bytes_(Size) const UINT8* Config,
    _In_ SIZE_T Size,
    _Out_ std::vector<UINT8>& ParameterSets,
    _Out_ UINT32* LengthSize,
    _Out_ std::string& Error)
{
    static const UINT8 StartCode[4] = { 0, 0, 0, 1 };
    SIZE_T Offset = 6;
    UINT32 Count;
    UINT32 Length;
    UINT32 Index;
    UINT32 Group;

    ParameterSets.clear();
    *LengthSize = 4;

    if (Size < 7 || Config[0] != 1)
    {
        Error = "avcC record is missing or malformed";
        return FALSE;
    }

    *LengthSize = (Config[4] & 3) + 1;

    /* SPS group first, then the PPS group with its own count byte */
    for (Group = 0; Group < 2; Group++)
    {
        if (Offset >= Size)
            break;
        Count = (Group == 0) ? (Config[5] & 0x1F) : Config[Offset++];

        for (Index = 0; Index < Count; Index++)
        {
            if (Offset + 2 > Size)
                break;
            Length = ((UINT32)Config[Offset] << 8) | Config[Offset + 1];
            Offset += 2;
            if (Offset + Length > Size)
                break;

            ParameterSets.insert(ParameterSets.end(), StartCode, StartCode + 4);
            ParameterSets.insert(ParameterSets.end(), Config + Offset, Config + Offset + Length);
            Offset += Length;
        }
    }

    return TRUE;
}

BOOLEAN
NvdParseAv1Config(
    _In_reads_bytes_(Size) const UINT8* Config,
    _In_ SIZE_T Size,
    _Out_ std::vector<UINT8>& ConfigObus,
    _Out_ std::string& Error)
{
    ConfigObus.clear();

    /* marker bit and version 1 make the first byte 0x81 */
    if (Size < 4 || Config[0] != 0x81)
    {
        Error = "av1C record is missing or malformed";
        return FALSE;
    }

    ConfigObus.assign(Config + 4, Config + Size);
    return TRUE;
}

VOID
NvdAppendAnnexB(
    _In_reads_bytes_(Size) const UINT8* Data,
    _In_ SIZE_T Size,
    _In_ UINT32 LengthSize,
    _Inout_ std::vector<UINT8>& Out)
{
    SIZE_T Offset = 0;
    UINT32 Length;
    UINT32 Byte;

    while (Offset + LengthSize <= Size)
    {
        Length = 0;
        for (Byte = 0; Byte < LengthSize; Byte++)
            Length = (Length << 8) | Data[Offset + Byte];
        Offset += LengthSize;
        if (Length == 0 || Length > Size - Offset)
            break;

        Out.push_back(0);
        Out.push_back(0);
        Out.push_back(1);
        Out.insert(Out.end(), Data + Offset, Data + Offset + Length);
        Offset += Length;
    }
}

std::unique_ptr<VideoSource>
NvdOpenSource(
    _In_z_ PCWSTR Path,
    _Out_ std::string& Error)
{
    UINT8 Header[12] = {};
    DWORD Read = 0;
    HANDLE File;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        Error = "cannot open file";
        return nullptr;
    }
    ReadFile(File, Header, sizeof(Header), &Read, NULL);
    CloseHandle(File);

    /* EBML magic for Matroska and WebM, ftyp for ISO BMFF; anything else is treated as Annex B */
    if (Read >= 4 && Header[0] == 0x1A && Header[1] == 0x45 && Header[2] == 0xDF && Header[3] == 0xA3)
        return NvdOpenMatroskaSource(Path, Error);
    if (Read >= 8 && memcmp(Header + 4, "ftyp", 4) == 0)
        return NvdOpenMp4Source(Path, Error);

    return NvdOpenAnnexBSource(Path, Error);
}
