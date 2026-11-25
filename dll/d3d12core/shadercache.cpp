/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The shader cache the runtime keeps for drivers, held in memory for the process
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "shadercache.hpp"

/* What a driver stored under one key: compiled code and its compiler's metadata */
struct RxCacheEntry
{
    RxCacheEntry* NextInBucket;
    RxCacheEntry* NextStored;
    LUID Adapter;
    UINT Hash;
    SIZE_T KeySize;
    SIZE_T CodeSize;
    SIZE_T MetadataSize;
    /* The key, then the code, then the metadata */
    BYTE* Data;
};

constexpr UINT RxCacheBucketCount = 512;

/* Oldest values go first once the cache holds this much */
constexpr SIZE_T RxCacheLimit = 512 * 1024 * 1024;

static SRWLOCK RxCacheLock = SRWLOCK_INIT;
static RxCacheEntry* RxCacheBuckets[RxCacheBucketCount];
static RxCacheEntry* RxCacheOldest;
static RxCacheEntry* RxCacheNewest;
static SIZE_T RxCacheBytes;

/* Values from one adapter's driver are never handed to another's */
static
UINT
RxCacheHash(
    _In_ const LUID& Adapter,
    _In_reads_bytes_(KeySize) const BYTE* Key,
    _In_ SIZE_T KeySize)
{
    UINT Hash = 2166136261u ^ Adapter.LowPart ^ ((UINT)Adapter.HighPart << 16);

    for (SIZE_T Index = 0; Index < KeySize; Index++)
        Hash = (Hash ^ Key[Index]) * 16777619u;
    return Hash;
}

/* The lock is held */
_Ret_maybenull_
static
RxCacheEntry*
RxCacheFind(
    _In_ const LUID& Adapter,
    _In_ UINT Hash,
    _In_reads_bytes_(KeySize) const BYTE* Key,
    _In_ SIZE_T KeySize)
{
    for (RxCacheEntry* Entry = RxCacheBuckets[Hash % RxCacheBucketCount]; Entry != nullptr; Entry = Entry->NextInBucket)
    {
        if (Entry->Hash == Hash && Entry->KeySize == KeySize && Entry->Adapter.LowPart == Adapter.LowPart &&
            Entry->Adapter.HighPart == Adapter.HighPart && memcmp(Entry->Data, Key, KeySize) == 0)
        {
            return Entry;
        }
    }
    return nullptr;
}

/* Unlinks and frees one entry; the lock is held exclusively */
static
VOID
RxCacheDrop(
    _In_ RxCacheEntry* Entry)
{
    RxCacheEntry** Link;

    for (Link = &RxCacheBuckets[Entry->Hash % RxCacheBucketCount]; *Link != Entry; Link = &(*Link)->NextInBucket)
        ;
    *Link = Entry->NextInBucket;

    if (RxCacheOldest == Entry)
    {
        RxCacheOldest = Entry->NextStored;
        if (RxCacheNewest == Entry)
            RxCacheNewest = nullptr;
    }
    else
    {
        RxCacheEntry* Before = RxCacheOldest;

        while (Before->NextStored != Entry)
            Before = Before->NextStored;
        Before->NextStored = Entry->NextStored;
        if (RxCacheNewest == Entry)
            RxCacheNewest = Before;
    }

    RxCacheBytes -= Entry->KeySize + Entry->CodeSize + Entry->MetadataSize;
    free(Entry->Data);
    free(Entry);
}

/* Hands one stored value out through the driver's allocator, its buffer, or as a size alone */
static
HRESULT
RxCacheCopyOut(
    _Inout_opt_ D3D12DDI_SHADER_CACHE_VALUE* Value,
    _In_reads_bytes_(Size) const BYTE* Stored,
    _In_ SIZE_T Size,
    _In_opt_ PFND3D12DDI_SHADERCACHEALLOCATIONFUNC Allocate,
    _Inout_opt_ VOID* Context)
{
    if (Value == nullptr)
        return S_OK;

    if (Allocate != nullptr)
    {
        VOID* Memory = Allocate(Size, Context);

        if (Memory == nullptr && Size != 0)
            return E_OUTOFMEMORY;
        if (Size != 0)
            CopyMemory(Memory, Stored, Size);
        Value->pValue = Memory;
        Value->ValueSize = Size;
        return S_OK;
    }

    if (Value->pValue != nullptr)
    {
        if (Value->ValueSize < Size)
        {
            Value->ValueSize = Size;
            return E_INVALIDARG;
        }
        CopyMemory(Value->pValue, Stored, Size);
    }
    Value->ValueSize = Size;
    return S_OK;
}

