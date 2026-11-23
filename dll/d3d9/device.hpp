/*
 * PROJECT:     ReactX Direct3D 9
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     The device, its swap chains and the surfaces they own
 * COPYRIGHT:   Copyright 2026 Justin Miller <justinmiller100@gmail.com>
 */

#pragma once

#include "d3d9.hpp"
#include "kmcallbacks.hpp"

class D9Device;
class D9SwapChain;

/* The most render targets a device binds at once */
static constexpr UINT D9MaxRenderTargets = 4;

/* Render states a device keeps a value for */
static constexpr UINT D9RenderStateCount = 256;

/* Kernel contexts a device tracks for its driver */
static constexpr UINT D9MaxKernelContexts = 16;

/* A resource the driver made; its address is the runtime handle the driver passes back */
struct D9Resource
{
    HANDLE DriverResource;
    D3DKMT_HANDLE KernelResource;
    D3DDDIFORMAT Format;
    D3DDDI_POOL Pool;
    D3DDDI_RESOURCEFLAGS Flags;
    UINT Width;
    UINT Height;
};

/* Objects a device owns hold the device while the application holds them */
class D9Child
{
public:
    explicit
    D9Child(
        _In_ D9Device* Owner)
        : Device(Owner)
    {
    }

    D9Device*
    Owner() const
    {
        return Device;
    }

protected:
    ULONG
    AddRefChild(VOID);

    ULONG
    ReleaseChild(VOID);

    D9Device* Device;
    LONG RefCount = 0;
};

/* A surface the device made: back buffers, the automatic depth buffer and later render targets */
class D9Surface final : public IDirect3DSurface9, public D9Child
{
public:
    D9Surface(
        _In_ D9Device* Owner,
        _In_opt_ IUnknown* Container,
        _In_ const D3DSURFACE_DESC* Desc,
        _In_ BOOLEAN Lockable);

    ~D9Surface();

    D9Surface(const D9Surface&) = delete;
    D9Surface& operator=(const D9Surface&) = delete;

    HRESULT
    Create(VOID);

    const D3DSURFACE_DESC&
    Describe() const
    {
        return Desc;
    }

    D9Resource*
    DriverResource()
    {
        return &Resource;
    }

    /* IUnknown */
    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    /* IDirect3DResource9 */
    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override;

    STDMETHOD(SetPrivateData)(
        _In_ REFGUID Guid,
        _In_reads_bytes_opt_(Size) const VOID* Data,
        _In_ DWORD Size,
        _In_ DWORD Flags) override;

    STDMETHOD(GetPrivateData)(
        _In_ REFGUID Guid,
        _Out_writes_bytes_opt_(*Size) VOID* Data,
        _Inout_ DWORD* Size) override;

    STDMETHOD(FreePrivateData)(
        _In_ REFGUID Guid) override;

    STDMETHOD_(DWORD, SetPriority)(
        _In_ DWORD Priority) override;

    STDMETHOD_(DWORD, GetPriority)() override;

    STDMETHOD_(VOID, PreLoad)() override;

    STDMETHOD_(D3DRESOURCETYPE, GetType)() override;

    /* IDirect3DSurface9 */
    STDMETHOD(GetContainer)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Result) override;

    STDMETHOD(GetDesc)(
        _Out_ D3DSURFACE_DESC* Result) override;

    STDMETHOD(LockRect)(
        _Out_ D3DLOCKED_RECT* Locked,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags) override;

    STDMETHOD(UnlockRect)() override;

    STDMETHOD(GetDC)(
        _Out_ HDC* Dc) override;

    STDMETHOD(ReleaseDC)(
        _In_ HDC Dc) override;

private:
    IUnknown* Container;
    D3DSURFACE_DESC Desc;
    D9Resource Resource = {};
    BOOLEAN Lockable;
    BOOLEAN IsLocked = FALSE;
    DWORD ResourcePriority = 0;
};

/* A swap chain: the back buffers of one window and how they reach it */
class D9SwapChain final : public IDirect3DSwapChain9Ex, public D9Child
{
public:
    D9SwapChain(
        _In_ D9Device* Owner,
        _In_ UINT Position);

    ~D9SwapChain();

    D9SwapChain(const D9SwapChain&) = delete;
    D9SwapChain& operator=(const D9SwapChain&) = delete;

