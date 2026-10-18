/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     A plain ID3D10Blob over a heap buffer
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3dcommon.h>

class RxBlob final : public ID3D10Blob
{
public:
    static
    HRESULT
    Create(
        _In_ SIZE_T Size,
        _Outptr_ RxBlob** Blob)
    {
        RxBlob* NewBlob = new (std::nothrow) RxBlob();

        *Blob = nullptr;
        if (NewBlob == nullptr)
            return E_OUTOFMEMORY;

        NewBlob->Data = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Size ? Size : 1);
        if (NewBlob->Data == nullptr)
        {
            delete NewBlob;
            return E_OUTOFMEMORY;
        }

        NewBlob->Size = Size;
        *Blob = NewBlob;
        return S_OK;
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        if (ppvObject == nullptr)
            return E_POINTER;

        if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D10Blob))
        {
            *ppvObject = static_cast<ID3D10Blob*>(this);
            AddRef();
            return S_OK;
        }

        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return InterlockedIncrement(&RefCount);
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        ULONG Count = InterlockedDecrement(&RefCount);

        if (Count == 0)
            delete this;
        return Count;
    }

    LPVOID
    STDMETHODCALLTYPE
    GetBufferPointer(VOID) override
    {
        return Data;
    }

    SIZE_T
    STDMETHODCALLTYPE
    GetBufferSize(VOID) override
    {
        return Size;
    }

private:
    RxBlob() = default;

    ~RxBlob()
    {
        if (Data != nullptr)
            HeapFree(GetProcessHeap(), 0, Data);
    }

    LONG RefCount = 1;
    BYTE* Data = nullptr;
    SIZE_T Size = 0;
};
