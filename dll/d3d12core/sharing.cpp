/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NT handles for shared device children
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "objects.hpp"

#include <winternl.h>

/* What a name that is already taken comes back as */
constexpr HRESULT RxHandleInUse = E_HANDLE;

/* A fence shares its monitored fence; only full access may be asked for */
static
HRESULT
RxShareFence(
    _In_ RxDevice12& Device,
    _In_ RxFence& Fence,
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    _In_ DWORD Access,
    _Out_ HANDLE* pHandle)
{
    D3DKMT_HANDLE Object = Fence.GetSyncObject();
    OBJECT_ATTRIBUTES Attributes;
    HRESULT hr;

    if (Access != GENERIC_ALL)
        return E_INVALIDARG;

    InitializeObjectAttributes(&Attributes, nullptr, 0, nullptr,
                               pAttributes != nullptr ? pAttributes->lpSecurityDescriptor : nullptr);
    if (pAttributes != nullptr && pAttributes->bInheritHandle)
        Attributes.Attributes |= OBJ_INHERIT;

    hr = RxKernelResult(Device, RxGetKmtThunks()->ShareObjects(1, &Object, &Attributes, Access, pHandle),
                        "D3DKMTShareObjects", nullptr);
    return hr == RxHandleInUse ? DXGI_ERROR_NAME_ALREADY_EXISTS : hr;
}

HRESULT
STDMETHODCALLTYPE
RxDevice12::CreateSharedHandle(
    _In_ ID3D12DeviceChild* pObject,
    _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
    DWORD Access,
    _In_opt_ LPCWSTR Name,
    _Out_ HANDLE* pHandle)
{
    RxFence* Fence = RxUnwrap12<RxFence>(pObject);

    /* Failures leave the output as the application had it */
    if (pHandle == nullptr)
        return E_INVALIDARG;

    if (Fence == nullptr)
    {
        RX_TRACE(L"only fences can be shared so far");
        return E_NOTIMPL;
    }

    /* Named handles live in the session's object directory, which this runtime does not open yet */
    if (Name != nullptr)
    {
        RX_TRACE(L"named shared handles are not supported");
        return E_NOTIMPL;
    }

    return RxShareFence(*this, *Fence, pAttributes, Access, pHandle);
}
