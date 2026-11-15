/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font face objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "fontface.hpp"
#include "glyphraster.hpp"

/* Private interface our faces answer to, so internal code can find the tables behind one */
static constexpr GUID RxFontFaceIid = { 0x5a0f8b31, 0x7c2e, 0x4d6a, { 0x9e, 0x13, 0x48, 0xb2, 0x0c, 0x7f, 0xd1, 0x66 } };

BOOLEAN
RxGlyphBox(
    _In_ const RxFaceTables* Tables,
    _In_ USHORT Glyph,
    _Out_ SHORT* XMin,
    _Out_ SHORT* YMin,
    _Out_ SHORT* XMax,
    _Out_ SHORT* YMax)
{
    RxSpan Data = Tables->GlyphData(Glyph);

    *XMin = *YMin = *XMax = *YMax = 0;
    if (!Data.Has(0, 10) || Data.S16(0) == 0)
        return FALSE;

    *XMin = Data.S16(2);
    *YMin = Data.S16(4);
    *XMax = Data.S16(6);
    *YMax = Data.S16(8);

    return TRUE;
}

HRESULT
RxFontFace::Create(
    _In_ IDWriteFontFile* File,
    _In_ UINT32 FaceIndex,
    _In_ DWRITE_FONT_SIMULATIONS Simulations,
    _Outptr_ RxFontFace** Face)
{
    RxFontFace* Result;
    HRESULT hr;

    *Face = nullptr;

    Result = new (std::nothrow) RxFontFace();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    hr = RxFaceTables::Create(File, FaceIndex, &Result->Data);
    if (FAILED(hr))
    {
        Result->Release();
        return hr;
    }

    Result->Simulations = Simulations;
    Result->ComputeMetrics();
    *Face = Result;
    return S_OK;
}

RxFontFace::~RxFontFace()
{
    delete GlyphRasterizer;
    if (Data != nullptr)
        Data->Release();
}

static
BOOL
CALLBACK
RxCreateRasterizer(
    _Inout_ PINIT_ONCE InitOnce,
    _Inout_opt_ PVOID Parameter,
    _Outptr_opt_result_maybenull_ PVOID* Context)
{
    RxFaceTables* Tables = static_cast<RxFaceTables*>(Parameter);

    *Context = new (std::nothrow) RxGlyphRasterizer(Tables);
    return *Context != nullptr;
}

const RxKerning&
RxFontFace::Kerning(VOID)
{
    PVOID Context;
    BOOL Pending = FALSE;

    if (InitOnceBeginInitialize(&PairsOnce, 0, &Pending, &Context) && Pending)
    {
        Pairs.Load(Data, RxTag('l', 'a', 't', 'n'));
        InitOnceComplete(&PairsOnce, 0, nullptr);
    }
    return Pairs;
}

RxGlyphRasterizer*
RxFontFace::Rasterizer(VOID)
{
    PVOID Context = nullptr;

    if (!InitOnceExecuteOnce(&RasterizerOnce, RxCreateRasterizer, Data, &Context))
        return nullptr;
    GlyphRasterizer = static_cast<RxGlyphRasterizer*>(Context);
    return GlyphRasterizer;
}

RxFontFace*
RxFontFace::FromInterface(
    _In_opt_ IUnknown* Face)
{
    RxFontFace* Result = nullptr;

    if (Face == nullptr || FAILED(Face->QueryInterface(RxFontFaceIid, reinterpret_cast<void**>(&Result))))
        return nullptr;

    /* The caller keeps its own reference */
    Result->Release();
    return Result;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == RxFontFaceIid)
    {
        *ppvObject = this;
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFace) || riid == __uuidof(IDWriteFontFace1) ||
        riid == __uuidof(IDWriteFontFace2) || riid == __uuidof(IDWriteFontFace3) || riid == __uuidof(IDWriteFontFace4) ||
        riid == __uuidof(IDWriteFontFace5))
    {
        *ppvObject = static_cast<IDWriteFontFace5*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontFace)

