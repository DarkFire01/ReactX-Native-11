/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shader cache stores, sessions and the cache the runtime keeps for drivers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Session kinds, as ShaderCacheControl names them */
constexpr UINT RxCacheKindDriver12 = D3D12_SHADER_CACHE_KIND_FLAG_IMPLICIT_D3D_CACHE_FOR_DRIVER;
constexpr UINT RxCacheKindApplication12 = D3D12_SHADER_CACHE_KIND_FLAG_APPLICATION_MANAGED;
constexpr UINT RxCacheKindSystem12 = 0x10;

/* The system managed mode newer headers name, which takes no identifier */
constexpr UINT RxCacheModeSystem12 = 2;

/* Keyed values every session opened on one identifier shares, in memory or in a file */
class RxShaderCacheStore12
{
public:
    /* Opens the store the desc names, or joins the one already open in the process */
    static
    HRESULT
    Open(
        _In_ RxDevice12& Device,
        _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc,
        _Outptr_ RxShaderCacheStore12** Store);

    VOID
    AddRef();

    VOID
    Release();

    /* *Size goes in as the room in Value and comes out as the stored size */
    HRESULT
    Find(
        _In_reads_bytes_(KeySize) const VOID* Key,
        _In_ UINT KeySize,
        _Out_writes_bytes_opt_(*Size) VOID* Value,
        _Inout_ UINT* Size);

    /* Values are written once; a key already stored is refused */
    HRESULT
    Store(
        _In_reads_bytes_(KeySize) const VOID* Key,
        _In_ UINT KeySize,
        _In_reads_bytes_(FirstSize) const VOID* First,
        _In_ UINT FirstSize,
        _In_reads_bytes_opt_(SecondSize) const VOID* Second,
        _In_ UINT SecondSize);

    VOID
    Clear();

    VOID
    SetDeleteOnDestroy()
    {
        DeleteOnDestroy = TRUE;
    }

    /* Mode and sizes in effect; identifier, flags and version come from each session */
    D3D12_SHADER_CACHE_MODE
    GetMode() const
    {
        return Mode;
    }

    UINT
    GetMemoryLimit() const
    {
        return MemoryLimit;
    }

    UINT
    GetEntryLimit() const
    {
        return EntryLimit;
    }

    UINT
    GetFileLimit() const
    {
        return FileLimit;
    }

private:
    struct RxEntry
    {
        RxEntry* Next;
        UINT Hash;
        UINT KeySize;
        UINT ValueSize;
        /* Where a file store keeps the value */
        UINT64 Offset;
        /* The key, then for memory stores the value */
        BYTE Data[1];
    };

    static constexpr UINT BucketCount = 1024;

    RxShaderCacheStore12() = default;
    ~RxShaderCacheStore12();

    HRESULT
    Initialize(
        _In_ RxDevice12& Device,
        _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc);

    HRESULT
    LoadFile();

    BOOLEAN
    Matches(
        _In_ const GUID& Id,
        _In_ BOOLEAN Versioned,
        _In_ const LUID& Adapter) const;

    _Ret_maybenull_
    RxEntry*
    Lookup(
        _In_reads_bytes_(KeySize) const BYTE* Key,
        _In_ UINT KeySize,
        _In_ UINT Hash) const;

    VOID
    Insert(
        _In_ RxEntry* Entry);

    VOID
    DropEntries();

    LONG Refs = 1;
    RxShaderCacheStore12* NextOpen = nullptr;
    SRWLOCK Lock = SRWLOCK_INIT;
    GUID Identifier = {};
    BOOLEAN DriverVersioned = FALSE;
    LUID AdapterLuid = {};
    UINT64 Version = 0;
    UINT64 DriverVersion = 0;
    D3D12_SHADER_CACHE_MODE Mode = D3D12_SHADER_CACHE_MODE_MEMORY;
    UINT MemoryLimit = 0;
    UINT EntryLimit = 0;
    UINT FileLimit = 0;
    UINT64 Capacity = 0;
    UINT64 Used = 0;
    BOOLEAN DeleteOnDestroy = FALSE;
    HANDLE File = INVALID_HANDLE_VALUE;
    WCHAR Path[MAX_PATH] = {};
    RxEntry* Buckets[BucketCount] = {};
};

/* An application's view of a store */
class RxShaderCacheSession final : public RxDeviceChild12<ID3D12ShaderCacheSession>
{
public:
    static constexpr GUID PrivateId = {0x7e1c5a42, 0x93d8, 0x4f06, {0xb2, 0x5d, 0x18, 0xa7, 0x6c, 0xe0, 0x41, 0x9b}};

    /* The desc is checked */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_SHADER_CACHE_SESSION_DESC& Desc,
        _In_ UINT Kind,
        _Outptr_ RxShaderCacheSession** Session);

    /* Clears the stores of this device's sessions of the given kinds */
    static
    VOID
    ClearKinds(
        _In_ RxDevice12& Device,
        _In_ UINT Kinds);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    FindValue(
        _In_reads_bytes_(KeySize) const void* pKey,
        UINT KeySize,
        _Out_writes_bytes_(*pValueSize) void* pValue,
        _Inout_ UINT* pValueSize) override;

    HRESULT
    STDMETHODCALLTYPE
    StoreValue(
        _In_reads_bytes_(KeySize) const void* pKey,
        UINT KeySize,
        _In_reads_bytes_(ValueSize) const void* pValue,
        UINT ValueSize) override;

    VOID
    STDMETHODCALLTYPE
    SetDeleteOnDestroy(VOID) override;

    D3D12_SHADER_CACHE_SESSION_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override;

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxShaderCacheSession() override;

    RxShaderCacheSession* NextSession = nullptr;
    RxShaderCacheSession* PreviousSession = nullptr;
    RxShaderCacheStore12* Store = nullptr;
    D3D12_SHADER_CACHE_SESSION_DESC Desc = {};
    UINT Kind = 0;
};

/* The store a device keeps for its driver, kept in a file per application and driver when it can be */
HRESULT
RxOpenDriverShaderCache12(
    _In_ RxDevice12& Device,
    _Outptr_ RxShaderCacheStore12** Store);

/* Turns on the driver's shader caching feature and hands it the runtime's store */
HRESULT
RxEnableShaderCache12(
    _In_ const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096& Functions,
    _In_ D3D12DDI_HDEVICE Device);
