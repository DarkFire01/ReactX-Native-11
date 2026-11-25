/*
 * PROJECT:     ReactX Direct3D 12 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Meta commands and lifetime trackers
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "device.hpp"

/* A driver operation applications find by GUID */
class RxMetaCommand final : public RxDeviceChild12<ID3D12MetaCommand>
{
public:
    static constexpr GUID PrivateId = {0x6b0e93d2, 0x4c15, 0x4a87, {0x9e, 0x3f, 0x21, 0xd8, 0x70, 0xb4, 0x5c, 0x96}};

    /* The command is one the device lists; the node mask is in driver numbering */
    static
    HRESULT
    Create(
        _In_ RxDevice12& Device,
        _In_ const GUID& CommandId,
        _In_ UINT DriverNodeMask,
        _In_reads_bytes_opt_(ParametersSize) const void* Parameters,
        _In_ SIZE_T ParametersSize,
        _Outptr_ RxMetaCommand** Command);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    UINT64
    STDMETHODCALLTYPE
    GetRequiredParameterResourceSize(
        _In_ D3D12_META_COMMAND_PARAMETER_STAGE Stage,
        _In_ UINT ParameterIndex) override;

    D3D12DDI_HMETACOMMAND_0052
    GetDriverHandle() const
    {
        return DriverCommand;
    }

private:
    using RxDeviceChild12::RxDeviceChild12;
    ~RxMetaCommand() override;

    HRESULT
    Initialize(
        _In_ const GUID& CommandId,
        _In_ UINT DriverNodeMask,
        _In_reads_bytes_opt_(ParametersSize) const void* Parameters,
        _In_ SIZE_T ParametersSize);

    RxDriverMemory12 Memory;
    D3D12DDI_HMETACOMMAND_0052 DriverCommand = {};
    BOOLEAN IsDriverCommandCreated = FALSE;
};

/* Hands destruction of owned objects to an owner; nothing is owned without a protected workload */
class RxLifetimeTracker final : public RxDeviceChild12<ID3D12LifetimeTracker>
{
public:
    static constexpr GUID PrivateId = {0x2d7c4f19, 0xa3e6, 0x4b52, {0x87, 0x0d, 0x5e, 0xc1, 0x94, 0x3a, 0x6f, 0x28}};

    RxLifetimeTracker(
        _In_ RxDevice12& Device,
        _In_ ID3D12LifetimeOwner* Owner)
        : RxDeviceChild12(Device),
          LifetimeOwner(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    HRESULT
    STDMETHODCALLTYPE
    DestroyOwnedObject(
        _In_ ID3D12DeviceChild* pObject) override;

private:
    ~RxLifetimeTracker() override = default;

    /* Not referenced, as on Windows */
    ID3D12LifetimeOwner* LifetimeOwner;
};
