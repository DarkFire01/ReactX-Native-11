/*
 * PROJECT:     ReactX shared runtime code
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Binary contract between d3d11.dll and d3d11on12.dll
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include <d3d12.h>
#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d10umddi.h>

/* The newest contract revision both sides understand */
constexpr UINT On12ContractVersion = 7;

/* Set in D3D10DDIARG_CREATEDEVICE::Flags by Xbox titles */
constexpr UINT On12CreateDeviceXbox = 0x80000000;

struct IOn12DdiResource;

enum On12ResourceInfoKind
{
    On12ResourceInfoTilePool,
    On12ResourceInfoResource,
};

enum class On12QueueKind
{
    Graphics = 0,
    VideoDecode = 1,
    VideoProcess = 2,
    Count = 3,
};

/* What a kernel handle the layer handed out describes */
struct On12ResourceInfo
{
    union
    {
        struct
        {
            D3D12_HEAP_DESC HeapDesc;
        } TilePool;

        struct
        {
            D3D12_RESOURCE_DESC Desc;
            D3D11_RESOURCE_FLAGS Flags11;
            D3D12_HEAP_FLAGS HeapFlags;
            D3D12_HEAP_PROPERTIES HeapProperties;
        } Resource;
    };
    On12ResourceInfoKind Kind;
    bool IsShared;
    bool IsNtHandle;
    bool IsSynchronized;
    HANDLE GdiHandle;
};

struct On12PresentSurface
{
    IOn12DdiResource* Resource;
    UINT Subresource;
};

struct On12PresentArgs
{
    ID3D12CommandQueue* GraphicsQueue;
    ID3D12CommandList* GraphicsList;
    const On12PresentSurface* Sources;
    UINT SourceCount;
    IOn12DdiResource* Destination;
    UINT FlipInterval;
    UINT VidPnSourceId;
    D3DKMT_PRESENT* KernelPresent;
};

/* Runtime services the layer calls back into */
struct On12Callbacks
{
    D3D11_RESOURCE_FLAGS (CALLBACK* GetResourceFlags)(
        _In_ D3D10DDI_HRESOURCE Resource,
        _Out_ bool* AcquireOnWrite);

    bool (CALLBACK* NotifySharedResourceCreation)(
        _In_ HANDLE Resource,
        _In_ IUnknown* Object);
};

struct On12Callbacks2
{
    HRESULT (CALLBACK* Present)(
        _In_ HANDLE Device,
        _In_ On12PresentArgs* Args);
};

/* The second argument of OpenAdapter_D3D11On12 */
struct On12OpenArgs
{
    ID3D12Device1* Device;
    ID3D12CommandQueue* GraphicsQueue;
    IUnknown* Adapter;
    UINT NodeIndex;
    On12Callbacks Callbacks;
    bool DisableGpuTimeout;
    bool DisplayableTextures;
    bool DeferredContexts;
    UINT ContractVersion;
    On12Callbacks2* Callbacks2;
    bool PrepatchedShaders;
};

struct On12ShaderDesc
{
    const BYTE* Code;
    UINT Size;
    ID3D11ClassLinkage* Linkage;
};

struct On12GeometryShaderDesc
{
    const BYTE* Code;
    UINT Size;
    const D3D11_SO_DECLARATION_ENTRY* Declaration;
    UINT ElementCount;
    const UINT* Strides;
    UINT StrideCount;
    UINT RasterizedStream;
    ID3D11ClassLinkage* Linkage;
};

enum class On12WrapReason
{
    CreateWrappedResource = 1,
};

enum class On12StateReason
{
    Create,
    Acquire,
};

struct IOn12DdiResource
{
    STDMETHOD_(ID3D12Resource*, GetUnderlyingResource)() = 0;
    STDMETHOD_(VOID, SetGraphicsCurrentState)(D3D12_RESOURCE_STATES State, On12StateReason Reason) = 0;
    STDMETHOD(CreateSharedHandle)(_In_opt_ const SECURITY_ATTRIBUTES* Attributes, _In_ DWORD Access,
                                  _In_opt_ LPCWSTR Name, _Out_ HANDLE* Handle) = 0;
};

struct IOn12DdiFence
{
    STDMETHOD(CreateSharedHandle)(_In_opt_ const SECURITY_ATTRIBUTES* Attributes, _In_ DWORD Access,
                                  _In_opt_ LPCWSTR Name, _Out_ HANDLE* Handle) = 0;
    STDMETHOD(SetEventOnCompletion)(UINT64 Value, _In_ HANDLE Event) = 0;
    STDMETHOD_(UINT64, GetCompletedValue)() = 0;
    STDMETHOD_(VOID, Destroy)() = 0;
};

/* The extra device interface the hDrvDevice pointer starts with */
struct IOn12DdiDevice
{
    STDMETHOD(GetD3D12Device)(REFIID Iid, void** Object) = 0;
    STDMETHOD(GetGraphicsQueue)(REFIID Iid, void** Object) = 0;
    STDMETHOD(EnqueueSetEvent)(_In_ HANDLE Event) = 0;
    STDMETHOD_(UINT, GetNodeMask)() = 0;
    STDMETHOD(Present)(_In_ D3DKMT_PRESENT* Args) = 0;

