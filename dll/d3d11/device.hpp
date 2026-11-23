/*
 * PROJECT:     ReactX Direct3D 11 runtime
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Device and immediate context objects
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "precomp.hpp"
#include "pipeline.hpp"
#include "privatedata.hpp"
#include "umdadapter.hpp"
#include "kmcallbacks.hpp"
#include "partner.hpp"
#include "d3d10face.hpp"

class RxDevice;

/*
 * What a D3D11 runtime records with a shared texture so another process can open it,
 * laid out as Windows writes it for a single sample swapchain buffer.
 */
struct RxSharedTextureRecord
{
    UINT Size;
    UINT Version;
    UINT Width;
    UINT Height;
    DXGI_FORMAT Format;
    UINT Constants[9];          /* The first is the array size; the rest are fixed for swapchain buffers */
    D3D11_RESOURCE_DIMENSION Dimension;
    D3D11_TEXTURE2D_DESC Desc;
};

static_assert(sizeof(RxSharedTextureRecord) == 0x68);

/* The only record version this runtime writes and reads */
constexpr UINT RxSharedRecordVersion = 4;

/* Record constants: the array size, the keyed mutex with its fence, then NT handle and displayable flags */
constexpr UINT RxSharedRecordArraySizeIndex = 0;
constexpr UINT RxSharedRecordKeyedMutexIndex = 2;
constexpr UINT RxSharedRecordKeyedMutexShareIndex = 3;
constexpr UINT RxSharedRecordSyncObjectShareIndex = 4;
constexpr UINT RxSharedRecordNtHandleIndex = 5;
constexpr UINT RxSharedRecordDisplayableIndex = 6;
constexpr UINT RxSharedRecordDisplayableAgainIndex = 8;

/* How a resource is shared, which picks the flags its kernel resource is created with */
enum class RxShareKind : UCHAR
{
    None,
    GlobalHandle,
    NtHandle,
    SwapChain
};

/*
 * The hRTResource a UMD gets for each resource. Drivers may deallocate after the resource
 * is destroyed, even as late as device teardown, so this outlives the D3D object until
 * the driver has let go of its kernel resource.
 */
struct RxRuntimeResource
{
    D3DKMT_HANDLE KmResource = 0;

    /* The API flags the resource was created with, which the D3D11 on 12 layer asks back for */
    D3D11_RESOURCE_FLAGS ApiFlags = {};

    /* Set for shared textures and swapchain buffers; written with the kernel resource */
    const VOID* SharedRecord = nullptr;
    UINT SharedRecordSize = 0;
    RxSharedTextureRecord Shared = {};
    RxShareKind ShareKind = RxShareKind::None;

    /* What GetSharedHandle hands out for a resource shared without NT security */
    D3DKMT_HANDLE GlobalShare = 0;

    /* Keyed mutex textures: the mutex and the fence its owners order their GPU work with, closed with the record */
    D3DKMT_HANDLE KeyedMutex = 0;
    D3DKMT_HANDLE SyncObject = 0;

    /* Opened from a shared handle; the runtime closes it only when the driver never opened it */
    BOOLEAN IsOpened = FALSE;

    /* The D3D object is gone and the driver still holds the kernel resource */
    BOOLEAN IsOrphaned = FALSE;
};

/* A tear-off of the immediate context, reached through QueryInterface on it */
class RxVideoContext final : public ID3D11VideoContext
{
public:
    explicit RxVideoContext(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "videocontext.inl"

private:
    RxDevice& Device;
    RxPrivateDataStore PrivateData;
};

/* Lives inside RxDevice and shares its reference count, like the D3D11 immediate context */
/*
 * ID3D10Multithread on the device and ID3D11Multithread on the context are the same lock.
 * A tear-off: lifetime and QueryInterface belong to the object it was reached from.
 */
template <typename Interface, typename Owner>
class RxMultithread final : public Interface
{
public:
    explicit RxMultithread(
        _In_ Owner& Parent,
        _In_ RxDevice& Device)
        : Parent(Parent),
          Device(Device)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override
    {
        return Parent.QueryInterface(riid, ppvObject);
    }

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override
    {
        return Parent.AddRef();
    }

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override
    {
        return Parent.Release();
    }