    HRESULT
    Reset(
        _Inout_ D3DPRESENT_PARAMETERS* Parameters);

    D9Surface*
    BackBuffer(
        _In_ UINT Buffer) const
    {
        return Buffer < BackBufferCount ? BackBuffers[Buffer] : nullptr;
    }

    const D3DPRESENT_PARAMETERS&
    PresentParameters() const
    {
        return Parameters;
    }

    HWND
    Window() const
    {
        return Parameters.hDeviceWindow != NULL ? Parameters.hDeviceWindow : FocusWindow;
    }

    HRESULT
    PresentWindowed(
        _In_opt_ const RECT* SourceRect,
        _In_opt_ const RECT* DestRect,
        _In_opt_ HWND WindowOverride,
        _In_opt_ const RGNDATA* DirtyRegion,
        _In_ DWORD Flags);

    /* IUnknown */
    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    /* IDirect3DSwapChain9 */
    STDMETHOD(Present)(
        _In_opt_ const RECT* SourceRect,
        _In_opt_ const RECT* DestRect,
        _In_opt_ HWND WindowOverride,
        _In_opt_ const RGNDATA* DirtyRegion,
        _In_ DWORD Flags) override;

    STDMETHOD(GetFrontBufferData)(
        _In_ IDirect3DSurface9* Destination) override;

    STDMETHOD(GetBackBuffer)(
        _In_ UINT Index,
        _In_ D3DBACKBUFFER_TYPE Type,
        _COM_Outptr_ IDirect3DSurface9** Result) override;

    STDMETHOD(GetRasterStatus)(
        _Out_ D3DRASTER_STATUS* Status) override;

    STDMETHOD(GetDisplayMode)(
        _Out_ D3DDISPLAYMODE* Mode) override;

    STDMETHOD(GetDevice)(
        _Out_ IDirect3DDevice9** Result) override;

    STDMETHOD(GetPresentParameters)(
        _Out_ D3DPRESENT_PARAMETERS* Result) override;

    /* IDirect3DSwapChain9Ex */
    STDMETHOD(GetLastPresentCount)(
        _Out_ UINT* Count) override;

    STDMETHOD(GetPresentStats)(
        _Out_ D3DPRESENTSTATS* Stats) override;

    STDMETHOD(GetDisplayModeEx)(
        _Out_ D3DDISPLAYMODEEX* Mode,
        _Out_opt_ D3DDISPLAYROTATION* Rotation) override;

private:
    VOID
    ReleaseBuffers(VOID);

    UINT Index;
    HWND FocusWindow;
    D3DPRESENT_PARAMETERS Parameters = {};
    D9Surface* BackBuffers[D3DPRESENT_BACK_BUFFERS_MAX_EX] = {};
    UINT BackBufferCount = 0;
};

/* present.cpp */
HRESULT
D9CheckOcclusion(
    _In_ HWND Window);

/* The IDirect3DDevice9Ex object CreateDevice and CreateDeviceEx hand out */
class D9Device final : public IDirect3DDevice9Ex, public RxKernelDevice
{
public:
    D9Device(
        _In_ D9Enum* Enum,
        _In_ const D9Adapter* Adapter,
        _In_ D3DDEVTYPE DeviceType,
        _In_opt_ HWND FocusWindow,
        _In_ DWORD BehaviorFlags,
        _In_ BOOLEAN IsEx);

    ~D9Device();

    D9Device(const D9Device&) = delete;
    D9Device& operator=(const D9Device&) = delete;

    HRESULT
    Initialize(
        _Inout_ D3DPRESENT_PARAMETERS* Parameters);

    /* What the swap chain and surfaces need from their device */
    const D9Adapter*
    AdapterInfo() const
    {
        return Adapter;
    }

    HWND
    Focus() const
    {
        return FocusWindow;
    }

    BOOLEAN
    IsExtended() const
    {
        return IsEx;
    }

    D9Enum*
    Owner() const
    {
        return Enum;
    }

    HRESULT
    CreateDriverSurface(
        _In_ const D3DSURFACE_DESC* Desc,
        _In_ BOOLEAN Lockable,
        _Out_ D9Resource* Resource);

    VOID
    DestroyDriverSurface(
        _Inout_ D9Resource* Resource);

