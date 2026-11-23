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

/* Presents handed to the driver that it has not reported through a callback yet */
static constexpr UINT D9QueuedPresentLimit = 8;

/* The vertex and index buffers a device keeps for drawing from application memory */
static constexpr UINT D9StagingBufferCount = 4;

/* Sixteen pixel samplers, the displacement map sampler and four vertex samplers */
static constexpr UINT D9TextureSlotCount = 21;

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
    UINT Fvf;

    /* Textures hand the driver every level, and cube maps every face, in one resource */
    UINT SurfaceCount;
    UINT MipLevels;
};

class D9TextureCore;

/* Managed resources keep a system memory copy and drop their video memory copy when evicted */
class D9ManagedResource
{
public:
    virtual
    VOID
    Evict(VOID) = 0;

    D9ManagedResource* NextManaged = nullptr;
};

/* Driver calls a batch records instead of making them */
enum class D9BatchCall : UINT
{
    RenderState,
    TextureStageState,
    PixelShaderConst,
    Viewport,
    ZRange,
    ScissorRect,
    ClipPlane,
    WInfo,
    RenderTarget,
    DepthStencil,
    Clear,
    Present1,
    Rename,
    Blt,
    ColorFill,
};

/* Slots of the table that remembers which batch last used a driver resource */
static constexpr UINT D9BatchReferenceSlots = 0x1FFF;

/* One recording buffer, with the system memory vertex and index buffers drawing from application memory fills */
struct D9BatchBuffer
{
    BYTE* Data;
    UINT Used;
    UINT Calls;
    D9Resource Vertices;
    D9Resource Indices16;
    D9Resource Indices32;
    VOID* Memory[3];
};

/* Records state calls and runs them on a worker thread for drivers that lock asynchronously, as the system does
   when the driver leaves threading to the runtime */
class D9Batch
{
public:
    D9Batch(
        _In_ D9Device* Owner,
        _In_ const D3DDDI_DEVICEFUNCS* Functions,
        _In_ HANDLE DriverDevice);

    ~D9Batch();

    D9Batch(const D9Batch&) = delete;
    D9Batch& operator=(const D9Batch&) = delete;

    HRESULT
    Start(VOID);

    VOID
    Stop(VOID);

    _Ret_maybenull_
    VOID*
    Record(
        _In_ D9BatchCall Call,
        _In_ UINT Size);

    VOID
    Submit(VOID);

    VOID
    Drain(VOID);

    VOID
    Reference(
        _In_opt_ HANDLE Resource);

    BOOLEAN
    IsReferenced(
        _In_ HANDLE Resource) const;

    VOID
    NoteBound(
        _In_ UINT Slot,
        _In_opt_ HANDLE Resource);

private:
    VOID
    Advance(VOID);

    static
    DWORD
    WINAPI
    WorkerMain(
        _In_ VOID* Context);

    VOID
    WaitForWorker(VOID);

    VOID
    Run(
        _Inout_ D9BatchBuffer* Buffer);

    HRESULT
    CreateFills(
        _Inout_ D9BatchBuffer* Buffer);

    VOID
    LockFills(
        _Inout_ D9BatchBuffer* Buffer);

    VOID
    DestroyFills(
        _Inout_ D9BatchBuffer* Buffer);

    D9Device* Device;
    const D3DDDI_DEVICEFUNCS* Funcs;
    HANDLE Driver;
    D9BatchBuffer Buffers[2] = {};
    UINT Current = 0;
    HANDLE Thread = NULL;
    HANDLE Wake = NULL;
    HANDLE Done = NULL;
    volatile LONG Busy = 0;
    BOOLEAN Pending = FALSE;
    BOOLEAN Exiting = FALSE;

    /* Each handed over or drained batch starts a generation; a resource remembers the last one that used it */
    UINT64 Generation = 0;
    BYTE References[D9BatchReferenceSlots] = {};

    /* The bound targets every new generation uses again, the depth buffer last */
    HANDLE Bound[D9MaxRenderTargets + 1] = {};
};

/* Data the application attaches to a resource under a GUID, copied or as an interface it holds */
class D9PrivateData
{
public:
    D9PrivateData() = default;

