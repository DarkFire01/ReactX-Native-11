/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exports, the class factory and the converter object
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"

#include <new>

const CLSID DxcClassConverter = { 0x4900391E, 0xB752, 0x4EDD, { 0xA8, 0x85, 0x6F, 0xB7, 0x6E, 0x25, 0xAD, 0xDB } };

/* FNV-1a over a caller buffer; it touches every byte on purpose */
static
ULONG
DxcFingerprint(
    _In_reads_bytes_opt_(Size) const VOID* Data,
    _In_ ULONG Size)
{
    const volatile BYTE* Bytes = static_cast<const volatile BYTE*>(Data);
    ULONG Hash = 0x811C9DC5;

    if (Data == nullptr || Size == 0)
        return 0;

    for (ULONG Index = 0; Index < Size; Index++)
    {
        Hash ^= Bytes[Index];
        Hash *= 0x01000193;
    }

    return Hash;
}

CDxbcConverter::CDxbcConverter(
    _In_opt_ IMalloc* Malloc)
    : m_References(1),
      m_Malloc(Malloc),
      m_SkipDigestCheck(FALSE),
      m_RunCleanup(TRUE)
{
    if (m_Malloc != nullptr)
        m_Malloc->AddRef();
}

CDxbcConverter::~CDxbcConverter()
{
    if (m_Malloc != nullptr)
        m_Malloc->Release();
}

STDMETHODIMP
CDxbcConverter::QueryInterface(
    _In_ REFIID Riid,
    _COM_Outptr_ void** Object)
{
    if (Object == nullptr)
        return E_POINTER;

    if (!IsEqualIID(Riid, __uuidof(IUnknown)) &&
        !IsEqualIID(Riid, __uuidof(INoMarshal)) &&
        !IsEqualIID(Riid, __uuidof(IDxbcConverter)))
    {
        return E_NOINTERFACE;
    }

    *Object = static_cast<IDxbcConverter*>(this);
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG)
CDxbcConverter::AddRef()
{
    return static_cast<ULONG>(InterlockedIncrement(&m_References));
}

STDMETHODIMP_(ULONG)
CDxbcConverter::Release()
{
    LONG Remaining = InterlockedDecrement(&m_References);

    if (Remaining == 0)
        delete this;

    return static_cast<ULONG>(Remaining);
}

/* Options only ever switch behavior on; a later call cannot undo an earlier one */
VOID
CDxbcConverter::ApplyOptions(
    _In_opt_z_ LPCWSTR ExtraOptions)
{
    if (ExtraOptions == nullptr)
        return;

    if (wcsstr(ExtraOptions, L"-disableHashCheck") != nullptr && !m_SkipDigestCheck)
    {
        m_SkipDigestCheck = TRUE;
        DxcTrace(L"container digest checks are off for this converter");
    }

    if (wcsstr(ExtraOptions, L"-no-dxil-cleanup") != nullptr && m_RunCleanup)
    {
        m_RunCleanup = FALSE;
        DxcTrace(L"the DXIL cleanup pass is off for this converter");
    }
}

