/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Shared ID3D11DeviceChild plumbing: lifetime, device link and private data
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"
#include "bound.hpp"

#include <malloc.h>

/* Driver private memory sized by a CalcPrivate*Size call, freed with the object */
class RxDriverMemory
{
public:
    RxDriverMemory() = default;

    ~RxDriverMemory()
    {
        if (Memory != nullptr)
            _aligned_free(Memory);
    }

    RxDriverMemory(const RxDriverMemory&) = delete;
    RxDriverMemory& operator=(const RxDriverMemory&) = delete;

    _Ret_maybenull_
    PVOID
    Allocate(
        _In_ SIZE_T Size)
    {
        Memory = _aligned_malloc(Size ? Size : 1, 16);
        if (Memory != nullptr)
            ZeroMemory(Memory, Size);
        return Memory;
    }

private:
    PVOID Memory = nullptr;
};

/* Recovers a runtime object from an API pointer, or nullptr when it came from elsewhere */
template <typename Object>
_Ret_maybenull_
Object*
RxUnwrap(
    _In_opt_ IUnknown* Unknown)
{
    Object* Found = nullptr;

    if (Unknown == nullptr || FAILED(Unknown->QueryInterface(Object::PrivateId, reinterpret_cast<void**>(&Found))))
        return nullptr;

    return Found;
}

/* Every child keeps the device alive; QueryInterface stays with the concrete class */
/*
 * Lifetime shared by every device child. Public references keep the device alive;
 * internal ones, taken by pipeline bindings and device defaults, keep only the object.
 * That breaks the device -> context -> bound object -> device cycle.
 */
class RxChildLifetime
{
public:
    explicit RxChildLifetime(
        _In_ RxDevice& Owner)
        : Owner(Owner)
    {
        Owner.AddRef();
    }

    virtual ~RxChildLifetime()
    {
        delete D3D10Face;
    }

    ULONG
    AddPublicRef()
    {
        ULONG Count = InterlockedIncrement(&PublicCount);

        /* Coming back from zero, as when a getter hands out a bound object */
        if (Count == 1)
            Owner.AddRef();
        return Count;
    }

    ULONG
    ReleasePublicRef()
    {
        RxDevice& Device = Owner;
        ULONG Count = InterlockedDecrement(&PublicCount);

        /* The device may go with the last child, so it is released after the delete */
        if (Count == 0)
        {
            if (InternalCount == 0)
                delete this;
            Device.Release();
        }

        return Count;
    }

    VOID
    AddInternalRef()
    {
        InternalCount++;
    }

    VOID
    ReleaseInternalRef()
    {
        if (--InternalCount == 0 && PublicCount == 0)
            delete this;
    }

    /* Made by RxQueryD3D10Face when a Direct3D 10 caller first asks for it */
    RxD3D10FaceBase* D3D10Face = nullptr;

private:
    RxDevice& Owner;
    LONG PublicCount = 1;
    LONG InternalCount = 0;
};

template <typename Interface>
class RxDeviceChild : public Interface, public RxChildLifetime
{
public:
    explicit RxDeviceChild(
        _In_ RxDevice& Owner)
        : RxChildLifetime(Owner),
          Device(Owner)
    {
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return AddPublicRef();
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return ReleasePublicRef();
    }

    VOID
    STDMETHODCALLTYPE
    GetDevice(
        _Outptr_ ID3D11Device** ppDevice) override
    {
        Device.AddRef();
        *ppDevice = &Device;
    }

    HRESULT
    STDMETHODCALLTYPE
    GetPrivateData(
        _In_ REFGUID guid,
        _Inout_ UINT* pDataSize,
        _Out_writes_bytes_opt_(*pDataSize) void* pData) override
    {
        return PrivateData.Get(guid, pDataSize, pData);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateData(
        _In_ REFGUID guid,
        _In_ UINT DataSize,
        _In_reads_bytes_opt_(DataSize) const void* pData) override
    {
        return PrivateData.Set(guid, DataSize, pData, nullptr);
    }

    HRESULT
    STDMETHODCALLTYPE
    SetPrivateDataInterface(
        _In_ REFGUID guid,
        _In_opt_ const IUnknown* pData) override
    {
        return PrivateData.Set(guid, 0, nullptr, pData);
    }

protected:
    RxDevice& Device;
    RxPrivateDataStore PrivateData;
};
