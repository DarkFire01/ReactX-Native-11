/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Protected resource sessions
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "objects.hpp"

/* What the driver gets as a runtime protected session handle: the kernel session it asks the runtime for */
struct RxKernelSession12
{
    RxDevice12* Device;
    D3DKMT_HANDLE StatusFence;
    D3DKMT_HANDLE Session;

    /* Handed to the kernel as the runtime's own data about the session */
    const D3D12_PROTECTED_RESOURCE_SESSION_DESC1* Desc;
};

/* The runtime side of the driver's request for a kernel protected session */
HRESULT
RxCreateKernelSession12(
    _In_ RxKernelSession12& Session,
    _In_ const D3D12DDICB_CREATE_PROTECTED_SESSION_0030& Args);

class RxProtectedResourceSession final : public RxDeviceChild12<ID3D12ProtectedResourceSession1>
{
public:
    static constexpr GUID PrivateId = {0x4f9b27c3, 0x6e1d, 0x45a0, {0x9c, 0x83, 0x17, 0xe2, 0x5b, 0xa4, 0x08, 0xd6}};

    /* The desc is checked; the node mask is in driver numbering */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& Desc,
        _In_ UINT DriverNodeMask,
        _Outptr_ RxProtectedResourceSession** Session);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    GetStatusFence(
        REFIID riid,
        _COM_Outptr_opt_ void** ppFence) override;

    D3D12_PROTECTED_SESSION_STATUS
    STDMETHODCALLTYPE
    GetSessionStatus(VOID) override;

    D3D12_PROTECTED_RESOURCE_SESSION_DESC
    STDMETHODCALLTYPE
    GetDesc(VOID) override;

    D3D12_PROTECTED_RESOURCE_SESSION_DESC1
    STDMETHODCALLTYPE
    GetDesc1(VOID) override;

    D3D12DDI_HPROTECTEDRESOURCESESSION_0030
    GetDriverHandle() const
    {
        return DriverSession;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxProtectedResourceSession() override;

    HRESULT
    Initialize(
        _In_ const D3D12_PROTECTED_RESOURCE_SESSION_DESC1& SessionDesc,
        _In_ UINT DriverNodeMask);

    D3D12_PROTECTED_RESOURCE_SESSION_DESC1 Desc = {};
    RxFence* StatusFence = nullptr;
    RxKernelSession12 Kernel = {};
    RxDriverMemory12 Memory;
    D3D12DDI_HPROTECTEDRESOURCESESSION_0030 DriverSession = {};
    BOOLEAN IsDriverSessionCreated = FALSE;
};
