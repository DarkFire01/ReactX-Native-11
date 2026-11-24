/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     GDI interop and the bitmap render target
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "factory.hpp"
#include "gdiinterop.hpp"

#include <cmath>

/* What GDI reports about the font selected into a DC */
struct RxRealization
{
    DWORD Size;
    DWORD Flags;
    DWORD CacheNumber;
    DWORD InstanceId;
    DWORD FileCount;
    WORD FaceIndex;
    WORD Simulations;
};

struct RxGdiFileInfo
{
    FILETIME WriteTime;
    LARGE_INTEGER FileSize;
    WCHAR Path[MAX_PATH];
};

typedef BOOL (WINAPI *RxRealizationProc)(HDC, RxRealization*);
typedef BOOL (WINAPI *RxFileInfoProc)(DWORD, DWORD, RxGdiFileInfo*, SIZE_T, SIZE_T*);

/* Realized from a font file, as a scalable outline */
static constexpr DWORD RxRealizedOutline = 3;

static const WCHAR RxEmptyText[] = L"";

/* ulCodePageRange1 bits a LOGFONT charset asks for */
static
UINT32
RxCharsetMask(
    _In_ BYTE Charset)
{
    static const struct
    {
        BYTE Charset;
        UINT32 Mask;
    } Table[] =
    {
        { ANSI_CHARSET, 0x00000001 },
        { DEFAULT_CHARSET, 0xFFFFFFFF },
        { SYMBOL_CHARSET, 0x80000000 },
        { MAC_CHARSET, 0x20000000 },
        { SHIFTJIS_CHARSET, 0x00020000 },
        { HANGUL_CHARSET, 0x00080000 },
        { JOHAB_CHARSET, 0x00200000 },
        { GB2312_CHARSET, 0x00040000 },
        { CHINESEBIG5_CHARSET, 0x00100000 },
        { GREEK_CHARSET, 0x00000008 },
        { TURKISH_CHARSET, 0x00000010 },
        { VIETNAMESE_CHARSET, 0x00000100 },
        { HEBREW_CHARSET, 0x00000020 },
        { ARABIC_CHARSET, 0x00000040 },
        { BALTIC_CHARSET, 0x00000080 },
        { RUSSIAN_CHARSET, 0x00000004 },
        { EASTEUROPE_CHARSET, 0x00000002 },
        { THAI_CHARSET, 0x00010000 },
        { OEM_CHARSET, 0x40000000 },
    };

    for (const auto& Entry : Table)
    {
        if (Entry.Charset == Charset)
            return Entry.Mask;
    }
    return 0;
}

/* RxGdiInterop */

