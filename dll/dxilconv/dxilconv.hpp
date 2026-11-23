/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Converter object, digest helpers and shared declarations
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>
#include <objidl.h>
#define DXC_API_IMPORT
#include <dxcapi.h>

#include "trace.hpp"

/* Reported for a token stream shorter than its two header words */
#define DXC_STATUS_BAD_BYTECODE         ((HRESULT)0x80AA0005L)

/* The interface the D3D12 runtime, WARP and the translation layers ask for */
struct __declspec(uuid("5F956ED5-78D1-4B15-8247-F7187614A041")) IDxbcConverter : public IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Convert(
        _In_reads_bytes_(DxbcSize) LPCVOID Dxbc,
        _In_ UINT32 DxbcSize,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Outptr_result_bytebuffer_maybenull_(*DxilSize) LPVOID* Dxil,
        _Out_ UINT32* DxilSize,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) = 0;

    virtual HRESULT STDMETHODCALLTYPE ConvertInDriver(
        _In_ const UINT32* Bytecode,
        _In_opt_ LPCVOID InputSignature,
        _In_ UINT32 InputCount,
        _In_opt_ LPCVOID OutputSignature,
        _In_ UINT32 OutputCount,
        _In_opt_ LPCVOID PatchSignature,
        _In_ UINT32 PatchCount,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Out_ IDxcBlob** Blob,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) = 0;
};

extern const CLSID DxcClassConverter;

typedef enum _DXC_DIGEST_KIND
{
    DxcDigestNone,
    DxcDigestRetail,
    DxcDigestDebug,
} DXC_DIGEST_KIND;

VOID
DxcComputeDigest(
    _In_reads_bytes_(Size) const BYTE* Data,
    _In_ ULONG Size,
    _In_ DXC_DIGEST_KIND Kind,
    _Out_writes_bytes_(16) BYTE* Digest);

DXC_DIGEST_KIND
DxcMatchDigest(
    _In_reads_bytes_(Size) const BYTE* Container,
    _In_ ULONG Size);

/* The converter object handed out by DxcCreateInstance */
class CDxbcConverter : public IDxbcConverter
{
public:
    explicit CDxbcConverter(
        _In_ IDxbcConverter* Inner);

    STDMETHOD(QueryInterface)(
        _In_ REFIID Riid,
        _COM_Outptr_ void** Object) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(Convert)(
        _In_reads_bytes_(DxbcSize) LPCVOID Dxbc,
        _In_ UINT32 DxbcSize,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Outptr_result_bytebuffer_maybenull_(*DxilSize) LPVOID* Dxil,
        _Out_ UINT32* DxilSize,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) override;

    STDMETHOD(ConvertInDriver)(
        _In_ const UINT32* Bytecode,
        _In_opt_ LPCVOID InputSignature,
        _In_ UINT32 InputCount,
        _In_opt_ LPCVOID OutputSignature,
        _In_ UINT32 OutputCount,
        _In_opt_ LPCVOID PatchSignature,
        _In_ UINT32 PatchCount,
        _In_opt_z_ LPCWSTR ExtraOptions,
        _Out_ IDxcBlob** Blob,
        _Outptr_opt_result_maybenull_z_ LPWSTR* Diag) override;

private:
    ~CDxbcConverter();

    VOID
    ApplyOptions(
        _In_opt_z_ LPCWSTR ExtraOptions);

    LONG m_References;
    IDxbcConverter* m_Inner;
    BOOLEAN m_SkipDigestCheck;
    BOOLEAN m_RunCleanup;
};
