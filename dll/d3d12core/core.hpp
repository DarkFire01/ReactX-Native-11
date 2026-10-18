/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The entry points the core hands to the loader
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

HRESULT
WINAPI
RxCreateDevice(
    _In_opt_ IUnknown* pAdapter,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppDevice);

HRESULT
WINAPI
RxSerializeRootSignature(
    _In_ const D3D12_ROOT_SIGNATURE_DESC* pRootSignature,
    _In_ D3D_ROOT_SIGNATURE_VERSION Version,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob);

HRESULT
WINAPI
RxSerializeVersionedRootSignature(
    _In_ const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* pRootSignature,
    _Out_ ID3DBlob** ppBlob,
    _Always_(_Outptr_opt_result_maybenull_) ID3DBlob** ppErrorBlob);

HRESULT
WINAPI
RxCreateRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer);

HRESULT
WINAPI
RxCreateVersionedRootSignatureDeserializer(
    _In_reads_bytes_(SrcDataSizeInBytes) LPCVOID pSrcData,
    _In_ SIZE_T SrcDataSizeInBytes,
    _In_ REFIID pRootSignatureDeserializerInterface,
    _Out_ void** ppRootSignatureDeserializer);