    ~D9PrivateData();

    D9PrivateData(const D9PrivateData&) = delete;
    D9PrivateData& operator=(const D9PrivateData&) = delete;

    HRESULT
    Set(
        _In_ REFGUID Guid,
        _In_reads_bytes_opt_(Size) const VOID* Data,
        _In_ DWORD Size,
        _In_ DWORD Flags);

    HRESULT
    Get(
        _In_ REFGUID Guid,
        _Out_writes_bytes_opt_(*Size) VOID* Data,
        _Inout_ DWORD* Size) const;

    HRESULT
    Free(
        _In_ REFGUID Guid);

private:
    struct Entry
    {
        GUID Guid;
        Entry* Next;
        DWORD Size;
        DWORD Flags;
        VOID* Data;
    };

    static
    VOID
    Empty(
        _Inout_ Entry* Item);

    Entry*
    Find(
        _In_ REFGUID Guid) const;

    Entry* First = nullptr;
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

/* A surface the device made: back buffers, the automatic depth buffer and the surfaces applications ask for */
class D9Surface final : public IDirect3DSurface9, public D9Child
{
public:
    D9Surface(
        _In_ D9Device* Owner,
        _In_opt_ IUnknown* Container,
        _In_ const D3DSURFACE_DESC* Desc,
        _In_ BOOLEAN Lockable,
        _In_ BOOLEAN Discard = FALSE,
        _In_ BOOLEAN Standalone = FALSE);

    /* A level or a face of a texture, which shares the texture's reference count and driver resource */
    D9Surface(
        _In_ D9TextureCore* Texture,
        _In_ UINT Subresource,
        _In_ const D3DSURFACE_DESC* Desc);

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
        return Resource;
    }

    VOID
    ExchangeResource(
        _Inout_ D9Resource** Held);

    /* The swap chain or device lets go; a surface the application still holds lives on by itself */
    VOID
    Abandon(VOID);

    BOOLEAN
    IsHeld() const
    {
        return RefCount > 0;
    }

    /* Default pool surfaces the application made keep the plain object's devices from resetting */
    VOID
    CountAsDefault(VOID);

    /* The driver resource copies name, and which of its subresources this surface is */
    D9Resource*
    CopyResource(
        _Out_ UINT* Index) const;

    /* The texture a level or face belongs to, or nothing for a surface of its own */
    D9TextureCore*
    OwnerTexture() const
    {
        return Texture;
    }

    /* Locked, or lending its memory to a device context */
    BOOLEAN
    IsBusy() const;

    BOOLEAN
    IsDiscardable() const
    {
        return Discard;
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
    HRESULT
    LockMemory(
        _Out_ D3DLOCKED_RECT* Locked,
        _In_opt_ const RECT* Rect,
        _In_ DWORD Flags);

    IUnknown* Container;
    D3DSURFACE_DESC Desc;
    D9Resource* Resource = nullptr;
    BOOLEAN Lockable;
    BOOLEAN Discard;
    BOOLEAN IsLocked = FALSE;
    DWORD ResourcePriority = 0;

    /* Surfaces the application made on their own go away with their last reference */
    BOOLEAN Standalone;

    /* System memory and scratch surfaces keep their pixels in memory of the runtime's own */
    BYTE* Memory = nullptr;
    UINT MemoryPitch = 0;

    /* Set for the levels and faces of a texture */
    D9TextureCore* Texture = nullptr;
    UINT Subresource = 0;

    BOOLEAN CountedDefault = FALSE;
    D9PrivateData Private;

    /* The device context GetDC handed out and the bitmap behind it */
    HDC HeldDc = NULL;
    HANDLE HeldBitmap = NULL;
};

/* A level of a volume texture */
class D9Volume final : public IDirect3DVolume9
{
public:
    D9Volume(
        _In_ D9TextureCore* Texture,
        _In_ UINT Subresource,
        _In_ const D3DVOLUME_DESC* Desc);

    D9Volume(const D9Volume&) = delete;
    D9Volume& operator=(const D9Volume&) = delete;

    /* IUnknown */
    STDMETHOD(QueryInterface)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Object) override;