/* Metrics */

/* Height of the glyph a character maps to, or zero */
static
SHORT
RxCharacterTop(
    _In_ const RxFaceTables* Tables,
    _In_ UINT32 Character)
{
    SHORT XMin;
    SHORT YMin;
    SHORT XMax;
    SHORT YMax;
    USHORT Glyph = Tables->GlyphIndex(Character);

    if (Glyph == 0 || !RxGlyphBox(Tables, Glyph, &XMin, &YMin, &XMax, &YMax))
        return 0;
    return YMax;
}

VOID
RxFontFace::ComputeMetrics(VOID)
{
    const RxFaceHeader& Header = Data->Header();
    DWRITE_FONT_METRICS1& Result = Metrics;

    Result = DWRITE_FONT_METRICS1{};
    Result.designUnitsPerEm = Header.UnitsPerEm;

    if (Header.HasOs2)
    {
        if (Header.FsSelection & RxFsSelectionUseTypoMetrics)
        {
            Result.ascent = static_cast<UINT16>(std::max<INT32>(0, Header.TypoAscender));
            Result.descent = static_cast<UINT16>(std::max<INT32>(0, -Header.TypoDescender));
            Result.lineGap = static_cast<INT16>(Header.TypoLineGap);
            Result.hasTypographicMetrics = TRUE;
        }
        else
        {
            INT32 Hhea = Header.HheaAscender - Header.HheaDescender + Header.HheaLineGap;
            INT32 Win = Header.WinAscent + Header.WinDescent;

            Result.ascent = Header.WinAscent;
            Result.descent = Header.WinDescent;
            Result.lineGap = static_cast<INT16>(std::max<INT32>(0, Hhea - Win));
        }
    }
    else
    {
        Result.ascent = static_cast<UINT16>(std::max<INT32>(0, Header.HheaAscender));
        Result.descent = static_cast<UINT16>(std::max<INT32>(0, -Header.HheaDescender));
        Result.lineGap = Header.HheaLineGap;
    }

    Result.capHeight = (Header.HasOs2 && Header.Os2Version >= 2) ? Header.CapHeight : 0;
    if (Result.capHeight == 0)
        Result.capHeight = RxCharacterTop(Data, L'H');
    Result.xHeight = (Header.HasOs2 && Header.Os2Version >= 2) ? Header.XHeight : 0;
    if (Result.xHeight == 0)
        Result.xHeight = RxCharacterTop(Data, L'x');

    Result.underlinePosition = Header.UnderlinePosition;
    Result.underlineThickness = Header.UnderlineThickness;
    Result.strikethroughPosition = Header.StrikeoutPosition;
    Result.strikethroughThickness = Header.StrikeoutSize;

    Result.glyphBoxLeft = Header.XMin;
    Result.glyphBoxTop = Header.YMax;
    Result.glyphBoxRight = Header.XMax;
    Result.glyphBoxBottom = Header.YMin;

    Result.subscriptPositionX = Header.SubscriptXOffset;
    Result.subscriptPositionY = static_cast<INT16>(-Header.SubscriptYOffset);
    Result.subscriptSizeX = Header.SubscriptXSize;
    Result.subscriptSizeY = Header.SubscriptYSize;
    Result.superscriptPositionX = Header.SuperscriptXOffset;
    Result.superscriptPositionY = Header.SuperscriptYOffset;
    Result.superscriptSizeX = Header.SuperscriptXSize;
    Result.superscriptSizeY = Header.SuperscriptYSize;
}

