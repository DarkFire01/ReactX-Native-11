/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Parsed tables of one font face, shared by every object that uses the face
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "fontfile.hpp"

/* Header fields from head, hhea, vhea, maxp, OS/2 and post */
struct RxFaceHeader
{
    USHORT UnitsPerEm;
    SHORT XMin;
    SHORT YMin;
    SHORT XMax;
    SHORT YMax;
    USHORT MacStyle;
    SHORT IndexToLocFormat;

    SHORT HheaAscender;
    SHORT HheaDescender;
    SHORT HheaLineGap;
    SHORT CaretSlopeRise;
    SHORT CaretSlopeRun;
    SHORT CaretOffset;
    USHORT HMetricCount;

    BOOLEAN HasVhea;
    SHORT VheaAscent;
    SHORT VheaDescent;
    SHORT VheaLineGap;
    USHORT VMetricCount;

    USHORT GlyphCount;
    /* TrueType maxp version 1 limits the interpreter sizes its stores from */
    USHORT MaxZones;
    USHORT MaxTwilightPoints;
    USHORT MaxStorage;
    USHORT MaxFunctionDefs;
    USHORT MaxInstructionDefs;
    USHORT MaxStackElements;
    USHORT MaxPoints;
    USHORT MaxContours;
    USHORT MaxComponentPoints;
    USHORT MaxComponentContours;

    BOOLEAN HasOs2;
    USHORT Os2Version;
    SHORT XAvgCharWidth;
    USHORT WeightClass;
    USHORT WidthClass;
    USHORT FsType;
    SHORT SubscriptXSize;
    SHORT SubscriptYSize;
    SHORT SubscriptXOffset;
    SHORT SubscriptYOffset;
    SHORT SuperscriptXSize;
    SHORT SuperscriptYSize;
    SHORT SuperscriptXOffset;
    SHORT SuperscriptYOffset;
    SHORT StrikeoutSize;
    SHORT StrikeoutPosition;
    SHORT FamilyClass;
    BYTE Panose[10];
    UINT32 UnicodeRange[4];
    UINT32 VendorId;
    USHORT FsSelection;
    USHORT FirstCharIndex;
    USHORT LastCharIndex;
    SHORT TypoAscender;
    SHORT TypoDescender;
    SHORT TypoLineGap;
    USHORT WinAscent;
    USHORT WinDescent;
    UINT32 CodePageRange[2];
    SHORT XHeight;
    SHORT CapHeight;

    INT32 ItalicAngle;
    SHORT UnderlinePosition;
    SHORT UnderlineThickness;
    UINT32 IsFixedPitch;
};

/* OS/2 fsSelection bits */
constexpr USHORT RxFsSelectionItalic = 0x0001;
constexpr USHORT RxFsSelectionBold = 0x0020;
constexpr USHORT RxFsSelectionRegular = 0x0040;
constexpr USHORT RxFsSelectionUseTypoMetrics = 0x0080;
constexpr USHORT RxFsSelectionWws = 0x0100;
constexpr USHORT RxFsSelectionOblique = 0x0200;

/* head macStyle bits */
constexpr USHORT RxMacStyleBold = 0x0001;
constexpr USHORT RxMacStyleItalic = 0x0002;

/* An outline point in font units; Flags bit 0 is on curve */
struct RxOutlinePoint
{
    INT32 X;
    INT32 Y;
    BYTE OnCurve;
};

/* A glyph outline in font units with what the hinter needs alongside it */
struct RxGlyphOutline
{
    std::vector<RxOutlinePoint> Points;
    std::vector<USHORT> ContourEnds;
    /* Instructions of a simple glyph, or of the composite itself */
    RxSpan Instructions;
    SHORT XMin;
    SHORT YMin;
    SHORT XMax;
    SHORT YMax;
    BOOLEAN IsComposite;

    VOID
    Clear(VOID)
    {
        Points.clear();
        ContourEnds.clear();
        Instructions = RxSpan();
        XMin = YMin = XMax = YMax = 0;
        IsComposite = FALSE;
    }
};

/* One face of a file, read once and shared */
class RxFaceTables
{
public:
    static
    HRESULT
    Create(
        _In_ IDWriteFontFile* File,
        _In_ UINT32 FaceIndex,
        _Outptr_ RxFaceTables** Tables);

    VOID
    AddRef(VOID)
    {
        InterlockedIncrement(&RefCount);
    }

    VOID
    Release(VOID)
    {
        if (InterlockedDecrement(&RefCount) == 0)
            delete this;
    }

    const RxSfnt&
    Sfnt(VOID) const
    {
        return Directory;
    }

    const RxFaceHeader&
    Header(VOID) const
    {
        return Fields;
    }

    DWRITE_FONT_FACE_TYPE
    FaceType(VOID) const
    {
        return Type;
    }

    UINT32
    Index(VOID) const
    {
        return FaceIndex;
    }

    IDWriteFontFile*
    File(VOID) const
    {
        return FontFile.Get();
    }

    /* Zero for a code point the font does not map */
    USHORT
    GlyphIndex(
        _In_ UINT32 CodePoint) const;

    BOOLEAN
    IsSymbolFont(VOID) const
    {
        return CmapIsSymbol;
    }

    /* Design units; the last advance repeats past numberOfHMetrics */
    VOID
    HorizontalMetrics(
        _In_ USHORT Glyph,
        _Out_ USHORT* Advance,
        _Out_ SHORT* LeftSideBearing) const;

    BOOLEAN
    VerticalMetrics(
        _In_ USHORT Glyph,
        _Out_ USHORT* Advance,
        _Out_ SHORT* TopSideBearing) const;

    /* The glyf entry of a glyph; empty for a glyph without contours */
    RxSpan
    GlyphData(
        _In_ USHORT Glyph) const;

    /* Expands composites; Depth guards against loops in broken fonts */
    HRESULT
    LoadOutline(
        _In_ USHORT Glyph,
        _Inout_ RxGlyphOutline& Outline) const;

    /* An entry of the name table in the given platform, encoding and language; NameId only when Language is MAXUINT32 */
    BOOLEAN
    FindName(
        _In_ USHORT NameId,
        _In_ USHORT Platform,
        _In_ USHORT Language,
        _Inout_ std::vector<WCHAR>& Text) const;

private:
    RxFaceTables() : RefCount(1), FaceIndex(0), Type(DWRITE_FONT_FACE_TYPE_UNKNOWN), Fields{}, CmapFormat(0),
                     CmapIsSymbol(FALSE) {}

    HRESULT
    Parse(VOID);

    VOID
    SelectCmap(VOID);

    HRESULT
    LoadOutlineAt(
        _In_ USHORT Glyph,
        _In_ ULONG Depth,
        _Inout_ RxGlyphOutline& Outline) const;

    LONG RefCount;
    RxComPtr<IDWriteFontFile> FontFile;
    RxFontFileData Data;
    RxSfnt Directory;
    UINT32 FaceIndex;
    DWRITE_FONT_FACE_TYPE Type;
    RxFaceHeader Fields;

    RxSpan Hmtx;
    RxSpan Vmtx;
    RxSpan Loca;
    RxSpan Glyf;
    RxSpan Name;

    RxSpan Cmap;
    USHORT CmapFormat;
    BOOLEAN CmapIsSymbol;
};