    STDMETHOD_(ULONG, AddRef)() override;

    STDMETHOD_(ULONG, Release)() override;

    /* IDirect3DVolume9 */
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

    STDMETHOD(GetContainer)(
        _In_ REFIID Iid,
        _COM_Outptr_ VOID** Result) override;

    STDMETHOD(GetDesc)(
        _Out_ D3DVOLUME_DESC* Result) override;

    STDMETHOD(LockBox)(
        _Out_ D3DLOCKED_BOX* Locked,
        _In_opt_ const D3DBOX* Box,
        _In_ DWORD Flags) override;

    STDMETHOD(UnlockBox)() override;

private:
    D9TextureCore* Texture;
    UINT Subresource;
    D3DVOLUME_DESC Desc;
    D9PrivateData Private;
};

/* The shapes of texture Direct3D 9 knows */
enum class D9TextureKind : UINT
{
    Planar,
    Cube,
    Volume,
};

/* What one texture creation asked for once the device checked it */
struct D9TextureRequest
{
    D9TextureKind Kind;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT Levels;
    DWORD Usage;
    D3DFORMAT Format;
    D3DFORMAT DriverFormat;
    D3DPOOL Pool;

    /* The pool the texture reports, which keeps the extended object's spelling of the managed pool */
    D3DPOOL ReportedPool;
    BOOLEAN Lockable;
    BOOLEAN DriverAutogen;
};

/* Where one level or face of a texture lives */
struct D9Subresource
{
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT Pitch;
    UINT SlicePitch;
    SIZE_T Offset;
    BOOLEAN Locked;
};

/* Managed textures remember up to this many dirty regions per face before they count as wholly dirty */
static constexpr UINT D9DirtyRegionLimit = 6;

/* The state and behavior the three texture interfaces share */
class D9TextureCore : public D9Child, public D9ManagedResource
{
public:
    D9TextureCore(
        _In_ D9Device* Owner,
        _In_ const D9TextureRequest* Request);

    virtual ~D9TextureCore();

    D9TextureCore(const D9TextureCore&) = delete;
    D9TextureCore& operator=(const D9TextureCore&) = delete;

    HRESULT
    Create(VOID);

    /* The texture's own interface, the container its levels report */
    virtual IUnknown*
    Identity() = 0;

    ULONG
    AddRefTexture(VOID);

    ULONG
    ReleaseTexture(VOID);

    D9Device*
    Device9() const
    {
        return Device;
    }

    UINT
    LevelCount() const
    {
        return Levels;
    }

    UINT
    FaceCount() const
    {
        return Faces;
    }

    BOOLEAN
    IsManaged() const
    {
        return Pool == D3DPOOL_MANAGED;
    }

    HRESULT
    LockSubresource(
        _In_ UINT Index,
        _In_opt_ const D3DBOX* Box,
        _In_ DWORD Flags,
        _In_ BOOLEAN FromLevel,
        _Out_ D3DLOCKED_BOX* Locked);

    HRESULT
    UnlockSubresource(
        _In_ UINT Index,
        _In_ BOOLEAN FromLevel);

    /* The texture is bound to a stage; a texture the application let go of lives on while it is bound */
    VOID
    Bind(VOID);

    VOID
    Unbind(VOID);

    /* Managed textures drop their video copy, and make it again before use */
    VOID
    Evict(VOID) override;

    D9Resource*
    DriverResource() const
    {
        return Resource;
    }

    D3DPOOL
    PoolOf() const
    {
        return Pool;
    }

    DWORD
    UsageOf() const
    {
        return Usage;
    }

    BOOLEAN
    IsSubresourceLocked(
        _In_ UINT Index) const
    {
        return Subresources[Index].Locked;
    }

    BOOLEAN
    IsAnySubresourceLocked() const;

    /* A level lending its memory to a device context keeps every level of the texture from locking */
    VOID
    NoteDc(
        _In_ BOOLEAN Held)
    {
        DcHeld = Held;
    }

    /* UpdateTexture: copies what changed since the last copy into a texture of the same type */
    HRESULT
    CopyDirtyInto(
        _In_ D9TextureCore* Target);