DWRITE_FONT_FACE_TYPE
STDMETHODCALLTYPE
RxFontFace::GetType(VOID)
{
    /* A face from a collection still reports what its outlines are */
    return Data->Sfnt().IsCff() ? DWRITE_FONT_FACE_TYPE_CFF : DWRITE_FONT_FACE_TYPE_TRUETYPE;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetFiles(
    _Inout_ UINT32* numberOfFiles,
    _Out_writes_opt_(*numberOfFiles) IDWriteFontFile** fontFiles)
{
    if (numberOfFiles == nullptr)
        return E_INVALIDARG;

    if (fontFiles == nullptr)
    {
        *numberOfFiles = 1;
        return S_OK;
    }
    if (*numberOfFiles < 1)
        return E_INVALIDARG;

    fontFiles[0] = Data->File();
    fontFiles[0]->AddRef();
    *numberOfFiles = 1;
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxFontFace::GetIndex(VOID)
{
    return Data->Index();
}

DWRITE_FONT_SIMULATIONS
STDMETHODCALLTYPE
RxFontFace::GetSimulations(VOID)
{
    return Simulations;
}

/* A symbol cmap or a pictorial PANOSE family */
BOOL
STDMETHODCALLTYPE
RxFontFace::IsSymbolFont(VOID)
{
    return Data->IsSymbolFont() || (Data->Header().HasOs2 && Data->Header().Panose[0] == 5);
}

VOID
STDMETHODCALLTYPE
RxFontFace::GetMetrics(
    _Out_ DWRITE_FONT_METRICS* fontFaceMetrics)
{
    memcpy(fontFaceMetrics, &Metrics, sizeof(*fontFaceMetrics));
}

VOID
STDMETHODCALLTYPE
RxFontFace::GetMetrics(
    _Out_ DWRITE_FONT_METRICS1* fontMetrics)
{
    *fontMetrics = Metrics;
}

UINT16
STDMETHODCALLTYPE
RxFontFace::GetGlyphCount(VOID)
{
    return Data->Header().GlyphCount;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetDesignGlyphMetrics(
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    UINT32 glyphCount,
    _Out_writes_(glyphCount) DWRITE_GLYPH_METRICS* glyphMetrics,
    BOOL isSideways)
{
    const RxFaceHeader& Header = Data->Header();
    /* Without vertical metrics the typographic ascent and descent frame the glyph */
    INT32 VerticalAscent = Header.HasOs2 ? Header.TypoAscender : Header.HheaAscender;
    INT32 VerticalHeight = VerticalAscent - (Header.HasOs2 ? Header.TypoDescender : Header.HheaDescender);

    if (glyphCount != 0 && (glyphIndices == nullptr || glyphMetrics == nullptr))
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < glyphCount; Index++)
    {
        DWRITE_GLYPH_METRICS& Out = glyphMetrics[Index];
        USHORT Glyph = glyphIndices[Index];
        USHORT Advance;
        SHORT Bearing;
        USHORT VerticalAdvance;
        SHORT TopBearing;
        SHORT XMin;
        SHORT YMin;
        SHORT XMax;
        SHORT YMax;
        BOOLEAN HasBox;

        if (Glyph >= Data->Header().GlyphCount)
            return E_INVALIDARG;

        Data->HorizontalMetrics(Glyph, &Advance, &Bearing);
        HasBox = RxGlyphBox(Data, Glyph, &XMin, &YMin, &XMax, &YMax);

        /* The bearing comes from hmtx; the box only gives the width */
        Out.advanceWidth = Advance;
        Out.leftSideBearing = HasBox ? Bearing : 0;
        Out.rightSideBearing = HasBox ? static_cast<INT32>(Advance) - Bearing - (XMax - XMin) : static_cast<INT32>(Advance);

        if (Data->VerticalMetrics(Glyph, &VerticalAdvance, &TopBearing))
        {
            Out.advanceHeight = VerticalAdvance;
            Out.topSideBearing = TopBearing;
            Out.verticalOriginY = TopBearing + (HasBox ? YMax : 0);
        }
        else
        {
            Out.advanceHeight = static_cast<UINT32>(VerticalHeight);
            Out.topSideBearing = HasBox ? VerticalAscent - YMax : VerticalAscent;
            Out.verticalOriginY = VerticalAscent;
        }
        Out.bottomSideBearing = HasBox ? static_cast<INT32>(Out.advanceHeight) - Out.topSideBearing - (YMax - YMin) :
                                         static_cast<INT32>(Out.advanceHeight) - Out.topSideBearing;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGlyphIndices(
    _In_reads_(codePointCount) UINT32 const* codePoints,
    UINT32 codePointCount,
    _Out_writes_(codePointCount) UINT16* glyphIndices)
{
    if (codePointCount != 0 && (codePoints == nullptr || glyphIndices == nullptr))
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < codePointCount; Index++)
        glyphIndices[Index] = Data->GlyphIndex(codePoints[Index]);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::TryGetFontTable(
    _In_ UINT32 openTypeTableTag,
    _Outptr_result_bytebuffer_(*tableSize) const void** tableData,
    _Out_ UINT32* tableSize,
    _Out_ void** tableContext,
    _Out_ BOOL* exists)
{
    RxSpan Table = Data->Sfnt().Table(RxDWriteTag(openTypeTableTag));

    *tableData = Table.IsEmpty() ? nullptr : Table.Bytes();
    *tableSize = Table.Length();
    *tableContext = nullptr;
    *exists = !Table.IsEmpty();
    return S_OK;
}

VOID
STDMETHODCALLTYPE
RxFontFace::ReleaseFontTable(
    _In_ void* tableContext)
{
}

VOID
STDMETHODCALLTYPE
RxFontFace::GetCaretMetrics(
    _Out_ DWRITE_CARET_METRICS* caretMetrics)
{
    const RxFaceHeader& Header = Data->Header();

    caretMetrics->slopeRise = Header.CaretSlopeRise;
    caretMetrics->slopeRun = Header.CaretSlopeRun;
    caretMetrics->offset = Header.CaretOffset;
}

BOOL
STDMETHODCALLTYPE
RxFontFace::IsMonospacedFont(VOID)
{
    return Data->Header().IsFixedPitch != 0;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetDesignGlyphAdvances(
    UINT32 glyphCount,
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    _Out_writes_(glyphCount) INT32* glyphAdvances,
    BOOL isSideways)
{
    std::vector<DWRITE_GLYPH_METRICS> Glyphs(glyphCount);
    HRESULT hr;

    hr = GetDesignGlyphMetrics(glyphIndices, glyphCount, Glyphs.data(), isSideways);
    if (FAILED(hr))
        return hr;

    for (UINT32 Index = 0; Index < glyphCount; Index++)
        glyphAdvances[Index] = static_cast<INT32>(isSideways ? Glyphs[Index].advanceHeight : Glyphs[Index].advanceWidth);
    return S_OK;
}

/* kern format 0 pairs, horizontal subtables only */
static
INT32
RxKernPair(
    _In_ const RxSpan& Kern,
    _In_ USHORT Left,
    _In_ USHORT Right)
{
    USHORT Tables = Kern.U16(2);
    UINT32 Offset = 4;
    INT32 Total = 0;
    UINT32 Key = (static_cast<UINT32>(Left) << 16) | Right;

    for (USHORT Table = 0; Table < Tables; Table++)
    {
        USHORT Length = Kern.U16(Offset + 2);
        USHORT Coverage = Kern.U16(Offset + 4);

        if ((Coverage >> 8) == 0 && (Coverage & 0x1) && !(Coverage & 0x4))
        {
            UINT32 Pairs = Kern.U16(Offset + 6);
            UINT32 Low = 0;
            UINT32 High = Pairs;

            while (Low < High)
            {
                UINT32 Middle = (Low + High) / 2;
                UINT32 Entry = Offset + 14 + Middle * 6;
                UINT32 Found = Kern.U32(Entry);

                if (Found == Key)
                {
                    INT32 Value = Kern.S16(Entry + 4);

                    if (Coverage & 0x8)
                        Total = Value;
                    else
                        Total += Value;
                    break;
                }
                if (Found < Key)
                    Low = Middle + 1;
                else
                    High = Middle;
            }
        }
        if (Length == 0)
            break;
        Offset += Length;
    }
    return Total;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetKerningPairAdjustments(
    UINT32 glyphCount,
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    _Out_writes_(glyphCount) INT32* glyphAdvanceAdjustments)
{
    RxSpan Kern = Data->Sfnt().Table(RxTag('k', 'e', 'r', 'n'));

    if (glyphCount == 0)
        return S_OK;
    if (glyphIndices == nullptr || glyphAdvanceAdjustments == nullptr)
        return E_INVALIDARG;

    for (UINT32 Index = 0; Index < glyphCount; Index++)
    {
        glyphAdvanceAdjustments[Index] = 0;
        if (Index + 1 < glyphCount && !Kern.IsEmpty())
            glyphAdvanceAdjustments[Index] = RxKernPair(Kern, glyphIndices[Index], glyphIndices[Index + 1]);
    }
    return S_OK;
}

BOOL
STDMETHODCALLTYPE
RxFontFace::HasKerningPairs(VOID)
{
    return Data->Sfnt().HasTable(RxTag('k', 'e', 'r', 'n'));
}

BOOL
STDMETHODCALLTYPE
RxFontFace::IsColorFont(VOID)
{
    return Data->Sfnt().HasTable(RxTag('C', 'O', 'L', 'R')) && Data->Sfnt().HasTable(RxTag('C', 'P', 'A', 'L'));
}

UINT32
STDMETHODCALLTYPE
RxFontFace::GetColorPaletteCount(VOID)
{
    return Data->Sfnt().Table(RxTag('C', 'P', 'A', 'L')).U16(4);
}

UINT32
STDMETHODCALLTYPE
RxFontFace::GetPaletteEntryCount(VOID)
{
    return Data->Sfnt().Table(RxTag('C', 'P', 'A', 'L')).U16(2);
}

VOID
STDMETHODCALLTYPE
RxFontFace::GetPanose(
    _Out_ DWRITE_PANOSE* panose)
{
    static_assert(sizeof(panose->values) == sizeof(Data->Header().Panose), "PANOSE is ten bytes");
    memcpy(panose->values, Data->Header().Panose, sizeof(panose->values));
}

DWRITE_FONT_WEIGHT
STDMETHODCALLTYPE
RxFontFace::GetWeight(VOID)
{
    USHORT Weight = Data->Header().HasOs2 ? Data->Header().WeightClass : 400;

    if (Weight < 1 || Weight > 999)
        Weight = 400;
    return static_cast<DWRITE_FONT_WEIGHT>(Weight);
}

DWRITE_FONT_STRETCH
STDMETHODCALLTYPE
RxFontFace::GetStretch(VOID)
{
    USHORT Width = Data->Header().HasOs2 ? Data->Header().WidthClass : 5;

    if (Width < 1 || Width > 9)
        Width = 5;
    return static_cast<DWRITE_FONT_STRETCH>(Width);
}

DWRITE_FONT_STYLE
STDMETHODCALLTYPE
RxFontFace::GetStyle(VOID)
{
    const RxFaceHeader& Header = Data->Header();

    if (Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE)
        return DWRITE_FONT_STYLE_OBLIQUE;
    if (Header.HasOs2)
    {
        if (Header.FsSelection & RxFsSelectionOblique)
            return DWRITE_FONT_STYLE_OBLIQUE;
        if (Header.FsSelection & RxFsSelectionItalic)
            return DWRITE_FONT_STYLE_ITALIC;
        return DWRITE_FONT_STYLE_NORMAL;
    }
    return (Header.MacStyle & RxMacStyleItalic) ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
}

BOOL
STDMETHODCALLTYPE
RxFontFace::HasCharacter(
    UINT32 unicodeValue)
{
    return Data->GlyphIndex(unicodeValue) != 0;
}

/* Every font here is on disk; nothing is downloaded */
BOOL
STDMETHODCALLTYPE
RxFontFace::IsCharacterLocal(
    UINT32 unicodeValue)
{
    return TRUE;
}

BOOL
STDMETHODCALLTYPE
RxFontFace::IsGlyphLocal(
    UINT16 glyphId)
{
    return TRUE;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::AreCharactersLocal(
    _In_reads_(characterCount) WCHAR const* characters,
    UINT32 characterCount,
    BOOL enqueueIfNotLocal,
    _Out_ BOOL* isLocal)
{
    *isLocal = TRUE;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::AreGlyphsLocal(
    _In_reads_(glyphCount) UINT16 const* glyphIndices,
    UINT32 glyphCount,
    BOOL enqueueIfNotLocal,
    _Out_ BOOL* isLocal)
{
    *isLocal = TRUE;
    return S_OK;
}

DWRITE_GLYPH_IMAGE_FORMATS
STDMETHODCALLTYPE
RxFontFace::GetGlyphImageFormats(VOID)
{
    const RxSfnt& Sfnt = Data->Sfnt();
    UINT32 Formats = DWRITE_GLYPH_IMAGE_FORMATS_NONE;

    if (Sfnt.HasTable(RxTag('g', 'l', 'y', 'f')))
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE;
    if (Sfnt.HasTable(RxTag('C', 'F', 'F', ' ')) || Sfnt.HasTable(RxTag('C', 'F', 'F', '2')))
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_CFF;
    if (IsColorFont())
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_COLR;
    if (Sfnt.HasTable(RxTag('S', 'V', 'G', ' ')))
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_SVG;
    if (Sfnt.HasTable(RxTag('s', 'b', 'i', 'x')))
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_PNG;
    if (Sfnt.HasTable(RxTag('C', 'B', 'D', 'T')) || Sfnt.HasTable(RxTag('E', 'B', 'D', 'T')))
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8;
    return static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(Formats);
}

HRESULT
STDMETHODCALLTYPE
RxFontFace::GetGlyphImageFormats(
    UINT16 glyphId,
    UINT32 pixelsPerEmFirst,
    UINT32 pixelsPerEmLast,
    _Out_ DWRITE_GLYPH_IMAGE_FORMATS* glyphImageFormats)
{
    UINT32 Formats = DWRITE_GLYPH_IMAGE_FORMATS_NONE;

    if (glyphId >= Data->Header().GlyphCount)
        return E_INVALIDARG;

    if (Data->Sfnt().IsCff())
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_CFF;
    else if (!Data->GlyphData(glyphId).IsEmpty())
        Formats |= DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE;
    *glyphImageFormats = static_cast<DWRITE_GLYPH_IMAGE_FORMATS>(Formats);
    return S_OK;
}

UINT32
STDMETHODCALLTYPE
RxFontFace::GetFontAxisValueCount(VOID)
{
    return Data->Sfnt().Table(RxTag('f', 'v', 'a', 'r')).U16(8);
}

BOOL
STDMETHODCALLTYPE
RxFontFace::HasVariations(VOID)
{
    return Data->Sfnt().HasTable(RxTag('f', 'v', 'a', 'r'));
}

BOOL
STDMETHODCALLTYPE
RxFontFace::Equals(
    IDWriteFontFace* fontFace)
{
    RxFontFace* Other = FromInterface(fontFace);
    RxFontFile* File;

    if (Other == nullptr)
        return FALSE;
    if (Other == this)
        return TRUE;
    if (Other->Data->Index() != Data->Index() || Other->Simulations != Simulations)
        return FALSE;

    File = static_cast<RxFontFile*>(Data->File());
    return File->Matches(Other->Data->File());
}