STDMETHODIMP
CDxbcConverter::Convert(
    _In_reads_bytes_(DxbcSize) LPCVOID Dxbc,
    _In_ UINT32 DxbcSize,
    _In_opt_z_ LPCWSTR ExtraOptions,
    _Outptr_result_bytebuffer_maybenull_(*DxilSize) LPVOID* Dxil,
    _Out_ UINT32* DxilSize,
    _Outptr_opt_result_maybenull_z_ LPWSTR* Diag)
{
    const BYTE* Source = static_cast<const BYTE*>(Dxbc);
    DXC_DIGEST_KIND Digest = DxcDigestNone;
    HRESULT hr = S_OK;

    if (Source == nullptr || Dxil == nullptr || DxilSize == nullptr)
    {
        hr = E_INVALIDARG;
    }
    else
    {
        *Dxil = nullptr;
        *DxilSize = 0;
        if (Diag != nullptr)
            *Diag = nullptr;

        ApplyOptions(ExtraOptions);

        if (DxbcSize < 32 ||
            *reinterpret_cast<const ULONG*>(Source) != DXC_FOURCC('D', 'X', 'B', 'C') ||
            *reinterpret_cast<const ULONG*>(Source + 24) != DxbcSize)
        {
            hr = E_FAIL;
        }
        else if (!m_SkipDigestCheck)
        {
            Digest = DxcMatchDigest(Source, DxbcSize);
            if (Digest == DxcDigestNone)
                hr = E_FAIL;
        }

        if (SUCCEEDED(hr))
            hr = ConvertContainer(Source, DxbcSize, Dxil, DxilSize);

        if (SUCCEEDED(hr) && *Dxil != nullptr && Digest != DxcDigestNone)
        {
            BYTE* Out = static_cast<BYTE*>(*Dxil);

            DxcComputeDigest(Out + 20, *DxilSize - 20, Digest, Out + 4);
        }
    }

    /* The result record reads both out parameters whatever state they are in */
    if (FAILED(hr))
    {
        ULONG Input = DxcFingerprint(Dxbc, DxbcSize);
        ULONG Output = DxcFingerprint(*Dxil, *DxilSize);

        DxcTrace(L"Convert of %u bytes failed 0x%08lX (input %08lX, output %08lX)", DxbcSize, hr, Input, Output);
    }

    return hr;
}

STDMETHODIMP
CDxbcConverter::ConvertInDriver(
    _In_ const UINT32* Bytecode,
    _In_opt_ LPCVOID InputSignature,
    _In_ UINT32 InputCount,
    _In_opt_ LPCVOID OutputSignature,
    _In_ UINT32 OutputCount,
    _In_opt_ LPCVOID PatchSignature,
    _In_ UINT32 PatchCount,
    _In_opt_z_ LPCWSTR ExtraOptions,
    _Out_ IDxcBlob** Blob,
    _Outptr_opt_result_maybenull_z_ LPWSTR* Diag)
{
    if (Bytecode == nullptr || Blob == nullptr)
        return E_INVALIDARG;

    if (Bytecode[1] < 2)
        return DXC_STATUS_BAD_BYTECODE;

    *Blob = nullptr;
    if (Diag != nullptr)
        *Diag = nullptr;

    ApplyOptions(ExtraOptions);

    DxcTrace(L"ConvertInDriver is not implemented yet");
    return E_NOTIMPL;
}

extern "C"
HRESULT
WINAPI
DxcCreateInstance(
    _In_ REFCLSID Clsid,
    _In_ REFIID Riid,
    _Out_ LPVOID* Object)
{
    CDxbcConverter* Converter;
    HRESULT hr;

    *Object = nullptr;
    if (!IsEqualCLSID(Clsid, DxcClassConverter))
        return REGDB_E_CLASSNOTREG;

    Converter = new (std::nothrow) CDxbcConverter(nullptr);
    if (Converter == nullptr)
    {
        DxcTrace(L"no memory for a converter");
        return E_OUTOFMEMORY;
    }

    hr = Converter->QueryInterface(Riid, Object);
    Converter->Release();
    return hr;
}

extern "C"
HRESULT
WINAPI
DxcCreateInstance2(
    _In_opt_ IMalloc* Malloc,
    _In_ REFCLSID Clsid,
    _In_ REFIID Riid,
    _Out_ LPVOID* Object)
{
    CDxbcConverter* Converter;
    HRESULT hr;

    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (!IsEqualCLSID(Clsid, DxcClassConverter))
        return REGDB_E_CLASSNOTREG;

    Converter = new (std::nothrow) CDxbcConverter(Malloc);
    if (Converter == nullptr)
    {
        DxcTrace(L"no memory for a converter");
        return E_OUTOFMEMORY;
    }

    hr = Converter->QueryInterface(Riid, Object);
    Converter->Release();
    return hr;
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(Instance);

    return TRUE;
}