    VOID
    STDMETHODCALLTYPE
    Enter(VOID) override;

    VOID
    STDMETHODCALLTYPE
    Leave(VOID) override;

    BOOL
    STDMETHODCALLTYPE
    SetMultithreadProtected(
        _In_ BOOL bMTProtect) override;

    BOOL
    STDMETHODCALLTYPE
    GetMultithreadProtected(VOID) override;

private:
    Owner& Parent;
    RxDevice& Device;
};

class RxImmediateContext final : public ID3D11DeviceContext2
{
public:
    explicit RxImmediateContext(
        _In_ RxDevice& Owner)
        : Device(Owner),
          VideoContext(Owner),
          Multithread(*this, Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "context.inl"

    /* Binds the device defaults, as a new context starts out */
    VOID
    LoadPipelineDefaults();

    /* Sends the driver null blend, depth stencil and rasterizer states as the device goes away */
    VOID
    EmptyDriverStates();

    /* Drops every binding; the device does this before it goes away */
    VOID
    ReleaseBindings();

    /*
     * Each Send call hands the driver what is bound right now. The setters use them,
     * and so do the core layer callbacks a UMD calls when it wants state again.
     * A count of UINT_MAX means up to the highest slot ever bound.
     */
    VOID
    SendShader(
        _In_ RxStage Stage);

    VOID
    SendConstantBuffers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count);

    VOID
    SendShaderResources(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count);

    VOID
    SendSamplers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count);

    VOID
    SendInputLayout();

    VOID
    SendVertexBuffers(
        _In_ UINT Start,
        _In_ UINT Count);

    VOID
    SendIndexBuffer();

    VOID
    SendTopology();

    VOID
    SendRenderTargets(
        _In_ UINT ClearSlots);

    VOID
    SendBlendState();

    VOID
    SendDepthStencilState();

    VOID
    SendRasterizerState();

    VOID
    SendViewports(
        _In_ UINT ClearViewports);

    VOID
    SendScissorRects(
        _In_ UINT ClearRects);

    VOID
    SendComputeUavs(
        _In_ UINT Start,
        _In_ UINT Count);

    VOID
    SendStreamTargets(
        _In_ UINT ClearTargets);

private:
    /* Targets and output merger UAVs go down together; Counts NULL keeps every hidden counter */
    VOID
    SendOutputs(
        _In_ UINT ClearSlots,
        _In_ UINT UavRangeStart,
        _In_ UINT UavRangeSize,
        _In_reads_opt_(RxUavSlotCount) const UINT* Counts);

    /*
     * Both output merger setters land here. Kept targets are cut to TargetCount, kept UAVs
     * stay; UavStart is already resolved, and UavCounts lines up with Uavs.
     */
    VOID
    SetOutputs(
        _In_ BOOLEAN KeepTargets,
        _In_ UINT TargetCount,
        _In_reads_opt_(TargetCount) ID3D11RenderTargetView* const* Targets,
        _In_opt_ ID3D11DepthStencilView* DepthStencil,
        _In_ BOOLEAN KeepUavs,
        _In_ UINT UavStart,
        _In_ UINT UavCount,
        _In_reads_opt_(UavCount) ID3D11UnorderedAccessView* const* Uavs,
        _In_reads_opt_(UavCount) const UINT* UavCounts);

    /* UAV slots the feature level allows */
    UINT
    GetUavSlotCount() const;

    /*
     * A resource about to be written: drop it from every read binding, let other kinds
     * of output that hold it go, and count the write.
     */
    VOID
    NoteOutput(
        _In_ DdiResource* Resource,
        _In_ RxOutput Kind);

    /* Each lets go of one kind of output over a resource and tells the driver */
    VOID
    ReleaseMergerOutputsOf(
        _In_ const DdiResource* Resource);

    VOID
    ReleaseComputeUavsOf(
        _In_ const DdiResource* Resource,
        _In_ UINT KeepStart,
        _In_ UINT KeepCount);

    VOID
    ReleaseStreamTargetsOf(
        _In_ const DdiResource* Resource);

    VOID
    SetShader(
        _In_ RxStage Stage,
        _In_opt_ RxChildLifetime* Shader,
        _In_opt_ ID3D11DeviceChild* Object,
        _In_ D3D10DDI_HSHADER Handle);