    HRESULT
    LockDriverSurface(
        _In_ D9Resource* Resource,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags,
        _Out_ D3DLOCKED_RECT* Locked);

    HRESULT
    UnlockDriverSurface(
        _In_ D9Resource* Resource);

    HRESULT
    PresentToWindow(
        _In_ D9Resource* Source,
        _In_ HWND Window,
        _In_ const RECT* From,
        _In_ const RECT* To);

    VOID
    ReleaseWindowSurface(VOID);

    HRESULT
    PresentCallback(
        _Inout_ D3DDDICB_PRESENT* Args);

    HRESULT
    NotePresentStatus(
        _In_ NTSTATUS Status);

    HRESULT
    SubmitPresentCallback(
        _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args);

    HRESULT
    AllocateCallback(
        _Inout_ D3DDDICB_ALLOCATE* Args);

    HRESULT
    DeallocateCallback(
        _In_ const D3DDDICB_DEALLOCATE* Args);

    D3DKMT_HANDLE
    KernelAdapterHandle() const
    {
        return KernelAdapter();
    }

    UINT
    DriverInterfaceVersion() const
    {
        return DriverVersion;
    }

    /* RxKernelDevice */
    D3DKMT_HANDLE
    KernelDevice() const override;

    D3DKMT_HANDLE
    KernelAdapter() const override;

    D3DKMT_CLIENTHINT
    KernelClientHint() const override;

    D3DKMT_HANDLE
    KernelResourceOf(
        _In_opt_ HANDLE RuntimeResource) override;

    VOID
    RetireKernelResource(
        _In_ HANDLE RuntimeResource) override;

    VOID
    NoteKernelContext(
        _In_ D3DKMT_HANDLE Context) override;

    VOID
    ForgetKernelContext(
        _In_ D3DKMT_HANDLE Context) override;

    /* IDirect3DDevice9 and IDirect3DDevice9Ex */
    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    STDMETHOD(TestCooperativeLevel)() override;

    STDMETHOD_(UINT, GetAvailableTextureMem)() override;

    STDMETHOD(EvictManagedResources)() override;

    STDMETHOD(GetDirect3D)(
        _Out_ IDirect3D9** D3D9) override;

    STDMETHOD(GetDeviceCaps)(
        _Out_ D3DCAPS9* Caps) override;

    STDMETHOD(GetDisplayMode)(
        _In_ UINT SwapChain,
        _Out_ D3DDISPLAYMODE* Mode) override;

    STDMETHOD(GetCreationParameters)(
        _Out_ D3DDEVICE_CREATION_PARAMETERS* Parameters) override;

    STDMETHOD(SetCursorProperties)(
        _In_ UINT XHotSpot,
        _In_ UINT YHotSpot,
        _In_opt_ IDirect3DSurface9* CursorBitmap) override;

    STDMETHOD_(VOID, SetCursorPosition)(
        _In_ INT X,
        _In_ INT Y,
        _In_ DWORD Flags) override;

    STDMETHOD_(BOOL, ShowCursor)(
        _In_ BOOL Show) override;

    STDMETHOD(CreateAdditionalSwapChain)(
        _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters,
        _Out_ IDirect3DSwapChain9** SwapChain) override;

    STDMETHOD(GetSwapChain)(
        _In_ UINT SwapChain,
        _Out_ IDirect3DSwapChain9** Result) override;

    STDMETHOD_(UINT, GetNumberOfSwapChains)() override;

    STDMETHOD(Reset)(
        _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters) override;

    STDMETHOD(Present)(
        _In_opt_ const RECT* SourceRect,
        _In_opt_ const RECT* DestRect,
        _In_opt_ HWND DestWindowOverride,
        _In_opt_ const RGNDATA* DirtyRegion) override;

    STDMETHOD(GetBackBuffer)(
        _In_ UINT SwapChain,
        _In_ UINT BackBuffer,
        _In_ D3DBACKBUFFER_TYPE Type,
        _Out_ IDirect3DSurface9** Result) override;

    STDMETHOD(GetRasterStatus)(
        _In_ UINT SwapChain,
        _Out_ D3DRASTER_STATUS* RasterStatus) override;

    STDMETHOD(SetDialogBoxMode)(
        _In_ BOOL EnableDialogs) override;

