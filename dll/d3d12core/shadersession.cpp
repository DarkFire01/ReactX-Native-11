/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader cache sessions and the stores behind them
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "shadercache.hpp"

/* Room the system cache keeps for its own tables; limits at or below it are refused */
constexpr UINT RxCacheReservedBytes = 36864;

constexpr UINT RxCacheDefaultMemory = 1024 * 1024;
constexpr UINT RxCacheDefaultEntries = 128;
constexpr UINT RxCacheDefaultFile = 1024 * 1024 * 1024;
constexpr UINT RxCacheLargestFile = 1024 * 1024 * 1024;

/* Overhead a memory store counts per value; keys count twice, once for the index */
constexpr UINT RxCacheEntryCost = 82;

/* Identifiers the runtime keeps for its own caches */
static const GUID RxCacheDriverId = {0x6f75932f, 0x7dfc, 0x4fb0, {0xb4, 0xb8, 0x12, 0xde, 0x1a, 0xc4, 0x15, 0xda}};
static const GUID RxCacheConversionId = {0xf4eb2d6c, 0xed2b, 0x4bdd, {0xad, 0x9d, 0xf9, 0x13, 0x28, 0x7e, 0x67, 0x68}};
static const GUID RxCacheSystemId = {0x52264c4c, 0x172f, 0x41b9, {0x91, 0xb8, 0x7f, 0x0c, 0x3b, 0x1e, 0x90, 0x21}};

/* The start of every cache file */
struct RxCacheFileHeader
{
    UINT32 Magic;
    UINT32 Format;
    GUID Identifier;
    UINT64 Version;
    UINT64 DriverVersion;
};

/* Each value is appended as one record: this, the key, then the value */
struct RxCacheRecord
{
    UINT32 Magic;
    UINT32 KeySize;
    UINT32 ValueSize;
    UINT32 Check;
};

constexpr UINT32 RxCacheFileMagic = 0x43535852;
constexpr UINT32 RxCacheRecordMagic = 0x56535852;
constexpr UINT32 RxCacheFileFormat = 1;

static SRWLOCK RxStoresLock = SRWLOCK_INIT;
static RxShaderCacheStore12* RxOpenStores;
static RxShaderCacheSession* RxOpenSessions;

static
UINT
RxCacheKeyHash(
    _In_reads_bytes_(KeySize) const BYTE* Key,
    _In_ UINT KeySize)
{
    UINT Hash = 2166136261u;

    for (UINT Index = 0; Index < KeySize; Index++)
        Hash = (Hash ^ Key[Index]) * 16777619u;
    return Hash;
}

/* The record check covers the sizes and the key, so a torn or foreign record is not loaded */
static
UINT32
RxCacheRecordCheck(
    _In_ UINT KeySize,
    _In_ UINT ValueSize,
    _In_reads_bytes_(KeySize) const BYTE* Key)
{
    return RxCacheKeyHash(Key, KeySize) ^ (KeySize * 0x9E3779B1u) ^ (ValueSize * 0x85EBCA77u);
}

/* Applications get their own folder, named by a CRC64 of the executable path */
static
UINT64
RxCachePathHash(
    _In_reads_bytes_(Size) const BYTE* Bytes,
    _In_ SIZE_T Size)
{
    UINT64 Crc = 0x012345678ABCDEF0ull;

    for (SIZE_T Index = 0; Index < Size; Index++)
    {
        Crc ^= Bytes[Index];
        for (UINT Bit = 0; Bit < 8; Bit++)
            Crc = (Crc & 1) != 0 ? (Crc >> 1) ^ 0x92C64265D32139A4ull : Crc >> 1;
    }
    return Crc;
}

