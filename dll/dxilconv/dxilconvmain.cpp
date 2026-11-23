/*
 * PROJECT:     ReactX DXBC to DXIL converter
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Exports, the class factory and the converter object
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "dxilconv.hpp"
#include "upstream.hpp"

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

/* Takes over the caller's reference on Inner */
CDxbcConverter::CDxbcConverter(
    _In_ IDxbcConverter* Inner)
    : m_References(1),
      m_Inner(Inner),
      m_SkipDigestCheck(FALSE),
      m_RunCleanup(TRUE)
{
}

CDxbcConverter::~CDxbcConverter()
{
    m_Inner->Release();
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
            hr = m_Inner->Convert(Dxbc, DxbcSize, ExtraOptions, Dxil, DxilSize, Diag);

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
    HRESULT hr;

    if (Bytecode == nullptr || Blob == nullptr)
    {
        hr = E_INVALIDARG;
    }
    else if (Bytecode[1] < 2)
    {
        hr = DXC_STATUS_BAD_BYTECODE;
    }
    else
    {
        *Blob = nullptr;
        if (Diag != nullptr)
            *Diag = nullptr;

        ApplyOptions(ExtraOptions);
        hr = m_Inner->ConvertInDriver(Bytecode,
                                      InputSignature,
                                      InputCount,
                                      OutputSignature,
                                      OutputCount,
                                      PatchSignature,
                                      PatchCount,
                                      ExtraOptions,
                                      Blob,
                                      Diag);
    }

    /* The result record fingerprints the token stream by its own length word */
    if (FAILED(hr))
    {
        ULONG Length = *reinterpret_cast<const volatile UINT32*>(Bytecode + 1);
        ULONG Input = DxcFingerprint(Bytecode, Length * sizeof(UINT32));

        DxcTrace(L"ConvertInDriver failed 0x%08lX (input %08lX)", hr, Input);
    }

    return hr;
}

/* The upstream converter does the work; this object keeps the checks in front of it */
static
HRESULT
DxcCreateConverter(
    _In_opt_ IMalloc* Malloc,
    _In_ REFIID Riid,
    _Out_ LPVOID* Object)
{
    CDxbcConverter* Converter;
    IDxbcConverter* Inner;
    HRESULT hr;

    hr = DxcUpstreamCreateConverter(Malloc, __uuidof(IDxbcConverter), reinterpret_cast<void**>(&Inner));
    if (FAILED(hr))
    {
        DxcTrace(L"creating the upstream converter failed 0x%08lX", hr);
        return hr;
    }

    Converter = new (std::nothrow) CDxbcConverter(Inner);
    if (Converter == nullptr)
    {
        Inner->Release();
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
DxcCreateInstance(
    _In_ REFCLSID Clsid,
    _In_ REFIID Riid,
    _Out_ LPVOID* Object)
{
    *Object = nullptr;
    if (!IsEqualCLSID(Clsid, DxcClassConverter))
        return REGDB_E_CLASSNOTREG;

    return DxcCreateConverter(nullptr, Riid, Object);
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
    if (Object == nullptr)
        return E_POINTER;

    *Object = nullptr;
    if (!IsEqualCLSID(Clsid, DxcClassConverter))
        return REGDB_E_CLASSNOTREG;

    return DxcCreateConverter(Malloc, Riid, Object);
}

BOOL
WINAPI
DllMain(
    _In_ HINSTANCE Instance,
    _In_ DWORD Reason,
    _In_opt_ LPVOID Reserved)
{
    if (Reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(Instance);
        if (!DxcUpstreamAttach())
        {
            DxcTrace(L"converter runtime setup failed");
            return FALSE;
        }
    }
    else if (Reason == DLL_PROCESS_DETACH)
    {
        DxcUpstreamDetach();
    }

    return TRUE;
}