    STDMETHOD_(VOID, SetGammaRamp)(
        _In_ UINT SwapChain,
        _In_ DWORD Flags,
        _In_opt_ const D3DGAMMARAMP* Ramp) override;

    STDMETHOD_(VOID, GetGammaRamp)(
        _In_ UINT SwapChain,
        _Out_ D3DGAMMARAMP* Ramp) override;

    STDMETHOD(CreateTexture)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ UINT Levels,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DTexture9** Texture,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateVolumeTexture)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ UINT Depth,
        _In_ UINT Levels,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DVolumeTexture9** VolumeTexture,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateCubeTexture)(
        _In_ UINT EdgeLength,
        _In_ UINT Levels,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DCubeTexture9** CubeTexture,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateVertexBuffer)(
        _In_ UINT Length,
        _In_ DWORD Usage,
        _In_ DWORD FVF,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DVertexBuffer9** VertexBuffer,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateIndexBuffer)(
        _In_ UINT Length,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DIndexBuffer9** IndexBuffer,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateRenderTarget)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DMULTISAMPLE_TYPE MultiSample,
        _In_ DWORD MultisampleQuality,
        _In_ BOOL Lockable,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(CreateDepthStencilSurface)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DMULTISAMPLE_TYPE MultiSample,
        _In_ DWORD MultisampleQuality,
        _In_ BOOL Discard,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(UpdateSurface)(
        _In_opt_ IDirect3DSurface9* SourceSurface,
        _In_opt_ const RECT* SourceRect,
        _In_opt_ IDirect3DSurface9* DestinationSurface,
        _In_opt_ const POINT* DestPoint) override;

    STDMETHOD(UpdateTexture)(
        _In_opt_ IDirect3DBaseTexture9* SourceTexture,
        _In_opt_ IDirect3DBaseTexture9* DestinationTexture) override;

    STDMETHOD(GetRenderTargetData)(
        _Out_ IDirect3DSurface9* RenderTarget,
        _Out_ IDirect3DSurface9* DestSurface) override;

    STDMETHOD(GetFrontBufferData)(
        _In_ UINT SwapChain,
        _Out_ IDirect3DSurface9* DestSurface) override;

    STDMETHOD(StretchRect)(
        _In_opt_ IDirect3DSurface9* SourceSurface,
        _In_opt_ const RECT* SourceRect,
        _In_opt_ IDirect3DSurface9* DestSurface,
        _In_opt_ const RECT* DestRect,
        _In_ D3DTEXTUREFILTERTYPE Filter) override;

    STDMETHOD(ColorFill)(
        _In_opt_ IDirect3DSurface9* Surface,
        _In_opt_ const RECT* Rect,
        _In_ D3DCOLOR Color) override;

    STDMETHOD(CreateOffscreenPlainSurface)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle) override;

    STDMETHOD(SetRenderTarget)(
        _In_ DWORD RenderTargetIndex,
        _In_opt_ IDirect3DSurface9* RenderTarget) override;

    STDMETHOD(GetRenderTarget)(
        _In_ DWORD RenderTargetIndex,
        _Out_ IDirect3DSurface9** RenderTarget) override;

    STDMETHOD(SetDepthStencilSurface)(
        _In_opt_ IDirect3DSurface9* NewZStencil) override;

    STDMETHOD(GetDepthStencilSurface)(
        _Out_ IDirect3DSurface9** ZStencilSurface) override;

    STDMETHOD(BeginScene)() override;

    STDMETHOD(EndScene)() override;

    STDMETHOD(Clear)(
        _In_ DWORD Count,
        _In_opt_ const D3DRECT* Rects,
        _In_ DWORD Flags,
        _In_ D3DCOLOR Color,
        _In_ FLOAT Z,
        _In_ DWORD Stencil) override;

    STDMETHOD(SetTransform)(
        _In_ D3DTRANSFORMSTATETYPE State,
        _In_opt_ const D3DMATRIX* Matrix) override;

    STDMETHOD(GetTransform)(
        _In_ D3DTRANSFORMSTATETYPE State,
        _Out_ D3DMATRIX* Matrix) override;

    STDMETHOD(MultiplyTransform)(
        _In_ D3DTRANSFORMSTATETYPE State,
        _In_opt_ const D3DMATRIX* Matrix) override;

    STDMETHOD(SetViewport)(
        _In_opt_ const D3DVIEWPORT9* Viewport) override;

    STDMETHOD(GetViewport)(
        _Out_ D3DVIEWPORT9* Viewport) override;

    STDMETHOD(SetMaterial)(
        _In_opt_ const D3DMATERIAL9* Material) override;

    STDMETHOD(GetMaterial)(
        _Out_ D3DMATERIAL9* Material) override;

    STDMETHOD(SetLight)(
        _In_ DWORD Index,
        _In_opt_ const D3DLIGHT9* Light) override;

    STDMETHOD(GetLight)(
        _In_ DWORD Index,
        _Out_ D3DLIGHT9* Light) override;

    STDMETHOD(LightEnable)(
        _In_ DWORD Index,
        _In_ BOOL Enable) override;

    STDMETHOD(GetLightEnable)(
        _In_ DWORD Index,
        _Out_ BOOL* Enable) override;

    STDMETHOD(SetClipPlane)(
        _In_ DWORD Index,
        _In_opt_ const float* Plane) override;

    STDMETHOD(GetClipPlane)(
        _In_ DWORD Index,
        _Out_ float* Plane) override;

    STDMETHOD(SetRenderState)(
        _In_ D3DRENDERSTATETYPE State,
        _In_ DWORD Value) override;

    STDMETHOD(GetRenderState)(
        _In_ D3DRENDERSTATETYPE State,
        _Out_ DWORD* Value) override;

    STDMETHOD(CreateStateBlock)(
        _In_ D3DSTATEBLOCKTYPE Type,
        _Out_ IDirect3DStateBlock9** SB) override;

    STDMETHOD(BeginStateBlock)() override;

    STDMETHOD(EndStateBlock)(
        _In_ IDirect3DStateBlock9** SB) override;

    STDMETHOD(SetClipStatus)(
        _In_opt_ const D3DCLIPSTATUS9* ClipStatus) override;

    STDMETHOD(GetClipStatus)(
        _Out_ D3DCLIPSTATUS9* ClipStatus) override;

    STDMETHOD(GetTexture)(
        _In_ DWORD Stage,
        _Out_ IDirect3DBaseTexture9** Texture) override;

    STDMETHOD(SetTexture)(
        _In_ DWORD Stage,
        _In_opt_ IDirect3DBaseTexture9* Texture) override;

    STDMETHOD(GetTextureStageState)(
        _In_ DWORD Stage,
        _In_ D3DTEXTURESTAGESTATETYPE Type,
        _Out_ DWORD* Value) override;

    STDMETHOD(SetTextureStageState)(
        _In_ DWORD Stage,
        _In_ D3DTEXTURESTAGESTATETYPE Type,
        _In_ DWORD Value) override;

    STDMETHOD(GetSamplerState)(
        _In_ DWORD Sampler,
        _In_ D3DSAMPLERSTATETYPE Type,
        _Out_ DWORD* Value) override;

    STDMETHOD(SetSamplerState)(
        _In_ DWORD Sampler,
        _In_ D3DSAMPLERSTATETYPE Type,
        _In_ DWORD Value) override;

    STDMETHOD(ValidateDevice)(
        _In_opt_ DWORD* NumPasses) override;

    STDMETHOD(SetPaletteEntries)(
        _In_ UINT PaletteNumber,
        _In_opt_ const PALETTEENTRY* Entries) override;

    STDMETHOD(GetPaletteEntries)(
        _In_ UINT PaletteNumber,
        _Out_ PALETTEENTRY* Entries) override;

    STDMETHOD(SetCurrentTexturePalette)(
        _In_ UINT PaletteNumber) override;

    STDMETHOD(GetCurrentTexturePalette)(
        _Out_ UINT* PaletteNumber) override;

    STDMETHOD(SetScissorRect)(
        _In_opt_ const RECT* Rect) override;

    STDMETHOD(GetScissorRect)(
        _Out_ RECT* Rect) override;

    STDMETHOD(SetSoftwareVertexProcessing)(
        _In_ BOOL Software) override;

    STDMETHOD_(BOOL, GetSoftwareVertexProcessing)() override;

    STDMETHOD(SetNPatchMode)(
        _In_ FLOAT NSegments) override;

    STDMETHOD_(float, GetNPatchMode)() override;

    STDMETHOD(DrawPrimitive)(
        _In_ D3DPRIMITIVETYPE PrimitiveType,
        _In_ UINT StartVertex,
        _In_ UINT PrimitiveCount) override;

    STDMETHOD(DrawIndexedPrimitive)(
        _In_ D3DPRIMITIVETYPE PrimitiveType,
        _In_ INT BaseVertexIndex,
        _In_ UINT MinVertexIndex,
        _In_ UINT NumVertices,
        _In_ UINT StartIndex,
        _In_ UINT PrimCount) override;

    STDMETHOD(DrawPrimitiveUP)(
        _In_ D3DPRIMITIVETYPE PrimitiveType,
        _In_ UINT PrimitiveCount,
        _In_opt_ const VOID* VertexStreamZeroData,
        _In_ UINT VertexStreamZeroStride) override;

    STDMETHOD(DrawIndexedPrimitiveUP)(
        _In_ D3DPRIMITIVETYPE PrimitiveType,
        _In_ UINT MinVertexIndex,
        _In_ UINT NumVertices,
        _In_ UINT PrimitiveCount,
        _In_opt_ const VOID* IndexData,
        _In_ D3DFORMAT IndexDataFormat,
        _In_opt_ const VOID* VertexStreamZeroData,
        _In_ UINT VertexStreamZeroStride) override;

    STDMETHOD(ProcessVertices)(
        _In_ UINT SrcStartIndex,
        _In_ UINT DestIndex,
        _In_ UINT VertexCount,
        _In_opt_ IDirect3DVertexBuffer9* DestBuffer,
        _In_opt_ IDirect3DVertexDeclaration9* VertexDecl,
        _In_ DWORD Flags) override;

    STDMETHOD(CreateVertexDeclaration)(
        _In_opt_ const D3DVERTEXELEMENT9* VertexElements,
        _Out_ IDirect3DVertexDeclaration9** Decl) override;

    STDMETHOD(SetVertexDeclaration)(
        _In_opt_ IDirect3DVertexDeclaration9* Decl) override;

    STDMETHOD(GetVertexDeclaration)(
        _Out_ IDirect3DVertexDeclaration9** Decl) override;

    STDMETHOD(SetFVF)(
        _In_ DWORD FVF) override;

    STDMETHOD(GetFVF)(
        _Out_ DWORD* FVF) override;

    STDMETHOD(CreateVertexShader)(
        _In_opt_ const DWORD* Function,
        _Out_ IDirect3DVertexShader9** Shader) override;

    STDMETHOD(SetVertexShader)(
        _In_opt_ IDirect3DVertexShader9* Shader) override;

    STDMETHOD(GetVertexShader)(
        _Out_ IDirect3DVertexShader9** Shader) override;

    STDMETHOD(SetVertexShaderConstantF)(
        _In_ UINT StartRegister,
        _In_opt_ const float* ConstantData,
        _In_ UINT Vector4fCount) override;

    STDMETHOD(GetVertexShaderConstantF)(
        _In_ UINT StartRegister,
        _Out_ float* ConstantData,
        _In_ UINT Vector4fCount) override;

    STDMETHOD(SetVertexShaderConstantI)(
        _In_ UINT StartRegister,
        _In_opt_ const int* ConstantData,
        _In_ UINT Vector4iCount) override;

    STDMETHOD(GetVertexShaderConstantI)(
        _In_ UINT StartRegister,
        _Out_ int* ConstantData,
        _In_ UINT Vector4iCount) override;

    STDMETHOD(SetVertexShaderConstantB)(
        _In_ UINT StartRegister,
        _In_opt_ const BOOL* ConstantData,
        _In_ UINT BoolCount) override;

    STDMETHOD(GetVertexShaderConstantB)(
        _In_ UINT StartRegister,
        _Out_ BOOL* ConstantData,
        _In_ UINT BoolCount) override;

    STDMETHOD(SetStreamSource)(
        _In_ UINT StreamNumber,
        _In_opt_ IDirect3DVertexBuffer9* StreamData,
        _In_ UINT OffsetInBytes,
        _In_ UINT Stride) override;

    STDMETHOD(GetStreamSource)(
        _In_ UINT StreamNumber,
        _Out_ IDirect3DVertexBuffer9** StreamData,
        _Out_ UINT* OffsetInBytes,
        _Out_ UINT* Stride) override;

    STDMETHOD(SetStreamSourceFreq)(
        _In_ UINT StreamNumber,
        _In_ UINT Setting) override;

    STDMETHOD(GetStreamSourceFreq)(
        _In_ UINT StreamNumber,
        _Out_ UINT* Setting) override;

    STDMETHOD(SetIndices)(
        _In_opt_ IDirect3DIndexBuffer9* IndexData) override;

    STDMETHOD(GetIndices)(
        _Out_ IDirect3DIndexBuffer9** IndexData) override;

    STDMETHOD(CreatePixelShader)(
        _In_opt_ const DWORD* Function,
        _Out_ IDirect3DPixelShader9** Shader) override;

    STDMETHOD(SetPixelShader)(
        _In_opt_ IDirect3DPixelShader9* Shader) override;

    STDMETHOD(GetPixelShader)(
        _Out_ IDirect3DPixelShader9** Shader) override;

    STDMETHOD(SetPixelShaderConstantF)(
        _In_ UINT StartRegister,
        _In_opt_ const float* ConstantData,
        _In_ UINT Vector4fCount) override;

    STDMETHOD(GetPixelShaderConstantF)(
        _In_ UINT StartRegister,
        _Out_ float* ConstantData,
        _In_ UINT Vector4fCount) override;

    STDMETHOD(SetPixelShaderConstantI)(
        _In_ UINT StartRegister,
        _In_opt_ const int* ConstantData,
        _In_ UINT Vector4iCount) override;

    STDMETHOD(GetPixelShaderConstantI)(
        _In_ UINT StartRegister,
        _Out_ int* ConstantData,
        _In_ UINT Vector4iCount) override;

    STDMETHOD(SetPixelShaderConstantB)(
        _In_ UINT StartRegister,
        _In_opt_ const BOOL* ConstantData,
        _In_ UINT BoolCount) override;

    STDMETHOD(GetPixelShaderConstantB)(
        _In_ UINT StartRegister,
        _Out_ BOOL* ConstantData,
        _In_ UINT BoolCount) override;

    STDMETHOD(DrawRectPatch)(
        _In_ UINT Handle,
        _In_opt_ const float* NumSegs,
        _In_opt_ const D3DRECTPATCH_INFO* RectPatchInfo) override;

    STDMETHOD(DrawTriPatch)(
        _In_ UINT Handle,
        _In_opt_ const float* NumSegs,
        _In_opt_ const D3DTRIPATCH_INFO* TriPatchInfo) override;

    STDMETHOD(DeletePatch)(
        _In_ UINT Handle) override;

    STDMETHOD(CreateQuery)(
        _In_ D3DQUERYTYPE Type,
        _Out_ IDirect3DQuery9** Query) override;

    STDMETHOD(SetConvolutionMonoKernel)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_opt_ float* Rows,
        _In_opt_ float* Columns) override;

    STDMETHOD(ComposeRects)(
        _In_opt_ IDirect3DSurface9* Src,
        _In_opt_ IDirect3DSurface9* Dst,
        _In_opt_ IDirect3DVertexBuffer9* SrcRectDescs,
        _In_ UINT NumRects,
        _In_opt_ IDirect3DVertexBuffer9* DstRectDescs,
        _In_ D3DCOMPOSERECTSOP Operation,
        _In_ INT OffsetX,
        _In_ INT OffsetY) override;

    STDMETHOD(PresentEx)(
        _In_opt_ const RECT* SourceRect,
        _In_opt_ const RECT* DestRect,
        _In_opt_ HWND DestWindowOverride,
        _In_opt_ const RGNDATA* DirtyRegion,
        _In_ DWORD Flags) override;

    STDMETHOD(GetGPUThreadPriority)(
        _Out_ INT* Priority) override;

    STDMETHOD(SetGPUThreadPriority)(
        _In_ INT Priority) override;

    STDMETHOD(WaitForVBlank)(
        _In_ UINT SwapChain) override;

    STDMETHOD(CheckResourceResidency)(
        _In_ IDirect3DResource9** ResourceArray,
        _In_ UINT32 NumResources) override;

    STDMETHOD(SetMaximumFrameLatency)(
        _In_ UINT MaxLatency) override;

    STDMETHOD(GetMaximumFrameLatency)(
        _Out_ UINT* MaxLatency) override;

    STDMETHOD(CheckDeviceState)(
        _In_opt_ HWND DestinationWindow) override;

    STDMETHOD(CreateRenderTargetEx)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DMULTISAMPLE_TYPE MultiSample,
        _In_ DWORD MultisampleQuality,
        _In_ BOOL Lockable,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle,
        _In_ DWORD Usage) override;

    STDMETHOD(CreateOffscreenPlainSurfaceEx)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle,
        _In_ DWORD Usage) override;

    STDMETHOD(CreateDepthStencilSurfaceEx)(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DMULTISAMPLE_TYPE MultiSample,
        _In_ DWORD MultisampleQuality,
        _In_ BOOL Discard,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle,
        _In_ DWORD Usage) override;

    STDMETHOD(ResetEx)(
        _In_opt_ D3DPRESENT_PARAMETERS* PresentationParameters,
        _In_opt_ D3DDISPLAYMODEEX* FullscreenDisplayMode) override;

    STDMETHOD(GetDisplayModeEx)(
        _In_ UINT SwapChain,
        _Out_ D3DDISPLAYMODEEX* Mode,
        _Out_ D3DDISPLAYROTATION* Rotation) override;
