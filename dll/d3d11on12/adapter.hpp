/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DDI adapter the Direct3D 11 runtime opens over a D3D12 device
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

class O12Adapter
{
public:
    static
    HRESULT
    Open(
        _Inout_ D3D10DDIARG_OPENADAPTER* Args,
        _Inout_ On12OpenArgs* On12);

    ~O12Adapter();

    static
    O12Adapter*
    From(
        _In_ D3D10DDI_HADAPTER Adapter)
    {
        return static_cast<O12Adapter*>(Adapter.pDrvPrivate);
    }

    ID3D12Device*
    GetDevice() const
    {
        return Device;
    }

    ID3D12CommandQueue*
    GetGraphicsQueue() const
    {
        return GraphicsQueue;
    }

    UINT
    GetNodeIndex() const
    {
        return NodeIndex;
    }

    const D3D12_FEATURE_DATA_D3D12_OPTIONS&
    GetOptions() const
    {
        return Options;
    }

    const D3D12_FEATURE_DATA_ARCHITECTURE1&
    GetArchitecture() const
    {
        return Architecture;
    }

    BOOLEAN
    IsComputeOnly() const
    {
        return ComputeOnly;
    }

    const On12Callbacks&
    GetCallbacks() const
    {
        return Callbacks;
    }

    BOOLEAN
    DisablesGpuTimeout() const
    {
        return DisableGpuTimeout;
    }

    BOOLEAN
    SupportsDisplayableTextures() const
    {
        return DisplayableTextures;
    }

    BOOLEAN
    HasPresentCallback() const
    {
        return PresentCallbackPresent;
    }

    BOOLEAN
    TakesPrepatchedShaders() const
    {
        return PrepatchedShaders;
    }

private:
    O12Adapter() = default;

    HRESULT
    Initialize(
        _Inout_ On12OpenArgs* On12);

    static
    SIZE_T
    APIENTRY
    CalcPrivateDeviceSize(
        _In_ D3D10DDI_HADAPTER Adapter,
        _In_ const D3D10DDIARG_CALCPRIVATEDEVICESIZE* Args);

    static
    HRESULT
    APIENTRY
    CreateDevice(
        _In_ D3D10DDI_HADAPTER Adapter,
        _Inout_ D3D10DDIARG_CREATEDEVICE* Args);

    static
    HRESULT
    APIENTRY
    CloseAdapter(
        _In_ D3D10DDI_HADAPTER Adapter);

    static
    HRESULT
    APIENTRY
    GetSupportedVersions(
        _In_ D3D10DDI_HADAPTER Adapter,
        _Inout_ UINT32* Count,
        _Out_writes_opt_(*Count) UINT64* Versions);

    static
    HRESULT
    APIENTRY
    GetCaps(
        _In_ D3D10DDI_HADAPTER Adapter,
        _In_ const D3D10_2DDIARG_GETCAPS* Args);

    HRESULT
    GetPipelineCaps(
        _Out_ UINT* Caps) const;

    UINT
    GetTiledResourceCaps() const;

    ID3D12Device* Device = nullptr;
    ID3D12CommandQueue* GraphicsQueue = nullptr;
    ID3D12CompatibilityDevice* Compatibility = nullptr;
    UINT NodeIndex = 0;
    D3D12_FEATURE_DATA_ARCHITECTURE1 Architecture = {};
    D3D12_FEATURE_DATA_D3D12_OPTIONS Options = {};
    D3D_SHADER_MODEL HighestShaderModel = D3D_SHADER_MODEL_6_2;
    BOOLEAN ComputeOnly = FALSE;
    On12Callbacks Callbacks = {};
    PVOID PresentCallback = nullptr;
    BOOLEAN DisableGpuTimeout = FALSE;
    BOOLEAN DisplayableTextures = FALSE;
    BOOLEAN DeferredContexts = TRUE;
    BOOLEAN PresentCallbackPresent = FALSE;
    BOOLEAN PrepatchedShaders = FALSE;
};
