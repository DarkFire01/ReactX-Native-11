/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     OpenType container and table access
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"

constexpr UINT32
RxTag(
    _In_ CHAR A,
    _In_ CHAR B,
    _In_ CHAR C,
    _In_ CHAR D)
{
    return (static_cast<UINT32>(static_cast<UCHAR>(A)) << 24) | (static_cast<UINT32>(static_cast<UCHAR>(B)) << 16) |
           (static_cast<UINT32>(static_cast<UCHAR>(C)) << 8) | static_cast<UINT32>(static_cast<UCHAR>(D));
}

/* DWrite table tags are the four bytes in file order read as a little endian value */
constexpr UINT32
RxDWriteTag(
    _In_ UINT32 Tag)
{
    return ((Tag & 0xFF) << 24) | ((Tag & 0xFF00) << 8) | ((Tag >> 8) & 0xFF00) | (Tag >> 24);
}

/* Bounds checked big endian reads; anything past the end reads as zero */
class RxSpan
{
public:
    RxSpan() : Data(nullptr), Size(0) {}

    RxSpan(
        _In_reads_bytes_opt_(Length) const VOID* Base,
        _In_ UINT32 Length) : Data(static_cast<const UCHAR*>(Base)), Size(Base != nullptr ? Length : 0) {}

    BOOLEAN
    IsEmpty(VOID) const
    {
        return Size == 0;
    }

    BOOLEAN
    Has(
        _In_ UINT32 Offset,
        _In_ UINT32 Length) const
    {
        return Offset <= Size && Length <= Size - Offset;
    }

    UCHAR
    U8(
        _In_ UINT32 Offset) const
    {
        return Has(Offset, 1) ? Data[Offset] : 0;
    }

    USHORT
    U16(
        _In_ UINT32 Offset) const
    {
        if (!Has(Offset, 2))
            return 0;
        return static_cast<USHORT>((Data[Offset] << 8) | Data[Offset + 1]);
    }

    SHORT
    S16(
        _In_ UINT32 Offset) const
    {
        return static_cast<SHORT>(U16(Offset));
    }

    UINT32
    U24(
        _In_ UINT32 Offset) const
    {
        if (!Has(Offset, 3))
            return 0;
        return (static_cast<UINT32>(Data[Offset]) << 16) | (static_cast<UINT32>(Data[Offset + 1]) << 8) | Data[Offset + 2];
    }

    UINT32
    U32(
        _In_ UINT32 Offset) const
    {
        if (!Has(Offset, 4))
            return 0;
        return (static_cast<UINT32>(Data[Offset]) << 24) | (static_cast<UINT32>(Data[Offset + 1]) << 16) |
               (static_cast<UINT32>(Data[Offset + 2]) << 8) | Data[Offset + 3];
    }

    INT32
    S32(
        _In_ UINT32 Offset) const
    {
        return static_cast<INT32>(U32(Offset));
    }

    /* A sub range, clipped to what exists */
    RxSpan
    Sub(
        _In_ UINT32 Offset,
        _In_ UINT32 Length = MAXUINT32) const
    {
        if (Offset > Size)
            return RxSpan();
        if (Length > Size - Offset)
            Length = Size - Offset;
        return RxSpan(Data + Offset, Length);
    }

    const UCHAR*
    Bytes(VOID) const
    {
        return Data;
    }

    UINT32
    Length(VOID) const
    {
        return Size;
    }

private:
    const UCHAR* Data;
    UINT32 Size;
};

/* Version tags at the start of a font file */
constexpr UINT32 RxSfntVersionTrueType = 0x00010000;
constexpr UINT32 RxSfntVersionOpenType = RxTag('O', 'T', 'T', 'O');
constexpr UINT32 RxSfntVersionApple = RxTag('t', 'r', 'u', 'e');
constexpr UINT32 RxSfntCollection = RxTag('t', 't', 'c', 'f');

/* Classifies a whole font file and counts its faces */
VOID
RxAnalyzeFontFile(
    _In_ const RxSpan& File,
    _Out_ BOOL* IsSupported,
    _Out_ DWRITE_FONT_FILE_TYPE* FileType,
    _Out_ DWRITE_FONT_FACE_TYPE* FaceType,
    _Out_ UINT32* FaceCount);

/* The table directory of one face inside a font file */
class RxSfnt
{
public:
    RxSfnt() : DirectoryOffset(0), TableCount(0) {}

    /* Fails when FaceIndex is past the end of a collection or the directory is damaged */
    BOOLEAN
    Open(
        _In_ const RxSpan& File,
        _In_ UINT32 FaceIndex);

    /* Tag in big endian form, as RxTag builds it */
    RxSpan
    Table(
        _In_ UINT32 Tag) const;

    BOOLEAN
    HasTable(
        _In_ UINT32 Tag) const
    {
        return !Table(Tag).IsEmpty();
    }

    BOOLEAN
    IsCff(VOID) const
    {
        return HasTable(RxTag('C', 'F', 'F', ' ')) || HasTable(RxTag('C', 'F', 'F', '2'));
    }

    const RxSpan&
    FileData(VOID) const
    {
        return File;
    }

private:
    RxSpan File;
    UINT32 DirectoryOffset;
    UINT32 TableCount;
};