    STDMETHOD_(UINT, GetResourcePrivateDataSize)() = 0;
    STDMETHOD(OpenSharedHandle)(_In_ HANDLE Shared, _Out_writes_bytes_(PrivateSize) void* Private, UINT PrivateSize,
                                _Out_ D3DKMT_HANDLE* KernelHandle) = 0;
    STDMETHOD(CreateWrappingHandle)(_In_ IUnknown* Resource, On12WrapReason Reason,
                                    _Out_writes_bytes_(PrivateSize) void* Private, UINT PrivateSize,
                                    _Out_ D3DKMT_HANDLE* KernelHandle) = 0;
    STDMETHOD(FillResourceInfo)(D3DKMT_HANDLE KernelHandle, _In_ const D3D11_RESOURCE_FLAGS* Overrides,
                                _Out_ On12ResourceInfo* Info) = 0;
    STDMETHOD_(VOID, DestroyKMTHandle)(D3DKMT_HANDLE KernelHandle) = 0;

    STDMETHOD_(VOID, TransitionResourceForRelease)(_In_ IOn12DdiResource* Resource, D3D12_RESOURCE_STATES State) = 0;
    STDMETHOD_(VOID, ApplyAllResourceTransitions)() = 0;

    STDMETHOD(CreateFence)(UINT64 InitialValue, UINT Flags, _COM_Outptr_ IOn12DdiFence** Fence) = 0;
    STDMETHOD(OpenFence)(HANDLE Shared, _Out_ bool* IsMonitored, _COM_Outptr_ IOn12DdiFence** Fence) = 0;
    STDMETHOD(Wait)(_In_ IOn12DdiFence* Fence, UINT64 Value) = 0;
    STDMETHOD(Signal)(_In_ IOn12DdiFence* Fence, UINT64 Value) = 0;

    STDMETHOD(CreateVertexShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) = 0;
    STDMETHOD(CreatePixelShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) = 0;
    STDMETHOD(CreateGeometryShader)(D3D10DDI_HSHADER Shader, _In_ const On12GeometryShaderDesc* Desc,
                                    _In_opt_ const D3D11DDIARG_CREATEGEOMETRYSHADERWITHSTREAMOUTPUT* StreamOut) = 0;
    STDMETHOD(CreateHullShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) = 0;
    STDMETHOD(CreateDomainShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) = 0;
    STDMETHOD(CreateComputeShader)(D3D10DDI_HSHADER Shader, _In_ const On12ShaderDesc* Desc) = 0;

    STDMETHOD_(VOID, WriteToSubresource)(D3D10DDI_HRESOURCE Destination, UINT Subresource, _In_opt_ const D3D11_BOX* Box,
                                         const void* Source, UINT RowPitch, UINT DepthPitch) = 0;
    STDMETHOD_(VOID, ReadFromSubresource)(void* Destination, UINT RowPitch, UINT DepthPitch, D3D10DDI_HRESOURCE Source,
                                          UINT Subresource, _In_opt_ const D3D11_BOX* Box) = 0;

    /* Revision 1 */
    STDMETHOD_(VOID, SharingContractPresent)(_In_ IOn12DdiResource* Resource) = 0;

    /* Revision 2 */
    STDMETHOD_(VOID, SetMarker)(_In_opt_z_ const wchar_t* Name) = 0;
    STDMETHOD_(VOID, BeginEvent)(_In_opt_z_ const wchar_t* Name) = 0;
    STDMETHOD_(VOID, EndEvent)() = 0;

    /* Revision 3 */
    STDMETHOD(AddResourceWaitsToQueue)(_In_ IOn12DdiResource* Resource, _In_ ID3D12CommandQueue* Queue) = 0;
    STDMETHOD(AddDeferredWaitsToResource)(_In_ IOn12DdiResource* Resource, UINT Count,
                                          _In_reads_(Count) UINT64* Values, _In_reads_(Count) ID3D12Fence** Fences) = 0;

    /* Revision 4 */
    STDMETHOD_(VOID, SetMaximumFrameLatency)(UINT Latency) = 0;
    STDMETHOD_(bool, IsMaximumFrameLatencyReached)() = 0;

    /* Revision 6 */
    STDMETHOD(CloseAndSubmitGraphicsCommandListForPresent)(BOOL CommandsAdded,
                                                           _In_reads_(SourceCount) const On12PresentSurface* Sources,
                                                           UINT SourceCount, _In_opt_ IOn12DdiResource* Destination,
                                                           _In_ D3DKMT_PRESENT* KernelPresent) = 0;
    STDMETHOD(PreExecuteCommandQueueCommand)(On12QueueKind Kind) = 0;
    STDMETHOD(PostExecuteCommandQueueCommand)(On12QueueKind Kind) = 0;
};

typedef HRESULT (APIENTRY* PFN_ON12_OPEN_ADAPTER)(_Inout_ D3D10DDIARG_OPENADAPTER* Args, _Inout_ On12OpenArgs* On12);