private:
    HRESULT
    CreateKernelDevice(VOID);

    HRESULT
    OpenDriverAdapter(VOID);

    HRESULT
    CreateDriverDevice(
        _In_ BOOLEAN Windowed);

    VOID
    DestroyHardware(VOID);

    VOID
    ComputeCaps(VOID);

    VOID
    SetDefaultState(
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    HRESULT
    CreateAutoDepthStencil(
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    VOID
    BindRenderTargets(VOID);

    HRESULT
    ResetCommon(
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode);

    VOID
    FlushDriver(VOID);

    HRESULT
    DriverPresent(
        _In_opt_ HANDLE SourceResource,
        _In_opt_ HANDLE TargetResource);

    HRESULT
    OpenWindowSurface(
        _In_ D3DKMT_HANDLE Share,
        _In_ D3DDDIFORMAT Format);

    LONG RefCount = 1;
    CRITICAL_SECTION Lock;
    D9Enum* Enum;
    const D9Adapter* Adapter;
    D3DDEVTYPE DeviceType;
    HWND FocusWindow;
    DWORD BehaviorFlags;
    BOOLEAN IsEx;

    /* The kernel device and the driver's own adapter and device behind it */
    D3DKMT_HANDLE DeviceHandle = 0;
    D3DKMT_HANDLE ContextHandle = 0;
    D3DDDI_ADAPTERCALLBACKS AdapterCallbacks = {};
    D3DDDI_ADAPTERFUNCS AdapterFunctions = {};
    HANDLE DriverAdapter = NULL;
    UINT RuntimeVersion = 0;
    UINT DriverVersion = 0;
    D3DDDI_DEVICECALLBACKS DeviceCallbacks = {};
    D3DDDI_DEVICEFUNCS DeviceFunctions = {};
    HANDLE DriverDevice = NULL;
    BOOLEAN UsesFlush1 = FALSE;
    BOOLEAN Lost = FALSE;

    /* The present the driver is about to report through its callback */
    D3DKMT_PRESENT PendingPresent = {};
    HRESULT PresentResult = S_OK;
    D9Resource* WindowSurface = nullptr;
    D3DKMT_HANDLE WindowShare = 0;

    D3DCAPS9 Caps = {};
    D9SwapChain* ImplicitChain = nullptr;
    D9Surface* RenderTargets[D9MaxRenderTargets] = {};
    D9Surface* DepthStencil = nullptr;
    D9Surface* AutoDepthStencil = nullptr;
    DWORD RenderStates[D9RenderStateCount];
    INT GpuPriority = 0;
    UINT FrameLatency = 3;

    /* The driver's contexts in creation order; the kernel is told about them newest first */
    D3DKMT_HANDLE KernelContexts[D9MaxKernelContexts] = {};
    UINT KernelContextCount = 0;
};
