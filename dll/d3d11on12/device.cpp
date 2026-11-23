/*
 * PROJECT:     ReactX Direct3D 11 on 12
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The DDI device, recording Direct3D 11 work as D3D12 command lists
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#include "precomp.hpp"
#include "adapter.hpp"
#include "device.hpp"

HRESULT
O12Device::Create(
    _In_ O12Adapter* Adapter,
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args)
{
    O12Device* Device;
    HRESULT hr;

    Device = new (Args->hDrvDevice.pDrvPrivate) O12Device(Adapter, Args);
    hr = Device->Initialize();
    if (FAILED(hr))
    {
        Device->~O12Device();
        return hr;
    }

    Device->FillTables(Args);
    return S_OK;
}

O12Device::O12Device(
    _In_ O12Adapter* Adapter,
    _In_ const D3D10DDIARG_CREATEDEVICE* Args)
    : Adapter(Adapter),
      D3D12(Adapter->GetDevice()),
      Context(Adapter->GetDevice(), Adapter->GetNodeIndex()),
      RuntimeDevice(Args->hRTDevice),
      RuntimeCore(Args->hRTCoreLayer),
      KernelCallbacks(Args->pKTCallbacks),
      CoreCallbacks(Args->pWDDM2_6UMCallbacks),
      Interface(Args->Interface),
      Version(Args->Version),
      CreateFlags(Args->Flags)
{
}

O12Device::~O12Device()
{
}

HRESULT
O12Device::Initialize()
{
    O12ContextArgs Args = {};

    Args.DisableGpuTimeout = Adapter->DisablesGpuTimeout();
    Args.ComputeOnly = Context.GetFeatureLevel() == D3D_FEATURE_LEVEL_1_0_CORE;
    return Context.Initialize(Adapter->GetGraphicsQueue(), Args);
}

VOID
O12Device::FillTables(
    _Inout_ D3D10DDIARG_CREATEDEVICE* Args)
{
    D3DWDDM2_6DDI_DEVICEFUNCS* Functions = Args->pWDDM2_6DeviceFuncs;

    O12FillStubs(Functions, Args->DXGIBaseDDI.pDXGIDDIBaseFunctions6_1);
    O12FillStateFunctions(Functions);
    Functions->pfnDestroyDevice = DestroyDevice;
}

VOID
APIENTRY
O12Device::DestroyDevice(
    _In_ D3D10DDI_HDEVICE Device)
{
    O12Device* Self = From(Device);

    Self->Context.RunQueued();
    Self->~O12Device();
}

#define O12_UNIMPLEMENTED() O12_TRACE_ONCE(L"unimplemented %S", __FUNCTION__)

STDMETHODIMP
O12Device::GetD3D12Device(
    REFIID Iid,
    void** Object)
{
    return D3D12->QueryInterface(Iid, Object);
}

STDMETHODIMP
O12Device::GetGraphicsQueue(
    REFIID Iid,
    void** Object)
{
    return Context.GetGraphicsQueue()->QueryInterface(Iid, Object);
}

STDMETHODIMP
O12Device::EnqueueSetEvent(
    _In_ HANDLE Event)
{
    UNREFERENCED_PARAMETER(Event);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP_(UINT)
O12Device::GetNodeMask()
{
    return 1u << Adapter->GetNodeIndex();
}

STDMETHODIMP
O12Device::Present(
    _In_ D3DKMT_PRESENT* Args)
{
    UNREFERENCED_PARAMETER(Args);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP_(UINT)
O12Device::GetResourcePrivateDataSize()
{
    O12_UNIMPLEMENTED();
    return 0;
}

STDMETHODIMP
O12Device::OpenSharedHandle(
    _In_ HANDLE Shared,
    _Out_writes_bytes_(PrivateSize) void* Private,
    UINT PrivateSize,
    _Out_ D3DKMT_HANDLE* KernelHandle)
{
    UNREFERENCED_PARAMETER(Shared);
    UNREFERENCED_PARAMETER(Private);
    UNREFERENCED_PARAMETER(PrivateSize);
    *KernelHandle = 0;
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateWrappingHandle(
    _In_ IUnknown* Resource,
    On12WrapReason Reason,
    _Out_writes_bytes_(PrivateSize) void* Private,
    UINT PrivateSize,
    _Out_ D3DKMT_HANDLE* KernelHandle)
{
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(Reason);
    UNREFERENCED_PARAMETER(Private);
    UNREFERENCED_PARAMETER(PrivateSize);
    *KernelHandle = 0;
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::FillResourceInfo(
    D3DKMT_HANDLE KernelHandle,
    _In_ const D3D11_RESOURCE_FLAGS* Overrides,
    _Out_ On12ResourceInfo* Info)
{
    UNREFERENCED_PARAMETER(KernelHandle);
    UNREFERENCED_PARAMETER(Overrides);
    UNREFERENCED_PARAMETER(Info);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP_(VOID)
O12Device::DestroyKMTHandle(
    D3DKMT_HANDLE KernelHandle)
{
    UNREFERENCED_PARAMETER(KernelHandle);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::TransitionResourceForRelease(
    _In_ IOn12DdiResource* Resource,
    D3D12_RESOURCE_STATES State)
{
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(State);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::ApplyAllResourceTransitions()
{
    O12_UNIMPLEMENTED();
}

STDMETHODIMP
O12Device::CreateFence(
    UINT64 InitialValue,
    UINT Flags,
    _COM_Outptr_ IOn12DdiFence** Fence)
{
    UNREFERENCED_PARAMETER(InitialValue);
    UNREFERENCED_PARAMETER(Flags);
    *Fence = nullptr;
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::OpenFence(
    HANDLE Shared,
    _Out_ bool* IsMonitored,
    _COM_Outptr_ IOn12DdiFence** Fence)
{
    UNREFERENCED_PARAMETER(Shared);
    *IsMonitored = false;
    *Fence = nullptr;
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::Wait(
    _In_ IOn12DdiFence* Fence,
    UINT64 Value)
{
    UNREFERENCED_PARAMETER(Fence);
    UNREFERENCED_PARAMETER(Value);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::Signal(
    _In_ IOn12DdiFence* Fence,
    UINT64 Value)
{
    UNREFERENCED_PARAMETER(Fence);
    UNREFERENCED_PARAMETER(Value);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateVertexShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12ShaderDesc* Desc)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreatePixelShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12ShaderDesc* Desc)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateGeometryShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12GeometryShaderDesc* Desc,
    _In_opt_ const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* StreamOut)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    UNREFERENCED_PARAMETER(StreamOut);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateHullShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12ShaderDesc* Desc)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateDomainShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12ShaderDesc* Desc)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::CreateComputeShader(
    D3D10DDI_HSHADER Shader,
    _In_ const On12ShaderDesc* Desc)
{
    UNREFERENCED_PARAMETER(Shader);
    UNREFERENCED_PARAMETER(Desc);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP_(VOID)
O12Device::WriteToSubresource(
    D3D10DDI_HRESOURCE Destination,
    UINT Subresource,
    _In_opt_ const D3D11_BOX* Box,
    const void* Source,
    UINT RowPitch,
    UINT DepthPitch)
{
    UNREFERENCED_PARAMETER(Destination);
    UNREFERENCED_PARAMETER(Subresource);
    UNREFERENCED_PARAMETER(Box);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(RowPitch);
    UNREFERENCED_PARAMETER(DepthPitch);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::ReadFromSubresource(
    void* Destination,
    UINT RowPitch,
    UINT DepthPitch,
    D3D10DDI_HRESOURCE Source,
    UINT Subresource,
    _In_opt_ const D3D11_BOX* Box)
{
    UNREFERENCED_PARAMETER(Destination);
    UNREFERENCED_PARAMETER(RowPitch);
    UNREFERENCED_PARAMETER(DepthPitch);
    UNREFERENCED_PARAMETER(Source);
    UNREFERENCED_PARAMETER(Subresource);
    UNREFERENCED_PARAMETER(Box);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::SharingContractPresent(
    _In_ IOn12DdiResource* Resource)
{
    UNREFERENCED_PARAMETER(Resource);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::SetMarker(
    _In_opt_z_ const wchar_t* Name)
{
    UNREFERENCED_PARAMETER(Name);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::BeginEvent(
    _In_opt_z_ const wchar_t* Name)
{
    UNREFERENCED_PARAMETER(Name);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(VOID)
O12Device::EndEvent()
{
    O12_UNIMPLEMENTED();
}

STDMETHODIMP
O12Device::AddResourceWaitsToQueue(
    _In_ IOn12DdiResource* Resource,
    _In_ ID3D12CommandQueue* Queue)
{
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(Queue);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::AddDeferredWaitsToResource(
    _In_ IOn12DdiResource* Resource,
    UINT Count,
    _In_reads_(Count) UINT64* Values,
    _In_reads_(Count) ID3D12Fence** Fences)
{
    UNREFERENCED_PARAMETER(Resource);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Values);
    UNREFERENCED_PARAMETER(Fences);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP_(VOID)
O12Device::SetMaximumFrameLatency(
    UINT Latency)
{
    UNREFERENCED_PARAMETER(Latency);
    O12_UNIMPLEMENTED();
}

STDMETHODIMP_(bool)
O12Device::IsMaximumFrameLatencyReached()
{
    O12_UNIMPLEMENTED();
    return false;
}

STDMETHODIMP
O12Device::CloseAndSubmitGraphicsCommandListForPresent(
    BOOL CommandsAdded,
    _In_reads_(SourceCount) const On12PresentSurface* Sources,
    UINT SourceCount,
    _In_opt_ IOn12DdiResource* Destination,
    _In_ D3DKMT_PRESENT* KernelPresent)
{
    UNREFERENCED_PARAMETER(CommandsAdded);
    UNREFERENCED_PARAMETER(Sources);
    UNREFERENCED_PARAMETER(SourceCount);
    UNREFERENCED_PARAMETER(Destination);
    UNREFERENCED_PARAMETER(KernelPresent);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::PreExecuteCommandQueueCommand(
    On12QueueKind Kind)
{
    UNREFERENCED_PARAMETER(Kind);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}

STDMETHODIMP
O12Device::PostExecuteCommandQueueCommand(
    On12QueueKind Kind)
{
    UNREFERENCED_PARAMETER(Kind);
    O12_UNIMPLEMENTED();
    return E_NOTIMPL;
}