    VOID
    Upload(VOID);

protected:
    HRESULT
    GetLevelSurface(
        _In_ UINT Face,
        _In_ UINT Level,
        _Outptr_ IUnknown** Result);

    HRESULT
    DescribeLevel(
        _In_ UINT Level,
        _Out_ D3DSURFACE_DESC* Desc) const;

    HRESULT
    AddDirtyRegion(
        _In_ UINT Face,
        _In_opt_ const D3DBOX* Box);

    DWORD
    SetPriorityCore(
        _In_ DWORD Priority);

    DWORD
    GetPriorityCore(VOID) const;

    DWORD
    SetLodCore(
        _In_ DWORD Lod);

    DWORD
    GetLodCore(VOID) const;

    HRESULT
    SetFilterCore(
        _In_ D3DTEXTUREFILTERTYPE Filter);

    VOID
    GenerateMipsCore(VOID);

    UINT
    SubresourceIndex(
        _In_ UINT Face,
        _In_ UINT Level) const
    {
        return Face * Levels + Level;
    }

    D9TextureKind Kind;
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT Levels;
    UINT Faces;
    DWORD Usage;
    D3DFORMAT Format;
    D3DFORMAT DriverFormat;
    D3DPOOL Pool;
    D3DPOOL ReportedPool;
    BOOLEAN Lockable;
    BOOLEAN DriverAutogen;
    D3DTEXTUREFILTERTYPE Filter;
    DWORD ResourcePriority = 0;
    DWORD Lod = 0;

    /* The driver resource: the texture itself, or the system memory copy of a managed texture */
    D9Resource* Resource = nullptr;

    /* The video memory copy of a managed texture, made when it is first used */
    D9Resource* VideoResource = nullptr;

    /* Memory textures keep every level and face in one block of runtime memory */
    BYTE* Memory = nullptr;
    D9Subresource* Subresources = nullptr;
    IUnknown** LevelObjects = nullptr;

    /* Dirty regions of each face's top level a managed texture copies on its next upload */
    UINT DirtyCount[6] = {};
    D3DBOX DirtyRegions[6][D9DirtyRegionLimit] = {};

    /* Stages the texture is bound to, and whether the application released it while bound */
    LONG BindCount = 0;
    BOOLEAN Orphaned = FALSE;
    BOOLEAN DcHeld = FALSE;

    D9PrivateData Private;

private:
    HRESULT
    LockMemorySubresource(
        _In_ UINT Index,
        _In_opt_ const D3DBOX* Box,
        _In_ DWORD Flags,
        _Out_ D3DLOCKED_BOX* Locked);

    VOID
    DestroyLevelObjects(VOID);
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

    VOID
    ReleaseBuffers(VOID);

    BOOLEAN
    BuffersHeld(VOID) const;

private:
    VOID
    RotateBuffers(VOID);

    UINT Index;
    HWND FocusWindow;
    D3DPRESENT_PARAMETERS Parameters = {};
    /* Flip swap chains keep one buffer more than they report, the one on screen */
    D9Surface* BackBuffers[D3DPRESENT_BACK_BUFFERS_MAX_EX + 1] = {};
    UINT BackBufferCount = 0;
};

/* Vertex and index buffers share everything but their description */
template <class Interface, class Description>
class D9Buffer final : public Interface, public D9Child, public D9ManagedResource
{
public:
    D9Buffer(
        _In_ D9Device* Owner,
        _In_ UINT Length,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ DWORD Fvf,
        _In_ D3DPOOL Pool);

    ~D9Buffer();

    D9Buffer(const D9Buffer&) = delete;
    D9Buffer& operator=(const D9Buffer&) = delete;

    HRESULT
    Create(
        _In_ D3DDDI_POOL Placement);

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

    /* IDirect3DVertexBuffer9 and IDirect3DIndexBuffer9 */
    STDMETHOD(Lock)(
        _In_ UINT OffsetToLock,
        _In_ UINT SizeToLock,
        _Outptr_ VOID** Data,
        _In_ DWORD Flags) override;

    STDMETHOD(Unlock)() override;

    STDMETHOD(GetDesc)(
        _Out_ Description* Result) override;