RxGdiInterop::RxGdiInterop(
    _In_ RxWriteFactory* Owner) : Factory(Owner)
{
    WCHAR Name[LOCALE_NAME_MAX_LENGTH];

    if (LCIDToLocaleName(GetSystemDefaultLCID(), Name, LOCALE_NAME_MAX_LENGTH, 0) > 0)
    {
        Locale = Name;
        for (auto& Char : Locale)
        {
            if (Char >= L'A' && Char <= L'Z')
                Char += L'a' - L'A';
        }
    }
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteGdiInterop) || riid == __uuidof(IDWriteGdiInterop1))
    {
        *ppvObject = static_cast<IDWriteGdiInterop1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG
STDMETHODCALLTYPE
RxGdiInterop::AddRef(VOID)
{
    return Factory->AddRef();
}

ULONG
STDMETHODCALLTYPE
RxGdiInterop::Release(VOID)
{
    return Factory->Release();
}

const std::wstring&
RxGdiInterop::PickName(
    _In_ const RxNameList& Names) const
{
    for (const auto& Entry : Names)
    {
        if (Entry.Locale == Locale)
            return Entry.Text;
    }
    return RxIdentityName(Names);
}

VOID
RxGdiInterop::FillLogFont(
    _In_ const RxFaceDescription& Description,
    _In_ DWRITE_FONT_SIMULATIONS Simulations,
    _In_ BOOLEAN IsSymbol,
    _Out_ LOGFONTW* LogFont) const
{
    PCWSTR Name = RxEmptyText;
    SIZE_T Length = 0;

    if (!Description.Win32Family.empty())
    {
        const std::wstring& Picked = PickName(Description.Win32Family);

        Name = Picked.c_str();
        Length = std::min<SIZE_T>(Picked.size(), LF_FACESIZE - 1);
    }
    memcpy(LogFont->lfFaceName, Name, Length * sizeof(WCHAR));
    LogFont->lfFaceName[Length] = UNICODE_NULL;

    /* Bold simulation reads as 151 heavier, which GDI turns back into emboldening */
    LogFont->lfWeight = Description.Win32Weight + ((Simulations & DWRITE_FONT_SIMULATIONS_BOLD) ? 151 : 0);
    LogFont->lfItalic = ((Simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) || Description.Win32Style != 0) ? TRUE : FALSE;
    LogFont->lfCharSet = IsSymbol ? SYMBOL_CHARSET : DEFAULT_CHARSET;
    LogFont->lfOutPrecision = OUT_OUTLINE_PRECIS;
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::CreateFontFromLOGFONT(
    _In_ LOGFONTW const* logFont,
    _COM_Outptr_ IDWriteFont** font)
{
    return CreateFontFromLOGFONT(logFont, nullptr, font);
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::CreateFontFromLOGFONT(
    _In_ LOGFONTW const* logFont,
    _In_opt_ IDWriteFontCollection* fontCollection,
    _COM_Outptr_ IDWriteFont** font)
{
    RxComPtr<RxFontCollection> Collection;
    const RxFontSetData* Data;
    std::vector<UINT32> Candidates;
    UINT32 Mask;
    UINT32 Best = MAXUINT32;
    UINT32 BestEntry = 0;
    DWRITE_FONT_SIMULATIONS BestSimulations = DWRITE_FONT_SIMULATIONS_NONE;
    UINT32 Family;
    RxFont* Result;
    HRESULT hr;

    if (font == nullptr)
        return E_INVALIDARG;
    *font = nullptr;
    if (logFont == nullptr)
        return E_INVALIDARG;

    if (fontCollection == nullptr)
    {
        RxFontCollection* Installed;

        hr = Factory->SystemCollection(&Installed);
        if (FAILED(hr))
            return hr;
        Collection.Attach(Installed);
    }
    else
    {
        RxFontCollection* Ours = RxFontCollection::FromInterface(fontCollection);

        if (Ours == nullptr)
            return E_INVALIDARG;
        Collection = RxComPtr<RxFontCollection>(Ours);
    }

    Data = Collection->Data();
    RxWin32Candidates(Data, logFont->lfFaceName, wcsnlen(logFont->lfFaceName, LF_FACESIZE), Candidates);
    Mask = RxCharsetMask(logFont->lfCharSet);

    for (UINT32 Index : Candidates)
    {
        DWRITE_FONT_SIMULATIONS Simulations;
        UINT32 Penalty = RxWin32Penalty(Data->Entries[Index], Mask, logFont->lfWeight, logFont->lfItalic != 0,
                                        &Simulations);

        if (Penalty < Best)
        {
            Best = Penalty;
            BestEntry = Index;
            BestSimulations = Simulations;
        }
    }
    if (Best == MAXUINT32)
        return DWRITE_E_NOFONT;

    Family = Data->FamilyOfEntry(BestEntry, Collection->Model());
    if (Family == MAXUINT32)
        return DWRITE_E_NOFONT;

    hr = RxFont::Create(Collection.Get(), Family, { BestEntry, BestSimulations }, &Result);
    if (FAILED(hr))
        return hr;
    *font = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::ConvertFontToLOGFONT(
    _In_ IDWriteFont* font,
    _Out_ LOGFONTW* logFont,
    _Out_ BOOL* isSystemFont)
{
    RxFont* Ours;

    if (logFont == nullptr || isSystemFont == nullptr)
        return E_INVALIDARG;
    memset(logFont, 0, sizeof(*logFont));
    *isSystemFont = FALSE;
    Ours = RxFont::FromInterface(font);
    if (Ours == nullptr)
        return E_INVALIDARG;

    FillLogFont(Ours->Entry().Description, Ours->GetSimulations(), Ours->Entry().IsSymbol, logFont);
    *isSystemFont = Ours->Owner()->Data()->IsSystem;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::ConvertFontFaceToLOGFONT(
    _In_ IDWriteFontFace* font,
    _Out_ LOGFONTW* logFont)
{
    RxFontFace* Face;
    RxFaceDescription Description;

    if (logFont == nullptr)
        return E_INVALIDARG;
    memset(logFont, 0, sizeof(*logFont));
    if (font == nullptr)
        return E_INVALIDARG;
    if (font->GetType() == DWRITE_FONT_FACE_TYPE_RAW_CFF)
        return DWRITE_E_UNSUPPORTEDOPERATION;

    Face = RxFontFace::FromInterface(font);
    if (Face == nullptr)
        return E_INVALIDARG;

    RxDescribeFace(Face->Tables(), &Description);
    FillLogFont(Description, Face->GetSimulations(), Face->IsSymbolFont() != FALSE, logFont);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::CreateFontFaceFromHdc(
    HDC hdc,
    _COM_Outptr_ IDWriteFontFace** fontFace)
{
    static RxRealizationProc Realize;
    static RxFileInfoProc FileInfo;
    RxRealization Info = {};
    RxGdiFileInfo File = {};
    SIZE_T Needed = 0;
    RxComPtr<IDWriteFontFile> FontFile;
    BOOL Supported;
    DWRITE_FONT_FILE_TYPE FileType;
    DWRITE_FONT_FACE_TYPE FaceType;
    UINT32 Faces;
    HRESULT hr;

    if (fontFace == nullptr)
        return E_INVALIDARG;
    *fontFace = nullptr;
    if (hdc == nullptr)
        return E_INVALIDARG;

    if (Realize == nullptr || FileInfo == nullptr)
    {
        HMODULE Gdi = GetModuleHandleW(L"gdi32.dll");

        if (Gdi == nullptr)
            return E_FAIL;
        FileInfo = reinterpret_cast<RxFileInfoProc>(GetProcAddress(Gdi, "GetFontFileInfo"));
        Realize = reinterpret_cast<RxRealizationProc>(GetProcAddress(Gdi, "GetFontRealizationInfo"));
        if (Realize == nullptr || FileInfo == nullptr)
            return E_FAIL;
    }

    Info.Size = sizeof(Info);
    SetLastError(ERROR_SUCCESS);
    if (!Realize(hdc, &Info))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        RX_TRACE(L"CreateFontFaceFromHdc: no realization info, 0x%08lX", hr);
        return FAILED(hr) ? hr : E_FAIL;
    }
    if ((Info.Flags & RxRealizedOutline) != RxRealizedOutline)
        return DWRITE_E_FILEFORMAT;

    if (!FileInfo(Info.InstanceId, 0, &File, sizeof(File), &Needed) || File.Path[0] == UNICODE_NULL)
    {
        RX_TRACE(L"CreateFontFaceFromHdc: font %lu has no file", Info.InstanceId);
        return DWRITE_E_FILENOTFOUND;
    }

    hr = Factory->CreateFontFileReference(File.Path, &File.WriteTime, FontFile.Put());
    if (FAILED(hr))
        return hr;
    hr = FontFile->Analyze(&Supported, &FileType, &FaceType, &Faces);
    if (FAILED(hr))
        return hr;
    if (!Supported)
        return DWRITE_E_FILEFORMAT;

    IDWriteFontFile* Files[] = { FontFile.Get() };
    return Factory->CreateFontFace(FaceType, 1, Files, Info.FaceIndex,
                                   static_cast<DWRITE_FONT_SIMULATIONS>(Info.Simulations & 3), fontFace);
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::CreateBitmapRenderTarget(
    _In_opt_ HDC hdc,
    UINT32 width,
    UINT32 height,
    _COM_Outptr_ IDWriteBitmapRenderTarget** renderTarget)
{
    RxBitmapTarget* Target;
    HRESULT hr;

    if (renderTarget == nullptr)
        return E_INVALIDARG;
    *renderTarget = nullptr;

    hr = RxBitmapTarget::Create(hdc, width, height, &Target);
    if (FAILED(hr))
        return hr;
    *renderTarget = Target;
    return S_OK;
}

static
HRESULT
RxSignatureOf(
    _In_opt_ const FONTSIGNATURE* Source,
    _Out_ FONTSIGNATURE* fontSignature)
{
    if (fontSignature == nullptr)
        return E_INVALIDARG;
    memset(fontSignature, 0, sizeof(*fontSignature));
    if (Source == nullptr)
        return E_INVALIDARG;
    *fontSignature = *Source;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::GetFontSignature(
    _In_ IDWriteFontFace* fontFace,
    _Out_ FONTSIGNATURE* fontSignature)
{
    RxFontFace* Face = RxFontFace::FromInterface(fontFace);
    FONTSIGNATURE Signature = {};

    if (Face == nullptr)
        return RxSignatureOf(nullptr, fontSignature);

    if (Face->Tables()->Header().HasOs2)
    {
        memcpy(Signature.fsUsb, Face->Tables()->Header().UnicodeRange, sizeof(Signature.fsUsb));
        memcpy(Signature.fsCsb, Face->Tables()->Header().CodePageRange, sizeof(Signature.fsCsb));
    }
    return RxSignatureOf(&Signature, fontSignature);
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::GetFontSignature(
    _In_ IDWriteFont* font,
    _Out_ FONTSIGNATURE* fontSignature)
{
    RxFont* Ours = RxFont::FromInterface(font);

    return RxSignatureOf(Ours != nullptr ? &Ours->Entry().Signature : nullptr, fontSignature);
}

HRESULT
STDMETHODCALLTYPE
RxGdiInterop::GetMatchingFontsByLOGFONT(
    _In_ LOGFONT const* logFont,
    _In_ IDWriteFontSet* fontSet,
    _COM_Outptr_ IDWriteFontSet** filteredSet)
{
    if (filteredSet != nullptr)
        *filteredSet = nullptr;

    RX_UNIMPLEMENTED();
    return E_NOTIMPL;
}

/* RxBitmapTarget */

HRESULT
RxBitmapTarget::MakeSection(
    _In_ UINT32 NewWidth,
    _In_ UINT32 NewHeight,
    _Out_ HBITMAP* Section,
    _Out_ UINT32** SectionBits)
{
    BITMAPINFO Info = {};
    VOID* Memory = nullptr;

    *Section = nullptr;
    *SectionBits = nullptr;
    if (NewWidth == 0 || NewHeight == 0)
        return S_OK;
    if (NewWidth > 0x7FFFFFFF || NewHeight > 0x7FFFFFFF)
        return E_INVALIDARG;

    Info.bmiHeader.biSize = sizeof(Info.bmiHeader);
    Info.bmiHeader.biWidth = static_cast<LONG>(NewWidth);
    Info.bmiHeader.biHeight = -static_cast<LONG>(NewHeight);
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    *Section = CreateDIBSection(Dc, &Info, DIB_RGB_COLORS, &Memory, nullptr, 0);
    if (*Section == nullptr)
    {
        RX_TRACE(L"bitmap render target: no %ux%u section (%lu)", NewWidth, NewHeight, GetLastError());
        return E_OUTOFMEMORY;
    }
    *SectionBits = static_cast<UINT32*>(Memory);
    return S_OK;
}

HRESULT
RxBitmapTarget::Create(
    _In_opt_ HDC Reference,
    _In_ UINT32 Width,
    _In_ UINT32 Height,
    _Outptr_ RxBitmapTarget** Target)
{
    RxBitmapTarget* Result;
    HRESULT hr;

    *Target = nullptr;
    Result = new (std::nothrow) RxBitmapTarget();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->Dc = CreateCompatibleDC(Reference);
    if (Result->Dc == nullptr)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        Result->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = Result->MakeSection(Width, Height, &Result->Bitmap, &Result->Bits);
    if (FAILED(hr))
    {
        Result->Release();
        return hr;
    }
    Result->Width = Width;
    Result->Height = Height;

    Result->PixelsPerDip = GetDeviceCaps(Result->Dc, LOGPIXELSY) / 96.0f;
    SetGraphicsMode(Result->Dc, GM_ADVANCED);
    if (Result->Bitmap != nullptr)
        Result->Original = SelectObject(Result->Dc, Result->Bitmap);
    else
        Result->Original = GetCurrentObject(Result->Dc, OBJ_BITMAP);

    *Target = Result;
    return S_OK;
}

RxBitmapTarget::~RxBitmapTarget()
{
    if (Dc != nullptr)
    {
        if (Original != nullptr)
            SelectObject(Dc, Original);
        DeleteDC(Dc);
    }
    if (Bitmap != nullptr)
        DeleteObject(Bitmap);
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteBitmapRenderTarget) ||
        riid == __uuidof(IDWriteBitmapRenderTarget1))
    {
        *ppvObject = static_cast<IDWriteBitmapRenderTarget1*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxBitmapTarget)

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::DrawGlyphRun(
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode,
    _In_ DWRITE_GLYPH_RUN const* glyphRun,
    IDWriteRenderingParams* renderingParams,
    COLORREF textColor,
    _Out_opt_ RECT* blackBoxRect)
{
    RxComPtr<IDWriteFontFace3> Face;
    DWRITE_RENDERING_MODE1 Mode;
    DWRITE_GRID_FIT_MODE GridFit;
    RxParamValues Values;
    RxGlyphRunAnalysis* Analysis;
    RECT Drawn;
    HRESULT hr;

    if (Bits == nullptr)
        return S_OK;
    if (blackBoxRect != nullptr)
        SetRectEmpty(blackBoxRect);
    if (glyphRun == nullptr || renderingParams == nullptr || glyphRun->fontFace == nullptr ||
        (glyphRun->glyphIndices == nullptr && glyphRun->glyphCount != 0) || glyphRun->fontEmSize < 0.0f)
        return E_INVALIDARG;

    hr = glyphRun->fontFace->QueryInterface(__uuidof(IDWriteFontFace3), reinterpret_cast<void**>(Face.Put()));
    if (FAILED(hr))
        return E_INVALIDARG;

    hr = Face->GetRecommendedRenderingMode(glyphRun->fontEmSize, PixelsPerDip * 96.0f, PixelsPerDip * 96.0f,
                                           HasTransform ? &Transform : nullptr, glyphRun->isSideways,
                                           DWRITE_OUTLINE_THRESHOLD_ALIASED, measuringMode, renderingParams, &Mode,
                                           &GridFit);
    if (FAILED(hr))
        return hr;

    /* Outlines go through a GDI path fill, which is aliased coverage */
    if (Mode == DWRITE_RENDERING_MODE1_OUTLINE)
        Mode = DWRITE_RENDERING_MODE1_ALIASED;

    hr = RxGlyphRunAnalysis::Create(glyphRun, PixelsPerDip, HasTransform ? &Transform : nullptr, Mode, measuringMode,
                                    GridFit, Antialias, TRUE, baselineOriginX, baselineOriginY, &Analysis);
    if (FAILED(hr))
        return hr;

    RxRenderingParams::ReadValues(renderingParams, &Values);
    Analysis->DrawInto(Values, textColor, Bits, Width, Height, &Drawn);
    Analysis->Release();

    if (blackBoxRect != nullptr)
        *blackBoxRect = Drawn;
    return S_OK;
}

HDC
STDMETHODCALLTYPE
RxBitmapTarget::GetMemoryDC(VOID)
{
    return Dc;
}

FLOAT
STDMETHODCALLTYPE
RxBitmapTarget::GetPixelsPerDip(VOID)
{
    return PixelsPerDip;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::SetPixelsPerDip(
    FLOAT pixelsPerDip)
{
    if (!(pixelsPerDip > 0.0f))
        return E_INVALIDARG;
    PixelsPerDip = pixelsPerDip;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::GetCurrentTransform(
    _Out_ DWRITE_MATRIX* transform)
{
    if (transform == nullptr)
        return E_INVALIDARG;
    *transform = Transform;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::SetCurrentTransform(
    _In_opt_ DWRITE_MATRIX const* transform)
{
    static const DWRITE_MATRIX Identity = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };

    if (transform != nullptr)
    {
        const FLOAT* Values = &transform->m11;

        for (UINT32 Index = 0; Index < 6; Index++)
        {
            if (!std::isfinite(Values[Index]))
                return E_INVALIDARG;
        }
    }

    if (transform == nullptr || memcmp(transform, &Identity, sizeof(Identity)) == 0)
    {
        Transform = Identity;
        HasTransform = FALSE;
    }
    else
    {
        Transform = *transform;
        HasTransform = TRUE;
    }
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::GetSize(
    _Out_ SIZE* size)
{
    if (size == nullptr)
        return E_INVALIDARG;
    size->cx = static_cast<LONG>(Width);
    size->cy = static_cast<LONG>(Height);
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::Resize(
    UINT32 width,
    UINT32 height)
{
    HBITMAP Section;
    UINT32* SectionBits;
    HRESULT hr;

    if (width == Width && height == Height)
        return S_OK;

    hr = MakeSection(width, height, &Section, &SectionBits);
    if (FAILED(hr))
        return hr;

    /* The old pixels survive where the sizes overlap */
    if (Section != nullptr && Bits != nullptr)
    {
        UINT32 Rows = std::min(Height, height);
        UINT32 Columns = std::min(Width, width);

        for (UINT32 Row = 0; Row < Rows; Row++)
            memcpy(&SectionBits[static_cast<SIZE_T>(Row) * width], &Bits[static_cast<SIZE_T>(Row) * Width],
                   Columns * sizeof(UINT32));
    }

    SelectObject(Dc, Section != nullptr ? static_cast<HGDIOBJ>(Section) : Original);
    if (Bitmap != nullptr)
        DeleteObject(Bitmap);
    Bitmap = Section;
    Bits = SectionBits;
    Width = width;
    Height = height;
    return S_OK;
}

DWRITE_TEXT_ANTIALIAS_MODE
STDMETHODCALLTYPE
RxBitmapTarget::GetTextAntialiasMode(VOID)
{
    return Antialias;
}

HRESULT
STDMETHODCALLTYPE
RxBitmapTarget::SetTextAntialiasMode(
    DWRITE_TEXT_ANTIALIAS_MODE antialiasMode)
{
    if (static_cast<UINT32>(antialiasMode) > DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        return E_INVALIDARG;
    Antialias = antialiasMode;
    return S_OK;
}
