/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     OpenType container and table access
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "sfnt.hpp"

/* A table record is tag, checksum, offset, length */
constexpr UINT32 RxTableRecordSize = 16;

static
BOOLEAN
RxIsSfntVersion(
    _In_ UINT32 Version)
{
    return Version == RxSfntVersionTrueType || Version == RxSfntVersionOpenType || Version == RxSfntVersionApple;
}

VOID
RxAnalyzeFontFile(
    _In_ const RxSpan& File,
    _Out_ BOOL* IsSupported,
    _Out_ DWRITE_FONT_FILE_TYPE* FileType,
    _Out_ DWRITE_FONT_FACE_TYPE* FaceType,
    _Out_ UINT32* FaceCount)
{
    UINT32 Version = File.U32(0);
    RxSfnt Face;

    *IsSupported = FALSE;
    *FileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    *FaceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    *FaceCount = 0;

    if (Version == RxSfntCollection)
    {
        UINT32 Count = File.U32(8);

        if (Count == 0 || !File.Has(12, Count * 4) || !Face.Open(File, 0))
            return;

        *FileType = DWRITE_FONT_FILE_TYPE_OPENTYPE_COLLECTION;
        *FaceType = DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION;
        *FaceCount = Count;
        *IsSupported = TRUE;
        return;
    }

    if (!RxIsSfntVersion(Version) || !Face.Open(File, 0))
        return;

    *FaceCount = 1;
    if (Face.IsCff())
    {
        *FileType = DWRITE_FONT_FILE_TYPE_CFF;
        *FaceType = DWRITE_FONT_FACE_TYPE_CFF;
    }
    else
    {
        *FileType = DWRITE_FONT_FILE_TYPE_TRUETYPE;
        *FaceType = DWRITE_FONT_FACE_TYPE_TRUETYPE;
    }
    *IsSupported = TRUE;
}

BOOLEAN
RxSfnt::Open(
    _In_ const RxSpan& Data,
    _In_ UINT32 FaceIndex)
{
    UINT32 Offset = 0;
    UINT32 Count;

    File = Data;
    TableCount = 0;

    if (File.U32(0) == RxSfntCollection)
    {
        if (FaceIndex >= File.U32(8))
            return FALSE;
        Offset = File.U32(12 + FaceIndex * 4);
    }
    else if (FaceIndex != 0)
    {
        return FALSE;
    }

    if (!RxIsSfntVersion(File.U32(Offset)))
        return FALSE;

    Count = File.U16(Offset + 4);
    if (!File.Has(Offset + 12, Count * RxTableRecordSize))
        return FALSE;

    DirectoryOffset = Offset + 12;
    TableCount = Count;
    return TRUE;
}

RxSpan
RxSfnt::Table(
    _In_ UINT32 Tag) const
{
    UINT32 Low = 0;
    UINT32 High = TableCount;

    /* Records are sorted by tag, but some fonts get that wrong; fall back to a scan */
    while (Low < High)
    {
        UINT32 Middle = (Low + High) / 2;
        UINT32 Record = DirectoryOffset + Middle * RxTableRecordSize;
        UINT32 Found = File.U32(Record);

        if (Found == Tag)
            return File.Sub(File.U32(Record + 8), File.U32(Record + 12));
        if (Found < Tag)
            Low = Middle + 1;
        else
            High = Middle;
    }

    for (UINT32 Index = 0; Index < TableCount; Index++)
    {
        UINT32 Record = DirectoryOffset + Index * RxTableRecordSize;

        if (File.U32(Record) == Tag)
            return File.Sub(File.U32(Record + 8), File.U32(Record + 12));
    }

    return RxSpan();
}