static
BOOLEAN
RxCacheReadAt(
    _In_ HANDLE File,
    _In_ UINT64 Offset,
    _Out_writes_bytes_(Size) VOID* Buffer,
    _In_ UINT Size)
{
    OVERLAPPED At = {};
    DWORD Done = 0;

    At.Offset = (DWORD)Offset;
    At.OffsetHigh = (DWORD)(Offset >> 32);
    return ReadFile(File, Buffer, Size, &Done, &At) && Done == Size;
}

static
BOOLEAN
RxCacheWriteAt(
    _In_ HANDLE File,
    _In_ UINT64 Offset,
    _In_reads_bytes_(Size) const VOID* Buffer,
    _In_ UINT Size)
{
    OVERLAPPED At = {};
    DWORD Done = 0;

    At.Offset = (DWORD)Offset;
    At.OffsetHigh = (DWORD)(Offset >> 32);
    return WriteFile(File, Buffer, Size, &Done, &At) && Done == Size;
}

static
BOOLEAN
RxCacheTruncate(
    _In_ HANDLE File,
    _In_ UINT64 Size)
{
    LARGE_INTEGER End;

    End.QuadPart = (LONGLONG)Size;
    return SetFilePointerEx(File, End, nullptr, FILE_BEGIN) && SetEndOfFile(File);
}

