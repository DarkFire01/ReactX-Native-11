/*
 * PROJECT:     ReactX WARP
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The Direct3D 12 device and its DDI tables
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d12umddi.h>

class RxWarpAdapter;

class RxWarp12Device
{
public:
    static
    HRESULT
    Create(
        _In_ RxWarpAdapter *Adapter,
        _In_ const D3D12DDIARG_CREATEDEVICE_0003 *Args);

    static
    VOID
    APIENTRY
    Destroy(
        _In_ D3D12DDI_HDEVICE Device);

    /* The newest table of each kind; older interfaces take a prefix of it */
    static const D3D12DDI_DEVICE_FUNCS_CORE_0116 *DeviceTable();
    static const D3D12DDI_COMMAND_LIST_FUNCS_3D_0114 *CommandListTable();
    static const D3D12DDI_COMMAND_QUEUE_FUNCS_CORE_0001 *CommandQueueTable();
    static const D3D12DDI_EXTENDED_FEATURES_FUNCS_0096 *ExtendedFeaturesTable();
    static const DXGI1_4_DDI_BASE_FUNCTIONS *DxgiTable();

    RxWarpAdapter *Adapter = nullptr;
    D3D12DDI_HRTDEVICE RuntimeDevice = {};
    UINT Interface = 0;
    UINT Version = 0;
    D3D12DDI_CREATE_DEVICE_FLAGS Flags = D3D12DDI_CREATE_DEVICE_FLAG_NONE;
    const D3DDDI_DEVICECALLBACKS *KernelCallbacks = nullptr;
    const D3D12DDI_CORELAYER_DEVICECALLBACKS_0003 *Callbacks = nullptr;
    VOID *ContentProtectionCallback = nullptr;
};

/* Command allocators: the runtime makes one with every device */
class RxWarp12CommandPool
{
public:
    RxWarp12Device *Device = nullptr;
};

/* Root signatures: the layout is read when command lists bind them */
class RxWarp12RootSignature
{
public:
    RxWarp12Device *Device = nullptr;
};

/* Pipeline states: compiled later, when the shaders run */
class RxWarp12PipelineState
{
public:
    RxWarp12Device *Device = nullptr;
};
