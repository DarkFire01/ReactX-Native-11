/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Font files, streams and the local loader
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "fontfile.hpp"

/* Keys start with the write time; a path that begins with this marks one under the Fonts folder */
constexpr WCHAR RxFontsDirectoryMarker = L'*';

PCWSTR
RxFontsDirectory(VOID)
{
    static WCHAR Directory[MAX_PATH];
    static LONG Ready;

    if (InterlockedCompareExchange(&Ready, 0, 0) == 0)
    {
        WCHAR Path[MAX_PATH];
        UINT Length = GetWindowsDirectoryW(Path, _countof(Path));

        if (Length == 0 || Length >= _countof(Path) - 8)
            wcscpy_s(Path, L"C:\\Windows");
        if (Path[wcslen(Path) - 1] != L'\\')
            wcscat_s(Path, L"\\");
        wcscat_s(Path, L"Fonts\\");
        wcscpy_s(Directory, Path);
        InterlockedExchange(&Ready, 1);
    }
    return Directory;
}

/* RxMappedStream */

HRESULT
RxMappedStream::Open(
    _In_z_ PCWSTR Path,
    _Outptr_ RxMappedStream** Stream)
{
    RxMappedStream* Result;
    HANDLE File;
    HANDLE Mapping;
    LARGE_INTEGER Size;
    FILETIME WriteTime;
    HRESULT hr = S_OK;

    *Stream = nullptr;

    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (File == INVALID_HANDLE_VALUE)
    {
        DWORD Error = GetLastError();

        if (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND)
            return DWRITE_E_FILENOTFOUND;
        return DWRITE_E_FILEACCESS;
    }

    Result = new (std::nothrow) RxMappedStream();
    if (Result == nullptr)
    {
        CloseHandle(File);
        return E_OUTOFMEMORY;
    }

    if (!GetFileSizeEx(File, &Size) || !GetFileTime(File, nullptr, nullptr, &WriteTime))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
    }
    else if (Size.QuadPart != 0)
    {
        Mapping = CreateFileMappingW(File, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (Mapping == nullptr)
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
        }
        else
        {
            Result->View = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
            if (Result->View == nullptr)
                hr = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(Mapping);
        }
    }

    CloseHandle(File);
    if (FAILED(hr))
    {
        Result->Release();
        return hr;
    }

    Result->Size = static_cast<UINT64>(Size.QuadPart);
    Result->WriteTime = (static_cast<UINT64>(WriteTime.dwHighDateTime) << 32) | WriteTime.dwLowDateTime;
    *Stream = Result;
    return S_OK;
}

RxMappedStream::~RxMappedStream()
{
    if (View != nullptr)
        UnmapViewOfFile(View);
}

HRESULT
STDMETHODCALLTYPE
RxMappedStream::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileStream))
    {
        *ppvObject = static_cast<IDWriteFontFileStream*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxMappedStream)

HRESULT
STDMETHODCALLTYPE
RxMappedStream::ReadFileFragment(
    _Outptr_result_bytebuffer_(fragmentSize) void const** fragmentStart,
    UINT64 fileOffset,
    UINT64 fragmentSize,
    _Out_ void** fragmentContext)
{
    *fragmentStart = nullptr;
    *fragmentContext = nullptr;

    if (fileOffset + fragmentSize < fileOffset || fileOffset + fragmentSize > Size)
        return E_FAIL;

    *fragmentStart = static_cast<const BYTE*>(View) + fileOffset;
    return S_OK;
}

void
STDMETHODCALLTYPE
RxMappedStream::ReleaseFileFragment(
    void* fragmentContext)
{
}

