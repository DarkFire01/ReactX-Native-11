/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DDI device, recording Direct3D 11 work as D3D12 command lists
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "context.hpp"

class O12Adapter;

enum O12Stage
{
    O12StageVertex,
    O12StagePixel,
    O12StageGeometry,
    O12StageHull,
    O12StageDomain,
    O12StageCompute,
    O12StageCount,
};

/* Bindings that turn into pipeline state and descriptor tables at the next draw */
constexpr UINT O12DirtyPipeline = 0x1;
constexpr UINT O12DirtySamplers = 0x2;

struct O12PipelineInputs
{
    PVOID Blend;
    PVOID DepthStencil;
    PVOID Rasterizer;
    UINT SampleMask;
    PVOID Samplers[O12StageCount][D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT];
    PVOID CacheSession;
    UINT Dirty;
};

class O12Device final : public IOn12DdiDevice
{
public:
    static
    HRESULT
    Create(
        _In_ O12Adapter* Adapter,
        _Inout_ D3D10DDIARG_CREATEDEVICE* Args);

    static
    O12Device*
    From(
        _In_ D3D10DDI_HDEVICE Device)
    {
        return static_cast<O12Device*>(static_cast<IOn12DdiDevice*>(Device.pDrvPrivate));
    }

    /* IOn12DdiDevice */
    STDMETHOD(GetD3D12Device)(REFIID Iid, void** Object) override;
    STDMETHOD(GetGraphicsQueue)(REFIID Iid, void** Object) override;
    STDMETHOD(EnqueueSetEvent)(_In_ HANDLE Event) override;
    STDMETHOD_(UINT, GetNodeMask)() override;
    STDMETHOD(Present)(_In_ D3DKMT_PRESENT* Args) override;
    STDMETHOD_(UINT, GetResourcePrivateDataSize)() override;
    STDMETHOD(OpenSharedHandle)(_In_ HANDLE Shared, _Out_writes_bytes_(PrivateSize) void* Private, UINT PrivateSize,
                                _Out_ D3DKMT_HANDLE* KernelHandle) override;
    STDMETHOD(CreateWrappingHandle)(_In_ IUnknown* Resource, On12WrapReason Reason,
                                    _Out_writes_bytes_(PrivateSize) void* Private, UINT PrivateSize,
                                    _Out_ D3DKMT_HANDLE* KernelHandle) override;
    STDMETHOD(FillResourceInfo)(D3DKMT_HANDLE KernelHandle, _In_ const D3D11_RESOURCE_FLAGS* Overrides,
                                _Out_ On12ResourceInfo* Info) override;
    STDMETHOD_(VOID, DestroyKMTHandle)(D3DKMT_HANDLE KernelHandle) override;
    STDMETHOD_(VOID, TransitionResourceForRelease)(_In_ IOn12DdiResource* Resource,
                                                   D3D12_RESOURCE_STATES State) override;
    STDMETHOD_(VOID, ApplyAllResourceTransitions)() override;
    STDMETHOD(CreateFence)(UINT64 InitialValue, UINT Flags, _COM_Outptr_ IOn12DdiFence** Fence) override;
    STDMETHOD(OpenFence)(HANDLE Shared, _Out_ bool* IsMonitored, _COM_Outptr_ IOn12DdiFence** Fence) override;
    STDMETHOD(Wait)(_In_ IOn12DdiFence* Fence, UINT64 Value) override;
    STDMETHOD(Signal)(_In_ IOn12DdiFence* Fence, UINT64 Value) override;
    STDMETHOD(CreateVertexShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) override;
    STDMETHOD(CreatePixelShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) override;
    STDMETHOD(CreateGeometryShader)(D3D10DDI_HSHADER Shader, _In_ const On12GeometryShaderDesc* Desc,
                                    _In_opt_ const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* StreamOut) override;
    STDMETHOD(CreateHullShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) override;
    STDMETHOD(CreateDomainShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) override;
    STDMETHOD(CreateComputeShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) override;
    STDMETHOD_(VOID, WriteToSubresource)(D3D10DDI_HRESOURCE Destination, UINT Subresource, _In_opt_ const D3D11_BOX* Box,
                                         const void* Source, UINT RowPitch, UINT DepthPitch) override;
    STDMETHOD_(VOID, ReadFromSubresource)(void* Destination, UINT RowPitch, UINT DepthPitch, D3D10DDI_HRESOURCE Source,
                                          UINT Subresource, _In_opt_ const D3D11_BOX* Box) override;
    STDMETHOD_(VOID, SharingContractPresent)(_In_ IOn12DdiResource* Resource) override;
    STDMETHOD_(VOID, SetMarker)(_In_opt_z_ const wchar_t* Name) override;
    STDMETHOD_(VOID, BeginEvent)(_In_opt_z_ const wchar_t* Name) override;
    STDMETHOD_(VOID, EndEvent)() override;
    STDMETHOD(AddResourceWaitsToQueue)(_In_ IOn12DdiResource* Resource, _In_ ID3D12CommandQueue* Queue) override;
    STDMETHOD(AddDeferredWaitsToResource)(_In_ IOn12DdiResource* Resource, UINT Count,
                                          _In_reads_(Count) UINT64* Values,
                                          _In_reads_(Count) ID3D12Fence** Fences) override;
    STDMETHOD_(VOID, SetMaximumFrameLatency)(UINT Latency) override;
    STDMETHOD_(bool, IsMaximumFrameLatencyReached)() override;
    STDMETHOD(CloseAndSubmitGraphicsCommandListForPresent)(BOOL CommandsAdded,
                                                           _In_reads_(SourceCount) const On12PresentSurface* Sources,
                                                           UINT SourceCount, _In_opt_ IOn12DdiResource* Destination,
                                                           _In_ D3DKMT_PRESENT* KernelPresent) override;
    STDMETHOD(PreExecuteCommandQueueCommand)(On12QueueKind Kind) override;
    STDMETHOD(PostExecuteCommandQueueCommand)(On12QueueKind Kind) override;