/* Where a disk store lives: the working directory, or a folder of the application's own */
static
HRESULT
RxCacheFilePath(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc,
    _Out_writes_(MAX_PATH) WCHAR* Path)
{
    WCHAR Module[MAX_PATH];
    WCHAR Suffix[64] = L"";
    SIZE_T Length;
    DWORD Count;
    const GUID& Id = Desc.Identifier;

    if ((Desc.Flags & D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED) != 0)
    {
        D3DKMT_QUERY_DEVICE_IDS Ids = {};
        HRESULT hr;

        hr = Device.GetAdapter().QueryKernel(KMTQAITYPE_PHYSICALADAPTERDEVICEIDS, &Ids, sizeof(Ids));
        if (FAILED(hr))
            return hr;
        swprintf_s(Suffix, _countof(Suffix), L"_VEN_%04X&DEV_%04X&SUBSYS_%04X&REV_%02X", Ids.DeviceIds.VendorID,
                   Ids.DeviceIds.DeviceID, Ids.DeviceIds.SubSystemID, Ids.DeviceIds.RevisionID);
    }

    if ((Desc.Flags & D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR) != 0)
    {
        Count = GetCurrentDirectoryW(MAX_PATH, Path);
        if (Count == 0 || Count >= MAX_PATH)
            return E_FAIL;
    }
    else
    {
        Count = GetEnvironmentVariableW(L"LOCALAPPDATA", Path, MAX_PATH);
        if (Count == 0 || Count >= MAX_PATH || wcscat_s(Path, MAX_PATH, L"\\D3DSCache") != 0)
            return E_FAIL;
        if (!CreateDirectoryW(Path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return E_FAIL;

        Count = GetModuleFileNameW(NULL, Module, MAX_PATH);
        if (Count == 0 || Count >= MAX_PATH)
            return E_FAIL;
        Length = wcslen(Path);
        if (swprintf_s(Path + Length, MAX_PATH - Length, L"\\%I64x",
                       RxCachePathHash(reinterpret_cast<const BYTE*>(Module), Count * sizeof(WCHAR))) < 0)
        {
            return E_FAIL;
        }
        if (!CreateDirectoryW(Path, nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return E_FAIL;
    }

    Length = wcslen(Path);
    if (swprintf_s(Path + Length, MAX_PATH - Length, L"\\%08lX-%04hX-%04hX-%02X%02X-%02X%02X%02X%02X%02X%02X%s.dxcache",
                   Id.Data1, Id.Data2, Id.Data3, Id.Data4[0], Id.Data4[1], Id.Data4[2], Id.Data4[3], Id.Data4[4],
                   Id.Data4[5], Id.Data4[6], Id.Data4[7], Suffix) < 0)
    {
        return E_FAIL;
    }
    return S_OK;
}

/* Stores */

HRESULT
RxShaderCacheStore12::Open(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc,
    _Outptr_ RxShaderCacheStore12** Store)
{
    BOOLEAN Versioned = (Desc.Flags & D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED) != 0;
    LUID Adapter = {};
    RxShaderCacheStore12* Found;
    HRESULT hr;

    *Store = nullptr;
    if (Versioned)
        Adapter = Device.GetAdapter().GetLuid();

    AcquireSRWLockExclusive(&RxStoresLock);

    for (Found = RxOpenStores; Found != nullptr; Found = Found->NextOpen)
    {
        if (!Found->Matches(Desc.Identifier, Versioned, Adapter))
            continue;

        /* Joining takes the open store's mode and sizes, but a version cannot change under it */
        if (Found->Version != Desc.Version)
        {
            ReleaseSRWLockExclusive(&RxStoresLock);
            return DXGI_ERROR_ALREADY_EXISTS;
        }
        Found->Refs++;
        ReleaseSRWLockExclusive(&RxStoresLock);
        *Store = Found;
        return S_OK;
    }

    Found = new (std::nothrow) RxShaderCacheStore12();
    if (Found == nullptr)
    {
        ReleaseSRWLockExclusive(&RxStoresLock);
        return E_OUTOFMEMORY;
    }

    hr = Found->Initialize(Device, Desc);
    if (FAILED(hr))
    {
        ReleaseSRWLockExclusive(&RxStoresLock);
        delete Found;
        return hr;
    }

    Found->NextOpen = RxOpenStores;
    RxOpenStores = Found;
    ReleaseSRWLockExclusive(&RxStoresLock);

    *Store = Found;
    return S_OK;
}

VOID
RxShaderCacheStore12::AddRef()
{
    AcquireSRWLockExclusive(&RxStoresLock);
    Refs++;
    ReleaseSRWLockExclusive(&RxStoresLock);
}

/* The last reference closes the store, and deletes its file when a session asked for that */
VOID
RxShaderCacheStore12::Release()
{
    RxShaderCacheStore12** Link;
    BOOLEAN Last;

    AcquireSRWLockExclusive(&RxStoresLock);
    Refs -= 1;
    Last = Refs == 0;
    if (Last)
    {
        for (Link = &RxOpenStores; *Link != this; Link = &(*Link)->NextOpen)
            ;
        *Link = NextOpen;
    }
    ReleaseSRWLockExclusive(&RxStoresLock);

    if (Last)
        delete this;
}

RxShaderCacheStore12::~RxShaderCacheStore12()
{
    DropEntries();

    if (File != INVALID_HANDLE_VALUE)
    {
        CloseHandle(File);
        if (DeleteOnDestroy)
            DeleteFileW(Path);
    }
}

BOOLEAN
RxShaderCacheStore12::Matches(
    _In_ const GUID& Id,
    _In_ BOOLEAN Versioned,
    _In_ const LUID& Adapter) const
{
    return IsEqualGUID(Identifier, Id) && DriverVersioned == Versioned && AdapterLuid.LowPart == Adapter.LowPart &&
           AdapterLuid.HighPart == Adapter.HighPart;
}

HRESULT
RxShaderCacheStore12::Initialize(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc)
{
    BOOLEAN Existed;
    HRESULT hr;

    Identifier = Desc.Identifier;
    DriverVersioned = (Desc.Flags & D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED) != 0;
    Version = Desc.Version;
    Mode = Desc.Mode == D3D12_SHADER_CACHE_MODE_DISK ? D3D12_SHADER_CACHE_MODE_DISK : D3D12_SHADER_CACHE_MODE_MEMORY;
    MemoryLimit = Desc.MaximumInMemoryCacheSizeBytes != 0 ? Desc.MaximumInMemoryCacheSizeBytes : RxCacheDefaultMemory;
    EntryLimit = Desc.MaximumInMemoryCacheEntries != 0 ? Desc.MaximumInMemoryCacheEntries : RxCacheDefaultEntries;
    FileLimit = Desc.MaximumValueFileSizeBytes;

    if (DriverVersioned)
    {
        D3DKMT_UMD_DRIVER_VERSION Umd = {};

        AdapterLuid = Device.GetAdapter().GetLuid();
        if (SUCCEEDED(Device.GetAdapter().QueryKernel(KMTQAITYPE_UMD_DRIVER_VERSION, &Umd, sizeof(Umd))))
            DriverVersion = (UINT64)Umd.DriverVersion.QuadPart;
    }

    /* A memory store is as large as its in-memory limit */
    if (Mode == D3D12_SHADER_CACHE_MODE_MEMORY)
    {
        if (MemoryLimit <= RxCacheReservedBytes)
            return E_INVALIDARG;
        Capacity = MemoryLimit - RxCacheReservedBytes;
        return S_OK;
    }

    hr = RxCacheFilePath(Device, Desc, Path);
    if (FAILED(hr))
    {
        RX_TRACE(L"no place for shader cache file (0x%08lX)", hr);
        return hr;
    }

    /* The value file limit is checked when the file is made; an existing file keeps what it has */
    Existed = GetFileAttributesW(Path) != INVALID_FILE_ATTRIBUTES;
    if (!Existed && FileLimit != 0 && FileLimit <= RxCacheReservedBytes)
        return E_INVALIDARG;
    Capacity = (FileLimit > RxCacheReservedBytes ? FileLimit : RxCacheDefaultFile) - RxCacheReservedBytes;

    File = CreateFileW(Path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        RX_TRACE(L"shader cache file %ls could not be opened (0x%08lX)", Path, hr);
        return hr;
    }

    return LoadFile();
}

/* Reads what an earlier run stored; a file of another version, driver or format starts over */
HRESULT
RxShaderCacheStore12::LoadFile()
{
    RxCacheFileHeader Header = {};
    RxCacheFileHeader Expected = {};
    LARGE_INTEGER FileSize;
    UINT64 Offset;

    Expected.Magic = RxCacheFileMagic;
    Expected.Format = RxCacheFileFormat;
    Expected.Identifier = Identifier;
    Expected.Version = Version;
    Expected.DriverVersion = DriverVersion;

    if (!GetFileSizeEx(File, &FileSize))
        return HRESULT_FROM_WIN32(GetLastError());

    if ((UINT64)FileSize.QuadPart < sizeof(Header) || !RxCacheReadAt(File, 0, &Header, sizeof(Header)) ||
        memcmp(&Header, &Expected, sizeof(Header)) != 0)
    {
        if (!RxCacheTruncate(File, 0) || !RxCacheWriteAt(File, 0, &Expected, sizeof(Expected)))
            return HRESULT_FROM_WIN32(GetLastError());
        return S_OK;
    }

    Offset = sizeof(Header);
    while (Offset < (UINT64)FileSize.QuadPart)
    {
        RxCacheRecord Record;
        RxEntry* Entry;
        UINT64 End;

        if (!RxCacheReadAt(File, Offset, &Record, sizeof(Record)) || Record.Magic != RxCacheRecordMagic ||
            Record.KeySize == 0)
        {
            break;
        }
        End = Offset + sizeof(Record) + Record.KeySize + Record.ValueSize;
        if (End > (UINT64)FileSize.QuadPart)
            break;

        Entry = static_cast<RxEntry*>(malloc(FIELD_OFFSET(RxEntry, Data) + Record.KeySize));
        if (Entry == nullptr)
            return E_OUTOFMEMORY;
        if (!RxCacheReadAt(File, Offset + sizeof(Record), Entry->Data, Record.KeySize) ||
            RxCacheRecordCheck(Record.KeySize, Record.ValueSize, Entry->Data) != Record.Check)
        {
            free(Entry);
            break;
        }

        Entry->Hash = RxCacheKeyHash(Entry->Data, Record.KeySize);
        Entry->KeySize = Record.KeySize;
        Entry->ValueSize = Record.ValueSize;
        Entry->Offset = Offset + sizeof(Record) + Record.KeySize;

        /* Another process may have stored the same key; the first record wins */
        if (Lookup(Entry->Data, Entry->KeySize, Entry->Hash) != nullptr)
            free(Entry);
        else
            Insert(Entry);

        Used += sizeof(Record) + Record.KeySize + Record.ValueSize;
        Offset = End;
    }

    /* A record cut short by an earlier run is dropped */
    if (Offset < (UINT64)FileSize.QuadPart)
        RxCacheTruncate(File, Offset);
    return S_OK;
}

_Ret_maybenull_
RxShaderCacheStore12::RxEntry*
RxShaderCacheStore12::Lookup(
    _In_reads_bytes_(KeySize) const BYTE* Key,
    _In_ UINT KeySize,
    _In_ UINT Hash) const
{
    for (RxEntry* Entry = Buckets[Hash % BucketCount]; Entry != nullptr; Entry = Entry->Next)
    {
        if (Entry->Hash == Hash && Entry->KeySize == KeySize && memcmp(Entry->Data, Key, KeySize) == 0)
            return Entry;
    }
    return nullptr;
}

VOID
RxShaderCacheStore12::Insert(
    _In_ RxEntry* Entry)
{
    Entry->Next = Buckets[Entry->Hash % BucketCount];
    Buckets[Entry->Hash % BucketCount] = Entry;
}

VOID
RxShaderCacheStore12::DropEntries()
{
    for (RxEntry*& Head : Buckets)
    {
        while (Head != nullptr)
        {
            RxEntry* Next = Head->Next;

            free(Head);
            Head = Next;
        }
    }
    Used = 0;
}

HRESULT
RxShaderCacheStore12::Find(
    _In_reads_bytes_(KeySize) const VOID* Key,
    _In_ UINT KeySize,
    _Out_writes_bytes_opt_(*Size) VOID* Value,
    _Inout_ UINT* Size)
{
    const BYTE* KeyBytes = static_cast<const BYTE*>(Key);
    RxEntry* Entry;
    UINT Room = *Size;
    HRESULT hr = S_OK;

    AcquireSRWLockShared(&Lock);

    Entry = Lookup(KeyBytes, KeySize, RxCacheKeyHash(KeyBytes, KeySize));
    if (Entry == nullptr)
    {
        ReleaseSRWLockShared(&Lock);
        return DXGI_ERROR_NOT_FOUND;
    }

    *Size = Entry->ValueSize;
    if (Value != nullptr)
    {
        if (Entry->ValueSize > Room)
            hr = DXGI_ERROR_MORE_DATA;
        else if (File == INVALID_HANDLE_VALUE)
            CopyMemory(Value, Entry->Data + Entry->KeySize, Entry->ValueSize);
        else if (!RxCacheReadAt(File, Entry->Offset, Value, Entry->ValueSize))
            hr = DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }

    ReleaseSRWLockShared(&Lock);
    return hr;
}

HRESULT
RxShaderCacheStore12::Store(
    _In_reads_bytes_(KeySize) const VOID* Key,
    _In_ UINT KeySize,
    _In_reads_bytes_(FirstSize) const VOID* First,
    _In_ UINT FirstSize,
    _In_reads_bytes_opt_(SecondSize) const VOID* Second,
    _In_ UINT SecondSize)
{
    const BYTE* KeyBytes = static_cast<const BYTE*>(Key);
    UINT Hash = RxCacheKeyHash(KeyBytes, KeySize);
    UINT64 ValueSize = (UINT64)FirstSize + SecondSize;
    UINT64 Cost;
    RxEntry* Entry;

    if (ValueSize > UINT_MAX)
        return E_INVALIDARG;

    AcquireSRWLockExclusive(&Lock);

    if (Lookup(KeyBytes, KeySize, Hash) != nullptr)
    {
        ReleaseSRWLockExclusive(&Lock);
        return DXGI_ERROR_ALREADY_EXISTS;
    }

    /* Memory stores start over once full; file stores stop taking values */
    if (File == INVALID_HANDLE_VALUE)
    {
        Cost = 2ull * KeySize + ValueSize + RxCacheEntryCost;
        if (Cost > Capacity)
        {
            ReleaseSRWLockExclusive(&Lock);
            return DXGI_ERROR_CACHE_FULL;
        }
        if (Used + Cost > Capacity)
            DropEntries();

        Entry = static_cast<RxEntry*>(malloc(FIELD_OFFSET(RxEntry, Data) + KeySize + (SIZE_T)ValueSize));
        if (Entry == nullptr)
        {
            ReleaseSRWLockExclusive(&Lock);
            return E_OUTOFMEMORY;
        }
        CopyMemory(Entry->Data, Key, KeySize);
        CopyMemory(Entry->Data + KeySize, First, FirstSize);
        if (SecondSize != 0)
            CopyMemory(Entry->Data + KeySize + FirstSize, Second, SecondSize);
        Entry->Offset = 0;
    }
    else
    {
        RxCacheRecord* Record;
        OVERLAPPED Region = {};
        LARGE_INTEGER End;
        BOOLEAN Written;

        Cost = sizeof(*Record) + KeySize + ValueSize;
        if (Used + Cost > Capacity)
        {
            ReleaseSRWLockExclusive(&Lock);
            return DXGI_ERROR_CACHE_FULL;
        }

        Entry = static_cast<RxEntry*>(malloc(FIELD_OFFSET(RxEntry, Data) + KeySize));
        Record = static_cast<RxCacheRecord*>(malloc((SIZE_T)Cost));
        if (Entry == nullptr || Record == nullptr)
        {
            free(Entry);
            free(Record);
            ReleaseSRWLockExclusive(&Lock);
            return E_OUTOFMEMORY;
        }

        CopyMemory(Entry->Data, Key, KeySize);
        Record->Magic = RxCacheRecordMagic;
        Record->KeySize = KeySize;
        Record->ValueSize = (UINT)ValueSize;
        Record->Check = RxCacheRecordCheck(KeySize, (UINT)ValueSize, KeyBytes);
        CopyMemory(Record + 1, Key, KeySize);
        CopyMemory(reinterpret_cast<BYTE*>(Record + 1) + KeySize, First, FirstSize);
        if (SecondSize != 0)
            CopyMemory(reinterpret_cast<BYTE*>(Record + 1) + KeySize + FirstSize, Second, SecondSize);

        /* Other processes append to the same file, so the end is found under a lock past any data */
        Region.Offset = 0xFFFFFFFF;
        Region.OffsetHigh = 0x7FFFFFFF;
        LockFileEx(File, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &Region);
        Written = GetFileSizeEx(File, &End) && RxCacheWriteAt(File, (UINT64)End.QuadPart, Record, (UINT)Cost);
        UnlockFileEx(File, 0, 1, 0, &Region);
        free(Record);

        if (!Written)
        {
            free(Entry);
            ReleaseSRWLockExclusive(&Lock);
            return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
        }
        Entry->Offset = (UINT64)End.QuadPart + sizeof(*Record) + KeySize;
    }

    Entry->Hash = Hash;
    Entry->KeySize = KeySize;
    Entry->ValueSize = (UINT)ValueSize;
    Insert(Entry);
    Used += Cost;

    ReleaseSRWLockExclusive(&Lock);
    return S_OK;
}

VOID
RxShaderCacheStore12::Clear()
{
    AcquireSRWLockExclusive(&Lock);
    DropEntries();
    if (File != INVALID_HANDLE_VALUE)
        RxCacheTruncate(File, sizeof(RxCacheFileHeader));
    ReleaseSRWLockExclusive(&Lock);
}

HRESULT
RxOpenDriverShaderCache12(
    _In_ RxDevice12& Device,
    _Outptr_ RxShaderCacheStore12** Store)
{
    D3D12_SHADER_CACHE_SESSION_DESC Desc = {};
    HRESULT hr;

    Desc.Identifier = RxCacheDriverId;
    Desc.Mode = D3D12_SHADER_CACHE_MODE_DISK;
    Desc.Flags = D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED;

    hr = RxShaderCacheStore12::Open(Device, Desc, Store);
    if (FAILED(hr))
    {
        /* Without a file the driver still gets a cache for the process */
        RX_TRACE(L"driver shader cache stays in memory (0x%08lX)", hr);
        Desc.Mode = D3D12_SHADER_CACHE_MODE_MEMORY;
        hr = RxShaderCacheStore12::Open(Device, Desc, Store);
    }
    return hr;
}

/* Sessions */

HRESULT
RxShaderCacheSession::Create(
    _In_ RxDevice12& Device,
    _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc,
    _In_ UINT Kind,
    _Outptr_ RxShaderCacheSession** Session)
{
    RxShaderCacheStore12* Store;
    RxShaderCacheSession* Created;
    HRESULT hr;

    *Session = nullptr;

    hr = RxShaderCacheStore12::Open(Device, Desc, &Store);
    if (FAILED(hr))
        return hr;

    Created = new (std::nothrow) RxShaderCacheSession(Device);
    if (Created == nullptr)
    {
        Store->Release();
        return E_OUTOFMEMORY;
    }

    Created->Store = Store;
    Created->Desc = Desc;
    Created->Kind = Kind;

    AcquireSRWLockExclusive(&RxStoresLock);
    Created->NextSession = RxOpenSessions;
    if (RxOpenSessions != nullptr)
        RxOpenSessions->PreviousSession = Created;
    RxOpenSessions = Created;
    ReleaseSRWLockExclusive(&RxStoresLock);

    *Session = Created;
    return S_OK;
}

RxShaderCacheSession::~RxShaderCacheSession()
{
    if (Store == nullptr)
        return;

    AcquireSRWLockExclusive(&RxStoresLock);
    if (PreviousSession != nullptr)
        PreviousSession->NextSession = NextSession;
    else
        RxOpenSessions = NextSession;
    if (NextSession != nullptr)
        NextSession->PreviousSession = PreviousSession;
    ReleaseSRWLockExclusive(&RxStoresLock);

    Store->Release();
}

VOID
RxShaderCacheSession::ClearKinds(
    _In_ RxDevice12& Device,
    _In_ UINT Kinds)
{
    AcquireSRWLockShared(&RxStoresLock);
    for (RxShaderCacheSession* Session = RxOpenSessions; Session != nullptr; Session = Session->NextSession)
    {
        if (&Session->GetOwner() == &Device && (Session->Kind & Kinds) != 0)
            Session->Store->Clear();
    }
    ReleaseSRWLockShared(&RxStoresLock);
}

/* Sessions answer as device children, though not as plain objects, as on Windows */
HRESULT
STDMETHODCALLTYPE
RxShaderCacheSession::QueryInterface(
    _In_ REFIID riid,
    _COM_Outptr_ void** ppvObject)
{
    if (ppvObject == nullptr)
        return E_POINTER;

    if (riid == __uuidof(IUnknown))
    {
        *ppvObject = Identity();
        AddRef();
        return S_OK;
    }

    if (riid == __uuidof(ID3D12DeviceChild) || riid == __uuidof(ID3D12ShaderCacheSession) || riid == PrivateId)
    {
        *ppvObject = static_cast<ID3D12ShaderCacheSession*>(this);
        AddRef();
        return S_OK;
    }

    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

HRESULT
STDMETHODCALLTYPE
RxShaderCacheSession::FindValue(
    _In_reads_bytes_(KeySize) const void* pKey,
    UINT KeySize,
    _Out_writes_bytes_(*pValueSize) void* pValue,
    _Inout_ UINT* pValueSize)
{
    if (pKey == nullptr || pValueSize == nullptr || KeySize == 0)
        return E_INVALIDARG;
    if ((Kind & Device.GetDisabledShaderCaches()) != 0)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    return Store->Find(pKey, KeySize, pValue, pValueSize);
}

HRESULT
STDMETHODCALLTYPE
RxShaderCacheSession::StoreValue(
    _In_reads_bytes_(KeySize) const void* pKey,
    UINT KeySize,
    _In_reads_bytes_(ValueSize) const void* pValue,
    UINT ValueSize)
{
    if (pKey == nullptr || pValue == nullptr || KeySize == 0 || ValueSize == 0)
        return E_INVALIDARG;
    if ((Kind & Device.GetDisabledShaderCaches()) != 0)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    return Store->Store(pKey, KeySize, pValue, ValueSize, nullptr, 0);
}

/* The file goes when the last session on the store does */
VOID
STDMETHODCALLTYPE
RxShaderCacheSession::SetDeleteOnDestroy(VOID)
{
    Store->SetDeleteOnDestroy();
}

D3D12_SHADER_CACHE_SESSION_DESC
STDMETHODCALLTYPE
RxShaderCacheSession::GetDesc(VOID)
{
    D3D12_SHADER_CACHE_SESSION_DESC Result = Desc;

    Result.Mode = Store->GetMode();
    Result.MaximumInMemoryCacheSizeBytes = Store->GetMemoryLimit();
    Result.MaximumInMemoryCacheEntries = Store->GetEntryLimit();
    Result.MaximumValueFileSizeBytes = Store->GetFileLimit();
    return Result;
}

/* The device method */

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateShaderCacheSession(
    _In_ const D3D12_SHADER_CACHE_SESSION_DESC* pDesc,
    REFIID riid,
    _COM_Outptr_opt_ void** ppvSession)
{
    RxShaderCacheSession* Session;
    UINT Kind = RxCacheKindApplication12;
    HRESULT hr;

    /* Failed checks leave the output alone */
    if (pDesc == nullptr)
        return E_INVALIDARG;
    if ((UINT)pDesc->Mode != RxCacheModeSystem12 && IsEqualGUID(pDesc->Identifier, GUID_NULL))
        return E_INVALIDARG;
    if (IsEqualGUID(pDesc->Identifier, RxCacheDriverId) || IsEqualGUID(pDesc->Identifier, RxCacheConversionId) ||
        IsEqualGUID(pDesc->Identifier, RxCacheSystemId))
    {
        return E_INVALIDARG;
    }
    if (pDesc->MaximumValueFileSizeBytes > RxCacheLargestFile)
        return E_INVALIDARG;

    if ((UINT)pDesc->Mode == RxCacheModeSystem12)
    {
        if (!IsEqualGUID(pDesc->Identifier, GUID_NULL))
            return E_INVALIDARG;
        Kind = RxCacheKindSystem12;
    }
    else if (pDesc->Mode != D3D12_SHADER_CACHE_MODE_MEMORY && pDesc->Mode != D3D12_SHADER_CACHE_MODE_DISK)
    {
        return E_INVALIDARG;
    }
    if ((DisabledShaderCaches & Kind) != 0)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    if ((pDesc->Flags & ~(D3D12_SHADER_CACHE_FLAG_DRIVER_VERSIONED | D3D12_SHADER_CACHE_FLAG_USE_WORKING_DIR)) != 0)
        return E_INVALIDARG;
    if (ppvSession == nullptr)
        return S_FALSE;

    *ppvSession = nullptr;
    hr = RxShaderCacheSession::Create(*this, *pDesc, Kind, &Session);
    if (FAILED(hr))
        return hr;

    return RxHandOut12(Session, riid, ppvSession);
}
