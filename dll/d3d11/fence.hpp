/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ID3D11Fence over a kernel synchronization object
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "devicechild.hpp"

class RxFence final : public RxDeviceChild<ID3D11Fence>
{
public:
    static constexpr GUID PrivateId = { 0x7c3e51a8, 0x2b94, 0x4d6f, { 0xb1, 0x37, 0x0e, 0x58, 0xa4, 0xc2, 0x96, 0x1d } };

    static
    HRESULT
    Create(
        _In_ RxDevice& Device,
        _In_ UINT64 InitialValue,
        _In_ D3D11_FENCE_FLAG Flags,
        _Outptr_ RxFence** Fence);

    static
    HRESULT
    Open(
        _In_ RxDevice& Device,
        _In_ HANDLE SharedHandle,
        _Outptr_ RxFence** Fence);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateSharedHandle(
        _In_opt_ const SECURITY_ATTRIBUTES* pAttributes,
        _In_ DWORD dwAccess,
        _In_opt_ LPCWSTR lpName,
        _Out_ HANDLE* pHandle) override;

    UINT64
    STDMETHODCALLTYPE
    GetCompletedValue(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    SetEventOnCompletion(
        _In_ UINT64 Value,
        _In_opt_ HANDLE hEvent) override;

    D3DKMT_HANDLE
    GetSyncObject() const
    {
        return SyncObject;
    }

private:
    explicit RxFence(
        _In_ RxDevice& Device)
        : RxDeviceChild<ID3D11Fence>(Device)
    {
    }

    ~RxFence() override;

    D3DKMT_HANDLE SyncObject = 0;

    /* Where a monitored fence's current value can be read; null for other fences */
    volatile UINT64* CpuValue = nullptr;

    BOOLEAN IsShareable = FALSE;
};
