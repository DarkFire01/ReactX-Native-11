/*
 * PROJECT:     ReactX DirectWrite
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Common includes
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <windows.h>

/* This module defines DWriteCreateFactory, so the header must not import it */
#define DWRITE_EXPORT WINAPI
#include <dwrite_3.h>

#include <new>
#include <vector>
#include <algorithm>

#include "trace.hpp"

/* Smart pointer for the COM objects this module holds internally */
template <typename T>
class RxComPtr
{
public:
    RxComPtr() : Pointer(nullptr) {}

    RxComPtr(
        _In_opt_ T* Value) : Pointer(Value)
    {
        if (Pointer != nullptr)
            Pointer->AddRef();
    }

    RxComPtr(
        _In_ const RxComPtr& Other) : Pointer(Other.Pointer)
    {
        if (Pointer != nullptr)
            Pointer->AddRef();
    }

    RxComPtr(
        _Inout_ RxComPtr&& Other) noexcept : Pointer(Other.Pointer)
    {
        Other.Pointer = nullptr;
    }

    ~RxComPtr()
    {
        Reset();
    }

    RxComPtr&
    operator=(
        _In_ const RxComPtr& Other)
    {
        if (this != &Other)
        {
            if (Other.Pointer != nullptr)
                Other.Pointer->AddRef();
            Reset();
            Pointer = Other.Pointer;
        }
        return *this;
    }

    RxComPtr&
    operator=(
        _Inout_ RxComPtr&& Other) noexcept
    {
        if (this != &Other)
        {
            Reset();
            Pointer = Other.Pointer;
            Other.Pointer = nullptr;
        }
        return *this;
    }

    VOID
    Reset(VOID)
    {
        if (Pointer != nullptr)
        {
            Pointer->Release();
            Pointer = nullptr;
        }
    }

    /* Takes over a reference the caller already owns */
    VOID
    Attach(
        _In_opt_ T* Value)
    {
        Reset();
        Pointer = Value;
    }

    T**
    Put(VOID)
    {
        Reset();
        return &Pointer;
    }

    void**
    PutVoid(VOID)
    {
        return reinterpret_cast<void**>(Put());
    }

    T*
    Get(VOID) const
    {
        return Pointer;
    }

    T*
    operator->() const
    {
        return Pointer;
    }

    /* Hands out a new reference */
    T*
    Copy(VOID) const
    {
        if (Pointer != nullptr)
            Pointer->AddRef();
        return Pointer;
    }

private:
    T* Pointer;
};

/* Reference counting shared by the module's COM objects */
#define RX_IMPLEMENT_REFCOUNT(Class) \
    ULONG \
    STDMETHODCALLTYPE \
    Class::AddRef(VOID) \
    { \
        return InterlockedIncrement(&RefCount); \
    } \
    \
    ULONG \
    STDMETHODCALLTYPE \
    Class::Release(VOID) \
    { \
        ULONG Count = InterlockedDecrement(&RefCount); \
        \
        if (Count == 0) \
            delete this; \
        return Count; \
    }

#define RX_DECLARE_IUNKNOWN \
    HRESULT \
    STDMETHODCALLTYPE \
    QueryInterface( \
        _In_ REFIID riid, \
        _COM_Outptr_ void** ppvObject) override; \
    \
    ULONG \
    STDMETHODCALLTYPE \
    AddRef(VOID) override; \
    \
    ULONG \
    STDMETHODCALLTYPE \
    Release(VOID) override;