    /* Getters hand out new references to what is bound; empty slots come back NULL */
    VOID
    GetShader(
        _In_ RxStage Stage,
        _In_ REFIID Interface,
        _Outptr_result_maybenull_ VOID** Shader,
        _Out_opt_ UINT* ClassInstanceCount);

    VOID
    GetConstantBuffers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _Out_writes_opt_(Count) ID3D11Buffer** Buffers);

    VOID
    GetShaderResources(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _Out_writes_opt_(Count) ID3D11ShaderResourceView** Views);

    VOID
    GetSamplers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _Out_writes_opt_(Count) ID3D11SamplerState** Samplers);

    VOID
    SetConstantBuffers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_opt_(Count) ID3D11Buffer* const* Buffers);

    VOID
    SetShaderResources(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_opt_(Count) ID3D11ShaderResourceView* const* Views);

    VOID
    SetSamplers(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_opt_(Count) ID3D11SamplerState* const* Samplers);

    /* A resource about to be written can not stay bound for reading */
    VOID
    UnbindInputsOf(
        _In_ const DdiResource* Resource);

    BOOLEAN
    IsBoundForOutput(
        _In_ const DdiResource* Resource) const;

    /* Sends every current binding to the driver, after the whole pipeline was replaced */
    VOID
    ResendPipeline();

    /* Lets go of the active context state object; the device does this before it goes away */
    VOID
    ReleaseContextState();

    /* Shared by the Discard* methods */
    VOID
    Discard(
        _In_ D3D11DDI_HANDLETYPE Type,
        _In_ VOID* Handle,
        _In_reads_opt_(NumRects) const D3D11_RECT* Rects,
        _In_ UINT NumRects);

    VOID
    SetConstantBufferRanges(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _In_reads_opt_(Count) ID3D11Buffer* const* Buffers,
        _In_reads_opt_(Count) const UINT* FirstConstants,
        _In_reads_opt_(Count) const UINT* NumConstants);

    VOID
    GetConstantBufferRanges(
        _In_ RxStage Stage,
        _In_ UINT Start,
        _In_ UINT Count,
        _Out_writes_opt_(Count) ID3D11Buffer** Buffers,
        _Out_writes_opt_(Count) UINT* FirstConstants,
        _Out_writes_opt_(Count) UINT* NumConstants);

    RxDevice& Device;
    RxVideoContext VideoContext;
    RxMultithread<ID3D11Multithread, RxImmediateContext> Multithread;
    RxPrivateDataStore PrivateData;
    RxPipelineState Pipeline;

    /* The state object SwapDeviceContextState last made active, held by an internal reference */
    RxChildLifetime* ActiveState = nullptr;
};

/* A tear-off of RxDevice: same lifetime, QueryInterface goes back to the device */
class RxVideoDevice final : public ID3D11VideoDevice
{
public:
    explicit RxVideoDevice(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "videodevice.inl"

private:
    RxDevice& Device;
};

/* The device's IDXGIDevice face; a tear-off sharing the device's lifetime and private data */
class RxDxgiDevice final : public IDXGIDevice3
{
public:
    explicit RxDxgiDevice(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "dxgidevice.inl"

    UINT
    GetFrameLatency() const
    {
        return MaximumFrameLatency;
    }

private:
    RxDevice& Device;
    UINT MaximumFrameLatency = 3;
    INT GpuThreadPriority = 0;
};

/* The device as the ReactX dxgi.dll sees it when it builds a swapchain */
class RxPresentBridge final : public IRxPresentDevice
{
public:
    explicit RxPresentBridge(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    GetAdapterLuid(
        _Out_ LUID* Luid) override;

    HRESULT
    STDMETHODCALLTYPE
    CreatePresentBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) override;

