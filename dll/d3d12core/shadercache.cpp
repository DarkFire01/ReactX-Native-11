/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The shader cache the runtime keeps for drivers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "device.hpp"
#include "shadercache.hpp"

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
    RxDevice12* Device = static_cast<RxDevice12*>(hRTDevice.handle);
    RxShaderCacheStore12* Store = Device->GetDriverShaderCache();
    BYTE* Stored;
    UINT32 CodeSize = 0;
    UINT Size = 0;
    HRESULT hr;

    UNREFERENCED_PARAMETER(hRTObject);

    if (KeyLen == 0 || KeyLen > UINT_MAX || (pObjectCode != nullptr && pObjectCode->ValueSize > UINT_MAX) ||
        (pCompilerMetadata != nullptr && pCompilerMetadata->ValueSize > UINT_MAX) ||
        ((UINT)Flags & ~(D3D12DDI_SHADER_CACHE_GET_VALUE_FLAG_STORED_VALUES_ONLY |
                         D3D12DDI_SHADER_CACHE_GET_VALUE_FLAG_0118_PSDB_MISS_EXPECTED)) != 0)
    {
        return E_INVALIDARG;
    }
    if (Store == nullptr || (Device->GetDisabledShaderCaches() & RxCacheKindDriver12) != 0)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    /* With nowhere to put a value the driver only asks whether the key is there */
    hr = Store->Find(pKey, (UINT)KeyLen, nullptr, &Size);
    if (FAILED(hr) || (pObjectCode == nullptr && pCompilerMetadata == nullptr))
        return hr;

    Stored = static_cast<BYTE*>(malloc(Size));
    if (Stored == nullptr)
        return E_OUTOFMEMORY;
    hr = Store->Find(pKey, (UINT)KeyLen, Stored, &Size);
    if (FAILED(hr))
    {
        free(Stored);
        return hr;
    }

    /* The code size comes first, then the code and the compiler's metadata */
    if (Size >= sizeof(CodeSize))
        CopyMemory(&CodeSize, Stored, sizeof(CodeSize));
    if (Size < sizeof(CodeSize) || CodeSize > Size - sizeof(CodeSize))
    {
        free(Stored);
        return DXGI_ERROR_NOT_FOUND;
    }

    hr = RxCacheCopyOut(pObjectCode, Stored + sizeof(CodeSize), CodeSize, pCallbackFunc, pContext);
    if (SUCCEEDED(hr))
    {
        hr = RxCacheCopyOut(pCompilerMetadata, Stored + sizeof(CodeSize) + CodeSize, Size - sizeof(CodeSize) - CodeSize,
                            pCallbackFunc, pContext);
    }
    free(Stored);
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
    RxDevice12* Device = static_cast<RxDevice12*>(hRTDevice.handle);
    RxShaderCacheStore12* Store = Device->GetDriverShaderCache();
    SIZE_T CodeSize = pObjectCode != nullptr ? pObjectCode->ValueSize : 0;
    SIZE_T MetadataSize = pCompilerMetadata != nullptr ? pCompilerMetadata->ValueSize : 0;
    BYTE* Value;
    UINT32 Prefix;
    HRESULT hr;

    UNREFERENCED_PARAMETER(hRTObject);

    if (Store == nullptr)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    if (pKey == nullptr || KeyLen == 0 || KeyLen > UINT_MAX || CodeSize > UINT_MAX - sizeof(Prefix) ||
        MetadataSize > UINT_MAX || (pObjectCode == nullptr && pCompilerMetadata == nullptr) ||
        (CodeSize != 0 && pObjectCode->pValue == nullptr) || (MetadataSize != 0 && pCompilerMetadata->pValue == nullptr))
    {
        return E_INVALIDARG;
    }
    if ((Device->GetDisabledShaderCaches() & RxCacheKindDriver12) != 0)
        return DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;

    Value = static_cast<BYTE*>(malloc(sizeof(Prefix) + CodeSize));
    if (Value == nullptr)
        return E_OUTOFMEMORY;
    Prefix = (UINT32)CodeSize;
    CopyMemory(Value, &Prefix, sizeof(Prefix));
    if (CodeSize != 0)
        CopyMemory(Value + sizeof(Prefix), pObjectCode->pValue, CodeSize);

    hr = Store->Store(pKey, (UINT)KeyLen, Value, (UINT)(sizeof(Prefix) + CodeSize),
                      MetadataSize != 0 ? pCompilerMetadata->pValue : nullptr, (UINT)MetadataSize);
    free(Value);
    return hr;
}

/* Drivers say when a value no longer helps; the store keeps it until it is cleared */
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
