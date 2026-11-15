/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font files, streams and the local and in memory loaders
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "sfnt.hpp"

/* A read only view of a file on disk */
class RxMappedStream final : public IDWriteFontFileStream
{
public:
    static
    HRESULT
    Open(
        _In_z_ PCWSTR Path,
        _Outptr_ RxMappedStream** Stream);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    ReadFileFragment(
        _Outptr_result_bytebuffer_(fragmentSize) void const** fragmentStart,
        UINT64 fileOffset,
        UINT64 fragmentSize,
        _Out_ void** fragmentContext) override;

    void
    STDMETHODCALLTYPE
    ReleaseFileFragment(
        void* fragmentContext) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFileSize(
        _Out_ UINT64* fileSize) override;

    HRESULT
    STDMETHODCALLTYPE
    GetLastWriteTime(
        _Out_ UINT64* lastWriteTime) override;

private:
    RxMappedStream() : RefCount(1), View(nullptr), Size(0), WriteTime(0) {}
    ~RxMappedStream();

    LONG RefCount;
    const VOID* View;
    UINT64 Size;
    UINT64 WriteTime;
};

/* The process wide loader behind CreateFontFileReference */
class RxLocalLoader final : public IDWriteLocalFontFileLoader
{
public:
    static
    RxLocalLoader*
    Instance(VOID);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    CreateStreamFromKey(
        _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        _COM_Outptr_ IDWriteFontFileStream** fontFileStream) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFilePathLengthFromKey(
        _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        _Out_ UINT32* filePathLength) override;

    HRESULT
    STDMETHODCALLTYPE
    GetFilePathFromKey(
        _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        _Out_writes_z_(filePathSize) WCHAR* filePath,
        UINT32 filePathSize) override;

    HRESULT
    STDMETHODCALLTYPE
    GetLastWriteTimeFromKey(
        _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
        UINT32 fontFileReferenceKeySize,
        _Out_ FILETIME* lastWriteTime) override;

    /* Builds the reference key for a path; WriteTime is read from the file when not given */
    static
    HRESULT
    MakeKey(
        _In_z_ PCWSTR Path,
        _In_opt_ const FILETIME* WriteTime,
        _Inout_ std::vector<BYTE>& Key);
};

/* Key bytes plus the loader that understands them */
class RxFontFile final : public IDWriteFontFile
{
public:
    static
    HRESULT
    Create(
        _In_ IDWriteFontFileLoader* Loader,
        _In_reads_bytes_(KeySize) const VOID* Key,
        _In_ UINT32 KeySize,
        _Outptr_ RxFontFile** File);

    RX_DECLARE_IUNKNOWN

    HRESULT
    STDMETHODCALLTYPE
    GetReferenceKey(
        _Outptr_result_bytebuffer_(*fontFileReferenceKeySize) void const** fontFileReferenceKey,
        _Out_ UINT32* fontFileReferenceKeySize) override;

    HRESULT
    STDMETHODCALLTYPE
    GetLoader(
        _COM_Outptr_ IDWriteFontFileLoader** fontFileLoader) override;

    HRESULT
    STDMETHODCALLTYPE
    Analyze(
        _Out_ BOOL* isSupportedFontType,
        _Out_ DWRITE_FONT_FILE_TYPE* fontFileType,
        _Out_opt_ DWRITE_FONT_FACE_TYPE* fontFaceType,
        _Out_ UINT32* numberOfFaces) override;

    /* Same loader and same key bytes */
    BOOLEAN
    Matches(
        _In_ IDWriteFontFile* Other);

private:
    RxFontFile() : RefCount(1) {}

    LONG RefCount;
    RxComPtr<IDWriteFontFileLoader> Loader;
    std::vector<BYTE> Key;
};

/* The whole file of a stream mapped in one fragment, released with the object */
class RxFontFileData
{
public:
    RxFontFileData() : Fragment(nullptr), Context(nullptr), Size(0) {}
    ~RxFontFileData();

    HRESULT
    Load(
        _In_ IDWriteFontFile* File);

    RxSpan
    Span(VOID) const
    {
        return RxSpan(Fragment, static_cast<UINT32>(Size));
    }

private:
    RxComPtr<IDWriteFontFileStream> Stream;
    const VOID* Fragment;
    VOID* Context;
    UINT64 Size;
};

/* %windir%\Fonts\ with the trailing separator */
PCWSTR
RxFontsDirectory(VOID);