    VOID
    Evict(VOID) override;

private:
    BOOLEAN
    IsManaged() const
    {
        return Pool == D3DPOOL_MANAGED || Pool == static_cast<D3DPOOL>(6);
    }

    VOID
    Upload(VOID);

    BOOLEAN
    IsVertex() const
    {
        return Format == D3DFMT_VERTEXDATA;
    }

    D9Resource* Resource = nullptr;
    BYTE* Memory = nullptr;
    UINT Length;
    DWORD Usage;
    D3DFORMAT Format;
    DWORD Fvf;
    D3DPOOL Pool;
    DWORD ResourcePriority = 0;

    /* Buffers lock any number of times; every unlock the driver accepts takes one away */
    LONG LockCount = 0;

    /* Set while the buffer is locked through the driver's asynchronous lock */
    BOOLEAN LockedAsync = FALSE;

    D9PrivateData Private;

    /* A managed buffer's video memory copy, and the byte range its next upload copies */
    D9Resource* VideoResource = nullptr;
    UINT DirtyStart = 0;
    UINT DirtyEnd = 0;
};

typedef D9Buffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC> D9VertexBuffer;
typedef D9Buffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC> D9IndexBuffer;

/* surface.cpp */
UINT64
D9MemoryPitch(
    _In_ D3DFORMAT Format,
    _In_ UINT Width);

UINT
D9MemoryRows(
    _In_ D3DFORMAT Format,
    _In_ UINT Height);

BOOLEAN
D9IsValidRect(
    _In_ D3DFORMAT Format,
    _In_ UINT Width,
    _In_ UINT Height,
    _In_ const RECT* Rect);

BOOLEAN
D9LockFlagsAllowed(
    _In_ DWORD Flags);

/* device.cpp */
BOOLEAN
D9IsHiddenDepth(
    _In_ D3DFORMAT Format);

BOOLEAN
D9IsDxtFormat(
    _In_ D3DFORMAT Format);

BOOLEAN
D9HasStencil(
    _In_ D3DFORMAT Format);

/* present.cpp */
HRESULT
D9CheckOcclusion(
    _In_ HWND Window);

/* Holds the device lock for applications that asked for a multithreaded device */
class D9DeviceLock
{
public:
    D9DeviceLock(
        _In_ CRITICAL_SECTION* Section,
        _In_ BOOLEAN Needed)
        : Held(Needed ? Section : nullptr)
    {
        if (Held != nullptr)
            EnterCriticalSection(Held);
    }

    ~D9DeviceLock()
    {
        if (Held != nullptr)
            LeaveCriticalSection(Held);
    }

    D9DeviceLock(const D9DeviceLock&) = delete;
    D9DeviceLock& operator=(const D9DeviceLock&) = delete;

private:
    CRITICAL_SECTION* Held;
};