static
HRESULT
APIENTRY
RxShaderCacheGetValueCb(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HANDLE_AND_TYPE hRTObject,
    _In_reads_bytes_(KeyLen) const void* pKey,
    _In_ SIZE_T KeyLen,
    _In_ D3D12DDI_SHADER_CACHE_GET_VALUE_FLAGS Flags,
    _Inout_opt_ D3D12DDI_SHADER_CACHE_VALUE* pObjectCode,
    _Inout_opt_ D3D12DDI_SHADER_CACHE_VALUE* pCompilerMetadata,
    _In_opt_ PFND3D12DDI_SHADERCACHEALLOCATIONFUNC pCallbackFunc,
    _Inout_opt_ void* pContext)
{
    const LUID& Adapter = static_cast<RxDevice12*>(hRTDevice.handle)->GetAdapter().GetLuid();
    const BYTE* Key = static_cast<const BYTE*>(pKey);
    RxCacheEntry* Entry;
    HRESULT hr;

    UNREFERENCED_PARAMETER(hRTObject);

    if (KeyLen > UINT_MAX || (pObjectCode != nullptr && pObjectCode->ValueSize > UINT_MAX) ||
        (pCompilerMetadata != nullptr && pCompilerMetadata->ValueSize > UINT_MAX) ||
        ((UINT)Flags & ~(D3D12DDI_SHADER_CACHE_GET_VALUE_FLAG_STORED_VALUES_ONLY |
                         D3D12DDI_SHADER_CACHE_GET_VALUE_FLAG_0118_PSDB_MISS_EXPECTED)) != 0)
    {
        return E_INVALIDARG;
    }

    AcquireSRWLockShared(&RxCacheLock);
    Entry = RxCacheFind(Adapter, RxCacheHash(Adapter, Key, KeyLen), Key, KeyLen);
    if (Entry == nullptr)
    {
        ReleaseSRWLockShared(&RxCacheLock);
        return DXGI_ERROR_NOT_FOUND;
    }

    hr = RxCacheCopyOut(pObjectCode, Entry->Data + Entry->KeySize, Entry->CodeSize, pCallbackFunc, pContext);
    if (SUCCEEDED(hr))
    {
        hr = RxCacheCopyOut(pCompilerMetadata, Entry->Data + Entry->KeySize + Entry->CodeSize, Entry->MetadataSize,
                            pCallbackFunc, pContext);
    }
    ReleaseSRWLockShared(&RxCacheLock);
    return hr;
}

static
HRESULT
APIENTRY
RxShaderCacheStoreValueCb(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HANDLE_AND_TYPE hRTObject,
    _In_reads_bytes_(KeyLen) const void* pKey,
    _In_ SIZE_T KeyLen,
    _In_ CONST D3D12DDI_SHADER_CACHE_VALUE* pObjectCode,
    _In_ CONST D3D12DDI_SHADER_CACHE_VALUE* pCompilerMetadata)
{
    const LUID& Adapter = static_cast<RxDevice12*>(hRTDevice.handle)->GetAdapter().GetLuid();
    const BYTE* Key = static_cast<const BYTE*>(pKey);
    SIZE_T CodeSize = pObjectCode != nullptr ? pObjectCode->ValueSize : 0;
    SIZE_T MetadataSize = pCompilerMetadata != nullptr ? pCompilerMetadata->ValueSize : 0;
    RxCacheEntry* Entry;
    RxCacheEntry* Old;
    UINT Hash;

    UNREFERENCED_PARAMETER(hRTObject);

    if (pKey == nullptr || KeyLen == 0 || KeyLen > UINT_MAX || CodeSize > UINT_MAX || MetadataSize > UINT_MAX ||
        (CodeSize != 0 && pObjectCode->pValue == nullptr) || (MetadataSize != 0 && pCompilerMetadata->pValue == nullptr))
    {
        return E_INVALIDARG;
    }

    Entry = static_cast<RxCacheEntry*>(malloc(sizeof(*Entry)));
    if (Entry == nullptr)
        return E_OUTOFMEMORY;
    Entry->Data = static_cast<BYTE*>(malloc(KeyLen + CodeSize + MetadataSize));
    if (Entry->Data == nullptr)
    {
        free(Entry);
        return E_OUTOFMEMORY;
    }

    Hash = RxCacheHash(Adapter, Key, KeyLen);
    Entry->NextStored = nullptr;
    Entry->Adapter = Adapter;
    Entry->Hash = Hash;
    Entry->KeySize = KeyLen;
    Entry->CodeSize = CodeSize;
    Entry->MetadataSize = MetadataSize;
    CopyMemory(Entry->Data, Key, KeyLen);
    if (CodeSize != 0)
        CopyMemory(Entry->Data + KeyLen, pObjectCode->pValue, CodeSize);
    if (MetadataSize != 0)
        CopyMemory(Entry->Data + KeyLen + CodeSize, pCompilerMetadata->pValue, MetadataSize);

    AcquireSRWLockExclusive(&RxCacheLock);

    /* A newer value replaces the old one */
    Old = RxCacheFind(Adapter, Hash, Key, KeyLen);
    if (Old != nullptr)
        RxCacheDrop(Old);

    while (RxCacheOldest != nullptr && RxCacheBytes + KeyLen + CodeSize + MetadataSize > RxCacheLimit)
        RxCacheDrop(RxCacheOldest);

    Entry->NextInBucket = RxCacheBuckets[Hash % RxCacheBucketCount];
    RxCacheBuckets[Hash % RxCacheBucketCount] = Entry;
    if (RxCacheNewest != nullptr)
        RxCacheNewest->NextStored = Entry;
    else
        RxCacheOldest = Entry;
    RxCacheNewest = Entry;
    RxCacheBytes += KeyLen + CodeSize + MetadataSize;

    ReleaseSRWLockExclusive(&RxCacheLock);
    return S_OK;
}