    O12Adapter*
    GetAdapter() const
    {
        return Adapter;
    }

    ID3D12Device*
    GetD3D12() const
    {
        return D3D12;
    }

    O12Context&
    GetContext()
    {
        return Context;
    }

    O12PipelineInputs&
    GetInputs()
    {
        return Inputs;
    }

private:
    O12Device(
        _In_ O12Adapter* Adapter,
        _In_ const D3D10DDIARG_CREATEDEVICE* Args);

    ~O12Device();

    HRESULT
    Initialize();

    VOID
    FillTables(
        _Inout_ D3D10DDIARG_CREATEDEVICE* Args);

    static
    VOID
    APIENTRY
    DestroyDevice(
        _In_ D3D10DDI_HDEVICE Device);

    O12Adapter* Adapter;
    ID3D12Device* D3D12;
    O12Context Context;
    O12PipelineInputs Inputs = {};
    D3D10DDI_HRTDEVICE RuntimeDevice;
    D3D10DDI_HRTCORELAYER RuntimeCore;
    const D3DDDI_DEVICECALLBACKS* KernelCallbacks;
    const D3DWDDM2_6DDI_CORELAYER_DEVICECALLBACKS* CoreCallbacks;
    UINT Interface;
    UINT Version;
    UINT CreateFlags;
};

VOID
O12FillStateFunctions(
    _Inout_ D3DWDDM2_6DDI_DEVICEFUNCS* Functions);

/* Points every entry of the DDI tables at a stub that names itself once */
VOID
O12FillStubs(
    _Out_ D3DWDDM2_6DDI_DEVICEFUNCS* Device,
    _Out_ DXGI1_6_1_DDI_BASE_FUNCTIONS* Dxgi);
