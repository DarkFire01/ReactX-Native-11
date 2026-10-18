/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     What the ReactX d3d12.dll loader asks d3d12core.dll for
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

/* Passed to the core's D3D12GetInterface to get RxD3D12CoreExports back */
inline constexpr GUID CLSID_RxD3D12CoreExports = {0x5c0b8e1a, 0x8f2d, 0x4b7e, {0x9a, 0x61, 0x2e, 0x4d, 0x7c, 0x93, 0x18, 0xb5}};

/* Bumped whenever RxD3D12CoreExports changes shape */
constexpr UINT RxD3D12CoreExportsVersion = 1;

struct RxD3D12CoreExports
{
    UINT Version;

    HRESULT
    (WINAPI* CreateDevice)(
        _In_opt_ IUnknown* pAdapter,
        _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
        _In_ REFIID riid,
        _COM_Outptr_opt_ void** ppDevice);

    HRESULT
    (WINAPI* SerializeRootSignature)(
        _In_ const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
        _In_ D3D_ROOT_SIGNATURE_VERSION Version,
        _Out_ ID3DBlob** ppBlob,
        _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob);

    HRESULT
    (WINAPI* CreateRootSignatureDeserializer)(
        _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
        _In_ SIZE_T SrcDataSizeInBytes,
        _In_ REFIID pRootSignatureDeserializerInterface,
        _Out_ void** ppRootSignatureDeserializer);

    HRESULT
    (WINAPI* SerializeVersionedRootSignature)(
        _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
        _Out_ ID3DBlob** ppBlob,
        _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob);

    HRESULT
    (WINAPI* CreateVersionedRootSignatureDeserializer)(
        _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
        _In_ SIZE_T SrcDataSizeInBytes,
        _In_ REFIID pRootSignatureDeserializerInterface,
        _Out_ void** ppRootSignatureDeserializer);

    HRESULT
    (WINAPI* EnableExperimentalFeatures)(
        _In_ UINT NumFeatures,
        _In_count_(NumFeatures) const IID* pIIDs,
        _In_opt_count_(NumFeatures) void* pConfigurationStructs,
        _In_opt_count_(NumFeatures) UINT* pConfigurationStructSizes);

    HRESULT
    (WINAPI* GetInterface)(
        _In_ REFCLSID rclsid,
        _In_ REFIID riid,
        _COM_Outptr_opt_ void** ppvDebug);
};