    VOID
    STDMETHODCALLTYPE
    DetachPresentBuffer(
        _In_ IUnknown* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    OpenPresentTarget(
        _In_ HANDLE SharedHandle,
        _COM_Outptr_ IUnknown** Target) override;

    HRESULT
    STDMETHODCALLTYPE
    SubmitPresent(
        _In_ IUnknown* Buffer,
        _In_opt_ IUnknown* Target,
        _In_ RxPresentContext* Context) override;

    HRESULT
    STDMETHODCALLTYPE
    ShareBuffer(
        _In_ IUnknown* Buffer,
        _Out_ HANDLE* SharedHandle) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateFlipFence(
        _Out_ UINT* Fence,
        _Out_ HANDLE* SharedHandle) override;

    VOID
    STDMETHODCALLTYPE
    DestroyFlipFence(
        _In_ UINT Fence) override;

    HANDLE
    STDMETHODCALLTYPE
    GetPresentLimitSemaphore() override;

    HRESULT
    STDMETHODCALLTYPE
    PresentFlip(
        _In_ IUnknown* Buffer,
        _In_ RxPresentContext* Context) override;

    HRESULT
    STDMETHODCALLTYPE
    RotateBuffers(
        _In_reads_(Count) IUnknown* const* Buffers,
        _In_ UINT Count) override;

    HRESULT
    STDMETHODCALLTYPE
    CopyBufferRegions(
        _In_ IUnknown* Destination,
        _In_ IUnknown* Source,
        _In_reads_(Count) const RECT* Rects,
        _In_ UINT Count,
        _In_ POINT Offset) override;

    D3DKMT_HANDLE
    STDMETHODCALLTYPE
    GetKernelDevice() override;

    HRESULT
    STDMETHODCALLTYPE
    CreatePrimaryBuffer(
        _In_ const DXGI_SWAP_CHAIN_DESC1* Desc,
        _In_ const DXGI_DDI_PRIMARY_DESC* PrimaryDesc,
        _In_ IUnknown* Parent,
        _COM_Outptr_ IUnknown** Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    SetDisplaySurface(
        _In_ IUnknown* Buffer) override;

    HRESULT
    STDMETHODCALLTYPE
    PresentPrimary(
        _In_ IUnknown* Buffer,
        _In_ UINT FlipInterval,
        _In_ RxPresentContext* Context) override;

    ~RxPresentBridge();

private:
    RxDevice& Device;
    SRWLOCK SemaphoreLock = SRWLOCK_INIT;
    HANDLE LimitSemaphore = NULL;
};

/* ID3D11On12Device2 of a device on the D3D11 on 12 layer, reached through QueryInterface */
class RxOn12Face final : public ID3D11On12Device2
{
public:
    explicit RxOn12Face(
        _In_ RxDevice& Owner)
        : Device(Owner)
    {
    }

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

    HRESULT
    STDMETHODCALLTYPE
    CreateWrappedResource(
        _In_ IUnknown* pResource12,
        _In_ const D3D11_RESOURCE_FLAGS* pFlags11,
        _In_ D3D12_RESOURCE_STATES InState,
        _In_ D3D12_RESOURCE_STATES OutState,
        _In_ REFIID riid,
        _COM_Outptr_opt_ void** ppResource11) override;

    VOID
    STDMETHODCALLTYPE
    ReleaseWrappedResources(
        _In_reads_(NumResources) ID3D11Resource* const* ppResources,
        _In_ UINT NumResources) override;

    VOID
    STDMETHODCALLTYPE
    AcquireWrappedResources(
        _In_reads_(NumResources) ID3D11Resource* const* ppResources,
        _In_ UINT NumResources) override;

    HRESULT
    STDMETHODCALLTYPE
    GetD3D12Device(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvDevice) override;

    HRESULT
    STDMETHODCALLTYPE
    UnwrapUnderlyingResource(
        _In_ ID3D11Resource* pResource11,
        _In_ ID3D12CommandQueue* pCommandQueue,
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvResource12) override;

    HRESULT
    STDMETHODCALLTYPE
    ReturnUnderlyingResource(
        _In_ ID3D11Resource* pResource11,
        _In_ UINT NumSync,
        _In_reads_(NumSync) UINT64* pSignalValues,
        _In_reads_(NumSync) ID3D12Fence** ppFences) override;

private:
    RxDevice& Device;
};

class RxDevice final : public ID3D11Device2, public RxKernelDevice
{
public:
    static
    HRESULT
    Create(
        _In_ UmdAdapter* Adapter,
        _In_ D3D_FEATURE_LEVEL FeatureLevel,
        _In_ D3D11DDI_3DPIPELINELEVEL PipelineLevel,
        _In_ UINT CreationFlags,
        _Outptr_ RxDevice** Device);

    HRESULT
    STDMETHODCALLTYPE
    QueryInterface(
        _In_ REFIID riid,
        _COM_Outptr_ void** ppvObject) override;

    ULONG
    STDMETHODCALLTYPE
    AddRef(VOID) override;

    ULONG
    STDMETHODCALLTYPE
    Release(VOID) override;

#include "device.inl"

    RxImmediateContext&
    GetContext()
    {
        return Context;
    }

    /* The layer's interface on its device; null when the driver is not the D3D11 on 12 layer */
    IOn12DdiDevice*
    GetLayerDevice() const
    {
        return LayerDevice;
    }

    ID3D12Device5*
    GetLayerDevice12() const
    {
        return LayerDevice12;
    }

    /* Used by the DDI callbacks in ddicallbacks.cpp */
    UmdAdapter&
    GetAdapter()
    {
        return *Adapter;
    }

    D3DKMT_HANDLE
    GetKmDevice() const
    {
        return KmDevice;
    }

    D3D10DDI_HDEVICE
    GetDriverDevice() const
    {
        return DriverDevice;
    }

    const D3DWDDM1_3DDI_DEVICEFUNCS&
    GetDeviceFuncs() const
    {
        return reinterpret_cast<const D3DWDDM1_3DDI_DEVICEFUNCS&>(DeviceFuncs);
    }

    /* The table as WDDM 2.0 types it, where some 1.3 entries take newer arguments */
    const D3DWDDM2_0DDI_DEVICEFUNCS&
    GetWddm2DeviceFuncs() const
    {
        return reinterpret_cast<const D3DWDDM2_0DDI_DEVICEFUNCS&>(DeviceFuncs);
    }

    BOOLEAN
    IsWddm2Driver();

    VOID
    FlushDriver(
        _In_ UINT Flags);

    const DXGI1_6_1_DDI_BASE_FUNCTIONS&
    GetDxgiFuncs() const
    {
        return DxgiFuncs;
    }

    /* Fetches the UMD's video table on first use; fails when the driver has none */
    HRESULT
    EnsureVideoFuncs();

    const D3D11_1DDI_VIDEODEVICEFUNCS&
    GetVideoFuncs() const
    {
        return VideoFuncs;
    }

    VOID
    SetDriverError(
        _In_ HRESULT Error);

    HRESULT
    TakeDriverError();

    RxPrivateDataStore&
    GetPrivateDataStore()
    {
        return PrivateData;
    }

    const RxDxgiDevice&
    GetDxgiDevice() const
    {
        return DxgiDevice;
    }

    /* The DXGI adapter this device reports: the one it was created on, or found on first use */
    HRESULT
    GetDxgiAdapter(
        _COM_Outptr_ IDXGIAdapter1** Result);

    VOID
    SetDxgiAdapter(
        _In_ IDXGIAdapter* Reported);

    /*
     * A resource's D3D object is going away: its record is freed now if the driver is done
     * with it, otherwise kept until the driver deallocates or the device is destroyed.
     */
    VOID
    RetireRuntimeResource(
        _In_ RxRuntimeResource* Resource);

    /* The driver deallocated a record's kernel resource */
    VOID
    ReleaseRuntimeResource(
        _In_ RxRuntimeResource* Resource);

    /* What a format can do here, as CheckFormatSupport reports it; E_FAIL when it does not exist */
    HRESULT
    GetFormatSupport(
        _In_ DXGI_FORMAT Format,
        _Out_ UINT* Support);

    /* The D3D11_FORMAT_SUPPORT2 bits for a format, E_FAIL when there are none */
    HRESULT
    GetFormatSupport2(
        _In_ DXGI_FORMAT Format,
        _Out_ UINT* Support);

    /* Kernel contexts the UMD created, so scheduling priority can reach all of them */
    VOID
    TrackContext(
        _In_ D3DKMT_HANDLE KmContext);

    /* For the shared kernel callbacks */
    D3DKMT_HANDLE
    KernelDevice() const override
    {
        return KmDevice;
    }

    D3DKMT_HANDLE
    KernelAdapter() const override
    {
        return Adapter->GetKmtHandle();
    }

    VOID
    NoteKernelContext(
        _In_ D3DKMT_HANDLE Context) override
    {
        TrackContext(Context);
    }

    VOID
    ForgetKernelContext(
        _In_ D3DKMT_HANDLE Context) override
    {
        UntrackContext(Context);
    }

    VOID
    NoteKernelQueue(
        _In_ D3DKMT_HANDLE KmContext,
        _In_ D3DKMT_HANDLE KmQueue) override;

    VOID
    ForgetKernelQueue(
        _In_ D3DKMT_HANDLE KmQueue) override;

    /* Makes every context of this device wait on the GPU until a fence reaches FenceValue */
    HRESULT
    WaitForFenceOnGpu(
        _In_ D3DKMT_HANDLE SyncObject,
        _In_ UINT64 FenceValue);

    /* Signals a fence from every context once the work already submitted there is done; S_FALSE without contexts */
    HRESULT
    SignalFenceOnGpu(
        _In_ D3DKMT_HANDLE SyncObject,
        _In_ UINT64 FenceValue);

    D3DKMT_CLIENTHINT
    KernelClientHint() const override
    {
        return D3DKMT_CLIENTHINT_DX11;
    }

    D3DKMT_HANDLE
    KernelResourceOf(
        _In_opt_ HANDLE RuntimeResource) override
    {
        return RuntimeResource ? static_cast<RxRuntimeResource*>(RuntimeResource)->KmResource : 0;
    }

    VOID
    RetireKernelResource(
        _In_ HANDLE RuntimeResource) override
    {
        ReleaseRuntimeResource(static_cast<RxRuntimeResource*>(RuntimeResource));
    }

    VOID
    UntrackContext(
        _In_ D3DKMT_HANDLE KmContext);

    NTSTATUS
    SetContextPriority(
        _In_ INT Priority);

    /* The objects a NULL binding stands for */
    struct DefaultStates
    {
        DefaultStates();
        ~DefaultStates();
        DefaultStates& operator=(const DefaultStates&) = default;

        RxBound<RxStateObject<RxBlendTraits>> Blend;
        RxBound<RxStateObject<RxDepthStencilTraits>> DepthStencil;
        RxBound<RxStateObject<RxRasterizerTraits>> Rasterizer;
        RxBound<RxStateObject<RxSamplerTraits>> Sampler;
    };

    const DefaultStates&
    GetDefaults() const
    {
        return Defaults;
    }

private:
    RxDevice(
        _In_ UmdAdapter* Adapter,
        _In_ D3D_FEATURE_LEVEL FeatureLevel,
        _In_ UINT CreationFlags);

    ~RxDevice();

    HRESULT
    Initialize(
        _In_ D3D11DDI_3DPIPELINELEVEL PipelineLevel);

    HRESULT
    BuildStockStates();

    /* Takes the D3D12 objects behind the layer and asks for what a new layer device caches */
    HRESULT
    AttachLayer();

    /* Asks the driver once for every format the runtime caches, with each multisample count */
    VOID
    WarmFormatCaches();

    /* The driver's own caps for a format, asked once */
    UINT
    GetDriverFormatCaps(
        _In_ DXGI_FORMAT Format);

    LONG RefCount = 1;
    UmdAdapter* Adapter;
    D3D_FEATURE_LEVEL FeatureLevel;
    UINT CreationFlags;
    UINT ExceptionMode = 0;
    RxImmediateContext Context;
    RxVideoDevice VideoDevice;
    RxDxgiDevice DxgiDevice;
    RxPresentBridge PresentBridge;
    RxMultithread<ID3D10Multithread, RxDevice> Multithread;
    RxCompositionPartner CompositionPartner;
    RxD3D10Device D3D10Device;
    RxOn12Face On12Face;

    SRWLOCK DxgiLock = SRWLOCK_INIT;
    IDXGIAdapter1* DxgiAdapter = nullptr;

    /* Driver answers per format, asked on first use */
    SRWLOCK FormatLock = SRWLOCK_INIT;
    UINT64 FormatAsked[4] = {};
    UINT FormatDriverCaps[256] = {};

    SRWLOCK RuntimeResourceLock = SRWLOCK_INIT;
    std::vector<RxRuntimeResource*> OrphanedResources;

    /* Hardware queues belong to a context, and GPU waits go to them instead of to it */
    struct KernelQueue
    {
        D3DKMT_HANDLE Context;
        D3DKMT_HANDLE Queue;
    };

    SRWLOCK ContextLock = SRWLOCK_INIT;
    std::vector<D3DKMT_HANDLE> KmContexts;
    std::vector<KernelQueue> KmQueues;

    DefaultStates Defaults;

    D3DKMT_HANDLE KmDevice = 0;
    PVOID DriverMemory = nullptr;
    D3D10DDI_HDEVICE DriverDevice = {};
    BOOLEAN IsDriverDeviceCreated = FALSE;

    /* Sized for WDDM 2.6, whose table extends the 2.0 one; older drivers fill only their part */
    D3DWDDM2_6DDI_DEVICEFUNCS DeviceFuncs = {};
    DXGI1_6_1_DDI_BASE_FUNCTIONS DxgiFuncs = {};
    PFND3D10DDI_RETRIEVESUBOBJECT RetrieveSubObject = nullptr;

    /* Only for devices on the D3D11 on 12 layer */
    IOn12DdiDevice* LayerDevice = nullptr;
    ID3D12Device5* LayerDevice12 = nullptr;
    IUnknown* LayerPrivate = nullptr;
    IUnknown* LayerQueue = nullptr;

    SRWLOCK VideoLock = SRWLOCK_INIT;
    BOOLEAN IsVideoQueried = FALSE;
    HRESULT VideoStatus = E_NOINTERFACE;
    D3D11_1DDI_VIDEODEVICEFUNCS VideoFuncs = {};

    RxPrivateDataStore PrivateData;

public:
    /* Made through d3d10.dll or d3d10_1.dll: the device and its objects also answer as Direct3D 10 */
    BOOLEAN
    IsD3D10() const
    {
        return (CreationFlags & RxD3D10DeviceFlag) != 0;
    }

    SRWLOCK D3D10FaceLock = SRWLOCK_INIT;

    /* The lock ID3D10Multithread and ID3D11Multithread hand out */
    CRITICAL_SECTION ThreadLock;
    BOOL IsThreadProtected = FALSE;
};

/* Every creation flag the runtime accepts */
inline constexpr UINT RxKnownCreateFlags =
    D3D11_CREATE_DEVICE_SINGLETHREADED |
    D3D11_CREATE_DEVICE_DEBUG |
    D3D11_CREATE_DEVICE_SWITCH_TO_REF |
    D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
    D3D11_CREATE_DEVICE_BGRA_SUPPORT |
    D3D11_CREATE_DEVICE_DEBUGGABLE |
    D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY |
    D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT |
    D3D11_CREATE_DEVICE_VIDEO_SUPPORT |
    RxD3D10DeviceFlag;

/* Private interface id: QueryInterface with it yields the RxDevice itself, without a reference */
DEFINE_GUID(IID_RxDevice, 0x3b6e8a52, 0x71d4, 0x4c9a, 0x9e, 0x0f, 0x5a, 0x21, 0xc7, 0x84, 0xd3, 0x6b);

/* Finds the RxDevice behind any interface of a device created by this runtime */
_Ret_maybenull_
RxDevice*
RxGetDevice(
    _In_opt_ IUnknown* Unknown);

/* Maps D3D11 bind and CPU access flags to their DDI encodings */
UINT
RxDdiBindFlags(
    _In_ UINT BindFlags);

UINT
RxDdiCpuAccessFlags(
    _In_ UINT CpuAccessFlags);

UINT
RxDdiMiscFlags(
    _In_ UINT MiscFlags);

/* Turns a pending driver error into the HRESULT the API documents */
HRESULT
RxApiResult(
    _In_ HRESULT DriverResult);

const D3DDDI_DEVICECALLBACKS*
RxGetKernelCallbacks(VOID);

/* Laid out as the WDDM 2.6 table; older drivers read only the entries their version has */
const D3DWDDM2_6DDI_CORELAYER_DEVICECALLBACKS*
RxGetCoreLayerCallbacks(VOID);

DXGI_DDI_BASE_CALLBACKS*
RxGetDxgiCallbacks(VOID);