#define D9_DEVICE_LOCK() D9DeviceLock Scope(&Lock, (BehaviorFlags & D3DCREATE_MULTITHREADED) != 0)

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
        _In_ BOOLEAN IsEx,
        _In_ BOOLEAN SwapEffectUpgrade);

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
        _In_ BOOLEAN Discard,
        _In_ D3DDDI_POOL DriverPool,
        _In_opt_ VOID* Memory,
        _In_ UINT MemoryPitch,
        _Out_ D9Resource* Resource);

    HRESULT
    CreateDriverBuffer(
        _In_ D3DDDIFORMAT Format,
        _In_ UINT Size,
        _In_ UINT Flags,
        _Out_ D9Resource* Resource);

    HRESULT
    CreateSystemBuffer(
        _In_ D3DDDIFORMAT Format,
        _In_ UINT Size,
        _In_ UINT Flags,
        _In_ VOID* Memory,
        _Out_ D9Resource* Resource);

    /* Driver calls that a batch may record; the rest wait for it to empty first */
    VOID
    DrainBatch(VOID);

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

    /* Textures: one driver resource holding every level and face */
    HRESULT
    CreateTextureResource(
        _In_reads_(Count) const D3DDDI_SURFACEINFO* Surfaces,
        _In_ UINT Count,
        _In_ UINT MipLevels,
        _Inout_ D9Resource* Resource);

    HRESULT
    LockDriverSubresource(
        _In_ D9Resource* Resource,
        _In_ UINT Index,
        _In_opt_ const D3DBOX* Box,
        _In_ BOOLEAN IsVolume,
        _In_ DWORD Flags,
        _Out_ D3DLOCKED_BOX* Locked);

    HRESULT
    UnlockDriverSubresource(
        _In_ D9Resource* Resource,
        _In_ UINT Index);

    VOID
    DriverTexBlt(
        _In_ D9Resource* Destination,
        _In_ D9Resource* Source,
        _In_ UINT Face,
        _In_ const D3DBOX* Box);

    VOID
    DriverVolBlt(
        _In_ D9Resource* Destination,
        _In_ D9Resource* Source,
        _In_ const D3DBOX* Box);

    VOID
    DriverGenerateMips(
        _In_ D9Resource* Resource,
        _In_ D3DTEXTUREFILTERTYPE Filter);

    VOID
    DriverSetPriority(
        _In_ D9Resource* Resource,
        _In_ DWORD Priority);

    /* Surface copies and fills, which a batch records */
    HRESULT
    DriverBlt(
        _In_ const D3DDDIARG_BLT* Args);

    HRESULT
    DriverColorFill(
        _In_ const D3DDDIARG_COLORFILL* Args);

    /* What the kernel last said about the device running, as the error a call returns */
    HRESULT
    ExecutionStatus(VOID);

    /* The display GDI device contexts over surface memory are made for */
    PCSTR
    DisplayName() const
    {
        return Adapter->DeviceName;
    }

    const D3DCAPS9&
    DeviceCaps() const
    {
        return Caps;
    }

    D3DDEVTYPE
    Type() const
    {
        return DeviceType;
    }

    /* The lock texture methods take, held only for multithreaded devices */
    CRITICAL_SECTION*
    LockSection()
    {
        return &Lock;
    }

    BOOLEAN
    IsMultithreaded() const
    {
        return (BehaviorFlags & D3DCREATE_MULTITHREADED) != 0;
    }

    /* Priority is kept for managed resources, and handed to the driver for default pool ones on extended devices */
    DWORD
    ChangePriority(
        _In_opt_ D9Resource* Resource,
        _In_ D3DPOOL Pool,
        _Inout_ DWORD* Stored,
        _In_ DWORD Priority);

    DWORD
    ReadPriority(
        _In_ D3DPOOL Pool,
        _In_ DWORD Stored) const;

    /* Counts the default pool resources the application made */
    VOID
    NoteDefaultResource(
        _In_ BOOLEAN Made)
    {
        if (Made)
            InterlockedIncrement(&DefaultResources);
        else
            InterlockedDecrement(&DefaultResources);
    }

    VOID
    TrackManaged(
        _In_ D9ManagedResource* Resource);

    VOID
    ForgetManaged(
        _In_ D9ManagedResource* Resource);

    VOID
    DriverBufBlt(
        _In_ D9Resource* Destination,
        _In_ D9Resource* Source,
        _In_ UINT Offset,
        _In_ UINT Size);

    D3DDDI_POOL
    BufferPlacement(
        _In_ BOOLEAN IsVertex,
        _In_ DWORD Usage,
        _In_ DWORD Fvf,
        _In_ D3DPOOL Pool) const;

    HRESULT
    CreateBufferResource(
        _In_ D3DFORMAT Format,
        _In_ UINT Length,
        _In_ UINT Flags,
        _In_ DWORD Fvf,
        _In_ D3DDDI_POOL Placement,
        _In_opt_ VOID* Memory,
        _Out_ D9Resource* Resource);

    HRESULT
    LockDriverBuffer(
        _In_ D9Resource* Resource,
        _In_ UINT Offset,
        _In_ UINT Size,
        _In_ BOOLEAN Ranged,
        _In_ DWORD Flags,
        _In_ UINT LockCount,
        _Inout_ BOOLEAN* Asynchronous,
        _Outptr_ VOID** Data);

    HRESULT
    UnlockDriverBuffer(
        _In_ D9Resource* Resource,
        _In_ BOOLEAN Asynchronous);

    HRESULT
    PresentToWindow(
        _In_ D9Resource* Source,
        _In_ HWND Window,
        _In_ const RECT* From,
        _In_ const RECT* To);

    VOID
    ReleaseWindowSurface(VOID);

    /* A flip moved the surfaces behind the bound targets */
    VOID
    NoteTargetsMoved(VOID)
    {
        TargetsDirty = TRUE;
    }

    VOID
    FlushDriver(VOID);

    HRESULT
    SetDisplayModeCallback(
        _Inout_ D3DDDICB_SETDISPLAYMODE* Args);

    HRESULT
    PresentCallback(
        _Inout_ D3DDDICB_PRESENT* Args);

    HRESULT
    NotePresentStatus(
        _In_ NTSTATUS Status);

    HRESULT
    SubmitPresentCallback(
        _Inout_ D3DDDICB_SUBMITPRESENTTOHWQUEUE* Args);

    VOID
    QueuePendingPresent(VOID);

    BOOLEAN
    TakeQueuedPresent(
        _Out_ D3DKMT_PRESENT* Present);

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
    ReadDriverAnswers(VOID);

    HRESULT
    QueryDeviceState(
        _In_opt_ HWND DestinationWindow);

    VOID
    ReleaseVidPnOwnership(VOID);

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

    DWORD
    InitialStateValue(
        _In_ UINT State) const;

    VOID
    SendRenderState(
        _In_ UINT State);

    VOID
    SendTextureStageState(
        _In_ UINT Stage,
        _In_ UINT State,
        _In_ UINT Value);

    VOID
    SendPixelConstant(
        _In_ UINT Register,
        _In_ FLOAT X,
        _In_ FLOAT Y,
        _In_ FLOAT Z,
        _In_ FLOAT W);

    VOID
    SendClipPlanes(
        _In_ BOOLEAN Twice);

    VOID
    SendInitialState(VOID);

    HRESULT
    CreateAutoDepthStencil(
        _In_ const D3DPRESENT_PARAMETERS* Parameters);

    VOID
    BindRenderTargets(VOID);

    VOID
    SendTargets(VOID);

    VOID
    SendViewport(VOID);

    VOID
    SendViewportRange(VOID);

    BOOLEAN
    WantsBatch(VOID) const;

    VOID
    StartBatch(VOID);

    VOID
    StopBatch(VOID);

    VOID
    DdiRenderState(
        _In_ const D3DDDIARG_RENDERSTATE* Args);

    VOID
    DdiTextureStageState(
        _In_ const D3DDDIARG_TEXTURESTAGESTATE* Args);

    VOID
    DdiPixelShaderConst(
        _In_ const D3DDDIARG_SETPIXELSHADERCONST* Args,
        _In_reads_(Args->Count * 4) const FLOAT* Data);

    VOID
    DdiViewport(
        _In_ const D3DDDIARG_VIEWPORTINFO* Args);

    VOID
    DdiZRange(
        _In_ const D3DDDIARG_ZRANGE* Args);

    VOID
    DdiScissorRect(
        _In_ const RECT* Rect);

    VOID
    DdiClipPlane(
        _In_ const D3DDDIARG_SETCLIPPLANE* Args);

    VOID
    DdiWInfo(
        _In_ const D3DDDIARG_WINFO* Args);

    VOID
    DdiRenderTarget(
        _In_ const D3DDDIARG_SETRENDERTARGET* Args);

    VOID
    DdiDepthStencil(
        _In_ const D3DDDIARG_SETDEPTHSTENCIL* Args);

    HRESULT
    DdiClear(
        _In_ const D3DDDIARG_CLEAR* Args,
        _In_ UINT Count,
        _In_reads_(Count) const RECT* Rects);

    BOOLEAN
    DdiPresent1(
        _In_ const D3DDDIARG_PRESENT1* Args);

    HRESULT
    CreateDriverResource(
        _Inout_ D9Resource* Resource,
        _In_ const D3DDDI_SURFACEINFO* Surface,
        _In_ D3DDDIMULTISAMPLE_TYPE MultisampleType,
        _In_ UINT MultisampleQuality);

    HRESULT
    CheckSurfaceFormat(
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format);

    HRESULT
    CheckProtectedContent(VOID);

    HRESULT
    CreateTargetSurface(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ DWORD Usage,
        _In_ D3DMULTISAMPLE_TYPE MultiSample,
        _In_ DWORD MultisampleQuality,
        _In_ BOOLEAN Lockable,
        _In_ BOOLEAN Discard,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle);

    HRESULT
    CreatePlainSurface(
        _In_ UINT Width,
        _In_ UINT Height,
        _In_ D3DFORMAT Format,
        _In_ D3DPOOL Pool,
        _In_ DWORD Usage,
        _Out_ IDirect3DSurface9** Surface,
        _Inout_opt_ HANDLE* SharedHandle);

    HRESULT
    AddSurface(
        _In_ const D3DSURFACE_DESC* Desc,
        _In_ BOOLEAN Lockable,
        _In_ BOOLEAN Discard,
        _Out_ IDirect3DSurface9** Surface);

    HRESULT
    LockAsyncBuffer(
        _In_ D9Resource* Resource,
        _In_ const D3DDDIARG_LOCK* Request,
        _In_ UINT LockCount,
        _Outptr_ VOID** Data);

    template <class Buffer>
    HRESULT
    AddBuffer(
        _In_ UINT Length,
        _In_ DWORD Usage,
        _In_ D3DFORMAT Format,
        _In_ DWORD Fvf,
        _In_ D3DPOOL Pool,
        _In_ D3DPOOL Placed,
        _Outptr_ Buffer** Made);

    HRESULT
    CheckTextureRequest(
        _Inout_ D9TextureRequest* Request,
        _In_opt_ HANDLE* SharedHandle);

    VOID
    UnbindTextures(VOID);

    HRESULT
    CreateStagingBuffers(VOID);

    VOID
    DestroyStagingBuffers(VOID);

    HRESULT
    ResetCommon(
        _Inout_ D3DPRESENT_PARAMETERS* Parameters,
        _In_opt_ const D3DDISPLAYMODEEX* FullscreenMode);

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

    /* Set when the device presents through the upgraded flip model instead of the legacy kernel mode */
    BOOLEAN SwapEffectUpgrade;

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

    /* The last execution state the kernel reported, and whether a display mode change was seen */
    D3DKMT_DEVICEEXECUTION_STATE ExecutionState = static_cast<D3DKMT_DEVICEEXECUTION_STATE>(0);
    BOOLEAN ModeChanged = FALSE;

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

    /* Copies of PendingPresent the driver may report later from its own thread */
    SRWLOCK PresentQueueLock = SRWLOCK_INIT;
    D3DKMT_PRESENT QueuedPresents[D9QueuedPresentLimit] = {};
    UINT QueuedPresentFirst = 0;
    UINT QueuedPresentCount = 0;

    /* The bound targets reach the driver before the first operation that writes them */
    BOOLEAN TargetsDirty = FALSE;
    D9Resource StagingBuffers[D9StagingBufferCount] = {};

    /* Content protection caps the driver reported, asked for once when a surface first wants them */
    BOOLEAN ProtectionQueried = FALSE;
    BOOLEAN ProtectionSupported = FALSE;

    /* Records driver calls for a worker thread when the driver leaves threading to the runtime */
    D9Batch* Batch = nullptr;

    /* The driver's contexts in creation order; the kernel is told about them newest first */
    D3DKMT_HANDLE KernelContexts[D9MaxKernelContexts] = {};
    UINT KernelContextCount = 0;

    /* Textures bound to the pixel samplers, the displacement map sampler and the vertex samplers */
    IDirect3DBaseTexture9* Textures[D9TextureSlotCount] = {};

    /* Managed textures and buffers, which EvictManagedResources walks */
    D9ManagedResource* ManagedResources = nullptr;

    /* Default pool resources the application holds, and a failed reset the device waits to see undone */
    LONG DefaultResources = 0;
    BOOLEAN NotReset = FALSE;

    /* Between BeginScene and EndScene */
    BOOLEAN InScene = FALSE;
};