HRESULT
STDMETHODCALLTYPE
RxMappedStream::GetFileSize(
    _Out_ UINT64* fileSize)
{
    *fileSize = Size;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxMappedStream::GetLastWriteTime(
    _Out_ UINT64* lastWriteTime)
{
    *lastWriteTime = WriteTime;
    return S_OK;
}

/* RxLocalLoader */

/* Splits a key into its write time and path; the path comes back with the Fonts folder restored */
static
HRESULT
RxReadLocalKey(
    _In_reads_bytes_(KeySize) const VOID* Key,
    _In_ UINT32 KeySize,
    _Out_opt_ UINT64* WriteTime,
    _Out_writes_opt_(PathSize) PWSTR Path,
    _In_ UINT32 PathSize,
    _Out_opt_ UINT32* PathLength)
{
    const BYTE* Bytes = static_cast<const BYTE*>(Key);
    PCWSTR Name;
    UINT32 Chars;
    UINT32 Length;
    UINT32 Prefix = 0;
    PCWSTR Directory = RxFontsDirectory();

    if (Key == nullptr || KeySize <= sizeof(UINT64) || ((KeySize - sizeof(UINT64)) & 1) != 0)
        return DWRITE_E_CACHEFORMAT;

    Name = reinterpret_cast<PCWSTR>(Bytes + sizeof(UINT64));
    Chars = (KeySize - sizeof(UINT64)) / sizeof(WCHAR);
    Length = static_cast<UINT32>(wcsnlen(Name, Chars));
    if (Length >= Chars)
        return DWRITE_E_CACHEFORMAT;

    if (WriteTime != nullptr)
        memcpy(WriteTime, Bytes, sizeof(*WriteTime));

    if (Length != 0 && Name[0] == RxFontsDirectoryMarker)
    {
        Name++;
        Length--;
        Prefix = static_cast<UINT32>(wcslen(Directory));
    }

    if (PathLength != nullptr)
        *PathLength = Prefix + Length;

    if (Path != nullptr)
    {
        if (PathSize < Prefix + Length + 1)
        {
            if (PathSize != 0)
                Path[0] = UNICODE_NULL;
            return E_NOT_SUFFICIENT_BUFFER;
        }
        memcpy(Path, Directory, Prefix * sizeof(WCHAR));
        memcpy(Path + Prefix, Name, Length * sizeof(WCHAR));
        Path[Prefix + Length] = UNICODE_NULL;
    }
    return S_OK;
}

RxLocalLoader*
RxLocalLoader::Instance(VOID)
{
    static RxLocalLoader Loader;

    return &Loader;
}

HRESULT
STDMETHODCALLTYPE
RxLocalLoader::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFileLoader) ||
        riid == __uuidof(IDWriteLocalFontFileLoader))
    {
        *ppvObject = static_cast<IDWriteLocalFontFileLoader*>(this);
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

/* Lives for the life of the process */
ULONG
STDMETHODCALLTYPE
RxLocalLoader::AddRef(VOID)
{
    return 2;
}

ULONG
STDMETHODCALLTYPE
RxLocalLoader::Release(VOID)
{
    return 1;
}

HRESULT
STDMETHODCALLTYPE
RxLocalLoader::CreateStreamFromKey(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _COM_Outptr_ IDWriteFontFileStream** fontFileStream)
{
    std::vector<WCHAR> Path;
    RxMappedStream* Stream;
    UINT32 Length;
    HRESULT hr;

    if (fontFileStream == nullptr)
        return E_INVALIDARG;
    *fontFileStream = nullptr;

    hr = RxReadLocalKey(fontFileReferenceKey, fontFileReferenceKeySize, nullptr, nullptr, 0, &Length);
    if (FAILED(hr))
        return hr;

    Path.resize(Length + 1);
    hr = RxReadLocalKey(fontFileReferenceKey, fontFileReferenceKeySize, nullptr, Path.data(), Length + 1, nullptr);
    if (FAILED(hr))
        return hr;

    hr = RxMappedStream::Open(Path.data(), &Stream);
    if (FAILED(hr))
        return hr;

    *fontFileStream = Stream;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxLocalLoader::GetFilePathLengthFromKey(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _Out_ UINT32* filePathLength)
{
    *filePathLength = MAXUINT32;
    return RxReadLocalKey(fontFileReferenceKey, fontFileReferenceKeySize, nullptr, nullptr, 0, filePathLength);
}

HRESULT
STDMETHODCALLTYPE
RxLocalLoader::GetFilePathFromKey(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _Out_writes_z_(filePathSize) WCHAR* filePath,
    UINT32 filePathSize)
{
    if (filePath == nullptr)
        return E_INVALIDARG;
    return RxReadLocalKey(fontFileReferenceKey, fontFileReferenceKeySize, nullptr, filePath, filePathSize, nullptr);
}

HRESULT
STDMETHODCALLTYPE
RxLocalLoader::GetLastWriteTimeFromKey(
    _In_reads_bytes_(fontFileReferenceKeySize) void const* fontFileReferenceKey,
    UINT32 fontFileReferenceKeySize,
    _Out_ FILETIME* lastWriteTime)
{
    UINT64 Ticks = 0;
    HRESULT hr;

    hr = RxReadLocalKey(fontFileReferenceKey, fontFileReferenceKeySize, &Ticks, nullptr, 0, nullptr);
    lastWriteTime->dwLowDateTime = static_cast<DWORD>(Ticks);
    lastWriteTime->dwHighDateTime = static_cast<DWORD>(Ticks >> 32);
    return hr;
}

HRESULT
RxLocalLoader::MakeKey(
    _In_z_ PCWSTR Path,
    _In_opt_ const FILETIME* WriteTime,
    _Inout_ std::vector<BYTE>& Key)
{
    PCWSTR Directory = RxFontsDirectory();
    SIZE_T DirectoryLength = wcslen(Directory);
    WCHAR Full[MAX_PATH];
    PCWSTR Name = Full;
    BOOLEAN UnderFonts = FALSE;
    UINT64 Ticks;
    SIZE_T Length;
    DWORD FullLength;

    FullLength = GetFullPathNameW(Path, _countof(Full), Full, nullptr);
    if (FullLength == 0 || FullLength >= _countof(Full))
        return E_INVALIDARG;

    if (WriteTime != nullptr)
    {
        Ticks = (static_cast<UINT64>(WriteTime->dwHighDateTime) << 32) | WriteTime->dwLowDateTime;
    }
    else
    {
        WIN32_FILE_ATTRIBUTE_DATA Data;

        if (!GetFileAttributesExW(Full, GetFileExInfoStandard, &Data))
        {
            DWORD Error = GetLastError();

            return (Error == ERROR_FILE_NOT_FOUND || Error == ERROR_PATH_NOT_FOUND) ? DWRITE_E_FILENOTFOUND :
                                                                                     DWRITE_E_FILEACCESS;
        }
        Ticks = (static_cast<UINT64>(Data.ftLastWriteTime.dwHighDateTime) << 32) | Data.ftLastWriteTime.dwLowDateTime;
    }

    if (_wcsnicmp(Full, Directory, DirectoryLength) == 0)
    {
        Name = Full + DirectoryLength;
        UnderFonts = TRUE;
    }

    Length = wcslen(Name);
    Key.clear();
    Key.resize(sizeof(Ticks) + (Length + 1 + (UnderFonts ? 1 : 0)) * sizeof(WCHAR));
    memcpy(Key.data(), &Ticks, sizeof(Ticks));

    PWSTR Out = reinterpret_cast<PWSTR>(Key.data() + sizeof(Ticks));
    if (UnderFonts)
        *Out++ = RxFontsDirectoryMarker;
    memcpy(Out, Name, (Length + 1) * sizeof(WCHAR));
    return S_OK;
}

/* RxFontFile */

HRESULT
RxFontFile::Create(
    _In_ IDWriteFontFileLoader* Loader,
    _In_reads_bytes_(KeySize) const VOID* Key,
    _In_ UINT32 KeySize,
    _Outptr_ RxFontFile** File)
{
    RxFontFile* Result;

    *File = nullptr;

    Result = new (std::nothrow) RxFontFile();
    if (Result == nullptr)
        return E_OUTOFMEMORY;

    Result->Loader = RxComPtr<IDWriteFontFileLoader>(Loader);
    Result->Key.assign(static_cast<const BYTE*>(Key), static_cast<const BYTE*>(Key) + KeySize);
    *File = Result;
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFile::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteFontFile))
    {
        *ppvObject = static_cast<IDWriteFontFile*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

RX_IMPLEMENT_REFCOUNT(RxFontFile)

HRESULT
STDMETHODCALLTYPE
RxFontFile::GetReferenceKey(
    _Outptr_result_bytebuffer_(*fontFileReferenceKeySize) void const** fontFileReferenceKey,
    _Out_ UINT32* fontFileReferenceKeySize)
{
    *fontFileReferenceKey = Key.data();
    *fontFileReferenceKeySize = static_cast<UINT32>(Key.size());
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFile::GetLoader(
    _COM_Outptr_ IDWriteFontFileLoader** fontFileLoader)
{
    if (fontFileLoader == nullptr)
        return E_INVALIDARG;

    *fontFileLoader = Loader.Copy();
    return S_OK;
}

HRESULT
STDMETHODCALLTYPE
RxFontFile::Analyze(
    _Out_ BOOL* isSupportedFontType,
    _Out_ DWRITE_FONT_FILE_TYPE* fontFileType,
    _Out_opt_ DWRITE_FONT_FACE_TYPE* fontFaceType,
    _Out_ UINT32* numberOfFaces)
{
    RxFontFileData Data;
    DWRITE_FONT_FACE_TYPE FaceType;
    HRESULT hr;

    *isSupportedFontType = FALSE;
    *fontFileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    *numberOfFaces = 0;
    if (fontFaceType != nullptr)
        *fontFaceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;

    hr = Data.Load(this);
    if (FAILED(hr))
        return hr;

    RxAnalyzeFontFile(Data.Span(), isSupportedFontType, fontFileType, &FaceType, numberOfFaces);
    if (fontFaceType != nullptr)
        *fontFaceType = FaceType;
    return S_OK;
}

BOOLEAN
RxFontFile::Matches(
    _In_ IDWriteFontFile* Other)
{
    RxComPtr<IDWriteFontFileLoader> OtherLoader;
    const VOID* OtherKey;
    UINT32 OtherSize;

    if (Other == this)
        return TRUE;
    if (FAILED(Other->GetLoader(OtherLoader.Put())) || OtherLoader.Get() != Loader.Get())
        return FALSE;
    if (FAILED(Other->GetReferenceKey(&OtherKey, &OtherSize)) || OtherSize != Key.size())
        return FALSE;
    return memcmp(OtherKey, Key.data(), OtherSize) == 0;
}

/* RxFontFileData */

HRESULT
RxFontFileData::Load(
    _In_ IDWriteFontFile* File)
{
    RxComPtr<IDWriteFontFileLoader> Loader;
    const VOID* Key;
    UINT32 KeySize;
    HRESULT hr;

    hr = File->GetLoader(Loader.Put());
    if (FAILED(hr))
        return hr;

    hr = File->GetReferenceKey(&Key, &KeySize);
    if (FAILED(hr))
        return hr;

    hr = Loader->CreateStreamFromKey(Key, KeySize, Stream.Put());
    if (FAILED(hr))
        return hr;

    hr = Stream->GetFileSize(&Size);
    if (FAILED(hr))
        return hr;

    /* Tables address the file with 32 bit offsets */
    if (Size > MAXUINT32)
        return DWRITE_E_FILEFORMAT;
    if (Size == 0)
        return S_OK;

    return Stream->ReadFileFragment(&Fragment, 0, Size, &Context);
}

RxFontFileData::~RxFontFileData()
{
    if (Fragment != nullptr)
        Stream->ReleaseFileFragment(Context);
}