/* Drivers say when a value no longer helps; memory goes the next time the cache is full */
static
HRESULT
APIENTRY
RxShaderCacheUnusedValueCb(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HANDLE_AND_TYPE hRTObject,
    _In_reads_bytes_(KeySize) const void* pKey,
    _In_ SIZE_T KeySize,
    _In_ D3D12DDI_SHADER_CACHE_UNUSED_VALUE_FLAGS Flags)
{
    UNREFERENCED_PARAMETER(hRTDevice);
    UNREFERENCED_PARAMETER(hRTObject);
    UNREFERENCED_PARAMETER(pKey);
    UNREFERENCED_PARAMETER(KeySize);
    UNREFERENCED_PARAMETER(Flags);
    return S_OK;
}

/* State object descs are not kept for drivers to read back */
static
HRESULT
APIENTRY
RxShaderCacheGetDescCb(
    _In_ D3D12DDI_HRTDEVICE hRTDevice,
    _In_ D3D12DDI_HRTSTATEOBJECT_0054 hRTSO,
    _Out_ D3D12DDIARG_CREATE_STATE_OBJECT_0054* pSODESC)
{
    UNREFERENCED_PARAMETER(hRTDevice);
    UNREFERENCED_PARAMETER(hRTSO);
    ZeroMemory(pSODESC, sizeof(*pSODESC));
    return E_NOTIMPL;
}

static const D3D12DDI_SHADERCACHE_CALLBACKS_0118 RxCacheCallbacks0118 =
{
    RxShaderCacheGetValueCb,
    RxShaderCacheStoreValueCb,
    RxShaderCacheUnusedValueCb,
};

static const D3D12DDI_SHADERCACHE_CALLBACKS_0114 RxCacheCallbacks0114 =
{
    RxShaderCacheGetValueCb,
    RxShaderCacheStoreValueCb,
    RxShaderCacheGetDescCb,
};

HRESULT
RxEnableShaderCache12(
    _In_ const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096& Functions,
    _In_ D3D12DDI_HDEVICE Device)
{
    UINT32 Versions[8];
    UINT32 Count = 0;
    UINT Chosen = 0;
    HRESULT hr;

    hr = Functions.pfnGetSupportedExtendedFeatureVersions(Device, D3D12DDI_FEATURE_0021_SHADERCACHING, &Count, nullptr);
    if (FAILED(hr))
        return hr;
    if (Count > _countof(Versions))
        Count = _countof(Versions);
    hr = Functions.pfnGetSupportedExtendedFeatureVersions(Device, D3D12DDI_FEATURE_0021_SHADERCACHING, &Count, Versions);
    if (FAILED(hr))
        return hr;

    /* The newest callback form both sides know; the first form, keyed by pipeline state, is not driven */
    for (UINT Index = 0; Index < Count; Index++)
    {
        if ((Versions[Index] == D3D12DDI_FEATURE_VERSION_SHADERCACHE_0118_0 ||
             Versions[Index] == D3D12DDI_FEATURE_VERSION_SHADERCACHE_0114_0) &&
            Versions[Index] > Chosen)
        {
            Chosen = Versions[Index];
        }
    }
    if (Chosen == 0)
        return S_OK;

    hr = Functions.pfnEnableExtendedFeature(Device, D3D12DDI_FEATURE_0021_SHADERCACHING, Chosen);
    if (FAILED(hr))
        return hr;

    if (Chosen == D3D12DDI_FEATURE_VERSION_SHADERCACHE_0118_0)
    {
        return Functions.pfnSetExtendedFeatureCallbacks(Device, D3D12DDI_TABLE_TYPE_0021_SHADERCACHE_CALLBACKS,
                                                        &RxCacheCallbacks0118, sizeof(RxCacheCallbacks0118));
    }
    return Functions.pfnSetExtendedFeatureCallbacks(Device, D3D12DDI_TABLE_TYPE_0021_SHADERCACHE_CALLBACKS,
                                                    &RxCacheCallbacks0114, sizeof(RxCacheCallbacks0114));
}
