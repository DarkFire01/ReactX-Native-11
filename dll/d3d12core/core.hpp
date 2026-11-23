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

/* D3D12CreateDevice with a choice over the adapter's singleton device, as device factories make */
HRESULT
WINAPI
RxCreateSharedDevice(
    _In_opt_ IUnknown* pAdapter,
    _In_ D3D_FEATURE_LEVEL MinimumFeatureLevel,
    _In_ UINT Sharing,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppDevice);

/* What applications ask removed device data to record, applied to devices made after */
struct RxDredState
{
    D3D12_DRED_ENABLEMENT AutoBreadcrumbs;
    D3D12_DRED_ENABLEMENT PageFault;
    D3D12_DRED_ENABLEMENT WatsonDump;
    D3D12_DRED_ENABLEMENT BreadcrumbContext;
    BOOL MarkersOnlyBreadcrumbs;
};

extern RxDredState RxProcessDred;

/* A settings object writing one DRED state, the process one or a device factory's */
HRESULT
RxMakeDredSettings(
    _In_ RxDredState* Target,
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject);

/* A device factory starting from the process state */
HRESULT
RxMakeDeviceFactory(
    _In_ REFIID riid,
    _COM_Outptr_opt_ void** ppvObject);

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
